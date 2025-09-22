//===- CodeEmitterGen.cpp - Code Emitter Generator ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CodeEmitterGen uses the descriptions of instructions and their fields to
// construct an automated code emitter: a function called
// getBinaryCodeForInstr() that, given a MCInst, returns the value of the
// instruction - either as an uint64_t or as an APInt, depending on the
// maximum bit width of all Inst definitions.
//
// In addition, it generates another function called getOperandBitOffset()
// that, given a MCInst and an operand index, returns the minimum of indices of
// all bits that carry some portion of the respective operand. When the target's
// encodeInstruction() stores the instruction in a little-endian byte order, the
// returned value is the offset of the start of the operand in the encoded
// instruction. Other targets might need to adjust the returned value according
// to their encodeInstruction() implementation.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenHwModes.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "Common/InfoByHwMode.h"
#include "Common/VarLenCodeEmitterGen.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

class CodeEmitterGen {
  RecordKeeper &Records;

public:
  CodeEmitterGen(RecordKeeper &R) : Records(R) {}

  void run(raw_ostream &o);

private:
  int getVariableBit(const std::string &VarName, BitsInit *BI, int bit);
  std::pair<std::string, std::string>
  getInstructionCases(Record *R, CodeGenTarget &Target);
  void addInstructionCasesForEncoding(Record *R, Record *EncodingDef,
                                      CodeGenTarget &Target, std::string &Case,
                                      std::string &BitOffsetCase);
  bool addCodeToMergeInOperand(Record *R, BitsInit *BI,
                               const std::string &VarName, std::string &Case,
                               std::string &BitOffsetCase,
                               CodeGenTarget &Target);

  void emitInstructionBaseValues(
      raw_ostream &o, ArrayRef<const CodeGenInstruction *> NumberedInstructions,
      CodeGenTarget &Target, unsigned HwMode = DefaultMode);
  void
  emitCaseMap(raw_ostream &o,
              const std::map<std::string, std::vector<std::string>> &CaseMap);
  unsigned BitWidth = 0u;
  bool UseAPInt = false;
};

// If the VarBitInit at position 'bit' matches the specified variable then
// return the variable bit position.  Otherwise return -1.
int CodeEmitterGen::getVariableBit(const std::string &VarName, BitsInit *BI,
                                   int bit) {
  if (VarBitInit *VBI = dyn_cast<VarBitInit>(BI->getBit(bit))) {
    if (VarInit *VI = dyn_cast<VarInit>(VBI->getBitVar()))
      if (VI->getName() == VarName)
        return VBI->getBitNum();
  } else if (VarInit *VI = dyn_cast<VarInit>(BI->getBit(bit))) {
    if (VI->getName() == VarName)
      return 0;
  }

  return -1;
}

