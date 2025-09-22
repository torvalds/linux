//===--- CGClass.cpp - Emit LLVM Code for C++ classes -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation of classes
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "CGBlocks.h"
#include "CGCXXABI.h"
#include "CGDebugInfo.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "TargetInfo.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Transforms/Utils/SanitizerStats.h"
#include <optional>

using namespace clang;
using namespace CodeGen;

/// Return the best known alignment for an unknown pointer to a
/// particular class.
CharUnits CodeGenModule::getClassPointerAlignment(const CXXRecordDecl *RD) {
  if (!RD->hasDefinition())
    return CharUnits::One(); // Hopefully won't be used anywhere.

  auto &layout = getContext().getASTRecordLayout(RD);

  // If the class is final, then we know that the pointer points to an
  // object of that type and can use the full alignment.
  if (RD->isEffectivelyFinal())
    return layout.getAlignment();

  // Otherwise, we have to assume it could be a subclass.
  return layout.getNonVirtualAlignment();
}

/// Return the smallest possible amount of storage that might be allocated
/// starting from the beginning of an object of a particular class.
///
/// This may be smaller than sizeof(RD) if RD has virtual base classes.
CharUnits CodeGenModule::getMinimumClassObjectSize(const CXXRecordDecl *RD) {
  if (!RD->hasDefinition())
    return CharUnits::One();

  auto &layout = getContext().getASTRecordLayout(RD);

  // If the class is final, then we know that the pointer points to an
  // object of that type and can use the full alignment.
  if (RD->isEffectivelyFinal())
    return layout.getSize();

  // Otherwise, we have to assume it could be a subclass.
  return std::max(layout.getNonVirtualSize(), CharUnits::One());
}

/// Return the best known alignment for a pointer to a virtual base,
/// given the alignment of a pointer to the derived class.
CharUnits CodeGenModule::getVBaseAlignment(CharUnits actualDerivedAlign,
                                           const CXXRecordDecl *derivedClass,
                                           const CXXRecordDecl *vbaseClass) {
  // The basic idea here is that an underaligned derived pointer might
  // indicate an underaligned base pointer.

  assert(vbaseClass->isCompleteDefinition());
  auto &baseLayout = getContext().getASTRecordLayout(vbaseClass);
  CharUnits expectedVBaseAlign = baseLayout.getNonVirtualAlignment();

  return getDynamicOffsetAlignment(actualDerivedAlign, derivedClass,
                                   expectedVBaseAlign);
}

CharUnits
CodeGenModule::getDynamicOffsetAlignment(CharUnits actualBaseAlign,
                                         const CXXRecordDecl *baseDecl,
                                         CharUnits expectedTargetAlign) {
  // If the base is an incomplete type (which is, alas, possible with
  // member pointers), be pessimistic.
  if (!baseDecl->isCompleteDefinition())
    return std::min(actualBaseAlign, expectedTargetAlign);

  auto &baseLayout = getContext().getASTRecordLayout(baseDecl);
  CharUnits expectedBaseAlign = baseLayout.getNonVirtualAlignment();

  // If the class is properly aligned, assume the target offset is, too.
  //
  // This actually isn't necessarily the right thing to do --- if the
  // class is a complete object, but it's only properly aligned for a
  // base subobject, then the alignments of things relative to it are
  // probably off as well.  (Note that this requires the alignment of
  // the target to be greater than the NV alignment of the derived
  // class.)
  //
  // However, our approach to this kind of under-alignment can only
  // ever be best effort; after all, we're never going to propagate
  // alignments through variables or parameters.  Note, in particular,
  // that constructing a polymorphic type in an address that's less
  // than pointer-aligned will generally trap in the constructor,
  // unless we someday add some sort of attribute to change the
  // assumed alignment of 'this'.  So our goal here is pretty much
  // just to allow the user to explicitly say that a pointer is
  // under-aligned and then safely access its fields and vtables.
  if (actualBaseAlign >= expectedBaseAlign) {
    return expectedTargetAlign;
  }

  // Otherwise, we might be offset by an arbitrary multiple of the
  // actual alignment.  The correct adjustment is to take the min of
  // the two alignments.
  return std::min(actualBaseAlign, expectedTargetAlign);
}

Address CodeGenFunction::LoadCXXThisAddress() {
  assert(CurFuncDecl && "loading 'this' without a func declaration?");
  auto *MD = cast<CXXMethodDecl>(CurFuncDecl);

  // Lazily compute CXXThisAlignment.
  if (CXXThisAlignment.isZero()) {
    // Just use the best known alignment for the parent.
    // TODO: if we're currently emitting a complete-object ctor/dtor,
    // we can always use the complete-object alignment.
    CXXThisAlignment = CGM.getClassPointerAlignment(MD->getParent());
  }

  return makeNaturalAddressForPointer(
      LoadCXXThis(), MD->getFunctionObjectParameterType(), CXXThisAlignment,
      false, nullptr, nullptr, KnownNonNull);
}

/// Emit the address of a field using a member data pointer.
///
/// \param E Only used for emergency diagnostics
Address
CodeGenFunction::EmitCXXMemberDataPointerAddress(const Expr *E, Address base,
                                                 llvm::Value *memberPtr,
                                      const MemberPointerType *memberPtrType,
                                                 LValueBaseInfo *BaseInfo,
                                                 TBAAAccessInfo *TBAAInfo) {
  // Ask the ABI to compute the actual address.
  llvm::Value *ptr =
    CGM.getCXXABI().EmitMemberDataPointerAddress(*this, E, base,
                                                 memberPtr, memberPtrType);

  QualType memberType = memberPtrType->getPointeeType();
  CharUnits memberAlign =
      CGM.getNaturalTypeAlignment(memberType, BaseInfo, TBAAInfo);
  memberAlign =
    CGM.getDynamicOffsetAlignment(base.getAlignment(),
                            memberPtrType->getClass()->getAsCXXRecordDecl(),
                                  memberAlign);
  return Address(ptr, ConvertTypeForMem(memberPtrType->getPointeeType()),
                 memberAlign);
}

CharUnits CodeGenModule::computeNonVirtualBaseClassOffset(
    const CXXRecordDecl *DerivedClass, CastExpr::path_const_iterator Start,
    CastExpr::path_const_iterator End) {
  CharUnits Offset = CharUnits::Zero();

  const ASTContext &Context = getContext();
  const CXXRecordDecl *RD = DerivedClass;

  for (CastExpr::path_const_iterator I = Start; I != End; ++I) {
    const CXXBaseSpecifier *Base = *I;
    assert(!Base->isVirtual() && "Should not see virtual bases here!");

    // Get the layout.
    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);

    const auto *BaseDecl =
        cast<CXXRecordDecl>(Base->getType()->castAs<RecordType>()->getDecl());

    // Add the offset.
    Offset += Layout.getBaseClassOffset(BaseDecl);

    RD = BaseDecl;
  }

  return Offset;
}

llvm::Constant *
CodeGenModule::GetNonVirtualBaseClassOffset(const CXXRecordDecl *ClassDecl,
                                   CastExpr::path_const_iterator PathBegin,
                                   CastExpr::path_const_iterator PathEnd) {
  assert(PathBegin != PathEnd && "Base path should not be empty!");

  CharUnits Offset =
      computeNonVirtualBaseClassOffset(ClassDecl, PathBegin, PathEnd);
  if (Offset.isZero())
    return nullptr;

  llvm::Type *PtrDiffTy =
      getTypes().ConvertType(getContext().getPointerDiffType());

  return llvm::ConstantInt::get(PtrDiffTy, Offset.getQuantity());
}

/// Gets the address of a direct base class within a complete object.
/// This should only be used for (1) non-virtual bases or (2) virtual bases
/// when the type is known to be complete (e.g. in complete destructors).
///
/// The object pointed to by 'This' is assumed to be non-null.
Address
CodeGenFunction::GetAddressOfDirectBaseInCompleteClass(Address This,
                                                   const CXXRecordDecl *Derived,
                                                   const CXXRecordDecl *Base,
                                                   bool BaseIsVirtual) {
  // 'this' must be a pointer (in some address space) to Derived.
  assert(This.getElementType() == ConvertType(Derived));

  // Compute the offset of the virtual base.
  CharUnits Offset;
  const ASTRecordLayout &Layout = getContext().getASTRecordLayout(Derived);
  if (BaseIsVirtual)
    Offset = Layout.getVBaseClassOffset(Base);
  else
    Offset = Layout.getBaseClassOffset(Base);

  // Shift and cast down to the base type.
  // TODO: for complete types, this should be possible with a GEP.
  Address V = This;
  if (!Offset.isZero()) {
    V = V.withElementType(Int8Ty);
    V = Builder.CreateConstInBoundsByteGEP(V, Offset);
  }
  return V.withElementType(ConvertType(Base));
}

static Address
ApplyNonVirtualAndVirtualOffset(CodeGenFunction &CGF, Address addr,
                                CharUnits nonVirtualOffset,
                                llvm::Value *virtualOffset,
                                const CXXRecordDecl *derivedClass,
                                const CXXRecordDecl *nearestVBase) {
  // Assert that we have something to do.
  assert(!nonVirtualOffset.isZero() || virtualOffset != nullptr);

  // Compute the offset from the static and dynamic components.
  llvm::Value *baseOffset;
  if (!nonVirtualOffset.isZero()) {
    llvm::Type *OffsetType =
        (CGF.CGM.getTarget().getCXXABI().isItaniumFamily() &&
         CGF.CGM.getItaniumVTableContext().isRelativeLayout())
            ? CGF.Int32Ty
            : CGF.PtrDiffTy;
    baseOffset =
        llvm::ConstantInt::get(OffsetType, nonVirtualOffset.getQuantity());
    if (virtualOffset) {
      baseOffset = CGF.Builder.CreateAdd(virtualOffset, baseOffset);
    }
  } else {
    baseOffset = virtualOffset;
  }

  // Apply the base offset.
  llvm::Value *ptr = addr.emitRawPointer(CGF);
  ptr = CGF.Builder.CreateInBoundsGEP(CGF.Int8Ty, ptr, baseOffset, "add.ptr");

  // If we have a virtual component, the alignment of the result will
  // be relative only to the known alignment of that vbase.
  CharUnits alignment;
  if (virtualOffset) {
    assert(nearestVBase && "virtual offset without vbase?");
    alignment = CGF.CGM.getVBaseAlignment(addr.getAlignment(),
                                          derivedClass, nearestVBase);
  } else {
    alignment = addr.getAlignment();
  }
  alignment = alignment.alignmentAtOffset(nonVirtualOffset);

  return Address(ptr, CGF.Int8Ty, alignment);
}

Address CodeGenFunction::GetAddressOfBaseClass(
    Address Value, const CXXRecordDecl *Derived,
    CastExpr::path_const_iterator PathBegin,
    CastExpr::path_const_iterator PathEnd, bool NullCheckValue,
    SourceLocation Loc) {
  assert(PathBegin != PathEnd && "Base path should not be empty!");

  CastExpr::path_const_iterator Start = PathBegin;
  const CXXRecordDecl *VBase = nullptr;

  // Sema has done some convenient canonicalization here: if the
  // access path involved any virtual steps, the conversion path will
  // *start* with a step down to the correct virtual base subobject,
  // and hence will not require any further steps.
  if ((*Start)->isVirtual()) {
    VBase = cast<CXXRecordDecl>(
        (*Start)->getType()->castAs<RecordType>()->getDecl());
    ++Start;
  }

  // Compute the static offset of the ultimate destination within its
  // allocating subobject (the virtual base, if there is one, or else
  // the "complete" object that we see).
  CharUnits NonVirtualOffset = CGM.computeNonVirtualBaseClassOffset(
      VBase ? VBase : Derived, Start, PathEnd);

  // If there's a virtual step, we can sometimes "devirtualize" it.
  // For now, that's limited to when the derived type is final.
  // TODO: "devirtualize" this for accesses to known-complete objects.
  if (VBase && Derived->hasAttr<FinalAttr>()) {
    const ASTRecordLayout &layout = getContext().getASTRecordLayout(Derived);
    CharUnits vBaseOffset = layout.getVBaseClassOffset(VBase);
    NonVirtualOffset += vBaseOffset;
    VBase = nullptr; // we no longer have a virtual step
  }

  // Get the base pointer type.
  llvm::Type *BaseValueTy = ConvertType((PathEnd[-1])->getType());
  llvm::Type *PtrTy = llvm::PointerType::get(
      CGM.getLLVMContext(), Value.getType()->getPointerAddressSpace());

  QualType DerivedTy = getContext().getRecordType(Derived);
  CharUnits DerivedAlign = CGM.getClassPointerAlignment(Derived);

  // If the static offset is zero and we don't have a virtual step,
  // just do a bitcast; null checks are unnecessary.
  if (NonVirtualOffset.isZero() && !VBase) {
    if (sanitizePerformTypeCheck()) {
      SanitizerSet SkippedChecks;
      SkippedChecks.set(SanitizerKind::Null, !NullCheckValue);
      EmitTypeCheck(TCK_Upcast, Loc, Value.emitRawPointer(*this), DerivedTy,
                    DerivedAlign, SkippedChecks);
    }
    return Value.withElementType(BaseValueTy);
  }

  llvm::BasicBlock *origBB = nullptr;
  llvm::BasicBlock *endBB = nullptr;

  // Skip over the offset (and the vtable load) if we're supposed to
  // null-check the pointer.
  if (NullCheckValue) {
    origBB = Builder.GetInsertBlock();
    llvm::BasicBlock *notNullBB = createBasicBlock("cast.notnull");
    endBB = createBasicBlock("cast.end");

    llvm::Value *isNull = Builder.CreateIsNull(Value);
    Builder.CreateCondBr(isNull, endBB, notNullBB);
    EmitBlock(notNullBB);
  }

  if (sanitizePerformTypeCheck()) {
    SanitizerSet SkippedChecks;
    SkippedChecks.set(SanitizerKind::Null, true);
    EmitTypeCheck(VBase ? TCK_UpcastToVirtualBase : TCK_Upcast, Loc,
                  Value.emitRawPointer(*this), DerivedTy, DerivedAlign,
                  SkippedChecks);
  }

  // Compute the virtual offset.
  llvm::Value *VirtualOffset = nullptr;
  if (VBase) {
    VirtualOffset =
        CGM.getCXXABI().GetVirtualBaseClassOffset(*this, Value, Derived, VBase);
  }

  // Apply both offsets.
  Value = ApplyNonVirtualAndVirtualOffset(*this, Value, NonVirtualOffset,
                                          VirtualOffset, Derived, VBase);

  // Cast to the destination type.
  Value = Value.withElementType(BaseValueTy);

  // Build a phi if we needed a null check.
  if (NullCheckValue) {
    llvm::BasicBlock *notNullBB = Builder.GetInsertBlock();
    Builder.CreateBr(endBB);
    EmitBlock(endBB);

    llvm::PHINode *PHI = Builder.CreatePHI(PtrTy, 2, "cast.result");
    PHI->addIncoming(Value.emitRawPointer(*this), notNullBB);
    PHI->addIncoming(llvm::Constant::getNullValue(PtrTy), origBB);
    Value = Value.withPointer(PHI, NotKnownNonNull);
  }

  return Value;
}

Address
CodeGenFunction::GetAddressOfDerivedClass(Address BaseAddr,
                                          const CXXRecordDecl *Derived,
                                        CastExpr::path_const_iterator PathBegin,
                                          CastExpr::path_const_iterator PathEnd,
                                          bool NullCheckValue) {
  assert(PathBegin != PathEnd && "Base path should not be empty!");

  QualType DerivedTy =
      getContext().getCanonicalType(getContext().getTagDeclType(Derived));
  llvm::Type *DerivedValueTy = ConvertType(DerivedTy);

  llvm::Value *NonVirtualOffset =
    CGM.GetNonVirtualBaseClassOffset(Derived, PathBegin, PathEnd);

  if (!NonVirtualOffset) {
    // No offset, we can just cast back.
    return BaseAddr.withElementType(DerivedValueTy);
  }

  llvm::BasicBlock *CastNull = nullptr;
  llvm::BasicBlock *CastNotNull = nullptr;
  llvm::BasicBlock *CastEnd = nullptr;

  if (NullCheckValue) {
    CastNull = createBasicBlock("cast.null");
    CastNotNull = createBasicBlock("cast.notnull");
    CastEnd = createBasicBlock("cast.end");

    llvm::Value *IsNull = Builder.CreateIsNull(BaseAddr);
    Builder.CreateCondBr(IsNull, CastNull, CastNotNull);
    EmitBlock(CastNotNull);
  }

  // Apply the offset.
  Address Addr = BaseAddr.withElementType(Int8Ty);
  Addr = Builder.CreateInBoundsGEP(
      Addr, Builder.CreateNeg(NonVirtualOffset), Int8Ty,
      CGM.getClassPointerAlignment(Derived), "sub.ptr");

  // Just cast.
  Addr = Addr.withElementType(DerivedValueTy);

  // Produce a PHI if we had a null-check.
  if (NullCheckValue) {
    Builder.CreateBr(CastEnd);
    EmitBlock(CastNull);
    Builder.CreateBr(CastEnd);
    EmitBlock(CastEnd);

    llvm::Value *Value = Addr.emitRawPointer(*this);
    llvm::PHINode *PHI = Builder.CreatePHI(Value->getType(), 2);
    PHI->addIncoming(Value, CastNotNull);
    PHI->addIncoming(llvm::Constant::getNullValue(Value->getType()), CastNull);
    return Address(PHI, Addr.getElementType(),
                   CGM.getClassPointerAlignment(Derived));
  }

  return Addr;
}

