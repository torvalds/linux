//===--- NSAPI.cpp - NSFoundation APIs ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/NSAPI.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/StringSwitch.h"
#include <optional>

using namespace clang;

NSAPI::NSAPI(ASTContext &ctx)
  : Ctx(ctx), ClassIds(), BOOLId(nullptr), NSIntegerId(nullptr),
    NSUIntegerId(nullptr), NSASCIIStringEncodingId(nullptr),
    NSUTF8StringEncodingId(nullptr) {}

IdentifierInfo *NSAPI::getNSClassId(NSClassIdKindKind K) const {
  static const char *ClassName[NumClassIds] = {
    "NSObject",
    "NSString",
    "NSArray",
    "NSMutableArray",
    "NSDictionary",
    "NSMutableDictionary",
    "NSNumber",
    "NSMutableSet",
    "NSMutableOrderedSet",
    "NSValue"
  };

  if (!ClassIds[K])
    return (ClassIds[K] = &Ctx.Idents.get(ClassName[K]));

  return ClassIds[K];
}

Selector NSAPI::getNSStringSelector(NSStringMethodKind MK) const {
  if (NSStringSelectors[MK].isNull()) {
    Selector Sel;
    switch (MK) {
    case NSStr_stringWithString:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("stringWithString"));
      break;
    case NSStr_stringWithUTF8String:
      Sel = Ctx.Selectors.getUnarySelector(
                                       &Ctx.Idents.get("stringWithUTF8String"));
      break;
    case NSStr_initWithUTF8String:
      Sel = Ctx.Selectors.getUnarySelector(
                                       &Ctx.Idents.get("initWithUTF8String"));
      break;
    case NSStr_stringWithCStringEncoding: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("stringWithCString"),
                                           &Ctx.Idents.get("encoding")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSStr_stringWithCString:
      Sel= Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("stringWithCString"));
      break;
    case NSStr_initWithString:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("initWithString"));
      break;
    }
    return (NSStringSelectors[MK] = Sel);
  }

  return NSStringSelectors[MK];
}

Selector NSAPI::getNSArraySelector(NSArrayMethodKind MK) const {
  if (NSArraySelectors[MK].isNull()) {
    Selector Sel;
    switch (MK) {
    case NSArr_array:
      Sel = Ctx.Selectors.getNullarySelector(&Ctx.Idents.get("array"));
      break;
    case NSArr_arrayWithArray:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("arrayWithArray"));
      break;
    case NSArr_arrayWithObject:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("arrayWithObject"));
      break;
    case NSArr_arrayWithObjects:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("arrayWithObjects"));
      break;
    case NSArr_arrayWithObjectsCount: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("arrayWithObjects"),
                                           &Ctx.Idents.get("count")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSArr_initWithArray:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("initWithArray"));
      break;
    case NSArr_initWithObjects:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("initWithObjects"));
      break;
    case NSArr_objectAtIndex:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("objectAtIndex"));
      break;
    case NSMutableArr_replaceObjectAtIndex: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("replaceObjectAtIndex"),
          &Ctx.Idents.get("withObject")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSMutableArr_addObject:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("addObject"));
      break;
    case NSMutableArr_insertObjectAtIndex: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("insertObject"),
                                           &Ctx.Idents.get("atIndex")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSMutableArr_setObjectAtIndexedSubscript: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("setObject"), &Ctx.Idents.get("atIndexedSubscript")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    }
    return (NSArraySelectors[MK] = Sel);
  }

  return NSArraySelectors[MK];
}

std::optional<NSAPI::NSArrayMethodKind>
NSAPI::getNSArrayMethodKind(Selector Sel) {
  for (unsigned i = 0; i != NumNSArrayMethods; ++i) {
    NSArrayMethodKind MK = NSArrayMethodKind(i);
    if (Sel == getNSArraySelector(MK))
      return MK;
  }

  return std::nullopt;
}

