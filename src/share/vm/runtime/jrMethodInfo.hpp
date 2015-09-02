#ifndef _SHARE_VM_RUNTIME_JR_METHOD_INFO_HPP_
#define _SHARE_VM_RUNTIME_JR_METHOD_INFO_HPP_

#include "oops/method.hpp"
#include "ci/ciSymbol.hpp"

#if 0
class JRMethodInfoManager;

class JRMethodInfoAccessList : public CHeapObj {
 private:
  GrowableArray<const Symbol*>* _access_list;

  void print_helper(const Symbol* sym) const;

 public:
  JRMethodInfoAccessList();
  virtual ~JRMethodInfoAccessList();
  
  void addAccess(const ciSymbol* access);

  void doAccesses(void (*f)(const Symbol*)) const;

  void print() const;
};

class JRMethodInfo : public CHeapObj {
  friend class JRMethodInfoManager;

 private:
  const Symbol* _klass;
  const Symbol* _method;
  const Symbol* _signature;

  const JRMethodInfoAccessList* _access_list;

  JRMethodInfo(const ciSymbol* klass, const ciSymbol* method, const ciSymbol* signature, const JRMethodInfoAccessList* access_list);
  ~JRMethodInfo();
  
 public:

  const Symbol* getKlass() const      { return _klass; }
  const Symbol* getMethod() const     { return _method; }
  const Symbol* getSignature() const  { return _signature; }

  const JRMethodInfoAccessList* getAccesses() const { return _access_list; }

  const methodOop getMethodOop() const;

  const void print() const;
  const void print2() const;
};

class JRMethodInfoManager : public AllStatic {
 private:
  static GrowableArray<const JRMethodInfo*>* _infos;
  
 public:
  static const JRMethodInfo* make(const ciSymbol* klass, const ciSymbol* method, const ciSymbol* signature, const JRMethodInfoAccessList* access_list);

  static const JRMethodInfo* getMethodInfo(const ciSymbol* klass, const ciSymbol* method, const ciSymbol* signature);
  
  static const JRMethodInfo* getMethodInfo(const Symbol* klass, const Symbol* method, const Symbol* signature);

  static const Symbol* resolve_ciSymbol(const ciSymbol* sym);

  static void printAll();
};
#endif

class JRMethodInfoManager : public AllStatic {
 private:
  static GrowableArray<Method*>* _methods;
  static GrowableArray<Klass*>* _klasses;
 public:
  static void cool_down_methods();
  static void cool_down_klasses();
  static int methods_length() { return _methods->length(); }
  static int klasses_length() { return _klasses->length(); }
  static void add_method(Method* m);
  static void add_klass(Klass* k);

  static bool kal_contains(GrowableArray<Klass*>* kal, Klass* k);
};

#endif // _SHARE_VM_RUNTIME_METHOD_INFO_HPP_
