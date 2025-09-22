//===- ExternalASTMerger.cpp - Merging External AST Interface ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ExternalASTMerger, which vends a combination of
//  ASTs from several different ASTContext/FileManager pairs
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExternalASTMerger.h"

using namespace clang;

namespace {

template <typename T> struct Source {
  T t;
  Source(T t) : t(t) {}
  operator T() { return t; }
  template <typename U = T> U &get() { return t; }
  template <typename U = T> const U &get() const { return t; }
  template <typename U> operator Source<U>() { return Source<U>(t); }
};

typedef std::pair<Source<NamedDecl *>, ASTImporter *> Candidate;

/// For the given DC, return the DC that is safe to perform lookups on.  This is
/// the DC we actually want to work with most of the time.
const DeclContext *CanonicalizeDC(const DeclContext *DC) {
  if (isa<LinkageSpecDecl>(DC))
    return DC->getRedeclContext();
  return DC;
}

Source<const DeclContext *>
LookupSameContext(Source<TranslationUnitDecl *> SourceTU, const DeclContext *DC,
                  ASTImporter &ReverseImporter) {
  DC = CanonicalizeDC(DC);
  if (DC->isTranslationUnit()) {
    return SourceTU;
  }
  Source<const DeclContext *> SourceParentDC =
      LookupSameContext(SourceTU, DC->getParent(), ReverseImporter);
  if (!SourceParentDC) {
    // If we couldn't find the parent DC in this TranslationUnit, give up.
    return nullptr;
  }
  auto *ND = cast<NamedDecl>(DC);
  DeclarationName Name = ND->getDeclName();
  auto SourceNameOrErr = ReverseImporter.Import(Name);
  if (!SourceNameOrErr) {
    llvm::consumeError(SourceNameOrErr.takeError());
    return nullptr;
  }
  Source<DeclarationName> SourceName = *SourceNameOrErr;
  DeclContext::lookup_result SearchResult =
      SourceParentDC.get()->lookup(SourceName.get());

  // There are two cases here. First, we might not find the name.
  // We might also find multiple copies, in which case we have no
  // guarantee that the one we wanted is the one we pick.  (E.g.,
  // if we have two specializations of the same template it is
  // very hard to determine which is the one you want.)
  //
  // The Origins map fixes this problem by allowing the origin to be
  // explicitly recorded, so we trigger that recording by returning
  // nothing (rather than a possibly-inaccurate guess) here.
  if (SearchResult.isSingleResult()) {
    NamedDecl *SearchResultDecl = SearchResult.front();
    if (isa<DeclContext>(SearchResultDecl) &&
        SearchResultDecl->getKind() == DC->getDeclKind())
      return cast<DeclContext>(SearchResultDecl)->getPrimaryContext();
    return nullptr; // This type of lookup is unsupported
  } else {
    return nullptr;
  }
}

/// A custom implementation of ASTImporter, for ExternalASTMerger's purposes.
///
/// There are several modifications:
///
/// - It enables lazy lookup (via the HasExternalLexicalStorage flag and a few
///   others), which instructs Clang to refer to ExternalASTMerger.  Also, it
///   forces MinimalImport to true, which is necessary to make this work.
/// - It maintains a reverse importer for use with names.  This allows lookup of
///   arbitrary names in the source context.
/// - It updates the ExternalASTMerger's origin map as needed whenever a
///   it sees a DeclContext.
class LazyASTImporter : public ASTImporter {
private:
  ExternalASTMerger &Parent;
  ASTImporter Reverse;
  const ExternalASTMerger::OriginMap &FromOrigins;
  /// @see ExternalASTMerger::ImporterSource::Temporary
  bool TemporarySource;
  /// Map of imported declarations back to the declarations they originated
  /// from.
  llvm::DenseMap<Decl *, Decl *> ToOrigin;
  /// @see ExternalASTMerger::ImporterSource::Merger
  ExternalASTMerger *SourceMerger;
  llvm::raw_ostream &logs() { return Parent.logs(); }
public:
  LazyASTImporter(ExternalASTMerger &_Parent, ASTContext &ToContext,
                  FileManager &ToFileManager,
                  const ExternalASTMerger::ImporterSource &S,
                  std::shared_ptr<ASTImporterSharedState> SharedState)
      : ASTImporter(ToContext, ToFileManager, S.getASTContext(),
                    S.getFileManager(),
                    /*MinimalImport=*/true, SharedState),
        Parent(_Parent),
        Reverse(S.getASTContext(), S.getFileManager(), ToContext, ToFileManager,
                /*MinimalImport=*/true),
        FromOrigins(S.getOriginMap()), TemporarySource(S.isTemporary()),
        SourceMerger(S.getMerger()) {}

