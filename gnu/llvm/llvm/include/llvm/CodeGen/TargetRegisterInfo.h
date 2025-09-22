//==- CodeGen/TargetRegisterInfo.h - Target Register Information -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes an abstract interface used to get information about a
// target machines register file.  This information is used for a variety of
// purposed, especially register allocation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETREGISTERINFO_H
#define LLVM_CODEGEN_TARGETREGISTERINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Printable.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class BitVector;
class DIExpression;
class LiveRegMatrix;
class MachineFunction;
class MachineInstr;
class RegScavenger;
class VirtRegMap;
class LiveIntervals;
class LiveInterval;

class TargetRegisterClass {
public:
  using iterator = const MCPhysReg *;
  using const_iterator = const MCPhysReg *;
  using sc_iterator = const TargetRegisterClass* const *;

  // Instance variables filled by tablegen, do not use!
  const MCRegisterClass *MC;
  const uint32_t *SubClassMask;
  const uint16_t *SuperRegIndices;
  const LaneBitmask LaneMask;
  /// Classes with a higher priority value are assigned first by register
  /// allocators using a greedy heuristic. The value is in the range [0,31].
  const uint8_t AllocationPriority;

  // Change allocation priority heuristic used by greedy.
  const bool GlobalPriority;

  /// Configurable target specific flags.
  const uint8_t TSFlags;
  /// Whether the class supports two (or more) disjunct subregister indices.
  const bool HasDisjunctSubRegs;
  /// Whether a combination of subregisters can cover every register in the
  /// class. See also the CoveredBySubRegs description in Target.td.
  const bool CoveredBySubRegs;
  const sc_iterator SuperClasses;
  ArrayRef<MCPhysReg> (*OrderFunc)(const MachineFunction&);

  /// Return the register class ID number.
  unsigned getID() const { return MC->getID(); }

  /// begin/end - Return all of the registers in this class.
  ///
  iterator       begin() const { return MC->begin(); }
  iterator         end() const { return MC->end(); }

  /// Return the number of registers in this class.
  unsigned getNumRegs() const { return MC->getNumRegs(); }

  ArrayRef<MCPhysReg> getRegisters() const {
    return ArrayRef(begin(), getNumRegs());
  }

  /// Return the specified register in the class.
  MCRegister getRegister(unsigned i) const {
    return MC->getRegister(i);
  }

  /// Return true if the specified register is included in this register class.
  /// This does not include virtual registers.
  bool contains(Register Reg) const {
    /// FIXME: Historically this function has returned false when given vregs
    ///        but it should probably only receive physical registers
    if (!Reg.isPhysical())
      return false;
    return MC->contains(Reg.asMCReg());
  }

  /// Return true if both registers are in this class.
  bool contains(Register Reg1, Register Reg2) const {
    /// FIXME: Historically this function has returned false when given a vregs
    ///        but it should probably only receive physical registers
    if (!Reg1.isPhysical() || !Reg2.isPhysical())
      return false;
    return MC->contains(Reg1.asMCReg(), Reg2.asMCReg());
  }

  /// Return the cost of copying a value between two registers in this class.
  /// A negative number means the register class is very expensive
  /// to copy e.g. status flag register classes.
  int getCopyCost() const { return MC->getCopyCost(); }

  /// Return true if this register class may be used to create virtual
  /// registers.
  bool isAllocatable() const { return MC->isAllocatable(); }

  /// Return true if this register class has a defined BaseClassOrder.
  bool isBaseClass() const { return MC->isBaseClass(); }

  /// Return true if the specified TargetRegisterClass
  /// is a proper sub-class of this TargetRegisterClass.
  bool hasSubClass(const TargetRegisterClass *RC) const {
    return RC != this && hasSubClassEq(RC);
  }

  /// Returns true if RC is a sub-class of or equal to this class.
  bool hasSubClassEq(const TargetRegisterClass *RC) const {
    unsigned ID = RC->getID();
    return (SubClassMask[ID / 32] >> (ID % 32)) & 1;
  }

  /// Return true if the specified TargetRegisterClass is a
  /// proper super-class of this TargetRegisterClass.
  bool hasSuperClass(const TargetRegisterClass *RC) const {
    return RC->hasSubClass(this);
  }

  /// Returns true if RC is a super-class of or equal to this class.
  bool hasSuperClassEq(const TargetRegisterClass *RC) const {
    return RC->hasSubClassEq(this);
  }

  /// Returns a bit vector of subclasses, including this one.
  /// The vector is indexed by class IDs.
  ///
  /// To use it, consider the returned array as a chunk of memory that
  /// contains an array of bits of size NumRegClasses. Each 32-bit chunk
  /// contains a bitset of the ID of the subclasses in big-endian style.

  /// I.e., the representation of the memory from left to right at the
  /// bit level looks like:
  /// [31 30 ... 1 0] [ 63 62 ... 33 32] ...
  ///                     [ XXX NumRegClasses NumRegClasses - 1 ... ]
  /// Where the number represents the class ID and XXX bits that
  /// should be ignored.
  ///
  /// See the implementation of hasSubClassEq for an example of how it
  /// can be used.
  const uint32_t *getSubClassMask() const {
    return SubClassMask;
  }

  /// Returns a 0-terminated list of sub-register indices that project some
  /// super-register class into this register class. The list has an entry for
  /// each Idx such that:
  ///
  ///   There exists SuperRC where:
  ///     For all Reg in SuperRC:
  ///       this->contains(Reg:Idx)
  const uint16_t *getSuperRegIndices() const {
    return SuperRegIndices;
  }

  /// Returns a NULL-terminated list of super-classes.  The
  /// classes are ordered by ID which is also a topological ordering from large
  /// to small classes.  The list does NOT include the current class.
  sc_iterator getSuperClasses() const {
    return SuperClasses;
  }

  /// Return true if this TargetRegisterClass is a subset
  /// class of at least one other TargetRegisterClass.
  bool isASubClass() const {
    return SuperClasses[0] != nullptr;
  }

  /// Returns the preferred order for allocating registers from this register
  /// class in MF. The raw order comes directly from the .td file and may
  /// include reserved registers that are not allocatable.
  /// Register allocators should also make sure to allocate
  /// callee-saved registers only after all the volatiles are used. The
  /// RegisterClassInfo class provides filtered allocation orders with
  /// callee-saved registers moved to the end.
  ///
  /// The MachineFunction argument can be used to tune the allocatable
  /// registers based on the characteristics of the function, subtarget, or
  /// other criteria.
  ///
  /// By default, this method returns all registers in the class.
  ArrayRef<MCPhysReg> getRawAllocationOrder(const MachineFunction &MF) const {
    return OrderFunc ? OrderFunc(MF) : getRegisters();
  }

