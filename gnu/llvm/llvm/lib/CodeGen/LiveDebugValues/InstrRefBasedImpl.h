//===- InstrRefBasedImpl.h - Tracking Debug Value MIs ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_LIVEDEBUGVALUES_INSTRREFBASEDLDV_H
#define LLVM_LIB_CODEGEN_LIVEDEBUGVALUES_INSTRREFBASEDLDV_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/UniqueVector.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <optional>

#include "LiveDebugValues.h"

class TransferTracker;

// Forward dec of unit test class, so that we can peer into the LDV object.
class InstrRefLDVTest;

namespace LiveDebugValues {

class MLocTracker;
class DbgOpIDMap;

using namespace llvm;

using DebugVariableID = unsigned;
using VarAndLoc = std::pair<DebugVariable, const DILocation *>;

/// Mapping from DebugVariable to/from a unique identifying number. Each
/// DebugVariable consists of three pointers, and after a small amount of
/// work to identify overlapping fragments of variables we mostly only use
/// DebugVariables as identities of variables. It's much more compile-time
/// efficient to use an ID number instead, which this class provides.
class DebugVariableMap {
  DenseMap<DebugVariable, unsigned> VarToIdx;
  SmallVector<VarAndLoc> IdxToVar;

public:
  DebugVariableID getDVID(const DebugVariable &Var) const {
    auto It = VarToIdx.find(Var);
    assert(It != VarToIdx.end());
    return It->second;
  }

  DebugVariableID insertDVID(DebugVariable &Var, const DILocation *Loc) {
    unsigned Size = VarToIdx.size();
    auto ItPair = VarToIdx.insert({Var, Size});
    if (ItPair.second) {
      IdxToVar.push_back({Var, Loc});
      return Size;
    }

    return ItPair.first->second;
  }

  const VarAndLoc &lookupDVID(DebugVariableID ID) const { return IdxToVar[ID]; }

  void clear() {
    VarToIdx.clear();
    IdxToVar.clear();
  }
};

/// Handle-class for a particular "location". This value-type uniquely
/// symbolises a register or stack location, allowing manipulation of locations
/// without concern for where that location is. Practically, this allows us to
/// treat the state of the machine at a particular point as an array of values,
/// rather than a map of values.
class LocIdx {
  unsigned Location;

  // Default constructor is private, initializing to an illegal location number.
  // Use only for "not an entry" elements in IndexedMaps.
  LocIdx() : Location(UINT_MAX) {}

public:
#define NUM_LOC_BITS 24
  LocIdx(unsigned L) : Location(L) {
    assert(L < (1 << NUM_LOC_BITS) && "Machine locations must fit in 24 bits");
  }

  static LocIdx MakeIllegalLoc() { return LocIdx(); }
  static LocIdx MakeTombstoneLoc() {
    LocIdx L = LocIdx();
    --L.Location;
    return L;
  }

  bool isIllegal() const { return Location == UINT_MAX; }

  uint64_t asU64() const { return Location; }

  bool operator==(unsigned L) const { return Location == L; }

  bool operator==(const LocIdx &L) const { return Location == L.Location; }

  bool operator!=(unsigned L) const { return !(*this == L); }

  bool operator!=(const LocIdx &L) const { return !(*this == L); }

  bool operator<(const LocIdx &Other) const {
    return Location < Other.Location;
  }
};

// The location at which a spilled value resides. It consists of a register and
// an offset.
struct SpillLoc {
  unsigned SpillBase;
  StackOffset SpillOffset;
  bool operator==(const SpillLoc &Other) const {
    return std::make_pair(SpillBase, SpillOffset) ==
           std::make_pair(Other.SpillBase, Other.SpillOffset);
  }
  bool operator<(const SpillLoc &Other) const {
    return std::make_tuple(SpillBase, SpillOffset.getFixed(),
                           SpillOffset.getScalable()) <
           std::make_tuple(Other.SpillBase, Other.SpillOffset.getFixed(),
                           Other.SpillOffset.getScalable());
  }
};

/// Unique identifier for a value defined by an instruction, as a value type.
/// Casts back and forth to a uint64_t. Probably replacable with something less
/// bit-constrained. Each value identifies the instruction and machine location
/// where the value is defined, although there may be no corresponding machine
/// operand for it (ex: regmasks clobbering values). The instructions are
/// one-based, and definitions that are PHIs have instruction number zero.
///
/// The obvious limits of a 1M block function or 1M instruction blocks are
/// problematic; but by that point we should probably have bailed out of
/// trying to analyse the function.
class ValueIDNum {
  union {
    struct {
      uint64_t BlockNo : 20; /// The block where the def happens.
      uint64_t InstNo : 20;  /// The Instruction where the def happens.
                             /// One based, is distance from start of block.
      uint64_t LocNo
          : NUM_LOC_BITS; /// The machine location where the def happens.
    } s;
    uint64_t Value;
  } u;

  static_assert(sizeof(u) == 8, "Badly packed ValueIDNum?");

public:
  // Default-initialize to EmptyValue. This is necessary to make IndexedMaps
  // of values to work.
  ValueIDNum() { u.Value = EmptyValue.asU64(); }

  ValueIDNum(uint64_t Block, uint64_t Inst, uint64_t Loc) {
    u.s = {Block, Inst, Loc};
  }

  ValueIDNum(uint64_t Block, uint64_t Inst, LocIdx Loc) {
    u.s = {Block, Inst, Loc.asU64()};
  }

  uint64_t getBlock() const { return u.s.BlockNo; }
  uint64_t getInst() const { return u.s.InstNo; }
  uint64_t getLoc() const { return u.s.LocNo; }
  bool isPHI() const { return u.s.InstNo == 0; }

  uint64_t asU64() const { return u.Value; }

  static ValueIDNum fromU64(uint64_t v) {
    ValueIDNum Val;
    Val.u.Value = v;
    return Val;
  }

  bool operator<(const ValueIDNum &Other) const {
    return asU64() < Other.asU64();
  }

  bool operator==(const ValueIDNum &Other) const {
    return u.Value == Other.u.Value;
  }

  bool operator!=(const ValueIDNum &Other) const { return !(*this == Other); }

  std::string asString(const std::string &mlocname) const {
    return Twine("Value{bb: ")
        .concat(Twine(u.s.BlockNo)
                    .concat(Twine(", inst: ")
                                .concat((u.s.InstNo ? Twine(u.s.InstNo)
                                                    : Twine("live-in"))
                                            .concat(Twine(", loc: ").concat(
                                                Twine(mlocname)))
                                            .concat(Twine("}")))))
        .str();
  }

  static ValueIDNum EmptyValue;
  static ValueIDNum TombstoneValue;
};

} // End namespace LiveDebugValues

namespace llvm {
using namespace LiveDebugValues;

template <> struct DenseMapInfo<LocIdx> {
  static inline LocIdx getEmptyKey() { return LocIdx::MakeIllegalLoc(); }
  static inline LocIdx getTombstoneKey() { return LocIdx::MakeTombstoneLoc(); }

  static unsigned getHashValue(const LocIdx &Loc) { return Loc.asU64(); }

  static bool isEqual(const LocIdx &A, const LocIdx &B) { return A == B; }
};

template <> struct DenseMapInfo<ValueIDNum> {
  static inline ValueIDNum getEmptyKey() { return ValueIDNum::EmptyValue; }
  static inline ValueIDNum getTombstoneKey() {
    return ValueIDNum::TombstoneValue;
  }

  static unsigned getHashValue(const ValueIDNum &Val) {
    return hash_value(Val.asU64());
  }

  static bool isEqual(const ValueIDNum &A, const ValueIDNum &B) {
    return A == B;
  }
};

} // end namespace llvm

