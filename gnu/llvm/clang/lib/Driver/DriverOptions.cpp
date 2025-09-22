//===--- DriverOptions.cpp - Driver Options Table -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Options.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include <cassert>

using namespace clang::driver;
using namespace clang::driver::options;
using namespace llvm::opt;

#define OPTTABLE_VALUES_CODE
#include "clang/Driver/Options.inc"
#undef OPTTABLE_VALUES_CODE

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "clang/Driver/Options.inc"
#undef PREFIX

static constexpr const llvm::StringLiteral PrefixTable_init[] =
#define PREFIX_UNION(VALUES) VALUES
#include "clang/Driver/Options.inc"
#undef PREFIX_UNION
    ;
static constexpr const llvm::ArrayRef<llvm::StringLiteral>
    PrefixTable(PrefixTable_init, std::size(PrefixTable_init) - 1);

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "clang/Driver/Options.inc"
#undef OPTION
};

namespace {

class DriverOptTable : public PrecomputedOptTable {
public:
  DriverOptTable() : PrecomputedOptTable(InfoTable, PrefixTable) {}
};
}

const llvm::opt::OptTable &clang::driver::getDriverOptTable() {
  static DriverOptTable Table;
  return Table;
}
