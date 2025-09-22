//===--- Descriptor.cpp - Types for the constexpr VM ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Descriptor.h"
#include "Boolean.h"
#include "Floating.h"
#include "FunctionPointer.h"
#include "IntegralAP.h"
#include "MemberPointer.h"
#include "Pointer.h"
#include "PrimType.h"
#include "Record.h"

using namespace clang;
using namespace clang::interp;

template <typename T>
static void ctorTy(Block *, std::byte *Ptr, bool, bool, bool,
                   const Descriptor *) {
  new (Ptr) T();
}

template <typename T>
static void dtorTy(Block *, std::byte *Ptr, const Descriptor *) {
  reinterpret_cast<T *>(Ptr)->~T();
}

template <typename T>
static void moveTy(Block *, const std::byte *Src, std::byte *Dst,
                   const Descriptor *) {
  const auto *SrcPtr = reinterpret_cast<const T *>(Src);
  auto *DstPtr = reinterpret_cast<T *>(Dst);
  new (DstPtr) T(std::move(*SrcPtr));
}

template <typename T>
static void ctorArrayTy(Block *, std::byte *Ptr, bool, bool, bool,
                        const Descriptor *D) {
  new (Ptr) InitMapPtr(std::nullopt);

  Ptr += sizeof(InitMapPtr);
  for (unsigned I = 0, NE = D->getNumElems(); I < NE; ++I) {
    new (&reinterpret_cast<T *>(Ptr)[I]) T();
  }
}

template <typename T>
static void dtorArrayTy(Block *, std::byte *Ptr, const Descriptor *D) {
  InitMapPtr &IMP = *reinterpret_cast<InitMapPtr *>(Ptr);

  if (IMP)
    IMP = std::nullopt;
  Ptr += sizeof(InitMapPtr);
  for (unsigned I = 0, NE = D->getNumElems(); I < NE; ++I) {
    reinterpret_cast<T *>(Ptr)[I].~T();
  }
}

template <typename T>
static void moveArrayTy(Block *, const std::byte *Src, std::byte *Dst,
                        const Descriptor *D) {
  // FIXME: Get rid of the const_cast.
  InitMapPtr &SrcIMP =
      *reinterpret_cast<InitMapPtr *>(const_cast<std::byte *>(Src));
  if (SrcIMP) {
    // We only ever invoke the moveFunc when moving block contents to a
    // DeadBlock. DeadBlocks don't need InitMaps, so we destroy them here.
    SrcIMP = std::nullopt;
  }
  Src += sizeof(InitMapPtr);
  Dst += sizeof(InitMapPtr);
  for (unsigned I = 0, NE = D->getNumElems(); I < NE; ++I) {
    const auto *SrcPtr = &reinterpret_cast<const T *>(Src)[I];
    auto *DstPtr = &reinterpret_cast<T *>(Dst)[I];
    new (DstPtr) T(std::move(*SrcPtr));
  }
}

static void ctorArrayDesc(Block *B, std::byte *Ptr, bool IsConst,
                          bool IsMutable, bool IsActive, const Descriptor *D) {
  const unsigned NumElems = D->getNumElems();
  const unsigned ElemSize =
      D->ElemDesc->getAllocSize() + sizeof(InlineDescriptor);

  unsigned ElemOffset = 0;
  for (unsigned I = 0; I < NumElems; ++I, ElemOffset += ElemSize) {
    auto *ElemPtr = Ptr + ElemOffset;
    auto *Desc = reinterpret_cast<InlineDescriptor *>(ElemPtr);
    auto *ElemLoc = reinterpret_cast<std::byte *>(Desc + 1);
    auto *SD = D->ElemDesc;

    Desc->Offset = ElemOffset + sizeof(InlineDescriptor);
    Desc->Desc = SD;
    Desc->IsInitialized = true;
    Desc->IsBase = false;
    Desc->IsActive = IsActive;
    Desc->IsConst = IsConst || D->IsConst;
    Desc->IsFieldMutable = IsMutable || D->IsMutable;
    if (auto Fn = D->ElemDesc->CtorFn)
      Fn(B, ElemLoc, Desc->IsConst, Desc->IsFieldMutable, IsActive,
         D->ElemDesc);
  }
}

static void dtorArrayDesc(Block *B, std::byte *Ptr, const Descriptor *D) {
  const unsigned NumElems = D->getNumElems();
  const unsigned ElemSize =
      D->ElemDesc->getAllocSize() + sizeof(InlineDescriptor);

  unsigned ElemOffset = 0;
  for (unsigned I = 0; I < NumElems; ++I, ElemOffset += ElemSize) {
    auto *ElemPtr = Ptr + ElemOffset;
    auto *Desc = reinterpret_cast<InlineDescriptor *>(ElemPtr);
    auto *ElemLoc = reinterpret_cast<std::byte *>(Desc + 1);
    if (auto Fn = D->ElemDesc->DtorFn)
      Fn(B, ElemLoc, D->ElemDesc);
  }
}

