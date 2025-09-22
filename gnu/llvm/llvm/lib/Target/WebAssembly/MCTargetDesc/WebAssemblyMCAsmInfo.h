//===-- WebAssemblyMCAsmInfo.h - WebAssembly asm properties -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the WebAssemblyMCAsmInfo class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCASMINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCASMINFO_H

#include "llvm/MC/MCAsmInfoWasm.h"

namespace llvm {

class Triple;

class WebAssemblyMCAsmInfo final : public MCAsmInfoWasm {
public:
  explicit WebAssemblyMCAsmInfo(const Triple &T,
                                const MCTargetOptions &Options);
  ~WebAssemblyMCAsmInfo() override;
};

} // end namespace llvm

#endif