  /// Returns the combination of all lane masks of register in this class.
  /// The lane masks of the registers are the combination of all lane masks
  /// of their subregisters. Returns 1 if there are no subregisters.
  LaneBitmask getLaneMask() const {
    return LaneMask;
  }
};

/// Extra information, not in MCRegisterDesc, about registers.
/// These are used by codegen, not by MC.
struct TargetRegisterInfoDesc {
  const uint8_t *CostPerUse; // Extra cost of instructions using register.
  unsigned NumCosts; // Number of cost values associated with each register.
  const bool
      *InAllocatableClass; // Register belongs to an allocatable regclass.
};

/// Each TargetRegisterClass has a per register weight, and weight
/// limit which must be less than the limits of its pressure sets.
struct RegClassWeight {
  unsigned RegWeight;
  unsigned WeightLimit;
};

/// TargetRegisterInfo base class - We assume that the target defines a static
/// array of TargetRegisterDesc objects that represent all of the machine
/// registers that the target has.  As such, we simply have to track a pointer
/// to this array so that we can turn register number into a register
/// descriptor.
///
class TargetRegisterInfo : public MCRegisterInfo {
public:
  using regclass_iterator = const TargetRegisterClass * const *;
  using vt_iterator = const MVT::SimpleValueType *;
  struct RegClassInfo {
    unsigned RegSize, SpillSize, SpillAlignment;
    unsigned VTListOffset;
  };

  /// SubRegCoveredBits - Emitted by tablegen: bit range covered by a subreg
  /// index, -1 in any being invalid.
  struct SubRegCoveredBits {
    uint16_t Offset;
    uint16_t Size;
  };

private:
  const TargetRegisterInfoDesc *InfoDesc;     // Extra desc array for codegen
  const char *const *SubRegIndexNames;        // Names of subreg indexes.
  const SubRegCoveredBits *SubRegIdxRanges;   // Pointer to the subreg covered
                                              // bit ranges array.

  // Pointer to array of lane masks, one per sub-reg index.
  const LaneBitmask *SubRegIndexLaneMasks;

  regclass_iterator RegClassBegin, RegClassEnd;   // List of regclasses
  LaneBitmask CoveringLanes;
  const RegClassInfo *const RCInfos;
  const MVT::SimpleValueType *const RCVTLists;
  unsigned HwMode;

protected:
  TargetRegisterInfo(const TargetRegisterInfoDesc *ID, regclass_iterator RCB,
                     regclass_iterator RCE, const char *const *SRINames,
                     const SubRegCoveredBits *SubIdxRanges,
                     const LaneBitmask *SRILaneMasks, LaneBitmask CoveringLanes,
                     const RegClassInfo *const RCIs,
                     const MVT::SimpleValueType *const RCVTLists,
                     unsigned Mode = 0);
  virtual ~TargetRegisterInfo();

public:
  /// Return the number of registers for the function. (may overestimate)
  virtual unsigned getNumSupportedRegs(const MachineFunction &) const {
    return getNumRegs();
  }

  // Register numbers can represent physical registers, virtual registers, and
  // sometimes stack slots. The unsigned values are divided into these ranges:
  //
  //   0           Not a register, can be used as a sentinel.
  //   [1;2^30)    Physical registers assigned by TableGen.
  //   [2^30;2^31) Stack slots. (Rarely used.)
  //   [2^31;2^32) Virtual registers assigned by MachineRegisterInfo.
  //
  // Further sentinels can be allocated from the small negative integers.
  // DenseMapInfo<unsigned> uses -1u and -2u.

  /// Return the size in bits of a register from class RC.
  TypeSize getRegSizeInBits(const TargetRegisterClass &RC) const {
    return TypeSize::getFixed(getRegClassInfo(RC).RegSize);
  }

  /// Return the size in bytes of the stack slot allocated to hold a spilled
  /// copy of a register from class RC.
  unsigned getSpillSize(const TargetRegisterClass &RC) const {
    return getRegClassInfo(RC).SpillSize / 8;
  }

  /// Return the minimum required alignment in bytes for a spill slot for
  /// a register of this class.
  Align getSpillAlign(const TargetRegisterClass &RC) const {
    return Align(getRegClassInfo(RC).SpillAlignment / 8);
  }

  /// Return true if the given TargetRegisterClass has the ValueType T.
  bool isTypeLegalForClass(const TargetRegisterClass &RC, MVT T) const {
    for (auto I = legalclasstypes_begin(RC); *I != MVT::Other; ++I)
      if (MVT(*I) == T)
        return true;
    return false;
  }

  /// Return true if the given TargetRegisterClass is compatible with LLT T.
  bool isTypeLegalForClass(const TargetRegisterClass &RC, LLT T) const {
    for (auto I = legalclasstypes_begin(RC); *I != MVT::Other; ++I) {
      MVT VT(*I);
      if (VT == MVT::Untyped)
        return true;

      if (LLT(VT) == T)
        return true;
    }
    return false;
  }

  /// Loop over all of the value types that can be represented by values
  /// in the given register class.
  vt_iterator legalclasstypes_begin(const TargetRegisterClass &RC) const {
    return &RCVTLists[getRegClassInfo(RC).VTListOffset];
  }

  vt_iterator legalclasstypes_end(const TargetRegisterClass &RC) const {
    vt_iterator I = legalclasstypes_begin(RC);
    while (*I != MVT::Other)
      ++I;
    return I;
  }

  /// Returns the Register Class of a physical register of the given type,
  /// picking the most sub register class of the right type that contains this
  /// physreg.
  const TargetRegisterClass *getMinimalPhysRegClass(MCRegister Reg,
                                                    MVT VT = MVT::Other) const;

  /// Returns the Register Class of a physical register of the given type,
  /// picking the most sub register class of the right type that contains this
  /// physreg. If there is no register class compatible with the given type,
  /// returns nullptr.
  const TargetRegisterClass *getMinimalPhysRegClassLLT(MCRegister Reg,
                                                       LLT Ty = LLT()) const;

  /// Return the maximal subclass of the given register class that is
  /// allocatable or NULL.
  const TargetRegisterClass *
    getAllocatableClass(const TargetRegisterClass *RC) const;

  /// Returns a bitset indexed by register number indicating if a register is
  /// allocatable or not. If a register class is specified, returns the subset
  /// for the class.
  BitVector getAllocatableSet(const MachineFunction &MF,
                              const TargetRegisterClass *RC = nullptr) const;

  /// Get a list of cost values for all registers that correspond to the index
  /// returned by RegisterCostTableIndex.
  ArrayRef<uint8_t> getRegisterCosts(const MachineFunction &MF) const {
    unsigned Idx = getRegisterCostTableIndex(MF);
    unsigned NumRegs = getNumRegs();
    assert(Idx < InfoDesc->NumCosts && "CostPerUse index out of bounds");

    return ArrayRef(&InfoDesc->CostPerUse[Idx * NumRegs], NumRegs);
  }

