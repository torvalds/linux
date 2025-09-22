//===- CodeGenTarget.cpp - CodeGen Target Class Wrapper -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class wraps target description classes used by the various code
// generation TableGen backends.  This makes it easier to access the data and
// provides a single place that needs to check it for validity.  All of these
// classes abort on error conditions.
//
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"
#include "CodeGenInstruction.h"
#include "CodeGenRegisters.h"
#include "CodeGenSchedule.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <iterator>
#include <tuple>
using namespace llvm;

cl::OptionCategory AsmParserCat("Options for -gen-asm-parser");
cl::OptionCategory AsmWriterCat("Options for -gen-asm-writer");

static cl::opt<unsigned>
    AsmParserNum("asmparsernum", cl::init(0),
                 cl::desc("Make -gen-asm-parser emit assembly parser #N"),
                 cl::cat(AsmParserCat));

static cl::opt<unsigned>
    AsmWriterNum("asmwriternum", cl::init(0),
                 cl::desc("Make -gen-asm-writer emit assembly writer #N"),
                 cl::cat(AsmWriterCat));

/// getValueType - Return the MVT::SimpleValueType that the specified TableGen
/// record corresponds to.
MVT::SimpleValueType llvm::getValueType(const Record *Rec) {
  return (MVT::SimpleValueType)Rec->getValueAsInt("Value");
}

StringRef llvm::getName(MVT::SimpleValueType T) {
  switch (T) {
  case MVT::Other:
    return "UNKNOWN";
  case MVT::iPTR:
    return "TLI.getPointerTy()";
  case MVT::iPTRAny:
    return "TLI.getPointerTy()";
  default:
    return getEnumName(T);
  }
}

StringRef llvm::getEnumName(MVT::SimpleValueType T) {
  // clang-format off
  switch (T) {
#define GET_VT_ATTR(Ty, N, Sz, Any, Int, FP, Vec, Sc, NElem, EltTy)   \
  case MVT::Ty: return "MVT::" # Ty;
#include "llvm/CodeGen/GenVT.inc"
  default: llvm_unreachable("ILLEGAL VALUE TYPE!");
  }
  // clang-format on
}

/// getQualifiedName - Return the name of the specified record, with a
/// namespace qualifier if the record contains one.
///
std::string llvm::getQualifiedName(const Record *R) {
  std::string Namespace;
  if (R->getValue("Namespace"))
    Namespace = std::string(R->getValueAsString("Namespace"));
  if (Namespace.empty())
    return std::string(R->getName());
  return Namespace + "::" + R->getName().str();
}

/// getTarget - Return the current instance of the Target class.
///
CodeGenTarget::CodeGenTarget(RecordKeeper &records)
    : Records(records), CGH(records) {
  std::vector<Record *> Targets = Records.getAllDerivedDefinitions("Target");
  if (Targets.size() == 0)
    PrintFatalError("No 'Target' subclasses defined!");
  if (Targets.size() != 1)
    PrintFatalError("Multiple subclasses of Target defined!");
  TargetRec = Targets[0];
  MacroFusions = Records.getAllDerivedDefinitions("Fusion");
}

CodeGenTarget::~CodeGenTarget() {}

StringRef CodeGenTarget::getName() const { return TargetRec->getName(); }

/// getInstNamespace - Find and return the target machine's instruction
/// namespace. The namespace is cached because it is requested multiple times.
StringRef CodeGenTarget::getInstNamespace() const {
  if (InstNamespace.empty()) {
    for (const CodeGenInstruction *Inst : getInstructionsByEnumValue()) {
      // We are not interested in the "TargetOpcode" namespace.
      if (Inst->Namespace != "TargetOpcode") {
        InstNamespace = Inst->Namespace;
        break;
      }
    }
  }

  return InstNamespace;
}

StringRef CodeGenTarget::getRegNamespace() const {
  auto &RegClasses = RegBank->getRegClasses();
  return RegClasses.size() > 0 ? RegClasses.front().Namespace : "";
}

Record *CodeGenTarget::getInstructionSet() const {
  return TargetRec->getValueAsDef("InstructionSet");
}

bool CodeGenTarget::getAllowRegisterRenaming() const {
  return TargetRec->getValueAsInt("AllowRegisterRenaming");
}

