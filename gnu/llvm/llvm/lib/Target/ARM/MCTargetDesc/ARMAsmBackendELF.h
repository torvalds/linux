//===-- ARMAsmBackendELF.h  ARM Asm Backend ELF -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ELFARMASMBACKEND_H
#define LLVM_LIB_TARGET_ARM_ELFARMASMBACKEND_H

#include "ARMAsmBackend.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/MC/MCObjectWriter.h"

using namespace llvm;

namespace {
class ARMAsmBackendELF : public ARMAsmBackend {
public:
  uint8_t OSABI;
  ARMAsmBackendELF(const Target &T, bool isThumb, uint8_t OSABI,
                   llvm::endianness Endian)
      : ARMAsmBackend(T, isThumb, Endian), OSABI(OSABI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createARMELFObjectWriter(OSABI);
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;
};
}

#endif