  /// Return true if the register is in the allocation of any register class.
  bool isInAllocatableClass(MCRegister RegNo) const {
    return InfoDesc->InAllocatableClass[RegNo];
  }

  /// Return the human-readable symbolic target-specific
  /// name for the specified SubRegIndex.
  const char *getSubRegIndexName(unsigned SubIdx) const {
    assert(SubIdx && SubIdx < getNumSubRegIndices() &&
           "This is not a subregister index");
    return SubRegIndexNames[SubIdx-1];
  }

  /// Get the size of the bit range covered by a sub-register index.
  /// If the index isn't continuous, return the sum of the sizes of its parts.
  /// If the index is used to access subregisters of different sizes, return -1.
  unsigned getSubRegIdxSize(unsigned Idx) const;

  /// Get the offset of the bit range covered by a sub-register index.
  /// If an Offset doesn't make sense (the index isn't continuous, or is used to
  /// access sub-registers at different offsets), return -1.
  unsigned getSubRegIdxOffset(unsigned Idx) const;

  /// Return a bitmask representing the parts of a register that are covered by
  /// SubIdx \see LaneBitmask.
  ///
  /// SubIdx == 0 is allowed, it has the lane mask ~0u.
  LaneBitmask getSubRegIndexLaneMask(unsigned SubIdx) const {
    assert(SubIdx < getNumSubRegIndices() && "This is not a subregister index");
    return SubRegIndexLaneMasks[SubIdx];
  }

  /// Try to find one or more subregister indexes to cover \p LaneMask.
  ///
  /// If this is possible, returns true and appends the best matching set of
  /// indexes to \p Indexes. If this is not possible, returns false.
  bool getCoveringSubRegIndexes(const MachineRegisterInfo &MRI,
                                const TargetRegisterClass *RC,
                                LaneBitmask LaneMask,
                                SmallVectorImpl<unsigned> &Indexes) const;

  /// The lane masks returned by getSubRegIndexLaneMask() above can only be
  /// used to determine if sub-registers overlap - they can't be used to
  /// determine if a set of sub-registers completely cover another
  /// sub-register.
  ///
  /// The X86 general purpose registers have two lanes corresponding to the
  /// sub_8bit and sub_8bit_hi sub-registers. Both sub_32bit and sub_16bit have
  /// lane masks '3', but the sub_16bit sub-register doesn't fully cover the
  /// sub_32bit sub-register.
  ///
  /// On the other hand, the ARM NEON lanes fully cover their registers: The
  /// dsub_0 sub-register is completely covered by the ssub_0 and ssub_1 lanes.
  /// This is related to the CoveredBySubRegs property on register definitions.
  ///
  /// This function returns a bit mask of lanes that completely cover their
  /// sub-registers. More precisely, given:
  ///
  ///   Covering = getCoveringLanes();
  ///   MaskA = getSubRegIndexLaneMask(SubA);
  ///   MaskB = getSubRegIndexLaneMask(SubB);
  ///
  /// If (MaskA & ~(MaskB & Covering)) == 0, then SubA is completely covered by
  /// SubB.
  LaneBitmask getCoveringLanes() const { return CoveringLanes; }

  /// Returns true if the two registers are equal or alias each other.
  /// The registers may be virtual registers.
  bool regsOverlap(Register RegA, Register RegB) const {
    if (RegA == RegB)
      return true;
    if (RegA.isPhysical() && RegB.isPhysical())
      return MCRegisterInfo::regsOverlap(RegA.asMCReg(), RegB.asMCReg());
    return false;
  }

  /// Returns true if Reg contains RegUnit.
  bool hasRegUnit(MCRegister Reg, Register RegUnit) const {
    for (MCRegUnit Unit : regunits(Reg))
      if (Register(Unit) == RegUnit)
        return true;
    return false;
  }

  /// Returns the original SrcReg unless it is the target of a copy-like
  /// operation, in which case we chain backwards through all such operations
  /// to the ultimate source register.  If a physical register is encountered,
  /// we stop the search.
  virtual Register lookThruCopyLike(Register SrcReg,
                                    const MachineRegisterInfo *MRI) const;

  /// Find the original SrcReg unless it is the target of a copy-like operation,
  /// in which case we chain backwards through all such operations to the
  /// ultimate source register. If a physical register is encountered, we stop
  /// the search.
  /// Return the original SrcReg if all the definitions in the chain only have
  /// one user and not a physical register.
  virtual Register
  lookThruSingleUseCopyChain(Register SrcReg,
                             const MachineRegisterInfo *MRI) const;

  /// Return a null-terminated list of all of the callee-saved registers on
  /// this target. The register should be in the order of desired callee-save
  /// stack frame offset. The first register is closest to the incoming stack
  /// pointer if stack grows down, and vice versa.
  /// Notice: This function does not take into account disabled CSRs.
  ///         In most cases you will want to use instead the function
  ///         getCalleeSavedRegs that is implemented in MachineRegisterInfo.
  virtual const MCPhysReg*
  getCalleeSavedRegs(const MachineFunction *MF) const = 0;

