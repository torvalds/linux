//===- MC/MCRegisterInfo.h - Target Register Description --------*- C++ -*-===//
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

#ifndef LLVM_MC_MCREGISTERINFO_H
#define LLVM_MC_MCREGISTERINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegister.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

namespace llvm {

class MCRegUnitIterator;
class MCSubRegIterator;
class MCSuperRegIterator;

/// MCRegisterClass - Base class of TargetRegisterClass.
class MCRegisterClass {
public:
  using iterator = const MCPhysReg*;
  using const_iterator = const MCPhysReg*;

  const iterator RegsBegin;
  const uint8_t *const RegSet;
  const uint32_t NameIdx;
  const uint16_t RegsSize;
  const uint16_t RegSetSize;
  const uint16_t ID;
  const uint16_t RegSizeInBits;
  const int8_t CopyCost;
  const bool Allocatable;
  const bool BaseClass;

  /// getID() - Return the register class ID number.
  ///
  unsigned getID() const { return ID; }

  /// begin/end - Return all of the registers in this class.
  ///
  iterator       begin() const { return RegsBegin; }
  iterator         end() const { return RegsBegin + RegsSize; }

  /// getNumRegs - Return the number of registers in this class.
  ///
  unsigned getNumRegs() const { return RegsSize; }

  /// getRegister - Return the specified register in the class.
  ///
  unsigned getRegister(unsigned i) const {
    assert(i < getNumRegs() && "Register number out of range!");
    return RegsBegin[i];
  }

  /// contains - Return true if the specified register is included in this
  /// register class.  This does not include virtual registers.
  bool contains(MCRegister Reg) const {
    unsigned RegNo = unsigned(Reg);
    unsigned InByte = RegNo % 8;
    unsigned Byte = RegNo / 8;
    if (Byte >= RegSetSize)
      return false;
    return (RegSet[Byte] & (1 << InByte)) != 0;
  }

  /// contains - Return true if both registers are in this class.
  bool contains(MCRegister Reg1, MCRegister Reg2) const {
    return contains(Reg1) && contains(Reg2);
  }

  /// Return the size of the physical register in bits if we are able to
  /// determine it. This always returns zero for registers of targets that use
  /// HW modes, as we need more information to determine the size of registers
  /// in such cases. Use TargetRegisterInfo to cover them.
  unsigned getSizeInBits() const { return RegSizeInBits; }

  /// getCopyCost - Return the cost of copying a value between two registers in
  /// this class. A negative number means the register class is very expensive
  /// to copy e.g. status flag register classes.
  int getCopyCost() const { return CopyCost; }

  /// isAllocatable - Return true if this register class may be used to create
  /// virtual registers.
  bool isAllocatable() const { return Allocatable; }

  /// Return true if this register class has a defined BaseClassOrder.
  bool isBaseClass() const { return BaseClass; }
};

/// MCRegisterDesc - This record contains information about a particular
/// register.  The SubRegs field is a zero terminated array of registers that
/// are sub-registers of the specific register, e.g. AL, AH are sub-registers
/// of AX. The SuperRegs field is a zero terminated array of registers that are
/// super-registers of the specific register, e.g. RAX, EAX, are
/// super-registers of AX.
///
struct MCRegisterDesc {
  uint32_t Name;      // Printable name for the reg (for debugging)
  uint32_t SubRegs;   // Sub-register set, described above
  uint32_t SuperRegs; // Super-register set, described above

  // Offset into MCRI::SubRegIndices of a list of sub-register indices for each
  // sub-register in SubRegs.
  uint32_t SubRegIndices;

  // Points to the list of register units. The low bits hold the first regunit
  // number, the high bits hold an offset into DiffLists. See MCRegUnitIterator.
  uint32_t RegUnits;

  /// Index into list with lane mask sequences. The sequence contains a lanemask
  /// for every register unit.
  uint16_t RegUnitLaneMasks;

