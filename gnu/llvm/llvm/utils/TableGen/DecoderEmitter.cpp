//===---------------- DecoderEmitter.cpp - Decoder Generator --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// It contains the tablegen backend that emits the decoder functions for
// targets with fixed/variable length instruction set.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenHwModes.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "Common/InfoByHwMode.h"
#include "Common/VarLenCodeEmitterGen.h"
#include "TableGenBackends.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "decoder-emitter"

extern cl::OptionCategory DisassemblerEmitterCat;

enum SuppressLevel {
  SUPPRESSION_DISABLE,
  SUPPRESSION_LEVEL1,
  SUPPRESSION_LEVEL2
};

cl::opt<SuppressLevel> DecoderEmitterSuppressDuplicates(
    "suppress-per-hwmode-duplicates",
    cl::desc("Suppress duplication of instrs into per-HwMode decoder tables"),
    cl::values(
        clEnumValN(
            SUPPRESSION_DISABLE, "O0",
            "Do not prevent DecoderTable duplications caused by HwModes"),
        clEnumValN(
            SUPPRESSION_LEVEL1, "O1",
            "Remove duplicate DecoderTable entries generated due to HwModes"),
        clEnumValN(
            SUPPRESSION_LEVEL2, "O2",
            "Extract HwModes-specific instructions into new DecoderTables, "
            "significantly reducing Table Duplications")),
    cl::init(SUPPRESSION_DISABLE), cl::cat(DisassemblerEmitterCat));

namespace {

STATISTIC(NumEncodings, "Number of encodings considered");
STATISTIC(NumEncodingsLackingDisasm,
          "Number of encodings without disassembler info");
STATISTIC(NumInstructions, "Number of instructions considered");
STATISTIC(NumEncodingsSupported, "Number of encodings supported");
STATISTIC(NumEncodingsOmitted, "Number of encodings omitted");

struct EncodingField {
  unsigned Base, Width, Offset;
  EncodingField(unsigned B, unsigned W, unsigned O)
      : Base(B), Width(W), Offset(O) {}
};

struct OperandInfo {
  std::vector<EncodingField> Fields;
  std::string Decoder;
  bool HasCompleteDecoder;
  uint64_t InitValue;

  OperandInfo(std::string D, bool HCD)
      : Decoder(std::move(D)), HasCompleteDecoder(HCD), InitValue(0) {}

  void addField(unsigned Base, unsigned Width, unsigned Offset) {
    Fields.push_back(EncodingField(Base, Width, Offset));
  }

  unsigned numFields() const { return Fields.size(); }

  typedef std::vector<EncodingField>::const_iterator const_iterator;

  const_iterator begin() const { return Fields.begin(); }
  const_iterator end() const { return Fields.end(); }
};

typedef std::vector<uint8_t> DecoderTable;
typedef uint32_t DecoderFixup;
typedef std::vector<DecoderFixup> FixupList;
typedef std::vector<FixupList> FixupScopeList;
typedef SmallSetVector<CachedHashString, 16> PredicateSet;
typedef SmallSetVector<CachedHashString, 16> DecoderSet;
struct DecoderTableInfo {
  DecoderTable Table;
  FixupScopeList FixupStack;
  PredicateSet Predicates;
  DecoderSet Decoders;
};

struct EncodingAndInst {
  const Record *EncodingDef;
  const CodeGenInstruction *Inst;
  StringRef HwModeName;

  EncodingAndInst(const Record *EncodingDef, const CodeGenInstruction *Inst,
                  StringRef HwModeName = "")
      : EncodingDef(EncodingDef), Inst(Inst), HwModeName(HwModeName) {}
};

struct EncodingIDAndOpcode {
  unsigned EncodingID;
  unsigned Opcode;

  EncodingIDAndOpcode() : EncodingID(0), Opcode(0) {}
  EncodingIDAndOpcode(unsigned EncodingID, unsigned Opcode)
      : EncodingID(EncodingID), Opcode(Opcode) {}
};

using EncodingIDsVec = std::vector<EncodingIDAndOpcode>;
using NamespacesHwModesMap = std::map<std::string, std::set<StringRef>>;

raw_ostream &operator<<(raw_ostream &OS, const EncodingAndInst &Value) {
  if (Value.EncodingDef != Value.Inst->TheDef)
    OS << Value.EncodingDef->getName() << ":";
  OS << Value.Inst->TheDef->getName();
  return OS;
}

class DecoderEmitter {
  RecordKeeper &RK;
  std::vector<EncodingAndInst> NumberedEncodings;

public:
  DecoderEmitter(RecordKeeper &R, std::string PredicateNamespace)
      : RK(R), Target(R), PredicateNamespace(std::move(PredicateNamespace)) {}

  // Emit the decoder state machine table.
  void emitTable(formatted_raw_ostream &o, DecoderTable &Table,
                 unsigned Indentation, unsigned BitWidth, StringRef Namespace,
                 const EncodingIDsVec &EncodingIDs) const;
  void emitInstrLenTable(formatted_raw_ostream &OS,
                         std::vector<unsigned> &InstrLen) const;
  void emitPredicateFunction(formatted_raw_ostream &OS,
                             PredicateSet &Predicates,
                             unsigned Indentation) const;
  void emitDecoderFunction(formatted_raw_ostream &OS, DecoderSet &Decoders,
                           unsigned Indentation) const;

  // run - Output the code emitter
  void run(raw_ostream &o);

private:
  CodeGenTarget Target;

public:
  std::string PredicateNamespace;
};

} // end anonymous namespace

// The set (BIT_TRUE, BIT_FALSE, BIT_UNSET) represents a ternary logic system
// for a bit value.
//
// BIT_UNFILTERED is used as the init value for a filter position.  It is used
// only for filter processings.
typedef enum {
  BIT_TRUE,      // '1'
  BIT_FALSE,     // '0'
  BIT_UNSET,     // '?'
  BIT_UNFILTERED // unfiltered
} bit_value_t;

static bool ValueSet(bit_value_t V) {
  return (V == BIT_TRUE || V == BIT_FALSE);
}

static bool ValueNotSet(bit_value_t V) { return (V == BIT_UNSET); }

static int Value(bit_value_t V) {
  return ValueNotSet(V) ? -1 : (V == BIT_FALSE ? 0 : 1);
}

static bit_value_t bitFromBits(const BitsInit &bits, unsigned index) {
  if (BitInit *bit = dyn_cast<BitInit>(bits.getBit(index)))
    return bit->getValue() ? BIT_TRUE : BIT_FALSE;

  // The bit is uninitialized.
  return BIT_UNSET;
}

// Prints the bit value for each position.
static void dumpBits(raw_ostream &o, const BitsInit &bits) {
  for (unsigned index = bits.getNumBits(); index > 0; --index) {
    switch (bitFromBits(bits, index - 1)) {
    case BIT_TRUE:
      o << "1";
      break;
    case BIT_FALSE:
      o << "0";
      break;
    case BIT_UNSET:
      o << "_";
      break;
    default:
      llvm_unreachable("unexpected return value from bitFromBits");
    }
  }
}

static BitsInit &getBitsField(const Record &def, StringRef str) {
  const RecordVal *RV = def.getValue(str);
  if (BitsInit *Bits = dyn_cast<BitsInit>(RV->getValue()))
    return *Bits;

  // variable length instruction
  VarLenInst VLI = VarLenInst(cast<DagInit>(RV->getValue()), RV);
  SmallVector<Init *, 16> Bits;

  for (const auto &SI : VLI) {
    if (const BitsInit *BI = dyn_cast<BitsInit>(SI.Value)) {
      for (unsigned Idx = 0U; Idx < BI->getNumBits(); ++Idx) {
        Bits.push_back(BI->getBit(Idx));
      }
    } else if (const BitInit *BI = dyn_cast<BitInit>(SI.Value)) {
      Bits.push_back(const_cast<BitInit *>(BI));
    } else {
      for (unsigned Idx = 0U; Idx < SI.BitWidth; ++Idx)
        Bits.push_back(UnsetInit::get(def.getRecords()));
    }
  }

  return *BitsInit::get(def.getRecords(), Bits);
}

// Representation of the instruction to work on.
typedef std::vector<bit_value_t> insn_t;

namespace {

static const uint64_t NO_FIXED_SEGMENTS_SENTINEL = -1ULL;

class FilterChooser;

/// Filter - Filter works with FilterChooser to produce the decoding tree for
/// the ISA.
///
/// It is useful to think of a Filter as governing the switch stmts of the
/// decoding tree in a certain level.  Each case stmt delegates to an inferior
/// FilterChooser to decide what further decoding logic to employ, or in another
/// words, what other remaining bits to look at.  The FilterChooser eventually
/// chooses a best Filter to do its job.
///
/// This recursive scheme ends when the number of Opcodes assigned to the
/// FilterChooser becomes 1 or if there is a conflict.  A conflict happens when
/// the Filter/FilterChooser combo does not know how to distinguish among the
/// Opcodes assigned.
///
/// An example of a conflict is
///
/// Conflict:
///                     111101000.00........00010000....
///                     111101000.00........0001........
///                     1111010...00........0001........
///                     1111010...00....................
///                     1111010.........................
///                     1111............................
///                     ................................
///     VST4q8a         111101000_00________00010000____
///     VST4q8b         111101000_00________00010000____
///
/// The Debug output shows the path that the decoding tree follows to reach the
/// the conclusion that there is a conflict.  VST4q8a is a vst4 to double-spaced
/// even registers, while VST4q8b is a vst4 to double-spaced odd registers.
///
/// The encoding info in the .td files does not specify this meta information,
/// which could have been used by the decoder to resolve the conflict.  The
/// decoder could try to decode the even/odd register numbering and assign to
/// VST4q8a or VST4q8b, but for the time being, the decoder chooses the "a"
/// version and return the Opcode since the two have the same Asm format string.
class Filter {
protected:
  const FilterChooser
      *Owner;        // points to the FilterChooser who owns this filter
  unsigned StartBit; // the starting bit position
  unsigned NumBits;  // number of bits to filter
  bool Mixed;        // a mixed region contains both set and unset bits

  // Map of well-known segment value to the set of uid's with that value.
  std::map<uint64_t, std::vector<EncodingIDAndOpcode>> FilteredInstructions;

  // Set of uid's with non-constant segment values.
  std::vector<EncodingIDAndOpcode> VariableInstructions;

  // Map of well-known segment value to its delegate.
  std::map<uint64_t, std::unique_ptr<const FilterChooser>> FilterChooserMap;

  // Number of instructions which fall under FilteredInstructions category.
  unsigned NumFiltered;

  // Keeps track of the last opcode in the filtered bucket.
  EncodingIDAndOpcode LastOpcFiltered;

public:
  Filter(Filter &&f);
  Filter(FilterChooser &owner, unsigned startBit, unsigned numBits, bool mixed);

  ~Filter() = default;

  unsigned getNumFiltered() const { return NumFiltered; }

  EncodingIDAndOpcode getSingletonOpc() const {
    assert(NumFiltered == 1);
    return LastOpcFiltered;
  }

  // Return the filter chooser for the group of instructions without constant
  // segment values.
  const FilterChooser &getVariableFC() const {
    assert(NumFiltered == 1);
    assert(FilterChooserMap.size() == 1);
    return *(FilterChooserMap.find(NO_FIXED_SEGMENTS_SENTINEL)->second);
  }

  // Divides the decoding task into sub tasks and delegates them to the
  // inferior FilterChooser's.
  //
  // A special case arises when there's only one entry in the filtered
  // instructions.  In order to unambiguously decode the singleton, we need to
  // match the remaining undecoded encoding bits against the singleton.
  void recurse();

  // Emit table entries to decode instructions given a segment or segments of
  // bits.
  void emitTableEntry(DecoderTableInfo &TableInfo) const;

  // Returns the number of fanout produced by the filter.  More fanout implies
  // the filter distinguishes more categories of instructions.
  unsigned usefulness() const;
}; // end class Filter

} // end anonymous namespace

// These are states of our finite state machines used in FilterChooser's
// filterProcessor() which produces the filter candidates to use.
typedef enum {
  ATTR_NONE,
  ATTR_FILTERED,
  ATTR_ALL_SET,
  ATTR_ALL_UNSET,
  ATTR_MIXED
} bitAttr_t;

