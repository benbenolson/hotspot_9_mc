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

#ifndef SHARE_VM_MEMORY_HEAPINSPECTION_HPP
#define SHARE_VM_MEMORY_HEAPINSPECTION_HPP

#include "memory/allocation.hpp"
#include "oops/objArrayOop.hpp"
#include "oops/oop.hpp"
#include "oops/annotations.hpp"
#include "utilities/macros.hpp"

// ObjectInfoCollection inherits PeriodicTask 
#include "runtime/task.hpp"

#if INCLUDE_SERVICES

class RegularScavengeTask : public PeriodicTask {
  public:
     RegularScavengeTask(int interval_time) : PeriodicTask(interval_time) {}
     void task();
     static void engage();
     static void disengage();
};

class RegularScavenge : public AllStatic
{
  friend class RegularScavengeTask;
private:
  static RegularScavengeTask* _task;
  static bool _collecting;
public: 
  static bool collecting()           { return _collecting; }
  static void set_collecting(bool c) { _collecting = c;    }

  static void engage();
  static void disengage();

  static RegularScavengeTask* task() { return _task; }
  static bool is_active() { return _task != NULL; }

  static void initialize();
  static void destroy();
};


// HeapInspection

// KlassInfoTable is a bucket hash table that
// maps Klass*s to extra information:
//    instance count and instance word size.
//
// A KlassInfoBucket is the head of a link list
// of KlassInfoEntry's
//
// KlassInfoHisto is a growable array of pointers
// to KlassInfoEntry's and is used to sort
// the entries.

#define HEAP_INSPECTION_COLUMNS_DO(f) \
    f(inst_size, InstSize, \
        "Size of each object instance of the Java class") \
    f(inst_count, InstCount, \
        "Number of object instances of the Java class")  \
    f(inst_bytes, InstBytes, \
        "This is usually (InstSize * InstNum). The only exception is " \
        "java.lang.Class, whose InstBytes also includes the slots " \
        "used to store static fields. InstBytes is not counted in " \
        "ROAll, RWAll or Total") \
    f(mirror_bytes, Mirror, \
        "Size of the Klass::java_mirror() object") \
    f(klass_bytes, KlassBytes, \
        "Size of the InstanceKlass or ArrayKlass for this class. " \
        "Note that this includes VTab, ITab, OopMap") \
    f(secondary_supers_bytes, K_secondary_supers, \
        "Number of bytes used by the Klass::secondary_supers() array") \
    f(vtab_bytes, VTab, \
        "Size of the embedded vtable in InstanceKlass") \
    f(itab_bytes, ITab, \
        "Size of the embedded itable in InstanceKlass") \
    f(nonstatic_oopmap_bytes, OopMap, \
        "Size of the embedded nonstatic_oop_map in InstanceKlass") \
    f(methods_array_bytes, IK_methods, \
        "Number of bytes used by the InstanceKlass::methods() array") \
    f(method_ordering_bytes, IK_method_ordering, \
        "Number of bytes used by the InstanceKlass::method_ordering() array") \
    f(default_methods_array_bytes, IK_default_methods, \
        "Number of bytes used by the InstanceKlass::default_methods() array") \
    f(default_vtable_indices_bytes, IK_default_vtable_indices, \
        "Number of bytes used by the InstanceKlass::default_vtable_indices() array") \
    f(local_interfaces_bytes, IK_local_interfaces, \
        "Number of bytes used by the InstanceKlass::local_interfaces() array") \
    f(transitive_interfaces_bytes, IK_transitive_interfaces, \
        "Number of bytes used by the InstanceKlass::transitive_interfaces() array") \
    f(fields_bytes, IK_fields, \
        "Number of bytes used by the InstanceKlass::fields() array") \
    f(inner_classes_bytes, IK_inner_classes, \
        "Number of bytes used by the InstanceKlass::inner_classes() array") \
    f(signers_bytes, IK_signers, \
        "Number of bytes used by the InstanceKlass::singers() array") \
    f(class_annotations_bytes, class_annotations, \
        "Size of class annotations") \
    f(class_type_annotations_bytes, class_type_annotations, \
        "Size of class type annotations") \
    f(fields_annotations_bytes, fields_annotations, \
        "Size of field annotations") \
    f(fields_type_annotations_bytes, fields_type_annotations, \
        "Size of field type annotations") \
    f(methods_annotations_bytes, methods_annotations, \
        "Size of method annotations") \
    f(methods_parameter_annotations_bytes, methods_parameter_annotations, \
        "Size of method parameter annotations") \
    f(methods_type_annotations_bytes, methods_type_annotations, \
        "Size of methods type annotations") \
    f(methods_default_annotations_bytes, methods_default_annotations, \
        "Size of methods default annotations") \
    f(annotations_bytes, annotations, \
        "Size of all annotations") \
    f(cp_bytes, Cp, \
        "Size of InstanceKlass::constants()") \
    f(cp_tags_bytes, CpTags, \
        "Size of InstanceKlass::constants()->tags()") \
    f(cp_cache_bytes, CpCache, \
        "Size of InstanceKlass::constants()->cache()") \
    f(cp_operands_bytes, CpOperands, \
        "Size of InstanceKlass::constants()->operands()") \
    f(cp_refmap_bytes, CpRefMap, \
        "Size of InstanceKlass::constants()->reference_map()") \
    f(cp_all_bytes, CpAll, \
        "Sum of Cp + CpTags + CpCache + CpOperands + CpRefMap") \
    f(method_count, MethodCount, \
        "Number of methods in this class") \
    f(method_bytes, MethodBytes, \
        "Size of the Method object") \
    f(const_method_bytes, ConstMethod, \
        "Size of the ConstMethod object") \
    f(method_data_bytes, MethodData, \
        "Size of the MethodData object") \
    f(stackmap_bytes, StackMap, \
        "Size of the stackmap_data") \
    f(bytecode_bytes, Bytecodes, \
        "Of the MethodBytes column, how much are the space taken up by bytecodes") \
    f(method_all_bytes, MethodAll, \
        "Sum of MethodBytes + Constmethod + Stackmap + Methoddata") \
    f(ro_bytes, ROAll, \
        "Size of all class meta data that could (potentially) be placed " \
        "in read-only memory. (This could change with CDS design)") \
    f(rw_bytes, RWAll, \
        "Size of all class meta data that must be placed in read/write " \
        "memory. (This could change with CDS design) ") \
    f(total_bytes, Total, \
        "ROAll + RWAll. Note that this does NOT include InstBytes.")

// Size statistics for a Klass - filled in by Klass::collect_statistics()
class KlassSizeStats {
public:
#define COUNT_KLASS_SIZE_STATS_FIELD(field, name, help)   _index_ ## field,
#define DECLARE_KLASS_SIZE_STATS_FIELD(field, name, help) julong _ ## field;

  enum {
    HEAP_INSPECTION_COLUMNS_DO(COUNT_KLASS_SIZE_STATS_FIELD)
    _num_columns
  };

  HEAP_INSPECTION_COLUMNS_DO(DECLARE_KLASS_SIZE_STATS_FIELD)

  static int count(oop x) {
    return (HeapWordSize * (((x) != NULL) ? (x)->size() : 0));
  }

  static int count_array(objArrayOop x) {
    return (HeapWordSize * (((x) != NULL) ? (x)->size() : 0));
  }

