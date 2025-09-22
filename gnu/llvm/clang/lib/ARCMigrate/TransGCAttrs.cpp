//===--- TransGCAttrs.cpp - Transformations to ARC mode -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

/// Collects all the places where GC attributes __strong/__weak occur.
class GCAttrsCollector : public RecursiveASTVisitor<GCAttrsCollector> {
  MigrationContext &MigrateCtx;
  bool FullyMigratable;
  std::vector<ObjCPropertyDecl *> &AllProps;

  typedef RecursiveASTVisitor<GCAttrsCollector> base;
public:
  GCAttrsCollector(MigrationContext &ctx,
                   std::vector<ObjCPropertyDecl *> &AllProps)
    : MigrateCtx(ctx), FullyMigratable(false),
      AllProps(AllProps) { }

  bool shouldWalkTypesOfTypeLocs() const { return false; }

  bool VisitAttributedTypeLoc(AttributedTypeLoc TL) {
    handleAttr(TL);
    return true;
  }

  bool TraverseDecl(Decl *D) {
    if (!D || D->isImplicit())
      return true;

    SaveAndRestore Save(FullyMigratable, isMigratable(D));

    if (ObjCPropertyDecl *PropD = dyn_cast<ObjCPropertyDecl>(D)) {
      lookForAttribute(PropD, PropD->getTypeSourceInfo());
      AllProps.push_back(PropD);
    } else if (DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
      lookForAttribute(DD, DD->getTypeSourceInfo());
    }
    return base::TraverseDecl(D);
  }

  void lookForAttribute(Decl *D, TypeSourceInfo *TInfo) {
    if (!TInfo)
      return;
    TypeLoc TL = TInfo->getTypeLoc();
    while (TL) {
      if (QualifiedTypeLoc QL = TL.getAs<QualifiedTypeLoc>()) {
        TL = QL.getUnqualifiedLoc();
      } else if (AttributedTypeLoc Attr = TL.getAs<AttributedTypeLoc>()) {
        if (handleAttr(Attr, D))
          break;
        TL = Attr.getModifiedLoc();
      } else if (MacroQualifiedTypeLoc MDTL =
                     TL.getAs<MacroQualifiedTypeLoc>()) {
        TL = MDTL.getInnerLoc();
      } else if (ArrayTypeLoc Arr = TL.getAs<ArrayTypeLoc>()) {
        TL = Arr.getElementLoc();
      } else if (PointerTypeLoc PT = TL.getAs<PointerTypeLoc>()) {
        TL = PT.getPointeeLoc();
      } else if (ReferenceTypeLoc RT = TL.getAs<ReferenceTypeLoc>())
        TL = RT.getPointeeLoc();
      else
        break;
    }
  }

  bool handleAttr(AttributedTypeLoc TL, Decl *D = nullptr) {
    auto *OwnershipAttr = TL.getAttrAs<ObjCOwnershipAttr>();
    if (!OwnershipAttr)
      return false;

    SourceLocation Loc = OwnershipAttr->getLocation();
    SourceLocation OrigLoc = Loc;
    if (MigrateCtx.AttrSet.count(OrigLoc))
      return true;

    ASTContext &Ctx = MigrateCtx.Pass.Ctx;
    SourceManager &SM = Ctx.getSourceManager();
    if (Loc.isMacroID())
      Loc = SM.getImmediateExpansionRange(Loc).getBegin();
    StringRef Spell = OwnershipAttr->getKind()->getName();
    MigrationContext::GCAttrOccurrence::AttrKind Kind;
    if (Spell == "strong")
      Kind = MigrationContext::GCAttrOccurrence::Strong;
    else if (Spell == "weak")
      Kind = MigrationContext::GCAttrOccurrence::Weak;
    else
      return false;

    MigrateCtx.AttrSet.insert(OrigLoc);
    MigrateCtx.GCAttrs.push_back(MigrationContext::GCAttrOccurrence());
    MigrationContext::GCAttrOccurrence &Attr = MigrateCtx.GCAttrs.back();

    Attr.Kind = Kind;
    Attr.Loc = Loc;
    Attr.ModifiedType = TL.getModifiedLoc().getType();
    Attr.Dcl = D;
    Attr.FullyMigratable = FullyMigratable;
    return true;
  }

