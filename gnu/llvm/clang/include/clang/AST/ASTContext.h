//===- ASTContext.h - Context to hold long-lived AST nodes ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::ASTContext interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTCONTEXT_H
#define LLVM_CLANG_AST_ASTCONTEXT_H

#include "clang/AST/ASTFwd.h"
#include "clang/AST/CanonicalType.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/TemplateName.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

namespace llvm {

class APFixedPoint;
class FixedPointSemantics;
struct fltSemantics;
template <typename T, unsigned N> class SmallPtrSet;

} // namespace llvm

namespace clang {

class APValue;
class ASTMutationListener;
class ASTRecordLayout;
class AtomicExpr;
class BlockExpr;
struct BlockVarCopyInit;
class BuiltinTemplateDecl;
class CharUnits;
class ConceptDecl;
class CXXABI;
class CXXConstructorDecl;
class CXXMethodDecl;
class CXXRecordDecl;
class DiagnosticsEngine;
class DynTypedNodeList;
class Expr;
enum class FloatModeKind;
class GlobalDecl;
class IdentifierTable;
class LangOptions;
class MangleContext;
class MangleNumberingContext;
class MemberSpecializationInfo;
class Module;
struct MSGuidDeclParts;
class NestedNameSpecifier;
class NoSanitizeList;
class ObjCCategoryDecl;
class ObjCCategoryImplDecl;
class ObjCContainerDecl;
class ObjCImplDecl;
class ObjCImplementationDecl;
class ObjCInterfaceDecl;
class ObjCIvarDecl;
class ObjCMethodDecl;
class ObjCPropertyDecl;
class ObjCPropertyImplDecl;
class ObjCProtocolDecl;
class ObjCTypeParamDecl;
class OMPTraitInfo;
class ParentMapContext;
struct ParsedTargetAttr;
class Preprocessor;
class ProfileList;
class StoredDeclsMap;
class TargetAttr;
class TargetInfo;
class TemplateDecl;
class TemplateParameterList;
class TemplateTemplateParmDecl;
class TemplateTypeParmDecl;
class TypeConstraint;
class UnresolvedSetIterator;
class UsingShadowDecl;
class VarTemplateDecl;
class VTableContextBase;
class XRayFunctionFilter;

/// A simple array of base specifiers.
typedef SmallVector<CXXBaseSpecifier *, 4> CXXCastPath;

namespace Builtin {

class Context;

} // namespace Builtin

enum BuiltinTemplateKind : int;
enum OpenCLTypeKind : uint8_t;

namespace comments {

class FullComment;

} // namespace comments

namespace interp {

class Context;

} // namespace interp

namespace serialization {
template <class> class AbstractTypeReader;
} // namespace serialization

enum class AlignRequirementKind {
  /// The alignment was not explicit in code.
  None,

  /// The alignment comes from an alignment attribute on a typedef.
  RequiredByTypedef,

  /// The alignment comes from an alignment attribute on a record type.
  RequiredByRecord,

  /// The alignment comes from an alignment attribute on a enum type.
  RequiredByEnum,
};

struct TypeInfo {
  uint64_t Width = 0;
  unsigned Align = 0;
  AlignRequirementKind AlignRequirement;

  TypeInfo() : AlignRequirement(AlignRequirementKind::None) {}
  TypeInfo(uint64_t Width, unsigned Align,
           AlignRequirementKind AlignRequirement)
      : Width(Width), Align(Align), AlignRequirement(AlignRequirement) {}
  bool isAlignRequired() {
    return AlignRequirement != AlignRequirementKind::None;
  }
};

struct TypeInfoChars {
  CharUnits Width;
  CharUnits Align;
  AlignRequirementKind AlignRequirement;

  TypeInfoChars() : AlignRequirement(AlignRequirementKind::None) {}
  TypeInfoChars(CharUnits Width, CharUnits Align,
                AlignRequirementKind AlignRequirement)
      : Width(Width), Align(Align), AlignRequirement(AlignRequirement) {}
  bool isAlignRequired() {
    return AlignRequirement != AlignRequirementKind::None;
  }
};

/// Holds long-lived AST nodes (such as types and decls) that can be
/// referred to throughout the semantic analysis of a file.
class ASTContext : public RefCountedBase<ASTContext> {
  friend class NestedNameSpecifier;

  mutable SmallVector<Type *, 0> Types;
  mutable llvm::FoldingSet<ExtQuals> ExtQualNodes;
  mutable llvm::FoldingSet<ComplexType> ComplexTypes;
  mutable llvm::FoldingSet<PointerType> PointerTypes{GeneralTypesLog2InitSize};
  mutable llvm::FoldingSet<AdjustedType> AdjustedTypes;
  mutable llvm::FoldingSet<BlockPointerType> BlockPointerTypes;
  mutable llvm::FoldingSet<LValueReferenceType> LValueReferenceTypes;
  mutable llvm::FoldingSet<RValueReferenceType> RValueReferenceTypes;
  mutable llvm::FoldingSet<MemberPointerType> MemberPointerTypes;
  mutable llvm::ContextualFoldingSet<ConstantArrayType, ASTContext &>
      ConstantArrayTypes;
  mutable llvm::FoldingSet<IncompleteArrayType> IncompleteArrayTypes;
  mutable std::vector<VariableArrayType*> VariableArrayTypes;
  mutable llvm::ContextualFoldingSet<DependentSizedArrayType, ASTContext &>
      DependentSizedArrayTypes;
  mutable llvm::ContextualFoldingSet<DependentSizedExtVectorType, ASTContext &>
      DependentSizedExtVectorTypes;
  mutable llvm::ContextualFoldingSet<DependentAddressSpaceType, ASTContext &>
      DependentAddressSpaceTypes;
  mutable llvm::FoldingSet<VectorType> VectorTypes;
  mutable llvm::ContextualFoldingSet<DependentVectorType, ASTContext &>
      DependentVectorTypes;
  mutable llvm::FoldingSet<ConstantMatrixType> MatrixTypes;
  mutable llvm::ContextualFoldingSet<DependentSizedMatrixType, ASTContext &>
      DependentSizedMatrixTypes;
  mutable llvm::FoldingSet<FunctionNoProtoType> FunctionNoProtoTypes;
  mutable llvm::ContextualFoldingSet<FunctionProtoType, ASTContext&>
    FunctionProtoTypes;
  mutable llvm::ContextualFoldingSet<DependentTypeOfExprType, ASTContext &>
      DependentTypeOfExprTypes;
  mutable llvm::ContextualFoldingSet<DependentDecltypeType, ASTContext &>
      DependentDecltypeTypes;

  mutable llvm::FoldingSet<PackIndexingType> DependentPackIndexingTypes;

  mutable llvm::FoldingSet<TemplateTypeParmType> TemplateTypeParmTypes;
  mutable llvm::FoldingSet<ObjCTypeParamType> ObjCTypeParamTypes;
  mutable llvm::FoldingSet<SubstTemplateTypeParmType>
    SubstTemplateTypeParmTypes;
  mutable llvm::FoldingSet<SubstTemplateTypeParmPackType>
    SubstTemplateTypeParmPackTypes;
  mutable llvm::ContextualFoldingSet<TemplateSpecializationType, ASTContext&>
    TemplateSpecializationTypes;
  mutable llvm::FoldingSet<ParenType> ParenTypes{GeneralTypesLog2InitSize};
  mutable llvm::FoldingSet<UsingType> UsingTypes;
  mutable llvm::FoldingSet<TypedefType> TypedefTypes;
  mutable llvm::FoldingSet<ElaboratedType> ElaboratedTypes{
      GeneralTypesLog2InitSize};
  mutable llvm::FoldingSet<DependentNameType> DependentNameTypes;
  mutable llvm::ContextualFoldingSet<DependentTemplateSpecializationType,
                                     ASTContext&>
    DependentTemplateSpecializationTypes;
  llvm::FoldingSet<PackExpansionType> PackExpansionTypes;
  mutable llvm::FoldingSet<ObjCObjectTypeImpl> ObjCObjectTypes;
  mutable llvm::FoldingSet<ObjCObjectPointerType> ObjCObjectPointerTypes;
  mutable llvm::FoldingSet<DependentUnaryTransformType>
    DependentUnaryTransformTypes;
  mutable llvm::ContextualFoldingSet<AutoType, ASTContext&> AutoTypes;
  mutable llvm::FoldingSet<DeducedTemplateSpecializationType>
    DeducedTemplateSpecializationTypes;
  mutable llvm::FoldingSet<AtomicType> AtomicTypes;
  mutable llvm::FoldingSet<AttributedType> AttributedTypes;
  mutable llvm::FoldingSet<PipeType> PipeTypes;
  mutable llvm::FoldingSet<BitIntType> BitIntTypes;
  mutable llvm::ContextualFoldingSet<DependentBitIntType, ASTContext &>
      DependentBitIntTypes;
  llvm::FoldingSet<BTFTagAttributedType> BTFTagAttributedTypes;

  mutable llvm::FoldingSet<CountAttributedType> CountAttributedTypes;

  mutable llvm::FoldingSet<QualifiedTemplateName> QualifiedTemplateNames;
  mutable llvm::FoldingSet<DependentTemplateName> DependentTemplateNames;
  mutable llvm::FoldingSet<SubstTemplateTemplateParmStorage>
    SubstTemplateTemplateParms;
  mutable llvm::ContextualFoldingSet<SubstTemplateTemplateParmPackStorage,
                                     ASTContext&>
    SubstTemplateTemplateParmPacks;

  mutable llvm::ContextualFoldingSet<ArrayParameterType, ASTContext &>
      ArrayParameterTypes;

  /// The set of nested name specifiers.
  ///
  /// This set is managed by the NestedNameSpecifier class.
  mutable llvm::FoldingSet<NestedNameSpecifier> NestedNameSpecifiers;
  mutable NestedNameSpecifier *GlobalNestedNameSpecifier = nullptr;

  /// A cache mapping from RecordDecls to ASTRecordLayouts.
  ///
  /// This is lazily created.  This is intentionally not serialized.
  mutable llvm::DenseMap<const RecordDecl*, const ASTRecordLayout*>
    ASTRecordLayouts;
  mutable llvm::DenseMap<const ObjCContainerDecl*, const ASTRecordLayout*>
    ObjCLayouts;

  /// A cache from types to size and alignment information.
  using TypeInfoMap = llvm::DenseMap<const Type *, struct TypeInfo>;
  mutable TypeInfoMap MemoizedTypeInfo;

  /// A cache from types to unadjusted alignment information. Only ARM and
  /// AArch64 targets need this information, keeping it separate prevents
  /// imposing overhead on TypeInfo size.
  using UnadjustedAlignMap = llvm::DenseMap<const Type *, unsigned>;
  mutable UnadjustedAlignMap MemoizedUnadjustedAlign;

  /// A cache mapping from CXXRecordDecls to key functions.
  llvm::DenseMap<const CXXRecordDecl*, LazyDeclPtr> KeyFunctions;

  /// Mapping from ObjCContainers to their ObjCImplementations.
  llvm::DenseMap<ObjCContainerDecl*, ObjCImplDecl*> ObjCImpls;

  /// Mapping from ObjCMethod to its duplicate declaration in the same
  /// interface.
  llvm::DenseMap<const ObjCMethodDecl*,const ObjCMethodDecl*> ObjCMethodRedecls;

  /// Mapping from __block VarDecls to BlockVarCopyInit.
  llvm::DenseMap<const VarDecl *, BlockVarCopyInit> BlockVarCopyInits;

  /// Mapping from GUIDs to the corresponding MSGuidDecl.
  mutable llvm::FoldingSet<MSGuidDecl> MSGuidDecls;

  /// Mapping from APValues to the corresponding UnnamedGlobalConstantDecl.
  mutable llvm::FoldingSet<UnnamedGlobalConstantDecl>
      UnnamedGlobalConstantDecls;

  /// Mapping from APValues to the corresponding TemplateParamObjects.
  mutable llvm::FoldingSet<TemplateParamObjectDecl> TemplateParamObjectDecls;

  /// A cache mapping a string value to a StringLiteral object with the same
  /// value.
  ///
  /// This is lazily created.  This is intentionally not serialized.
  mutable llvm::StringMap<StringLiteral *> StringLiteralCache;

  /// MD5 hash of CUID. It is calculated when first used and cached by this
  /// data member.
  mutable std::string CUIDHash;

  /// Representation of a "canonical" template template parameter that
  /// is used in canonical template names.
  class CanonicalTemplateTemplateParm : public llvm::FoldingSetNode {
    TemplateTemplateParmDecl *Parm;

  public:
    CanonicalTemplateTemplateParm(TemplateTemplateParmDecl *Parm)
        : Parm(Parm) {}

    TemplateTemplateParmDecl *getParam() const { return Parm; }

    void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &C) {
      Profile(ID, C, Parm);
    }

    static void Profile(llvm::FoldingSetNodeID &ID,
                        const ASTContext &C,
                        TemplateTemplateParmDecl *Parm);
  };
  mutable llvm::ContextualFoldingSet<CanonicalTemplateTemplateParm,
                                     const ASTContext&>
    CanonTemplateTemplateParms;

  TemplateTemplateParmDecl *
    getCanonicalTemplateTemplateParmDecl(TemplateTemplateParmDecl *TTP) const;

  /// The typedef for the __int128_t type.
  mutable TypedefDecl *Int128Decl = nullptr;

  /// The typedef for the __uint128_t type.
  mutable TypedefDecl *UInt128Decl = nullptr;

  /// The typedef for the target specific predefined
  /// __builtin_va_list type.
  mutable TypedefDecl *BuiltinVaListDecl = nullptr;

  /// The typedef for the predefined \c __builtin_ms_va_list type.
  mutable TypedefDecl *BuiltinMSVaListDecl = nullptr;

  /// The typedef for the predefined \c id type.
  mutable TypedefDecl *ObjCIdDecl = nullptr;

  /// The typedef for the predefined \c SEL type.
  mutable TypedefDecl *ObjCSelDecl = nullptr;

  /// The typedef for the predefined \c Class type.
  mutable TypedefDecl *ObjCClassDecl = nullptr;

  /// The typedef for the predefined \c Protocol class in Objective-C.
  mutable ObjCInterfaceDecl *ObjCProtocolClassDecl = nullptr;

  /// The typedef for the predefined 'BOOL' type.
  mutable TypedefDecl *BOOLDecl = nullptr;

  // Typedefs which may be provided defining the structure of Objective-C
  // pseudo-builtins
  QualType ObjCIdRedefinitionType;
  QualType ObjCClassRedefinitionType;
  QualType ObjCSelRedefinitionType;

  /// The identifier 'bool'.
  mutable IdentifierInfo *BoolName = nullptr;

  /// The identifier 'NSObject'.
  mutable IdentifierInfo *NSObjectName = nullptr;

  /// The identifier 'NSCopying'.
  IdentifierInfo *NSCopyingName = nullptr;

  /// The identifier '__make_integer_seq'.
  mutable IdentifierInfo *MakeIntegerSeqName = nullptr;

  /// The identifier '__type_pack_element'.
  mutable IdentifierInfo *TypePackElementName = nullptr;

  QualType ObjCConstantStringType;
  mutable RecordDecl *CFConstantStringTagDecl = nullptr;
  mutable TypedefDecl *CFConstantStringTypeDecl = nullptr;

  mutable QualType ObjCSuperType;

  QualType ObjCNSStringType;

  /// The typedef declaration for the Objective-C "instancetype" type.
  TypedefDecl *ObjCInstanceTypeDecl = nullptr;

  /// The type for the C FILE type.
  TypeDecl *FILEDecl = nullptr;

  /// The type for the C jmp_buf type.
  TypeDecl *jmp_bufDecl = nullptr;

  /// The type for the C sigjmp_buf type.
  TypeDecl *sigjmp_bufDecl = nullptr;

  /// The type for the C ucontext_t type.
  TypeDecl *ucontext_tDecl = nullptr;

  /// Type for the Block descriptor for Blocks CodeGen.
  ///
  /// Since this is only used for generation of debug info, it is not
  /// serialized.
  mutable RecordDecl *BlockDescriptorType = nullptr;

  /// Type for the Block descriptor for Blocks CodeGen.
  ///
  /// Since this is only used for generation of debug info, it is not
  /// serialized.
  mutable RecordDecl *BlockDescriptorExtendedType = nullptr;

  /// Declaration for the CUDA cudaConfigureCall function.
  FunctionDecl *cudaConfigureCallDecl = nullptr;

  /// Keeps track of all declaration attributes.
  ///
  /// Since so few decls have attrs, we keep them in a hash map instead of
  /// wasting space in the Decl class.
  llvm::DenseMap<const Decl*, AttrVec*> DeclAttrs;

  /// A mapping from non-redeclarable declarations in modules that were
  /// merged with other declarations to the canonical declaration that they were
  /// merged into.
  llvm::DenseMap<Decl*, Decl*> MergedDecls;

  /// A mapping from a defining declaration to a list of modules (other
  /// than the owning module of the declaration) that contain merged
  /// definitions of that entity.
  llvm::DenseMap<NamedDecl*, llvm::TinyPtrVector<Module*>> MergedDefModules;

  /// Initializers for a module, in order. Each Decl will be either
  /// something that has a semantic effect on startup (such as a variable with
  /// a non-constant initializer), or an ImportDecl (which recursively triggers
  /// initialization of another module).
  struct PerModuleInitializers {
    llvm::SmallVector<Decl*, 4> Initializers;
    llvm::SmallVector<GlobalDeclID, 4> LazyInitializers;

    void resolve(ASTContext &Ctx);
  };
  llvm::DenseMap<Module*, PerModuleInitializers*> ModuleInitializers;

  /// This is the top-level (C++20) Named module we are building.
  Module *CurrentCXXNamedModule = nullptr;

  /// Help structures to decide whether two `const Module *` belongs
  /// to the same conceptual module to avoid the expensive to string comparison
  /// if possible.
  ///
  /// Not serialized intentionally.
  llvm::StringMap<const Module *> PrimaryModuleNameMap;
  llvm::DenseMap<const Module *, const Module *> SameModuleLookupSet;

