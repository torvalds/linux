//===- ErlangGCPrinter.cpp - Erlang/OTP frametable emitter ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the compiler plugin that is used in order to emit
// garbage collection information in a convenient layout for parsing and
// loading in the Erlang/OTP runtime.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/GCMetadataPrinter.h"
#include "llvm/IR/BuiltinGCs.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

using namespace llvm;

namespace {

class ErlangGCPrinter : public GCMetadataPrinter {
public:
  void finishAssembly(Module &M, GCModuleInfo &Info, AsmPrinter &AP) override;
};

} // end anonymous namespace

static GCMetadataPrinterRegistry::Add<ErlangGCPrinter>
    X("erlang", "erlang-compatible garbage collector");

void ErlangGCPrinter::finishAssembly(Module &M, GCModuleInfo &Info,
                                     AsmPrinter &AP) {
  MCStreamer &OS = *AP.OutStreamer;
  unsigned IntPtrSize = M.getDataLayout().getPointerSize();

  // Put this in a custom .note section.
  OS.switchSection(AP.getObjFileLowering().getContext().getELFSection(
      ".note.gc", ELF::SHT_PROGBITS, 0));

  // For each function...
  for (GCModuleInfo::FuncInfoVec::iterator FI = Info.funcinfo_begin(),
                                           IE = Info.funcinfo_end();
       FI != IE; ++FI) {
    GCFunctionInfo &MD = **FI;
    if (MD.getStrategy().getName() != getStrategy().getName())
      // this function is managed by some other GC
      continue;
    /** A compact GC layout. Emit this data structure:
     *
     * struct {
     *   int16_t PointCount;
     *   void *SafePointAddress[PointCount];
     *   int16_t StackFrameSize; (in words)
     *   int16_t StackArity;
     *   int16_t LiveCount;
     *   int16_t LiveOffsets[LiveCount];
     * } __gcmap_<FUNCTIONNAME>;
     **/

    // Align to address width.
    AP.emitAlignment(IntPtrSize == 4 ? Align(4) : Align(8));

    // Emit PointCount.
    OS.AddComment("safe point count");
    AP.emitInt16(MD.size());

    // And each safe point...
    for (const GCPoint &P : MD) {
      // Emit the address of the safe point.
      OS.AddComment("safe point address");
      MCSymbol *Label = P.Label;
      AP.emitLabelPlusOffset(Label /*Hi*/, 0 /*Offset*/, 4 /*Size*/);
    }

    // Stack information never change in safe points! Only print info from the
    // first call-site.
    GCFunctionInfo::iterator PI = MD.begin();

    // Emit the stack frame size.
    OS.AddComment("stack frame size (in words)");
    AP.emitInt16(MD.getFrameSize() / IntPtrSize);

    // Emit stack arity, i.e. the number of stacked arguments.
    unsigned RegisteredArgs = IntPtrSize == 4 ? 5 : 6;
    unsigned StackArity = MD.getFunction().arg_size() > RegisteredArgs
                              ? MD.getFunction().arg_size() - RegisteredArgs
                              : 0;
    OS.AddComment("stack arity");
    AP.emitInt16(StackArity);

    // Emit the number of live roots in the function.
    OS.AddComment("live root count");
    AP.emitInt16(MD.live_size(PI));

    // And for each live root...
    for (GCFunctionInfo::live_iterator LI = MD.live_begin(PI),
                                       LE = MD.live_end(PI);
         LI != LE; ++LI) {
      // Emit live root's offset within the stack frame.
      OS.AddComment("stack index (offset / wordsize)");
      AP.emitInt16(LI->StackOffset / IntPtrSize);
    }
  }
}

void llvm::linkErlangGCPrinter() {}
