//===- CodeGenRegisters.h - Register and RegisterClass Info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines structures to encapsulate information gleaned from the
// target register and register class definitions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENREGISTERS_H
#define LLVM_UTILS_TABLEGEN_CODEGENREGISTERS_H

#include "CodeGenHwModes.h"
#include "InfoByHwMode.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/SetTheory.h"
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class CodeGenRegBank;

/// Used to encode a step in a register lane mask transformation.
/// Mask the bits specified in Mask, then rotate them Rol bits to the left
/// assuming a wraparound at 32bits.
struct MaskRolPair {
  LaneBitmask Mask;
  uint8_t RotateLeft;

  bool operator==(const MaskRolPair Other) const {
    return Mask == Other.Mask && RotateLeft == Other.RotateLeft;
  }
  bool operator!=(const MaskRolPair Other) const {
    return Mask != Other.Mask || RotateLeft != Other.RotateLeft;
  }
};

/// CodeGenSubRegIndex - Represents a sub-register index.
class CodeGenSubRegIndex {
  Record *const TheDef;
  std::string Name;
  std::string Namespace;

public:
  SubRegRangeByHwMode Range;
  const unsigned EnumValue;
  mutable LaneBitmask LaneMask;
  mutable SmallVector<MaskRolPair, 1> CompositionLaneMaskTransform;

  /// A list of subregister indexes concatenated resulting in this
  /// subregister index. This is the reverse of CodeGenRegBank::ConcatIdx.
  SmallVector<CodeGenSubRegIndex *, 4> ConcatenationOf;

  // Are all super-registers containing this SubRegIndex covered by their
  // sub-registers?
  bool AllSuperRegsCovered;
  // A subregister index is "artificial" if every subregister obtained
  // from applying this index is artificial. Artificial subregister
  // indexes are not used to create new register classes.
  bool Artificial;

  CodeGenSubRegIndex(Record *R, unsigned Enum, const CodeGenHwModes &CGH);
  CodeGenSubRegIndex(StringRef N, StringRef Nspace, unsigned Enum);
  CodeGenSubRegIndex(CodeGenSubRegIndex &) = delete;

  const std::string &getName() const { return Name; }
  const std::string &getNamespace() const { return Namespace; }
  std::string getQualifiedName() const;

  // Map of composite subreg indices.
  typedef std::map<CodeGenSubRegIndex *, CodeGenSubRegIndex *,
                   deref<std::less<>>>
      CompMap;

  // Returns the subreg index that results from composing this with Idx.
  // Returns NULL if this and Idx don't compose.
  CodeGenSubRegIndex *compose(CodeGenSubRegIndex *Idx) const {
    CompMap::const_iterator I = Composed.find(Idx);
    return I == Composed.end() ? nullptr : I->second;
  }

  // Add a composite subreg index: this+A = B.
  // Return a conflicting composite, or NULL
  CodeGenSubRegIndex *addComposite(CodeGenSubRegIndex *A, CodeGenSubRegIndex *B,
                                   const CodeGenHwModes &CGH) {
    assert(A && B);
    std::pair<CompMap::iterator, bool> Ins = Composed.insert(std::pair(A, B));

    // Synthetic subreg indices that aren't contiguous (for instance ARM
    // register tuples) don't have a bit range, so it's OK to let
    // B->Offset == -1. For the other cases, accumulate the offset and set
    // the size here. Only do so if there is no offset yet though.
    unsigned NumModes = CGH.getNumModeIds();
    // Skip default mode.
    for (unsigned M = 0; M < NumModes; ++M) {
      // Handle DefaultMode last.
      if (M == DefaultMode)
        continue;
      SubRegRange &Range = this->Range.get(M);
      SubRegRange &ARange = A->Range.get(M);
      SubRegRange &BRange = B->Range.get(M);

      if (Range.Offset != (uint16_t)-1 && ARange.Offset != (uint16_t)-1 &&
          BRange.Offset == (uint16_t)-1) {
        BRange.Offset = Range.Offset + ARange.Offset;
        BRange.Size = ARange.Size;
      }
    }

    // Now handle default.
    SubRegRange &Range = this->Range.get(DefaultMode);
    SubRegRange &ARange = A->Range.get(DefaultMode);
    SubRegRange &BRange = B->Range.get(DefaultMode);
    if (Range.Offset != (uint16_t)-1 && ARange.Offset != (uint16_t)-1 &&
        BRange.Offset == (uint16_t)-1) {
      BRange.Offset = Range.Offset + ARange.Offset;
      BRange.Size = ARange.Size;
    }

    return (Ins.second || Ins.first->second == B) ? nullptr : Ins.first->second;
  }

