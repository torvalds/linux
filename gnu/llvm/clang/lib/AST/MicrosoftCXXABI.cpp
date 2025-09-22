//===------- MicrosoftCXXABI.cpp - AST support for the Microsoft C++ ABI --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides C++ AST support targeting the Microsoft Visual C++
// ABI.
//
//===----------------------------------------------------------------------===//

#include "CXXABI.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/MangleNumberingContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/TargetInfo.h"

using namespace clang;

namespace {

/// Numbers things which need to correspond across multiple TUs.
/// Typically these are things like static locals, lambdas, or blocks.
class MicrosoftNumberingContext : public MangleNumberingContext {
  llvm::DenseMap<const Type *, unsigned> ManglingNumbers;
  unsigned LambdaManglingNumber = 0;
  unsigned StaticLocalNumber = 0;
  unsigned StaticThreadlocalNumber = 0;

public:
  MicrosoftNumberingContext() = default;

  unsigned getManglingNumber(const CXXMethodDecl *CallOperator) override {
    return ++LambdaManglingNumber;
  }

  unsigned getManglingNumber(const BlockDecl *BD) override {
    const Type *Ty = nullptr;
    return ++ManglingNumbers[Ty];
  }

  unsigned getStaticLocalNumber(const VarDecl *VD) override {
    if (VD->getTLSKind())
      return ++StaticThreadlocalNumber;
    return ++StaticLocalNumber;
  }

  unsigned getManglingNumber(const VarDecl *VD,
                             unsigned MSLocalManglingNumber) override {
    return MSLocalManglingNumber;
  }

  unsigned getManglingNumber(const TagDecl *TD,
                             unsigned MSLocalManglingNumber) override {
    return MSLocalManglingNumber;
  }
};

class MSHIPNumberingContext : public MicrosoftNumberingContext {
  std::unique_ptr<MangleNumberingContext> DeviceCtx;

public:
  using MicrosoftNumberingContext::getManglingNumber;
  MSHIPNumberingContext(MangleContext *DeviceMangler) {
    DeviceCtx = createItaniumNumberingContext(DeviceMangler);
  }

  unsigned getDeviceManglingNumber(const CXXMethodDecl *CallOperator) override {
    return DeviceCtx->getManglingNumber(CallOperator);
  }

  unsigned getManglingNumber(const TagDecl *TD,
                             unsigned MSLocalManglingNumber) override {
    unsigned DeviceN = DeviceCtx->getManglingNumber(TD, MSLocalManglingNumber);
    unsigned HostN =
        MicrosoftNumberingContext::getManglingNumber(TD, MSLocalManglingNumber);
    if (DeviceN > 0xFFFF || HostN > 0xFFFF) {
      DiagnosticsEngine &Diags = TD->getASTContext().getDiagnostics();
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Error, "Mangling number exceeds limit (65535)");
      Diags.Report(TD->getLocation(), DiagID);
    }
    return (DeviceN << 16) | HostN;
  }
};

class MSSYCLNumberingContext : public MicrosoftNumberingContext {
  std::unique_ptr<MangleNumberingContext> DeviceCtx;

public:
  MSSYCLNumberingContext(MangleContext *DeviceMangler) {
    DeviceCtx = createItaniumNumberingContext(DeviceMangler);
  }

  unsigned getDeviceManglingNumber(const CXXMethodDecl *CallOperator) override {
    return DeviceCtx->getManglingNumber(CallOperator);
  }
};

class MicrosoftCXXABI : public CXXABI {
  ASTContext &Context;
  llvm::SmallDenseMap<CXXRecordDecl *, CXXConstructorDecl *> RecordToCopyCtor;

  llvm::SmallDenseMap<TagDecl *, DeclaratorDecl *>
      UnnamedTagDeclToDeclaratorDecl;
  llvm::SmallDenseMap<TagDecl *, TypedefNameDecl *>
      UnnamedTagDeclToTypedefNameDecl;