/// getAsmParser - Return the AssemblyParser definition for this target.
///
Record *CodeGenTarget::getAsmParser() const {
  std::vector<Record *> LI = TargetRec->getValueAsListOfDefs("AssemblyParsers");
  if (AsmParserNum >= LI.size())
    PrintFatalError("Target does not have an AsmParser #" +
                    Twine(AsmParserNum) + "!");
  return LI[AsmParserNum];
}

/// getAsmParserVariant - Return the AssemblyParserVariant definition for
/// this target.
///
Record *CodeGenTarget::getAsmParserVariant(unsigned i) const {
  std::vector<Record *> LI =
      TargetRec->getValueAsListOfDefs("AssemblyParserVariants");
  if (i >= LI.size())
    PrintFatalError("Target does not have an AsmParserVariant #" + Twine(i) +
                    "!");
  return LI[i];
}

/// getAsmParserVariantCount - Return the AssemblyParserVariant definition
/// available for this target.
///
unsigned CodeGenTarget::getAsmParserVariantCount() const {
  std::vector<Record *> LI =
      TargetRec->getValueAsListOfDefs("AssemblyParserVariants");
  return LI.size();
}

/// getAsmWriter - Return the AssemblyWriter definition for this target.
///
Record *CodeGenTarget::getAsmWriter() const {
  std::vector<Record *> LI = TargetRec->getValueAsListOfDefs("AssemblyWriters");
  if (AsmWriterNum >= LI.size())
    PrintFatalError("Target does not have an AsmWriter #" +
                    Twine(AsmWriterNum) + "!");
  return LI[AsmWriterNum];
}

CodeGenRegBank &CodeGenTarget::getRegBank() const {
  if (!RegBank)
    RegBank = std::make_unique<CodeGenRegBank>(Records, getHwModes());
  return *RegBank;
}

std::optional<CodeGenRegisterClass *> CodeGenTarget::getSuperRegForSubReg(
    const ValueTypeByHwMode &ValueTy, CodeGenRegBank &RegBank,
    const CodeGenSubRegIndex *SubIdx, bool MustBeAllocatable) const {
  std::vector<CodeGenRegisterClass *> Candidates;
  auto &RegClasses = RegBank.getRegClasses();

  // Try to find a register class which supports ValueTy, and also contains
  // SubIdx.
  for (CodeGenRegisterClass &RC : RegClasses) {
    // Is there a subclass of this class which contains this subregister index?
    CodeGenRegisterClass *SubClassWithSubReg = RC.getSubClassWithSubReg(SubIdx);
    if (!SubClassWithSubReg)
      continue;

    // We have a class. Check if it supports this value type.
    if (!llvm::is_contained(SubClassWithSubReg->VTs, ValueTy))
      continue;

    // If necessary, check that it is allocatable.
    if (MustBeAllocatable && !SubClassWithSubReg->Allocatable)
      continue;

    // We have a register class which supports both the value type and
    // subregister index. Remember it.
    Candidates.push_back(SubClassWithSubReg);
  }

  // If we didn't find anything, we're done.
  if (Candidates.empty())
    return std::nullopt;

  // Find and return the largest of our candidate classes.
  llvm::stable_sort(Candidates, [&](const CodeGenRegisterClass *A,
                                    const CodeGenRegisterClass *B) {
    if (A->getMembers().size() > B->getMembers().size())
      return true;

    if (A->getMembers().size() < B->getMembers().size())
      return false;

    // Order by name as a tie-breaker.
    return StringRef(A->getName()) < B->getName();
  });

  return Candidates[0];
}

void CodeGenTarget::ReadRegAltNameIndices() const {
  RegAltNameIndices = Records.getAllDerivedDefinitions("RegAltNameIndex");
  llvm::sort(RegAltNameIndices, LessRecord());
}

/// getRegisterByName - If there is a register with the specific AsmName,
/// return it.
const CodeGenRegister *CodeGenTarget::getRegisterByName(StringRef Name) const {
  return getRegBank().getRegistersByName().lookup(Name);
}

const CodeGenRegisterClass &CodeGenTarget::getRegisterClass(Record *R) const {
  return *getRegBank().getRegClass(R);
}

