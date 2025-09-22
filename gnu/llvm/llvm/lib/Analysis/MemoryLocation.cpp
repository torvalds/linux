//===- MemoryLocation.cpp - Memory location descriptions -------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include <optional>
using namespace llvm;

void LocationSize::print(raw_ostream &OS) const {
  OS << "LocationSize::";
  if (*this == beforeOrAfterPointer())
    OS << "beforeOrAfterPointer";
  else if (*this == afterPointer())
    OS << "afterPointer";
  else if (*this == mapEmpty())
    OS << "mapEmpty";
  else if (*this == mapTombstone())
    OS << "mapTombstone";
  else if (isPrecise())
    OS << "precise(" << getValue() << ')';
  else
    OS << "upperBound(" << getValue() << ')';
}

MemoryLocation MemoryLocation::get(const LoadInst *LI) {
  const auto &DL = LI->getDataLayout();

  return MemoryLocation(
      LI->getPointerOperand(),
      LocationSize::precise(DL.getTypeStoreSize(LI->getType())),
      LI->getAAMetadata());
}

MemoryLocation MemoryLocation::get(const StoreInst *SI) {
  const auto &DL = SI->getDataLayout();

  return MemoryLocation(SI->getPointerOperand(),
                        LocationSize::precise(DL.getTypeStoreSize(
                            SI->getValueOperand()->getType())),
                        SI->getAAMetadata());
}

MemoryLocation MemoryLocation::get(const VAArgInst *VI) {
  return MemoryLocation(VI->getPointerOperand(),
                        LocationSize::afterPointer(), VI->getAAMetadata());
}

MemoryLocation MemoryLocation::get(const AtomicCmpXchgInst *CXI) {
  const auto &DL = CXI->getDataLayout();

  return MemoryLocation(CXI->getPointerOperand(),
                        LocationSize::precise(DL.getTypeStoreSize(
                            CXI->getCompareOperand()->getType())),
                        CXI->getAAMetadata());
}

MemoryLocation MemoryLocation::get(const AtomicRMWInst *RMWI) {
  const auto &DL = RMWI->getDataLayout();

  return MemoryLocation(RMWI->getPointerOperand(),
                        LocationSize::precise(DL.getTypeStoreSize(
                            RMWI->getValOperand()->getType())),
                        RMWI->getAAMetadata());
}

std::optional<MemoryLocation>
MemoryLocation::getOrNone(const Instruction *Inst) {
  switch (Inst->getOpcode()) {
  case Instruction::Load:
    return get(cast<LoadInst>(Inst));
  case Instruction::Store:
    return get(cast<StoreInst>(Inst));
  case Instruction::VAArg:
    return get(cast<VAArgInst>(Inst));
  case Instruction::AtomicCmpXchg:
    return get(cast<AtomicCmpXchgInst>(Inst));
  case Instruction::AtomicRMW:
    return get(cast<AtomicRMWInst>(Inst));
  default:
    return std::nullopt;
  }
}

MemoryLocation MemoryLocation::getForSource(const MemTransferInst *MTI) {
  return getForSource(cast<AnyMemTransferInst>(MTI));
}

MemoryLocation MemoryLocation::getForSource(const AtomicMemTransferInst *MTI) {
  return getForSource(cast<AnyMemTransferInst>(MTI));
}

MemoryLocation MemoryLocation::getForSource(const AnyMemTransferInst *MTI) {
  assert(MTI->getRawSource() == MTI->getArgOperand(1));
  return getForArgument(MTI, 1, nullptr);
}

MemoryLocation MemoryLocation::getForDest(const MemIntrinsic *MI) {
  return getForDest(cast<AnyMemIntrinsic>(MI));
}

MemoryLocation MemoryLocation::getForDest(const AtomicMemIntrinsic *MI) {
  return getForDest(cast<AnyMemIntrinsic>(MI));
}

MemoryLocation MemoryLocation::getForDest(const AnyMemIntrinsic *MI) {
  assert(MI->getRawDest() == MI->getArgOperand(0));
  return getForArgument(MI, 0, nullptr);
}

