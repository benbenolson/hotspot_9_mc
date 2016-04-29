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

#ifndef SHARE_VM_GC_PARALLEL_PSPROMOTIONMANAGER_HPP
#define SHARE_VM_GC_PARALLEL_PSPROMOTIONMANAGER_HPP

#include "gc/parallel/psPromotionLAB.hpp"
#include "gc/shared/copyFailedInfo.hpp"
#include "gc/shared/gcTrace.hpp"
#include "gc/shared/taskqueue.hpp"
#include "memory/allocation.hpp"
#include "memory/padded.hpp"
#include "utilities/globalDefinitions.hpp"

//
// psPromotionManager is used by a single thread to manage object survival
// during a scavenge. The promotion manager contains thread local data only.
//
// NOTE! Be careful when allocating the stacks on cheap. If you are going
// to use a promotion manager in more than one thread, the stacks MUST be
// on cheap. This can lead to memory leaks, though, as they are not auto
// deallocated.
//
// FIX ME FIX ME Add a destructor, and don't rely on the user to drain/flush/deallocate!
//

class MutableSpace;
class PSOldGen;
class ParCompactionManager;

class PSPromotionManager VALUE_OBJ_CLASS_SPEC {
  friend class PSScavenge;
  friend class PSRefProcTaskExecutor;
 private:
  static PaddedEnd<PSPromotionManager>* _manager_array;
  static OopStarTaskQueueSet*           _stack_array_depth;
  static PSOldGen*                      _old_gen;
  static MutableSpace*                  _young_space;

  /* MRJ -- for colored space collection */
  static MutableSpace*                _young_colored_space[HC_TOTAL];
  static MutableSpace*                _old_colored_space[HC_TOTAL];


#if TASKQUEUE_STATS
  size_t                              _masked_pushes;
  size_t                              _masked_steals;
  size_t                              _arrays_chunked;
  size_t                              _array_chunks_processed;

  void print_local_stats(outputStream* const out, uint i) const;
  static void print_taskqueue_stats(outputStream* const out = gclog_or_tty);

  void reset_stats();
#endif // TASKQUEUE_STATS

  PSYoungPromotionLAB                 _young_lab;
  PSOldPromotionLAB                   _old_lab;
  PSColoredSpacePromotionLAB          _young_colored_lab[HC_TOTAL];
  PSColoredSpacePromotionLAB          _old_colored_lab[HC_TOTAL];
  bool                                _young_gen_is_full;
  bool                                _old_gen_is_full;
  bool                                _young_colored_space_is_full[HC_TOTAL];
  bool                                _old_colored_space_is_full[HC_TOTAL];

  OopStarTaskQueue                    _claimed_stack_depth;
  OverflowTaskQueue<oop, mtGC>        _claimed_stack_breadth;

  bool                                _totally_drain;
  uint                                _target_stack_size;

  uint                                _array_chunk_size;
  uint                                _min_array_size_for_chunking;

  static bool                         _object_organize;
  //static jint                         _cnt;

  bool                                _safe_scavenge;

  PromotionFailedInfo                 _promotion_failed_info;

  // Accessors
  static PSOldGen* old_gen()         { return _old_gen; }
  static MutableSpace* young_space() { return _young_space; }
  static MutableSpace* young_colored_space(HeapColor color) {
    return _young_colored_space[color];
  }
  static MutableSpace* old_colored_space(HeapColor color) {
    return _old_colored_space[color];
  }


  inline static PSPromotionManager* manager_array(uint index);
  template <class T> inline void claim_or_forward_internal_depth(T* p);

  // On the task queues we push reference locations as well as
  // partially-scanned arrays (in the latter case, we push an oop to
  // the from-space image of the array and the length on the
  // from-space image indicates how many entries on the array we still
  // need to scan; this is basically how ParNew does partial array
  // scanning too). To be able to distinguish between reference
  // locations and partially-scanned array oops we simply mask the
  // latter oops with 0x01. The next three methods do the masking,
  // unmasking, and checking whether the oop is masked or not. Notice
  // that the signature of the mask and unmask methods looks a bit
  // strange, as they accept and return different types (oop and
  // oop*). This is because of the difference in types between what
  // the task queue holds (oop*) and oops to partially-scanned arrays
  // (oop). We do all the necessary casting in the mask / unmask
  // methods to avoid sprinkling the rest of the code with more casts.

  // These are added to the taskqueue so PS_CHUNKED_ARRAY_OOP_MASK (or any
  // future masks) can't conflict with COMPRESSED_OOP_MASK
#define PS_CHUNKED_ARRAY_OOP_MASK  0x2

