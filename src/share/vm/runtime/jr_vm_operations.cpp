#include "precompiled.hpp"
#include "code/nmethod.hpp"
#include "oops/oop.hpp"
#include "runtime/task.hpp"
#include "runtime/thread.hpp"
#include "runtime/frame.hpp"
#include "runtime/jrMethodInfo.hpp"
#include "runtime/vm_operations.hpp"
#include "runtime/jr_vm_operations.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"

// ThreadSampler is used by JRHotMethodSampler in its call to threads_do
class ThreadSampler : public ThreadClosure {
 public:
  // Mark access to the top nmethod on "thread"'s stack
  void do_thread(Thread* thread) {

#if 1
    if (thread->is_Java_thread()) {
      JavaThread* jthread = (JavaThread*) thread;

      if (jthread->has_last_Java_frame()) {
        RegisterMap rm(jthread);
        javaVFrame* vf;

        // Look for the highest java vframe on the stack
        for (vf = jthread->last_java_vframe(&rm); vf && !vf->is_java_frame(); vf->sender());

        // Make sure the found frame corresponds to a nmethod
        if (vf->is_java_frame() && vf->cb()->is_nmethod()) {
          nmethod* nm = vf->nm();
        
          assert(nm != NULL, "Bad NMethod");

          Method* m = nm->method();
          m->mark_hot(); 

          if (HotKlassOrganize) {
            GrowableArray <Klass*>* kal = m->klass_access_list();
            guarantee(kal, "null klass_access_list");
            for (int i = 0; i < kal->length(); i++) {
              kal->at(i)->mark_hot();
            }
          }
        }
      }
    }
#endif
  }
};

class JRHotMethodSamplerTask : public PeriodicTask {
  jlong _start_time;
 public:
  JRHotMethodSamplerTask(size_t interval_time) : PeriodicTask(interval_time) {}

  void set_start_time(jlong millis) { _start_time = millis; }

  void task() {
    jlong start, end, elapsed;

    start = os::javaTimeMillis();
    JRHotMethodSampler vmop;
    VMThread::execute(&vmop);
    end = os::javaTimeMillis();
#if 1
    elapsed = end - start;
    tty->print_cr("rel_time: %ld, val_time: %ld, nthreads: %d",
                   (start - _start_time), elapsed,
                   Threads::number_of_threads());
    tty->flush();
#endif
  }
};

JRHotMethodSamplerTask* JRHotMethodSamplerTaskManager::_task = NULL;

// JRHotMethodSampler vm operation implementation
JRHotMethodSampler::JRHotMethodSampler()  {}
JRHotMethodSampler::~JRHotMethodSampler() {}

// JRHotMethodSamplerTaskManager - Wrapper for the corresponding 
// PeriodicTask subclass
void JRHotMethodSamplerTaskManager::engage(size_t interval_time) {
  _task = new JRHotMethodSamplerTask(interval_time);
  _task->enroll();
  _task->set_start_time(os::javaTimeMillis());
}

void JRHotMethodSamplerTaskManager::disengage() {
  _task->disenroll();
  delete _task;
  _task = NULL;
}

JRHotMethodSampler::Mode JRHotMethodSampler::evaluation_mode() const { 
  return _safepoint;
}

// JRHotMethodSampler's action - Scans each thread for hot methods
void JRHotMethodSampler::doit() {
  ThreadSampler ts;
  Threads::threads_do(&ts);
}

class JRCoolDownMethodsTask : public PeriodicTask {
 public:
  JRCoolDownMethodsTask(size_t interval_time) : PeriodicTask(interval_time) {}

  void task() {
    JRCoolDownMethods vmop;
    VMThread::execute(&vmop);
  }
};

JRCoolDownMethodsTask* JRCoolDownMethodsTaskManager::_task = NULL;

// JRCoolDownMethods vm operation implementation
JRCoolDownMethods::JRCoolDownMethods()  {}
JRCoolDownMethods::~JRCoolDownMethods() {}

// JRCoolDownMethodsTaskManager - Wrapper for the corresponding 
// PeriodicTask subclass
void JRCoolDownMethodsTaskManager::engage(size_t interval_time) {
  _task = new JRCoolDownMethodsTask(interval_time);
  _task->enroll();
}