  // Is true for constant registers.
  bool IsConstant;
};

/// MCRegisterInfo base class - We assume that the target defines a static
/// array of MCRegisterDesc objects that represent all of the machine
/// registers that the target has.  As such, we simply have to track a pointer
/// to this array so that we can turn register number into a register
/// descriptor.
///
/// Note this class is designed to be a base class of TargetRegisterInfo, which
/// is the interface used by codegen. However, specific targets *should never*
/// specialize this class. MCRegisterInfo should only contain getters to access
/// TableGen generated physical register data. It must not be extended with
/// virtual methods.
///
class MCRegisterInfo {
public:
  using regclass_iterator = const MCRegisterClass *;

  /// DwarfLLVMRegPair - Emitted by tablegen so Dwarf<->LLVM reg mappings can be
  /// performed with a binary search.
  struct DwarfLLVMRegPair {
    unsigned FromReg;
    unsigned ToReg;

    bool operator<(DwarfLLVMRegPair RHS) const { return FromReg < RHS.FromReg; }
  };

private:
  const MCRegisterDesc *Desc;                 // Pointer to the descriptor array
  unsigned NumRegs;                           // Number of entries in the array
  MCRegister RAReg;                           // Return address register
  MCRegister PCReg;                           // Program counter register
  const MCRegisterClass *Classes;             // Pointer to the regclass array
  unsigned NumClasses;                        // Number of entries in the array
  unsigned NumRegUnits;                       // Number of regunits.
  const MCPhysReg (*RegUnitRoots)[2];         // Pointer to regunit root table.
  const int16_t *DiffLists;                   // Pointer to the difflists array
  const LaneBitmask *RegUnitMaskSequences;    // Pointer to lane mask sequences
                                              // for register units.
  const char *RegStrings;                     // Pointer to the string table.
  const char *RegClassStrings;                // Pointer to the class strings.
  const uint16_t *SubRegIndices;              // Pointer to the subreg lookup
                                              // array.
  unsigned NumSubRegIndices;                  // Number of subreg indices.
  const uint16_t *RegEncodingTable;           // Pointer to array of register
                                              // encodings.

  unsigned L2DwarfRegsSize;
  unsigned EHL2DwarfRegsSize;
  unsigned Dwarf2LRegsSize;
  unsigned EHDwarf2LRegsSize;
  const DwarfLLVMRegPair *L2DwarfRegs;        // LLVM to Dwarf regs mapping
  const DwarfLLVMRegPair *EHL2DwarfRegs;      // LLVM to Dwarf regs mapping EH
  const DwarfLLVMRegPair *Dwarf2LRegs;        // Dwarf to LLVM regs mapping
  const DwarfLLVMRegPair *EHDwarf2LRegs;      // Dwarf to LLVM regs mapping EH
  DenseMap<MCRegister, int> L2SEHRegs;        // LLVM to SEH regs mapping
  DenseMap<MCRegister, int> L2CVRegs;         // LLVM to CV regs mapping

  mutable std::vector<std::vector<MCPhysReg>> RegAliasesCache;
  ArrayRef<MCPhysReg> getCachedAliasesOf(MCPhysReg R) const;

  /// Iterator class that can traverse the differentially encoded values in
  /// DiffLists. Don't use this class directly, use one of the adaptors below.
  class DiffListIterator
      : public iterator_facade_base<DiffListIterator, std::forward_iterator_tag,
                                    unsigned> {
    unsigned Val = 0;
    const int16_t *List = nullptr;

  public:
    /// Constructs an invalid iterator, which is also the end iterator.
    /// Call init() to point to something useful.
    DiffListIterator() = default;

    /// Point the iterator to InitVal, decoding subsequent values from DiffList.
    void init(unsigned InitVal, const int16_t *DiffList) {
      Val = InitVal;
      List = DiffList;
    }

    /// Returns true if this iterator is not yet at the end.
    bool isValid() const { return List; }

    /// Dereference the iterator to get the value at the current position.
    const unsigned &operator*() const { return Val; }

    using DiffListIterator::iterator_facade_base::operator++;
    /// Pre-increment to move to the next position.
    DiffListIterator &operator++() {
      assert(isValid() && "Cannot move off the end of the list.");
      int16_t D = *List++;
      Val += D;
      // The end of the list is encoded as a 0 differential.
      if (!D)
        List = nullptr;
      return *this;
    }

    bool operator==(const DiffListIterator &Other) const {
      return List == Other.List;
    }
  };

public:
  /// Return an iterator range over all sub-registers of \p Reg, excluding \p
  /// Reg.
  iterator_range<MCSubRegIterator> subregs(MCRegister Reg) const;

