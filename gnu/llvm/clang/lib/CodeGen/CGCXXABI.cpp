//===----- CGCXXABI.cpp - Interface to C++ ABIs ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for C++ code generation. Concrete subclasses
// of this implement code generation for specific C++ ABIs.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "clang/AST/Attr.h"

using namespace clang;
using namespace CodeGen;

CGCXXABI::~CGCXXABI() { }

Address CGCXXABI::getThisAddress(CodeGenFunction &CGF) {
  return CGF.makeNaturalAddressForPointer(
      CGF.CXXABIThisValue, CGF.CXXABIThisDecl->getType()->getPointeeType(),
      CGF.CXXABIThisAlignment);
}

void CGCXXABI::ErrorUnsupportedABI(CodeGenFunction &CGF, StringRef S) {
  DiagnosticsEngine &Diags = CGF.CGM.getDiags();
  unsigned DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                          "cannot yet compile %0 in this ABI");
  Diags.Report(CGF.getContext().getFullLoc(CGF.CurCodeDecl->getLocation()),
               DiagID)
    << S;
}

llvm::Constant *CGCXXABI::GetBogusMemberPointer(QualType T) {
  return llvm::Constant::getNullValue(CGM.getTypes().ConvertType(T));
}

llvm::Type *
CGCXXABI::ConvertMemberPointerType(const MemberPointerType *MPT) {
  return CGM.getTypes().ConvertType(CGM.getContext().getPointerDiffType());
}

CGCallee CGCXXABI::EmitLoadOfMemberFunctionPointer(
    CodeGenFunction &CGF, const Expr *E, Address This,
    llvm::Value *&ThisPtrForCall,
    llvm::Value *MemPtr, const MemberPointerType *MPT) {
  ErrorUnsupportedABI(CGF, "calls through member pointers");

  const auto *RD =
      cast<CXXRecordDecl>(MPT->getClass()->castAs<RecordType>()->getDecl());
  ThisPtrForCall =
      CGF.getAsNaturalPointerTo(This, CGF.getContext().getRecordType(RD));
  const FunctionProtoType *FPT =
      MPT->getPointeeType()->getAs<FunctionProtoType>();
  llvm::Constant *FnPtr = llvm::Constant::getNullValue(
      llvm::PointerType::getUnqual(CGM.getLLVMContext()));
  return CGCallee::forDirect(FnPtr, FPT);
}

llvm::Value *
CGCXXABI::EmitMemberDataPointerAddress(CodeGenFunction &CGF, const Expr *E,
                                       Address Base, llvm::Value *MemPtr,
                                       const MemberPointerType *MPT) {
  ErrorUnsupportedABI(CGF, "loads of member pointers");
  llvm::Type *Ty =
      llvm::PointerType::get(CGF.getLLVMContext(), Base.getAddressSpace());
  return llvm::Constant::getNullValue(Ty);
}

llvm::Value *CGCXXABI::EmitMemberPointerConversion(CodeGenFunction &CGF,
                                                   const CastExpr *E,
                                                   llvm::Value *Src) {
  ErrorUnsupportedABI(CGF, "member function pointer conversions");
  return GetBogusMemberPointer(E->getType());
}

llvm::Constant *CGCXXABI::EmitMemberPointerConversion(const CastExpr *E,
                                                      llvm::Constant *Src) {
  return GetBogusMemberPointer(E->getType());
}

llvm::Value *
CGCXXABI::EmitMemberPointerComparison(CodeGenFunction &CGF,
                                      llvm::Value *L,
                                      llvm::Value *R,
                                      const MemberPointerType *MPT,
                                      bool Inequality) {
  ErrorUnsupportedABI(CGF, "member function pointer comparison");
  return CGF.Builder.getFalse();
}

llvm::Value *
CGCXXABI::EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                     llvm::Value *MemPtr,
                                     const MemberPointerType *MPT) {
  ErrorUnsupportedABI(CGF, "member function pointer null testing");
  return CGF.Builder.getFalse();
}

llvm::Constant *
CGCXXABI::EmitNullMemberPointer(const MemberPointerType *MPT) {
  return GetBogusMemberPointer(QualType(MPT, 0));
}

llvm::Constant *CGCXXABI::EmitMemberFunctionPointer(const CXXMethodDecl *MD) {
  return GetBogusMemberPointer(CGM.getContext().getMemberPointerType(
      MD->getType(), MD->getParent()->getTypeForDecl()));
}

llvm::Constant *CGCXXABI::EmitMemberDataPointer(const MemberPointerType *MPT,
                                                CharUnits offset) {
  return GetBogusMemberPointer(QualType(MPT, 0));
}

llvm::Constant *CGCXXABI::EmitMemberPointer(const APValue &MP, QualType MPT) {
  return GetBogusMemberPointer(MPT);
}

bool CGCXXABI::isZeroInitializable(const MemberPointerType *MPT) {
  // Fake answer.
  return true;
}

