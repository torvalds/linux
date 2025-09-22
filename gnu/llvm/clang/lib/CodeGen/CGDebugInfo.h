//===--- CGDebugInfo.h - DebugInfo for LLVM CodeGen -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the source-level debug info generator for llvm translation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGDEBUGINFO_H
#define LLVM_CLANG_LIB_CODEGEN_CGDEBUGINFO_H

#include "CGBuilder.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/ASTSourceDescriptor.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Allocator.h"
#include <map>
#include <optional>
#include <string>

namespace llvm {
class MDNode;
}

namespace clang {
class ClassTemplateSpecializationDecl;
class GlobalDecl;
class Module;
class ModuleMap;
class ObjCInterfaceDecl;
class UsingDecl;
class VarDecl;
enum class DynamicInitKind : unsigned;

namespace CodeGen {
class CodeGenModule;
class CodeGenFunction;
class CGBlockInfo;

/// This class gathers all debug information during compilation and is
/// responsible for emitting to llvm globals or pass directly to the
/// backend.
class CGDebugInfo {
  friend class ApplyDebugLocation;
  friend class SaveAndRestoreLocation;
  CodeGenModule &CGM;
  const llvm::codegenoptions::DebugInfoKind DebugKind;
  bool DebugTypeExtRefs;
  llvm::DIBuilder DBuilder;
  llvm::DICompileUnit *TheCU = nullptr;
  ModuleMap *ClangModuleMap = nullptr;
  ASTSourceDescriptor PCHDescriptor;
  SourceLocation CurLoc;
  llvm::MDNode *CurInlinedAt = nullptr;
  llvm::DIType *VTablePtrType = nullptr;
  llvm::DIType *ClassTy = nullptr;
  llvm::DICompositeType *ObjTy = nullptr;
  llvm::DIType *SelTy = nullptr;
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix)                   \
  llvm::DIType *SingletonId = nullptr;
#include "clang/Basic/OpenCLImageTypes.def"
  llvm::DIType *OCLSamplerDITy = nullptr;
  llvm::DIType *OCLEventDITy = nullptr;
  llvm::DIType *OCLClkEventDITy = nullptr;
  llvm::DIType *OCLQueueDITy = nullptr;
  llvm::DIType *OCLNDRangeDITy = nullptr;
  llvm::DIType *OCLReserveIDDITy = nullptr;
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  llvm::DIType *Id##Ty = nullptr;
#include "clang/Basic/OpenCLExtensionTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) llvm::DIType *SingletonId = nullptr;
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) llvm::DIType *SingletonId = nullptr;
#include "clang/Basic/AMDGPUTypes.def"

  /// Cache of previously constructed Types.
  llvm::DenseMap<const void *, llvm::TrackingMDRef> TypeCache;

  /// Cache that maps VLA types to size expressions for that type,
  /// represented by instantiated Metadata nodes.
  llvm::SmallDenseMap<QualType, llvm::Metadata *> SizeExprCache;

  /// Callbacks to use when printing names and types.
  class PrintingCallbacks final : public clang::PrintingCallbacks {
    const CGDebugInfo &Self;

  public:
    PrintingCallbacks(const CGDebugInfo &Self) : Self(Self) {}
    std::string remapPath(StringRef Path) const override {
      return Self.remapDIPath(Path);
    }
  };
  PrintingCallbacks PrintCB = {*this};

  struct ObjCInterfaceCacheEntry {
    const ObjCInterfaceType *Type;
    llvm::DIType *Decl;
    llvm::DIFile *Unit;
    ObjCInterfaceCacheEntry(const ObjCInterfaceType *Type, llvm::DIType *Decl,
                            llvm::DIFile *Unit)
        : Type(Type), Decl(Decl), Unit(Unit) {}
  };

  /// Cache of previously constructed interfaces which may change.
  llvm::SmallVector<ObjCInterfaceCacheEntry, 32> ObjCInterfaceCache;

  /// Cache of forward declarations for methods belonging to the interface.
  /// The extra bit on the DISubprogram specifies whether a method is
  /// "objc_direct".
  llvm::DenseMap<const ObjCInterfaceDecl *,
                 std::vector<llvm::PointerIntPair<llvm::DISubprogram *, 1>>>
      ObjCMethodCache;

  /// Cache of references to clang modules and precompiled headers.
  llvm::DenseMap<const Module *, llvm::TrackingMDRef> ModuleCache;

  /// List of interfaces we want to keep even if orphaned.
  std::vector<void *> RetainedTypes;

  /// Cache of forward declared types to RAUW at the end of compilation.
  std::vector<std::pair<const TagType *, llvm::TrackingMDRef>> ReplaceMap;

  /// Cache of replaceable forward declarations (functions and
  /// variables) to RAUW at the end of compilation.
  std::vector<std::pair<const DeclaratorDecl *, llvm::TrackingMDRef>>
      FwdDeclReplaceMap;

  /// Keep track of our current nested lexical block.
  std::vector<llvm::TypedTrackingMDRef<llvm::DIScope>> LexicalBlockStack;
  llvm::DenseMap<const Decl *, llvm::TrackingMDRef> RegionMap;
  /// Keep track of LexicalBlockStack counter at the beginning of a
  /// function. This is used to pop unbalanced regions at the end of a
  /// function.
  std::vector<unsigned> FnBeginRegionCount;