void JRCoolDownMethodsTaskManager::disengage() {
  _task->disenroll();
  delete _task;
  _task = NULL;
}

JRCoolDownMethods::Mode JRCoolDownMethods::evaluation_mode() const { 
  return _safepoint;
}

// JRHotMethodSampler's action - Scans each thread for hot methods
void JRCoolDownMethods::doit() {
  //static int _cnt = 0;
  jlong start, end, elapsed;
  start = os::javaTimeMillis();

  JRMethodInfoManager_lock->lock_without_safepoint_check();
  JRMethodInfoManager::cool_down_methods();
  if (HotKlassOrganize) {
    JRMethodInfoManager::cool_down_klasses();
  }
  JRMethodInfoManager_lock->unlock();

  end = os::javaTimeMillis();
  elapsed = end - start;
  //if (_cnt % 100 == 0) {
    tty->print_cr("mlen: %d, klen: %d, elapsed: %ld",
      JRMethodInfoManager::methods_length(),
      JRMethodInfoManager::klasses_length(),
      elapsed);
  //}
  //_cnt++;
}

#if 0
GrowableArray<JRCountedSymbol*>* JRHotKlassList::klasses = new (ResourceObj::C_HEAP) GrowableArray<JRCountedSymbol*>(32, true);

int JRHotKlassList::add_count = 0;

class HeapInstanceCounter : public ObjectClosure {
 private:
  const Symbol* _klass;

  long _count;
  long _size;

 public:
  HeapInstanceCounter(const Symbol* klass) : _klass(klass), _count(0), _size(0) {}

  void do_object(oop obj) {
    if (obj->klass()->klass_part()->name() == _klass) {
      _count++;
      _size += obj->size();
    }
  }

  long getCount() const {
    return _count;
  }

  long getSize() const {
    return _size;
  }
};

class HeapOopCounter : public ObjectClosure {
 private:
  const Symbol* _klass;

  long _count;
  long _size;

 public:
  HeapOopCounter() : _count(0), _size(0) {}

  void do_object(oop obj) {
    long prev_count = _count;
    long prev_size = _size;

    _count++;
    _size += obj->size();

    if (prev_count > _count || prev_size > _size)
      tty->print_cr("Overflow");
  }

  long getCount() const {
    return _count;
  }

  long getSize() const {
    return _size;
  }
};

class JRHotMethodSamplerTask : public PeriodicTask {
 public:
  JRHotMethodSamplerTask(size_t interval_time) : PeriodicTask(interval_time) {}

  void task() {
    JRHotMethodSampler vmop;
    VMThread::execute(&vmop);
  }
};

class JRHotFieldCollectorTask : public PeriodicTask {
 private:
  static int _count;

 public:
  JRHotFieldCollectorTask(size_t interval_time) : PeriodicTask(interval_time) {}

  void task() {
    JRHotFieldCollector vmop;
    tty->print_cr("Collection Interval %d", ++_count);
    VMThread::execute(&vmop);
  }
};

void JRHotKlassList::addKlass(const Symbol* klass) {
  // Register this addition
  add_count++;
  
  // Check if "klass" is already in the set
  for (int i = 0; i < klasses->length(); i++) {
    if (klass == klasses->at(i)->getSymbol()) {
      klasses->at(i)->increment();
      return;
    }
  }
  
  // Add "klass" to the set
  klasses->push(new JRCountedSymbol(klass));
}

// Call a function on each Symbol in the klasses array
void JRHotKlassList::doKlasses(void (*f)(const Symbol* klass)) {
  for (int i = 0; i < klasses->length(); i++) {
    (*f)(klasses->at(i)->getSymbol());
  }
}

void JRHotKlassList::flush() {
  add_count = 0;

  for (int i = 0; i < klasses->length(); i++) {
    delete klasses->at(i);
  }

  klasses->clear();
}

void JRHotKlassList::sort() {
  klasses->sort(JRCountedSymbol::compare);
}

