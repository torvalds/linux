//===- ABIInfo.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfo.h"
#include "ABIInfoImpl.h"

using namespace clang;
using namespace clang::CodeGen;

// Pin the vtable to this file.
ABIInfo::~ABIInfo() = default;

CGCXXABI &ABIInfo::getCXXABI() const { return CGT.getCXXABI(); }

ASTContext &ABIInfo::getContext() const { return CGT.getContext(); }

llvm::LLVMContext &ABIInfo::getVMContext() const {
  return CGT.getLLVMContext();
}

const llvm::DataLayout &ABIInfo::getDataLayout() const {
  return CGT.getDataLayout();
}

const TargetInfo &ABIInfo::getTarget() const { return CGT.getTarget(); }

const CodeGenOptions &ABIInfo::getCodeGenOpts() const {
  return CGT.getCodeGenOpts();
}

bool ABIInfo::isAndroid() const { return getTarget().getTriple().isAndroid(); }

bool ABIInfo::isOHOSFamily() const {
  return getTarget().getTriple().isOHOSFamily();
}

RValue ABIInfo::EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr,
                            QualType Ty, AggValueSlot Slot) const {
  return RValue::getIgnored();
}

bool ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  return false;
}

bool ABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                uint64_t Members) const {
  return false;
}

bool ABIInfo::isZeroLengthBitfieldPermittedInHomogeneousAggregate() const {
  // For compatibility with GCC, ignore empty bitfields in C++ mode.
  return getContext().getLangOpts().CPlusPlus;
}

bool ABIInfo::isHomogeneousAggregate(QualType Ty, const Type *&Base,
                                     uint64_t &Members) const {
  if (const ConstantArrayType *AT = getContext().getAsConstantArrayType(Ty)) {
    uint64_t NElements = AT->getZExtSize();
    if (NElements == 0)
      return false;
    if (!isHomogeneousAggregate(AT->getElementType(), Base, Members))
      return false;
    Members *= NElements;
  } else if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    if (RD->hasFlexibleArrayMember())
      return false;

    Members = 0;

    // If this is a C++ record, check the properties of the record such as
    // bases and ABI specific restrictions
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      if (!getCXXABI().isPermittedToBeHomogeneousAggregate(CXXRD))
        return false;

      for (const auto &I : CXXRD->bases()) {
        // Ignore empty records.
        if (isEmptyRecord(getContext(), I.getType(), true))
          continue;

        uint64_t FldMembers;
        if (!isHomogeneousAggregate(I.getType(), Base, FldMembers))
          return false;

        Members += FldMembers;
      }
    }

    for (const auto *FD : RD->fields()) {
      // Ignore (non-zero arrays of) empty records.
      QualType FT = FD->getType();
      while (const ConstantArrayType *AT =
             getContext().getAsConstantArrayType(FT)) {
        if (AT->isZeroSize())
          return false;
        FT = AT->getElementType();
      }
      if (isEmptyRecord(getContext(), FT, true))
        continue;

      if (isZeroLengthBitfieldPermittedInHomogeneousAggregate() &&
          FD->isZeroLengthBitField(getContext()))
        continue;

      uint64_t FldMembers;
      if (!isHomogeneousAggregate(FD->getType(), Base, FldMembers))
        return false;

      Members = (RD->isUnion() ?
                 std::max(Members, FldMembers) : Members + FldMembers);
    }

    if (!Base)
      return false;

    // Ensure there is no padding.
    if (getContext().getTypeSize(Base) * Members !=
        getContext().getTypeSize(Ty))
      return false;
  } else {
    Members = 1;
    if (const ComplexType *CT = Ty->getAs<ComplexType>()) {
      Members = 2;
      Ty = CT->getElementType();
    }

    // Most ABIs only support float, double, and some vector type widths.
    if (!isHomogeneousAggregateBaseType(Ty))
      return false;

    // The base type must be the same for all members.  Types that
    // agree in both total size and mode (float vs. vector) are
    // treated as being equivalent here.
    const Type *TyPtr = Ty.getTypePtr();
    if (!Base) {
      Base = TyPtr;
      // If it's a non-power-of-2 vector, its size is already a power-of-2,
      // so make sure to widen it explicitly.
      if (const VectorType *VT = Base->getAs<VectorType>()) {
        QualType EltTy = VT->getElementType();
        unsigned NumElements =
            getContext().getTypeSize(VT) / getContext().getTypeSize(EltTy);
        Base = getContext()
                   .getVectorType(EltTy, NumElements, VT->getVectorKind())
                   .getTypePtr();
      }
    }

    if (Base->isVectorType() != TyPtr->isVectorType() ||
        getContext().getTypeSize(Base) != getContext().getTypeSize(TyPtr))
      return false;
  }
  return Members > 0 && isHomogeneousAggregateSmallEnough(Base, Members);
}

