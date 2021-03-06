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

#ifndef SHARE_VM_GC_PARALLEL_PSSCAVENGE_INLINE_HPP
#define SHARE_VM_GC_PARALLEL_PSSCAVENGE_INLINE_HPP

#include "gc/parallel/cardTableExtension.hpp"
#include "gc/parallel/parallelScavengeHeap.hpp"
#include "gc/parallel/psPromotionManager.inline.hpp"
#include "gc/parallel/psScavenge.hpp"
#include "memory/iterator.hpp"
#include "utilities/globalDefinitions.hpp"
#include "gc/shared/mutableColoredSpace.hpp"
#include <string.h>

inline void PSScavenge::save_to_space_top_before_gc() {
  ParallelScavengeHeap* heap = ParallelScavengeHeap::heap();
  _to_space_top_before_gc = heap->young_gen()->to_space()->top();
}

template <class T> inline bool PSScavenge::should_scavenge(T* p) {
  T heap_oop = oopDesc::load_heap_oop(p);
  return PSScavenge::is_obj_in_young(heap_oop);
}

inline void PSScavenge::save_eden_colored_space_bounds() {
  assert(UseColoredSpaces, "only used with colors");
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();

  _eden_colored_space_top[HC_RED]  =
    ((MutableColoredSpace*)heap->young_gen()->eden_space())->
     colored_spaces()->at(HC_RED)->space()->top();
  _eden_colored_space_top[HC_BLUE] =
    ((MutableColoredSpace*)heap->young_gen()->eden_space())->
     colored_spaces()->at(HC_BLUE)->space()->top();
  _eden_colored_space_bottom[HC_RED]  =
    ((MutableColoredSpace*)heap->young_gen()->eden_space())->
     colored_spaces()->at(HC_RED)->space()->bottom();
  _eden_colored_space_bottom[HC_BLUE] =
    ((MutableColoredSpace*)heap->young_gen()->eden_space())->
     colored_spaces()->at(HC_BLUE)->space()->bottom();
}

inline void PSScavenge::save_from_colored_space_bounds() {
  assert(UseColoredSpaces, "only used with colors");
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();

  _from_colored_space_top[HC_RED]  =
    ((MutableColoredSpace*)heap->young_gen()->from_space())->
     colored_spaces()->at(HC_RED)->space()->top();
  _from_colored_space_top[HC_BLUE] =
    ((MutableColoredSpace*)heap->young_gen()->from_space())->
     colored_spaces()->at(HC_BLUE)->space()->top();
  _from_colored_space_bottom[HC_RED]  =
    ((MutableColoredSpace*)heap->young_gen()->from_space())->
     colored_spaces()->at(HC_RED)->space()->bottom();
  _from_colored_space_bottom[HC_BLUE] =
    ((MutableColoredSpace*)heap->young_gen()->from_space())->
     colored_spaces()->at(HC_BLUE)->space()->bottom();
}

template <class T> inline bool PSScavenge::should_scavenge(T* p, bool safe) {
  T heap_oop = oopDesc::load_heap_oop(p);
  if (oopDesc::is_null(heap_oop)) return false;
  oop obj = oopDesc::decode_heap_oop_not_null(heap_oop);
  if (safe && !(obj->is_oop())) {
    tty->print_cr("should_scavenge: bad oop: %p\n", obj);
    return false;
  }
  return PSScavenge::is_obj_in_young((HeapWord*)obj);
}

template <class T>
inline bool PSScavenge::should_scavenge(T* p, bool safe, MutableSpace* to_space) {
  if (should_scavenge(p, safe)) {
    oop obj = oopDesc::load_decode_heap_oop_not_null(p);
    // Skip objects copied to to_space since the scavenge started.
    HeapWord* const addr = (HeapWord*)obj;
    return addr < to_space_top_before_gc() || addr >= to_space->end();
  }
  return false;
}

template <class T>
inline bool PSScavenge::should_scavenge(T* p, bool safe, bool check_to_space) {
  if (check_to_space) {
    ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
    return should_scavenge(p, safe, heap->young_gen()->to_space());
  }
  return should_scavenge(p, safe);
}

#if 0
inline HeapColor PSScavenge::get_current_color(HeapWord *obj)
{
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  if (eden_colored_space_contains(HC_RED, obj) ||
      from_colored_space_contains(HC_RED, obj))
    return HC_RED;
  return HC_BLUE;
}
#endif

