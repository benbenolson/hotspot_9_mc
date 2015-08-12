/*
 * Copyright (c) 2002, 2015, Oracle and/or its affiliates. All rights reserved.
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
#include "classfile/classLoaderData.hpp"
#include "classfile/systemDictionary.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/genCollectedHeap.hpp"
#include "memory/heapInspection.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/os.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"
#include "utilities/stack.inline.hpp"
#include "runtime/mutexLocker.hpp"
#if INCLUDE_ALL_GCS
#include "gc/parallel/parallelScavengeHeap.hpp"
#endif // INCLUDE_ALL_GCS

static const int INITIAL_ENTRY_ARRAY_SIZE = 10;
static const int INITIAL_KNOWN_FREE_SIZE  = 200;

#ifdef PROFILE_OBJECT_INFO
bool PersistentObjectInfoTable::_printing = false;
#endif
#ifdef PROFILE_OBJECT_ADDRESS_INFO
int ObjectAddressInfoTable::_klass_print_order = 0;
#endif

// ObjectLayout needs to know about the colored spaces
#include "gc/shared/mutableColoredSpace.hpp"
#include "gc/parallel/psScavenge.hpp"

#ifndef SIGBREAK
#define SIGBREAK SIGQUIT
#endif

// Implementation of RegularScavengeTask
RegularScavengeTask* RegularScavenge::_task = NULL;
bool                 RegularScavenge::_collecting = false;

/*
 * The engage() method is called at initialization time via
 * Thread::create_vm() to initialize the RegularScavenge and
 * register it with the WatcherThread as a periodic task.
 */
void RegularScavenge::engage() {
  if (!is_active()) {
    // start up the periodic task
    _task = new RegularScavengeTask((int)ScavengeInterval);
    _task->enroll();
  }
}

/*
 * the disengage() method is responsible for deactivating the periodic
 * task. This  method is called from before_exit() in java.cpp and is only called
 * after the WatcherThread has been stopped.
 */
void RegularScavenge::disengage() {
  if (is_active()) {
    //RegularScavenge::print_persistent_object_info(objalloc_log);
    // remove RegularScavenge
    _task->disenroll();
    delete _task;
    _task = NULL;
  }
}

void RegularScavengeTask::task() {
  if (!RegularScavenge::_collecting) {
      os::signal_notify(SIGBREAK);
  }
}

// HeapInspection

inline KlassInfoEntry::~KlassInfoEntry() {
  if (_subclasses != NULL) {
    delete _subclasses;
  }
}

inline void KlassInfoEntry::add_subclass(KlassInfoEntry* cie) {
  if (_subclasses == NULL) {
    _subclasses = new  (ResourceObj::C_HEAP, mtInternal) GrowableArray<KlassInfoEntry*>(4, true);
  }
  _subclasses->append(cie);
}

int KlassInfoEntry::compare(KlassInfoEntry* e1, KlassInfoEntry* e2) {
  if(e1->_instance_words > e2->_instance_words) {
    return -1;
  } else if(e1->_instance_words < e2->_instance_words) {
    return 1;
  }
  // Sort alphabetically, note 'Z' < '[' < 'a', but it's better to group
  // the array classes before all the instance classes.
  ResourceMark rm;
  const char* name1 = e1->klass()->external_name();
  const char* name2 = e2->klass()->external_name();
  bool d1 = (name1[0] == '[');
  bool d2 = (name2[0] == '[');
  if (d1 && !d2) {
    return -1;
  } else if (d2 && !d1) {
    return 1;
  } else {
    return strcmp(name1, name2);
  }
}

const char* KlassInfoEntry::name() const {
  const char* name;
  if (_klass->name() != NULL) {
    name = _klass->external_name();
  } else {
    if (_klass == Universe::boolArrayKlassObj())         name = "<boolArrayKlass>";         else
    if (_klass == Universe::charArrayKlassObj())         name = "<charArrayKlass>";         else
    if (_klass == Universe::singleArrayKlassObj())       name = "<singleArrayKlass>";       else
    if (_klass == Universe::doubleArrayKlassObj())       name = "<doubleArrayKlass>";       else
    if (_klass == Universe::byteArrayKlassObj())         name = "<byteArrayKlass>";         else
    if (_klass == Universe::shortArrayKlassObj())        name = "<shortArrayKlass>";        else
    if (_klass == Universe::intArrayKlassObj())          name = "<intArrayKlass>";          else
    if (_klass == Universe::longArrayKlassObj())         name = "<longArrayKlass>";         else
      name = "<no name>";
  }
  return name;
}

void KlassInfoEntry::print_on(outputStream* st) const {
  ResourceMark rm;

  // simplify the formatting (ILP32 vs LP64) - always cast the numbers to 64-bit
  st->print_cr(INT64_FORMAT_W(13) "  " UINT64_FORMAT_W(13) "  %s",
               (int64_t)_instance_count,
               (uint64_t)_instance_words * HeapWordSize,
               name());
}

KlassInfoEntry* KlassInfoBucket::lookup(Klass* const k) {
  KlassInfoEntry* elt = _list;
  while (elt != NULL) {
    if (elt->is_equal(k)) {
      return elt;
    }
    elt = elt->next();
  }
  elt = new (std::nothrow) KlassInfoEntry(k, list());
  // We may be out of space to allocate the new entry.
  if (elt != NULL) {
    set_list(elt);
  }
  return elt;
}

void KlassInfoBucket::iterate(KlassInfoClosure* cic) {
  KlassInfoEntry* elt = _list;
  while (elt != NULL) {
    cic->do_cinfo(elt);
    elt = elt->next();
  }
}

void KlassInfoBucket::empty() {
  KlassInfoEntry* elt = _list;
  _list = NULL;
  while (elt != NULL) {
    KlassInfoEntry* next = elt->next();
    delete elt;
    elt = next;
  }
}

void KlassInfoBucket::print_on(outputStream *out) const {
  KlassInfoEntry* elt = _list;
  while (elt != NULL) {
    elt->print_on(out);
    elt = elt->next();
  }
}

void KlassInfoTable::AllClassesFinder::do_klass(Klass* k) {
  // This has the SIDE EFFECT of creating a KlassInfoEntry
  // for <k>, if one doesn't exist yet.
  _table->lookup(k);
}

KlassInfoTable::KlassInfoTable(bool add_all_classes) {
  _size_of_instances_in_words = 0;
  _size = 0;
  _ref = (HeapWord*) Universe::boolArrayKlassObj();
  _buckets =
    (KlassInfoBucket*)  AllocateHeap(sizeof(KlassInfoBucket) * _num_buckets,
       mtInternal, CURRENT_PC, AllocFailStrategy::RETURN_NULL);
  if (_buckets != NULL) {
    _size = _num_buckets;
    for (int index = 0; index < _size; index++) {
      _buckets[index].initialize();
    }
    if (add_all_classes) {
      AllClassesFinder finder(this);
      ClassLoaderDataGraph::classes_do(&finder);
    }
  }
}

KlassInfoTable::~KlassInfoTable() {
  if (_buckets != NULL) {
    for (int index = 0; index < _size; index++) {
      _buckets[index].empty();
    }
    FREE_C_HEAP_ARRAY(KlassInfoBucket, _buckets);
    _size = 0;
  }
}

uint KlassInfoTable::hash(const Klass* p) {
  return (uint)(((uintptr_t)p - (uintptr_t)_ref) >> 2);
}

KlassInfoEntry* KlassInfoTable::lookup(Klass* k) {
  uint         idx = hash(k) % _size;
  assert(_buckets != NULL, "Allocation failure should have been caught");
  KlassInfoEntry*  e   = _buckets[idx].lookup(k);
  // Lookup may fail if this is a new klass for which we
  // could not allocate space for an new entry.
  assert(e == NULL || k == e->klass(), "must be equal");
  return e;
}

// Return false if the entry could not be recorded on account
// of running out of space required to create a new entry.
bool KlassInfoTable::record_instance(const oop obj) {
  Klass*        k = obj->klass();
  KlassInfoEntry* elt = lookup(k);
  // elt may be NULL if it's a new klass for which we
  // could not allocate space for a new entry in the hashtable.
  if (elt != NULL) {
    elt->set_count(elt->count() + 1);
    elt->set_words(elt->words() + obj->size());
    _size_of_instances_in_words += obj->size();
    return true;
  } else {
    return false;
  }
}

void KlassInfoTable::iterate(KlassInfoClosure* cic) {
  assert(_size == 0 || _buckets != NULL, "Allocation failure should have been caught");
  for (int index = 0; index < _size; index++) {
    _buckets[index].iterate(cic);
  }
}

void KlassInfoTable::print_on(outputStream *out) const {
  assert(_size == 0 || _buckets != NULL, "Allocation failure should have been caught");
  for (int index = 0; index < _size; index++) {
    _buckets[index].print_on(out);
  }
}

size_t KlassInfoTable::size_of_instances_in_words() const {
  return _size_of_instances_in_words;
}

int KlassInfoHisto::sort_helper(KlassInfoEntry** e1, KlassInfoEntry** e2) {
  return (*e1)->compare(*e1,*e2);
}

KlassInfoHisto::KlassInfoHisto(KlassInfoTable* cit, const char* title) :
  _cit(cit),
  _title(title) {
  _elements = new (ResourceObj::C_HEAP, mtInternal) GrowableArray<KlassInfoEntry*>(_histo_initial_size, true);
}

KlassInfoHisto::~KlassInfoHisto() {
  delete _elements;
}

void KlassInfoHisto::add(KlassInfoEntry* cie) {
  elements()->append(cie);
}

void KlassInfoHisto::sort() {
  elements()->sort(KlassInfoHisto::sort_helper);
}

void KlassInfoHisto::print_elements(outputStream* st) const {
  // simplify the formatting (ILP32 vs LP64) - store the sum in 64-bit
  int64_t total = 0;
  uint64_t totalw = 0;
  for(int i=0; i < elements()->length(); i++) {
    st->print("%4d: ", i+1);
    elements()->at(i)->print_on(st);
    total += elements()->at(i)->count();
    totalw += elements()->at(i)->words();
  }
  st->print_cr("Total " INT64_FORMAT_W(13) "  " UINT64_FORMAT_W(13),
               total, totalw * HeapWordSize);
}

#define MAKE_COL_NAME(field, name, help)     #name,
#define MAKE_COL_HELP(field, name, help)     help,

static const char *name_table[] = {
  HEAP_INSPECTION_COLUMNS_DO(MAKE_COL_NAME)
};

static const char *help_table[] = {
  HEAP_INSPECTION_COLUMNS_DO(MAKE_COL_HELP)
};

bool KlassInfoHisto::is_selected(const char *col_name) {
  if (_selected_columns == NULL) {
    return true;
  }
  if (strcmp(_selected_columns, col_name) == 0) {
    return true;
  }

  const char *start = strstr(_selected_columns, col_name);
  if (start == NULL) {
    return false;
  }

  // The following must be true, because _selected_columns != col_name
  if (start > _selected_columns && start[-1] != ',') {
    return false;
  }
  char x = start[strlen(col_name)];
  if (x != ',' && x != '\0') {
    return false;
  }

  return true;
}

PRAGMA_FORMAT_NONLITERAL_IGNORED_EXTERNAL
void KlassInfoHisto::print_title(outputStream* st, bool csv_format,
                                 bool selected[], int width_table[],
                                 const char *name_table[]) {
  if (csv_format) {
    st->print("Index,Super");
    for (int c=0; c<KlassSizeStats::_num_columns; c++) {
       if (selected[c]) {st->print(",%s", name_table[c]);}
    }
    st->print(",ClassName");
  } else {
    st->print("Index Super");
    for (int c=0; c<KlassSizeStats::_num_columns; c++) {
PRAGMA_DIAG_PUSH
PRAGMA_FORMAT_NONLITERAL_IGNORED_INTERNAL
      if (selected[c]) {st->print(str_fmt(width_table[c]), name_table[c]);}
PRAGMA_DIAG_POP
    }
    st->print(" ClassName");
  }

  if (is_selected("ClassLoader")) {
    st->print(",ClassLoader");
  }
  st->cr();
}

class HierarchyClosure : public KlassInfoClosure {
private:
  GrowableArray<KlassInfoEntry*> *_elements;
public:
  HierarchyClosure(GrowableArray<KlassInfoEntry*> *_elements) : _elements(_elements) {}

  void do_cinfo(KlassInfoEntry* cie) {
    // ignore array classes
    if (cie->klass()->oop_is_instance()) {
      _elements->append(cie);
    }
  }
};

void KlassHierarchy::print_class_hierarchy(outputStream* st, bool print_interfaces,
                                           bool print_subclasses, char* classname) {
  ResourceMark rm;
  Stack <KlassInfoEntry*, mtClass> class_stack;
  GrowableArray<KlassInfoEntry*> elements;

  // Add all classes to the KlassInfoTable, which allows for quick lookup.
  // A KlassInfoEntry will be created for each class.
  KlassInfoTable cit(true);
  if (cit.allocation_failed()) {
    st->print_cr("ERROR: Ran out of C-heap; hierarchy not generated");
    return;
  }

  // Add all created KlassInfoEntry instances to the elements array for easy
  // iteration, and to allow each KlassInfoEntry instance to have a unique index.
  HierarchyClosure hc(&elements);
  cit.iterate(&hc);

  for(int i = 0; i < elements.length(); i++) {
    KlassInfoEntry* cie = elements.at(i);
    const InstanceKlass* k = (InstanceKlass*)cie->klass();
    Klass* super = ((InstanceKlass*)k)->java_super();

    // Set the index for the class.
    cie->set_index(i + 1);

    // Add the class to the subclass array of its superclass.
    if (super != NULL) {
      KlassInfoEntry* super_cie = cit.lookup(super);
      assert(super_cie != NULL, "could not lookup superclass");
      super_cie->add_subclass(cie);
    }
  }

  // Set the do_print flag for each class that should be printed.
  for(int i = 0; i < elements.length(); i++) {
    KlassInfoEntry* cie = elements.at(i);
    if (classname == NULL) {
      // We are printing all classes.
      cie->set_do_print(true);
    } else {
      // We are only printing the hierarchy of a specific class.
      if (strcmp(classname, cie->klass()->external_name()) == 0) {
        KlassHierarchy::set_do_print_for_class_hierarchy(cie, &cit, print_subclasses);
      }
    }
  }

  // Now we do a depth first traversal of the class hierachry. The class_stack will
  // maintain the list of classes we still need to process. Start things off
  // by priming it with java.lang.Object.
  KlassInfoEntry* jlo_cie = cit.lookup(SystemDictionary::Object_klass());
  assert(jlo_cie != NULL, "could not lookup java.lang.Object");
  class_stack.push(jlo_cie);

  // Repeatedly pop the top item off the stack, print its class info,
  // and push all of its subclasses on to the stack. Do this until there
  // are no classes left on the stack.
  while (!class_stack.is_empty()) {
    KlassInfoEntry* curr_cie = class_stack.pop();
    if (curr_cie->do_print()) {
      print_class(st, curr_cie, print_interfaces);
      if (curr_cie->subclasses() != NULL) {
        // Current class has subclasses, so push all of them onto the stack.
        for (int i = 0; i < curr_cie->subclasses()->length(); i++) {
          KlassInfoEntry* cie = curr_cie->subclasses()->at(i);
          if (cie->do_print()) {
            class_stack.push(cie);
          }
        }
      }
    }
  }

  st->flush();
}

// Sets the do_print flag for every superclass and subclass of the specified class.
void KlassHierarchy::set_do_print_for_class_hierarchy(KlassInfoEntry* cie, KlassInfoTable* cit,
                                                      bool print_subclasses) {
  // Set do_print for all superclasses of this class.
  Klass* super = ((InstanceKlass*)cie->klass())->java_super();
  while (super != NULL) {
    KlassInfoEntry* super_cie = cit->lookup(super);
    super_cie->set_do_print(true);
    super = super->super();
  }

  // Set do_print for this class and all of its subclasses.
  Stack <KlassInfoEntry*, mtClass> class_stack;
  class_stack.push(cie);
  while (!class_stack.is_empty()) {
    KlassInfoEntry* curr_cie = class_stack.pop();
    curr_cie->set_do_print(true);
    if (print_subclasses && curr_cie->subclasses() != NULL) {
      // Current class has subclasses, so push all of them onto the stack.
      for (int i = 0; i < curr_cie->subclasses()->length(); i++) {
        KlassInfoEntry* cie = curr_cie->subclasses()->at(i);
        class_stack.push(cie);
      }
    }
  }
}

static void print_indent(outputStream* st, int indent) {
  while (indent != 0) {
    st->print("|");
    indent--;
    if (indent != 0) {
      st->print("  ");
    }
  }
}

// Print the class name and its unique ClassLoader identifer.
static void print_classname(outputStream* st, Klass* klass) {
  oop loader_oop = klass->class_loader_data()->class_loader();
  st->print("%s/", klass->external_name());
  if (loader_oop == NULL) {
    st->print("null");
  } else {
    st->print(INTPTR_FORMAT, p2i(klass->class_loader_data()));
  }
}

static void print_interface(outputStream* st, Klass* intf_klass, const char* intf_type, int indent) {
  print_indent(st, indent);
  st->print("  implements ");
  print_classname(st, intf_klass);
  st->print(" (%s intf)\n", intf_type);
}

void KlassHierarchy::print_class(outputStream* st, KlassInfoEntry* cie, bool print_interfaces) {
  ResourceMark rm;
  InstanceKlass* klass = (InstanceKlass*)cie->klass();
  int indent = 0;

  // Print indentation with proper indicators of superclass.
  Klass* super = klass->super();
  while (super != NULL) {
    super = super->super();
    indent++;
  }
  print_indent(st, indent);
  if (indent != 0) st->print("--");

  // Print the class name, its unique ClassLoader identifer, and if it is an interface.
  print_classname(st, klass);
  if (klass->is_interface()) {
    st->print(" (intf)");
  }
  st->print("\n");

  // Print any interfaces the class has.
  if (print_interfaces) {
    Array<Klass*>* local_intfs = klass->local_interfaces();
    Array<Klass*>* trans_intfs = klass->transitive_interfaces();
    for (int i = 0; i < local_intfs->length(); i++) {
      print_interface(st, local_intfs->at(i), "declared", indent);
    }
    for (int i = 0; i < trans_intfs->length(); i++) {
      Klass* trans_interface = trans_intfs->at(i);
      // Only print transitive interfaces if they are not also declared.
      if (!local_intfs->contains(trans_interface)) {
        print_interface(st, trans_interface, "inherited", indent);
      }
    }
  }
}

void KlassInfoHisto::print_class_stats(outputStream* st,
                                      bool csv_format, const char *columns) {
  ResourceMark rm;
  KlassSizeStats sz, sz_sum;
  int i;
  julong *col_table = (julong*)(&sz);
  julong *colsum_table = (julong*)(&sz_sum);
  int width_table[KlassSizeStats::_num_columns];
  bool selected[KlassSizeStats::_num_columns];

  _selected_columns = columns;

  memset(&sz_sum, 0, sizeof(sz_sum));
  for (int c=0; c<KlassSizeStats::_num_columns; c++) {
    selected[c] = is_selected(name_table[c]);
  }

  for(i=0; i < elements()->length(); i++) {
    elements()->at(i)->set_index(i+1);
  }

  // First iteration is for accumulating stats totals in colsum_table[].
  // Second iteration is for printing stats for each class.
  for (int pass=1; pass<=2; pass++) {
    if (pass == 2) {
      print_title(st, csv_format, selected, width_table, name_table);
    }
    for(i=0; i < elements()->length(); i++) {
      KlassInfoEntry* e = (KlassInfoEntry*)elements()->at(i);
      const Klass* k = e->klass();

      // Get the stats for this class.
      memset(&sz, 0, sizeof(sz));
      sz._inst_count = e->count();
      sz._inst_bytes = HeapWordSize * e->words();
      k->collect_statistics(&sz);
      sz._total_bytes = sz._ro_bytes + sz._rw_bytes;

      if (pass == 1) {
        // Add the stats for this class to the overall totals.
        for (int c=0; c<KlassSizeStats::_num_columns; c++) {
          colsum_table[c] += col_table[c];
        }
      } else {
        int super_index = -1;
        // Print the stats for this class.
        if (k->oop_is_instance()) {
          Klass* super = ((InstanceKlass*)k)->java_super();
          if (super) {
            KlassInfoEntry* super_e = _cit->lookup(super);
            if (super_e) {
              super_index = super_e->index();
            }
          }
        }

        if (csv_format) {
          st->print("%ld,%d", e->index(), super_index);
          for (int c=0; c<KlassSizeStats::_num_columns; c++) {
            if (selected[c]) {st->print("," JULONG_FORMAT, col_table[c]);}
          }
          st->print(",%s",e->name());
        } else {
          st->print("%5ld %5d", e->index(), super_index);
          for (int c=0; c<KlassSizeStats::_num_columns; c++) {
            if (selected[c]) {print_julong(st, width_table[c], col_table[c]);}
          }
          st->print(" %s", e->name());
        }
        if (is_selected("ClassLoader")) {
          ClassLoaderData* loader_data = k->class_loader_data();
          st->print(",");
          loader_data->print_value_on(st);
        }
        st->cr();
      }
    }

    if (pass == 1) {
      // Calculate the minimum width needed for the column by accounting for the
      // column header width and the width of the largest value in the column.
      for (int c=0; c<KlassSizeStats::_num_columns; c++) {
        width_table[c] = col_width(colsum_table[c], name_table[c]);
      }
    }
  }

  sz_sum._inst_size = 0;

  // Print the column totals.
  if (csv_format) {
    st->print(",");
    for (int c=0; c<KlassSizeStats::_num_columns; c++) {
      if (selected[c]) {st->print("," JULONG_FORMAT, colsum_table[c]);}
    }
  } else {
    st->print("           ");
    for (int c=0; c<KlassSizeStats::_num_columns; c++) {
      if (selected[c]) {print_julong(st, width_table[c], colsum_table[c]);}
    }
    st->print(" Total");
    if (sz_sum._total_bytes > 0) {
      st->cr();
      st->print("           ");
      for (int c=0; c<KlassSizeStats::_num_columns; c++) {
        if (selected[c]) {
          switch (c) {
          case KlassSizeStats::_index_inst_size:
          case KlassSizeStats::_index_inst_count:
          case KlassSizeStats::_index_method_count:
PRAGMA_DIAG_PUSH
PRAGMA_FORMAT_NONLITERAL_IGNORED_INTERNAL
            st->print(str_fmt(width_table[c]), "-");
PRAGMA_DIAG_POP
            break;
          default:
            {
              double perc = (double)(100) * (double)(colsum_table[c]) / (double)sz_sum._total_bytes;
PRAGMA_DIAG_PUSH
PRAGMA_FORMAT_NONLITERAL_IGNORED_INTERNAL
              st->print(perc_fmt(width_table[c]), perc);
PRAGMA_DIAG_POP
            }
          }
        }
      }
    }
  }
  st->cr();

  if (!csv_format) {
    print_title(st, csv_format, selected, width_table, name_table);
  }
}

