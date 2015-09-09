
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

enum heap_space {
  HS_INVALID_SPACE = -1,
  HS_EDEN_SPACE,
  HS_SURVIVOR_SPACE,
  HS_TENURED_SPACE,
  HS_PERM_SPACE,
  HS_NR_SPACES
};

#if defined (PROFILE_OBJECT_ADDRESS_INFO) or defined (PROFILE_OBJECT_INFO)
#define APP_TOTAL_REC -1
#define UNKNOWN_REC   -2
#define SUM_TOTAL_REC -3
#define PRE_GC_REC    -4
#define POST_GC_REC   -5
#define INVALID_REC   -6

class AllocPointInfoCollectionTask;
class ObjectAddressInfo;
class PersistentObjectInfo;

struct klass_record_record {
  int klass_id;
  jlong live_objects[NR_OBJECT_TYPES];
  jlong live_size[NR_OBJECT_TYPES];
  jlong live_refs[NR_OBJECT_TYPES];
  jlong hot_objects[NR_OBJECT_TYPES];
  jlong hot_size[NR_OBJECT_TYPES];
  jlong hot_refs[NR_OBJECT_TYPES];
  jlong new_objects[NR_OBJECT_TYPES];
  jlong new_size[NR_OBJECT_TYPES];
  jlong new_refs[NR_OBJECT_TYPES];
};

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

  jint     _klass_id;
  char*    _klass_name;

  int _nr_fields;
  GrowableArray<FieldRecord*>* _fields;

  enum klass_type _klass_type;
  int  _instance_size;

  jint _stats[HS_NR_SPACES][NR_OBJECT_TYPES][NR_KLASS_STATS];

 public:
  KlassRecord(char *name, int instance_size, klass_type ktype);
  GrowableArray<FieldRecord*>* fields()  { return _fields;            }

  jint klass_id()                        { return _klass_id;          }
  void set_klass_id(jint kid)            { _klass_id = kid;           }

  int instance_size()                    { return _instance_size;     }
  void set_instance_size(int new_size)   { _instance_size = new_size; }

  enum klass_type klass_type()           { return _klass_type;        }
  void set_klass_type(enum klass_type kt){ _klass_type = kt;          }

  char *klass_name()                     { return _klass_name;        }

  jint get_stat(enum heap_space hs, enum obj_type ot, enum klass_stat ks) {
    return _stats[hs][ot][ks];
  }

  jint add_stat(enum heap_space hs, enum obj_type ot, enum klass_stat ks, jint val) {
    return _stats[hs][ot][ks] += val;
  }

  jint inc_stat(enum heap_space hs, enum obj_type ot, enum klass_stat ks) {
    return _stats[hs][ot][ks] += 1;
  }

  jint get_type_stat(enum obj_type ot, enum klass_stat ks) {
    jint sum = 0;
    for (int i = 0 ; i < HS_NR_SPACES; i++) {
      sum += _stats[i][ot][ks];
    }
    return sum;
  }

  jint get_total_stat(enum klass_stat ks) {
    jint sum = 0;
    for (int i = 0 ; i < HS_NR_SPACES; i++) {
      for (int j = 0 ; j < NR_OBJECT_TYPES; j++) {
        sum += _stats[i][j][ks];
      }
    }
    return sum;
  }

  bool is_instance()  { return ((_klass_type == KT_VM_INSTANCE)  ||
                                (_klass_type == KT_APP_INSTANCE)); }
  bool is_array()     { return ((_klass_type == KT_VM_ARRAY)     ||
                                (_klass_type == KT_APP_ARRAY));    }

  void print_bin_on(FILE *binout, outputStream *textout);
  void print_map_on(outputStream *out);
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
  char *_key;
  KlassRecord *_value;
 public:
  KlassRecordEntry(const char *key, int instance_size, klass_type ktype,
                   KlassRecordEntry *next);
  ~KlassRecordEntry();
  char *key()                 { return _key;   }
  KlassRecordEntry *next()    { return _next;  }
  KlassRecord *value()        { return _value; }
  int compare(KlassRecordEntry* e1, KlassRecordEntry* e2);
  void print_bin_on(FILE *binout, outputStream *textout);
  void print_map_on(outputStream *out);
  void reset_ref_cnts();
  //void reset_klass_stats(bool do_all_spaces);
  void reset_klass_stats();
  void add_to_klass_totals(KlassRecordTable *krt);
};

