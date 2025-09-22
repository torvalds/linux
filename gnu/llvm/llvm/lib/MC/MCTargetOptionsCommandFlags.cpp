//===-- MCTargetOptionsCommandFlags.cpp -----------------------*- C++ //-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains machine code-specific flags that are shared between
// different command line tools.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define MCOPT(TY, NAME)                                                        \
  static cl::opt<TY> *NAME##View;                                              \
  TY llvm::mc::get##NAME() {                                                   \
    assert(NAME##View && "RegisterMCTargetOptionsFlags not created.");         \
    return *NAME##View;                                                        \
  }

#define MCOPT_EXP(TY, NAME)                                                    \
  MCOPT(TY, NAME)                                                              \
  std::optional<TY> llvm::mc::getExplicit##NAME() {                            \
    if (NAME##View->getNumOccurrences()) {                                     \
      TY res = *NAME##View;                                                    \
      return res;                                                              \
    }                                                                          \
    return std::nullopt;                                                       \
  }

MCOPT_EXP(bool, RelaxAll)
MCOPT(bool, IncrementalLinkerCompatible)
MCOPT(bool, FDPIC)
MCOPT(int, DwarfVersion)
MCOPT(bool, Dwarf64)
MCOPT(EmitDwarfUnwindType, EmitDwarfUnwind)
MCOPT(bool, EmitCompactUnwindNonCanonical)
MCOPT(bool, ShowMCInst)
MCOPT(bool, FatalWarnings)
MCOPT(bool, NoWarn)
MCOPT(bool, NoDeprecatedWarn)
MCOPT(bool, NoTypeCheck)
MCOPT(bool, SaveTempLabels)
MCOPT(bool, Crel)
MCOPT(bool, X86RelaxRelocations)
MCOPT(bool, X86Sse2Avx)
MCOPT(std::string, ABIName)
MCOPT(std::string, AsSecureLogFile)

