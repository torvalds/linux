//===- XCore.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// XCore ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

/// A SmallStringEnc instance is used to build up the TypeString by passing
/// it by reference between functions that append to it.
typedef llvm::SmallString<128> SmallStringEnc;

/// TypeStringCache caches the meta encodings of Types.
///
/// The reason for caching TypeStrings is two fold:
///   1. To cache a type's encoding for later uses;
///   2. As a means to break recursive member type inclusion.
///
/// A cache Entry can have a Status of:
///   NonRecursive:   The type encoding is not recursive;
///   Recursive:      The type encoding is recursive;
///   Incomplete:     An incomplete TypeString;
///   IncompleteUsed: An incomplete TypeString that has been used in a
///                   Recursive type encoding.
///
/// A NonRecursive entry will have all of its sub-members expanded as fully
/// as possible. Whilst it may contain types which are recursive, the type
/// itself is not recursive and thus its encoding may be safely used whenever
/// the type is encountered.
///
/// A Recursive entry will have all of its sub-members expanded as fully as
/// possible. The type itself is recursive and it may contain other types which
/// are recursive. The Recursive encoding must not be used during the expansion
/// of a recursive type's recursive branch. For simplicity the code uses
/// IncompleteCount to reject all usage of Recursive encodings for member types.
///
/// An Incomplete entry is always a RecordType and only encodes its
/// identifier e.g. "s(S){}". Incomplete 'StubEnc' entries are ephemeral and
/// are placed into the cache during type expansion as a means to identify and
/// handle recursive inclusion of types as sub-members. If there is recursion
/// the entry becomes IncompleteUsed.
///
/// During the expansion of a RecordType's members:
///
///   If the cache contains a NonRecursive encoding for the member type, the
///   cached encoding is used;
///
///   If the cache contains a Recursive encoding for the member type, the
///   cached encoding is 'Swapped' out, as it may be incorrect, and...
///
///   If the member is a RecordType, an Incomplete encoding is placed into the
///   cache to break potential recursive inclusion of itself as a sub-member;
///
///   Once a member RecordType has been expanded, its temporary incomplete
///   entry is removed from the cache. If a Recursive encoding was swapped out
///   it is swapped back in;
///
///   If an incomplete entry is used to expand a sub-member, the incomplete
///   entry is marked as IncompleteUsed. The cache keeps count of how many
///   IncompleteUsed entries it currently contains in IncompleteUsedCount;
///
///   If a member's encoding is found to be a NonRecursive or Recursive viz:
///   IncompleteUsedCount==0, the member's encoding is added to the cache.
///   Else the member is part of a recursive type and thus the recursion has
///   been exited too soon for the encoding to be correct for the member.
///
class TypeStringCache {
  enum Status {NonRecursive, Recursive, Incomplete, IncompleteUsed};
  struct Entry {
    std::string Str;     // The encoded TypeString for the type.
    enum Status State;   // Information about the encoding in 'Str'.
    std::string Swapped; // A temporary place holder for a Recursive encoding
                         // during the expansion of RecordType's members.
  };
  std::map<const IdentifierInfo *, struct Entry> Map;
  unsigned IncompleteCount;     // Number of Incomplete entries in the Map.
  unsigned IncompleteUsedCount; // Number of IncompleteUsed entries in the Map.
public:
  TypeStringCache() : IncompleteCount(0), IncompleteUsedCount(0) {}
  void addIncomplete(const IdentifierInfo *ID, std::string StubEnc);
  bool removeIncomplete(const IdentifierInfo *ID);
  void addIfComplete(const IdentifierInfo *ID, StringRef Str,
                     bool IsRecursive);
  StringRef lookupStr(const IdentifierInfo *ID);
};

/// TypeString encodings for enum & union fields must be order.
/// FieldEncoding is a helper for this ordering process.
class FieldEncoding {
  bool HasName;
  std::string Enc;
public:
  FieldEncoding(bool b, SmallStringEnc &e) : HasName(b), Enc(e.c_str()) {}
  StringRef str() { return Enc; }
  bool operator<(const FieldEncoding &rhs) const {
    if (HasName != rhs.HasName) return HasName;
    return Enc < rhs.Enc;
  }
};

