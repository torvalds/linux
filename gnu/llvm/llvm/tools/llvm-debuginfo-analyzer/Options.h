//===-- Options.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines command line options used by llvm-debuginfo-analyzer.
//
//===----------------------------------------------------------------------===//

#ifndef OPTIONS_H
#define OPTIONS_H

#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {
namespace logicalview {
namespace cmdline {

class OffsetParser final : public llvm::cl::parser<unsigned long long> {
public:
  OffsetParser(llvm::cl::Option &O);
  ~OffsetParser() override;

  // Parse an argument representing an offset. Return true on error.
  // If the prefix is 0, the base is octal, if the prefix is 0x or 0X, the
  // base is hexadecimal, otherwise the base is decimal.
  bool parse(llvm::cl::Option &O, StringRef ArgName, StringRef ArgValue,
             unsigned long long &Val);
};

typedef llvm::cl::list<unsigned long long, bool, OffsetParser> OffsetOptionList;

extern llvm::cl::OptionCategory AttributeCategory;
extern llvm::cl::OptionCategory CompareCategory;
extern llvm::cl::OptionCategory OutputCategory;
extern llvm::cl::OptionCategory PrintCategory;
extern llvm::cl::OptionCategory ReportCategory;
extern llvm::cl::OptionCategory SelectCategory;
extern llvm::cl::OptionCategory WarningCategory;
extern llvm::cl::OptionCategory InternalCategory;

extern llvm::cl::list<std::string> InputFilenames;
extern llvm::cl::opt<std::string> OutputFilename;

extern llvm::cl::list<std::string> SelectPatterns;

extern llvm::cl::list<LVElementKind> SelectElements;
extern llvm::cl::list<LVLineKind> SelectLines;
extern llvm::cl::list<LVScopeKind> SelectScopes;
extern llvm::cl::list<LVSymbolKind> SelectSymbols;
extern llvm::cl::list<LVTypeKind> SelectTypes;
extern OffsetOptionList SelectOffsets;

extern llvm::cl::list<LVAttributeKind> AttributeOptions;
extern llvm::cl::list<LVOutputKind> OutputOptions;
extern llvm::cl::list<LVPrintKind> PrintOptions;
extern llvm::cl::list<LVWarningKind> WarningOptions;
extern llvm::cl::list<LVInternalKind> InternalOptions;

extern llvm::cl::list<LVCompareKind> CompareElements;
extern llvm::cl::list<LVReportKind> ReportOptions;

extern LVOptions ReaderOptions;

// Perform any additional post parse command line actions. Propagate the
// values captured by the command line parser, into the generic reader.
void propagateOptions();

} // namespace cmdline
} // namespace logicalview
} // namespace llvm

#endif // OPTIONS_H