  llvm::Expected<Decl *> ImportImpl(Decl *FromD) override {
    if (!TemporarySource || !SourceMerger)
      return ASTImporter::ImportImpl(FromD);

    // If we get here, then this source is importing from a temporary ASTContext
    // that also has another ExternalASTMerger attached. It could be
    // possible that the current ExternalASTMerger and the temporary ASTContext
    // share a common ImporterSource, which means that the temporary
    // AST could contain declarations that were imported from a source
    // that this ExternalASTMerger can access directly. Instead of importing
    // such declarations from the temporary ASTContext, they should instead
    // be directly imported by this ExternalASTMerger from the original
    // source. This way the ExternalASTMerger can safely do a minimal import
    // without creating incomplete declarations originated from a temporary
    // ASTContext. If we would try to complete such declarations later on, we
    // would fail to do so as their temporary AST could be deleted (which means
    // that the missing parts of the minimally imported declaration in that
    // ASTContext were also deleted).
    //
    // The following code tracks back any declaration that needs to be
    // imported from the temporary ASTContext to a persistent ASTContext.
    // Then the ExternalASTMerger tries to import from the persistent
    // ASTContext directly by using the associated ASTImporter. If that
    // succeeds, this ASTImporter just maps the declarations imported by
    // the other (persistent) ASTImporter to this (temporary) ASTImporter.
    // The steps can be visualized like this:
    //
    //  Target AST <--- 3. Indirect import --- Persistent AST
    //       ^            of persistent decl        ^
    //       |                                      |
    // 1. Current import           2. Tracking back to persistent decl
    // 4. Map persistent decl                       |
    //  & pretend we imported.                      |
    //       |                                      |
    // Temporary AST -------------------------------'

    // First, ask the ExternalASTMerger of the source where the temporary
    // declaration originated from.
    Decl *Persistent = SourceMerger->FindOriginalDecl(FromD);
    // FromD isn't from a persistent AST, so just do a normal import.
    if (!Persistent)
      return ASTImporter::ImportImpl(FromD);
    // Now ask the current ExternalASTMerger to try import the persistent
    // declaration into the target.
    ASTContext &PersistentCtx = Persistent->getASTContext();
    ASTImporter &OtherImporter = Parent.ImporterForOrigin(PersistentCtx);
    // Check that we never end up in the current Importer again.
    assert((&PersistentCtx != &getFromContext()) && (&OtherImporter != this) &&
           "Delegated to same Importer?");
    auto DeclOrErr = OtherImporter.Import(Persistent);
    // Errors when importing the persistent decl are treated as if we
    // had errors with importing the temporary decl.
    if (!DeclOrErr)
      return DeclOrErr.takeError();
    Decl *D = *DeclOrErr;
    // Tell the current ASTImporter that this has already been imported
    // to prevent any further queries for the temporary decl.
    MapImported(FromD, D);
    return D;
  }

  /// Implements the ASTImporter interface for tracking back a declaration
  /// to its original declaration it came from.
  Decl *GetOriginalDecl(Decl *To) override {
    return ToOrigin.lookup(To);
  }