  // Update the composite maps of components specified in 'ComposedOf'.
  void updateComponents(CodeGenRegBank &);

  // Return the map of composites.
  const CompMap &getComposites() const { return Composed; }

  // Compute LaneMask from Composed. Return LaneMask.
  LaneBitmask computeLaneMask() const;

  void setConcatenationOf(ArrayRef<CodeGenSubRegIndex *> Parts);

  /// Replaces subregister indexes in the `ConcatenationOf` list with
  /// list of subregisters they are composed of (if any). Do this recursively.
  void computeConcatTransitiveClosure();

  bool operator<(const CodeGenSubRegIndex &RHS) const {
    return this->EnumValue < RHS.EnumValue;
  }

private:
  CompMap Composed;
};

/// CodeGenRegister - Represents a register definition.
class CodeGenRegister {
public:
  Record *TheDef;
  unsigned EnumValue;
  std::vector<int64_t> CostPerUse;
  bool CoveredBySubRegs = true;
  bool HasDisjunctSubRegs = false;
  bool Artificial = true;
  bool Constant = false;

  // Map SubRegIndex -> Register.
  typedef std::map<CodeGenSubRegIndex *, CodeGenRegister *, deref<std::less<>>>
      SubRegMap;

  CodeGenRegister(Record *R, unsigned Enum);

  StringRef getName() const;

  // Extract more information from TheDef. This is used to build an object
  // graph after all CodeGenRegister objects have been created.
  void buildObjectGraph(CodeGenRegBank &);

  // Lazily compute a map of all sub-registers.
  // This includes unique entries for all sub-sub-registers.
  const SubRegMap &computeSubRegs(CodeGenRegBank &);

  // Compute extra sub-registers by combining the existing sub-registers.
  void computeSecondarySubRegs(CodeGenRegBank &);

  // Add this as a super-register to all sub-registers after the sub-register
  // graph has been built.
  void computeSuperRegs(CodeGenRegBank &);

  const SubRegMap &getSubRegs() const {
    assert(SubRegsComplete && "Must precompute sub-registers");
    return SubRegs;
  }

  // Add sub-registers to OSet following a pre-order defined by the .td file.
  void addSubRegsPreOrder(SetVector<const CodeGenRegister *> &OSet,
                          CodeGenRegBank &) const;

  // Return the sub-register index naming Reg as a sub-register of this
  // register. Returns NULL if Reg is not a sub-register.
  CodeGenSubRegIndex *getSubRegIndex(const CodeGenRegister *Reg) const {
    return SubReg2Idx.lookup(Reg);
  }

  typedef std::vector<const CodeGenRegister *> SuperRegList;

  // Get the list of super-registers in topological order, small to large.
  // This is valid after computeSubRegs visits all registers during RegBank
  // construction.
  const SuperRegList &getSuperRegs() const {
    assert(SubRegsComplete && "Must precompute sub-registers");
    return SuperRegs;
  }

  // Get the list of ad hoc aliases. The graph is symmetric, so the list
  // contains all registers in 'Aliases', and all registers that mention this
  // register in 'Aliases'.
  ArrayRef<CodeGenRegister *> getExplicitAliases() const {
    return ExplicitAliases;
  }

  // Get the topological signature of this register. This is a small integer
  // less than RegBank.getNumTopoSigs(). Registers with the same TopoSig have
  // identical sub-register structure. That is, they support the same set of
  // sub-register indices mapping to the same kind of sub-registers
  // (TopoSig-wise).
  unsigned getTopoSig() const {
    assert(SuperRegsComplete && "TopoSigs haven't been computed yet.");
    return TopoSig;
  }