bool ABIInfo::isPromotableIntegerTypeForABI(QualType Ty) const {
  if (getContext().isPromotableIntegerType(Ty))
    return true;

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() < getContext().getTypeSize(getContext().IntTy))
      return true;

  return false;
}

ABIArgInfo ABIInfo::getNaturalAlignIndirect(QualType Ty, bool ByVal,
                                            bool Realign,
                                            llvm::Type *Padding) const {
  return ABIArgInfo::getIndirect(getContext().getTypeAlignInChars(Ty), ByVal,
                                 Realign, Padding);
}

ABIArgInfo ABIInfo::getNaturalAlignIndirectInReg(QualType Ty,
                                                 bool Realign) const {
  return ABIArgInfo::getIndirectInReg(getContext().getTypeAlignInChars(Ty),
                                      /*ByVal*/ false, Realign);
}

void ABIInfo::appendAttributeMangling(TargetAttr *Attr,
                                      raw_ostream &Out) const {
  if (Attr->isDefaultVersion())
    return;
  appendAttributeMangling(Attr->getFeaturesStr(), Out);
}

void ABIInfo::appendAttributeMangling(TargetVersionAttr *Attr,
                                      raw_ostream &Out) const {
  appendAttributeMangling(Attr->getNamesStr(), Out);
}

void ABIInfo::appendAttributeMangling(TargetClonesAttr *Attr, unsigned Index,
                                      raw_ostream &Out) const {
  appendAttributeMangling(Attr->getFeatureStr(Index), Out);
  Out << '.' << Attr->getMangledIndex(Index);
}

void ABIInfo::appendAttributeMangling(StringRef AttrStr,
                                      raw_ostream &Out) const {
  if (AttrStr == "default") {
    Out << ".default";
    return;
  }

  Out << '.';
  const TargetInfo &TI = CGT.getTarget();
  ParsedTargetAttr Info = TI.parseTargetAttr(AttrStr);

  llvm::sort(Info.Features, [&TI](StringRef LHS, StringRef RHS) {
    // Multiversioning doesn't allow "no-${feature}", so we can
    // only have "+" prefixes here.
    assert(LHS.starts_with("+") && RHS.starts_with("+") &&
           "Features should always have a prefix.");
    return TI.multiVersionSortPriority(LHS.substr(1)) >
           TI.multiVersionSortPriority(RHS.substr(1));
  });

  bool IsFirst = true;
  if (!Info.CPU.empty()) {
    IsFirst = false;
    Out << "arch_" << Info.CPU;
  }

  for (StringRef Feat : Info.Features) {
    if (!IsFirst)
      Out << '_';
    IsFirst = false;
    Out << Feat.substr(1);
  }
}

// Pin the vtable to this file.
SwiftABIInfo::~SwiftABIInfo() = default;

/// Does the given lowering require more than the given number of
/// registers when expanded?
///
/// This is intended to be the basis of a reasonable basic implementation
/// of should{Pass,Return}Indirectly.
///
/// For most targets, a limit of four total registers is reasonable; this
/// limits the amount of code required in order to move around the value
/// in case it wasn't produced immediately prior to the call by the caller
/// (or wasn't produced in exactly the right registers) or isn't used
/// immediately within the callee.  But some targets may need to further
/// limit the register count due to an inability to support that many
/// return registers.
bool SwiftABIInfo::occupiesMoreThan(ArrayRef<llvm::Type *> scalarTypes,
                                    unsigned maxAllRegisters) const {
  unsigned intCount = 0, fpCount = 0;
  for (llvm::Type *type : scalarTypes) {
    if (type->isPointerTy()) {
      intCount++;
    } else if (auto intTy = dyn_cast<llvm::IntegerType>(type)) {
      auto ptrWidth = CGT.getTarget().getPointerWidth(LangAS::Default);
      intCount += (intTy->getBitWidth() + ptrWidth - 1) / ptrWidth;
    } else {
      assert(type->isVectorTy() || type->isFloatingPointTy());
      fpCount++;
    }
  }

  return (intCount + fpCount > maxAllRegisters);
}

bool SwiftABIInfo::shouldPassIndirectly(ArrayRef<llvm::Type *> ComponentTys,
                                        bool AsReturnValue) const {
  return occupiesMoreThan(ComponentTys, /*total=*/4);
}

bool SwiftABIInfo::isLegalVectorType(CharUnits VectorSize, llvm::Type *EltTy,
                                     unsigned NumElts) const {
  // The default implementation of this assumes that the target guarantees
  // 128-bit SIMD support but nothing more.
  return (VectorSize.getQuantity() > 8 && VectorSize.getQuantity() <= 16);
}
