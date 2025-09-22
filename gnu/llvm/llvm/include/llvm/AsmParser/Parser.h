//===-- Parser.h - Parser for LLVM IR text assembly files -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  These classes are implemented by the lib/AsmParser library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_PARSER_H
#define LLVM_ASMPARSER_PARSER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <optional>

namespace llvm {

class Constant;
class DIExpression;
class LLVMContext;
class MemoryBufferRef;
class Module;
class ModuleSummaryIndex;
struct SlotMapping;
class SMDiagnostic;
class Type;

typedef llvm::function_ref<std::optional<std::string>(StringRef, StringRef)>
    DataLayoutCallbackTy;

/// This function is a main interface to the LLVM Assembly Parser. It parses
/// an ASCII file that (presumably) contains LLVM Assembly code. It returns a
/// Module (intermediate representation) with the corresponding features. Note
/// that this does not verify that the generated Module is valid, so you should
/// run the verifier after parsing the file to check that it is okay.
/// Parse LLVM Assembly from a file
/// \param Filename The name of the file to parse
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
std::unique_ptr<Module> parseAssemblyFile(StringRef Filename, SMDiagnostic &Err,
                                          LLVMContext &Context,
                                          SlotMapping *Slots = nullptr);

/// The function is a secondary interface to the LLVM Assembly Parser. It parses
/// an ASCII string that (presumably) contains LLVM Assembly code. It returns a
/// Module (intermediate representation) with the corresponding features. Note
/// that this does not verify that the generated Module is valid, so you should
/// run the verifier after parsing the file to check that it is okay.
/// Parse LLVM Assembly from a string
/// \param AsmString The string containing assembly
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
std::unique_ptr<Module> parseAssemblyString(StringRef AsmString,
                                            SMDiagnostic &Err,
                                            LLVMContext &Context,
                                            SlotMapping *Slots = nullptr);

/// Holds the Module and ModuleSummaryIndex returned by the interfaces
/// that parse both.
struct ParsedModuleAndIndex {
  std::unique_ptr<Module> Mod;
  std::unique_ptr<ModuleSummaryIndex> Index;
};

/// This function is a main interface to the LLVM Assembly Parser. It parses
/// an ASCII file that (presumably) contains LLVM Assembly code, including
/// a module summary. It returns a Module (intermediate representation) and
/// a ModuleSummaryIndex with the corresponding features. Note that this does
/// not verify that the generated Module or Index are valid, so you should
/// run the verifier after parsing the file to check that they are okay.
/// Parse LLVM Assembly from a file
/// \param Filename The name of the file to parse
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
ParsedModuleAndIndex parseAssemblyFileWithIndex(
    StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
    SlotMapping *Slots = nullptr,
    DataLayoutCallbackTy DataLayoutCallback = [](StringRef, StringRef) {
      return std::nullopt;
    });

/// Only for use in llvm-as for testing; this does not produce a valid module.
ParsedModuleAndIndex parseAssemblyFileWithIndexNoUpgradeDebugInfo(
    StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
    SlotMapping *Slots, DataLayoutCallbackTy DataLayoutCallback);

/// This function is a main interface to the LLVM Assembly Parser. It parses
/// an ASCII file that (presumably) contains LLVM Assembly code for a module
/// summary. It returns a ModuleSummaryIndex with the corresponding features.
/// Note that this does not verify that the generated Index is valid, so you
/// should run the verifier after parsing the file to check that it is okay.
/// Parse LLVM Assembly Index from a file
/// \param Filename The name of the file to parse
/// \param Err Error result info.
std::unique_ptr<ModuleSummaryIndex>
parseSummaryIndexAssemblyFile(StringRef Filename, SMDiagnostic &Err);

/// The function is a secondary interface to the LLVM Assembly Parser. It parses
/// an ASCII string that (presumably) contains LLVM Assembly code for a module
/// summary. It returns a a ModuleSummaryIndex with the corresponding features.
/// Note that this does not verify that the generated Index is valid, so you
/// should run the verifier after parsing the file to check that it is okay.
/// Parse LLVM Assembly from a string
/// \param AsmString The string containing assembly
/// \param Err Error result info.
std::unique_ptr<ModuleSummaryIndex>
parseSummaryIndexAssemblyString(StringRef AsmString, SMDiagnostic &Err);

/// parseAssemblyFile and parseAssemblyString are wrappers around this function.
/// Parse LLVM Assembly from a MemoryBuffer.
/// \param F The MemoryBuffer containing assembly
/// \param Err Error result info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
std::unique_ptr<Module> parseAssembly(
    MemoryBufferRef F, SMDiagnostic &Err, LLVMContext &Context,
    SlotMapping *Slots = nullptr,
    DataLayoutCallbackTy DataLayoutCallback = [](StringRef, StringRef) {
      return std::nullopt;
    });

/// Parse LLVM Assembly including the summary index from a MemoryBuffer.
///
/// \param F The MemoryBuffer containing assembly with summary
/// \param Err Error result info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
///
/// parseAssemblyFileWithIndex is a wrapper around this function.
ParsedModuleAndIndex parseAssemblyWithIndex(MemoryBufferRef F,
                                            SMDiagnostic &Err,
                                            LLVMContext &Context,
                                            SlotMapping *Slots = nullptr);

/// Parse LLVM Assembly for summary index from a MemoryBuffer.
///
/// \param F The MemoryBuffer containing assembly with summary
/// \param Err Error result info.
///
/// parseSummaryIndexAssemblyFile is a wrapper around this function.
std::unique_ptr<ModuleSummaryIndex>
parseSummaryIndexAssembly(MemoryBufferRef F, SMDiagnostic &Err);

/// This function is the low-level interface to the LLVM Assembly Parser.
/// This is kept as an independent function instead of being inlined into
/// parseAssembly for the convenience of interactive users that want to add
/// recently parsed bits to an existing module.
///
/// \param F The MemoryBuffer containing assembly
/// \param M The module to add data to.
/// \param Index The index to add data to.
/// \param Err Error result info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \return true on error.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
bool parseAssemblyInto(
    MemoryBufferRef F, Module *M, ModuleSummaryIndex *Index, SMDiagnostic &Err,
    SlotMapping *Slots = nullptr,
    DataLayoutCallbackTy DataLayoutCallback = [](StringRef, StringRef) {
      return std::nullopt;
    });

/// Parse a type and a constant value in the given string.
///
/// The constant value can be any LLVM constant, including a constant
/// expression.
///
/// \param Slots The optional slot mapping that will restore the parsing state
/// of the module.
/// \return null on error.
Constant *parseConstantValue(StringRef Asm, SMDiagnostic &Err, const Module &M,
                             const SlotMapping *Slots = nullptr);

/// Parse a type in the given string.
///
/// \param Slots The optional slot mapping that will restore the parsing state
/// of the module.
/// \return null on error.
Type *parseType(StringRef Asm, SMDiagnostic &Err, const Module &M,
                const SlotMapping *Slots = nullptr);

/// Parse a string \p Asm that starts with a type.
/// \p Read[out] gives the number of characters that have been read to parse
/// the type in \p Asm.
///
/// \param Slots The optional slot mapping that will restore the parsing state
/// of the module.
/// \return null on error.
Type *parseTypeAtBeginning(StringRef Asm, unsigned &Read, SMDiagnostic &Err,
                           const Module &M, const SlotMapping *Slots = nullptr);

DIExpression *parseDIExpressionBodyAtBeginning(StringRef Asm, unsigned &Read,
                                               SMDiagnostic &Err,
                                               const Module &M,
                                               const SlotMapping *Slots);

} // End llvm namespace

#endif