std::vector<ValueTypeByHwMode> CodeGenTarget::getRegisterVTs(Record *R) const {
  const CodeGenRegister *Reg = getRegBank().getReg(R);
  std::vector<ValueTypeByHwMode> Result;
  for (const auto &RC : getRegBank().getRegClasses()) {
    if (RC.contains(Reg)) {
      ArrayRef<ValueTypeByHwMode> InVTs = RC.getValueTypes();
      llvm::append_range(Result, InVTs);
    }
  }

  // Remove duplicates.
  llvm::sort(Result);
  Result.erase(llvm::unique(Result), Result.end());
  return Result;
}

void CodeGenTarget::ReadLegalValueTypes() const {
  for (const auto &RC : getRegBank().getRegClasses())
    llvm::append_range(LegalValueTypes, RC.VTs);

  // Remove duplicates.
  llvm::sort(LegalValueTypes);
  LegalValueTypes.erase(llvm::unique(LegalValueTypes), LegalValueTypes.end());
}

CodeGenSchedModels &CodeGenTarget::getSchedModels() const {
  if (!SchedModels)
    SchedModels = std::make_unique<CodeGenSchedModels>(Records, *this);
  return *SchedModels;
}

void CodeGenTarget::ReadInstructions() const {
  std::vector<Record *> Insts = Records.getAllDerivedDefinitions("Instruction");
  if (Insts.size() <= 2)
    PrintFatalError("No 'Instruction' subclasses defined!");

  // Parse the instructions defined in the .td file.
  for (Record *R : Insts) {
    Instructions[R] = std::make_unique<CodeGenInstruction>(R);
    if (Instructions[R]->isVariableLengthEncoding())
      HasVariableLengthEncodings = true;
  }
}

static const CodeGenInstruction *GetInstByName(
    const char *Name,
    const DenseMap<const Record *, std::unique_ptr<CodeGenInstruction>> &Insts,
    RecordKeeper &Records) {
  const Record *Rec = Records.getDef(Name);

  const auto I = Insts.find(Rec);
  if (!Rec || I == Insts.end())
    PrintFatalError(Twine("Could not find '") + Name + "' instruction!");
  return I->second.get();
}

static const char *FixedInstrs[] = {
#define HANDLE_TARGET_OPCODE(OPC) #OPC,
#include "llvm/Support/TargetOpcodes.def"
    nullptr};

unsigned CodeGenTarget::getNumFixedInstructions() {
  return std::size(FixedInstrs) - 1;
}

/// Return all of the instructions defined by the target, ordered by
/// their enum value.
void CodeGenTarget::ComputeInstrsByEnum() const {
  const auto &Insts = getInstructions();
  for (const char *const *p = FixedInstrs; *p; ++p) {
    const CodeGenInstruction *Instr = GetInstByName(*p, Insts, Records);
    assert(Instr && "Missing target independent instruction");
    assert(Instr->Namespace == "TargetOpcode" && "Bad namespace");
    InstrsByEnum.push_back(Instr);
  }
  unsigned EndOfPredefines = InstrsByEnum.size();
  assert(EndOfPredefines == getNumFixedInstructions() &&
         "Missing generic opcode");

  for (const auto &I : Insts) {
    const CodeGenInstruction *CGI = I.second.get();
    if (CGI->Namespace != "TargetOpcode") {
      InstrsByEnum.push_back(CGI);
      if (CGI->TheDef->getValueAsBit("isPseudo"))
        ++NumPseudoInstructions;
    }
  }

  assert(InstrsByEnum.size() == Insts.size() && "Missing predefined instr");

  // All of the instructions are now in random order based on the map iteration.
  llvm::sort(
      InstrsByEnum.begin() + EndOfPredefines, InstrsByEnum.end(),
      [](const CodeGenInstruction *Rec1, const CodeGenInstruction *Rec2) {
        const auto &D1 = *Rec1->TheDef;
        const auto &D2 = *Rec2->TheDef;
        return std::tuple(!D1.getValueAsBit("isPseudo"), D1.getName()) <
               std::tuple(!D2.getValueAsBit("isPseudo"), D2.getName());
      });

  // Assign an enum value to each instruction according to the sorted order.
  unsigned Num = 0;
  for (const CodeGenInstruction *Inst : InstrsByEnum)
    Inst->EnumVal = Num++;
}

/// isLittleEndianEncoding - Return whether this target encodes its instruction
/// in little-endian format, i.e. bits laid out in the order [0..n]
///
bool CodeGenTarget::isLittleEndianEncoding() const {
  return getInstructionSet()->getValueAsBit("isLittleEndianEncoding");
}