llvm::Value *CodeGenFunction::GetVTTParameter(GlobalDecl GD,
                                              bool ForVirtualBase,
                                              bool Delegating) {
  if (!CGM.getCXXABI().NeedsVTTParameter(GD)) {
    // This constructor/destructor does not need a VTT parameter.
    return nullptr;
  }

  const CXXRecordDecl *RD = cast<CXXMethodDecl>(CurCodeDecl)->getParent();
  const CXXRecordDecl *Base = cast<CXXMethodDecl>(GD.getDecl())->getParent();

  uint64_t SubVTTIndex;

  if (Delegating) {
    // If this is a delegating constructor call, just load the VTT.
    return LoadCXXVTT();
  } else if (RD == Base) {
    // If the record matches the base, this is the complete ctor/dtor
    // variant calling the base variant in a class with virtual bases.
    assert(!CGM.getCXXABI().NeedsVTTParameter(CurGD) &&
           "doing no-op VTT offset in base dtor/ctor?");
    assert(!ForVirtualBase && "Can't have same class as virtual base!");
    SubVTTIndex = 0;
  } else {
    const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
    CharUnits BaseOffset = ForVirtualBase ?
      Layout.getVBaseClassOffset(Base) :
      Layout.getBaseClassOffset(Base);

    SubVTTIndex =
      CGM.getVTables().getSubVTTIndex(RD, BaseSubobject(Base, BaseOffset));
    assert(SubVTTIndex != 0 && "Sub-VTT index must be greater than zero!");
  }

  if (CGM.getCXXABI().NeedsVTTParameter(CurGD)) {
    // A VTT parameter was passed to the constructor, use it.
    llvm::Value *VTT = LoadCXXVTT();
    return Builder.CreateConstInBoundsGEP1_64(VoidPtrTy, VTT, SubVTTIndex);
  } else {
    // We're the complete constructor, so get the VTT by name.
    llvm::GlobalValue *VTT = CGM.getVTables().GetAddrOfVTT(RD);
    return Builder.CreateConstInBoundsGEP2_64(
        VTT->getValueType(), VTT, 0, SubVTTIndex);
  }
}

namespace {
  /// Call the destructor for a direct base class.
  struct CallBaseDtor final : EHScopeStack::Cleanup {
    const CXXRecordDecl *BaseClass;
    bool BaseIsVirtual;
    CallBaseDtor(const CXXRecordDecl *Base, bool BaseIsVirtual)
      : BaseClass(Base), BaseIsVirtual(BaseIsVirtual) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const CXXRecordDecl *DerivedClass =
        cast<CXXMethodDecl>(CGF.CurCodeDecl)->getParent();

      const CXXDestructorDecl *D = BaseClass->getDestructor();
      // We are already inside a destructor, so presumably the object being
      // destroyed should have the expected type.
      QualType ThisTy = D->getFunctionObjectParameterType();
      Address Addr =
        CGF.GetAddressOfDirectBaseInCompleteClass(CGF.LoadCXXThisAddress(),
                                                  DerivedClass, BaseClass,
                                                  BaseIsVirtual);
      CGF.EmitCXXDestructorCall(D, Dtor_Base, BaseIsVirtual,
                                /*Delegating=*/false, Addr, ThisTy);
    }
  };

  /// A visitor which checks whether an initializer uses 'this' in a
  /// way which requires the vtable to be properly set.
  struct DynamicThisUseChecker : ConstEvaluatedExprVisitor<DynamicThisUseChecker> {
    typedef ConstEvaluatedExprVisitor<DynamicThisUseChecker> super;

    bool UsesThis;

    DynamicThisUseChecker(const ASTContext &C) : super(C), UsesThis(false) {}

    // Black-list all explicit and implicit references to 'this'.
    //
    // Do we need to worry about external references to 'this' derived
    // from arbitrary code?  If so, then anything which runs arbitrary
    // external code might potentially access the vtable.
    void VisitCXXThisExpr(const CXXThisExpr *E) { UsesThis = true; }
  };
} // end anonymous namespace

static bool BaseInitializerUsesThis(ASTContext &C, const Expr *Init) {
  DynamicThisUseChecker Checker(C);
  Checker.Visit(Init);
  return Checker.UsesThis;
}

static void EmitBaseInitializer(CodeGenFunction &CGF,
                                const CXXRecordDecl *ClassDecl,
                                CXXCtorInitializer *BaseInit) {
  assert(BaseInit->isBaseInitializer() &&
         "Must have base initializer!");

  Address ThisPtr = CGF.LoadCXXThisAddress();

  const Type *BaseType = BaseInit->getBaseClass();
  const auto *BaseClassDecl =
      cast<CXXRecordDecl>(BaseType->castAs<RecordType>()->getDecl());

  bool isBaseVirtual = BaseInit->isBaseVirtual();

  // If the initializer for the base (other than the constructor
  // itself) accesses 'this' in any way, we need to initialize the
  // vtables.
  if (BaseInitializerUsesThis(CGF.getContext(), BaseInit->getInit()))
    CGF.InitializeVTablePointers(ClassDecl);

  // We can pretend to be a complete class because it only matters for
  // virtual bases, and we only do virtual bases for complete ctors.
  Address V =
    CGF.GetAddressOfDirectBaseInCompleteClass(ThisPtr, ClassDecl,
                                              BaseClassDecl,
                                              isBaseVirtual);
  AggValueSlot AggSlot =
      AggValueSlot::forAddr(
          V, Qualifiers(),
          AggValueSlot::IsDestructed,
          AggValueSlot::DoesNotNeedGCBarriers,
          AggValueSlot::IsNotAliased,
          CGF.getOverlapForBaseInit(ClassDecl, BaseClassDecl, isBaseVirtual));

  CGF.EmitAggExpr(BaseInit->getInit(), AggSlot);

  if (CGF.CGM.getLangOpts().Exceptions &&
      !BaseClassDecl->hasTrivialDestructor())
    CGF.EHStack.pushCleanup<CallBaseDtor>(EHCleanup, BaseClassDecl,
                                          isBaseVirtual);
}

static bool isMemcpyEquivalentSpecialMember(const CXXMethodDecl *D) {
  auto *CD = dyn_cast<CXXConstructorDecl>(D);
  if (!(CD && CD->isCopyOrMoveConstructor()) &&
      !D->isCopyAssignmentOperator() && !D->isMoveAssignmentOperator())
    return false;

  // We can emit a memcpy for a trivial copy or move constructor/assignment.
  if (D->isTrivial() && !D->getParent()->mayInsertExtraPadding())
    return true;

  // We *must* emit a memcpy for a defaulted union copy or move op.
  if (D->getParent()->isUnion() && D->isDefaulted())
    return true;

  return false;
}

static void EmitLValueForAnyFieldInitialization(CodeGenFunction &CGF,
                                                CXXCtorInitializer *MemberInit,
                                                LValue &LHS) {
  FieldDecl *Field = MemberInit->getAnyMember();
  if (MemberInit->isIndirectMemberInitializer()) {
    // If we are initializing an anonymous union field, drill down to the field.
    IndirectFieldDecl *IndirectField = MemberInit->getIndirectMember();
    for (const auto *I : IndirectField->chain())
      LHS = CGF.EmitLValueForFieldInitialization(LHS, cast<FieldDecl>(I));
  } else {
    LHS = CGF.EmitLValueForFieldInitialization(LHS, Field);
  }
}

static void EmitMemberInitializer(CodeGenFunction &CGF,
                                  const CXXRecordDecl *ClassDecl,
                                  CXXCtorInitializer *MemberInit,
                                  const CXXConstructorDecl *Constructor,
                                  FunctionArgList &Args) {
  ApplyDebugLocation Loc(CGF, MemberInit->getSourceLocation());
  assert(MemberInit->isAnyMemberInitializer() &&
         "Must have member initializer!");
  assert(MemberInit->getInit() && "Must have initializer!");

  // non-static data member initializers.
  FieldDecl *Field = MemberInit->getAnyMember();
  QualType FieldType = Field->getType();

  llvm::Value *ThisPtr = CGF.LoadCXXThis();
  QualType RecordTy = CGF.getContext().getTypeDeclType(ClassDecl);
  LValue LHS;

  // If a base constructor is being emitted, create an LValue that has the
  // non-virtual alignment.
  if (CGF.CurGD.getCtorType() == Ctor_Base)
    LHS = CGF.MakeNaturalAlignPointeeAddrLValue(ThisPtr, RecordTy);
  else
    LHS = CGF.MakeNaturalAlignAddrLValue(ThisPtr, RecordTy);

  EmitLValueForAnyFieldInitialization(CGF, MemberInit, LHS);

  // Special case: if we are in a copy or move constructor, and we are copying
  // an array of PODs or classes with trivial copy constructors, ignore the
  // AST and perform the copy we know is equivalent.
  // FIXME: This is hacky at best... if we had a bit more explicit information
  // in the AST, we could generalize it more easily.
  const ConstantArrayType *Array
    = CGF.getContext().getAsConstantArrayType(FieldType);
  if (Array && Constructor->isDefaulted() &&
      Constructor->isCopyOrMoveConstructor()) {
    QualType BaseElementTy = CGF.getContext().getBaseElementType(Array);
    CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(MemberInit->getInit());
    if (BaseElementTy.isPODType(CGF.getContext()) ||
        (CE && isMemcpyEquivalentSpecialMember(CE->getConstructor()))) {
      unsigned SrcArgIndex =
          CGF.CGM.getCXXABI().getSrcArgforCopyCtor(Constructor, Args);
      llvm::Value *SrcPtr
        = CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(Args[SrcArgIndex]));
      LValue ThisRHSLV = CGF.MakeNaturalAlignAddrLValue(SrcPtr, RecordTy);
      LValue Src = CGF.EmitLValueForFieldInitialization(ThisRHSLV, Field);

      // Copy the aggregate.
      CGF.EmitAggregateCopy(LHS, Src, FieldType, CGF.getOverlapForFieldInit(Field),
                            LHS.isVolatileQualified());
      // Ensure that we destroy the objects if an exception is thrown later in
      // the constructor.
      QualType::DestructionKind dtorKind = FieldType.isDestructedType();
      if (CGF.needsEHCleanup(dtorKind))
        CGF.pushEHDestroy(dtorKind, LHS.getAddress(), FieldType);
      return;
    }
  }

  CGF.EmitInitializerForField(Field, LHS, MemberInit->getInit());
}

void CodeGenFunction::EmitInitializerForField(FieldDecl *Field, LValue LHS,
                                              Expr *Init) {
  QualType FieldType = Field->getType();
  switch (getEvaluationKind(FieldType)) {
  case TEK_Scalar:
    if (LHS.isSimple()) {
      EmitExprAsInit(Init, Field, LHS, false);
    } else {
      RValue RHS = RValue::get(EmitScalarExpr(Init));
      EmitStoreThroughLValue(RHS, LHS);
    }
    break;
  case TEK_Complex:
    EmitComplexExprIntoLValue(Init, LHS, /*isInit*/ true);
    break;
  case TEK_Aggregate: {
    AggValueSlot Slot = AggValueSlot::forLValue(
        LHS, AggValueSlot::IsDestructed, AggValueSlot::DoesNotNeedGCBarriers,
        AggValueSlot::IsNotAliased, getOverlapForFieldInit(Field),
        AggValueSlot::IsNotZeroed,
        // Checks are made by the code that calls constructor.
        AggValueSlot::IsSanitizerChecked);
    EmitAggExpr(Init, Slot);
    break;
  }
  }

  // Ensure that we destroy this object if an exception is thrown
  // later in the constructor.
  QualType::DestructionKind dtorKind = FieldType.isDestructedType();
  if (needsEHCleanup(dtorKind))
    pushEHDestroy(dtorKind, LHS.getAddress(), FieldType);
}

/// Checks whether the given constructor is a valid subject for the
/// complete-to-base constructor delegation optimization, i.e.
/// emitting the complete constructor as a simple call to the base
/// constructor.
bool CodeGenFunction::IsConstructorDelegationValid(
    const CXXConstructorDecl *Ctor) {

  // Currently we disable the optimization for classes with virtual
  // bases because (1) the addresses of parameter variables need to be
  // consistent across all initializers but (2) the delegate function
  // call necessarily creates a second copy of the parameter variable.
  //
  // The limiting example (purely theoretical AFAIK):
  //   struct A { A(int &c) { c++; } };
  //   struct B : virtual A {
  //     B(int count) : A(count) { printf("%d\n", count); }
  //   };
  // ...although even this example could in principle be emitted as a
  // delegation since the address of the parameter doesn't escape.
  if (Ctor->getParent()->getNumVBases()) {
    // TODO: white-list trivial vbase initializers.  This case wouldn't
    // be subject to the restrictions below.

    // TODO: white-list cases where:
    //  - there are no non-reference parameters to the constructor
    //  - the initializers don't access any non-reference parameters
    //  - the initializers don't take the address of non-reference
    //    parameters
    //  - etc.
    // If we ever add any of the above cases, remember that:
    //  - function-try-blocks will always exclude this optimization
    //  - we need to perform the constructor prologue and cleanup in
    //    EmitConstructorBody.

    return false;
  }

  // We also disable the optimization for variadic functions because
  // it's impossible to "re-pass" varargs.
  if (Ctor->getType()->castAs<FunctionProtoType>()->isVariadic())
    return false;

  // FIXME: Decide if we can do a delegation of a delegating constructor.
  if (Ctor->isDelegatingConstructor())
    return false;

  return true;
}

// Emit code in ctor (Prologue==true) or dtor (Prologue==false)
// to poison the extra field paddings inserted under
// -fsanitize-address-field-padding=1|2.
void CodeGenFunction::EmitAsanPrologueOrEpilogue(bool Prologue) {
  ASTContext &Context = getContext();
  const CXXRecordDecl *ClassDecl =
      Prologue ? cast<CXXConstructorDecl>(CurGD.getDecl())->getParent()
               : cast<CXXDestructorDecl>(CurGD.getDecl())->getParent();
  if (!ClassDecl->mayInsertExtraPadding()) return;

  struct SizeAndOffset {
    uint64_t Size;
    uint64_t Offset;
  };

  unsigned PtrSize = CGM.getDataLayout().getPointerSizeInBits();
  const ASTRecordLayout &Info = Context.getASTRecordLayout(ClassDecl);

  // Populate sizes and offsets of fields.
  SmallVector<SizeAndOffset, 16> SSV(Info.getFieldCount());
  for (unsigned i = 0, e = Info.getFieldCount(); i != e; ++i)
    SSV[i].Offset =
        Context.toCharUnitsFromBits(Info.getFieldOffset(i)).getQuantity();

  size_t NumFields = 0;
  for (const auto *Field : ClassDecl->fields()) {
    const FieldDecl *D = Field;
    auto FieldInfo = Context.getTypeInfoInChars(D->getType());
    CharUnits FieldSize = FieldInfo.Width;
    assert(NumFields < SSV.size());
    SSV[NumFields].Size = D->isBitField() ? 0 : FieldSize.getQuantity();
    NumFields++;
  }
  assert(NumFields == SSV.size());
  if (SSV.size() <= 1) return;

  // We will insert calls to __asan_* run-time functions.
  // LLVM AddressSanitizer pass may decide to inline them later.
  llvm::Type *Args[2] = {IntPtrTy, IntPtrTy};
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.VoidTy, Args, false);
  llvm::FunctionCallee F = CGM.CreateRuntimeFunction(
      FTy, Prologue ? "__asan_poison_intra_object_redzone"
                    : "__asan_unpoison_intra_object_redzone");

  llvm::Value *ThisPtr = LoadCXXThis();
  ThisPtr = Builder.CreatePtrToInt(ThisPtr, IntPtrTy);
  uint64_t TypeSize = Info.getNonVirtualSize().getQuantity();
  // For each field check if it has sufficient padding,
  // if so (un)poison it with a call.
  for (size_t i = 0; i < SSV.size(); i++) {
    uint64_t AsanAlignment = 8;
    uint64_t NextField = i == SSV.size() - 1 ? TypeSize : SSV[i + 1].Offset;
    uint64_t PoisonSize = NextField - SSV[i].Offset - SSV[i].Size;
    uint64_t EndOffset = SSV[i].Offset + SSV[i].Size;
    if (PoisonSize < AsanAlignment || !SSV[i].Size ||
        (NextField % AsanAlignment) != 0)
      continue;
    Builder.CreateCall(
        F, {Builder.CreateAdd(ThisPtr, Builder.getIntN(PtrSize, EndOffset)),
            Builder.getIntN(PtrSize, PoisonSize)});
  }
}