  /// Return an iterator range over all sub-registers of \p Reg, including \p
  /// Reg.
  iterator_range<MCSubRegIterator> subregs_inclusive(MCRegister Reg) const;

  /// Return an iterator range over all super-registers of \p Reg, excluding \p
  /// Reg.
  iterator_range<MCSuperRegIterator> superregs(MCRegister Reg) const;

  /// Return an iterator range over all super-registers of \p Reg, including \p
  /// Reg.
  iterator_range<MCSuperRegIterator> superregs_inclusive(MCRegister Reg) const;

  /// Return an iterator range over all sub- and super-registers of \p Reg,
  /// including \p Reg.
  detail::concat_range<const MCPhysReg, iterator_range<MCSubRegIterator>,
                       iterator_range<MCSuperRegIterator>>
  sub_and_superregs_inclusive(MCRegister Reg) const;

  /// Returns an iterator range over all regunits for \p Reg.
  iterator_range<MCRegUnitIterator> regunits(MCRegister Reg) const;

  // These iterators are allowed to sub-class DiffListIterator and access
  // internal list pointers.
  friend class MCSubRegIterator;
  friend class MCSubRegIndexIterator;
  friend class MCSuperRegIterator;
  friend class MCRegUnitIterator;
  friend class MCRegUnitMaskIterator;
  friend class MCRegUnitRootIterator;
  friend class MCRegAliasIterator;

  /// Initialize MCRegisterInfo, called by TableGen
  /// auto-generated routines. *DO NOT USE*.
  void InitMCRegisterInfo(const MCRegisterDesc *D, unsigned NR, unsigned RA,
                          unsigned PC, const MCRegisterClass *C, unsigned NC,
                          const MCPhysReg (*RURoots)[2], unsigned NRU,
                          const int16_t *DL, const LaneBitmask *RUMS,
                          const char *Strings, const char *ClassStrings,
                          const uint16_t *SubIndices, unsigned NumIndices,
                          const uint16_t *RET) {
    Desc = D;
    NumRegs = NR;
    RAReg = RA;
    PCReg = PC;
    Classes = C;
    DiffLists = DL;
    RegUnitMaskSequences = RUMS;
    RegStrings = Strings;
    RegClassStrings = ClassStrings;
    NumClasses = NC;
    RegUnitRoots = RURoots;
    NumRegUnits = NRU;
    SubRegIndices = SubIndices;
    NumSubRegIndices = NumIndices;
    RegEncodingTable = RET;

    // Initialize DWARF register mapping variables
    EHL2DwarfRegs = nullptr;
    EHL2DwarfRegsSize = 0;
    L2DwarfRegs = nullptr;
    L2DwarfRegsSize = 0;
    EHDwarf2LRegs = nullptr;
    EHDwarf2LRegsSize = 0;
    Dwarf2LRegs = nullptr;
    Dwarf2LRegsSize = 0;

    RegAliasesCache.resize(NumRegs);
  }

  /// Used to initialize LLVM register to Dwarf
  /// register number mapping. Called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  void mapLLVMRegsToDwarfRegs(const DwarfLLVMRegPair *Map, unsigned Size,
                              bool isEH) {
    if (isEH) {
      EHL2DwarfRegs = Map;
      EHL2DwarfRegsSize = Size;
    } else {
      L2DwarfRegs = Map;
      L2DwarfRegsSize = Size;
    }
  }