llvm::mc::RegisterMCTargetOptionsFlags::RegisterMCTargetOptionsFlags() {
#define MCBINDOPT(NAME)                                                        \
  do {                                                                         \
    NAME##View = std::addressof(NAME);                                         \
  } while (0)

  static cl::opt<bool> RelaxAll(
      "mc-relax-all", cl::desc("When used with filetype=obj, relax all fixups "
                               "in the emitted object file"));
  MCBINDOPT(RelaxAll);

  static cl::opt<bool> IncrementalLinkerCompatible(
      "incremental-linker-compatible",
      cl::desc(
          "When used with filetype=obj, "
          "emit an object file which can be used with an incremental linker"));
  MCBINDOPT(IncrementalLinkerCompatible);

  static cl::opt<bool> FDPIC("fdpic", cl::desc("Use the FDPIC ABI"));
  MCBINDOPT(FDPIC);

  static cl::opt<int> DwarfVersion("dwarf-version", cl::desc("Dwarf version"),
                                   cl::init(0));
  MCBINDOPT(DwarfVersion);

  static cl::opt<bool> Dwarf64(
      "dwarf64",
      cl::desc("Generate debugging info in the 64-bit DWARF format"));
  MCBINDOPT(Dwarf64);

  static cl::opt<EmitDwarfUnwindType> EmitDwarfUnwind(
      "emit-dwarf-unwind", cl::desc("Whether to emit DWARF EH frame entries."),
      cl::init(EmitDwarfUnwindType::Default),
      cl::values(clEnumValN(EmitDwarfUnwindType::Always, "always",
                            "Always emit EH frame entries"),
                 clEnumValN(EmitDwarfUnwindType::NoCompactUnwind,
                            "no-compact-unwind",
                            "Only emit EH frame entries when compact unwind is "
                            "not available"),
                 clEnumValN(EmitDwarfUnwindType::Default, "default",
                            "Use target platform default")));
  MCBINDOPT(EmitDwarfUnwind);

  static cl::opt<bool> EmitCompactUnwindNonCanonical(
      "emit-compact-unwind-non-canonical",
      cl::desc(
          "Whether to try to emit Compact Unwind for non canonical entries."),
      cl::init(
          false)); // By default, use DWARF for non-canonical personalities.
  MCBINDOPT(EmitCompactUnwindNonCanonical);

  static cl::opt<bool> ShowMCInst(
      "asm-show-inst",
      cl::desc("Emit internal instruction representation to assembly file"));
  MCBINDOPT(ShowMCInst);

  static cl::opt<bool> FatalWarnings("fatal-warnings",
                                     cl::desc("Treat warnings as errors"));
  MCBINDOPT(FatalWarnings);

  static cl::opt<bool> NoWarn("no-warn", cl::desc("Suppress all warnings"));
  static cl::alias NoWarnW("W", cl::desc("Alias for --no-warn"),
                           cl::aliasopt(NoWarn));
  MCBINDOPT(NoWarn);

  static cl::opt<bool> NoDeprecatedWarn(
      "no-deprecated-warn", cl::desc("Suppress all deprecated warnings"));
  MCBINDOPT(NoDeprecatedWarn);

  static cl::opt<bool> NoTypeCheck(
      "no-type-check", cl::desc("Suppress type errors (Wasm)"));
  MCBINDOPT(NoTypeCheck);

  static cl::opt<bool> SaveTempLabels(
      "save-temp-labels", cl::desc("Don't discard temporary labels"));
  MCBINDOPT(SaveTempLabels);

  static cl::opt<bool> Crel("crel",
                            cl::desc("Use CREL relocation format for ELF"));
  MCBINDOPT(Crel);

  static cl::opt<bool> X86RelaxRelocations(
      "x86-relax-relocations",
      cl::desc(
          "Emit GOTPCRELX/REX_GOTPCRELX instead of GOTPCREL on x86-64 ELF"),
      cl::init(true));
  MCBINDOPT(X86RelaxRelocations);

  static cl::opt<bool> X86Sse2Avx(
      "x86-sse2avx", cl::desc("Specify that the assembler should encode SSE "
                              "instructions with VEX prefix"));
  MCBINDOPT(X86Sse2Avx);

  static cl::opt<std::string> ABIName(
      "target-abi", cl::Hidden,
      cl::desc("The name of the ABI to be targeted from the backend."),
      cl::init(""));
  MCBINDOPT(ABIName);

  static cl::opt<std::string> AsSecureLogFile(
      "as-secure-log-file", cl::desc("As secure log file name"), cl::Hidden);
  MCBINDOPT(AsSecureLogFile);

#undef MCBINDOPT
}

MCTargetOptions llvm::mc::InitMCTargetOptionsFromFlags() {
  MCTargetOptions Options;
  Options.MCRelaxAll = getRelaxAll();
  Options.MCIncrementalLinkerCompatible = getIncrementalLinkerCompatible();
  Options.FDPIC = getFDPIC();
  Options.Dwarf64 = getDwarf64();
  Options.DwarfVersion = getDwarfVersion();
  Options.ShowMCInst = getShowMCInst();
  Options.ABIName = getABIName();
  Options.MCFatalWarnings = getFatalWarnings();
  Options.MCNoWarn = getNoWarn();
  Options.MCNoDeprecatedWarn = getNoDeprecatedWarn();
  Options.MCNoTypeCheck = getNoTypeCheck();
  Options.MCSaveTempLabels = getSaveTempLabels();
  Options.Crel = getCrel();
  Options.X86RelaxRelocations = getX86RelaxRelocations();
  Options.X86Sse2Avx = getX86Sse2Avx();
  Options.EmitDwarfUnwind = getEmitDwarfUnwind();
  Options.EmitCompactUnwindNonCanonical = getEmitCompactUnwindNonCanonical();
  Options.AsSecureLogFile = getAsSecureLogFile();

  return Options;
}