/// EmitConstructorBody - Emits the body of the current constructor.
void CodeGenFunction::EmitConstructorBody(FunctionArgList &Args) {
  EmitAsanPrologueOrEpilogue(true);
  const CXXConstructorDecl *Ctor = cast<CXXConstructorDecl>(CurGD.getDecl());
  CXXCtorType CtorType = CurGD.getCtorType();

  assert((CGM.getTarget().getCXXABI().hasConstructorVariants() ||
          CtorType == Ctor_Complete) &&
         "can only generate complete ctor for this ABI");

  // Before we go any further, try the complete->base constructor
  // delegation optimization.
  if (CtorType == Ctor_Complete && IsConstructorDelegationValid(Ctor) &&
      CGM.getTarget().getCXXABI().hasConstructorVariants()) {
    EmitDelegateCXXConstructorCall(Ctor, Ctor_Base, Args, Ctor->getEndLoc());
    return;
  }

  const FunctionDecl *Definition = nullptr;
  Stmt *Body = Ctor->getBody(Definition);
  assert(Definition == Ctor && "emitting wrong constructor body");

  // Enter the function-try-block before the constructor prologue if
  // applicable.
  bool IsTryBody = isa_and_nonnull<CXXTryStmt>(Body);
  if (IsTryBody)
    EnterCXXTryStmt(*cast<CXXTryStmt>(Body), true);

  incrementProfileCounter(Body);
  maybeCreateMCDCCondBitmap();

  RunCleanupsScope RunCleanups(*this);

  // TODO: in restricted cases, we can emit the vbase initializers of
  // a complete ctor and then delegate to the base ctor.

  // Emit the constructor prologue, i.e. the base and member
  // initializers.
  EmitCtorPrologue(Ctor, CtorType, Args);

  // Emit the body of the statement.
  if (IsTryBody)
    EmitStmt(cast<CXXTryStmt>(Body)->getTryBlock());
  else if (Body)
    EmitStmt(Body);

  // Emit any cleanup blocks associated with the member or base
  // initializers, which includes (along the exceptional path) the
  // destructors for those members and bases that were fully
  // constructed.
  RunCleanups.ForceCleanup();

  if (IsTryBody)
    ExitCXXTryStmt(*cast<CXXTryStmt>(Body), true);
}

namespace {
  /// RAII object to indicate that codegen is copying the value representation
  /// instead of the object representation. Useful when copying a struct or
  /// class which has uninitialized members and we're only performing
  /// lvalue-to-rvalue conversion on the object but not its members.
  class CopyingValueRepresentation {
  public:
    explicit CopyingValueRepresentation(CodeGenFunction &CGF)
        : CGF(CGF), OldSanOpts(CGF.SanOpts) {
      CGF.SanOpts.set(SanitizerKind::Bool, false);
      CGF.SanOpts.set(SanitizerKind::Enum, false);
    }
    ~CopyingValueRepresentation() {
      CGF.SanOpts = OldSanOpts;
    }
  private:
    CodeGenFunction &CGF;
    SanitizerSet OldSanOpts;
  };
} // end anonymous namespace

namespace {
  class FieldMemcpyizer {
  public:
    FieldMemcpyizer(CodeGenFunction &CGF, const CXXRecordDecl *ClassDecl,
                    const VarDecl *SrcRec)
      : CGF(CGF), ClassDecl(ClassDecl), SrcRec(SrcRec),
        RecLayout(CGF.getContext().getASTRecordLayout(ClassDecl)),
        FirstField(nullptr), LastField(nullptr), FirstFieldOffset(0),
        LastFieldOffset(0), LastAddedFieldIndex(0) {}

    bool isMemcpyableField(FieldDecl *F) const {
      // Never memcpy fields when we are adding poisoned paddings.
      if (CGF.getContext().getLangOpts().SanitizeAddressFieldPadding)
        return false;
      Qualifiers Qual = F->getType().getQualifiers();
      if (Qual.hasVolatile() || Qual.hasObjCLifetime())
        return false;
      return true;
    }

    void addMemcpyableField(FieldDecl *F) {
      if (isEmptyFieldForLayout(CGF.getContext(), F))
        return;
      if (!FirstField)
        addInitialField(F);
      else
        addNextField(F);
    }

    CharUnits getMemcpySize(uint64_t FirstByteOffset) const {
      ASTContext &Ctx = CGF.getContext();
      unsigned LastFieldSize =
          LastField->isBitField()
              ? LastField->getBitWidthValue(Ctx)
              : Ctx.toBits(
                    Ctx.getTypeInfoDataSizeInChars(LastField->getType()).Width);
      uint64_t MemcpySizeBits = LastFieldOffset + LastFieldSize -
                                FirstByteOffset + Ctx.getCharWidth() - 1;
      CharUnits MemcpySize = Ctx.toCharUnitsFromBits(MemcpySizeBits);
      return MemcpySize;
    }

    void emitMemcpy() {
      // Give the subclass a chance to bail out if it feels the memcpy isn't
      // worth it (e.g. Hasn't aggregated enough data).
      if (!FirstField) {
        return;
      }

      uint64_t FirstByteOffset;
      if (FirstField->isBitField()) {
        const CGRecordLayout &RL =
          CGF.getTypes().getCGRecordLayout(FirstField->getParent());
        const CGBitFieldInfo &BFInfo = RL.getBitFieldInfo(FirstField);
        // FirstFieldOffset is not appropriate for bitfields,
        // we need to use the storage offset instead.
        FirstByteOffset = CGF.getContext().toBits(BFInfo.StorageOffset);
      } else {
        FirstByteOffset = FirstFieldOffset;
      }

      CharUnits MemcpySize = getMemcpySize(FirstByteOffset);
      QualType RecordTy = CGF.getContext().getTypeDeclType(ClassDecl);
      Address ThisPtr = CGF.LoadCXXThisAddress();
      LValue DestLV = CGF.MakeAddrLValue(ThisPtr, RecordTy);
      LValue Dest = CGF.EmitLValueForFieldInitialization(DestLV, FirstField);
      llvm::Value *SrcPtr = CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(SrcRec));
      LValue SrcLV = CGF.MakeNaturalAlignAddrLValue(SrcPtr, RecordTy);
      LValue Src = CGF.EmitLValueForFieldInitialization(SrcLV, FirstField);

      emitMemcpyIR(
          Dest.isBitField() ? Dest.getBitFieldAddress() : Dest.getAddress(),
          Src.isBitField() ? Src.getBitFieldAddress() : Src.getAddress(),
          MemcpySize);
      reset();
    }

    void reset() {
      FirstField = nullptr;
    }

  protected:
    CodeGenFunction &CGF;
    const CXXRecordDecl *ClassDecl;

  private:
    void emitMemcpyIR(Address DestPtr, Address SrcPtr, CharUnits Size) {
      DestPtr = DestPtr.withElementType(CGF.Int8Ty);
      SrcPtr = SrcPtr.withElementType(CGF.Int8Ty);
      CGF.Builder.CreateMemCpy(DestPtr, SrcPtr, Size.getQuantity());
    }

    void addInitialField(FieldDecl *F) {
      FirstField = F;
      LastField = F;
      FirstFieldOffset = RecLayout.getFieldOffset(F->getFieldIndex());
      LastFieldOffset = FirstFieldOffset;
      LastAddedFieldIndex = F->getFieldIndex();
    }

    void addNextField(FieldDecl *F) {
      // For the most part, the following invariant will hold:
      //   F->getFieldIndex() == LastAddedFieldIndex + 1
      // The one exception is that Sema won't add a copy-initializer for an
      // unnamed bitfield, which will show up here as a gap in the sequence.
      assert(F->getFieldIndex() >= LastAddedFieldIndex + 1 &&
             "Cannot aggregate fields out of order.");
      LastAddedFieldIndex = F->getFieldIndex();

      // The 'first' and 'last' fields are chosen by offset, rather than field
      // index. This allows the code to support bitfields, as well as regular
      // fields.
      uint64_t FOffset = RecLayout.getFieldOffset(F->getFieldIndex());
      if (FOffset < FirstFieldOffset) {
        FirstField = F;
        FirstFieldOffset = FOffset;
      } else if (FOffset >= LastFieldOffset) {
        LastField = F;
        LastFieldOffset = FOffset;
      }
    }

    const VarDecl *SrcRec;
    const ASTRecordLayout &RecLayout;
    FieldDecl *FirstField;
    FieldDecl *LastField;
    uint64_t FirstFieldOffset, LastFieldOffset;
    unsigned LastAddedFieldIndex;
  };

  class ConstructorMemcpyizer : public FieldMemcpyizer {
  private:
    /// Get source argument for copy constructor. Returns null if not a copy
    /// constructor.
    static const VarDecl *getTrivialCopySource(CodeGenFunction &CGF,
                                               const CXXConstructorDecl *CD,
                                               FunctionArgList &Args) {
      if (CD->isCopyOrMoveConstructor() && CD->isDefaulted())
        return Args[CGF.CGM.getCXXABI().getSrcArgforCopyCtor(CD, Args)];
      return nullptr;
    }

    // Returns true if a CXXCtorInitializer represents a member initialization
    // that can be rolled into a memcpy.
    bool isMemberInitMemcpyable(CXXCtorInitializer *MemberInit) const {
      if (!MemcpyableCtor)
        return false;
      FieldDecl *Field = MemberInit->getMember();
      assert(Field && "No field for member init.");
      QualType FieldType = Field->getType();
      CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(MemberInit->getInit());

      // Bail out on non-memcpyable, not-trivially-copyable members.
      if (!(CE && isMemcpyEquivalentSpecialMember(CE->getConstructor())) &&
          !(FieldType.isTriviallyCopyableType(CGF.getContext()) ||
            FieldType->isReferenceType()))
        return false;

      // Bail out on volatile fields.
      if (!isMemcpyableField(Field))
        return false;

      // Otherwise we're good.
      return true;
    }

  public:
    ConstructorMemcpyizer(CodeGenFunction &CGF, const CXXConstructorDecl *CD,
                          FunctionArgList &Args)
      : FieldMemcpyizer(CGF, CD->getParent(), getTrivialCopySource(CGF, CD, Args)),
        ConstructorDecl(CD),
        MemcpyableCtor(CD->isDefaulted() &&
                       CD->isCopyOrMoveConstructor() &&
                       CGF.getLangOpts().getGC() == LangOptions::NonGC),
        Args(Args) { }

    void addMemberInitializer(CXXCtorInitializer *MemberInit) {
      if (isMemberInitMemcpyable(MemberInit)) {
        AggregatedInits.push_back(MemberInit);
        addMemcpyableField(MemberInit->getMember());
      } else {
        emitAggregatedInits();
        EmitMemberInitializer(CGF, ConstructorDecl->getParent(), MemberInit,
                              ConstructorDecl, Args);
      }
    }

    void emitAggregatedInits() {
      if (AggregatedInits.size() <= 1) {
        // This memcpy is too small to be worthwhile. Fall back on default
        // codegen.
        if (!AggregatedInits.empty()) {
          CopyingValueRepresentation CVR(CGF);
          EmitMemberInitializer(CGF, ConstructorDecl->getParent(),
                                AggregatedInits[0], ConstructorDecl, Args);
          AggregatedInits.clear();
        }
        reset();
        return;
      }

      pushEHDestructors();
      emitMemcpy();
      AggregatedInits.clear();
    }

    void pushEHDestructors() {
      Address ThisPtr = CGF.LoadCXXThisAddress();
      QualType RecordTy = CGF.getContext().getTypeDeclType(ClassDecl);
      LValue LHS = CGF.MakeAddrLValue(ThisPtr, RecordTy);

      for (unsigned i = 0; i < AggregatedInits.size(); ++i) {
        CXXCtorInitializer *MemberInit = AggregatedInits[i];
        QualType FieldType = MemberInit->getAnyMember()->getType();
        QualType::DestructionKind dtorKind = FieldType.isDestructedType();
        if (!CGF.needsEHCleanup(dtorKind))
          continue;
        LValue FieldLHS = LHS;
        EmitLValueForAnyFieldInitialization(CGF, MemberInit, FieldLHS);
        CGF.pushEHDestroy(dtorKind, FieldLHS.getAddress(), FieldType);
      }
    }

    void finish() {
      emitAggregatedInits();
    }

  private:
    const CXXConstructorDecl *ConstructorDecl;
    bool MemcpyableCtor;
    FunctionArgList &Args;
    SmallVector<CXXCtorInitializer*, 16> AggregatedInits;
  };

  class AssignmentMemcpyizer : public FieldMemcpyizer {
  private:
    // Returns the memcpyable field copied by the given statement, if one
    // exists. Otherwise returns null.
    FieldDecl *getMemcpyableField(Stmt *S) {
      if (!AssignmentsMemcpyable)
        return nullptr;
      if (BinaryOperator *BO = dyn_cast<BinaryOperator>(S)) {
        // Recognise trivial assignments.
        if (BO->getOpcode() != BO_Assign)
          return nullptr;
        MemberExpr *ME = dyn_cast<MemberExpr>(BO->getLHS());
        if (!ME)
          return nullptr;
        FieldDecl *Field = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (!Field || !isMemcpyableField(Field))
          return nullptr;
        Stmt *RHS = BO->getRHS();
        if (ImplicitCastExpr *EC = dyn_cast<ImplicitCastExpr>(RHS))
          RHS = EC->getSubExpr();
        if (!RHS)
          return nullptr;
        if (MemberExpr *ME2 = dyn_cast<MemberExpr>(RHS)) {
          if (ME2->getMemberDecl() == Field)
            return Field;
        }
        return nullptr;
      } else if (CXXMemberCallExpr *MCE = dyn_cast<CXXMemberCallExpr>(S)) {
        CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(MCE->getCalleeDecl());
        if (!(MD && isMemcpyEquivalentSpecialMember(MD)))
          return nullptr;
        MemberExpr *IOA = dyn_cast<MemberExpr>(MCE->getImplicitObjectArgument());
        if (!IOA)
          return nullptr;
        FieldDecl *Field = dyn_cast<FieldDecl>(IOA->getMemberDecl());
        if (!Field || !isMemcpyableField(Field))
          return nullptr;
        MemberExpr *Arg0 = dyn_cast<MemberExpr>(MCE->getArg(0));
        if (!Arg0 || Field != dyn_cast<FieldDecl>(Arg0->getMemberDecl()))
          return nullptr;
        return Field;
      } else if (CallExpr *CE = dyn_cast<CallExpr>(S)) {
        FunctionDecl *FD = dyn_cast<FunctionDecl>(CE->getCalleeDecl());
        if (!FD || FD->getBuiltinID() != Builtin::BI__builtin_memcpy)
          return nullptr;
        Expr *DstPtr = CE->getArg(0);
        if (ImplicitCastExpr *DC = dyn_cast<ImplicitCastExpr>(DstPtr))
          DstPtr = DC->getSubExpr();
        UnaryOperator *DUO = dyn_cast<UnaryOperator>(DstPtr);
        if (!DUO || DUO->getOpcode() != UO_AddrOf)
          return nullptr;
        MemberExpr *ME = dyn_cast<MemberExpr>(DUO->getSubExpr());
        if (!ME)
          return nullptr;
        FieldDecl *Field = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (!Field || !isMemcpyableField(Field))
          return nullptr;
        Expr *SrcPtr = CE->getArg(1);
        if (ImplicitCastExpr *SC = dyn_cast<ImplicitCastExpr>(SrcPtr))
          SrcPtr = SC->getSubExpr();
        UnaryOperator *SUO = dyn_cast<UnaryOperator>(SrcPtr);
        if (!SUO || SUO->getOpcode() != UO_AddrOf)
          return nullptr;
        MemberExpr *ME2 = dyn_cast<MemberExpr>(SUO->getSubExpr());
        if (!ME2 || Field != dyn_cast<FieldDecl>(ME2->getMemberDecl()))
          return nullptr;
        return Field;
      }

      return nullptr;
    }

    bool AssignmentsMemcpyable;
    SmallVector<Stmt*, 16> AggregatedStmts;

  public:
    AssignmentMemcpyizer(CodeGenFunction &CGF, const CXXMethodDecl *AD,
                         FunctionArgList &Args)
      : FieldMemcpyizer(CGF, AD->getParent(), Args[Args.size() - 1]),
        AssignmentsMemcpyable(CGF.getLangOpts().getGC() == LangOptions::NonGC) {
      assert(Args.size() == 2);
    }

    void emitAssignment(Stmt *S) {
      FieldDecl *F = getMemcpyableField(S);
      if (F) {
        addMemcpyableField(F);
        AggregatedStmts.push_back(S);
      } else {
        emitAggregatedStmts();
        CGF.EmitStmt(S);
      }
    }

    void emitAggregatedStmts() {
      if (AggregatedStmts.size() <= 1) {
        if (!AggregatedStmts.empty()) {
          CopyingValueRepresentation CVR(CGF);
          CGF.EmitStmt(AggregatedStmts[0]);
        }
        reset();
      }

      emitMemcpy();
      AggregatedStmts.clear();
    }

    void finish() {
      emitAggregatedStmts();
    }
  };
} // end anonymous namespace

