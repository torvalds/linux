//===--- CodeGenTBAA.h - TBAA information for LLVM CodeGen ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the code that manages TBAA information and defines the TBAA policy
// for the optimizer to use.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENTBAA_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENTBAA_H

#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"

namespace clang {
  class ASTContext;
  class CodeGenOptions;
  class LangOptions;
  class QualType;
  class Type;

namespace CodeGen {
class CodeGenTypes;

// TBAAAccessKind - A kind of TBAA memory access descriptor.
enum class TBAAAccessKind : unsigned {
  Ordinary,
  MayAlias,
  Incomplete,
};

// TBAAAccessInfo - Describes a memory access in terms of TBAA.
struct TBAAAccessInfo {
  TBAAAccessInfo(TBAAAccessKind Kind, llvm::MDNode *BaseType,
                 llvm::MDNode *AccessType, uint64_t Offset, uint64_t Size)
    : Kind(Kind), BaseType(BaseType), AccessType(AccessType),
      Offset(Offset), Size(Size)
  {}

  TBAAAccessInfo(llvm::MDNode *BaseType, llvm::MDNode *AccessType,
                 uint64_t Offset, uint64_t Size)
    : TBAAAccessInfo(TBAAAccessKind::Ordinary, BaseType, AccessType,
                     Offset, Size)
  {}

  explicit TBAAAccessInfo(llvm::MDNode *AccessType, uint64_t Size)
    : TBAAAccessInfo(/* BaseType= */ nullptr, AccessType, /* Offset= */ 0, Size)
  {}

  TBAAAccessInfo()
    : TBAAAccessInfo(/* AccessType= */ nullptr, /* Size= */ 0)
  {}

  static TBAAAccessInfo getMayAliasInfo() {
    return TBAAAccessInfo(TBAAAccessKind::MayAlias,
                          /* BaseType= */ nullptr, /* AccessType= */ nullptr,
                          /* Offset= */ 0, /* Size= */ 0);
  }

  bool isMayAlias() const { return Kind == TBAAAccessKind::MayAlias; }

  static TBAAAccessInfo getIncompleteInfo() {
    return TBAAAccessInfo(TBAAAccessKind::Incomplete,
                          /* BaseType= */ nullptr, /* AccessType= */ nullptr,
                          /* Offset= */ 0, /* Size= */ 0);
  }

  bool isIncomplete() const { return Kind == TBAAAccessKind::Incomplete; }

  bool operator==(const TBAAAccessInfo &Other) const {
    return Kind == Other.Kind &&
           BaseType == Other.BaseType &&
           AccessType == Other.AccessType &&
           Offset == Other.Offset &&
           Size == Other.Size;
  }

  bool operator!=(const TBAAAccessInfo &Other) const {
    return !(*this == Other);
  }

  explicit operator bool() const {
    return *this != TBAAAccessInfo();
  }

  /// Kind - The kind of the access descriptor.
  TBAAAccessKind Kind;

  /// BaseType - The base/leading access type. May be null if this access
  /// descriptor represents an access that is not considered to be an access
  /// to an aggregate or union member.
  llvm::MDNode *BaseType;

  /// AccessType - The final access type. May be null if there is no TBAA
  /// information available about this access.
  llvm::MDNode *AccessType;

  /// Offset - The byte offset of the final access within the base one. Must be
  /// zero if the base access type is not specified.
  uint64_t Offset;

  /// Size - The size of access, in bytes.
  uint64_t Size;
};

/// CodeGenTBAA - This class organizes the cross-module state that is used
/// while lowering AST types to LLVM types.
class CodeGenTBAA {
  ASTContext &Context;
  CodeGenTypes &CGTypes;
  llvm::Module &Module;
  const CodeGenOptions &CodeGenOpts;
  const LangOptions &Features;

  // MDHelper - Helper for creating metadata.
  llvm::MDBuilder MDHelper;

  /// MetadataCache - This maps clang::Types to scalar llvm::MDNodes describing
  /// them.
  llvm::DenseMap<const Type *, llvm::MDNode *> MetadataCache;
  /// This maps clang::Types to a base access type in the type DAG.
  llvm::DenseMap<const Type *, llvm::MDNode *> BaseTypeMetadataCache;
  /// This maps TBAA access descriptors to tag nodes.
  llvm::DenseMap<TBAAAccessInfo, llvm::MDNode *> AccessTagMetadataCache;

  /// StructMetadataCache - This maps clang::Types to llvm::MDNodes describing
  /// them for struct assignments.
  llvm::DenseMap<const Type *, llvm::MDNode *> StructMetadataCache;

  llvm::MDNode *Root;
  llvm::MDNode *Char;

  /// getRoot - This is the mdnode for the root of the metadata type graph
  /// for this translation unit.
  llvm::MDNode *getRoot();

  /// getChar - This is the mdnode for "char", which is special, and any types
  /// considered to be equivalent to it.
  llvm::MDNode *getChar();

