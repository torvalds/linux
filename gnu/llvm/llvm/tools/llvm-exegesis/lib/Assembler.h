//===-- Assembler.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines classes to assemble functions composed of a single basic block of
/// MCInsts.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_ASSEMBLER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_ASSEMBLER_H

#include <memory>

#include "BenchmarkCode.h"
#include "Error.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
namespace exegesis {

class ExegesisTarget;

// Gather the set of reserved registers (depends on function's calling
// convention and target machine).
BitVector getFunctionReservedRegs(const TargetMachine &TM);

// Helper to fill in a basic block.
class BasicBlockFiller {
public:
  BasicBlockFiller(MachineFunction &MF, MachineBasicBlock *MBB,
                   const MCInstrInfo *MCII);

  void addInstruction(const MCInst &Inst, const DebugLoc &DL = DebugLoc());
  void addInstructions(ArrayRef<MCInst> Insts, const DebugLoc &DL = DebugLoc());

  void addReturn(const ExegesisTarget &ET, bool SubprocessCleanup,
                 const DebugLoc &DL = DebugLoc());

  MachineFunction &MF;
  MachineBasicBlock *const MBB;
  const MCInstrInfo *const MCII;
};

// Helper to fill in a function.
class FunctionFiller {
public:
  FunctionFiller(MachineFunction &MF, std::vector<unsigned> RegistersSetUp);

  // Adds a basic block to the function.
  BasicBlockFiller addBasicBlock();

  // Returns the function entry point.
  BasicBlockFiller getEntry() { return Entry; }

  MachineFunction &MF;
  const MCInstrInfo *const MCII;

  // Returns the set of registers in the snippet setup code.
  ArrayRef<unsigned> getRegistersSetUp() const;

private:
  BasicBlockFiller Entry;
  // The set of registers that are set up in the basic block.
  std::vector<unsigned> RegistersSetUp;
};

// A callback that fills a function.
using FillFunction = std::function<void(FunctionFiller &)>;

// Creates a temporary `void foo(char*)` function containing the provided
// Instructions. Runs a set of llvm Passes to provide correct prologue and
// epilogue. Once the MachineFunction is ready, it is assembled for TM to
// AsmStream, the temporary function is eventually discarded.
Error assembleToStream(const ExegesisTarget &ET,
                       std::unique_ptr<LLVMTargetMachine> TM,
                       ArrayRef<unsigned> LiveIns, const FillFunction &Fill,
                       raw_pwrite_stream &AsmStreamm, const BenchmarkKey &Key,
                       bool GenerateMemoryInstructions);

// Creates an ObjectFile in the format understood by the host.
// Note: the resulting object keeps a copy of Buffer so it can be discarded once
// this function returns.
object::OwningBinary<object::ObjectFile> getObjectFromBuffer(StringRef Buffer);

// Loads the content of Filename as on ObjectFile and returns it.
object::OwningBinary<object::ObjectFile> getObjectFromFile(StringRef Filename);

// Consumes an ObjectFile containing a `void foo(char*)` function and make it
// executable.
class ExecutableFunction {
public:
  static Expected<ExecutableFunction>
  create(std::unique_ptr<LLVMTargetMachine> TM,
         object::OwningBinary<object::ObjectFile> &&ObjectFileHolder);

  // Retrieves the function as an array of bytes.
  StringRef getFunctionBytes() const { return FunctionBytes; }

  // Executes the function.
  void operator()(char *Memory) const {
    ((void (*)(char *))(intptr_t)FunctionBytes.data())(Memory);
  }

  StringRef FunctionBytes;

private:
  ExecutableFunction(std::unique_ptr<LLVMContext> Ctx,
                     std::unique_ptr<orc::LLJIT> EJIT, StringRef FunctionBytes);

  std::unique_ptr<LLVMContext> Context;
  std::unique_ptr<orc::LLJIT> ExecJIT;
};

// Copies benchmark function's bytes from benchmark object.
Error getBenchmarkFunctionBytes(const StringRef InputData,
                                std::vector<uint8_t> &Bytes);

// Creates a void(int8*) MachineFunction.
MachineFunction &createVoidVoidPtrMachineFunction(StringRef FunctionID,
                                                  Module *Module,
                                                  MachineModuleInfo *MMI);

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_ASSEMBLER_H