namespace LiveDebugValues {
using namespace llvm;

/// Type for a table of values in a block.
using ValueTable = SmallVector<ValueIDNum, 0>;

/// A collection of ValueTables, one per BB in a function, with convenient
/// accessor methods.
struct FuncValueTable {
  FuncValueTable(int NumBBs, int NumLocs) {
    Storage.reserve(NumBBs);
    for (int i = 0; i != NumBBs; ++i)
      Storage.push_back(
          std::make_unique<ValueTable>(NumLocs, ValueIDNum::EmptyValue));
  }

  /// Returns the ValueTable associated with MBB.
  ValueTable &operator[](const MachineBasicBlock &MBB) const {
    return (*this)[MBB.getNumber()];
  }

  /// Returns the ValueTable associated with the MachineBasicBlock whose number
  /// is MBBNum.
  ValueTable &operator[](int MBBNum) const {
    auto &TablePtr = Storage[MBBNum];
    assert(TablePtr && "Trying to access a deleted table");
    return *TablePtr;
  }

  /// Returns the ValueTable associated with the entry MachineBasicBlock.
  ValueTable &tableForEntryMBB() const { return (*this)[0]; }

  /// Returns true if the ValueTable associated with MBB has not been freed.
  bool hasTableFor(MachineBasicBlock &MBB) const {
    return Storage[MBB.getNumber()] != nullptr;
  }

  /// Frees the memory of the ValueTable associated with MBB.
  void ejectTableForBlock(const MachineBasicBlock &MBB) {
    Storage[MBB.getNumber()].reset();
  }

private:
  /// ValueTables are stored as unique_ptrs to allow for deallocation during
  /// LDV; this was measured to have a significant impact on compiler memory
  /// usage.
  SmallVector<std::unique_ptr<ValueTable>, 0> Storage;
};

/// Thin wrapper around an integer -- designed to give more type safety to
/// spill location numbers.
class SpillLocationNo {
public:
  explicit SpillLocationNo(unsigned SpillNo) : SpillNo(SpillNo) {}
  unsigned SpillNo;
  unsigned id() const { return SpillNo; }

  bool operator<(const SpillLocationNo &Other) const {
    return SpillNo < Other.SpillNo;
  }

  bool operator==(const SpillLocationNo &Other) const {
    return SpillNo == Other.SpillNo;
  }
  bool operator!=(const SpillLocationNo &Other) const {
    return !(*this == Other);
  }
};

/// Meta qualifiers for a value. Pair of whatever expression is used to qualify
/// the value, and Boolean of whether or not it's indirect.
class DbgValueProperties {
public:
  DbgValueProperties(const DIExpression *DIExpr, bool Indirect, bool IsVariadic)
      : DIExpr(DIExpr), Indirect(Indirect), IsVariadic(IsVariadic) {}

  /// Extract properties from an existing DBG_VALUE instruction.
  DbgValueProperties(const MachineInstr &MI) {
    assert(MI.isDebugValue());
    assert(MI.getDebugExpression()->getNumLocationOperands() == 0 ||
           MI.isDebugValueList() || MI.isUndefDebugValue());
    IsVariadic = MI.isDebugValueList();
    DIExpr = MI.getDebugExpression();
    Indirect = MI.isDebugOffsetImm();
  }

  bool isJoinable(const DbgValueProperties &Other) const {
    return DIExpression::isEqualExpression(DIExpr, Indirect, Other.DIExpr,
                                           Other.Indirect);
  }

  bool operator==(const DbgValueProperties &Other) const {
    return std::tie(DIExpr, Indirect, IsVariadic) ==
           std::tie(Other.DIExpr, Other.Indirect, Other.IsVariadic);
  }

  bool operator!=(const DbgValueProperties &Other) const {
    return !(*this == Other);
  }

  unsigned getLocationOpCount() const {
    return IsVariadic ? DIExpr->getNumLocationOperands() : 1;
  }

  const DIExpression *DIExpr;
  bool Indirect;
  bool IsVariadic;
};

/// TODO: Might pack better if we changed this to a Struct of Arrays, since
/// MachineOperand is width 32, making this struct width 33. We could also
/// potentially avoid storing the whole MachineOperand (sizeof=32), instead
/// choosing to store just the contents portion (sizeof=8) and a Kind enum,
/// since we already know it is some type of immediate value.
/// Stores a single debug operand, which can either be a MachineOperand for
/// directly storing immediate values, or a ValueIDNum representing some value
/// computed at some point in the program. IsConst is used as a discriminator.
struct DbgOp {
  union {
    ValueIDNum ID;
    MachineOperand MO;
  };
  bool IsConst;

  DbgOp() : ID(ValueIDNum::EmptyValue), IsConst(false) {}
  DbgOp(ValueIDNum ID) : ID(ID), IsConst(false) {}
  DbgOp(MachineOperand MO) : MO(MO), IsConst(true) {}

  bool isUndef() const { return !IsConst && ID == ValueIDNum::EmptyValue; }

#ifndef NDEBUG
  void dump(const MLocTracker *MTrack) const;
#endif
};

/// A DbgOp whose ID (if any) has resolved to an actual location, LocIdx. Used
/// when working with concrete debug values, i.e. when joining MLocs and VLocs
/// in the TransferTracker or emitting DBG_VALUE/DBG_VALUE_LIST instructions in
/// the MLocTracker.
struct ResolvedDbgOp {
  union {
    LocIdx Loc;
    MachineOperand MO;
  };
  bool IsConst;

  ResolvedDbgOp(LocIdx Loc) : Loc(Loc), IsConst(false) {}
  ResolvedDbgOp(MachineOperand MO) : MO(MO), IsConst(true) {}

  bool operator==(const ResolvedDbgOp &Other) const {
    if (IsConst != Other.IsConst)
      return false;
    if (IsConst)
      return MO.isIdenticalTo(Other.MO);
    return Loc == Other.Loc;
  }

#ifndef NDEBUG
  void dump(const MLocTracker *MTrack) const;
#endif
};

/// An ID used in the DbgOpIDMap (below) to lookup a stored DbgOp. This is used
/// in place of actual DbgOps inside of a DbgValue to reduce its size, as
/// DbgValue is very frequently used and passed around, and the actual DbgOp is
/// over 8x larger than this class, due to storing a MachineOperand. This ID
/// should be equal for all equal DbgOps, and also encodes whether the mapped
/// DbgOp is a constant, meaning that for simple equality or const-ness checks
/// it is not necessary to lookup this ID.
struct DbgOpID {
  struct IsConstIndexPair {
    uint32_t IsConst : 1;
    uint32_t Index : 31;
  };

  union {
    struct IsConstIndexPair ID;
    uint32_t RawID;
  };

  DbgOpID() : RawID(UndefID.RawID) {
    static_assert(sizeof(DbgOpID) == 4, "DbgOpID should fit within 4 bytes.");
  }
  DbgOpID(uint32_t RawID) : RawID(RawID) {}
  DbgOpID(bool IsConst, uint32_t Index) : ID({IsConst, Index}) {}

  static DbgOpID UndefID;

  bool operator==(const DbgOpID &Other) const { return RawID == Other.RawID; }
  bool operator!=(const DbgOpID &Other) const { return !(*this == Other); }

  uint32_t asU32() const { return RawID; }

  bool isUndef() const { return *this == UndefID; }
  bool isConst() const { return ID.IsConst && !isUndef(); }
  uint32_t getIndex() const { return ID.Index; }

#ifndef NDEBUG
  void dump(const MLocTracker *MTrack, const DbgOpIDMap *OpStore) const;
#endif
};

/// Class storing the complete set of values that are observed by DbgValues
/// within the current function. Allows 2-way lookup, with `find` returning the
/// Op for a given ID and `insert` returning the ID for a given Op (creating one
/// if none exists).
class DbgOpIDMap {

  SmallVector<ValueIDNum, 0> ValueOps;
  SmallVector<MachineOperand, 0> ConstOps;

