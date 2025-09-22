//===- Parser.cpp - Main dispatch module for the Parser library -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This library implements the functionality defined in llvm/AsmParser/Parser.h
//
//===----------------------------------------------------------------------===//

#include "llvm/AsmParser/Parser.h"
#include "llvm/AsmParser/LLParser.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include <system_error>

using namespace llvm;

static bool parseAssemblyInto(MemoryBufferRef F, Module *M,
                              ModuleSummaryIndex *Index, SMDiagnostic &Err,
                              SlotMapping *Slots, bool UpgradeDebugInfo,
                              DataLayoutCallbackTy DataLayoutCallback) {
  SourceMgr SM;
  std::unique_ptr<MemoryBuffer> Buf = MemoryBuffer::getMemBuffer(F);
  SM.AddNewSourceBuffer(std::move(Buf), SMLoc());

  std::optional<LLVMContext> OptContext;
  return LLParser(F.getBuffer(), SM, Err, M, Index,
                  M ? M->getContext() : OptContext.emplace(), Slots)
      .Run(UpgradeDebugInfo, DataLayoutCallback);
}

bool llvm::parseAssemblyInto(MemoryBufferRef F, Module *M,
                             ModuleSummaryIndex *Index, SMDiagnostic &Err,
                             SlotMapping *Slots,
                             DataLayoutCallbackTy DataLayoutCallback) {
  return ::parseAssemblyInto(F, M, Index, Err, Slots,
                             /*UpgradeDebugInfo*/ true, DataLayoutCallback);
}

std::unique_ptr<Module>
llvm::parseAssembly(MemoryBufferRef F, SMDiagnostic &Err, LLVMContext &Context,
                    SlotMapping *Slots,
                    DataLayoutCallbackTy DataLayoutCallback) {
  std::unique_ptr<Module> M =
      std::make_unique<Module>(F.getBufferIdentifier(), Context);

  if (parseAssemblyInto(F, M.get(), nullptr, Err, Slots, DataLayoutCallback))
    return nullptr;

  return M;
}

std::unique_ptr<Module> llvm::parseAssemblyFile(StringRef Filename,
                                                SMDiagnostic &Err,
                                                LLVMContext &Context,
                                                SlotMapping *Slots) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(Filename);
  if (std::error_code EC = FileOrErr.getError()) {
    Err = SMDiagnostic(Filename, SourceMgr::DK_Error,
                       "Could not open input file: " + EC.message());
    return nullptr;
  }

  return parseAssembly(FileOrErr.get()->getMemBufferRef(), Err, Context, Slots);
}

static ParsedModuleAndIndex
parseAssemblyWithIndex(MemoryBufferRef F, SMDiagnostic &Err,
                       LLVMContext &Context, SlotMapping *Slots,
                       bool UpgradeDebugInfo,
                       DataLayoutCallbackTy DataLayoutCallback) {
  std::unique_ptr<Module> M =
      std::make_unique<Module>(F.getBufferIdentifier(), Context);
  std::unique_ptr<ModuleSummaryIndex> Index =
      std::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/true);

  if (parseAssemblyInto(F, M.get(), Index.get(), Err, Slots, UpgradeDebugInfo,
                        DataLayoutCallback))
    return {nullptr, nullptr};

  return {std::move(M), std::move(Index)};
}

ParsedModuleAndIndex llvm::parseAssemblyWithIndex(MemoryBufferRef F,
                                                  SMDiagnostic &Err,
                                                  LLVMContext &Context,
                                                  SlotMapping *Slots) {
  return ::parseAssemblyWithIndex(
      F, Err, Context, Slots,
      /*UpgradeDebugInfo*/ true,
      [](StringRef, StringRef) { return std::nullopt; });
}

static ParsedModuleAndIndex
parseAssemblyFileWithIndex(StringRef Filename, SMDiagnostic &Err,
                           LLVMContext &Context, SlotMapping *Slots,
                           bool UpgradeDebugInfo,
                           DataLayoutCallbackTy DataLayoutCallback) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(Filename, /*IsText=*/true);
  if (std::error_code EC = FileOrErr.getError()) {
    Err = SMDiagnostic(Filename, SourceMgr::DK_Error,
                       "Could not open input file: " + EC.message());
    return {nullptr, nullptr};
  }

  return parseAssemblyWithIndex(FileOrErr.get()->getMemBufferRef(), Err,
                                Context, Slots, UpgradeDebugInfo,
                                DataLayoutCallback);
}

ParsedModuleAndIndex
llvm::parseAssemblyFileWithIndex(StringRef Filename, SMDiagnostic &Err,
                                 LLVMContext &Context, SlotMapping *Slots,
                                 DataLayoutCallbackTy DataLayoutCallback) {
  return ::parseAssemblyFileWithIndex(Filename, Err, Context, Slots,
                                      /*UpgradeDebugInfo*/ true,
                                      DataLayoutCallback);
}

