//===-- SPIRVInstPrinter.cpp - Output SPIR-V MCInsts as ASM -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints a SPIR-V MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "SPIRVInstPrinter.h"
#include "SPIRV.h"
#include "SPIRVBaseInfo.h"
#include "SPIRVInstrInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;
using namespace llvm::SPIRV;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#include "SPIRVGenAsmWriter.inc"

void SPIRVInstPrinter::printRemainingVariableOps(const MCInst *MI,
                                                 unsigned StartIndex,
                                                 raw_ostream &O,
                                                 bool SkipFirstSpace,
                                                 bool SkipImmediates) {
  const unsigned NumOps = MI->getNumOperands();
  for (unsigned i = StartIndex; i < NumOps; ++i) {
    if (!SkipImmediates || !MI->getOperand(i).isImm()) {
      if (!SkipFirstSpace || i != StartIndex)
        O << ' ';
      printOperand(MI, i, O);
    }
  }
}

void SPIRVInstPrinter::printOpConstantVarOps(const MCInst *MI,
                                             unsigned StartIndex,
                                             raw_ostream &O) {
  unsigned IsBitwidth16 = MI->getFlags() & SPIRV::ASM_PRINTER_WIDTH16;
  const unsigned NumVarOps = MI->getNumOperands() - StartIndex;

  assert((NumVarOps == 1 || NumVarOps == 2) &&
         "Unsupported number of bits for literal variable");

  O << ' ';

  uint64_t Imm = MI->getOperand(StartIndex).getImm();

  // Handle 64 bit literals.
  if (NumVarOps == 2) {
    Imm |= (MI->getOperand(StartIndex + 1).getImm() << 32);
  }

  // Format and print float values.
  if (MI->getOpcode() == SPIRV::OpConstantF && IsBitwidth16 == 0) {
    APFloat FP = NumVarOps == 1 ? APFloat(APInt(32, Imm).bitsToFloat())
                                : APFloat(APInt(64, Imm).bitsToDouble());

    // Print infinity and NaN as hex floats.
    // TODO: Make sure subnormal numbers are handled correctly as they may also
    // require hex float notation.
    if (FP.isInfinity()) {
      if (FP.isNegative())
        O << '-';
      O << "0x1p+128";
      return;
    }
    if (FP.isNaN()) {
      O << "0x1.8p+128";
      return;
    }

    // Format val as a decimal floating point or scientific notation (whichever
    // is shorter), with enough digits of precision to produce the exact value.
    O << format("%.*g", std::numeric_limits<double>::max_digits10,
                FP.convertToDouble());

    return;
  }

  // Print integer values directly.
  O << Imm;
}

void SPIRVInstPrinter::recordOpExtInstImport(const MCInst *MI) {
  Register Reg = MI->getOperand(0).getReg();
  auto Name = getSPIRVStringOperand(*MI, 1);
  auto Set = getExtInstSetFromString(Name);
  ExtInstSetIDs.insert({Reg, Set});
}

void SPIRVInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot, const MCSubtargetInfo &STI,
                                 raw_ostream &OS) {
  const unsigned OpCode = MI->getOpcode();
  printInstruction(MI, Address, OS);

  if (OpCode == SPIRV::OpDecorate) {
    printOpDecorate(MI, OS);
  } else if (OpCode == SPIRV::OpExtInstImport) {
    recordOpExtInstImport(MI);
  } else if (OpCode == SPIRV::OpExtInst) {
    printOpExtInst(MI, OS);
  } else {
    // Print any extra operands for variadic instructions.
    const MCInstrDesc &MCDesc = MII.get(OpCode);
    if (MCDesc.isVariadic()) {
      const unsigned NumFixedOps = MCDesc.getNumOperands();
      const unsigned LastFixedIndex = NumFixedOps - 1;
      const int FirstVariableIndex = NumFixedOps;
      if (NumFixedOps > 0 && MCDesc.operands()[LastFixedIndex].OperandType ==
                                 MCOI::OPERAND_UNKNOWN) {
        // For instructions where a custom type (not reg or immediate) comes as
        // the last operand before the variable_ops. This is usually a StringImm
        // operand, but there are a few other cases.
        switch (OpCode) {
        case SPIRV::OpTypeImage:
          OS << ' ';
          printSymbolicOperand<OperandCategory::AccessQualifierOperand>(
              MI, FirstVariableIndex, OS);
          break;
        case SPIRV::OpVariable:
          OS << ' ';
          printOperand(MI, FirstVariableIndex, OS);
          break;
        case SPIRV::OpEntryPoint: {
          // Print the interface ID operands, skipping the name's string
          // literal.
          printRemainingVariableOps(MI, NumFixedOps, OS, false, true);
          break;
        }
        case SPIRV::OpExecutionMode:
        case SPIRV::OpExecutionModeId:
        case SPIRV::OpLoopMerge: {
          // Print any literals after the OPERAND_UNKNOWN argument normally.
          printRemainingVariableOps(MI, NumFixedOps, OS);
          break;
        }
        default:
          break; // printStringImm has already been handled.
        }
      } else {
        // For instructions with no fixed ops or a reg/immediate as the final
        // fixed operand, we can usually print the rest with "printOperand", but
        // check for a few cases with custom types first.
        switch (OpCode) {
        case SPIRV::OpLoad:
        case SPIRV::OpStore:
          OS << ' ';
          printSymbolicOperand<OperandCategory::MemoryOperandOperand>(
              MI, FirstVariableIndex, OS);
          printRemainingVariableOps(MI, FirstVariableIndex + 1, OS);
          break;
        case SPIRV::OpImageSampleImplicitLod:
        case SPIRV::OpImageSampleDrefImplicitLod:
        case SPIRV::OpImageSampleProjImplicitLod:
        case SPIRV::OpImageSampleProjDrefImplicitLod:
        case SPIRV::OpImageFetch:
        case SPIRV::OpImageGather:
        case SPIRV::OpImageDrefGather:
        case SPIRV::OpImageRead:
        case SPIRV::OpImageWrite:
        case SPIRV::OpImageSparseSampleImplicitLod:
        case SPIRV::OpImageSparseSampleDrefImplicitLod:
        case SPIRV::OpImageSparseSampleProjImplicitLod:
        case SPIRV::OpImageSparseSampleProjDrefImplicitLod:
        case SPIRV::OpImageSparseFetch:
        case SPIRV::OpImageSparseGather:
        case SPIRV::OpImageSparseDrefGather:
        case SPIRV::OpImageSparseRead:
        case SPIRV::OpImageSampleFootprintNV:
          OS << ' ';
          printSymbolicOperand<OperandCategory::ImageOperandOperand>(
              MI, FirstVariableIndex, OS);
          printRemainingVariableOps(MI, NumFixedOps + 1, OS);
          break;
        case SPIRV::OpCopyMemory:
        case SPIRV::OpCopyMemorySized: {
          const unsigned NumOps = MI->getNumOperands();
          for (unsigned i = NumFixedOps; i < NumOps; ++i) {
            OS << ' ';
            printSymbolicOperand<OperandCategory::MemoryOperandOperand>(MI, i,
                                                                        OS);
            if (MI->getOperand(i).getImm() & MemoryOperand::Aligned) {
              assert(i + 1 < NumOps && "Missing alignment operand");
              OS << ' ';
              printOperand(MI, i + 1, OS);
              i += 1;
            }
          }
          break;
        }
        case SPIRV::OpConstantI:
        case SPIRV::OpConstantF:
          // The last fixed operand along with any variadic operands that follow
          // are part of the variable value.
          printOpConstantVarOps(MI, NumFixedOps - 1, OS);
          break;
        default:
          printRemainingVariableOps(MI, NumFixedOps, OS);
          break;
        }
      }
    }
  }

  printAnnotation(OS, Annot);
}

void SPIRVInstPrinter::printOpExtInst(const MCInst *MI, raw_ostream &O) {
  // The fixed operands have already been printed, so just need to decide what
  // type of ExtInst operands to print based on the instruction set and number.
  const MCInstrDesc &MCDesc = MII.get(MI->getOpcode());
  unsigned NumFixedOps = MCDesc.getNumOperands();
  const auto NumOps = MI->getNumOperands();
  if (NumOps == NumFixedOps)
    return;

  O << ' ';

  // TODO: implement special printing for OpenCLExtInst::vstor*.
  printRemainingVariableOps(MI, NumFixedOps, O, true);
}