julong KlassInfoHisto::annotations_bytes(Array<AnnotationArray*>* p) const {
  julong bytes = 0;
  if (p != NULL) {
    for (int i = 0; i < p->length(); i++) {
      bytes += count_bytes_array(p->at(i));
    }
    bytes += count_bytes_array(p);
  }
  return bytes;
}

void KlassInfoHisto::print_histo_on(outputStream* st, bool print_stats,
                                    bool csv_format, const char *columns) {
  if (print_stats) {
    print_class_stats(st, csv_format, columns);
  } else {
    st->print_cr("%s",title());
    print_elements(st);
  }
}

class HistoClosure : public KlassInfoClosure {
 private:
  KlassInfoHisto* _cih;
 public:
  HistoClosure(KlassInfoHisto* cih) : _cih(cih) {}

  void do_cinfo(KlassInfoEntry* cie) {
    _cih->add(cie);
  }
};

class RecordInstanceClosure : public ObjectClosure {
 private:
  KlassInfoTable* _cit;
  size_t _missed_count;
  BoolObjectClosure* _filter;
 public:
  RecordInstanceClosure(KlassInfoTable* cit, BoolObjectClosure* filter) :
    _cit(cit), _missed_count(0), _filter(filter) {}

  void do_object(oop obj) {
    if (should_visit(obj)) {
      if (!_cit->record_instance(obj)) {
        _missed_count++;
      }
    }
  }

  size_t missed_count() { return _missed_count; }

 private:
  bool should_visit(oop obj) {
    return _filter == NULL || _filter->do_object_b(obj);
  }
};

size_t HeapInspection::populate_table(KlassInfoTable* cit, BoolObjectClosure *filter) {
  ResourceMark rm;

  RecordInstanceClosure ric(cit, filter);
  Universe::heap()->object_iterate(&ric);
  return ric.missed_count();
}

void HeapInspection::heap_inspection(outputStream* st) {
  ResourceMark rm;
  bool tenured_only = false;

  if (_print_help) {
    for (int c=0; c<KlassSizeStats::_num_columns; c++) {
      st->print("%s:\n\t", name_table[c]);
      const int max_col = 60;
      int col = 0;
      for (const char *p = help_table[c]; *p; p++,col++) {
        if (col >= max_col && *p == ' ') {
          st->print("\n\t");
          col = 0;
        } else {
          st->print("%c", *p);
        }
      }
      st->print_cr(".\n");
    }
    return;
  }

  KlassInfoTable cit(_print_class_stats);
  if (!cit.allocation_failed()) {
    // populate table with object allocation info
    size_t missed_count = populate_table(&cit);
    if (missed_count != 0) {
      st->print_cr("WARNING: Ran out of C-heap; undercounted " SIZE_FORMAT
                   " total instances in data below",
                   missed_count);
    }

    // Sort and print klass instance info
    const char *title = "\n"
              " num     #instances         #bytes  class name\n"
              "----------------------------------------------";
    KlassInfoHisto histo(&cit, title);
    HistoClosure hc(&histo);

    cit.iterate(&hc);

    histo.sort();
    histo.print_histo_on(st, _print_class_stats, _csv_format, _columns);
  } else {
    st->print_cr("ERROR: Ran out of C-heap; histogram not generated");
  }
  st->flush();
}

class FindInstanceClosure : public ObjectClosure {
 private:
  Klass* _klass;
  GrowableArray<oop>* _result;

 public:
  FindInstanceClosure(Klass* k, GrowableArray<oop>* result) : _klass(k), _result(result) {};

  void do_object(oop obj) {
    if (obj->is_a(_klass)) {
      _result->append(obj);
    }
  }
};

void HeapInspection::find_instances_at_safepoint(Klass* k, GrowableArray<oop>* result) {
  assert(SafepointSynchronize::is_at_safepoint(), "all threads are stopped");
  assert(Heap_lock->is_locked(), "should have the Heap_lock");

  // Ensure that the heap is parsable
  Universe::heap()->ensure_parsability(false);  // no need to retire TALBs

  // Iterate over objects in the heap
  FindInstanceClosure fic(k, result);
  // If this operation encounters a bad object when using CMS,
  // consider using safe_object_iterate() which avoids metadata
  // objects that may contain bad references.
  Universe::heap()->object_iterate(&fic);
}

#ifdef PROFILE_OBJECT_INFO
//----------------------------------------------------------
// Implementation of ObjectInfoCollectionTask
ObjectInfoCollectionTask* ObjectInfoCollection::_task = NULL;
bool                      ObjectInfoCollection::_collecting = false;
jlong                     ObjectInfoCollection::_val_start   = 0;

/*
 * The engage() method is called at initialization time via
 * Thread::create_vm() to initialize the ObjectInfoCollection and
 * register it with the WatcherThread as a periodic task.
 */
void ObjectInfoCollection::engage() {
  if (!is_active()) {
    ObjectInfoCollection::_val_start = os::javaTimeMillis();
    objinfo_log->print_cr("start time = %14ld", ObjectInfoCollection::_val_start);
    // start up the periodic task
    _task = new ObjectInfoCollectionTask((int)ObjectInfoInterval);
    _task->enroll();
  }
}

/*
 * the disengage() method is responsible for deactivating the periodic
 * task. This  method is called from before_exit() in java.cpp and is only called
 * after the WatcherThread has been stopped.
 */
void ObjectInfoCollection::disengage() {
  if (is_active()) {
    //ObjectInfoCollection::print_persistent_object_info(objalloc_log);
    // remove ObjectInfoCollection
    _task->disenroll();
    delete _task;
    _task = NULL;
  }
}

void ObjectInfoCollectionTask::task() {
  if (!ObjectInfoCollection::_collecting) {
      os::signal_notify(SIGBREAK);
  }
}


class ObjectInfoPrintClosure : public ObjectInfoClosure {
 private:
  outputStream *_out;
 public:
  ObjectInfoPrintClosure(outputStream *out) : _out(out) {}

  void do_cinfo(ObjectInfoEntry* cie) {
    cie->print_on(_out);
  }
};

ObjectInfoEntry::ObjectInfoEntry(klassOop k, ObjectInfoEntry* next) :
  _klass(k), _instance_count(0), _instance_words(0), _next(next) {
  _elements = new (ResourceObj::C_HEAP) GrowableArray<oop>(INITIAL_ENTRY_ARRAY_SIZE,true);
}

int ObjectInfoEntry::compare(ObjectInfoEntry* e1, ObjectInfoEntry* e2) {
  if(e1->_instance_words > e2->_instance_words) {
    return -1;
  } else if(e1->_instance_words < e2->_instance_words) {
    return 1;
  }
  return 0;
}

int ObjectInfoEntry::oop_compare(oop *o1, oop *o2) {
  jint o1_refs, o2_refs;
  PersistentObjectInfo *o1_poi, *o2_poi;
  oop _o1, _o2;

  o1_refs = o2_refs = 0;
  _o1 = *o1;
  _o2 = *o2;
  o1_poi  = PSScavenge::obj_poi(_o1);
  o2_poi  = PSScavenge::obj_poi(_o2);
  o1_refs = o1_poi->val_load_cnt() + o1_poi->val_store_cnt();
  o2_refs = o2_poi->val_load_cnt() + o2_poi->val_store_cnt();

  if(o1_refs > o2_refs) {
    return -1;
  } else if(o1_refs < o2_refs) {
    return 1;
  }
  return 0;
}

/* MRJ -- TODO: clean this up */
/* NOTE: this function also resets the load and store counts for each oop */
void ObjectInfoEntry::print_oop_info(outputStream *st, oop cur_oop) const {
  jint load_cnt, store_cnt, id;

  load_cnt = store_cnt = BAD_REF_CNT;
  id = BOTTOM_OOP_ID;

  PersistentObjectInfo *poi = PSScavenge::obj_poi(cur_oop);
  guarantee(poi, "wtf");

  load_cnt  = poi->val_load_cnt();
  store_cnt = poi->val_store_cnt();
  id        = poi->id();

  //st->print("       %p          %p", cur_oop, poi);

  /* formatted print */
  if (TotalRefCounts) {
    if (cur_oop->blueprint()->oop_is_instance() ||
        cur_oop->blueprint()->oop_is_array()) {
      guarantee(load_cnt != BAD_REF_CNT, "bad ref count");
      jint total_cnt = load_cnt + store_cnt;
      if (TrimObjectInfo) {
        st->print_cr(INT64_FORMAT_W(13) "  "
                     INT64_FORMAT_W(13) "  ",
                     (jlong)id,
                     (jlong)total_cnt);
      } else {
        st->print_cr("        %s " INT64_FORMAT_W(13) "  " INT64_FORMAT_W(13),
                     cur_oop->print_address_string(),
                     (jlong)(cur_oop->size() * HeapWordSize),
                     (jlong)total_cnt);
      }
    } else {
      if (TrimObjectInfo) {
        st->print_cr(INT64_FORMAT_W(13) "             NA",
                     (jlong)id);
      } else {
        st->print_cr("        %s " INT64_FORMAT_W(13) "             NA",
                     cur_oop->print_address_string(),
                     (jlong)(cur_oop->size() * HeapWordSize));
      }
    }
  } else {
    if (cur_oop->blueprint()->oop_is_instance() ||
        cur_oop->blueprint()->oop_is_array()) {
      guarantee(load_cnt != BAD_REF_CNT, "bad ref count");
      if (TrimObjectInfo) {
        st->print_cr(INT64_FORMAT_W(13) "  " INT64_FORMAT_W(13)
                     "  " INT64_FORMAT_W(13),
                     (jlong)id,
                     (jlong)load_cnt,
                     (jlong)store_cnt);
      } else {
        st->print_cr("        %s " INT64_FORMAT_W(13) "  " INT64_FORMAT_W(13)
                     "  " INT64_FORMAT_W(13),
                     cur_oop->print_address_string(),
                     (jlong)(cur_oop->size() * HeapWordSize),
                     (jlong)load_cnt,
                     (jlong)store_cnt);
      }
    } else {
      if (TrimObjectInfo) {
        st->print_cr(INT64_FORMAT_W(13) "             NA"
                     "             NA", (jlong)id);
      } else {
        st->print_cr("        %s " INT64_FORMAT_W(13) "             NA"
                     "             NA",
                     cur_oop->print_address_string(),
                     (jlong)(cur_oop->size() * HeapWordSize));
      }
    }
  }
}

void ObjectInfoEntry::long_print_on(outputStream* st) const {
  ResourceMark rm;
  const char* name;

  if (_klass->klass_part()->name() != NULL) {
    name = _klass->klass_part()->external_name();
  } else {
    if (_klass == Universe::klassKlassObj())             name = "<klassKlass>";             else
    if (_klass == Universe::arrayKlassKlassObj())        name = "<arrayKlassKlass>";        else
    if (_klass == Universe::objArrayKlassKlassObj())     name = "<objArrayKlassKlass>";     else
    if (_klass == Universe::instanceKlassKlassObj())     name = "<instanceKlassKlass>";     else
    if (_klass == Universe::typeArrayKlassKlassObj())    name = "<typeArrayKlassKlass>";    else
    if (_klass == Universe::boolArrayKlassObj())         name = "<boolArrayKlass>";         else
    if (_klass == Universe::charArrayKlassObj())         name = "<charArrayKlass>";         else
    if (_klass == Universe::singleArrayKlassObj())       name = "<singleArrayKlass>";       else
    if (_klass == Universe::doubleArrayKlassObj())       name = "<doubleArrayKlass>";       else
    if (_klass == Universe::byteArrayKlassObj())         name = "<byteArrayKlass>";         else
    if (_klass == Universe::shortArrayKlassObj())        name = "<shortArrayKlass>";        else
    if (_klass == Universe::intArrayKlassObj())          name = "<intArrayKlass>";          else
    if (_klass == Universe::longArrayKlassObj())         name = "<longArrayKlass>";         else
    if (_klass == Universe::methodKlassObj())            name = "<methodKlass>";            else
    if (_klass == Universe::constMethodKlassObj())       name = "<constMethodKlass>";       else
    if (_klass == Universe::methodDataKlassObj())        name = "<methodDataKlass>";        else
    if (_klass == Universe::constantPoolKlassObj())      name = "<constantPoolKlass>";      else
    if (_klass == Universe::constantPoolCacheKlassObj()) name = "<constantPoolCacheKlass>"; else
    if (_klass == Universe::compiledICHolderKlassObj())  name = "<compiledICHolderKlass>";  else
      name = "<no name>";
  }

  //oop first_obj = elements()->at(0);

  // simplify the formatting (ILP32 vs LP64) - always cast the numbers to 64-bit
  //st->print_cr("%s", name);

  st->print("    * %s : ", name);
  if (_klass->klass_part()->oop_is_instance())          st->print("%s ", "instance");
  if (_klass->klass_part()->oop_is_array())             st->print("%s ", "array");
  if (_klass->klass_part()->oop_is_objArray_slow())     st->print("%s ", "objArray_slow");
  if (_klass->klass_part()->oop_is_klass())             st->print("%s ", "klass");
  if (_klass->klass_part()->oop_is_thread())            st->print("%s ", "thread");
  if (_klass->klass_part()->oop_is_method())            st->print("%s ", "method");
  if (_klass->klass_part()->oop_is_constMethod())       st->print("%s ", "constMethod");
  if (_klass->klass_part()->oop_is_methodData())        st->print("%s ", "methodData");
  if (_klass->klass_part()->oop_is_constantPool())      st->print("%s ", "constantPool");
  if (_klass->klass_part()->oop_is_constantPoolCache()) st->print("%s ", "constantPoolCache");
  if (_klass->klass_part()->oop_is_typeArray_slow())    st->print("%s ", "typeArray_slow");
  if (_klass->klass_part()->oop_is_arrayKlass())        st->print("%s ", "arrayKlass");
  if (_klass->klass_part()->oop_is_objArrayKlass())     st->print("%s ", "objArrayKlass");
  if (_klass->klass_part()->oop_is_typeArrayKlass())    st->print("%s ", "typeArrayKlass");
  if (_klass->klass_part()->oop_is_compiledICHolder())  st->print("%s ", "compiledICHolder");
  if (_klass->klass_part()->oop_is_instanceKlass())     st->print("%s ", "instanceKlass");
  st->print_cr(" ");

#if 0
  st->print_cr("        total                " INT64_FORMAT_W(13) "  (%d)",
               ((julong) _instance_words * HeapWordSize),
               (jlong)  _instance_count);
#endif

  elements()->sort(ObjectInfoEntry::oop_compare);
  for(int i=0; i < elements()->length(); i++) {
    print_oop_info(st, elements()->at(i));
  }
}

void ObjectInfoEntry::trim_print_on(outputStream* st) const {
  ResourceMark rm;
  if (elements()->length() > 0) {
    st->print(" *");
    print_oop_info(st, elements()->at(0));
  }
  for(int i=1; i < elements()->length(); i++) {
    st->print("  ");
    print_oop_info(st, elements()->at(i));
  }
}

void ObjectInfoEntry::print_on(outputStream* st) const {
#if 0
  if (TrimObjectInfo) {
    trim_print_on(st);
  } else {
    long_print_on(st);
  }
#endif
  long_print_on(st);
}

ObjectInfoEntry* ObjectInfoBucket::lookup(const klassOop k) {
  ObjectInfoEntry* oie = _list;
  while (oie != NULL) {
    if (oie->is_equal(k)) {
      return oie;
    }
    oie = oie->next();
  }
  oie = new ObjectInfoEntry(k, list());
  // We may be out of space to allocate the new entry.
  if (oie != NULL) {
    set_list(oie);
  }
  return oie;
}

void ObjectInfoBucket::iterate(ObjectInfoClosure* oic) {
  ObjectInfoEntry* oie = _list;
  while (oie != NULL) {
    oic->do_cinfo(oie);
    oie = oie->next();
  }
}

void ObjectInfoBucket::empty() {
  ObjectInfoEntry* oie = _list;
  _list = NULL;
  while (oie != NULL) {
    ObjectInfoEntry* next = oie->next();
    delete oie;
    oie = next;
  }
}

ObjectInfoTable::ObjectInfoTable(int size, HeapWord* ref) {
  _size = 0;
  _ref = ref;
  _buckets = NEW_C_HEAP_ARRAY(ObjectInfoBucket, size);
  if (_buckets != NULL) {
    _size = size;
    for (int index = 0; index < _size; index++) {
      _buckets[index].initialize();
    }
  }
  reset_age_tables();
}

ObjectInfoTable::~ObjectInfoTable() {
  if (_buckets != NULL) {
    for (int index = 0; index < _size; index++) {
      _buckets[index].empty();
    }
    FREE_C_HEAP_ARRAY(ObjectInfoBucket, _buckets);
    _size = 0;
  }
}

uint ObjectInfoTable::hash(klassOop p) {
  assert(Universe::heap()->is_in_permanent((HeapWord*)p), "all klasses in permgen");
  return (uint)(((uintptr_t)p - (uintptr_t)_ref) >> 2);
}

ObjectInfoEntry* ObjectInfoTable::lookup(const klassOop k) {
  uint         idx = hash(k) % _size;
  assert(_buckets != NULL, "Allocation failure should have been caught");
  ObjectInfoEntry* oie = _buckets[idx].lookup(k);
  // Lookup may fail if this is a new klass for which we
  // could not allocate space for an new entry.
  assert(oie == NULL || k == oie->klass(), "must be equal");
  return oie;
}

void ObjectInfoTable::print_age_tables(outputStream *out, char *pref) {
  int i;

  out->print_cr("%4s live ages:", pref);
  for (i = 0; i < AGE_TABLE_SIZE; i++) {
    if (_live_object_age_table[i] > 0) {
      out->print_cr("          %5d %13d objects, %13d KB", i,
        _live_object_age_table[i], ((_live_size_age_table[i]*HeapWordSize)>>10));
    }
  }
  out->print_cr(" %4s hot ages:", pref);
  for (i = 0; i < AGE_TABLE_SIZE; i++) {
    if (_hot_object_age_table[i] > 0) {
      out->print_cr("          %5d %13d objects, %13d KB", i,
        _hot_object_age_table[i], ((_hot_size_age_table[i]*HeapWordSize)>>10));
    }
  }
}

void ObjectInfoTable::reset_age_tables() {
  int i;
  for (i = 0; i < AGE_TABLE_SIZE; i++) {
    _live_object_age_table[i] = 0;
    _live_size_age_table[i]   = 0;
  }
  for (i = 0; i < AGE_TABLE_SIZE; i++) {
    _hot_object_age_table[i] = 0;
    _hot_size_age_table[i]   = 0;
  }
}

void ObjectInfoTable::append_to_live_age_tables(const oop obj) {
  markOop test_mark = obj->mark();
  int age = (test_mark->has_displaced_mark_helper() /* o->has_displaced_mark() */) ?
    test_mark->displaced_mark_helper()->age() : test_mark->age();

  if (age >= MAX_AGE) {
    _live_object_age_table[MAX_AGE] += 1;
    _live_size_age_table[MAX_AGE]   += obj->size();
  } else {
    _live_object_age_table[age] += 1;
    _live_size_age_table[age]   += obj->size();
  }
}

void ObjectInfoTable::append_to_hot_age_tables(const oop obj) {
  markOop test_mark = obj->mark();
  int age = (test_mark->has_displaced_mark_helper() /* o->has_displaced_mark() */) ?
    test_mark->displaced_mark_helper()->age() : test_mark->age();

  if (age >= MAX_AGE) {
    _hot_object_age_table[MAX_AGE] += 1;
    _hot_size_age_table[MAX_AGE]   += obj->size();
  } else {
    _hot_object_age_table[age] += 1;
    _hot_size_age_table[age]   += obj->size();
  }
}

// Return false if the entry could not be recorded on account
// of running out of space required to create a new entry.
bool ObjectInfoTable::append_instance(const oop obj) {
  klassOop      k = obj->klass();
  ObjectInfoEntry* oie = lookup(k);

  // koil may be NULL if it's a new klass for which we
  // could not allocate space for a new entry in the hashtable.
  if (oie != NULL) {
    oie->set_count(oie->count() + 1);
    oie->set_words(oie->words() + obj->size());
    oie->elements()->append(obj);
    return true;
  } else {
    return false;
  }
}

void ObjectInfoTable::iterate(ObjectInfoClosure* oic) {
  assert(_size == 0 || _buckets != NULL, "Allocation failure should have been caught");
  for (int index = 0; index < _size; index++) {
    _buckets[index].iterate(oic);
  }
}

int ObjectInfoHisto::sort_helper(ObjectInfoEntry** e1, ObjectInfoEntry** e2) {
  return (*e1)->compare(*e1,*e2);
}

ObjectInfoHisto::ObjectInfoHisto(int estimatedCount) {
  _elements = new (ResourceObj::C_HEAP) GrowableArray<ObjectInfoEntry*>(estimatedCount,true);
}

ObjectInfoHisto::~ObjectInfoHisto() {
  delete _elements;
}

void ObjectInfoHisto::set_title(const char* title) {
  _title = title;
}

void ObjectInfoHisto::add(ObjectInfoEntry* cie) {
  elements()->append(cie);
}

void ObjectInfoHisto::sort() {
  elements()->sort(ObjectInfoHisto::sort_helper);
}

void ObjectInfoHisto::print_elements(outputStream* st) const {
  // simplify the formatting (ILP32 vs LP64) - store the sum in 64-bit
  jlong total = 0;
  julong totalw = 0;
  for(int i=0; i < elements()->length(); i++) {
    if (!TrimObjectInfo)
      st->print("%4d: ", i+1);
    elements()->at(i)->print_on(st);
    total += elements()->at(i)->count();
    totalw += elements()->at(i)->words();
  }
  if (!TrimObjectInfo) {
    st->print_cr("  All classes: " INT64_FORMAT_W(13) "  (%d)\n",
                 (totalw * HeapWordSize), total);
  }
}

void ObjectInfoHisto::print_on(outputStream* st) const {
  if (!TrimObjectInfo)
    st->print_cr("%s",title());
  print_elements(st);
}

class ObjectHistoClosure : public ObjectInfoClosure {
 private:
  ObjectInfoHisto* _cih;
 public:
  ObjectHistoClosure(ObjectInfoHisto* cih) : _cih(cih) {}

  void do_cinfo(ObjectInfoEntry* cie) {
    _cih->add(cie);
  }
};

class PersistentObjectHistoClosure : public PersistentObjectInfoClosure {
 private:
  PersistentObjectInfoHisto* _cih;
 public:
  PersistentObjectHistoClosure(PersistentObjectInfoHisto* cih) : _cih(cih) {}