  /// Used to initialize Dwarf register to LLVM
  /// register number mapping. Called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  void mapDwarfRegsToLLVMRegs(const DwarfLLVMRegPair *Map, unsigned Size,
                              bool isEH) {
    if (isEH) {
      EHDwarf2LRegs = Map;
      EHDwarf2LRegsSize = Size;
    } else {
      Dwarf2LRegs = Map;
      Dwarf2LRegsSize = Size;
    }
  }

  /// mapLLVMRegToSEHReg - Used to initialize LLVM register to SEH register
  /// number mapping. By default the SEH register number is just the same
  /// as the LLVM register number.
  /// FIXME: TableGen these numbers. Currently this requires target specific
  /// initialization code.
  void mapLLVMRegToSEHReg(MCRegister LLVMReg, int SEHReg) {
    L2SEHRegs[LLVMReg] = SEHReg;
  }

  void mapLLVMRegToCVReg(MCRegister LLVMReg, int CVReg) {
    L2CVRegs[LLVMReg] = CVReg;
  }

  /// This method should return the register where the return
  /// address can be found.
  MCRegister getRARegister() const {
    return RAReg;
  }

  /// Return the register which is the program counter.
  MCRegister getProgramCounter() const {
    return PCReg;
  }

  const MCRegisterDesc &operator[](MCRegister RegNo) const {
    assert(RegNo < NumRegs &&
           "Attempting to access record for invalid register number!");
    return Desc[RegNo];
  }

  /// Provide a get method, equivalent to [], but more useful with a
  /// pointer to this object.
  const MCRegisterDesc &get(MCRegister RegNo) const {
    return operator[](RegNo);
  }

  /// Returns the physical register number of sub-register "Index"
  /// for physical register RegNo. Return zero if the sub-register does not
  /// exist.
  MCRegister getSubReg(MCRegister Reg, unsigned Idx) const;

  /// Return a super-register of the specified register
  /// Reg so its sub-register of index SubIdx is Reg.
  MCRegister getMatchingSuperReg(MCRegister Reg, unsigned SubIdx,
                                 const MCRegisterClass *RC) const;

  /// For a given register pair, return the sub-register index
  /// if the second register is a sub-register of the first. Return zero
  /// otherwise.
  unsigned getSubRegIndex(MCRegister RegNo, MCRegister SubRegNo) const;

  /// Return the human-readable symbolic target-specific name for the
  /// specified physical register.
  const char *getName(MCRegister RegNo) const {
    return RegStrings + get(RegNo).Name;
  }

  /// Returns true if the given register is constant.
  bool isConstant(MCRegister RegNo) const { return get(RegNo).IsConstant; }

  /// Return the number of registers this target has (useful for
  /// sizing arrays holding per register information)
  unsigned getNumRegs() const {
    return NumRegs;
  }

  /// Return the number of sub-register indices
  /// understood by the target. Index 0 is reserved for the no-op sub-register,
  /// while 1 to getNumSubRegIndices() - 1 represent real sub-registers.
  unsigned getNumSubRegIndices() const {
    return NumSubRegIndices;
  }

  /// Return the number of (native) register units in the
  /// target. Register units are numbered from 0 to getNumRegUnits() - 1. They
  /// can be accessed through MCRegUnitIterator defined below.
  unsigned getNumRegUnits() const {
    return NumRegUnits;
  }

  /// Map a target register to an equivalent dwarf register
  /// number.  Returns -1 if there is no equivalent value.  The second
  /// parameter allows targets to use different numberings for EH info and
  /// debugging info.
  int getDwarfRegNum(MCRegister RegNum, bool isEH) const;

  /// Map a dwarf register back to a target register. Returns std::nullopt is
  /// there is no mapping.
  std::optional<unsigned> getLLVMRegNum(unsigned RegNum, bool isEH) const;

  /// Map a target EH register number to an equivalent DWARF register
  /// number.
  int getDwarfRegNumFromDwarfEHRegNum(unsigned RegNum) const;

  /// Map a target register to an equivalent SEH register
  /// number.  Returns LLVM register number if there is no equivalent value.
  int getSEHRegNum(MCRegister RegNum) const;

  /// Map a target register to an equivalent CodeView register
  /// number.
  int getCodeViewRegNum(MCRegister RegNum) const;

  regclass_iterator regclass_begin() const { return Classes; }
  regclass_iterator regclass_end() const { return Classes+NumClasses; }
  iterator_range<regclass_iterator> regclasses() const {
    return make_range(regclass_begin(), regclass_end());
  }

  unsigned getNumRegClasses() const {
    return (unsigned)(regclass_end()-regclass_begin());
  }

  /// Returns the register class associated with the enumeration
  /// value.  See class MCOperandInfo.
  const MCRegisterClass& getRegClass(unsigned i) const {
    assert(i < getNumRegClasses() && "Register Class ID out of range");
    return Classes[i];
  }

  const char *getRegClassName(const MCRegisterClass *Class) const {
    return RegClassStrings + Class->NameIdx;
  }

   /// Returns the encoding for RegNo
  uint16_t getEncodingValue(MCRegister RegNo) const {
    assert(RegNo < NumRegs &&
           "Attempting to get encoding for invalid register number!");
    return RegEncodingTable[RegNo];
  }

  /// Returns true if RegB is a sub-register of RegA.
  bool isSubRegister(MCRegister RegA, MCRegister RegB) const {
    return isSuperRegister(RegB, RegA);
  }

  /// Returns true if RegB is a super-register of RegA.
  bool isSuperRegister(MCRegister RegA, MCRegister RegB) const;

  /// Returns true if RegB is a sub-register of RegA or if RegB == RegA.
  bool isSubRegisterEq(MCRegister RegA, MCRegister RegB) const {
    return isSuperRegisterEq(RegB, RegA);
  }

  /// Returns true if RegB is a super-register of RegA or if
  /// RegB == RegA.
  bool isSuperRegisterEq(MCRegister RegA, MCRegister RegB) const {
    return RegA == RegB || isSuperRegister(RegA, RegB);
  }

  /// Returns true if RegB is a super-register or sub-register of RegA
  /// or if RegB == RegA.
  bool isSuperOrSubRegisterEq(MCRegister RegA, MCRegister RegB) const {
    return isSubRegisterEq(RegA, RegB) || isSuperRegister(RegA, RegB);
  }

  /// Returns true if the two registers are equal or alias each other.
  bool regsOverlap(MCRegister RegA, MCRegister RegB) const;
};