static bool isInitializerOfDynamicClass(const CXXCtorInitializer *BaseInit) {
  const Type *BaseType = BaseInit->getBaseClass();
  const auto *BaseClassDecl =
      cast<CXXRecordDecl>(BaseType->castAs<RecordType>()->getDecl());
  return BaseClassDecl->isDynamicClass();
}

/// EmitCtorPrologue - This routine generates necessary code to initialize
/// base classes and non-static data members belonging to this constructor.
void CodeGenFunction::EmitCtorPrologue(const CXXConstructorDecl *CD,
                                       CXXCtorType CtorType,
                                       FunctionArgList &Args) {
  if (CD->isDelegatingConstructor())
    return EmitDelegatingCXXConstructorCall(CD, Args);

  const CXXRecordDecl *ClassDecl = CD->getParent();

  CXXConstructorDecl::init_const_iterator B = CD->init_begin(),
                                          E = CD->init_end();

  // Virtual base initializers first, if any. They aren't needed if:
  // - This is a base ctor variant
  // - There are no vbases
  // - The class is abstract, so a complete object of it cannot be constructed
  //
  // The check for an abstract class is necessary because sema may not have
  // marked virtual base destructors referenced.
  bool ConstructVBases = CtorType != Ctor_Base &&
                         ClassDecl->getNumVBases() != 0 &&
                         !ClassDecl->isAbstract();

  // In the Microsoft C++ ABI, there are no constructor variants. Instead, the
  // constructor of a class with virtual bases takes an additional parameter to
  // conditionally construct the virtual bases. Emit that check here.
  llvm::BasicBlock *BaseCtorContinueBB = nullptr;
  if (ConstructVBases &&
      !CGM.getTarget().getCXXABI().hasConstructorVariants()) {
    BaseCtorContinueBB =
        CGM.getCXXABI().EmitCtorCompleteObjectHandler(*this, ClassDecl);
    assert(BaseCtorContinueBB);
  }

  for (; B != E && (*B)->isBaseInitializer() && (*B)->isBaseVirtual(); B++) {
    if (!ConstructVBases)
      continue;
    SaveAndRestore ThisRAII(CXXThisValue);
    if (CGM.getCodeGenOpts().StrictVTablePointers &&
        CGM.getCodeGenOpts().OptimizationLevel > 0 &&
        isInitializerOfDynamicClass(*B))
      CXXThisValue = Builder.CreateLaunderInvariantGroup(LoadCXXThis());
    EmitBaseInitializer(*this, ClassDecl, *B);
  }

  if (BaseCtorContinueBB) {
    // Complete object handler should continue to the remaining initializers.
    Builder.CreateBr(BaseCtorContinueBB);
    EmitBlock(BaseCtorContinueBB);
  }

  // Then, non-virtual base initializers.
  for (; B != E && (*B)->isBaseInitializer(); B++) {
    assert(!(*B)->isBaseVirtual());
    SaveAndRestore ThisRAII(CXXThisValue);
    if (CGM.getCodeGenOpts().StrictVTablePointers &&
        CGM.getCodeGenOpts().OptimizationLevel > 0 &&
        isInitializerOfDynamicClass(*B))
      CXXThisValue = Builder.CreateLaunderInvariantGroup(LoadCXXThis());
    EmitBaseInitializer(*this, ClassDecl, *B);
  }

  InitializeVTablePointers(ClassDecl);

  // And finally, initialize class members.
  FieldConstructionScope FCS(*this, LoadCXXThisAddress());
  ConstructorMemcpyizer CM(*this, CD, Args);
  for (; B != E; B++) {
    CXXCtorInitializer *Member = (*B);
    assert(!Member->isBaseInitializer());
    assert(Member->isAnyMemberInitializer() &&
           "Delegating initializer on non-delegating constructor");
    CM.addMemberInitializer(Member);
  }
  CM.finish();
}

static bool
FieldHasTrivialDestructorBody(ASTContext &Context, const FieldDecl *Field);

static bool
HasTrivialDestructorBody(ASTContext &Context,
                         const CXXRecordDecl *BaseClassDecl,
                         const CXXRecordDecl *MostDerivedClassDecl)
{
  // If the destructor is trivial we don't have to check anything else.
  if (BaseClassDecl->hasTrivialDestructor())
    return true;

  if (!BaseClassDecl->getDestructor()->hasTrivialBody())
    return false;

  // Check fields.
  for (const auto *Field : BaseClassDecl->fields())
    if (!FieldHasTrivialDestructorBody(Context, Field))
      return false;

  // Check non-virtual bases.
  for (const auto &I : BaseClassDecl->bases()) {
    if (I.isVirtual())
      continue;

    const CXXRecordDecl *NonVirtualBase =
      cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());
    if (!HasTrivialDestructorBody(Context, NonVirtualBase,
                                  MostDerivedClassDecl))
      return false;
  }

  if (BaseClassDecl == MostDerivedClassDecl) {
    // Check virtual bases.
    for (const auto &I : BaseClassDecl->vbases()) {
      const CXXRecordDecl *VirtualBase =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());
      if (!HasTrivialDestructorBody(Context, VirtualBase,
                                    MostDerivedClassDecl))
        return false;
    }
  }

  return true;
}

static bool
FieldHasTrivialDestructorBody(ASTContext &Context,
                                          const FieldDecl *Field)
{
  QualType FieldBaseElementType = Context.getBaseElementType(Field->getType());

  const RecordType *RT = FieldBaseElementType->getAs<RecordType>();
  if (!RT)
    return true;

  CXXRecordDecl *FieldClassDecl = cast<CXXRecordDecl>(RT->getDecl());

  // The destructor for an implicit anonymous union member is never invoked.
  if (FieldClassDecl->isUnion() && FieldClassDecl->isAnonymousStructOrUnion())
    return true;

  return HasTrivialDestructorBody(Context, FieldClassDecl, FieldClassDecl);
}

/// CanSkipVTablePointerInitialization - Check whether we need to initialize
/// any vtable pointers before calling this destructor.
static bool CanSkipVTablePointerInitialization(CodeGenFunction &CGF,
                                               const CXXDestructorDecl *Dtor) {
  const CXXRecordDecl *ClassDecl = Dtor->getParent();
  if (!ClassDecl->isDynamicClass())
    return true;

  // For a final class, the vtable pointer is known to already point to the
  // class's vtable.
  if (ClassDecl->isEffectivelyFinal())
    return true;

  if (!Dtor->hasTrivialBody())
    return false;

  // Check the fields.
  for (const auto *Field : ClassDecl->fields())
    if (!FieldHasTrivialDestructorBody(CGF.getContext(), Field))
      return false;

  return true;
}

/// EmitDestructorBody - Emits the body of the current destructor.
void CodeGenFunction::EmitDestructorBody(FunctionArgList &Args) {
  const CXXDestructorDecl *Dtor = cast<CXXDestructorDecl>(CurGD.getDecl());
  CXXDtorType DtorType = CurGD.getDtorType();

  // For an abstract class, non-base destructors are never used (and can't
  // be emitted in general, because vbase dtors may not have been validated
  // by Sema), but the Itanium ABI doesn't make them optional and Clang may
  // in fact emit references to them from other compilations, so emit them
  // as functions containing a trap instruction.
  if (DtorType != Dtor_Base && Dtor->getParent()->isAbstract()) {
    llvm::CallInst *TrapCall = EmitTrapCall(llvm::Intrinsic::trap);
    TrapCall->setDoesNotReturn();
    TrapCall->setDoesNotThrow();
    Builder.CreateUnreachable();
    Builder.ClearInsertionPoint();
    return;
  }

  Stmt *Body = Dtor->getBody();
  if (Body) {
    incrementProfileCounter(Body);
    maybeCreateMCDCCondBitmap();
  }

  // The call to operator delete in a deleting destructor happens
  // outside of the function-try-block, which means it's always
  // possible to delegate the destructor body to the complete
  // destructor.  Do so.
  if (DtorType == Dtor_Deleting) {
    RunCleanupsScope DtorEpilogue(*this);
    EnterDtorCleanups(Dtor, Dtor_Deleting);
    if (HaveInsertPoint()) {
      QualType ThisTy = Dtor->getFunctionObjectParameterType();
      EmitCXXDestructorCall(Dtor, Dtor_Complete, /*ForVirtualBase=*/false,
                            /*Delegating=*/false, LoadCXXThisAddress(), ThisTy);
    }
    return;
  }

  // If the body is a function-try-block, enter the try before
  // anything else.
  bool isTryBody = isa_and_nonnull<CXXTryStmt>(Body);
  if (isTryBody)
    EnterCXXTryStmt(*cast<CXXTryStmt>(Body), true);
  EmitAsanPrologueOrEpilogue(false);

  // Enter the epilogue cleanups.
  RunCleanupsScope DtorEpilogue(*this);

  // If this is the complete variant, just invoke the base variant;
  // the epilogue will destruct the virtual bases.  But we can't do
  // this optimization if the body is a function-try-block, because
  // we'd introduce *two* handler blocks.  In the Microsoft ABI, we
  // always delegate because we might not have a definition in this TU.
  switch (DtorType) {
  case Dtor_Comdat: llvm_unreachable("not expecting a COMDAT");
  case Dtor_Deleting: llvm_unreachable("already handled deleting case");

  case Dtor_Complete:
    assert((Body || getTarget().getCXXABI().isMicrosoft()) &&
           "can't emit a dtor without a body for non-Microsoft ABIs");

    // Enter the cleanup scopes for virtual bases.
    EnterDtorCleanups(Dtor, Dtor_Complete);

    if (!isTryBody) {
      QualType ThisTy = Dtor->getFunctionObjectParameterType();
      EmitCXXDestructorCall(Dtor, Dtor_Base, /*ForVirtualBase=*/false,
                            /*Delegating=*/false, LoadCXXThisAddress(), ThisTy);
      break;
    }

    // Fallthrough: act like we're in the base variant.
    [[fallthrough]];

  case Dtor_Base:
    assert(Body);

    // Enter the cleanup scopes for fields and non-virtual bases.
    EnterDtorCleanups(Dtor, Dtor_Base);

    // Initialize the vtable pointers before entering the body.
    if (!CanSkipVTablePointerInitialization(*this, Dtor)) {
      // Insert the llvm.launder.invariant.group intrinsic before initializing
      // the vptrs to cancel any previous assumptions we might have made.
      if (CGM.getCodeGenOpts().StrictVTablePointers &&
          CGM.getCodeGenOpts().OptimizationLevel > 0)
        CXXThisValue = Builder.CreateLaunderInvariantGroup(LoadCXXThis());
      InitializeVTablePointers(Dtor->getParent());
    }

    if (isTryBody)
      EmitStmt(cast<CXXTryStmt>(Body)->getTryBlock());
    else if (Body)
      EmitStmt(Body);
    else {
      assert(Dtor->isImplicit() && "bodyless dtor not implicit");
      // nothing to do besides what's in the epilogue
    }
    // -fapple-kext must inline any call to this dtor into
    // the caller's body.
    if (getLangOpts().AppleKext)
      CurFn->addFnAttr(llvm::Attribute::AlwaysInline);

    break;
  }

  // Jump out through the epilogue cleanups.
  DtorEpilogue.ForceCleanup();

  // Exit the try if applicable.
  if (isTryBody)
    ExitCXXTryStmt(*cast<CXXTryStmt>(Body), true);
}

void CodeGenFunction::emitImplicitAssignmentOperatorBody(FunctionArgList &Args) {
  const CXXMethodDecl *AssignOp = cast<CXXMethodDecl>(CurGD.getDecl());
  const Stmt *RootS = AssignOp->getBody();
  assert(isa<CompoundStmt>(RootS) &&
         "Body of an implicit assignment operator should be compound stmt.");
  const CompoundStmt *RootCS = cast<CompoundStmt>(RootS);

  LexicalScope Scope(*this, RootCS->getSourceRange());

  incrementProfileCounter(RootCS);
  maybeCreateMCDCCondBitmap();
  AssignmentMemcpyizer AM(*this, AssignOp, Args);
  for (auto *I : RootCS->body())
    AM.emitAssignment(I);
  AM.finish();
}

namespace {
  llvm::Value *LoadThisForDtorDelete(CodeGenFunction &CGF,
                                     const CXXDestructorDecl *DD) {
    if (Expr *ThisArg = DD->getOperatorDeleteThisArg())
      return CGF.EmitScalarExpr(ThisArg);
    return CGF.LoadCXXThis();
  }

  /// Call the operator delete associated with the current destructor.
  struct CallDtorDelete final : EHScopeStack::Cleanup {
    CallDtorDelete() {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const CXXDestructorDecl *Dtor = cast<CXXDestructorDecl>(CGF.CurCodeDecl);
      const CXXRecordDecl *ClassDecl = Dtor->getParent();
      CGF.EmitDeleteCall(Dtor->getOperatorDelete(),
                         LoadThisForDtorDelete(CGF, Dtor),
                         CGF.getContext().getTagDeclType(ClassDecl));
    }
  };

  void EmitConditionalDtorDeleteCall(CodeGenFunction &CGF,
                                     llvm::Value *ShouldDeleteCondition,
                                     bool ReturnAfterDelete) {
    llvm::BasicBlock *callDeleteBB = CGF.createBasicBlock("dtor.call_delete");
    llvm::BasicBlock *continueBB = CGF.createBasicBlock("dtor.continue");
    llvm::Value *ShouldCallDelete
      = CGF.Builder.CreateIsNull(ShouldDeleteCondition);
    CGF.Builder.CreateCondBr(ShouldCallDelete, continueBB, callDeleteBB);

    CGF.EmitBlock(callDeleteBB);
    const CXXDestructorDecl *Dtor = cast<CXXDestructorDecl>(CGF.CurCodeDecl);
    const CXXRecordDecl *ClassDecl = Dtor->getParent();
    CGF.EmitDeleteCall(Dtor->getOperatorDelete(),
                       LoadThisForDtorDelete(CGF, Dtor),
                       CGF.getContext().getTagDeclType(ClassDecl));
    assert(Dtor->getOperatorDelete()->isDestroyingOperatorDelete() ==
               ReturnAfterDelete &&
           "unexpected value for ReturnAfterDelete");
    if (ReturnAfterDelete)
      CGF.EmitBranchThroughCleanup(CGF.ReturnBlock);
    else
      CGF.Builder.CreateBr(continueBB);

    CGF.EmitBlock(continueBB);
  }

  struct CallDtorDeleteConditional final : EHScopeStack::Cleanup {
    llvm::Value *ShouldDeleteCondition;