  void do_cinfo(PersistentObjectInfoEntry* cie) {
    _cih->add(cie);
  }
};


class AppendInstanceClosure : public ObjectClosure {
 private:
  ObjectInfoTable* _oit;
  size_t _missed_count;
  jint _live_objects, _live_size, _live_refs,
        _hot_objects,  _hot_size,  _hot_refs;
  static bool _post_gc, _only_unmarked;

 public:
  AppendInstanceClosure(ObjectInfoTable* oit, bool post_gc, bool only_unmarked=false) :
    _oit(oit), _missed_count(0), _live_objects(0), _live_size(0),
    _live_refs(0), _hot_objects(0), _hot_size(0), _hot_refs(0) {
      _post_gc = post_gc;
      _only_unmarked = only_unmarked;
    }

  jint live_objects() { return _live_objects; }
  jint live_size()    { return _live_size;    }
  jint live_refs()    { return _live_refs;    }
  jint hot_objects()  { return _hot_objects;  }
  jint hot_size()     { return _hot_size;     }
  jint hot_refs()     { return _hot_refs;     }

  static inline bool should_include(oop obj) {
    if (!obj->is_oop()) {
      tty->print("should_include got bad obj: %p", obj); tty->flush();
      //tty->print(", id=%d\n", PSScavenge::obj_poi(obj)->id()); tty->flush();
      return false;
    }

    if (!PSScavenge::obj_is_initialized(obj)) {
      return false;
    }

    /* if we are post-gc, all the objects just had their ages incremented --
     * so give them an extra interval for the ProfileAgeThreshold
     */
    if ( (obj->age() - (_post_gc?1:0)) <= ProfileAgeThreshold ) {
      return false;
    }

    if (_only_unmarked && PSScavenge::obj_poi(obj)->mark()) {
      return false;
    }

    return true;
  }

  static inline bool referenced(oop obj) {
    //tty->print_cr("referenced: %p", obj);
    return (PSScavenge::obj_poi(obj)->val_load_cnt()  > 0 || 
            PSScavenge::obj_poi(obj)->val_store_cnt() > 0);
  }

  static inline jint obj_refs(oop obj) {
    //tty->print_cr("obj_refs: %p", obj);
    return (PSScavenge::obj_refs(obj));
  }

  static inline jint obj_id(oop obj) {
    return PSScavenge::obj_poi(obj)->id();
  }

  void do_object(oop obj) {
    if (should_include(obj)) {
      PSScavenge::obj_poi(obj)->alloc_point()->mark_val(obj);
      if (referenced(obj)) {
        if (obj_refs(obj) < 0) {
          tty->print_cr("negative refs! obj=%p, id=%7d, refs=0x%x\n",
                        obj, obj_id(obj), obj_refs(obj));
          return;
        }
        guarantee(obj_refs(obj) >= 0, "negative refs!");
        _live_refs += obj_refs(obj);
      }
      _live_objects++;
      _live_size += obj->size();
      _oit->append_to_live_age_tables(obj);
      if (PSScavenge::obj_is_hot(obj)) {
        _hot_objects++;
        _hot_size += obj->size();
        if (referenced(obj)) {
          _hot_refs += obj_refs(obj);
        }
        _oit->append_to_hot_age_tables(obj);
      }
      if (!TrimObjectInfo || (PrintReferencedObjects && referenced(obj))) {
        if (!_oit->append_instance(obj)) {
          _missed_count++;
        }
      }
    }
  }

  size_t missed_count() { return _missed_count; }
};

bool AppendInstanceClosure::_post_gc = false;
bool AppendInstanceClosure::_only_unmarked = false;


class AllocPointInfoClosure : public ObjectClosure {
 private:
  static bool _only_unmarked;
 public:
  AllocPointInfoClosure(bool only_unmarked) {
    _only_unmarked = only_unmarked;
  }

  static inline AllocPointInfo *alloc_point(oop obj) {
    return PSScavenge::obj_poi(obj)->alloc_point();
  }

  static inline bool should_include(oop obj) {
    if (!obj->is_oop()) {
      tty->print("should_include got bad obj: %p", obj); tty->flush();
      //tty->print(", id=%d\n", PSScavenge::obj_poi(obj)->id()); tty->flush();
      return false;
    }

    if (!PSScavenge::obj_is_initialized(obj)) {
      return false;
    }

    if (_only_unmarked && PSScavenge::obj_poi(obj)->mark()) {
      return false;
    }

    return true;
  }

  void do_object(oop obj) {
    if (should_include(obj)) {
      alloc_point(obj)->mark_val(obj);
    }
  }
};

bool AllocPointInfoClosure::_only_unmarked = false;

void ObjectInfoCollection::print_persistent_object_info(outputStream *out)
{
  ResourceMark rm;

  PersistentObjectInfoTable *poit = Universe::persistent_object_info_table();

  /* no poit -- nothing to do */
  if (!poit) {
    tty->print("no persistent object info table to print\n");
    return;
  }

  PersistentObjectInfoTable::set_printing(true);
  tty->print("printing persistent object info table\n");
  out->print("persistent object info table: start_time = %ld\n", os::javaTimeMillis());

  // Sort and print object instance info
  PersistentObjectInfoHisto histo(PersistentObjectInfoHisto::histo_initial_size);
  histo.set_title("\n"
    "   i: class name\n"
    "          id     cval    size (bytes)             init  vaddr\n"
    "-------------------------------------------------------------\n"
  );
  PersistentObjectHistoClosure hc(&histo);
  poit->iterate(&hc);
  histo.sort();
  histo.print_on(out);
  PersistentObjectInfoTable::set_printing(false);
}

void ObjectInfoCollection::print_young_collection_stats(outputStream *out)
{
  int i, g;
  out->print_cr("  young collection stats");
  for (g=0; g < PERM_GEN; g++) {
    out->print_cr("  lives (to %5s)",
                  ((PSGenType)g) == OLD_GEN ? "old" : "young");
#if 1
    for (i=0; i < HC_ENUM_TOTAL; i++) {
      out->print_cr(" %17s: %13d objects, %13d KB, %13d refs",
        heapColorEnum2Str((HeapColorEnum)i),
        PSScavenge::live_objects((PSGenType)g,(HeapColorEnum)i),
        ((PSScavenge::live_size((PSGenType)g,(HeapColorEnum)i)*HeapWordSize)>>10),
        PSScavenge::live_refs((PSGenType)g,(HeapColorEnum)i));
    }
#else
    for (i=0; i < HC_ENUM_TOTAL; i++) {
      out->print_cr(" %17s: %13lu objects, %13lu KB",
        heapColorEnum2Str((HeapColorEnum)i),
        PSScavenge::live_objects((PSGenType)g,(HeapColorEnum)i),
        ((PSScavenge::live_size((PSGenType)g,(HeapColorEnum)i)*HeapWordSize)>>10));
    }
#endif
  }
#if 1
  for (g=0; g < PERM_GEN; g++) {
    out->print_cr("   hots (to %5s)", 
                  ((PSGenType)g) == OLD_GEN ? "old" : "young");
    for (i=0; i < HC_ENUM_TOTAL; i++) {
      out->print_cr(" %17s: %13d objects, %13d KB, %13d refs",
        heapColorEnum2Str((HeapColorEnum)i),
        PSScavenge::hot_objects((PSGenType)g,(HeapColorEnum)i),
        ((PSScavenge::hot_size((PSGenType)g,(HeapColorEnum)i)*HeapWordSize)>>10),
        PSScavenge::hot_refs((PSGenType)g,(HeapColorEnum)i));
    }
  }
#endif
  out->print_cr("");
}

void ObjectInfoCollection::print_gen_color_object_info(outputStream *out,
  HeapWord *ref, PSGenType gen_type, HeapColor color, bool header_only,
  bool post_gc)
{
  ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());
  ObjectInfoTable oit(ObjectInfoTable::oit_size, ref);
  if (!oit.allocation_failed()) {
    // Iterate over objects in the heap
    AppendInstanceClosure aic(&oit, post_gc);
    // If this operation encounters a bad object when using CMS,
    // consider using safe_object_iterate() which avoids perm gen
    // objects that may contain bad references.

    /* XXX: MRJ XXX: colored_object_iterate has changed -- I might need to
     * revisit this!
     */
    if (color != HC_NOT_COLORED) {
      switch(gen_type) {
        case YOUNG_GEN:
          psh->young_gen()->colored_object_iterate(&aic, color);
          break;
        case OLD_GEN:
          psh->old_gen()->colored_object_iterate(&aic, color);
          break;
        case PERM_GEN:
          psh->perm_gen()->colored_object_iterate(&aic, color);
          break;
        case ALL_GENS:
          psh->colored_object_iterate(&aic, color);
        default:
          ShouldNotReachHere();
      }
    } else {
      switch(gen_type) {
        case YOUNG_GEN:
          psh->young_gen()->object_iterate(&aic);
          break;
        case OLD_GEN:
          psh->old_gen()->object_iterate(&aic);
          break;
        case PERM_GEN:
          psh->perm_gen()->object_iterate(&aic);
          break;
        case ALL_GENS:
          psh->object_iterate(&aic);
          break;
        default:
          ShouldNotReachHere();
      }
    }

    // Report if certain classes are not counted because of
    // running out of C-heap for the histogram.
    size_t missed_count = aic.missed_count();
    if (missed_count != 0) {
      out->print_cr("WARNING: Ran out of C-heap; undercounted " SIZE_FORMAT
                    " total instances in data below", missed_count);
    }

    char buf[10];
    out->print("  %10s lives: " INT64_FORMAT_W(13) " objects, "
               INT64_FORMAT_W(13) " KB, " INT64_FORMAT_W(13) " refs\n",
               genColorStr(gen_type, color, buf), aic.live_objects(),
               ((aic.live_size()*HeapWordSize) >> 10), aic.live_refs());
    out->print("  %10s  hots: " INT64_FORMAT_W(13) " objects, "
               INT64_FORMAT_W(13) " KB, " INT64_FORMAT_W(13) " refs\n",
               genColorStr(gen_type, color, buf), aic.hot_objects(),
               ((aic.hot_size()*HeapWordSize) >> 10), aic.hot_refs());
    //oit.print_age_tables(out, pref);

    if (!header_only) {
      // Sort and print object instance info
      ObjectInfoHisto histo(ObjectInfoHisto::histo_initial_size);
      if (TotalRefCounts) {
        histo.set_title("\n"
          "   i: class name\n"
          "        vaddr                       #bytes           refs\n"
          "---------------------------------------------------------\n"
        );
      } else {
        histo.set_title("\n"
          "   i: class name\n"
          "        vaddr                       #bytes          loads         stores\n"
          "------------------------------------------------------------------------\n"
        );
      }
      ObjectHistoClosure hc(&histo);
      oit.iterate(&hc);
      histo.sort();
      histo.print_on(out);
    }
  }
}

void ObjectInfoCollection::collect_gen_color_alloc_point_info(bool only_unmarked,
  PSGenType gen_type, HeapColor color)
{
  ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());
  AllocPointInfoClosure apic(only_unmarked);

  if (color != HC_NOT_COLORED) {
    switch(gen_type) {
      case YOUNG_GEN:
        psh->young_gen()->colored_object_iterate(&apic, color);
        break;
      case OLD_GEN:
        psh->old_gen()->colored_object_iterate(&apic, color);
        break;
      case PERM_GEN:
        psh->perm_gen()->colored_object_iterate(&apic, color);
        break;
      case ALL_GENS:
        psh->colored_object_iterate(&apic, color);
      default:
        ShouldNotReachHere();
    }
  } else {
    switch(gen_type) {
      case YOUNG_GEN:
        psh->young_gen()->object_iterate(&apic);
        break;
      case OLD_GEN:
        psh->old_gen()->object_iterate(&apic);
        break;
      case PERM_GEN:
        psh->perm_gen()->object_iterate(&apic);
        break;
      case ALL_GENS:
        psh->object_iterate(&apic);
        break;
      default:
        ShouldNotReachHere();
    }
  }
}

void ObjectInfoCollection::collect_alloc_point_info(bool only_unmarked)
{

  if (UseColoredSpaces) {
    HeapColor color;
    int g, c;
    for (g = 0; g < PERM_GEN; g++) {
      for (c = 0; c < HC_TOTAL; c++) {
        collect_gen_color_alloc_point_info(only_unmarked, (PSGenType)g, (HeapColor)c);
      }
    }
    // do the perm gen
    collect_gen_color_alloc_point_info(only_unmarked, PERM_GEN, HC_NOT_COLORED);
  } else {
    collect_gen_color_alloc_point_info(only_unmarked, ALL_GENS, HC_NOT_COLORED);
  }
}

void ObjectInfoCollection::print_object_info(outputStream *objlog,
  bool header_only, const char *reason, bool post_gc)
{
  ResourceMark rm;
  HeapWord* ref;

  jlong print_start_time, print_stop_time;
  double elapsed_time;

  CollectedHeap* heap = Universe::heap();
  heap->ensure_parsability(false); // must happen, even if collection does
                                   // not happen (e.g. due to GC_locker)

  bool tenured_only = false;
  bool is_shared_heap = false;

  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "invalid heap kind");
  ParallelScavengeHeap* psh = (ParallelScavengeHeap*)heap;
  ref = psh->perm_gen()->object_space()->used_region().start();
  if (OnlyTenuredObjectInfo) {
    tenured_only = true;
  }

#if 0
  /* XXX: removing this may break object organize profile! */
  ObjectInfoCollection::_collecting = true;

  /* update the persistent table */
  PersistentObjectInfoTable *poit = Universe::persistent_object_info_table();
  guarantee(poit != NULL, "no persistent object info table!");
  poit->inc_cur_val();
#endif

  if ((strcmp("post-org", reason) == 0)) {
    print_young_collection_stats(objlog);
  }

#if 0
  /* XXX: removing this may break object organize profile! */
  ObjectInfoCollection::start_time = os::javaTimeMillis();
  guarantee(ObjectInfoCollection::_val_start != 0, "_val_start not started!");
  out->print("%10s ObjectInfoCollection: %-5d ( time: %-14ld  duration: %-8ld )\n",
             reason, poit->cur_val(), ObjectInfoCollection::start_time,
             ObjectInfoCollection::start_time-ObjectInfoCollection::_val_start);
  out->flush();
#endif

  if (TimeObjectInfoPrinting) {
    print_start_time = os::javaTimeMillis();
  }
  objlog->print("object info %s:\n", reason);
  objlog->flush();

#if 0
  AppendPersistentInstanceClosure apic(poit);
  if (tenured_only) {
    psh->tenured_object_iterate(&apic);
  } else {
    psh->object_iterate(&apic);
  }

  // Report if certain classes are not counted because of
  // running out of C-heap for the histogram.
  size_t missed_count = apic.missed_count();
  if (missed_count != 0) {
    out->print_cr("WARNING: Ran out of C-heap; undercounted " SIZE_FORMAT
                   " total instances in data below",
                   missed_count);
  }
#endif

  // Collect klass instance info
  if (UseColoredSpaces) {
    HeapColor color;
    int g, c;
    for (g = 0; g < PERM_GEN; g++) {
      for (c = 0; c < HC_TOTAL; c++) {
        print_gen_color_object_info(objlog, ref, (PSGenType)g,
                                    (HeapColor)c, header_only, post_gc);
      }
    }
    // do the perm gen
    print_gen_color_object_info(objlog, ref, PERM_GEN, HC_NOT_COLORED,
                                header_only, post_gc);
  } else {
    print_gen_color_object_info(objlog, ref, ALL_GENS, HC_NOT_COLORED,
                                header_only, post_gc);
  }
  objlog->flush();

  if (TimeObjectInfoPrinting) {
    print_stop_time = os::javaTimeMillis();
    elapsed_time = ((double) (print_stop_time
                            - print_start_time) / 1000.0);
    objlog->print_cr("ObjectInfoCollection elapsed: %2.4lf   (%ld)\n",
                  elapsed_time, print_stop_time);
    objlog->flush();
  }
#if 0
  /* XXX: removing this may break object organize profile! */
  poit->inc_cur_val();
  ObjectInfoCollection::_val_start = os::javaTimeMillis();
  ObjectInfoCollection::_collecting = false;
#endif
}

void PersistentObjectInfo::mark_load() {
  if (_tot_load_cnt < 0) {
    ResourceMark rm;
    tty->print_cr("warning: negative _tot_load_cnt for "
                  "poi: " INT64_FORMAT_W(12) " ap: " INT64_FORMAT_W(12),
                  (jlong)_id, (jlong)_alloc_point->id());
    Atomic::store(0, &_tot_load_cnt);
  }
  if (UseColoredSpaces) {
    Atomic::inc(&_colored_load_cnt[(int)_color]);
  }
  Atomic::inc(&_val_load_cnt);
  Atomic::inc(&_tot_load_cnt);
}

void PersistentObjectInfo::mark_store() {
  if (_tot_store_cnt < 0) {
    ResourceMark rm;
    tty->print_cr("warning: negative _tot_load_cnt for "
                  "poi: " INT64_FORMAT_W(12) " ap: " INT64_FORMAT_W(12),
                  (jlong)_id, (jlong)_alloc_point);
    Atomic::store(0, &_tot_store_cnt);
  }
  if (UseColoredSpaces) {
    Atomic::inc(&_colored_store_cnt[(int)_color]);
  }
  Atomic::inc(&_val_store_cnt);
  Atomic::inc(&_tot_store_cnt);
}

void PersistentObjectInfo::batch_mark_load(int n) {
  guarantee(_tot_load_cnt >= 0,"ruh roh");
  if (UseColoredSpaces) {
    Atomic::add(n, &_colored_load_cnt[(int)_color]);
  }
  Atomic::add(n, &_val_load_cnt);
  Atomic::add(n, &_tot_load_cnt);
}

void PersistentObjectInfo::batch_mark_store(int n) {
  guarantee(_tot_store_cnt >= 0,"ruh roh");
  if (UseColoredSpaces) {
    Atomic::add(n, &_colored_store_cnt[(int)_color]);
  }
  Atomic::add(n, &_val_store_cnt);
  Atomic::add(n, &_tot_store_cnt);
}

PersistentObjectInfoEntry::PersistentObjectInfoEntry(klassOop k, PersistentObjectInfoEntry* next) :
  _klass(k), _instance_count(0), _instance_words(0), _next(next) {
  _elements = new (ResourceObj::C_HEAP) GrowableArray<PersistentObjectInfo*>(INITIAL_ENTRY_ARRAY_SIZE,true);
  ResourceMark rm;
  FILE *_klass_name_stream;

  _klass_name_stream = open_memstream(&_klass_name, &_klass_name_len);
  if (_klass->klass_part()->name() != NULL) {
    fprintf(_klass_name_stream, "%s", _klass->klass_part()->external_name());
  } else {
    if (_klass == Universe::klassKlassObj())             fprintf(_klass_name_stream, "<klassKlass>");             else
    if (_klass == Universe::arrayKlassKlassObj())        fprintf(_klass_name_stream, "<arrayKlassKlass>");        else
    if (_klass == Universe::objArrayKlassKlassObj())     fprintf(_klass_name_stream, "<objArrayKlassKlass>");     else
    if (_klass == Universe::instanceKlassKlassObj())     fprintf(_klass_name_stream, "<instanceKlassKlass>");     else
    if (_klass == Universe::typeArrayKlassKlassObj())    fprintf(_klass_name_stream, "<typeArrayKlassKlass>");    else
    if (_klass == Universe::boolArrayKlassObj())         fprintf(_klass_name_stream, "<boolArrayKlass>");         else
    if (_klass == Universe::charArrayKlassObj())         fprintf(_klass_name_stream, "<charArrayKlass>");         else
    if (_klass == Universe::singleArrayKlassObj())       fprintf(_klass_name_stream, "<singleArrayKlass>");       else
    if (_klass == Universe::doubleArrayKlassObj())       fprintf(_klass_name_stream, "<doubleArrayKlass>");       else
    if (_klass == Universe::byteArrayKlassObj())         fprintf(_klass_name_stream, "<byteArrayKlass>");         else
    if (_klass == Universe::shortArrayKlassObj())        fprintf(_klass_name_stream, "<shortArrayKlass>");        else
    if (_klass == Universe::intArrayKlassObj())          fprintf(_klass_name_stream, "<intArrayKlass>");          else
    if (_klass == Universe::longArrayKlassObj())         fprintf(_klass_name_stream, "<longArrayKlass>");         else
    if (_klass == Universe::methodKlassObj())            fprintf(_klass_name_stream, "<methodKlass>");            else
    if (_klass == Universe::constMethodKlassObj())       fprintf(_klass_name_stream, "<constMethodKlass>");       else
    if (_klass == Universe::methodDataKlassObj())        fprintf(_klass_name_stream, "<methodDataKlass>");        else
    if (_klass == Universe::constantPoolKlassObj())      fprintf(_klass_name_stream, "<constantPoolKlass>");      else
    if (_klass == Universe::constantPoolCacheKlassObj()) fprintf(_klass_name_stream, "<constantPoolCacheKlass>"); else
    if (_klass == Universe::compiledICHolderKlassObj())  fprintf(_klass_name_stream, "<compiledICHolderKlass>");  else
      fprintf(_klass_name_stream, "<no name>");
  }

  if (_klass->klass_part()->oop_is_instance())          fprintf(_klass_name_stream, " : instance");
  if (_klass->klass_part()->oop_is_array())             fprintf(_klass_name_stream, " : array");
  if (_klass->klass_part()->oop_is_objArray())          fprintf(_klass_name_stream, " : objArray");
  if (_klass->klass_part()->oop_is_typeArray())         fprintf(_klass_name_stream, " : typeArray");
  if (_klass->klass_part()->oop_is_klass())             fprintf(_klass_name_stream, " : klass");
  if (_klass->klass_part()->oop_is_thread())            fprintf(_klass_name_stream, " : thread");
  if (_klass->klass_part()->oop_is_method())            fprintf(_klass_name_stream, " : method");
  if (_klass->klass_part()->oop_is_constMethod())       fprintf(_klass_name_stream, " : constMethod");
  if (_klass->klass_part()->oop_is_methodData())        fprintf(_klass_name_stream, " : methodData");
  if (_klass->klass_part()->oop_is_constantPool())      fprintf(_klass_name_stream, " : constantPool");
  if (_klass->klass_part()->oop_is_constantPoolCache()) fprintf(_klass_name_stream, " : constantPoolCache");
  if (_klass->klass_part()->oop_is_arrayKlass())        fprintf(_klass_name_stream, " : arrayKlass");
  if (_klass->klass_part()->oop_is_objArrayKlass())     fprintf(_klass_name_stream, " : objArrayKlass");
  if (_klass->klass_part()->oop_is_typeArrayKlass())    fprintf(_klass_name_stream, " : typeArrayKlass");
  if (_klass->klass_part()->oop_is_compiledICHolder())  fprintf(_klass_name_stream, " : compiledICHolder");
  if (_klass->klass_part()->oop_is_instanceKlass())     fprintf(_klass_name_stream, " : instanceKlass");
  fclose(_klass_name_stream);
}

