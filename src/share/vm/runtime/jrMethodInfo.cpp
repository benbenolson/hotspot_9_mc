#include "utilities/growableArray.hpp"
#include "runtime/jrMethodInfo.hpp"

GrowableArray<Method*>* JRMethodInfoManager::_methods = new (ResourceObj::C_HEAP, mtInternal) GrowableArray<Method*>(512, true);
GrowableArray<Klass*>*  JRMethodInfoManager::_klasses = new (ResourceObj::C_HEAP, mtInternal) GrowableArray<Klass*> (512, true);

void JRMethodInfoManager::add_method(Method* m) {
  for (int i = 0; i < _methods->length(); i++) {
    if ( _methods->at(i) == m ) {
      return;
    }
  }
  _methods->append(m);
}

void JRMethodInfoManager::add_klass(Klass* k) {
  for (int i = 0; i < _klasses->length(); i++) {
    if ( _klasses->at(i) == k ) {
      return;
    }
  }
  _klasses->append(k);
}

bool JRMethodInfoManager::kal_contains(GrowableArray<Klass*>* kal, Klass* k) {
  if (kal) {
    for (int i = 0; i < kal->length(); i++) {
      if ( kal->at(i) == k ) {
        return true;
      }
    }
  }
  return false;
}

void JRMethodInfoManager::cool_down_methods() {
  for (int i = 0; i < _methods->length(); i++) {
    _methods->at(i)->cool_down();
  }
}

void JRMethodInfoManager::cool_down_klasses() {
  for (int i = 0; i < _klasses->length(); i++) {
    _klasses->at(i)->cool_down();
  }
}

