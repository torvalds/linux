//===- llvm/Analysis/Trace.h - Represent one trace of LLVM code -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents a single trace of LLVM basic blocks.  A trace is a
// single entry, multiple exit, region of code that is often hot.  Trace-based
// optimizations treat traces almost like they are a large, strange, basic
// block: because the trace path is assumed to be hot, optimizations for the
// fall-through path are made at the expense of the non-fall-through paths.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TRACE_H
#define LLVM_ANALYSIS_TRACE_H

#include <cassert>
#include <vector>

namespace llvm {

class BasicBlock;
class Function;
class Module;
class raw_ostream;

class Trace {
  using BasicBlockListType = std::vector<BasicBlock *>;

  BasicBlockListType BasicBlocks;

public:
  /// Trace ctor - Make a new trace from a vector of basic blocks,
  /// residing in the function which is the parent of the first
  /// basic block in the vector.
  Trace(const std::vector<BasicBlock *> &vBB) : BasicBlocks (vBB) {}

  /// getEntryBasicBlock - Return the entry basic block (first block)
  /// of the trace.
  BasicBlock *getEntryBasicBlock () const { return BasicBlocks[0]; }

  /// operator[]/getBlock - Return basic block N in the trace.
  BasicBlock *operator[](unsigned i) const { return BasicBlocks[i]; }
  BasicBlock *getBlock(unsigned i)   const { return BasicBlocks[i]; }

  /// getFunction - Return this trace's parent function.
  Function *getFunction () const;

  /// getModule - Return this Module that contains this trace's parent
  /// function.
  Module *getModule () const;

  /// getBlockIndex - Return the index of the specified basic block in the
  /// trace, or -1 if it is not in the trace.
  int getBlockIndex(const BasicBlock *X) const {
    for (unsigned i = 0, e = BasicBlocks.size(); i != e; ++i)
      if (BasicBlocks[i] == X)
        return i;
    return -1;
  }

  /// contains - Returns true if this trace contains the given basic
  /// block.
  bool contains(const BasicBlock *X) const {
    return getBlockIndex(X) != -1;
  }

  /// Returns true if B1 occurs before B2 in the trace, or if it is the same
  /// block as B2..  Both blocks must be in the trace.
  bool dominates(const BasicBlock *B1, const BasicBlock *B2) const {
    int B1Idx = getBlockIndex(B1), B2Idx = getBlockIndex(B2);
    assert(B1Idx != -1 && B2Idx != -1 && "Block is not in the trace!");
    return B1Idx <= B2Idx;
  }

  // BasicBlock iterators...
  using iterator = BasicBlockListType::iterator;
  using const_iterator = BasicBlockListType::const_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator                begin()       { return BasicBlocks.begin(); }
  const_iterator          begin() const { return BasicBlocks.begin(); }
  iterator                end  ()       { return BasicBlocks.end();   }
  const_iterator          end  () const { return BasicBlocks.end();   }

  reverse_iterator       rbegin()       { return BasicBlocks.rbegin(); }
  const_reverse_iterator rbegin() const { return BasicBlocks.rbegin(); }
  reverse_iterator       rend  ()       { return BasicBlocks.rend();   }
  const_reverse_iterator rend  () const { return BasicBlocks.rend();   }

  unsigned                 size() const { return BasicBlocks.size(); }
  bool                    empty() const { return BasicBlocks.empty(); }

  iterator erase(iterator q)               { return BasicBlocks.erase (q); }
  iterator erase(iterator q1, iterator q2) { return BasicBlocks.erase (q1, q2); }

  /// print - Write trace to output stream.
  void print(raw_ostream &O) const;

  /// dump - Debugger convenience method; writes trace to standard error
  /// output stream.
  void dump() const;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_TRACE_H