int PersistentObjectInfoEntry::compare(PersistentObjectInfoEntry* e1, PersistentObjectInfoEntry* e2) {
  if(e1->_instance_count > e2->_instance_count) {
    return -1;
  } else if(e1->_instance_count < e2->_instance_count) {
    return 1;
  }
  return 0;
}

int PersistentObjectInfoEntry::poi_compare(PersistentObjectInfo **o1, PersistentObjectInfo **o2) {
  PersistentObjectInfo *_o1 = *(o1);
  PersistentObjectInfo *_o2 = *(o2);

  if (_o1->_id < _o2->_id) {
    return -1;
  } else if (_o1->_id < _o2->_id) {
    return 1;
  }
  return 0;
}

bool PersistentObjectInfoEntry::contains(int id) {
  return (find_poi(id) != NULL);
}

PersistentObjectInfo* PersistentObjectInfoEntry::append_new_poi(jint id,
  jint cval, int size, AllocPointInfo *api, HeapColor color,
  intptr_t addr) {
  static int cnt = 0;
#if 0
  PersistentObjectInfo *poi = new PersistentObjectInfo(id,cval,size,
                                                       ap_klass,ap_name,
                                                       ap_sig,ap_bci,
                                                       color, addr);
#endif
  //tty->print_cr("alloc_point: %p\n", api);
  PersistentObjectInfo *poi = new PersistentObjectInfo(id,cval,size,
                                    api, color, addr);
  if (poi == NULL)
    return NULL;
  //tty->print_cr("my alloc_point: %p\n", poi->alloc_point());

  /* we grab the ObjectInfoTable_lock in append_instance */
  set_count(count() + 1);
  set_words(words() + size);
  elements()->append(poi);

  return poi;
}

/* find the poi for the given id */
PersistentObjectInfo* PersistentObjectInfoEntry::find_poi(jint id) {
  PersistentObjectInfo *poi = NULL;
  for(int i=0; i < elements()->length(); i++) {
    if (elements()->at(i)->id() == id) {
      poi = elements()->at(i);
    }
  }
  return poi;
}

void PersistentObjectInfoEntry::print_poi(outputStream *st,
  PersistentObjectInfo *cur_poi) const {
  jlong cnt_a, cnt_b;

  if (UseColoredSpaces) {
    cnt_a = (jlong)cur_poi->_colored_load_cnt[HC_RED] +
            (jlong)cur_poi->_colored_store_cnt[HC_RED];
    cnt_b = (jlong)cur_poi->_colored_load_cnt[HC_BLUE] +
            (jlong)cur_poi->_colored_store_cnt[HC_BLUE];
  } else {
    if (TotalRefCounts) {
      cnt_a = (jlong)cur_poi->_tot_load_cnt + (jlong)cur_poi->_tot_store_cnt;
      cnt_b = 0;
    } else {
      cnt_a = (jlong)cur_poi->_tot_load_cnt;
      cnt_b = (jlong)cur_poi->_tot_store_cnt;
    }
  }
  st->print_cr(INT64_FORMAT_W(12)   " " INT64_FORMAT_W(8)  " "
               INT64_FORMAT_W(12)   " " INT64_FORMAT_W(12) " "
               INT64_FORMAT_W(12)   " " INT64_FORMAT_W(12),
               //INT64_FORMAT_W(8) ") {"INTPTR_FORMAT"}",
               (jlong)cur_poi->id(),
               (jlong)cur_poi->cval(),
               (cur_poi->size()*HeapWordSize),
               cnt_a, cnt_b,
               (jlong)cur_poi->alloc_point()->id());
#if 0
  st->print_cr(INT64_FORMAT_W(12)   " " INT64_FORMAT_W(8)  " "
               INT64_FORMAT_W(12)   " " INT64_FORMAT_W(12) " "
               INT64_FORMAT_W(12)   " ({" INTPTR_FORMAT", "
               INTPTR_FORMAT", "INTPTR_FORMAT"}, "
               INT64_FORMAT_W(8) ")",
               //INT64_FORMAT_W(8) ") {"INTPTR_FORMAT"}",
               (jlong)cur_poi->id(),
               (jlong)cur_poi->cval(),
               (cur_poi->size()*HeapWordSize),
               cnt_a, cnt_b,
               cur_poi->ap_klass(),
               cur_poi->ap_name(),
               cur_poi->ap_signature(),
               (jlong)cur_poi->ap_bci());
#endif
}

void PersistentObjectInfoEntry::print_on(outputStream* st) const {
  ResourceMark rm;
  const char* name;
  //oop first_obj = elements()->at(0);

  // simplify the formatting (ILP32 vs LP64) - always cast the numbers to 64-bit
  //st->print_cr("%s", name);
  st->print_cr("%s", _klass_name);
  elements()->sort(PersistentObjectInfoEntry::poi_compare);
  for(int i=0; i < elements()->length(); i++) {
    print_poi(st, elements()->at(i));
  }
}

PersistentObjectInfoEntry* PersistentObjectInfoBucket::lookup(const klassOop k) {
  PersistentObjectInfoEntry* oie = _list;
  while (oie != NULL) {
    if (oie->is_equal(k)) {
      return oie;
    }
    oie = oie->next();
  }
  oie = new PersistentObjectInfoEntry(k, list());
  // We may be out of space to allocate the new entry.
  if (oie != NULL) {
    set_list(oie);
  }
  return oie;
}

void PersistentObjectInfoBucket::iterate(PersistentObjectInfoClosure* oic) {
  PersistentObjectInfoEntry* oie = _list;
  while (oie != NULL) {
    oic->do_cinfo(oie);
    oie = oie->next();
  }
}

void PersistentObjectInfoBucket::empty() {
  PersistentObjectInfoEntry* oie = _list;
  _list = NULL;
  while (oie != NULL) {
    PersistentObjectInfoEntry* next = oie->next();
    delete oie;
    oie = next;
  }
}

PersistentObjectInfoTable::PersistentObjectInfoTable(int size, HeapWord* ref) {
  _size = 0;
  _ref = ref;
  _printing = false;
  _cur_val = 0;
  _buckets = NEW_C_HEAP_ARRAY(PersistentObjectInfoBucket, size);
  if (_buckets != NULL) {
    _size = size;
    for (int index = 0; index < _size; index++) {
      _buckets[index].initialize();
    }
  }
}

PersistentObjectInfoTable::~PersistentObjectInfoTable() {
  if (_buckets != NULL) {
    for (int index = 0; index < _size; index++) {
      _buckets[index].empty();
    }
    FREE_C_HEAP_ARRAY(PersistentObjectInfoBucket, _buckets);
    _size = 0;
  }
}

uint PersistentObjectInfoTable::hash(klassOop p) {
  assert(Universe::heap()->is_in_permanent((HeapWord*)p), "all klasses in permgen");
  return (uint)(((uintptr_t)p - (uintptr_t)_ref) >> 2);
}

PersistentObjectInfoEntry* PersistentObjectInfoTable::lookup(const klassOop k) {
  uint         idx = hash(k) % _size;
  assert(_buckets != NULL, "Allocation failure should have been caught");
  PersistentObjectInfoEntry* oie = _buckets[idx].lookup(k);
  // Lookup may fail if this is a new klass for which we
  // could not allocate space for an new entry.
  assert(oie == NULL || k == oie->klass(), "must be equal");
  return oie;
}

// Return false if the entry could not be recorded on account
// of running out of space required to create a new entry.
PersistentObjectInfo* PersistentObjectInfoTable::append_instance(const oop obj,
  AllocPointInfo *api, HeapColor color) {
#if 0
  ResourceMark rm;
#endif
  klassOop      k = obj->klass();
  if (k != NULL && k->klass_part()->name() != NULL) {
    PersistentObjectInfoEntry* poie = lookup(k);
    // poie may be NULL if it's a new klass for which we
    // could not allocate space for a new entry in the hashtable.
    if (poie != NULL) {
      CollectedHeap *heap = Universe::heap();
      PersistentObjectInfo *poi = NULL;
      /* add a poi if this oop has not been seen before */
      if (obj->blueprint()->oop_is_instance()) {
        ObjectInfoTable_lock->lock_without_safepoint_check();
        instanceOop inst_oop = ((instanceOop)obj);
        inst_oop->initialize(heap->fresh_oop_id(), _cur_val);
#if 0
        objinfo_log->print_cr("oop:   "INTPTR_FORMAT"  "
                              "klass: "INTPTR_FORMAT"  "
                              "mname: "INTPTR_FORMAT"  "
                              "msig:  "INTPTR_FORMAT"  "
                              "--> %s.%s %s %d  ",
                              ap_method, ap_method->method_holder()->klass_part()->name(),
                              ap_method->name(), ap_method->signature(),
                              ap_method->method_holder()->klass_part()->name()->as_C_string(),
                              ap_method->name()->as_C_string(),
                              ap_method->signature()->as_C_string(), ap_bci);
#endif
        poi = poie->append_new_poi(inst_oop->id(), _cur_val, obj->size(),
                                   api, color, (intptr_t)obj);
        if (poi == NULL) return false;
        guarantee (heap->valid_id(inst_oop->id()), "invalid id!!");
        ObjectInfoTable_lock->unlock();
      } else if (obj->blueprint()->oop_is_array()) {
        ObjectInfoTable_lock->lock_without_safepoint_check();
        arrayOop arr_oop = ((arrayOop)obj);
        arr_oop->initialize(heap->fresh_oop_id(), _cur_val);
#if 0
        objinfo_log->print_cr("oop:   "INTPTR_FORMAT"  "
                              "klass: "INTPTR_FORMAT"  "
                              "mname: "INTPTR_FORMAT"  "
                              "msig:  "INTPTR_FORMAT"  "
                              "--> %s.%s %s %d  ",
                              ap_method, ap_method->method_holder()->klass_part()->name(),
                              ap_method->name(), ap_method->signature(),
                              ap_method->method_holder()->klass_part()->name()->as_C_string(),
                              ap_method->name()->as_C_string(),
                              ap_method->signature()->as_C_string(), ap_bci);
#endif
        poi = poie->append_new_poi(arr_oop->id(), _cur_val, obj->size(),
                                   api, color, (intptr_t)obj);
        if (poi == NULL) return false;
        guarantee (heap->valid_id(arr_oop->id()), "invalid id!!");
        ObjectInfoTable_lock->unlock();
      }
      return poi;
    }
  }
  return NULL;
}

void PersistentObjectInfoTable::iterate(PersistentObjectInfoClosure* oic) {
  assert(_size == 0 || _buckets != NULL, "Allocation failure should have been caught");
  for (int index = 0; index < _size; index++) {
    _buckets[index].iterate(oic);
  }
}

int PersistentObjectInfoHisto::sort_helper(PersistentObjectInfoEntry** e1, PersistentObjectInfoEntry** e2) {
  return (*e1)->compare(*e1,*e2);
}

PersistentObjectInfoHisto::PersistentObjectInfoHisto(int estimatedCount) {
  _elements = new (ResourceObj::C_HEAP) GrowableArray<PersistentObjectInfoEntry*>(estimatedCount,true);
}

PersistentObjectInfoHisto::~PersistentObjectInfoHisto() {
  delete _elements;
}

void PersistentObjectInfoHisto::set_title(const char* title) {
  _title = title;
}

void PersistentObjectInfoHisto::add(PersistentObjectInfoEntry* cie) {
  elements()->append(cie);
}

void PersistentObjectInfoHisto::sort() {
  elements()->sort(PersistentObjectInfoHisto::sort_helper);
}

void PersistentObjectInfoHisto::print_elements(outputStream* st) const {
  // simplify the formatting (ILP32 vs LP64) - store the sum in 64-bit
  jlong total = 0;
  julong totalw = 0;
  for(int i=0; i < elements()->length(); i++) {
    st->print("%4d: ", i+1);
    elements()->at(i)->print_on(st);
    total += elements()->at(i)->count();
    totalw += elements()->at(i)->words();
  }
  st->print_cr("  All classes: " INT64_FORMAT_W(13) " KB  (%d instances)\n",
               ((totalw * HeapWordSize) >> 10), total);
}

void PersistentObjectInfoHisto::print_on(outputStream* st) const {
  st->print_cr("%s",title());
  print_elements(st);
}

class PersistentObjectClosure : public ObjectInfoClosure {
 private:
  PersistentObjectInfoHisto* _cih;
 public:
  PersistentObjectClosure(PersistentObjectInfoHisto* cih) : _cih(cih) {}

  void do_cinfo(PersistentObjectInfoEntry* cie) {
    _cih->add(cie);
  }
};
#endif //PROFILE_OBJECT_INFO




//----------------------------------------------------------
// Implementation of ObjectLayout
ObjectLayoutTask*    ObjectLayout::_task = NULL;
bool                 ObjectLayout::_organizing = false;
jlong                ObjectLayout::start_time = 0;
jlong                ObjectLayout::stop_time  = 0;
jlong                ObjectLayout::_val_start  = 0;

void ObjectLayoutTask::task() {
  if (!ObjectLayout::_organizing) {
      os::signal_notify(SIGBREAK);
  }
}

/*
 * The engage() method is called at initialization time via
 * Thread::create_vm() to initialize the ObjectLayout and
 * register it with the WatcherThread as a periodic task.
 */
void ObjectLayout::engage() {
  if (!is_active()) {
    ObjectLayout::_val_start = os::javaTimeMillis();
    tty->print_cr("start time = %14ld", ObjectLayout::_val_start);
#ifdef PROFILE_OBJECT_INFO
    ObjectInfoCollection::_val_start = ObjectLayout::_val_start;
    objinfo_log->print_cr("start time = %14ld", ObjectInfoCollection::_val_start);
#endif
    // start up the periodic task
    _task = new ObjectLayoutTask((int)ObjectLayoutInterval);
    _task->enroll();
  }
}

/*
 * the disengage() method is responsible for deactivating the periodic
 * task. This  method is called from before_exit() in java.cpp and is only called
 * after the WatcherThread has been stopped.
 */
void ObjectLayout::disengage() {
  if (is_active()) {
    _task->disenroll();
    delete _task;
    _task = NULL;
  }
}

#if 0
void ObjectLayout::check_lock (outputStream *out)
{
  if (instanceRefKlass::owns_pending_list_lock(JavaThread::current())) {
    out->print_cr("Lock is held by current thread");
  } else {
    out->print_cr("Lock not held!");
  }
}
#endif

#ifdef PROFILE_OBJECT_INFO
class ResetRefCntClosure : public ObjectClosure {
  void do_object(oop obj) {
    if (!obj->is_oop()) {
      tty->print("obj: %p", obj); tty->flush();
      //tty->print(", id=%d\n", PSScavenge::obj_poi(obj)->id()); tty->flush();
      return;
    }
    if (PSScavenge::obj_is_initialized(obj)) {
      PersistentObjectInfo *poi = PSScavenge::obj_poi(obj);
      poi->reset_val_load_cnt();
      poi->reset_val_store_cnt();
      poi->reset_is_new();
      poi->reset_mark();
    }
  }
};

void ObjectLayout::reset_ref_cnts()
{
  ResourceMark rm;

  CollectedHeap* heap = Universe::heap();
  bool tenured_only = false;

  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "invalid heap kind");
  ParallelScavengeHeap* psh = (ParallelScavengeHeap*)heap;
  if (OnlyTenuredObjectInfo) {
    tenured_only = true;
  }

  ResetRefCntClosure rrcc;
  if (tenured_only) {
    psh->tenured_object_iterate(&rrcc);
  } else {
    psh->object_iterate(&rrcc);
  }
}

void ObjectInfoCollection::collect_object_info(outputStream *objlog, 
  outputStream *aplog, const char *reason)
{
  ResourceMark rm;
  jlong cur_time;
  AllocPointInfoTable *apit;
  bool skip_gc;
  assert(Universe::heap()->kind() == CollectedHeap::ParallelScavengeHeap,
         "organize_objects: must be parallel scavenge heap");

  ParallelScavengeHeap* heap = ((ParallelScavengeHeap*)Universe::heap());
  PersistentObjectInfoTable *poit = Universe::persistent_object_info_table();
  guarantee(poit != NULL, "no persistent object info table!");

  ObjectInfoCollection::_collecting = true;
  cur_time = os::javaTimeMillis();
  guarantee(ObjectInfoCollection::_val_start != 0, "_val_start not started!");

  objlog->print("%10s ObjectInfoCollection: %-5d ( time: %-14ld  duration: %-8ld )\n",
                      reason, poit->cur_val(), cur_time,
                      cur_time-ObjectInfoCollection::_val_start);
  objlog->flush();

  skip_gc = false;
  if ( (strcmp(reason, "before-GC") == 0) || (strcmp(reason, "after-GC") == 0) ) {
    skip_gc = true;
  }

  if (PrintAPInfoAtInterval) {
    apit = Universe::alloc_point_info_table();
    guarantee(apit != NULL, "no alloc point info table!");

    aplog->print("%10s APInfoCollection: %-5d ( time: %-14ld  duration: %-8ld )\n",
                       reason, poit->cur_val(), cur_time,
                       cur_time-ObjectInfoCollection::_val_start);
    aplog->flush();
  }

  if (ObjectInfoWithGC && !skip_gc) {
    /* print_object_info will collect all the alloc point info */
    ObjectInfoCollection::print_object_info(objlog, false, "pre-gc", false);
#if 0
    ObjectLayout::reset_ref_cnts();
#endif

    /* GC can find new objects -- don't print out the alloc point info just yet */
    heap->young_collect_as_vm_thread(GCCause::_object_info_collection);

    /* another scan to just get the new objects from the GC */
    collect_alloc_point_info(true);
    if (PrintAPInfoAtInterval) {
      aplog->print_cr("    allocation point info %-5d pre-gc:", poit->cur_val());
      apit->print_val_info(aplog);
      apit->reset_val_cnts();
      aplog->print_cr(""); aplog->flush();
    }

#if 0
    if (PrintAPInfoAtInterval) {
      apit->print_new_val_info(aplog);
      apit->reset_ref_cnts();
      aplog->print_cr(""); aplog->flush();
    }
#endif

    /* the GC will discover objects that don't have a known AP -- print those
     * out and reset again
     */
    ObjectInfoCollection::print_object_info(objlog, true, "post-GC", true);
#if 0
    ObjectLayout::reset_ref_cnts(); // ref_cnts should always be 0 -- but do this anyway
#endif

    if (PrintAPInfoAtInterval) {
      aplog->print_cr("    allocation point info %-5d post-gc:", poit->cur_val());
      apit->print_val_info(aplog);
      apit->reset_val_cnts();
      aplog->print_cr(""); aplog->flush();
    }
#if 1
    ObjectLayout::reset_ref_cnts(); // ref_cnts should always be 0 -- but do this anyway
#endif

  } else {
    //collect_alloc_point_info(false);
    ObjectInfoCollection::print_object_info(objlog, false, reason, false);
    ObjectLayout::reset_ref_cnts();
    if (PrintAPInfoAtInterval) {
      aplog->print_cr("    allocation point info %-5d:", poit->cur_val());
      apit->print_val_info(aplog);
      apit->reset_val_cnts();
      aplog->print_cr(""); aplog->flush();
    }
  }

  ObjectInfoCollection::task()->reset_counter();
  poit->inc_cur_val();
  ObjectInfoCollection::_val_start = os::javaTimeMillis();
  ObjectInfoCollection::_collecting = false;
}
#endif /* PROFILE_OBJECT_INFO */

void ObjectLayout::organize_objects(outputStream *out, const char *reason)
{
  ResourceMark rm;
  assert(Universe::heap()->kind() == CollectedHeap::ParallelScavengeHeap,
         "organize_objects: must be parallel scavenge heap");
  assert(UseColoredSpaces, "organize_objects: no colored spaces");

  ParallelScavengeHeap* heap = ((ParallelScavengeHeap*)Universe::heap());

  ObjectLayout::_organizing = true;
  ObjectLayout::start_time = os::javaTimeMillis();
  guarantee(ObjectLayout::_val_start != 0, "_val_start not started!");
  out->print("%10s ObjectLayout: ( time: %-14ld  duration: %-8ld )\n",
             reason, ObjectLayout::start_time,
             ObjectLayout::start_time-ObjectLayout::_val_start);
  out->flush();

#ifdef PROFILE_OBJECT_INFO
  if (ProfileObjectInfo) {
    PSScavenge::reset_object_copy_profile();
    ObjectInfoCollection::print_object_info(out, false, "pre-org", false);
  }
#endif

  heap->young_collect_as_vm_thread(GCCause::_object_organize);
  heap->ensure_parsability(false); // must happen, even if collection does
                                   // not happen (e.g. due to GC_locker)

#ifdef PROFILE_OBJECT_INFO
  if (ProfileObjectInfo) {
    ObjectInfoCollection::print_object_info(out, true, "post-org", true);
    //ObjectInfoCollection::print_object_info(out, false /* header_only */, "post-org");
    reset_ref_cnts();
  }
#endif

  ObjectLayout::_val_start = os::javaTimeMillis();
  _task->reset_counter();
  ObjectLayout::_organizing = false;
}

#ifdef PROFILE_OBJECT_INFO
void AllocPointInfo::mark_val(oop o) {
  guarantee(_tot_store_cnt >= 0,"ruh roh");
  PersistentObjectInfo *poi = PSScavenge::obj_poi(o);
  guarantee(poi != NULL, "mark_val bad oop");
  jint obj_loads  = poi->val_load_cnt();
  jint obj_stores = poi->val_store_cnt();
  jint obj_is_new = poi->is_new();
  jint mark       = poi->mark();
  jint obj_size   = o->size();

  _val_objects += 1;
  _val_size    += obj_size;
  if (obj_loads > 0) {
    _val_load_cnt += obj_loads;
    if (!mark) {
      _tot_load_cnt += obj_loads;
    }
  }

  if (obj_stores > 0) {
    _val_store_cnt += obj_stores;
    if (!mark) {
      _tot_store_cnt += obj_stores;
    }
  }
  
  if ((obj_loads > 0) || (obj_stores > 0)) {
    _val_hot_objects += 1;
    _val_hot_size    += obj_size;
  }

  if (obj_is_new) {
    _val_new_objects += 1;
    _val_new_size    += obj_size;
  }

  jlong val_cnt = _val_load_cnt + _val_store_cnt;
  poi->set_mark();
#if 0
  Atomic::inc(&_val_objects);
  Atomic::add(o->size(), &_val_size);

  if (obj_loads > 0) {
    Atomic::add(obj_loads, &_val_load_cnt);
    Atomic::add(obj_loads, &_tot_load_cnt);
  }

  if (obj_stores > 0) {
    Atomic::add(obj_stores, &_val_store_cnt);
    Atomic::add(obj_stores, &_tot_store_cnt);
  }
  
  if ((obj_loads > 0) || (obj_stores > 0)) {
    Atomic::inc(&_val_hot_objects);
    Atomic::add(o->size(), &_val_hot_size);
  }

  if (obj_is_new) {
    Atomic::inc(&_val_new_objects);
    Atomic::add(o->size(), &_val_new_size);
  }
  poi->set_mark();
#endif
}