  DenseMap<ValueIDNum, DbgOpID> ValueOpToID;
  DenseMap<MachineOperand, DbgOpID> ConstOpToID;

public:
  /// If \p Op does not already exist in this map, it is inserted and the
  /// corresponding DbgOpID is returned. If Op already exists in this map, then
  /// no change is made and the existing ID for Op is returned.
  /// Calling this with the undef DbgOp will always return DbgOpID::UndefID.
  DbgOpID insert(DbgOp Op) {
    if (Op.isUndef())
      return DbgOpID::UndefID;
    if (Op.IsConst)
      return insertConstOp(Op.MO);
    return insertValueOp(Op.ID);
  }
  /// Returns the DbgOp associated with \p ID. Should only be used for IDs
  /// returned from calling `insert` from this map or DbgOpID::UndefID.
  DbgOp find(DbgOpID ID) const {
    if (ID == DbgOpID::UndefID)
      return DbgOp();
    if (ID.isConst())
      return DbgOp(ConstOps[ID.getIndex()]);
    return DbgOp(ValueOps[ID.getIndex()]);
  }

  void clear() {
    ValueOps.clear();
    ConstOps.clear();
    ValueOpToID.clear();
    ConstOpToID.clear();
  }

private:
  DbgOpID insertConstOp(MachineOperand &MO) {
    auto ExistingIt = ConstOpToID.find(MO);
    if (ExistingIt != ConstOpToID.end())
      return ExistingIt->second;
    DbgOpID ID(true, ConstOps.size());
    ConstOpToID.insert(std::make_pair(MO, ID));
    ConstOps.push_back(MO);
    return ID;
  }
  DbgOpID insertValueOp(ValueIDNum VID) {
    auto ExistingIt = ValueOpToID.find(VID);
    if (ExistingIt != ValueOpToID.end())
      return ExistingIt->second;
    DbgOpID ID(false, ValueOps.size());
    ValueOpToID.insert(std::make_pair(VID, ID));
    ValueOps.push_back(VID);
    return ID;
  }
};

// We set the maximum number of operands that we will handle to keep DbgValue
// within a reasonable size (64 bytes), as we store and pass a lot of them
// around.
#define MAX_DBG_OPS 8

/// Class recording the (high level) _value_ of a variable. Identifies the value
/// of the variable as a list of ValueIDNums and constant MachineOperands, or as
/// an empty list for undef debug values or VPHI values which we have not found
/// valid locations for.
/// This class also stores meta-information about how the value is qualified.
/// Used to reason about variable values when performing the second
/// (DebugVariable specific) dataflow analysis.
class DbgValue {
private:
  /// If Kind is Def or VPHI, the set of IDs corresponding to the DbgOps that
  /// are used. VPHIs set every ID to EmptyID when we have not found a valid
  /// machine-value for every operand, and sets them to the corresponding
  /// machine-values when we have found all of them.
  DbgOpID DbgOps[MAX_DBG_OPS];
  unsigned OpCount;

public:
  /// For a NoVal or VPHI DbgValue, which block it was generated in.
  int BlockNo;

  /// Qualifiers for the ValueIDNum above.
  DbgValueProperties Properties;

  typedef enum {
    Undef, // Represents a DBG_VALUE $noreg in the transfer function only.
    Def,   // This value is defined by some combination of constants,
           // instructions, or PHI values.
    VPHI,  // Incoming values to BlockNo differ, those values must be joined by
           // a PHI in this block.
    NoVal, // Empty DbgValue indicating an unknown value. Used as initializer,
           // before dominating blocks values are propagated in.
  } KindT;
  /// Discriminator for whether this is a constant or an in-program value.
  KindT Kind;

  DbgValue(ArrayRef<DbgOpID> DbgOps, const DbgValueProperties &Prop)
      : OpCount(DbgOps.size()), BlockNo(0), Properties(Prop), Kind(Def) {
    static_assert(sizeof(DbgValue) <= 64,
                  "DbgValue should fit within 64 bytes.");
    assert(DbgOps.size() == Prop.getLocationOpCount());
    if (DbgOps.size() > MAX_DBG_OPS ||
        any_of(DbgOps, [](DbgOpID ID) { return ID.isUndef(); })) {
      Kind = Undef;
      OpCount = 0;
#define DEBUG_TYPE "LiveDebugValues"
      if (DbgOps.size() > MAX_DBG_OPS) {
        LLVM_DEBUG(dbgs() << "Found DbgValue with more than maximum allowed "
                             "operands.\n");
      }
#undef DEBUG_TYPE
    } else {
      for (unsigned Idx = 0; Idx < DbgOps.size(); ++Idx)
        this->DbgOps[Idx] = DbgOps[Idx];
    }
  }

  DbgValue(unsigned BlockNo, const DbgValueProperties &Prop, KindT Kind)
      : OpCount(0), BlockNo(BlockNo), Properties(Prop), Kind(Kind) {
    assert(Kind == NoVal || Kind == VPHI);
  }

  DbgValue(const DbgValueProperties &Prop, KindT Kind)
      : OpCount(0), BlockNo(0), Properties(Prop), Kind(Kind) {
    assert(Kind == Undef &&
           "Empty DbgValue constructor must pass in Undef kind");
  }

#ifndef NDEBUG
  void dump(const MLocTracker *MTrack = nullptr,
            const DbgOpIDMap *OpStore = nullptr) const;
#endif

  bool operator==(const DbgValue &Other) const {
    if (std::tie(Kind, Properties) != std::tie(Other.Kind, Other.Properties))
      return false;
    else if (Kind == Def && !equal(getDbgOpIDs(), Other.getDbgOpIDs()))
      return false;
    else if (Kind == NoVal && BlockNo != Other.BlockNo)
      return false;
    else if (Kind == VPHI && BlockNo != Other.BlockNo)
      return false;
    else if (Kind == VPHI && !equal(getDbgOpIDs(), Other.getDbgOpIDs()))
      return false;

    return true;
  }

  bool operator!=(const DbgValue &Other) const { return !(*this == Other); }

  // Returns an array of all the machine values used to calculate this variable
  // value, or an empty list for an Undef or unjoined VPHI.
  ArrayRef<DbgOpID> getDbgOpIDs() const { return {DbgOps, OpCount}; }

  // Returns either DbgOps[Index] if this DbgValue has Debug Operands, or
  // the ID for ValueIDNum::EmptyValue otherwise (i.e. if this is an Undef,
  // NoVal, or an unjoined VPHI).
  DbgOpID getDbgOpID(unsigned Index) const {
    if (!OpCount)
      return DbgOpID::UndefID;
    assert(Index < OpCount);
    return DbgOps[Index];
  }
  // Replaces this DbgValue's existing DbgOpIDs (if any) with the contents of
  // \p NewIDs. The number of DbgOpIDs passed must be equal to the number of
  // arguments expected by this DbgValue's properties (the return value of
  // `getLocationOpCount()`).
  void setDbgOpIDs(ArrayRef<DbgOpID> NewIDs) {
    // We can go from no ops to some ops, but not from some ops to no ops.
    assert(NewIDs.size() == getLocationOpCount() &&
           "Incorrect number of Debug Operands for this DbgValue.");
    OpCount = NewIDs.size();
    for (unsigned Idx = 0; Idx < NewIDs.size(); ++Idx)
      DbgOps[Idx] = NewIDs[Idx];
  }

  // The number of debug operands expected by this DbgValue's expression.
  // getDbgOpIDs() should return an array of this length, unless this is an
  // Undef or an unjoined VPHI.
  unsigned getLocationOpCount() const {
    return Properties.getLocationOpCount();
  }

  // Returns true if this or Other are unjoined PHIs, which do not have defined
  // Loc Ops, or if the `n`th Loc Op for this has a different constness to the
  // `n`th Loc Op for Other.
  bool hasJoinableLocOps(const DbgValue &Other) const {
    if (isUnjoinedPHI() || Other.isUnjoinedPHI())
      return true;
    for (unsigned Idx = 0; Idx < getLocationOpCount(); ++Idx) {
      if (getDbgOpID(Idx).isConst() != Other.getDbgOpID(Idx).isConst())
        return false;
    }
    return true;
  }

