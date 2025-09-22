//===- ModuleSymbolTable.cpp - symbol table for in-memory IR --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents a symbol table built from in-memory IR. It provides
// access to GlobalValues and should only be used if such access is required
// (e.g. in the LTO implementation).
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ModuleSymbolTable.h"
#include "RecordStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

using namespace llvm;
using namespace object;

void ModuleSymbolTable::addModule(Module *M) {
  if (FirstMod)
    assert(FirstMod->getTargetTriple() == M->getTargetTriple());
  else
    FirstMod = M;

  for (GlobalValue &GV : M->global_values())
    SymTab.push_back(&GV);

  CollectAsmSymbols(*M, [this](StringRef Name, BasicSymbolRef::Flags Flags) {
    SymTab.push_back(new (AsmSymbols.Allocate())
                         AsmSymbol(std::string(Name), Flags));
  });
}

static void
initializeRecordStreamer(const Module &M,
                         function_ref<void(RecordStreamer &)> Init) {
  // This function may be called twice, once for ModuleSummaryIndexAnalysis and
  // the other when writing the IR symbol table. If parsing inline assembly has
  // caused errors in the first run, suppress the second run.
  if (M.getContext().getDiagHandlerPtr()->HasErrors)
    return;
  StringRef InlineAsm = M.getModuleInlineAsm();
  if (InlineAsm.empty())
    return;

  std::string Err;
  const Triple TT(M.getTargetTriple());
  const Target *T = TargetRegistry::lookupTarget(TT.str(), Err);
  assert(T && T->hasMCAsmParser());

  std::unique_ptr<MCRegisterInfo> MRI(T->createMCRegInfo(TT.str()));
  if (!MRI)
    return;

  MCTargetOptions MCOptions;
  std::unique_ptr<MCAsmInfo> MAI(T->createMCAsmInfo(*MRI, TT.str(), MCOptions));
  if (!MAI)
    return;

  std::unique_ptr<MCSubtargetInfo> STI(
      T->createMCSubtargetInfo(TT.str(), "", ""));
  if (!STI)
    return;

  std::unique_ptr<MCInstrInfo> MCII(T->createMCInstrInfo());
  if (!MCII)
    return;

  std::unique_ptr<MemoryBuffer> Buffer(
      MemoryBuffer::getMemBuffer(InlineAsm, "<inline asm>"));
  SourceMgr SrcMgr;
  SrcMgr.AddNewSourceBuffer(std::move(Buffer), SMLoc());

  MCContext MCCtx(TT, MAI.get(), MRI.get(), STI.get(), &SrcMgr);
  std::unique_ptr<MCObjectFileInfo> MOFI(
      T->createMCObjectFileInfo(MCCtx, /*PIC=*/false));
  MOFI->setSDKVersion(M.getSDKVersion());
  MCCtx.setObjectFileInfo(MOFI.get());
  RecordStreamer Streamer(MCCtx, M);
  T->createNullTargetStreamer(Streamer);

  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, MCCtx, Streamer, *MAI));

  std::unique_ptr<MCTargetAsmParser> TAP(
      T->createMCAsmParser(*STI, *Parser, *MCII, MCOptions));
  if (!TAP)
    return;

  MCCtx.setDiagnosticHandler([&](const SMDiagnostic &SMD, bool IsInlineAsm,
                                 const SourceMgr &SrcMgr,
                                 std::vector<const MDNode *> &LocInfos) {
    M.getContext().diagnose(
        DiagnosticInfoSrcMgr(SMD, M.getName(), IsInlineAsm, /*LocCookie=*/0));
  });

  // Module-level inline asm is assumed to use At&t syntax (see
  // AsmPrinter::doInitialization()).
  Parser->setAssemblerDialect(InlineAsm::AD_ATT);

  Parser->setTargetParser(*TAP);
  if (Parser->Run(false))
    return;

  Init(Streamer);
}