  /// This is a storage for names that are constructed on demand. For
  /// example, C++ destructors, C++ operators etc..
  llvm::BumpPtrAllocator DebugInfoNames;
  StringRef CWDName;

  llvm::DenseMap<const char *, llvm::TrackingMDRef> DIFileCache;
  llvm::DenseMap<const FunctionDecl *, llvm::TrackingMDRef> SPCache;
  /// Cache declarations relevant to DW_TAG_imported_declarations (C++
  /// using declarations and global alias variables) that aren't covered
  /// by other more specific caches.
  llvm::DenseMap<const Decl *, llvm::TrackingMDRef> DeclCache;
  llvm::DenseMap<const Decl *, llvm::TrackingMDRef> ImportedDeclCache;
  llvm::DenseMap<const NamespaceDecl *, llvm::TrackingMDRef> NamespaceCache;
  llvm::DenseMap<const NamespaceAliasDecl *, llvm::TrackingMDRef>
      NamespaceAliasCache;
  llvm::DenseMap<const Decl *, llvm::TypedTrackingMDRef<llvm::DIDerivedType>>
      StaticDataMemberCache;

  using ParamDecl2StmtTy = llvm::DenseMap<const ParmVarDecl *, const Stmt *>;
  using Param2DILocTy =
      llvm::DenseMap<const ParmVarDecl *, llvm::DILocalVariable *>;

  /// The key is coroutine real parameters, value is coroutine move parameters.
  ParamDecl2StmtTy CoroutineParameterMappings;
  /// The key is coroutine real parameters, value is DIVariable in LLVM IR.
  Param2DILocTy ParamDbgMappings;

  /// Helper functions for getOrCreateType.
  /// @{
  /// Currently the checksum of an interface includes the number of
  /// ivars and property accessors.
  llvm::DIType *CreateType(const BuiltinType *Ty);
  llvm::DIType *CreateType(const ComplexType *Ty);
  llvm::DIType *CreateType(const BitIntType *Ty);
  llvm::DIType *CreateQualifiedType(QualType Ty, llvm::DIFile *Fg);
  llvm::DIType *CreateQualifiedType(const FunctionProtoType *Ty,
                                    llvm::DIFile *Fg);
  llvm::DIType *CreateType(const TypedefType *Ty, llvm::DIFile *Fg);
  llvm::DIType *CreateType(const TemplateSpecializationType *Ty,
                           llvm::DIFile *Fg);
  llvm::DIType *CreateType(const ObjCObjectPointerType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const PointerType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const BlockPointerType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const FunctionType *Ty, llvm::DIFile *F);
  /// Get structure or union type.
  llvm::DIType *CreateType(const RecordType *Tyg);