  template <class T> static int count(T* x) {
    return (HeapWordSize * ((x) ? (x)->size() : 0));
  }

  template <class T> static int count_array(T* x) {
    if (x == NULL) {
      return 0;
    }
    if (x->length() == 0) {
      // This is a shared array, e.g., Universe::the_empty_int_array(). Don't
      // count it to avoid double-counting.
      return 0;
    }
    return HeapWordSize * x->size();
  }
};




class KlassInfoEntry: public CHeapObj<mtInternal> {
 private:
  KlassInfoEntry* _next;
  Klass*          _klass;
  long            _instance_count;
  size_t          _instance_words;
  long            _index;
  bool            _do_print; // True if we should print this class when printing the class hierarchy.
  GrowableArray<KlassInfoEntry*>* _subclasses;

 public:
  KlassInfoEntry(Klass* k, KlassInfoEntry* next) :
    _klass(k), _instance_count(0), _instance_words(0), _next(next), _index(-1),
    _do_print(false), _subclasses(NULL)
  {}
  ~KlassInfoEntry();
  KlassInfoEntry* next() const   { return _next; }
  bool is_equal(const Klass* k)  { return k == _klass; }
  Klass* klass()  const      { return _klass; }
  long count()    const      { return _instance_count; }
  void set_count(long ct)    { _instance_count = ct; }
  size_t words()  const      { return _instance_words; }
  void set_words(size_t wds) { _instance_words = wds; }
  void set_index(long index) { _index = index; }
  long index()    const      { return _index; }
  GrowableArray<KlassInfoEntry*>* subclasses() const { return _subclasses; }
  void add_subclass(KlassInfoEntry* cie);
  void set_do_print(bool do_print) { _do_print = do_print; }
  bool do_print() const      { return _do_print; }
  int compare(KlassInfoEntry* e1, KlassInfoEntry* e2);
  void print_on(outputStream* st) const;
  const char* name() const;
};

class KlassInfoClosure : public StackObj {
 public:
  // Called for each KlassInfoEntry.
  virtual void do_cinfo(KlassInfoEntry* cie) = 0;
};

class KlassInfoBucket: public CHeapObj<mtInternal> {
 private:
  KlassInfoEntry* _list;
  KlassInfoEntry* list()           { return _list; }
  void set_list(KlassInfoEntry* l) { _list = l; }
 public:
  KlassInfoEntry* lookup(Klass* k);
  void initialize() { _list = NULL; }
  void empty();
  void iterate(KlassInfoClosure* cic);
  void print_on(outputStream* out) const;
};

class KlassInfoTable: public StackObj {
 private:
  int _size;
  static const int _num_buckets = 20011;
  size_t _size_of_instances_in_words;

  // An aligned reference address (typically the least
  // address in the perm gen) used for hashing klass
  // objects.
  HeapWord* _ref;

  KlassInfoBucket* _buckets;
  uint hash(const Klass* p);
  KlassInfoEntry* lookup(Klass* k); // allocates if not found!

  class AllClassesFinder : public KlassClosure {
    KlassInfoTable *_table;
   public:
    AllClassesFinder(KlassInfoTable* table) : _table(table) {}
    virtual void do_klass(Klass* k);
  };

 public:
  KlassInfoTable(bool add_all_classes);
  ~KlassInfoTable();
  bool record_instance(const oop obj);
  void iterate(KlassInfoClosure* cic);
  bool allocation_failed() { return _buckets == NULL; }
  size_t size_of_instances_in_words() const;
  void print_on(outputStream* out) const;

  friend class KlassInfoHisto;
  friend class KlassHierarchy;
};

class KlassHierarchy : AllStatic {
 public:
  static void print_class_hierarchy(outputStream* st, bool print_interfaces,  bool print_subclasses,
                                    char* classname);

 private:
  static void set_do_print_for_class_hierarchy(KlassInfoEntry* cie, KlassInfoTable* cit,
                                               bool print_subclasse);
  static void print_class(outputStream* st, KlassInfoEntry* cie, bool print_subclasses);
};

class KlassInfoHisto : public StackObj {
 private:
  static const int _histo_initial_size = 1000;
  KlassInfoTable *_cit;
  GrowableArray<KlassInfoEntry*>* _elements;
  GrowableArray<KlassInfoEntry*>* elements() const { return _elements; }
  const char* _title;
  const char* title() const { return _title; }
  static int sort_helper(KlassInfoEntry** e1, KlassInfoEntry** e2);
  void print_elements(outputStream* st) const;
  void print_class_stats(outputStream* st, bool csv_format, const char *columns);
  julong annotations_bytes(Array<AnnotationArray*>* p) const;
  const char *_selected_columns;
  bool is_selected(const char *col_name);
  void print_title(outputStream* st, bool csv_format,
                   bool selected_columns_table[], int width_table[],
                   const char *name_table[]);

  template <class T> static int count_bytes(T* x) {
    return (HeapWordSize * ((x) ? (x)->size() : 0));
  }

  template <class T> static int count_bytes_array(T* x) {
    if (x == NULL) {
      return 0;
    }
    if (x->length() == 0) {
      // This is a shared array, e.g., Universe::the_empty_int_array(). Don't
      // count it to avoid double-counting.
      return 0;
    }
    return HeapWordSize * x->size();
  }

  // returns a format string to print a julong with the given width. E.g,
  // printf(num_fmt(6), julong(10)) would print out the number 10 with 4
  // leading spaces.
PRAGMA_DIAG_PUSH
PRAGMA_FORMAT_NONLITERAL_IGNORED

  static void print_julong(outputStream* st, int width, julong n) {
    int num_spaces = width - julong_width(n);
    if (num_spaces > 0) {
      st->print(str_fmt(num_spaces), "");
    }
    st->print(JULONG_FORMAT, n);
  }
PRAGMA_DIAG_POP

  static char* perc_fmt(int width) {
    static char buf[32];
    jio_snprintf(buf, sizeof(buf), "%%%d.1f%%%%", width-1);
    return buf;
  }

  static char* str_fmt(int width) {
    static char buf[32];
    jio_snprintf(buf, sizeof(buf), "%%%ds", width);
    return buf;
  }

  static int julong_width(julong n) {
    if (n == 0) {
      return 1;
    }
    int w = 0;
    while (n > 0) {
      n /= 10;
      w += 1;
    }
    return w;
  }

  static int col_width(julong n, const char *name) {
    int w = julong_width(n);
    int min = (int)(strlen(name));
    if (w < min) {
        w = min;
    }
    // add a leading space for separation.
    return w + 1;
  }

 public:
  KlassInfoHisto(KlassInfoTable* cit, const char* title);
  ~KlassInfoHisto();
  void add(KlassInfoEntry* cie);
  void print_histo_on(outputStream* st, bool print_class_stats, bool csv_format, const char *columns);
  void sort();
};

#endif // INCLUDE_SERVICES

// These declarations are needed since the declaration of KlassInfoTable and
// KlassInfoClosure are guarded by #if INLCUDE_SERVICES
class KlassInfoTable;
class KlassInfoClosure;

