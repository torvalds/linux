//===- Attributes.cpp - Implement AttributesList --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements the Attribute, AttributeImpl, AttrBuilder,
// AttributeListImpl, and AttributeList classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Attributes.h"
#include "AttributeImpl.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/ConstantRangeList.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Attribute Construction Methods
//===----------------------------------------------------------------------===//

// allocsize has two integer arguments, but because they're both 32 bits, we can
// pack them into one 64-bit value, at the cost of making said value
// nonsensical.
//
// In order to do this, we need to reserve one value of the second (optional)
// allocsize argument to signify "not present."
static const unsigned AllocSizeNumElemsNotPresent = -1;

static uint64_t packAllocSizeArgs(unsigned ElemSizeArg,
                                  const std::optional<unsigned> &NumElemsArg) {
  assert((!NumElemsArg || *NumElemsArg != AllocSizeNumElemsNotPresent) &&
         "Attempting to pack a reserved value");

  return uint64_t(ElemSizeArg) << 32 |
         NumElemsArg.value_or(AllocSizeNumElemsNotPresent);
}

static std::pair<unsigned, std::optional<unsigned>>
unpackAllocSizeArgs(uint64_t Num) {
  unsigned NumElems = Num & std::numeric_limits<unsigned>::max();
  unsigned ElemSizeArg = Num >> 32;

  std::optional<unsigned> NumElemsArg;
  if (NumElems != AllocSizeNumElemsNotPresent)
    NumElemsArg = NumElems;
  return std::make_pair(ElemSizeArg, NumElemsArg);
}

static uint64_t packVScaleRangeArgs(unsigned MinValue,
                                    std::optional<unsigned> MaxValue) {
  return uint64_t(MinValue) << 32 | MaxValue.value_or(0);
}

static std::pair<unsigned, std::optional<unsigned>>
unpackVScaleRangeArgs(uint64_t Value) {
  unsigned MaxValue = Value & std::numeric_limits<unsigned>::max();
  unsigned MinValue = Value >> 32;

  return std::make_pair(MinValue,
                        MaxValue > 0 ? MaxValue : std::optional<unsigned>());
}

Attribute Attribute::get(LLVMContext &Context, Attribute::AttrKind Kind,
                         uint64_t Val) {
  bool IsIntAttr = Attribute::isIntAttrKind(Kind);
  assert((IsIntAttr || Attribute::isEnumAttrKind(Kind)) &&
         "Not an enum or int attribute");

  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  ID.AddInteger(Kind);
  if (IsIntAttr)
    ID.AddInteger(Val);
  else
    assert(Val == 0 && "Value must be zero for enum attributes");

  void *InsertPoint;
  AttributeImpl *PA = pImpl->AttrsSet.FindNodeOrInsertPos(ID, InsertPoint);

  if (!PA) {
    // If we didn't find any existing attributes of the same shape then create a
    // new one and insert it.
    if (!IsIntAttr)
      PA = new (pImpl->Alloc) EnumAttributeImpl(Kind);
    else
      PA = new (pImpl->Alloc) IntAttributeImpl(Kind, Val);
    pImpl->AttrsSet.InsertNode(PA, InsertPoint);
  }

  // Return the Attribute that we found or created.
  return Attribute(PA);
}

Attribute Attribute::get(LLVMContext &Context, StringRef Kind, StringRef Val) {
  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  ID.AddString(Kind);
  if (!Val.empty()) ID.AddString(Val);

  void *InsertPoint;
  AttributeImpl *PA = pImpl->AttrsSet.FindNodeOrInsertPos(ID, InsertPoint);

  if (!PA) {
    // If we didn't find any existing attributes of the same shape then create a
    // new one and insert it.
    void *Mem =
        pImpl->Alloc.Allocate(StringAttributeImpl::totalSizeToAlloc(Kind, Val),
                              alignof(StringAttributeImpl));
    PA = new (Mem) StringAttributeImpl(Kind, Val);
    pImpl->AttrsSet.InsertNode(PA, InsertPoint);
  }

  // Return the Attribute that we found or created.
  return Attribute(PA);
}

Attribute Attribute::get(LLVMContext &Context, Attribute::AttrKind Kind,
                         Type *Ty) {
  assert(Attribute::isTypeAttrKind(Kind) && "Not a type attribute");
  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  ID.AddInteger(Kind);
  ID.AddPointer(Ty);

  void *InsertPoint;
  AttributeImpl *PA = pImpl->AttrsSet.FindNodeOrInsertPos(ID, InsertPoint);

  if (!PA) {
    // If we didn't find any existing attributes of the same shape then create a
    // new one and insert it.
    PA = new (pImpl->Alloc) TypeAttributeImpl(Kind, Ty);
    pImpl->AttrsSet.InsertNode(PA, InsertPoint);
  }

  // Return the Attribute that we found or created.
  return Attribute(PA);
}

Attribute Attribute::get(LLVMContext &Context, Attribute::AttrKind Kind,
                         const ConstantRange &CR) {
  assert(Attribute::isConstantRangeAttrKind(Kind) &&
         "Not a ConstantRange attribute");
  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  ID.AddInteger(Kind);
  CR.getLower().Profile(ID);
  CR.getUpper().Profile(ID);

  void *InsertPoint;
  AttributeImpl *PA = pImpl->AttrsSet.FindNodeOrInsertPos(ID, InsertPoint);

  if (!PA) {
    // If we didn't find any existing attributes of the same shape then create a
    // new one and insert it.
    PA = new (pImpl->ConstantRangeAttributeAlloc.Allocate())
        ConstantRangeAttributeImpl(Kind, CR);
    pImpl->AttrsSet.InsertNode(PA, InsertPoint);
  }

  // Return the Attribute that we found or created.
  return Attribute(PA);
}

Attribute Attribute::get(LLVMContext &Context, Attribute::AttrKind Kind,
                         ArrayRef<ConstantRange> Val) {
  assert(Attribute::isConstantRangeListAttrKind(Kind) &&
         "Not a ConstantRangeList attribute");
  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  ID.AddInteger(Kind);
  ID.AddInteger(Val.size());
  for (auto &CR : Val) {
    CR.getLower().Profile(ID);
    CR.getUpper().Profile(ID);
  }

  void *InsertPoint;
  AttributeImpl *PA = pImpl->AttrsSet.FindNodeOrInsertPos(ID, InsertPoint);

  if (!PA) {
    // If we didn't find any existing attributes of the same shape then create a
    // new one and insert it.
    // ConstantRangeListAttributeImpl is a dynamically sized class and cannot
    // use SpecificBumpPtrAllocator. Instead, we use normal Alloc for
    // allocation and record the allocated pointer in
    // `ConstantRangeListAttributes`. LLVMContext destructor will call the
    // destructor of the allocated pointer explicitly.
    void *Mem = pImpl->Alloc.Allocate(
        ConstantRangeListAttributeImpl::totalSizeToAlloc(Val),
        alignof(ConstantRangeListAttributeImpl));
    PA = new (Mem) ConstantRangeListAttributeImpl(Kind, Val);
    pImpl->AttrsSet.InsertNode(PA, InsertPoint);
    pImpl->ConstantRangeListAttributes.push_back(
        reinterpret_cast<ConstantRangeListAttributeImpl *>(PA));
  }

  // Return the Attribute that we found or created.
  return Attribute(PA);
}

Attribute Attribute::getWithAlignment(LLVMContext &Context, Align A) {
  assert(A <= llvm::Value::MaximumAlignment && "Alignment too large.");
  return get(Context, Alignment, A.value());
}

Attribute Attribute::getWithStackAlignment(LLVMContext &Context, Align A) {
  assert(A <= 0x100 && "Alignment too large.");
  return get(Context, StackAlignment, A.value());
}

Attribute Attribute::getWithDereferenceableBytes(LLVMContext &Context,
                                                uint64_t Bytes) {
  assert(Bytes && "Bytes must be non-zero.");
  return get(Context, Dereferenceable, Bytes);
}

Attribute Attribute::getWithDereferenceableOrNullBytes(LLVMContext &Context,
                                                       uint64_t Bytes) {
  assert(Bytes && "Bytes must be non-zero.");
  return get(Context, DereferenceableOrNull, Bytes);
}

Attribute Attribute::getWithByValType(LLVMContext &Context, Type *Ty) {
  return get(Context, ByVal, Ty);
}

Attribute Attribute::getWithStructRetType(LLVMContext &Context, Type *Ty) {
  return get(Context, StructRet, Ty);
}

Attribute Attribute::getWithByRefType(LLVMContext &Context, Type *Ty) {
  return get(Context, ByRef, Ty);
}

Attribute Attribute::getWithPreallocatedType(LLVMContext &Context, Type *Ty) {
  return get(Context, Preallocated, Ty);
}

Attribute Attribute::getWithInAllocaType(LLVMContext &Context, Type *Ty) {
  return get(Context, InAlloca, Ty);
}

Attribute Attribute::getWithUWTableKind(LLVMContext &Context,
                                        UWTableKind Kind) {
  return get(Context, UWTable, uint64_t(Kind));
}

Attribute Attribute::getWithMemoryEffects(LLVMContext &Context,
                                          MemoryEffects ME) {
  return get(Context, Memory, ME.toIntValue());
}

Attribute Attribute::getWithNoFPClass(LLVMContext &Context,
                                      FPClassTest ClassMask) {
  return get(Context, NoFPClass, ClassMask);
}

Attribute
Attribute::getWithAllocSizeArgs(LLVMContext &Context, unsigned ElemSizeArg,
                                const std::optional<unsigned> &NumElemsArg) {
  assert(!(ElemSizeArg == 0 && NumElemsArg && *NumElemsArg == 0) &&
         "Invalid allocsize arguments -- given allocsize(0, 0)");
  return get(Context, AllocSize, packAllocSizeArgs(ElemSizeArg, NumElemsArg));
}

Attribute Attribute::getWithVScaleRangeArgs(LLVMContext &Context,
                                            unsigned MinValue,
                                            unsigned MaxValue) {
  return get(Context, VScaleRange, packVScaleRangeArgs(MinValue, MaxValue));
}