  public:
    CallDtorDeleteConditional(llvm::Value *ShouldDeleteCondition)
        : ShouldDeleteCondition(ShouldDeleteCondition) {
      assert(ShouldDeleteCondition != nullptr);
    }

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      EmitConditionalDtorDeleteCall(CGF, ShouldDeleteCondition,
                                    /*ReturnAfterDelete*/false);
    }
  };

  class DestroyField  final : public EHScopeStack::Cleanup {
    const FieldDecl *field;
    CodeGenFunction::Destroyer *destroyer;
    bool useEHCleanupForArray;

  public:
    DestroyField(const FieldDecl *field, CodeGenFunction::Destroyer *destroyer,
                 bool useEHCleanupForArray)
        : field(field), destroyer(destroyer),
          useEHCleanupForArray(useEHCleanupForArray) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      // Find the address of the field.
      Address thisValue = CGF.LoadCXXThisAddress();
      QualType RecordTy = CGF.getContext().getTagDeclType(field->getParent());
      LValue ThisLV = CGF.MakeAddrLValue(thisValue, RecordTy);
      LValue LV = CGF.EmitLValueForField(ThisLV, field);
      assert(LV.isSimple());

      CGF.emitDestroy(LV.getAddress(), field->getType(), destroyer,
                      flags.isForNormalCleanup() && useEHCleanupForArray);
    }
  };

  class DeclAsInlineDebugLocation {
    CGDebugInfo *DI;
    llvm::MDNode *InlinedAt;
    std::optional<ApplyDebugLocation> Location;

  public:
    DeclAsInlineDebugLocation(CodeGenFunction &CGF, const NamedDecl &Decl)
        : DI(CGF.getDebugInfo()) {
      if (!DI)
        return;
      InlinedAt = DI->getInlinedAt();
      DI->setInlinedAt(CGF.Builder.getCurrentDebugLocation());
      Location.emplace(CGF, Decl.getLocation());
    }

    ~DeclAsInlineDebugLocation() {
      if (!DI)
        return;
      Location.reset();
      DI->setInlinedAt(InlinedAt);
    }
  };

  static void EmitSanitizerDtorCallback(
      CodeGenFunction &CGF, StringRef Name, llvm::Value *Ptr,
      std::optional<CharUnits::QuantityType> PoisonSize = {}) {
    CodeGenFunction::SanitizerScope SanScope(&CGF);
    // Pass in void pointer and size of region as arguments to runtime
    // function
    SmallVector<llvm::Value *, 2> Args = {Ptr};
    SmallVector<llvm::Type *, 2> ArgTypes = {CGF.VoidPtrTy};

    if (PoisonSize.has_value()) {
      Args.emplace_back(llvm::ConstantInt::get(CGF.SizeTy, *PoisonSize));
      ArgTypes.emplace_back(CGF.SizeTy);
    }

    llvm::FunctionType *FnType =
        llvm::FunctionType::get(CGF.VoidTy, ArgTypes, false);
    llvm::FunctionCallee Fn = CGF.CGM.CreateRuntimeFunction(FnType, Name);

    CGF.EmitNounwindRuntimeCall(Fn, Args);
  }

  static void
  EmitSanitizerDtorFieldsCallback(CodeGenFunction &CGF, llvm::Value *Ptr,
                                  CharUnits::QuantityType PoisonSize) {
    EmitSanitizerDtorCallback(CGF, "__sanitizer_dtor_callback_fields", Ptr,
                              PoisonSize);
  }

  /// Poison base class with a trivial destructor.
  struct SanitizeDtorTrivialBase final : EHScopeStack::Cleanup {
    const CXXRecordDecl *BaseClass;
    bool BaseIsVirtual;
    SanitizeDtorTrivialBase(const CXXRecordDecl *Base, bool BaseIsVirtual)
        : BaseClass(Base), BaseIsVirtual(BaseIsVirtual) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const CXXRecordDecl *DerivedClass =
          cast<CXXMethodDecl>(CGF.CurCodeDecl)->getParent();

      Address Addr = CGF.GetAddressOfDirectBaseInCompleteClass(
          CGF.LoadCXXThisAddress(), DerivedClass, BaseClass, BaseIsVirtual);

      const ASTRecordLayout &BaseLayout =
          CGF.getContext().getASTRecordLayout(BaseClass);
      CharUnits BaseSize = BaseLayout.getSize();

      if (!BaseSize.isPositive())
        return;

      // Use the base class declaration location as inline DebugLocation. All
      // fields of the class are destroyed.
      DeclAsInlineDebugLocation InlineHere(CGF, *BaseClass);
      EmitSanitizerDtorFieldsCallback(CGF, Addr.emitRawPointer(CGF),
                                      BaseSize.getQuantity());

      // Prevent the current stack frame from disappearing from the stack trace.
      CGF.CurFn->addFnAttr("disable-tail-calls", "true");
    }
  };

  class SanitizeDtorFieldRange final : public EHScopeStack::Cleanup {
    const CXXDestructorDecl *Dtor;
    unsigned StartIndex;
    unsigned EndIndex;

  public:
    SanitizeDtorFieldRange(const CXXDestructorDecl *Dtor, unsigned StartIndex,
                           unsigned EndIndex)
        : Dtor(Dtor), StartIndex(StartIndex), EndIndex(EndIndex) {}

    // Generate function call for handling object poisoning.
    // Disables tail call elimination, to prevent the current stack frame
    // from disappearing from the stack trace.
    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const ASTContext &Context = CGF.getContext();
      const ASTRecordLayout &Layout =
          Context.getASTRecordLayout(Dtor->getParent());

      // It's a first trivial field so it should be at the begining of a char,
      // still round up start offset just in case.
      CharUnits PoisonStart = Context.toCharUnitsFromBits(
          Layout.getFieldOffset(StartIndex) + Context.getCharWidth() - 1);
      llvm::ConstantInt *OffsetSizePtr =
          llvm::ConstantInt::get(CGF.SizeTy, PoisonStart.getQuantity());

      llvm::Value *OffsetPtr =
          CGF.Builder.CreateGEP(CGF.Int8Ty, CGF.LoadCXXThis(), OffsetSizePtr);

      CharUnits PoisonEnd;
      if (EndIndex >= Layout.getFieldCount()) {
        PoisonEnd = Layout.getNonVirtualSize();
      } else {
        PoisonEnd =
            Context.toCharUnitsFromBits(Layout.getFieldOffset(EndIndex));
      }
      CharUnits PoisonSize = PoisonEnd - PoisonStart;
      if (!PoisonSize.isPositive())
        return;

      // Use the top field declaration location as inline DebugLocation.
      DeclAsInlineDebugLocation InlineHere(
          CGF, **std::next(Dtor->getParent()->field_begin(), StartIndex));
      EmitSanitizerDtorFieldsCallback(CGF, OffsetPtr, PoisonSize.getQuantity());

      // Prevent the current stack frame from disappearing from the stack trace.
      CGF.CurFn->addFnAttr("disable-tail-calls", "true");
    }
  };

 class SanitizeDtorVTable final : public EHScopeStack::Cleanup {
    const CXXDestructorDecl *Dtor;

  public:
    SanitizeDtorVTable(const CXXDestructorDecl *Dtor) : Dtor(Dtor) {}

    // Generate function call for handling vtable pointer poisoning.
    void Emit(CodeGenFunction &CGF, Flags flags) override {
      assert(Dtor->getParent()->isDynamicClass());
      (void)Dtor;
      // Poison vtable and vtable ptr if they exist for this class.
      llvm::Value *VTablePtr = CGF.LoadCXXThis();

      // Pass in void pointer and size of region as arguments to runtime
      // function
      EmitSanitizerDtorCallback(CGF, "__sanitizer_dtor_callback_vptr",
                                VTablePtr);
    }
 };

 class SanitizeDtorCleanupBuilder {
   ASTContext &Context;
   EHScopeStack &EHStack;
   const CXXDestructorDecl *DD;
   std::optional<unsigned> StartIndex;

 public:
   SanitizeDtorCleanupBuilder(ASTContext &Context, EHScopeStack &EHStack,
                              const CXXDestructorDecl *DD)
       : Context(Context), EHStack(EHStack), DD(DD), StartIndex(std::nullopt) {}
   void PushCleanupForField(const FieldDecl *Field) {
     if (isEmptyFieldForLayout(Context, Field))
       return;
     unsigned FieldIndex = Field->getFieldIndex();
     if (FieldHasTrivialDestructorBody(Context, Field)) {
       if (!StartIndex)
         StartIndex = FieldIndex;
     } else if (StartIndex) {
       EHStack.pushCleanup<SanitizeDtorFieldRange>(NormalAndEHCleanup, DD,
                                                   *StartIndex, FieldIndex);
       StartIndex = std::nullopt;
     }
   }
   void End() {
     if (StartIndex)
       EHStack.pushCleanup<SanitizeDtorFieldRange>(NormalAndEHCleanup, DD,
                                                   *StartIndex, -1);
   }
 };
} // end anonymous namespace

/// Emit all code that comes at the end of class's
/// destructor. This is to call destructors on members and base classes
/// in reverse order of their construction.
///
/// For a deleting destructor, this also handles the case where a destroying
/// operator delete completely overrides the definition.
void CodeGenFunction::EnterDtorCleanups(const CXXDestructorDecl *DD,
                                        CXXDtorType DtorType) {
  assert((!DD->isTrivial() || DD->hasAttr<DLLExportAttr>()) &&
         "Should not emit dtor epilogue for non-exported trivial dtor!");

  // The deleting-destructor phase just needs to call the appropriate
  // operator delete that Sema picked up.
  if (DtorType == Dtor_Deleting) {
    assert(DD->getOperatorDelete() &&
           "operator delete missing - EnterDtorCleanups");
    if (CXXStructorImplicitParamValue) {
      // If there is an implicit param to the deleting dtor, it's a boolean
      // telling whether this is a deleting destructor.
      if (DD->getOperatorDelete()->isDestroyingOperatorDelete())
        EmitConditionalDtorDeleteCall(*this, CXXStructorImplicitParamValue,
                                      /*ReturnAfterDelete*/true);
      else
        EHStack.pushCleanup<CallDtorDeleteConditional>(
            NormalAndEHCleanup, CXXStructorImplicitParamValue);
    } else {
      if (DD->getOperatorDelete()->isDestroyingOperatorDelete()) {
        const CXXRecordDecl *ClassDecl = DD->getParent();
        EmitDeleteCall(DD->getOperatorDelete(),
                       LoadThisForDtorDelete(*this, DD),
                       getContext().getTagDeclType(ClassDecl));
        EmitBranchThroughCleanup(ReturnBlock);
      } else {
        EHStack.pushCleanup<CallDtorDelete>(NormalAndEHCleanup);
      }
    }
    return;
  }

  const CXXRecordDecl *ClassDecl = DD->getParent();

  // Unions have no bases and do not call field destructors.
  if (ClassDecl->isUnion())
    return;

  // The complete-destructor phase just destructs all the virtual bases.
  if (DtorType == Dtor_Complete) {
    // Poison the vtable pointer such that access after the base
    // and member destructors are invoked is invalid.
    if (CGM.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
        SanOpts.has(SanitizerKind::Memory) && ClassDecl->getNumVBases() &&
        ClassDecl->isPolymorphic())
      EHStack.pushCleanup<SanitizeDtorVTable>(NormalAndEHCleanup, DD);

    // We push them in the forward order so that they'll be popped in
    // the reverse order.
    for (const auto &Base : ClassDecl->vbases()) {
      auto *BaseClassDecl =
          cast<CXXRecordDecl>(Base.getType()->castAs<RecordType>()->getDecl());

      if (BaseClassDecl->hasTrivialDestructor()) {
        // Under SanitizeMemoryUseAfterDtor, poison the trivial base class
        // memory. For non-trival base classes the same is done in the class
        // destructor.
        if (CGM.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
            SanOpts.has(SanitizerKind::Memory) && !BaseClassDecl->isEmpty())
          EHStack.pushCleanup<SanitizeDtorTrivialBase>(NormalAndEHCleanup,
                                                       BaseClassDecl,
                                                       /*BaseIsVirtual*/ true);
      } else {
        EHStack.pushCleanup<CallBaseDtor>(NormalAndEHCleanup, BaseClassDecl,
                                          /*BaseIsVirtual*/ true);
      }
    }

    return;
  }

  assert(DtorType == Dtor_Base);
  // Poison the vtable pointer if it has no virtual bases, but inherits
  // virtual functions.
  if (CGM.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
      SanOpts.has(SanitizerKind::Memory) && !ClassDecl->getNumVBases() &&
      ClassDecl->isPolymorphic())
    EHStack.pushCleanup<SanitizeDtorVTable>(NormalAndEHCleanup, DD);

  // Destroy non-virtual bases.
  for (const auto &Base : ClassDecl->bases()) {
    // Ignore virtual bases.
    if (Base.isVirtual())
      continue;

    CXXRecordDecl *BaseClassDecl = Base.getType()->getAsCXXRecordDecl();

    if (BaseClassDecl->hasTrivialDestructor()) {
      if (CGM.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
          SanOpts.has(SanitizerKind::Memory) && !BaseClassDecl->isEmpty())
        EHStack.pushCleanup<SanitizeDtorTrivialBase>(NormalAndEHCleanup,
                                                     BaseClassDecl,
                                                     /*BaseIsVirtual*/ false);
    } else {
      EHStack.pushCleanup<CallBaseDtor>(NormalAndEHCleanup, BaseClassDecl,
                                        /*BaseIsVirtual*/ false);
    }
  }

  // Poison fields such that access after their destructors are
  // invoked, and before the base class destructor runs, is invalid.
  bool SanitizeFields = CGM.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
                        SanOpts.has(SanitizerKind::Memory);
  SanitizeDtorCleanupBuilder SanitizeBuilder(getContext(), EHStack, DD);

  // Destroy direct fields.
  for (const auto *Field : ClassDecl->fields()) {
    if (SanitizeFields)
      SanitizeBuilder.PushCleanupForField(Field);

    QualType type = Field->getType();
    QualType::DestructionKind dtorKind = type.isDestructedType();
    if (!dtorKind)
      continue;

    // Anonymous union members do not have their destructors called.
    const RecordType *RT = type->getAsUnionType();
    if (RT && RT->getDecl()->isAnonymousStructOrUnion())
      continue;

    CleanupKind cleanupKind = getCleanupKind(dtorKind);
    EHStack.pushCleanup<DestroyField>(
        cleanupKind, Field, getDestroyer(dtorKind), cleanupKind & EHCleanup);
  }

  if (SanitizeFields)
    SanitizeBuilder.End();
}

/// EmitCXXAggrConstructorCall - Emit a loop to call a particular
/// constructor for each of several members of an array.
///
/// \param ctor the constructor to call for each element
/// \param arrayType the type of the array to initialize
/// \param arrayBegin an arrayType*
/// \param zeroInitialize true if each element should be
///   zero-initialized before it is constructed
void CodeGenFunction::EmitCXXAggrConstructorCall(
    const CXXConstructorDecl *ctor, const ArrayType *arrayType,
    Address arrayBegin, const CXXConstructExpr *E, bool NewPointerIsChecked,
    bool zeroInitialize) {
  QualType elementType;
  llvm::Value *numElements =
    emitArrayLength(arrayType, elementType, arrayBegin);

  EmitCXXAggrConstructorCall(ctor, numElements, arrayBegin, E,
                             NewPointerIsChecked, zeroInitialize);
}