class HeapInspection : public StackObj {
  bool _csv_format; // "comma separated values" format for spreadsheet.
  bool _print_help;
  bool _print_class_stats;
  const char* _columns;
 public:
  HeapInspection(bool csv_format, bool print_help,
                 bool print_class_stats, const char *columns) :
      _csv_format(csv_format), _print_help(print_help),
      _print_class_stats(print_class_stats), _columns(columns) {}
  void heap_inspection(outputStream* st) NOT_SERVICES_RETURN;
  size_t populate_table(KlassInfoTable* cit, BoolObjectClosure* filter = NULL) NOT_SERVICES_RETURN_(0);
  static void find_instances_at_safepoint(Klass* k, GrowableArray<oop>* result) NOT_SERVICES_RETURN;
 private:
  void iterate_over_heap(KlassInfoTable* cit, BoolObjectClosure* filter = NULL);
};

class ObjectLayoutTask;

class ObjectLayout : public AllStatic
{
  friend class ObjectLayoutTask;
private:
  static ObjectLayoutTask* _task;
  static bool _organizing;
  static jlong start_time, stop_time, _val_start;
public:
  static void engage();
  static void disengage();
  static bool is_active() { return _task != NULL; }
  static void organize_objects(outputStream *out, const char *reason);
  static bool organizing() { return _organizing; }
#ifdef PROFILE_OBJECT_INFO
  static void reset_ref_cnts();
#endif
};

class ObjectLayoutTask : public PeriodicTask {
  public:
     ObjectLayoutTask(int interval_time) : PeriodicTask(interval_time) {}
     void task();
     static void engage();
     static void disengage();
};

#ifdef PROFILE_OBJECT_INFO
class ObjectInfoCollectionTask;

class ObjectInfoCollection : public AllStatic
{
  friend class ObjectInfoCollectionTask;
  friend class ObjectLayout;
private:
  static ObjectInfoCollectionTask* _task;
  static bool _collecting;
  static jlong _val_start;
  
public: 
  static bool collecting() { return _collecting; }
  static void engage();
  static void disengage();

  static ObjectInfoCollectionTask* task() { return _task; }
  static bool is_active() { return _task != NULL; }

  static void initialize();
  static void destroy();

  static void collect_gen_color_alloc_point_info(bool only_unmarked,
    PSGenType gen_type, HeapColor color);
  static void collect_alloc_point_info(bool only_unmarked);
  static void collect_object_info(outputStream *objinfo_log,
    outputStream *apinfo_log, const char *reason);
  static void print_object_info(outputStream *objinfo_log, bool header_only,
    const char *reason, bool post_gc);
  static void print_persistent_object_info(outputStream *out);
  static void print_gen_color_object_info(outputStream *out, HeapWord *ref,
    PSGenType gen_type, HeapColor color, bool header_only, bool post_gc);
  static void print_young_collection_stats(outputStream *out);
};

class ObjectInfoList : public CHeapObj {
  friend class ListAppendInstanceClosure;
 private:
  GrowableArray<oop>* _elements;
  GrowableArray<oop>* elements() const { return _elements; }
 public:
  ObjectInfoList(int initial_size);
  void print_oop_info(outputStream *st, oop cur_oop) const;
  void print_on(outputStream* st) const;
};

class ObjectInfoEntry : public CHeapObj {
  friend class ObjectInfoTable;
 private:
  ObjectInfoEntry* _next;
  klassOop         _klass;
  long             _instance_count;
  size_t           _instance_words;
  GrowableArray<oop>* _elements;
  GrowableArray<oop>* elements() const { return _elements; }

 public:

  ObjectInfoEntry(klassOop k, ObjectInfoEntry* next);
  ObjectInfoEntry* next()  { return _next; }

  bool is_equal(klassOop k)     { return k == _klass; }
  klassOop klass()              { return _klass; }
  long count()                  { return _instance_count; }
  void set_count(long ct)       { _instance_count = ct; }
  size_t words()                { return _instance_words; }
  void set_words(size_t wds)    { _instance_words = wds; }
  int compare(ObjectInfoEntry* e1, ObjectInfoEntry* e2);
  static int oop_compare(oop *o1, oop *o2);
  void print_oop_info(outputStream *st, oop cur_oop) const;
  void print_on(outputStream* st) const;
  void trim_print_on(outputStream* st) const;
  void long_print_on(outputStream* st) const;
};

class ObjectInfoClosure: public StackObj {
 public:
  // Called for each ObjectInfoEntry.
  virtual void do_cinfo(ObjectInfoEntry* koie) = 0;
};

class ObjectInfoBucket: public CHeapObj {
 private:
  ObjectInfoEntry* _list;
  ObjectInfoEntry* list()           { return _list; }
  void set_list(ObjectInfoEntry* l) { _list = l; }
 public:
  ObjectInfoEntry* lookup(const klassOop k);
  void initialize() { _list = NULL; }
  void empty();
  void iterate(ObjectInfoClosure* cic);
};

#define MAX_AGE 6
#define AGE_TABLE_SIZE MAX_AGE+1

class ObjectInfoTable: public StackObj {
 private:
  int _size;

  // An aligned reference address (typically the least
  // address in the perm gen) used for hashing klass
  // objects.
  HeapWord* _ref;

  ObjectInfoBucket* _buckets;
  uint hash(klassOop p);
  ObjectInfoEntry* lookup(const klassOop k);
  jint _live_object_age_table[AGE_TABLE_SIZE];
  jint _live_size_age_table[AGE_TABLE_SIZE];
  jint _hot_object_age_table[AGE_TABLE_SIZE];
  jint _hot_size_age_table[AGE_TABLE_SIZE];

 public:
  // Table size
  enum {
    oit_size = 20011
  };
  ObjectInfoTable(int size, HeapWord* ref);
  ~ObjectInfoTable();
  bool append_instance(const oop obj);
  void append_to_live_age_tables(const oop obj);
  void append_to_hot_age_tables(const oop obj);
  void reset_age_tables();
  void print_age_tables(outputStream *out, char *pref);
  void iterate(ObjectInfoClosure* cic);
  bool allocation_failed() { return _buckets == NULL; }
};

class ObjectInfoHisto : public StackObj {
 private:
  GrowableArray<ObjectInfoEntry*>* _elements;
  GrowableArray<ObjectInfoEntry*>* elements() const { return _elements; }
  const char* _title;
  const char* title() const { return _title; }
  static int sort_helper(ObjectInfoEntry** e1, ObjectInfoEntry** e2);
  void print_elements(outputStream* st) const;
 public:
  enum {
    histo_initial_size = 1000
  };
  ObjectInfoHisto(int estimatedCount);
  ~ObjectInfoHisto();
  void set_title(const char* title);
  void add(ObjectInfoEntry* cie);
  void print_on(outputStream* st) const;
  void sort();
};

class ObjectInfoCollectionTask : public PeriodicTask {
  public:
     ObjectInfoCollectionTask(int interval_time) : PeriodicTask(interval_time) {}
     void task();
     static void engage();
     static void disengage();
};

#if 0
/* this was experimental to see if these values changed from sample to sample
 * -- they did not -- at least in the tenured generation collection -- but we
 * keep this around in case we want to use it for debugging
 */
class ValObjectInfo : public CHeapObj {
 private:
  int _val, _size;
  intptr_t _addr;
 public:
  ValObjectInfo (int val, int size, intptr_t addr) :
    _val(val), _size(size), _addr(addr) {}

  int size()      { return _size; }
  intptr_t addr() { return _addr; }
  int val()       { return _val; }
};