  /// Return a mask of call-preserved registers for the given calling convention
  /// on the current function. The mask should include all call-preserved
  /// aliases. This is used by the register allocator to determine which
  /// registers can be live across a call.
  ///
  /// The mask is an array containing (TRI::getNumRegs()+31)/32 entries.
  /// A set bit indicates that all bits of the corresponding register are
  /// preserved across the function call.  The bit mask is expected to be
  /// sub-register complete, i.e. if A is preserved, so are all its
  /// sub-registers.
  ///
  /// Bits are numbered from the LSB, so the bit for physical register Reg can
  /// be found as (Mask[Reg / 32] >> Reg % 32) & 1.
  ///
  /// A NULL pointer means that no register mask will be used, and call
  /// instructions should use implicit-def operands to indicate call clobbered
  /// registers.
  ///
  virtual const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                               CallingConv::ID) const {
    // The default mask clobbers everything.  All targets should override.
    return nullptr;
  }

  /// Return a register mask for the registers preserved by the unwinder,
  /// or nullptr if no custom mask is needed.
  virtual const uint32_t *
  getCustomEHPadPreservedMask(const MachineFunction &MF) const {
    return nullptr;
  }

  /// Return a register mask that clobbers everything.
  virtual const uint32_t *getNoPreservedMask() const {
    llvm_unreachable("target does not provide no preserved mask");
  }

  /// Return a list of all of the registers which are clobbered "inside" a call
  /// to the given function. For example, these might be needed for PLT
  /// sequences of long-branch veneers.
  virtual ArrayRef<MCPhysReg>
  getIntraCallClobberedRegs(const MachineFunction *MF) const {
    return {};
  }

  /// Return true if all bits that are set in mask \p mask0 are also set in
  /// \p mask1.
  bool regmaskSubsetEqual(const uint32_t *mask0, const uint32_t *mask1) const;

  /// Return all the call-preserved register masks defined for this target.
  virtual ArrayRef<const uint32_t *> getRegMasks() const = 0;
  virtual ArrayRef<const char *> getRegMaskNames() const = 0;

  /// Returns a bitset indexed by physical register number indicating if a
  /// register is a special register that has particular uses and should be
  /// considered unavailable at all times, e.g. stack pointer, return address.
  /// A reserved register:
  /// - is not allocatable
  /// - is considered always live
  /// - is ignored by liveness tracking
  /// It is often necessary to reserve the super registers of a reserved
  /// register as well, to avoid them getting allocated indirectly. You may use
  /// markSuperRegs() and checkAllSuperRegsMarked() in this case.
  virtual BitVector getReservedRegs(const MachineFunction &MF) const = 0;

  /// Returns either a string explaining why the given register is reserved for
  /// this function, or an empty optional if no explanation has been written.
  /// The absence of an explanation does not mean that the register is not
  /// reserved (meaning, you should check that PhysReg is in fact reserved
  /// before calling this).
  virtual std::optional<std::string>
  explainReservedReg(const MachineFunction &MF, MCRegister PhysReg) const {
    return {};
  }

  /// Returns false if we can't guarantee that Physreg, specified as an IR asm
  /// clobber constraint, will be preserved across the statement.
  virtual bool isAsmClobberable(const MachineFunction &MF,
                                MCRegister PhysReg) const {
    return true;
  }

  /// Returns true if PhysReg cannot be written to in inline asm statements.
  virtual bool isInlineAsmReadOnlyReg(const MachineFunction &MF,
                                      unsigned PhysReg) const {
    return false;
  }

  /// Returns true if PhysReg is unallocatable and constant throughout the
  /// function.  Used by MachineRegisterInfo::isConstantPhysReg().
  virtual bool isConstantPhysReg(MCRegister PhysReg) const { return false; }

  /// Returns true if the register class is considered divergent.
  virtual bool isDivergentRegClass(const TargetRegisterClass *RC) const {
    return false;
  }

  /// Returns true if the register is considered uniform.
  virtual bool isUniformReg(const MachineRegisterInfo &MRI,
                            const RegisterBankInfo &RBI, Register Reg) const {
    return false;
  }

  /// Returns true if MachineLoopInfo should analyze the given physreg
  /// for loop invariance.
  virtual bool shouldAnalyzePhysregInMachineLoopInfo(MCRegister R) const {
    return false;
  }

  /// Physical registers that may be modified within a function but are
  /// guaranteed to be restored before any uses. This is useful for targets that
  /// have call sequences where a GOT register may be updated by the caller
  /// prior to a call and is guaranteed to be restored (also by the caller)
  /// after the call.
  virtual bool isCallerPreservedPhysReg(MCRegister PhysReg,
                                        const MachineFunction &MF) const {
    return false;
  }

  /// This is a wrapper around getCallPreservedMask().
  /// Return true if the register is preserved after the call.
  virtual bool isCalleeSavedPhysReg(MCRegister PhysReg,
                                    const MachineFunction &MF) const;

  /// Returns true if PhysReg can be used as an argument to a function.
  virtual bool isArgumentRegister(const MachineFunction &MF,
                                  MCRegister PhysReg) const {
    return false;
  }

  /// Returns true if PhysReg is a fixed register.
  virtual bool isFixedRegister(const MachineFunction &MF,
                               MCRegister PhysReg) const {
    return false;
  }

  /// Returns true if PhysReg is a general purpose register.
  virtual bool isGeneralPurposeRegister(const MachineFunction &MF,
                                        MCRegister PhysReg) const {
    return false;
  }

  /// Prior to adding the live-out mask to a stackmap or patchpoint
  /// instruction, provide the target the opportunity to adjust it (mainly to
  /// remove pseudo-registers that should be ignored).
  virtual void adjustStackMapLiveOutMask(uint32_t *Mask) const {}

  /// Return a super-register of the specified register
  /// Reg so its sub-register of index SubIdx is Reg.
  MCRegister getMatchingSuperReg(MCRegister Reg, unsigned SubIdx,
                                 const TargetRegisterClass *RC) const {
    return MCRegisterInfo::getMatchingSuperReg(Reg, SubIdx, RC->MC);
  }

  /// Return a subclass of the specified register
  /// class A so that each register in it has a sub-register of the
  /// specified sub-register index which is in the specified register class B.
  ///
  /// TableGen will synthesize missing A sub-classes.
  virtual const TargetRegisterClass *
  getMatchingSuperRegClass(const TargetRegisterClass *A,
                           const TargetRegisterClass *B, unsigned Idx) const;

  // For a copy-like instruction that defines a register of class DefRC with
  // subreg index DefSubReg, reading from another source with class SrcRC and
  // subregister SrcSubReg return true if this is a preferable copy
  // instruction or an earlier use should be used.
  virtual bool shouldRewriteCopySrc(const TargetRegisterClass *DefRC,
                                    unsigned DefSubReg,
                                    const TargetRegisterClass *SrcRC,
                                    unsigned SrcSubReg) const;

  /// Returns the largest legal sub-class of RC that
  /// supports the sub-register index Idx.
  /// If no such sub-class exists, return NULL.
  /// If all registers in RC already have an Idx sub-register, return RC.
  ///
  /// TableGen generates a version of this function that is good enough in most
  /// cases.  Targets can override if they have constraints that TableGen
  /// doesn't understand.  For example, the x86 sub_8bit sub-register index is
  /// supported by the full GR32 register class in 64-bit mode, but only by the
  /// GR32_ABCD regiister class in 32-bit mode.
  ///
  /// TableGen will synthesize missing RC sub-classes.
  virtual const TargetRegisterClass *
  getSubClassWithSubReg(const TargetRegisterClass *RC, unsigned Idx) const {
    assert(Idx == 0 && "Target has no sub-registers");
    return RC;
  }

  /// Return a register class that can be used for a subregister copy from/into
  /// \p SuperRC at \p SubRegIdx.
  virtual const TargetRegisterClass *
  getSubRegisterClass(const TargetRegisterClass *SuperRC,
                      unsigned SubRegIdx) const {
    return nullptr;
  }

  /// Return the subregister index you get from composing
  /// two subregister indices.
  ///
  /// The special null sub-register index composes as the identity.
  ///
  /// If R:a:b is the same register as R:c, then composeSubRegIndices(a, b)
  /// returns c. Note that composeSubRegIndices does not tell you about illegal
  /// compositions. If R does not have a subreg a, or R:a does not have a subreg
  /// b, composeSubRegIndices doesn't tell you.
  ///
  /// The ARM register Q0 has two D subregs dsub_0:D0 and dsub_1:D1. It also has
  /// ssub_0:S0 - ssub_3:S3 subregs.
  /// If you compose subreg indices dsub_1, ssub_0 you get ssub_2.
  unsigned composeSubRegIndices(unsigned a, unsigned b) const {
    if (!a) return b;
    if (!b) return a;
    return composeSubRegIndicesImpl(a, b);
  }

  /// Transforms a LaneMask computed for one subregister to the lanemask that
  /// would have been computed when composing the subsubregisters with IdxA
  /// first. @sa composeSubRegIndices()
  LaneBitmask composeSubRegIndexLaneMask(unsigned IdxA,
                                         LaneBitmask Mask) const {
    if (!IdxA)
      return Mask;
    return composeSubRegIndexLaneMaskImpl(IdxA, Mask);
  }

  /// Transform a lanemask given for a virtual register to the corresponding
  /// lanemask before using subregister with index \p IdxA.
  /// This is the reverse of composeSubRegIndexLaneMask(), assuming Mask is a
  /// valie lane mask (no invalid bits set) the following holds:
  /// X0 = composeSubRegIndexLaneMask(Idx, Mask)
  /// X1 = reverseComposeSubRegIndexLaneMask(Idx, X0)
  /// => X1 == Mask
  LaneBitmask reverseComposeSubRegIndexLaneMask(unsigned IdxA,
                                                LaneBitmask LaneMask) const {
    if (!IdxA)
      return LaneMask;
    return reverseComposeSubRegIndexLaneMaskImpl(IdxA, LaneMask);
  }

  /// Debugging helper: dump register in human readable form to dbgs() stream.
  static void dumpReg(Register Reg, unsigned SubRegIndex = 0,
                      const TargetRegisterInfo *TRI = nullptr);

  /// Return target defined base register class for a physical register.
  /// This is the register class with the lowest BaseClassOrder containing the
  /// register.
  /// Will be nullptr if the register is not in any base register class.
  virtual const TargetRegisterClass *getPhysRegBaseClass(MCRegister Reg) const {
    return nullptr;
  }