#if 0
void AllocPointInfo::mark_load(HeapColor color) {
  guarantee(_tot_load_cnt >= 0,"ruh roh");
  if (UseColoredSpaces) {
    Atomic::inc(&_colored_store_cnt[(int)_color]);
  }
  Atomic::inc(&_val_store_cnt);
  Atomic::inc(&_tot_store_cnt);
}

void AllocPointInfo::mark_store(HeapColor color) {
  guarantee(_tot_store_cnt >= 0,"ruh roh");
  if (UseColoredSpaces) {
    Atomic::inc(&_colored_store_cnt[(int)_color]);
  }
  Atomic::inc(&_val_store_cnt);
  Atomic::inc(&_tot_store_cnt);
}

void AllocPointInfo::mark_hot(oop o) {
  Atomic::inc(&_val_hot_objects);
  Atomic::add(o->size(), &_val_hot_size);
}
#endif

void AllocPointInfo::mark_new_object(int size) {
  guarantee(_tot_store_cnt >= 0,"ruh roh");
#if 0
  if (UseColoredSpaces) {
    Atomic::add(size, &_colored_store_cnt[(int)_color]);
  }
  Atomic::add(size, &_val_store_cnt);
  Atomic::add(size, &_tot_store_cnt);

  Atomic::add(size, &_val_hot_size);
  Atomic::inc(&_val_hot_objects);
#endif
#if 0
  Atomic::add(size, &_total_size);
  Atomic::inc(&_total_objects);
#endif
  _total_objects += 1;
  _total_size    += size;

#if 0
  Atomic::add(size, &_val_new_size);
  Atomic::inc(&_val_new_objects);
#endif
}

void AllocPointInfo::add_val_to_totals(AllocPointInfoTable *apit) {
  apit->add_val_total_size(_val_size);
  apit->add_val_total_objects(_val_objects);
  apit->add_val_total_new_size(_val_new_size);
  apit->add_val_total_new_objects(_val_new_objects);
  apit->add_val_total_hot_size(_val_hot_size);
  apit->add_val_total_hot_objects(_val_hot_objects);
  apit->add_val_total_load_cnt(_val_load_cnt);
  apit->add_val_total_store_cnt(_val_store_cnt);
}

void AllocPointInfo::reset_ref_cnts() {
  _val_load_cnt    = 0;
  _val_store_cnt   = 0;
  _val_hot_size    = 0;
  _val_hot_objects = 0;
  _val_new_size    = 0;
  _val_new_objects = 0;
}

void AllocPointInfo::reset_val_cnts() {
  _val_size        = 0;
  _val_objects     = 0;
  reset_ref_cnts();
}

AllocPointInfoEntry::AllocPointInfoEntry(const char *key, unsigned int id,
  AllocPointInfoEntry *next, HeapColor color) {
	unsigned int key_len;

  key_len = strlen(key);
	_key    = NEW_C_HEAP_ARRAY(char, key_len+1  );
  strcpy(_key, key);

  _value  = new AllocPointInfo(id, color);
  _next   = next;
}

AllocPointInfoEntry::~AllocPointInfoEntry()
{
  FREE_C_HEAP_ARRAY(AllocPointInfoEntry, _key);
  _next = NULL;
}

bool AllocPointInfoEntry::matches(const char *key)
{
  if (_key != NULL) {
    if (strcmp(_key, key) == 0) {
      return true;
    }
  }
  return false;
}

AllocPointInfoEntry* AllocPointInfoBucket::get_apie(const char *key)
{
	AllocPointInfoEntry *apie;

	apie = _aps;
	while (apie != NULL) {
    if (apie->matches(key)) {
      return apie;
    }
    apie = apie->next();
	}
	return NULL;
}

void AllocPointInfoEntry::print_map_on(outputStream *out)
{
  jlong total_objects = _value->total_objects();
  jlong total_size    = (_value->total_size()*HeapWordSize);
  jlong total_cnt     = _value->tot_load_cnt() + _value->tot_store_cnt();

  guarantee(total_objects >= 0, "total objects overflowed");
  guarantee(total_size    >= 0, "total size overflowed");
  guarantee(total_cnt     >= 0, "total ref cnt overflowed");

  out->print_cr("%-7d %-16ld %-16ld %-16ld %s", _value->id(),
    total_objects, total_size, total_cnt, _key);
}

void AllocPointInfoEntry::print_val_info(outputStream *out)
{
  jlong val_objects     = _value->val_objects();
  jlong val_size        = (_value->val_size()*HeapWordSize);
  jlong val_new_objects = _value->val_new_objects();
  jlong val_new_size    = (_value->val_new_size()*HeapWordSize);
  jlong val_hot_objects = _value->val_hot_objects();
  jlong val_hot_size    = (_value->val_hot_size()*HeapWordSize);
  jlong val_cnt         = _value->val_load_cnt() + _value->val_store_cnt();

  guarantee(val_objects     >= 0, "val objects overflowed");
  guarantee(val_size        >= 0, "val size overflowed");
  guarantee(val_new_objects >= 0, "val new objects overflowed");
  guarantee(val_new_size    >= 0, "val new size overflowed");
  guarantee(val_hot_objects >= 0, "val hot objects overflowed");
  guarantee(val_hot_size    >= 0, "val hot size overflowed");
  guarantee(val_cnt         >= 0, "val ref cnt overflowed");

  out->print_cr("%-7d | %16ld %16ld |  %16ld %16ld |  %16ld %16ld |  %16ld",
    _value->id(),
    val_objects, val_size, val_hot_objects, val_hot_size,
    val_new_objects, val_new_size, val_cnt);
}

#if 0
void AllocPointInfoEntry::print_new_val_info(outputStream *out)
{
  if (_value->val_new_objects() > 0) {
    out->print_cr("%-7d | %16d %16d |  %16d %16d |  %16d %16d |  %16d",
      _value->id(),
      _value->val_objects(),     (_value->val_size()*HeapWordSize),
      _value->val_new_objects(), (_value->val_new_size()*HeapWordSize),
      _value->val_hot_objects(), (_value->val_hot_size()*HeapWordSize),
      _value->val_load_cnt() + _value->val_store_cnt());
  }
}
#endif

void AllocPointInfoEntry::add_val_to_totals(AllocPointInfoTable *apit)
{
  _value->add_val_to_totals(apit);
}

void AllocPointInfoEntry::reset_val_cnts()
{
  _value->reset_val_cnts();
}

void AllocPointInfoEntry::reset_ref_cnts()
{
  _value->reset_ref_cnts();
}

void AllocPointInfoBucket::empty() {
  AllocPointInfoEntry* apie = _aps;
  _aps = NULL;
  while (apie != NULL) {
    AllocPointInfoEntry* next = apie->next();
    delete apie;
    apie = next;
  }
}

AllocPointInfoEntry* AllocPointInfoBucket::insert(const char *key,
  unsigned int value, HeapColor color)
{
	/* Check if we can handle insertion by simply replacing
	 * an existing value in a key-value pair in the bucket.
	 */

  guarantee((get_apie(key) == NULL), "alloc point already exists!");

#if 0
	if ((apie = get_apie(key)) != NULL) {
		/* The bucket contains a pair that matches the provided key,
		 * change the value for that pair to the new value.
		 */
    apie->set_value(value);
	}
#endif
  AllocPointInfoEntry *new_apie = new AllocPointInfoEntry(key, value, _aps, color);
  _aps = new_apie;
  return new_apie;
}

void AllocPointInfoBucket::print_map_on(outputStream *out)
{
	AllocPointInfoEntry *apie = _aps;
	while (apie != NULL) {
    apie->print_map_on(out);
    apie = apie->next();
	}
}

void AllocPointInfoBucket::print_val_info(outputStream *out)
{
	AllocPointInfoEntry *apie = _aps;
	while (apie != NULL) {
    apie->print_val_info(out);
    apie = apie->next();
	}
}

#if 0
void AllocPointInfoBucket::print_new_val_info(outputStream *out)
{
	AllocPointInfoEntry *apie = _aps;
	while (apie != NULL) {
    apie->print_new_val_info(out);
    apie = apie->next();
	}
}
#endif

void AllocPointInfoBucket::accumulate_val_totals(AllocPointInfoTable *apit)
{
	AllocPointInfoEntry *apie = _aps;
	while (apie != NULL) {
    apie->add_val_to_totals(apit);
    apie = apie->next();
	}
}

void AllocPointInfoBucket::reset_val_cnts()
{
	AllocPointInfoEntry *apie = _aps;
	while (apie != NULL) {
    apie->reset_val_cnts();
    apie = apie->next();
	}
}

void AllocPointInfoBucket::reset_ref_cnts()
{
	AllocPointInfoEntry *apie = _aps;
	while (apie != NULL) {
    apie->reset_ref_cnts();
    apie = apie->next();
	}
}

AllocPointInfoTable::AllocPointInfoTable(unsigned int capacity)
{
	_size = 0;
  _cur_id = 0;
  _buckets = NEW_C_HEAP_ARRAY(AllocPointInfoBucket, capacity);
  if (_buckets != NULL) {
    _size = capacity;
    for (unsigned int index = 0; index < _size; index++) {
      _buckets[index].initialize();
    }
  }
}

AllocPointInfoTable::~AllocPointInfoTable()
{
  if (_buckets != NULL) {
    for (unsigned int index = 0; index < _size; index++) {
      _buckets[index].empty();
    }
    FREE_C_HEAP_ARRAY(AllocPointInfoBucket, _buckets);
    _size = 0;
  }
}

AllocPointInfo *AllocPointInfoTable::get(methodOopDesc *ap_method, int ap_bci,
  HeapColor color)
{
  ResourceMark rm;
	unsigned int index;
  Symbol *ap_klass, *ap_name, *ap_sig;
  char key[MAXPATHLEN];
	AllocPointInfoEntry *apie;

  assert(_buckets != NULL, "AllocPointInfoTable buckets is null");

  if (ap_method) {
    ap_klass = ap_method->method_holder()->klass_part()->name();
    ap_name  = ap_method->name();
    ap_sig   = ap_method->signature();
#if 0
    objinfo_log->print_cr ("  ap_method: "
                           "(" INTPTR_FORMAT
                           ", "INTPTR_FORMAT
                           ", "INTPTR_FORMAT")"
                           " --> %s.%s %s",
                 ap_klass, ap_name, ap_sig, ap_klass->as_C_string(),
                 ap_name->as_C_string(), ap_sig->as_C_string());
#endif
    sprintf(key, "%s.%s %s %d", ap_klass->as_C_string(),
            ap_name->as_C_string(), ap_sig->as_C_string(), ap_bci);
  } else {
    ap_klass = ap_name = ap_sig = NULL;
    sprintf(key, "unknown-AP");
  }

	index = hash(key) % _size;
  ObjectInfoTable_lock->lock_without_safepoint_check();
	apie  = _buckets[index].get_apie(key);

  if (apie == NULL) {
	  apie = _buckets[index].insert(key, _cur_id, color);
    guarantee ((apie->value()->id() == _cur_id), "bad AllocPointInfoTable insert");
    _cur_id++;
  }

  guarantee ((strcmp(apie->key(), key) == 0), "bad AllocPointInfoTable insert");
  ObjectInfoTable_lock->unlock();
	return apie->value();
}

unsigned long AllocPointInfoTable::hash(const char *str)
{
	unsigned long hash = 5381;
	int c;

	while (c = *str++) {
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

void AllocPointInfoTable::print_map_on(outputStream *out)
{
  out->print_cr("%-7s %-16s %-16s %-16s %s",
    "ID", "Objects", "Size", "Refs", "Allocation Point");

  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].print_map_on(out);
  }
  out->print_cr("");
}

void AllocPointInfoTable::print_val_info(outputStream *out)
{
  out->print_cr("%-7s | %16s %16s |  %16s %16s |  %16s %16s |  %16s",
    "id", "alive_objs", "alive_size", "hot_objs", "hot_size",
    "new_objs", "new_size", "refs");

  compute_val_totals();
  out->print_cr("total   | %16d %16d |  %16d %16d |  %16d %16d |  %16d",
    _val_total_objects,     _val_total_size*HeapWordSize,
    _val_total_hot_objects, _val_total_hot_size*HeapWordSize,
    _val_total_new_objects, _val_total_new_size*HeapWordSize,
    _val_total_load_cnt   + _val_total_store_cnt);

  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].print_val_info(out);
  }
}

#if 0
void AllocPointInfoTable::print_new_val_info(outputStream *out)
{
  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].print_new_val_info(out);
  }
}
#endif

void AllocPointInfoTable::reset_val_totals()
{
  _val_total_objects     = 0;
  _val_total_size        = 0;
  _val_total_new_objects = 0;
  _val_total_new_size    = 0;
  _val_total_hot_objects = 0;
  _val_total_hot_size    = 0;
  _val_total_load_cnt    = 0;
  _val_total_store_cnt   = 0;
}

void AllocPointInfoTable::compute_val_totals()
{
  reset_val_totals();
  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].accumulate_val_totals(this);
  }
}

void AllocPointInfoTable::reset_val_cnts()
{
  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].reset_val_cnts();
  }
}

void AllocPointInfoTable::reset_ref_cnts()
{
  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].reset_ref_cnts();
  }
}
#endif /* PROFILE_OBJECT_INFO */
#ifdef PROFILE_OBJECT_ADDRESS_INFO
#if 0
/* Some allocations are missed during profiling (these are likely allocated in
 * native methods) -- we use this closure to put these missed allocations in
 * the ObjectAddressInfoTable
 */
class InsertObjectAddressClosure : public OopClosure {
 private:
  ObjectAddressInfoTable *_oait;
  bool _count_refs;
 public:
  InsertObjectAddressClosure(ObjectAddressInfoTable *oait, bool count_refs) : _oait(oait),
    _count_refs(count_refs) {}

  static inline bool should_include(oop obj) {
    if (obj && obj->is_oop()) {
      klassOop klass = obj->klass();
      if (klass) {
        if (klass->klass_part()->oop_is_instance()
            || klass->klass_part()->oop_is_array()) {
          return true;
        }
      }
    }
    return false;
  }

  void do_object(oop obj) {
    if (should_include(obj)) {
      if ((_oait->lookup(obj)) == NULL) {
        _oait->inc_missed_objects();
        _oait->add_missed_size(obj->size());
        if (obj->size() > 131072) {
          ResourceMark rm;
          const char* name;;

          addrinfo_log->print_cr("big object missed!");
          klassOop klass = obj->klass();
          if (klass) {
            if (klass->klass_part()->name() != NULL) {
              name = klass->klass_part()->external_name();
            } else {
              if (klass == Universe::klassKlassObj())             name = "<klassKlass>";             else
              if (klass == Universe::arrayKlassKlassObj())        name = "<arrayKlassKlass>";        else
              if (klass == Universe::objArrayKlassKlassObj())     name = "<objArrayKlassKlass>";     else
              if (klass == Universe::instanceKlassKlassObj())     name = "<instanceKlassKlass>";     else
              if (klass == Universe::typeArrayKlassKlassObj())    name = "<typeArrayKlassKlass>";    else
              if (klass == Universe::boolArrayKlassObj())         name = "<boolArrayKlass>";         else
              if (klass == Universe::charArrayKlassObj())         name = "<charArrayKlass>";         else
              if (klass == Universe::singleArrayKlassObj())       name = "<singleArrayKlass>";       else
              if (klass == Universe::doubleArrayKlassObj())       name = "<doubleArrayKlass>";       else
              if (klass == Universe::byteArrayKlassObj())         name = "<byteArrayKlass>";         else
              if (klass == Universe::shortArrayKlassObj())        name = "<shortArrayKlass>";        else
              if (klass == Universe::intArrayKlassObj())          name = "<intArrayKlass>";          else
              if (klass == Universe::longArrayKlassObj())         name = "<longArrayKlass>";         else
              if (klass == Universe::methodKlassObj())            name = "<methodKlass>";            else
              if (klass == Universe::constMethodKlassObj())       name = "<constMethodKlass>";       else
              if (klass == Universe::methodDataKlassObj())        name = "<methodDataKlass>";        else
              if (klass == Universe::constantPoolKlassObj())      name = "<constantPoolKlass>";      else
              if (klass == Universe::constantPoolCacheKlassObj()) name = "<constantPoolCacheKlass>"; else
              if (klass == Universe::compiledICHolderKlassObj())  name = "<compiledICHolderKlass>";  else
                name = "<no name>";
            }
          } else {
            name = "<no name>";
          }
          // simplify the formatting (ILP32 vs LP64) - always cast the numbers to 64-bit
          addrinfo_log->print_cr(INTPTR_FORMAT "  " UINT64_FORMAT_W(13) "  %s",
                                 obj, (julong) obj->size() * HeapWordSize, name);
        }
        if (_count_refs) {
          _oait->add_missed_refs(obj->size());
          _oait->mark_alloc(obj);
        } else {
          _oait->insert(obj);
        }
      }
    }
  }
};

class HeapStatsClosure : public ObjectClosure {
 private:
  ObjectAddressInfoTable *_oait;
  ObjectAddressInfo *_oai;
  size_t _missed_count;
  jint _live_objects, _live_size, _live_refs,
        _hot_objects,  _hot_size, _hot_refs;
 public:
  HeapStatsClosure(ObjectAddressInfoTable *oait) : _oait(oait),
    _live_objects(0), _live_size(0), _live_refs(0),
    _hot_objects(0), _hot_size(0), _hot_refs(0) {    
  }

  jint live_objects() { return _live_objects; }
  jint live_size()    { return _live_size;    }
  jint live_refs()    { return _live_refs;    }
  jint hot_objects()  { return _hot_objects;  }
  jint hot_size()     { return _hot_size;     }
  jint hot_refs()     { return _hot_refs;     }

  static inline bool should_include(oop obj) {
    if (obj)
      return true;

#if 0
    if (obj && obj->is_oop()) {
      klassOop klass = obj->klass();
      if (klass) {
        if (klass->klass_part()->oop_is_instance()
            || klass->klass_part()->oop_is_array()) {
          return true;
        }
      }
    }
#endif
    return false;
  }

  void do_object(oop obj) {
    if (should_include(obj)) {
      _live_objects++;
      _live_size += obj->size();
      if (_oai = (_oait->lookup(obj))) {
        jint refs = _oai->load_cnt() + _oai->store_cnt();
        if (refs > 0) {
          _live_refs += refs;
          _hot_refs  += refs;
          _hot_objects++;
          _hot_size += obj->size();
        }
      } else {
#if 0
        if (_pre_gc) {
          /* this is a missed allocation (likely allocated in native code) --
           * add it to the oait
           */
          oait->mark_alloc(obj)
        }
#endif
      }
    }
  }
};
#endif

#if 0
class UnknownPagesClosure : public ObjectClosure {
 private:
  ObjectAddressInfoTable *_oait;
  ObjectAddressInfo *_oai;
  intptr_t _prev_page, _cur_page, _next_page;
  int _val;

  int _unk_objs, _unk_size, _knw_objs, _knw_size;
  intptr_t heap_boundaries[HS_NR_SPACES][NR_MARKS];

 public:
  UnknownPagesClosure(ObjectAddressInfoTable *oait, int val) : _oait(oait),
    _val(val) {
    _prev_page = _next_page = NULL;
    _cur_page = OAR_MARKER;
    _unk_objs = _unk_size = _knw_objs = _knw_size = 0;

    ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());
    heap_boundaries[HS_EDEN_SPACE][BOTTOM_ADDR]     = (intptr_t)psh->young_gen()->eden_space()->bottom();
    heap_boundaries[HS_EDEN_SPACE][END_ADDR]        = (intptr_t)psh->young_gen()->eden_space()->top();
    heap_boundaries[HS_SURVIVOR_SPACE][BOTTOM_ADDR] = (intptr_t)psh->young_gen()->from_space()->bottom();
    heap_boundaries[HS_SURVIVOR_SPACE][END_ADDR]    = (intptr_t)psh->young_gen()->from_space()->top();
    heap_boundaries[HS_TENURED_SPACE][BOTTOM_ADDR]  = (intptr_t)psh->old_gen()->object_space()->bottom();
    heap_boundaries[HS_TENURED_SPACE][END_ADDR]     = (intptr_t)psh->old_gen()->object_space()->top();
    heap_boundaries[HS_PERM_SPACE][BOTTOM_ADDR]     = (intptr_t)psh->perm_gen()->object_space()->bottom();
    heap_boundaries[HS_PERM_SPACE][END_ADDR]        = (intptr_t)psh->perm_gen()->object_space()->top();

