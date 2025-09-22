//===- SelectionDAGBuilder.cpp - Selection-DAG building -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements routines for translating from LLVM IR into SelectionDAG IR.
//
//===----------------------------------------------------------------------===//

#include "SelectionDAGBuilder.h"
#include "SDNodeDbgValue.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/AssignmentTrackingAnalysis.h"
#include "llvm/CodeGen/CodeGenCommonISel.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/SwiftErrorValueTracking.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cstddef>
#include <deque>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>

using namespace llvm;
using namespace PatternMatch;
using namespace SwitchCG;

#define DEBUG_TYPE "isel"

/// LimitFloatPrecision - Generate low-precision inline sequences for
/// some float libcalls (6, 8 or 12 bits).
static unsigned LimitFloatPrecision;

static cl::opt<bool>
    InsertAssertAlign("insert-assert-align", cl::init(true),
                      cl::desc("Insert the experimental `assertalign` node."),
                      cl::ReallyHidden);

static cl::opt<unsigned, true>
    LimitFPPrecision("limit-float-precision",
                     cl::desc("Generate low-precision inline sequences "
                              "for some float libcalls"),
                     cl::location(LimitFloatPrecision), cl::Hidden,
                     cl::init(0));

static cl::opt<unsigned> SwitchPeelThreshold(
    "switch-peel-threshold", cl::Hidden, cl::init(66),
    cl::desc("Set the case probability threshold for peeling the case from a "
             "switch statement. A value greater than 100 will void this "
             "optimization"));

// Limit the width of DAG chains. This is important in general to prevent
// DAG-based analysis from blowing up. For example, alias analysis and
// load clustering may not complete in reasonable time. It is difficult to
// recognize and avoid this situation within each individual analysis, and
// future analyses are likely to have the same behavior. Limiting DAG width is
// the safe approach and will be especially important with global DAGs.
//
// MaxParallelChains default is arbitrarily high to avoid affecting
// optimization, but could be lowered to improve compile time. Any ld-ld-st-st
// sequence over this should have been converted to llvm.memcpy by the
// frontend. It is easy to induce this behavior with .ll code such as:
// %buffer = alloca [4096 x i8]
// %data = load [4096 x i8]* %argPtr
// store [4096 x i8] %data, [4096 x i8]* %buffer
static const unsigned MaxParallelChains = 64;

static SDValue getCopyFromPartsVector(SelectionDAG &DAG, const SDLoc &DL,
                                      const SDValue *Parts, unsigned NumParts,
                                      MVT PartVT, EVT ValueVT, const Value *V,
                                      SDValue InChain,
                                      std::optional<CallingConv::ID> CC);

/// getCopyFromParts - Create a value that contains the specified legal parts
/// combined into the value they represent.  If the parts combine to a type
/// larger than ValueVT then AssertOp can be used to specify whether the extra
/// bits are known to be zero (ISD::AssertZext) or sign extended from ValueVT
/// (ISD::AssertSext).
static SDValue
getCopyFromParts(SelectionDAG &DAG, const SDLoc &DL, const SDValue *Parts,
                 unsigned NumParts, MVT PartVT, EVT ValueVT, const Value *V,
                 SDValue InChain,
                 std::optional<CallingConv::ID> CC = std::nullopt,
                 std::optional<ISD::NodeType> AssertOp = std::nullopt) {
  // Let the target assemble the parts if it wants to
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (SDValue Val = TLI.joinRegisterPartsIntoValue(DAG, DL, Parts, NumParts,
                                                   PartVT, ValueVT, CC))
    return Val;

  if (ValueVT.isVector())
    return getCopyFromPartsVector(DAG, DL, Parts, NumParts, PartVT, ValueVT, V,
                                  InChain, CC);

  assert(NumParts > 0 && "No parts to assemble!");
  SDValue Val = Parts[0];

  if (NumParts > 1) {
    // Assemble the value from multiple parts.
    if (ValueVT.isInteger()) {
      unsigned PartBits = PartVT.getSizeInBits();
      unsigned ValueBits = ValueVT.getSizeInBits();

      // Assemble the power of 2 part.
      unsigned RoundParts = llvm::bit_floor(NumParts);
      unsigned RoundBits = PartBits * RoundParts;
      EVT RoundVT = RoundBits == ValueBits ?
        ValueVT : EVT::getIntegerVT(*DAG.getContext(), RoundBits);
      SDValue Lo, Hi;

      EVT HalfVT = EVT::getIntegerVT(*DAG.getContext(), RoundBits/2);

      if (RoundParts > 2) {
        Lo = getCopyFromParts(DAG, DL, Parts, RoundParts / 2, PartVT, HalfVT, V,
                              InChain);
        Hi = getCopyFromParts(DAG, DL, Parts + RoundParts / 2, RoundParts / 2,
                              PartVT, HalfVT, V, InChain);
      } else {
        Lo = DAG.getNode(ISD::BITCAST, DL, HalfVT, Parts[0]);
        Hi = DAG.getNode(ISD::BITCAST, DL, HalfVT, Parts[1]);
      }

      if (DAG.getDataLayout().isBigEndian())
        std::swap(Lo, Hi);

      Val = DAG.getNode(ISD::BUILD_PAIR, DL, RoundVT, Lo, Hi);

      if (RoundParts < NumParts) {
        // Assemble the trailing non-power-of-2 part.
        unsigned OddParts = NumParts - RoundParts;
        EVT OddVT = EVT::getIntegerVT(*DAG.getContext(), OddParts * PartBits);
        Hi = getCopyFromParts(DAG, DL, Parts + RoundParts, OddParts, PartVT,
                              OddVT, V, InChain, CC);

        // Combine the round and odd parts.
        Lo = Val;
        if (DAG.getDataLayout().isBigEndian())
          std::swap(Lo, Hi);
        EVT TotalVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
        Hi = DAG.getNode(ISD::ANY_EXTEND, DL, TotalVT, Hi);
        Hi = DAG.getNode(ISD::SHL, DL, TotalVT, Hi,
                         DAG.getConstant(Lo.getValueSizeInBits(), DL,
                                         TLI.getShiftAmountTy(
                                             TotalVT, DAG.getDataLayout())));
        Lo = DAG.getNode(ISD::ZERO_EXTEND, DL, TotalVT, Lo);
        Val = DAG.getNode(ISD::OR, DL, TotalVT, Lo, Hi);
      }
    } else if (PartVT.isFloatingPoint()) {
      // FP split into multiple FP parts (for ppcf128)
      assert(ValueVT == EVT(MVT::ppcf128) && PartVT == MVT::f64 &&
             "Unexpected split");
      SDValue Lo, Hi;
      Lo = DAG.getNode(ISD::BITCAST, DL, EVT(MVT::f64), Parts[0]);
      Hi = DAG.getNode(ISD::BITCAST, DL, EVT(MVT::f64), Parts[1]);
      if (TLI.hasBigEndianPartOrdering(ValueVT, DAG.getDataLayout()))
        std::swap(Lo, Hi);
      Val = DAG.getNode(ISD::BUILD_PAIR, DL, ValueVT, Lo, Hi);
    } else {
      // FP split into integer parts (soft fp)
      assert(ValueVT.isFloatingPoint() && PartVT.isInteger() &&
             !PartVT.isVector() && "Unexpected split");
      EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), ValueVT.getSizeInBits());
      Val = getCopyFromParts(DAG, DL, Parts, NumParts, PartVT, IntVT, V,
                             InChain, CC);
    }
  }

  // There is now one part, held in Val.  Correct it to match ValueVT.
  // PartEVT is the type of the register class that holds the value.
  // ValueVT is the type of the inline asm operation.
  EVT PartEVT = Val.getValueType();

  if (PartEVT == ValueVT)
    return Val;

  if (PartEVT.isInteger() && ValueVT.isFloatingPoint() &&
      ValueVT.bitsLT(PartEVT)) {
    // For an FP value in an integer part, we need to truncate to the right
    // width first.
    PartEVT = EVT::getIntegerVT(*DAG.getContext(),  ValueVT.getSizeInBits());
    Val = DAG.getNode(ISD::TRUNCATE, DL, PartEVT, Val);
  }

  // Handle types that have the same size.
  if (PartEVT.getSizeInBits() == ValueVT.getSizeInBits())
    return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);

  // Handle types with different sizes.
  if (PartEVT.isInteger() && ValueVT.isInteger()) {
    if (ValueVT.bitsLT(PartEVT)) {
      // For a truncate, see if we have any information to
      // indicate whether the truncated bits will always be
      // zero or sign-extension.
      if (AssertOp)
        Val = DAG.getNode(*AssertOp, DL, PartEVT, Val,
                          DAG.getValueType(ValueVT));
      return DAG.getNode(ISD::TRUNCATE, DL, ValueVT, Val);
    }
    return DAG.getNode(ISD::ANY_EXTEND, DL, ValueVT, Val);
  }

  if (PartEVT.isFloatingPoint() && ValueVT.isFloatingPoint()) {
    // FP_ROUND's are always exact here.
    if (ValueVT.bitsLT(Val.getValueType())) {

      SDValue NoChange =
          DAG.getTargetConstant(1, DL, TLI.getPointerTy(DAG.getDataLayout()));

      if (DAG.getMachineFunction().getFunction().getAttributes().hasFnAttr(
              llvm::Attribute::StrictFP)) {
        return DAG.getNode(ISD::STRICT_FP_ROUND, DL,
                           DAG.getVTList(ValueVT, MVT::Other), InChain, Val,
                           NoChange);
      }

      return DAG.getNode(ISD::FP_ROUND, DL, ValueVT, Val, NoChange);
    }

    return DAG.getNode(ISD::FP_EXTEND, DL, ValueVT, Val);
  }

  // Handle MMX to a narrower integer type by bitcasting MMX to integer and
  // then truncating.
  if (PartEVT == MVT::x86mmx && ValueVT.isInteger() &&
      ValueVT.bitsLT(PartEVT)) {
    Val = DAG.getNode(ISD::BITCAST, DL, MVT::i64, Val);
    return DAG.getNode(ISD::TRUNCATE, DL, ValueVT, Val);
  }

  report_fatal_error("Unknown mismatch in getCopyFromParts!");
}

static void diagnosePossiblyInvalidConstraint(LLVMContext &Ctx, const Value *V,
                                              const Twine &ErrMsg) {
  const Instruction *I = dyn_cast_or_null<Instruction>(V);
  if (!V)
    return Ctx.emitError(ErrMsg);

  const char *AsmError = ", possible invalid constraint for vector type";
  if (const CallInst *CI = dyn_cast<CallInst>(I))
    if (CI->isInlineAsm())
      return Ctx.emitError(I, ErrMsg + AsmError);

  return Ctx.emitError(I, ErrMsg);
}

/// getCopyFromPartsVector - Create a value that contains the specified legal
/// parts combined into the value they represent.  If the parts combine to a
/// type larger than ValueVT then AssertOp can be used to specify whether the
/// extra bits are known to be zero (ISD::AssertZext) or sign extended from
/// ValueVT (ISD::AssertSext).
static SDValue getCopyFromPartsVector(SelectionDAG &DAG, const SDLoc &DL,
                                      const SDValue *Parts, unsigned NumParts,
                                      MVT PartVT, EVT ValueVT, const Value *V,
                                      SDValue InChain,
                                      std::optional<CallingConv::ID> CallConv) {
  assert(ValueVT.isVector() && "Not a vector value");
  assert(NumParts > 0 && "No parts to assemble!");
  const bool IsABIRegCopy = CallConv.has_value();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Val = Parts[0];

  // Handle a multi-element vector.
  if (NumParts > 1) {
    EVT IntermediateVT;
    MVT RegisterVT;
    unsigned NumIntermediates;
    unsigned NumRegs;

    if (IsABIRegCopy) {
      NumRegs = TLI.getVectorTypeBreakdownForCallingConv(
          *DAG.getContext(), *CallConv, ValueVT, IntermediateVT,
          NumIntermediates, RegisterVT);
    } else {
      NumRegs =
          TLI.getVectorTypeBreakdown(*DAG.getContext(), ValueVT, IntermediateVT,
                                     NumIntermediates, RegisterVT);
    }

    assert(NumRegs == NumParts && "Part count doesn't match vector breakdown!");
    NumParts = NumRegs; // Silence a compiler warning.
    assert(RegisterVT == PartVT && "Part type doesn't match vector breakdown!");
    assert(RegisterVT.getSizeInBits() ==
           Parts[0].getSimpleValueType().getSizeInBits() &&
           "Part type sizes don't match!");

    // Assemble the parts into intermediate operands.
    SmallVector<SDValue, 8> Ops(NumIntermediates);
    if (NumIntermediates == NumParts) {
      // If the register was not expanded, truncate or copy the value,
      // as appropriate.
      for (unsigned i = 0; i != NumParts; ++i)
        Ops[i] = getCopyFromParts(DAG, DL, &Parts[i], 1, PartVT, IntermediateVT,
                                  V, InChain, CallConv);
    } else if (NumParts > 0) {
      // If the intermediate type was expanded, build the intermediate
      // operands from the parts.
      assert(NumParts % NumIntermediates == 0 &&
             "Must expand into a divisible number of parts!");
      unsigned Factor = NumParts / NumIntermediates;
      for (unsigned i = 0; i != NumIntermediates; ++i)
        Ops[i] = getCopyFromParts(DAG, DL, &Parts[i * Factor], Factor, PartVT,
                                  IntermediateVT, V, InChain, CallConv);
    }

    // Build a vector with BUILD_VECTOR or CONCAT_VECTORS from the
    // intermediate operands.
    EVT BuiltVectorTy =
        IntermediateVT.isVector()
            ? EVT::getVectorVT(
                  *DAG.getContext(), IntermediateVT.getScalarType(),
                  IntermediateVT.getVectorElementCount() * NumParts)
            : EVT::getVectorVT(*DAG.getContext(),
                               IntermediateVT.getScalarType(),
                               NumIntermediates);
    Val = DAG.getNode(IntermediateVT.isVector() ? ISD::CONCAT_VECTORS
                                                : ISD::BUILD_VECTOR,
                      DL, BuiltVectorTy, Ops);
  }

  // There is now one part, held in Val.  Correct it to match ValueVT.
  EVT PartEVT = Val.getValueType();

  if (PartEVT == ValueVT)
    return Val;

  if (PartEVT.isVector()) {
    // Vector/Vector bitcast.
    if (ValueVT.getSizeInBits() == PartEVT.getSizeInBits())
      return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);

    // If the parts vector has more elements than the value vector, then we
    // have a vector widening case (e.g. <2 x float> -> <4 x float>).
    // Extract the elements we want.
    if (PartEVT.getVectorElementCount() != ValueVT.getVectorElementCount()) {
      assert((PartEVT.getVectorElementCount().getKnownMinValue() >
              ValueVT.getVectorElementCount().getKnownMinValue()) &&
             (PartEVT.getVectorElementCount().isScalable() ==
              ValueVT.getVectorElementCount().isScalable()) &&
             "Cannot narrow, it would be a lossy transformation");
      PartEVT =
          EVT::getVectorVT(*DAG.getContext(), PartEVT.getVectorElementType(),
                           ValueVT.getVectorElementCount());
      Val = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, PartEVT, Val,
                        DAG.getVectorIdxConstant(0, DL));
      if (PartEVT == ValueVT)
        return Val;
      if (PartEVT.isInteger() && ValueVT.isFloatingPoint())
        return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);

      // Vector/Vector bitcast (e.g. <2 x bfloat> -> <2 x half>).
      if (ValueVT.getSizeInBits() == PartEVT.getSizeInBits())
        return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);
    }

    // Promoted vector extract
    return DAG.getAnyExtOrTrunc(Val, DL, ValueVT);
  }

  // Trivial bitcast if the types are the same size and the destination
  // vector type is legal.
  if (PartEVT.getSizeInBits() == ValueVT.getSizeInBits() &&
      TLI.isTypeLegal(ValueVT))
    return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);

  if (ValueVT.getVectorNumElements() != 1) {
     // Certain ABIs require that vectors are passed as integers. For vectors
     // are the same size, this is an obvious bitcast.
     if (ValueVT.getSizeInBits() == PartEVT.getSizeInBits()) {
       return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);
     } else if (ValueVT.bitsLT(PartEVT)) {
       const uint64_t ValueSize = ValueVT.getFixedSizeInBits();
       EVT IntermediateType = EVT::getIntegerVT(*DAG.getContext(), ValueSize);
       // Drop the extra bits.
       Val = DAG.getNode(ISD::TRUNCATE, DL, IntermediateType, Val);
       return DAG.getBitcast(ValueVT, Val);
     }

     diagnosePossiblyInvalidConstraint(
         *DAG.getContext(), V, "non-trivial scalar-to-vector conversion");
     return DAG.getUNDEF(ValueVT);
  }

  // Handle cases such as i8 -> <1 x i1>
  EVT ValueSVT = ValueVT.getVectorElementType();
  if (ValueVT.getVectorNumElements() == 1 && ValueSVT != PartEVT) {
    unsigned ValueSize = ValueSVT.getSizeInBits();
    if (ValueSize == PartEVT.getSizeInBits()) {
      Val = DAG.getNode(ISD::BITCAST, DL, ValueSVT, Val);
    } else if (ValueSVT.isFloatingPoint() && PartEVT.isInteger()) {
      // It's possible a scalar floating point type gets softened to integer and
      // then promoted to a larger integer. If PartEVT is the larger integer
      // we need to truncate it and then bitcast to the FP type.
      assert(ValueSVT.bitsLT(PartEVT) && "Unexpected types");
      EVT IntermediateType = EVT::getIntegerVT(*DAG.getContext(), ValueSize);
      Val = DAG.getNode(ISD::TRUNCATE, DL, IntermediateType, Val);
      Val = DAG.getBitcast(ValueSVT, Val);
    } else {
      Val = ValueVT.isFloatingPoint()
                ? DAG.getFPExtendOrRound(Val, DL, ValueSVT)
                : DAG.getAnyExtOrTrunc(Val, DL, ValueSVT);
    }
  }

  return DAG.getBuildVector(ValueVT, DL, Val);
}

static void getCopyToPartsVector(SelectionDAG &DAG, const SDLoc &dl,
                                 SDValue Val, SDValue *Parts, unsigned NumParts,
                                 MVT PartVT, const Value *V,
                                 std::optional<CallingConv::ID> CallConv);

/// getCopyToParts - Create a series of nodes that contain the specified value
/// split into legal parts.  If the parts contain more bits than Val, then, for
/// integers, ExtendKind can be used to specify how to generate the extra bits.
static void
getCopyToParts(SelectionDAG &DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
               unsigned NumParts, MVT PartVT, const Value *V,
               std::optional<CallingConv::ID> CallConv = std::nullopt,
               ISD::NodeType ExtendKind = ISD::ANY_EXTEND) {
  // Let the target split the parts if it wants to
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (TLI.splitValueIntoRegisterParts(DAG, DL, Val, Parts, NumParts, PartVT,
                                      CallConv))
    return;
  EVT ValueVT = Val.getValueType();

  // Handle the vector case separately.
  if (ValueVT.isVector())
    return getCopyToPartsVector(DAG, DL, Val, Parts, NumParts, PartVT, V,
                                CallConv);

  unsigned OrigNumParts = NumParts;
  assert(DAG.getTargetLoweringInfo().isTypeLegal(PartVT) &&
         "Copying to an illegal type!");

  if (NumParts == 0)
    return;

  assert(!ValueVT.isVector() && "Vector case handled elsewhere");
  EVT PartEVT = PartVT;
  if (PartEVT == ValueVT) {
    assert(NumParts == 1 && "No-op copy with multiple parts!");
    Parts[0] = Val;
    return;
  }

  unsigned PartBits = PartVT.getSizeInBits();
  if (NumParts * PartBits > ValueVT.getSizeInBits()) {
    // If the parts cover more bits than the value has, promote the value.
    if (PartVT.isFloatingPoint() && ValueVT.isFloatingPoint()) {
      assert(NumParts == 1 && "Do not know what to promote to!");
      Val = DAG.getNode(ISD::FP_EXTEND, DL, PartVT, Val);
    } else {
      if (ValueVT.isFloatingPoint()) {
        // FP values need to be bitcast, then extended if they are being put
        // into a larger container.
        ValueVT = EVT::getIntegerVT(*DAG.getContext(),  ValueVT.getSizeInBits());
        Val = DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);
      }
      assert((PartVT.isInteger() || PartVT == MVT::x86mmx) &&
             ValueVT.isInteger() &&
             "Unknown mismatch!");
      ValueVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
      Val = DAG.getNode(ExtendKind, DL, ValueVT, Val);
      if (PartVT == MVT::x86mmx)
        Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
    }
  } else if (PartBits == ValueVT.getSizeInBits()) {
    // Different types of the same size.
    assert(NumParts == 1 && PartEVT != ValueVT);
    Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
  } else if (NumParts * PartBits < ValueVT.getSizeInBits()) {
    // If the parts cover less bits than value has, truncate the value.
    assert((PartVT.isInteger() || PartVT == MVT::x86mmx) &&
           ValueVT.isInteger() &&
           "Unknown mismatch!");
    ValueVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
    Val = DAG.getNode(ISD::TRUNCATE, DL, ValueVT, Val);
    if (PartVT == MVT::x86mmx)
      Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
  }

  // The value may have changed - recompute ValueVT.
  ValueVT = Val.getValueType();
  assert(NumParts * PartBits == ValueVT.getSizeInBits() &&
         "Failed to tile the value with PartVT!");

  if (NumParts == 1) {
    if (PartEVT != ValueVT) {
      diagnosePossiblyInvalidConstraint(*DAG.getContext(), V,
                                        "scalar-to-vector conversion failed");
      Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
    }

    Parts[0] = Val;
    return;
  }

  // Expand the value into multiple parts.
  if (NumParts & (NumParts - 1)) {
    // The number of parts is not a power of 2.  Split off and copy the tail.
    assert(PartVT.isInteger() && ValueVT.isInteger() &&
           "Do not know what to expand to!");
    unsigned RoundParts = llvm::bit_floor(NumParts);
    unsigned RoundBits = RoundParts * PartBits;
    unsigned OddParts = NumParts - RoundParts;
    SDValue OddVal = DAG.getNode(ISD::SRL, DL, ValueVT, Val,
      DAG.getShiftAmountConstant(RoundBits, ValueVT, DL));

    getCopyToParts(DAG, DL, OddVal, Parts + RoundParts, OddParts, PartVT, V,
                   CallConv);

    if (DAG.getDataLayout().isBigEndian())
      // The odd parts were reversed by getCopyToParts - unreverse them.
      std::reverse(Parts + RoundParts, Parts + NumParts);

    NumParts = RoundParts;
    ValueVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
    Val = DAG.getNode(ISD::TRUNCATE, DL, ValueVT, Val);
  }

  // The number of parts is a power of 2.  Repeatedly bisect the value using
  // EXTRACT_ELEMENT.
  Parts[0] = DAG.getNode(ISD::BITCAST, DL,
                         EVT::getIntegerVT(*DAG.getContext(),
                                           ValueVT.getSizeInBits()),
                         Val);

  for (unsigned StepSize = NumParts; StepSize > 1; StepSize /= 2) {
    for (unsigned i = 0; i < NumParts; i += StepSize) {
      unsigned ThisBits = StepSize * PartBits / 2;
      EVT ThisVT = EVT::getIntegerVT(*DAG.getContext(), ThisBits);
      SDValue &Part0 = Parts[i];
      SDValue &Part1 = Parts[i+StepSize/2];

      Part1 = DAG.getNode(ISD::EXTRACT_ELEMENT, DL,
                          ThisVT, Part0, DAG.getIntPtrConstant(1, DL));
      Part0 = DAG.getNode(ISD::EXTRACT_ELEMENT, DL,
                          ThisVT, Part0, DAG.getIntPtrConstant(0, DL));

      if (ThisBits == PartBits && ThisVT != PartVT) {
        Part0 = DAG.getNode(ISD::BITCAST, DL, PartVT, Part0);
        Part1 = DAG.getNode(ISD::BITCAST, DL, PartVT, Part1);
      }
    }
  }

  if (DAG.getDataLayout().isBigEndian())
    std::reverse(Parts, Parts + OrigNumParts);
}

static SDValue widenVectorToPartType(SelectionDAG &DAG, SDValue Val,
                                     const SDLoc &DL, EVT PartVT) {
  if (!PartVT.isVector())
    return SDValue();

  EVT ValueVT = Val.getValueType();
  EVT PartEVT = PartVT.getVectorElementType();
  EVT ValueEVT = ValueVT.getVectorElementType();
  ElementCount PartNumElts = PartVT.getVectorElementCount();
  ElementCount ValueNumElts = ValueVT.getVectorElementCount();

  // We only support widening vectors with equivalent element types and
  // fixed/scalable properties. If a target needs to widen a fixed-length type
  // to a scalable one, it should be possible to use INSERT_SUBVECTOR below.
  if (ElementCount::isKnownLE(PartNumElts, ValueNumElts) ||
      PartNumElts.isScalable() != ValueNumElts.isScalable())
    return SDValue();

  // Have a try for bf16 because some targets share its ABI with fp16.
  if (ValueEVT == MVT::bf16 && PartEVT == MVT::f16) {
    assert(DAG.getTargetLoweringInfo().isTypeLegal(PartVT) &&
           "Cannot widen to illegal type");
    Val = DAG.getNode(ISD::BITCAST, DL,
                      ValueVT.changeVectorElementType(MVT::f16), Val);
  } else if (PartEVT != ValueEVT) {
    return SDValue();
  }

  // Widening a scalable vector to another scalable vector is done by inserting
  // the vector into a larger undef one.
  if (PartNumElts.isScalable())
    return DAG.getNode(ISD::INSERT_SUBVECTOR, DL, PartVT, DAG.getUNDEF(PartVT),
                       Val, DAG.getVectorIdxConstant(0, DL));

  // Vector widening case, e.g. <2 x float> -> <4 x float>.  Shuffle in
  // undef elements.
  SmallVector<SDValue, 16> Ops;
  DAG.ExtractVectorElements(Val, Ops);
  SDValue EltUndef = DAG.getUNDEF(PartEVT);
  Ops.append((PartNumElts - ValueNumElts).getFixedValue(), EltUndef);

  // FIXME: Use CONCAT for 2x -> 4x.
  return DAG.getBuildVector(PartVT, DL, Ops);
}

/// getCopyToPartsVector - Create a series of nodes that contain the specified
/// value split into legal parts.
static void getCopyToPartsVector(SelectionDAG &DAG, const SDLoc &DL,
                                 SDValue Val, SDValue *Parts, unsigned NumParts,
                                 MVT PartVT, const Value *V,
                                 std::optional<CallingConv::ID> CallConv) {
  EVT ValueVT = Val.getValueType();
  assert(ValueVT.isVector() && "Not a vector");
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const bool IsABIRegCopy = CallConv.has_value();

  if (NumParts == 1) {
    EVT PartEVT = PartVT;
    if (PartEVT == ValueVT) {
      // Nothing to do.
    } else if (PartVT.getSizeInBits() == ValueVT.getSizeInBits()) {
      // Bitconvert vector->vector case.
      Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
    } else if (SDValue Widened = widenVectorToPartType(DAG, Val, DL, PartVT)) {
      Val = Widened;
    } else if (PartVT.isVector() &&
               PartEVT.getVectorElementType().bitsGE(
                   ValueVT.getVectorElementType()) &&
               PartEVT.getVectorElementCount() ==
                   ValueVT.getVectorElementCount()) {

      // Promoted vector extract
      Val = DAG.getAnyExtOrTrunc(Val, DL, PartVT);
    } else if (PartEVT.isVector() &&
               PartEVT.getVectorElementType() !=
                   ValueVT.getVectorElementType() &&
               TLI.getTypeAction(*DAG.getContext(), ValueVT) ==
                   TargetLowering::TypeWidenVector) {
      // Combination of widening and promotion.
      EVT WidenVT =
          EVT::getVectorVT(*DAG.getContext(), ValueVT.getVectorElementType(),
                           PartVT.getVectorElementCount());
      SDValue Widened = widenVectorToPartType(DAG, Val, DL, WidenVT);
      Val = DAG.getAnyExtOrTrunc(Widened, DL, PartVT);
    } else {
      // Don't extract an integer from a float vector. This can happen if the
      // FP type gets softened to integer and then promoted. The promotion
      // prevents it from being picked up by the earlier bitcast case.
      if (ValueVT.getVectorElementCount().isScalar() &&
          (!ValueVT.isFloatingPoint() || !PartVT.isInteger())) {
        // If we reach this condition and PartVT is FP, this means that
        // ValueVT is also FP and both have a different size, otherwise we
        // would have bitcasted them. Producing an EXTRACT_VECTOR_ELT here
        // would be invalid since that would mean the smaller FP type has to
        // be extended to the larger one.
        if (PartVT.isFloatingPoint()) {
          Val = DAG.getBitcast(ValueVT.getScalarType(), Val);
          Val = DAG.getNode(ISD::FP_EXTEND, DL, PartVT, Val);
        } else
          Val = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, PartVT, Val,
                            DAG.getVectorIdxConstant(0, DL));
      } else {
        uint64_t ValueSize = ValueVT.getFixedSizeInBits();
        assert(PartVT.getFixedSizeInBits() > ValueSize &&
               "lossy conversion of vector to scalar type");
        EVT IntermediateType = EVT::getIntegerVT(*DAG.getContext(), ValueSize);
        Val = DAG.getBitcast(IntermediateType, Val);
        Val = DAG.getAnyExtOrTrunc(Val, DL, PartVT);
      }
    }

    assert(Val.getValueType() == PartVT && "Unexpected vector part value type");
    Parts[0] = Val;
    return;
  }

  // Handle a multi-element vector.
  EVT IntermediateVT;
  MVT RegisterVT;
  unsigned NumIntermediates;
  unsigned NumRegs;
  if (IsABIRegCopy) {
    NumRegs = TLI.getVectorTypeBreakdownForCallingConv(
        *DAG.getContext(), *CallConv, ValueVT, IntermediateVT, NumIntermediates,
        RegisterVT);
  } else {
    NumRegs =
        TLI.getVectorTypeBreakdown(*DAG.getContext(), ValueVT, IntermediateVT,
                                   NumIntermediates, RegisterVT);
  }

  assert(NumRegs == NumParts && "Part count doesn't match vector breakdown!");
  NumParts = NumRegs; // Silence a compiler warning.
  assert(RegisterVT == PartVT && "Part type doesn't match vector breakdown!");

  assert(IntermediateVT.isScalableVector() == ValueVT.isScalableVector() &&
         "Mixing scalable and fixed vectors when copying in parts");

  std::optional<ElementCount> DestEltCnt;

  if (IntermediateVT.isVector())
    DestEltCnt = IntermediateVT.getVectorElementCount() * NumIntermediates;
  else
    DestEltCnt = ElementCount::getFixed(NumIntermediates);

  EVT BuiltVectorTy = EVT::getVectorVT(
      *DAG.getContext(), IntermediateVT.getScalarType(), *DestEltCnt);

  if (ValueVT == BuiltVectorTy) {
    // Nothing to do.
  } else if (ValueVT.getSizeInBits() == BuiltVectorTy.getSizeInBits()) {
    // Bitconvert vector->vector case.
    Val = DAG.getNode(ISD::BITCAST, DL, BuiltVectorTy, Val);
  } else {
    if (BuiltVectorTy.getVectorElementType().bitsGT(
            ValueVT.getVectorElementType())) {
      // Integer promotion.
      ValueVT = EVT::getVectorVT(*DAG.getContext(),
                                 BuiltVectorTy.getVectorElementType(),
                                 ValueVT.getVectorElementCount());
      Val = DAG.getNode(ISD::ANY_EXTEND, DL, ValueVT, Val);
    }

    if (SDValue Widened = widenVectorToPartType(DAG, Val, DL, BuiltVectorTy)) {
      Val = Widened;
    }
  }

  assert(Val.getValueType() == BuiltVectorTy && "Unexpected vector value type");

  // Split the vector into intermediate operands.
  SmallVector<SDValue, 8> Ops(NumIntermediates);
  for (unsigned i = 0; i != NumIntermediates; ++i) {
    if (IntermediateVT.isVector()) {
      // This does something sensible for scalable vectors - see the
      // definition of EXTRACT_SUBVECTOR for further details.
      unsigned IntermediateNumElts = IntermediateVT.getVectorMinNumElements();
      Ops[i] =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, IntermediateVT, Val,
                      DAG.getVectorIdxConstant(i * IntermediateNumElts, DL));
    } else {
      Ops[i] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, IntermediateVT, Val,
                           DAG.getVectorIdxConstant(i, DL));
    }
  }

  // Split the intermediate operands into legal parts.
  if (NumParts == NumIntermediates) {
    // If the register was not expanded, promote or copy the value,
    // as appropriate.
    for (unsigned i = 0; i != NumParts; ++i)
      getCopyToParts(DAG, DL, Ops[i], &Parts[i], 1, PartVT, V, CallConv);
  } else if (NumParts > 0) {
    // If the intermediate type was expanded, split each the value into
    // legal parts.
    assert(NumIntermediates != 0 && "division by zero");
    assert(NumParts % NumIntermediates == 0 &&
           "Must expand into a divisible number of parts!");
    unsigned Factor = NumParts / NumIntermediates;
    for (unsigned i = 0; i != NumIntermediates; ++i)
      getCopyToParts(DAG, DL, Ops[i], &Parts[i * Factor], Factor, PartVT, V,
                     CallConv);
  }
}

RegsForValue::RegsForValue(const SmallVector<unsigned, 4> &regs, MVT regvt,
                           EVT valuevt, std::optional<CallingConv::ID> CC)
    : ValueVTs(1, valuevt), RegVTs(1, regvt), Regs(regs),
      RegCount(1, regs.size()), CallConv(CC) {}

RegsForValue::RegsForValue(LLVMContext &Context, const TargetLowering &TLI,
                           const DataLayout &DL, unsigned Reg, Type *Ty,
                           std::optional<CallingConv::ID> CC) {
  ComputeValueVTs(TLI, DL, Ty, ValueVTs);

  CallConv = CC;

  for (EVT ValueVT : ValueVTs) {
    unsigned NumRegs =
        isABIMangled()
            ? TLI.getNumRegistersForCallingConv(Context, *CC, ValueVT)
            : TLI.getNumRegisters(Context, ValueVT);
    MVT RegisterVT =
        isABIMangled()
            ? TLI.getRegisterTypeForCallingConv(Context, *CC, ValueVT)
            : TLI.getRegisterType(Context, ValueVT);
    for (unsigned i = 0; i != NumRegs; ++i)
      Regs.push_back(Reg + i);
    RegVTs.push_back(RegisterVT);
    RegCount.push_back(NumRegs);
    Reg += NumRegs;
  }
}

SDValue RegsForValue::getCopyFromRegs(SelectionDAG &DAG,
                                      FunctionLoweringInfo &FuncInfo,
                                      const SDLoc &dl, SDValue &Chain,
                                      SDValue *Glue, const Value *V) const {
  // A Value with type {} or [0 x %t] needs no registers.
  if (ValueVTs.empty())
    return SDValue();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // Assemble the legal parts into the final values.
  SmallVector<SDValue, 4> Values(ValueVTs.size());
  SmallVector<SDValue, 8> Parts;
  for (unsigned Value = 0, Part = 0, e = ValueVTs.size(); Value != e; ++Value) {
    // Copy the legal parts from the registers.
    EVT ValueVT = ValueVTs[Value];
    unsigned NumRegs = RegCount[Value];
    MVT RegisterVT = isABIMangled()
                         ? TLI.getRegisterTypeForCallingConv(
                               *DAG.getContext(), *CallConv, RegVTs[Value])
                         : RegVTs[Value];

    Parts.resize(NumRegs);
    for (unsigned i = 0; i != NumRegs; ++i) {
      SDValue P;
      if (!Glue) {
        P = DAG.getCopyFromReg(Chain, dl, Regs[Part+i], RegisterVT);
      } else {
        P = DAG.getCopyFromReg(Chain, dl, Regs[Part+i], RegisterVT, *Glue);
        *Glue = P.getValue(2);
      }

      Chain = P.getValue(1);
      Parts[i] = P;

      // If the source register was virtual and if we know something about it,
      // add an assert node.
      if (!Register::isVirtualRegister(Regs[Part + i]) ||
          !RegisterVT.isInteger())
        continue;

      const FunctionLoweringInfo::LiveOutInfo *LOI =
        FuncInfo.GetLiveOutRegInfo(Regs[Part+i]);
      if (!LOI)
        continue;

      unsigned RegSize = RegisterVT.getScalarSizeInBits();
      unsigned NumSignBits = LOI->NumSignBits;
      unsigned NumZeroBits = LOI->Known.countMinLeadingZeros();

      if (NumZeroBits == RegSize) {
        // The current value is a zero.
        // Explicitly express that as it would be easier for
        // optimizations to kick in.
        Parts[i] = DAG.getConstant(0, dl, RegisterVT);
        continue;
      }

      // FIXME: We capture more information than the dag can represent.  For
      // now, just use the tightest assertzext/assertsext possible.
      bool isSExt;
      EVT FromVT(MVT::Other);
      if (NumZeroBits) {
        FromVT = EVT::getIntegerVT(*DAG.getContext(), RegSize - NumZeroBits);
        isSExt = false;
      } else if (NumSignBits > 1) {
        FromVT =
            EVT::getIntegerVT(*DAG.getContext(), RegSize - NumSignBits + 1);
        isSExt = true;
      } else {
        continue;
      }
      // Add an assertion node.
      assert(FromVT != MVT::Other);
      Parts[i] = DAG.getNode(isSExt ? ISD::AssertSext : ISD::AssertZext, dl,
                             RegisterVT, P, DAG.getValueType(FromVT));
    }

    Values[Value] = getCopyFromParts(DAG, dl, Parts.begin(), NumRegs,
                                     RegisterVT, ValueVT, V, Chain, CallConv);
    Part += NumRegs;
    Parts.clear();
  }

  return DAG.getNode(ISD::MERGE_VALUES, dl, DAG.getVTList(ValueVTs), Values);
}

void RegsForValue::getCopyToRegs(SDValue Val, SelectionDAG &DAG,
                                 const SDLoc &dl, SDValue &Chain, SDValue *Glue,
                                 const Value *V,
                                 ISD::NodeType PreferredExtendType) const {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  ISD::NodeType ExtendKind = PreferredExtendType;

  // Get the list of the values's legal parts.
  unsigned NumRegs = Regs.size();
  SmallVector<SDValue, 8> Parts(NumRegs);
  for (unsigned Value = 0, Part = 0, e = ValueVTs.size(); Value != e; ++Value) {
    unsigned NumParts = RegCount[Value];

    MVT RegisterVT = isABIMangled()
                         ? TLI.getRegisterTypeForCallingConv(
                               *DAG.getContext(), *CallConv, RegVTs[Value])
                         : RegVTs[Value];

    if (ExtendKind == ISD::ANY_EXTEND && TLI.isZExtFree(Val, RegisterVT))
      ExtendKind = ISD::ZERO_EXTEND;

    getCopyToParts(DAG, dl, Val.getValue(Val.getResNo() + Value), &Parts[Part],
                   NumParts, RegisterVT, V, CallConv, ExtendKind);
    Part += NumParts;
  }

  // Copy the parts into the registers.
  SmallVector<SDValue, 8> Chains(NumRegs);
  for (unsigned i = 0; i != NumRegs; ++i) {
    SDValue Part;
    if (!Glue) {
      Part = DAG.getCopyToReg(Chain, dl, Regs[i], Parts[i]);
    } else {
      Part = DAG.getCopyToReg(Chain, dl, Regs[i], Parts[i], *Glue);
      *Glue = Part.getValue(1);
    }

    Chains[i] = Part.getValue(0);
  }

  if (NumRegs == 1 || Glue)
    // If NumRegs > 1 && Glue is used then the use of the last CopyToReg is
    // flagged to it. That is the CopyToReg nodes and the user are considered
    // a single scheduling unit. If we create a TokenFactor and return it as
    // chain, then the TokenFactor is both a predecessor (operand) of the
    // user as well as a successor (the TF operands are flagged to the user).
    // c1, f1 = CopyToReg
    // c2, f2 = CopyToReg
    // c3     = TokenFactor c1, c2
    // ...
    //        = op c3, ..., f2
    Chain = Chains[NumRegs-1];
  else
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
}

void RegsForValue::AddInlineAsmOperands(InlineAsm::Kind Code, bool HasMatching,
                                        unsigned MatchingIdx, const SDLoc &dl,
                                        SelectionDAG &DAG,
                                        std::vector<SDValue> &Ops) const {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  InlineAsm::Flag Flag(Code, Regs.size());
  if (HasMatching)
    Flag.setMatchingOp(MatchingIdx);
  else if (!Regs.empty() && Register::isVirtualRegister(Regs.front())) {
    // Put the register class of the virtual registers in the flag word.  That
    // way, later passes can recompute register class constraints for inline
    // assembly as well as normal instructions.
    // Don't do this for tied operands that can use the regclass information
    // from the def.
    const MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
    const TargetRegisterClass *RC = MRI.getRegClass(Regs.front());
    Flag.setRegClass(RC->getID());
  }

  SDValue Res = DAG.getTargetConstant(Flag, dl, MVT::i32);
  Ops.push_back(Res);

  if (Code == InlineAsm::Kind::Clobber) {
    // Clobbers should always have a 1:1 mapping with registers, and may
    // reference registers that have illegal (e.g. vector) types. Hence, we
    // shouldn't try to apply any sort of splitting logic to them.
    assert(Regs.size() == RegVTs.size() && Regs.size() == ValueVTs.size() &&
           "No 1:1 mapping from clobbers to regs?");
    Register SP = TLI.getStackPointerRegisterToSaveRestore();
    (void)SP;
    for (unsigned I = 0, E = ValueVTs.size(); I != E; ++I) {
      Ops.push_back(DAG.getRegister(Regs[I], RegVTs[I]));
      assert(
          (Regs[I] != SP ||
           DAG.getMachineFunction().getFrameInfo().hasOpaqueSPAdjustment()) &&
          "If we clobbered the stack pointer, MFI should know about it.");
    }
    return;
  }

  for (unsigned Value = 0, Reg = 0, e = ValueVTs.size(); Value != e; ++Value) {
    MVT RegisterVT = RegVTs[Value];
    unsigned NumRegs = TLI.getNumRegisters(*DAG.getContext(), ValueVTs[Value],
                                           RegisterVT);
    for (unsigned i = 0; i != NumRegs; ++i) {
      assert(Reg < Regs.size() && "Mismatch in # registers expected");
      unsigned TheReg = Regs[Reg++];
      Ops.push_back(DAG.getRegister(TheReg, RegisterVT));
    }
  }
}

SmallVector<std::pair<unsigned, TypeSize>, 4>
RegsForValue::getRegsAndSizes() const {
  SmallVector<std::pair<unsigned, TypeSize>, 4> OutVec;
  unsigned I = 0;
  for (auto CountAndVT : zip_first(RegCount, RegVTs)) {
    unsigned RegCount = std::get<0>(CountAndVT);
    MVT RegisterVT = std::get<1>(CountAndVT);
    TypeSize RegisterSize = RegisterVT.getSizeInBits();
    for (unsigned E = I + RegCount; I != E; ++I)
      OutVec.push_back(std::make_pair(Regs[I], RegisterSize));
  }
  return OutVec;
}

void SelectionDAGBuilder::init(GCFunctionInfo *gfi, AliasAnalysis *aa,
                               AssumptionCache *ac,
                               const TargetLibraryInfo *li) {
  AA = aa;
  AC = ac;
  GFI = gfi;
  LibInfo = li;
  Context = DAG.getContext();
  LPadToCallSiteMap.clear();
  SL->init(DAG.getTargetLoweringInfo(), TM, DAG.getDataLayout());
  AssignmentTrackingEnabled = isAssignmentTrackingEnabled(
      *DAG.getMachineFunction().getFunction().getParent());
}

void SelectionDAGBuilder::clear() {
  NodeMap.clear();
  UnusedArgNodeMap.clear();
  PendingLoads.clear();
  PendingExports.clear();
  PendingConstrainedFP.clear();
  PendingConstrainedFPStrict.clear();
  CurInst = nullptr;
  HasTailCall = false;
  SDNodeOrder = LowestSDNodeOrder;
  StatepointLowering.clear();
}

void SelectionDAGBuilder::clearDanglingDebugInfo() {
  DanglingDebugInfoMap.clear();
}

// Update DAG root to include dependencies on Pending chains.
SDValue SelectionDAGBuilder::updateRoot(SmallVectorImpl<SDValue> &Pending) {
  SDValue Root = DAG.getRoot();

  if (Pending.empty())
    return Root;

  // Add current root to PendingChains, unless we already indirectly
  // depend on it.
  if (Root.getOpcode() != ISD::EntryToken) {
    unsigned i = 0, e = Pending.size();
    for (; i != e; ++i) {
      assert(Pending[i].getNode()->getNumOperands() > 1);
      if (Pending[i].getNode()->getOperand(0) == Root)
        break;  // Don't add the root if we already indirectly depend on it.
    }

    if (i == e)
      Pending.push_back(Root);
  }

  if (Pending.size() == 1)
    Root = Pending[0];
  else
    Root = DAG.getTokenFactor(getCurSDLoc(), Pending);

  DAG.setRoot(Root);
  Pending.clear();
  return Root;
}

SDValue SelectionDAGBuilder::getMemoryRoot() {
  return updateRoot(PendingLoads);
}

SDValue SelectionDAGBuilder::getRoot() {
  // Chain up all pending constrained intrinsics together with all
  // pending loads, by simply appending them to PendingLoads and
  // then calling getMemoryRoot().
  PendingLoads.reserve(PendingLoads.size() +
                       PendingConstrainedFP.size() +
                       PendingConstrainedFPStrict.size());
  PendingLoads.append(PendingConstrainedFP.begin(),
                      PendingConstrainedFP.end());
  PendingLoads.append(PendingConstrainedFPStrict.begin(),
                      PendingConstrainedFPStrict.end());
  PendingConstrainedFP.clear();
  PendingConstrainedFPStrict.clear();
  return getMemoryRoot();
}

SDValue SelectionDAGBuilder::getControlRoot() {
  // We need to emit pending fpexcept.strict constrained intrinsics,
  // so append them to the PendingExports list.
  PendingExports.append(PendingConstrainedFPStrict.begin(),
                        PendingConstrainedFPStrict.end());
  PendingConstrainedFPStrict.clear();
  return updateRoot(PendingExports);
}

void SelectionDAGBuilder::handleDebugDeclare(Value *Address,
                                             DILocalVariable *Variable,
                                             DIExpression *Expression,
                                             DebugLoc DL) {
  assert(Variable && "Missing variable");

  // Check if address has undef value.
  if (!Address || isa<UndefValue>(Address) ||
      (Address->use_empty() && !isa<Argument>(Address))) {
    LLVM_DEBUG(
        dbgs()
        << "dbg_declare: Dropping debug info (bad/undef/unused-arg address)\n");
    return;
  }

  bool IsParameter = Variable->isParameter() || isa<Argument>(Address);

  SDValue &N = NodeMap[Address];
  if (!N.getNode() && isa<Argument>(Address))
    // Check unused arguments map.
    N = UnusedArgNodeMap[Address];
  SDDbgValue *SDV;
  if (N.getNode()) {
    if (const BitCastInst *BCI = dyn_cast<BitCastInst>(Address))
      Address = BCI->getOperand(0);
    // Parameters are handled specially.
    auto *FINode = dyn_cast<FrameIndexSDNode>(N.getNode());
    if (IsParameter && FINode) {
      // Byval parameter. We have a frame index at this point.
      SDV = DAG.getFrameIndexDbgValue(Variable, Expression, FINode->getIndex(),
                                      /*IsIndirect*/ true, DL, SDNodeOrder);
    } else if (isa<Argument>(Address)) {
      // Address is an argument, so try to emit its dbg value using
      // virtual register info from the FuncInfo.ValueMap.
      EmitFuncArgumentDbgValue(Address, Variable, Expression, DL,
                               FuncArgumentDbgValueKind::Declare, N);
      return;
    } else {
      SDV = DAG.getDbgValue(Variable, Expression, N.getNode(), N.getResNo(),
                            true, DL, SDNodeOrder);
    }
    DAG.AddDbgValue(SDV, IsParameter);
  } else {
    // If Address is an argument then try to emit its dbg value using
    // virtual register info from the FuncInfo.ValueMap.
    if (!EmitFuncArgumentDbgValue(Address, Variable, Expression, DL,
                                  FuncArgumentDbgValueKind::Declare, N)) {
      LLVM_DEBUG(dbgs() << "dbg_declare: Dropping debug info"
                        << " (could not emit func-arg dbg_value)\n");
    }
  }
  return;
}

void SelectionDAGBuilder::visitDbgInfo(const Instruction &I) {
  // Add SDDbgValue nodes for any var locs here. Do so before updating
  // SDNodeOrder, as this mapping is {Inst -> Locs BEFORE Inst}.
  if (FunctionVarLocs const *FnVarLocs = DAG.getFunctionVarLocs()) {
    // Add SDDbgValue nodes for any var locs here. Do so before updating
    // SDNodeOrder, as this mapping is {Inst -> Locs BEFORE Inst}.
    for (auto It = FnVarLocs->locs_begin(&I), End = FnVarLocs->locs_end(&I);
         It != End; ++It) {
      auto *Var = FnVarLocs->getDILocalVariable(It->VariableID);
      dropDanglingDebugInfo(Var, It->Expr);
      if (It->Values.isKillLocation(It->Expr)) {
        handleKillDebugValue(Var, It->Expr, It->DL, SDNodeOrder);
        continue;
      }
      SmallVector<Value *> Values(It->Values.location_ops());
      if (!handleDebugValue(Values, Var, It->Expr, It->DL, SDNodeOrder,
                            It->Values.hasArgList())) {
        SmallVector<Value *, 4> Vals;
        for (Value *V : It->Values.location_ops())
          Vals.push_back(V);
        addDanglingDebugInfo(Vals,
                             FnVarLocs->getDILocalVariable(It->VariableID),
                             It->Expr, Vals.size() > 1, It->DL, SDNodeOrder);
      }
    }
  }

  // We must skip DbgVariableRecords if they've already been processed above as
  // we have just emitted the debug values resulting from assignment tracking
  // analysis, making any existing DbgVariableRecords redundant (and probably
  // less correct). We still need to process DbgLabelRecords. This does sink
  // DbgLabelRecords to the bottom of the group of debug records. That sholdn't
  // be important as it does so deterministcally and ordering between
  // DbgLabelRecords and DbgVariableRecords is immaterial (other than for MIR/IR
  // printing).
  bool SkipDbgVariableRecords = DAG.getFunctionVarLocs();
  // Is there is any debug-info attached to this instruction, in the form of
  // DbgRecord non-instruction debug-info records.
  for (DbgRecord &DR : I.getDbgRecordRange()) {
    if (DbgLabelRecord *DLR = dyn_cast<DbgLabelRecord>(&DR)) {
      assert(DLR->getLabel() && "Missing label");
      SDDbgLabel *SDV =
          DAG.getDbgLabel(DLR->getLabel(), DLR->getDebugLoc(), SDNodeOrder);
      DAG.AddDbgLabel(SDV);
      continue;
    }

    if (SkipDbgVariableRecords)
      continue;
    DbgVariableRecord &DVR = cast<DbgVariableRecord>(DR);
    DILocalVariable *Variable = DVR.getVariable();
    DIExpression *Expression = DVR.getExpression();
    dropDanglingDebugInfo(Variable, Expression);

    if (DVR.getType() == DbgVariableRecord::LocationType::Declare) {
      if (FuncInfo.PreprocessedDVRDeclares.contains(&DVR))
        continue;
      LLVM_DEBUG(dbgs() << "SelectionDAG visiting dbg_declare: " << DVR
                        << "\n");
      handleDebugDeclare(DVR.getVariableLocationOp(0), Variable, Expression,
                         DVR.getDebugLoc());
      continue;
    }

    // A DbgVariableRecord with no locations is a kill location.
    SmallVector<Value *, 4> Values(DVR.location_ops());
    if (Values.empty()) {
      handleKillDebugValue(Variable, Expression, DVR.getDebugLoc(),
                           SDNodeOrder);
      continue;
    }

    // A DbgVariableRecord with an undef or absent location is also a kill
    // location.
    if (llvm::any_of(Values,
                     [](Value *V) { return !V || isa<UndefValue>(V); })) {
      handleKillDebugValue(Variable, Expression, DVR.getDebugLoc(),
                           SDNodeOrder);
      continue;
    }

    bool IsVariadic = DVR.hasArgList();
    if (!handleDebugValue(Values, Variable, Expression, DVR.getDebugLoc(),
                          SDNodeOrder, IsVariadic)) {
      addDanglingDebugInfo(Values, Variable, Expression, IsVariadic,
                           DVR.getDebugLoc(), SDNodeOrder);
    }
  }
}

void SelectionDAGBuilder::visit(const Instruction &I) {
  visitDbgInfo(I);

  // Set up outgoing PHI node register values before emitting the terminator.
  if (I.isTerminator()) {
    HandlePHINodesInSuccessorBlocks(I.getParent());
  }

  // Increase the SDNodeOrder if dealing with a non-debug instruction.
  if (!isa<DbgInfoIntrinsic>(I))
    ++SDNodeOrder;

  CurInst = &I;

  // Set inserted listener only if required.
  bool NodeInserted = false;
  std::unique_ptr<SelectionDAG::DAGNodeInsertedListener> InsertedListener;
  MDNode *PCSectionsMD = I.getMetadata(LLVMContext::MD_pcsections);
  MDNode *MMRA = I.getMetadata(LLVMContext::MD_mmra);
  if (PCSectionsMD || MMRA) {
    InsertedListener = std::make_unique<SelectionDAG::DAGNodeInsertedListener>(
        DAG, [&](SDNode *) { NodeInserted = true; });
  }

  visit(I.getOpcode(), I);

  if (!I.isTerminator() && !HasTailCall &&
      !isa<GCStatepointInst>(I)) // statepoints handle their exports internally
    CopyToExportRegsIfNeeded(&I);

  // Handle metadata.
  if (PCSectionsMD || MMRA) {
    auto It = NodeMap.find(&I);
    if (It != NodeMap.end()) {
      if (PCSectionsMD)
        DAG.addPCSections(It->second.getNode(), PCSectionsMD);
      if (MMRA)
        DAG.addMMRAMetadata(It->second.getNode(), MMRA);
    } else if (NodeInserted) {
      // This should not happen; if it does, don't let it go unnoticed so we can
      // fix it. Relevant visit*() function is probably missing a setValue().
      errs() << "warning: loosing !pcsections and/or !mmra metadata ["
             << I.getModule()->getName() << "]\n";
      LLVM_DEBUG(I.dump());
      assert(false);
    }
  }

  CurInst = nullptr;
}

void SelectionDAGBuilder::visitPHI(const PHINode &) {
  llvm_unreachable("SelectionDAGBuilder shouldn't visit PHI nodes!");
}

void SelectionDAGBuilder::visit(unsigned Opcode, const User &I) {
  // Note: this doesn't use InstVisitor, because it has to work with
  // ConstantExpr's in addition to instructions.
  switch (Opcode) {
  default: llvm_unreachable("Unknown instruction type encountered!");
    // Build the switch statement using the Instruction.def file.
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    case Instruction::OPCODE: visit##OPCODE((const CLASS&)I); break;
#include "llvm/IR/Instruction.def"
  }
}

static bool handleDanglingVariadicDebugInfo(SelectionDAG &DAG,
                                            DILocalVariable *Variable,
                                            DebugLoc DL, unsigned Order,
                                            SmallVectorImpl<Value *> &Values,
                                            DIExpression *Expression) {
  // For variadic dbg_values we will now insert an undef.
  // FIXME: We can potentially recover these!
  SmallVector<SDDbgOperand, 2> Locs;
  for (const Value *V : Values) {
    auto *Undef = UndefValue::get(V->getType());
    Locs.push_back(SDDbgOperand::fromConst(Undef));
  }
  SDDbgValue *SDV = DAG.getDbgValueList(Variable, Expression, Locs, {},
                                        /*IsIndirect=*/false, DL, Order,
                                        /*IsVariadic=*/true);
  DAG.AddDbgValue(SDV, /*isParameter=*/false);
  return true;
}

void SelectionDAGBuilder::addDanglingDebugInfo(SmallVectorImpl<Value *> &Values,
                                               DILocalVariable *Var,
                                               DIExpression *Expr,
                                               bool IsVariadic, DebugLoc DL,
                                               unsigned Order) {
  if (IsVariadic) {
    handleDanglingVariadicDebugInfo(DAG, Var, DL, Order, Values, Expr);
    return;
  }
  // TODO: Dangling debug info will eventually either be resolved or produce
  // an Undef DBG_VALUE. However in the resolution case, a gap may appear
  // between the original dbg.value location and its resolved DBG_VALUE,
  // which we should ideally fill with an extra Undef DBG_VALUE.
  assert(Values.size() == 1);
  DanglingDebugInfoMap[Values[0]].emplace_back(Var, Expr, DL, Order);
}

void SelectionDAGBuilder::dropDanglingDebugInfo(const DILocalVariable *Variable,
                                                const DIExpression *Expr) {
  auto isMatchingDbgValue = [&](DanglingDebugInfo &DDI) {
    DIVariable *DanglingVariable = DDI.getVariable();
    DIExpression *DanglingExpr = DDI.getExpression();
    if (DanglingVariable == Variable && Expr->fragmentsOverlap(DanglingExpr)) {
      LLVM_DEBUG(dbgs() << "Dropping dangling debug info for "
                        << printDDI(nullptr, DDI) << "\n");
      return true;
    }
    return false;
  };

  for (auto &DDIMI : DanglingDebugInfoMap) {
    DanglingDebugInfoVector &DDIV = DDIMI.second;

    // If debug info is to be dropped, run it through final checks to see
    // whether it can be salvaged.
    for (auto &DDI : DDIV)
      if (isMatchingDbgValue(DDI))
        salvageUnresolvedDbgValue(DDIMI.first, DDI);

    erase_if(DDIV, isMatchingDbgValue);
  }
}

// resolveDanglingDebugInfo - if we saw an earlier dbg_value referring to V,
// generate the debug data structures now that we've seen its definition.
void SelectionDAGBuilder::resolveDanglingDebugInfo(const Value *V,
                                                   SDValue Val) {
  auto DanglingDbgInfoIt = DanglingDebugInfoMap.find(V);
  if (DanglingDbgInfoIt == DanglingDebugInfoMap.end())
    return;

  DanglingDebugInfoVector &DDIV = DanglingDbgInfoIt->second;
  for (auto &DDI : DDIV) {
    DebugLoc DL = DDI.getDebugLoc();
    unsigned ValSDNodeOrder = Val.getNode()->getIROrder();
    unsigned DbgSDNodeOrder = DDI.getSDNodeOrder();
    DILocalVariable *Variable = DDI.getVariable();
    DIExpression *Expr = DDI.getExpression();
    assert(Variable->isValidLocationForIntrinsic(DL) &&
           "Expected inlined-at fields to agree");
    SDDbgValue *SDV;
    if (Val.getNode()) {
      // FIXME: I doubt that it is correct to resolve a dangling DbgValue as a
      // FuncArgumentDbgValue (it would be hoisted to the function entry, and if
      // we couldn't resolve it directly when examining the DbgValue intrinsic
      // in the first place we should not be more successful here). Unless we
      // have some test case that prove this to be correct we should avoid
      // calling EmitFuncArgumentDbgValue here.
      if (!EmitFuncArgumentDbgValue(V, Variable, Expr, DL,
                                    FuncArgumentDbgValueKind::Value, Val)) {
        LLVM_DEBUG(dbgs() << "Resolve dangling debug info for "
                          << printDDI(V, DDI) << "\n");
        LLVM_DEBUG(dbgs() << "  By mapping to:\n    "; Val.dump());
        // Increase the SDNodeOrder for the DbgValue here to make sure it is
        // inserted after the definition of Val when emitting the instructions
        // after ISel. An alternative could be to teach
        // ScheduleDAGSDNodes::EmitSchedule to delay the insertion properly.
        LLVM_DEBUG(if (ValSDNodeOrder > DbgSDNodeOrder) dbgs()
                   << "changing SDNodeOrder from " << DbgSDNodeOrder << " to "
                   << ValSDNodeOrder << "\n");
        SDV = getDbgValue(Val, Variable, Expr, DL,
                          std::max(DbgSDNodeOrder, ValSDNodeOrder));
        DAG.AddDbgValue(SDV, false);
      } else
        LLVM_DEBUG(dbgs() << "Resolved dangling debug info for "
                          << printDDI(V, DDI)
                          << " in EmitFuncArgumentDbgValue\n");
    } else {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << printDDI(V, DDI)
                        << "\n");
      auto Undef = UndefValue::get(V->getType());
      auto SDV =
          DAG.getConstantDbgValue(Variable, Expr, Undef, DL, DbgSDNodeOrder);
      DAG.AddDbgValue(SDV, false);
    }
  }
  DDIV.clear();
}

void SelectionDAGBuilder::salvageUnresolvedDbgValue(const Value *V,
                                                    DanglingDebugInfo &DDI) {
  // TODO: For the variadic implementation, instead of only checking the fail
  // state of `handleDebugValue`, we need know specifically which values were
  // invalid, so that we attempt to salvage only those values when processing
  // a DIArgList.
  const Value *OrigV = V;
  DILocalVariable *Var = DDI.getVariable();
  DIExpression *Expr = DDI.getExpression();
  DebugLoc DL = DDI.getDebugLoc();
  unsigned SDOrder = DDI.getSDNodeOrder();

  // Currently we consider only dbg.value intrinsics -- we tell the salvager
  // that DW_OP_stack_value is desired.
  bool StackValue = true;

  // Can this Value can be encoded without any further work?
  if (handleDebugValue(V, Var, Expr, DL, SDOrder, /*IsVariadic=*/false))
    return;

  // Attempt to salvage back through as many instructions as possible. Bail if
  // a non-instruction is seen, such as a constant expression or global
  // variable. FIXME: Further work could recover those too.
  while (isa<Instruction>(V)) {
    const Instruction &VAsInst = *cast<const Instruction>(V);
    // Temporary "0", awaiting real implementation.
    SmallVector<uint64_t, 16> Ops;
    SmallVector<Value *, 4> AdditionalValues;
    V = salvageDebugInfoImpl(const_cast<Instruction &>(VAsInst),
                             Expr->getNumLocationOperands(), Ops,
                             AdditionalValues);
    // If we cannot salvage any further, and haven't yet found a suitable debug
    // expression, bail out.
    if (!V)
      break;

    // TODO: If AdditionalValues isn't empty, then the salvage can only be
    // represented with a DBG_VALUE_LIST, so we give up. When we have support
    // here for variadic dbg_values, remove that condition.
    if (!AdditionalValues.empty())
      break;

    // New value and expr now represent this debuginfo.
    Expr = DIExpression::appendOpsToArg(Expr, Ops, 0, StackValue);

    // Some kind of simplification occurred: check whether the operand of the
    // salvaged debug expression can be encoded in this DAG.
    if (handleDebugValue(V, Var, Expr, DL, SDOrder, /*IsVariadic=*/false)) {
      LLVM_DEBUG(
          dbgs() << "Salvaged debug location info for:\n  " << *Var << "\n"
                 << *OrigV << "\nBy stripping back to:\n  " << *V << "\n");
      return;
    }
  }

  // This was the final opportunity to salvage this debug information, and it
  // couldn't be done. Place an undef DBG_VALUE at this location to terminate
  // any earlier variable location.
  assert(OrigV && "V shouldn't be null");
  auto *Undef = UndefValue::get(OrigV->getType());
  auto *SDV = DAG.getConstantDbgValue(Var, Expr, Undef, DL, SDNodeOrder);
  DAG.AddDbgValue(SDV, false);
  LLVM_DEBUG(dbgs() << "Dropping debug value info for:\n  "
                    << printDDI(OrigV, DDI) << "\n");
}

void SelectionDAGBuilder::handleKillDebugValue(DILocalVariable *Var,
                                               DIExpression *Expr,
                                               DebugLoc DbgLoc,
                                               unsigned Order) {
  Value *Poison = PoisonValue::get(Type::getInt1Ty(*Context));
  DIExpression *NewExpr =
      const_cast<DIExpression *>(DIExpression::convertToUndefExpression(Expr));
  handleDebugValue(Poison, Var, NewExpr, DbgLoc, Order,
                   /*IsVariadic*/ false);
}

bool SelectionDAGBuilder::handleDebugValue(ArrayRef<const Value *> Values,
                                           DILocalVariable *Var,
                                           DIExpression *Expr, DebugLoc DbgLoc,
                                           unsigned Order, bool IsVariadic) {
  if (Values.empty())
    return true;

  // Filter EntryValue locations out early.
  if (visitEntryValueDbgValue(Values, Var, Expr, DbgLoc))
    return true;

  SmallVector<SDDbgOperand> LocationOps;
  SmallVector<SDNode *> Dependencies;
  for (const Value *V : Values) {
    // Constant value.
    if (isa<ConstantInt>(V) || isa<ConstantFP>(V) || isa<UndefValue>(V) ||
        isa<ConstantPointerNull>(V)) {
      LocationOps.emplace_back(SDDbgOperand::fromConst(V));
      continue;
    }

    // Look through IntToPtr constants.
    if (auto *CE = dyn_cast<ConstantExpr>(V))
      if (CE->getOpcode() == Instruction::IntToPtr) {
        LocationOps.emplace_back(SDDbgOperand::fromConst(CE->getOperand(0)));
        continue;
      }

    // If the Value is a frame index, we can create a FrameIndex debug value
    // without relying on the DAG at all.
    if (const AllocaInst *AI = dyn_cast<AllocaInst>(V)) {
      auto SI = FuncInfo.StaticAllocaMap.find(AI);
      if (SI != FuncInfo.StaticAllocaMap.end()) {
        LocationOps.emplace_back(SDDbgOperand::fromFrameIdx(SI->second));
        continue;
      }
    }

    // Do not use getValue() in here; we don't want to generate code at
    // this point if it hasn't been done yet.
    SDValue N = NodeMap[V];
    if (!N.getNode() && isa<Argument>(V)) // Check unused arguments map.
      N = UnusedArgNodeMap[V];
    if (N.getNode()) {
      // Only emit func arg dbg value for non-variadic dbg.values for now.
      if (!IsVariadic &&
          EmitFuncArgumentDbgValue(V, Var, Expr, DbgLoc,
                                   FuncArgumentDbgValueKind::Value, N))
        return true;
      if (auto *FISDN = dyn_cast<FrameIndexSDNode>(N.getNode())) {
        // Construct a FrameIndexDbgValue for FrameIndexSDNodes so we can
        // describe stack slot locations.
        //
        // Consider "int x = 0; int *px = &x;". There are two kinds of
        // interesting debug values here after optimization:
        //
        //   dbg.value(i32* %px, !"int *px", !DIExpression()), and
        //   dbg.value(i32* %px, !"int x", !DIExpression(DW_OP_deref))
        //
        // Both describe the direct values of their associated variables.
        Dependencies.push_back(N.getNode());
        LocationOps.emplace_back(SDDbgOperand::fromFrameIdx(FISDN->getIndex()));
        continue;
      }
      LocationOps.emplace_back(
          SDDbgOperand::fromNode(N.getNode(), N.getResNo()));
      continue;
    }

    const TargetLowering &TLI = DAG.getTargetLoweringInfo();
    // Special rules apply for the first dbg.values of parameter variables in a
    // function. Identify them by the fact they reference Argument Values, that
    // they're parameters, and they are parameters of the current function. We
    // need to let them dangle until they get an SDNode.
    bool IsParamOfFunc =
        isa<Argument>(V) && Var->isParameter() && !DbgLoc.getInlinedAt();
    if (IsParamOfFunc)
      return false;

    // The value is not used in this block yet (or it would have an SDNode).
    // We still want the value to appear for the user if possible -- if it has
    // an associated VReg, we can refer to that instead.
    auto VMI = FuncInfo.ValueMap.find(V);
    if (VMI != FuncInfo.ValueMap.end()) {
      unsigned Reg = VMI->second;
      // If this is a PHI node, it may be split up into several MI PHI nodes
      // (in FunctionLoweringInfo::set).
      RegsForValue RFV(V->getContext(), TLI, DAG.getDataLayout(), Reg,
                       V->getType(), std::nullopt);
      if (RFV.occupiesMultipleRegs()) {
        // FIXME: We could potentially support variadic dbg_values here.
        if (IsVariadic)
          return false;
        unsigned Offset = 0;
        unsigned BitsToDescribe = 0;
        if (auto VarSize = Var->getSizeInBits())
          BitsToDescribe = *VarSize;
        if (auto Fragment = Expr->getFragmentInfo())
          BitsToDescribe = Fragment->SizeInBits;
        for (const auto &RegAndSize : RFV.getRegsAndSizes()) {
          // Bail out if all bits are described already.
          if (Offset >= BitsToDescribe)
            break;
          // TODO: handle scalable vectors.
          unsigned RegisterSize = RegAndSize.second;
          unsigned FragmentSize = (Offset + RegisterSize > BitsToDescribe)
                                      ? BitsToDescribe - Offset
                                      : RegisterSize;
          auto FragmentExpr = DIExpression::createFragmentExpression(
              Expr, Offset, FragmentSize);
          if (!FragmentExpr)
            continue;
          SDDbgValue *SDV = DAG.getVRegDbgValue(
              Var, *FragmentExpr, RegAndSize.first, false, DbgLoc, Order);
          DAG.AddDbgValue(SDV, false);
          Offset += RegisterSize;
        }
        return true;
      }
      // We can use simple vreg locations for variadic dbg_values as well.
      LocationOps.emplace_back(SDDbgOperand::fromVReg(Reg));
      continue;
    }
    // We failed to create a SDDbgOperand for V.
    return false;
  }

  // We have created a SDDbgOperand for each Value in Values.
  assert(!LocationOps.empty());
  SDDbgValue *SDV =
      DAG.getDbgValueList(Var, Expr, LocationOps, Dependencies,
                          /*IsIndirect=*/false, DbgLoc, Order, IsVariadic);
  DAG.AddDbgValue(SDV, /*isParameter=*/false);
  return true;
}

void SelectionDAGBuilder::resolveOrClearDbgInfo() {
  // Try to fixup any remaining dangling debug info -- and drop it if we can't.
  for (auto &Pair : DanglingDebugInfoMap)
    for (auto &DDI : Pair.second)
      salvageUnresolvedDbgValue(const_cast<Value *>(Pair.first), DDI);
  clearDanglingDebugInfo();
}

/// getCopyFromRegs - If there was virtual register allocated for the value V
/// emit CopyFromReg of the specified type Ty. Return empty SDValue() otherwise.
SDValue SelectionDAGBuilder::getCopyFromRegs(const Value *V, Type *Ty) {
  DenseMap<const Value *, Register>::iterator It = FuncInfo.ValueMap.find(V);
  SDValue Result;

  if (It != FuncInfo.ValueMap.end()) {
    Register InReg = It->second;

    RegsForValue RFV(*DAG.getContext(), DAG.getTargetLoweringInfo(),
                     DAG.getDataLayout(), InReg, Ty,
                     std::nullopt); // This is not an ABI copy.
    SDValue Chain = DAG.getEntryNode();
    Result = RFV.getCopyFromRegs(DAG, FuncInfo, getCurSDLoc(), Chain, nullptr,
                                 V);
    resolveDanglingDebugInfo(V, Result);
  }

  return Result;
}

/// getValue - Return an SDValue for the given Value.
SDValue SelectionDAGBuilder::getValue(const Value *V) {
  // If we already have an SDValue for this value, use it. It's important
  // to do this first, so that we don't create a CopyFromReg if we already
  // have a regular SDValue.
  SDValue &N = NodeMap[V];
  if (N.getNode()) return N;

  // If there's a virtual register allocated and initialized for this
  // value, use it.
  if (SDValue copyFromReg = getCopyFromRegs(V, V->getType()))
    return copyFromReg;

  // Otherwise create a new SDValue and remember it.
  SDValue Val = getValueImpl(V);
  NodeMap[V] = Val;
  resolveDanglingDebugInfo(V, Val);
  return Val;
}

/// getNonRegisterValue - Return an SDValue for the given Value, but
/// don't look in FuncInfo.ValueMap for a virtual register.
SDValue SelectionDAGBuilder::getNonRegisterValue(const Value *V) {
  // If we already have an SDValue for this value, use it.
  SDValue &N = NodeMap[V];
  if (N.getNode()) {
    if (isIntOrFPConstant(N)) {
      // Remove the debug location from the node as the node is about to be used
      // in a location which may differ from the original debug location.  This
      // is relevant to Constant and ConstantFP nodes because they can appear
      // as constant expressions inside PHI nodes.
      N->setDebugLoc(DebugLoc());
    }
    return N;
  }

  // Otherwise create a new SDValue and remember it.
  SDValue Val = getValueImpl(V);
  NodeMap[V] = Val;
  resolveDanglingDebugInfo(V, Val);
  return Val;
}

/// getValueImpl - Helper function for getValue and getNonRegisterValue.
/// Create an SDValue for the given value.
SDValue SelectionDAGBuilder::getValueImpl(const Value *V) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  if (const Constant *C = dyn_cast<Constant>(V)) {
    EVT VT = TLI.getValueType(DAG.getDataLayout(), V->getType(), true);

    if (const ConstantInt *CI = dyn_cast<ConstantInt>(C))
      return DAG.getConstant(*CI, getCurSDLoc(), VT);

    if (const GlobalValue *GV = dyn_cast<GlobalValue>(C))
      return DAG.getGlobalAddress(GV, getCurSDLoc(), VT);

    if (const ConstantPtrAuth *CPA = dyn_cast<ConstantPtrAuth>(C)) {
      return DAG.getNode(ISD::PtrAuthGlobalAddress, getCurSDLoc(), VT,
                         getValue(CPA->getPointer()), getValue(CPA->getKey()),
                         getValue(CPA->getAddrDiscriminator()),
                         getValue(CPA->getDiscriminator()));
    }

    if (isa<ConstantPointerNull>(C)) {
      unsigned AS = V->getType()->getPointerAddressSpace();
      return DAG.getConstant(0, getCurSDLoc(),
                             TLI.getPointerTy(DAG.getDataLayout(), AS));
    }

    if (match(C, m_VScale()))
      return DAG.getVScale(getCurSDLoc(), VT, APInt(VT.getSizeInBits(), 1));

    if (const ConstantFP *CFP = dyn_cast<ConstantFP>(C))
      return DAG.getConstantFP(*CFP, getCurSDLoc(), VT);

    if (isa<UndefValue>(C) && !V->getType()->isAggregateType())
      return DAG.getUNDEF(VT);

    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
      visit(CE->getOpcode(), *CE);
      SDValue N1 = NodeMap[V];
      assert(N1.getNode() && "visit didn't populate the NodeMap!");
      return N1;
    }

    if (isa<ConstantStruct>(C) || isa<ConstantArray>(C)) {
      SmallVector<SDValue, 4> Constants;
      for (const Use &U : C->operands()) {
        SDNode *Val = getValue(U).getNode();
        // If the operand is an empty aggregate, there are no values.
        if (!Val) continue;
        // Add each leaf value from the operand to the Constants list
        // to form a flattened list of all the values.
        for (unsigned i = 0, e = Val->getNumValues(); i != e; ++i)
          Constants.push_back(SDValue(Val, i));
      }

      return DAG.getMergeValues(Constants, getCurSDLoc());
    }

    if (const ConstantDataSequential *CDS =
          dyn_cast<ConstantDataSequential>(C)) {
      SmallVector<SDValue, 4> Ops;
      for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
        SDNode *Val = getValue(CDS->getElementAsConstant(i)).getNode();
        // Add each leaf value from the operand to the Constants list
        // to form a flattened list of all the values.
        for (unsigned i = 0, e = Val->getNumValues(); i != e; ++i)
          Ops.push_back(SDValue(Val, i));
      }

      if (isa<ArrayType>(CDS->getType()))
        return DAG.getMergeValues(Ops, getCurSDLoc());
      return NodeMap[V] = DAG.getBuildVector(VT, getCurSDLoc(), Ops);
    }

    if (C->getType()->isStructTy() || C->getType()->isArrayTy()) {
      assert((isa<ConstantAggregateZero>(C) || isa<UndefValue>(C)) &&
             "Unknown struct or array constant!");

      SmallVector<EVT, 4> ValueVTs;
      ComputeValueVTs(TLI, DAG.getDataLayout(), C->getType(), ValueVTs);
      unsigned NumElts = ValueVTs.size();
      if (NumElts == 0)
        return SDValue(); // empty struct
      SmallVector<SDValue, 4> Constants(NumElts);
      for (unsigned i = 0; i != NumElts; ++i) {
        EVT EltVT = ValueVTs[i];
        if (isa<UndefValue>(C))
          Constants[i] = DAG.getUNDEF(EltVT);
        else if (EltVT.isFloatingPoint())
          Constants[i] = DAG.getConstantFP(0, getCurSDLoc(), EltVT);
        else
          Constants[i] = DAG.getConstant(0, getCurSDLoc(), EltVT);
      }

      return DAG.getMergeValues(Constants, getCurSDLoc());
    }

    if (const BlockAddress *BA = dyn_cast<BlockAddress>(C))
      return DAG.getBlockAddress(BA, VT);

    if (const auto *Equiv = dyn_cast<DSOLocalEquivalent>(C))
      return getValue(Equiv->getGlobalValue());

    if (const auto *NC = dyn_cast<NoCFIValue>(C))
      return getValue(NC->getGlobalValue());

    if (VT == MVT::aarch64svcount) {
      assert(C->isNullValue() && "Can only zero this target type!");
      return DAG.getNode(ISD::BITCAST, getCurSDLoc(), VT,
                         DAG.getConstant(0, getCurSDLoc(), MVT::nxv16i1));
    }

    VectorType *VecTy = cast<VectorType>(V->getType());

    // Now that we know the number and type of the elements, get that number of
    // elements into the Ops array based on what kind of constant it is.
    if (const ConstantVector *CV = dyn_cast<ConstantVector>(C)) {
      SmallVector<SDValue, 16> Ops;
      unsigned NumElements = cast<FixedVectorType>(VecTy)->getNumElements();
      for (unsigned i = 0; i != NumElements; ++i)
        Ops.push_back(getValue(CV->getOperand(i)));

      return NodeMap[V] = DAG.getBuildVector(VT, getCurSDLoc(), Ops);
    }

    if (isa<ConstantAggregateZero>(C)) {
      EVT EltVT =
          TLI.getValueType(DAG.getDataLayout(), VecTy->getElementType());

      SDValue Op;
      if (EltVT.isFloatingPoint())
        Op = DAG.getConstantFP(0, getCurSDLoc(), EltVT);
      else
        Op = DAG.getConstant(0, getCurSDLoc(), EltVT);

      return NodeMap[V] = DAG.getSplat(VT, getCurSDLoc(), Op);
    }

    llvm_unreachable("Unknown vector constant");
  }

  // If this is a static alloca, generate it as the frameindex instead of
  // computation.
  if (const AllocaInst *AI = dyn_cast<AllocaInst>(V)) {
    DenseMap<const AllocaInst*, int>::iterator SI =
      FuncInfo.StaticAllocaMap.find(AI);
    if (SI != FuncInfo.StaticAllocaMap.end())
      return DAG.getFrameIndex(
          SI->second, TLI.getValueType(DAG.getDataLayout(), AI->getType()));
  }

  // If this is an instruction which fast-isel has deferred, select it now.
  if (const Instruction *Inst = dyn_cast<Instruction>(V)) {
    Register InReg = FuncInfo.InitializeRegForValue(Inst);

    RegsForValue RFV(*DAG.getContext(), TLI, DAG.getDataLayout(), InReg,
                     Inst->getType(), std::nullopt);
    SDValue Chain = DAG.getEntryNode();
    return RFV.getCopyFromRegs(DAG, FuncInfo, getCurSDLoc(), Chain, nullptr, V);
  }

  if (const MetadataAsValue *MD = dyn_cast<MetadataAsValue>(V))
    return DAG.getMDNode(cast<MDNode>(MD->getMetadata()));

  if (const auto *BB = dyn_cast<BasicBlock>(V))
    return DAG.getBasicBlock(FuncInfo.MBBMap[BB]);

  llvm_unreachable("Can't get register for value!");
}

void SelectionDAGBuilder::visitCatchPad(const CatchPadInst &I) {
  auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  bool IsMSVCCXX = Pers == EHPersonality::MSVC_CXX;
  bool IsCoreCLR = Pers == EHPersonality::CoreCLR;
  bool IsSEH = isAsynchronousEHPersonality(Pers);
  MachineBasicBlock *CatchPadMBB = FuncInfo.MBB;
  if (!IsSEH)
    CatchPadMBB->setIsEHScopeEntry();
  // In MSVC C++ and CoreCLR, catchblocks are funclets and need prologues.
  if (IsMSVCCXX || IsCoreCLR)
    CatchPadMBB->setIsEHFuncletEntry();
}

void SelectionDAGBuilder::visitCatchRet(const CatchReturnInst &I) {
  // Update machine-CFG edge.
  MachineBasicBlock *TargetMBB = FuncInfo.MBBMap[I.getSuccessor()];
  FuncInfo.MBB->addSuccessor(TargetMBB);
  TargetMBB->setIsEHCatchretTarget(true);
  DAG.getMachineFunction().setHasEHCatchret(true);

  auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  bool IsSEH = isAsynchronousEHPersonality(Pers);
  if (IsSEH) {
    // If this is not a fall-through branch or optimizations are switched off,
    // emit the branch.
    if (TargetMBB != NextBlock(FuncInfo.MBB) ||
        TM.getOptLevel() == CodeGenOptLevel::None)
      DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(), MVT::Other,
                              getControlRoot(), DAG.getBasicBlock(TargetMBB)));
    return;
  }

  // Figure out the funclet membership for the catchret's successor.
  // This will be used by the FuncletLayout pass to determine how to order the
  // BB's.
  // A 'catchret' returns to the outer scope's color.
  Value *ParentPad = I.getCatchSwitchParentPad();
  const BasicBlock *SuccessorColor;
  if (isa<ConstantTokenNone>(ParentPad))
    SuccessorColor = &FuncInfo.Fn->getEntryBlock();
  else
    SuccessorColor = cast<Instruction>(ParentPad)->getParent();
  assert(SuccessorColor && "No parent funclet for catchret!");
  MachineBasicBlock *SuccessorColorMBB = FuncInfo.MBBMap[SuccessorColor];
  assert(SuccessorColorMBB && "No MBB for SuccessorColor!");

  // Create the terminator node.
  SDValue Ret = DAG.getNode(ISD::CATCHRET, getCurSDLoc(), MVT::Other,
                            getControlRoot(), DAG.getBasicBlock(TargetMBB),
                            DAG.getBasicBlock(SuccessorColorMBB));
  DAG.setRoot(Ret);
}

void SelectionDAGBuilder::visitCleanupPad(const CleanupPadInst &CPI) {
  // Don't emit any special code for the cleanuppad instruction. It just marks
  // the start of an EH scope/funclet.
  FuncInfo.MBB->setIsEHScopeEntry();
  auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  if (Pers != EHPersonality::Wasm_CXX) {
    FuncInfo.MBB->setIsEHFuncletEntry();
    FuncInfo.MBB->setIsCleanupFuncletEntry();
  }
}

// In wasm EH, even though a catchpad may not catch an exception if a tag does
// not match, it is OK to add only the first unwind destination catchpad to the
// successors, because there will be at least one invoke instruction within the
// catch scope that points to the next unwind destination, if one exists, so
// CFGSort cannot mess up with BB sorting order.
// (All catchpads with 'catch (type)' clauses have a 'llvm.rethrow' intrinsic
// call within them, and catchpads only consisting of 'catch (...)' have a
// '__cxa_end_catch' call within them, both of which generate invokes in case
// the next unwind destination exists, i.e., the next unwind destination is not
// the caller.)
//
// Having at most one EH pad successor is also simpler and helps later
// transformations.
//
// For example,
// current:
//   invoke void @foo to ... unwind label %catch.dispatch
// catch.dispatch:
//   %0 = catchswitch within ... [label %catch.start] unwind label %next
// catch.start:
//   ...
//   ... in this BB or some other child BB dominated by this BB there will be an
//   invoke that points to 'next' BB as an unwind destination
//
// next: ; We don't need to add this to 'current' BB's successor
//   ...
static void findWasmUnwindDestinations(
    FunctionLoweringInfo &FuncInfo, const BasicBlock *EHPadBB,
    BranchProbability Prob,
    SmallVectorImpl<std::pair<MachineBasicBlock *, BranchProbability>>
        &UnwindDests) {
  while (EHPadBB) {
    const Instruction *Pad = EHPadBB->getFirstNonPHI();
    if (isa<CleanupPadInst>(Pad)) {
      // Stop on cleanup pads.
      UnwindDests.emplace_back(FuncInfo.MBBMap[EHPadBB], Prob);
      UnwindDests.back().first->setIsEHScopeEntry();
      break;
    } else if (const auto *CatchSwitch = dyn_cast<CatchSwitchInst>(Pad)) {
      // Add the catchpad handlers to the possible destinations. We don't
      // continue to the unwind destination of the catchswitch for wasm.
      for (const BasicBlock *CatchPadBB : CatchSwitch->handlers()) {
        UnwindDests.emplace_back(FuncInfo.MBBMap[CatchPadBB], Prob);
        UnwindDests.back().first->setIsEHScopeEntry();
      }
      break;
    } else {
      continue;
    }
  }
}

/// When an invoke or a cleanupret unwinds to the next EH pad, there are
/// many places it could ultimately go. In the IR, we have a single unwind
/// destination, but in the machine CFG, we enumerate all the possible blocks.
/// This function skips over imaginary basic blocks that hold catchswitch
/// instructions, and finds all the "real" machine
/// basic block destinations. As those destinations may not be successors of
/// EHPadBB, here we also calculate the edge probability to those destinations.
/// The passed-in Prob is the edge probability to EHPadBB.
static void findUnwindDestinations(
    FunctionLoweringInfo &FuncInfo, const BasicBlock *EHPadBB,
    BranchProbability Prob,
    SmallVectorImpl<std::pair<MachineBasicBlock *, BranchProbability>>
        &UnwindDests) {
  EHPersonality Personality =
    classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  bool IsMSVCCXX = Personality == EHPersonality::MSVC_CXX;
  bool IsCoreCLR = Personality == EHPersonality::CoreCLR;
  bool IsWasmCXX = Personality == EHPersonality::Wasm_CXX;
  bool IsSEH = isAsynchronousEHPersonality(Personality);

  if (IsWasmCXX) {
    findWasmUnwindDestinations(FuncInfo, EHPadBB, Prob, UnwindDests);
    assert(UnwindDests.size() <= 1 &&
           "There should be at most one unwind destination for wasm");
    return;
  }

  while (EHPadBB) {
    const Instruction *Pad = EHPadBB->getFirstNonPHI();
    BasicBlock *NewEHPadBB = nullptr;
    if (isa<LandingPadInst>(Pad)) {
      // Stop on landingpads. They are not funclets.
      UnwindDests.emplace_back(FuncInfo.MBBMap[EHPadBB], Prob);
      break;
    } else if (isa<CleanupPadInst>(Pad)) {
      // Stop on cleanup pads. Cleanups are always funclet entries for all known
      // personalities.
      UnwindDests.emplace_back(FuncInfo.MBBMap[EHPadBB], Prob);
      UnwindDests.back().first->setIsEHScopeEntry();
      UnwindDests.back().first->setIsEHFuncletEntry();
      break;
    } else if (const auto *CatchSwitch = dyn_cast<CatchSwitchInst>(Pad)) {
      // Add the catchpad handlers to the possible destinations.
      for (const BasicBlock *CatchPadBB : CatchSwitch->handlers()) {
        UnwindDests.emplace_back(FuncInfo.MBBMap[CatchPadBB], Prob);
        // For MSVC++ and the CLR, catchblocks are funclets and need prologues.
        if (IsMSVCCXX || IsCoreCLR)
          UnwindDests.back().first->setIsEHFuncletEntry();
        if (!IsSEH)
          UnwindDests.back().first->setIsEHScopeEntry();
      }
      NewEHPadBB = CatchSwitch->getUnwindDest();
    } else {
      continue;
    }

    BranchProbabilityInfo *BPI = FuncInfo.BPI;
    if (BPI && NewEHPadBB)
      Prob *= BPI->getEdgeProbability(EHPadBB, NewEHPadBB);
    EHPadBB = NewEHPadBB;
  }
}

void SelectionDAGBuilder::visitCleanupRet(const CleanupReturnInst &I) {
  // Update successor info.
  SmallVector<std::pair<MachineBasicBlock *, BranchProbability>, 1> UnwindDests;
  auto UnwindDest = I.getUnwindDest();
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  BranchProbability UnwindDestProb =
      (BPI && UnwindDest)
          ? BPI->getEdgeProbability(FuncInfo.MBB->getBasicBlock(), UnwindDest)
          : BranchProbability::getZero();
  findUnwindDestinations(FuncInfo, UnwindDest, UnwindDestProb, UnwindDests);
  for (auto &UnwindDest : UnwindDests) {
    UnwindDest.first->setIsEHPad();
    addSuccessorWithProb(FuncInfo.MBB, UnwindDest.first, UnwindDest.second);
  }
  FuncInfo.MBB->normalizeSuccProbs();

  // Create the terminator node.
  MachineBasicBlock *CleanupPadMBB =
      FuncInfo.MBBMap[I.getCleanupPad()->getParent()];
  SDValue Ret = DAG.getNode(ISD::CLEANUPRET, getCurSDLoc(), MVT::Other,
                            getControlRoot(), DAG.getBasicBlock(CleanupPadMBB));
  DAG.setRoot(Ret);
}

void SelectionDAGBuilder::visitCatchSwitch(const CatchSwitchInst &CSI) {
  report_fatal_error("visitCatchSwitch not yet implemented!");
}

void SelectionDAGBuilder::visitRet(const ReturnInst &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  auto &DL = DAG.getDataLayout();
  SDValue Chain = getControlRoot();
  SmallVector<ISD::OutputArg, 8> Outs;
  SmallVector<SDValue, 8> OutVals;

  // Calls to @llvm.experimental.deoptimize don't generate a return value, so
  // lower
  //
  //   %val = call <ty> @llvm.experimental.deoptimize()
  //   ret <ty> %val
  //
  // differently.
  if (I.getParent()->getTerminatingDeoptimizeCall()) {
    LowerDeoptimizingReturn();
    return;
  }

  if (!FuncInfo.CanLowerReturn) {
    unsigned DemoteReg = FuncInfo.DemoteRegister;
    const Function *F = I.getParent()->getParent();

    // Emit a store of the return value through the virtual register.
    // Leave Outs empty so that LowerReturn won't try to load return
    // registers the usual way.
    SmallVector<EVT, 1> PtrValueVTs;
    ComputeValueVTs(TLI, DL,
                    PointerType::get(F->getContext(),
                                     DAG.getDataLayout().getAllocaAddrSpace()),
                    PtrValueVTs);

    SDValue RetPtr =
        DAG.getCopyFromReg(Chain, getCurSDLoc(), DemoteReg, PtrValueVTs[0]);
    SDValue RetOp = getValue(I.getOperand(0));

    SmallVector<EVT, 4> ValueVTs, MemVTs;
    SmallVector<uint64_t, 4> Offsets;
    ComputeValueVTs(TLI, DL, I.getOperand(0)->getType(), ValueVTs, &MemVTs,
                    &Offsets, 0);
    unsigned NumValues = ValueVTs.size();

    SmallVector<SDValue, 4> Chains(NumValues);
    Align BaseAlign = DL.getPrefTypeAlign(I.getOperand(0)->getType());
    for (unsigned i = 0; i != NumValues; ++i) {
      // An aggregate return value cannot wrap around the address space, so
      // offsets to its parts don't wrap either.
      SDValue Ptr = DAG.getObjectPtrOffset(getCurSDLoc(), RetPtr,
                                           TypeSize::getFixed(Offsets[i]));

      SDValue Val = RetOp.getValue(RetOp.getResNo() + i);
      if (MemVTs[i] != ValueVTs[i])
        Val = DAG.getPtrExtOrTrunc(Val, getCurSDLoc(), MemVTs[i]);
      Chains[i] = DAG.getStore(
          Chain, getCurSDLoc(), Val,
          // FIXME: better loc info would be nice.
          Ptr, MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()),
          commonAlignment(BaseAlign, Offsets[i]));
    }

    Chain = DAG.getNode(ISD::TokenFactor, getCurSDLoc(),
                        MVT::Other, Chains);
  } else if (I.getNumOperands() != 0) {
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(TLI, DL, I.getOperand(0)->getType(), ValueVTs);
    unsigned NumValues = ValueVTs.size();
    if (NumValues) {
      SDValue RetOp = getValue(I.getOperand(0));

      const Function *F = I.getParent()->getParent();

      bool NeedsRegBlock = TLI.functionArgumentNeedsConsecutiveRegisters(
          I.getOperand(0)->getType(), F->getCallingConv(),
          /*IsVarArg*/ false, DL);

      ISD::NodeType ExtendKind = ISD::ANY_EXTEND;
      if (F->getAttributes().hasRetAttr(Attribute::SExt))
        ExtendKind = ISD::SIGN_EXTEND;
      else if (F->getAttributes().hasRetAttr(Attribute::ZExt))
        ExtendKind = ISD::ZERO_EXTEND;

      LLVMContext &Context = F->getContext();
      bool RetInReg = F->getAttributes().hasRetAttr(Attribute::InReg);

      for (unsigned j = 0; j != NumValues; ++j) {
        EVT VT = ValueVTs[j];

        if (ExtendKind != ISD::ANY_EXTEND && VT.isInteger())
          VT = TLI.getTypeForExtReturn(Context, VT, ExtendKind);

        CallingConv::ID CC = F->getCallingConv();

        unsigned NumParts = TLI.getNumRegistersForCallingConv(Context, CC, VT);
        MVT PartVT = TLI.getRegisterTypeForCallingConv(Context, CC, VT);
        SmallVector<SDValue, 4> Parts(NumParts);
        getCopyToParts(DAG, getCurSDLoc(),
                       SDValue(RetOp.getNode(), RetOp.getResNo() + j),
                       &Parts[0], NumParts, PartVT, &I, CC, ExtendKind);

        // 'inreg' on function refers to return value
        ISD::ArgFlagsTy Flags = ISD::ArgFlagsTy();
        if (RetInReg)
          Flags.setInReg();

        if (I.getOperand(0)->getType()->isPointerTy()) {
          Flags.setPointer();
          Flags.setPointerAddrSpace(
              cast<PointerType>(I.getOperand(0)->getType())->getAddressSpace());
        }

        if (NeedsRegBlock) {
          Flags.setInConsecutiveRegs();
          if (j == NumValues - 1)
            Flags.setInConsecutiveRegsLast();
        }

        // Propagate extension type if any
        if (ExtendKind == ISD::SIGN_EXTEND)
          Flags.setSExt();
        else if (ExtendKind == ISD::ZERO_EXTEND)
          Flags.setZExt();

        for (unsigned i = 0; i < NumParts; ++i) {
          Outs.push_back(ISD::OutputArg(Flags,
                                        Parts[i].getValueType().getSimpleVT(),
                                        VT, /*isfixed=*/true, 0, 0));
          OutVals.push_back(Parts[i]);
        }
      }
    }
  }

  // Push in swifterror virtual register as the last element of Outs. This makes
  // sure swifterror virtual register will be returned in the swifterror
  // physical register.
  const Function *F = I.getParent()->getParent();
  if (TLI.supportSwiftError() &&
      F->getAttributes().hasAttrSomewhere(Attribute::SwiftError)) {
    assert(SwiftError.getFunctionArg() && "Need a swift error argument");
    ISD::ArgFlagsTy Flags = ISD::ArgFlagsTy();
    Flags.setSwiftError();
    Outs.push_back(ISD::OutputArg(
        Flags, /*vt=*/TLI.getPointerTy(DL), /*argvt=*/EVT(TLI.getPointerTy(DL)),
        /*isfixed=*/true, /*origidx=*/1, /*partOffs=*/0));
    // Create SDNode for the swifterror virtual register.
    OutVals.push_back(
        DAG.getRegister(SwiftError.getOrCreateVRegUseAt(
                            &I, FuncInfo.MBB, SwiftError.getFunctionArg()),
                        EVT(TLI.getPointerTy(DL))));
  }

  bool isVarArg = DAG.getMachineFunction().getFunction().isVarArg();
  CallingConv::ID CallConv =
    DAG.getMachineFunction().getFunction().getCallingConv();
  Chain = DAG.getTargetLoweringInfo().LowerReturn(
      Chain, CallConv, isVarArg, Outs, OutVals, getCurSDLoc(), DAG);

  // Verify that the target's LowerReturn behaved as expected.
  assert(Chain.getNode() && Chain.getValueType() == MVT::Other &&
         "LowerReturn didn't return a valid chain!");

  // Update the DAG with the new chain value resulting from return lowering.
  DAG.setRoot(Chain);
}

/// CopyToExportRegsIfNeeded - If the given value has virtual registers
/// created for it, emit nodes to copy the value into the virtual
/// registers.
void SelectionDAGBuilder::CopyToExportRegsIfNeeded(const Value *V) {
  // Skip empty types
  if (V->getType()->isEmptyTy())
    return;

  DenseMap<const Value *, Register>::iterator VMI = FuncInfo.ValueMap.find(V);
  if (VMI != FuncInfo.ValueMap.end()) {
    assert((!V->use_empty() || isa<CallBrInst>(V)) &&
           "Unused value assigned virtual registers!");
    CopyValueToVirtualRegister(V, VMI->second);
  }
}

/// ExportFromCurrentBlock - If this condition isn't known to be exported from
/// the current basic block, add it to ValueMap now so that we'll get a
/// CopyTo/FromReg.
void SelectionDAGBuilder::ExportFromCurrentBlock(const Value *V) {
  // No need to export constants.
  if (!isa<Instruction>(V) && !isa<Argument>(V)) return;

  // Already exported?
  if (FuncInfo.isExportedInst(V)) return;

  Register Reg = FuncInfo.InitializeRegForValue(V);
  CopyValueToVirtualRegister(V, Reg);
}

bool SelectionDAGBuilder::isExportableFromCurrentBlock(const Value *V,
                                                     const BasicBlock *FromBB) {
  // The operands of the setcc have to be in this block.  We don't know
  // how to export them from some other block.
  if (const Instruction *VI = dyn_cast<Instruction>(V)) {
    // Can export from current BB.
    if (VI->getParent() == FromBB)
      return true;

    // Is already exported, noop.
    return FuncInfo.isExportedInst(V);
  }

  // If this is an argument, we can export it if the BB is the entry block or
  // if it is already exported.
  if (isa<Argument>(V)) {
    if (FromBB->isEntryBlock())
      return true;

    // Otherwise, can only export this if it is already exported.
    return FuncInfo.isExportedInst(V);
  }

  // Otherwise, constants can always be exported.
  return true;
}

/// Return branch probability calculated by BranchProbabilityInfo for IR blocks.
BranchProbability
SelectionDAGBuilder::getEdgeProbability(const MachineBasicBlock *Src,
                                        const MachineBasicBlock *Dst) const {
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  const BasicBlock *SrcBB = Src->getBasicBlock();
  const BasicBlock *DstBB = Dst->getBasicBlock();
  if (!BPI) {
    // If BPI is not available, set the default probability as 1 / N, where N is
    // the number of successors.
    auto SuccSize = std::max<uint32_t>(succ_size(SrcBB), 1);
    return BranchProbability(1, SuccSize);
  }
  return BPI->getEdgeProbability(SrcBB, DstBB);
}

void SelectionDAGBuilder::addSuccessorWithProb(MachineBasicBlock *Src,
                                               MachineBasicBlock *Dst,
                                               BranchProbability Prob) {
  if (!FuncInfo.BPI)
    Src->addSuccessorWithoutProb(Dst);
  else {
    if (Prob.isUnknown())
      Prob = getEdgeProbability(Src, Dst);
    Src->addSuccessor(Dst, Prob);
  }
}

static bool InBlock(const Value *V, const BasicBlock *BB) {
  if (const Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent() == BB;
  return true;
}

/// EmitBranchForMergedCondition - Helper method for FindMergedConditions.
/// This function emits a branch and is used at the leaves of an OR or an
/// AND operator tree.
void
SelectionDAGBuilder::EmitBranchForMergedCondition(const Value *Cond,
                                                  MachineBasicBlock *TBB,
                                                  MachineBasicBlock *FBB,
                                                  MachineBasicBlock *CurBB,
                                                  MachineBasicBlock *SwitchBB,
                                                  BranchProbability TProb,
                                                  BranchProbability FProb,
                                                  bool InvertCond) {
  const BasicBlock *BB = CurBB->getBasicBlock();

  // If the leaf of the tree is a comparison, merge the condition into
  // the caseblock.
  if (const CmpInst *BOp = dyn_cast<CmpInst>(Cond)) {
    // The operands of the cmp have to be in this block.  We don't know
    // how to export them from some other block.  If this is the first block
    // of the sequence, no exporting is needed.
    if (CurBB == SwitchBB ||
        (isExportableFromCurrentBlock(BOp->getOperand(0), BB) &&
         isExportableFromCurrentBlock(BOp->getOperand(1), BB))) {
      ISD::CondCode Condition;
      if (const ICmpInst *IC = dyn_cast<ICmpInst>(Cond)) {
        ICmpInst::Predicate Pred =
            InvertCond ? IC->getInversePredicate() : IC->getPredicate();
        Condition = getICmpCondCode(Pred);
      } else {
        const FCmpInst *FC = cast<FCmpInst>(Cond);
        FCmpInst::Predicate Pred =
            InvertCond ? FC->getInversePredicate() : FC->getPredicate();
        Condition = getFCmpCondCode(Pred);
        if (TM.Options.NoNaNsFPMath)
          Condition = getFCmpCodeWithoutNaN(Condition);
      }

      CaseBlock CB(Condition, BOp->getOperand(0), BOp->getOperand(1), nullptr,
                   TBB, FBB, CurBB, getCurSDLoc(), TProb, FProb);
      SL->SwitchCases.push_back(CB);
      return;
    }
  }

  // Create a CaseBlock record representing this branch.
  ISD::CondCode Opc = InvertCond ? ISD::SETNE : ISD::SETEQ;
  CaseBlock CB(Opc, Cond, ConstantInt::getTrue(*DAG.getContext()),
               nullptr, TBB, FBB, CurBB, getCurSDLoc(), TProb, FProb);
  SL->SwitchCases.push_back(CB);
}

// Collect dependencies on V recursively. This is used for the cost analysis in
// `shouldKeepJumpConditionsTogether`.
static bool collectInstructionDeps(
    SmallMapVector<const Instruction *, bool, 8> *Deps, const Value *V,
    SmallMapVector<const Instruction *, bool, 8> *Necessary = nullptr,
    unsigned Depth = 0) {
  // Return false if we have an incomplete count.
  if (Depth >= SelectionDAG::MaxRecursionDepth)
    return false;

  auto *I = dyn_cast<Instruction>(V);
  if (I == nullptr)
    return true;

  if (Necessary != nullptr) {
    // This instruction is necessary for the other side of the condition so
    // don't count it.
    if (Necessary->contains(I))
      return true;
  }

  // Already added this dep.
  if (!Deps->try_emplace(I, false).second)
    return true;

  for (unsigned OpIdx = 0, E = I->getNumOperands(); OpIdx < E; ++OpIdx)
    if (!collectInstructionDeps(Deps, I->getOperand(OpIdx), Necessary,
                                Depth + 1))
      return false;
  return true;
}

bool SelectionDAGBuilder::shouldKeepJumpConditionsTogether(
    const FunctionLoweringInfo &FuncInfo, const BranchInst &I,
    Instruction::BinaryOps Opc, const Value *Lhs, const Value *Rhs,
    TargetLoweringBase::CondMergingParams Params) const {
  if (I.getNumSuccessors() != 2)
    return false;

  if (!I.isConditional())
    return false;

  if (Params.BaseCost < 0)
    return false;

  // Baseline cost.
  InstructionCost CostThresh = Params.BaseCost;

  BranchProbabilityInfo *BPI = nullptr;
  if (Params.LikelyBias || Params.UnlikelyBias)
    BPI = FuncInfo.BPI;
  if (BPI != nullptr) {
    // See if we are either likely to get an early out or compute both lhs/rhs
    // of the condition.
    BasicBlock *IfFalse = I.getSuccessor(0);
    BasicBlock *IfTrue = I.getSuccessor(1);

    std::optional<bool> Likely;
    if (BPI->isEdgeHot(I.getParent(), IfTrue))
      Likely = true;
    else if (BPI->isEdgeHot(I.getParent(), IfFalse))
      Likely = false;

    if (Likely) {
      if (Opc == (*Likely ? Instruction::And : Instruction::Or))
        // Its likely we will have to compute both lhs and rhs of condition
        CostThresh += Params.LikelyBias;
      else {
        if (Params.UnlikelyBias < 0)
          return false;
        // Its likely we will get an early out.
        CostThresh -= Params.UnlikelyBias;
      }
    }
  }

  if (CostThresh <= 0)
    return false;

  // Collect "all" instructions that lhs condition is dependent on.
  // Use map for stable iteration (to avoid non-determanism of iteration of
  // SmallPtrSet). The `bool` value is just a dummy.
  SmallMapVector<const Instruction *, bool, 8> LhsDeps, RhsDeps;
  collectInstructionDeps(&LhsDeps, Lhs);
  // Collect "all" instructions that rhs condition is dependent on AND are
  // dependencies of lhs. This gives us an estimate on which instructions we
  // stand to save by splitting the condition.
  if (!collectInstructionDeps(&RhsDeps, Rhs, &LhsDeps))
    return false;
  // Add the compare instruction itself unless its a dependency on the LHS.
  if (const auto *RhsI = dyn_cast<Instruction>(Rhs))
    if (!LhsDeps.contains(RhsI))
      RhsDeps.try_emplace(RhsI, false);

  const auto &TLI = DAG.getTargetLoweringInfo();
  const auto &TTI =
      TLI.getTargetMachine().getTargetTransformInfo(*I.getFunction());

  InstructionCost CostOfIncluding = 0;
  // See if this instruction will need to computed independently of whether RHS
  // is.
  Value *BrCond = I.getCondition();
  auto ShouldCountInsn = [&RhsDeps, &BrCond](const Instruction *Ins) {
    for (const auto *U : Ins->users()) {
      // If user is independent of RHS calculation we don't need to count it.
      if (auto *UIns = dyn_cast<Instruction>(U))
        if (UIns != BrCond && !RhsDeps.contains(UIns))
          return false;
    }
    return true;
  };

  // Prune instructions from RHS Deps that are dependencies of unrelated
  // instructions. The value (SelectionDAG::MaxRecursionDepth) is fairly
  // arbitrary and just meant to cap the how much time we spend in the pruning
  // loop. Its highly unlikely to come into affect.
  const unsigned MaxPruneIters = SelectionDAG::MaxRecursionDepth;
  // Stop after a certain point. No incorrectness from including too many
  // instructions.
  for (unsigned PruneIters = 0; PruneIters < MaxPruneIters; ++PruneIters) {
    const Instruction *ToDrop = nullptr;
    for (const auto &InsPair : RhsDeps) {
      if (!ShouldCountInsn(InsPair.first)) {
        ToDrop = InsPair.first;
        break;
      }
    }
    if (ToDrop == nullptr)
      break;
    RhsDeps.erase(ToDrop);
  }

  for (const auto &InsPair : RhsDeps) {
    // Finally accumulate latency that we can only attribute to computing the
    // RHS condition. Use latency because we are essentially trying to calculate
    // the cost of the dependency chain.
    // Possible TODO: We could try to estimate ILP and make this more precise.
    CostOfIncluding +=
        TTI.getInstructionCost(InsPair.first, TargetTransformInfo::TCK_Latency);

    if (CostOfIncluding > CostThresh)
      return false;
  }
  return true;
}

void SelectionDAGBuilder::FindMergedConditions(const Value *Cond,
                                               MachineBasicBlock *TBB,
                                               MachineBasicBlock *FBB,
                                               MachineBasicBlock *CurBB,
                                               MachineBasicBlock *SwitchBB,
                                               Instruction::BinaryOps Opc,
                                               BranchProbability TProb,
                                               BranchProbability FProb,
                                               bool InvertCond) {
  // Skip over not part of the tree and remember to invert op and operands at
  // next level.
  Value *NotCond;
  if (match(Cond, m_OneUse(m_Not(m_Value(NotCond)))) &&
      InBlock(NotCond, CurBB->getBasicBlock())) {
    FindMergedConditions(NotCond, TBB, FBB, CurBB, SwitchBB, Opc, TProb, FProb,
                         !InvertCond);
    return;
  }

  const Instruction *BOp = dyn_cast<Instruction>(Cond);
  const Value *BOpOp0, *BOpOp1;
  // Compute the effective opcode for Cond, taking into account whether it needs
  // to be inverted, e.g.
  //   and (not (or A, B)), C
  // gets lowered as
  //   and (and (not A, not B), C)
  Instruction::BinaryOps BOpc = (Instruction::BinaryOps)0;
  if (BOp) {
    BOpc = match(BOp, m_LogicalAnd(m_Value(BOpOp0), m_Value(BOpOp1)))
               ? Instruction::And
               : (match(BOp, m_LogicalOr(m_Value(BOpOp0), m_Value(BOpOp1)))
                      ? Instruction::Or
                      : (Instruction::BinaryOps)0);
    if (InvertCond) {
      if (BOpc == Instruction::And)
        BOpc = Instruction::Or;
      else if (BOpc == Instruction::Or)
        BOpc = Instruction::And;
    }
  }

  // If this node is not part of the or/and tree, emit it as a branch.
  // Note that all nodes in the tree should have same opcode.
  bool BOpIsInOrAndTree = BOpc && BOpc == Opc && BOp->hasOneUse();
  if (!BOpIsInOrAndTree || BOp->getParent() != CurBB->getBasicBlock() ||
      !InBlock(BOpOp0, CurBB->getBasicBlock()) ||
      !InBlock(BOpOp1, CurBB->getBasicBlock())) {
    EmitBranchForMergedCondition(Cond, TBB, FBB, CurBB, SwitchBB,
                                 TProb, FProb, InvertCond);
    return;
  }

  //  Create TmpBB after CurBB.
  MachineFunction::iterator BBI(CurBB);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineBasicBlock *TmpBB = MF.CreateMachineBasicBlock(CurBB->getBasicBlock());
  CurBB->getParent()->insert(++BBI, TmpBB);

  if (Opc == Instruction::Or) {
    // Codegen X | Y as:
    // BB1:
    //   jmp_if_X TBB
    //   jmp TmpBB
    // TmpBB:
    //   jmp_if_Y TBB
    //   jmp FBB
    //

    // We have flexibility in setting Prob for BB1 and Prob for TmpBB.
    // The requirement is that
    //   TrueProb for BB1 + (FalseProb for BB1 * TrueProb for TmpBB)
    //     = TrueProb for original BB.
    // Assuming the original probabilities are A and B, one choice is to set
    // BB1's probabilities to A/2 and A/2+B, and set TmpBB's probabilities to
    // A/(1+B) and 2B/(1+B). This choice assumes that
    //   TrueProb for BB1 == FalseProb for BB1 * TrueProb for TmpBB.
    // Another choice is to assume TrueProb for BB1 equals to TrueProb for
    // TmpBB, but the math is more complicated.

    auto NewTrueProb = TProb / 2;
    auto NewFalseProb = TProb / 2 + FProb;
    // Emit the LHS condition.
    FindMergedConditions(BOpOp0, TBB, TmpBB, CurBB, SwitchBB, Opc, NewTrueProb,
                         NewFalseProb, InvertCond);

    // Normalize A/2 and B to get A/(1+B) and 2B/(1+B).
    SmallVector<BranchProbability, 2> Probs{TProb / 2, FProb};
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
    // Emit the RHS condition into TmpBB.
    FindMergedConditions(BOpOp1, TBB, FBB, TmpBB, SwitchBB, Opc, Probs[0],
                         Probs[1], InvertCond);
  } else {
    assert(Opc == Instruction::And && "Unknown merge op!");
    // Codegen X & Y as:
    // BB1:
    //   jmp_if_X TmpBB
    //   jmp FBB
    // TmpBB:
    //   jmp_if_Y TBB
    //   jmp FBB
    //
    //  This requires creation of TmpBB after CurBB.

    // We have flexibility in setting Prob for BB1 and Prob for TmpBB.
    // The requirement is that
    //   FalseProb for BB1 + (TrueProb for BB1 * FalseProb for TmpBB)
    //     = FalseProb for original BB.
    // Assuming the original probabilities are A and B, one choice is to set
    // BB1's probabilities to A+B/2 and B/2, and set TmpBB's probabilities to
    // 2A/(1+A) and B/(1+A). This choice assumes that FalseProb for BB1 ==
    // TrueProb for BB1 * FalseProb for TmpBB.

    auto NewTrueProb = TProb + FProb / 2;
    auto NewFalseProb = FProb / 2;
    // Emit the LHS condition.
    FindMergedConditions(BOpOp0, TmpBB, FBB, CurBB, SwitchBB, Opc, NewTrueProb,
                         NewFalseProb, InvertCond);

    // Normalize A and B/2 to get 2A/(1+A) and B/(1+A).
    SmallVector<BranchProbability, 2> Probs{TProb, FProb / 2};
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
    // Emit the RHS condition into TmpBB.
    FindMergedConditions(BOpOp1, TBB, FBB, TmpBB, SwitchBB, Opc, Probs[0],
                         Probs[1], InvertCond);
  }
}

/// If the set of cases should be emitted as a series of branches, return true.
/// If we should emit this as a bunch of and/or'd together conditions, return
/// false.
bool
SelectionDAGBuilder::ShouldEmitAsBranches(const std::vector<CaseBlock> &Cases) {
  if (Cases.size() != 2) return true;

  // If this is two comparisons of the same values or'd or and'd together, they
  // will get folded into a single comparison, so don't emit two blocks.
  if ((Cases[0].CmpLHS == Cases[1].CmpLHS &&
       Cases[0].CmpRHS == Cases[1].CmpRHS) ||
      (Cases[0].CmpRHS == Cases[1].CmpLHS &&
       Cases[0].CmpLHS == Cases[1].CmpRHS)) {
    return false;
  }

  // Handle: (X != null) | (Y != null) --> (X|Y) != 0
  // Handle: (X == null) & (Y == null) --> (X|Y) == 0
  if (Cases[0].CmpRHS == Cases[1].CmpRHS &&
      Cases[0].CC == Cases[1].CC &&
      isa<Constant>(Cases[0].CmpRHS) &&
      cast<Constant>(Cases[0].CmpRHS)->isNullValue()) {
    if (Cases[0].CC == ISD::SETEQ && Cases[0].TrueBB == Cases[1].ThisBB)
      return false;
    if (Cases[0].CC == ISD::SETNE && Cases[0].FalseBB == Cases[1].ThisBB)
      return false;
  }

  return true;
}

void SelectionDAGBuilder::visitBr(const BranchInst &I) {
  MachineBasicBlock *BrMBB = FuncInfo.MBB;

  // Update machine-CFG edges.
  MachineBasicBlock *Succ0MBB = FuncInfo.MBBMap[I.getSuccessor(0)];

  if (I.isUnconditional()) {
    // Update machine-CFG edges.
    BrMBB->addSuccessor(Succ0MBB);

    // If this is not a fall-through branch or optimizations are switched off,
    // emit the branch.
    if (Succ0MBB != NextBlock(BrMBB) ||
        TM.getOptLevel() == CodeGenOptLevel::None) {
      auto Br = DAG.getNode(ISD::BR, getCurSDLoc(), MVT::Other,
                            getControlRoot(), DAG.getBasicBlock(Succ0MBB));
      setValue(&I, Br);
      DAG.setRoot(Br);
    }

    return;
  }

  // If this condition is one of the special cases we handle, do special stuff
  // now.
  const Value *CondVal = I.getCondition();
  MachineBasicBlock *Succ1MBB = FuncInfo.MBBMap[I.getSuccessor(1)];

  // If this is a series of conditions that are or'd or and'd together, emit
  // this as a sequence of branches instead of setcc's with and/or operations.
  // As long as jumps are not expensive (exceptions for multi-use logic ops,
  // unpredictable branches, and vector extracts because those jumps are likely
  // expensive for any target), this should improve performance.
  // For example, instead of something like:
  //     cmp A, B
  //     C = seteq
  //     cmp D, E
  //     F = setle
  //     or C, F
  //     jnz foo
  // Emit:
  //     cmp A, B
  //     je foo
  //     cmp D, E
  //     jle foo
  const Instruction *BOp = dyn_cast<Instruction>(CondVal);
  if (!DAG.getTargetLoweringInfo().isJumpExpensive() && BOp &&
      BOp->hasOneUse() && !I.hasMetadata(LLVMContext::MD_unpredictable)) {
    Value *Vec;
    const Value *BOp0, *BOp1;
    Instruction::BinaryOps Opcode = (Instruction::BinaryOps)0;
    if (match(BOp, m_LogicalAnd(m_Value(BOp0), m_Value(BOp1))))
      Opcode = Instruction::And;
    else if (match(BOp, m_LogicalOr(m_Value(BOp0), m_Value(BOp1))))
      Opcode = Instruction::Or;

    if (Opcode &&
        !(match(BOp0, m_ExtractElt(m_Value(Vec), m_Value())) &&
          match(BOp1, m_ExtractElt(m_Specific(Vec), m_Value()))) &&
        !shouldKeepJumpConditionsTogether(
            FuncInfo, I, Opcode, BOp0, BOp1,
            DAG.getTargetLoweringInfo().getJumpConditionMergingParams(
                Opcode, BOp0, BOp1))) {
      FindMergedConditions(BOp, Succ0MBB, Succ1MBB, BrMBB, BrMBB, Opcode,
                           getEdgeProbability(BrMBB, Succ0MBB),
                           getEdgeProbability(BrMBB, Succ1MBB),
                           /*InvertCond=*/false);
      // If the compares in later blocks need to use values not currently
      // exported from this block, export them now.  This block should always
      // be the first entry.
      assert(SL->SwitchCases[0].ThisBB == BrMBB && "Unexpected lowering!");

      // Allow some cases to be rejected.
      if (ShouldEmitAsBranches(SL->SwitchCases)) {
        for (unsigned i = 1, e = SL->SwitchCases.size(); i != e; ++i) {
          ExportFromCurrentBlock(SL->SwitchCases[i].CmpLHS);
          ExportFromCurrentBlock(SL->SwitchCases[i].CmpRHS);
        }

        // Emit the branch for this block.
        visitSwitchCase(SL->SwitchCases[0], BrMBB);
        SL->SwitchCases.erase(SL->SwitchCases.begin());
        return;
      }

      // Okay, we decided not to do this, remove any inserted MBB's and clear
      // SwitchCases.
      for (unsigned i = 1, e = SL->SwitchCases.size(); i != e; ++i)
        FuncInfo.MF->erase(SL->SwitchCases[i].ThisBB);

      SL->SwitchCases.clear();
    }
  }

  // Create a CaseBlock record representing this branch.
  CaseBlock CB(ISD::SETEQ, CondVal, ConstantInt::getTrue(*DAG.getContext()),
               nullptr, Succ0MBB, Succ1MBB, BrMBB, getCurSDLoc());

  // Use visitSwitchCase to actually insert the fast branch sequence for this
  // cond branch.
  visitSwitchCase(CB, BrMBB);
}

/// visitSwitchCase - Emits the necessary code to represent a single node in
/// the binary search tree resulting from lowering a switch instruction.
void SelectionDAGBuilder::visitSwitchCase(CaseBlock &CB,
                                          MachineBasicBlock *SwitchBB) {
  SDValue Cond;
  SDValue CondLHS = getValue(CB.CmpLHS);
  SDLoc dl = CB.DL;

  if (CB.CC == ISD::SETTRUE) {
    // Branch or fall through to TrueBB.
    addSuccessorWithProb(SwitchBB, CB.TrueBB, CB.TrueProb);
    SwitchBB->normalizeSuccProbs();
    if (CB.TrueBB != NextBlock(SwitchBB)) {
      DAG.setRoot(DAG.getNode(ISD::BR, dl, MVT::Other, getControlRoot(),
                              DAG.getBasicBlock(CB.TrueBB)));
    }
    return;
  }

  auto &TLI = DAG.getTargetLoweringInfo();
  EVT MemVT = TLI.getMemValueType(DAG.getDataLayout(), CB.CmpLHS->getType());

  // Build the setcc now.
  if (!CB.CmpMHS) {
    // Fold "(X == true)" to X and "(X == false)" to !X to
    // handle common cases produced by branch lowering.
    if (CB.CmpRHS == ConstantInt::getTrue(*DAG.getContext()) &&
        CB.CC == ISD::SETEQ)
      Cond = CondLHS;
    else if (CB.CmpRHS == ConstantInt::getFalse(*DAG.getContext()) &&
             CB.CC == ISD::SETEQ) {
      SDValue True = DAG.getConstant(1, dl, CondLHS.getValueType());
      Cond = DAG.getNode(ISD::XOR, dl, CondLHS.getValueType(), CondLHS, True);
    } else {
      SDValue CondRHS = getValue(CB.CmpRHS);

      // If a pointer's DAG type is larger than its memory type then the DAG
      // values are zero-extended. This breaks signed comparisons so truncate
      // back to the underlying type before doing the compare.
      if (CondLHS.getValueType() != MemVT) {
        CondLHS = DAG.getPtrExtOrTrunc(CondLHS, getCurSDLoc(), MemVT);
        CondRHS = DAG.getPtrExtOrTrunc(CondRHS, getCurSDLoc(), MemVT);
      }
      Cond = DAG.getSetCC(dl, MVT::i1, CondLHS, CondRHS, CB.CC);
    }
  } else {
    assert(CB.CC == ISD::SETLE && "Can handle only LE ranges now");

    const APInt& Low = cast<ConstantInt>(CB.CmpLHS)->getValue();
    const APInt& High = cast<ConstantInt>(CB.CmpRHS)->getValue();

    SDValue CmpOp = getValue(CB.CmpMHS);
    EVT VT = CmpOp.getValueType();

    if (cast<ConstantInt>(CB.CmpLHS)->isMinValue(true)) {
      Cond = DAG.getSetCC(dl, MVT::i1, CmpOp, DAG.getConstant(High, dl, VT),
                          ISD::SETLE);
    } else {
      SDValue SUB = DAG.getNode(ISD::SUB, dl,
                                VT, CmpOp, DAG.getConstant(Low, dl, VT));
      Cond = DAG.getSetCC(dl, MVT::i1, SUB,
                          DAG.getConstant(High-Low, dl, VT), ISD::SETULE);
    }
  }

  // Update successor info
  addSuccessorWithProb(SwitchBB, CB.TrueBB, CB.TrueProb);
  // TrueBB and FalseBB are always different unless the incoming IR is
  // degenerate. This only happens when running llc on weird IR.
  if (CB.TrueBB != CB.FalseBB)
    addSuccessorWithProb(SwitchBB, CB.FalseBB, CB.FalseProb);
  SwitchBB->normalizeSuccProbs();

  // If the lhs block is the next block, invert the condition so that we can
  // fall through to the lhs instead of the rhs block.
  if (CB.TrueBB == NextBlock(SwitchBB)) {
    std::swap(CB.TrueBB, CB.FalseBB);
    SDValue True = DAG.getConstant(1, dl, Cond.getValueType());
    Cond = DAG.getNode(ISD::XOR, dl, Cond.getValueType(), Cond, True);
  }

  SDValue BrCond = DAG.getNode(ISD::BRCOND, dl,
                               MVT::Other, getControlRoot(), Cond,
                               DAG.getBasicBlock(CB.TrueBB));

  setValue(CurInst, BrCond);

  // Insert the false branch. Do this even if it's a fall through branch,
  // this makes it easier to do DAG optimizations which require inverting
  // the branch condition.
  BrCond = DAG.getNode(ISD::BR, dl, MVT::Other, BrCond,
                       DAG.getBasicBlock(CB.FalseBB));

  DAG.setRoot(BrCond);
}

/// visitJumpTable - Emit JumpTable node in the current MBB
void SelectionDAGBuilder::visitJumpTable(SwitchCG::JumpTable &JT) {
  // Emit the code for the jump table
  assert(JT.SL && "Should set SDLoc for SelectionDAG!");
  assert(JT.Reg != -1U && "Should lower JT Header first!");
  EVT PTy = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  SDValue Index = DAG.getCopyFromReg(getControlRoot(), *JT.SL, JT.Reg, PTy);
  SDValue Table = DAG.getJumpTable(JT.JTI, PTy);
  SDValue BrJumpTable = DAG.getNode(ISD::BR_JT, *JT.SL, MVT::Other,
                                    Index.getValue(1), Table, Index);
  DAG.setRoot(BrJumpTable);
}

/// visitJumpTableHeader - This function emits necessary code to produce index
/// in the JumpTable from switch case.
void SelectionDAGBuilder::visitJumpTableHeader(SwitchCG::JumpTable &JT,
                                               JumpTableHeader &JTH,
                                               MachineBasicBlock *SwitchBB) {
  assert(JT.SL && "Should set SDLoc for SelectionDAG!");
  const SDLoc &dl = *JT.SL;

  // Subtract the lowest switch case value from the value being switched on.
  SDValue SwitchOp = getValue(JTH.SValue);
  EVT VT = SwitchOp.getValueType();
  SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, SwitchOp,
                            DAG.getConstant(JTH.First, dl, VT));

  // The SDNode we just created, which holds the value being switched on minus
  // the smallest case value, needs to be copied to a virtual register so it
  // can be used as an index into the jump table in a subsequent basic block.
  // This value may be smaller or larger than the target's pointer type, and
  // therefore require extension or truncating.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SwitchOp = DAG.getZExtOrTrunc(Sub, dl, TLI.getPointerTy(DAG.getDataLayout()));

  unsigned JumpTableReg =
      FuncInfo.CreateReg(TLI.getPointerTy(DAG.getDataLayout()));
  SDValue CopyTo = DAG.getCopyToReg(getControlRoot(), dl,
                                    JumpTableReg, SwitchOp);
  JT.Reg = JumpTableReg;

  if (!JTH.FallthroughUnreachable) {
    // Emit the range check for the jump table, and branch to the default block
    // for the switch statement if the value being switched on exceeds the
    // largest case in the switch.
    SDValue CMP = DAG.getSetCC(
        dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                   Sub.getValueType()),
        Sub, DAG.getConstant(JTH.Last - JTH.First, dl, VT), ISD::SETUGT);

    SDValue BrCond = DAG.getNode(ISD::BRCOND, dl,
                                 MVT::Other, CopyTo, CMP,
                                 DAG.getBasicBlock(JT.Default));

    // Avoid emitting unnecessary branches to the next block.
    if (JT.MBB != NextBlock(SwitchBB))
      BrCond = DAG.getNode(ISD::BR, dl, MVT::Other, BrCond,
                           DAG.getBasicBlock(JT.MBB));

    DAG.setRoot(BrCond);
  } else {
    // Avoid emitting unnecessary branches to the next block.
    if (JT.MBB != NextBlock(SwitchBB))
      DAG.setRoot(DAG.getNode(ISD::BR, dl, MVT::Other, CopyTo,
                              DAG.getBasicBlock(JT.MBB)));
    else
      DAG.setRoot(CopyTo);
  }
}

/// Create a LOAD_STACK_GUARD node, and let it carry the target specific global
/// variable if there exists one.
static SDValue getLoadStackGuard(SelectionDAG &DAG, const SDLoc &DL,
                                 SDValue &Chain) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT PtrTy = TLI.getPointerTy(DAG.getDataLayout());
  EVT PtrMemTy = TLI.getPointerMemTy(DAG.getDataLayout());
  MachineFunction &MF = DAG.getMachineFunction();
  Value *Global = TLI.getSDagStackGuard(*MF.getFunction().getParent());
  MachineSDNode *Node =
      DAG.getMachineNode(TargetOpcode::LOAD_STACK_GUARD, DL, PtrTy, Chain);
  if (Global) {
    MachinePointerInfo MPInfo(Global);
    auto Flags = MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant |
                 MachineMemOperand::MODereferenceable;
    MachineMemOperand *MemRef = MF.getMachineMemOperand(
        MPInfo, Flags, LocationSize::precise(PtrTy.getSizeInBits() / 8),
        DAG.getEVTAlign(PtrTy));
    DAG.setNodeMemRefs(Node, {MemRef});
  }
  if (PtrTy != PtrMemTy)
    return DAG.getPtrExtOrTrunc(SDValue(Node, 0), DL, PtrMemTy);
  return SDValue(Node, 0);
}

/// Codegen a new tail for a stack protector check ParentMBB which has had its
/// tail spliced into a stack protector check success bb.
///
/// For a high level explanation of how this fits into the stack protector
/// generation see the comment on the declaration of class
/// StackProtectorDescriptor.
void SelectionDAGBuilder::visitSPDescriptorParent(StackProtectorDescriptor &SPD,
                                                  MachineBasicBlock *ParentBB) {

  // First create the loads to the guard/stack slot for the comparison.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT PtrTy = TLI.getPointerTy(DAG.getDataLayout());
  EVT PtrMemTy = TLI.getPointerMemTy(DAG.getDataLayout());

  MachineFrameInfo &MFI = ParentBB->getParent()->getFrameInfo();
  int FI = MFI.getStackProtectorIndex();

  SDValue Guard;
  SDLoc dl = getCurSDLoc();
  SDValue StackSlotPtr = DAG.getFrameIndex(FI, PtrTy);
  const Module &M = *ParentBB->getParent()->getFunction().getParent();
  Align Align =
      DAG.getDataLayout().getPrefTypeAlign(PointerType::get(M.getContext(), 0));

  // Generate code to load the content of the guard slot.
  SDValue GuardVal = DAG.getLoad(
      PtrMemTy, dl, DAG.getEntryNode(), StackSlotPtr,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI), Align,
      MachineMemOperand::MOVolatile);

  if (TLI.useStackGuardXorFP())
    GuardVal = TLI.emitStackGuardXorFP(DAG, GuardVal, dl);

  // Retrieve guard check function, nullptr if instrumentation is inlined.
  if (const Function *GuardCheckFn = TLI.getSSPStackGuardCheck(M)) {
    // The target provides a guard check function to validate the guard value.
    // Generate a call to that function with the content of the guard slot as
    // argument.
    FunctionType *FnTy = GuardCheckFn->getFunctionType();
    assert(FnTy->getNumParams() == 1 && "Invalid function signature");

    TargetLowering::ArgListTy Args;
    TargetLowering::ArgListEntry Entry;
    Entry.Node = GuardVal;
    Entry.Ty = FnTy->getParamType(0);
    if (GuardCheckFn->hasParamAttribute(0, Attribute::AttrKind::InReg))
      Entry.IsInReg = true;
    Args.push_back(Entry);

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(getCurSDLoc())
        .setChain(DAG.getEntryNode())
        .setCallee(GuardCheckFn->getCallingConv(), FnTy->getReturnType(),
                   getValue(GuardCheckFn), std::move(Args));

    std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);
    DAG.setRoot(Result.second);
    return;
  }

  // If useLoadStackGuardNode returns true, generate LOAD_STACK_GUARD.
  // Otherwise, emit a volatile load to retrieve the stack guard value.
  SDValue Chain = DAG.getEntryNode();
  if (TLI.useLoadStackGuardNode()) {
    Guard = getLoadStackGuard(DAG, dl, Chain);
  } else {
    const Value *IRGuard = TLI.getSDagStackGuard(M);
    SDValue GuardPtr = getValue(IRGuard);

    Guard = DAG.getLoad(PtrMemTy, dl, Chain, GuardPtr,
                        MachinePointerInfo(IRGuard, 0), Align,
                        MachineMemOperand::MOVolatile);
  }

  // Perform the comparison via a getsetcc.
  SDValue Cmp = DAG.getSetCC(dl, TLI.getSetCCResultType(DAG.getDataLayout(),
                                                        *DAG.getContext(),
                                                        Guard.getValueType()),
                             Guard, GuardVal, ISD::SETNE);

  // If the guard/stackslot do not equal, branch to failure MBB.
  SDValue BrCond = DAG.getNode(ISD::BRCOND, dl,
                               MVT::Other, GuardVal.getOperand(0),
                               Cmp, DAG.getBasicBlock(SPD.getFailureMBB()));
  // Otherwise branch to success MBB.
  SDValue Br = DAG.getNode(ISD::BR, dl,
                           MVT::Other, BrCond,
                           DAG.getBasicBlock(SPD.getSuccessMBB()));

  DAG.setRoot(Br);
}

/// Codegen the failure basic block for a stack protector check.
///
/// A failure stack protector machine basic block consists simply of a call to
/// __stack_chk_fail().
///
/// For a high level explanation of how this fits into the stack protector
/// generation see the comment on the declaration of class
/// StackProtectorDescriptor.
void
SelectionDAGBuilder::visitSPDescriptorFailure(StackProtectorDescriptor &SPD) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setDiscardResult(true);
  SDValue Chain =
      TLI.makeLibCall(DAG, RTLIB::STACKPROTECTOR_CHECK_FAIL, MVT::isVoid,
                      std::nullopt, CallOptions, getCurSDLoc())
          .second;
  // On PS4/PS5, the "return address" must still be within the calling
  // function, even if it's at the very end, so emit an explicit TRAP here.
  // Passing 'true' for doesNotReturn above won't generate the trap for us.
  if (TM.getTargetTriple().isPS())
    Chain = DAG.getNode(ISD::TRAP, getCurSDLoc(), MVT::Other, Chain);
  // WebAssembly needs an unreachable instruction after a non-returning call,
  // because the function return type can be different from __stack_chk_fail's
  // return type (void).
  if (TM.getTargetTriple().isWasm())
    Chain = DAG.getNode(ISD::TRAP, getCurSDLoc(), MVT::Other, Chain);

  DAG.setRoot(Chain);
}

/// visitBitTestHeader - This function emits necessary code to produce value
/// suitable for "bit tests"
void SelectionDAGBuilder::visitBitTestHeader(BitTestBlock &B,
                                             MachineBasicBlock *SwitchBB) {
  SDLoc dl = getCurSDLoc();

  // Subtract the minimum value.
  SDValue SwitchOp = getValue(B.SValue);
  EVT VT = SwitchOp.getValueType();
  SDValue RangeSub =
      DAG.getNode(ISD::SUB, dl, VT, SwitchOp, DAG.getConstant(B.First, dl, VT));

  // Determine the type of the test operands.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  bool UsePtrType = false;
  if (!TLI.isTypeLegal(VT)) {
    UsePtrType = true;
  } else {
    for (unsigned i = 0, e = B.Cases.size(); i != e; ++i)
      if (!isUIntN(VT.getSizeInBits(), B.Cases[i].Mask)) {
        // Switch table case range are encoded into series of masks.
        // Just use pointer type, it's guaranteed to fit.
        UsePtrType = true;
        break;
      }
  }
  SDValue Sub = RangeSub;
  if (UsePtrType) {
    VT = TLI.getPointerTy(DAG.getDataLayout());
    Sub = DAG.getZExtOrTrunc(Sub, dl, VT);
  }

  B.RegVT = VT.getSimpleVT();
  B.Reg = FuncInfo.CreateReg(B.RegVT);
  SDValue CopyTo = DAG.getCopyToReg(getControlRoot(), dl, B.Reg, Sub);

  MachineBasicBlock* MBB = B.Cases[0].ThisBB;

  if (!B.FallthroughUnreachable)
    addSuccessorWithProb(SwitchBB, B.Default, B.DefaultProb);
  addSuccessorWithProb(SwitchBB, MBB, B.Prob);
  SwitchBB->normalizeSuccProbs();

  SDValue Root = CopyTo;
  if (!B.FallthroughUnreachable) {
    // Conditional branch to the default block.
    SDValue RangeCmp = DAG.getSetCC(dl,
        TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                               RangeSub.getValueType()),
        RangeSub, DAG.getConstant(B.Range, dl, RangeSub.getValueType()),
        ISD::SETUGT);

    Root = DAG.getNode(ISD::BRCOND, dl, MVT::Other, Root, RangeCmp,
                       DAG.getBasicBlock(B.Default));
  }

  // Avoid emitting unnecessary branches to the next block.
  if (MBB != NextBlock(SwitchBB))
    Root = DAG.getNode(ISD::BR, dl, MVT::Other, Root, DAG.getBasicBlock(MBB));

  DAG.setRoot(Root);
}

/// visitBitTestCase - this function produces one "bit test"
void SelectionDAGBuilder::visitBitTestCase(BitTestBlock &BB,
                                           MachineBasicBlock* NextMBB,
                                           BranchProbability BranchProbToNext,
                                           unsigned Reg,
                                           BitTestCase &B,
                                           MachineBasicBlock *SwitchBB) {
  SDLoc dl = getCurSDLoc();
  MVT VT = BB.RegVT;
  SDValue ShiftOp = DAG.getCopyFromReg(getControlRoot(), dl, Reg, VT);
  SDValue Cmp;
  unsigned PopCount = llvm::popcount(B.Mask);
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (PopCount == 1) {
    // Testing for a single bit; just compare the shift count with what it
    // would need to be to shift a 1 bit in that position.
    Cmp = DAG.getSetCC(
        dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT),
        ShiftOp, DAG.getConstant(llvm::countr_zero(B.Mask), dl, VT),
        ISD::SETEQ);
  } else if (PopCount == BB.Range) {
    // There is only one zero bit in the range, test for it directly.
    Cmp = DAG.getSetCC(
        dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT),
        ShiftOp, DAG.getConstant(llvm::countr_one(B.Mask), dl, VT), ISD::SETNE);
  } else {
    // Make desired shift
    SDValue SwitchVal = DAG.getNode(ISD::SHL, dl, VT,
                                    DAG.getConstant(1, dl, VT), ShiftOp);

    // Emit bit tests and jumps
    SDValue AndOp = DAG.getNode(ISD::AND, dl,
                                VT, SwitchVal, DAG.getConstant(B.Mask, dl, VT));
    Cmp = DAG.getSetCC(
        dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT),
        AndOp, DAG.getConstant(0, dl, VT), ISD::SETNE);
  }

  // The branch probability from SwitchBB to B.TargetBB is B.ExtraProb.
  addSuccessorWithProb(SwitchBB, B.TargetBB, B.ExtraProb);
  // The branch probability from SwitchBB to NextMBB is BranchProbToNext.
  addSuccessorWithProb(SwitchBB, NextMBB, BranchProbToNext);
  // It is not guaranteed that the sum of B.ExtraProb and BranchProbToNext is
  // one as they are relative probabilities (and thus work more like weights),
  // and hence we need to normalize them to let the sum of them become one.
  SwitchBB->normalizeSuccProbs();

  SDValue BrAnd = DAG.getNode(ISD::BRCOND, dl,
                              MVT::Other, getControlRoot(),
                              Cmp, DAG.getBasicBlock(B.TargetBB));

  // Avoid emitting unnecessary branches to the next block.
  if (NextMBB != NextBlock(SwitchBB))
    BrAnd = DAG.getNode(ISD::BR, dl, MVT::Other, BrAnd,
                        DAG.getBasicBlock(NextMBB));

  DAG.setRoot(BrAnd);
}

void SelectionDAGBuilder::visitInvoke(const InvokeInst &I) {
  MachineBasicBlock *InvokeMBB = FuncInfo.MBB;

  // Retrieve successors. Look through artificial IR level blocks like
  // catchswitch for successors.
  MachineBasicBlock *Return = FuncInfo.MBBMap[I.getSuccessor(0)];
  const BasicBlock *EHPadBB = I.getSuccessor(1);
  MachineBasicBlock *EHPadMBB = FuncInfo.MBBMap[EHPadBB];

  // Deopt and ptrauth bundles are lowered in helper functions, and we don't
  // have to do anything here to lower funclet bundles.
  assert(!I.hasOperandBundlesOtherThan(
             {LLVMContext::OB_deopt, LLVMContext::OB_gc_transition,
              LLVMContext::OB_gc_live, LLVMContext::OB_funclet,
              LLVMContext::OB_cfguardtarget, LLVMContext::OB_ptrauth,
              LLVMContext::OB_clang_arc_attachedcall}) &&
         "Cannot lower invokes with arbitrary operand bundles yet!");

  const Value *Callee(I.getCalledOperand());
  const Function *Fn = dyn_cast<Function>(Callee);
  if (isa<InlineAsm>(Callee))
    visitInlineAsm(I, EHPadBB);
  else if (Fn && Fn->isIntrinsic()) {
    switch (Fn->getIntrinsicID()) {
    default:
      llvm_unreachable("Cannot invoke this intrinsic");
    case Intrinsic::donothing:
      // Ignore invokes to @llvm.donothing: jump directly to the next BB.
    case Intrinsic::seh_try_begin:
    case Intrinsic::seh_scope_begin:
    case Intrinsic::seh_try_end:
    case Intrinsic::seh_scope_end:
      if (EHPadMBB)
          // a block referenced by EH table
          // so dtor-funclet not removed by opts
          EHPadMBB->setMachineBlockAddressTaken();
      break;
    case Intrinsic::experimental_patchpoint_void:
    case Intrinsic::experimental_patchpoint:
      visitPatchpoint(I, EHPadBB);
      break;
    case Intrinsic::experimental_gc_statepoint:
      LowerStatepoint(cast<GCStatepointInst>(I), EHPadBB);
      break;
    case Intrinsic::wasm_rethrow: {
      // This is usually done in visitTargetIntrinsic, but this intrinsic is
      // special because it can be invoked, so we manually lower it to a DAG
      // node here.
      SmallVector<SDValue, 8> Ops;
      Ops.push_back(getControlRoot()); // inchain for the terminator node
      const TargetLowering &TLI = DAG.getTargetLoweringInfo();
      Ops.push_back(
          DAG.getTargetConstant(Intrinsic::wasm_rethrow, getCurSDLoc(),
                                TLI.getPointerTy(DAG.getDataLayout())));
      SDVTList VTs = DAG.getVTList(ArrayRef<EVT>({MVT::Other})); // outchain
      DAG.setRoot(DAG.getNode(ISD::INTRINSIC_VOID, getCurSDLoc(), VTs, Ops));
      break;
    }
    }
  } else if (I.hasDeoptState()) {
    // Currently we do not lower any intrinsic calls with deopt operand bundles.
    // Eventually we will support lowering the @llvm.experimental.deoptimize
    // intrinsic, and right now there are no plans to support other intrinsics
    // with deopt state.
    LowerCallSiteWithDeoptBundle(&I, getValue(Callee), EHPadBB);
  } else if (I.countOperandBundlesOfType(LLVMContext::OB_ptrauth)) {
    LowerCallSiteWithPtrAuthBundle(cast<CallBase>(I), EHPadBB);
  } else {
    LowerCallTo(I, getValue(Callee), false, false, EHPadBB);
  }

  // If the value of the invoke is used outside of its defining block, make it
  // available as a virtual register.
  // We already took care of the exported value for the statepoint instruction
  // during call to the LowerStatepoint.
  if (!isa<GCStatepointInst>(I)) {
    CopyToExportRegsIfNeeded(&I);
  }

  SmallVector<std::pair<MachineBasicBlock *, BranchProbability>, 1> UnwindDests;
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  BranchProbability EHPadBBProb =
      BPI ? BPI->getEdgeProbability(InvokeMBB->getBasicBlock(), EHPadBB)
          : BranchProbability::getZero();
  findUnwindDestinations(FuncInfo, EHPadBB, EHPadBBProb, UnwindDests);

  // Update successor info.
  addSuccessorWithProb(InvokeMBB, Return);
  for (auto &UnwindDest : UnwindDests) {
    UnwindDest.first->setIsEHPad();
    addSuccessorWithProb(InvokeMBB, UnwindDest.first, UnwindDest.second);
  }
  InvokeMBB->normalizeSuccProbs();

  // Drop into normal successor.
  DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(), MVT::Other, getControlRoot(),
                          DAG.getBasicBlock(Return)));
}

void SelectionDAGBuilder::visitCallBr(const CallBrInst &I) {
  MachineBasicBlock *CallBrMBB = FuncInfo.MBB;

  // Deopt bundles are lowered in LowerCallSiteWithDeoptBundle, and we don't
  // have to do anything here to lower funclet bundles.
  assert(!I.hasOperandBundlesOtherThan(
             {LLVMContext::OB_deopt, LLVMContext::OB_funclet}) &&
         "Cannot lower callbrs with arbitrary operand bundles yet!");

  assert(I.isInlineAsm() && "Only know how to handle inlineasm callbr");
  visitInlineAsm(I);
  CopyToExportRegsIfNeeded(&I);

  // Retrieve successors.
  SmallPtrSet<BasicBlock *, 8> Dests;
  Dests.insert(I.getDefaultDest());
  MachineBasicBlock *Return = FuncInfo.MBBMap[I.getDefaultDest()];

  // Update successor info.
  addSuccessorWithProb(CallBrMBB, Return, BranchProbability::getOne());
  for (unsigned i = 0, e = I.getNumIndirectDests(); i < e; ++i) {
    BasicBlock *Dest = I.getIndirectDest(i);
    MachineBasicBlock *Target = FuncInfo.MBBMap[Dest];
    Target->setIsInlineAsmBrIndirectTarget();
    Target->setMachineBlockAddressTaken();
    Target->setLabelMustBeEmitted();
    // Don't add duplicate machine successors.
    if (Dests.insert(Dest).second)
      addSuccessorWithProb(CallBrMBB, Target, BranchProbability::getZero());
  }
  CallBrMBB->normalizeSuccProbs();

  // Drop into default successor.
  DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(),
                          MVT::Other, getControlRoot(),
                          DAG.getBasicBlock(Return)));
}

void SelectionDAGBuilder::visitResume(const ResumeInst &RI) {
  llvm_unreachable("SelectionDAGBuilder shouldn't visit resume instructions!");
}

void SelectionDAGBuilder::visitLandingPad(const LandingPadInst &LP) {
  assert(FuncInfo.MBB->isEHPad() &&
         "Call to landingpad not in landing pad!");

  // If there aren't registers to copy the values into (e.g., during SjLj
  // exceptions), then don't bother to create these DAG nodes.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const Constant *PersonalityFn = FuncInfo.Fn->getPersonalityFn();
  if (TLI.getExceptionPointerRegister(PersonalityFn) == 0 &&
      TLI.getExceptionSelectorRegister(PersonalityFn) == 0)
    return;

  // If landingpad's return type is token type, we don't create DAG nodes
  // for its exception pointer and selector value. The extraction of exception
  // pointer or selector value from token type landingpads is not currently
  // supported.
  if (LP.getType()->isTokenTy())
    return;

  SmallVector<EVT, 2> ValueVTs;
  SDLoc dl = getCurSDLoc();
  ComputeValueVTs(TLI, DAG.getDataLayout(), LP.getType(), ValueVTs);
  assert(ValueVTs.size() == 2 && "Only two-valued landingpads are supported");

  // Get the two live-in registers as SDValues. The physregs have already been
  // copied into virtual registers.
  SDValue Ops[2];
  if (FuncInfo.ExceptionPointerVirtReg) {
    Ops[0] = DAG.getZExtOrTrunc(
        DAG.getCopyFromReg(DAG.getEntryNode(), dl,
                           FuncInfo.ExceptionPointerVirtReg,
                           TLI.getPointerTy(DAG.getDataLayout())),
        dl, ValueVTs[0]);
  } else {
    Ops[0] = DAG.getConstant(0, dl, TLI.getPointerTy(DAG.getDataLayout()));
  }
  Ops[1] = DAG.getZExtOrTrunc(
      DAG.getCopyFromReg(DAG.getEntryNode(), dl,
                         FuncInfo.ExceptionSelectorVirtReg,
                         TLI.getPointerTy(DAG.getDataLayout())),
      dl, ValueVTs[1]);

  // Merge into one.
  SDValue Res = DAG.getNode(ISD::MERGE_VALUES, dl,
                            DAG.getVTList(ValueVTs), Ops);
  setValue(&LP, Res);
}

void SelectionDAGBuilder::UpdateSplitBlock(MachineBasicBlock *First,
                                           MachineBasicBlock *Last) {
  // Update JTCases.
  for (JumpTableBlock &JTB : SL->JTCases)
    if (JTB.first.HeaderBB == First)
      JTB.first.HeaderBB = Last;

  // Update BitTestCases.
  for (BitTestBlock &BTB : SL->BitTestCases)
    if (BTB.Parent == First)
      BTB.Parent = Last;
}

void SelectionDAGBuilder::visitIndirectBr(const IndirectBrInst &I) {
  MachineBasicBlock *IndirectBrMBB = FuncInfo.MBB;

  // Update machine-CFG edges with unique successors.
  SmallSet<BasicBlock*, 32> Done;
  for (unsigned i = 0, e = I.getNumSuccessors(); i != e; ++i) {
    BasicBlock *BB = I.getSuccessor(i);
    bool Inserted = Done.insert(BB).second;
    if (!Inserted)
        continue;

    MachineBasicBlock *Succ = FuncInfo.MBBMap[BB];
    addSuccessorWithProb(IndirectBrMBB, Succ);
  }
  IndirectBrMBB->normalizeSuccProbs();

  DAG.setRoot(DAG.getNode(ISD::BRIND, getCurSDLoc(),
                          MVT::Other, getControlRoot(),
                          getValue(I.getAddress())));
}

void SelectionDAGBuilder::visitUnreachable(const UnreachableInst &I) {
  if (!DAG.getTarget().Options.TrapUnreachable)
    return;

  // We may be able to ignore unreachable behind a noreturn call.
  if (const CallInst *Call = dyn_cast_or_null<CallInst>(I.getPrevNode());
      Call && Call->doesNotReturn()) {
    if (DAG.getTarget().Options.NoTrapAfterNoreturn)
      return;
    // Do not emit an additional trap instruction.
    if (Call->isNonContinuableTrap())
      return;
  }

  DAG.setRoot(DAG.getNode(ISD::TRAP, getCurSDLoc(), MVT::Other, DAG.getRoot()));
}

void SelectionDAGBuilder::visitUnary(const User &I, unsigned Opcode) {
  SDNodeFlags Flags;
  if (auto *FPOp = dyn_cast<FPMathOperator>(&I))
    Flags.copyFMF(*FPOp);

  SDValue Op = getValue(I.getOperand(0));
  SDValue UnNodeValue = DAG.getNode(Opcode, getCurSDLoc(), Op.getValueType(),
                                    Op, Flags);
  setValue(&I, UnNodeValue);
}

void SelectionDAGBuilder::visitBinary(const User &I, unsigned Opcode) {
  SDNodeFlags Flags;
  if (auto *OFBinOp = dyn_cast<OverflowingBinaryOperator>(&I)) {
    Flags.setNoSignedWrap(OFBinOp->hasNoSignedWrap());
    Flags.setNoUnsignedWrap(OFBinOp->hasNoUnsignedWrap());
  }
  if (auto *ExactOp = dyn_cast<PossiblyExactOperator>(&I))
    Flags.setExact(ExactOp->isExact());
  if (auto *DisjointOp = dyn_cast<PossiblyDisjointInst>(&I))
    Flags.setDisjoint(DisjointOp->isDisjoint());
  if (auto *FPOp = dyn_cast<FPMathOperator>(&I))
    Flags.copyFMF(*FPOp);

  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));
  SDValue BinNodeValue = DAG.getNode(Opcode, getCurSDLoc(), Op1.getValueType(),
                                     Op1, Op2, Flags);
  setValue(&I, BinNodeValue);
}

void SelectionDAGBuilder::visitShift(const User &I, unsigned Opcode) {
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));

  EVT ShiftTy = DAG.getTargetLoweringInfo().getShiftAmountTy(
      Op1.getValueType(), DAG.getDataLayout());

  // Coerce the shift amount to the right type if we can. This exposes the
  // truncate or zext to optimization early.
  if (!I.getType()->isVectorTy() && Op2.getValueType() != ShiftTy) {
    assert(ShiftTy.getSizeInBits() >= Log2_32_Ceil(Op1.getValueSizeInBits()) &&
           "Unexpected shift type");
    Op2 = DAG.getZExtOrTrunc(Op2, getCurSDLoc(), ShiftTy);
  }

  bool nuw = false;
  bool nsw = false;
  bool exact = false;

  if (Opcode == ISD::SRL || Opcode == ISD::SRA || Opcode == ISD::SHL) {

    if (const OverflowingBinaryOperator *OFBinOp =
            dyn_cast<const OverflowingBinaryOperator>(&I)) {
      nuw = OFBinOp->hasNoUnsignedWrap();
      nsw = OFBinOp->hasNoSignedWrap();
    }
    if (const PossiblyExactOperator *ExactOp =
            dyn_cast<const PossiblyExactOperator>(&I))
      exact = ExactOp->isExact();
  }
  SDNodeFlags Flags;
  Flags.setExact(exact);
  Flags.setNoSignedWrap(nsw);
  Flags.setNoUnsignedWrap(nuw);
  SDValue Res = DAG.getNode(Opcode, getCurSDLoc(), Op1.getValueType(), Op1, Op2,
                            Flags);
  setValue(&I, Res);
}

void SelectionDAGBuilder::visitSDiv(const User &I) {
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));

  SDNodeFlags Flags;
  Flags.setExact(isa<PossiblyExactOperator>(&I) &&
                 cast<PossiblyExactOperator>(&I)->isExact());
  setValue(&I, DAG.getNode(ISD::SDIV, getCurSDLoc(), Op1.getValueType(), Op1,
                           Op2, Flags));
}

void SelectionDAGBuilder::visitICmp(const ICmpInst &I) {
  ICmpInst::Predicate predicate = I.getPredicate();
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));
  ISD::CondCode Opcode = getICmpCondCode(predicate);

  auto &TLI = DAG.getTargetLoweringInfo();
  EVT MemVT =
      TLI.getMemValueType(DAG.getDataLayout(), I.getOperand(0)->getType());

  // If a pointer's DAG type is larger than its memory type then the DAG values
  // are zero-extended. This breaks signed comparisons so truncate back to the
  // underlying type before doing the compare.
  if (Op1.getValueType() != MemVT) {
    Op1 = DAG.getPtrExtOrTrunc(Op1, getCurSDLoc(), MemVT);
    Op2 = DAG.getPtrExtOrTrunc(Op2, getCurSDLoc(), MemVT);
  }

  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getSetCC(getCurSDLoc(), DestVT, Op1, Op2, Opcode));
}

void SelectionDAGBuilder::visitFCmp(const FCmpInst &I) {
  FCmpInst::Predicate predicate = I.getPredicate();
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));

  ISD::CondCode Condition = getFCmpCondCode(predicate);
  auto *FPMO = cast<FPMathOperator>(&I);
  if (FPMO->hasNoNaNs() || TM.Options.NoNaNsFPMath)
    Condition = getFCmpCodeWithoutNaN(Condition);

  SDNodeFlags Flags;
  Flags.copyFMF(*FPMO);
  SelectionDAG::FlagInserter FlagsInserter(DAG, Flags);

  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getSetCC(getCurSDLoc(), DestVT, Op1, Op2, Condition));
}

// Check if the condition of the select has one use or two users that are both
// selects with the same condition.
static bool hasOnlySelectUsers(const Value *Cond) {
  return llvm::all_of(Cond->users(), [](const Value *V) {
    return isa<SelectInst>(V);
  });
}

void SelectionDAGBuilder::visitSelect(const User &I) {
  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(), I.getType(),
                  ValueVTs);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0) return;

  SmallVector<SDValue, 4> Values(NumValues);
  SDValue Cond     = getValue(I.getOperand(0));
  SDValue LHSVal   = getValue(I.getOperand(1));
  SDValue RHSVal   = getValue(I.getOperand(2));
  SmallVector<SDValue, 1> BaseOps(1, Cond);
  ISD::NodeType OpCode =
      Cond.getValueType().isVector() ? ISD::VSELECT : ISD::SELECT;

  bool IsUnaryAbs = false;
  bool Negate = false;

  SDNodeFlags Flags;
  if (auto *FPOp = dyn_cast<FPMathOperator>(&I))
    Flags.copyFMF(*FPOp);

  Flags.setUnpredictable(
      cast<SelectInst>(I).getMetadata(LLVMContext::MD_unpredictable));

  // Min/max matching is only viable if all output VTs are the same.
  if (all_equal(ValueVTs)) {
    EVT VT = ValueVTs[0];
    LLVMContext &Ctx = *DAG.getContext();
    auto &TLI = DAG.getTargetLoweringInfo();

    // We care about the legality of the operation after it has been type
    // legalized.
    while (TLI.getTypeAction(Ctx, VT) != TargetLoweringBase::TypeLegal)
      VT = TLI.getTypeToTransformTo(Ctx, VT);

    // If the vselect is legal, assume we want to leave this as a vector setcc +
    // vselect. Otherwise, if this is going to be scalarized, we want to see if
    // min/max is legal on the scalar type.
    bool UseScalarMinMax = VT.isVector() &&
      !TLI.isOperationLegalOrCustom(ISD::VSELECT, VT);

    // ValueTracking's select pattern matching does not account for -0.0,
    // so we can't lower to FMINIMUM/FMAXIMUM because those nodes specify that
    // -0.0 is less than +0.0.
    Value *LHS, *RHS;
    auto SPR = matchSelectPattern(const_cast<User*>(&I), LHS, RHS);
    ISD::NodeType Opc = ISD::DELETED_NODE;
    switch (SPR.Flavor) {
    case SPF_UMAX:    Opc = ISD::UMAX; break;
    case SPF_UMIN:    Opc = ISD::UMIN; break;
    case SPF_SMAX:    Opc = ISD::SMAX; break;
    case SPF_SMIN:    Opc = ISD::SMIN; break;
    case SPF_FMINNUM:
      switch (SPR.NaNBehavior) {
      case SPNB_NA: llvm_unreachable("No NaN behavior for FP op?");
      case SPNB_RETURNS_NAN: break;
      case SPNB_RETURNS_OTHER: Opc = ISD::FMINNUM; break;
      case SPNB_RETURNS_ANY:
        if (TLI.isOperationLegalOrCustom(ISD::FMINNUM, VT) ||
            (UseScalarMinMax &&
             TLI.isOperationLegalOrCustom(ISD::FMINNUM, VT.getScalarType())))
          Opc = ISD::FMINNUM;
        break;
      }
      break;
    case SPF_FMAXNUM:
      switch (SPR.NaNBehavior) {
      case SPNB_NA: llvm_unreachable("No NaN behavior for FP op?");
      case SPNB_RETURNS_NAN: break;
      case SPNB_RETURNS_OTHER: Opc = ISD::FMAXNUM; break;
      case SPNB_RETURNS_ANY:
        if (TLI.isOperationLegalOrCustom(ISD::FMAXNUM, VT) ||
            (UseScalarMinMax &&
             TLI.isOperationLegalOrCustom(ISD::FMAXNUM, VT.getScalarType())))
          Opc = ISD::FMAXNUM;
        break;
      }
      break;
    case SPF_NABS:
      Negate = true;
      [[fallthrough]];
    case SPF_ABS:
      IsUnaryAbs = true;
      Opc = ISD::ABS;
      break;
    default: break;
    }

    if (!IsUnaryAbs && Opc != ISD::DELETED_NODE &&
        (TLI.isOperationLegalOrCustomOrPromote(Opc, VT) ||
         (UseScalarMinMax &&
          TLI.isOperationLegalOrCustom(Opc, VT.getScalarType()))) &&
        // If the underlying comparison instruction is used by any other
        // instruction, the consumed instructions won't be destroyed, so it is
        // not profitable to convert to a min/max.
        hasOnlySelectUsers(cast<SelectInst>(I).getCondition())) {
      OpCode = Opc;
      LHSVal = getValue(LHS);
      RHSVal = getValue(RHS);
      BaseOps.clear();
    }

    if (IsUnaryAbs) {
      OpCode = Opc;
      LHSVal = getValue(LHS);
      BaseOps.clear();
    }
  }

  if (IsUnaryAbs) {
    for (unsigned i = 0; i != NumValues; ++i) {
      SDLoc dl = getCurSDLoc();
      EVT VT = LHSVal.getNode()->getValueType(LHSVal.getResNo() + i);
      Values[i] =
          DAG.getNode(OpCode, dl, VT, LHSVal.getValue(LHSVal.getResNo() + i));
      if (Negate)
        Values[i] = DAG.getNegative(Values[i], dl, VT);
    }
  } else {
    for (unsigned i = 0; i != NumValues; ++i) {
      SmallVector<SDValue, 3> Ops(BaseOps.begin(), BaseOps.end());
      Ops.push_back(SDValue(LHSVal.getNode(), LHSVal.getResNo() + i));
      Ops.push_back(SDValue(RHSVal.getNode(), RHSVal.getResNo() + i));
      Values[i] = DAG.getNode(
          OpCode, getCurSDLoc(),
          LHSVal.getNode()->getValueType(LHSVal.getResNo() + i), Ops, Flags);
    }
  }

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                           DAG.getVTList(ValueVTs), Values));
}

void SelectionDAGBuilder::visitTrunc(const User &I) {
  // TruncInst cannot be a no-op cast because sizeof(src) > sizeof(dest).
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::TRUNCATE, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitZExt(const User &I) {
  // ZExt cannot be a no-op cast because sizeof(src) < sizeof(dest).
  // ZExt also can't be a cast to bool for same reason. So, nothing much to do
  SDValue N = getValue(I.getOperand(0));
  auto &TLI = DAG.getTargetLoweringInfo();
  EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());

  SDNodeFlags Flags;
  if (auto *PNI = dyn_cast<PossiblyNonNegInst>(&I))
    Flags.setNonNeg(PNI->hasNonNeg());

  // Eagerly use nonneg information to canonicalize towards sign_extend if
  // that is the target's preference.
  // TODO: Let the target do this later.
  if (Flags.hasNonNeg() &&
      TLI.isSExtCheaperThanZExt(N.getValueType(), DestVT)) {
    setValue(&I, DAG.getNode(ISD::SIGN_EXTEND, getCurSDLoc(), DestVT, N));
    return;
  }

  setValue(&I, DAG.getNode(ISD::ZERO_EXTEND, getCurSDLoc(), DestVT, N, Flags));
}

void SelectionDAGBuilder::visitSExt(const User &I) {
  // SExt cannot be a no-op cast because sizeof(src) < sizeof(dest).
  // SExt also can't be a cast to bool for same reason. So, nothing much to do
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::SIGN_EXTEND, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitFPTrunc(const User &I) {
  // FPTrunc is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  SDLoc dl = getCurSDLoc();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  setValue(&I, DAG.getNode(ISD::FP_ROUND, dl, DestVT, N,
                           DAG.getTargetConstant(
                               0, dl, TLI.getPointerTy(DAG.getDataLayout()))));
}

void SelectionDAGBuilder::visitFPExt(const User &I) {
  // FPExt is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::FP_EXTEND, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitFPToUI(const User &I) {
  // FPToUI is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::FP_TO_UINT, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitFPToSI(const User &I) {
  // FPToSI is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::FP_TO_SINT, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitUIToFP(const User &I) {
  // UIToFP is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  SDNodeFlags Flags;
  if (auto *PNI = dyn_cast<PossiblyNonNegInst>(&I))
    Flags.setNonNeg(PNI->hasNonNeg());

  setValue(&I, DAG.getNode(ISD::UINT_TO_FP, getCurSDLoc(), DestVT, N, Flags));
}

void SelectionDAGBuilder::visitSIToFP(const User &I) {
  // SIToFP is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::SINT_TO_FP, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitPtrToInt(const User &I) {
  // What to do depends on the size of the integer and the size of the pointer.
  // We can either truncate, zero extend, or no-op, accordingly.
  SDValue N = getValue(I.getOperand(0));
  auto &TLI = DAG.getTargetLoweringInfo();
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  EVT PtrMemVT =
      TLI.getMemValueType(DAG.getDataLayout(), I.getOperand(0)->getType());
  N = DAG.getPtrExtOrTrunc(N, getCurSDLoc(), PtrMemVT);
  N = DAG.getZExtOrTrunc(N, getCurSDLoc(), DestVT);
  setValue(&I, N);
}

void SelectionDAGBuilder::visitIntToPtr(const User &I) {
  // What to do depends on the size of the integer and the size of the pointer.
  // We can either truncate, zero extend, or no-op, accordingly.
  SDValue N = getValue(I.getOperand(0));
  auto &TLI = DAG.getTargetLoweringInfo();
  EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  EVT PtrMemVT = TLI.getMemValueType(DAG.getDataLayout(), I.getType());
  N = DAG.getZExtOrTrunc(N, getCurSDLoc(), PtrMemVT);
  N = DAG.getPtrExtOrTrunc(N, getCurSDLoc(), DestVT);
  setValue(&I, N);
}

void SelectionDAGBuilder::visitBitCast(const User &I) {
  SDValue N = getValue(I.getOperand(0));
  SDLoc dl = getCurSDLoc();
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());

  // BitCast assures us that source and destination are the same size so this is
  // either a BITCAST or a no-op.
  if (DestVT != N.getValueType())
    setValue(&I, DAG.getNode(ISD::BITCAST, dl,
                             DestVT, N)); // convert types.
  // Check if the original LLVM IR Operand was a ConstantInt, because getValue()
  // might fold any kind of constant expression to an integer constant and that
  // is not what we are looking for. Only recognize a bitcast of a genuine
  // constant integer as an opaque constant.
  else if(ConstantInt *C = dyn_cast<ConstantInt>(I.getOperand(0)))
    setValue(&I, DAG.getConstant(C->getValue(), dl, DestVT, /*isTarget=*/false,
                                 /*isOpaque*/true));
  else
    setValue(&I, N);            // noop cast.
}

void SelectionDAGBuilder::visitAddrSpaceCast(const User &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const Value *SV = I.getOperand(0);
  SDValue N = getValue(SV);
  EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());

  unsigned SrcAS = SV->getType()->getPointerAddressSpace();
  unsigned DestAS = I.getType()->getPointerAddressSpace();

  if (!TM.isNoopAddrSpaceCast(SrcAS, DestAS))
    N = DAG.getAddrSpaceCast(getCurSDLoc(), DestVT, N, SrcAS, DestAS);

  setValue(&I, N);
}

void SelectionDAGBuilder::visitInsertElement(const User &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue InVec = getValue(I.getOperand(0));
  SDValue InVal = getValue(I.getOperand(1));
  SDValue InIdx = DAG.getZExtOrTrunc(getValue(I.getOperand(2)), getCurSDLoc(),
                                     TLI.getVectorIdxTy(DAG.getDataLayout()));
  setValue(&I, DAG.getNode(ISD::INSERT_VECTOR_ELT, getCurSDLoc(),
                           TLI.getValueType(DAG.getDataLayout(), I.getType()),
                           InVec, InVal, InIdx));
}

void SelectionDAGBuilder::visitExtractElement(const User &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue InVec = getValue(I.getOperand(0));
  SDValue InIdx = DAG.getZExtOrTrunc(getValue(I.getOperand(1)), getCurSDLoc(),
                                     TLI.getVectorIdxTy(DAG.getDataLayout()));
  setValue(&I, DAG.getNode(ISD::EXTRACT_VECTOR_ELT, getCurSDLoc(),
                           TLI.getValueType(DAG.getDataLayout(), I.getType()),
                           InVec, InIdx));
}

void SelectionDAGBuilder::visitShuffleVector(const User &I) {
  SDValue Src1 = getValue(I.getOperand(0));
  SDValue Src2 = getValue(I.getOperand(1));
  ArrayRef<int> Mask;
  if (auto *SVI = dyn_cast<ShuffleVectorInst>(&I))
    Mask = SVI->getShuffleMask();
  else
    Mask = cast<ConstantExpr>(I).getShuffleMask();
  SDLoc DL = getCurSDLoc();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  EVT SrcVT = Src1.getValueType();

  if (all_of(Mask, [](int Elem) { return Elem == 0; }) &&
      VT.isScalableVector()) {
    // Canonical splat form of first element of first input vector.
    SDValue FirstElt =
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, SrcVT.getScalarType(), Src1,
                    DAG.getVectorIdxConstant(0, DL));
    setValue(&I, DAG.getNode(ISD::SPLAT_VECTOR, DL, VT, FirstElt));
    return;
  }

  // For now, we only handle splats for scalable vectors.
  // The DAGCombiner will perform a BUILD_VECTOR -> SPLAT_VECTOR transformation
  // for targets that support a SPLAT_VECTOR for non-scalable vector types.
  assert(!VT.isScalableVector() && "Unsupported scalable vector shuffle");

  unsigned SrcNumElts = SrcVT.getVectorNumElements();
  unsigned MaskNumElts = Mask.size();

  if (SrcNumElts == MaskNumElts) {
    setValue(&I, DAG.getVectorShuffle(VT, DL, Src1, Src2, Mask));
    return;
  }

  // Normalize the shuffle vector since mask and vector length don't match.
  if (SrcNumElts < MaskNumElts) {
    // Mask is longer than the source vectors. We can use concatenate vector to
    // make the mask and vectors lengths match.

    if (MaskNumElts % SrcNumElts == 0) {
      // Mask length is a multiple of the source vector length.
      // Check if the shuffle is some kind of concatenation of the input
      // vectors.
      unsigned NumConcat = MaskNumElts / SrcNumElts;
      bool IsConcat = true;
      SmallVector<int, 8> ConcatSrcs(NumConcat, -1);
      for (unsigned i = 0; i != MaskNumElts; ++i) {
        int Idx = Mask[i];
        if (Idx < 0)
          continue;
        // Ensure the indices in each SrcVT sized piece are sequential and that
        // the same source is used for the whole piece.
        if ((Idx % SrcNumElts != (i % SrcNumElts)) ||
            (ConcatSrcs[i / SrcNumElts] >= 0 &&
             ConcatSrcs[i / SrcNumElts] != (int)(Idx / SrcNumElts))) {
          IsConcat = false;
          break;
        }
        // Remember which source this index came from.
        ConcatSrcs[i / SrcNumElts] = Idx / SrcNumElts;
      }

      // The shuffle is concatenating multiple vectors together. Just emit
      // a CONCAT_VECTORS operation.
      if (IsConcat) {
        SmallVector<SDValue, 8> ConcatOps;
        for (auto Src : ConcatSrcs) {
          if (Src < 0)
            ConcatOps.push_back(DAG.getUNDEF(SrcVT));
          else if (Src == 0)
            ConcatOps.push_back(Src1);
          else
            ConcatOps.push_back(Src2);
        }
        setValue(&I, DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, ConcatOps));
        return;
      }
    }

    unsigned PaddedMaskNumElts = alignTo(MaskNumElts, SrcNumElts);
    unsigned NumConcat = PaddedMaskNumElts / SrcNumElts;
    EVT PaddedVT = EVT::getVectorVT(*DAG.getContext(), VT.getScalarType(),
                                    PaddedMaskNumElts);

    // Pad both vectors with undefs to make them the same length as the mask.
    SDValue UndefVal = DAG.getUNDEF(SrcVT);

    SmallVector<SDValue, 8> MOps1(NumConcat, UndefVal);
    SmallVector<SDValue, 8> MOps2(NumConcat, UndefVal);
    MOps1[0] = Src1;
    MOps2[0] = Src2;

    Src1 = DAG.getNode(ISD::CONCAT_VECTORS, DL, PaddedVT, MOps1);
    Src2 = DAG.getNode(ISD::CONCAT_VECTORS, DL, PaddedVT, MOps2);

    // Readjust mask for new input vector length.
    SmallVector<int, 8> MappedOps(PaddedMaskNumElts, -1);
    for (unsigned i = 0; i != MaskNumElts; ++i) {
      int Idx = Mask[i];
      if (Idx >= (int)SrcNumElts)
        Idx -= SrcNumElts - PaddedMaskNumElts;
      MappedOps[i] = Idx;
    }

    SDValue Result = DAG.getVectorShuffle(PaddedVT, DL, Src1, Src2, MappedOps);

    // If the concatenated vector was padded, extract a subvector with the
    // correct number of elements.
    if (MaskNumElts != PaddedMaskNumElts)
      Result = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Result,
                           DAG.getVectorIdxConstant(0, DL));

    setValue(&I, Result);
    return;
  }

  if (SrcNumElts > MaskNumElts) {
    // Analyze the access pattern of the vector to see if we can extract
    // two subvectors and do the shuffle.
    int StartIdx[2] = { -1, -1 };  // StartIdx to extract from
    bool CanExtract = true;
    for (int Idx : Mask) {
      unsigned Input = 0;
      if (Idx < 0)
        continue;

      if (Idx >= (int)SrcNumElts) {
        Input = 1;
        Idx -= SrcNumElts;
      }

      // If all the indices come from the same MaskNumElts sized portion of
      // the sources we can use extract. Also make sure the extract wouldn't
      // extract past the end of the source.
      int NewStartIdx = alignDown(Idx, MaskNumElts);
      if (NewStartIdx + MaskNumElts > SrcNumElts ||
          (StartIdx[Input] >= 0 && StartIdx[Input] != NewStartIdx))
        CanExtract = false;
      // Make sure we always update StartIdx as we use it to track if all
      // elements are undef.
      StartIdx[Input] = NewStartIdx;
    }

    if (StartIdx[0] < 0 && StartIdx[1] < 0) {
      setValue(&I, DAG.getUNDEF(VT)); // Vectors are not used.
      return;
    }
    if (CanExtract) {
      // Extract appropriate subvector and generate a vector shuffle
      for (unsigned Input = 0; Input < 2; ++Input) {
        SDValue &Src = Input == 0 ? Src1 : Src2;
        if (StartIdx[Input] < 0)
          Src = DAG.getUNDEF(VT);
        else {
          Src = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Src,
                            DAG.getVectorIdxConstant(StartIdx[Input], DL));
        }
      }

      // Calculate new mask.
      SmallVector<int, 8> MappedOps(Mask);
      for (int &Idx : MappedOps) {
        if (Idx >= (int)SrcNumElts)
          Idx -= SrcNumElts + StartIdx[1] - MaskNumElts;
        else if (Idx >= 0)
          Idx -= StartIdx[0];
      }

      setValue(&I, DAG.getVectorShuffle(VT, DL, Src1, Src2, MappedOps));
      return;
    }
  }

  // We can't use either concat vectors or extract subvectors so fall back to
  // replacing the shuffle with extract and build vector.
  // to insert and build vector.
  EVT EltVT = VT.getVectorElementType();
  SmallVector<SDValue,8> Ops;
  for (int Idx : Mask) {
    SDValue Res;

    if (Idx < 0) {
      Res = DAG.getUNDEF(EltVT);
    } else {
      SDValue &Src = Idx < (int)SrcNumElts ? Src1 : Src2;
      if (Idx >= (int)SrcNumElts) Idx -= SrcNumElts;

      Res = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Src,
                        DAG.getVectorIdxConstant(Idx, DL));
    }

    Ops.push_back(Res);
  }

  setValue(&I, DAG.getBuildVector(VT, DL, Ops));
}

void SelectionDAGBuilder::visitInsertValue(const InsertValueInst &I) {
  ArrayRef<unsigned> Indices = I.getIndices();
  const Value *Op0 = I.getOperand(0);
  const Value *Op1 = I.getOperand(1);
  Type *AggTy = I.getType();
  Type *ValTy = Op1->getType();
  bool IntoUndef = isa<UndefValue>(Op0);
  bool FromUndef = isa<UndefValue>(Op1);

  unsigned LinearIndex = ComputeLinearIndex(AggTy, Indices);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SmallVector<EVT, 4> AggValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), AggTy, AggValueVTs);
  SmallVector<EVT, 4> ValValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), ValTy, ValValueVTs);

  unsigned NumAggValues = AggValueVTs.size();
  unsigned NumValValues = ValValueVTs.size();
  SmallVector<SDValue, 4> Values(NumAggValues);

  // Ignore an insertvalue that produces an empty object
  if (!NumAggValues) {
    setValue(&I, DAG.getUNDEF(MVT(MVT::Other)));
    return;
  }

  SDValue Agg = getValue(Op0);
  unsigned i = 0;
  // Copy the beginning value(s) from the original aggregate.
  for (; i != LinearIndex; ++i)
    Values[i] = IntoUndef ? DAG.getUNDEF(AggValueVTs[i]) :
                SDValue(Agg.getNode(), Agg.getResNo() + i);
  // Copy values from the inserted value(s).
  if (NumValValues) {
    SDValue Val = getValue(Op1);
    for (; i != LinearIndex + NumValValues; ++i)
      Values[i] = FromUndef ? DAG.getUNDEF(AggValueVTs[i]) :
                  SDValue(Val.getNode(), Val.getResNo() + i - LinearIndex);
  }
  // Copy remaining value(s) from the original aggregate.
  for (; i != NumAggValues; ++i)
    Values[i] = IntoUndef ? DAG.getUNDEF(AggValueVTs[i]) :
                SDValue(Agg.getNode(), Agg.getResNo() + i);

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                           DAG.getVTList(AggValueVTs), Values));
}

void SelectionDAGBuilder::visitExtractValue(const ExtractValueInst &I) {
  ArrayRef<unsigned> Indices = I.getIndices();
  const Value *Op0 = I.getOperand(0);
  Type *AggTy = Op0->getType();
  Type *ValTy = I.getType();
  bool OutOfUndef = isa<UndefValue>(Op0);

  unsigned LinearIndex = ComputeLinearIndex(AggTy, Indices);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SmallVector<EVT, 4> ValValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), ValTy, ValValueVTs);

  unsigned NumValValues = ValValueVTs.size();

  // Ignore a extractvalue that produces an empty object
  if (!NumValValues) {
    setValue(&I, DAG.getUNDEF(MVT(MVT::Other)));
    return;
  }

  SmallVector<SDValue, 4> Values(NumValValues);

  SDValue Agg = getValue(Op0);
  // Copy out the selected value(s).
  for (unsigned i = LinearIndex; i != LinearIndex + NumValValues; ++i)
    Values[i - LinearIndex] =
      OutOfUndef ?
        DAG.getUNDEF(Agg.getNode()->getValueType(Agg.getResNo() + i)) :
        SDValue(Agg.getNode(), Agg.getResNo() + i);

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                           DAG.getVTList(ValValueVTs), Values));
}

void SelectionDAGBuilder::visitGetElementPtr(const User &I) {
  Value *Op0 = I.getOperand(0);
  // Note that the pointer operand may be a vector of pointers. Take the scalar
  // element which holds a pointer.
  unsigned AS = Op0->getType()->getScalarType()->getPointerAddressSpace();
  SDValue N = getValue(Op0);
  SDLoc dl = getCurSDLoc();
  auto &TLI = DAG.getTargetLoweringInfo();

  // Normalize Vector GEP - all scalar operands should be converted to the
  // splat vector.
  bool IsVectorGEP = I.getType()->isVectorTy();
  ElementCount VectorElementCount =
      IsVectorGEP ? cast<VectorType>(I.getType())->getElementCount()
                  : ElementCount::getFixed(0);

  if (IsVectorGEP && !N.getValueType().isVector()) {
    LLVMContext &Context = *DAG.getContext();
    EVT VT = EVT::getVectorVT(Context, N.getValueType(), VectorElementCount);
    N = DAG.getSplat(VT, dl, N);
  }

  for (gep_type_iterator GTI = gep_type_begin(&I), E = gep_type_end(&I);
       GTI != E; ++GTI) {
    const Value *Idx = GTI.getOperand();
    if (StructType *StTy = GTI.getStructTypeOrNull()) {
      unsigned Field = cast<Constant>(Idx)->getUniqueInteger().getZExtValue();
      if (Field) {
        // N = N + Offset
        uint64_t Offset =
            DAG.getDataLayout().getStructLayout(StTy)->getElementOffset(Field);

        // In an inbounds GEP with an offset that is nonnegative even when
        // interpreted as signed, assume there is no unsigned overflow.
        SDNodeFlags Flags;
        if (int64_t(Offset) >= 0 && cast<GEPOperator>(I).isInBounds())
          Flags.setNoUnsignedWrap(true);

        N = DAG.getNode(ISD::ADD, dl, N.getValueType(), N,
                        DAG.getConstant(Offset, dl, N.getValueType()), Flags);
      }
    } else {
      // IdxSize is the width of the arithmetic according to IR semantics.
      // In SelectionDAG, we may prefer to do arithmetic in a wider bitwidth
      // (and fix up the result later).
      unsigned IdxSize = DAG.getDataLayout().getIndexSizeInBits(AS);
      MVT IdxTy = MVT::getIntegerVT(IdxSize);
      TypeSize ElementSize =
          GTI.getSequentialElementStride(DAG.getDataLayout());
      // We intentionally mask away the high bits here; ElementSize may not
      // fit in IdxTy.
      APInt ElementMul(IdxSize, ElementSize.getKnownMinValue());
      bool ElementScalable = ElementSize.isScalable();

      // If this is a scalar constant or a splat vector of constants,
      // handle it quickly.
      const auto *C = dyn_cast<Constant>(Idx);
      if (C && isa<VectorType>(C->getType()))
        C = C->getSplatValue();

      const auto *CI = dyn_cast_or_null<ConstantInt>(C);
      if (CI && CI->isZero())
        continue;
      if (CI && !ElementScalable) {
        APInt Offs = ElementMul * CI->getValue().sextOrTrunc(IdxSize);
        LLVMContext &Context = *DAG.getContext();
        SDValue OffsVal;
        if (IsVectorGEP)
          OffsVal = DAG.getConstant(
              Offs, dl, EVT::getVectorVT(Context, IdxTy, VectorElementCount));
        else
          OffsVal = DAG.getConstant(Offs, dl, IdxTy);

        // In an inbounds GEP with an offset that is nonnegative even when
        // interpreted as signed, assume there is no unsigned overflow.
        SDNodeFlags Flags;
        if (Offs.isNonNegative() && cast<GEPOperator>(I).isInBounds())
          Flags.setNoUnsignedWrap(true);

        OffsVal = DAG.getSExtOrTrunc(OffsVal, dl, N.getValueType());

        N = DAG.getNode(ISD::ADD, dl, N.getValueType(), N, OffsVal, Flags);
        continue;
      }

      // N = N + Idx * ElementMul;
      SDValue IdxN = getValue(Idx);

      if (!IdxN.getValueType().isVector() && IsVectorGEP) {
        EVT VT = EVT::getVectorVT(*Context, IdxN.getValueType(),
                                  VectorElementCount);
        IdxN = DAG.getSplat(VT, dl, IdxN);
      }

      // If the index is smaller or larger than intptr_t, truncate or extend
      // it.
      IdxN = DAG.getSExtOrTrunc(IdxN, dl, N.getValueType());

      if (ElementScalable) {
        EVT VScaleTy = N.getValueType().getScalarType();
        SDValue VScale = DAG.getNode(
            ISD::VSCALE, dl, VScaleTy,
            DAG.getConstant(ElementMul.getZExtValue(), dl, VScaleTy));
        if (IsVectorGEP)
          VScale = DAG.getSplatVector(N.getValueType(), dl, VScale);
        IdxN = DAG.getNode(ISD::MUL, dl, N.getValueType(), IdxN, VScale);
      } else {
        // If this is a multiply by a power of two, turn it into a shl
        // immediately.  This is a very common case.
        if (ElementMul != 1) {
          if (ElementMul.isPowerOf2()) {
            unsigned Amt = ElementMul.logBase2();
            IdxN = DAG.getNode(ISD::SHL, dl,
                               N.getValueType(), IdxN,
                               DAG.getConstant(Amt, dl, IdxN.getValueType()));
          } else {
            SDValue Scale = DAG.getConstant(ElementMul.getZExtValue(), dl,
                                            IdxN.getValueType());
            IdxN = DAG.getNode(ISD::MUL, dl,
                               N.getValueType(), IdxN, Scale);
          }
        }
      }

      N = DAG.getNode(ISD::ADD, dl,
                      N.getValueType(), N, IdxN);
    }
  }

  MVT PtrTy = TLI.getPointerTy(DAG.getDataLayout(), AS);
  MVT PtrMemTy = TLI.getPointerMemTy(DAG.getDataLayout(), AS);
  if (IsVectorGEP) {
    PtrTy = MVT::getVectorVT(PtrTy, VectorElementCount);
    PtrMemTy = MVT::getVectorVT(PtrMemTy, VectorElementCount);
  }

  if (PtrMemTy != PtrTy && !cast<GEPOperator>(I).isInBounds())
    N = DAG.getPtrExtendInReg(N, dl, PtrMemTy);

  setValue(&I, N);
}

void SelectionDAGBuilder::visitAlloca(const AllocaInst &I) {
  // If this is a fixed sized alloca in the entry block of the function,
  // allocate it statically on the stack.
  if (FuncInfo.StaticAllocaMap.count(&I))
    return;   // getValue will auto-populate this.

  SDLoc dl = getCurSDLoc();
  Type *Ty = I.getAllocatedType();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  auto &DL = DAG.getDataLayout();
  TypeSize TySize = DL.getTypeAllocSize(Ty);
  MaybeAlign Alignment = std::max(DL.getPrefTypeAlign(Ty), I.getAlign());

  SDValue AllocSize = getValue(I.getArraySize());

  EVT IntPtr = TLI.getPointerTy(DL, I.getAddressSpace());
  if (AllocSize.getValueType() != IntPtr)
    AllocSize = DAG.getZExtOrTrunc(AllocSize, dl, IntPtr);

  if (TySize.isScalable())
    AllocSize = DAG.getNode(ISD::MUL, dl, IntPtr, AllocSize,
                            DAG.getVScale(dl, IntPtr,
                                          APInt(IntPtr.getScalarSizeInBits(),
                                                TySize.getKnownMinValue())));
  else {
    SDValue TySizeValue =
        DAG.getConstant(TySize.getFixedValue(), dl, MVT::getIntegerVT(64));
    AllocSize = DAG.getNode(ISD::MUL, dl, IntPtr, AllocSize,
                            DAG.getZExtOrTrunc(TySizeValue, dl, IntPtr));
  }

  // Handle alignment.  If the requested alignment is less than or equal to
  // the stack alignment, ignore it.  If the size is greater than or equal to
  // the stack alignment, we note this in the DYNAMIC_STACKALLOC node.
  Align StackAlign = DAG.getSubtarget().getFrameLowering()->getStackAlign();
  if (*Alignment <= StackAlign)
    Alignment = std::nullopt;

  const uint64_t StackAlignMask = StackAlign.value() - 1U;
  // Round the size of the allocation up to the stack alignment size
  // by add SA-1 to the size. This doesn't overflow because we're computing
  // an address inside an alloca.
  SDNodeFlags Flags;
  Flags.setNoUnsignedWrap(true);
  AllocSize = DAG.getNode(ISD::ADD, dl, AllocSize.getValueType(), AllocSize,
                          DAG.getConstant(StackAlignMask, dl, IntPtr), Flags);

  // Mask out the low bits for alignment purposes.
  AllocSize = DAG.getNode(ISD::AND, dl, AllocSize.getValueType(), AllocSize,
                          DAG.getConstant(~StackAlignMask, dl, IntPtr));

  SDValue Ops[] = {
      getRoot(), AllocSize,
      DAG.getConstant(Alignment ? Alignment->value() : 0, dl, IntPtr)};
  SDVTList VTs = DAG.getVTList(AllocSize.getValueType(), MVT::Other);
  SDValue DSA = DAG.getNode(ISD::DYNAMIC_STACKALLOC, dl, VTs, Ops);
  setValue(&I, DSA);
  DAG.setRoot(DSA.getValue(1));

  assert(FuncInfo.MF->getFrameInfo().hasVarSizedObjects());
}

static const MDNode *getRangeMetadata(const Instruction &I) {
  // If !noundef is not present, then !range violation results in a poison
  // value rather than immediate undefined behavior. In theory, transferring
  // these annotations to SDAG is fine, but in practice there are key SDAG
  // transforms that are known not to be poison-safe, such as folding logical
  // and/or to bitwise and/or. For now, only transfer !range if !noundef is
  // also present.
  if (!I.hasMetadata(LLVMContext::MD_noundef))
    return nullptr;
  return I.getMetadata(LLVMContext::MD_range);
}

static std::optional<ConstantRange> getRange(const Instruction &I) {
  if (const auto *CB = dyn_cast<CallBase>(&I)) {
    // see comment in getRangeMetadata about this check
    if (CB->hasRetAttr(Attribute::NoUndef))
      return CB->getRange();
  }
  if (const MDNode *Range = getRangeMetadata(I))
    return getConstantRangeFromMetadata(*Range);
  return std::nullopt;
}

void SelectionDAGBuilder::visitLoad(const LoadInst &I) {
  if (I.isAtomic())
    return visitAtomicLoad(I);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const Value *SV = I.getOperand(0);
  if (TLI.supportSwiftError()) {
    // Swifterror values can come from either a function parameter with
    // swifterror attribute or an alloca with swifterror attribute.
    if (const Argument *Arg = dyn_cast<Argument>(SV)) {
      if (Arg->hasSwiftErrorAttr())
        return visitLoadFromSwiftError(I);
    }

    if (const AllocaInst *Alloca = dyn_cast<AllocaInst>(SV)) {
      if (Alloca->isSwiftError())
        return visitLoadFromSwiftError(I);
    }
  }

  SDValue Ptr = getValue(SV);

  Type *Ty = I.getType();
  SmallVector<EVT, 4> ValueVTs, MemVTs;
  SmallVector<TypeSize, 4> Offsets;
  ComputeValueVTs(TLI, DAG.getDataLayout(), Ty, ValueVTs, &MemVTs, &Offsets);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0)
    return;

  Align Alignment = I.getAlign();
  AAMDNodes AAInfo = I.getAAMetadata();
  const MDNode *Ranges = getRangeMetadata(I);
  bool isVolatile = I.isVolatile();
  MachineMemOperand::Flags MMOFlags =
      TLI.getLoadMemOperandFlags(I, DAG.getDataLayout(), AC, LibInfo);

  SDValue Root;
  bool ConstantMemory = false;
  if (isVolatile)
    // Serialize volatile loads with other side effects.
    Root = getRoot();
  else if (NumValues > MaxParallelChains)
    Root = getMemoryRoot();
  else if (AA &&
           AA->pointsToConstantMemory(MemoryLocation(
               SV,
               LocationSize::precise(DAG.getDataLayout().getTypeStoreSize(Ty)),
               AAInfo))) {
    // Do not serialize (non-volatile) loads of constant memory with anything.
    Root = DAG.getEntryNode();
    ConstantMemory = true;
    MMOFlags |= MachineMemOperand::MOInvariant;
  } else {
    // Do not serialize non-volatile loads against each other.
    Root = DAG.getRoot();
  }

  SDLoc dl = getCurSDLoc();

  if (isVolatile)
    Root = TLI.prepareVolatileOrAtomicLoad(Root, dl, DAG);

  SmallVector<SDValue, 4> Values(NumValues);
  SmallVector<SDValue, 4> Chains(std::min(MaxParallelChains, NumValues));

  unsigned ChainI = 0;
  for (unsigned i = 0; i != NumValues; ++i, ++ChainI) {
    // Serializing loads here may result in excessive register pressure, and
    // TokenFactor places arbitrary choke points on the scheduler. SD scheduling
    // could recover a bit by hoisting nodes upward in the chain by recognizing
    // they are side-effect free or do not alias. The optimizer should really
    // avoid this case by converting large object/array copies to llvm.memcpy
    // (MaxParallelChains should always remain as failsafe).
    if (ChainI == MaxParallelChains) {
      assert(PendingLoads.empty() && "PendingLoads must be serialized first");
      SDValue Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                  ArrayRef(Chains.data(), ChainI));
      Root = Chain;
      ChainI = 0;
    }

    // TODO: MachinePointerInfo only supports a fixed length offset.
    MachinePointerInfo PtrInfo =
        !Offsets[i].isScalable() || Offsets[i].isZero()
            ? MachinePointerInfo(SV, Offsets[i].getKnownMinValue())
            : MachinePointerInfo();

    SDValue A = DAG.getObjectPtrOffset(dl, Ptr, Offsets[i]);
    SDValue L = DAG.getLoad(MemVTs[i], dl, Root, A, PtrInfo, Alignment,
                            MMOFlags, AAInfo, Ranges);
    Chains[ChainI] = L.getValue(1);

    if (MemVTs[i] != ValueVTs[i])
      L = DAG.getPtrExtOrTrunc(L, dl, ValueVTs[i]);

    Values[i] = L;
  }

  if (!ConstantMemory) {
    SDValue Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                ArrayRef(Chains.data(), ChainI));
    if (isVolatile)
      DAG.setRoot(Chain);
    else
      PendingLoads.push_back(Chain);
  }

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, dl,
                           DAG.getVTList(ValueVTs), Values));
}

void SelectionDAGBuilder::visitStoreToSwiftError(const StoreInst &I) {
  assert(DAG.getTargetLoweringInfo().supportSwiftError() &&
         "call visitStoreToSwiftError when backend supports swifterror");

  SmallVector<EVT, 4> ValueVTs;
  SmallVector<uint64_t, 4> Offsets;
  const Value *SrcV = I.getOperand(0);
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(),
                  SrcV->getType(), ValueVTs, &Offsets, 0);
  assert(ValueVTs.size() == 1 && Offsets[0] == 0 &&
         "expect a single EVT for swifterror");

  SDValue Src = getValue(SrcV);
  // Create a virtual register, then update the virtual register.
  Register VReg =
      SwiftError.getOrCreateVRegDefAt(&I, FuncInfo.MBB, I.getPointerOperand());
  // Chain, DL, Reg, N or Chain, DL, Reg, N, Glue
  // Chain can be getRoot or getControlRoot.
  SDValue CopyNode = DAG.getCopyToReg(getRoot(), getCurSDLoc(), VReg,
                                      SDValue(Src.getNode(), Src.getResNo()));
  DAG.setRoot(CopyNode);
}

void SelectionDAGBuilder::visitLoadFromSwiftError(const LoadInst &I) {
  assert(DAG.getTargetLoweringInfo().supportSwiftError() &&
         "call visitLoadFromSwiftError when backend supports swifterror");

  assert(!I.isVolatile() &&
         !I.hasMetadata(LLVMContext::MD_nontemporal) &&
         !I.hasMetadata(LLVMContext::MD_invariant_load) &&
         "Support volatile, non temporal, invariant for load_from_swift_error");

  const Value *SV = I.getOperand(0);
  Type *Ty = I.getType();
  assert(
      (!AA ||
       !AA->pointsToConstantMemory(MemoryLocation(
           SV, LocationSize::precise(DAG.getDataLayout().getTypeStoreSize(Ty)),
           I.getAAMetadata()))) &&
      "load_from_swift_error should not be constant memory");

  SmallVector<EVT, 4> ValueVTs;
  SmallVector<uint64_t, 4> Offsets;
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(), Ty,
                  ValueVTs, &Offsets, 0);
  assert(ValueVTs.size() == 1 && Offsets[0] == 0 &&
         "expect a single EVT for swifterror");

  // Chain, DL, Reg, VT, Glue or Chain, DL, Reg, VT
  SDValue L = DAG.getCopyFromReg(
      getRoot(), getCurSDLoc(),
      SwiftError.getOrCreateVRegUseAt(&I, FuncInfo.MBB, SV), ValueVTs[0]);

  setValue(&I, L);
}

void SelectionDAGBuilder::visitStore(const StoreInst &I) {
  if (I.isAtomic())
    return visitAtomicStore(I);

  const Value *SrcV = I.getOperand(0);
  const Value *PtrV = I.getOperand(1);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (TLI.supportSwiftError()) {
    // Swifterror values can come from either a function parameter with
    // swifterror attribute or an alloca with swifterror attribute.
    if (const Argument *Arg = dyn_cast<Argument>(PtrV)) {
      if (Arg->hasSwiftErrorAttr())
        return visitStoreToSwiftError(I);
    }

    if (const AllocaInst *Alloca = dyn_cast<AllocaInst>(PtrV)) {
      if (Alloca->isSwiftError())
        return visitStoreToSwiftError(I);
    }
  }

  SmallVector<EVT, 4> ValueVTs, MemVTs;
  SmallVector<TypeSize, 4> Offsets;
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(),
                  SrcV->getType(), ValueVTs, &MemVTs, &Offsets);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0)
    return;

  // Get the lowered operands. Note that we do this after
  // checking if NumResults is zero, because with zero results
  // the operands won't have values in the map.
  SDValue Src = getValue(SrcV);
  SDValue Ptr = getValue(PtrV);

  SDValue Root = I.isVolatile() ? getRoot() : getMemoryRoot();
  SmallVector<SDValue, 4> Chains(std::min(MaxParallelChains, NumValues));
  SDLoc dl = getCurSDLoc();
  Align Alignment = I.getAlign();
  AAMDNodes AAInfo = I.getAAMetadata();

  auto MMOFlags = TLI.getStoreMemOperandFlags(I, DAG.getDataLayout());

  unsigned ChainI = 0;
  for (unsigned i = 0; i != NumValues; ++i, ++ChainI) {
    // See visitLoad comments.
    if (ChainI == MaxParallelChains) {
      SDValue Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                  ArrayRef(Chains.data(), ChainI));
      Root = Chain;
      ChainI = 0;
    }

    // TODO: MachinePointerInfo only supports a fixed length offset.
    MachinePointerInfo PtrInfo =
        !Offsets[i].isScalable() || Offsets[i].isZero()
            ? MachinePointerInfo(PtrV, Offsets[i].getKnownMinValue())
            : MachinePointerInfo();

    SDValue Add = DAG.getObjectPtrOffset(dl, Ptr, Offsets[i]);
    SDValue Val = SDValue(Src.getNode(), Src.getResNo() + i);
    if (MemVTs[i] != ValueVTs[i])
      Val = DAG.getPtrExtOrTrunc(Val, dl, MemVTs[i]);
    SDValue St =
        DAG.getStore(Root, dl, Val, Add, PtrInfo, Alignment, MMOFlags, AAInfo);
    Chains[ChainI] = St;
  }

  SDValue StoreNode = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                  ArrayRef(Chains.data(), ChainI));
  setValue(&I, StoreNode);
  DAG.setRoot(StoreNode);
}

void SelectionDAGBuilder::visitMaskedStore(const CallInst &I,
                                           bool IsCompressing) {
  SDLoc sdl = getCurSDLoc();

  auto getMaskedStoreOps = [&](Value *&Ptr, Value *&Mask, Value *&Src0,
                               Align &Alignment) {
    // llvm.masked.store.*(Src0, Ptr, alignment, Mask)
    Src0 = I.getArgOperand(0);
    Ptr = I.getArgOperand(1);
    Alignment = cast<ConstantInt>(I.getArgOperand(2))->getAlignValue();
    Mask = I.getArgOperand(3);
  };
  auto getCompressingStoreOps = [&](Value *&Ptr, Value *&Mask, Value *&Src0,
                                    Align &Alignment) {
    // llvm.masked.compressstore.*(Src0, Ptr, Mask)
    Src0 = I.getArgOperand(0);
    Ptr = I.getArgOperand(1);
    Mask = I.getArgOperand(2);
    Alignment = I.getParamAlign(1).valueOrOne();
  };

  Value  *PtrOperand, *MaskOperand, *Src0Operand;
  Align Alignment;
  if (IsCompressing)
    getCompressingStoreOps(PtrOperand, MaskOperand, Src0Operand, Alignment);
  else
    getMaskedStoreOps(PtrOperand, MaskOperand, Src0Operand, Alignment);

  SDValue Ptr = getValue(PtrOperand);
  SDValue Src0 = getValue(Src0Operand);
  SDValue Mask = getValue(MaskOperand);
  SDValue Offset = DAG.getUNDEF(Ptr.getValueType());

  EVT VT = Src0.getValueType();

  auto MMOFlags = MachineMemOperand::MOStore;
  if (I.hasMetadata(LLVMContext::MD_nontemporal))
    MMOFlags |= MachineMemOperand::MONonTemporal;

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(PtrOperand), MMOFlags,
      LocationSize::beforeOrAfterPointer(), Alignment, I.getAAMetadata());

  const auto &TLI = DAG.getTargetLoweringInfo();
  const auto &TTI =
      TLI.getTargetMachine().getTargetTransformInfo(*I.getFunction());
  SDValue StoreNode =
      !IsCompressing &&
              TTI.hasConditionalLoadStoreForType(I.getArgOperand(0)->getType())
          ? TLI.visitMaskedStore(DAG, sdl, getMemoryRoot(), MMO, Ptr, Src0,
                                 Mask)
          : DAG.getMaskedStore(getMemoryRoot(), sdl, Src0, Ptr, Offset, Mask,
                               VT, MMO, ISD::UNINDEXED, /*Truncating=*/false,
                               IsCompressing);
  DAG.setRoot(StoreNode);
  setValue(&I, StoreNode);
}

// Get a uniform base for the Gather/Scatter intrinsic.
// The first argument of the Gather/Scatter intrinsic is a vector of pointers.
// We try to represent it as a base pointer + vector of indices.
// Usually, the vector of pointers comes from a 'getelementptr' instruction.
// The first operand of the GEP may be a single pointer or a vector of pointers
// Example:
//   %gep.ptr = getelementptr i32, <8 x i32*> %vptr, <8 x i32> %ind
//  or
//   %gep.ptr = getelementptr i32, i32* %ptr,        <8 x i32> %ind
// %res = call <8 x i32> @llvm.masked.gather.v8i32(<8 x i32*> %gep.ptr, ..
//
// When the first GEP operand is a single pointer - it is the uniform base we
// are looking for. If first operand of the GEP is a splat vector - we
// extract the splat value and use it as a uniform base.
// In all other cases the function returns 'false'.
static bool getUniformBase(const Value *Ptr, SDValue &Base, SDValue &Index,
                           ISD::MemIndexType &IndexType, SDValue &Scale,
                           SelectionDAGBuilder *SDB, const BasicBlock *CurBB,
                           uint64_t ElemSize) {
  SelectionDAG& DAG = SDB->DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const DataLayout &DL = DAG.getDataLayout();

  assert(Ptr->getType()->isVectorTy() && "Unexpected pointer type");

  // Handle splat constant pointer.
  if (auto *C = dyn_cast<Constant>(Ptr)) {
    C = C->getSplatValue();
    if (!C)
      return false;

    Base = SDB->getValue(C);

    ElementCount NumElts = cast<VectorType>(Ptr->getType())->getElementCount();
    EVT VT = EVT::getVectorVT(*DAG.getContext(), TLI.getPointerTy(DL), NumElts);
    Index = DAG.getConstant(0, SDB->getCurSDLoc(), VT);
    IndexType = ISD::SIGNED_SCALED;
    Scale = DAG.getTargetConstant(1, SDB->getCurSDLoc(), TLI.getPointerTy(DL));
    return true;
  }

  const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP || GEP->getParent() != CurBB)
    return false;

  if (GEP->getNumOperands() != 2)
    return false;

  const Value *BasePtr = GEP->getPointerOperand();
  const Value *IndexVal = GEP->getOperand(GEP->getNumOperands() - 1);

  // Make sure the base is scalar and the index is a vector.
  if (BasePtr->getType()->isVectorTy() || !IndexVal->getType()->isVectorTy())
    return false;

  TypeSize ScaleVal = DL.getTypeAllocSize(GEP->getResultElementType());
  if (ScaleVal.isScalable())
    return false;

  // Target may not support the required addressing mode.
  if (ScaleVal != 1 &&
      !TLI.isLegalScaleForGatherScatter(ScaleVal.getFixedValue(), ElemSize))
    return false;

  Base = SDB->getValue(BasePtr);
  Index = SDB->getValue(IndexVal);
  IndexType = ISD::SIGNED_SCALED;

  Scale =
      DAG.getTargetConstant(ScaleVal, SDB->getCurSDLoc(), TLI.getPointerTy(DL));
  return true;
}

void SelectionDAGBuilder::visitMaskedScatter(const CallInst &I) {
  SDLoc sdl = getCurSDLoc();

  // llvm.masked.scatter.*(Src0, Ptrs, alignment, Mask)
  const Value *Ptr = I.getArgOperand(1);
  SDValue Src0 = getValue(I.getArgOperand(0));
  SDValue Mask = getValue(I.getArgOperand(3));
  EVT VT = Src0.getValueType();
  Align Alignment = cast<ConstantInt>(I.getArgOperand(2))
                        ->getMaybeAlignValue()
                        .value_or(DAG.getEVTAlign(VT.getScalarType()));
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  SDValue Base;
  SDValue Index;
  ISD::MemIndexType IndexType;
  SDValue Scale;
  bool UniformBase = getUniformBase(Ptr, Base, Index, IndexType, Scale, this,
                                    I.getParent(), VT.getScalarStoreSize());

  unsigned AS = Ptr->getType()->getScalarType()->getPointerAddressSpace();
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(AS), MachineMemOperand::MOStore,
      LocationSize::beforeOrAfterPointer(), Alignment, I.getAAMetadata());
  if (!UniformBase) {
    Base = DAG.getConstant(0, sdl, TLI.getPointerTy(DAG.getDataLayout()));
    Index = getValue(Ptr);
    IndexType = ISD::SIGNED_SCALED;
    Scale = DAG.getTargetConstant(1, sdl, TLI.getPointerTy(DAG.getDataLayout()));
  }

  EVT IdxVT = Index.getValueType();
  EVT EltTy = IdxVT.getVectorElementType();
  if (TLI.shouldExtendGSIndex(IdxVT, EltTy)) {
    EVT NewIdxVT = IdxVT.changeVectorElementType(EltTy);
    Index = DAG.getNode(ISD::SIGN_EXTEND, sdl, NewIdxVT, Index);
  }

  SDValue Ops[] = { getMemoryRoot(), Src0, Mask, Base, Index, Scale };
  SDValue Scatter = DAG.getMaskedScatter(DAG.getVTList(MVT::Other), VT, sdl,
                                         Ops, MMO, IndexType, false);
  DAG.setRoot(Scatter);
  setValue(&I, Scatter);
}

void SelectionDAGBuilder::visitMaskedLoad(const CallInst &I, bool IsExpanding) {
  SDLoc sdl = getCurSDLoc();

  auto getMaskedLoadOps = [&](Value *&Ptr, Value *&Mask, Value *&Src0,
                              Align &Alignment) {
    // @llvm.masked.load.*(Ptr, alignment, Mask, Src0)
    Ptr = I.getArgOperand(0);
    Alignment = cast<ConstantInt>(I.getArgOperand(1))->getAlignValue();
    Mask = I.getArgOperand(2);
    Src0 = I.getArgOperand(3);
  };
  auto getExpandingLoadOps = [&](Value *&Ptr, Value *&Mask, Value *&Src0,
                                 Align &Alignment) {
    // @llvm.masked.expandload.*(Ptr, Mask, Src0)
    Ptr = I.getArgOperand(0);
    Alignment = I.getParamAlign(0).valueOrOne();
    Mask = I.getArgOperand(1);
    Src0 = I.getArgOperand(2);
  };

  Value  *PtrOperand, *MaskOperand, *Src0Operand;
  Align Alignment;
  if (IsExpanding)
    getExpandingLoadOps(PtrOperand, MaskOperand, Src0Operand, Alignment);
  else
    getMaskedLoadOps(PtrOperand, MaskOperand, Src0Operand, Alignment);

  SDValue Ptr = getValue(PtrOperand);
  SDValue Src0 = getValue(Src0Operand);
  SDValue Mask = getValue(MaskOperand);
  SDValue Offset = DAG.getUNDEF(Ptr.getValueType());

  EVT VT = Src0.getValueType();
  AAMDNodes AAInfo = I.getAAMetadata();
  const MDNode *Ranges = getRangeMetadata(I);

  // Do not serialize masked loads of constant memory with anything.
  MemoryLocation ML = MemoryLocation::getAfter(PtrOperand, AAInfo);
  bool AddToChain = !AA || !AA->pointsToConstantMemory(ML);

  SDValue InChain = AddToChain ? DAG.getRoot() : DAG.getEntryNode();

  auto MMOFlags = MachineMemOperand::MOLoad;
  if (I.hasMetadata(LLVMContext::MD_nontemporal))
    MMOFlags |= MachineMemOperand::MONonTemporal;

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(PtrOperand), MMOFlags,
      LocationSize::beforeOrAfterPointer(), Alignment, AAInfo, Ranges);

  const auto &TLI = DAG.getTargetLoweringInfo();
  const auto &TTI =
      TLI.getTargetMachine().getTargetTransformInfo(*I.getFunction());
  // The Load/Res may point to different values and both of them are output
  // variables.
  SDValue Load;
  SDValue Res;
  if (!IsExpanding &&
      TTI.hasConditionalLoadStoreForType(Src0Operand->getType()))
    Res = TLI.visitMaskedLoad(DAG, sdl, InChain, MMO, Load, Ptr, Src0, Mask);
  else
    Res = Load =
        DAG.getMaskedLoad(VT, sdl, InChain, Ptr, Offset, Mask, Src0, VT, MMO,
                          ISD::UNINDEXED, ISD::NON_EXTLOAD, IsExpanding);
  if (AddToChain)
    PendingLoads.push_back(Load.getValue(1));
  setValue(&I, Res);
}

void SelectionDAGBuilder::visitMaskedGather(const CallInst &I) {
  SDLoc sdl = getCurSDLoc();

  // @llvm.masked.gather.*(Ptrs, alignment, Mask, Src0)
  const Value *Ptr = I.getArgOperand(0);
  SDValue Src0 = getValue(I.getArgOperand(3));
  SDValue Mask = getValue(I.getArgOperand(2));

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  Align Alignment = cast<ConstantInt>(I.getArgOperand(1))
                        ->getMaybeAlignValue()
                        .value_or(DAG.getEVTAlign(VT.getScalarType()));

  const MDNode *Ranges = getRangeMetadata(I);

  SDValue Root = DAG.getRoot();
  SDValue Base;
  SDValue Index;
  ISD::MemIndexType IndexType;
  SDValue Scale;
  bool UniformBase = getUniformBase(Ptr, Base, Index, IndexType, Scale, this,
                                    I.getParent(), VT.getScalarStoreSize());
  unsigned AS = Ptr->getType()->getScalarType()->getPointerAddressSpace();
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(AS), MachineMemOperand::MOLoad,
      LocationSize::beforeOrAfterPointer(), Alignment, I.getAAMetadata(),
      Ranges);

  if (!UniformBase) {
    Base = DAG.getConstant(0, sdl, TLI.getPointerTy(DAG.getDataLayout()));
    Index = getValue(Ptr);
    IndexType = ISD::SIGNED_SCALED;
    Scale = DAG.getTargetConstant(1, sdl, TLI.getPointerTy(DAG.getDataLayout()));
  }

  EVT IdxVT = Index.getValueType();
  EVT EltTy = IdxVT.getVectorElementType();
  if (TLI.shouldExtendGSIndex(IdxVT, EltTy)) {
    EVT NewIdxVT = IdxVT.changeVectorElementType(EltTy);
    Index = DAG.getNode(ISD::SIGN_EXTEND, sdl, NewIdxVT, Index);
  }

  SDValue Ops[] = { Root, Src0, Mask, Base, Index, Scale };
  SDValue Gather = DAG.getMaskedGather(DAG.getVTList(VT, MVT::Other), VT, sdl,
                                       Ops, MMO, IndexType, ISD::NON_EXTLOAD);

  PendingLoads.push_back(Gather.getValue(1));
  setValue(&I, Gather);
}

void SelectionDAGBuilder::visitAtomicCmpXchg(const AtomicCmpXchgInst &I) {
  SDLoc dl = getCurSDLoc();
  AtomicOrdering SuccessOrdering = I.getSuccessOrdering();
  AtomicOrdering FailureOrdering = I.getFailureOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  MVT MemVT = getValue(I.getCompareOperand()).getSimpleValueType();
  SDVTList VTs = DAG.getVTList(MemVT, MVT::i1, MVT::Other);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  auto Flags = TLI.getAtomicMemOperandFlags(I, DAG.getDataLayout());

  MachineFunction &MF = DAG.getMachineFunction();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo(I.getPointerOperand()), Flags,
      LocationSize::precise(MemVT.getStoreSize()), DAG.getEVTAlign(MemVT),
      AAMDNodes(), nullptr, SSID, SuccessOrdering, FailureOrdering);

  SDValue L = DAG.getAtomicCmpSwap(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS,
                                   dl, MemVT, VTs, InChain,
                                   getValue(I.getPointerOperand()),
                                   getValue(I.getCompareOperand()),
                                   getValue(I.getNewValOperand()), MMO);

  SDValue OutChain = L.getValue(2);

  setValue(&I, L);
  DAG.setRoot(OutChain);
}

void SelectionDAGBuilder::visitAtomicRMW(const AtomicRMWInst &I) {
  SDLoc dl = getCurSDLoc();
  ISD::NodeType NT;
  switch (I.getOperation()) {
  default: llvm_unreachable("Unknown atomicrmw operation");
  case AtomicRMWInst::Xchg: NT = ISD::ATOMIC_SWAP; break;
  case AtomicRMWInst::Add:  NT = ISD::ATOMIC_LOAD_ADD; break;
  case AtomicRMWInst::Sub:  NT = ISD::ATOMIC_LOAD_SUB; break;
  case AtomicRMWInst::And:  NT = ISD::ATOMIC_LOAD_AND; break;
  case AtomicRMWInst::Nand: NT = ISD::ATOMIC_LOAD_NAND; break;
  case AtomicRMWInst::Or:   NT = ISD::ATOMIC_LOAD_OR; break;
  case AtomicRMWInst::Xor:  NT = ISD::ATOMIC_LOAD_XOR; break;
  case AtomicRMWInst::Max:  NT = ISD::ATOMIC_LOAD_MAX; break;
  case AtomicRMWInst::Min:  NT = ISD::ATOMIC_LOAD_MIN; break;
  case AtomicRMWInst::UMax: NT = ISD::ATOMIC_LOAD_UMAX; break;
  case AtomicRMWInst::UMin: NT = ISD::ATOMIC_LOAD_UMIN; break;
  case AtomicRMWInst::FAdd: NT = ISD::ATOMIC_LOAD_FADD; break;
  case AtomicRMWInst::FSub: NT = ISD::ATOMIC_LOAD_FSUB; break;
  case AtomicRMWInst::FMax: NT = ISD::ATOMIC_LOAD_FMAX; break;
  case AtomicRMWInst::FMin: NT = ISD::ATOMIC_LOAD_FMIN; break;
  case AtomicRMWInst::UIncWrap:
    NT = ISD::ATOMIC_LOAD_UINC_WRAP;
    break;
  case AtomicRMWInst::UDecWrap:
    NT = ISD::ATOMIC_LOAD_UDEC_WRAP;
    break;
  }
  AtomicOrdering Ordering = I.getOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  auto MemVT = getValue(I.getValOperand()).getSimpleValueType();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  auto Flags = TLI.getAtomicMemOperandFlags(I, DAG.getDataLayout());

  MachineFunction &MF = DAG.getMachineFunction();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo(I.getPointerOperand()), Flags,
      LocationSize::precise(MemVT.getStoreSize()), DAG.getEVTAlign(MemVT),
      AAMDNodes(), nullptr, SSID, Ordering);

  SDValue L =
    DAG.getAtomic(NT, dl, MemVT, InChain,
                  getValue(I.getPointerOperand()), getValue(I.getValOperand()),
                  MMO);

  SDValue OutChain = L.getValue(1);

  setValue(&I, L);
  DAG.setRoot(OutChain);
}

void SelectionDAGBuilder::visitFence(const FenceInst &I) {
  SDLoc dl = getCurSDLoc();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Ops[3];
  Ops[0] = getRoot();
  Ops[1] = DAG.getTargetConstant((unsigned)I.getOrdering(), dl,
                                 TLI.getFenceOperandTy(DAG.getDataLayout()));
  Ops[2] = DAG.getTargetConstant(I.getSyncScopeID(), dl,
                                 TLI.getFenceOperandTy(DAG.getDataLayout()));
  SDValue N = DAG.getNode(ISD::ATOMIC_FENCE, dl, MVT::Other, Ops);
  setValue(&I, N);
  DAG.setRoot(N);
}

void SelectionDAGBuilder::visitAtomicLoad(const LoadInst &I) {
  SDLoc dl = getCurSDLoc();
  AtomicOrdering Order = I.getOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  EVT MemVT = TLI.getMemValueType(DAG.getDataLayout(), I.getType());

  if (!TLI.supportsUnalignedAtomics() &&
      I.getAlign().value() < MemVT.getSizeInBits() / 8)
    report_fatal_error("Cannot generate unaligned atomic load");

  auto Flags = TLI.getLoadMemOperandFlags(I, DAG.getDataLayout(), AC, LibInfo);

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(I.getPointerOperand()), Flags,
      LocationSize::precise(MemVT.getStoreSize()), I.getAlign(), AAMDNodes(),
      nullptr, SSID, Order);

  InChain = TLI.prepareVolatileOrAtomicLoad(InChain, dl, DAG);

  SDValue Ptr = getValue(I.getPointerOperand());
  SDValue L = DAG.getAtomic(ISD::ATOMIC_LOAD, dl, MemVT, MemVT, InChain,
                            Ptr, MMO);

  SDValue OutChain = L.getValue(1);
  if (MemVT != VT)
    L = DAG.getPtrExtOrTrunc(L, dl, VT);

  setValue(&I, L);
  DAG.setRoot(OutChain);
}

void SelectionDAGBuilder::visitAtomicStore(const StoreInst &I) {
  SDLoc dl = getCurSDLoc();

  AtomicOrdering Ordering = I.getOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT MemVT =
      TLI.getMemValueType(DAG.getDataLayout(), I.getValueOperand()->getType());

  if (!TLI.supportsUnalignedAtomics() &&
      I.getAlign().value() < MemVT.getSizeInBits() / 8)
    report_fatal_error("Cannot generate unaligned atomic store");

  auto Flags = TLI.getStoreMemOperandFlags(I, DAG.getDataLayout());

  MachineFunction &MF = DAG.getMachineFunction();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo(I.getPointerOperand()), Flags,
      LocationSize::precise(MemVT.getStoreSize()), I.getAlign(), AAMDNodes(),
      nullptr, SSID, Ordering);

  SDValue Val = getValue(I.getValueOperand());
  if (Val.getValueType() != MemVT)
    Val = DAG.getPtrExtOrTrunc(Val, dl, MemVT);
  SDValue Ptr = getValue(I.getPointerOperand());

  SDValue OutChain =
      DAG.getAtomic(ISD::ATOMIC_STORE, dl, MemVT, InChain, Val, Ptr, MMO);

  setValue(&I, OutChain);
  DAG.setRoot(OutChain);
}

/// visitTargetIntrinsic - Lower a call of a target intrinsic to an INTRINSIC
/// node.
void SelectionDAGBuilder::visitTargetIntrinsic(const CallInst &I,
                                               unsigned Intrinsic) {
  // Ignore the callsite's attributes. A specific call site may be marked with
  // readnone, but the lowering code will expect the chain based on the
  // definition.
  const Function *F = I.getCalledFunction();
  bool HasChain = !F->doesNotAccessMemory();
  bool OnlyLoad = HasChain && F->onlyReadsMemory();

  // Build the operand list.
  SmallVector<SDValue, 8> Ops;
  if (HasChain) {  // If this intrinsic has side-effects, chainify it.
    if (OnlyLoad) {
      // We don't need to serialize loads against other loads.
      Ops.push_back(DAG.getRoot());
    } else {
      Ops.push_back(getRoot());
    }
  }

  // Info is set by getTgtMemIntrinsic
  TargetLowering::IntrinsicInfo Info;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  bool IsTgtIntrinsic = TLI.getTgtMemIntrinsic(Info, I,
                                               DAG.getMachineFunction(),
                                               Intrinsic);

  // Add the intrinsic ID as an integer operand if it's not a target intrinsic.
  if (!IsTgtIntrinsic || Info.opc == ISD::INTRINSIC_VOID ||
      Info.opc == ISD::INTRINSIC_W_CHAIN)
    Ops.push_back(DAG.getTargetConstant(Intrinsic, getCurSDLoc(),
                                        TLI.getPointerTy(DAG.getDataLayout())));

  // Add all operands of the call to the operand list.
  for (unsigned i = 0, e = I.arg_size(); i != e; ++i) {
    const Value *Arg = I.getArgOperand(i);
    if (!I.paramHasAttr(i, Attribute::ImmArg)) {
      Ops.push_back(getValue(Arg));
      continue;
    }

    // Use TargetConstant instead of a regular constant for immarg.
    EVT VT = TLI.getValueType(DAG.getDataLayout(), Arg->getType(), true);
    if (const ConstantInt *CI = dyn_cast<ConstantInt>(Arg)) {
      assert(CI->getBitWidth() <= 64 &&
             "large intrinsic immediates not handled");
      Ops.push_back(DAG.getTargetConstant(*CI, SDLoc(), VT));
    } else {
      Ops.push_back(
          DAG.getTargetConstantFP(*cast<ConstantFP>(Arg), SDLoc(), VT));
    }
  }

  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), I.getType(), ValueVTs);

  if (HasChain)
    ValueVTs.push_back(MVT::Other);

  SDVTList VTs = DAG.getVTList(ValueVTs);

  // Propagate fast-math-flags from IR to node(s).
  SDNodeFlags Flags;
  if (auto *FPMO = dyn_cast<FPMathOperator>(&I))
    Flags.copyFMF(*FPMO);
  SelectionDAG::FlagInserter FlagsInserter(DAG, Flags);

  // Create the node.
  SDValue Result;

  if (auto Bundle = I.getOperandBundle(LLVMContext::OB_convergencectrl)) {
    auto *Token = Bundle->Inputs[0].get();
    SDValue ConvControlToken = getValue(Token);
    assert(Ops.back().getValueType() != MVT::Glue &&
           "Did not expected another glue node here.");
    ConvControlToken =
        DAG.getNode(ISD::CONVERGENCECTRL_GLUE, {}, MVT::Glue, ConvControlToken);
    Ops.push_back(ConvControlToken);
  }

  // In some cases, custom collection of operands from CallInst I may be needed.
  TLI.CollectTargetIntrinsicOperands(I, Ops, DAG);
  if (IsTgtIntrinsic) {
    // This is target intrinsic that touches memory
    //
    // TODO: We currently just fallback to address space 0 if getTgtMemIntrinsic
    //       didn't yield anything useful.
    MachinePointerInfo MPI;
    if (Info.ptrVal)
      MPI = MachinePointerInfo(Info.ptrVal, Info.offset);
    else if (Info.fallbackAddressSpace)
      MPI = MachinePointerInfo(*Info.fallbackAddressSpace);
    Result = DAG.getMemIntrinsicNode(Info.opc, getCurSDLoc(), VTs, Ops,
                                     Info.memVT, MPI, Info.align, Info.flags,
                                     Info.size, I.getAAMetadata());
  } else if (!HasChain) {
    Result = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, getCurSDLoc(), VTs, Ops);
  } else if (!I.getType()->isVoidTy()) {
    Result = DAG.getNode(ISD::INTRINSIC_W_CHAIN, getCurSDLoc(), VTs, Ops);
  } else {
    Result = DAG.getNode(ISD::INTRINSIC_VOID, getCurSDLoc(), VTs, Ops);
  }

  if (HasChain) {
    SDValue Chain = Result.getValue(Result.getNode()->getNumValues()-1);
    if (OnlyLoad)
      PendingLoads.push_back(Chain);
    else
      DAG.setRoot(Chain);
  }

  if (!I.getType()->isVoidTy()) {
    if (!isa<VectorType>(I.getType()))
      Result = lowerRangeToAssertZExt(DAG, I, Result);

    MaybeAlign Alignment = I.getRetAlign();

    // Insert `assertalign` node if there's an alignment.
    if (InsertAssertAlign && Alignment) {
      Result =
          DAG.getAssertAlign(getCurSDLoc(), Result, Alignment.valueOrOne());
    }
  }

  setValue(&I, Result);
}

/// GetSignificand - Get the significand and build it into a floating-point
/// number with exponent of 1:
///
///   Op = (Op & 0x007fffff) | 0x3f800000;
///
/// where Op is the hexadecimal representation of floating point value.
static SDValue GetSignificand(SelectionDAG &DAG, SDValue Op, const SDLoc &dl) {
  SDValue t1 = DAG.getNode(ISD::AND, dl, MVT::i32, Op,
                           DAG.getConstant(0x007fffff, dl, MVT::i32));
  SDValue t2 = DAG.getNode(ISD::OR, dl, MVT::i32, t1,
                           DAG.getConstant(0x3f800000, dl, MVT::i32));
  return DAG.getNode(ISD::BITCAST, dl, MVT::f32, t2);
}

/// GetExponent - Get the exponent:
///
///   (float)(int)(((Op & 0x7f800000) >> 23) - 127);
///
/// where Op is the hexadecimal representation of floating point value.
static SDValue GetExponent(SelectionDAG &DAG, SDValue Op,
                           const TargetLowering &TLI, const SDLoc &dl) {
  SDValue t0 = DAG.getNode(ISD::AND, dl, MVT::i32, Op,
                           DAG.getConstant(0x7f800000, dl, MVT::i32));
  SDValue t1 = DAG.getNode(
      ISD::SRL, dl, MVT::i32, t0,
      DAG.getConstant(23, dl,
                      TLI.getShiftAmountTy(MVT::i32, DAG.getDataLayout())));
  SDValue t2 = DAG.getNode(ISD::SUB, dl, MVT::i32, t1,
                           DAG.getConstant(127, dl, MVT::i32));
  return DAG.getNode(ISD::SINT_TO_FP, dl, MVT::f32, t2);
}

/// getF32Constant - Get 32-bit floating point constant.
static SDValue getF32Constant(SelectionDAG &DAG, unsigned Flt,
                              const SDLoc &dl) {
  return DAG.getConstantFP(APFloat(APFloat::IEEEsingle(), APInt(32, Flt)), dl,
                           MVT::f32);
}

static SDValue getLimitedPrecisionExp2(SDValue t0, const SDLoc &dl,
                                       SelectionDAG &DAG) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  //   IntegerPartOfX = ((int32_t)(t0);
  SDValue IntegerPartOfX = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32, t0);

  //   FractionalPartOfX = t0 - (float)IntegerPartOfX;
  SDValue t1 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::f32, IntegerPartOfX);
  SDValue X = DAG.getNode(ISD::FSUB, dl, MVT::f32, t0, t1);

  //   IntegerPartOfX <<= 23;
  IntegerPartOfX =
      DAG.getNode(ISD::SHL, dl, MVT::i32, IntegerPartOfX,
                  DAG.getConstant(23, dl,
                                  DAG.getTargetLoweringInfo().getShiftAmountTy(
                                      MVT::i32, DAG.getDataLayout())));

  SDValue TwoToFractionalPartOfX;
  if (LimitFloatPrecision <= 6) {
    // For floating-point precision of 6:
    //
    //   TwoToFractionalPartOfX =
    //     0.997535578f +
    //       (0.735607626f + 0.252464424f * x) * x;
    //
    // error 0.0144103317, which is 6 bits
    SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                             getF32Constant(DAG, 0x3e814304, dl));
    SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                             getF32Constant(DAG, 0x3f3c50c8, dl));
    SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
    TwoToFractionalPartOfX = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                                         getF32Constant(DAG, 0x3f7f5e7e, dl));
  } else if (LimitFloatPrecision <= 12) {
    // For floating-point precision of 12:
    //
    //   TwoToFractionalPartOfX =
    //     0.999892986f +
    //       (0.696457318f +
    //         (0.224338339f + 0.792043434e-1f * x) * x) * x;
    //
    // error 0.000107046256, which is 13 to 14 bits
    SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                             getF32Constant(DAG, 0x3da235e3, dl));
    SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                             getF32Constant(DAG, 0x3e65b8f3, dl));
    SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
    SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                             getF32Constant(DAG, 0x3f324b07, dl));
    SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
    TwoToFractionalPartOfX = DAG.getNode(ISD::FADD, dl, MVT::f32, t6,
                                         getF32Constant(DAG, 0x3f7ff8fd, dl));
  } else { // LimitFloatPrecision <= 18
    // For floating-point precision of 18:
    //
    //   TwoToFractionalPartOfX =
    //     0.999999982f +
    //       (0.693148872f +
    //         (0.240227044f +
    //           (0.554906021e-1f +
    //             (0.961591928e-2f +
    //               (0.136028312e-2f + 0.157059148e-3f *x)*x)*x)*x)*x)*x;
    // error 2.47208000*10^(-7), which is better than 18 bits
    SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                             getF32Constant(DAG, 0x3924b03e, dl));
    SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                             getF32Constant(DAG, 0x3ab24b87, dl));
    SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
    SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                             getF32Constant(DAG, 0x3c1d8c17, dl));
    SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
    SDValue t7 = DAG.getNode(ISD::FADD, dl, MVT::f32, t6,
                             getF32Constant(DAG, 0x3d634a1d, dl));
    SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
    SDValue t9 = DAG.getNode(ISD::FADD, dl, MVT::f32, t8,
                             getF32Constant(DAG, 0x3e75fe14, dl));
    SDValue t10 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t9, X);
    SDValue t11 = DAG.getNode(ISD::FADD, dl, MVT::f32, t10,
                              getF32Constant(DAG, 0x3f317234, dl));
    SDValue t12 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t11, X);
    TwoToFractionalPartOfX = DAG.getNode(ISD::FADD, dl, MVT::f32, t12,
                                         getF32Constant(DAG, 0x3f800000, dl));
  }

  // Add the exponent into the result in integer domain.
  SDValue t13 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, TwoToFractionalPartOfX);
  return DAG.getNode(ISD::BITCAST, dl, MVT::f32,
                     DAG.getNode(ISD::ADD, dl, MVT::i32, t13, IntegerPartOfX));
}

/// expandExp - Lower an exp intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandExp(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                         const TargetLowering &TLI, SDNodeFlags Flags) {
  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {

    // Put the exponent in the right bit position for later addition to the
    // final result:
    //
    // t0 = Op * log2(e)

    // TODO: What fast-math-flags should be set here?
    SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, Op,
                             DAG.getConstantFP(numbers::log2ef, dl, MVT::f32));
    return getLimitedPrecisionExp2(t0, dl, DAG);
  }

  // No special expansion.
  return DAG.getNode(ISD::FEXP, dl, Op.getValueType(), Op, Flags);
}

/// expandLog - Lower a log intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandLog(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                         const TargetLowering &TLI, SDNodeFlags Flags) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    SDValue Op1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op);

    // Scale the exponent by log(2).
    SDValue Exp = GetExponent(DAG, Op1, TLI, dl);
    SDValue LogOfExponent =
        DAG.getNode(ISD::FMUL, dl, MVT::f32, Exp,
                    DAG.getConstantFP(numbers::ln2f, dl, MVT::f32));

    // Get the significand and build it into a floating-point number with
    // exponent of 1.
    SDValue X = GetSignificand(DAG, Op1, dl);

    SDValue LogOfMantissa;
    if (LimitFloatPrecision <= 6) {
      // For floating-point precision of 6:
      //
      //   LogofMantissa =
      //     -1.1609546f +
      //       (1.4034025f - 0.23903021f * x) * x;
      //
      // error 0.0034276066, which is better than 8 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbe74c456, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3fb3a2b1, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      LogOfMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                                  getF32Constant(DAG, 0x3f949a29, dl));
    } else if (LimitFloatPrecision <= 12) {
      // For floating-point precision of 12:
      //
      //   LogOfMantissa =
      //     -1.7417939f +
      //       (2.8212026f +
      //         (-1.4699568f +
      //           (0.44717955f - 0.56570851e-1f * x) * x) * x) * x;
      //
      // error 0.000061011436, which is 14 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbd67b6d6, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3ee4f4b8, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3fbc278b, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x40348e95, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      LogOfMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                                  getF32Constant(DAG, 0x3fdef31a, dl));
    } else { // LimitFloatPrecision <= 18
      // For floating-point precision of 18:
      //
      //   LogOfMantissa =
      //     -2.1072184f +
      //       (4.2372794f +
      //         (-3.7029485f +
      //           (2.2781945f +
      //             (-0.87823314f +
      //               (0.19073739f - 0.17809712e-1f * x) * x) * x) * x) * x)*x;
      //
      // error 0.0000023660568, which is better than 18 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbc91e5ac, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3e4350aa, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3f60d3e3, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x4011cdf0, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      SDValue t7 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                               getF32Constant(DAG, 0x406cfd1c, dl));
      SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
      SDValue t9 = DAG.getNode(ISD::FADD, dl, MVT::f32, t8,
                               getF32Constant(DAG, 0x408797cb, dl));
      SDValue t10 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t9, X);
      LogOfMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t10,
                                  getF32Constant(DAG, 0x4006dcab, dl));
    }

    return DAG.getNode(ISD::FADD, dl, MVT::f32, LogOfExponent, LogOfMantissa);
  }

  // No special expansion.
  return DAG.getNode(ISD::FLOG, dl, Op.getValueType(), Op, Flags);
}

/// expandLog2 - Lower a log2 intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandLog2(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                          const TargetLowering &TLI, SDNodeFlags Flags) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    SDValue Op1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op);

    // Get the exponent.
    SDValue LogOfExponent = GetExponent(DAG, Op1, TLI, dl);

    // Get the significand and build it into a floating-point number with
    // exponent of 1.
    SDValue X = GetSignificand(DAG, Op1, dl);

    // Different possible minimax approximations of significand in
    // floating-point for various degrees of accuracy over [1,2].
    SDValue Log2ofMantissa;
    if (LimitFloatPrecision <= 6) {
      // For floating-point precision of 6:
      //
      //   Log2ofMantissa = -1.6749035f + (2.0246817f - .34484768f * x) * x;
      //
      // error 0.0049451742, which is more than 7 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbeb08fe0, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x40019463, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      Log2ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                                   getF32Constant(DAG, 0x3fd6633d, dl));
    } else if (LimitFloatPrecision <= 12) {
      // For floating-point precision of 12:
      //
      //   Log2ofMantissa =
      //     -2.51285454f +
      //       (4.07009056f +
      //         (-2.12067489f +
      //           (.645142248f - 0.816157886e-1f * x) * x) * x) * x;
      //
      // error 0.0000876136000, which is better than 13 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbda7262e, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3f25280b, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x4007b923, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x40823e2f, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      Log2ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                                   getF32Constant(DAG, 0x4020d29c, dl));
    } else { // LimitFloatPrecision <= 18
      // For floating-point precision of 18:
      //
      //   Log2ofMantissa =
      //     -3.0400495f +
      //       (6.1129976f +
      //         (-5.3420409f +
      //           (3.2865683f +
      //             (-1.2669343f +
      //               (0.27515199f -
      //                 0.25691327e-1f * x) * x) * x) * x) * x) * x;
      //
      // error 0.0000018516, which is better than 18 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbcd2769e, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3e8ce0b9, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3fa22ae7, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x40525723, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      SDValue t7 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                               getF32Constant(DAG, 0x40aaf200, dl));
      SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
      SDValue t9 = DAG.getNode(ISD::FADD, dl, MVT::f32, t8,
                               getF32Constant(DAG, 0x40c39dad, dl));
      SDValue t10 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t9, X);
      Log2ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t10,
                                   getF32Constant(DAG, 0x4042902c, dl));
    }

    return DAG.getNode(ISD::FADD, dl, MVT::f32, LogOfExponent, Log2ofMantissa);
  }

  // No special expansion.
  return DAG.getNode(ISD::FLOG2, dl, Op.getValueType(), Op, Flags);
}

/// expandLog10 - Lower a log10 intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandLog10(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                           const TargetLowering &TLI, SDNodeFlags Flags) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    SDValue Op1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op);

    // Scale the exponent by log10(2) [0.30102999f].
    SDValue Exp = GetExponent(DAG, Op1, TLI, dl);
    SDValue LogOfExponent = DAG.getNode(ISD::FMUL, dl, MVT::f32, Exp,
                                        getF32Constant(DAG, 0x3e9a209a, dl));

    // Get the significand and build it into a floating-point number with
    // exponent of 1.
    SDValue X = GetSignificand(DAG, Op1, dl);

    SDValue Log10ofMantissa;
    if (LimitFloatPrecision <= 6) {
      // For floating-point precision of 6:
      //
      //   Log10ofMantissa =
      //     -0.50419619f +
      //       (0.60948995f - 0.10380950f * x) * x;
      //
      // error 0.0014886165, which is 6 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbdd49a13, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3f1c0789, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      Log10ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                                    getF32Constant(DAG, 0x3f011300, dl));
    } else if (LimitFloatPrecision <= 12) {
      // For floating-point precision of 12:
      //
      //   Log10ofMantissa =
      //     -0.64831180f +
      //       (0.91751397f +
      //         (-0.31664806f + 0.47637168e-1f * x) * x) * x;
      //
      // error 0.00019228036, which is better than 12 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0x3d431f31, dl));
      SDValue t1 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3ea21fb2, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3f6ae232, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      Log10ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t4,
                                    getF32Constant(DAG, 0x3f25f7c3, dl));
    } else { // LimitFloatPrecision <= 18
      // For floating-point precision of 18:
      //
      //   Log10ofMantissa =
      //     -0.84299375f +
      //       (1.5327582f +
      //         (-1.0688956f +
      //           (0.49102474f +
      //             (-0.12539807f + 0.13508273e-1f * x) * x) * x) * x) * x;
      //
      // error 0.0000037995730, which is better than 18 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0x3c5d51ce, dl));
      SDValue t1 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3e00685a, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3efb6798, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x3f88d192, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      SDValue t7 = DAG.getNode(ISD::FADD, dl, MVT::f32, t6,
                               getF32Constant(DAG, 0x3fc4316c, dl));
      SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
      Log10ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t8,
                                    getF32Constant(DAG, 0x3f57ce70, dl));
    }

    return DAG.getNode(ISD::FADD, dl, MVT::f32, LogOfExponent, Log10ofMantissa);
  }

  // No special expansion.
  return DAG.getNode(ISD::FLOG10, dl, Op.getValueType(), Op, Flags);
}

/// expandExp2 - Lower an exp2 intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandExp2(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                          const TargetLowering &TLI, SDNodeFlags Flags) {
  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18)
    return getLimitedPrecisionExp2(Op, dl, DAG);

  // No special expansion.
  return DAG.getNode(ISD::FEXP2, dl, Op.getValueType(), Op, Flags);
}

/// visitPow - Lower a pow intrinsic. Handles the special sequences for
/// limited-precision mode with x == 10.0f.
static SDValue expandPow(const SDLoc &dl, SDValue LHS, SDValue RHS,
                         SelectionDAG &DAG, const TargetLowering &TLI,
                         SDNodeFlags Flags) {
  bool IsExp10 = false;
  if (LHS.getValueType() == MVT::f32 && RHS.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    if (ConstantFPSDNode *LHSC = dyn_cast<ConstantFPSDNode>(LHS)) {
      APFloat Ten(10.0f);
      IsExp10 = LHSC->isExactlyValue(Ten);
    }
  }

  // TODO: What fast-math-flags should be set on the FMUL node?
  if (IsExp10) {
    // Put the exponent in the right bit position for later addition to the
    // final result:
    //
    //   #define LOG2OF10 3.3219281f
    //   t0 = Op * LOG2OF10;
    SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, RHS,
                             getF32Constant(DAG, 0x40549a78, dl));
    return getLimitedPrecisionExp2(t0, dl, DAG);
  }

  // No special expansion.
  return DAG.getNode(ISD::FPOW, dl, LHS.getValueType(), LHS, RHS, Flags);
}

/// ExpandPowI - Expand a llvm.powi intrinsic.
static SDValue ExpandPowI(const SDLoc &DL, SDValue LHS, SDValue RHS,
                          SelectionDAG &DAG) {
  // If RHS is a constant, we can expand this out to a multiplication tree if
  // it's beneficial on the target, otherwise we end up lowering to a call to
  // __powidf2 (for example).
  if (ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS)) {
    unsigned Val = RHSC->getSExtValue();

    // powi(x, 0) -> 1.0
    if (Val == 0)
      return DAG.getConstantFP(1.0, DL, LHS.getValueType());

    if (DAG.getTargetLoweringInfo().isBeneficialToExpandPowI(
            Val, DAG.shouldOptForSize())) {
      // Get the exponent as a positive value.
      if ((int)Val < 0)
        Val = -Val;
      // We use the simple binary decomposition method to generate the multiply
      // sequence.  There are more optimal ways to do this (for example,
      // powi(x,15) generates one more multiply than it should), but this has
      // the benefit of being both really simple and much better than a libcall.
      SDValue Res; // Logically starts equal to 1.0
      SDValue CurSquare = LHS;
      // TODO: Intrinsics should have fast-math-flags that propagate to these
      // nodes.
      while (Val) {
        if (Val & 1) {
          if (Res.getNode())
            Res =
                DAG.getNode(ISD::FMUL, DL, Res.getValueType(), Res, CurSquare);
          else
            Res = CurSquare; // 1.0*CurSquare.
        }

        CurSquare = DAG.getNode(ISD::FMUL, DL, CurSquare.getValueType(),
                                CurSquare, CurSquare);
        Val >>= 1;
      }

      // If the original was negative, invert the result, producing 1/(x*x*x).
      if (RHSC->getSExtValue() < 0)
        Res = DAG.getNode(ISD::FDIV, DL, LHS.getValueType(),
                          DAG.getConstantFP(1.0, DL, LHS.getValueType()), Res);
      return Res;
    }
  }

  // Otherwise, expand to a libcall.
  return DAG.getNode(ISD::FPOWI, DL, LHS.getValueType(), LHS, RHS);
}

static SDValue expandDivFix(unsigned Opcode, const SDLoc &DL,
                            SDValue LHS, SDValue RHS, SDValue Scale,
                            SelectionDAG &DAG, const TargetLowering &TLI) {
  EVT VT = LHS.getValueType();
  bool Signed = Opcode == ISD::SDIVFIX || Opcode == ISD::SDIVFIXSAT;
  bool Saturating = Opcode == ISD::SDIVFIXSAT || Opcode == ISD::UDIVFIXSAT;
  LLVMContext &Ctx = *DAG.getContext();

  // If the type is legal but the operation isn't, this node might survive all
  // the way to operation legalization. If we end up there and we do not have
  // the ability to widen the type (if VT*2 is not legal), we cannot expand the
  // node.

  // Coax the legalizer into expanding the node during type legalization instead
  // by bumping the size by one bit. This will force it to Promote, enabling the
  // early expansion and avoiding the need to expand later.

  // We don't have to do this if Scale is 0; that can always be expanded, unless
  // it's a saturating signed operation. Those can experience true integer
  // division overflow, a case which we must avoid.

  // FIXME: We wouldn't have to do this (or any of the early
  // expansion/promotion) if it was possible to expand a libcall of an
  // illegal type during operation legalization. But it's not, so things
  // get a bit hacky.
  unsigned ScaleInt = Scale->getAsZExtVal();
  if ((ScaleInt > 0 || (Saturating && Signed)) &&
      (TLI.isTypeLegal(VT) ||
       (VT.isVector() && TLI.isTypeLegal(VT.getVectorElementType())))) {
    TargetLowering::LegalizeAction Action = TLI.getFixedPointOperationAction(
        Opcode, VT, ScaleInt);
    if (Action != TargetLowering::Legal && Action != TargetLowering::Custom) {
      EVT PromVT;
      if (VT.isScalarInteger())
        PromVT = EVT::getIntegerVT(Ctx, VT.getSizeInBits() + 1);
      else if (VT.isVector()) {
        PromVT = VT.getVectorElementType();
        PromVT = EVT::getIntegerVT(Ctx, PromVT.getSizeInBits() + 1);
        PromVT = EVT::getVectorVT(Ctx, PromVT, VT.getVectorElementCount());
      } else
        llvm_unreachable("Wrong VT for DIVFIX?");
      LHS = DAG.getExtOrTrunc(Signed, LHS, DL, PromVT);
      RHS = DAG.getExtOrTrunc(Signed, RHS, DL, PromVT);
      EVT ShiftTy = TLI.getShiftAmountTy(PromVT, DAG.getDataLayout());
      // For saturating operations, we need to shift up the LHS to get the
      // proper saturation width, and then shift down again afterwards.
      if (Saturating)
        LHS = DAG.getNode(ISD::SHL, DL, PromVT, LHS,
                          DAG.getConstant(1, DL, ShiftTy));
      SDValue Res = DAG.getNode(Opcode, DL, PromVT, LHS, RHS, Scale);
      if (Saturating)
        Res = DAG.getNode(Signed ? ISD::SRA : ISD::SRL, DL, PromVT, Res,
                          DAG.getConstant(1, DL, ShiftTy));
      return DAG.getZExtOrTrunc(Res, DL, VT);
    }
  }

  return DAG.getNode(Opcode, DL, VT, LHS, RHS, Scale);
}

// getUnderlyingArgRegs - Find underlying registers used for a truncated,
// bitcasted, or split argument. Returns a list of <Register, size in bits>
static void
getUnderlyingArgRegs(SmallVectorImpl<std::pair<unsigned, TypeSize>> &Regs,
                     const SDValue &N) {
  switch (N.getOpcode()) {
  case ISD::CopyFromReg: {
    SDValue Op = N.getOperand(1);
    Regs.emplace_back(cast<RegisterSDNode>(Op)->getReg(),
                      Op.getValueType().getSizeInBits());
    return;
  }
  case ISD::BITCAST:
  case ISD::AssertZext:
  case ISD::AssertSext:
  case ISD::TRUNCATE:
    getUnderlyingArgRegs(Regs, N.getOperand(0));
    return;
  case ISD::BUILD_PAIR:
  case ISD::BUILD_VECTOR:
  case ISD::CONCAT_VECTORS:
    for (SDValue Op : N->op_values())
      getUnderlyingArgRegs(Regs, Op);
    return;
  default:
    return;
  }
}

/// If the DbgValueInst is a dbg_value of a function argument, create the
/// corresponding DBG_VALUE machine instruction for it now.  At the end of
/// instruction selection, they will be inserted to the entry BB.
/// We don't currently support this for variadic dbg_values, as they shouldn't
/// appear for function arguments or in the prologue.
bool SelectionDAGBuilder::EmitFuncArgumentDbgValue(
    const Value *V, DILocalVariable *Variable, DIExpression *Expr,
    DILocation *DL, FuncArgumentDbgValueKind Kind, const SDValue &N) {
  const Argument *Arg = dyn_cast<Argument>(V);
  if (!Arg)
    return false;

  MachineFunction &MF = DAG.getMachineFunction();
  const TargetInstrInfo *TII = DAG.getSubtarget().getInstrInfo();

  // Helper to create DBG_INSTR_REFs or DBG_VALUEs, depending on what kind
  // we've been asked to pursue.
  auto MakeVRegDbgValue = [&](Register Reg, DIExpression *FragExpr,
                              bool Indirect) {
    if (Reg.isVirtual() && MF.useDebugInstrRef()) {
      // For VRegs, in instruction referencing mode, create a DBG_INSTR_REF
      // pointing at the VReg, which will be patched up later.
      auto &Inst = TII->get(TargetOpcode::DBG_INSTR_REF);
      SmallVector<MachineOperand, 1> MOs({MachineOperand::CreateReg(
          /* Reg */ Reg, /* isDef */ false, /* isImp */ false,
          /* isKill */ false, /* isDead */ false,
          /* isUndef */ false, /* isEarlyClobber */ false,
          /* SubReg */ 0, /* isDebug */ true)});

      auto *NewDIExpr = FragExpr;
      // We don't have an "Indirect" field in DBG_INSTR_REF, fold that into
      // the DIExpression.
      if (Indirect)
        NewDIExpr = DIExpression::prepend(FragExpr, DIExpression::DerefBefore);
      SmallVector<uint64_t, 2> Ops({dwarf::DW_OP_LLVM_arg, 0});
      NewDIExpr = DIExpression::prependOpcodes(NewDIExpr, Ops);
      return BuildMI(MF, DL, Inst, false, MOs, Variable, NewDIExpr);
    } else {
      // Create a completely standard DBG_VALUE.
      auto &Inst = TII->get(TargetOpcode::DBG_VALUE);
      return BuildMI(MF, DL, Inst, Indirect, Reg, Variable, FragExpr);
    }
  };

  if (Kind == FuncArgumentDbgValueKind::Value) {
    // ArgDbgValues are hoisted to the beginning of the entry block. So we
    // should only emit as ArgDbgValue if the dbg.value intrinsic is found in
    // the entry block.
    bool IsInEntryBlock = FuncInfo.MBB == &FuncInfo.MF->front();
    if (!IsInEntryBlock)
      return false;

    // ArgDbgValues are hoisted to the beginning of the entry block.  So we
    // should only emit as ArgDbgValue if the dbg.value intrinsic describes a
    // variable that also is a param.
    //
    // Although, if we are at the top of the entry block already, we can still
    // emit using ArgDbgValue. This might catch some situations when the
    // dbg.value refers to an argument that isn't used in the entry block, so
    // any CopyToReg node would be optimized out and the only way to express
    // this DBG_VALUE is by using the physical reg (or FI) as done in this
    // method.  ArgDbgValues are hoisted to the beginning of the entry block. So
    // we should only emit as ArgDbgValue if the Variable is an argument to the
    // current function, and the dbg.value intrinsic is found in the entry
    // block.
    bool VariableIsFunctionInputArg = Variable->isParameter() &&
        !DL->getInlinedAt();
    bool IsInPrologue = SDNodeOrder == LowestSDNodeOrder;
    if (!IsInPrologue && !VariableIsFunctionInputArg)
      return false;

    // Here we assume that a function argument on IR level only can be used to
    // describe one input parameter on source level. If we for example have
    // source code like this
    //
    //    struct A { long x, y; };
    //    void foo(struct A a, long b) {
    //      ...
    //      b = a.x;
    //      ...
    //    }
    //
    // and IR like this
    //
    //  define void @foo(i32 %a1, i32 %a2, i32 %b)  {
    //  entry:
    //    call void @llvm.dbg.value(metadata i32 %a1, "a", DW_OP_LLVM_fragment
    //    call void @llvm.dbg.value(metadata i32 %a2, "a", DW_OP_LLVM_fragment
    //    call void @llvm.dbg.value(metadata i32 %b, "b",
    //    ...
    //    call void @llvm.dbg.value(metadata i32 %a1, "b"
    //    ...
    //
    // then the last dbg.value is describing a parameter "b" using a value that
    // is an argument. But since we already has used %a1 to describe a parameter
    // we should not handle that last dbg.value here (that would result in an
    // incorrect hoisting of the DBG_VALUE to the function entry).
    // Notice that we allow one dbg.value per IR level argument, to accommodate
    // for the situation with fragments above.
    // If there is no node for the value being handled, we return true to skip
    // the normal generation of debug info, as it would kill existing debug
    // info for the parameter in case of duplicates.
    if (VariableIsFunctionInputArg) {
      unsigned ArgNo = Arg->getArgNo();
      if (ArgNo >= FuncInfo.DescribedArgs.size())
        FuncInfo.DescribedArgs.resize(ArgNo + 1, false);
      else if (!IsInPrologue && FuncInfo.DescribedArgs.test(ArgNo))
        return !NodeMap[V].getNode();
      FuncInfo.DescribedArgs.set(ArgNo);
    }
  }

  bool IsIndirect = false;
  std::optional<MachineOperand> Op;
  // Some arguments' frame index is recorded during argument lowering.
  int FI = FuncInfo.getArgumentFrameIndex(Arg);
  if (FI != std::numeric_limits<int>::max())
    Op = MachineOperand::CreateFI(FI);

  SmallVector<std::pair<unsigned, TypeSize>, 8> ArgRegsAndSizes;
  if (!Op && N.getNode()) {
    getUnderlyingArgRegs(ArgRegsAndSizes, N);
    Register Reg;
    if (ArgRegsAndSizes.size() == 1)
      Reg = ArgRegsAndSizes.front().first;

    if (Reg && Reg.isVirtual()) {
      MachineRegisterInfo &RegInfo = MF.getRegInfo();
      Register PR = RegInfo.getLiveInPhysReg(Reg);
      if (PR)
        Reg = PR;
    }
    if (Reg) {
      Op = MachineOperand::CreateReg(Reg, false);
      IsIndirect = Kind != FuncArgumentDbgValueKind::Value;
    }
  }

  if (!Op && N.getNode()) {
    // Check if frame index is available.
    SDValue LCandidate = peekThroughBitcasts(N);
    if (LoadSDNode *LNode = dyn_cast<LoadSDNode>(LCandidate.getNode()))
      if (FrameIndexSDNode *FINode =
          dyn_cast<FrameIndexSDNode>(LNode->getBasePtr().getNode()))
        Op = MachineOperand::CreateFI(FINode->getIndex());
  }

  if (!Op) {
    // Create a DBG_VALUE for each decomposed value in ArgRegs to cover Reg
    auto splitMultiRegDbgValue = [&](ArrayRef<std::pair<unsigned, TypeSize>>
                                         SplitRegs) {
      unsigned Offset = 0;
      for (const auto &RegAndSize : SplitRegs) {
        // If the expression is already a fragment, the current register
        // offset+size might extend beyond the fragment. In this case, only
        // the register bits that are inside the fragment are relevant.
        int RegFragmentSizeInBits = RegAndSize.second;
        if (auto ExprFragmentInfo = Expr->getFragmentInfo()) {
          uint64_t ExprFragmentSizeInBits = ExprFragmentInfo->SizeInBits;
          // The register is entirely outside the expression fragment,
          // so is irrelevant for debug info.
          if (Offset >= ExprFragmentSizeInBits)
            break;
          // The register is partially outside the expression fragment, only
          // the low bits within the fragment are relevant for debug info.
          if (Offset + RegFragmentSizeInBits > ExprFragmentSizeInBits) {
            RegFragmentSizeInBits = ExprFragmentSizeInBits - Offset;
          }
        }

        auto FragmentExpr = DIExpression::createFragmentExpression(
            Expr, Offset, RegFragmentSizeInBits);
        Offset += RegAndSize.second;
        // If a valid fragment expression cannot be created, the variable's
        // correct value cannot be determined and so it is set as Undef.
        if (!FragmentExpr) {
          SDDbgValue *SDV = DAG.getConstantDbgValue(
              Variable, Expr, UndefValue::get(V->getType()), DL, SDNodeOrder);
          DAG.AddDbgValue(SDV, false);
          continue;
        }
        MachineInstr *NewMI =
            MakeVRegDbgValue(RegAndSize.first, *FragmentExpr,
                             Kind != FuncArgumentDbgValueKind::Value);
        FuncInfo.ArgDbgValues.push_back(NewMI);
      }
    };

    // Check if ValueMap has reg number.
    DenseMap<const Value *, Register>::const_iterator
      VMI = FuncInfo.ValueMap.find(V);
    if (VMI != FuncInfo.ValueMap.end()) {
      const auto &TLI = DAG.getTargetLoweringInfo();
      RegsForValue RFV(V->getContext(), TLI, DAG.getDataLayout(), VMI->second,
                       V->getType(), std::nullopt);
      if (RFV.occupiesMultipleRegs()) {
        splitMultiRegDbgValue(RFV.getRegsAndSizes());
        return true;
      }

      Op = MachineOperand::CreateReg(VMI->second, false);
      IsIndirect = Kind != FuncArgumentDbgValueKind::Value;
    } else if (ArgRegsAndSizes.size() > 1) {
      // This was split due to the calling convention, and no virtual register
      // mapping exists for the value.
      splitMultiRegDbgValue(ArgRegsAndSizes);
      return true;
    }
  }

  if (!Op)
    return false;

  assert(Variable->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  MachineInstr *NewMI = nullptr;

  if (Op->isReg())
    NewMI = MakeVRegDbgValue(Op->getReg(), Expr, IsIndirect);
  else
    NewMI = BuildMI(MF, DL, TII->get(TargetOpcode::DBG_VALUE), true, *Op,
                    Variable, Expr);

  // Otherwise, use ArgDbgValues.
  FuncInfo.ArgDbgValues.push_back(NewMI);
  return true;
}

/// Return the appropriate SDDbgValue based on N.
SDDbgValue *SelectionDAGBuilder::getDbgValue(SDValue N,
                                             DILocalVariable *Variable,
                                             DIExpression *Expr,
                                             const DebugLoc &dl,
                                             unsigned DbgSDNodeOrder) {
  if (auto *FISDN = dyn_cast<FrameIndexSDNode>(N.getNode())) {
    // Construct a FrameIndexDbgValue for FrameIndexSDNodes so we can describe
    // stack slot locations.
    //
    // Consider "int x = 0; int *px = &x;". There are two kinds of interesting
    // debug values here after optimization:
    //
    //   dbg.value(i32* %px, !"int *px", !DIExpression()), and
    //   dbg.value(i32* %px, !"int x", !DIExpression(DW_OP_deref))
    //
    // Both describe the direct values of their associated variables.
    return DAG.getFrameIndexDbgValue(Variable, Expr, FISDN->getIndex(),
                                     /*IsIndirect*/ false, dl, DbgSDNodeOrder);
  }
  return DAG.getDbgValue(Variable, Expr, N.getNode(), N.getResNo(),
                         /*IsIndirect*/ false, dl, DbgSDNodeOrder);
}

static unsigned FixedPointIntrinsicToOpcode(unsigned Intrinsic) {
  switch (Intrinsic) {
  case Intrinsic::smul_fix:
    return ISD::SMULFIX;
  case Intrinsic::umul_fix:
    return ISD::UMULFIX;
  case Intrinsic::smul_fix_sat:
    return ISD::SMULFIXSAT;
  case Intrinsic::umul_fix_sat:
    return ISD::UMULFIXSAT;
  case Intrinsic::sdiv_fix:
    return ISD::SDIVFIX;
  case Intrinsic::udiv_fix:
    return ISD::UDIVFIX;
  case Intrinsic::sdiv_fix_sat:
    return ISD::SDIVFIXSAT;
  case Intrinsic::udiv_fix_sat:
    return ISD::UDIVFIXSAT;
  default:
    llvm_unreachable("Unhandled fixed point intrinsic");
  }
}

void SelectionDAGBuilder::lowerCallToExternalSymbol(const CallInst &I,
                                           const char *FunctionName) {
  assert(FunctionName && "FunctionName must not be nullptr");
  SDValue Callee = DAG.getExternalSymbol(
      FunctionName,
      DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout()));
  LowerCallTo(I, Callee, I.isTailCall(), I.isMustTailCall());
}

/// Given a @llvm.call.preallocated.setup, return the corresponding
/// preallocated call.
static const CallBase *FindPreallocatedCall(const Value *PreallocatedSetup) {
  assert(cast<CallBase>(PreallocatedSetup)
                 ->getCalledFunction()
                 ->getIntrinsicID() == Intrinsic::call_preallocated_setup &&
         "expected call_preallocated_setup Value");
  for (const auto *U : PreallocatedSetup->users()) {
    auto *UseCall = cast<CallBase>(U);
    const Function *Fn = UseCall->getCalledFunction();
    if (!Fn || Fn->getIntrinsicID() != Intrinsic::call_preallocated_arg) {
      return UseCall;
    }
  }
  llvm_unreachable("expected corresponding call to preallocated setup/arg");
}

/// If DI is a debug value with an EntryValue expression, lower it using the
/// corresponding physical register of the associated Argument value
/// (guaranteed to exist by the verifier).
bool SelectionDAGBuilder::visitEntryValueDbgValue(
    ArrayRef<const Value *> Values, DILocalVariable *Variable,
    DIExpression *Expr, DebugLoc DbgLoc) {
  if (!Expr->isEntryValue() || !hasSingleElement(Values))
    return false;

  // These properties are guaranteed by the verifier.
  const Argument *Arg = cast<Argument>(Values[0]);
  assert(Arg->hasAttribute(Attribute::AttrKind::SwiftAsync));

  auto ArgIt = FuncInfo.ValueMap.find(Arg);
  if (ArgIt == FuncInfo.ValueMap.end()) {
    LLVM_DEBUG(
        dbgs() << "Dropping dbg.value: expression is entry_value but "
                  "couldn't find an associated register for the Argument\n");
    return true;
  }
  Register ArgVReg = ArgIt->getSecond();

  for (auto [PhysReg, VirtReg] : FuncInfo.RegInfo->liveins())
    if (ArgVReg == VirtReg || ArgVReg == PhysReg) {
      SDDbgValue *SDV = DAG.getVRegDbgValue(
          Variable, Expr, PhysReg, false /*IsIndidrect*/, DbgLoc, SDNodeOrder);
      DAG.AddDbgValue(SDV, false /*treat as dbg.declare byval parameter*/);
      return true;
    }
  LLVM_DEBUG(dbgs() << "Dropping dbg.value: expression is entry_value but "
                       "couldn't find a physical register\n");
  return true;
}

/// Lower the call to the specified intrinsic function.
void SelectionDAGBuilder::visitConvergenceControl(const CallInst &I,
                                                  unsigned Intrinsic) {
  SDLoc sdl = getCurSDLoc();
  switch (Intrinsic) {
  case Intrinsic::experimental_convergence_anchor:
    setValue(&I, DAG.getNode(ISD::CONVERGENCECTRL_ANCHOR, sdl, MVT::Untyped));
    break;
  case Intrinsic::experimental_convergence_entry:
    setValue(&I, DAG.getNode(ISD::CONVERGENCECTRL_ENTRY, sdl, MVT::Untyped));
    break;
  case Intrinsic::experimental_convergence_loop: {
    auto Bundle = I.getOperandBundle(LLVMContext::OB_convergencectrl);
    auto *Token = Bundle->Inputs[0].get();
    setValue(&I, DAG.getNode(ISD::CONVERGENCECTRL_LOOP, sdl, MVT::Untyped,
                             getValue(Token)));
    break;
  }
  }
}

void SelectionDAGBuilder::visitVectorHistogram(const CallInst &I,
                                               unsigned IntrinsicID) {
  // For now, we're only lowering an 'add' histogram.
  // We can add others later, e.g. saturating adds, min/max.
  assert(IntrinsicID == Intrinsic::experimental_vector_histogram_add &&
         "Tried to lower unsupported histogram type");
  SDLoc sdl = getCurSDLoc();
  Value *Ptr = I.getOperand(0);
  SDValue Inc = getValue(I.getOperand(1));
  SDValue Mask = getValue(I.getOperand(2));

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  DataLayout TargetDL = DAG.getDataLayout();
  EVT VT = Inc.getValueType();
  Align Alignment = DAG.getEVTAlign(VT);

  const MDNode *Ranges = getRangeMetadata(I);

  SDValue Root = DAG.getRoot();
  SDValue Base;
  SDValue Index;
  ISD::MemIndexType IndexType;
  SDValue Scale;
  bool UniformBase = getUniformBase(Ptr, Base, Index, IndexType, Scale, this,
                                    I.getParent(), VT.getScalarStoreSize());

  unsigned AS = Ptr->getType()->getScalarType()->getPointerAddressSpace();

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(AS),
      MachineMemOperand::MOLoad | MachineMemOperand::MOStore,
      MemoryLocation::UnknownSize, Alignment, I.getAAMetadata(), Ranges);

  if (!UniformBase) {
    Base = DAG.getConstant(0, sdl, TLI.getPointerTy(DAG.getDataLayout()));
    Index = getValue(Ptr);
    IndexType = ISD::SIGNED_SCALED;
    Scale =
        DAG.getTargetConstant(1, sdl, TLI.getPointerTy(DAG.getDataLayout()));
  }

  EVT IdxVT = Index.getValueType();
  EVT EltTy = IdxVT.getVectorElementType();
  if (TLI.shouldExtendGSIndex(IdxVT, EltTy)) {
    EVT NewIdxVT = IdxVT.changeVectorElementType(EltTy);
    Index = DAG.getNode(ISD::SIGN_EXTEND, sdl, NewIdxVT, Index);
  }

  SDValue ID = DAG.getTargetConstant(IntrinsicID, sdl, MVT::i32);

  SDValue Ops[] = {Root, Inc, Mask, Base, Index, Scale, ID};
  SDValue Histogram = DAG.getMaskedHistogram(DAG.getVTList(MVT::Other), VT, sdl,
                                             Ops, MMO, IndexType);

  setValue(&I, Histogram);
  DAG.setRoot(Histogram);
}

/// Lower the call to the specified intrinsic function.
void SelectionDAGBuilder::visitIntrinsicCall(const CallInst &I,
                                             unsigned Intrinsic) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc sdl = getCurSDLoc();
  DebugLoc dl = getCurDebugLoc();
  SDValue Res;

  SDNodeFlags Flags;
  if (auto *FPOp = dyn_cast<FPMathOperator>(&I))
    Flags.copyFMF(*FPOp);

  switch (Intrinsic) {
  default:
    // By default, turn this into a target intrinsic node.
    visitTargetIntrinsic(I, Intrinsic);
    return;
  case Intrinsic::vscale: {
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    setValue(&I, DAG.getVScale(sdl, VT, APInt(VT.getSizeInBits(), 1)));
    return;
  }
  case Intrinsic::vastart:  visitVAStart(I); return;
  case Intrinsic::vaend:    visitVAEnd(I); return;
  case Intrinsic::vacopy:   visitVACopy(I); return;
  case Intrinsic::returnaddress:
    setValue(&I, DAG.getNode(ISD::RETURNADDR, sdl,
                             TLI.getValueType(DAG.getDataLayout(), I.getType()),
                             getValue(I.getArgOperand(0))));
    return;
  case Intrinsic::addressofreturnaddress:
    setValue(&I,
             DAG.getNode(ISD::ADDROFRETURNADDR, sdl,
                         TLI.getValueType(DAG.getDataLayout(), I.getType())));
    return;
  case Intrinsic::sponentry:
    setValue(&I,
             DAG.getNode(ISD::SPONENTRY, sdl,
                         TLI.getValueType(DAG.getDataLayout(), I.getType())));
    return;
  case Intrinsic::frameaddress:
    setValue(&I, DAG.getNode(ISD::FRAMEADDR, sdl,
                             TLI.getFrameIndexTy(DAG.getDataLayout()),
                             getValue(I.getArgOperand(0))));
    return;
  case Intrinsic::read_volatile_register:
  case Intrinsic::read_register: {
    Value *Reg = I.getArgOperand(0);
    SDValue Chain = getRoot();
    SDValue RegName =
        DAG.getMDNode(cast<MDNode>(cast<MetadataAsValue>(Reg)->getMetadata()));
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    Res = DAG.getNode(ISD::READ_REGISTER, sdl,
      DAG.getVTList(VT, MVT::Other), Chain, RegName);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;
  }
  case Intrinsic::write_register: {
    Value *Reg = I.getArgOperand(0);
    Value *RegValue = I.getArgOperand(1);
    SDValue Chain = getRoot();
    SDValue RegName =
        DAG.getMDNode(cast<MDNode>(cast<MetadataAsValue>(Reg)->getMetadata()));
    DAG.setRoot(DAG.getNode(ISD::WRITE_REGISTER, sdl, MVT::Other, Chain,
                            RegName, getValue(RegValue)));
    return;
  }
  case Intrinsic::memcpy: {
    const auto &MCI = cast<MemCpyInst>(I);
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    // @llvm.memcpy defines 0 and 1 to both mean no alignment.
    Align DstAlign = MCI.getDestAlign().valueOrOne();
    Align SrcAlign = MCI.getSourceAlign().valueOrOne();
    Align Alignment = std::min(DstAlign, SrcAlign);
    bool isVol = MCI.isVolatile();
    // FIXME: Support passing different dest/src alignments to the memcpy DAG
    // node.
    SDValue Root = isVol ? getRoot() : getMemoryRoot();
    SDValue MC = DAG.getMemcpy(Root, sdl, Op1, Op2, Op3, Alignment, isVol,
                               /* AlwaysInline */ false, &I, std::nullopt,
                               MachinePointerInfo(I.getArgOperand(0)),
                               MachinePointerInfo(I.getArgOperand(1)),
                               I.getAAMetadata(), AA);
    updateDAGForMaybeTailCall(MC);
    return;
  }
  case Intrinsic::memcpy_inline: {
    const auto &MCI = cast<MemCpyInlineInst>(I);
    SDValue Dst = getValue(I.getArgOperand(0));
    SDValue Src = getValue(I.getArgOperand(1));
    SDValue Size = getValue(I.getArgOperand(2));
    assert(isa<ConstantSDNode>(Size) && "memcpy_inline needs constant size");
    // @llvm.memcpy.inline defines 0 and 1 to both mean no alignment.
    Align DstAlign = MCI.getDestAlign().valueOrOne();
    Align SrcAlign = MCI.getSourceAlign().valueOrOne();
    Align Alignment = std::min(DstAlign, SrcAlign);
    bool isVol = MCI.isVolatile();
    // FIXME: Support passing different dest/src alignments to the memcpy DAG
    // node.
    SDValue MC = DAG.getMemcpy(getRoot(), sdl, Dst, Src, Size, Alignment, isVol,
                               /* AlwaysInline */ true, &I, std::nullopt,
                               MachinePointerInfo(I.getArgOperand(0)),
                               MachinePointerInfo(I.getArgOperand(1)),
                               I.getAAMetadata(), AA);
    updateDAGForMaybeTailCall(MC);
    return;
  }
  case Intrinsic::memset: {
    const auto &MSI = cast<MemSetInst>(I);
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    // @llvm.memset defines 0 and 1 to both mean no alignment.
    Align Alignment = MSI.getDestAlign().valueOrOne();
    bool isVol = MSI.isVolatile();
    SDValue Root = isVol ? getRoot() : getMemoryRoot();
    SDValue MS = DAG.getMemset(
        Root, sdl, Op1, Op2, Op3, Alignment, isVol, /* AlwaysInline */ false,
        &I, MachinePointerInfo(I.getArgOperand(0)), I.getAAMetadata());
    updateDAGForMaybeTailCall(MS);
    return;
  }
  case Intrinsic::memset_inline: {
    const auto &MSII = cast<MemSetInlineInst>(I);
    SDValue Dst = getValue(I.getArgOperand(0));
    SDValue Value = getValue(I.getArgOperand(1));
    SDValue Size = getValue(I.getArgOperand(2));
    assert(isa<ConstantSDNode>(Size) && "memset_inline needs constant size");
    // @llvm.memset defines 0 and 1 to both mean no alignment.
    Align DstAlign = MSII.getDestAlign().valueOrOne();
    bool isVol = MSII.isVolatile();
    SDValue Root = isVol ? getRoot() : getMemoryRoot();
    SDValue MC = DAG.getMemset(Root, sdl, Dst, Value, Size, DstAlign, isVol,
                               /* AlwaysInline */ true, &I,
                               MachinePointerInfo(I.getArgOperand(0)),
                               I.getAAMetadata());
    updateDAGForMaybeTailCall(MC);
    return;
  }
  case Intrinsic::memmove: {
    const auto &MMI = cast<MemMoveInst>(I);
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    // @llvm.memmove defines 0 and 1 to both mean no alignment.
    Align DstAlign = MMI.getDestAlign().valueOrOne();
    Align SrcAlign = MMI.getSourceAlign().valueOrOne();
    Align Alignment = std::min(DstAlign, SrcAlign);
    bool isVol = MMI.isVolatile();
    // FIXME: Support passing different dest/src alignments to the memmove DAG
    // node.
    SDValue Root = isVol ? getRoot() : getMemoryRoot();
    SDValue MM = DAG.getMemmove(Root, sdl, Op1, Op2, Op3, Alignment, isVol, &I,
                                /* OverrideTailCall */ std::nullopt,
                                MachinePointerInfo(I.getArgOperand(0)),
                                MachinePointerInfo(I.getArgOperand(1)),
                                I.getAAMetadata(), AA);
    updateDAGForMaybeTailCall(MM);
    return;
  }
  case Intrinsic::memcpy_element_unordered_atomic: {
    const AtomicMemCpyInst &MI = cast<AtomicMemCpyInst>(I);
    SDValue Dst = getValue(MI.getRawDest());
    SDValue Src = getValue(MI.getRawSource());
    SDValue Length = getValue(MI.getLength());

    Type *LengthTy = MI.getLength()->getType();
    unsigned ElemSz = MI.getElementSizeInBytes();
    bool isTC = I.isTailCall() && isInTailCallPosition(I, DAG.getTarget());
    SDValue MC =
        DAG.getAtomicMemcpy(getRoot(), sdl, Dst, Src, Length, LengthTy, ElemSz,
                            isTC, MachinePointerInfo(MI.getRawDest()),
                            MachinePointerInfo(MI.getRawSource()));
    updateDAGForMaybeTailCall(MC);
    return;
  }
  case Intrinsic::memmove_element_unordered_atomic: {
    auto &MI = cast<AtomicMemMoveInst>(I);
    SDValue Dst = getValue(MI.getRawDest());
    SDValue Src = getValue(MI.getRawSource());
    SDValue Length = getValue(MI.getLength());

    Type *LengthTy = MI.getLength()->getType();
    unsigned ElemSz = MI.getElementSizeInBytes();
    bool isTC = I.isTailCall() && isInTailCallPosition(I, DAG.getTarget());
    SDValue MC =
        DAG.getAtomicMemmove(getRoot(), sdl, Dst, Src, Length, LengthTy, ElemSz,
                             isTC, MachinePointerInfo(MI.getRawDest()),
                             MachinePointerInfo(MI.getRawSource()));
    updateDAGForMaybeTailCall(MC);
    return;
  }
  case Intrinsic::memset_element_unordered_atomic: {
    auto &MI = cast<AtomicMemSetInst>(I);
    SDValue Dst = getValue(MI.getRawDest());
    SDValue Val = getValue(MI.getValue());
    SDValue Length = getValue(MI.getLength());

    Type *LengthTy = MI.getLength()->getType();
    unsigned ElemSz = MI.getElementSizeInBytes();
    bool isTC = I.isTailCall() && isInTailCallPosition(I, DAG.getTarget());
    SDValue MC =
        DAG.getAtomicMemset(getRoot(), sdl, Dst, Val, Length, LengthTy, ElemSz,
                            isTC, MachinePointerInfo(MI.getRawDest()));
    updateDAGForMaybeTailCall(MC);
    return;
  }
  case Intrinsic::call_preallocated_setup: {
    const CallBase *PreallocatedCall = FindPreallocatedCall(&I);
    SDValue SrcValue = DAG.getSrcValue(PreallocatedCall);
    SDValue Res = DAG.getNode(ISD::PREALLOCATED_SETUP, sdl, MVT::Other,
                              getRoot(), SrcValue);
    setValue(&I, Res);
    DAG.setRoot(Res);
    return;
  }
  case Intrinsic::call_preallocated_arg: {
    const CallBase *PreallocatedCall = FindPreallocatedCall(I.getOperand(0));
    SDValue SrcValue = DAG.getSrcValue(PreallocatedCall);
    SDValue Ops[3];
    Ops[0] = getRoot();
    Ops[1] = SrcValue;
    Ops[2] = DAG.getTargetConstant(*cast<ConstantInt>(I.getArgOperand(1)), sdl,
                                   MVT::i32); // arg index
    SDValue Res = DAG.getNode(
        ISD::PREALLOCATED_ARG, sdl,
        DAG.getVTList(TLI.getPointerTy(DAG.getDataLayout()), MVT::Other), Ops);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;
  }
  case Intrinsic::dbg_declare: {
    const auto &DI = cast<DbgDeclareInst>(I);
    // Debug intrinsics are handled separately in assignment tracking mode.
    // Some intrinsics are handled right after Argument lowering.
    if (AssignmentTrackingEnabled ||
        FuncInfo.PreprocessedDbgDeclares.count(&DI))
      return;
    LLVM_DEBUG(dbgs() << "SelectionDAG visiting dbg_declare: " << DI << "\n");
    DILocalVariable *Variable = DI.getVariable();
    DIExpression *Expression = DI.getExpression();
    dropDanglingDebugInfo(Variable, Expression);
    // Assume dbg.declare can not currently use DIArgList, i.e.
    // it is non-variadic.
    assert(!DI.hasArgList() && "Only dbg.value should currently use DIArgList");
    handleDebugDeclare(DI.getVariableLocationOp(0), Variable, Expression,
                       DI.getDebugLoc());
    return;
  }
  case Intrinsic::dbg_label: {
    const DbgLabelInst &DI = cast<DbgLabelInst>(I);
    DILabel *Label = DI.getLabel();
    assert(Label && "Missing label");

    SDDbgLabel *SDV;
    SDV = DAG.getDbgLabel(Label, dl, SDNodeOrder);
    DAG.AddDbgLabel(SDV);
    return;
  }
  case Intrinsic::dbg_assign: {
    // Debug intrinsics are handled separately in assignment tracking mode.
    if (AssignmentTrackingEnabled)
      return;
    // If assignment tracking hasn't been enabled then fall through and treat
    // the dbg.assign as a dbg.value.
    [[fallthrough]];
  }
  case Intrinsic::dbg_value: {
    // Debug intrinsics are handled separately in assignment tracking mode.
    if (AssignmentTrackingEnabled)
      return;
    const DbgValueInst &DI = cast<DbgValueInst>(I);
    assert(DI.getVariable() && "Missing variable");

    DILocalVariable *Variable = DI.getVariable();
    DIExpression *Expression = DI.getExpression();
    dropDanglingDebugInfo(Variable, Expression);

    if (DI.isKillLocation()) {
      handleKillDebugValue(Variable, Expression, DI.getDebugLoc(), SDNodeOrder);
      return;
    }

    SmallVector<Value *, 4> Values(DI.getValues());
    if (Values.empty())
      return;

    bool IsVariadic = DI.hasArgList();
    if (!handleDebugValue(Values, Variable, Expression, DI.getDebugLoc(),
                          SDNodeOrder, IsVariadic))
      addDanglingDebugInfo(Values, Variable, Expression, IsVariadic,
                           DI.getDebugLoc(), SDNodeOrder);
    return;
  }

  case Intrinsic::eh_typeid_for: {
    // Find the type id for the given typeinfo.
    GlobalValue *GV = ExtractTypeInfo(I.getArgOperand(0));
    unsigned TypeID = DAG.getMachineFunction().getTypeIDFor(GV);
    Res = DAG.getConstant(TypeID, sdl, MVT::i32);
    setValue(&I, Res);
    return;
  }

  case Intrinsic::eh_return_i32:
  case Intrinsic::eh_return_i64:
    DAG.getMachineFunction().setCallsEHReturn(true);
    DAG.setRoot(DAG.getNode(ISD::EH_RETURN, sdl,
                            MVT::Other,
                            getControlRoot(),
                            getValue(I.getArgOperand(0)),
                            getValue(I.getArgOperand(1))));
    return;
  case Intrinsic::eh_unwind_init:
    DAG.getMachineFunction().setCallsUnwindInit(true);
    return;
  case Intrinsic::eh_dwarf_cfa:
    setValue(&I, DAG.getNode(ISD::EH_DWARF_CFA, sdl,
                             TLI.getPointerTy(DAG.getDataLayout()),
                             getValue(I.getArgOperand(0))));
    return;
  case Intrinsic::eh_sjlj_callsite: {
    MachineModuleInfo &MMI = DAG.getMachineFunction().getMMI();
    ConstantInt *CI = cast<ConstantInt>(I.getArgOperand(0));
    assert(MMI.getCurrentCallSite() == 0 && "Overlapping call sites!");

    MMI.setCurrentCallSite(CI->getZExtValue());
    return;
  }
  case Intrinsic::eh_sjlj_functioncontext: {
    // Get and store the index of the function context.
    MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    AllocaInst *FnCtx =
      cast<AllocaInst>(I.getArgOperand(0)->stripPointerCasts());
    int FI = FuncInfo.StaticAllocaMap[FnCtx];
    MFI.setFunctionContextIndex(FI);
    return;
  }
  case Intrinsic::eh_sjlj_setjmp: {
    SDValue Ops[2];
    Ops[0] = getRoot();
    Ops[1] = getValue(I.getArgOperand(0));
    SDValue Op = DAG.getNode(ISD::EH_SJLJ_SETJMP, sdl,
                             DAG.getVTList(MVT::i32, MVT::Other), Ops);
    setValue(&I, Op.getValue(0));
    DAG.setRoot(Op.getValue(1));
    return;
  }
  case Intrinsic::eh_sjlj_longjmp:
    DAG.setRoot(DAG.getNode(ISD::EH_SJLJ_LONGJMP, sdl, MVT::Other,
                            getRoot(), getValue(I.getArgOperand(0))));
    return;
  case Intrinsic::eh_sjlj_setup_dispatch:
    DAG.setRoot(DAG.getNode(ISD::EH_SJLJ_SETUP_DISPATCH, sdl, MVT::Other,
                            getRoot()));
    return;
  case Intrinsic::masked_gather:
    visitMaskedGather(I);
    return;
  case Intrinsic::masked_load:
    visitMaskedLoad(I);
    return;
  case Intrinsic::masked_scatter:
    visitMaskedScatter(I);
    return;
  case Intrinsic::masked_store:
    visitMaskedStore(I);
    return;
  case Intrinsic::masked_expandload:
    visitMaskedLoad(I, true /* IsExpanding */);
    return;
  case Intrinsic::masked_compressstore:
    visitMaskedStore(I, true /* IsCompressing */);
    return;
  case Intrinsic::powi:
    setValue(&I, ExpandPowI(sdl, getValue(I.getArgOperand(0)),
                            getValue(I.getArgOperand(1)), DAG));
    return;
  case Intrinsic::log:
    setValue(&I, expandLog(sdl, getValue(I.getArgOperand(0)), DAG, TLI, Flags));
    return;
  case Intrinsic::log2:
    setValue(&I,
             expandLog2(sdl, getValue(I.getArgOperand(0)), DAG, TLI, Flags));
    return;
  case Intrinsic::log10:
    setValue(&I,
             expandLog10(sdl, getValue(I.getArgOperand(0)), DAG, TLI, Flags));
    return;
  case Intrinsic::exp:
    setValue(&I, expandExp(sdl, getValue(I.getArgOperand(0)), DAG, TLI, Flags));
    return;
  case Intrinsic::exp2:
    setValue(&I,
             expandExp2(sdl, getValue(I.getArgOperand(0)), DAG, TLI, Flags));
    return;
  case Intrinsic::pow:
    setValue(&I, expandPow(sdl, getValue(I.getArgOperand(0)),
                           getValue(I.getArgOperand(1)), DAG, TLI, Flags));
    return;
  case Intrinsic::sqrt:
  case Intrinsic::fabs:
  case Intrinsic::sin:
  case Intrinsic::cos:
  case Intrinsic::tan:
  case Intrinsic::asin:
  case Intrinsic::acos:
  case Intrinsic::atan:
  case Intrinsic::sinh:
  case Intrinsic::cosh:
  case Intrinsic::tanh:
  case Intrinsic::exp10:
  case Intrinsic::floor:
  case Intrinsic::ceil:
  case Intrinsic::trunc:
  case Intrinsic::rint:
  case Intrinsic::nearbyint:
  case Intrinsic::round:
  case Intrinsic::roundeven:
  case Intrinsic::canonicalize: {
    unsigned Opcode;
    // clang-format off
    switch (Intrinsic) {
    default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
    case Intrinsic::sqrt:         Opcode = ISD::FSQRT;         break;
    case Intrinsic::fabs:         Opcode = ISD::FABS;          break;
    case Intrinsic::sin:          Opcode = ISD::FSIN;          break;
    case Intrinsic::cos:          Opcode = ISD::FCOS;          break;
    case Intrinsic::tan:          Opcode = ISD::FTAN;          break;
    case Intrinsic::asin:         Opcode = ISD::FASIN;         break;
    case Intrinsic::acos:         Opcode = ISD::FACOS;         break;
    case Intrinsic::atan:         Opcode = ISD::FATAN;         break;
    case Intrinsic::sinh:         Opcode = ISD::FSINH;         break;
    case Intrinsic::cosh:         Opcode = ISD::FCOSH;         break;
    case Intrinsic::tanh:         Opcode = ISD::FTANH;         break;
    case Intrinsic::exp10:        Opcode = ISD::FEXP10;        break;
    case Intrinsic::floor:        Opcode = ISD::FFLOOR;        break;
    case Intrinsic::ceil:         Opcode = ISD::FCEIL;         break;
    case Intrinsic::trunc:        Opcode = ISD::FTRUNC;        break;
    case Intrinsic::rint:         Opcode = ISD::FRINT;         break;
    case Intrinsic::nearbyint:    Opcode = ISD::FNEARBYINT;    break;
    case Intrinsic::round:        Opcode = ISD::FROUND;        break;
    case Intrinsic::roundeven:    Opcode = ISD::FROUNDEVEN;    break;
    case Intrinsic::canonicalize: Opcode = ISD::FCANONICALIZE; break;
    }
    // clang-format on

    setValue(&I, DAG.getNode(Opcode, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)), Flags));
    return;
  }
  case Intrinsic::lround:
  case Intrinsic::llround:
  case Intrinsic::lrint:
  case Intrinsic::llrint: {
    unsigned Opcode;
    // clang-format off
    switch (Intrinsic) {
    default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
    case Intrinsic::lround:  Opcode = ISD::LROUND;  break;
    case Intrinsic::llround: Opcode = ISD::LLROUND; break;
    case Intrinsic::lrint:   Opcode = ISD::LRINT;   break;
    case Intrinsic::llrint:  Opcode = ISD::LLRINT;  break;
    }
    // clang-format on

    EVT RetVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    setValue(&I, DAG.getNode(Opcode, sdl, RetVT,
                             getValue(I.getArgOperand(0))));
    return;
  }
  case Intrinsic::minnum:
    setValue(&I, DAG.getNode(ISD::FMINNUM, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)), Flags));
    return;
  case Intrinsic::maxnum:
    setValue(&I, DAG.getNode(ISD::FMAXNUM, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)), Flags));
    return;
  case Intrinsic::minimum:
    setValue(&I, DAG.getNode(ISD::FMINIMUM, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)), Flags));
    return;
  case Intrinsic::maximum:
    setValue(&I, DAG.getNode(ISD::FMAXIMUM, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)), Flags));
    return;
  case Intrinsic::copysign:
    setValue(&I, DAG.getNode(ISD::FCOPYSIGN, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)), Flags));
    return;
  case Intrinsic::ldexp:
    setValue(&I, DAG.getNode(ISD::FLDEXP, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)), Flags));
    return;
  case Intrinsic::frexp: {
    SmallVector<EVT, 2> ValueVTs;
    ComputeValueVTs(TLI, DAG.getDataLayout(), I.getType(), ValueVTs);
    SDVTList VTs = DAG.getVTList(ValueVTs);
    setValue(&I,
             DAG.getNode(ISD::FFREXP, sdl, VTs, getValue(I.getArgOperand(0))));
    return;
  }
  case Intrinsic::arithmetic_fence: {
    setValue(&I, DAG.getNode(ISD::ARITH_FENCE, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)), Flags));
    return;
  }
  case Intrinsic::fma:
    setValue(&I, DAG.getNode(
                     ISD::FMA, sdl, getValue(I.getArgOperand(0)).getValueType(),
                     getValue(I.getArgOperand(0)), getValue(I.getArgOperand(1)),
                     getValue(I.getArgOperand(2)), Flags));
    return;
#define INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC)                         \
  case Intrinsic::INTRINSIC:
#include "llvm/IR/ConstrainedOps.def"
    visitConstrainedFPIntrinsic(cast<ConstrainedFPIntrinsic>(I));
    return;
#define BEGIN_REGISTER_VP_INTRINSIC(VPID, ...) case Intrinsic::VPID:
#include "llvm/IR/VPIntrinsics.def"
    visitVectorPredicationIntrinsic(cast<VPIntrinsic>(I));
    return;
  case Intrinsic::fptrunc_round: {
    // Get the last argument, the metadata and convert it to an integer in the
    // call
    Metadata *MD = cast<MetadataAsValue>(I.getArgOperand(1))->getMetadata();
    std::optional<RoundingMode> RoundMode =
        convertStrToRoundingMode(cast<MDString>(MD)->getString());

    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());

    // Propagate fast-math-flags from IR to node(s).
    SDNodeFlags Flags;
    Flags.copyFMF(*cast<FPMathOperator>(&I));
    SelectionDAG::FlagInserter FlagsInserter(DAG, Flags);

    SDValue Result;
    Result = DAG.getNode(
        ISD::FPTRUNC_ROUND, sdl, VT, getValue(I.getArgOperand(0)),
        DAG.getTargetConstant((int)*RoundMode, sdl,
                              TLI.getPointerTy(DAG.getDataLayout())));
    setValue(&I, Result);

    return;
  }
  case Intrinsic::fmuladd: {
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    if (TM.Options.AllowFPOpFusion != FPOpFusion::Strict &&
        TLI.isFMAFasterThanFMulAndFAdd(DAG.getMachineFunction(), VT)) {
      setValue(&I, DAG.getNode(ISD::FMA, sdl,
                               getValue(I.getArgOperand(0)).getValueType(),
                               getValue(I.getArgOperand(0)),
                               getValue(I.getArgOperand(1)),
                               getValue(I.getArgOperand(2)), Flags));
    } else {
      // TODO: Intrinsic calls should have fast-math-flags.
      SDValue Mul = DAG.getNode(
          ISD::FMUL, sdl, getValue(I.getArgOperand(0)).getValueType(),
          getValue(I.getArgOperand(0)), getValue(I.getArgOperand(1)), Flags);
      SDValue Add = DAG.getNode(ISD::FADD, sdl,
                                getValue(I.getArgOperand(0)).getValueType(),
                                Mul, getValue(I.getArgOperand(2)), Flags);
      setValue(&I, Add);
    }
    return;
  }
  case Intrinsic::convert_to_fp16:
    setValue(&I, DAG.getNode(ISD::BITCAST, sdl, MVT::i16,
                             DAG.getNode(ISD::FP_ROUND, sdl, MVT::f16,
                                         getValue(I.getArgOperand(0)),
                                         DAG.getTargetConstant(0, sdl,
                                                               MVT::i32))));
    return;
  case Intrinsic::convert_from_fp16:
    setValue(&I, DAG.getNode(ISD::FP_EXTEND, sdl,
                             TLI.getValueType(DAG.getDataLayout(), I.getType()),
                             DAG.getNode(ISD::BITCAST, sdl, MVT::f16,
                                         getValue(I.getArgOperand(0)))));
    return;
  case Intrinsic::fptosi_sat: {
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    setValue(&I, DAG.getNode(ISD::FP_TO_SINT_SAT, sdl, VT,
                             getValue(I.getArgOperand(0)),
                             DAG.getValueType(VT.getScalarType())));
    return;
  }
  case Intrinsic::fptoui_sat: {
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    setValue(&I, DAG.getNode(ISD::FP_TO_UINT_SAT, sdl, VT,
                             getValue(I.getArgOperand(0)),
                             DAG.getValueType(VT.getScalarType())));
    return;
  }
  case Intrinsic::set_rounding:
    Res = DAG.getNode(ISD::SET_ROUNDING, sdl, MVT::Other,
                      {getRoot(), getValue(I.getArgOperand(0))});
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(0));
    return;
  case Intrinsic::is_fpclass: {
    const DataLayout DLayout = DAG.getDataLayout();
    EVT DestVT = TLI.getValueType(DLayout, I.getType());
    EVT ArgVT = TLI.getValueType(DLayout, I.getArgOperand(0)->getType());
    FPClassTest Test = static_cast<FPClassTest>(
        cast<ConstantInt>(I.getArgOperand(1))->getZExtValue());
    MachineFunction &MF = DAG.getMachineFunction();
    const Function &F = MF.getFunction();
    SDValue Op = getValue(I.getArgOperand(0));
    SDNodeFlags Flags;
    Flags.setNoFPExcept(
        !F.getAttributes().hasFnAttr(llvm::Attribute::StrictFP));
    // If ISD::IS_FPCLASS should be expanded, do it right now, because the
    // expansion can use illegal types. Making expansion early allows
    // legalizing these types prior to selection.
    if (!TLI.isOperationLegalOrCustom(ISD::IS_FPCLASS, ArgVT)) {
      SDValue Result = TLI.expandIS_FPCLASS(DestVT, Op, Test, Flags, sdl, DAG);
      setValue(&I, Result);
      return;
    }

    SDValue Check = DAG.getTargetConstant(Test, sdl, MVT::i32);
    SDValue V = DAG.getNode(ISD::IS_FPCLASS, sdl, DestVT, {Op, Check}, Flags);
    setValue(&I, V);
    return;
  }
  case Intrinsic::get_fpenv: {
    const DataLayout DLayout = DAG.getDataLayout();
    EVT EnvVT = TLI.getValueType(DLayout, I.getType());
    Align TempAlign = DAG.getEVTAlign(EnvVT);
    SDValue Chain = getRoot();
    // Use GET_FPENV if it is legal or custom. Otherwise use memory-based node
    // and temporary storage in stack.
    if (TLI.isOperationLegalOrCustom(ISD::GET_FPENV, EnvVT)) {
      Res = DAG.getNode(
          ISD::GET_FPENV, sdl,
          DAG.getVTList(TLI.getValueType(DAG.getDataLayout(), I.getType()),
                        MVT::Other),
          Chain);
    } else {
      SDValue Temp = DAG.CreateStackTemporary(EnvVT, TempAlign.value());
      int SPFI = cast<FrameIndexSDNode>(Temp.getNode())->getIndex();
      auto MPI =
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI);
      MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
          MPI, MachineMemOperand::MOStore, LocationSize::beforeOrAfterPointer(),
          TempAlign);
      Chain = DAG.getGetFPEnv(Chain, sdl, Temp, EnvVT, MMO);
      Res = DAG.getLoad(EnvVT, sdl, Chain, Temp, MPI);
    }
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;
  }
  case Intrinsic::set_fpenv: {
    const DataLayout DLayout = DAG.getDataLayout();
    SDValue Env = getValue(I.getArgOperand(0));
    EVT EnvVT = Env.getValueType();
    Align TempAlign = DAG.getEVTAlign(EnvVT);
    SDValue Chain = getRoot();
    // If SET_FPENV is custom or legal, use it. Otherwise use loading
    // environment from memory.
    if (TLI.isOperationLegalOrCustom(ISD::SET_FPENV, EnvVT)) {
      Chain = DAG.getNode(ISD::SET_FPENV, sdl, MVT::Other, Chain, Env);
    } else {
      // Allocate space in stack, copy environment bits into it and use this
      // memory in SET_FPENV_MEM.
      SDValue Temp = DAG.CreateStackTemporary(EnvVT, TempAlign.value());
      int SPFI = cast<FrameIndexSDNode>(Temp.getNode())->getIndex();
      auto MPI =
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI);
      Chain = DAG.getStore(Chain, sdl, Env, Temp, MPI, TempAlign,
                           MachineMemOperand::MOStore);
      MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
          MPI, MachineMemOperand::MOLoad, LocationSize::beforeOrAfterPointer(),
          TempAlign);
      Chain = DAG.getSetFPEnv(Chain, sdl, Temp, EnvVT, MMO);
    }
    DAG.setRoot(Chain);
    return;
  }
  case Intrinsic::reset_fpenv:
    DAG.setRoot(DAG.getNode(ISD::RESET_FPENV, sdl, MVT::Other, getRoot()));
    return;
  case Intrinsic::get_fpmode:
    Res = DAG.getNode(
        ISD::GET_FPMODE, sdl,
        DAG.getVTList(TLI.getValueType(DAG.getDataLayout(), I.getType()),
                      MVT::Other),
        DAG.getRoot());
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;
  case Intrinsic::set_fpmode:
    Res = DAG.getNode(ISD::SET_FPMODE, sdl, MVT::Other, {DAG.getRoot()},
                      getValue(I.getArgOperand(0)));
    DAG.setRoot(Res);
    return;
  case Intrinsic::reset_fpmode: {
    Res = DAG.getNode(ISD::RESET_FPMODE, sdl, MVT::Other, getRoot());
    DAG.setRoot(Res);
    return;
  }
  case Intrinsic::pcmarker: {
    SDValue Tmp = getValue(I.getArgOperand(0));
    DAG.setRoot(DAG.getNode(ISD::PCMARKER, sdl, MVT::Other, getRoot(), Tmp));
    return;
  }
  case Intrinsic::readcyclecounter: {
    SDValue Op = getRoot();
    Res = DAG.getNode(ISD::READCYCLECOUNTER, sdl,
                      DAG.getVTList(MVT::i64, MVT::Other), Op);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;
  }
  case Intrinsic::readsteadycounter: {
    SDValue Op = getRoot();
    Res = DAG.getNode(ISD::READSTEADYCOUNTER, sdl,
                      DAG.getVTList(MVT::i64, MVT::Other), Op);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;
  }
  case Intrinsic::bitreverse:
    setValue(&I, DAG.getNode(ISD::BITREVERSE, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0))));
    return;
  case Intrinsic::bswap:
    setValue(&I, DAG.getNode(ISD::BSWAP, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0))));
    return;
  case Intrinsic::cttz: {
    SDValue Arg = getValue(I.getArgOperand(0));
    ConstantInt *CI = cast<ConstantInt>(I.getArgOperand(1));
    EVT Ty = Arg.getValueType();
    setValue(&I, DAG.getNode(CI->isZero() ? ISD::CTTZ : ISD::CTTZ_ZERO_UNDEF,
                             sdl, Ty, Arg));
    return;
  }
  case Intrinsic::ctlz: {
    SDValue Arg = getValue(I.getArgOperand(0));
    ConstantInt *CI = cast<ConstantInt>(I.getArgOperand(1));
    EVT Ty = Arg.getValueType();
    setValue(&I, DAG.getNode(CI->isZero() ? ISD::CTLZ : ISD::CTLZ_ZERO_UNDEF,
                             sdl, Ty, Arg));
    return;
  }
  case Intrinsic::ctpop: {
    SDValue Arg = getValue(I.getArgOperand(0));
    EVT Ty = Arg.getValueType();
    setValue(&I, DAG.getNode(ISD::CTPOP, sdl, Ty, Arg));
    return;
  }
  case Intrinsic::fshl:
  case Intrinsic::fshr: {
    bool IsFSHL = Intrinsic == Intrinsic::fshl;
    SDValue X = getValue(I.getArgOperand(0));
    SDValue Y = getValue(I.getArgOperand(1));
    SDValue Z = getValue(I.getArgOperand(2));
    EVT VT = X.getValueType();

    if (X == Y) {
      auto RotateOpcode = IsFSHL ? ISD::ROTL : ISD::ROTR;
      setValue(&I, DAG.getNode(RotateOpcode, sdl, VT, X, Z));
    } else {
      auto FunnelOpcode = IsFSHL ? ISD::FSHL : ISD::FSHR;
      setValue(&I, DAG.getNode(FunnelOpcode, sdl, VT, X, Y, Z));
    }
    return;
  }
  case Intrinsic::sadd_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::SADDSAT, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::uadd_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::UADDSAT, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::ssub_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::SSUBSAT, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::usub_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::USUBSAT, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::sshl_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::SSHLSAT, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::ushl_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::USHLSAT, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::smul_fix:
  case Intrinsic::umul_fix:
  case Intrinsic::smul_fix_sat:
  case Intrinsic::umul_fix_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    setValue(&I, DAG.getNode(FixedPointIntrinsicToOpcode(Intrinsic), sdl,
                             Op1.getValueType(), Op1, Op2, Op3));
    return;
  }
  case Intrinsic::sdiv_fix:
  case Intrinsic::udiv_fix:
  case Intrinsic::sdiv_fix_sat:
  case Intrinsic::udiv_fix_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    setValue(&I, expandDivFix(FixedPointIntrinsicToOpcode(Intrinsic), sdl,
                              Op1, Op2, Op3, DAG, TLI));
    return;
  }
  case Intrinsic::smax: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::SMAX, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::smin: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::SMIN, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::umax: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::UMAX, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::umin: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::UMIN, sdl, Op1.getValueType(), Op1, Op2));
    return;
  }
  case Intrinsic::abs: {
    // TODO: Preserve "int min is poison" arg in SDAG?
    SDValue Op1 = getValue(I.getArgOperand(0));
    setValue(&I, DAG.getNode(ISD::ABS, sdl, Op1.getValueType(), Op1));
    return;
  }
  case Intrinsic::scmp: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    setValue(&I, DAG.getNode(ISD::SCMP, sdl, DestVT, Op1, Op2));
    break;
  }
  case Intrinsic::ucmp: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    setValue(&I, DAG.getNode(ISD::UCMP, sdl, DestVT, Op1, Op2));
    break;
  }
  case Intrinsic::stacksave: {
    SDValue Op = getRoot();
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    Res = DAG.getNode(ISD::STACKSAVE, sdl, DAG.getVTList(VT, MVT::Other), Op);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;
  }
  case Intrinsic::stackrestore:
    Res = getValue(I.getArgOperand(0));
    DAG.setRoot(DAG.getNode(ISD::STACKRESTORE, sdl, MVT::Other, getRoot(), Res));
    return;
  case Intrinsic::get_dynamic_area_offset: {
    SDValue Op = getRoot();
    EVT PtrTy = TLI.getFrameIndexTy(DAG.getDataLayout());
    EVT ResTy = TLI.getValueType(DAG.getDataLayout(), I.getType());
    // Result type for @llvm.get.dynamic.area.offset should match PtrTy for
    // target.
    if (PtrTy.getFixedSizeInBits() < ResTy.getFixedSizeInBits())
      report_fatal_error("Wrong result type for @llvm.get.dynamic.area.offset"
                         " intrinsic!");
    Res = DAG.getNode(ISD::GET_DYNAMIC_AREA_OFFSET, sdl, DAG.getVTList(ResTy),
                      Op);
    DAG.setRoot(Op);
    setValue(&I, Res);
    return;
  }
  case Intrinsic::stackguard: {
    MachineFunction &MF = DAG.getMachineFunction();
    const Module &M = *MF.getFunction().getParent();
    EVT PtrTy = TLI.getValueType(DAG.getDataLayout(), I.getType());
    SDValue Chain = getRoot();
    if (TLI.useLoadStackGuardNode()) {
      Res = getLoadStackGuard(DAG, sdl, Chain);
      Res = DAG.getPtrExtOrTrunc(Res, sdl, PtrTy);
    } else {
      const Value *Global = TLI.getSDagStackGuard(M);
      Align Align = DAG.getDataLayout().getPrefTypeAlign(Global->getType());
      Res = DAG.getLoad(PtrTy, sdl, Chain, getValue(Global),
                        MachinePointerInfo(Global, 0), Align,
                        MachineMemOperand::MOVolatile);
    }
    if (TLI.useStackGuardXorFP())
      Res = TLI.emitStackGuardXorFP(DAG, Res, sdl);
    DAG.setRoot(Chain);
    setValue(&I, Res);
    return;
  }
  case Intrinsic::stackprotector: {
    // Emit code into the DAG to store the stack guard onto the stack.
    MachineFunction &MF = DAG.getMachineFunction();
    MachineFrameInfo &MFI = MF.getFrameInfo();
    SDValue Src, Chain = getRoot();

    if (TLI.useLoadStackGuardNode())
      Src = getLoadStackGuard(DAG, sdl, Chain);
    else
      Src = getValue(I.getArgOperand(0));   // The guard's value.

    AllocaInst *Slot = cast<AllocaInst>(I.getArgOperand(1));

    int FI = FuncInfo.StaticAllocaMap[Slot];
    MFI.setStackProtectorIndex(FI);
    EVT PtrTy = TLI.getFrameIndexTy(DAG.getDataLayout());

    SDValue FIN = DAG.getFrameIndex(FI, PtrTy);

    // Store the stack protector onto the stack.
    Res = DAG.getStore(
        Chain, sdl, Src, FIN,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI),
        MaybeAlign(), MachineMemOperand::MOVolatile);
    setValue(&I, Res);
    DAG.setRoot(Res);
    return;
  }
  case Intrinsic::objectsize:
    llvm_unreachable("llvm.objectsize.* should have been lowered already");

  case Intrinsic::is_constant:
    llvm_unreachable("llvm.is.constant.* should have been lowered already");

  case Intrinsic::annotation:
  case Intrinsic::ptr_annotation:
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
    // Drop the intrinsic, but forward the value
    setValue(&I, getValue(I.getOperand(0)));
    return;

  case Intrinsic::assume:
  case Intrinsic::experimental_noalias_scope_decl:
  case Intrinsic::var_annotation:
  case Intrinsic::sideeffect:
    // Discard annotate attributes, noalias scope declarations, assumptions, and
    // artificial side-effects.
    return;

  case Intrinsic::codeview_annotation: {
    // Emit a label associated with this metadata.
    MachineFunction &MF = DAG.getMachineFunction();
    MCSymbol *Label = MF.getContext().createTempSymbol("annotation", true);
    Metadata *MD = cast<MetadataAsValue>(I.getArgOperand(0))->getMetadata();
    MF.addCodeViewAnnotation(Label, cast<MDNode>(MD));
    Res = DAG.getLabelNode(ISD::ANNOTATION_LABEL, sdl, getRoot(), Label);
    DAG.setRoot(Res);
    return;
  }

  case Intrinsic::init_trampoline: {
    const Function *F = cast<Function>(I.getArgOperand(1)->stripPointerCasts());

    SDValue Ops[6];
    Ops[0] = getRoot();
    Ops[1] = getValue(I.getArgOperand(0));
    Ops[2] = getValue(I.getArgOperand(1));
    Ops[3] = getValue(I.getArgOperand(2));
    Ops[4] = DAG.getSrcValue(I.getArgOperand(0));
    Ops[5] = DAG.getSrcValue(F);

    Res = DAG.getNode(ISD::INIT_TRAMPOLINE, sdl, MVT::Other, Ops);

    DAG.setRoot(Res);
    return;
  }
  case Intrinsic::adjust_trampoline:
    setValue(&I, DAG.getNode(ISD::ADJUST_TRAMPOLINE, sdl,
                             TLI.getPointerTy(DAG.getDataLayout()),
                             getValue(I.getArgOperand(0))));
    return;
  case Intrinsic::gcroot: {
    assert(DAG.getMachineFunction().getFunction().hasGC() &&
           "only valid in functions with gc specified, enforced by Verifier");
    assert(GFI && "implied by previous");
    const Value *Alloca = I.getArgOperand(0)->stripPointerCasts();
    const Constant *TypeMap = cast<Constant>(I.getArgOperand(1));

    FrameIndexSDNode *FI = cast<FrameIndexSDNode>(getValue(Alloca).getNode());
    GFI->addStackRoot(FI->getIndex(), TypeMap);
    return;
  }
  case Intrinsic::gcread:
  case Intrinsic::gcwrite:
    llvm_unreachable("GC failed to lower gcread/gcwrite intrinsics!");
  case Intrinsic::get_rounding:
    Res = DAG.getNode(ISD::GET_ROUNDING, sdl, {MVT::i32, MVT::Other}, getRoot());
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return;

  case Intrinsic::expect:
    // Just replace __builtin_expect(exp, c) with EXP.
    setValue(&I, getValue(I.getArgOperand(0)));
    return;

  case Intrinsic::ubsantrap:
  case Intrinsic::debugtrap:
  case Intrinsic::trap: {
    StringRef TrapFuncName =
        I.getAttributes().getFnAttr("trap-func-name").getValueAsString();
    if (TrapFuncName.empty()) {
      switch (Intrinsic) {
      case Intrinsic::trap:
        DAG.setRoot(DAG.getNode(ISD::TRAP, sdl, MVT::Other, getRoot()));
        break;
      case Intrinsic::debugtrap:
        DAG.setRoot(DAG.getNode(ISD::DEBUGTRAP, sdl, MVT::Other, getRoot()));
        break;
      case Intrinsic::ubsantrap:
        DAG.setRoot(DAG.getNode(
            ISD::UBSANTRAP, sdl, MVT::Other, getRoot(),
            DAG.getTargetConstant(
                cast<ConstantInt>(I.getArgOperand(0))->getZExtValue(), sdl,
                MVT::i32)));
        break;
      default: llvm_unreachable("unknown trap intrinsic");
      }
      return;
    }
    TargetLowering::ArgListTy Args;
    if (Intrinsic == Intrinsic::ubsantrap) {
      Args.push_back(TargetLoweringBase::ArgListEntry());
      Args[0].Val = I.getArgOperand(0);
      Args[0].Node = getValue(Args[0].Val);
      Args[0].Ty = Args[0].Val->getType();
    }

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(sdl).setChain(getRoot()).setLibCallee(
        CallingConv::C, I.getType(),
        DAG.getExternalSymbol(TrapFuncName.data(),
                              TLI.getPointerTy(DAG.getDataLayout())),
        std::move(Args));

    std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);
    DAG.setRoot(Result.second);
    return;
  }

  case Intrinsic::allow_runtime_check:
  case Intrinsic::allow_ubsan_check:
    setValue(&I, getValue(ConstantInt::getTrue(I.getType())));
    return;

  case Intrinsic::uadd_with_overflow:
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::umul_with_overflow:
  case Intrinsic::smul_with_overflow: {
    ISD::NodeType Op;
    switch (Intrinsic) {
    default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
    case Intrinsic::uadd_with_overflow: Op = ISD::UADDO; break;
    case Intrinsic::sadd_with_overflow: Op = ISD::SADDO; break;
    case Intrinsic::usub_with_overflow: Op = ISD::USUBO; break;
    case Intrinsic::ssub_with_overflow: Op = ISD::SSUBO; break;
    case Intrinsic::umul_with_overflow: Op = ISD::UMULO; break;
    case Intrinsic::smul_with_overflow: Op = ISD::SMULO; break;
    }
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));

    EVT ResultVT = Op1.getValueType();
    EVT OverflowVT = MVT::i1;
    if (ResultVT.isVector())
      OverflowVT = EVT::getVectorVT(
          *Context, OverflowVT, ResultVT.getVectorElementCount());

    SDVTList VTs = DAG.getVTList(ResultVT, OverflowVT);
    setValue(&I, DAG.getNode(Op, sdl, VTs, Op1, Op2));
    return;
  }
  case Intrinsic::prefetch: {
    SDValue Ops[5];
    unsigned rw = cast<ConstantInt>(I.getArgOperand(1))->getZExtValue();
    auto Flags = rw == 0 ? MachineMemOperand::MOLoad :MachineMemOperand::MOStore;
    Ops[0] = DAG.getRoot();
    Ops[1] = getValue(I.getArgOperand(0));
    Ops[2] = DAG.getTargetConstant(*cast<ConstantInt>(I.getArgOperand(1)), sdl,
                                   MVT::i32);
    Ops[3] = DAG.getTargetConstant(*cast<ConstantInt>(I.getArgOperand(2)), sdl,
                                   MVT::i32);
    Ops[4] = DAG.getTargetConstant(*cast<ConstantInt>(I.getArgOperand(3)), sdl,
                                   MVT::i32);
    SDValue Result = DAG.getMemIntrinsicNode(
        ISD::PREFETCH, sdl, DAG.getVTList(MVT::Other), Ops,
        EVT::getIntegerVT(*Context, 8), MachinePointerInfo(I.getArgOperand(0)),
        /* align */ std::nullopt, Flags);

    // Chain the prefetch in parallel with any pending loads, to stay out of
    // the way of later optimizations.
    PendingLoads.push_back(Result);
    Result = getRoot();
    DAG.setRoot(Result);
    return;
  }
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end: {
    bool IsStart = (Intrinsic == Intrinsic::lifetime_start);
    // Stack coloring is not enabled in O0, discard region information.
    if (TM.getOptLevel() == CodeGenOptLevel::None)
      return;

    const int64_t ObjectSize =
        cast<ConstantInt>(I.getArgOperand(0))->getSExtValue();
    Value *const ObjectPtr = I.getArgOperand(1);
    SmallVector<const Value *, 4> Allocas;
    getUnderlyingObjects(ObjectPtr, Allocas);

    for (const Value *Alloca : Allocas) {
      const AllocaInst *LifetimeObject = dyn_cast_or_null<AllocaInst>(Alloca);

      // Could not find an Alloca.
      if (!LifetimeObject)
        continue;

      // First check that the Alloca is static, otherwise it won't have a
      // valid frame index.
      auto SI = FuncInfo.StaticAllocaMap.find(LifetimeObject);
      if (SI == FuncInfo.StaticAllocaMap.end())
        return;

      const int FrameIndex = SI->second;
      int64_t Offset;
      if (GetPointerBaseWithConstantOffset(
              ObjectPtr, Offset, DAG.getDataLayout()) != LifetimeObject)
        Offset = -1; // Cannot determine offset from alloca to lifetime object.
      Res = DAG.getLifetimeNode(IsStart, sdl, getRoot(), FrameIndex, ObjectSize,
                                Offset);
      DAG.setRoot(Res);
    }
    return;
  }
  case Intrinsic::pseudoprobe: {
    auto Guid = cast<ConstantInt>(I.getArgOperand(0))->getZExtValue();
    auto Index = cast<ConstantInt>(I.getArgOperand(1))->getZExtValue();
    auto Attr = cast<ConstantInt>(I.getArgOperand(2))->getZExtValue();
    Res = DAG.getPseudoProbeNode(sdl, getRoot(), Guid, Index, Attr);
    DAG.setRoot(Res);
    return;
  }
  case Intrinsic::invariant_start:
    // Discard region information.
    setValue(&I,
             DAG.getUNDEF(TLI.getValueType(DAG.getDataLayout(), I.getType())));
    return;
  case Intrinsic::invariant_end:
    // Discard region information.
    return;
  case Intrinsic::clear_cache: {
    SDValue InputChain = DAG.getRoot();
    SDValue StartVal = getValue(I.getArgOperand(0));
    SDValue EndVal = getValue(I.getArgOperand(1));
    Res = DAG.getNode(ISD::CLEAR_CACHE, sdl, DAG.getVTList(MVT::Other),
                      {InputChain, StartVal, EndVal});
    setValue(&I, Res);
    DAG.setRoot(Res);
    return;
  }
  case Intrinsic::donothing:
  case Intrinsic::seh_try_begin:
  case Intrinsic::seh_scope_begin:
  case Intrinsic::seh_try_end:
  case Intrinsic::seh_scope_end:
    // ignore
    return;
  case Intrinsic::experimental_stackmap:
    visitStackmap(I);
    return;
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint:
    visitPatchpoint(I);
    return;
  case Intrinsic::experimental_gc_statepoint:
    LowerStatepoint(cast<GCStatepointInst>(I));
    return;
  case Intrinsic::experimental_gc_result:
    visitGCResult(cast<GCResultInst>(I));
    return;
  case Intrinsic::experimental_gc_relocate:
    visitGCRelocate(cast<GCRelocateInst>(I));
    return;
  case Intrinsic::instrprof_cover:
    llvm_unreachable("instrprof failed to lower a cover");
  case Intrinsic::instrprof_increment:
    llvm_unreachable("instrprof failed to lower an increment");
  case Intrinsic::instrprof_timestamp:
    llvm_unreachable("instrprof failed to lower a timestamp");
  case Intrinsic::instrprof_value_profile:
    llvm_unreachable("instrprof failed to lower a value profiling call");
  case Intrinsic::instrprof_mcdc_parameters:
    llvm_unreachable("instrprof failed to lower mcdc parameters");
  case Intrinsic::instrprof_mcdc_tvbitmap_update:
    llvm_unreachable("instrprof failed to lower an mcdc tvbitmap update");
  case Intrinsic::localescape: {
    MachineFunction &MF = DAG.getMachineFunction();
    const TargetInstrInfo *TII = DAG.getSubtarget().getInstrInfo();

    // Directly emit some LOCAL_ESCAPE machine instrs. Label assignment emission
    // is the same on all targets.
    for (unsigned Idx = 0, E = I.arg_size(); Idx < E; ++Idx) {
      Value *Arg = I.getArgOperand(Idx)->stripPointerCasts();
      if (isa<ConstantPointerNull>(Arg))
        continue; // Skip null pointers. They represent a hole in index space.
      AllocaInst *Slot = cast<AllocaInst>(Arg);
      assert(FuncInfo.StaticAllocaMap.count(Slot) &&
             "can only escape static allocas");
      int FI = FuncInfo.StaticAllocaMap[Slot];
      MCSymbol *FrameAllocSym = MF.getContext().getOrCreateFrameAllocSymbol(
          GlobalValue::dropLLVMManglingEscape(MF.getName()), Idx);
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, dl,
              TII->get(TargetOpcode::LOCAL_ESCAPE))
          .addSym(FrameAllocSym)
          .addFrameIndex(FI);
    }

    return;
  }

  case Intrinsic::localrecover: {
    // i8* @llvm.localrecover(i8* %fn, i8* %fp, i32 %idx)
    MachineFunction &MF = DAG.getMachineFunction();

    // Get the symbol that defines the frame offset.
    auto *Fn = cast<Function>(I.getArgOperand(0)->stripPointerCasts());
    auto *Idx = cast<ConstantInt>(I.getArgOperand(2));
    unsigned IdxVal =
        unsigned(Idx->getLimitedValue(std::numeric_limits<int>::max()));
    MCSymbol *FrameAllocSym = MF.getContext().getOrCreateFrameAllocSymbol(
        GlobalValue::dropLLVMManglingEscape(Fn->getName()), IdxVal);

    Value *FP = I.getArgOperand(1);
    SDValue FPVal = getValue(FP);
    EVT PtrVT = FPVal.getValueType();

    // Create a MCSymbol for the label to avoid any target lowering
    // that would make this PC relative.
    SDValue OffsetSym = DAG.getMCSymbol(FrameAllocSym, PtrVT);
    SDValue OffsetVal =
        DAG.getNode(ISD::LOCAL_RECOVER, sdl, PtrVT, OffsetSym);

    // Add the offset to the FP.
    SDValue Add = DAG.getMemBasePlusOffset(FPVal, OffsetVal, sdl);
    setValue(&I, Add);

    return;
  }

  case Intrinsic::eh_exceptionpointer:
  case Intrinsic::eh_exceptioncode: {
    // Get the exception pointer vreg, copy from it, and resize it to fit.
    const auto *CPI = cast<CatchPadInst>(I.getArgOperand(0));
    MVT PtrVT = TLI.getPointerTy(DAG.getDataLayout());
    const TargetRegisterClass *PtrRC = TLI.getRegClassFor(PtrVT);
    unsigned VReg = FuncInfo.getCatchPadExceptionPointerVReg(CPI, PtrRC);
    SDValue N = DAG.getCopyFromReg(DAG.getEntryNode(), sdl, VReg, PtrVT);
    if (Intrinsic == Intrinsic::eh_exceptioncode)
      N = DAG.getZExtOrTrunc(N, sdl, MVT::i32);
    setValue(&I, N);
    return;
  }
  case Intrinsic::xray_customevent: {
    // Here we want to make sure that the intrinsic behaves as if it has a
    // specific calling convention.
    const auto &Triple = DAG.getTarget().getTargetTriple();
    if (!Triple.isAArch64(64) && Triple.getArch() != Triple::x86_64)
      return;

    SmallVector<SDValue, 8> Ops;

    // We want to say that we always want the arguments in registers.
    SDValue LogEntryVal = getValue(I.getArgOperand(0));
    SDValue StrSizeVal = getValue(I.getArgOperand(1));
    SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Chain = getRoot();
    Ops.push_back(LogEntryVal);
    Ops.push_back(StrSizeVal);
    Ops.push_back(Chain);

    // We need to enforce the calling convention for the callsite, so that
    // argument ordering is enforced correctly, and that register allocation can
    // see that some registers may be assumed clobbered and have to preserve
    // them across calls to the intrinsic.
    MachineSDNode *MN = DAG.getMachineNode(TargetOpcode::PATCHABLE_EVENT_CALL,
                                           sdl, NodeTys, Ops);
    SDValue patchableNode = SDValue(MN, 0);
    DAG.setRoot(patchableNode);
    setValue(&I, patchableNode);
    return;
  }
  case Intrinsic::xray_typedevent: {
    // Here we want to make sure that the intrinsic behaves as if it has a
    // specific calling convention.
    const auto &Triple = DAG.getTarget().getTargetTriple();
    if (!Triple.isAArch64(64) && Triple.getArch() != Triple::x86_64)
      return;

    SmallVector<SDValue, 8> Ops;

    // We want to say that we always want the arguments in registers.
    // It's unclear to me how manipulating the selection DAG here forces callers
    // to provide arguments in registers instead of on the stack.
    SDValue LogTypeId = getValue(I.getArgOperand(0));
    SDValue LogEntryVal = getValue(I.getArgOperand(1));
    SDValue StrSizeVal = getValue(I.getArgOperand(2));
    SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Chain = getRoot();
    Ops.push_back(LogTypeId);
    Ops.push_back(LogEntryVal);
    Ops.push_back(StrSizeVal);
    Ops.push_back(Chain);

    // We need to enforce the calling convention for the callsite, so that
    // argument ordering is enforced correctly, and that register allocation can
    // see that some registers may be assumed clobbered and have to preserve
    // them across calls to the intrinsic.
    MachineSDNode *MN = DAG.getMachineNode(
        TargetOpcode::PATCHABLE_TYPED_EVENT_CALL, sdl, NodeTys, Ops);
    SDValue patchableNode = SDValue(MN, 0);
    DAG.setRoot(patchableNode);
    setValue(&I, patchableNode);
    return;
  }
  case Intrinsic::experimental_deoptimize:
    LowerDeoptimizeCall(&I);
    return;
  case Intrinsic::experimental_stepvector:
    visitStepVector(I);
    return;
  case Intrinsic::vector_reduce_fadd:
  case Intrinsic::vector_reduce_fmul:
  case Intrinsic::vector_reduce_add:
  case Intrinsic::vector_reduce_mul:
  case Intrinsic::vector_reduce_and:
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_xor:
  case Intrinsic::vector_reduce_smax:
  case Intrinsic::vector_reduce_smin:
  case Intrinsic::vector_reduce_umax:
  case Intrinsic::vector_reduce_umin:
  case Intrinsic::vector_reduce_fmax:
  case Intrinsic::vector_reduce_fmin:
  case Intrinsic::vector_reduce_fmaximum:
  case Intrinsic::vector_reduce_fminimum:
    visitVectorReduce(I, Intrinsic);
    return;

  case Intrinsic::icall_branch_funnel: {
    SmallVector<SDValue, 16> Ops;
    Ops.push_back(getValue(I.getArgOperand(0)));

    int64_t Offset;
    auto *Base = dyn_cast<GlobalObject>(GetPointerBaseWithConstantOffset(
        I.getArgOperand(1), Offset, DAG.getDataLayout()));
    if (!Base)
      report_fatal_error(
          "llvm.icall.branch.funnel operand must be a GlobalValue");
    Ops.push_back(DAG.getTargetGlobalAddress(Base, sdl, MVT::i64, 0));

    struct BranchFunnelTarget {
      int64_t Offset;
      SDValue Target;
    };
    SmallVector<BranchFunnelTarget, 8> Targets;

    for (unsigned Op = 1, N = I.arg_size(); Op != N; Op += 2) {
      auto *ElemBase = dyn_cast<GlobalObject>(GetPointerBaseWithConstantOffset(
          I.getArgOperand(Op), Offset, DAG.getDataLayout()));
      if (ElemBase != Base)
        report_fatal_error("all llvm.icall.branch.funnel operands must refer "
                           "to the same GlobalValue");

      SDValue Val = getValue(I.getArgOperand(Op + 1));
      auto *GA = dyn_cast<GlobalAddressSDNode>(Val);
      if (!GA)
        report_fatal_error(
            "llvm.icall.branch.funnel operand must be a GlobalValue");
      Targets.push_back({Offset, DAG.getTargetGlobalAddress(
                                     GA->getGlobal(), sdl, Val.getValueType(),
                                     GA->getOffset())});
    }
    llvm::sort(Targets,
               [](const BranchFunnelTarget &T1, const BranchFunnelTarget &T2) {
                 return T1.Offset < T2.Offset;
               });

    for (auto &T : Targets) {
      Ops.push_back(DAG.getTargetConstant(T.Offset, sdl, MVT::i32));
      Ops.push_back(T.Target);
    }

    Ops.push_back(DAG.getRoot()); // Chain
    SDValue N(DAG.getMachineNode(TargetOpcode::ICALL_BRANCH_FUNNEL, sdl,
                                 MVT::Other, Ops),
              0);
    DAG.setRoot(N);
    setValue(&I, N);
    HasTailCall = true;
    return;
  }

  case Intrinsic::wasm_landingpad_index:
    // Information this intrinsic contained has been transferred to
    // MachineFunction in SelectionDAGISel::PrepareEHLandingPad. We can safely
    // delete it now.
    return;

  case Intrinsic::aarch64_settag:
  case Intrinsic::aarch64_settag_zero: {
    const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
    bool ZeroMemory = Intrinsic == Intrinsic::aarch64_settag_zero;
    SDValue Val = TSI.EmitTargetCodeForSetTag(
        DAG, sdl, getRoot(), getValue(I.getArgOperand(0)),
        getValue(I.getArgOperand(1)), MachinePointerInfo(I.getArgOperand(0)),
        ZeroMemory);
    DAG.setRoot(Val);
    setValue(&I, Val);
    return;
  }
  case Intrinsic::amdgcn_cs_chain: {
    assert(I.arg_size() == 5 && "Additional args not supported yet");
    assert(cast<ConstantInt>(I.getOperand(4))->isZero() &&
           "Non-zero flags not supported yet");

    // At this point we don't care if it's amdgpu_cs_chain or
    // amdgpu_cs_chain_preserve.
    CallingConv::ID CC = CallingConv::AMDGPU_CS_Chain;

    Type *RetTy = I.getType();
    assert(RetTy->isVoidTy() && "Should not return");

    SDValue Callee = getValue(I.getOperand(0));

    // We only have 2 actual args: one for the SGPRs and one for the VGPRs.
    // We'll also tack the value of the EXEC mask at the end.
    TargetLowering::ArgListTy Args;
    Args.reserve(3);

    for (unsigned Idx : {2, 3, 1}) {
      TargetLowering::ArgListEntry Arg;
      Arg.Node = getValue(I.getOperand(Idx));
      Arg.Ty = I.getOperand(Idx)->getType();
      Arg.setAttributes(&I, Idx);
      Args.push_back(Arg);
    }

    assert(Args[0].IsInReg && "SGPR args should be marked inreg");
    assert(!Args[1].IsInReg && "VGPR args should not be marked inreg");
    Args[2].IsInReg = true; // EXEC should be inreg

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(getCurSDLoc())
        .setChain(getRoot())
        .setCallee(CC, RetTy, Callee, std::move(Args))
        .setNoReturn(true)
        .setTailCall(true)
        .setConvergent(I.isConvergent());
    CLI.CB = &I;
    std::pair<SDValue, SDValue> Result =
        lowerInvokable(CLI, /*EHPadBB*/ nullptr);
    (void)Result;
    assert(!Result.first.getNode() && !Result.second.getNode() &&
           "Should've lowered as tail call");

    HasTailCall = true;
    return;
  }
  case Intrinsic::ptrmask: {
    SDValue Ptr = getValue(I.getOperand(0));
    SDValue Mask = getValue(I.getOperand(1));

    // On arm64_32, pointers are 32 bits when stored in memory, but
    // zero-extended to 64 bits when in registers.  Thus the mask is 32 bits to
    // match the index type, but the pointer is 64 bits, so the the mask must be
    // zero-extended up to 64 bits to match the pointer.
    EVT PtrVT =
        TLI.getValueType(DAG.getDataLayout(), I.getOperand(0)->getType());
    EVT MemVT =
        TLI.getMemValueType(DAG.getDataLayout(), I.getOperand(0)->getType());
    assert(PtrVT == Ptr.getValueType());
    assert(MemVT == Mask.getValueType());
    if (MemVT != PtrVT)
      Mask = DAG.getPtrExtOrTrunc(Mask, sdl, PtrVT);

    setValue(&I, DAG.getNode(ISD::AND, sdl, PtrVT, Ptr, Mask));
    return;
  }
  case Intrinsic::threadlocal_address: {
    setValue(&I, getValue(I.getOperand(0)));
    return;
  }
  case Intrinsic::get_active_lane_mask: {
    EVT CCVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    SDValue Index = getValue(I.getOperand(0));
    EVT ElementVT = Index.getValueType();

    if (!TLI.shouldExpandGetActiveLaneMask(CCVT, ElementVT)) {
      visitTargetIntrinsic(I, Intrinsic);
      return;
    }

    SDValue TripCount = getValue(I.getOperand(1));
    EVT VecTy = EVT::getVectorVT(*DAG.getContext(), ElementVT,
                                 CCVT.getVectorElementCount());

    SDValue VectorIndex = DAG.getSplat(VecTy, sdl, Index);
    SDValue VectorTripCount = DAG.getSplat(VecTy, sdl, TripCount);
    SDValue VectorStep = DAG.getStepVector(sdl, VecTy);
    SDValue VectorInduction = DAG.getNode(
        ISD::UADDSAT, sdl, VecTy, VectorIndex, VectorStep);
    SDValue SetCC = DAG.getSetCC(sdl, CCVT, VectorInduction,
                                 VectorTripCount, ISD::CondCode::SETULT);
    setValue(&I, SetCC);
    return;
  }
  case Intrinsic::experimental_get_vector_length: {
    assert(cast<ConstantInt>(I.getOperand(1))->getSExtValue() > 0 &&
           "Expected positive VF");
    unsigned VF = cast<ConstantInt>(I.getOperand(1))->getZExtValue();
    bool IsScalable = cast<ConstantInt>(I.getOperand(2))->isOne();

    SDValue Count = getValue(I.getOperand(0));
    EVT CountVT = Count.getValueType();

    if (!TLI.shouldExpandGetVectorLength(CountVT, VF, IsScalable)) {
      visitTargetIntrinsic(I, Intrinsic);
      return;
    }

    // Expand to a umin between the trip count and the maximum elements the type
    // can hold.
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());

    // Extend the trip count to at least the result VT.
    if (CountVT.bitsLT(VT)) {
      Count = DAG.getNode(ISD::ZERO_EXTEND, sdl, VT, Count);
      CountVT = VT;
    }

    SDValue MaxEVL = DAG.getElementCount(sdl, CountVT,
                                         ElementCount::get(VF, IsScalable));

    SDValue UMin = DAG.getNode(ISD::UMIN, sdl, CountVT, Count, MaxEVL);
    // Clip to the result type if needed.
    SDValue Trunc = DAG.getNode(ISD::TRUNCATE, sdl, VT, UMin);

    setValue(&I, Trunc);
    return;
  }
  case Intrinsic::experimental_vector_partial_reduce_add: {
    SDValue OpNode = getValue(I.getOperand(1));
    EVT ReducedTy = EVT::getEVT(I.getType());
    EVT FullTy = OpNode.getValueType();

    unsigned Stride = ReducedTy.getVectorMinNumElements();
    unsigned ScaleFactor = FullTy.getVectorMinNumElements() / Stride;

    // Collect all of the subvectors
    std::deque<SDValue> Subvectors;
    Subvectors.push_back(getValue(I.getOperand(0)));
    for (unsigned i = 0; i < ScaleFactor; i++) {
      auto SourceIndex = DAG.getVectorIdxConstant(i * Stride, sdl);
      Subvectors.push_back(DAG.getNode(ISD::EXTRACT_SUBVECTOR, sdl, ReducedTy,
                                       {OpNode, SourceIndex}));
    }

    // Flatten the subvector tree
    while (Subvectors.size() > 1) {
      Subvectors.push_back(DAG.getNode(ISD::ADD, sdl, ReducedTy,
                                       {Subvectors[0], Subvectors[1]}));
      Subvectors.pop_front();
      Subvectors.pop_front();
    }

    assert(Subvectors.size() == 1 &&
           "There should only be one subvector after tree flattening");

    setValue(&I, Subvectors[0]);
    return;
  }
  case Intrinsic::experimental_cttz_elts: {
    auto DL = getCurSDLoc();
    SDValue Op = getValue(I.getOperand(0));
    EVT OpVT = Op.getValueType();

    if (!TLI.shouldExpandCttzElements(OpVT)) {
      visitTargetIntrinsic(I, Intrinsic);
      return;
    }

    if (OpVT.getScalarType() != MVT::i1) {
      // Compare the input vector elements to zero & use to count trailing zeros
      SDValue AllZero = DAG.getConstant(0, DL, OpVT);
      OpVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                              OpVT.getVectorElementCount());
      Op = DAG.getSetCC(DL, OpVT, Op, AllZero, ISD::SETNE);
    }

    // If the zero-is-poison flag is set, we can assume the upper limit
    // of the result is VF-1.
    bool ZeroIsPoison =
        !cast<ConstantSDNode>(getValue(I.getOperand(1)))->isZero();
    ConstantRange VScaleRange(1, true); // Dummy value.
    if (isa<ScalableVectorType>(I.getOperand(0)->getType()))
      VScaleRange = getVScaleRange(I.getCaller(), 64);
    unsigned EltWidth = TLI.getBitWidthForCttzElements(
        I.getType(), OpVT.getVectorElementCount(), ZeroIsPoison, &VScaleRange);

    MVT NewEltTy = MVT::getIntegerVT(EltWidth);

    // Create the new vector type & get the vector length
    EVT NewVT = EVT::getVectorVT(*DAG.getContext(), NewEltTy,
                                 OpVT.getVectorElementCount());

    SDValue VL =
        DAG.getElementCount(DL, NewEltTy, OpVT.getVectorElementCount());

    SDValue StepVec = DAG.getStepVector(DL, NewVT);
    SDValue SplatVL = DAG.getSplat(NewVT, DL, VL);
    SDValue StepVL = DAG.getNode(ISD::SUB, DL, NewVT, SplatVL, StepVec);
    SDValue Ext = DAG.getNode(ISD::SIGN_EXTEND, DL, NewVT, Op);
    SDValue And = DAG.getNode(ISD::AND, DL, NewVT, StepVL, Ext);
    SDValue Max = DAG.getNode(ISD::VECREDUCE_UMAX, DL, NewEltTy, And);
    SDValue Sub = DAG.getNode(ISD::SUB, DL, NewEltTy, VL, Max);

    EVT RetTy = TLI.getValueType(DAG.getDataLayout(), I.getType());
    SDValue Ret = DAG.getZExtOrTrunc(Sub, DL, RetTy);

    setValue(&I, Ret);
    return;
  }
  case Intrinsic::vector_insert: {
    SDValue Vec = getValue(I.getOperand(0));
    SDValue SubVec = getValue(I.getOperand(1));
    SDValue Index = getValue(I.getOperand(2));

    // The intrinsic's index type is i64, but the SDNode requires an index type
    // suitable for the target. Convert the index as required.
    MVT VectorIdxTy = TLI.getVectorIdxTy(DAG.getDataLayout());
    if (Index.getValueType() != VectorIdxTy)
      Index = DAG.getVectorIdxConstant(Index->getAsZExtVal(), sdl);

    EVT ResultVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    setValue(&I, DAG.getNode(ISD::INSERT_SUBVECTOR, sdl, ResultVT, Vec, SubVec,
                             Index));
    return;
  }
  case Intrinsic::vector_extract: {
    SDValue Vec = getValue(I.getOperand(0));
    SDValue Index = getValue(I.getOperand(1));
    EVT ResultVT = TLI.getValueType(DAG.getDataLayout(), I.getType());

    // The intrinsic's index type is i64, but the SDNode requires an index type
    // suitable for the target. Convert the index as required.
    MVT VectorIdxTy = TLI.getVectorIdxTy(DAG.getDataLayout());
    if (Index.getValueType() != VectorIdxTy)
      Index = DAG.getVectorIdxConstant(Index->getAsZExtVal(), sdl);

    setValue(&I,
             DAG.getNode(ISD::EXTRACT_SUBVECTOR, sdl, ResultVT, Vec, Index));
    return;
  }
  case Intrinsic::vector_reverse:
    visitVectorReverse(I);
    return;
  case Intrinsic::vector_splice:
    visitVectorSplice(I);
    return;
  case Intrinsic::callbr_landingpad:
    visitCallBrLandingPad(I);
    return;
  case Intrinsic::vector_interleave2:
    visitVectorInterleave(I);
    return;
  case Intrinsic::vector_deinterleave2:
    visitVectorDeinterleave(I);
    return;
  case Intrinsic::experimental_vector_compress:
    setValue(&I, DAG.getNode(ISD::VECTOR_COMPRESS, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)),
                             getValue(I.getArgOperand(2)), Flags));
    return;
  case Intrinsic::experimental_convergence_anchor:
  case Intrinsic::experimental_convergence_entry:
  case Intrinsic::experimental_convergence_loop:
    visitConvergenceControl(I, Intrinsic);
    return;
  case Intrinsic::experimental_vector_histogram_add: {
    visitVectorHistogram(I, Intrinsic);
    return;
  }
  }
}

void SelectionDAGBuilder::visitConstrainedFPIntrinsic(
    const ConstrainedFPIntrinsic &FPI) {
  SDLoc sdl = getCurSDLoc();

  // We do not need to serialize constrained FP intrinsics against
  // each other or against (nonvolatile) loads, so they can be
  // chained like loads.
  SDValue Chain = DAG.getRoot();
  SmallVector<SDValue, 4> Opers;
  Opers.push_back(Chain);
  for (unsigned I = 0, E = FPI.getNonMetadataArgCount(); I != E; ++I)
    Opers.push_back(getValue(FPI.getArgOperand(I)));

  auto pushOutChain = [this](SDValue Result, fp::ExceptionBehavior EB) {
    assert(Result.getNode()->getNumValues() == 2);

    // Push node to the appropriate list so that future instructions can be
    // chained up correctly.
    SDValue OutChain = Result.getValue(1);
    switch (EB) {
    case fp::ExceptionBehavior::ebIgnore:
      // The only reason why ebIgnore nodes still need to be chained is that
      // they might depend on the current rounding mode, and therefore must
      // not be moved across instruction that may change that mode.
      [[fallthrough]];
    case fp::ExceptionBehavior::ebMayTrap:
      // These must not be moved across calls or instructions that may change
      // floating-point exception masks.
      PendingConstrainedFP.push_back(OutChain);
      break;
    case fp::ExceptionBehavior::ebStrict:
      // These must not be moved across calls or instructions that may change
      // floating-point exception masks or read floating-point exception flags.
      // In addition, they cannot be optimized out even if unused.
      PendingConstrainedFPStrict.push_back(OutChain);
      break;
    }
  };

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), FPI.getType());
  SDVTList VTs = DAG.getVTList(VT, MVT::Other);
  fp::ExceptionBehavior EB = *FPI.getExceptionBehavior();

  SDNodeFlags Flags;
  if (EB == fp::ExceptionBehavior::ebIgnore)
    Flags.setNoFPExcept(true);

  if (auto *FPOp = dyn_cast<FPMathOperator>(&FPI))
    Flags.copyFMF(*FPOp);

  unsigned Opcode;
  switch (FPI.getIntrinsicID()) {
  default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case Intrinsic::INTRINSIC:                                                   \
    Opcode = ISD::STRICT_##DAGN;                                               \
    break;
#include "llvm/IR/ConstrainedOps.def"
  case Intrinsic::experimental_constrained_fmuladd: {
    Opcode = ISD::STRICT_FMA;
    // Break fmuladd into fmul and fadd.
    if (TM.Options.AllowFPOpFusion == FPOpFusion::Strict ||
        !TLI.isFMAFasterThanFMulAndFAdd(DAG.getMachineFunction(), VT)) {
      Opers.pop_back();
      SDValue Mul = DAG.getNode(ISD::STRICT_FMUL, sdl, VTs, Opers, Flags);
      pushOutChain(Mul, EB);
      Opcode = ISD::STRICT_FADD;
      Opers.clear();
      Opers.push_back(Mul.getValue(1));
      Opers.push_back(Mul.getValue(0));
      Opers.push_back(getValue(FPI.getArgOperand(2)));
    }
    break;
  }
  }

  // A few strict DAG nodes carry additional operands that are not
  // set up by the default code above.
  switch (Opcode) {
  default: break;
  case ISD::STRICT_FP_ROUND:
    Opers.push_back(
        DAG.getTargetConstant(0, sdl, TLI.getPointerTy(DAG.getDataLayout())));
    break;
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS: {
    auto *FPCmp = dyn_cast<ConstrainedFPCmpIntrinsic>(&FPI);
    ISD::CondCode Condition = getFCmpCondCode(FPCmp->getPredicate());
    if (TM.Options.NoNaNsFPMath)
      Condition = getFCmpCodeWithoutNaN(Condition);
    Opers.push_back(DAG.getCondCode(Condition));
    break;
  }
  }

  SDValue Result = DAG.getNode(Opcode, sdl, VTs, Opers, Flags);
  pushOutChain(Result, EB);

  SDValue FPResult = Result.getValue(0);
  setValue(&FPI, FPResult);
}

static unsigned getISDForVPIntrinsic(const VPIntrinsic &VPIntrin) {
  std::optional<unsigned> ResOPC;
  switch (VPIntrin.getIntrinsicID()) {
  case Intrinsic::vp_ctlz: {
    bool IsZeroUndef = cast<ConstantInt>(VPIntrin.getArgOperand(1))->isOne();
    ResOPC = IsZeroUndef ? ISD::VP_CTLZ_ZERO_UNDEF : ISD::VP_CTLZ;
    break;
  }
  case Intrinsic::vp_cttz: {
    bool IsZeroUndef = cast<ConstantInt>(VPIntrin.getArgOperand(1))->isOne();
    ResOPC = IsZeroUndef ? ISD::VP_CTTZ_ZERO_UNDEF : ISD::VP_CTTZ;
    break;
  }
  case Intrinsic::vp_cttz_elts: {
    bool IsZeroPoison = cast<ConstantInt>(VPIntrin.getArgOperand(1))->isOne();
    ResOPC = IsZeroPoison ? ISD::VP_CTTZ_ELTS_ZERO_UNDEF : ISD::VP_CTTZ_ELTS;
    break;
  }
#define HELPER_MAP_VPID_TO_VPSD(VPID, VPSD)                                    \
  case Intrinsic::VPID:                                                        \
    ResOPC = ISD::VPSD;                                                        \
    break;
#include "llvm/IR/VPIntrinsics.def"
  }

  if (!ResOPC)
    llvm_unreachable(
        "Inconsistency: no SDNode available for this VPIntrinsic!");

  if (*ResOPC == ISD::VP_REDUCE_SEQ_FADD ||
      *ResOPC == ISD::VP_REDUCE_SEQ_FMUL) {
    if (VPIntrin.getFastMathFlags().allowReassoc())
      return *ResOPC == ISD::VP_REDUCE_SEQ_FADD ? ISD::VP_REDUCE_FADD
                                                : ISD::VP_REDUCE_FMUL;
  }

  return *ResOPC;
}

void SelectionDAGBuilder::visitVPLoad(
    const VPIntrinsic &VPIntrin, EVT VT,
    const SmallVectorImpl<SDValue> &OpValues) {
  SDLoc DL = getCurSDLoc();
  Value *PtrOperand = VPIntrin.getArgOperand(0);
  MaybeAlign Alignment = VPIntrin.getPointerAlignment();
  AAMDNodes AAInfo = VPIntrin.getAAMetadata();
  const MDNode *Ranges = getRangeMetadata(VPIntrin);
  SDValue LD;
  // Do not serialize variable-length loads of constant memory with
  // anything.
  if (!Alignment)
    Alignment = DAG.getEVTAlign(VT);
  MemoryLocation ML = MemoryLocation::getAfter(PtrOperand, AAInfo);
  bool AddToChain = !AA || !AA->pointsToConstantMemory(ML);
  SDValue InChain = AddToChain ? DAG.getRoot() : DAG.getEntryNode();
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(PtrOperand), MachineMemOperand::MOLoad,
      LocationSize::beforeOrAfterPointer(), *Alignment, AAInfo, Ranges);
  LD = DAG.getLoadVP(VT, DL, InChain, OpValues[0], OpValues[1], OpValues[2],
                     MMO, false /*IsExpanding */);
  if (AddToChain)
    PendingLoads.push_back(LD.getValue(1));
  setValue(&VPIntrin, LD);
}

void SelectionDAGBuilder::visitVPGather(
    const VPIntrinsic &VPIntrin, EVT VT,
    const SmallVectorImpl<SDValue> &OpValues) {
  SDLoc DL = getCurSDLoc();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  Value *PtrOperand = VPIntrin.getArgOperand(0);
  MaybeAlign Alignment = VPIntrin.getPointerAlignment();
  AAMDNodes AAInfo = VPIntrin.getAAMetadata();
  const MDNode *Ranges = getRangeMetadata(VPIntrin);
  SDValue LD;
  if (!Alignment)
    Alignment = DAG.getEVTAlign(VT.getScalarType());
  unsigned AS =
    PtrOperand->getType()->getScalarType()->getPointerAddressSpace();
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(AS), MachineMemOperand::MOLoad,
      LocationSize::beforeOrAfterPointer(), *Alignment, AAInfo, Ranges);
  SDValue Base, Index, Scale;
  ISD::MemIndexType IndexType;
  bool UniformBase = getUniformBase(PtrOperand, Base, Index, IndexType, Scale,
                                    this, VPIntrin.getParent(),
                                    VT.getScalarStoreSize());
  if (!UniformBase) {
    Base = DAG.getConstant(0, DL, TLI.getPointerTy(DAG.getDataLayout()));
    Index = getValue(PtrOperand);
    IndexType = ISD::SIGNED_SCALED;
    Scale = DAG.getTargetConstant(1, DL, TLI.getPointerTy(DAG.getDataLayout()));
  }
  EVT IdxVT = Index.getValueType();
  EVT EltTy = IdxVT.getVectorElementType();
  if (TLI.shouldExtendGSIndex(IdxVT, EltTy)) {
    EVT NewIdxVT = IdxVT.changeVectorElementType(EltTy);
    Index = DAG.getNode(ISD::SIGN_EXTEND, DL, NewIdxVT, Index);
  }
  LD = DAG.getGatherVP(
      DAG.getVTList(VT, MVT::Other), VT, DL,
      {DAG.getRoot(), Base, Index, Scale, OpValues[1], OpValues[2]}, MMO,
      IndexType);
  PendingLoads.push_back(LD.getValue(1));
  setValue(&VPIntrin, LD);
}

void SelectionDAGBuilder::visitVPStore(
    const VPIntrinsic &VPIntrin, const SmallVectorImpl<SDValue> &OpValues) {
  SDLoc DL = getCurSDLoc();
  Value *PtrOperand = VPIntrin.getArgOperand(1);
  EVT VT = OpValues[0].getValueType();
  MaybeAlign Alignment = VPIntrin.getPointerAlignment();
  AAMDNodes AAInfo = VPIntrin.getAAMetadata();
  SDValue ST;
  if (!Alignment)
    Alignment = DAG.getEVTAlign(VT);
  SDValue Ptr = OpValues[1];
  SDValue Offset = DAG.getUNDEF(Ptr.getValueType());
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(PtrOperand), MachineMemOperand::MOStore,
      LocationSize::beforeOrAfterPointer(), *Alignment, AAInfo);
  ST = DAG.getStoreVP(getMemoryRoot(), DL, OpValues[0], Ptr, Offset,
                      OpValues[2], OpValues[3], VT, MMO, ISD::UNINDEXED,
                      /* IsTruncating */ false, /*IsCompressing*/ false);
  DAG.setRoot(ST);
  setValue(&VPIntrin, ST);
}

void SelectionDAGBuilder::visitVPScatter(
    const VPIntrinsic &VPIntrin, const SmallVectorImpl<SDValue> &OpValues) {
  SDLoc DL = getCurSDLoc();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  Value *PtrOperand = VPIntrin.getArgOperand(1);
  EVT VT = OpValues[0].getValueType();
  MaybeAlign Alignment = VPIntrin.getPointerAlignment();
  AAMDNodes AAInfo = VPIntrin.getAAMetadata();
  SDValue ST;
  if (!Alignment)
    Alignment = DAG.getEVTAlign(VT.getScalarType());
  unsigned AS =
      PtrOperand->getType()->getScalarType()->getPointerAddressSpace();
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(AS), MachineMemOperand::MOStore,
      LocationSize::beforeOrAfterPointer(), *Alignment, AAInfo);
  SDValue Base, Index, Scale;
  ISD::MemIndexType IndexType;
  bool UniformBase = getUniformBase(PtrOperand, Base, Index, IndexType, Scale,
                                    this, VPIntrin.getParent(),
                                    VT.getScalarStoreSize());
  if (!UniformBase) {
    Base = DAG.getConstant(0, DL, TLI.getPointerTy(DAG.getDataLayout()));
    Index = getValue(PtrOperand);
    IndexType = ISD::SIGNED_SCALED;
    Scale =
      DAG.getTargetConstant(1, DL, TLI.getPointerTy(DAG.getDataLayout()));
  }
  EVT IdxVT = Index.getValueType();
  EVT EltTy = IdxVT.getVectorElementType();
  if (TLI.shouldExtendGSIndex(IdxVT, EltTy)) {
    EVT NewIdxVT = IdxVT.changeVectorElementType(EltTy);
    Index = DAG.getNode(ISD::SIGN_EXTEND, DL, NewIdxVT, Index);
  }
  ST = DAG.getScatterVP(DAG.getVTList(MVT::Other), VT, DL,
                        {getMemoryRoot(), OpValues[0], Base, Index, Scale,
                         OpValues[2], OpValues[3]},
                        MMO, IndexType);
  DAG.setRoot(ST);
  setValue(&VPIntrin, ST);
}

void SelectionDAGBuilder::visitVPStridedLoad(
    const VPIntrinsic &VPIntrin, EVT VT,
    const SmallVectorImpl<SDValue> &OpValues) {
  SDLoc DL = getCurSDLoc();
  Value *PtrOperand = VPIntrin.getArgOperand(0);
  MaybeAlign Alignment = VPIntrin.getPointerAlignment();
  if (!Alignment)
    Alignment = DAG.getEVTAlign(VT.getScalarType());
  AAMDNodes AAInfo = VPIntrin.getAAMetadata();
  const MDNode *Ranges = getRangeMetadata(VPIntrin);
  MemoryLocation ML = MemoryLocation::getAfter(PtrOperand, AAInfo);
  bool AddToChain = !AA || !AA->pointsToConstantMemory(ML);
  SDValue InChain = AddToChain ? DAG.getRoot() : DAG.getEntryNode();
  unsigned AS = PtrOperand->getType()->getPointerAddressSpace();
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(AS), MachineMemOperand::MOLoad,
      LocationSize::beforeOrAfterPointer(), *Alignment, AAInfo, Ranges);

  SDValue LD = DAG.getStridedLoadVP(VT, DL, InChain, OpValues[0], OpValues[1],
                                    OpValues[2], OpValues[3], MMO,
                                    false /*IsExpanding*/);

  if (AddToChain)
    PendingLoads.push_back(LD.getValue(1));
  setValue(&VPIntrin, LD);
}

void SelectionDAGBuilder::visitVPStridedStore(
    const VPIntrinsic &VPIntrin, const SmallVectorImpl<SDValue> &OpValues) {
  SDLoc DL = getCurSDLoc();
  Value *PtrOperand = VPIntrin.getArgOperand(1);
  EVT VT = OpValues[0].getValueType();
  MaybeAlign Alignment = VPIntrin.getPointerAlignment();
  if (!Alignment)
    Alignment = DAG.getEVTAlign(VT.getScalarType());
  AAMDNodes AAInfo = VPIntrin.getAAMetadata();
  unsigned AS = PtrOperand->getType()->getPointerAddressSpace();
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(AS), MachineMemOperand::MOStore,
      LocationSize::beforeOrAfterPointer(), *Alignment, AAInfo);

  SDValue ST = DAG.getStridedStoreVP(
      getMemoryRoot(), DL, OpValues[0], OpValues[1],
      DAG.getUNDEF(OpValues[1].getValueType()), OpValues[2], OpValues[3],
      OpValues[4], VT, MMO, ISD::UNINDEXED, /*IsTruncating*/ false,
      /*IsCompressing*/ false);

  DAG.setRoot(ST);
  setValue(&VPIntrin, ST);
}

void SelectionDAGBuilder::visitVPCmp(const VPCmpIntrinsic &VPIntrin) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc DL = getCurSDLoc();

  ISD::CondCode Condition;
  CmpInst::Predicate CondCode = VPIntrin.getPredicate();
  bool IsFP = VPIntrin.getOperand(0)->getType()->isFPOrFPVectorTy();
  if (IsFP) {
    // FIXME: Regular fcmps are FPMathOperators which may have fast-math (nnan)
    // flags, but calls that don't return floating-point types can't be
    // FPMathOperators, like vp.fcmp. This affects constrained fcmp too.
    Condition = getFCmpCondCode(CondCode);
    if (TM.Options.NoNaNsFPMath)
      Condition = getFCmpCodeWithoutNaN(Condition);
  } else {
    Condition = getICmpCondCode(CondCode);
  }

  SDValue Op1 = getValue(VPIntrin.getOperand(0));
  SDValue Op2 = getValue(VPIntrin.getOperand(1));
  // #2 is the condition code
  SDValue MaskOp = getValue(VPIntrin.getOperand(3));
  SDValue EVL = getValue(VPIntrin.getOperand(4));
  MVT EVLParamVT = TLI.getVPExplicitVectorLengthTy();
  assert(EVLParamVT.isScalarInteger() && EVLParamVT.bitsGE(MVT::i32) &&
         "Unexpected target EVL type");
  EVL = DAG.getNode(ISD::ZERO_EXTEND, DL, EVLParamVT, EVL);

  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        VPIntrin.getType());
  setValue(&VPIntrin,
           DAG.getSetCCVP(DL, DestVT, Op1, Op2, Condition, MaskOp, EVL));
}

void SelectionDAGBuilder::visitVectorPredicationIntrinsic(
    const VPIntrinsic &VPIntrin) {
  SDLoc DL = getCurSDLoc();
  unsigned Opcode = getISDForVPIntrinsic(VPIntrin);

  auto IID = VPIntrin.getIntrinsicID();

  if (const auto *CmpI = dyn_cast<VPCmpIntrinsic>(&VPIntrin))
    return visitVPCmp(*CmpI);

  SmallVector<EVT, 4> ValueVTs;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  ComputeValueVTs(TLI, DAG.getDataLayout(), VPIntrin.getType(), ValueVTs);
  SDVTList VTs = DAG.getVTList(ValueVTs);

  auto EVLParamPos = VPIntrinsic::getVectorLengthParamPos(IID);

  MVT EVLParamVT = TLI.getVPExplicitVectorLengthTy();
  assert(EVLParamVT.isScalarInteger() && EVLParamVT.bitsGE(MVT::i32) &&
         "Unexpected target EVL type");

  // Request operands.
  SmallVector<SDValue, 7> OpValues;
  for (unsigned I = 0; I < VPIntrin.arg_size(); ++I) {
    auto Op = getValue(VPIntrin.getArgOperand(I));
    if (I == EVLParamPos)
      Op = DAG.getNode(ISD::ZERO_EXTEND, DL, EVLParamVT, Op);
    OpValues.push_back(Op);
  }

  switch (Opcode) {
  default: {
    SDNodeFlags SDFlags;
    if (auto *FPMO = dyn_cast<FPMathOperator>(&VPIntrin))
      SDFlags.copyFMF(*FPMO);
    SDValue Result = DAG.getNode(Opcode, DL, VTs, OpValues, SDFlags);
    setValue(&VPIntrin, Result);
    break;
  }
  case ISD::VP_LOAD:
    visitVPLoad(VPIntrin, ValueVTs[0], OpValues);
    break;
  case ISD::VP_GATHER:
    visitVPGather(VPIntrin, ValueVTs[0], OpValues);
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
    visitVPStridedLoad(VPIntrin, ValueVTs[0], OpValues);
    break;
  case ISD::VP_STORE:
    visitVPStore(VPIntrin, OpValues);
    break;
  case ISD::VP_SCATTER:
    visitVPScatter(VPIntrin, OpValues);
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    visitVPStridedStore(VPIntrin, OpValues);
    break;
  case ISD::VP_FMULADD: {
    assert(OpValues.size() == 5 && "Unexpected number of operands");
    SDNodeFlags SDFlags;
    if (auto *FPMO = dyn_cast<FPMathOperator>(&VPIntrin))
      SDFlags.copyFMF(*FPMO);
    if (TM.Options.AllowFPOpFusion != FPOpFusion::Strict &&
        TLI.isFMAFasterThanFMulAndFAdd(DAG.getMachineFunction(), ValueVTs[0])) {
      setValue(&VPIntrin, DAG.getNode(ISD::VP_FMA, DL, VTs, OpValues, SDFlags));
    } else {
      SDValue Mul = DAG.getNode(
          ISD::VP_FMUL, DL, VTs,
          {OpValues[0], OpValues[1], OpValues[3], OpValues[4]}, SDFlags);
      SDValue Add =
          DAG.getNode(ISD::VP_FADD, DL, VTs,
                      {Mul, OpValues[2], OpValues[3], OpValues[4]}, SDFlags);
      setValue(&VPIntrin, Add);
    }
    break;
  }
  case ISD::VP_IS_FPCLASS: {
    const DataLayout DLayout = DAG.getDataLayout();
    EVT DestVT = TLI.getValueType(DLayout, VPIntrin.getType());
    auto Constant = OpValues[1]->getAsZExtVal();
    SDValue Check = DAG.getTargetConstant(Constant, DL, MVT::i32);
    SDValue V = DAG.getNode(ISD::VP_IS_FPCLASS, DL, DestVT,
                            {OpValues[0], Check, OpValues[2], OpValues[3]});
    setValue(&VPIntrin, V);
    return;
  }
  case ISD::VP_INTTOPTR: {
    SDValue N = OpValues[0];
    EVT DestVT = TLI.getValueType(DAG.getDataLayout(), VPIntrin.getType());
    EVT PtrMemVT = TLI.getMemValueType(DAG.getDataLayout(), VPIntrin.getType());
    N = DAG.getVPPtrExtOrTrunc(getCurSDLoc(), DestVT, N, OpValues[1],
                               OpValues[2]);
    N = DAG.getVPZExtOrTrunc(getCurSDLoc(), PtrMemVT, N, OpValues[1],
                             OpValues[2]);
    setValue(&VPIntrin, N);
    break;
  }
  case ISD::VP_PTRTOINT: {
    SDValue N = OpValues[0];
    EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                          VPIntrin.getType());
    EVT PtrMemVT = TLI.getMemValueType(DAG.getDataLayout(),
                                       VPIntrin.getOperand(0)->getType());
    N = DAG.getVPPtrExtOrTrunc(getCurSDLoc(), PtrMemVT, N, OpValues[1],
                               OpValues[2]);
    N = DAG.getVPZExtOrTrunc(getCurSDLoc(), DestVT, N, OpValues[1],
                             OpValues[2]);
    setValue(&VPIntrin, N);
    break;
  }
  case ISD::VP_ABS:
  case ISD::VP_CTLZ:
  case ISD::VP_CTLZ_ZERO_UNDEF:
  case ISD::VP_CTTZ:
  case ISD::VP_CTTZ_ZERO_UNDEF:
  case ISD::VP_CTTZ_ELTS_ZERO_UNDEF:
  case ISD::VP_CTTZ_ELTS: {
    SDValue Result =
        DAG.getNode(Opcode, DL, VTs, {OpValues[0], OpValues[2], OpValues[3]});
    setValue(&VPIntrin, Result);
    break;
  }
  }
}

SDValue SelectionDAGBuilder::lowerStartEH(SDValue Chain,
                                          const BasicBlock *EHPadBB,
                                          MCSymbol *&BeginLabel) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineModuleInfo &MMI = MF.getMMI();

  // Insert a label before the invoke call to mark the try range.  This can be
  // used to detect deletion of the invoke via the MachineModuleInfo.
  BeginLabel = MF.getContext().createTempSymbol();

  // For SjLj, keep track of which landing pads go with which invokes
  // so as to maintain the ordering of pads in the LSDA.
  unsigned CallSiteIndex = MMI.getCurrentCallSite();
  if (CallSiteIndex) {
    MF.setCallSiteBeginLabel(BeginLabel, CallSiteIndex);
    LPadToCallSiteMap[FuncInfo.MBBMap[EHPadBB]].push_back(CallSiteIndex);

    // Now that the call site is handled, stop tracking it.
    MMI.setCurrentCallSite(0);
  }

  return DAG.getEHLabel(getCurSDLoc(), Chain, BeginLabel);
}

SDValue SelectionDAGBuilder::lowerEndEH(SDValue Chain, const InvokeInst *II,
                                        const BasicBlock *EHPadBB,
                                        MCSymbol *BeginLabel) {
  assert(BeginLabel && "BeginLabel should've been set");

  MachineFunction &MF = DAG.getMachineFunction();

  // Insert a label at the end of the invoke call to mark the try range.  This
  // can be used to detect deletion of the invoke via the MachineModuleInfo.
  MCSymbol *EndLabel = MF.getContext().createTempSymbol();
  Chain = DAG.getEHLabel(getCurSDLoc(), Chain, EndLabel);

  // Inform MachineModuleInfo of range.
  auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  // There is a platform (e.g. wasm) that uses funclet style IR but does not
  // actually use outlined funclets and their LSDA info style.
  if (MF.hasEHFunclets() && isFuncletEHPersonality(Pers)) {
    assert(II && "II should've been set");
    WinEHFuncInfo *EHInfo = MF.getWinEHFuncInfo();
    EHInfo->addIPToStateRange(II, BeginLabel, EndLabel);
  } else if (!isScopedEHPersonality(Pers)) {
    assert(EHPadBB);
    MF.addInvoke(FuncInfo.MBBMap[EHPadBB], BeginLabel, EndLabel);
  }

  return Chain;
}

std::pair<SDValue, SDValue>
SelectionDAGBuilder::lowerInvokable(TargetLowering::CallLoweringInfo &CLI,
                                    const BasicBlock *EHPadBB) {
  MCSymbol *BeginLabel = nullptr;

  if (EHPadBB) {
    // Both PendingLoads and PendingExports must be flushed here;
    // this call might not return.
    (void)getRoot();
    DAG.setRoot(lowerStartEH(getControlRoot(), EHPadBB, BeginLabel));
    CLI.setChain(getRoot());
  }

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);

  assert((CLI.IsTailCall || Result.second.getNode()) &&
         "Non-null chain expected with non-tail call!");
  assert((Result.second.getNode() || !Result.first.getNode()) &&
         "Null value expected with tail call!");

  if (!Result.second.getNode()) {
    // As a special case, a null chain means that a tail call has been emitted
    // and the DAG root is already updated.
    HasTailCall = true;

    // Since there's no actual continuation from this block, nothing can be
    // relying on us setting vregs for them.
    PendingExports.clear();
  } else {
    DAG.setRoot(Result.second);
  }

  if (EHPadBB) {
    DAG.setRoot(lowerEndEH(getRoot(), cast_or_null<InvokeInst>(CLI.CB), EHPadBB,
                           BeginLabel));
    Result.second = getRoot();
  }

  return Result;
}

void SelectionDAGBuilder::LowerCallTo(const CallBase &CB, SDValue Callee,
                                      bool isTailCall, bool isMustTailCall,
                                      const BasicBlock *EHPadBB,
                                      const TargetLowering::PtrAuthInfo *PAI) {
  auto &DL = DAG.getDataLayout();
  FunctionType *FTy = CB.getFunctionType();
  Type *RetTy = CB.getType();

  TargetLowering::ArgListTy Args;
  Args.reserve(CB.arg_size());

  const Value *SwiftErrorVal = nullptr;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  if (isTailCall) {
    // Avoid emitting tail calls in functions with the disable-tail-calls
    // attribute.
    auto *Caller = CB.getParent()->getParent();
    if (Caller->getFnAttribute("disable-tail-calls").getValueAsString() ==
        "true" && !isMustTailCall)
      isTailCall = false;

    // We can't tail call inside a function with a swifterror argument. Lowering
    // does not support this yet. It would have to move into the swifterror
    // register before the call.
    if (TLI.supportSwiftError() &&
        Caller->getAttributes().hasAttrSomewhere(Attribute::SwiftError))
      isTailCall = false;
  }

  for (auto I = CB.arg_begin(), E = CB.arg_end(); I != E; ++I) {
    TargetLowering::ArgListEntry Entry;
    const Value *V = *I;

    // Skip empty types
    if (V->getType()->isEmptyTy())
      continue;

    SDValue ArgNode = getValue(V);
    Entry.Node = ArgNode; Entry.Ty = V->getType();

    Entry.setAttributes(&CB, I - CB.arg_begin());

    // Use swifterror virtual register as input to the call.
    if (Entry.IsSwiftError && TLI.supportSwiftError()) {
      SwiftErrorVal = V;
      // We find the virtual register for the actual swifterror argument.
      // Instead of using the Value, we use the virtual register instead.
      Entry.Node =
          DAG.getRegister(SwiftError.getOrCreateVRegUseAt(&CB, FuncInfo.MBB, V),
                          EVT(TLI.getPointerTy(DL)));
    }

    Args.push_back(Entry);

    // If we have an explicit sret argument that is an Instruction, (i.e., it
    // might point to function-local memory), we can't meaningfully tail-call.
    if (Entry.IsSRet && isa<Instruction>(V))
      isTailCall = false;
  }

  // If call site has a cfguardtarget operand bundle, create and add an
  // additional ArgListEntry.
  if (auto Bundle = CB.getOperandBundle(LLVMContext::OB_cfguardtarget)) {
    TargetLowering::ArgListEntry Entry;
    Value *V = Bundle->Inputs[0];
    SDValue ArgNode = getValue(V);
    Entry.Node = ArgNode;
    Entry.Ty = V->getType();
    Entry.IsCFGuardTarget = true;
    Args.push_back(Entry);
  }

  // Check if target-independent constraints permit a tail call here.
  // Target-dependent constraints are checked within TLI->LowerCallTo.
  if (isTailCall && !isInTailCallPosition(CB, DAG.getTarget()))
    isTailCall = false;

  // Disable tail calls if there is an swifterror argument. Targets have not
  // been updated to support tail calls.
  if (TLI.supportSwiftError() && SwiftErrorVal)
    isTailCall = false;

  ConstantInt *CFIType = nullptr;
  if (CB.isIndirectCall()) {
    if (auto Bundle = CB.getOperandBundle(LLVMContext::OB_kcfi)) {
      if (!TLI.supportKCFIBundles())
        report_fatal_error(
            "Target doesn't support calls with kcfi operand bundles.");
      CFIType = cast<ConstantInt>(Bundle->Inputs[0]);
      assert(CFIType->getType()->isIntegerTy(32) && "Invalid CFI type");
    }
  }

  SDValue ConvControlToken;
  if (auto Bundle = CB.getOperandBundle(LLVMContext::OB_convergencectrl)) {
    auto *Token = Bundle->Inputs[0].get();
    ConvControlToken = getValue(Token);
  }

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(getCurSDLoc())
      .setChain(getRoot())
      .setCallee(RetTy, FTy, Callee, std::move(Args), CB)
      .setTailCall(isTailCall)
      .setConvergent(CB.isConvergent())
      .setIsPreallocated(
          CB.countOperandBundlesOfType(LLVMContext::OB_preallocated) != 0)
      .setCFIType(CFIType)
      .setConvergenceControlToken(ConvControlToken);

  // Set the pointer authentication info if we have it.
  if (PAI) {
    if (!TLI.supportPtrAuthBundles())
      report_fatal_error(
          "This target doesn't support calls with ptrauth operand bundles.");
    CLI.setPtrAuth(*PAI);
  }

  std::pair<SDValue, SDValue> Result = lowerInvokable(CLI, EHPadBB);

  if (Result.first.getNode()) {
    Result.first = lowerRangeToAssertZExt(DAG, CB, Result.first);
    setValue(&CB, Result.first);
  }

  // The last element of CLI.InVals has the SDValue for swifterror return.
  // Here we copy it to a virtual register and update SwiftErrorMap for
  // book-keeping.
  if (SwiftErrorVal && TLI.supportSwiftError()) {
    // Get the last element of InVals.
    SDValue Src = CLI.InVals.back();
    Register VReg =
        SwiftError.getOrCreateVRegDefAt(&CB, FuncInfo.MBB, SwiftErrorVal);
    SDValue CopyNode = CLI.DAG.getCopyToReg(Result.second, CLI.DL, VReg, Src);
    DAG.setRoot(CopyNode);
  }
}

static SDValue getMemCmpLoad(const Value *PtrVal, MVT LoadVT,
                             SelectionDAGBuilder &Builder) {
  // Check to see if this load can be trivially constant folded, e.g. if the
  // input is from a string literal.
  if (const Constant *LoadInput = dyn_cast<Constant>(PtrVal)) {
    // Cast pointer to the type we really want to load.
    Type *LoadTy =
        Type::getIntNTy(PtrVal->getContext(), LoadVT.getScalarSizeInBits());
    if (LoadVT.isVector())
      LoadTy = FixedVectorType::get(LoadTy, LoadVT.getVectorNumElements());

    LoadInput = ConstantExpr::getBitCast(const_cast<Constant *>(LoadInput),
                                         PointerType::getUnqual(LoadTy));

    if (const Constant *LoadCst =
            ConstantFoldLoadFromConstPtr(const_cast<Constant *>(LoadInput),
                                         LoadTy, Builder.DAG.getDataLayout()))
      return Builder.getValue(LoadCst);
  }

  // Otherwise, we have to emit the load.  If the pointer is to unfoldable but
  // still constant memory, the input chain can be the entry node.
  SDValue Root;
  bool ConstantMemory = false;

  // Do not serialize (non-volatile) loads of constant memory with anything.
  if (Builder.AA && Builder.AA->pointsToConstantMemory(PtrVal)) {
    Root = Builder.DAG.getEntryNode();
    ConstantMemory = true;
  } else {
    // Do not serialize non-volatile loads against each other.
    Root = Builder.DAG.getRoot();
  }

  SDValue Ptr = Builder.getValue(PtrVal);
  SDValue LoadVal =
      Builder.DAG.getLoad(LoadVT, Builder.getCurSDLoc(), Root, Ptr,
                          MachinePointerInfo(PtrVal), Align(1));

  if (!ConstantMemory)
    Builder.PendingLoads.push_back(LoadVal.getValue(1));
  return LoadVal;
}

/// Record the value for an instruction that produces an integer result,
/// converting the type where necessary.
void SelectionDAGBuilder::processIntegerCallValue(const Instruction &I,
                                                  SDValue Value,
                                                  bool IsSigned) {
  EVT VT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                    I.getType(), true);
  Value = DAG.getExtOrTrunc(IsSigned, Value, getCurSDLoc(), VT);
  setValue(&I, Value);
}

/// See if we can lower a memcmp/bcmp call into an optimized form. If so, return
/// true and lower it. Otherwise return false, and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitMemCmpBCmpCall(const CallInst &I) {
  const Value *LHS = I.getArgOperand(0), *RHS = I.getArgOperand(1);
  const Value *Size = I.getArgOperand(2);
  const ConstantSDNode *CSize = dyn_cast<ConstantSDNode>(getValue(Size));
  if (CSize && CSize->getZExtValue() == 0) {
    EVT CallVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                          I.getType(), true);
    setValue(&I, DAG.getConstant(0, getCurSDLoc(), CallVT));
    return true;
  }

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res = TSI.EmitTargetCodeForMemcmp(
      DAG, getCurSDLoc(), DAG.getRoot(), getValue(LHS), getValue(RHS),
      getValue(Size), MachinePointerInfo(LHS), MachinePointerInfo(RHS));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, true);
    PendingLoads.push_back(Res.second);
    return true;
  }

  // memcmp(S1,S2,2) != 0 -> (*(short*)LHS != *(short*)RHS)  != 0
  // memcmp(S1,S2,4) != 0 -> (*(int*)LHS != *(int*)RHS)  != 0
  if (!CSize || !isOnlyUsedInZeroEqualityComparison(&I))
    return false;

  // If the target has a fast compare for the given size, it will return a
  // preferred load type for that size. Require that the load VT is legal and
  // that the target supports unaligned loads of that type. Otherwise, return
  // INVALID.
  auto hasFastLoadsAndCompare = [&](unsigned NumBits) {
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();
    MVT LVT = TLI.hasFastEqualityCompare(NumBits);
    if (LVT != MVT::INVALID_SIMPLE_VALUE_TYPE) {
      // TODO: Handle 5 byte compare as 4-byte + 1 byte.
      // TODO: Handle 8 byte compare on x86-32 as two 32-bit loads.
      // TODO: Check alignment of src and dest ptrs.
      unsigned DstAS = LHS->getType()->getPointerAddressSpace();
      unsigned SrcAS = RHS->getType()->getPointerAddressSpace();
      if (!TLI.isTypeLegal(LVT) ||
          !TLI.allowsMisalignedMemoryAccesses(LVT, SrcAS) ||
          !TLI.allowsMisalignedMemoryAccesses(LVT, DstAS))
        LVT = MVT::INVALID_SIMPLE_VALUE_TYPE;
    }

    return LVT;
  };

  // This turns into unaligned loads. We only do this if the target natively
  // supports the MVT we'll be loading or if it is small enough (<= 4) that
  // we'll only produce a small number of byte loads.
  MVT LoadVT;
  unsigned NumBitsToCompare = CSize->getZExtValue() * 8;
  switch (NumBitsToCompare) {
  default:
    return false;
  case 16:
    LoadVT = MVT::i16;
    break;
  case 32:
    LoadVT = MVT::i32;
    break;
  case 64:
  case 128:
  case 256:
    LoadVT = hasFastLoadsAndCompare(NumBitsToCompare);
    break;
  }

  if (LoadVT == MVT::INVALID_SIMPLE_VALUE_TYPE)
    return false;

  SDValue LoadL = getMemCmpLoad(LHS, LoadVT, *this);
  SDValue LoadR = getMemCmpLoad(RHS, LoadVT, *this);

  // Bitcast to a wide integer type if the loads are vectors.
  if (LoadVT.isVector()) {
    EVT CmpVT = EVT::getIntegerVT(LHS->getContext(), LoadVT.getSizeInBits());
    LoadL = DAG.getBitcast(CmpVT, LoadL);
    LoadR = DAG.getBitcast(CmpVT, LoadR);
  }

  SDValue Cmp = DAG.getSetCC(getCurSDLoc(), MVT::i1, LoadL, LoadR, ISD::SETNE);
  processIntegerCallValue(I, Cmp, false);
  return true;
}

/// See if we can lower a memchr call into an optimized form. If so, return
/// true and lower it. Otherwise return false, and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitMemChrCall(const CallInst &I) {
  const Value *Src = I.getArgOperand(0);
  const Value *Char = I.getArgOperand(1);
  const Value *Length = I.getArgOperand(2);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForMemchr(DAG, getCurSDLoc(), DAG.getRoot(),
                                getValue(Src), getValue(Char), getValue(Length),
                                MachinePointerInfo(Src));
  if (Res.first.getNode()) {
    setValue(&I, Res.first);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a mempcpy call into an optimized form. If so, return
/// true and lower it. Otherwise return false, and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitMemPCpyCall(const CallInst &I) {
  SDValue Dst = getValue(I.getArgOperand(0));
  SDValue Src = getValue(I.getArgOperand(1));
  SDValue Size = getValue(I.getArgOperand(2));

  Align DstAlign = DAG.InferPtrAlign(Dst).valueOrOne();
  Align SrcAlign = DAG.InferPtrAlign(Src).valueOrOne();
  // DAG::getMemcpy needs Alignment to be defined.
  Align Alignment = std::min(DstAlign, SrcAlign);

  SDLoc sdl = getCurSDLoc();

  // In the mempcpy context we need to pass in a false value for isTailCall
  // because the return pointer needs to be adjusted by the size of
  // the copied memory.
  SDValue Root = getMemoryRoot();
  SDValue MC = DAG.getMemcpy(
      Root, sdl, Dst, Src, Size, Alignment, false, false, /*CI=*/nullptr,
      std::nullopt, MachinePointerInfo(I.getArgOperand(0)),
      MachinePointerInfo(I.getArgOperand(1)), I.getAAMetadata());
  assert(MC.getNode() != nullptr &&
         "** memcpy should not be lowered as TailCall in mempcpy context **");
  DAG.setRoot(MC);

  // Check if Size needs to be truncated or extended.
  Size = DAG.getSExtOrTrunc(Size, sdl, Dst.getValueType());

  // Adjust return pointer to point just past the last dst byte.
  SDValue DstPlusSize = DAG.getNode(ISD::ADD, sdl, Dst.getValueType(),
                                    Dst, Size);
  setValue(&I, DstPlusSize);
  return true;
}

/// See if we can lower a strcpy call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrCpyCall(const CallInst &I, bool isStpcpy) {
  const Value *Arg0 = I.getArgOperand(0), *Arg1 = I.getArgOperand(1);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrcpy(DAG, getCurSDLoc(), getRoot(),
                                getValue(Arg0), getValue(Arg1),
                                MachinePointerInfo(Arg0),
                                MachinePointerInfo(Arg1), isStpcpy);
  if (Res.first.getNode()) {
    setValue(&I, Res.first);
    DAG.setRoot(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a strcmp call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrCmpCall(const CallInst &I) {
  const Value *Arg0 = I.getArgOperand(0), *Arg1 = I.getArgOperand(1);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrcmp(DAG, getCurSDLoc(), DAG.getRoot(),
                                getValue(Arg0), getValue(Arg1),
                                MachinePointerInfo(Arg0),
                                MachinePointerInfo(Arg1));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, true);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a strlen call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrLenCall(const CallInst &I) {
  const Value *Arg0 = I.getArgOperand(0);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrlen(DAG, getCurSDLoc(), DAG.getRoot(),
                                getValue(Arg0), MachinePointerInfo(Arg0));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, false);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a strnlen call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrNLenCall(const CallInst &I) {
  const Value *Arg0 = I.getArgOperand(0), *Arg1 = I.getArgOperand(1);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrnlen(DAG, getCurSDLoc(), DAG.getRoot(),
                                 getValue(Arg0), getValue(Arg1),
                                 MachinePointerInfo(Arg0));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, false);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a unary floating-point operation into an SDNode with
/// the specified Opcode.  If so, return true and lower it, otherwise return
/// false and it will be lowered like a normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitUnaryFloatCall(const CallInst &I,
                                              unsigned Opcode) {
  // We already checked this call's prototype; verify it doesn't modify errno.
  if (!I.onlyReadsMemory())
    return false;

  SDNodeFlags Flags;
  Flags.copyFMF(cast<FPMathOperator>(I));

  SDValue Tmp = getValue(I.getArgOperand(0));
  setValue(&I,
           DAG.getNode(Opcode, getCurSDLoc(), Tmp.getValueType(), Tmp, Flags));
  return true;
}

/// See if we can lower a binary floating-point operation into an SDNode with
/// the specified Opcode. If so, return true and lower it. Otherwise return
/// false, and it will be lowered like a normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitBinaryFloatCall(const CallInst &I,
                                               unsigned Opcode) {
  // We already checked this call's prototype; verify it doesn't modify errno.
  if (!I.onlyReadsMemory())
    return false;

  SDNodeFlags Flags;
  Flags.copyFMF(cast<FPMathOperator>(I));

  SDValue Tmp0 = getValue(I.getArgOperand(0));
  SDValue Tmp1 = getValue(I.getArgOperand(1));
  EVT VT = Tmp0.getValueType();
  setValue(&I, DAG.getNode(Opcode, getCurSDLoc(), VT, Tmp0, Tmp1, Flags));
  return true;
}

void SelectionDAGBuilder::visitCall(const CallInst &I) {
  // Handle inline assembly differently.
  if (I.isInlineAsm()) {
    visitInlineAsm(I);
    return;
  }

  diagnoseDontCall(I);

  if (Function *F = I.getCalledFunction()) {
    if (F->isDeclaration()) {
      // Is this an LLVM intrinsic or a target-specific intrinsic?
      unsigned IID = F->getIntrinsicID();
      if (!IID)
        if (const TargetIntrinsicInfo *II = TM.getIntrinsicInfo())
          IID = II->getIntrinsicID(F);

      if (IID) {
        visitIntrinsicCall(I, IID);
        return;
      }
    }

    // Check for well-known libc/libm calls.  If the function is internal, it
    // can't be a library call.  Don't do the check if marked as nobuiltin for
    // some reason or the call site requires strict floating point semantics.
    LibFunc Func;
    if (!I.isNoBuiltin() && !I.isStrictFP() && !F->hasLocalLinkage() &&
        F->hasName() && LibInfo->getLibFunc(*F, Func) &&
        LibInfo->hasOptimizedCodeGen(Func)) {
      switch (Func) {
      default: break;
      case LibFunc_bcmp:
        if (visitMemCmpBCmpCall(I))
          return;
        break;
      case LibFunc_copysign:
      case LibFunc_copysignf:
      case LibFunc_copysignl:
        // We already checked this call's prototype; verify it doesn't modify
        // errno.
        if (I.onlyReadsMemory()) {
          SDValue LHS = getValue(I.getArgOperand(0));
          SDValue RHS = getValue(I.getArgOperand(1));
          setValue(&I, DAG.getNode(ISD::FCOPYSIGN, getCurSDLoc(),
                                   LHS.getValueType(), LHS, RHS));
          return;
        }
        break;
      case LibFunc_fabs:
      case LibFunc_fabsf:
      case LibFunc_fabsl:
        if (visitUnaryFloatCall(I, ISD::FABS))
          return;
        break;
      case LibFunc_fmin:
      case LibFunc_fminf:
      case LibFunc_fminl:
        if (visitBinaryFloatCall(I, ISD::FMINNUM))
          return;
        break;
      case LibFunc_fmax:
      case LibFunc_fmaxf:
      case LibFunc_fmaxl:
        if (visitBinaryFloatCall(I, ISD::FMAXNUM))
          return;
        break;
      case LibFunc_sin:
      case LibFunc_sinf:
      case LibFunc_sinl:
        if (visitUnaryFloatCall(I, ISD::FSIN))
          return;
        break;
      case LibFunc_cos:
      case LibFunc_cosf:
      case LibFunc_cosl:
        if (visitUnaryFloatCall(I, ISD::FCOS))
          return;
        break;
      case LibFunc_tan:
      case LibFunc_tanf:
      case LibFunc_tanl:
        if (visitUnaryFloatCall(I, ISD::FTAN))
          return;
        break;
      case LibFunc_asin:
      case LibFunc_asinf:
      case LibFunc_asinl:
        if (visitUnaryFloatCall(I, ISD::FASIN))
          return;
        break;
      case LibFunc_acos:
      case LibFunc_acosf:
      case LibFunc_acosl:
        if (visitUnaryFloatCall(I, ISD::FACOS))
          return;
        break;
      case LibFunc_atan:
      case LibFunc_atanf:
      case LibFunc_atanl:
        if (visitUnaryFloatCall(I, ISD::FATAN))
          return;
        break;
      case LibFunc_sinh:
      case LibFunc_sinhf:
      case LibFunc_sinhl:
        if (visitUnaryFloatCall(I, ISD::FSINH))
          return;
        break;
      case LibFunc_cosh:
      case LibFunc_coshf:
      case LibFunc_coshl:
        if (visitUnaryFloatCall(I, ISD::FCOSH))
          return;
        break;
      case LibFunc_tanh:
      case LibFunc_tanhf:
      case LibFunc_tanhl:
        if (visitUnaryFloatCall(I, ISD::FTANH))
          return;
        break;
      case LibFunc_sqrt:
      case LibFunc_sqrtf:
      case LibFunc_sqrtl:
      case LibFunc_sqrt_finite:
      case LibFunc_sqrtf_finite:
      case LibFunc_sqrtl_finite:
        if (visitUnaryFloatCall(I, ISD::FSQRT))
          return;
        break;
      case LibFunc_floor:
      case LibFunc_floorf:
      case LibFunc_floorl:
        if (visitUnaryFloatCall(I, ISD::FFLOOR))
          return;
        break;
      case LibFunc_nearbyint:
      case LibFunc_nearbyintf:
      case LibFunc_nearbyintl:
        if (visitUnaryFloatCall(I, ISD::FNEARBYINT))
          return;
        break;
      case LibFunc_ceil:
      case LibFunc_ceilf:
      case LibFunc_ceill:
        if (visitUnaryFloatCall(I, ISD::FCEIL))
          return;
        break;
      case LibFunc_rint:
      case LibFunc_rintf:
      case LibFunc_rintl:
        if (visitUnaryFloatCall(I, ISD::FRINT))
          return;
        break;
      case LibFunc_round:
      case LibFunc_roundf:
      case LibFunc_roundl:
        if (visitUnaryFloatCall(I, ISD::FROUND))
          return;
        break;
      case LibFunc_trunc:
      case LibFunc_truncf:
      case LibFunc_truncl:
        if (visitUnaryFloatCall(I, ISD::FTRUNC))
          return;
        break;
      case LibFunc_log2:
      case LibFunc_log2f:
      case LibFunc_log2l:
        if (visitUnaryFloatCall(I, ISD::FLOG2))
          return;
        break;
      case LibFunc_exp2:
      case LibFunc_exp2f:
      case LibFunc_exp2l:
        if (visitUnaryFloatCall(I, ISD::FEXP2))
          return;
        break;
      case LibFunc_exp10:
      case LibFunc_exp10f:
      case LibFunc_exp10l:
        if (visitUnaryFloatCall(I, ISD::FEXP10))
          return;
        break;
      case LibFunc_ldexp:
      case LibFunc_ldexpf:
      case LibFunc_ldexpl:
        if (visitBinaryFloatCall(I, ISD::FLDEXP))
          return;
        break;
      case LibFunc_memcmp:
        if (visitMemCmpBCmpCall(I))
          return;
        break;
      case LibFunc_mempcpy:
        if (visitMemPCpyCall(I))
          return;
        break;
      case LibFunc_memchr:
        if (visitMemChrCall(I))
          return;
        break;
      case LibFunc_strcpy:
        if (visitStrCpyCall(I, false))
          return;
        break;
      case LibFunc_stpcpy:
        if (visitStrCpyCall(I, true))
          return;
        break;
      case LibFunc_strcmp:
        if (visitStrCmpCall(I))
          return;
        break;
      case LibFunc_strlen:
        if (visitStrLenCall(I))
          return;
        break;
      case LibFunc_strnlen:
        if (visitStrNLenCall(I))
          return;
        break;
      }
    }
  }

  if (I.countOperandBundlesOfType(LLVMContext::OB_ptrauth)) {
    LowerCallSiteWithPtrAuthBundle(cast<CallBase>(I), /*EHPadBB=*/nullptr);
    return;
  }

  // Deopt bundles are lowered in LowerCallSiteWithDeoptBundle, and we don't
  // have to do anything here to lower funclet bundles.
  // CFGuardTarget bundles are lowered in LowerCallTo.
  assert(!I.hasOperandBundlesOtherThan(
             {LLVMContext::OB_deopt, LLVMContext::OB_funclet,
              LLVMContext::OB_cfguardtarget, LLVMContext::OB_preallocated,
              LLVMContext::OB_clang_arc_attachedcall, LLVMContext::OB_kcfi,
              LLVMContext::OB_convergencectrl}) &&
         "Cannot lower calls with arbitrary operand bundles!");

  SDValue Callee = getValue(I.getCalledOperand());

  if (I.hasDeoptState())
    LowerCallSiteWithDeoptBundle(&I, Callee, nullptr);
  else
    // Check if we can potentially perform a tail call. More detailed checking
    // is be done within LowerCallTo, after more information about the call is
    // known.
    LowerCallTo(I, Callee, I.isTailCall(), I.isMustTailCall());
}

void SelectionDAGBuilder::LowerCallSiteWithPtrAuthBundle(
    const CallBase &CB, const BasicBlock *EHPadBB) {
  auto PAB = CB.getOperandBundle("ptrauth");
  const Value *CalleeV = CB.getCalledOperand();

  // Gather the call ptrauth data from the operand bundle:
  //   [ i32 <key>, i64 <discriminator> ]
  const auto *Key = cast<ConstantInt>(PAB->Inputs[0]);
  const Value *Discriminator = PAB->Inputs[1];

  assert(Key->getType()->isIntegerTy(32) && "Invalid ptrauth key");
  assert(Discriminator->getType()->isIntegerTy(64) &&
         "Invalid ptrauth discriminator");

  // Look through ptrauth constants to find the raw callee.
  // Do a direct unauthenticated call if we found it and everything matches.
  if (const auto *CalleeCPA = dyn_cast<ConstantPtrAuth>(CalleeV))
    if (CalleeCPA->isKnownCompatibleWith(Key, Discriminator,
                                         DAG.getDataLayout()))
      return LowerCallTo(CB, getValue(CalleeCPA->getPointer()), CB.isTailCall(),
                         CB.isMustTailCall(), EHPadBB);

  // Functions should never be ptrauth-called directly.
  assert(!isa<Function>(CalleeV) && "invalid direct ptrauth call");

  // Otherwise, do an authenticated indirect call.
  TargetLowering::PtrAuthInfo PAI = {Key->getZExtValue(),
                                     getValue(Discriminator)};

  LowerCallTo(CB, getValue(CalleeV), CB.isTailCall(), CB.isMustTailCall(),
              EHPadBB, &PAI);
}

namespace {

/// AsmOperandInfo - This contains information for each constraint that we are
/// lowering.
class SDISelAsmOperandInfo : public TargetLowering::AsmOperandInfo {
public:
  /// CallOperand - If this is the result output operand or a clobber
  /// this is null, otherwise it is the incoming operand to the CallInst.
  /// This gets modified as the asm is processed.
  SDValue CallOperand;

  /// AssignedRegs - If this is a register or register class operand, this
  /// contains the set of register corresponding to the operand.
  RegsForValue AssignedRegs;

  explicit SDISelAsmOperandInfo(const TargetLowering::AsmOperandInfo &info)
    : TargetLowering::AsmOperandInfo(info), CallOperand(nullptr, 0) {
  }

  /// Whether or not this operand accesses memory
  bool hasMemory(const TargetLowering &TLI) const {
    // Indirect operand accesses access memory.
    if (isIndirect)
      return true;

    for (const auto &Code : Codes)
      if (TLI.getConstraintType(Code) == TargetLowering::C_Memory)
        return true;

    return false;
  }
};


} // end anonymous namespace

/// Make sure that the output operand \p OpInfo and its corresponding input
/// operand \p MatchingOpInfo have compatible constraint types (otherwise error
/// out).
static void patchMatchingInput(const SDISelAsmOperandInfo &OpInfo,
                               SDISelAsmOperandInfo &MatchingOpInfo,
                               SelectionDAG &DAG) {
  if (OpInfo.ConstraintVT == MatchingOpInfo.ConstraintVT)
    return;

  const TargetRegisterInfo *TRI = DAG.getSubtarget().getRegisterInfo();
  const auto &TLI = DAG.getTargetLoweringInfo();

  std::pair<unsigned, const TargetRegisterClass *> MatchRC =
      TLI.getRegForInlineAsmConstraint(TRI, OpInfo.ConstraintCode,
                                       OpInfo.ConstraintVT);
  std::pair<unsigned, const TargetRegisterClass *> InputRC =
      TLI.getRegForInlineAsmConstraint(TRI, MatchingOpInfo.ConstraintCode,
                                       MatchingOpInfo.ConstraintVT);
  if ((OpInfo.ConstraintVT.isInteger() !=
       MatchingOpInfo.ConstraintVT.isInteger()) ||
      (MatchRC.second != InputRC.second)) {
    // FIXME: error out in a more elegant fashion
    report_fatal_error("Unsupported asm: input constraint"
                       " with a matching output constraint of"
                       " incompatible type!");
  }
  MatchingOpInfo.ConstraintVT = OpInfo.ConstraintVT;
}

/// Get a direct memory input to behave well as an indirect operand.
/// This may introduce stores, hence the need for a \p Chain.
/// \return The (possibly updated) chain.
static SDValue getAddressForMemoryInput(SDValue Chain, const SDLoc &Location,
                                        SDISelAsmOperandInfo &OpInfo,
                                        SelectionDAG &DAG) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // If we don't have an indirect input, put it in the constpool if we can,
  // otherwise spill it to a stack slot.
  // TODO: This isn't quite right. We need to handle these according to
  // the addressing mode that the constraint wants. Also, this may take
  // an additional register for the computation and we don't want that
  // either.

  // If the operand is a float, integer, or vector constant, spill to a
  // constant pool entry to get its address.
  const Value *OpVal = OpInfo.CallOperandVal;
  if (isa<ConstantFP>(OpVal) || isa<ConstantInt>(OpVal) ||
      isa<ConstantVector>(OpVal) || isa<ConstantDataVector>(OpVal)) {
    OpInfo.CallOperand = DAG.getConstantPool(
        cast<Constant>(OpVal), TLI.getPointerTy(DAG.getDataLayout()));
    return Chain;
  }

  // Otherwise, create a stack slot and emit a store to it before the asm.
  Type *Ty = OpVal->getType();
  auto &DL = DAG.getDataLayout();
  TypeSize TySize = DL.getTypeAllocSize(Ty);
  MachineFunction &MF = DAG.getMachineFunction();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  int StackID = 0;
  if (TySize.isScalable())
    StackID = TFI->getStackIDForScalableVectors();
  int SSFI = MF.getFrameInfo().CreateStackObject(TySize.getKnownMinValue(),
                                                 DL.getPrefTypeAlign(Ty), false,
                                                 nullptr, StackID);
  SDValue StackSlot = DAG.getFrameIndex(SSFI, TLI.getFrameIndexTy(DL));
  Chain = DAG.getTruncStore(Chain, Location, OpInfo.CallOperand, StackSlot,
                            MachinePointerInfo::getFixedStack(MF, SSFI),
                            TLI.getMemValueType(DL, Ty));
  OpInfo.CallOperand = StackSlot;

  return Chain;
}

/// GetRegistersForValue - Assign registers (virtual or physical) for the
/// specified operand.  We prefer to assign virtual registers, to allow the
/// register allocator to handle the assignment process.  However, if the asm
/// uses features that we can't model on machineinstrs, we have SDISel do the
/// allocation.  This produces generally horrible, but correct, code.
///
///   OpInfo describes the operand
///   RefOpInfo describes the matching operand if any, the operand otherwise
static std::optional<unsigned>
getRegistersForValue(SelectionDAG &DAG, const SDLoc &DL,
                     SDISelAsmOperandInfo &OpInfo,
                     SDISelAsmOperandInfo &RefOpInfo) {
  LLVMContext &Context = *DAG.getContext();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<unsigned, 4> Regs;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // No work to do for memory/address operands.
  if (OpInfo.ConstraintType == TargetLowering::C_Memory ||
      OpInfo.ConstraintType == TargetLowering::C_Address)
    return std::nullopt;

  // If this is a constraint for a single physreg, or a constraint for a
  // register class, find it.
  unsigned AssignedReg;
  const TargetRegisterClass *RC;
  std::tie(AssignedReg, RC) = TLI.getRegForInlineAsmConstraint(
      &TRI, RefOpInfo.ConstraintCode, RefOpInfo.ConstraintVT);
  // RC is unset only on failure. Return immediately.
  if (!RC)
    return std::nullopt;

  // Get the actual register value type.  This is important, because the user
  // may have asked for (e.g.) the AX register in i32 type.  We need to
  // remember that AX is actually i16 to get the right extension.
  const MVT RegVT = *TRI.legalclasstypes_begin(*RC);

  if (OpInfo.ConstraintVT != MVT::Other && RegVT != MVT::Untyped) {
    // If this is an FP operand in an integer register (or visa versa), or more
    // generally if the operand value disagrees with the register class we plan
    // to stick it in, fix the operand type.
    //
    // If this is an input value, the bitcast to the new type is done now.
    // Bitcast for output value is done at the end of visitInlineAsm().
    if ((OpInfo.Type == InlineAsm::isOutput ||
         OpInfo.Type == InlineAsm::isInput) &&
        !TRI.isTypeLegalForClass(*RC, OpInfo.ConstraintVT)) {
      // Try to convert to the first EVT that the reg class contains.  If the
      // types are identical size, use a bitcast to convert (e.g. two differing
      // vector types).  Note: output bitcast is done at the end of
      // visitInlineAsm().
      if (RegVT.getSizeInBits() == OpInfo.ConstraintVT.getSizeInBits()) {
        // Exclude indirect inputs while they are unsupported because the code
        // to perform the load is missing and thus OpInfo.CallOperand still
        // refers to the input address rather than the pointed-to value.
        if (OpInfo.Type == InlineAsm::isInput && !OpInfo.isIndirect)
          OpInfo.CallOperand =
              DAG.getNode(ISD::BITCAST, DL, RegVT, OpInfo.CallOperand);
        OpInfo.ConstraintVT = RegVT;
        // If the operand is an FP value and we want it in integer registers,
        // use the corresponding integer type. This turns an f64 value into
        // i64, which can be passed with two i32 values on a 32-bit machine.
      } else if (RegVT.isInteger() && OpInfo.ConstraintVT.isFloatingPoint()) {
        MVT VT = MVT::getIntegerVT(OpInfo.ConstraintVT.getSizeInBits());
        if (OpInfo.Type == InlineAsm::isInput)
          OpInfo.CallOperand =
              DAG.getNode(ISD::BITCAST, DL, VT, OpInfo.CallOperand);
        OpInfo.ConstraintVT = VT;
      }
    }
  }

  // No need to allocate a matching input constraint since the constraint it's
  // matching to has already been allocated.
  if (OpInfo.isMatchingInputConstraint())
    return std::nullopt;

  EVT ValueVT = OpInfo.ConstraintVT;
  if (OpInfo.ConstraintVT == MVT::Other)
    ValueVT = RegVT;

  // Initialize NumRegs.
  unsigned NumRegs = 1;
  if (OpInfo.ConstraintVT != MVT::Other)
    NumRegs = TLI.getNumRegisters(Context, OpInfo.ConstraintVT, RegVT);

  // If this is a constraint for a specific physical register, like {r17},
  // assign it now.

  // If this associated to a specific register, initialize iterator to correct
  // place. If virtual, make sure we have enough registers

  // Initialize iterator if necessary
  TargetRegisterClass::iterator I = RC->begin();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  // Do not check for single registers.
  if (AssignedReg) {
    I = std::find(I, RC->end(), AssignedReg);
    if (I == RC->end()) {
      // RC does not contain the selected register, which indicates a
      // mismatch between the register and the required type/bitwidth.
      return {AssignedReg};
    }
  }

  for (; NumRegs; --NumRegs, ++I) {
    assert(I != RC->end() && "Ran out of registers to allocate!");
    Register R = AssignedReg ? Register(*I) : RegInfo.createVirtualRegister(RC);
    Regs.push_back(R);
  }

  OpInfo.AssignedRegs = RegsForValue(Regs, RegVT, ValueVT);
  return std::nullopt;
}

static unsigned
findMatchingInlineAsmOperand(unsigned OperandNo,
                             const std::vector<SDValue> &AsmNodeOperands) {
  // Scan until we find the definition we already emitted of this operand.
  unsigned CurOp = InlineAsm::Op_FirstOperand;
  for (; OperandNo; --OperandNo) {
    // Advance to the next operand.
    unsigned OpFlag = AsmNodeOperands[CurOp]->getAsZExtVal();
    const InlineAsm::Flag F(OpFlag);
    assert(
        (F.isRegDefKind() || F.isRegDefEarlyClobberKind() || F.isMemKind()) &&
        "Skipped past definitions?");
    CurOp += F.getNumOperandRegisters() + 1;
  }
  return CurOp;
}

namespace {

class ExtraFlags {
  unsigned Flags = 0;

public:
  explicit ExtraFlags(const CallBase &Call) {
    const InlineAsm *IA = cast<InlineAsm>(Call.getCalledOperand());
    if (IA->hasSideEffects())
      Flags |= InlineAsm::Extra_HasSideEffects;
    if (IA->isAlignStack())
      Flags |= InlineAsm::Extra_IsAlignStack;
    if (Call.isConvergent())
      Flags |= InlineAsm::Extra_IsConvergent;
    Flags |= IA->getDialect() * InlineAsm::Extra_AsmDialect;
  }

  void update(const TargetLowering::AsmOperandInfo &OpInfo) {
    // Ideally, we would only check against memory constraints.  However, the
    // meaning of an Other constraint can be target-specific and we can't easily
    // reason about it.  Therefore, be conservative and set MayLoad/MayStore
    // for Other constraints as well.
    if (OpInfo.ConstraintType == TargetLowering::C_Memory ||
        OpInfo.ConstraintType == TargetLowering::C_Other) {
      if (OpInfo.Type == InlineAsm::isInput)
        Flags |= InlineAsm::Extra_MayLoad;
      else if (OpInfo.Type == InlineAsm::isOutput)
        Flags |= InlineAsm::Extra_MayStore;
      else if (OpInfo.Type == InlineAsm::isClobber)
        Flags |= (InlineAsm::Extra_MayLoad | InlineAsm::Extra_MayStore);
    }
  }

  unsigned get() const { return Flags; }
};

} // end anonymous namespace

static bool isFunction(SDValue Op) {
  if (Op && Op.getOpcode() == ISD::GlobalAddress) {
    if (auto *GA = dyn_cast<GlobalAddressSDNode>(Op)) {
      auto Fn = dyn_cast_or_null<Function>(GA->getGlobal());

      // In normal "call dllimport func" instruction (non-inlineasm) it force
      // indirect access by specifing call opcode. And usually specially print
      // asm with indirect symbol (i.g: "*") according to opcode. Inline asm can
      // not do in this way now. (In fact, this is similar with "Data Access"
      // action). So here we ignore dllimport function.
      if (Fn && !Fn->hasDLLImportStorageClass())
        return true;
    }
  }
  return false;
}

/// visitInlineAsm - Handle a call to an InlineAsm object.
void SelectionDAGBuilder::visitInlineAsm(const CallBase &Call,
                                         const BasicBlock *EHPadBB) {
  const InlineAsm *IA = cast<InlineAsm>(Call.getCalledOperand());

  /// ConstraintOperands - Information about all of the constraints.
  SmallVector<SDISelAsmOperandInfo, 16> ConstraintOperands;

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  TargetLowering::AsmOperandInfoVector TargetConstraints = TLI.ParseConstraints(
      DAG.getDataLayout(), DAG.getSubtarget().getRegisterInfo(), Call);

  // First Pass: Calculate HasSideEffects and ExtraFlags (AlignStack,
  // AsmDialect, MayLoad, MayStore).
  bool HasSideEffect = IA->hasSideEffects();
  ExtraFlags ExtraInfo(Call);

  for (auto &T : TargetConstraints) {
    ConstraintOperands.push_back(SDISelAsmOperandInfo(T));
    SDISelAsmOperandInfo &OpInfo = ConstraintOperands.back();

    if (OpInfo.CallOperandVal)
      OpInfo.CallOperand = getValue(OpInfo.CallOperandVal);

    if (!HasSideEffect)
      HasSideEffect = OpInfo.hasMemory(TLI);

    // Determine if this InlineAsm MayLoad or MayStore based on the constraints.
    // FIXME: Could we compute this on OpInfo rather than T?

    // Compute the constraint code and ConstraintType to use.
    TLI.ComputeConstraintToUse(T, SDValue());

    if (T.ConstraintType == TargetLowering::C_Immediate &&
        OpInfo.CallOperand && !isa<ConstantSDNode>(OpInfo.CallOperand))
      // We've delayed emitting a diagnostic like the "n" constraint because
      // inlining could cause an integer showing up.
      return emitInlineAsmError(Call, "constraint '" + Twine(T.ConstraintCode) +
                                          "' expects an integer constant "
                                          "expression");

    ExtraInfo.update(T);
  }

  // We won't need to flush pending loads if this asm doesn't touch
  // memory and is nonvolatile.
  SDValue Glue, Chain = (HasSideEffect) ? getRoot() : DAG.getRoot();

  bool EmitEHLabels = isa<InvokeInst>(Call);
  if (EmitEHLabels) {
    assert(EHPadBB && "InvokeInst must have an EHPadBB");
  }
  bool IsCallBr = isa<CallBrInst>(Call);

  if (IsCallBr || EmitEHLabels) {
    // If this is a callbr or invoke we need to flush pending exports since
    // inlineasm_br and invoke are terminators.
    // We need to do this before nodes are glued to the inlineasm_br node.
    Chain = getControlRoot();
  }

  MCSymbol *BeginLabel = nullptr;
  if (EmitEHLabels) {
    Chain = lowerStartEH(Chain, EHPadBB, BeginLabel);
  }

  int OpNo = -1;
  SmallVector<StringRef> AsmStrs;
  IA->collectAsmStrs(AsmStrs);

  // Second pass over the constraints: compute which constraint option to use.
  for (SDISelAsmOperandInfo &OpInfo : ConstraintOperands) {
    if (OpInfo.hasArg() || OpInfo.Type == InlineAsm::isOutput)
      OpNo++;

    // If this is an output operand with a matching input operand, look up the
    // matching input. If their types mismatch, e.g. one is an integer, the
    // other is floating point, or their sizes are different, flag it as an
    // error.
    if (OpInfo.hasMatchingInput()) {
      SDISelAsmOperandInfo &Input = ConstraintOperands[OpInfo.MatchingInput];
      patchMatchingInput(OpInfo, Input, DAG);
    }

    // Compute the constraint code and ConstraintType to use.
    TLI.ComputeConstraintToUse(OpInfo, OpInfo.CallOperand, &DAG);

    if ((OpInfo.ConstraintType == TargetLowering::C_Memory &&
         OpInfo.Type == InlineAsm::isClobber) ||
        OpInfo.ConstraintType == TargetLowering::C_Address)
      continue;

    // In Linux PIC model, there are 4 cases about value/label addressing:
    //
    // 1: Function call or Label jmp inside the module.
    // 2: Data access (such as global variable, static variable) inside module.
    // 3: Function call or Label jmp outside the module.
    // 4: Data access (such as global variable) outside the module.
    //
    // Due to current llvm inline asm architecture designed to not "recognize"
    // the asm code, there are quite troubles for us to treat mem addressing
    // differently for same value/adress used in different instuctions.
    // For example, in pic model, call a func may in plt way or direclty
    // pc-related, but lea/mov a function adress may use got.
    //
    // Here we try to "recognize" function call for the case 1 and case 3 in
    // inline asm. And try to adjust the constraint for them.
    //
    // TODO: Due to current inline asm didn't encourage to jmp to the outsider
    // label, so here we don't handle jmp function label now, but we need to
    // enhance it (especilly in PIC model) if we meet meaningful requirements.
    if (OpInfo.isIndirect && isFunction(OpInfo.CallOperand) &&
        TLI.isInlineAsmTargetBranch(AsmStrs, OpNo) &&
        TM.getCodeModel() != CodeModel::Large) {
      OpInfo.isIndirect = false;
      OpInfo.ConstraintType = TargetLowering::C_Address;
    }

    // If this is a memory input, and if the operand is not indirect, do what we
    // need to provide an address for the memory input.
    if (OpInfo.ConstraintType == TargetLowering::C_Memory &&
        !OpInfo.isIndirect) {
      assert((OpInfo.isMultipleAlternative ||
              (OpInfo.Type == InlineAsm::isInput)) &&
             "Can only indirectify direct input operands!");

      // Memory operands really want the address of the value.
      Chain = getAddressForMemoryInput(Chain, getCurSDLoc(), OpInfo, DAG);

      // There is no longer a Value* corresponding to this operand.
      OpInfo.CallOperandVal = nullptr;

      // It is now an indirect operand.
      OpInfo.isIndirect = true;
    }

  }

  // AsmNodeOperands - The operands for the ISD::INLINEASM node.
  std::vector<SDValue> AsmNodeOperands;
  AsmNodeOperands.push_back(SDValue());  // reserve space for input chain
  AsmNodeOperands.push_back(DAG.getTargetExternalSymbol(
      IA->getAsmString().c_str(), TLI.getProgramPointerTy(DAG.getDataLayout())));

  // If we have a !srcloc metadata node associated with it, we want to attach
  // this to the ultimately generated inline asm machineinstr.  To do this, we
  // pass in the third operand as this (potentially null) inline asm MDNode.
  const MDNode *SrcLoc = Call.getMetadata("srcloc");
  AsmNodeOperands.push_back(DAG.getMDNode(SrcLoc));

  // Remember the HasSideEffect, AlignStack, AsmDialect, MayLoad and MayStore
  // bits as operand 3.
  AsmNodeOperands.push_back(DAG.getTargetConstant(
      ExtraInfo.get(), getCurSDLoc(), TLI.getPointerTy(DAG.getDataLayout())));

  // Third pass: Loop over operands to prepare DAG-level operands.. As part of
  // this, assign virtual and physical registers for inputs and otput.
  for (SDISelAsmOperandInfo &OpInfo : ConstraintOperands) {
    // Assign Registers.
    SDISelAsmOperandInfo &RefOpInfo =
        OpInfo.isMatchingInputConstraint()
            ? ConstraintOperands[OpInfo.getMatchedOperand()]
            : OpInfo;
    const auto RegError =
        getRegistersForValue(DAG, getCurSDLoc(), OpInfo, RefOpInfo);
    if (RegError) {
      const MachineFunction &MF = DAG.getMachineFunction();
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const char *RegName = TRI.getName(*RegError);
      emitInlineAsmError(Call, "register '" + Twine(RegName) +
                                   "' allocated for constraint '" +
                                   Twine(OpInfo.ConstraintCode) +
                                   "' does not match required type");
      return;
    }

    auto DetectWriteToReservedRegister = [&]() {
      const MachineFunction &MF = DAG.getMachineFunction();
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      for (unsigned Reg : OpInfo.AssignedRegs.Regs) {
        if (Register::isPhysicalRegister(Reg) &&
            TRI.isInlineAsmReadOnlyReg(MF, Reg)) {
          const char *RegName = TRI.getName(Reg);
          emitInlineAsmError(Call, "write to reserved register '" +
                                       Twine(RegName) + "'");
          return true;
        }
      }
      return false;
    };
    assert((OpInfo.ConstraintType != TargetLowering::C_Address ||
            (OpInfo.Type == InlineAsm::isInput &&
             !OpInfo.isMatchingInputConstraint())) &&
           "Only address as input operand is allowed.");

    switch (OpInfo.Type) {
    case InlineAsm::isOutput:
      if (OpInfo.ConstraintType == TargetLowering::C_Memory) {
        const InlineAsm::ConstraintCode ConstraintID =
            TLI.getInlineAsmMemConstraint(OpInfo.ConstraintCode);
        assert(ConstraintID != InlineAsm::ConstraintCode::Unknown &&
               "Failed to convert memory constraint code to constraint id.");

        // Add information to the INLINEASM node to know about this output.
        InlineAsm::Flag OpFlags(InlineAsm::Kind::Mem, 1);
        OpFlags.setMemConstraint(ConstraintID);
        AsmNodeOperands.push_back(DAG.getTargetConstant(OpFlags, getCurSDLoc(),
                                                        MVT::i32));
        AsmNodeOperands.push_back(OpInfo.CallOperand);
      } else {
        // Otherwise, this outputs to a register (directly for C_Register /
        // C_RegisterClass, and a target-defined fashion for
        // C_Immediate/C_Other). Find a register that we can use.
        if (OpInfo.AssignedRegs.Regs.empty()) {
          emitInlineAsmError(
              Call, "couldn't allocate output register for constraint '" +
                        Twine(OpInfo.ConstraintCode) + "'");
          return;
        }

        if (DetectWriteToReservedRegister())
          return;

        // Add information to the INLINEASM node to know that this register is
        // set.
        OpInfo.AssignedRegs.AddInlineAsmOperands(
            OpInfo.isEarlyClobber ? InlineAsm::Kind::RegDefEarlyClobber
                                  : InlineAsm::Kind::RegDef,
            false, 0, getCurSDLoc(), DAG, AsmNodeOperands);
      }
      break;

    case InlineAsm::isInput:
    case InlineAsm::isLabel: {
      SDValue InOperandVal = OpInfo.CallOperand;

      if (OpInfo.isMatchingInputConstraint()) {
        // If this is required to match an output register we have already set,
        // just use its register.
        auto CurOp = findMatchingInlineAsmOperand(OpInfo.getMatchedOperand(),
                                                  AsmNodeOperands);
        InlineAsm::Flag Flag(AsmNodeOperands[CurOp]->getAsZExtVal());
        if (Flag.isRegDefKind() || Flag.isRegDefEarlyClobberKind()) {
          if (OpInfo.isIndirect) {
            // This happens on gcc/testsuite/gcc.dg/pr8788-1.c
            emitInlineAsmError(Call, "inline asm not supported yet: "
                                     "don't know how to handle tied "
                                     "indirect register inputs");
            return;
          }

          SmallVector<unsigned, 4> Regs;
          MachineFunction &MF = DAG.getMachineFunction();
          MachineRegisterInfo &MRI = MF.getRegInfo();
          const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
          auto *R = cast<RegisterSDNode>(AsmNodeOperands[CurOp+1]);
          Register TiedReg = R->getReg();
          MVT RegVT = R->getSimpleValueType(0);
          const TargetRegisterClass *RC =
              TiedReg.isVirtual()     ? MRI.getRegClass(TiedReg)
              : RegVT != MVT::Untyped ? TLI.getRegClassFor(RegVT)
                                      : TRI.getMinimalPhysRegClass(TiedReg);
          for (unsigned i = 0, e = Flag.getNumOperandRegisters(); i != e; ++i)
            Regs.push_back(MRI.createVirtualRegister(RC));

          RegsForValue MatchedRegs(Regs, RegVT, InOperandVal.getValueType());

          SDLoc dl = getCurSDLoc();
          // Use the produced MatchedRegs object to
          MatchedRegs.getCopyToRegs(InOperandVal, DAG, dl, Chain, &Glue, &Call);
          MatchedRegs.AddInlineAsmOperands(InlineAsm::Kind::RegUse, true,
                                           OpInfo.getMatchedOperand(), dl, DAG,
                                           AsmNodeOperands);
          break;
        }

        assert(Flag.isMemKind() && "Unknown matching constraint!");
        assert(Flag.getNumOperandRegisters() == 1 &&
               "Unexpected number of operands");
        // Add information to the INLINEASM node to know about this input.
        // See InlineAsm.h isUseOperandTiedToDef.
        Flag.clearMemConstraint();
        Flag.setMatchingOp(OpInfo.getMatchedOperand());
        AsmNodeOperands.push_back(DAG.getTargetConstant(
            Flag, getCurSDLoc(), TLI.getPointerTy(DAG.getDataLayout())));
        AsmNodeOperands.push_back(AsmNodeOperands[CurOp+1]);
        break;
      }

      // Treat indirect 'X' constraint as memory.
      if (OpInfo.ConstraintType == TargetLowering::C_Other &&
          OpInfo.isIndirect)
        OpInfo.ConstraintType = TargetLowering::C_Memory;

      if (OpInfo.ConstraintType == TargetLowering::C_Immediate ||
          OpInfo.ConstraintType == TargetLowering::C_Other) {
        std::vector<SDValue> Ops;
        TLI.LowerAsmOperandForConstraint(InOperandVal, OpInfo.ConstraintCode,
                                          Ops, DAG);
        if (Ops.empty()) {
          if (OpInfo.ConstraintType == TargetLowering::C_Immediate)
            if (isa<ConstantSDNode>(InOperandVal)) {
              emitInlineAsmError(Call, "value out of range for constraint '" +
                                           Twine(OpInfo.ConstraintCode) + "'");
              return;
            }

          emitInlineAsmError(Call,
                             "invalid operand for inline asm constraint '" +
                                 Twine(OpInfo.ConstraintCode) + "'");
          return;
        }

        // Add information to the INLINEASM node to know about this input.
        InlineAsm::Flag ResOpType(InlineAsm::Kind::Imm, Ops.size());
        AsmNodeOperands.push_back(DAG.getTargetConstant(
            ResOpType, getCurSDLoc(), TLI.getPointerTy(DAG.getDataLayout())));
        llvm::append_range(AsmNodeOperands, Ops);
        break;
      }

      if (OpInfo.ConstraintType == TargetLowering::C_Memory) {
        assert((OpInfo.isIndirect ||
                OpInfo.ConstraintType != TargetLowering::C_Memory) &&
               "Operand must be indirect to be a mem!");
        assert(InOperandVal.getValueType() ==
                   TLI.getPointerTy(DAG.getDataLayout()) &&
               "Memory operands expect pointer values");

        const InlineAsm::ConstraintCode ConstraintID =
            TLI.getInlineAsmMemConstraint(OpInfo.ConstraintCode);
        assert(ConstraintID != InlineAsm::ConstraintCode::Unknown &&
               "Failed to convert memory constraint code to constraint id.");

        // Add information to the INLINEASM node to know about this input.
        InlineAsm::Flag ResOpType(InlineAsm::Kind::Mem, 1);
        ResOpType.setMemConstraint(ConstraintID);
        AsmNodeOperands.push_back(DAG.getTargetConstant(ResOpType,
                                                        getCurSDLoc(),
                                                        MVT::i32));
        AsmNodeOperands.push_back(InOperandVal);
        break;
      }

      if (OpInfo.ConstraintType == TargetLowering::C_Address) {
        const InlineAsm::ConstraintCode ConstraintID =
            TLI.getInlineAsmMemConstraint(OpInfo.ConstraintCode);
        assert(ConstraintID != InlineAsm::ConstraintCode::Unknown &&
               "Failed to convert memory constraint code to constraint id.");

        InlineAsm::Flag ResOpType(InlineAsm::Kind::Mem, 1);

        SDValue AsmOp = InOperandVal;
        if (isFunction(InOperandVal)) {
          auto *GA = cast<GlobalAddressSDNode>(InOperandVal);
          ResOpType = InlineAsm::Flag(InlineAsm::Kind::Func, 1);
          AsmOp = DAG.getTargetGlobalAddress(GA->getGlobal(), getCurSDLoc(),
                                             InOperandVal.getValueType(),
                                             GA->getOffset());
        }

        // Add information to the INLINEASM node to know about this input.
        ResOpType.setMemConstraint(ConstraintID);

        AsmNodeOperands.push_back(
            DAG.getTargetConstant(ResOpType, getCurSDLoc(), MVT::i32));

        AsmNodeOperands.push_back(AsmOp);
        break;
      }

      if (OpInfo.ConstraintType != TargetLowering::C_RegisterClass &&
          OpInfo.ConstraintType != TargetLowering::C_Register) {
        emitInlineAsmError(Call, "unknown asm constraint '" +
                                     Twine(OpInfo.ConstraintCode) + "'");
        return;
      }

      // TODO: Support this.
      if (OpInfo.isIndirect) {
        emitInlineAsmError(
            Call, "Don't know how to handle indirect register inputs yet "
                  "for constraint '" +
                      Twine(OpInfo.ConstraintCode) + "'");
        return;
      }

      // Copy the input into the appropriate registers.
      if (OpInfo.AssignedRegs.Regs.empty()) {
        emitInlineAsmError(Call,
                           "couldn't allocate input reg for constraint '" +
                               Twine(OpInfo.ConstraintCode) + "'");
        return;
      }

      if (DetectWriteToReservedRegister())
        return;

      SDLoc dl = getCurSDLoc();

      OpInfo.AssignedRegs.getCopyToRegs(InOperandVal, DAG, dl, Chain, &Glue,
                                        &Call);

      OpInfo.AssignedRegs.AddInlineAsmOperands(InlineAsm::Kind::RegUse, false,
                                               0, dl, DAG, AsmNodeOperands);
      break;
    }
    case InlineAsm::isClobber:
      // Add the clobbered value to the operand list, so that the register
      // allocator is aware that the physreg got clobbered.
      if (!OpInfo.AssignedRegs.Regs.empty())
        OpInfo.AssignedRegs.AddInlineAsmOperands(InlineAsm::Kind::Clobber,
                                                 false, 0, getCurSDLoc(), DAG,
                                                 AsmNodeOperands);
      break;
    }
  }

  // Finish up input operands.  Set the input chain and add the flag last.
  AsmNodeOperands[InlineAsm::Op_InputChain] = Chain;
  if (Glue.getNode()) AsmNodeOperands.push_back(Glue);

  unsigned ISDOpc = IsCallBr ? ISD::INLINEASM_BR : ISD::INLINEASM;
  Chain = DAG.getNode(ISDOpc, getCurSDLoc(),
                      DAG.getVTList(MVT::Other, MVT::Glue), AsmNodeOperands);
  Glue = Chain.getValue(1);

  // Do additional work to generate outputs.

  SmallVector<EVT, 1> ResultVTs;
  SmallVector<SDValue, 1> ResultValues;
  SmallVector<SDValue, 8> OutChains;

  llvm::Type *CallResultType = Call.getType();
  ArrayRef<Type *> ResultTypes;
  if (StructType *StructResult = dyn_cast<StructType>(CallResultType))
    ResultTypes = StructResult->elements();
  else if (!CallResultType->isVoidTy())
    ResultTypes = ArrayRef(CallResultType);

  auto CurResultType = ResultTypes.begin();
  auto handleRegAssign = [&](SDValue V) {
    assert(CurResultType != ResultTypes.end() && "Unexpected value");
    assert((*CurResultType)->isSized() && "Unexpected unsized type");
    EVT ResultVT = TLI.getValueType(DAG.getDataLayout(), *CurResultType);
    ++CurResultType;
    // If the type of the inline asm call site return value is different but has
    // same size as the type of the asm output bitcast it.  One example of this
    // is for vectors with different width / number of elements.  This can
    // happen for register classes that can contain multiple different value
    // types.  The preg or vreg allocated may not have the same VT as was
    // expected.
    //
    // This can also happen for a return value that disagrees with the register
    // class it is put in, eg. a double in a general-purpose register on a
    // 32-bit machine.
    if (ResultVT != V.getValueType() &&
        ResultVT.getSizeInBits() == V.getValueSizeInBits())
      V = DAG.getNode(ISD::BITCAST, getCurSDLoc(), ResultVT, V);
    else if (ResultVT != V.getValueType() && ResultVT.isInteger() &&
             V.getValueType().isInteger()) {
      // If a result value was tied to an input value, the computed result
      // may have a wider width than the expected result.  Extract the
      // relevant portion.
      V = DAG.getNode(ISD::TRUNCATE, getCurSDLoc(), ResultVT, V);
    }
    assert(ResultVT == V.getValueType() && "Asm result value mismatch!");
    ResultVTs.push_back(ResultVT);
    ResultValues.push_back(V);
  };

  // Deal with output operands.
  for (SDISelAsmOperandInfo &OpInfo : ConstraintOperands) {
    if (OpInfo.Type == InlineAsm::isOutput) {
      SDValue Val;
      // Skip trivial output operands.
      if (OpInfo.AssignedRegs.Regs.empty())
        continue;

      switch (OpInfo.ConstraintType) {
      case TargetLowering::C_Register:
      case TargetLowering::C_RegisterClass:
        Val = OpInfo.AssignedRegs.getCopyFromRegs(DAG, FuncInfo, getCurSDLoc(),
                                                  Chain, &Glue, &Call);
        break;
      case TargetLowering::C_Immediate:
      case TargetLowering::C_Other:
        Val = TLI.LowerAsmOutputForConstraint(Chain, Glue, getCurSDLoc(),
                                              OpInfo, DAG);
        break;
      case TargetLowering::C_Memory:
        break; // Already handled.
      case TargetLowering::C_Address:
        break; // Silence warning.
      case TargetLowering::C_Unknown:
        assert(false && "Unexpected unknown constraint");
      }

      // Indirect output manifest as stores. Record output chains.
      if (OpInfo.isIndirect) {
        const Value *Ptr = OpInfo.CallOperandVal;
        assert(Ptr && "Expected value CallOperandVal for indirect asm operand");
        SDValue Store = DAG.getStore(Chain, getCurSDLoc(), Val, getValue(Ptr),
                                     MachinePointerInfo(Ptr));
        OutChains.push_back(Store);
      } else {
        // generate CopyFromRegs to associated registers.
        assert(!Call.getType()->isVoidTy() && "Bad inline asm!");
        if (Val.getOpcode() == ISD::MERGE_VALUES) {
          for (const SDValue &V : Val->op_values())
            handleRegAssign(V);
        } else
          handleRegAssign(Val);
      }
    }
  }

  // Set results.
  if (!ResultValues.empty()) {
    assert(CurResultType == ResultTypes.end() &&
           "Mismatch in number of ResultTypes");
    assert(ResultValues.size() == ResultTypes.size() &&
           "Mismatch in number of output operands in asm result");

    SDValue V = DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                            DAG.getVTList(ResultVTs), ResultValues);
    setValue(&Call, V);
  }

  // Collect store chains.
  if (!OutChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, getCurSDLoc(), MVT::Other, OutChains);

  if (EmitEHLabels) {
    Chain = lowerEndEH(Chain, cast<InvokeInst>(&Call), EHPadBB, BeginLabel);
  }

  // Only Update Root if inline assembly has a memory effect.
  if (ResultValues.empty() || HasSideEffect || !OutChains.empty() || IsCallBr ||
      EmitEHLabels)
    DAG.setRoot(Chain);
}

void SelectionDAGBuilder::emitInlineAsmError(const CallBase &Call,
                                             const Twine &Message) {
  LLVMContext &Ctx = *DAG.getContext();
  Ctx.emitError(&Call, Message);

  // Make sure we leave the DAG in a valid state
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SmallVector<EVT, 1> ValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), Call.getType(), ValueVTs);

  if (ValueVTs.empty())
    return;

  SmallVector<SDValue, 1> Ops;
  for (const EVT &VT : ValueVTs)
    Ops.push_back(DAG.getUNDEF(VT));

  setValue(&Call, DAG.getMergeValues(Ops, getCurSDLoc()));
}

void SelectionDAGBuilder::visitVAStart(const CallInst &I) {
  DAG.setRoot(DAG.getNode(ISD::VASTART, getCurSDLoc(),
                          MVT::Other, getRoot(),
                          getValue(I.getArgOperand(0)),
                          DAG.getSrcValue(I.getArgOperand(0))));
}

void SelectionDAGBuilder::visitVAArg(const VAArgInst &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const DataLayout &DL = DAG.getDataLayout();
  SDValue V = DAG.getVAArg(
      TLI.getMemValueType(DAG.getDataLayout(), I.getType()), getCurSDLoc(),
      getRoot(), getValue(I.getOperand(0)), DAG.getSrcValue(I.getOperand(0)),
      DL.getABITypeAlign(I.getType()).value());
  DAG.setRoot(V.getValue(1));

  if (I.getType()->isPointerTy())
    V = DAG.getPtrExtOrTrunc(
        V, getCurSDLoc(), TLI.getValueType(DAG.getDataLayout(), I.getType()));
  setValue(&I, V);
}

void SelectionDAGBuilder::visitVAEnd(const CallInst &I) {
  DAG.setRoot(DAG.getNode(ISD::VAEND, getCurSDLoc(),
                          MVT::Other, getRoot(),
                          getValue(I.getArgOperand(0)),
                          DAG.getSrcValue(I.getArgOperand(0))));
}

void SelectionDAGBuilder::visitVACopy(const CallInst &I) {
  DAG.setRoot(DAG.getNode(ISD::VACOPY, getCurSDLoc(),
                          MVT::Other, getRoot(),
                          getValue(I.getArgOperand(0)),
                          getValue(I.getArgOperand(1)),
                          DAG.getSrcValue(I.getArgOperand(0)),
                          DAG.getSrcValue(I.getArgOperand(1))));
}

SDValue SelectionDAGBuilder::lowerRangeToAssertZExt(SelectionDAG &DAG,
                                                    const Instruction &I,
                                                    SDValue Op) {
  std::optional<ConstantRange> CR = getRange(I);

  if (!CR || CR->isFullSet() || CR->isEmptySet() || CR->isUpperWrapped())
    return Op;

  APInt Lo = CR->getUnsignedMin();
  if (!Lo.isMinValue())
    return Op;

  APInt Hi = CR->getUnsignedMax();
  unsigned Bits = std::max(Hi.getActiveBits(),
                           static_cast<unsigned>(IntegerType::MIN_INT_BITS));

  EVT SmallVT = EVT::getIntegerVT(*DAG.getContext(), Bits);

  SDLoc SL = getCurSDLoc();

  SDValue ZExt = DAG.getNode(ISD::AssertZext, SL, Op.getValueType(), Op,
                             DAG.getValueType(SmallVT));
  unsigned NumVals = Op.getNode()->getNumValues();
  if (NumVals == 1)
    return ZExt;

  SmallVector<SDValue, 4> Ops;

  Ops.push_back(ZExt);
  for (unsigned I = 1; I != NumVals; ++I)
    Ops.push_back(Op.getValue(I));

  return DAG.getMergeValues(Ops, SL);
}

/// Populate a CallLowerinInfo (into \p CLI) based on the properties of
/// the call being lowered.
///
/// This is a helper for lowering intrinsics that follow a target calling
/// convention or require stack pointer adjustment. Only a subset of the
/// intrinsic's operands need to participate in the calling convention.
void SelectionDAGBuilder::populateCallLoweringInfo(
    TargetLowering::CallLoweringInfo &CLI, const CallBase *Call,
    unsigned ArgIdx, unsigned NumArgs, SDValue Callee, Type *ReturnTy,
    AttributeSet RetAttrs, bool IsPatchPoint) {
  TargetLowering::ArgListTy Args;
  Args.reserve(NumArgs);

  // Populate the argument list.
  // Attributes for args start at offset 1, after the return attribute.
  for (unsigned ArgI = ArgIdx, ArgE = ArgIdx + NumArgs;
       ArgI != ArgE; ++ArgI) {
    const Value *V = Call->getOperand(ArgI);

    assert(!V->getType()->isEmptyTy() && "Empty type passed to intrinsic.");

    TargetLowering::ArgListEntry Entry;
    Entry.Node = getValue(V);
    Entry.Ty = V->getType();
    Entry.setAttributes(Call, ArgI);
    Args.push_back(Entry);
  }

  CLI.setDebugLoc(getCurSDLoc())
      .setChain(getRoot())
      .setCallee(Call->getCallingConv(), ReturnTy, Callee, std::move(Args),
                 RetAttrs)
      .setDiscardResult(Call->use_empty())
      .setIsPatchPoint(IsPatchPoint)
      .setIsPreallocated(
          Call->countOperandBundlesOfType(LLVMContext::OB_preallocated) != 0);
}

/// Add a stack map intrinsic call's live variable operands to a stackmap
/// or patchpoint target node's operand list.
///
/// Constants are converted to TargetConstants purely as an optimization to
/// avoid constant materialization and register allocation.
///
/// FrameIndex operands are converted to TargetFrameIndex so that ISEL does not
/// generate addess computation nodes, and so FinalizeISel can convert the
/// TargetFrameIndex into a DirectMemRefOp StackMap location. This avoids
/// address materialization and register allocation, but may also be required
/// for correctness. If a StackMap (or PatchPoint) intrinsic directly uses an
/// alloca in the entry block, then the runtime may assume that the alloca's
/// StackMap location can be read immediately after compilation and that the
/// location is valid at any point during execution (this is similar to the
/// assumption made by the llvm.gcroot intrinsic). If the alloca's location were
/// only available in a register, then the runtime would need to trap when
/// execution reaches the StackMap in order to read the alloca's location.
static void addStackMapLiveVars(const CallBase &Call, unsigned StartIdx,
                                const SDLoc &DL, SmallVectorImpl<SDValue> &Ops,
                                SelectionDAGBuilder &Builder) {
  SelectionDAG &DAG = Builder.DAG;
  for (unsigned I = StartIdx; I < Call.arg_size(); I++) {
    SDValue Op = Builder.getValue(Call.getArgOperand(I));

    // Things on the stack are pointer-typed, meaning that they are already
    // legal and can be emitted directly to target nodes.
    if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(Op)) {
      Ops.push_back(DAG.getTargetFrameIndex(FI->getIndex(), Op.getValueType()));
    } else {
      // Otherwise emit a target independent node to be legalised.
      Ops.push_back(Builder.getValue(Call.getArgOperand(I)));
    }
  }
}

/// Lower llvm.experimental.stackmap.
void SelectionDAGBuilder::visitStackmap(const CallInst &CI) {
  // void @llvm.experimental.stackmap(i64 <id>, i32 <numShadowBytes>,
  //                                  [live variables...])

  assert(CI.getType()->isVoidTy() && "Stackmap cannot return a value.");

  SDValue Chain, InGlue, Callee;
  SmallVector<SDValue, 32> Ops;

  SDLoc DL = getCurSDLoc();
  Callee = getValue(CI.getCalledOperand());

  // The stackmap intrinsic only records the live variables (the arguments
  // passed to it) and emits NOPS (if requested). Unlike the patchpoint
  // intrinsic, this won't be lowered to a function call. This means we don't
  // have to worry about calling conventions and target specific lowering code.
  // Instead we perform the call lowering right here.
  //
  // chain, flag = CALLSEQ_START(chain, 0, 0)
  // chain, flag = STACKMAP(id, nbytes, ..., chain, flag)
  // chain, flag = CALLSEQ_END(chain, 0, 0, flag)
  //
  Chain = DAG.getCALLSEQ_START(getRoot(), 0, 0, DL);
  InGlue = Chain.getValue(1);

  // Add the STACKMAP operands, starting with DAG house-keeping.
  Ops.push_back(Chain);
  Ops.push_back(InGlue);

  // Add the <id>, <numShadowBytes> operands.
  //
  // These do not require legalisation, and can be emitted directly to target
  // constant nodes.
  SDValue ID = getValue(CI.getArgOperand(0));
  assert(ID.getValueType() == MVT::i64);
  SDValue IDConst =
      DAG.getTargetConstant(ID->getAsZExtVal(), DL, ID.getValueType());
  Ops.push_back(IDConst);

  SDValue Shad = getValue(CI.getArgOperand(1));
  assert(Shad.getValueType() == MVT::i32);
  SDValue ShadConst =
      DAG.getTargetConstant(Shad->getAsZExtVal(), DL, Shad.getValueType());
  Ops.push_back(ShadConst);

  // Add the live variables.
  addStackMapLiveVars(CI, 2, DL, Ops, *this);

  // Create the STACKMAP node.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(ISD::STACKMAP, DL, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, 0, 0, InGlue, DL);

  // Stackmaps don't generate values, so nothing goes into the NodeMap.

  // Set the root to the target-lowered call chain.
  DAG.setRoot(Chain);

  // Inform the Frame Information that we have a stackmap in this function.
  FuncInfo.MF->getFrameInfo().setHasStackMap();
}

/// Lower llvm.experimental.patchpoint directly to its target opcode.
void SelectionDAGBuilder::visitPatchpoint(const CallBase &CB,
                                          const BasicBlock *EHPadBB) {
  // <ty> @llvm.experimental.patchpoint.<ty>(i64 <id>,
  //                                         i32 <numBytes>,
  //                                         i8* <target>,
  //                                         i32 <numArgs>,
  //                                         [Args...],
  //                                         [live variables...])

  CallingConv::ID CC = CB.getCallingConv();
  bool IsAnyRegCC = CC == CallingConv::AnyReg;
  bool HasDef = !CB.getType()->isVoidTy();
  SDLoc dl = getCurSDLoc();
  SDValue Callee = getValue(CB.getArgOperand(PatchPointOpers::TargetPos));

  // Handle immediate and symbolic callees.
  if (auto* ConstCallee = dyn_cast<ConstantSDNode>(Callee))
    Callee = DAG.getIntPtrConstant(ConstCallee->getZExtValue(), dl,
                                   /*isTarget=*/true);
  else if (auto* SymbolicCallee = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee =  DAG.getTargetGlobalAddress(SymbolicCallee->getGlobal(),
                                         SDLoc(SymbolicCallee),
                                         SymbolicCallee->getValueType(0));

  // Get the real number of arguments participating in the call <numArgs>
  SDValue NArgVal = getValue(CB.getArgOperand(PatchPointOpers::NArgPos));
  unsigned NumArgs = NArgVal->getAsZExtVal();

  // Skip the four meta args: <id>, <numNopBytes>, <target>, <numArgs>
  // Intrinsics include all meta-operands up to but not including CC.
  unsigned NumMetaOpers = PatchPointOpers::CCPos;
  assert(CB.arg_size() >= NumMetaOpers + NumArgs &&
         "Not enough arguments provided to the patchpoint intrinsic");

  // For AnyRegCC the arguments are lowered later on manually.
  unsigned NumCallArgs = IsAnyRegCC ? 0 : NumArgs;
  Type *ReturnTy =
      IsAnyRegCC ? Type::getVoidTy(*DAG.getContext()) : CB.getType();

  TargetLowering::CallLoweringInfo CLI(DAG);
  populateCallLoweringInfo(CLI, &CB, NumMetaOpers, NumCallArgs, Callee,
                           ReturnTy, CB.getAttributes().getRetAttrs(), true);
  std::pair<SDValue, SDValue> Result = lowerInvokable(CLI, EHPadBB);

  SDNode *CallEnd = Result.second.getNode();
  if (CallEnd->getOpcode() == ISD::EH_LABEL)
    CallEnd = CallEnd->getOperand(0).getNode();
  if (HasDef && (CallEnd->getOpcode() == ISD::CopyFromReg))
    CallEnd = CallEnd->getOperand(0).getNode();

  /// Get a call instruction from the call sequence chain.
  /// Tail calls are not allowed.
  assert(CallEnd->getOpcode() == ISD::CALLSEQ_END &&
         "Expected a callseq node.");
  SDNode *Call = CallEnd->getOperand(0).getNode();
  bool HasGlue = Call->getGluedNode();

  // Replace the target specific call node with the patchable intrinsic.
  SmallVector<SDValue, 8> Ops;

  // Push the chain.
  Ops.push_back(*(Call->op_begin()));

  // Optionally, push the glue (if any).
  if (HasGlue)
    Ops.push_back(*(Call->op_end() - 1));

  // Push the register mask info.
  if (HasGlue)
    Ops.push_back(*(Call->op_end() - 2));
  else
    Ops.push_back(*(Call->op_end() - 1));

  // Add the <id> and <numBytes> constants.
  SDValue IDVal = getValue(CB.getArgOperand(PatchPointOpers::IDPos));
  Ops.push_back(DAG.getTargetConstant(IDVal->getAsZExtVal(), dl, MVT::i64));
  SDValue NBytesVal = getValue(CB.getArgOperand(PatchPointOpers::NBytesPos));
  Ops.push_back(DAG.getTargetConstant(NBytesVal->getAsZExtVal(), dl, MVT::i32));

  // Add the callee.
  Ops.push_back(Callee);

  // Adjust <numArgs> to account for any arguments that have been passed on the
  // stack instead.
  // Call Node: Chain, Target, {Args}, RegMask, [Glue]
  unsigned NumCallRegArgs = Call->getNumOperands() - (HasGlue ? 4 : 3);
  NumCallRegArgs = IsAnyRegCC ? NumArgs : NumCallRegArgs;
  Ops.push_back(DAG.getTargetConstant(NumCallRegArgs, dl, MVT::i32));

  // Add the calling convention
  Ops.push_back(DAG.getTargetConstant((unsigned)CC, dl, MVT::i32));

  // Add the arguments we omitted previously. The register allocator should
  // place these in any free register.
  if (IsAnyRegCC)
    for (unsigned i = NumMetaOpers, e = NumMetaOpers + NumArgs; i != e; ++i)
      Ops.push_back(getValue(CB.getArgOperand(i)));

  // Push the arguments from the call instruction.
  SDNode::op_iterator e = HasGlue ? Call->op_end()-2 : Call->op_end()-1;
  Ops.append(Call->op_begin() + 2, e);

  // Push live variables for the stack map.
  addStackMapLiveVars(CB, NumMetaOpers + NumArgs, dl, Ops, *this);

  SDVTList NodeTys;
  if (IsAnyRegCC && HasDef) {
    // Create the return types based on the intrinsic definition
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();
    SmallVector<EVT, 3> ValueVTs;
    ComputeValueVTs(TLI, DAG.getDataLayout(), CB.getType(), ValueVTs);
    assert(ValueVTs.size() == 1 && "Expected only one return value type.");

    // There is always a chain and a glue type at the end
    ValueVTs.push_back(MVT::Other);
    ValueVTs.push_back(MVT::Glue);
    NodeTys = DAG.getVTList(ValueVTs);
  } else
    NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  // Replace the target specific call node with a PATCHPOINT node.
  SDValue PPV = DAG.getNode(ISD::PATCHPOINT, dl, NodeTys, Ops);

  // Update the NodeMap.
  if (HasDef) {
    if (IsAnyRegCC)
      setValue(&CB, SDValue(PPV.getNode(), 0));
    else
      setValue(&CB, Result.first);
  }

  // Fixup the consumers of the intrinsic. The chain and glue may be used in the
  // call sequence. Furthermore the location of the chain and glue can change
  // when the AnyReg calling convention is used and the intrinsic returns a
  // value.
  if (IsAnyRegCC && HasDef) {
    SDValue From[] = {SDValue(Call, 0), SDValue(Call, 1)};
    SDValue To[] = {PPV.getValue(1), PPV.getValue(2)};
    DAG.ReplaceAllUsesOfValuesWith(From, To, 2);
  } else
    DAG.ReplaceAllUsesWith(Call, PPV.getNode());
  DAG.DeleteNode(Call);

  // Inform the Frame Information that we have a patchpoint in this function.
  FuncInfo.MF->getFrameInfo().setHasPatchPoint();
}

void SelectionDAGBuilder::visitVectorReduce(const CallInst &I,
                                            unsigned Intrinsic) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Op1 = getValue(I.getArgOperand(0));
  SDValue Op2;
  if (I.arg_size() > 1)
    Op2 = getValue(I.getArgOperand(1));
  SDLoc dl = getCurSDLoc();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  SDValue Res;
  SDNodeFlags SDFlags;
  if (auto *FPMO = dyn_cast<FPMathOperator>(&I))
    SDFlags.copyFMF(*FPMO);

  switch (Intrinsic) {
  case Intrinsic::vector_reduce_fadd:
    if (SDFlags.hasAllowReassociation())
      Res = DAG.getNode(ISD::FADD, dl, VT, Op1,
                        DAG.getNode(ISD::VECREDUCE_FADD, dl, VT, Op2, SDFlags),
                        SDFlags);
    else
      Res = DAG.getNode(ISD::VECREDUCE_SEQ_FADD, dl, VT, Op1, Op2, SDFlags);
    break;
  case Intrinsic::vector_reduce_fmul:
    if (SDFlags.hasAllowReassociation())
      Res = DAG.getNode(ISD::FMUL, dl, VT, Op1,
                        DAG.getNode(ISD::VECREDUCE_FMUL, dl, VT, Op2, SDFlags),
                        SDFlags);
    else
      Res = DAG.getNode(ISD::VECREDUCE_SEQ_FMUL, dl, VT, Op1, Op2, SDFlags);
    break;
  case Intrinsic::vector_reduce_add:
    Res = DAG.getNode(ISD::VECREDUCE_ADD, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_mul:
    Res = DAG.getNode(ISD::VECREDUCE_MUL, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_and:
    Res = DAG.getNode(ISD::VECREDUCE_AND, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_or:
    Res = DAG.getNode(ISD::VECREDUCE_OR, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_xor:
    Res = DAG.getNode(ISD::VECREDUCE_XOR, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_smax:
    Res = DAG.getNode(ISD::VECREDUCE_SMAX, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_smin:
    Res = DAG.getNode(ISD::VECREDUCE_SMIN, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_umax:
    Res = DAG.getNode(ISD::VECREDUCE_UMAX, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_umin:
    Res = DAG.getNode(ISD::VECREDUCE_UMIN, dl, VT, Op1);
    break;
  case Intrinsic::vector_reduce_fmax:
    Res = DAG.getNode(ISD::VECREDUCE_FMAX, dl, VT, Op1, SDFlags);
    break;
  case Intrinsic::vector_reduce_fmin:
    Res = DAG.getNode(ISD::VECREDUCE_FMIN, dl, VT, Op1, SDFlags);
    break;
  case Intrinsic::vector_reduce_fmaximum:
    Res = DAG.getNode(ISD::VECREDUCE_FMAXIMUM, dl, VT, Op1, SDFlags);
    break;
  case Intrinsic::vector_reduce_fminimum:
    Res = DAG.getNode(ISD::VECREDUCE_FMINIMUM, dl, VT, Op1, SDFlags);
    break;
  default:
    llvm_unreachable("Unhandled vector reduce intrinsic");
  }
  setValue(&I, Res);
}

/// Returns an AttributeList representing the attributes applied to the return
/// value of the given call.
static AttributeList getReturnAttrs(TargetLowering::CallLoweringInfo &CLI) {
  SmallVector<Attribute::AttrKind, 2> Attrs;
  if (CLI.RetSExt)
    Attrs.push_back(Attribute::SExt);
  if (CLI.RetZExt)
    Attrs.push_back(Attribute::ZExt);
  if (CLI.IsInReg)
    Attrs.push_back(Attribute::InReg);

  return AttributeList::get(CLI.RetTy->getContext(), AttributeList::ReturnIndex,
                            Attrs);
}

/// TargetLowering::LowerCallTo - This is the default LowerCallTo
/// implementation, which just calls LowerCall.
/// FIXME: When all targets are
/// migrated to using LowerCall, this hook should be integrated into SDISel.
std::pair<SDValue, SDValue>
TargetLowering::LowerCallTo(TargetLowering::CallLoweringInfo &CLI) const {
  // Handle the incoming return values from the call.
  CLI.Ins.clear();
  Type *OrigRetTy = CLI.RetTy;
  SmallVector<EVT, 4> RetTys;
  SmallVector<TypeSize, 4> Offsets;
  auto &DL = CLI.DAG.getDataLayout();
  ComputeValueVTs(*this, DL, CLI.RetTy, RetTys, &Offsets);

  if (CLI.IsPostTypeLegalization) {
    // If we are lowering a libcall after legalization, split the return type.
    SmallVector<EVT, 4> OldRetTys;
    SmallVector<TypeSize, 4> OldOffsets;
    RetTys.swap(OldRetTys);
    Offsets.swap(OldOffsets);

    for (size_t i = 0, e = OldRetTys.size(); i != e; ++i) {
      EVT RetVT = OldRetTys[i];
      uint64_t Offset = OldOffsets[i];
      MVT RegisterVT = getRegisterType(CLI.RetTy->getContext(), RetVT);
      unsigned NumRegs = getNumRegisters(CLI.RetTy->getContext(), RetVT);
      unsigned RegisterVTByteSZ = RegisterVT.getSizeInBits() / 8;
      RetTys.append(NumRegs, RegisterVT);
      for (unsigned j = 0; j != NumRegs; ++j)
        Offsets.push_back(TypeSize::getFixed(Offset + j * RegisterVTByteSZ));
    }
  }

  SmallVector<ISD::OutputArg, 4> Outs;
  GetReturnInfo(CLI.CallConv, CLI.RetTy, getReturnAttrs(CLI), Outs, *this, DL);

  bool CanLowerReturn =
      this->CanLowerReturn(CLI.CallConv, CLI.DAG.getMachineFunction(),
                           CLI.IsVarArg, Outs, CLI.RetTy->getContext());

  SDValue DemoteStackSlot;
  int DemoteStackIdx = -100;
  if (!CanLowerReturn) {
    // FIXME: equivalent assert?
    // assert(!CS.hasInAllocaArgument() &&
    //        "sret demotion is incompatible with inalloca");
    uint64_t TySize = DL.getTypeAllocSize(CLI.RetTy);
    Align Alignment = DL.getPrefTypeAlign(CLI.RetTy);
    MachineFunction &MF = CLI.DAG.getMachineFunction();
    DemoteStackIdx =
        MF.getFrameInfo().CreateStackObject(TySize, Alignment, false);
    Type *StackSlotPtrType = PointerType::get(CLI.RetTy,
                                              DL.getAllocaAddrSpace());

    DemoteStackSlot = CLI.DAG.getFrameIndex(DemoteStackIdx, getFrameIndexTy(DL));
    ArgListEntry Entry;
    Entry.Node = DemoteStackSlot;
    Entry.Ty = StackSlotPtrType;
    Entry.IsSExt = false;
    Entry.IsZExt = false;
    Entry.IsInReg = false;
    Entry.IsSRet = true;
    Entry.IsNest = false;
    Entry.IsByVal = false;
    Entry.IsByRef = false;
    Entry.IsReturned = false;
    Entry.IsSwiftSelf = false;
    Entry.IsSwiftAsync = false;
    Entry.IsSwiftError = false;
    Entry.IsCFGuardTarget = false;
    Entry.Alignment = Alignment;
    CLI.getArgs().insert(CLI.getArgs().begin(), Entry);
    CLI.NumFixedArgs += 1;
    CLI.getArgs()[0].IndirectType = CLI.RetTy;
    CLI.RetTy = Type::getVoidTy(CLI.RetTy->getContext());

    // sret demotion isn't compatible with tail-calls, since the sret argument
    // points into the callers stack frame.
    CLI.IsTailCall = false;
  } else {
    bool NeedsRegBlock = functionArgumentNeedsConsecutiveRegisters(
        CLI.RetTy, CLI.CallConv, CLI.IsVarArg, DL);
    for (unsigned I = 0, E = RetTys.size(); I != E; ++I) {
      ISD::ArgFlagsTy Flags;
      if (NeedsRegBlock) {
        Flags.setInConsecutiveRegs();
        if (I == RetTys.size() - 1)
          Flags.setInConsecutiveRegsLast();
      }
      EVT VT = RetTys[I];
      MVT RegisterVT = getRegisterTypeForCallingConv(CLI.RetTy->getContext(),
                                                     CLI.CallConv, VT);
      unsigned NumRegs = getNumRegistersForCallingConv(CLI.RetTy->getContext(),
                                                       CLI.CallConv, VT);
      for (unsigned i = 0; i != NumRegs; ++i) {
        ISD::InputArg MyFlags;
        MyFlags.Flags = Flags;
        MyFlags.VT = RegisterVT;
        MyFlags.ArgVT = VT;
        MyFlags.Used = CLI.IsReturnValueUsed;
        if (CLI.RetTy->isPointerTy()) {
          MyFlags.Flags.setPointer();
          MyFlags.Flags.setPointerAddrSpace(
              cast<PointerType>(CLI.RetTy)->getAddressSpace());
        }
        if (CLI.RetSExt)
          MyFlags.Flags.setSExt();
        if (CLI.RetZExt)
          MyFlags.Flags.setZExt();
        if (CLI.IsInReg)
          MyFlags.Flags.setInReg();
        CLI.Ins.push_back(MyFlags);
      }
    }
  }

  // We push in swifterror return as the last element of CLI.Ins.
  ArgListTy &Args = CLI.getArgs();
  if (supportSwiftError()) {
    for (const ArgListEntry &Arg : Args) {
      if (Arg.IsSwiftError) {
        ISD::InputArg MyFlags;
        MyFlags.VT = getPointerTy(DL);
        MyFlags.ArgVT = EVT(getPointerTy(DL));
        MyFlags.Flags.setSwiftError();
        CLI.Ins.push_back(MyFlags);
      }
    }
  }

  // Handle all of the outgoing arguments.
  CLI.Outs.clear();
  CLI.OutVals.clear();
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(*this, DL, Args[i].Ty, ValueVTs);
    // FIXME: Split arguments if CLI.IsPostTypeLegalization
    Type *FinalType = Args[i].Ty;
    if (Args[i].IsByVal)
      FinalType = Args[i].IndirectType;
    bool NeedsRegBlock = functionArgumentNeedsConsecutiveRegisters(
        FinalType, CLI.CallConv, CLI.IsVarArg, DL);
    for (unsigned Value = 0, NumValues = ValueVTs.size(); Value != NumValues;
         ++Value) {
      EVT VT = ValueVTs[Value];
      Type *ArgTy = VT.getTypeForEVT(CLI.RetTy->getContext());
      SDValue Op = SDValue(Args[i].Node.getNode(),
                           Args[i].Node.getResNo() + Value);
      ISD::ArgFlagsTy Flags;

      // Certain targets (such as MIPS), may have a different ABI alignment
      // for a type depending on the context. Give the target a chance to
      // specify the alignment it wants.
      const Align OriginalAlignment(getABIAlignmentForCallingConv(ArgTy, DL));
      Flags.setOrigAlign(OriginalAlignment);

      if (Args[i].Ty->isPointerTy()) {
        Flags.setPointer();
        Flags.setPointerAddrSpace(
            cast<PointerType>(Args[i].Ty)->getAddressSpace());
      }
      if (Args[i].IsZExt)
        Flags.setZExt();
      if (Args[i].IsSExt)
        Flags.setSExt();
      if (Args[i].IsInReg) {
        // If we are using vectorcall calling convention, a structure that is
        // passed InReg - is surely an HVA
        if (CLI.CallConv == CallingConv::X86_VectorCall &&
            isa<StructType>(FinalType)) {
          // The first value of a structure is marked
          if (0 == Value)
            Flags.setHvaStart();
          Flags.setHva();
        }
        // Set InReg Flag
        Flags.setInReg();
      }
      if (Args[i].IsSRet)
        Flags.setSRet();
      if (Args[i].IsSwiftSelf)
        Flags.setSwiftSelf();
      if (Args[i].IsSwiftAsync)
        Flags.setSwiftAsync();
      if (Args[i].IsSwiftError)
        Flags.setSwiftError();
      if (Args[i].IsCFGuardTarget)
        Flags.setCFGuardTarget();
      if (Args[i].IsByVal)
        Flags.setByVal();
      if (Args[i].IsByRef)
        Flags.setByRef();
      if (Args[i].IsPreallocated) {
        Flags.setPreallocated();
        // Set the byval flag for CCAssignFn callbacks that don't know about
        // preallocated.  This way we can know how many bytes we should've
        // allocated and how many bytes a callee cleanup function will pop.  If
        // we port preallocated to more targets, we'll have to add custom
        // preallocated handling in the various CC lowering callbacks.
        Flags.setByVal();
      }
      if (Args[i].IsInAlloca) {
        Flags.setInAlloca();
        // Set the byval flag for CCAssignFn callbacks that don't know about
        // inalloca.  This way we can know how many bytes we should've allocated
        // and how many bytes a callee cleanup function will pop.  If we port
        // inalloca to more targets, we'll have to add custom inalloca handling
        // in the various CC lowering callbacks.
        Flags.setByVal();
      }
      Align MemAlign;
      if (Args[i].IsByVal || Args[i].IsInAlloca || Args[i].IsPreallocated) {
        unsigned FrameSize = DL.getTypeAllocSize(Args[i].IndirectType);
        Flags.setByValSize(FrameSize);

        // info is not there but there are cases it cannot get right.
        if (auto MA = Args[i].Alignment)
          MemAlign = *MA;
        else
          MemAlign = Align(getByValTypeAlignment(Args[i].IndirectType, DL));
      } else if (auto MA = Args[i].Alignment) {
        MemAlign = *MA;
      } else {
        MemAlign = OriginalAlignment;
      }
      Flags.setMemAlign(MemAlign);
      if (Args[i].IsNest)
        Flags.setNest();
      if (NeedsRegBlock)
        Flags.setInConsecutiveRegs();

      MVT PartVT = getRegisterTypeForCallingConv(CLI.RetTy->getContext(),
                                                 CLI.CallConv, VT);
      unsigned NumParts = getNumRegistersForCallingConv(CLI.RetTy->getContext(),
                                                        CLI.CallConv, VT);
      SmallVector<SDValue, 4> Parts(NumParts);
      ISD::NodeType ExtendKind = ISD::ANY_EXTEND;

      if (Args[i].IsSExt)
        ExtendKind = ISD::SIGN_EXTEND;
      else if (Args[i].IsZExt)
        ExtendKind = ISD::ZERO_EXTEND;

      // Conservatively only handle 'returned' on non-vectors that can be lowered,
      // for now.
      if (Args[i].IsReturned && !Op.getValueType().isVector() &&
          CanLowerReturn) {
        assert((CLI.RetTy == Args[i].Ty ||
                (CLI.RetTy->isPointerTy() && Args[i].Ty->isPointerTy() &&
                 CLI.RetTy->getPointerAddressSpace() ==
                     Args[i].Ty->getPointerAddressSpace())) &&
               RetTys.size() == NumValues && "unexpected use of 'returned'");
        // Before passing 'returned' to the target lowering code, ensure that
        // either the register MVT and the actual EVT are the same size or that
        // the return value and argument are extended in the same way; in these
        // cases it's safe to pass the argument register value unchanged as the
        // return register value (although it's at the target's option whether
        // to do so)
        // TODO: allow code generation to take advantage of partially preserved
        // registers rather than clobbering the entire register when the
        // parameter extension method is not compatible with the return
        // extension method
        if ((NumParts * PartVT.getSizeInBits() == VT.getSizeInBits()) ||
            (ExtendKind != ISD::ANY_EXTEND && CLI.RetSExt == Args[i].IsSExt &&
             CLI.RetZExt == Args[i].IsZExt))
          Flags.setReturned();
      }

      getCopyToParts(CLI.DAG, CLI.DL, Op, &Parts[0], NumParts, PartVT, CLI.CB,
                     CLI.CallConv, ExtendKind);

      for (unsigned j = 0; j != NumParts; ++j) {
        // if it isn't first piece, alignment must be 1
        // For scalable vectors the scalable part is currently handled
        // by individual targets, so we just use the known minimum size here.
        ISD::OutputArg MyFlags(
            Flags, Parts[j].getValueType().getSimpleVT(), VT,
            i < CLI.NumFixedArgs, i,
            j * Parts[j].getValueType().getStoreSize().getKnownMinValue());
        if (NumParts > 1 && j == 0)
          MyFlags.Flags.setSplit();
        else if (j != 0) {
          MyFlags.Flags.setOrigAlign(Align(1));
          if (j == NumParts - 1)
            MyFlags.Flags.setSplitEnd();
        }

        CLI.Outs.push_back(MyFlags);
        CLI.OutVals.push_back(Parts[j]);
      }

      if (NeedsRegBlock && Value == NumValues - 1)
        CLI.Outs[CLI.Outs.size() - 1].Flags.setInConsecutiveRegsLast();
    }
  }

  SmallVector<SDValue, 4> InVals;
  CLI.Chain = LowerCall(CLI, InVals);

  // Update CLI.InVals to use outside of this function.
  CLI.InVals = InVals;

  // Verify that the target's LowerCall behaved as expected.
  assert(CLI.Chain.getNode() && CLI.Chain.getValueType() == MVT::Other &&
         "LowerCall didn't return a valid chain!");
  assert((!CLI.IsTailCall || InVals.empty()) &&
         "LowerCall emitted a return value for a tail call!");
  assert((CLI.IsTailCall || InVals.size() == CLI.Ins.size()) &&
         "LowerCall didn't emit the correct number of values!");

  // For a tail call, the return value is merely live-out and there aren't
  // any nodes in the DAG representing it. Return a special value to
  // indicate that a tail call has been emitted and no more Instructions
  // should be processed in the current block.
  if (CLI.IsTailCall) {
    CLI.DAG.setRoot(CLI.Chain);
    return std::make_pair(SDValue(), SDValue());
  }

#ifndef NDEBUG
  for (unsigned i = 0, e = CLI.Ins.size(); i != e; ++i) {
    assert(InVals[i].getNode() && "LowerCall emitted a null value!");
    assert(EVT(CLI.Ins[i].VT) == InVals[i].getValueType() &&
           "LowerCall emitted a value with the wrong type!");
  }
#endif

  SmallVector<SDValue, 4> ReturnValues;
  if (!CanLowerReturn) {
    // The instruction result is the result of loading from the
    // hidden sret parameter.
    SmallVector<EVT, 1> PVTs;
    Type *PtrRetTy =
        PointerType::get(OrigRetTy->getContext(), DL.getAllocaAddrSpace());

    ComputeValueVTs(*this, DL, PtrRetTy, PVTs);
    assert(PVTs.size() == 1 && "Pointers should fit in one register");
    EVT PtrVT = PVTs[0];

    unsigned NumValues = RetTys.size();
    ReturnValues.resize(NumValues);
    SmallVector<SDValue, 4> Chains(NumValues);

    // An aggregate return value cannot wrap around the address space, so
    // offsets to its parts don't wrap either.
    SDNodeFlags Flags;
    Flags.setNoUnsignedWrap(true);

    MachineFunction &MF = CLI.DAG.getMachineFunction();
    Align HiddenSRetAlign = MF.getFrameInfo().getObjectAlign(DemoteStackIdx);
    for (unsigned i = 0; i < NumValues; ++i) {
      SDValue Add = CLI.DAG.getNode(ISD::ADD, CLI.DL, PtrVT, DemoteStackSlot,
                                    CLI.DAG.getConstant(Offsets[i], CLI.DL,
                                                        PtrVT), Flags);
      SDValue L = CLI.DAG.getLoad(
          RetTys[i], CLI.DL, CLI.Chain, Add,
          MachinePointerInfo::getFixedStack(CLI.DAG.getMachineFunction(),
                                            DemoteStackIdx, Offsets[i]),
          HiddenSRetAlign);
      ReturnValues[i] = L;
      Chains[i] = L.getValue(1);
    }

    CLI.Chain = CLI.DAG.getNode(ISD::TokenFactor, CLI.DL, MVT::Other, Chains);
  } else {
    // Collect the legal value parts into potentially illegal values
    // that correspond to the original function's return values.
    std::optional<ISD::NodeType> AssertOp;
    if (CLI.RetSExt)
      AssertOp = ISD::AssertSext;
    else if (CLI.RetZExt)
      AssertOp = ISD::AssertZext;
    unsigned CurReg = 0;
    for (EVT VT : RetTys) {
      MVT RegisterVT = getRegisterTypeForCallingConv(CLI.RetTy->getContext(),
                                                     CLI.CallConv, VT);
      unsigned NumRegs = getNumRegistersForCallingConv(CLI.RetTy->getContext(),
                                                       CLI.CallConv, VT);

      ReturnValues.push_back(getCopyFromParts(
          CLI.DAG, CLI.DL, &InVals[CurReg], NumRegs, RegisterVT, VT, nullptr,
          CLI.Chain, CLI.CallConv, AssertOp));
      CurReg += NumRegs;
    }

    // For a function returning void, there is no return value. We can't create
    // such a node, so we just return a null return value in that case. In
    // that case, nothing will actually look at the value.
    if (ReturnValues.empty())
      return std::make_pair(SDValue(), CLI.Chain);
  }

  SDValue Res = CLI.DAG.getNode(ISD::MERGE_VALUES, CLI.DL,
                                CLI.DAG.getVTList(RetTys), ReturnValues);
  return std::make_pair(Res, CLI.Chain);
}

/// Places new result values for the node in Results (their number
/// and types must exactly match those of the original return values of
/// the node), or leaves Results empty, which indicates that the node is not
/// to be custom lowered after all.
void TargetLowering::LowerOperationWrapper(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  SDValue Res = LowerOperation(SDValue(N, 0), DAG);

  if (!Res.getNode())
    return;

  // If the original node has one result, take the return value from
  // LowerOperation as is. It might not be result number 0.
  if (N->getNumValues() == 1) {
    Results.push_back(Res);
    return;
  }

  // If the original node has multiple results, then the return node should
  // have the same number of results.
  assert((N->getNumValues() == Res->getNumValues()) &&
      "Lowering returned the wrong number of results!");

  // Places new result values base on N result number.
  for (unsigned I = 0, E = N->getNumValues(); I != E; ++I)
    Results.push_back(Res.getValue(I));
}

SDValue TargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  llvm_unreachable("LowerOperation not implemented for this target!");
}

void SelectionDAGBuilder::CopyValueToVirtualRegister(const Value *V,
                                                     unsigned Reg,
                                                     ISD::NodeType ExtendType) {
  SDValue Op = getNonRegisterValue(V);
  assert((Op.getOpcode() != ISD::CopyFromReg ||
          cast<RegisterSDNode>(Op.getOperand(1))->getReg() != Reg) &&
         "Copy from a reg to the same reg!");
  assert(!Register::isPhysicalRegister(Reg) && "Is a physreg");

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  // If this is an InlineAsm we have to match the registers required, not the
  // notional registers required by the type.

  RegsForValue RFV(V->getContext(), TLI, DAG.getDataLayout(), Reg, V->getType(),
                   std::nullopt); // This is not an ABI copy.
  SDValue Chain = DAG.getEntryNode();

  if (ExtendType == ISD::ANY_EXTEND) {
    auto PreferredExtendIt = FuncInfo.PreferredExtendType.find(V);
    if (PreferredExtendIt != FuncInfo.PreferredExtendType.end())
      ExtendType = PreferredExtendIt->second;
  }
  RFV.getCopyToRegs(Op, DAG, getCurSDLoc(), Chain, nullptr, V, ExtendType);
  PendingExports.push_back(Chain);
}

#include "llvm/CodeGen/SelectionDAGISel.h"

/// isOnlyUsedInEntryBlock - If the specified argument is only used in the
/// entry block, return true.  This includes arguments used by switches, since
/// the switch may expand into multiple basic blocks.
static bool isOnlyUsedInEntryBlock(const Argument *A, bool FastISel) {
  // With FastISel active, we may be splitting blocks, so force creation
  // of virtual registers for all non-dead arguments.
  if (FastISel)
    return A->use_empty();

  const BasicBlock &Entry = A->getParent()->front();
  for (const User *U : A->users())
    if (cast<Instruction>(U)->getParent() != &Entry || isa<SwitchInst>(U))
      return false;  // Use not in entry block.

  return true;
}

using ArgCopyElisionMapTy =
    DenseMap<const Argument *,
             std::pair<const AllocaInst *, const StoreInst *>>;

/// Scan the entry block of the function in FuncInfo for arguments that look
/// like copies into a local alloca. Record any copied arguments in
/// ArgCopyElisionCandidates.
static void
findArgumentCopyElisionCandidates(const DataLayout &DL,
                                  FunctionLoweringInfo *FuncInfo,
                                  ArgCopyElisionMapTy &ArgCopyElisionCandidates) {
  // Record the state of every static alloca used in the entry block. Argument
  // allocas are all used in the entry block, so we need approximately as many
  // entries as we have arguments.
  enum StaticAllocaInfo { Unknown, Clobbered, Elidable };
  SmallDenseMap<const AllocaInst *, StaticAllocaInfo, 8> StaticAllocas;
  unsigned NumArgs = FuncInfo->Fn->arg_size();
  StaticAllocas.reserve(NumArgs * 2);

  auto GetInfoIfStaticAlloca = [&](const Value *V) -> StaticAllocaInfo * {
    if (!V)
      return nullptr;
    V = V->stripPointerCasts();
    const auto *AI = dyn_cast<AllocaInst>(V);
    if (!AI || !AI->isStaticAlloca() || !FuncInfo->StaticAllocaMap.count(AI))
      return nullptr;
    auto Iter = StaticAllocas.insert({AI, Unknown});
    return &Iter.first->second;
  };

  // Look for stores of arguments to static allocas. Look through bitcasts and
  // GEPs to handle type coercions, as long as the alloca is fully initialized
  // by the store. Any non-store use of an alloca escapes it and any subsequent
  // unanalyzed store might write it.
  // FIXME: Handle structs initialized with multiple stores.
  for (const Instruction &I : FuncInfo->Fn->getEntryBlock()) {
    // Look for stores, and handle non-store uses conservatively.
    const auto *SI = dyn_cast<StoreInst>(&I);
    if (!SI) {
      // We will look through cast uses, so ignore them completely.
      if (I.isCast())
        continue;
      // Ignore debug info and pseudo op intrinsics, they don't escape or store
      // to allocas.
      if (I.isDebugOrPseudoInst())
        continue;
      // This is an unknown instruction. Assume it escapes or writes to all
      // static alloca operands.
      for (const Use &U : I.operands()) {
        if (StaticAllocaInfo *Info = GetInfoIfStaticAlloca(U))
          *Info = StaticAllocaInfo::Clobbered;
      }
      continue;
    }

    // If the stored value is a static alloca, mark it as escaped.
    if (StaticAllocaInfo *Info = GetInfoIfStaticAlloca(SI->getValueOperand()))
      *Info = StaticAllocaInfo::Clobbered;

    // Check if the destination is a static alloca.
    const Value *Dst = SI->getPointerOperand()->stripPointerCasts();
    StaticAllocaInfo *Info = GetInfoIfStaticAlloca(Dst);
    if (!Info)
      continue;
    const AllocaInst *AI = cast<AllocaInst>(Dst);

    // Skip allocas that have been initialized or clobbered.
    if (*Info != StaticAllocaInfo::Unknown)
      continue;

    // Check if the stored value is an argument, and that this store fully
    // initializes the alloca.
    // If the argument type has padding bits we can't directly forward a pointer
    // as the upper bits may contain garbage.
    // Don't elide copies from the same argument twice.
    const Value *Val = SI->getValueOperand()->stripPointerCasts();
    const auto *Arg = dyn_cast<Argument>(Val);
    if (!Arg || Arg->hasPassPointeeByValueCopyAttr() ||
        Arg->getType()->isEmptyTy() ||
        DL.getTypeStoreSize(Arg->getType()) !=
            DL.getTypeAllocSize(AI->getAllocatedType()) ||
        !DL.typeSizeEqualsStoreSize(Arg->getType()) ||
        ArgCopyElisionCandidates.count(Arg)) {
      *Info = StaticAllocaInfo::Clobbered;
      continue;
    }

    LLVM_DEBUG(dbgs() << "Found argument copy elision candidate: " << *AI
                      << '\n');

    // Mark this alloca and store for argument copy elision.
    *Info = StaticAllocaInfo::Elidable;
    ArgCopyElisionCandidates.insert({Arg, {AI, SI}});

    // Stop scanning if we've seen all arguments. This will happen early in -O0
    // builds, which is useful, because -O0 builds have large entry blocks and
    // many allocas.
    if (ArgCopyElisionCandidates.size() == NumArgs)
      break;
  }
}

/// Try to elide argument copies from memory into a local alloca. Succeeds if
/// ArgVal is a load from a suitable fixed stack object.
static void tryToElideArgumentCopy(
    FunctionLoweringInfo &FuncInfo, SmallVectorImpl<SDValue> &Chains,
    DenseMap<int, int> &ArgCopyElisionFrameIndexMap,
    SmallPtrSetImpl<const Instruction *> &ElidedArgCopyInstrs,
    ArgCopyElisionMapTy &ArgCopyElisionCandidates, const Argument &Arg,
    ArrayRef<SDValue> ArgVals, bool &ArgHasUses) {
  // Check if this is a load from a fixed stack object.
  auto *LNode = dyn_cast<LoadSDNode>(ArgVals[0]);
  if (!LNode)
    return;
  auto *FINode = dyn_cast<FrameIndexSDNode>(LNode->getBasePtr().getNode());
  if (!FINode)
    return;

  // Check that the fixed stack object is the right size and alignment.
  // Look at the alignment that the user wrote on the alloca instead of looking
  // at the stack object.
  auto ArgCopyIter = ArgCopyElisionCandidates.find(&Arg);
  assert(ArgCopyIter != ArgCopyElisionCandidates.end());
  const AllocaInst *AI = ArgCopyIter->second.first;
  int FixedIndex = FINode->getIndex();
  int &AllocaIndex = FuncInfo.StaticAllocaMap[AI];
  int OldIndex = AllocaIndex;
  MachineFrameInfo &MFI = FuncInfo.MF->getFrameInfo();
  if (MFI.getObjectSize(FixedIndex) != MFI.getObjectSize(OldIndex)) {
    LLVM_DEBUG(
        dbgs() << "  argument copy elision failed due to bad fixed stack "
                  "object size\n");
    return;
  }
  Align RequiredAlignment = AI->getAlign();
  if (MFI.getObjectAlign(FixedIndex) < RequiredAlignment) {
    LLVM_DEBUG(dbgs() << "  argument copy elision failed: alignment of alloca "
                         "greater than stack argument alignment ("
                      << DebugStr(RequiredAlignment) << " vs "
                      << DebugStr(MFI.getObjectAlign(FixedIndex)) << ")\n");
    return;
  }

  // Perform the elision. Delete the old stack object and replace its only use
  // in the variable info map. Mark the stack object as mutable and aliased.
  LLVM_DEBUG({
    dbgs() << "Eliding argument copy from " << Arg << " to " << *AI << '\n'
           << "  Replacing frame index " << OldIndex << " with " << FixedIndex
           << '\n';
  });
  MFI.RemoveStackObject(OldIndex);
  MFI.setIsImmutableObjectIndex(FixedIndex, false);
  MFI.setIsAliasedObjectIndex(FixedIndex, true);
  AllocaIndex = FixedIndex;
  ArgCopyElisionFrameIndexMap.insert({OldIndex, FixedIndex});
  for (SDValue ArgVal : ArgVals)
    Chains.push_back(ArgVal.getValue(1));

  // Avoid emitting code for the store implementing the copy.
  const StoreInst *SI = ArgCopyIter->second.second;
  ElidedArgCopyInstrs.insert(SI);

  // Check for uses of the argument again so that we can avoid exporting ArgVal
  // if it is't used by anything other than the store.
  for (const Value *U : Arg.users()) {
    if (U != SI) {
      ArgHasUses = true;
      break;
    }
  }
}

void SelectionDAGISel::LowerArguments(const Function &F) {
  SelectionDAG &DAG = SDB->DAG;
  SDLoc dl = SDB->getCurSDLoc();
  const DataLayout &DL = DAG.getDataLayout();
  SmallVector<ISD::InputArg, 16> Ins;

  // In Naked functions we aren't going to save any registers.
  if (F.hasFnAttribute(Attribute::Naked))
    return;

  if (!FuncInfo->CanLowerReturn) {
    // Put in an sret pointer parameter before all the other parameters.
    SmallVector<EVT, 1> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(),
                    PointerType::get(F.getContext(),
                                     DAG.getDataLayout().getAllocaAddrSpace()),
                    ValueVTs);

    // NOTE: Assuming that a pointer will never break down to more than one VT
    // or one register.
    ISD::ArgFlagsTy Flags;
    Flags.setSRet();
    MVT RegisterVT = TLI->getRegisterType(*DAG.getContext(), ValueVTs[0]);
    ISD::InputArg RetArg(Flags, RegisterVT, ValueVTs[0], true,
                         ISD::InputArg::NoArgIndex, 0);
    Ins.push_back(RetArg);
  }

  // Look for stores of arguments to static allocas. Mark such arguments with a
  // flag to ask the target to give us the memory location of that argument if
  // available.
  ArgCopyElisionMapTy ArgCopyElisionCandidates;
  findArgumentCopyElisionCandidates(DL, FuncInfo.get(),
                                    ArgCopyElisionCandidates);

  // Set up the incoming argument description vector.
  for (const Argument &Arg : F.args()) {
    unsigned ArgNo = Arg.getArgNo();
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(), Arg.getType(), ValueVTs);
    bool isArgValueUsed = !Arg.use_empty();
    unsigned PartBase = 0;
    Type *FinalType = Arg.getType();
    if (Arg.hasAttribute(Attribute::ByVal))
      FinalType = Arg.getParamByValType();
    bool NeedsRegBlock = TLI->functionArgumentNeedsConsecutiveRegisters(
        FinalType, F.getCallingConv(), F.isVarArg(), DL);
    for (unsigned Value = 0, NumValues = ValueVTs.size();
         Value != NumValues; ++Value) {
      EVT VT = ValueVTs[Value];
      Type *ArgTy = VT.getTypeForEVT(*DAG.getContext());
      ISD::ArgFlagsTy Flags;


      if (Arg.getType()->isPointerTy()) {
        Flags.setPointer();
        Flags.setPointerAddrSpace(
            cast<PointerType>(Arg.getType())->getAddressSpace());
      }
      if (Arg.hasAttribute(Attribute::ZExt))
        Flags.setZExt();
      if (Arg.hasAttribute(Attribute::SExt))
        Flags.setSExt();
      if (Arg.hasAttribute(Attribute::InReg)) {
        // If we are using vectorcall calling convention, a structure that is
        // passed InReg - is surely an HVA
        if (F.getCallingConv() == CallingConv::X86_VectorCall &&
            isa<StructType>(Arg.getType())) {
          // The first value of a structure is marked
          if (0 == Value)
            Flags.setHvaStart();
          Flags.setHva();
        }
        // Set InReg Flag
        Flags.setInReg();
      }
      if (Arg.hasAttribute(Attribute::StructRet))
        Flags.setSRet();
      if (Arg.hasAttribute(Attribute::SwiftSelf))
        Flags.setSwiftSelf();
      if (Arg.hasAttribute(Attribute::SwiftAsync))
        Flags.setSwiftAsync();
      if (Arg.hasAttribute(Attribute::SwiftError))
        Flags.setSwiftError();
      if (Arg.hasAttribute(Attribute::ByVal))
        Flags.setByVal();
      if (Arg.hasAttribute(Attribute::ByRef))
        Flags.setByRef();
      if (Arg.hasAttribute(Attribute::InAlloca)) {
        Flags.setInAlloca();
        // Set the byval flag for CCAssignFn callbacks that don't know about
        // inalloca.  This way we can know how many bytes we should've allocated
        // and how many bytes a callee cleanup function will pop.  If we port
        // inalloca to more targets, we'll have to add custom inalloca handling
        // in the various CC lowering callbacks.
        Flags.setByVal();
      }
      if (Arg.hasAttribute(Attribute::Preallocated)) {
        Flags.setPreallocated();
        // Set the byval flag for CCAssignFn callbacks that don't know about
        // preallocated.  This way we can know how many bytes we should've
        // allocated and how many bytes a callee cleanup function will pop.  If
        // we port preallocated to more targets, we'll have to add custom
        // preallocated handling in the various CC lowering callbacks.
        Flags.setByVal();
      }

      // Certain targets (such as MIPS), may have a different ABI alignment
      // for a type depending on the context. Give the target a chance to
      // specify the alignment it wants.
      const Align OriginalAlignment(
          TLI->getABIAlignmentForCallingConv(ArgTy, DL));
      Flags.setOrigAlign(OriginalAlignment);

      Align MemAlign;
      Type *ArgMemTy = nullptr;
      if (Flags.isByVal() || Flags.isInAlloca() || Flags.isPreallocated() ||
          Flags.isByRef()) {
        if (!ArgMemTy)
          ArgMemTy = Arg.getPointeeInMemoryValueType();

        uint64_t MemSize = DL.getTypeAllocSize(ArgMemTy);

        // For in-memory arguments, size and alignment should be passed from FE.
        // BE will guess if this info is not there but there are cases it cannot
        // get right.
        if (auto ParamAlign = Arg.getParamStackAlign())
          MemAlign = *ParamAlign;
        else if ((ParamAlign = Arg.getParamAlign()))
          MemAlign = *ParamAlign;
        else
          MemAlign = Align(TLI->getByValTypeAlignment(ArgMemTy, DL));
        if (Flags.isByRef())
          Flags.setByRefSize(MemSize);
        else
          Flags.setByValSize(MemSize);
      } else if (auto ParamAlign = Arg.getParamStackAlign()) {
        MemAlign = *ParamAlign;
      } else {
        MemAlign = OriginalAlignment;
      }
      Flags.setMemAlign(MemAlign);

      if (Arg.hasAttribute(Attribute::Nest))
        Flags.setNest();
      if (NeedsRegBlock)
        Flags.setInConsecutiveRegs();
      if (ArgCopyElisionCandidates.count(&Arg))
        Flags.setCopyElisionCandidate();
      if (Arg.hasAttribute(Attribute::Returned))
        Flags.setReturned();

      MVT RegisterVT = TLI->getRegisterTypeForCallingConv(
          *CurDAG->getContext(), F.getCallingConv(), VT);
      unsigned NumRegs = TLI->getNumRegistersForCallingConv(
          *CurDAG->getContext(), F.getCallingConv(), VT);
      for (unsigned i = 0; i != NumRegs; ++i) {
        // For scalable vectors, use the minimum size; individual targets
        // are responsible for handling scalable vector arguments and
        // return values.
        ISD::InputArg MyFlags(
            Flags, RegisterVT, VT, isArgValueUsed, ArgNo,
            PartBase + i * RegisterVT.getStoreSize().getKnownMinValue());
        if (NumRegs > 1 && i == 0)
          MyFlags.Flags.setSplit();
        // if it isn't first piece, alignment must be 1
        else if (i > 0) {
          MyFlags.Flags.setOrigAlign(Align(1));
          if (i == NumRegs - 1)
            MyFlags.Flags.setSplitEnd();
        }
        Ins.push_back(MyFlags);
      }
      if (NeedsRegBlock && Value == NumValues - 1)
        Ins[Ins.size() - 1].Flags.setInConsecutiveRegsLast();
      PartBase += VT.getStoreSize().getKnownMinValue();
    }
  }

  // Call the target to set up the argument values.
  SmallVector<SDValue, 8> InVals;
  SDValue NewRoot = TLI->LowerFormalArguments(
      DAG.getRoot(), F.getCallingConv(), F.isVarArg(), Ins, dl, DAG, InVals);

  // Verify that the target's LowerFormalArguments behaved as expected.
  assert(NewRoot.getNode() && NewRoot.getValueType() == MVT::Other &&
         "LowerFormalArguments didn't return a valid chain!");
  assert(InVals.size() == Ins.size() &&
         "LowerFormalArguments didn't emit the correct number of values!");
  LLVM_DEBUG({
    for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
      assert(InVals[i].getNode() &&
             "LowerFormalArguments emitted a null value!");
      assert(EVT(Ins[i].VT) == InVals[i].getValueType() &&
             "LowerFormalArguments emitted a value with the wrong type!");
    }
  });

  // Update the DAG with the new chain value resulting from argument lowering.
  DAG.setRoot(NewRoot);

  // Set up the argument values.
  unsigned i = 0;
  if (!FuncInfo->CanLowerReturn) {
    // Create a virtual register for the sret pointer, and put in a copy
    // from the sret argument into it.
    SmallVector<EVT, 1> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(),
                    PointerType::get(F.getContext(),
                                     DAG.getDataLayout().getAllocaAddrSpace()),
                    ValueVTs);
    MVT VT = ValueVTs[0].getSimpleVT();
    MVT RegVT = TLI->getRegisterType(*CurDAG->getContext(), VT);
    std::optional<ISD::NodeType> AssertOp;
    SDValue ArgValue =
        getCopyFromParts(DAG, dl, &InVals[0], 1, RegVT, VT, nullptr, NewRoot,
                         F.getCallingConv(), AssertOp);

    MachineFunction& MF = SDB->DAG.getMachineFunction();
    MachineRegisterInfo& RegInfo = MF.getRegInfo();
    Register SRetReg =
        RegInfo.createVirtualRegister(TLI->getRegClassFor(RegVT));
    FuncInfo->DemoteRegister = SRetReg;
    NewRoot =
        SDB->DAG.getCopyToReg(NewRoot, SDB->getCurSDLoc(), SRetReg, ArgValue);
    DAG.setRoot(NewRoot);

    // i indexes lowered arguments.  Bump it past the hidden sret argument.
    ++i;
  }

  SmallVector<SDValue, 4> Chains;
  DenseMap<int, int> ArgCopyElisionFrameIndexMap;
  for (const Argument &Arg : F.args()) {
    SmallVector<SDValue, 4> ArgValues;
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(), Arg.getType(), ValueVTs);
    unsigned NumValues = ValueVTs.size();
    if (NumValues == 0)
      continue;

    bool ArgHasUses = !Arg.use_empty();

    // Elide the copying store if the target loaded this argument from a
    // suitable fixed stack object.
    if (Ins[i].Flags.isCopyElisionCandidate()) {
      unsigned NumParts = 0;
      for (EVT VT : ValueVTs)
        NumParts += TLI->getNumRegistersForCallingConv(*CurDAG->getContext(),
                                                       F.getCallingConv(), VT);

      tryToElideArgumentCopy(*FuncInfo, Chains, ArgCopyElisionFrameIndexMap,
                             ElidedArgCopyInstrs, ArgCopyElisionCandidates, Arg,
                             ArrayRef(&InVals[i], NumParts), ArgHasUses);
    }

    // If this argument is unused then remember its value. It is used to generate
    // debugging information.
    bool isSwiftErrorArg =
        TLI->supportSwiftError() &&
        Arg.hasAttribute(Attribute::SwiftError);
    if (!ArgHasUses && !isSwiftErrorArg) {
      SDB->setUnusedArgValue(&Arg, InVals[i]);

      // Also remember any frame index for use in FastISel.
      if (FrameIndexSDNode *FI =
          dyn_cast<FrameIndexSDNode>(InVals[i].getNode()))
        FuncInfo->setArgumentFrameIndex(&Arg, FI->getIndex());
    }

    for (unsigned Val = 0; Val != NumValues; ++Val) {
      EVT VT = ValueVTs[Val];
      MVT PartVT = TLI->getRegisterTypeForCallingConv(*CurDAG->getContext(),
                                                      F.getCallingConv(), VT);
      unsigned NumParts = TLI->getNumRegistersForCallingConv(
          *CurDAG->getContext(), F.getCallingConv(), VT);

      // Even an apparent 'unused' swifterror argument needs to be returned. So
      // we do generate a copy for it that can be used on return from the
      // function.
      if (ArgHasUses || isSwiftErrorArg) {
        std::optional<ISD::NodeType> AssertOp;
        if (Arg.hasAttribute(Attribute::SExt))
          AssertOp = ISD::AssertSext;
        else if (Arg.hasAttribute(Attribute::ZExt))
          AssertOp = ISD::AssertZext;

        ArgValues.push_back(getCopyFromParts(DAG, dl, &InVals[i], NumParts,
                                             PartVT, VT, nullptr, NewRoot,
                                             F.getCallingConv(), AssertOp));
      }

      i += NumParts;
    }

    // We don't need to do anything else for unused arguments.
    if (ArgValues.empty())
      continue;

    // Note down frame index.
    if (FrameIndexSDNode *FI =
        dyn_cast<FrameIndexSDNode>(ArgValues[0].getNode()))
      FuncInfo->setArgumentFrameIndex(&Arg, FI->getIndex());

    SDValue Res = DAG.getMergeValues(ArrayRef(ArgValues.data(), NumValues),
                                     SDB->getCurSDLoc());

    SDB->setValue(&Arg, Res);
    if (!TM.Options.EnableFastISel && Res.getOpcode() == ISD::BUILD_PAIR) {
      // We want to associate the argument with the frame index, among
      // involved operands, that correspond to the lowest address. The
      // getCopyFromParts function, called earlier, is swapping the order of
      // the operands to BUILD_PAIR depending on endianness. The result of
      // that swapping is that the least significant bits of the argument will
      // be in the first operand of the BUILD_PAIR node, and the most
      // significant bits will be in the second operand.
      unsigned LowAddressOp = DAG.getDataLayout().isBigEndian() ? 1 : 0;
      if (LoadSDNode *LNode =
          dyn_cast<LoadSDNode>(Res.getOperand(LowAddressOp).getNode()))
        if (FrameIndexSDNode *FI =
            dyn_cast<FrameIndexSDNode>(LNode->getBasePtr().getNode()))
          FuncInfo->setArgumentFrameIndex(&Arg, FI->getIndex());
    }

    // Analyses past this point are naive and don't expect an assertion.
    if (Res.getOpcode() == ISD::AssertZext)
      Res = Res.getOperand(0);

    // Update the SwiftErrorVRegDefMap.
    if (Res.getOpcode() == ISD::CopyFromReg && isSwiftErrorArg) {
      unsigned Reg = cast<RegisterSDNode>(Res.getOperand(1))->getReg();
      if (Register::isVirtualRegister(Reg))
        SwiftError->setCurrentVReg(FuncInfo->MBB, SwiftError->getFunctionArg(),
                                   Reg);
    }

    // If this argument is live outside of the entry block, insert a copy from
    // wherever we got it to the vreg that other BB's will reference it as.
    if (Res.getOpcode() == ISD::CopyFromReg) {
      // If we can, though, try to skip creating an unnecessary vreg.
      // FIXME: This isn't very clean... it would be nice to make this more
      // general.
      unsigned Reg = cast<RegisterSDNode>(Res.getOperand(1))->getReg();
      if (Register::isVirtualRegister(Reg)) {
        FuncInfo->ValueMap[&Arg] = Reg;
        continue;
      }
    }
    if (!isOnlyUsedInEntryBlock(&Arg, TM.Options.EnableFastISel)) {
      FuncInfo->InitializeRegForValue(&Arg);
      SDB->CopyToExportRegsIfNeeded(&Arg);
    }
  }

  if (!Chains.empty()) {
    Chains.push_back(NewRoot);
    NewRoot = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
  }

  DAG.setRoot(NewRoot);

  assert(i == InVals.size() && "Argument register count mismatch!");

  // If any argument copy elisions occurred and we have debug info, update the
  // stale frame indices used in the dbg.declare variable info table.
  if (!ArgCopyElisionFrameIndexMap.empty()) {
    for (MachineFunction::VariableDbgInfo &VI :
         MF->getInStackSlotVariableDbgInfo()) {
      auto I = ArgCopyElisionFrameIndexMap.find(VI.getStackSlot());
      if (I != ArgCopyElisionFrameIndexMap.end())
        VI.updateStackSlot(I->second);
    }
  }

  // Finally, if the target has anything special to do, allow it to do so.
  emitFunctionEntryCode();
}

/// Handle PHI nodes in successor blocks.  Emit code into the SelectionDAG to
/// ensure constants are generated when needed.  Remember the virtual registers
/// that need to be added to the Machine PHI nodes as input.  We cannot just
/// directly add them, because expansion might result in multiple MBB's for one
/// BB.  As such, the start of the BB might correspond to a different MBB than
/// the end.
void
SelectionDAGBuilder::HandlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  SmallPtrSet<MachineBasicBlock *, 4> SuccsHandled;

  // Check PHI nodes in successors that expect a value to be available from this
  // block.
  for (const BasicBlock *SuccBB : successors(LLVMBB->getTerminator())) {
    if (!isa<PHINode>(SuccBB->begin())) continue;
    MachineBasicBlock *SuccMBB = FuncInfo.MBBMap[SuccBB];

    // If this terminator has multiple identical successors (common for
    // switches), only handle each succ once.
    if (!SuccsHandled.insert(SuccMBB).second)
      continue;

    MachineBasicBlock::iterator MBBI = SuccMBB->begin();

    // At this point we know that there is a 1-1 correspondence between LLVM PHI
    // nodes and Machine PHI nodes, but the incoming operands have not been
    // emitted yet.
    for (const PHINode &PN : SuccBB->phis()) {
      // Ignore dead phi's.
      if (PN.use_empty())
        continue;

      // Skip empty types
      if (PN.getType()->isEmptyTy())
        continue;

      unsigned Reg;
      const Value *PHIOp = PN.getIncomingValueForBlock(LLVMBB);

      if (const auto *C = dyn_cast<Constant>(PHIOp)) {
        unsigned &RegOut = ConstantsOut[C];
        if (RegOut == 0) {
          RegOut = FuncInfo.CreateRegs(C);
          // We need to zero/sign extend ConstantInt phi operands to match
          // assumptions in FunctionLoweringInfo::ComputePHILiveOutRegInfo.
          ISD::NodeType ExtendType = ISD::ANY_EXTEND;
          if (auto *CI = dyn_cast<ConstantInt>(C))
            ExtendType = TLI.signExtendConstant(CI) ? ISD::SIGN_EXTEND
                                                    : ISD::ZERO_EXTEND;
          CopyValueToVirtualRegister(C, RegOut, ExtendType);
        }
        Reg = RegOut;
      } else {
        DenseMap<const Value *, Register>::iterator I =
          FuncInfo.ValueMap.find(PHIOp);
        if (I != FuncInfo.ValueMap.end())
          Reg = I->second;
        else {
          assert(isa<AllocaInst>(PHIOp) &&
                 FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(PHIOp)) &&
                 "Didn't codegen value into a register!??");
          Reg = FuncInfo.CreateRegs(PHIOp);
          CopyValueToVirtualRegister(PHIOp, Reg);
        }
      }

      // Remember that this register needs to added to the machine PHI node as
      // the input for this MBB.
      SmallVector<EVT, 4> ValueVTs;
      ComputeValueVTs(TLI, DAG.getDataLayout(), PN.getType(), ValueVTs);
      for (EVT VT : ValueVTs) {
        const unsigned NumRegisters = TLI.getNumRegisters(*DAG.getContext(), VT);
        for (unsigned i = 0; i != NumRegisters; ++i)
          FuncInfo.PHINodesToUpdate.push_back(
              std::make_pair(&*MBBI++, Reg + i));
        Reg += NumRegisters;
      }
    }
  }

  ConstantsOut.clear();
}

MachineBasicBlock *SelectionDAGBuilder::NextBlock(MachineBasicBlock *MBB) {
  MachineFunction::iterator I(MBB);
  if (++I == FuncInfo.MF->end())
    return nullptr;
  return &*I;
}

/// During lowering new call nodes can be created (such as memset, etc.).
/// Those will become new roots of the current DAG, but complications arise
/// when they are tail calls. In such cases, the call lowering will update
/// the root, but the builder still needs to know that a tail call has been
/// lowered in order to avoid generating an additional return.
void SelectionDAGBuilder::updateDAGForMaybeTailCall(SDValue MaybeTC) {
  // If the node is null, we do have a tail call.
  if (MaybeTC.getNode() != nullptr)
    DAG.setRoot(MaybeTC);
  else
    HasTailCall = true;
}

void SelectionDAGBuilder::lowerWorkItem(SwitchWorkListItem W, Value *Cond,
                                        MachineBasicBlock *SwitchMBB,
                                        MachineBasicBlock *DefaultMBB) {
  MachineFunction *CurMF = FuncInfo.MF;
  MachineBasicBlock *NextMBB = nullptr;
  MachineFunction::iterator BBI(W.MBB);
  if (++BBI != FuncInfo.MF->end())
    NextMBB = &*BBI;

  unsigned Size = W.LastCluster - W.FirstCluster + 1;

  BranchProbabilityInfo *BPI = FuncInfo.BPI;

  if (Size == 2 && W.MBB == SwitchMBB) {
    // If any two of the cases has the same destination, and if one value
    // is the same as the other, but has one bit unset that the other has set,
    // use bit manipulation to do two compares at once.  For example:
    // "if (X == 6 || X == 4)" -> "if ((X|2) == 6)"
    // TODO: This could be extended to merge any 2 cases in switches with 3
    // cases.
    // TODO: Handle cases where W.CaseBB != SwitchBB.
    CaseCluster &Small = *W.FirstCluster;
    CaseCluster &Big = *W.LastCluster;

    if (Small.Low == Small.High && Big.Low == Big.High &&
        Small.MBB == Big.MBB) {
      const APInt &SmallValue = Small.Low->getValue();
      const APInt &BigValue = Big.Low->getValue();

      // Check that there is only one bit different.
      APInt CommonBit = BigValue ^ SmallValue;
      if (CommonBit.isPowerOf2()) {
        SDValue CondLHS = getValue(Cond);
        EVT VT = CondLHS.getValueType();
        SDLoc DL = getCurSDLoc();

        SDValue Or = DAG.getNode(ISD::OR, DL, VT, CondLHS,
                                 DAG.getConstant(CommonBit, DL, VT));
        SDValue Cond = DAG.getSetCC(
            DL, MVT::i1, Or, DAG.getConstant(BigValue | SmallValue, DL, VT),
            ISD::SETEQ);

        // Update successor info.
        // Both Small and Big will jump to Small.BB, so we sum up the
        // probabilities.
        addSuccessorWithProb(SwitchMBB, Small.MBB, Small.Prob + Big.Prob);
        if (BPI)
          addSuccessorWithProb(
              SwitchMBB, DefaultMBB,
              // The default destination is the first successor in IR.
              BPI->getEdgeProbability(SwitchMBB->getBasicBlock(), (unsigned)0));
        else
          addSuccessorWithProb(SwitchMBB, DefaultMBB);

        // Insert the true branch.
        SDValue BrCond =
            DAG.getNode(ISD::BRCOND, DL, MVT::Other, getControlRoot(), Cond,
                        DAG.getBasicBlock(Small.MBB));
        // Insert the false branch.
        BrCond = DAG.getNode(ISD::BR, DL, MVT::Other, BrCond,
                             DAG.getBasicBlock(DefaultMBB));

        DAG.setRoot(BrCond);
        return;
      }
    }
  }

  if (TM.getOptLevel() != CodeGenOptLevel::None) {
    // Here, we order cases by probability so the most likely case will be
    // checked first. However, two clusters can have the same probability in
    // which case their relative ordering is non-deterministic. So we use Low
    // as a tie-breaker as clusters are guaranteed to never overlap.
    llvm::sort(W.FirstCluster, W.LastCluster + 1,
               [](const CaseCluster &a, const CaseCluster &b) {
      return a.Prob != b.Prob ?
             a.Prob > b.Prob :
             a.Low->getValue().slt(b.Low->getValue());
    });

    // Rearrange the case blocks so that the last one falls through if possible
    // without changing the order of probabilities.
    for (CaseClusterIt I = W.LastCluster; I > W.FirstCluster; ) {
      --I;
      if (I->Prob > W.LastCluster->Prob)
        break;
      if (I->Kind == CC_Range && I->MBB == NextMBB) {
        std::swap(*I, *W.LastCluster);
        break;
      }
    }
  }

  // Compute total probability.
  BranchProbability DefaultProb = W.DefaultProb;
  BranchProbability UnhandledProbs = DefaultProb;
  for (CaseClusterIt I = W.FirstCluster; I <= W.LastCluster; ++I)
    UnhandledProbs += I->Prob;

  MachineBasicBlock *CurMBB = W.MBB;
  for (CaseClusterIt I = W.FirstCluster, E = W.LastCluster; I <= E; ++I) {
    bool FallthroughUnreachable = false;
    MachineBasicBlock *Fallthrough;
    if (I == W.LastCluster) {
      // For the last cluster, fall through to the default destination.
      Fallthrough = DefaultMBB;
      FallthroughUnreachable = isa<UnreachableInst>(
          DefaultMBB->getBasicBlock()->getFirstNonPHIOrDbg());
    } else {
      Fallthrough = CurMF->CreateMachineBasicBlock(CurMBB->getBasicBlock());
      CurMF->insert(BBI, Fallthrough);
      // Put Cond in a virtual register to make it available from the new blocks.
      ExportFromCurrentBlock(Cond);
    }
    UnhandledProbs -= I->Prob;

    switch (I->Kind) {
      case CC_JumpTable: {
        // FIXME: Optimize away range check based on pivot comparisons.
        JumpTableHeader *JTH = &SL->JTCases[I->JTCasesIndex].first;
        SwitchCG::JumpTable *JT = &SL->JTCases[I->JTCasesIndex].second;

        // The jump block hasn't been inserted yet; insert it here.
        MachineBasicBlock *JumpMBB = JT->MBB;
        CurMF->insert(BBI, JumpMBB);

        auto JumpProb = I->Prob;
        auto FallthroughProb = UnhandledProbs;

        // If the default statement is a target of the jump table, we evenly
        // distribute the default probability to successors of CurMBB. Also
        // update the probability on the edge from JumpMBB to Fallthrough.
        for (MachineBasicBlock::succ_iterator SI = JumpMBB->succ_begin(),
                                              SE = JumpMBB->succ_end();
             SI != SE; ++SI) {
          if (*SI == DefaultMBB) {
            JumpProb += DefaultProb / 2;
            FallthroughProb -= DefaultProb / 2;
            JumpMBB->setSuccProbability(SI, DefaultProb / 2);
            JumpMBB->normalizeSuccProbs();
            break;
          }
        }

        // If the default clause is unreachable, propagate that knowledge into
        // JTH->FallthroughUnreachable which will use it to suppress the range
        // check.
        //
        // However, don't do this if we're doing branch target enforcement,
        // because a table branch _without_ a range check can be a tempting JOP
        // gadget - out-of-bounds inputs that are impossible in correct
        // execution become possible again if an attacker can influence the
        // control flow. So if an attacker doesn't already have a BTI bypass
        // available, we don't want them to be able to get one out of this
        // table branch.
        if (FallthroughUnreachable) {
          Function &CurFunc = CurMF->getFunction();
          if (!CurFunc.hasFnAttribute("branch-target-enforcement"))
            JTH->FallthroughUnreachable = true;
        }

        if (!JTH->FallthroughUnreachable)
          addSuccessorWithProb(CurMBB, Fallthrough, FallthroughProb);
        addSuccessorWithProb(CurMBB, JumpMBB, JumpProb);
        CurMBB->normalizeSuccProbs();

        // The jump table header will be inserted in our current block, do the
        // range check, and fall through to our fallthrough block.
        JTH->HeaderBB = CurMBB;
        JT->Default = Fallthrough; // FIXME: Move Default to JumpTableHeader.

        // If we're in the right place, emit the jump table header right now.
        if (CurMBB == SwitchMBB) {
          visitJumpTableHeader(*JT, *JTH, SwitchMBB);
          JTH->Emitted = true;
        }
        break;
      }
      case CC_BitTests: {
        // FIXME: Optimize away range check based on pivot comparisons.
        BitTestBlock *BTB = &SL->BitTestCases[I->BTCasesIndex];

        // The bit test blocks haven't been inserted yet; insert them here.
        for (BitTestCase &BTC : BTB->Cases)
          CurMF->insert(BBI, BTC.ThisBB);

        // Fill in fields of the BitTestBlock.
        BTB->Parent = CurMBB;
        BTB->Default = Fallthrough;

        BTB->DefaultProb = UnhandledProbs;
        // If the cases in bit test don't form a contiguous range, we evenly
        // distribute the probability on the edge to Fallthrough to two
        // successors of CurMBB.
        if (!BTB->ContiguousRange) {
          BTB->Prob += DefaultProb / 2;
          BTB->DefaultProb -= DefaultProb / 2;
        }

        if (FallthroughUnreachable)
          BTB->FallthroughUnreachable = true;

        // If we're in the right place, emit the bit test header right now.
        if (CurMBB == SwitchMBB) {
          visitBitTestHeader(*BTB, SwitchMBB);
          BTB->Emitted = true;
        }
        break;
      }
      case CC_Range: {
        const Value *RHS, *LHS, *MHS;
        ISD::CondCode CC;
        if (I->Low == I->High) {
          // Check Cond == I->Low.
          CC = ISD::SETEQ;
          LHS = Cond;
          RHS=I->Low;
          MHS = nullptr;
        } else {
          // Check I->Low <= Cond <= I->High.
          CC = ISD::SETLE;
          LHS = I->Low;
          MHS = Cond;
          RHS = I->High;
        }

        // If Fallthrough is unreachable, fold away the comparison.
        if (FallthroughUnreachable)
          CC = ISD::SETTRUE;

        // The false probability is the sum of all unhandled cases.
        CaseBlock CB(CC, LHS, RHS, MHS, I->MBB, Fallthrough, CurMBB,
                     getCurSDLoc(), I->Prob, UnhandledProbs);

        if (CurMBB == SwitchMBB)
          visitSwitchCase(CB, SwitchMBB);
        else
          SL->SwitchCases.push_back(CB);

        break;
      }
    }
    CurMBB = Fallthrough;
  }
}

void SelectionDAGBuilder::splitWorkItem(SwitchWorkList &WorkList,
                                        const SwitchWorkListItem &W,
                                        Value *Cond,
                                        MachineBasicBlock *SwitchMBB) {
  assert(W.FirstCluster->Low->getValue().slt(W.LastCluster->Low->getValue()) &&
         "Clusters not sorted?");
  assert(W.LastCluster - W.FirstCluster + 1 >= 2 && "Too small to split!");

  auto [LastLeft, FirstRight, LeftProb, RightProb] =
      SL->computeSplitWorkItemInfo(W);

  // Use the first element on the right as pivot since we will make less-than
  // comparisons against it.
  CaseClusterIt PivotCluster = FirstRight;
  assert(PivotCluster > W.FirstCluster);
  assert(PivotCluster <= W.LastCluster);

  CaseClusterIt FirstLeft = W.FirstCluster;
  CaseClusterIt LastRight = W.LastCluster;

  const ConstantInt *Pivot = PivotCluster->Low;

  // New blocks will be inserted immediately after the current one.
  MachineFunction::iterator BBI(W.MBB);
  ++BBI;

  // We will branch to the LHS if Value < Pivot. If LHS is a single cluster,
  // we can branch to its destination directly if it's squeezed exactly in
  // between the known lower bound and Pivot - 1.
  MachineBasicBlock *LeftMBB;
  if (FirstLeft == LastLeft && FirstLeft->Kind == CC_Range &&
      FirstLeft->Low == W.GE &&
      (FirstLeft->High->getValue() + 1LL) == Pivot->getValue()) {
    LeftMBB = FirstLeft->MBB;
  } else {
    LeftMBB = FuncInfo.MF->CreateMachineBasicBlock(W.MBB->getBasicBlock());
    FuncInfo.MF->insert(BBI, LeftMBB);
    WorkList.push_back(
        {LeftMBB, FirstLeft, LastLeft, W.GE, Pivot, W.DefaultProb / 2});
    // Put Cond in a virtual register to make it available from the new blocks.
    ExportFromCurrentBlock(Cond);
  }

  // Similarly, we will branch to the RHS if Value >= Pivot. If RHS is a
  // single cluster, RHS.Low == Pivot, and we can branch to its destination
  // directly if RHS.High equals the current upper bound.
  MachineBasicBlock *RightMBB;
  if (FirstRight == LastRight && FirstRight->Kind == CC_Range &&
      W.LT && (FirstRight->High->getValue() + 1ULL) == W.LT->getValue()) {
    RightMBB = FirstRight->MBB;
  } else {
    RightMBB = FuncInfo.MF->CreateMachineBasicBlock(W.MBB->getBasicBlock());
    FuncInfo.MF->insert(BBI, RightMBB);
    WorkList.push_back(
        {RightMBB, FirstRight, LastRight, Pivot, W.LT, W.DefaultProb / 2});
    // Put Cond in a virtual register to make it available from the new blocks.
    ExportFromCurrentBlock(Cond);
  }

  // Create the CaseBlock record that will be used to lower the branch.
  CaseBlock CB(ISD::SETLT, Cond, Pivot, nullptr, LeftMBB, RightMBB, W.MBB,
               getCurSDLoc(), LeftProb, RightProb);

  if (W.MBB == SwitchMBB)
    visitSwitchCase(CB, SwitchMBB);
  else
    SL->SwitchCases.push_back(CB);
}

// Scale CaseProb after peeling a case with the probablity of PeeledCaseProb
// from the swith statement.
static BranchProbability scaleCaseProbality(BranchProbability CaseProb,
                                            BranchProbability PeeledCaseProb) {
  if (PeeledCaseProb == BranchProbability::getOne())
    return BranchProbability::getZero();
  BranchProbability SwitchProb = PeeledCaseProb.getCompl();

  uint32_t Numerator = CaseProb.getNumerator();
  uint32_t Denominator = SwitchProb.scale(CaseProb.getDenominator());
  return BranchProbability(Numerator, std::max(Numerator, Denominator));
}

// Try to peel the top probability case if it exceeds the threshold.
// Return current MachineBasicBlock for the switch statement if the peeling
// does not occur.
// If the peeling is performed, return the newly created MachineBasicBlock
// for the peeled switch statement. Also update Clusters to remove the peeled
// case. PeeledCaseProb is the BranchProbability for the peeled case.
MachineBasicBlock *SelectionDAGBuilder::peelDominantCaseCluster(
    const SwitchInst &SI, CaseClusterVector &Clusters,
    BranchProbability &PeeledCaseProb) {
  MachineBasicBlock *SwitchMBB = FuncInfo.MBB;
  // Don't perform if there is only one cluster or optimizing for size.
  if (SwitchPeelThreshold > 100 || !FuncInfo.BPI || Clusters.size() < 2 ||
      TM.getOptLevel() == CodeGenOptLevel::None ||
      SwitchMBB->getParent()->getFunction().hasMinSize())
    return SwitchMBB;

  BranchProbability TopCaseProb = BranchProbability(SwitchPeelThreshold, 100);
  unsigned PeeledCaseIndex = 0;
  bool SwitchPeeled = false;
  for (unsigned Index = 0; Index < Clusters.size(); ++Index) {
    CaseCluster &CC = Clusters[Index];
    if (CC.Prob < TopCaseProb)
      continue;
    TopCaseProb = CC.Prob;
    PeeledCaseIndex = Index;
    SwitchPeeled = true;
  }
  if (!SwitchPeeled)
    return SwitchMBB;

  LLVM_DEBUG(dbgs() << "Peeled one top case in switch stmt, prob: "
                    << TopCaseProb << "\n");

  // Record the MBB for the peeled switch statement.
  MachineFunction::iterator BBI(SwitchMBB);
  ++BBI;
  MachineBasicBlock *PeeledSwitchMBB =
      FuncInfo.MF->CreateMachineBasicBlock(SwitchMBB->getBasicBlock());
  FuncInfo.MF->insert(BBI, PeeledSwitchMBB);

  ExportFromCurrentBlock(SI.getCondition());
  auto PeeledCaseIt = Clusters.begin() + PeeledCaseIndex;
  SwitchWorkListItem W = {SwitchMBB, PeeledCaseIt, PeeledCaseIt,
                          nullptr,   nullptr,      TopCaseProb.getCompl()};
  lowerWorkItem(W, SI.getCondition(), SwitchMBB, PeeledSwitchMBB);

  Clusters.erase(PeeledCaseIt);
  for (CaseCluster &CC : Clusters) {
    LLVM_DEBUG(
        dbgs() << "Scale the probablity for one cluster, before scaling: "
               << CC.Prob << "\n");
    CC.Prob = scaleCaseProbality(CC.Prob, TopCaseProb);
    LLVM_DEBUG(dbgs() << "After scaling: " << CC.Prob << "\n");
  }
  PeeledCaseProb = TopCaseProb;
  return PeeledSwitchMBB;
}

void SelectionDAGBuilder::visitSwitch(const SwitchInst &SI) {
  // Extract cases from the switch.
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  CaseClusterVector Clusters;
  Clusters.reserve(SI.getNumCases());
  for (auto I : SI.cases()) {
    MachineBasicBlock *Succ = FuncInfo.MBBMap[I.getCaseSuccessor()];
    const ConstantInt *CaseVal = I.getCaseValue();
    BranchProbability Prob =
        BPI ? BPI->getEdgeProbability(SI.getParent(), I.getSuccessorIndex())
            : BranchProbability(1, SI.getNumCases() + 1);
    Clusters.push_back(CaseCluster::range(CaseVal, CaseVal, Succ, Prob));
  }

  MachineBasicBlock *DefaultMBB = FuncInfo.MBBMap[SI.getDefaultDest()];

  // Cluster adjacent cases with the same destination. We do this at all
  // optimization levels because it's cheap to do and will make codegen faster
  // if there are many clusters.
  sortAndRangeify(Clusters);

  // The branch probablity of the peeled case.
  BranchProbability PeeledCaseProb = BranchProbability::getZero();
  MachineBasicBlock *PeeledSwitchMBB =
      peelDominantCaseCluster(SI, Clusters, PeeledCaseProb);

  // If there is only the default destination, jump there directly.
  MachineBasicBlock *SwitchMBB = FuncInfo.MBB;
  if (Clusters.empty()) {
    assert(PeeledSwitchMBB == SwitchMBB);
    SwitchMBB->addSuccessor(DefaultMBB);
    if (DefaultMBB != NextBlock(SwitchMBB)) {
      DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(), MVT::Other,
                              getControlRoot(), DAG.getBasicBlock(DefaultMBB)));
    }
    return;
  }

  SL->findJumpTables(Clusters, &SI, getCurSDLoc(), DefaultMBB, DAG.getPSI(),
                     DAG.getBFI());
  SL->findBitTestClusters(Clusters, &SI);

  LLVM_DEBUG({
    dbgs() << "Case clusters: ";
    for (const CaseCluster &C : Clusters) {
      if (C.Kind == CC_JumpTable)
        dbgs() << "JT:";
      if (C.Kind == CC_BitTests)
        dbgs() << "BT:";

      C.Low->getValue().print(dbgs(), true);
      if (C.Low != C.High) {
        dbgs() << '-';
        C.High->getValue().print(dbgs(), true);
      }
      dbgs() << ' ';
    }
    dbgs() << '\n';
  });

  assert(!Clusters.empty());
  SwitchWorkList WorkList;
  CaseClusterIt First = Clusters.begin();
  CaseClusterIt Last = Clusters.end() - 1;
  auto DefaultProb = getEdgeProbability(PeeledSwitchMBB, DefaultMBB);
  // Scale the branchprobability for DefaultMBB if the peel occurs and
  // DefaultMBB is not replaced.
  if (PeeledCaseProb != BranchProbability::getZero() &&
      DefaultMBB == FuncInfo.MBBMap[SI.getDefaultDest()])
    DefaultProb = scaleCaseProbality(DefaultProb, PeeledCaseProb);
  WorkList.push_back(
      {PeeledSwitchMBB, First, Last, nullptr, nullptr, DefaultProb});

  while (!WorkList.empty()) {
    SwitchWorkListItem W = WorkList.pop_back_val();
    unsigned NumClusters = W.LastCluster - W.FirstCluster + 1;

    if (NumClusters > 3 && TM.getOptLevel() != CodeGenOptLevel::None &&
        !DefaultMBB->getParent()->getFunction().hasMinSize()) {
      // For optimized builds, lower large range as a balanced binary tree.
      splitWorkItem(WorkList, W, SI.getCondition(), SwitchMBB);
      continue;
    }

    lowerWorkItem(W, SI.getCondition(), SwitchMBB, DefaultMBB);
  }
}

void SelectionDAGBuilder::visitStepVector(const CallInst &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  auto DL = getCurSDLoc();
  EVT ResultVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  setValue(&I, DAG.getStepVector(DL, ResultVT));
}

void SelectionDAGBuilder::visitVectorReverse(const CallInst &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());

  SDLoc DL = getCurSDLoc();
  SDValue V = getValue(I.getOperand(0));
  assert(VT == V.getValueType() && "Malformed vector.reverse!");

  if (VT.isScalableVector()) {
    setValue(&I, DAG.getNode(ISD::VECTOR_REVERSE, DL, VT, V));
    return;
  }

  // Use VECTOR_SHUFFLE for the fixed-length vector
  // to maintain existing behavior.
  SmallVector<int, 8> Mask;
  unsigned NumElts = VT.getVectorMinNumElements();
  for (unsigned i = 0; i != NumElts; ++i)
    Mask.push_back(NumElts - 1 - i);

  setValue(&I, DAG.getVectorShuffle(VT, DL, V, DAG.getUNDEF(VT), Mask));
}

void SelectionDAGBuilder::visitVectorDeinterleave(const CallInst &I) {
  auto DL = getCurSDLoc();
  SDValue InVec = getValue(I.getOperand(0));
  EVT OutVT =
      InVec.getValueType().getHalfNumVectorElementsVT(*DAG.getContext());

  unsigned OutNumElts = OutVT.getVectorMinNumElements();

  // ISD Node needs the input vectors split into two equal parts
  SDValue Lo = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, OutVT, InVec,
                           DAG.getVectorIdxConstant(0, DL));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, OutVT, InVec,
                           DAG.getVectorIdxConstant(OutNumElts, DL));

  // Use VECTOR_SHUFFLE for fixed-length vectors to benefit from existing
  // legalisation and combines.
  if (OutVT.isFixedLengthVector()) {
    SDValue Even = DAG.getVectorShuffle(OutVT, DL, Lo, Hi,
                                        createStrideMask(0, 2, OutNumElts));
    SDValue Odd = DAG.getVectorShuffle(OutVT, DL, Lo, Hi,
                                       createStrideMask(1, 2, OutNumElts));
    SDValue Res = DAG.getMergeValues({Even, Odd}, getCurSDLoc());
    setValue(&I, Res);
    return;
  }

  SDValue Res = DAG.getNode(ISD::VECTOR_DEINTERLEAVE, DL,
                            DAG.getVTList(OutVT, OutVT), Lo, Hi);
  setValue(&I, Res);
}

void SelectionDAGBuilder::visitVectorInterleave(const CallInst &I) {
  auto DL = getCurSDLoc();
  EVT InVT = getValue(I.getOperand(0)).getValueType();
  SDValue InVec0 = getValue(I.getOperand(0));
  SDValue InVec1 = getValue(I.getOperand(1));
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT OutVT = TLI.getValueType(DAG.getDataLayout(), I.getType());

  // Use VECTOR_SHUFFLE for fixed-length vectors to benefit from existing
  // legalisation and combines.
  if (OutVT.isFixedLengthVector()) {
    unsigned NumElts = InVT.getVectorMinNumElements();
    SDValue V = DAG.getNode(ISD::CONCAT_VECTORS, DL, OutVT, InVec0, InVec1);
    setValue(&I, DAG.getVectorShuffle(OutVT, DL, V, DAG.getUNDEF(OutVT),
                                      createInterleaveMask(NumElts, 2)));
    return;
  }

  SDValue Res = DAG.getNode(ISD::VECTOR_INTERLEAVE, DL,
                            DAG.getVTList(InVT, InVT), InVec0, InVec1);
  Res = DAG.getNode(ISD::CONCAT_VECTORS, DL, OutVT, Res.getValue(0),
                    Res.getValue(1));
  setValue(&I, Res);
}

void SelectionDAGBuilder::visitFreeze(const FreezeInst &I) {
  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(), I.getType(),
                  ValueVTs);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0) return;

  SmallVector<SDValue, 4> Values(NumValues);
  SDValue Op = getValue(I.getOperand(0));

  for (unsigned i = 0; i != NumValues; ++i)
    Values[i] = DAG.getNode(ISD::FREEZE, getCurSDLoc(), ValueVTs[i],
                            SDValue(Op.getNode(), Op.getResNo() + i));

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                           DAG.getVTList(ValueVTs), Values));
}

void SelectionDAGBuilder::visitVectorSplice(const CallInst &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());

  SDLoc DL = getCurSDLoc();
  SDValue V1 = getValue(I.getOperand(0));
  SDValue V2 = getValue(I.getOperand(1));
  int64_t Imm = cast<ConstantInt>(I.getOperand(2))->getSExtValue();

  // VECTOR_SHUFFLE doesn't support a scalable mask so use a dedicated node.
  if (VT.isScalableVector()) {
    setValue(&I, DAG.getNode(ISD::VECTOR_SPLICE, DL, VT, V1, V2,
                             DAG.getVectorIdxConstant(Imm, DL)));
    return;
  }

  unsigned NumElts = VT.getVectorNumElements();

  uint64_t Idx = (NumElts + Imm) % NumElts;

  // Use VECTOR_SHUFFLE to maintain original behaviour for fixed-length vectors.
  SmallVector<int, 8> Mask;
  for (unsigned i = 0; i < NumElts; ++i)
    Mask.push_back(Idx + i);
  setValue(&I, DAG.getVectorShuffle(VT, DL, V1, V2, Mask));
}

// Consider the following MIR after SelectionDAG, which produces output in
// phyregs in the first case or virtregs in the second case.
//
// INLINEASM_BR ..., implicit-def $ebx, ..., implicit-def $edx
// %5:gr32 = COPY $ebx
// %6:gr32 = COPY $edx
// %1:gr32 = COPY %6:gr32
// %0:gr32 = COPY %5:gr32
//
// INLINEASM_BR ..., def %5:gr32, ..., def %6:gr32
// %1:gr32 = COPY %6:gr32
// %0:gr32 = COPY %5:gr32
//
// Given %0, we'd like to return $ebx in the first case and %5 in the second.
// Given %1, we'd like to return $edx in the first case and %6 in the second.
//
// If a callbr has outputs, it will have a single mapping in FuncInfo.ValueMap
// to a single virtreg (such as %0). The remaining outputs monotonically
// increase in virtreg number from there. If a callbr has no outputs, then it
// should not have a corresponding callbr landingpad; in fact, the callbr
// landingpad would not even be able to refer to such a callbr.
static Register FollowCopyChain(MachineRegisterInfo &MRI, Register Reg) {
  MachineInstr *MI = MRI.def_begin(Reg)->getParent();
  // There is definitely at least one copy.
  assert(MI->getOpcode() == TargetOpcode::COPY &&
         "start of copy chain MUST be COPY");
  Reg = MI->getOperand(1).getReg();
  MI = MRI.def_begin(Reg)->getParent();
  // There may be an optional second copy.
  if (MI->getOpcode() == TargetOpcode::COPY) {
    assert(Reg.isVirtual() && "expected COPY of virtual register");
    Reg = MI->getOperand(1).getReg();
    assert(Reg.isPhysical() && "expected COPY of physical register");
    MI = MRI.def_begin(Reg)->getParent();
  }
  // The start of the chain must be an INLINEASM_BR.
  assert(MI->getOpcode() == TargetOpcode::INLINEASM_BR &&
         "end of copy chain MUST be INLINEASM_BR");
  return Reg;
}

// We must do this walk rather than the simpler
//   setValue(&I, getCopyFromRegs(CBR, CBR->getType()));
// otherwise we will end up with copies of virtregs only valid along direct
// edges.
void SelectionDAGBuilder::visitCallBrLandingPad(const CallInst &I) {
  SmallVector<EVT, 8> ResultVTs;
  SmallVector<SDValue, 8> ResultValues;
  const auto *CBR =
      cast<CallBrInst>(I.getParent()->getUniquePredecessor()->getTerminator());

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const TargetRegisterInfo *TRI = DAG.getSubtarget().getRegisterInfo();
  MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();

  unsigned InitialDef = FuncInfo.ValueMap[CBR];
  SDValue Chain = DAG.getRoot();

  // Re-parse the asm constraints string.
  TargetLowering::AsmOperandInfoVector TargetConstraints =
      TLI.ParseConstraints(DAG.getDataLayout(), TRI, *CBR);
  for (auto &T : TargetConstraints) {
    SDISelAsmOperandInfo OpInfo(T);
    if (OpInfo.Type != InlineAsm::isOutput)
      continue;

    // Pencil in OpInfo.ConstraintType and OpInfo.ConstraintVT based on the
    // individual constraint.
    TLI.ComputeConstraintToUse(OpInfo, OpInfo.CallOperand, &DAG);

    switch (OpInfo.ConstraintType) {
    case TargetLowering::C_Register:
    case TargetLowering::C_RegisterClass: {
      // Fill in OpInfo.AssignedRegs.Regs.
      getRegistersForValue(DAG, getCurSDLoc(), OpInfo, OpInfo);

      // getRegistersForValue may produce 1 to many registers based on whether
      // the OpInfo.ConstraintVT is legal on the target or not.
      for (unsigned &Reg : OpInfo.AssignedRegs.Regs) {
        Register OriginalDef = FollowCopyChain(MRI, InitialDef++);
        if (Register::isPhysicalRegister(OriginalDef))
          FuncInfo.MBB->addLiveIn(OriginalDef);
        // Update the assigned registers to use the original defs.
        Reg = OriginalDef;
      }

      SDValue V = OpInfo.AssignedRegs.getCopyFromRegs(
          DAG, FuncInfo, getCurSDLoc(), Chain, nullptr, CBR);
      ResultValues.push_back(V);
      ResultVTs.push_back(OpInfo.ConstraintVT);
      break;
    }
    case TargetLowering::C_Other: {
      SDValue Flag;
      SDValue V = TLI.LowerAsmOutputForConstraint(Chain, Flag, getCurSDLoc(),
                                                  OpInfo, DAG);
      ++InitialDef;
      ResultValues.push_back(V);
      ResultVTs.push_back(OpInfo.ConstraintVT);
      break;
    }
    default:
      break;
    }
  }
  SDValue V = DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                          DAG.getVTList(ResultVTs), ResultValues);
  setValue(&I, V);
}