void SPIRVInstPrinter::printOpDecorate(const MCInst *MI, raw_ostream &O) {
  // The fixed operands have already been printed, so just need to decide what
  // type of decoration operands to print based on the Decoration type.
  const MCInstrDesc &MCDesc = MII.get(MI->getOpcode());
  unsigned NumFixedOps = MCDesc.getNumOperands();

  if (NumFixedOps != MI->getNumOperands()) {
    auto DecOp = MI->getOperand(NumFixedOps - 1);
    auto Dec = static_cast<Decoration::Decoration>(DecOp.getImm());

    O << ' ';

    switch (Dec) {
    case Decoration::BuiltIn:
      printSymbolicOperand<OperandCategory::BuiltInOperand>(MI, NumFixedOps, O);
      break;
    case Decoration::UniformId:
      printSymbolicOperand<OperandCategory::ScopeOperand>(MI, NumFixedOps, O);
      break;
    case Decoration::FuncParamAttr:
      printSymbolicOperand<OperandCategory::FunctionParameterAttributeOperand>(
          MI, NumFixedOps, O);
      break;
    case Decoration::FPRoundingMode:
      printSymbolicOperand<OperandCategory::FPRoundingModeOperand>(
          MI, NumFixedOps, O);
      break;
    case Decoration::FPFastMathMode:
      printSymbolicOperand<OperandCategory::FPFastMathModeOperand>(
          MI, NumFixedOps, O);
      break;
    case Decoration::LinkageAttributes:
    case Decoration::UserSemantic:
      printStringImm(MI, NumFixedOps, O);
      break;
    case Decoration::HostAccessINTEL:
      printOperand(MI, NumFixedOps, O);
      if (NumFixedOps + 1 < MI->getNumOperands()) {
        O << ' ';
        printStringImm(MI, NumFixedOps + 1, O);
      }
      break;
    default:
      printRemainingVariableOps(MI, NumFixedOps, O, true);
      break;
    }
  }
}

static void printExpr(const MCExpr *Expr, raw_ostream &O) {
#ifndef NDEBUG
  const MCSymbolRefExpr *SRE;

  if (const MCBinaryExpr *BE = dyn_cast<MCBinaryExpr>(Expr))
    SRE = cast<MCSymbolRefExpr>(BE->getLHS());
  else
    SRE = cast<MCSymbolRefExpr>(Expr);

  MCSymbolRefExpr::VariantKind Kind = SRE->getKind();

  assert(Kind == MCSymbolRefExpr::VK_None);
#endif
  O << *Expr;
}

void SPIRVInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    raw_ostream &O, const char *Modifier) {
  assert((Modifier == 0 || Modifier[0] == 0) && "No modifiers supported");
  if (OpNo < MI->getNumOperands()) {
    const MCOperand &Op = MI->getOperand(OpNo);
    if (Op.isReg())
      O << '%' << (Register::virtReg2Index(Op.getReg()) + 1);
    else if (Op.isImm())
      O << formatImm((int64_t)Op.getImm());
    else if (Op.isDFPImm())
      O << formatImm((double)Op.getDFPImm());
    else if (Op.isExpr())
      printExpr(Op.getExpr(), O);
    else
      llvm_unreachable("Unexpected operand type");
  }
}

void SPIRVInstPrinter::printStringImm(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O) {
  const unsigned NumOps = MI->getNumOperands();
  unsigned StrStartIndex = OpNo;
  while (StrStartIndex < NumOps) {
    if (MI->getOperand(StrStartIndex).isReg())
      break;

    std::string Str = getSPIRVStringOperand(*MI, StrStartIndex);
    if (StrStartIndex != OpNo)
      O << ' '; // Add a space if we're starting a new string/argument.
    O << '"';
    for (char c : Str) {
      // Escape ", \n characters (might break for complex UTF-8).
      if (c == '\n') {
        O.write("\\n", 2);
      } else {
        if (c == '"')
          O.write('\\');
        O.write(c);
      }
    }
    O << '"';

    unsigned numOpsInString = (Str.size() / 4) + 1;
    StrStartIndex += numOpsInString;

    // Check for final Op of "OpDecorate %x %stringImm %linkageAttribute".
    if (MI->getOpcode() == SPIRV::OpDecorate &&
        MI->getOperand(1).getImm() ==
            static_cast<unsigned>(Decoration::LinkageAttributes)) {
      O << ' ';
      printSymbolicOperand<OperandCategory::LinkageTypeOperand>(
          MI, StrStartIndex, O);
      break;
    }
  }
}

void SPIRVInstPrinter::printExtension(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O) {
  auto SetReg = MI->getOperand(2).getReg();
  auto Set = ExtInstSetIDs[SetReg];
  auto Op = MI->getOperand(OpNo).getImm();
  O << getExtInstName(Set, Op);
}

template <OperandCategory::OperandCategory category>
void SPIRVInstPrinter::printSymbolicOperand(const MCInst *MI, unsigned OpNo,
                                            raw_ostream &O) {
  if (OpNo < MI->getNumOperands()) {
    O << getSymbolicOperandMnemonic(category, MI->getOperand(OpNo).getImm());
  }
}