  bool isMigratable(Decl *D) {
    if (isa<TranslationUnitDecl>(D))
      return false;

    if (isInMainFile(D))
      return true;

    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
      return FD->hasBody();

    if (ObjCContainerDecl *ContD = dyn_cast<ObjCContainerDecl>(D))
      return hasObjCImpl(ContD);

    if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D)) {
      for (const auto *MI : RD->methods()) {
        if (MI->isOutOfLine())
          return true;
      }
      return false;
    }

    return isMigratable(cast<Decl>(D->getDeclContext()));
  }

  static bool hasObjCImpl(Decl *D) {
    if (!D)
      return false;
    if (ObjCContainerDecl *ContD = dyn_cast<ObjCContainerDecl>(D)) {
      if (ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(ContD))
        return ID->getImplementation() != nullptr;
      if (ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(ContD))
        return CD->getImplementation() != nullptr;
      return isa<ObjCImplDecl>(ContD);
    }
    return false;
  }

  bool isInMainFile(Decl *D) {
    if (!D)
      return false;

    for (auto *I : D->redecls())
      if (!isInMainFile(I->getLocation()))
        return false;

    return true;
  }

  bool isInMainFile(SourceLocation Loc) {
    if (Loc.isInvalid())
      return false;

    SourceManager &SM = MigrateCtx.Pass.Ctx.getSourceManager();
    return SM.isInFileID(SM.getExpansionLoc(Loc), SM.getMainFileID());
  }
};

} // anonymous namespace

static void errorForGCAttrsOnNonObjC(MigrationContext &MigrateCtx) {
  TransformActions &TA = MigrateCtx.Pass.TA;

  for (unsigned i = 0, e = MigrateCtx.GCAttrs.size(); i != e; ++i) {
    MigrationContext::GCAttrOccurrence &Attr = MigrateCtx.GCAttrs[i];
    if (Attr.FullyMigratable && Attr.Dcl) {
      if (Attr.ModifiedType.isNull())
        continue;
      if (!Attr.ModifiedType->isObjCRetainableType()) {
        TA.reportError("GC managed memory will become unmanaged in ARC",
                       Attr.Loc);
      }
    }
  }
}

static void checkWeakGCAttrs(MigrationContext &MigrateCtx) {
  TransformActions &TA = MigrateCtx.Pass.TA;

  for (unsigned i = 0, e = MigrateCtx.GCAttrs.size(); i != e; ++i) {
    MigrationContext::GCAttrOccurrence &Attr = MigrateCtx.GCAttrs[i];
    if (Attr.Kind == MigrationContext::GCAttrOccurrence::Weak) {
      if (Attr.ModifiedType.isNull() ||
          !Attr.ModifiedType->isObjCRetainableType())
        continue;
      if (!canApplyWeak(MigrateCtx.Pass.Ctx, Attr.ModifiedType,
                        /*AllowOnUnknownClass=*/true)) {
        Transaction Trans(TA);
        if (!MigrateCtx.RemovedAttrSet.count(Attr.Loc))
          TA.replaceText(Attr.Loc, "__weak", "__unsafe_unretained");
        TA.clearDiagnostic(diag::err_arc_weak_no_runtime,
                           diag::err_arc_unsupported_weak_class,
                           Attr.Loc);
      }
    }
  }
}

typedef llvm::TinyPtrVector<ObjCPropertyDecl *> IndivPropsTy;

