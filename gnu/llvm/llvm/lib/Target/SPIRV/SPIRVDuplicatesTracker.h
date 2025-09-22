//===-- SPIRVDuplicatesTracker.h - SPIR-V Duplicates Tracker ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// General infrastructure for keeping track of the values that according to
// the SPIR-V binary layout should be global to the whole module.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVDUPLICATESTRACKER_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVDUPLICATESTRACKER_H

#include "MCTargetDesc/SPIRVBaseInfo.h"
#include "MCTargetDesc/SPIRVMCTargetDesc.h"
#include "SPIRVUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"

#include <type_traits>

namespace llvm {
namespace SPIRV {
// NOTE: using MapVector instead of DenseMap because it helps getting
// everything ordered in a stable manner for a price of extra (NumKeys)*PtrSize
// memory and expensive removals which do not happen anyway.
class DTSortableEntry : public MapVector<const MachineFunction *, Register> {
  SmallVector<DTSortableEntry *, 2> Deps;

  struct FlagsTy {
    unsigned IsFunc : 1;
    unsigned IsGV : 1;
    // NOTE: bit-field default init is a C++20 feature.
    FlagsTy() : IsFunc(0), IsGV(0) {}
  };
  FlagsTy Flags;

public:
  // Common hoisting utility doesn't support function, because their hoisting
  // require hoisting of params as well.
  bool getIsFunc() const { return Flags.IsFunc; }
  bool getIsGV() const { return Flags.IsGV; }
  void setIsFunc(bool V) { Flags.IsFunc = V; }
  void setIsGV(bool V) { Flags.IsGV = V; }

  const SmallVector<DTSortableEntry *, 2> &getDeps() const { return Deps; }
  void addDep(DTSortableEntry *E) { Deps.push_back(E); }
};

enum SpecialTypeKind {
  STK_Empty = 0,
  STK_Image,
  STK_SampledImage,
  STK_Sampler,
  STK_Pipe,
  STK_DeviceEvent,
  STK_Pointer,
  STK_Last = -1
};

using SpecialTypeDescriptor = std::tuple<const Type *, unsigned, unsigned>;

union ImageAttrs {
  struct BitFlags {
    unsigned Dim : 3;
    unsigned Depth : 2;
    unsigned Arrayed : 1;
    unsigned MS : 1;
    unsigned Sampled : 2;
    unsigned ImageFormat : 6;
    unsigned AQ : 2;
  } Flags;
  unsigned Val;

  ImageAttrs(unsigned Dim, unsigned Depth, unsigned Arrayed, unsigned MS,
             unsigned Sampled, unsigned ImageFormat, unsigned AQ = 0) {
    Val = 0;
    Flags.Dim = Dim;
    Flags.Depth = Depth;
    Flags.Arrayed = Arrayed;
    Flags.MS = MS;
    Flags.Sampled = Sampled;
    Flags.ImageFormat = ImageFormat;
    Flags.AQ = AQ;
  }
};

inline SpecialTypeDescriptor
make_descr_image(const Type *SampledTy, unsigned Dim, unsigned Depth,
                 unsigned Arrayed, unsigned MS, unsigned Sampled,
                 unsigned ImageFormat, unsigned AQ = 0) {
  return std::make_tuple(
      SampledTy,
      ImageAttrs(Dim, Depth, Arrayed, MS, Sampled, ImageFormat, AQ).Val,
      SpecialTypeKind::STK_Image);
}

inline SpecialTypeDescriptor
make_descr_sampled_image(const Type *SampledTy, const MachineInstr *ImageTy) {
  assert(ImageTy->getOpcode() == SPIRV::OpTypeImage);
  return std::make_tuple(
      SampledTy,
      ImageAttrs(
          ImageTy->getOperand(2).getImm(), ImageTy->getOperand(3).getImm(),
          ImageTy->getOperand(4).getImm(), ImageTy->getOperand(5).getImm(),
          ImageTy->getOperand(6).getImm(), ImageTy->getOperand(7).getImm(),
          ImageTy->getOperand(8).getImm())
          .Val,
      SpecialTypeKind::STK_SampledImage);
}

inline SpecialTypeDescriptor make_descr_sampler() {
  return std::make_tuple(nullptr, 0U, SpecialTypeKind::STK_Sampler);
}

inline SpecialTypeDescriptor make_descr_pipe(uint8_t AQ) {
  return std::make_tuple(nullptr, AQ, SpecialTypeKind::STK_Pipe);
}

inline SpecialTypeDescriptor make_descr_event() {
  return std::make_tuple(nullptr, 0U, SpecialTypeKind::STK_DeviceEvent);
}

inline SpecialTypeDescriptor make_descr_pointee(const Type *ElementType,
                                                unsigned AddressSpace) {
  return std::make_tuple(ElementType, AddressSpace,
                         SpecialTypeKind::STK_Pointer);
}
} // namespace SPIRV

template <typename KeyTy> class SPIRVDuplicatesTrackerBase {
public:
  // NOTE: using MapVector instead of DenseMap helps getting everything ordered
  // in a stable manner for a price of extra (NumKeys)*PtrSize memory and
  // expensive removals which don't happen anyway.
  using StorageTy = MapVector<KeyTy, SPIRV::DTSortableEntry>;

private:
  StorageTy Storage;

public:
  void add(KeyTy V, const MachineFunction *MF, Register R) {
    if (find(V, MF).isValid())
      return;

    Storage[V][MF] = R;
    if (std::is_same<Function,
                     typename std::remove_const<
                         typename std::remove_pointer<KeyTy>::type>::type>() ||
        std::is_same<Argument,
                     typename std::remove_const<
                         typename std::remove_pointer<KeyTy>::type>::type>())
      Storage[V].setIsFunc(true);
    if (std::is_same<GlobalVariable,
                     typename std::remove_const<
                         typename std::remove_pointer<KeyTy>::type>::type>())
      Storage[V].setIsGV(true);
  }

  Register find(KeyTy V, const MachineFunction *MF) const {
    auto iter = Storage.find(V);
    if (iter != Storage.end()) {
      auto Map = iter->second;
      auto iter2 = Map.find(MF);
      if (iter2 != Map.end())
        return iter2->second;
    }
    return Register();
  }

  const StorageTy &getAllUses() const { return Storage; }

private:
  StorageTy &getAllUses() { return Storage; }

  // The friend class needs to have access to the internal storage
  // to be able to build dependency graph, can't declare only one
  // function a 'friend' due to the incomplete declaration at this point
  // and mutual dependency problems.
  friend class SPIRVGeneralDuplicatesTracker;
};

template <typename T>
class SPIRVDuplicatesTracker : public SPIRVDuplicatesTrackerBase<const T *> {};

template <>
class SPIRVDuplicatesTracker<SPIRV::SpecialTypeDescriptor>
    : public SPIRVDuplicatesTrackerBase<SPIRV::SpecialTypeDescriptor> {};

class SPIRVGeneralDuplicatesTracker {
  SPIRVDuplicatesTracker<Type> TT;
  SPIRVDuplicatesTracker<Constant> CT;
  SPIRVDuplicatesTracker<GlobalVariable> GT;
  SPIRVDuplicatesTracker<Function> FT;
  SPIRVDuplicatesTracker<Argument> AT;
  SPIRVDuplicatesTracker<MachineInstr> MT;
  SPIRVDuplicatesTracker<SPIRV::SpecialTypeDescriptor> ST;

