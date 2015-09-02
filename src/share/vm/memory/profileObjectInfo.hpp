
#ifndef SHARE_VM_MEMORY_PROFILEOBJECTINFO_HPP
#define SHARE_VM_MEMORY_PROFILEOBJECTINFO_HPP

#include "memory/allocation.hpp"
#include "memory/allocation.inline.hpp"
#include "oops/objArrayOop.hpp"
#include "oops/oop.hpp"
#include "oops/oop.inline.hpp"
#include "oops/annotations.hpp"
#include "utilities/macros.hpp"
#include "gc/parallel/mutableSpace.hpp"

// ObjectInfoCollection inherits PeriodicTask 
#include "runtime/task.hpp"

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

class ObjectInfoList : public CHeapObj<mtInternal> {
  friend class ListAppendInstanceClosure;
 private:
  GrowableArray<oop>* _elements;
  GrowableArray<oop>* elements() const { return _elements; }
 public:
  ObjectInfoList(int initial_size);
  void print_oop_info(outputStream *st, oop cur_oop) const;
  void print_on(outputStream* st) const;
};

class ObjectInfoEntry : public CHeapObj<mtInternal> {
  friend class ObjectInfoTable;
 private:
  ObjectInfoEntry* _next;
  Klass*         _klass;
  long             _instance_count;
  size_t           _instance_words;
  GrowableArray<oop>* _elements;
  GrowableArray<oop>* elements() const { return _elements; }

 public:

  ObjectInfoEntry(Klass* k, ObjectInfoEntry* next);
  ObjectInfoEntry* next()  { return _next; }

  bool is_equal(Klass* k)     { return k == _klass; }
  Klass* klass()              { return _klass; }
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

class ObjectInfoBucket: public CHeapObj<mtInternal> {
 private:
  ObjectInfoEntry* _list;
  ObjectInfoEntry* list()           { return _list; }
  void set_list(ObjectInfoEntry* l) { _list = l; }
 public:
  ObjectInfoEntry* lookup(Klass* k);
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
  uint hash(const Klass* p);
  ObjectInfoEntry* lookup(Klass* k);
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


class AllocPointInfo : public CHeapObj<mtInternal> {
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


class AllocPointInfoEntry : public CHeapObj<mtInternal> {
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

class AllocPointInfoBucket : public CHeapObj<mtInternal> {
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

class AllocPointInfoTable : public CHeapObj<mtInternal> {
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

