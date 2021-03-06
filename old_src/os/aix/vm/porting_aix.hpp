/*
 * Copyright 2012, 2015 SAP AG. All rights reserved.
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

#ifndef OS_AIX_VM_PORTING_AIX_HPP
#define OS_AIX_VM_PORTING_AIX_HPP

#include <stddef.h>

// PPC port only:
#define assert0(b) assert( (b), "" )
#define guarantee0(b) assert( (b), "" )
template <class T1, class T2> bool is_aligned_to(T1 what, T2 alignment) {
  return  ( ((uintx)(what)) & (((uintx)(alignment)) - 1) ) == 0 ? true : false;
}

// Header file to contain porting-relevant code which does not have a
// home anywhere else and which can not go into os_<platform>.h because
// that header is included inside the os class definition, hence all
// its content is part of the os class.

// Aix' own version of dladdr().
// This function tries to mimick dladdr(3) on Linux
// (see http://linux.die.net/man/3/dladdr)
// dladdr(3) is not POSIX but a GNU extension, and is not available on AIX.
//
// Differences between AIX dladdr and Linux dladdr:
//
// 1) Dl_info.dli_fbase: can never work, is disabled.
//   A loaded image on AIX is divided in multiple segments, at least two
//   (text and data) but potentially also far more. This is because the loader may
//   load each member into an own segment, as for instance happens with the libC.a
// 2) Dl_info.dli_sname: This only works for code symbols (functions); for data, a
//   zero-length string is returned ("").
// 3) Dl_info.dli_saddr: For code, this will return the entry point of the function,
//   not the function descriptor.

typedef struct {
  const char *dli_fname; // file path of loaded library
  // void *dli_fbase;
  const char *dli_sname; // symbol name; "" if not known
  void *dli_saddr;       // address of *entry* of function; not function descriptor;
} Dl_info;

// Note: we export this to use it inside J2se too
#ifdef __cplusplus
extern "C"
#endif
int dladdr(void *addr, Dl_info *info);


// The semantics in this file are thus that codeptr_t is a *real code ptr*.
// This means that any function taking codeptr_t as arguments will assume
// a real codeptr and won't handle function descriptors (eg getFuncName),
// whereas functions taking address as args will deal with function
// descriptors (eg os::dll_address_to_library_name).
typedef unsigned int* codeptr_t;

// helper function - given a program counter, tries to locate the traceback table and
// returns info from it (like, most importantly, function name, displacement of the
// pc inside the function, and the traceback table itself.
#ifdef __cplusplus
extern "C"
#endif
int getFuncName(
      codeptr_t pc,                    // [in] program counter
      char* p_name, size_t namelen,    // [out] optional: user provided buffer for the function name
      int* p_displacement,             // [out] optional: displacement
      const struct tbtable** p_tb,     // [out] optional: ptr to traceback table to get further information
      char* p_errmsg, size_t errmsglen,// [out] optional: user provided buffer for error messages
      bool demangle = true             // [in] whether to demangle the name
    );

// -------------------------------------------------------------------------

// A simple critical section which shall be based upon OS critical
// sections (CRITICAL_SECTION resp. Posix Mutex) and nothing else.

#include <pthread.h>

namespace MiscUtils {
  typedef pthread_mutex_t critsect_t;

  inline void init_critsect(MiscUtils::critsect_t* cs) {
    pthread_mutex_init(cs, NULL);
  }
  inline void free_critsect(MiscUtils::critsect_t* cs) {
    pthread_mutex_destroy(cs);
  }
  inline void enter_critsect(MiscUtils::critsect_t* cs) {
    pthread_mutex_lock(cs);
  }
  inline void leave_critsect(MiscUtils::critsect_t* cs) {
    pthread_mutex_unlock(cs);
  }

  // Need to wrap this in an object because we need to dynamically initialize
  // critical section (because of windows, where there is no way to initialize
  // a CRITICAL_SECTION statically. On Unix, we could use
  // PTHREAD_MUTEX_INITIALIZER)

  // Note: The critical section does NOT get cleaned up in the destructor. That is
  // by design: the CritSect class is only ever used as global objects whose
  // lifetime spans the whole VM life; in that context we don't want the lock to
  // be cleaned up when global C++ objects are destroyed, but to continue to work
  // correctly right to the very end of the process life.
  class CritSect {
    critsect_t _cs;
  public:
    CritSect()        { init_critsect(&_cs); }
    //~CritSect()       { free_critsect(&_cs); }
    void enter()      { enter_critsect(&_cs); }
    void leave()      { leave_critsect(&_cs); }
  };

  class AutoCritSect {
    CritSect* const _pcsobj;
  public:
    AutoCritSect(CritSect* pcsobj)
      : _pcsobj(pcsobj)
    {
      _pcsobj->enter();
    }
    ~AutoCritSect() {
      _pcsobj->leave();
    }
  };

}

#endif // OS_AIX_VM_PORTING_AIX_HPP
