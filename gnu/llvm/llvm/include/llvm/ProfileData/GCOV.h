//===- GCOV.h - LLVM coverage tool ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header provides the interface to read and write coverage files that
// use 'gcov' format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_GCOV_H
#define LLVM_PROFILEDATA_GCOV_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace llvm {

class GCOVFunction;
class GCOVBlock;

namespace GCOV {

enum GCOVVersion { V304, V407, V408, V800, V900, V1200 };

/// A struct for passing gcov options between functions.
struct Options {
  Options(bool A, bool B, bool C, bool F, bool P, bool U, bool I, bool L,
          bool M, bool N, bool R, bool T, bool X, std::string SourcePrefix)
      : AllBlocks(A), BranchInfo(B), BranchCount(C), FuncCoverage(F),
        PreservePaths(P), UncondBranch(U), Intermediate(I), LongFileNames(L),
        Demangle(M), NoOutput(N), RelativeOnly(R), UseStdout(T),
        HashFilenames(X), SourcePrefix(std::move(SourcePrefix)) {}

  bool AllBlocks;
  bool BranchInfo;
  bool BranchCount;
  bool FuncCoverage;
  bool PreservePaths;
  bool UncondBranch;
  bool Intermediate;
  bool LongFileNames;
  bool Demangle;
  bool NoOutput;
  bool RelativeOnly;
  bool UseStdout;
  bool HashFilenames;
  std::string SourcePrefix;
};

} // end namespace GCOV

/// GCOVBuffer - A wrapper around MemoryBuffer to provide GCOV specific
/// read operations.
class GCOVBuffer {
public:
  GCOVBuffer(MemoryBuffer *B) : Buffer(B) {}
  ~GCOVBuffer() { consumeError(cursor.takeError()); }

  /// readGCNOFormat - Check GCNO signature is valid at the beginning of buffer.
  bool readGCNOFormat() {
    StringRef buf = Buffer->getBuffer();
    StringRef magic = buf.substr(0, 4);
    if (magic == "gcno") {
      de = DataExtractor(buf.substr(4), false, 0);
    } else if (magic == "oncg") {
      de = DataExtractor(buf.substr(4), true, 0);
    } else {
      errs() << "unexpected magic: " << magic << "\n";
      return false;
    }
    return true;
  }

  /// readGCDAFormat - Check GCDA signature is valid at the beginning of buffer.
  bool readGCDAFormat() {
    StringRef buf = Buffer->getBuffer();
    StringRef magic = buf.substr(0, 4);
    if (magic == "gcda") {
      de = DataExtractor(buf.substr(4), false, 0);
    } else if (magic == "adcg") {
      de = DataExtractor(buf.substr(4), true, 0);
    } else {
      return false;
    }
    return true;
  }

  /// readGCOVVersion - Read GCOV version.
  bool readGCOVVersion(GCOV::GCOVVersion &version) {
    std::string str(de.getBytes(cursor, 4));
    if (str.size() != 4)
      return false;
    if (de.isLittleEndian())
      std::reverse(str.begin(), str.end());
    int ver = str[0] >= 'A'
                  ? (str[0] - 'A') * 100 + (str[1] - '0') * 10 + str[2] - '0'
                  : (str[0] - '0') * 10 + str[2] - '0';
    if (ver >= 120) {
      this->version = version = GCOV::V1200;
      return true;
    } else if (ver >= 90) {
      // PR gcov-profile/84846, r269678
      this->version = version = GCOV::V900;
      return true;
    } else if (ver >= 80) {
      // PR gcov-profile/48463
      this->version = version = GCOV::V800;
      return true;
    } else if (ver >= 48) {
      // r189778: the exit block moved from the last to the second.
      this->version = version = GCOV::V408;
      return true;
    } else if (ver >= 47) {
      // r173147: split checksum into cfg checksum and line checksum.
      this->version = version = GCOV::V407;
      return true;
    } else if (ver >= 34) {
      this->version = version = GCOV::V304;
      return true;
    }
    errs() << "unexpected version: " << str << "\n";
    return false;
  }

  uint32_t getWord() { return de.getU32(cursor); }
  StringRef getString() {
    uint32_t len;
    if (!readInt(len) || len == 0)
      return {};
    return de.getBytes(cursor, len * 4).split('\0').first;
  }

  bool readInt(uint32_t &Val) {
    if (cursor.tell() + 4 > de.size()) {
      Val = 0;
      errs() << "unexpected end of memory buffer: " << cursor.tell() << "\n";
      return false;
    }
    Val = de.getU32(cursor);
    return true;
  }

  bool readInt64(uint64_t &Val) {
    uint32_t Lo, Hi;
    if (!readInt(Lo) || !readInt(Hi))
      return false;
    Val = ((uint64_t)Hi << 32) | Lo;
    return true;
  }

  bool readString(StringRef &str) {
    uint32_t len;
    if (!readInt(len) || len == 0)
      return false;
    if (version >= GCOV::V1200)
      str = de.getBytes(cursor, len).drop_back();
    else
      str = de.getBytes(cursor, len * 4).split('\0').first;
    return bool(cursor);
  }