/// FilterChooser - FilterChooser chooses the best filter among a set of Filters
/// in order to perform the decoding of instructions at the current level.
///
/// Decoding proceeds from the top down.  Based on the well-known encoding bits
/// of instructions available, FilterChooser builds up the possible Filters that
/// can further the task of decoding by distinguishing among the remaining
/// candidate instructions.
///
/// Once a filter has been chosen, it is called upon to divide the decoding task
/// into sub-tasks and delegates them to its inferior FilterChoosers for further
/// processings.
///
/// It is useful to think of a Filter as governing the switch stmts of the
/// decoding tree.  And each case is delegated to an inferior FilterChooser to
/// decide what further remaining bits to look at.
namespace {

class FilterChooser {
protected:
  friend class Filter;

  // Vector of codegen instructions to choose our filter.
  ArrayRef<EncodingAndInst> AllInstructions;

  // Vector of uid's for this filter chooser to work on.
  // The first member of the pair is the opcode id being decoded, the second is
  // the opcode id that should be emitted.
  const std::vector<EncodingIDAndOpcode> &Opcodes;

  // Lookup table for the operand decoding of instructions.
  const std::map<unsigned, std::vector<OperandInfo>> &Operands;

  // Vector of candidate filters.
  std::vector<Filter> Filters;

  // Array of bit values passed down from our parent.
  // Set to all BIT_UNFILTERED's for Parent == NULL.
  std::vector<bit_value_t> FilterBitValues;

  // Links to the FilterChooser above us in the decoding tree.
  const FilterChooser *Parent;

  // Index of the best filter from Filters.
  int BestIndex;

  // Width of instructions
  unsigned BitWidth;

  // Parent emitter
  const DecoderEmitter *Emitter;

public:
  FilterChooser(ArrayRef<EncodingAndInst> Insts,
                const std::vector<EncodingIDAndOpcode> &IDs,
                const std::map<unsigned, std::vector<OperandInfo>> &Ops,
                unsigned BW, const DecoderEmitter *E)
      : AllInstructions(Insts), Opcodes(IDs), Operands(Ops),
        FilterBitValues(BW, BIT_UNFILTERED), Parent(nullptr), BestIndex(-1),
        BitWidth(BW), Emitter(E) {
    doFilter();
  }

  FilterChooser(ArrayRef<EncodingAndInst> Insts,
                const std::vector<EncodingIDAndOpcode> &IDs,
                const std::map<unsigned, std::vector<OperandInfo>> &Ops,
                const std::vector<bit_value_t> &ParentFilterBitValues,
                const FilterChooser &parent)
      : AllInstructions(Insts), Opcodes(IDs), Operands(Ops),
        FilterBitValues(ParentFilterBitValues), Parent(&parent), BestIndex(-1),
        BitWidth(parent.BitWidth), Emitter(parent.Emitter) {
    doFilter();
  }

  FilterChooser(const FilterChooser &) = delete;
  void operator=(const FilterChooser &) = delete;

  unsigned getBitWidth() const { return BitWidth; }

protected:
  // Populates the insn given the uid.
  void insnWithID(insn_t &Insn, unsigned Opcode) const {
    const Record *EncodingDef = AllInstructions[Opcode].EncodingDef;
    BitsInit &Bits = getBitsField(*EncodingDef, "Inst");
    Insn.resize(std::max(BitWidth, Bits.getNumBits()), BIT_UNSET);
    // We may have a SoftFail bitmask, which specifies a mask where an encoding
    // may differ from the value in "Inst" and yet still be valid, but the
    // disassembler should return SoftFail instead of Success.
    //
    // This is used for marking UNPREDICTABLE instructions in the ARM world.
    const RecordVal *RV = EncodingDef->getValue("SoftFail");
    const BitsInit *SFBits = RV ? dyn_cast<BitsInit>(RV->getValue()) : nullptr;
    for (unsigned i = 0; i < Bits.getNumBits(); ++i) {
      if (SFBits && bitFromBits(*SFBits, i) == BIT_TRUE)
        Insn[i] = BIT_UNSET;
      else
        Insn[i] = bitFromBits(Bits, i);
    }
  }

  // Emit the name of the encoding/instruction pair.
  void emitNameWithID(raw_ostream &OS, unsigned Opcode) const {
    const Record *EncodingDef = AllInstructions[Opcode].EncodingDef;
    const Record *InstDef = AllInstructions[Opcode].Inst->TheDef;
    if (EncodingDef != InstDef)
      OS << EncodingDef->getName() << ":";
    OS << InstDef->getName();
  }

  // Populates the field of the insn given the start position and the number of
  // consecutive bits to scan for.
  //
  // Returns a pair of values (indicator, field), where the indicator is false
  // if there exists any uninitialized bit value in the range and true if all
  // bits are well-known. The second value is the potentially populated field.
  std::pair<bool, uint64_t> fieldFromInsn(const insn_t &Insn, unsigned StartBit,
                                          unsigned NumBits) const;

  /// dumpFilterArray - dumpFilterArray prints out debugging info for the given
  /// filter array as a series of chars.
  void dumpFilterArray(raw_ostream &o,
                       const std::vector<bit_value_t> &filter) const;

  /// dumpStack - dumpStack traverses the filter chooser chain and calls
  /// dumpFilterArray on each filter chooser up to the top level one.
  void dumpStack(raw_ostream &o, const char *prefix) const;

  Filter &bestFilter() {
    assert(BestIndex != -1 && "BestIndex not set");
    return Filters[BestIndex];
  }

  bool PositionFiltered(unsigned i) const {
    return ValueSet(FilterBitValues[i]);
  }

  // Calculates the island(s) needed to decode the instruction.
  // This returns a lit of undecoded bits of an instructions, for example,
  // Inst{20} = 1 && Inst{3-0} == 0b1111 represents two islands of yet-to-be
  // decoded bits in order to verify that the instruction matches the Opcode.
  unsigned getIslands(std::vector<unsigned> &StartBits,
                      std::vector<unsigned> &EndBits,
                      std::vector<uint64_t> &FieldVals,
                      const insn_t &Insn) const;

  // Emits code to check the Predicates member of an instruction are true.
  // Returns true if predicate matches were emitted, false otherwise.
  bool emitPredicateMatch(raw_ostream &o, unsigned &Indentation,
                          unsigned Opc) const;
  bool emitPredicateMatchAux(const Init &Val, bool ParenIfBinOp,
                             raw_ostream &OS) const;

  bool doesOpcodeNeedPredicate(unsigned Opc) const;
  unsigned getPredicateIndex(DecoderTableInfo &TableInfo, StringRef P) const;
  void emitPredicateTableEntry(DecoderTableInfo &TableInfo, unsigned Opc) const;

  void emitSoftFailTableEntry(DecoderTableInfo &TableInfo, unsigned Opc) const;

  // Emits table entries to decode the singleton.
  void emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                               EncodingIDAndOpcode Opc) const;

  // Emits code to decode the singleton, and then to decode the rest.
  void emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                               const Filter &Best) const;

  void emitBinaryParser(raw_ostream &o, unsigned &Indentation,
                        const OperandInfo &OpInfo,
                        bool &OpHasCompleteDecoder) const;

  void emitDecoder(raw_ostream &OS, unsigned Indentation, unsigned Opc,
                   bool &HasCompleteDecoder) const;
  unsigned getDecoderIndex(DecoderSet &Decoders, unsigned Opc,
                           bool &HasCompleteDecoder) const;

  // Assign a single filter and run with it.
  void runSingleFilter(unsigned startBit, unsigned numBit, bool mixed);

  // reportRegion is a helper function for filterProcessor to mark a region as
  // eligible for use as a filter region.
  void reportRegion(bitAttr_t RA, unsigned StartBit, unsigned BitIndex,
                    bool AllowMixed);

  // FilterProcessor scans the well-known encoding bits of the instructions and
  // builds up a list of candidate filters.  It chooses the best filter and
  // recursively descends down the decoding tree.
  bool filterProcessor(bool AllowMixed, bool Greedy = true);

  // Decides on the best configuration of filter(s) to use in order to decode
  // the instructions.  A conflict of instructions may occur, in which case we
  // dump the conflict set to the standard error.
  void doFilter();

public:
  // emitTableEntries - Emit state machine entries to decode our share of
  // instructions.
  void emitTableEntries(DecoderTableInfo &TableInfo) const;
};

} // end anonymous namespace

///////////////////////////
//                       //
// Filter Implementation //
//                       //
///////////////////////////

Filter::Filter(Filter &&f)
    : Owner(f.Owner), StartBit(f.StartBit), NumBits(f.NumBits), Mixed(f.Mixed),
      FilteredInstructions(std::move(f.FilteredInstructions)),
      VariableInstructions(std::move(f.VariableInstructions)),
      FilterChooserMap(std::move(f.FilterChooserMap)),
      NumFiltered(f.NumFiltered), LastOpcFiltered(f.LastOpcFiltered) {}

Filter::Filter(FilterChooser &owner, unsigned startBit, unsigned numBits,
               bool mixed)
    : Owner(&owner), StartBit(startBit), NumBits(numBits), Mixed(mixed) {
  assert(StartBit + NumBits - 1 < Owner->BitWidth);

  NumFiltered = 0;
  LastOpcFiltered = {0, 0};

  for (const auto &OpcPair : Owner->Opcodes) {
    insn_t Insn;

    // Populates the insn given the uid.
    Owner->insnWithID(Insn, OpcPair.EncodingID);

    // Scans the segment for possibly well-specified encoding bits.
    auto [Ok, Field] = Owner->fieldFromInsn(Insn, StartBit, NumBits);

    if (Ok) {
      // The encoding bits are well-known.  Lets add the uid of the
      // instruction into the bucket keyed off the constant field value.
      LastOpcFiltered = OpcPair;
      FilteredInstructions[Field].push_back(LastOpcFiltered);
      ++NumFiltered;
    } else {
      // Some of the encoding bit(s) are unspecified.  This contributes to
      // one additional member of "Variable" instructions.
      VariableInstructions.push_back(OpcPair);
    }
  }

  assert((FilteredInstructions.size() + VariableInstructions.size() > 0) &&
         "Filter returns no instruction categories");
}

// Divides the decoding task into sub tasks and delegates them to the
// inferior FilterChooser's.
//
// A special case arises when there's only one entry in the filtered
// instructions.  In order to unambiguously decode the singleton, we need to
// match the remaining undecoded encoding bits against the singleton.
void Filter::recurse() {
  // Starts by inheriting our parent filter chooser's filter bit values.
  std::vector<bit_value_t> BitValueArray(Owner->FilterBitValues);

  if (!VariableInstructions.empty()) {
    // Conservatively marks each segment position as BIT_UNSET.
    for (unsigned bitIndex = 0; bitIndex < NumBits; ++bitIndex)
      BitValueArray[StartBit + bitIndex] = BIT_UNSET;

    // Delegates to an inferior filter chooser for further processing on this
    // group of instructions whose segment values are variable.
    FilterChooserMap.insert(std::pair(
        NO_FIXED_SEGMENTS_SENTINEL,
        std::make_unique<FilterChooser>(Owner->AllInstructions,
                                        VariableInstructions, Owner->Operands,
                                        BitValueArray, *Owner)));
  }

  // No need to recurse for a singleton filtered instruction.
  // See also Filter::emit*().
  if (getNumFiltered() == 1) {
    assert(FilterChooserMap.size() == 1);
    return;
  }

  // Otherwise, create sub choosers.
  for (const auto &Inst : FilteredInstructions) {

    // Marks all the segment positions with either BIT_TRUE or BIT_FALSE.
    for (unsigned bitIndex = 0; bitIndex < NumBits; ++bitIndex) {
      if (Inst.first & (1ULL << bitIndex))
        BitValueArray[StartBit + bitIndex] = BIT_TRUE;
      else
        BitValueArray[StartBit + bitIndex] = BIT_FALSE;
    }

    // Delegates to an inferior filter chooser for further processing on this
    // category of instructions.
    FilterChooserMap.insert(
        std::pair(Inst.first, std::make_unique<FilterChooser>(
                                  Owner->AllInstructions, Inst.second,
                                  Owner->Operands, BitValueArray, *Owner)));
  }
}

static void resolveTableFixups(DecoderTable &Table, const FixupList &Fixups,
                               uint32_t DestIdx) {
  // Any NumToSkip fixups in the current scope can resolve to the
  // current location.
  for (FixupList::const_reverse_iterator I = Fixups.rbegin(), E = Fixups.rend();
       I != E; ++I) {
    // Calculate the distance from the byte following the fixup entry byte
    // to the destination. The Target is calculated from after the 16-bit
    // NumToSkip entry itself, so subtract two  from the displacement here
    // to account for that.
    uint32_t FixupIdx = *I;
    uint32_t Delta = DestIdx - FixupIdx - 3;
    // Our NumToSkip entries are 24-bits. Make sure our table isn't too
    // big.
    assert(Delta < (1u << 24));
    Table[FixupIdx] = (uint8_t)Delta;
    Table[FixupIdx + 1] = (uint8_t)(Delta >> 8);
    Table[FixupIdx + 2] = (uint8_t)(Delta >> 16);
  }
}

