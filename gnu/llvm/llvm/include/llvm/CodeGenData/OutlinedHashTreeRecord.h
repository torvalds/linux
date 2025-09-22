//===- OutlinedHashTreeRecord.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This defines the OutlinedHashTreeRecord class. This class holds the outlined
// hash tree for both serialization and deserialization processes. It utilizes
// two data formats for serialization: raw binary data and YAML.
// These two formats can be used interchangeably.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_CODEGENDATA_OUTLINEDHASHTREERECORD_H
#define LLVM_CODEGENDATA_OUTLINEDHASHTREERECORD_H

#include "llvm/CodeGenData/OutlinedHashTree.h"

namespace llvm {

/// HashNodeStable is the serialized, stable, and compact representation
/// of a HashNode.
struct HashNodeStable {
  llvm::yaml::Hex64 Hash;
  unsigned Terminals;
  std::vector<unsigned> SuccessorIds;
};

using IdHashNodeStableMapTy = std::map<unsigned, HashNodeStable>;
using IdHashNodeMapTy = DenseMap<unsigned, HashNode *>;
using HashNodeIdMapTy = DenseMap<const HashNode *, unsigned>;

struct OutlinedHashTreeRecord {
  std::unique_ptr<OutlinedHashTree> HashTree;

  OutlinedHashTreeRecord() { HashTree = std::make_unique<OutlinedHashTree>(); }
  OutlinedHashTreeRecord(std::unique_ptr<OutlinedHashTree> HashTree)
      : HashTree(std::move(HashTree)) {};

  /// Serialize the outlined hash tree to a raw_ostream.
  void serialize(raw_ostream &OS) const;
  /// Deserialize the outlined hash tree from a raw_ostream.
  void deserialize(const unsigned char *&Ptr);
  /// Serialize the outlined hash tree to a YAML stream.
  void serializeYAML(yaml::Output &YOS) const;
  /// Deserialize the outlined hash tree from a YAML stream.
  void deserializeYAML(yaml::Input &YIS);

  /// Merge the other outlined hash tree into this one.
  void merge(const OutlinedHashTreeRecord &Other) {
    HashTree->merge(Other.HashTree.get());
  }

  /// \returns true if the outlined hash tree is empty.
  bool empty() const { return HashTree->empty(); }

  /// Print the outlined hash tree in a YAML format.
  void print(raw_ostream &OS = llvm::errs()) const {
    yaml::Output YOS(OS);
    serializeYAML(YOS);
  }

private:
  /// Convert the outlined hash tree to stable data.
  void convertToStableData(IdHashNodeStableMapTy &IdNodeStableMap) const;

  /// Convert the stable data back to the outlined hash tree.
  void convertFromStableData(const IdHashNodeStableMapTy &IdNodeStableMap);
};

} // end namespace llvm

#endif // LLVM_CODEGENDATA_OUTLINEDHASHTREERECORD_H