/// EmitCXXAggrConstructorCall - Emit a loop to call a particular
/// constructor for each of several members of an array.
///
/// \param ctor the constructor to call for each element
/// \param numElements the number of elements in the array;
///   may be zero
/// \param arrayBase a T*, where T is the type constructed by ctor
/// \param zeroInitialize true if each element should be
///   zero-initialized before it is constructed
void CodeGenFunction::EmitCXXAggrConstructorCall(const CXXConstructorDecl *ctor,
                                                 llvm::Value *numElements,
                                                 Address arrayBase,
                                                 const CXXConstructExpr *E,
                                                 bool NewPointerIsChecked,
                                                 bool zeroInitialize) {
  // It's legal for numElements to be zero.  This can happen both
  // dynamically, because x can be zero in 'new A[x]', and statically,
  // because of GCC extensions that permit zero-length arrays.  There
  // are probably legitimate places where we could assume that this
  // doesn't happen, but it's not clear that it's worth it.
  llvm::BranchInst *zeroCheckBranch = nullptr;

  // Optimize for a constant count.
  llvm::ConstantInt *constantCount
    = dyn_cast<llvm::ConstantInt>(numElements);
  if (constantCount) {
    // Just skip out if the constant count is zero.
    if (constantCount->isZero()) return;

  // Otherwise, emit the check.
  } else {
    llvm::BasicBlock *loopBB = createBasicBlock("new.ctorloop");
    llvm::Value *iszero = Builder.CreateIsNull(numElements, "isempty");
    zeroCheckBranch = Builder.CreateCondBr(iszero, loopBB, loopBB);
    EmitBlock(loopBB);
  }

  // Find the end of the array.
  llvm::Type *elementType = arrayBase.getElementType();
  llvm::Value *arrayBegin = arrayBase.emitRawPointer(*this);
  llvm::Value *arrayEnd = Builder.CreateInBoundsGEP(
      elementType, arrayBegin, numElements, "arrayctor.end");

  // Enter the loop, setting up a phi for the current location to initialize.
  llvm::BasicBlock *entryBB = Builder.GetInsertBlock();
  llvm::BasicBlock *loopBB = createBasicBlock("arrayctor.loop");
  EmitBlock(loopBB);
  llvm::PHINode *cur = Builder.CreatePHI(arrayBegin->getType(), 2,
                                         "arrayctor.cur");
  cur->addIncoming(arrayBegin, entryBB);

  // Inside the loop body, emit the constructor call on the array element.

  // The alignment of the base, adjusted by the size of a single element,
  // provides a conservative estimate of the alignment of every element.
  // (This assumes we never start tracking offsetted alignments.)
  //
  // Note that these are complete objects and so we don't need to
  // use the non-virtual size or alignment.
  QualType type = getContext().getTypeDeclType(ctor->getParent());
  CharUnits eltAlignment =
    arrayBase.getAlignment()
             .alignmentOfArrayElement(getContext().getTypeSizeInChars(type));
  Address curAddr = Address(cur, elementType, eltAlignment);

  // Zero initialize the storage, if requested.
  if (zeroInitialize)
    EmitNullInitialization(curAddr, type);

  // C++ [class.temporary]p4:
  // There are two contexts in which temporaries are destroyed at a different
  // point than the end of the full-expression. The first context is when a
  // default constructor is called to initialize an element of an array.
  // If the constructor has one or more default arguments, the destruction of
  // every temporary created in a default argument expression is sequenced
  // before the construction of the next array element, if any.

  {
    RunCleanupsScope Scope(*this);

    // Evaluate the constructor and its arguments in a regular
    // partial-destroy cleanup.
    if (getLangOpts().Exceptions &&
        !ctor->getParent()->hasTrivialDestructor()) {
      Destroyer *destroyer = destroyCXXObject;
      pushRegularPartialArrayCleanup(arrayBegin, cur, type, eltAlignment,
                                     *destroyer);
    }
    auto currAVS = AggValueSlot::forAddr(
        curAddr, type.getQualifiers(), AggValueSlot::IsDestructed,
        AggValueSlot::DoesNotNeedGCBarriers, AggValueSlot::IsNotAliased,
        AggValueSlot::DoesNotOverlap, AggValueSlot::IsNotZeroed,
        NewPointerIsChecked ? AggValueSlot::IsSanitizerChecked
                            : AggValueSlot::IsNotSanitizerChecked);
    EmitCXXConstructorCall(ctor, Ctor_Complete, /*ForVirtualBase=*/false,
                           /*Delegating=*/false, currAVS, E);
  }

  // Go to the next element.
  llvm::Value *next = Builder.CreateInBoundsGEP(
      elementType, cur, llvm::ConstantInt::get(SizeTy, 1), "arrayctor.next");
  cur->addIncoming(next, Builder.GetInsertBlock());

  // Check whether that's the end of the loop.
  llvm::Value *done = Builder.CreateICmpEQ(next, arrayEnd, "arrayctor.done");
  llvm::BasicBlock *contBB = createBasicBlock("arrayctor.cont");
  Builder.CreateCondBr(done, contBB, loopBB);

  // Patch the earlier check to skip over the loop.
  if (zeroCheckBranch) zeroCheckBranch->setSuccessor(0, contBB);

  EmitBlock(contBB);
}

void CodeGenFunction::destroyCXXObject(CodeGenFunction &CGF,
                                       Address addr,
                                       QualType type) {
  const RecordType *rtype = type->castAs<RecordType>();
  const CXXRecordDecl *record = cast<CXXRecordDecl>(rtype->getDecl());
  const CXXDestructorDecl *dtor = record->getDestructor();
  assert(!dtor->isTrivial());
  CGF.EmitCXXDestructorCall(dtor, Dtor_Complete, /*for vbase*/ false,
                            /*Delegating=*/false, addr, type);
}

void CodeGenFunction::EmitCXXConstructorCall(const CXXConstructorDecl *D,
                                             CXXCtorType Type,
                                             bool ForVirtualBase,
                                             bool Delegating,
                                             AggValueSlot ThisAVS,
                                             const CXXConstructExpr *E) {
  CallArgList Args;
  Address This = ThisAVS.getAddress();
  LangAS SlotAS = ThisAVS.getQualifiers().getAddressSpace();
  LangAS ThisAS = D->getFunctionObjectParameterType().getAddressSpace();
  llvm::Value *ThisPtr =
      getAsNaturalPointerTo(This, D->getThisType()->getPointeeType());

  if (SlotAS != ThisAS) {
    unsigned TargetThisAS = getContext().getTargetAddressSpace(ThisAS);
    llvm::Type *NewType =
        llvm::PointerType::get(getLLVMContext(), TargetThisAS);
    ThisPtr = getTargetHooks().performAddrSpaceCast(*this, ThisPtr, ThisAS,
                                                    SlotAS, NewType);
  }

  // Push the this ptr.
  Args.add(RValue::get(ThisPtr), D->getThisType());

  // If this is a trivial constructor, emit a memcpy now before we lose
  // the alignment information on the argument.
  // FIXME: It would be better to preserve alignment information into CallArg.
  if (isMemcpyEquivalentSpecialMember(D)) {
    assert(E->getNumArgs() == 1 && "unexpected argcount for trivial ctor");

    const Expr *Arg = E->getArg(0);
    LValue Src = EmitLValue(Arg);
    QualType DestTy = getContext().getTypeDeclType(D->getParent());
    LValue Dest = MakeAddrLValue(This, DestTy);
    EmitAggregateCopyCtor(Dest, Src, ThisAVS.mayOverlap());
    return;
  }

  // Add the rest of the user-supplied arguments.
  const FunctionProtoType *FPT = D->getType()->castAs<FunctionProtoType>();
  EvaluationOrder Order = E->isListInitialization()
                              ? EvaluationOrder::ForceLeftToRight
                              : EvaluationOrder::Default;
  EmitCallArgs(Args, FPT, E->arguments(), E->getConstructor(),
               /*ParamsToSkip*/ 0, Order);

  EmitCXXConstructorCall(D, Type, ForVirtualBase, Delegating, This, Args,
                         ThisAVS.mayOverlap(), E->getExprLoc(),
                         ThisAVS.isSanitizerChecked());
}

static bool canEmitDelegateCallArgs(CodeGenFunction &CGF,
                                    const CXXConstructorDecl *Ctor,
                                    CXXCtorType Type, CallArgList &Args) {
  // We can't forward a variadic call.
  if (Ctor->isVariadic())
    return false;

  if (CGF.getTarget().getCXXABI().areArgsDestroyedLeftToRightInCallee()) {
    // If the parameters are callee-cleanup, it's not safe to forward.
    for (auto *P : Ctor->parameters())
      if (P->needsDestruction(CGF.getContext()))
        return false;

    // Likewise if they're inalloca.
    const CGFunctionInfo &Info =
        CGF.CGM.getTypes().arrangeCXXConstructorCall(Args, Ctor, Type, 0, 0);
    if (Info.usesInAlloca())
      return false;
  }

  // Anything else should be OK.
  return true;
}

void CodeGenFunction::EmitCXXConstructorCall(const CXXConstructorDecl *D,
                                             CXXCtorType Type,
                                             bool ForVirtualBase,
                                             bool Delegating,
                                             Address This,
                                             CallArgList &Args,
                                             AggValueSlot::Overlap_t Overlap,
                                             SourceLocation Loc,
                                             bool NewPointerIsChecked) {
  const CXXRecordDecl *ClassDecl = D->getParent();

  if (!NewPointerIsChecked)
    EmitTypeCheck(CodeGenFunction::TCK_ConstructorCall, Loc, This,
                  getContext().getRecordType(ClassDecl), CharUnits::Zero());

  if (D->isTrivial() && D->isDefaultConstructor()) {
    assert(Args.size() == 1 && "trivial default ctor with args");
    return;
  }

  // If this is a trivial constructor, just emit what's needed. If this is a
  // union copy constructor, we must emit a memcpy, because the AST does not
  // model that copy.
  if (isMemcpyEquivalentSpecialMember(D)) {
    assert(Args.size() == 2 && "unexpected argcount for trivial ctor");
    QualType SrcTy = D->getParamDecl(0)->getType().getNonReferenceType();
    Address Src = makeNaturalAddressForPointer(
        Args[1].getRValue(*this).getScalarVal(), SrcTy);
    LValue SrcLVal = MakeAddrLValue(Src, SrcTy);
    QualType DestTy = getContext().getTypeDeclType(ClassDecl);
    LValue DestLVal = MakeAddrLValue(This, DestTy);
    EmitAggregateCopyCtor(DestLVal, SrcLVal, Overlap);
    return;
  }

  bool PassPrototypeArgs = true;
  // Check whether we can actually emit the constructor before trying to do so.
  if (auto Inherited = D->getInheritedConstructor()) {
    PassPrototypeArgs = getTypes().inheritingCtorHasParams(Inherited, Type);
    if (PassPrototypeArgs && !canEmitDelegateCallArgs(*this, D, Type, Args)) {
      EmitInlinedInheritingCXXConstructorCall(D, Type, ForVirtualBase,
                                              Delegating, Args);
      return;
    }
  }

  // Insert any ABI-specific implicit constructor arguments.
  CGCXXABI::AddedStructorArgCounts ExtraArgs =
      CGM.getCXXABI().addImplicitConstructorArgs(*this, D, Type, ForVirtualBase,
                                                 Delegating, Args);

  // Emit the call.
  llvm::Constant *CalleePtr = CGM.getAddrOfCXXStructor(GlobalDecl(D, Type));
  const CGFunctionInfo &Info = CGM.getTypes().arrangeCXXConstructorCall(
      Args, D, Type, ExtraArgs.Prefix, ExtraArgs.Suffix, PassPrototypeArgs);
  CGCallee Callee = CGCallee::forDirect(CalleePtr, GlobalDecl(D, Type));
  EmitCall(Info, Callee, ReturnValueSlot(), Args, nullptr, false, Loc);

  // Generate vtable assumptions if we're constructing a complete object
  // with a vtable.  We don't do this for base subobjects for two reasons:
  // first, it's incorrect for classes with virtual bases, and second, we're
  // about to overwrite the vptrs anyway.
  // We also have to make sure if we can refer to vtable:
  // - Otherwise we can refer to vtable if it's safe to speculatively emit.
  // FIXME: If vtable is used by ctor/dtor, or if vtable is external and we are
  // sure that definition of vtable is not hidden,
  // then we are always safe to refer to it.
  // FIXME: It looks like InstCombine is very inefficient on dealing with
  // assumes. Make assumption loads require -fstrict-vtable-pointers temporarily.
  if (CGM.getCodeGenOpts().OptimizationLevel > 0 &&
      ClassDecl->isDynamicClass() && Type != Ctor_Base &&
      CGM.getCXXABI().canSpeculativelyEmitVTable(ClassDecl) &&
      CGM.getCodeGenOpts().StrictVTablePointers)
    EmitVTableAssumptionLoads(ClassDecl, This);
}

void CodeGenFunction::EmitInheritedCXXConstructorCall(
    const CXXConstructorDecl *D, bool ForVirtualBase, Address This,
    bool InheritedFromVBase, const CXXInheritedCtorInitExpr *E) {
  CallArgList Args;
  CallArg ThisArg(RValue::get(getAsNaturalPointerTo(
                      This, D->getThisType()->getPointeeType())),
                  D->getThisType());

  // Forward the parameters.
  if (InheritedFromVBase &&
      CGM.getTarget().getCXXABI().hasConstructorVariants()) {
    // Nothing to do; this construction is not responsible for constructing
    // the base class containing the inherited constructor.
    // FIXME: Can we just pass undef's for the remaining arguments if we don't
    // have constructor variants?
    Args.push_back(ThisArg);
  } else if (!CXXInheritedCtorInitExprArgs.empty()) {
    // The inheriting constructor was inlined; just inject its arguments.
    assert(CXXInheritedCtorInitExprArgs.size() >= D->getNumParams() &&
           "wrong number of parameters for inherited constructor call");
    Args = CXXInheritedCtorInitExprArgs;
    Args[0] = ThisArg;
  } else {
    // The inheriting constructor was not inlined. Emit delegating arguments.
    Args.push_back(ThisArg);
    const auto *OuterCtor = cast<CXXConstructorDecl>(CurCodeDecl);
    assert(OuterCtor->getNumParams() == D->getNumParams());
    assert(!OuterCtor->isVariadic() && "should have been inlined");

    for (const auto *Param : OuterCtor->parameters()) {
      assert(getContext().hasSameUnqualifiedType(
          OuterCtor->getParamDecl(Param->getFunctionScopeIndex())->getType(),
          Param->getType()));
      EmitDelegateCallArg(Args, Param, E->getLocation());

      // Forward __attribute__(pass_object_size).
      if (Param->hasAttr<PassObjectSizeAttr>()) {
        auto *POSParam = SizeArguments[Param];
        assert(POSParam && "missing pass_object_size value for forwarding");
        EmitDelegateCallArg(Args, POSParam, E->getLocation());
      }
    }
  }

  EmitCXXConstructorCall(D, Ctor_Base, ForVirtualBase, /*Delegating*/false,
                         This, Args, AggValueSlot::MayOverlap,
                         E->getLocation(), /*NewPointerIsChecked*/true);
}

void CodeGenFunction::EmitInlinedInheritingCXXConstructorCall(
    const CXXConstructorDecl *Ctor, CXXCtorType CtorType, bool ForVirtualBase,
    bool Delegating, CallArgList &Args) {
  GlobalDecl GD(Ctor, CtorType);
  InlinedInheritingConstructorScope Scope(*this, GD);
  ApplyInlineDebugLocation DebugScope(*this, GD);
  RunCleanupsScope RunCleanups(*this);

  // Save the arguments to be passed to the inherited constructor.
  CXXInheritedCtorInitExprArgs = Args;

  FunctionArgList Params;
  QualType RetType = BuildFunctionArgList(CurGD, Params);
  FnRetTy = RetType;

  // Insert any ABI-specific implicit constructor arguments.
  CGM.getCXXABI().addImplicitConstructorArgs(*this, Ctor, CtorType,
                                             ForVirtualBase, Delegating, Args);

  // Emit a simplified prolog. We only need to emit the implicit params.
  assert(Args.size() >= Params.size() && "too few arguments for call");
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (I < Params.size() && isa<ImplicitParamDecl>(Params[I])) {
      const RValue &RV = Args[I].getRValue(*this);
      assert(!RV.isComplex() && "complex indirect params not supported");
      ParamValue Val = RV.isScalar()
                           ? ParamValue::forDirect(RV.getScalarVal())
                           : ParamValue::forIndirect(RV.getAggregateAddress());
      EmitParmDecl(*Params[I], Val, I + 1);
    }
  }

  // Create a return value slot if the ABI implementation wants one.
  // FIXME: This is dumb, we should ask the ABI not to try to set the return
  // value instead.
  if (!RetType->isVoidType())
    ReturnValue = CreateIRTemp(RetType, "retval.inhctor");

  CGM.getCXXABI().EmitInstanceFunctionProlog(*this);
  CXXThisValue = CXXABIThisValue;

  // Directly emit the constructor initializers.
  EmitCtorPrologue(Ctor, CtorType, Params);
}

void CodeGenFunction::EmitVTableAssumptionLoad(const VPtr &Vptr, Address This) {
  llvm::Value *VTableGlobal =
      CGM.getCXXABI().getVTableAddressPoint(Vptr.Base, Vptr.VTableClass);
  if (!VTableGlobal)
    return;

  // We can just use the base offset in the complete class.
  CharUnits NonVirtualOffset = Vptr.Base.getBaseOffset();

  if (!NonVirtualOffset.isZero())
    This =
        ApplyNonVirtualAndVirtualOffset(*this, This, NonVirtualOffset, nullptr,
                                        Vptr.VTableClass, Vptr.NearestVBase);

  llvm::Value *VPtrValue =
      GetVTablePtr(This, VTableGlobal->getType(), Vptr.VTableClass);
  llvm::Value *Cmp =
      Builder.CreateICmpEQ(VPtrValue, VTableGlobal, "cmp.vtables");
  Builder.CreateAssumption(Cmp);
}

void CodeGenFunction::EmitVTableAssumptionLoads(const CXXRecordDecl *ClassDecl,
                                                Address This) {
  if (CGM.getCXXABI().doStructorsInitializeVPtrs(ClassDecl))
    for (const VPtr &Vptr : getVTablePointers(ClassDecl))
      EmitVTableAssumptionLoad(Vptr, This);
}