class KlassRecordBucket : public CHeapObj<mtInternal> {
 private:
	KlassRecordEntry *_kres;
 public:
  KlassRecordBucket();
  ~KlassRecordBucket();
  void initialize() { _kres = NULL; }
  void empty();
  KlassRecordEntry* get_kre(const char *key);
  KlassRecordEntry* insert(const char *key, int instance_size,
                           klass_type ktype);
  void print_bin_on(FILE *binout, outputStream *textout);
  void print_map_on(outputStream *out);
  void reset_ref_cnts();
  //void reset_klass_stats(bool do_all_spaces);
  void reset_klass_stats();
  void accumulate_klass_totals(KlassRecordTable *krt);
};

class KlassRecordTable : public CHeapObj<mtInternal> {
 public:

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
    NR_KS_TOTALS
  };

  typedef jint (* kst_sum_fn) (enum klass_stat_total);

 private:
  KlassRecordBucket *_buckets;
  unsigned int _size;
  unsigned long hash(const char *key);
  static jint _klass_totals[NR_OBJECT_TYPES][NR_KS_TOTALS];
  static jint _cur_klass_id;

 public:
  KlassRecordTable (unsigned int klass_table_size);
  ~KlassRecordTable();
  KlassRecord *lookup(Klass* k);
  KlassRecord *insert(Klass* k, int instance_size, klass_type ktype);

  void print_bin_on(FILE *binout, outputStream *textout);
  void print_bin_table(FILE *binout, outputStream *textout);
  void print_bin_header(FILE *binout, outputStream *textout);
#if 0
  void print_header_table(FILE *binout, outputStream *textout,
    const char *title, jint (* kstf) (enum klass_stat_total));
#endif
  void print_header_table(FILE *binout, outputStream *textout,
    const char *title, kst_sum_fn kstf);
  void add_to_klass_totals(KlassRecord *kr);
  void compute_klass_stats();
  void print_klass_stats(outputStream *out);
  void reset_klass_cnts();
  void reset_klass_totals();
  void print_map_on(outputStream *out);
  static void get_klass_name(char *buf, Klass* klass);
  static void get_klass_size_type(Klass* klass, int instance_size,
    enum klass_type klass_type, int *isize, enum klass_type *ktype);

  static jint get_app_total(enum klass_stat_total kst) {
    return _klass_totals[APP_OBJECT][kst];
  }

  static jint get_sum_total(enum klass_stat_total kst) {
    jint sum = 0;
    for (int i = 0; i < NR_OBJECT_TYPES; i++) {
      sum += _klass_totals[i][kst];
    }
    return sum;
  }

  static jint get_unknown_total(enum klass_stat_total kst) {
    return (get_sum_total(kst) - get_app_total(kst));
  }

  static jint next_klass_id () {
    return _cur_klass_id++;
  }

  bool allocation_failed() { return _buckets == NULL; }
};

struct apinfo_record {
  int id;
  jlong total_objects;
  jlong total_size;
  jlong hot_objects;
  jlong hot_size;
  jlong new_objects;
  jlong new_size;
  jlong ref_cnt;
};

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
  KlassRecord *_klass_record;

  jlong _total_size;
  jlong _total_objects;

  jlong _tot_load_cnt;
  jlong _tot_store_cnt;
  jlong _tot_init_cnt;

  jlong _val_size;
  jlong _val_objects;

  jlong _val_load_cnt;
  jlong _val_store_cnt;
  jlong _val_init_cnt;

  jlong _val_hot_size;
  jlong _val_hot_objects;

  jlong _val_new_size;
  jlong _val_new_objects;

 public:
  AllocPointInfo(jint id, HeapColor color=HC_NOT_COLORED) :
    _id(id), _color(color), _klass_record(NULL) {
    _total_size    = 0;  
    _total_objects = 0;  
    _tot_load_cnt  = 0;
    _tot_store_cnt = 0;
    _tot_init_cnt  = 0;
    reset_val_cnts();
  }

  unsigned int id()                  { return _id;              }
  HeapColor color()                  { return _color;           }

  jlong total_objects   ()           { return _total_objects;   }
  jlong total_size      ()           { return _total_size;      }

  jlong tot_load_cnt    ()           { return _tot_load_cnt;    }
  jlong tot_store_cnt   ()           { return _tot_store_cnt;   }
  jlong tot_init_cnt    ()           { return _tot_init_cnt;    }

  jlong val_load_cnt    ()           { return _val_load_cnt;    }
  jlong val_store_cnt   ()           { return _val_store_cnt;   }
  jlong val_init_cnt    ()           { return _val_init_cnt;    }

  jlong val_objects     ()           { return _val_objects;     }
  jlong val_size        ()           { return _val_size;        }

  jlong val_hot_objects ()           { return _val_hot_objects; }
  jlong val_hot_size    ()           { return _val_hot_size;    }

  jlong val_new_objects ()           { return _val_new_objects; }
  jlong val_new_size    ()           { return _val_new_size;    }