  bool isUnjoinedPHI() const { return Kind == VPHI && OpCount == 0; }

  bool hasIdenticalValidLocOps(const DbgValue &Other) const {
    if (!OpCount)
      return false;
    return equal(getDbgOpIDs(), Other.getDbgOpIDs());
  }
};

class LocIdxToIndexFunctor {
public:
  using argument_type = LocIdx;
  unsigned operator()(const LocIdx &L) const { return L.asU64(); }
};

/// Tracker for what values are in machine locations. Listens to the Things
/// being Done by various instructions, and maintains a table of what machine
/// locations have what values (as defined by a ValueIDNum).
///
/// There are potentially a much larger number of machine locations on the
/// target machine than the actual working-set size of the function. On x86 for
/// example, we're extremely unlikely to want to track values through control
/// or debug registers. To avoid doing so, MLocTracker has several layers of
/// indirection going on, described below, to avoid unnecessarily tracking
/// any location.
///
/// Here's a sort of diagram of the indexes, read from the bottom up:
///
///           Size on stack   Offset on stack
///                 \              /
///          Stack Idx (Where in slot is this?)
///                         /
///                        /
/// Slot Num (%stack.0)   /
/// FrameIdx => SpillNum /
///              \      /
///           SpillID (int)   Register number (int)
///                      \       /
///                      LocationID => LocIdx
///                                |
///                       LocIdx => ValueIDNum
///
/// The aim here is that the LocIdx => ValueIDNum vector is just an array of
/// values in numbered locations, so that later analyses can ignore whether the
/// location is a register or otherwise. To map a register / spill location to
/// a LocIdx, you have to use the (sparse) LocationID => LocIdx map. And to
/// build a LocationID for a stack slot, you need to combine identifiers for
/// which stack slot it is and where within that slot is being described.
///
/// Register mask operands cause trouble by technically defining every register;
/// various hacks are used to avoid tracking registers that are never read and
/// only written by regmasks.
class MLocTracker {
public:
  MachineFunction &MF;
  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  const TargetLowering &TLI;

  /// IndexedMap type, mapping from LocIdx to ValueIDNum.
  using LocToValueType = IndexedMap<ValueIDNum, LocIdxToIndexFunctor>;

  /// Map of LocIdxes to the ValueIDNums that they store. This is tightly
  /// packed, entries only exist for locations that are being tracked.
  LocToValueType LocIdxToIDNum;

  /// "Map" of machine location IDs (i.e., raw register or spill number) to the
  /// LocIdx key / number for that location. There are always at least as many
  /// as the number of registers on the target -- if the value in the register
  /// is not being tracked, then the LocIdx value will be zero. New entries are
  /// appended if a new spill slot begins being tracked.
  /// This, and the corresponding reverse map persist for the analysis of the
  /// whole function, and is necessarying for decoding various vectors of
  /// values.
  std::vector<LocIdx> LocIDToLocIdx;

  /// Inverse map of LocIDToLocIdx.
  IndexedMap<unsigned, LocIdxToIndexFunctor> LocIdxToLocID;

  /// When clobbering register masks, we chose to not believe the machine model
  /// and don't clobber SP. Do the same for SP aliases, and for efficiency,
  /// keep a set of them here.
  SmallSet<Register, 8> SPAliases;

  /// Unique-ification of spill. Used to number them -- their LocID number is
  /// the index in SpillLocs minus one plus NumRegs.
  UniqueVector<SpillLoc> SpillLocs;

  // If we discover a new machine location, assign it an mphi with this
  // block number.
  unsigned CurBB = -1;

  /// Cached local copy of the number of registers the target has.
  unsigned NumRegs;

  /// Number of slot indexes the target has -- distinct segments of a stack
  /// slot that can take on the value of a subregister, when a super-register
  /// is written to the stack.
  unsigned NumSlotIdxes;

  /// Collection of register mask operands that have been observed. Second part
  /// of pair indicates the instruction that they happened in. Used to
  /// reconstruct where defs happened if we start tracking a location later
  /// on.
  SmallVector<std::pair<const MachineOperand *, unsigned>, 32> Masks;

  /// Pair for describing a position within a stack slot -- first the size in
  /// bits, then the offset.
  typedef std::pair<unsigned short, unsigned short> StackSlotPos;

  /// Map from a size/offset pair describing a position in a stack slot, to a
  /// numeric identifier for that position. Allows easier identification of
  /// individual positions.
  DenseMap<StackSlotPos, unsigned> StackSlotIdxes;

  /// Inverse of StackSlotIdxes.
  DenseMap<unsigned, StackSlotPos> StackIdxesToPos;

  /// Iterator for locations and the values they contain. Dereferencing
  /// produces a struct/pair containing the LocIdx key for this location,
  /// and a reference to the value currently stored. Simplifies the process
  /// of seeking a particular location.
  class MLocIterator {
    LocToValueType &ValueMap;
    LocIdx Idx;

  public:
    class value_type {
    public:
      value_type(LocIdx Idx, ValueIDNum &Value) : Idx(Idx), Value(Value) {}
      const LocIdx Idx;  /// Read-only index of this location.
      ValueIDNum &Value; /// Reference to the stored value at this location.
    };

    MLocIterator(LocToValueType &ValueMap, LocIdx Idx)
        : ValueMap(ValueMap), Idx(Idx) {}

    bool operator==(const MLocIterator &Other) const {
      assert(&ValueMap == &Other.ValueMap);
      return Idx == Other.Idx;
    }

    bool operator!=(const MLocIterator &Other) const {
      return !(*this == Other);
    }

    void operator++() { Idx = LocIdx(Idx.asU64() + 1); }

    value_type operator*() { return value_type(Idx, ValueMap[LocIdx(Idx)]); }
  };

  MLocTracker(MachineFunction &MF, const TargetInstrInfo &TII,
              const TargetRegisterInfo &TRI, const TargetLowering &TLI);

  /// Produce location ID number for a Register. Provides some small amount of
  /// type safety.
  /// \param Reg The register we're looking up.
  unsigned getLocID(Register Reg) { return Reg.id(); }

  /// Produce location ID number for a spill position.
  /// \param Spill The number of the spill we're fetching the location for.
  /// \param SpillSubReg Subregister within the spill we're addressing.
  unsigned getLocID(SpillLocationNo Spill, unsigned SpillSubReg) {
    unsigned short Size = TRI.getSubRegIdxSize(SpillSubReg);
    unsigned short Offs = TRI.getSubRegIdxOffset(SpillSubReg);
    return getLocID(Spill, {Size, Offs});
  }

  /// Produce location ID number for a spill position.
  /// \param Spill The number of the spill we're fetching the location for.
  /// \apram SpillIdx size/offset within the spill slot to be addressed.
  unsigned getLocID(SpillLocationNo Spill, StackSlotPos Idx) {
    unsigned SlotNo = Spill.id() - 1;
    SlotNo *= NumSlotIdxes;
    assert(StackSlotIdxes.contains(Idx));
    SlotNo += StackSlotIdxes[Idx];
    SlotNo += NumRegs;
    return SlotNo;
  }

  /// Given a spill number, and a slot within the spill, calculate the ID number
  /// for that location.
  unsigned getSpillIDWithIdx(SpillLocationNo Spill, unsigned Idx) {
    unsigned SlotNo = Spill.id() - 1;
    SlotNo *= NumSlotIdxes;
    SlotNo += Idx;
    SlotNo += NumRegs;
    return SlotNo;
  }

  /// Return the spill number that a location ID corresponds to.
  SpillLocationNo locIDToSpill(unsigned ID) const {
    assert(ID >= NumRegs);
    ID -= NumRegs;
    // Truncate away the index part, leaving only the spill number.
    ID /= NumSlotIdxes;
    return SpillLocationNo(ID + 1); // The UniqueVector is one-based.
  }