/// reverseBitsForLittleEndianEncoding - For little-endian instruction bit
/// encodings, reverse the bit order of all instructions.
void CodeGenTarget::reverseBitsForLittleEndianEncoding() {
  if (!isLittleEndianEncoding())
    return;

  std::vector<Record *> Insts =
      Records.getAllDerivedDefinitions("InstructionEncoding");
  for (Record *R : Insts) {
    if (R->getValueAsString("Namespace") == "TargetOpcode" ||
        R->getValueAsBit("isPseudo"))
      continue;

    BitsInit *BI = R->getValueAsBitsInit("Inst");

    unsigned numBits = BI->getNumBits();

    SmallVector<Init *, 16> NewBits(numBits);

    for (unsigned bit = 0, end = numBits / 2; bit != end; ++bit) {
      unsigned bitSwapIdx = numBits - bit - 1;
      Init *OrigBit = BI->getBit(bit);
      Init *BitSwap = BI->getBit(bitSwapIdx);
      NewBits[bit] = BitSwap;
      NewBits[bitSwapIdx] = OrigBit;
    }
    if (numBits % 2) {
      unsigned middle = (numBits + 1) / 2;
      NewBits[middle] = BI->getBit(middle);
    }

    BitsInit *NewBI = BitsInit::get(Records, NewBits);

    // Update the bits in reversed order so that emitInstrOpBits will get the
    // correct endianness.
    R->getValue("Inst")->setValue(NewBI);
  }
}

/// guessInstructionProperties - Return true if it's OK to guess instruction
/// properties instead of raising an error.
///
/// This is configurable as a temporary migration aid. It will eventually be
/// permanently false.
bool CodeGenTarget::guessInstructionProperties() const {
  return getInstructionSet()->getValueAsBit("guessInstructionProperties");
}

//===----------------------------------------------------------------------===//
// ComplexPattern implementation
//
ComplexPattern::ComplexPattern(Record *R) {
  Ty = R->getValueAsDef("Ty");
  NumOperands = R->getValueAsInt("NumOperands");
  SelectFunc = std::string(R->getValueAsString("SelectFunc"));
  RootNodes = R->getValueAsListOfDefs("RootNodes");

  // FIXME: This is a hack to statically increase the priority of patterns which
  // maps a sub-dag to a complex pattern. e.g. favors LEA over ADD. To get best
  // possible pattern match we'll need to dynamically calculate the complexity
  // of all patterns a dag can potentially map to.
  int64_t RawComplexity = R->getValueAsInt("Complexity");
  if (RawComplexity == -1)
    Complexity = NumOperands * 3;
  else
    Complexity = RawComplexity;

  // FIXME: Why is this different from parseSDPatternOperatorProperties?
  // Parse the properties.
  Properties = 0;
  std::vector<Record *> PropList = R->getValueAsListOfDefs("Properties");
  for (unsigned i = 0, e = PropList.size(); i != e; ++i)
    if (PropList[i]->getName() == "SDNPHasChain") {
      Properties |= 1 << SDNPHasChain;
    } else if (PropList[i]->getName() == "SDNPOptInGlue") {
      Properties |= 1 << SDNPOptInGlue;
    } else if (PropList[i]->getName() == "SDNPMayStore") {
      Properties |= 1 << SDNPMayStore;
    } else if (PropList[i]->getName() == "SDNPMayLoad") {
      Properties |= 1 << SDNPMayLoad;
    } else if (PropList[i]->getName() == "SDNPSideEffect") {
      Properties |= 1 << SDNPSideEffect;
    } else if (PropList[i]->getName() == "SDNPMemOperand") {
      Properties |= 1 << SDNPMemOperand;
    } else if (PropList[i]->getName() == "SDNPVariadic") {
      Properties |= 1 << SDNPVariadic;
    } else if (PropList[i]->getName() == "SDNPWantRoot") {
      Properties |= 1 << SDNPWantRoot;
    } else if (PropList[i]->getName() == "SDNPWantParent") {
      Properties |= 1 << SDNPWantParent;
    } else {
      PrintFatalError(R->getLoc(), "Unsupported SD Node property '" +
                                       PropList[i]->getName() +
                                       "' on ComplexPattern '" + R->getName() +
                                       "'!");
    }
}
