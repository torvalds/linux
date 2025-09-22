//===---- llvm/IRReader/IRReader.h - Reader for LLVM IR files ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions for reading LLVM IR. They support both
// Bitcode and Assembly, automatically detecting the input format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IRREADER_IRREADER_H
#define LLVM_IRREADER_IRREADER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include <memory>

namespace llvm {

class MemoryBuffer;
class MemoryBufferRef;
class Module;
class SMDiagnostic;
class LLVMContext;

/// If the given MemoryBuffer holds a bitcode image, return a Module
/// for it which does lazy deserialization of function bodies.  Otherwise,
/// attempt to parse it as LLVM Assembly and return a fully populated
/// Module. The ShouldLazyLoadMetadata flag is passed down to the bitcode
/// reader to optionally enable lazy metadata loading. This takes ownership
/// of \p Buffer.
std::unique_ptr<Module> getLazyIRModule(std::unique_ptr<MemoryBuffer> Buffer,
                                        SMDiagnostic &Err, LLVMContext &Context,
                                        bool ShouldLazyLoadMetadata = false);

/// If the given file holds a bitcode image, return a Module
/// for it which does lazy deserialization of function bodies.  Otherwise,
/// attempt to parse it as LLVM Assembly and return a fully populated
/// Module. The ShouldLazyLoadMetadata flag is passed down to the bitcode
/// reader to optionally enable lazy metadata loading.
std::unique_ptr<Module>
getLazyIRFileModule(StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
                    bool ShouldLazyLoadMetadata = false);

/// If the given MemoryBuffer holds a bitcode image, return a Module
/// for it.  Otherwise, attempt to parse it as LLVM Assembly and return
/// a Module for it.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
std::unique_ptr<Module> parseIR(MemoryBufferRef Buffer, SMDiagnostic &Err,
                                LLVMContext &Context,
                                ParserCallbacks Callbacks = {});

/// If the given file holds a bitcode image, return a Module for it.
/// Otherwise, attempt to parse it as LLVM Assembly and return a Module
/// for it.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
std::unique_ptr<Module> parseIRFile(StringRef Filename, SMDiagnostic &Err,
                                    LLVMContext &Context,
                                    ParserCallbacks Callbacks = {});
}

#endif
