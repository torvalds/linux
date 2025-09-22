//===- ExportTrie.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_EXPORT_TRIE_H
#define LLD_MACHO_EXPORT_TRIE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"

#include <vector>

namespace lld::macho {

struct TrieNode;
class Symbol;

class TrieBuilder {
public:
  ~TrieBuilder();
  void setImageBase(uint64_t addr) { imageBase = addr; }
  void addSymbol(const Symbol &sym) { exported.push_back(&sym); }
  // Returns the size in bytes of the serialized trie.
  size_t build();
  void writeTo(uint8_t *buf) const;

private:
  TrieNode *makeNode();
  void sortAndBuild(llvm::MutableArrayRef<const Symbol *> vec, TrieNode *node,
                    size_t lastPos, size_t pos);

  uint64_t imageBase = 0;
  std::vector<const Symbol *> exported;
  std::vector<TrieNode *> nodes;
};

using TrieEntryCallback =
    llvm::function_ref<void(const llvm::Twine & /*name*/, uint64_t /*flags*/)>;

void parseTrie(const uint8_t *buf, size_t size, const TrieEntryCallback &);

} // namespace lld::macho

#endif
