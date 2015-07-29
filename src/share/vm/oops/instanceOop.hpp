/*
 * Copyright (c) 1997, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_OOPS_INSTANCEOOP_HPP
#define SHARE_VM_OOPS_INSTANCEOOP_HPP

#include "oops/oop.hpp"

class PersistentObjectInfo;

// An instanceOop is an instance of a Java Class
// Evaluating "new HashTable()" will create an instanceOop.

/* MRJ -- IMPORTANT -- reference count profiling framework does not work with
 * compressed oops
 */
class instanceOopDesc : public oopDesc {
  private:
#ifdef PROFILE_OBJECT_INFO
    jint _id, _init_val;
    PersistentObjectInfo *_poi;

    jint _color_mark;
    HeapColor _color;
#endif
#if 0
    /* MRJ -- just assume COLORED_EDEN_SPACE is defined when UseColoredSpaces
     * is set
     */
#if (defined PROFILE_OBJECT_INFO) || !(defined COLORED_EDEN_SPACE)
    jint _color_mark;
    HeapColor _color;
#endif
#endif
 public:
  // aligned header size.
  static int header_size() { return sizeof(instanceOopDesc)/HeapWordSize; }

  // If compressed, the offset of the fields of the instance may not be aligned.
  static int base_offset_in_bytes() {
    // offset computation code breaks if UseCompressedClassPointers
    // only is true
    return (UseCompressedOops && UseCompressedClassPointers) ?
             klass_gap_offset_in_bytes() :
             sizeof(instanceOopDesc);
  }

  static bool contains_field_offset(int offset, int nonstatic_field_size) {
    int base_in_bytes = base_offset_in_bytes();
    return (offset >= base_in_bytes &&
            (offset-base_in_bytes) < nonstatic_field_size * heapOopSize);
  }

#ifdef PROFILE_OBJECT_INFO
  void initialize (jint id, jint init_val) { 
    _id               = id;
    _init_val         = init_val;
    _color_mark       = COLOR_MARK;
    _poi              = NULL;
  }
  jint id ()             { return _id; }
  jint init_val ()       { return _init_val; }
  bool is_initialized () { return _color_mark == COLOR_MARK; }

  PersistentObjectInfo *poi ()             { return _poi; }
  void set_poi (PersistentObjectInfo *poi) { _poi = poi;  }

  HeapColor color()               { return _color;  }
  void set_color(HeapColor color) { _color = color; }

  void set_color_mark(jint val) { _color_mark = val; }
  jint color_mark()             { return _color_mark; }
#endif
#if 0
    /* MRJ -- just assume COLORED_EDEN_SPACE is defined when UseColoredSpaces
     * is set
     */
#if (defined PROFILE_OBJECT_INFO) || !(defined COLORED_EDEN_SPACE)
  HeapColor color()               { return _color;  }
  void set_color(HeapColor color) { _color = color; }

  void set_color_mark(jint val) { _color_mark = val; }
  jint color_mark()             { return _color_mark; }
#endif
#endif
};

#endif // SHARE_VM_OOPS_INSTANCEOOP_HPP