  /// Returns the spill-slot size/offs that a location ID corresponds to.
  StackSlotPos locIDToSpillIdx(unsigned ID) const {
    assert(ID >= NumRegs);
    ID -= NumRegs;
    unsigned Idx = ID % NumSlotIdxes;
    return StackIdxesToPos.find(Idx)->second;
  }

  unsigned getNumLocs() const { return LocIdxToIDNum.size(); }

  /// Reset all locations to contain a PHI value at the designated block. Used
  /// sometimes for actual PHI values, othertimes to indicate the block entry
  /// value (before any more information is known).
  void setMPhis(unsigned NewCurBB) {
    CurBB = NewCurBB;
    for (auto Location : locations())
      Location.Value = {CurBB, 0, Location.Idx};
  }

  /// Load values for each location from array of ValueIDNums. Take current
  /// bbnum just in case we read a value from a hitherto untouched register.
  void loadFromArray(ValueTable &Locs, unsigned NewCurBB) {
    CurBB = NewCurBB;
    // Iterate over all tracked locations, and load each locations live-in
    // value into our local index.
    for (auto Location : locations())
      Location.Value = Locs[Location.Idx.asU64()];
  }

  /// Wipe any un-necessary location records after traversing a block.
  void reset() {
    // We could reset all the location values too; however either loadFromArray
    // or setMPhis should be called before this object is re-used. Just
    // clear Masks, they're definitely not needed.
    Masks.clear();
  }

  /// Clear all data. Destroys the LocID <=> LocIdx map, which makes most of
  /// the information in this pass uninterpretable.
  void clear() {
    reset();
    LocIDToLocIdx.clear();
    LocIdxToLocID.clear();
    LocIdxToIDNum.clear();
    // SpillLocs.reset(); XXX UniqueVector::reset assumes a SpillLoc casts from
    // 0
    SpillLocs = decltype(SpillLocs)();
    StackSlotIdxes.clear();
    StackIdxesToPos.clear();

    LocIDToLocIdx.resize(NumRegs, LocIdx::MakeIllegalLoc());
  }

  /// Set a locaiton to a certain value.
  void setMLoc(LocIdx L, ValueIDNum Num) {
    assert(L.asU64() < LocIdxToIDNum.size());
    LocIdxToIDNum[L] = Num;
  }

  /// Read the value of a particular location
  ValueIDNum readMLoc(LocIdx L) {
    assert(L.asU64() < LocIdxToIDNum.size());
    return LocIdxToIDNum[L];
  }

  /// Create a LocIdx for an untracked register ID. Initialize it to either an
  /// mphi value representing a live-in, or a recent register mask clobber.
  LocIdx trackRegister(unsigned ID);

  LocIdx lookupOrTrackRegister(unsigned ID) {
    LocIdx &Index = LocIDToLocIdx[ID];
    if (Index.isIllegal())
      Index = trackRegister(ID);
    return Index;
  }

  /// Is register R currently tracked by MLocTracker?
  bool isRegisterTracked(Register R) {
    LocIdx &Index = LocIDToLocIdx[R];
    return !Index.isIllegal();
  }

  /// Record a definition of the specified register at the given block / inst.
  /// This doesn't take a ValueIDNum, because the definition and its location
  /// are synonymous.
  void defReg(Register R, unsigned BB, unsigned Inst) {
    unsigned ID = getLocID(R);
    LocIdx Idx = lookupOrTrackRegister(ID);
    ValueIDNum ValueID = {BB, Inst, Idx};
    LocIdxToIDNum[Idx] = ValueID;
  }

  /// Set a register to a value number. To be used if the value number is
  /// known in advance.
  void setReg(Register R, ValueIDNum ValueID) {
    unsigned ID = getLocID(R);
    LocIdx Idx = lookupOrTrackRegister(ID);
    LocIdxToIDNum[Idx] = ValueID;
  }

  ValueIDNum readReg(Register R) {
    unsigned ID = getLocID(R);
    LocIdx Idx = lookupOrTrackRegister(ID);
    return LocIdxToIDNum[Idx];
  }

  /// Reset a register value to zero / empty. Needed to replicate the
  /// VarLoc implementation where a copy to/from a register effectively
  /// clears the contents of the source register. (Values can only have one
  ///  machine location in VarLocBasedImpl).
  void wipeRegister(Register R) {
    unsigned ID = getLocID(R);
    LocIdx Idx = LocIDToLocIdx[ID];
    LocIdxToIDNum[Idx] = ValueIDNum::EmptyValue;
  }

  /// Determine the LocIdx of an existing register.
  LocIdx getRegMLoc(Register R) {
    unsigned ID = getLocID(R);
    assert(ID < LocIDToLocIdx.size());
    assert(LocIDToLocIdx[ID] != UINT_MAX); // Sentinel for IndexedMap.
    return LocIDToLocIdx[ID];
  }

  /// Record a RegMask operand being executed. Defs any register we currently
  /// track, stores a pointer to the mask in case we have to account for it
  /// later.
  void writeRegMask(const MachineOperand *MO, unsigned CurBB, unsigned InstID);

  /// Find LocIdx for SpillLoc \p L, creating a new one if it's not tracked.
  /// Returns std::nullopt when in scenarios where a spill slot could be
  /// tracked, but we would likely run into resource limitations.
  std::optional<SpillLocationNo> getOrTrackSpillLoc(SpillLoc L);

  // Get LocIdx of a spill ID.
  LocIdx getSpillMLoc(unsigned SpillID) {
    assert(LocIDToLocIdx[SpillID] != UINT_MAX); // Sentinel for IndexedMap.
    return LocIDToLocIdx[SpillID];
  }

  /// Return true if Idx is a spill machine location.
  bool isSpill(LocIdx Idx) const { return LocIdxToLocID[Idx] >= NumRegs; }

  /// How large is this location (aka, how wide is a value defined there?).
  unsigned getLocSizeInBits(LocIdx L) const {
    unsigned ID = LocIdxToLocID[L];
    if (!isSpill(L)) {
      return TRI.getRegSizeInBits(Register(ID), MF.getRegInfo());
    } else {
      // The slot location on the stack is uninteresting, we care about the
      // position of the value within the slot (which comes with a size).
      StackSlotPos Pos = locIDToSpillIdx(ID);
      return Pos.first;
    }
  }

  MLocIterator begin() { return MLocIterator(LocIdxToIDNum, 0); }

  MLocIterator end() {
    return MLocIterator(LocIdxToIDNum, LocIdxToIDNum.size());
  }

  /// Return a range over all locations currently tracked.
  iterator_range<MLocIterator> locations() {
    return llvm::make_range(begin(), end());
  }

  std::string LocIdxToName(LocIdx Idx) const;

  std::string IDAsString(const ValueIDNum &Num) const;

#ifndef NDEBUG
  LLVM_DUMP_METHOD void dump();

  LLVM_DUMP_METHOD void dump_mloc_map();
#endif