class XCoreABIInfo : public DefaultABIInfo {
public:
  XCoreABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class XCoreTargetCodeGenInfo : public TargetCodeGenInfo {
  mutable TypeStringCache TSC;
  void emitTargetMD(const Decl *D, llvm::GlobalValue *GV,
                    const CodeGen::CodeGenModule &M) const;

public:
  XCoreTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<XCoreABIInfo>(CGT)) {}
  void emitTargetMetadata(CodeGen::CodeGenModule &CGM,
                          const llvm::MapVector<GlobalDecl, StringRef>
                              &MangledDeclNames) const override;
};

} // End anonymous namespace.

// TODO: this implementation is likely now redundant with the default
// EmitVAArg.
RValue XCoreABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                               QualType Ty, AggValueSlot Slot) const {
  CGBuilderTy &Builder = CGF.Builder;

  // Get the VAList.
  CharUnits SlotSize = CharUnits::fromQuantity(4);
  Address AP = Address(Builder.CreateLoad(VAListAddr),
                       getVAListElementType(CGF), SlotSize);

  // Handle the argument.
  ABIArgInfo AI = classifyArgumentType(Ty);
  CharUnits TypeAlign = getContext().getTypeAlignInChars(Ty);
  llvm::Type *ArgTy = CGT.ConvertType(Ty);
  if (AI.canHaveCoerceToType() && !AI.getCoerceToType())
    AI.setCoerceToType(ArgTy);
  llvm::Type *ArgPtrTy = llvm::PointerType::getUnqual(ArgTy);

  Address Val = Address::invalid();
  CharUnits ArgSize = CharUnits::Zero();
  switch (AI.getKind()) {
  case ABIArgInfo::Expand:
  case ABIArgInfo::CoerceAndExpand:
  case ABIArgInfo::InAlloca:
    llvm_unreachable("Unsupported ABI kind for va_arg");
  case ABIArgInfo::Ignore:
    Val = Address(llvm::UndefValue::get(ArgPtrTy), ArgTy, TypeAlign);
    ArgSize = CharUnits::Zero();
    break;
  case ABIArgInfo::Extend:
  case ABIArgInfo::Direct:
    Val = AP.withElementType(ArgTy);
    ArgSize = CharUnits::fromQuantity(
        getDataLayout().getTypeAllocSize(AI.getCoerceToType()));
    ArgSize = ArgSize.alignTo(SlotSize);
    break;
  case ABIArgInfo::Indirect:
  case ABIArgInfo::IndirectAliased:
    Val = AP.withElementType(ArgPtrTy);
    Val = Address(Builder.CreateLoad(Val), ArgTy, TypeAlign);
    ArgSize = SlotSize;
    break;
  }

  // Increment the VAList.
  if (!ArgSize.isZero()) {
    Address APN = Builder.CreateConstInBoundsByteGEP(AP, ArgSize);
    Builder.CreateStore(APN.emitRawPointer(CGF), VAListAddr);
  }

  return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(Val, Ty), Slot);
}

/// During the expansion of a RecordType, an incomplete TypeString is placed
/// into the cache as a means to identify and break recursion.
/// If there is a Recursive encoding in the cache, it is swapped out and will
/// be reinserted by removeIncomplete().
/// All other types of encoding should have been used rather than arriving here.
void TypeStringCache::addIncomplete(const IdentifierInfo *ID,
                                    std::string StubEnc) {
  if (!ID)
    return;
  Entry &E = Map[ID];
  assert( (E.Str.empty() || E.State == Recursive) &&
         "Incorrectly use of addIncomplete");
  assert(!StubEnc.empty() && "Passing an empty string to addIncomplete()");
  E.Swapped.swap(E.Str); // swap out the Recursive
  E.Str.swap(StubEnc);
  E.State = Incomplete;
  ++IncompleteCount;
}

