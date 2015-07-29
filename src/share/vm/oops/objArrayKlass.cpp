/*
 * Copyright (c) 1997, 2015, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "gc/shared/specialized_oop_closures.hpp"
#include "memory/iterator.inline.hpp"
#include "memory/metadataFactory.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.inline.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "oops/objArrayKlass.inline.hpp"
#include "oops/objArrayOop.inline.hpp"
#include "oops/oop.inline.hpp"
#include "oops/symbol.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/orderAccess.inline.hpp"
#include "utilities/copy.hpp"
#include "utilities/macros.hpp"

#if defined (PROFILE_OBJECT_INFO) || defined (PROFILE_OBJECT_ADDRESS_INFO)
#include "memory/heapInspection.hpp"
#endif

ObjArrayKlass* ObjArrayKlass::allocate(ClassLoaderData* loader_data, int n, KlassHandle klass_handle, Symbol* name, TRAPS) {
  assert(ObjArrayKlass::header_size() <= InstanceKlass::header_size(),
      "array klasses must be same size as InstanceKlass");

  int size = ArrayKlass::static_size(ObjArrayKlass::header_size());

  return new (loader_data, size, THREAD) ObjArrayKlass(n, klass_handle, name);
}

Klass* ObjArrayKlass::allocate_objArray_klass(ClassLoaderData* loader_data,
                                                int n, KlassHandle element_klass, TRAPS) {

  // Eagerly allocate the direct array supertype.
  KlassHandle super_klass = KlassHandle();
  if (!Universe::is_bootstrapping() || SystemDictionary::Object_klass_loaded()) {
    KlassHandle element_super (THREAD, element_klass->super());
    if (element_super.not_null()) {
      // The element type has a direct super.  E.g., String[] has direct super of Object[].
      super_klass = KlassHandle(THREAD, element_super->array_klass_or_null());
      bool supers_exist = super_klass.not_null();
      // Also, see if the element has secondary supertypes.
      // We need an array type for each.
      Array<Klass*>* element_supers = element_klass->secondary_supers();
      for( int i = element_supers->length()-1; i >= 0; i-- ) {
        Klass* elem_super = element_supers->at(i);
        if (elem_super->array_klass_or_null() == NULL) {
          supers_exist = false;
          break;
        }
      }
      if (!supers_exist) {
        // Oops.  Not allocated yet.  Back out, allocate it, and retry.
        KlassHandle ek;
        {
          MutexUnlocker mu(MultiArray_lock);
          MutexUnlocker mc(Compile_lock);   // for vtables
          Klass* sk = element_super->array_klass(CHECK_0);
          super_klass = KlassHandle(THREAD, sk);
          for( int i = element_supers->length()-1; i >= 0; i-- ) {
            KlassHandle elem_super (THREAD, element_supers->at(i));
            elem_super->array_klass(CHECK_0);
          }
          // Now retry from the beginning
          Klass* klass_oop = element_klass->array_klass(n, CHECK_0);
          // Create a handle because the enclosing brace, when locking
          // can cause a gc.  Better to have this function return a Handle.
          ek = KlassHandle(THREAD, klass_oop);
        }  // re-lock
        return ek();
      }
    } else {
      // The element type is already Object.  Object[] has direct super of Object.
      super_klass = KlassHandle(THREAD, SystemDictionary::Object_klass());
    }
  }

  // Create type name for klass.
  Symbol* name = NULL;
  if (!element_klass->oop_is_instance() ||
      (name = InstanceKlass::cast(element_klass())->array_name()) == NULL) {

    ResourceMark rm(THREAD);
    char *name_str = element_klass->name()->as_C_string();
    int len = element_klass->name()->utf8_length();
    char *new_str = NEW_RESOURCE_ARRAY(char, len + 4);
    int idx = 0;
    new_str[idx++] = '[';
    if (element_klass->oop_is_instance()) { // it could be an array or simple type
      new_str[idx++] = 'L';
    }
    memcpy(&new_str[idx], name_str, len * sizeof(char));
    idx += len;
    if (element_klass->oop_is_instance()) {
      new_str[idx++] = ';';
    }
    new_str[idx++] = '\0';
    name = SymbolTable::new_permanent_symbol(new_str, CHECK_0);
    if (element_klass->oop_is_instance()) {
      InstanceKlass* ik = InstanceKlass::cast(element_klass());
      ik->set_array_name(name);
    }
  }

  // Initialize instance variables
  ObjArrayKlass* oak = ObjArrayKlass::allocate(loader_data, n, element_klass, name, CHECK_0);

  // Add all classes to our internal class loader list here,
  // including classes in the bootstrap (NULL) class loader.
  // GC walks these as strong roots.
  loader_data->add_class(oak);

  // Call complete_create_array_klass after all instance variables has been initialized.
  ArrayKlass::complete_create_array_klass(oak, super_klass, CHECK_0);

  return oak;
}

ObjArrayKlass::ObjArrayKlass(int n, KlassHandle element_klass, Symbol* name) : ArrayKlass(name) {
  this->set_dimension(n);
  this->set_element_klass(element_klass());
  // decrement refcount because object arrays are not explicitly freed.  The
  // InstanceKlass array_name() keeps the name counted while the klass is
  // loaded.
  name->decrement_refcount();

  Klass* bk;
  if (element_klass->oop_is_objArray()) {
    bk = ObjArrayKlass::cast(element_klass())->bottom_klass();
  } else {
    bk = element_klass();
  }
  assert(bk != NULL && (bk->oop_is_instance() || bk->oop_is_typeArray()), "invalid bottom klass");
  this->set_bottom_klass(bk);
  this->set_class_loader_data(bk->class_loader_data());

  this->set_layout_helper(array_layout_helper(T_OBJECT));
  assert(this->oop_is_array(), "sanity");
  assert(this->oop_is_objArray(), "sanity");
}

int ObjArrayKlass::oop_size(oop obj) const {
  assert(obj->is_objArray(), "must be object array");
  return objArrayOop(obj)->object_size();
}

objArrayOop ObjArrayKlass::allocate(int length, TRAPS) {
  if (length >= 0) {
    if (length <= arrayOopDesc::max_array_length(T_OBJECT)) {
      int size = objArrayOopDesc::object_size(length);
      KlassHandle h_k(THREAD, this);
      return (objArrayOop)CollectedHeap::array_allocate(h_k, size, length, THREAD);
    } else {
      report_java_out_of_memory("Requested array size exceeds VM limit");
      JvmtiExport::post_array_size_exhausted();
      THROW_OOP_0(Universe::out_of_memory_error_array_size());
    }
  } else {
    THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  }
}

objArrayOop objArrayKlass::allocate(int length, HeapColor color, TRAPS) {
  if (length >= 0) {
    if (length <= arrayOopDesc::max_array_length(T_OBJECT)) {
      int size = objArrayOopDesc::object_size(length);
      KlassHandle h_k(THREAD, as_klassOop());
      objArrayOop a = (objArrayOop)CollectedHeap::array_allocate(h_k, size, length, color, CHECK_NULL);
      assert(a->is_parsable(), "Can't publish unless parsable");
      return a;
    } else {
      report_java_out_of_memory("Requested array size exceeds VM limit");
      THROW_OOP_0(Universe::out_of_memory_error_array_size());
    }
  } else {
    THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  }
}

static int multi_alloc_counter = 0;

oop ObjArrayKlass::multi_allocate(int rank, jint* sizes, TRAPS) {
  int length = *sizes;
  // Call to lower_dimension uses this pointer, so most be called before a
  // possible GC
  KlassHandle h_lower_dimension(THREAD, lower_dimension());
  // If length < 0 allocate will throw an exception.
  objArrayOop array = allocate(length, CHECK_NULL);
  objArrayHandle h_array (THREAD, array);
  if (rank > 1) {
    if (length != 0) {
      for (int index = 0; index < length; index++) {
        ArrayKlass* ak = ArrayKlass::cast(h_lower_dimension());
        oop sub_array = ak->multi_allocate(rank-1, &sizes[1], CHECK_NULL);
#ifdef PROFILE_OBJECT_INFO
        if (ProfileObjectInfo) {
          JavaThread *thread = (JavaThread*)THREAD;
          methodOop method = thread->last_frame().interpreter_frame_method();
          int bci = thread->last_frame().interpreter_frame_bci();
          SharedRuntime::profile_object_alloc(sub_array, method, bci);
        }
#endif
#ifdef PROFILE_OBJECT_ADDRESS_INFO
        if (ProfileObjectAddressInfo) {
          SharedRuntime::profile_object_address_alloc(sub_array);
        }
#endif
        h_array->obj_at_put(index, sub_array);
      }
    } else {
      // Since this array dimension has zero length, nothing will be
      // allocated, however the lower dimension values must be checked
      // for illegal values.
      for (int i = 0; i < rank - 1; ++i) {
        sizes += 1;
        if (*sizes < 0) {
          THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
        }
      }
    }
  }
  return h_array();
}

oop objArrayKlass::multi_allocate(int rank, jint* sizes, HeapColor color, TRAPS) {
  int length = *sizes;
  // Call to lower_dimension uses this pointer, so most be called before a
  // possible GC
  KlassHandle h_lower_dimension(THREAD, lower_dimension());
  // If length < 0 allocate will throw an exception.
  objArrayOop array = allocate(length, color, CHECK_NULL);
  assert(array->is_parsable(), "Don't handlize unless parsable");
  objArrayHandle h_array (THREAD, array);
  if (rank > 1) {
    if (length != 0) {
      for (int index = 0; index < length; index++) {
        arrayKlass* ak = arrayKlass::cast(h_lower_dimension());
        oop sub_array = ak->multi_allocate(rank-1, &sizes[1], color, CHECK_NULL);
        assert(sub_array->is_parsable(), "Don't publish until parsable");
#ifdef PROFILE_OBJECT_INFO
        if (ProfileObjectInfo) {
          JavaThread *thread = (JavaThread*)THREAD;
          methodOop method = thread->last_frame().interpreter_frame_method();
          int bci = thread->last_frame().interpreter_frame_bci();
          SharedRuntime::profile_object_alloc(sub_array, method, bci);
        }
#endif
#ifdef PROFILE_OBJECT_ADDRESS_INFO
        if (ProfileObjectAddressInfo) {
          SharedRuntime::profile_object_address_alloc(sub_array);
        }
#endif
        h_array->obj_at_put(index, sub_array);
      }
    } else {
      // Since this array dimension has zero length, nothing will be
      // allocated, however the lower dimension values must be checked
      // for illegal values.
      for (int i = 0; i < rank - 1; ++i) {
        sizes += 1;
        if (*sizes < 0) {
          THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
        }
      }
    }
  }
  return h_array();
}

// Either oop or narrowOop depending on UseCompressedOops.
template <class T> void ObjArrayKlass::do_copy(arrayOop s, T* src,
                               arrayOop d, T* dst, int length, TRAPS) {

  BarrierSet* bs = Universe::heap()->barrier_set();
  // For performance reasons, we assume we are that the write barrier we
  // are using has optimized modes for arrays of references.  At least one
  // of the asserts below will fail if this is not the case.
  assert(bs->has_write_ref_array_opt(), "Barrier set must have ref array opt");
  assert(bs->has_write_ref_array_pre_opt(), "For pre-barrier as well.");

  if (s == d) {
    // since source and destination are equal we do not need conversion checks.
    assert(length > 0, "sanity check");
    bs->write_ref_array_pre(dst, length);
    Copy::conjoint_oops_atomic(src, dst, length);
  } else {
    // We have to make sure all elements conform to the destination array
    Klass* bound = ObjArrayKlass::cast(d->klass())->element_klass();
    Klass* stype = ObjArrayKlass::cast(s->klass())->element_klass();
    if (stype == bound || stype->is_subtype_of(bound)) {
      // elements are guaranteed to be subtypes, so no check necessary
      bs->write_ref_array_pre(dst, length);
      Copy::conjoint_oops_atomic(src, dst, length);
    } else {
      // slow case: need individual subtype checks
      // note: don't use obj_at_put below because it includes a redundant store check
      T* from = src;
      T* end = from + length;
      for (T* p = dst; from < end; from++, p++) {
        // XXX this is going to be slow.
        T element = *from;
        // even slower now
        bool element_is_null = oopDesc::is_null(element);
        oop new_val = element_is_null ? oop(NULL)
                                      : oopDesc::decode_heap_oop_not_null(element);
        if (element_is_null ||
            (new_val->klass())->is_subtype_of(bound)) {
          bs->write_ref_field_pre(p, new_val);
          *p = element;
        } else {
          // We must do a barrier to cover the partial copy.
          const size_t pd = pointer_delta(p, dst, (size_t)heapOopSize);
          // pointer delta is scaled to number of elements (length field in
          // objArrayOop) which we assume is 32 bit.
          assert(pd == (size_t)(int)pd, "length field overflow");
          bs->write_ref_array((HeapWord*)dst, pd);
          THROW(vmSymbols::java_lang_ArrayStoreException());
          return;
        }
      }
    }
  }
  bs->write_ref_array((HeapWord*)dst, length);
}

void ObjArrayKlass::copy_array(arrayOop s, int src_pos, arrayOop d,
                               int dst_pos, int length, TRAPS) {
  assert(s->is_objArray(), "must be obj array");

  if (!d->is_objArray()) {
    THROW(vmSymbols::java_lang_ArrayStoreException());
  }

  // Check is all offsets and lengths are non negative
  if (src_pos < 0 || dst_pos < 0 || length < 0) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  // Check if the ranges are valid
  if  ( (((unsigned int) length + (unsigned int) src_pos) > (unsigned int) s->length())
     || (((unsigned int) length + (unsigned int) dst_pos) > (unsigned int) d->length()) ) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }

  // Special case. Boundary cases must be checked first
  // This allows the following call: copy_array(s, s.length(), d.length(), 0).
  // This is correct, since the position is supposed to be an 'in between point', i.e., s.length(),
  // points to the right of the last element.
  if (length==0) {
    return;
  }

#ifdef PROFILE_OBJECT_INFO
  if (ProfileObjectInfo) {
    ResourceMark rm;
    if (s->poi()) {
      s->poi()->batch_mark_load(length);
    }
    if (d->poi()) {
      d->poi()->batch_mark_store(length);
    }
  }
#endif
#ifdef PROFILE_OBJECT_ADDRESS_INFO
  if (ProfileObjectAddressInfo) {
    ObjectAddressInfoTable *oait = Universe::object_address_info_table();
    oait->batch_mark_load (s, length);
    oait->batch_mark_store(d, length);
  }
#endif

  if (UseCompressedOops) {
    narrowOop* const src = objArrayOop(s)->obj_at_addr<narrowOop>(src_pos);
    narrowOop* const dst = objArrayOop(d)->obj_at_addr<narrowOop>(dst_pos);
    do_copy<narrowOop>(s, src, d, dst, length, CHECK);
  } else {
    oop* const src = objArrayOop(s)->obj_at_addr<oop>(src_pos);
    oop* const dst = objArrayOop(d)->obj_at_addr<oop>(dst_pos);
    do_copy<oop> (s, src, d, dst, length, CHECK);
  }
}


Klass* ObjArrayKlass::array_klass_impl(bool or_null, int n, TRAPS) {

  assert(dimension() <= n, "check order of chain");
  int dim = dimension();
  if (dim == n) return this;

  if (higher_dimension() == NULL) {
    if (or_null)  return NULL;

    ResourceMark rm;
    JavaThread *jt = (JavaThread *)THREAD;
    {
      MutexLocker mc(Compile_lock, THREAD);   // for vtables
      // Ensure atomic creation of higher dimensions
      MutexLocker mu(MultiArray_lock, THREAD);

      // Check if another thread beat us
      if (higher_dimension() == NULL) {

        // Create multi-dim klass object and link them together
        Klass* k =
          ObjArrayKlass::allocate_objArray_klass(class_loader_data(), dim + 1, this, CHECK_NULL);
        ObjArrayKlass* ak = ObjArrayKlass::cast(k);
        ak->set_lower_dimension(this);
        OrderAccess::storestore();
        set_higher_dimension(ak);
        assert(ak->oop_is_objArray(), "incorrect initialization of ObjArrayKlass");
      }
    }
  } else {
    CHECK_UNHANDLED_OOPS_ONLY(Thread::current()->clear_unhandled_oops());
  }

  ObjArrayKlass *ak = ObjArrayKlass::cast(higher_dimension());
  if (or_null) {
    return ak->array_klass_or_null(n);
  }
  return ak->array_klass(n, THREAD);
}

Klass* ObjArrayKlass::array_klass_impl(bool or_null, TRAPS) {
  return array_klass_impl(or_null, dimension() +  1, THREAD);
}

bool ObjArrayKlass::can_be_primary_super_slow() const {
  if (!bottom_klass()->can_be_primary_super())
    // array of interfaces
    return false;
  else
    return Klass::can_be_primary_super_slow();
}

GrowableArray<Klass*>* ObjArrayKlass::compute_secondary_supers(int num_extra_slots) {
  // interfaces = { cloneable_klass, serializable_klass, elemSuper[], ... };
  Array<Klass*>* elem_supers = element_klass()->secondary_supers();
  int num_elem_supers = elem_supers == NULL ? 0 : elem_supers->length();
  int num_secondaries = num_extra_slots + 2 + num_elem_supers;
  if (num_secondaries == 2) {
    // Must share this for correct bootstrapping!
    set_secondary_supers(Universe::the_array_interfaces_array());
    return NULL;
  } else {
    GrowableArray<Klass*>* secondaries = new GrowableArray<Klass*>(num_elem_supers+2);
    secondaries->push(SystemDictionary::Cloneable_klass());
    secondaries->push(SystemDictionary::Serializable_klass());
    for (int i = 0; i < num_elem_supers; i++) {
      Klass* elem_super = (Klass*) elem_supers->at(i);
      Klass* array_super = elem_super->array_klass_or_null();
      assert(array_super != NULL, "must already have been created");
      secondaries->push(array_super);
    }
    return secondaries;
  }
}

bool ObjArrayKlass::compute_is_subtype_of(Klass* k) {
  if (!k->oop_is_objArray())
    return ArrayKlass::compute_is_subtype_of(k);

  ObjArrayKlass* oak = ObjArrayKlass::cast(k);
  return element_klass()->is_subtype_of(oak->element_klass());
}

void ObjArrayKlass::initialize(TRAPS) {
  bottom_klass()->initialize(THREAD);  // dispatches to either InstanceKlass or TypeArrayKlass
}

// JVM support

jint ObjArrayKlass::compute_modifier_flags(TRAPS) const {
  // The modifier for an objectArray is the same as its element
  if (element_klass() == NULL) {
    assert(Universe::is_bootstrapping(), "partial objArray only at startup");
    return JVM_ACC_ABSTRACT | JVM_ACC_FINAL | JVM_ACC_PUBLIC;
  }
  // Return the flags of the bottom element type.
  jint element_flags = bottom_klass()->compute_modifier_flags(CHECK_0);

  return (element_flags & (JVM_ACC_PUBLIC | JVM_ACC_PRIVATE | JVM_ACC_PROTECTED))
                        | (JVM_ACC_ABSTRACT | JVM_ACC_FINAL);
}


// Printing

void ObjArrayKlass::print_on(outputStream* st) const {
#ifndef PRODUCT
  Klass::print_on(st);
  st->print(" - instance klass: ");
  element_klass()->print_value_on(st);
  st->cr();
#endif //PRODUCT
}

void ObjArrayKlass::print_value_on(outputStream* st) const {
  assert(is_klass(), "must be klass");

  element_klass()->print_value_on(st);
  st->print("[]");
}

#ifndef PRODUCT

void ObjArrayKlass::oop_print_on(oop obj, outputStream* st) {
  ArrayKlass::oop_print_on(obj, st);
  assert(obj->is_objArray(), "must be objArray");
  objArrayOop oa = objArrayOop(obj);
  int print_len = MIN2((intx) oa->length(), MaxElementPrintSize);
  for(int index = 0; index < print_len; index++) {
    st->print(" - %3d : ", index);
    oa->obj_at(index)->print_value_on(st);
    st->cr();
  }
  int remaining = oa->length() - print_len;
  if (remaining > 0) {
    st->print_cr(" - <%d more elements, increase MaxElementPrintSize to print>", remaining);
  }
}

#endif //PRODUCT

static int max_objArray_print_length = 4;

void ObjArrayKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_objArray(), "must be objArray");
  st->print("a ");
  element_klass()->print_value_on(st);
  int len = objArrayOop(obj)->length();
  st->print("[%d] ", len);
  obj->print_address_on(st);
  if (NOT_PRODUCT(PrintOopAddress ||) PrintMiscellaneous && (WizardMode || Verbose)) {
    st->print("{");
    for (int i = 0; i < len; i++) {
      if (i > max_objArray_print_length) {
        st->print("..."); break;
      }
      st->print(" "INTPTR_FORMAT, (intptr_t)(void*)objArrayOop(obj)->obj_at(i));
    }
    st->print(" }");
  }
}

const char* ObjArrayKlass::internal_name() const {
  return external_name();
}


// Verification

void ObjArrayKlass::verify_on(outputStream* st) {
  ArrayKlass::verify_on(st);
  guarantee(element_klass()->is_klass(), "should be klass");
  guarantee(bottom_klass()->is_klass(), "should be klass");
  Klass* bk = bottom_klass();
  guarantee(bk->oop_is_instance() || bk->oop_is_typeArray(),  "invalid bottom klass");
}

void ObjArrayKlass::oop_verify_on(oop obj, outputStream* st) {
  ArrayKlass::oop_verify_on(obj, st);
  guarantee(obj->is_objArray(), "must be objArray");
  objArrayOop oa = objArrayOop(obj);
  for(int index = 0; index < oa->length(); index++) {
    guarantee(oa->obj_at(index)->is_oop_or_null(), "should be oop");
  }
}
