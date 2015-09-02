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

#ifndef SHARE_VM_GC_PARALLEL_PSSCAVENGE_HPP
#define SHARE_VM_GC_PARALLEL_PSSCAVENGE_HPP

#include "gc/parallel/cardTableExtension.hpp"
#include "gc/parallel/psVirtualspace.hpp"
#include "gc/shared/collectorCounters.hpp"
#include "gc/shared/gcTrace.hpp"
#include "memory/allocation.hpp"
#include "oops/oop.hpp"
#include "utilities/stack.hpp"
#ifdef PROFILE_OBJECT_INFO
#include "memory/heapInspection.hpp"
#endif

class GCTaskManager;
class GCTaskQueue;
class OopStack;
class ReferenceProcessor;
class ParallelScavengeHeap;
class ParallelScavengeTracer;
class PSIsAliveClosure;
class PSRefProcTaskExecutor;
class STWGCTimer;

class PSScavenge: AllStatic {
  friend class PSIsAliveClosure;
  friend class PSKeepAliveClosure;
  friend class PSPromotionManager;

 enum ScavengeSkippedCause {
   not_skipped = 0,
   to_space_not_empty,
   promoted_too_large,
   full_follows_scavenge
 };

  // Saved value of to_space->top(), used to prevent objects in to_space from
  // being rescanned.
  static HeapWord* _to_space_top_before_gc;
#if 0
#ifdef COLORED_EDEN_SPACE
  static HeapWord* _eden_colored_space_top[HC_TOTAL];
  static HeapWord* _eden_colored_space_bottom[HC_TOTAL];
#else
  static HeapWord* _eden_space_top;
  static HeapWord* _eden_space_bottom;
#endif
#else
  static HeapWord* _eden_colored_space_top[HC_TOTAL];
  static HeapWord* _eden_colored_space_bottom[HC_TOTAL];
#endif
  static HeapWord* _from_colored_space_top[HC_TOTAL];
  static HeapWord* _from_colored_space_bottom[HC_TOTAL];
#if 0
  static HeapWord* _to_colored_space_top[HC_TOTAL];
  static HeapWord* _to_colored_space_bottom[HC_TOTAL];
#endif

  // Number of consecutive attempts to scavenge that were skipped
  static int                _consecutive_skipped_scavenges;


 protected:
  // Flags/counters
  static ReferenceProcessor*  _ref_processor;        // Reference processor for scavenging.
  static PSIsAliveClosure     _is_alive_closure;     // Closure used for reference processing
  static CardTableExtension*  _card_table;           // We cache the card table for fast access.
  static bool                 _survivor_overflow;    // Overflow this collection
  static uint                 _tenuring_threshold;   // tenuring threshold for next scavenge
  static elapsedTimer         _accumulated_time;     // total time spent on scavenge
  static STWGCTimer           _gc_timer;             // GC time book keeper
  static ParallelScavengeTracer _gc_tracer;          // GC tracing
  // The lowest address possible for the young_gen.
  // This is used to decide if an oop should be scavenged,
  // cards should be marked, etc.
  static HeapWord*            _young_generation_boundary;
  // Used to optimize compressed oops young gen boundary checking.
  static uintptr_t            _young_generation_boundary_compressed;
  static Stack<markOop, mtGC> _preserved_mark_stack; // List of marks to be restored after failed promotion
  static Stack<oop, mtGC>     _preserved_oop_stack;  // List of oops that need their mark restored.
  static CollectorCounters*   _counters;             // collector performance counters

  static void clean_up_failed_promotion();

  static bool should_attempt_scavenge();

  static HeapWord* to_space_top_before_gc() { return _to_space_top_before_gc; }
  static inline void save_to_space_top_before_gc();
  static inline void save_from_colored_space_bounds();
#if 0
  static inline void save_to_colored_space_bounds();
#endif
#if 0
#ifdef COLORED_EDEN_SPACE
  static inline void save_eden_colored_space_bounds();
#else
  static inline void save_eden_space_bounds();
#endif
#else
  static inline void save_eden_colored_space_bounds();
#endif

  // Private accessors
  static CardTableExtension* const card_table()       { assert(_card_table != NULL, "Sanity"); return _card_table; }

#ifdef PROFILE_OBJECT_INFO
  static unsigned long _live_objects[2][HC_ENUM_TOTAL];
  static unsigned long _live_size[2][HC_ENUM_TOTAL];
  static unsigned long _live_refs[2][HC_ENUM_TOTAL];
  static unsigned long _hot_objects[2][HC_ENUM_TOTAL];
  static unsigned long _hot_size[2][HC_ENUM_TOTAL];
  static unsigned long _hot_refs[2][HC_ENUM_TOTAL];
#endif

  static const ParallelScavengeTracer* gc_tracer() { return &_gc_tracer; }

 public:
  // Accessors
  static uint             tenuring_threshold()  { return _tenuring_threshold; }
  static elapsedTimer*    accumulated_time()    { return &_accumulated_time; }
  static int              consecutive_skipped_scavenges()
    { return _consecutive_skipped_scavenges; }

  static HeapWord* my_young_gen_boundary()     { return _young_generation_boundary; }
  static HeapWord* my_to_space_top_before_gc() { return _to_space_top_before_gc; }

  // Performance Counters
  static CollectorCounters* counters()           { return _counters; }