  static constexpr unsigned ConstantArrayTypesLog2InitSize = 8;
  static constexpr unsigned GeneralTypesLog2InitSize = 9;
  static constexpr unsigned FunctionProtoTypesLog2InitSize = 12;

  ASTContext &this_() { return *this; }

public:
  /// A type synonym for the TemplateOrInstantiation mapping.
  using TemplateOrSpecializationInfo =
      llvm::PointerUnion<VarTemplateDecl *, MemberSpecializationInfo *>;

private:
  friend class ASTDeclReader;
  friend class ASTReader;
  friend class ASTWriter;
  template <class> friend class serialization::AbstractTypeReader;
  friend class CXXRecordDecl;
  friend class IncrementalParser;

  /// A mapping to contain the template or declaration that
  /// a variable declaration describes or was instantiated from,
  /// respectively.
  ///
  /// For non-templates, this value will be NULL. For variable
  /// declarations that describe a variable template, this will be a
  /// pointer to a VarTemplateDecl. For static data members
  /// of class template specializations, this will be the
  /// MemberSpecializationInfo referring to the member variable that was
  /// instantiated or specialized. Thus, the mapping will keep track of
  /// the static data member templates from which static data members of
  /// class template specializations were instantiated.
  ///
  /// Given the following example:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   static T value;
  /// };
  ///
  /// template<typename T>
  ///   T X<T>::value = T(17);
  ///
  /// int *x = &X<int>::value;
  /// \endcode
  ///
  /// This mapping will contain an entry that maps from the VarDecl for
  /// X<int>::value to the corresponding VarDecl for X<T>::value (within the
  /// class template X) and will be marked TSK_ImplicitInstantiation.
  llvm::DenseMap<const VarDecl *, TemplateOrSpecializationInfo>
  TemplateOrInstantiation;

  /// Keeps track of the declaration from which a using declaration was
  /// created during instantiation.
  ///
  /// The source and target declarations are always a UsingDecl, an
  /// UnresolvedUsingValueDecl, or an UnresolvedUsingTypenameDecl.
  ///
  /// For example:
  /// \code
  /// template<typename T>
  /// struct A {
  ///   void f();
  /// };
  ///
  /// template<typename T>
  /// struct B : A<T> {
  ///   using A<T>::f;
  /// };
  ///
  /// template struct B<int>;
  /// \endcode
  ///
  /// This mapping will contain an entry that maps from the UsingDecl in
  /// B<int> to the UnresolvedUsingDecl in B<T>.
  llvm::DenseMap<NamedDecl *, NamedDecl *> InstantiatedFromUsingDecl;

  /// Like InstantiatedFromUsingDecl, but for using-enum-declarations. Maps
  /// from the instantiated using-enum to the templated decl from whence it
  /// came.
  /// Note that using-enum-declarations cannot be dependent and
  /// thus will never be instantiated from an "unresolved"
  /// version thereof (as with using-declarations), so each mapping is from
  /// a (resolved) UsingEnumDecl to a (resolved) UsingEnumDecl.
  llvm::DenseMap<UsingEnumDecl *, UsingEnumDecl *>
      InstantiatedFromUsingEnumDecl;

  /// Simlarly maps instantiated UsingShadowDecls to their origin.
  llvm::DenseMap<UsingShadowDecl*, UsingShadowDecl*>
    InstantiatedFromUsingShadowDecl;

  llvm::DenseMap<FieldDecl *, FieldDecl *> InstantiatedFromUnnamedFieldDecl;

  /// Mapping that stores the methods overridden by a given C++
  /// member function.
  ///
  /// Since most C++ member functions aren't virtual and therefore
  /// don't override anything, we store the overridden functions in
  /// this map on the side rather than within the CXXMethodDecl structure.
  using CXXMethodVector = llvm::TinyPtrVector<const CXXMethodDecl *>;
  llvm::DenseMap<const CXXMethodDecl *, CXXMethodVector> OverriddenMethods;

  /// Mapping from each declaration context to its corresponding
  /// mangling numbering context (used for constructs like lambdas which
  /// need to be consistently numbered for the mangler).
  llvm::DenseMap<const DeclContext *, std::unique_ptr<MangleNumberingContext>>
      MangleNumberingContexts;
  llvm::DenseMap<const Decl *, std::unique_ptr<MangleNumberingContext>>
      ExtraMangleNumberingContexts;

  /// Side-table of mangling numbers for declarations which rarely
  /// need them (like static local vars).
  llvm::MapVector<const NamedDecl *, unsigned> MangleNumbers;
  llvm::MapVector<const VarDecl *, unsigned> StaticLocalNumbers;
  /// Mapping the associated device lambda mangling number if present.
  mutable llvm::DenseMap<const CXXRecordDecl *, unsigned>
      DeviceLambdaManglingNumbers;

  /// Mapping that stores parameterIndex values for ParmVarDecls when
  /// that value exceeds the bitfield size of ParmVarDeclBits.ParameterIndex.
  using ParameterIndexTable = llvm::DenseMap<const VarDecl *, unsigned>;
  ParameterIndexTable ParamIndices;

  ImportDecl *FirstLocalImport = nullptr;
  ImportDecl *LastLocalImport = nullptr;

  TranslationUnitDecl *TUDecl = nullptr;
  mutable ExternCContextDecl *ExternCContext = nullptr;
  mutable BuiltinTemplateDecl *MakeIntegerSeqDecl = nullptr;
  mutable BuiltinTemplateDecl *TypePackElementDecl = nullptr;

  /// The associated SourceManager object.
  SourceManager &SourceMgr;

  /// The language options used to create the AST associated with
  ///  this ASTContext object.
  LangOptions &LangOpts;

  /// NoSanitizeList object that is used by sanitizers to decide which
  /// entities should not be instrumented.
  std::unique_ptr<NoSanitizeList> NoSanitizeL;

  /// Function filtering mechanism to determine whether a given function
  /// should be imbued with the XRay "always" or "never" attributes.
  std::unique_ptr<XRayFunctionFilter> XRayFilter;

  /// ProfileList object that is used by the profile instrumentation
  /// to decide which entities should be instrumented.
  std::unique_ptr<ProfileList> ProfList;

  /// The allocator used to create AST objects.
  ///
  /// AST objects are never destructed; rather, all memory associated with the
  /// AST objects will be released when the ASTContext itself is destroyed.
  mutable llvm::BumpPtrAllocator BumpAlloc;

  /// Allocator for partial diagnostics.
  PartialDiagnostic::DiagStorageAllocator DiagAllocator;

  /// The current C++ ABI.
  std::unique_ptr<CXXABI> ABI;
  CXXABI *createCXXABI(const TargetInfo &T);

  /// Address space map mangling must be used with language specific
  /// address spaces (e.g. OpenCL/CUDA)
  bool AddrSpaceMapMangling;

  /// For performance, track whether any function effects are in use.
  mutable bool AnyFunctionEffects = false;

  const TargetInfo *Target = nullptr;
  const TargetInfo *AuxTarget = nullptr;
  clang::PrintingPolicy PrintingPolicy;
  std::unique_ptr<interp::Context> InterpContext;
  std::unique_ptr<ParentMapContext> ParentMapCtx;

  /// Keeps track of the deallocated DeclListNodes for future reuse.
  DeclListNode *ListNodeFreeList = nullptr;

public:
  IdentifierTable &Idents;
  SelectorTable &Selectors;
  Builtin::Context &BuiltinInfo;
  const TranslationUnitKind TUKind;
  mutable DeclarationNameTable DeclarationNames;
  IntrusiveRefCntPtr<ExternalASTSource> ExternalSource;
  ASTMutationListener *Listener = nullptr;

  /// Returns the clang bytecode interpreter context.
  interp::Context &getInterpContext();

  struct CUDAConstantEvalContext {
    /// Do not allow wrong-sided variables in constant expressions.
    bool NoWrongSidedVars = false;
  } CUDAConstantEvalCtx;
  struct CUDAConstantEvalContextRAII {
    ASTContext &Ctx;
    CUDAConstantEvalContext SavedCtx;
    CUDAConstantEvalContextRAII(ASTContext &Ctx_, bool NoWrongSidedVars)
        : Ctx(Ctx_), SavedCtx(Ctx_.CUDAConstantEvalCtx) {
      Ctx_.CUDAConstantEvalCtx.NoWrongSidedVars = NoWrongSidedVars;
    }
    ~CUDAConstantEvalContextRAII() { Ctx.CUDAConstantEvalCtx = SavedCtx; }
  };

  /// Returns the dynamic AST node parent map context.
  ParentMapContext &getParentMapContext();

  // A traversal scope limits the parts of the AST visible to certain analyses.
  // RecursiveASTVisitor only visits specified children of TranslationUnitDecl.
  // getParents() will only observe reachable parent edges.
  //
  // The scope is defined by a set of "top-level" declarations which will be
  // visible under the TranslationUnitDecl.
  // Initially, it is the entire TU, represented by {getTranslationUnitDecl()}.
  //
  // After setTraversalScope({foo, bar}), the exposed AST looks like:
  // TranslationUnitDecl
  //  - foo
  //    - ...
  //  - bar
  //    - ...
  // All other siblings of foo and bar are pruned from the tree.
  // (However they are still accessible via TranslationUnitDecl->decls())
  //
  // Changing the scope clears the parent cache, which is expensive to rebuild.
  std::vector<Decl *> getTraversalScope() const { return TraversalScope; }
  void setTraversalScope(const std::vector<Decl *> &);

  /// Forwards to get node parents from the ParentMapContext. New callers should
  /// use ParentMapContext::getParents() directly.
  template <typename NodeT> DynTypedNodeList getParents(const NodeT &Node);

  const clang::PrintingPolicy &getPrintingPolicy() const {
    return PrintingPolicy;
  }

  void setPrintingPolicy(const clang::PrintingPolicy &Policy) {
    PrintingPolicy = Policy;
  }

  SourceManager& getSourceManager() { return SourceMgr; }
  const SourceManager& getSourceManager() const { return SourceMgr; }

  // Cleans up some of the data structures. This allows us to do cleanup
  // normally done in the destructor earlier. Renders much of the ASTContext
  // unusable, mostly the actual AST nodes, so should be called when we no
  // longer need access to the AST.
  void cleanup();

  llvm::BumpPtrAllocator &getAllocator() const {
    return BumpAlloc;
  }

  void *Allocate(size_t Size, unsigned Align = 8) const {
    return BumpAlloc.Allocate(Size, Align);
  }
  template <typename T> T *Allocate(size_t Num = 1) const {
    return static_cast<T *>(Allocate(Num * sizeof(T), alignof(T)));
  }
  void Deallocate(void *Ptr) const {}

  llvm::StringRef backupStr(llvm::StringRef S) const {
    char *Buf = new (*this) char[S.size()];
    std::copy(S.begin(), S.end(), Buf);
    return llvm::StringRef(Buf, S.size());
  }

  /// Allocates a \c DeclListNode or returns one from the \c ListNodeFreeList
  /// pool.
  DeclListNode *AllocateDeclListNode(clang::NamedDecl *ND) {
    if (DeclListNode *Alloc = ListNodeFreeList) {
      ListNodeFreeList = Alloc->Rest.dyn_cast<DeclListNode*>();
      Alloc->D = ND;
      Alloc->Rest = nullptr;
      return Alloc;
    }
    return new (*this) DeclListNode(ND);
  }
  /// Deallcates a \c DeclListNode by returning it to the \c ListNodeFreeList
  /// pool.
  void DeallocateDeclListNode(DeclListNode *N) {
    N->Rest = ListNodeFreeList;
    ListNodeFreeList = N;
  }

  /// Return the total amount of physical memory allocated for representing
  /// AST nodes and type information.
  size_t getASTAllocatedMemory() const {
    return BumpAlloc.getTotalMemory();
  }

  /// Return the total memory used for various side tables.
  size_t getSideTableAllocatedMemory() const;

  PartialDiagnostic::DiagStorageAllocator &getDiagAllocator() {
    return DiagAllocator;
  }

  const TargetInfo &getTargetInfo() const { return *Target; }
  const TargetInfo *getAuxTargetInfo() const { return AuxTarget; }

  /// getIntTypeForBitwidth -
  /// sets integer QualTy according to specified details:
  /// bitwidth, signed/unsigned.
  /// Returns empty type if there is no appropriate target types.
  QualType getIntTypeForBitwidth(unsigned DestWidth,
                                 unsigned Signed) const;

  /// getRealTypeForBitwidth -
  /// sets floating point QualTy according to specified bitwidth.
  /// Returns empty type if there is no appropriate target types.
  QualType getRealTypeForBitwidth(unsigned DestWidth,
                                  FloatModeKind ExplicitType) const;

  bool AtomicUsesUnsupportedLibcall(const AtomicExpr *E) const;

  const LangOptions& getLangOpts() const { return LangOpts; }

  // If this condition is false, typo correction must be performed eagerly
  // rather than delayed in many places, as it makes use of dependent types.
  // the condition is false for clang's C-only codepath, as it doesn't support
  // dependent types yet.
  bool isDependenceAllowed() const {
    return LangOpts.CPlusPlus || LangOpts.RecoveryAST;
  }

  const NoSanitizeList &getNoSanitizeList() const { return *NoSanitizeL; }

  const XRayFunctionFilter &getXRayFilter() const {
    return *XRayFilter;
  }

  const ProfileList &getProfileList() const { return *ProfList; }

  DiagnosticsEngine &getDiagnostics() const;

  FullSourceLoc getFullLoc(SourceLocation Loc) const {
    return FullSourceLoc(Loc,SourceMgr);
  }

  /// Return the C++ ABI kind that should be used. The C++ ABI can be overriden
  /// at compile time with `-fc++-abi=`. If this is not provided, we instead use
  /// the default ABI set by the target.
  TargetCXXABI::Kind getCXXABIKind() const;

  /// All comments in this translation unit.
  RawCommentList Comments;

  /// True if comments are already loaded from ExternalASTSource.
  mutable bool CommentsLoaded = false;

  /// Mapping from declaration to directly attached comment.
  ///
  /// Raw comments are owned by Comments list.  This mapping is populated
  /// lazily.
  mutable llvm::DenseMap<const Decl *, const RawComment *> DeclRawComments;

  /// Mapping from canonical declaration to the first redeclaration in chain
  /// that has a comment attached.
  ///
  /// Raw comments are owned by Comments list.  This mapping is populated
  /// lazily.
  mutable llvm::DenseMap<const Decl *, const Decl *> RedeclChainComments;

  /// Keeps track of redeclaration chains that don't have any comment attached.
  /// Mapping from canonical declaration to redeclaration chain that has no
  /// comments attached to any redeclaration. Specifically it's mapping to
  /// the last redeclaration we've checked.
  ///
  /// Shall not contain declarations that have comments attached to any
  /// redeclaration in their chain.
  mutable llvm::DenseMap<const Decl *, const Decl *> CommentlessRedeclChains;

  /// Mapping from declarations to parsed comments attached to any
  /// redeclaration.
  mutable llvm::DenseMap<const Decl *, comments::FullComment *> ParsedComments;

  /// Attaches \p Comment to \p OriginalD and to its redeclaration chain
  /// and removes the redeclaration chain from the set of commentless chains.
  ///
  /// Don't do anything if a comment has already been attached to \p OriginalD
  /// or its redeclaration chain.
  void cacheRawCommentForDecl(const Decl &OriginalD,
                              const RawComment &Comment) const;

  /// \returns searches \p CommentsInFile for doc comment for \p D.
  ///
  /// \p RepresentativeLocForDecl is used as a location for searching doc
  /// comments. \p CommentsInFile is a mapping offset -> comment of files in the
  /// same file where \p RepresentativeLocForDecl is.
  RawComment *getRawCommentForDeclNoCacheImpl(
      const Decl *D, const SourceLocation RepresentativeLocForDecl,
      const std::map<unsigned, RawComment *> &CommentsInFile) const;

  /// Return the documentation comment attached to a given declaration,
  /// without looking into cache.
  RawComment *getRawCommentForDeclNoCache(const Decl *D) const;

public:
  void addComment(const RawComment &RC);

  /// Return the documentation comment attached to a given declaration.
  /// Returns nullptr if no comment is attached.
  ///
  /// \param OriginalDecl if not nullptr, is set to declaration AST node that
  /// had the comment, if the comment we found comes from a redeclaration.
  const RawComment *
  getRawCommentForAnyRedecl(const Decl *D,
                            const Decl **OriginalDecl = nullptr) const;

  /// Searches existing comments for doc comments that should be attached to \p
  /// Decls. If any doc comment is found, it is parsed.
  ///
  /// Requirement: All \p Decls are in the same file.
  ///
  /// If the last comment in the file is already attached we assume
  /// there are not comments left to be attached to \p Decls.
  void attachCommentsToJustParsedDecls(ArrayRef<Decl *> Decls,
                                       const Preprocessor *PP);

  /// Return parsed documentation comment attached to a given declaration.
  /// Returns nullptr if no comment is attached.
  ///
  /// \param PP the Preprocessor used with this TU.  Could be nullptr if
  /// preprocessor is not available.
  comments::FullComment *getCommentForDecl(const Decl *D,
                                           const Preprocessor *PP) const;

  /// Return parsed documentation comment attached to a given declaration.
  /// Returns nullptr if no comment is attached. Does not look at any
  /// redeclarations of the declaration.
  comments::FullComment *getLocalCommentForDeclUncached(const Decl *D) const;

  comments::FullComment *cloneFullComment(comments::FullComment *FC,
                                         const Decl *D) const;

private:
  mutable comments::CommandTraits CommentCommandTraits;

  /// Iterator that visits import declarations.
  class import_iterator {
    ImportDecl *Import = nullptr;