// Returns true if it succeeds, false if an error.
bool CodeEmitterGen::addCodeToMergeInOperand(Record *R, BitsInit *BI,
                                             const std::string &VarName,
                                             std::string &Case,
                                             std::string &BitOffsetCase,
                                             CodeGenTarget &Target) {
  CodeGenInstruction &CGI = Target.getInstruction(R);

  // Determine if VarName actually contributes to the Inst encoding.
  int bit = BI->getNumBits() - 1;

  // Scan for a bit that this contributed to.
  for (; bit >= 0;) {
    if (getVariableBit(VarName, BI, bit) != -1)
      break;

    --bit;
  }

  // If we found no bits, ignore this value, otherwise emit the call to get the
  // operand encoding.
  if (bit < 0)
    return true;

  // If the operand matches by name, reference according to that
  // operand number. Non-matching operands are assumed to be in
  // order.
  unsigned OpIdx;
  std::pair<unsigned, unsigned> SubOp;
  if (CGI.Operands.hasSubOperandAlias(VarName, SubOp)) {
    OpIdx = CGI.Operands[SubOp.first].MIOperandNo + SubOp.second;
  } else if (CGI.Operands.hasOperandNamed(VarName, OpIdx)) {
    // Get the machine operand number for the indicated operand.
    OpIdx = CGI.Operands[OpIdx].MIOperandNo;
  } else {
    PrintError(R, Twine("No operand named ") + VarName + " in record " +
                      R->getName());
    return false;
  }

  if (CGI.Operands.isFlatOperandNotEmitted(OpIdx)) {
    PrintError(R,
               "Operand " + VarName + " used but also marked as not emitted!");
    return false;
  }

  std::pair<unsigned, unsigned> SO = CGI.Operands.getSubOperandNumber(OpIdx);
  std::string &EncoderMethodName =
      CGI.Operands[SO.first].EncoderMethodNames[SO.second];

  if (UseAPInt)
    Case += "      op.clearAllBits();\n";

  Case += "      // op: " + VarName + "\n";

  // If the source operand has a custom encoder, use it.
  if (!EncoderMethodName.empty()) {
    if (UseAPInt) {
      Case += "      " + EncoderMethodName + "(MI, " + utostr(OpIdx);
      Case += ", op";
    } else {
      Case += "      op = " + EncoderMethodName + "(MI, " + utostr(OpIdx);
    }
    Case += ", Fixups, STI);\n";
  } else {
    if (UseAPInt) {
      Case +=
          "      getMachineOpValue(MI, MI.getOperand(" + utostr(OpIdx) + ")";
      Case += ", op, Fixups, STI";
    } else {
      Case += "      op = getMachineOpValue(MI, MI.getOperand(" +
              utostr(OpIdx) + ")";
      Case += ", Fixups, STI";
    }
    Case += ");\n";
  }

  // Precalculate the number of lits this variable contributes to in the
  // operand. If there is a single lit (consecutive range of bits) we can use a
  // destructive sequence on APInt that reduces memory allocations.
  int numOperandLits = 0;
  for (int tmpBit = bit; tmpBit >= 0;) {
    int varBit = getVariableBit(VarName, BI, tmpBit);

    // If this bit isn't from a variable, skip it.
    if (varBit == -1) {
      --tmpBit;
      continue;
    }

    // Figure out the consecutive range of bits covered by this operand, in
    // order to generate better encoding code.
    int beginVarBit = varBit;
    int N = 1;
    for (--tmpBit; tmpBit >= 0;) {
      varBit = getVariableBit(VarName, BI, tmpBit);
      if (varBit == -1 || varBit != (beginVarBit - N))
        break;
      ++N;
      --tmpBit;
    }
    ++numOperandLits;
  }

  unsigned BitOffset = -1;
  for (; bit >= 0;) {
    int varBit = getVariableBit(VarName, BI, bit);

    // If this bit isn't from a variable, skip it.
    if (varBit == -1) {
      --bit;
      continue;
    }

    // Figure out the consecutive range of bits covered by this operand, in
    // order to generate better encoding code.
    int beginInstBit = bit;
    int beginVarBit = varBit;
    int N = 1;
    for (--bit; bit >= 0;) {
      varBit = getVariableBit(VarName, BI, bit);
      if (varBit == -1 || varBit != (beginVarBit - N))
        break;
      ++N;
      --bit;
    }

    std::string maskStr;
    int opShift;

    unsigned loBit = beginVarBit - N + 1;
    unsigned hiBit = loBit + N;
    unsigned loInstBit = beginInstBit - N + 1;
    BitOffset = loInstBit;
    if (UseAPInt) {
      std::string extractStr;
      if (N >= 64) {
        extractStr = "op.extractBits(" + itostr(hiBit - loBit) + ", " +
                     itostr(loBit) + ")";
        Case += "      Value.insertBits(" + extractStr + ", " +
                itostr(loInstBit) + ");\n";
      } else {
        extractStr = "op.extractBitsAsZExtValue(" + itostr(hiBit - loBit) +
                     ", " + itostr(loBit) + ")";
        Case += "      Value.insertBits(" + extractStr + ", " +
                itostr(loInstBit) + ", " + itostr(hiBit - loBit) + ");\n";
      }
    } else {
      uint64_t opMask = ~(uint64_t)0 >> (64 - N);
      opShift = beginVarBit - N + 1;
      opMask <<= opShift;
      maskStr = "UINT64_C(" + utostr(opMask) + ")";
      opShift = beginInstBit - beginVarBit;

      if (numOperandLits == 1) {
        Case += "      op &= " + maskStr + ";\n";
        if (opShift > 0) {
          Case += "      op <<= " + itostr(opShift) + ";\n";
        } else if (opShift < 0) {
          Case += "      op >>= " + itostr(-opShift) + ";\n";
        }
        Case += "      Value |= op;\n";
      } else {
        if (opShift > 0) {
          Case += "      Value |= (op & " + maskStr + ") << " +
                  itostr(opShift) + ";\n";
        } else if (opShift < 0) {
          Case += "      Value |= (op & " + maskStr + ") >> " +
                  itostr(-opShift) + ";\n";
        } else {
          Case += "      Value |= (op & " + maskStr + ");\n";
        }
      }
    }
  }

  if (BitOffset != (unsigned)-1) {
    BitOffsetCase += "      case " + utostr(OpIdx) + ":\n";
    BitOffsetCase += "        // op: " + VarName + "\n";
    BitOffsetCase += "        return " + utostr(BitOffset) + ";\n";
  }

  return true;
}

