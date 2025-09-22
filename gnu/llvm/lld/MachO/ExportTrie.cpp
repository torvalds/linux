//===- ExportTrie.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a partial implementation of the Mach-O export trie format. It's
// essentially a symbol table encoded as a compressed prefix trie, meaning that
// the common prefixes of each symbol name are shared for a more compact
// representation. The prefixes are stored on the edges of the trie, and one
// edge can represent multiple characters. For example, given two exported
// symbols _bar and _baz, we will have a trie like this (terminal nodes are
// marked with an asterisk):
//
//              +-+-+
//              |   | // root node
//              +-+-+
//                |
//                | _ba
//                |
//              +-+-+
//              |   |
//              +-+-+
//           r /     \ z
//            /       \
//        +-+-+       +-+-+
//        | * |       | * |
//        +-+-+       +-+-+
//
// More documentation of the format can be found in
// llvm/tools/obj2yaml/macho2yaml.cpp.
//
//===----------------------------------------------------------------------===//

#include "ExportTrie.h"
#include "Symbols.h"

#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/LEB128.h"
#include <optional>

using namespace llvm;
using namespace lld;
using namespace lld::macho;

namespace {

struct Edge {
  Edge(StringRef s, TrieNode *node) : substring(s), child(node) {}

  StringRef substring;
  struct TrieNode *child;
};

struct ExportInfo {
  uint64_t address;
  uint64_t ordinal = 0;
  uint8_t flags = 0;
  ExportInfo(const Symbol &sym, uint64_t imageBase)
      : address(sym.getVA() - imageBase) {
    using namespace llvm::MachO;
    if (sym.isWeakDef())
      flags |= EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION;
    if (sym.isTlv())
      flags |= EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL;
    // TODO: Add proper support for stub-and-resolver flags.

    if (auto *defined = dyn_cast<Defined>(&sym)) {
      if (defined->isAbsolute())
        flags |= EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE;
    } else if (auto *dysym = dyn_cast<DylibSymbol>(&sym)) {
      flags |= EXPORT_SYMBOL_FLAGS_REEXPORT;
      if (!dysym->isDynamicLookup())
        ordinal = dysym->getFile()->ordinal;
    }
  }
};

} // namespace

struct macho::TrieNode {
  std::vector<Edge> edges;
  std::optional<ExportInfo> info;
  // Estimated offset from the start of the serialized trie to the current node.
  // This will converge to the true offset when updateOffset() is run to a
  // fixpoint.
  size_t offset = 0;

  uint32_t getTerminalSize() const;
  // Returns whether the new estimated offset differs from the old one.
  bool updateOffset(size_t &nextOffset);
  void writeTo(uint8_t *buf) const;
};

// For regular symbols, the node layout (excluding the children) is
//
//   uleb128 terminalSize;
//   uleb128 flags;
//   uleb128 address;
//
// For re-exported symbols, the layout is
//
//   uleb128 terminalSize;
//   uleb128 flags;
//   uleb128 ordinal;
//   char[] originalName;
//
// If libfoo.dylib is linked against libbar.dylib, and libfoo exports an alias
// _foo to a symbol _bar in libbar, then originalName will be "_bar". If libfoo
// re-exports _bar directly (i.e. not via an alias), then originalName will be
// the empty string.
//
// TODO: Support aliased re-exports. (Since we don't yet support these,
// originalName will always be the empty string.)
//
// For stub-and-resolver nodes, the layout is
//
//   uleb128 terminalSize;
//   uleb128 flags;
//   uleb128 stubAddress;
//   uleb128 resolverAddress;
//
// TODO: Support stub-and-resolver nodes.
uint32_t TrieNode::getTerminalSize() const {
  uint32_t size = getULEB128Size(info->flags);
  if (info->flags & MachO::EXPORT_SYMBOL_FLAGS_REEXPORT)
    size += getULEB128Size(info->ordinal) + 1; // + 1 for the null-terminator
  else
    size += getULEB128Size(info->address);
  return size;
}

bool TrieNode::updateOffset(size_t &nextOffset) {
  // Size of the whole node (including the terminalSize and the outgoing edges.)
  // In contrast, terminalSize only records the size of the other data in the
  // node.
  size_t nodeSize;
  if (info) {
    uint32_t terminalSize = getTerminalSize();
    // Overall node size so far is the uleb128 size of the length of the symbol
    // info + the symbol info itself.
    nodeSize = terminalSize + getULEB128Size(terminalSize);
  } else {
    nodeSize = 1; // Size of terminalSize (which has a value of 0)
  }
  // Compute size of all child edges.
  ++nodeSize; // Byte for number of children.
  for (const Edge &edge : edges) {
    nodeSize += edge.substring.size() + 1             // String length.
                + getULEB128Size(edge.child->offset); // Offset len.
  }
  // On input, 'nextOffset' is the new preferred location for this node.
  bool result = (offset != nextOffset);
  // Store new location in node object for use by parents.
  offset = nextOffset;
  nextOffset += nodeSize;
  return result;
}

