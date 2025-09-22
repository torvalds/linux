//===-- CodeGenTBAA.cpp - TBAA information for LLVM CodeGen ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the code that manages TBAA information and defines the TBAA policy
// for the optimizer to use. Relevant standards text includes:
//
//   C99 6.5p7
//   C++ [basic.lval] (p10 in n3126, p15 in some earlier versions)
//
//===----------------------------------------------------------------------===//

#include "CodeGenTBAA.h"
#include "ABIInfoImpl.h"
#include "CGCXXABI.h"
#include "CGRecordLayout.h"
#include "CodeGenTypes.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
using namespace clang;
using namespace CodeGen;

CodeGenTBAA::CodeGenTBAA(ASTContext &Ctx, CodeGenTypes &CGTypes,
                         llvm::Module &M, const CodeGenOptions &CGO,
                         const LangOptions &Features)
    : Context(Ctx), CGTypes(CGTypes), Module(M), CodeGenOpts(CGO),
      Features(Features), MDHelper(M.getContext()), Root(nullptr),
      Char(nullptr) {}

CodeGenTBAA::~CodeGenTBAA() {
}

llvm::MDNode *CodeGenTBAA::getRoot() {
  // Define the root of the tree. This identifies the tree, so that
  // if our LLVM IR is linked with LLVM IR from a different front-end
  // (or a different version of this front-end), their TBAA trees will
  // remain distinct, and the optimizer will treat them conservatively.
  if (!Root) {
    if (Features.CPlusPlus)
      Root = MDHelper.createTBAARoot("Simple C++ TBAA");
    else
      Root = MDHelper.createTBAARoot("Simple C/C++ TBAA");
  }

  return Root;
}

llvm::MDNode *CodeGenTBAA::createScalarTypeNode(StringRef Name,
                                                llvm::MDNode *Parent,
                                                uint64_t Size) {
  if (CodeGenOpts.NewStructPathTBAA) {
    llvm::Metadata *Id = MDHelper.createString(Name);
    return MDHelper.createTBAATypeNode(Parent, Size, Id);
  }
  return MDHelper.createTBAAScalarTypeNode(Name, Parent);
}

llvm::MDNode *CodeGenTBAA::getChar() {
  // Define the root of the tree for user-accessible memory. C and C++
  // give special powers to char and certain similar types. However,
  // these special powers only cover user-accessible memory, and doesn't
  // include things like vtables.
  if (!Char)
    Char = createScalarTypeNode("omnipotent char", getRoot(), /* Size= */ 1);

  return Char;
}

static bool TypeHasMayAlias(QualType QTy) {
  // Tagged types have declarations, and therefore may have attributes.
  if (auto *TD = QTy->getAsTagDecl())
    if (TD->hasAttr<MayAliasAttr>())
      return true;

  // Also look for may_alias as a declaration attribute on a typedef.
  // FIXME: We should follow GCC and model may_alias as a type attribute
  // rather than as a declaration attribute.
  while (auto *TT = QTy->getAs<TypedefType>()) {
    if (TT->getDecl()->hasAttr<MayAliasAttr>())
      return true;
    QTy = TT->desugar();
  }
  return false;
}

/// Check if the given type is a valid base type to be used in access tags.
static bool isValidBaseType(QualType QTy) {
  if (const RecordType *TTy = QTy->getAs<RecordType>()) {
    const RecordDecl *RD = TTy->getDecl()->getDefinition();
    // Incomplete types are not valid base access types.
    if (!RD)
      return false;
    if (RD->hasFlexibleArrayMember())
      return false;
    // RD can be struct, union, class, interface or enum.
    // For now, we only handle struct and class.
    if (RD->isStruct() || RD->isClass())
      return true;
  }
  return false;
}

