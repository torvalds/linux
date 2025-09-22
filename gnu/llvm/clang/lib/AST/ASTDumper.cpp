//===--- ASTDumper.cpp - Dumping implementation for ASTs ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AST dump methods, which dump out the
// AST in a form that exposes type details and other fields.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTDumper.h"
#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclLookups.h"
#include "clang/AST/JSONNodeDumper.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::comments;

void ASTDumper::dumpInvalidDeclContext(const DeclContext *DC) {
  NodeDumper.AddChild([=] {
    if (!DC) {
      ColorScope Color(OS, ShowColors, NullColor);
      OS << "<<<NULL>>>";
      return;
    }
    // An invalid DeclContext is one for which a dyn_cast() from a DeclContext
    // pointer to a Decl pointer would fail an assertion or otherwise fall prey
    // to undefined behavior as a result of an invalid associated DeclKind.
    // Such invalidity is not supposed to happen of course, but, when it does,
    // the information provided below is intended to provide some hints about
    // what might have gone awry.
    {
      ColorScope Color(OS, ShowColors, DeclKindNameColor);
      OS << "DeclContext";
    }
    NodeDumper.dumpPointer(DC);
    OS << " <";
    {
      ColorScope Color(OS, ShowColors, DeclNameColor);
      OS << "unrecognized Decl kind " << (unsigned)DC->getDeclKind();
    }
    OS << ">";
  });
}

void ASTDumper::dumpLookups(const DeclContext *DC, bool DumpDecls) {
  NodeDumper.AddChild([=] {
    OS << "StoredDeclsMap ";
    NodeDumper.dumpBareDeclRef(cast<Decl>(DC));

    const DeclContext *Primary = DC->getPrimaryContext();
    if (Primary != DC) {
      OS << " primary";
      NodeDumper.dumpPointer(cast<Decl>(Primary));
    }

    bool HasUndeserializedLookups = Primary->hasExternalVisibleStorage();

    auto Range = getDeserialize()
                     ? Primary->lookups()
                     : Primary->noload_lookups(/*PreserveInternalState=*/true);
    for (auto I = Range.begin(), E = Range.end(); I != E; ++I) {
      DeclarationName Name = I.getLookupName();
      DeclContextLookupResult R = *I;

      NodeDumper.AddChild([=] {
        OS << "DeclarationName ";
        {
          ColorScope Color(OS, ShowColors, DeclNameColor);
          OS << '\'' << Name << '\'';
        }

        for (DeclContextLookupResult::iterator RI = R.begin(), RE = R.end();
             RI != RE; ++RI) {
          NodeDumper.AddChild([=] {
            NodeDumper.dumpBareDeclRef(*RI);

            if (!(*RI)->isUnconditionallyVisible())
              OS << " hidden";

            // If requested, dump the redecl chain for this lookup.
            if (DumpDecls) {
              // Dump earliest decl first.
              std::function<void(Decl *)> DumpWithPrev = [&](Decl *D) {
                if (Decl *Prev = D->getPreviousDecl())
                  DumpWithPrev(Prev);
                Visit(D);
              };
              DumpWithPrev(*RI);
            }
          });
        }
      });
    }

    if (HasUndeserializedLookups) {
      NodeDumper.AddChild([=] {
        ColorScope Color(OS, ShowColors, UndeserializedColor);
        OS << "<undeserialized lookups>";
      });
    }
  });
}

template <typename SpecializationDecl>
void ASTDumper::dumpTemplateDeclSpecialization(const SpecializationDecl *D,
                                               bool DumpExplicitInst,
                                               bool DumpRefOnly) {
  bool DumpedAny = false;
  for (const auto *RedeclWithBadType : D->redecls()) {
    // FIXME: The redecls() range sometimes has elements of a less-specific
    // type. (In particular, ClassTemplateSpecializationDecl::redecls() gives
    // us TagDecls, and should give CXXRecordDecls).
    auto *Redecl = cast<SpecializationDecl>(RedeclWithBadType);
    switch (Redecl->getTemplateSpecializationKind()) {
    case TSK_ExplicitInstantiationDeclaration:
    case TSK_ExplicitInstantiationDefinition:
      if (!DumpExplicitInst)
        break;
      [[fallthrough]];
    case TSK_Undeclared:
    case TSK_ImplicitInstantiation:
      if (DumpRefOnly)
        NodeDumper.dumpDeclRef(Redecl);
      else
        Visit(Redecl);
      DumpedAny = true;
      break;
    case TSK_ExplicitSpecialization:
      break;
    }
  }

  // Ensure we dump at least one decl for each specialization.
  if (!DumpedAny)
    NodeDumper.dumpDeclRef(D);
}