void CGCXXABI::buildThisParam(CodeGenFunction &CGF, FunctionArgList &params) {
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(CGF.CurGD.getDecl());

  // FIXME: I'm not entirely sure I like using a fake decl just for code
  // generation. Maybe we can come up with a better way?
  auto *ThisDecl =
      ImplicitParamDecl::Create(CGM.getContext(), nullptr, MD->getLocation(),
                                &CGM.getContext().Idents.get("this"),
                                MD->getThisType(), ImplicitParamKind::CXXThis);
  params.push_back(ThisDecl);
  CGF.CXXABIThisDecl = ThisDecl;

  // Compute the presumed alignment of 'this', which basically comes
  // down to whether we know it's a complete object or not.
  auto &Layout = CGF.getContext().getASTRecordLayout(MD->getParent());
  if (MD->getParent()->getNumVBases() == 0 || // avoid vcall in common case
      MD->getParent()->isEffectivelyFinal() ||
      isThisCompleteObject(CGF.CurGD)) {
    CGF.CXXABIThisAlignment = Layout.getAlignment();
  } else {
    CGF.CXXABIThisAlignment = Layout.getNonVirtualAlignment();
  }
}

llvm::Value *CGCXXABI::loadIncomingCXXThis(CodeGenFunction &CGF) {
  return CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(getThisDecl(CGF)),
                                "this");
}

void CGCXXABI::setCXXABIThisValue(CodeGenFunction &CGF, llvm::Value *ThisPtr) {
  /// Initialize the 'this' slot.
  assert(getThisDecl(CGF) && "no 'this' variable for function");
  CGF.CXXABIThisValue = ThisPtr;
}

bool CGCXXABI::mayNeedDestruction(const VarDecl *VD) const {
  if (VD->needsDestruction(getContext()))
    return true;

  // If the variable has an incomplete class type (or array thereof), it
  // might need destruction.
  const Type *T = VD->getType()->getBaseElementTypeUnsafe();
  if (T->getAs<RecordType>() && T->isIncompleteType())
    return true;

  return false;
}

bool CGCXXABI::isEmittedWithConstantInitializer(
    const VarDecl *VD, bool InspectInitForWeakDef) const {
  VD = VD->getMostRecentDecl();
  if (VD->hasAttr<ConstInitAttr>())
    return true;

  // All later checks examine the initializer specified on the variable. If
  // the variable is weak, such examination would not be correct.
  if (!InspectInitForWeakDef && (VD->isWeak() || VD->hasAttr<SelectAnyAttr>()))
    return false;

  const VarDecl *InitDecl = VD->getInitializingDeclaration();
  if (!InitDecl)
    return false;

  // If there's no initializer to run, this is constant initialization.
  if (!InitDecl->hasInit())
    return true;

  // If we have the only definition, we don't need a thread wrapper if we
  // will emit the value as a constant.
  if (isUniqueGVALinkage(getContext().GetGVALinkageForVariable(VD)))
    return !mayNeedDestruction(VD) && InitDecl->evaluateValue();

  // Otherwise, we need a thread wrapper unless we know that every
  // translation unit will emit the value as a constant. We rely on the
  // variable being constant-initialized in every translation unit if it's
  // constant-initialized in any translation unit, which isn't actually
  // guaranteed by the standard but is necessary for sanity.
  return InitDecl->hasConstantInitialization();
}

void CGCXXABI::EmitReturnFromThunk(CodeGenFunction &CGF,
                                   RValue RV, QualType ResultType) {
  assert(!CGF.hasAggregateEvaluationKind(ResultType) &&
         "cannot handle aggregates");
  CGF.EmitReturnOfRValue(RV, ResultType);
}

CharUnits CGCXXABI::GetArrayCookieSize(const CXXNewExpr *expr) {
  if (!requiresArrayCookie(expr))
    return CharUnits::Zero();
  return getArrayCookieSizeImpl(expr->getAllocatedType());
}

CharUnits CGCXXABI::getArrayCookieSizeImpl(QualType elementType) {
  // BOGUS
  return CharUnits::Zero();
}

Address CGCXXABI::InitializeArrayCookie(CodeGenFunction &CGF,
                                        Address NewPtr,
                                        llvm::Value *NumElements,
                                        const CXXNewExpr *expr,
                                        QualType ElementType) {
  // Should never be called.
  ErrorUnsupportedABI(CGF, "array cookie initialization");
  return Address::invalid();
}

bool CGCXXABI::requiresArrayCookie(const CXXDeleteExpr *expr,
                                   QualType elementType) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (expr->doesUsualArrayDeleteWantSize())
    return true;

  return elementType.isDestructedType();
}

bool CGCXXABI::requiresArrayCookie(const CXXNewExpr *expr) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (expr->doesUsualArrayDeleteWantSize())
    return true;

  return expr->getAllocatedType().isDestructedType();
}