static void moveArrayDesc(Block *B, const std::byte *Src, std::byte *Dst,
                          const Descriptor *D) {
  const unsigned NumElems = D->getNumElems();
  const unsigned ElemSize =
      D->ElemDesc->getAllocSize() + sizeof(InlineDescriptor);

  unsigned ElemOffset = 0;
  for (unsigned I = 0; I < NumElems; ++I, ElemOffset += ElemSize) {
    const auto *SrcPtr = Src + ElemOffset;
    auto *DstPtr = Dst + ElemOffset;

    const auto *SrcDesc = reinterpret_cast<const InlineDescriptor *>(SrcPtr);
    const auto *SrcElemLoc = reinterpret_cast<const std::byte *>(SrcDesc + 1);
    auto *DstDesc = reinterpret_cast<InlineDescriptor *>(DstPtr);
    auto *DstElemLoc = reinterpret_cast<std::byte *>(DstDesc + 1);

    *DstDesc = *SrcDesc;
    if (auto Fn = D->ElemDesc->MoveFn)
      Fn(B, SrcElemLoc, DstElemLoc, D->ElemDesc);
  }
}

static void initField(Block *B, std::byte *Ptr, bool IsConst, bool IsMutable,
                      bool IsActive, bool IsUnion, const Descriptor *D,
                      unsigned FieldOffset) {
  auto *Desc = reinterpret_cast<InlineDescriptor *>(Ptr + FieldOffset) - 1;
  Desc->Offset = FieldOffset;
  Desc->Desc = D;
  Desc->IsInitialized = D->IsArray;
  Desc->IsBase = false;
  Desc->IsActive = IsActive && !IsUnion;
  Desc->IsConst = IsConst || D->IsConst;
  Desc->IsFieldMutable = IsMutable || D->IsMutable;

  if (auto Fn = D->CtorFn)
    Fn(B, Ptr + FieldOffset, Desc->IsConst, Desc->IsFieldMutable,
       Desc->IsActive, D);
}

static void initBase(Block *B, std::byte *Ptr, bool IsConst, bool IsMutable,
                     bool IsActive, const Descriptor *D, unsigned FieldOffset,
                     bool IsVirtualBase) {
  assert(D);
  assert(D->ElemRecord);

  bool IsUnion = D->ElemRecord->isUnion();
  auto *Desc = reinterpret_cast<InlineDescriptor *>(Ptr + FieldOffset) - 1;
  Desc->Offset = FieldOffset;
  Desc->Desc = D;
  Desc->IsInitialized = D->IsArray;
  Desc->IsBase = true;
  Desc->IsVirtualBase = IsVirtualBase;
  Desc->IsActive = IsActive && !IsUnion;
  Desc->IsConst = IsConst || D->IsConst;
  Desc->IsFieldMutable = IsMutable || D->IsMutable;

  for (const auto &V : D->ElemRecord->bases())
    initBase(B, Ptr + FieldOffset, IsConst, IsMutable, IsActive, V.Desc,
             V.Offset, false);
  for (const auto &F : D->ElemRecord->fields())
    initField(B, Ptr + FieldOffset, IsConst, IsMutable, IsActive, IsUnion,
              F.Desc, F.Offset);
}

static void ctorRecord(Block *B, std::byte *Ptr, bool IsConst, bool IsMutable,
                       bool IsActive, const Descriptor *D) {
  for (const auto &V : D->ElemRecord->bases())
    initBase(B, Ptr, IsConst, IsMutable, IsActive, V.Desc, V.Offset, false);
  for (const auto &F : D->ElemRecord->fields())
    initField(B, Ptr, IsConst, IsMutable, IsActive, D->ElemRecord->isUnion(), F.Desc, F.Offset);
  for (const auto &V : D->ElemRecord->virtual_bases())
    initBase(B, Ptr, IsConst, IsMutable, IsActive, V.Desc, V.Offset, true);
}

static void destroyField(Block *B, std::byte *Ptr, const Descriptor *D,
                         unsigned FieldOffset) {
  if (auto Fn = D->DtorFn)
    Fn(B, Ptr + FieldOffset, D);
}

static void destroyBase(Block *B, std::byte *Ptr, const Descriptor *D,
                        unsigned FieldOffset) {
  assert(D);
  assert(D->ElemRecord);

  for (const auto &V : D->ElemRecord->bases())
    destroyBase(B, Ptr + FieldOffset, V.Desc, V.Offset);
  for (const auto &F : D->ElemRecord->fields())
    destroyField(B, Ptr + FieldOffset, F.Desc, F.Offset);
}

