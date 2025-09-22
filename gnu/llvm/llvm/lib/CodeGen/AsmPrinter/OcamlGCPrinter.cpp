//===- OcamlGCPrinter.cpp - Ocaml frametable emitter ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements printing the assembly code for an Ocaml frametable.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/GCMetadataPrinter.h"
#include "llvm/IR/BuiltinGCs.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace llvm;

namespace {

class OcamlGCMetadataPrinter : public GCMetadataPrinter {
public:
  void beginAssembly(Module &M, GCModuleInfo &Info, AsmPrinter &AP) override;
  void finishAssembly(Module &M, GCModuleInfo &Info, AsmPrinter &AP) override;
};

} // end anonymous namespace

static GCMetadataPrinterRegistry::Add<OcamlGCMetadataPrinter>
    Y("ocaml", "ocaml 3.10-compatible collector");

void llvm::linkOcamlGCPrinter() {}

static void EmitCamlGlobal(const Module &M, AsmPrinter &AP, const char *Id) {
  const std::string &MId = M.getModuleIdentifier();

  std::string SymName;
  SymName += "caml";
  size_t Letter = SymName.size();
  SymName.append(MId.begin(), llvm::find(MId, '.'));
  SymName += "__";
  SymName += Id;

  // Capitalize the first letter of the module name.
  SymName[Letter] = toupper(SymName[Letter]);

  SmallString<128> TmpStr;
  Mangler::getNameWithPrefix(TmpStr, SymName, M.getDataLayout());

  MCSymbol *Sym = AP.OutContext.getOrCreateSymbol(TmpStr);

  AP.OutStreamer->emitSymbolAttribute(Sym, MCSA_Global);
  AP.OutStreamer->emitLabel(Sym);
}

void OcamlGCMetadataPrinter::beginAssembly(Module &M, GCModuleInfo &Info,
                                           AsmPrinter &AP) {
  AP.OutStreamer->switchSection(AP.getObjFileLowering().getTextSection());
  EmitCamlGlobal(M, AP, "code_begin");

  AP.OutStreamer->switchSection(AP.getObjFileLowering().getDataSection());
  EmitCamlGlobal(M, AP, "data_begin");
}

/// emitAssembly - Print the frametable. The ocaml frametable format is thus:
///
///   extern "C" struct align(sizeof(intptr_t)) {
///     uint16_t NumDescriptors;
///     struct align(sizeof(intptr_t)) {
///       void *ReturnAddress;
///       uint16_t FrameSize;
///       uint16_t NumLiveOffsets;
///       uint16_t LiveOffsets[NumLiveOffsets];
///     } Descriptors[NumDescriptors];
///   } caml${module}__frametable;
///
/// Note that this precludes programs from stack frames larger than 64K
/// (FrameSize and LiveOffsets would overflow). FrameTablePrinter will abort if
/// either condition is detected in a function which uses the GC.
///
void OcamlGCMetadataPrinter::finishAssembly(Module &M, GCModuleInfo &Info,
                                            AsmPrinter &AP) {
  unsigned IntPtrSize = M.getDataLayout().getPointerSize();

  AP.OutStreamer->switchSection(AP.getObjFileLowering().getTextSection());
  EmitCamlGlobal(M, AP, "code_end");

  AP.OutStreamer->switchSection(AP.getObjFileLowering().getDataSection());
  EmitCamlGlobal(M, AP, "data_end");

  // FIXME: Why does ocaml emit this??
  AP.OutStreamer->emitIntValue(0, IntPtrSize);

  AP.OutStreamer->switchSection(AP.getObjFileLowering().getDataSection());
  EmitCamlGlobal(M, AP, "frametable");

  int NumDescriptors = 0;
  for (std::unique_ptr<GCFunctionInfo> &FI :
       llvm::make_range(Info.funcinfo_begin(), Info.funcinfo_end())) {
    if (FI->getStrategy().getName() != getStrategy().getName())
      // this function is managed by some other GC
      continue;
    NumDescriptors += FI->size();
  }

  if (NumDescriptors >= 1 << 16) {
    // Very rude!
    report_fatal_error(" Too much descriptor for ocaml GC");
  }
  AP.emitInt16(NumDescriptors);
  AP.emitAlignment(IntPtrSize == 4 ? Align(4) : Align(8));

  for (std::unique_ptr<GCFunctionInfo> &FI :
       llvm::make_range(Info.funcinfo_begin(), Info.funcinfo_end())) {
    if (FI->getStrategy().getName() != getStrategy().getName())
      // this function is managed by some other GC
      continue;

    uint64_t FrameSize = FI->getFrameSize();
    if (FrameSize >= 1 << 16) {
      // Very rude!
      report_fatal_error("Function '" + FI->getFunction().getName() +
                         "' is too large for the ocaml GC! "
                         "Frame size " +
                         Twine(FrameSize) +
                         ">= 65536.\n"
                         "(" +
                         Twine(reinterpret_cast<uintptr_t>(FI.get())) + ")");
    }

    AP.OutStreamer->AddComment("live roots for " +
                               Twine(FI->getFunction().getName()));
    AP.OutStreamer->addBlankLine();

    for (GCFunctionInfo::iterator J = FI->begin(), JE = FI->end(); J != JE;
         ++J) {
      size_t LiveCount = FI->live_size(J);
      if (LiveCount >= 1 << 16) {
        // Very rude!
        report_fatal_error("Function '" + FI->getFunction().getName() +
                           "' is too large for the ocaml GC! "
                           "Live root count " +
                           Twine(LiveCount) + " >= 65536.");
      }

      AP.OutStreamer->emitSymbolValue(J->Label, IntPtrSize);
      AP.emitInt16(FrameSize);
      AP.emitInt16(LiveCount);

      for (GCFunctionInfo::live_iterator K = FI->live_begin(J),
                                         KE = FI->live_end(J);
           K != KE; ++K) {
        if (K->StackOffset >= 1 << 16) {
          // Very rude!
          report_fatal_error(
              "GC root stack offset is outside of fixed stack frame and out "
              "of range for ocaml GC!");
        }
        AP.emitInt16(K->StackOffset);
      }

      AP.emitAlignment(IntPtrSize == 4 ? Align(4) : Align(8));
    }
  }
}