  // List of register units in ascending order.
  typedef SparseBitVector<> RegUnitList;
  typedef SmallVector<LaneBitmask, 16> RegUnitLaneMaskList;

  // How many entries in RegUnitList are native?
  RegUnitList NativeRegUnits;

  // Get the list of register units.
  // This is only valid after computeSubRegs() completes.
  const RegUnitList &getRegUnits() const { return RegUnits; }

  ArrayRef<LaneBitmask> getRegUnitLaneMasks() const {
    return ArrayRef(RegUnitLaneMasks).slice(0, NativeRegUnits.count());
  }

  // Get the native register units. This is a prefix of getRegUnits().
  RegUnitList getNativeRegUnits() const { return NativeRegUnits; }

  void setRegUnitLaneMasks(const RegUnitLaneMaskList &LaneMasks) {
    RegUnitLaneMasks = LaneMasks;
  }

  // Inherit register units from subregisters.
  // Return true if the RegUnits changed.
  bool inheritRegUnits(CodeGenRegBank &RegBank);

  // Adopt a register unit for pressure tracking.
  // A unit is adopted iff its unit number is >= NativeRegUnits.count().
  void adoptRegUnit(unsigned RUID) { RegUnits.set(RUID); }

  // Get the sum of this register's register unit weights.
  unsigned getWeight(const CodeGenRegBank &RegBank) const;

  // Canonically ordered set.
  typedef std::vector<const CodeGenRegister *> Vec;

private:
  bool SubRegsComplete;
  bool SuperRegsComplete;
  unsigned TopoSig;

  // The sub-registers explicit in the .td file form a tree.
  SmallVector<CodeGenSubRegIndex *, 8> ExplicitSubRegIndices;
  SmallVector<CodeGenRegister *, 8> ExplicitSubRegs;

  // Explicit ad hoc aliases, symmetrized to form an undirected graph.
  SmallVector<CodeGenRegister *, 8> ExplicitAliases;

  // Super-registers where this is the first explicit sub-register.
  SuperRegList LeadingSuperRegs;

  SubRegMap SubRegs;
  SuperRegList SuperRegs;
  DenseMap<const CodeGenRegister *, CodeGenSubRegIndex *> SubReg2Idx;
  RegUnitList RegUnits;
  RegUnitLaneMaskList RegUnitLaneMasks;
};

inline bool operator<(const CodeGenRegister &A, const CodeGenRegister &B) {
  return A.EnumValue < B.EnumValue;
}

inline bool operator==(const CodeGenRegister &A, const CodeGenRegister &B) {
  return A.EnumValue == B.EnumValue;
}

class CodeGenRegisterClass {
  CodeGenRegister::Vec Members;
  // Allocation orders. Order[0] always contains all registers in Members.
  std::vector<SmallVector<Record *, 16>> Orders;
  // Bit mask of sub-classes including this, indexed by their EnumValue.
  BitVector SubClasses;
  // List of super-classes, topologocally ordered to have the larger classes
  // first.  This is the same as sorting by EnumValue.
  SmallVector<CodeGenRegisterClass *, 4> SuperClasses;
  Record *TheDef;
  std::string Name;

  // For a synthesized class, inherit missing properties from the nearest
  // super-class.
  void inheritProperties(CodeGenRegBank &);

  // Map SubRegIndex -> sub-class.  This is the largest sub-class where all
  // registers have a SubRegIndex sub-register.
  DenseMap<const CodeGenSubRegIndex *, CodeGenRegisterClass *>
      SubClassWithSubReg;

  // Map SubRegIndex -> set of super-reg classes.  This is all register
  // classes SuperRC such that:
  //
  //   R:SubRegIndex in this RC for all R in SuperRC.
  //
  DenseMap<const CodeGenSubRegIndex *, SmallPtrSet<CodeGenRegisterClass *, 8>>
      SuperRegClasses;

