#include "precompiled.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "interpreter/oopMapCache.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/memprofiler.hpp"
#include "runtime/hotMethodSampler.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/task.hpp"
#include "runtime/vmThread.hpp"
#include "code/nmethod.hpp"
#include "oops/oop.hpp"
#include "runtime/thread.hpp"
#include "runtime/frame.hpp"
#include "runtime/vframe.hpp"
#include "runtime/vm_operations.hpp"
#include "runtime/mutexLocker.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "runtime/os.hpp"
#include "memory/heapInspection.hpp"
#include "memory/resourceArea.hpp"
#include "oops/klass.hpp"
#include "runtime/os.hpp"
#include "utilities/globalDefinitions.hpp"
#include "runtime/mutexLocker.hpp"
#ifndef SERIALGC
#include "gc/parallel/parallelScavengeHeap.hpp"
#endif
#include "setjmp.h"

#ifdef TARGET_OS_FAMILY_linux
# include "thread_linux.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_solaris
# include "thread_solaris.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_windows
# include "thread_windows.inline.hpp"
#endif

static int _cnt = 0;

jlong HotMethodSampler::total_stack_samples = 0;
jlong HotMethodSampler::valid_stack_samples = 0;
jlong HotMethodSampler::segfault_samples    = 0;
jlong HotMethodSampler::val_threads_sampled = 0;
jlong HotMethodSampler::max_threads_sampled = 0;
jlong HotMethodSampler::tot_threads_sampled = 0;
jlong HotMethodSampler::total_sampler_invs  = 0;
jlong HotMethodSampler::cool_down_counter   = 0;
float HotMethodSampler::avg_threads_sampled = 0.0;

#ifndef PRODUCT
class HotMethodSamplerTask : public PeriodicTask 
{     public:
  bool is_active();
  HotMethodSamplerTask(int interval_time) : PeriodicTask(interval_time) {}
  void task();
};

HotMethodSamplerTask* HotMethodSampler::_task    = NULL;

class ThreadSampler : public ThreadClosure {
  outputStream *_out;

public:
  ThreadSampler()                  : _out(NULL) {}
  ThreadSampler(outputStream *out) : _out(out)  {}
  
  static bool should_care(JavaThread *jthread) {
    oop threadObj = jthread->threadObj();
    if (threadObj == NULL || !threadObj->is_a(SystemDictionary::Thread_klass())) {
      //stacks_log->print(" null thread");
      return false;
    }

    if (java_lang_Thread::is_daemon(threadObj)) {
      //stacks_log->print(" daemon");
      return false;
    }

    jint state = (jint)java_lang_Thread::get_thread_status(threadObj);
    if (!(state & JVMTI_THREAD_STATE_RUNNABLE)) {
      //stacks_log->print(" not runnable");
      return false;
    }

    if (jthread->is_ext_suspended() || jthread->is_external_suspend()) {
      //stacks_log->print(" suspended");
      return false;
    }

    OSThread* osThread = jthread->osthread();
    if (osThread != NULL && osThread->interrupted()) {
      //stacks_log->print(" interrupted");
      return false;
    }

    return true;
  }

  void do_thread(Thread* thread) {
    sigjmp_buf *env = Universe::sample_thread_env();
    if( sigsetjmp(*env,1) == 0 ) {
      JavaThread* jthread = (JavaThread*) thread; 
      /*if (jthread) {
        stacks_log->print("trying thread: %d ...", jthread->osthread()->thread_id());
      }*/
      if (jthread && should_care(jthread)) {
        //stacks_log->print(" we care ...");
        jthread->sample_stack(_out);
        HotMethodSampler::val_threads_sampled++; 
      }
      //stacks_log->print_cr("");
    }
    if (HotMethodSampler::val_threads_sampled > HotMethodSampler::max_threads_sampled) {
      HotMethodSampler::max_threads_sampled = HotMethodSampler::val_threads_sampled;
    }
  }
};

void HotMethodSamplerTask::task() 
{
  HotMethodSampler::do_stack();
}

bool HotMethodSampler::is_active() {
  return _task != NULL;
}

void HotMethodSampler::engage() {
  if (!is_active()) {
    _task = new HotMethodSamplerTask(CallStackSampleInterval);
    _task->enroll();
  }
}

void HotMethodSampler::disengage() {
  if (!is_active()) return;

  // remove HotMethodSamplerTask
  assert(_task != NULL, "sanity check");
  _task->disenroll();
  delete _task;
  _task = NULL;
}

void HotMethodSampler::cool_down() {
  HotMethodSampler_lock->lock_without_safepoint_check();
  if (HotMethodAllocate) {
    HotMethodSampler::cool_down_methods();
  }
  if (HotKlassAllocate || HotKlassOrganize) {
    HotMethodSampler::cool_down_klasses();
  }
  HotMethodSampler_lock->unlock();
}

void HotMethodSampler::do_stack() {

  val_threads_sampled = 0;

  ThreadSampler ts;
  Threads::java_threads_do(&ts);

  total_sampler_invs++;
  cool_down_counter++;
  if (cool_down_counter == CoolDownFactor) {
    cool_down();
    cool_down_counter = 0;
  }

  tot_threads_sampled += val_threads_sampled;
}

void HotMethodSampler::do_stack(outputStream *out) {

  val_threads_sampled = 0;

  ThreadSampler ts(out);
  Threads::java_threads_do(&ts);

  total_sampler_invs++;
  cool_down_counter++;
  if (cool_down_counter == CoolDownFactor) { 
    cool_down();
    cool_down_counter = 0;
  }

  tot_threads_sampled += val_threads_sampled;
}

GrowableArray<Method*>* HotMethodSampler::_methods = new (ResourceObj::C_HEAP, mtInternal) GrowableArray<Method*>(512, true);
GrowableArray<Klass*>*  HotMethodSampler::_klasses = new (ResourceObj::C_HEAP, mtInternal) GrowableArray<Klass*> (512, true);

void HotMethodSampler::add_method(Method* m) {
  for (int i = 0; i < _methods->length(); i++) {
    if ( _methods->at(i) == m ) {
      return;
    }
  }
  _methods->append(m);
}

void HotMethodSampler::add_klass(Klass* k) {
  for (int i = 0; i < _klasses->length(); i++) {
    if ( _klasses->at(i) == k ) {
      return;
    }
  }
  _klasses->append(k);
}

bool HotMethodSampler::kal_contains(GrowableArray<Klass*>* kal, Klass* k) {
  if (kal) {
    for (int i = 0; i < kal->length(); i++) {
      if ( kal->at(i) == k ) {
        return true;
      }
    }
  }
  return false;
}

void HotMethodSampler::cool_down_methods() {
  for (int i = 0; i < _methods->length(); i++) {
    _methods->at(i)->cool_down();
  }
}

void HotMethodSampler::cool_down_klasses() {
  for (int i = 0; i < _klasses->length(); i++) {
    _klasses->at(i)->cool_down();
  }
}

#endif

