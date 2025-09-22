//===--- ItaniumMangle.cpp - Itanium C++ Name Mangling ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements C++ name mangling according to the Itanium C++ ABI,
// which is used in GCC 3.2 and newer (and many compilers that are
// ABI-compatible with GCC):
//
//   http://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/ABI.h"
#include "clang/Basic/DiagnosticAST.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Thunk.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/RISCVTargetParser.h"
#include <optional>

using namespace clang;

namespace {

static bool isLocalContainerContext(const DeclContext *DC) {
  return isa<FunctionDecl>(DC) || isa<ObjCMethodDecl>(DC) || isa<BlockDecl>(DC);
}

static const FunctionDecl *getStructor(const FunctionDecl *fn) {
  if (const FunctionTemplateDecl *ftd = fn->getPrimaryTemplate())
    return ftd->getTemplatedDecl();

  return fn;
}

static const NamedDecl *getStructor(const NamedDecl *decl) {
  const FunctionDecl *fn = dyn_cast_or_null<FunctionDecl>(decl);
  return (fn ? getStructor(fn) : decl);
}

static bool isLambda(const NamedDecl *ND) {
  const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(ND);
  if (!Record)
    return false;

  return Record->isLambda();
}

static const unsigned UnknownArity = ~0U;

class ItaniumMangleContextImpl : public ItaniumMangleContext {
  typedef std::pair<const DeclContext*, IdentifierInfo*> DiscriminatorKeyTy;
  llvm::DenseMap<DiscriminatorKeyTy, unsigned> Discriminator;
  llvm::DenseMap<const NamedDecl*, unsigned> Uniquifier;
  const DiscriminatorOverrideTy DiscriminatorOverride = nullptr;
  NamespaceDecl *StdNamespace = nullptr;

  bool NeedsUniqueInternalLinkageNames = false;

public:
  explicit ItaniumMangleContextImpl(
      ASTContext &Context, DiagnosticsEngine &Diags,
      DiscriminatorOverrideTy DiscriminatorOverride, bool IsAux = false)
      : ItaniumMangleContext(Context, Diags, IsAux),
        DiscriminatorOverride(DiscriminatorOverride) {}

  /// @name Mangler Entry Points
  /// @{

  bool shouldMangleCXXName(const NamedDecl *D) override;
  bool shouldMangleStringLiteral(const StringLiteral *) override {
    return false;
  }

  bool isUniqueInternalLinkageDecl(const NamedDecl *ND) override;
  void needsUniqueInternalLinkageNames() override {
    NeedsUniqueInternalLinkageNames = true;
  }

  void mangleCXXName(GlobalDecl GD, raw_ostream &) override;
  void mangleThunk(const CXXMethodDecl *MD, const ThunkInfo &Thunk, bool,
                   raw_ostream &) override;
  void mangleCXXDtorThunk(const CXXDestructorDecl *DD, CXXDtorType Type,
                          const ThunkInfo &Thunk, bool, raw_ostream &) override;
  void mangleReferenceTemporary(const VarDecl *D, unsigned ManglingNumber,
                                raw_ostream &) override;
  void mangleCXXVTable(const CXXRecordDecl *RD, raw_ostream &) override;
  void mangleCXXVTT(const CXXRecordDecl *RD, raw_ostream &) override;
  void mangleCXXCtorVTable(const CXXRecordDecl *RD, int64_t Offset,
                           const CXXRecordDecl *Type, raw_ostream &) override;
  void mangleCXXRTTI(QualType T, raw_ostream &) override;
  void mangleCXXRTTIName(QualType T, raw_ostream &,
                         bool NormalizeIntegers) override;
  void mangleCanonicalTypeName(QualType T, raw_ostream &,
                               bool NormalizeIntegers) override;

  void mangleCXXCtorComdat(const CXXConstructorDecl *D, raw_ostream &) override;
  void mangleCXXDtorComdat(const CXXDestructorDecl *D, raw_ostream &) override;
  void mangleStaticGuardVariable(const VarDecl *D, raw_ostream &) override;
  void mangleDynamicInitializer(const VarDecl *D, raw_ostream &Out) override;
  void mangleDynamicAtExitDestructor(const VarDecl *D,
                                     raw_ostream &Out) override;
  void mangleDynamicStermFinalizer(const VarDecl *D, raw_ostream &Out) override;
  void mangleSEHFilterExpression(GlobalDecl EnclosingDecl,
                                 raw_ostream &Out) override;
  void mangleSEHFinallyBlock(GlobalDecl EnclosingDecl,
                             raw_ostream &Out) override;
  void mangleItaniumThreadLocalInit(const VarDecl *D, raw_ostream &) override;
  void mangleItaniumThreadLocalWrapper(const VarDecl *D,
                                       raw_ostream &) override;

  void mangleStringLiteral(const StringLiteral *, raw_ostream &) override;

  void mangleLambdaSig(const CXXRecordDecl *Lambda, raw_ostream &) override;

  void mangleModuleInitializer(const Module *Module, raw_ostream &) override;

  bool getNextDiscriminator(const NamedDecl *ND, unsigned &disc) {
    // Lambda closure types are already numbered.
    if (isLambda(ND))
      return false;

    // Anonymous tags are already numbered.
    if (const TagDecl *Tag = dyn_cast<TagDecl>(ND)) {
      if (Tag->getName().empty() && !Tag->getTypedefNameForAnonDecl())
        return false;
    }

    // Use the canonical number for externally visible decls.
    if (ND->isExternallyVisible()) {
      unsigned discriminator = getASTContext().getManglingNumber(ND, isAux());
      if (discriminator == 1)
        return false;
      disc = discriminator - 2;
      return true;
    }

    // Make up a reasonable number for internal decls.
    unsigned &discriminator = Uniquifier[ND];
    if (!discriminator) {
      const DeclContext *DC = getEffectiveDeclContext(ND);
      discriminator = ++Discriminator[std::make_pair(DC, ND->getIdentifier())];
    }
    if (discriminator == 1)
      return false;
    disc = discriminator-2;
    return true;
  }

  std::string getLambdaString(const CXXRecordDecl *Lambda) override {
    // This function matches the one in MicrosoftMangle, which returns
    // the string that is used in lambda mangled names.
    assert(Lambda->isLambda() && "RD must be a lambda!");
    std::string Name("<lambda");
    Decl *LambdaContextDecl = Lambda->getLambdaContextDecl();
    unsigned LambdaManglingNumber = Lambda->getLambdaManglingNumber();
    unsigned LambdaId;
    const ParmVarDecl *Parm = dyn_cast_or_null<ParmVarDecl>(LambdaContextDecl);
    const FunctionDecl *Func =
        Parm ? dyn_cast<FunctionDecl>(Parm->getDeclContext()) : nullptr;

    if (Func) {
      unsigned DefaultArgNo =
          Func->getNumParams() - Parm->getFunctionScopeIndex();
      Name += llvm::utostr(DefaultArgNo);
      Name += "_";
    }

    if (LambdaManglingNumber)
      LambdaId = LambdaManglingNumber;
    else
      LambdaId = getAnonymousStructIdForDebugInfo(Lambda);

    Name += llvm::utostr(LambdaId);
    Name += '>';
    return Name;
  }

  DiscriminatorOverrideTy getDiscriminatorOverride() const override {
    return DiscriminatorOverride;
  }

  NamespaceDecl *getStdNamespace();

  const DeclContext *getEffectiveDeclContext(const Decl *D);
  const DeclContext *getEffectiveParentContext(const DeclContext *DC) {
    return getEffectiveDeclContext(cast<Decl>(DC));
  }

  bool isInternalLinkageDecl(const NamedDecl *ND);

  /// @}
};

/// Manage the mangling of a single name.
class CXXNameMangler {
  ItaniumMangleContextImpl &Context;
  raw_ostream &Out;
  /// Normalize integer types for cross-language CFI support with other
  /// languages that can't represent and encode C/C++ integer types.
  bool NormalizeIntegers = false;

  bool NullOut = false;
  /// In the "DisableDerivedAbiTags" mode derived ABI tags are not calculated.
  /// This mode is used when mangler creates another mangler recursively to
  /// calculate ABI tags for the function return value or the variable type.
  /// Also it is required to avoid infinite recursion in some cases.
  bool DisableDerivedAbiTags = false;

  /// The "structor" is the top-level declaration being mangled, if
  /// that's not a template specialization; otherwise it's the pattern
  /// for that specialization.
  const NamedDecl *Structor;
  unsigned StructorType = 0;

  // An offset to add to all template parameter depths while mangling. Used
  // when mangling a template parameter list to see if it matches a template
  // template parameter exactly.
  unsigned TemplateDepthOffset = 0;

  /// The next substitution sequence number.
  unsigned SeqID = 0;

  class FunctionTypeDepthState {
    unsigned Bits = 0;

    enum { InResultTypeMask = 1 };

  public:
    FunctionTypeDepthState() = default;

    /// The number of function types we're inside.
    unsigned getDepth() const {
      return Bits >> 1;
    }

    /// True if we're in the return type of the innermost function type.
    bool isInResultType() const {
      return Bits & InResultTypeMask;
    }

    FunctionTypeDepthState push() {
      FunctionTypeDepthState tmp = *this;
      Bits = (Bits & ~InResultTypeMask) + 2;
      return tmp;
    }

    void enterResultType() {
      Bits |= InResultTypeMask;
    }

    void leaveResultType() {
      Bits &= ~InResultTypeMask;
    }

    void pop(FunctionTypeDepthState saved) {
      assert(getDepth() == saved.getDepth() + 1);
      Bits = saved.Bits;
    }

  } FunctionTypeDepth;

  // abi_tag is a gcc attribute, taking one or more strings called "tags".
  // The goal is to annotate against which version of a library an object was
  // built and to be able to provide backwards compatibility ("dual abi").
  // For more information see docs/ItaniumMangleAbiTags.rst.
  typedef SmallVector<StringRef, 4> AbiTagList;

  // State to gather all implicit and explicit tags used in a mangled name.
  // Must always have an instance of this while emitting any name to keep
  // track.
  class AbiTagState final {
  public:
    explicit AbiTagState(AbiTagState *&Head) : LinkHead(Head) {
      Parent = LinkHead;
      LinkHead = this;
    }

    // No copy, no move.
    AbiTagState(const AbiTagState &) = delete;
    AbiTagState &operator=(const AbiTagState &) = delete;

    ~AbiTagState() { pop(); }

    void write(raw_ostream &Out, const NamedDecl *ND,
               const AbiTagList *AdditionalAbiTags) {
      ND = cast<NamedDecl>(ND->getCanonicalDecl());
      if (!isa<FunctionDecl>(ND) && !isa<VarDecl>(ND)) {
        assert(
            !AdditionalAbiTags &&
            "only function and variables need a list of additional abi tags");
        if (const auto *NS = dyn_cast<NamespaceDecl>(ND)) {
          if (const auto *AbiTag = NS->getAttr<AbiTagAttr>()) {
            UsedAbiTags.insert(UsedAbiTags.end(), AbiTag->tags().begin(),
                               AbiTag->tags().end());
          }
          // Don't emit abi tags for namespaces.
          return;
        }
      }

      AbiTagList TagList;
      if (const auto *AbiTag = ND->getAttr<AbiTagAttr>()) {
        UsedAbiTags.insert(UsedAbiTags.end(), AbiTag->tags().begin(),
                           AbiTag->tags().end());
        TagList.insert(TagList.end(), AbiTag->tags().begin(),
                       AbiTag->tags().end());
      }

      if (AdditionalAbiTags) {
        UsedAbiTags.insert(UsedAbiTags.end(), AdditionalAbiTags->begin(),
                           AdditionalAbiTags->end());
        TagList.insert(TagList.end(), AdditionalAbiTags->begin(),
                       AdditionalAbiTags->end());
      }

      llvm::sort(TagList);
      TagList.erase(std::unique(TagList.begin(), TagList.end()), TagList.end());

      writeSortedUniqueAbiTags(Out, TagList);
    }

    const AbiTagList &getUsedAbiTags() const { return UsedAbiTags; }
    void setUsedAbiTags(const AbiTagList &AbiTags) {
      UsedAbiTags = AbiTags;
    }

    const AbiTagList &getEmittedAbiTags() const {
      return EmittedAbiTags;
    }

    const AbiTagList &getSortedUniqueUsedAbiTags() {
      llvm::sort(UsedAbiTags);
      UsedAbiTags.erase(std::unique(UsedAbiTags.begin(), UsedAbiTags.end()),
                        UsedAbiTags.end());
      return UsedAbiTags;
    }

  private:
    //! All abi tags used implicitly or explicitly.
    AbiTagList UsedAbiTags;
    //! All explicit abi tags (i.e. not from namespace).
    AbiTagList EmittedAbiTags;

    AbiTagState *&LinkHead;
    AbiTagState *Parent = nullptr;

    void pop() {
      assert(LinkHead == this &&
             "abi tag link head must point to us on destruction");
      if (Parent) {
        Parent->UsedAbiTags.insert(Parent->UsedAbiTags.end(),
                                   UsedAbiTags.begin(), UsedAbiTags.end());
        Parent->EmittedAbiTags.insert(Parent->EmittedAbiTags.end(),
                                      EmittedAbiTags.begin(),
                                      EmittedAbiTags.end());
      }
      LinkHead = Parent;
    }

    void writeSortedUniqueAbiTags(raw_ostream &Out, const AbiTagList &AbiTags) {
      for (const auto &Tag : AbiTags) {
        EmittedAbiTags.push_back(Tag);
        Out << "B";
        Out << Tag.size();
        Out << Tag;
      }
    }
  };

  AbiTagState *AbiTags = nullptr;
  AbiTagState AbiTagsRoot;

  llvm::DenseMap<uintptr_t, unsigned> Substitutions;
  llvm::DenseMap<StringRef, unsigned> ModuleSubstitutions;

  ASTContext &getASTContext() const { return Context.getASTContext(); }

  bool isCompatibleWith(LangOptions::ClangABI Ver) {
    return Context.getASTContext().getLangOpts().getClangABICompat() <= Ver;
  }

  bool isStd(const NamespaceDecl *NS);
  bool isStdNamespace(const DeclContext *DC);

  const RecordDecl *GetLocalClassDecl(const Decl *D);
  bool isSpecializedAs(QualType S, llvm::StringRef Name, QualType A);
  bool isStdCharSpecialization(const ClassTemplateSpecializationDecl *SD,
                               llvm::StringRef Name, bool HasAllocator);

public:
  CXXNameMangler(ItaniumMangleContextImpl &C, raw_ostream &Out_,
                 const NamedDecl *D = nullptr, bool NullOut_ = false)
      : Context(C), Out(Out_), NullOut(NullOut_), Structor(getStructor(D)),
        AbiTagsRoot(AbiTags) {
    // These can't be mangled without a ctor type or dtor type.
    assert(!D || (!isa<CXXDestructorDecl>(D) &&
                  !isa<CXXConstructorDecl>(D)));
  }
  CXXNameMangler(ItaniumMangleContextImpl &C, raw_ostream &Out_,
                 const CXXConstructorDecl *D, CXXCtorType Type)
      : Context(C), Out(Out_), Structor(getStructor(D)), StructorType(Type),
        AbiTagsRoot(AbiTags) {}
  CXXNameMangler(ItaniumMangleContextImpl &C, raw_ostream &Out_,
                 const CXXDestructorDecl *D, CXXDtorType Type)
      : Context(C), Out(Out_), Structor(getStructor(D)), StructorType(Type),
        AbiTagsRoot(AbiTags) {}

  CXXNameMangler(ItaniumMangleContextImpl &C, raw_ostream &Out_,
                 bool NormalizeIntegers_)
      : Context(C), Out(Out_), NormalizeIntegers(NormalizeIntegers_),
        NullOut(false), Structor(nullptr), AbiTagsRoot(AbiTags) {}
  CXXNameMangler(CXXNameMangler &Outer, raw_ostream &Out_)
      : Context(Outer.Context), Out(Out_), Structor(Outer.Structor),
        StructorType(Outer.StructorType), SeqID(Outer.SeqID),
        FunctionTypeDepth(Outer.FunctionTypeDepth), AbiTagsRoot(AbiTags),
        Substitutions(Outer.Substitutions),
        ModuleSubstitutions(Outer.ModuleSubstitutions) {}

  CXXNameMangler(CXXNameMangler &Outer, llvm::raw_null_ostream &Out_)
      : CXXNameMangler(Outer, (raw_ostream &)Out_) {
    NullOut = true;
  }

  struct WithTemplateDepthOffset { unsigned Offset; };
  CXXNameMangler(ItaniumMangleContextImpl &C, raw_ostream &Out,
                 WithTemplateDepthOffset Offset)
      : CXXNameMangler(C, Out) {
    TemplateDepthOffset = Offset.Offset;
  }

  raw_ostream &getStream() { return Out; }

  void disableDerivedAbiTags() { DisableDerivedAbiTags = true; }
  static bool shouldHaveAbiTags(ItaniumMangleContextImpl &C, const VarDecl *VD);

  void mangle(GlobalDecl GD);
  void mangleCallOffset(int64_t NonVirtual, int64_t Virtual);
  void mangleNumber(const llvm::APSInt &I);
  void mangleNumber(int64_t Number);
  void mangleFloat(const llvm::APFloat &F);
  void mangleFunctionEncoding(GlobalDecl GD);
  void mangleSeqID(unsigned SeqID);
  void mangleName(GlobalDecl GD);
  void mangleType(QualType T);
  void mangleNameOrStandardSubstitution(const NamedDecl *ND);
  void mangleLambdaSig(const CXXRecordDecl *Lambda);
  void mangleModuleNamePrefix(StringRef Name, bool IsPartition = false);
  void mangleVendorQualifier(StringRef Name);

private:

  bool mangleSubstitution(const NamedDecl *ND);
  bool mangleSubstitution(NestedNameSpecifier *NNS);
  bool mangleSubstitution(QualType T);
  bool mangleSubstitution(TemplateName Template);
  bool mangleSubstitution(uintptr_t Ptr);

  void mangleExistingSubstitution(TemplateName name);

  bool mangleStandardSubstitution(const NamedDecl *ND);

  void addSubstitution(const NamedDecl *ND) {
    ND = cast<NamedDecl>(ND->getCanonicalDecl());

    addSubstitution(reinterpret_cast<uintptr_t>(ND));
  }
  void addSubstitution(NestedNameSpecifier *NNS) {
    NNS = Context.getASTContext().getCanonicalNestedNameSpecifier(NNS);

    addSubstitution(reinterpret_cast<uintptr_t>(NNS));
  }
  void addSubstitution(QualType T);
  void addSubstitution(TemplateName Template);
  void addSubstitution(uintptr_t Ptr);
  // Destructive copy substitutions from other mangler.
  void extendSubstitutions(CXXNameMangler* Other);

  void mangleUnresolvedPrefix(NestedNameSpecifier *qualifier,
                              bool recursive = false);
  void mangleUnresolvedName(NestedNameSpecifier *qualifier,
                            DeclarationName name,
                            const TemplateArgumentLoc *TemplateArgs,
                            unsigned NumTemplateArgs,
                            unsigned KnownArity = UnknownArity);

  void mangleFunctionEncodingBareType(const FunctionDecl *FD);

  void mangleNameWithAbiTags(GlobalDecl GD,
                             const AbiTagList *AdditionalAbiTags);
  void mangleModuleName(const NamedDecl *ND);
  void mangleTemplateName(const TemplateDecl *TD,
                          ArrayRef<TemplateArgument> Args);
  void mangleUnqualifiedName(GlobalDecl GD, const DeclContext *DC,
                             const AbiTagList *AdditionalAbiTags) {
    mangleUnqualifiedName(GD, cast<NamedDecl>(GD.getDecl())->getDeclName(), DC,
                          UnknownArity, AdditionalAbiTags);
  }
  void mangleUnqualifiedName(GlobalDecl GD, DeclarationName Name,
                             const DeclContext *DC, unsigned KnownArity,
                             const AbiTagList *AdditionalAbiTags);
  void mangleUnscopedName(GlobalDecl GD, const DeclContext *DC,
                          const AbiTagList *AdditionalAbiTags);
  void mangleUnscopedTemplateName(GlobalDecl GD, const DeclContext *DC,
                                  const AbiTagList *AdditionalAbiTags);
  void mangleSourceName(const IdentifierInfo *II);
  void mangleRegCallName(const IdentifierInfo *II);
  void mangleDeviceStubName(const IdentifierInfo *II);
  void mangleSourceNameWithAbiTags(
      const NamedDecl *ND, const AbiTagList *AdditionalAbiTags = nullptr);
  void mangleLocalName(GlobalDecl GD,
                       const AbiTagList *AdditionalAbiTags);
  void mangleBlockForPrefix(const BlockDecl *Block);
  void mangleUnqualifiedBlock(const BlockDecl *Block);
  void mangleTemplateParamDecl(const NamedDecl *Decl);
  void mangleTemplateParameterList(const TemplateParameterList *Params);
  void mangleTypeConstraint(const ConceptDecl *Concept,
                            ArrayRef<TemplateArgument> Arguments);
  void mangleTypeConstraint(const TypeConstraint *Constraint);
  void mangleRequiresClause(const Expr *RequiresClause);
  void mangleLambda(const CXXRecordDecl *Lambda);
  void mangleNestedName(GlobalDecl GD, const DeclContext *DC,
                        const AbiTagList *AdditionalAbiTags,
                        bool NoFunction=false);
  void mangleNestedName(const TemplateDecl *TD,
                        ArrayRef<TemplateArgument> Args);
  void mangleNestedNameWithClosurePrefix(GlobalDecl GD,
                                         const NamedDecl *PrefixND,
                                         const AbiTagList *AdditionalAbiTags);
  void manglePrefix(NestedNameSpecifier *qualifier);
  void manglePrefix(const DeclContext *DC, bool NoFunction=false);
  void manglePrefix(QualType type);
  void mangleTemplatePrefix(GlobalDecl GD, bool NoFunction=false);
  void mangleTemplatePrefix(TemplateName Template);
  const NamedDecl *getClosurePrefix(const Decl *ND);
  void mangleClosurePrefix(const NamedDecl *ND, bool NoFunction = false);
  bool mangleUnresolvedTypeOrSimpleId(QualType DestroyedType,
                                      StringRef Prefix = "");
  void mangleOperatorName(DeclarationName Name, unsigned Arity);
  void mangleOperatorName(OverloadedOperatorKind OO, unsigned Arity);
  void mangleQualifiers(Qualifiers Quals, const DependentAddressSpaceType *DAST = nullptr);
  void mangleRefQualifier(RefQualifierKind RefQualifier);

  void mangleObjCMethodName(const ObjCMethodDecl *MD);

  // Declare manglers for every type class.
#define ABSTRACT_TYPE(CLASS, PARENT)
#define NON_CANONICAL_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT) void mangleType(const CLASS##Type *T);
#include "clang/AST/TypeNodes.inc"

  void mangleType(const TagType*);
  void mangleType(TemplateName);
  static StringRef getCallingConvQualifierName(CallingConv CC);
  void mangleExtParameterInfo(FunctionProtoType::ExtParameterInfo info);
  void mangleExtFunctionInfo(const FunctionType *T);
  void mangleBareFunctionType(const FunctionProtoType *T, bool MangleReturnType,
                              const FunctionDecl *FD = nullptr);
  void mangleNeonVectorType(const VectorType *T);
  void mangleNeonVectorType(const DependentVectorType *T);
  void mangleAArch64NeonVectorType(const VectorType *T);
  void mangleAArch64NeonVectorType(const DependentVectorType *T);
  void mangleAArch64FixedSveVectorType(const VectorType *T);
  void mangleAArch64FixedSveVectorType(const DependentVectorType *T);
  void mangleRISCVFixedRVVVectorType(const VectorType *T);
  void mangleRISCVFixedRVVVectorType(const DependentVectorType *T);

  void mangleIntegerLiteral(QualType T, const llvm::APSInt &Value);
  void mangleFloatLiteral(QualType T, const llvm::APFloat &V);
  void mangleFixedPointLiteral();
  void mangleNullPointer(QualType T);

  void mangleMemberExprBase(const Expr *base, bool isArrow);
  void mangleMemberExpr(const Expr *base, bool isArrow,
                        NestedNameSpecifier *qualifier,
                        NamedDecl *firstQualifierLookup,
                        DeclarationName name,
                        const TemplateArgumentLoc *TemplateArgs,
                        unsigned NumTemplateArgs,
                        unsigned knownArity);
  void mangleCastExpression(const Expr *E, StringRef CastEncoding);
  void mangleInitListElements(const InitListExpr *InitList);
  void mangleRequirement(SourceLocation RequiresExprLoc,
                         const concepts::Requirement *Req);
  void mangleExpression(const Expr *E, unsigned Arity = UnknownArity,
                        bool AsTemplateArg = false);
  void mangleCXXCtorType(CXXCtorType T, const CXXRecordDecl *InheritedFrom);
  void mangleCXXDtorType(CXXDtorType T);

  struct TemplateArgManglingInfo;
  void mangleTemplateArgs(TemplateName TN,
                          const TemplateArgumentLoc *TemplateArgs,
                          unsigned NumTemplateArgs);
  void mangleTemplateArgs(TemplateName TN, ArrayRef<TemplateArgument> Args);
  void mangleTemplateArgs(TemplateName TN, const TemplateArgumentList &AL);
  void mangleTemplateArg(TemplateArgManglingInfo &Info, unsigned Index,
                         TemplateArgument A);
  void mangleTemplateArg(TemplateArgument A, bool NeedExactType);
  void mangleTemplateArgExpr(const Expr *E);
  void mangleValueInTemplateArg(QualType T, const APValue &V, bool TopLevel,
                                bool NeedExactType = false);

  void mangleTemplateParameter(unsigned Depth, unsigned Index);

  void mangleFunctionParam(const ParmVarDecl *parm);

  void writeAbiTags(const NamedDecl *ND,
                    const AbiTagList *AdditionalAbiTags);

  // Returns sorted unique list of ABI tags.
  AbiTagList makeFunctionReturnTypeTags(const FunctionDecl *FD);
  // Returns sorted unique list of ABI tags.
  AbiTagList makeVariableTypeTags(const VarDecl *VD);
};

}

NamespaceDecl *ItaniumMangleContextImpl::getStdNamespace() {
  if (!StdNamespace) {
    StdNamespace = NamespaceDecl::Create(
        getASTContext(), getASTContext().getTranslationUnitDecl(),
        /*Inline=*/false, SourceLocation(), SourceLocation(),
        &getASTContext().Idents.get("std"),
        /*PrevDecl=*/nullptr, /*Nested=*/false);
    StdNamespace->setImplicit();
  }
  return StdNamespace;
}

/// Retrieve the declaration context that should be used when mangling the given
/// declaration.
const DeclContext *
ItaniumMangleContextImpl::getEffectiveDeclContext(const Decl *D) {
  // The ABI assumes that lambda closure types that occur within
  // default arguments live in the context of the function. However, due to
  // the way in which Clang parses and creates function declarations, this is
  // not the case: the lambda closure type ends up living in the context
  // where the function itself resides, because the function declaration itself
  // had not yet been created. Fix the context here.
  if (const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D)) {
    if (RD->isLambda())
      if (ParmVarDecl *ContextParam =
              dyn_cast_or_null<ParmVarDecl>(RD->getLambdaContextDecl()))
        return ContextParam->getDeclContext();
  }

  // Perform the same check for block literals.
  if (const BlockDecl *BD = dyn_cast<BlockDecl>(D)) {
    if (ParmVarDecl *ContextParam =
            dyn_cast_or_null<ParmVarDecl>(BD->getBlockManglingContextDecl()))
      return ContextParam->getDeclContext();
  }

  // On ARM and AArch64, the va_list tag is always mangled as if in the std
  // namespace. We do not represent va_list as actually being in the std
  // namespace in C because this would result in incorrect debug info in C,
  // among other things. It is important for both languages to have the same
  // mangling in order for -fsanitize=cfi-icall to work.
  if (D == getASTContext().getVaListTagDecl()) {
    const llvm::Triple &T = getASTContext().getTargetInfo().getTriple();
    if (T.isARM() || T.isThumb() || T.isAArch64())
      return getStdNamespace();
  }

  const DeclContext *DC = D->getDeclContext();
  if (isa<CapturedDecl>(DC) || isa<OMPDeclareReductionDecl>(DC) ||
      isa<OMPDeclareMapperDecl>(DC)) {
    return getEffectiveDeclContext(cast<Decl>(DC));
  }

  if (const auto *VD = dyn_cast<VarDecl>(D))
    if (VD->isExternC())
      return getASTContext().getTranslationUnitDecl();

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isExternC())
      return getASTContext().getTranslationUnitDecl();
    // Member-like constrained friends are mangled as if they were members of
    // the enclosing class.
    if (FD->isMemberLikeConstrainedFriend() &&
        getASTContext().getLangOpts().getClangABICompat() >
            LangOptions::ClangABI::Ver17)
      return D->getLexicalDeclContext()->getRedeclContext();
  }

  return DC->getRedeclContext();
}

bool ItaniumMangleContextImpl::isInternalLinkageDecl(const NamedDecl *ND) {
  if (ND && ND->getFormalLinkage() == Linkage::Internal &&
      !ND->isExternallyVisible() &&
      getEffectiveDeclContext(ND)->isFileContext() &&
      !ND->isInAnonymousNamespace())
    return true;
  return false;
}

// Check if this Function Decl needs a unique internal linkage name.
bool ItaniumMangleContextImpl::isUniqueInternalLinkageDecl(
    const NamedDecl *ND) {
  if (!NeedsUniqueInternalLinkageNames || !ND)
    return false;

  const auto *FD = dyn_cast<FunctionDecl>(ND);
  if (!FD)
    return false;

  // For C functions without prototypes, return false as their
  // names should not be mangled.
  if (!FD->getType()->getAs<FunctionProtoType>())
    return false;

  if (isInternalLinkageDecl(ND))
    return true;

  return false;
}

bool ItaniumMangleContextImpl::shouldMangleCXXName(const NamedDecl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    LanguageLinkage L = FD->getLanguageLinkage();
    // Overloadable functions need mangling.
    if (FD->hasAttr<OverloadableAttr>())
      return true;

    // "main" is not mangled.
    if (FD->isMain())
      return false;

    // The Windows ABI expects that we would never mangle "typical"
    // user-defined entry points regardless of visibility or freestanding-ness.
    //
    // N.B. This is distinct from asking about "main".  "main" has a lot of
    // special rules associated with it in the standard while these
    // user-defined entry points are outside of the purview of the standard.
    // For example, there can be only one definition for "main" in a standards
    // compliant program; however nothing forbids the existence of wmain and
    // WinMain in the same translation unit.
    if (FD->isMSVCRTEntryPoint())
      return false;

    // C++ functions and those whose names are not a simple identifier need
    // mangling.
    if (!FD->getDeclName().isIdentifier() || L == CXXLanguageLinkage)
      return true;

    // C functions are not mangled.
    if (L == CLanguageLinkage)
      return false;
  }

  // Otherwise, no mangling is done outside C++ mode.
  if (!getASTContext().getLangOpts().CPlusPlus)
    return false;

  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    // Decompositions are mangled.
    if (isa<DecompositionDecl>(VD))
      return true;

    // C variables are not mangled.
    if (VD->isExternC())
      return false;

    // Variables at global scope are not mangled unless they have internal
    // linkage or are specializations or are attached to a named module.
    const DeclContext *DC = getEffectiveDeclContext(D);
    // Check for extern variable declared locally.
    if (DC->isFunctionOrMethod() && D->hasLinkage())
      while (!DC->isFileContext())
        DC = getEffectiveParentContext(DC);
    if (DC->isTranslationUnit() && D->getFormalLinkage() != Linkage::Internal &&
        !CXXNameMangler::shouldHaveAbiTags(*this, VD) &&
        !isa<VarTemplateSpecializationDecl>(VD) &&
        !VD->getOwningModuleForLinkage())
      return false;
  }

  return true;
}

void CXXNameMangler::writeAbiTags(const NamedDecl *ND,
                                  const AbiTagList *AdditionalAbiTags) {
  assert(AbiTags && "require AbiTagState");
  AbiTags->write(Out, ND, DisableDerivedAbiTags ? nullptr : AdditionalAbiTags);
}

void CXXNameMangler::mangleSourceNameWithAbiTags(
    const NamedDecl *ND, const AbiTagList *AdditionalAbiTags) {
  mangleSourceName(ND->getIdentifier());
  writeAbiTags(ND, AdditionalAbiTags);
}

void CXXNameMangler::mangle(GlobalDecl GD) {
  // <mangled-name> ::= _Z <encoding>
  //            ::= <data name>
  //            ::= <special-name>
  Out << "_Z";
  if (isa<FunctionDecl>(GD.getDecl()))
    mangleFunctionEncoding(GD);
  else if (isa<VarDecl, FieldDecl, MSGuidDecl, TemplateParamObjectDecl,
               BindingDecl>(GD.getDecl()))
    mangleName(GD);
  else if (const IndirectFieldDecl *IFD =
               dyn_cast<IndirectFieldDecl>(GD.getDecl()))
    mangleName(IFD->getAnonField());
  else
    llvm_unreachable("unexpected kind of global decl");
}