/// Once the RecordType has been expanded, the temporary incomplete TypeString
/// must be removed from the cache.
/// If a Recursive was swapped out by addIncomplete(), it will be replaced.
/// Returns true if the RecordType was defined recursively.
bool TypeStringCache::removeIncomplete(const IdentifierInfo *ID) {
  if (!ID)
    return false;
  auto I = Map.find(ID);
  assert(I != Map.end() && "Entry not present");
  Entry &E = I->second;
  assert( (E.State == Incomplete ||
           E.State == IncompleteUsed) &&
         "Entry must be an incomplete type");
  bool IsRecursive = false;
  if (E.State == IncompleteUsed) {
    // We made use of our Incomplete encoding, thus we are recursive.
    IsRecursive = true;
    --IncompleteUsedCount;
  }
  if (E.Swapped.empty())
    Map.erase(I);
  else {
    // Swap the Recursive back.
    E.Swapped.swap(E.Str);
    E.Swapped.clear();
    E.State = Recursive;
  }
  --IncompleteCount;
  return IsRecursive;
}

/// Add the encoded TypeString to the cache only if it is NonRecursive or
/// Recursive (viz: all sub-members were expanded as fully as possible).
void TypeStringCache::addIfComplete(const IdentifierInfo *ID, StringRef Str,
                                    bool IsRecursive) {
  if (!ID || IncompleteUsedCount)
    return; // No key or it is an incomplete sub-type so don't add.
  Entry &E = Map[ID];
  if (IsRecursive && !E.Str.empty()) {
    assert(E.State==Recursive && E.Str.size() == Str.size() &&
           "This is not the same Recursive entry");
    // The parent container was not recursive after all, so we could have used
    // this Recursive sub-member entry after all, but we assumed the worse when
    // we started viz: IncompleteCount!=0.
    return;
  }
  assert(E.Str.empty() && "Entry already present");
  E.Str = Str.str();
  E.State = IsRecursive? Recursive : NonRecursive;
}

/// Return a cached TypeString encoding for the ID. If there isn't one, or we
/// are recursively expanding a type (IncompleteCount != 0) and the cached
/// encoding is Recursive, return an empty StringRef.
StringRef TypeStringCache::lookupStr(const IdentifierInfo *ID) {
  if (!ID)
    return StringRef();   // We have no key.
  auto I = Map.find(ID);
  if (I == Map.end())
    return StringRef();   // We have no encoding.
  Entry &E = I->second;
  if (E.State == Recursive && IncompleteCount)
    return StringRef();   // We don't use Recursive encodings for member types.

  if (E.State == Incomplete) {
    // The incomplete type is being used to break out of recursion.
    E.State = IncompleteUsed;
    ++IncompleteUsedCount;
  }
  return E.Str;
}

/// The XCore ABI includes a type information section that communicates symbol
/// type information to the linker. The linker uses this information to verify
/// safety/correctness of things such as array bound and pointers et al.
/// The ABI only requires C (and XC) language modules to emit TypeStrings.
/// This type information (TypeString) is emitted into meta data for all global
/// symbols: definitions, declarations, functions & variables.
///
/// The TypeString carries type, qualifier, name, size & value details.
/// Please see 'Tools Development Guide' section 2.16.2 for format details:
/// https://www.xmos.com/download/public/Tools-Development-Guide%28X9114A%29.pdf
/// The output is tested by test/CodeGen/xcore-stringtype.c.
///
static bool getTypeString(SmallStringEnc &Enc, const Decl *D,
                          const CodeGen::CodeGenModule &CGM,
                          TypeStringCache &TSC);

/// XCore uses emitTargetMD to emit TypeString metadata for global symbols.
void XCoreTargetCodeGenInfo::emitTargetMD(
    const Decl *D, llvm::GlobalValue *GV,
    const CodeGen::CodeGenModule &CGM) const {
  SmallStringEnc Enc;
  if (getTypeString(Enc, D, CGM, TSC)) {
    llvm::LLVMContext &Ctx = CGM.getModule().getContext();
    llvm::Metadata *MDVals[] = {llvm::ConstantAsMetadata::get(GV),
                                llvm::MDString::get(Ctx, Enc.str())};
    llvm::NamedMDNode *MD =
      CGM.getModule().getOrInsertNamedMetadata("xcore.typestrings");
    MD->addOperand(llvm::MDNode::get(Ctx, MDVals));
  }
}

