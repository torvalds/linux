//===--- SourceLocationEncoding.h - Small serialized locations --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// We wish to encode the SourceLocation from other module file not dependent
// on the other module file. So that the source location changes from other
// module file may not affect the contents of the current module file. Then the
// users don't need to recompile the whole project due to a new line in a module
// unit in the root of the dependency graph.
//
// To achieve this, we need to encode the index of the module file into the
// encoding of the source location. The encoding of the source location may be:
//
//      |-----------------------|-----------------------|
//      |          A            |         B         | C |
//
//  * A: 32 bit. The index of the module file in the module manager + 1. The +1
//  here is necessary since we wish 0 stands for the current module file.
//  * B: 31 bit. The offset of the source location to the module file containing
//  it.
//  * C: The macro bit. We rotate it to the lowest bit so that we can save some
//  space in case the index of the module file is 0.
//
// Specially, if the index of the module file is 0, we allow to encode a
// sequence of locations we store only differences between successive elements.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceLocation.h"
#include "llvm/Support/MathExtras.h"
#include <climits>

#ifndef LLVM_CLANG_SERIALIZATION_SOURCELOCATIONENCODING_H
#define LLVM_CLANG_SERIALIZATION_SOURCELOCATIONENCODING_H

namespace clang {
class SourceLocationSequence;

/// Serialized encoding of SourceLocations without context.
/// Optimized to have small unsigned values (=> small after VBR encoding).
///
// Macro locations have the top bit set, we rotate by one so it is the low bit.
class SourceLocationEncoding {
  using UIntTy = SourceLocation::UIntTy;
  constexpr static unsigned UIntBits = CHAR_BIT * sizeof(UIntTy);

  static UIntTy encodeRaw(UIntTy Raw) {
    return (Raw << 1) | (Raw >> (UIntBits - 1));
  }
  static UIntTy decodeRaw(UIntTy Raw) {
    return (Raw >> 1) | (Raw << (UIntBits - 1));
  }
  friend SourceLocationSequence;

public:
  using RawLocEncoding = uint64_t;

  static RawLocEncoding encode(SourceLocation Loc, UIntTy BaseOffset,
                               unsigned BaseModuleFileIndex,
                               SourceLocationSequence * = nullptr);
  static std::pair<SourceLocation, unsigned>
  decode(RawLocEncoding, SourceLocationSequence * = nullptr);
};

/// Serialized encoding of a sequence of SourceLocations.
///
/// Optimized to produce small values when locations with the sequence are
/// similar. Each element can be delta-encoded against the last nonzero element.
///
/// Sequences should be started by creating a SourceLocationSequence::State,
/// and then passed around as SourceLocationSequence*. Example:
///
///   // establishes a sequence
///   void EmitTopLevelThing() {
///     SourceLocationSequence::State Seq;
///     EmitContainedThing(Seq);
///     EmitRecursiveThing(Seq);
///   }
///
///   // optionally part of a sequence
///   void EmitContainedThing(SourceLocationSequence *Seq = nullptr) {
///     Record.push_back(SourceLocationEncoding::encode(SomeLoc, Seq));
///   }
///
///   // establishes a sequence if there isn't one already
///   void EmitRecursiveThing(SourceLocationSequence *ParentSeq = nullptr) {
///     SourceLocationSequence::State Seq(ParentSeq);
///     Record.push_back(SourceLocationEncoding::encode(SomeLoc, Seq));
///     EmitRecursiveThing(Seq);
///   }
///
class SourceLocationSequence {
  using UIntTy = SourceLocation::UIntTy;
  using EncodedTy = uint64_t;
  constexpr static auto UIntBits = SourceLocationEncoding::UIntBits;
  static_assert(sizeof(EncodedTy) > sizeof(UIntTy), "Need one extra bit!");

  // Prev stores the rotated last nonzero location.
  UIntTy &Prev;