static void dtorRecord(Block *B, std::byte *Ptr, const Descriptor *D) {
  for (const auto &F : D->ElemRecord->bases())
    destroyBase(B, Ptr, F.Desc, F.Offset);
  for (const auto &F : D->ElemRecord->fields())
    destroyField(B, Ptr, F.Desc, F.Offset);
  for (const auto &F : D->ElemRecord->virtual_bases())
    destroyBase(B, Ptr, F.Desc, F.Offset);
}

static void moveRecord(Block *B, const std::byte *Src, std::byte *Dst,
                       const Descriptor *D) {
  for (const auto &F : D->ElemRecord->fields()) {
    auto FieldOff = F.Offset;
    auto *FieldDesc = F.Desc;

    if (auto Fn = FieldDesc->MoveFn)
      Fn(B, Src + FieldOff, Dst + FieldOff, FieldDesc);
  }
}

static BlockCtorFn getCtorPrim(PrimType Type) {
  // Floating types are special. They are primitives, but need their
  // constructor called.
  if (Type == PT_Float)
    return ctorTy<PrimConv<PT_Float>::T>;
  if (Type == PT_IntAP)
    return ctorTy<PrimConv<PT_IntAP>::T>;
  if (Type == PT_IntAPS)
    return ctorTy<PrimConv<PT_IntAPS>::T>;
  if (Type == PT_MemberPtr)
    return ctorTy<PrimConv<PT_MemberPtr>::T>;

  COMPOSITE_TYPE_SWITCH(Type, return ctorTy<T>, return nullptr);
}

static BlockDtorFn getDtorPrim(PrimType Type) {
  // Floating types are special. They are primitives, but need their
  // destructor called, since they might allocate memory.
  if (Type == PT_Float)
    return dtorTy<PrimConv<PT_Float>::T>;
  if (Type == PT_IntAP)
    return dtorTy<PrimConv<PT_IntAP>::T>;
  if (Type == PT_IntAPS)
    return dtorTy<PrimConv<PT_IntAPS>::T>;
  if (Type == PT_MemberPtr)
    return dtorTy<PrimConv<PT_MemberPtr>::T>;

  COMPOSITE_TYPE_SWITCH(Type, return dtorTy<T>, return nullptr);
}

static BlockMoveFn getMovePrim(PrimType Type) {
  COMPOSITE_TYPE_SWITCH(Type, return moveTy<T>, return nullptr);
}

static BlockCtorFn getCtorArrayPrim(PrimType Type) {
  TYPE_SWITCH(Type, return ctorArrayTy<T>);
  llvm_unreachable("unknown Expr");
}

static BlockDtorFn getDtorArrayPrim(PrimType Type) {
  TYPE_SWITCH(Type, return dtorArrayTy<T>);
  llvm_unreachable("unknown Expr");
}

static BlockMoveFn getMoveArrayPrim(PrimType Type) {
  TYPE_SWITCH(Type, return moveArrayTy<T>);
  llvm_unreachable("unknown Expr");
}

/// Primitives.
Descriptor::Descriptor(const DeclTy &D, PrimType Type, MetadataSize MD,
                       bool IsConst, bool IsTemporary, bool IsMutable)
    : Source(D), ElemSize(primSize(Type)), Size(ElemSize),
      MDSize(MD.value_or(0)), AllocSize(align(Size + MDSize)), PrimT(Type),
      IsConst(IsConst), IsMutable(IsMutable), IsTemporary(IsTemporary),
      CtorFn(getCtorPrim(Type)), DtorFn(getDtorPrim(Type)),
      MoveFn(getMovePrim(Type)) {
  assert(AllocSize >= Size);
  assert(Source && "Missing source");
}

/// Primitive arrays.
Descriptor::Descriptor(const DeclTy &D, PrimType Type, MetadataSize MD,
                       size_t NumElems, bool IsConst, bool IsTemporary,
                       bool IsMutable)
    : Source(D), ElemSize(primSize(Type)), Size(ElemSize * NumElems),
      MDSize(MD.value_or(0)),
      AllocSize(align(MDSize) + align(Size) + sizeof(InitMapPtr)), PrimT(Type),
      IsConst(IsConst), IsMutable(IsMutable), IsTemporary(IsTemporary),
      IsArray(true), CtorFn(getCtorArrayPrim(Type)),
      DtorFn(getDtorArrayPrim(Type)), MoveFn(getMoveArrayPrim(Type)) {
  assert(Source && "Missing source");
  assert(NumElems <= (MaxArrayElemBytes / ElemSize));
}

