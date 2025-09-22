//===--- Mangle.cpp - Mangle C++ Names --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements generic name mangling support for blocks and Objective-C.
//
//===----------------------------------------------------------------------===//
#include "clang/AST/Attr.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/ABI.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

// FIXME: For blocks we currently mimic GCC's mangling scheme, which leaves
// much to be desired. Come up with a better mangling scheme.

static void mangleFunctionBlock(MangleContext &Context,
                                StringRef Outer,
                                const BlockDecl *BD,
                                raw_ostream &Out) {
  unsigned discriminator = Context.getBlockId(BD, true);
  if (discriminator == 0)
    Out << "__" << Outer << "_block_invoke";
  else
    Out << "__" << Outer << "_block_invoke_" << discriminator+1;
}

void MangleContext::anchor() { }

enum CCMangling {
  CCM_Other,
  CCM_Fast,
  CCM_RegCall,
  CCM_Vector,
  CCM_Std,
  CCM_WasmMainArgcArgv
};

static bool isExternC(const NamedDecl *ND) {
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(ND))
    return FD->isExternC();
  if (const VarDecl *VD = dyn_cast<VarDecl>(ND))
    return VD->isExternC();
  return false;
}

static CCMangling getCallingConvMangling(const ASTContext &Context,
                                         const NamedDecl *ND) {
  const TargetInfo &TI = Context.getTargetInfo();
  const llvm::Triple &Triple = TI.getTriple();

  // On wasm, the argc/argv form of "main" is renamed so that the startup code
  // can call it with the correct function signature.
  if (Triple.isWasm())
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(ND))
      if (FD->isMain() && FD->getNumParams() == 2)
        return CCM_WasmMainArgcArgv;

  if (!Triple.isOSWindows() || !Triple.isX86())
    return CCM_Other;

  if (Context.getLangOpts().CPlusPlus && !isExternC(ND) &&
      TI.getCXXABI() == TargetCXXABI::Microsoft)
    return CCM_Other;

  const FunctionDecl *FD = dyn_cast<FunctionDecl>(ND);
  if (!FD)
    return CCM_Other;
  QualType T = FD->getType();

  const FunctionType *FT = T->castAs<FunctionType>();

  CallingConv CC = FT->getCallConv();
  switch (CC) {
  default:
    return CCM_Other;
  case CC_X86FastCall:
    return CCM_Fast;
  case CC_X86StdCall:
    return CCM_Std;
  case CC_X86VectorCall:
    return CCM_Vector;
  }
}

bool MangleContext::shouldMangleDeclName(const NamedDecl *D) {
  const ASTContext &ASTContext = getASTContext();

  CCMangling CC = getCallingConvMangling(ASTContext, D);
  if (CC != CCM_Other)
    return true;

  // If the declaration has an owning module for linkage purposes that needs to
  // be mangled, we must mangle its name.
  if (!D->hasExternalFormalLinkage() && D->getOwningModuleForLinkage())
    return true;

  // C functions with internal linkage have to be mangled with option
  // -funique-internal-linkage-names.
  if (!getASTContext().getLangOpts().CPlusPlus &&
      isUniqueInternalLinkageDecl(D))
    return true;

  // In C, functions with no attributes never need to be mangled. Fastpath them.
  if (!getASTContext().getLangOpts().CPlusPlus && !D->hasAttrs())
    return false;

  // Any decl can be declared with __asm("foo") on it, and this takes precedence
  // over all other naming in the .o file.
  if (D->hasAttr<AsmLabelAttr>())
    return true;

  // Declarations that don't have identifier names always need to be mangled.
  if (isa<MSGuidDecl>(D))
    return true;

  return shouldMangleCXXName(D);
}