void
CodeGenFunction::EmitSynthesizedCXXCopyCtorCall(const CXXConstructorDecl *D,
                                                Address This, Address Src,
                                                const CXXConstructExpr *E) {
  const FunctionProtoType *FPT = D->getType()->castAs<FunctionProtoType>();

  CallArgList Args;

  // Push the this ptr.
  Args.add(RValue::get(getAsNaturalPointerTo(This, D->getThisType())),
           D->getThisType());

  // Push the src ptr.
  QualType QT = *(FPT->param_type_begin());
  llvm::Type *t = CGM.getTypes().ConvertType(QT);
  llvm::Value *Val = getAsNaturalPointerTo(Src, D->getThisType());
  llvm::Value *SrcVal = Builder.CreateBitCast(Val, t);
  Args.add(RValue::get(SrcVal), QT);

  // Skip over first argument (Src).
  EmitCallArgs(Args, FPT, drop_begin(E->arguments(), 1), E->getConstructor(),
               /*ParamsToSkip*/ 1);

  EmitCXXConstructorCall(D, Ctor_Complete, /*ForVirtualBase*/false,
                         /*Delegating*/false, This, Args,
                         AggValueSlot::MayOverlap, E->getExprLoc(),
                         /*NewPointerIsChecked*/false);
}

void
CodeGenFunction::EmitDelegateCXXConstructorCall(const CXXConstructorDecl *Ctor,
                                                CXXCtorType CtorType,
                                                const FunctionArgList &Args,
                                                SourceLocation Loc) {
  CallArgList DelegateArgs;

  FunctionArgList::const_iterator I = Args.begin(), E = Args.end();
  assert(I != E && "no parameters to constructor");

  // this
  Address This = LoadCXXThisAddress();
  DelegateArgs.add(RValue::get(getAsNaturalPointerTo(
                       This, (*I)->getType()->getPointeeType())),
                   (*I)->getType());
  ++I;

  // FIXME: The location of the VTT parameter in the parameter list is
  // specific to the Itanium ABI and shouldn't be hardcoded here.
  if (CGM.getCXXABI().NeedsVTTParameter(CurGD)) {
    assert(I != E && "cannot skip vtt parameter, already done with args");
    assert((*I)->getType()->isPointerType() &&
           "skipping parameter not of vtt type");
    ++I;
  }

  // Explicit arguments.
  for (; I != E; ++I) {
    const VarDecl *param = *I;
    // FIXME: per-argument source location
    EmitDelegateCallArg(DelegateArgs, param, Loc);
  }

  EmitCXXConstructorCall(Ctor, CtorType, /*ForVirtualBase=*/false,
                         /*Delegating=*/true, This, DelegateArgs,
                         AggValueSlot::MayOverlap, Loc,
                         /*NewPointerIsChecked=*/true);
}

namespace {
  struct CallDelegatingCtorDtor final : EHScopeStack::Cleanup {
    const CXXDestructorDecl *Dtor;
    Address Addr;
    CXXDtorType Type;

    CallDelegatingCtorDtor(const CXXDestructorDecl *D, Address Addr,
                           CXXDtorType Type)
      : Dtor(D), Addr(Addr), Type(Type) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      // We are calling the destructor from within the constructor.
      // Therefore, "this" should have the expected type.
      QualType ThisTy = Dtor->getFunctionObjectParameterType();
      CGF.EmitCXXDestructorCall(Dtor, Type, /*ForVirtualBase=*/false,
                                /*Delegating=*/true, Addr, ThisTy);
    }
  };
} // end anonymous namespace

void
CodeGenFunction::EmitDelegatingCXXConstructorCall(const CXXConstructorDecl *Ctor,
                                                  const FunctionArgList &Args) {
  assert(Ctor->isDelegatingConstructor());

  Address ThisPtr = LoadCXXThisAddress();

  AggValueSlot AggSlot =
    AggValueSlot::forAddr(ThisPtr, Qualifiers(),
                          AggValueSlot::IsDestructed,
                          AggValueSlot::DoesNotNeedGCBarriers,
                          AggValueSlot::IsNotAliased,
                          AggValueSlot::MayOverlap,
                          AggValueSlot::IsNotZeroed,
                          // Checks are made by the code that calls constructor.
                          AggValueSlot::IsSanitizerChecked);

  EmitAggExpr(Ctor->init_begin()[0]->getInit(), AggSlot);

  const CXXRecordDecl *ClassDecl = Ctor->getParent();
  if (CGM.getLangOpts().Exceptions && !ClassDecl->hasTrivialDestructor()) {
    CXXDtorType Type =
      CurGD.getCtorType() == Ctor_Complete ? Dtor_Complete : Dtor_Base;

    EHStack.pushCleanup<CallDelegatingCtorDtor>(EHCleanup,
                                                ClassDecl->getDestructor(),
                                                ThisPtr, Type);
  }
}

void CodeGenFunction::EmitCXXDestructorCall(const CXXDestructorDecl *DD,
                                            CXXDtorType Type,
                                            bool ForVirtualBase,
                                            bool Delegating, Address This,
                                            QualType ThisTy) {
  CGM.getCXXABI().EmitDestructorCall(*this, DD, Type, ForVirtualBase,
                                     Delegating, This, ThisTy);
}

namespace {
  struct CallLocalDtor final : EHScopeStack::Cleanup {
    const CXXDestructorDecl *Dtor;
    Address Addr;
    QualType Ty;

    CallLocalDtor(const CXXDestructorDecl *D, Address Addr, QualType Ty)
        : Dtor(D), Addr(Addr), Ty(Ty) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.EmitCXXDestructorCall(Dtor, Dtor_Complete,
                                /*ForVirtualBase=*/false,
                                /*Delegating=*/false, Addr, Ty);
    }
  };
} // end anonymous namespace

void CodeGenFunction::PushDestructorCleanup(const CXXDestructorDecl *D,
                                            QualType T, Address Addr) {
  EHStack.pushCleanup<CallLocalDtor>(NormalAndEHCleanup, D, Addr, T);
}

void CodeGenFunction::PushDestructorCleanup(QualType T, Address Addr) {
  CXXRecordDecl *ClassDecl = T->getAsCXXRecordDecl();
  if (!ClassDecl) return;
  if (ClassDecl->hasTrivialDestructor()) return;

  const CXXDestructorDecl *D = ClassDecl->getDestructor();
  assert(D && D->isUsed() && "destructor not marked as used!");
  PushDestructorCleanup(D, T, Addr);
}

void CodeGenFunction::InitializeVTablePointer(const VPtr &Vptr) {
  // Compute the address point.
  llvm::Value *VTableAddressPoint =
      CGM.getCXXABI().getVTableAddressPointInStructor(
          *this, Vptr.VTableClass, Vptr.Base, Vptr.NearestVBase);

  if (!VTableAddressPoint)
    return;

  // Compute where to store the address point.
  llvm::Value *VirtualOffset = nullptr;
  CharUnits NonVirtualOffset = CharUnits::Zero();

  if (CGM.getCXXABI().isVirtualOffsetNeededForVTableField(*this, Vptr)) {
    // We need to use the virtual base offset offset because the virtual base
    // might have a different offset in the most derived class.

    VirtualOffset = CGM.getCXXABI().GetVirtualBaseClassOffset(
        *this, LoadCXXThisAddress(), Vptr.VTableClass, Vptr.NearestVBase);
    NonVirtualOffset = Vptr.OffsetFromNearestVBase;
  } else {
    // We can just use the base offset in the complete class.
    NonVirtualOffset = Vptr.Base.getBaseOffset();
  }

  // Apply the offsets.
  Address VTableField = LoadCXXThisAddress();
  if (!NonVirtualOffset.isZero() || VirtualOffset)
    VTableField = ApplyNonVirtualAndVirtualOffset(
        *this, VTableField, NonVirtualOffset, VirtualOffset, Vptr.VTableClass,
        Vptr.NearestVBase);

  // Finally, store the address point. Use the same LLVM types as the field to
  // support optimization.
  unsigned GlobalsAS = CGM.getDataLayout().getDefaultGlobalsAddressSpace();
  llvm::Type *PtrTy = llvm::PointerType::get(CGM.getLLVMContext(), GlobalsAS);
  // vtable field is derived from `this` pointer, therefore they should be in
  // the same addr space. Note that this might not be LLVM address space 0.
  VTableField = VTableField.withElementType(PtrTy);

  if (auto AuthenticationInfo = CGM.getVTablePointerAuthInfo(
          this, Vptr.Base.getBase(), VTableField.emitRawPointer(*this)))
    VTableAddressPoint =
        EmitPointerAuthSign(*AuthenticationInfo, VTableAddressPoint);

  llvm::StoreInst *Store = Builder.CreateStore(VTableAddressPoint, VTableField);
  TBAAAccessInfo TBAAInfo = CGM.getTBAAVTablePtrAccessInfo(PtrTy);
  CGM.DecorateInstructionWithTBAA(Store, TBAAInfo);
  if (CGM.getCodeGenOpts().OptimizationLevel > 0 &&
      CGM.getCodeGenOpts().StrictVTablePointers)
    CGM.DecorateInstructionWithInvariantGroup(Store, Vptr.VTableClass);
}

CodeGenFunction::VPtrsVector
CodeGenFunction::getVTablePointers(const CXXRecordDecl *VTableClass) {
  CodeGenFunction::VPtrsVector VPtrsResult;
  VisitedVirtualBasesSetTy VBases;
  getVTablePointers(BaseSubobject(VTableClass, CharUnits::Zero()),
                    /*NearestVBase=*/nullptr,
                    /*OffsetFromNearestVBase=*/CharUnits::Zero(),
                    /*BaseIsNonVirtualPrimaryBase=*/false, VTableClass, VBases,
                    VPtrsResult);
  return VPtrsResult;
}

void CodeGenFunction::getVTablePointers(BaseSubobject Base,
                                        const CXXRecordDecl *NearestVBase,
                                        CharUnits OffsetFromNearestVBase,
                                        bool BaseIsNonVirtualPrimaryBase,
                                        const CXXRecordDecl *VTableClass,
                                        VisitedVirtualBasesSetTy &VBases,
                                        VPtrsVector &Vptrs) {
  // If this base is a non-virtual primary base the address point has already
  // been set.
  if (!BaseIsNonVirtualPrimaryBase) {
    // Initialize the vtable pointer for this base.
    VPtr Vptr = {Base, NearestVBase, OffsetFromNearestVBase, VTableClass};
    Vptrs.push_back(Vptr);
  }

  const CXXRecordDecl *RD = Base.getBase();

  // Traverse bases.
  for (const auto &I : RD->bases()) {
    auto *BaseDecl =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

    // Ignore classes without a vtable.
    if (!BaseDecl->isDynamicClass())
      continue;

    CharUnits BaseOffset;
    CharUnits BaseOffsetFromNearestVBase;
    bool BaseDeclIsNonVirtualPrimaryBase;

    if (I.isVirtual()) {
      // Check if we've visited this virtual base before.
      if (!VBases.insert(BaseDecl).second)
        continue;

      const ASTRecordLayout &Layout =
        getContext().getASTRecordLayout(VTableClass);

      BaseOffset = Layout.getVBaseClassOffset(BaseDecl);
      BaseOffsetFromNearestVBase = CharUnits::Zero();
      BaseDeclIsNonVirtualPrimaryBase = false;
    } else {
      const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);

      BaseOffset = Base.getBaseOffset() + Layout.getBaseClassOffset(BaseDecl);
      BaseOffsetFromNearestVBase =
        OffsetFromNearestVBase + Layout.getBaseClassOffset(BaseDecl);
      BaseDeclIsNonVirtualPrimaryBase = Layout.getPrimaryBase() == BaseDecl;
    }

    getVTablePointers(
        BaseSubobject(BaseDecl, BaseOffset),
        I.isVirtual() ? BaseDecl : NearestVBase, BaseOffsetFromNearestVBase,
        BaseDeclIsNonVirtualPrimaryBase, VTableClass, VBases, Vptrs);
  }
}

void CodeGenFunction::InitializeVTablePointers(const CXXRecordDecl *RD) {
  // Ignore classes without a vtable.
  if (!RD->isDynamicClass())
    return;

  // Initialize the vtable pointers for this class and all of its bases.
  if (CGM.getCXXABI().doStructorsInitializeVPtrs(RD))
    for (const VPtr &Vptr : getVTablePointers(RD))
      InitializeVTablePointer(Vptr);

  if (RD->getNumVBases())
    CGM.getCXXABI().initializeHiddenVirtualInheritanceMembers(*this, RD);
}

llvm::Value *CodeGenFunction::GetVTablePtr(Address This,
                                           llvm::Type *VTableTy,
                                           const CXXRecordDecl *RD,
                                           VTableAuthMode AuthMode) {
  Address VTablePtrSrc = This.withElementType(VTableTy);
  llvm::Instruction *VTable = Builder.CreateLoad(VTablePtrSrc, "vtable");
  TBAAAccessInfo TBAAInfo = CGM.getTBAAVTablePtrAccessInfo(VTableTy);
  CGM.DecorateInstructionWithTBAA(VTable, TBAAInfo);

  if (auto AuthenticationInfo =
          CGM.getVTablePointerAuthInfo(this, RD, This.emitRawPointer(*this))) {
    if (AuthMode != VTableAuthMode::UnsafeUbsanStrip) {
      VTable = cast<llvm::Instruction>(
          EmitPointerAuthAuth(*AuthenticationInfo, VTable));
      if (AuthMode == VTableAuthMode::MustTrap) {
        // This is clearly suboptimal but until we have an ability
        // to rely on the authentication intrinsic trapping and force
        // an authentication to occur we don't really have a choice.
        VTable =
            cast<llvm::Instruction>(Builder.CreateBitCast(VTable, Int8PtrTy));
        Builder.CreateLoad(RawAddress(VTable, Int8Ty, CGM.getPointerAlign()),
                           /* IsVolatile */ true);
      }
    } else {
      VTable = cast<llvm::Instruction>(EmitPointerAuthAuth(
          CGPointerAuthInfo(0, PointerAuthenticationMode::Strip, false, false,
                            nullptr),
          VTable));
    }
  }

  if (CGM.getCodeGenOpts().OptimizationLevel > 0 &&
      CGM.getCodeGenOpts().StrictVTablePointers)
    CGM.DecorateInstructionWithInvariantGroup(VTable, RD);

  return VTable;
}

// If a class has a single non-virtual base and does not introduce or override
// virtual member functions or fields, it will have the same layout as its base.
// This function returns the least derived such class.
//
// Casting an instance of a base class to such a derived class is technically
// undefined behavior, but it is a relatively common hack for introducing member
// functions on class instances with specific properties (e.g. llvm::Operator)
// that works under most compilers and should not have security implications, so
// we allow it by default. It can be disabled with -fsanitize=cfi-cast-strict.
static const CXXRecordDecl *
LeastDerivedClassWithSameLayout(const CXXRecordDecl *RD) {
  if (!RD->field_empty())
    return RD;

  if (RD->getNumVBases() != 0)
    return RD;

  if (RD->getNumBases() != 1)
    return RD;

  for (const CXXMethodDecl *MD : RD->methods()) {
    if (MD->isVirtual()) {
      // Virtual member functions are only ok if they are implicit destructors
      // because the implicit destructor will have the same semantics as the
      // base class's destructor if no fields are added.
      if (isa<CXXDestructorDecl>(MD) && MD->isImplicit())
        continue;
      return RD;
    }
  }

  return LeastDerivedClassWithSameLayout(
      RD->bases_begin()->getType()->getAsCXXRecordDecl());
}

void CodeGenFunction::EmitTypeMetadataCodeForVCall(const CXXRecordDecl *RD,
                                                   llvm::Value *VTable,
                                                   SourceLocation Loc) {
  if (SanOpts.has(SanitizerKind::CFIVCall))
    EmitVTablePtrCheckForCall(RD, VTable, CodeGenFunction::CFITCK_VCall, Loc);
  else if (CGM.getCodeGenOpts().WholeProgramVTables &&
           // Don't insert type test assumes if we are forcing public
           // visibility.
           !CGM.AlwaysHasLTOVisibilityPublic(RD)) {
    QualType Ty = QualType(RD->getTypeForDecl(), 0);
    llvm::Metadata *MD = CGM.CreateMetadataIdentifierForType(Ty);
    llvm::Value *TypeId =
        llvm::MetadataAsValue::get(CGM.getLLVMContext(), MD);

    // If we already know that the call has hidden LTO visibility, emit
    // @llvm.type.test(). Otherwise emit @llvm.public.type.test(), which WPD
    // will convert to @llvm.type.test() if we assert at link time that we have
    // whole program visibility.
    llvm::Intrinsic::ID IID = CGM.HasHiddenLTOVisibility(RD)
                                  ? llvm::Intrinsic::type_test
                                  : llvm::Intrinsic::public_type_test;
    llvm::Value *TypeTest =
        Builder.CreateCall(CGM.getIntrinsic(IID), {VTable, TypeId});
    Builder.CreateCall(CGM.getIntrinsic(llvm::Intrinsic::assume), TypeTest);
  }
}

