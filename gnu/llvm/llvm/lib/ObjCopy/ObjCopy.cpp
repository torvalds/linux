//===- Objcopy.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjCopy/ObjCopy.h"
#include "llvm/ObjCopy/COFF/COFFConfig.h"
#include "llvm/ObjCopy/COFF/COFFObjcopy.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/ObjCopy/ELF/ELFConfig.h"
#include "llvm/ObjCopy/ELF/ELFObjcopy.h"
#include "llvm/ObjCopy/MachO/MachOConfig.h"
#include "llvm/ObjCopy/MachO/MachOObjcopy.h"
#include "llvm/ObjCopy/MultiFormatConfig.h"
#include "llvm/ObjCopy/wasm/WasmConfig.h"
#include "llvm/ObjCopy/wasm/WasmObjcopy.h"
#include "llvm/ObjCopy/XCOFF/XCOFFConfig.h"
#include "llvm/ObjCopy/XCOFF/XCOFFObjcopy.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Object/XCOFFObjectFile.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"

namespace llvm {
namespace objcopy {

using namespace llvm::object;

/// The function executeObjcopyOnBinary does the dispatch based on the format
/// of the input binary (ELF, MachO or COFF).
Error executeObjcopyOnBinary(const MultiFormatConfig &Config,
                             object::Binary &In, raw_ostream &Out) {
  if (auto *ELFBinary = dyn_cast<object::ELFObjectFileBase>(&In)) {
    Expected<const ELFConfig &> ELFConfig = Config.getELFConfig();
    if (!ELFConfig)
      return ELFConfig.takeError();

    return elf::executeObjcopyOnBinary(Config.getCommonConfig(), *ELFConfig,
                                       *ELFBinary, Out);
  }
  if (auto *COFFBinary = dyn_cast<object::COFFObjectFile>(&In)) {
    Expected<const COFFConfig &> COFFConfig = Config.getCOFFConfig();
    if (!COFFConfig)
      return COFFConfig.takeError();

    return coff::executeObjcopyOnBinary(Config.getCommonConfig(), *COFFConfig,
                                        *COFFBinary, Out);
  }
  if (auto *MachOBinary = dyn_cast<object::MachOObjectFile>(&In)) {
    Expected<const MachOConfig &> MachOConfig = Config.getMachOConfig();
    if (!MachOConfig)
      return MachOConfig.takeError();

    return macho::executeObjcopyOnBinary(Config.getCommonConfig(), *MachOConfig,
                                         *MachOBinary, Out);
  }
  if (auto *MachOUniversalBinary =
          dyn_cast<object::MachOUniversalBinary>(&In)) {
    return macho::executeObjcopyOnMachOUniversalBinary(
        Config, *MachOUniversalBinary, Out);
  }
  if (auto *WasmBinary = dyn_cast<object::WasmObjectFile>(&In)) {
    Expected<const WasmConfig &> WasmConfig = Config.getWasmConfig();
    if (!WasmConfig)
      return WasmConfig.takeError();

    return objcopy::wasm::executeObjcopyOnBinary(Config.getCommonConfig(),
                                                 *WasmConfig, *WasmBinary, Out);
  }
  if (auto *XCOFFBinary = dyn_cast<object::XCOFFObjectFile>(&In)) {
    Expected<const XCOFFConfig &> XCOFFConfig = Config.getXCOFFConfig();
    if (!XCOFFConfig)
      return XCOFFConfig.takeError();

    return xcoff::executeObjcopyOnBinary(Config.getCommonConfig(), *XCOFFConfig,
                                         *XCOFFBinary, Out);
  }
  return createStringError(object_error::invalid_file_type,
                           "unsupported object file format");
}

} // end namespace objcopy
} // end namespace llvm
