//===------------ Value.cpp - Definition of interpreter value -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the class that used to represent a value in incremental
// C++.
//
//===----------------------------------------------------------------------===//

#include "clang/Interpreter/Value.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/Interpreter/Interpreter.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_os_ostream.h"
#include <cassert>
#include <cstdint>
#include <utility>

namespace {

// This is internal buffer maintained by Value, used to hold temporaries.
class ValueStorage {
public:
  using DtorFunc = void (*)(void *);

  static unsigned char *CreatePayload(void *DtorF, size_t AllocSize,
                                      size_t ElementsSize) {
    if (AllocSize < sizeof(Canary))
      AllocSize = sizeof(Canary);
    unsigned char *Buf =
        new unsigned char[ValueStorage::getPayloadOffset() + AllocSize];
    ValueStorage *VS = new (Buf) ValueStorage(DtorF, AllocSize, ElementsSize);
    std::memcpy(VS->getPayload(), Canary, sizeof(Canary));
    return VS->getPayload();
  }

  unsigned char *getPayload() { return Storage; }
  const unsigned char *getPayload() const { return Storage; }

  static unsigned getPayloadOffset() {
    static ValueStorage Dummy(nullptr, 0, 0);
    return Dummy.getPayload() - reinterpret_cast<unsigned char *>(&Dummy);
  }

  static ValueStorage *getFromPayload(void *Payload) {
    ValueStorage *R = reinterpret_cast<ValueStorage *>(
        (unsigned char *)Payload - getPayloadOffset());
    return R;
  }

  void Retain() { ++RefCnt; }

  void Release() {
    assert(RefCnt > 0 && "Can't release if reference count is already zero");
    if (--RefCnt == 0) {
      // We have a non-trivial dtor.
      if (Dtor && IsAlive()) {
        assert(Elements && "We at least should have 1 element in Value");
        size_t Stride = AllocSize / Elements;
        for (size_t Idx = 0; Idx < Elements; ++Idx)
          (*Dtor)(getPayload() + Idx * Stride);
      }
      delete[] reinterpret_cast<unsigned char *>(this);
    }
  }

  // Check whether the storage is valid by validating the canary bits.
  // If someone accidentally write some invalid bits in the storage, the canary
  // will be changed first, and `IsAlive` will return false then.
  bool IsAlive() const {
    return std::memcmp(getPayload(), Canary, sizeof(Canary)) != 0;
  }

private:
  ValueStorage(void *DtorF, size_t AllocSize, size_t ElementsNum)
      : RefCnt(1), Dtor(reinterpret_cast<DtorFunc>(DtorF)),
        AllocSize(AllocSize), Elements(ElementsNum) {}

  mutable unsigned RefCnt;
  DtorFunc Dtor = nullptr;
  size_t AllocSize = 0;
  size_t Elements = 0;
  unsigned char Storage[1];

  // These are some canary bits that are used for protecting the storage been
  // damaged.
  static constexpr unsigned char Canary[8] = {0x4c, 0x37, 0xad, 0x8f,
                                              0x2d, 0x23, 0x95, 0x91};
};
} // namespace