  // Bit vector of TopoSigs for the registers in this class. This will be
  // very sparse on regular architectures.
  BitVector TopoSigs;

public:
  unsigned EnumValue;
  StringRef Namespace;
  SmallVector<ValueTypeByHwMode, 4> VTs;
  RegSizeInfoByHwMode RSI;
  int CopyCost;
  bool Allocatable;
  StringRef AltOrderSelect;
  uint8_t AllocationPriority;
  bool GlobalPriority;
  uint8_t TSFlags;
  /// Contains the combination of the lane masks of all subregisters.
  LaneBitmask LaneMask;
  /// True if there are at least 2 subregisters which do not interfere.
  bool HasDisjunctSubRegs;
  bool CoveredBySubRegs;
  /// A register class is artificial if all its members are artificial.
  bool Artificial;
  /// Generate register pressure set for this register class and any class
  /// synthesized from it.
  bool GeneratePressureSet;

  // Return the Record that defined this class, or NULL if the class was
  // created by TableGen.
  Record *getDef() const { return TheDef; }

  std::string getNamespaceQualification() const;
  const std::string &getName() const { return Name; }
  std::string getQualifiedName() const;
  std::string getIdName() const;
  std::string getQualifiedIdName() const;
  ArrayRef<ValueTypeByHwMode> getValueTypes() const { return VTs; }
  unsigned getNumValueTypes() const { return VTs.size(); }
  bool hasType(const ValueTypeByHwMode &VT) const;

  const ValueTypeByHwMode &getValueTypeNum(unsigned VTNum) const {
    if (VTNum < VTs.size())
      return VTs[VTNum];
    llvm_unreachable("VTNum greater than number of ValueTypes in RegClass!");
  }

  // Return true if this class contains the register.
  bool contains(const CodeGenRegister *) const;

  // Returns true if RC is a subclass.
  // RC is a sub-class of this class if it is a valid replacement for any
  // instruction operand where a register of this classis required. It must
  // satisfy these conditions:
  //
  // 1. All RC registers are also in this.
  // 2. The RC spill size must not be smaller than our spill size.
  // 3. RC spill alignment must be compatible with ours.
  //
  bool hasSubClass(const CodeGenRegisterClass *RC) const {
    return SubClasses.test(RC->EnumValue);
  }

  // getSubClassWithSubReg - Returns the largest sub-class where all
  // registers have a SubIdx sub-register.
  CodeGenRegisterClass *
  getSubClassWithSubReg(const CodeGenSubRegIndex *SubIdx) const {
    return SubClassWithSubReg.lookup(SubIdx);
  }

  /// Find largest subclass where all registers have SubIdx subregisters in
  /// SubRegClass and the largest subregister class that contains those
  /// subregisters without (as far as possible) also containing additional
  /// registers.
  ///
  /// This can be used to find a suitable pair of classes for subregister
  /// copies. \return std::pair<SubClass, SubRegClass> where SubClass is a
  /// SubClass is a class where every register has SubIdx and SubRegClass is a
  /// class where every register is covered by the SubIdx subregister of
  /// SubClass.
  std::optional<std::pair<CodeGenRegisterClass *, CodeGenRegisterClass *>>
  getMatchingSubClassWithSubRegs(CodeGenRegBank &RegBank,
                                 const CodeGenSubRegIndex *SubIdx) const;

  void setSubClassWithSubReg(const CodeGenSubRegIndex *SubIdx,
                             CodeGenRegisterClass *SubRC) {
    SubClassWithSubReg[SubIdx] = SubRC;
  }

  // getSuperRegClasses - Returns a bit vector of all register classes
  // containing only SubIdx super-registers of this class.
  void getSuperRegClasses(const CodeGenSubRegIndex *SubIdx,
                          BitVector &Out) const;

  // addSuperRegClass - Add a class containing only SubIdx super-registers.
  void addSuperRegClass(CodeGenSubRegIndex *SubIdx,
                        CodeGenRegisterClass *SuperRC) {
    SuperRegClasses[SubIdx].insert(SuperRC);
  }

  // getSubClasses - Returns a constant BitVector of subclasses indexed by
  // EnumValue.
  // The SubClasses vector includes an entry for this class.
  const BitVector &getSubClasses() const { return SubClasses; }