class PersistentObjectInfo : public CHeapObj {
  friend class PersistentObjectInfoEntry;
 private:
  int _id;
  GrowableArray<ValObjectInfo*>* _vals;
  GrowableArray<ValObjectInfo*>* vals() const { return _vals; }

 public:
  PersistentObjectInfo(int id);
  int id()               { return _id; }
  ValObjectInfo* append_new_val(int vnum, int size, intptr_t addr);
};
#endif


class AllocPointInfoCollectionTask;

class AllocPointInfoCollection : public AllStatic
{
  friend class AllocPointInfoCollectionTask;
  friend class ObjectLayout;
private:
  static AllocPointInfoCollectionTask* _task;
  static bool _collecting;
  static jlong _val_start;
  
public: 
  static bool collecting() { return _collecting; }
  static void engage();
  static void disengage();

  static AllocPointInfoCollectionTask* task() { return _task; }
  static bool is_active() { return _task != NULL; }

  static void initialize();
  static void destroy();

  static void collect_object_info(outputStream *out, const char *reason);
  static void print_object_info(outputStream *out,
              bool header_only, const char *reason, bool post_gc);
  static void print_persistent_object_info(outputStream *out);
  static void print_gen_color_object_info(outputStream *out, HeapWord *ref,
    PSGenType gen_type, HeapColor color, bool header_only, bool post_gc);
  static void print_young_collection_stats(outputStream *out);
};


class AllocPointInfo : public CHeapObj {
 private:
  unsigned int _id;
  HeapColor _color;

  jlong _total_size;
  jlong _total_objects;

  jlong _tot_load_cnt;
  jlong _tot_store_cnt;

  jlong _val_size;
  jlong _val_objects;

  jlong _val_load_cnt;
  jlong _val_store_cnt;

  jlong _val_hot_size;
  jlong _val_hot_objects;

  jlong _val_new_size;
  jlong _val_new_objects;
#if 0
  jint _colored_total_size[HC_TOTAL];
  jint _colored_total_objects[HC_TOTAL];

  jint _colored_load_cnt[HC_TOTAL];
  jint _colored_store_cnt[HC_TOTAL];
#endif

 public:
  AllocPointInfo(jint id, HeapColor color=HC_NOT_COLORED) :
    _id(id), _color(color) {
#if 0
    _colored_load_cnt[HC_RED]   = 0;
    _colored_load_cnt[HC_BLUE]  = 0;
    _colored_store_cnt[HC_RED]  = 0;
    _colored_store_cnt[HC_BLUE] = 0; 
#endif
    _total_size    = 0;  
    _total_objects = 0;  
    _tot_load_cnt  = 0;
    _tot_store_cnt = 0;
    reset_val_cnts();
  }

  unsigned int id()                  { return _id;              }
  HeapColor color()                  { return _color;           }

  jlong total_objects   ()           { return _total_objects;   }
  jlong total_size      ()           { return _total_size;      }

  jlong tot_load_cnt    ()           { return _tot_load_cnt;    }
  jlong tot_store_cnt   ()           { return _tot_store_cnt;   }

  jlong val_load_cnt    ()           { return _val_load_cnt;    }
  jlong val_store_cnt   ()           { return _val_store_cnt;   }

  jlong val_objects     ()           { return _val_objects;     }
  jlong val_size        ()           { return _val_size;        }

  jlong val_hot_objects ()           { return _val_hot_objects; }
  jlong val_hot_size    ()           { return _val_hot_size;    }

  jlong val_new_objects ()           { return _val_new_objects; }
  jlong val_new_size    ()           { return _val_new_size;    }

#if 0
  jint colored_load_cnt  (HeapColor color) { return _colored_load_cnt[color];  }
  jint colored_store_cnt (HeapColor color) { return _colored_store_cnt[color]; }
  void set_color(HeapColor color) { _color = color; }
#endif

  void mark_val(oop o);
#if 0
  void mark_load();
  void mark_store();
  void mark_hot(oop o);
#endif
  void mark_new_object(int size);

  void reset_val_cnts();
  void reset_ref_cnts();
  void add_val_to_totals(AllocPointInfoTable *apit);
};


class AllocPointInfoEntry : public CHeapObj {
 private:
  AllocPointInfoEntry *_next;
  char *_key;
  AllocPointInfo *_value;
 public:
  AllocPointInfoEntry(const char *k, unsigned int v,
    AllocPointInfoEntry *next, HeapColor color=HC_NOT_COLORED);
  ~AllocPointInfoEntry();
  char *key()                       { return _key;   }
  AllocPointInfo *value()           { return _value; }
  AllocPointInfoEntry *next()        { return _next;  }
  void set_value(AllocPointInfo* v) { _value = v;    }
  bool matches(const char *key);
  void print_map_on(outputStream *out);
  void print_val_info(outputStream *out);
  /*void print_new_val_info(outputStream *out);*/
  void reset_val_cnts();
  void reset_ref_cnts();
  void add_val_to_totals(AllocPointInfoTable *apit);
};

class AllocPointInfoBucket : public CHeapObj {
 private:
	AllocPointInfoEntry *_aps;
 public:
  AllocPointInfoBucket();
  ~AllocPointInfoBucket();
  void initialize() { _aps = NULL; }
  void empty();
  AllocPointInfoEntry* get_apie(const char *key);
  AllocPointInfoEntry* insert(const char *key, unsigned int value,
    HeapColor color=HC_NOT_COLORED);
  void print_map_on(outputStream *out);
  void print_val_info(outputStream *out);
  /*void print_new_val_info(outputStream *out);*/
  void reset_val_cnts();
  void reset_ref_cnts();
  void accumulate_val_totals(AllocPointInfoTable *apit);
};

class AllocPointInfoTable : public CHeapObj {
 private:
	AllocPointInfoBucket *_buckets;
  unsigned long hash(const char *str);
  unsigned int _size, _cur_id;

  jlong _val_total_objects;
  jlong _val_total_size;
  jlong _val_total_new_objects;
  jlong _val_total_new_size;
  jlong _val_total_hot_objects;
  jlong _val_total_hot_size;
  jlong _val_total_load_cnt;
  jlong _val_total_store_cnt;

 public:
  AllocPointInfoTable (unsigned int capacity);
  ~AllocPointInfoTable();

  enum {
    apit_size = 8192
  };

  AllocPointInfo *get(methodOopDesc *ap_method, int ap_bci,
    HeapColor color=HC_NOT_COLORED);
  void print_map_on(outputStream *out);
  void print_val_info(outputStream *out);
  /*void print_new_val_info(outputStream *out);*/
  void reset_val_cnts();
  void reset_ref_cnts();

  void reset_val_totals();
  void compute_val_totals();

  void add_val_total_objects(jlong v)     { _val_total_objects += v;     }
  void add_val_total_size(jlong v)        { _val_total_size += v;        }
  void add_val_total_new_objects(jlong v) { _val_total_new_objects += v; }
  void add_val_total_new_size(jlong v)    { _val_total_new_size += v;    }
  void add_val_total_hot_objects(jlong v) { _val_total_hot_objects += v; }
  void add_val_total_hot_size(jlong v)    { _val_total_hot_size += v;    }
  void add_val_total_load_cnt(jlong v)    { _val_total_load_cnt += v;    }
  void add_val_total_store_cnt(jlong v)   { _val_total_store_cnt += v;   }