void CXXNameMangler::mangleFunctionEncoding(GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  // <encoding> ::= <function name> <bare-function-type>

  // Don't mangle in the type if this isn't a decl we should typically mangle.
  if (!Context.shouldMangleDeclName(FD)) {
    mangleName(GD);
    return;
  }

  AbiTagList ReturnTypeAbiTags = makeFunctionReturnTypeTags(FD);
  if (ReturnTypeAbiTags.empty()) {
    // There are no tags for return type, the simplest case. Enter the function
    // parameter scope before mangling the name, because a template using
    // constrained `auto` can have references to its parameters within its
    // template argument list:
    //
    //   template<typename T> void f(T x, C<decltype(x)> auto)
    // ... is mangled as ...
    //   template<typename T, C<decltype(param 1)> U> void f(T, U)
    FunctionTypeDepthState Saved = FunctionTypeDepth.push();
    mangleName(GD);
    FunctionTypeDepth.pop(Saved);
    mangleFunctionEncodingBareType(FD);
    return;
  }

  // Mangle function name and encoding to temporary buffer.
  // We have to output name and encoding to the same mangler to get the same
  // substitution as it will be in final mangling.
  SmallString<256> FunctionEncodingBuf;
  llvm::raw_svector_ostream FunctionEncodingStream(FunctionEncodingBuf);
  CXXNameMangler FunctionEncodingMangler(*this, FunctionEncodingStream);
  // Output name of the function.
  FunctionEncodingMangler.disableDerivedAbiTags();

  FunctionTypeDepthState Saved = FunctionTypeDepth.push();
  FunctionEncodingMangler.mangleNameWithAbiTags(FD, nullptr);
  FunctionTypeDepth.pop(Saved);

  // Remember length of the function name in the buffer.
  size_t EncodingPositionStart = FunctionEncodingStream.str().size();
  FunctionEncodingMangler.mangleFunctionEncodingBareType(FD);

  // Get tags from return type that are not present in function name or
  // encoding.
  const AbiTagList &UsedAbiTags =
      FunctionEncodingMangler.AbiTagsRoot.getSortedUniqueUsedAbiTags();
  AbiTagList AdditionalAbiTags(ReturnTypeAbiTags.size());
  AdditionalAbiTags.erase(
      std::set_difference(ReturnTypeAbiTags.begin(), ReturnTypeAbiTags.end(),
                          UsedAbiTags.begin(), UsedAbiTags.end(),
                          AdditionalAbiTags.begin()),
      AdditionalAbiTags.end());

  // Output name with implicit tags and function encoding from temporary buffer.
  Saved = FunctionTypeDepth.push();
  mangleNameWithAbiTags(FD, &AdditionalAbiTags);
  FunctionTypeDepth.pop(Saved);
  Out << FunctionEncodingStream.str().substr(EncodingPositionStart);

  // Function encoding could create new substitutions so we have to add
  // temp mangled substitutions to main mangler.
  extendSubstitutions(&FunctionEncodingMangler);
}

void CXXNameMangler::mangleFunctionEncodingBareType(const FunctionDecl *FD) {
  if (FD->hasAttr<EnableIfAttr>()) {
    FunctionTypeDepthState Saved = FunctionTypeDepth.push();
    Out << "Ua9enable_ifI";
    for (AttrVec::const_iterator I = FD->getAttrs().begin(),
                                 E = FD->getAttrs().end();
         I != E; ++I) {
      EnableIfAttr *EIA = dyn_cast<EnableIfAttr>(*I);
      if (!EIA)
        continue;
      if (isCompatibleWith(LangOptions::ClangABI::Ver11)) {
        // Prior to Clang 12, we hardcoded the X/E around enable-if's argument,
        // even though <template-arg> should not include an X/E around
        // <expr-primary>.
        Out << 'X';
        mangleExpression(EIA->getCond());
        Out << 'E';
      } else {
        mangleTemplateArgExpr(EIA->getCond());
      }
    }
    Out << 'E';
    FunctionTypeDepth.pop(Saved);
  }

  // When mangling an inheriting constructor, the bare function type used is
  // that of the inherited constructor.
  if (auto *CD = dyn_cast<CXXConstructorDecl>(FD))
    if (auto Inherited = CD->getInheritedConstructor())
      FD = Inherited.getConstructor();

  // Whether the mangling of a function type includes the return type depends on
  // the context and the nature of the function. The rules for deciding whether
  // the return type is included are:
  //
  //   1. Template functions (names or types) have return types encoded, with
  //   the exceptions listed below.
  //   2. Function types not appearing as part of a function name mangling,
  //   e.g. parameters, pointer types, etc., have return type encoded, with the
  //   exceptions listed below.
  //   3. Non-template function names do not have return types encoded.
  //
  // The exceptions mentioned in (1) and (2) above, for which the return type is
  // never included, are
  //   1. Constructors.
  //   2. Destructors.
  //   3. Conversion operator functions, e.g. operator int.
  bool MangleReturnType = false;
  if (FunctionTemplateDecl *PrimaryTemplate = FD->getPrimaryTemplate()) {
    if (!(isa<CXXConstructorDecl>(FD) || isa<CXXDestructorDecl>(FD) ||
          isa<CXXConversionDecl>(FD)))
      MangleReturnType = true;

    // Mangle the type of the primary template.
    FD = PrimaryTemplate->getTemplatedDecl();
  }

  mangleBareFunctionType(FD->getType()->castAs<FunctionProtoType>(),
                         MangleReturnType, FD);
}

/// Return whether a given namespace is the 'std' namespace.
bool CXXNameMangler::isStd(const NamespaceDecl *NS) {
  if (!Context.getEffectiveParentContext(NS)->isTranslationUnit())
    return false;

  const IdentifierInfo *II = NS->getFirstDecl()->getIdentifier();
  return II && II->isStr("std");
}

// isStdNamespace - Return whether a given decl context is a toplevel 'std'
// namespace.
bool CXXNameMangler::isStdNamespace(const DeclContext *DC) {
  if (!DC->isNamespace())
    return false;

  return isStd(cast<NamespaceDecl>(DC));
}

static const GlobalDecl
isTemplate(GlobalDecl GD, const TemplateArgumentList *&TemplateArgs) {
  const NamedDecl *ND = cast<NamedDecl>(GD.getDecl());
  // Check if we have a function template.
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(ND)) {
    if (const TemplateDecl *TD = FD->getPrimaryTemplate()) {
      TemplateArgs = FD->getTemplateSpecializationArgs();
      return GD.getWithDecl(TD);
    }
  }

  // Check if we have a class template.
  if (const ClassTemplateSpecializationDecl *Spec =
        dyn_cast<ClassTemplateSpecializationDecl>(ND)) {
    TemplateArgs = &Spec->getTemplateArgs();
    return GD.getWithDecl(Spec->getSpecializedTemplate());
  }

  // Check if we have a variable template.
  if (const VarTemplateSpecializationDecl *Spec =
          dyn_cast<VarTemplateSpecializationDecl>(ND)) {
    TemplateArgs = &Spec->getTemplateArgs();
    return GD.getWithDecl(Spec->getSpecializedTemplate());
  }

  return GlobalDecl();
}

static TemplateName asTemplateName(GlobalDecl GD) {
  const TemplateDecl *TD = dyn_cast_or_null<TemplateDecl>(GD.getDecl());
  return TemplateName(const_cast<TemplateDecl*>(TD));
}

void CXXNameMangler::mangleName(GlobalDecl GD) {
  const NamedDecl *ND = cast<NamedDecl>(GD.getDecl());
  if (const VarDecl *VD = dyn_cast<VarDecl>(ND)) {
    // Variables should have implicit tags from its type.
    AbiTagList VariableTypeAbiTags = makeVariableTypeTags(VD);
    if (VariableTypeAbiTags.empty()) {
      // Simple case no variable type tags.
      mangleNameWithAbiTags(VD, nullptr);
      return;
    }

    // Mangle variable name to null stream to collect tags.
    llvm::raw_null_ostream NullOutStream;
    CXXNameMangler VariableNameMangler(*this, NullOutStream);
    VariableNameMangler.disableDerivedAbiTags();
    VariableNameMangler.mangleNameWithAbiTags(VD, nullptr);

    // Get tags from variable type that are not present in its name.
    const AbiTagList &UsedAbiTags =
        VariableNameMangler.AbiTagsRoot.getSortedUniqueUsedAbiTags();
    AbiTagList AdditionalAbiTags(VariableTypeAbiTags.size());
    AdditionalAbiTags.erase(
        std::set_difference(VariableTypeAbiTags.begin(),
                            VariableTypeAbiTags.end(), UsedAbiTags.begin(),
                            UsedAbiTags.end(), AdditionalAbiTags.begin()),
        AdditionalAbiTags.end());

    // Output name with implicit tags.
    mangleNameWithAbiTags(VD, &AdditionalAbiTags);
  } else {
    mangleNameWithAbiTags(GD, nullptr);
  }
}

const RecordDecl *CXXNameMangler::GetLocalClassDecl(const Decl *D) {
  const DeclContext *DC = Context.getEffectiveDeclContext(D);
  while (!DC->isNamespace() && !DC->isTranslationUnit()) {
    if (isLocalContainerContext(DC))
      return dyn_cast<RecordDecl>(D);
    D = cast<Decl>(DC);
    DC = Context.getEffectiveDeclContext(D);
  }
  return nullptr;
}

void CXXNameMangler::mangleNameWithAbiTags(GlobalDecl GD,
                                           const AbiTagList *AdditionalAbiTags) {
  const NamedDecl *ND = cast<NamedDecl>(GD.getDecl());
  //  <name> ::= [<module-name>] <nested-name>
  //         ::= [<module-name>] <unscoped-name>
  //         ::= [<module-name>] <unscoped-template-name> <template-args>
  //         ::= <local-name>
  //
  const DeclContext *DC = Context.getEffectiveDeclContext(ND);
  bool IsLambda = isLambda(ND);

  // If this is an extern variable declared locally, the relevant DeclContext
  // is that of the containing namespace, or the translation unit.
  // FIXME: This is a hack; extern variables declared locally should have
  // a proper semantic declaration context!
  if (isLocalContainerContext(DC) && ND->hasLinkage() && !IsLambda)
    while (!DC->isNamespace() && !DC->isTranslationUnit())
      DC = Context.getEffectiveParentContext(DC);
  else if (GetLocalClassDecl(ND) &&
           (!IsLambda || isCompatibleWith(LangOptions::ClangABI::Ver18))) {
    mangleLocalName(GD, AdditionalAbiTags);
    return;
  }

  assert(!isa<LinkageSpecDecl>(DC) && "context cannot be LinkageSpecDecl");

  // Closures can require a nested-name mangling even if they're semantically
  // in the global namespace.
  if (const NamedDecl *PrefixND = getClosurePrefix(ND)) {
    mangleNestedNameWithClosurePrefix(GD, PrefixND, AdditionalAbiTags);
    return;
  }

  if (isLocalContainerContext(DC)) {
    mangleLocalName(GD, AdditionalAbiTags);
    return;
  }

  if (DC->isTranslationUnit() || isStdNamespace(DC)) {
    // Check if we have a template.
    const TemplateArgumentList *TemplateArgs = nullptr;
    if (GlobalDecl TD = isTemplate(GD, TemplateArgs)) {
      mangleUnscopedTemplateName(TD, DC, AdditionalAbiTags);
      mangleTemplateArgs(asTemplateName(TD), *TemplateArgs);
      return;
    }

    mangleUnscopedName(GD, DC, AdditionalAbiTags);
    return;
  }

  mangleNestedName(GD, DC, AdditionalAbiTags);
}

void CXXNameMangler::mangleModuleName(const NamedDecl *ND) {
  if (ND->isExternallyVisible())
    if (Module *M = ND->getOwningModuleForLinkage())
      mangleModuleNamePrefix(M->getPrimaryModuleInterfaceName());
}

// <module-name> ::= <module-subname>
//		 ::= <module-name> <module-subname>
//	 	 ::= <substitution>
// <module-subname> ::= W <source-name>
//		    ::= W P <source-name>
void CXXNameMangler::mangleModuleNamePrefix(StringRef Name, bool IsPartition) {
  //  <substitution> ::= S <seq-id> _
  auto It = ModuleSubstitutions.find(Name);
  if (It != ModuleSubstitutions.end()) {
    Out << 'S';
    mangleSeqID(It->second);
    return;
  }

  // FIXME: Preserve hierarchy in module names rather than flattening
  // them to strings; use Module*s as substitution keys.
  auto Parts = Name.rsplit('.');
  if (Parts.second.empty())
    Parts.second = Parts.first;
  else {
    mangleModuleNamePrefix(Parts.first, IsPartition);
    IsPartition = false;
  }

  Out << 'W';
  if (IsPartition)
    Out << 'P';
  Out << Parts.second.size() << Parts.second;
  ModuleSubstitutions.insert({Name, SeqID++});
}

void CXXNameMangler::mangleTemplateName(const TemplateDecl *TD,
                                        ArrayRef<TemplateArgument> Args) {
  const DeclContext *DC = Context.getEffectiveDeclContext(TD);

  if (DC->isTranslationUnit() || isStdNamespace(DC)) {
    mangleUnscopedTemplateName(TD, DC, nullptr);
    mangleTemplateArgs(asTemplateName(TD), Args);
  } else {
    mangleNestedName(TD, Args);
  }
}

void CXXNameMangler::mangleUnscopedName(GlobalDecl GD, const DeclContext *DC,
                                        const AbiTagList *AdditionalAbiTags) {
  //  <unscoped-name> ::= <unqualified-name>
  //                  ::= St <unqualified-name>   # ::std::

  assert(!isa<LinkageSpecDecl>(DC) && "unskipped LinkageSpecDecl");
  if (isStdNamespace(DC))
    Out << "St";

  mangleUnqualifiedName(GD, DC, AdditionalAbiTags);
}

void CXXNameMangler::mangleUnscopedTemplateName(
    GlobalDecl GD, const DeclContext *DC, const AbiTagList *AdditionalAbiTags) {
  const TemplateDecl *ND = cast<TemplateDecl>(GD.getDecl());
  //     <unscoped-template-name> ::= <unscoped-name>
  //                              ::= <substitution>
  if (mangleSubstitution(ND))
    return;

  // <template-template-param> ::= <template-param>
  if (const auto *TTP = dyn_cast<TemplateTemplateParmDecl>(ND)) {
    assert(!AdditionalAbiTags &&
           "template template param cannot have abi tags");
    mangleTemplateParameter(TTP->getDepth(), TTP->getIndex());
  } else if (isa<BuiltinTemplateDecl>(ND) || isa<ConceptDecl>(ND)) {
    mangleUnscopedName(GD, DC, AdditionalAbiTags);
  } else {
    mangleUnscopedName(GD.getWithDecl(ND->getTemplatedDecl()), DC,
                       AdditionalAbiTags);
  }

  addSubstitution(ND);
}

void CXXNameMangler::mangleFloat(const llvm::APFloat &f) {
  // ABI:
  //   Floating-point literals are encoded using a fixed-length
  //   lowercase hexadecimal string corresponding to the internal
  //   representation (IEEE on Itanium), high-order bytes first,
  //   without leading zeroes. For example: "Lf bf800000 E" is -1.0f
  //   on Itanium.
  // The 'without leading zeroes' thing seems to be an editorial
  // mistake; see the discussion on cxx-abi-dev beginning on
  // 2012-01-16.

  // Our requirements here are just barely weird enough to justify
  // using a custom algorithm instead of post-processing APInt::toString().

  llvm::APInt valueBits = f.bitcastToAPInt();
  unsigned numCharacters = (valueBits.getBitWidth() + 3) / 4;
  assert(numCharacters != 0);

  // Allocate a buffer of the right number of characters.
  SmallVector<char, 20> buffer(numCharacters);

  // Fill the buffer left-to-right.
  for (unsigned stringIndex = 0; stringIndex != numCharacters; ++stringIndex) {
    // The bit-index of the next hex digit.
    unsigned digitBitIndex = 4 * (numCharacters - stringIndex - 1);

    // Project out 4 bits starting at 'digitIndex'.
    uint64_t hexDigit = valueBits.getRawData()[digitBitIndex / 64];
    hexDigit >>= (digitBitIndex % 64);
    hexDigit &= 0xF;

    // Map that over to a lowercase hex digit.
    static const char charForHex[16] = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    buffer[stringIndex] = charForHex[hexDigit];
  }

  Out.write(buffer.data(), numCharacters);
}

void CXXNameMangler::mangleFloatLiteral(QualType T, const llvm::APFloat &V) {
  Out << 'L';
  mangleType(T);
  mangleFloat(V);
  Out << 'E';
}

void CXXNameMangler::mangleFixedPointLiteral() {
  DiagnosticsEngine &Diags = Context.getDiags();
  unsigned DiagID = Diags.getCustomDiagID(
      DiagnosticsEngine::Error, "cannot mangle fixed point literals yet");
  Diags.Report(DiagID);
}

void CXXNameMangler::mangleNullPointer(QualType T) {
  //  <expr-primary> ::= L <type> 0 E
  Out << 'L';
  mangleType(T);
  Out << "0E";
}

void CXXNameMangler::mangleNumber(const llvm::APSInt &Value) {
  if (Value.isSigned() && Value.isNegative()) {
    Out << 'n';
    Value.abs().print(Out, /*signed*/ false);
  } else {
    Value.print(Out, /*signed*/ false);
  }
}

void CXXNameMangler::mangleNumber(int64_t Number) {
  //  <number> ::= [n] <non-negative decimal integer>
  if (Number < 0) {
    Out << 'n';
    Number = -Number;
  }

  Out << Number;
}

void CXXNameMangler::mangleCallOffset(int64_t NonVirtual, int64_t Virtual) {
  //  <call-offset>  ::= h <nv-offset> _
  //                 ::= v <v-offset> _
  //  <nv-offset>    ::= <offset number>        # non-virtual base override
  //  <v-offset>     ::= <offset number> _ <virtual offset number>
  //                      # virtual base override, with vcall offset
  if (!Virtual) {
    Out << 'h';
    mangleNumber(NonVirtual);
    Out << '_';
    return;
  }

  Out << 'v';
  mangleNumber(NonVirtual);
  Out << '_';
  mangleNumber(Virtual);
  Out << '_';
}

void CXXNameMangler::manglePrefix(QualType type) {
  if (const auto *TST = type->getAs<TemplateSpecializationType>()) {
    if (!mangleSubstitution(QualType(TST, 0))) {
      mangleTemplatePrefix(TST->getTemplateName());

      // FIXME: GCC does not appear to mangle the template arguments when
      // the template in question is a dependent template name. Should we
      // emulate that badness?
      mangleTemplateArgs(TST->getTemplateName(), TST->template_arguments());
      addSubstitution(QualType(TST, 0));
    }
  } else if (const auto *DTST =
                 type->getAs<DependentTemplateSpecializationType>()) {
    if (!mangleSubstitution(QualType(DTST, 0))) {
      TemplateName Template = getASTContext().getDependentTemplateName(
          DTST->getQualifier(), DTST->getIdentifier());
      mangleTemplatePrefix(Template);

      // FIXME: GCC does not appear to mangle the template arguments when
      // the template in question is a dependent template name. Should we
      // emulate that badness?
      mangleTemplateArgs(Template, DTST->template_arguments());
      addSubstitution(QualType(DTST, 0));
    }
  } else {
    // We use the QualType mangle type variant here because it handles
    // substitutions.
    mangleType(type);
  }
}

/// Mangle everything prior to the base-unresolved-name in an unresolved-name.
///
/// \param recursive - true if this is being called recursively,
///   i.e. if there is more prefix "to the right".
void CXXNameMangler::mangleUnresolvedPrefix(NestedNameSpecifier *qualifier,
                                            bool recursive) {

  // x, ::x
  // <unresolved-name> ::= [gs] <base-unresolved-name>

  // T::x / decltype(p)::x
  // <unresolved-name> ::= sr <unresolved-type> <base-unresolved-name>

  // T::N::x /decltype(p)::N::x
  // <unresolved-name> ::= srN <unresolved-type> <unresolved-qualifier-level>+ E
  //                       <base-unresolved-name>

  // A::x, N::y, A<T>::z; "gs" means leading "::"
  // <unresolved-name> ::= [gs] sr <unresolved-qualifier-level>+ E
  //                       <base-unresolved-name>

  switch (qualifier->getKind()) {
  case NestedNameSpecifier::Global:
    Out << "gs";

    // We want an 'sr' unless this is the entire NNS.
    if (recursive)
      Out << "sr";

    // We never want an 'E' here.
    return;

  case NestedNameSpecifier::Super:
    llvm_unreachable("Can't mangle __super specifier");

  case NestedNameSpecifier::Namespace:
    if (qualifier->getPrefix())
      mangleUnresolvedPrefix(qualifier->getPrefix(),
                             /*recursive*/ true);
    else
      Out << "sr";
    mangleSourceNameWithAbiTags(qualifier->getAsNamespace());
    break;
  case NestedNameSpecifier::NamespaceAlias:
    if (qualifier->getPrefix())
      mangleUnresolvedPrefix(qualifier->getPrefix(),
                             /*recursive*/ true);
    else
      Out << "sr";
    mangleSourceNameWithAbiTags(qualifier->getAsNamespaceAlias());
    break;

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate: {
    const Type *type = qualifier->getAsType();

    // We only want to use an unresolved-type encoding if this is one of:
    //   - a decltype
    //   - a template type parameter
    //   - a template template parameter with arguments
    // In all of these cases, we should have no prefix.
    if (qualifier->getPrefix()) {
      mangleUnresolvedPrefix(qualifier->getPrefix(),
                             /*recursive*/ true);
    } else {
      // Otherwise, all the cases want this.
      Out << "sr";
    }

    if (mangleUnresolvedTypeOrSimpleId(QualType(type, 0), recursive ? "N" : ""))
      return;

    break;
  }

  case NestedNameSpecifier::Identifier:
    // Member expressions can have these without prefixes.
    if (qualifier->getPrefix())
      mangleUnresolvedPrefix(qualifier->getPrefix(),
                             /*recursive*/ true);
    else
      Out << "sr";

    mangleSourceName(qualifier->getAsIdentifier());
    // An Identifier has no type information, so we can't emit abi tags for it.
    break;
  }

  // If this was the innermost part of the NNS, and we fell out to
  // here, append an 'E'.
  if (!recursive)
    Out << 'E';
}

/// Mangle an unresolved-name, which is generally used for names which
/// weren't resolved to specific entities.
void CXXNameMangler::mangleUnresolvedName(
    NestedNameSpecifier *qualifier, DeclarationName name,
    const TemplateArgumentLoc *TemplateArgs, unsigned NumTemplateArgs,
    unsigned knownArity) {
  if (qualifier) mangleUnresolvedPrefix(qualifier);
  switch (name.getNameKind()) {
    // <base-unresolved-name> ::= <simple-id>
    case DeclarationName::Identifier:
      mangleSourceName(name.getAsIdentifierInfo());
      break;
    // <base-unresolved-name> ::= dn <destructor-name>
    case DeclarationName::CXXDestructorName:
      Out << "dn";
      mangleUnresolvedTypeOrSimpleId(name.getCXXNameType());
      break;
    // <base-unresolved-name> ::= on <operator-name>
    case DeclarationName::CXXConversionFunctionName:
    case DeclarationName::CXXLiteralOperatorName:
    case DeclarationName::CXXOperatorName:
      Out << "on";
      mangleOperatorName(name, knownArity);
      break;
    case DeclarationName::CXXConstructorName:
      llvm_unreachable("Can't mangle a constructor name!");
    case DeclarationName::CXXUsingDirective:
      llvm_unreachable("Can't mangle a using directive name!");
    case DeclarationName::CXXDeductionGuideName:
      llvm_unreachable("Can't mangle a deduction guide name!");
    case DeclarationName::ObjCMultiArgSelector:
    case DeclarationName::ObjCOneArgSelector:
    case DeclarationName::ObjCZeroArgSelector:
      llvm_unreachable("Can't mangle Objective-C selector names here!");
  }

  // The <simple-id> and on <operator-name> productions end in an optional
  // <template-args>.
  if (TemplateArgs)
    mangleTemplateArgs(TemplateName(), TemplateArgs, NumTemplateArgs);
}

void CXXNameMangler::mangleUnqualifiedName(
    GlobalDecl GD, DeclarationName Name, const DeclContext *DC,
    unsigned KnownArity, const AbiTagList *AdditionalAbiTags) {
  const NamedDecl *ND = cast_or_null<NamedDecl>(GD.getDecl());
  //  <unqualified-name> ::= [<module-name>] [F] <operator-name>
  //                     ::= <ctor-dtor-name>
  //                     ::= [<module-name>] [F] <source-name>
  //                     ::= [<module-name>] DC <source-name>* E

  if (ND && DC && DC->isFileContext())
    mangleModuleName(ND);

  // A member-like constrained friend is mangled with a leading 'F'.
  // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/24.
  auto *FD = dyn_cast<FunctionDecl>(ND);
  auto *FTD = dyn_cast<FunctionTemplateDecl>(ND);
  if ((FD && FD->isMemberLikeConstrainedFriend()) ||
      (FTD && FTD->getTemplatedDecl()->isMemberLikeConstrainedFriend())) {
    if (!isCompatibleWith(LangOptions::ClangABI::Ver17))
      Out << 'F';
  }

  unsigned Arity = KnownArity;
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier: {
    const IdentifierInfo *II = Name.getAsIdentifierInfo();

    // We mangle decomposition declarations as the names of their bindings.
    if (auto *DD = dyn_cast<DecompositionDecl>(ND)) {
      // FIXME: Non-standard mangling for decomposition declarations:
      //
      //  <unqualified-name> ::= DC <source-name>* E
      //
      // Proposed on cxx-abi-dev on 2016-08-12
      Out << "DC";
      for (auto *BD : DD->bindings())
        mangleSourceName(BD->getDeclName().getAsIdentifierInfo());
      Out << 'E';
      writeAbiTags(ND, AdditionalAbiTags);
      break;
    }

    if (auto *GD = dyn_cast<MSGuidDecl>(ND)) {
      // We follow MSVC in mangling GUID declarations as if they were variables
      // with a particular reserved name. Continue the pretense here.
      SmallString<sizeof("_GUID_12345678_1234_1234_1234_1234567890ab")> GUID;
      llvm::raw_svector_ostream GUIDOS(GUID);
      Context.mangleMSGuidDecl(GD, GUIDOS);
      Out << GUID.size() << GUID;
      break;
    }

    if (auto *TPO = dyn_cast<TemplateParamObjectDecl>(ND)) {
      // Proposed in https://github.com/itanium-cxx-abi/cxx-abi/issues/63.
      Out << "TA";
      mangleValueInTemplateArg(TPO->getType().getUnqualifiedType(),
                               TPO->getValue(), /*TopLevel=*/true);
      break;
    }

    if (II) {
      // Match GCC's naming convention for internal linkage symbols, for
      // symbols that are not actually visible outside of this TU. GCC
      // distinguishes between internal and external linkage symbols in
      // its mangling, to support cases like this that were valid C++ prior
      // to DR426:
      //
      //   void test() { extern void foo(); }
      //   static void foo();
      //
      // Don't bother with the L marker for names in anonymous namespaces; the
      // 12_GLOBAL__N_1 mangling is quite sufficient there, and this better
      // matches GCC anyway, because GCC does not treat anonymous namespaces as
      // implying internal linkage.
      if (Context.isInternalLinkageDecl(ND))
        Out << 'L';

      bool IsRegCall = FD &&
                       FD->getType()->castAs<FunctionType>()->getCallConv() ==
                           clang::CC_X86RegCall;
      bool IsDeviceStub =
          FD && FD->hasAttr<CUDAGlobalAttr>() &&
          GD.getKernelReferenceKind() == KernelReferenceKind::Stub;
      if (IsDeviceStub)
        mangleDeviceStubName(II);
      else if (IsRegCall)
        mangleRegCallName(II);
      else
        mangleSourceName(II);

      writeAbiTags(ND, AdditionalAbiTags);
      break;
    }

    // Otherwise, an anonymous entity.  We must have a declaration.
    assert(ND && "mangling empty name without declaration");

    if (const NamespaceDecl *NS = dyn_cast<NamespaceDecl>(ND)) {
      if (NS->isAnonymousNamespace()) {
        // This is how gcc mangles these names.
        Out << "12_GLOBAL__N_1";
        break;
      }
    }

    if (const VarDecl *VD = dyn_cast<VarDecl>(ND)) {
      // We must have an anonymous union or struct declaration.
      const RecordDecl *RD = VD->getType()->castAs<RecordType>()->getDecl();

      // Itanium C++ ABI 5.1.2:
      //
      //   For the purposes of mangling, the name of an anonymous union is
      //   considered to be the name of the first named data member found by a
      //   pre-order, depth-first, declaration-order walk of the data members of
      //   the anonymous union. If there is no such data member (i.e., if all of
      //   the data members in the union are unnamed), then there is no way for
      //   a program to refer to the anonymous union, and there is therefore no
      //   need to mangle its name.
      assert(RD->isAnonymousStructOrUnion()
             && "Expected anonymous struct or union!");
      const FieldDecl *FD = RD->findFirstNamedDataMember();

      // It's actually possible for various reasons for us to get here
      // with an empty anonymous struct / union.  Fortunately, it
      // doesn't really matter what name we generate.
      if (!FD) break;
      assert(FD->getIdentifier() && "Data member name isn't an identifier!");

      mangleSourceName(FD->getIdentifier());
      // Not emitting abi tags: internal name anyway.
      break;
    }

    // Class extensions have no name as a category, and it's possible
    // for them to be the semantic parent of certain declarations
    // (primarily, tag decls defined within declarations).  Such
    // declarations will always have internal linkage, so the name
    // doesn't really matter, but we shouldn't crash on them.  For
    // safety, just handle all ObjC containers here.
    if (isa<ObjCContainerDecl>(ND))
      break;

    // We must have an anonymous struct.
    const TagDecl *TD = cast<TagDecl>(ND);
    if (const TypedefNameDecl *D = TD->getTypedefNameForAnonDecl()) {
      assert(TD->getDeclContext() == D->getDeclContext() &&
             "Typedef should not be in another decl context!");
      assert(D->getDeclName().getAsIdentifierInfo() &&
             "Typedef was not named!");
      mangleSourceName(D->getDeclName().getAsIdentifierInfo());
      assert(!AdditionalAbiTags && "Type cannot have additional abi tags");
      // Explicit abi tags are still possible; take from underlying type, not
      // from typedef.
      writeAbiTags(TD, nullptr);
      break;
    }

    // <unnamed-type-name> ::= <closure-type-name>
    //
    // <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
    // <lambda-sig> ::= <template-param-decl>* <parameter-type>+
    //     # Parameter types or 'v' for 'void'.
    if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(TD)) {
      std::optional<unsigned> DeviceNumber =
          Context.getDiscriminatorOverride()(Context.getASTContext(), Record);

      // If we have a device-number via the discriminator, use that to mangle
      // the lambda, otherwise use the typical lambda-mangling-number. In either
      // case, a '0' should be mangled as a normal unnamed class instead of as a
      // lambda.
      if (Record->isLambda() &&
          ((DeviceNumber && *DeviceNumber > 0) ||
           (!DeviceNumber && Record->getLambdaManglingNumber() > 0))) {
        assert(!AdditionalAbiTags &&
               "Lambda type cannot have additional abi tags");
        mangleLambda(Record);
        break;
      }
    }

    if (TD->isExternallyVisible()) {
      unsigned UnnamedMangle =
          getASTContext().getManglingNumber(TD, Context.isAux());
      Out << "Ut";
      if (UnnamedMangle > 1)
        Out << UnnamedMangle - 2;
      Out << '_';
      writeAbiTags(TD, AdditionalAbiTags);
      break;
    }

    // Get a unique id for the anonymous struct. If it is not a real output
    // ID doesn't matter so use fake one.
    unsigned AnonStructId =
        NullOut ? 0
                : Context.getAnonymousStructId(TD, dyn_cast<FunctionDecl>(DC));

    // Mangle it as a source name in the form
    // [n] $_<id>
    // where n is the length of the string.
    SmallString<8> Str;
    Str += "$_";
    Str += llvm::utostr(AnonStructId);

    Out << Str.size();
    Out << Str;
    break;
  }

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    llvm_unreachable("Can't mangle Objective-C selector names here!");

  case DeclarationName::CXXConstructorName: {
    const CXXRecordDecl *InheritedFrom = nullptr;
    TemplateName InheritedTemplateName;
    const TemplateArgumentList *InheritedTemplateArgs = nullptr;
    if (auto Inherited =
            cast<CXXConstructorDecl>(ND)->getInheritedConstructor()) {
      InheritedFrom = Inherited.getConstructor()->getParent();
      InheritedTemplateName =
          TemplateName(Inherited.getConstructor()->getPrimaryTemplate());
      InheritedTemplateArgs =
          Inherited.getConstructor()->getTemplateSpecializationArgs();
    }

    if (ND == Structor)
      // If the named decl is the C++ constructor we're mangling, use the type
      // we were given.
      mangleCXXCtorType(static_cast<CXXCtorType>(StructorType), InheritedFrom);
    else
      // Otherwise, use the complete constructor name. This is relevant if a
      // class with a constructor is declared within a constructor.
      mangleCXXCtorType(Ctor_Complete, InheritedFrom);

    // FIXME: The template arguments are part of the enclosing prefix or
    // nested-name, but it's more convenient to mangle them here.
    if (InheritedTemplateArgs)
      mangleTemplateArgs(InheritedTemplateName, *InheritedTemplateArgs);

    writeAbiTags(ND, AdditionalAbiTags);
    break;
  }

  case DeclarationName::CXXDestructorName:
    if (ND == Structor)
      // If the named decl is the C++ destructor we're mangling, use the type we
      // were given.
      mangleCXXDtorType(static_cast<CXXDtorType>(StructorType));
    else
      // Otherwise, use the complete destructor name. This is relevant if a
      // class with a destructor is declared within a destructor.
      mangleCXXDtorType(Dtor_Complete);
    assert(ND);
    writeAbiTags(ND, AdditionalAbiTags);
    break;

  case DeclarationName::CXXOperatorName:
    if (ND && Arity == UnknownArity) {
      Arity = cast<FunctionDecl>(ND)->getNumParams();

      // If we have a member function, we need to include the 'this' pointer.
      if (const auto *MD = dyn_cast<CXXMethodDecl>(ND))
        if (MD->isImplicitObjectMemberFunction())
          Arity++;
    }
    [[fallthrough]];
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXLiteralOperatorName:
    mangleOperatorName(Name, Arity);
    writeAbiTags(ND, AdditionalAbiTags);
    break;

  case DeclarationName::CXXDeductionGuideName:
    llvm_unreachable("Can't mangle a deduction guide name!");

  case DeclarationName::CXXUsingDirective:
    llvm_unreachable("Can't mangle a using directive name!");
  }
}