std::pair<std::string, std::string>
CodeEmitterGen::getInstructionCases(Record *R, CodeGenTarget &Target) {
  std::string Case, BitOffsetCase;

  auto append = [&](const std::string &S) {
    Case += S;
    BitOffsetCase += S;
  };

  if (const RecordVal *RV = R->getValue("EncodingInfos")) {
    if (auto *DI = dyn_cast_or_null<DefInit>(RV->getValue())) {
      const CodeGenHwModes &HWM = Target.getHwModes();
      EncodingInfoByHwMode EBM(DI->getDef(), HWM);

      // Invoke the interface to obtain the HwMode ID controlling the
      // EncodingInfo for the current subtarget. This interface will
      // mask off irrelevant HwMode IDs.
      append("      unsigned HwMode = "
             "STI.getHwMode(MCSubtargetInfo::HwMode_EncodingInfo);\n");
      Case += "      switch (HwMode) {\n";
      Case += "      default: llvm_unreachable(\"Unknown hardware mode!\"); "
              "break;\n";
      for (auto &[ModeId, Encoding] : EBM) {
        if (ModeId == DefaultMode) {
          Case +=
              "      case " + itostr(DefaultMode) + ": InstBitsByHw = InstBits";
        } else {
          Case += "      case " + itostr(ModeId) +
                  ": InstBitsByHw = InstBits_" +
                  std::string(HWM.getMode(ModeId).Name);
        }
        Case += "; break;\n";
      }
      Case += "      };\n";

      // We need to remodify the 'Inst' value from the table we found above.
      if (UseAPInt) {
        int NumWords = APInt::getNumWords(BitWidth);
        Case += "      Inst = APInt(" + itostr(BitWidth);
        Case += ", ArrayRef(InstBitsByHw + opcode * " + itostr(NumWords) +
                ", " + itostr(NumWords);
        Case += "));\n";
        Case += "      Value = Inst;\n";
      } else {
        Case += "      Value = InstBitsByHw[opcode];\n";
      }

      append("      switch (HwMode) {\n");
      append("      default: llvm_unreachable(\"Unhandled HwMode\");\n");
      for (auto &[ModeId, Encoding] : EBM) {
        append("      case " + itostr(ModeId) + ": {\n");
        addInstructionCasesForEncoding(R, Encoding, Target, Case,
                                       BitOffsetCase);
        append("      break;\n");
        append("      }\n");
      }
      append("      }\n");
      return std::pair(std::move(Case), std::move(BitOffsetCase));
    }
  }
  addInstructionCasesForEncoding(R, R, Target, Case, BitOffsetCase);
  return std::pair(std::move(Case), std::move(BitOffsetCase));
}

void CodeEmitterGen::addInstructionCasesForEncoding(
    Record *R, Record *EncodingDef, CodeGenTarget &Target, std::string &Case,
    std::string &BitOffsetCase) {
  BitsInit *BI = EncodingDef->getValueAsBitsInit("Inst");

  // Loop over all of the fields in the instruction, determining which are the
  // operands to the instruction.
  bool Success = true;
  size_t OrigBitOffsetCaseSize = BitOffsetCase.size();
  BitOffsetCase += "      switch (OpNum) {\n";
  size_t BitOffsetCaseSizeBeforeLoop = BitOffsetCase.size();
  for (const RecordVal &RV : EncodingDef->getValues()) {
    // Ignore fixed fields in the record, we're looking for values like:
    //    bits<5> RST = { ?, ?, ?, ?, ? };
    if (RV.isNonconcreteOK() || RV.getValue()->isComplete())
      continue;

    Success &= addCodeToMergeInOperand(R, BI, std::string(RV.getName()), Case,
                                       BitOffsetCase, Target);
  }
  // Avoid empty switches.
  if (BitOffsetCase.size() == BitOffsetCaseSizeBeforeLoop)
    BitOffsetCase.resize(OrigBitOffsetCaseSize);
  else
    BitOffsetCase += "      }\n";

  if (!Success) {
    // Dump the record, so we can see what's going on...
    std::string E;
    raw_string_ostream S(E);
    S << "Dumping record for previous error:\n";
    S << *R;
    PrintNote(E);
  }

  StringRef PostEmitter = R->getValueAsString("PostEncoderMethod");
  if (!PostEmitter.empty()) {
    Case += "      Value = ";
    Case += PostEmitter;
    Case += "(MI, Value";
    Case += ", STI";
    Case += ");\n";
  }
}