protected:
  /// Overridden by TableGen in targets that have sub-registers.
  virtual unsigned composeSubRegIndicesImpl(unsigned, unsigned) const {
    llvm_unreachable("Target has no sub-registers");
  }

  /// Overridden by TableGen in targets that have sub-registers.
  virtual LaneBitmask
  composeSubRegIndexLaneMaskImpl(unsigned, LaneBitmask) const {
    llvm_unreachable("Target has no sub-registers");
  }

  virtual LaneBitmask reverseComposeSubRegIndexLaneMaskImpl(unsigned,
                                                            LaneBitmask) const {
    llvm_unreachable("Target has no sub-registers");
  }

  /// Return the register cost table index. This implementation is sufficient
  /// for most architectures and can be overriden by targets in case there are
  /// multiple cost values associated with each register.
  virtual unsigned getRegisterCostTableIndex(const MachineFunction &MF) const {
    return 0;
  }

public:
  /// Find a common super-register class if it exists.
  ///
  /// Find a register class, SuperRC and two sub-register indices, PreA and
  /// PreB, such that:
  ///
  ///   1. PreA + SubA == PreB + SubB  (using composeSubRegIndices()), and
  ///
  ///   2. For all Reg in SuperRC: Reg:PreA in RCA and Reg:PreB in RCB, and
  ///
  ///   3. SuperRC->getSize() >= max(RCA->getSize(), RCB->getSize()).
  ///
  /// SuperRC will be chosen such that no super-class of SuperRC satisfies the
  /// requirements, and there is no register class with a smaller spill size
  /// that satisfies the requirements.
  ///
  /// SubA and SubB must not be 0. Use getMatchingSuperRegClass() instead.
  ///
  /// Either of the PreA and PreB sub-register indices may be returned as 0. In
  /// that case, the returned register class will be a sub-class of the
  /// corresponding argument register class.
  ///
  /// The function returns NULL if no register class can be found.
  const TargetRegisterClass*
  getCommonSuperRegClass(const TargetRegisterClass *RCA, unsigned SubA,
                         const TargetRegisterClass *RCB, unsigned SubB,
                         unsigned &PreA, unsigned &PreB) const;

  //===--------------------------------------------------------------------===//
  // Register Class Information
  //
protected:
  const RegClassInfo &getRegClassInfo(const TargetRegisterClass &RC) const {
    return RCInfos[getNumRegClasses() * HwMode + RC.getID()];
  }