void CXXNameMangler::mangleRegCallName(const IdentifierInfo *II) {
  // <source-name> ::= <positive length number> __regcall3__ <identifier>
  // <number> ::= [n] <non-negative decimal integer>
  // <identifier> ::= <unqualified source code identifier>
  if (getASTContext().getLangOpts().RegCall4)
    Out << II->getLength() + sizeof("__regcall4__") - 1 << "__regcall4__"
        << II->getName();
  else
    Out << II->getLength() + sizeof("__regcall3__") - 1 << "__regcall3__"
        << II->getName();
}

void CXXNameMangler::mangleDeviceStubName(const IdentifierInfo *II) {
  // <source-name> ::= <positive length number> __device_stub__ <identifier>
  // <number> ::= [n] <non-negative decimal integer>
  // <identifier> ::= <unqualified source code identifier>
  Out << II->getLength() + sizeof("__device_stub__") - 1 << "__device_stub__"
      << II->getName();
}

void CXXNameMangler::mangleSourceName(const IdentifierInfo *II) {
  // <source-name> ::= <positive length number> <identifier>
  // <number> ::= [n] <non-negative decimal integer>
  // <identifier> ::= <unqualified source code identifier>
  Out << II->getLength() << II->getName();
}

void CXXNameMangler::mangleNestedName(GlobalDecl GD,
                                      const DeclContext *DC,
                                      const AbiTagList *AdditionalAbiTags,
                                      bool NoFunction) {
  const NamedDecl *ND = cast<NamedDecl>(GD.getDecl());
  // <nested-name>
  //   ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
  //   ::= N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix>
  //       <template-args> E

  Out << 'N';
  if (const CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(ND)) {
    Qualifiers MethodQuals = Method->getMethodQualifiers();
    // We do not consider restrict a distinguishing attribute for overloading
    // purposes so we must not mangle it.
    if (Method->isExplicitObjectMemberFunction())
      Out << 'H';
    MethodQuals.removeRestrict();
    mangleQualifiers(MethodQuals);
    mangleRefQualifier(Method->getRefQualifier());
  }

  // Check if we have a template.
  const TemplateArgumentList *TemplateArgs = nullptr;
  if (GlobalDecl TD = isTemplate(GD, TemplateArgs)) {
    mangleTemplatePrefix(TD, NoFunction);
    mangleTemplateArgs(asTemplateName(TD), *TemplateArgs);
  } else {
    manglePrefix(DC, NoFunction);
    mangleUnqualifiedName(GD, DC, AdditionalAbiTags);
  }

  Out << 'E';
}
void CXXNameMangler::mangleNestedName(const TemplateDecl *TD,
                                      ArrayRef<TemplateArgument> Args) {
  // <nested-name> ::= N [<CV-qualifiers>] <template-prefix> <template-args> E

  Out << 'N';

  mangleTemplatePrefix(TD);
  mangleTemplateArgs(asTemplateName(TD), Args);

  Out << 'E';
}

void CXXNameMangler::mangleNestedNameWithClosurePrefix(
    GlobalDecl GD, const NamedDecl *PrefixND,
    const AbiTagList *AdditionalAbiTags) {
  // A <closure-prefix> represents a variable or field, not a regular
  // DeclContext, so needs special handling. In this case we're mangling a
  // limited form of <nested-name>:
  //
  // <nested-name> ::= N <closure-prefix> <closure-type-name> E

  Out << 'N';

  mangleClosurePrefix(PrefixND);
  mangleUnqualifiedName(GD, nullptr, AdditionalAbiTags);

  Out << 'E';
}

static GlobalDecl getParentOfLocalEntity(const DeclContext *DC) {
  GlobalDecl GD;
  // The Itanium spec says:
  // For entities in constructors and destructors, the mangling of the
  // complete object constructor or destructor is used as the base function
  // name, i.e. the C1 or D1 version.
  if (auto *CD = dyn_cast<CXXConstructorDecl>(DC))
    GD = GlobalDecl(CD, Ctor_Complete);
  else if (auto *DD = dyn_cast<CXXDestructorDecl>(DC))
    GD = GlobalDecl(DD, Dtor_Complete);
  else
    GD = GlobalDecl(cast<FunctionDecl>(DC));
  return GD;
}

void CXXNameMangler::mangleLocalName(GlobalDecl GD,
                                     const AbiTagList *AdditionalAbiTags) {
  const Decl *D = GD.getDecl();
  // <local-name> := Z <function encoding> E <entity name> [<discriminator>]
  //              := Z <function encoding> E s [<discriminator>]
  // <local-name> := Z <function encoding> E d [ <parameter number> ]
  //                 _ <entity name>
  // <discriminator> := _ <non-negative number>
  assert(isa<NamedDecl>(D) || isa<BlockDecl>(D));
  const RecordDecl *RD = GetLocalClassDecl(D);
  const DeclContext *DC = Context.getEffectiveDeclContext(RD ? RD : D);

  Out << 'Z';

  {
    AbiTagState LocalAbiTags(AbiTags);

    if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(DC))
      mangleObjCMethodName(MD);
    else if (const BlockDecl *BD = dyn_cast<BlockDecl>(DC))
      mangleBlockForPrefix(BD);
    else
      mangleFunctionEncoding(getParentOfLocalEntity(DC));

    // Implicit ABI tags (from namespace) are not available in the following
    // entity; reset to actually emitted tags, which are available.
    LocalAbiTags.setUsedAbiTags(LocalAbiTags.getEmittedAbiTags());
  }

  Out << 'E';

  // GCC 5.3.0 doesn't emit derived ABI tags for local names but that seems to
  // be a bug that is fixed in trunk.

  if (RD) {
    // The parameter number is omitted for the last parameter, 0 for the
    // second-to-last parameter, 1 for the third-to-last parameter, etc. The
    // <entity name> will of course contain a <closure-type-name>: Its
    // numbering will be local to the particular argument in which it appears
    // -- other default arguments do not affect its encoding.
    const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD);
    if (CXXRD && CXXRD->isLambda()) {
      if (const ParmVarDecl *Parm
              = dyn_cast_or_null<ParmVarDecl>(CXXRD->getLambdaContextDecl())) {
        if (const FunctionDecl *Func
              = dyn_cast<FunctionDecl>(Parm->getDeclContext())) {
          Out << 'd';
          unsigned Num = Func->getNumParams() - Parm->getFunctionScopeIndex();
          if (Num > 1)
            mangleNumber(Num - 2);
          Out << '_';
        }
      }
    }

    // Mangle the name relative to the closest enclosing function.
    // equality ok because RD derived from ND above
    if (D == RD)  {
      mangleUnqualifiedName(RD, DC, AdditionalAbiTags);
    } else if (const BlockDecl *BD = dyn_cast<BlockDecl>(D)) {
      if (const NamedDecl *PrefixND = getClosurePrefix(BD))
        mangleClosurePrefix(PrefixND, true /*NoFunction*/);
      else
        manglePrefix(Context.getEffectiveDeclContext(BD), true /*NoFunction*/);
      assert(!AdditionalAbiTags && "Block cannot have additional abi tags");
      mangleUnqualifiedBlock(BD);
    } else {
      const NamedDecl *ND = cast<NamedDecl>(D);
      mangleNestedName(GD, Context.getEffectiveDeclContext(ND),
                       AdditionalAbiTags, true /*NoFunction*/);
    }
  } else if (const BlockDecl *BD = dyn_cast<BlockDecl>(D)) {
    // Mangle a block in a default parameter; see above explanation for
    // lambdas.
    if (const ParmVarDecl *Parm
            = dyn_cast_or_null<ParmVarDecl>(BD->getBlockManglingContextDecl())) {
      if (const FunctionDecl *Func
            = dyn_cast<FunctionDecl>(Parm->getDeclContext())) {
        Out << 'd';
        unsigned Num = Func->getNumParams() - Parm->getFunctionScopeIndex();
        if (Num > 1)
          mangleNumber(Num - 2);
        Out << '_';
      }
    }

    assert(!AdditionalAbiTags && "Block cannot have additional abi tags");
    mangleUnqualifiedBlock(BD);
  } else {
    mangleUnqualifiedName(GD, DC, AdditionalAbiTags);
  }

  if (const NamedDecl *ND = dyn_cast<NamedDecl>(RD ? RD : D)) {
    unsigned disc;
    if (Context.getNextDiscriminator(ND, disc)) {
      if (disc < 10)
        Out << '_' << disc;
      else
        Out << "__" << disc << '_';
    }
  }
}

void CXXNameMangler::mangleBlockForPrefix(const BlockDecl *Block) {
  if (GetLocalClassDecl(Block)) {
    mangleLocalName(Block, /* AdditionalAbiTags */ nullptr);
    return;
  }
  const DeclContext *DC = Context.getEffectiveDeclContext(Block);
  if (isLocalContainerContext(DC)) {
    mangleLocalName(Block, /* AdditionalAbiTags */ nullptr);
    return;
  }
  if (const NamedDecl *PrefixND = getClosurePrefix(Block))
    mangleClosurePrefix(PrefixND);
  else
    manglePrefix(DC);
  mangleUnqualifiedBlock(Block);
}

void CXXNameMangler::mangleUnqualifiedBlock(const BlockDecl *Block) {
  // When trying to be ABI-compatibility with clang 12 and before, mangle a
  // <data-member-prefix> now, with no substitutions and no <template-args>.
  if (Decl *Context = Block->getBlockManglingContextDecl()) {
    if (isCompatibleWith(LangOptions::ClangABI::Ver12) &&
        (isa<VarDecl>(Context) || isa<FieldDecl>(Context)) &&
        Context->getDeclContext()->isRecord()) {
      const auto *ND = cast<NamedDecl>(Context);
      if (ND->getIdentifier()) {
        mangleSourceNameWithAbiTags(ND);
        Out << 'M';
      }
    }
  }

  // If we have a block mangling number, use it.
  unsigned Number = Block->getBlockManglingNumber();
  // Otherwise, just make up a number. It doesn't matter what it is because
  // the symbol in question isn't externally visible.
  if (!Number)
    Number = Context.getBlockId(Block, false);
  else {
    // Stored mangling numbers are 1-based.
    --Number;
  }
  Out << "Ub";
  if (Number > 0)
    Out << Number - 1;
  Out << '_';
}

// <template-param-decl>
//   ::= Ty                                  # template type parameter
//   ::= Tk <concept name> [<template-args>] # constrained type parameter
//   ::= Tn <type>                           # template non-type parameter
//   ::= Tt <template-param-decl>* E [Q <requires-clause expr>]
//                                           # template template parameter
//   ::= Tp <template-param-decl>            # template parameter pack
void CXXNameMangler::mangleTemplateParamDecl(const NamedDecl *Decl) {
  // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/47.
  if (auto *Ty = dyn_cast<TemplateTypeParmDecl>(Decl)) {
    if (Ty->isParameterPack())
      Out << "Tp";
    const TypeConstraint *Constraint = Ty->getTypeConstraint();
    if (Constraint && !isCompatibleWith(LangOptions::ClangABI::Ver17)) {
      // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/24.
      Out << "Tk";
      mangleTypeConstraint(Constraint);
    } else {
      Out << "Ty";
    }
  } else if (auto *Tn = dyn_cast<NonTypeTemplateParmDecl>(Decl)) {
    if (Tn->isExpandedParameterPack()) {
      for (unsigned I = 0, N = Tn->getNumExpansionTypes(); I != N; ++I) {
        Out << "Tn";
        mangleType(Tn->getExpansionType(I));
      }
    } else {
      QualType T = Tn->getType();
      if (Tn->isParameterPack()) {
        Out << "Tp";
        if (auto *PackExpansion = T->getAs<PackExpansionType>())
          T = PackExpansion->getPattern();
      }
      Out << "Tn";
      mangleType(T);
    }
  } else if (auto *Tt = dyn_cast<TemplateTemplateParmDecl>(Decl)) {
    if (Tt->isExpandedParameterPack()) {
      for (unsigned I = 0, N = Tt->getNumExpansionTemplateParameters(); I != N;
           ++I)
        mangleTemplateParameterList(Tt->getExpansionTemplateParameters(I));
    } else {
      if (Tt->isParameterPack())
        Out << "Tp";
      mangleTemplateParameterList(Tt->getTemplateParameters());
    }
  }
}

void CXXNameMangler::mangleTemplateParameterList(
    const TemplateParameterList *Params) {
  Out << "Tt";
  for (auto *Param : *Params)
    mangleTemplateParamDecl(Param);
  mangleRequiresClause(Params->getRequiresClause());
  Out << "E";
}

void CXXNameMangler::mangleTypeConstraint(
    const ConceptDecl *Concept, ArrayRef<TemplateArgument> Arguments) {
  const DeclContext *DC = Context.getEffectiveDeclContext(Concept);
  if (!Arguments.empty())
    mangleTemplateName(Concept, Arguments);
  else if (DC->isTranslationUnit() || isStdNamespace(DC))
    mangleUnscopedName(Concept, DC, nullptr);
  else
    mangleNestedName(Concept, DC, nullptr);
}

void CXXNameMangler::mangleTypeConstraint(const TypeConstraint *Constraint) {
  llvm::SmallVector<TemplateArgument, 8> Args;
  if (Constraint->getTemplateArgsAsWritten()) {
    for (const TemplateArgumentLoc &ArgLoc :
         Constraint->getTemplateArgsAsWritten()->arguments())
      Args.push_back(ArgLoc.getArgument());
  }
  return mangleTypeConstraint(Constraint->getNamedConcept(), Args);
}

void CXXNameMangler::mangleRequiresClause(const Expr *RequiresClause) {
  // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/24.
  if (RequiresClause && !isCompatibleWith(LangOptions::ClangABI::Ver17)) {
    Out << 'Q';
    mangleExpression(RequiresClause);
  }
}

void CXXNameMangler::mangleLambda(const CXXRecordDecl *Lambda) {
  // When trying to be ABI-compatibility with clang 12 and before, mangle a
  // <data-member-prefix> now, with no substitutions.
  if (Decl *Context = Lambda->getLambdaContextDecl()) {
    if (isCompatibleWith(LangOptions::ClangABI::Ver12) &&
        (isa<VarDecl>(Context) || isa<FieldDecl>(Context)) &&
        !isa<ParmVarDecl>(Context)) {
      if (const IdentifierInfo *Name
            = cast<NamedDecl>(Context)->getIdentifier()) {
        mangleSourceName(Name);
        const TemplateArgumentList *TemplateArgs = nullptr;
        if (GlobalDecl TD = isTemplate(cast<NamedDecl>(Context), TemplateArgs))
          mangleTemplateArgs(asTemplateName(TD), *TemplateArgs);
        Out << 'M';
      }
    }
  }

  Out << "Ul";
  mangleLambdaSig(Lambda);
  Out << "E";

  // The number is omitted for the first closure type with a given
  // <lambda-sig> in a given context; it is n-2 for the nth closure type
  // (in lexical order) with that same <lambda-sig> and context.
  //
  // The AST keeps track of the number for us.
  //
  // In CUDA/HIP, to ensure the consistent lamba numbering between the device-
  // and host-side compilations, an extra device mangle context may be created
  // if the host-side CXX ABI has different numbering for lambda. In such case,
  // if the mangle context is that device-side one, use the device-side lambda
  // mangling number for this lambda.
  std::optional<unsigned> DeviceNumber =
      Context.getDiscriminatorOverride()(Context.getASTContext(), Lambda);
  unsigned Number =
      DeviceNumber ? *DeviceNumber : Lambda->getLambdaManglingNumber();

  assert(Number > 0 && "Lambda should be mangled as an unnamed class");
  if (Number > 1)
    mangleNumber(Number - 2);
  Out << '_';
}

void CXXNameMangler::mangleLambdaSig(const CXXRecordDecl *Lambda) {
  // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/31.
  for (auto *D : Lambda->getLambdaExplicitTemplateParameters())
    mangleTemplateParamDecl(D);

  // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/24.
  if (auto *TPL = Lambda->getGenericLambdaTemplateParameterList())
    mangleRequiresClause(TPL->getRequiresClause());

  auto *Proto =
      Lambda->getLambdaTypeInfo()->getType()->castAs<FunctionProtoType>();
  mangleBareFunctionType(Proto, /*MangleReturnType=*/false,
                         Lambda->getLambdaStaticInvoker());
}

void CXXNameMangler::manglePrefix(NestedNameSpecifier *qualifier) {
  switch (qualifier->getKind()) {
  case NestedNameSpecifier::Global:
    // nothing
    return;

  case NestedNameSpecifier::Super:
    llvm_unreachable("Can't mangle __super specifier");

  case NestedNameSpecifier::Namespace:
    mangleName(qualifier->getAsNamespace());
    return;

  case NestedNameSpecifier::NamespaceAlias:
    mangleName(qualifier->getAsNamespaceAlias()->getNamespace());
    return;

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    manglePrefix(QualType(qualifier->getAsType(), 0));
    return;

  case NestedNameSpecifier::Identifier:
    // Clang 14 and before did not consider this substitutable.
    bool Clang14Compat = isCompatibleWith(LangOptions::ClangABI::Ver14);
    if (!Clang14Compat && mangleSubstitution(qualifier))
      return;

    // Member expressions can have these without prefixes, but that
    // should end up in mangleUnresolvedPrefix instead.
    assert(qualifier->getPrefix());
    manglePrefix(qualifier->getPrefix());

    mangleSourceName(qualifier->getAsIdentifier());

    if (!Clang14Compat)
      addSubstitution(qualifier);
    return;
  }

  llvm_unreachable("unexpected nested name specifier");
}

void CXXNameMangler::manglePrefix(const DeclContext *DC, bool NoFunction) {
  //  <prefix> ::= <prefix> <unqualified-name>
  //           ::= <template-prefix> <template-args>
  //           ::= <closure-prefix>
  //           ::= <template-param>
  //           ::= # empty
  //           ::= <substitution>

  assert(!isa<LinkageSpecDecl>(DC) && "prefix cannot be LinkageSpecDecl");

  if (DC->isTranslationUnit())
    return;

  if (NoFunction && isLocalContainerContext(DC))
    return;

  const NamedDecl *ND = cast<NamedDecl>(DC);
  if (mangleSubstitution(ND))
    return;

  // Check if we have a template-prefix or a closure-prefix.
  const TemplateArgumentList *TemplateArgs = nullptr;
  if (GlobalDecl TD = isTemplate(ND, TemplateArgs)) {
    mangleTemplatePrefix(TD);
    mangleTemplateArgs(asTemplateName(TD), *TemplateArgs);
  } else if (const NamedDecl *PrefixND = getClosurePrefix(ND)) {
    mangleClosurePrefix(PrefixND, NoFunction);
    mangleUnqualifiedName(ND, nullptr, nullptr);
  } else {
    const DeclContext *DC = Context.getEffectiveDeclContext(ND);
    manglePrefix(DC, NoFunction);
    mangleUnqualifiedName(ND, DC, nullptr);
  }

  addSubstitution(ND);
}

void CXXNameMangler::mangleTemplatePrefix(TemplateName Template) {
  // <template-prefix> ::= <prefix> <template unqualified-name>
  //                   ::= <template-param>
  //                   ::= <substitution>
  if (TemplateDecl *TD = Template.getAsTemplateDecl())
    return mangleTemplatePrefix(TD);

  DependentTemplateName *Dependent = Template.getAsDependentTemplateName();
  assert(Dependent && "unexpected template name kind");

  // Clang 11 and before mangled the substitution for a dependent template name
  // after already having emitted (a substitution for) the prefix.
  bool Clang11Compat = isCompatibleWith(LangOptions::ClangABI::Ver11);
  if (!Clang11Compat && mangleSubstitution(Template))
    return;

  if (NestedNameSpecifier *Qualifier = Dependent->getQualifier())
    manglePrefix(Qualifier);

  if (Clang11Compat && mangleSubstitution(Template))
    return;

  if (const IdentifierInfo *Id = Dependent->getIdentifier())
    mangleSourceName(Id);
  else
    mangleOperatorName(Dependent->getOperator(), UnknownArity);

  addSubstitution(Template);
}

void CXXNameMangler::mangleTemplatePrefix(GlobalDecl GD,
                                          bool NoFunction) {
  const TemplateDecl *ND = cast<TemplateDecl>(GD.getDecl());
  // <template-prefix> ::= <prefix> <template unqualified-name>
  //                   ::= <template-param>
  //                   ::= <substitution>
  // <template-template-param> ::= <template-param>
  //                               <substitution>

  if (mangleSubstitution(ND))
    return;

  // <template-template-param> ::= <template-param>
  if (const auto *TTP = dyn_cast<TemplateTemplateParmDecl>(ND)) {
    mangleTemplateParameter(TTP->getDepth(), TTP->getIndex());
  } else {
    const DeclContext *DC = Context.getEffectiveDeclContext(ND);
    manglePrefix(DC, NoFunction);
    if (isa<BuiltinTemplateDecl>(ND) || isa<ConceptDecl>(ND))
      mangleUnqualifiedName(GD, DC, nullptr);
    else
      mangleUnqualifiedName(GD.getWithDecl(ND->getTemplatedDecl()), DC,
                            nullptr);
  }

  addSubstitution(ND);
}

const NamedDecl *CXXNameMangler::getClosurePrefix(const Decl *ND) {
  if (isCompatibleWith(LangOptions::ClangABI::Ver12))
    return nullptr;

  const NamedDecl *Context = nullptr;
  if (auto *Block = dyn_cast<BlockDecl>(ND)) {
    Context = dyn_cast_or_null<NamedDecl>(Block->getBlockManglingContextDecl());
  } else if (auto *RD = dyn_cast<CXXRecordDecl>(ND)) {
    if (RD->isLambda())
      Context = dyn_cast_or_null<NamedDecl>(RD->getLambdaContextDecl());
  }
  if (!Context)
    return nullptr;

  // Only lambdas within the initializer of a non-local variable or non-static
  // data member get a <closure-prefix>.
  if ((isa<VarDecl>(Context) && cast<VarDecl>(Context)->hasGlobalStorage()) ||
      isa<FieldDecl>(Context))
    return Context;

  return nullptr;
}

void CXXNameMangler::mangleClosurePrefix(const NamedDecl *ND, bool NoFunction) {
  //  <closure-prefix> ::= [ <prefix> ] <unqualified-name> M
  //                   ::= <template-prefix> <template-args> M
  if (mangleSubstitution(ND))
    return;

  const TemplateArgumentList *TemplateArgs = nullptr;
  if (GlobalDecl TD = isTemplate(ND, TemplateArgs)) {
    mangleTemplatePrefix(TD, NoFunction);
    mangleTemplateArgs(asTemplateName(TD), *TemplateArgs);
  } else {
    const auto *DC = Context.getEffectiveDeclContext(ND);
    manglePrefix(DC, NoFunction);
    mangleUnqualifiedName(ND, DC, nullptr);
  }

  Out << 'M';

  addSubstitution(ND);
}

/// Mangles a template name under the production <type>.  Required for
/// template template arguments.
///   <type> ::= <class-enum-type>
///          ::= <template-param>
///          ::= <substitution>
void CXXNameMangler::mangleType(TemplateName TN) {
  if (mangleSubstitution(TN))
    return;

  TemplateDecl *TD = nullptr;

  switch (TN.getKind()) {
  case TemplateName::QualifiedTemplate:
  case TemplateName::UsingTemplate:
  case TemplateName::Template:
    TD = TN.getAsTemplateDecl();
    goto HaveDecl;

  HaveDecl:
    if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(TD))
      mangleTemplateParameter(TTP->getDepth(), TTP->getIndex());
    else
      mangleName(TD);
    break;

  case TemplateName::OverloadedTemplate:
  case TemplateName::AssumedTemplate:
    llvm_unreachable("can't mangle an overloaded template name as a <type>");

  case TemplateName::DependentTemplate: {
    const DependentTemplateName *Dependent = TN.getAsDependentTemplateName();
    assert(Dependent->isIdentifier());

    // <class-enum-type> ::= <name>
    // <name> ::= <nested-name>
    mangleUnresolvedPrefix(Dependent->getQualifier());
    mangleSourceName(Dependent->getIdentifier());
    break;
  }

  case TemplateName::SubstTemplateTemplateParm: {
    // Substituted template parameters are mangled as the substituted
    // template.  This will check for the substitution twice, which is
    // fine, but we have to return early so that we don't try to *add*
    // the substitution twice.
    SubstTemplateTemplateParmStorage *subst
      = TN.getAsSubstTemplateTemplateParm();
    mangleType(subst->getReplacement());
    return;
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    // FIXME: not clear how to mangle this!
    // template <template <class> class T...> class A {
    //   template <template <class> class U...> void foo(B<T,U> x...);
    // };
    Out << "_SUBSTPACK_";
    break;
  }
  }

  addSubstitution(TN);
}

bool CXXNameMangler::mangleUnresolvedTypeOrSimpleId(QualType Ty,
                                                    StringRef Prefix) {
  // Only certain other types are valid as prefixes;  enumerate them.
  switch (Ty->getTypeClass()) {
  case Type::Builtin:
  case Type::Complex:
  case Type::Adjusted:
  case Type::Decayed:
  case Type::ArrayParameter:
  case Type::Pointer:
  case Type::BlockPointer:
  case Type::LValueReference:
  case Type::RValueReference:
  case Type::MemberPointer:
  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::DependentSizedArray:
  case Type::DependentAddressSpace:
  case Type::DependentVector:
  case Type::DependentSizedExtVector:
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::DependentSizedMatrix:
  case Type::FunctionProto:
  case Type::FunctionNoProto:
  case Type::Paren:
  case Type::Attributed:
  case Type::BTFTagAttributed:
  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
  case Type::PackExpansion:
  case Type::ObjCObject:
  case Type::ObjCInterface:
  case Type::ObjCObjectPointer:
  case Type::ObjCTypeParam:
  case Type::Atomic:
  case Type::Pipe:
  case Type::MacroQualified:
  case Type::BitInt:
  case Type::DependentBitInt:
  case Type::CountAttributed:
    llvm_unreachable("type is illegal as a nested name specifier");

  case Type::SubstTemplateTypeParmPack:
    // FIXME: not clear how to mangle this!
    // template <class T...> class A {
    //   template <class U...> void foo(decltype(T::foo(U())) x...);
    // };
    Out << "_SUBSTPACK_";
    break;

  // <unresolved-type> ::= <template-param>
  //                   ::= <decltype>
  //                   ::= <template-template-param> <template-args>
  // (this last is not official yet)
  case Type::TypeOfExpr:
  case Type::TypeOf:
  case Type::Decltype:
  case Type::PackIndexing:
  case Type::TemplateTypeParm:
  case Type::UnaryTransform:
  case Type::SubstTemplateTypeParm:
  unresolvedType:
    // Some callers want a prefix before the mangled type.
    Out << Prefix;

    // This seems to do everything we want.  It's not really
    // sanctioned for a substituted template parameter, though.
    mangleType(Ty);

    // We never want to print 'E' directly after an unresolved-type,
    // so we return directly.
    return true;

  case Type::Typedef:
    mangleSourceNameWithAbiTags(cast<TypedefType>(Ty)->getDecl());
    break;

  case Type::UnresolvedUsing:
    mangleSourceNameWithAbiTags(
        cast<UnresolvedUsingType>(Ty)->getDecl());
    break;

  case Type::Enum:
  case Type::Record:
    mangleSourceNameWithAbiTags(cast<TagType>(Ty)->getDecl());
    break;

  case Type::TemplateSpecialization: {
    const TemplateSpecializationType *TST =
        cast<TemplateSpecializationType>(Ty);
    TemplateName TN = TST->getTemplateName();
    switch (TN.getKind()) {
    case TemplateName::Template:
    case TemplateName::QualifiedTemplate: {
      TemplateDecl *TD = TN.getAsTemplateDecl();

      // If the base is a template template parameter, this is an
      // unresolved type.
      assert(TD && "no template for template specialization type");
      if (isa<TemplateTemplateParmDecl>(TD))
        goto unresolvedType;

      mangleSourceNameWithAbiTags(TD);
      break;
    }

    case TemplateName::OverloadedTemplate:
    case TemplateName::AssumedTemplate:
    case TemplateName::DependentTemplate:
      llvm_unreachable("invalid base for a template specialization type");

    case TemplateName::SubstTemplateTemplateParm: {
      SubstTemplateTemplateParmStorage *subst =
          TN.getAsSubstTemplateTemplateParm();
      mangleExistingSubstitution(subst->getReplacement());
      break;
    }

    case TemplateName::SubstTemplateTemplateParmPack: {
      // FIXME: not clear how to mangle this!
      // template <template <class U> class T...> class A {
      //   template <class U...> void foo(decltype(T<U>::foo) x...);
      // };
      Out << "_SUBSTPACK_";
      break;
    }
    case TemplateName::UsingTemplate: {
      TemplateDecl *TD = TN.getAsTemplateDecl();
      assert(TD && !isa<TemplateTemplateParmDecl>(TD));
      mangleSourceNameWithAbiTags(TD);
      break;
    }
    }

    // Note: we don't pass in the template name here. We are mangling the
    // original source-level template arguments, so we shouldn't consider
    // conversions to the corresponding template parameter.
    // FIXME: Other compilers mangle partially-resolved template arguments in
    // unresolved-qualifier-levels.
    mangleTemplateArgs(TemplateName(), TST->template_arguments());
    break;
  }

  case Type::InjectedClassName:
    mangleSourceNameWithAbiTags(
        cast<InjectedClassNameType>(Ty)->getDecl());
    break;

  case Type::DependentName:
    mangleSourceName(cast<DependentNameType>(Ty)->getIdentifier());
    break;

  case Type::DependentTemplateSpecialization: {
    const DependentTemplateSpecializationType *DTST =
        cast<DependentTemplateSpecializationType>(Ty);
    TemplateName Template = getASTContext().getDependentTemplateName(
        DTST->getQualifier(), DTST->getIdentifier());
    mangleSourceName(DTST->getIdentifier());
    mangleTemplateArgs(Template, DTST->template_arguments());
    break;
  }

  case Type::Using:
    return mangleUnresolvedTypeOrSimpleId(cast<UsingType>(Ty)->desugar(),
                                          Prefix);
  case Type::Elaborated:
    return mangleUnresolvedTypeOrSimpleId(
        cast<ElaboratedType>(Ty)->getNamedType(), Prefix);
  }

  return false;
}

void CXXNameMangler::mangleOperatorName(DeclarationName Name, unsigned Arity) {
  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXDeductionGuideName:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::Identifier:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCZeroArgSelector:
    llvm_unreachable("Not an operator name");

  case DeclarationName::CXXConversionFunctionName:
    // <operator-name> ::= cv <type>    # (cast)
    Out << "cv";
    mangleType(Name.getCXXNameType());
    break;

  case DeclarationName::CXXLiteralOperatorName:
    Out << "li";
    mangleSourceName(Name.getCXXLiteralIdentifier());
    return;

  case DeclarationName::CXXOperatorName:
    mangleOperatorName(Name.getCXXOverloadedOperator(), Arity);
    break;
  }
}

void
CXXNameMangler::mangleOperatorName(OverloadedOperatorKind OO, unsigned Arity) {
  switch (OO) {
  // <operator-name> ::= nw     # new
  case OO_New: Out << "nw"; break;
  //              ::= na        # new[]
  case OO_Array_New: Out << "na"; break;
  //              ::= dl        # delete
  case OO_Delete: Out << "dl"; break;
  //              ::= da        # delete[]
  case OO_Array_Delete: Out << "da"; break;
  //              ::= ps        # + (unary)
  //              ::= pl        # + (binary or unknown)
  case OO_Plus:
    Out << (Arity == 1? "ps" : "pl"); break;
  //              ::= ng        # - (unary)
  //              ::= mi        # - (binary or unknown)
  case OO_Minus:
    Out << (Arity == 1? "ng" : "mi"); break;
  //              ::= ad        # & (unary)
  //              ::= an        # & (binary or unknown)
  case OO_Amp:
    Out << (Arity == 1? "ad" : "an"); break;
  //              ::= de        # * (unary)
  //              ::= ml        # * (binary or unknown)
  case OO_Star:
    // Use binary when unknown.
    Out << (Arity == 1? "de" : "ml"); break;
  //              ::= co        # ~
  case OO_Tilde: Out << "co"; break;
  //              ::= dv        # /
  case OO_Slash: Out << "dv"; break;
  //              ::= rm        # %
  case OO_Percent: Out << "rm"; break;
  //              ::= or        # |
  case OO_Pipe: Out << "or"; break;
  //              ::= eo        # ^
  case OO_Caret: Out << "eo"; break;
  //              ::= aS        # =
  case OO_Equal: Out << "aS"; break;
  //              ::= pL        # +=
  case OO_PlusEqual: Out << "pL"; break;
  //              ::= mI        # -=
  case OO_MinusEqual: Out << "mI"; break;
  //              ::= mL        # *=
  case OO_StarEqual: Out << "mL"; break;
  //              ::= dV        # /=
  case OO_SlashEqual: Out << "dV"; break;
  //              ::= rM        # %=
  case OO_PercentEqual: Out << "rM"; break;
  //              ::= aN        # &=
  case OO_AmpEqual: Out << "aN"; break;
  //              ::= oR        # |=
  case OO_PipeEqual: Out << "oR"; break;
  //              ::= eO        # ^=
  case OO_CaretEqual: Out << "eO"; break;
  //              ::= ls        # <<
  case OO_LessLess: Out << "ls"; break;
  //              ::= rs        # >>
  case OO_GreaterGreater: Out << "rs"; break;
  //              ::= lS        # <<=
  case OO_LessLessEqual: Out << "lS"; break;
  //              ::= rS        # >>=
  case OO_GreaterGreaterEqual: Out << "rS"; break;
  //              ::= eq        # ==
  case OO_EqualEqual: Out << "eq"; break;
  //              ::= ne        # !=
  case OO_ExclaimEqual: Out << "ne"; break;
  //              ::= lt        # <
  case OO_Less: Out << "lt"; break;
  //              ::= gt        # >
  case OO_Greater: Out << "gt"; break;
  //              ::= le        # <=
  case OO_LessEqual: Out << "le"; break;
  //              ::= ge        # >=
  case OO_GreaterEqual: Out << "ge"; break;
  //              ::= nt        # !
  case OO_Exclaim: Out << "nt"; break;
  //              ::= aa        # &&
  case OO_AmpAmp: Out << "aa"; break;
  //              ::= oo        # ||
  case OO_PipePipe: Out << "oo"; break;
  //              ::= pp        # ++
  case OO_PlusPlus: Out << "pp"; break;
  //              ::= mm        # --
  case OO_MinusMinus: Out << "mm"; break;
  //              ::= cm        # ,
  case OO_Comma: Out << "cm"; break;
  //              ::= pm        # ->*
  case OO_ArrowStar: Out << "pm"; break;
  //              ::= pt        # ->
  case OO_Arrow: Out << "pt"; break;
  //              ::= cl        # ()
  case OO_Call: Out << "cl"; break;
  //              ::= ix        # []
  case OO_Subscript: Out << "ix"; break;

  //              ::= qu        # ?
  // The conditional operator can't be overloaded, but we still handle it when
  // mangling expressions.
  case OO_Conditional: Out << "qu"; break;
  // Proposal on cxx-abi-dev, 2015-10-21.
  //              ::= aw        # co_await
  case OO_Coawait: Out << "aw"; break;
  // Proposed in cxx-abi github issue 43.
  //              ::= ss        # <=>
  case OO_Spaceship: Out << "ss"; break;

  case OO_None:
  case NUM_OVERLOADED_OPERATORS:
    llvm_unreachable("Not an overloaded operator");
  }
}