#if 0
inline HeapColor PSScavenge::get_survivor_color(PSPromotionManager *pm, HeapWord *obj) {
  if (SurvivorsAlwaysBlue) return HC_BLUE;
  if (SurvivorsAlwaysRed)  return HC_RED;

  oop o = (oop)obj;
  if (ColorObjectAllocations) {
    return get_current_color(obj);
  }

  if (HotKlassOrganize) {
    return ( o->klass()->is_hot() ? HC_RED : HC_BLUE );
  }

  if (MemBenchOrganize) {
    if (o->klass()->name() != NULL) {
      if (strstr(o->klass()->external_name(), "ColdObject")) {
        return HC_BLUE;
      }
    }
  }
#ifdef PROFILE_OBJECT_INFO
  if (pm->object_organize()) {
    markOop test_mark = o->mark();
    int age = (test_mark->has_displaced_mark_helper() /* o->has_displaced_mark() */) ?
      test_mark->displaced_mark_helper()->age() : test_mark->age();

    jint refcnt = -1;
    PersistentObjectInfo *poi = obj_poi(o);
    if (poi && obj_is_initialized(o)) {
      refcnt = (poi->val_load_cnt() + poi->val_store_cnt());
    }

    if (MemBenchOrganize) {
      if (o->klass()->name() != NULL) {
        if (strstr(o->klass()->external_name(), "ColdObject")) {
          return HC_BLUE;
        }
      }
    }

    if (age > ColorAgeThreshold) {
      return refcnt > ColorRefThreshold ? HC_RED : HC_BLUE;
    }
    return get_current_color(obj);
  }
  return get_current_color(obj);
#endif
}
#endif

template<bool promote_immediately>
class PSRootsClosure: public OopClosure {
 private:
  PSPromotionManager* _promotion_manager;

 protected:
  template <class T> void do_oop_work(T *p) {
    if (PSScavenge::should_scavenge(p)) {
      // We never card mark roots, maybe call a func without test?
      _promotion_manager->copy_and_push_safe_barrier<T, promote_immediately>(p);
    }
  }
 public:
  PSRootsClosure(PSPromotionManager* pm) : _promotion_manager(pm) { }
  void do_oop(oop* p)       { PSRootsClosure::do_oop_work(p); }
  void do_oop(narrowOop* p) { PSRootsClosure::do_oop_work(p); }
};

typedef PSRootsClosure</*promote_immediately=*/false> PSScavengeRootsClosure;
typedef PSRootsClosure</*promote_immediately=*/true> PSPromoteRootsClosure;

// Scavenges a single oop in a Klass.
class PSScavengeFromKlassClosure: public OopClosure {
 private:
  PSPromotionManager* _pm;
  // Used to redirty a scanned klass if it has oops
  // pointing to the young generation after being scanned.
  Klass*             _scanned_klass;
 public:
  PSScavengeFromKlassClosure(PSPromotionManager* pm) : _pm(pm), _scanned_klass(NULL) { }
  void do_oop(narrowOop* p) { ShouldNotReachHere(); }
  void do_oop(oop* p)       {
    ParallelScavengeHeap* psh = ParallelScavengeHeap::heap();
    assert(!psh->is_in_reserved(p), "GC barrier needed");
    if (PSScavenge::should_scavenge(p)) {
      assert(PSScavenge::should_scavenge(p, true), "revisiting object?");

      oop o = *p;
      oop new_obj;
      if (UseColoredSpaces) {
        HeapColor surv_color = _pm->get_survivor_color((HeapWord*)o);
        if (o->is_forwarded()) {
          new_obj = o->forwardee();
        } else {
          new_obj = _pm->copy_to_colored_space</*promote_immediately=*/false>(o, surv_color);
        }
      } else {
        if (o->is_forwarded()) {
          new_obj = o->forwardee();
        } else {
          new_obj = _pm->copy_to_survivor_space</*promote_immediately=*/false>(o);
        }
      }

      oopDesc::encode_store_heap_oop_not_null(p, new_obj);

      if (PSScavenge::is_obj_in_young(new_obj)) {
        do_klass_barrier();
      }
    }
  }
  void set_scanned_klass(Klass* klass) {
    assert(_scanned_klass == NULL || klass == NULL, "Should always only handling one klass at a time");
    _scanned_klass = klass;
  }

 private:
  void do_klass_barrier() {
    assert(_scanned_klass != NULL, "Should not be called without having a scanned klass");
    _scanned_klass->record_modified_oops();
  }

};


// Scavenges the oop in a Klass.
class PSScavengeKlassClosure: public KlassClosure {
 private:
  PSScavengeFromKlassClosure _oop_closure;
 protected:
 public:
  PSScavengeKlassClosure(PSPromotionManager* pm) : _oop_closure(pm) { }
  void do_klass(Klass* klass) {
    // If the klass has not been dirtied we know that there's
    // no references into  the young gen and we can skip it.

#ifndef PRODUCT
    if (TraceScavenge) {
      ResourceMark rm;
      gclog_or_tty->print_cr("PSScavengeKlassClosure::do_klass " PTR_FORMAT ", %s, dirty: %s",
                             p2i(klass),
                             klass->external_name(),
                             klass->has_modified_oops() ? "true" : "false");
    }
#endif

    if (klass->has_modified_oops()) {
      // Clean the klass since we're going to scavenge all the metadata.
      klass->clear_modified_oops();

      // Setup the promotion manager to redirty this klass
      // if references are left in the young gen.
      _oop_closure.set_scanned_klass(klass);

      klass->oops_do(&_oop_closure);

      _oop_closure.set_scanned_klass(NULL);
    }
  }
};

#endif // SHARE_VM_GC_PARALLEL_PSSCAVENGE_INLINE_HPP
