//===- DIBuilder.h - Debug Information Builder ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a DIBuilder that is useful for creating debugging
// information entries in LLVM IR form.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DIBUILDER_H
#define LLVM_IR_DIBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace llvm {

  class BasicBlock;
  class Constant;
  class Function;
  class Instruction;
  class LLVMContext;
  class Module;
  class Value;
  class DbgAssignIntrinsic;
  class DbgRecord;

  using DbgInstPtr = PointerUnion<Instruction *, DbgRecord *>;

  class DIBuilder {
    Module &M;
    LLVMContext &VMContext;

    DICompileUnit *CUNode;   ///< The one compile unit created by this DIBuiler.
    Function *DeclareFn;     ///< llvm.dbg.declare
    Function *ValueFn;       ///< llvm.dbg.value
    Function *LabelFn;       ///< llvm.dbg.label
    Function *AssignFn;      ///< llvm.dbg.assign

    SmallVector<TrackingMDNodeRef, 4> AllEnumTypes;
    /// Track the RetainTypes, since they can be updated later on.
    SmallVector<TrackingMDNodeRef, 4> AllRetainTypes;
    SmallVector<DISubprogram *, 4> AllSubprograms;
    SmallVector<Metadata *, 4> AllGVs;
    SmallVector<TrackingMDNodeRef, 4> ImportedModules;
    /// Map Macro parent (which can be DIMacroFile or nullptr) to a list of
    /// Metadata all of type DIMacroNode.
    /// DIMacroNode's with nullptr parent are DICompileUnit direct children.
    MapVector<MDNode *, SetVector<Metadata *>> AllMacrosPerParent;

    /// Track nodes that may be unresolved.
    SmallVector<TrackingMDNodeRef, 4> UnresolvedNodes;
    bool AllowUnresolvedNodes;

    /// Each subprogram's preserved local variables, labels and imported
    /// entities.
    ///
    /// Do not use a std::vector.  Some versions of libc++ apparently copy
    /// instead of move on grow operations, and TrackingMDRef is expensive to
    /// copy.
    DenseMap<DISubprogram *, SmallVector<TrackingMDNodeRef, 4>>
        SubprogramTrackedNodes;

    SmallVectorImpl<TrackingMDNodeRef> &
    getImportTrackingVector(const DIScope *S) {
      return isa_and_nonnull<DILocalScope>(S)
                 ? getSubprogramNodesTrackingVector(S)
                 : ImportedModules;
    }
    SmallVectorImpl<TrackingMDNodeRef> &
    getSubprogramNodesTrackingVector(const DIScope *S) {
      return SubprogramTrackedNodes[cast<DILocalScope>(S)->getSubprogram()];
    }

    /// Create a temporary.
    ///
    /// Create an \a temporary node and track it in \a UnresolvedNodes.
    void trackIfUnresolved(MDNode *N);

    /// Internal helper for insertDeclare.
    DbgInstPtr insertDeclare(llvm::Value *Storage, DILocalVariable *VarInfo,
                             DIExpression *Expr, const DILocation *DL,
                             BasicBlock *InsertBB, Instruction *InsertBefore);

    /// Internal helper for insertLabel.
    DbgInstPtr insertLabel(DILabel *LabelInfo, const DILocation *DL,
                           BasicBlock *InsertBB, Instruction *InsertBefore);

    /// Internal helper. Track metadata if untracked and insert \p DVR.
    void insertDbgVariableRecord(DbgVariableRecord *DVR, BasicBlock *InsertBB,
                                 Instruction *InsertBefore,
                                 bool InsertAtHead = false);

    /// Internal helper with common code used by insertDbg{Value,Addr}Intrinsic.
    Instruction *insertDbgIntrinsic(llvm::Function *Intrinsic, llvm::Value *Val,
                                    DILocalVariable *VarInfo,
                                    DIExpression *Expr, const DILocation *DL,
                                    BasicBlock *InsertBB,
                                    Instruction *InsertBefore);

    /// Internal helper for insertDbgValueIntrinsic.
    DbgInstPtr insertDbgValueIntrinsic(llvm::Value *Val,
                                       DILocalVariable *VarInfo,
                                       DIExpression *Expr, const DILocation *DL,
                                       BasicBlock *InsertBB,
                                       Instruction *InsertBefore);

  public:
    /// Construct a builder for a module.
    ///
    /// If \c AllowUnresolved, collect unresolved nodes attached to the module
    /// in order to resolve cycles during \a finalize().
    ///
    /// If \p CU is given a value other than nullptr, then set \p CUNode to CU.
    explicit DIBuilder(Module &M, bool AllowUnresolved = true,
                       DICompileUnit *CU = nullptr);
    DIBuilder(const DIBuilder &) = delete;
    DIBuilder &operator=(const DIBuilder &) = delete;

    /// Construct any deferred debug info descriptors.
    void finalize();

    /// Finalize a specific subprogram - no new variables may be added to this
    /// subprogram afterwards.
    void finalizeSubprogram(DISubprogram *SP);

    /// A CompileUnit provides an anchor for all debugging
    /// information generated during this instance of compilation.
    /// \param Lang          Source programming language, eg. dwarf::DW_LANG_C99
    /// \param File          File info.
    /// \param Producer      Identify the producer of debugging information
    ///                      and code.  Usually this is a compiler
    ///                      version string.
    /// \param isOptimized   A boolean flag which indicates whether optimization
    ///                      is enabled or not.
    /// \param Flags         This string lists command line options. This
    ///                      string is directly embedded in debug info
    ///                      output which may be used by a tool
    ///                      analyzing generated debugging information.
    /// \param RV            This indicates runtime version for languages like
    ///                      Objective-C.
    /// \param SplitName     The name of the file that we'll split debug info
    ///                      out into.
    /// \param Kind          The kind of debug information to generate.
    /// \param DWOId         The DWOId if this is a split skeleton compile unit.
    /// \param SplitDebugInlining    Whether to emit inline debug info.
    /// \param DebugInfoForProfiling Whether to emit extra debug info for
    ///                              profile collection.
    /// \param NameTableKind  Whether to emit .debug_gnu_pubnames,
    ///                      .debug_pubnames, or no pubnames at all.
    /// \param SysRoot       The clang system root (value of -isysroot).
    /// \param SDK           The SDK name. On Darwin, this is the last component
    ///                      of the sysroot.
    DICompileUnit *
    createCompileUnit(unsigned Lang, DIFile *File, StringRef Producer,
                      bool isOptimized, StringRef Flags, unsigned RV,
                      StringRef SplitName = StringRef(),
                      DICompileUnit::DebugEmissionKind Kind =
                          DICompileUnit::DebugEmissionKind::FullDebug,
                      uint64_t DWOId = 0, bool SplitDebugInlining = true,
                      bool DebugInfoForProfiling = false,
                      DICompileUnit::DebugNameTableKind NameTableKind =
                          DICompileUnit::DebugNameTableKind::Default,
                      bool RangesBaseAddress = false, StringRef SysRoot = {},
                      StringRef SDK = {});

    /// Create a file descriptor to hold debugging information for a file.
    /// \param Filename  File name.
    /// \param Directory Directory.
    /// \param Checksum  Optional checksum kind (e.g. CSK_MD5, CSK_SHA1, etc.)
    ///                  and value.
    /// \param Source    Optional source text.
    DIFile *createFile(
        StringRef Filename, StringRef Directory,
        std::optional<DIFile::ChecksumInfo<StringRef>> Checksum = std::nullopt,
        std::optional<StringRef> Source = std::nullopt);

    /// Create debugging information entry for a macro.
    /// \param Parent     Macro parent (could be nullptr).
    /// \param Line       Source line number where the macro is defined.
    /// \param MacroType  DW_MACINFO_define or DW_MACINFO_undef.
    /// \param Name       Macro name.
    /// \param Value      Macro value.
    DIMacro *createMacro(DIMacroFile *Parent, unsigned Line, unsigned MacroType,
                         StringRef Name, StringRef Value = StringRef());

    /// Create debugging information temporary entry for a macro file.
    /// List of macro node direct children will be calculated by DIBuilder,
    /// using the \p Parent relationship.
    /// \param Parent     Macro file parent (could be nullptr).
    /// \param Line       Source line number where the macro file is included.
    /// \param File       File descriptor containing the name of the macro file.
    DIMacroFile *createTempMacroFile(DIMacroFile *Parent, unsigned Line,
                                     DIFile *File);

    /// Create a single enumerator value.
    DIEnumerator *createEnumerator(StringRef Name, const APSInt &Value);
    DIEnumerator *createEnumerator(StringRef Name, uint64_t Val,
                                   bool IsUnsigned = false);

    /// Create a DWARF unspecified type.
    DIBasicType *createUnspecifiedType(StringRef Name);

    /// Create C++11 nullptr type.
    DIBasicType *createNullPtrType();

    /// Create debugging information entry for a basic
    /// type.
    /// \param Name        Type name.
    /// \param SizeInBits  Size of the type.
    /// \param Encoding    DWARF encoding code, e.g., dwarf::DW_ATE_float.
    /// \param Flags       Optional DWARF attributes, e.g., DW_AT_endianity.
    DIBasicType *createBasicType(StringRef Name, uint64_t SizeInBits,
                                 unsigned Encoding,
                                 DINode::DIFlags Flags = DINode::FlagZero);

    /// Create debugging information entry for a string
    /// type.
    /// \param Name        Type name.
    /// \param SizeInBits  Size of the type.
    DIStringType *createStringType(StringRef Name, uint64_t SizeInBits);

    /// Create debugging information entry for Fortran
    /// assumed length string type.
    /// \param Name            Type name.
    /// \param StringLength    String length expressed as DIVariable *.
    /// \param StrLocationExp  Optional memory location of the string.
    DIStringType *createStringType(StringRef Name, DIVariable *StringLength,
                                   DIExpression *StrLocationExp = nullptr);

    /// Create debugging information entry for Fortran
    /// assumed length string type.
    /// \param Name             Type name.
    /// \param StringLengthExp  String length expressed in DIExpression form.
    /// \param StrLocationExp   Optional memory location of the string.
    DIStringType *createStringType(StringRef Name,
                                   DIExpression *StringLengthExp,
                                   DIExpression *StrLocationExp = nullptr);

    /// Create debugging information entry for a qualified
    /// type, e.g. 'const int'.
    /// \param Tag         Tag identifing type, e.g. dwarf::TAG_volatile_type
    /// \param FromTy      Base Type.
    DIDerivedType *createQualifiedType(unsigned Tag, DIType *FromTy);

    /// Create debugging information entry for a pointer.
    /// \param PointeeTy         Type pointed by this pointer.
    /// \param SizeInBits        Size.
    /// \param AlignInBits       Alignment. (optional)
    /// \param DWARFAddressSpace DWARF address space. (optional)
    /// \param Name              Pointer type name. (optional)
    /// \param Annotations       Member annotations.
    DIDerivedType *
    createPointerType(DIType *PointeeTy, uint64_t SizeInBits,
                      uint32_t AlignInBits = 0,
                      std::optional<unsigned> DWARFAddressSpace = std::nullopt,
                      StringRef Name = "", DINodeArray Annotations = nullptr);

    /// Create a __ptrauth qualifier.
    DIDerivedType *createPtrAuthQualifiedType(DIType *FromTy, unsigned Key,
                                              bool IsAddressDiscriminated,
                                              unsigned ExtraDiscriminator,
                                              bool IsaPointer,
                                              bool authenticatesNullValues);

    /// Create debugging information entry for a pointer to member.
    /// \param PointeeTy Type pointed to by this pointer.
    /// \param SizeInBits  Size.
    /// \param AlignInBits Alignment. (optional)
    /// \param Class Type for which this pointer points to members of.
    DIDerivedType *
    createMemberPointerType(DIType *PointeeTy, DIType *Class,
                            uint64_t SizeInBits, uint32_t AlignInBits = 0,
                            DINode::DIFlags Flags = DINode::FlagZero);

    /// Create debugging information entry for a c++
    /// style reference or rvalue reference type.
    DIDerivedType *createReferenceType(
        unsigned Tag, DIType *RTy, uint64_t SizeInBits = 0,
        uint32_t AlignInBits = 0,
        std::optional<unsigned> DWARFAddressSpace = std::nullopt);

    /// Create debugging information entry for a typedef.
    /// \param Ty          Original type.
    /// \param Name        Typedef name.
    /// \param File        File where this type is defined.
    /// \param LineNo      Line number.
    /// \param Context     The surrounding context for the typedef.
    /// \param AlignInBits Alignment. (optional)
    /// \param Flags       Flags to describe inheritance attribute, e.g. private
    /// \param Annotations Annotations. (optional)
    DIDerivedType *createTypedef(DIType *Ty, StringRef Name, DIFile *File,
                                 unsigned LineNo, DIScope *Context,
                                 uint32_t AlignInBits = 0,
                                 DINode::DIFlags Flags = DINode::FlagZero,
                                 DINodeArray Annotations = nullptr);

    /// Create debugging information entry for a template alias.
    /// \param Ty          Original type.
    /// \param Name        Alias name.
    /// \param File        File where this type is defined.
    /// \param LineNo      Line number.
    /// \param Context     The surrounding context for the alias.
    /// \param TParams     The template arguments.
    /// \param AlignInBits Alignment. (optional)
    /// \param Flags       Flags to describe inheritance attribute (optional),
    ///                    e.g. private.
    /// \param Annotations Annotations. (optional)
    DIDerivedType *createTemplateAlias(DIType *Ty, StringRef Name, DIFile *File,
                                       unsigned LineNo, DIScope *Context,
                                       DINodeArray TParams,
                                       uint32_t AlignInBits = 0,
                                       DINode::DIFlags Flags = DINode::FlagZero,
                                       DINodeArray Annotations = nullptr);

    /// Create debugging information entry for a 'friend'.
    DIDerivedType *createFriend(DIType *Ty, DIType *FriendTy);

    /// Create debugging information entry to establish
    /// inheritance relationship between two types.
    /// \param Ty           Original type.
    /// \param BaseTy       Base type. Ty is inherits from base.
    /// \param BaseOffset   Base offset.
    /// \param VBPtrOffset  Virtual base pointer offset.
    /// \param Flags        Flags to describe inheritance attribute,
    ///                     e.g. private
    DIDerivedType *createInheritance(DIType *Ty, DIType *BaseTy,
                                     uint64_t BaseOffset, uint32_t VBPtrOffset,
                                     DINode::DIFlags Flags);

    /// Create debugging information entry for a member.
    /// \param Scope        Member scope.
    /// \param Name         Member name.
    /// \param File         File where this member is defined.
    /// \param LineNo       Line number.
    /// \param SizeInBits   Member size.
    /// \param AlignInBits  Member alignment.
    /// \param OffsetInBits Member offset.
    /// \param Flags        Flags to encode member attribute, e.g. private
    /// \param Ty           Parent type.
    /// \param Annotations  Member annotations.
    DIDerivedType *createMemberType(DIScope *Scope, StringRef Name,
                                    DIFile *File, unsigned LineNo,
                                    uint64_t SizeInBits, uint32_t AlignInBits,
                                    uint64_t OffsetInBits,
                                    DINode::DIFlags Flags, DIType *Ty,
                                    DINodeArray Annotations = nullptr);

    /// Create debugging information entry for a variant.  A variant
    /// normally should be a member of a variant part.
    /// \param Scope        Member scope.
    /// \param Name         Member name.
    /// \param File         File where this member is defined.
    /// \param LineNo       Line number.
    /// \param SizeInBits   Member size.
    /// \param AlignInBits  Member alignment.
    /// \param OffsetInBits Member offset.
    /// \param Flags        Flags to encode member attribute, e.g. private
    /// \param Discriminant The discriminant for this branch; null for
    ///                     the default branch
    /// \param Ty           Parent type.
    DIDerivedType *createVariantMemberType(DIScope *Scope, StringRef Name,
					   DIFile *File, unsigned LineNo,
					   uint64_t SizeInBits,
					   uint32_t AlignInBits,
					   uint64_t OffsetInBits,
					   Constant *Discriminant,
					   DINode::DIFlags Flags, DIType *Ty);

    /// Create debugging information entry for a bit field member.
    /// \param Scope               Member scope.
    /// \param Name                Member name.
    /// \param File                File where this member is defined.
    /// \param LineNo              Line number.
    /// \param SizeInBits          Member size.
    /// \param OffsetInBits        Member offset.
    /// \param StorageOffsetInBits Member storage offset.
    /// \param Flags               Flags to encode member attribute.
    /// \param Ty                  Parent type.
    /// \param Annotations         Member annotations.
    DIDerivedType *createBitFieldMemberType(DIScope *Scope, StringRef Name,
                                            DIFile *File, unsigned LineNo,
                                            uint64_t SizeInBits,
                                            uint64_t OffsetInBits,
                                            uint64_t StorageOffsetInBits,
                                            DINode::DIFlags Flags, DIType *Ty,
                                            DINodeArray Annotations = nullptr);

    /// Create debugging information entry for a
    /// C++ static data member.
    /// \param Scope      Member scope.
    /// \param Name       Member name.
    /// \param File       File where this member is declared.
    /// \param LineNo     Line number.
    /// \param Ty         Type of the static member.
    /// \param Flags      Flags to encode member attribute, e.g. private.
    /// \param Val        Const initializer of the member.
    /// \param Tag        DWARF tag of the static member.
    /// \param AlignInBits  Member alignment.
    DIDerivedType *createStaticMemberType(DIScope *Scope, StringRef Name,
                                          DIFile *File, unsigned LineNo,
                                          DIType *Ty, DINode::DIFlags Flags,
                                          Constant *Val, unsigned Tag,
                                          uint32_t AlignInBits = 0);

    /// Create debugging information entry for Objective-C
    /// instance variable.
    /// \param Name         Member name.
    /// \param File         File where this member is defined.
    /// \param LineNo       Line number.
    /// \param SizeInBits   Member size.
    /// \param AlignInBits  Member alignment.
    /// \param OffsetInBits Member offset.
    /// \param Flags        Flags to encode member attribute, e.g. private
    /// \param Ty           Parent type.
    /// \param PropertyNode Property associated with this ivar.
    DIDerivedType *createObjCIVar(StringRef Name, DIFile *File, unsigned LineNo,
                                  uint64_t SizeInBits, uint32_t AlignInBits,
                                  uint64_t OffsetInBits, DINode::DIFlags Flags,
                                  DIType *Ty, MDNode *PropertyNode);

    /// Create debugging information entry for Objective-C
    /// property.
    /// \param Name         Property name.
    /// \param File         File where this property is defined.
    /// \param LineNumber   Line number.
    /// \param GetterName   Name of the Objective C property getter selector.
    /// \param SetterName   Name of the Objective C property setter selector.
    /// \param PropertyAttributes Objective C property attributes.
    /// \param Ty           Type.
    DIObjCProperty *createObjCProperty(StringRef Name, DIFile *File,
                                       unsigned LineNumber,
                                       StringRef GetterName,
                                       StringRef SetterName,
                                       unsigned PropertyAttributes, DIType *Ty);

    /// Create debugging information entry for a class.
    /// \param Scope        Scope in which this class is defined.
    /// \param Name         class name.
    /// \param File         File where this member is defined.
    /// \param LineNumber   Line number.
    /// \param SizeInBits   Member size.
    /// \param AlignInBits  Member alignment.
    /// \param OffsetInBits Member offset.
    /// \param Flags        Flags to encode member attribute, e.g. private
    /// \param Elements     class members.
    /// \param RunTimeLang  Optional parameter, Objective-C runtime version.
    /// \param VTableHolder Debug info of the base class that contains vtable
    ///                     for this type. This is used in
    ///                     DW_AT_containing_type. See DWARF documentation
    ///                     for more info.
    /// \param TemplateParms Template type parameters.
    /// \param UniqueIdentifier A unique identifier for the class.
    DICompositeType *createClassType(
        DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
        uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
        DINode::DIFlags Flags, DIType *DerivedFrom, DINodeArray Elements,
        unsigned RunTimeLang = 0, DIType *VTableHolder = nullptr,
        MDNode *TemplateParms = nullptr, StringRef UniqueIdentifier = "");

    /// Create debugging information entry for a struct.
    /// \param Scope        Scope in which this struct is defined.
    /// \param Name         Struct name.
    /// \param File         File where this member is defined.
    /// \param LineNumber   Line number.
    /// \param SizeInBits   Member size.
    /// \param AlignInBits  Member alignment.
    /// \param Flags        Flags to encode member attribute, e.g. private
    /// \param Elements     Struct elements.
    /// \param RunTimeLang  Optional parameter, Objective-C runtime version.
    /// \param UniqueIdentifier A unique identifier for the struct.
    DICompositeType *createStructType(
        DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
        uint64_t SizeInBits, uint32_t AlignInBits, DINode::DIFlags Flags,
        DIType *DerivedFrom, DINodeArray Elements, unsigned RunTimeLang = 0,
        DIType *VTableHolder = nullptr, StringRef UniqueIdentifier = "");

    /// Create debugging information entry for an union.
    /// \param Scope        Scope in which this union is defined.
    /// \param Name         Union name.
    /// \param File         File where this member is defined.
    /// \param LineNumber   Line number.
    /// \param SizeInBits   Member size.
    /// \param AlignInBits  Member alignment.
    /// \param Flags        Flags to encode member attribute, e.g. private
    /// \param Elements     Union elements.
    /// \param RunTimeLang  Optional parameter, Objective-C runtime version.
    /// \param UniqueIdentifier A unique identifier for the union.
    DICompositeType *createUnionType(DIScope *Scope, StringRef Name,
                                     DIFile *File, unsigned LineNumber,
                                     uint64_t SizeInBits, uint32_t AlignInBits,
                                     DINode::DIFlags Flags,
                                     DINodeArray Elements,
                                     unsigned RunTimeLang = 0,
                                     StringRef UniqueIdentifier = "");

    /// Create debugging information entry for a variant part.  A
    /// variant part normally has a discriminator (though this is not
    /// required) and a number of variant children.
    /// \param Scope        Scope in which this union is defined.
    /// \param Name         Union name.
    /// \param File         File where this member is defined.
    /// \param LineNumber   Line number.
    /// \param SizeInBits   Member size.
    /// \param AlignInBits  Member alignment.
    /// \param Flags        Flags to encode member attribute, e.g. private
    /// \param Discriminator Discriminant member
    /// \param Elements     Variant elements.
    /// \param UniqueIdentifier A unique identifier for the union.
    DICompositeType *createVariantPart(DIScope *Scope, StringRef Name,
				       DIFile *File, unsigned LineNumber,
				       uint64_t SizeInBits, uint32_t AlignInBits,
				       DINode::DIFlags Flags,
				       DIDerivedType *Discriminator,
				       DINodeArray Elements,
				       StringRef UniqueIdentifier = "");

    /// Create debugging information for template
    /// type parameter.
    /// \param Scope        Scope in which this type is defined.
    /// \param Name         Type parameter name.
    /// \param Ty           Parameter type.
    /// \param IsDefault    Parameter is default or not
    DITemplateTypeParameter *createTemplateTypeParameter(DIScope *Scope,
                                                         StringRef Name,
                                                         DIType *Ty,
                                                         bool IsDefault);

    /// Create debugging information for template
    /// value parameter.
    /// \param Scope        Scope in which this type is defined.
    /// \param Name         Value parameter name.
    /// \param Ty           Parameter type.
    /// \param IsDefault    Parameter is default or not
    /// \param Val          Constant parameter value.
    DITemplateValueParameter *
    createTemplateValueParameter(DIScope *Scope, StringRef Name, DIType *Ty,
                                 bool IsDefault, Constant *Val);

    /// Create debugging information for a template template parameter.
    /// \param Scope        Scope in which this type is defined.
    /// \param Name         Value parameter name.
    /// \param Ty           Parameter type.
    /// \param Val          The fully qualified name of the template.
    /// \param IsDefault    Parameter is default or not.
    DITemplateValueParameter *
    createTemplateTemplateParameter(DIScope *Scope, StringRef Name, DIType *Ty,
                                    StringRef Val, bool IsDefault = false);

    /// Create debugging information for a template parameter pack.
    /// \param Scope        Scope in which this type is defined.
    /// \param Name         Value parameter name.
    /// \param Ty           Parameter type.
    /// \param Val          An array of types in the pack.
    DITemplateValueParameter *createTemplateParameterPack(DIScope *Scope,
                                                          StringRef Name,
                                                          DIType *Ty,
                                                          DINodeArray Val);

    /// Create debugging information entry for an array.
    /// \param Size         Array size.
    /// \param AlignInBits  Alignment.
    /// \param Ty           Element type.
    /// \param Subscripts   Subscripts.
    /// \param DataLocation The location of the raw data of a descriptor-based
    ///                     Fortran array, either a DIExpression* or
    ///                     a DIVariable*.
    /// \param Associated   The associated attribute of a descriptor-based
    ///                     Fortran array, either a DIExpression* or
    ///                     a DIVariable*.
    /// \param Allocated    The allocated attribute of a descriptor-based
    ///                     Fortran array, either a DIExpression* or
    ///                     a DIVariable*.
    /// \param Rank         The rank attribute of a descriptor-based
    ///                     Fortran array, either a DIExpression* or
    ///                     a DIVariable*.
    DICompositeType *createArrayType(
        uint64_t Size, uint32_t AlignInBits, DIType *Ty, DINodeArray Subscripts,
        PointerUnion<DIExpression *, DIVariable *> DataLocation = nullptr,
        PointerUnion<DIExpression *, DIVariable *> Associated = nullptr,
        PointerUnion<DIExpression *, DIVariable *> Allocated = nullptr,
        PointerUnion<DIExpression *, DIVariable *> Rank = nullptr);

    /// Create debugging information entry for a vector type.
    /// \param Size         Array size.
    /// \param AlignInBits  Alignment.
    /// \param Ty           Element type.
    /// \param Subscripts   Subscripts.
    DICompositeType *createVectorType(uint64_t Size, uint32_t AlignInBits,
                                      DIType *Ty, DINodeArray Subscripts);

    /// Create debugging information entry for an
    /// enumeration.
    /// \param Scope          Scope in which this enumeration is defined.
    /// \param Name           Union name.
    /// \param File           File where this member is defined.
    /// \param LineNumber     Line number.
    /// \param SizeInBits     Member size.
    /// \param AlignInBits    Member alignment.
    /// \param Elements       Enumeration elements.
    /// \param UnderlyingType Underlying type of a C++11/ObjC fixed enum.
    /// \param RunTimeLang  Optional parameter, Objective-C runtime version.
    /// \param UniqueIdentifier A unique identifier for the enum.
    /// \param IsScoped Boolean flag indicate if this is C++11/ObjC 'enum
    /// class'.
    DICompositeType *createEnumerationType(
        DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNumber,
        uint64_t SizeInBits, uint32_t AlignInBits, DINodeArray Elements,
        DIType *UnderlyingType, unsigned RunTimeLang = 0,
        StringRef UniqueIdentifier = "", bool IsScoped = false);
    /// Create debugging information entry for a set.
    /// \param Scope          Scope in which this set is defined.
    /// \param Name           Set name.
    /// \param File           File where this set is defined.
    /// \param LineNo         Line number.
    /// \param SizeInBits     Set size.
    /// \param AlignInBits    Set alignment.
    /// \param Ty             Base type of the set.
    DIDerivedType *createSetType(DIScope *Scope, StringRef Name, DIFile *File,
                                 unsigned LineNo, uint64_t SizeInBits,
                                 uint32_t AlignInBits, DIType *Ty);

    /// Create subroutine type.
    /// \param ParameterTypes  An array of subroutine parameter types. This
    ///                        includes return type at 0th index.
    /// \param Flags           E.g.: LValueReference.
    ///                        These flags are used to emit dwarf attributes.
    /// \param CC              Calling convention, e.g. dwarf::DW_CC_normal
    DISubroutineType *
    createSubroutineType(DITypeRefArray ParameterTypes,
                         DINode::DIFlags Flags = DINode::FlagZero,
                         unsigned CC = 0);

    /// Create a distinct clone of \p SP with FlagArtificial set.
    static DISubprogram *createArtificialSubprogram(DISubprogram *SP);

    /// Create a uniqued clone of \p Ty with FlagArtificial set.
    static DIType *createArtificialType(DIType *Ty);

    /// Create a uniqued clone of \p Ty with FlagObjectPointer and
    /// FlagArtificial set.
    static DIType *createObjectPointerType(DIType *Ty);

    /// Create a permanent forward-declared type.
    DICompositeType *createForwardDecl(unsigned Tag, StringRef Name,
                                       DIScope *Scope, DIFile *F, unsigned Line,
                                       unsigned RuntimeLang = 0,
                                       uint64_t SizeInBits = 0,
                                       uint32_t AlignInBits = 0,
                                       StringRef UniqueIdentifier = "");

    /// Create a temporary forward-declared type.
    DICompositeType *createReplaceableCompositeType(
        unsigned Tag, StringRef Name, DIScope *Scope, DIFile *F, unsigned Line,
        unsigned RuntimeLang = 0, uint64_t SizeInBits = 0,
        uint32_t AlignInBits = 0, DINode::DIFlags Flags = DINode::FlagFwdDecl,
        StringRef UniqueIdentifier = "", DINodeArray Annotations = nullptr);

    /// Retain DIScope* in a module even if it is not referenced
    /// through debug info anchors.
    void retainType(DIScope *T);

    /// Create unspecified parameter type
    /// for a subroutine type.
    DIBasicType *createUnspecifiedParameter();

    /// Get a DINodeArray, create one if required.
    DINodeArray getOrCreateArray(ArrayRef<Metadata *> Elements);

    /// Get a DIMacroNodeArray, create one if required.
    DIMacroNodeArray getOrCreateMacroArray(ArrayRef<Metadata *> Elements);

    /// Get a DITypeRefArray, create one if required.
    DITypeRefArray getOrCreateTypeArray(ArrayRef<Metadata *> Elements);

    /// Create a descriptor for a value range.  This
    /// implicitly uniques the values returned.
    DISubrange *getOrCreateSubrange(int64_t Lo, int64_t Count);
    DISubrange *getOrCreateSubrange(int64_t Lo, Metadata *CountNode);
    DISubrange *getOrCreateSubrange(Metadata *Count, Metadata *LowerBound,
                                    Metadata *UpperBound, Metadata *Stride);

    DIGenericSubrange *
    getOrCreateGenericSubrange(DIGenericSubrange::BoundType Count,
                               DIGenericSubrange::BoundType LowerBound,
                               DIGenericSubrange::BoundType UpperBound,
                               DIGenericSubrange::BoundType Stride);

    /// Create a new descriptor for the specified variable.
    /// \param Context     Variable scope.
    /// \param Name        Name of the variable.
    /// \param LinkageName Mangled  name of the variable.
    /// \param File        File where this variable is defined.
    /// \param LineNo      Line number.
    /// \param Ty          Variable Type.
    /// \param IsLocalToUnit Boolean flag indicate whether this variable is
    ///                      externally visible or not.
    /// \param Expr        The location of the global relative to the attached
    ///                    GlobalVariable.
    /// \param Decl        Reference to the corresponding declaration.
    /// \param AlignInBits Variable alignment(or 0 if no alignment attr was
    ///                    specified)
    DIGlobalVariableExpression *createGlobalVariableExpression(
        DIScope *Context, StringRef Name, StringRef LinkageName, DIFile *File,
        unsigned LineNo, DIType *Ty, bool IsLocalToUnit, bool isDefined = true,
        DIExpression *Expr = nullptr, MDNode *Decl = nullptr,
        MDTuple *TemplateParams = nullptr, uint32_t AlignInBits = 0,
        DINodeArray Annotations = nullptr);

    /// Identical to createGlobalVariable
    /// except that the resulting DbgNode is temporary and meant to be RAUWed.
    DIGlobalVariable *createTempGlobalVariableFwdDecl(
        DIScope *Context, StringRef Name, StringRef LinkageName, DIFile *File,
        unsigned LineNo, DIType *Ty, bool IsLocalToUnit, MDNode *Decl = nullptr,
        MDTuple *TemplateParams = nullptr, uint32_t AlignInBits = 0);

    /// Create a new descriptor for an auto variable.  This is a local variable
    /// that is not a subprogram parameter.
    ///
    /// \c Scope must be a \a DILocalScope, and thus its scope chain eventually
    /// leads to a \a DISubprogram.
    ///
    /// If \c AlwaysPreserve, this variable will be referenced from its
    /// containing subprogram, and will survive some optimizations.
    DILocalVariable *
    createAutoVariable(DIScope *Scope, StringRef Name, DIFile *File,
                       unsigned LineNo, DIType *Ty, bool AlwaysPreserve = false,
                       DINode::DIFlags Flags = DINode::FlagZero,
                       uint32_t AlignInBits = 0);

    /// Create a new descriptor for an label.
    ///
    /// \c Scope must be a \a DILocalScope, and thus its scope chain eventually
    /// leads to a \a DISubprogram.
    DILabel *
    createLabel(DIScope *Scope, StringRef Name, DIFile *File, unsigned LineNo,
                bool AlwaysPreserve = false);

    /// Create a new descriptor for a parameter variable.
    ///
    /// \c Scope must be a \a DILocalScope, and thus its scope chain eventually
    /// leads to a \a DISubprogram.
    ///
    /// \c ArgNo is the index (starting from \c 1) of this variable in the
    /// subprogram parameters.  \c ArgNo should not conflict with other
    /// parameters of the same subprogram.
    ///
    /// If \c AlwaysPreserve, this variable will be referenced from its
    /// containing subprogram, and will survive some optimizations.
    DILocalVariable *
    createParameterVariable(DIScope *Scope, StringRef Name, unsigned ArgNo,
                            DIFile *File, unsigned LineNo, DIType *Ty,
                            bool AlwaysPreserve = false,
                            DINode::DIFlags Flags = DINode::FlagZero,
                            DINodeArray Annotations = nullptr);

    /// Create a new descriptor for the specified
    /// variable which has a complex address expression for its address.
    /// \param Addr        An array of complex address operations.
    DIExpression *createExpression(ArrayRef<uint64_t> Addr = std::nullopt);

    /// Create an expression for a variable that does not have an address, but
    /// does have a constant value.
    DIExpression *createConstantValueExpression(uint64_t Val) {
      return DIExpression::get(
          VMContext, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_stack_value});
    }

    /// Create a new descriptor for the specified subprogram.
    /// See comments in DISubprogram* for descriptions of these fields.
    /// \param Scope         Function scope.
    /// \param Name          Function name.
    /// \param LinkageName   Mangled function name.
    /// \param File          File where this variable is defined.
    /// \param LineNo        Line number.
    /// \param Ty            Function type.
    /// \param ScopeLine     Set to the beginning of the scope this starts
    /// \param Flags         e.g. is this function prototyped or not.
    ///                      These flags are used to emit dwarf attributes.
    /// \param SPFlags       Additional flags specific to subprograms.
    /// \param TParams       Function template parameters.
    /// \param ThrownTypes   Exception types this function may throw.
    /// \param Annotations   Attribute Annotations.
    /// \param TargetFuncName The name of the target function if this is
    ///                       a trampoline.
    DISubprogram *
    createFunction(DIScope *Scope, StringRef Name, StringRef LinkageName,
                   DIFile *File, unsigned LineNo, DISubroutineType *Ty,
                   unsigned ScopeLine, DINode::DIFlags Flags = DINode::FlagZero,
                   DISubprogram::DISPFlags SPFlags = DISubprogram::SPFlagZero,
                   DITemplateParameterArray TParams = nullptr,
                   DISubprogram *Decl = nullptr,
                   DITypeArray ThrownTypes = nullptr,
                   DINodeArray Annotations = nullptr,
                   StringRef TargetFuncName = "");

    /// Identical to createFunction,
    /// except that the resulting DbgNode is meant to be RAUWed.
    DISubprogram *createTempFunctionFwdDecl(
        DIScope *Scope, StringRef Name, StringRef LinkageName, DIFile *File,
        unsigned LineNo, DISubroutineType *Ty, unsigned ScopeLine,
        DINode::DIFlags Flags = DINode::FlagZero,
        DISubprogram::DISPFlags SPFlags = DISubprogram::SPFlagZero,
        DITemplateParameterArray TParams = nullptr,
        DISubprogram *Decl = nullptr, DITypeArray ThrownTypes = nullptr);

    /// Create a new descriptor for the specified C++ method.
    /// See comments in \a DISubprogram* for descriptions of these fields.
    /// \param Scope         Function scope.
    /// \param Name          Function name.
    /// \param LinkageName   Mangled function name.
    /// \param File          File where this variable is defined.
    /// \param LineNo        Line number.
    /// \param Ty            Function type.
    /// \param VTableIndex   Index no of this method in virtual table, or -1u if
    ///                      unrepresentable.
    /// \param ThisAdjustment
    ///                      MS ABI-specific adjustment of 'this' that occurs
    ///                      in the prologue.
    /// \param VTableHolder  Type that holds vtable.
    /// \param Flags         e.g. is this function prototyped or not.
    ///                      This flags are used to emit dwarf attributes.
    /// \param SPFlags       Additional flags specific to subprograms.
    /// \param TParams       Function template parameters.
    /// \param ThrownTypes   Exception types this function may throw.
    DISubprogram *
    createMethod(DIScope *Scope, StringRef Name, StringRef LinkageName,
                 DIFile *File, unsigned LineNo, DISubroutineType *Ty,
                 unsigned VTableIndex = 0, int ThisAdjustment = 0,
                 DIType *VTableHolder = nullptr,
                 DINode::DIFlags Flags = DINode::FlagZero,
                 DISubprogram::DISPFlags SPFlags = DISubprogram::SPFlagZero,
                 DITemplateParameterArray TParams = nullptr,
                 DITypeArray ThrownTypes = nullptr);

    /// Create common block entry for a Fortran common block.
    /// \param Scope       Scope of this common block.
    /// \param decl        Global variable declaration.
    /// \param Name        The name of this common block.
    /// \param File        The file this common block is defined.
    /// \param LineNo      Line number.
    DICommonBlock *createCommonBlock(DIScope *Scope, DIGlobalVariable *decl,
                                     StringRef Name, DIFile *File,
                                     unsigned LineNo);

    /// This creates new descriptor for a namespace with the specified
    /// parent scope.
    /// \param Scope       Namespace scope
    /// \param Name        Name of this namespace
    /// \param ExportSymbols True for C++ inline namespaces.
    DINamespace *createNameSpace(DIScope *Scope, StringRef Name,
                                 bool ExportSymbols);

    /// This creates new descriptor for a module with the specified
    /// parent scope.
    /// \param Scope       Parent scope
    /// \param Name        Name of this module
    /// \param ConfigurationMacros
    ///                    A space-separated shell-quoted list of -D macro
    ///                    definitions as they would appear on a command line.
    /// \param IncludePath The path to the module map file.
    /// \param APINotesFile The path to an API notes file for this module.
    /// \param File        Source file of the module.
    ///                    Used for Fortran modules.
    /// \param LineNo      Source line number of the module.
    ///                    Used for Fortran modules.
    /// \param IsDecl      This is a module declaration; default to false;
    ///                    when set to true, only Scope and Name are required
    ///                    as this entry is just a hint for the debugger to find
    ///                    the corresponding definition in the global scope.
    DIModule *createModule(DIScope *Scope, StringRef Name,
                           StringRef ConfigurationMacros, StringRef IncludePath,
                           StringRef APINotesFile = {}, DIFile *File = nullptr,
                           unsigned LineNo = 0, bool IsDecl = false);

    /// This creates a descriptor for a lexical block with a new file
    /// attached. This merely extends the existing
    /// lexical block as it crosses a file.
    /// \param Scope       Lexical block.
    /// \param File        Source file.
    /// \param Discriminator DWARF path discriminator value.
    DILexicalBlockFile *createLexicalBlockFile(DIScope *Scope, DIFile *File,
                                               unsigned Discriminator = 0);

    /// This creates a descriptor for a lexical block with the
    /// specified parent context.
    /// \param Scope         Parent lexical scope.
    /// \param File          Source file.
    /// \param Line          Line number.
    /// \param Col           Column number.
    DILexicalBlock *createLexicalBlock(DIScope *Scope, DIFile *File,
                                       unsigned Line, unsigned Col);

    /// Create a descriptor for an imported module.
    /// \param Context        The scope this module is imported into
    /// \param NS             The namespace being imported here.
    /// \param File           File where the declaration is located.
    /// \param Line           Line number of the declaration.
    /// \param Elements       Renamed elements.
    DIImportedEntity *createImportedModule(DIScope *Context, DINamespace *NS,
                                           DIFile *File, unsigned Line,
                                           DINodeArray Elements = nullptr);

    /// Create a descriptor for an imported module.
    /// \param Context The scope this module is imported into.
    /// \param NS      An aliased namespace.
    /// \param File    File where the declaration is located.
    /// \param Line    Line number of the declaration.
    /// \param Elements       Renamed elements.
    DIImportedEntity *createImportedModule(DIScope *Context,
                                           DIImportedEntity *NS, DIFile *File,
                                           unsigned Line,
                                           DINodeArray Elements = nullptr);

    /// Create a descriptor for an imported module.
    /// \param Context        The scope this module is imported into.
    /// \param M              The module being imported here
    /// \param File           File where the declaration is located.
    /// \param Line           Line number of the declaration.
    /// \param Elements       Renamed elements.
    DIImportedEntity *createImportedModule(DIScope *Context, DIModule *M,
                                           DIFile *File, unsigned Line,
                                           DINodeArray Elements = nullptr);

    /// Create a descriptor for an imported function.
    /// \param Context The scope this module is imported into.
    /// \param Decl    The declaration (or definition) of a function, type, or
    ///                variable.
    /// \param File    File where the declaration is located.
    /// \param Line    Line number of the declaration.
    /// \param Elements       Renamed elements.
    DIImportedEntity *createImportedDeclaration(DIScope *Context, DINode *Decl,
                                                DIFile *File, unsigned Line,
                                                StringRef Name = "",
                                                DINodeArray Elements = nullptr);

    /// Insert a new llvm.dbg.declare intrinsic call.
    /// \param Storage     llvm::Value of the variable
    /// \param VarInfo     Variable's debug info descriptor.
    /// \param Expr        A complex location expression.
    /// \param DL          Debug info location.
    /// \param InsertAtEnd Location for the new intrinsic.
    DbgInstPtr insertDeclare(llvm::Value *Storage, DILocalVariable *VarInfo,
                             DIExpression *Expr, const DILocation *DL,
                             BasicBlock *InsertAtEnd);

    /// Insert a new llvm.dbg.assign intrinsic call.
    /// \param LinkedInstr   Instruction with a DIAssignID to link with the new
    ///                      intrinsic. The intrinsic will be inserted after
    ///                      this instruction.
    /// \param Val           The value component of this dbg.assign.
    /// \param SrcVar        Variable's debug info descriptor.
    /// \param ValExpr       A complex location expression to modify \p Val.
    /// \param Addr          The address component (store destination).
    /// \param AddrExpr      A complex location expression to modify \p Addr.
    ///                      NOTE: \p ValExpr carries the FragInfo for the
    ///                      variable.
    /// \param DL            Debug info location, usually: (line: 0,
    ///                      column: 0, scope: var-decl-scope). See
    ///                      getDebugValueLoc.
    DbgInstPtr insertDbgAssign(Instruction *LinkedInstr, Value *Val,
                               DILocalVariable *SrcVar, DIExpression *ValExpr,
                               Value *Addr, DIExpression *AddrExpr,
                               const DILocation *DL);

    /// Insert a new llvm.dbg.declare intrinsic call.
    /// \param Storage      llvm::Value of the variable
    /// \param VarInfo      Variable's debug info descriptor.
    /// \param Expr         A complex location expression.
    /// \param DL           Debug info location.
    /// \param InsertBefore Location for the new intrinsic.
    DbgInstPtr insertDeclare(llvm::Value *Storage, DILocalVariable *VarInfo,
                             DIExpression *Expr, const DILocation *DL,
                             Instruction *InsertBefore);

    /// Insert a new llvm.dbg.label intrinsic call.
    /// \param LabelInfo    Label's debug info descriptor.
    /// \param DL           Debug info location.
    /// \param InsertBefore Location for the new intrinsic.
    DbgInstPtr insertLabel(DILabel *LabelInfo, const DILocation *DL,
                           Instruction *InsertBefore);

    /// Insert a new llvm.dbg.label intrinsic call.
    /// \param LabelInfo    Label's debug info descriptor.
    /// \param DL           Debug info location.
    /// \param InsertAtEnd Location for the new intrinsic.
    DbgInstPtr insertLabel(DILabel *LabelInfo, const DILocation *DL,
                           BasicBlock *InsertAtEnd);

    /// Insert a new llvm.dbg.value intrinsic call.
    /// \param Val          llvm::Value of the variable
    /// \param VarInfo      Variable's debug info descriptor.
    /// \param Expr         A complex location expression.
    /// \param DL           Debug info location.
    /// \param InsertAtEnd Location for the new intrinsic.
    DbgInstPtr insertDbgValueIntrinsic(llvm::Value *Val,
                                       DILocalVariable *VarInfo,
                                       DIExpression *Expr, const DILocation *DL,
                                       BasicBlock *InsertAtEnd);

    /// Insert a new llvm.dbg.value intrinsic call.
    /// \param Val          llvm::Value of the variable
    /// \param VarInfo      Variable's debug info descriptor.
    /// \param Expr         A complex location expression.
    /// \param DL           Debug info location.
    /// \param InsertBefore Location for the new intrinsic.
    DbgInstPtr insertDbgValueIntrinsic(llvm::Value *Val,
                                       DILocalVariable *VarInfo,
                                       DIExpression *Expr, const DILocation *DL,
                                       Instruction *InsertBefore);

    /// Replace the vtable holder in the given type.
    ///
    /// If this creates a self reference, it may orphan some unresolved cycles
    /// in the operands of \c T, so \a DIBuilder needs to track that.
    void replaceVTableHolder(DICompositeType *&T,
                             DIType *VTableHolder);

    /// Replace arrays on a composite type.
    ///
    /// If \c T is resolved, but the arrays aren't -- which can happen if \c T
    /// has a self-reference -- \a DIBuilder needs to track the array to
    /// resolve cycles.
    void replaceArrays(DICompositeType *&T, DINodeArray Elements,
                       DINodeArray TParams = DINodeArray());

    /// Replace a temporary node.
    ///
    /// Call \a MDNode::replaceAllUsesWith() on \c N, replacing it with \c
    /// Replacement.
    ///
    /// If \c Replacement is the same as \c N.get(), instead call \a
    /// MDNode::replaceWithUniqued().  In this case, the uniqued node could
    /// have a different address, so we return the final address.
    template <class NodeTy>
    NodeTy *replaceTemporary(TempMDNode &&N, NodeTy *Replacement) {
      if (N.get() == Replacement)
        return cast<NodeTy>(MDNode::replaceWithUniqued(std::move(N)));

      N->replaceAllUsesWith(Replacement);
      return Replacement;
    }
  };

  // Create wrappers for C Binding types (see CBindingWrapping.h).
  DEFINE_ISA_CONVERSION_FUNCTIONS(DIBuilder, LLVMDIBuilderRef)

} // end namespace llvm

#endif // LLVM_IR_DIBUILDER_H