//===----------------------------------------------------------------------===//
//                          Register List Iterators
//===----------------------------------------------------------------------===//

// MCRegisterInfo provides lists of super-registers, sub-registers, and
// aliasing registers. Use these iterator classes to traverse the lists.

/// MCSubRegIterator enumerates all sub-registers of Reg.
/// If IncludeSelf is set, Reg itself is included in the list.
class MCSubRegIterator
    : public iterator_adaptor_base<MCSubRegIterator,
                                   MCRegisterInfo::DiffListIterator,
                                   std::forward_iterator_tag, const MCPhysReg> {
  // Cache the current value, so that we can return a reference to it.
  MCPhysReg Val;

public:
  /// Constructs an end iterator.
  MCSubRegIterator() = default;

  MCSubRegIterator(MCRegister Reg, const MCRegisterInfo *MCRI,
                   bool IncludeSelf = false) {
    assert(MCRegister::isPhysicalRegister(Reg.id()));
    I.init(Reg.id(), MCRI->DiffLists + MCRI->get(Reg).SubRegs);
    // Initially, the iterator points to Reg itself.
    Val = MCPhysReg(*I);
    if (!IncludeSelf)
      ++*this;
  }

  const MCPhysReg &operator*() const { return Val; }

  using iterator_adaptor_base::operator++;
  MCSubRegIterator &operator++() {
    Val = MCPhysReg(*++I);
    return *this;
  }

  /// Returns true if this iterator is not yet at the end.
  bool isValid() const { return I.isValid(); }
};

/// Iterator that enumerates the sub-registers of a Reg and the associated
/// sub-register indices.
class MCSubRegIndexIterator {
  MCSubRegIterator SRIter;
  const uint16_t *SRIndex;

public:
  /// Constructs an iterator that traverses subregisters and their
  /// associated subregister indices.
  MCSubRegIndexIterator(MCRegister Reg, const MCRegisterInfo *MCRI)
    : SRIter(Reg, MCRI) {
    SRIndex = MCRI->SubRegIndices + MCRI->get(Reg).SubRegIndices;
  }

  /// Returns current sub-register.
  MCRegister getSubReg() const {
    return *SRIter;
  }