  /// Whenever a DeclContext is imported, ensure that ExternalASTSource's origin
  /// map is kept up to date.  Also set the appropriate flags.
  void Imported(Decl *From, Decl *To) override {
    ToOrigin[To] = From;

    if (auto *ToDC = dyn_cast<DeclContext>(To)) {
      const bool LoggingEnabled = Parent.LoggingEnabled();
      if (LoggingEnabled)
        logs() << "(ExternalASTMerger*)" << (void*)&Parent
               << " imported (DeclContext*)" << (void*)ToDC
               << ", (ASTContext*)" << (void*)&getToContext()
               << " from (DeclContext*)" << (void*)llvm::cast<DeclContext>(From)
               << ", (ASTContext*)" << (void*)&getFromContext()
               << "\n";
      Source<DeclContext *> FromDC(
          cast<DeclContext>(From)->getPrimaryContext());
      if (FromOrigins.count(FromDC) &&
          Parent.HasImporterForOrigin(*FromOrigins.at(FromDC).AST)) {
        if (LoggingEnabled)
          logs() << "(ExternalASTMerger*)" << (void*)&Parent
                 << " forced origin (DeclContext*)"
                 << (void*)FromOrigins.at(FromDC).DC
                 << ", (ASTContext*)"
                 << (void*)FromOrigins.at(FromDC).AST
                 << "\n";
        Parent.ForceRecordOrigin(ToDC, FromOrigins.at(FromDC));
      } else {
        if (LoggingEnabled)
          logs() << "(ExternalASTMerger*)" << (void*)&Parent
                 << " maybe recording origin (DeclContext*)" << (void*)FromDC
                 << ", (ASTContext*)" << (void*)&getFromContext()
                 << "\n";
        Parent.MaybeRecordOrigin(ToDC, {FromDC, &getFromContext()});
      }
    }
    if (auto *ToTag = dyn_cast<TagDecl>(To)) {
      ToTag->setHasExternalLexicalStorage();
      ToTag->getPrimaryContext()->setMustBuildLookupTable();
      assert(Parent.CanComplete(ToTag));
    } else if (auto *ToNamespace = dyn_cast<NamespaceDecl>(To)) {
      ToNamespace->setHasExternalVisibleStorage();
      assert(Parent.CanComplete(ToNamespace));
    } else if (auto *ToContainer = dyn_cast<ObjCContainerDecl>(To)) {
      ToContainer->setHasExternalLexicalStorage();
      ToContainer->getPrimaryContext()->setMustBuildLookupTable();
      assert(Parent.CanComplete(ToContainer));
    }
  }
  ASTImporter &GetReverse() { return Reverse; }
};

bool HasDeclOfSameType(llvm::ArrayRef<Candidate> Decls, const Candidate &C) {
  if (isa<FunctionDecl>(C.first.get()))
    return false;
  return llvm::any_of(Decls, [&](const Candidate &D) {
    return C.first.get()->getKind() == D.first.get()->getKind();
  });
}

} // end namespace

ASTImporter &ExternalASTMerger::ImporterForOrigin(ASTContext &OriginContext) {
  for (const std::unique_ptr<ASTImporter> &I : Importers)
    if (&I->getFromContext() == &OriginContext)
      return *I;
  llvm_unreachable("We should have an importer for this origin!");
}

namespace {
LazyASTImporter &LazyImporterForOrigin(ExternalASTMerger &Merger,
                                   ASTContext &OriginContext) {
  return static_cast<LazyASTImporter &>(
      Merger.ImporterForOrigin(OriginContext));
}
}

bool ExternalASTMerger::HasImporterForOrigin(ASTContext &OriginContext) {
  for (const std::unique_ptr<ASTImporter> &I : Importers)
    if (&I->getFromContext() == &OriginContext)
      return true;
  return false;
}