std::optional<MemoryLocation>
MemoryLocation::getForDest(const CallBase *CB, const TargetLibraryInfo &TLI) {
  if (!CB->onlyAccessesArgMemory())
    return std::nullopt;

  if (CB->hasOperandBundles())
    // TODO: remove implementation restriction
    return std::nullopt;

  Value *UsedV = nullptr;
  std::optional<unsigned> UsedIdx;
  for (unsigned i = 0; i < CB->arg_size(); i++) {
    if (!CB->getArgOperand(i)->getType()->isPointerTy())
      continue;
    if (CB->onlyReadsMemory(i))
      continue;
    if (!UsedV) {
      // First potentially writing parameter
      UsedV = CB->getArgOperand(i);
      UsedIdx = i;
      continue;
    }
    UsedIdx = std::nullopt;
    if (UsedV != CB->getArgOperand(i))
      // Can't describe writing to two distinct locations.
      // TODO: This results in an inprecision when two values derived from the
      // same object are passed as arguments to the same function.
      return std::nullopt;
  }
  if (!UsedV)
    // We don't currently have a way to represent a "does not write" result
    // and thus have to be conservative and return unknown.
    return std::nullopt;

  if (UsedIdx)
    return getForArgument(CB, *UsedIdx, &TLI);
  return MemoryLocation::getBeforeOrAfter(UsedV, CB->getAAMetadata());
}