  /// Create a DBG_VALUE based on debug operands \p DbgOps. Qualify it with the
  /// information in \pProperties, for variable Var. Don't insert it anywhere,
  /// just return the builder for it.
  MachineInstrBuilder emitLoc(const SmallVectorImpl<ResolvedDbgOp> &DbgOps,
                              const DebugVariable &Var, const DILocation *DILoc,
                              const DbgValueProperties &Properties);
};

/// Types for recording sets of variable fragments that overlap. For a given
/// local variable, we record all other fragments of that variable that could
/// overlap it, to reduce search time.
using FragmentOfVar =
    std::pair<const DILocalVariable *, DIExpression::FragmentInfo>;
using OverlapMap =
    DenseMap<FragmentOfVar, SmallVector<DIExpression::FragmentInfo, 1>>;

/// Collection of DBG_VALUEs observed when traversing a block. Records each
/// variable and the value the DBG_VALUE refers to. Requires the machine value
/// location dataflow algorithm to have run already, so that values can be
/// identified.
class VLocTracker {
public:
  /// Ref to function-wide map of DebugVariable <=> ID-numbers.
  DebugVariableMap &DVMap;
  /// Map DebugVariable to the latest Value it's defined to have.
  /// Needs to be a MapVector because we determine order-in-the-input-MIR from
  /// the order in this container. (FIXME: likely no longer true as the ordering
  /// is now provided by DebugVariableMap).
  /// We only retain the last DbgValue in each block for each variable, to
  /// determine the blocks live-out variable value. The Vars container forms the
  /// transfer function for this block, as part of the dataflow analysis. The
  /// movement of values between locations inside of a block is handled at a
  /// much later stage, in the TransferTracker class.
  MapVector<DebugVariableID, DbgValue> Vars;
  SmallDenseMap<DebugVariableID, const DILocation *, 8> Scopes;
  MachineBasicBlock *MBB = nullptr;
  const OverlapMap &OverlappingFragments;
  DbgValueProperties EmptyProperties;

public:
  VLocTracker(DebugVariableMap &DVMap, const OverlapMap &O,
              const DIExpression *EmptyExpr)
      : DVMap(DVMap), OverlappingFragments(O),
        EmptyProperties(EmptyExpr, false, false) {}

  void defVar(const MachineInstr &MI, const DbgValueProperties &Properties,
              const SmallVectorImpl<DbgOpID> &DebugOps) {
    assert(MI.isDebugValueLike());
    DebugVariable Var(MI.getDebugVariable(), MI.getDebugExpression(),
                      MI.getDebugLoc()->getInlinedAt());
    // Either insert or fetch an ID number for this variable.
    DebugVariableID VarID = DVMap.insertDVID(Var, MI.getDebugLoc().get());
    DbgValue Rec = (DebugOps.size() > 0)
                       ? DbgValue(DebugOps, Properties)
                       : DbgValue(Properties, DbgValue::Undef);

    // Attempt insertion; overwrite if it's already mapped.
    auto Result = Vars.insert(std::make_pair(VarID, Rec));
    if (!Result.second)
      Result.first->second = Rec;
    Scopes[VarID] = MI.getDebugLoc().get();

    considerOverlaps(Var, MI.getDebugLoc().get());
  }

  void considerOverlaps(const DebugVariable &Var, const DILocation *Loc) {
    auto Overlaps = OverlappingFragments.find(
        {Var.getVariable(), Var.getFragmentOrDefault()});
    if (Overlaps == OverlappingFragments.end())
      return;

    // Otherwise: terminate any overlapped variable locations.
    for (auto FragmentInfo : Overlaps->second) {
      // The "empty" fragment is stored as DebugVariable::DefaultFragment, so
      // that it overlaps with everything, however its cannonical representation
      // in a DebugVariable is as "None".
      std::optional<DIExpression::FragmentInfo> OptFragmentInfo = FragmentInfo;
      if (DebugVariable::isDefaultFragment(FragmentInfo))
        OptFragmentInfo = std::nullopt;

      DebugVariable Overlapped(Var.getVariable(), OptFragmentInfo,
                               Var.getInlinedAt());
      // Produce an ID number for this overlapping fragment of a variable.
      DebugVariableID OverlappedID = DVMap.insertDVID(Overlapped, Loc);
      DbgValue Rec = DbgValue(EmptyProperties, DbgValue::Undef);

      // Attempt insertion; overwrite if it's already mapped.
      auto Result = Vars.insert(std::make_pair(OverlappedID, Rec));
      if (!Result.second)
        Result.first->second = Rec;
      Scopes[OverlappedID] = Loc;
    }
  }

  void clear() {
    Vars.clear();
    Scopes.clear();
  }
};

// XXX XXX docs
class InstrRefBasedLDV : public LDVImpl {
public:
  friend class ::InstrRefLDVTest;

  using FragmentInfo = DIExpression::FragmentInfo;
  using OptFragmentInfo = std::optional<DIExpression::FragmentInfo>;

  // Helper while building OverlapMap, a map of all fragments seen for a given
  // DILocalVariable.
  using VarToFragments =
      DenseMap<const DILocalVariable *, SmallSet<FragmentInfo, 4>>;

  /// Machine location/value transfer function, a mapping of which locations
  /// are assigned which new values.
  using MLocTransferMap = SmallDenseMap<LocIdx, ValueIDNum>;

  /// Live in/out structure for the variable values: a per-block map of
  /// variables to their values.
  using LiveIdxT = DenseMap<const MachineBasicBlock *, DbgValue *>;

  using VarAndLoc = std::pair<DebugVariableID, DbgValue>;

  /// Type for a live-in value: the predecessor block, and its value.
  using InValueT = std::pair<MachineBasicBlock *, DbgValue *>;

  /// Vector (per block) of a collection (inner smallvector) of live-ins.
  /// Used as the result type for the variable value dataflow problem.
  using LiveInsT = SmallVector<SmallVector<VarAndLoc, 8>, 8>;

  /// Mapping from lexical scopes to a DILocation in that scope.
  using ScopeToDILocT = DenseMap<const LexicalScope *, const DILocation *>;

  /// Mapping from lexical scopes to variables in that scope.
  using ScopeToVarsT =
      DenseMap<const LexicalScope *, SmallSet<DebugVariableID, 4>>;

  /// Mapping from lexical scopes to blocks where variables in that scope are
  /// assigned. Such blocks aren't necessarily "in" the lexical scope, it's
  /// just a block where an assignment happens.
  using ScopeToAssignBlocksT = DenseMap<const LexicalScope *, SmallPtrSet<MachineBasicBlock *, 4>>;

private:
  MachineDominatorTree *DomTree;
  const TargetRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  const TargetInstrInfo *TII;
  const TargetFrameLowering *TFI;
  const MachineFrameInfo *MFI;
  BitVector CalleeSavedRegs;
  LexicalScopes LS;
  TargetPassConfig *TPC;

  // An empty DIExpression. Used default / placeholder DbgValueProperties
  // objects, as we can't have null expressions.
  const DIExpression *EmptyExpr;

  /// Object to track machine locations as we step through a block. Could
  /// probably be a field rather than a pointer, as it's always used.
  MLocTracker *MTracker = nullptr;

  /// Number of the current block LiveDebugValues is stepping through.
  unsigned CurBB = -1;

  /// Number of the current instruction LiveDebugValues is evaluating.
  unsigned CurInst;

  /// Variable tracker -- listens to DBG_VALUEs occurring as InstrRefBasedImpl
  /// steps through a block. Reads the values at each location from the
  /// MLocTracker object.
  VLocTracker *VTracker = nullptr;

  /// Tracker for transfers, listens to DBG_VALUEs and transfers of values
  /// between locations during stepping, creates new DBG_VALUEs when values move
  /// location.
  TransferTracker *TTracker = nullptr;

  /// Blocks which are artificial, i.e. blocks which exclusively contain
  /// instructions without DebugLocs, or with line 0 locations.
  SmallPtrSet<MachineBasicBlock *, 16> ArtificialBlocks;

  // Mapping of blocks to and from their RPOT order.
  SmallVector<MachineBasicBlock *> OrderToBB;
  DenseMap<const MachineBasicBlock *, unsigned int> BBToOrder;
  DenseMap<unsigned, unsigned> BBNumToRPO;

  /// Pair of MachineInstr, and its 1-based offset into the containing block.
  using InstAndNum = std::pair<const MachineInstr *, unsigned>;
  /// Map from debug instruction number to the MachineInstr labelled with that
  /// number, and its location within the function. Used to transform
  /// instruction numbers in DBG_INSTR_REFs into machine value numbers.
  std::map<uint64_t, InstAndNum> DebugInstrNumToInstr;

