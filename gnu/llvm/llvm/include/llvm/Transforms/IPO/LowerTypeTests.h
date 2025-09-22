//===- LowerTypeTests.h - type metadata lowering pass -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines parts of the type test lowering pass implementation that
// may be usefully unit tested.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_LOWERTYPETESTS_H
#define LLVM_TRANSFORMS_IPO_LOWERTYPETESTS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

namespace llvm {

class Module;
class ModuleSummaryIndex;
class raw_ostream;

namespace lowertypetests {

struct BitSetInfo {
  // The indices of the set bits in the bitset.
  std::set<uint64_t> Bits;

  // The byte offset into the combined global represented by the bitset.
  uint64_t ByteOffset;

  // The size of the bitset in bits.
  uint64_t BitSize;

  // Log2 alignment of the bit set relative to the combined global.
  // For example, a log2 alignment of 3 means that bits in the bitset
  // represent addresses 8 bytes apart.
  unsigned AlignLog2;

  bool isSingleOffset() const {
    return Bits.size() == 1;
  }

  bool isAllOnes() const {
    return Bits.size() == BitSize;
  }

  bool containsGlobalOffset(uint64_t Offset) const;

  void print(raw_ostream &OS) const;
};

struct BitSetBuilder {
  SmallVector<uint64_t, 16> Offsets;
  uint64_t Min = std::numeric_limits<uint64_t>::max();
  uint64_t Max = 0;

  BitSetBuilder() = default;

  void addOffset(uint64_t Offset) {
    if (Min > Offset)
      Min = Offset;
    if (Max < Offset)
      Max = Offset;

    Offsets.push_back(Offset);
  }

  BitSetInfo build();
};

/// This class implements a layout algorithm for globals referenced by bit sets
/// that tries to keep members of small bit sets together. This can
/// significantly reduce bit set sizes in many cases.
///
/// It works by assembling fragments of layout from sets of referenced globals.
/// Each set of referenced globals causes the algorithm to create a new
/// fragment, which is assembled by appending each referenced global in the set
/// into the fragment. If a referenced global has already been referenced by an
/// fragment created earlier, we instead delete that fragment and append its
/// contents into the fragment we are assembling.
///
/// By starting with the smallest fragments, we minimize the size of the
/// fragments that are copied into larger fragments. This is most intuitively
/// thought about when considering the case where the globals are virtual tables
/// and the bit sets represent their derived classes: in a single inheritance
/// hierarchy, the optimum layout would involve a depth-first search of the
/// class hierarchy (and in fact the computed layout ends up looking a lot like
/// a DFS), but a naive DFS would not work well in the presence of multiple
/// inheritance. This aspect of the algorithm ends up fitting smaller
/// hierarchies inside larger ones where that would be beneficial.
///
/// For example, consider this class hierarchy:
///
/// A       B
///   \   / | \
///     C   D   E
///
/// We have five bit sets: bsA (A, C), bsB (B, C, D, E), bsC (C), bsD (D) and
/// bsE (E). If we laid out our objects by DFS traversing B followed by A, our
/// layout would be {B, C, D, E, A}. This is optimal for bsB as it needs to
/// cover the only 4 objects in its hierarchy, but not for bsA as it needs to
/// cover 5 objects, i.e. the entire layout. Our algorithm proceeds as follows:
///
/// Add bsC, fragments {{C}}
/// Add bsD, fragments {{C}, {D}}
/// Add bsE, fragments {{C}, {D}, {E}}
/// Add bsA, fragments {{A, C}, {D}, {E}}
/// Add bsB, fragments {{B, A, C, D, E}}
///
/// This layout is optimal for bsA, as it now only needs to cover two (i.e. 3
/// fewer) objects, at the cost of bsB needing to cover 1 more object.
///
/// The bit set lowering pass assigns an object index to each object that needs
/// to be laid out, and calls addFragment for each bit set passing the object
/// indices of its referenced globals. It then assembles a layout from the
/// computed layout in the Fragments field.
struct GlobalLayoutBuilder {
  /// The computed layout. Each element of this vector contains a fragment of
  /// layout (which may be empty) consisting of object indices.
  std::vector<std::vector<uint64_t>> Fragments;