void XCoreTargetCodeGenInfo::emitTargetMetadata(
    CodeGen::CodeGenModule &CGM,
    const llvm::MapVector<GlobalDecl, StringRef> &MangledDeclNames) const {
  // Warning, new MangledDeclNames may be appended within this loop.
  // We rely on MapVector insertions adding new elements to the end
  // of the container.
  for (unsigned I = 0; I != MangledDeclNames.size(); ++I) {
    auto Val = *(MangledDeclNames.begin() + I);
    llvm::GlobalValue *GV = CGM.GetGlobalValue(Val.second);
    if (GV) {
      const Decl *D = Val.first.getDecl()->getMostRecentDecl();
      emitTargetMD(D, GV, CGM);
    }
  }
}

static bool appendType(SmallStringEnc &Enc, QualType QType,
                       const CodeGen::CodeGenModule &CGM,
                       TypeStringCache &TSC);

/// Helper function for appendRecordType().
/// Builds a SmallVector containing the encoded field types in declaration
/// order.
static bool extractFieldType(SmallVectorImpl<FieldEncoding> &FE,
                             const RecordDecl *RD,
                             const CodeGen::CodeGenModule &CGM,
                             TypeStringCache &TSC) {
  for (const auto *Field : RD->fields()) {
    SmallStringEnc Enc;
    Enc += "m(";
    Enc += Field->getName();
    Enc += "){";
    if (Field->isBitField()) {
      Enc += "b(";
      llvm::raw_svector_ostream OS(Enc);
      OS << Field->getBitWidthValue(CGM.getContext());
      Enc += ':';
    }
    if (!appendType(Enc, Field->getType(), CGM, TSC))
      return false;
    if (Field->isBitField())
      Enc += ')';
    Enc += '}';
    FE.emplace_back(!Field->getName().empty(), Enc);
  }
  return true;
}

/// Appends structure and union types to Enc and adds encoding to cache.
/// Recursively calls appendType (via extractFieldType) for each field.
/// Union types have their fields ordered according to the ABI.
static bool appendRecordType(SmallStringEnc &Enc, const RecordType *RT,
                             const CodeGen::CodeGenModule &CGM,
                             TypeStringCache &TSC, const IdentifierInfo *ID) {
  // Append the cached TypeString if we have one.
  StringRef TypeString = TSC.lookupStr(ID);
  if (!TypeString.empty()) {
    Enc += TypeString;
    return true;
  }

  // Start to emit an incomplete TypeString.
  size_t Start = Enc.size();
  Enc += (RT->isUnionType()? 'u' : 's');
  Enc += '(';
  if (ID)
    Enc += ID->getName();
  Enc += "){";

  // We collect all encoded fields and order as necessary.
  bool IsRecursive = false;
  const RecordDecl *RD = RT->getDecl()->getDefinition();
  if (RD && !RD->field_empty()) {
    // An incomplete TypeString stub is placed in the cache for this RecordType
    // so that recursive calls to this RecordType will use it whilst building a
    // complete TypeString for this RecordType.
    SmallVector<FieldEncoding, 16> FE;
    std::string StubEnc(Enc.substr(Start).str());
    StubEnc += '}';  // StubEnc now holds a valid incomplete TypeString.
    TSC.addIncomplete(ID, std::move(StubEnc));
    if (!extractFieldType(FE, RD, CGM, TSC)) {
      (void) TSC.removeIncomplete(ID);
      return false;
    }
    IsRecursive = TSC.removeIncomplete(ID);
    // The ABI requires unions to be sorted but not structures.
    // See FieldEncoding::operator< for sort algorithm.
    if (RT->isUnionType())
      llvm::sort(FE);
    // We can now complete the TypeString.
    unsigned E = FE.size();
    for (unsigned I = 0; I != E; ++I) {
      if (I)
        Enc += ',';
      Enc += FE[I].str();
    }
  }
  Enc += '}';
  TSC.addIfComplete(ID, Enc.substr(Start), IsRecursive);
  return true;
}