Attribute::AttrKind Attribute::getAttrKindFromName(StringRef AttrName) {
  return StringSwitch<Attribute::AttrKind>(AttrName)
#define GET_ATTR_NAMES
#define ATTRIBUTE_ENUM(ENUM_NAME, DISPLAY_NAME)                                \
  .Case(#DISPLAY_NAME, Attribute::ENUM_NAME)
#include "llvm/IR/Attributes.inc"
      .Default(Attribute::None);
}

StringRef Attribute::getNameFromAttrKind(Attribute::AttrKind AttrKind) {
  switch (AttrKind) {
#define GET_ATTR_NAMES
#define ATTRIBUTE_ENUM(ENUM_NAME, DISPLAY_NAME)                                \
  case Attribute::ENUM_NAME:                                                   \
    return #DISPLAY_NAME;
#include "llvm/IR/Attributes.inc"
  case Attribute::None:
    return "none";
  default:
    llvm_unreachable("invalid Kind");
  }
}

bool Attribute::isExistingAttribute(StringRef Name) {
  return StringSwitch<bool>(Name)
#define GET_ATTR_NAMES
#define ATTRIBUTE_ALL(ENUM_NAME, DISPLAY_NAME) .Case(#DISPLAY_NAME, true)
#include "llvm/IR/Attributes.inc"
      .Default(false);
}

//===----------------------------------------------------------------------===//
// Attribute Accessor Methods
//===----------------------------------------------------------------------===//

bool Attribute::isEnumAttribute() const {
  return pImpl && pImpl->isEnumAttribute();
}

bool Attribute::isIntAttribute() const {
  return pImpl && pImpl->isIntAttribute();
}

bool Attribute::isStringAttribute() const {
  return pImpl && pImpl->isStringAttribute();
}

bool Attribute::isTypeAttribute() const {
  return pImpl && pImpl->isTypeAttribute();
}

bool Attribute::isConstantRangeAttribute() const {
  return pImpl && pImpl->isConstantRangeAttribute();
}

bool Attribute::isConstantRangeListAttribute() const {
  return pImpl && pImpl->isConstantRangeListAttribute();
}

Attribute::AttrKind Attribute::getKindAsEnum() const {
  if (!pImpl) return None;
  assert((isEnumAttribute() || isIntAttribute() || isTypeAttribute() ||
          isConstantRangeAttribute() || isConstantRangeListAttribute()) &&
         "Invalid attribute type to get the kind as an enum!");
  return pImpl->getKindAsEnum();
}

uint64_t Attribute::getValueAsInt() const {
  if (!pImpl) return 0;
  assert(isIntAttribute() &&
         "Expected the attribute to be an integer attribute!");
  return pImpl->getValueAsInt();
}

bool Attribute::getValueAsBool() const {
  if (!pImpl) return false;
  assert(isStringAttribute() &&
         "Expected the attribute to be a string attribute!");
  return pImpl->getValueAsBool();
}

StringRef Attribute::getKindAsString() const {
  if (!pImpl) return {};
  assert(isStringAttribute() &&
         "Invalid attribute type to get the kind as a string!");
  return pImpl->getKindAsString();
}

StringRef Attribute::getValueAsString() const {
  if (!pImpl) return {};
  assert(isStringAttribute() &&
         "Invalid attribute type to get the value as a string!");
  return pImpl->getValueAsString();
}

Type *Attribute::getValueAsType() const {
  if (!pImpl) return {};
  assert(isTypeAttribute() &&
         "Invalid attribute type to get the value as a type!");
  return pImpl->getValueAsType();
}

const ConstantRange &Attribute::getValueAsConstantRange() const {
  assert(isConstantRangeAttribute() &&
         "Invalid attribute type to get the value as a ConstantRange!");
  return pImpl->getValueAsConstantRange();
}

ArrayRef<ConstantRange> Attribute::getValueAsConstantRangeList() const {
  assert(isConstantRangeListAttribute() &&
         "Invalid attribute type to get the value as a ConstantRangeList!");
  return pImpl->getValueAsConstantRangeList();
}

bool Attribute::hasAttribute(AttrKind Kind) const {
  return (pImpl && pImpl->hasAttribute(Kind)) || (!pImpl && Kind == None);
}

bool Attribute::hasAttribute(StringRef Kind) const {
  if (!isStringAttribute()) return false;
  return pImpl && pImpl->hasAttribute(Kind);
}

MaybeAlign Attribute::getAlignment() const {
  assert(hasAttribute(Attribute::Alignment) &&
         "Trying to get alignment from non-alignment attribute!");
  return MaybeAlign(pImpl->getValueAsInt());
}

MaybeAlign Attribute::getStackAlignment() const {
  assert(hasAttribute(Attribute::StackAlignment) &&
         "Trying to get alignment from non-alignment attribute!");
  return MaybeAlign(pImpl->getValueAsInt());
}

uint64_t Attribute::getDereferenceableBytes() const {
  assert(hasAttribute(Attribute::Dereferenceable) &&
         "Trying to get dereferenceable bytes from "
         "non-dereferenceable attribute!");
  return pImpl->getValueAsInt();
}

uint64_t Attribute::getDereferenceableOrNullBytes() const {
  assert(hasAttribute(Attribute::DereferenceableOrNull) &&
         "Trying to get dereferenceable bytes from "
         "non-dereferenceable attribute!");
  return pImpl->getValueAsInt();
}

std::pair<unsigned, std::optional<unsigned>>
Attribute::getAllocSizeArgs() const {
  assert(hasAttribute(Attribute::AllocSize) &&
         "Trying to get allocsize args from non-allocsize attribute");
  return unpackAllocSizeArgs(pImpl->getValueAsInt());
}

unsigned Attribute::getVScaleRangeMin() const {
  assert(hasAttribute(Attribute::VScaleRange) &&
         "Trying to get vscale args from non-vscale attribute");
  return unpackVScaleRangeArgs(pImpl->getValueAsInt()).first;
}

std::optional<unsigned> Attribute::getVScaleRangeMax() const {
  assert(hasAttribute(Attribute::VScaleRange) &&
         "Trying to get vscale args from non-vscale attribute");
  return unpackVScaleRangeArgs(pImpl->getValueAsInt()).second;
}

UWTableKind Attribute::getUWTableKind() const {
  assert(hasAttribute(Attribute::UWTable) &&
         "Trying to get unwind table kind from non-uwtable attribute");
  return UWTableKind(pImpl->getValueAsInt());
}

AllocFnKind Attribute::getAllocKind() const {
  assert(hasAttribute(Attribute::AllocKind) &&
         "Trying to get allockind value from non-allockind attribute");
  return AllocFnKind(pImpl->getValueAsInt());
}

MemoryEffects Attribute::getMemoryEffects() const {
  assert(hasAttribute(Attribute::Memory) &&
         "Can only call getMemoryEffects() on memory attribute");
  return MemoryEffects::createFromIntValue(pImpl->getValueAsInt());
}

FPClassTest Attribute::getNoFPClass() const {
  assert(hasAttribute(Attribute::NoFPClass) &&
         "Can only call getNoFPClass() on nofpclass attribute");
  return static_cast<FPClassTest>(pImpl->getValueAsInt());
}

const ConstantRange &Attribute::getRange() const {
  assert(hasAttribute(Attribute::Range) &&
         "Trying to get range args from non-range attribute");
  return pImpl->getValueAsConstantRange();
}

ArrayRef<ConstantRange> Attribute::getInitializes() const {
  assert(hasAttribute(Attribute::Initializes) &&
         "Trying to get initializes attr from non-ConstantRangeList attribute");
  return pImpl->getValueAsConstantRangeList();
}

static const char *getModRefStr(ModRefInfo MR) {
  switch (MR) {
  case ModRefInfo::NoModRef:
    return "none";
  case ModRefInfo::Ref:
    return "read";
  case ModRefInfo::Mod:
    return "write";
  case ModRefInfo::ModRef:
    return "readwrite";
  }
  llvm_unreachable("Invalid ModRefInfo");
}

std::string Attribute::getAsString(bool InAttrGrp) const {
  if (!pImpl) return {};

  if (isEnumAttribute())
    return getNameFromAttrKind(getKindAsEnum()).str();

  if (isTypeAttribute()) {
    std::string Result = getNameFromAttrKind(getKindAsEnum()).str();
    Result += '(';
    raw_string_ostream OS(Result);
    getValueAsType()->print(OS, false, true);
    OS.flush();
    Result += ')';
    return Result;
  }

  // FIXME: These should be output like this:
  //
  //   align=4
  //   alignstack=8
  //
  if (hasAttribute(Attribute::Alignment))
    return (InAttrGrp ? "align=" + Twine(getValueAsInt())
                      : "align " + Twine(getValueAsInt()))
        .str();

  auto AttrWithBytesToString = [&](const char *Name) {
    return (InAttrGrp ? Name + ("=" + Twine(getValueAsInt()))
                      : Name + ("(" + Twine(getValueAsInt())) + ")")
        .str();
  };

  if (hasAttribute(Attribute::StackAlignment))
    return AttrWithBytesToString("alignstack");

  if (hasAttribute(Attribute::Dereferenceable))
    return AttrWithBytesToString("dereferenceable");

  if (hasAttribute(Attribute::DereferenceableOrNull))
    return AttrWithBytesToString("dereferenceable_or_null");

  if (hasAttribute(Attribute::AllocSize)) {
    unsigned ElemSize;
    std::optional<unsigned> NumElems;
    std::tie(ElemSize, NumElems) = getAllocSizeArgs();

    return (NumElems
                ? "allocsize(" + Twine(ElemSize) + "," + Twine(*NumElems) + ")"
                : "allocsize(" + Twine(ElemSize) + ")")
        .str();
  }

  if (hasAttribute(Attribute::VScaleRange)) {
    unsigned MinValue = getVScaleRangeMin();
    std::optional<unsigned> MaxValue = getVScaleRangeMax();
    return ("vscale_range(" + Twine(MinValue) + "," +
            Twine(MaxValue.value_or(0)) + ")")
        .str();
  }

  if (hasAttribute(Attribute::UWTable)) {
    UWTableKind Kind = getUWTableKind();
    assert(Kind != UWTableKind::None && "uwtable attribute should not be none");
    return Kind == UWTableKind::Default ? "uwtable" : "uwtable(sync)";
  }

  if (hasAttribute(Attribute::AllocKind)) {
    AllocFnKind Kind = getAllocKind();
    SmallVector<StringRef> parts;
    if ((Kind & AllocFnKind::Alloc) != AllocFnKind::Unknown)
      parts.push_back("alloc");
    if ((Kind & AllocFnKind::Realloc) != AllocFnKind::Unknown)
      parts.push_back("realloc");
    if ((Kind & AllocFnKind::Free) != AllocFnKind::Unknown)
      parts.push_back("free");
    if ((Kind & AllocFnKind::Uninitialized) != AllocFnKind::Unknown)
      parts.push_back("uninitialized");
    if ((Kind & AllocFnKind::Zeroed) != AllocFnKind::Unknown)
      parts.push_back("zeroed");
    if ((Kind & AllocFnKind::Aligned) != AllocFnKind::Unknown)
      parts.push_back("aligned");
    return ("allockind(\"" +
            Twine(llvm::join(parts.begin(), parts.end(), ",")) + "\")")
        .str();
  }

  if (hasAttribute(Attribute::Memory)) {
    std::string Result;
    raw_string_ostream OS(Result);
    bool First = true;
    OS << "memory(";

    MemoryEffects ME = getMemoryEffects();

    // Print access kind for "other" as the default access kind. This way it
    // will apply to any new location kinds that get split out of "other".
    ModRefInfo OtherMR = ME.getModRef(IRMemLocation::Other);
    if (OtherMR != ModRefInfo::NoModRef || ME.getModRef() == OtherMR) {
      First = false;
      OS << getModRefStr(OtherMR);
    }

    for (auto Loc : MemoryEffects::locations()) {
      ModRefInfo MR = ME.getModRef(Loc);
      if (MR == OtherMR)
        continue;

      if (!First)
        OS << ", ";
      First = false;

      switch (Loc) {
      case IRMemLocation::ArgMem:
        OS << "argmem: ";
        break;
      case IRMemLocation::InaccessibleMem:
        OS << "inaccessiblemem: ";
        break;
      case IRMemLocation::Other:
        llvm_unreachable("This is represented as the default access kind");
      }
      OS << getModRefStr(MR);
    }
    OS << ")";
    OS.flush();
    return Result;
  }

  if (hasAttribute(Attribute::NoFPClass)) {
    std::string Result = "nofpclass";
    raw_string_ostream OS(Result);
    OS << getNoFPClass();
    return Result;
  }

  if (hasAttribute(Attribute::Range)) {
    std::string Result;
    raw_string_ostream OS(Result);
    const ConstantRange &CR = getValueAsConstantRange();
    OS << "range(";
    OS << "i" << CR.getBitWidth() << " ";
    OS << CR.getLower() << ", " << CR.getUpper();
    OS << ")";
    OS.flush();
    return Result;
  }

  if (hasAttribute(Attribute::Initializes)) {
    std::string Result;
    raw_string_ostream OS(Result);
    ConstantRangeList CRL = getInitializes();
    OS << "initializes(";
    CRL.print(OS);
    OS << ")";
    OS.flush();
    return Result;
  }

  // Convert target-dependent attributes to strings of the form:
  //
  //   "kind"
  //   "kind" = "value"
  //
  if (isStringAttribute()) {
    std::string Result;
    {
      raw_string_ostream OS(Result);
      OS << '"' << getKindAsString() << '"';

      // Since some attribute strings contain special characters that cannot be
      // printable, those have to be escaped to make the attribute value
      // printable as is.  e.g. "\01__gnu_mcount_nc"
      const auto &AttrVal = pImpl->getValueAsString();
      if (!AttrVal.empty()) {
        OS << "=\"";
        printEscapedString(AttrVal, OS);
        OS << "\"";
      }
    }
    return Result;
  }

  llvm_unreachable("Unknown attribute");
}

bool Attribute::hasParentContext(LLVMContext &C) const {
  assert(isValid() && "invalid Attribute doesn't refer to any context");
  FoldingSetNodeID ID;
  pImpl->Profile(ID);
  void *Unused;
  return C.pImpl->AttrsSet.FindNodeOrInsertPos(ID, Unused) == pImpl;
}

bool Attribute::operator<(Attribute A) const {
  if (!pImpl && !A.pImpl) return false;
  if (!pImpl) return true;
  if (!A.pImpl) return false;
  return *pImpl < *A.pImpl;
}

void Attribute::Profile(FoldingSetNodeID &ID) const {
  ID.AddPointer(pImpl);
}

enum AttributeProperty {
  FnAttr = (1 << 0),
  ParamAttr = (1 << 1),
  RetAttr = (1 << 2),
};

#define GET_ATTR_PROP_TABLE
#include "llvm/IR/Attributes.inc"

static bool hasAttributeProperty(Attribute::AttrKind Kind,
                                 AttributeProperty Prop) {
  unsigned Index = Kind - 1;
  assert(Index < std::size(AttrPropTable) && "Invalid attribute kind");
  return AttrPropTable[Index] & Prop;
}

bool Attribute::canUseAsFnAttr(AttrKind Kind) {
  return hasAttributeProperty(Kind, AttributeProperty::FnAttr);
}

bool Attribute::canUseAsParamAttr(AttrKind Kind) {
  return hasAttributeProperty(Kind, AttributeProperty::ParamAttr);
}

bool Attribute::canUseAsRetAttr(AttrKind Kind) {
  return hasAttributeProperty(Kind, AttributeProperty::RetAttr);
}

//===----------------------------------------------------------------------===//
// AttributeImpl Definition
//===----------------------------------------------------------------------===//

bool AttributeImpl::hasAttribute(Attribute::AttrKind A) const {
  if (isStringAttribute()) return false;
  return getKindAsEnum() == A;
}

bool AttributeImpl::hasAttribute(StringRef Kind) const {
  if (!isStringAttribute()) return false;
  return getKindAsString() == Kind;
}

Attribute::AttrKind AttributeImpl::getKindAsEnum() const {
  assert(isEnumAttribute() || isIntAttribute() || isTypeAttribute() ||
         isConstantRangeAttribute() || isConstantRangeListAttribute());
  return static_cast<const EnumAttributeImpl *>(this)->getEnumKind();
}

uint64_t AttributeImpl::getValueAsInt() const {
  assert(isIntAttribute());
  return static_cast<const IntAttributeImpl *>(this)->getValue();
}

bool AttributeImpl::getValueAsBool() const {
  assert(getValueAsString().empty() || getValueAsString() == "false" || getValueAsString() == "true");
  return getValueAsString() == "true";
}

StringRef AttributeImpl::getKindAsString() const {
  assert(isStringAttribute());
  return static_cast<const StringAttributeImpl *>(this)->getStringKind();
}

StringRef AttributeImpl::getValueAsString() const {
  assert(isStringAttribute());
  return static_cast<const StringAttributeImpl *>(this)->getStringValue();
}

Type *AttributeImpl::getValueAsType() const {
  assert(isTypeAttribute());
  return static_cast<const TypeAttributeImpl *>(this)->getTypeValue();
}

const ConstantRange &AttributeImpl::getValueAsConstantRange() const {
  assert(isConstantRangeAttribute());
  return static_cast<const ConstantRangeAttributeImpl *>(this)
      ->getConstantRangeValue();
}

ArrayRef<ConstantRange> AttributeImpl::getValueAsConstantRangeList() const {
  assert(isConstantRangeListAttribute());
  return static_cast<const ConstantRangeListAttributeImpl *>(this)
      ->getConstantRangeListValue();
}

bool AttributeImpl::operator<(const AttributeImpl &AI) const {
  if (this == &AI)
    return false;

  // This sorts the attributes with Attribute::AttrKinds coming first (sorted
  // relative to their enum value) and then strings.
  if (!isStringAttribute()) {
    if (AI.isStringAttribute())
      return true;
    if (getKindAsEnum() != AI.getKindAsEnum())
      return getKindAsEnum() < AI.getKindAsEnum();
    assert(!AI.isEnumAttribute() && "Non-unique attribute");
    assert(!AI.isTypeAttribute() && "Comparison of types would be unstable");
    assert(!AI.isConstantRangeAttribute() && "Unclear how to compare ranges");
    assert(!AI.isConstantRangeListAttribute() &&
           "Unclear how to compare range list");
    // TODO: Is this actually needed?
    assert(AI.isIntAttribute() && "Only possibility left");
    return getValueAsInt() < AI.getValueAsInt();
  }

  if (!AI.isStringAttribute())
    return false;
  if (getKindAsString() == AI.getKindAsString())
    return getValueAsString() < AI.getValueAsString();
  return getKindAsString() < AI.getKindAsString();
}

//===----------------------------------------------------------------------===//
// AttributeSet Definition
//===----------------------------------------------------------------------===//

AttributeSet AttributeSet::get(LLVMContext &C, const AttrBuilder &B) {
  return AttributeSet(AttributeSetNode::get(C, B));
}

AttributeSet AttributeSet::get(LLVMContext &C, ArrayRef<Attribute> Attrs) {
  return AttributeSet(AttributeSetNode::get(C, Attrs));
}

AttributeSet AttributeSet::addAttribute(LLVMContext &C,
                                        Attribute::AttrKind Kind) const {
  if (hasAttribute(Kind)) return *this;
  AttrBuilder B(C);
  B.addAttribute(Kind);
  return addAttributes(C, AttributeSet::get(C, B));
}

AttributeSet AttributeSet::addAttribute(LLVMContext &C, StringRef Kind,
                                        StringRef Value) const {
  AttrBuilder B(C);
  B.addAttribute(Kind, Value);
  return addAttributes(C, AttributeSet::get(C, B));
}

AttributeSet AttributeSet::addAttributes(LLVMContext &C,
                                         const AttributeSet AS) const {
  if (!hasAttributes())
    return AS;

  if (!AS.hasAttributes())
    return *this;

  AttrBuilder B(C, *this);
  B.merge(AttrBuilder(C, AS));
  return get(C, B);
}

AttributeSet AttributeSet::removeAttribute(LLVMContext &C,
                                             Attribute::AttrKind Kind) const {
  if (!hasAttribute(Kind)) return *this;
  AttrBuilder B(C, *this);
  B.removeAttribute(Kind);
  return get(C, B);
}

AttributeSet AttributeSet::removeAttribute(LLVMContext &C,
                                             StringRef Kind) const {
  if (!hasAttribute(Kind)) return *this;
  AttrBuilder B(C, *this);
  B.removeAttribute(Kind);
  return get(C, B);
}

AttributeSet AttributeSet::removeAttributes(LLVMContext &C,
                                            const AttributeMask &Attrs) const {
  AttrBuilder B(C, *this);
  // If there is nothing to remove, directly return the original set.
  if (!B.overlaps(Attrs))
    return *this;

  B.remove(Attrs);
  return get(C, B);
}

unsigned AttributeSet::getNumAttributes() const {
  return SetNode ? SetNode->getNumAttributes() : 0;
}

bool AttributeSet::hasAttribute(Attribute::AttrKind Kind) const {
  return SetNode ? SetNode->hasAttribute(Kind) : false;
}

bool AttributeSet::hasAttribute(StringRef Kind) const {
  return SetNode ? SetNode->hasAttribute(Kind) : false;
}

Attribute AttributeSet::getAttribute(Attribute::AttrKind Kind) const {
  return SetNode ? SetNode->getAttribute(Kind) : Attribute();
}

Attribute AttributeSet::getAttribute(StringRef Kind) const {
  return SetNode ? SetNode->getAttribute(Kind) : Attribute();
}

MaybeAlign AttributeSet::getAlignment() const {
  return SetNode ? SetNode->getAlignment() : std::nullopt;
}

MaybeAlign AttributeSet::getStackAlignment() const {
  return SetNode ? SetNode->getStackAlignment() : std::nullopt;
}

uint64_t AttributeSet::getDereferenceableBytes() const {
  return SetNode ? SetNode->getDereferenceableBytes() : 0;
}

uint64_t AttributeSet::getDereferenceableOrNullBytes() const {
  return SetNode ? SetNode->getDereferenceableOrNullBytes() : 0;
}

Type *AttributeSet::getByRefType() const {
  return SetNode ? SetNode->getAttributeType(Attribute::ByRef) : nullptr;
}

Type *AttributeSet::getByValType() const {
  return SetNode ? SetNode->getAttributeType(Attribute::ByVal) : nullptr;
}

Type *AttributeSet::getStructRetType() const {
  return SetNode ? SetNode->getAttributeType(Attribute::StructRet) : nullptr;
}

Type *AttributeSet::getPreallocatedType() const {
  return SetNode ? SetNode->getAttributeType(Attribute::Preallocated) : nullptr;
}

Type *AttributeSet::getInAllocaType() const {
  return SetNode ? SetNode->getAttributeType(Attribute::InAlloca) : nullptr;
}

Type *AttributeSet::getElementType() const {
  return SetNode ? SetNode->getAttributeType(Attribute::ElementType) : nullptr;
}

std::optional<std::pair<unsigned, std::optional<unsigned>>>
AttributeSet::getAllocSizeArgs() const {
  if (SetNode)
    return SetNode->getAllocSizeArgs();
  return std::nullopt;
}

unsigned AttributeSet::getVScaleRangeMin() const {
  return SetNode ? SetNode->getVScaleRangeMin() : 1;
}

std::optional<unsigned> AttributeSet::getVScaleRangeMax() const {
  return SetNode ? SetNode->getVScaleRangeMax() : std::nullopt;
}

UWTableKind AttributeSet::getUWTableKind() const {
  return SetNode ? SetNode->getUWTableKind() : UWTableKind::None;
}

AllocFnKind AttributeSet::getAllocKind() const {
  return SetNode ? SetNode->getAllocKind() : AllocFnKind::Unknown;
}

MemoryEffects AttributeSet::getMemoryEffects() const {
  return SetNode ? SetNode->getMemoryEffects() : MemoryEffects::unknown();
}

FPClassTest AttributeSet::getNoFPClass() const {
  return SetNode ? SetNode->getNoFPClass() : fcNone;
}

std::string AttributeSet::getAsString(bool InAttrGrp) const {
  return SetNode ? SetNode->getAsString(InAttrGrp) : "";
}

bool AttributeSet::hasParentContext(LLVMContext &C) const {
  assert(hasAttributes() && "empty AttributeSet doesn't refer to any context");
  FoldingSetNodeID ID;
  SetNode->Profile(ID);
  void *Unused;
  return C.pImpl->AttrsSetNodes.FindNodeOrInsertPos(ID, Unused) == SetNode;
}

AttributeSet::iterator AttributeSet::begin() const {
  return SetNode ? SetNode->begin() : nullptr;
}

AttributeSet::iterator AttributeSet::end() const {
  return SetNode ? SetNode->end() : nullptr;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AttributeSet::dump() const {
  dbgs() << "AS =\n";
    dbgs() << "  { ";
    dbgs() << getAsString(true) << " }\n";
}
#endif

//===----------------------------------------------------------------------===//
// AttributeSetNode Definition
//===----------------------------------------------------------------------===//

AttributeSetNode::AttributeSetNode(ArrayRef<Attribute> Attrs)
    : NumAttrs(Attrs.size()) {
  // There's memory after the node where we can store the entries in.
  llvm::copy(Attrs, getTrailingObjects<Attribute>());

  for (const auto &I : *this) {
    if (I.isStringAttribute())
      StringAttrs.insert({ I.getKindAsString(), I });
    else
      AvailableAttrs.addAttribute(I.getKindAsEnum());
  }
}

AttributeSetNode *AttributeSetNode::get(LLVMContext &C,
                                        ArrayRef<Attribute> Attrs) {
  SmallVector<Attribute, 8> SortedAttrs(Attrs.begin(), Attrs.end());
  llvm::sort(SortedAttrs);
  return getSorted(C, SortedAttrs);
}

AttributeSetNode *AttributeSetNode::getSorted(LLVMContext &C,
                                              ArrayRef<Attribute> SortedAttrs) {
  if (SortedAttrs.empty())
    return nullptr;

  // Build a key to look up the existing attributes.
  LLVMContextImpl *pImpl = C.pImpl;
  FoldingSetNodeID ID;

  assert(llvm::is_sorted(SortedAttrs) && "Expected sorted attributes!");
  for (const auto &Attr : SortedAttrs)
    Attr.Profile(ID);

  void *InsertPoint;
  AttributeSetNode *PA =
    pImpl->AttrsSetNodes.FindNodeOrInsertPos(ID, InsertPoint);

  // If we didn't find any existing attributes of the same shape then create a
  // new one and insert it.
  if (!PA) {
    // Coallocate entries after the AttributeSetNode itself.
    void *Mem = ::operator new(totalSizeToAlloc<Attribute>(SortedAttrs.size()));
    PA = new (Mem) AttributeSetNode(SortedAttrs);
    pImpl->AttrsSetNodes.InsertNode(PA, InsertPoint);
  }

  // Return the AttributeSetNode that we found or created.
  return PA;
}

AttributeSetNode *AttributeSetNode::get(LLVMContext &C, const AttrBuilder &B) {
  return getSorted(C, B.attrs());
}

bool AttributeSetNode::hasAttribute(StringRef Kind) const {
  return StringAttrs.count(Kind);
}

std::optional<Attribute>
AttributeSetNode::findEnumAttribute(Attribute::AttrKind Kind) const {
  // Do a quick presence check.
  if (!hasAttribute(Kind))
    return std::nullopt;

  // Attributes in a set are sorted by enum value, followed by string
  // attributes. Binary search the one we want.
  const Attribute *I =
      std::lower_bound(begin(), end() - StringAttrs.size(), Kind,
                       [](Attribute A, Attribute::AttrKind Kind) {
                         return A.getKindAsEnum() < Kind;
                       });
  assert(I != end() && I->hasAttribute(Kind) && "Presence check failed?");
  return *I;
}

Attribute AttributeSetNode::getAttribute(Attribute::AttrKind Kind) const {
  if (auto A = findEnumAttribute(Kind))
    return *A;
  return {};
}

Attribute AttributeSetNode::getAttribute(StringRef Kind) const {
  return StringAttrs.lookup(Kind);
}

MaybeAlign AttributeSetNode::getAlignment() const {
  if (auto A = findEnumAttribute(Attribute::Alignment))
    return A->getAlignment();
  return std::nullopt;
}

MaybeAlign AttributeSetNode::getStackAlignment() const {
  if (auto A = findEnumAttribute(Attribute::StackAlignment))
    return A->getStackAlignment();
  return std::nullopt;
}

Type *AttributeSetNode::getAttributeType(Attribute::AttrKind Kind) const {
  if (auto A = findEnumAttribute(Kind))
    return A->getValueAsType();
  return nullptr;
}

uint64_t AttributeSetNode::getDereferenceableBytes() const {
  if (auto A = findEnumAttribute(Attribute::Dereferenceable))
    return A->getDereferenceableBytes();
  return 0;
}

uint64_t AttributeSetNode::getDereferenceableOrNullBytes() const {
  if (auto A = findEnumAttribute(Attribute::DereferenceableOrNull))
    return A->getDereferenceableOrNullBytes();
  return 0;
}

std::optional<std::pair<unsigned, std::optional<unsigned>>>
AttributeSetNode::getAllocSizeArgs() const {
  if (auto A = findEnumAttribute(Attribute::AllocSize))
    return A->getAllocSizeArgs();
  return std::nullopt;
}

unsigned AttributeSetNode::getVScaleRangeMin() const {
  if (auto A = findEnumAttribute(Attribute::VScaleRange))
    return A->getVScaleRangeMin();
  return 1;
}

std::optional<unsigned> AttributeSetNode::getVScaleRangeMax() const {
  if (auto A = findEnumAttribute(Attribute::VScaleRange))
    return A->getVScaleRangeMax();
  return std::nullopt;
}

UWTableKind AttributeSetNode::getUWTableKind() const {
  if (auto A = findEnumAttribute(Attribute::UWTable))
    return A->getUWTableKind();
  return UWTableKind::None;
}

AllocFnKind AttributeSetNode::getAllocKind() const {
  if (auto A = findEnumAttribute(Attribute::AllocKind))
    return A->getAllocKind();
  return AllocFnKind::Unknown;
}

MemoryEffects AttributeSetNode::getMemoryEffects() const {
  if (auto A = findEnumAttribute(Attribute::Memory))
    return A->getMemoryEffects();
  return MemoryEffects::unknown();
}

FPClassTest AttributeSetNode::getNoFPClass() const {
  if (auto A = findEnumAttribute(Attribute::NoFPClass))
    return A->getNoFPClass();
  return fcNone;
}

std::string AttributeSetNode::getAsString(bool InAttrGrp) const {
  std::string Str;
  for (iterator I = begin(), E = end(); I != E; ++I) {
    if (I != begin())
      Str += ' ';
    Str += I->getAsString(InAttrGrp);
  }
  return Str;
}

//===----------------------------------------------------------------------===//
// AttributeListImpl Definition
//===----------------------------------------------------------------------===//

/// Map from AttributeList index to the internal array index. Adding one happens
/// to work, because -1 wraps around to 0.
static unsigned attrIdxToArrayIdx(unsigned Index) {
  return Index + 1;
}

AttributeListImpl::AttributeListImpl(ArrayRef<AttributeSet> Sets)
    : NumAttrSets(Sets.size()) {
  assert(!Sets.empty() && "pointless AttributeListImpl");

  // There's memory after the node where we can store the entries in.
  llvm::copy(Sets, getTrailingObjects<AttributeSet>());

  // Initialize AvailableFunctionAttrs and AvailableSomewhereAttrs
  // summary bitsets.
  for (const auto &I : Sets[attrIdxToArrayIdx(AttributeList::FunctionIndex)])
    if (!I.isStringAttribute())
      AvailableFunctionAttrs.addAttribute(I.getKindAsEnum());

  for (const auto &Set : Sets)
    for (const auto &I : Set)
      if (!I.isStringAttribute())
        AvailableSomewhereAttrs.addAttribute(I.getKindAsEnum());
}

void AttributeListImpl::Profile(FoldingSetNodeID &ID) const {
  Profile(ID, ArrayRef(begin(), end()));
}

void AttributeListImpl::Profile(FoldingSetNodeID &ID,
                                ArrayRef<AttributeSet> Sets) {
  for (const auto &Set : Sets)
    ID.AddPointer(Set.SetNode);
}

bool AttributeListImpl::hasAttrSomewhere(Attribute::AttrKind Kind,
                                        unsigned *Index) const {
  if (!AvailableSomewhereAttrs.hasAttribute(Kind))
    return false;

  if (Index) {
    for (unsigned I = 0, E = NumAttrSets; I != E; ++I) {
      if (begin()[I].hasAttribute(Kind)) {
        *Index = I - 1;
        break;
      }
    }
  }

  return true;
}


#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AttributeListImpl::dump() const {
  AttributeList(const_cast<AttributeListImpl *>(this)).dump();
}
#endif

//===----------------------------------------------------------------------===//
// AttributeList Construction and Mutation Methods
//===----------------------------------------------------------------------===//

AttributeList AttributeList::getImpl(LLVMContext &C,
                                     ArrayRef<AttributeSet> AttrSets) {
  assert(!AttrSets.empty() && "pointless AttributeListImpl");

  LLVMContextImpl *pImpl = C.pImpl;
  FoldingSetNodeID ID;
  AttributeListImpl::Profile(ID, AttrSets);

  void *InsertPoint;
  AttributeListImpl *PA =
      pImpl->AttrsLists.FindNodeOrInsertPos(ID, InsertPoint);

  // If we didn't find any existing attributes of the same shape then
  // create a new one and insert it.
  if (!PA) {
    // Coallocate entries after the AttributeListImpl itself.
    void *Mem = pImpl->Alloc.Allocate(
        AttributeListImpl::totalSizeToAlloc<AttributeSet>(AttrSets.size()),
        alignof(AttributeListImpl));
    PA = new (Mem) AttributeListImpl(AttrSets);
    pImpl->AttrsLists.InsertNode(PA, InsertPoint);
  }

  // Return the AttributesList that we found or created.
  return AttributeList(PA);
}

AttributeList
AttributeList::get(LLVMContext &C,
                   ArrayRef<std::pair<unsigned, Attribute>> Attrs) {
  // If there are no attributes then return a null AttributesList pointer.
  if (Attrs.empty())
    return {};

  assert(llvm::is_sorted(Attrs, llvm::less_first()) &&
         "Misordered Attributes list!");
  assert(llvm::all_of(Attrs,
                      [](const std::pair<unsigned, Attribute> &Pair) {
                        return Pair.second.isValid();
                      }) &&
         "Pointless attribute!");

  // Create a vector if (unsigned, AttributeSetNode*) pairs from the attributes
  // list.
  SmallVector<std::pair<unsigned, AttributeSet>, 8> AttrPairVec;
  for (ArrayRef<std::pair<unsigned, Attribute>>::iterator I = Attrs.begin(),
         E = Attrs.end(); I != E; ) {
    unsigned Index = I->first;
    SmallVector<Attribute, 4> AttrVec;
    while (I != E && I->first == Index) {
      AttrVec.push_back(I->second);
      ++I;
    }

    AttrPairVec.emplace_back(Index, AttributeSet::get(C, AttrVec));
  }

  return get(C, AttrPairVec);
}

AttributeList
AttributeList::get(LLVMContext &C,
                   ArrayRef<std::pair<unsigned, AttributeSet>> Attrs) {
  // If there are no attributes then return a null AttributesList pointer.
  if (Attrs.empty())
    return {};

  assert(llvm::is_sorted(Attrs, llvm::less_first()) &&
         "Misordered Attributes list!");
  assert(llvm::none_of(Attrs,
                       [](const std::pair<unsigned, AttributeSet> &Pair) {
                         return !Pair.second.hasAttributes();
                       }) &&
         "Pointless attribute!");

  unsigned MaxIndex = Attrs.back().first;
  // If the MaxIndex is FunctionIndex and there are other indices in front
  // of it, we need to use the largest of those to get the right size.
  if (MaxIndex == FunctionIndex && Attrs.size() > 1)
    MaxIndex = Attrs[Attrs.size() - 2].first;

  SmallVector<AttributeSet, 4> AttrVec(attrIdxToArrayIdx(MaxIndex) + 1);
  for (const auto &Pair : Attrs)
    AttrVec[attrIdxToArrayIdx(Pair.first)] = Pair.second;

  return getImpl(C, AttrVec);
}

AttributeList AttributeList::get(LLVMContext &C, AttributeSet FnAttrs,
                                 AttributeSet RetAttrs,
                                 ArrayRef<AttributeSet> ArgAttrs) {
  // Scan from the end to find the last argument with attributes.  Most
  // arguments don't have attributes, so it's nice if we can have fewer unique
  // AttributeListImpls by dropping empty attribute sets at the end of the list.
  unsigned NumSets = 0;
  for (size_t I = ArgAttrs.size(); I != 0; --I) {
    if (ArgAttrs[I - 1].hasAttributes()) {
      NumSets = I + 2;
      break;
    }
  }
  if (NumSets == 0) {
    // Check function and return attributes if we didn't have argument
    // attributes.
    if (RetAttrs.hasAttributes())
      NumSets = 2;
    else if (FnAttrs.hasAttributes())
      NumSets = 1;
  }

  // If all attribute sets were empty, we can use the empty attribute list.
  if (NumSets == 0)
    return {};

  SmallVector<AttributeSet, 8> AttrSets;
  AttrSets.reserve(NumSets);
  // If we have any attributes, we always have function attributes.
  AttrSets.push_back(FnAttrs);
  if (NumSets > 1)
    AttrSets.push_back(RetAttrs);
  if (NumSets > 2) {
    // Drop the empty argument attribute sets at the end.
    ArgAttrs = ArgAttrs.take_front(NumSets - 2);
    llvm::append_range(AttrSets, ArgAttrs);
  }

  return getImpl(C, AttrSets);
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 AttributeSet Attrs) {
  if (!Attrs.hasAttributes())
    return {};
  Index = attrIdxToArrayIdx(Index);
  SmallVector<AttributeSet, 8> AttrSets(Index + 1);
  AttrSets[Index] = Attrs;
  return getImpl(C, AttrSets);
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 const AttrBuilder &B) {
  return get(C, Index, AttributeSet::get(C, B));
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 ArrayRef<Attribute::AttrKind> Kinds) {
  SmallVector<std::pair<unsigned, Attribute>, 8> Attrs;
  for (const auto K : Kinds)
    Attrs.emplace_back(Index, Attribute::get(C, K));
  return get(C, Attrs);
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 ArrayRef<Attribute::AttrKind> Kinds,
                                 ArrayRef<uint64_t> Values) {
  assert(Kinds.size() == Values.size() && "Mismatched attribute values.");
  SmallVector<std::pair<unsigned, Attribute>, 8> Attrs;
  auto VI = Values.begin();
  for (const auto K : Kinds)
    Attrs.emplace_back(Index, Attribute::get(C, K, *VI++));
  return get(C, Attrs);
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 ArrayRef<StringRef> Kinds) {
  SmallVector<std::pair<unsigned, Attribute>, 8> Attrs;
  for (const auto &K : Kinds)
    Attrs.emplace_back(Index, Attribute::get(C, K));
  return get(C, Attrs);
}

AttributeList AttributeList::get(LLVMContext &C,
                                 ArrayRef<AttributeList> Attrs) {
  if (Attrs.empty())
    return {};
  if (Attrs.size() == 1)
    return Attrs[0];

  unsigned MaxSize = 0;
  for (const auto &List : Attrs)
    MaxSize = std::max(MaxSize, List.getNumAttrSets());

  // If every list was empty, there is no point in merging the lists.
  if (MaxSize == 0)
    return {};

  SmallVector<AttributeSet, 8> NewAttrSets(MaxSize);
  for (unsigned I = 0; I < MaxSize; ++I) {
    AttrBuilder CurBuilder(C);
    for (const auto &List : Attrs)
      CurBuilder.merge(AttrBuilder(C, List.getAttributes(I - 1)));
    NewAttrSets[I] = AttributeSet::get(C, CurBuilder);
  }

  return getImpl(C, NewAttrSets);
}

AttributeList
AttributeList::addAttributeAtIndex(LLVMContext &C, unsigned Index,
                                   Attribute::AttrKind Kind) const {
  AttributeSet Attrs = getAttributes(Index);
  if (Attrs.hasAttribute(Kind))
    return *this;
  // TODO: Insert at correct position and avoid sort.
  SmallVector<Attribute, 8> NewAttrs(Attrs.begin(), Attrs.end());
  NewAttrs.push_back(Attribute::get(C, Kind));
  return setAttributesAtIndex(C, Index, AttributeSet::get(C, NewAttrs));
}

AttributeList AttributeList::addAttributeAtIndex(LLVMContext &C, unsigned Index,
                                                 StringRef Kind,
                                                 StringRef Value) const {
  AttrBuilder B(C);
  B.addAttribute(Kind, Value);
  return addAttributesAtIndex(C, Index, B);
}

AttributeList AttributeList::addAttributeAtIndex(LLVMContext &C, unsigned Index,
                                                 Attribute A) const {
  AttrBuilder B(C);
  B.addAttribute(A);
  return addAttributesAtIndex(C, Index, B);
}

AttributeList AttributeList::setAttributesAtIndex(LLVMContext &C,
                                                  unsigned Index,
                                                  AttributeSet Attrs) const {
  Index = attrIdxToArrayIdx(Index);
  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  if (Index >= AttrSets.size())
    AttrSets.resize(Index + 1);
  AttrSets[Index] = Attrs;

  // Remove trailing empty attribute sets.
  while (!AttrSets.empty() && !AttrSets.back().hasAttributes())
    AttrSets.pop_back();
  if (AttrSets.empty())
    return {};
  return AttributeList::getImpl(C, AttrSets);
}

AttributeList AttributeList::addAttributesAtIndex(LLVMContext &C,
                                                  unsigned Index,
                                                  const AttrBuilder &B) const {
  if (!B.hasAttributes())
    return *this;

  if (!pImpl)
    return AttributeList::get(C, {{Index, AttributeSet::get(C, B)}});

  AttrBuilder Merged(C, getAttributes(Index));
  Merged.merge(B);
  return setAttributesAtIndex(C, Index, AttributeSet::get(C, Merged));
}

AttributeList AttributeList::addParamAttribute(LLVMContext &C,
                                               ArrayRef<unsigned> ArgNos,
                                               Attribute A) const {
  assert(llvm::is_sorted(ArgNos));

  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  unsigned MaxIndex = attrIdxToArrayIdx(ArgNos.back() + FirstArgIndex);
  if (MaxIndex >= AttrSets.size())
    AttrSets.resize(MaxIndex + 1);

  for (unsigned ArgNo : ArgNos) {
    unsigned Index = attrIdxToArrayIdx(ArgNo + FirstArgIndex);
    AttrBuilder B(C, AttrSets[Index]);
    B.addAttribute(A);
    AttrSets[Index] = AttributeSet::get(C, B);
  }

  return getImpl(C, AttrSets);
}

AttributeList
AttributeList::removeAttributeAtIndex(LLVMContext &C, unsigned Index,
                                      Attribute::AttrKind Kind) const {
  AttributeSet Attrs = getAttributes(Index);
  AttributeSet NewAttrs = Attrs.removeAttribute(C, Kind);
  if (Attrs == NewAttrs)
    return *this;
  return setAttributesAtIndex(C, Index, NewAttrs);
}

AttributeList AttributeList::removeAttributeAtIndex(LLVMContext &C,
                                                    unsigned Index,
                                                    StringRef Kind) const {
  AttributeSet Attrs = getAttributes(Index);
  AttributeSet NewAttrs = Attrs.removeAttribute(C, Kind);
  if (Attrs == NewAttrs)
    return *this;
  return setAttributesAtIndex(C, Index, NewAttrs);
}

AttributeList AttributeList::removeAttributesAtIndex(
    LLVMContext &C, unsigned Index, const AttributeMask &AttrsToRemove) const {
  AttributeSet Attrs = getAttributes(Index);
  AttributeSet NewAttrs = Attrs.removeAttributes(C, AttrsToRemove);
  // If nothing was removed, return the original list.
  if (Attrs == NewAttrs)
    return *this;
  return setAttributesAtIndex(C, Index, NewAttrs);
}

AttributeList
AttributeList::removeAttributesAtIndex(LLVMContext &C,
                                       unsigned WithoutIndex) const {
  if (!pImpl)
    return {};
  if (attrIdxToArrayIdx(WithoutIndex) >= getNumAttrSets())
    return *this;
  return setAttributesAtIndex(C, WithoutIndex, AttributeSet());
}

AttributeList AttributeList::addDereferenceableRetAttr(LLVMContext &C,
                                                       uint64_t Bytes) const {
  AttrBuilder B(C);
  B.addDereferenceableAttr(Bytes);
  return addRetAttributes(C, B);
}

AttributeList AttributeList::addDereferenceableParamAttr(LLVMContext &C,
                                                         unsigned Index,
                                                         uint64_t Bytes) const {
  AttrBuilder B(C);
  B.addDereferenceableAttr(Bytes);
  return addParamAttributes(C, Index, B);
}

AttributeList
AttributeList::addDereferenceableOrNullParamAttr(LLVMContext &C, unsigned Index,
                                                 uint64_t Bytes) const {
  AttrBuilder B(C);
  B.addDereferenceableOrNullAttr(Bytes);
  return addParamAttributes(C, Index, B);
}

AttributeList AttributeList::addRangeRetAttr(LLVMContext &C,
                                             const ConstantRange &CR) const {
  AttrBuilder B(C);
  B.addRangeAttr(CR);
  return addRetAttributes(C, B);
}

AttributeList AttributeList::addAllocSizeParamAttr(
    LLVMContext &C, unsigned Index, unsigned ElemSizeArg,
    const std::optional<unsigned> &NumElemsArg) {
  AttrBuilder B(C);
  B.addAllocSizeAttr(ElemSizeArg, NumElemsArg);
  return addParamAttributes(C, Index, B);
}

//===----------------------------------------------------------------------===//
// AttributeList Accessor Methods
//===----------------------------------------------------------------------===//

AttributeSet AttributeList::getParamAttrs(unsigned ArgNo) const {
  return getAttributes(ArgNo + FirstArgIndex);
}

AttributeSet AttributeList::getRetAttrs() const {
  return getAttributes(ReturnIndex);
}

AttributeSet AttributeList::getFnAttrs() const {
  return getAttributes(FunctionIndex);
}

bool AttributeList::hasAttributeAtIndex(unsigned Index,
                                        Attribute::AttrKind Kind) const {
  return getAttributes(Index).hasAttribute(Kind);
}

bool AttributeList::hasAttributeAtIndex(unsigned Index, StringRef Kind) const {
  return getAttributes(Index).hasAttribute(Kind);
}

bool AttributeList::hasAttributesAtIndex(unsigned Index) const {
  return getAttributes(Index).hasAttributes();
}

bool AttributeList::hasFnAttr(Attribute::AttrKind Kind) const {
  return pImpl && pImpl->hasFnAttribute(Kind);
}

bool AttributeList::hasFnAttr(StringRef Kind) const {
  return hasAttributeAtIndex(AttributeList::FunctionIndex, Kind);
}

bool AttributeList::hasAttrSomewhere(Attribute::AttrKind Attr,
                                     unsigned *Index) const {
  return pImpl && pImpl->hasAttrSomewhere(Attr, Index);
}

Attribute AttributeList::getAttributeAtIndex(unsigned Index,
                                             Attribute::AttrKind Kind) const {
  return getAttributes(Index).getAttribute(Kind);
}

Attribute AttributeList::getAttributeAtIndex(unsigned Index,
                                             StringRef Kind) const {
  return getAttributes(Index).getAttribute(Kind);
}

MaybeAlign AttributeList::getRetAlignment() const {
  return getAttributes(ReturnIndex).getAlignment();
}

MaybeAlign AttributeList::getParamAlignment(unsigned ArgNo) const {
  return getAttributes(ArgNo + FirstArgIndex).getAlignment();
}

MaybeAlign AttributeList::getParamStackAlignment(unsigned ArgNo) const {
  return getAttributes(ArgNo + FirstArgIndex).getStackAlignment();
}

Type *AttributeList::getParamByValType(unsigned Index) const {
  return getAttributes(Index+FirstArgIndex).getByValType();
}

Type *AttributeList::getParamStructRetType(unsigned Index) const {
  return getAttributes(Index + FirstArgIndex).getStructRetType();
}

Type *AttributeList::getParamByRefType(unsigned Index) const {
  return getAttributes(Index + FirstArgIndex).getByRefType();
}

Type *AttributeList::getParamPreallocatedType(unsigned Index) const {
  return getAttributes(Index + FirstArgIndex).getPreallocatedType();
}

Type *AttributeList::getParamInAllocaType(unsigned Index) const {
  return getAttributes(Index + FirstArgIndex).getInAllocaType();
}

Type *AttributeList::getParamElementType(unsigned Index) const {
  return getAttributes(Index + FirstArgIndex).getElementType();
}

MaybeAlign AttributeList::getFnStackAlignment() const {
  return getFnAttrs().getStackAlignment();
}

MaybeAlign AttributeList::getRetStackAlignment() const {
  return getRetAttrs().getStackAlignment();
}

uint64_t AttributeList::getRetDereferenceableBytes() const {
  return getRetAttrs().getDereferenceableBytes();
}

uint64_t AttributeList::getParamDereferenceableBytes(unsigned Index) const {
  return getParamAttrs(Index).getDereferenceableBytes();
}

uint64_t AttributeList::getRetDereferenceableOrNullBytes() const {
  return getRetAttrs().getDereferenceableOrNullBytes();
}

uint64_t
AttributeList::getParamDereferenceableOrNullBytes(unsigned Index) const {
  return getParamAttrs(Index).getDereferenceableOrNullBytes();
}

FPClassTest AttributeList::getRetNoFPClass() const {
  return getRetAttrs().getNoFPClass();
}

FPClassTest AttributeList::getParamNoFPClass(unsigned Index) const {
  return getParamAttrs(Index).getNoFPClass();
}

UWTableKind AttributeList::getUWTableKind() const {
  return getFnAttrs().getUWTableKind();
}

AllocFnKind AttributeList::getAllocKind() const {
  return getFnAttrs().getAllocKind();
}

MemoryEffects AttributeList::getMemoryEffects() const {
  return getFnAttrs().getMemoryEffects();
}

std::string AttributeList::getAsString(unsigned Index, bool InAttrGrp) const {
  return getAttributes(Index).getAsString(InAttrGrp);
}

AttributeSet AttributeList::getAttributes(unsigned Index) const {
  Index = attrIdxToArrayIdx(Index);
  if (!pImpl || Index >= getNumAttrSets())
    return {};
  return pImpl->begin()[Index];
}

bool AttributeList::hasParentContext(LLVMContext &C) const {
  assert(!isEmpty() && "an empty attribute list has no parent context");
  FoldingSetNodeID ID;
  pImpl->Profile(ID);
  void *Unused;
  return C.pImpl->AttrsLists.FindNodeOrInsertPos(ID, Unused) == pImpl;
}

AttributeList::iterator AttributeList::begin() const {
  return pImpl ? pImpl->begin() : nullptr;
}

AttributeList::iterator AttributeList::end() const {
  return pImpl ? pImpl->end() : nullptr;
}

//===----------------------------------------------------------------------===//
// AttributeList Introspection Methods
//===----------------------------------------------------------------------===//

unsigned AttributeList::getNumAttrSets() const {
  return pImpl ? pImpl->NumAttrSets : 0;
}

void AttributeList::print(raw_ostream &O) const {
  O << "AttributeList[\n";

  for (unsigned i : indexes()) {
    if (!getAttributes(i).hasAttributes())
      continue;
    O << "  { ";
    switch (i) {
    case AttrIndex::ReturnIndex:
      O << "return";
      break;
    case AttrIndex::FunctionIndex:
      O << "function";
      break;
    default:
      O << "arg(" << i - AttrIndex::FirstArgIndex << ")";
    }
    O << " => " << getAsString(i) << " }\n";
  }

  O << "]\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AttributeList::dump() const { print(dbgs()); }
#endif

//===----------------------------------------------------------------------===//
// AttrBuilder Method Implementations
//===----------------------------------------------------------------------===//

AttrBuilder::AttrBuilder(LLVMContext &Ctx, AttributeSet AS) : Ctx(Ctx) {
  append_range(Attrs, AS);
  assert(is_sorted(Attrs) && "AttributeSet should be sorted");
}

void AttrBuilder::clear() { Attrs.clear(); }

/// Attribute comparator that only compares attribute keys. Enum attributes are
/// sorted before string attributes.
struct AttributeComparator {
  bool operator()(Attribute A0, Attribute A1) const {
    bool A0IsString = A0.isStringAttribute();
    bool A1IsString = A1.isStringAttribute();
    if (A0IsString) {
      if (A1IsString)
        return A0.getKindAsString() < A1.getKindAsString();
      else
        return false;
    }
    if (A1IsString)
      return true;
    return A0.getKindAsEnum() < A1.getKindAsEnum();
  }
  bool operator()(Attribute A0, Attribute::AttrKind Kind) const {
    if (A0.isStringAttribute())
      return false;
    return A0.getKindAsEnum() < Kind;
  }
  bool operator()(Attribute A0, StringRef Kind) const {
    if (A0.isStringAttribute())
      return A0.getKindAsString() < Kind;
    return true;
  }
};

template <typename K>
static void addAttributeImpl(SmallVectorImpl<Attribute> &Attrs, K Kind,
                             Attribute Attr) {
  auto It = lower_bound(Attrs, Kind, AttributeComparator());
  if (It != Attrs.end() && It->hasAttribute(Kind))
    std::swap(*It, Attr);
  else
    Attrs.insert(It, Attr);
}

AttrBuilder &AttrBuilder::addAttribute(Attribute Attr) {
  if (Attr.isStringAttribute())
    addAttributeImpl(Attrs, Attr.getKindAsString(), Attr);
  else
    addAttributeImpl(Attrs, Attr.getKindAsEnum(), Attr);
  return *this;
}

AttrBuilder &AttrBuilder::addAttribute(Attribute::AttrKind Kind) {
  addAttributeImpl(Attrs, Kind, Attribute::get(Ctx, Kind));
  return *this;
}

AttrBuilder &AttrBuilder::addAttribute(StringRef A, StringRef V) {
  addAttributeImpl(Attrs, A, Attribute::get(Ctx, A, V));
  return *this;
}

AttrBuilder &AttrBuilder::removeAttribute(Attribute::AttrKind Val) {
  assert((unsigned)Val < Attribute::EndAttrKinds && "Attribute out of range!");
  auto It = lower_bound(Attrs, Val, AttributeComparator());
  if (It != Attrs.end() && It->hasAttribute(Val))
    Attrs.erase(It);
  return *this;
}

AttrBuilder &AttrBuilder::removeAttribute(StringRef A) {
  auto It = lower_bound(Attrs, A, AttributeComparator());
  if (It != Attrs.end() && It->hasAttribute(A))
    Attrs.erase(It);
  return *this;
}

std::optional<uint64_t>
AttrBuilder::getRawIntAttr(Attribute::AttrKind Kind) const {
  assert(Attribute::isIntAttrKind(Kind) && "Not an int attribute");
  Attribute A = getAttribute(Kind);
  if (A.isValid())
    return A.getValueAsInt();
  return std::nullopt;
}

AttrBuilder &AttrBuilder::addRawIntAttr(Attribute::AttrKind Kind,
                                        uint64_t Value) {
  return addAttribute(Attribute::get(Ctx, Kind, Value));
}

std::optional<std::pair<unsigned, std::optional<unsigned>>>
AttrBuilder::getAllocSizeArgs() const {
  Attribute A = getAttribute(Attribute::AllocSize);
  if (A.isValid())
    return A.getAllocSizeArgs();
  return std::nullopt;
}

AttrBuilder &AttrBuilder::addAlignmentAttr(MaybeAlign Align) {
  if (!Align)
    return *this;

  assert(*Align <= llvm::Value::MaximumAlignment && "Alignment too large.");
  return addRawIntAttr(Attribute::Alignment, Align->value());
}

AttrBuilder &AttrBuilder::addStackAlignmentAttr(MaybeAlign Align) {
  // Default alignment, allow the target to define how to align it.
  if (!Align)
    return *this;

  assert(*Align <= 0x100 && "Alignment too large.");
  return addRawIntAttr(Attribute::StackAlignment, Align->value());
}

AttrBuilder &AttrBuilder::addDereferenceableAttr(uint64_t Bytes) {
  if (Bytes == 0) return *this;

  return addRawIntAttr(Attribute::Dereferenceable, Bytes);
}

AttrBuilder &AttrBuilder::addDereferenceableOrNullAttr(uint64_t Bytes) {
  if (Bytes == 0)
    return *this;

  return addRawIntAttr(Attribute::DereferenceableOrNull, Bytes);
}

AttrBuilder &
AttrBuilder::addAllocSizeAttr(unsigned ElemSize,
                              const std::optional<unsigned> &NumElems) {
  return addAllocSizeAttrFromRawRepr(packAllocSizeArgs(ElemSize, NumElems));
}

AttrBuilder &AttrBuilder::addAllocSizeAttrFromRawRepr(uint64_t RawArgs) {
  // (0, 0) is our "not present" value, so we need to check for it here.
  assert(RawArgs && "Invalid allocsize arguments -- given allocsize(0, 0)");
  return addRawIntAttr(Attribute::AllocSize, RawArgs);
}

AttrBuilder &AttrBuilder::addVScaleRangeAttr(unsigned MinValue,
                                             std::optional<unsigned> MaxValue) {
  return addVScaleRangeAttrFromRawRepr(packVScaleRangeArgs(MinValue, MaxValue));
}

AttrBuilder &AttrBuilder::addVScaleRangeAttrFromRawRepr(uint64_t RawArgs) {
  // (0, 0) is not present hence ignore this case
  if (RawArgs == 0)
    return *this;

  return addRawIntAttr(Attribute::VScaleRange, RawArgs);
}

AttrBuilder &AttrBuilder::addUWTableAttr(UWTableKind Kind) {
  if (Kind == UWTableKind::None)
    return *this;
  return addRawIntAttr(Attribute::UWTable, uint64_t(Kind));
}

AttrBuilder &AttrBuilder::addMemoryAttr(MemoryEffects ME) {
  return addRawIntAttr(Attribute::Memory, ME.toIntValue());
}

AttrBuilder &AttrBuilder::addNoFPClassAttr(FPClassTest Mask) {
  if (Mask == fcNone)
    return *this;

  return addRawIntAttr(Attribute::NoFPClass, Mask);
}

AttrBuilder &AttrBuilder::addAllocKindAttr(AllocFnKind Kind) {
  return addRawIntAttr(Attribute::AllocKind, static_cast<uint64_t>(Kind));
}

Type *AttrBuilder::getTypeAttr(Attribute::AttrKind Kind) const {
  assert(Attribute::isTypeAttrKind(Kind) && "Not a type attribute");
  Attribute A = getAttribute(Kind);
  return A.isValid() ? A.getValueAsType() : nullptr;
}

AttrBuilder &AttrBuilder::addTypeAttr(Attribute::AttrKind Kind, Type *Ty) {
  return addAttribute(Attribute::get(Ctx, Kind, Ty));
}

AttrBuilder &AttrBuilder::addByValAttr(Type *Ty) {
  return addTypeAttr(Attribute::ByVal, Ty);
}

AttrBuilder &AttrBuilder::addStructRetAttr(Type *Ty) {
  return addTypeAttr(Attribute::StructRet, Ty);
}

AttrBuilder &AttrBuilder::addByRefAttr(Type *Ty) {
  return addTypeAttr(Attribute::ByRef, Ty);
}

AttrBuilder &AttrBuilder::addPreallocatedAttr(Type *Ty) {
  return addTypeAttr(Attribute::Preallocated, Ty);
}

AttrBuilder &AttrBuilder::addInAllocaAttr(Type *Ty) {
  return addTypeAttr(Attribute::InAlloca, Ty);
}

AttrBuilder &AttrBuilder::addConstantRangeAttr(Attribute::AttrKind Kind,
                                               const ConstantRange &CR) {
  return addAttribute(Attribute::get(Ctx, Kind, CR));
}

AttrBuilder &AttrBuilder::addRangeAttr(const ConstantRange &CR) {
  return addConstantRangeAttr(Attribute::Range, CR);
}

AttrBuilder &
AttrBuilder::addConstantRangeListAttr(Attribute::AttrKind Kind,
                                      ArrayRef<ConstantRange> Val) {
  return addAttribute(Attribute::get(Ctx, Kind, Val));
}

AttrBuilder &AttrBuilder::addInitializesAttr(const ConstantRangeList &CRL) {
  return addConstantRangeListAttr(Attribute::Initializes, CRL.rangesRef());
}

AttrBuilder &AttrBuilder::merge(const AttrBuilder &B) {
  // TODO: Could make this O(n) as we're merging two sorted lists.
  for (const auto &I : B.attrs())
    addAttribute(I);

  return *this;
}

AttrBuilder &AttrBuilder::remove(const AttributeMask &AM) {
  erase_if(Attrs, [&](Attribute A) { return AM.contains(A); });
  return *this;
}

bool AttrBuilder::overlaps(const AttributeMask &AM) const {
  return any_of(Attrs, [&](Attribute A) { return AM.contains(A); });
}

Attribute AttrBuilder::getAttribute(Attribute::AttrKind A) const {
  assert((unsigned)A < Attribute::EndAttrKinds && "Attribute out of range!");
  auto It = lower_bound(Attrs, A, AttributeComparator());
  if (It != Attrs.end() && It->hasAttribute(A))
    return *It;
  return {};
}

Attribute AttrBuilder::getAttribute(StringRef A) const {
  auto It = lower_bound(Attrs, A, AttributeComparator());
  if (It != Attrs.end() && It->hasAttribute(A))
    return *It;
  return {};
}

bool AttrBuilder::contains(Attribute::AttrKind A) const {
  return getAttribute(A).isValid();
}

bool AttrBuilder::contains(StringRef A) const {
  return getAttribute(A).isValid();
}

bool AttrBuilder::operator==(const AttrBuilder &B) const {
  return Attrs == B.Attrs;
}

//===----------------------------------------------------------------------===//
// AttributeFuncs Function Defintions
//===----------------------------------------------------------------------===//

/// Returns true if this is a type legal for the 'nofpclass' attribute. This
/// follows the same type rules as FPMathOperator.
///
/// TODO: Consider relaxing to any FP type struct fields.
bool AttributeFuncs::isNoFPClassCompatibleType(Type *Ty) {
  while (ArrayType *ArrTy = dyn_cast<ArrayType>(Ty))
    Ty = ArrTy->getElementType();
  return Ty->isFPOrFPVectorTy();
}

/// Which attributes cannot be applied to a type.
AttributeMask AttributeFuncs::typeIncompatible(Type *Ty,
                                               AttributeSafetyKind ASK) {
  AttributeMask Incompatible;

  if (!Ty->isIntegerTy()) {
    // Attributes that only apply to integers.
    if (ASK & ASK_SAFE_TO_DROP)
      Incompatible.addAttribute(Attribute::AllocAlign);
    if (ASK & ASK_UNSAFE_TO_DROP)
      Incompatible.addAttribute(Attribute::SExt).addAttribute(Attribute::ZExt);
  }

  if (!Ty->isIntOrIntVectorTy()) {
    // Attributes that only apply to integers or vector of integers.
    if (ASK & ASK_SAFE_TO_DROP)
      Incompatible.addAttribute(Attribute::Range);
  }

  if (!Ty->isPointerTy()) {
    // Attributes that only apply to pointers.
    if (ASK & ASK_SAFE_TO_DROP)
      Incompatible.addAttribute(Attribute::NoAlias)
          .addAttribute(Attribute::NoCapture)
          .addAttribute(Attribute::NonNull)
          .addAttribute(Attribute::ReadNone)
          .addAttribute(Attribute::ReadOnly)
          .addAttribute(Attribute::Dereferenceable)
          .addAttribute(Attribute::DereferenceableOrNull)
          .addAttribute(Attribute::Writable)
          .addAttribute(Attribute::DeadOnUnwind)
          .addAttribute(Attribute::Initializes);
    if (ASK & ASK_UNSAFE_TO_DROP)
      Incompatible.addAttribute(Attribute::Nest)
          .addAttribute(Attribute::SwiftError)
          .addAttribute(Attribute::Preallocated)
          .addAttribute(Attribute::InAlloca)
          .addAttribute(Attribute::ByVal)
          .addAttribute(Attribute::StructRet)
          .addAttribute(Attribute::ByRef)
          .addAttribute(Attribute::ElementType)
          .addAttribute(Attribute::AllocatedPointer);
  }

    // Attributes that only apply to pointers or vectors of pointers.
  if (!Ty->isPtrOrPtrVectorTy()) {
    if (ASK & ASK_SAFE_TO_DROP)
      Incompatible.addAttribute(Attribute::Alignment);
  }

  if (ASK & ASK_SAFE_TO_DROP) {
    if (!isNoFPClassCompatibleType(Ty))
      Incompatible.addAttribute(Attribute::NoFPClass);
  }

  // Some attributes can apply to all "values" but there are no `void` values.
  if (Ty->isVoidTy()) {
    if (ASK & ASK_SAFE_TO_DROP)
      Incompatible.addAttribute(Attribute::NoUndef);
  }

  return Incompatible;
}

AttributeMask AttributeFuncs::getUBImplyingAttributes() {
  AttributeMask AM;
  AM.addAttribute(Attribute::NoUndef);
  AM.addAttribute(Attribute::Dereferenceable);
  AM.addAttribute(Attribute::DereferenceableOrNull);
  return AM;
}

/// Callees with dynamic denormal modes are compatible with any caller mode.
static bool denormModeCompatible(DenormalMode CallerMode,
                                 DenormalMode CalleeMode) {
  if (CallerMode == CalleeMode || CalleeMode == DenormalMode::getDynamic())
    return true;

  // If they don't exactly match, it's OK if the mismatched component is
  // dynamic.
  if (CalleeMode.Input == CallerMode.Input &&
      CalleeMode.Output == DenormalMode::Dynamic)
    return true;

  if (CalleeMode.Output == CallerMode.Output &&
      CalleeMode.Input == DenormalMode::Dynamic)
    return true;
  return false;
}

static bool checkDenormMode(const Function &Caller, const Function &Callee) {
  DenormalMode CallerMode = Caller.getDenormalModeRaw();
  DenormalMode CalleeMode = Callee.getDenormalModeRaw();

  if (denormModeCompatible(CallerMode, CalleeMode)) {
    DenormalMode CallerModeF32 = Caller.getDenormalModeF32Raw();
    DenormalMode CalleeModeF32 = Callee.getDenormalModeF32Raw();
    if (CallerModeF32 == DenormalMode::getInvalid())
      CallerModeF32 = CallerMode;
    if (CalleeModeF32 == DenormalMode::getInvalid())
      CalleeModeF32 = CalleeMode;
    return denormModeCompatible(CallerModeF32, CalleeModeF32);
  }

  return false;
}

static bool checkStrictFP(const Function &Caller, const Function &Callee) {
  // Do not inline strictfp function into non-strictfp one. It would require
  // conversion of all FP operations in host function to constrained intrinsics.
  return !Callee.getAttributes().hasFnAttr(Attribute::StrictFP) ||
         Caller.getAttributes().hasFnAttr(Attribute::StrictFP);
}

template<typename AttrClass>
static bool isEqual(const Function &Caller, const Function &Callee) {
  return Caller.getFnAttribute(AttrClass::getKind()) ==
         Callee.getFnAttribute(AttrClass::getKind());
}

static bool isEqual(const Function &Caller, const Function &Callee,
                    const StringRef &AttrName) {
  return Caller.getFnAttribute(AttrName) == Callee.getFnAttribute(AttrName);
}

/// Compute the logical AND of the attributes of the caller and the
/// callee.
///
/// This function sets the caller's attribute to false if the callee's attribute
/// is false.
template<typename AttrClass>
static void setAND(Function &Caller, const Function &Callee) {
  if (AttrClass::isSet(Caller, AttrClass::getKind()) &&
      !AttrClass::isSet(Callee, AttrClass::getKind()))
    AttrClass::set(Caller, AttrClass::getKind(), false);
}

/// Compute the logical OR of the attributes of the caller and the
/// callee.
///
/// This function sets the caller's attribute to true if the callee's attribute
/// is true.
template<typename AttrClass>
static void setOR(Function &Caller, const Function &Callee) {
  if (!AttrClass::isSet(Caller, AttrClass::getKind()) &&
      AttrClass::isSet(Callee, AttrClass::getKind()))
    AttrClass::set(Caller, AttrClass::getKind(), true);
}

/// If the inlined function had a higher stack protection level than the
/// calling function, then bump up the caller's stack protection level.
static void adjustCallerSSPLevel(Function &Caller, const Function &Callee) {
  // If the calling function has *no* stack protection level (e.g. it was built
  // with Clang's -fno-stack-protector or no_stack_protector attribute), don't
  // change it as that could change the program's semantics.
  if (!Caller.hasStackProtectorFnAttr())
    return;

  // If upgrading the SSP attribute, clear out the old SSP Attributes first.
  // Having multiple SSP attributes doesn't actually hurt, but it adds useless
  // clutter to the IR.
  AttributeMask OldSSPAttr;
  OldSSPAttr.addAttribute(Attribute::StackProtect)
      .addAttribute(Attribute::StackProtectStrong)
      .addAttribute(Attribute::StackProtectReq);

  if (Callee.hasFnAttribute(Attribute::StackProtectReq)) {
    Caller.removeFnAttrs(OldSSPAttr);
    Caller.addFnAttr(Attribute::StackProtectReq);
  } else if (Callee.hasFnAttribute(Attribute::StackProtectStrong) &&
             !Caller.hasFnAttribute(Attribute::StackProtectReq)) {
    Caller.removeFnAttrs(OldSSPAttr);
    Caller.addFnAttr(Attribute::StackProtectStrong);
  } else if (Callee.hasFnAttribute(Attribute::StackProtect) &&
             !Caller.hasFnAttribute(Attribute::StackProtectReq) &&
             !Caller.hasFnAttribute(Attribute::StackProtectStrong))
    Caller.addFnAttr(Attribute::StackProtect);
}

/// If the inlined function required stack probes, then ensure that
/// the calling function has those too.
static void adjustCallerStackProbes(Function &Caller, const Function &Callee) {
  if (!Caller.hasFnAttribute("probe-stack") &&
      Callee.hasFnAttribute("probe-stack")) {
    Caller.addFnAttr(Callee.getFnAttribute("probe-stack"));
  }
}

/// If the inlined function defines the size of guard region
/// on the stack, then ensure that the calling function defines a guard region
/// that is no larger.
static void
adjustCallerStackProbeSize(Function &Caller, const Function &Callee) {
  Attribute CalleeAttr = Callee.getFnAttribute("stack-probe-size");
  if (CalleeAttr.isValid()) {
    Attribute CallerAttr = Caller.getFnAttribute("stack-probe-size");
    if (CallerAttr.isValid()) {
      uint64_t CallerStackProbeSize, CalleeStackProbeSize;
      CallerAttr.getValueAsString().getAsInteger(0, CallerStackProbeSize);
      CalleeAttr.getValueAsString().getAsInteger(0, CalleeStackProbeSize);

      if (CallerStackProbeSize > CalleeStackProbeSize) {
        Caller.addFnAttr(CalleeAttr);
      }
    } else {
      Caller.addFnAttr(CalleeAttr);
    }
  }
}

/// If the inlined function defines a min legal vector width, then ensure
/// the calling function has the same or larger min legal vector width. If the
/// caller has the attribute, but the callee doesn't, we need to remove the
/// attribute from the caller since we can't make any guarantees about the
/// caller's requirements.
/// This function is called after the inlining decision has been made so we have
/// to merge the attribute this way. Heuristics that would use
/// min-legal-vector-width to determine inline compatibility would need to be
/// handled as part of inline cost analysis.
static void
adjustMinLegalVectorWidth(Function &Caller, const Function &Callee) {
  Attribute CallerAttr = Caller.getFnAttribute("min-legal-vector-width");
  if (CallerAttr.isValid()) {
    Attribute CalleeAttr = Callee.getFnAttribute("min-legal-vector-width");
    if (CalleeAttr.isValid()) {
      uint64_t CallerVectorWidth, CalleeVectorWidth;
      CallerAttr.getValueAsString().getAsInteger(0, CallerVectorWidth);
      CalleeAttr.getValueAsString().getAsInteger(0, CalleeVectorWidth);
      if (CallerVectorWidth < CalleeVectorWidth)
        Caller.addFnAttr(CalleeAttr);
    } else {
      // If the callee doesn't have the attribute then we don't know anything
      // and must drop the attribute from the caller.
      Caller.removeFnAttr("min-legal-vector-width");
    }
  }
}

/// If the inlined function has null_pointer_is_valid attribute,
/// set this attribute in the caller post inlining.
static void
adjustNullPointerValidAttr(Function &Caller, const Function &Callee) {
  if (Callee.nullPointerIsDefined() && !Caller.nullPointerIsDefined()) {
    Caller.addFnAttr(Attribute::NullPointerIsValid);
  }
}

struct EnumAttr {
  static bool isSet(const Function &Fn,
                    Attribute::AttrKind Kind) {
    return Fn.hasFnAttribute(Kind);
  }

  static void set(Function &Fn,
                  Attribute::AttrKind Kind, bool Val) {
    if (Val)
      Fn.addFnAttr(Kind);
    else
      Fn.removeFnAttr(Kind);
  }
};

struct StrBoolAttr {
  static bool isSet(const Function &Fn,
                    StringRef Kind) {
    auto A = Fn.getFnAttribute(Kind);
    return A.getValueAsString() == "true";
  }

  static void set(Function &Fn,
                  StringRef Kind, bool Val) {
    Fn.addFnAttr(Kind, Val ? "true" : "false");
  }
};

#define GET_ATTR_NAMES
#define ATTRIBUTE_ENUM(ENUM_NAME, DISPLAY_NAME)                                \
  struct ENUM_NAME##Attr : EnumAttr {                                          \
    static enum Attribute::AttrKind getKind() {                                \
      return llvm::Attribute::ENUM_NAME;                                       \
    }                                                                          \
  };
#define ATTRIBUTE_STRBOOL(ENUM_NAME, DISPLAY_NAME)                             \
  struct ENUM_NAME##Attr : StrBoolAttr {                                       \
    static StringRef getKind() { return #DISPLAY_NAME; }                       \
  };
#include "llvm/IR/Attributes.inc"

#define GET_ATTR_COMPAT_FUNC
#include "llvm/IR/Attributes.inc"

bool AttributeFuncs::areInlineCompatible(const Function &Caller,
                                         const Function &Callee) {
  return hasCompatibleFnAttrs(Caller, Callee);
}

bool AttributeFuncs::areOutlineCompatible(const Function &A,
                                          const Function &B) {
  return hasCompatibleFnAttrs(A, B);
}

void AttributeFuncs::mergeAttributesForInlining(Function &Caller,
                                                const Function &Callee) {
  mergeFnAttrs(Caller, Callee);
}

void AttributeFuncs::mergeAttributesForOutlining(Function &Base,
                                                const Function &ToMerge) {

  // We merge functions so that they meet the most general case.
  // For example, if the NoNansFPMathAttr is set in one function, but not in
  // the other, in the merged function we can say that the NoNansFPMathAttr
  // is not set.
  // However if we have the SpeculativeLoadHardeningAttr set true in one
  // function, but not the other, we make sure that the function retains
  // that aspect in the merged function.
  mergeFnAttrs(Base, ToMerge);
}

void AttributeFuncs::updateMinLegalVectorWidthAttr(Function &Fn,
                                                   uint64_t Width) {
  Attribute Attr = Fn.getFnAttribute("min-legal-vector-width");
  if (Attr.isValid()) {
    uint64_t OldWidth;
    Attr.getValueAsString().getAsInteger(0, OldWidth);
    if (Width > OldWidth)
      Fn.addFnAttr("min-legal-vector-width", llvm::utostr(Width));
  }
}
