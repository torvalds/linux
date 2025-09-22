//===-- CGBuilder.h - Choose IRBuilder implementation  ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGBUILDER_H
#define LLVM_CLANG_LIB_CODEGEN_CGBUILDER_H

#include "Address.h"
#include "CGValue.h"
#include "CodeGenTypeCache.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

namespace clang {
namespace CodeGen {

class CGBuilderTy;
class CodeGenFunction;

/// This is an IRBuilder insertion helper that forwards to
/// CodeGenFunction::InsertHelper, which adds necessary metadata to
/// instructions.
class CGBuilderInserter final : public llvm::IRBuilderDefaultInserter {
  friend CGBuilderTy;

public:
  CGBuilderInserter() = default;
  explicit CGBuilderInserter(CodeGenFunction *CGF) : CGF(CGF) {}

  /// This forwards to CodeGenFunction::InsertHelper.
  void InsertHelper(llvm::Instruction *I, const llvm::Twine &Name,
                    llvm::BasicBlock::iterator InsertPt) const override;

private:
  CodeGenFunction *CGF = nullptr;
};

typedef CGBuilderInserter CGBuilderInserterTy;

typedef llvm::IRBuilder<llvm::ConstantFolder, CGBuilderInserterTy>
    CGBuilderBaseTy;

class CGBuilderTy : public CGBuilderBaseTy {
  friend class Address;

  /// Storing a reference to the type cache here makes it a lot easier
  /// to build natural-feeling, target-specific IR.
  const CodeGenTypeCache &TypeCache;

  CodeGenFunction *getCGF() const { return getInserter().CGF; }

  llvm::Value *emitRawPointerFromAddress(Address Addr) const {
    return Addr.getBasePointer();
  }

  template <bool IsInBounds>
  Address createConstGEP2_32(Address Addr, unsigned Idx0, unsigned Idx1,
                             const llvm::Twine &Name) {
    const llvm::DataLayout &DL = BB->getDataLayout();
    llvm::GetElementPtrInst *GEP;
    if (IsInBounds)
      GEP = cast<llvm::GetElementPtrInst>(CreateConstInBoundsGEP2_32(
          Addr.getElementType(), emitRawPointerFromAddress(Addr), Idx0, Idx1,
          Name));
    else
      GEP = cast<llvm::GetElementPtrInst>(CreateConstGEP2_32(
          Addr.getElementType(), emitRawPointerFromAddress(Addr), Idx0, Idx1,
          Name));
    llvm::APInt Offset(
        DL.getIndexSizeInBits(Addr.getType()->getPointerAddressSpace()), 0,
        /*isSigned=*/true);
    if (!GEP->accumulateConstantOffset(DL, Offset))
      llvm_unreachable("offset of GEP with constants is always computable");
    return Address(GEP, GEP->getResultElementType(),
                   Addr.getAlignment().alignmentAtOffset(
                       CharUnits::fromQuantity(Offset.getSExtValue())),
                   IsInBounds ? Addr.isKnownNonNull() : NotKnownNonNull);
  }

public:
  CGBuilderTy(const CodeGenTypeCache &TypeCache, llvm::LLVMContext &C)
      : CGBuilderBaseTy(C), TypeCache(TypeCache) {}
  CGBuilderTy(const CodeGenTypeCache &TypeCache, llvm::LLVMContext &C,
              const llvm::ConstantFolder &F,
              const CGBuilderInserterTy &Inserter)
      : CGBuilderBaseTy(C, F, Inserter), TypeCache(TypeCache) {}
  CGBuilderTy(const CodeGenTypeCache &TypeCache, llvm::Instruction *I)
      : CGBuilderBaseTy(I), TypeCache(TypeCache) {}
  CGBuilderTy(const CodeGenTypeCache &TypeCache, llvm::BasicBlock *BB)
      : CGBuilderBaseTy(BB), TypeCache(TypeCache) {}

  llvm::ConstantInt *getSize(CharUnits N) {
    return llvm::ConstantInt::get(TypeCache.SizeTy, N.getQuantity());
  }
  llvm::ConstantInt *getSize(uint64_t N) {
    return llvm::ConstantInt::get(TypeCache.SizeTy, N);
  }