llvm::MDNode *CodeGenTBAA::getTypeInfoHelper(const Type *Ty) {
  uint64_t Size = Context.getTypeSizeInChars(Ty).getQuantity();

  // Handle builtin types.
  if (const BuiltinType *BTy = dyn_cast<BuiltinType>(Ty)) {
    switch (BTy->getKind()) {
    // Character types are special and can alias anything.
    // In C++, this technically only includes "char" and "unsigned char",
    // and not "signed char". In C, it includes all three. For now,
    // the risk of exploiting this detail in C++ seems likely to outweigh
    // the benefit.
    case BuiltinType::Char_U:
    case BuiltinType::Char_S:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
      return getChar();

    // Unsigned types can alias their corresponding signed types.
    case BuiltinType::UShort:
      return getTypeInfo(Context.ShortTy);
    case BuiltinType::UInt:
      return getTypeInfo(Context.IntTy);
    case BuiltinType::ULong:
      return getTypeInfo(Context.LongTy);
    case BuiltinType::ULongLong:
      return getTypeInfo(Context.LongLongTy);
    case BuiltinType::UInt128:
      return getTypeInfo(Context.Int128Ty);

    case BuiltinType::UShortFract:
      return getTypeInfo(Context.ShortFractTy);
    case BuiltinType::UFract:
      return getTypeInfo(Context.FractTy);
    case BuiltinType::ULongFract:
      return getTypeInfo(Context.LongFractTy);

    case BuiltinType::SatUShortFract:
      return getTypeInfo(Context.SatShortFractTy);
    case BuiltinType::SatUFract:
      return getTypeInfo(Context.SatFractTy);
    case BuiltinType::SatULongFract:
      return getTypeInfo(Context.SatLongFractTy);

    case BuiltinType::UShortAccum:
      return getTypeInfo(Context.ShortAccumTy);
    case BuiltinType::UAccum:
      return getTypeInfo(Context.AccumTy);
    case BuiltinType::ULongAccum:
      return getTypeInfo(Context.LongAccumTy);

    case BuiltinType::SatUShortAccum:
      return getTypeInfo(Context.SatShortAccumTy);
    case BuiltinType::SatUAccum:
      return getTypeInfo(Context.SatAccumTy);
    case BuiltinType::SatULongAccum:
      return getTypeInfo(Context.SatLongAccumTy);

    // Treat all other builtin types as distinct types. This includes
    // treating wchar_t, char16_t, and char32_t as distinct from their
    // "underlying types".
    default:
      return createScalarTypeNode(BTy->getName(Features), getChar(), Size);
    }
  }

  // C++1z [basic.lval]p10: "If a program attempts to access the stored value of
  // an object through a glvalue of other than one of the following types the
  // behavior is undefined: [...] a char, unsigned char, or std::byte type."
  if (Ty->isStdByteType())
    return getChar();

  // Handle pointers and references.
  //
  // C has a very strict rule for pointer aliasing. C23 6.7.6.1p2:
  //     For two pointer types to be compatible, both shall be identically
  //     qualified and both shall be pointers to compatible types.
  //
  // This rule is impractically strict; we want to at least ignore CVR
  // qualifiers. Distinguishing by CVR qualifiers would make it UB to
  // e.g. cast a `char **` to `const char * const *` and dereference it,
  // which is too common and useful to invalidate. C++'s similar types
  // rule permits qualifier differences in these nested positions; in fact,
  // C++ even allows that cast as an implicit conversion.
  //
  // Other qualifiers could theoretically be distinguished, especially if
  // they involve a significant representation difference.  We don't
  // currently do so, however.
  //
  // Computing the pointee type string recursively is implicitly more
  // forgiving than the standards require.  Effectively, we are turning
  // the question "are these types compatible/similar" into "are
  // accesses to these types allowed to alias".  In both C and C++,
  // the latter question has special carve-outs for signedness
  // mismatches that only apply at the top level.  As a result, we are
  // allowing e.g. `int *` l-values to access `unsigned *` objects.
  if (Ty->isPointerType() || Ty->isReferenceType()) {
    llvm::MDNode *AnyPtr = createScalarTypeNode("any pointer", getChar(), Size);
    if (!CodeGenOpts.PointerTBAA)
      return AnyPtr;
    // Compute the depth of the pointer and generate a tag of the form "p<depth>
    // <base type tag>".
    unsigned PtrDepth = 0;
    do {
      PtrDepth++;
      Ty = Ty->getPointeeType().getTypePtr();
    } while (Ty->isPointerType());
    // TODO: Implement C++'s type "similarity" and consider dis-"similar"
    // pointers distinct for non-builtin types.
    if (isa<BuiltinType>(Ty)) {
      llvm::MDNode *ScalarMD = getTypeInfoHelper(Ty);
      StringRef Name =
          cast<llvm::MDString>(
              ScalarMD->getOperand(CodeGenOpts.NewStructPathTBAA ? 2 : 0))
              ->getString();
      SmallString<256> OutName("p");
      OutName += std::to_string(PtrDepth);
      OutName += " ";
      OutName += Name;
      return createScalarTypeNode(OutName, AnyPtr, Size);
    }
    return AnyPtr;
  }

  // Accesses to arrays are accesses to objects of their element types.
  if (CodeGenOpts.NewStructPathTBAA && Ty->isArrayType())
    return getTypeInfo(cast<ArrayType>(Ty)->getElementType());

  // Enum types are distinct types. In C++ they have "underlying types",
  // however they aren't related for TBAA.
  if (const EnumType *ETy = dyn_cast<EnumType>(Ty)) {
    if (!Features.CPlusPlus)
      return getTypeInfo(ETy->getDecl()->getIntegerType());

    // In C++ mode, types have linkage, so we can rely on the ODR and
    // on their mangled names, if they're external.
    // TODO: Is there a way to get a program-wide unique name for a
    // decl with local linkage or no linkage?
    if (!ETy->getDecl()->isExternallyVisible())
      return getChar();

    SmallString<256> OutName;
    llvm::raw_svector_ostream Out(OutName);
    CGTypes.getCXXABI().getMangleContext().mangleCanonicalTypeName(
        QualType(ETy, 0), Out);
    return createScalarTypeNode(OutName, getChar(), Size);
  }

  if (const auto *EIT = dyn_cast<BitIntType>(Ty)) {
    SmallString<256> OutName;
    llvm::raw_svector_ostream Out(OutName);
    // Don't specify signed/unsigned since integer types can alias despite sign
    // differences.
    Out << "_BitInt(" << EIT->getNumBits() << ')';
    return createScalarTypeNode(OutName, getChar(), Size);
  }

  // For now, handle any other kind of type conservatively.
  return getChar();
}