  /// Returns sub-register index of the current sub-register.
  unsigned getSubRegIndex() const {
    return *SRIndex;
  }

  /// Returns true if this iterator is not yet at the end.
  bool isValid() const { return SRIter.isValid(); }

  /// Moves to the next position.
  MCSubRegIndexIterator &operator++() {
    ++SRIter;
    ++SRIndex;
    return *this;
  }
};

/// MCSuperRegIterator enumerates all super-registers of Reg.
/// If IncludeSelf is set, Reg itself is included in the list.
class MCSuperRegIterator
    : public iterator_adaptor_base<MCSuperRegIterator,
                                   MCRegisterInfo::DiffListIterator,
                                   std::forward_iterator_tag, const MCPhysReg> {
  // Cache the current value, so that we can return a reference to it.
  MCPhysReg Val;

public:
  /// Constructs an end iterator.
  MCSuperRegIterator() = default;

  MCSuperRegIterator(MCRegister Reg, const MCRegisterInfo *MCRI,
                     bool IncludeSelf = false) {
    assert(MCRegister::isPhysicalRegister(Reg.id()));
    I.init(Reg.id(), MCRI->DiffLists + MCRI->get(Reg).SuperRegs);
    // Initially, the iterator points to Reg itself.
    Val = MCPhysReg(*I);
    if (!IncludeSelf)
      ++*this;
  }

  const MCPhysReg &operator*() const { return Val; }

  using iterator_adaptor_base::operator++;
  MCSuperRegIterator &operator++() {
    Val = MCPhysReg(*++I);
    return *this;
  }

  /// Returns true if this iterator is not yet at the end.
  bool isValid() const { return I.isValid(); }
};

// Definition for isSuperRegister. Put it down here since it needs the
// iterator defined above in addition to the MCRegisterInfo class itself.
inline bool MCRegisterInfo::isSuperRegister(MCRegister RegA, MCRegister RegB) const{
  return is_contained(superregs(RegA), RegB);
}

//===----------------------------------------------------------------------===//
//                               Register Units
//===----------------------------------------------------------------------===//

// MCRegUnitIterator enumerates a list of register units for Reg. The list is
// in ascending numerical order.
class MCRegUnitIterator
    : public iterator_adaptor_base<MCRegUnitIterator,
                                   MCRegisterInfo::DiffListIterator,
                                   std::forward_iterator_tag, const MCRegUnit> {
  // The value must be kept in sync with RegisterInfoEmitter.cpp.
  static constexpr unsigned RegUnitBits = 12;
  // Cache the current value, so that we can return a reference to it.
  MCRegUnit Val;

public:
  /// Constructs an end iterator.
  MCRegUnitIterator() = default;

  MCRegUnitIterator(MCRegister Reg, const MCRegisterInfo *MCRI) {
    assert(Reg && "Null register has no regunits");
    assert(MCRegister::isPhysicalRegister(Reg.id()));
    // Decode the RegUnits MCRegisterDesc field.
    unsigned RU = MCRI->get(Reg).RegUnits;
    unsigned FirstRU = RU & ((1u << RegUnitBits) - 1);
    unsigned Offset = RU >> RegUnitBits;
    I.init(FirstRU, MCRI->DiffLists + Offset);
    Val = MCRegUnit(*I);
  }

  const MCRegUnit &operator*() const { return Val; }

  using iterator_adaptor_base::operator++;
  MCRegUnitIterator &operator++() {
    Val = MCRegUnit(*++I);
    return *this;
  }

  /// Returns true if this iterator is not yet at the end.
  bool isValid() const { return I.isValid(); }
};

/// MCRegUnitMaskIterator enumerates a list of register units and their
/// associated lane masks for Reg. The register units are in ascending
/// numerical order.
class MCRegUnitMaskIterator {
  MCRegUnitIterator RUIter;
  const LaneBitmask *MaskListIter;

public:
  MCRegUnitMaskIterator() = default;

