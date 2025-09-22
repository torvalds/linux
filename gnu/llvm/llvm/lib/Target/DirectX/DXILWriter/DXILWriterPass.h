//===-- DXILWriterPass.h - Bitcode writing pass --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides a bitcode writing pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_DXILWriterPass_H
#define LLVM_BITCODE_DXILWriterPass_H

#include "DirectX.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
class raw_ostream;

/// Create and return a pass that writes the module to the specified
/// ostream. Note that this pass is designed for use with the legacy pass
/// manager.
ModulePass *createDXILWriterPass(raw_ostream &Str);

/// Create and return a pass that writes the module to a global variable in the
/// module for later emission in the MCStreamer. Note that this pass is designed
/// for use with the legacy pass manager because it is run in CodeGen only.
ModulePass *createDXILEmbedderPass();

} // namespace llvm

#endif