/// Appends enum types to Enc and adds the encoding to the cache.
static bool appendEnumType(SmallStringEnc &Enc, const EnumType *ET,
                           TypeStringCache &TSC,
                           const IdentifierInfo *ID) {
  // Append the cached TypeString if we have one.
  StringRef TypeString = TSC.lookupStr(ID);
  if (!TypeString.empty()) {
    Enc += TypeString;
    return true;
  }

  size_t Start = Enc.size();
  Enc += "e(";
  if (ID)
    Enc += ID->getName();
  Enc += "){";

  // We collect all encoded enumerations and order them alphanumerically.
  if (const EnumDecl *ED = ET->getDecl()->getDefinition()) {
    SmallVector<FieldEncoding, 16> FE;
    for (auto I = ED->enumerator_begin(), E = ED->enumerator_end(); I != E;
         ++I) {
      SmallStringEnc EnumEnc;
      EnumEnc += "m(";
      EnumEnc += I->getName();
      EnumEnc += "){";
      I->getInitVal().toString(EnumEnc);
      EnumEnc += '}';
      FE.push_back(FieldEncoding(!I->getName().empty(), EnumEnc));
    }
    llvm::sort(FE);
    unsigned E = FE.size();
    for (unsigned I = 0; I != E; ++I) {
      if (I)
        Enc += ',';
      Enc += FE[I].str();
    }
  }
  Enc += '}';
  TSC.addIfComplete(ID, Enc.substr(Start), false);
  return true;
}

/// Appends type's qualifier to Enc.
/// This is done prior to appending the type's encoding.
static void appendQualifier(SmallStringEnc &Enc, QualType QT) {
  // Qualifiers are emitted in alphabetical order.
  static const char *const Table[]={"","c:","r:","cr:","v:","cv:","rv:","crv:"};
  int Lookup = 0;
  if (QT.isConstQualified())
    Lookup += 1<<0;
  if (QT.isRestrictQualified())
    Lookup += 1<<1;
  if (QT.isVolatileQualified())
    Lookup += 1<<2;
  Enc += Table[Lookup];
}

/// Appends built-in types to Enc.
static bool appendBuiltinType(SmallStringEnc &Enc, const BuiltinType *BT) {
  const char *EncType;
  switch (BT->getKind()) {
    case BuiltinType::Void:
      EncType = "0";
      break;
    case BuiltinType::Bool:
      EncType = "b";
      break;
    case BuiltinType::Char_U:
      EncType = "uc";
      break;
    case BuiltinType::UChar:
      EncType = "uc";
      break;
    case BuiltinType::SChar:
      EncType = "sc";
      break;
    case BuiltinType::UShort:
      EncType = "us";
      break;
    case BuiltinType::Short:
      EncType = "ss";
      break;
    case BuiltinType::UInt:
      EncType = "ui";
      break;
    case BuiltinType::Int:
      EncType = "si";
      break;
    case BuiltinType::ULong:
      EncType = "ul";
      break;
    case BuiltinType::Long:
      EncType = "sl";
      break;
    case BuiltinType::ULongLong:
      EncType = "ull";
      break;
    case BuiltinType::LongLong:
      EncType = "sll";
      break;
    case BuiltinType::Float:
      EncType = "ft";
      break;
    case BuiltinType::Double:
      EncType = "d";
      break;
    case BuiltinType::LongDouble:
      EncType = "ld";
      break;
    default:
      return false;
  }
  Enc += EncType;
  return true;
}

/// Appends a pointer encoding to Enc before calling appendType for the pointee.
static bool appendPointerType(SmallStringEnc &Enc, const PointerType *PT,
                              const CodeGen::CodeGenModule &CGM,
                              TypeStringCache &TSC) {
  Enc += "p(";
  if (!appendType(Enc, PT->getPointeeType(), CGM, TSC))
    return false;
  Enc += ')';
  return true;
}

