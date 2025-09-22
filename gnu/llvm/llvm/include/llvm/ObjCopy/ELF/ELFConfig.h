//===- ELFConfig.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_ELF_ELFCONFIG_H
#define LLVM_OBJCOPY_ELF_ELFCONFIG_H

#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/Object/ELFTypes.h"

namespace llvm {
namespace objcopy {

// ELF specific configuration for copying/stripping a single file.
struct ELFConfig {
  uint8_t NewSymbolVisibility = (uint8_t)ELF::STV_DEFAULT;

  std::vector<std::pair<NameMatcher, uint8_t>> SymbolsToSetVisibility;

  // ELF entry point address expression. The input parameter is an entry point
  // address in the input ELF file. The entry address in the output file is
  // calculated with EntryExpr(input_address), when either --set-start or
  // --change-start is used.
  std::function<uint64_t(uint64_t)> EntryExpr;

  bool AllowBrokenLinks = false;
  bool KeepFileSymbols = false;
  bool LocalizeHidden = false;
  bool VerifyNoteSections = true;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_ELF_ELFCONFIG_H