  /// Create definition for the specified 'Ty'.
  ///
  /// \returns A pair of 'llvm::DIType's. The first is the definition
  /// of the 'Ty'. The second is the type specified by the preferred_name
  /// attribute on 'Ty', which can be a nullptr if no such attribute
  /// exists.
  std::pair<llvm::DIType *, llvm::DIType *>
  CreateTypeDefinition(const RecordType *Ty);
  llvm::DICompositeType *CreateLimitedType(const RecordType *Ty);
  void CollectContainingType(const CXXRecordDecl *RD,
                             llvm::DICompositeType *CT);
  /// Get Objective-C interface type.
  llvm::DIType *CreateType(const ObjCInterfaceType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateTypeDefinition(const ObjCInterfaceType *Ty,
                                     llvm::DIFile *F);
  /// Get Objective-C object type.
  llvm::DIType *CreateType(const ObjCObjectType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const ObjCTypeParamType *Ty, llvm::DIFile *Unit);

  llvm::DIType *CreateType(const VectorType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const ConstantMatrixType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const ArrayType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const LValueReferenceType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const RValueReferenceType *Ty, llvm::DIFile *Unit);
  llvm::DIType *CreateType(const MemberPointerType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const AtomicType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const PipeType *Ty, llvm::DIFile *F);
  /// Get enumeration type.
  llvm::DIType *CreateEnumType(const EnumType *Ty);
  llvm::DIType *CreateTypeDefinition(const EnumType *Ty);
  /// Look up the completed type for a self pointer in the TypeCache and
  /// create a copy of it with the ObjectPointer and Artificial flags
  /// set. If the type is not cached, a new one is created. This should
  /// never happen though, since creating a type for the implicit self
  /// argument implies that we already parsed the interface definition
  /// and the ivar declarations in the implementation.
  llvm::DIType *CreateSelfType(const QualType &QualTy, llvm::DIType *Ty);
  /// @}

  /// Get the type from the cache or return null type if it doesn't
  /// exist.
  llvm::DIType *getTypeOrNull(const QualType);
  /// Return the debug type for a C++ method.
  /// \arg CXXMethodDecl is of FunctionType. This function type is
  /// not updated to include implicit \c this pointer. Use this routine
  /// to get a method type which includes \c this pointer.
  llvm::DISubroutineType *getOrCreateMethodType(const CXXMethodDecl *Method,
                                                llvm::DIFile *F);
  llvm::DISubroutineType *
  getOrCreateInstanceMethodType(QualType ThisPtr, const FunctionProtoType *Func,
                                llvm::DIFile *Unit);
  llvm::DISubroutineType *
  getOrCreateFunctionType(const Decl *D, QualType FnType, llvm::DIFile *F);
  /// \return debug info descriptor for vtable.
  llvm::DIType *getOrCreateVTablePtrType(llvm::DIFile *F);

  /// \return namespace descriptor for the given namespace decl.
  llvm::DINamespace *getOrCreateNamespace(const NamespaceDecl *N);
  llvm::DIType *CreatePointerLikeType(llvm::dwarf::Tag Tag, const Type *Ty,
                                      QualType PointeeTy, llvm::DIFile *F);
  llvm::DIType *getOrCreateStructPtrType(StringRef Name, llvm::DIType *&Cache);

  /// A helper function to create a subprogram for a single member
  /// function GlobalDecl.
  llvm::DISubprogram *CreateCXXMemberFunction(const CXXMethodDecl *Method,
                                              llvm::DIFile *F,
                                              llvm::DIType *RecordTy);

  /// A helper function to collect debug info for C++ member
  /// functions. This is used while creating debug info entry for a
  /// Record.
  void CollectCXXMemberFunctions(const CXXRecordDecl *Decl, llvm::DIFile *F,
                                 SmallVectorImpl<llvm::Metadata *> &E,
                                 llvm::DIType *T);

  /// A helper function to collect debug info for C++ base
  /// classes. This is used while creating debug info entry for a
  /// Record.
  void CollectCXXBases(const CXXRecordDecl *Decl, llvm::DIFile *F,
                       SmallVectorImpl<llvm::Metadata *> &EltTys,
                       llvm::DIType *RecordTy);

  /// Helper function for CollectCXXBases.
  /// Adds debug info entries for types in Bases that are not in SeenTypes.
  void CollectCXXBasesAux(
      const CXXRecordDecl *RD, llvm::DIFile *Unit,
      SmallVectorImpl<llvm::Metadata *> &EltTys, llvm::DIType *RecordTy,
      const CXXRecordDecl::base_class_const_range &Bases,
      llvm::DenseSet<CanonicalDeclPtr<const CXXRecordDecl>> &SeenTypes,
      llvm::DINode::DIFlags StartingFlags);

  /// Helper function that returns the llvm::DIType that the
  /// PreferredNameAttr attribute on \ref RD refers to. If no such
  /// attribute exists, returns nullptr.
  llvm::DIType *GetPreferredNameType(const CXXRecordDecl *RD,
                                     llvm::DIFile *Unit);

  struct TemplateArgs {
    const TemplateParameterList *TList;
    llvm::ArrayRef<TemplateArgument> Args;
  };
  /// A helper function to collect template parameters.
  llvm::DINodeArray CollectTemplateParams(std::optional<TemplateArgs> Args,
                                          llvm::DIFile *Unit);
  /// A helper function to collect debug info for function template
  /// parameters.
  llvm::DINodeArray CollectFunctionTemplateParams(const FunctionDecl *FD,
                                                  llvm::DIFile *Unit);

  /// A helper function to collect debug info for function template
  /// parameters.
  llvm::DINodeArray CollectVarTemplateParams(const VarDecl *VD,
                                             llvm::DIFile *Unit);

  std::optional<TemplateArgs> GetTemplateArgs(const VarDecl *) const;
  std::optional<TemplateArgs> GetTemplateArgs(const RecordDecl *) const;
  std::optional<TemplateArgs> GetTemplateArgs(const FunctionDecl *) const;

  /// A helper function to collect debug info for template
  /// parameters.
  llvm::DINodeArray CollectCXXTemplateParams(const RecordDecl *TS,
                                             llvm::DIFile *F);

  /// A helper function to collect debug info for btf_decl_tag annotations.
  llvm::DINodeArray CollectBTFDeclTagAnnotations(const Decl *D);

  llvm::DIType *createFieldType(StringRef name, QualType type,
                                SourceLocation loc, AccessSpecifier AS,
                                uint64_t offsetInBits, uint32_t AlignInBits,
                                llvm::DIFile *tunit, llvm::DIScope *scope,
                                const RecordDecl *RD = nullptr,
                                llvm::DINodeArray Annotations = nullptr);

  llvm::DIType *createFieldType(StringRef name, QualType type,
                                SourceLocation loc, AccessSpecifier AS,
                                uint64_t offsetInBits, llvm::DIFile *tunit,
                                llvm::DIScope *scope,
                                const RecordDecl *RD = nullptr) {
    return createFieldType(name, type, loc, AS, offsetInBits, 0, tunit, scope,
                           RD);
  }

  /// Create new bit field member.
  llvm::DIDerivedType *createBitFieldType(const FieldDecl *BitFieldDecl,
                                          llvm::DIScope *RecordTy,
                                          const RecordDecl *RD);

  /// Create an anonnymous zero-size separator for bit-field-decl if needed on
  /// the target.
  llvm::DIDerivedType *createBitFieldSeparatorIfNeeded(
      const FieldDecl *BitFieldDecl, const llvm::DIDerivedType *BitFieldDI,
      llvm::ArrayRef<llvm::Metadata *> PreviousFieldsDI, const RecordDecl *RD);

  /// A cache that maps names of artificial inlined functions to subprograms.
  llvm::StringMap<llvm::DISubprogram *> InlinedTrapFuncMap;

  /// A function that returns the subprogram corresponding to the artificial
  /// inlined function for traps.
  llvm::DISubprogram *createInlinedTrapSubprogram(StringRef FuncName,
                                                  llvm::DIFile *FileScope);

  /// Helpers for collecting fields of a record.
  /// @{
  void CollectRecordLambdaFields(const CXXRecordDecl *CXXDecl,
                                 SmallVectorImpl<llvm::Metadata *> &E,
                                 llvm::DIType *RecordTy);
  llvm::DIDerivedType *CreateRecordStaticField(const VarDecl *Var,
                                               llvm::DIType *RecordTy,
                                               const RecordDecl *RD);
  void CollectRecordNormalField(const FieldDecl *Field, uint64_t OffsetInBits,
                                llvm::DIFile *F,
                                SmallVectorImpl<llvm::Metadata *> &E,
                                llvm::DIType *RecordTy, const RecordDecl *RD);
  void CollectRecordNestedType(const TypeDecl *RD,
                               SmallVectorImpl<llvm::Metadata *> &E);
  void CollectRecordFields(const RecordDecl *Decl, llvm::DIFile *F,
                           SmallVectorImpl<llvm::Metadata *> &E,
                           llvm::DICompositeType *RecordTy);

  /// If the C++ class has vtable info then insert appropriate debug
  /// info entry in EltTys vector.
  void CollectVTableInfo(const CXXRecordDecl *Decl, llvm::DIFile *F,
                         SmallVectorImpl<llvm::Metadata *> &EltTys);
  /// @}

  /// Create a new lexical block node and push it on the stack.
  void CreateLexicalBlock(SourceLocation Loc);

  /// If target-specific LLVM \p AddressSpace directly maps to target-specific
  /// DWARF address space, appends extended dereferencing mechanism to complex
  /// expression \p Expr. Otherwise, does nothing.
  ///
  /// Extended dereferencing mechanism is has the following format:
  ///     DW_OP_constu <DWARF Address Space> DW_OP_swap DW_OP_xderef
  void AppendAddressSpaceXDeref(unsigned AddressSpace,
                                SmallVectorImpl<uint64_t> &Expr) const;

  /// A helper function to collect debug info for the default elements of a
  /// block.
  ///
  /// \returns The next available field offset after the default elements.
  uint64_t collectDefaultElementTypesForBlockPointer(
      const BlockPointerType *Ty, llvm::DIFile *Unit,
      llvm::DIDerivedType *DescTy, unsigned LineNo,
      SmallVectorImpl<llvm::Metadata *> &EltTys);

  /// A helper function to collect debug info for the default fields of a
  /// block.
  void collectDefaultFieldsForBlockLiteralDeclare(
      const CGBlockInfo &Block, const ASTContext &Context, SourceLocation Loc,
      const llvm::StructLayout &BlockLayout, llvm::DIFile *Unit,
      SmallVectorImpl<llvm::Metadata *> &Fields);

public:
  CGDebugInfo(CodeGenModule &CGM);
  ~CGDebugInfo();

  void finalize();

  /// Remap a given path with the current debug prefix map
  std::string remapDIPath(StringRef) const;

  /// Register VLA size expression debug node with the qualified type.
  void registerVLASizeExpression(QualType Ty, llvm::Metadata *SizeExpr) {
    SizeExprCache[Ty] = SizeExpr;
  }

  /// Module debugging: Support for building PCMs.
  /// @{
  /// Set the main CU's DwoId field to \p Signature.
  void setDwoId(uint64_t Signature);

  /// When generating debug information for a clang module or
  /// precompiled header, this module map will be used to determine
  /// the module of origin of each Decl.
  void setModuleMap(ModuleMap &MMap) { ClangModuleMap = &MMap; }

  /// When generating debug information for a clang module or
  /// precompiled header, this module map will be used to determine
  /// the module of origin of each Decl.
  void setPCHDescriptor(ASTSourceDescriptor PCH) { PCHDescriptor = PCH; }
  /// @}

  /// Update the current source location. If \arg loc is invalid it is
  /// ignored.
  void setLocation(SourceLocation Loc);

  /// Return the current source location. This does not necessarily correspond
  /// to the IRBuilder's current DebugLoc.
  SourceLocation getLocation() const { return CurLoc; }

  /// Update the current inline scope. All subsequent calls to \p EmitLocation
  /// will create a location with this inlinedAt field.
  void setInlinedAt(llvm::MDNode *InlinedAt) { CurInlinedAt = InlinedAt; }

  /// \return the current inline scope.
  llvm::MDNode *getInlinedAt() const { return CurInlinedAt; }

  // Converts a SourceLocation to a DebugLoc
  llvm::DebugLoc SourceLocToDebugLoc(SourceLocation Loc);

  /// Emit metadata to indicate a change in line/column information in
  /// the source file. If the location is invalid, the previous
  /// location will be reused.
  void EmitLocation(CGBuilderTy &Builder, SourceLocation Loc);

  QualType getFunctionType(const FunctionDecl *FD, QualType RetTy,
                           const SmallVectorImpl<const VarDecl *> &Args);

  /// Emit a call to llvm.dbg.function.start to indicate
  /// start of a new function.
  /// \param Loc       The location of the function header.
  /// \param ScopeLoc  The location of the function body.
  void emitFunctionStart(GlobalDecl GD, SourceLocation Loc,
                         SourceLocation ScopeLoc, QualType FnType,
                         llvm::Function *Fn, bool CurFnIsThunk);

  /// Start a new scope for an inlined function.
  void EmitInlineFunctionStart(CGBuilderTy &Builder, GlobalDecl GD);
  /// End an inlined function scope.
  void EmitInlineFunctionEnd(CGBuilderTy &Builder);

  /// Emit debug info for a function declaration.
  /// \p Fn is set only when a declaration for a debug call site gets created.
  void EmitFunctionDecl(GlobalDecl GD, SourceLocation Loc,
                        QualType FnType, llvm::Function *Fn = nullptr);

  /// Emit debug info for an extern function being called.
  /// This is needed for call site debug info.
  void EmitFuncDeclForCallSite(llvm::CallBase *CallOrInvoke,
                               QualType CalleeType,
                               const FunctionDecl *CalleeDecl);

  /// Constructs the debug code for exiting a function.
  void EmitFunctionEnd(CGBuilderTy &Builder, llvm::Function *Fn);

  /// Emit metadata to indicate the beginning of a new lexical block
  /// and push the block onto the stack.
  void EmitLexicalBlockStart(CGBuilderTy &Builder, SourceLocation Loc);

  /// Emit metadata to indicate the end of a new lexical block and pop
  /// the current block.
  void EmitLexicalBlockEnd(CGBuilderTy &Builder, SourceLocation Loc);

  /// Emit call to \c llvm.dbg.declare for an automatic variable
  /// declaration.
  /// Returns a pointer to the DILocalVariable associated with the
  /// llvm.dbg.declare, or nullptr otherwise.
  llvm::DILocalVariable *
  EmitDeclareOfAutoVariable(const VarDecl *Decl, llvm::Value *AI,
                            CGBuilderTy &Builder,
                            const bool UsePointerValue = false);

  /// Emit call to \c llvm.dbg.label for an label.
  void EmitLabel(const LabelDecl *D, CGBuilderTy &Builder);

  /// Emit call to \c llvm.dbg.declare for an imported variable
  /// declaration in a block.
  void EmitDeclareOfBlockDeclRefVariable(
      const VarDecl *variable, llvm::Value *storage, CGBuilderTy &Builder,
      const CGBlockInfo &blockInfo, llvm::Instruction *InsertPoint = nullptr);

  /// Emit call to \c llvm.dbg.declare for an argument variable
  /// declaration.
  llvm::DILocalVariable *
  EmitDeclareOfArgVariable(const VarDecl *Decl, llvm::Value *AI, unsigned ArgNo,
                           CGBuilderTy &Builder, bool UsePointerValue = false);

  /// Emit call to \c llvm.dbg.declare for the block-literal argument
  /// to a block invocation function.
  void EmitDeclareOfBlockLiteralArgVariable(const CGBlockInfo &block,
                                            StringRef Name, unsigned ArgNo,
                                            llvm::AllocaInst *LocalAddr,
                                            CGBuilderTy &Builder);

  /// Emit information about a global variable.
  void EmitGlobalVariable(llvm::GlobalVariable *GV, const VarDecl *Decl);

  /// Emit a constant global variable's debug info.
  void EmitGlobalVariable(const ValueDecl *VD, const APValue &Init);

  /// Emit information about an external variable.
  void EmitExternalVariable(llvm::GlobalVariable *GV, const VarDecl *Decl);

  /// Emit a pseudo variable and debug info for an intermediate value if it does
  /// not correspond to a variable in the source code, so that a profiler can
  /// track more accurate usage of certain instructions of interest.
  void EmitPseudoVariable(CGBuilderTy &Builder, llvm::Instruction *Value,
                          QualType Ty);

  /// Emit information about global variable alias.
  void EmitGlobalAlias(const llvm::GlobalValue *GV, const GlobalDecl Decl);

  /// Emit C++ using directive.
  void EmitUsingDirective(const UsingDirectiveDecl &UD);

  /// Emit the type explicitly casted to.
  void EmitExplicitCastType(QualType Ty);

  /// Emit the type even if it might not be used.
  void EmitAndRetainType(QualType Ty);

  /// Emit a shadow decl brought in by a using or using-enum
  void EmitUsingShadowDecl(const UsingShadowDecl &USD);

  /// Emit C++ using declaration.
  void EmitUsingDecl(const UsingDecl &UD);

  /// Emit C++ using-enum declaration.
  void EmitUsingEnumDecl(const UsingEnumDecl &UD);

  /// Emit an @import declaration.
  void EmitImportDecl(const ImportDecl &ID);

  /// DebugInfo isn't attached to string literals by default. While certain
  /// aspects of debuginfo aren't useful for string literals (like a name), it's
  /// nice to be able to symbolize the line and column information. This is
  /// especially useful for sanitizers, as it allows symbolization of
  /// heap-buffer-overflows on constant strings.
  void AddStringLiteralDebugInfo(llvm::GlobalVariable *GV,
                                 const StringLiteral *S);

  /// Emit C++ namespace alias.
  llvm::DIImportedEntity *EmitNamespaceAlias(const NamespaceAliasDecl &NA);

  /// Emit record type's standalone debug info.
  llvm::DIType *getOrCreateRecordType(QualType Ty, SourceLocation L);

  /// Emit an Objective-C interface type standalone debug info.
  llvm::DIType *getOrCreateInterfaceType(QualType Ty, SourceLocation Loc);

  /// Emit standalone debug info for a type.
  llvm::DIType *getOrCreateStandaloneType(QualType Ty, SourceLocation Loc);

  /// Add heapallocsite metadata for MSAllocator calls.
  void addHeapAllocSiteMetadata(llvm::CallBase *CallSite, QualType AllocatedTy,
                                SourceLocation Loc);

  void completeType(const EnumDecl *ED);
  void completeType(const RecordDecl *RD);
  void completeRequiredType(const RecordDecl *RD);
  void completeClassData(const RecordDecl *RD);
  void completeClass(const RecordDecl *RD);

  void completeTemplateDefinition(const ClassTemplateSpecializationDecl &SD);
  void completeUnusedClass(const CXXRecordDecl &D);

  /// Create debug info for a macro defined by a #define directive or a macro
  /// undefined by a #undef directive.
  llvm::DIMacro *CreateMacro(llvm::DIMacroFile *Parent, unsigned MType,
                             SourceLocation LineLoc, StringRef Name,
                             StringRef Value);

  /// Create debug info for a file referenced by an #include directive.
  llvm::DIMacroFile *CreateTempMacroFile(llvm::DIMacroFile *Parent,
                                         SourceLocation LineLoc,
                                         SourceLocation FileLoc);

  Param2DILocTy &getParamDbgMappings() { return ParamDbgMappings; }
  ParamDecl2StmtTy &getCoroutineParameterMappings() {
    return CoroutineParameterMappings;
  }

  /// Create a debug location from `TrapLocation` that adds an artificial inline
  /// frame where the frame name is
  ///
  /// * `<Prefix>:<Category>:<FailureMsg>`
  ///
  /// `<Prefix>` is "__clang_trap_msg".
  ///
  /// This is used to store failure reasons for traps.
  llvm::DILocation *CreateTrapFailureMessageFor(llvm::DebugLoc TrapLocation,
                                                StringRef Category,
                                                StringRef FailureMsg);

private:
  /// Emit call to llvm.dbg.declare for a variable declaration.
  /// Returns a pointer to the DILocalVariable associated with the
  /// llvm.dbg.declare, or nullptr otherwise.
  llvm::DILocalVariable *EmitDeclare(const VarDecl *decl, llvm::Value *AI,
                                     std::optional<unsigned> ArgNo,
                                     CGBuilderTy &Builder,
                                     const bool UsePointerValue = false);

  /// Emit call to llvm.dbg.declare for a binding declaration.
  /// Returns a pointer to the DILocalVariable associated with the
  /// llvm.dbg.declare, or nullptr otherwise.
  llvm::DILocalVariable *EmitDeclare(const BindingDecl *decl, llvm::Value *AI,
                                     std::optional<unsigned> ArgNo,
                                     CGBuilderTy &Builder,
                                     const bool UsePointerValue = false);

  struct BlockByRefType {
    /// The wrapper struct used inside the __block_literal struct.
    llvm::DIType *BlockByRefWrapper;
    /// The type as it appears in the source code.
    llvm::DIType *WrappedType;
  };

  bool HasReconstitutableArgs(ArrayRef<TemplateArgument> Args) const;
  std::string GetName(const Decl *, bool Qualified = false) const;

  /// Build up structure info for the byref.  See \a BuildByRefType.
  BlockByRefType EmitTypeForVarWithBlocksAttr(const VarDecl *VD,
                                              uint64_t *OffSet);

  /// Get context info for the DeclContext of \p Decl.
  llvm::DIScope *getDeclContextDescriptor(const Decl *D);
  /// Get context info for a given DeclContext \p Decl.
  llvm::DIScope *getContextDescriptor(const Decl *Context,
                                      llvm::DIScope *Default);

  llvm::DIScope *getCurrentContextDescriptor(const Decl *Decl);

  /// Create a forward decl for a RecordType in a given context.
  llvm::DICompositeType *getOrCreateRecordFwdDecl(const RecordType *,
                                                  llvm::DIScope *);

  /// Return current directory name.
  StringRef getCurrentDirname();

  /// Create new compile unit.
  void CreateCompileUnit();

  /// Compute the file checksum debug info for input file ID.
  std::optional<llvm::DIFile::ChecksumKind>
  computeChecksum(FileID FID, SmallString<64> &Checksum) const;

  /// Get the source of the given file ID.
  std::optional<StringRef> getSource(const SourceManager &SM, FileID FID);

  /// Convenience function to get the file debug info descriptor for the input
  /// location.
  llvm::DIFile *getOrCreateFile(SourceLocation Loc);

  /// Create a file debug info descriptor for a source file.
  llvm::DIFile *
  createFile(StringRef FileName,
             std::optional<llvm::DIFile::ChecksumInfo<StringRef>> CSInfo,
             std::optional<StringRef> Source);

  /// Get the type from the cache or create a new type if necessary.
  llvm::DIType *getOrCreateType(QualType Ty, llvm::DIFile *Fg);

  /// Get a reference to a clang module.  If \p CreateSkeletonCU is true,
  /// this also creates a split dwarf skeleton compile unit.
  llvm::DIModule *getOrCreateModuleRef(ASTSourceDescriptor Mod,
                                       bool CreateSkeletonCU);

  /// DebugTypeExtRefs: If \p D originated in a clang module, return it.
  llvm::DIModule *getParentModuleOrNull(const Decl *D);

  /// Get the type from the cache or create a new partial type if
  /// necessary.
  llvm::DICompositeType *getOrCreateLimitedType(const RecordType *Ty);

  /// Create type metadata for a source language type.
  llvm::DIType *CreateTypeNode(QualType Ty, llvm::DIFile *Fg);

  /// Create new member and increase Offset by FType's size.
  llvm::DIType *CreateMemberType(llvm::DIFile *Unit, QualType FType,
                                 StringRef Name, uint64_t *Offset);

  /// Retrieve the DIDescriptor, if any, for the canonical form of this
  /// declaration.
  llvm::DINode *getDeclarationOrDefinition(const Decl *D);

  /// \return debug info descriptor to describe method
  /// declaration for the given method definition.
  llvm::DISubprogram *getFunctionDeclaration(const Decl *D);

  /// \return          debug info descriptor to the describe method declaration
  ///                  for the given method definition.
  /// \param FnType    For Objective-C methods, their type.
  /// \param LineNo    The declaration's line number.
  /// \param Flags     The DIFlags for the method declaration.
  /// \param SPFlags   The subprogram-spcific flags for the method declaration.
  llvm::DISubprogram *
  getObjCMethodDeclaration(const Decl *D, llvm::DISubroutineType *FnType,
                           unsigned LineNo, llvm::DINode::DIFlags Flags,
                           llvm::DISubprogram::DISPFlags SPFlags);

  /// \return debug info descriptor to describe in-class static data
  /// member declaration for the given out-of-class definition.  If D
  /// is an out-of-class definition of a static data member of a
  /// class, find its corresponding in-class declaration.
  llvm::DIDerivedType *
  getOrCreateStaticDataMemberDeclarationOrNull(const VarDecl *D);

  /// Helper that either creates a forward declaration or a stub.
  llvm::DISubprogram *getFunctionFwdDeclOrStub(GlobalDecl GD, bool Stub);

  /// Create a subprogram describing the forward declaration
  /// represented in the given FunctionDecl wrapped in a GlobalDecl.
  llvm::DISubprogram *getFunctionForwardDeclaration(GlobalDecl GD);

  /// Create a DISubprogram describing the function
  /// represented in the given FunctionDecl wrapped in a GlobalDecl.
  llvm::DISubprogram *getFunctionStub(GlobalDecl GD);

  /// Create a global variable describing the forward declaration
  /// represented in the given VarDecl.
  llvm::DIGlobalVariable *
  getGlobalVariableForwardDeclaration(const VarDecl *VD);

  /// Return a global variable that represents one of the collection of global
  /// variables created for an anonmyous union.
  ///
  /// Recursively collect all of the member fields of a global
  /// anonymous decl and create static variables for them. The first
  /// time this is called it needs to be on a union and then from
  /// there we can have additional unnamed fields.
  llvm::DIGlobalVariableExpression *
  CollectAnonRecordDecls(const RecordDecl *RD, llvm::DIFile *Unit,
                         unsigned LineNo, StringRef LinkageName,
                         llvm::GlobalVariable *Var, llvm::DIScope *DContext);


  /// Return flags which enable debug info emission for call sites, provided
  /// that it is supported and enabled.
  llvm::DINode::DIFlags getCallSiteRelatedAttrs() const;

  /// Get the printing policy for producing names for debug info.
  PrintingPolicy getPrintingPolicy() const;

  /// Get function name for the given FunctionDecl. If the name is
  /// constructed on demand (e.g., C++ destructor) then the name is
  /// stored on the side.
  StringRef getFunctionName(const FunctionDecl *FD);

  /// Returns the unmangled name of an Objective-C method.
  /// This is the display name for the debugging info.
  StringRef getObjCMethodName(const ObjCMethodDecl *FD);

  /// Return selector name. This is used for debugging
  /// info.
  StringRef getSelectorName(Selector S);

  /// Get class name including template argument list.
  StringRef getClassName(const RecordDecl *RD);

  /// Get the vtable name for the given class.
  StringRef getVTableName(const CXXRecordDecl *Decl);

  /// Get the name to use in the debug info for a dynamic initializer or atexit
  /// stub function.
  StringRef getDynamicInitializerName(const VarDecl *VD,
                                      DynamicInitKind StubKind,
                                      llvm::Function *InitFn);

  /// Get line number for the location. If location is invalid
  /// then use current location.
  unsigned getLineNumber(SourceLocation Loc);

  /// Get column number for the location. If location is
  /// invalid then use current location.
  /// \param Force  Assume DebugColumnInfo option is true.
  unsigned getColumnNumber(SourceLocation Loc, bool Force = false);

  /// Collect various properties of a FunctionDecl.
  /// \param GD  A GlobalDecl whose getDecl() must return a FunctionDecl.
  void collectFunctionDeclProps(GlobalDecl GD, llvm::DIFile *Unit,
                                StringRef &Name, StringRef &LinkageName,
                                llvm::DIScope *&FDContext,
                                llvm::DINodeArray &TParamsArray,
                                llvm::DINode::DIFlags &Flags);

  /// Collect various properties of a VarDecl.
  void collectVarDeclProps(const VarDecl *VD, llvm::DIFile *&Unit,
                           unsigned &LineNo, QualType &T, StringRef &Name,
                           StringRef &LinkageName,
                           llvm::MDTuple *&TemplateParameters,
                           llvm::DIScope *&VDContext);

  /// Create a DIExpression representing the constant corresponding
  /// to the specified 'Val'. Returns nullptr on failure.
  llvm::DIExpression *createConstantValueExpression(const clang::ValueDecl *VD,
                                                    const APValue &Val);

  /// Allocate a copy of \p A using the DebugInfoNames allocator
  /// and return a reference to it. If multiple arguments are given the strings
  /// are concatenated.
  StringRef internString(StringRef A, StringRef B = StringRef()) {
    char *Data = DebugInfoNames.Allocate<char>(A.size() + B.size());
    if (!A.empty())
      std::memcpy(Data, A.data(), A.size());
    if (!B.empty())
      std::memcpy(Data + A.size(), B.data(), B.size());
    return StringRef(Data, A.size() + B.size());
  }
};

/// A scoped helper to set the current debug location to the specified
/// location or preferred location of the specified Expr.
class ApplyDebugLocation {
private:
  void init(SourceLocation TemporaryLocation, bool DefaultToEmpty = false);
  ApplyDebugLocation(CodeGenFunction &CGF, bool DefaultToEmpty,
                     SourceLocation TemporaryLocation);

