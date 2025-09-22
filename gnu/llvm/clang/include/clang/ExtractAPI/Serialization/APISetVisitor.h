//===- ExtractAPI/Serialization/APISetVisitor.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the ExtractAPI APISetVisitor interface.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXTRACTAPI_SERIALIZATION_SERIALIZERBASE_H
#define LLVM_CLANG_EXTRACTAPI_SERIALIZATION_SERIALIZERBASE_H

#include "clang/ExtractAPI/API.h"

namespace clang {
namespace extractapi {

// A helper macro to implement short-circuiting when recursing.  It
// invokes CALL_EXPR, which must be a method call, on the derived
// object (s.t. a user of RecursiveASTVisitor can override the method
// in CALL_EXPR).
#define TRY_TO(CALL_EXPR)                                                      \
  do {                                                                         \
    if (!getDerived()->CALL_EXPR)                                              \
      return false;                                                            \
  } while (false)

/// The base interface of visitors for API information, the interface and usage
/// is almost identical to RecurisveASTVistor. This class performs three
/// distinct tasks:
/// 1. traverse the APISet (i.e. go to every record);
/// 2. at a given record, walk up the class hierarchy starting from the record's
/// dynamic type until APIRecord is reached.
/// 3. given a (record, class) combination where 'class' is some base class of
/// the dynamic type of 'record', call a user-overridable function to actually
/// visit the record.
///
/// These tasks are done by three groups of methods, respectively:
/// 1. traverseRecord(APIRecord *x) does task #1, it is the entry point for
/// traversing the records starting from x. This method simply forwards to
/// traverseFoo(Foo *x) where Foo is the dynamic type of *x, which calls
/// walkUpFromFoo(x) and then recursively visits the child records of x.
/// 2. walkUpFromFoo(Foo *x) does task #2. It doesn't visit children records of
/// x, instead it first calls walkUpFromBar(x) where Bar is the direct parent
/// class of Foo (unless Foo has no parent) and then calls visitFoo(x).
/// 3. visitFoo(Foo *x) does task #3.
///
/// These three method groups are tiered (traverse* > walkUpFrom* >
/// visit*).  A method (e.g. traverse*) may call methods from the same
/// tier (e.g. other traverse*) or one tier lower (e.g. walkUpFrom*).
/// It may not call methods from a higher tier.
///
/// Note that since walkUpFromFoo() calls walkUpFromBar() (where Bar
/// is Foo's super class) before calling visitFoo(), the result is
/// that the visit*() methods for a given record are called in the
/// top-down order (e.g. for a record of type ObjCInstancePropertyRecord, the
/// order will be visitRecord(), visitObjCPropertyRecord(), and then
/// visitObjCInstancePropertyRecord()).
///
/// This scheme guarantees that all visit*() calls for the same record
/// are grouped together.  In other words, visit*() methods for different
/// records are never interleaved.
///
/// Clients of this visitor should subclass the visitor (providing
/// themselves as the template argument, using the curiously recurring
/// template pattern) and override any of the traverse*, walkUpFrom*,
/// and visit* methods for records where the visitor should customize
/// behavior.  Most users only need to override visit*.  Advanced
/// users may override traverse* and walkUpFrom* to implement custom
/// traversal strategies.  Returning false from one of these overridden
/// functions will abort the entire traversal.
template <typename Derived> class APISetVisitor {
public:
  bool traverseAPISet() {
    for (const APIRecord *TLR : API.getTopLevelRecords()) {
      TRY_TO(traverseAPIRecord(TLR));
    }
    return true;
  }

  bool traverseAPIRecord(const APIRecord *Record);
  bool walkUpFromAPIRecord(const APIRecord *Record) {
    TRY_TO(visitAPIRecord(Record));
    return true;
  }
  bool visitAPIRecord(const APIRecord *Record) { return true; }

#define GENERATE_TRAVERSE_METHOD(CLASS, BASE)                                  \
  bool traverse##CLASS(const CLASS *Record) {                                  \
    TRY_TO(walkUpFrom##CLASS(Record));                                         \
    TRY_TO(traverseRecordContext(dyn_cast<RecordContext>(Record)));            \
    return true;                                                               \
  }

#define GENERATE_WALKUP_AND_VISIT_METHODS(CLASS, BASE)                         \
  bool walkUpFrom##CLASS(const CLASS *Record) {                                \
    TRY_TO(walkUpFrom##BASE(Record));                                          \
    TRY_TO(visit##CLASS(Record));                                              \
    return true;                                                               \
  }                                                                            \
  bool visit##CLASS(const CLASS *Record) { return true; }

#define CONCRETE_RECORD(CLASS, BASE, KIND)                                     \
  GENERATE_TRAVERSE_METHOD(CLASS, BASE)                                        \
  GENERATE_WALKUP_AND_VISIT_METHODS(CLASS, BASE)

#define ABSTRACT_RECORD(CLASS, BASE)                                           \
  GENERATE_WALKUP_AND_VISIT_METHODS(CLASS, BASE)

#include "../APIRecords.inc"

#undef GENERATE_WALKUP_AND_VISIT_METHODS
#undef GENERATE_TRAVERSE_METHOD

  bool traverseRecordContext(const RecordContext *);

protected:
  const APISet &API;

public:
  APISetVisitor() = delete;
  APISetVisitor(const APISetVisitor &) = delete;
  APISetVisitor(APISetVisitor &&) = delete;
  APISetVisitor &operator=(const APISetVisitor &) = delete;
  APISetVisitor &operator=(APISetVisitor &&) = delete;

protected:
  APISetVisitor(const APISet &API) : API(API) {}
  ~APISetVisitor() = default;

  Derived *getDerived() { return static_cast<Derived *>(this); };
};

template <typename Derived>
bool APISetVisitor<Derived>::traverseRecordContext(
    const RecordContext *Context) {
  if (!Context)
    return true;

  for (auto *Child : Context->records())
    TRY_TO(traverseAPIRecord(Child));

  return true;
}

template <typename Derived>
bool APISetVisitor<Derived>::traverseAPIRecord(const APIRecord *Record) {
  switch (Record->getKind()) {
#define CONCRETE_RECORD(CLASS, BASE, KIND)                                     \
  case APIRecord::KIND: {                                                      \
    TRY_TO(traverse##CLASS(static_cast<const CLASS *>(Record)));               \
    break;                                                                     \
  }
#include "../APIRecords.inc"
  case APIRecord::RK_Unknown: {
    TRY_TO(walkUpFromAPIRecord(static_cast<const APIRecord *>(Record)));
    break;
  }
  default:
    llvm_unreachable("API Record with uninstantiable kind");
  }
  return true;
}

} // namespace extractapi
} // namespace clang

#endif // LLVM_CLANG_EXTRACTAPI_SERIALIZATION_SERIALIZERBASE_H