  // MangleContext for device numbering context, which is based on Itanium C++
  // ABI.
  std::unique_ptr<MangleContext> DeviceMangler;

public:
  MicrosoftCXXABI(ASTContext &Ctx) : Context(Ctx) {
    if (Context.getLangOpts().CUDA && Context.getAuxTargetInfo()) {
      assert(Context.getTargetInfo().getCXXABI().isMicrosoft() &&
             Context.getAuxTargetInfo()->getCXXABI().isItaniumFamily() &&
             "Unexpected combination of C++ ABIs.");
      DeviceMangler.reset(
          Context.createMangleContext(Context.getAuxTargetInfo()));
    }
    else if (Context.getLangOpts().isSYCL()) {
      DeviceMangler.reset(
          ItaniumMangleContext::create(Context, Context.getDiagnostics()));
    }
  }

  MemberPointerInfo
  getMemberPointerInfo(const MemberPointerType *MPT) const override;

  CallingConv getDefaultMethodCallConv(bool isVariadic) const override {
    if (!isVariadic &&
        Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86)
      return CC_X86ThisCall;
    return Context.getTargetInfo().getDefaultCallingConv();
  }

  bool isNearlyEmpty(const CXXRecordDecl *RD) const override {
    llvm_unreachable("unapplicable to the MS ABI");
  }

  const CXXConstructorDecl *
  getCopyConstructorForExceptionObject(CXXRecordDecl *RD) override {
    return RecordToCopyCtor[RD];
  }

  void
  addCopyConstructorForExceptionObject(CXXRecordDecl *RD,
                                       CXXConstructorDecl *CD) override {
    assert(CD != nullptr);
    assert(RecordToCopyCtor[RD] == nullptr || RecordToCopyCtor[RD] == CD);
    RecordToCopyCtor[RD] = CD;
  }

  void addTypedefNameForUnnamedTagDecl(TagDecl *TD,
                                       TypedefNameDecl *DD) override {
    TD = TD->getCanonicalDecl();
    DD = DD->getCanonicalDecl();
    TypedefNameDecl *&I = UnnamedTagDeclToTypedefNameDecl[TD];
    if (!I)
      I = DD;
  }

  TypedefNameDecl *getTypedefNameForUnnamedTagDecl(const TagDecl *TD) override {
    return UnnamedTagDeclToTypedefNameDecl.lookup(
        const_cast<TagDecl *>(TD->getCanonicalDecl()));
  }

  void addDeclaratorForUnnamedTagDecl(TagDecl *TD,
                                      DeclaratorDecl *DD) override {
    TD = TD->getCanonicalDecl();
    DD = cast<DeclaratorDecl>(DD->getCanonicalDecl());
    DeclaratorDecl *&I = UnnamedTagDeclToDeclaratorDecl[TD];
    if (!I)
      I = DD;
  }

  DeclaratorDecl *getDeclaratorForUnnamedTagDecl(const TagDecl *TD) override {
    return UnnamedTagDeclToDeclaratorDecl.lookup(
        const_cast<TagDecl *>(TD->getCanonicalDecl()));
  }

  std::unique_ptr<MangleNumberingContext>
  createMangleNumberingContext() const override {
    if (Context.getLangOpts().CUDA && Context.getAuxTargetInfo()) {
      assert(DeviceMangler && "Missing device mangler");
      return std::make_unique<MSHIPNumberingContext>(DeviceMangler.get());
    } else if (Context.getLangOpts().isSYCL()) {
      assert(DeviceMangler && "Missing device mangler");
      return std::make_unique<MSSYCLNumberingContext>(DeviceMangler.get());
    }

    return std::make_unique<MicrosoftNumberingContext>();
  }
};
}

// getNumBases() seems to only give us the number of direct bases, and not the
// total.  This function tells us if we inherit from anybody that uses MI, or if
// we have a non-primary base class, which uses the multiple inheritance model.
static bool usesMultipleInheritanceModel(const CXXRecordDecl *RD) {
  while (RD->getNumBases() > 0) {
    if (RD->getNumBases() > 1)
      return true;
    assert(RD->getNumBases() == 1);
    const CXXRecordDecl *Base =
        RD->bases_begin()->getType()->getAsCXXRecordDecl();
    if (RD->isPolymorphic() && !Base->isPolymorphic())
      return true;
    RD = Base;
  }
  return false;
}

MSInheritanceModel CXXRecordDecl::calculateInheritanceModel() const {
  if (!hasDefinition() || isParsingBaseSpecifiers())
    return MSInheritanceModel::Unspecified;
  if (getNumVBases() > 0)
    return MSInheritanceModel::Virtual;
  if (usesMultipleInheritanceModel(this))
    return MSInheritanceModel::Multiple;
  return MSInheritanceModel::Single;
}