    fwrite(&_cur_page, sizeof(intptr_t), 1, addrups_log);
    fwrite(&_val, sizeof(int), 1, addrups_log);

#if 0
    if (PrintTextOAT) {
      addrinfo_log->print_cr("  Pages with unknown objects (val=%d)", _val);
    }
#endif
    addrinfo_log->flush();
  }

  int unk_objs() { return _unk_objs; }
  int unk_size() { return _unk_size; }
  int knw_objs() { return _knw_objs; }
  int knw_size() { return _knw_size; }

  bool obj_in_eden(oop obj) {
    if ( ((intptr_t)obj) >= heap_boundaries[HS_EDEN_SPACE][BOTTOM_ADDR] &&
         ((intptr_t)obj) <  heap_boundaries[HS_EDEN_SPACE][END_ADDR] ) {
      return true;
    }
    return false;
  }

  bool obj_in_survivor(oop obj) {
    if ( ((intptr_t)obj) >= heap_boundaries[HS_SURVIVOR_SPACE][BOTTOM_ADDR] &&
         ((intptr_t)obj) <  heap_boundaries[HS_SURVIVOR_SPACE][END_ADDR] ) {
      return true;
    }
    return false;
  }

  bool obj_in_tenured(oop obj) {
    if ( ((intptr_t)obj) >= heap_boundaries[HS_TENURED_SPACE][BOTTOM_ADDR] &&
         ((intptr_t)obj) <  heap_boundaries[HS_TENURED_SPACE][END_ADDR] ) {
      return true;
    }
    return false;
  }

  bool obj_in_perm(oop obj) {
    if ( ((intptr_t)obj) >= heap_boundaries[HS_PERM_SPACE][BOTTOM_ADDR] &&
         ((intptr_t)obj) <  heap_boundaries[HS_PERM_SPACE][END_ADDR] ) {
      return true;
    }
    return false;
  }

  bool page_in_survivor(intptr_t page) {
    if ( page >= heap_boundaries[HS_SURVIVOR_SPACE][BOTTOM_ADDR] &&
         page <  heap_boundaries[HS_SURVIVOR_SPACE][END_ADDR] ) {
      return true;
    }
    return false;
  }

  void print_cur_page() {
    if (_cur_page != _prev_page) {
      fwrite(&_cur_page, sizeof(intptr_t), 1, addrups_log);
#if 0
      if (PrintTextOAT) {
        addrinfo_log->print_cr("%p", _cur_page);
      }
#endif
#if 0
      if (page_in_survivor(_cur_page)) {
        addrinfo_log->print_cr("  %p", _cur_page);
      }
#endif
      _prev_page = _cur_page;
    }
  }

  void do_object(oop obj) {
    ObjectAddressInfo *oai = _oait->lookup(obj);
#if 0
    if (obj == (void*)0x6c06e0000) {
      ObjectAddressInfoTable *oait     = Universe::object_address_info_table();
      ObjectAddressInfoTable *alt_oait = Universe::alt_oait();
      ObjectAddressInfo *oai           = oait->lookup(obj);
      ObjectAddressInfo *alt_oai       = alt_oait->lookup(obj);
      tty->print_cr("got %p : oai=%p alt_oai=%p oait=%p alt_oait=%p _oait=%p", obj,
        oai, alt_oai, oait, alt_oait, _oait);
    }
#endif
    if (obj && (oai == NULL)) {
      if (_oait->is_known_free(obj)) {
#if 0
        if (obj_in_eden(obj)) {
          _knw_objs += 1;
          _knw_size += obj->size();
        }
#endif
        //tty->print_cr("inserting known free: %p %d", obj, obj->size()*HeapWordSize);
        _oait->insert(obj, false);
        return;
      }

#if 1
      if (obj_in_survivor(obj)) {
        _unk_objs += 1;
        _unk_size += obj->size();
        tty->print_cr("unk_obj: %p %d", obj, (obj->size()*HeapWordSize));
      }
#endif
#if 0
      if (_oait->cur_val() == 20 || ((obj->size()*HeapWordSize)>>10) > 128) {
        ResourceMark rm;
        const char* name;;

        klassOop klass = obj->klass();
        if (klass) {
          if (klass->klass_part()->name() != NULL) {
            name = klass->klass_part()->external_name();
          } else {
            if (klass == Universe::klassKlassObj())             name = "<klassKlass>";             else
            if (klass == Universe::arrayKlassKlassObj())        name = "<arrayKlassKlass>";        else
            if (klass == Universe::objArrayKlassKlassObj())     name = "<objArrayKlassKlass>";     else
            if (klass == Universe::instanceKlassKlassObj())     name = "<instanceKlassKlass>";     else
            if (klass == Universe::typeArrayKlassKlassObj())    name = "<typeArrayKlassKlass>";    else
            if (klass == Universe::boolArrayKlassObj())         name = "<boolArrayKlass>";         else
            if (klass == Universe::charArrayKlassObj())         name = "<charArrayKlass>";         else
            if (klass == Universe::singleArrayKlassObj())       name = "<singleArrayKlass>";       else
            if (klass == Universe::doubleArrayKlassObj())       name = "<doubleArrayKlass>";       else
            if (klass == Universe::byteArrayKlassObj())         name = "<byteArrayKlass>";         else
            if (klass == Universe::shortArrayKlassObj())        name = "<shortArrayKlass>";        else
            if (klass == Universe::intArrayKlassObj())          name = "<intArrayKlass>";          else
            if (klass == Universe::longArrayKlassObj())         name = "<longArrayKlass>";         else
            if (klass == Universe::methodKlassObj())            name = "<methodKlass>";            else
            if (klass == Universe::constMethodKlassObj())       name = "<constMethodKlass>";       else
            if (klass == Universe::methodDataKlassObj())        name = "<methodDataKlass>";        else
            if (klass == Universe::constantPoolKlassObj())      name = "<constantPoolKlass>";      else
            if (klass == Universe::constantPoolCacheKlassObj()) name = "<constantPoolCacheKlass>"; else
            if (klass == Universe::compiledICHolderKlassObj())  name = "<compiledICHolderKlass>";  else
              name = "<no name>";
          }
        } else {
          name = "<no name>";
        }
        addrinfo_log->print_cr("unknw: " INTPTR_FORMAT "  " UINT64_FORMAT_W(13) "  %s",
                               obj, (julong) obj->size() * HeapWordSize, name);
      }
#endif

#if 0
      static int i=0;
      if (obj_in_eden(obj) && i < 20000) {
        tty->print_cr("eden object: %p %d", obj, (obj->size()*HeapWordSize));
        i++;
      }
#endif
#if 0
      if (obj_in_survivor(obj)) {
        addrinfo_log->print_cr("unknown obj: %p %d", obj, (obj->size()*HeapWordSize));
      }
#endif
      int rem = (obj->size()*HeapWordSize);
      _cur_page = ((((intptr_t)obj) >> PAGE_SHIFT) << PAGE_SHIFT);
      print_cur_page();

      _next_page = _cur_page + PAGE_SIZE;
      rem -= (_next_page - ((intptr_t)obj));
      while (rem > 0) {
        _cur_page = _next_page;
        _next_page += PAGE_SIZE;
        print_cur_page();
        rem -= PAGE_SIZE;
      }
    } else {
      /* MRJ: don't know why this happens -- but it can happen in some
       * benchmarks
       */
      if (oai->size() != obj->size()) {
        oai->set_size(obj->size());
      }
#if 0
      if (obj_in_eden(obj)) {
        _knw_objs += 1;
        _knw_size += obj->size();
      }
#endif
#if 0
      if (obj && _oait->cur_val() == 20) {
        addrinfo_log->print_cr("known: " INTPTR_FORMAT "  " UINT64_FORMAT_W(13),
                               obj, (julong) obj->size() * HeapWordSize);
      }
#endif
#if 0
      if (obj_in_survivor(obj)) {
        addrinfo_log->print_cr("known obj:   %p %d", obj, (obj->size()*HeapWordSize));
      }
#endif
    }
  }
};
#endif

//----------------------------------------------------------
// Implementation of ObjectAddressInfoCollectionTask
ObjectAddressInfoCollectionTask* ObjectAddressInfoCollection::_task = NULL;
bool                      ObjectAddressInfoCollection::_collecting  = false;
jlong                     ObjectAddressInfoCollection::_val_start   = 0;

/*
 * The engage() method is called at initialization time via
 * Thread::create_vm() to initialize the ObjectAddressInfoCollection and
 * register it with the WatcherThread as a periodic task.
 */
void ObjectAddressInfoCollection::engage() {
  if (!is_active()) {
    ObjectAddressInfoCollection::_val_start = os::javaTimeMillis();
    addrinfo_log->print_cr("start time = %14ld", ObjectAddressInfoCollection::_val_start);
    // start up the periodic task
    _task = new ObjectAddressInfoCollectionTask((int)ObjectAddressInfoInterval);
    _task->enroll();
  }
}

/*
 * the disengage() method is responsible for deactivating the periodic
 * task. This  method is called from before_exit() in java.cpp and is only called
 * after the WatcherThread has been stopped.
 */
void ObjectAddressInfoCollection::disengage() {
  if (is_active()) {
    _task->disenroll();
    delete _task;
    _task = NULL;
  }
}

void ObjectAddressInfoCollectionTask::task() {
  if (!ObjectAddressInfoCollection::_collecting) {
      os::signal_notify(SIGBREAK);
  }
}

#if 0
void ObjectAddressInfoCollection::print_oait_summary(outputStream *out, bool pre_gc)
{
  ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());
  ObjectAddressInfoTable *oait;
  bool count_refs;
  
  if (pre_gc) {
    oait = Universe::object_address_info_table();
    count_refs = true;
  } else {
    oait = Universe::alt_oait();
    count_refs = false;
  }

  InsertObjectAddressClosure ioac(oait, count_refs);
  psh->object_iterate(&ioac);

  oait->compute_heap_stats(pre_gc);

  out->print("  lives:   " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " KB, " INT64_FORMAT_W(13) " refs\n",
             oait->live_objects(), ((oait->live_size()*HeapWordSize) >> 10),
             oait->live_refs());
  out->print("  hots:    " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " KB, " INT64_FORMAT_W(13) " refs\n",
             oait->hot_objects(), ((oait->hot_size()*HeapWordSize) >> 10),
             oait->hot_refs());
  out->print("  (missed: " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " KB)\n", oait->missed_objects(),
             ((oait->missed_size()*HeapWordSize) >> 10));
  out->flush();

  guarantee(oait != NULL, "no object address info table!");
}

void ObjectAddressInfoCollection::print_object_address_info(outputStream *out,
  const char *reason)
{
  ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());
  ObjectAddressInfoTable *oait = Universe::object_address_info_table();
  guarantee(oait != NULL, "no object address info table!");

#if 0
  jlong cur_time = os::javaTimeMillis();
  out->print("%10s ObjectAddressInfoCollection: %-5d ( time: %-14ld  duration: %-8ld )\n",
                      reason, oait->cur_val(), cur_time,
                      cur_time-ObjectAddressInfoCollection::_val_start);
  out->flush();
#endif

  HeapStatsClosure yhsc(oait);
  psh->young_gen()->object_iterate(&yhsc);

  out->print("  young lives: " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " bytes, " INT64_FORMAT_W(13) " refs\n",
             yhsc.live_objects(), (yhsc.live_size()*HeapWordSize),
             yhsc.live_refs());
  out->print("  young hots:  " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " bytes, " INT64_FORMAT_W(13) " refs\n",
             yhsc.hot_objects(), (yhsc.hot_size()*HeapWordSize),
             yhsc.hot_refs());
  out->flush();

  HeapStatsClosure ohsc(oait);
  psh->old_gen()->object_iterate(&ohsc);

  out->print("  old lives:   " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " bytes, " INT64_FORMAT_W(13) " refs\n",
             ohsc.live_objects(), (ohsc.live_size()*HeapWordSize),
             ohsc.live_refs());
  out->print("  old hots:    " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " bytes, " INT64_FORMAT_W(13) " refs\n",
             ohsc.hot_objects(), (ohsc.hot_size()*HeapWordSize),
             ohsc.hot_refs());
  out->flush();

  HeapStatsClosure phsc(oait);
  psh->perm_gen()->object_iterate(&phsc);

  out->print("  perm lives:  " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " bytes, " INT64_FORMAT_W(13) " refs\n",
             phsc.live_objects(), (phsc.live_size()*HeapWordSize),
             phsc.live_refs());
  out->print("  perm hots:   " INT64_FORMAT_W(13) " objects, "
             INT64_FORMAT_W(13) " bytes, " INT64_FORMAT_W(13) " refs\n",
             phsc.hot_objects(), (phsc.hot_size()*HeapWordSize),
             phsc.hot_refs());
  out->flush();
}
#endif

void ObjectAddressInfoCollection::collect_object_address_info(outputStream *addrlog, 
  outputStream *fieldlog, const char *reason)
{
  ResourceMark rm;
  assert(Universe::heap()->kind() == CollectedHeap::ParallelScavengeHeap,
         "organize_objects: must be parallel scavenge heap");

  ParallelScavengeHeap* heap       = ((ParallelScavengeHeap*)Universe::heap());
  ObjectAddressInfoTable *oait     = Universe::object_address_info_table();
  ObjectAddressInfoTable *alt_oait = Universe::alt_oait();
  guarantee(oait != NULL, "no object address info table!");

  if (ObjectAddressInfoCollection::_collecting) {
    return;
  }

  ObjectAddressInfoCollection::_collecting = true;
  guarantee(ObjectAddressInfoCollection::_val_start != 0, "_val_start not started!");

  jlong cur_time = os::javaTimeMillis();
  addrlog->print("%10s ObjectAddressInfoCollection: %-5d ( time: %-14ld  duration: %-8ld )\n",
                  reason, oait->cur_val(), cur_time,
                  cur_time-ObjectAddressInfoCollection::_val_start);
  addrlog->flush();

  tty->print("%10s ObjectAddressInfoCollection: %-5d ( time: %-14ld  duration: %-8ld )\n",
                  reason, oait->cur_val(), cur_time,
                  cur_time-ObjectAddressInfoCollection::_val_start);
  tty->flush();

  if (ProfileObjectFieldInfo) {
    fieldlog->print("%10s ObjectAddressInfoCollection: %-5d ( time: %-14ld  duration: %-8ld )\n",
                    reason, oait->cur_val(), cur_time,
                    cur_time-ObjectAddressInfoCollection::_val_start);
    fieldlog->flush();
  }

  /* try to determine if fields on objects created at different times will
   * have different rates of access
   *
   * will need: an interval field on ObjectAddressInfo
   *
   * the profile run does not need to print ref counts at every interval --
   * but it needs to print before every GC so no information is lost
   *
   * at the start of every interval then, create a new record on the
   * KlassRecord object that represents references to new objects created in
   * that interval
   */
  if (strstr(reason, "post-")) {
    alt_oait->use_from_space();
    //UnknownPagesClosure upc(alt_oait, alt_oait->cur_val());
    //heap->object_iterate(&upc);
  } else {
    oait->record_heap_boundaries();
    //UnknownPagesClosure upc(oait, oait->cur_val());
    //heap->object_iterate(&upc);
  }

  if (strstr(reason, "pre-")) {
    if ((strcmp(reason, "pre-minor-gc") == 0)) { 
      oait->copy_to(alt_oait);
      alt_oait->reset_spaces(false /* young only */);
      alt_oait->use_to_space();
    } else /* pre-major-gc */ {
      alt_oait->reset_spaces(true /* old and young */);
    }
  } else if ((strcmp(reason, "post-minor-gc") == 0) ||
             (strcmp(reason, "post-major-gc") == 0)) {
    Universe::switch_obj_addr_tables();
    oait = Universe::object_address_info_table();
    ObjectAddressInfoTable *alt_oait = Universe::alt_oait();
    oait->set_cur_val(alt_oait->cur_val());
    alt_oait->reset_spaces(true /* old and young */);
  }

#if 0
  addrinfo_log->print_cr("unknown objects: " INT64_FORMAT_W(13)
                         " unknown size: " INT64_FORMAT_W(13),
                         upc.unk_objs(), (upc.unk_size()*HeapWordSize));
  addrinfo_log->print_cr("  known objects: " INT64_FORMAT_W(13)
                         "   known size: " INT64_FORMAT_W(13),
                         upc.knw_objs(), (upc.knw_size()*HeapWordSize));
#endif
  oait->compute_heap_stats();
  oait->print_on(addrlog, fieldlog, reason);
  oait->inc_cur_val();
  oait->reset_ref_cnts();
  oait->use_from_space();

  ObjectAddressInfoCollection::task()->reset_counter();
  ObjectAddressInfoCollection::_val_start = os::javaTimeMillis();

  /* if we've reached end of run -- do not allow any more signals */
  if (strcmp(reason, "end-of-run")) {
    ObjectAddressInfoCollection::_collecting = false;
  }
}

KlassRecord::KlassRecord(klassOop k, int instance_size, enum klass_type ktype) :
  _klass(k) {
  /* XXX: eclipse-default throws an error in the very last part of the run
   * with the FIELD_TLAB_INTERVAL configuration in this code -- we can just
   * ignore it for now -- but I'd like to find why the error occurs
   */
  ResourceMark rm;
  FILE *_klass_name_stream;
  int i, j;
  //static int _cnt = 0;

  ParallelScavengeHeap *heap = (ParallelScavengeHeap*)Universe::heap();

  _klass_name     = NULL;
  _klass_name_len = 0;
  if (_klass && _klass != FILLER_KLASS && heap->is_in(_klass->klass_part())) {
    //ObjectAddressInfoTable::_klass_print_order += 1;
    //tty->print_cr("inserting: %p, size: %d", k, instance_size);
    //tty->print_cr("  klass_part: %p", _klass->klass_part());

    _klass_name_stream = open_memstream(&_klass_name, &_klass_name_len);
    if (_klass->klass_part()->name() != NULL) {
      fprintf(_klass_name_stream, "%s", _klass->klass_part()->external_name());
    } else {
      if (_klass == Universe::klassKlassObj())             fprintf(_klass_name_stream, "<klassKlass>");             else
      if (_klass == Universe::arrayKlassKlassObj())        fprintf(_klass_name_stream, "<arrayKlassKlass>");        else
      if (_klass == Universe::objArrayKlassKlassObj())     fprintf(_klass_name_stream, "<objArrayKlassKlass>");     else
      if (_klass == Universe::instanceKlassKlassObj())     fprintf(_klass_name_stream, "<instanceKlassKlass>");     else
      if (_klass == Universe::typeArrayKlassKlassObj())    fprintf(_klass_name_stream, "<typeArrayKlassKlass>");    else
      if (_klass == Universe::boolArrayKlassObj())         fprintf(_klass_name_stream, "<boolArrayKlass>");         else
      if (_klass == Universe::charArrayKlassObj())         fprintf(_klass_name_stream, "<charArrayKlass>");         else
      if (_klass == Universe::singleArrayKlassObj())       fprintf(_klass_name_stream, "<singleArrayKlass>");       else
      if (_klass == Universe::doubleArrayKlassObj())       fprintf(_klass_name_stream, "<doubleArrayKlass>");       else
      if (_klass == Universe::byteArrayKlassObj())         fprintf(_klass_name_stream, "<byteArrayKlass>");         else
      if (_klass == Universe::shortArrayKlassObj())        fprintf(_klass_name_stream, "<shortArrayKlass>");        else
      if (_klass == Universe::intArrayKlassObj())          fprintf(_klass_name_stream, "<intArrayKlass>");          else
      if (_klass == Universe::longArrayKlassObj())         fprintf(_klass_name_stream, "<longArrayKlass>");         else
      if (_klass == Universe::methodKlassObj())            fprintf(_klass_name_stream, "<methodKlass>");            else
      if (_klass == Universe::constMethodKlassObj())       fprintf(_klass_name_stream, "<constMethodKlass>");       else
      if (_klass == Universe::methodDataKlassObj())        fprintf(_klass_name_stream, "<methodDataKlass>");        else
      if (_klass == Universe::constantPoolKlassObj())      fprintf(_klass_name_stream, "<constantPoolKlass>");      else
      if (_klass == Universe::constantPoolCacheKlassObj()) fprintf(_klass_name_stream, "<constantPoolCacheKlass>"); else
      if (_klass == Universe::compiledICHolderKlassObj())  fprintf(_klass_name_stream, "<compiledICHolderKlass>");  else
        fprintf(_klass_name_stream, "<no name>");
    }

    /* for whatever reason -- this code does not work with eclipse-default */
    if (_klass->klass_part()->oop_is_instance())          fprintf(_klass_name_stream, " : instance");
    if (_klass->klass_part()->oop_is_array())             fprintf(_klass_name_stream, " : array");
    if (_klass->klass_part()->oop_is_objArray())          fprintf(_klass_name_stream, " : objArray");
    if (_klass->klass_part()->oop_is_typeArray())         fprintf(_klass_name_stream, " : typeArray");
    if (_klass->klass_part()->oop_is_klass())             fprintf(_klass_name_stream, " : klass");
    if (_klass->klass_part()->oop_is_thread())            fprintf(_klass_name_stream, " : thread");
    if (_klass->klass_part()->oop_is_method())            fprintf(_klass_name_stream, " : method");
    if (_klass->klass_part()->oop_is_constMethod())       fprintf(_klass_name_stream, " : constMethod");
    if (_klass->klass_part()->oop_is_methodData())        fprintf(_klass_name_stream, " : methodData");
    if (_klass->klass_part()->oop_is_constantPool())      fprintf(_klass_name_stream, " : constantPool");
    if (_klass->klass_part()->oop_is_constantPoolCache()) fprintf(_klass_name_stream, " : constantPoolCache");
    if (_klass->klass_part()->oop_is_arrayKlass())        fprintf(_klass_name_stream, " : arrayKlass");
    if (_klass->klass_part()->oop_is_objArrayKlass())     fprintf(_klass_name_stream, " : objArrayKlass");
    if (_klass->klass_part()->oop_is_typeArrayKlass())    fprintf(_klass_name_stream, " : typeArrayKlass");
    if (_klass->klass_part()->oop_is_compiledICHolder())  fprintf(_klass_name_stream, " : compiledICHolder");
    if (_klass->klass_part()->oop_is_instanceKlass())     fprintf(_klass_name_stream, " : instanceKlass");
    fclose(_klass_name_stream);
  }

  if (ktype == KT_UNSPECIFIED) {
    if (_klass && _klass != FILLER_KLASS) {
      if (_klass->klass_part()->oop_is_instance()) {
        _klass_type = KT_VM_INSTANCE;
        _instance_size = instance_size;
      } else if (_klass->klass_part()->oop_is_array()) {
        _klass_type = KT_VM_ARRAY;
        _instance_size = -1;
      } else {
        _klass_type = KT_VM_OTHER;
        _instance_size = -1;
      }
    } else {
      _klass_type = (_klass == FILLER_KLASS) ? KT_VM_FILLER : KT_VM_OTHER;
      _instance_size = -1;
    }
  } else {
    /* we are copying this record */
    _klass_type = ktype;
    _instance_size = instance_size;
  }

  _fields = NULL;
  if (_instance_size > 0) {
    _nr_fields = (_instance_size * HeapWordSize) - OOP_HEADER_SIZE;
    _fields = new (ResourceObj::C_HEAP)
      GrowableArray<FieldRecord*>(_nr_fields,true);

    for (i = 0; i < _nr_fields; i++) {
      FieldRecord *field = new FieldRecord();
      _fields->append(field);
    }
  }

  for (i = 0 ; i < HS_NR_SPACES; i++) {
    for (j = 0; j < NR_KLASS_STATS; j++) {
      _stats[i][j] = 0;
    }
  }
  //tty->print_cr("created klass record: %d size: %d name: %s", _cnt, _instance_size, _klass_name);
  //_cnt++;
}

void KlassRecord::add_to_klass_stats(ObjectAddressInfo *oai, enum heap_space hs)
{
  jint refs = (oai->init_cnt() < 0) ? 
              (oai->load_cnt() + oai->store_cnt()) :
              (oai->load_cnt() + oai->store_cnt() + oai->init_cnt());

  _stats[hs][KS_LIVE_OBJECTS] += 1;
  _stats[hs][KS_LIVE_SIZE]    += oai->size();
  _stats[hs][KS_LIVE_REFS]    += refs;

  if (refs) {
    _stats[hs][KS_HOT_OBJECTS] += 1;
    _stats[hs][KS_HOT_SIZE]    += oai->size();
    _stats[hs][KS_HOT_REFS]    += refs;
  }

  if ((oai->init_cnt() > 0) || oai->init_cnt() == NEW_MARKER) {
    _stats[hs][KS_NEW_OBJECTS] += 1;
    _stats[hs][KS_NEW_SIZE]    += oai->size();
    _stats[hs][KS_NEW_REFS]    += (oai->init_cnt() == NEW_MARKER) ? 0 :
                                   oai->init_cnt();
  }
}