  /// Constructs an iterator that traverses the register units and their
  /// associated LaneMasks in Reg.
  MCRegUnitMaskIterator(MCRegister Reg, const MCRegisterInfo *MCRI)
    : RUIter(Reg, MCRI) {
      uint16_t Idx = MCRI->get(Reg).RegUnitLaneMasks;
      MaskListIter = &MCRI->RegUnitMaskSequences[Idx];
  }

  /// Returns a (RegUnit, LaneMask) pair.
  std::pair<unsigned,LaneBitmask> operator*() const {
    return std::make_pair(*RUIter, *MaskListIter);
  }

  /// Returns true if this iterator is not yet at the end.
  bool isValid() const { return RUIter.isValid(); }

  /// Moves to the next position.
  MCRegUnitMaskIterator &operator++() {
    ++MaskListIter;
    ++RUIter;
    return *this;
  }
};

// Each register unit has one or two root registers. The complete set of
// registers containing a register unit is the union of the roots and their
// super-registers. All registers aliasing Unit can be visited like this:
//
//   for (MCRegUnitRootIterator RI(Unit, MCRI); RI.isValid(); ++RI) {
//     for (MCSuperRegIterator SI(*RI, MCRI, true); SI.isValid(); ++SI)
//       visit(*SI);
//    }

/// MCRegUnitRootIterator enumerates the root registers of a register unit.
class MCRegUnitRootIterator {
  uint16_t Reg0 = 0;
  uint16_t Reg1 = 0;

public:
  MCRegUnitRootIterator() = default;

  MCRegUnitRootIterator(unsigned RegUnit, const MCRegisterInfo *MCRI) {
    assert(RegUnit < MCRI->getNumRegUnits() && "Invalid register unit");
    Reg0 = MCRI->RegUnitRoots[RegUnit][0];
    Reg1 = MCRI->RegUnitRoots[RegUnit][1];
  }

  /// Dereference to get the current root register.
  unsigned operator*() const {
    return Reg0;
  }

  /// Check if the iterator is at the end of the list.
  bool isValid() const {
    return Reg0;
  }

  /// Preincrement to move to the next root register.
  MCRegUnitRootIterator &operator++() {
    assert(isValid() && "Cannot move off the end of the list.");
    Reg0 = Reg1;
    Reg1 = 0;
    return *this;
  }
};

/// MCRegAliasIterator enumerates all registers aliasing Reg.
class MCRegAliasIterator {
private:
  const MCPhysReg *It = nullptr;
  const MCPhysReg *End = nullptr;

public:
  MCRegAliasIterator(MCRegister Reg, const MCRegisterInfo *MCRI,
                     bool IncludeSelf) {
    ArrayRef<MCPhysReg> Cache = MCRI->getCachedAliasesOf(Reg);
    assert(Cache.back() == Reg);
    It = Cache.begin();
    End = Cache.end();
    if (!IncludeSelf)
      --End;
  }

  bool isValid() const { return It != End; }

  MCRegister operator*() const { return *It; }

  MCRegAliasIterator &operator++() {
    assert(isValid() && "Cannot move off the end of the list.");
    ++It;
    return *this;
  }
};

inline iterator_range<MCSubRegIterator>
MCRegisterInfo::subregs(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/false}, MCSubRegIterator());
}

inline iterator_range<MCSubRegIterator>
MCRegisterInfo::subregs_inclusive(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/true}, MCSubRegIterator());
}

inline iterator_range<MCSuperRegIterator>
MCRegisterInfo::superregs(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/false}, MCSuperRegIterator());
}

inline iterator_range<MCSuperRegIterator>
MCRegisterInfo::superregs_inclusive(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/true}, MCSuperRegIterator());
}

inline detail::concat_range<const MCPhysReg, iterator_range<MCSubRegIterator>,
                            iterator_range<MCSuperRegIterator>>
MCRegisterInfo::sub_and_superregs_inclusive(MCRegister Reg) const {
  return concat<const MCPhysReg>(subregs_inclusive(Reg), superregs(Reg));
}

inline iterator_range<MCRegUnitIterator>
MCRegisterInfo::regunits(MCRegister Reg) const {
  return make_range({Reg, this}, MCRegUnitIterator());
}

} // end namespace llvm

#endif // LLVM_MC_MCREGISTERINFO_H
