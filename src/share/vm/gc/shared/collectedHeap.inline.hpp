/*
 * Copyright (c) 2001, 2015, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_SHARED_COLLECTEDHEAP_INLINE_HPP
#define SHARE_VM_GC_SHARED_COLLECTEDHEAP_INLINE_HPP

#include "gc/shared/allocTracer.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/threadLocalAllocBuffer.inline.hpp"
#include "memory/universe.hpp"
#include "oops/arrayOop.hpp"
#include "prims/jvmtiExport.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/thread.inline.hpp"
#include "services/lowMemoryDetector.hpp"
#include "utilities/copy.hpp"

// Inline allocation implementations.

void CollectedHeap::post_allocation_setup_common(KlassHandle klass,
                                                 HeapWord* obj) {
  post_allocation_setup_no_klass_install(klass, obj);
  post_allocation_install_obj_klass(klass, oop(obj));
}

void CollectedHeap::post_allocation_setup_no_klass_install(KlassHandle klass,
                                                           HeapWord* objPtr) {
  oop obj = (oop)objPtr;

  assert(obj != NULL, "NULL object pointer");
  if (UseBiasedLocking && (klass() != NULL)) {
    obj->set_mark(klass->prototype_header());
  } else {
    // May be bootstrapping
    obj->set_mark(markOopDesc::prototype());
  }
}

void CollectedHeap::post_allocation_install_obj_klass(KlassHandle klass,
                                                   oop obj) {
  // These asserts are kind of complicated because of klassKlass
  // and the beginning of the world.
  assert(klass() != NULL || !Universe::is_fully_initialized(), "NULL klass");
  assert(klass() == NULL || klass()->is_klass(), "not a klass");
  assert(obj != NULL, "NULL object pointer");
  obj->set_klass(klass());
  assert(!Universe::is_fully_initialized() || obj->klass() != NULL,
         "missing klass");
}

// Support for jvmti and dtrace
inline void post_allocation_notify(KlassHandle klass, oop obj, int size) {
  // support low memory notifications (no-op if not enabled)
  LowMemoryDetector::detect_low_memory_for_collected_pools();

  // support for JVMTI VMObjectAlloc event (no-op if not enabled)
  JvmtiExport::vm_object_alloc_event_collector(obj);

  if (DTraceAllocProbes) {
    // support for Dtrace object alloc event (no-op most of the time)
    if (klass() != NULL && klass()->name() != NULL) {
      SharedRuntime::dtrace_object_alloc(obj, size);
    }
  }
}

inline void post_allocation_notify(KlassHandle klass, oop obj, int size, HeapColor color) {
  // support low memory notifications (no-op if not enabled)
  LowMemoryDetector::detect_low_memory_for_collected_pools();

  // support for JVMTI VMObjectAlloc event (no-op if not enabled)
  JvmtiExport::vm_object_alloc_event_collector(obj);

  if (DTraceAllocProbes) {
    // support for Dtrace object alloc event (no-op most of the time)
    if (klass() != NULL && klass()->name() != NULL) {
      SharedRuntime::dtrace_object_alloc(obj, size);
    }
  }
}

void CollectedHeap::post_allocation_setup_obj(KlassHandle klass,
                                              HeapWord* obj,
                                              int size) {
  post_allocation_setup_common(klass, obj);
  assert(Universe::is_bootstrapping() ||
         !((oop)obj)->is_array(), "must not be an array");
  // notify jvmti and dtrace
  post_allocation_notify(klass, (oop)obj, size);
}

void CollectedHeap::post_allocation_setup_obj(KlassHandle klass,
                                              HeapWord* obj,
                                              size_t size,
                                              HeapColor color) {
  post_allocation_setup_common(klass, obj);
  assert(Universe::is_bootstrapping() ||
         !((oop)obj)->blueprint()->oop_is_array(), "must not be an array");

  // notify jvmti and dtrace
  post_allocation_notify(klass, (oop)obj, color);
}

void CollectedHeap::post_allocation_setup_array(KlassHandle klass,
                                                HeapWord* obj,
                                                int length) {
  // Set array length before setting the _klass field
  // in post_allocation_setup_common() because the klass field
  // indicates that the object is parsable by concurrent GC.
  assert(length >= 0, "length should be non-negative");
  ((arrayOop)obj)->set_length(length);
  post_allocation_setup_common(klass, obj);
  oop new_obj = (oop)obj;
  assert(new_obj->is_array(), "must be an array");
  // notify jvmti and dtrace (must be after length is set for dtrace)
  post_allocation_notify(klass, new_obj, new_obj->size());
}

void CollectedHeap::post_allocation_setup_array(KlassHandle klass,
                                                HeapWord* obj,
                                                size_t size,
                                                int length,
                                                HeapColor color) {
  // Set array length before setting the _klass field
  // in post_allocation_setup_common() because the klass field
  // indicates that the object is parsable by concurrent GC.
  assert(length >= 0, "length should be non-negative");
  ((arrayOop)obj)->set_length(length);
  post_allocation_setup_common(klass, obj);
  assert(((oop)obj)->blueprint()->oop_is_array(), "must be an array");
  // notify jvmti and dtrace (must be after length is set for dtrace)
  post_allocation_notify(klass, (oop)obj, color);
}

HeapWord* CollectedHeap::common_mem_allocate_noinit(KlassHandle klass, size_t size, TRAPS) {

  // Clear unhandled oops for memory allocation.  Memory allocation might
  // not take out a lock if from tlab, so clear here.
  CHECK_UNHANDLED_OOPS_ONLY(THREAD->clear_unhandled_oops();)

  if (HAS_PENDING_EXCEPTION) {
    NOT_PRODUCT(guarantee(false, "Should not allocate with exception pending"));
    return NULL;  // caller does a CHECK_0 too
  }

  HeapWord* result = NULL;
  if (UseTLAB) {
#ifdef COLORED_TLABS
    if (UseColoredSpaces) {
      result = CollectedHeap::allocate_from_tlab(THREAD, size, UnknownObjectHeapColor);
    } else {
      result = CollectedHeap::allocate_from_tlab(THREAD, size);
    }
#else
    result = allocate_from_tlab(klass, THREAD, size);
#endif
    if (result != NULL) {
      assert(!HAS_PENDING_EXCEPTION,
             "Unexpected exception, will result in uninitialized storage");
      return result;
    }
  }
  bool gc_overhead_limit_was_exceeded = false;
  result = Universe::heap()->mem_allocate(size,
                                          &gc_overhead_limit_was_exceeded);
  if (result != NULL) {
    NOT_PRODUCT(Universe::heap()->
      check_for_non_bad_heap_word_value(result, size));
    assert(!HAS_PENDING_EXCEPTION,
           "Unexpected exception, will result in uninitialized storage");
    THREAD->incr_allocated_bytes(size * HeapWordSize);

    AllocTracer::send_allocation_outside_tlab_event(klass, size * HeapWordSize);

    return result;
  }


  if (!gc_overhead_limit_was_exceeded) {
    // -XX:+HeapDumpOnOutOfMemoryError and -XX:OnOutOfMemoryError support
    report_java_out_of_memory("Java heap space");

    if (JvmtiExport::should_post_resource_exhausted()) {
      JvmtiExport::post_resource_exhausted(
        JVMTI_RESOURCE_EXHAUSTED_OOM_ERROR | JVMTI_RESOURCE_EXHAUSTED_JAVA_HEAP,
        "Java heap space");
    }

    THROW_OOP_0(Universe::out_of_memory_error_java_heap());
  } else {
    // -XX:+HeapDumpOnOutOfMemoryError and -XX:OnOutOfMemoryError support
    report_java_out_of_memory("GC overhead limit exceeded");

    if (JvmtiExport::should_post_resource_exhausted()) {
      JvmtiExport::post_resource_exhausted(
        JVMTI_RESOURCE_EXHAUSTED_OOM_ERROR | JVMTI_RESOURCE_EXHAUSTED_JAVA_HEAP,
        "GC overhead limit exceeded");
    }

    THROW_OOP_0(Universe::out_of_memory_error_gc_overhead_limit());
  }
}

HeapWord* CollectedHeap::common_mem_allocate_noinit(size_t size, bool is_noref,
  HeapColor color, TRAPS) {
  assert(UseColoredSpaces, "colored allocation without colored spaces");
  //assert(!UseTLAB, "cannot allocate from TLAB with colored spaces");

  // Clear unhandled oops for memory allocation.  Memory allocation might
  // not take out a lock if from tlab, so clear here.
  CHECK_UNHANDLED_OOPS_ONLY(THREAD->clear_unhandled_oops();)

  if (HAS_PENDING_EXCEPTION) {
    NOT_PRODUCT(guarantee(false, "Should not allocate with exception pending"));
    return NULL;  // caller does a CHECK_0 too
  }

  // We may want to update this, is_noref objects might not be allocated in TLABs.
  HeapWord* result = NULL;
#ifdef COLORED_TLABS
  if (UseTLAB) {
    result = CollectedHeap::allocate_from_tlab(THREAD, size, color);
    if (result != NULL) {
      assert(!HAS_PENDING_EXCEPTION,
             "Unexpected exception, will result in uninitialized storage");
      return result;
    }
  }
#endif
  bool gc_overhead_limit_was_exceeded = false;
  result = Universe::heap()->mem_allocate(size,
                                          &gc_overhead_limit_was_exceeded,
                                          color);
  if (result != NULL) {
    NOT_PRODUCT(Universe::heap()->
      check_for_non_bad_heap_word_value(result, size));
    assert(!HAS_PENDING_EXCEPTION,
           "Unexpected exception, will result in uninitialized storage");
    THREAD->incr_allocated_bytes(size * HeapWordSize);
    return result;
  }


  if (!gc_overhead_limit_was_exceeded) {
    // -XX:+HeapDumpOnOutOfMemoryError and -XX:OnOutOfMemoryError support
    report_java_out_of_memory("Java heap space");

    if (JvmtiExport::should_post_resource_exhausted()) {
      JvmtiExport::post_resource_exhausted(
        JVMTI_RESOURCE_EXHAUSTED_OOM_ERROR | JVMTI_RESOURCE_EXHAUSTED_JAVA_HEAP,
        "Java heap space");
    }

    THROW_OOP_0(Universe::out_of_memory_error_java_heap());
  } else {
    // -XX:+HeapDumpOnOutOfMemoryError and -XX:OnOutOfMemoryError support
    report_java_out_of_memory("GC overhead limit exceeded");

    if (JvmtiExport::should_post_resource_exhausted()) {
      JvmtiExport::post_resource_exhausted(
        JVMTI_RESOURCE_EXHAUSTED_OOM_ERROR | JVMTI_RESOURCE_EXHAUSTED_JAVA_HEAP,
        "GC overhead limit exceeded");
    }

    THROW_OOP_0(Universe::out_of_memory_error_gc_overhead_limit());
  }
}

HeapWord* CollectedHeap::common_mem_allocate_init(KlassHandle klass, size_t size, TRAPS) {
  HeapWord* obj = common_mem_allocate_noinit(klass, size, CHECK_NULL);
  init_obj(obj, size);
  return obj;
}

HeapWord* CollectedHeap::common_mem_allocate_init(size_t size, bool is_noref,
                                                  HeapColor color, TRAPS) {
  HeapWord* obj = common_mem_allocate_noinit(size, is_noref, color, CHECK_NULL);
  init_obj(obj, size);
  return obj;
}

#ifdef COLORED_TLABS
HeapWord* CollectedHeap::allocate_from_tlab(Thread* thread, size_t size,
  HeapColor color) {
  assert(UseTLAB, "should use UseTLAB");

  if (color==HC_NOT_COLORED) {
    color = UnknownObjectHeapColor;
  }

#if 0
  color == HC_RED ? thread->add_cnt(HC_RED, size) : thread->add_cnt(HC_BLUE, size);
  thread->add_mark(1);
  if (thread->reached_mark()) {
    tty->print_cr("_colored_alloc: tid=%d, red=%d, blue=%d", thread->osthread()->thread_id(),
                  (thread->cnt(HC_RED)/(1024*1024)),
                  (thread->cnt(HC_BLUE)/(1024*1024)));
  }
#endif

  HeapWord* obj = thread->tlab(color).allocate(size);
  if (obj != NULL) {
    return obj;
  }
  // Otherwise...
  return allocate_from_tlab_slow(thread, size, color);
}
#endif
HeapWord* CollectedHeap::allocate_from_tlab(KlassHandle klass, Thread* thread, size_t size) {
  assert(UseTLAB, "should use UseTLAB");

  HeapWord* obj = thread->tlab().allocate(size);
  if (obj != NULL) {
    return obj;
  }
  // Otherwise...
  return allocate_from_tlab_slow(klass, thread, size);
}

void CollectedHeap::init_obj(HeapWord* obj, size_t size) {
  assert(obj != NULL, "cannot initialize NULL object");
  const size_t hs = oopDesc::header_size();
  assert(size >= hs, "unexpected object size");
  ((oop)obj)->set_klass_gap(0);
  Copy::fill_to_aligned_words(obj + hs, size - hs);
}

oop CollectedHeap::obj_allocate(KlassHandle klass, int size, TRAPS) {
  debug_only(check_for_valid_allocation_state());
  assert(!Universe::heap()->is_gc_active(), "Allocation during gc not allowed");
  assert(size >= 0, "int won't convert to size_t");
  HeapWord* obj = common_mem_allocate_init(klass, size, CHECK_NULL);
#ifdef PROFILE_OBJECT_ADDRESS_INFO
  if (ProfileObjectAddressInfo) {
    ObjectAddressInfoTable *oait = Universe::object_address_info_table();
    oait->insert((oop)obj, size, klass(), VM_OBJECT);
  }
#endif
  post_allocation_setup_obj(klass, obj, size);
  //tty->print_cr("obj_alloc");
  NOT_PRODUCT(Universe::heap()->check_for_bad_heap_word_value(obj, size));
  return (oop)obj;
}

oop CollectedHeap::obj_allocate(KlassHandle klass, int size, HeapColor color, TRAPS) {
  debug_only(check_for_valid_allocation_state());
  assert(UseColoredSpaces, "colored allocation without colored spaces");
  assert(!Universe::heap()->is_gc_active(), "Allocation during gc not allowed");
  assert(size >= 0, "int won't convert to size_t");
  HeapWord* obj = common_mem_allocate_init(size, false, color, CHECK_NULL);
#ifdef PROFILE_OBJECT_ADDRESS_INFO
  if (ProfileObjectAddressInfo) {
    ObjectAddressInfoTable *oait = Universe::object_address_info_table();
    oait->insert((oop)obj, size, klass(), VM_OBJECT);
  }
#endif
  post_allocation_setup_obj(klass, obj, size, color);
  //tty->print_cr("colored obj_alloc");
  NOT_PRODUCT(Universe::heap()->check_for_bad_heap_word_value(obj, size));
  return (oop)obj;
}

oop CollectedHeap::array_allocate(KlassHandle klass,
                                  int size,
                                  int length,
                                  TRAPS) {
  debug_only(check_for_valid_allocation_state());
  assert(!Universe::heap()->is_gc_active(), "Allocation during gc not allowed");
  assert(size >= 0, "int won't convert to size_t");
  HeapWord* obj = common_mem_allocate_init(klass, size, CHECK_NULL);
  post_allocation_setup_array(klass, obj, length);
#ifdef PROFILE_OBJECT_ADDRESS_INFO
  if (ProfileObjectAddressInfo) {
    ObjectAddressInfoTable *oait = Universe::object_address_info_table();
    oait->insert((oop)obj, size, klass(), VM_OBJECT);
  }
#endif
  //tty->print_cr("array_alloc");
  NOT_PRODUCT(Universe::heap()->check_for_bad_heap_word_value(obj, size));
  return (oop)obj;
}

oop CollectedHeap::array_allocate(KlassHandle klass,
                                  int size,
                                  int length,
                                  HeapColor color,
                                  TRAPS) {
  debug_only(check_for_valid_allocation_state());
  assert(!Universe::heap()->is_gc_active(), "Allocation during gc not allowed");
  assert(size >= 0, "int won't convert to size_t");
  HeapWord* obj = common_mem_allocate_init(size, false, color, CHECK_NULL);
#ifdef PROFILE_OBJECT_ADDRESS_INFO
  if (ProfileObjectAddressInfo) {
    ObjectAddressInfoTable *oait = Universe::object_address_info_table();
    oait->insert((oop)obj, size, klass(), VM_OBJECT);
  }
#endif
  post_allocation_setup_array(klass, obj, size, length, color);
  //tty->print_cr("colored array_alloc");
  NOT_PRODUCT(Universe::heap()->check_for_bad_heap_word_value(obj, size));
  return (oop)obj;
}

oop CollectedHeap::large_typearray_allocate(KlassHandle klass,
                                            int size,
                                            int length,
                                            HeapColor color,
                                            TRAPS) {
  debug_only(check_for_valid_allocation_state());
  assert(!Universe::heap()->is_gc_active(), "Allocation during gc not allowed");
  assert(size >= 0, "int won't convert to size_t");
  HeapWord* obj = common_mem_allocate_init(size, true, color, CHECK_NULL);
#ifdef PROFILE_OBJECT_ADDRESS_INFO
  if (ProfileObjectAddressInfo) {
    ObjectAddressInfoTable *oait = Universe::object_address_info_table();
    oait->insert((oop)obj, size, klass(), VM_OBJECT);
  }
#endif
  post_allocation_setup_array(klass, obj, size, length, color);
  NOT_PRODUCT(Universe::heap()->check_for_bad_heap_word_value(obj, size));
  return (oop)obj;
}

oop CollectedHeap::array_allocate_nozero(KlassHandle klass,
                                         int size,
                                         int length,
                                         TRAPS) {
  debug_only(check_for_valid_allocation_state());
  assert(!Universe::heap()->is_gc_active(), "Allocation during gc not allowed");
  assert(size >= 0, "int won't convert to size_t");
  HeapWord* obj = common_mem_allocate_noinit(klass, size, CHECK_NULL);
  ((oop)obj)->set_klass_gap(0);
  post_allocation_setup_array(klass, obj, length);
#ifndef PRODUCT
  const size_t hs = oopDesc::header_size()+1;
  Universe::heap()->check_for_non_bad_heap_word_value(obj+hs, size-hs);
#endif
  return (oop)obj;
}

inline HeapWord* CollectedHeap::align_allocation_or_fail(HeapWord* addr,
                                                         HeapWord* end,
                                                         unsigned short alignment_in_bytes) {
  if (alignment_in_bytes <= ObjectAlignmentInBytes) {
    return addr;
  }

  assert(is_ptr_aligned(addr, HeapWordSize),
    err_msg("Address " PTR_FORMAT " is not properly aligned.", p2i(addr)));
  assert(is_size_aligned(alignment_in_bytes, HeapWordSize),
    err_msg("Alignment size %u is incorrect.", alignment_in_bytes));

  HeapWord* new_addr = (HeapWord*) align_pointer_up(addr, alignment_in_bytes);
  size_t padding = pointer_delta(new_addr, addr);

  if (padding == 0) {
    return addr;
  }

  if (padding < CollectedHeap::min_fill_size()) {
    padding += alignment_in_bytes / HeapWordSize;
    assert(padding >= CollectedHeap::min_fill_size(),
      err_msg("alignment_in_bytes %u is expect to be larger "
      "than the minimum object size", alignment_in_bytes));
    new_addr = addr + padding;
  }

  assert(new_addr > addr, err_msg("Unexpected arithmetic overflow "
    PTR_FORMAT " not greater than " PTR_FORMAT, p2i(new_addr), p2i(addr)));
  if(new_addr < end) {
    CollectedHeap::fill_with_object(addr, padding);
    return new_addr;
  } else {
    return NULL;
  }
}

#ifndef PRODUCT

inline bool
CollectedHeap::promotion_should_fail(volatile size_t* count) {
  // Access to count is not atomic; the value does not have to be exact.
  if (PromotionFailureALot) {
    const size_t gc_num = total_collections();
    const size_t elapsed_gcs = gc_num - _promotion_failure_alot_gc_number;
    if (elapsed_gcs >= PromotionFailureALotInterval) {
      // Test for unsigned arithmetic wrap-around.
      if (++*count >= PromotionFailureALotCount) {
        *count = 0;
        return true;
      }
    }
  }
  return false;
}

inline bool CollectedHeap::promotion_should_fail() {
  return promotion_should_fail(&_promotion_failure_alot_count);
}

inline void CollectedHeap::reset_promotion_should_fail(volatile size_t* count) {
  if (PromotionFailureALot) {
    _promotion_failure_alot_gc_number = total_collections();
    *count = 0;
  }
}

inline void CollectedHeap::reset_promotion_should_fail() {
  reset_promotion_should_fail(&_promotion_failure_alot_count);
}
#endif  // #ifndef PRODUCT

#endif // SHARE_VM_GC_SHARED_COLLECTEDHEAP_INLINE_HPP