  public:
    using value_type = ImportDecl *;
    using reference = ImportDecl *;
    using pointer = ImportDecl *;
    using difference_type = int;
    using iterator_category = std::forward_iterator_tag;

    import_iterator() = default;
    explicit import_iterator(ImportDecl *Import) : Import(Import) {}

    reference operator*() const { return Import; }
    pointer operator->() const { return Import; }

    import_iterator &operator++() {
      Import = ASTContext::getNextLocalImport(Import);
      return *this;
    }

    import_iterator operator++(int) {
      import_iterator Other(*this);
      ++(*this);
      return Other;
    }

    friend bool operator==(import_iterator X, import_iterator Y) {
      return X.Import == Y.Import;
    }

    friend bool operator!=(import_iterator X, import_iterator Y) {
      return X.Import != Y.Import;
    }
  };

public:
  comments::CommandTraits &getCommentCommandTraits() const {
    return CommentCommandTraits;
  }

  /// Retrieve the attributes for the given declaration.
  AttrVec& getDeclAttrs(const Decl *D);

  /// Erase the attributes corresponding to the given declaration.
  void eraseDeclAttrs(const Decl *D);

  /// If this variable is an instantiated static data member of a
  /// class template specialization, returns the templated static data member
  /// from which it was instantiated.
  // FIXME: Remove ?
  MemberSpecializationInfo *getInstantiatedFromStaticDataMember(
                                                           const VarDecl *Var);

  /// Note that the static data member \p Inst is an instantiation of
  /// the static data member template \p Tmpl of a class template.
  void setInstantiatedFromStaticDataMember(VarDecl *Inst, VarDecl *Tmpl,
                                           TemplateSpecializationKind TSK,
                        SourceLocation PointOfInstantiation = SourceLocation());

  TemplateOrSpecializationInfo
  getTemplateOrSpecializationInfo(const VarDecl *Var);

  void setTemplateOrSpecializationInfo(VarDecl *Inst,
                                       TemplateOrSpecializationInfo TSI);

  /// If the given using decl \p Inst is an instantiation of
  /// another (possibly unresolved) using decl, return it.
  NamedDecl *getInstantiatedFromUsingDecl(NamedDecl *Inst);

  /// Remember that the using decl \p Inst is an instantiation
  /// of the using decl \p Pattern of a class template.
  void setInstantiatedFromUsingDecl(NamedDecl *Inst, NamedDecl *Pattern);

  /// If the given using-enum decl \p Inst is an instantiation of
  /// another using-enum decl, return it.
  UsingEnumDecl *getInstantiatedFromUsingEnumDecl(UsingEnumDecl *Inst);

  /// Remember that the using enum decl \p Inst is an instantiation
  /// of the using enum decl \p Pattern of a class template.
  void setInstantiatedFromUsingEnumDecl(UsingEnumDecl *Inst,
                                        UsingEnumDecl *Pattern);

  UsingShadowDecl *getInstantiatedFromUsingShadowDecl(UsingShadowDecl *Inst);
  void setInstantiatedFromUsingShadowDecl(UsingShadowDecl *Inst,
                                          UsingShadowDecl *Pattern);

  FieldDecl *getInstantiatedFromUnnamedFieldDecl(FieldDecl *Field);

  void setInstantiatedFromUnnamedFieldDecl(FieldDecl *Inst, FieldDecl *Tmpl);

  // Access to the set of methods overridden by the given C++ method.
  using overridden_cxx_method_iterator = CXXMethodVector::const_iterator;
  overridden_cxx_method_iterator
  overridden_methods_begin(const CXXMethodDecl *Method) const;

  overridden_cxx_method_iterator
  overridden_methods_end(const CXXMethodDecl *Method) const;

  unsigned overridden_methods_size(const CXXMethodDecl *Method) const;

  using overridden_method_range =
      llvm::iterator_range<overridden_cxx_method_iterator>;

  overridden_method_range overridden_methods(const CXXMethodDecl *Method) const;

  /// Note that the given C++ \p Method overrides the given \p
  /// Overridden method.
  void addOverriddenMethod(const CXXMethodDecl *Method,
                           const CXXMethodDecl *Overridden);

  /// Return C++ or ObjC overridden methods for the given \p Method.
  ///
  /// An ObjC method is considered to override any method in the class's
  /// base classes, its protocols, or its categories' protocols, that has
  /// the same selector and is of the same kind (class or instance).
  /// A method in an implementation is not considered as overriding the same
  /// method in the interface or its categories.
  void getOverriddenMethods(
                        const NamedDecl *Method,
                        SmallVectorImpl<const NamedDecl *> &Overridden) const;

  /// Notify the AST context that a new import declaration has been
  /// parsed or implicitly created within this translation unit.
  void addedLocalImportDecl(ImportDecl *Import);

  static ImportDecl *getNextLocalImport(ImportDecl *Import) {
    return Import->getNextLocalImport();
  }

  using import_range = llvm::iterator_range<import_iterator>;

  import_range local_imports() const {
    return import_range(import_iterator(FirstLocalImport), import_iterator());
  }

  Decl *getPrimaryMergedDecl(Decl *D) {
    Decl *Result = MergedDecls.lookup(D);
    return Result ? Result : D;
  }
  void setPrimaryMergedDecl(Decl *D, Decl *Primary) {
    MergedDecls[D] = Primary;
  }

  /// Note that the definition \p ND has been merged into module \p M,
  /// and should be visible whenever \p M is visible.
  void mergeDefinitionIntoModule(NamedDecl *ND, Module *M,
                                 bool NotifyListeners = true);

  /// Clean up the merged definition list. Call this if you might have
  /// added duplicates into the list.
  void deduplicateMergedDefinitonsFor(NamedDecl *ND);

  /// Get the additional modules in which the definition \p Def has
  /// been merged.
  ArrayRef<Module*> getModulesWithMergedDefinition(const NamedDecl *Def);

  /// Add a declaration to the list of declarations that are initialized
  /// for a module. This will typically be a global variable (with internal
  /// linkage) that runs module initializers, such as the iostream initializer,
  /// or an ImportDecl nominating another module that has initializers.
  void addModuleInitializer(Module *M, Decl *Init);

  void addLazyModuleInitializers(Module *M, ArrayRef<GlobalDeclID> IDs);

  /// Get the initializations to perform when importing a module, if any.
  ArrayRef<Decl*> getModuleInitializers(Module *M);

  /// Set the (C++20) module we are building.
  void setCurrentNamedModule(Module *M);

  /// Get module under construction, nullptr if this is not a C++20 module.
  Module *getCurrentNamedModule() const { return CurrentCXXNamedModule; }

  /// If the two module \p M1 and \p M2 are in the same module.
  ///
  /// FIXME: The signature may be confusing since `clang::Module` means to
  /// a module fragment or a module unit but not a C++20 module.
  bool isInSameModule(const Module *M1, const Module *M2);

  TranslationUnitDecl *getTranslationUnitDecl() const {
    return TUDecl->getMostRecentDecl();
  }
  void addTranslationUnitDecl() {
    assert(!TUDecl || TUKind == TU_Incremental);
    TranslationUnitDecl *NewTUDecl = TranslationUnitDecl::Create(*this);
    if (TraversalScope.empty() || TraversalScope.back() == TUDecl)
      TraversalScope = {NewTUDecl};
    if (TUDecl)
      NewTUDecl->setPreviousDecl(TUDecl);
    TUDecl = NewTUDecl;
  }

  ExternCContextDecl *getExternCContextDecl() const;
  BuiltinTemplateDecl *getMakeIntegerSeqDecl() const;
  BuiltinTemplateDecl *getTypePackElementDecl() const;

  // Builtin Types.
  CanQualType VoidTy;
  CanQualType BoolTy;
  CanQualType CharTy;
  CanQualType WCharTy;  // [C++ 3.9.1p5].
  CanQualType WideCharTy; // Same as WCharTy in C++, integer type in C99.
  CanQualType WIntTy;   // [C99 7.24.1], integer type unchanged by default promotions.
  CanQualType Char8Ty;  // [C++20 proposal]
  CanQualType Char16Ty; // [C++0x 3.9.1p5], integer type in C99.
  CanQualType Char32Ty; // [C++0x 3.9.1p5], integer type in C99.
  CanQualType SignedCharTy, ShortTy, IntTy, LongTy, LongLongTy, Int128Ty;
  CanQualType UnsignedCharTy, UnsignedShortTy, UnsignedIntTy, UnsignedLongTy;
  CanQualType UnsignedLongLongTy, UnsignedInt128Ty;
  CanQualType FloatTy, DoubleTy, LongDoubleTy, Float128Ty, Ibm128Ty;
  CanQualType ShortAccumTy, AccumTy,
      LongAccumTy;  // ISO/IEC JTC1 SC22 WG14 N1169 Extension
  CanQualType UnsignedShortAccumTy, UnsignedAccumTy, UnsignedLongAccumTy;
  CanQualType ShortFractTy, FractTy, LongFractTy;
  CanQualType UnsignedShortFractTy, UnsignedFractTy, UnsignedLongFractTy;
  CanQualType SatShortAccumTy, SatAccumTy, SatLongAccumTy;
  CanQualType SatUnsignedShortAccumTy, SatUnsignedAccumTy,
      SatUnsignedLongAccumTy;
  CanQualType SatShortFractTy, SatFractTy, SatLongFractTy;
  CanQualType SatUnsignedShortFractTy, SatUnsignedFractTy,
      SatUnsignedLongFractTy;
  CanQualType HalfTy; // [OpenCL 6.1.1.1], ARM NEON
  CanQualType BFloat16Ty;
  CanQualType Float16Ty; // C11 extension ISO/IEC TS 18661-3
  CanQualType VoidPtrTy, NullPtrTy;
  CanQualType DependentTy, OverloadTy, BoundMemberTy, UnresolvedTemplateTy,
      UnknownAnyTy;
  CanQualType BuiltinFnTy;
  CanQualType PseudoObjectTy, ARCUnbridgedCastTy;
  CanQualType ObjCBuiltinIdTy, ObjCBuiltinClassTy, ObjCBuiltinSelTy;
  CanQualType ObjCBuiltinBoolTy;
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  CanQualType SingletonId;
#include "clang/Basic/OpenCLImageTypes.def"
  CanQualType OCLSamplerTy, OCLEventTy, OCLClkEventTy;
  CanQualType OCLQueueTy, OCLReserveIDTy;
  CanQualType IncompleteMatrixIdxTy;
  CanQualType ArraySectionTy;
  CanQualType OMPArrayShapingTy, OMPIteratorTy;
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  CanQualType Id##Ty;
#include "clang/Basic/OpenCLExtensionTypes.def"
#define SVE_TYPE(Name, Id, SingletonId) \
  CanQualType SingletonId;
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
  CanQualType Id##Ty;
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) \
  CanQualType SingletonId;
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) CanQualType SingletonId;
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) CanQualType SingletonId;
#include "clang/Basic/AMDGPUTypes.def"

  // Types for deductions in C++0x [stmt.ranged]'s desugaring. Built on demand.
  mutable QualType AutoDeductTy;     // Deduction against 'auto'.
  mutable QualType AutoRRefDeductTy; // Deduction against 'auto &&'.

  // Decl used to help define __builtin_va_list for some targets.
  // The decl is built when constructing 'BuiltinVaListDecl'.
  mutable Decl *VaListTagDecl = nullptr;

  // Implicitly-declared type 'struct _GUID'.
  mutable TagDecl *MSGuidTagDecl = nullptr;

  /// Keep track of CUDA/HIP device-side variables ODR-used by host code.
  /// This does not include extern shared variables used by device host
  /// functions as addresses of shared variables are per warp, therefore
  /// cannot be accessed by host code.
  llvm::DenseSet<const VarDecl *> CUDADeviceVarODRUsedByHost;

  /// Keep track of CUDA/HIP external kernels or device variables ODR-used by
  /// host code.
  llvm::DenseSet<const ValueDecl *> CUDAExternalDeviceDeclODRUsedByHost;

  /// Keep track of CUDA/HIP implicit host device functions used on device side
  /// in device compilation.
  llvm::DenseSet<const FunctionDecl *> CUDAImplicitHostDeviceFunUsedByDevice;

  /// For capturing lambdas with an explicit object parameter whose type is
  /// derived from the lambda type, we need to perform derived-to-base
  /// conversion so we can access the captures; the cast paths for that
  /// are stored here.
  llvm::DenseMap<const CXXMethodDecl *, CXXCastPath> LambdaCastPaths;

  ASTContext(LangOptions &LOpts, SourceManager &SM, IdentifierTable &idents,
             SelectorTable &sels, Builtin::Context &builtins,
             TranslationUnitKind TUKind);
  ASTContext(const ASTContext &) = delete;
  ASTContext &operator=(const ASTContext &) = delete;
  ~ASTContext();

  /// Attach an external AST source to the AST context.
  ///
  /// The external AST source provides the ability to load parts of
  /// the abstract syntax tree as needed from some external storage,
  /// e.g., a precompiled header.
  void setExternalSource(IntrusiveRefCntPtr<ExternalASTSource> Source);

  /// Retrieve a pointer to the external AST source associated
  /// with this AST context, if any.
  ExternalASTSource *getExternalSource() const {
    return ExternalSource.get();
  }

  /// Attach an AST mutation listener to the AST context.
  ///
  /// The AST mutation listener provides the ability to track modifications to
  /// the abstract syntax tree entities committed after they were initially
  /// created.
  void setASTMutationListener(ASTMutationListener *Listener) {
    this->Listener = Listener;
  }

  /// Retrieve a pointer to the AST mutation listener associated
  /// with this AST context, if any.
  ASTMutationListener *getASTMutationListener() const { return Listener; }

  void PrintStats() const;
  const SmallVectorImpl<Type *>& getTypes() const { return Types; }

  BuiltinTemplateDecl *buildBuiltinTemplateDecl(BuiltinTemplateKind BTK,
                                                const IdentifierInfo *II) const;

  /// Create a new implicit TU-level CXXRecordDecl or RecordDecl
  /// declaration.
  RecordDecl *buildImplicitRecord(
      StringRef Name,
      RecordDecl::TagKind TK = RecordDecl::TagKind::Struct) const;

  /// Create a new implicit TU-level typedef declaration.
  TypedefDecl *buildImplicitTypedef(QualType T, StringRef Name) const;

  /// Retrieve the declaration for the 128-bit signed integer type.
  TypedefDecl *getInt128Decl() const;

  /// Retrieve the declaration for the 128-bit unsigned integer type.
  TypedefDecl *getUInt128Decl() const;

  //===--------------------------------------------------------------------===//
  //                           Type Constructors
  //===--------------------------------------------------------------------===//

private:
  /// Return a type with extended qualifiers.
  QualType getExtQualType(const Type *Base, Qualifiers Quals) const;

  QualType getTypeDeclTypeSlow(const TypeDecl *Decl) const;

  QualType getPipeType(QualType T, bool ReadOnly) const;