void CXXNameMangler::mangleQualifiers(Qualifiers Quals, const DependentAddressSpaceType *DAST) {
  // Vendor qualifiers come first and if they are order-insensitive they must
  // be emitted in reversed alphabetical order, see Itanium ABI 5.1.5.

  // <type> ::= U <addrspace-expr>
  if (DAST) {
    Out << "U2ASI";
    mangleExpression(DAST->getAddrSpaceExpr());
    Out << "E";
  }

  // Address space qualifiers start with an ordinary letter.
  if (Quals.hasAddressSpace()) {
    // Address space extension:
    //
    //   <type> ::= U <target-addrspace>
    //   <type> ::= U <OpenCL-addrspace>
    //   <type> ::= U <CUDA-addrspace>

    SmallString<64> ASString;
    LangAS AS = Quals.getAddressSpace();

    if (Context.getASTContext().addressSpaceMapManglingFor(AS)) {
      //  <target-addrspace> ::= "AS" <address-space-number>
      unsigned TargetAS = Context.getASTContext().getTargetAddressSpace(AS);
      if (TargetAS != 0 ||
          Context.getASTContext().getTargetAddressSpace(LangAS::Default) != 0)
        ASString = "AS" + llvm::utostr(TargetAS);
    } else {
      switch (AS) {
      default: llvm_unreachable("Not a language specific address space");
      //  <OpenCL-addrspace> ::= "CL" [ "global" | "local" | "constant" |
      //                                "private"| "generic" | "device" |
      //                                "host" ]
      case LangAS::opencl_global:
        ASString = "CLglobal";
        break;
      case LangAS::opencl_global_device:
        ASString = "CLdevice";
        break;
      case LangAS::opencl_global_host:
        ASString = "CLhost";
        break;
      case LangAS::opencl_local:
        ASString = "CLlocal";
        break;
      case LangAS::opencl_constant:
        ASString = "CLconstant";
        break;
      case LangAS::opencl_private:
        ASString = "CLprivate";
        break;
      case LangAS::opencl_generic:
        ASString = "CLgeneric";
        break;
      //  <SYCL-addrspace> ::= "SY" [ "global" | "local" | "private" |
      //                              "device" | "host" ]
      case LangAS::sycl_global:
        ASString = "SYglobal";
        break;
      case LangAS::sycl_global_device:
        ASString = "SYdevice";
        break;
      case LangAS::sycl_global_host:
        ASString = "SYhost";
        break;
      case LangAS::sycl_local:
        ASString = "SYlocal";
        break;
      case LangAS::sycl_private:
        ASString = "SYprivate";
        break;
      //  <CUDA-addrspace> ::= "CU" [ "device" | "constant" | "shared" ]
      case LangAS::cuda_device:
        ASString = "CUdevice";
        break;
      case LangAS::cuda_constant:
        ASString = "CUconstant";
        break;
      case LangAS::cuda_shared:
        ASString = "CUshared";
        break;
      //  <ptrsize-addrspace> ::= [ "ptr32_sptr" | "ptr32_uptr" | "ptr64" ]
      case LangAS::ptr32_sptr:
        ASString = "ptr32_sptr";
        break;
      case LangAS::ptr32_uptr:
        ASString = "ptr32_uptr";
        break;
      case LangAS::ptr64:
        ASString = "ptr64";
        break;
      }
    }
    if (!ASString.empty())
      mangleVendorQualifier(ASString);
  }

  // The ARC ownership qualifiers start with underscores.
  // Objective-C ARC Extension:
  //
  //   <type> ::= U "__strong"
  //   <type> ::= U "__weak"
  //   <type> ::= U "__autoreleasing"
  //
  // Note: we emit __weak first to preserve the order as
  // required by the Itanium ABI.
  if (Quals.getObjCLifetime() == Qualifiers::OCL_Weak)
    mangleVendorQualifier("__weak");

  // __unaligned (from -fms-extensions)
  if (Quals.hasUnaligned())
    mangleVendorQualifier("__unaligned");

  // Remaining ARC ownership qualifiers.
  switch (Quals.getObjCLifetime()) {
  case Qualifiers::OCL_None:
    break;

  case Qualifiers::OCL_Weak:
    // Do nothing as we already handled this case above.
    break;

  case Qualifiers::OCL_Strong:
    mangleVendorQualifier("__strong");
    break;

  case Qualifiers::OCL_Autoreleasing:
    mangleVendorQualifier("__autoreleasing");
    break;

  case Qualifiers::OCL_ExplicitNone:
    // The __unsafe_unretained qualifier is *not* mangled, so that
    // __unsafe_unretained types in ARC produce the same manglings as the
    // equivalent (but, naturally, unqualified) types in non-ARC, providing
    // better ABI compatibility.
    //
    // It's safe to do this because unqualified 'id' won't show up
    // in any type signatures that need to be mangled.
    break;
  }

  // <CV-qualifiers> ::= [r] [V] [K]    # restrict (C99), volatile, const
  if (Quals.hasRestrict())
    Out << 'r';
  if (Quals.hasVolatile())
    Out << 'V';
  if (Quals.hasConst())
    Out << 'K';
}

void CXXNameMangler::mangleVendorQualifier(StringRef name) {
  Out << 'U' << name.size() << name;
}

void CXXNameMangler::mangleRefQualifier(RefQualifierKind RefQualifier) {
  // <ref-qualifier> ::= R                # lvalue reference
  //                 ::= O                # rvalue-reference
  switch (RefQualifier) {
  case RQ_None:
    break;

  case RQ_LValue:
    Out << 'R';
    break;

  case RQ_RValue:
    Out << 'O';
    break;
  }
}

void CXXNameMangler::mangleObjCMethodName(const ObjCMethodDecl *MD) {
  Context.mangleObjCMethodNameAsSourceName(MD, Out);
}

static bool isTypeSubstitutable(Qualifiers Quals, const Type *Ty,
                                ASTContext &Ctx) {
  if (Quals)
    return true;
  if (Ty->isSpecificBuiltinType(BuiltinType::ObjCSel))
    return true;
  if (Ty->isOpenCLSpecificType())
    return true;
  // From Clang 18.0 we correctly treat SVE types as substitution candidates.
  if (Ty->isSVESizelessBuiltinType() &&
      Ctx.getLangOpts().getClangABICompat() > LangOptions::ClangABI::Ver17)
    return true;
  if (Ty->isBuiltinType())
    return false;
  // Through to Clang 6.0, we accidentally treated undeduced auto types as
  // substitution candidates.
  if (Ctx.getLangOpts().getClangABICompat() > LangOptions::ClangABI::Ver6 &&
      isa<AutoType>(Ty))
    return false;
  // A placeholder type for class template deduction is substitutable with
  // its corresponding template name; this is handled specially when mangling
  // the type.
  if (auto *DeducedTST = Ty->getAs<DeducedTemplateSpecializationType>())
    if (DeducedTST->getDeducedType().isNull())
      return false;
  return true;
}

void CXXNameMangler::mangleType(QualType T) {
  // If our type is instantiation-dependent but not dependent, we mangle
  // it as it was written in the source, removing any top-level sugar.
  // Otherwise, use the canonical type.
  //
  // FIXME: This is an approximation of the instantiation-dependent name
  // mangling rules, since we should really be using the type as written and
  // augmented via semantic analysis (i.e., with implicit conversions and
  // default template arguments) for any instantiation-dependent type.
  // Unfortunately, that requires several changes to our AST:
  //   - Instantiation-dependent TemplateSpecializationTypes will need to be
  //     uniqued, so that we can handle substitutions properly
  //   - Default template arguments will need to be represented in the
  //     TemplateSpecializationType, since they need to be mangled even though
  //     they aren't written.
  //   - Conversions on non-type template arguments need to be expressed, since
  //     they can affect the mangling of sizeof/alignof.
  //
  // FIXME: This is wrong when mapping to the canonical type for a dependent
  // type discards instantiation-dependent portions of the type, such as for:
  //
  //   template<typename T, int N> void f(T (&)[sizeof(N)]);
  //   template<typename T> void f(T() throw(typename T::type)); (pre-C++17)
  //
  // It's also wrong in the opposite direction when instantiation-dependent,
  // canonically-equivalent types differ in some irrelevant portion of inner
  // type sugar. In such cases, we fail to form correct substitutions, eg:
  //
  //   template<int N> void f(A<sizeof(N)> *, A<sizeof(N)> (*));
  //
  // We should instead canonicalize the non-instantiation-dependent parts,
  // regardless of whether the type as a whole is dependent or instantiation
  // dependent.
  if (!T->isInstantiationDependentType() || T->isDependentType())
    T = T.getCanonicalType();
  else {
    // Desugar any types that are purely sugar.
    do {
      // Don't desugar through template specialization types that aren't
      // type aliases. We need to mangle the template arguments as written.
      if (const TemplateSpecializationType *TST
                                      = dyn_cast<TemplateSpecializationType>(T))
        if (!TST->isTypeAlias())
          break;

      // FIXME: We presumably shouldn't strip off ElaboratedTypes with
      // instantation-dependent qualifiers. See
      // https://github.com/itanium-cxx-abi/cxx-abi/issues/114.

      QualType Desugared
        = T.getSingleStepDesugaredType(Context.getASTContext());
      if (Desugared == T)
        break;

      T = Desugared;
    } while (true);
  }
  SplitQualType split = T.split();
  Qualifiers quals = split.Quals;
  const Type *ty = split.Ty;

  bool isSubstitutable =
    isTypeSubstitutable(quals, ty, Context.getASTContext());
  if (isSubstitutable && mangleSubstitution(T))
    return;

  // If we're mangling a qualified array type, push the qualifiers to
  // the element type.
  if (quals && isa<ArrayType>(T)) {
    ty = Context.getASTContext().getAsArrayType(T);
    quals = Qualifiers();

    // Note that we don't update T: we want to add the
    // substitution at the original type.
  }

  if (quals || ty->isDependentAddressSpaceType()) {
    if (const DependentAddressSpaceType *DAST =
        dyn_cast<DependentAddressSpaceType>(ty)) {
      SplitQualType splitDAST = DAST->getPointeeType().split();
      mangleQualifiers(splitDAST.Quals, DAST);
      mangleType(QualType(splitDAST.Ty, 0));
    } else {
      mangleQualifiers(quals);

      // Recurse:  even if the qualified type isn't yet substitutable,
      // the unqualified type might be.
      mangleType(QualType(ty, 0));
    }
  } else {
    switch (ty->getTypeClass()) {
#define ABSTRACT_TYPE(CLASS, PARENT)
#define NON_CANONICAL_TYPE(CLASS, PARENT) \
    case Type::CLASS: \
      llvm_unreachable("can't mangle non-canonical type " #CLASS "Type"); \
      return;
#define TYPE(CLASS, PARENT) \
    case Type::CLASS: \
      mangleType(static_cast<const CLASS##Type*>(ty)); \
      break;
#include "clang/AST/TypeNodes.inc"
    }
  }

  // Add the substitution.
  if (isSubstitutable)
    addSubstitution(T);
}

void CXXNameMangler::mangleNameOrStandardSubstitution(const NamedDecl *ND) {
  if (!mangleStandardSubstitution(ND))
    mangleName(ND);
}

void CXXNameMangler::mangleType(const BuiltinType *T) {
  //  <type>         ::= <builtin-type>
  //  <builtin-type> ::= v  # void
  //                 ::= w  # wchar_t
  //                 ::= b  # bool
  //                 ::= c  # char
  //                 ::= a  # signed char
  //                 ::= h  # unsigned char
  //                 ::= s  # short
  //                 ::= t  # unsigned short
  //                 ::= i  # int
  //                 ::= j  # unsigned int
  //                 ::= l  # long
  //                 ::= m  # unsigned long
  //                 ::= x  # long long, __int64
  //                 ::= y  # unsigned long long, __int64
  //                 ::= n  # __int128
  //                 ::= o  # unsigned __int128
  //                 ::= f  # float
  //                 ::= d  # double
  //                 ::= e  # long double, __float80
  //                 ::= g  # __float128
  //                 ::= g  # __ibm128
  // UNSUPPORTED:    ::= Dd # IEEE 754r decimal floating point (64 bits)
  // UNSUPPORTED:    ::= De # IEEE 754r decimal floating point (128 bits)
  // UNSUPPORTED:    ::= Df # IEEE 754r decimal floating point (32 bits)
  //                 ::= Dh # IEEE 754r half-precision floating point (16 bits)
  //                 ::= DF <number> _ # ISO/IEC TS 18661 binary floating point type _FloatN (N bits);
  //                 ::= Di # char32_t
  //                 ::= Ds # char16_t
  //                 ::= Dn # std::nullptr_t (i.e., decltype(nullptr))
  //                 ::= [DS] DA  # N1169 fixed-point [_Sat] T _Accum
  //                 ::= [DS] DR  # N1169 fixed-point [_Sat] T _Fract
  //                 ::= u <source-name>    # vendor extended type
  //
  //  <fixed-point-size>
  //                 ::= s # short
  //                 ::= t # unsigned short
  //                 ::= i # plain
  //                 ::= j # unsigned
  //                 ::= l # long
  //                 ::= m # unsigned long
  std::string type_name;
  // Normalize integer types as vendor extended types:
  // u<length>i<type size>
  // u<length>u<type size>
  if (NormalizeIntegers && T->isInteger()) {
    if (T->isSignedInteger()) {
      switch (getASTContext().getTypeSize(T)) {
      case 8:
        // Pick a representative for each integer size in the substitution
        // dictionary. (Its actual defined size is not relevant.)
        if (mangleSubstitution(BuiltinType::SChar))
          break;
        Out << "u2i8";
        addSubstitution(BuiltinType::SChar);
        break;
      case 16:
        if (mangleSubstitution(BuiltinType::Short))
          break;
        Out << "u3i16";
        addSubstitution(BuiltinType::Short);
        break;
      case 32:
        if (mangleSubstitution(BuiltinType::Int))
          break;
        Out << "u3i32";
        addSubstitution(BuiltinType::Int);
        break;
      case 64:
        if (mangleSubstitution(BuiltinType::Long))
          break;
        Out << "u3i64";
        addSubstitution(BuiltinType::Long);
        break;
      case 128:
        if (mangleSubstitution(BuiltinType::Int128))
          break;
        Out << "u4i128";
        addSubstitution(BuiltinType::Int128);
        break;
      default:
        llvm_unreachable("Unknown integer size for normalization");
      }
    } else {
      switch (getASTContext().getTypeSize(T)) {
      case 8:
        if (mangleSubstitution(BuiltinType::UChar))
          break;
        Out << "u2u8";
        addSubstitution(BuiltinType::UChar);
        break;
      case 16:
        if (mangleSubstitution(BuiltinType::UShort))
          break;
        Out << "u3u16";
        addSubstitution(BuiltinType::UShort);
        break;
      case 32:
        if (mangleSubstitution(BuiltinType::UInt))
          break;
        Out << "u3u32";
        addSubstitution(BuiltinType::UInt);
        break;
      case 64:
        if (mangleSubstitution(BuiltinType::ULong))
          break;
        Out << "u3u64";
        addSubstitution(BuiltinType::ULong);
        break;
      case 128:
        if (mangleSubstitution(BuiltinType::UInt128))
          break;
        Out << "u4u128";
        addSubstitution(BuiltinType::UInt128);
        break;
      default:
        llvm_unreachable("Unknown integer size for normalization");
      }
    }
    return;
  }
  switch (T->getKind()) {
  case BuiltinType::Void:
    Out << 'v';
    break;
  case BuiltinType::Bool:
    Out << 'b';
    break;
  case BuiltinType::Char_U:
  case BuiltinType::Char_S:
    Out << 'c';
    break;
  case BuiltinType::UChar:
    Out << 'h';
    break;
  case BuiltinType::UShort:
    Out << 't';
    break;
  case BuiltinType::UInt:
    Out << 'j';
    break;
  case BuiltinType::ULong:
    Out << 'm';
    break;
  case BuiltinType::ULongLong:
    Out << 'y';
    break;
  case BuiltinType::UInt128:
    Out << 'o';
    break;
  case BuiltinType::SChar:
    Out << 'a';
    break;
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    Out << 'w';
    break;
  case BuiltinType::Char8:
    Out << "Du";
    break;
  case BuiltinType::Char16:
    Out << "Ds";
    break;
  case BuiltinType::Char32:
    Out << "Di";
    break;
  case BuiltinType::Short:
    Out << 's';
    break;
  case BuiltinType::Int:
    Out << 'i';
    break;
  case BuiltinType::Long:
    Out << 'l';
    break;
  case BuiltinType::LongLong:
    Out << 'x';
    break;
  case BuiltinType::Int128:
    Out << 'n';
    break;
  case BuiltinType::Float16:
    Out << "DF16_";
    break;
  case BuiltinType::ShortAccum:
    Out << "DAs";
    break;
  case BuiltinType::Accum:
    Out << "DAi";
    break;
  case BuiltinType::LongAccum:
    Out << "DAl";
    break;
  case BuiltinType::UShortAccum:
    Out << "DAt";
    break;
  case BuiltinType::UAccum:
    Out << "DAj";
    break;
  case BuiltinType::ULongAccum:
    Out << "DAm";
    break;
  case BuiltinType::ShortFract:
    Out << "DRs";
    break;
  case BuiltinType::Fract:
    Out << "DRi";
    break;
  case BuiltinType::LongFract:
    Out << "DRl";
    break;
  case BuiltinType::UShortFract:
    Out << "DRt";
    break;
  case BuiltinType::UFract:
    Out << "DRj";
    break;
  case BuiltinType::ULongFract:
    Out << "DRm";
    break;
  case BuiltinType::SatShortAccum:
    Out << "DSDAs";
    break;
  case BuiltinType::SatAccum:
    Out << "DSDAi";
    break;
  case BuiltinType::SatLongAccum:
    Out << "DSDAl";
    break;
  case BuiltinType::SatUShortAccum:
    Out << "DSDAt";
    break;
  case BuiltinType::SatUAccum:
    Out << "DSDAj";
    break;
  case BuiltinType::SatULongAccum:
    Out << "DSDAm";
    break;
  case BuiltinType::SatShortFract:
    Out << "DSDRs";
    break;
  case BuiltinType::SatFract:
    Out << "DSDRi";
    break;
  case BuiltinType::SatLongFract:
    Out << "DSDRl";
    break;
  case BuiltinType::SatUShortFract:
    Out << "DSDRt";
    break;
  case BuiltinType::SatUFract:
    Out << "DSDRj";
    break;
  case BuiltinType::SatULongFract:
    Out << "DSDRm";
    break;
  case BuiltinType::Half:
    Out << "Dh";
    break;
  case BuiltinType::Float:
    Out << 'f';
    break;
  case BuiltinType::Double:
    Out << 'd';
    break;
  case BuiltinType::LongDouble: {
    const TargetInfo *TI =
        getASTContext().getLangOpts().OpenMP &&
                getASTContext().getLangOpts().OpenMPIsTargetDevice
            ? getASTContext().getAuxTargetInfo()
            : &getASTContext().getTargetInfo();
    Out << TI->getLongDoubleMangling();
    break;
  }
  case BuiltinType::Float128: {
    const TargetInfo *TI =
        getASTContext().getLangOpts().OpenMP &&
                getASTContext().getLangOpts().OpenMPIsTargetDevice
            ? getASTContext().getAuxTargetInfo()
            : &getASTContext().getTargetInfo();
    Out << TI->getFloat128Mangling();
    break;
  }
  case BuiltinType::BFloat16: {
    const TargetInfo *TI =
        ((getASTContext().getLangOpts().OpenMP &&
          getASTContext().getLangOpts().OpenMPIsTargetDevice) ||
         getASTContext().getLangOpts().SYCLIsDevice)
            ? getASTContext().getAuxTargetInfo()
            : &getASTContext().getTargetInfo();
    Out << TI->getBFloat16Mangling();
    break;
  }
  case BuiltinType::Ibm128: {
    const TargetInfo *TI = &getASTContext().getTargetInfo();
    Out << TI->getIbm128Mangling();
    break;
  }
  case BuiltinType::NullPtr:
    Out << "Dn";
    break;

#define BUILTIN_TYPE(Id, SingletonId)
#define PLACEHOLDER_TYPE(Id, SingletonId) \
  case BuiltinType::Id:
#include "clang/AST/BuiltinTypes.def"
  case BuiltinType::Dependent:
    if (!NullOut)
      llvm_unreachable("mangling a placeholder type");
    break;
  case BuiltinType::ObjCId:
    Out << "11objc_object";
    break;
  case BuiltinType::ObjCClass:
    Out << "10objc_class";
    break;
  case BuiltinType::ObjCSel:
    Out << "13objc_selector";
    break;
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  case BuiltinType::Id: \
    type_name = "ocl_" #ImgType "_" #Suffix; \
    Out << type_name.size() << type_name; \
    break;
#include "clang/Basic/OpenCLImageTypes.def"
  case BuiltinType::OCLSampler:
    Out << "11ocl_sampler";
    break;
  case BuiltinType::OCLEvent:
    Out << "9ocl_event";
    break;
  case BuiltinType::OCLClkEvent:
    Out << "12ocl_clkevent";
    break;
  case BuiltinType::OCLQueue:
    Out << "9ocl_queue";
    break;
  case BuiltinType::OCLReserveID:
    Out << "13ocl_reserveid";
    break;
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id: \
    type_name = "ocl_" #ExtType; \
    Out << type_name.size() << type_name; \
    break;
#include "clang/Basic/OpenCLExtensionTypes.def"
  // The SVE types are effectively target-specific.  The mangling scheme
  // is defined in the appendices to the Procedure Call Standard for the
  // Arm Architecture.
#define SVE_VECTOR_TYPE(InternalName, MangledName, Id, SingletonId, NumEls,    \
                        ElBits, IsSigned, IsFP, IsBF)                          \
  case BuiltinType::Id:                                                        \
    if (T->getKind() == BuiltinType::SveBFloat16 &&                            \
        isCompatibleWith(LangOptions::ClangABI::Ver17)) {                      \
      /* Prior to Clang 18.0 we used this incorrect mangled name */            \
      type_name = "__SVBFloat16_t";                                            \
      Out << "u" << type_name.size() << type_name;                             \
    } else {                                                                   \
      type_name = MangledName;                                                 \
      Out << (type_name == InternalName ? "u" : "") << type_name.size()        \
          << type_name;                                                        \
    }                                                                          \
    break;
#define SVE_PREDICATE_TYPE(InternalName, MangledName, Id, SingletonId, NumEls) \
  case BuiltinType::Id:                                                        \
    type_name = MangledName;                                                   \
    Out << (type_name == InternalName ? "u" : "") << type_name.size()          \
        << type_name;                                                          \
    break;
#define SVE_OPAQUE_TYPE(InternalName, MangledName, Id, SingletonId)            \
  case BuiltinType::Id:                                                        \
    type_name = MangledName;                                                   \
    Out << (type_name == InternalName ? "u" : "") << type_name.size()          \
        << type_name;                                                          \
    break;
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
  case BuiltinType::Id: \
    type_name = #Name; \
    Out << 'u' << type_name.size() << type_name; \
    break;
#include "clang/Basic/PPCTypes.def"
    // TODO: Check the mangling scheme for RISC-V V.
#define RVV_TYPE(Name, Id, SingletonId)                                        \
  case BuiltinType::Id:                                                        \
    type_name = Name;                                                          \
    Out << 'u' << type_name.size() << type_name;                               \
    break;
#include "clang/Basic/RISCVVTypes.def"
#define WASM_REF_TYPE(InternalName, MangledName, Id, SingletonId, AS)          \
  case BuiltinType::Id:                                                        \
    type_name = MangledName;                                                   \
    Out << 'u' << type_name.size() << type_name;                               \
    break;
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId)                                     \
  case BuiltinType::Id:                                                        \
    type_name = Name;                                                          \
    Out << 'u' << type_name.size() << type_name;                               \
    break;
#include "clang/Basic/AMDGPUTypes.def"
  }
}

StringRef CXXNameMangler::getCallingConvQualifierName(CallingConv CC) {
  switch (CC) {
  case CC_C:
    return "";

  case CC_X86VectorCall:
  case CC_X86Pascal:
  case CC_X86RegCall:
  case CC_AAPCS:
  case CC_AAPCS_VFP:
  case CC_AArch64VectorCall:
  case CC_AArch64SVEPCS:
  case CC_AMDGPUKernelCall:
  case CC_IntelOclBicc:
  case CC_SpirFunction:
  case CC_OpenCLKernel:
  case CC_PreserveMost:
  case CC_PreserveAll:
  case CC_M68kRTD:
  case CC_PreserveNone:
  case CC_RISCVVectorCall:
    // FIXME: we should be mangling all of the above.
    return "";

  case CC_X86ThisCall:
    // FIXME: To match mingw GCC, thiscall should only be mangled in when it is
    // used explicitly. At this point, we don't have that much information in
    // the AST, since clang tends to bake the convention into the canonical
    // function type. thiscall only rarely used explicitly, so don't mangle it
    // for now.
    return "";

  case CC_X86StdCall:
    return "stdcall";
  case CC_X86FastCall:
    return "fastcall";
  case CC_X86_64SysV:
    return "sysv_abi";
  case CC_Win64:
    return "ms_abi";
  case CC_Swift:
    return "swiftcall";
  case CC_SwiftAsync:
    return "swiftasynccall";
  }
  llvm_unreachable("bad calling convention");
}

void CXXNameMangler::mangleExtFunctionInfo(const FunctionType *T) {
  // Fast path.
  if (T->getExtInfo() == FunctionType::ExtInfo())
    return;

  // Vendor-specific qualifiers are emitted in reverse alphabetical order.
  // This will get more complicated in the future if we mangle other
  // things here; but for now, since we mangle ns_returns_retained as
  // a qualifier on the result type, we can get away with this:
  StringRef CCQualifier = getCallingConvQualifierName(T->getExtInfo().getCC());
  if (!CCQualifier.empty())
    mangleVendorQualifier(CCQualifier);

  // FIXME: regparm
  // FIXME: noreturn
}

void
CXXNameMangler::mangleExtParameterInfo(FunctionProtoType::ExtParameterInfo PI) {
  // Vendor-specific qualifiers are emitted in reverse alphabetical order.

  // Note that these are *not* substitution candidates.  Demanglers might
  // have trouble with this if the parameter type is fully substituted.

  switch (PI.getABI()) {
  case ParameterABI::Ordinary:
    break;

  // All of these start with "swift", so they come before "ns_consumed".
  case ParameterABI::SwiftContext:
  case ParameterABI::SwiftAsyncContext:
  case ParameterABI::SwiftErrorResult:
  case ParameterABI::SwiftIndirectResult:
    mangleVendorQualifier(getParameterABISpelling(PI.getABI()));
    break;
  }

  if (PI.isConsumed())
    mangleVendorQualifier("ns_consumed");

  if (PI.isNoEscape())
    mangleVendorQualifier("noescape");
}

// <type>          ::= <function-type>
// <function-type> ::= [<CV-qualifiers>] F [Y]
//                      <bare-function-type> [<ref-qualifier>] E
void CXXNameMangler::mangleType(const FunctionProtoType *T) {
  mangleExtFunctionInfo(T);

  // Mangle CV-qualifiers, if present.  These are 'this' qualifiers,
  // e.g. "const" in "int (A::*)() const".
  mangleQualifiers(T->getMethodQuals());

  // Mangle instantiation-dependent exception-specification, if present,
  // per cxx-abi-dev proposal on 2016-10-11.
  if (T->hasInstantiationDependentExceptionSpec()) {
    if (isComputedNoexcept(T->getExceptionSpecType())) {
      Out << "DO";
      mangleExpression(T->getNoexceptExpr());
      Out << "E";
    } else {
      assert(T->getExceptionSpecType() == EST_Dynamic);
      Out << "Dw";
      for (auto ExceptTy : T->exceptions())
        mangleType(ExceptTy);
      Out << "E";
    }
  } else if (T->isNothrow()) {
    Out << "Do";
  }

  Out << 'F';

  // FIXME: We don't have enough information in the AST to produce the 'Y'
  // encoding for extern "C" function types.
  mangleBareFunctionType(T, /*MangleReturnType=*/true);

  // Mangle the ref-qualifier, if present.
  mangleRefQualifier(T->getRefQualifier());

  Out << 'E';
}

void CXXNameMangler::mangleType(const FunctionNoProtoType *T) {
  // Function types without prototypes can arise when mangling a function type
  // within an overloadable function in C. We mangle these as the absence of any
  // parameter types (not even an empty parameter list).
  Out << 'F';

  FunctionTypeDepthState saved = FunctionTypeDepth.push();

  FunctionTypeDepth.enterResultType();
  mangleType(T->getReturnType());
  FunctionTypeDepth.leaveResultType();

  FunctionTypeDepth.pop(saved);
  Out << 'E';
}

void CXXNameMangler::mangleBareFunctionType(const FunctionProtoType *Proto,
                                            bool MangleReturnType,
                                            const FunctionDecl *FD) {
  // Record that we're in a function type.  See mangleFunctionParam
  // for details on what we're trying to achieve here.
  FunctionTypeDepthState saved = FunctionTypeDepth.push();

  // <bare-function-type> ::= <signature type>+
  if (MangleReturnType) {
    FunctionTypeDepth.enterResultType();

    // Mangle ns_returns_retained as an order-sensitive qualifier here.
    if (Proto->getExtInfo().getProducesResult() && FD == nullptr)
      mangleVendorQualifier("ns_returns_retained");

    // Mangle the return type without any direct ARC ownership qualifiers.
    QualType ReturnTy = Proto->getReturnType();
    if (ReturnTy.getObjCLifetime()) {
      auto SplitReturnTy = ReturnTy.split();
      SplitReturnTy.Quals.removeObjCLifetime();
      ReturnTy = getASTContext().getQualifiedType(SplitReturnTy);
    }
    mangleType(ReturnTy);

    FunctionTypeDepth.leaveResultType();
  }

  if (Proto->getNumParams() == 0 && !Proto->isVariadic()) {
    //   <builtin-type> ::= v   # void
    Out << 'v';
  } else {
    assert(!FD || FD->getNumParams() == Proto->getNumParams());
    for (unsigned I = 0, E = Proto->getNumParams(); I != E; ++I) {
      // Mangle extended parameter info as order-sensitive qualifiers here.
      if (Proto->hasExtParameterInfos() && FD == nullptr) {
        mangleExtParameterInfo(Proto->getExtParameterInfo(I));
      }

      // Mangle the type.
      QualType ParamTy = Proto->getParamType(I);
      mangleType(Context.getASTContext().getSignatureParameterType(ParamTy));

      if (FD) {
        if (auto *Attr = FD->getParamDecl(I)->getAttr<PassObjectSizeAttr>()) {
          // Attr can only take 1 character, so we can hardcode the length
          // below.
          assert(Attr->getType() <= 9 && Attr->getType() >= 0);
          if (Attr->isDynamic())
            Out << "U25pass_dynamic_object_size" << Attr->getType();
          else
            Out << "U17pass_object_size" << Attr->getType();
        }
      }
    }

    // <builtin-type>      ::= z  # ellipsis
    if (Proto->isVariadic())
      Out << 'z';
  }

  if (FD) {
    FunctionTypeDepth.enterResultType();
    mangleRequiresClause(FD->getTrailingRequiresClause());
  }

  FunctionTypeDepth.pop(saved);
}

// <type>            ::= <class-enum-type>
// <class-enum-type> ::= <name>
void CXXNameMangler::mangleType(const UnresolvedUsingType *T) {
  mangleName(T->getDecl());
}

// <type>            ::= <class-enum-type>
// <class-enum-type> ::= <name>
void CXXNameMangler::mangleType(const EnumType *T) {
  mangleType(static_cast<const TagType*>(T));
}
void CXXNameMangler::mangleType(const RecordType *T) {
  mangleType(static_cast<const TagType*>(T));
}
void CXXNameMangler::mangleType(const TagType *T) {
  mangleName(T->getDecl());
}

// <type>       ::= <array-type>
// <array-type> ::= A <positive dimension number> _ <element type>
//              ::= A [<dimension expression>] _ <element type>
void CXXNameMangler::mangleType(const ConstantArrayType *T) {
  Out << 'A' << T->getSize() << '_';
  mangleType(T->getElementType());
}
void CXXNameMangler::mangleType(const VariableArrayType *T) {
  Out << 'A';
  // decayed vla types (size 0) will just be skipped.
  if (T->getSizeExpr())
    mangleExpression(T->getSizeExpr());
  Out << '_';
  mangleType(T->getElementType());
}
void CXXNameMangler::mangleType(const DependentSizedArrayType *T) {
  Out << 'A';
  // A DependentSizedArrayType might not have size expression as below
  //
  // template<int ...N> int arr[] = {N...};
  if (T->getSizeExpr())
    mangleExpression(T->getSizeExpr());
  Out << '_';
  mangleType(T->getElementType());
}
void CXXNameMangler::mangleType(const IncompleteArrayType *T) {
  Out << "A_";
  mangleType(T->getElementType());
}