// Emit table entries to decode instructions given a segment or segments
// of bits.
void Filter::emitTableEntry(DecoderTableInfo &TableInfo) const {
  assert((NumBits < (1u << 8)) && "NumBits overflowed uint8 table entry!");
  TableInfo.Table.push_back(MCD::OPC_ExtractField);

  SmallString<16> SBytes;
  raw_svector_ostream S(SBytes);
  encodeULEB128(StartBit, S);
  TableInfo.Table.insert(TableInfo.Table.end(), SBytes.begin(), SBytes.end());
  TableInfo.Table.push_back(NumBits);

  // A new filter entry begins a new scope for fixup resolution.
  TableInfo.FixupStack.emplace_back();

  DecoderTable &Table = TableInfo.Table;

  size_t PrevFilter = 0;
  bool HasFallthrough = false;
  for (const auto &Filter : FilterChooserMap) {
    // Field value -1 implies a non-empty set of variable instructions.
    // See also recurse().
    if (Filter.first == NO_FIXED_SEGMENTS_SENTINEL) {
      HasFallthrough = true;

      // Each scope should always have at least one filter value to check
      // for.
      assert(PrevFilter != 0 && "empty filter set!");
      FixupList &CurScope = TableInfo.FixupStack.back();
      // Resolve any NumToSkip fixups in the current scope.
      resolveTableFixups(Table, CurScope, Table.size());
      CurScope.clear();
      PrevFilter = 0; // Don't re-process the filter's fallthrough.
    } else {
      Table.push_back(MCD::OPC_FilterValue);
      // Encode and emit the value to filter against.
      uint8_t Buffer[16];
      unsigned Len = encodeULEB128(Filter.first, Buffer);
      Table.insert(Table.end(), Buffer, Buffer + Len);
      // Reserve space for the NumToSkip entry. We'll backpatch the value
      // later.
      PrevFilter = Table.size();
      Table.push_back(0);
      Table.push_back(0);
      Table.push_back(0);
    }

    // We arrive at a category of instructions with the same segment value.
    // Now delegate to the sub filter chooser for further decodings.
    // The case may fallthrough, which happens if the remaining well-known
    // encoding bits do not match exactly.
    Filter.second->emitTableEntries(TableInfo);

    // Now that we've emitted the body of the handler, update the NumToSkip
    // of the filter itself to be able to skip forward when false. Subtract
    // two as to account for the width of the NumToSkip field itself.
    if (PrevFilter) {
      uint32_t NumToSkip = Table.size() - PrevFilter - 3;
      assert(NumToSkip < (1u << 24) &&
             "disassembler decoding table too large!");
      Table[PrevFilter] = (uint8_t)NumToSkip;
      Table[PrevFilter + 1] = (uint8_t)(NumToSkip >> 8);
      Table[PrevFilter + 2] = (uint8_t)(NumToSkip >> 16);
    }
  }

  // Any remaining unresolved fixups bubble up to the parent fixup scope.
  assert(TableInfo.FixupStack.size() > 1 && "fixup stack underflow!");
  FixupScopeList::iterator Source = TableInfo.FixupStack.end() - 1;
  FixupScopeList::iterator Dest = Source - 1;
  llvm::append_range(*Dest, *Source);
  TableInfo.FixupStack.pop_back();

  // If there is no fallthrough, then the final filter should get fixed
  // up according to the enclosing scope rather than the current position.
  if (!HasFallthrough)
    TableInfo.FixupStack.back().push_back(PrevFilter);
}

// Returns the number of fanout produced by the filter.  More fanout implies
// the filter distinguishes more categories of instructions.
unsigned Filter::usefulness() const {
  if (!VariableInstructions.empty())
    return FilteredInstructions.size();
  else
    return FilteredInstructions.size() + 1;
}

//////////////////////////////////
//                              //
// Filterchooser Implementation //
//                              //
//////////////////////////////////

// Emit the decoder state machine table.
void DecoderEmitter::emitTable(formatted_raw_ostream &OS, DecoderTable &Table,
                               unsigned Indentation, unsigned BitWidth,
                               StringRef Namespace,
                               const EncodingIDsVec &EncodingIDs) const {
  // We'll need to be able to map from a decoded opcode into the corresponding
  // EncodingID for this specific combination of BitWidth and Namespace. This
  // is used below to index into NumberedEncodings.
  DenseMap<unsigned, unsigned> OpcodeToEncodingID;
  OpcodeToEncodingID.reserve(EncodingIDs.size());
  for (const auto &EI : EncodingIDs)
    OpcodeToEncodingID[EI.Opcode] = EI.EncodingID;

  OS.indent(Indentation) << "static const uint8_t DecoderTable" << Namespace
                         << BitWidth << "[] = {\n";

  Indentation += 2;

  // Emit ULEB128 encoded value to OS, returning the number of bytes emitted.
  auto emitULEB128 = [](DecoderTable::const_iterator I,
                        formatted_raw_ostream &OS) {
    unsigned Len = 0;
    while (*I >= 128) {
      OS << (unsigned)*I++ << ", ";
      Len++;
    }
    OS << (unsigned)*I++ << ", ";
    return Len + 1;
  };

  // Emit 24-bit numtoskip value to OS, returning the NumToSkip value.
  auto emitNumToSkip = [](DecoderTable::const_iterator I,
                          formatted_raw_ostream &OS) {
    uint8_t Byte = *I++;
    uint32_t NumToSkip = Byte;
    OS << (unsigned)Byte << ", ";
    Byte = *I++;
    OS << (unsigned)Byte << ", ";
    NumToSkip |= Byte << 8;
    Byte = *I++;
    OS << utostr(Byte) << ", ";
    NumToSkip |= Byte << 16;
    return NumToSkip;
  };

  // FIXME: We may be able to use the NumToSkip values to recover
  // appropriate indentation levels.
  DecoderTable::const_iterator I = Table.begin();
  DecoderTable::const_iterator E = Table.end();
  while (I != E) {
    assert(I < E && "incomplete decode table entry!");

    uint64_t Pos = I - Table.begin();
    OS << "/* " << Pos << " */";
    OS.PadToColumn(12);

    switch (*I) {
    default:
      PrintFatalError("invalid decode table opcode");
    case MCD::OPC_ExtractField: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_ExtractField, ";

      // ULEB128 encoded start value.
      const char *ErrMsg = nullptr;
      unsigned Start = decodeULEB128(Table.data() + Pos + 1, nullptr,
                                     Table.data() + Table.size(), &ErrMsg);
      assert(ErrMsg == nullptr && "ULEB128 value too large!");
      I += emitULEB128(I, OS);

      unsigned Len = *I++;
      OS << Len << ",  // Inst{";
      if (Len > 1)
        OS << (Start + Len - 1) << "-";
      OS << Start << "} ...\n";
      break;
    }
    case MCD::OPC_FilterValue: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_FilterValue, ";
      // The filter value is ULEB128 encoded.
      I += emitULEB128(I, OS);

      // 24-bit numtoskip value.
      uint32_t NumToSkip = emitNumToSkip(I, OS);
      I += 3;
      OS << "// Skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_CheckField: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_CheckField, ";
      // ULEB128 encoded start value.
      I += emitULEB128(I, OS);
      // 8-bit length.
      unsigned Len = *I++;
      OS << Len << ", ";
      // ULEB128 encoded field value.
      I += emitULEB128(I, OS);

      // 24-bit numtoskip value.
      uint32_t NumToSkip = emitNumToSkip(I, OS);
      I += 3;
      OS << "// Skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_CheckPredicate: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_CheckPredicate, ";
      I += emitULEB128(I, OS);

      // 24-bit numtoskip value.
      uint32_t NumToSkip = emitNumToSkip(I, OS);
      I += 3;
      OS << "// Skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_Decode:
    case MCD::OPC_TryDecode: {
      bool IsTry = *I == MCD::OPC_TryDecode;
      ++I;
      // Decode the Opcode value.
      const char *ErrMsg = nullptr;
      unsigned Opc = decodeULEB128(Table.data() + Pos + 1, nullptr,
                                   Table.data() + Table.size(), &ErrMsg);
      assert(ErrMsg == nullptr && "ULEB128 value too large!");

      OS.indent(Indentation)
          << "MCD::OPC_" << (IsTry ? "Try" : "") << "Decode, ";
      I += emitULEB128(I, OS);

      // Decoder index.
      I += emitULEB128(I, OS);

      auto EncI = OpcodeToEncodingID.find(Opc);
      assert(EncI != OpcodeToEncodingID.end() && "no encoding entry");
      auto EncodingID = EncI->second;

      if (!IsTry) {
        OS << "// Opcode: " << NumberedEncodings[EncodingID] << "\n";
        break;
      }

      // Fallthrough for OPC_TryDecode.

      // 24-bit numtoskip value.
      uint32_t NumToSkip = emitNumToSkip(I, OS);
      I += 3;

      OS << "// Opcode: " << NumberedEncodings[EncodingID]
         << ", skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_SoftFail: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_SoftFail";
      // Positive mask
      uint64_t Value = 0;
      unsigned Shift = 0;
      do {
        OS << ", " << (unsigned)*I;
        Value += ((uint64_t)(*I & 0x7f)) << Shift;
        Shift += 7;
      } while (*I++ >= 128);
      if (Value > 127) {
        OS << " /* 0x";
        OS.write_hex(Value);
        OS << " */";
      }
      // Negative mask
      Value = 0;
      Shift = 0;
      do {
        OS << ", " << (unsigned)*I;
        Value += ((uint64_t)(*I & 0x7f)) << Shift;
        Shift += 7;
      } while (*I++ >= 128);
      if (Value > 127) {
        OS << " /* 0x";
        OS.write_hex(Value);
        OS << " */";
      }
      OS << ",\n";
      break;
    }
    case MCD::OPC_Fail: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_Fail,\n";
      break;
    }
    }
  }
  OS.indent(Indentation) << "0\n";

  Indentation -= 2;

  OS.indent(Indentation) << "};\n\n";
}

void DecoderEmitter::emitInstrLenTable(formatted_raw_ostream &OS,
                                       std::vector<unsigned> &InstrLen) const {
  OS << "static const uint8_t InstrLenTable[] = {\n";
  for (unsigned &Len : InstrLen) {
    OS << Len << ",\n";
  }
  OS << "};\n\n";
}

void DecoderEmitter::emitPredicateFunction(formatted_raw_ostream &OS,
                                           PredicateSet &Predicates,
                                           unsigned Indentation) const {
  // The predicate function is just a big switch statement based on the
  // input predicate index.
  OS.indent(Indentation) << "static bool checkDecoderPredicate(unsigned Idx, "
                         << "const FeatureBitset &Bits) {\n";
  Indentation += 2;
  if (!Predicates.empty()) {
    OS.indent(Indentation) << "switch (Idx) {\n";
    OS.indent(Indentation)
        << "default: llvm_unreachable(\"Invalid index!\");\n";
    unsigned Index = 0;
    for (const auto &Predicate : Predicates) {
      OS.indent(Indentation) << "case " << Index++ << ":\n";
      OS.indent(Indentation + 2) << "return (" << Predicate << ");\n";
    }
    OS.indent(Indentation) << "}\n";
  } else {
    // No case statement to emit
    OS.indent(Indentation) << "llvm_unreachable(\"Invalid index!\");\n";
  }
  Indentation -= 2;
  OS.indent(Indentation) << "}\n\n";
}

void DecoderEmitter::emitDecoderFunction(formatted_raw_ostream &OS,
                                         DecoderSet &Decoders,
                                         unsigned Indentation) const {
  // The decoder function is just a big switch statement based on the
  // input decoder index.
  OS.indent(Indentation) << "template <typename InsnType>\n";
  OS.indent(Indentation) << "static DecodeStatus decodeToMCInst(DecodeStatus S,"
                         << " unsigned Idx, InsnType insn, MCInst &MI,\n";
  OS.indent(Indentation)
      << "                                   uint64_t "
      << "Address, const MCDisassembler *Decoder, bool &DecodeComplete) {\n";
  Indentation += 2;
  OS.indent(Indentation) << "DecodeComplete = true;\n";
  // TODO: When InsnType is large, using uint64_t limits all fields to 64 bits
  // It would be better for emitBinaryParser to use a 64-bit tmp whenever
  // possible but fall back to an InsnType-sized tmp for truly large fields.
  OS.indent(Indentation) << "using TmpType = "
                            "std::conditional_t<std::is_integral<InsnType>::"
                            "value, InsnType, uint64_t>;\n";
  OS.indent(Indentation) << "TmpType tmp;\n";
  OS.indent(Indentation) << "switch (Idx) {\n";
  OS.indent(Indentation) << "default: llvm_unreachable(\"Invalid index!\");\n";
  unsigned Index = 0;
  for (const auto &Decoder : Decoders) {
    OS.indent(Indentation) << "case " << Index++ << ":\n";
    OS << Decoder;
    OS.indent(Indentation + 2) << "return S;\n";
  }
  OS.indent(Indentation) << "}\n";
  Indentation -= 2;
  OS.indent(Indentation) << "}\n";
}

