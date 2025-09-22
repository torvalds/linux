//===-- WebAssemblyMCTargetDesc.cpp - WebAssembly Target Descriptions -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides WebAssembly-specific target descriptions.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "MCTargetDesc/WebAssemblyInstPrinter.h"
#include "MCTargetDesc/WebAssemblyMCAsmInfo.h"
#include "MCTargetDesc/WebAssemblyTargetStreamer.h"
#include "TargetInfo/WebAssemblyTargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-mc-target-desc"

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "WebAssemblyGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "WebAssemblyGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "WebAssemblyGenRegisterInfo.inc"

// Exception handling & setjmp-longjmp handling related options.

// Emscripten's asm.js-style exception handling
cl::opt<bool> WebAssembly::WasmEnableEmEH(
    "enable-emscripten-cxx-exceptions",
    cl::desc("WebAssembly Emscripten-style exception handling"),
    cl::init(false));
// Emscripten's asm.js-style setjmp/longjmp handling
cl::opt<bool> WebAssembly::WasmEnableEmSjLj(
    "enable-emscripten-sjlj",
    cl::desc("WebAssembly Emscripten-style setjmp/longjmp handling"),
    cl::init(false));
// Exception handling using wasm EH instructions
cl::opt<bool>
    WebAssembly::WasmEnableEH("wasm-enable-eh",
                              cl::desc("WebAssembly exception handling"));
// setjmp/longjmp handling using wasm EH instrutions
cl::opt<bool> WebAssembly::WasmEnableSjLj(
    "wasm-enable-sjlj", cl::desc("WebAssembly setjmp/longjmp handling"));
// Whether we use the new exnref Wasm EH proposal adopted on Oct 2023.
// Should be used with -wasm-enable-eh.
// Currently set to false by default, but will later change to true and then
// later can be removed after the legacy WAsm EH instructions are removed.
cl::opt<bool> WebAssembly::WasmEnableExnref(
    "wasm-enable-exnref", cl::desc("WebAssembly exception handling (exnref)"),
    cl::init(false));

static MCAsmInfo *createMCAsmInfo(const MCRegisterInfo & /*MRI*/,
                                  const Triple &TT,
                                  const MCTargetOptions &Options) {
  return new WebAssemblyMCAsmInfo(TT, Options);
}

static MCInstrInfo *createMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitWebAssemblyMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createMCRegisterInfo(const Triple & /*T*/) {
  auto *X = new MCRegisterInfo();
  InitWebAssemblyMCRegisterInfo(X, 0);
  return X;
}

static MCInstPrinter *createMCInstPrinter(const Triple & /*T*/,
                                          unsigned SyntaxVariant,
                                          const MCAsmInfo &MAI,
                                          const MCInstrInfo &MII,
                                          const MCRegisterInfo &MRI) {
  assert(SyntaxVariant == 0 && "WebAssembly only has one syntax variant");
  return new WebAssemblyInstPrinter(MAI, MII, MRI);
}

static MCCodeEmitter *createCodeEmitter(const MCInstrInfo &MCII,
                                        MCContext &Ctx) {
  return createWebAssemblyMCCodeEmitter(MCII, Ctx);
}

static MCAsmBackend *createAsmBackend(const Target & /*T*/,
                                      const MCSubtargetInfo &STI,
                                      const MCRegisterInfo & /*MRI*/,
                                      const MCTargetOptions & /*Options*/) {
  return createWebAssemblyAsmBackend(STI.getTargetTriple());
}

static MCSubtargetInfo *createMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                              StringRef FS) {
  return createWebAssemblyMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCTargetStreamer *
createObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new WebAssemblyTargetWasmStreamer(S);
}

static MCTargetStreamer *
createAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                        MCInstPrinter * /*InstPrint*/) {
  return new WebAssemblyTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) {
  return new WebAssemblyTargetNullStreamer(S);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeWebAssemblyTargetMC() {
  for (Target *T :
       {&getTheWebAssemblyTarget32(), &getTheWebAssemblyTarget64()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createMCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createMCRegisterInfo);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createMCInstPrinter);

    // Register the MC code emitter.
    TargetRegistry::RegisterMCCodeEmitter(*T, createCodeEmitter);

    // Register the ASM Backend.
    TargetRegistry::RegisterMCAsmBackend(*T, createAsmBackend);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createMCSubtargetInfo);

    // Register the object target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(*T,
                                                 createObjectTargetStreamer);
    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createAsmTargetStreamer);
    // Register the null target streamer.
    TargetRegistry::RegisterNullTargetStreamer(*T, createNullTargetStreamer);
  }
}