  // Note that we intentionally hide the CreateLoad APIs that don't
  // take an alignment.
  llvm::LoadInst *CreateLoad(Address Addr, const llvm::Twine &Name = "") {
    return CreateAlignedLoad(Addr.getElementType(),
                             emitRawPointerFromAddress(Addr),
                             Addr.getAlignment().getAsAlign(), Name);
  }
  llvm::LoadInst *CreateLoad(Address Addr, const char *Name) {
    // This overload is required to prevent string literals from
    // ending up in the IsVolatile overload.
    return CreateAlignedLoad(Addr.getElementType(),
                             emitRawPointerFromAddress(Addr),
                             Addr.getAlignment().getAsAlign(), Name);
  }
  llvm::LoadInst *CreateLoad(Address Addr, bool IsVolatile,
                             const llvm::Twine &Name = "") {
    return CreateAlignedLoad(
        Addr.getElementType(), emitRawPointerFromAddress(Addr),
        Addr.getAlignment().getAsAlign(), IsVolatile, Name);
  }

  using CGBuilderBaseTy::CreateAlignedLoad;
  llvm::LoadInst *CreateAlignedLoad(llvm::Type *Ty, llvm::Value *Addr,
                                    CharUnits Align,
                                    const llvm::Twine &Name = "") {
    return CreateAlignedLoad(Ty, Addr, Align.getAsAlign(), Name);
  }

  // Note that we intentionally hide the CreateStore APIs that don't
  // take an alignment.
  llvm::StoreInst *CreateStore(llvm::Value *Val, Address Addr,
                               bool IsVolatile = false) {
    return CreateAlignedStore(Val, emitRawPointerFromAddress(Addr),
                              Addr.getAlignment().getAsAlign(), IsVolatile);
  }

  using CGBuilderBaseTy::CreateAlignedStore;
  llvm::StoreInst *CreateAlignedStore(llvm::Value *Val, llvm::Value *Addr,
                                      CharUnits Align,
                                      bool IsVolatile = false) {
    return CreateAlignedStore(Val, Addr, Align.getAsAlign(), IsVolatile);
  }

  // FIXME: these "default-aligned" APIs should be removed,
  // but I don't feel like fixing all the builtin code right now.
  llvm::StoreInst *CreateDefaultAlignedStore(llvm::Value *Val,
                                             llvm::Value *Addr,
                                             bool IsVolatile = false) {
    return CGBuilderBaseTy::CreateStore(Val, Addr, IsVolatile);
  }

  /// Emit a load from an i1 flag variable.
  llvm::LoadInst *CreateFlagLoad(llvm::Value *Addr,
                                 const llvm::Twine &Name = "") {
    return CreateAlignedLoad(getInt1Ty(), Addr, CharUnits::One(), Name);
  }

  /// Emit a store to an i1 flag variable.
  llvm::StoreInst *CreateFlagStore(bool Value, llvm::Value *Addr) {
    return CreateAlignedStore(getInt1(Value), Addr, CharUnits::One());
  }

  llvm::AtomicCmpXchgInst *
  CreateAtomicCmpXchg(Address Addr, llvm::Value *Cmp, llvm::Value *New,
                      llvm::AtomicOrdering SuccessOrdering,
                      llvm::AtomicOrdering FailureOrdering,
                      llvm::SyncScope::ID SSID = llvm::SyncScope::System) {
    return CGBuilderBaseTy::CreateAtomicCmpXchg(
        Addr.emitRawPointer(*getCGF()), Cmp, New,
        Addr.getAlignment().getAsAlign(), SuccessOrdering, FailureOrdering,
        SSID);
  }

  llvm::AtomicRMWInst *
  CreateAtomicRMW(llvm::AtomicRMWInst::BinOp Op, Address Addr, llvm::Value *Val,
                  llvm::AtomicOrdering Ordering,
                  llvm::SyncScope::ID SSID = llvm::SyncScope::System) {
    return CGBuilderBaseTy::CreateAtomicRMW(
        Op, Addr.emitRawPointer(*getCGF()), Val,
        Addr.getAlignment().getAsAlign(), Ordering, SSID);
  }