// <type>                   ::= <pointer-to-member-type>
// <pointer-to-member-type> ::= M <class type> <member type>
void CXXNameMangler::mangleType(const MemberPointerType *T) {
  Out << 'M';
  mangleType(QualType(T->getClass(), 0));
  QualType PointeeType = T->getPointeeType();
  if (const FunctionProtoType *FPT = dyn_cast<FunctionProtoType>(PointeeType)) {
    mangleType(FPT);

    // Itanium C++ ABI 5.1.8:
    //
    //   The type of a non-static member function is considered to be different,
    //   for the purposes of substitution, from the type of a namespace-scope or
    //   static member function whose type appears similar. The types of two
    //   non-static member functions are considered to be different, for the
    //   purposes of substitution, if the functions are members of different
    //   classes. In other words, for the purposes of substitution, the class of
    //   which the function is a member is considered part of the type of
    //   function.

    // Given that we already substitute member function pointers as a
    // whole, the net effect of this rule is just to unconditionally
    // suppress substitution on the function type in a member pointer.
    // We increment the SeqID here to emulate adding an entry to the
    // substitution table.
    ++SeqID;
  } else
    mangleType(PointeeType);
}

// <type>           ::= <template-param>
void CXXNameMangler::mangleType(const TemplateTypeParmType *T) {
  mangleTemplateParameter(T->getDepth(), T->getIndex());
}

// <type>           ::= <template-param>
void CXXNameMangler::mangleType(const SubstTemplateTypeParmPackType *T) {
  // FIXME: not clear how to mangle this!
  // template <class T...> class A {
  //   template <class U...> void foo(T(*)(U) x...);
  // };
  Out << "_SUBSTPACK_";
}

// <type> ::= P <type>   # pointer-to
void CXXNameMangler::mangleType(const PointerType *T) {
  Out << 'P';
  mangleType(T->getPointeeType());
}
void CXXNameMangler::mangleType(const ObjCObjectPointerType *T) {
  Out << 'P';
  mangleType(T->getPointeeType());
}

// <type> ::= R <type>   # reference-to
void CXXNameMangler::mangleType(const LValueReferenceType *T) {
  Out << 'R';
  mangleType(T->getPointeeType());
}

// <type> ::= O <type>   # rvalue reference-to (C++0x)
void CXXNameMangler::mangleType(const RValueReferenceType *T) {
  Out << 'O';
  mangleType(T->getPointeeType());
}

// <type> ::= C <type>   # complex pair (C 2000)
void CXXNameMangler::mangleType(const ComplexType *T) {
  Out << 'C';
  mangleType(T->getElementType());
}

// ARM's ABI for Neon vector types specifies that they should be mangled as
// if they are structs (to match ARM's initial implementation).  The
// vector type must be one of the special types predefined by ARM.
void CXXNameMangler::mangleNeonVectorType(const VectorType *T) {
  QualType EltType = T->getElementType();
  assert(EltType->isBuiltinType() && "Neon vector element not a BuiltinType");
  const char *EltName = nullptr;
  if (T->getVectorKind() == VectorKind::NeonPoly) {
    switch (cast<BuiltinType>(EltType)->getKind()) {
    case BuiltinType::SChar:
    case BuiltinType::UChar:
      EltName = "poly8_t";
      break;
    case BuiltinType::Short:
    case BuiltinType::UShort:
      EltName = "poly16_t";
      break;
    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
      EltName = "poly64_t";
      break;
    default: llvm_unreachable("unexpected Neon polynomial vector element type");
    }
  } else {
    switch (cast<BuiltinType>(EltType)->getKind()) {
    case BuiltinType::SChar:     EltName = "int8_t"; break;
    case BuiltinType::UChar:     EltName = "uint8_t"; break;
    case BuiltinType::Short:     EltName = "int16_t"; break;
    case BuiltinType::UShort:    EltName = "uint16_t"; break;
    case BuiltinType::Int:       EltName = "int32_t"; break;
    case BuiltinType::UInt:      EltName = "uint32_t"; break;
    case BuiltinType::LongLong:  EltName = "int64_t"; break;
    case BuiltinType::ULongLong: EltName = "uint64_t"; break;
    case BuiltinType::Double:    EltName = "float64_t"; break;
    case BuiltinType::Float:     EltName = "float32_t"; break;
    case BuiltinType::Half:      EltName = "float16_t"; break;
    case BuiltinType::BFloat16:  EltName = "bfloat16_t"; break;
    default:
      llvm_unreachable("unexpected Neon vector element type");
    }
  }
  const char *BaseName = nullptr;
  unsigned BitSize = (T->getNumElements() *
                      getASTContext().getTypeSize(EltType));
  if (BitSize == 64)
    BaseName = "__simd64_";
  else {
    assert(BitSize == 128 && "Neon vector type not 64 or 128 bits");
    BaseName = "__simd128_";
  }
  Out << strlen(BaseName) + strlen(EltName);
  Out << BaseName << EltName;
}

void CXXNameMangler::mangleNeonVectorType(const DependentVectorType *T) {
  DiagnosticsEngine &Diags = Context.getDiags();
  unsigned DiagID = Diags.getCustomDiagID(
      DiagnosticsEngine::Error,
      "cannot mangle this dependent neon vector type yet");
  Diags.Report(T->getAttributeLoc(), DiagID);
}

static StringRef mangleAArch64VectorBase(const BuiltinType *EltType) {
  switch (EltType->getKind()) {
  case BuiltinType::SChar:
    return "Int8";
  case BuiltinType::Short:
    return "Int16";
  case BuiltinType::Int:
    return "Int32";
  case BuiltinType::Long:
  case BuiltinType::LongLong:
    return "Int64";
  case BuiltinType::UChar:
    return "Uint8";
  case BuiltinType::UShort:
    return "Uint16";
  case BuiltinType::UInt:
    return "Uint32";
  case BuiltinType::ULong:
  case BuiltinType::ULongLong:
    return "Uint64";
  case BuiltinType::Half:
    return "Float16";
  case BuiltinType::Float:
    return "Float32";
  case BuiltinType::Double:
    return "Float64";
  case BuiltinType::BFloat16:
    return "Bfloat16";
  default:
    llvm_unreachable("Unexpected vector element base type");
  }
}

// AArch64's ABI for Neon vector types specifies that they should be mangled as
// the equivalent internal name. The vector type must be one of the special
// types predefined by ARM.
void CXXNameMangler::mangleAArch64NeonVectorType(const VectorType *T) {
  QualType EltType = T->getElementType();
  assert(EltType->isBuiltinType() && "Neon vector element not a BuiltinType");
  unsigned BitSize =
      (T->getNumElements() * getASTContext().getTypeSize(EltType));
  (void)BitSize; // Silence warning.

  assert((BitSize == 64 || BitSize == 128) &&
         "Neon vector type not 64 or 128 bits");

  StringRef EltName;
  if (T->getVectorKind() == VectorKind::NeonPoly) {
    switch (cast<BuiltinType>(EltType)->getKind()) {
    case BuiltinType::UChar:
      EltName = "Poly8";
      break;
    case BuiltinType::UShort:
      EltName = "Poly16";
      break;
    case BuiltinType::ULong:
    case BuiltinType::ULongLong:
      EltName = "Poly64";
      break;
    default:
      llvm_unreachable("unexpected Neon polynomial vector element type");
    }
  } else
    EltName = mangleAArch64VectorBase(cast<BuiltinType>(EltType));

  std::string TypeName =
      ("__" + EltName + "x" + Twine(T->getNumElements()) + "_t").str();
  Out << TypeName.length() << TypeName;
}
void CXXNameMangler::mangleAArch64NeonVectorType(const DependentVectorType *T) {
  DiagnosticsEngine &Diags = Context.getDiags();
  unsigned DiagID = Diags.getCustomDiagID(
      DiagnosticsEngine::Error,
      "cannot mangle this dependent neon vector type yet");
  Diags.Report(T->getAttributeLoc(), DiagID);
}

// The AArch64 ACLE specifies that fixed-length SVE vector and predicate types
// defined with the 'arm_sve_vector_bits' attribute map to the same AAPCS64
// type as the sizeless variants.
//
// The mangling scheme for VLS types is implemented as a "pseudo" template:
//
//   '__SVE_VLS<<type>, <vector length>>'
//
// Combining the existing SVE type and a specific vector length (in bits).
// For example:
//
//   typedef __SVInt32_t foo __attribute__((arm_sve_vector_bits(512)));
//
// is described as '__SVE_VLS<__SVInt32_t, 512u>' and mangled as:
//
//   "9__SVE_VLSI" + base type mangling + "Lj" + __ARM_FEATURE_SVE_BITS + "EE"
//
//   i.e. 9__SVE_VLSIu11__SVInt32_tLj512EE
//
// The latest ACLE specification (00bet5) does not contain details of this
// mangling scheme, it will be specified in the next revision. The mangling
// scheme is otherwise defined in the appendices to the Procedure Call Standard
// for the Arm Architecture, see
// https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#appendix-c-mangling
void CXXNameMangler::mangleAArch64FixedSveVectorType(const VectorType *T) {
  assert((T->getVectorKind() == VectorKind::SveFixedLengthData ||
          T->getVectorKind() == VectorKind::SveFixedLengthPredicate) &&
         "expected fixed-length SVE vector!");

  QualType EltType = T->getElementType();
  assert(EltType->isBuiltinType() &&
         "expected builtin type for fixed-length SVE vector!");

  StringRef TypeName;
  switch (cast<BuiltinType>(EltType)->getKind()) {
  case BuiltinType::SChar:
    TypeName = "__SVInt8_t";
    break;
  case BuiltinType::UChar: {
    if (T->getVectorKind() == VectorKind::SveFixedLengthData)
      TypeName = "__SVUint8_t";
    else
      TypeName = "__SVBool_t";
    break;
  }
  case BuiltinType::Short:
    TypeName = "__SVInt16_t";
    break;
  case BuiltinType::UShort:
    TypeName = "__SVUint16_t";
    break;
  case BuiltinType::Int:
    TypeName = "__SVInt32_t";
    break;
  case BuiltinType::UInt:
    TypeName = "__SVUint32_t";
    break;
  case BuiltinType::Long:
    TypeName = "__SVInt64_t";
    break;
  case BuiltinType::ULong:
    TypeName = "__SVUint64_t";
    break;
  case BuiltinType::Half:
    TypeName = "__SVFloat16_t";
    break;
  case BuiltinType::Float:
    TypeName = "__SVFloat32_t";
    break;
  case BuiltinType::Double:
    TypeName = "__SVFloat64_t";
    break;
  case BuiltinType::BFloat16:
    TypeName = "__SVBfloat16_t";
    break;
  default:
    llvm_unreachable("unexpected element type for fixed-length SVE vector!");
  }

  unsigned VecSizeInBits = getASTContext().getTypeInfo(T).Width;

  if (T->getVectorKind() == VectorKind::SveFixedLengthPredicate)
    VecSizeInBits *= 8;

  Out << "9__SVE_VLSI" << 'u' << TypeName.size() << TypeName << "Lj"
      << VecSizeInBits << "EE";
}

void CXXNameMangler::mangleAArch64FixedSveVectorType(
    const DependentVectorType *T) {
  DiagnosticsEngine &Diags = Context.getDiags();
  unsigned DiagID = Diags.getCustomDiagID(
      DiagnosticsEngine::Error,
      "cannot mangle this dependent fixed-length SVE vector type yet");
  Diags.Report(T->getAttributeLoc(), DiagID);
}

void CXXNameMangler::mangleRISCVFixedRVVVectorType(const VectorType *T) {
  assert((T->getVectorKind() == VectorKind::RVVFixedLengthData ||
          T->getVectorKind() == VectorKind::RVVFixedLengthMask) &&
         "expected fixed-length RVV vector!");

  QualType EltType = T->getElementType();
  assert(EltType->isBuiltinType() &&
         "expected builtin type for fixed-length RVV vector!");

  SmallString<20> TypeNameStr;
  llvm::raw_svector_ostream TypeNameOS(TypeNameStr);
  TypeNameOS << "__rvv_";
  switch (cast<BuiltinType>(EltType)->getKind()) {
  case BuiltinType::SChar:
    TypeNameOS << "int8";
    break;
  case BuiltinType::UChar:
    if (T->getVectorKind() == VectorKind::RVVFixedLengthData)
      TypeNameOS << "uint8";
    else
      TypeNameOS << "bool";
    break;
  case BuiltinType::Short:
    TypeNameOS << "int16";
    break;
  case BuiltinType::UShort:
    TypeNameOS << "uint16";
    break;
  case BuiltinType::Int:
    TypeNameOS << "int32";
    break;
  case BuiltinType::UInt:
    TypeNameOS << "uint32";
    break;
  case BuiltinType::Long:
    TypeNameOS << "int64";
    break;
  case BuiltinType::ULong:
    TypeNameOS << "uint64";
    break;
  case BuiltinType::Float16:
    TypeNameOS << "float16";
    break;
  case BuiltinType::Float:
    TypeNameOS << "float32";
    break;
  case BuiltinType::Double:
    TypeNameOS << "float64";
    break;
  default:
    llvm_unreachable("unexpected element type for fixed-length RVV vector!");
  }

  unsigned VecSizeInBits = getASTContext().getTypeInfo(T).Width;

  // Apend the LMUL suffix.
  auto VScale = getASTContext().getTargetInfo().getVScaleRange(
      getASTContext().getLangOpts());
  unsigned VLen = VScale->first * llvm::RISCV::RVVBitsPerBlock;

  if (T->getVectorKind() == VectorKind::RVVFixedLengthData) {
    TypeNameOS << 'm';
    if (VecSizeInBits >= VLen)
      TypeNameOS << (VecSizeInBits / VLen);
    else
      TypeNameOS << 'f' << (VLen / VecSizeInBits);
  } else {
    TypeNameOS << (VLen / VecSizeInBits);
  }
  TypeNameOS << "_t";

  Out << "9__RVV_VLSI" << 'u' << TypeNameStr.size() << TypeNameStr << "Lj"
      << VecSizeInBits << "EE";
}

void CXXNameMangler::mangleRISCVFixedRVVVectorType(
    const DependentVectorType *T) {
  DiagnosticsEngine &Diags = Context.getDiags();
  unsigned DiagID = Diags.getCustomDiagID(
      DiagnosticsEngine::Error,
      "cannot mangle this dependent fixed-length RVV vector type yet");
  Diags.Report(T->getAttributeLoc(), DiagID);
}

// GNU extension: vector types
// <type>                  ::= <vector-type>
// <vector-type>           ::= Dv <positive dimension number> _
//                                    <extended element type>
//                         ::= Dv [<dimension expression>] _ <element type>
// <extended element type> ::= <element type>
//                         ::= p # AltiVec vector pixel
//                         ::= b # Altivec vector bool
void CXXNameMangler::mangleType(const VectorType *T) {
  if ((T->getVectorKind() == VectorKind::Neon ||
       T->getVectorKind() == VectorKind::NeonPoly)) {
    llvm::Triple Target = getASTContext().getTargetInfo().getTriple();
    llvm::Triple::ArchType Arch =
        getASTContext().getTargetInfo().getTriple().getArch();
    if ((Arch == llvm::Triple::aarch64 ||
         Arch == llvm::Triple::aarch64_be) && !Target.isOSDarwin())
      mangleAArch64NeonVectorType(T);
    else
      mangleNeonVectorType(T);
    return;
  } else if (T->getVectorKind() == VectorKind::SveFixedLengthData ||
             T->getVectorKind() == VectorKind::SveFixedLengthPredicate) {
    mangleAArch64FixedSveVectorType(T);
    return;
  } else if (T->getVectorKind() == VectorKind::RVVFixedLengthData ||
             T->getVectorKind() == VectorKind::RVVFixedLengthMask) {
    mangleRISCVFixedRVVVectorType(T);
    return;
  }
  Out << "Dv" << T->getNumElements() << '_';
  if (T->getVectorKind() == VectorKind::AltiVecPixel)
    Out << 'p';
  else if (T->getVectorKind() == VectorKind::AltiVecBool)
    Out << 'b';
  else
    mangleType(T->getElementType());
}

void CXXNameMangler::mangleType(const DependentVectorType *T) {
  if ((T->getVectorKind() == VectorKind::Neon ||
       T->getVectorKind() == VectorKind::NeonPoly)) {
    llvm::Triple Target = getASTContext().getTargetInfo().getTriple();
    llvm::Triple::ArchType Arch =
        getASTContext().getTargetInfo().getTriple().getArch();
    if ((Arch == llvm::Triple::aarch64 || Arch == llvm::Triple::aarch64_be) &&
        !Target.isOSDarwin())
      mangleAArch64NeonVectorType(T);
    else
      mangleNeonVectorType(T);
    return;
  } else if (T->getVectorKind() == VectorKind::SveFixedLengthData ||
             T->getVectorKind() == VectorKind::SveFixedLengthPredicate) {
    mangleAArch64FixedSveVectorType(T);
    return;
  } else if (T->getVectorKind() == VectorKind::RVVFixedLengthData) {
    mangleRISCVFixedRVVVectorType(T);
    return;
  }

  Out << "Dv";
  mangleExpression(T->getSizeExpr());
  Out << '_';
  if (T->getVectorKind() == VectorKind::AltiVecPixel)
    Out << 'p';
  else if (T->getVectorKind() == VectorKind::AltiVecBool)
    Out << 'b';
  else
    mangleType(T->getElementType());
}

void CXXNameMangler::mangleType(const ExtVectorType *T) {
  mangleType(static_cast<const VectorType*>(T));
}
void CXXNameMangler::mangleType(const DependentSizedExtVectorType *T) {
  Out << "Dv";
  mangleExpression(T->getSizeExpr());
  Out << '_';
  mangleType(T->getElementType());
}

void CXXNameMangler::mangleType(const ConstantMatrixType *T) {
  // Mangle matrix types as a vendor extended type:
  // u<Len>matrix_typeI<Rows><Columns><element type>E

  StringRef VendorQualifier = "matrix_type";
  Out << "u" << VendorQualifier.size() << VendorQualifier;

  Out << "I";
  auto &ASTCtx = getASTContext();
  unsigned BitWidth = ASTCtx.getTypeSize(ASTCtx.getSizeType());
  llvm::APSInt Rows(BitWidth);
  Rows = T->getNumRows();
  mangleIntegerLiteral(ASTCtx.getSizeType(), Rows);
  llvm::APSInt Columns(BitWidth);
  Columns = T->getNumColumns();
  mangleIntegerLiteral(ASTCtx.getSizeType(), Columns);
  mangleType(T->getElementType());
  Out << "E";
}

void CXXNameMangler::mangleType(const DependentSizedMatrixType *T) {
  // Mangle matrix types as a vendor extended type:
  // u<Len>matrix_typeI<row expr><column expr><element type>E
  StringRef VendorQualifier = "matrix_type";
  Out << "u" << VendorQualifier.size() << VendorQualifier;

  Out << "I";
  mangleTemplateArgExpr(T->getRowExpr());
  mangleTemplateArgExpr(T->getColumnExpr());
  mangleType(T->getElementType());
  Out << "E";
}

void CXXNameMangler::mangleType(const DependentAddressSpaceType *T) {
  SplitQualType split = T->getPointeeType().split();
  mangleQualifiers(split.Quals, T);
  mangleType(QualType(split.Ty, 0));
}

void CXXNameMangler::mangleType(const PackExpansionType *T) {
  // <type>  ::= Dp <type>          # pack expansion (C++0x)
  Out << "Dp";
  mangleType(T->getPattern());
}

void CXXNameMangler::mangleType(const PackIndexingType *T) {
  if (!T->hasSelectedType())
    mangleType(T->getPattern());
  else
    mangleType(T->getSelectedType());
}

void CXXNameMangler::mangleType(const ObjCInterfaceType *T) {
  mangleSourceName(T->getDecl()->getIdentifier());
}

void CXXNameMangler::mangleType(const ObjCObjectType *T) {
  // Treat __kindof as a vendor extended type qualifier.
  if (T->isKindOfType())
    Out << "U8__kindof";

  if (!T->qual_empty()) {
    // Mangle protocol qualifiers.
    SmallString<64> QualStr;
    llvm::raw_svector_ostream QualOS(QualStr);
    QualOS << "objcproto";
    for (const auto *I : T->quals()) {
      StringRef name = I->getName();
      QualOS << name.size() << name;
    }
    Out << 'U' << QualStr.size() << QualStr;
  }

  mangleType(T->getBaseType());

  if (T->isSpecialized()) {
    // Mangle type arguments as I <type>+ E
    Out << 'I';
    for (auto typeArg : T->getTypeArgs())
      mangleType(typeArg);
    Out << 'E';
  }
}

void CXXNameMangler::mangleType(const BlockPointerType *T) {
  Out << "U13block_pointer";
  mangleType(T->getPointeeType());
}

void CXXNameMangler::mangleType(const InjectedClassNameType *T) {
  // Mangle injected class name types as if the user had written the
  // specialization out fully.  It may not actually be possible to see
  // this mangling, though.
  mangleType(T->getInjectedSpecializationType());
}

void CXXNameMangler::mangleType(const TemplateSpecializationType *T) {
  if (TemplateDecl *TD = T->getTemplateName().getAsTemplateDecl()) {
    mangleTemplateName(TD, T->template_arguments());
  } else {
    if (mangleSubstitution(QualType(T, 0)))
      return;

    mangleTemplatePrefix(T->getTemplateName());

    // FIXME: GCC does not appear to mangle the template arguments when
    // the template in question is a dependent template name. Should we
    // emulate that badness?
    mangleTemplateArgs(T->getTemplateName(), T->template_arguments());
    addSubstitution(QualType(T, 0));
  }
}

void CXXNameMangler::mangleType(const DependentNameType *T) {
  // Proposal by cxx-abi-dev, 2014-03-26
  // <class-enum-type> ::= <name>    # non-dependent or dependent type name or
  //                                 # dependent elaborated type specifier using
  //                                 # 'typename'
  //                   ::= Ts <name> # dependent elaborated type specifier using
  //                                 # 'struct' or 'class'
  //                   ::= Tu <name> # dependent elaborated type specifier using
  //                                 # 'union'
  //                   ::= Te <name> # dependent elaborated type specifier using
  //                                 # 'enum'
  switch (T->getKeyword()) {
  case ElaboratedTypeKeyword::None:
  case ElaboratedTypeKeyword::Typename:
    break;
  case ElaboratedTypeKeyword::Struct:
  case ElaboratedTypeKeyword::Class:
  case ElaboratedTypeKeyword::Interface:
    Out << "Ts";
    break;
  case ElaboratedTypeKeyword::Union:
    Out << "Tu";
    break;
  case ElaboratedTypeKeyword::Enum:
    Out << "Te";
    break;
  }
  // Typename types are always nested
  Out << 'N';
  manglePrefix(T->getQualifier());
  mangleSourceName(T->getIdentifier());
  Out << 'E';
}

void CXXNameMangler::mangleType(const DependentTemplateSpecializationType *T) {
  // Dependently-scoped template types are nested if they have a prefix.
  Out << 'N';

  // TODO: avoid making this TemplateName.
  TemplateName Prefix =
    getASTContext().getDependentTemplateName(T->getQualifier(),
                                             T->getIdentifier());
  mangleTemplatePrefix(Prefix);

  // FIXME: GCC does not appear to mangle the template arguments when
  // the template in question is a dependent template name. Should we
  // emulate that badness?
  mangleTemplateArgs(Prefix, T->template_arguments());
  Out << 'E';
}

void CXXNameMangler::mangleType(const TypeOfType *T) {
  // FIXME: this is pretty unsatisfactory, but there isn't an obvious
  // "extension with parameters" mangling.
  Out << "u6typeof";
}

void CXXNameMangler::mangleType(const TypeOfExprType *T) {
  // FIXME: this is pretty unsatisfactory, but there isn't an obvious
  // "extension with parameters" mangling.
  Out << "u6typeof";
}

void CXXNameMangler::mangleType(const DecltypeType *T) {
  Expr *E = T->getUnderlyingExpr();

  // type ::= Dt <expression> E  # decltype of an id-expression
  //                             #   or class member access
  //      ::= DT <expression> E  # decltype of an expression

  // This purports to be an exhaustive list of id-expressions and
  // class member accesses.  Note that we do not ignore parentheses;
  // parentheses change the semantics of decltype for these
  // expressions (and cause the mangler to use the other form).
  if (isa<DeclRefExpr>(E) ||
      isa<MemberExpr>(E) ||
      isa<UnresolvedLookupExpr>(E) ||
      isa<DependentScopeDeclRefExpr>(E) ||
      isa<CXXDependentScopeMemberExpr>(E) ||
      isa<UnresolvedMemberExpr>(E))
    Out << "Dt";
  else
    Out << "DT";
  mangleExpression(E);
  Out << 'E';
}

void CXXNameMangler::mangleType(const UnaryTransformType *T) {
  // If this is dependent, we need to record that. If not, we simply
  // mangle it as the underlying type since they are equivalent.
  if (T->isDependentType()) {
    Out << "u";

    StringRef BuiltinName;
    switch (T->getUTTKind()) {
#define TRANSFORM_TYPE_TRAIT_DEF(Enum, Trait)                                  \
  case UnaryTransformType::Enum:                                               \
    BuiltinName = "__" #Trait;                                                 \
    break;
#include "clang/Basic/TransformTypeTraits.def"
    }
    Out << BuiltinName.size() << BuiltinName;
  }

  Out << "I";
  mangleType(T->getBaseType());
  Out << "E";
}

void CXXNameMangler::mangleType(const AutoType *T) {
  assert(T->getDeducedType().isNull() &&
         "Deduced AutoType shouldn't be handled here!");
  assert(T->getKeyword() != AutoTypeKeyword::GNUAutoType &&
         "shouldn't need to mangle __auto_type!");
  // <builtin-type> ::= Da # auto
  //                ::= Dc # decltype(auto)
  //                ::= Dk # constrained auto
  //                ::= DK # constrained decltype(auto)
  if (T->isConstrained() && !isCompatibleWith(LangOptions::ClangABI::Ver17)) {
    Out << (T->isDecltypeAuto() ? "DK" : "Dk");
    mangleTypeConstraint(T->getTypeConstraintConcept(),
                         T->getTypeConstraintArguments());
  } else {
    Out << (T->isDecltypeAuto() ? "Dc" : "Da");
  }
}

void CXXNameMangler::mangleType(const DeducedTemplateSpecializationType *T) {
  QualType Deduced = T->getDeducedType();
  if (!Deduced.isNull())
    return mangleType(Deduced);

  TemplateDecl *TD = T->getTemplateName().getAsTemplateDecl();
  assert(TD && "shouldn't form deduced TST unless we know we have a template");

  if (mangleSubstitution(TD))
    return;

  mangleName(GlobalDecl(TD));
  addSubstitution(TD);
}

void CXXNameMangler::mangleType(const AtomicType *T) {
  // <type> ::= U <source-name> <type>  # vendor extended type qualifier
  // (Until there's a standardized mangling...)
  Out << "U7_Atomic";
  mangleType(T->getValueType());
}

void CXXNameMangler::mangleType(const PipeType *T) {
  // Pipe type mangling rules are described in SPIR 2.0 specification
  // A.1 Data types and A.3 Summary of changes
  // <type> ::= 8ocl_pipe
  Out << "8ocl_pipe";
}

void CXXNameMangler::mangleType(const BitIntType *T) {
  // 5.1.5.2 Builtin types
  // <type> ::= DB <number | instantiation-dependent expression> _
  //        ::= DU <number | instantiation-dependent expression> _
  Out << "D" << (T->isUnsigned() ? "U" : "B") << T->getNumBits() << "_";
}

void CXXNameMangler::mangleType(const DependentBitIntType *T) {
  // 5.1.5.2 Builtin types
  // <type> ::= DB <number | instantiation-dependent expression> _
  //        ::= DU <number | instantiation-dependent expression> _
  Out << "D" << (T->isUnsigned() ? "U" : "B");
  mangleExpression(T->getNumBitsExpr());
  Out << "_";
}

void CXXNameMangler::mangleType(const ArrayParameterType *T) {
  mangleType(cast<ConstantArrayType>(T));
}

void CXXNameMangler::mangleIntegerLiteral(QualType T,
                                          const llvm::APSInt &Value) {
  //  <expr-primary> ::= L <type> <value number> E # integer literal
  Out << 'L';

  mangleType(T);
  if (T->isBooleanType()) {
    // Boolean values are encoded as 0/1.
    Out << (Value.getBoolValue() ? '1' : '0');
  } else {
    mangleNumber(Value);
  }
  Out << 'E';

}

void CXXNameMangler::mangleMemberExprBase(const Expr *Base, bool IsArrow) {
  // Ignore member expressions involving anonymous unions.
  while (const auto *RT = Base->getType()->getAs<RecordType>()) {
    if (!RT->getDecl()->isAnonymousStructOrUnion())
      break;
    const auto *ME = dyn_cast<MemberExpr>(Base);
    if (!ME)
      break;
    Base = ME->getBase();
    IsArrow = ME->isArrow();
  }

  if (Base->isImplicitCXXThis()) {
    // Note: GCC mangles member expressions to the implicit 'this' as
    // *this., whereas we represent them as this->. The Itanium C++ ABI
    // does not specify anything here, so we follow GCC.
    Out << "dtdefpT";
  } else {
    Out << (IsArrow ? "pt" : "dt");
    mangleExpression(Base);
  }
}

/// Mangles a member expression.
void CXXNameMangler::mangleMemberExpr(const Expr *base,
                                      bool isArrow,
                                      NestedNameSpecifier *qualifier,
                                      NamedDecl *firstQualifierLookup,
                                      DeclarationName member,
                                      const TemplateArgumentLoc *TemplateArgs,
                                      unsigned NumTemplateArgs,
                                      unsigned arity) {
  // <expression> ::= dt <expression> <unresolved-name>
  //              ::= pt <expression> <unresolved-name>
  if (base)
    mangleMemberExprBase(base, isArrow);
  mangleUnresolvedName(qualifier, member, TemplateArgs, NumTemplateArgs, arity);
}

/// Look at the callee of the given call expression and determine if
/// it's a parenthesized id-expression which would have triggered ADL
/// otherwise.
static bool isParenthesizedADLCallee(const CallExpr *call) {
  const Expr *callee = call->getCallee();
  const Expr *fn = callee->IgnoreParens();

  // Must be parenthesized.  IgnoreParens() skips __extension__ nodes,
  // too, but for those to appear in the callee, it would have to be
  // parenthesized.
  if (callee == fn) return false;

  // Must be an unresolved lookup.
  const UnresolvedLookupExpr *lookup = dyn_cast<UnresolvedLookupExpr>(fn);
  if (!lookup) return false;

  assert(!lookup->requiresADL());

  // Must be an unqualified lookup.
  if (lookup->getQualifier()) return false;

  // Must not have found a class member.  Note that if one is a class
  // member, they're all class members.
  if (lookup->getNumDecls() > 0 &&
      (*lookup->decls_begin())->isCXXClassMember())
    return false;

  // Otherwise, ADL would have been triggered.
  return true;
}

void CXXNameMangler::mangleCastExpression(const Expr *E, StringRef CastEncoding) {
  const ExplicitCastExpr *ECE = cast<ExplicitCastExpr>(E);
  Out << CastEncoding;
  mangleType(ECE->getType());
  mangleExpression(ECE->getSubExpr());
}

void CXXNameMangler::mangleInitListElements(const InitListExpr *InitList) {
  if (auto *Syntactic = InitList->getSyntacticForm())
    InitList = Syntactic;
  for (unsigned i = 0, e = InitList->getNumInits(); i != e; ++i)
    mangleExpression(InitList->getInit(i));
}

void CXXNameMangler::mangleRequirement(SourceLocation RequiresExprLoc,
                                       const concepts::Requirement *Req) {
  using concepts::Requirement;

  // TODO: We can't mangle the result of a failed substitution. It's not clear
  // whether we should be mangling the original form prior to any substitution
  // instead. See https://lists.isocpp.org/core/2023/04/14118.php
  auto HandleSubstitutionFailure =
      [&](SourceLocation Loc) {
        DiagnosticsEngine &Diags = Context.getDiags();
        unsigned DiagID = Diags.getCustomDiagID(
            DiagnosticsEngine::Error, "cannot mangle this requires-expression "
                                      "containing a substitution failure");
        Diags.Report(Loc, DiagID);
        Out << 'F';
      };

  switch (Req->getKind()) {
  case Requirement::RK_Type: {
    const auto *TR = cast<concepts::TypeRequirement>(Req);
    if (TR->isSubstitutionFailure())
      return HandleSubstitutionFailure(
          TR->getSubstitutionDiagnostic()->DiagLoc);

    Out << 'T';
    mangleType(TR->getType()->getType());
    break;
  }

  case Requirement::RK_Simple:
  case Requirement::RK_Compound: {
    const auto *ER = cast<concepts::ExprRequirement>(Req);
    if (ER->isExprSubstitutionFailure())
      return HandleSubstitutionFailure(
          ER->getExprSubstitutionDiagnostic()->DiagLoc);

    Out << 'X';
    mangleExpression(ER->getExpr());

    if (ER->hasNoexceptRequirement())
      Out << 'N';

    if (!ER->getReturnTypeRequirement().isEmpty()) {
      if (ER->getReturnTypeRequirement().isSubstitutionFailure())
        return HandleSubstitutionFailure(ER->getReturnTypeRequirement()
                                             .getSubstitutionDiagnostic()
                                             ->DiagLoc);

      Out << 'R';
      mangleTypeConstraint(ER->getReturnTypeRequirement().getTypeConstraint());
    }
    break;
  }

  case Requirement::RK_Nested:
    const auto *NR = cast<concepts::NestedRequirement>(Req);
    if (NR->hasInvalidConstraint()) {
      // FIXME: NestedRequirement should track the location of its requires
      // keyword.
      return HandleSubstitutionFailure(RequiresExprLoc);
    }

    Out << 'Q';
    mangleExpression(NR->getConstraintExpr());
    break;
  }
}