template <typename CallbackType>
void ExternalASTMerger::ForEachMatchingDC(const DeclContext *DC,
                                          CallbackType Callback) {
  if (Origins.count(DC)) {
    ExternalASTMerger::DCOrigin Origin = Origins[DC];
    LazyASTImporter &Importer = LazyImporterForOrigin(*this, *Origin.AST);
    Callback(Importer, Importer.GetReverse(), Origin.DC);
  } else {
    bool DidCallback = false;
    for (const std::unique_ptr<ASTImporter> &Importer : Importers) {
      Source<TranslationUnitDecl *> SourceTU =
          Importer->getFromContext().getTranslationUnitDecl();
      ASTImporter &Reverse =
          static_cast<LazyASTImporter *>(Importer.get())->GetReverse();
      if (auto SourceDC = LookupSameContext(SourceTU, DC, Reverse)) {
        DidCallback = true;
        if (Callback(*Importer, Reverse, SourceDC))
          break;
      }
    }
    if (!DidCallback && LoggingEnabled())
      logs() << "(ExternalASTMerger*)" << (void*)this
             << " asserting for (DeclContext*)" << (const void*)DC
             << ", (ASTContext*)" << (void*)&Target.AST
             << "\n";
    assert(DidCallback && "Couldn't find a source context matching our DC");
  }
}

void ExternalASTMerger::CompleteType(TagDecl *Tag) {
  assert(Tag->hasExternalLexicalStorage());
  ForEachMatchingDC(Tag, [&](ASTImporter &Forward, ASTImporter &Reverse,
                             Source<const DeclContext *> SourceDC) -> bool {
    auto *SourceTag = const_cast<TagDecl *>(cast<TagDecl>(SourceDC.get()));
    if (SourceTag->hasExternalLexicalStorage())
      SourceTag->getASTContext().getExternalSource()->CompleteType(SourceTag);
    if (!SourceTag->getDefinition())
      return false;
    Forward.MapImported(SourceTag, Tag);
    if (llvm::Error Err = Forward.ImportDefinition(SourceTag))
      llvm::consumeError(std::move(Err));
    Tag->setCompleteDefinition(SourceTag->isCompleteDefinition());
    return true;
  });
}

void ExternalASTMerger::CompleteType(ObjCInterfaceDecl *Interface) {
  assert(Interface->hasExternalLexicalStorage());
  ForEachMatchingDC(
      Interface, [&](ASTImporter &Forward, ASTImporter &Reverse,
                     Source<const DeclContext *> SourceDC) -> bool {
        auto *SourceInterface = const_cast<ObjCInterfaceDecl *>(
            cast<ObjCInterfaceDecl>(SourceDC.get()));
        if (SourceInterface->hasExternalLexicalStorage())
          SourceInterface->getASTContext().getExternalSource()->CompleteType(
              SourceInterface);
        if (!SourceInterface->getDefinition())
          return false;
        Forward.MapImported(SourceInterface, Interface);
        if (llvm::Error Err = Forward.ImportDefinition(SourceInterface))
          llvm::consumeError(std::move(Err));
        return true;
      });
}

bool ExternalASTMerger::CanComplete(DeclContext *Interface) {
  assert(Interface->hasExternalLexicalStorage() ||
         Interface->hasExternalVisibleStorage());
  bool FoundMatchingDC = false;
  ForEachMatchingDC(Interface,
                    [&](ASTImporter &Forward, ASTImporter &Reverse,
                        Source<const DeclContext *> SourceDC) -> bool {
                      FoundMatchingDC = true;
                      return true;
                    });
  return FoundMatchingDC;
}

namespace {
bool IsSameDC(const DeclContext *D1, const DeclContext *D2) {
  if (isa<ObjCContainerDecl>(D1) && isa<ObjCContainerDecl>(D2))
    return true; // There are many cases where Objective-C is ambiguous.
  if (auto *T1 = dyn_cast<TagDecl>(D1))
    if (auto *T2 = dyn_cast<TagDecl>(D2))
      if (T1->getFirstDecl() == T2->getFirstDecl())
        return true;
  return D1 == D2 || D1 == CanonicalizeDC(D2);
}
}