public:
  /// Return the uniqued reference to the type for an address space
  /// qualified type with the specified type and address space.
  ///
  /// The resulting type has a union of the qualifiers from T and the address
  /// space. If T already has an address space specifier, it is silently
  /// replaced.
  QualType getAddrSpaceQualType(QualType T, LangAS AddressSpace) const;

  /// Remove any existing address space on the type and returns the type
  /// with qualifiers intact (or that's the idea anyway)
  ///
  /// The return type should be T with all prior qualifiers minus the address
  /// space.
  QualType removeAddrSpaceQualType(QualType T) const;

  /// Return the "other" discriminator used for the pointer auth schema used for
  /// vtable pointers in instances of the requested type.
  uint16_t
  getPointerAuthVTablePointerDiscriminator(const CXXRecordDecl *RD);

  /// Return the "other" type-specific discriminator for the given type.
  uint16_t getPointerAuthTypeDiscriminator(QualType T);

  /// Apply Objective-C protocol qualifiers to the given type.
  /// \param allowOnPointerType specifies if we can apply protocol
  /// qualifiers on ObjCObjectPointerType. It can be set to true when
  /// constructing the canonical type of a Objective-C type parameter.
  QualType applyObjCProtocolQualifiers(QualType type,
      ArrayRef<ObjCProtocolDecl *> protocols, bool &hasError,
      bool allowOnPointerType = false) const;

  /// Return the uniqued reference to the type for an Objective-C
  /// gc-qualified type.
  ///
  /// The resulting type has a union of the qualifiers from T and the gc
  /// attribute.
  QualType getObjCGCQualType(QualType T, Qualifiers::GC gcAttr) const;

  /// Remove the existing address space on the type if it is a pointer size
  /// address space and return the type with qualifiers intact.
  QualType removePtrSizeAddrSpace(QualType T) const;

  /// Return the uniqued reference to the type for a \c restrict
  /// qualified type.
  ///
  /// The resulting type has a union of the qualifiers from \p T and
  /// \c restrict.
  QualType getRestrictType(QualType T) const {
    return T.withFastQualifiers(Qualifiers::Restrict);
  }

  /// Return the uniqued reference to the type for a \c volatile
  /// qualified type.
  ///
  /// The resulting type has a union of the qualifiers from \p T and
  /// \c volatile.
  QualType getVolatileType(QualType T) const {
    return T.withFastQualifiers(Qualifiers::Volatile);
  }

  /// Return the uniqued reference to the type for a \c const
  /// qualified type.
  ///
  /// The resulting type has a union of the qualifiers from \p T and \c const.
  ///
  /// It can be reasonably expected that this will always be equivalent to
  /// calling T.withConst().
  QualType getConstType(QualType T) const { return T.withConst(); }

  /// Change the ExtInfo on a function type.
  const FunctionType *adjustFunctionType(const FunctionType *Fn,
                                         FunctionType::ExtInfo EInfo);

  /// Adjust the given function result type.
  CanQualType getCanonicalFunctionResultType(QualType ResultType) const;

  /// Change the result type of a function type once it is deduced.
  void adjustDeducedFunctionResultType(FunctionDecl *FD, QualType ResultType);

  /// Get a function type and produce the equivalent function type with the
  /// specified exception specification. Type sugar that can be present on a
  /// declaration of a function with an exception specification is permitted
  /// and preserved. Other type sugar (for instance, typedefs) is not.
  QualType getFunctionTypeWithExceptionSpec(
      QualType Orig, const FunctionProtoType::ExceptionSpecInfo &ESI) const;

  /// Determine whether two function types are the same, ignoring
  /// exception specifications in cases where they're part of the type.
  bool hasSameFunctionTypeIgnoringExceptionSpec(QualType T, QualType U) const;

  /// Change the exception specification on a function once it is
  /// delay-parsed, instantiated, or computed.
  void adjustExceptionSpec(FunctionDecl *FD,
                           const FunctionProtoType::ExceptionSpecInfo &ESI,
                           bool AsWritten = false);

  /// Get a function type and produce the equivalent function type where
  /// pointer size address spaces in the return type and parameter tyeps are
  /// replaced with the default address space.
  QualType getFunctionTypeWithoutPtrSizes(QualType T);

  /// Determine whether two function types are the same, ignoring pointer sizes
  /// in the return type and parameter types.
  bool hasSameFunctionTypeIgnoringPtrSizes(QualType T, QualType U);

  /// Return the uniqued reference to the type for a complex
  /// number with the specified element type.
  QualType getComplexType(QualType T) const;
  CanQualType getComplexType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getComplexType((QualType) T));
  }

  /// Return the uniqued reference to the type for a pointer to
  /// the specified type.
  QualType getPointerType(QualType T) const;
  CanQualType getPointerType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getPointerType((QualType) T));
  }

  QualType
  getCountAttributedType(QualType T, Expr *CountExpr, bool CountInBytes,
                         bool OrNull,
                         ArrayRef<TypeCoupledDeclRefInfo> DependentDecls) const;

  /// Return the uniqued reference to a type adjusted from the original
  /// type to a new type.
  QualType getAdjustedType(QualType Orig, QualType New) const;
  CanQualType getAdjustedType(CanQualType Orig, CanQualType New) const {
    return CanQualType::CreateUnsafe(
        getAdjustedType((QualType)Orig, (QualType)New));
  }

  /// Return the uniqued reference to the decayed version of the given
  /// type.  Can only be called on array and function types which decay to
  /// pointer types.
  QualType getDecayedType(QualType T) const;
  CanQualType getDecayedType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getDecayedType((QualType) T));
  }
  /// Return the uniqued reference to a specified decay from the original
  /// type to the decayed type.
  QualType getDecayedType(QualType Orig, QualType Decayed) const;

  /// Return the uniqued reference to a specified array parameter type from the
  /// original array type.
  QualType getArrayParameterType(QualType Ty) const;

  /// Return the uniqued reference to the atomic type for the specified
  /// type.
  QualType getAtomicType(QualType T) const;

  /// Return the uniqued reference to the type for a block of the
  /// specified type.
  QualType getBlockPointerType(QualType T) const;

  /// Gets the struct used to keep track of the descriptor for pointer to
  /// blocks.
  QualType getBlockDescriptorType() const;

  /// Return a read_only pipe type for the specified type.
  QualType getReadPipeType(QualType T) const;

  /// Return a write_only pipe type for the specified type.
  QualType getWritePipeType(QualType T) const;

  /// Return a bit-precise integer type with the specified signedness and bit
  /// count.
  QualType getBitIntType(bool Unsigned, unsigned NumBits) const;

  /// Return a dependent bit-precise integer type with the specified signedness
  /// and bit count.
  QualType getDependentBitIntType(bool Unsigned, Expr *BitsExpr) const;

  /// Gets the struct used to keep track of the extended descriptor for
  /// pointer to blocks.
  QualType getBlockDescriptorExtendedType() const;

  /// Map an AST Type to an OpenCLTypeKind enum value.
  OpenCLTypeKind getOpenCLTypeKind(const Type *T) const;

  /// Get address space for OpenCL type.
  LangAS getOpenCLTypeAddrSpace(const Type *T) const;

  /// Returns default address space based on OpenCL version and enabled features
  inline LangAS getDefaultOpenCLPointeeAddrSpace() {
    return LangOpts.OpenCLGenericAddressSpace ? LangAS::opencl_generic
                                              : LangAS::opencl_private;
  }

  void setcudaConfigureCallDecl(FunctionDecl *FD) {
    cudaConfigureCallDecl = FD;
  }

  FunctionDecl *getcudaConfigureCallDecl() {
    return cudaConfigureCallDecl;
  }

  /// Returns true iff we need copy/dispose helpers for the given type.
  bool BlockRequiresCopying(QualType Ty, const VarDecl *D);

  /// Returns true, if given type has a known lifetime. HasByrefExtendedLayout
  /// is set to false in this case. If HasByrefExtendedLayout returns true,
  /// byref variable has extended lifetime.
  bool getByrefLifetime(QualType Ty,
                        Qualifiers::ObjCLifetime &Lifetime,
                        bool &HasByrefExtendedLayout) const;

  /// Return the uniqued reference to the type for an lvalue reference
  /// to the specified type.
  QualType getLValueReferenceType(QualType T, bool SpelledAsLValue = true)
    const;

  /// Return the uniqued reference to the type for an rvalue reference
  /// to the specified type.
  QualType getRValueReferenceType(QualType T) const;

  /// Return the uniqued reference to the type for a member pointer to
  /// the specified type in the specified class.
  ///
  /// The class \p Cls is a \c Type because it could be a dependent name.
  QualType getMemberPointerType(QualType T, const Type *Cls) const;

  /// Return a non-unique reference to the type for a variable array of
  /// the specified element type.
  QualType getVariableArrayType(QualType EltTy, Expr *NumElts,
                                ArraySizeModifier ASM, unsigned IndexTypeQuals,
                                SourceRange Brackets) const;

  /// Return a non-unique reference to the type for a dependently-sized
  /// array of the specified element type.
  ///
  /// FIXME: We will need these to be uniqued, or at least comparable, at some
  /// point.
  QualType getDependentSizedArrayType(QualType EltTy, Expr *NumElts,
                                      ArraySizeModifier ASM,
                                      unsigned IndexTypeQuals,
                                      SourceRange Brackets) const;

  /// Return a unique reference to the type for an incomplete array of
  /// the specified element type.
  QualType getIncompleteArrayType(QualType EltTy, ArraySizeModifier ASM,
                                  unsigned IndexTypeQuals) const;

  /// Return the unique reference to the type for a constant array of
  /// the specified element type.
  QualType getConstantArrayType(QualType EltTy, const llvm::APInt &ArySize,
                                const Expr *SizeExpr, ArraySizeModifier ASM,
                                unsigned IndexTypeQuals) const;

  /// Return a type for a constant array for a string literal of the
  /// specified element type and length.
  QualType getStringLiteralArrayType(QualType EltTy, unsigned Length) const;

  /// Returns a vla type where known sizes are replaced with [*].
  QualType getVariableArrayDecayedType(QualType Ty) const;

  // Convenience struct to return information about a builtin vector type.
  struct BuiltinVectorTypeInfo {
    QualType ElementType;
    llvm::ElementCount EC;
    unsigned NumVectors;
    BuiltinVectorTypeInfo(QualType ElementType, llvm::ElementCount EC,
                          unsigned NumVectors)
        : ElementType(ElementType), EC(EC), NumVectors(NumVectors) {}
  };

  /// Returns the element type, element count and number of vectors
  /// (in case of tuple) for a builtin vector type.
  BuiltinVectorTypeInfo
  getBuiltinVectorTypeInfo(const BuiltinType *VecTy) const;

  /// Return the unique reference to a scalable vector type of the specified
  /// element type and scalable number of elements.
  /// For RISC-V, number of fields is also provided when it fetching for
  /// tuple type.
  ///
  /// \pre \p EltTy must be a built-in type.
  QualType getScalableVectorType(QualType EltTy, unsigned NumElts,
                                 unsigned NumFields = 1) const;

  /// Return a WebAssembly externref type.
  QualType getWebAssemblyExternrefType() const;

  /// Return the unique reference to a vector type of the specified
  /// element type and size.
  ///
  /// \pre \p VectorType must be a built-in type.
  QualType getVectorType(QualType VectorType, unsigned NumElts,
                         VectorKind VecKind) const;
  /// Return the unique reference to the type for a dependently sized vector of
  /// the specified element type.
  QualType getDependentVectorType(QualType VectorType, Expr *SizeExpr,
                                  SourceLocation AttrLoc,
                                  VectorKind VecKind) const;

  /// Return the unique reference to an extended vector type
  /// of the specified element type and size.
  ///
  /// \pre \p VectorType must be a built-in type.
  QualType getExtVectorType(QualType VectorType, unsigned NumElts) const;

  /// \pre Return a non-unique reference to the type for a dependently-sized
  /// vector of the specified element type.
  ///
  /// FIXME: We will need these to be uniqued, or at least comparable, at some
  /// point.
  QualType getDependentSizedExtVectorType(QualType VectorType,
                                          Expr *SizeExpr,
                                          SourceLocation AttrLoc) const;

  /// Return the unique reference to the matrix type of the specified element
  /// type and size
  ///
  /// \pre \p ElementType must be a valid matrix element type (see
  /// MatrixType::isValidElementType).
  QualType getConstantMatrixType(QualType ElementType, unsigned NumRows,
                                 unsigned NumColumns) const;

  /// Return the unique reference to the matrix type of the specified element
  /// type and size
  QualType getDependentSizedMatrixType(QualType ElementType, Expr *RowExpr,
                                       Expr *ColumnExpr,
                                       SourceLocation AttrLoc) const;

  QualType getDependentAddressSpaceType(QualType PointeeType,
                                        Expr *AddrSpaceExpr,
                                        SourceLocation AttrLoc) const;

  /// Return a K&R style C function type like 'int()'.
  QualType getFunctionNoProtoType(QualType ResultTy,
                                  const FunctionType::ExtInfo &Info) const;

  QualType getFunctionNoProtoType(QualType ResultTy) const {
    return getFunctionNoProtoType(ResultTy, FunctionType::ExtInfo());
  }

  /// Return a normal function type with a typed argument list.
  QualType getFunctionType(QualType ResultTy, ArrayRef<QualType> Args,
                           const FunctionProtoType::ExtProtoInfo &EPI) const {
    return getFunctionTypeInternal(ResultTy, Args, EPI, false);
  }

  QualType adjustStringLiteralBaseType(QualType StrLTy) const;

private:
  /// Return a normal function type with a typed argument list.
  QualType getFunctionTypeInternal(QualType ResultTy, ArrayRef<QualType> Args,
                                   const FunctionProtoType::ExtProtoInfo &EPI,
                                   bool OnlyWantCanonical) const;
  QualType
  getAutoTypeInternal(QualType DeducedType, AutoTypeKeyword Keyword,
                      bool IsDependent, bool IsPack = false,
                      ConceptDecl *TypeConstraintConcept = nullptr,
                      ArrayRef<TemplateArgument> TypeConstraintArgs = {},
                      bool IsCanon = false) const;