  // getSuperClasses - Returns a list of super classes ordered by EnumValue.
  // The array does not include an entry for this class.
  ArrayRef<CodeGenRegisterClass *> getSuperClasses() const {
    return SuperClasses;
  }

  // Returns an ordered list of class members.
  // The order of registers is the same as in the .td file.
  // No = 0 is the default allocation order, No = 1 is the first alternative.
  ArrayRef<Record *> getOrder(unsigned No = 0) const { return Orders[No]; }

  // Return the total number of allocation orders available.
  unsigned getNumOrders() const { return Orders.size(); }

  // Get the set of registers.  This set contains the same registers as
  // getOrder(0).
  const CodeGenRegister::Vec &getMembers() const { return Members; }

  // Get a bit vector of TopoSigs present in this register class.
  const BitVector &getTopoSigs() const { return TopoSigs; }

  // Get a weight of this register class.
  unsigned getWeight(const CodeGenRegBank &) const;

  // Populate a unique sorted list of units from a register set.
  void buildRegUnitSet(const CodeGenRegBank &RegBank,
                       std::vector<unsigned> &RegUnits) const;

  CodeGenRegisterClass(CodeGenRegBank &, Record *R);
  CodeGenRegisterClass(CodeGenRegisterClass &) = delete;

  // A key representing the parts of a register class used for forming
  // sub-classes.  Note the ordering provided by this key is not the same as
  // the topological order used for the EnumValues.
  struct Key {
    const CodeGenRegister::Vec *Members;
    RegSizeInfoByHwMode RSI;

    Key(const CodeGenRegister::Vec *M, const RegSizeInfoByHwMode &I)
        : Members(M), RSI(I) {}

    Key(const CodeGenRegisterClass &RC)
        : Members(&RC.getMembers()), RSI(RC.RSI) {}

    // Lexicographical order of (Members, RegSizeInfoByHwMode).
    bool operator<(const Key &) const;
  };

  // Create a non-user defined register class.
  CodeGenRegisterClass(CodeGenRegBank &, StringRef Name, Key Props);

  // Called by CodeGenRegBank::CodeGenRegBank().
  static void computeSubClasses(CodeGenRegBank &);

  // Get ordering value among register base classes.
  std::optional<int> getBaseClassOrder() const {
    if (TheDef && !TheDef->isValueUnset("BaseClassOrder"))
      return TheDef->getValueAsInt("BaseClassOrder");
    return {};
  }
};

// Register categories are used when we need to deterine the category a
// register falls into (GPR, vector, fixed, etc.) without having to know
// specific information about the target architecture.
class CodeGenRegisterCategory {
  Record *TheDef;
  std::string Name;
  std::list<CodeGenRegisterClass *> Classes;

public:
  CodeGenRegisterCategory(CodeGenRegBank &, Record *R);
  CodeGenRegisterCategory(CodeGenRegisterCategory &) = delete;

  // Return the Record that defined this class, or NULL if the class was
  // created by TableGen.
  Record *getDef() const { return TheDef; }

  std::string getName() const { return Name; }
  std::list<CodeGenRegisterClass *> getClasses() const { return Classes; }
};

// Register units are used to model interference and register pressure.
// Every register is assigned one or more register units such that two
// registers overlap if and only if they have a register unit in common.
//
// Normally, one register unit is created per leaf register. Non-leaf
// registers inherit the units of their sub-registers.
struct RegUnit {
  // Weight assigned to this RegUnit for estimating register pressure.
  // This is useful when equalizing weights in register classes with mixed
  // register topologies.
  unsigned Weight;

  // Each native RegUnit corresponds to one or two root registers. The full
  // set of registers containing this unit can be computed as the union of
  // these two registers and their super-registers.
  const CodeGenRegister *Roots[2];

  // Index into RegClassUnitSets where we can find the list of UnitSets that
  // contain this unit.
  unsigned RegClassUnitSetsIdx;
  // A register unit is artificial if at least one of its roots is
  // artificial.
  bool Artificial;

  RegUnit() : Weight(0), RegClassUnitSetsIdx(0), Artificial(false) {
    Roots[0] = Roots[1] = nullptr;
  }

