//===--- SemaInternal.h - Internal Sema Interfaces --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides common API and #includes for the internal
// implementation of Sema.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAINTERNAL_H
#define LLVM_CLANG_SEMA_SEMAINTERNAL_H

#include "clang/AST/ASTContext.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"

namespace clang {

inline bool
FTIHasSingleVoidParameter(const DeclaratorChunk::FunctionTypeInfo &FTI) {
  return FTI.NumParams == 1 && !FTI.isVariadic &&
         FTI.Params[0].Ident == nullptr && FTI.Params[0].Param &&
         cast<ParmVarDecl>(FTI.Params[0].Param)->getType()->isVoidType();
}

inline bool
FTIHasNonVoidParameters(const DeclaratorChunk::FunctionTypeInfo &FTI) {
  // Assume FTI is well-formed.
  return FTI.NumParams && !FTIHasSingleVoidParameter(FTI);
}

// Helper function to check whether D's attributes match current CUDA mode.
// Decls with mismatched attributes and related diagnostics may have to be
// ignored during this CUDA compilation pass.
inline bool DeclAttrsMatchCUDAMode(const LangOptions &LangOpts, Decl *D) {
  if (!LangOpts.CUDA || !D)
    return true;
  bool isDeviceSideDecl = D->hasAttr<CUDADeviceAttr>() ||
                          D->hasAttr<CUDASharedAttr>() ||
                          D->hasAttr<CUDAGlobalAttr>();
  return isDeviceSideDecl == LangOpts.CUDAIsDevice;
}

/// Return a DLL attribute from the declaration.
inline InheritableAttr *getDLLAttr(Decl *D) {
  assert(!(D->hasAttr<DLLImportAttr>() && D->hasAttr<DLLExportAttr>()) &&
         "A declaration cannot be both dllimport and dllexport.");
  if (auto *Import = D->getAttr<DLLImportAttr>())
    return Import;
  if (auto *Export = D->getAttr<DLLExportAttr>())
    return Export;
  return nullptr;
}

/// Retrieve the depth and index of a template parameter.
inline std::pair<unsigned, unsigned> getDepthAndIndex(NamedDecl *ND) {
  if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(ND))
    return std::make_pair(TTP->getDepth(), TTP->getIndex());

  if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(ND))
    return std::make_pair(NTTP->getDepth(), NTTP->getIndex());

  const auto *TTP = cast<TemplateTemplateParmDecl>(ND);
  return std::make_pair(TTP->getDepth(), TTP->getIndex());
}

/// Retrieve the depth and index of an unexpanded parameter pack.
inline std::pair<unsigned, unsigned>
getDepthAndIndex(UnexpandedParameterPack UPP) {
  if (const auto *TTP = UPP.first.dyn_cast<const TemplateTypeParmType *>())
    return std::make_pair(TTP->getDepth(), TTP->getIndex());

  return getDepthAndIndex(UPP.first.get<NamedDecl *>());
}

class TypoCorrectionConsumer : public VisibleDeclConsumer {
  typedef SmallVector<TypoCorrection, 1> TypoResultList;
  typedef llvm::StringMap<TypoResultList> TypoResultsMap;
  typedef std::map<unsigned, TypoResultsMap> TypoEditDistanceMap;

public:
  TypoCorrectionConsumer(Sema &SemaRef,
                         const DeclarationNameInfo &TypoName,
                         Sema::LookupNameKind LookupKind,
                         Scope *S, CXXScopeSpec *SS,
                         std::unique_ptr<CorrectionCandidateCallback> CCC,
                         DeclContext *MemberContext,
                         bool EnteringContext)
      : Typo(TypoName.getName().getAsIdentifierInfo()), CurrentTCIndex(0),
        SavedTCIndex(0), SemaRef(SemaRef), S(S),
        SS(SS ? std::make_unique<CXXScopeSpec>(*SS) : nullptr),
        CorrectionValidator(std::move(CCC)), MemberContext(MemberContext),
        Result(SemaRef, TypoName, LookupKind),
        Namespaces(SemaRef.Context, SemaRef.CurContext, SS),
        EnteringContext(EnteringContext), SearchNamespaces(false) {
    Result.suppressDiagnostics();
    // Arrange for ValidatedCorrections[0] to always be an empty correction.
    ValidatedCorrections.push_back(TypoCorrection());
  }

  bool includeHiddenDecls() const override { return true; }