public:
  /// Return the unique reference to the type for the specified type
  /// declaration.
  QualType getTypeDeclType(const TypeDecl *Decl,
                           const TypeDecl *PrevDecl = nullptr) const {
    assert(Decl && "Passed null for Decl param");
    if (Decl->TypeForDecl) return QualType(Decl->TypeForDecl, 0);

    if (PrevDecl) {
      assert(PrevDecl->TypeForDecl && "previous decl has no TypeForDecl");
      Decl->TypeForDecl = PrevDecl->TypeForDecl;
      return QualType(PrevDecl->TypeForDecl, 0);
    }

    return getTypeDeclTypeSlow(Decl);
  }

  QualType getUsingType(const UsingShadowDecl *Found,
                        QualType Underlying) const;

  /// Return the unique reference to the type for the specified
  /// typedef-name decl.
  QualType getTypedefType(const TypedefNameDecl *Decl,
                          QualType Underlying = QualType()) const;

  QualType getRecordType(const RecordDecl *Decl) const;

  QualType getEnumType(const EnumDecl *Decl) const;

  QualType
  getUnresolvedUsingType(const UnresolvedUsingTypenameDecl *Decl) const;

  QualType getInjectedClassNameType(CXXRecordDecl *Decl, QualType TST) const;

  QualType getAttributedType(attr::Kind attrKind, QualType modifiedType,
                             QualType equivalentType) const;

  QualType getBTFTagAttributedType(const BTFTypeTagAttr *BTFAttr,
                                   QualType Wrapped);

  QualType
  getSubstTemplateTypeParmType(QualType Replacement, Decl *AssociatedDecl,
                               unsigned Index,
                               std::optional<unsigned> PackIndex) const;
  QualType getSubstTemplateTypeParmPackType(Decl *AssociatedDecl,
                                            unsigned Index, bool Final,
                                            const TemplateArgument &ArgPack);

  QualType
  getTemplateTypeParmType(unsigned Depth, unsigned Index,
                          bool ParameterPack,
                          TemplateTypeParmDecl *ParmDecl = nullptr) const;

  QualType getTemplateSpecializationType(TemplateName T,
                                         ArrayRef<TemplateArgument> Args,
                                         QualType Canon = QualType()) const;

  QualType
  getCanonicalTemplateSpecializationType(TemplateName T,
                                         ArrayRef<TemplateArgument> Args) const;

  QualType getTemplateSpecializationType(TemplateName T,
                                         ArrayRef<TemplateArgumentLoc> Args,
                                         QualType Canon = QualType()) const;

  TypeSourceInfo *
  getTemplateSpecializationTypeInfo(TemplateName T, SourceLocation TLoc,
                                    const TemplateArgumentListInfo &Args,
                                    QualType Canon = QualType()) const;

  QualType getParenType(QualType NamedType) const;

  QualType getMacroQualifiedType(QualType UnderlyingTy,
                                 const IdentifierInfo *MacroII) const;

  QualType getElaboratedType(ElaboratedTypeKeyword Keyword,
                             NestedNameSpecifier *NNS, QualType NamedType,
                             TagDecl *OwnedTagDecl = nullptr) const;
  QualType getDependentNameType(ElaboratedTypeKeyword Keyword,
                                NestedNameSpecifier *NNS,
                                const IdentifierInfo *Name,
                                QualType Canon = QualType()) const;

  QualType getDependentTemplateSpecializationType(
      ElaboratedTypeKeyword Keyword, NestedNameSpecifier *NNS,
      const IdentifierInfo *Name, ArrayRef<TemplateArgumentLoc> Args) const;
  QualType getDependentTemplateSpecializationType(
      ElaboratedTypeKeyword Keyword, NestedNameSpecifier *NNS,
      const IdentifierInfo *Name, ArrayRef<TemplateArgument> Args) const;

  TemplateArgument getInjectedTemplateArg(NamedDecl *ParamDecl);

  /// Get a template argument list with one argument per template parameter
  /// in a template parameter list, such as for the injected class name of
  /// a class template.
  void getInjectedTemplateArgs(const TemplateParameterList *Params,
                               SmallVectorImpl<TemplateArgument> &Args);

  /// Form a pack expansion type with the given pattern.
  /// \param NumExpansions The number of expansions for the pack, if known.
  /// \param ExpectPackInType If \c false, we should not expect \p Pattern to
  ///        contain an unexpanded pack. This only makes sense if the pack
  ///        expansion is used in a context where the arity is inferred from
  ///        elsewhere, such as if the pattern contains a placeholder type or
  ///        if this is the canonical type of another pack expansion type.
  QualType getPackExpansionType(QualType Pattern,
                                std::optional<unsigned> NumExpansions,
                                bool ExpectPackInType = true);

  QualType getObjCInterfaceType(const ObjCInterfaceDecl *Decl,
                                ObjCInterfaceDecl *PrevDecl = nullptr) const;

  /// Legacy interface: cannot provide type arguments or __kindof.
  QualType getObjCObjectType(QualType Base,
                             ObjCProtocolDecl * const *Protocols,
                             unsigned NumProtocols) const;

  QualType getObjCObjectType(QualType Base,
                             ArrayRef<QualType> typeArgs,
                             ArrayRef<ObjCProtocolDecl *> protocols,
                             bool isKindOf) const;

  QualType getObjCTypeParamType(const ObjCTypeParamDecl *Decl,
                                ArrayRef<ObjCProtocolDecl *> protocols) const;
  void adjustObjCTypeParamBoundType(const ObjCTypeParamDecl *Orig,
                                    ObjCTypeParamDecl *New) const;

  bool ObjCObjectAdoptsQTypeProtocols(QualType QT, ObjCInterfaceDecl *Decl);

  /// QIdProtocolsAdoptObjCObjectProtocols - Checks that protocols in
  /// QT's qualified-id protocol list adopt all protocols in IDecl's list
  /// of protocols.
  bool QIdProtocolsAdoptObjCObjectProtocols(QualType QT,
                                            ObjCInterfaceDecl *IDecl);

  /// Return a ObjCObjectPointerType type for the given ObjCObjectType.
  QualType getObjCObjectPointerType(QualType OIT) const;

  /// C23 feature and GCC extension.
  QualType getTypeOfExprType(Expr *E, TypeOfKind Kind) const;
  QualType getTypeOfType(QualType QT, TypeOfKind Kind) const;

  QualType getReferenceQualifiedType(const Expr *e) const;

  /// C++11 decltype.
  QualType getDecltypeType(Expr *e, QualType UnderlyingType) const;

  QualType getPackIndexingType(QualType Pattern, Expr *IndexExpr,
                               bool FullySubstituted = false,
                               ArrayRef<QualType> Expansions = {},
                               int Index = -1) const;

  /// Unary type transforms
  QualType getUnaryTransformType(QualType BaseType, QualType UnderlyingType,
                                 UnaryTransformType::UTTKind UKind) const;

  /// C++11 deduced auto type.
  QualType getAutoType(QualType DeducedType, AutoTypeKeyword Keyword,
                       bool IsDependent, bool IsPack = false,
                       ConceptDecl *TypeConstraintConcept = nullptr,
                       ArrayRef<TemplateArgument> TypeConstraintArgs ={}) const;

  /// C++11 deduction pattern for 'auto' type.
  QualType getAutoDeductType() const;

  /// C++11 deduction pattern for 'auto &&' type.
  QualType getAutoRRefDeductType() const;

  /// Remove any type constraints from a template parameter type, for
  /// equivalence comparison of template parameters.
  QualType getUnconstrainedType(QualType T) const;

  /// C++17 deduced class template specialization type.
  QualType getDeducedTemplateSpecializationType(TemplateName Template,
                                                QualType DeducedType,
                                                bool IsDependent) const;

  /// Return the unique reference to the type for the specified TagDecl
  /// (struct/union/class/enum) decl.
  QualType getTagDeclType(const TagDecl *Decl) const;

  /// Return the unique type for "size_t" (C99 7.17), defined in
  /// <stddef.h>.
  ///
  /// The sizeof operator requires this (C99 6.5.3.4p4).
  CanQualType getSizeType() const;

  /// Return the unique signed counterpart of
  /// the integer type corresponding to size_t.
  CanQualType getSignedSizeType() const;

  /// Return the unique type for "intmax_t" (C99 7.18.1.5), defined in
  /// <stdint.h>.
  CanQualType getIntMaxType() const;

  /// Return the unique type for "uintmax_t" (C99 7.18.1.5), defined in
  /// <stdint.h>.
  CanQualType getUIntMaxType() const;

  /// Return the unique wchar_t type available in C++ (and available as
  /// __wchar_t as a Microsoft extension).
  QualType getWCharType() const { return WCharTy; }

  /// Return the type of wide characters. In C++, this returns the
  /// unique wchar_t type. In C99, this returns a type compatible with the type
  /// defined in <stddef.h> as defined by the target.
  QualType getWideCharType() const { return WideCharTy; }

  /// Return the type of "signed wchar_t".
  ///
  /// Used when in C++, as a GCC extension.
  QualType getSignedWCharType() const;

  /// Return the type of "unsigned wchar_t".
  ///
  /// Used when in C++, as a GCC extension.
  QualType getUnsignedWCharType() const;

  /// In C99, this returns a type compatible with the type
  /// defined in <stddef.h> as defined by the target.
  QualType getWIntType() const { return WIntTy; }

  /// Return a type compatible with "intptr_t" (C99 7.18.1.4),
  /// as defined by the target.
  QualType getIntPtrType() const;

  /// Return a type compatible with "uintptr_t" (C99 7.18.1.4),
  /// as defined by the target.
  QualType getUIntPtrType() const;

  /// Return the unique type for "ptrdiff_t" (C99 7.17) defined in
  /// <stddef.h>. Pointer - pointer requires this (C99 6.5.6p9).
  QualType getPointerDiffType() const;

  /// Return the unique unsigned counterpart of "ptrdiff_t"
  /// integer type. The standard (C11 7.21.6.1p7) refers to this type
  /// in the definition of %tu format specifier.
  QualType getUnsignedPointerDiffType() const;

  /// Return the unique type for "pid_t" defined in
  /// <sys/types.h>. We need this to compute the correct type for vfork().
  QualType getProcessIDType() const;

  /// Return the C structure type used to represent constant CFStrings.
  QualType getCFConstantStringType() const;

  /// Returns the C struct type for objc_super
  QualType getObjCSuperType() const;
  void setObjCSuperType(QualType ST) { ObjCSuperType = ST; }

  /// Get the structure type used to representation CFStrings, or NULL
  /// if it hasn't yet been built.
  QualType getRawCFConstantStringType() const {
    if (CFConstantStringTypeDecl)
      return getTypedefType(CFConstantStringTypeDecl);
    return QualType();
  }
  void setCFConstantStringType(QualType T);
  TypedefDecl *getCFConstantStringDecl() const;
  RecordDecl *getCFConstantStringTagDecl() const;

  // This setter/getter represents the ObjC type for an NSConstantString.
  void setObjCConstantStringInterface(ObjCInterfaceDecl *Decl);
  QualType getObjCConstantStringInterface() const {
    return ObjCConstantStringType;
  }

  QualType getObjCNSStringType() const {
    return ObjCNSStringType;
  }

  void setObjCNSStringType(QualType T) {
    ObjCNSStringType = T;
  }

  /// Retrieve the type that \c id has been defined to, which may be
  /// different from the built-in \c id if \c id has been typedef'd.
  QualType getObjCIdRedefinitionType() const {
    if (ObjCIdRedefinitionType.isNull())
      return getObjCIdType();
    return ObjCIdRedefinitionType;
  }

  /// Set the user-written type that redefines \c id.
  void setObjCIdRedefinitionType(QualType RedefType) {
    ObjCIdRedefinitionType = RedefType;
  }

  /// Retrieve the type that \c Class has been defined to, which may be
  /// different from the built-in \c Class if \c Class has been typedef'd.
  QualType getObjCClassRedefinitionType() const {
    if (ObjCClassRedefinitionType.isNull())
      return getObjCClassType();
    return ObjCClassRedefinitionType;
  }

  /// Set the user-written type that redefines 'SEL'.
  void setObjCClassRedefinitionType(QualType RedefType) {
    ObjCClassRedefinitionType = RedefType;
  }

  /// Retrieve the type that 'SEL' has been defined to, which may be
  /// different from the built-in 'SEL' if 'SEL' has been typedef'd.
  QualType getObjCSelRedefinitionType() const {
    if (ObjCSelRedefinitionType.isNull())
      return getObjCSelType();
    return ObjCSelRedefinitionType;
  }

  /// Set the user-written type that redefines 'SEL'.
  void setObjCSelRedefinitionType(QualType RedefType) {
    ObjCSelRedefinitionType = RedefType;
  }

  /// Retrieve the identifier 'NSObject'.
  IdentifierInfo *getNSObjectName() const {
    if (!NSObjectName) {
      NSObjectName = &Idents.get("NSObject");
    }

    return NSObjectName;
  }

  /// Retrieve the identifier 'NSCopying'.
  IdentifierInfo *getNSCopyingName() {
    if (!NSCopyingName) {
      NSCopyingName = &Idents.get("NSCopying");
    }

    return NSCopyingName;
  }

  CanQualType getNSUIntegerType() const;

  CanQualType getNSIntegerType() const;

  /// Retrieve the identifier 'bool'.
  IdentifierInfo *getBoolName() const {
    if (!BoolName)
      BoolName = &Idents.get("bool");
    return BoolName;
  }

  IdentifierInfo *getMakeIntegerSeqName() const {
    if (!MakeIntegerSeqName)
      MakeIntegerSeqName = &Idents.get("__make_integer_seq");
    return MakeIntegerSeqName;
  }

  IdentifierInfo *getTypePackElementName() const {
    if (!TypePackElementName)
      TypePackElementName = &Idents.get("__type_pack_element");
    return TypePackElementName;
  }

  /// Retrieve the Objective-C "instancetype" type, if already known;
  /// otherwise, returns a NULL type;
  QualType getObjCInstanceType() {
    return getTypeDeclType(getObjCInstanceTypeDecl());
  }

  /// Retrieve the typedef declaration corresponding to the Objective-C
  /// "instancetype" type.
  TypedefDecl *getObjCInstanceTypeDecl();

  /// Set the type for the C FILE type.
  void setFILEDecl(TypeDecl *FILEDecl) { this->FILEDecl = FILEDecl; }

  /// Retrieve the C FILE type.
  QualType getFILEType() const {
    if (FILEDecl)
      return getTypeDeclType(FILEDecl);
    return QualType();
  }

  /// Set the type for the C jmp_buf type.
  void setjmp_bufDecl(TypeDecl *jmp_bufDecl) {
    this->jmp_bufDecl = jmp_bufDecl;
  }

  /// Retrieve the C jmp_buf type.
  QualType getjmp_bufType() const {
    if (jmp_bufDecl)
      return getTypeDeclType(jmp_bufDecl);
    return QualType();
  }

  /// Set the type for the C sigjmp_buf type.
  void setsigjmp_bufDecl(TypeDecl *sigjmp_bufDecl) {
    this->sigjmp_bufDecl = sigjmp_bufDecl;
  }

  /// Retrieve the C sigjmp_buf type.
  QualType getsigjmp_bufType() const {
    if (sigjmp_bufDecl)
      return getTypeDeclType(sigjmp_bufDecl);
    return QualType();
  }

  /// Set the type for the C ucontext_t type.
  void setucontext_tDecl(TypeDecl *ucontext_tDecl) {
    this->ucontext_tDecl = ucontext_tDecl;
  }

  /// Retrieve the C ucontext_t type.
  QualType getucontext_tType() const {
    if (ucontext_tDecl)
      return getTypeDeclType(ucontext_tDecl);
    return QualType();
  }

  /// The result type of logical operations, '<', '>', '!=', etc.
  QualType getLogicalOperationType() const {
    return getLangOpts().CPlusPlus ? BoolTy : IntTy;
  }

  /// Emit the Objective-CC type encoding for the given type \p T into
  /// \p S.
  ///
  /// If \p Field is specified then record field names are also encoded.
  void getObjCEncodingForType(QualType T, std::string &S,
                              const FieldDecl *Field=nullptr,
                              QualType *NotEncodedT=nullptr) const;

  /// Emit the Objective-C property type encoding for the given
  /// type \p T into \p S.
  void getObjCEncodingForPropertyType(QualType T, std::string &S) const;

  void getLegacyIntegralTypeEncoding(QualType &t) const;

  /// Put the string version of the type qualifiers \p QT into \p S.
  void getObjCEncodingForTypeQualifier(Decl::ObjCDeclQualifier QT,
                                       std::string &S) const;

  /// Emit the encoded type for the function \p Decl into \p S.
  ///
  /// This is in the same format as Objective-C method encodings.
  ///
  /// \returns true if an error occurred (e.g., because one of the parameter
  /// types is incomplete), false otherwise.
  std::string getObjCEncodingForFunctionDecl(const FunctionDecl *Decl) const;

  /// Emit the encoded type for the method declaration \p Decl into
  /// \p S.
  std::string getObjCEncodingForMethodDecl(const ObjCMethodDecl *Decl,
                                           bool Extended = false) const;

  /// Return the encoded type for this block declaration.
  std::string getObjCEncodingForBlock(const BlockExpr *blockExpr) const;

  /// getObjCEncodingForPropertyDecl - Return the encoded type for
  /// this method declaration. If non-NULL, Container must be either
  /// an ObjCCategoryImplDecl or ObjCImplementationDecl; it should
  /// only be NULL when getting encodings for protocol properties.
  std::string getObjCEncodingForPropertyDecl(const ObjCPropertyDecl *PD,
                                             const Decl *Container) const;

  bool ProtocolCompatibleWithProtocol(ObjCProtocolDecl *lProto,
                                      ObjCProtocolDecl *rProto) const;

  ObjCPropertyImplDecl *getObjCPropertyImplDeclForPropertyDecl(
                                                  const ObjCPropertyDecl *PD,
                                                  const Decl *Container) const;

  /// Return the size of type \p T for Objective-C encoding purpose,
  /// in characters.
  CharUnits getObjCEncodingTypeSize(QualType T) const;

  /// Retrieve the typedef corresponding to the predefined \c id type
  /// in Objective-C.
  TypedefDecl *getObjCIdDecl() const;

  /// Represents the Objective-CC \c id type.
  ///
  /// This is set up lazily, by Sema.  \c id is always a (typedef for a)
  /// pointer type, a pointer to a struct.
  QualType getObjCIdType() const {
    return getTypeDeclType(getObjCIdDecl());
  }

  /// Retrieve the typedef corresponding to the predefined 'SEL' type
  /// in Objective-C.
  TypedefDecl *getObjCSelDecl() const;

  /// Retrieve the type that corresponds to the predefined Objective-C
  /// 'SEL' type.
  QualType getObjCSelType() const {
    return getTypeDeclType(getObjCSelDecl());
  }

  /// Retrieve the typedef declaration corresponding to the predefined
  /// Objective-C 'Class' type.
  TypedefDecl *getObjCClassDecl() const;

  /// Represents the Objective-C \c Class type.
  ///
  /// This is set up lazily, by Sema.  \c Class is always a (typedef for a)
  /// pointer type, a pointer to a struct.
  QualType getObjCClassType() const {
    return getTypeDeclType(getObjCClassDecl());
  }

  /// Retrieve the Objective-C class declaration corresponding to
  /// the predefined \c Protocol class.
  ObjCInterfaceDecl *getObjCProtocolDecl() const;

  /// Retrieve declaration of 'BOOL' typedef
  TypedefDecl *getBOOLDecl() const {
    return BOOLDecl;
  }

  /// Save declaration of 'BOOL' typedef
  void setBOOLDecl(TypedefDecl *TD) {
    BOOLDecl = TD;
  }

  /// type of 'BOOL' type.
  QualType getBOOLType() const {
    return getTypeDeclType(getBOOLDecl());
  }

  /// Retrieve the type of the Objective-C \c Protocol class.
  QualType getObjCProtoType() const {
    return getObjCInterfaceType(getObjCProtocolDecl());
  }

  /// Retrieve the C type declaration corresponding to the predefined
  /// \c __builtin_va_list type.
  TypedefDecl *getBuiltinVaListDecl() const;

  /// Retrieve the type of the \c __builtin_va_list type.
  QualType getBuiltinVaListType() const {
    return getTypeDeclType(getBuiltinVaListDecl());
  }

  /// Retrieve the C type declaration corresponding to the predefined
  /// \c __va_list_tag type used to help define the \c __builtin_va_list type
  /// for some targets.
  Decl *getVaListTagDecl() const;

  /// Retrieve the C type declaration corresponding to the predefined
  /// \c __builtin_ms_va_list type.
  TypedefDecl *getBuiltinMSVaListDecl() const;

  /// Retrieve the type of the \c __builtin_ms_va_list type.
  QualType getBuiltinMSVaListType() const {
    return getTypeDeclType(getBuiltinMSVaListDecl());
  }

  /// Retrieve the implicitly-predeclared 'struct _GUID' declaration.
  TagDecl *getMSGuidTagDecl() const { return MSGuidTagDecl; }

  /// Retrieve the implicitly-predeclared 'struct _GUID' type.
  QualType getMSGuidType() const {
    assert(MSGuidTagDecl && "asked for GUID type but MS extensions disabled");
    return getTagDeclType(MSGuidTagDecl);
  }

  /// Return whether a declaration to a builtin is allowed to be
  /// overloaded/redeclared.
  bool canBuiltinBeRedeclared(const FunctionDecl *) const;

  /// Return a type with additional \c const, \c volatile, or
  /// \c restrict qualifiers.
  QualType getCVRQualifiedType(QualType T, unsigned CVR) const {
    return getQualifiedType(T, Qualifiers::fromCVRMask(CVR));
  }

  /// Un-split a SplitQualType.
  QualType getQualifiedType(SplitQualType split) const {
    return getQualifiedType(split.Ty, split.Quals);
  }

  /// Return a type with additional qualifiers.
  QualType getQualifiedType(QualType T, Qualifiers Qs) const {
    if (!Qs.hasNonFastQualifiers())
      return T.withFastQualifiers(Qs.getFastQualifiers());
    QualifierCollector Qc(Qs);
    const Type *Ptr = Qc.strip(T);
    return getExtQualType(Ptr, Qc);
  }

  /// Return a type with additional qualifiers.
  QualType getQualifiedType(const Type *T, Qualifiers Qs) const {
    if (!Qs.hasNonFastQualifiers())
      return QualType(T, Qs.getFastQualifiers());
    return getExtQualType(T, Qs);
  }

  /// Return a type with the given lifetime qualifier.
  ///
  /// \pre Neither type.ObjCLifetime() nor \p lifetime may be \c OCL_None.
  QualType getLifetimeQualifiedType(QualType type,
                                    Qualifiers::ObjCLifetime lifetime) {
    assert(type.getObjCLifetime() == Qualifiers::OCL_None);
    assert(lifetime != Qualifiers::OCL_None);

    Qualifiers qs;
    qs.addObjCLifetime(lifetime);
    return getQualifiedType(type, qs);
  }

  /// getUnqualifiedObjCPointerType - Returns version of
  /// Objective-C pointer type with lifetime qualifier removed.
  QualType getUnqualifiedObjCPointerType(QualType type) const {
    if (!type.getTypePtr()->isObjCObjectPointerType() ||
        !type.getQualifiers().hasObjCLifetime())
      return type;
    Qualifiers Qs = type.getQualifiers();
    Qs.removeObjCLifetime();
    return getQualifiedType(type.getUnqualifiedType(), Qs);
  }

  /// \brief Return a type with the given __ptrauth qualifier.
  QualType getPointerAuthType(QualType Ty, PointerAuthQualifier PointerAuth) {
    assert(!Ty.getPointerAuth());
    assert(PointerAuth);

    Qualifiers Qs;
    Qs.setPointerAuth(PointerAuth);
    return getQualifiedType(Ty, Qs);
  }

  unsigned char getFixedPointScale(QualType Ty) const;
  unsigned char getFixedPointIBits(QualType Ty) const;
  llvm::FixedPointSemantics getFixedPointSemantics(QualType Ty) const;
  llvm::APFixedPoint getFixedPointMax(QualType Ty) const;
  llvm::APFixedPoint getFixedPointMin(QualType Ty) const;

  DeclarationNameInfo getNameForTemplate(TemplateName Name,
                                         SourceLocation NameLoc) const;

  TemplateName getOverloadedTemplateName(UnresolvedSetIterator Begin,
                                         UnresolvedSetIterator End) const;
  TemplateName getAssumedTemplateName(DeclarationName Name) const;

  TemplateName getQualifiedTemplateName(NestedNameSpecifier *NNS,
                                        bool TemplateKeyword,
                                        TemplateName Template) const;

  TemplateName getDependentTemplateName(NestedNameSpecifier *NNS,
                                        const IdentifierInfo *Name) const;
  TemplateName getDependentTemplateName(NestedNameSpecifier *NNS,
                                        OverloadedOperatorKind Operator) const;
  TemplateName
  getSubstTemplateTemplateParm(TemplateName replacement, Decl *AssociatedDecl,
                               unsigned Index,
                               std::optional<unsigned> PackIndex) const;
  TemplateName getSubstTemplateTemplateParmPack(const TemplateArgument &ArgPack,
                                                Decl *AssociatedDecl,
                                                unsigned Index,
                                                bool Final) const;

  enum GetBuiltinTypeError {
    /// No error
    GE_None,

    /// Missing a type
    GE_Missing_type,

    /// Missing a type from <stdio.h>
    GE_Missing_stdio,

    /// Missing a type from <setjmp.h>
    GE_Missing_setjmp,

    /// Missing a type from <ucontext.h>
    GE_Missing_ucontext
  };

  QualType DecodeTypeStr(const char *&Str, const ASTContext &Context,
                         ASTContext::GetBuiltinTypeError &Error,
                         bool &RequireICE, bool AllowTypeModifiers) const;

  /// Return the type for the specified builtin.
  ///
  /// If \p IntegerConstantArgs is non-null, it is filled in with a bitmask of
  /// arguments to the builtin that are required to be integer constant
  /// expressions.
  QualType GetBuiltinType(unsigned ID, GetBuiltinTypeError &Error,
                          unsigned *IntegerConstantArgs = nullptr) const;

  /// Types and expressions required to build C++2a three-way comparisons
  /// using operator<=>, including the values return by builtin <=> operators.
  ComparisonCategories CompCategories;