llvm::MDNode *CodeGenTBAA::getTypeInfo(QualType QTy) {
  // At -O0 or relaxed aliasing, TBAA is not emitted for regular types.
  if (CodeGenOpts.OptimizationLevel == 0 || CodeGenOpts.RelaxedAliasing)
    return nullptr;

  // If the type has the may_alias attribute (even on a typedef), it is
  // effectively in the general char alias class.
  if (TypeHasMayAlias(QTy))
    return getChar();

  // We need this function to not fall back to returning the "omnipotent char"
  // type node for aggregate and union types. Otherwise, any dereference of an
  // aggregate will result into the may-alias access descriptor, meaning all
  // subsequent accesses to direct and indirect members of that aggregate will
  // be considered may-alias too.
  // TODO: Combine getTypeInfo() and getValidBaseTypeInfo() into a single
  // function.
  if (isValidBaseType(QTy))
    return getValidBaseTypeInfo(QTy);

  const Type *Ty = Context.getCanonicalType(QTy).getTypePtr();
  if (llvm::MDNode *N = MetadataCache[Ty])
    return N;

  // Note that the following helper call is allowed to add new nodes to the
  // cache, which invalidates all its previously obtained iterators. So we
  // first generate the node for the type and then add that node to the cache.
  llvm::MDNode *TypeNode = getTypeInfoHelper(Ty);
  return MetadataCache[Ty] = TypeNode;
}