  bool is_oop_masked(StarTask p) {
    // If something is marked chunked it's always treated like wide oop*
    return (((intptr_t)(oop*)p) & PS_CHUNKED_ARRAY_OOP_MASK) ==
                                  PS_CHUNKED_ARRAY_OOP_MASK;
  }

  oop* mask_chunked_array_oop(oop obj) {
    assert(!is_oop_masked((oop*) obj), "invariant");
    oop* ret = (oop*) (cast_from_oop<uintptr_t>(obj) | PS_CHUNKED_ARRAY_OOP_MASK);
    assert(is_oop_masked(ret), "invariant");
    return ret;
  }

  oop unmask_chunked_array_oop(StarTask p) {
    assert(is_oop_masked(p), "invariant");
    assert(!p.is_narrow(), "chunked array oops cannot be narrow");
    oop *chunk = (oop*)p;  // cast p to oop (uses conversion operator)
    oop ret = oop((oop*)((uintptr_t)chunk & ~PS_CHUNKED_ARRAY_OOP_MASK));
    assert(!is_oop_masked((oop*) ret), "invariant");
    return ret;
  }

  template <class T> void  process_array_chunk_work(oop obj,
                                                    int start, int end);
  void process_array_chunk(oop old);

  template <class T> void push_depth(T* p);

  inline void promotion_trace_event(oop new_obj, oop old_obj, size_t obj_size,
                                    uint age, bool tenured,
                                    const PSPromotionLAB* lab);

#ifdef PROFILE_OBJECT_INFO
  static unsigned long _live_objects[2][HC_ENUM_TOTAL];
  static unsigned long _live_size[2][HC_ENUM_TOTAL];
  static unsigned long _live_refs[2][HC_ENUM_TOTAL];
  static unsigned long _hot_objects[2][HC_ENUM_TOTAL];
  static unsigned long _hot_size[2][HC_ENUM_TOTAL];
  static unsigned long _hot_refs[2][HC_ENUM_TOTAL];
#endif

 protected:
  static OopStarTaskQueueSet* stack_array_depth()   { return _stack_array_depth; }
 public:
  // Static
  static void initialize();

  static void pre_scavenge();
  static bool post_scavenge(YoungGCTracer& gc_tracer);
  static void set_object_organize(bool org) { _object_organize = org;  }
  static bool object_organize()             { return _object_organize; }

  static PSPromotionManager* gc_thread_promotion_manager(uint index);
  static PSPromotionManager* vm_thread_promotion_manager();

  static bool steal_depth(int queue_num, int* seed, StarTask& t);

  PSPromotionManager();

  // Accessors
  OopStarTaskQueue* claimed_stack_depth() {
    return &_claimed_stack_depth;
  }

  bool young_gen_is_full()             { return _young_gen_is_full; }

  bool old_gen_is_full()               { return _old_gen_is_full; }
  void set_old_gen_is_full(bool state) { _old_gen_is_full = state; }

  bool colored_space_is_full(HeapColor color) {
    return _young_colored_space_is_full[color];
  }

  bool old_colored_space_is_full(HeapColor color) {
    return _old_colored_space_is_full[color];
  }

  bool safe_scavenge()                { return _safe_scavenge; }
  void set_safe_scavenge(bool s)      { _safe_scavenge = s; }

  // Promotion methods
  template<bool promote_immediately> oop copy_to_survivor_space(oop o);
  template<bool promote_immediately> oop copy_to_colored_space(oop o, HeapColor color);
  oop oop_promotion_failed(oop obj, markOop obj_mark);

  void reset();

  void flush_labs();
  void drain_stacks(bool totally_drain) {
    drain_stacks_depth(totally_drain);
  }
 public:
  void drain_stacks_cond_depth() {
    if (claimed_stack_depth()->size() > _target_stack_size) {
      drain_stacks_depth(false);
    }
  }
  void drain_stacks_depth(bool totally_drain);

  bool stacks_empty() {
    return claimed_stack_depth()->is_empty();
  }

  inline void process_popped_location_depth(StarTask p);

  static bool should_scavenge(oop* p, bool check_to_space = false);
  static bool should_scavenge(narrowOop* p, bool check_to_space = false);
  
  template <class T, bool promote_immediately>
  void copy_and_push_safe_barrier(T* p);

  static inline HeapColor get_survivor_color(HeapWord* obj);
  static inline HeapColor get_current_color(HeapWord *obj);

  template <class T> inline void claim_or_forward_depth(T* p);

  TASKQUEUE_STATS_ONLY(inline void record_steal(StarTask& p);)

  void push_contents(oop obj);

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

#endif // SHARE_VM_GC_PARALLEL_PSPROMOTIONMANAGER_HPP