  ArrayRef<const CodeGenRegister *> getRoots() const {
    assert(!(Roots[1] && !Roots[0]) && "Invalid roots array");
    return ArrayRef(Roots, !!Roots[0] + !!Roots[1]);
  }
};

// Each RegUnitSet is a sorted vector with a name.
struct RegUnitSet {
  typedef std::vector<unsigned>::const_iterator iterator;

  std::string Name;
  std::vector<unsigned> Units;
  unsigned Weight = 0; // Cache the sum of all unit weights.
  unsigned Order = 0;  // Cache the sort key.

  RegUnitSet(std::string Name) : Name(Name) {}
};

// Base vector for identifying TopoSigs. The contents uniquely identify a
// TopoSig, only computeSuperRegs needs to know how.
typedef SmallVector<unsigned, 16> TopoSigId;

// CodeGenRegBank - Represent a target's registers and the relations between
// them.
class CodeGenRegBank {
  SetTheory Sets;

  const CodeGenHwModes &CGH;

  std::deque<CodeGenSubRegIndex> SubRegIndices;
  DenseMap<Record *, CodeGenSubRegIndex *> Def2SubRegIdx;

  CodeGenSubRegIndex *createSubRegIndex(StringRef Name, StringRef NameSpace);

  typedef std::map<SmallVector<CodeGenSubRegIndex *, 8>, CodeGenSubRegIndex *>
      ConcatIdxMap;
  ConcatIdxMap ConcatIdx;

  // Registers.
  std::deque<CodeGenRegister> Registers;
  StringMap<CodeGenRegister *> RegistersByName;
  DenseMap<Record *, CodeGenRegister *> Def2Reg;
  unsigned NumNativeRegUnits;

  std::map<TopoSigId, unsigned> TopoSigs;

  // Includes native (0..NumNativeRegUnits-1) and adopted register units.
  SmallVector<RegUnit, 8> RegUnits;

  // Register classes.
  std::list<CodeGenRegisterClass> RegClasses;
  DenseMap<Record *, CodeGenRegisterClass *> Def2RC;
  typedef std::map<CodeGenRegisterClass::Key, CodeGenRegisterClass *> RCKeyMap;
  RCKeyMap Key2RC;

  // Register categories.
  std::list<CodeGenRegisterCategory> RegCategories;
  DenseMap<Record *, CodeGenRegisterCategory *> Def2RCat;
  using RCatKeyMap =
      std::map<CodeGenRegisterClass::Key, CodeGenRegisterCategory *>;
  RCatKeyMap Key2RCat;

  // Remember each unique set of register units. Initially, this contains a
  // unique set for each register class. Simliar sets are coalesced with
  // pruneUnitSets and new supersets are inferred during computeRegUnitSets.
  std::vector<RegUnitSet> RegUnitSets;

  // Map RegisterClass index to the index of the RegUnitSet that contains the
  // class's units and any inferred RegUnit supersets.
  //
  // NOTE: This could grow beyond the number of register classes when we map
  // register units to lists of unit sets. If the list of unit sets does not
  // already exist for a register class, we create a new entry in this vector.
  std::vector<std::vector<unsigned>> RegClassUnitSets;

  // Give each register unit set an order based on sorting criteria.
  std::vector<unsigned> RegUnitSetOrder;

  // Keep track of synthesized definitions generated in TupleExpander.
  std::vector<std::unique_ptr<Record>> SynthDefs;

  // Add RC to *2RC maps.
  void addToMaps(CodeGenRegisterClass *);

  // Create a synthetic sub-class if it is missing.
  CodeGenRegisterClass *getOrCreateSubClass(const CodeGenRegisterClass *RC,
                                            const CodeGenRegister::Vec *Membs,
                                            StringRef Name);

  // Infer missing register classes.
  void computeInferredRegisterClasses();
  void inferCommonSubClass(CodeGenRegisterClass *RC);
  void inferSubClassWithSubReg(CodeGenRegisterClass *RC);