TBAAAccessInfo CodeGenTBAA::getAccessInfo(QualType AccessType) {
  // Pointee values may have incomplete types, but they shall never be
  // dereferenced.
  if (AccessType->isIncompleteType())
    return TBAAAccessInfo::getIncompleteInfo();

  if (TypeHasMayAlias(AccessType))
    return TBAAAccessInfo::getMayAliasInfo();

  uint64_t Size = Context.getTypeSizeInChars(AccessType).getQuantity();
  return TBAAAccessInfo(getTypeInfo(AccessType), Size);
}

TBAAAccessInfo CodeGenTBAA::getVTablePtrAccessInfo(llvm::Type *VTablePtrType) {
  llvm::DataLayout DL(&Module);
  unsigned Size = DL.getPointerTypeSize(VTablePtrType);
  return TBAAAccessInfo(createScalarTypeNode("vtable pointer", getRoot(), Size),
                        Size);
}

bool
CodeGenTBAA::CollectFields(uint64_t BaseOffset,
                           QualType QTy,
                           SmallVectorImpl<llvm::MDBuilder::TBAAStructField> &
                             Fields,
                           bool MayAlias) {
  /* Things not handled yet include: C++ base classes, bitfields, */

  if (const RecordType *TTy = QTy->getAs<RecordType>()) {
    if (TTy->isUnionType()) {
      uint64_t Size = Context.getTypeSizeInChars(QTy).getQuantity();
      llvm::MDNode *TBAAType = getChar();
      llvm::MDNode *TBAATag = getAccessTagInfo(TBAAAccessInfo(TBAAType, Size));
      Fields.push_back(
          llvm::MDBuilder::TBAAStructField(BaseOffset, Size, TBAATag));
      return true;
    }
    const RecordDecl *RD = TTy->getDecl()->getDefinition();
    if (RD->hasFlexibleArrayMember())
      return false;

    // TODO: Handle C++ base classes.
    if (const CXXRecordDecl *Decl = dyn_cast<CXXRecordDecl>(RD))
      if (Decl->bases_begin() != Decl->bases_end())
        return false;

    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
    const CGRecordLayout &CGRL = CGTypes.getCGRecordLayout(RD);

    unsigned idx = 0;
    for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
         i != e; ++i, ++idx) {
      if (isEmptyFieldForLayout(Context, *i))
        continue;

      uint64_t Offset =
          BaseOffset + Layout.getFieldOffset(idx) / Context.getCharWidth();

      // Create a single field for consecutive named bitfields using char as
      // base type.
      if ((*i)->isBitField()) {
        const CGBitFieldInfo &Info = CGRL.getBitFieldInfo(*i);
        // For big endian targets the first bitfield in the consecutive run is
        // at the most-significant end; see CGRecordLowering::setBitFieldInfo
        // for more information.
        bool IsBE = Context.getTargetInfo().isBigEndian();
        bool IsFirst = IsBE ? Info.StorageSize - (Info.Offset + Info.Size) == 0
                            : Info.Offset == 0;
        if (!IsFirst)
          continue;
        unsigned CurrentBitFieldSize = Info.StorageSize;
        uint64_t Size =
            llvm::divideCeil(CurrentBitFieldSize, Context.getCharWidth());
        llvm::MDNode *TBAAType = getChar();
        llvm::MDNode *TBAATag =
            getAccessTagInfo(TBAAAccessInfo(TBAAType, Size));
        Fields.push_back(
            llvm::MDBuilder::TBAAStructField(Offset, Size, TBAATag));
        continue;
      }

      QualType FieldQTy = i->getType();
      if (!CollectFields(Offset, FieldQTy, Fields,
                         MayAlias || TypeHasMayAlias(FieldQTy)))
        return false;
    }
    return true;
  }

  /* Otherwise, treat whatever it is as a field. */
  uint64_t Offset = BaseOffset;
  uint64_t Size = Context.getTypeSizeInChars(QTy).getQuantity();
  llvm::MDNode *TBAAType = MayAlias ? getChar() : getTypeInfo(QTy);
  llvm::MDNode *TBAATag = getAccessTagInfo(TBAAAccessInfo(TBAAType, Size));
  Fields.push_back(llvm::MDBuilder::TBAAStructField(Offset, Size, TBAATag));
  return true;
}