  bool allocation_failed() { return _buckets == NULL; }
};

class PersistentObjectInfo : public CHeapObj {
  friend class PersistentObjectInfoEntry;
 private:
  jint _id, _is_new, _mark;
  intptr_t _addr;
  int _size, _cval;
#if 0
  int _size, _cval, _ap_bci;
  Symbol *_ap_klass;
  Symbol *_ap_name;
  Symbol *_ap_signature;
#endif
  AllocPointInfo *_alloc_point;
  HeapColor _color;

  jint _colored_load_cnt[HC_TOTAL];
  jint _colored_store_cnt[HC_TOTAL];
  jint _val_load_cnt;
  jint _val_store_cnt;
  jint _tot_load_cnt;
  jint _tot_store_cnt;

 public:
#if 0
  PersistentObjectInfo(jint id, int cval, int size, Symbol* ap_klass,
    Symbol* ap_name, Symbol *ap_sig, int ap_bci, HeapColor color,
    intptr_t addr) :
    _id(id), _cval(cval), _size(size), _ap_klass(ap_klass),
    _ap_name(ap_name), _ap_signature(ap_sig), _ap_bci(ap_bci),
#endif
  PersistentObjectInfo(jint id, int cval, int size, AllocPointInfo *alloc_point,
    HeapColor color, intptr_t addr) :
    _id(id), _cval(cval), _size(size), _alloc_point(alloc_point),
    _color(color), _addr(addr) {
    _colored_load_cnt[HC_RED]   = 0;
    _colored_load_cnt[HC_BLUE]  = 0;
    _colored_store_cnt[HC_RED]  = 0;
    _colored_store_cnt[HC_BLUE] = 0; 
    _val_load_cnt               = 0;
    _val_store_cnt              = 0;
    _tot_load_cnt               = 0;
    _tot_store_cnt              = 0;
    _is_new                     = 1;
    _mark                       = 0;
  }

  jint id()                     { return _id;           }
  int cval()                    { return _cval;         }
  int size()                    { return _size;         }
  AllocPointInfo *alloc_point() { return _alloc_point;  }
#if 0
  int ap_bci()               { return _ap_bci;       }
  Symbol *ap_klass()         { return _ap_klass;     }
  Symbol *ap_name()          { return _ap_name;      }
  Symbol *ap_signature()     { return _ap_signature; }
#endif
  HeapColor color()          { return _color;        }
  intptr_t addr()            { return _addr;         }

  jint val_load_cnt  ()      { return _val_load_cnt;  }
  jint val_store_cnt ()      { return _val_store_cnt; }

  jint tot_load_cnt  ()      { return _tot_load_cnt;  }
  jint tot_store_cnt ()      { return _tot_store_cnt; }

  jint colored_load_cnt  (HeapColor color) { return _colored_load_cnt[color];  }
  jint colored_store_cnt (HeapColor color) { return _colored_store_cnt[color]; }

  jint mark ()               { return _mark;               }
  void set_mark ()           { Atomic::store(1, &_mark);   }
  void reset_mark ()         { Atomic::store(0, &_mark);   }

  bool is_new ()             { return _is_new;             }
  void reset_is_new ()       { Atomic::store(0, &_is_new); }

  void reset_val_load_cnt()  { Atomic::store(0, &_val_load_cnt);  }
  void reset_val_store_cnt() { Atomic::store(0, &_val_store_cnt); }

  void mark_load();
  void mark_store();

  void batch_mark_load(int n);
  void batch_mark_store(int n);
};

class PersistentObjectInfoEntry : public CHeapObj {
  friend class PersistentObjectInfoTable;
 private:
  PersistentObjectInfoEntry* _next;
  klassOop         _klass;
  long             _instance_count;
  size_t           _instance_words;
  char*            _klass_name;
  size_t           _klass_name_len;
  GrowableArray<PersistentObjectInfo*>* _elements;
  GrowableArray<PersistentObjectInfo*>* elements() const { return _elements; }

 public:

  PersistentObjectInfoEntry(klassOop k, PersistentObjectInfoEntry* next);
  PersistentObjectInfoEntry* next()  { return _next; }

  bool is_equal(klassOop k)     { return k == _klass; }
  klassOop klass()              { return _klass; }
  long count()                  { return _instance_count; }
  void set_count(long ct)       { _instance_count = ct; }
  size_t words()                { return _instance_words; }
  void set_words(size_t wds)    { _instance_words = wds; }
  char *klass_name()            { return _klass_name; }
  int compare(PersistentObjectInfoEntry* e1, PersistentObjectInfoEntry* e2);
  static int poi_compare(PersistentObjectInfo **o1, PersistentObjectInfo **o2);
  bool contains(int id);
  void get_klass_name();
  void print_poi(outputStream *st, PersistentObjectInfo *cur_poi) const;
  void print_on(outputStream* st) const;
  PersistentObjectInfo *append_new_poi(jint id, int cval, int size,
    AllocPointInfo *api, HeapColor color, intptr_t addr);
  PersistentObjectInfo *find_poi(jint id);
};

class PersistentObjectInfoClosure: public CHeapObj {
 public:
  // Called for each PersistentObjectInfoEntry.
  virtual void do_cinfo(PersistentObjectInfoEntry* koie) = 0;
};

class PersistentObjectInfoBucket: public CHeapObj {
 private:
  PersistentObjectInfoEntry* _list;
  PersistentObjectInfoEntry* list()           { return _list; }
  void set_list(PersistentObjectInfoEntry* l) { _list = l; }
 public:
  PersistentObjectInfoEntry* lookup(const klassOop k);
  void initialize() { _list = NULL; }
  void empty();
  void iterate(PersistentObjectInfoClosure* cic);
};

class PersistentObjectInfoTable: public CHeapObj {
 private:
  int _size;
  int _cur_val;
  static bool _printing;

  // An aligned reference address (typically the least
  // address in the perm gen) used for hashing klass
  // objects.
  HeapWord* _ref;

  PersistentObjectInfoBucket* _buckets;
  uint hash(klassOop p);
  PersistentObjectInfoEntry* lookup(const klassOop k);

   public:
  // Table size
  enum {
    oit_size = 20011
  };
  PersistentObjectInfoTable(int size, HeapWord* ref);
  ~PersistentObjectInfoTable();
  PersistentObjectInfo* append_instance(const oop obj, AllocPointInfo *api,
                                        HeapColor color);
  void iterate(PersistentObjectInfoClosure* cic);
  bool allocation_failed() { return _buckets == NULL; }
  void inc_cur_val() { _cur_val++;      }
  int cur_val()      { return _cur_val; }
  static void set_printing(bool b) { _printing = b;}
  static bool is_printing() { return _printing; }
};

class PersistentObjectInfoHisto : public StackObj {
 private:
  GrowableArray<PersistentObjectInfoEntry*>* _elements;
  GrowableArray<PersistentObjectInfoEntry*>* elements() const { return _elements; }
  const char* _title;
  const char* title() const { return _title; }
  static int sort_helper(PersistentObjectInfoEntry** e1, PersistentObjectInfoEntry** e2);
  void print_elements(outputStream* st) const;
 public:
  enum {
    histo_initial_size = 1000
  };
  PersistentObjectInfoHisto(int estimatedCount);
  ~PersistentObjectInfoHisto();
  void set_title(const char* title);
  void add(PersistentObjectInfoEntry* cie);
  void print_on(outputStream* st) const;
  void sort();
};

