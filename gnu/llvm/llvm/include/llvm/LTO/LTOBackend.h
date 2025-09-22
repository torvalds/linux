//===-LTOBackend.h - LLVM Link Time Optimizer Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the "backend" phase of LTO, i.e. it performs
// optimization and code generation on a loaded module. It is generally used
// internally by the LTO class but can also be used independently, for example
// to implement a standalone ThinLTO backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTOBACKEND_H
#define LLVM_LTO_LTOBACKEND_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO/FunctionImport.h"

namespace llvm {

class BitcodeModule;
class Error;
class Module;
class Target;

namespace lto {

/// Runs middle-end LTO optimizations on \p Mod.
bool opt(const Config &Conf, TargetMachine *TM, unsigned Task, Module &Mod,
         bool IsThinLTO, ModuleSummaryIndex *ExportSummary,
         const ModuleSummaryIndex *ImportSummary,
         const std::vector<uint8_t> &CmdArgs);

/// Runs a regular LTO backend. The regular LTO backend can also act as the
/// regular LTO phase of ThinLTO, which may need to access the combined index.
Error backend(const Config &C, AddStreamFn AddStream,
              unsigned ParallelCodeGenParallelismLevel, Module &M,
              ModuleSummaryIndex &CombinedIndex);

/// Runs a ThinLTO backend.
/// If \p ModuleMap is not nullptr, all the module files to be imported have
/// already been mapped to memory and the corresponding BitcodeModule objects
/// are saved in the ModuleMap. If \p ModuleMap is nullptr, module files will
/// be mapped to memory on demand and at any given time during importing, only
/// one source module will be kept open at the most.
Error thinBackend(const Config &C, unsigned Task, AddStreamFn AddStream,
                  Module &M, const ModuleSummaryIndex &CombinedIndex,
                  const FunctionImporter::ImportMapTy &ImportList,
                  const GVSummaryMapTy &DefinedGlobals,
                  MapVector<StringRef, BitcodeModule> *ModuleMap,
                  const std::vector<uint8_t> &CmdArgs = std::vector<uint8_t>());

Error finalizeOptimizationRemarks(
    std::unique_ptr<ToolOutputFile> DiagOutputFile);

/// Returns the BitcodeModule that is ThinLTO.
BitcodeModule *findThinLTOModule(MutableArrayRef<BitcodeModule> BMs);

/// Variant of the above.
Expected<BitcodeModule> findThinLTOModule(MemoryBufferRef MBRef);

/// Distributed ThinLTO: collect the referenced modules based on
/// module summary and initialize ImportList. Returns false if the
/// operation failed.
bool initImportList(const Module &M, const ModuleSummaryIndex &CombinedIndex,
                    FunctionImporter::ImportMapTy &ImportList);
}
}

#endif