public:
  /// Register class iterators
  regclass_iterator regclass_begin() const { return RegClassBegin; }
  regclass_iterator regclass_end() const { return RegClassEnd; }
  iterator_range<regclass_iterator> regclasses() const {
    return make_range(regclass_begin(), regclass_end());
  }

  unsigned getNumRegClasses() const {
    return (unsigned)(regclass_end()-regclass_begin());
  }

  /// Returns the register class associated with the enumeration value.
  /// See class MCOperandInfo.
  const TargetRegisterClass *getRegClass(unsigned i) const {
    assert(i < getNumRegClasses() && "Register Class ID out of range");
    return RegClassBegin[i];
  }

  /// Returns the name of the register class.
  const char *getRegClassName(const TargetRegisterClass *Class) const {
    return MCRegisterInfo::getRegClassName(Class->MC);
  }

  /// Find the largest common subclass of A and B.
  /// Return NULL if there is no common subclass.
  const TargetRegisterClass *
  getCommonSubClass(const TargetRegisterClass *A,
                    const TargetRegisterClass *B) const;

  /// Returns a TargetRegisterClass used for pointer values.
  /// If a target supports multiple different pointer register classes,
  /// kind specifies which one is indicated.
  virtual const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF, unsigned Kind=0) const {
    llvm_unreachable("Target didn't implement getPointerRegClass!");
  }

  /// Returns a legal register class to copy a register in the specified class
  /// to or from. If it is possible to copy the register directly without using
  /// a cross register class copy, return the specified RC. Returns NULL if it
  /// is not possible to copy between two registers of the specified class.
  virtual const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const {
    return RC;
  }

  /// Returns the largest super class of RC that is legal to use in the current
  /// sub-target and has the same spill size.
  /// The returned register class can be used to create virtual registers which
  /// means that all its registers can be copied and spilled.
  virtual const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &) const {
    /// The default implementation is very conservative and doesn't allow the
    /// register allocator to inflate register classes.
    return RC;
  }

  /// Return the register pressure "high water mark" for the specific register
  /// class. The scheduler is in high register pressure mode (for the specific
  /// register class) if it goes over the limit.
  ///
  /// Note: this is the old register pressure model that relies on a manually
  /// specified representative register class per value type.
  virtual unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                                       MachineFunction &MF) const {
    return 0;
  }

  /// Return a heuristic for the machine scheduler to compare the profitability
  /// of increasing one register pressure set versus another.  The scheduler
  /// will prefer increasing the register pressure of the set which returns
  /// the largest value for this function.
  virtual unsigned getRegPressureSetScore(const MachineFunction &MF,
                                          unsigned PSetID) const {
    return PSetID;
  }

  /// Get the weight in units of pressure for this register class.
  virtual const RegClassWeight &getRegClassWeight(
    const TargetRegisterClass *RC) const = 0;

  /// Returns size in bits of a phys/virtual/generic register.
  TypeSize getRegSizeInBits(Register Reg, const MachineRegisterInfo &MRI) const;

  /// Get the weight in units of pressure for this register unit.
  virtual unsigned getRegUnitWeight(unsigned RegUnit) const = 0;

  /// Get the number of dimensions of register pressure.
  virtual unsigned getNumRegPressureSets() const = 0;

  /// Get the name of this register unit pressure set.
  virtual const char *getRegPressureSetName(unsigned Idx) const = 0;

  /// Get the register unit pressure limit for this dimension.
  /// This limit must be adjusted dynamically for reserved registers.
  virtual unsigned getRegPressureSetLimit(const MachineFunction &MF,
                                          unsigned Idx) const = 0;

  /// Get the dimensions of register pressure impacted by this register class.
  /// Returns a -1 terminated array of pressure set IDs.
  virtual const int *getRegClassPressureSets(
    const TargetRegisterClass *RC) const = 0;

  /// Get the dimensions of register pressure impacted by this register unit.
  /// Returns a -1 terminated array of pressure set IDs.
  virtual const int *getRegUnitPressureSets(unsigned RegUnit) const = 0;

  /// Get a list of 'hint' registers that the register allocator should try
  /// first when allocating a physical register for the virtual register
  /// VirtReg. These registers are effectively moved to the front of the
  /// allocation order. If true is returned, regalloc will try to only use
  /// hints to the greatest extent possible even if it means spilling.
  ///
  /// The Order argument is the allocation order for VirtReg's register class
  /// as returned from RegisterClassInfo::getOrder(). The hint registers must
  /// come from Order, and they must not be reserved.
  ///
  /// The default implementation of this function will only add target
  /// independent register allocation hints. Targets that override this
  /// function should typically call this default implementation as well and
  /// expect to see generic copy hints added.
  virtual bool
  getRegAllocationHints(Register VirtReg, ArrayRef<MCPhysReg> Order,
                        SmallVectorImpl<MCPhysReg> &Hints,
                        const MachineFunction &MF,
                        const VirtRegMap *VRM = nullptr,
                        const LiveRegMatrix *Matrix = nullptr) const;

  /// A callback to allow target a chance to update register allocation hints
  /// when a register is "changed" (e.g. coalesced) to another register.
  /// e.g. On ARM, some virtual registers should target register pairs,
  /// if one of pair is coalesced to another register, the allocation hint of
  /// the other half of the pair should be changed to point to the new register.
  virtual void updateRegAllocHint(Register Reg, Register NewReg,
                                  MachineFunction &MF) const {
    // Do nothing.
  }

  /// Allow the target to reverse allocation order of local live ranges. This
  /// will generally allocate shorter local live ranges first. For targets with
  /// many registers, this could reduce regalloc compile time by a large
  /// factor. It is disabled by default for three reasons:
  /// (1) Top-down allocation is simpler and easier to debug for targets that
  /// don't benefit from reversing the order.
  /// (2) Bottom-up allocation could result in poor evicition decisions on some
  /// targets affecting the performance of compiled code.
  /// (3) Bottom-up allocation is no longer guaranteed to optimally color.
  virtual bool reverseLocalAssignment() const { return false; }

  /// Allow the target to override the cost of using a callee-saved register for
  /// the first time. Default value of 0 means we will use a callee-saved
  /// register if it is available.
  virtual unsigned getCSRFirstUseCost() const { return 0; }

  /// Returns true if the target requires (and can make use of) the register
  /// scavenger.
  virtual bool requiresRegisterScavenging(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target wants to use frame pointer based accesses to
  /// spill to the scavenger emergency spill slot.
  virtual bool useFPForScavengingIndex(const MachineFunction &MF) const {
    return true;
  }

  /// Returns true if the target requires post PEI scavenging of registers for
  /// materializing frame index constants.
  virtual bool requiresFrameIndexScavenging(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target requires using the RegScavenger directly for
  /// frame elimination despite using requiresFrameIndexScavenging.
  virtual bool requiresFrameIndexReplacementScavenging(
      const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target wants the LocalStackAllocation pass to be run
  /// and virtual base registers used for more efficient stack access.
  virtual bool requiresVirtualBaseRegisters(const MachineFunction &MF) const {
    return false;
  }

  /// Return true if target has reserved a spill slot in the stack frame of
  /// the given function for the specified register. e.g. On x86, if the frame
  /// register is required, the first fixed stack object is reserved as its
  /// spill slot. This tells PEI not to create a new stack frame
  /// object for the given register. It should be called only after
  /// determineCalleeSaves().
  virtual bool hasReservedSpillSlot(const MachineFunction &MF, Register Reg,
                                    int &FrameIdx) const {
    return false;
  }

  /// Returns true if the live-ins should be tracked after register allocation.
  virtual bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
    return true;
  }

  /// True if the stack can be realigned for the target.
  virtual bool canRealignStack(const MachineFunction &MF) const;

  /// True if storage within the function requires the stack pointer to be
  /// aligned more than the normal calling convention calls for.
  virtual bool shouldRealignStack(const MachineFunction &MF) const;

  /// True if stack realignment is required and still possible.
  bool hasStackRealignment(const MachineFunction &MF) const {
    return shouldRealignStack(MF) && canRealignStack(MF);
  }

  /// Get the offset from the referenced frame index in the instruction,
  /// if there is one.
  virtual int64_t getFrameIndexInstrOffset(const MachineInstr *MI,
                                           int Idx) const {
    return 0;
  }

  /// Returns true if the instruction's frame index reference would be better
  /// served by a base register other than FP or SP.
  /// Used by LocalStackFrameAllocation to determine which frame index
  /// references it should create new base registers for.
  virtual bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
    return false;
  }

  /// Insert defining instruction(s) for a pointer to FrameIdx before
  /// insertion point I. Return materialized frame pointer.
  virtual Register materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                int FrameIdx,
                                                int64_t Offset) const {
    llvm_unreachable("materializeFrameBaseRegister does not exist on this "
                     "target");
  }

  /// Resolve a frame index operand of an instruction
  /// to reference the indicated base register plus offset instead.
  virtual void resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                 int64_t Offset) const {
    llvm_unreachable("resolveFrameIndex does not exist on this target");
  }

  /// Determine whether a given base register plus offset immediate is
  /// encodable to resolve a frame index.
  virtual bool isFrameOffsetLegal(const MachineInstr *MI, Register BaseReg,
                                  int64_t Offset) const {
    llvm_unreachable("isFrameOffsetLegal does not exist on this target");
  }

  /// Gets the DWARF expression opcodes for \p Offset.
  virtual void getOffsetOpcodes(const StackOffset &Offset,
                                SmallVectorImpl<uint64_t> &Ops) const;

  /// Prepends a DWARF expression for \p Offset to DIExpression \p Expr.
  DIExpression *
  prependOffsetExpression(const DIExpression *Expr, unsigned PrependFlags,
                          const StackOffset &Offset) const;

  /// Spill the register so it can be used by the register scavenger.
  /// Return true if the register was spilled, false otherwise.
  /// If this function does not spill the register, the scavenger
  /// will instead spill it to the emergency spill slot.
  virtual bool saveScavengerRegister(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     MachineBasicBlock::iterator &UseMI,
                                     const TargetRegisterClass *RC,
                                     Register Reg) const {
    return false;
  }

  /// Process frame indices in reverse block order. This changes the behavior of
  /// the RegScavenger passed to eliminateFrameIndex. If this is true targets
  /// should scavengeRegisterBackwards in eliminateFrameIndex. New targets
  /// should prefer reverse scavenging behavior.
  /// TODO: Remove this when all targets return true.
  virtual bool eliminateFrameIndicesBackwards() const { return true; }

  /// This method must be overriden to eliminate abstract frame indices from
  /// instructions which may use them. The instruction referenced by the
  /// iterator contains an MO_FrameIndex operand which must be eliminated by
  /// this method. This method may modify or replace the specified instruction,
  /// as long as it keeps the iterator pointing at the finished product.
  /// SPAdj is the SP adjustment due to call frame setup instruction.
  /// FIOperandNum is the FI operand number.
  /// Returns true if the current instruction was removed and the iterator
  /// is not longer valid
  virtual bool eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                   int SPAdj, unsigned FIOperandNum,
                                   RegScavenger *RS = nullptr) const = 0;

  /// Return the assembly name for \p Reg.
  virtual StringRef getRegAsmName(MCRegister Reg) const {
    // FIXME: We are assuming that the assembly name is equal to the TableGen
    // name converted to lower case
    //
    // The TableGen name is the name of the definition for this register in the
    // target's tablegen files.  For example, the TableGen name of
    // def EAX : Register <...>; is "EAX"
    return StringRef(getName(Reg));
  }

  //===--------------------------------------------------------------------===//
  /// Subtarget Hooks

  /// SrcRC and DstRC will be morphed into NewRC if this returns true.
  virtual bool shouldCoalesce(MachineInstr *MI,
                              const TargetRegisterClass *SrcRC,
                              unsigned SubReg,
                              const TargetRegisterClass *DstRC,
                              unsigned DstSubReg,
                              const TargetRegisterClass *NewRC,
                              LiveIntervals &LIS) const
  { return true; }

  /// Region split has a high compile time cost especially for large live range.
  /// This method is used to decide whether or not \p VirtReg should
  /// go through this expensive splitting heuristic.
  virtual bool shouldRegionSplitForVirtReg(const MachineFunction &MF,
                                           const LiveInterval &VirtReg) const;

  /// Last chance recoloring has a high compile time cost especially for
  /// targets with a lot of registers.
  /// This method is used to decide whether or not \p VirtReg should
  /// go through this expensive heuristic.
  /// When this target hook is hit, by returning false, there is a high
  /// chance that the register allocation will fail altogether (usually with
  /// "ran out of registers").
  /// That said, this error usually points to another problem in the
  /// optimization pipeline.
  virtual bool
  shouldUseLastChanceRecoloringForVirtReg(const MachineFunction &MF,
                                          const LiveInterval &VirtReg) const {
    return true;
  }

  /// Deferred spilling delays the spill insertion of a virtual register
  /// after every other allocation. By deferring the spilling, it is
  /// sometimes possible to eliminate that spilling altogether because
  /// something else could have been eliminated, thus leaving some space
  /// for the virtual register.
  /// However, this comes with a compile time impact because it adds one
  /// more stage to the greedy register allocator.
  /// This method is used to decide whether \p VirtReg should use the deferred
  /// spilling stage instead of being spilled right away.
  virtual bool
  shouldUseDeferredSpillingForVirtReg(const MachineFunction &MF,
                                      const LiveInterval &VirtReg) const {
    return false;
  }

  /// When prioritizing live ranges in register allocation, if this hook returns
  /// true then the AllocationPriority of the register class will be treated as
  /// more important than whether the range is local to a basic block or global.
  virtual bool
  regClassPriorityTrumpsGlobalness(const MachineFunction &MF) const {
    return false;
  }

  //===--------------------------------------------------------------------===//
  /// Debug information queries.

  /// getFrameRegister - This method should return the register used as a base
  /// for values allocated in the current stack frame.
  virtual Register getFrameRegister(const MachineFunction &MF) const = 0;

  /// Mark a register and all its aliases as reserved in the given set.
  void markSuperRegs(BitVector &RegisterSet, MCRegister Reg) const;

  /// Returns true if for every register in the set all super registers are part
  /// of the set as well.
  bool checkAllSuperRegsMarked(const BitVector &RegisterSet,
      ArrayRef<MCPhysReg> Exceptions = ArrayRef<MCPhysReg>()) const;

  virtual const TargetRegisterClass *
  getConstrainedRegClassForOperand(const MachineOperand &MO,
                                   const MachineRegisterInfo &MRI) const {
    return nullptr;
  }

  /// Returns the physical register number of sub-register "Index"
  /// for physical register RegNo. Return zero if the sub-register does not
  /// exist.
  inline MCRegister getSubReg(MCRegister Reg, unsigned Idx) const {
    return static_cast<const MCRegisterInfo *>(this)->getSubReg(Reg, Idx);
  }

  /// Some targets have non-allocatable registers that aren't technically part
  /// of the explicit callee saved register list, but should be handled as such
  /// in certain cases.
  virtual bool isNonallocatableRegisterCalleeSave(MCRegister Reg) const {
    return false;
  }

  /// Returns the Largest Super Class that is being initialized. There
  /// should be a Pseudo Instruction implemented for the super class
  /// that is being returned to ensure that Init Undef can apply the
  /// initialization correctly.
  virtual const TargetRegisterClass *
  getLargestSuperClass(const TargetRegisterClass *RC) const {
    llvm_unreachable("Unexpected target register class.");
  }

  /// Returns if the architecture being targeted has the required Pseudo
  /// Instructions for initializing the register. By default this returns false,
  /// but where it is overriden for an architecture, the behaviour will be
  /// different. This can either be a check to ensure the Register Class is
  /// present, or to return true as an indication the architecture supports the
  /// pass. If using the method that does not check for the Register Class, it
  /// is imperative to ensure all required Pseudo Instructions are implemented,
  /// otherwise compilation may fail with an `Unexpected register class` error.
  virtual bool
  doesRegClassHavePseudoInitUndef(const TargetRegisterClass *RC) const {
    return false;
  }
};