private:
  CanQualType getFromTargetType(unsigned Type) const;
  TypeInfo getTypeInfoImpl(const Type *T) const;

  //===--------------------------------------------------------------------===//
  //                         Type Predicates.
  //===--------------------------------------------------------------------===//

public:
  /// Return one of the GCNone, Weak or Strong Objective-C garbage
  /// collection attributes.
  Qualifiers::GC getObjCGCAttrKind(QualType Ty) const;

  /// Return true if the given vector types are of the same unqualified
  /// type or if they are equivalent to the same GCC vector type.
  ///
  /// \note This ignores whether they are target-specific (AltiVec or Neon)
  /// types.
  bool areCompatibleVectorTypes(QualType FirstVec, QualType SecondVec);

  /// Return true if the given types are an SVE builtin and a VectorType that
  /// is a fixed-length representation of the SVE builtin for a specific
  /// vector-length.
  bool areCompatibleSveTypes(QualType FirstType, QualType SecondType);

  /// Return true if the given vector types are lax-compatible SVE vector types,
  /// false otherwise.
  bool areLaxCompatibleSveTypes(QualType FirstType, QualType SecondType);

  /// Return true if the given types are an RISC-V vector builtin type and a
  /// VectorType that is a fixed-length representation of the RISC-V vector
  /// builtin type for a specific vector-length.
  bool areCompatibleRVVTypes(QualType FirstType, QualType SecondType);

  /// Return true if the given vector types are lax-compatible RISC-V vector
  /// types as defined by -flax-vector-conversions=, which permits implicit
  /// conversions between vectors with different number of elements and/or
  /// incompatible element types, false otherwise.
  bool areLaxCompatibleRVVTypes(QualType FirstType, QualType SecondType);

  /// Return true if the type has been explicitly qualified with ObjC ownership.
  /// A type may be implicitly qualified with ownership under ObjC ARC, and in
  /// some cases the compiler treats these differently.
  bool hasDirectOwnershipQualifier(QualType Ty) const;

  /// Return true if this is an \c NSObject object with its \c NSObject
  /// attribute set.
  static bool isObjCNSObjectType(QualType Ty) {
    return Ty->isObjCNSObjectType();
  }

  //===--------------------------------------------------------------------===//
  //                         Type Sizing and Analysis
  //===--------------------------------------------------------------------===//

  /// Return the APFloat 'semantics' for the specified scalar floating
  /// point type.
  const llvm::fltSemantics &getFloatTypeSemantics(QualType T) const;

  /// Get the size and alignment of the specified complete type in bits.
  TypeInfo getTypeInfo(const Type *T) const;
  TypeInfo getTypeInfo(QualType T) const { return getTypeInfo(T.getTypePtr()); }

  /// Get default simd alignment of the specified complete type in bits.
  unsigned getOpenMPDefaultSimdAlign(QualType T) const;

  /// Return the size of the specified (complete) type \p T, in bits.
  uint64_t getTypeSize(QualType T) const { return getTypeInfo(T).Width; }
  uint64_t getTypeSize(const Type *T) const { return getTypeInfo(T).Width; }

  /// Return the size of the character type, in bits.
  uint64_t getCharWidth() const {
    return getTypeSize(CharTy);
  }

  /// Convert a size in bits to a size in characters.
  CharUnits toCharUnitsFromBits(int64_t BitSize) const;

  /// Convert a size in characters to a size in bits.
  int64_t toBits(CharUnits CharSize) const;

  /// Return the size of the specified (complete) type \p T, in
  /// characters.
  CharUnits getTypeSizeInChars(QualType T) const;
  CharUnits getTypeSizeInChars(const Type *T) const;

  std::optional<CharUnits> getTypeSizeInCharsIfKnown(QualType Ty) const {
    if (Ty->isIncompleteType() || Ty->isDependentType())
      return std::nullopt;
    return getTypeSizeInChars(Ty);
  }

  std::optional<CharUnits> getTypeSizeInCharsIfKnown(const Type *Ty) const {
    return getTypeSizeInCharsIfKnown(QualType(Ty, 0));
  }

  /// Return the ABI-specified alignment of a (complete) type \p T, in
  /// bits.
  unsigned getTypeAlign(QualType T) const { return getTypeInfo(T).Align; }
  unsigned getTypeAlign(const Type *T) const { return getTypeInfo(T).Align; }

  /// Return the ABI-specified natural alignment of a (complete) type \p T,
  /// before alignment adjustments, in bits.
  ///
  /// This alignment is curently used only by ARM and AArch64 when passing
  /// arguments of a composite type.
  unsigned getTypeUnadjustedAlign(QualType T) const {
    return getTypeUnadjustedAlign(T.getTypePtr());
  }
  unsigned getTypeUnadjustedAlign(const Type *T) const;

  /// Return the alignment of a type, in bits, or 0 if
  /// the type is incomplete and we cannot determine the alignment (for
  /// example, from alignment attributes). The returned alignment is the
  /// Preferred alignment if NeedsPreferredAlignment is true, otherwise is the
  /// ABI alignment.
  unsigned getTypeAlignIfKnown(QualType T,
                               bool NeedsPreferredAlignment = false) const;

  /// Return the ABI-specified alignment of a (complete) type \p T, in
  /// characters.
  CharUnits getTypeAlignInChars(QualType T) const;
  CharUnits getTypeAlignInChars(const Type *T) const;

  /// Return the PreferredAlignment of a (complete) type \p T, in
  /// characters.
  CharUnits getPreferredTypeAlignInChars(QualType T) const {
    return toCharUnitsFromBits(getPreferredTypeAlign(T));
  }

  /// getTypeUnadjustedAlignInChars - Return the ABI-specified alignment of a type,
  /// in characters, before alignment adjustments. This method does not work on
  /// incomplete types.
  CharUnits getTypeUnadjustedAlignInChars(QualType T) const;
  CharUnits getTypeUnadjustedAlignInChars(const Type *T) const;

  // getTypeInfoDataSizeInChars - Return the size of a type, in chars. If the
  // type is a record, its data size is returned.
  TypeInfoChars getTypeInfoDataSizeInChars(QualType T) const;

  TypeInfoChars getTypeInfoInChars(const Type *T) const;
  TypeInfoChars getTypeInfoInChars(QualType T) const;

  /// Determine if the alignment the type has was required using an
  /// alignment attribute.
  bool isAlignmentRequired(const Type *T) const;
  bool isAlignmentRequired(QualType T) const;

  /// More type predicates useful for type checking/promotion
  bool isPromotableIntegerType(QualType T) const; // C99 6.3.1.1p2

  /// Return the "preferred" alignment of the specified type \p T for
  /// the current target, in bits.
  ///
  /// This can be different than the ABI alignment in cases where it is
  /// beneficial for performance or backwards compatibility preserving to
  /// overalign a data type. (Note: despite the name, the preferred alignment
  /// is ABI-impacting, and not an optimization.)
  unsigned getPreferredTypeAlign(QualType T) const {
    return getPreferredTypeAlign(T.getTypePtr());
  }
  unsigned getPreferredTypeAlign(const Type *T) const;

  /// Return the default alignment for __attribute__((aligned)) on
  /// this target, to be used if no alignment value is specified.
  unsigned getTargetDefaultAlignForAttributeAligned() const;

  /// Return the alignment in bits that should be given to a
  /// global variable with type \p T. If \p VD is non-null it will be
  /// considered specifically for the query.
  unsigned getAlignOfGlobalVar(QualType T, const VarDecl *VD) const;

  /// Return the alignment in characters that should be given to a
  /// global variable with type \p T. If \p VD is non-null it will be
  /// considered specifically for the query.
  CharUnits getAlignOfGlobalVarInChars(QualType T, const VarDecl *VD) const;

  /// Return the minimum alignement as specified by the target. If \p VD is
  /// non-null it may be used to identify external or weak variables.
  unsigned getMinGlobalAlignOfVar(uint64_t Size, const VarDecl *VD) const;

  /// Return a conservative estimate of the alignment of the specified
  /// decl \p D.
  ///
  /// \pre \p D must not be a bitfield type, as bitfields do not have a valid
  /// alignment.
  ///
  /// If \p ForAlignof, references are treated like their underlying type
  /// and  large arrays don't get any special treatment. If not \p ForAlignof
  /// it computes the value expected by CodeGen: references are treated like
  /// pointers and large arrays get extra alignment.
  CharUnits getDeclAlign(const Decl *D, bool ForAlignof = false) const;

  /// Return the alignment (in bytes) of the thrown exception object. This is
  /// only meaningful for targets that allocate C++ exceptions in a system
  /// runtime, such as those using the Itanium C++ ABI.
  CharUnits getExnObjectAlignment() const;

  /// Get or compute information about the layout of the specified
  /// record (struct/union/class) \p D, which indicates its size and field
  /// position information.
  const ASTRecordLayout &getASTRecordLayout(const RecordDecl *D) const;

  /// Get or compute information about the layout of the specified
  /// Objective-C interface.
  const ASTRecordLayout &getASTObjCInterfaceLayout(const ObjCInterfaceDecl *D)
    const;

  void DumpRecordLayout(const RecordDecl *RD, raw_ostream &OS,
                        bool Simple = false) const;

  /// Get or compute information about the layout of the specified
  /// Objective-C implementation.
  ///
  /// This may differ from the interface if synthesized ivars are present.
  const ASTRecordLayout &
  getASTObjCImplementationLayout(const ObjCImplementationDecl *D) const;

  /// Get our current best idea for the key function of the
  /// given record decl, or nullptr if there isn't one.
  ///
  /// The key function is, according to the Itanium C++ ABI section 5.2.3:
  ///   ...the first non-pure virtual function that is not inline at the
  ///   point of class definition.
  ///
  /// Other ABIs use the same idea.  However, the ARM C++ ABI ignores
  /// virtual functions that are defined 'inline', which means that
  /// the result of this computation can change.
  const CXXMethodDecl *getCurrentKeyFunction(const CXXRecordDecl *RD);

  /// Observe that the given method cannot be a key function.
  /// Checks the key-function cache for the method's class and clears it
  /// if matches the given declaration.
  ///
  /// This is used in ABIs where out-of-line definitions marked
  /// inline are not considered to be key functions.
  ///
  /// \param method should be the declaration from the class definition
  void setNonKeyFunction(const CXXMethodDecl *method);

  /// Loading virtual member pointers using the virtual inheritance model
  /// always results in an adjustment using the vbtable even if the index is
  /// zero.
  ///
  /// This is usually OK because the first slot in the vbtable points
  /// backwards to the top of the MDC.  However, the MDC might be reusing a
  /// vbptr from an nv-base.  In this case, the first slot in the vbtable
  /// points to the start of the nv-base which introduced the vbptr and *not*
  /// the MDC.  Modify the NonVirtualBaseAdjustment to account for this.
  CharUnits getOffsetOfBaseWithVBPtr(const CXXRecordDecl *RD) const;

  /// Get the offset of a FieldDecl or IndirectFieldDecl, in bits.
  uint64_t getFieldOffset(const ValueDecl *FD) const;

  /// Get the offset of an ObjCIvarDecl in bits.
  uint64_t lookupFieldBitOffset(const ObjCInterfaceDecl *OID,
                                const ObjCImplementationDecl *ID,
                                const ObjCIvarDecl *Ivar) const;

  /// Find the 'this' offset for the member path in a pointer-to-member
  /// APValue.
  CharUnits getMemberPointerPathAdjustment(const APValue &MP) const;

  bool isNearlyEmpty(const CXXRecordDecl *RD) const;

  VTableContextBase *getVTableContext();

  /// If \p T is null pointer, assume the target in ASTContext.
  MangleContext *createMangleContext(const TargetInfo *T = nullptr);

  /// Creates a device mangle context to correctly mangle lambdas in a mixed
  /// architecture compile by setting the lambda mangling number source to the
  /// DeviceLambdaManglingNumber. Currently this asserts that the TargetInfo
  /// (from the AuxTargetInfo) is a an itanium target.
  MangleContext *createDeviceMangleContext(const TargetInfo &T);

  void DeepCollectObjCIvars(const ObjCInterfaceDecl *OI, bool leafClass,
                            SmallVectorImpl<const ObjCIvarDecl*> &Ivars) const;

  unsigned CountNonClassIvars(const ObjCInterfaceDecl *OI) const;
  void CollectInheritedProtocols(const Decl *CDecl,
                          llvm::SmallPtrSet<ObjCProtocolDecl*, 8> &Protocols);

  /// Return true if the specified type has unique object representations
  /// according to (C++17 [meta.unary.prop]p9)
  bool
  hasUniqueObjectRepresentations(QualType Ty,
                                 bool CheckIfTriviallyCopyable = true) const;

  //===--------------------------------------------------------------------===//
  //                            Type Operators
  //===--------------------------------------------------------------------===//

  /// Return the canonical (structural) type corresponding to the
  /// specified potentially non-canonical type \p T.
  ///
  /// The non-canonical version of a type may have many "decorated" versions of
  /// types.  Decorators can include typedefs, 'typeof' operators, etc. The
  /// returned type is guaranteed to be free of any of these, allowing two
  /// canonical types to be compared for exact equality with a simple pointer
  /// comparison.
  CanQualType getCanonicalType(QualType T) const {
    return CanQualType::CreateUnsafe(T.getCanonicalType());
  }

  const Type *getCanonicalType(const Type *T) const {
    return T->getCanonicalTypeInternal().getTypePtr();
  }

  /// Return the canonical parameter type corresponding to the specific
  /// potentially non-canonical one.
  ///
  /// Qualifiers are stripped off, functions are turned into function
  /// pointers, and arrays decay one level into pointers.
  CanQualType getCanonicalParamType(QualType T) const;

  /// Determine whether the given types \p T1 and \p T2 are equivalent.
  bool hasSameType(QualType T1, QualType T2) const {
    return getCanonicalType(T1) == getCanonicalType(T2);
  }
  bool hasSameType(const Type *T1, const Type *T2) const {
    return getCanonicalType(T1) == getCanonicalType(T2);
  }

  /// Determine whether the given expressions \p X and \p Y are equivalent.
  bool hasSameExpr(const Expr *X, const Expr *Y) const;

  /// Return this type as a completely-unqualified array type,
  /// capturing the qualifiers in \p Quals.
  ///
  /// This will remove the minimal amount of sugaring from the types, similar
  /// to the behavior of QualType::getUnqualifiedType().
  ///
  /// \param T is the qualified type, which may be an ArrayType
  ///
  /// \param Quals will receive the full set of qualifiers that were
  /// applied to the array.
  ///
  /// \returns if this is an array type, the completely unqualified array type
  /// that corresponds to it. Otherwise, returns T.getUnqualifiedType().
  QualType getUnqualifiedArrayType(QualType T, Qualifiers &Quals) const;
  QualType getUnqualifiedArrayType(QualType T) const {
    Qualifiers Quals;
    return getUnqualifiedArrayType(T, Quals);
  }

  /// Determine whether the given types are equivalent after
  /// cvr-qualifiers have been removed.
  bool hasSameUnqualifiedType(QualType T1, QualType T2) const {
    return getCanonicalType(T1).getTypePtr() ==
           getCanonicalType(T2).getTypePtr();
  }

  bool hasSameNullabilityTypeQualifier(QualType SubT, QualType SuperT,
                                       bool IsParam) const {
    auto SubTnullability = SubT->getNullability();
    auto SuperTnullability = SuperT->getNullability();
    if (SubTnullability.has_value() == SuperTnullability.has_value()) {
      // Neither has nullability; return true
      if (!SubTnullability)
        return true;
      // Both have nullability qualifier.
      if (*SubTnullability == *SuperTnullability ||
          *SubTnullability == NullabilityKind::Unspecified ||
          *SuperTnullability == NullabilityKind::Unspecified)
        return true;

      if (IsParam) {
        // Ok for the superclass method parameter to be "nonnull" and the subclass
        // method parameter to be "nullable"
        return (*SuperTnullability == NullabilityKind::NonNull &&
                *SubTnullability == NullabilityKind::Nullable);
      }
      // For the return type, it's okay for the superclass method to specify
      // "nullable" and the subclass method specify "nonnull"
      return (*SuperTnullability == NullabilityKind::Nullable &&
              *SubTnullability == NullabilityKind::NonNull);
    }
    return true;
  }

  bool ObjCMethodsAreEqual(const ObjCMethodDecl *MethodDecl,
                           const ObjCMethodDecl *MethodImp);

  bool UnwrapSimilarTypes(QualType &T1, QualType &T2,
                          bool AllowPiMismatch = true);
  void UnwrapSimilarArrayTypes(QualType &T1, QualType &T2,
                               bool AllowPiMismatch = true);

  /// Determine if two types are similar, according to the C++ rules. That is,
  /// determine if they are the same other than qualifiers on the initial
  /// sequence of pointer / pointer-to-member / array (and in Clang, object
  /// pointer) types and their element types.
  ///
  /// Clang offers a number of qualifiers in addition to the C++ qualifiers;
  /// those qualifiers are also ignored in the 'similarity' check.
  bool hasSimilarType(QualType T1, QualType T2);

  /// Determine if two types are similar, ignoring only CVR qualifiers.
  bool hasCvrSimilarType(QualType T1, QualType T2);

  /// Retrieves the "canonical" nested name specifier for a
  /// given nested name specifier.
  ///
  /// The canonical nested name specifier is a nested name specifier
  /// that uniquely identifies a type or namespace within the type
  /// system. For example, given:
  ///
  /// \code
  /// namespace N {
  ///   struct S {
  ///     template<typename T> struct X { typename T* type; };
  ///   };
  /// }
  ///
  /// template<typename T> struct Y {
  ///   typename N::S::X<T>::type member;
  /// };
  /// \endcode
  ///
  /// Here, the nested-name-specifier for N::S::X<T>:: will be
  /// S::X<template-param-0-0>, since 'S' and 'X' are uniquely defined
  /// by declarations in the type system and the canonical type for
  /// the template type parameter 'T' is template-param-0-0.
  NestedNameSpecifier *
  getCanonicalNestedNameSpecifier(NestedNameSpecifier *NNS) const;

  /// Retrieves the default calling convention for the current target.
  CallingConv getDefaultCallingConvention(bool IsVariadic,
                                          bool IsCXXMethod,
                                          bool IsBuiltin = false) const;

  /// Retrieves the "canonical" template name that refers to a
  /// given template.
  ///
  /// The canonical template name is the simplest expression that can
  /// be used to refer to a given template. For most templates, this
  /// expression is just the template declaration itself. For example,
  /// the template std::vector can be referred to via a variety of
  /// names---std::vector, \::std::vector, vector (if vector is in
  /// scope), etc.---but all of these names map down to the same
  /// TemplateDecl, which is used to form the canonical template name.
  ///
  /// Dependent template names are more interesting. Here, the
  /// template name could be something like T::template apply or
  /// std::allocator<T>::template rebind, where the nested name
  /// specifier itself is dependent. In this case, the canonical
  /// template name uses the shortest form of the dependent
  /// nested-name-specifier, which itself contains all canonical
  /// types, values, and templates.
  TemplateName getCanonicalTemplateName(const TemplateName &Name) const;

  /// Determine whether the given template names refer to the same
  /// template.
  bool hasSameTemplateName(const TemplateName &X, const TemplateName &Y) const;

  /// Determine whether the two declarations refer to the same entity.
  bool isSameEntity(const NamedDecl *X, const NamedDecl *Y) const;

  /// Determine whether two template parameter lists are similar enough
  /// that they may be used in declarations of the same template.
  bool isSameTemplateParameterList(const TemplateParameterList *X,
                                   const TemplateParameterList *Y) const;

  /// Determine whether two template parameters are similar enough
  /// that they may be used in declarations of the same template.
  bool isSameTemplateParameter(const NamedDecl *X, const NamedDecl *Y) const;

  /// Determine whether two 'requires' expressions are similar enough that they
  /// may be used in re-declarations.
  ///
  /// Use of 'requires' isn't mandatory, works with constraints expressed in
  /// other ways too.
  bool isSameConstraintExpr(const Expr *XCE, const Expr *YCE) const;

  /// Determine whether two type contraint are similar enough that they could
  /// used in declarations of the same template.
  bool isSameTypeConstraint(const TypeConstraint *XTC,
                            const TypeConstraint *YTC) const;

  /// Determine whether two default template arguments are similar enough
  /// that they may be used in declarations of the same template.
  bool isSameDefaultTemplateArgument(const NamedDecl *X,
                                     const NamedDecl *Y) const;

  /// Retrieve the "canonical" template argument.
  ///
  /// The canonical template argument is the simplest template argument
  /// (which may be a type, value, expression, or declaration) that
  /// expresses the value of the argument.
  TemplateArgument getCanonicalTemplateArgument(const TemplateArgument &Arg)
    const;

  /// Type Query functions.  If the type is an instance of the specified class,
  /// return the Type pointer for the underlying maximally pretty type.  This
  /// is a member of ASTContext because this may need to do some amount of
  /// canonicalization, e.g. to move type qualifiers into the element type.
  const ArrayType *getAsArrayType(QualType T) const;
  const ConstantArrayType *getAsConstantArrayType(QualType T) const {
    return dyn_cast_or_null<ConstantArrayType>(getAsArrayType(T));
  }
  const VariableArrayType *getAsVariableArrayType(QualType T) const {
    return dyn_cast_or_null<VariableArrayType>(getAsArrayType(T));
  }
  const IncompleteArrayType *getAsIncompleteArrayType(QualType T) const {
    return dyn_cast_or_null<IncompleteArrayType>(getAsArrayType(T));
  }
  const DependentSizedArrayType *getAsDependentSizedArrayType(QualType T)
    const {
    return dyn_cast_or_null<DependentSizedArrayType>(getAsArrayType(T));
  }

  /// Return the innermost element type of an array type.
  ///
  /// For example, will return "int" for int[m][n]
  QualType getBaseElementType(const ArrayType *VAT) const;

  /// Return the innermost element type of a type (which needn't
  /// actually be an array type).
  QualType getBaseElementType(QualType QT) const;

  /// Return number of constant array elements.
  uint64_t getConstantArrayElementCount(const ConstantArrayType *CA) const;

  /// Return number of elements initialized in an ArrayInitLoopExpr.
  uint64_t
  getArrayInitLoopExprElementCount(const ArrayInitLoopExpr *AILE) const;

  /// Perform adjustment on the parameter type of a function.
  ///
  /// This routine adjusts the given parameter type @p T to the actual
  /// parameter type used by semantic analysis (C99 6.7.5.3p[7,8],
  /// C++ [dcl.fct]p3). The adjusted parameter type is returned.
  QualType getAdjustedParameterType(QualType T) const;

  /// Retrieve the parameter type as adjusted for use in the signature
  /// of a function, decaying array and function types and removing top-level
  /// cv-qualifiers.
  QualType getSignatureParameterType(QualType T) const;

  QualType getExceptionObjectType(QualType T) const;

  /// Return the properly qualified result of decaying the specified
  /// array type to a pointer.
  ///
  /// This operation is non-trivial when handling typedefs etc.  The canonical
  /// type of \p T must be an array type, this returns a pointer to a properly
  /// qualified element of the array.
  ///
  /// See C99 6.7.5.3p7 and C99 6.3.2.1p3.
  QualType getArrayDecayedType(QualType T) const;

  /// Return the type that \p PromotableType will promote to: C99
  /// 6.3.1.1p2, assuming that \p PromotableType is a promotable integer type.
  QualType getPromotedIntegerType(QualType PromotableType) const;

  /// Recurses in pointer/array types until it finds an Objective-C
  /// retainable type and returns its ownership.
  Qualifiers::ObjCLifetime getInnerObjCOwnership(QualType T) const;

  /// Whether this is a promotable bitfield reference according
  /// to C99 6.3.1.1p2, bullet 2 (and GCC extensions).
  ///
  /// \returns the type this bit-field will promote to, or NULL if no
  /// promotion occurs.
  QualType isPromotableBitField(Expr *E) const;

  /// Return the highest ranked integer type, see C99 6.3.1.8p1.
  ///
  /// If \p LHS > \p RHS, returns 1.  If \p LHS == \p RHS, returns 0.  If
  /// \p LHS < \p RHS, return -1.
  int getIntegerTypeOrder(QualType LHS, QualType RHS) const;

  /// Compare the rank of the two specified floating point types,
  /// ignoring the domain of the type (i.e. 'double' == '_Complex double').
  ///
  /// If \p LHS > \p RHS, returns 1.  If \p LHS == \p RHS, returns 0.  If
  /// \p LHS < \p RHS, return -1.
  int getFloatingTypeOrder(QualType LHS, QualType RHS) const;

  /// Compare the rank of two floating point types as above, but compare equal
  /// if both types have the same floating-point semantics on the target (i.e.
  /// long double and double on AArch64 will return 0).
  int getFloatingTypeSemanticOrder(QualType LHS, QualType RHS) const;

  unsigned getTargetAddressSpace(LangAS AS) const;

  LangAS getLangASForBuiltinAddressSpace(unsigned AS) const;

  /// Get target-dependent integer value for null pointer which is used for
  /// constant folding.
  uint64_t getTargetNullPointerValue(QualType QT) const;

  bool addressSpaceMapManglingFor(LangAS AS) const {
    return AddrSpaceMapMangling || isTargetAddressSpace(AS);
  }

  bool hasAnyFunctionEffects() const { return AnyFunctionEffects; }

  // Merges two exception specifications, such that the resulting
  // exception spec is the union of both. For example, if either
  // of them can throw something, the result can throw it as well.
  FunctionProtoType::ExceptionSpecInfo
  mergeExceptionSpecs(FunctionProtoType::ExceptionSpecInfo ESI1,
                      FunctionProtoType::ExceptionSpecInfo ESI2,
                      SmallVectorImpl<QualType> &ExceptionTypeStorage,
                      bool AcceptDependent);

  // For two "same" types, return a type which has
  // the common sugar between them. If Unqualified is true,
  // both types need only be the same unqualified type.
  // The result will drop the qualifiers which do not occur
  // in both types.
  QualType getCommonSugaredType(QualType X, QualType Y,
                                bool Unqualified = false);