class MinorDeadInstanceClosure : public ObjectClosure {
  MutableSpace *_live_space, *_tenured_space;
 public:
  MinorDeadInstanceClosure(MutableSpace *live_space,
                           MutableSpace *tenured_space) :
    _live_space(live_space), _tenured_space(tenured_space) {}
  void do_object(oop obj) {
    if (obj->blueprint()->oop_is_instance()) {
      instanceOop inst_oop = (instanceOop)obj;
      if (inst_oop->is_initialized()) {
        if (!(_live_space->contains(inst_oop->forwardee()) ||
              _tenured_space->contains(inst_oop->forwardee())) ) {
          deadobj_log->print_cr("  " INT64_FORMAT_W(12), inst_oop->id());
        }
      }
    } else if (obj->blueprint()->oop_is_array()) {
      arrayOop arr_oop = (arrayOop)obj;
      if (arr_oop->is_initialized()) {
        if (!(_live_space->contains(arr_oop->forwardee()) ||
              _tenured_space->contains(arr_oop->forwardee())) ) {
          deadobj_log->print_cr("  " INT64_FORMAT_W(12), arr_oop->id());
        }
      }
    }
  }
};

class MajorDeadInstanceClosure : public ObjectClosure {
 public:
  MajorDeadInstanceClosure() {}
  void do_object(oop obj) {
    if (obj->blueprint()->oop_is_instance()) {
      instanceOop inst_oop = (instanceOop)obj;
      if (inst_oop->is_initialized()) {
        if (!inst_oop->is_gc_marked()) {
          deadobj_log->print_cr("  " INT64_FORMAT_W(12), inst_oop->id());
        }
      }
    } else if (obj->blueprint()->oop_is_array()) {
      arrayOop arr_oop = (arrayOop)obj;
      if (arr_oop->is_initialized()) {
        if (!arr_oop->is_gc_marked()) {
          deadobj_log->print_cr("  " INT64_FORMAT_W(12), arr_oop->id());
        }
      }
    }
  }
};
#endif // PROFILE_OBJECT_INFO

#ifdef PROFILE_OBJECT_ADDRESS_INFO
class ObjectAddressInfoCollectionTask;

class ObjectAddressInfoCollection : public AllStatic
{
  friend class ObjectAddressInfoCollectionTask;
  friend class ObjectLayout;
private:
  static ObjectAddressInfoCollectionTask* _task;
  static bool _collecting;
  static jlong _val_start;
  
public: 
  static bool collecting() { return _collecting; }
  static void engage();
  static void disengage();

  static ObjectAddressInfoCollectionTask* task() { return _task; }
  static bool is_active() { return _task != NULL; }

  static void initialize();
  static void destroy();

  static void collect_object_address_info(outputStream *addrinfo_log,
    outputStream *fieldinfo_log, const char *reason);
  static void print_object_address_info(outputStream *addrinfo_log,
    const char *reason);
};

class ObjectAddressInfoCollectionTask : public PeriodicTask {
  public:
     ObjectAddressInfoCollectionTask(int interval_time) : PeriodicTask(interval_time) {}
     void task();
     static void engage();
     static void disengage();
};

#define OAR_MARKER 0xDEFECA7E
#define EMPTY_MARKER -0xDEADBEEF
#define PAGE_SHIFT 12
#define PAGE_SIZE (1<<PAGE_SHIFT)
#define MARK_OOP_SIZE 8
#define KLASS_OOP_SIZE (UseCompressedOops ? 4 : 8)
#define OOP_HEADER_SIZE (MARK_OOP_SIZE + KLASS_OOP_SIZE)

enum heap_space {
  HS_INVALID_SPACE = -1,
  HS_EDEN_SPACE,
  HS_SURVIVOR_SPACE,
  HS_TENURED_SPACE,
  HS_PERM_SPACE,
  HS_NR_SPACES
};

enum heap_mark {
  BOTTOM_ADDR,
  TOP_ADDR,
  END_ADDR,
  NR_MARKS
};

struct object_address_record {
  intptr_t addr;
  int size;
  jlong ref_cnt;
};

#if 0
class FieldRecord : public CHeapObj {
 private:
  int _offset;
  int _size;
  jint _load_cnt;
  jint _store_cnt;
 public:
  FieldRecord(int offset, int size) :
    _offset(offset), _size(size), _load_cnt(0), _store_cnt(0) {}

  int offset()     { return _offset;    }
  int size()       { return _size;      }
  jint load_cnt()  { return _load_cnt;  }
  jint store_cnt() { return _store_cnt; }

  void set_size(int s)      { _size = s;                   }
  void inc_load_cnt()       { Atomic::inc(&_load_cnt);     }
  void inc_store_cnt()      { Atomic::inc(&_store_cnt);    }
  void add_load_cnt(int n)  { Atomic::add(n, &_load_cnt);  }
  void add_store_cnt(int n) { Atomic::add(n, &_store_cnt); }

  void reset_ref_cnts() { _load_cnt = 0; _store_cnt = 0; }

  int  compare(FieldRecord *f1, FieldRecord *f2);
  static int sort_helper(FieldRecord **f1, FieldRecord **f2);
};
#endif

class FieldRecord : public CHeapObj {
 private:
  jint _load_cnt;
  jint _store_cnt;
 public:
  FieldRecord() : _load_cnt(0), _store_cnt(0) {}

  jint load_cnt()  { return _load_cnt;  }
  jint store_cnt() { return _store_cnt; }

  void inc_load_cnt()       { Atomic::inc(&_load_cnt);     }
  void inc_store_cnt()      { Atomic::inc(&_store_cnt);    }
  void add_load_cnt(int n)  { Atomic::add(n, &_load_cnt);  }
  void add_store_cnt(int n) { Atomic::add(n, &_store_cnt); }

  void reset_ref_cnts() { _load_cnt = 0; _store_cnt = 0; }
};

class ObjectAddressInfo;

class KlassRecord : public CHeapObj {

 public:
  enum klass_stat {
    KS_LIVE_OBJECTS,
    KS_LIVE_SIZE,
    KS_LIVE_REFS,
    KS_HOT_OBJECTS,
    KS_HOT_SIZE,
    KS_HOT_REFS,
    KS_NEW_OBJECTS,
    KS_NEW_SIZE,
    KS_NEW_REFS,
    NR_KLASS_STATS
  };

 private:

  klassOop _klass;
  char*    _klass_name;
  size_t   _klass_name_len;

  int _nr_fields;
  GrowableArray<FieldRecord*>* _fields;

  enum klass_type _klass_type;
  int  _instance_size;

  jint _stats[HS_NR_SPACES][NR_KLASS_STATS];

 public:
  KlassRecord(klassOop k, int instance_size, klass_type ktype);
  GrowableArray<FieldRecord*>* fields()  { return _fields;            }

  int instance_size()                    { return _instance_size;     }
  void set_instance_size(int new_size)   { _instance_size = new_size; }

  enum klass_type klass_type()           { return _klass_type;        }
  void set_klass_type(enum klass_type kt){ _klass_type = kt;          }

  klassOop klass()                       { return _klass;             }
  bool is_equal(klassOop k)              { return k == _klass;        }