void CXXNameMangler::mangleExpression(const Expr *E, unsigned Arity,
                                      bool AsTemplateArg) {
  // <expression> ::= <unary operator-name> <expression>
  //              ::= <binary operator-name> <expression> <expression>
  //              ::= <trinary operator-name> <expression> <expression> <expression>
  //              ::= cv <type> expression           # conversion with one argument
  //              ::= cv <type> _ <expression>* E # conversion with a different number of arguments
  //              ::= dc <type> <expression>         # dynamic_cast<type> (expression)
  //              ::= sc <type> <expression>         # static_cast<type> (expression)
  //              ::= cc <type> <expression>         # const_cast<type> (expression)
  //              ::= rc <type> <expression>         # reinterpret_cast<type> (expression)
  //              ::= st <type>                      # sizeof (a type)
  //              ::= at <type>                      # alignof (a type)
  //              ::= <template-param>
  //              ::= <function-param>
  //              ::= fpT                            # 'this' expression (part of <function-param>)
  //              ::= sr <type> <unqualified-name>                   # dependent name
  //              ::= sr <type> <unqualified-name> <template-args>   # dependent template-id
  //              ::= ds <expression> <expression>                   # expr.*expr
  //              ::= sZ <template-param>                            # size of a parameter pack
  //              ::= sZ <function-param>    # size of a function parameter pack
  //              ::= u <source-name> <template-arg>* E # vendor extended expression
  //              ::= <expr-primary>
  // <expr-primary> ::= L <type> <value number> E    # integer literal
  //                ::= L <type> <value float> E     # floating literal
  //                ::= L <type> <string type> E     # string literal
  //                ::= L <nullptr type> E           # nullptr literal "LDnE"
  //                ::= L <pointer type> 0 E         # null pointer template argument
  //                ::= L <type> <real-part float> _ <imag-part float> E    # complex floating point literal (C99); not used by clang
  //                ::= L <mangled-name> E           # external name
  QualType ImplicitlyConvertedToType;

  // A top-level expression that's not <expr-primary> needs to be wrapped in
  // X...E in a template arg.
  bool IsPrimaryExpr = true;
  auto NotPrimaryExpr = [&] {
    if (AsTemplateArg && IsPrimaryExpr)
      Out << 'X';
    IsPrimaryExpr = false;
  };

  auto MangleDeclRefExpr = [&](const NamedDecl *D) {
    switch (D->getKind()) {
    default:
      //  <expr-primary> ::= L <mangled-name> E # external name
      Out << 'L';
      mangle(D);
      Out << 'E';
      break;

    case Decl::ParmVar:
      NotPrimaryExpr();
      mangleFunctionParam(cast<ParmVarDecl>(D));
      break;

    case Decl::EnumConstant: {
      // <expr-primary>
      const EnumConstantDecl *ED = cast<EnumConstantDecl>(D);
      mangleIntegerLiteral(ED->getType(), ED->getInitVal());
      break;
    }

    case Decl::NonTypeTemplateParm:
      NotPrimaryExpr();
      const NonTypeTemplateParmDecl *PD = cast<NonTypeTemplateParmDecl>(D);
      mangleTemplateParameter(PD->getDepth(), PD->getIndex());
      break;
    }
  };

  // 'goto recurse' is used when handling a simple "unwrapping" node which
  // produces no output, where ImplicitlyConvertedToType and AsTemplateArg need
  // to be preserved.
recurse:
  switch (E->getStmtClass()) {
  case Expr::NoStmtClass:
#define ABSTRACT_STMT(Type)
#define EXPR(Type, Base)
#define STMT(Type, Base) \
  case Expr::Type##Class:
#include "clang/AST/StmtNodes.inc"
    // fallthrough

  // These all can only appear in local or variable-initialization
  // contexts and so should never appear in a mangling.
  case Expr::AddrLabelExprClass:
  case Expr::DesignatedInitUpdateExprClass:
  case Expr::ImplicitValueInitExprClass:
  case Expr::ArrayInitLoopExprClass:
  case Expr::ArrayInitIndexExprClass:
  case Expr::NoInitExprClass:
  case Expr::ParenListExprClass:
  case Expr::MSPropertyRefExprClass:
  case Expr::MSPropertySubscriptExprClass:
  case Expr::TypoExprClass: // This should no longer exist in the AST by now.
  case Expr::RecoveryExprClass:
  case Expr::ArraySectionExprClass:
  case Expr::OMPArrayShapingExprClass:
  case Expr::OMPIteratorExprClass:
  case Expr::CXXInheritedCtorInitExprClass:
  case Expr::CXXParenListInitExprClass:
  case Expr::PackIndexingExprClass:
    llvm_unreachable("unexpected statement kind");

  case Expr::ConstantExprClass:
    E = cast<ConstantExpr>(E)->getSubExpr();
    goto recurse;

  // FIXME: invent manglings for all these.
  case Expr::BlockExprClass:
  case Expr::ChooseExprClass:
  case Expr::CompoundLiteralExprClass:
  case Expr::ExtVectorElementExprClass:
  case Expr::GenericSelectionExprClass:
  case Expr::ObjCEncodeExprClass:
  case Expr::ObjCIsaExprClass:
  case Expr::ObjCIvarRefExprClass:
  case Expr::ObjCMessageExprClass:
  case Expr::ObjCPropertyRefExprClass:
  case Expr::ObjCProtocolExprClass:
  case Expr::ObjCSelectorExprClass:
  case Expr::ObjCStringLiteralClass:
  case Expr::ObjCBoxedExprClass:
  case Expr::ObjCArrayLiteralClass:
  case Expr::ObjCDictionaryLiteralClass:
  case Expr::ObjCSubscriptRefExprClass:
  case Expr::ObjCIndirectCopyRestoreExprClass:
  case Expr::ObjCAvailabilityCheckExprClass:
  case Expr::OffsetOfExprClass:
  case Expr::PredefinedExprClass:
  case Expr::ShuffleVectorExprClass:
  case Expr::ConvertVectorExprClass:
  case Expr::StmtExprClass:
  case Expr::ArrayTypeTraitExprClass:
  case Expr::ExpressionTraitExprClass:
  case Expr::VAArgExprClass:
  case Expr::CUDAKernelCallExprClass:
  case Expr::AsTypeExprClass:
  case Expr::PseudoObjectExprClass:
  case Expr::AtomicExprClass:
  case Expr::SourceLocExprClass:
  case Expr::EmbedExprClass:
  case Expr::BuiltinBitCastExprClass:
  {
    NotPrimaryExpr();
    if (!NullOut) {
      // As bad as this diagnostic is, it's better than crashing.
      DiagnosticsEngine &Diags = Context.getDiags();
      unsigned DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                       "cannot yet mangle expression type %0");
      Diags.Report(E->getExprLoc(), DiagID)
        << E->getStmtClassName() << E->getSourceRange();
      return;
    }
    break;
  }

  case Expr::CXXUuidofExprClass: {
    NotPrimaryExpr();
    const CXXUuidofExpr *UE = cast<CXXUuidofExpr>(E);
    // As of clang 12, uuidof uses the vendor extended expression
    // mangling. Previously, it used a special-cased nonstandard extension.
    if (!isCompatibleWith(LangOptions::ClangABI::Ver11)) {
      Out << "u8__uuidof";
      if (UE->isTypeOperand())
        mangleType(UE->getTypeOperand(Context.getASTContext()));
      else
        mangleTemplateArgExpr(UE->getExprOperand());
      Out << 'E';
    } else {
      if (UE->isTypeOperand()) {
        QualType UuidT = UE->getTypeOperand(Context.getASTContext());
        Out << "u8__uuidoft";
        mangleType(UuidT);
      } else {
        Expr *UuidExp = UE->getExprOperand();
        Out << "u8__uuidofz";
        mangleExpression(UuidExp);
      }
    }
    break;
  }

  // Even gcc-4.5 doesn't mangle this.
  case Expr::BinaryConditionalOperatorClass: {
    NotPrimaryExpr();
    DiagnosticsEngine &Diags = Context.getDiags();
    unsigned DiagID =
      Diags.getCustomDiagID(DiagnosticsEngine::Error,
                "?: operator with omitted middle operand cannot be mangled");
    Diags.Report(E->getExprLoc(), DiagID)
      << E->getStmtClassName() << E->getSourceRange();
    return;
  }

  // These are used for internal purposes and cannot be meaningfully mangled.
  case Expr::OpaqueValueExprClass:
    llvm_unreachable("cannot mangle opaque value; mangling wrong thing?");

  case Expr::InitListExprClass: {
    NotPrimaryExpr();
    Out << "il";
    mangleInitListElements(cast<InitListExpr>(E));
    Out << "E";
    break;
  }

  case Expr::DesignatedInitExprClass: {
    NotPrimaryExpr();
    auto *DIE = cast<DesignatedInitExpr>(E);
    for (const auto &Designator : DIE->designators()) {
      if (Designator.isFieldDesignator()) {
        Out << "di";
        mangleSourceName(Designator.getFieldName());
      } else if (Designator.isArrayDesignator()) {
        Out << "dx";
        mangleExpression(DIE->getArrayIndex(Designator));
      } else {
        assert(Designator.isArrayRangeDesignator() &&
               "unknown designator kind");
        Out << "dX";
        mangleExpression(DIE->getArrayRangeStart(Designator));
        mangleExpression(DIE->getArrayRangeEnd(Designator));
      }
    }
    mangleExpression(DIE->getInit());
    break;
  }

  case Expr::CXXDefaultArgExprClass:
    E = cast<CXXDefaultArgExpr>(E)->getExpr();
    goto recurse;

  case Expr::CXXDefaultInitExprClass:
    E = cast<CXXDefaultInitExpr>(E)->getExpr();
    goto recurse;

  case Expr::CXXStdInitializerListExprClass:
    E = cast<CXXStdInitializerListExpr>(E)->getSubExpr();
    goto recurse;

  case Expr::SubstNonTypeTemplateParmExprClass: {
    // Mangle a substituted parameter the same way we mangle the template
    // argument.
    auto *SNTTPE = cast<SubstNonTypeTemplateParmExpr>(E);
    if (auto *CE = dyn_cast<ConstantExpr>(SNTTPE->getReplacement())) {
      // Pull out the constant value and mangle it as a template argument.
      QualType ParamType = SNTTPE->getParameterType(Context.getASTContext());
      assert(CE->hasAPValueResult() && "expected the NTTP to have an APValue");
      mangleValueInTemplateArg(ParamType, CE->getAPValueResult(), false,
                               /*NeedExactType=*/true);
      break;
    }
    // The remaining cases all happen to be substituted with expressions that
    // mangle the same as a corresponding template argument anyway.
    E = cast<SubstNonTypeTemplateParmExpr>(E)->getReplacement();
    goto recurse;
  }

  case Expr::UserDefinedLiteralClass:
    // We follow g++'s approach of mangling a UDL as a call to the literal
    // operator.
  case Expr::CXXMemberCallExprClass: // fallthrough
  case Expr::CallExprClass: {
    NotPrimaryExpr();
    const CallExpr *CE = cast<CallExpr>(E);

    // <expression> ::= cp <simple-id> <expression>* E
    // We use this mangling only when the call would use ADL except
    // for being parenthesized.  Per discussion with David
    // Vandervoorde, 2011.04.25.
    if (isParenthesizedADLCallee(CE)) {
      Out << "cp";
      // The callee here is a parenthesized UnresolvedLookupExpr with
      // no qualifier and should always get mangled as a <simple-id>
      // anyway.

    // <expression> ::= cl <expression>* E
    } else {
      Out << "cl";
    }

    unsigned CallArity = CE->getNumArgs();
    for (const Expr *Arg : CE->arguments())
      if (isa<PackExpansionExpr>(Arg))
        CallArity = UnknownArity;

    mangleExpression(CE->getCallee(), CallArity);
    for (const Expr *Arg : CE->arguments())
      mangleExpression(Arg);
    Out << 'E';
    break;
  }

  case Expr::CXXNewExprClass: {
    NotPrimaryExpr();
    const CXXNewExpr *New = cast<CXXNewExpr>(E);
    if (New->isGlobalNew()) Out << "gs";
    Out << (New->isArray() ? "na" : "nw");
    for (CXXNewExpr::const_arg_iterator I = New->placement_arg_begin(),
           E = New->placement_arg_end(); I != E; ++I)
      mangleExpression(*I);
    Out << '_';
    mangleType(New->getAllocatedType());
    if (New->hasInitializer()) {
      if (New->getInitializationStyle() == CXXNewInitializationStyle::Braces)
        Out << "il";
      else
        Out << "pi";
      const Expr *Init = New->getInitializer();
      if (const CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(Init)) {
        // Directly inline the initializers.
        for (CXXConstructExpr::const_arg_iterator I = CCE->arg_begin(),
                                                  E = CCE->arg_end();
             I != E; ++I)
          mangleExpression(*I);
      } else if (const ParenListExpr *PLE = dyn_cast<ParenListExpr>(Init)) {
        for (unsigned i = 0, e = PLE->getNumExprs(); i != e; ++i)
          mangleExpression(PLE->getExpr(i));
      } else if (New->getInitializationStyle() ==
                     CXXNewInitializationStyle::Braces &&
                 isa<InitListExpr>(Init)) {
        // Only take InitListExprs apart for list-initialization.
        mangleInitListElements(cast<InitListExpr>(Init));
      } else
        mangleExpression(Init);
    }
    Out << 'E';
    break;
  }

  case Expr::CXXPseudoDestructorExprClass: {
    NotPrimaryExpr();
    const auto *PDE = cast<CXXPseudoDestructorExpr>(E);
    if (const Expr *Base = PDE->getBase())
      mangleMemberExprBase(Base, PDE->isArrow());
    NestedNameSpecifier *Qualifier = PDE->getQualifier();
    if (TypeSourceInfo *ScopeInfo = PDE->getScopeTypeInfo()) {
      if (Qualifier) {
        mangleUnresolvedPrefix(Qualifier,
                               /*recursive=*/true);
        mangleUnresolvedTypeOrSimpleId(ScopeInfo->getType());
        Out << 'E';
      } else {
        Out << "sr";
        if (!mangleUnresolvedTypeOrSimpleId(ScopeInfo->getType()))
          Out << 'E';
      }
    } else if (Qualifier) {
      mangleUnresolvedPrefix(Qualifier);
    }
    // <base-unresolved-name> ::= dn <destructor-name>
    Out << "dn";
    QualType DestroyedType = PDE->getDestroyedType();
    mangleUnresolvedTypeOrSimpleId(DestroyedType);
    break;
  }

  case Expr::MemberExprClass: {
    NotPrimaryExpr();
    const MemberExpr *ME = cast<MemberExpr>(E);
    mangleMemberExpr(ME->getBase(), ME->isArrow(),
                     ME->getQualifier(), nullptr,
                     ME->getMemberDecl()->getDeclName(),
                     ME->getTemplateArgs(), ME->getNumTemplateArgs(),
                     Arity);
    break;
  }

  case Expr::UnresolvedMemberExprClass: {
    NotPrimaryExpr();
    const UnresolvedMemberExpr *ME = cast<UnresolvedMemberExpr>(E);
    mangleMemberExpr(ME->isImplicitAccess() ? nullptr : ME->getBase(),
                     ME->isArrow(), ME->getQualifier(), nullptr,
                     ME->getMemberName(),
                     ME->getTemplateArgs(), ME->getNumTemplateArgs(),
                     Arity);
    break;
  }

  case Expr::CXXDependentScopeMemberExprClass: {
    NotPrimaryExpr();
    const CXXDependentScopeMemberExpr *ME
      = cast<CXXDependentScopeMemberExpr>(E);
    mangleMemberExpr(ME->isImplicitAccess() ? nullptr : ME->getBase(),
                     ME->isArrow(), ME->getQualifier(),
                     ME->getFirstQualifierFoundInScope(),
                     ME->getMember(),
                     ME->getTemplateArgs(), ME->getNumTemplateArgs(),
                     Arity);
    break;
  }

  case Expr::UnresolvedLookupExprClass: {
    NotPrimaryExpr();
    const UnresolvedLookupExpr *ULE = cast<UnresolvedLookupExpr>(E);
    mangleUnresolvedName(ULE->getQualifier(), ULE->getName(),
                         ULE->getTemplateArgs(), ULE->getNumTemplateArgs(),
                         Arity);
    break;
  }

  case Expr::CXXUnresolvedConstructExprClass: {
    NotPrimaryExpr();
    const CXXUnresolvedConstructExpr *CE = cast<CXXUnresolvedConstructExpr>(E);
    unsigned N = CE->getNumArgs();

    if (CE->isListInitialization()) {
      assert(N == 1 && "unexpected form for list initialization");
      auto *IL = cast<InitListExpr>(CE->getArg(0));
      Out << "tl";
      mangleType(CE->getType());
      mangleInitListElements(IL);
      Out << "E";
      break;
    }

    Out << "cv";
    mangleType(CE->getType());
    if (N != 1) Out << '_';
    for (unsigned I = 0; I != N; ++I) mangleExpression(CE->getArg(I));
    if (N != 1) Out << 'E';
    break;
  }

  case Expr::CXXConstructExprClass: {
    // An implicit cast is silent, thus may contain <expr-primary>.
    const auto *CE = cast<CXXConstructExpr>(E);
    if (!CE->isListInitialization() || CE->isStdInitListInitialization()) {
      assert(
          CE->getNumArgs() >= 1 &&
          (CE->getNumArgs() == 1 || isa<CXXDefaultArgExpr>(CE->getArg(1))) &&
          "implicit CXXConstructExpr must have one argument");
      E = cast<CXXConstructExpr>(E)->getArg(0);
      goto recurse;
    }
    NotPrimaryExpr();
    Out << "il";
    for (auto *E : CE->arguments())
      mangleExpression(E);
    Out << "E";
    break;
  }

  case Expr::CXXTemporaryObjectExprClass: {
    NotPrimaryExpr();
    const auto *CE = cast<CXXTemporaryObjectExpr>(E);
    unsigned N = CE->getNumArgs();
    bool List = CE->isListInitialization();

    if (List)
      Out << "tl";
    else
      Out << "cv";
    mangleType(CE->getType());
    if (!List && N != 1)
      Out << '_';
    if (CE->isStdInitListInitialization()) {
      // We implicitly created a std::initializer_list<T> for the first argument
      // of a constructor of type U in an expression of the form U{a, b, c}.
      // Strip all the semantic gunk off the initializer list.
      auto *SILE =
          cast<CXXStdInitializerListExpr>(CE->getArg(0)->IgnoreImplicit());
      auto *ILE = cast<InitListExpr>(SILE->getSubExpr()->IgnoreImplicit());
      mangleInitListElements(ILE);
    } else {
      for (auto *E : CE->arguments())
        mangleExpression(E);
    }
    if (List || N != 1)
      Out << 'E';
    break;
  }

  case Expr::CXXScalarValueInitExprClass:
    NotPrimaryExpr();
    Out << "cv";
    mangleType(E->getType());
    Out << "_E";
    break;

  case Expr::CXXNoexceptExprClass:
    NotPrimaryExpr();
    Out << "nx";
    mangleExpression(cast<CXXNoexceptExpr>(E)->getOperand());
    break;

  case Expr::UnaryExprOrTypeTraitExprClass: {
    // Non-instantiation-dependent traits are an <expr-primary> integer literal.
    const UnaryExprOrTypeTraitExpr *SAE = cast<UnaryExprOrTypeTraitExpr>(E);

    if (!SAE->isInstantiationDependent()) {
      // Itanium C++ ABI:
      //   If the operand of a sizeof or alignof operator is not
      //   instantiation-dependent it is encoded as an integer literal
      //   reflecting the result of the operator.
      //
      //   If the result of the operator is implicitly converted to a known
      //   integer type, that type is used for the literal; otherwise, the type
      //   of std::size_t or std::ptrdiff_t is used.
      //
      // FIXME: We still include the operand in the profile in this case. This
      // can lead to mangling collisions between function templates that we
      // consider to be different.
      QualType T = (ImplicitlyConvertedToType.isNull() ||
                    !ImplicitlyConvertedToType->isIntegerType())? SAE->getType()
                                                    : ImplicitlyConvertedToType;
      llvm::APSInt V = SAE->EvaluateKnownConstInt(Context.getASTContext());
      mangleIntegerLiteral(T, V);
      break;
    }

    NotPrimaryExpr(); // But otherwise, they are not.

    auto MangleAlignofSizeofArg = [&] {
      if (SAE->isArgumentType()) {
        Out << 't';
        mangleType(SAE->getArgumentType());
      } else {
        Out << 'z';
        mangleExpression(SAE->getArgumentExpr());
      }
    };

    switch(SAE->getKind()) {
    case UETT_SizeOf:
      Out << 's';
      MangleAlignofSizeofArg();
      break;
    case UETT_PreferredAlignOf:
      // As of clang 12, we mangle __alignof__ differently than alignof. (They
      // have acted differently since Clang 8, but were previously mangled the
      // same.)
      if (!isCompatibleWith(LangOptions::ClangABI::Ver11)) {
        Out << "u11__alignof__";
        if (SAE->isArgumentType())
          mangleType(SAE->getArgumentType());
        else
          mangleTemplateArgExpr(SAE->getArgumentExpr());
        Out << 'E';
        break;
      }
      [[fallthrough]];
    case UETT_AlignOf:
      Out << 'a';
      MangleAlignofSizeofArg();
      break;
    case UETT_DataSizeOf: {
      DiagnosticsEngine &Diags = Context.getDiags();
      unsigned DiagID =
          Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                "cannot yet mangle __datasizeof expression");
      Diags.Report(DiagID);
      return;
    }
    case UETT_PtrAuthTypeDiscriminator: {
      DiagnosticsEngine &Diags = Context.getDiags();
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Error,
          "cannot yet mangle __builtin_ptrauth_type_discriminator expression");
      Diags.Report(E->getExprLoc(), DiagID);
      return;
    }
    case UETT_VecStep: {
      DiagnosticsEngine &Diags = Context.getDiags();
      unsigned DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                     "cannot yet mangle vec_step expression");
      Diags.Report(DiagID);
      return;
    }
    case UETT_OpenMPRequiredSimdAlign: {
      DiagnosticsEngine &Diags = Context.getDiags();
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Error,
          "cannot yet mangle __builtin_omp_required_simd_align expression");
      Diags.Report(DiagID);
      return;
    }
    case UETT_VectorElements: {
      DiagnosticsEngine &Diags = Context.getDiags();
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Error,
          "cannot yet mangle __builtin_vectorelements expression");
      Diags.Report(DiagID);
      return;
    }
    }
    break;
  }

  case Expr::TypeTraitExprClass: {
    //  <expression> ::= u <source-name> <template-arg>* E # vendor extension
    const TypeTraitExpr *TTE = cast<TypeTraitExpr>(E);
    NotPrimaryExpr();
    Out << 'u';
    llvm::StringRef Spelling = getTraitSpelling(TTE->getTrait());
    Out << Spelling.size() << Spelling;
    for (TypeSourceInfo *TSI : TTE->getArgs()) {
      mangleType(TSI->getType());
    }
    Out << 'E';
    break;
  }

  case Expr::CXXThrowExprClass: {
    NotPrimaryExpr();
    const CXXThrowExpr *TE = cast<CXXThrowExpr>(E);
    //  <expression> ::= tw <expression>  # throw expression
    //               ::= tr               # rethrow
    if (TE->getSubExpr()) {
      Out << "tw";
      mangleExpression(TE->getSubExpr());
    } else {
      Out << "tr";
    }
    break;
  }

  case Expr::CXXTypeidExprClass: {
    NotPrimaryExpr();
    const CXXTypeidExpr *TIE = cast<CXXTypeidExpr>(E);
    //  <expression> ::= ti <type>        # typeid (type)
    //               ::= te <expression>  # typeid (expression)
    if (TIE->isTypeOperand()) {
      Out << "ti";
      mangleType(TIE->getTypeOperand(Context.getASTContext()));
    } else {
      Out << "te";
      mangleExpression(TIE->getExprOperand());
    }
    break;
  }

  case Expr::CXXDeleteExprClass: {
    NotPrimaryExpr();
    const CXXDeleteExpr *DE = cast<CXXDeleteExpr>(E);
    //  <expression> ::= [gs] dl <expression>  # [::] delete expr
    //               ::= [gs] da <expression>  # [::] delete [] expr
    if (DE->isGlobalDelete()) Out << "gs";
    Out << (DE->isArrayForm() ? "da" : "dl");
    mangleExpression(DE->getArgument());
    break;
  }

  case Expr::UnaryOperatorClass: {
    NotPrimaryExpr();
    const UnaryOperator *UO = cast<UnaryOperator>(E);
    mangleOperatorName(UnaryOperator::getOverloadedOperator(UO->getOpcode()),
                       /*Arity=*/1);
    mangleExpression(UO->getSubExpr());
    break;
  }

  case Expr::ArraySubscriptExprClass: {
    NotPrimaryExpr();
    const ArraySubscriptExpr *AE = cast<ArraySubscriptExpr>(E);

    // Array subscript is treated as a syntactically weird form of
    // binary operator.
    Out << "ix";
    mangleExpression(AE->getLHS());
    mangleExpression(AE->getRHS());
    break;
  }

  case Expr::MatrixSubscriptExprClass: {
    NotPrimaryExpr();
    const MatrixSubscriptExpr *ME = cast<MatrixSubscriptExpr>(E);
    Out << "ixix";
    mangleExpression(ME->getBase());
    mangleExpression(ME->getRowIdx());
    mangleExpression(ME->getColumnIdx());
    break;
  }

  case Expr::CompoundAssignOperatorClass: // fallthrough
  case Expr::BinaryOperatorClass: {
    NotPrimaryExpr();
    const BinaryOperator *BO = cast<BinaryOperator>(E);
    if (BO->getOpcode() == BO_PtrMemD)
      Out << "ds";
    else
      mangleOperatorName(BinaryOperator::getOverloadedOperator(BO->getOpcode()),
                         /*Arity=*/2);
    mangleExpression(BO->getLHS());
    mangleExpression(BO->getRHS());
    break;
  }

  case Expr::CXXRewrittenBinaryOperatorClass: {
    NotPrimaryExpr();
    // The mangled form represents the original syntax.
    CXXRewrittenBinaryOperator::DecomposedForm Decomposed =
        cast<CXXRewrittenBinaryOperator>(E)->getDecomposedForm();
    mangleOperatorName(BinaryOperator::getOverloadedOperator(Decomposed.Opcode),
                       /*Arity=*/2);
    mangleExpression(Decomposed.LHS);
    mangleExpression(Decomposed.RHS);
    break;
  }

  case Expr::ConditionalOperatorClass: {
    NotPrimaryExpr();
    const ConditionalOperator *CO = cast<ConditionalOperator>(E);
    mangleOperatorName(OO_Conditional, /*Arity=*/3);
    mangleExpression(CO->getCond());
    mangleExpression(CO->getLHS(), Arity);
    mangleExpression(CO->getRHS(), Arity);
    break;
  }

  case Expr::ImplicitCastExprClass: {
    ImplicitlyConvertedToType = E->getType();
    E = cast<ImplicitCastExpr>(E)->getSubExpr();
    goto recurse;
  }

  case Expr::ObjCBridgedCastExprClass: {
    NotPrimaryExpr();
    // Mangle ownership casts as a vendor extended operator __bridge,
    // __bridge_transfer, or __bridge_retain.
    StringRef Kind = cast<ObjCBridgedCastExpr>(E)->getBridgeKindName();
    Out << "v1U" << Kind.size() << Kind;
    mangleCastExpression(E, "cv");
    break;
  }

  case Expr::CStyleCastExprClass:
    NotPrimaryExpr();
    mangleCastExpression(E, "cv");
    break;

  case Expr::CXXFunctionalCastExprClass: {
    NotPrimaryExpr();
    auto *Sub = cast<ExplicitCastExpr>(E)->getSubExpr()->IgnoreImplicit();
    // FIXME: Add isImplicit to CXXConstructExpr.
    if (auto *CCE = dyn_cast<CXXConstructExpr>(Sub))
      if (CCE->getParenOrBraceRange().isInvalid())
        Sub = CCE->getArg(0)->IgnoreImplicit();
    if (auto *StdInitList = dyn_cast<CXXStdInitializerListExpr>(Sub))
      Sub = StdInitList->getSubExpr()->IgnoreImplicit();
    if (auto *IL = dyn_cast<InitListExpr>(Sub)) {
      Out << "tl";
      mangleType(E->getType());
      mangleInitListElements(IL);
      Out << "E";
    } else {
      mangleCastExpression(E, "cv");
    }
    break;
  }

  case Expr::CXXStaticCastExprClass:
    NotPrimaryExpr();
    mangleCastExpression(E, "sc");
    break;
  case Expr::CXXDynamicCastExprClass:
    NotPrimaryExpr();
    mangleCastExpression(E, "dc");
    break;
  case Expr::CXXReinterpretCastExprClass:
    NotPrimaryExpr();
    mangleCastExpression(E, "rc");
    break;
  case Expr::CXXConstCastExprClass:
    NotPrimaryExpr();
    mangleCastExpression(E, "cc");
    break;
  case Expr::CXXAddrspaceCastExprClass:
    NotPrimaryExpr();
    mangleCastExpression(E, "ac");
    break;

  case Expr::CXXOperatorCallExprClass: {
    NotPrimaryExpr();
    const CXXOperatorCallExpr *CE = cast<CXXOperatorCallExpr>(E);
    unsigned NumArgs = CE->getNumArgs();
    // A CXXOperatorCallExpr for OO_Arrow models only semantics, not syntax
    // (the enclosing MemberExpr covers the syntactic portion).
    if (CE->getOperator() != OO_Arrow)
      mangleOperatorName(CE->getOperator(), /*Arity=*/NumArgs);
    // Mangle the arguments.
    for (unsigned i = 0; i != NumArgs; ++i)
      mangleExpression(CE->getArg(i));
    break;
  }

  case Expr::ParenExprClass:
    E = cast<ParenExpr>(E)->getSubExpr();
    goto recurse;

  case Expr::ConceptSpecializationExprClass: {
    auto *CSE = cast<ConceptSpecializationExpr>(E);
    if (isCompatibleWith(LangOptions::ClangABI::Ver17)) {
      // Clang 17 and before mangled concept-ids as if they resolved to an
      // entity, meaning that references to enclosing template arguments don't
      // work.
      Out << "L_Z";
      mangleTemplateName(CSE->getNamedConcept(), CSE->getTemplateArguments());
      Out << 'E';
      break;
    }
    // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/24.
    NotPrimaryExpr();
    mangleUnresolvedName(
        CSE->getNestedNameSpecifierLoc().getNestedNameSpecifier(),
        CSE->getConceptNameInfo().getName(),
        CSE->getTemplateArgsAsWritten()->getTemplateArgs(),
        CSE->getTemplateArgsAsWritten()->getNumTemplateArgs());
    break;
  }

  case Expr::RequiresExprClass: {
    // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/24.
    auto *RE = cast<RequiresExpr>(E);
    // This is a primary-expression in the C++ grammar, but does not have an
    // <expr-primary> mangling (starting with 'L').
    NotPrimaryExpr();
    if (RE->getLParenLoc().isValid()) {
      Out << "rQ";
      FunctionTypeDepthState saved = FunctionTypeDepth.push();
      if (RE->getLocalParameters().empty()) {
        Out << 'v';
      } else {
        for (ParmVarDecl *Param : RE->getLocalParameters()) {
          mangleType(Context.getASTContext().getSignatureParameterType(
              Param->getType()));
        }
      }
      Out << '_';

      // The rest of the mangling is in the immediate scope of the parameters.
      FunctionTypeDepth.enterResultType();
      for (const concepts::Requirement *Req : RE->getRequirements())
        mangleRequirement(RE->getExprLoc(), Req);
      FunctionTypeDepth.pop(saved);
      Out << 'E';
    } else {
      Out << "rq";
      for (const concepts::Requirement *Req : RE->getRequirements())
        mangleRequirement(RE->getExprLoc(), Req);
      Out << 'E';
    }
    break;
  }

  case Expr::DeclRefExprClass:
    // MangleDeclRefExpr helper handles primary-vs-nonprimary
    MangleDeclRefExpr(cast<DeclRefExpr>(E)->getDecl());
    break;

  case Expr::SubstNonTypeTemplateParmPackExprClass:
    NotPrimaryExpr();
    // FIXME: not clear how to mangle this!
    // template <unsigned N...> class A {
    //   template <class U...> void foo(U (&x)[N]...);
    // };
    Out << "_SUBSTPACK_";
    break;

  case Expr::FunctionParmPackExprClass: {
    NotPrimaryExpr();
    // FIXME: not clear how to mangle this!
    const FunctionParmPackExpr *FPPE = cast<FunctionParmPackExpr>(E);
    Out << "v110_SUBSTPACK";
    MangleDeclRefExpr(FPPE->getParameterPack());
    break;
  }

  case Expr::DependentScopeDeclRefExprClass: {
    NotPrimaryExpr();
    const DependentScopeDeclRefExpr *DRE = cast<DependentScopeDeclRefExpr>(E);
    mangleUnresolvedName(DRE->getQualifier(), DRE->getDeclName(),
                         DRE->getTemplateArgs(), DRE->getNumTemplateArgs(),
                         Arity);
    break;
  }

  case Expr::CXXBindTemporaryExprClass:
    E = cast<CXXBindTemporaryExpr>(E)->getSubExpr();
    goto recurse;

  case Expr::ExprWithCleanupsClass:
    E = cast<ExprWithCleanups>(E)->getSubExpr();
    goto recurse;

  case Expr::FloatingLiteralClass: {
    // <expr-primary>
    const FloatingLiteral *FL = cast<FloatingLiteral>(E);
    mangleFloatLiteral(FL->getType(), FL->getValue());
    break;
  }

  case Expr::FixedPointLiteralClass:
    // Currently unimplemented -- might be <expr-primary> in future?
    mangleFixedPointLiteral();
    break;

  case Expr::CharacterLiteralClass:
    // <expr-primary>
    Out << 'L';
    mangleType(E->getType());
    Out << cast<CharacterLiteral>(E)->getValue();
    Out << 'E';
    break;

  // FIXME. __objc_yes/__objc_no are mangled same as true/false
  case Expr::ObjCBoolLiteralExprClass:
    // <expr-primary>
    Out << "Lb";
    Out << (cast<ObjCBoolLiteralExpr>(E)->getValue() ? '1' : '0');
    Out << 'E';
    break;

  case Expr::CXXBoolLiteralExprClass:
    // <expr-primary>
    Out << "Lb";
    Out << (cast<CXXBoolLiteralExpr>(E)->getValue() ? '1' : '0');
    Out << 'E';
    break;

  case Expr::IntegerLiteralClass: {
    // <expr-primary>
    llvm::APSInt Value(cast<IntegerLiteral>(E)->getValue());
    if (E->getType()->isSignedIntegerType())
      Value.setIsSigned(true);
    mangleIntegerLiteral(E->getType(), Value);
    break;
  }

  case Expr::ImaginaryLiteralClass: {
    // <expr-primary>
    const ImaginaryLiteral *IE = cast<ImaginaryLiteral>(E);
    // Mangle as if a complex literal.
    // Proposal from David Vandevoorde, 2010.06.30.
    Out << 'L';
    mangleType(E->getType());
    if (const FloatingLiteral *Imag =
          dyn_cast<FloatingLiteral>(IE->getSubExpr())) {
      // Mangle a floating-point zero of the appropriate type.
      mangleFloat(llvm::APFloat(Imag->getValue().getSemantics()));
      Out << '_';
      mangleFloat(Imag->getValue());
    } else {
      Out << "0_";
      llvm::APSInt Value(cast<IntegerLiteral>(IE->getSubExpr())->getValue());
      if (IE->getSubExpr()->getType()->isSignedIntegerType())
        Value.setIsSigned(true);
      mangleNumber(Value);
    }
    Out << 'E';
    break;
  }

  case Expr::StringLiteralClass: {
    // <expr-primary>
    // Revised proposal from David Vandervoorde, 2010.07.15.
    Out << 'L';
    assert(isa<ConstantArrayType>(E->getType()));
    mangleType(E->getType());
    Out << 'E';
    break;
  }

  case Expr::GNUNullExprClass:
    // <expr-primary>
    // Mangle as if an integer literal 0.
    mangleIntegerLiteral(E->getType(), llvm::APSInt(32));
    break;

  case Expr::CXXNullPtrLiteralExprClass: {
    // <expr-primary>
    Out << "LDnE";
    break;
  }

  case Expr::LambdaExprClass: {
    // A lambda-expression can't appear in the signature of an
    // externally-visible declaration, so there's no standard mangling for
    // this, but mangling as a literal of the closure type seems reasonable.
    Out << "L";
    mangleType(Context.getASTContext().getRecordType(cast<LambdaExpr>(E)->getLambdaClass()));
    Out << "E";
    break;
  }

  case Expr::PackExpansionExprClass:
    NotPrimaryExpr();
    Out << "sp";
    mangleExpression(cast<PackExpansionExpr>(E)->getPattern());
    break;

  case Expr::SizeOfPackExprClass: {
    NotPrimaryExpr();
    auto *SPE = cast<SizeOfPackExpr>(E);
    if (SPE->isPartiallySubstituted()) {
      Out << "sP";
      for (const auto &A : SPE->getPartialArguments())
        mangleTemplateArg(A, false);
      Out << "E";
      break;
    }

    Out << "sZ";
    const NamedDecl *Pack = SPE->getPack();
    if (const TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(Pack))
      mangleTemplateParameter(TTP->getDepth(), TTP->getIndex());
    else if (const NonTypeTemplateParmDecl *NTTP
                = dyn_cast<NonTypeTemplateParmDecl>(Pack))
      mangleTemplateParameter(NTTP->getDepth(), NTTP->getIndex());
    else if (const TemplateTemplateParmDecl *TempTP
                                    = dyn_cast<TemplateTemplateParmDecl>(Pack))
      mangleTemplateParameter(TempTP->getDepth(), TempTP->getIndex());
    else
      mangleFunctionParam(cast<ParmVarDecl>(Pack));
    break;
  }

  case Expr::MaterializeTemporaryExprClass:
    E = cast<MaterializeTemporaryExpr>(E)->getSubExpr();
    goto recurse;

  case Expr::CXXFoldExprClass: {
    NotPrimaryExpr();
    auto *FE = cast<CXXFoldExpr>(E);
    if (FE->isLeftFold())
      Out << (FE->getInit() ? "fL" : "fl");
    else
      Out << (FE->getInit() ? "fR" : "fr");

    if (FE->getOperator() == BO_PtrMemD)
      Out << "ds";
    else
      mangleOperatorName(
          BinaryOperator::getOverloadedOperator(FE->getOperator()),
          /*Arity=*/2);

    if (FE->getLHS())
      mangleExpression(FE->getLHS());
    if (FE->getRHS())
      mangleExpression(FE->getRHS());
    break;
  }

  case Expr::CXXThisExprClass:
    NotPrimaryExpr();
    Out << "fpT";
    break;

  case Expr::CoawaitExprClass:
    // FIXME: Propose a non-vendor mangling.
    NotPrimaryExpr();
    Out << "v18co_await";
    mangleExpression(cast<CoawaitExpr>(E)->getOperand());
    break;

  case Expr::DependentCoawaitExprClass:
    // FIXME: Propose a non-vendor mangling.
    NotPrimaryExpr();
    Out << "v18co_await";
    mangleExpression(cast<DependentCoawaitExpr>(E)->getOperand());
    break;

  case Expr::CoyieldExprClass:
    // FIXME: Propose a non-vendor mangling.
    NotPrimaryExpr();
    Out << "v18co_yield";
    mangleExpression(cast<CoawaitExpr>(E)->getOperand());
    break;
  case Expr::SYCLUniqueStableNameExprClass: {
    const auto *USN = cast<SYCLUniqueStableNameExpr>(E);
    NotPrimaryExpr();

    Out << "u33__builtin_sycl_unique_stable_name";
    mangleType(USN->getTypeSourceInfo()->getType());

    Out << "E";
    break;
  }
  }

  if (AsTemplateArg && !IsPrimaryExpr)
    Out << 'E';
}