template <typename TemplateDecl>
void ASTDumper::dumpTemplateDecl(const TemplateDecl *D, bool DumpExplicitInst) {
  dumpTemplateParameters(D->getTemplateParameters());

  Visit(D->getTemplatedDecl());

  if (GetTraversalKind() == TK_AsIs) {
    for (const auto *Child : D->specializations())
      dumpTemplateDeclSpecialization(Child, DumpExplicitInst,
                                     !D->isCanonicalDecl());
  }
}

void ASTDumper::VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
  // FIXME: We don't add a declaration of a function template specialization
  // to its context when it's explicitly instantiated, so dump explicit
  // instantiations when we dump the template itself.
  dumpTemplateDecl(D, true);
}

void ASTDumper::VisitClassTemplateDecl(const ClassTemplateDecl *D) {
  dumpTemplateDecl(D, false);
}

void ASTDumper::VisitVarTemplateDecl(const VarTemplateDecl *D) {
  dumpTemplateDecl(D, false);
}

//===----------------------------------------------------------------------===//
// Type method implementations
//===----------------------------------------------------------------------===//

void QualType::dump(const char *msg) const {
  if (msg)
    llvm::errs() << msg << ": ";
  dump();
}

LLVM_DUMP_METHOD void QualType::dump() const {
  ASTDumper Dumper(llvm::errs(), /*ShowColors=*/false);
  Dumper.Visit(*this);
}

LLVM_DUMP_METHOD void QualType::dump(llvm::raw_ostream &OS,
                                     const ASTContext &Context) const {
  ASTDumper Dumper(OS, Context, Context.getDiagnostics().getShowColors());
  Dumper.Visit(*this);
}

LLVM_DUMP_METHOD void Type::dump() const { QualType(this, 0).dump(); }

LLVM_DUMP_METHOD void Type::dump(llvm::raw_ostream &OS,
                                 const ASTContext &Context) const {
  QualType(this, 0).dump(OS, Context);
}

//===----------------------------------------------------------------------===//
// TypeLoc method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void TypeLoc::dump() const {
  ASTDumper(llvm::errs(), /*ShowColors=*/false).Visit(*this);
}

LLVM_DUMP_METHOD void TypeLoc::dump(llvm::raw_ostream &OS,
                                    const ASTContext &Context) const {
  ASTDumper(OS, Context, Context.getDiagnostics().getShowColors()).Visit(*this);
}

//===----------------------------------------------------------------------===//
// Decl method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void Decl::dump() const { dump(llvm::errs()); }

LLVM_DUMP_METHOD void Decl::dump(raw_ostream &OS, bool Deserialize,
                                 ASTDumpOutputFormat Format) const {
  ASTContext &Ctx = getASTContext();
  const SourceManager &SM = Ctx.getSourceManager();

  if (ADOF_JSON == Format) {
    JSONDumper P(OS, SM, Ctx, Ctx.getPrintingPolicy(),
                 &Ctx.getCommentCommandTraits());
    (void)Deserialize; // FIXME?
    P.Visit(this);
  } else {
    ASTDumper P(OS, Ctx, Ctx.getDiagnostics().getShowColors());
    P.setDeserialize(Deserialize);
    P.Visit(this);
  }
}

LLVM_DUMP_METHOD void Decl::dumpColor() const {
  const ASTContext &Ctx = getASTContext();
  ASTDumper P(llvm::errs(), Ctx, /*ShowColors=*/true);
  P.Visit(this);
}

LLVM_DUMP_METHOD void DeclContext::dumpAsDecl() const {
  dumpAsDecl(nullptr);
}

LLVM_DUMP_METHOD void DeclContext::dumpAsDecl(const ASTContext *Ctx) const {
  // By design, DeclContext is required to be a base class of some class that
  // derives from Decl. Thus, it should always be possible to dyn_cast() from
  // a DeclContext pointer to a Decl pointer and Decl::castFromDeclContext()
  // asserts that to be the case. Since this function is intended for use in a
  // debugger, it performs an additional check in order to prevent a failed
  // cast and assertion. If that check fails, then the (invalid) DeclContext
  // is dumped with an indication of its invalidity.
  if (hasValidDeclKind()) {
    const auto *D = cast<Decl>(this);
    D->dump();
  } else {
    // If an ASTContext is not available, a less capable ASTDumper is
    // constructed for which color diagnostics are, regrettably, disabled.
    ASTDumper P = Ctx ? ASTDumper(llvm::errs(), *Ctx,
                                  Ctx->getDiagnostics().getShowColors())
                      : ASTDumper(llvm::errs(), /*ShowColors*/ false);
    P.dumpInvalidDeclContext(this);
  }
}