static void emitInstBits(raw_ostream &OS, const APInt &Bits) {
  for (unsigned I = 0; I < Bits.getNumWords(); ++I)
    OS << ((I > 0) ? ", " : "") << "UINT64_C(" << utostr(Bits.getRawData()[I])
       << ")";
}

void CodeEmitterGen::emitInstructionBaseValues(
    raw_ostream &o, ArrayRef<const CodeGenInstruction *> NumberedInstructions,
    CodeGenTarget &Target, unsigned HwMode) {
  const CodeGenHwModes &HWM = Target.getHwModes();
  if (HwMode == DefaultMode)
    o << "  static const uint64_t InstBits[] = {\n";
  else
    o << "  static const uint64_t InstBits_"
      << HWM.getModeName(HwMode, /*IncludeDefault=*/true) << "[] = {\n";

  for (const CodeGenInstruction *CGI : NumberedInstructions) {
    Record *R = CGI->TheDef;

    if (R->getValueAsString("Namespace") == "TargetOpcode" ||
        R->getValueAsBit("isPseudo")) {
      o << "    ";
      emitInstBits(o, APInt(BitWidth, 0));
      o << ",\n";
      continue;
    }

    Record *EncodingDef = R;
    if (const RecordVal *RV = R->getValue("EncodingInfos")) {
      if (auto *DI = dyn_cast_or_null<DefInit>(RV->getValue())) {
        EncodingInfoByHwMode EBM(DI->getDef(), HWM);
        if (EBM.hasMode(HwMode)) {
          EncodingDef = EBM.get(HwMode);
        } else {
          // If the HwMode does not match, then Encoding '0'
          // should be generated.
          APInt Value(BitWidth, 0);
          o << "    ";
          emitInstBits(o, Value);
          o << "," << '\t' << "// " << R->getName() << "\n";
          continue;
        }
      }
    }
    BitsInit *BI = EncodingDef->getValueAsBitsInit("Inst");

    // Start by filling in fixed values.
    APInt Value(BitWidth, 0);
    for (unsigned i = 0, e = BI->getNumBits(); i != e; ++i) {
      if (auto *B = dyn_cast<BitInit>(BI->getBit(i)); B && B->getValue())
        Value.setBit(i);
    }
    o << "    ";
    emitInstBits(o, Value);
    o << "," << '\t' << "// " << R->getName() << "\n";
  }
  o << "    UINT64_C(0)\n  };\n";
}

void CodeEmitterGen::emitCaseMap(
    raw_ostream &o,
    const std::map<std::string, std::vector<std::string>> &CaseMap) {
  std::map<std::string, std::vector<std::string>>::const_iterator IE, EE;
  for (IE = CaseMap.begin(), EE = CaseMap.end(); IE != EE; ++IE) {
    const std::string &Case = IE->first;
    const std::vector<std::string> &InstList = IE->second;

    for (int i = 0, N = InstList.size(); i < N; i++) {
      if (i)
        o << "\n";
      o << "    case " << InstList[i] << ":";
    }
    o << " {\n";
    o << Case;
    o << "      break;\n"
      << "    }\n";
  }
}