/// Mangle an expression which refers to a parameter variable.
///
/// <expression>     ::= <function-param>
/// <function-param> ::= fp <top-level CV-qualifiers> _      # L == 0, I == 0
/// <function-param> ::= fp <top-level CV-qualifiers>
///                      <parameter-2 non-negative number> _ # L == 0, I > 0
/// <function-param> ::= fL <L-1 non-negative number>
///                      p <top-level CV-qualifiers> _       # L > 0, I == 0
/// <function-param> ::= fL <L-1 non-negative number>
///                      p <top-level CV-qualifiers>
///                      <I-1 non-negative number> _         # L > 0, I > 0
///
/// L is the nesting depth of the parameter, defined as 1 if the
/// parameter comes from the innermost function prototype scope
/// enclosing the current context, 2 if from the next enclosing
/// function prototype scope, and so on, with one special case: if
/// we've processed the full parameter clause for the innermost
/// function type, then L is one less.  This definition conveniently
/// makes it irrelevant whether a function's result type was written
/// trailing or leading, but is otherwise overly complicated; the
/// numbering was first designed without considering references to
/// parameter in locations other than return types, and then the
/// mangling had to be generalized without changing the existing
/// manglings.
///
/// I is the zero-based index of the parameter within its parameter
/// declaration clause.  Note that the original ABI document describes
/// this using 1-based ordinals.
void CXXNameMangler::mangleFunctionParam(const ParmVarDecl *parm) {
  unsigned parmDepth = parm->getFunctionScopeDepth();
  unsigned parmIndex = parm->getFunctionScopeIndex();

  // Compute 'L'.
  // parmDepth does not include the declaring function prototype.
  // FunctionTypeDepth does account for that.
  assert(parmDepth < FunctionTypeDepth.getDepth());
  unsigned nestingDepth = FunctionTypeDepth.getDepth() - parmDepth;
  if (FunctionTypeDepth.isInResultType())
    nestingDepth--;

  if (nestingDepth == 0) {
    Out << "fp";
  } else {
    Out << "fL" << (nestingDepth - 1) << 'p';
  }

  // Top-level qualifiers.  We don't have to worry about arrays here,
  // because parameters declared as arrays should already have been
  // transformed to have pointer type. FIXME: apparently these don't
  // get mangled if used as an rvalue of a known non-class type?
  assert(!parm->getType()->isArrayType()
         && "parameter's type is still an array type?");

  if (const DependentAddressSpaceType *DAST =
      dyn_cast<DependentAddressSpaceType>(parm->getType())) {
    mangleQualifiers(DAST->getPointeeType().getQualifiers(), DAST);
  } else {
    mangleQualifiers(parm->getType().getQualifiers());
  }

  // Parameter index.
  if (parmIndex != 0) {
    Out << (parmIndex - 1);
  }
  Out << '_';
}

void CXXNameMangler::mangleCXXCtorType(CXXCtorType T,
                                       const CXXRecordDecl *InheritedFrom) {
  // <ctor-dtor-name> ::= C1  # complete object constructor
  //                  ::= C2  # base object constructor
  //                  ::= CI1 <type> # complete inheriting constructor
  //                  ::= CI2 <type> # base inheriting constructor
  //
  // In addition, C5 is a comdat name with C1 and C2 in it.
  Out << 'C';
  if (InheritedFrom)
    Out << 'I';
  switch (T) {
  case Ctor_Complete:
    Out << '1';
    break;
  case Ctor_Base:
    Out << '2';
    break;
  case Ctor_Comdat:
    Out << '5';
    break;
  case Ctor_DefaultClosure:
  case Ctor_CopyingClosure:
    llvm_unreachable("closure constructors don't exist for the Itanium ABI!");
  }
  if (InheritedFrom)
    mangleName(InheritedFrom);
}

void CXXNameMangler::mangleCXXDtorType(CXXDtorType T) {
  // <ctor-dtor-name> ::= D0  # deleting destructor
  //                  ::= D1  # complete object destructor
  //                  ::= D2  # base object destructor
  //
  // In addition, D5 is a comdat name with D1, D2 and, if virtual, D0 in it.
  switch (T) {
  case Dtor_Deleting:
    Out << "D0";
    break;
  case Dtor_Complete:
    Out << "D1";
    break;
  case Dtor_Base:
    Out << "D2";
    break;
  case Dtor_Comdat:
    Out << "D5";
    break;
  }
}

// Helper to provide ancillary information on a template used to mangle its
// arguments.
struct CXXNameMangler::TemplateArgManglingInfo {
  const CXXNameMangler &Mangler;
  TemplateDecl *ResolvedTemplate = nullptr;
  bool SeenPackExpansionIntoNonPack = false;
  const NamedDecl *UnresolvedExpandedPack = nullptr;

  TemplateArgManglingInfo(const CXXNameMangler &Mangler, TemplateName TN)
      : Mangler(Mangler) {
    if (TemplateDecl *TD = TN.getAsTemplateDecl())
      ResolvedTemplate = TD;
  }

  /// Information about how to mangle a template argument.
  struct Info {
    /// Do we need to mangle the template argument with an exactly correct type?
    bool NeedExactType;
    /// If we need to prefix the mangling with a mangling of the template
    /// parameter, the corresponding parameter.
    const NamedDecl *TemplateParameterToMangle;
  };

  /// Determine whether the resolved template might be overloaded on its
  /// template parameter list. If so, the mangling needs to include enough
  /// information to reconstruct the template parameter list.
  bool isOverloadable() {
    // Function templates are generally overloadable. As a special case, a
    // member function template of a generic lambda is not overloadable.
    if (auto *FTD = dyn_cast_or_null<FunctionTemplateDecl>(ResolvedTemplate)) {
      auto *RD = dyn_cast<CXXRecordDecl>(FTD->getDeclContext());
      if (!RD || !RD->isGenericLambda())
        return true;
    }

    // All other templates are not overloadable. Partial specializations would
    // be, but we never mangle them.
    return false;
  }

  /// Determine whether we need to prefix this <template-arg> mangling with a
  /// <template-param-decl>. This happens if the natural template parameter for
  /// the argument mangling is not the same as the actual template parameter.
  bool needToMangleTemplateParam(const NamedDecl *Param,
                                 const TemplateArgument &Arg) {
    // For a template type parameter, the natural parameter is 'typename T'.
    // The actual parameter might be constrained.
    if (auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param))
      return TTP->hasTypeConstraint();

    if (Arg.getKind() == TemplateArgument::Pack) {
      // For an empty pack, the natural parameter is `typename...`.
      if (Arg.pack_size() == 0)
        return true;

      // For any other pack, we use the first argument to determine the natural
      // template parameter.
      return needToMangleTemplateParam(Param, *Arg.pack_begin());
    }

    // For a non-type template parameter, the natural parameter is `T V` (for a
    // prvalue argument) or `T &V` (for a glvalue argument), where `T` is the
    // type of the argument, which we require to exactly match. If the actual
    // parameter has a deduced or instantiation-dependent type, it is not
    // equivalent to the natural parameter.
    if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param))
      return NTTP->getType()->isInstantiationDependentType() ||
             NTTP->getType()->getContainedDeducedType();

    // For a template template parameter, the template-head might differ from
    // that of the template.
    auto *TTP = cast<TemplateTemplateParmDecl>(Param);
    TemplateName ArgTemplateName = Arg.getAsTemplateOrTemplatePattern();
    const TemplateDecl *ArgTemplate = ArgTemplateName.getAsTemplateDecl();
    if (!ArgTemplate)
      return true;

    // Mangle the template parameter list of the parameter and argument to see
    // if they are the same. We can't use Profile for this, because it can't
    // model the depth difference between parameter and argument and might not
    // necessarily have the same definition of "identical" that we use here --
    // that is, same mangling.
    auto MangleTemplateParamListToString =
        [&](SmallVectorImpl<char> &Buffer, const TemplateParameterList *Params,
            unsigned DepthOffset) {
          llvm::raw_svector_ostream Stream(Buffer);
          CXXNameMangler(Mangler.Context, Stream,
                         WithTemplateDepthOffset{DepthOffset})
              .mangleTemplateParameterList(Params);
        };
    llvm::SmallString<128> ParamTemplateHead, ArgTemplateHead;
    MangleTemplateParamListToString(ParamTemplateHead,
                                    TTP->getTemplateParameters(), 0);
    // Add the depth of the parameter's template parameter list to all
    // parameters appearing in the argument to make the indexes line up
    // properly.
    MangleTemplateParamListToString(ArgTemplateHead,
                                    ArgTemplate->getTemplateParameters(),
                                    TTP->getTemplateParameters()->getDepth());
    return ParamTemplateHead != ArgTemplateHead;
  }

  /// Determine information about how this template argument should be mangled.
  /// This should be called exactly once for each parameter / argument pair, in
  /// order.
  Info getArgInfo(unsigned ParamIdx, const TemplateArgument &Arg) {
    // We need correct types when the template-name is unresolved or when it
    // names a template that is able to be overloaded.
    if (!ResolvedTemplate || SeenPackExpansionIntoNonPack)
      return {true, nullptr};

    // Move to the next parameter.
    const NamedDecl *Param = UnresolvedExpandedPack;
    if (!Param) {
      assert(ParamIdx < ResolvedTemplate->getTemplateParameters()->size() &&
             "no parameter for argument");
      Param = ResolvedTemplate->getTemplateParameters()->getParam(ParamIdx);

      // If we reach a parameter pack whose argument isn't in pack form, that
      // means Sema couldn't or didn't figure out which arguments belonged to
      // it, because it contains a pack expansion or because Sema bailed out of
      // computing parameter / argument correspondence before this point. Track
      // the pack as the corresponding parameter for all further template
      // arguments until we hit a pack expansion, at which point we don't know
      // the correspondence between parameters and arguments at all.
      if (Param->isParameterPack() && Arg.getKind() != TemplateArgument::Pack) {
        UnresolvedExpandedPack = Param;
      }
    }

    // If we encounter a pack argument that is expanded into a non-pack
    // parameter, we can no longer track parameter / argument correspondence,
    // and need to use exact types from this point onwards.
    if (Arg.isPackExpansion() &&
        (!Param->isParameterPack() || UnresolvedExpandedPack)) {
      SeenPackExpansionIntoNonPack = true;
      return {true, nullptr};
    }

    // We need exact types for arguments of a template that might be overloaded
    // on template parameter type.
    if (isOverloadable())
      return {true, needToMangleTemplateParam(Param, Arg) ? Param : nullptr};

    // Otherwise, we only need a correct type if the parameter has a deduced
    // type.
    //
    // Note: for an expanded parameter pack, getType() returns the type prior
    // to expansion. We could ask for the expanded type with getExpansionType(),
    // but it doesn't matter because substitution and expansion don't affect
    // whether a deduced type appears in the type.
    auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param);
    bool NeedExactType = NTTP && NTTP->getType()->getContainedDeducedType();
    return {NeedExactType, nullptr};
  }

  /// Determine if we should mangle a requires-clause after the template
  /// argument list. If so, returns the expression to mangle.
  const Expr *getTrailingRequiresClauseToMangle() {
    if (!isOverloadable())
      return nullptr;
    return ResolvedTemplate->getTemplateParameters()->getRequiresClause();
  }
};

void CXXNameMangler::mangleTemplateArgs(TemplateName TN,
                                        const TemplateArgumentLoc *TemplateArgs,
                                        unsigned NumTemplateArgs) {
  // <template-args> ::= I <template-arg>+ [Q <requires-clause expr>] E
  Out << 'I';
  TemplateArgManglingInfo Info(*this, TN);
  for (unsigned i = 0; i != NumTemplateArgs; ++i) {
    mangleTemplateArg(Info, i, TemplateArgs[i].getArgument());
  }
  mangleRequiresClause(Info.getTrailingRequiresClauseToMangle());
  Out << 'E';
}

void CXXNameMangler::mangleTemplateArgs(TemplateName TN,
                                        const TemplateArgumentList &AL) {
  // <template-args> ::= I <template-arg>+ [Q <requires-clause expr>] E
  Out << 'I';
  TemplateArgManglingInfo Info(*this, TN);
  for (unsigned i = 0, e = AL.size(); i != e; ++i) {
    mangleTemplateArg(Info, i, AL[i]);
  }
  mangleRequiresClause(Info.getTrailingRequiresClauseToMangle());
  Out << 'E';
}

void CXXNameMangler::mangleTemplateArgs(TemplateName TN,
                                        ArrayRef<TemplateArgument> Args) {
  // <template-args> ::= I <template-arg>+ [Q <requires-clause expr>] E
  Out << 'I';
  TemplateArgManglingInfo Info(*this, TN);
  for (unsigned i = 0; i != Args.size(); ++i) {
    mangleTemplateArg(Info, i, Args[i]);
  }
  mangleRequiresClause(Info.getTrailingRequiresClauseToMangle());
  Out << 'E';
}

void CXXNameMangler::mangleTemplateArg(TemplateArgManglingInfo &Info,
                                       unsigned Index, TemplateArgument A) {
  TemplateArgManglingInfo::Info ArgInfo = Info.getArgInfo(Index, A);

  // Proposed on https://github.com/itanium-cxx-abi/cxx-abi/issues/47.
  if (ArgInfo.TemplateParameterToMangle &&
      !isCompatibleWith(LangOptions::ClangABI::Ver17)) {
    // The template parameter is mangled if the mangling would otherwise be
    // ambiguous.
    //
    // <template-arg> ::= <template-param-decl> <template-arg>
    //
    // Clang 17 and before did not do this.
    mangleTemplateParamDecl(ArgInfo.TemplateParameterToMangle);
  }

  mangleTemplateArg(A, ArgInfo.NeedExactType);
}

void CXXNameMangler::mangleTemplateArg(TemplateArgument A, bool NeedExactType) {
  // <template-arg> ::= <type>              # type or template
  //                ::= X <expression> E    # expression
  //                ::= <expr-primary>      # simple expressions
  //                ::= J <template-arg>* E # argument pack
  if (!A.isInstantiationDependent() || A.isDependent())
    A = Context.getASTContext().getCanonicalTemplateArgument(A);

  switch (A.getKind()) {
  case TemplateArgument::Null:
    llvm_unreachable("Cannot mangle NULL template argument");

  case TemplateArgument::Type:
    mangleType(A.getAsType());
    break;
  case TemplateArgument::Template:
    // This is mangled as <type>.
    mangleType(A.getAsTemplate());
    break;
  case TemplateArgument::TemplateExpansion:
    // <type>  ::= Dp <type>          # pack expansion (C++0x)
    Out << "Dp";
    mangleType(A.getAsTemplateOrTemplatePattern());
    break;
  case TemplateArgument::Expression:
    mangleTemplateArgExpr(A.getAsExpr());
    break;
  case TemplateArgument::Integral:
    mangleIntegerLiteral(A.getIntegralType(), A.getAsIntegral());
    break;
  case TemplateArgument::Declaration: {
    //  <expr-primary> ::= L <mangled-name> E # external name
    ValueDecl *D = A.getAsDecl();

    // Template parameter objects are modeled by reproducing a source form
    // produced as if by aggregate initialization.
    if (A.getParamTypeForDecl()->isRecordType()) {
      auto *TPO = cast<TemplateParamObjectDecl>(D);
      mangleValueInTemplateArg(TPO->getType().getUnqualifiedType(),
                               TPO->getValue(), /*TopLevel=*/true,
                               NeedExactType);
      break;
    }

    ASTContext &Ctx = Context.getASTContext();
    APValue Value;
    if (D->isCXXInstanceMember())
      // Simple pointer-to-member with no conversion.
      Value = APValue(D, /*IsDerivedMember=*/false, /*Path=*/{});
    else if (D->getType()->isArrayType() &&
             Ctx.hasSimilarType(Ctx.getDecayedType(D->getType()),
                                A.getParamTypeForDecl()) &&
             !isCompatibleWith(LangOptions::ClangABI::Ver11))
      // Build a value corresponding to this implicit array-to-pointer decay.
      Value = APValue(APValue::LValueBase(D), CharUnits::Zero(),
                      {APValue::LValuePathEntry::ArrayIndex(0)},
                      /*OnePastTheEnd=*/false);
    else
      // Regular pointer or reference to a declaration.
      Value = APValue(APValue::LValueBase(D), CharUnits::Zero(),
                      ArrayRef<APValue::LValuePathEntry>(),
                      /*OnePastTheEnd=*/false);
    mangleValueInTemplateArg(A.getParamTypeForDecl(), Value, /*TopLevel=*/true,
                             NeedExactType);
    break;
  }
  case TemplateArgument::NullPtr: {
    mangleNullPointer(A.getNullPtrType());
    break;
  }
  case TemplateArgument::StructuralValue:
    mangleValueInTemplateArg(A.getStructuralValueType(),
                             A.getAsStructuralValue(),
                             /*TopLevel=*/true, NeedExactType);
    break;
  case TemplateArgument::Pack: {
    //  <template-arg> ::= J <template-arg>* E
    Out << 'J';
    for (const auto &P : A.pack_elements())
      mangleTemplateArg(P, NeedExactType);
    Out << 'E';
  }
  }
}

void CXXNameMangler::mangleTemplateArgExpr(const Expr *E) {
  if (!isCompatibleWith(LangOptions::ClangABI::Ver11)) {
    mangleExpression(E, UnknownArity, /*AsTemplateArg=*/true);
    return;
  }

  // Prior to Clang 12, we didn't omit the X .. E around <expr-primary>
  // correctly in cases where the template argument was
  // constructed from an expression rather than an already-evaluated
  // literal. In such a case, we would then e.g. emit 'XLi0EE' instead of
  // 'Li0E'.
  //
  // We did special-case DeclRefExpr to attempt to DTRT for that one
  // expression-kind, but while doing so, unfortunately handled ParmVarDecl
  // (subtype of VarDecl) _incorrectly_, and emitted 'L_Z .. E' instead of
  // the proper 'Xfp_E'.
  E = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    const ValueDecl *D = DRE->getDecl();
    if (isa<VarDecl>(D) || isa<FunctionDecl>(D)) {
      Out << 'L';
      mangle(D);
      Out << 'E';
      return;
    }
  }
  Out << 'X';
  mangleExpression(E);
  Out << 'E';
}

/// Determine whether a given value is equivalent to zero-initialization for
/// the purpose of discarding a trailing portion of a 'tl' mangling.
///
/// Note that this is not in general equivalent to determining whether the
/// value has an all-zeroes bit pattern.
static bool isZeroInitialized(QualType T, const APValue &V) {
  // FIXME: mangleValueInTemplateArg has quadratic time complexity in
  // pathological cases due to using this, but it's a little awkward
  // to do this in linear time in general.
  switch (V.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
  case APValue::AddrLabelDiff:
    return false;

  case APValue::Struct: {
    const CXXRecordDecl *RD = T->getAsCXXRecordDecl();
    assert(RD && "unexpected type for record value");
    unsigned I = 0;
    for (const CXXBaseSpecifier &BS : RD->bases()) {
      if (!isZeroInitialized(BS.getType(), V.getStructBase(I)))
        return false;
      ++I;
    }
    I = 0;
    for (const FieldDecl *FD : RD->fields()) {
      if (!FD->isUnnamedBitField() &&
          !isZeroInitialized(FD->getType(), V.getStructField(I)))
        return false;
      ++I;
    }
    return true;
  }

  case APValue::Union: {
    const CXXRecordDecl *RD = T->getAsCXXRecordDecl();
    assert(RD && "unexpected type for union value");
    // Zero-initialization zeroes the first non-unnamed-bitfield field, if any.
    for (const FieldDecl *FD : RD->fields()) {
      if (!FD->isUnnamedBitField())
        return V.getUnionField() && declaresSameEntity(FD, V.getUnionField()) &&
               isZeroInitialized(FD->getType(), V.getUnionValue());
    }
    // If there are no fields (other than unnamed bitfields), the value is
    // necessarily zero-initialized.
    return true;
  }

  case APValue::Array: {
    QualType ElemT(T->getArrayElementTypeNoTypeQual(), 0);
    for (unsigned I = 0, N = V.getArrayInitializedElts(); I != N; ++I)
      if (!isZeroInitialized(ElemT, V.getArrayInitializedElt(I)))
        return false;
    return !V.hasArrayFiller() || isZeroInitialized(ElemT, V.getArrayFiller());
  }

  case APValue::Vector: {
    const VectorType *VT = T->castAs<VectorType>();
    for (unsigned I = 0, N = V.getVectorLength(); I != N; ++I)
      if (!isZeroInitialized(VT->getElementType(), V.getVectorElt(I)))
        return false;
    return true;
  }

  case APValue::Int:
    return !V.getInt();

  case APValue::Float:
    return V.getFloat().isPosZero();

  case APValue::FixedPoint:
    return !V.getFixedPoint().getValue();

  case APValue::ComplexFloat:
    return V.getComplexFloatReal().isPosZero() &&
           V.getComplexFloatImag().isPosZero();

  case APValue::ComplexInt:
    return !V.getComplexIntReal() && !V.getComplexIntImag();

  case APValue::LValue:
    return V.isNullPointer();

  case APValue::MemberPointer:
    return !V.getMemberPointerDecl();
  }

  llvm_unreachable("Unhandled APValue::ValueKind enum");
}

static QualType getLValueType(ASTContext &Ctx, const APValue &LV) {
  QualType T = LV.getLValueBase().getType();
  for (APValue::LValuePathEntry E : LV.getLValuePath()) {
    if (const ArrayType *AT = Ctx.getAsArrayType(T))
      T = AT->getElementType();
    else if (const FieldDecl *FD =
                 dyn_cast<FieldDecl>(E.getAsBaseOrMember().getPointer()))
      T = FD->getType();
    else
      T = Ctx.getRecordType(
          cast<CXXRecordDecl>(E.getAsBaseOrMember().getPointer()));
  }
  return T;
}

static IdentifierInfo *getUnionInitName(SourceLocation UnionLoc,
                                        DiagnosticsEngine &Diags,
                                        const FieldDecl *FD) {
  // According to:
  // http://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling.anonymous
  // For the purposes of mangling, the name of an anonymous union is considered
  // to be the name of the first named data member found by a pre-order,
  // depth-first, declaration-order walk of the data members of the anonymous
  // union.

  if (FD->getIdentifier())
    return FD->getIdentifier();

  // The only cases where the identifer of a FieldDecl would be blank is if the
  // field represents an anonymous record type or if it is an unnamed bitfield.
  // There is no type to descend into in the case of a bitfield, so we can just
  // return nullptr in that case.
  if (FD->isBitField())
    return nullptr;
  const CXXRecordDecl *RD = FD->getType()->getAsCXXRecordDecl();

  // Consider only the fields in declaration order, searched depth-first.  We
  // don't care about the active member of the union, as all we are doing is
  // looking for a valid name. We also don't check bases, due to guidance from
  // the Itanium ABI folks.
  for (const FieldDecl *RDField : RD->fields()) {
    if (IdentifierInfo *II = getUnionInitName(UnionLoc, Diags, RDField))
      return II;
  }

  // According to the Itanium ABI: If there is no such data member (i.e., if all
  // of the data members in the union are unnamed), then there is no way for a
  // program to refer to the anonymous union, and there is therefore no need to
  // mangle its name. However, we should diagnose this anyway.
  unsigned DiagID = Diags.getCustomDiagID(
      DiagnosticsEngine::Error, "cannot mangle this unnamed union NTTP yet");
  Diags.Report(UnionLoc, DiagID);

  return nullptr;
}