MSInheritanceModel CXXRecordDecl::getMSInheritanceModel() const {
  MSInheritanceAttr *IA = getAttr<MSInheritanceAttr>();
  assert(IA && "Expected MSInheritanceAttr on the CXXRecordDecl!");
  return IA->getInheritanceModel();
}

bool CXXRecordDecl::nullFieldOffsetIsZero() const {
  return !inheritanceModelHasOnlyOneField(/*IsMemberFunction=*/false,
                                          getMSInheritanceModel()) ||
         (hasDefinition() && isPolymorphic());
}

MSVtorDispMode CXXRecordDecl::getMSVtorDispMode() const {
  if (MSVtorDispAttr *VDA = getAttr<MSVtorDispAttr>())
    return VDA->getVtorDispMode();
  return getASTContext().getLangOpts().getVtorDispMode();
}

// Returns the number of pointer and integer slots used to represent a member
// pointer in the MS C++ ABI.
//
// Member function pointers have the following general form;  however, fields
// are dropped as permitted (under the MSVC interpretation) by the inheritance
// model of the actual class.
//
//   struct {
//     // A pointer to the member function to call.  If the member function is
//     // virtual, this will be a thunk that forwards to the appropriate vftable
//     // slot.
//     void *FunctionPointerOrVirtualThunk;
//
//     // An offset to add to the address of the vbtable pointer after
//     // (possibly) selecting the virtual base but before resolving and calling
//     // the function.
//     // Only needed if the class has any virtual bases or bases at a non-zero
//     // offset.
//     int NonVirtualBaseAdjustment;
//
//     // The offset of the vb-table pointer within the object.  Only needed for
//     // incomplete types.
//     int VBPtrOffset;
//
//     // An offset within the vb-table that selects the virtual base containing
//     // the member.  Loading from this offset produces a new offset that is
//     // added to the address of the vb-table pointer to produce the base.
//     int VirtualBaseAdjustmentOffset;
//   };
static std::pair<unsigned, unsigned>
getMSMemberPointerSlots(const MemberPointerType *MPT) {
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  MSInheritanceModel Inheritance = RD->getMSInheritanceModel();
  unsigned Ptrs = 0;
  unsigned Ints = 0;
  if (MPT->isMemberFunctionPointer())
    Ptrs = 1;
  else
    Ints = 1;
  if (inheritanceModelHasNVOffsetField(MPT->isMemberFunctionPointer(),
                                          Inheritance))
    Ints++;
  if (inheritanceModelHasVBPtrOffsetField(Inheritance))
    Ints++;
  if (inheritanceModelHasVBTableOffsetField(Inheritance))
    Ints++;
  return std::make_pair(Ptrs, Ints);
}

CXXABI::MemberPointerInfo MicrosoftCXXABI::getMemberPointerInfo(
    const MemberPointerType *MPT) const {
  // The nominal struct is laid out with pointers followed by ints and aligned
  // to a pointer width if any are present and an int width otherwise.
  const TargetInfo &Target = Context.getTargetInfo();
  unsigned PtrSize = Target.getPointerWidth(LangAS::Default);
  unsigned IntSize = Target.getIntWidth();

  unsigned Ptrs, Ints;
  std::tie(Ptrs, Ints) = getMSMemberPointerSlots(MPT);
  MemberPointerInfo MPI;
  MPI.HasPadding = false;
  MPI.Width = Ptrs * PtrSize + Ints * IntSize;

  // When MSVC does x86_32 record layout, it aligns aggregate member pointers to
  // 8 bytes.  However, __alignof usually returns 4 for data memptrs and 8 for
  // function memptrs.
  if (Ptrs + Ints > 1 && Target.getTriple().isArch32Bit())
    MPI.Align = 64;
  else if (Ptrs)
    MPI.Align = Target.getPointerAlign(LangAS::Default);
  else
    MPI.Align = Target.getIntAlign();

  if (Target.getTriple().isArch64Bit()) {
    MPI.Width = llvm::alignTo(MPI.Width, MPI.Align);
    MPI.HasPadding = MPI.Width != (Ptrs * PtrSize + Ints * IntSize);
  }
  return MPI;
}

CXXABI *clang::CreateMicrosoftCXXABI(ASTContext &Ctx) {
  return new MicrosoftCXXABI(Ctx);
}