void CodeEmitterGen::run(raw_ostream &o) {
  emitSourceFileHeader("Machine Code Emitter", o);

  CodeGenTarget Target(Records);
  std::vector<Record *> Insts = Records.getAllDerivedDefinitions("Instruction");

  // For little-endian instruction bit encodings, reverse the bit order
  Target.reverseBitsForLittleEndianEncoding();

  ArrayRef<const CodeGenInstruction *> NumberedInstructions =
      Target.getInstructionsByEnumValue();

  if (Target.hasVariableLengthEncodings()) {
    emitVarLenCodeEmitter(Records, o);
  } else {
    const CodeGenHwModes &HWM = Target.getHwModes();
    // The set of HwModes used by instruction encodings.
    std::set<unsigned> HwModes;
    BitWidth = 0;
    for (const CodeGenInstruction *CGI : NumberedInstructions) {
      Record *R = CGI->TheDef;
      if (R->getValueAsString("Namespace") == "TargetOpcode" ||
          R->getValueAsBit("isPseudo"))
        continue;

      if (const RecordVal *RV = R->getValue("EncodingInfos")) {
        if (DefInit *DI = dyn_cast_or_null<DefInit>(RV->getValue())) {
          EncodingInfoByHwMode EBM(DI->getDef(), HWM);
          for (auto &KV : EBM) {
            BitsInit *BI = KV.second->getValueAsBitsInit("Inst");
            BitWidth = std::max(BitWidth, BI->getNumBits());
            HwModes.insert(KV.first);
          }
          continue;
        }
      }
      BitsInit *BI = R->getValueAsBitsInit("Inst");
      BitWidth = std::max(BitWidth, BI->getNumBits());
    }
    UseAPInt = BitWidth > 64;

    // Emit function declaration
    if (UseAPInt) {
      o << "void " << Target.getName()
        << "MCCodeEmitter::getBinaryCodeForInstr(const MCInst &MI,\n"
        << "    SmallVectorImpl<MCFixup> &Fixups,\n"
        << "    APInt &Inst,\n"
        << "    APInt &Scratch,\n"
        << "    const MCSubtargetInfo &STI) const {\n";
    } else {
      o << "uint64_t " << Target.getName();
      o << "MCCodeEmitter::getBinaryCodeForInstr(const MCInst &MI,\n"
        << "    SmallVectorImpl<MCFixup> &Fixups,\n"
        << "    const MCSubtargetInfo &STI) const {\n";
    }

    // Emit instruction base values
    emitInstructionBaseValues(o, NumberedInstructions, Target, DefaultMode);
    if (!HwModes.empty()) {
      // Emit table for instrs whose encodings are controlled by HwModes.
      for (unsigned HwMode : HwModes) {
        if (HwMode == DefaultMode)
          continue;
        emitInstructionBaseValues(o, NumberedInstructions, Target, HwMode);
      }

      // This pointer will be assigned to the HwMode table later.
      o << "  const uint64_t *InstBitsByHw;\n";
    }

    // Map to accumulate all the cases.
    std::map<std::string, std::vector<std::string>> CaseMap;
    std::map<std::string, std::vector<std::string>> BitOffsetCaseMap;

    // Construct all cases statement for each opcode
    for (Record *R : Insts) {
      if (R->getValueAsString("Namespace") == "TargetOpcode" ||
          R->getValueAsBit("isPseudo"))
        continue;
      std::string InstName =
          (R->getValueAsString("Namespace") + "::" + R->getName()).str();
      std::string Case, BitOffsetCase;
      std::tie(Case, BitOffsetCase) = getInstructionCases(R, Target);

      CaseMap[Case].push_back(InstName);
      BitOffsetCaseMap[BitOffsetCase].push_back(std::move(InstName));
    }

    // Emit initial function code
    if (UseAPInt) {
      int NumWords = APInt::getNumWords(BitWidth);
      o << "  const unsigned opcode = MI.getOpcode();\n"
        << "  if (Scratch.getBitWidth() != " << BitWidth << ")\n"
        << "    Scratch = Scratch.zext(" << BitWidth << ");\n"
        << "  Inst = APInt(" << BitWidth << ", ArrayRef(InstBits + opcode * "
        << NumWords << ", " << NumWords << "));\n"
        << "  APInt &Value = Inst;\n"
        << "  APInt &op = Scratch;\n"
        << "  switch (opcode) {\n";
    } else {
      o << "  const unsigned opcode = MI.getOpcode();\n"
        << "  uint64_t Value = InstBits[opcode];\n"
        << "  uint64_t op = 0;\n"
        << "  (void)op;  // suppress warning\n"
        << "  switch (opcode) {\n";
    }

    // Emit each case statement
    emitCaseMap(o, CaseMap);

    // Default case: unhandled opcode
    o << "  default:\n"
      << "    std::string msg;\n"
      << "    raw_string_ostream Msg(msg);\n"
      << "    Msg << \"Not supported instr: \" << MI;\n"
      << "    report_fatal_error(Msg.str().c_str());\n"
      << "  }\n";
    if (UseAPInt)
      o << "  Inst = Value;\n";
    else
      o << "  return Value;\n";
    o << "}\n\n";

    o << "#ifdef GET_OPERAND_BIT_OFFSET\n"
      << "#undef GET_OPERAND_BIT_OFFSET\n\n"
      << "uint32_t " << Target.getName()
      << "MCCodeEmitter::getOperandBitOffset(const MCInst &MI,\n"
      << "    unsigned OpNum,\n"
      << "    const MCSubtargetInfo &STI) const {\n"
      << "  switch (MI.getOpcode()) {\n";
    emitCaseMap(o, BitOffsetCaseMap);
    o << "  }\n"
      << "  std::string msg;\n"
      << "  raw_string_ostream Msg(msg);\n"
      << "  Msg << \"Not supported instr[opcode]: \" << MI << \"[\" << OpNum "
         "<< \"]\";\n"
      << "  report_fatal_error(Msg.str().c_str());\n"
      << "}\n\n"
      << "#endif // GET_OPERAND_BIT_OFFSET\n\n";
  }
}

} // end anonymous namespace

static TableGen::Emitter::OptClass<CodeEmitterGen>
    X("gen-emitter", "Generate machine code emitter");