#ifdef PROFILE_OBJECT_ADDRESS_INFO
  void add_to_val(ObjectAddressInfo *oai);
#endif
#ifdef PROFILE_OBJECT_INFO
  void add_to_val(PersistentObjectInfo *poi);
#endif
  void mark_new_object(int size);

  void reset_val_cnts();
  void reset_ref_cnts();
  void add_val_to_totals(AllocPointInfoTable *apit);

  KlassRecord *klass_record()            { return _klass_record; }
  void set_klass_record(KlassRecord *kr) { _klass_record = kr;   }
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
  void print_val_info(FILE *binout, outputStream *textout);
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
  void print_val_info(FILE *binout, outputStream *textout);
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
  jlong _val_total_init_cnt;

 public:
  AllocPointInfoTable (unsigned int capacity);
  ~AllocPointInfoTable();

  enum {
    apit_size = 8192
  };

  AllocPointInfo *get(Method* ap_method, int ap_bci,
    HeapColor color=HC_NOT_COLORED);

  AllocPointInfo *unknown_alloc_point() {
    return get(NULL, -1);
  }
  void print_map_on(outputStream *out);
  void print_val_info(FILE *binout, outputStream *textout);
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
  void add_val_total_init_cnt(jlong v)    { _val_total_init_cnt += v;    }

  jlong val_total_objects()     { return _val_total_objects;     }
  jlong val_total_size()        { return _val_total_size;        }

  bool allocation_failed() { return _buckets == NULL; }
};
#endif /* PROFILE_OBJECT_ADDRESS_INFO or PROFILE_OBJECT_INFO */

#ifdef PROFILE_OBJECT_INFO
class ObjectInfoCollectionTask;

class ObjectInfoCollection : public AllStatic
{
  friend class ObjectInfoCollectionTask;
  friend class ObjectLayout;
private:
  static ObjectInfoCollectionTask* _task;
  static bool _collecting;
  static bool _exiting;
  static jlong _val_start;
  static jint  _tracker_val;
  
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
  Klass*           _klass;
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
  ObjectInfoEntry* lookup(const Klass* k);
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
  uint hash(Klass* p);
  ObjectInfoEntry* lookup(const Klass* k);
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

class PersistentObjectInfo : public CHeapObj<mtInternal> {
  friend class PersistentObjectInfoEntry;
 private:
  //jint _id, _is_new, _mark;
  jint _id, _mark;
  intptr_t _addr;
  int _size, _cval;
  AllocPointInfo *_alloc_point;
  HeapColor _color;

  jint _colored_load_cnt[HC_TOTAL];
  jint _colored_store_cnt[HC_TOTAL];
  jint _colored_init_cnt[HC_TOTAL];
  jint _val_load_cnt;
  jint _val_store_cnt;
  jint _val_init_cnt;
  jint _tot_load_cnt;
  jint _tot_store_cnt;
  jint _tot_init_cnt;

 public:
  PersistentObjectInfo(jint id, int cval, int size, AllocPointInfo *alloc_point,
    HeapColor color, intptr_t addr) :
    _id(id), _cval(cval), _size(size), _alloc_point(alloc_point),
    _color(color), _addr(addr) {
    _colored_load_cnt[HC_RED]   = 0;
    _colored_load_cnt[HC_BLUE]  = 0;
    _colored_store_cnt[HC_RED]  = 0;
    _colored_store_cnt[HC_BLUE] = 0; 
    _colored_init_cnt[HC_RED]   = 0;
    _colored_init_cnt[HC_BLUE]  = 0;
    _val_load_cnt               = 0;
    _val_store_cnt              = 0;
    _val_init_cnt               = 0;
    _tot_load_cnt               = 0;
    _tot_store_cnt              = 0;
    _tot_init_cnt               = 0;
    _mark                       = 0;
  }