// Populates the field of the insn given the start position and the number of
// consecutive bits to scan for.
//
// Returns a pair of values (indicator, field), where the indicator is false
// if there exists any uninitialized bit value in the range and true if all
// bits are well-known. The second value is the potentially populated field.
std::pair<bool, uint64_t> FilterChooser::fieldFromInsn(const insn_t &Insn,
                                                       unsigned StartBit,
                                                       unsigned NumBits) const {
  uint64_t Field = 0;

  for (unsigned i = 0; i < NumBits; ++i) {
    if (Insn[StartBit + i] == BIT_UNSET)
      return {false, Field};

    if (Insn[StartBit + i] == BIT_TRUE)
      Field = Field | (1ULL << i);
  }

  return {true, Field};
}

/// dumpFilterArray - dumpFilterArray prints out debugging info for the given
/// filter array as a series of chars.
void FilterChooser::dumpFilterArray(
    raw_ostream &o, const std::vector<bit_value_t> &filter) const {
  for (unsigned bitIndex = BitWidth; bitIndex > 0; bitIndex--) {
    switch (filter[bitIndex - 1]) {
    case BIT_UNFILTERED:
      o << ".";
      break;
    case BIT_UNSET:
      o << "_";
      break;
    case BIT_TRUE:
      o << "1";
      break;
    case BIT_FALSE:
      o << "0";
      break;
    }
  }
}

/// dumpStack - dumpStack traverses the filter chooser chain and calls
/// dumpFilterArray on each filter chooser up to the top level one.
void FilterChooser::dumpStack(raw_ostream &o, const char *prefix) const {
  const FilterChooser *current = this;

  while (current) {
    o << prefix;
    dumpFilterArray(o, current->FilterBitValues);
    o << '\n';
    current = current->Parent;
  }
}

// Calculates the island(s) needed to decode the instruction.
// This returns a list of undecoded bits of an instructions, for example,
// Inst{20} = 1 && Inst{3-0} == 0b1111 represents two islands of yet-to-be
// decoded bits in order to verify that the instruction matches the Opcode.
unsigned FilterChooser::getIslands(std::vector<unsigned> &StartBits,
                                   std::vector<unsigned> &EndBits,
                                   std::vector<uint64_t> &FieldVals,
                                   const insn_t &Insn) const {
  unsigned Num, BitNo;
  Num = BitNo = 0;

  uint64_t FieldVal = 0;

  // 0: Init
  // 1: Water (the bit value does not affect decoding)
  // 2: Island (well-known bit value needed for decoding)
  int State = 0;

  for (unsigned i = 0; i < BitWidth; ++i) {
    int64_t Val = Value(Insn[i]);
    bool Filtered = PositionFiltered(i);
    switch (State) {
    default:
      llvm_unreachable("Unreachable code!");
    case 0:
    case 1:
      if (Filtered || Val == -1)
        State = 1; // Still in Water
      else {
        State = 2; // Into the Island
        BitNo = 0;
        StartBits.push_back(i);
        FieldVal = Val;
      }
      break;
    case 2:
      if (Filtered || Val == -1) {
        State = 1; // Into the Water
        EndBits.push_back(i - 1);
        FieldVals.push_back(FieldVal);
        ++Num;
      } else {
        State = 2; // Still in Island
        ++BitNo;
        FieldVal = FieldVal | Val << BitNo;
      }
      break;
    }
  }
  // If we are still in Island after the loop, do some housekeeping.
  if (State == 2) {
    EndBits.push_back(BitWidth - 1);
    FieldVals.push_back(FieldVal);
    ++Num;
  }

  assert(StartBits.size() == Num && EndBits.size() == Num &&
         FieldVals.size() == Num);
  return Num;
}

void FilterChooser::emitBinaryParser(raw_ostream &o, unsigned &Indentation,
                                     const OperandInfo &OpInfo,
                                     bool &OpHasCompleteDecoder) const {
  const std::string &Decoder = OpInfo.Decoder;

  bool UseInsertBits = OpInfo.numFields() != 1 || OpInfo.InitValue != 0;

  if (UseInsertBits) {
    o.indent(Indentation) << "tmp = 0x";
    o.write_hex(OpInfo.InitValue);
    o << ";\n";
  }

  for (const EncodingField &EF : OpInfo) {
    o.indent(Indentation);
    if (UseInsertBits)
      o << "insertBits(tmp, ";
    else
      o << "tmp = ";
    o << "fieldFromInstruction(insn, " << EF.Base << ", " << EF.Width << ')';
    if (UseInsertBits)
      o << ", " << EF.Offset << ", " << EF.Width << ')';
    else if (EF.Offset != 0)
      o << " << " << EF.Offset;
    o << ";\n";
  }

  if (Decoder != "") {
    OpHasCompleteDecoder = OpInfo.HasCompleteDecoder;
    o.indent(Indentation) << "if (!Check(S, " << Decoder
                          << "(MI, tmp, Address, Decoder))) { "
                          << (OpHasCompleteDecoder ? ""
                                                   : "DecodeComplete = false; ")
                          << "return MCDisassembler::Fail; }\n";
  } else {
    OpHasCompleteDecoder = true;
    o.indent(Indentation) << "MI.addOperand(MCOperand::createImm(tmp));\n";
  }
}

void FilterChooser::emitDecoder(raw_ostream &OS, unsigned Indentation,
                                unsigned Opc, bool &HasCompleteDecoder) const {
  HasCompleteDecoder = true;

  for (const auto &Op : Operands.find(Opc)->second) {
    // If a custom instruction decoder was specified, use that.
    if (Op.numFields() == 0 && !Op.Decoder.empty()) {
      HasCompleteDecoder = Op.HasCompleteDecoder;
      OS.indent(Indentation)
          << "if (!Check(S, " << Op.Decoder
          << "(MI, insn, Address, Decoder))) { "
          << (HasCompleteDecoder ? "" : "DecodeComplete = false; ")
          << "return MCDisassembler::Fail; }\n";
      break;
    }

    bool OpHasCompleteDecoder;
    emitBinaryParser(OS, Indentation, Op, OpHasCompleteDecoder);
    if (!OpHasCompleteDecoder)
      HasCompleteDecoder = false;
  }
}

unsigned FilterChooser::getDecoderIndex(DecoderSet &Decoders, unsigned Opc,
                                        bool &HasCompleteDecoder) const {
  // Build up the predicate string.
  SmallString<256> Decoder;
  // FIXME: emitDecoder() function can take a buffer directly rather than
  // a stream.
  raw_svector_ostream S(Decoder);
  unsigned I = 4;
  emitDecoder(S, I, Opc, HasCompleteDecoder);

  // Using the full decoder string as the key value here is a bit
  // heavyweight, but is effective. If the string comparisons become a
  // performance concern, we can implement a mangling of the predicate
  // data easily enough with a map back to the actual string. That's
  // overkill for now, though.

  // Make sure the predicate is in the table.
  Decoders.insert(CachedHashString(Decoder));
  // Now figure out the index for when we write out the table.
  DecoderSet::const_iterator P = find(Decoders, Decoder.str());
  return (unsigned)(P - Decoders.begin());
}

// If ParenIfBinOp is true, print a surrounding () if Val uses && or ||.
bool FilterChooser::emitPredicateMatchAux(const Init &Val, bool ParenIfBinOp,
                                          raw_ostream &OS) const {
  if (const auto *D = dyn_cast<DefInit>(&Val)) {
    if (!D->getDef()->isSubClassOf("SubtargetFeature"))
      return true;
    OS << "Bits[" << Emitter->PredicateNamespace << "::" << D->getAsString()
       << "]";
    return false;
  }
  if (const auto *D = dyn_cast<DagInit>(&Val)) {
    std::string Op = D->getOperator()->getAsString();
    if (Op == "not" && D->getNumArgs() == 1) {
      OS << '!';
      return emitPredicateMatchAux(*D->getArg(0), true, OS);
    }
    if ((Op == "any_of" || Op == "all_of") && D->getNumArgs() > 0) {
      bool Paren = D->getNumArgs() > 1 && std::exchange(ParenIfBinOp, true);
      if (Paren)
        OS << '(';
      ListSeparator LS(Op == "any_of" ? " || " : " && ");
      for (auto *Arg : D->getArgs()) {
        OS << LS;
        if (emitPredicateMatchAux(*Arg, ParenIfBinOp, OS))
          return true;
      }
      if (Paren)
        OS << ')';
      return false;
    }
  }
  return true;
}

bool FilterChooser::emitPredicateMatch(raw_ostream &o, unsigned &Indentation,
                                       unsigned Opc) const {
  ListInit *Predicates =
      AllInstructions[Opc].EncodingDef->getValueAsListInit("Predicates");
  bool IsFirstEmission = true;
  for (unsigned i = 0; i < Predicates->size(); ++i) {
    Record *Pred = Predicates->getElementAsRecord(i);
    if (!Pred->getValue("AssemblerMatcherPredicate"))
      continue;

    if (!isa<DagInit>(Pred->getValue("AssemblerCondDag")->getValue()))
      continue;

    if (!IsFirstEmission)
      o << " && ";
    if (emitPredicateMatchAux(*Pred->getValueAsDag("AssemblerCondDag"),
                              Predicates->size() > 1, o))
      PrintFatalError(Pred->getLoc(), "Invalid AssemblerCondDag!");
    IsFirstEmission = false;
  }
  return !Predicates->empty();
}

bool FilterChooser::doesOpcodeNeedPredicate(unsigned Opc) const {
  ListInit *Predicates =
      AllInstructions[Opc].EncodingDef->getValueAsListInit("Predicates");
  for (unsigned i = 0; i < Predicates->size(); ++i) {
    Record *Pred = Predicates->getElementAsRecord(i);
    if (!Pred->getValue("AssemblerMatcherPredicate"))
      continue;

    if (isa<DagInit>(Pred->getValue("AssemblerCondDag")->getValue()))
      return true;
  }
  return false;
}

unsigned FilterChooser::getPredicateIndex(DecoderTableInfo &TableInfo,
                                          StringRef Predicate) const {
  // Using the full predicate string as the key value here is a bit
  // heavyweight, but is effective. If the string comparisons become a
  // performance concern, we can implement a mangling of the predicate
  // data easily enough with a map back to the actual string. That's
  // overkill for now, though.

  // Make sure the predicate is in the table.
  TableInfo.Predicates.insert(CachedHashString(Predicate));
  // Now figure out the index for when we write out the table.
  PredicateSet::const_iterator P = find(TableInfo.Predicates, Predicate);
  return (unsigned)(P - TableInfo.Predicates.begin());
}

void FilterChooser::emitPredicateTableEntry(DecoderTableInfo &TableInfo,
                                            unsigned Opc) const {
  if (!doesOpcodeNeedPredicate(Opc))
    return;

  // Build up the predicate string.
  SmallString<256> Predicate;
  // FIXME: emitPredicateMatch() functions can take a buffer directly rather
  // than a stream.
  raw_svector_ostream PS(Predicate);
  unsigned I = 0;
  emitPredicateMatch(PS, I, Opc);

  // Figure out the index into the predicate table for the predicate just
  // computed.
  unsigned PIdx = getPredicateIndex(TableInfo, PS.str());
  SmallString<16> PBytes;
  raw_svector_ostream S(PBytes);
  encodeULEB128(PIdx, S);

  TableInfo.Table.push_back(MCD::OPC_CheckPredicate);
  // Predicate index.
  for (const auto PB : PBytes)
    TableInfo.Table.push_back(PB);
  // Push location for NumToSkip backpatching.
  TableInfo.FixupStack.back().push_back(TableInfo.Table.size());
  TableInfo.Table.push_back(0);
  TableInfo.Table.push_back(0);
  TableInfo.Table.push_back(0);
}