void MangleContext::mangleName(GlobalDecl GD, raw_ostream &Out) {
  const ASTContext &ASTContext = getASTContext();
  const NamedDecl *D = cast<NamedDecl>(GD.getDecl());

  // Any decl can be declared with __asm("foo") on it, and this takes precedence
  // over all other naming in the .o file.
  if (const AsmLabelAttr *ALA = D->getAttr<AsmLabelAttr>()) {
    // If we have an asm name, then we use it as the mangling.

    // If the label isn't literal, or if this is an alias for an LLVM intrinsic,
    // do not add a "\01" prefix.
    if (!ALA->getIsLiteralLabel() || ALA->getLabel().starts_with("llvm.")) {
      Out << ALA->getLabel();
      return;
    }

    // Adding the prefix can cause problems when one file has a "foo" and
    // another has a "\01foo". That is known to happen on ELF with the
    // tricks normally used for producing aliases (PR9177). Fortunately the
    // llvm mangler on ELF is a nop, so we can just avoid adding the \01
    // marker.
    StringRef UserLabelPrefix =
        getASTContext().getTargetInfo().getUserLabelPrefix();
#ifndef NDEBUG
    char GlobalPrefix =
        llvm::DataLayout(getASTContext().getTargetInfo().getDataLayoutString())
            .getGlobalPrefix();
    assert((UserLabelPrefix.empty() && !GlobalPrefix) ||
           (UserLabelPrefix.size() == 1 && UserLabelPrefix[0] == GlobalPrefix));
#endif
    if (!UserLabelPrefix.empty())
      Out << '\01'; // LLVM IR Marker for __asm("foo")

    Out << ALA->getLabel();
    return;
  }

  if (auto *GD = dyn_cast<MSGuidDecl>(D))
    return mangleMSGuidDecl(GD, Out);

  CCMangling CC = getCallingConvMangling(ASTContext, D);

  if (CC == CCM_WasmMainArgcArgv) {
    Out << "__main_argc_argv";
    return;
  }

  bool MCXX = shouldMangleCXXName(D);
  const TargetInfo &TI = Context.getTargetInfo();
  if (CC == CCM_Other || (MCXX && TI.getCXXABI() == TargetCXXABI::Microsoft)) {
    if (const ObjCMethodDecl *OMD = dyn_cast<ObjCMethodDecl>(D))
      mangleObjCMethodNameAsSourceName(OMD, Out);
    else
      mangleCXXName(GD, Out);
    return;
  }

  Out << '\01';
  if (CC == CCM_Std)
    Out << '_';
  else if (CC == CCM_Fast)
    Out << '@';
  else if (CC == CCM_RegCall) {
    if (getASTContext().getLangOpts().RegCall4)
      Out << "__regcall4__";
    else
      Out << "__regcall3__";
  }

  if (!MCXX)
    Out << D->getIdentifier()->getName();
  else if (const ObjCMethodDecl *OMD = dyn_cast<ObjCMethodDecl>(D))
    mangleObjCMethodNameAsSourceName(OMD, Out);
  else
    mangleCXXName(GD, Out);

  const FunctionDecl *FD = cast<FunctionDecl>(D);
  const FunctionType *FT = FD->getType()->castAs<FunctionType>();
  const FunctionProtoType *Proto = dyn_cast<FunctionProtoType>(FT);
  if (CC == CCM_Vector)
    Out << '@';
  Out << '@';
  if (!Proto) {
    Out << '0';
    return;
  }
  assert(!Proto->isVariadic());
  unsigned ArgWords = 0;
  if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD))
    if (MD->isImplicitObjectMemberFunction())
      ++ArgWords;
  uint64_t DefaultPtrWidth = TI.getPointerWidth(LangAS::Default);
  for (const auto &AT : Proto->param_types()) {
    // If an argument type is incomplete there is no way to get its size to
    // correctly encode into the mangling scheme.
    // Follow GCCs behaviour by simply breaking out of the loop.
    if (AT->isIncompleteType())
      break;
    // Size should be aligned to pointer size.
    ArgWords += llvm::alignTo(ASTContext.getTypeSize(AT), DefaultPtrWidth) /
                DefaultPtrWidth;
  }
  Out << ((DefaultPtrWidth / 8) * ArgWords);
}

void MangleContext::mangleMSGuidDecl(const MSGuidDecl *GD, raw_ostream &Out) {
  // For now, follow the MSVC naming convention for GUID objects on all
  // targets.
  MSGuidDecl::Parts P = GD->getParts();
  Out << llvm::format("_GUID_%08" PRIx32 "_%04" PRIx32 "_%04" PRIx32 "_",
                      P.Part1, P.Part2, P.Part3);
  unsigned I = 0;
  for (uint8_t C : P.Part4And5) {
    Out << llvm::format("%02" PRIx8, C);
    if (++I == 2)
      Out << "_";
  }
}

void MangleContext::mangleGlobalBlock(const BlockDecl *BD,
                                      const NamedDecl *ID,
                                      raw_ostream &Out) {
  unsigned discriminator = getBlockId(BD, false);
  if (ID) {
    if (shouldMangleDeclName(ID))
      mangleName(ID, Out);
    else {
      Out << ID->getIdentifier()->getName();
    }
  }
  if (discriminator == 0)
    Out << "_block_invoke";
  else
    Out << "_block_invoke_" << discriminator+1;
}