  jint id()                     { return _id;           }
  int cval()                    { return _cval;         }
  int size()                    { return _size;         }

  AllocPointInfo *alloc_point()             { return _alloc_point;  }
  void set_alloc_point(AllocPointInfo *api) { _alloc_point = api;   }

  HeapColor color()          { return _color;        }
  intptr_t addr()            { return _addr;         }

  jint val_load_cnt  ()      { return _val_load_cnt;  }
  jint val_store_cnt ()      { return _val_store_cnt; }
  jint val_init_cnt  ()      { return _val_init_cnt;  }

  jint tot_load_cnt  ()      { return _tot_load_cnt;  }
  jint tot_store_cnt ()      { return _tot_store_cnt; }
  jint tot_init_cnt  ()      { return _tot_init_cnt;  }

  jint colored_load_cnt  (HeapColor color) { return _colored_load_cnt[color];  }
  jint colored_store_cnt (HeapColor color) { return _colored_store_cnt[color]; }
  jint colored_init_cnt  (HeapColor color) { return _colored_init_cnt[color];  }

  jint mark ()               { return _mark;               }
  void set_mark ()           { Atomic::store(1, &_mark);   }
  void reset_mark ()         { Atomic::store(0, &_mark);   }

#if 0
  bool is_new ()             { return _is_new;             }
  void reset_is_new ()       { Atomic::store(0, &_is_new); }
#endif

  void reset_val_load_cnt()  { Atomic::store(0, &_val_load_cnt);  }
  void reset_val_store_cnt() { Atomic::store(0, &_val_store_cnt); }
  void reset_val_init_cnt()  { Atomic::store(0, &_val_init_cnt);  }

  void mark_load();
  void mark_store();
  void mark_alloc();

  void batch_mark_load(int n);
  void batch_mark_store(int n);
};

class PersistentObjectInfoEntry : public CHeapObj<mtInternal> {
  friend class PersistentObjectInfoTable;
 private:
  PersistentObjectInfoEntry* _next;
  Klass*           _klass;
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
  PersistentObjectInfoEntry* lookup(const Klass* k);
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
  PersistentObjectInfoEntry* lookup(const Klass* k);

   public:
  // Table size
  enum {
    oit_size = 20011
  };
  PersistentObjectInfoTable(int size, HeapWord* ref);
  ~PersistentObjectInfoTable();
  PersistentObjectInfo* append_instance(const oop obj, int size, Klass* klass);
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
  static jint _tracker_val;
  
public: 
  static bool collecting() { return _collecting; }
  static void engage();
  static void disengage();

  static ObjectAddressInfoCollectionTask* task() { return _task; }
  static bool is_active() { return _task != NULL; }

  static void initialize();
  static void destroy();

  static void collect_object_address_info(outputStream *addrinfo_log,
    outputStream *krinfo_log, const char *reason);
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

class ObjectAddressInfo : public CHeapObj<mtInternal> {
 private:
  intptr_t _addr;
  int _size;
  jint _load_cnt;
  jint _store_cnt;
  jint _init_cnt;
  enum obj_type _type;
  KlassRecord *_klass_record;
  AllocPointInfo *_alloc_point;
 public:

  ObjectAddressInfo(oop obj, int obj_size, KlassRecord *kr,
    AllocPointInfo *api, obj_type type, bool old_rec) :
    _addr((intptr_t)obj), _size(obj_size) {
    _load_cnt     = 0;
    _store_cnt    = 0;
    _init_cnt     = old_rec ? OLD_MARKER : NEW_MARKER;
    _type         = type;
    _klass_record = kr;
    _alloc_point  = api;
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
  void add_init_cnt(int n)   { Atomic::add(n, &_init_cnt);   }
  void set_init_cnt(int n)   { Atomic::store(n, &_init_cnt); }

  KlassRecord *klass_record()            { return _klass_record; }
  void set_klass_record(KlassRecord *kr) { _klass_record = kr;   }

  AllocPointInfo *alloc_point()             { return _alloc_point; }
  void set_alloc_point(AllocPointInfo *api) { _alloc_point = api;   }

  enum obj_type type()              { return _type; }
  void set_type(enum obj_type type) { _type = type; }

  void add_to_object_totals(ObjectAddressInfoTable *oait);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER) {
    _load_cnt  = 0;
    _store_cnt = 0;
    _init_cnt  = im;
  }

