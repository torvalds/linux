//===- ConfigManager.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_CONFIGMANAGER_H
#define LLVM_OBJCOPY_CONFIGMANAGER_H

#include "llvm/ObjCopy/COFF/COFFConfig.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/ObjCopy/ELF/ELFConfig.h"
#include "llvm/ObjCopy/MachO/MachOConfig.h"
#include "llvm/ObjCopy/MultiFormatConfig.h"
#include "llvm/ObjCopy/wasm/WasmConfig.h"
#include "llvm/ObjCopy/XCOFF/XCOFFConfig.h"

namespace llvm {
namespace objcopy {

struct ConfigManager : public MultiFormatConfig {
  virtual ~ConfigManager() {}

  const CommonConfig &getCommonConfig() const override { return Common; }

  Expected<const ELFConfig &> getELFConfig() const override { return ELF; }

  Expected<const COFFConfig &> getCOFFConfig() const override;

  Expected<const MachOConfig &> getMachOConfig() const override;

  Expected<const WasmConfig &> getWasmConfig() const override;

  Expected<const XCOFFConfig &> getXCOFFConfig() const override;

  // All configs.
  CommonConfig Common;
  ELFConfig ELF;
  COFFConfig COFF;
  MachOConfig MachO;
  WasmConfig Wasm;
  XCOFFConfig XCOFF;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_CONFIGMANAGER_H