  // Methods for adding potential corrections to the consumer.
  void FoundDecl(NamedDecl *ND, NamedDecl *Hiding, DeclContext *Ctx,
                 bool InBaseClass) override;
  void FoundName(StringRef Name);
  void addKeywordResult(StringRef Keyword);
  void addCorrection(TypoCorrection Correction);

  bool empty() const {
    return CorrectionResults.empty() && ValidatedCorrections.size() == 1;
  }

  /// Return the list of TypoCorrections for the given identifier from
  /// the set of corrections that have the closest edit distance, if any.
  TypoResultList &operator[](StringRef Name) {
    return CorrectionResults.begin()->second[Name];
  }

  /// Return the edit distance of the corrections that have the
  /// closest/best edit distance from the original typop.
  unsigned getBestEditDistance(bool Normalized) {
    if (CorrectionResults.empty())
      return (std::numeric_limits<unsigned>::max)();

    unsigned BestED = CorrectionResults.begin()->first;
    return Normalized ? TypoCorrection::NormalizeEditDistance(BestED) : BestED;
  }

  /// Set-up method to add to the consumer the set of namespaces to use
  /// in performing corrections to nested name specifiers. This method also
  /// implicitly adds all of the known classes in the current AST context to the
  /// to the consumer for correcting nested name specifiers.
  void
  addNamespaces(const llvm::MapVector<NamespaceDecl *, bool> &KnownNamespaces);

  /// Return the next typo correction that passes all internal filters
  /// and is deemed valid by the consumer's CorrectionCandidateCallback,
  /// starting with the corrections that have the closest edit distance. An
  /// empty TypoCorrection is returned once no more viable corrections remain
  /// in the consumer.
  const TypoCorrection &getNextCorrection();

  /// Get the last correction returned by getNextCorrection().
  const TypoCorrection &getCurrentCorrection() {
    return CurrentTCIndex < ValidatedCorrections.size()
               ? ValidatedCorrections[CurrentTCIndex]
               : ValidatedCorrections[0];  // The empty correction.
  }

  /// Return the next typo correction like getNextCorrection, but keep
  /// the internal state pointed to the current correction (i.e. the next time
  /// getNextCorrection is called, it will return the same correction returned
  /// by peekNextcorrection).
  const TypoCorrection &peekNextCorrection() {
    auto Current = CurrentTCIndex;
    const TypoCorrection &TC = getNextCorrection();
    CurrentTCIndex = Current;
    return TC;
  }

  /// In the case of deeply invalid expressions, `getNextCorrection()` will
  /// never be called since the transform never makes progress. If we don't
  /// detect this we risk trying to correct typos forever.
  bool hasMadeAnyCorrectionProgress() const { return CurrentTCIndex != 0; }

  /// Reset the consumer's position in the stream of viable corrections
  /// (i.e. getNextCorrection() will return each of the previously returned
  /// corrections in order before returning any new corrections).
  void resetCorrectionStream() {
    CurrentTCIndex = 0;
  }

  /// Return whether the end of the stream of corrections has been
  /// reached.
  bool finished() {
    return CorrectionResults.empty() &&
           CurrentTCIndex >= ValidatedCorrections.size();
  }

  /// Save the current position in the correction stream (overwriting any
  /// previously saved position).
  void saveCurrentPosition() {
    SavedTCIndex = CurrentTCIndex;
  }

  /// Restore the saved position in the correction stream.
  void restoreSavedPosition() {
    CurrentTCIndex = SavedTCIndex;
  }

  ASTContext &getContext() const { return SemaRef.Context; }
  const LookupResult &getLookupResult() const { return Result; }

  bool isAddressOfOperand() const { return CorrectionValidator->IsAddressOfOperand; }
  const CXXScopeSpec *getSS() const { return SS.get(); }
  Scope *getScope() const { return S; }
  CorrectionCandidateCallback *getCorrectionValidator() const {
    return CorrectionValidator.get();
  }

private:
  class NamespaceSpecifierSet {
    struct SpecifierInfo {
      DeclContext* DeclCtx;
      NestedNameSpecifier* NameSpecifier;
      unsigned EditDistance;
    };

    typedef SmallVector<DeclContext*, 4> DeclContextList;
    typedef SmallVector<SpecifierInfo, 16> SpecifierInfoList;

