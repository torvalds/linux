//===-- DiffLog.h - Difference Log Builder and accessories ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the interface to the LLVM difference log builder.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_DIFF_DIFFLOG_H
#define LLVM_TOOLS_LLVM_DIFF_DIFFLOG_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
  class Instruction;
  class Value;
  class Consumer;

  /// Trichotomy assumption
  enum DiffChange { DC_match, DC_left, DC_right };

  /// A temporary-object class for building up log messages.
  class LogBuilder {
    Consumer *consumer;

    /// The use of a stored StringRef here is okay because
    /// LogBuilder should be used only as a temporary, and as a
    /// temporary it will be destructed before whatever temporary
    /// might be initializing this format.
    StringRef Format;

    SmallVector<const Value *, 4> Arguments;

  public:
    LogBuilder(Consumer &c, StringRef Format) : consumer(&c), Format(Format) {}
    LogBuilder(LogBuilder &&L)
        : consumer(L.consumer), Format(L.Format),
          Arguments(std::move(L.Arguments)) {
      L.consumer = nullptr;
    }

    LogBuilder &operator<<(const Value *V) {
      Arguments.push_back(V);
      return *this;
    }

    ~LogBuilder();

    StringRef getFormat() const;
    unsigned getNumArguments() const;
    const Value *getArgument(unsigned I) const;
  };

  /// A temporary-object class for building up diff messages.
  class DiffLogBuilder {
    typedef std::pair<const Instruction *, const Instruction *> DiffRecord;
    SmallVector<DiffRecord, 20> Diff;

    Consumer &consumer;

  public:
    DiffLogBuilder(Consumer &c) : consumer(c) {}
    ~DiffLogBuilder();

    void addMatch(const Instruction *L, const Instruction *R);
    // HACK: VS 2010 has a bug in the stdlib that requires this.
    void addLeft(const Instruction *L);
    void addRight(const Instruction *R);

    unsigned getNumLines() const;
    DiffChange getLineKind(unsigned I) const;
    const Instruction *getLeft(unsigned I) const;
    const Instruction *getRight(unsigned I) const;
  };

}

#endif