void TrieNode::writeTo(uint8_t *buf) const {
  buf += offset;
  if (info) {
    uint32_t terminalSize = getTerminalSize();
    buf += encodeULEB128(terminalSize, buf);
    buf += encodeULEB128(info->flags, buf);
    if (info->flags & MachO::EXPORT_SYMBOL_FLAGS_REEXPORT) {
      buf += encodeULEB128(info->ordinal, buf);
      *buf++ = 0; // empty originalName string
    } else {
      buf += encodeULEB128(info->address, buf);
    }
  } else {
    // TrieNode with no Symbol info.
    *buf++ = 0; // terminalSize
  }
  // Add number of children. TODO: Handle case where we have more than 256.
  assert(edges.size() < 256);
  *buf++ = edges.size();
  // Append each child edge substring and node offset.
  for (const Edge &edge : edges) {
    memcpy(buf, edge.substring.data(), edge.substring.size());
    buf += edge.substring.size();
    *buf++ = '\0';
    buf += encodeULEB128(edge.child->offset, buf);
  }
}

TrieBuilder::~TrieBuilder() {
  for (TrieNode *node : nodes)
    delete node;
}

TrieNode *TrieBuilder::makeNode() {
  auto *node = new TrieNode();
  nodes.emplace_back(node);
  return node;
}

static int charAt(const Symbol *sym, size_t pos) {
  StringRef str = sym->getName();
  if (pos >= str.size())
    return -1;
  return str[pos];
}

// Build the trie by performing a three-way radix quicksort: We start by sorting
// the strings by their first characters, then sort the strings with the same
// first characters by their second characters, and so on recursively. Each
// time the prefixes diverge, we add a node to the trie.
//
// node:    The most recently created node along this path in the trie (i.e.
//          the furthest from the root.)
// lastPos: The prefix length of the most recently created node, i.e. the number
//          of characters along its path from the root.
// pos:     The string index we are currently sorting on. Note that each symbol
//          S contained in vec has the same prefix S[0...pos).
void TrieBuilder::sortAndBuild(MutableArrayRef<const Symbol *> vec,
                               TrieNode *node, size_t lastPos, size_t pos) {
tailcall:
  if (vec.empty())
    return;

  // Partition items so that items in [0, i) are less than the pivot,
  // [i, j) are the same as the pivot, and [j, vec.size()) are greater than
  // the pivot.
  const Symbol *pivotSymbol = vec[vec.size() / 2];
  int pivot = charAt(pivotSymbol, pos);
  size_t i = 0;
  size_t j = vec.size();
  for (size_t k = 0; k < j;) {
    int c = charAt(vec[k], pos);
    if (c < pivot)
      std::swap(vec[i++], vec[k++]);
    else if (c > pivot)
      std::swap(vec[--j], vec[k]);
    else
      k++;
  }

  bool isTerminal = pivot == -1;
  bool prefixesDiverge = i != 0 || j != vec.size();
  if (lastPos != pos && (isTerminal || prefixesDiverge)) {
    TrieNode *newNode = makeNode();
    node->edges.emplace_back(pivotSymbol->getName().slice(lastPos, pos),
                             newNode);
    node = newNode;
    lastPos = pos;
  }

  sortAndBuild(vec.slice(0, i), node, lastPos, pos);
  sortAndBuild(vec.slice(j), node, lastPos, pos);

  if (isTerminal) {
    assert(j - i == 1); // no duplicate symbols
    node->info = ExportInfo(*pivotSymbol, imageBase);
  } else {
    // This is the tail-call-optimized version of the following:
    // sortAndBuild(vec.slice(i, j - i), node, lastPos, pos + 1);
    vec = vec.slice(i, j - i);
    ++pos;
    goto tailcall;
  }
}

size_t TrieBuilder::build() {
  if (exported.empty())
    return 0;

  TrieNode *root = makeNode();
  sortAndBuild(exported, root, 0, 0);

  // Assign each node in the vector an offset in the trie stream, iterating
  // until all uleb128 sizes have stabilized.
  size_t offset;
  bool more;
  do {
    offset = 0;
    more = false;
    for (TrieNode *node : nodes)
      more |= node->updateOffset(offset);
  } while (more);

  return offset;
}

void TrieBuilder::writeTo(uint8_t *buf) const {
  for (TrieNode *node : nodes)
    node->writeTo(buf);
}

namespace {

// Parse a serialized trie and invoke a callback for each entry.
class TrieParser {
public:
  TrieParser(const uint8_t *buf, size_t size, const TrieEntryCallback &callback)
      : start(buf), end(start + size), callback(callback) {}

  void parse(const uint8_t *buf, const Twine &cumulativeString);

  void parse() { parse(start, ""); }

  const uint8_t *start;
  const uint8_t *end;
  const TrieEntryCallback &callback;
};

} // namespace

void TrieParser::parse(const uint8_t *buf, const Twine &cumulativeString) {
  if (buf >= end)
    fatal("Node offset points outside export section");

  unsigned ulebSize;
  uint64_t terminalSize = decodeULEB128(buf, &ulebSize);
  buf += ulebSize;
  uint64_t flags = 0;
  size_t offset;
  if (terminalSize != 0) {
    flags = decodeULEB128(buf, &ulebSize);
    callback(cumulativeString, flags);
  }
  buf += terminalSize;
  uint8_t numEdges = *buf++;
  for (uint8_t i = 0; i < numEdges; ++i) {
    const char *cbuf = reinterpret_cast<const char *>(buf);
    StringRef substring = StringRef(cbuf, strnlen(cbuf, end - buf));
    buf += substring.size() + 1;
    offset = decodeULEB128(buf, &ulebSize);
    buf += ulebSize;
    parse(start + offset, cumulativeString + substring);
  }
}

void macho::parseTrie(const uint8_t *buf, size_t size,
                      const TrieEntryCallback &callback) {
  if (size == 0)
    return;

  TrieParser(buf, size, callback).parse();
}