  using CGBuilderBaseTy::CreateAddrSpaceCast;
  Address CreateAddrSpaceCast(Address Addr, llvm::Type *Ty,
                              llvm::Type *ElementTy,
                              const llvm::Twine &Name = "") {
    if (!Addr.hasOffset())
      return Address(CreateAddrSpaceCast(Addr.getBasePointer(), Ty, Name),
                     ElementTy, Addr.getAlignment(), Addr.getPointerAuthInfo(),
                     /*Offset=*/nullptr, Addr.isKnownNonNull());
    // Eagerly force a raw address if these is an offset.
    return RawAddress(
        CreateAddrSpaceCast(Addr.emitRawPointer(*getCGF()), Ty, Name),
        ElementTy, Addr.getAlignment(), Addr.isKnownNonNull());
  }

  using CGBuilderBaseTy::CreatePointerBitCastOrAddrSpaceCast;
  Address CreatePointerBitCastOrAddrSpaceCast(Address Addr, llvm::Type *Ty,
                                              llvm::Type *ElementTy,
                                              const llvm::Twine &Name = "") {
    if (Addr.getType()->getAddressSpace() == Ty->getPointerAddressSpace())
      return Addr.withElementType(ElementTy);
    return CreateAddrSpaceCast(Addr, Ty, ElementTy, Name);
  }

  /// Given
  ///   %addr = {T1, T2...}* ...
  /// produce
  ///   %name = getelementptr inbounds %addr, i32 0, i32 index
  ///
  /// This API assumes that drilling into a struct like this is always an
  /// inbounds operation.
  using CGBuilderBaseTy::CreateStructGEP;
  Address CreateStructGEP(Address Addr, unsigned Index,
                          const llvm::Twine &Name = "") {
    llvm::StructType *ElTy = cast<llvm::StructType>(Addr.getElementType());
    const llvm::DataLayout &DL = BB->getDataLayout();
    const llvm::StructLayout *Layout = DL.getStructLayout(ElTy);
    auto Offset = CharUnits::fromQuantity(Layout->getElementOffset(Index));

    return Address(CreateStructGEP(Addr.getElementType(), Addr.getBasePointer(),
                                   Index, Name),
                   ElTy->getElementType(Index),
                   Addr.getAlignment().alignmentAtOffset(Offset),
                   Addr.isKnownNonNull());
  }

  /// Given
  ///   %addr = [n x T]* ...
  /// produce
  ///   %name = getelementptr inbounds %addr, i64 0, i64 index
  /// where i64 is actually the target word size.
  ///
  /// This API assumes that drilling into an array like this is always
  /// an inbounds operation.
  Address CreateConstArrayGEP(Address Addr, uint64_t Index,
                              const llvm::Twine &Name = "") {
    llvm::ArrayType *ElTy = cast<llvm::ArrayType>(Addr.getElementType());
    const llvm::DataLayout &DL = BB->getDataLayout();
    CharUnits EltSize =
        CharUnits::fromQuantity(DL.getTypeAllocSize(ElTy->getElementType()));

    return Address(
        CreateInBoundsGEP(Addr.getElementType(), Addr.getBasePointer(),
                          {getSize(CharUnits::Zero()), getSize(Index)}, Name),
        ElTy->getElementType(),
        Addr.getAlignment().alignmentAtOffset(Index * EltSize),
        Addr.isKnownNonNull());
  }

  /// Given
  ///   %addr = T* ...
  /// produce
  ///   %name = getelementptr inbounds %addr, i64 index
  /// where i64 is actually the target word size.
  Address CreateConstInBoundsGEP(Address Addr, uint64_t Index,
                                 const llvm::Twine &Name = "") {
    llvm::Type *ElTy = Addr.getElementType();
    const llvm::DataLayout &DL = BB->getDataLayout();
    CharUnits EltSize = CharUnits::fromQuantity(DL.getTypeAllocSize(ElTy));

    return Address(
        CreateInBoundsGEP(ElTy, Addr.getBasePointer(), getSize(Index), Name),
        ElTy, Addr.getAlignment().alignmentAtOffset(Index * EltSize),
        Addr.isKnownNonNull());
  }

  /// Given
  ///   %addr = T* ...
  /// produce
  ///   %name = getelementptr inbounds %addr, i64 index
  /// where i64 is actually the target word size.
  Address CreateConstGEP(Address Addr, uint64_t Index,
                         const llvm::Twine &Name = "") {
    llvm::Type *ElTy = Addr.getElementType();
    const llvm::DataLayout &DL = BB->getDataLayout();
    CharUnits EltSize = CharUnits::fromQuantity(DL.getTypeAllocSize(ElTy));

    return Address(CreateGEP(ElTy, Addr.getBasePointer(), getSize(Index), Name),
                   Addr.getElementType(),
                   Addr.getAlignment().alignmentAtOffset(Index * EltSize));
  }

