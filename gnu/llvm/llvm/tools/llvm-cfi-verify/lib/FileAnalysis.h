//===- FileAnalysis.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CFI_VERIFY_FILE_ANALYSIS_H
#define LLVM_CFI_VERIFY_FILE_ANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>
#include <set>
#include <string>

namespace llvm {
namespace cfi_verify {

struct GraphResult;

extern bool IgnoreDWARFFlag;

enum class CFIProtectionStatus {
  // This instruction is protected by CFI.
  PROTECTED,
  // The instruction is not an indirect control flow instruction, and thus
  // shouldn't be protected.
  FAIL_NOT_INDIRECT_CF,
  // There is a path to the instruction that was unexpected.
  FAIL_ORPHANS,
  // There is a path to the instruction from a conditional branch that does not
  // properly check the destination for this vcall/icall.
  FAIL_BAD_CONDITIONAL_BRANCH,
  // One of the operands of the indirect CF instruction is modified between the
  // CFI-check and execution.
  FAIL_REGISTER_CLOBBERED,
  // The instruction referenced does not exist. This normally indicates an
  // error in the program, where you try and validate a graph that was created
  // in a different FileAnalysis object.
  FAIL_INVALID_INSTRUCTION,
};

StringRef stringCFIProtectionStatus(CFIProtectionStatus Status);

// Disassembler and analysis tool for machine code files. Keeps track of non-
// sequential control flows, including indirect control flow instructions.
class FileAnalysis {
public:
  // A metadata struct for an instruction.
  struct Instr {
    uint64_t VMAddress;       // Virtual memory address of this instruction.
    MCInst Instruction;       // Instruction.
    uint64_t InstructionSize; // Size of this instruction.
    bool Valid; // Is this a valid instruction? If false, Instr::Instruction is
                // undefined.
  };

  // Construct a FileAnalysis from a file path.
  static Expected<FileAnalysis> Create(StringRef Filename);

  // Construct and take ownership of the supplied object. Do not use this
  // constructor, prefer to use FileAnalysis::Create instead.
  FileAnalysis(object::OwningBinary<object::Binary> Binary);
  FileAnalysis() = delete;
  FileAnalysis(const FileAnalysis &) = delete;
  FileAnalysis(FileAnalysis &&Other) = default;

  // Returns the instruction at the provided address. Returns nullptr if there
  // is no instruction at the provided address.
  const Instr *getInstruction(uint64_t Address) const;

  // Returns the instruction at the provided adress, dying if the instruction is
  // not found.
  const Instr &getInstructionOrDie(uint64_t Address) const;

  // Returns a pointer to the previous/next instruction in sequence,
  // respectively. Returns nullptr if the next/prev instruction doesn't exist,
  // or if the provided instruction doesn't exist.
  const Instr *getPrevInstructionSequential(const Instr &InstrMeta) const;
  const Instr *getNextInstructionSequential(const Instr &InstrMeta) const;

  // Returns whether this instruction is used by CFI to trap the program.
  bool isCFITrap(const Instr &InstrMeta) const;

  // Returns whether this instruction is a call to a function that will trap on
  // CFI violations (i.e., it serves as a trap in this instance).
  bool willTrapOnCFIViolation(const Instr &InstrMeta) const;

  // Returns whether this function can fall through to the next instruction.
  // Undefined (and bad) instructions cannot fall through, and instruction that
  // modify the control flow can only fall through if they are conditional
  // branches or calls.
  bool canFallThrough(const Instr &InstrMeta) const;

  // Returns the definitive next instruction. This is different from the next
  // instruction sequentially as it will follow unconditional branches (assuming
  // they can be resolved at compile time, i.e. not indirect). This method
  // returns nullptr if the provided instruction does not transfer control flow
  // to exactly one instruction that is known deterministically at compile time.
  // Also returns nullptr if the deterministic target does not exist in this
  // file.
  const Instr *getDefiniteNextInstruction(const Instr &InstrMeta) const;

  // Get a list of deterministic control flows that lead to the provided
  // instruction. This list includes all static control flow cross-references as
  // well as the previous instruction if it can fall through.
  std::set<const Instr *>
  getDirectControlFlowXRefs(const Instr &InstrMeta) const;

  // Returns whether this instruction uses a register operand.
  bool usesRegisterOperand(const Instr &InstrMeta) const;