/// Primitive unknown-size arrays.
Descriptor::Descriptor(const DeclTy &D, PrimType Type, MetadataSize MD,
                       bool IsTemporary, UnknownSize)
    : Source(D), ElemSize(primSize(Type)), Size(UnknownSizeMark),
      MDSize(MD.value_or(0)),
      AllocSize(MDSize + sizeof(InitMapPtr) + alignof(void *)), IsConst(true),
      IsMutable(false), IsTemporary(IsTemporary), IsArray(true),
      CtorFn(getCtorArrayPrim(Type)), DtorFn(getDtorArrayPrim(Type)),
      MoveFn(getMoveArrayPrim(Type)) {
  assert(Source && "Missing source");
}

/// Arrays of composite elements.
Descriptor::Descriptor(const DeclTy &D, const Descriptor *Elem, MetadataSize MD,
                       unsigned NumElems, bool IsConst, bool IsTemporary,
                       bool IsMutable)
    : Source(D), ElemSize(Elem->getAllocSize() + sizeof(InlineDescriptor)),
      Size(ElemSize * NumElems), MDSize(MD.value_or(0)),
      AllocSize(std::max<size_t>(alignof(void *), Size) + MDSize),
      ElemDesc(Elem), IsConst(IsConst), IsMutable(IsMutable),
      IsTemporary(IsTemporary), IsArray(true), CtorFn(ctorArrayDesc),
      DtorFn(dtorArrayDesc), MoveFn(moveArrayDesc) {
  assert(Source && "Missing source");
}

/// Unknown-size arrays of composite elements.
Descriptor::Descriptor(const DeclTy &D, const Descriptor *Elem, MetadataSize MD,
                       bool IsTemporary, UnknownSize)
    : Source(D), ElemSize(Elem->getAllocSize() + sizeof(InlineDescriptor)),
      Size(UnknownSizeMark), MDSize(MD.value_or(0)),
      AllocSize(MDSize + alignof(void *)), ElemDesc(Elem), IsConst(true),
      IsMutable(false), IsTemporary(IsTemporary), IsArray(true),
      CtorFn(ctorArrayDesc), DtorFn(dtorArrayDesc), MoveFn(moveArrayDesc) {
  assert(Source && "Missing source");
}

/// Composite records.
Descriptor::Descriptor(const DeclTy &D, const Record *R, MetadataSize MD,
                       bool IsConst, bool IsTemporary, bool IsMutable)
    : Source(D), ElemSize(std::max<size_t>(alignof(void *), R->getFullSize())),
      Size(ElemSize), MDSize(MD.value_or(0)), AllocSize(Size + MDSize),
      ElemRecord(R), IsConst(IsConst), IsMutable(IsMutable),
      IsTemporary(IsTemporary), CtorFn(ctorRecord), DtorFn(dtorRecord),
      MoveFn(moveRecord) {
  assert(Source && "Missing source");
}

/// Dummy.
Descriptor::Descriptor(const DeclTy &D)
    : Source(D), ElemSize(1), Size(1), MDSize(0), AllocSize(MDSize),
      ElemRecord(nullptr), IsConst(true), IsMutable(false), IsTemporary(false),
      IsDummy(true) {
  assert(Source && "Missing source");
}

QualType Descriptor::getType() const {
  if (const auto *E = asExpr())
    return E->getType();
  if (const auto *D = asValueDecl())
    return D->getType();
  if (const auto *T = dyn_cast<TypeDecl>(asDecl()))
    return QualType(T->getTypeForDecl(), 0);
  llvm_unreachable("Invalid descriptor type");
}

QualType Descriptor::getElemQualType() const {
  assert(isArray());
  QualType T = getType();
  if (const auto *AT = T->getAsArrayTypeUnsafe())
    return AT->getElementType();
  if (const auto *CT = T->getAs<ComplexType>())
    return CT->getElementType();
  if (const auto *CT = T->getAs<VectorType>())
    return CT->getElementType();
  llvm_unreachable("Array that's not an array/complex/vector type?");
}

SourceLocation Descriptor::getLocation() const {
  if (auto *D = Source.dyn_cast<const Decl *>())
    return D->getLocation();
  if (auto *E = Source.dyn_cast<const Expr *>())
    return E->getExprLoc();
  llvm_unreachable("Invalid descriptor type");
}

InitMap::InitMap(unsigned N)
    : UninitFields(N), Data(std::make_unique<T[]>(numFields(N))) {
  std::fill_n(data(), numFields(N), 0);
}

bool InitMap::initializeElement(unsigned I) {
  unsigned Bucket = I / PER_FIELD;
  T Mask = T(1) << (I % PER_FIELD);
  if (!(data()[Bucket] & Mask)) {
    data()[Bucket] |= Mask;
    UninitFields -= 1;
  }
  return UninitFields == 0;
}

bool InitMap::isElementInitialized(unsigned I) const {
  unsigned Bucket = I / PER_FIELD;
  return data()[Bucket] & (T(1) << (I % PER_FIELD));
}