MemoryLocation MemoryLocation::getForArgument(const CallBase *Call,
                                              unsigned ArgIdx,
                                              const TargetLibraryInfo *TLI) {
  AAMDNodes AATags = Call->getAAMetadata();
  const Value *Arg = Call->getArgOperand(ArgIdx);

  // We may be able to produce an exact size for known intrinsics.
  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(Call)) {
    const DataLayout &DL = II->getDataLayout();

    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::memset:
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memmove:
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
      assert((ArgIdx == 0 || ArgIdx == 1) &&
             "Invalid argument index for memory intrinsic");
      if (ConstantInt *LenCI = dyn_cast<ConstantInt>(II->getArgOperand(2)))
        return MemoryLocation(Arg, LocationSize::precise(LenCI->getZExtValue()),
                              AATags);
      return MemoryLocation::getAfter(Arg, AATags);

    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::invariant_start:
      assert(ArgIdx == 1 && "Invalid argument index");
      return MemoryLocation(
          Arg,
          LocationSize::precise(
              cast<ConstantInt>(II->getArgOperand(0))->getZExtValue()),
          AATags);

    case Intrinsic::masked_load:
      assert(ArgIdx == 0 && "Invalid argument index");
      return MemoryLocation(
          Arg,
          LocationSize::upperBound(DL.getTypeStoreSize(II->getType())),
          AATags);

    case Intrinsic::masked_store:
      assert(ArgIdx == 1 && "Invalid argument index");
      return MemoryLocation(
          Arg,
          LocationSize::upperBound(
              DL.getTypeStoreSize(II->getArgOperand(0)->getType())),
          AATags);

    case Intrinsic::invariant_end:
      // The first argument to an invariant.end is a "descriptor" type (e.g. a
      // pointer to a empty struct) which is never actually dereferenced.
      if (ArgIdx == 0)
        return MemoryLocation(Arg, LocationSize::precise(0), AATags);
      assert(ArgIdx == 2 && "Invalid argument index");
      return MemoryLocation(
          Arg,
          LocationSize::precise(
              cast<ConstantInt>(II->getArgOperand(1))->getZExtValue()),
          AATags);

    case Intrinsic::arm_neon_vld1:
      assert(ArgIdx == 0 && "Invalid argument index");
      // LLVM's vld1 and vst1 intrinsics currently only support a single
      // vector register.
      return MemoryLocation(
          Arg, LocationSize::precise(DL.getTypeStoreSize(II->getType())),
          AATags);

    case Intrinsic::arm_neon_vst1:
      assert(ArgIdx == 0 && "Invalid argument index");
      return MemoryLocation(Arg,
                            LocationSize::precise(DL.getTypeStoreSize(
                                II->getArgOperand(1)->getType())),
                            AATags);
    }

    assert(
        !isa<AnyMemTransferInst>(II) &&
        "all memory transfer intrinsics should be handled by the switch above");
  }

  // We can bound the aliasing properties of memset_pattern16 just as we can
  // for memcpy/memset.  This is particularly important because the
  // LoopIdiomRecognizer likes to turn loops into calls to memset_pattern16
  // whenever possible.
  LibFunc F;
  if (TLI && TLI->getLibFunc(*Call, F) && TLI->has(F)) {
    switch (F) {
    case LibFunc_strcpy:
    case LibFunc_strcat:
    case LibFunc_strncat:
      assert((ArgIdx == 0 || ArgIdx == 1) && "Invalid argument index for str function");
      return MemoryLocation::getAfter(Arg, AATags);

    case LibFunc_memset_chk:
      assert(ArgIdx == 0 && "Invalid argument index for memset_chk");
      [[fallthrough]];
    case LibFunc_memcpy_chk: {
      assert((ArgIdx == 0 || ArgIdx == 1) &&
             "Invalid argument index for memcpy_chk");
      LocationSize Size = LocationSize::afterPointer();
      if (const auto *Len = dyn_cast<ConstantInt>(Call->getArgOperand(2))) {
        // memset_chk writes at most Len bytes, memcpy_chk reads/writes at most
        // Len bytes. They may read/write less, if Len exceeds the specified max
        // size and aborts.
        Size = LocationSize::upperBound(Len->getZExtValue());
      }
      return MemoryLocation(Arg, Size, AATags);
    }
    case LibFunc_strncpy: {
      assert((ArgIdx == 0 || ArgIdx == 1) &&
             "Invalid argument index for strncpy");
      LocationSize Size = LocationSize::afterPointer();
      if (const auto *Len = dyn_cast<ConstantInt>(Call->getArgOperand(2))) {
        // strncpy is guaranteed to write Len bytes, but only reads up to Len
        // bytes.
        Size = ArgIdx == 0 ? LocationSize::precise(Len->getZExtValue())
                           : LocationSize::upperBound(Len->getZExtValue());
      }
      return MemoryLocation(Arg, Size, AATags);
    }
    case LibFunc_memset_pattern16:
    case LibFunc_memset_pattern4:
    case LibFunc_memset_pattern8:
      assert((ArgIdx == 0 || ArgIdx == 1) &&
             "Invalid argument index for memset_pattern16");
      if (ArgIdx == 1) {
        unsigned Size = 16;
        if (F == LibFunc_memset_pattern4)
          Size = 4;
        else if (F == LibFunc_memset_pattern8)
          Size = 8;
        return MemoryLocation(Arg, LocationSize::precise(Size), AATags);
      }
      if (const ConstantInt *LenCI =
              dyn_cast<ConstantInt>(Call->getArgOperand(2)))
        return MemoryLocation(Arg, LocationSize::precise(LenCI->getZExtValue()),
                              AATags);
      return MemoryLocation::getAfter(Arg, AATags);
    case LibFunc_bcmp:
    case LibFunc_memcmp:
      assert((ArgIdx == 0 || ArgIdx == 1) &&
             "Invalid argument index for memcmp/bcmp");
      if (const ConstantInt *LenCI =
              dyn_cast<ConstantInt>(Call->getArgOperand(2)))
        return MemoryLocation(Arg, LocationSize::precise(LenCI->getZExtValue()),
                              AATags);
      return MemoryLocation::getAfter(Arg, AATags);
    case LibFunc_memchr:
      assert((ArgIdx == 0) && "Invalid argument index for memchr");
      if (const ConstantInt *LenCI =
              dyn_cast<ConstantInt>(Call->getArgOperand(2)))
        return MemoryLocation(Arg, LocationSize::precise(LenCI->getZExtValue()),
                              AATags);
      return MemoryLocation::getAfter(Arg, AATags);
    case LibFunc_memccpy:
      assert((ArgIdx == 0 || ArgIdx == 1) &&
             "Invalid argument index for memccpy");
      // We only know an upper bound on the number of bytes read/written.
      if (const ConstantInt *LenCI =
              dyn_cast<ConstantInt>(Call->getArgOperand(3)))
        return MemoryLocation(
            Arg, LocationSize::upperBound(LenCI->getZExtValue()), AATags);
      return MemoryLocation::getAfter(Arg, AATags);
    default:
      break;
    };
  }

  return MemoryLocation::getBeforeOrAfter(Call->getArgOperand(ArgIdx), AATags);
}
