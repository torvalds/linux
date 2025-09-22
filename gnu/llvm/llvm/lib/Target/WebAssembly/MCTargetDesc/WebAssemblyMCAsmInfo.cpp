//===-- WebAssemblyMCAsmInfo.cpp - WebAssembly asm properties -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the WebAssemblyMCAsmInfo
/// properties.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyMCAsmInfo.h"
#include "WebAssemblyMCTargetDesc.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define DEBUG_TYPE "wasm-mc-asm-info"

WebAssemblyMCAsmInfo::~WebAssemblyMCAsmInfo() = default; // anchor.

WebAssemblyMCAsmInfo::WebAssemblyMCAsmInfo(const Triple &T,
                                           const MCTargetOptions &Options) {
  CodePointerSize = CalleeSaveStackSlotSize = T.isArch64Bit() ? 8 : 4;

  // TODO: What should MaxInstLength be?

  UseDataRegionDirectives = true;

  // Use .skip instead of .zero because .zero is confusing when used with two
  // arguments (it doesn't actually zero things out).
  ZeroDirective = "\t.skip\t";

  Data8bitsDirective = "\t.int8\t";
  Data16bitsDirective = "\t.int16\t";
  Data32bitsDirective = "\t.int32\t";
  Data64bitsDirective = "\t.int64\t";

  AlignmentIsInBytes = false;
  COMMDirectiveAlignmentIsInBytes = false;
  LCOMMDirectiveAlignmentType = LCOMM::Log2Alignment;

  SupportsDebugInformation = true;

  // When compilation is done on a cpp file by clang, the exception model info
  // is stored in LangOptions, which is later used to set the info in
  // TargetOptions and then MCAsmInfo in LLVMTargetMachine::initAsmInfo(). But
  // this process does not happen when compiling bitcode directly with clang, so
  // we make sure this info is set correctly.
  if (WebAssembly::WasmEnableEH || WebAssembly::WasmEnableSjLj)
    ExceptionsType = ExceptionHandling::Wasm;
}