llvm::MDNode *
CodeGenTBAA::getTBAAStructInfo(QualType QTy) {
  if (CodeGenOpts.OptimizationLevel == 0 || CodeGenOpts.RelaxedAliasing)
    return nullptr;

  const Type *Ty = Context.getCanonicalType(QTy).getTypePtr();

  if (llvm::MDNode *N = StructMetadataCache[Ty])
    return N;

  SmallVector<llvm::MDBuilder::TBAAStructField, 4> Fields;
  if (CollectFields(0, QTy, Fields, TypeHasMayAlias(QTy)))
    return MDHelper.createTBAAStructNode(Fields);

  // For now, handle any other kind of type conservatively.
  return StructMetadataCache[Ty] = nullptr;
}

llvm::MDNode *CodeGenTBAA::getBaseTypeInfoHelper(const Type *Ty) {
  if (auto *TTy = dyn_cast<RecordType>(Ty)) {
    const RecordDecl *RD = TTy->getDecl()->getDefinition();
    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
    using TBAAStructField = llvm::MDBuilder::TBAAStructField;
    SmallVector<TBAAStructField, 4> Fields;
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      // Handle C++ base classes. Non-virtual bases can treated a kind of
      // field. Virtual bases are more complex and omitted, but avoid an
      // incomplete view for NewStructPathTBAA.
      if (CodeGenOpts.NewStructPathTBAA && CXXRD->getNumVBases() != 0)
        return nullptr;
      for (const CXXBaseSpecifier &B : CXXRD->bases()) {
        if (B.isVirtual())
          continue;
        QualType BaseQTy = B.getType();
        const CXXRecordDecl *BaseRD = BaseQTy->getAsCXXRecordDecl();
        if (BaseRD->isEmpty())
          continue;
        llvm::MDNode *TypeNode = isValidBaseType(BaseQTy)
                                     ? getValidBaseTypeInfo(BaseQTy)
                                     : getTypeInfo(BaseQTy);
        if (!TypeNode)
          return nullptr;
        uint64_t Offset = Layout.getBaseClassOffset(BaseRD).getQuantity();
        uint64_t Size =
            Context.getASTRecordLayout(BaseRD).getDataSize().getQuantity();
        Fields.push_back(
            llvm::MDBuilder::TBAAStructField(Offset, Size, TypeNode));
      }
      // The order in which base class subobjects are allocated is unspecified,
      // so may differ from declaration order. In particular, Itanium ABI will
      // allocate a primary base first.
      // Since we exclude empty subobjects, the objects are not overlapping and
      // their offsets are unique.
      llvm::sort(Fields,
                 [](const TBAAStructField &A, const TBAAStructField &B) {
                   return A.Offset < B.Offset;
                 });
    }
    for (FieldDecl *Field : RD->fields()) {
      if (Field->isZeroSize(Context) || Field->isUnnamedBitField())
        continue;
      QualType FieldQTy = Field->getType();
      llvm::MDNode *TypeNode = isValidBaseType(FieldQTy)
                                   ? getValidBaseTypeInfo(FieldQTy)
                                   : getTypeInfo(FieldQTy);
      if (!TypeNode)
        return nullptr;

      uint64_t BitOffset = Layout.getFieldOffset(Field->getFieldIndex());
      uint64_t Offset = Context.toCharUnitsFromBits(BitOffset).getQuantity();
      uint64_t Size = Context.getTypeSizeInChars(FieldQTy).getQuantity();
      Fields.push_back(llvm::MDBuilder::TBAAStructField(Offset, Size,
                                                        TypeNode));
    }

    SmallString<256> OutName;
    if (Features.CPlusPlus) {
      // Don't use the mangler for C code.
      llvm::raw_svector_ostream Out(OutName);
      CGTypes.getCXXABI().getMangleContext().mangleCanonicalTypeName(
          QualType(Ty, 0), Out);
    } else {
      OutName = RD->getName();
    }

    if (CodeGenOpts.NewStructPathTBAA) {
      llvm::MDNode *Parent = getChar();
      uint64_t Size = Context.getTypeSizeInChars(Ty).getQuantity();
      llvm::Metadata *Id = MDHelper.createString(OutName);
      return MDHelper.createTBAATypeNode(Parent, Size, Id, Fields);
    }

    // Create the struct type node with a vector of pairs (offset, type).
    SmallVector<std::pair<llvm::MDNode*, uint64_t>, 4> OffsetsAndTypes;
    for (const auto &Field : Fields)
        OffsetsAndTypes.push_back(std::make_pair(Field.Type, Field.Offset));
    return MDHelper.createTBAAStructTypeNode(OutName, OffsetsAndTypes);
  }

  return nullptr;
}