ParsedModuleAndIndex llvm::parseAssemblyFileWithIndexNoUpgradeDebugInfo(
    StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
    SlotMapping *Slots, DataLayoutCallbackTy DataLayoutCallback) {
  return ::parseAssemblyFileWithIndex(Filename, Err, Context, Slots,
                                      /*UpgradeDebugInfo*/ false,
                                      DataLayoutCallback);
}

std::unique_ptr<Module> llvm::parseAssemblyString(StringRef AsmString,
                                                  SMDiagnostic &Err,
                                                  LLVMContext &Context,
                                                  SlotMapping *Slots) {
  MemoryBufferRef F(AsmString, "<string>");
  return parseAssembly(F, Err, Context, Slots);
}

static bool parseSummaryIndexAssemblyInto(MemoryBufferRef F,
                                          ModuleSummaryIndex &Index,
                                          SMDiagnostic &Err) {
  SourceMgr SM;
  std::unique_ptr<MemoryBuffer> Buf = MemoryBuffer::getMemBuffer(F);
  SM.AddNewSourceBuffer(std::move(Buf), SMLoc());

  // The parser holds a reference to a context that is unused when parsing the
  // index, but we need to initialize it.
  LLVMContext unusedContext;
  return LLParser(F.getBuffer(), SM, Err, nullptr, &Index, unusedContext)
      .Run(true, [](StringRef, StringRef) { return std::nullopt; });
}

std::unique_ptr<ModuleSummaryIndex>
llvm::parseSummaryIndexAssembly(MemoryBufferRef F, SMDiagnostic &Err) {
  std::unique_ptr<ModuleSummaryIndex> Index =
      std::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/false);

  if (parseSummaryIndexAssemblyInto(F, *Index, Err))
    return nullptr;

  return Index;
}

std::unique_ptr<ModuleSummaryIndex>
llvm::parseSummaryIndexAssemblyFile(StringRef Filename, SMDiagnostic &Err) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(Filename);
  if (std::error_code EC = FileOrErr.getError()) {
    Err = SMDiagnostic(Filename, SourceMgr::DK_Error,
                       "Could not open input file: " + EC.message());
    return nullptr;
  }

  return parseSummaryIndexAssembly(FileOrErr.get()->getMemBufferRef(), Err);
}

std::unique_ptr<ModuleSummaryIndex>
llvm::parseSummaryIndexAssemblyString(StringRef AsmString, SMDiagnostic &Err) {
  MemoryBufferRef F(AsmString, "<string>");
  return parseSummaryIndexAssembly(F, Err);
}

Constant *llvm::parseConstantValue(StringRef Asm, SMDiagnostic &Err,
                                   const Module &M, const SlotMapping *Slots) {
  SourceMgr SM;
  std::unique_ptr<MemoryBuffer> Buf = MemoryBuffer::getMemBuffer(Asm);
  SM.AddNewSourceBuffer(std::move(Buf), SMLoc());
  Constant *C;
  if (LLParser(Asm, SM, Err, const_cast<Module *>(&M), nullptr, M.getContext())
          .parseStandaloneConstantValue(C, Slots))
    return nullptr;
  return C;
}

Type *llvm::parseType(StringRef Asm, SMDiagnostic &Err, const Module &M,
                      const SlotMapping *Slots) {
  unsigned Read;
  Type *Ty = parseTypeAtBeginning(Asm, Read, Err, M, Slots);
  if (!Ty)
    return nullptr;
  if (Read != Asm.size()) {
    SourceMgr SM;
    std::unique_ptr<MemoryBuffer> Buf = MemoryBuffer::getMemBuffer(Asm);
    SM.AddNewSourceBuffer(std::move(Buf), SMLoc());
    Err = SM.GetMessage(SMLoc::getFromPointer(Asm.begin() + Read),
                        SourceMgr::DK_Error, "expected end of string");
    return nullptr;
  }
  return Ty;
}
Type *llvm::parseTypeAtBeginning(StringRef Asm, unsigned &Read,
                                 SMDiagnostic &Err, const Module &M,
                                 const SlotMapping *Slots) {
  SourceMgr SM;
  std::unique_ptr<MemoryBuffer> Buf = MemoryBuffer::getMemBuffer(Asm);
  SM.AddNewSourceBuffer(std::move(Buf), SMLoc());
  Type *Ty;
  if (LLParser(Asm, SM, Err, const_cast<Module *>(&M), nullptr, M.getContext())
          .parseTypeAtBeginning(Ty, Read, Slots))
    return nullptr;
  return Ty;
}

DIExpression *llvm::parseDIExpressionBodyAtBeginning(StringRef Asm,
                                                     unsigned &Read,
                                                     SMDiagnostic &Err,
                                                     const Module &M,
                                                     const SlotMapping *Slots) {
  SourceMgr SM;
  std::unique_ptr<MemoryBuffer> Buf = MemoryBuffer::getMemBuffer(Asm);
  SM.AddNewSourceBuffer(std::move(Buf), SMLoc());
  MDNode *MD;
  if (LLParser(Asm, SM, Err, const_cast<Module *>(&M), nullptr, M.getContext())
          .parseDIExpressionBodyAtBeginning(MD, Read, Slots))
    return nullptr;
  return dyn_cast<DIExpression>(MD);
}
