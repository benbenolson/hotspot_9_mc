#ifndef _SHARE_VM_RUNTIME_JR_VM_OPERATIONS_HPP_
#define _SHARE_VM_RUNTIME_JR_VM_OPERATIONS_HPP_

#include "runtime/vm_operations.hpp"

class JRHotMethodSamplerTask;
class JRCoolDownMethodsTask;

// This VM operation should collect the top method in all threads
class JRHotMethodSampler: public VM_Operation {
 public:
  JRHotMethodSampler();
  virtual ~JRHotMethodSampler();

  // Do the sampling
  virtual void doit();

  // Configuration. Override these appropriatly in subclasses.
  virtual VMOp_Type type() const                  { return VMOp_JRHotMethodSampler; }
  virtual Mode evaluation_mode() const;
};

class JRHotMethodSamplerTaskManager : public AllStatic {
 private:
  static JRHotMethodSamplerTask* _task;

 public:
  static void engage(size_t interval_time);
  static void disengage();
};

// This VM operation should cool down methods
class JRCoolDownMethods: public VM_Operation {
 public:
  JRCoolDownMethods();
  virtual ~JRCoolDownMethods();

  // Do the sampling
  virtual void doit();

  // Configuration. Override these appropriatly in subclasses.
  virtual VMOp_Type type() const                  { return VMOp_JRCoolDownMethods; }
  virtual Mode evaluation_mode() const;
};

class JRCoolDownMethodsTaskManager : public AllStatic {
 private:
  static JRCoolDownMethodsTask* _task;

 public:
  static void engage(size_t interval_time);
  static void disengage();
};

#if 0
class JRHotMethodSamplerTask;
class JRHotFieldCollectorTask;

// Allow for symbol counting
class JRCountedSymbol : public CHeapObj {
 private:
  const Symbol* _sym;
  int _count;

 public:
  JRCountedSymbol(const Symbol* sym) : _sym(sym), _count(1) {}

  const Symbol* getSymbol() const { return _sym; }
  int getCount() const            { return _count; }

  void increment()                { _count++; }

  static int compare(JRCountedSymbol** l, JRCountedSymbol** r) {
    return (*r)->getCount() - (*l)->getCount();
  }
};

// Container for hot classes
class JRHotKlassList : public AllStatic {
 private:
  static GrowableArray<JRCountedSymbol*>* klasses;
  static int add_count;

 public:
  static void doKlasses(void (*f)(const Symbol* klass));

  static void addKlass(const Symbol* klass);
  static void flush();
  
  static void sort();

  static GrowableArray<const Symbol*> matchHeapOopsWithHotKlasses(size_t cutoff);
  static void print();
}; 

// This VM operation should collect the top method in all threads
class JRHotMethodSampler: public VM_Operation {
 public:
  JRHotMethodSampler();
  virtual ~JRHotMethodSampler();

  // Do the sampling
  virtual void doit();

  // Configuration. Override these appropriatly in subclasses.
  virtual VMOp_Type type() const                  { return VMOp_JRHotMethodSampler; }
  virtual Mode evaluation_mode() const;
};

class JRHotFieldCollector: public VM_Operation {
 public:
  JRHotFieldCollector();
  virtual ~JRHotFieldCollector();

  // Do the collecting
  virtual void doit();

  // Configuration. Override these appropriatly in subclasses.
  virtual VMOp_Type type() const                  { return VMOp_JRHotFieldCollector; }
  virtual Mode evaluation_mode() const;
};

class JRHotMethodSamplerTaskManager : public AllStatic {
 private:
  static JRHotMethodSamplerTask* _task;

 public:
  static void engage(size_t interval_time);
  static void disengage();
};

class JRHotFieldCollectorTaskManager : public AllStatic {
 private:
  static JRHotFieldCollectorTask* _task;

 public:
  static void engage(size_t interval_time);
  static void disengage();
};
#endif

#endif // _SHARE_VM_RUNTIME_JR_VM_OPERATIONS_HPP_