  // NOTE: using MOs instead of regs to get rid of MF dependency to be able
  // to use flat data structure.
  // NOTE: replacing DenseMap with MapVector doesn't affect overall correctness
  // but makes LITs more stable, should prefer DenseMap still due to
  // significant perf difference.
  using SPIRVReg2EntryTy =
      MapVector<MachineOperand *, SPIRV::DTSortableEntry *>;

  template <typename T>
  void prebuildReg2Entry(SPIRVDuplicatesTracker<T> &DT,
                         SPIRVReg2EntryTy &Reg2Entry);

public:
  void buildDepsGraph(std::vector<SPIRV::DTSortableEntry *> &Graph,
                      MachineModuleInfo *MMI);

  void add(const Type *Ty, const MachineFunction *MF, Register R) {
    TT.add(unifyPtrType(Ty), MF, R);
  }

  void add(const Type *PointeeTy, unsigned AddressSpace,
           const MachineFunction *MF, Register R) {
    ST.add(SPIRV::make_descr_pointee(unifyPtrType(PointeeTy), AddressSpace), MF,
           R);
  }

  void add(const Constant *C, const MachineFunction *MF, Register R) {
    CT.add(C, MF, R);
  }

  void add(const GlobalVariable *GV, const MachineFunction *MF, Register R) {
    GT.add(GV, MF, R);
  }

  void add(const Function *F, const MachineFunction *MF, Register R) {
    FT.add(F, MF, R);
  }

  void add(const Argument *Arg, const MachineFunction *MF, Register R) {
    AT.add(Arg, MF, R);
  }

  void add(const MachineInstr *MI, const MachineFunction *MF, Register R) {
    MT.add(MI, MF, R);
  }

  void add(const SPIRV::SpecialTypeDescriptor &TD, const MachineFunction *MF,
           Register R) {
    ST.add(TD, MF, R);
  }

  Register find(const Type *Ty, const MachineFunction *MF) {
    return TT.find(unifyPtrType(Ty), MF);
  }

  Register find(const Type *PointeeTy, unsigned AddressSpace,
                const MachineFunction *MF) {
    return ST.find(
        SPIRV::make_descr_pointee(unifyPtrType(PointeeTy), AddressSpace), MF);
  }

  Register find(const Constant *C, const MachineFunction *MF) {
    return CT.find(const_cast<Constant *>(C), MF);
  }

  Register find(const GlobalVariable *GV, const MachineFunction *MF) {
    return GT.find(const_cast<GlobalVariable *>(GV), MF);
  }

  Register find(const Function *F, const MachineFunction *MF) {
    return FT.find(const_cast<Function *>(F), MF);
  }

  Register find(const Argument *Arg, const MachineFunction *MF) {
    return AT.find(const_cast<Argument *>(Arg), MF);
  }

  Register find(const MachineInstr *MI, const MachineFunction *MF) {
    return MT.find(const_cast<MachineInstr *>(MI), MF);
  }

  Register find(const SPIRV::SpecialTypeDescriptor &TD,
                const MachineFunction *MF) {
    return ST.find(TD, MF);
  }

  const SPIRVDuplicatesTracker<Type> *getTypes() { return &TT; }
};
} // namespace llvm
#endif // LLVM_LIB_TARGET_SPIRV_SPIRVDUPLICATESTRACKER_H