  /// Create GEP with single dynamic index. The address alignment is reduced
  /// according to the element size.
  using CGBuilderBaseTy::CreateGEP;
  Address CreateGEP(CodeGenFunction &CGF, Address Addr, llvm::Value *Index,
                    const llvm::Twine &Name = "") {
    const llvm::DataLayout &DL = BB->getDataLayout();
    CharUnits EltSize =
        CharUnits::fromQuantity(DL.getTypeAllocSize(Addr.getElementType()));

    return Address(
        CreateGEP(Addr.getElementType(), Addr.emitRawPointer(CGF), Index, Name),
        Addr.getElementType(),
        Addr.getAlignment().alignmentOfArrayElement(EltSize));
  }

  /// Given a pointer to i8, adjust it by a given constant offset.
  Address CreateConstInBoundsByteGEP(Address Addr, CharUnits Offset,
                                     const llvm::Twine &Name = "") {
    assert(Addr.getElementType() == TypeCache.Int8Ty);
    return Address(
        CreateInBoundsGEP(Addr.getElementType(), Addr.getBasePointer(),
                          getSize(Offset), Name),
        Addr.getElementType(), Addr.getAlignment().alignmentAtOffset(Offset),
        Addr.isKnownNonNull());
  }

  Address CreateConstByteGEP(Address Addr, CharUnits Offset,
                             const llvm::Twine &Name = "") {
    assert(Addr.getElementType() == TypeCache.Int8Ty);
    return Address(CreateGEP(Addr.getElementType(), Addr.getBasePointer(),
                             getSize(Offset), Name),
                   Addr.getElementType(),
                   Addr.getAlignment().alignmentAtOffset(Offset));
  }

  using CGBuilderBaseTy::CreateConstInBoundsGEP2_32;
  Address CreateConstInBoundsGEP2_32(Address Addr, unsigned Idx0, unsigned Idx1,
                                     const llvm::Twine &Name = "") {
    return createConstGEP2_32<true>(Addr, Idx0, Idx1, Name);
  }

  using CGBuilderBaseTy::CreateConstGEP2_32;
  Address CreateConstGEP2_32(Address Addr, unsigned Idx0, unsigned Idx1,
                             const llvm::Twine &Name = "") {
    return createConstGEP2_32<false>(Addr, Idx0, Idx1, Name);
  }

  Address CreateGEP(Address Addr, ArrayRef<llvm::Value *> IdxList,
                    llvm::Type *ElementType, CharUnits Align,
                    const Twine &Name = "") {
    llvm::Value *Ptr = emitRawPointerFromAddress(Addr);
    return RawAddress(CreateGEP(Addr.getElementType(), Ptr, IdxList, Name),
                      ElementType, Align);
  }

  using CGBuilderBaseTy::CreateInBoundsGEP;
  Address CreateInBoundsGEP(Address Addr, ArrayRef<llvm::Value *> IdxList,
                            llvm::Type *ElementType, CharUnits Align,
                            const Twine &Name = "") {
    return RawAddress(CreateInBoundsGEP(Addr.getElementType(),
                                        emitRawPointerFromAddress(Addr),
                                        IdxList, Name),
                      ElementType, Align, Addr.isKnownNonNull());
  }

  using CGBuilderBaseTy::CreateIsNull;
  llvm::Value *CreateIsNull(Address Addr, const Twine &Name = "") {
    if (!Addr.hasOffset())
      return CreateIsNull(Addr.getBasePointer(), Name);
    // The pointer isn't null if Addr has an offset since offsets can always
    // be applied inbound.
    return llvm::ConstantInt::getFalse(Context);
  }