GrowableArray<const Symbol*> JRHotKlassList::matchHeapOopsWithHotKlasses(size_t cutoff) {
  GrowableArray<const Symbol*> hot_klasses(32, false);

  long hot_count = 0;
  long live_count = 0;
  long hot_size = 0;
  long live_size = 0;

  HeapOopCounter hoc;

  // Sort the classes so that we may deal with them in a most
  // used class first order.
  if (cutoff > 0)
    sort();

  // Measure the heap
  Universe::heap()->ensure_parsability(false);
  Universe::heap()->object_iterate(&hoc);

  live_count = hoc.getCount();
  live_size = hoc.getSize();
  
  // Measure the hot objects on the heap
  for (int i = 0; i < klasses->length(); i++) {
    const Symbol* sym = klasses->at(i)->getSymbol();
    HeapInstanceCounter hic(sym);

    // Ensure again and iterate over all objects on the heap
    Universe::heap()->ensure_parsability(false);
    Universe::heap()->object_iterate(&hic);

    // Add classes to the hot classes list ignoring those that don't fit.
    // If zero is specified as the cutoff then we add the class to the list
    // without question.
    if (((size_t)(hot_size + hic.getSize())) <= cutoff || cutoff == 0) { 
#if 0
      tty->print_cr("%s --- %d", sym->as_utf8(), klasses->at(i)->getCount());
      tty->print_cr("   count: %7ld, %6.2f%%", hic.getCount(), ((float) hic.getCount())/hoc.getCount()*100);
      tty->print_cr("   size:  %7ld, %6.2f%%", hic.getSize(), ((float) hic.getSize())/hoc.getSize()*100);
#endif
      
      hot_count += hic.getCount();
      hot_size += hic.getSize();

      // Add the klass to the list
      hot_klasses.push(sym);
    }
  }

  tty->print_cr("      |    Hot    |    Live   | Hot / Live");
  tty->print_cr("-------------------------------------------");
  tty->print_cr("Count | %9ld | %9ld | %8.4f%%", hot_count, live_count, ((float) hot_count)/live_count * 100);
  tty->print_cr("Size  | %9ld | %9ld | %8.4f%%", hot_size, live_size, ((float) hot_size)/live_size * 100);
  tty->cr();

  return hot_klasses;
}

void JRHotKlassList::print() {
  for (int i = 0; i < klasses->length(); i++) {
    tty->print_cr("\t%s", klasses->at(i)->getSymbol()->as_utf8());
  }
}

int JRHotFieldCollectorTask::_count = 0;

JRHotMethodSamplerTask* JRHotMethodSamplerTaskManager::_task = NULL;
JRHotFieldCollectorTask* JRHotFieldCollectorTaskManager::_task = NULL;

// JRHotMethodSampler vm operation implementation
JRHotMethodSampler::JRHotMethodSampler() {}
JRHotMethodSampler::~JRHotMethodSampler() {}

JRHotMethodSampler::Mode JRHotMethodSampler::evaluation_mode() const { 
  return _safepoint;
}

// JRHotMethodSampler's action - Scans each thread for hot methods
void JRHotMethodSampler::doit() {
  ThreadSampler ts;
  Threads::threads_do(&ts);
}

// JRHotFieldCollector vm operation implementation
JRHotFieldCollector::JRHotFieldCollector() {}
JRHotFieldCollector::~JRHotFieldCollector() {}

JRHotFieldCollector::Mode JRHotFieldCollector::evaluation_mode() const { 
  return _safepoint;
}

// JRHotFieldCollector's action - Currently it only prints and then
// empties the array
void JRHotFieldCollector::doit() {
  JRHotKlassList::matchHeapOopsWithHotKlasses(500000);
  JRHotKlassList::flush();
}

// JRHotMethodSamplerTaskManager - Wrapper for the corresponding 
// PeriodicTask subclass
void JRHotMethodSamplerTaskManager::engage(size_t interval_time) {
  _task = new JRHotMethodSamplerTask(interval_time);
  _task->enroll();
}

void JRHotMethodSamplerTaskManager::disengage() {
  _task->disenroll();
  delete _task;
  _task = NULL;
}

// JRHotFieldCollectorTaskManager - Wrapper for the corresponding 
// PeriodicTask subclass
void JRHotFieldCollectorTaskManager::engage(size_t interval_time) {
  _task = new JRHotFieldCollectorTask(interval_time);
  _task->enroll();
}

void JRHotFieldCollectorTaskManager::disengage() {
  _task->disenroll();
  delete _task;
  _task = NULL;
}
#endif