void CodeGenFunction::EmitVTablePtrCheckForCall(const CXXRecordDecl *RD,
                                                llvm::Value *VTable,
                                                CFITypeCheckKind TCK,
                                                SourceLocation Loc) {
  if (!SanOpts.has(SanitizerKind::CFICastStrict))
    RD = LeastDerivedClassWithSameLayout(RD);

  EmitVTablePtrCheck(RD, VTable, TCK, Loc);
}

void CodeGenFunction::EmitVTablePtrCheckForCast(QualType T, Address Derived,
                                                bool MayBeNull,
                                                CFITypeCheckKind TCK,
                                                SourceLocation Loc) {
  if (!getLangOpts().CPlusPlus)
    return;

  auto *ClassTy = T->getAs<RecordType>();
  if (!ClassTy)
    return;

  const CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(ClassTy->getDecl());

  if (!ClassDecl->isCompleteDefinition() || !ClassDecl->isDynamicClass())
    return;

  if (!SanOpts.has(SanitizerKind::CFICastStrict))
    ClassDecl = LeastDerivedClassWithSameLayout(ClassDecl);

  llvm::BasicBlock *ContBlock = nullptr;

  if (MayBeNull) {
    llvm::Value *DerivedNotNull =
        Builder.CreateIsNotNull(Derived.emitRawPointer(*this), "cast.nonnull");

    llvm::BasicBlock *CheckBlock = createBasicBlock("cast.check");
    ContBlock = createBasicBlock("cast.cont");

    Builder.CreateCondBr(DerivedNotNull, CheckBlock, ContBlock);

    EmitBlock(CheckBlock);
  }

  llvm::Value *VTable;
  std::tie(VTable, ClassDecl) =
      CGM.getCXXABI().LoadVTablePtr(*this, Derived, ClassDecl);

  EmitVTablePtrCheck(ClassDecl, VTable, TCK, Loc);

  if (MayBeNull) {
    Builder.CreateBr(ContBlock);
    EmitBlock(ContBlock);
  }
}

void CodeGenFunction::EmitVTablePtrCheck(const CXXRecordDecl *RD,
                                         llvm::Value *VTable,
                                         CFITypeCheckKind TCK,
                                         SourceLocation Loc) {
  if (!CGM.getCodeGenOpts().SanitizeCfiCrossDso &&
      !CGM.HasHiddenLTOVisibility(RD))
    return;

  SanitizerMask M;
  llvm::SanitizerStatKind SSK;
  switch (TCK) {
  case CFITCK_VCall:
    M = SanitizerKind::CFIVCall;
    SSK = llvm::SanStat_CFI_VCall;
    break;
  case CFITCK_NVCall:
    M = SanitizerKind::CFINVCall;
    SSK = llvm::SanStat_CFI_NVCall;
    break;
  case CFITCK_DerivedCast:
    M = SanitizerKind::CFIDerivedCast;
    SSK = llvm::SanStat_CFI_DerivedCast;
    break;
  case CFITCK_UnrelatedCast:
    M = SanitizerKind::CFIUnrelatedCast;
    SSK = llvm::SanStat_CFI_UnrelatedCast;
    break;
  case CFITCK_ICall:
  case CFITCK_NVMFCall:
  case CFITCK_VMFCall:
    llvm_unreachable("unexpected sanitizer kind");
  }

  std::string TypeName = RD->getQualifiedNameAsString();
  if (getContext().getNoSanitizeList().containsType(M, TypeName))
    return;

  SanitizerScope SanScope(this);
  EmitSanitizerStatReport(SSK);

  llvm::Metadata *MD =
      CGM.CreateMetadataIdentifierForType(QualType(RD->getTypeForDecl(), 0));
  llvm::Value *TypeId = llvm::MetadataAsValue::get(getLLVMContext(), MD);

  llvm::Value *TypeTest = Builder.CreateCall(
      CGM.getIntrinsic(llvm::Intrinsic::type_test), {VTable, TypeId});

  llvm::Constant *StaticData[] = {
      llvm::ConstantInt::get(Int8Ty, TCK),
      EmitCheckSourceLocation(Loc),
      EmitCheckTypeDescriptor(QualType(RD->getTypeForDecl(), 0)),
  };

  auto CrossDsoTypeId = CGM.CreateCrossDsoCfiTypeId(MD);
  if (CGM.getCodeGenOpts().SanitizeCfiCrossDso && CrossDsoTypeId) {
    EmitCfiSlowPathCheck(M, TypeTest, CrossDsoTypeId, VTable, StaticData);
    return;
  }

  if (CGM.getCodeGenOpts().SanitizeTrap.has(M)) {
    EmitTrapCheck(TypeTest, SanitizerHandler::CFICheckFail);
    return;
  }

  llvm::Value *AllVtables = llvm::MetadataAsValue::get(
      CGM.getLLVMContext(),
      llvm::MDString::get(CGM.getLLVMContext(), "all-vtables"));
  llvm::Value *ValidVtable = Builder.CreateCall(
      CGM.getIntrinsic(llvm::Intrinsic::type_test), {VTable, AllVtables});
  EmitCheck(std::make_pair(TypeTest, M), SanitizerHandler::CFICheckFail,
            StaticData, {VTable, ValidVtable});
}

bool CodeGenFunction::ShouldEmitVTableTypeCheckedLoad(const CXXRecordDecl *RD) {
  if (!CGM.getCodeGenOpts().WholeProgramVTables ||
      !CGM.HasHiddenLTOVisibility(RD))
    return false;

  if (CGM.getCodeGenOpts().VirtualFunctionElimination)
    return true;

  if (!SanOpts.has(SanitizerKind::CFIVCall) ||
      !CGM.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::CFIVCall))
    return false;

  std::string TypeName = RD->getQualifiedNameAsString();
  return !getContext().getNoSanitizeList().containsType(SanitizerKind::CFIVCall,
                                                        TypeName);
}

llvm::Value *CodeGenFunction::EmitVTableTypeCheckedLoad(
    const CXXRecordDecl *RD, llvm::Value *VTable, llvm::Type *VTableTy,
    uint64_t VTableByteOffset) {
  SanitizerScope SanScope(this);

  EmitSanitizerStatReport(llvm::SanStat_CFI_VCall);

  llvm::Metadata *MD =
      CGM.CreateMetadataIdentifierForType(QualType(RD->getTypeForDecl(), 0));
  llvm::Value *TypeId = llvm::MetadataAsValue::get(CGM.getLLVMContext(), MD);

  llvm::Value *CheckedLoad = Builder.CreateCall(
      CGM.getIntrinsic(llvm::Intrinsic::type_checked_load),
      {VTable, llvm::ConstantInt::get(Int32Ty, VTableByteOffset), TypeId});
  llvm::Value *CheckResult = Builder.CreateExtractValue(CheckedLoad, 1);

  std::string TypeName = RD->getQualifiedNameAsString();
  if (SanOpts.has(SanitizerKind::CFIVCall) &&
      !getContext().getNoSanitizeList().containsType(SanitizerKind::CFIVCall,
                                                     TypeName)) {
    EmitCheck(std::make_pair(CheckResult, SanitizerKind::CFIVCall),
              SanitizerHandler::CFICheckFail, {}, {});
  }

  return Builder.CreateBitCast(Builder.CreateExtractValue(CheckedLoad, 0),
                               VTableTy);
}

void CodeGenFunction::EmitForwardingCallToLambda(
    const CXXMethodDecl *callOperator, CallArgList &callArgs,
    const CGFunctionInfo *calleeFnInfo, llvm::Constant *calleePtr) {
  // Get the address of the call operator.
  if (!calleeFnInfo)
    calleeFnInfo = &CGM.getTypes().arrangeCXXMethodDeclaration(callOperator);

  if (!calleePtr)
    calleePtr =
        CGM.GetAddrOfFunction(GlobalDecl(callOperator),
                              CGM.getTypes().GetFunctionType(*calleeFnInfo));

  // Prepare the return slot.
  const FunctionProtoType *FPT =
    callOperator->getType()->castAs<FunctionProtoType>();
  QualType resultType = FPT->getReturnType();
  ReturnValueSlot returnSlot;
  if (!resultType->isVoidType() &&
      calleeFnInfo->getReturnInfo().getKind() == ABIArgInfo::Indirect &&
      !hasScalarEvaluationKind(calleeFnInfo->getReturnType()))
    returnSlot =
        ReturnValueSlot(ReturnValue, resultType.isVolatileQualified(),
                        /*IsUnused=*/false, /*IsExternallyDestructed=*/true);

  // We don't need to separately arrange the call arguments because
  // the call can't be variadic anyway --- it's impossible to forward
  // variadic arguments.

  // Now emit our call.
  auto callee = CGCallee::forDirect(calleePtr, GlobalDecl(callOperator));
  RValue RV = EmitCall(*calleeFnInfo, callee, returnSlot, callArgs);

  // If necessary, copy the returned value into the slot.
  if (!resultType->isVoidType() && returnSlot.isNull()) {
    if (getLangOpts().ObjCAutoRefCount && resultType->isObjCRetainableType()) {
      RV = RValue::get(EmitARCRetainAutoreleasedReturnValue(RV.getScalarVal()));
    }
    EmitReturnOfRValue(RV, resultType);
  } else
    EmitBranchThroughCleanup(ReturnBlock);
}

void CodeGenFunction::EmitLambdaBlockInvokeBody() {
  const BlockDecl *BD = BlockInfo->getBlockDecl();
  const VarDecl *variable = BD->capture_begin()->getVariable();
  const CXXRecordDecl *Lambda = variable->getType()->getAsCXXRecordDecl();
  const CXXMethodDecl *CallOp = Lambda->getLambdaCallOperator();

  if (CallOp->isVariadic()) {
    // FIXME: Making this work correctly is nasty because it requires either
    // cloning the body of the call operator or making the call operator
    // forward.
    CGM.ErrorUnsupported(CurCodeDecl, "lambda conversion to variadic function");
    return;
  }

  // Start building arguments for forwarding call
  CallArgList CallArgs;

  QualType ThisType = getContext().getPointerType(getContext().getRecordType(Lambda));
  Address ThisPtr = GetAddrOfBlockDecl(variable);
  CallArgs.add(RValue::get(getAsNaturalPointerTo(ThisPtr, ThisType)), ThisType);

  // Add the rest of the parameters.
  for (auto *param : BD->parameters())
    EmitDelegateCallArg(CallArgs, param, param->getBeginLoc());

  assert(!Lambda->isGenericLambda() &&
            "generic lambda interconversion to block not implemented");
  EmitForwardingCallToLambda(CallOp, CallArgs);
}

void CodeGenFunction::EmitLambdaStaticInvokeBody(const CXXMethodDecl *MD) {
  if (MD->isVariadic()) {
    // FIXME: Making this work correctly is nasty because it requires either
    // cloning the body of the call operator or making the call operator
    // forward.
    CGM.ErrorUnsupported(MD, "lambda conversion to variadic function");
    return;
  }

  const CXXRecordDecl *Lambda = MD->getParent();

  // Start building arguments for forwarding call
  CallArgList CallArgs;

  QualType LambdaType = getContext().getRecordType(Lambda);
  QualType ThisType = getContext().getPointerType(LambdaType);
  Address ThisPtr = CreateMemTemp(LambdaType, "unused.capture");
  CallArgs.add(RValue::get(ThisPtr.emitRawPointer(*this)), ThisType);

  EmitLambdaDelegatingInvokeBody(MD, CallArgs);
}

void CodeGenFunction::EmitLambdaDelegatingInvokeBody(const CXXMethodDecl *MD,
                                                     CallArgList &CallArgs) {
  // Add the rest of the forwarded parameters.
  for (auto *Param : MD->parameters())
    EmitDelegateCallArg(CallArgs, Param, Param->getBeginLoc());

  const CXXRecordDecl *Lambda = MD->getParent();
  const CXXMethodDecl *CallOp = Lambda->getLambdaCallOperator();
  // For a generic lambda, find the corresponding call operator specialization
  // to which the call to the static-invoker shall be forwarded.
  if (Lambda->isGenericLambda()) {
    assert(MD->isFunctionTemplateSpecialization());
    const TemplateArgumentList *TAL = MD->getTemplateSpecializationArgs();
    FunctionTemplateDecl *CallOpTemplate = CallOp->getDescribedFunctionTemplate();
    void *InsertPos = nullptr;
    FunctionDecl *CorrespondingCallOpSpecialization =
        CallOpTemplate->findSpecialization(TAL->asArray(), InsertPos);
    assert(CorrespondingCallOpSpecialization);
    CallOp = cast<CXXMethodDecl>(CorrespondingCallOpSpecialization);
  }

  // Special lambda forwarding when there are inalloca parameters.
  if (hasInAllocaArg(MD)) {
    const CGFunctionInfo *ImplFnInfo = nullptr;
    llvm::Function *ImplFn = nullptr;
    EmitLambdaInAllocaImplFn(CallOp, &ImplFnInfo, &ImplFn);

    EmitForwardingCallToLambda(CallOp, CallArgs, ImplFnInfo, ImplFn);
    return;
  }

  EmitForwardingCallToLambda(CallOp, CallArgs);
}

void CodeGenFunction::EmitLambdaInAllocaCallOpBody(const CXXMethodDecl *MD) {
  if (MD->isVariadic()) {
    // FIXME: Making this work correctly is nasty because it requires either
    // cloning the body of the call operator or making the call operator forward.
    CGM.ErrorUnsupported(MD, "lambda conversion to variadic function");
    return;
  }

  // Forward %this argument.
  CallArgList CallArgs;
  QualType LambdaType = getContext().getRecordType(MD->getParent());
  QualType ThisType = getContext().getPointerType(LambdaType);
  llvm::Value *ThisArg = CurFn->getArg(0);
  CallArgs.add(RValue::get(ThisArg), ThisType);

  EmitLambdaDelegatingInvokeBody(MD, CallArgs);
}

void CodeGenFunction::EmitLambdaInAllocaImplFn(
    const CXXMethodDecl *CallOp, const CGFunctionInfo **ImplFnInfo,
    llvm::Function **ImplFn) {
  const CGFunctionInfo &FnInfo =
      CGM.getTypes().arrangeCXXMethodDeclaration(CallOp);
  llvm::Function *CallOpFn =
      cast<llvm::Function>(CGM.GetAddrOfFunction(GlobalDecl(CallOp)));

  // Emit function containing the original call op body. __invoke will delegate
  // to this function.
  SmallVector<CanQualType, 4> ArgTypes;
  for (auto I = FnInfo.arg_begin(); I != FnInfo.arg_end(); ++I)
    ArgTypes.push_back(I->type);
  *ImplFnInfo = &CGM.getTypes().arrangeLLVMFunctionInfo(
      FnInfo.getReturnType(), FnInfoOpts::IsDelegateCall, ArgTypes,
      FnInfo.getExtInfo(), {}, FnInfo.getRequiredArgs());

  // Create mangled name as if this was a method named __impl. If for some
  // reason the name doesn't look as expected then just tack __impl to the
  // front.
  // TODO: Use the name mangler to produce the right name instead of using
  // string replacement.
  StringRef CallOpName = CallOpFn->getName();
  std::string ImplName;
  if (size_t Pos = CallOpName.find_first_of("<lambda"))
    ImplName = ("?__impl@" + CallOpName.drop_front(Pos)).str();
  else
    ImplName = ("__impl" + CallOpName).str();

  llvm::Function *Fn = CallOpFn->getParent()->getFunction(ImplName);
  if (!Fn) {
    Fn = llvm::Function::Create(CGM.getTypes().GetFunctionType(**ImplFnInfo),
                                llvm::GlobalValue::InternalLinkage, ImplName,
                                CGM.getModule());
    CGM.SetInternalFunctionAttributes(CallOp, Fn, **ImplFnInfo);

    const GlobalDecl &GD = GlobalDecl(CallOp);
    const auto *D = cast<FunctionDecl>(GD.getDecl());
    CodeGenFunction(CGM).GenerateCode(GD, Fn, **ImplFnInfo);
    CGM.SetLLVMFunctionAttributesForDefinition(D, Fn);
  }
  *ImplFn = Fn;
}