llvm::MDNode *CodeGenTBAA::getValidBaseTypeInfo(QualType QTy) {
  assert(isValidBaseType(QTy) && "Must be a valid base type");

  const Type *Ty = Context.getCanonicalType(QTy).getTypePtr();

  // nullptr is a valid value in the cache, so use find rather than []
  auto I = BaseTypeMetadataCache.find(Ty);
  if (I != BaseTypeMetadataCache.end())
    return I->second;

  // First calculate the metadata, before recomputing the insertion point, as
  // the helper can recursively call us.
  llvm::MDNode *TypeNode = getBaseTypeInfoHelper(Ty);
  LLVM_ATTRIBUTE_UNUSED auto inserted =
      BaseTypeMetadataCache.insert({Ty, TypeNode});
  assert(inserted.second && "BaseType metadata was already inserted");

  return TypeNode;
}

llvm::MDNode *CodeGenTBAA::getBaseTypeInfo(QualType QTy) {
  return isValidBaseType(QTy) ? getValidBaseTypeInfo(QTy) : nullptr;
}

llvm::MDNode *CodeGenTBAA::getAccessTagInfo(TBAAAccessInfo Info) {
  assert(!Info.isIncomplete() && "Access to an object of an incomplete type!");

  if (Info.isMayAlias())
    Info = TBAAAccessInfo(getChar(), Info.Size);

  if (!Info.AccessType)
    return nullptr;

  if (!CodeGenOpts.StructPathTBAA)
    Info = TBAAAccessInfo(Info.AccessType, Info.Size);

  llvm::MDNode *&N = AccessTagMetadataCache[Info];
  if (N)
    return N;

  if (!Info.BaseType) {
    Info.BaseType = Info.AccessType;
    assert(!Info.Offset && "Nonzero offset for an access with no base type!");
  }
  if (CodeGenOpts.NewStructPathTBAA) {
    return N = MDHelper.createTBAAAccessTag(Info.BaseType, Info.AccessType,
                                            Info.Offset, Info.Size);
  }
  return N = MDHelper.createTBAAStructTagNode(Info.BaseType, Info.AccessType,
                                              Info.Offset);
}

TBAAAccessInfo CodeGenTBAA::mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                                 TBAAAccessInfo TargetInfo) {
  if (SourceInfo.isMayAlias() || TargetInfo.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();
  return TargetInfo;
}

TBAAAccessInfo
CodeGenTBAA::mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                 TBAAAccessInfo InfoB) {
  if (InfoA == InfoB)
    return InfoA;

  if (!InfoA || !InfoB)
    return TBAAAccessInfo();

  if (InfoA.isMayAlias() || InfoB.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();

  // TODO: Implement the rest of the logic here. For example, two accesses
  // with same final access types result in an access to an object of that final
  // access type regardless of their base types.
  return TBAAAccessInfo::getMayAliasInfo();
}

TBAAAccessInfo
CodeGenTBAA::mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                            TBAAAccessInfo SrcInfo) {
  if (DestInfo == SrcInfo)
    return DestInfo;

  if (!DestInfo || !SrcInfo)
    return TBAAAccessInfo();

  if (DestInfo.isMayAlias() || SrcInfo.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();

  // TODO: Implement the rest of the logic here. For example, two accesses
  // with same final access types result in an access to an object of that final
  // access type regardless of their base types.
  return TBAAAccessInfo::getMayAliasInfo();
}