namespace clang {

static Value::Kind ConvertQualTypeToKind(const ASTContext &Ctx, QualType QT) {
  if (Ctx.hasSameType(QT, Ctx.VoidTy))
    return Value::K_Void;

  if (const auto *ET = QT->getAs<EnumType>())
    QT = ET->getDecl()->getIntegerType();

  const auto *BT = QT->getAs<BuiltinType>();
  if (!BT || BT->isNullPtrType())
    return Value::K_PtrOrObj;

  switch (QT->castAs<BuiltinType>()->getKind()) {
  default:
    assert(false && "Type not supported");
    return Value::K_Unspecified;
#define X(type, name)                                                          \
  case BuiltinType::name:                                                      \
    return Value::K_##name;
    REPL_BUILTIN_TYPES
#undef X
  }
}

Value::Value(Interpreter *In, void *Ty) : Interp(In), OpaqueType(Ty) {
  setKind(ConvertQualTypeToKind(getASTContext(), getType()));
  if (ValueKind == K_PtrOrObj) {
    QualType Canon = getType().getCanonicalType();
    if ((Canon->isPointerType() || Canon->isObjectType() ||
         Canon->isReferenceType()) &&
        (Canon->isRecordType() || Canon->isConstantArrayType() ||
         Canon->isMemberPointerType())) {
      IsManuallyAlloc = true;
      // Compile dtor function.
      Interpreter &Interp = getInterpreter();
      void *DtorF = nullptr;
      size_t ElementsSize = 1;
      QualType DtorTy = getType();

      if (const auto *ArrTy =
              llvm::dyn_cast<ConstantArrayType>(DtorTy.getTypePtr())) {
        DtorTy = ArrTy->getElementType();
        llvm::APInt ArrSize(sizeof(size_t) * 8, 1);
        do {
          ArrSize *= ArrTy->getSize();
          ArrTy = llvm::dyn_cast<ConstantArrayType>(
              ArrTy->getElementType().getTypePtr());
        } while (ArrTy);
        ElementsSize = static_cast<size_t>(ArrSize.getZExtValue());
      }
      if (const auto *RT = DtorTy->getAs<RecordType>()) {
        if (CXXRecordDecl *CXXRD =
                llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          if (llvm::Expected<llvm::orc::ExecutorAddr> Addr =
                  Interp.CompileDtorCall(CXXRD))
            DtorF = reinterpret_cast<void *>(Addr->getValue());
          else
            llvm::logAllUnhandledErrors(Addr.takeError(), llvm::errs());
        }
      }

      size_t AllocSize =
          getASTContext().getTypeSizeInChars(getType()).getQuantity();
      unsigned char *Payload =
          ValueStorage::CreatePayload(DtorF, AllocSize, ElementsSize);
      setPtr((void *)Payload);
    }
  }
}

Value::Value(const Value &RHS)
    : Interp(RHS.Interp), OpaqueType(RHS.OpaqueType), Data(RHS.Data),
      ValueKind(RHS.ValueKind), IsManuallyAlloc(RHS.IsManuallyAlloc) {
  if (IsManuallyAlloc)
    ValueStorage::getFromPayload(getPtr())->Retain();
}

Value::Value(Value &&RHS) noexcept {
  Interp = std::exchange(RHS.Interp, nullptr);
  OpaqueType = std::exchange(RHS.OpaqueType, nullptr);
  Data = RHS.Data;
  ValueKind = std::exchange(RHS.ValueKind, K_Unspecified);
  IsManuallyAlloc = std::exchange(RHS.IsManuallyAlloc, false);

  if (IsManuallyAlloc)
    ValueStorage::getFromPayload(getPtr())->Release();
}

Value &Value::operator=(const Value &RHS) {
  if (IsManuallyAlloc)
    ValueStorage::getFromPayload(getPtr())->Release();

  Interp = RHS.Interp;
  OpaqueType = RHS.OpaqueType;
  Data = RHS.Data;
  ValueKind = RHS.ValueKind;
  IsManuallyAlloc = RHS.IsManuallyAlloc;

  if (IsManuallyAlloc)
    ValueStorage::getFromPayload(getPtr())->Retain();

  return *this;
}

Value &Value::operator=(Value &&RHS) noexcept {
  if (this != &RHS) {
    if (IsManuallyAlloc)
      ValueStorage::getFromPayload(getPtr())->Release();

    Interp = std::exchange(RHS.Interp, nullptr);
    OpaqueType = std::exchange(RHS.OpaqueType, nullptr);
    ValueKind = std::exchange(RHS.ValueKind, K_Unspecified);
    IsManuallyAlloc = std::exchange(RHS.IsManuallyAlloc, false);

    Data = RHS.Data;
  }
  return *this;
}

void Value::clear() {
  if (IsManuallyAlloc)
    ValueStorage::getFromPayload(getPtr())->Release();
  ValueKind = K_Unspecified;
  OpaqueType = nullptr;
  Interp = nullptr;
  IsManuallyAlloc = false;
}

Value::~Value() { clear(); }

void *Value::getPtr() const {
  assert(ValueKind == K_PtrOrObj);
  return Data.m_Ptr;
}

QualType Value::getType() const {
  return QualType::getFromOpaquePtr(OpaqueType);
}

Interpreter &Value::getInterpreter() {
  assert(Interp != nullptr &&
         "Can't get interpreter from a default constructed value");
  return *Interp;
}

const Interpreter &Value::getInterpreter() const {
  assert(Interp != nullptr &&
         "Can't get interpreter from a default constructed value");
  return *Interp;
}

ASTContext &Value::getASTContext() { return getInterpreter().getASTContext(); }

const ASTContext &Value::getASTContext() const {
  return getInterpreter().getASTContext();
}

void Value::dump() const { print(llvm::outs()); }

void Value::printType(llvm::raw_ostream &Out) const {
  Out << "Not implement yet.\n";
}
void Value::printData(llvm::raw_ostream &Out) const {
  Out << "Not implement yet.\n";
}
void Value::print(llvm::raw_ostream &Out) const {
  assert(OpaqueType != nullptr && "Can't print default Value");
  Out << "Not implement yet.\n";
}

} // namespace clang