void ExternalASTMerger::MaybeRecordOrigin(const DeclContext *ToDC,
                                          DCOrigin Origin) {
  LazyASTImporter &Importer = LazyImporterForOrigin(*this, *Origin.AST);
  ASTImporter &Reverse = Importer.GetReverse();
  Source<const DeclContext *> FoundFromDC =
      LookupSameContext(Origin.AST->getTranslationUnitDecl(), ToDC, Reverse);
  const bool DoRecord = !FoundFromDC || !IsSameDC(FoundFromDC.get(), Origin.DC);
  if (DoRecord)
    RecordOriginImpl(ToDC, Origin, Importer);
  if (LoggingEnabled())
    logs() << "(ExternalASTMerger*)" << (void*)this
             << (DoRecord ? " decided " : " decided NOT")
             << " to record origin (DeclContext*)" << (void*)Origin.DC
             << ", (ASTContext*)" << (void*)&Origin.AST
             << "\n";
}

void ExternalASTMerger::ForceRecordOrigin(const DeclContext *ToDC,
                                          DCOrigin Origin) {
  RecordOriginImpl(ToDC, Origin, ImporterForOrigin(*Origin.AST));
}

void ExternalASTMerger::RecordOriginImpl(const DeclContext *ToDC, DCOrigin Origin,
                                         ASTImporter &Importer) {
  Origins[ToDC] = Origin;
  Importer.ASTImporter::MapImported(cast<Decl>(Origin.DC), const_cast<Decl*>(cast<Decl>(ToDC)));
}

ExternalASTMerger::ExternalASTMerger(const ImporterTarget &Target,
                                     llvm::ArrayRef<ImporterSource> Sources) : LogStream(&llvm::nulls()), Target(Target) {
  SharedState = std::make_shared<ASTImporterSharedState>(
      *Target.AST.getTranslationUnitDecl());
  AddSources(Sources);
}

Decl *ExternalASTMerger::FindOriginalDecl(Decl *D) {
  assert(&D->getASTContext() == &Target.AST);
  for (const auto &I : Importers)
    if (auto Result = I->GetOriginalDecl(D))
      return Result;
  return nullptr;
}

void ExternalASTMerger::AddSources(llvm::ArrayRef<ImporterSource> Sources) {
  for (const ImporterSource &S : Sources) {
    assert(&S.getASTContext() != &Target.AST);
    // Check that the associated merger actually imports into the source AST.
    assert(!S.getMerger() || &S.getMerger()->Target.AST == &S.getASTContext());
    Importers.push_back(std::make_unique<LazyASTImporter>(
        *this, Target.AST, Target.FM, S, SharedState));
  }
}

void ExternalASTMerger::RemoveSources(llvm::ArrayRef<ImporterSource> Sources) {
  if (LoggingEnabled())
    for (const ImporterSource &S : Sources)
      logs() << "(ExternalASTMerger*)" << (void *)this
             << " removing source (ASTContext*)" << (void *)&S.getASTContext()
             << "\n";
  llvm::erase_if(Importers,
                 [&Sources](std::unique_ptr<ASTImporter> &Importer) -> bool {
                   for (const ImporterSource &S : Sources) {
                     if (&Importer->getFromContext() == &S.getASTContext())
                       return true;
                   }
                   return false;
                 });
  for (OriginMap::iterator OI = Origins.begin(), OE = Origins.end(); OI != OE; ) {
    std::pair<const DeclContext *, DCOrigin> Origin = *OI;
    bool Erase = false;
    for (const ImporterSource &S : Sources) {
      if (&S.getASTContext() == Origin.second.AST) {
        Erase = true;
        break;
      }
    }
    if (Erase)
      OI = Origins.erase(OI);
    else
      ++OI;
  }
}

