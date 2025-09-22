//===- FileAnalysis.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FileAnalysis.h"
#include "GraphBuilder.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
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
#include "llvm/MC/MCTargetOptions.h"
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

using Instr = llvm::cfi_verify::FileAnalysis::Instr;
using LLVMSymbolizer = llvm::symbolize::LLVMSymbolizer;

namespace llvm {
namespace cfi_verify {

bool IgnoreDWARFFlag;

static cl::opt<bool, true> IgnoreDWARFArg(
    "ignore-dwarf",
    cl::desc(
        "Ignore all DWARF data. This relaxes the requirements for all "
        "statically linked libraries to have been compiled with '-g', but "
        "will result in false positives for 'CFI unprotected' instructions."),
    cl::location(IgnoreDWARFFlag), cl::init(false));

StringRef stringCFIProtectionStatus(CFIProtectionStatus Status) {
  switch (Status) {
  case CFIProtectionStatus::PROTECTED:
    return "PROTECTED";
  case CFIProtectionStatus::FAIL_NOT_INDIRECT_CF:
    return "FAIL_NOT_INDIRECT_CF";
  case CFIProtectionStatus::FAIL_ORPHANS:
    return "FAIL_ORPHANS";
  case CFIProtectionStatus::FAIL_BAD_CONDITIONAL_BRANCH:
    return "FAIL_BAD_CONDITIONAL_BRANCH";
  case CFIProtectionStatus::FAIL_REGISTER_CLOBBERED:
    return "FAIL_REGISTER_CLOBBERED";
  case CFIProtectionStatus::FAIL_INVALID_INSTRUCTION:
    return "FAIL_INVALID_INSTRUCTION";
  }
  llvm_unreachable("Attempted to stringify an unknown enum value.");
}

Expected<FileAnalysis> FileAnalysis::Create(StringRef Filename) {
  // Open the filename provided.
  Expected<object::OwningBinary<object::Binary>> BinaryOrErr =
      object::createBinary(Filename);
  if (!BinaryOrErr)
    return BinaryOrErr.takeError();

  // Construct the object and allow it to take ownership of the binary.
  object::OwningBinary<object::Binary> Binary = std::move(BinaryOrErr.get());
  FileAnalysis Analysis(std::move(Binary));

  Analysis.Object = dyn_cast<object::ObjectFile>(Analysis.Binary.getBinary());
  if (!Analysis.Object)
    return make_error<UnsupportedDisassembly>("Failed to cast object");

  switch (Analysis.Object->getArch()) {
    case Triple::x86:
    case Triple::x86_64:
    case Triple::aarch64:
    case Triple::aarch64_be:
      break;
    default:
      return make_error<UnsupportedDisassembly>("Unsupported architecture.");
  }

  Analysis.ObjectTriple = Analysis.Object->makeTriple();
  Expected<SubtargetFeatures> Features = Analysis.Object->getFeatures();
  if (!Features)
    return Features.takeError();

  Analysis.Features = *Features;

  // Init the rest of the object.
  if (auto InitResponse = Analysis.initialiseDisassemblyMembers())
    return std::move(InitResponse);

  if (auto SectionParseResponse = Analysis.parseCodeSections())
    return std::move(SectionParseResponse);

  if (auto SymbolTableParseResponse = Analysis.parseSymbolTable())
    return std::move(SymbolTableParseResponse);

  return std::move(Analysis);
}

FileAnalysis::FileAnalysis(object::OwningBinary<object::Binary> Binary)
    : Binary(std::move(Binary)) {}

FileAnalysis::FileAnalysis(const Triple &ObjectTriple,
                           const SubtargetFeatures &Features)
    : ObjectTriple(ObjectTriple), Features(Features) {}

const Instr *
FileAnalysis::getPrevInstructionSequential(const Instr &InstrMeta) const {
  std::map<uint64_t, Instr>::const_iterator KV =
      Instructions.find(InstrMeta.VMAddress);
  if (KV == Instructions.end() || KV == Instructions.begin())
    return nullptr;

  if (!(--KV)->second.Valid)
    return nullptr;

  return &KV->second;
}

const Instr *
FileAnalysis::getNextInstructionSequential(const Instr &InstrMeta) const {
  std::map<uint64_t, Instr>::const_iterator KV =
      Instructions.find(InstrMeta.VMAddress);
  if (KV == Instructions.end() || ++KV == Instructions.end())
    return nullptr;

  if (!KV->second.Valid)
    return nullptr;

  return &KV->second;
}

bool FileAnalysis::usesRegisterOperand(const Instr &InstrMeta) const {
  for (const auto &Operand : InstrMeta.Instruction) {
    if (Operand.isReg())
      return true;
  }
  return false;
}

const Instr *FileAnalysis::getInstruction(uint64_t Address) const {
  const auto &InstrKV = Instructions.find(Address);
  if (InstrKV == Instructions.end())
    return nullptr;

  return &InstrKV->second;
}

const Instr &FileAnalysis::getInstructionOrDie(uint64_t Address) const {
  const auto &InstrKV = Instructions.find(Address);
  assert(InstrKV != Instructions.end() && "Address doesn't exist.");
  return InstrKV->second;
}

bool FileAnalysis::isCFITrap(const Instr &InstrMeta) const {
  const auto &InstrDesc = MII->get(InstrMeta.Instruction.getOpcode());
  return InstrDesc.isTrap() || willTrapOnCFIViolation(InstrMeta);
}

bool FileAnalysis::willTrapOnCFIViolation(const Instr &InstrMeta) const {
  const auto &InstrDesc = MII->get(InstrMeta.Instruction.getOpcode());
  if (!InstrDesc.isCall())
    return false;
  uint64_t Target;
  if (!MIA->evaluateBranch(InstrMeta.Instruction, InstrMeta.VMAddress,
                           InstrMeta.InstructionSize, Target))
    return false;
  return TrapOnFailFunctionAddresses.contains(Target);
}

bool FileAnalysis::canFallThrough(const Instr &InstrMeta) const {
  if (!InstrMeta.Valid)
    return false;

  if (isCFITrap(InstrMeta))
    return false;

  const auto &InstrDesc = MII->get(InstrMeta.Instruction.getOpcode());
  if (InstrDesc.mayAffectControlFlow(InstrMeta.Instruction, *RegisterInfo))
    return InstrDesc.isConditionalBranch();

  return true;
}

const Instr *
FileAnalysis::getDefiniteNextInstruction(const Instr &InstrMeta) const {
  if (!InstrMeta.Valid)
    return nullptr;

  if (isCFITrap(InstrMeta))
    return nullptr;

  const auto &InstrDesc = MII->get(InstrMeta.Instruction.getOpcode());
  const Instr *NextMetaPtr;
  if (InstrDesc.mayAffectControlFlow(InstrMeta.Instruction, *RegisterInfo)) {
    if (InstrDesc.isConditionalBranch())
      return nullptr;

    uint64_t Target;
    if (!MIA->evaluateBranch(InstrMeta.Instruction, InstrMeta.VMAddress,
                             InstrMeta.InstructionSize, Target))
      return nullptr;

    NextMetaPtr = getInstruction(Target);
  } else {
    NextMetaPtr =
        getInstruction(InstrMeta.VMAddress + InstrMeta.InstructionSize);
  }

  if (!NextMetaPtr || !NextMetaPtr->Valid)
    return nullptr;

  return NextMetaPtr;
}

std::set<const Instr *>
FileAnalysis::getDirectControlFlowXRefs(const Instr &InstrMeta) const {
  std::set<const Instr *> CFCrossReferences;
  const Instr *PrevInstruction = getPrevInstructionSequential(InstrMeta);

  if (PrevInstruction && canFallThrough(*PrevInstruction))
    CFCrossReferences.insert(PrevInstruction);

  const auto &TargetRefsKV = StaticBranchTargetings.find(InstrMeta.VMAddress);
  if (TargetRefsKV == StaticBranchTargetings.end())
    return CFCrossReferences;

  for (uint64_t SourceInstrAddress : TargetRefsKV->second) {
    const auto &SourceInstrKV = Instructions.find(SourceInstrAddress);
    if (SourceInstrKV == Instructions.end()) {
      errs() << "Failed to find source instruction at address "
             << format_hex(SourceInstrAddress, 2)
             << " for the cross-reference to instruction at address "
             << format_hex(InstrMeta.VMAddress, 2) << ".\n";
      continue;
    }

    CFCrossReferences.insert(&SourceInstrKV->second);
  }

  return CFCrossReferences;
}

const std::set<object::SectionedAddress> &
FileAnalysis::getIndirectInstructions() const {
  return IndirectInstructions;
}

const MCRegisterInfo *FileAnalysis::getRegisterInfo() const {
  return RegisterInfo.get();
}

const MCInstrInfo *FileAnalysis::getMCInstrInfo() const { return MII.get(); }

const MCInstrAnalysis *FileAnalysis::getMCInstrAnalysis() const {
  return MIA.get();
}

Expected<DIInliningInfo>
FileAnalysis::symbolizeInlinedCode(object::SectionedAddress Address) {
  assert(Symbolizer != nullptr && "Symbolizer is invalid.");

  return Symbolizer->symbolizeInlinedCode(std::string(Object->getFileName()),
                                          Address);
}

CFIProtectionStatus
FileAnalysis::validateCFIProtection(const GraphResult &Graph) const {
  const Instr *InstrMetaPtr = getInstruction(Graph.BaseAddress);
  if (!InstrMetaPtr)
    return CFIProtectionStatus::FAIL_INVALID_INSTRUCTION;

  const auto &InstrDesc = MII->get(InstrMetaPtr->Instruction.getOpcode());
  if (!InstrDesc.mayAffectControlFlow(InstrMetaPtr->Instruction, *RegisterInfo))
    return CFIProtectionStatus::FAIL_NOT_INDIRECT_CF;

  if (!usesRegisterOperand(*InstrMetaPtr))
    return CFIProtectionStatus::FAIL_NOT_INDIRECT_CF;

  if (!Graph.OrphanedNodes.empty())
    return CFIProtectionStatus::FAIL_ORPHANS;

  for (const auto &BranchNode : Graph.ConditionalBranchNodes) {
    if (!BranchNode.CFIProtection)
      return CFIProtectionStatus::FAIL_BAD_CONDITIONAL_BRANCH;
  }

  if (indirectCFOperandClobber(Graph) != Graph.BaseAddress)
    return CFIProtectionStatus::FAIL_REGISTER_CLOBBERED;

  return CFIProtectionStatus::PROTECTED;
}

uint64_t FileAnalysis::indirectCFOperandClobber(const GraphResult &Graph) const {
  assert(Graph.OrphanedNodes.empty() && "Orphaned nodes should be empty.");

  // Get the set of registers we must check to ensure they're not clobbered.
  const Instr &IndirectCF = getInstructionOrDie(Graph.BaseAddress);
  DenseSet<unsigned> RegisterNumbers;
  for (const auto &Operand : IndirectCF.Instruction) {
    if (Operand.isReg())
      RegisterNumbers.insert(Operand.getReg());
  }
  assert(RegisterNumbers.size() && "Zero register operands on indirect CF.");

  // Now check all branches to indirect CFs and ensure no clobbering happens.
  for (const auto &Branch : Graph.ConditionalBranchNodes) {
    uint64_t Node;
    if (Branch.IndirectCFIsOnTargetPath)
      Node = Branch.Target;
    else
      Node = Branch.Fallthrough;

    // Some architectures (e.g., AArch64) cannot load in an indirect branch, so
    // we allow them one load.
    bool canLoad = !MII->get(IndirectCF.Instruction.getOpcode()).mayLoad();

    // We walk backwards from the indirect CF.  It is the last node returned by
    // Graph.flattenAddress, so we skip it since we already handled it.
    DenseSet<unsigned> CurRegisterNumbers = RegisterNumbers;
    std::vector<uint64_t> Nodes = Graph.flattenAddress(Node);
    for (auto I = Nodes.rbegin() + 1, E = Nodes.rend(); I != E; ++I) {
      Node = *I;
      const Instr &NodeInstr = getInstructionOrDie(Node);
      const auto &InstrDesc = MII->get(NodeInstr.Instruction.getOpcode());

      for (auto RI = CurRegisterNumbers.begin(), RE = CurRegisterNumbers.end();
           RI != RE; ++RI) {
        unsigned RegNum = *RI;
        if (InstrDesc.hasDefOfPhysReg(NodeInstr.Instruction, RegNum,
                                      *RegisterInfo)) {
          if (!canLoad || !InstrDesc.mayLoad())
            return Node;
          canLoad = false;
          CurRegisterNumbers.erase(RI);
          // Add the registers this load reads to those we check for clobbers.
          for (unsigned i = InstrDesc.getNumDefs(),
                        e = InstrDesc.getNumOperands(); i != e; i++) {
            const auto &Operand = NodeInstr.Instruction.getOperand(i);
            if (Operand.isReg())
              CurRegisterNumbers.insert(Operand.getReg());
          }
          break;
        }
      }
    }
  }

  return Graph.BaseAddress;
}

void FileAnalysis::printInstruction(const Instr &InstrMeta,
                                    raw_ostream &OS) const {
  Printer->printInst(&InstrMeta.Instruction, 0, "", *SubtargetInfo, OS);
}

Error FileAnalysis::initialiseDisassemblyMembers() {
  std::string TripleName = ObjectTriple.getTriple();
  ArchName = "";
  MCPU = "";
  std::string ErrorString;

  LLVMSymbolizer::Options Opt;
  Opt.UseSymbolTable = false;
  Symbolizer.reset(new LLVMSymbolizer(Opt));

  ObjectTarget =
      TargetRegistry::lookupTarget(ArchName, ObjectTriple, ErrorString);
  if (!ObjectTarget)
    return make_error<UnsupportedDisassembly>(
        (Twine("Couldn't find target \"") + ObjectTriple.getTriple() +
         "\", failed with error: " + ErrorString)
            .str());

  RegisterInfo.reset(ObjectTarget->createMCRegInfo(TripleName));
  if (!RegisterInfo)
    return make_error<UnsupportedDisassembly>(
        "Failed to initialise RegisterInfo.");

  MCTargetOptions MCOptions;
  AsmInfo.reset(
      ObjectTarget->createMCAsmInfo(*RegisterInfo, TripleName, MCOptions));
  if (!AsmInfo)
    return make_error<UnsupportedDisassembly>("Failed to initialise AsmInfo.");

  SubtargetInfo.reset(ObjectTarget->createMCSubtargetInfo(
      TripleName, MCPU, Features.getString()));
  if (!SubtargetInfo)
    return make_error<UnsupportedDisassembly>(
        "Failed to initialise SubtargetInfo.");

  MII.reset(ObjectTarget->createMCInstrInfo());
  if (!MII)
    return make_error<UnsupportedDisassembly>("Failed to initialise MII.");

  Context.reset(new MCContext(Triple(TripleName), AsmInfo.get(),
                              RegisterInfo.get(), SubtargetInfo.get()));

  Disassembler.reset(
      ObjectTarget->createMCDisassembler(*SubtargetInfo, *Context));

  if (!Disassembler)
    return make_error<UnsupportedDisassembly>(
        "No disassembler available for target");

  MIA.reset(ObjectTarget->createMCInstrAnalysis(MII.get()));

  Printer.reset(ObjectTarget->createMCInstPrinter(
      ObjectTriple, AsmInfo->getAssemblerDialect(), *AsmInfo, *MII,
      *RegisterInfo));

  return Error::success();
}

Error FileAnalysis::parseCodeSections() {
  if (!IgnoreDWARFFlag) {
    std::unique_ptr<DWARFContext> DWARF = DWARFContext::create(*Object);
    if (!DWARF)
      return make_error<StringError>("Could not create DWARF information.",
                                     inconvertibleErrorCode());

    bool LineInfoValid = false;

    for (auto &Unit : DWARF->compile_units()) {
      const auto &LineTable = DWARF->getLineTableForUnit(Unit.get());
      if (LineTable && !LineTable->Rows.empty()) {
        LineInfoValid = true;
        break;
      }
    }

    if (!LineInfoValid)
      return make_error<StringError>(
          "DWARF line information missing. Did you compile with '-g'?",
          inconvertibleErrorCode());
  }

  for (const object::SectionRef &Section : Object->sections()) {
    // Ensure only executable sections get analysed.
    if (!(object::ELFSectionRef(Section).getFlags() & ELF::SHF_EXECINSTR))
      continue;

    // Avoid checking the PLT since it produces spurious failures on AArch64
    // when ignoring DWARF data.
    Expected<StringRef> NameOrErr = Section.getName();
    if (NameOrErr && *NameOrErr == ".plt")
      continue;
    consumeError(NameOrErr.takeError());

    Expected<StringRef> Contents = Section.getContents();
    if (!Contents)
      return Contents.takeError();
    ArrayRef<uint8_t> SectionBytes = arrayRefFromStringRef(*Contents);

    parseSectionContents(SectionBytes,
                         {Section.getAddress(), Section.getIndex()});
  }
  return Error::success();
}

void FileAnalysis::parseSectionContents(ArrayRef<uint8_t> SectionBytes,
                                        object::SectionedAddress Address) {
  assert(Symbolizer && "Symbolizer is uninitialised.");
  MCInst Instruction;
  Instr InstrMeta;
  uint64_t InstructionSize;