void ModuleSymbolTable::CollectAsmSymbols(
    const Module &M,
    function_ref<void(StringRef, BasicSymbolRef::Flags)> AsmSymbol) {
  initializeRecordStreamer(M, [&](RecordStreamer &Streamer) {
    Streamer.flushSymverDirectives();

    for (auto &KV : Streamer) {
      StringRef Key = KV.first();
      RecordStreamer::State Value = KV.second;
      // FIXME: For now we just assume that all asm symbols are executable.
      uint32_t Res = BasicSymbolRef::SF_Executable;
      switch (Value) {
      case RecordStreamer::NeverSeen:
        llvm_unreachable("NeverSeen should have been replaced earlier");
      case RecordStreamer::DefinedGlobal:
        Res |= BasicSymbolRef::SF_Global;
        break;
      case RecordStreamer::Defined:
        break;
      case RecordStreamer::Global:
      case RecordStreamer::Used:
        Res |= BasicSymbolRef::SF_Undefined;
        Res |= BasicSymbolRef::SF_Global;
        break;
      case RecordStreamer::DefinedWeak:
        Res |= BasicSymbolRef::SF_Weak;
        Res |= BasicSymbolRef::SF_Global;
        break;
      case RecordStreamer::UndefinedWeak:
        Res |= BasicSymbolRef::SF_Weak;
        Res |= BasicSymbolRef::SF_Undefined;
      }
      AsmSymbol(Key, BasicSymbolRef::Flags(Res));
    }
  });

  // In ELF, object code generated for x86-32 and some code models of x86-64 may
  // reference the special symbol _GLOBAL_OFFSET_TABLE_ that is not used in the
  // IR. Record it like inline asm symbols.
  Triple TT(M.getTargetTriple());
  if (!TT.isOSBinFormatELF() || !TT.isX86())
    return;
  auto CM = M.getCodeModel();
  if (TT.getArch() == Triple::x86 || CM == CodeModel::Medium ||
      CM == CodeModel::Large) {
    AsmSymbol("_GLOBAL_OFFSET_TABLE_",
              BasicSymbolRef::Flags(BasicSymbolRef::SF_Undefined |
                                    BasicSymbolRef::SF_Global));
  }
}

void ModuleSymbolTable::CollectAsmSymvers(
    const Module &M, function_ref<void(StringRef, StringRef)> AsmSymver) {
  initializeRecordStreamer(M, [&](RecordStreamer &Streamer) {
    for (auto &KV : Streamer.symverAliases())
      for (auto &Alias : KV.second)
        AsmSymver(KV.first->getName(), Alias);
  });
}

void ModuleSymbolTable::printSymbolName(raw_ostream &OS, Symbol S) const {
  if (isa<AsmSymbol *>(S)) {
    OS << cast<AsmSymbol *>(S)->first;
    return;
  }

  auto *GV = cast<GlobalValue *>(S);
  if (GV->hasDLLImportStorageClass())
    OS << "__imp_";

  Mang.getNameWithPrefix(OS, GV, false);
}

uint32_t ModuleSymbolTable::getSymbolFlags(Symbol S) const {
  if (isa<AsmSymbol *>(S))
    return cast<AsmSymbol *>(S)->second;

  auto *GV = cast<GlobalValue *>(S);

  uint32_t Res = BasicSymbolRef::SF_None;
  if (GV->isDeclarationForLinker())
    Res |= BasicSymbolRef::SF_Undefined;
  else if (GV->hasHiddenVisibility() && !GV->hasLocalLinkage())
    Res |= BasicSymbolRef::SF_Hidden;
  if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV)) {
    if (GVar->isConstant())
      Res |= BasicSymbolRef::SF_Const;
  }
  if (const GlobalObject *GO = GV->getAliaseeObject())
    if (isa<Function>(GO) || isa<GlobalIFunc>(GO))
      Res |= BasicSymbolRef::SF_Executable;
  if (isa<GlobalAlias>(GV))
    Res |= BasicSymbolRef::SF_Indirect;
  if (GV->hasPrivateLinkage())
    Res |= BasicSymbolRef::SF_FormatSpecific;
  if (!GV->hasLocalLinkage())
    Res |= BasicSymbolRef::SF_Global;
  if (GV->hasCommonLinkage())
    Res |= BasicSymbolRef::SF_Common;
  if (GV->hasLinkOnceLinkage() || GV->hasWeakLinkage() ||
      GV->hasExternalWeakLinkage())
    Res |= BasicSymbolRef::SF_Weak;

  if (GV->getName().starts_with("llvm."))
    Res |= BasicSymbolRef::SF_FormatSpecific;
  else if (auto *Var = dyn_cast<GlobalVariable>(GV)) {
    if (Var->getSection() == "llvm.metadata")
      Res |= BasicSymbolRef::SF_FormatSpecific;
  }

  return Res;
}