void CGCXXABI::ReadArrayCookie(CodeGenFunction &CGF, Address ptr,
                               const CXXDeleteExpr *expr, QualType eltTy,
                               llvm::Value *&numElements,
                               llvm::Value *&allocPtr, CharUnits &cookieSize) {
  // Derive a char* in the same address space as the pointer.
  ptr = ptr.withElementType(CGF.Int8Ty);

  // If we don't need an array cookie, bail out early.
  if (!requiresArrayCookie(expr, eltTy)) {
    allocPtr = ptr.emitRawPointer(CGF);
    numElements = nullptr;
    cookieSize = CharUnits::Zero();
    return;
  }

  cookieSize = getArrayCookieSizeImpl(eltTy);
  Address allocAddr = CGF.Builder.CreateConstInBoundsByteGEP(ptr, -cookieSize);
  allocPtr = allocAddr.emitRawPointer(CGF);
  numElements = readArrayCookieImpl(CGF, allocAddr, cookieSize);
}

llvm::Value *CGCXXABI::readArrayCookieImpl(CodeGenFunction &CGF,
                                           Address ptr,
                                           CharUnits cookieSize) {
  ErrorUnsupportedABI(CGF, "reading a new[] cookie");
  return llvm::ConstantInt::get(CGF.SizeTy, 0);
}

/// Returns the adjustment, in bytes, required for the given
/// member-pointer operation.  Returns null if no adjustment is
/// required.
llvm::Constant *CGCXXABI::getMemberPointerAdjustment(const CastExpr *E) {
  assert(E->getCastKind() == CK_DerivedToBaseMemberPointer ||
         E->getCastKind() == CK_BaseToDerivedMemberPointer);

  QualType derivedType;
  if (E->getCastKind() == CK_DerivedToBaseMemberPointer)
    derivedType = E->getSubExpr()->getType();
  else
    derivedType = E->getType();

  const CXXRecordDecl *derivedClass =
    derivedType->castAs<MemberPointerType>()->getClass()->getAsCXXRecordDecl();

  return CGM.GetNonVirtualBaseClassOffset(derivedClass,
                                          E->path_begin(),
                                          E->path_end());
}

llvm::BasicBlock *
CGCXXABI::EmitCtorCompleteObjectHandler(CodeGenFunction &CGF,
                                        const CXXRecordDecl *RD) {
  if (CGM.getTarget().getCXXABI().hasConstructorVariants())
    llvm_unreachable("shouldn't be called in this ABI");

  ErrorUnsupportedABI(CGF, "complete object detection in ctor");
  return nullptr;
}

void CGCXXABI::setCXXDestructorDLLStorage(llvm::GlobalValue *GV,
                                          const CXXDestructorDecl *Dtor,
                                          CXXDtorType DT) const {
  // Assume the base C++ ABI has no special rules for destructor variants.
  CGM.setDLLImportDLLExport(GV, Dtor);
}

llvm::GlobalValue::LinkageTypes CGCXXABI::getCXXDestructorLinkage(
    GVALinkage Linkage, const CXXDestructorDecl *Dtor, CXXDtorType DT) const {
  // Delegate back to CGM by default.
  return CGM.getLLVMLinkageForDeclarator(Dtor, Linkage);
}

bool CGCXXABI::NeedsVTTParameter(GlobalDecl GD) {
  return false;
}

llvm::CallInst *
CGCXXABI::emitTerminateForUnexpectedException(CodeGenFunction &CGF,
                                              llvm::Value *Exn) {
  // Just call std::terminate and ignore the violating exception.
  return CGF.EmitNounwindRuntimeCall(CGF.CGM.getTerminateFn());
}

CatchTypeInfo CGCXXABI::getCatchAllTypeInfo() {
  return CatchTypeInfo{nullptr, 0};
}

std::vector<CharUnits> CGCXXABI::getVBPtrOffsets(const CXXRecordDecl *RD) {
  return std::vector<CharUnits>();
}

CGCXXABI::AddedStructorArgCounts CGCXXABI::addImplicitConstructorArgs(
    CodeGenFunction &CGF, const CXXConstructorDecl *D, CXXCtorType Type,
    bool ForVirtualBase, bool Delegating, CallArgList &Args) {
  AddedStructorArgs AddedArgs =
      getImplicitConstructorArgs(CGF, D, Type, ForVirtualBase, Delegating);
  for (size_t i = 0; i < AddedArgs.Prefix.size(); ++i) {
    Args.insert(Args.begin() + 1 + i,
                CallArg(RValue::get(AddedArgs.Prefix[i].Value),
                        AddedArgs.Prefix[i].Type));
  }
  for (const auto &arg : AddedArgs.Suffix) {
    Args.add(RValue::get(arg.Value), arg.Type);
  }
  return AddedStructorArgCounts(AddedArgs.Prefix.size(),
                                AddedArgs.Suffix.size());
}