  /// Record of where we observed a DBG_PHI instruction.
  class DebugPHIRecord {
  public:
    /// Instruction number of this DBG_PHI.
    uint64_t InstrNum;
    /// Block where DBG_PHI occurred.
    MachineBasicBlock *MBB;
    /// The value number read by the DBG_PHI -- or std::nullopt if it didn't
    /// refer to a value.
    std::optional<ValueIDNum> ValueRead;
    /// Register/Stack location the DBG_PHI reads -- or std::nullopt if it
    /// referred to something unexpected.
    std::optional<LocIdx> ReadLoc;

    operator unsigned() const { return InstrNum; }
  };

  /// Map from instruction numbers defined by DBG_PHIs to a record of what that
  /// DBG_PHI read and where. Populated and edited during the machine value
  /// location problem -- we use LLVMs SSA Updater to fix changes by
  /// optimizations that destroy PHI instructions.
  SmallVector<DebugPHIRecord, 32> DebugPHINumToValue;

  // Map of overlapping variable fragments.
  OverlapMap OverlapFragments;
  VarToFragments SeenFragments;

  /// Mapping of DBG_INSTR_REF instructions to their values, for those
  /// DBG_INSTR_REFs that call resolveDbgPHIs. These variable references solve
  /// a mini SSA problem caused by DBG_PHIs being cloned, this collection caches
  /// the result.
  DenseMap<std::pair<MachineInstr *, unsigned>, std::optional<ValueIDNum>>
      SeenDbgPHIs;

  DbgOpIDMap DbgOpStore;

  /// Mapping between DebugVariables and unique ID numbers. This is a more
  /// efficient way to represent the identity of a variable, versus a plain
  /// DebugVariable.
  DebugVariableMap DVMap;

  /// True if we need to examine call instructions for stack clobbers. We
  /// normally assume that they don't clobber SP, but stack probes on Windows
  /// do.
  bool AdjustsStackInCalls = false;

  /// If AdjustsStackInCalls is true, this holds the name of the target's stack
  /// probe function, which is the function we expect will alter the stack
  /// pointer.
  StringRef StackProbeSymbolName;

  /// Tests whether this instruction is a spill to a stack slot.
  std::optional<SpillLocationNo> isSpillInstruction(const MachineInstr &MI,
                                                    MachineFunction *MF);

  /// Decide if @MI is a spill instruction and return true if it is. We use 2
  /// criteria to make this decision:
  /// - Is this instruction a store to a spill slot?
  /// - Is there a register operand that is both used and killed?
  /// TODO: Store optimization can fold spills into other stores (including
  /// other spills). We do not handle this yet (more than one memory operand).
  bool isLocationSpill(const MachineInstr &MI, MachineFunction *MF,
                       unsigned &Reg);

  /// If a given instruction is identified as a spill, return the spill slot
  /// and set \p Reg to the spilled register.
  std::optional<SpillLocationNo> isRestoreInstruction(const MachineInstr &MI,
                                                      MachineFunction *MF,
                                                      unsigned &Reg);

  /// Given a spill instruction, extract the spill slot information, ensure it's
  /// tracked, and return the spill number.
  std::optional<SpillLocationNo>
  extractSpillBaseRegAndOffset(const MachineInstr &MI);

  /// For an instruction reference given by \p InstNo and \p OpNo in instruction
  /// \p MI returns the Value pointed to by that instruction reference if any
  /// exists, otherwise returns std::nullopt.
  std::optional<ValueIDNum> getValueForInstrRef(unsigned InstNo, unsigned OpNo,
                                                MachineInstr &MI,
                                                const FuncValueTable *MLiveOuts,
                                                const FuncValueTable *MLiveIns);

  /// Observe a single instruction while stepping through a block.
  void process(MachineInstr &MI, const FuncValueTable *MLiveOuts,
               const FuncValueTable *MLiveIns);

  /// Examines whether \p MI is a DBG_VALUE and notifies trackers.
  /// \returns true if MI was recognized and processed.
  bool transferDebugValue(const MachineInstr &MI);

  /// Examines whether \p MI is a DBG_INSTR_REF and notifies trackers.
  /// \returns true if MI was recognized and processed.
  bool transferDebugInstrRef(MachineInstr &MI, const FuncValueTable *MLiveOuts,
                             const FuncValueTable *MLiveIns);

  /// Stores value-information about where this PHI occurred, and what
  /// instruction number is associated with it.
  /// \returns true if MI was recognized and processed.
  bool transferDebugPHI(MachineInstr &MI);

  /// Examines whether \p MI is copy instruction, and notifies trackers.
  /// \returns true if MI was recognized and processed.
  bool transferRegisterCopy(MachineInstr &MI);

  /// Examines whether \p MI is stack spill or restore  instruction, and
  /// notifies trackers. \returns true if MI was recognized and processed.
  bool transferSpillOrRestoreInst(MachineInstr &MI);

  /// Examines \p MI for any registers that it defines, and notifies trackers.
  void transferRegisterDef(MachineInstr &MI);

  /// Copy one location to the other, accounting for movement of subregisters
  /// too.
  void performCopy(Register Src, Register Dst);

  void accumulateFragmentMap(MachineInstr &MI);

  /// Determine the machine value number referred to by (potentially several)
  /// DBG_PHI instructions. Block duplication and tail folding can duplicate
  /// DBG_PHIs, shifting the position where values in registers merge, and
  /// forming another mini-ssa problem to solve.
  /// \p Here the position of a DBG_INSTR_REF seeking a machine value number
  /// \p InstrNum Debug instruction number defined by DBG_PHI instructions.
  /// \returns The machine value number at position Here, or std::nullopt.
  std::optional<ValueIDNum> resolveDbgPHIs(MachineFunction &MF,
                                           const FuncValueTable &MLiveOuts,
                                           const FuncValueTable &MLiveIns,
                                           MachineInstr &Here,
                                           uint64_t InstrNum);

  std::optional<ValueIDNum> resolveDbgPHIsImpl(MachineFunction &MF,
                                               const FuncValueTable &MLiveOuts,
                                               const FuncValueTable &MLiveIns,
                                               MachineInstr &Here,
                                               uint64_t InstrNum);

  /// Step through the function, recording register definitions and movements
  /// in an MLocTracker. Convert the observations into a per-block transfer
  /// function in \p MLocTransfer, suitable for using with the machine value
  /// location dataflow problem.
  void
  produceMLocTransferFunction(MachineFunction &MF,
                              SmallVectorImpl<MLocTransferMap> &MLocTransfer,
                              unsigned MaxNumBlocks);

  /// Solve the machine value location dataflow problem. Takes as input the
  /// transfer functions in \p MLocTransfer. Writes the output live-in and
  /// live-out arrays to the (initialized to zero) multidimensional arrays in
  /// \p MInLocs and \p MOutLocs. The outer dimension is indexed by block
  /// number, the inner by LocIdx.
  void buildMLocValueMap(MachineFunction &MF, FuncValueTable &MInLocs,
                         FuncValueTable &MOutLocs,
                         SmallVectorImpl<MLocTransferMap> &MLocTransfer);

  /// Examine the stack indexes (i.e. offsets within the stack) to find the
  /// basic units of interference -- like reg units, but for the stack.
  void findStackIndexInterference(SmallVectorImpl<unsigned> &Slots);

  /// Install PHI values into the live-in array for each block, according to
  /// the IDF of each register.
  void placeMLocPHIs(MachineFunction &MF,
                     SmallPtrSetImpl<MachineBasicBlock *> &AllBlocks,
                     FuncValueTable &MInLocs,
                     SmallVectorImpl<MLocTransferMap> &MLocTransfer);

  /// Propagate variable values to blocks in the common case where there's
  /// only one value assigned to the variable. This function has better
  /// performance as it doesn't have to find the dominance frontier between
  /// different assignments.
  void placePHIsForSingleVarDefinition(
      const SmallPtrSetImpl<MachineBasicBlock *> &InScopeBlocks,
      MachineBasicBlock *MBB, SmallVectorImpl<VLocTracker> &AllTheVLocs,
      DebugVariableID Var, LiveInsT &Output);