void FilterChooser::emitSoftFailTableEntry(DecoderTableInfo &TableInfo,
                                           unsigned Opc) const {
  const Record *EncodingDef = AllInstructions[Opc].EncodingDef;
  const RecordVal *RV = EncodingDef->getValue("SoftFail");
  BitsInit *SFBits = RV ? dyn_cast<BitsInit>(RV->getValue()) : nullptr;

  if (!SFBits)
    return;
  BitsInit *InstBits = EncodingDef->getValueAsBitsInit("Inst");

  APInt PositiveMask(BitWidth, 0ULL);
  APInt NegativeMask(BitWidth, 0ULL);
  for (unsigned i = 0; i < BitWidth; ++i) {
    bit_value_t B = bitFromBits(*SFBits, i);
    bit_value_t IB = bitFromBits(*InstBits, i);

    if (B != BIT_TRUE)
      continue;

    switch (IB) {
    case BIT_FALSE:
      // The bit is meant to be false, so emit a check to see if it is true.
      PositiveMask.setBit(i);
      break;
    case BIT_TRUE:
      // The bit is meant to be true, so emit a check to see if it is false.
      NegativeMask.setBit(i);
      break;
    default:
      // The bit is not set; this must be an error!
      errs() << "SoftFail Conflict: bit SoftFail{" << i << "} in "
             << AllInstructions[Opc] << " is set but Inst{" << i
             << "} is unset!\n"
             << "  - You can only mark a bit as SoftFail if it is fully defined"
             << " (1/0 - not '?') in Inst\n";
      return;
    }
  }

  bool NeedPositiveMask = PositiveMask.getBoolValue();
  bool NeedNegativeMask = NegativeMask.getBoolValue();

  if (!NeedPositiveMask && !NeedNegativeMask)
    return;

  TableInfo.Table.push_back(MCD::OPC_SoftFail);

  SmallString<16> MaskBytes;
  raw_svector_ostream S(MaskBytes);
  if (NeedPositiveMask) {
    encodeULEB128(PositiveMask.getZExtValue(), S);
    for (unsigned i = 0, e = MaskBytes.size(); i != e; ++i)
      TableInfo.Table.push_back(MaskBytes[i]);
  } else
    TableInfo.Table.push_back(0);
  if (NeedNegativeMask) {
    MaskBytes.clear();
    encodeULEB128(NegativeMask.getZExtValue(), S);
    for (unsigned i = 0, e = MaskBytes.size(); i != e; ++i)
      TableInfo.Table.push_back(MaskBytes[i]);
  } else
    TableInfo.Table.push_back(0);
}

// Emits table entries to decode the singleton.
void FilterChooser::emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                                            EncodingIDAndOpcode Opc) const {
  std::vector<unsigned> StartBits;
  std::vector<unsigned> EndBits;
  std::vector<uint64_t> FieldVals;
  insn_t Insn;
  insnWithID(Insn, Opc.EncodingID);

  // Look for islands of undecoded bits of the singleton.
  getIslands(StartBits, EndBits, FieldVals, Insn);

  unsigned Size = StartBits.size();

  // Emit the predicate table entry if one is needed.
  emitPredicateTableEntry(TableInfo, Opc.EncodingID);

  // Check any additional encoding fields needed.
  for (unsigned I = Size; I != 0; --I) {
    unsigned NumBits = EndBits[I - 1] - StartBits[I - 1] + 1;
    assert((NumBits < (1u << 8)) && "NumBits overflowed uint8 table entry!");
    TableInfo.Table.push_back(MCD::OPC_CheckField);
    uint8_t Buffer[16], *P;
    encodeULEB128(StartBits[I - 1], Buffer);
    for (P = Buffer; *P >= 128; ++P)
      TableInfo.Table.push_back(*P);
    TableInfo.Table.push_back(*P);
    TableInfo.Table.push_back(NumBits);
    encodeULEB128(FieldVals[I - 1], Buffer);
    for (P = Buffer; *P >= 128; ++P)
      TableInfo.Table.push_back(*P);
    TableInfo.Table.push_back(*P);
    // Push location for NumToSkip backpatching.
    TableInfo.FixupStack.back().push_back(TableInfo.Table.size());
    // The fixup is always 24-bits, so go ahead and allocate the space
    // in the table so all our relative position calculations work OK even
    // before we fully resolve the real value here.
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
  }

  // Check for soft failure of the match.
  emitSoftFailTableEntry(TableInfo, Opc.EncodingID);

  bool HasCompleteDecoder;
  unsigned DIdx =
      getDecoderIndex(TableInfo.Decoders, Opc.EncodingID, HasCompleteDecoder);

  // Produce OPC_Decode or OPC_TryDecode opcode based on the information
  // whether the instruction decoder is complete or not. If it is complete
  // then it handles all possible values of remaining variable/unfiltered bits
  // and for any value can determine if the bitpattern is a valid instruction
  // or not. This means OPC_Decode will be the final step in the decoding
  // process. If it is not complete, then the Fail return code from the
  // decoder method indicates that additional processing should be done to see
  // if there is any other instruction that also matches the bitpattern and
  // can decode it.
  TableInfo.Table.push_back(HasCompleteDecoder ? MCD::OPC_Decode
                                               : MCD::OPC_TryDecode);
  NumEncodingsSupported++;
  uint8_t Buffer[16], *p;
  encodeULEB128(Opc.Opcode, Buffer);
  for (p = Buffer; *p >= 128; ++p)
    TableInfo.Table.push_back(*p);
  TableInfo.Table.push_back(*p);

  SmallString<16> Bytes;
  raw_svector_ostream S(Bytes);
  encodeULEB128(DIdx, S);

  // Decoder index.
  for (const auto B : Bytes)
    TableInfo.Table.push_back(B);

  if (!HasCompleteDecoder) {
    // Push location for NumToSkip backpatching.
    TableInfo.FixupStack.back().push_back(TableInfo.Table.size());
    // Allocate the space for the fixup.
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
  }
}

// Emits table entries to decode the singleton, and then to decode the rest.
void FilterChooser::emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                                            const Filter &Best) const {
  EncodingIDAndOpcode Opc = Best.getSingletonOpc();

  // complex singletons need predicate checks from the first singleton
  // to refer forward to the variable filterchooser that follows.
  TableInfo.FixupStack.emplace_back();

  emitSingletonTableEntry(TableInfo, Opc);

  resolveTableFixups(TableInfo.Table, TableInfo.FixupStack.back(),
                     TableInfo.Table.size());
  TableInfo.FixupStack.pop_back();

  Best.getVariableFC().emitTableEntries(TableInfo);
}

// Assign a single filter and run with it.  Top level API client can initialize
// with a single filter to start the filtering process.
void FilterChooser::runSingleFilter(unsigned startBit, unsigned numBit,
                                    bool mixed) {
  Filters.clear();
  Filters.emplace_back(*this, startBit, numBit, true);
  BestIndex = 0; // Sole Filter instance to choose from.
  bestFilter().recurse();
}

// reportRegion is a helper function for filterProcessor to mark a region as
// eligible for use as a filter region.
void FilterChooser::reportRegion(bitAttr_t RA, unsigned StartBit,
                                 unsigned BitIndex, bool AllowMixed) {
  if (RA == ATTR_MIXED && AllowMixed)
    Filters.emplace_back(*this, StartBit, BitIndex - StartBit, true);
  else if (RA == ATTR_ALL_SET && !AllowMixed)
    Filters.emplace_back(*this, StartBit, BitIndex - StartBit, false);
}

// FilterProcessor scans the well-known encoding bits of the instructions and
// builds up a list of candidate filters.  It chooses the best filter and
// recursively descends down the decoding tree.
bool FilterChooser::filterProcessor(bool AllowMixed, bool Greedy) {
  Filters.clear();
  BestIndex = -1;
  unsigned numInstructions = Opcodes.size();

  assert(numInstructions && "Filter created with no instructions");

  // No further filtering is necessary.
  if (numInstructions == 1)
    return true;

  // Heuristics.  See also doFilter()'s "Heuristics" comment when num of
  // instructions is 3.
  if (AllowMixed && !Greedy) {
    assert(numInstructions == 3);

    for (const auto &Opcode : Opcodes) {
      std::vector<unsigned> StartBits;
      std::vector<unsigned> EndBits;
      std::vector<uint64_t> FieldVals;
      insn_t Insn;

      insnWithID(Insn, Opcode.EncodingID);

      // Look for islands of undecoded bits of any instruction.
      if (getIslands(StartBits, EndBits, FieldVals, Insn) > 0) {
        // Found an instruction with island(s).  Now just assign a filter.
        runSingleFilter(StartBits[0], EndBits[0] - StartBits[0] + 1, true);
        return true;
      }
    }
  }

  unsigned BitIndex;

  // We maintain BIT_WIDTH copies of the bitAttrs automaton.
  // The automaton consumes the corresponding bit from each
  // instruction.
  //
  //   Input symbols: 0, 1, and _ (unset).
  //   States:        NONE, FILTERED, ALL_SET, ALL_UNSET, and MIXED.
  //   Initial state: NONE.
  //
  // (NONE) ------- [01] -> (ALL_SET)
  // (NONE) ------- _ ----> (ALL_UNSET)
  // (ALL_SET) ---- [01] -> (ALL_SET)
  // (ALL_SET) ---- _ ----> (MIXED)
  // (ALL_UNSET) -- [01] -> (MIXED)
  // (ALL_UNSET) -- _ ----> (ALL_UNSET)
  // (MIXED) ------ . ----> (MIXED)
  // (FILTERED)---- . ----> (FILTERED)

  std::vector<bitAttr_t> bitAttrs;

  // FILTERED bit positions provide no entropy and are not worthy of pursuing.
  // Filter::recurse() set either BIT_TRUE or BIT_FALSE for each position.
  for (BitIndex = 0; BitIndex < BitWidth; ++BitIndex)
    if (FilterBitValues[BitIndex] == BIT_TRUE ||
        FilterBitValues[BitIndex] == BIT_FALSE)
      bitAttrs.push_back(ATTR_FILTERED);
    else
      bitAttrs.push_back(ATTR_NONE);

  for (const auto &OpcPair : Opcodes) {
    insn_t insn;

    insnWithID(insn, OpcPair.EncodingID);

    for (BitIndex = 0; BitIndex < BitWidth; ++BitIndex) {
      switch (bitAttrs[BitIndex]) {
      case ATTR_NONE:
        if (insn[BitIndex] == BIT_UNSET)
          bitAttrs[BitIndex] = ATTR_ALL_UNSET;
        else
          bitAttrs[BitIndex] = ATTR_ALL_SET;
        break;
      case ATTR_ALL_SET:
        if (insn[BitIndex] == BIT_UNSET)
          bitAttrs[BitIndex] = ATTR_MIXED;
        break;
      case ATTR_ALL_UNSET:
        if (insn[BitIndex] != BIT_UNSET)
          bitAttrs[BitIndex] = ATTR_MIXED;
        break;
      case ATTR_MIXED:
      case ATTR_FILTERED:
        break;
      }
    }
  }

  // The regionAttr automaton consumes the bitAttrs automatons' state,
  // lowest-to-highest.
  //
  //   Input symbols: F(iltered), (all_)S(et), (all_)U(nset), M(ixed)
  //   States:        NONE, ALL_SET, MIXED
  //   Initial state: NONE
  //
  // (NONE) ----- F --> (NONE)
  // (NONE) ----- S --> (ALL_SET)     ; and set region start
  // (NONE) ----- U --> (NONE)
  // (NONE) ----- M --> (MIXED)       ; and set region start
  // (ALL_SET) -- F --> (NONE)        ; and report an ALL_SET region
  // (ALL_SET) -- S --> (ALL_SET)
  // (ALL_SET) -- U --> (NONE)        ; and report an ALL_SET region
  // (ALL_SET) -- M --> (MIXED)       ; and report an ALL_SET region
  // (MIXED) ---- F --> (NONE)        ; and report a MIXED region
  // (MIXED) ---- S --> (ALL_SET)     ; and report a MIXED region
  // (MIXED) ---- U --> (NONE)        ; and report a MIXED region
  // (MIXED) ---- M --> (MIXED)

  bitAttr_t RA = ATTR_NONE;
  unsigned StartBit = 0;

  for (BitIndex = 0; BitIndex < BitWidth; ++BitIndex) {
    bitAttr_t bitAttr = bitAttrs[BitIndex];

    assert(bitAttr != ATTR_NONE && "Bit without attributes");

    switch (RA) {
    case ATTR_NONE:
      switch (bitAttr) {
      case ATTR_FILTERED:
        break;
      case ATTR_ALL_SET:
        StartBit = BitIndex;
        RA = ATTR_ALL_SET;
        break;
      case ATTR_ALL_UNSET:
        break;
      case ATTR_MIXED:
        StartBit = BitIndex;
        RA = ATTR_MIXED;
        break;
      default:
        llvm_unreachable("Unexpected bitAttr!");
      }
      break;
    case ATTR_ALL_SET:
      switch (bitAttr) {
      case ATTR_FILTERED:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        RA = ATTR_NONE;
        break;
      case ATTR_ALL_SET:
        break;
      case ATTR_ALL_UNSET:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        RA = ATTR_NONE;
        break;
      case ATTR_MIXED:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        StartBit = BitIndex;
        RA = ATTR_MIXED;
        break;
      default:
        llvm_unreachable("Unexpected bitAttr!");
      }
      break;
    case ATTR_MIXED:
      switch (bitAttr) {
      case ATTR_FILTERED:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        StartBit = BitIndex;
        RA = ATTR_NONE;
        break;
      case ATTR_ALL_SET:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        StartBit = BitIndex;
        RA = ATTR_ALL_SET;
        break;
      case ATTR_ALL_UNSET:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        RA = ATTR_NONE;
        break;
      case ATTR_MIXED:
        break;
      default:
        llvm_unreachable("Unexpected bitAttr!");
      }
      break;
    case ATTR_ALL_UNSET:
      llvm_unreachable("regionAttr state machine has no ATTR_UNSET state");
    case ATTR_FILTERED:
      llvm_unreachable("regionAttr state machine has no ATTR_FILTERED state");
    }
  }

  // At the end, if we're still in ALL_SET or MIXED states, report a region
  switch (RA) {
  case ATTR_NONE:
    break;
  case ATTR_FILTERED:
    break;
  case ATTR_ALL_SET:
    reportRegion(RA, StartBit, BitIndex, AllowMixed);
    break;
  case ATTR_ALL_UNSET:
    break;
  case ATTR_MIXED:
    reportRegion(RA, StartBit, BitIndex, AllowMixed);
    break;
  }

  // We have finished with the filter processings.  Now it's time to choose
  // the best performing filter.
  BestIndex = 0;
  bool AllUseless = true;
  unsigned BestScore = 0;

  for (const auto &[Idx, Filter] : enumerate(Filters)) {
    unsigned Usefulness = Filter.usefulness();

    if (Usefulness)
      AllUseless = false;

    if (Usefulness > BestScore) {
      BestIndex = Idx;
      BestScore = Usefulness;
    }
  }

  if (!AllUseless)
    bestFilter().recurse();

  return !AllUseless;
} // end of FilterChooser::filterProcessor(bool)