Selector NSAPI::getNSDictionarySelector(
                                       NSDictionaryMethodKind MK) const {
  if (NSDictionarySelectors[MK].isNull()) {
    Selector Sel;
    switch (MK) {
    case NSDict_dictionary:
      Sel = Ctx.Selectors.getNullarySelector(&Ctx.Idents.get("dictionary"));
      break;
    case NSDict_dictionaryWithDictionary:
      Sel = Ctx.Selectors.getUnarySelector(
                                   &Ctx.Idents.get("dictionaryWithDictionary"));
      break;
    case NSDict_dictionaryWithObjectForKey: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("dictionaryWithObject"), &Ctx.Idents.get("forKey")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSDict_dictionaryWithObjectsForKeys: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("dictionaryWithObjects"), &Ctx.Idents.get("forKeys")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSDict_dictionaryWithObjectsForKeysCount: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("dictionaryWithObjects"), &Ctx.Idents.get("forKeys"),
          &Ctx.Idents.get("count")};
      Sel = Ctx.Selectors.getSelector(3, KeyIdents);
      break;
    }
    case NSDict_dictionaryWithObjectsAndKeys:
      Sel = Ctx.Selectors.getUnarySelector(
                               &Ctx.Idents.get("dictionaryWithObjectsAndKeys"));
      break;
    case NSDict_initWithDictionary:
      Sel = Ctx.Selectors.getUnarySelector(
                                         &Ctx.Idents.get("initWithDictionary"));
      break;
    case NSDict_initWithObjectsAndKeys:
      Sel = Ctx.Selectors.getUnarySelector(
                                     &Ctx.Idents.get("initWithObjectsAndKeys"));
      break;
    case NSDict_initWithObjectsForKeys: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("initWithObjects"),
                                           &Ctx.Idents.get("forKeys")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSDict_objectForKey:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("objectForKey"));
      break;
    case NSMutableDict_setObjectForKey: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("setObject"),
                                           &Ctx.Idents.get("forKey")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSMutableDict_setObjectForKeyedSubscript: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("setObject"), &Ctx.Idents.get("forKeyedSubscript")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSMutableDict_setValueForKey: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("setValue"),
                                           &Ctx.Idents.get("forKey")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    }
    return (NSDictionarySelectors[MK] = Sel);
  }

  return NSDictionarySelectors[MK];
}

std::optional<NSAPI::NSDictionaryMethodKind>
NSAPI::getNSDictionaryMethodKind(Selector Sel) {
  for (unsigned i = 0; i != NumNSDictionaryMethods; ++i) {
    NSDictionaryMethodKind MK = NSDictionaryMethodKind(i);
    if (Sel == getNSDictionarySelector(MK))
      return MK;
  }

  return std::nullopt;
}