  // Returns the list of indirect instructions.
  const std::set<object::SectionedAddress> &getIndirectInstructions() const;

  const MCRegisterInfo *getRegisterInfo() const;
  const MCInstrInfo *getMCInstrInfo() const;
  const MCInstrAnalysis *getMCInstrAnalysis() const;

  // Returns the inlining information for the provided address.
  Expected<DIInliningInfo>
  symbolizeInlinedCode(object::SectionedAddress Address);

  // Returns whether the provided Graph represents a protected indirect control
  // flow instruction in this file.
  CFIProtectionStatus validateCFIProtection(const GraphResult &Graph) const;

  // Returns the first place the operand register is clobbered between the CFI-
  // check and the indirect CF instruction execution. We do this by walking
  // backwards from the indirect CF and ensuring there is at most one load
  // involving the operand register (which is the indirect CF itself on x86).
  // If the register is not modified, returns the address of the indirect CF
  // instruction. The result is undefined if the provided graph does not fall
  // under either the FAIL_REGISTER_CLOBBERED or PROTECTED status (see
  // CFIProtectionStatus).
  uint64_t indirectCFOperandClobber(const GraphResult& Graph) const;

  // Prints an instruction to the provided stream using this object's pretty-
  // printers.
  void printInstruction(const Instr &InstrMeta, raw_ostream &OS) const;

protected:
  // Construct a blank object with the provided triple and features. Used in
  // testing, where a sub class will dependency inject protected methods to
  // allow analysis of raw binary, without requiring a fully valid ELF file.
  FileAnalysis(const Triple &ObjectTriple, const SubtargetFeatures &Features);

  // Add an instruction to this object.
  void addInstruction(const Instr &Instruction);

  // Disassemble and parse the provided bytes into this object. Instruction
  // address calculation is done relative to the provided SectionAddress.
  void parseSectionContents(ArrayRef<uint8_t> SectionBytes,
                            object::SectionedAddress Address);

  // Constructs and initialises members required for disassembly.
  Error initialiseDisassemblyMembers();

  // Parses code sections from the internal object file. Saves them into the
  // internal members. Should only be called once by Create().
  Error parseCodeSections();

  // Parses the symbol table to look for the addresses of functions that will
  // trap on CFI violations.
  Error parseSymbolTable();

private:
  // Members that describe the input file.
  object::OwningBinary<object::Binary> Binary;
  const object::ObjectFile *Object = nullptr;
  Triple ObjectTriple;
  std::string ArchName;
  std::string MCPU;
  const Target *ObjectTarget = nullptr;
  SubtargetFeatures Features;

  // Members required for disassembly.
  std::unique_ptr<const MCRegisterInfo> RegisterInfo;
  std::unique_ptr<const MCAsmInfo> AsmInfo;
  std::unique_ptr<MCSubtargetInfo> SubtargetInfo;
  std::unique_ptr<const MCInstrInfo> MII;
  std::unique_ptr<MCContext> Context;
  std::unique_ptr<const MCDisassembler> Disassembler;
  std::unique_ptr<const MCInstrAnalysis> MIA;
  std::unique_ptr<MCInstPrinter> Printer;

  // Symbolizer used for debug information parsing.
  std::unique_ptr<symbolize::LLVMSymbolizer> Symbolizer;

  // A mapping between the virtual memory address to the instruction metadata
  // struct. TODO(hctim): Reimplement this as a sorted vector to avoid per-
  // insertion allocation.
  std::map<uint64_t, Instr> Instructions;

  // Contains a mapping between a specific address, and a list of instructions
  // that use this address as a branch target (including call instructions).
  DenseMap<uint64_t, std::vector<uint64_t>> StaticBranchTargetings;

  // A list of addresses of indirect control flow instructions.
  std::set<object::SectionedAddress> IndirectInstructions;

  // The addresses of functions that will trap on CFI violations.
  SmallSet<uint64_t, 4> TrapOnFailFunctionAddresses;
};

class UnsupportedDisassembly : public ErrorInfo<UnsupportedDisassembly> {
public:
  static char ID;
  std::string Text;

  UnsupportedDisassembly(StringRef Text);

  void log(raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;
};

} // namespace cfi_verify
} // namespace llvm

#endif // LLVM_CFI_VERIFY_FILE_ANALYSIS_H