void KlassRecord::reset_klass_stats()
{
  int i, j, nspaces;

  for (i = 0 ; i < HS_NR_SPACES; i++) {
    for (j = 0 ; j < NR_KLASS_STATS; j++) {
      _stats[i][j] = 0;
    }
  }
}

void KlassRecord::reset_ref_cnts()
{
  int i;
  if (_instance_size > 0) {
    for (i = 0; i < _fields->length(); i++) {
      _fields->at(i)->reset_ref_cnts();
    }
  }
  for (i = 0 ; i < HS_NR_SPACES; i++) {
    _stats[i][KS_LIVE_REFS] = 0;
    _stats[i][KS_HOT_REFS] = 0;
    _stats[i][KS_NEW_REFS] = 0;
  }
}

void KlassRecord::mark_field_load(int field_offset, int size)
{
  for (int i = field_offset; i < (size+field_offset); i++) {
    _fields->at(i)->inc_load_cnt();
  }
}

void KlassRecord::mark_field_store(int field_offset, int size)
{
  for (int i = field_offset; i < (size+field_offset); i++) {
    _fields->at(i)->inc_store_cnt();
  }
}

void KlassRecord::print_on(outputStream *out)
{
  char sizebuf[14];
  if (_instance_size >= 0) {
    sprintf(sizebuf, "%13d", (_instance_size*HeapWordSize));
  } else {
    sprintf(sizebuf, "%13s", "-");
  }

  out->print_cr(" %5d : {0x%016x} %s B %s\t\t%s",
                ObjectAddressInfoTable::_klass_print_order,
                _klass, sizebuf, klass_type_str(_klass_type),
                _klass_name);
  out->print_cr("  alive:   %13d %13d %13d",
                get_total_stat(KS_LIVE_OBJECTS),
                (get_total_stat(KS_LIVE_SIZE) * HeapWordSize),
                get_total_stat(KS_LIVE_REFS));
  out->print_cr("    hot:   %13d %13d %13d",
                get_total_stat(KS_HOT_OBJECTS),
                (get_total_stat(KS_HOT_SIZE) * HeapWordSize),
                get_total_stat(KS_HOT_REFS));
  out->print_cr("    new:   %13d %13d %13d",
                get_total_stat(KS_NEW_OBJECTS),
                (get_total_stat(KS_NEW_SIZE) * HeapWordSize),
                get_total_stat(KS_NEW_REFS));

  if (_instance_size > 0) {
    bool first = true;
    for (int i = 0; i < _fields->length(); i++) {
      FieldRecord *cur    = _fields->at(i);
      jlong field_refs    = (jlong)cur->load_cnt() + (jlong)cur->store_cnt();
      if (field_refs > 0) {
        if (first) {
          out->print_cr(" fields:");
          first = false;
        }
        out->print_cr("           %13d %13d", (i+OOP_HEADER_SIZE), field_refs);
      }
    }
  }

  ObjectAddressInfoTable::_klass_print_order += 1;
}

KlassRecordEntry::KlassRecordEntry(klassOop k, int instance_size,
  enum klass_type ktype, KlassRecordEntry *next) {
  _key    = k;
  _value  = new KlassRecord(k, instance_size, ktype);
  _next   = next;
}

KlassRecordEntry::~KlassRecordEntry()
{
  _next = NULL;
}

void KlassRecordEntry::add_to_klass_totals(ObjectAddressInfoTable *oait)
{
  oait->add_to_klass_totals(_value);
}

KlassRecordEntry* KlassRecordBucket::get_kre(klassOop obj)
{
	KlassRecordEntry *kre;

	kre = _kres;
	while (kre != NULL) {
    if (kre->key() == obj) {
      return kre;
    }
    kre = kre->next();
	}
	return NULL;
}

void KlassRecordEntry::reset_klass_stats()
{
  _value->reset_klass_stats();
}

void KlassRecordEntry::reset_ref_cnts()
{
  _value->reset_ref_cnts();
}

void KlassRecordEntry::print_on(outputStream *out)
{
  _value->print_on(out);
}

int KlassRecordEntry::compare(KlassRecordEntry* e1, KlassRecordEntry* e2) {
  if(e1->value()->live_size() > e2->value()->live_size()) {
    return -1;
  } else if(e1->value()->live_size() < e2->value()->live_size()) {
    return 1;
  }
  return 0;
}

void KlassRecordBucket::copy_entries_to(ObjectAddressInfoTable *dst_oait) {
  KlassRecordEntry* kre = _kres;
  while (kre != NULL) {
    klassOop klass        = kre->value()->klass();
    int size              = kre->value()->instance_size();
    enum klass_type ktype = kre->value()->klass_type();

    dst_oait->kt_insert(klass, size, ktype);
    kre = kre->next();
  }
}

void KlassRecordBucket::reset_klass_stats() {
  KlassRecordEntry* kre = _kres;
  while (kre != NULL) {
    kre->reset_klass_stats();
    kre = kre->next();
  }
}

void KlassRecordBucket::reset_ref_cnts() {
  KlassRecordEntry* kre = _kres;
  while (kre != NULL) {
    kre->reset_ref_cnts();
    kre = kre->next();
  }
}

void KlassRecordBucket::empty() {
  KlassRecordEntry* kre = _kres;
  _kres = NULL;
  while (kre != NULL) {
    KlassRecordEntry* next = kre->next();
    delete kre;
    kre = next;
  }
}

KlassRecordEntry* KlassRecordBucket::kt_insert(klassOop k, int instance_size,
  enum klass_type ktype)
{
	/* Check if we can handle insertion by simply replacing
	 * an existing value in a key-value pair in the bucket.
	 */
  /*
  if (k == NULL) {
    tty->print_cr("inserting null klass");
  }
  */
  KlassRecordEntry *kre = get_kre(k);
  /*
  if (kre != NULL) {
    tty->print_cr("klass: %p name: %s", k, kre->value()->klass_name());
  } else {
    tty->print_cr("tried to insert: %p", k);
  }
  */
  guarantee(get_kre(k) == NULL, "inserting klass that's already there!");

  KlassRecordEntry *new_kre = new KlassRecordEntry(k, instance_size, ktype, _kres);
  _kres = new_kre;

  return new_kre;
}

void KlassRecordBucket::accumulate_klass_totals(ObjectAddressInfoTable *oait)
{
	KlassRecordEntry *kre = _kres;
	while (kre != NULL) {
    kre->add_to_klass_totals(oait);
    kre = kre->next();
	}
}

void KlassRecordBucket::print_on(outputStream *out)
{
	KlassRecordEntry *kre = _kres;
	while (kre != NULL) {
    kre->print_on(out);
    kre = kre->next();
	}
}

ObjectAddressInfoEntry::ObjectAddressInfoEntry(oop obj, int obj_size,
  KlassRecord *kr, obj_type type, ObjectAddressInfoEntry *next, bool old_rec) {
  _key    = obj;
  _value  = new ObjectAddressInfo(obj, obj_size, kr, type, old_rec);
  _next   = next;
}

ObjectAddressInfoEntry::~ObjectAddressInfoEntry()
{
  _next = NULL;
}

void ObjectAddressInfoEntry::add_to_heap_stats(ObjectAddressInfoTable *oait)
{
  oait->add_to_heap_stats(_value);
}

ObjectAddressInfoEntry* ObjectAddressInfoBucket::get_oaie(oop obj)
{
	ObjectAddressInfoEntry *oaie;

	oaie = _oaies;
	while (oaie != NULL) {
    if (oaie->key() == obj) {
      return oaie;
    }
    oaie = oaie->next();
	}
	return NULL;
}

//void ObjectAddressInfoEntry::reset_ref_cnts()
void ObjectAddressInfoEntry::reset_ref_cnts(enum init_marker im)
{
  _value->reset_ref_cnts(im);
}

void ObjectAddressInfoEntry::print_on(outputStream *out)
{
  intptr_t addr = _value->addr();
  int size      = (int)  (_value->size()*HeapWordSize);
  int type      = (int)  (_value->type());

  ObjectAddressInfoTable *oait = Universe::object_address_info_table();
  if (oait->is_perm(addr)) {
    return;
  }

  jlong ref_cnt  = 0l;
  if (((enum obj_type)type) == VM_OBJECT) {
    ref_cnt = -1l;
  } else {
    if (((enum obj_type)type) == FILLER_OBJECT) {
      ref_cnt = (jlong)_value->load_cnt()  +
                (jlong)_value->store_cnt();
    } else {
      ref_cnt = (jlong)_value->load_cnt()  +
                (jlong)_value->store_cnt() +
                (jlong)_value->init_cnt();
    }
  }

  if (ref_cnt > 0 || ((enum obj_type)type) == VM_OBJECT) {
    struct object_address_record oar;
    oar.addr      = addr;
    oar.size      = size;
    oar.ref_cnt   = ref_cnt;
    fwrite(&oar, sizeof(struct object_address_record), 1, addrtable_log);

    if (PrintTextOAT) {
      out->print_cr("  " INTPTR_FORMAT "    %-16d %-16ld (%2d)",
        _value->addr(), size, ref_cnt, type);
    }
  }
#if 0
  if (PrintTextOAT) {
    if (((size * HeapWordSize)>>10) > 512) {
    out->print_cr("  " INTPTR_FORMAT "    %-16ld %-16ld", _value->addr(),
      size, ref_cnt);
    }
  }
#endif
}

void ObjectAddressInfoBucket::copy_entries_to(ObjectAddressInfoTable *dst_oait) {
  ObjectAddressInfoEntry* oaie = _oaies;
  while (oaie != NULL) {
    intptr_t addr  = oaie->value()->addr();
    int size       = oaie->value()->size();
    obj_type type  = oaie->value()->type();
    klassOop klass = ProfileObjectFieldInfo ? 
                     oaie->value()->klass_record()->klass() : NULL;

    dst_oait->insert((oop)addr, size, klass, type, false);
    oaie = oaie->next();
  }
}

//void ObjectAddressInfoBucket::reset_ref_cnts() {
void ObjectAddressInfoBucket::reset_ref_cnts(enum init_marker im) {
  ObjectAddressInfoEntry* oaie = _oaies;
  while (oaie != NULL) {
    oaie->reset_ref_cnts(im);
    oaie = oaie->next();
  }
}

void ObjectAddressInfoBucket::empty_spaces(bool do_old_space,
  ObjectAddressInfoTable *oait) {
  ObjectAddressInfoEntry* cur  = _oaies;
  ObjectAddressInfoEntry* prev = NULL;

  while (cur != NULL) {
    if (!(oait->is_perm(cur->value()->addr())) && 
         (do_old_space || oait->is_young(cur->value()->addr())) ) {
      ObjectAddressInfoEntry* next = cur->next();

      if (prev == NULL) {
        guarantee(_oaies == cur, "invalid cur");
        _oaies = next;
      } else {
        prev->set_next(next);
      }

      delete cur;
      cur = next;
    } else {
      prev = cur;
      cur = cur->next();
    }
  }
}

void ObjectAddressInfoBucket::empty() {
  ObjectAddressInfoEntry* oaie = _oaies;
  _oaies = NULL;
  while (oaie != NULL) {
    ObjectAddressInfoEntry* next = oaie->next();
    delete oaie;
    oaie = next;
  }
}

ObjectAddressInfoEntry* ObjectAddressInfoBucket::insert(oop obj,
  int obj_size, KlassRecord *kr, obj_type otype, bool old_rec)
{
  ObjectAddressInfoEntry *new_oaie = NULL;
	ObjectAddressInfoEntry *oaie = get_oaie(obj);
  if (oaie) {
    /* MRJ -- this can happen during major GC or with the filler objects
     */
    guarantee(oaie->value()->init_cnt() <= 0, "invalid init_cnt");
    oaie->reset_ref_cnts((enum init_marker)oaie->value()->init_cnt());
    oaie->value()->set_size(obj_size);
    oaie->value()->set_klass_record(kr);
    new_oaie = oaie;
  } else {
    new_oaie = new ObjectAddressInfoEntry(obj, obj_size, kr, otype,
                                          _oaies, old_rec);
    _oaies = new_oaie;
  }
  return new_oaie;
}

void ObjectAddressInfoBucket::accumulate_heap_stats(ObjectAddressInfoTable *oait)
{
	ObjectAddressInfoEntry *oaie = _oaies;
	while (oaie != NULL) {
    oaie->add_to_heap_stats(oait);
    oaie = oaie->next();
	}
}

void ObjectAddressInfoBucket::print_on(outputStream *out)
{
	ObjectAddressInfoEntry *oaie = _oaies;
	while (oaie != NULL) {
    oaie->print_on(out);
    oaie = oaie->next();
	}
}

ObjectAddressInfoTable::ObjectAddressInfoTable(unsigned int obj_table_size,
  unsigned int klass_table_size)
{
	_size = 0;
  _using_to_space = false;

  _buckets = NEW_C_HEAP_ARRAY(ObjectAddressInfoBucket, obj_table_size);
  if (_buckets != NULL) {
    _size = obj_table_size;
    for (unsigned int index = 0; index < _size; index++) {
      _buckets[index].initialize();
    }
  }

	_kr_size = 0;
  _klass_buckets = NEW_C_HEAP_ARRAY(KlassRecordBucket, klass_table_size);
  if (_klass_buckets != NULL) {
    _kr_size = klass_table_size;
    for (unsigned int index = 0; index < _kr_size; index++) {
      _klass_buckets[index].initialize();
    }
  }

  //_known_free = new (ResourceObj::C_HEAP) GrowableArray<oop>(INITIAL_KNOWN_FREE_SIZE,true);
  use_from_space();
}

ObjectAddressInfoTable::~ObjectAddressInfoTable()
{
  if (_buckets != NULL) {
    for (unsigned int index = 0; index < _size; index++) {
      _buckets[index].empty();
    }
    FREE_C_HEAP_ARRAY(ObjectAddressInfoBucket, _buckets);
    _size = 0;
  }

  if (_klass_buckets != NULL) {
    for (unsigned int index = 0; index < _kr_size; index++) {
      _klass_buckets[index].empty();
    }
    FREE_C_HEAP_ARRAY(KlassRecordBucket, _klass_buckets);
    _kr_size = 0;
  }

  //delete _known_free;
}

#if 0
bool ObjectAddressInfoTable::is_known_free(oop obj)
{
  for (int i = 0; i < known_free()->length(); i++) {
    if (known_free()->at(i) == obj) {
      return true;
    }
  }
  return false;
}

void ObjectAddressInfoTable::mark_known_free(oop obj)
{
  known_free()->append(obj);
}
#endif

ObjectAddressInfo *ObjectAddressInfoTable::mark_alloc(oop obj)
{
  ObjectAddressInfo *oai = lookup(obj);
  if (!oai) {
    oai = insert(obj);
  }

  guarantee((intptr_t)obj == oai->addr(), "mark_alloc: addr does not match");

  if (ProfileObjectFieldInfo) {
    KlassRecord *kr = NULL;
    klassOop klass  = obj->klass();
    int obj_size    = obj->size();

    if (obj->size() != oai->size()) {
      if (oai->klass_record()->klass_type() != KT_VM_FILLER) {
        tty->print_cr("addr: %p obj_size: %d oai_type: %d oai_size: %d",
          oai->addr(), obj->size(), oai->type(), oai->size());
      }
      //guarantee(oai->klass_record()->klass_type() == KT_VM_FILLER,
      //          "over-writing a non-filler");
    }

    enum klass_type ktype;
    if (klass && klass->klass_part()->oop_is_instance()) {
      ktype = KT_APP_INSTANCE;
    } else if (klass && klass->klass_part()->oop_is_array()) {
      ktype = KT_APP_ARRAY;
    } else {
      ktype = KT_APP_OTHER;
    }

    if (klass != oai->klass_record()->klass()) {
      kr = kt_lookup(klass);

      if (!kr) {
        kr = kt_insert(klass, obj_size, ktype);
        guarantee(kr != NULL, "bad klass record insert");
      }

      oai->set_klass_record(kr);
    }

    if (oai->klass_record()->instance_size() != obj_size) {
      oai->klass_record()->set_instance_size(-2);
    }
    if (oai->klass_record()->klass_type() != ktype) {
      oai->klass_record()->set_klass_type(ktype);
    }
  }

  /* This can happen because of the filler objects -- we mark the filler
   * objects as we see them -- then some (usually smaller) object is used to
   * replace them. It seems that replacing a filler object with a smaller
   * application object should leave a smaller filler object in its place --
   * we might need to fix this.
   */
  if (obj->size() != oai->size()) {
    if (obj->size() < oai->size()) {
      intptr_t rem_filler = (intptr_t)obj + (obj->size()*HeapWordSize);
      int rem = (oai->size() - obj->size());
      ObjectAddressInfo *rem_filler_oai = insert((oop)rem_filler,rem,
                                                 FILLER_KLASS, FILLER_OBJECT);
      mark_filler((oop)rem_filler,rem);
    }

    oai->set_size(obj->size());
  }

  oai->set_type(APP_OBJECT);
  oai->set_init_cnt(obj->size());
  //if (oai->klass_record()->klass_type() == KT_VM_INSTANCE) {
  //  tty->print_cr("trouble!");
  //}

  return oai;
}

ObjectAddressInfo *ObjectAddressInfoTable::mark_filler(oop obj,
  int size)
{
  ObjectAddressInfo *oai = lookup(obj);
  if (!oai) {
    oai = insert(obj, size, FILLER_KLASS, FILLER_OBJECT);
  }

  guarantee((intptr_t)obj == oai->addr(), "mark_filler: addr does not match");
  guarantee(size          == oai->size(), "mark_filler: size does not match");
  if (ProfileObjectFieldInfo) {
#if 0
    if (FILLER_KLASS != oai->klass_record()->klass()) {
      tty->print_cr("oai_klass: %p", oai->klass_record()->klass());
    }
#endif
    guarantee((FILLER_KLASS == oai->klass_record()->klass()),
      "mark_filler: klass does not match");
    oai->klass_record()->set_klass_type(KT_VM_FILLER);
  }

  guarantee((oai->load_cnt()  == 0) &&
            (oai->store_cnt() == 0) &&
            ((oai->init_cnt()  == NEW_MARKER) || (oai->init_cnt() == 0) ),
            "invalid ref counts for filler object");

  oai->set_type(FILLER_OBJECT);
  //oai->reset_ref_cnts();

  return oai;
}

void ObjectAddressInfoTable::mark_load(oop obj)
{
  ObjectAddressInfo *oai = lookup(obj);

  if (!oai) {
    oai = insert(obj);
  }
  if (oai->type() != APP_OBJECT) {
    oai->set_type(APP_OBJECT);
  }

  oai->inc_load_cnt();
}

void ObjectAddressInfoTable::mark_store(oop obj)
{
  ObjectAddressInfo *oai = lookup(obj);

  if (!oai) {
    oai = insert(obj);
  }
  if (oai->type() != APP_OBJECT) {
    oai->set_type(APP_OBJECT);
  }
  
  oai->inc_store_cnt();
}

void ObjectAddressInfoTable::mark_load(oop obj, intptr_t field_addr, int size)
{
  static int i = 0;
  ObjectAddressInfo *oai = lookup(obj);

  if (oai && oai->type() == APP_OBJECT) {
    oai->inc_load_cnt();
    if (ProfileObjectFieldInfo) {
      if (oai->klass_record()->instance_size() > 0) {
        int offset = ((int)(field_addr - oai->addr()) - OOP_HEADER_SIZE);
        oai->klass_record()->mark_field_load(offset, size);
      }
    }
  }

  /* MRJ -- the old way (below) marked loads for VM objects */
#if 0
  if (!oai) {
    oai = insert(obj);
  }
  if (oai->type() != APP_OBJECT) {
    oai->set_type(APP_OBJECT);
  }

  oai->inc_load_cnt();
  if (ProfileObjectFieldInfo) {
    if (oai->klass_record()->instance_size() > 0) {
      int offset = ((int)(field_addr - oai->addr()) - OOP_HEADER_SIZE);
      oai->klass_record()->mark_field_load(offset, size);
    }
  }
#endif
}

void ObjectAddressInfoTable::mark_store(oop obj, intptr_t field_addr, int size)
{
  static int i = 0;
  ObjectAddressInfo *oai = lookup(obj);

  if (oai && oai->type() == APP_OBJECT) {
    oai->inc_store_cnt();
    if (ProfileObjectFieldInfo) {
      if (oai->klass_record()->instance_size() > 0) {
        int offset = ((int)(field_addr - oai->addr()) - OOP_HEADER_SIZE);
        oai->klass_record()->mark_field_store(offset, size);
      }
    }
  }

  /* MRJ -- the old way (below) marked stores for VM objects */
#if 0
  if (!oai) {
    oai = insert(obj);
  }
  if (oai->type() != APP_OBJECT) {
    oai->set_type(APP_OBJECT);
  }
 
  oai->inc_store_cnt();

  if (ProfileObjectFieldInfo) {
    if (oai->klass_record()->instance_size() > 0) {
      int offset = ((int)(field_addr - oai->addr()) - OOP_HEADER_SIZE);
      oai->klass_record()->mark_field_store(offset, size);
    }
  }
#endif
}

void ObjectAddressInfoTable::batch_mark_load(oop obj, int length)
{
  ObjectAddressInfo *oai = lookup(obj);

  if (oai && oai->type() == APP_OBJECT) {
    oai->add_load_cnt(length);
  }
  /* MRJ -- the old way (below) marked loads for VM objects */
#if 0
  if (!oai) {
    oai = insert(obj);
  }

  if (oai->type() != APP_OBJECT) {
    oai->set_type(APP_OBJECT);
  }
#endif
}

void ObjectAddressInfoTable::batch_mark_store(oop obj, int length)
{
  ObjectAddressInfo *oai = lookup(obj);

  if (oai && oai->type() == APP_OBJECT) {
    oai->add_store_cnt(length);
  }
  /* MRJ -- the old way (below) marked stores for VM objects */
#if 0
  if (!oai) {
    oai = insert(obj);
  }
  if (oai->type() != APP_OBJECT) {
    oai->set_type(APP_OBJECT);
  }

  oai->add_store_cnt(length);
#endif
}

