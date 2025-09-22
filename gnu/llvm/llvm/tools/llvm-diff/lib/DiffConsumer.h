//===-- DiffConsumer.h - Difference Consumer --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the interface to the LLVM difference Consumer
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_DIFF_DIFFCONSUMER_H
#define LLVM_TOOLS_LLVM_DIFF_DIFFCONSUMER_H

#include "DiffLog.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class StringRef;
  class Module;
  class Value;
  class Function;

  /// The interface for consumers of difference data.
  class Consumer {
    virtual void anchor();
  public:
    /// Record that a local context has been entered.  Left and
    /// Right are IR "containers" of some sort which are being
    /// considered for structural equivalence: global variables,
    /// functions, blocks, instructions, etc.
    virtual void enterContext(const Value *Left, const Value *Right) = 0;

    /// Record that a local context has been exited.
    virtual void exitContext() = 0;

    /// Record a difference within the current context.
    virtual void log(StringRef Text) = 0;

    /// Record a formatted difference within the current context.
    virtual void logf(const LogBuilder &Log) = 0;

    /// Record a line-by-line instruction diff.
    virtual void logd(const DiffLogBuilder &Log) = 0;

  protected:
    virtual ~Consumer() {}
  };

  class DiffConsumer : public Consumer {
  private:
    struct DiffContext {
      DiffContext(const Value *L, const Value *R)
          : L(L), R(R), Differences(false), IsFunction(isa<Function>(L)) {}
      const Value *L;
      const Value *R;
      bool Differences;
      bool IsFunction;
      DenseMap<const Value *, unsigned> LNumbering;
      DenseMap<const Value *, unsigned> RNumbering;
    };

    raw_ostream &out;
    SmallVector<DiffContext, 5> contexts;
    bool Differences;
    unsigned Indent;

    void printValue(const Value *V, bool isL);
    void header();
    void indent();

  public:
    DiffConsumer()
      : out(errs()), Differences(false), Indent(0) {}

    void reset();
    bool hadDifferences() const;
    void enterContext(const Value *L, const Value *R) override;
    void exitContext() override;
    void log(StringRef text) override;
    void logf(const LogBuilder &Log) override;
    void logd(const DiffLogBuilder &Log) override;
  };
}

#endif
