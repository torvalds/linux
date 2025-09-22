//===- BTFContext.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BTFContext interface is used by llvm-objdump tool to print source
// code alongside disassembly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_BTF_BTFCONTEXT_H
#define LLVM_DEBUGINFO_BTF_BTFCONTEXT_H

#include "llvm/DebugInfo/BTF/BTFParser.h"
#include "llvm/DebugInfo/DIContext.h"

namespace llvm {

class BTFContext final : public DIContext {
  BTFParser BTF;

public:
  BTFContext() : DIContext(CK_BTF) {}

  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) override {
    // This function is called from objdump when --dwarf=? option is set.
    // BTF is no DWARF, so ignore this operation for now.
  }

  DILineInfo getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  DILineInfo
  getLineInfoForDataAddress(object::SectionedAddress Address) override;

  DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) override;

  static std::unique_ptr<BTFContext> create(
      const object::ObjectFile &Obj,
      std::function<void(Error)> ErrorHandler = WithColor::defaultErrorHandler);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_BTF_BTFCONTEXT_H
