/*
 * Copyright (c) 1997, 2014, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_X86_VM_CPPINTERPRETERGENERATOR_X86_HPP
#define CPU_X86_VM_CPPINTERPRETERGENERATOR_X86_HPP

 protected:

  void generate_more_monitors();
  void generate_deopt_handling();
  address generate_interpreter_frame_manager(bool synchronized); // C++ interpreter only
  void generate_compute_interpreter_state(const Register state,
                                          const Register prev_state,
                                          const Register sender_sp,
                                          bool native); // C++ interpreter only

#endif // CPU_X86_VM_CPPINTERPRETERGENERATOR_X86_HPP