private:
  // Helper for integer ordering
  unsigned getIntegerRank(const Type *T) const;

public:
  //===--------------------------------------------------------------------===//
  //                    Type Compatibility Predicates
  //===--------------------------------------------------------------------===//

  /// Compatibility predicates used to check assignment expressions.
  bool typesAreCompatible(QualType T1, QualType T2,
                          bool CompareUnqualified = false); // C99 6.2.7p1

  bool propertyTypesAreCompatible(QualType, QualType);
  bool typesAreBlockPointerCompatible(QualType, QualType);

  bool isObjCIdType(QualType T) const {
    if (const auto *ET = dyn_cast<ElaboratedType>(T))
      T = ET->getNamedType();
    return T == getObjCIdType();
  }

  bool isObjCClassType(QualType T) const {
    if (const auto *ET = dyn_cast<ElaboratedType>(T))
      T = ET->getNamedType();
    return T == getObjCClassType();
  }

  bool isObjCSelType(QualType T) const {
    if (const auto *ET = dyn_cast<ElaboratedType>(T))
      T = ET->getNamedType();
    return T == getObjCSelType();
  }

  bool ObjCQualifiedIdTypesAreCompatible(const ObjCObjectPointerType *LHS,
                                         const ObjCObjectPointerType *RHS,
                                         bool ForCompare);

  bool ObjCQualifiedClassTypesAreCompatible(const ObjCObjectPointerType *LHS,
                                            const ObjCObjectPointerType *RHS);

  // Check the safety of assignment from LHS to RHS
  bool canAssignObjCInterfaces(const ObjCObjectPointerType *LHSOPT,
                               const ObjCObjectPointerType *RHSOPT);
  bool canAssignObjCInterfaces(const ObjCObjectType *LHS,
                               const ObjCObjectType *RHS);
  bool canAssignObjCInterfacesInBlockPointer(
                                          const ObjCObjectPointerType *LHSOPT,
                                          const ObjCObjectPointerType *RHSOPT,
                                          bool BlockReturnType);
  bool areComparableObjCPointerTypes(QualType LHS, QualType RHS);
  QualType areCommonBaseCompatible(const ObjCObjectPointerType *LHSOPT,
                                   const ObjCObjectPointerType *RHSOPT);
  bool canBindObjCObjectType(QualType To, QualType From);

  // Functions for calculating composite types
  QualType mergeTypes(QualType, QualType, bool OfBlockPointer = false,
                      bool Unqualified = false, bool BlockReturnType = false,
                      bool IsConditionalOperator = false);
  QualType mergeFunctionTypes(QualType, QualType, bool OfBlockPointer = false,
                              bool Unqualified = false, bool AllowCXX = false,
                              bool IsConditionalOperator = false);
  QualType mergeFunctionParameterTypes(QualType, QualType,
                                       bool OfBlockPointer = false,
                                       bool Unqualified = false);
  QualType mergeTransparentUnionType(QualType, QualType,
                                     bool OfBlockPointer=false,
                                     bool Unqualified = false);

  QualType mergeObjCGCQualifiers(QualType, QualType);

  /// This function merges the ExtParameterInfo lists of two functions. It
  /// returns true if the lists are compatible. The merged list is returned in
  /// NewParamInfos.
  ///
  /// \param FirstFnType The type of the first function.
  ///
  /// \param SecondFnType The type of the second function.
  ///
  /// \param CanUseFirst This flag is set to true if the first function's
  /// ExtParameterInfo list can be used as the composite list of
  /// ExtParameterInfo.
  ///
  /// \param CanUseSecond This flag is set to true if the second function's
  /// ExtParameterInfo list can be used as the composite list of
  /// ExtParameterInfo.
  ///
  /// \param NewParamInfos The composite list of ExtParameterInfo. The list is
  /// empty if none of the flags are set.
  ///
  bool mergeExtParameterInfo(
      const FunctionProtoType *FirstFnType,
      const FunctionProtoType *SecondFnType,
      bool &CanUseFirst, bool &CanUseSecond,
      SmallVectorImpl<FunctionProtoType::ExtParameterInfo> &NewParamInfos);

  void ResetObjCLayout(const ObjCContainerDecl *CD);

  //===--------------------------------------------------------------------===//
  //                    Integer Predicates
  //===--------------------------------------------------------------------===//

  // The width of an integer, as defined in C99 6.2.6.2. This is the number
  // of bits in an integer type excluding any padding bits.
  unsigned getIntWidth(QualType T) const;

  // Per C99 6.2.5p6, for every signed integer type, there is a corresponding
  // unsigned integer type.  This method takes a signed type, and returns the
  // corresponding unsigned integer type.
  // With the introduction of fixed point types in ISO N1169, this method also
  // accepts fixed point types and returns the corresponding unsigned type for
  // a given fixed point type.
  QualType getCorrespondingUnsignedType(QualType T) const;

  // Per C99 6.2.5p6, for every signed integer type, there is a corresponding
  // unsigned integer type.  This method takes an unsigned type, and returns the
  // corresponding signed integer type.
  // With the introduction of fixed point types in ISO N1169, this method also
  // accepts fixed point types and returns the corresponding signed type for
  // a given fixed point type.
  QualType getCorrespondingSignedType(QualType T) const;

  // Per ISO N1169, this method accepts fixed point types and returns the
  // corresponding saturated type for a given fixed point type.
  QualType getCorrespondingSaturatedType(QualType Ty) const;

  // Per ISO N1169, this method accepts fixed point types and returns the
  // corresponding non-saturated type for a given fixed point type.
  QualType getCorrespondingUnsaturatedType(QualType Ty) const;

  // This method accepts fixed point types and returns the corresponding signed
  // type. Unlike getCorrespondingUnsignedType(), this only accepts unsigned
  // fixed point types because there are unsigned integer types like bool and
  // char8_t that don't have signed equivalents.
  QualType getCorrespondingSignedFixedPointType(QualType Ty) const;

  //===--------------------------------------------------------------------===//
  //                    Integer Values
  //===--------------------------------------------------------------------===//

  /// Make an APSInt of the appropriate width and signedness for the
  /// given \p Value and integer \p Type.
  llvm::APSInt MakeIntValue(uint64_t Value, QualType Type) const {
    // If Type is a signed integer type larger than 64 bits, we need to be sure
    // to sign extend Res appropriately.
    llvm::APSInt Res(64, !Type->isSignedIntegerOrEnumerationType());
    Res = Value;
    unsigned Width = getIntWidth(Type);
    if (Width != Res.getBitWidth())
      return Res.extOrTrunc(Width);
    return Res;
  }

  bool isSentinelNullExpr(const Expr *E);

  /// Get the implementation of the ObjCInterfaceDecl \p D, or nullptr if
  /// none exists.
  ObjCImplementationDecl *getObjCImplementation(ObjCInterfaceDecl *D);

  /// Get the implementation of the ObjCCategoryDecl \p D, or nullptr if
  /// none exists.
  ObjCCategoryImplDecl *getObjCImplementation(ObjCCategoryDecl *D);

  /// Return true if there is at least one \@implementation in the TU.
  bool AnyObjCImplementation() {
    return !ObjCImpls.empty();
  }

  /// Set the implementation of ObjCInterfaceDecl.
  void setObjCImplementation(ObjCInterfaceDecl *IFaceD,
                             ObjCImplementationDecl *ImplD);

  /// Set the implementation of ObjCCategoryDecl.
  void setObjCImplementation(ObjCCategoryDecl *CatD,
                             ObjCCategoryImplDecl *ImplD);

  /// Get the duplicate declaration of a ObjCMethod in the same
  /// interface, or null if none exists.
  const ObjCMethodDecl *
  getObjCMethodRedeclaration(const ObjCMethodDecl *MD) const;

  void setObjCMethodRedeclaration(const ObjCMethodDecl *MD,
                                  const ObjCMethodDecl *Redecl);

  /// Returns the Objective-C interface that \p ND belongs to if it is
  /// an Objective-C method/property/ivar etc. that is part of an interface,
  /// otherwise returns null.
  const ObjCInterfaceDecl *getObjContainingInterface(const NamedDecl *ND) const;

  /// Set the copy initialization expression of a block var decl. \p CanThrow
  /// indicates whether the copy expression can throw or not.
  void setBlockVarCopyInit(const VarDecl* VD, Expr *CopyExpr, bool CanThrow);

  /// Get the copy initialization expression of the VarDecl \p VD, or
  /// nullptr if none exists.
  BlockVarCopyInit getBlockVarCopyInit(const VarDecl* VD) const;

  /// Allocate an uninitialized TypeSourceInfo.
  ///
  /// The caller should initialize the memory held by TypeSourceInfo using
  /// the TypeLoc wrappers.
  ///
  /// \param T the type that will be the basis for type source info. This type
  /// should refer to how the declarator was written in source code, not to
  /// what type semantic analysis resolved the declarator to.
  ///
  /// \param Size the size of the type info to create, or 0 if the size
  /// should be calculated based on the type.
  TypeSourceInfo *CreateTypeSourceInfo(QualType T, unsigned Size = 0) const;

  /// Allocate a TypeSourceInfo where all locations have been
  /// initialized to a given location, which defaults to the empty
  /// location.
  TypeSourceInfo *
  getTrivialTypeSourceInfo(QualType T,
                           SourceLocation Loc = SourceLocation()) const;

  /// Add a deallocation callback that will be invoked when the
  /// ASTContext is destroyed.
  ///
  /// \param Callback A callback function that will be invoked on destruction.
  ///
  /// \param Data Pointer data that will be provided to the callback function
  /// when it is called.
  void AddDeallocation(void (*Callback)(void *), void *Data) const;

  /// If T isn't trivially destructible, calls AddDeallocation to register it
  /// for destruction.
  template <typename T> void addDestruction(T *Ptr) const {
    if (!std::is_trivially_destructible<T>::value) {
      auto DestroyPtr = [](void *V) { static_cast<T *>(V)->~T(); };
      AddDeallocation(DestroyPtr, Ptr);
    }
  }

  GVALinkage GetGVALinkageForFunction(const FunctionDecl *FD) const;
  GVALinkage GetGVALinkageForVariable(const VarDecl *VD) const;

  /// Determines if the decl can be CodeGen'ed or deserialized from PCH
  /// lazily, only when used; this is only relevant for function or file scoped
  /// var definitions.
  ///
  /// \returns true if the function/var must be CodeGen'ed/deserialized even if
  /// it is not used.
  bool DeclMustBeEmitted(const Decl *D);

  /// Visits all versions of a multiversioned function with the passed
  /// predicate.
  void forEachMultiversionedFunctionVersion(
      const FunctionDecl *FD,
      llvm::function_ref<void(FunctionDecl *)> Pred) const;

  const CXXConstructorDecl *
  getCopyConstructorForExceptionObject(CXXRecordDecl *RD);

  void addCopyConstructorForExceptionObject(CXXRecordDecl *RD,
                                            CXXConstructorDecl *CD);

  void addTypedefNameForUnnamedTagDecl(TagDecl *TD, TypedefNameDecl *TND);

  TypedefNameDecl *getTypedefNameForUnnamedTagDecl(const TagDecl *TD);

  void addDeclaratorForUnnamedTagDecl(TagDecl *TD, DeclaratorDecl *DD);

  DeclaratorDecl *getDeclaratorForUnnamedTagDecl(const TagDecl *TD);

  void setManglingNumber(const NamedDecl *ND, unsigned Number);
  unsigned getManglingNumber(const NamedDecl *ND,
                             bool ForAuxTarget = false) const;

  void setStaticLocalNumber(const VarDecl *VD, unsigned Number);
  unsigned getStaticLocalNumber(const VarDecl *VD) const;

  /// Retrieve the context for computing mangling numbers in the given
  /// DeclContext.
  MangleNumberingContext &getManglingNumberContext(const DeclContext *DC);
  enum NeedExtraManglingDecl_t { NeedExtraManglingDecl };
  MangleNumberingContext &getManglingNumberContext(NeedExtraManglingDecl_t,
                                                   const Decl *D);

  std::unique_ptr<MangleNumberingContext> createMangleNumberingContext() const;

  /// Used by ParmVarDecl to store on the side the
  /// index of the parameter when it exceeds the size of the normal bitfield.
  void setParameterIndex(const ParmVarDecl *D, unsigned index);

  /// Used by ParmVarDecl to retrieve on the side the
  /// index of the parameter when it exceeds the size of the normal bitfield.
  unsigned getParameterIndex(const ParmVarDecl *D) const;

  /// Return a string representing the human readable name for the specified
  /// function declaration or file name. Used by SourceLocExpr and
  /// PredefinedExpr to cache evaluated results.
  StringLiteral *getPredefinedStringLiteralFromCache(StringRef Key) const;

  /// Return a declaration for the global GUID object representing the given
  /// GUID value.
  MSGuidDecl *getMSGuidDecl(MSGuidDeclParts Parts) const;

  /// Return a declaration for a uniquified anonymous global constant
  /// corresponding to a given APValue.
  UnnamedGlobalConstantDecl *
  getUnnamedGlobalConstantDecl(QualType Ty, const APValue &Value) const;

  /// Return the template parameter object of the given type with the given
  /// value.
  TemplateParamObjectDecl *getTemplateParamObjectDecl(QualType T,
                                                      const APValue &V) const;

  /// Parses the target attributes passed in, and returns only the ones that are
  /// valid feature names.
  ParsedTargetAttr filterFunctionTargetAttrs(const TargetAttr *TD) const;

  void getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                             const FunctionDecl *) const;
  void getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                             GlobalDecl GD) const;

  //===--------------------------------------------------------------------===//
  //                    Statistics
  //===--------------------------------------------------------------------===//

  /// The number of implicitly-declared default constructors.
  unsigned NumImplicitDefaultConstructors = 0;

  /// The number of implicitly-declared default constructors for
  /// which declarations were built.
  unsigned NumImplicitDefaultConstructorsDeclared = 0;

  /// The number of implicitly-declared copy constructors.
  unsigned NumImplicitCopyConstructors = 0;

  /// The number of implicitly-declared copy constructors for
  /// which declarations were built.
  unsigned NumImplicitCopyConstructorsDeclared = 0;

  /// The number of implicitly-declared move constructors.
  unsigned NumImplicitMoveConstructors = 0;

  /// The number of implicitly-declared move constructors for
  /// which declarations were built.
  unsigned NumImplicitMoveConstructorsDeclared = 0;

  /// The number of implicitly-declared copy assignment operators.
  unsigned NumImplicitCopyAssignmentOperators = 0;

  /// The number of implicitly-declared copy assignment operators for
  /// which declarations were built.
  unsigned NumImplicitCopyAssignmentOperatorsDeclared = 0;

  /// The number of implicitly-declared move assignment operators.
  unsigned NumImplicitMoveAssignmentOperators = 0;

  /// The number of implicitly-declared move assignment operators for
  /// which declarations were built.
  unsigned NumImplicitMoveAssignmentOperatorsDeclared = 0;

  /// The number of implicitly-declared destructors.
  unsigned NumImplicitDestructors = 0;

  /// The number of implicitly-declared destructors for which
  /// declarations were built.
  unsigned NumImplicitDestructorsDeclared = 0;

