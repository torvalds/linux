//===- ExtractAPI/API.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the APIRecord and derived record structs,
/// and the APISet class.
///
//===----------------------------------------------------------------------===//

#include "clang/ExtractAPI/API.h"
#include "clang/AST/RawCommentList.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>

using namespace clang::extractapi;
using namespace llvm;

SymbolReference::SymbolReference(const APIRecord *R)
    : Name(R->Name), USR(R->USR), Record(R) {}

APIRecord *APIRecord::castFromRecordContext(const RecordContext *Ctx) {
  switch (Ctx->getKind()) {
#define RECORD_CONTEXT(CLASS, KIND)                                            \
  case KIND:                                                                   \
    return static_cast<CLASS *>(const_cast<RecordContext *>(Ctx));
#include "clang/ExtractAPI/APIRecords.inc"
  default:
    return nullptr;
    // llvm_unreachable("RecordContext derived class isn't propertly
    // implemented");
  }
}

RecordContext *APIRecord::castToRecordContext(const APIRecord *Record) {
  if (!Record)
    return nullptr;
  switch (Record->getKind()) {
#define RECORD_CONTEXT(CLASS, KIND)                                            \
  case KIND:                                                                   \
    return static_cast<CLASS *>(const_cast<APIRecord *>(Record));
#include "clang/ExtractAPI/APIRecords.inc"
  default:
    return nullptr;
    // llvm_unreachable("RecordContext derived class isn't propertly
    // implemented");
  }
}

bool RecordContext::IsWellFormed() const {
  // Check that First and Last are both null or both non-null.
  return (First == nullptr) == (Last == nullptr);
}

void RecordContext::stealRecordChain(RecordContext &Other) {
  assert(IsWellFormed());
  // If we don't have an empty chain append Other's chain into ours.
  if (First)
    Last->NextInContext = Other.First;
  else
    First = Other.First;

  Last = Other.Last;

  // Delete Other's chain to ensure we don't accidentally traverse it.
  Other.First = nullptr;
  Other.Last = nullptr;
}

void RecordContext::addToRecordChain(APIRecord *Record) const {
  assert(IsWellFormed());
  if (!First) {
    First = Record;
    Last = Record;
    return;
  }

  Last->NextInContext = Record;
  Last = Record;
}

APIRecord *APISet::findRecordForUSR(StringRef USR) const {
  if (USR.empty())
    return nullptr;

  auto FindIt = USRBasedLookupTable.find(USR);
  if (FindIt != USRBasedLookupTable.end())
    return FindIt->getSecond().get();

  return nullptr;
}

StringRef APISet::copyString(StringRef String) {
  if (String.empty())
    return {};

  // No need to allocate memory and copy if the string has already been stored.
  if (Allocator.identifyObject(String.data()))
    return String;

  void *Ptr = Allocator.Allocate(String.size(), 1);
  memcpy(Ptr, String.data(), String.size());
  return StringRef(reinterpret_cast<const char *>(Ptr), String.size());
}

SymbolReference APISet::createSymbolReference(StringRef Name, StringRef USR,
                                              StringRef Source) {
  return SymbolReference(copyString(Name), copyString(USR), copyString(Source));
}

APIRecord::~APIRecord() {}
TagRecord::~TagRecord() {}
RecordRecord::~RecordRecord() {}
RecordFieldRecord::~RecordFieldRecord() {}
ObjCContainerRecord::~ObjCContainerRecord() {}
ObjCMethodRecord::~ObjCMethodRecord() {}
ObjCPropertyRecord::~ObjCPropertyRecord() {}
CXXMethodRecord::~CXXMethodRecord() {}

void GlobalFunctionRecord::anchor() {}
void GlobalVariableRecord::anchor() {}
void EnumConstantRecord::anchor() {}
void EnumRecord::anchor() {}
void StructFieldRecord::anchor() {}
void StructRecord::anchor() {}
void UnionFieldRecord::anchor() {}
void UnionRecord::anchor() {}
void CXXFieldRecord::anchor() {}
void CXXClassRecord::anchor() {}
void CXXConstructorRecord::anchor() {}
void CXXDestructorRecord::anchor() {}
void CXXInstanceMethodRecord::anchor() {}
void CXXStaticMethodRecord::anchor() {}
void ObjCInstancePropertyRecord::anchor() {}
void ObjCClassPropertyRecord::anchor() {}
void ObjCInstanceVariableRecord::anchor() {}
void ObjCInstanceMethodRecord::anchor() {}
void ObjCClassMethodRecord::anchor() {}
void ObjCCategoryRecord::anchor() {}
void ObjCInterfaceRecord::anchor() {}
void ObjCProtocolRecord::anchor() {}
void MacroDefinitionRecord::anchor() {}
void TypedefRecord::anchor() {}