  char *klass_name()                     { return _klass_name;        }
  int   klass_name_len()                 { return _klass_name_len;    }

  jint get_stat(enum heap_space hs, enum klass_stat ks) {
    return _stats[hs][ks];
  }

  jint add_stat(enum heap_space hs, enum klass_stat ks, jint val) {
    return _stats[hs][ks] += val;
  }

  jint inc_stat(enum heap_space hs, enum klass_stat ks) {
    return _stats[hs][ks] += 1;
  }

  jint get_total_stat(enum klass_stat ks) {
    jint sum = 0;
    for (int i = 0 ; i < HS_NR_SPACES; i++) {
      sum += _stats[i][ks];
    }
    return sum;
  }

  jint live_objects() { return get_total_stat(KS_LIVE_OBJECTS); }
  jint live_size()    { return get_total_stat(KS_LIVE_SIZE);    }
  jint live_refs()    { return get_total_stat(KS_LIVE_REFS);    }
  jint hot_objects()  { return get_total_stat(KS_HOT_OBJECTS);  }
  jint hot_size()     { return get_total_stat(KS_HOT_SIZE);     }
  jint hot_refs()     { return get_total_stat(KS_HOT_REFS);     }
  jint new_objects()  { return get_total_stat(KS_NEW_OBJECTS);  }
  jint new_size()     { return get_total_stat(KS_NEW_SIZE);     }
  jint new_refs()     { return get_total_stat(KS_NEW_REFS);     }

  bool is_instance()  { return ((_klass_type == KT_VM_INSTANCE)  ||
                                (_klass_type == KT_APP_INSTANCE)); }
  bool is_array()     { return ((_klass_type == KT_VM_ARRAY)     ||
                                (_klass_type == KT_APP_ARRAY));    }

  void print_on(outputStream* st);
  void reset_ref_cnts();
  void reset_klass_stats();
  void add_to_klass_stats(ObjectAddressInfo *oai, enum heap_space hs);

  //FieldRecord *find_field_record(int field_offset, int size);
  void mark_field_load (int field_offset, int size);
  void mark_field_store(int field_offset, int size);
};

class KlassRecordEntry : public CHeapObj {
 private:
  KlassRecordEntry *_next;
  klassOop _key;
  KlassRecord *_value;
 public:
  KlassRecordEntry(klassOop o, int instance_size, klass_type ktype,
                   KlassRecordEntry *next);
  ~KlassRecordEntry();
  klassOop key()              { return _key;   }
  KlassRecordEntry *next()    { return _next;  }
  KlassRecord *value()        { return _value; }
  int compare(KlassRecordEntry* e1, KlassRecordEntry* e2);
  void print_on(outputStream *out);
  void reset_ref_cnts();
  //void reset_klass_stats(bool do_all_spaces);
  void reset_klass_stats();
  void add_to_klass_totals(ObjectAddressInfoTable *oait);
};

class KlassRecordBucket : public CHeapObj {
 private:
	KlassRecordEntry *_kres;
 public:
  KlassRecordBucket();
  ~KlassRecordBucket();
  void initialize() { _kres = NULL; }
  void empty();
  void empty_spaces(bool do_all_spaces, ObjectAddressInfoTable *oait);
  void copy_entries_to(ObjectAddressInfoTable *dst_oait);
  KlassRecordEntry* get_kre(klassOop obj);
  KlassRecordEntry* kt_insert(klassOop obj, int instance_size, klass_type ktype);
  void print_on(outputStream *out);
  void reset_ref_cnts();
  //void reset_klass_stats(bool do_all_spaces);
  void reset_klass_stats();
  void accumulate_klass_totals(ObjectAddressInfoTable *oait);
};

class ObjectAddressInfo : public CHeapObj {
 private:
  intptr_t _addr;
  int _size;
  jint _load_cnt;
  jint _store_cnt;
  jint _init_cnt;
  enum obj_type _type;
  KlassRecord *_klass_record;
  //klassOop _klass;
 public:
#if 0
  ObjectAddressInfo(oop obj) :
    _addr((intptr_t)obj) {
    _size = obj->size();
    _load_cnt  = 0;
    _store_cnt = 0;
  }
#endif

  ObjectAddressInfo(oop obj, int obj_size, KlassRecord *kr,obj_type type,
    bool old_rec) :
    _addr((intptr_t)obj), _size(obj_size) {
    _klass_record = kr;
    _load_cnt  = 0;
    _store_cnt = 0;
    _init_cnt  = old_rec ? OLD_MARKER : NEW_MARKER;
    _type      = type;
  }

  intptr_t addr()    { return _addr;      }
  int size()         { return _size;      }
  jint load_cnt()    { return _load_cnt;  }
  jint store_cnt()   { return _store_cnt; }
  jint init_cnt()    { return _init_cnt;  }

  void set_size(int s)       { _size = s; }
  void inc_load_cnt()        { Atomic::inc(&_load_cnt);      }
  void inc_store_cnt()       { Atomic::inc(&_store_cnt);     }
  void add_load_cnt(int n)   { Atomic::add(n, &_load_cnt);   }
  void add_store_cnt(int n)  { Atomic::add(n, &_store_cnt);  }
  jint add_init_cnt(int n)   { Atomic::add(n, &_init_cnt);   }
  jint set_init_cnt(int n)   { Atomic::store(n, &_init_cnt); }

  KlassRecord *klass_record()            { return _klass_record; }
  void set_klass_record(KlassRecord *kr) { _klass_record = kr;   }

  enum obj_type type()              { return _type; }
  void set_type(enum obj_type type) { _type = type; }

  void add_to_object_totals(ObjectAddressInfoTable *oait);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER) {
    _load_cnt  = 0;
    _store_cnt = 0;
    _init_cnt  = im;
  }
};

class ObjectAddressInfoEntry : public CHeapObj {
 private:
  ObjectAddressInfoEntry *_next;
  oop _key;
  ObjectAddressInfo *_value;
 public:
  ObjectAddressInfoEntry(oop o, int obj_size, KlassRecord *kr,
    obj_type type, ObjectAddressInfoEntry *next, bool old_rec=false);
  ~ObjectAddressInfoEntry();
  oop key()                         { return _key;   }
  ObjectAddressInfoEntry *next()    { return _next;  }
  ObjectAddressInfo *value()        { return _value; }
  void print_on(outputStream *out);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER);
  //void reset_ref_cnts();
  void add_to_heap_stats(ObjectAddressInfoTable *oait);
  void set_next(ObjectAddressInfoEntry *ne) { _next = ne; }
};

class ObjectAddressInfoBucket : public CHeapObj {
 private:
	ObjectAddressInfoEntry *_oaies;
 public:
  ObjectAddressInfoBucket();
  ~ObjectAddressInfoBucket();
  void initialize() { _oaies = NULL; }
  void empty();
  //void empty_spaces(bool do_all_spaces, ObjectAddressInfoTable *oait, bool pp=false);
  void empty_spaces(bool do_all_spaces, ObjectAddressInfoTable *oait);
  void copy_entries_to(ObjectAddressInfoTable *dst_oait);
  ObjectAddressInfoEntry* get_oaie(oop obj);
  ObjectAddressInfoEntry* insert(oop obj, int obj_size, KlassRecord *kr,
                                 obj_type type, bool old_rec=false);
  void print_on(outputStream *out);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER);
  //void reset_ref_cnts();
  void accumulate_heap_stats(ObjectAddressInfoTable *oait);
	ObjectAddressInfoEntry *oaies() { return _oaies; }
};