static void checkAllAtProps(MigrationContext &MigrateCtx,
                            SourceLocation AtLoc,
                            IndivPropsTy &IndProps) {
  if (IndProps.empty())
    return;

  for (IndivPropsTy::iterator
         PI = IndProps.begin(), PE = IndProps.end(); PI != PE; ++PI) {
    QualType T = (*PI)->getType();
    if (T.isNull() || !T->isObjCRetainableType())
      return;
  }

  SmallVector<std::pair<AttributedTypeLoc, ObjCPropertyDecl *>, 4> ATLs;
  bool hasWeak = false, hasStrong = false;
  ObjCPropertyAttribute::Kind Attrs = ObjCPropertyAttribute::kind_noattr;
  for (IndivPropsTy::iterator
         PI = IndProps.begin(), PE = IndProps.end(); PI != PE; ++PI) {
    ObjCPropertyDecl *PD = *PI;
    Attrs = PD->getPropertyAttributesAsWritten();
    TypeSourceInfo *TInfo = PD->getTypeSourceInfo();
    if (!TInfo)
      return;
    TypeLoc TL = TInfo->getTypeLoc();
    if (AttributedTypeLoc ATL =
            TL.getAs<AttributedTypeLoc>()) {
      ATLs.push_back(std::make_pair(ATL, PD));
      if (TInfo->getType().getObjCLifetime() == Qualifiers::OCL_Weak) {
        hasWeak = true;
      } else if (TInfo->getType().getObjCLifetime() == Qualifiers::OCL_Strong)
        hasStrong = true;
      else
        return;
    }
  }
  if (ATLs.empty())
    return;
  if (hasWeak && hasStrong)
    return;

  TransformActions &TA = MigrateCtx.Pass.TA;
  Transaction Trans(TA);

  if (GCAttrsCollector::hasObjCImpl(
                              cast<Decl>(IndProps.front()->getDeclContext()))) {
    if (hasWeak)
      MigrateCtx.AtPropsWeak.insert(AtLoc);

  } else {
    StringRef toAttr = "strong";
    if (hasWeak) {
      if (canApplyWeak(MigrateCtx.Pass.Ctx, IndProps.front()->getType(),
                       /*AllowOnUnknownClass=*/true))
        toAttr = "weak";
      else
        toAttr = "unsafe_unretained";
    }
    if (Attrs & ObjCPropertyAttribute::kind_assign)
      MigrateCtx.rewritePropertyAttribute("assign", toAttr, AtLoc);
    else
      MigrateCtx.addPropertyAttribute(toAttr, AtLoc);
  }

  for (unsigned i = 0, e = ATLs.size(); i != e; ++i) {
    SourceLocation Loc = ATLs[i].first.getAttr()->getLocation();
    if (Loc.isMacroID())
      Loc = MigrateCtx.Pass.Ctx.getSourceManager()
                .getImmediateExpansionRange(Loc)
                .getBegin();
    TA.remove(Loc);
    TA.clearDiagnostic(diag::err_objc_property_attr_mutually_exclusive, AtLoc);
    TA.clearDiagnostic(diag::err_arc_inconsistent_property_ownership,
                       ATLs[i].second->getLocation());
    MigrateCtx.RemovedAttrSet.insert(Loc);
  }
}

static void checkAllProps(MigrationContext &MigrateCtx,
                          std::vector<ObjCPropertyDecl *> &AllProps) {
  typedef llvm::TinyPtrVector<ObjCPropertyDecl *> IndivPropsTy;
  llvm::DenseMap<SourceLocation, IndivPropsTy> AtProps;

  for (unsigned i = 0, e = AllProps.size(); i != e; ++i) {
    ObjCPropertyDecl *PD = AllProps[i];
    if (PD->getPropertyAttributesAsWritten() &
        (ObjCPropertyAttribute::kind_assign |
         ObjCPropertyAttribute::kind_readonly)) {
      SourceLocation AtLoc = PD->getAtLoc();
      if (AtLoc.isInvalid())
        continue;
      AtProps[AtLoc].push_back(PD);
    }
  }

  for (auto I = AtProps.begin(), E = AtProps.end(); I != E; ++I) {
    SourceLocation AtLoc = I->first;
    IndivPropsTy &IndProps = I->second;
    checkAllAtProps(MigrateCtx, AtLoc, IndProps);
  }
}

void GCAttrsTraverser::traverseTU(MigrationContext &MigrateCtx) {
  std::vector<ObjCPropertyDecl *> AllProps;
  GCAttrsCollector(MigrateCtx, AllProps).TraverseDecl(
                                  MigrateCtx.Pass.Ctx.getTranslationUnitDecl());

  errorForGCAttrsOnNonObjC(MigrateCtx);
  checkAllProps(MigrateCtx, AllProps);
  checkWeakGCAttrs(MigrateCtx);
}

void MigrationContext::dumpGCAttrs() {
  llvm::errs() << "\n################\n";
  for (unsigned i = 0, e = GCAttrs.size(); i != e; ++i) {
    GCAttrOccurrence &Attr = GCAttrs[i];
    llvm::errs() << "KIND: "
        << (Attr.Kind == GCAttrOccurrence::Strong ? "strong" : "weak");
    llvm::errs() << "\nLOC: ";
    Attr.Loc.print(llvm::errs(), Pass.Ctx.getSourceManager());
    llvm::errs() << "\nTYPE: ";
    Attr.ModifiedType.dump();
    if (Attr.Dcl) {
      llvm::errs() << "DECL:\n";
      Attr.Dcl->dump();
    } else {
      llvm::errs() << "DECL: NONE";
    }
    llvm::errs() << "\nMIGRATABLE: " << Attr.FullyMigratable;
    llvm::errs() << "\n----------------\n";
  }
  llvm::errs() << "\n################\n";
}