// Decides on the best configuration of filter(s) to use in order to decode
// the instructions.  A conflict of instructions may occur, in which case we
// dump the conflict set to the standard error.
void FilterChooser::doFilter() {
  unsigned Num = Opcodes.size();
  assert(Num && "FilterChooser created with no instructions");

  // Try regions of consecutive known bit values first.
  if (filterProcessor(false))
    return;

  // Then regions of mixed bits (both known and unitialized bit values allowed).
  if (filterProcessor(true))
    return;

  // Heuristics to cope with conflict set {t2CMPrs, t2SUBSrr, t2SUBSrs} where
  // no single instruction for the maximum ATTR_MIXED region Inst{14-4} has a
  // well-known encoding pattern.  In such case, we backtrack and scan for the
  // the very first consecutive ATTR_ALL_SET region and assign a filter to it.
  if (Num == 3 && filterProcessor(true, false))
    return;

  // If we come to here, the instruction decoding has failed.
  // Set the BestIndex to -1 to indicate so.
  BestIndex = -1;
}

// emitTableEntries - Emit state machine entries to decode our share of
// instructions.
void FilterChooser::emitTableEntries(DecoderTableInfo &TableInfo) const {
  if (Opcodes.size() == 1) {
    // There is only one instruction in the set, which is great!
    // Call emitSingletonDecoder() to see whether there are any remaining
    // encodings bits.
    emitSingletonTableEntry(TableInfo, Opcodes[0]);
    return;
  }

  // Choose the best filter to do the decodings!
  if (BestIndex != -1) {
    const Filter &Best = Filters[BestIndex];
    if (Best.getNumFiltered() == 1)
      emitSingletonTableEntry(TableInfo, Best);
    else
      Best.emitTableEntry(TableInfo);
    return;
  }

  // We don't know how to decode these instructions!  Dump the
  // conflict set and bail.

  // Print out useful conflict information for postmortem analysis.
  errs() << "Decoding Conflict:\n";

  dumpStack(errs(), "\t\t");

  for (auto Opcode : Opcodes) {
    errs() << '\t';
    emitNameWithID(errs(), Opcode.EncodingID);
    errs() << " ";
    dumpBits(
        errs(),
        getBitsField(*AllInstructions[Opcode.EncodingID].EncodingDef, "Inst"));
    errs() << '\n';
  }
}

static std::string findOperandDecoderMethod(Record *Record) {
  std::string Decoder;

  RecordVal *DecoderString = Record->getValue("DecoderMethod");
  StringInit *String =
      DecoderString ? dyn_cast<StringInit>(DecoderString->getValue()) : nullptr;
  if (String) {
    Decoder = std::string(String->getValue());
    if (!Decoder.empty())
      return Decoder;
  }

  if (Record->isSubClassOf("RegisterOperand"))
    // Allows use of a DecoderMethod in referenced RegisterClass if set.
    return findOperandDecoderMethod(Record->getValueAsDef("RegClass"));

  if (Record->isSubClassOf("RegisterClass")) {
    Decoder = "Decode" + Record->getName().str() + "RegisterClass";
  } else if (Record->isSubClassOf("PointerLikeRegClass")) {
    Decoder = "DecodePointerLikeRegClass" +
              utostr(Record->getValueAsInt("RegClassKind"));
  }

  return Decoder;
}

OperandInfo getOpInfo(Record *TypeRecord) {
  std::string Decoder = findOperandDecoderMethod(TypeRecord);

  RecordVal *HasCompleteDecoderVal = TypeRecord->getValue("hasCompleteDecoder");
  BitInit *HasCompleteDecoderBit =
      HasCompleteDecoderVal
          ? dyn_cast<BitInit>(HasCompleteDecoderVal->getValue())
          : nullptr;
  bool HasCompleteDecoder =
      HasCompleteDecoderBit ? HasCompleteDecoderBit->getValue() : true;

  return OperandInfo(Decoder, HasCompleteDecoder);
}

void parseVarLenInstOperand(const Record &Def,
                            std::vector<OperandInfo> &Operands,
                            const CodeGenInstruction &CGI) {

  const RecordVal *RV = Def.getValue("Inst");
  VarLenInst VLI(cast<DagInit>(RV->getValue()), RV);
  SmallVector<int> TiedTo;

  for (const auto &[Idx, Op] : enumerate(CGI.Operands)) {
    if (Op.MIOperandInfo && Op.MIOperandInfo->getNumArgs() > 0)
      for (auto *Arg : Op.MIOperandInfo->getArgs())
        Operands.push_back(getOpInfo(cast<DefInit>(Arg)->getDef()));
    else
      Operands.push_back(getOpInfo(Op.Rec));

    int TiedReg = Op.getTiedRegister();
    TiedTo.push_back(-1);
    if (TiedReg != -1) {
      TiedTo[Idx] = TiedReg;
      TiedTo[TiedReg] = Idx;
    }
  }

  unsigned CurrBitPos = 0;
  for (const auto &EncodingSegment : VLI) {
    unsigned Offset = 0;
    StringRef OpName;

    if (const StringInit *SI = dyn_cast<StringInit>(EncodingSegment.Value)) {
      OpName = SI->getValue();
    } else if (const DagInit *DI = dyn_cast<DagInit>(EncodingSegment.Value)) {
      OpName = cast<StringInit>(DI->getArg(0))->getValue();
      Offset = cast<IntInit>(DI->getArg(2))->getValue();
    }

    if (!OpName.empty()) {
      auto OpSubOpPair =
          const_cast<CodeGenInstruction &>(CGI).Operands.ParseOperandName(
              OpName);
      unsigned OpIdx = CGI.Operands.getFlattenedOperandNumber(OpSubOpPair);
      Operands[OpIdx].addField(CurrBitPos, EncodingSegment.BitWidth, Offset);
      if (!EncodingSegment.CustomDecoder.empty())
        Operands[OpIdx].Decoder = EncodingSegment.CustomDecoder.str();

      int TiedReg = TiedTo[OpSubOpPair.first];
      if (TiedReg != -1) {
        unsigned OpIdx = CGI.Operands.getFlattenedOperandNumber(
            std::pair(TiedReg, OpSubOpPair.second));
        Operands[OpIdx].addField(CurrBitPos, EncodingSegment.BitWidth, Offset);
      }
    }

    CurrBitPos += EncodingSegment.BitWidth;
  }
}

static void debugDumpRecord(const Record &Rec) {
  // Dump the record, so we can see what's going on...
  std::string E;
  raw_string_ostream S(E);
  S << "Dumping record for previous error:\n";
  S << Rec;
  PrintNote(E);
}

/// For an operand field named OpName: populate OpInfo.InitValue with the
/// constant-valued bit values, and OpInfo.Fields with the ranges of bits to
/// insert from the decoded instruction.
static void addOneOperandFields(const Record &EncodingDef, const BitsInit &Bits,
                                std::map<std::string, std::string> &TiedNames,
                                StringRef OpName, OperandInfo &OpInfo) {
  // Some bits of the operand may be required to be 1 depending on the
  // instruction's encoding. Collect those bits.
  if (const RecordVal *EncodedValue = EncodingDef.getValue(OpName))
    if (const BitsInit *OpBits = dyn_cast<BitsInit>(EncodedValue->getValue()))
      for (unsigned I = 0; I < OpBits->getNumBits(); ++I)
        if (const BitInit *OpBit = dyn_cast<BitInit>(OpBits->getBit(I)))
          if (OpBit->getValue())
            OpInfo.InitValue |= 1ULL << I;

  for (unsigned I = 0, J = 0; I != Bits.getNumBits(); I = J) {
    VarInit *Var;
    unsigned Offset = 0;
    for (; J != Bits.getNumBits(); ++J) {
      VarBitInit *BJ = dyn_cast<VarBitInit>(Bits.getBit(J));
      if (BJ) {
        Var = dyn_cast<VarInit>(BJ->getBitVar());
        if (I == J)
          Offset = BJ->getBitNum();
        else if (BJ->getBitNum() != Offset + J - I)
          break;
      } else {
        Var = dyn_cast<VarInit>(Bits.getBit(J));
      }
      if (!Var || (Var->getName() != OpName &&
                   Var->getName() != TiedNames[std::string(OpName)]))
        break;
    }
    if (I == J)
      ++J;
    else
      OpInfo.addField(I, J - I, Offset);
  }
}