  /// Calculate the iterated-dominance-frontier for a set of defs, using the
  /// existing LLVM facilities for this. Works for a single "value" or
  /// machine/variable location.
  /// \p AllBlocks Set of blocks where we might consume the value.
  /// \p DefBlocks Set of blocks where the value/location is defined.
  /// \p PHIBlocks Output set of blocks where PHIs must be placed.
  void BlockPHIPlacement(const SmallPtrSetImpl<MachineBasicBlock *> &AllBlocks,
                         const SmallPtrSetImpl<MachineBasicBlock *> &DefBlocks,
                         SmallVectorImpl<MachineBasicBlock *> &PHIBlocks);

  /// Perform a control flow join (lattice value meet) of the values in machine
  /// locations at \p MBB. Follows the algorithm described in the file-comment,
  /// reading live-outs of predecessors from \p OutLocs, the current live ins
  /// from \p InLocs, and assigning the newly computed live ins back into
  /// \p InLocs. \returns two bools -- the first indicates whether a change
  /// was made, the second whether a lattice downgrade occurred. If the latter
  /// is true, revisiting this block is necessary.
  bool mlocJoin(MachineBasicBlock &MBB,
                SmallPtrSet<const MachineBasicBlock *, 16> &Visited,
                FuncValueTable &OutLocs, ValueTable &InLocs);

  /// Produce a set of blocks that are in the current lexical scope. This means
  /// those blocks that contain instructions "in" the scope, blocks where
  /// assignments to variables in scope occur, and artificial blocks that are
  /// successors to any of the earlier blocks. See https://llvm.org/PR48091 for
  /// more commentry on what "in scope" means.
  /// \p DILoc A location in the scope that we're fetching blocks for.
  /// \p Output Set to put in-scope-blocks into.
  /// \p AssignBlocks Blocks known to contain assignments of variables in scope.
  void
  getBlocksForScope(const DILocation *DILoc,
                    SmallPtrSetImpl<const MachineBasicBlock *> &Output,
                    const SmallPtrSetImpl<MachineBasicBlock *> &AssignBlocks);

  /// Solve the variable value dataflow problem, for a single lexical scope.
  /// Uses the algorithm from the file comment to resolve control flow joins
  /// using PHI placement and value propagation. Reads the locations of machine
  /// values from the \p MInLocs and \p MOutLocs arrays (see buildMLocValueMap)
  /// and reads the variable values transfer function from \p AllTheVlocs.
  /// Live-in and Live-out variable values are stored locally, with the live-ins
  /// permanently stored to \p Output once a fixedpoint is reached.
  /// \p VarsWeCareAbout contains a collection of the variables in \p Scope
  /// that we should be tracking.
  /// \p AssignBlocks contains the set of blocks that aren't in \p DILoc's
  /// scope, but which do contain DBG_VALUEs, which VarLocBasedImpl tracks
  /// locations through.
  void buildVLocValueMap(const DILocation *DILoc,
                         const SmallSet<DebugVariableID, 4> &VarsWeCareAbout,
                         SmallPtrSetImpl<MachineBasicBlock *> &AssignBlocks,
                         LiveInsT &Output, FuncValueTable &MOutLocs,
                         FuncValueTable &MInLocs,
                         SmallVectorImpl<VLocTracker> &AllTheVLocs);

  /// Attempt to eliminate un-necessary PHIs on entry to a block. Examines the
  /// live-in values coming from predecessors live-outs, and replaces any PHIs
  /// already present in this blocks live-ins with a live-through value if the
  /// PHI isn't needed.
  /// \p LiveIn Old live-in value, overwritten with new one if live-in changes.
  /// \returns true if any live-ins change value, either from value propagation
  ///          or PHI elimination.
  bool vlocJoin(MachineBasicBlock &MBB, LiveIdxT &VLOCOutLocs,
                SmallPtrSet<const MachineBasicBlock *, 8> &BlocksToExplore,
                DbgValue &LiveIn);

  /// For the given block and live-outs feeding into it, try to find
  /// machine locations for each debug operand where all the values feeding
  /// into that operand join together.
  /// \returns true if a joined location was found for every value that needed
  ///          to be joined.
  bool
  pickVPHILoc(SmallVectorImpl<DbgOpID> &OutValues, const MachineBasicBlock &MBB,
              const LiveIdxT &LiveOuts, FuncValueTable &MOutLocs,
              const SmallVectorImpl<const MachineBasicBlock *> &BlockOrders);

  std::optional<ValueIDNum> pickOperandPHILoc(
      unsigned DbgOpIdx, const MachineBasicBlock &MBB, const LiveIdxT &LiveOuts,
      FuncValueTable &MOutLocs,
      const SmallVectorImpl<const MachineBasicBlock *> &BlockOrders);

  /// Take collections of DBG_VALUE instructions stored in TTracker, and
  /// install them into their output blocks.
  bool emitTransfers();

  /// Boilerplate computation of some initial sets, artifical blocks and
  /// RPOT block ordering.
  void initialSetup(MachineFunction &MF);

  /// Produce a map of the last lexical scope that uses a block, using the
  /// scopes DFSOut number. Mapping is block-number to DFSOut.
  /// \p EjectionMap Pre-allocated vector in which to install the built ma.
  /// \p ScopeToDILocation Mapping of LexicalScopes to their DILocations.
  /// \p AssignBlocks Map of blocks where assignments happen for a scope.
  void makeDepthFirstEjectionMap(SmallVectorImpl<unsigned> &EjectionMap,
                                 const ScopeToDILocT &ScopeToDILocation,
                                 ScopeToAssignBlocksT &AssignBlocks);

  /// When determining per-block variable values and emitting to DBG_VALUEs,
  /// this function explores by lexical scope depth. Doing so means that per
  /// block information can be fully computed before exploration finishes,
  /// allowing us to emit it and free data structures earlier than otherwise.
  /// It's also good for locality.
  bool depthFirstVLocAndEmit(unsigned MaxNumBlocks,
                             const ScopeToDILocT &ScopeToDILocation,
                             const ScopeToVarsT &ScopeToVars,
                             ScopeToAssignBlocksT &ScopeToBlocks,
                             LiveInsT &Output, FuncValueTable &MOutLocs,
                             FuncValueTable &MInLocs,
                             SmallVectorImpl<VLocTracker> &AllTheVLocs,
                             MachineFunction &MF, const TargetPassConfig &TPC);

  bool ExtendRanges(MachineFunction &MF, MachineDominatorTree *DomTree,
                    TargetPassConfig *TPC, unsigned InputBBLimit,
                    unsigned InputDbgValLimit) override;

public:
  /// Default construct and initialize the pass.
  InstrRefBasedLDV();

  LLVM_DUMP_METHOD
  void dump_mloc_transfer(const MLocTransferMap &mloc_transfer) const;

  bool isCalleeSaved(LocIdx L) const;
  bool isCalleeSavedReg(Register R) const;

  bool hasFoldedStackStore(const MachineInstr &MI) {
    // Instruction must have a memory operand that's a stack slot, and isn't
    // aliased, meaning it's a spill from regalloc instead of a variable.
    // If it's aliased, we can't guarantee its value.
    if (!MI.hasOneMemOperand())
      return false;
    auto *MemOperand = *MI.memoperands_begin();
    return MemOperand->isStore() &&
           MemOperand->getPseudoValue() &&
           MemOperand->getPseudoValue()->kind() == PseudoSourceValue::FixedStack
           && !MemOperand->getPseudoValue()->isAliased(MFI);
  }

  std::optional<LocIdx> findLocationForMemOperand(const MachineInstr &MI);

  // Utility for unit testing, don't use directly.
  DebugVariableMap &getDVMap() {
    return DVMap;
  }
};

} // namespace LiveDebugValues

#endif /* LLVM_LIB_CODEGEN_LIVEDEBUGVALUES_INSTRREFBASEDLDV_H */