void MangleContext::mangleCtorBlock(const CXXConstructorDecl *CD,
                                    CXXCtorType CT, const BlockDecl *BD,
                                    raw_ostream &ResStream) {
  SmallString<64> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  mangleName(GlobalDecl(CD, CT), Out);
  mangleFunctionBlock(*this, Buffer, BD, ResStream);
}

void MangleContext::mangleDtorBlock(const CXXDestructorDecl *DD,
                                    CXXDtorType DT, const BlockDecl *BD,
                                    raw_ostream &ResStream) {
  SmallString<64> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  mangleName(GlobalDecl(DD, DT), Out);
  mangleFunctionBlock(*this, Buffer, BD, ResStream);
}

void MangleContext::mangleBlock(const DeclContext *DC, const BlockDecl *BD,
                                raw_ostream &Out) {
  assert(!isa<CXXConstructorDecl>(DC) && !isa<CXXDestructorDecl>(DC));

  SmallString<64> Buffer;
  llvm::raw_svector_ostream Stream(Buffer);
  if (const ObjCMethodDecl *Method = dyn_cast<ObjCMethodDecl>(DC)) {
    mangleObjCMethodNameAsSourceName(Method, Stream);
  } else {
    assert((isa<NamedDecl>(DC) || isa<BlockDecl>(DC)) &&
           "expected a NamedDecl or BlockDecl");
    for (; isa_and_nonnull<BlockDecl>(DC); DC = DC->getParent())
      (void)getBlockId(cast<BlockDecl>(DC), true);
    assert((isa<TranslationUnitDecl>(DC) || isa<NamedDecl>(DC)) &&
           "expected a TranslationUnitDecl or a NamedDecl");
    if (const auto *CD = dyn_cast<CXXConstructorDecl>(DC))
      mangleCtorBlock(CD, /*CT*/ Ctor_Complete, BD, Out);
    else if (const auto *DD = dyn_cast<CXXDestructorDecl>(DC))
      mangleDtorBlock(DD, /*DT*/ Dtor_Complete, BD, Out);
    else if (auto ND = dyn_cast<NamedDecl>(DC)) {
      if (!shouldMangleDeclName(ND) && ND->getIdentifier())
        Stream << ND->getIdentifier()->getName();
      else {
        // FIXME: We were doing a mangleUnqualifiedName() before, but that's
        // a private member of a class that will soon itself be private to the
        // Itanium C++ ABI object. What should we do now? Right now, I'm just
        // calling the mangleName() method on the MangleContext; is there a
        // better way?
        mangleName(ND, Stream);
      }
    }
  }
  mangleFunctionBlock(*this, Buffer, BD, Out);
}

void MangleContext::mangleObjCMethodName(const ObjCMethodDecl *MD,
                                         raw_ostream &OS,
                                         bool includePrefixByte,
                                         bool includeCategoryNamespace) {
  if (getASTContext().getLangOpts().ObjCRuntime.isGNUFamily()) {
    // This is the mangling we've always used on the GNU runtimes, but it
    // has obvious collisions in the face of underscores within class
    // names, category names, and selectors; maybe we should improve it.

    OS << (MD->isClassMethod() ? "_c_" : "_i_")
       << MD->getClassInterface()->getName() << '_';

    if (includeCategoryNamespace) {
      if (auto category = MD->getCategory())
        OS << category->getName();
    }
    OS << '_';

    auto selector = MD->getSelector();
    for (unsigned slotIndex = 0,
                  numArgs = selector.getNumArgs(),
                  slotEnd = std::max(numArgs, 1U);
           slotIndex != slotEnd; ++slotIndex) {
      if (auto name = selector.getIdentifierInfoForSlot(slotIndex))
        OS << name->getName();

      // Replace all the positions that would've been ':' with '_'.
      // That's after each slot except that a unary selector doesn't
      // end in ':'.
      if (numArgs)
        OS << '_';
    }

    return;
  }

  // \01+[ContainerName(CategoryName) SelectorName]
  if (includePrefixByte) {
    OS << '\01';
  }
  OS << (MD->isInstanceMethod() ? '-' : '+') << '[';
  if (const auto *CID = MD->getCategory()) {
    OS << CID->getClassInterface()->getName();
    if (includeCategoryNamespace) {
      OS << '(' << *CID << ')';
    }
  } else if (const auto *CD =
                 dyn_cast<ObjCContainerDecl>(MD->getDeclContext())) {
    OS << CD->getName();
  } else {
    llvm_unreachable("Unexpected ObjC method decl context");
  }
  OS << ' ';
  MD->getSelector().print(OS);
  OS << ']';
}