public:
  /// Initialize built-in types.
  ///
  /// This routine may only be invoked once for a given ASTContext object.
  /// It is normally invoked after ASTContext construction.
  ///
  /// \param Target The target
  void InitBuiltinTypes(const TargetInfo &Target,
                        const TargetInfo *AuxTarget = nullptr);

private:
  void InitBuiltinType(CanQualType &R, BuiltinType::Kind K);

  class ObjCEncOptions {
    unsigned Bits;

    ObjCEncOptions(unsigned Bits) : Bits(Bits) {}

  public:
    ObjCEncOptions() : Bits(0) {}

#define OPT_LIST(V)                                                            \
  V(ExpandPointedToStructures, 0)                                              \
  V(ExpandStructures, 1)                                                       \
  V(IsOutermostType, 2)                                                        \
  V(EncodingProperty, 3)                                                       \
  V(IsStructField, 4)                                                          \
  V(EncodeBlockParameters, 5)                                                  \
  V(EncodeClassNames, 6)                                                       \

#define V(N,I) ObjCEncOptions& set##N() { Bits |= 1 << I; return *this; }
OPT_LIST(V)
#undef V

#define V(N,I) bool N() const { return Bits & 1 << I; }
OPT_LIST(V)
#undef V

#undef OPT_LIST

    [[nodiscard]] ObjCEncOptions keepingOnly(ObjCEncOptions Mask) const {
      return Bits & Mask.Bits;
    }

    [[nodiscard]] ObjCEncOptions forComponentType() const {
      ObjCEncOptions Mask = ObjCEncOptions()
                                .setIsOutermostType()
                                .setIsStructField();
      return Bits & ~Mask.Bits;
    }
  };

  // Return the Objective-C type encoding for a given type.
  void getObjCEncodingForTypeImpl(QualType t, std::string &S,
                                  ObjCEncOptions Options,
                                  const FieldDecl *Field,
                                  QualType *NotEncodedT = nullptr) const;

  // Adds the encoding of the structure's members.
  void getObjCEncodingForStructureImpl(RecordDecl *RD, std::string &S,
                                       const FieldDecl *Field,
                                       bool includeVBases = true,
                                       QualType *NotEncodedT=nullptr) const;

public:
  // Adds the encoding of a method parameter or return type.
  void getObjCEncodingForMethodParameter(Decl::ObjCDeclQualifier QT,
                                         QualType T, std::string& S,
                                         bool Extended) const;

  /// Returns true if this is an inline-initialized static data member
  /// which is treated as a definition for MSVC compatibility.
  bool isMSStaticDataMemberInlineDefinition(const VarDecl *VD) const;

  enum class InlineVariableDefinitionKind {
    /// Not an inline variable.
    None,

    /// Weak definition of inline variable.
    Weak,

    /// Weak for now, might become strong later in this TU.
    WeakUnknown,

    /// Strong definition.
    Strong
  };

  /// Determine whether a definition of this inline variable should
  /// be treated as a weak or strong definition. For compatibility with
  /// C++14 and before, for a constexpr static data member, if there is an
  /// out-of-line declaration of the member, we may promote it from weak to
  /// strong.
  InlineVariableDefinitionKind
  getInlineVariableDefinitionKind(const VarDecl *VD) const;

private:
  friend class DeclarationNameTable;
  friend class DeclContext;

  const ASTRecordLayout &
  getObjCLayout(const ObjCInterfaceDecl *D,
                const ObjCImplementationDecl *Impl) const;

  /// A set of deallocations that should be performed when the
  /// ASTContext is destroyed.
  // FIXME: We really should have a better mechanism in the ASTContext to
  // manage running destructors for types which do variable sized allocation
  // within the AST. In some places we thread the AST bump pointer allocator
  // into the datastructures which avoids this mess during deallocation but is
  // wasteful of memory, and here we require a lot of error prone book keeping
  // in order to track and run destructors while we're tearing things down.
  using DeallocationFunctionsAndArguments =
      llvm::SmallVector<std::pair<void (*)(void *), void *>, 16>;
  mutable DeallocationFunctionsAndArguments Deallocations;

  // FIXME: This currently contains the set of StoredDeclMaps used
  // by DeclContext objects.  This probably should not be in ASTContext,
  // but we include it here so that ASTContext can quickly deallocate them.
  llvm::PointerIntPair<StoredDeclsMap *, 1> LastSDM;

  std::vector<Decl *> TraversalScope;

  std::unique_ptr<VTableContextBase> VTContext;

  void ReleaseDeclContextMaps();

public:
  enum PragmaSectionFlag : unsigned {
    PSF_None = 0,
    PSF_Read = 0x1,
    PSF_Write = 0x2,
    PSF_Execute = 0x4,
    PSF_Implicit = 0x8,
    PSF_ZeroInit = 0x10,
    PSF_Invalid = 0x80000000U,
  };

  struct SectionInfo {
    NamedDecl *Decl;
    SourceLocation PragmaSectionLocation;
    int SectionFlags;

    SectionInfo() = default;
    SectionInfo(NamedDecl *Decl, SourceLocation PragmaSectionLocation,
                int SectionFlags)
        : Decl(Decl), PragmaSectionLocation(PragmaSectionLocation),
          SectionFlags(SectionFlags) {}
  };

  llvm::StringMap<SectionInfo> SectionInfos;

  /// Return a new OMPTraitInfo object owned by this context.
  OMPTraitInfo &getNewOMPTraitInfo();

  /// Whether a C++ static variable or CUDA/HIP kernel may be externalized.
  bool mayExternalize(const Decl *D) const;

  /// Whether a C++ static variable or CUDA/HIP kernel should be externalized.
  bool shouldExternalize(const Decl *D) const;

  /// Resolve the root record to be used to derive the vtable pointer
  /// authentication policy for the specified record.
  const CXXRecordDecl *
  baseForVTableAuthentication(const CXXRecordDecl *ThisClass);
  bool useAbbreviatedThunkName(GlobalDecl VirtualMethodDecl,
                               StringRef MangledName);

  StringRef getCUIDHash() const;

private:
  /// All OMPTraitInfo objects live in this collection, one per
  /// `pragma omp [begin] declare variant` directive.
  SmallVector<std::unique_ptr<OMPTraitInfo>, 4> OMPTraitInfoVector;

  llvm::DenseMap<GlobalDecl, llvm::StringSet<>> ThunksToBeAbbreviated;
};

/// Insertion operator for diagnostics.
const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                      const ASTContext::SectionInfo &Section);

/// Utility function for constructing a nullary selector.
inline Selector GetNullarySelector(StringRef name, ASTContext &Ctx) {
  const IdentifierInfo *II = &Ctx.Idents.get(name);
  return Ctx.Selectors.getSelector(0, &II);
}

/// Utility function for constructing an unary selector.
inline Selector GetUnarySelector(StringRef name, ASTContext &Ctx) {
  const IdentifierInfo *II = &Ctx.Idents.get(name);
  return Ctx.Selectors.getSelector(1, &II);
}

} // namespace clang

// operator new and delete aren't allowed inside namespaces.

/// Placement new for using the ASTContext's allocator.
///
/// This placement form of operator new uses the ASTContext's allocator for
/// obtaining memory.
///
/// IMPORTANT: These are also declared in clang/AST/ASTContextAllocate.h!
/// Any changes here need to also be made there.
///
/// We intentionally avoid using a nothrow specification here so that the calls
/// to this operator will not perform a null check on the result -- the
/// underlying allocator never returns null pointers.
///
/// Usage looks like this (assuming there's an ASTContext 'Context' in scope):
/// @code
/// // Default alignment (8)
/// IntegerLiteral *Ex = new (Context) IntegerLiteral(arguments);
/// // Specific alignment
/// IntegerLiteral *Ex2 = new (Context, 4) IntegerLiteral(arguments);
/// @endcode
/// Memory allocated through this placement new operator does not need to be
/// explicitly freed, as ASTContext will free all of this memory when it gets
/// destroyed. Please note that you cannot use delete on the pointer.
///
/// @param Bytes The number of bytes to allocate. Calculated by the compiler.
/// @param C The ASTContext that provides the allocator.
/// @param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// @return The allocated memory. Could be nullptr.
inline void *operator new(size_t Bytes, const clang::ASTContext &C,
                          size_t Alignment /* = 8 */) {
  return C.Allocate(Bytes, Alignment);
}

/// Placement delete companion to the new above.
///
/// This operator is just a companion to the new above. There is no way of
/// invoking it directly; see the new operator for more details. This operator
/// is called implicitly by the compiler if a placement new expression using
/// the ASTContext throws in the object constructor.
inline void operator delete(void *Ptr, const clang::ASTContext &C, size_t) {
  C.Deallocate(Ptr);
}

/// This placement form of operator new[] uses the ASTContext's allocator for
/// obtaining memory.
///
/// We intentionally avoid using a nothrow specification here so that the calls
/// to this operator will not perform a null check on the result -- the
/// underlying allocator never returns null pointers.
///
/// Usage looks like this (assuming there's an ASTContext 'Context' in scope):
/// @code
/// // Default alignment (8)
/// char *data = new (Context) char[10];
/// // Specific alignment
/// char *data = new (Context, 4) char[10];
/// @endcode
/// Memory allocated through this placement new[] operator does not need to be
/// explicitly freed, as ASTContext will free all of this memory when it gets
/// destroyed. Please note that you cannot use delete on the pointer.
///
/// @param Bytes The number of bytes to allocate. Calculated by the compiler.
/// @param C The ASTContext that provides the allocator.
/// @param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// @return The allocated memory. Could be nullptr.
inline void *operator new[](size_t Bytes, const clang::ASTContext& C,
                            size_t Alignment /* = 8 */) {
  return C.Allocate(Bytes, Alignment);
}

/// Placement delete[] companion to the new[] above.
///
/// This operator is just a companion to the new[] above. There is no way of
/// invoking it directly; see the new[] operator for more details. This operator
/// is called implicitly by the compiler if a placement new[] expression using
/// the ASTContext throws in the object constructor.
inline void operator delete[](void *Ptr, const clang::ASTContext &C, size_t) {
  C.Deallocate(Ptr);
}

/// Create the representation of a LazyGenerationalUpdatePtr.
template <typename Owner, typename T,
          void (clang::ExternalASTSource::*Update)(Owner)>
typename clang::LazyGenerationalUpdatePtr<Owner, T, Update>::ValueType
    clang::LazyGenerationalUpdatePtr<Owner, T, Update>::makeValue(
        const clang::ASTContext &Ctx, T Value) {
  // Note, this is implemented here so that ExternalASTSource.h doesn't need to
  // include ASTContext.h. We explicitly instantiate it for all relevant types
  // in ASTContext.cpp.
  if (auto *Source = Ctx.getExternalSource())
    return new (Ctx) LazyData(Source, Value);
  return Value;
}

#endif // LLVM_CLANG_AST_ASTCONTEXT_H