  // Used by scavenge_contents && psMarkSweep
  static ReferenceProcessor* const reference_processor() {
    assert(_ref_processor != NULL, "Sanity");
    return _ref_processor;
  }
  // Used to add tasks
  static GCTaskManager* const gc_task_manager();
  // The promotion managers tell us if they encountered overflow
  static void set_survivor_overflow(bool state) {
    _survivor_overflow = state;
  }
  // Adaptive size policy support.  When the young generation/old generation
  // boundary moves, _young_generation_boundary must be reset
  static void set_young_generation_boundary(HeapWord* v) {
    _young_generation_boundary = v;
    if (UseCompressedOops) {
      _young_generation_boundary_compressed = (uintptr_t)oopDesc::encode_heap_oop((oop)v);
    }
  }

  // Called by parallelScavengeHeap to init the tenuring threshold
  static void initialize();

  // Scavenge entry point.  This may invoke a full gc; return true if so.
  static bool invoke();
  // Return true if a collection was done; false otherwise.
  static bool invoke_no_policy();

  // If an attempt to promote fails, this method is invoked
  static void oop_promotion_failed(oop obj, markOop obj_mark);

  template <class T> static inline bool should_scavenge(T* p, bool safe);
  template <class T> static inline bool should_scavenge(T* p);

  // These call should_scavenge() above and, if it returns true, also check that
  // the object was not newly copied into to_space.  The version with the bool
  // argument is a convenience wrapper that fetches the to_space pointer from
  // the heap and calls the other version (if the arg is true).
  template <class T> static inline bool should_scavenge(T* p, bool safe, MutableSpace* to_space);
  template <class T> static inline bool should_scavenge(T* p, bool safe, bool check_to_space);

  static void copy_and_push_safe_barrier_from_klass(PSPromotionManager* pm, oop* p);

  static inline HeapColor get_survivor_color(PSPromotionManager* pm, HeapWord* obj);
  static inline HeapColor get_current_color(HeapWord* obj);

  // Is an object in the young generation
  // This assumes that the 'o' is in the heap,
  // so it only checks one side of the complete predicate.

  inline static bool is_obj_in_young(oop o) {
    return (HeapWord*)o >= _young_generation_boundary;
  }

  inline static bool is_obj_in_young(narrowOop o) {
    return (uintptr_t)o >= _young_generation_boundary_compressed;
  }

  inline static bool is_obj_in_young(HeapWord* o) {
    return o >= _young_generation_boundary;
  }

#ifdef PROFILE_OBJECT_INFO
  inline static PersistentObjectInfo *obj_poi(oop obj) {
    PersistentObjectInfo *poi = NULL;
    if (obj->is_instance()) {
      poi = ((instanceOop)obj)->poi();
    } else if (obj->is_array()) {
      poi = ((arrayOop)obj)->poi();
    }
    return poi;
  }

  inline static void obj_set_poi(oop obj, PersistentObjectInfo *poi) {
    if (obj->is_instance()) {
      ((instanceOop)obj)->set_poi(poi);
    } else if (obj->is_array()) {
      ((arrayOop)obj)->set_poi(poi);
    }
  }

  inline static bool obj_is_initialized(oop obj) {
    if (obj->is_instance()) {
      return ((instanceOop)obj)->is_initialized();
    } else if (obj->is_array()) {
      return ((arrayOop)obj)->is_initialized();
    }
    return false;
  }

  static inline jint obj_refs(oop obj) {
    return (obj_poi(obj)->val_load_cnt() + obj_poi(obj)->val_store_cnt());
  }

  inline static bool obj_is_hot(oop obj) {
    jint refcnt = obj_refs(obj);
    return (refcnt > HotObjectThreshold);
  }

  inline static jint obj_id(oop obj) {
    jint id = 0;
    if (obj->is_instance()) {
      id = ((instanceOop)obj)->id();
    } else if (obj->is_array()) {
      id = ((arrayOop)obj)->id();
    }
    return id;
  }
#endif
  
  inline static bool eden_colored_space_contains(HeapColor color, HeapWord* o) {
    return (_eden_colored_space_bottom[color] <= o &&
            _eden_colored_space_top[color] > o);
  }
  inline static bool from_colored_space_contains(HeapColor color, HeapWord* o) {
    return (_from_colored_space_bottom[color] <= o &&
            _from_colored_space_top[color] > o);
  }

#ifdef PROFILE_OBJECT_INFO
  inline static void profile_object_copy(oop obj, HeapColor to_color, bool forwarded);

  inline static void reset_object_copy_profile() {
    int g,i;
    //objinfo_log->print_cr("resetting object copy profile");
    for (g=0; g < PERM_GEN; g++) {
      for (i=0; i < HC_ENUM_TOTAL; i++) {
        _live_objects[g][i] = 0;
        _live_size[g][i]    = 0;
        _live_refs[g][i]    = 0;
        _hot_objects[g][i]  = 0;
        _hot_size[g][i]     = 0;
        _hot_refs[g][i]     = 0;
      }
    }
  }

  inline static unsigned long live_objects(PSGenType gen, HeapColorEnum hce) {
    return _live_objects[gen][hce];
  }
  inline static unsigned long live_size(PSGenType gen, HeapColorEnum hce) {
    return _live_size[gen][hce];
  }
  inline static unsigned long live_refs(PSGenType gen, HeapColorEnum hce) {
    return _live_refs[gen][hce];
  }
  inline static unsigned long hot_objects(PSGenType gen, HeapColorEnum hce) {
    return _hot_objects[gen][hce];
  }
  inline static unsigned long hot_size(PSGenType gen, HeapColorEnum hce) {
    return _hot_size[gen][hce];
  }
  inline static unsigned long hot_refs(PSGenType gen, HeapColorEnum hce) {
    return _hot_refs[gen][hce];
  }
#endif
};

#endif // SHARE_VM_GC_PARALLEL_PSSCAVENGE_HPP
