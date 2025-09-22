//===--- Options.h - Option info & table ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_OPTIONS_H
#define LLVM_CLANG_DRIVER_OPTIONS_H

#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"

namespace clang {
namespace driver {

namespace options {
/// Flags specifically for clang options.  Must not overlap with
/// llvm::opt::DriverFlag.
enum ClangFlags {
  NoXarchOption = (1 << 4),
  LinkerInput = (1 << 5),
  NoArgumentUnused = (1 << 6),
  Unsupported = (1 << 7),
  LinkOption = (1 << 8),
  Ignored = (1 << 9),
  TargetSpecific = (1 << 10),
};

// Flags specifically for clang option visibility. We alias DefaultVis to
// ClangOption, because "DefaultVis" is confusing in Options.td, which is used
// for multiple drivers (clang, cl, flang, etc).
enum ClangVisibility {
  ClangOption = llvm::opt::DefaultVis,
  CLOption = (1 << 1),
  CC1Option = (1 << 2),
  CC1AsOption = (1 << 3),
  FlangOption = (1 << 4),
  FC1Option = (1 << 5),
  DXCOption = (1 << 6),
};

enum ID {
    OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "clang/Driver/Options.inc"
    LastOption
#undef OPTION
  };
}

const llvm::opt::OptTable &getDriverOptTable();
}
}

#endif