//===----------------------------------------------------------------------===//
//                           SuperRegClassIterator
//===----------------------------------------------------------------------===//
//
// Iterate over the possible super-registers for a given register class. The
// iterator will visit a list of pairs (Idx, Mask) corresponding to the
// possible classes of super-registers.
//
// Each bit mask will have at least one set bit, and each set bit in Mask
// corresponds to a SuperRC such that:
//
//   For all Reg in SuperRC: Reg:Idx is in RC.
//
// The iterator can include (O, RC->getSubClassMask()) as the first entry which
// also satisfies the above requirement, assuming Reg:0 == Reg.
//
class SuperRegClassIterator {
  const unsigned RCMaskWords;
  unsigned SubReg = 0;
  const uint16_t *Idx;
  const uint32_t *Mask;

public:
  /// Create a SuperRegClassIterator that visits all the super-register classes
  /// of RC. When IncludeSelf is set, also include the (0, sub-classes) entry.
  SuperRegClassIterator(const TargetRegisterClass *RC,
                        const TargetRegisterInfo *TRI,
                        bool IncludeSelf = false)
    : RCMaskWords((TRI->getNumRegClasses() + 31) / 32),
      Idx(RC->getSuperRegIndices()), Mask(RC->getSubClassMask()) {
    if (!IncludeSelf)
      ++*this;
  }