  using CGBuilderBaseTy::CreateMemCpy;
  llvm::CallInst *CreateMemCpy(Address Dest, Address Src, llvm::Value *Size,
                               bool IsVolatile = false) {
    llvm::Value *DestPtr = emitRawPointerFromAddress(Dest);
    llvm::Value *SrcPtr = emitRawPointerFromAddress(Src);
    return CreateMemCpy(DestPtr, Dest.getAlignment().getAsAlign(), SrcPtr,
                        Src.getAlignment().getAsAlign(), Size, IsVolatile);
  }
  llvm::CallInst *CreateMemCpy(Address Dest, Address Src, uint64_t Size,
                               bool IsVolatile = false) {
    llvm::Value *DestPtr = emitRawPointerFromAddress(Dest);
    llvm::Value *SrcPtr = emitRawPointerFromAddress(Src);
    return CreateMemCpy(DestPtr, Dest.getAlignment().getAsAlign(), SrcPtr,
                        Src.getAlignment().getAsAlign(), Size, IsVolatile);
  }

  using CGBuilderBaseTy::CreateMemCpyInline;
  llvm::CallInst *CreateMemCpyInline(Address Dest, Address Src, uint64_t Size) {
    llvm::Value *DestPtr = emitRawPointerFromAddress(Dest);
    llvm::Value *SrcPtr = emitRawPointerFromAddress(Src);
    return CreateMemCpyInline(DestPtr, Dest.getAlignment().getAsAlign(), SrcPtr,
                              Src.getAlignment().getAsAlign(), getInt64(Size));
  }

  using CGBuilderBaseTy::CreateMemMove;
  llvm::CallInst *CreateMemMove(Address Dest, Address Src, llvm::Value *Size,
                                bool IsVolatile = false) {
    llvm::Value *DestPtr = emitRawPointerFromAddress(Dest);
    llvm::Value *SrcPtr = emitRawPointerFromAddress(Src);
    return CreateMemMove(DestPtr, Dest.getAlignment().getAsAlign(), SrcPtr,
                         Src.getAlignment().getAsAlign(), Size, IsVolatile);
  }

  using CGBuilderBaseTy::CreateMemSet;
  llvm::CallInst *CreateMemSet(Address Dest, llvm::Value *Value,
                               llvm::Value *Size, bool IsVolatile = false) {
    return CreateMemSet(emitRawPointerFromAddress(Dest), Value, Size,
                        Dest.getAlignment().getAsAlign(), IsVolatile);
  }

  using CGBuilderBaseTy::CreateMemSetInline;
  llvm::CallInst *CreateMemSetInline(Address Dest, llvm::Value *Value,
                                     uint64_t Size) {
    return CreateMemSetInline(emitRawPointerFromAddress(Dest),
                              Dest.getAlignment().getAsAlign(), Value,
                              getInt64(Size));
  }

  using CGBuilderBaseTy::CreatePreserveStructAccessIndex;
  Address CreatePreserveStructAccessIndex(Address Addr, unsigned Index,
                                          unsigned FieldIndex,
                                          llvm::MDNode *DbgInfo) {
    llvm::StructType *ElTy = cast<llvm::StructType>(Addr.getElementType());
    const llvm::DataLayout &DL = BB->getDataLayout();
    const llvm::StructLayout *Layout = DL.getStructLayout(ElTy);
    auto Offset = CharUnits::fromQuantity(Layout->getElementOffset(Index));

    return Address(
        CreatePreserveStructAccessIndex(ElTy, emitRawPointerFromAddress(Addr),
                                        Index, FieldIndex, DbgInfo),
        ElTy->getElementType(Index),
        Addr.getAlignment().alignmentAtOffset(Offset));
  }

  using CGBuilderBaseTy::CreatePreserveUnionAccessIndex;
  Address CreatePreserveUnionAccessIndex(Address Addr, unsigned FieldIndex,
                                         llvm::MDNode *DbgInfo) {
    Addr.replaceBasePointer(CreatePreserveUnionAccessIndex(
        Addr.getBasePointer(), FieldIndex, DbgInfo));
    return Addr;
  }

  using CGBuilderBaseTy::CreateLaunderInvariantGroup;
  Address CreateLaunderInvariantGroup(Address Addr) {
    Addr.replaceBasePointer(CreateLaunderInvariantGroup(Addr.getBasePointer()));
    return Addr;
  }

  using CGBuilderBaseTy::CreateStripInvariantGroup;
  Address CreateStripInvariantGroup(Address Addr) {
    Addr.replaceBasePointer(CreateStripInvariantGroup(Addr.getBasePointer()));
    return Addr;
  }
};

} // end namespace CodeGen
} // end namespace clang

#endif