void CXXNameMangler::mangleValueInTemplateArg(QualType T, const APValue &V,
                                              bool TopLevel,
                                              bool NeedExactType) {
  // Ignore all top-level cv-qualifiers, to match GCC.
  Qualifiers Quals;
  T = getASTContext().getUnqualifiedArrayType(T, Quals);

  // A top-level expression that's not a primary expression is wrapped in X...E.
  bool IsPrimaryExpr = true;
  auto NotPrimaryExpr = [&] {
    if (TopLevel && IsPrimaryExpr)
      Out << 'X';
    IsPrimaryExpr = false;
  };

  // Proposed in https://github.com/itanium-cxx-abi/cxx-abi/issues/63.
  switch (V.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
    Out << 'L';
    mangleType(T);
    Out << 'E';
    break;

  case APValue::AddrLabelDiff:
    llvm_unreachable("unexpected value kind in template argument");

  case APValue::Struct: {
    const CXXRecordDecl *RD = T->getAsCXXRecordDecl();
    assert(RD && "unexpected type for record value");

    // Drop trailing zero-initialized elements.
    llvm::SmallVector<const FieldDecl *, 16> Fields(RD->fields());
    while (
        !Fields.empty() &&
        (Fields.back()->isUnnamedBitField() ||
         isZeroInitialized(Fields.back()->getType(),
                           V.getStructField(Fields.back()->getFieldIndex())))) {
      Fields.pop_back();
    }
    llvm::ArrayRef<CXXBaseSpecifier> Bases(RD->bases_begin(), RD->bases_end());
    if (Fields.empty()) {
      while (!Bases.empty() &&
             isZeroInitialized(Bases.back().getType(),
                               V.getStructBase(Bases.size() - 1)))
        Bases = Bases.drop_back();
    }

    // <expression> ::= tl <type> <braced-expression>* E
    NotPrimaryExpr();
    Out << "tl";
    mangleType(T);
    for (unsigned I = 0, N = Bases.size(); I != N; ++I)
      mangleValueInTemplateArg(Bases[I].getType(), V.getStructBase(I), false);
    for (unsigned I = 0, N = Fields.size(); I != N; ++I) {
      if (Fields[I]->isUnnamedBitField())
        continue;
      mangleValueInTemplateArg(Fields[I]->getType(),
                               V.getStructField(Fields[I]->getFieldIndex()),
                               false);
    }
    Out << 'E';
    break;
  }

  case APValue::Union: {
    assert(T->getAsCXXRecordDecl() && "unexpected type for union value");
    const FieldDecl *FD = V.getUnionField();

    if (!FD) {
      Out << 'L';
      mangleType(T);
      Out << 'E';
      break;
    }

    // <braced-expression> ::= di <field source-name> <braced-expression>
    NotPrimaryExpr();
    Out << "tl";
    mangleType(T);
    if (!isZeroInitialized(T, V)) {
      Out << "di";
      IdentifierInfo *II = (getUnionInitName(
          T->getAsCXXRecordDecl()->getLocation(), Context.getDiags(), FD));
      if (II)
        mangleSourceName(II);
      mangleValueInTemplateArg(FD->getType(), V.getUnionValue(), false);
    }
    Out << 'E';
    break;
  }

  case APValue::Array: {
    QualType ElemT(T->getArrayElementTypeNoTypeQual(), 0);

    NotPrimaryExpr();
    Out << "tl";
    mangleType(T);

    // Drop trailing zero-initialized elements.
    unsigned N = V.getArraySize();
    if (!V.hasArrayFiller() || isZeroInitialized(ElemT, V.getArrayFiller())) {
      N = V.getArrayInitializedElts();
      while (N && isZeroInitialized(ElemT, V.getArrayInitializedElt(N - 1)))
        --N;
    }

    for (unsigned I = 0; I != N; ++I) {
      const APValue &Elem = I < V.getArrayInitializedElts()
                                ? V.getArrayInitializedElt(I)
                                : V.getArrayFiller();
      mangleValueInTemplateArg(ElemT, Elem, false);
    }
    Out << 'E';
    break;
  }

  case APValue::Vector: {
    const VectorType *VT = T->castAs<VectorType>();

    NotPrimaryExpr();
    Out << "tl";
    mangleType(T);
    unsigned N = V.getVectorLength();
    while (N && isZeroInitialized(VT->getElementType(), V.getVectorElt(N - 1)))
      --N;
    for (unsigned I = 0; I != N; ++I)
      mangleValueInTemplateArg(VT->getElementType(), V.getVectorElt(I), false);
    Out << 'E';
    break;
  }

  case APValue::Int:
    mangleIntegerLiteral(T, V.getInt());
    break;

  case APValue::Float:
    mangleFloatLiteral(T, V.getFloat());
    break;

  case APValue::FixedPoint:
    mangleFixedPointLiteral();
    break;

  case APValue::ComplexFloat: {
    const ComplexType *CT = T->castAs<ComplexType>();
    NotPrimaryExpr();
    Out << "tl";
    mangleType(T);
    if (!V.getComplexFloatReal().isPosZero() ||
        !V.getComplexFloatImag().isPosZero())
      mangleFloatLiteral(CT->getElementType(), V.getComplexFloatReal());
    if (!V.getComplexFloatImag().isPosZero())
      mangleFloatLiteral(CT->getElementType(), V.getComplexFloatImag());
    Out << 'E';
    break;
  }

  case APValue::ComplexInt: {
    const ComplexType *CT = T->castAs<ComplexType>();
    NotPrimaryExpr();
    Out << "tl";
    mangleType(T);
    if (V.getComplexIntReal().getBoolValue() ||
        V.getComplexIntImag().getBoolValue())
      mangleIntegerLiteral(CT->getElementType(), V.getComplexIntReal());
    if (V.getComplexIntImag().getBoolValue())
      mangleIntegerLiteral(CT->getElementType(), V.getComplexIntImag());
    Out << 'E';
    break;
  }

  case APValue::LValue: {
    // Proposed in https://github.com/itanium-cxx-abi/cxx-abi/issues/47.
    assert((T->isPointerType() || T->isReferenceType()) &&
           "unexpected type for LValue template arg");

    if (V.isNullPointer()) {
      mangleNullPointer(T);
      break;
    }

    APValue::LValueBase B = V.getLValueBase();
    if (!B) {
      // Non-standard mangling for integer cast to a pointer; this can only
      // occur as an extension.
      CharUnits Offset = V.getLValueOffset();
      if (Offset.isZero()) {
        // This is reinterpret_cast<T*>(0), not a null pointer. Mangle this as
        // a cast, because L <type> 0 E means something else.
        NotPrimaryExpr();
        Out << "rc";
        mangleType(T);
        Out << "Li0E";
        if (TopLevel)
          Out << 'E';
      } else {
        Out << "L";
        mangleType(T);
        Out << Offset.getQuantity() << 'E';
      }
      break;
    }

    ASTContext &Ctx = Context.getASTContext();

    enum { Base, Offset, Path } Kind;
    if (!V.hasLValuePath()) {
      // Mangle as (T*)((char*)&base + N).
      if (T->isReferenceType()) {
        NotPrimaryExpr();
        Out << "decvP";
        mangleType(T->getPointeeType());
      } else {
        NotPrimaryExpr();
        Out << "cv";
        mangleType(T);
      }
      Out << "plcvPcad";
      Kind = Offset;
    } else {
      // Clang 11 and before mangled an array subject to array-to-pointer decay
      // as if it were the declaration itself.
      bool IsArrayToPointerDecayMangledAsDecl = false;
      if (TopLevel && Ctx.getLangOpts().getClangABICompat() <=
                          LangOptions::ClangABI::Ver11) {
        QualType BType = B.getType();
        IsArrayToPointerDecayMangledAsDecl =
            BType->isArrayType() && V.getLValuePath().size() == 1 &&
            V.getLValuePath()[0].getAsArrayIndex() == 0 &&
            Ctx.hasSimilarType(T, Ctx.getDecayedType(BType));
      }

      if ((!V.getLValuePath().empty() || V.isLValueOnePastTheEnd()) &&
          !IsArrayToPointerDecayMangledAsDecl) {
        NotPrimaryExpr();
        // A final conversion to the template parameter's type is usually
        // folded into the 'so' mangling, but we can't do that for 'void*'
        // parameters without introducing collisions.
        if (NeedExactType && T->isVoidPointerType()) {
          Out << "cv";
          mangleType(T);
        }
        if (T->isPointerType())
          Out << "ad";
        Out << "so";
        mangleType(T->isVoidPointerType()
                       ? getLValueType(Ctx, V).getUnqualifiedType()
                       : T->getPointeeType());
        Kind = Path;
      } else {
        if (NeedExactType &&
            !Ctx.hasSameType(T->getPointeeType(), getLValueType(Ctx, V)) &&
            !isCompatibleWith(LangOptions::ClangABI::Ver11)) {
          NotPrimaryExpr();
          Out << "cv";
          mangleType(T);
        }
        if (T->isPointerType()) {
          NotPrimaryExpr();
          Out << "ad";
        }
        Kind = Base;
      }
    }

    QualType TypeSoFar = B.getType();
    if (auto *VD = B.dyn_cast<const ValueDecl*>()) {
      Out << 'L';
      mangle(VD);
      Out << 'E';
    } else if (auto *E = B.dyn_cast<const Expr*>()) {
      NotPrimaryExpr();
      mangleExpression(E);
    } else if (auto TI = B.dyn_cast<TypeInfoLValue>()) {
      NotPrimaryExpr();
      Out << "ti";
      mangleType(QualType(TI.getType(), 0));
    } else {
      // We should never see dynamic allocations here.
      llvm_unreachable("unexpected lvalue base kind in template argument");
    }

    switch (Kind) {
    case Base:
      break;

    case Offset:
      Out << 'L';
      mangleType(Ctx.getPointerDiffType());
      mangleNumber(V.getLValueOffset().getQuantity());
      Out << 'E';
      break;

    case Path:
      // <expression> ::= so <referent type> <expr> [<offset number>]
      //                  <union-selector>* [p] E
      if (!V.getLValueOffset().isZero())
        mangleNumber(V.getLValueOffset().getQuantity());

      // We model a past-the-end array pointer as array indexing with index N,
      // not with the "past the end" flag. Compensate for that.
      bool OnePastTheEnd = V.isLValueOnePastTheEnd();

      for (APValue::LValuePathEntry E : V.getLValuePath()) {
        if (auto *AT = TypeSoFar->getAsArrayTypeUnsafe()) {
          if (auto *CAT = dyn_cast<ConstantArrayType>(AT))
            OnePastTheEnd |= CAT->getSize() == E.getAsArrayIndex();
          TypeSoFar = AT->getElementType();
        } else {
          const Decl *D = E.getAsBaseOrMember().getPointer();
          if (auto *FD = dyn_cast<FieldDecl>(D)) {
            // <union-selector> ::= _ <number>
            if (FD->getParent()->isUnion()) {
              Out << '_';
              if (FD->getFieldIndex())
                Out << (FD->getFieldIndex() - 1);
            }
            TypeSoFar = FD->getType();
          } else {
            TypeSoFar = Ctx.getRecordType(cast<CXXRecordDecl>(D));
          }
        }
      }

      if (OnePastTheEnd)
        Out << 'p';
      Out << 'E';
      break;
    }

    break;
  }

  case APValue::MemberPointer:
    // Proposed in https://github.com/itanium-cxx-abi/cxx-abi/issues/47.
    if (!V.getMemberPointerDecl()) {
      mangleNullPointer(T);
      break;
    }

    ASTContext &Ctx = Context.getASTContext();

    NotPrimaryExpr();
    if (!V.getMemberPointerPath().empty()) {
      Out << "mc";
      mangleType(T);
    } else if (NeedExactType &&
               !Ctx.hasSameType(
                   T->castAs<MemberPointerType>()->getPointeeType(),
                   V.getMemberPointerDecl()->getType()) &&
               !isCompatibleWith(LangOptions::ClangABI::Ver11)) {
      Out << "cv";
      mangleType(T);
    }
    Out << "adL";
    mangle(V.getMemberPointerDecl());
    Out << 'E';
    if (!V.getMemberPointerPath().empty()) {
      CharUnits Offset =
          Context.getASTContext().getMemberPointerPathAdjustment(V);
      if (!Offset.isZero())
        mangleNumber(Offset.getQuantity());
      Out << 'E';
    }
    break;
  }

  if (TopLevel && !IsPrimaryExpr)
    Out << 'E';
}

void CXXNameMangler::mangleTemplateParameter(unsigned Depth, unsigned Index) {
  // <template-param> ::= T_    # first template parameter
  //                  ::= T <parameter-2 non-negative number> _
  //                  ::= TL <L-1 non-negative number> __
  //                  ::= TL <L-1 non-negative number> _
  //                         <parameter-2 non-negative number> _
  //
  // The latter two manglings are from a proposal here:
  // https://github.com/itanium-cxx-abi/cxx-abi/issues/31#issuecomment-528122117
  Out << 'T';
  Depth += TemplateDepthOffset;
  if (Depth != 0)
    Out << 'L' << (Depth - 1) << '_';
  if (Index != 0)
    Out << (Index - 1);
  Out << '_';
}

void CXXNameMangler::mangleSeqID(unsigned SeqID) {
  if (SeqID == 0) {
    // Nothing.
  } else if (SeqID == 1) {
    Out << '0';
  } else {
    SeqID--;

    // <seq-id> is encoded in base-36, using digits and upper case letters.
    char Buffer[7]; // log(2**32) / log(36) ~= 7
    MutableArrayRef<char> BufferRef(Buffer);
    MutableArrayRef<char>::reverse_iterator I = BufferRef.rbegin();

    for (; SeqID != 0; SeqID /= 36) {
      unsigned C = SeqID % 36;
      *I++ = (C < 10 ? '0' + C : 'A' + C - 10);
    }

    Out.write(I.base(), I - BufferRef.rbegin());
  }
  Out << '_';
}

void CXXNameMangler::mangleExistingSubstitution(TemplateName tname) {
  bool result = mangleSubstitution(tname);
  assert(result && "no existing substitution for template name");
  (void) result;
}

// <substitution> ::= S <seq-id> _
//                ::= S_
bool CXXNameMangler::mangleSubstitution(const NamedDecl *ND) {
  // Try one of the standard substitutions first.
  if (mangleStandardSubstitution(ND))
    return true;

  ND = cast<NamedDecl>(ND->getCanonicalDecl());
  return mangleSubstitution(reinterpret_cast<uintptr_t>(ND));
}

bool CXXNameMangler::mangleSubstitution(NestedNameSpecifier *NNS) {
  assert(NNS->getKind() == NestedNameSpecifier::Identifier &&
         "mangleSubstitution(NestedNameSpecifier *) is only used for "
         "identifier nested name specifiers.");
  NNS = Context.getASTContext().getCanonicalNestedNameSpecifier(NNS);
  return mangleSubstitution(reinterpret_cast<uintptr_t>(NNS));
}

/// Determine whether the given type has any qualifiers that are relevant for
/// substitutions.
static bool hasMangledSubstitutionQualifiers(QualType T) {
  Qualifiers Qs = T.getQualifiers();
  return Qs.getCVRQualifiers() || Qs.hasAddressSpace() || Qs.hasUnaligned();
}

bool CXXNameMangler::mangleSubstitution(QualType T) {
  if (!hasMangledSubstitutionQualifiers(T)) {
    if (const RecordType *RT = T->getAs<RecordType>())
      return mangleSubstitution(RT->getDecl());
  }

  uintptr_t TypePtr = reinterpret_cast<uintptr_t>(T.getAsOpaquePtr());

  return mangleSubstitution(TypePtr);
}

bool CXXNameMangler::mangleSubstitution(TemplateName Template) {
  if (TemplateDecl *TD = Template.getAsTemplateDecl())
    return mangleSubstitution(TD);

  Template = Context.getASTContext().getCanonicalTemplateName(Template);
  return mangleSubstitution(
                      reinterpret_cast<uintptr_t>(Template.getAsVoidPointer()));
}

bool CXXNameMangler::mangleSubstitution(uintptr_t Ptr) {
  llvm::DenseMap<uintptr_t, unsigned>::iterator I = Substitutions.find(Ptr);
  if (I == Substitutions.end())
    return false;

  unsigned SeqID = I->second;
  Out << 'S';
  mangleSeqID(SeqID);

  return true;
}

/// Returns whether S is a template specialization of std::Name with a single
/// argument of type A.
bool CXXNameMangler::isSpecializedAs(QualType S, llvm::StringRef Name,
                                     QualType A) {
  if (S.isNull())
    return false;

  const RecordType *RT = S->getAs<RecordType>();
  if (!RT)
    return false;

  const ClassTemplateSpecializationDecl *SD =
    dyn_cast<ClassTemplateSpecializationDecl>(RT->getDecl());
  if (!SD || !SD->getIdentifier()->isStr(Name))
    return false;

  if (!isStdNamespace(Context.getEffectiveDeclContext(SD)))
    return false;

  const TemplateArgumentList &TemplateArgs = SD->getTemplateArgs();
  if (TemplateArgs.size() != 1)
    return false;

  if (TemplateArgs[0].getAsType() != A)
    return false;

  if (SD->getSpecializedTemplate()->getOwningModuleForLinkage())
    return false;

  return true;
}

/// Returns whether SD is a template specialization std::Name<char,
/// std::char_traits<char> [, std::allocator<char>]>
/// HasAllocator controls whether the 3rd template argument is needed.
bool CXXNameMangler::isStdCharSpecialization(
    const ClassTemplateSpecializationDecl *SD, llvm::StringRef Name,
    bool HasAllocator) {
  if (!SD->getIdentifier()->isStr(Name))
    return false;

  const TemplateArgumentList &TemplateArgs = SD->getTemplateArgs();
  if (TemplateArgs.size() != (HasAllocator ? 3 : 2))
    return false;

  QualType A = TemplateArgs[0].getAsType();
  if (A.isNull())
    return false;
  // Plain 'char' is named Char_S or Char_U depending on the target ABI.
  if (!A->isSpecificBuiltinType(BuiltinType::Char_S) &&
      !A->isSpecificBuiltinType(BuiltinType::Char_U))
    return false;

  if (!isSpecializedAs(TemplateArgs[1].getAsType(), "char_traits", A))
    return false;

  if (HasAllocator &&
      !isSpecializedAs(TemplateArgs[2].getAsType(), "allocator", A))
    return false;

  if (SD->getSpecializedTemplate()->getOwningModuleForLinkage())
    return false;

  return true;
}

bool CXXNameMangler::mangleStandardSubstitution(const NamedDecl *ND) {
  // <substitution> ::= St # ::std::
  if (const NamespaceDecl *NS = dyn_cast<NamespaceDecl>(ND)) {
    if (isStd(NS)) {
      Out << "St";
      return true;
    }
    return false;
  }

  if (const ClassTemplateDecl *TD = dyn_cast<ClassTemplateDecl>(ND)) {
    if (!isStdNamespace(Context.getEffectiveDeclContext(TD)))
      return false;

    if (TD->getOwningModuleForLinkage())
      return false;

    // <substitution> ::= Sa # ::std::allocator
    if (TD->getIdentifier()->isStr("allocator")) {
      Out << "Sa";
      return true;
    }

    // <<substitution> ::= Sb # ::std::basic_string
    if (TD->getIdentifier()->isStr("basic_string")) {
      Out << "Sb";
      return true;
    }
    return false;
  }

  if (const ClassTemplateSpecializationDecl *SD =
        dyn_cast<ClassTemplateSpecializationDecl>(ND)) {
    if (!isStdNamespace(Context.getEffectiveDeclContext(SD)))
      return false;

    if (SD->getSpecializedTemplate()->getOwningModuleForLinkage())
      return false;

    //    <substitution> ::= Ss # ::std::basic_string<char,
    //                            ::std::char_traits<char>,
    //                            ::std::allocator<char> >
    if (isStdCharSpecialization(SD, "basic_string", /*HasAllocator=*/true)) {
      Out << "Ss";
      return true;
    }

    //    <substitution> ::= Si # ::std::basic_istream<char,
    //                            ::std::char_traits<char> >
    if (isStdCharSpecialization(SD, "basic_istream", /*HasAllocator=*/false)) {
      Out << "Si";
      return true;
    }

    //    <substitution> ::= So # ::std::basic_ostream<char,
    //                            ::std::char_traits<char> >
    if (isStdCharSpecialization(SD, "basic_ostream", /*HasAllocator=*/false)) {
      Out << "So";
      return true;
    }

    //    <substitution> ::= Sd # ::std::basic_iostream<char,
    //                            ::std::char_traits<char> >
    if (isStdCharSpecialization(SD, "basic_iostream", /*HasAllocator=*/false)) {
      Out << "Sd";
      return true;
    }
    return false;
  }

  return false;
}

void CXXNameMangler::addSubstitution(QualType T) {
  if (!hasMangledSubstitutionQualifiers(T)) {
    if (const RecordType *RT = T->getAs<RecordType>()) {
      addSubstitution(RT->getDecl());
      return;
    }
  }

  uintptr_t TypePtr = reinterpret_cast<uintptr_t>(T.getAsOpaquePtr());
  addSubstitution(TypePtr);
}

void CXXNameMangler::addSubstitution(TemplateName Template) {
  if (TemplateDecl *TD = Template.getAsTemplateDecl())
    return addSubstitution(TD);

  Template = Context.getASTContext().getCanonicalTemplateName(Template);
  addSubstitution(reinterpret_cast<uintptr_t>(Template.getAsVoidPointer()));
}

void CXXNameMangler::addSubstitution(uintptr_t Ptr) {
  assert(!Substitutions.count(Ptr) && "Substitution already exists!");
  Substitutions[Ptr] = SeqID++;
}

void CXXNameMangler::extendSubstitutions(CXXNameMangler* Other) {
  assert(Other->SeqID >= SeqID && "Must be superset of substitutions!");
  if (Other->SeqID > SeqID) {
    Substitutions.swap(Other->Substitutions);
    SeqID = Other->SeqID;
  }
}

CXXNameMangler::AbiTagList
CXXNameMangler::makeFunctionReturnTypeTags(const FunctionDecl *FD) {
  // When derived abi tags are disabled there is no need to make any list.
  if (DisableDerivedAbiTags)
    return AbiTagList();

  llvm::raw_null_ostream NullOutStream;
  CXXNameMangler TrackReturnTypeTags(*this, NullOutStream);
  TrackReturnTypeTags.disableDerivedAbiTags();

  const FunctionProtoType *Proto =
      cast<FunctionProtoType>(FD->getType()->getAs<FunctionType>());
  FunctionTypeDepthState saved = TrackReturnTypeTags.FunctionTypeDepth.push();
  TrackReturnTypeTags.FunctionTypeDepth.enterResultType();
  TrackReturnTypeTags.mangleType(Proto->getReturnType());
  TrackReturnTypeTags.FunctionTypeDepth.leaveResultType();
  TrackReturnTypeTags.FunctionTypeDepth.pop(saved);

  return TrackReturnTypeTags.AbiTagsRoot.getSortedUniqueUsedAbiTags();
}

CXXNameMangler::AbiTagList
CXXNameMangler::makeVariableTypeTags(const VarDecl *VD) {
  // When derived abi tags are disabled there is no need to make any list.
  if (DisableDerivedAbiTags)
    return AbiTagList();

  llvm::raw_null_ostream NullOutStream;
  CXXNameMangler TrackVariableType(*this, NullOutStream);
  TrackVariableType.disableDerivedAbiTags();

  TrackVariableType.mangleType(VD->getType());

  return TrackVariableType.AbiTagsRoot.getSortedUniqueUsedAbiTags();
}

bool CXXNameMangler::shouldHaveAbiTags(ItaniumMangleContextImpl &C,
                                       const VarDecl *VD) {
  llvm::raw_null_ostream NullOutStream;
  CXXNameMangler TrackAbiTags(C, NullOutStream, nullptr, true);
  TrackAbiTags.mangle(VD);
  return TrackAbiTags.AbiTagsRoot.getUsedAbiTags().size();
}

//

/// Mangles the name of the declaration D and emits that name to the given
/// output stream.
///
/// If the declaration D requires a mangled name, this routine will emit that
/// mangled name to \p os and return true. Otherwise, \p os will be unchanged
/// and this routine will return false. In this case, the caller should just
/// emit the identifier of the declaration (\c D->getIdentifier()) as its
/// name.
void ItaniumMangleContextImpl::mangleCXXName(GlobalDecl GD,
                                             raw_ostream &Out) {
  const NamedDecl *D = cast<NamedDecl>(GD.getDecl());
  assert((isa<FunctionDecl, VarDecl, TemplateParamObjectDecl>(D)) &&
         "Invalid mangleName() call, argument is not a variable or function!");

  PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                 getASTContext().getSourceManager(),
                                 "Mangling declaration");

  if (auto *CD = dyn_cast<CXXConstructorDecl>(D)) {
    auto Type = GD.getCtorType();
    CXXNameMangler Mangler(*this, Out, CD, Type);
    return Mangler.mangle(GlobalDecl(CD, Type));
  }

  if (auto *DD = dyn_cast<CXXDestructorDecl>(D)) {
    auto Type = GD.getDtorType();
    CXXNameMangler Mangler(*this, Out, DD, Type);
    return Mangler.mangle(GlobalDecl(DD, Type));
  }

  CXXNameMangler Mangler(*this, Out, D);
  Mangler.mangle(GD);
}

void ItaniumMangleContextImpl::mangleCXXCtorComdat(const CXXConstructorDecl *D,
                                                   raw_ostream &Out) {
  CXXNameMangler Mangler(*this, Out, D, Ctor_Comdat);
  Mangler.mangle(GlobalDecl(D, Ctor_Comdat));
}

void ItaniumMangleContextImpl::mangleCXXDtorComdat(const CXXDestructorDecl *D,
                                                   raw_ostream &Out) {
  CXXNameMangler Mangler(*this, Out, D, Dtor_Comdat);
  Mangler.mangle(GlobalDecl(D, Dtor_Comdat));
}

/// Mangles the pointer authentication override attribute for classes
/// that have explicit overrides for the vtable authentication schema.
///
/// The override is mangled as a parameterized vendor extension as follows
///
///   <type> ::= U "__vtptrauth" I
///                 <key>
///                 <addressDiscriminated>
///                 <extraDiscriminator>
///              E
///
/// The extra discriminator encodes the explicit value derived from the
/// override schema, e.g. if the override has specified type based
/// discrimination the encoded value will be the discriminator derived from the
/// type name.
static void mangleOverrideDiscrimination(CXXNameMangler &Mangler,
                                         ASTContext &Context,
                                         const ThunkInfo &Thunk) {
  auto &LangOpts = Context.getLangOpts();
  const CXXRecordDecl *ThisRD = Thunk.ThisType->getPointeeCXXRecordDecl();
  const CXXRecordDecl *PtrauthClassRD =
      Context.baseForVTableAuthentication(ThisRD);
  unsigned TypedDiscriminator =
      Context.getPointerAuthVTablePointerDiscriminator(ThisRD);
  Mangler.mangleVendorQualifier("__vtptrauth");
  auto &ManglerStream = Mangler.getStream();
  ManglerStream << "I";
  if (const auto *ExplicitAuth =
          PtrauthClassRD->getAttr<VTablePointerAuthenticationAttr>()) {
    ManglerStream << "Lj" << ExplicitAuth->getKey();

    if (ExplicitAuth->getAddressDiscrimination() ==
        VTablePointerAuthenticationAttr::DefaultAddressDiscrimination)
      ManglerStream << "Lb" << LangOpts.PointerAuthVTPtrAddressDiscrimination;
    else
      ManglerStream << "Lb"
                    << (ExplicitAuth->getAddressDiscrimination() ==
                        VTablePointerAuthenticationAttr::AddressDiscrimination);

    switch (ExplicitAuth->getExtraDiscrimination()) {
    case VTablePointerAuthenticationAttr::DefaultExtraDiscrimination: {
      if (LangOpts.PointerAuthVTPtrTypeDiscrimination)
        ManglerStream << "Lj" << TypedDiscriminator;
      else
        ManglerStream << "Lj" << 0;
      break;
    }
    case VTablePointerAuthenticationAttr::TypeDiscrimination:
      ManglerStream << "Lj" << TypedDiscriminator;
      break;
    case VTablePointerAuthenticationAttr::CustomDiscrimination:
      ManglerStream << "Lj" << ExplicitAuth->getCustomDiscriminationValue();
      break;
    case VTablePointerAuthenticationAttr::NoExtraDiscrimination:
      ManglerStream << "Lj" << 0;
      break;
    }
  } else {
    ManglerStream << "Lj"
                  << (unsigned)VTablePointerAuthenticationAttr::DefaultKey;
    ManglerStream << "Lb" << LangOpts.PointerAuthVTPtrAddressDiscrimination;
    if (LangOpts.PointerAuthVTPtrTypeDiscrimination)
      ManglerStream << "Lj" << TypedDiscriminator;
    else
      ManglerStream << "Lj" << 0;
  }
  ManglerStream << "E";
}

void ItaniumMangleContextImpl::mangleThunk(const CXXMethodDecl *MD,
                                           const ThunkInfo &Thunk,
                                           bool ElideOverrideInfo,
                                           raw_ostream &Out) {
  //  <special-name> ::= T <call-offset> <base encoding>
  //                      # base is the nominal target function of thunk
  //  <special-name> ::= Tc <call-offset> <call-offset> <base encoding>
  //                      # base is the nominal target function of thunk
  //                      # first call-offset is 'this' adjustment
  //                      # second call-offset is result adjustment

  assert(!isa<CXXDestructorDecl>(MD) &&
         "Use mangleCXXDtor for destructor decls!");
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZT";
  if (!Thunk.Return.isEmpty())
    Mangler.getStream() << 'c';

  // Mangle the 'this' pointer adjustment.
  Mangler.mangleCallOffset(Thunk.This.NonVirtual,
                           Thunk.This.Virtual.Itanium.VCallOffsetOffset);

  // Mangle the return pointer adjustment if there is one.
  if (!Thunk.Return.isEmpty())
    Mangler.mangleCallOffset(Thunk.Return.NonVirtual,
                             Thunk.Return.Virtual.Itanium.VBaseOffsetOffset);

  Mangler.mangleFunctionEncoding(MD);
  if (!ElideOverrideInfo)
    mangleOverrideDiscrimination(Mangler, getASTContext(), Thunk);
}

void ItaniumMangleContextImpl::mangleCXXDtorThunk(const CXXDestructorDecl *DD,
                                                  CXXDtorType Type,
                                                  const ThunkInfo &Thunk,
                                                  bool ElideOverrideInfo,
                                                  raw_ostream &Out) {
  //  <special-name> ::= T <call-offset> <base encoding>
  //                      # base is the nominal target function of thunk
  CXXNameMangler Mangler(*this, Out, DD, Type);
  Mangler.getStream() << "_ZT";

  auto &ThisAdjustment = Thunk.This;
  // Mangle the 'this' pointer adjustment.
  Mangler.mangleCallOffset(ThisAdjustment.NonVirtual,
                           ThisAdjustment.Virtual.Itanium.VCallOffsetOffset);

  Mangler.mangleFunctionEncoding(GlobalDecl(DD, Type));
  if (!ElideOverrideInfo)
    mangleOverrideDiscrimination(Mangler, getASTContext(), Thunk);
}

/// Returns the mangled name for a guard variable for the passed in VarDecl.
void ItaniumMangleContextImpl::mangleStaticGuardVariable(const VarDecl *D,
                                                         raw_ostream &Out) {
  //  <special-name> ::= GV <object name>       # Guard variable for one-time
  //                                            # initialization
  CXXNameMangler Mangler(*this, Out);
  // GCC 5.3.0 doesn't emit derived ABI tags for local names but that seems to
  // be a bug that is fixed in trunk.
  Mangler.getStream() << "_ZGV";
  Mangler.mangleName(D);
}

void ItaniumMangleContextImpl::mangleDynamicInitializer(const VarDecl *MD,
                                                        raw_ostream &Out) {
  // These symbols are internal in the Itanium ABI, so the names don't matter.
  // Clang has traditionally used this symbol and allowed LLVM to adjust it to
  // avoid duplicate symbols.
  Out << "__cxx_global_var_init";
}

void ItaniumMangleContextImpl::mangleDynamicAtExitDestructor(const VarDecl *D,
                                                             raw_ostream &Out) {
  // Prefix the mangling of D with __dtor_.
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "__dtor_";
  if (shouldMangleDeclName(D))
    Mangler.mangle(D);
  else
    Mangler.getStream() << D->getName();
}

void ItaniumMangleContextImpl::mangleDynamicStermFinalizer(const VarDecl *D,
                                                           raw_ostream &Out) {
  // Clang generates these internal-linkage functions as part of its
  // implementation of the XL ABI.
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "__finalize_";
  if (shouldMangleDeclName(D))
    Mangler.mangle(D);
  else
    Mangler.getStream() << D->getName();
}

void ItaniumMangleContextImpl::mangleSEHFilterExpression(
    GlobalDecl EnclosingDecl, raw_ostream &Out) {
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "__filt_";
  auto *EnclosingFD = cast<FunctionDecl>(EnclosingDecl.getDecl());
  if (shouldMangleDeclName(EnclosingFD))
    Mangler.mangle(EnclosingDecl);
  else
    Mangler.getStream() << EnclosingFD->getName();
}

void ItaniumMangleContextImpl::mangleSEHFinallyBlock(
    GlobalDecl EnclosingDecl, raw_ostream &Out) {
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "__fin_";
  auto *EnclosingFD = cast<FunctionDecl>(EnclosingDecl.getDecl());
  if (shouldMangleDeclName(EnclosingFD))
    Mangler.mangle(EnclosingDecl);
  else
    Mangler.getStream() << EnclosingFD->getName();
}

void ItaniumMangleContextImpl::mangleItaniumThreadLocalInit(const VarDecl *D,
                                                            raw_ostream &Out) {
  //  <special-name> ::= TH <object name>
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZTH";
  Mangler.mangleName(D);
}

void
ItaniumMangleContextImpl::mangleItaniumThreadLocalWrapper(const VarDecl *D,
                                                          raw_ostream &Out) {
  //  <special-name> ::= TW <object name>
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZTW";
  Mangler.mangleName(D);
}

void ItaniumMangleContextImpl::mangleReferenceTemporary(const VarDecl *D,
                                                        unsigned ManglingNumber,
                                                        raw_ostream &Out) {
  // We match the GCC mangling here.
  //  <special-name> ::= GR <object name>
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZGR";
  Mangler.mangleName(D);
  assert(ManglingNumber > 0 && "Reference temporary mangling number is zero!");
  Mangler.mangleSeqID(ManglingNumber - 1);
}

void ItaniumMangleContextImpl::mangleCXXVTable(const CXXRecordDecl *RD,
                                               raw_ostream &Out) {
  // <special-name> ::= TV <type>  # virtual table
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZTV";
  Mangler.mangleNameOrStandardSubstitution(RD);
}

void ItaniumMangleContextImpl::mangleCXXVTT(const CXXRecordDecl *RD,
                                            raw_ostream &Out) {
  // <special-name> ::= TT <type>  # VTT structure
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZTT";
  Mangler.mangleNameOrStandardSubstitution(RD);
}

void ItaniumMangleContextImpl::mangleCXXCtorVTable(const CXXRecordDecl *RD,
                                                   int64_t Offset,
                                                   const CXXRecordDecl *Type,
                                                   raw_ostream &Out) {
  // <special-name> ::= TC <type> <offset number> _ <base type>
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZTC";
  Mangler.mangleNameOrStandardSubstitution(RD);
  Mangler.getStream() << Offset;
  Mangler.getStream() << '_';
  Mangler.mangleNameOrStandardSubstitution(Type);
}

void ItaniumMangleContextImpl::mangleCXXRTTI(QualType Ty, raw_ostream &Out) {
  // <special-name> ::= TI <type>  # typeinfo structure
  assert(!Ty.hasQualifiers() && "RTTI info cannot have top-level qualifiers");
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZTI";
  Mangler.mangleType(Ty);
}

void ItaniumMangleContextImpl::mangleCXXRTTIName(
    QualType Ty, raw_ostream &Out, bool NormalizeIntegers = false) {
  // <special-name> ::= TS <type>  # typeinfo name (null terminated byte string)
  CXXNameMangler Mangler(*this, Out, NormalizeIntegers);
  Mangler.getStream() << "_ZTS";
  Mangler.mangleType(Ty);
}

void ItaniumMangleContextImpl::mangleCanonicalTypeName(
    QualType Ty, raw_ostream &Out, bool NormalizeIntegers = false) {
  mangleCXXRTTIName(Ty, Out, NormalizeIntegers);
}

void ItaniumMangleContextImpl::mangleStringLiteral(const StringLiteral *, raw_ostream &) {
  llvm_unreachable("Can't mangle string literals");
}

void ItaniumMangleContextImpl::mangleLambdaSig(const CXXRecordDecl *Lambda,
                                               raw_ostream &Out) {
  CXXNameMangler Mangler(*this, Out);
  Mangler.mangleLambdaSig(Lambda);
}

void ItaniumMangleContextImpl::mangleModuleInitializer(const Module *M,
                                                       raw_ostream &Out) {
  // <special-name> ::= GI <module-name>  # module initializer function
  CXXNameMangler Mangler(*this, Out);
  Mangler.getStream() << "_ZGI";
  Mangler.mangleModuleNamePrefix(M->getPrimaryModuleInterfaceName());
  if (M->isModulePartition()) {
    // The partition needs including, as partitions can have them too.
    auto Partition = M->Name.find(':');
    Mangler.mangleModuleNamePrefix(
        StringRef(&M->Name[Partition + 1], M->Name.size() - Partition - 1),
        /*IsPartition*/ true);
  }
}

ItaniumMangleContext *ItaniumMangleContext::create(ASTContext &Context,
                                                   DiagnosticsEngine &Diags,
                                                   bool IsAux) {
  return new ItaniumMangleContextImpl(
      Context, Diags,
      [](ASTContext &, const NamedDecl *) -> std::optional<unsigned> {
        return std::nullopt;
      },
      IsAux);
}

ItaniumMangleContext *
ItaniumMangleContext::create(ASTContext &Context, DiagnosticsEngine &Diags,
                             DiscriminatorOverrideTy DiscriminatorOverride,
                             bool IsAux) {
  return new ItaniumMangleContextImpl(Context, Diags, DiscriminatorOverride,
                                      IsAux);
}