/// Appends array encoding to Enc before calling appendType for the element.
static bool appendArrayType(SmallStringEnc &Enc, QualType QT,
                            const ArrayType *AT,
                            const CodeGen::CodeGenModule &CGM,
                            TypeStringCache &TSC, StringRef NoSizeEnc) {
  if (AT->getSizeModifier() != ArraySizeModifier::Normal)
    return false;
  Enc += "a(";
  if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT))
    CAT->getSize().toStringUnsigned(Enc);
  else
    Enc += NoSizeEnc; // Global arrays use "*", otherwise it is "".
  Enc += ':';
  // The Qualifiers should be attached to the type rather than the array.
  appendQualifier(Enc, QT);
  if (!appendType(Enc, AT->getElementType(), CGM, TSC))
    return false;
  Enc += ')';
  return true;
}

/// Appends a function encoding to Enc, calling appendType for the return type
/// and the arguments.
static bool appendFunctionType(SmallStringEnc &Enc, const FunctionType *FT,
                             const CodeGen::CodeGenModule &CGM,
                             TypeStringCache &TSC) {
  Enc += "f{";
  if (!appendType(Enc, FT->getReturnType(), CGM, TSC))
    return false;
  Enc += "}(";
  if (const FunctionProtoType *FPT = FT->getAs<FunctionProtoType>()) {
    // N.B. we are only interested in the adjusted param types.
    auto I = FPT->param_type_begin();
    auto E = FPT->param_type_end();
    if (I != E) {
      do {
        if (!appendType(Enc, *I, CGM, TSC))
          return false;
        ++I;
        if (I != E)
          Enc += ',';
      } while (I != E);
      if (FPT->isVariadic())
        Enc += ",va";
    } else {
      if (FPT->isVariadic())
        Enc += "va";
      else
        Enc += '0';
    }
  }
  Enc += ')';
  return true;
}

/// Handles the type's qualifier before dispatching a call to handle specific
/// type encodings.
static bool appendType(SmallStringEnc &Enc, QualType QType,
                       const CodeGen::CodeGenModule &CGM,
                       TypeStringCache &TSC) {

  QualType QT = QType.getCanonicalType();

  if (const ArrayType *AT = QT->getAsArrayTypeUnsafe())
    // The Qualifiers should be attached to the type rather than the array.
    // Thus we don't call appendQualifier() here.
    return appendArrayType(Enc, QT, AT, CGM, TSC, "");

  appendQualifier(Enc, QT);

  if (const BuiltinType *BT = QT->getAs<BuiltinType>())
    return appendBuiltinType(Enc, BT);

  if (const PointerType *PT = QT->getAs<PointerType>())
    return appendPointerType(Enc, PT, CGM, TSC);

  if (const EnumType *ET = QT->getAs<EnumType>())
    return appendEnumType(Enc, ET, TSC, QT.getBaseTypeIdentifier());

  if (const RecordType *RT = QT->getAsStructureType())
    return appendRecordType(Enc, RT, CGM, TSC, QT.getBaseTypeIdentifier());

  if (const RecordType *RT = QT->getAsUnionType())
    return appendRecordType(Enc, RT, CGM, TSC, QT.getBaseTypeIdentifier());

  if (const FunctionType *FT = QT->getAs<FunctionType>())
    return appendFunctionType(Enc, FT, CGM, TSC);

  return false;
}

static bool getTypeString(SmallStringEnc &Enc, const Decl *D,
                          const CodeGen::CodeGenModule &CGM,
                          TypeStringCache &TSC) {
  if (!D)
    return false;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->getLanguageLinkage() != CLanguageLinkage)
      return false;
    return appendType(Enc, FD->getType(), CGM, TSC);
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    if (VD->getLanguageLinkage() != CLanguageLinkage)
      return false;
    QualType QT = VD->getType().getCanonicalType();
    if (const ArrayType *AT = QT->getAsArrayTypeUnsafe()) {
      // Global ArrayTypes are given a size of '*' if the size is unknown.
      // The Qualifiers should be attached to the type rather than the array.
      // Thus we don't call appendQualifier() here.
      return appendArrayType(Enc, QT, AT, CGM, TSC, "*");
    }
    return appendType(Enc, QT, CGM, TSC);
  }
  return false;
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createXCoreTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<XCoreTargetCodeGenInfo>(CGM.getTypes());
}