void MangleContext::mangleObjCMethodNameAsSourceName(const ObjCMethodDecl *MD,
                                                     raw_ostream &Out) {
  SmallString<64> Name;
  llvm::raw_svector_ostream OS(Name);

  mangleObjCMethodName(MD, OS, /*includePrefixByte=*/false,
                       /*includeCategoryNamespace=*/true);
  Out << OS.str().size() << OS.str();
}

class ASTNameGenerator::Implementation {
  std::unique_ptr<MangleContext> MC;
  llvm::DataLayout DL;

public:
  explicit Implementation(ASTContext &Ctx)
      : MC(Ctx.createMangleContext()),
        DL(Ctx.getTargetInfo().getDataLayoutString()) {}

  bool writeName(const Decl *D, raw_ostream &OS) {
    // First apply frontend mangling.
    SmallString<128> FrontendBuf;
    llvm::raw_svector_ostream FrontendBufOS(FrontendBuf);
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->isDependentContext())
        return true;
      if (writeFuncOrVarName(FD, FrontendBufOS))
        return true;
    } else if (auto *VD = dyn_cast<VarDecl>(D)) {
      if (writeFuncOrVarName(VD, FrontendBufOS))
        return true;
    } else if (auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
      MC->mangleObjCMethodName(MD, OS, /*includePrefixByte=*/false,
                               /*includeCategoryNamespace=*/true);
      return false;
    } else if (auto *ID = dyn_cast<ObjCInterfaceDecl>(D)) {
      writeObjCClassName(ID, FrontendBufOS);
    } else {
      return true;
    }

    // Now apply backend mangling.
    llvm::Mangler::getNameWithPrefix(OS, FrontendBufOS.str(), DL);
    return false;
  }

  std::string getName(const Decl *D) {
    std::string Name;
    {
      llvm::raw_string_ostream OS(Name);
      writeName(D, OS);
    }
    return Name;
  }

  enum ObjCKind {
    ObjCClass,
    ObjCMetaclass,
  };

  static StringRef getClassSymbolPrefix(ObjCKind Kind,
                                        const ASTContext &Context) {
    if (Context.getLangOpts().ObjCRuntime.isGNUFamily())
      return Kind == ObjCMetaclass ? "_OBJC_METACLASS_" : "_OBJC_CLASS_";
    return Kind == ObjCMetaclass ? "OBJC_METACLASS_$_" : "OBJC_CLASS_$_";
  }

  std::vector<std::string> getAllManglings(const ObjCContainerDecl *OCD) {
    StringRef ClassName;
    if (const auto *OID = dyn_cast<ObjCInterfaceDecl>(OCD))
      ClassName = OID->getObjCRuntimeNameAsString();
    else if (const auto *OID = dyn_cast<ObjCImplementationDecl>(OCD))
      ClassName = OID->getObjCRuntimeNameAsString();

    if (ClassName.empty())
      return {};

    auto Mangle = [&](ObjCKind Kind, StringRef ClassName) -> std::string {
      SmallString<40> Mangled;
      auto Prefix = getClassSymbolPrefix(Kind, OCD->getASTContext());
      llvm::Mangler::getNameWithPrefix(Mangled, Prefix + ClassName, DL);
      return std::string(Mangled);
    };

    return {
        Mangle(ObjCClass, ClassName),
        Mangle(ObjCMetaclass, ClassName),
    };
  }

  std::vector<std::string> getAllManglings(const Decl *D) {
    if (const auto *OCD = dyn_cast<ObjCContainerDecl>(D))
      return getAllManglings(OCD);

    if (!(isa<CXXRecordDecl>(D) || isa<CXXMethodDecl>(D)))
      return {};

    const NamedDecl *ND = cast<NamedDecl>(D);

    ASTContext &Ctx = ND->getASTContext();
    std::unique_ptr<MangleContext> M(Ctx.createMangleContext());

    std::vector<std::string> Manglings;

    auto hasDefaultCXXMethodCC = [](ASTContext &C, const CXXMethodDecl *MD) {
      auto DefaultCC = C.getDefaultCallingConvention(/*IsVariadic=*/false,
                                                     /*IsCXXMethod=*/true);
      auto CC = MD->getType()->castAs<FunctionProtoType>()->getCallConv();
      return CC == DefaultCC;
    };

    if (const auto *CD = dyn_cast_or_null<CXXConstructorDecl>(ND)) {
      Manglings.emplace_back(getMangledStructor(CD, Ctor_Base));

      if (Ctx.getTargetInfo().getCXXABI().isItaniumFamily())
        if (!CD->getParent()->isAbstract())
          Manglings.emplace_back(getMangledStructor(CD, Ctor_Complete));

      if (Ctx.getTargetInfo().getCXXABI().isMicrosoft())
        if (CD->hasAttr<DLLExportAttr>() && CD->isDefaultConstructor())
          if (!(hasDefaultCXXMethodCC(Ctx, CD) && CD->getNumParams() == 0))
            Manglings.emplace_back(getMangledStructor(CD, Ctor_DefaultClosure));
    } else if (const auto *DD = dyn_cast_or_null<CXXDestructorDecl>(ND)) {
      Manglings.emplace_back(getMangledStructor(DD, Dtor_Base));
      if (Ctx.getTargetInfo().getCXXABI().isItaniumFamily()) {
        Manglings.emplace_back(getMangledStructor(DD, Dtor_Complete));
        if (DD->isVirtual())
          Manglings.emplace_back(getMangledStructor(DD, Dtor_Deleting));
      }
    } else if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(ND)) {
      Manglings.emplace_back(getName(ND));
      if (MD->isVirtual()) {
        if (const auto *TIV = Ctx.getVTableContext()->getThunkInfo(MD)) {
          for (const auto &T : *TIV) {
            std::string ThunkName;
            std::string ContextualizedName =
                getMangledThunk(MD, T, /* ElideOverrideInfo */ false);
            if (Ctx.useAbbreviatedThunkName(MD, ContextualizedName))
              ThunkName = getMangledThunk(MD, T, /* ElideOverrideInfo */ true);
            else
              ThunkName = ContextualizedName;
            Manglings.emplace_back(ThunkName);
          }
        }
      }
    }

    return Manglings;
  }