  DataExtractor de{ArrayRef<uint8_t>{}, false, 0};
  DataExtractor::Cursor cursor{0};

private:
  MemoryBuffer *Buffer;
  GCOV::GCOVVersion version{};
};

/// GCOVFile - Collects coverage information for one pair of coverage file
/// (.gcno and .gcda).
class GCOVFile {
public:
  GCOVFile() = default;

  bool readGCNO(GCOVBuffer &Buffer);
  bool readGCDA(GCOVBuffer &Buffer);
  GCOV::GCOVVersion getVersion() const { return version; }
  void print(raw_ostream &OS) const;
  void dump() const;

  std::vector<std::string> filenames;
  StringMap<unsigned> filenameToIdx;

public:
  bool GCNOInitialized = false;
  GCOV::GCOVVersion version{};
  uint32_t checksum = 0;
  StringRef cwd;
  SmallVector<std::unique_ptr<GCOVFunction>, 16> functions;
  std::map<uint32_t, GCOVFunction *> identToFunction;
  uint32_t runCount = 0;
  uint32_t programCount = 0;

  using iterator = pointee_iterator<
      SmallVectorImpl<std::unique_ptr<GCOVFunction>>::const_iterator>;
  iterator begin() const { return iterator(functions.begin()); }
  iterator end() const { return iterator(functions.end()); }

private:
  unsigned addNormalizedPathToMap(StringRef filename);
};

struct GCOVArc {
  GCOVArc(GCOVBlock &src, GCOVBlock &dst, uint32_t flags)
      : src(src), dst(dst), flags(flags) {}
  bool onTree() const;

  GCOVBlock &src;
  GCOVBlock &dst;
  uint32_t flags;
  uint64_t count = 0;
  uint64_t cycleCount = 0;
};

/// GCOVFunction - Collects function information.
class GCOVFunction {
public:
  using BlockIterator = pointee_iterator<
      SmallVectorImpl<std::unique_ptr<GCOVBlock>>::const_iterator>;

  GCOVFunction(GCOVFile &file) : file(file) {}

  StringRef getName(bool demangle) const;
  StringRef getFilename() const;
  uint64_t getEntryCount() const;
  GCOVBlock &getExitBlock() const;

  iterator_range<BlockIterator> blocksRange() const {
    return make_range(blocks.begin(), blocks.end());
  }

  void propagateCounts(const GCOVBlock &v, GCOVArc *pred);
  void print(raw_ostream &OS) const;
  void dump() const;

  GCOVFile &file;
  uint32_t ident = 0;
  uint32_t linenoChecksum;
  uint32_t cfgChecksum = 0;
  uint32_t startLine = 0;
  uint32_t startColumn = 0;
  uint32_t endLine = 0;
  uint32_t endColumn = 0;
  uint8_t artificial = 0;
  StringRef Name;
  mutable SmallString<0> demangled;
  unsigned srcIdx;
  SmallVector<std::unique_ptr<GCOVBlock>, 0> blocks;
  SmallVector<std::unique_ptr<GCOVArc>, 0> arcs, treeArcs;
  DenseSet<const GCOVBlock *> visited;
};

/// GCOVBlock - Collects block information.
class GCOVBlock {
public:
  using EdgeIterator = SmallVectorImpl<GCOVArc *>::const_iterator;
  using BlockVector = SmallVector<const GCOVBlock *, 1>;
  using BlockVectorLists = SmallVector<BlockVector, 4>;
  using Edges = SmallVector<GCOVArc *, 4>;

  GCOVBlock(uint32_t N) : number(N) {}

  void addLine(uint32_t N) { lines.push_back(N); }
  uint32_t getLastLine() const { return lines.back(); }
  uint64_t getCount() const { return count; }

  void addSrcEdge(GCOVArc *Edge) { pred.push_back(Edge); }

  void addDstEdge(GCOVArc *Edge) { succ.push_back(Edge); }

  iterator_range<EdgeIterator> srcs() const {
    return make_range(pred.begin(), pred.end());
  }

  iterator_range<EdgeIterator> dsts() const {
    return make_range(succ.begin(), succ.end());
  }

  void print(raw_ostream &OS) const;
  void dump() const;

  static uint64_t
  augmentOneCycle(GCOVBlock *src,
                  std::vector<std::pair<GCOVBlock *, size_t>> &stack);
  static uint64_t getCyclesCount(const BlockVector &blocks);
  static uint64_t getLineCount(const BlockVector &Blocks);

public:
  uint32_t number;
  uint64_t count = 0;
  SmallVector<GCOVArc *, 2> pred;
  SmallVector<GCOVArc *, 2> succ;
  SmallVector<uint32_t, 4> lines;
  bool traversable = false;
  GCOVArc *incoming = nullptr;
};

void gcovOneInput(const GCOV::Options &options, StringRef filename,
                  StringRef gcno, StringRef gcda, GCOVFile &file);

} // end namespace llvm

#endif // LLVM_PROFILEDATA_GCOV_H
