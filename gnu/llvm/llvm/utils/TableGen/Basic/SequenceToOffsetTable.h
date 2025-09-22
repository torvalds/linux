//===-- SequenceToOffsetTable.h - Compress similar sequences ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SequenceToOffsetTable can be used to emit a number of null-terminated
// sequences as one big array.  Use the same memory when a sequence is a suffix
// of another.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_SEQUENCETOOFFSETTABLE_H
#define LLVM_UTILS_TABLEGEN_SEQUENCETOOFFSETTABLE_H

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <functional>
#include <map>

namespace llvm {
extern llvm::cl::opt<bool> EmitLongStrLiterals;

static inline void printChar(raw_ostream &OS, char C) {
  unsigned char UC(C);
  if (isalnum(UC) || ispunct(UC)) {
    OS << '\'';
    if (C == '\\' || C == '\'')
      OS << '\\';
    OS << C << '\'';
  } else {
    OS << unsigned(UC);
  }
}

/// SequenceToOffsetTable - Collect a number of terminated sequences of T.
/// Compute the layout of a table that contains all the sequences, possibly by
/// reusing entries.
///
/// @tparam SeqT The sequence container. (vector or string).
/// @tparam Less A stable comparator for SeqT elements.
template <typename SeqT, typename Less = std::less<typename SeqT::value_type>>
class SequenceToOffsetTable {
  typedef typename SeqT::value_type ElemT;

  // Define a comparator for SeqT that sorts a suffix immediately before a
  // sequence with that suffix.
  struct SeqLess {
    Less L;
    bool operator()(const SeqT &A, const SeqT &B) const {
      return std::lexicographical_compare(A.rbegin(), A.rend(), B.rbegin(),
                                          B.rend(), L);
    }
  };

  // Keep sequences ordered according to SeqLess so suffixes are easy to find.
  // Map each sequence to its offset in the table.
  typedef std::map<SeqT, unsigned, SeqLess> SeqMap;

  // Sequences added so far, with suffixes removed.
  SeqMap Seqs;

  // Entries in the final table, or 0 before layout was called.
  unsigned Entries;

  // isSuffix - Returns true if A is a suffix of B.
  static bool isSuffix(const SeqT &A, const SeqT &B) {
    return A.size() <= B.size() && std::equal(A.rbegin(), A.rend(), B.rbegin());
  }

public:
  SequenceToOffsetTable() : Entries(0) {}

  /// add - Add a sequence to the table.
  /// This must be called before layout().
  void add(const SeqT &Seq) {
    assert(Entries == 0 && "Cannot call add() after layout()");
    typename SeqMap::iterator I = Seqs.lower_bound(Seq);

    // If SeqMap contains a sequence that has Seq as a suffix, I will be
    // pointing to it.
    if (I != Seqs.end() && isSuffix(Seq, I->first))
      return;

    I = Seqs.insert(I, std::pair(Seq, 0u));

    // The entry before I may be a suffix of Seq that can now be erased.
    if (I != Seqs.begin() && isSuffix((--I)->first, Seq))
      Seqs.erase(I);
  }

  bool empty() const { return Seqs.empty(); }

  unsigned size() const {
    assert((empty() || Entries) && "Call layout() before size()");
    return Entries;
  }

  /// layout - Computes the final table layout.
  void layout() {
    assert(Entries == 0 && "Can only call layout() once");
    // Lay out the table in Seqs iteration order.
    for (typename SeqMap::iterator I = Seqs.begin(), E = Seqs.end(); I != E;
         ++I) {
      I->second = Entries;
      // Include space for a terminator.
      Entries += I->first.size() + 1;
    }
  }

  /// get - Returns the offset of Seq in the final table.
  unsigned get(const SeqT &Seq) const {
    assert(Entries && "Call layout() before get()");
    typename SeqMap::const_iterator I = Seqs.lower_bound(Seq);
    assert(I != Seqs.end() && isSuffix(Seq, I->first) &&
           "get() called with sequence that wasn't added first");
    return I->second + (I->first.size() - Seq.size());
  }

  /// `emitStringLiteralDef` - Print out the table as the body of an array
  /// initializer, where each element is a C string literal terminated by
  /// `\0`. Falls back to emitting a comma-separated integer list if
  /// `EmitLongStrLiterals` is false
  void emitStringLiteralDef(raw_ostream &OS, const llvm::Twine &Decl) const {
    assert(Entries && "Call layout() before emitStringLiteralDef()");
    if (!EmitLongStrLiterals) {
      OS << Decl << " = {\n";
      emit(OS, printChar, "0");
      OS << "  0\n};\n\n";
      return;
    }

    OS << "\n#ifdef __GNUC__\n"
       << "#pragma GCC diagnostic push\n"
       << "#pragma GCC diagnostic ignored \"-Woverlength-strings\"\n"
       << "#endif\n"
       << Decl << " = {\n";
    for (auto I : Seqs) {
      OS << "  /* " << I.second << " */ \"";
      OS.write_escaped(I.first);
      OS << "\\0\"\n";
    }
    OS << "};\n"
       << "#ifdef __GNUC__\n"
       << "#pragma GCC diagnostic pop\n"
       << "#endif\n\n";
  }

  /// emit - Print out the table as the body of an array initializer.
  /// Use the Print function to print elements.
  void emit(raw_ostream &OS, void (*Print)(raw_ostream &, ElemT),
            const char *Term = "0") const {
    assert((empty() || Entries) && "Call layout() before emit()");
    for (typename SeqMap::const_iterator I = Seqs.begin(), E = Seqs.end();
         I != E; ++I) {
      OS << "  /* " << I->second << " */ ";
      for (typename SeqT::const_iterator SI = I->first.begin(),
                                         SE = I->first.end();
           SI != SE; ++SI) {
        Print(OS, *SI);
        OS << ", ";
      }
      OS << Term << ",\n";
    }
  }
};

} // end namespace llvm

#endif