LLVM_DUMP_METHOD void DeclContext::dumpLookups() const {
  dumpLookups(llvm::errs());
}

LLVM_DUMP_METHOD void DeclContext::dumpLookups(raw_ostream &OS,
                                               bool DumpDecls,
                                               bool Deserialize) const {
  const DeclContext *DC = this;
  while (!DC->isTranslationUnit())
    DC = DC->getParent();
  const ASTContext &Ctx = cast<TranslationUnitDecl>(DC)->getASTContext();
  ASTDumper P(OS, Ctx, Ctx.getDiagnostics().getShowColors());
  P.setDeserialize(Deserialize);
  P.dumpLookups(this, DumpDecls);
}

//===----------------------------------------------------------------------===//
// Stmt method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void Stmt::dump() const {
  ASTDumper P(llvm::errs(), /*ShowColors=*/false);
  P.Visit(this);
}

LLVM_DUMP_METHOD void Stmt::dump(raw_ostream &OS,
                                 const ASTContext &Context) const {
  ASTDumper P(OS, Context, Context.getDiagnostics().getShowColors());
  P.Visit(this);
}

LLVM_DUMP_METHOD void Stmt::dumpColor() const {
  ASTDumper P(llvm::errs(), /*ShowColors=*/true);
  P.Visit(this);
}

//===----------------------------------------------------------------------===//
// Comment method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void Comment::dump() const {
  const auto *FC = dyn_cast<FullComment>(this);
  if (!FC)
    return;
  ASTDumper Dumper(llvm::errs(), /*ShowColors=*/false);
  Dumper.Visit(FC, FC);
}

LLVM_DUMP_METHOD void Comment::dump(raw_ostream &OS,
                                    const ASTContext &Context) const {
  const auto *FC = dyn_cast<FullComment>(this);
  if (!FC)
    return;
  ASTDumper Dumper(OS, Context, Context.getDiagnostics().getShowColors());
  Dumper.Visit(FC, FC);
}

LLVM_DUMP_METHOD void Comment::dumpColor() const {
  const auto *FC = dyn_cast<FullComment>(this);
  if (!FC)
    return;
  ASTDumper Dumper(llvm::errs(), /*ShowColors=*/true);
  Dumper.Visit(FC, FC);
}

//===----------------------------------------------------------------------===//
// APValue method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void APValue::dump() const {
  ASTDumper Dumper(llvm::errs(), /*ShowColors=*/false);
  Dumper.Visit(*this, /*Ty=*/QualType());
}

LLVM_DUMP_METHOD void APValue::dump(raw_ostream &OS,
                                    const ASTContext &Context) const {
  ASTDumper Dumper(OS, Context, Context.getDiagnostics().getShowColors());
  Dumper.Visit(*this, /*Ty=*/Context.getPointerType(Context.CharTy));
}

//===----------------------------------------------------------------------===//
// ConceptReference method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void ConceptReference::dump() const {
  dump(llvm::errs());
}

LLVM_DUMP_METHOD void ConceptReference::dump(raw_ostream &OS) const {
  auto &Ctx = getNamedConcept()->getASTContext();
  ASTDumper P(OS, Ctx, Ctx.getDiagnostics().getShowColors());
  P.Visit(this);
}

//===----------------------------------------------------------------------===//
// TemplateName method implementations
//===----------------------------------------------------------------------===//

// FIXME: These are actually using the TemplateArgument dumper, through
// an implicit conversion. The dump will claim this is a template argument,
// which is misleading.

LLVM_DUMP_METHOD void TemplateName::dump() const {
  ASTDumper Dumper(llvm::errs(), /*ShowColors=*/false);
  Dumper.Visit(*this);
}

LLVM_DUMP_METHOD void TemplateName::dump(llvm::raw_ostream &OS,
                                         const ASTContext &Context) const {
  ASTDumper Dumper(OS, Context, Context.getDiagnostics().getShowColors());
  Dumper.Visit(*this);
}

//===----------------------------------------------------------------------===//
// TemplateArgument method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void TemplateArgument::dump() const {
  ASTDumper Dumper(llvm::errs(), /*ShowColors=*/false);
  Dumper.Visit(*this);
}

LLVM_DUMP_METHOD void TemplateArgument::dump(llvm::raw_ostream &OS,
                                             const ASTContext &Context) const {
  ASTDumper Dumper(OS, Context, Context.getDiagnostics().getShowColors());
  Dumper.Visit(*this);
}
