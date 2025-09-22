//===- FormatUtil.h ------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_TEST_FORMATUTIL_H
#define LLDB_TOOLS_LLDB_TEST_FORMATUTIL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <list>

namespace lldb_private {

class LinePrinter {
  llvm::raw_ostream &OS;
  int IndentSpaces;
  int CurrentIndent;

public:
  class Line {
    LinePrinter *P;

  public:
    Line(LinePrinter &P) : P(&P) { P.OS.indent(P.CurrentIndent); }
    ~Line();

    Line(Line &&RHS) : P(RHS.P) { RHS.P = nullptr; }
    void operator=(Line &&) = delete;

    operator llvm::raw_ostream &() { return P->OS; }
  };

  LinePrinter(int Indent, llvm::raw_ostream &Stream);

  void Indent(uint32_t Amount = 0);
  void Unindent(uint32_t Amount = 0);
  void NewLine();

  void printLine(const llvm::Twine &T) { line() << T; }
  template <typename... Ts> void formatLine(const char *Fmt, Ts &&... Items) {
    printLine(llvm::formatv(Fmt, std::forward<Ts>(Items)...));
  }

  void formatBinary(llvm::StringRef Label, llvm::ArrayRef<uint8_t> Data,
                    uint32_t StartOffset);
  void formatBinary(llvm::StringRef Label, llvm::ArrayRef<uint8_t> Data,
                    uint64_t BaseAddr, uint32_t StartOffset);

  Line line() { return Line(*this); }
  int getIndentLevel() const { return CurrentIndent; }
};

struct AutoIndent {
  explicit AutoIndent(LinePrinter &L, uint32_t Amount = 0)
      : L(&L), Amount(Amount) {
    L.Indent(Amount);
  }
  ~AutoIndent() {
    if (L)
      L->Unindent(Amount);
  }

  LinePrinter *L = nullptr;
  uint32_t Amount = 0;
};

} // namespace lldb_private

#endif