//#define NORMAL_OAIT_SIZE 262144
#define OAIT_SIZE 52428800
//#define OAIT_SIZE 1048576
#define KLASS_TABLE_SIZE 20015

class ObjectAddressInfoTable : public CHeapObj {
 public:

  enum heap_stat {
    USED_BYTES,
    LIVE_OBJECTS,
    LIVE_SIZE,
    LIVE_REFS,
    HOT_OBJECTS,
    HOT_SIZE,
    HOT_REFS,
    NEW_OBJECTS,
    NEW_SIZE,
    NEW_REFS,
    VM_OBJECTS,
    VM_SIZE,
    VM_REFS,
    FILLER_OBJECTS,
    FILLER_SIZE,
    FILLER_REFS,
    APP_OBJECTS,
    APP_SIZE,
    APP_REFS,
    NR_HEAP_STATS
  };

  enum klass_stat_total {
    KST_NR_KLASSES,
    KST_LIVE_OBJECTS,
    KST_LIVE_SIZE,
    KST_LIVE_REFS,
    KST_HOT_OBJECTS,
    KST_HOT_SIZE,
    KST_HOT_REFS,
    KST_NEW_OBJECTS,
    KST_NEW_SIZE,
    KST_NEW_REFS,
#if 0
    KST_INSTANCE_OBJECTS,
    KST_INSTANCE_SIZE,
    KST_HOT_INSTANCE_OBJECTS,
    KST_HOT_INSTANCE_SIZE,
    KST_ARRAY_OBJECTS,
    KST_ARRAY_SIZE,
    KST_HOT_ARRAY_OBJECTS,
    KST_HOT_ARRAY_SIZE,
#endif
    NR_KS_TOTALS
  };

 private:
	ObjectAddressInfoBucket *_buckets;
  KlassRecordBucket *_klass_buckets;
  unsigned long hash(oop obj);
  unsigned int _size, _kr_size, _cur_val;
  bool _using_to_space;

  intptr_t heap_boundaries[HS_NR_SPACES][NR_MARKS];
  jint heap_stats[HS_NR_SPACES][NR_HEAP_STATS];
  jint klass_totals[NR_KS_TOTALS];

  //GrowableArray<oop>* _known_free;
 
 public:
  ObjectAddressInfoTable (unsigned int obj_table_size, unsigned int klass_table_size);
  ~ObjectAddressInfoTable();
  //GrowableArray<oop>* known_free() const { return _known_free; }

  ObjectAddressInfo *insert(oop obj);
  ObjectAddressInfo *insert(oop obj, int obj_size, klassOop klass,
                            obj_type type, bool old_rec=false);
  ObjectAddressInfo *lookup(oop obj);
  KlassRecord *kt_lookup(klassOop k);
  KlassRecord *kt_insert(klassOop k, int instance_size, klass_type ktype);

  //ObjectAddressInfo *mark_alloc(oop obj, int size, enum obj_type type,
  //  klassOop klass);
  //void mark_known_free(oop obj);
  ObjectAddressInfo *mark_alloc(oop obj);
  ObjectAddressInfo *mark_filler(oop obj, int size);
  void mark_load(oop obj);
  void mark_store(oop obj);
  void mark_load(oop obj, intptr_t field, int size);
  void mark_store(oop obj, intptr_t field, int size);
  void batch_mark_load(oop obj, int n);
  void batch_mark_store(oop obj, int n);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER);
  //void reset_ref_cnts();
  void print_table(outputStream *out);
  void print_header(outputStream *out, const char *reason);
  void print_klass_table(outputStream *out);
  void print_klass_header(outputStream *out, const char *reason);
  void print_on(outputStream *addrout, outputStream *fieldout, const char *reason);
  enum heap_space get_space(intptr_t addr);
  void compute_heap_stats();
  void add_to_heap_stats(ObjectAddressInfo *oai);
  void add_to_klass_totals(KlassRecord *kr);
  void reset_heap_stats();
  void reset_klass_stats();
  void record_heap_boundaries();
  void print_klass_stats(outputStream *out);
  void print_heap_stats(outputStream *out, const char *space, enum heap_space hs);
  unsigned int cur_val()           { return _cur_val; }
  void inc_cur_val()               { _cur_val++;      }
  void set_cur_val(unsigned int v) { _cur_val = v;    }
  void reset_spaces(bool do_all_spaces);
  void copy_to(ObjectAddressInfoTable *dst_oait);
  //bool is_known_free(oop obj);

  bool using_to_space()   { return _using_to_space;         }
  bool using_from_space() { return !_using_to_space;        }
  bool use_to_space()     { _using_to_space = true;  record_heap_boundaries(); }
  bool use_from_space()   { _using_to_space = false; record_heap_boundaries(); }

  bool is_young(intptr_t addr) {
    if (( addr >= heap_boundaries[HS_EDEN_SPACE][BOTTOM_ADDR] &&
          addr <  heap_boundaries[HS_EDEN_SPACE][END_ADDR] ) ||
        ( addr >= heap_boundaries[HS_SURVIVOR_SPACE][BOTTOM_ADDR] &&
          addr <  heap_boundaries[HS_SURVIVOR_SPACE][END_ADDR] )) {
      return true;
      }
    return false;
  }

  bool is_tenured(intptr_t addr) {
    if ( addr >= heap_boundaries[HS_TENURED_SPACE][BOTTOM_ADDR] &&
         addr <  heap_boundaries[HS_TENURED_SPACE][END_ADDR] ) {
      return true;
      }
    return false;
  }

  bool is_perm(intptr_t addr) {
    if ( addr >= heap_boundaries[HS_PERM_SPACE][BOTTOM_ADDR] &&
         addr <  heap_boundaries[HS_PERM_SPACE][END_ADDR] ) {
      return true;
      }
    return false;
  }

  static int _klass_print_order;

#if 0
  void inc_live_objects()        { _live_objects++;    }
  void add_live_size(jint s)     { _live_size += s;    }
  void add_live_refs(jint r)     { _live_refs += r;    }
  void inc_hot_objects()         { _hot_objects++;     }
  void add_hot_size(jint s)      { _hot_size += s;     }
  void add_hot_refs(jint r)      { _hot_refs += r;     }
  void inc_missed_objects()      { _missed_objects++;  }
  void add_missed_size(jint s)   { _missed_size += s;  }
  void add_missed_refs(jint r)   { _missed_refs += r;  }

  jint live_objects()       { return _live_objects;    }
  jint live_size()          { return _live_size;       }
  jint live_refs()          { return _live_refs;       }
  jint hot_objects()        { return _hot_objects;     }
  jint hot_size()           { return _hot_size;        }
  jint hot_refs()           { return _hot_refs;        }
  jint missed_objects()     { return _missed_objects;  }
  jint missed_size()        { return _missed_size;     }
  jint missed_refs()        { return _missed_refs;     }
#endif

  bool allocation_failed() { return _buckets == NULL; }
};
#endif // PROFILE_OBJECT_ADDRESS_INFO
#endif // SHARE_VM_MEMORY_HEAPINSPECTION_HPP