  AllocPointInfo *get(Method *ap_method, int ap_bci,
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

class PersistentObjectInfo : public CHeapObj<mtInternal> {
  friend class PersistentObjectInfoEntry;
 private:
  jint _id, _is_new, _mark;
  intptr_t _addr;
  int _size, _cval;
  AllocPointInfo *_alloc_point;
  HeapColor _color;

  jint _colored_load_cnt[HC_TOTAL];
  jint _colored_store_cnt[HC_TOTAL];
  jint _val_load_cnt;
  jint _val_store_cnt;
  jint _tot_load_cnt;
  jint _tot_store_cnt;

 public:
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

class PersistentObjectInfoEntry : public CHeapObj<mtInternal> {
  friend class PersistentObjectInfoTable;
 private:
  PersistentObjectInfoEntry* _next;
  Klass*         _klass;
  long             _instance_count;
  size_t           _instance_words;
  char*            _klass_name;
  size_t           _klass_name_len;
  GrowableArray<PersistentObjectInfo*>* _elements;
  GrowableArray<PersistentObjectInfo*>* elements() const { return _elements; }

 public:

  PersistentObjectInfoEntry(Klass* k, PersistentObjectInfoEntry* next);
  PersistentObjectInfoEntry* next()  { return _next; }

  bool is_equal(Klass* k)     { return k == _klass; }
  Klass* klass()              { return _klass; }
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

class PersistentObjectInfoClosure: public CHeapObj<mtInternal> {
 public:
  // Called for each PersistentObjectInfoEntry.
  virtual void do_cinfo(PersistentObjectInfoEntry* koie) = 0;
};

class PersistentObjectInfoBucket: public CHeapObj<mtInternal> {
 private:
  PersistentObjectInfoEntry* _list;
  PersistentObjectInfoEntry* list()           { return _list; }
  void set_list(PersistentObjectInfoEntry* l) { _list = l; }
 public:
  PersistentObjectInfoEntry* lookup(Klass* k);
  void initialize() { _list = NULL; }
  void empty();
  void iterate(PersistentObjectInfoClosure* cic);
};

class PersistentObjectInfoTable: public CHeapObj<mtInternal> {
 private:
  int _size;
  int _cur_val;
  static bool _printing;

  // An aligned reference address (typically the least
  // address in the perm gen) used for hashing klass
  // objects.
  HeapWord* _ref;

  PersistentObjectInfoBucket* _buckets;
  uint hash(Klass* p);
  PersistentObjectInfoEntry* lookup(Klass* k);

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
    if (obj->is_instance()) {
      instanceOop inst_oop = (instanceOop)obj;
      if (inst_oop->is_initialized()) {
        if (!(_live_space->contains(inst_oop->forwardee()) ||
              _tenured_space->contains(inst_oop->forwardee())) ) {
          deadobj_log->print_cr("  " INT32_FORMAT_W(12), inst_oop->id());
        }
      }
    } else if (obj->is_array()) {
      arrayOop arr_oop = (arrayOop)obj;
      if (arr_oop->is_initialized()) {
        if (!(_live_space->contains(arr_oop->forwardee()) ||
              _tenured_space->contains(arr_oop->forwardee())) ) {
          deadobj_log->print_cr("  " INT32_FORMAT_W(12), arr_oop->id());
        }
      }
    }
  }
};

class MajorDeadInstanceClosure : public ObjectClosure {
 public:
  MajorDeadInstanceClosure() {}
  void do_object(oop obj) {
    if (obj->is_instance()) {
      instanceOop inst_oop = (instanceOop)obj;
      if (inst_oop->is_initialized()) {
        if (!inst_oop->is_gc_marked()) {
          deadobj_log->print_cr("  " INT32_FORMAT_W(12), inst_oop->id());
        }
      }
    } else if (obj->is_array()) {
      arrayOop arr_oop = (arrayOop)obj;
      if (arr_oop->is_initialized()) {
        if (!arr_oop->is_gc_marked()) {
          deadobj_log->print_cr("  " INT32_FORMAT_W(12), arr_oop->id());
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

class FieldRecord : public CHeapObj<mtInternal> {
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

class KlassRecord : public CHeapObj<mtInternal> {

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

  Klass* _klass;
  char*    _klass_name;
  size_t   _klass_name_len;

  int _nr_fields;
  GrowableArray<FieldRecord*>* _fields;

  enum klass_type _klass_type;
  int  _instance_size;

  jint _stats[HS_NR_SPACES][NR_KLASS_STATS];

 public:
  KlassRecord(Klass* k, int instance_size, klass_type ktype);
  GrowableArray<FieldRecord*>* fields()  { return _fields;            }

  int instance_size()                    { return _instance_size;     }
  void set_instance_size(int new_size)   { _instance_size = new_size; }

  enum klass_type klass_type()           { return _klass_type;        }
  void set_klass_type(enum klass_type kt){ _klass_type = kt;          }

  Klass* klass()                       { return _klass;             }
  bool is_equal(Klass* k)              { return k == _klass;        }

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
  
  using AllocatedObj::print_on;
  void print_on(outputStream* st);
  void reset_ref_cnts();
  void reset_klass_stats();
  void add_to_klass_stats(ObjectAddressInfo *oai, enum heap_space hs);

  //FieldRecord *find_field_record(int field_offset, int size);
  void mark_field_load (int field_offset, int size);
  void mark_field_store(int field_offset, int size);
};

class KlassRecordEntry : public CHeapObj<mtInternal> {
 private:
  KlassRecordEntry *_next;
  Klass* _key;
  KlassRecord *_value;
 public:
  KlassRecordEntry(Klass* o, int instance_size, klass_type ktype,
                   KlassRecordEntry *next);
  ~KlassRecordEntry();
  Klass* key()              { return _key;   }
  KlassRecordEntry *next()    { return _next;  }
  KlassRecord *value()        { return _value; }
  int compare(KlassRecordEntry* e1, KlassRecordEntry* e2);

  using AllocatedObj::print_on;
  void print_on(outputStream *out);
  void reset_ref_cnts();
  //void reset_klass_stats(bool do_all_spaces);
  void reset_klass_stats();
  void add_to_klass_totals(ObjectAddressInfoTable *oait);
};

class KlassRecordBucket : public CHeapObj<mtInternal> {
 private:
	KlassRecordEntry *_kres;
 public:
  KlassRecordBucket();
  ~KlassRecordBucket();
  void initialize() { _kres = NULL; }
  void empty();
  void empty_spaces(bool do_all_spaces, ObjectAddressInfoTable *oait);
  void copy_entries_to(ObjectAddressInfoTable *dst_oait);
  KlassRecordEntry* get_kre(Klass* obj);
  KlassRecordEntry* kt_insert(Klass* obj, int instance_size, klass_type ktype);

  using AllocatedObj::print_on;
  void print_on(outputStream *out);
  void reset_ref_cnts();
  //void reset_klass_stats(bool do_all_spaces);
  void reset_klass_stats();
  void accumulate_klass_totals(ObjectAddressInfoTable *oait);
};

class ObjectAddressInfo : public CHeapObj<mtInternal> {
 private:
  intptr_t _addr;
  int _size;
  jint _load_cnt;
  jint _store_cnt;
  jint _init_cnt;
  enum obj_type _type;
  KlassRecord *_klass_record;
 public:
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
  jint add_init_cnt(int n)   { Atomic::add(n, &_init_cnt); return 1; }
  jint set_init_cnt(int n)   { Atomic::store(n, &_init_cnt); return 1; }

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

class ObjectAddressInfoEntry : public CHeapObj<mtInternal> {
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

  using AllocatedObj::print_on;
  void print_on(outputStream *out);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER);
  //void reset_ref_cnts();
  void add_to_heap_stats(ObjectAddressInfoTable *oait);
  void set_next(ObjectAddressInfoEntry *ne) { _next = ne; }
};

class ObjectAddressInfoBucket : public CHeapObj<mtInternal> {
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

  using AllocatedObj::print_on;
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

class ObjectAddressInfoTable : public CHeapObj<mtInternal> {
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
  ObjectAddressInfo *insert(oop obj, int obj_size, Klass* klass,
                            obj_type type, bool old_rec=false);
  ObjectAddressInfo *lookup(oop obj);
  KlassRecord *kt_lookup(Klass* k);
  KlassRecord *kt_insert(Klass* k, int instance_size, klass_type ktype);

  ObjectAddressInfo *mark_alloc(oop obj);
  ObjectAddressInfo *mark_filler(oop obj, int size);
  void mark_load(oop obj);
  void mark_store(oop obj);
  void mark_load(oop obj, intptr_t field, int size);
  void mark_store(oop obj, intptr_t field, int size);
  void batch_mark_load(oop obj, int n);
  void batch_mark_store(oop obj, int n);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER);
  void print_table(outputStream *out);
  void print_header(outputStream *out, const char *reason);
  void print_klass_table(outputStream *out);
  void print_klass_header(outputStream *out, const char *reason);

  using AllocatedObj::print_on;
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
  bool use_to_space()     { _using_to_space = true;  record_heap_boundaries(); return true; }
  bool use_from_space()   { _using_to_space = false; record_heap_boundaries(); return true; }

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

#endif /* PROFILE_OBJECT_ADDRESS_INFO */
#endif /* SHARE_VM_MEMORY_PROFILEOBJECTINFO_HPP */