  /// Mapping from object index to fragment index.
  std::vector<uint64_t> FragmentMap;

  GlobalLayoutBuilder(uint64_t NumObjects)
      : Fragments(1), FragmentMap(NumObjects) {}

  /// Add F to the layout while trying to keep its indices contiguous.
  /// If a previously seen fragment uses any of F's indices, that
  /// fragment will be laid out inside F.
  void addFragment(const std::set<uint64_t> &F);
};

/// This class is used to build a byte array containing overlapping bit sets. By
/// loading from indexed offsets into the byte array and applying a mask, a
/// program can test bits from the bit set with a relatively short instruction
/// sequence. For example, suppose we have 15 bit sets to lay out:
///
/// A (16 bits), B (15 bits), C (14 bits), D (13 bits), E (12 bits),
/// F (11 bits), G (10 bits), H (9 bits), I (7 bits), J (6 bits), K (5 bits),
/// L (4 bits), M (3 bits), N (2 bits), O (1 bit)
///
/// These bits can be laid out in a 16-byte array like this:
///
///       Byte Offset
///     0123456789ABCDEF
/// Bit
///   7 HHHHHHHHHIIIIIII
///   6 GGGGGGGGGGJJJJJJ
///   5 FFFFFFFFFFFKKKKK
///   4 EEEEEEEEEEEELLLL
///   3 DDDDDDDDDDDDDMMM
///   2 CCCCCCCCCCCCCCNN
///   1 BBBBBBBBBBBBBBBO
///   0 AAAAAAAAAAAAAAAA
///
/// For example, to test bit X of A, we evaluate ((bits[X] & 1) != 0), or to
/// test bit X of I, we evaluate ((bits[9 + X] & 0x80) != 0). This can be done
/// in 1-2 machine instructions on x86, or 4-6 instructions on ARM.
///
/// This is a byte array, rather than (say) a 2-byte array or a 4-byte array,
/// because for one thing it gives us better packing (the more bins there are,
/// the less evenly they will be filled), and for another, the instruction
/// sequences can be slightly shorter, both on x86 and ARM.
struct ByteArrayBuilder {
  /// The byte array built so far.
  std::vector<uint8_t> Bytes;

  enum { BitsPerByte = 8 };

  /// The number of bytes allocated so far for each of the bits.
  uint64_t BitAllocs[BitsPerByte];

  ByteArrayBuilder() {
    memset(BitAllocs, 0, sizeof(BitAllocs));
  }

  /// Allocate BitSize bits in the byte array where Bits contains the bits to
  /// set. AllocByteOffset is set to the offset within the byte array and
  /// AllocMask is set to the bitmask for those bits. This uses the LPT (Longest
  /// Processing Time) multiprocessor scheduling algorithm to lay out the bits
  /// efficiently; the pass allocates bit sets in decreasing size order.
  void allocate(const std::set<uint64_t> &Bits, uint64_t BitSize,
                uint64_t &AllocByteOffset, uint8_t &AllocMask);
};

bool isJumpTableCanonical(Function *F);

} // end namespace lowertypetests

class LowerTypeTestsPass : public PassInfoMixin<LowerTypeTestsPass> {
  bool UseCommandLine = false;

  ModuleSummaryIndex *ExportSummary = nullptr;
  const ModuleSummaryIndex *ImportSummary = nullptr;
  bool DropTypeTests = true;

public:
  LowerTypeTestsPass() : UseCommandLine(true) {}
  LowerTypeTestsPass(ModuleSummaryIndex *ExportSummary,
                     const ModuleSummaryIndex *ImportSummary,
                     bool DropTypeTests = false)
      : ExportSummary(ExportSummary), ImportSummary(ImportSummary),
        DropTypeTests(DropTypeTests) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_LOWERTYPETESTS_H