static unsigned
populateInstruction(CodeGenTarget &Target, const Record &EncodingDef,
                    const CodeGenInstruction &CGI, unsigned Opc,
                    std::map<unsigned, std::vector<OperandInfo>> &Operands,
                    bool IsVarLenInst) {
  const Record &Def = *CGI.TheDef;
  // If all the bit positions are not specified; do not decode this instruction.
  // We are bound to fail!  For proper disassembly, the well-known encoding bits
  // of the instruction must be fully specified.

  BitsInit &Bits = getBitsField(EncodingDef, "Inst");
  if (Bits.allInComplete())
    return 0;

  std::vector<OperandInfo> InsnOperands;

  // If the instruction has specified a custom decoding hook, use that instead
  // of trying to auto-generate the decoder.
  StringRef InstDecoder = EncodingDef.getValueAsString("DecoderMethod");
  if (InstDecoder != "") {
    bool HasCompleteInstDecoder =
        EncodingDef.getValueAsBit("hasCompleteDecoder");
    InsnOperands.push_back(
        OperandInfo(std::string(InstDecoder), HasCompleteInstDecoder));
    Operands[Opc] = InsnOperands;
    return Bits.getNumBits();
  }

  // Generate a description of the operand of the instruction that we know
  // how to decode automatically.
  // FIXME: We'll need to have a way to manually override this as needed.

  // Gather the outputs/inputs of the instruction, so we can find their
  // positions in the encoding.  This assumes for now that they appear in the
  // MCInst in the order that they're listed.
  std::vector<std::pair<Init *, StringRef>> InOutOperands;
  DagInit *Out = Def.getValueAsDag("OutOperandList");
  DagInit *In = Def.getValueAsDag("InOperandList");
  for (const auto &[Idx, Arg] : enumerate(Out->getArgs()))
    InOutOperands.push_back(std::pair(Arg, Out->getArgNameStr(Idx)));
  for (const auto &[Idx, Arg] : enumerate(In->getArgs()))
    InOutOperands.push_back(std::pair(Arg, In->getArgNameStr(Idx)));

  // Search for tied operands, so that we can correctly instantiate
  // operands that are not explicitly represented in the encoding.
  std::map<std::string, std::string> TiedNames;
  for (const auto &[I, Op] : enumerate(CGI.Operands)) {
    for (const auto &[J, CI] : enumerate(Op.Constraints)) {
      if (CI.isTied()) {
        std::pair<unsigned, unsigned> SO =
            CGI.Operands.getSubOperandNumber(CI.getTiedOperand());
        std::string TiedName = CGI.Operands[SO.first].SubOpNames[SO.second];
        if (TiedName.empty())
          TiedName = CGI.Operands[SO.first].Name;
        std::string MyName = Op.SubOpNames[J];
        if (MyName.empty())
          MyName = Op.Name;

        TiedNames[MyName] = TiedName;
        TiedNames[TiedName] = MyName;
      }
    }
  }

  if (IsVarLenInst) {
    parseVarLenInstOperand(EncodingDef, InsnOperands, CGI);
  } else {
    // For each operand, see if we can figure out where it is encoded.
    for (const auto &Op : InOutOperands) {
      Init *OpInit = Op.first;
      StringRef OpName = Op.second;

      // We're ready to find the instruction encoding locations for this
      // operand.

      // First, find the operand type ("OpInit"), and sub-op names
      // ("SubArgDag") if present.
      DagInit *SubArgDag = dyn_cast<DagInit>(OpInit);
      if (SubArgDag)
        OpInit = SubArgDag->getOperator();
      Record *OpTypeRec = cast<DefInit>(OpInit)->getDef();
      // Lookup the sub-operands from the operand type record (note that only
      // Operand subclasses have MIOperandInfo, see CodeGenInstruction.cpp).
      DagInit *SubOps = OpTypeRec->isSubClassOf("Operand")
                            ? OpTypeRec->getValueAsDag("MIOperandInfo")
                            : nullptr;

      // Lookup the decoder method and construct a new OperandInfo to hold our
      // result.
      OperandInfo OpInfo = getOpInfo(OpTypeRec);

      // If we have named sub-operands...
      if (SubArgDag) {
        // Then there should not be a custom decoder specified on the top-level
        // type.
        if (!OpInfo.Decoder.empty()) {
          PrintError(EncodingDef.getLoc(),
                     "DecoderEmitter: operand \"" + OpName + "\" has type \"" +
                         OpInit->getAsString() +
                         "\" with a custom DecoderMethod, but also named "
                         "sub-operands.");
          continue;
        }

        // Decode each of the sub-ops separately.
        assert(SubOps && SubArgDag->getNumArgs() == SubOps->getNumArgs());
        for (const auto &[I, Arg] : enumerate(SubOps->getArgs())) {
          StringRef SubOpName = SubArgDag->getArgNameStr(I);
          OperandInfo SubOpInfo = getOpInfo(cast<DefInit>(Arg)->getDef());

          addOneOperandFields(EncodingDef, Bits, TiedNames, SubOpName,
                              SubOpInfo);
          InsnOperands.push_back(SubOpInfo);
        }
        continue;
      }

      // Otherwise, if we have an operand with sub-operands, but they aren't
      // named...
      if (SubOps && OpInfo.Decoder.empty()) {
        // If it's a single sub-operand, and no custom decoder, use the decoder
        // from the one sub-operand.
        if (SubOps->getNumArgs() == 1)
          OpInfo = getOpInfo(cast<DefInit>(SubOps->getArg(0))->getDef());

        // If we have multiple sub-ops, there'd better have a custom
        // decoder. (Otherwise we don't know how to populate them properly...)
        if (SubOps->getNumArgs() > 1) {
          PrintError(EncodingDef.getLoc(),
                     "DecoderEmitter: operand \"" + OpName +
                         "\" uses MIOperandInfo with multiple ops, but doesn't "
                         "have a custom decoder!");
          debugDumpRecord(EncodingDef);
          continue;
        }
      }

      addOneOperandFields(EncodingDef, Bits, TiedNames, OpName, OpInfo);
      // FIXME: it should be an error not to find a definition for a given
      // operand, rather than just failing to add it to the resulting
      // instruction! (This is a longstanding bug, which will be addressed in an
      // upcoming change.)
      if (OpInfo.numFields() > 0)
        InsnOperands.push_back(OpInfo);
    }
  }
  Operands[Opc] = InsnOperands;

#if 0
  LLVM_DEBUG({
      // Dumps the instruction encoding bits.
      dumpBits(errs(), Bits);

      errs() << '\n';

      // Dumps the list of operand info.
      for (unsigned i = 0, e = CGI.Operands.size(); i != e; ++i) {
        const CGIOperandList::OperandInfo &Info = CGI.Operands[i];
        const std::string &OperandName = Info.Name;
        const Record &OperandDef = *Info.Rec;

        errs() << "\t" << OperandName << " (" << OperandDef.getName() << ")\n";
      }
    });
#endif

  return Bits.getNumBits();
}

// emitFieldFromInstruction - Emit the templated helper function
// fieldFromInstruction().
// On Windows we make sure that this function is not inlined when
// using the VS compiler. It has a bug which causes the function
// to be optimized out in some circumstances. See llvm.org/pr38292
static void emitFieldFromInstruction(formatted_raw_ostream &OS) {
  OS << R"(
// Helper functions for extracting fields from encoded instructions.
// InsnType must either be integral or an APInt-like object that must:
// * be default-constructible and copy-constructible
// * be constructible from an APInt (this can be private)
// * Support insertBits(bits, startBit, numBits)
// * Support extractBitsAsZExtValue(numBits, startBit)
// * Support the ~, &, ==, and != operators with other objects of the same type
// * Support the != and bitwise & with uint64_t
// * Support put (<<) to raw_ostream&
template <typename InsnType>
#if defined(_MSC_VER) && !defined(__clang__)
__declspec(noinline)
#endif
static std::enable_if_t<std::is_integral<InsnType>::value, InsnType>
fieldFromInstruction(const InsnType &insn, unsigned startBit,
                     unsigned numBits) {
  assert(startBit + numBits <= 64 && "Cannot support >64-bit extractions!");
  assert(startBit + numBits <= (sizeof(InsnType) * 8) &&
         "Instruction field out of bounds!");
  InsnType fieldMask;
  if (numBits == sizeof(InsnType) * 8)
    fieldMask = (InsnType)(-1LL);
  else
    fieldMask = (((InsnType)1 << numBits) - 1) << startBit;
  return (insn & fieldMask) >> startBit;
}

template <typename InsnType>
static std::enable_if_t<!std::is_integral<InsnType>::value, uint64_t>
fieldFromInstruction(const InsnType &insn, unsigned startBit,
                     unsigned numBits) {
  return insn.extractBitsAsZExtValue(numBits, startBit);
}
)";
}

// emitInsertBits - Emit the templated helper function insertBits().
static void emitInsertBits(formatted_raw_ostream &OS) {
  OS << R"(
// Helper function for inserting bits extracted from an encoded instruction into
// a field.
template <typename InsnType>
static std::enable_if_t<std::is_integral<InsnType>::value>
insertBits(InsnType &field, InsnType bits, unsigned startBit, unsigned numBits) {
  assert(startBit + numBits <= sizeof field * 8);
  field |= (InsnType)bits << startBit;
}

template <typename InsnType>
static std::enable_if_t<!std::is_integral<InsnType>::value>
insertBits(InsnType &field, uint64_t bits, unsigned startBit, unsigned numBits) {
  field.insertBits(bits, startBit, numBits);
}
)";
}

// emitDecodeInstruction - Emit the templated helper function
// decodeInstruction().
static void emitDecodeInstruction(formatted_raw_ostream &OS,
                                  bool IsVarLenInst) {
  OS << R"(
template <typename InsnType>
static DecodeStatus decodeInstruction(const uint8_t DecodeTable[], MCInst &MI,
                                      InsnType insn, uint64_t Address,
                                      const MCDisassembler *DisAsm,
                                      const MCSubtargetInfo &STI)";
  if (IsVarLenInst) {
    OS << ",\n                                      "
          "llvm::function_ref<void(APInt &, uint64_t)> makeUp";
  }
  OS << R"() {
  const FeatureBitset &Bits = STI.getFeatureBits();

  const uint8_t *Ptr = DecodeTable;
  uint64_t CurFieldValue = 0;
  DecodeStatus S = MCDisassembler::Success;
  while (true) {
    ptrdiff_t Loc = Ptr - DecodeTable;
    switch (*Ptr) {
    default:
      errs() << Loc << ": Unexpected decode table opcode!\n";
      return MCDisassembler::Fail;
    case MCD::OPC_ExtractField: {
      // Decode the start value.
      unsigned Start = decodeULEB128AndIncUnsafe(++Ptr);
      unsigned Len = *Ptr++;)";
  if (IsVarLenInst)
    OS << "\n      makeUp(insn, Start + Len);";
  OS << R"(
      CurFieldValue = fieldFromInstruction(insn, Start, Len);
      LLVM_DEBUG(dbgs() << Loc << ": OPC_ExtractField(" << Start << ", "
                   << Len << "): " << CurFieldValue << "\n");
      break;
    }
    case MCD::OPC_FilterValue: {
      // Decode the field value.
      uint64_t Val = decodeULEB128AndIncUnsafe(++Ptr);
      // NumToSkip is a plain 24-bit integer.
      unsigned NumToSkip = *Ptr++;
      NumToSkip |= (*Ptr++) << 8;
      NumToSkip |= (*Ptr++) << 16;

      // Perform the filter operation.
      if (Val != CurFieldValue)
        Ptr += NumToSkip;
      LLVM_DEBUG(dbgs() << Loc << ": OPC_FilterValue(" << Val << ", " << NumToSkip
                   << "): " << ((Val != CurFieldValue) ? "FAIL:" : "PASS:")
                   << " continuing at " << (Ptr - DecodeTable) << "\n");

      break;
    }
    case MCD::OPC_CheckField: {
      // Decode the start value.
      unsigned Start = decodeULEB128AndIncUnsafe(++Ptr);
      unsigned Len = *Ptr;)";
  if (IsVarLenInst)
    OS << "\n      makeUp(insn, Start + Len);";
  OS << R"(
      uint64_t FieldValue = fieldFromInstruction(insn, Start, Len);
      // Decode the field value.
      unsigned PtrLen = 0;
      uint64_t ExpectedValue = decodeULEB128(++Ptr, &PtrLen);
      Ptr += PtrLen;
      // NumToSkip is a plain 24-bit integer.
      unsigned NumToSkip = *Ptr++;
      NumToSkip |= (*Ptr++) << 8;
      NumToSkip |= (*Ptr++) << 16;

      // If the actual and expected values don't match, skip.
      if (ExpectedValue != FieldValue)
        Ptr += NumToSkip;
      LLVM_DEBUG(dbgs() << Loc << ": OPC_CheckField(" << Start << ", "
                   << Len << ", " << ExpectedValue << ", " << NumToSkip
                   << "): FieldValue = " << FieldValue << ", ExpectedValue = "
                   << ExpectedValue << ": "
                   << ((ExpectedValue == FieldValue) ? "PASS\n" : "FAIL\n"));
      break;
    }
    case MCD::OPC_CheckPredicate: {
      // Decode the Predicate Index value.
      unsigned PIdx = decodeULEB128AndIncUnsafe(++Ptr);
      // NumToSkip is a plain 24-bit integer.
      unsigned NumToSkip = *Ptr++;
      NumToSkip |= (*Ptr++) << 8;
      NumToSkip |= (*Ptr++) << 16;
      // Check the predicate.
      bool Pred;
      if (!(Pred = checkDecoderPredicate(PIdx, Bits)))
        Ptr += NumToSkip;
      (void)Pred;
      LLVM_DEBUG(dbgs() << Loc << ": OPC_CheckPredicate(" << PIdx << "): "
            << (Pred ? "PASS\n" : "FAIL\n"));

      break;
    }
    case MCD::OPC_Decode: {
      // Decode the Opcode value.
      unsigned Opc = decodeULEB128AndIncUnsafe(++Ptr);
      unsigned DecodeIdx = decodeULEB128AndIncUnsafe(Ptr);

      MI.clear();
      MI.setOpcode(Opc);
      bool DecodeComplete;)";
  if (IsVarLenInst) {
    OS << "\n      unsigned Len = InstrLenTable[Opc];\n"
       << "      makeUp(insn, Len);";
  }
  OS << R"(
      S = decodeToMCInst(S, DecodeIdx, insn, MI, Address, DisAsm, DecodeComplete);
      assert(DecodeComplete);

      LLVM_DEBUG(dbgs() << Loc << ": OPC_Decode: opcode " << Opc
                   << ", using decoder " << DecodeIdx << ": "
                   << (S != MCDisassembler::Fail ? "PASS" : "FAIL") << "\n");
      return S;
    }
    case MCD::OPC_TryDecode: {
      // Decode the Opcode value.
      unsigned Opc = decodeULEB128AndIncUnsafe(++Ptr);
      unsigned DecodeIdx = decodeULEB128AndIncUnsafe(Ptr);
      // NumToSkip is a plain 24-bit integer.
      unsigned NumToSkip = *Ptr++;
      NumToSkip |= (*Ptr++) << 8;
      NumToSkip |= (*Ptr++) << 16;

      // Perform the decode operation.
      MCInst TmpMI;
      TmpMI.setOpcode(Opc);
      bool DecodeComplete;
      S = decodeToMCInst(S, DecodeIdx, insn, TmpMI, Address, DisAsm, DecodeComplete);
      LLVM_DEBUG(dbgs() << Loc << ": OPC_TryDecode: opcode " << Opc
                   << ", using decoder " << DecodeIdx << ": ");

      if (DecodeComplete) {
        // Decoding complete.
        LLVM_DEBUG(dbgs() << (S != MCDisassembler::Fail ? "PASS" : "FAIL") << "\n");
        MI = TmpMI;
        return S;
      } else {
        assert(S == MCDisassembler::Fail);
        // If the decoding was incomplete, skip.
        Ptr += NumToSkip;
        LLVM_DEBUG(dbgs() << "FAIL: continuing at " << (Ptr - DecodeTable) << "\n");
        // Reset decode status. This also drops a SoftFail status that could be
        // set before the decode attempt.
        S = MCDisassembler::Success;
      }
      break;
    }
    case MCD::OPC_SoftFail: {
      // Decode the mask values.
      uint64_t PositiveMask = decodeULEB128AndIncUnsafe(++Ptr);
      uint64_t NegativeMask = decodeULEB128AndIncUnsafe(Ptr);
      bool Fail = (insn & PositiveMask) != 0 || (~insn & NegativeMask) != 0;
      if (Fail)
        S = MCDisassembler::SoftFail;
      LLVM_DEBUG(dbgs() << Loc << ": OPC_SoftFail: " << (Fail ? "FAIL\n" : "PASS\n"));
      break;
    }
    case MCD::OPC_Fail: {
      LLVM_DEBUG(dbgs() << Loc << ": OPC_Fail\n");
      return MCDisassembler::Fail;
    }
    }
  }
  llvm_unreachable("bogosity detected in disassembler state machine!");
}

)";
}

