//===- MemoryModelRelaxationAnnotations.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides utility for Memory Model Relaxation Annotations (MMRAs).
/// Those annotations are represented using Metadata. The MMRATagSet class
/// offers a simple API to parse the metadata and perform common operations on
/// it. The MMRAMetadata class is a simple tuple of MDNode that provides easy
/// access to all MMRA annotations on an instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MEMORYMODELRELAXATIONANNOTATIONS_H
#define LLVM_IR_MEMORYMODELRELAXATIONANNOTATIONS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include <tuple> // for std::pair

namespace llvm {

template <typename T> class ArrayRef;

class MDNode;
class MDTuple;
class Metadata;
class raw_ostream;
class LLVMContext;
class Instruction;

/// Helper class to manipulate `!mmra` metadata nodes.
///
/// This can be visualized as a set of "tags", with each tag
/// representing a particular property of an instruction, as
/// explained in the MemoryModelRelaxationAnnotations docs.
///
/// This class (and the optimizer in general) does not reason
/// about the exact nature of the tags and the properties they
/// imply. It just sees the metadata as a collection of tags, which
/// are a prefix/suffix pair of strings.
class MMRAMetadata {
public:
  using TagT = std::pair<StringRef, StringRef>;
  using SetT = DenseSet<TagT>;
  using const_iterator = SetT::const_iterator;

  /// \name Constructors
  /// @{
  MMRAMetadata() = default;
  MMRAMetadata(const Instruction &I);
  MMRAMetadata(MDNode *MD);
  /// @}

  /// \name Metadata Helpers & Builders
  /// @{

  /// Combines \p A and \p B according to MMRA semantics.
  /// \returns !mmra metadata for the combined MMRAs.
  static MDNode *combine(LLVMContext &Ctx, const MMRAMetadata &A,
                         const MMRAMetadata &B);

  /// Creates !mmra metadata for a single tag.
  ///
  /// !mmra metadata can either be a single tag, or a MDTuple containing
  /// multiple tags.
  static MDTuple *getTagMD(LLVMContext &Ctx, StringRef Prefix,
                           StringRef Suffix);
  static MDTuple *getTagMD(LLVMContext &Ctx, const TagT &T) {
    return getTagMD(Ctx, T.first, T.second);
  }

  /// Creates !mmra metadata from \p Tags.
  /// \returns nullptr or a MDTuple* from \p Tags.
  static MDTuple *getMD(LLVMContext &Ctx, ArrayRef<TagT> Tags);

  /// \returns true if \p MD is a well-formed MMRA tag.
  static bool isTagMD(const Metadata *MD);

  /// @}

  /// \name Compatibility Helpers
  /// @{

  /// \returns whether the MMRAs on \p A and \p B are compatible.
  static bool checkCompatibility(const Instruction &A, const Instruction &B) {
    return MMRAMetadata(A).isCompatibleWith(B);
  }

  /// \returns whether this set of tags is compatible with \p Other.
  bool isCompatibleWith(const MMRAMetadata &Other) const;

  /// @}

  /// \name Content Queries
  /// @{

  bool hasTag(StringRef Prefix, StringRef Suffix) const;
  bool hasTagWithPrefix(StringRef Prefix) const;

  const_iterator begin() const;
  const_iterator end() const;
  bool empty() const;
  unsigned size() const;

  /// @}

  void print(raw_ostream &OS) const;
  void dump() const;

  operator bool() const { return !Tags.empty(); }
  bool operator==(const MMRAMetadata &Other) const {
    return Tags == Other.Tags;
  }
  bool operator!=(const MMRAMetadata &Other) const {
    return Tags != Other.Tags;
  }

private:
  SetT Tags;
};

/// \returns true if \p I can have !mmra metadata.
bool canInstructionHaveMMRAs(const Instruction &I);

} // namespace llvm

#endif