  void inferMatchingSuperRegClass(CodeGenRegisterClass *RC) {
    inferMatchingSuperRegClass(RC, RegClasses.begin());
  }

  void inferMatchingSuperRegClass(
      CodeGenRegisterClass *RC,
      std::list<CodeGenRegisterClass>::iterator FirstSubRegRC);

  // Iteratively prune unit sets.
  void pruneUnitSets();

  // Compute a weight for each register unit created during getSubRegs.
  void computeRegUnitWeights();

  // Create a RegUnitSet for each RegClass and infer superclasses.
  void computeRegUnitSets();

  // Populate the Composite map from sub-register relationships.
  void computeComposites();

  // Compute a lane mask for each sub-register index.
  void computeSubRegLaneMasks();

  /// Computes a lane mask for each register unit enumerated by a physical
  /// register.
  void computeRegUnitLaneMasks();

public:
  CodeGenRegBank(RecordKeeper &, const CodeGenHwModes &);
  CodeGenRegBank(CodeGenRegBank &) = delete;

  SetTheory &getSets() { return Sets; }

  const CodeGenHwModes &getHwModes() const { return CGH; }

  // Sub-register indices. The first NumNamedIndices are defined by the user
  // in the .td files. The rest are synthesized such that all sub-registers
  // have a unique name.
  const std::deque<CodeGenSubRegIndex> &getSubRegIndices() const {
    return SubRegIndices;
  }

  // Find a SubRegIndex from its Record def or add to the list if it does
  // not exist there yet.
  CodeGenSubRegIndex *getSubRegIdx(Record *);

  // Find a SubRegIndex from its Record def.
  const CodeGenSubRegIndex *findSubRegIdx(const Record *Def) const;

  // Find or create a sub-register index representing the A+B composition.
  CodeGenSubRegIndex *getCompositeSubRegIndex(CodeGenSubRegIndex *A,
                                              CodeGenSubRegIndex *B);

  // Find or create a sub-register index representing the concatenation of
  // non-overlapping sibling indices.
  CodeGenSubRegIndex *
  getConcatSubRegIndex(const SmallVector<CodeGenSubRegIndex *, 8> &,
                       const CodeGenHwModes &CGH);

  const std::deque<CodeGenRegister> &getRegisters() const { return Registers; }

  const StringMap<CodeGenRegister *> &getRegistersByName() const {
    return RegistersByName;
  }

  // Find a register from its Record def.
  CodeGenRegister *getReg(Record *);

  // Get a Register's index into the Registers array.
  unsigned getRegIndex(const CodeGenRegister *Reg) const {
    return Reg->EnumValue - 1;
  }

  // Return the number of allocated TopoSigs. The first TopoSig representing
  // leaf registers is allocated number 0.
  unsigned getNumTopoSigs() const { return TopoSigs.size(); }

  // Find or create a TopoSig for the given TopoSigId.
  // This function is only for use by CodeGenRegister::computeSuperRegs().
  // Others should simply use Reg->getTopoSig().
  unsigned getTopoSig(const TopoSigId &Id) {
    return TopoSigs.insert(std::pair(Id, TopoSigs.size())).first->second;
  }

  // Create a native register unit that is associated with one or two root
  // registers.
  unsigned newRegUnit(CodeGenRegister *R0, CodeGenRegister *R1 = nullptr) {
    RegUnit &RU = RegUnits.emplace_back();
    RU.Roots[0] = R0;
    RU.Roots[1] = R1;
    RU.Artificial = R0->Artificial;
    if (R1)
      RU.Artificial |= R1->Artificial;
    return RegUnits.size() - 1;
  }

  // Create a new non-native register unit that can be adopted by a register
  // to increase its pressure. Note that NumNativeRegUnits is not increased.
  unsigned newRegUnit(unsigned Weight) {
    RegUnit &RU = RegUnits.emplace_back();
    RU.Weight = Weight;
    return RegUnits.size() - 1;
  }

  // Native units are the singular unit of a leaf register. Register aliasing
  // is completely characterized by native units. Adopted units exist to give
  // register additional weight but don't affect aliasing.
  bool isNativeUnit(unsigned RUID) const { return RUID < NumNativeRegUnits; }