  // Zig-zag encoding turns small signed integers into small unsigned integers.
  // 0 => 0, -1 => 1, 1 => 2, -2 => 3, ...
  static UIntTy zigZag(UIntTy V) {
    UIntTy Sign = (V & (1 << (UIntBits - 1))) ? UIntTy(-1) : UIntTy(0);
    return Sign ^ (V << 1);
  }
  static UIntTy zagZig(UIntTy V) { return (V >> 1) ^ -(V & 1); }

  SourceLocationSequence(UIntTy &Prev) : Prev(Prev) {}

  EncodedTy encodeRaw(UIntTy Raw) {
    if (Raw == 0)
      return 0;
    UIntTy Rotated = SourceLocationEncoding::encodeRaw(Raw);
    if (Prev == 0)
      return Prev = Rotated;
    UIntTy Delta = Rotated - Prev;
    Prev = Rotated;
    // Exactly one 33 bit value is possible! (1 << 32).
    // This is because we have two representations of zero: trivial & relative.
    return 1 + EncodedTy{zigZag(Delta)};
  }
  UIntTy decodeRaw(EncodedTy Encoded) {
    if (Encoded == 0)
      return 0;
    if (Prev == 0)
      return SourceLocationEncoding::decodeRaw(Prev = Encoded);
    return SourceLocationEncoding::decodeRaw(Prev += zagZig(Encoded - 1));
  }

public:
  SourceLocation decode(EncodedTy Encoded) {
    return SourceLocation::getFromRawEncoding(decodeRaw(Encoded));
  }
  EncodedTy encode(SourceLocation Loc) {
    return encodeRaw(Loc.getRawEncoding());
  }

  class State;
};

/// This object establishes a SourceLocationSequence.
class SourceLocationSequence::State {
  UIntTy Prev = 0;
  SourceLocationSequence Seq;

public:
  // If Parent is provided and non-null, then this root becomes part of that
  // enclosing sequence instead of establishing a new one.
  State(SourceLocationSequence *Parent = nullptr)
      : Seq(Parent ? Parent->Prev : Prev) {}

  // Implicit conversion for uniform use of roots vs propagated sequences.
  operator SourceLocationSequence *() { return &Seq; }
};

inline SourceLocationEncoding::RawLocEncoding
SourceLocationEncoding::encode(SourceLocation Loc, UIntTy BaseOffset,
                               unsigned BaseModuleFileIndex,
                               SourceLocationSequence *Seq) {
  // If the source location is a local source location, we can try to optimize
  // the similar sequences to only record the differences.
  if (!BaseOffset)
    return Seq ? Seq->encode(Loc) : encodeRaw(Loc.getRawEncoding());

  if (Loc.isInvalid())
    return 0;

  // Otherwise, the higher bits are used to store the module file index,
  // so it is meaningless to optimize the source locations into small
  // integers. Let's try to always use the raw encodings.
  assert(Loc.getOffset() >= BaseOffset);
  Loc = Loc.getLocWithOffset(-BaseOffset);
  RawLocEncoding Encoded = encodeRaw(Loc.getRawEncoding());

  // 16 bits should be sufficient to store the module file index.
  assert(BaseModuleFileIndex < (1 << 16));
  Encoded |= (RawLocEncoding)BaseModuleFileIndex << 32;
  return Encoded;
}
inline std::pair<SourceLocation, unsigned>
SourceLocationEncoding::decode(RawLocEncoding Encoded,
                               SourceLocationSequence *Seq) {
  unsigned ModuleFileIndex = Encoded >> 32;

  if (!ModuleFileIndex)
    return {Seq ? Seq->decode(Encoded)
                : SourceLocation::getFromRawEncoding(decodeRaw(Encoded)),
            ModuleFileIndex};

  Encoded &= llvm::maskTrailingOnes<RawLocEncoding>(32);
  SourceLocation Loc = SourceLocation::getFromRawEncoding(decodeRaw(Encoded));

  return {Loc, ModuleFileIndex};
}

} // namespace clang
#endif