// Helper to propagate SoftFail status. Returns false if the status is Fail;
// callers are expected to early-exit in that condition. (Note, the '&' operator
// is correct to propagate the values of this enum; see comment on 'enum
// DecodeStatus'.)
static void emitCheck(formatted_raw_ostream &OS) {
  OS << R"(
static bool Check(DecodeStatus &Out, DecodeStatus In) {
  Out = static_cast<DecodeStatus>(Out & In);
  return Out != MCDisassembler::Fail;
}

)";
}

// Collect all HwModes referenced by the target for encoding purposes,
// returning a vector of corresponding names.
static void collectHwModesReferencedForEncodings(
    const CodeGenHwModes &HWM, std::vector<StringRef> &Names,
    NamespacesHwModesMap &NamespacesWithHwModes) {
  SmallBitVector BV(HWM.getNumModeIds());
  for (const auto &MS : HWM.getHwModeSelects()) {
    for (const HwModeSelect::PairType &P : MS.second.Items) {
      if (P.second->isSubClassOf("InstructionEncoding")) {
        std::string DecoderNamespace =
            std::string(P.second->getValueAsString("DecoderNamespace"));
        if (P.first == DefaultMode) {
          NamespacesWithHwModes[DecoderNamespace].insert("");
        } else {
          NamespacesWithHwModes[DecoderNamespace].insert(
              HWM.getMode(P.first).Name);
        }
        BV.set(P.first);
      }
    }
  }
  transform(BV.set_bits(), std::back_inserter(Names), [&HWM](const int &M) {
    if (M == DefaultMode)
      return StringRef("");
    return HWM.getModeName(M, /*IncludeDefault=*/true);
  });
}

static void
handleHwModesUnrelatedEncodings(const CodeGenInstruction *Instr,
                                const std::vector<StringRef> &HwModeNames,
                                NamespacesHwModesMap &NamespacesWithHwModes,
                                std::vector<EncodingAndInst> &GlobalEncodings) {
  const Record *InstDef = Instr->TheDef;

  switch (DecoderEmitterSuppressDuplicates) {
  case SUPPRESSION_DISABLE: {
    for (StringRef HwModeName : HwModeNames)
      GlobalEncodings.emplace_back(InstDef, Instr, HwModeName);
    break;
  }
  case SUPPRESSION_LEVEL1: {
    std::string DecoderNamespace =
        std::string(InstDef->getValueAsString("DecoderNamespace"));
    auto It = NamespacesWithHwModes.find(DecoderNamespace);
    if (It != NamespacesWithHwModes.end()) {
      for (StringRef HwModeName : It->second)
        GlobalEncodings.emplace_back(InstDef, Instr, HwModeName);
    } else {
      // Only emit the encoding once, as it's DecoderNamespace doesn't
      // contain any HwModes.
      GlobalEncodings.emplace_back(InstDef, Instr, "");
    }
    break;
  }
  case SUPPRESSION_LEVEL2:
    GlobalEncodings.emplace_back(InstDef, Instr, "");
    break;
  }
}

// Emits disassembler code for instruction decoding.
void DecoderEmitter::run(raw_ostream &o) {
  formatted_raw_ostream OS(o);
  OS << R"(
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <assert.h>

namespace llvm {
)";

  emitFieldFromInstruction(OS);
  emitInsertBits(OS);
  emitCheck(OS);

  Target.reverseBitsForLittleEndianEncoding();

  // Parameterize the decoders based on namespace and instruction width.

  // First, collect all encoding-related HwModes referenced by the target.
  // And establish a mapping table between DecoderNamespace and HwMode.
  // If HwModeNames is empty, add the empty string so we always have one HwMode.
  const CodeGenHwModes &HWM = Target.getHwModes();
  std::vector<StringRef> HwModeNames;
  NamespacesHwModesMap NamespacesWithHwModes;
  collectHwModesReferencedForEncodings(HWM, HwModeNames, NamespacesWithHwModes);
  if (HwModeNames.empty())
    HwModeNames.push_back("");

  const auto &NumberedInstructions = Target.getInstructionsByEnumValue();
  NumberedEncodings.reserve(NumberedInstructions.size());
  for (const auto &NumberedInstruction : NumberedInstructions) {
    const Record *InstDef = NumberedInstruction->TheDef;
    if (const RecordVal *RV = InstDef->getValue("EncodingInfos")) {
      if (DefInit *DI = dyn_cast_or_null<DefInit>(RV->getValue())) {
        EncodingInfoByHwMode EBM(DI->getDef(), HWM);
        for (auto &[ModeId, Encoding] : EBM) {
          // DecoderTables with DefaultMode should not have any suffix.
          if (ModeId == DefaultMode) {
            NumberedEncodings.emplace_back(Encoding, NumberedInstruction, "");
          } else {
            NumberedEncodings.emplace_back(Encoding, NumberedInstruction,
                                           HWM.getMode(ModeId).Name);
          }
        }
        continue;
      }
    }
    // This instruction is encoded the same on all HwModes.
    // According to user needs, provide varying degrees of suppression.
    handleHwModesUnrelatedEncodings(NumberedInstruction, HwModeNames,
                                    NamespacesWithHwModes, NumberedEncodings);
  }
  for (const auto &NumberedAlias :
       RK.getAllDerivedDefinitions("AdditionalEncoding"))
    NumberedEncodings.emplace_back(
        NumberedAlias,
        &Target.getInstruction(NumberedAlias->getValueAsDef("AliasOf")));

  std::map<std::pair<std::string, unsigned>, std::vector<EncodingIDAndOpcode>>
      OpcMap;
  std::map<unsigned, std::vector<OperandInfo>> Operands;
  std::vector<unsigned> InstrLen;
  bool IsVarLenInst = Target.hasVariableLengthEncodings();
  unsigned MaxInstLen = 0;

  for (const auto &[NEI, NumberedEncoding] : enumerate(NumberedEncodings)) {
    const Record *EncodingDef = NumberedEncoding.EncodingDef;
    const CodeGenInstruction *Inst = NumberedEncoding.Inst;
    const Record *Def = Inst->TheDef;
    unsigned Size = EncodingDef->getValueAsInt("Size");
    if (Def->getValueAsString("Namespace") == "TargetOpcode" ||
        Def->getValueAsBit("isPseudo") ||
        Def->getValueAsBit("isAsmParserOnly") ||
        Def->getValueAsBit("isCodeGenOnly")) {
      NumEncodingsLackingDisasm++;
      continue;
    }

    if (NEI < NumberedInstructions.size())
      NumInstructions++;
    NumEncodings++;

    if (!Size && !IsVarLenInst)
      continue;

    if (IsVarLenInst)
      InstrLen.resize(NumberedInstructions.size(), 0);

    if (unsigned Len = populateInstruction(Target, *EncodingDef, *Inst, NEI,
                                           Operands, IsVarLenInst)) {
      if (IsVarLenInst) {
        MaxInstLen = std::max(MaxInstLen, Len);
        InstrLen[NEI] = Len;
      }
      std::string DecoderNamespace =
          std::string(EncodingDef->getValueAsString("DecoderNamespace"));
      if (!NumberedEncoding.HwModeName.empty())
        DecoderNamespace +=
            std::string("_") + NumberedEncoding.HwModeName.str();
      OpcMap[std::pair(DecoderNamespace, Size)].emplace_back(
          NEI, Target.getInstrIntValue(Def));
    } else {
      NumEncodingsOmitted++;
    }
  }

  DecoderTableInfo TableInfo;
  for (const auto &Opc : OpcMap) {
    // Emit the decoder for this namespace+width combination.
    ArrayRef<EncodingAndInst> NumberedEncodingsRef(NumberedEncodings.data(),
                                                   NumberedEncodings.size());
    FilterChooser FC(NumberedEncodingsRef, Opc.second, Operands,
                     IsVarLenInst ? MaxInstLen : 8 * Opc.first.second, this);

    // The decode table is cleared for each top level decoder function. The
    // predicates and decoders themselves, however, are shared across all
    // decoders to give more opportunities for uniqueing.
    TableInfo.Table.clear();
    TableInfo.FixupStack.clear();
    TableInfo.Table.reserve(16384);
    TableInfo.FixupStack.emplace_back();
    FC.emitTableEntries(TableInfo);
    // Any NumToSkip fixups in the top level scope can resolve to the
    // OPC_Fail at the end of the table.
    assert(TableInfo.FixupStack.size() == 1 && "fixup stack phasing error!");
    // Resolve any NumToSkip fixups in the current scope.
    resolveTableFixups(TableInfo.Table, TableInfo.FixupStack.back(),
                       TableInfo.Table.size());
    TableInfo.FixupStack.clear();

    TableInfo.Table.push_back(MCD::OPC_Fail);

    // Print the table to the output stream.
    emitTable(OS, TableInfo.Table, 0, FC.getBitWidth(), Opc.first.first,
              Opc.second);
  }

  // For variable instruction, we emit a instruction length table
  // to let the decoder know how long the instructions are.
  // You can see example usage in M68k's disassembler.
  if (IsVarLenInst)
    emitInstrLenTable(OS, InstrLen);
  // Emit the predicate function.
  emitPredicateFunction(OS, TableInfo.Predicates, 0);

  // Emit the decoder function.
  emitDecoderFunction(OS, TableInfo.Decoders, 0);

  // Emit the main entry point for the decoder, decodeInstruction().
  emitDecodeInstruction(OS, IsVarLenInst);

  OS << "\n} // end namespace llvm\n";
}

namespace llvm {

void EmitDecoder(RecordKeeper &RK, raw_ostream &OS,
                 const std::string &PredicateNamespace) {
  DecoderEmitter(RK, PredicateNamespace).run(OS);
}

} // end namespace llvm