  llvm::DebugLoc OriginalLocation;
  CodeGenFunction *CGF;

public:
  /// Set the location to the (valid) TemporaryLocation.
  ApplyDebugLocation(CodeGenFunction &CGF, SourceLocation TemporaryLocation);
  ApplyDebugLocation(CodeGenFunction &CGF, const Expr *E);
  ApplyDebugLocation(CodeGenFunction &CGF, llvm::DebugLoc Loc);
  ApplyDebugLocation(ApplyDebugLocation &&Other) : CGF(Other.CGF) {
    Other.CGF = nullptr;
  }

  // Define copy assignment operator.
  ApplyDebugLocation &operator=(ApplyDebugLocation &&Other) {
    if (this != &Other) {
      CGF = Other.CGF;
      Other.CGF = nullptr;
    }
    return *this;
  }

  ~ApplyDebugLocation();

  /// Apply TemporaryLocation if it is valid. Otherwise switch
  /// to an artificial debug location that has a valid scope, but no
  /// line information.
  ///
  /// Artificial locations are useful when emitting compiler-generated
  /// helper functions that have no source location associated with
  /// them. The DWARF specification allows the compiler to use the
  /// special line number 0 to indicate code that can not be
  /// attributed to any source location. Note that passing an empty
  /// SourceLocation to CGDebugInfo::setLocation() will result in the
  /// last valid location being reused.
  static ApplyDebugLocation CreateArtificial(CodeGenFunction &CGF) {
    return ApplyDebugLocation(CGF, false, SourceLocation());
  }
  /// Apply TemporaryLocation if it is valid. Otherwise switch
  /// to an artificial debug location that has a valid scope, but no
  /// line information.
  static ApplyDebugLocation
  CreateDefaultArtificial(CodeGenFunction &CGF,
                          SourceLocation TemporaryLocation) {
    return ApplyDebugLocation(CGF, false, TemporaryLocation);
  }

  /// Set the IRBuilder to not attach debug locations.  Note that
  /// passing an empty SourceLocation to \a CGDebugInfo::setLocation()
  /// will result in the last valid location being reused.  Note that
  /// all instructions that do not have a location at the beginning of
  /// a function are counted towards to function prologue.
  static ApplyDebugLocation CreateEmpty(CodeGenFunction &CGF) {
    return ApplyDebugLocation(CGF, true, SourceLocation());
  }
};

/// A scoped helper to set the current debug location to an inlined location.
class ApplyInlineDebugLocation {
  SourceLocation SavedLocation;
  CodeGenFunction *CGF;

public:
  /// Set up the CodeGenFunction's DebugInfo to produce inline locations for the
  /// function \p InlinedFn. The current debug location becomes the inlined call
  /// site of the inlined function.
  ApplyInlineDebugLocation(CodeGenFunction &CGF, GlobalDecl InlinedFn);
  /// Restore everything back to the original state.
  ~ApplyInlineDebugLocation();
};

} // namespace CodeGen
} // namespace clang

#endif // LLVM_CLANG_LIB_CODEGEN_CGDEBUGINFO_H