private:
  bool writeFuncOrVarName(const NamedDecl *D, raw_ostream &OS) {
    if (MC->shouldMangleDeclName(D)) {
      GlobalDecl GD;
      if (const auto *CtorD = dyn_cast<CXXConstructorDecl>(D))
        GD = GlobalDecl(CtorD, Ctor_Complete);
      else if (const auto *DtorD = dyn_cast<CXXDestructorDecl>(D))
        GD = GlobalDecl(DtorD, Dtor_Complete);
      else if (D->hasAttr<CUDAGlobalAttr>())
        GD = GlobalDecl(cast<FunctionDecl>(D));
      else
        GD = GlobalDecl(D);
      MC->mangleName(GD, OS);
      return false;
    } else {
      IdentifierInfo *II = D->getIdentifier();
      if (!II)
        return true;
      OS << II->getName();
      return false;
    }
  }

  void writeObjCClassName(const ObjCInterfaceDecl *D, raw_ostream &OS) {
    OS << getClassSymbolPrefix(ObjCClass, D->getASTContext());
    OS << D->getObjCRuntimeNameAsString();
  }

  std::string getMangledStructor(const NamedDecl *ND, unsigned StructorType) {
    std::string FrontendBuf;
    llvm::raw_string_ostream FOS(FrontendBuf);

    GlobalDecl GD;
    if (const auto *CD = dyn_cast_or_null<CXXConstructorDecl>(ND))
      GD = GlobalDecl(CD, static_cast<CXXCtorType>(StructorType));
    else if (const auto *DD = dyn_cast_or_null<CXXDestructorDecl>(ND))
      GD = GlobalDecl(DD, static_cast<CXXDtorType>(StructorType));
    MC->mangleName(GD, FOS);

    std::string BackendBuf;
    llvm::raw_string_ostream BOS(BackendBuf);

    llvm::Mangler::getNameWithPrefix(BOS, FOS.str(), DL);

    return BOS.str();
  }

  std::string getMangledThunk(const CXXMethodDecl *MD, const ThunkInfo &T,
                              bool ElideOverrideInfo) {
    std::string FrontendBuf;
    llvm::raw_string_ostream FOS(FrontendBuf);

    MC->mangleThunk(MD, T, ElideOverrideInfo, FOS);

    std::string BackendBuf;
    llvm::raw_string_ostream BOS(BackendBuf);

    llvm::Mangler::getNameWithPrefix(BOS, FOS.str(), DL);

    return BOS.str();
  }
};

ASTNameGenerator::ASTNameGenerator(ASTContext &Ctx)
    : Impl(std::make_unique<Implementation>(Ctx)) {}

ASTNameGenerator::~ASTNameGenerator() {}

bool ASTNameGenerator::writeName(const Decl *D, raw_ostream &OS) {
  return Impl->writeName(D, OS);
}

std::string ASTNameGenerator::getName(const Decl *D) {
  return Impl->getName(D);
}

std::vector<std::string> ASTNameGenerator::getAllManglings(const Decl *D) {
  return Impl->getAllManglings(D);
}