template <typename DeclTy>
static bool importSpecializations(DeclTy *D, ASTImporter *Importer) {
  for (auto *Spec : D->specializations()) {
    auto ImportedSpecOrError = Importer->Import(Spec);
    if (!ImportedSpecOrError) {
      llvm::consumeError(ImportedSpecOrError.takeError());
      return true;
    }
  }
  return false;
}

/// Imports specializations from template declarations that can be specialized.
static bool importSpecializationsIfNeeded(Decl *D, ASTImporter *Importer) {
  if (!isa<TemplateDecl>(D))
    return false;
  if (auto *FunctionTD = dyn_cast<FunctionTemplateDecl>(D))
    return importSpecializations(FunctionTD, Importer);
  else if (auto *ClassTD = dyn_cast<ClassTemplateDecl>(D))
    return importSpecializations(ClassTD, Importer);
  else if (auto *VarTD = dyn_cast<VarTemplateDecl>(D))
    return importSpecializations(VarTD, Importer);
  return false;
}

bool ExternalASTMerger::FindExternalVisibleDeclsByName(const DeclContext *DC,
                                                       DeclarationName Name) {
  llvm::SmallVector<NamedDecl *, 1> Decls;
  llvm::SmallVector<Candidate, 4> Candidates;

  auto FilterFoundDecl = [&Candidates](const Candidate &C) {
   if (!HasDeclOfSameType(Candidates, C))
     Candidates.push_back(C);
  };

  ForEachMatchingDC(DC,
                    [&](ASTImporter &Forward, ASTImporter &Reverse,
                        Source<const DeclContext *> SourceDC) -> bool {
                      auto FromNameOrErr = Reverse.Import(Name);
                      if (!FromNameOrErr) {
                        llvm::consumeError(FromNameOrErr.takeError());
                        return false;
                      }
                      DeclContextLookupResult Result =
                          SourceDC.get()->lookup(*FromNameOrErr);
                      for (NamedDecl *FromD : Result) {
                        FilterFoundDecl(std::make_pair(FromD, &Forward));
                      }
                      return false;
                    });

  if (Candidates.empty())
    return false;

  Decls.reserve(Candidates.size());
  for (const Candidate &C : Candidates) {
    Decl *LookupRes = C.first.get();
    ASTImporter *Importer = C.second;
    auto NDOrErr = Importer->Import(LookupRes);
    NamedDecl *ND = cast<NamedDecl>(llvm::cantFail(std::move(NDOrErr)));
    assert(ND);
    // If we don't import specialization, they are not available via lookup
    // because the lookup result is imported TemplateDecl and it does not
    // reference its specializations until they are imported explicitly.
    bool IsSpecImportFailed =
        importSpecializationsIfNeeded(LookupRes, Importer);
    assert(!IsSpecImportFailed);
    (void)IsSpecImportFailed;
    Decls.push_back(ND);
  }
  SetExternalVisibleDeclsForName(DC, Name, Decls);
  return true;
}

void ExternalASTMerger::FindExternalLexicalDecls(
    const DeclContext *DC, llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
    SmallVectorImpl<Decl *> &Result) {
  ForEachMatchingDC(DC, [&](ASTImporter &Forward, ASTImporter &Reverse,
                            Source<const DeclContext *> SourceDC) -> bool {
    for (const Decl *SourceDecl : SourceDC.get()->decls()) {
      if (IsKindWeWant(SourceDecl->getKind())) {
        auto ImportedDeclOrErr = Forward.Import(SourceDecl);
        if (ImportedDeclOrErr)
          assert(!(*ImportedDeclOrErr) ||
                 IsSameDC((*ImportedDeclOrErr)->getDeclContext(), DC));
        else
          llvm::consumeError(ImportedDeclOrErr.takeError());
      }
    }
    return false;
  });
}