  void print_oai_on(outputStream *out) {
    out->print_cr("oai: %"PRIdPTR" size: %d refs: %d init: %d type: %d kr: %p ap: %p",
      _addr, _size, (_init_cnt < 0 ? _load_cnt+_store_cnt : _load_cnt+_store_cnt+_init_cnt),
      _init_cnt, _type, _klass_record, _alloc_point);
  }
};

class ObjectAddressInfoEntry : public CHeapObj<mtInternal> {
 private:
  ObjectAddressInfoEntry *_next;
  oop _key;
  ObjectAddressInfo *_value;
 public:
  ObjectAddressInfoEntry(oop o, int obj_size, KlassRecord *kr,
    AllocPointInfo *api, obj_type type, ObjectAddressInfoEntry *next,
    bool old_rec=false);
  ~ObjectAddressInfoEntry();
  oop key()                         { return _key;   }
  ObjectAddressInfoEntry *next()    { return _next;  }
  ObjectAddressInfo *value()        { return _value; }
  void print_bin_on(FILE *binout, outputStream *out);
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
                                 AllocPointInfo *api, obj_type type,
                                 bool old_rec=false);
  void print_bin_on(FILE *binout, outputStream *out);
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

 private:
	ObjectAddressInfoBucket *_buckets;
  AllocPointInfoTable *_apit;
  KlassRecordTable *_krt;
  unsigned long hash(oop obj);
  unsigned int _size, _cur_val;
  bool _using_to_space;

  intptr_t heap_boundaries[HS_NR_SPACES][NR_MARKS];
  jint heap_stats[HS_NR_SPACES][NR_HEAP_STATS];
 
 public:
  ObjectAddressInfoTable (unsigned int obj_table_size,
    KlassRecordTable *krt, AllocPointInfoTable *apit);
  ~ObjectAddressInfoTable();

  ObjectAddressInfo *insert(oop obj);
  ObjectAddressInfo *insert(oop obj, int size, Klass* klass);
  ObjectAddressInfo *insert(oop obj, int obj_size, KlassRecord *klass,
                            AllocPointInfo *alloc_point, obj_type type,
                            bool old_rec=false);
  ObjectAddressInfo *lookup(oop obj);

  ObjectAddressInfo *mark_alloc(oop obj, Method *method, int bci);
  ObjectAddressInfo *mark_filler(oop obj, int size);
  void mark_load(oop obj);
  void mark_store(oop obj);
  void mark_load(oop obj, intptr_t field, int size);
  void mark_store(oop obj, intptr_t field, int size);
  void batch_mark_load(oop obj, int n);
  void batch_mark_store(oop obj, int n);
  void reset_ref_cnts(enum init_marker im=OLD_MARKER);
  void print_table(FILE* binout, outputStream *textout);
  void print_header(outputStream *textout, const char *reason);
  void print_bin_on(FILE *binout, outputStream *textout, const char *reason);
  enum heap_space get_space(intptr_t addr);
  void compute_heap_stats();
  void add_to_heap_stats(ObjectAddressInfo *oai);
  void reset_heap_stats();
  void record_heap_boundaries();
  void print_heap_stats(outputStream *textout, const char *space,
    enum heap_space hs);
  unsigned int cur_val()           { return _cur_val; }
  void inc_cur_val()               { _cur_val++;      }
  void set_cur_val(unsigned int v) { _cur_val = v;    }
  void reset_spaces(bool do_all_spaces);
  void copy_to(ObjectAddressInfoTable *dst_oait);

  bool using_to_space()   { return _using_to_space;         }
  bool using_from_space() { return !_using_to_space;        }
  void use_to_space()     { _using_to_space = true;  record_heap_boundaries(); }
  void use_from_space()   { _using_to_space = false; record_heap_boundaries(); }

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

  bool allocation_failed() { return _buckets == NULL; }
};
#endif // PROFILE_OBJECT_ADDRESS_INFO
#endif /* SHARE_VM_MEMORY_PROFILEOBJECTINFO_HPP */