  for (uint64_t Byte = 0; Byte < SectionBytes.size();) {
    bool ValidInstruction =
        Disassembler->getInstruction(Instruction, InstructionSize,
                                     SectionBytes.drop_front(Byte), 0,
                                     outs()) == MCDisassembler::Success;

    Byte += InstructionSize;

    uint64_t VMAddress = Address.Address + Byte - InstructionSize;
    InstrMeta.Instruction = Instruction;
    InstrMeta.VMAddress = VMAddress;
    InstrMeta.InstructionSize = InstructionSize;
    InstrMeta.Valid = ValidInstruction;

    addInstruction(InstrMeta);

    if (!ValidInstruction)
      continue;

    // Skip additional parsing for instructions that do not affect the control
    // flow.
    const auto &InstrDesc = MII->get(Instruction.getOpcode());
    if (!InstrDesc.mayAffectControlFlow(Instruction, *RegisterInfo))
      continue;

    uint64_t Target;
    if (MIA->evaluateBranch(Instruction, VMAddress, InstructionSize, Target)) {
      // If the target can be evaluated, it's not indirect.
      StaticBranchTargetings[Target].push_back(VMAddress);
      continue;
    }

    if (!usesRegisterOperand(InstrMeta))
      continue;

    if (InstrDesc.isReturn())
      continue;

    // Check if this instruction exists in the range of the DWARF metadata.
    if (!IgnoreDWARFFlag) {
      auto LineInfo =
          Symbolizer->symbolizeCode(std::string(Object->getFileName()),
                                    {VMAddress, Address.SectionIndex});
      if (!LineInfo) {
        handleAllErrors(LineInfo.takeError(), [](const ErrorInfoBase &E) {
          errs() << "Symbolizer failed to get line: " << E.message() << "\n";
        });
        continue;
      }

      if (LineInfo->FileName == DILineInfo::BadString)
        continue;
    }

    IndirectInstructions.insert({VMAddress, Address.SectionIndex});
  }
}

void FileAnalysis::addInstruction(const Instr &Instruction) {
  const auto &KV =
      Instructions.insert(std::make_pair(Instruction.VMAddress, Instruction));
  if (!KV.second) {
    errs() << "Failed to add instruction at address "
           << format_hex(Instruction.VMAddress, 2)
           << ": Instruction at this address already exists.\n";
    exit(EXIT_FAILURE);
  }
}

Error FileAnalysis::parseSymbolTable() {
  // Functions that will trap on CFI violations.
  SmallSet<StringRef, 4> TrapOnFailFunctions;
  TrapOnFailFunctions.insert("__cfi_slowpath");
  TrapOnFailFunctions.insert("__cfi_slowpath_diag");
  TrapOnFailFunctions.insert("abort");

  // Look through the list of symbols for functions that will trap on CFI
  // violations.
  for (auto &Sym : Object->symbols()) {
    auto SymNameOrErr = Sym.getName();
    if (!SymNameOrErr)
      consumeError(SymNameOrErr.takeError());
    else if (TrapOnFailFunctions.contains(*SymNameOrErr)) {
      auto AddrOrErr = Sym.getAddress();
      if (!AddrOrErr)
        consumeError(AddrOrErr.takeError());
      else
        TrapOnFailFunctionAddresses.insert(*AddrOrErr);
    }
  }
  if (auto *ElfObject = dyn_cast<object::ELFObjectFileBase>(Object)) {
    for (const auto &Plt : ElfObject->getPltEntries()) {
      if (!Plt.Symbol)
        continue;
      object::SymbolRef Sym(*Plt.Symbol, Object);
      auto SymNameOrErr = Sym.getName();
      if (!SymNameOrErr)
        consumeError(SymNameOrErr.takeError());
      else if (TrapOnFailFunctions.contains(*SymNameOrErr))
        TrapOnFailFunctionAddresses.insert(Plt.Address);
    }
  }
  return Error::success();
}

UnsupportedDisassembly::UnsupportedDisassembly(StringRef Text)
    : Text(std::string(Text)) {}

char UnsupportedDisassembly::ID;
void UnsupportedDisassembly::log(raw_ostream &OS) const {
  OS << "Could not initialise disassembler: " << Text;
}

std::error_code UnsupportedDisassembly::convertToErrorCode() const {
  return std::error_code();
}

} // namespace cfi_verify
} // namespace llvm