Selector NSAPI::getNSSetSelector(NSSetMethodKind MK) const {
  if (NSSetSelectors[MK].isNull()) {
    Selector Sel;
    switch (MK) {
    case NSMutableSet_addObject:
      Sel = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get("addObject"));
      break;
    case NSOrderedSet_insertObjectAtIndex: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("insertObject"),
                                           &Ctx.Idents.get("atIndex")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSOrderedSet_setObjectAtIndex: {
      const IdentifierInfo *KeyIdents[] = {&Ctx.Idents.get("setObject"),
                                           &Ctx.Idents.get("atIndex")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSOrderedSet_setObjectAtIndexedSubscript: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("setObject"), &Ctx.Idents.get("atIndexedSubscript")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    case NSOrderedSet_replaceObjectAtIndexWithObject: {
      const IdentifierInfo *KeyIdents[] = {
          &Ctx.Idents.get("replaceObjectAtIndex"),
          &Ctx.Idents.get("withObject")};
      Sel = Ctx.Selectors.getSelector(2, KeyIdents);
      break;
    }
    }
    return (NSSetSelectors[MK] = Sel);
  }

  return NSSetSelectors[MK];
}

std::optional<NSAPI::NSSetMethodKind> NSAPI::getNSSetMethodKind(Selector Sel) {
  for (unsigned i = 0; i != NumNSSetMethods; ++i) {
    NSSetMethodKind MK = NSSetMethodKind(i);
    if (Sel == getNSSetSelector(MK))
      return MK;
  }

  return std::nullopt;
}

Selector NSAPI::getNSNumberLiteralSelector(NSNumberLiteralMethodKind MK,
                                           bool Instance) const {
  static const char *ClassSelectorName[NumNSNumberLiteralMethods] = {
    "numberWithChar",
    "numberWithUnsignedChar",
    "numberWithShort",
    "numberWithUnsignedShort",
    "numberWithInt",
    "numberWithUnsignedInt",
    "numberWithLong",
    "numberWithUnsignedLong",
    "numberWithLongLong",
    "numberWithUnsignedLongLong",
    "numberWithFloat",
    "numberWithDouble",
    "numberWithBool",
    "numberWithInteger",
    "numberWithUnsignedInteger"
  };
  static const char *InstanceSelectorName[NumNSNumberLiteralMethods] = {
    "initWithChar",
    "initWithUnsignedChar",
    "initWithShort",
    "initWithUnsignedShort",
    "initWithInt",
    "initWithUnsignedInt",
    "initWithLong",
    "initWithUnsignedLong",
    "initWithLongLong",
    "initWithUnsignedLongLong",
    "initWithFloat",
    "initWithDouble",
    "initWithBool",
    "initWithInteger",
    "initWithUnsignedInteger"
  };

  Selector *Sels;
  const char **Names;
  if (Instance) {
    Sels = NSNumberInstanceSelectors;
    Names = InstanceSelectorName;
  } else {
    Sels = NSNumberClassSelectors;
    Names = ClassSelectorName;
  }

  if (Sels[MK].isNull())
    Sels[MK] = Ctx.Selectors.getUnarySelector(&Ctx.Idents.get(Names[MK]));
  return Sels[MK];
}

std::optional<NSAPI::NSNumberLiteralMethodKind>
NSAPI::getNSNumberLiteralMethodKind(Selector Sel) const {
  for (unsigned i = 0; i != NumNSNumberLiteralMethods; ++i) {
    NSNumberLiteralMethodKind MK = NSNumberLiteralMethodKind(i);
    if (isNSNumberLiteralSelector(MK, Sel))
      return MK;
  }

  return std::nullopt;
}

std::optional<NSAPI::NSNumberLiteralMethodKind>
NSAPI::getNSNumberFactoryMethodKind(QualType T) const {
  const BuiltinType *BT = T->getAs<BuiltinType>();
  if (!BT)
    return std::nullopt;

  const TypedefType *TDT = T->getAs<TypedefType>();
  if (TDT) {
    QualType TDTTy = QualType(TDT, 0);
    if (isObjCBOOLType(TDTTy))
      return NSAPI::NSNumberWithBool;
    if (isObjCNSIntegerType(TDTTy))
      return NSAPI::NSNumberWithInteger;
    if (isObjCNSUIntegerType(TDTTy))
      return NSAPI::NSNumberWithUnsignedInteger;
  }

  switch (BT->getKind()) {
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
    return NSAPI::NSNumberWithChar;
  case BuiltinType::Char_U:
  case BuiltinType::UChar:
    return NSAPI::NSNumberWithUnsignedChar;
  case BuiltinType::Short:
    return NSAPI::NSNumberWithShort;
  case BuiltinType::UShort:
    return NSAPI::NSNumberWithUnsignedShort;
  case BuiltinType::Int:
    return NSAPI::NSNumberWithInt;
  case BuiltinType::UInt:
    return NSAPI::NSNumberWithUnsignedInt;
  case BuiltinType::Long:
    return NSAPI::NSNumberWithLong;
  case BuiltinType::ULong:
    return NSAPI::NSNumberWithUnsignedLong;
  case BuiltinType::LongLong:
    return NSAPI::NSNumberWithLongLong;
  case BuiltinType::ULongLong:
    return NSAPI::NSNumberWithUnsignedLongLong;
  case BuiltinType::Float:
    return NSAPI::NSNumberWithFloat;
  case BuiltinType::Double:
    return NSAPI::NSNumberWithDouble;
  case BuiltinType::Bool:
    return NSAPI::NSNumberWithBool;

  case BuiltinType::Void:
  case BuiltinType::WChar_U:
  case BuiltinType::WChar_S:
  case BuiltinType::Char8:
  case BuiltinType::Char16:
  case BuiltinType::Char32:
  case BuiltinType::Int128:
  case BuiltinType::LongDouble:
  case BuiltinType::ShortAccum:
  case BuiltinType::Accum:
  case BuiltinType::LongAccum:
  case BuiltinType::UShortAccum:
  case BuiltinType::UAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::ShortFract:
  case BuiltinType::Fract:
  case BuiltinType::LongFract:
  case BuiltinType::UShortFract:
  case BuiltinType::UFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatLongAccum:
  case BuiltinType::SatUShortAccum:
  case BuiltinType::SatUAccum:
  case BuiltinType::SatULongAccum:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatUShortFract:
  case BuiltinType::SatUFract:
  case BuiltinType::SatULongFract:
  case BuiltinType::UInt128:
  case BuiltinType::Float16:
  case BuiltinType::Float128:
  case BuiltinType::Ibm128:
  case BuiltinType::NullPtr:
  case BuiltinType::ObjCClass:
  case BuiltinType::ObjCId:
  case BuiltinType::ObjCSel:
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
  case BuiltinType::OCLSampler:
  case BuiltinType::OCLEvent:
  case BuiltinType::OCLClkEvent:
  case BuiltinType::OCLQueue:
  case BuiltinType::OCLReserveID:
#define SVE_TYPE(Name, Id, SingletonId) \
  case BuiltinType::Id:
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
  case BuiltinType::Id:
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
  case BuiltinType::BoundMember:
  case BuiltinType::UnresolvedTemplate:
  case BuiltinType::Dependent:
  case BuiltinType::Overload:
  case BuiltinType::UnknownAny:
  case BuiltinType::ARCUnbridgedCast:
  case BuiltinType::Half:
  case BuiltinType::PseudoObject:
  case BuiltinType::BuiltinFn:
  case BuiltinType::IncompleteMatrixIdx:
  case BuiltinType::ArraySection:
  case BuiltinType::OMPArrayShaping:
  case BuiltinType::OMPIterator:
  case BuiltinType::BFloat16:
    break;
  }

  return std::nullopt;
}

/// Returns true if \param T is a typedef of "BOOL" in objective-c.
bool NSAPI::isObjCBOOLType(QualType T) const {
  return isObjCTypedef(T, "BOOL", BOOLId);
}
/// Returns true if \param T is a typedef of "NSInteger" in objective-c.
bool NSAPI::isObjCNSIntegerType(QualType T) const {
  return isObjCTypedef(T, "NSInteger", NSIntegerId);
}
/// Returns true if \param T is a typedef of "NSUInteger" in objective-c.
bool NSAPI::isObjCNSUIntegerType(QualType T) const {
  return isObjCTypedef(T, "NSUInteger", NSUIntegerId);
}

StringRef NSAPI::GetNSIntegralKind(QualType T) const {
  if (!Ctx.getLangOpts().ObjC || T.isNull())
    return StringRef();

  while (const TypedefType *TDT = T->getAs<TypedefType>()) {
    StringRef NSIntegralResust =
      llvm::StringSwitch<StringRef>(
        TDT->getDecl()->getDeclName().getAsIdentifierInfo()->getName())
    .Case("int8_t", "int8_t")
    .Case("int16_t", "int16_t")
    .Case("int32_t", "int32_t")
    .Case("NSInteger", "NSInteger")
    .Case("int64_t", "int64_t")
    .Case("uint8_t", "uint8_t")
    .Case("uint16_t", "uint16_t")
    .Case("uint32_t", "uint32_t")
    .Case("NSUInteger", "NSUInteger")
    .Case("uint64_t", "uint64_t")
    .Default(StringRef());
    if (!NSIntegralResust.empty())
      return NSIntegralResust;
    T = TDT->desugar();
  }
  return StringRef();
}

bool NSAPI::isMacroDefined(StringRef Id) const {
  // FIXME: Check whether the relevant module macros are visible.
  return Ctx.Idents.get(Id).hasMacroDefinition();
}

bool NSAPI::isSubclassOfNSClass(ObjCInterfaceDecl *InterfaceDecl,
                                NSClassIdKindKind NSClassKind) const {
  if (!InterfaceDecl) {
    return false;
  }

  IdentifierInfo *NSClassID = getNSClassId(NSClassKind);

  bool IsSubclass = false;
  do {
    IsSubclass = NSClassID == InterfaceDecl->getIdentifier();

    if (IsSubclass) {
      break;
    }
  } while ((InterfaceDecl = InterfaceDecl->getSuperClass()));

  return IsSubclass;
}

bool NSAPI::isObjCTypedef(QualType T,
                          StringRef name, IdentifierInfo *&II) const {
  if (!Ctx.getLangOpts().ObjC)
    return false;
  if (T.isNull())
    return false;

  if (!II)
    II = &Ctx.Idents.get(name);

  while (const TypedefType *TDT = T->getAs<TypedefType>()) {
    if (TDT->getDecl()->getDeclName().getAsIdentifierInfo() == II)
      return true;
    T = TDT->desugar();
  }

  return false;
}

bool NSAPI::isObjCEnumerator(const Expr *E,
                             StringRef name, IdentifierInfo *&II) const {
  if (!Ctx.getLangOpts().ObjC)
    return false;
  if (!E)
    return false;

  if (!II)
    II = &Ctx.Idents.get(name);

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
    if (const EnumConstantDecl *
          EnumD = dyn_cast_or_null<EnumConstantDecl>(DRE->getDecl()))
      return EnumD->getIdentifier() == II;

  return false;
}

Selector NSAPI::getOrInitSelector(ArrayRef<StringRef> Ids,
                                  Selector &Sel) const {
  if (Sel.isNull()) {
    SmallVector<const IdentifierInfo *, 4> Idents;
    for (ArrayRef<StringRef>::const_iterator
           I = Ids.begin(), E = Ids.end(); I != E; ++I)
      Idents.push_back(&Ctx.Idents.get(*I));
    Sel = Ctx.Selectors.getSelector(Idents.size(), Idents.data());
  }
  return Sel;
}

Selector NSAPI::getOrInitNullarySelector(StringRef Id, Selector &Sel) const {
  if (Sel.isNull()) {
    const IdentifierInfo *Ident = &Ctx.Idents.get(Id);
    Sel = Ctx.Selectors.getSelector(0, &Ident);
  }
  return Sel;
}