    ASTContext &Context;
    DeclContextList CurContextChain;
    std::string CurNameSpecifier;
    SmallVector<const IdentifierInfo*, 4> CurContextIdentifiers;
    SmallVector<const IdentifierInfo*, 4> CurNameSpecifierIdentifiers;

    std::map<unsigned, SpecifierInfoList> DistanceMap;

    /// Helper for building the list of DeclContexts between the current
    /// context and the top of the translation unit
    static DeclContextList buildContextChain(DeclContext *Start);

    unsigned buildNestedNameSpecifier(DeclContextList &DeclChain,
                                      NestedNameSpecifier *&NNS);

   public:
    NamespaceSpecifierSet(ASTContext &Context, DeclContext *CurContext,
                          CXXScopeSpec *CurScopeSpec);

    /// Add the DeclContext (a namespace or record) to the set, computing
    /// the corresponding NestedNameSpecifier and its distance in the process.
    void addNameSpecifier(DeclContext *Ctx);

    /// Provides flat iteration over specifiers, sorted by distance.
    class iterator
        : public llvm::iterator_facade_base<iterator, std::forward_iterator_tag,
                                            SpecifierInfo> {
      /// Always points to the last element in the distance map.
      const std::map<unsigned, SpecifierInfoList>::iterator OuterBack;
      /// Iterator on the distance map.
      std::map<unsigned, SpecifierInfoList>::iterator Outer;
      /// Iterator on an element in the distance map.
      SpecifierInfoList::iterator Inner;

    public:
      iterator(NamespaceSpecifierSet &Set, bool IsAtEnd)
          : OuterBack(std::prev(Set.DistanceMap.end())),
            Outer(Set.DistanceMap.begin()),
            Inner(!IsAtEnd ? Outer->second.begin() : OuterBack->second.end()) {
        assert(!Set.DistanceMap.empty());
      }

      iterator &operator++() {
        ++Inner;
        if (Inner == Outer->second.end() && Outer != OuterBack) {
          ++Outer;
          Inner = Outer->second.begin();
        }
        return *this;
      }

      SpecifierInfo &operator*() { return *Inner; }
      bool operator==(const iterator &RHS) const { return Inner == RHS.Inner; }
    };

    iterator begin() { return iterator(*this, /*IsAtEnd=*/false); }
    iterator end() { return iterator(*this, /*IsAtEnd=*/true); }
  };

  void addName(StringRef Name, NamedDecl *ND,
               NestedNameSpecifier *NNS = nullptr, bool isKeyword = false);

  /// Find any visible decls for the given typo correction candidate.
  /// If none are found, it to the set of candidates for which qualified lookups
  /// will be performed to find possible nested name specifier changes.
  bool resolveCorrection(TypoCorrection &Candidate);

  /// Perform qualified lookups on the queued set of typo correction
  /// candidates and add the nested name specifier changes to each candidate if
  /// a lookup succeeds (at which point the candidate will be returned to the
  /// main pool of potential corrections).
  void performQualifiedLookups();

  /// The name written that is a typo in the source.
  IdentifierInfo *Typo;

  /// The results found that have the smallest edit distance
  /// found (so far) with the typo name.
  ///
  /// The pointer value being set to the current DeclContext indicates
  /// whether there is a keyword with this name.
  TypoEditDistanceMap CorrectionResults;

  SmallVector<TypoCorrection, 4> ValidatedCorrections;
  size_t CurrentTCIndex;
  size_t SavedTCIndex;

  Sema &SemaRef;
  Scope *S;
  std::unique_ptr<CXXScopeSpec> SS;
  std::unique_ptr<CorrectionCandidateCallback> CorrectionValidator;
  DeclContext *MemberContext;
  LookupResult Result;
  NamespaceSpecifierSet Namespaces;
  SmallVector<TypoCorrection, 2> QualifiedResults;
  bool EnteringContext;
  bool SearchNamespaces;
};

inline Sema::TypoExprState::TypoExprState() {}

inline Sema::TypoExprState::TypoExprState(TypoExprState &&other) noexcept {
  *this = std::move(other);
}

inline Sema::TypoExprState &Sema::TypoExprState::
operator=(Sema::TypoExprState &&other) noexcept {
  Consumer = std::move(other.Consumer);
  DiagHandler = std::move(other.DiagHandler);
  RecoveryHandler = std::move(other.RecoveryHandler);
  return *this;
}

} // end namespace clang

#endif
