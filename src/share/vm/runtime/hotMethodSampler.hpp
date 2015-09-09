#ifndef SHARE_VM_RUNTIME_HOTMETHODSAMPLER_HPP
#define SHARE_VM_RUNTIME_HOTMETHODSAMPLER_HPP

#include "runtime/vm_operations.hpp"

class HotMethodSamplerTask;

class HotMethodSampler : AllStatic {
  friend class HotMethodSamplerTask;
  friend class SamplerThread;
private:
  static HotMethodSamplerTask* _task;
  static GrowableArray<Method*>* _methods;
  static GrowableArray<Klass*>* _klasses;
public:
  static jlong total_stack_samples;
  static jlong valid_stack_samples;
  static jlong segfault_samples;
  static jlong val_threads_sampled;
  static jlong max_threads_sampled;
  static jlong tot_threads_sampled;
  static jlong total_sampler_invs;
  static jlong cool_down_counter;
  static float avg_threads_sampled;

  static void cool_down();
  static void cool_down_methods();
  static void cool_down_klasses();
  static int methods_length() { return _methods->length(); }
  static int klasses_length() { return _klasses->length(); }
  static void add_method(Method* m);
  static void add_klass(Klass* k);

  //Start/stop the profiler
  static void do_stack()                  PRODUCT_RETURN;
  static void do_stack(outputStream *out) PRODUCT_RETURN;
  static void do_stack_thread_list(const jthread *_thread_list,
                                   int _thread_count)  PRODUCT_RETURN;
  static void engage()        PRODUCT_RETURN;
  static void disengage()     PRODUCT_RETURN;
  static bool is_active()     PRODUCT_RETURN0;

  static bool kal_contains(GrowableArray<Klass*>* kal, Klass* k);
};
#endif // SHARE_VM_RUNTIME_HOTMETHODSAMPLER_HPP