ObjectAddressInfo *ObjectAddressInfoTable::insert(oop obj,
  int obj_size, klassOop klass, obj_type type, bool old_rec)
{
  ResourceMark rm;
	unsigned int index;
	ObjectAddressInfoEntry *oaie;
  //static int _cnt = 0;

  assert(_buckets != NULL, "ObjectAddressInfoTable buckets is null");
	index = hash(obj) % _size;

  ObjectAddressInfoTable_lock->lock_without_safepoint_check();

  KlassRecord *kr = NULL;
  if (ProfileObjectFieldInfo) {
    kr = kt_lookup(klass);

    if (!kr) {
      kr = kt_insert(klass, obj_size, KT_UNSPECIFIED);
      guarantee(kr != NULL, "bad klass record insert");
    }

    if (kr->is_instance() && kr->instance_size() != obj_size) {
      kr->set_instance_size(-2);
    }
  }

  oaie = _buckets[index].insert(obj, obj_size, kr, type, old_rec);
  guarantee ((oaie->key() == obj), "bad ObjectAddressInfoTable insert");

  //_cnt++;
  ObjectAddressInfoTable_lock->unlock();

	return oaie->value();
}

ObjectAddressInfo *ObjectAddressInfoTable::insert(oop obj)
{
	return insert(obj, obj->size(), obj->klass(), VM_OBJECT);
}

ObjectAddressInfo *ObjectAddressInfoTable::lookup(oop obj)
{
	unsigned int index;
	ObjectAddressInfoEntry *oaie;

	index = hash(obj) % _size;
  oaie = _buckets[index].get_oaie(obj);
  
  if (oaie) {
    return oaie->value();
  }

  return NULL;
}

KlassRecord *ObjectAddressInfoTable::kt_insert(klassOop k, int instance_size,
  enum klass_type ktype)
{
  ResourceMark rm;
	unsigned int index;
	KlassRecordEntry *kre;

  assert(_klass_buckets != NULL, "ObjectAddressInfoTable::_klass_buckets is null");
	index = hash((oop)k) % _kr_size;

  kre = _klass_buckets[index].kt_insert(k, instance_size, ktype);
  guarantee ((kre->value()->klass() == k),
    "bad KlassRecord initialize");

  guarantee ((kre->key() == k), "bad KlassRecord initialize");
	return kre->value();
}

KlassRecord *ObjectAddressInfoTable::kt_lookup(klassOop k)
{
	unsigned int index;
	KlassRecordEntry *kre;

	index = hash((oop)k) % _kr_size;
  kre = _klass_buckets[index].get_kre(k);
  
  if (kre) {
    return kre->value();
  }

  return NULL;
}

unsigned long ObjectAddressInfoTable::hash(oop obj)
{
  uintptr_t ad = (uintptr_t)obj;
  return (size_t)(ad ^ (ad >> 16));
}

void ObjectAddressInfoTable::print_klass_header(outputStream *out,
    const char *reason)
{
  out->print_cr("KlassRecords : (%s)\n", reason);
  out->print_cr("klasses      : " INT64_FORMAT_W(13), klass_totals[KST_NR_KLASSES]);
  out->print_cr("live_objects : " INT64_FORMAT_W(13) "  "
                "live_bytes   : " INT64_FORMAT_W(13) "  "
                "live_refs    : " INT64_FORMAT_W(13),
                klass_totals[KST_LIVE_OBJECTS],
                klass_totals[KST_LIVE_SIZE] * HeapWordSize,
                klass_totals[KST_LIVE_REFS]);
  out->print_cr("hot_size     : " INT64_FORMAT_W(13) "  "
                "hot_bytes    : " INT64_FORMAT_W(13) "  "
                "hot_refs     : " INT64_FORMAT_W(13),
                klass_totals[KST_HOT_OBJECTS],
                klass_totals[KST_HOT_SIZE] * HeapWordSize,
                klass_totals[KST_HOT_REFS]);
  out->print_cr("new_size     : " INT64_FORMAT_W(13) "  "
                "new_bytes    : " INT64_FORMAT_W(13) "  "
                "new_refs     : " INT64_FORMAT_W(13),
                klass_totals[KST_NEW_OBJECTS],
                klass_totals[KST_NEW_SIZE] * HeapWordSize,
                klass_totals[KST_NEW_REFS]);
#if 0
  out->print_cr("inst_objects : " INT64_FORMAT_W(13) "  "
                "inst_bytes   : " INT64_FORMAT_W(13),
                klass_totals[KST_INSTANCE_OBJECTS],
                klass_totals[KST_INSTANCE_SIZE] * HeapWordSize);
  out->print_cr("arr_objects  : " INT64_FORMAT_W(13) "  "
                "arr_bytes    : " INT64_FORMAT_W(13),
                klass_totals[KST_ARRAY_OBJECTS],
                klass_totals[KST_ARRAY_SIZE] * HeapWordSize);
  out->print_cr("hot_ins_objs : " INT64_FORMAT_W(13) "  "
                "hot_ins_byts : " INT64_FORMAT_W(13),
                klass_totals[KST_HOT_INSTANCE_OBJECTS],
                klass_totals[KST_HOT_INSTANCE_SIZE] * HeapWordSize);
  out->print_cr("hot_arr_objs : " INT64_FORMAT_W(13) "  "
                "hot_arr_byts : " INT64_FORMAT_W(13),
                klass_totals[KST_HOT_ARRAY_OBJECTS],
                klass_totals[KST_HOT_ARRAY_SIZE] * HeapWordSize);
#endif
  out->print_cr("");
}

void ObjectAddressInfoTable::print_klass_table(outputStream *out)
{
  _klass_print_order = 0;
  for(unsigned int i=0; i < _kr_size; i++) {
    _klass_buckets[i].print_on(out);
  }
  out->print_cr("");
}

void ObjectAddressInfoTable::print_heap_stats(outputStream *out, const char *name,
  enum heap_space hs)
{
  char buf[10];
  sprintf(buf, "%s:", name);
  out->print("  %-9s span: " INT64_FORMAT_W(13) " bytes ( "
                             INTPTR_FORMAT "  "
                             INTPTR_FORMAT " )\n", buf,
                             heap_stats[hs][USED_BYTES],
                             heap_boundaries[hs][BOTTOM_ADDR],
                             heap_boundaries[hs][TOP_ADDR]);
  out->print("           alive: " INT64_FORMAT_W(13) " objects, "
                                  INT64_FORMAT_W(13) " bytes, "
                                  INT64_FORMAT_W(13) " refs\n",
                                  heap_stats[hs][LIVE_OBJECTS],
                                  heap_stats[hs][LIVE_SIZE],
                                  heap_stats[hs][LIVE_REFS]);
  out->print("             hot: " INT64_FORMAT_W(13) " objects, "
                                  INT64_FORMAT_W(13) " bytes, "
                                  INT64_FORMAT_W(13) " refs\n",
                                  heap_stats[hs][HOT_OBJECTS],
                                  heap_stats[hs][HOT_SIZE],
                                  heap_stats[hs][HOT_REFS]);
  out->print("             new: " INT64_FORMAT_W(13) " objects, "
                                  INT64_FORMAT_W(13) " bytes, "
                                  INT64_FORMAT_W(13) " refs\n",
                                  heap_stats[hs][NEW_OBJECTS],
                                  heap_stats[hs][NEW_SIZE],
                                  heap_stats[hs][NEW_REFS]);
  out->print("              vm: " INT64_FORMAT_W(13) " objects, "
                                  INT64_FORMAT_W(13) " bytes, "
                                  INT64_FORMAT_W(13) " refs\n",
                                  heap_stats[hs][VM_OBJECTS],
                                  heap_stats[hs][VM_SIZE],
                                  heap_stats[hs][VM_REFS]);
  out->print("          filler: " INT64_FORMAT_W(13) " objects, "
                                  INT64_FORMAT_W(13) " bytes, "
                                  INT64_FORMAT_W(13) " refs\n",
                                  heap_stats[hs][FILLER_OBJECTS],
                                  heap_stats[hs][FILLER_SIZE],
                                  heap_stats[hs][FILLER_REFS]);
  out->print("             app: " INT64_FORMAT_W(13) " objects, "
                                  INT64_FORMAT_W(13) " bytes, "
                                  INT64_FORMAT_W(13) " refs\n",
                                  heap_stats[hs][APP_OBJECTS],
                                  heap_stats[hs][APP_SIZE],
                                  heap_stats[hs][APP_REFS]);
  out->print_cr("");
}

void ObjectAddressInfoTable::print_header(outputStream *out, const char *reason)
{
  out->print("ObjectAddressInfoTable: (%s)\n", reason);
  print_heap_stats(out, "eden",     HS_EDEN_SPACE);
  print_heap_stats(out, "survivor", HS_SURVIVOR_SPACE);
  print_heap_stats(out, "tenured",  HS_TENURED_SPACE);
  print_heap_stats(out, "perm",     HS_PERM_SPACE);

  out->print_cr("");
}

void ObjectAddressInfoTable::print_table(outputStream *out)
{
  if (PrintTextOAT) {
    out->print_cr("  %-21s %-16s %-16s",
      "Address", "Size", "Refs");
    out->flush();
  }

  /* Mark the end of the table with a marker rec */
  struct object_address_record marker_rec;
  marker_rec.addr     = (intptr_t) OAR_MARKER;
  marker_rec.size     = (int)  _cur_val;
  marker_rec.ref_cnt  = (jint) OAR_MARKER;

  fwrite(&marker_rec, sizeof(struct object_address_record), 1, addrtable_log);

  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].print_on(out);
  }

  out->print_cr("");
#if 0
  if (addrtable_log == NULL) {
    out->print_cr("  %-21s %-16s %-16s",
      "Address", "Size", "Refs");
    out->flush();
  }

  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].print_on(out);
  }

  if (addrtable_log != NULL) {
    /* Mark the end of the table with a marker rec */
    struct object_address_record marker_rec;
    marker_rec.addr      = (intptr_t) OAR_MARKER;
    marker_rec.size      = (int)  OAR_MARKER;
    marker_rec.load_cnt  = (jint) OAR_MARKER;
    marker_rec.store_cnt = (jint) OAR_MARKER;

    fwrite(&marker_rec, sizeof(struct object_address_record), 1, addrtable_log);
  } else {
    out->print_cr("");
  }
#endif
}

void ObjectAddressInfoTable::print_on(outputStream *obj_out,
  outputStream* klass_out, const char *reason)
{
  print_header(obj_out, reason);
  print_table(obj_out);

  if (ProfileObjectFieldInfo) {
    print_klass_header(klass_out, reason);
    print_klass_table(klass_out);
  }
}

void ObjectAddressInfoTable::reset_heap_stats()
{
  unsigned int i, j;
  for(i = 0; i < HS_NR_SPACES; i++) {
    for(j = 0; j < NR_HEAP_STATS; j++) {
      heap_stats[i][j] = 0;
    }
  }
  for (i = 0; i < NR_KS_TOTALS; i++) {
    klass_totals[i] = 0;
  }
}

enum heap_space ObjectAddressInfoTable::get_space(intptr_t addr)
{
  if (addr >= heap_boundaries[HS_EDEN_SPACE][BOTTOM_ADDR]             &&
      addr <  heap_boundaries[HS_EDEN_SPACE][END_ADDR]) {
    return HS_EDEN_SPACE;
  } else if (addr >= heap_boundaries[HS_SURVIVOR_SPACE][BOTTOM_ADDR]  &&
             addr <  heap_boundaries[HS_SURVIVOR_SPACE][END_ADDR]) {
    return HS_SURVIVOR_SPACE;
  } else if (addr >= heap_boundaries[HS_TENURED_SPACE][BOTTOM_ADDR]   &&
             addr <  heap_boundaries[HS_TENURED_SPACE][END_ADDR]) {
    return HS_TENURED_SPACE;
  } else if (addr >= heap_boundaries[HS_PERM_SPACE][BOTTOM_ADDR]      &&
             addr <  heap_boundaries[HS_PERM_SPACE][END_ADDR]) {
    return HS_PERM_SPACE;
  }

  return HS_INVALID_SPACE;
}

void ObjectAddressInfoTable::add_to_heap_stats(ObjectAddressInfo *oai)
{
  enum heap_space hs = (enum heap_space)get_space(oai->addr());
  if (hs == HS_INVALID_SPACE) {
    ObjectAddressInfoTable *oait = Universe::object_address_info_table();
    if (this == oait || oait == NULL) {
      tty->print_cr("invalid space: addr: %p oait: orig using_to_space? %d, ",
                    oai->addr(), _using_to_space);
    } else {
      tty->print_cr("invalid space: addr: %p oait: alt  using_to_space? %d, ",
                    oai->addr(), _using_to_space);
    }
#if 1
    for (int s = 0; s < HS_NR_SPACES; s++) {
      tty->print("  space: %d, bottom: %p, top: %p, end: %p\n", s,
                 heap_boundaries[s][BOTTOM_ADDR],
                 heap_boundaries[s][TOP_ADDR],
                 heap_boundaries[s][END_ADDR]);
    }
#endif
#if 0
    tty->print("oai with invalid space:  " INTPTR_FORMAT
               " " INT64_FORMAT_W(13) " bytes"
               " " INT64_FORMAT_W(13) " refs\n", oai->addr(),
               (oai->size()*HeapWordSize),
               (oai->load_cnt() + oai->store_cnt()));
#endif
    return;
  }

  jint refs = (oai->init_cnt() < 0) ? 
              (oai->load_cnt() + oai->store_cnt()) :
              (oai->load_cnt() + oai->store_cnt() + oai->init_cnt());
#if 0
  if (refs < 0) {
    tty->print_cr("refs: %d load: %d store: %d init: %d", refs,
                  oai->load_cnt(), oai->store_cnt(), oai->init_cnt());
  }
#endif
  guarantee(refs >= 0, "negative refs!");

  heap_stats[hs][LIVE_OBJECTS]      += 1;
  heap_stats[hs][LIVE_SIZE]         += (oai->size() * HeapWordSize);
  heap_stats[hs][LIVE_REFS]         += refs;

  if (refs > 0 || (oai->type() == VM_OBJECT)) {
    heap_stats[hs][HOT_OBJECTS]     += 1;
    heap_stats[hs][HOT_SIZE]        += (oai->size() * HeapWordSize);
    heap_stats[hs][HOT_REFS]        += refs;
  }

  if ((oai->init_cnt() > 0) || (oai->init_cnt() == NEW_MARKER)) {
    heap_stats[hs][NEW_OBJECTS] += 1;
    heap_stats[hs][NEW_SIZE]    += (oai->size() * HeapWordSize);
    heap_stats[hs][NEW_REFS]    += (oai->init_cnt() == NEW_MARKER) ? 0 :
                                    oai->init_cnt();
  }

  if (oai->type() == VM_OBJECT) {
    heap_stats[hs][VM_OBJECTS]      += 1;
    heap_stats[hs][VM_SIZE]         += (oai->size() * HeapWordSize);
    heap_stats[hs][VM_REFS]         += refs;
  } else if (oai->type() == FILLER_OBJECT) {
    heap_stats[hs][FILLER_OBJECTS]  += 1;
    heap_stats[hs][FILLER_SIZE]     += (oai->size() * HeapWordSize);
    heap_stats[hs][FILLER_REFS]     += refs;
  } else if (oai->type() == APP_OBJECT) {
    heap_stats[hs][APP_OBJECTS]     += 1;
    heap_stats[hs][APP_SIZE]        += (oai->size() * HeapWordSize);
    heap_stats[hs][APP_REFS]        += refs;
  }
 
  if (ProfileObjectFieldInfo) {
    KlassRecord *kr = oai->klass_record();
    if (kr) {
      kr->add_to_klass_stats(oai, hs);
    } else {
      tty->print_cr("oai without kr!: %p size: %d refs: %d",
        oai->addr(), oai->size() * HeapWordSize, refs);
    }
  }

  ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());
  heap_stats[HS_EDEN_SPACE][USED_BYTES]     = psh->young_gen()->eden_space()->used_in_bytes();
  heap_stats[HS_SURVIVOR_SPACE][USED_BYTES] = psh->young_gen()->from_space()->used_in_bytes();
  heap_stats[HS_TENURED_SPACE][USED_BYTES]  = psh->old_gen()->object_space()->used_in_bytes();
  heap_stats[HS_PERM_SPACE][USED_BYTES]     = psh->perm_gen()->object_space()->used_in_bytes();
}

void ObjectAddressInfoTable::add_to_klass_totals(KlassRecord *kr)
{
  klass_totals[KST_NR_KLASSES]   += 1;
  klass_totals[KST_LIVE_OBJECTS] += kr->live_objects();
  klass_totals[KST_LIVE_SIZE]    += kr->live_size();
  klass_totals[KST_LIVE_REFS]    += kr->live_refs();
  klass_totals[KST_HOT_OBJECTS]  += kr->hot_objects();
  klass_totals[KST_HOT_SIZE]     += kr->hot_size();
  klass_totals[KST_HOT_REFS]     += kr->hot_refs();
  klass_totals[KST_NEW_OBJECTS]  += kr->new_objects();
  klass_totals[KST_NEW_SIZE]     += kr->new_size();
  klass_totals[KST_NEW_REFS]     += kr->new_refs();

#if 0
  if (kr->is_instance()) {
    klass_totals[KST_INSTANCE_OBJECTS]     += kr->live_objects();
    klass_totals[KST_INSTANCE_SIZE]        += kr->live_size();
    klass_totals[KST_HOT_INSTANCE_OBJECTS] += kr->hot_objects();
    klass_totals[KST_HOT_INSTANCE_SIZE]    += kr->hot_size();
  } else if (kr->is_array()) {
    klass_totals[KST_ARRAY_OBJECTS]        += kr->live_objects();
    klass_totals[KST_ARRAY_SIZE]           += kr->live_size();
    klass_totals[KST_HOT_ARRAY_OBJECTS]    += kr->hot_objects();
    klass_totals[KST_HOT_ARRAY_SIZE]       += kr->hot_size();
  }
#endif
}

void ObjectAddressInfoTable::compute_heap_stats()
{
  ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());

  reset_heap_stats();
  reset_klass_stats();

  for(unsigned int i=0; i < _size; i++) {
    _buckets[i].accumulate_heap_stats(this);
  }

  for(unsigned int i=0; i < _kr_size; i++) {
    _klass_buckets[i].accumulate_klass_totals(this);
  }
  tty->flush();
}

void ObjectAddressInfoTable::record_heap_boundaries()
{
  ParallelScavengeHeap* psh = ((ParallelScavengeHeap*)Universe::heap());

  heap_boundaries[HS_EDEN_SPACE][BOTTOM_ADDR] = (intptr_t)psh->young_gen()->eden_space()->bottom();
  heap_boundaries[HS_EDEN_SPACE][TOP_ADDR]    = (intptr_t)psh->young_gen()->eden_space()->top();
  heap_boundaries[HS_EDEN_SPACE][END_ADDR]    = (intptr_t)psh->young_gen()->eden_space()->end();

  if (_using_to_space) {
    heap_boundaries[HS_SURVIVOR_SPACE][BOTTOM_ADDR] = (intptr_t)psh->young_gen()->to_space()->bottom();
    heap_boundaries[HS_SURVIVOR_SPACE][TOP_ADDR]    = (intptr_t)psh->young_gen()->to_space()->top();
    heap_boundaries[HS_SURVIVOR_SPACE][END_ADDR]    = (intptr_t)psh->young_gen()->to_space()->end();
  } else {
    heap_boundaries[HS_SURVIVOR_SPACE][BOTTOM_ADDR] = (intptr_t)psh->young_gen()->from_space()->bottom();
    heap_boundaries[HS_SURVIVOR_SPACE][TOP_ADDR]    = (intptr_t)psh->young_gen()->from_space()->top();
    heap_boundaries[HS_SURVIVOR_SPACE][END_ADDR]    = (intptr_t)psh->young_gen()->from_space()->end();
  }

  heap_boundaries[HS_TENURED_SPACE][BOTTOM_ADDR]  = (intptr_t)psh->old_gen()->object_space()->bottom();
  heap_boundaries[HS_TENURED_SPACE][TOP_ADDR]     = (intptr_t)psh->old_gen()->object_space()->top();
  heap_boundaries[HS_TENURED_SPACE][END_ADDR]     = (intptr_t)psh->old_gen()->object_space()->end();

  heap_boundaries[HS_PERM_SPACE][BOTTOM_ADDR]     = (intptr_t)psh->perm_gen()->object_space()->bottom();
  heap_boundaries[HS_PERM_SPACE][TOP_ADDR]        = (intptr_t)psh->perm_gen()->object_space()->top();
  heap_boundaries[HS_PERM_SPACE][END_ADDR]        = (intptr_t)psh->perm_gen()->object_space()->end();

#if 0
  ObjectAddressInfoTable *oait = Universe::object_address_info_table();
  if (this == oait || oait == NULL) {
    tty->print_cr("recorded heap bounds for orig oait: using_to_space? %d", _using_to_space);
  } else {
    tty->print_cr("recorded heap bounds for  alt_oait: using_to_space? %d", _using_to_space);
  }
  for (int s = 0; s < HS_NR_SPACES; s++) {
    tty->print("  space: %d, bottom: %p, top: %p, end: %p\n", s,
               heap_boundaries[s][BOTTOM_ADDR],
               heap_boundaries[s][TOP_ADDR],
               heap_boundaries[s][END_ADDR]);
  }
#endif
}

//void ObjectAddressInfoTable::reset_ref_cnts()
void ObjectAddressInfoTable::reset_ref_cnts(enum init_marker im)
{
  if (_buckets != NULL) {
    for (unsigned int index = 0; index < _size; index++) {
      _buckets[index].reset_ref_cnts(im);
    }
  }

  if (_klass_buckets != NULL) {
    for (unsigned int index = 0; index < _kr_size; index++) {
      _klass_buckets[index].reset_ref_cnts();
    }
  }
}

void ObjectAddressInfoTable::reset_klass_stats()
{
  if (_klass_buckets != NULL) {
    for (unsigned int index = 0; index < _kr_size; index++) {
      _klass_buckets[index].reset_klass_stats();
    }
  }
}

void ObjectAddressInfoTable::reset_spaces(bool do_old_space)
{
  reset_heap_stats();
  reset_klass_stats();

  if (_klass_buckets != NULL) {
    for (unsigned int index = 0; index < _kr_size; index++) {
      _klass_buckets[index].empty();
    }
  }

  if (_buckets != NULL) {
    for (unsigned int index = 0; index < _size; index++) {
      _buckets[index].empty_spaces(do_old_space, this);
    }
  }
}

void ObjectAddressInfoTable::copy_to(ObjectAddressInfoTable *dst_oait)
{
  if (_using_to_space) {
    dst_oait->use_to_space();
  } else {
    dst_oait->use_from_space();
  }

  dst_oait->reset_spaces(true);
  if (_klass_buckets != NULL) {
    for (unsigned int index = 0; index < _kr_size; index++) {
      _klass_buckets[index].copy_entries_to(dst_oait);
    }
  }

  if (_buckets != NULL) {
    for (unsigned int index = 0; index < _size; index++) {
      _buckets[index].copy_entries_to(dst_oait);
    }
  }
}
#endif /* PROFILE_OBJECT_ADDRESS_INFO */