  /// CollectFields - Collect information about the fields of a type for
  /// !tbaa.struct metadata formation. Return false for an unsupported type.
  bool CollectFields(uint64_t BaseOffset,
                     QualType Ty,
                     SmallVectorImpl<llvm::MDBuilder::TBAAStructField> &Fields,
                     bool MayAlias);

  /// createScalarTypeNode - A wrapper function to create a metadata node
  /// describing a scalar type.
  llvm::MDNode *createScalarTypeNode(StringRef Name, llvm::MDNode *Parent,
                                     uint64_t Size);

  /// getTypeInfoHelper - An internal helper function to generate metadata used
  /// to describe accesses to objects of the given type.
  llvm::MDNode *getTypeInfoHelper(const Type *Ty);

  /// getBaseTypeInfoHelper - An internal helper function to generate metadata
  /// used to describe accesses to objects of the given base type.
  llvm::MDNode *getBaseTypeInfoHelper(const Type *Ty);

  /// getValidBaseTypeInfo - Return metadata that describes the given base
  /// access type. The type must be suitable.
  llvm::MDNode *getValidBaseTypeInfo(QualType QTy);

public:
  CodeGenTBAA(ASTContext &Ctx, CodeGenTypes &CGTypes, llvm::Module &M,
              const CodeGenOptions &CGO, const LangOptions &Features);
  ~CodeGenTBAA();

  /// getTypeInfo - Get metadata used to describe accesses to objects of the
  /// given type.
  llvm::MDNode *getTypeInfo(QualType QTy);

  /// getAccessInfo - Get TBAA information that describes an access to
  /// an object of the given type.
  TBAAAccessInfo getAccessInfo(QualType AccessType);

  /// getVTablePtrAccessInfo - Get the TBAA information that describes an
  /// access to a virtual table pointer.
  TBAAAccessInfo getVTablePtrAccessInfo(llvm::Type *VTablePtrType);

  /// getTBAAStructInfo - Get the TBAAStruct MDNode to be used for a memcpy of
  /// the given type.
  llvm::MDNode *getTBAAStructInfo(QualType QTy);

  /// getBaseTypeInfo - Get metadata that describes the given base access
  /// type. Return null if the type is not suitable for use in TBAA access
  /// tags.
  llvm::MDNode *getBaseTypeInfo(QualType QTy);

  /// getAccessTagInfo - Get TBAA tag for a given memory access.
  llvm::MDNode *getAccessTagInfo(TBAAAccessInfo Info);

  /// mergeTBAAInfoForCast - Get merged TBAA information for the purpose of
  /// type casts.
  TBAAAccessInfo mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                      TBAAAccessInfo TargetInfo);

  /// mergeTBAAInfoForConditionalOperator - Get merged TBAA information for the
  /// purpose of conditional operator.
  TBAAAccessInfo mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                     TBAAAccessInfo InfoB);

  /// mergeTBAAInfoForMemoryTransfer - Get merged TBAA information for the
  /// purpose of memory transfer calls.
  TBAAAccessInfo mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                                TBAAAccessInfo SrcInfo);
};

}  // end namespace CodeGen
}  // end namespace clang

namespace llvm {

template<> struct DenseMapInfo<clang::CodeGen::TBAAAccessInfo> {
  static clang::CodeGen::TBAAAccessInfo getEmptyKey() {
    unsigned UnsignedKey = DenseMapInfo<unsigned>::getEmptyKey();
    return clang::CodeGen::TBAAAccessInfo(
      static_cast<clang::CodeGen::TBAAAccessKind>(UnsignedKey),
      DenseMapInfo<MDNode *>::getEmptyKey(),
      DenseMapInfo<MDNode *>::getEmptyKey(),
      DenseMapInfo<uint64_t>::getEmptyKey(),
      DenseMapInfo<uint64_t>::getEmptyKey());
  }

  static clang::CodeGen::TBAAAccessInfo getTombstoneKey() {
    unsigned UnsignedKey = DenseMapInfo<unsigned>::getTombstoneKey();
    return clang::CodeGen::TBAAAccessInfo(
      static_cast<clang::CodeGen::TBAAAccessKind>(UnsignedKey),
      DenseMapInfo<MDNode *>::getTombstoneKey(),
      DenseMapInfo<MDNode *>::getTombstoneKey(),
      DenseMapInfo<uint64_t>::getTombstoneKey(),
      DenseMapInfo<uint64_t>::getTombstoneKey());
  }

  static unsigned getHashValue(const clang::CodeGen::TBAAAccessInfo &Val) {
    auto KindValue = static_cast<unsigned>(Val.Kind);
    return DenseMapInfo<unsigned>::getHashValue(KindValue) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.BaseType) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.AccessType) ^
           DenseMapInfo<uint64_t>::getHashValue(Val.Offset) ^
           DenseMapInfo<uint64_t>::getHashValue(Val.Size);
  }

  static bool isEqual(const clang::CodeGen::TBAAAccessInfo &LHS,
                      const clang::CodeGen::TBAAAccessInfo &RHS) {
    return LHS == RHS;
  }
};

}  // end namespace llvm

#endif