  /// Returns true if this iterator is still pointing at a valid entry.
  bool isValid() const { return Idx; }

  /// Returns the current sub-register index.
  unsigned getSubReg() const { return SubReg; }

  /// Returns the bit mask of register classes that getSubReg() projects into
  /// RC.
  /// See TargetRegisterClass::getSubClassMask() for how to use it.
  const uint32_t *getMask() const { return Mask; }

  /// Advance iterator to the next entry.
  void operator++() {
    assert(isValid() && "Cannot move iterator past end.");
    Mask += RCMaskWords;
    SubReg = *Idx++;
    if (!SubReg)
      Idx = nullptr;
  }
};

//===----------------------------------------------------------------------===//
//                           BitMaskClassIterator
//===----------------------------------------------------------------------===//
/// This class encapuslates the logic to iterate over bitmask returned by
/// the various RegClass related APIs.
/// E.g., this class can be used to iterate over the subclasses provided by
/// TargetRegisterClass::getSubClassMask or SuperRegClassIterator::getMask.
class BitMaskClassIterator {
  /// Total number of register classes.
  const unsigned NumRegClasses;
  /// Base index of CurrentChunk.
  /// In other words, the number of bit we read to get at the
  /// beginning of that chunck.
  unsigned Base = 0;
  /// Adjust base index of CurrentChunk.
  /// Base index + how many bit we read within CurrentChunk.
  unsigned Idx = 0;
  /// Current register class ID.
  unsigned ID = 0;
  /// Mask we are iterating over.
  const uint32_t *Mask;
  /// Current chunk of the Mask we are traversing.
  uint32_t CurrentChunk;

  /// Move ID to the next set bit.
  void moveToNextID() {
    // If the current chunk of memory is empty, move to the next one,
    // while making sure we do not go pass the number of register
    // classes.
    while (!CurrentChunk) {
      // Move to the next chunk.
      Base += 32;
      if (Base >= NumRegClasses) {
        ID = NumRegClasses;
        return;
      }
      CurrentChunk = *++Mask;
      Idx = Base;
    }
    // Otherwise look for the first bit set from the right
    // (representation of the class ID is big endian).
    // See getSubClassMask for more details on the representation.
    unsigned Offset = llvm::countr_zero(CurrentChunk);
    // Add the Offset to the adjusted base number of this chunk: Idx.
    // This is the ID of the register class.
    ID = Idx + Offset;

    // Consume the zeros, if any, and the bit we just read
    // so that we are at the right spot for the next call.
    // Do not do Offset + 1 because Offset may be 31 and 32
    // will be UB for the shift, though in that case we could
    // have make the chunk being equal to 0, but that would
    // have introduced a if statement.
    moveNBits(Offset);
    moveNBits(1);
  }

  /// Move \p NumBits Bits forward in CurrentChunk.
  void moveNBits(unsigned NumBits) {
    assert(NumBits < 32 && "Undefined behavior spotted!");
    // Consume the bit we read for the next call.
    CurrentChunk >>= NumBits;
    // Adjust the base for the chunk.
    Idx += NumBits;
  }

public:
  /// Create a BitMaskClassIterator that visits all the register classes
  /// represented by \p Mask.
  ///
  /// \pre \p Mask != nullptr
  BitMaskClassIterator(const uint32_t *Mask, const TargetRegisterInfo &TRI)
      : NumRegClasses(TRI.getNumRegClasses()), Mask(Mask), CurrentChunk(*Mask) {
    // Move to the first ID.
    moveToNextID();
  }

  /// Returns true if this iterator is still pointing at a valid entry.
  bool isValid() const { return getID() != NumRegClasses; }

  /// Returns the current register class ID.
  unsigned getID() const { return ID; }

  /// Advance iterator to the next entry.
  void operator++() {
    assert(isValid() && "Cannot move iterator past end.");
    moveToNextID();
  }
};

// This is useful when building IndexedMaps keyed on virtual registers
struct VirtReg2IndexFunctor {
  using argument_type = Register;
  unsigned operator()(Register Reg) const {
    return Register::virtReg2Index(Reg);
  }
};

/// Prints virtual and physical registers with or without a TRI instance.
///
/// The format is:
///   %noreg          - NoRegister
///   %5              - a virtual register.
///   %5:sub_8bit     - a virtual register with sub-register index (with TRI).
///   %eax            - a physical register
///   %physreg17      - a physical register when no TRI instance given.
///
/// Usage: OS << printReg(Reg, TRI, SubRegIdx) << '\n';
Printable printReg(Register Reg, const TargetRegisterInfo *TRI = nullptr,
                   unsigned SubIdx = 0,
                   const MachineRegisterInfo *MRI = nullptr);

/// Create Printable object to print register units on a \ref raw_ostream.
///
/// Register units are named after their root registers:
///
///   al      - Single root.
///   fp0~st7 - Dual roots.
///
/// Usage: OS << printRegUnit(Unit, TRI) << '\n';
Printable printRegUnit(unsigned Unit, const TargetRegisterInfo *TRI);

/// Create Printable object to print virtual registers and physical
/// registers on a \ref raw_ostream.
Printable printVRegOrUnit(unsigned VRegOrUnit, const TargetRegisterInfo *TRI);

/// Create Printable object to print register classes or register banks
/// on a \ref raw_ostream.
Printable printRegClassOrBank(Register Reg, const MachineRegisterInfo &RegInfo,
                              const TargetRegisterInfo *TRI);

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETREGISTERINFO_H