  unsigned getNumNativeRegUnits() const { return NumNativeRegUnits; }

  RegUnit &getRegUnit(unsigned RUID) { return RegUnits[RUID]; }
  const RegUnit &getRegUnit(unsigned RUID) const { return RegUnits[RUID]; }

  std::list<CodeGenRegisterClass> &getRegClasses() { return RegClasses; }

  const std::list<CodeGenRegisterClass> &getRegClasses() const {
    return RegClasses;
  }

  std::list<CodeGenRegisterCategory> &getRegCategories() {
    return RegCategories;
  }

  const std::list<CodeGenRegisterCategory> &getRegCategories() const {
    return RegCategories;
  }

  // Find a register class from its def.
  CodeGenRegisterClass *getRegClass(const Record *) const;

  /// getRegisterClassForRegister - Find the register class that contains the
  /// specified physical register.  If the register is not in a register
  /// class, return null. If the register is in multiple classes, and the
  /// classes have a superset-subset relationship and the same set of types,
  /// return the superclass.  Otherwise return null.
  const CodeGenRegisterClass *getRegClassForRegister(Record *R);

  // Analog of TargetRegisterInfo::getMinimalPhysRegClass. Unlike
  // getRegClassForRegister, this tries to find the smallest class containing
  // the physical register. If \p VT is specified, it will only find classes
  // with a matching type
  const CodeGenRegisterClass *
  getMinimalPhysRegClass(Record *RegRecord, ValueTypeByHwMode *VT = nullptr);

  // Get the sum of unit weights.
  unsigned getRegUnitSetWeight(const std::vector<unsigned> &Units) const {
    unsigned Weight = 0;
    for (unsigned Unit : Units)
      Weight += getRegUnit(Unit).Weight;
    return Weight;
  }

  unsigned getRegSetIDAt(unsigned Order) const {
    return RegUnitSetOrder[Order];
  }

  const RegUnitSet &getRegSetAt(unsigned Order) const {
    return RegUnitSets[RegUnitSetOrder[Order]];
  }

  // Increase a RegUnitWeight.
  void increaseRegUnitWeight(unsigned RUID, unsigned Inc) {
    getRegUnit(RUID).Weight += Inc;
  }

  // Get the number of register pressure dimensions.
  unsigned getNumRegPressureSets() const { return RegUnitSets.size(); }

  // Get a set of register unit IDs for a given dimension of pressure.
  const RegUnitSet &getRegPressureSet(unsigned Idx) const {
    return RegUnitSets[Idx];
  }

  // The number of pressure set lists may be larget than the number of
  // register classes if some register units appeared in a list of sets that
  // did not correspond to an existing register class.
  unsigned getNumRegClassPressureSetLists() const {
    return RegClassUnitSets.size();
  }

  // Get a list of pressure set IDs for a register class. Liveness of a
  // register in this class impacts each pressure set in this list by the
  // weight of the register. An exact solution requires all registers in a
  // class to have the same class, but it is not strictly guaranteed.
  ArrayRef<unsigned> getRCPressureSetIDs(unsigned RCIdx) const {
    return RegClassUnitSets[RCIdx];
  }

  // Computed derived records such as missing sub-register indices.
  void computeDerivedInfo();

  // Compute the set of registers completely covered by the registers in Regs.
  // The returned BitVector will have a bit set for each register in Regs,
  // all sub-registers, and all super-registers that are covered by the
  // registers in Regs.
  //
  // This is used to compute the mask of call-preserved registers from a list
  // of callee-saves.
  BitVector computeCoveredRegisters(ArrayRef<Record *> Regs);

  // Bit mask of lanes that cover their registers. A sub-register index whose
  // LaneMask is contained in CoveringLanes will be completely covered by
  // another sub-register with the same or larger lane mask.
  LaneBitmask CoveringLanes;

  // Helper function for printing debug information. Handles artificial
  // (non-native) reg units.
  void printRegUnitName(unsigned Unit) const;
};

} // end namespace llvm

#endif // LLVM_UTILS_TABLEGEN_CODEGENREGISTERS_H
