//===-- RISCVISelLowering.cpp - RISC-V DAG Lowering Implementation  -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that RISC-V uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "RISCVISelLowering.h"
#include "MCTargetDesc/RISCVMatInt.h"
#include "RISCV.h"
#include "RISCVMachineFunctionInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "RISCVTargetMachine.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGAddressAnalysis.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsRISCV.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "riscv-lower"

STATISTIC(NumTailCalls, "Number of tail calls");

static cl::opt<unsigned> ExtensionMaxWebSize(
    DEBUG_TYPE "-ext-max-web-size", cl::Hidden,
    cl::desc("Give the maximum size (in number of nodes) of the web of "
             "instructions that we will consider for VW expansion"),
    cl::init(18));

static cl::opt<bool>
    AllowSplatInVW_W(DEBUG_TYPE "-form-vw-w-with-splat", cl::Hidden,
                     cl::desc("Allow the formation of VW_W operations (e.g., "
                              "VWADD_W) with splat constants"),
                     cl::init(false));

static cl::opt<unsigned> NumRepeatedDivisors(
    DEBUG_TYPE "-fp-repeated-divisors", cl::Hidden,
    cl::desc("Set the minimum number of repetitions of a divisor to allow "
             "transformation to multiplications by the reciprocal"),
    cl::init(2));

static cl::opt<int>
    FPImmCost(DEBUG_TYPE "-fpimm-cost", cl::Hidden,
              cl::desc("Give the maximum number of instructions that we will "
                       "use for creating a floating-point immediate value"),
              cl::init(2));

static cl::opt<bool>
    RV64LegalI32("riscv-experimental-rv64-legal-i32", cl::ReallyHidden,
                 cl::desc("Make i32 a legal type for SelectionDAG on RV64."));

RISCVTargetLowering::RISCVTargetLowering(const TargetMachine &TM,
                                         const RISCVSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {

  RISCVABI::ABI ABI = Subtarget.getTargetABI();
  assert(ABI != RISCVABI::ABI_Unknown && "Improperly initialised target ABI");

  if ((ABI == RISCVABI::ABI_ILP32F || ABI == RISCVABI::ABI_LP64F) &&
      !Subtarget.hasStdExtF()) {
    errs() << "Hard-float 'f' ABI can't be used for a target that "
                "doesn't support the F instruction set extension (ignoring "
                          "target-abi)\n";
    ABI = Subtarget.is64Bit() ? RISCVABI::ABI_LP64 : RISCVABI::ABI_ILP32;
  } else if ((ABI == RISCVABI::ABI_ILP32D || ABI == RISCVABI::ABI_LP64D) &&
             !Subtarget.hasStdExtD()) {
    errs() << "Hard-float 'd' ABI can't be used for a target that "
              "doesn't support the D instruction set extension (ignoring "
              "target-abi)\n";
    ABI = Subtarget.is64Bit() ? RISCVABI::ABI_LP64 : RISCVABI::ABI_ILP32;
  }

  switch (ABI) {
  default:
    report_fatal_error("Don't know how to lower this ABI");
  case RISCVABI::ABI_ILP32:
  case RISCVABI::ABI_ILP32E:
  case RISCVABI::ABI_LP64E:
  case RISCVABI::ABI_ILP32F:
  case RISCVABI::ABI_ILP32D:
  case RISCVABI::ABI_LP64:
  case RISCVABI::ABI_LP64F:
  case RISCVABI::ABI_LP64D:
    break;
  }

  MVT XLenVT = Subtarget.getXLenVT();

  // Set up the register classes.
  addRegisterClass(XLenVT, &RISCV::GPRRegClass);
  if (Subtarget.is64Bit() && RV64LegalI32)
    addRegisterClass(MVT::i32, &RISCV::GPRRegClass);

  if (Subtarget.hasStdExtZfhmin())
    addRegisterClass(MVT::f16, &RISCV::FPR16RegClass);
  if (Subtarget.hasStdExtZfbfmin())
    addRegisterClass(MVT::bf16, &RISCV::FPR16RegClass);
  if (Subtarget.hasStdExtF())
    addRegisterClass(MVT::f32, &RISCV::FPR32RegClass);
  if (Subtarget.hasStdExtD())
    addRegisterClass(MVT::f64, &RISCV::FPR64RegClass);
  if (Subtarget.hasStdExtZhinxmin())
    addRegisterClass(MVT::f16, &RISCV::GPRF16RegClass);
  if (Subtarget.hasStdExtZfinx())
    addRegisterClass(MVT::f32, &RISCV::GPRF32RegClass);
  if (Subtarget.hasStdExtZdinx()) {
    if (Subtarget.is64Bit())
      addRegisterClass(MVT::f64, &RISCV::GPRRegClass);
    else
      addRegisterClass(MVT::f64, &RISCV::GPRPairRegClass);
  }

  static const MVT::SimpleValueType BoolVecVTs[] = {
      MVT::nxv1i1,  MVT::nxv2i1,  MVT::nxv4i1, MVT::nxv8i1,
      MVT::nxv16i1, MVT::nxv32i1, MVT::nxv64i1};
  static const MVT::SimpleValueType IntVecVTs[] = {
      MVT::nxv1i8,  MVT::nxv2i8,   MVT::nxv4i8,   MVT::nxv8i8,  MVT::nxv16i8,
      MVT::nxv32i8, MVT::nxv64i8,  MVT::nxv1i16,  MVT::nxv2i16, MVT::nxv4i16,
      MVT::nxv8i16, MVT::nxv16i16, MVT::nxv32i16, MVT::nxv1i32, MVT::nxv2i32,
      MVT::nxv4i32, MVT::nxv8i32,  MVT::nxv16i32, MVT::nxv1i64, MVT::nxv2i64,
      MVT::nxv4i64, MVT::nxv8i64};
  static const MVT::SimpleValueType F16VecVTs[] = {
      MVT::nxv1f16, MVT::nxv2f16,  MVT::nxv4f16,
      MVT::nxv8f16, MVT::nxv16f16, MVT::nxv32f16};
  static const MVT::SimpleValueType BF16VecVTs[] = {
      MVT::nxv1bf16, MVT::nxv2bf16,  MVT::nxv4bf16,
      MVT::nxv8bf16, MVT::nxv16bf16, MVT::nxv32bf16};
  static const MVT::SimpleValueType F32VecVTs[] = {
      MVT::nxv1f32, MVT::nxv2f32, MVT::nxv4f32, MVT::nxv8f32, MVT::nxv16f32};
  static const MVT::SimpleValueType F64VecVTs[] = {
      MVT::nxv1f64, MVT::nxv2f64, MVT::nxv4f64, MVT::nxv8f64};

  if (Subtarget.hasVInstructions()) {
    auto addRegClassForRVV = [this](MVT VT) {
      // Disable the smallest fractional LMUL types if ELEN is less than
      // RVVBitsPerBlock.
      unsigned MinElts = RISCV::RVVBitsPerBlock / Subtarget.getELen();
      if (VT.getVectorMinNumElements() < MinElts)
        return;

      unsigned Size = VT.getSizeInBits().getKnownMinValue();
      const TargetRegisterClass *RC;
      if (Size <= RISCV::RVVBitsPerBlock)
        RC = &RISCV::VRRegClass;
      else if (Size == 2 * RISCV::RVVBitsPerBlock)
        RC = &RISCV::VRM2RegClass;
      else if (Size == 4 * RISCV::RVVBitsPerBlock)
        RC = &RISCV::VRM4RegClass;
      else if (Size == 8 * RISCV::RVVBitsPerBlock)
        RC = &RISCV::VRM8RegClass;
      else
        llvm_unreachable("Unexpected size");

      addRegisterClass(VT, RC);
    };

    for (MVT VT : BoolVecVTs)
      addRegClassForRVV(VT);
    for (MVT VT : IntVecVTs) {
      if (VT.getVectorElementType() == MVT::i64 &&
          !Subtarget.hasVInstructionsI64())
        continue;
      addRegClassForRVV(VT);
    }

    if (Subtarget.hasVInstructionsF16Minimal())
      for (MVT VT : F16VecVTs)
        addRegClassForRVV(VT);

    if (Subtarget.hasVInstructionsBF16())
      for (MVT VT : BF16VecVTs)
        addRegClassForRVV(VT);

    if (Subtarget.hasVInstructionsF32())
      for (MVT VT : F32VecVTs)
        addRegClassForRVV(VT);

    if (Subtarget.hasVInstructionsF64())
      for (MVT VT : F64VecVTs)
        addRegClassForRVV(VT);

    if (Subtarget.useRVVForFixedLengthVectors()) {
      auto addRegClassForFixedVectors = [this](MVT VT) {
        MVT ContainerVT = getContainerForFixedLengthVector(VT);
        unsigned RCID = getRegClassIDForVecVT(ContainerVT);
        const RISCVRegisterInfo &TRI = *Subtarget.getRegisterInfo();
        addRegisterClass(VT, TRI.getRegClass(RCID));
      };
      for (MVT VT : MVT::integer_fixedlen_vector_valuetypes())
        if (useRVVForFixedLengthVectorVT(VT))
          addRegClassForFixedVectors(VT);

      for (MVT VT : MVT::fp_fixedlen_vector_valuetypes())
        if (useRVVForFixedLengthVectorVT(VT))
          addRegClassForFixedVectors(VT);
    }
  }

  // Compute derived properties from the register classes.
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(RISCV::X2);

  setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, XLenVT,
                   MVT::i1, Promote);
  // DAGCombiner can call isLoadExtLegal for types that aren't legal.
  setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, MVT::i32,
                   MVT::i1, Promote);

  // TODO: add all necessary setOperationAction calls.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, XLenVT, Expand);

  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BR_CC, XLenVT, Expand);
  if (RV64LegalI32 && Subtarget.is64Bit())
    setOperationAction(ISD::BR_CC, MVT::i32, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  setOperationAction(ISD::SELECT_CC, XLenVT, Expand);
  if (RV64LegalI32 && Subtarget.is64Bit())
    setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);

  if (!Subtarget.hasVendorXCValu())
    setCondCodeAction(ISD::SETLE, XLenVT, Expand);
  setCondCodeAction(ISD::SETGT, XLenVT, Custom);
  setCondCodeAction(ISD::SETGE, XLenVT, Expand);
  if (!Subtarget.hasVendorXCValu())
    setCondCodeAction(ISD::SETULE, XLenVT, Expand);
  setCondCodeAction(ISD::SETUGT, XLenVT, Custom);
  setCondCodeAction(ISD::SETUGE, XLenVT, Expand);

  if (RV64LegalI32 && Subtarget.is64Bit())
    setOperationAction(ISD::SETCC, MVT::i32, Promote);

  setOperationAction({ISD::STACKSAVE, ISD::STACKRESTORE}, MVT::Other, Expand);

  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction({ISD::VAARG, ISD::VACOPY, ISD::VAEND}, MVT::Other, Expand);
  if (RV64LegalI32 && Subtarget.is64Bit())
    setOperationAction(ISD::VAARG, MVT::i32, Promote);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::EH_DWARF_CFA, MVT::i32, Custom);

  if (!Subtarget.hasStdExtZbb() && !Subtarget.hasVendorXTHeadBb())
    setOperationAction(ISD::SIGN_EXTEND_INREG, {MVT::i8, MVT::i16}, Expand);

  if (Subtarget.is64Bit()) {
    setOperationAction(ISD::EH_DWARF_CFA, MVT::i64, Custom);

    if (!RV64LegalI32) {
      setOperationAction(ISD::LOAD, MVT::i32, Custom);
      setOperationAction({ISD::ADD, ISD::SUB, ISD::SHL, ISD::SRA, ISD::SRL},
                         MVT::i32, Custom);
      setOperationAction({ISD::UADDO, ISD::USUBO, ISD::UADDSAT, ISD::USUBSAT},
                         MVT::i32, Custom);
      if (!Subtarget.hasStdExtZbb())
        setOperationAction({ISD::SADDSAT, ISD::SSUBSAT}, MVT::i32, Custom);
    } else {
      setOperationAction(ISD::SSUBO, MVT::i32, Custom);
      if (Subtarget.hasStdExtZbb()) {
        setOperationAction({ISD::SADDSAT, ISD::SSUBSAT}, MVT::i32, Custom);
        setOperationAction({ISD::UADDSAT, ISD::USUBSAT}, MVT::i32, Custom);
      }
    }
    setOperationAction(ISD::SADDO, MVT::i32, Custom);
  }
  if (!Subtarget.hasStdExtZmmul()) {
    setOperationAction({ISD::MUL, ISD::MULHS, ISD::MULHU}, XLenVT, Expand);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction(ISD::MUL, MVT::i32, Promote);
  } else if (Subtarget.is64Bit()) {
    setOperationAction(ISD::MUL, MVT::i128, Custom);
    if (!RV64LegalI32)
      setOperationAction(ISD::MUL, MVT::i32, Custom);
    else
      setOperationAction(ISD::SMULO, MVT::i32, Custom);
  } else {
    setOperationAction(ISD::MUL, MVT::i64, Custom);
  }

  if (!Subtarget.hasStdExtM()) {
    setOperationAction({ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM},
                       XLenVT, Expand);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction({ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM}, MVT::i32,
                         Promote);
  } else if (Subtarget.is64Bit()) {
    if (!RV64LegalI32)
      setOperationAction({ISD::SDIV, ISD::UDIV, ISD::UREM},
                         {MVT::i8, MVT::i16, MVT::i32}, Custom);
  }

  if (RV64LegalI32 && Subtarget.is64Bit()) {
    setOperationAction({ISD::MULHS, ISD::MULHU}, MVT::i32, Expand);
    setOperationAction(
        {ISD::SDIVREM, ISD::UDIVREM, ISD::SMUL_LOHI, ISD::UMUL_LOHI}, MVT::i32,
        Expand);
  }

  setOperationAction(
      {ISD::SDIVREM, ISD::UDIVREM, ISD::SMUL_LOHI, ISD::UMUL_LOHI}, XLenVT,
      Expand);

  setOperationAction({ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS}, XLenVT,
                     Custom);

  if (Subtarget.hasStdExtZbb() || Subtarget.hasStdExtZbkb()) {
    if (!RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction({ISD::ROTL, ISD::ROTR}, MVT::i32, Custom);
  } else if (Subtarget.hasVendorXTHeadBb()) {
    if (Subtarget.is64Bit())
      setOperationAction({ISD::ROTL, ISD::ROTR}, MVT::i32, Custom);
    setOperationAction({ISD::ROTL, ISD::ROTR}, XLenVT, Custom);
  } else if (Subtarget.hasVendorXCVbitmanip()) {
    setOperationAction(ISD::ROTL, XLenVT, Expand);
  } else {
    setOperationAction({ISD::ROTL, ISD::ROTR}, XLenVT, Expand);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction({ISD::ROTL, ISD::ROTR}, MVT::i32, Expand);
  }

  // With Zbb we have an XLen rev8 instruction, but not GREVI. So we'll
  // pattern match it directly in isel.
  setOperationAction(ISD::BSWAP, XLenVT,
                     (Subtarget.hasStdExtZbb() || Subtarget.hasStdExtZbkb() ||
                      Subtarget.hasVendorXTHeadBb())
                         ? Legal
                         : Expand);
  if (RV64LegalI32 && Subtarget.is64Bit())
    setOperationAction(ISD::BSWAP, MVT::i32,
                       (Subtarget.hasStdExtZbb() || Subtarget.hasStdExtZbkb() ||
                        Subtarget.hasVendorXTHeadBb())
                           ? Promote
                           : Expand);


  if (Subtarget.hasVendorXCVbitmanip()) {
    setOperationAction(ISD::BITREVERSE, XLenVT, Legal);
  } else {
    // Zbkb can use rev8+brev8 to implement bitreverse.
    setOperationAction(ISD::BITREVERSE, XLenVT,
                       Subtarget.hasStdExtZbkb() ? Custom : Expand);
  }

  if (Subtarget.hasStdExtZbb()) {
    setOperationAction({ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX}, XLenVT,
                       Legal);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction({ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX}, MVT::i32,
                         Promote);

    if (Subtarget.is64Bit()) {
      if (RV64LegalI32)
        setOperationAction(ISD::CTTZ, MVT::i32, Legal);
      else
        setOperationAction({ISD::CTTZ, ISD::CTTZ_ZERO_UNDEF}, MVT::i32, Custom);
    }
  } else if (!Subtarget.hasVendorXCVbitmanip()) {
    setOperationAction({ISD::CTTZ, ISD::CTPOP}, XLenVT, Expand);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction({ISD::CTTZ, ISD::CTPOP}, MVT::i32, Expand);
  }

  if (Subtarget.hasStdExtZbb() || Subtarget.hasVendorXTHeadBb() ||
      Subtarget.hasVendorXCVbitmanip()) {
    // We need the custom lowering to make sure that the resulting sequence
    // for the 32bit case is efficient on 64bit targets.
    if (Subtarget.is64Bit()) {
      if (RV64LegalI32) {
        setOperationAction(ISD::CTLZ, MVT::i32,
                           Subtarget.hasStdExtZbb() ? Legal : Promote);
        if (!Subtarget.hasStdExtZbb())
          setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, Promote);
      } else
        setOperationAction({ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF}, MVT::i32, Custom);
    }
  } else {
    setOperationAction(ISD::CTLZ, XLenVT, Expand);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction(ISD::CTLZ, MVT::i32, Expand);
  }

  if (!RV64LegalI32 && Subtarget.is64Bit() &&
      !Subtarget.hasShortForwardBranchOpt())
    setOperationAction(ISD::ABS, MVT::i32, Custom);

  // We can use PseudoCCSUB to implement ABS.
  if (Subtarget.hasShortForwardBranchOpt())
    setOperationAction(ISD::ABS, XLenVT, Legal);

  if (!Subtarget.hasVendorXTHeadCondMov()) {
    setOperationAction(ISD::SELECT, XLenVT, Custom);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction(ISD::SELECT, MVT::i32, Promote);
  }

  static const unsigned FPLegalNodeTypes[] = {
      ISD::FMINNUM,        ISD::FMAXNUM,       ISD::LRINT,
      ISD::LLRINT,         ISD::LROUND,        ISD::LLROUND,
      ISD::STRICT_LRINT,   ISD::STRICT_LLRINT, ISD::STRICT_LROUND,
      ISD::STRICT_LLROUND, ISD::STRICT_FMA,    ISD::STRICT_FADD,
      ISD::STRICT_FSUB,    ISD::STRICT_FMUL,   ISD::STRICT_FDIV,
      ISD::STRICT_FSQRT,   ISD::STRICT_FSETCC, ISD::STRICT_FSETCCS};

  static const ISD::CondCode FPCCToExpand[] = {
      ISD::SETOGT, ISD::SETOGE, ISD::SETONE, ISD::SETUEQ, ISD::SETUGT,
      ISD::SETUGE, ISD::SETULT, ISD::SETULE, ISD::SETUNE, ISD::SETGT,
      ISD::SETGE,  ISD::SETNE,  ISD::SETO,   ISD::SETUO};

  static const unsigned FPOpToExpand[] = {
      ISD::FSIN, ISD::FCOS,       ISD::FSINCOS,   ISD::FPOW,
      ISD::FREM};

  static const unsigned FPRndMode[] = {
      ISD::FCEIL, ISD::FFLOOR, ISD::FTRUNC, ISD::FRINT, ISD::FROUND,
      ISD::FROUNDEVEN};

  if (Subtarget.hasStdExtZfhminOrZhinxmin())
    setOperationAction(ISD::BITCAST, MVT::i16, Custom);

  static const unsigned ZfhminZfbfminPromoteOps[] = {
      ISD::FMINNUM,      ISD::FMAXNUM,       ISD::FADD,
      ISD::FSUB,         ISD::FMUL,          ISD::FMA,
      ISD::FDIV,         ISD::FSQRT,         ISD::FABS,
      ISD::FNEG,         ISD::STRICT_FMA,    ISD::STRICT_FADD,
      ISD::STRICT_FSUB,  ISD::STRICT_FMUL,   ISD::STRICT_FDIV,
      ISD::STRICT_FSQRT, ISD::STRICT_FSETCC, ISD::STRICT_FSETCCS,
      ISD::SETCC,        ISD::FCEIL,         ISD::FFLOOR,
      ISD::FTRUNC,       ISD::FRINT,         ISD::FROUND,
      ISD::FROUNDEVEN,   ISD::SELECT};

  if (Subtarget.hasStdExtZfbfmin()) {
    setOperationAction(ISD::BITCAST, MVT::i16, Custom);
    setOperationAction(ISD::BITCAST, MVT::bf16, Custom);
    setOperationAction(ISD::FP_ROUND, MVT::bf16, Custom);
    setOperationAction(ISD::FP_EXTEND, MVT::f32, Custom);
    setOperationAction(ISD::FP_EXTEND, MVT::f64, Custom);
    setOperationAction(ISD::ConstantFP, MVT::bf16, Expand);
    setOperationAction(ISD::SELECT_CC, MVT::bf16, Expand);
    setOperationAction(ISD::BR_CC, MVT::bf16, Expand);
    setOperationAction(ZfhminZfbfminPromoteOps, MVT::bf16, Promote);
    setOperationAction(ISD::FREM, MVT::bf16, Promote);
    // FIXME: Need to promote bf16 FCOPYSIGN to f32, but the
    // DAGCombiner::visitFP_ROUND probably needs improvements first.
    setOperationAction(ISD::FCOPYSIGN, MVT::bf16, Expand);
  }

  if (Subtarget.hasStdExtZfhminOrZhinxmin()) {
    if (Subtarget.hasStdExtZfhOrZhinx()) {
      setOperationAction(FPLegalNodeTypes, MVT::f16, Legal);
      setOperationAction(FPRndMode, MVT::f16,
                         Subtarget.hasStdExtZfa() ? Legal : Custom);
      setOperationAction(ISD::SELECT, MVT::f16, Custom);
      setOperationAction(ISD::IS_FPCLASS, MVT::f16, Custom);
    } else {
      setOperationAction(ZfhminZfbfminPromoteOps, MVT::f16, Promote);
      setOperationAction({ISD::STRICT_LRINT, ISD::STRICT_LLRINT,
                          ISD::STRICT_LROUND, ISD::STRICT_LLROUND},
                         MVT::f16, Legal);
      // FIXME: Need to promote f16 FCOPYSIGN to f32, but the
      // DAGCombiner::visitFP_ROUND probably needs improvements first.
      setOperationAction(ISD::FCOPYSIGN, MVT::f16, Expand);
    }

    setOperationAction(ISD::STRICT_FP_ROUND, MVT::f16, Legal);
    setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f32, Legal);
    setCondCodeAction(FPCCToExpand, MVT::f16, Expand);
    setOperationAction(ISD::SELECT_CC, MVT::f16, Expand);
    setOperationAction(ISD::BR_CC, MVT::f16, Expand);

    setOperationAction(ISD::FNEARBYINT, MVT::f16,
                       Subtarget.hasStdExtZfa() ? Legal : Promote);
    setOperationAction({ISD::FREM, ISD::FPOW, ISD::FPOWI,
                        ISD::FCOS, ISD::FSIN, ISD::FSINCOS, ISD::FEXP,
                        ISD::FEXP2, ISD::FEXP10, ISD::FLOG, ISD::FLOG2,
                        ISD::FLOG10},
                       MVT::f16, Promote);

    // FIXME: Need to promote f16 STRICT_* to f32 libcalls, but we don't have
    // complete support for all operations in LegalizeDAG.
    setOperationAction({ISD::STRICT_FCEIL, ISD::STRICT_FFLOOR,
                        ISD::STRICT_FNEARBYINT, ISD::STRICT_FRINT,
                        ISD::STRICT_FROUND, ISD::STRICT_FROUNDEVEN,
                        ISD::STRICT_FTRUNC},
                       MVT::f16, Promote);

    // We need to custom promote this.
    if (Subtarget.is64Bit())
      setOperationAction(ISD::FPOWI, MVT::i32, Custom);

    setOperationAction({ISD::FMAXIMUM, ISD::FMINIMUM}, MVT::f16,
                       Subtarget.hasStdExtZfa() ? Legal : Custom);
  }

  if (Subtarget.hasStdExtFOrZfinx()) {
    setOperationAction(FPLegalNodeTypes, MVT::f32, Legal);
    setOperationAction(FPRndMode, MVT::f32,
                       Subtarget.hasStdExtZfa() ? Legal : Custom);
    setCondCodeAction(FPCCToExpand, MVT::f32, Expand);
    setOperationAction(ISD::SELECT_CC, MVT::f32, Expand);
    setOperationAction(ISD::SELECT, MVT::f32, Custom);
    setOperationAction(ISD::BR_CC, MVT::f32, Expand);
    setOperationAction(FPOpToExpand, MVT::f32, Expand);
    setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Expand);
    setTruncStoreAction(MVT::f32, MVT::f16, Expand);
    setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::bf16, Expand);
    setTruncStoreAction(MVT::f32, MVT::bf16, Expand);
    setOperationAction(ISD::IS_FPCLASS, MVT::f32, Custom);
    setOperationAction(ISD::BF16_TO_FP, MVT::f32, Custom);
    setOperationAction(ISD::FP_TO_BF16, MVT::f32,
                       Subtarget.isSoftFPABI() ? LibCall : Custom);
    setOperationAction(ISD::FP_TO_FP16, MVT::f32, Custom);
    setOperationAction(ISD::FP16_TO_FP, MVT::f32, Custom);

    if (Subtarget.hasStdExtZfa()) {
      setOperationAction(ISD::FNEARBYINT, MVT::f32, Legal);
      setOperationAction({ISD::FMAXIMUM, ISD::FMINIMUM}, MVT::f32, Legal);
    } else {
      setOperationAction({ISD::FMAXIMUM, ISD::FMINIMUM}, MVT::f32, Custom);
    }
  }

  if (Subtarget.hasStdExtFOrZfinx() && Subtarget.is64Bit())
    setOperationAction(ISD::BITCAST, MVT::i32, Custom);

  if (Subtarget.hasStdExtDOrZdinx()) {
    setOperationAction(FPLegalNodeTypes, MVT::f64, Legal);

    if (!Subtarget.is64Bit())
      setOperationAction(ISD::BITCAST, MVT::i64, Custom);

    if (Subtarget.hasStdExtZfa()) {
      setOperationAction(FPRndMode, MVT::f64, Legal);
      setOperationAction(ISD::FNEARBYINT, MVT::f64, Legal);
      setOperationAction({ISD::FMAXIMUM, ISD::FMINIMUM}, MVT::f64, Legal);
    } else {
      if (Subtarget.is64Bit())
        setOperationAction(FPRndMode, MVT::f64, Custom);

      setOperationAction({ISD::FMAXIMUM, ISD::FMINIMUM}, MVT::f64, Custom);
    }

    setOperationAction(ISD::STRICT_FP_ROUND, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f64, Legal);
    setCondCodeAction(FPCCToExpand, MVT::f64, Expand);
    setOperationAction(ISD::SELECT_CC, MVT::f64, Expand);
    setOperationAction(ISD::SELECT, MVT::f64, Custom);
    setOperationAction(ISD::BR_CC, MVT::f64, Expand);
    setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
    setTruncStoreAction(MVT::f64, MVT::f32, Expand);
    setOperationAction(FPOpToExpand, MVT::f64, Expand);
    setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Expand);
    setTruncStoreAction(MVT::f64, MVT::f16, Expand);
    setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::bf16, Expand);
    setTruncStoreAction(MVT::f64, MVT::bf16, Expand);
    setOperationAction(ISD::IS_FPCLASS, MVT::f64, Custom);
    setOperationAction(ISD::BF16_TO_FP, MVT::f64, Custom);
    setOperationAction(ISD::FP_TO_BF16, MVT::f64,
                       Subtarget.isSoftFPABI() ? LibCall : Custom);
    setOperationAction(ISD::FP_TO_FP16, MVT::f64, Custom);
    setOperationAction(ISD::FP16_TO_FP, MVT::f64, Expand);
  }

  if (Subtarget.is64Bit()) {
    setOperationAction({ISD::FP_TO_UINT, ISD::FP_TO_SINT,
                        ISD::STRICT_FP_TO_UINT, ISD::STRICT_FP_TO_SINT},
                       MVT::i32, Custom);
    setOperationAction(ISD::LROUND, MVT::i32, Custom);
  }

  if (Subtarget.hasStdExtFOrZfinx()) {
    setOperationAction({ISD::FP_TO_UINT_SAT, ISD::FP_TO_SINT_SAT}, XLenVT,
                       Custom);

    setOperationAction({ISD::STRICT_FP_TO_UINT, ISD::STRICT_FP_TO_SINT,
                        ISD::STRICT_UINT_TO_FP, ISD::STRICT_SINT_TO_FP},
                       XLenVT, Legal);

    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction({ISD::STRICT_FP_TO_UINT, ISD::STRICT_FP_TO_SINT,
                          ISD::STRICT_UINT_TO_FP, ISD::STRICT_SINT_TO_FP},
                         MVT::i32, Legal);

    setOperationAction(ISD::GET_ROUNDING, XLenVT, Custom);
    setOperationAction(ISD::SET_ROUNDING, MVT::Other, Custom);
  }

  setOperationAction({ISD::GlobalAddress, ISD::BlockAddress, ISD::ConstantPool,
                      ISD::JumpTable},
                     XLenVT, Custom);

  setOperationAction(ISD::GlobalTLSAddress, XLenVT, Custom);

  if (Subtarget.is64Bit())
    setOperationAction(ISD::Constant, MVT::i64, Custom);

  // TODO: On M-mode only targets, the cycle[h]/time[h] CSR may not be present.
  // Unfortunately this can't be determined just from the ISA naming string.
  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64,
                     Subtarget.is64Bit() ? Legal : Custom);
  setOperationAction(ISD::READSTEADYCOUNTER, MVT::i64,
                     Subtarget.is64Bit() ? Legal : Custom);

  setOperationAction({ISD::TRAP, ISD::DEBUGTRAP}, MVT::Other, Legal);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);
  if (Subtarget.is64Bit())
    setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::i32, Custom);

  if (Subtarget.hasStdExtZicbop()) {
    setOperationAction(ISD::PREFETCH, MVT::Other, Legal);
  }

  if (Subtarget.hasStdExtA()) {
    setMaxAtomicSizeInBitsSupported(Subtarget.getXLen());
    if (Subtarget.hasStdExtZabha() && Subtarget.hasStdExtZacas())
      setMinCmpXchgSizeInBits(8);
    else
      setMinCmpXchgSizeInBits(32);
  } else if (Subtarget.hasForcedAtomics()) {
    setMaxAtomicSizeInBitsSupported(Subtarget.getXLen());
  } else {
    setMaxAtomicSizeInBitsSupported(0);
  }

  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);

  setBooleanContents(ZeroOrOneBooleanContent);

  if (getTargetMachine().getTargetTriple().isOSLinux()) {
    // Custom lowering of llvm.clear_cache.
    setOperationAction(ISD::CLEAR_CACHE, MVT::Other, Custom);
  }

  if (Subtarget.hasVInstructions()) {
    setBooleanVectorContents(ZeroOrOneBooleanContent);

    setOperationAction(ISD::VSCALE, XLenVT, Custom);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction(ISD::VSCALE, MVT::i32, Custom);

    // RVV intrinsics may have illegal operands.
    // We also need to custom legalize vmv.x.s.
    setOperationAction({ISD::INTRINSIC_WO_CHAIN, ISD::INTRINSIC_W_CHAIN,
                        ISD::INTRINSIC_VOID},
                       {MVT::i8, MVT::i16}, Custom);
    if (Subtarget.is64Bit())
      setOperationAction({ISD::INTRINSIC_W_CHAIN, ISD::INTRINSIC_VOID},
                         MVT::i32, Custom);
    else
      setOperationAction({ISD::INTRINSIC_WO_CHAIN, ISD::INTRINSIC_W_CHAIN},
                         MVT::i64, Custom);

    setOperationAction({ISD::INTRINSIC_W_CHAIN, ISD::INTRINSIC_VOID},
                       MVT::Other, Custom);

    static const unsigned IntegerVPOps[] = {
        ISD::VP_ADD,         ISD::VP_SUB,         ISD::VP_MUL,
        ISD::VP_SDIV,        ISD::VP_UDIV,        ISD::VP_SREM,
        ISD::VP_UREM,        ISD::VP_AND,         ISD::VP_OR,
        ISD::VP_XOR,         ISD::VP_SRA,         ISD::VP_SRL,
        ISD::VP_SHL,         ISD::VP_REDUCE_ADD,  ISD::VP_REDUCE_AND,
        ISD::VP_REDUCE_OR,   ISD::VP_REDUCE_XOR,  ISD::VP_REDUCE_SMAX,
        ISD::VP_REDUCE_SMIN, ISD::VP_REDUCE_UMAX, ISD::VP_REDUCE_UMIN,
        ISD::VP_MERGE,       ISD::VP_SELECT,      ISD::VP_FP_TO_SINT,
        ISD::VP_FP_TO_UINT,  ISD::VP_SETCC,       ISD::VP_SIGN_EXTEND,
        ISD::VP_ZERO_EXTEND, ISD::VP_TRUNCATE,    ISD::VP_SMIN,
        ISD::VP_SMAX,        ISD::VP_UMIN,        ISD::VP_UMAX,
        ISD::VP_ABS, ISD::EXPERIMENTAL_VP_REVERSE, ISD::EXPERIMENTAL_VP_SPLICE,
        ISD::VP_SADDSAT,     ISD::VP_UADDSAT,     ISD::VP_SSUBSAT,
        ISD::VP_USUBSAT,     ISD::VP_CTTZ_ELTS,   ISD::VP_CTTZ_ELTS_ZERO_UNDEF,
        ISD::EXPERIMENTAL_VP_SPLAT};

    static const unsigned FloatingPointVPOps[] = {
        ISD::VP_FADD,        ISD::VP_FSUB,        ISD::VP_FMUL,
        ISD::VP_FDIV,        ISD::VP_FNEG,        ISD::VP_FABS,
        ISD::VP_FMA,         ISD::VP_REDUCE_FADD, ISD::VP_REDUCE_SEQ_FADD,
        ISD::VP_REDUCE_FMIN, ISD::VP_REDUCE_FMAX, ISD::VP_MERGE,
        ISD::VP_SELECT,      ISD::VP_SINT_TO_FP,  ISD::VP_UINT_TO_FP,
        ISD::VP_SETCC,       ISD::VP_FP_ROUND,    ISD::VP_FP_EXTEND,
        ISD::VP_SQRT,        ISD::VP_FMINNUM,     ISD::VP_FMAXNUM,
        ISD::VP_FCEIL,       ISD::VP_FFLOOR,      ISD::VP_FROUND,
        ISD::VP_FROUNDEVEN,  ISD::VP_FCOPYSIGN,   ISD::VP_FROUNDTOZERO,
        ISD::VP_FRINT,       ISD::VP_FNEARBYINT,  ISD::VP_IS_FPCLASS,
        ISD::VP_FMINIMUM,    ISD::VP_FMAXIMUM,    ISD::VP_LRINT,
        ISD::VP_LLRINT,      ISD::EXPERIMENTAL_VP_REVERSE,
        ISD::EXPERIMENTAL_VP_SPLICE, ISD::VP_REDUCE_FMINIMUM,
        ISD::VP_REDUCE_FMAXIMUM, ISD::EXPERIMENTAL_VP_SPLAT};

    static const unsigned IntegerVecReduceOps[] = {
        ISD::VECREDUCE_ADD,  ISD::VECREDUCE_AND,  ISD::VECREDUCE_OR,
        ISD::VECREDUCE_XOR,  ISD::VECREDUCE_SMAX, ISD::VECREDUCE_SMIN,
        ISD::VECREDUCE_UMAX, ISD::VECREDUCE_UMIN};

    static const unsigned FloatingPointVecReduceOps[] = {
        ISD::VECREDUCE_FADD, ISD::VECREDUCE_SEQ_FADD, ISD::VECREDUCE_FMIN,
        ISD::VECREDUCE_FMAX, ISD::VECREDUCE_FMINIMUM, ISD::VECREDUCE_FMAXIMUM};

    if (!Subtarget.is64Bit()) {
      // We must custom-lower certain vXi64 operations on RV32 due to the vector
      // element type being illegal.
      setOperationAction({ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT},
                         MVT::i64, Custom);

      setOperationAction(IntegerVecReduceOps, MVT::i64, Custom);

      setOperationAction({ISD::VP_REDUCE_ADD, ISD::VP_REDUCE_AND,
                          ISD::VP_REDUCE_OR, ISD::VP_REDUCE_XOR,
                          ISD::VP_REDUCE_SMAX, ISD::VP_REDUCE_SMIN,
                          ISD::VP_REDUCE_UMAX, ISD::VP_REDUCE_UMIN},
                         MVT::i64, Custom);
    }

    for (MVT VT : BoolVecVTs) {
      if (!isTypeLegal(VT))
        continue;

      setOperationAction(ISD::SPLAT_VECTOR, VT, Custom);

      // Mask VTs are custom-expanded into a series of standard nodes
      setOperationAction({ISD::TRUNCATE, ISD::CONCAT_VECTORS,
                          ISD::INSERT_SUBVECTOR, ISD::EXTRACT_SUBVECTOR,
                          ISD::SCALAR_TO_VECTOR},
                         VT, Custom);

      setOperationAction({ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT}, VT,
                         Custom);

      setOperationAction(ISD::SELECT, VT, Custom);
      setOperationAction(
          {ISD::SELECT_CC, ISD::VSELECT, ISD::VP_MERGE, ISD::VP_SELECT}, VT,
          Expand);

      setOperationAction({ISD::VP_CTTZ_ELTS, ISD::VP_CTTZ_ELTS_ZERO_UNDEF}, VT,
                         Custom);

      setOperationAction({ISD::VP_AND, ISD::VP_OR, ISD::VP_XOR}, VT, Custom);

      setOperationAction(
          {ISD::VECREDUCE_AND, ISD::VECREDUCE_OR, ISD::VECREDUCE_XOR}, VT,
          Custom);

      setOperationAction(
          {ISD::VP_REDUCE_AND, ISD::VP_REDUCE_OR, ISD::VP_REDUCE_XOR}, VT,
          Custom);

      // RVV has native int->float & float->int conversions where the
      // element type sizes are within one power-of-two of each other. Any
      // wider distances between type sizes have to be lowered as sequences
      // which progressively narrow the gap in stages.
      setOperationAction({ISD::SINT_TO_FP, ISD::UINT_TO_FP, ISD::FP_TO_SINT,
                          ISD::FP_TO_UINT, ISD::STRICT_SINT_TO_FP,
                          ISD::STRICT_UINT_TO_FP, ISD::STRICT_FP_TO_SINT,
                          ISD::STRICT_FP_TO_UINT},
                         VT, Custom);
      setOperationAction({ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT}, VT,
                         Custom);

      // Expand all extending loads to types larger than this, and truncating
      // stores from types larger than this.
      for (MVT OtherVT : MVT::integer_scalable_vector_valuetypes()) {
        setTruncStoreAction(VT, OtherVT, Expand);
        setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, VT,
                         OtherVT, Expand);
      }

      setOperationAction({ISD::VP_FP_TO_SINT, ISD::VP_FP_TO_UINT,
                          ISD::VP_TRUNCATE, ISD::VP_SETCC},
                         VT, Custom);

      setOperationAction(ISD::VECTOR_DEINTERLEAVE, VT, Custom);
      setOperationAction(ISD::VECTOR_INTERLEAVE, VT, Custom);

      setOperationAction(ISD::VECTOR_REVERSE, VT, Custom);

      setOperationAction(ISD::EXPERIMENTAL_VP_SPLICE, VT, Custom);
      setOperationAction(ISD::EXPERIMENTAL_VP_REVERSE, VT, Custom);

      setOperationPromotedToType(
          ISD::VECTOR_SPLICE, VT,
          MVT::getVectorVT(MVT::i8, VT.getVectorElementCount()));
    }

    for (MVT VT : IntVecVTs) {
      if (!isTypeLegal(VT))
        continue;

      setOperationAction(ISD::SPLAT_VECTOR, VT, Legal);
      setOperationAction(ISD::SPLAT_VECTOR_PARTS, VT, Custom);

      // Vectors implement MULHS/MULHU.
      setOperationAction({ISD::SMUL_LOHI, ISD::UMUL_LOHI}, VT, Expand);

      // nxvXi64 MULHS/MULHU requires the V extension instead of Zve64*.
      if (VT.getVectorElementType() == MVT::i64 && !Subtarget.hasStdExtV())
        setOperationAction({ISD::MULHU, ISD::MULHS}, VT, Expand);

      setOperationAction({ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX}, VT,
                         Legal);

      setOperationAction({ISD::ABDS, ISD::ABDU}, VT, Custom);

      // Custom-lower extensions and truncations from/to mask types.
      setOperationAction({ISD::ANY_EXTEND, ISD::SIGN_EXTEND, ISD::ZERO_EXTEND},
                         VT, Custom);

      // RVV has native int->float & float->int conversions where the
      // element type sizes are within one power-of-two of each other. Any
      // wider distances between type sizes have to be lowered as sequences
      // which progressively narrow the gap in stages.
      setOperationAction({ISD::SINT_TO_FP, ISD::UINT_TO_FP, ISD::FP_TO_SINT,
                          ISD::FP_TO_UINT, ISD::STRICT_SINT_TO_FP,
                          ISD::STRICT_UINT_TO_FP, ISD::STRICT_FP_TO_SINT,
                          ISD::STRICT_FP_TO_UINT},
                         VT, Custom);
      setOperationAction({ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT}, VT,
                         Custom);
      setOperationAction({ISD::AVGFLOORS, ISD::AVGFLOORU, ISD::AVGCEILS,
                          ISD::AVGCEILU, ISD::SADDSAT, ISD::UADDSAT,
                          ISD::SSUBSAT, ISD::USUBSAT},
                         VT, Legal);

      // Integer VTs are lowered as a series of "RISCVISD::TRUNCATE_VECTOR_VL"
      // nodes which truncate by one power of two at a time.
      setOperationAction(ISD::TRUNCATE, VT, Custom);

      // Custom-lower insert/extract operations to simplify patterns.
      setOperationAction({ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT}, VT,
                         Custom);

      // Custom-lower reduction operations to set up the corresponding custom
      // nodes' operands.
      setOperationAction(IntegerVecReduceOps, VT, Custom);

      setOperationAction(IntegerVPOps, VT, Custom);

      setOperationAction({ISD::LOAD, ISD::STORE}, VT, Custom);

      setOperationAction({ISD::MLOAD, ISD::MSTORE, ISD::MGATHER, ISD::MSCATTER},
                         VT, Custom);

      setOperationAction(
          {ISD::VP_LOAD, ISD::VP_STORE, ISD::EXPERIMENTAL_VP_STRIDED_LOAD,
           ISD::EXPERIMENTAL_VP_STRIDED_STORE, ISD::VP_GATHER, ISD::VP_SCATTER},
          VT, Custom);

      setOperationAction({ISD::CONCAT_VECTORS, ISD::INSERT_SUBVECTOR,
                          ISD::EXTRACT_SUBVECTOR, ISD::SCALAR_TO_VECTOR},
                         VT, Custom);

      setOperationAction(ISD::SELECT, VT, Custom);
      setOperationAction(ISD::SELECT_CC, VT, Expand);

      setOperationAction({ISD::STEP_VECTOR, ISD::VECTOR_REVERSE}, VT, Custom);

      for (MVT OtherVT : MVT::integer_scalable_vector_valuetypes()) {
        setTruncStoreAction(VT, OtherVT, Expand);
        setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, VT,
                         OtherVT, Expand);
      }

      setOperationAction(ISD::VECTOR_DEINTERLEAVE, VT, Custom);
      setOperationAction(ISD::VECTOR_INTERLEAVE, VT, Custom);

      // Splice
      setOperationAction(ISD::VECTOR_SPLICE, VT, Custom);

      if (Subtarget.hasStdExtZvkb()) {
        setOperationAction(ISD::BSWAP, VT, Legal);
        setOperationAction(ISD::VP_BSWAP, VT, Custom);
      } else {
        setOperationAction({ISD::BSWAP, ISD::VP_BSWAP}, VT, Expand);
        setOperationAction({ISD::ROTL, ISD::ROTR}, VT, Expand);
      }

      if (Subtarget.hasStdExtZvbb()) {
        setOperationAction(ISD::BITREVERSE, VT, Legal);
        setOperationAction(ISD::VP_BITREVERSE, VT, Custom);
        setOperationAction({ISD::VP_CTLZ, ISD::VP_CTLZ_ZERO_UNDEF, ISD::VP_CTTZ,
                            ISD::VP_CTTZ_ZERO_UNDEF, ISD::VP_CTPOP},
                           VT, Custom);
      } else {
        setOperationAction({ISD::BITREVERSE, ISD::VP_BITREVERSE}, VT, Expand);
        setOperationAction({ISD::CTLZ, ISD::CTTZ, ISD::CTPOP}, VT, Expand);
        setOperationAction({ISD::VP_CTLZ, ISD::VP_CTLZ_ZERO_UNDEF, ISD::VP_CTTZ,
                            ISD::VP_CTTZ_ZERO_UNDEF, ISD::VP_CTPOP},
                           VT, Expand);

        // Lower CTLZ_ZERO_UNDEF and CTTZ_ZERO_UNDEF if element of VT in the
        // range of f32.
        EVT FloatVT = MVT::getVectorVT(MVT::f32, VT.getVectorElementCount());
        if (isTypeLegal(FloatVT)) {
          setOperationAction({ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF,
                              ISD::CTTZ_ZERO_UNDEF, ISD::VP_CTLZ,
                              ISD::VP_CTLZ_ZERO_UNDEF, ISD::VP_CTTZ_ZERO_UNDEF},
                             VT, Custom);
        }
      }
    }

    // Expand various CCs to best match the RVV ISA, which natively supports UNE
    // but no other unordered comparisons, and supports all ordered comparisons
    // except ONE. Additionally, we expand GT,OGT,GE,OGE for optimization
    // purposes; they are expanded to their swapped-operand CCs (LT,OLT,LE,OLE),
    // and we pattern-match those back to the "original", swapping operands once
    // more. This way we catch both operations and both "vf" and "fv" forms with
    // fewer patterns.
    static const ISD::CondCode VFPCCToExpand[] = {
        ISD::SETO,   ISD::SETONE, ISD::SETUEQ, ISD::SETUGT,
        ISD::SETUGE, ISD::SETULT, ISD::SETULE, ISD::SETUO,
        ISD::SETGT,  ISD::SETOGT, ISD::SETGE,  ISD::SETOGE,
    };

    // TODO: support more ops.
    static const unsigned ZvfhminPromoteOps[] = {
        ISD::FMINNUM,     ISD::FMAXNUM,      ISD::FADD,        ISD::FSUB,
        ISD::FMUL,        ISD::FMA,          ISD::FDIV,        ISD::FSQRT,
        ISD::FABS,        ISD::FNEG,         ISD::FCOPYSIGN,   ISD::FCEIL,
        ISD::FFLOOR,      ISD::FROUND,       ISD::FROUNDEVEN,  ISD::FRINT,
        ISD::FNEARBYINT,  ISD::IS_FPCLASS,   ISD::SETCC,       ISD::FMAXIMUM,
        ISD::FMINIMUM,    ISD::STRICT_FADD,  ISD::STRICT_FSUB, ISD::STRICT_FMUL,
        ISD::STRICT_FDIV, ISD::STRICT_FSQRT, ISD::STRICT_FMA};

    // TODO: support more vp ops.
    static const unsigned ZvfhminPromoteVPOps[] = {
        ISD::VP_FADD,        ISD::VP_FSUB,         ISD::VP_FMUL,
        ISD::VP_FDIV,        ISD::VP_FNEG,         ISD::VP_FABS,
        ISD::VP_FMA,         ISD::VP_REDUCE_FADD,  ISD::VP_REDUCE_SEQ_FADD,
        ISD::VP_REDUCE_FMIN, ISD::VP_REDUCE_FMAX,  ISD::VP_SQRT,
        ISD::VP_FMINNUM,     ISD::VP_FMAXNUM,      ISD::VP_FCEIL,
        ISD::VP_FFLOOR,      ISD::VP_FROUND,       ISD::VP_FROUNDEVEN,
        ISD::VP_FCOPYSIGN,   ISD::VP_FROUNDTOZERO, ISD::VP_FRINT,
        ISD::VP_FNEARBYINT,  ISD::VP_SETCC,        ISD::VP_FMINIMUM,
        ISD::VP_FMAXIMUM,    ISD::VP_REDUCE_FMINIMUM, ISD::VP_REDUCE_FMAXIMUM};

    // Sets common operation actions on RVV floating-point vector types.
    const auto SetCommonVFPActions = [&](MVT VT) {
      setOperationAction(ISD::SPLAT_VECTOR, VT, Legal);
      // RVV has native FP_ROUND & FP_EXTEND conversions where the element type
      // sizes are within one power-of-two of each other. Therefore conversions
      // between vXf16 and vXf64 must be lowered as sequences which convert via
      // vXf32.
      setOperationAction({ISD::FP_ROUND, ISD::FP_EXTEND}, VT, Custom);
      setOperationAction({ISD::LRINT, ISD::LLRINT}, VT, Custom);
      // Custom-lower insert/extract operations to simplify patterns.
      setOperationAction({ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT}, VT,
                         Custom);
      // Expand various condition codes (explained above).
      setCondCodeAction(VFPCCToExpand, VT, Expand);

      setOperationAction({ISD::FMINNUM, ISD::FMAXNUM}, VT, Legal);
      setOperationAction({ISD::FMAXIMUM, ISD::FMINIMUM}, VT, Custom);

      setOperationAction({ISD::FTRUNC, ISD::FCEIL, ISD::FFLOOR, ISD::FROUND,
                          ISD::FROUNDEVEN, ISD::FRINT, ISD::FNEARBYINT,
                          ISD::IS_FPCLASS},
                         VT, Custom);

      setOperationAction(FloatingPointVecReduceOps, VT, Custom);

      // Expand FP operations that need libcalls.
      setOperationAction(ISD::FREM, VT, Expand);
      setOperationAction(ISD::FPOW, VT, Expand);
      setOperationAction(ISD::FCOS, VT, Expand);
      setOperationAction(ISD::FSIN, VT, Expand);
      setOperationAction(ISD::FSINCOS, VT, Expand);
      setOperationAction(ISD::FEXP, VT, Expand);
      setOperationAction(ISD::FEXP2, VT, Expand);
      setOperationAction(ISD::FEXP10, VT, Expand);
      setOperationAction(ISD::FLOG, VT, Expand);
      setOperationAction(ISD::FLOG2, VT, Expand);
      setOperationAction(ISD::FLOG10, VT, Expand);

      setOperationAction(ISD::FCOPYSIGN, VT, Legal);

      setOperationAction({ISD::LOAD, ISD::STORE}, VT, Custom);

      setOperationAction({ISD::MLOAD, ISD::MSTORE, ISD::MGATHER, ISD::MSCATTER},
                         VT, Custom);

      setOperationAction(
          {ISD::VP_LOAD, ISD::VP_STORE, ISD::EXPERIMENTAL_VP_STRIDED_LOAD,
           ISD::EXPERIMENTAL_VP_STRIDED_STORE, ISD::VP_GATHER, ISD::VP_SCATTER},
          VT, Custom);

      setOperationAction(ISD::SELECT, VT, Custom);
      setOperationAction(ISD::SELECT_CC, VT, Expand);

      setOperationAction({ISD::CONCAT_VECTORS, ISD::INSERT_SUBVECTOR,
                          ISD::EXTRACT_SUBVECTOR, ISD::SCALAR_TO_VECTOR},
                         VT, Custom);

      setOperationAction(ISD::VECTOR_DEINTERLEAVE, VT, Custom);
      setOperationAction(ISD::VECTOR_INTERLEAVE, VT, Custom);

      setOperationAction({ISD::VECTOR_REVERSE, ISD::VECTOR_SPLICE}, VT, Custom);

      setOperationAction(FloatingPointVPOps, VT, Custom);

      setOperationAction({ISD::STRICT_FP_EXTEND, ISD::STRICT_FP_ROUND}, VT,
                         Custom);
      setOperationAction({ISD::STRICT_FADD, ISD::STRICT_FSUB, ISD::STRICT_FMUL,
                          ISD::STRICT_FDIV, ISD::STRICT_FSQRT, ISD::STRICT_FMA},
                         VT, Legal);
      setOperationAction({ISD::STRICT_FSETCC, ISD::STRICT_FSETCCS,
                          ISD::STRICT_FTRUNC, ISD::STRICT_FCEIL,
                          ISD::STRICT_FFLOOR, ISD::STRICT_FROUND,
                          ISD::STRICT_FROUNDEVEN, ISD::STRICT_FNEARBYINT},
                         VT, Custom);
    };

    // Sets common extload/truncstore actions on RVV floating-point vector
    // types.
    const auto SetCommonVFPExtLoadTruncStoreActions =
        [&](MVT VT, ArrayRef<MVT::SimpleValueType> SmallerVTs) {
          for (auto SmallVT : SmallerVTs) {
            setTruncStoreAction(VT, SmallVT, Expand);
            setLoadExtAction(ISD::EXTLOAD, VT, SmallVT, Expand);
          }
        };

    if (Subtarget.hasVInstructionsF16()) {
      for (MVT VT : F16VecVTs) {
        if (!isTypeLegal(VT))
          continue;
        SetCommonVFPActions(VT);
      }
    } else if (Subtarget.hasVInstructionsF16Minimal()) {
      for (MVT VT : F16VecVTs) {
        if (!isTypeLegal(VT))
          continue;
        setOperationAction({ISD::FP_ROUND, ISD::FP_EXTEND}, VT, Custom);
        setOperationAction({ISD::STRICT_FP_ROUND, ISD::STRICT_FP_EXTEND}, VT,
                           Custom);
        setOperationAction({ISD::VP_FP_ROUND, ISD::VP_FP_EXTEND}, VT, Custom);
        setOperationAction({ISD::VP_MERGE, ISD::VP_SELECT, ISD::SELECT}, VT,
                           Custom);
        setOperationAction(ISD::SELECT_CC, VT, Expand);
        setOperationAction({ISD::SINT_TO_FP, ISD::UINT_TO_FP,
                            ISD::VP_SINT_TO_FP, ISD::VP_UINT_TO_FP},
                           VT, Custom);
        setOperationAction({ISD::CONCAT_VECTORS, ISD::INSERT_SUBVECTOR,
                            ISD::EXTRACT_SUBVECTOR},
                           VT, Custom);
        if (Subtarget.hasStdExtZfhmin())
          setOperationAction(ISD::SPLAT_VECTOR, VT, Custom);
        // load/store
        setOperationAction({ISD::LOAD, ISD::STORE}, VT, Custom);

        // Custom split nxv32f16 since nxv32f32 if not legal.
        if (VT == MVT::nxv32f16) {
          setOperationAction(ZvfhminPromoteOps, VT, Custom);
          setOperationAction(ZvfhminPromoteVPOps, VT, Custom);
          continue;
        }
        // Add more promote ops.
        MVT F32VecVT = MVT::getVectorVT(MVT::f32, VT.getVectorElementCount());
        setOperationPromotedToType(ZvfhminPromoteOps, VT, F32VecVT);
        setOperationPromotedToType(ZvfhminPromoteVPOps, VT, F32VecVT);
      }
    }

    // TODO: Could we merge some code with zvfhmin?
    if (Subtarget.hasVInstructionsBF16()) {
      for (MVT VT : BF16VecVTs) {
        if (!isTypeLegal(VT))
          continue;
        setOperationAction({ISD::FP_ROUND, ISD::FP_EXTEND}, VT, Custom);
        setOperationAction({ISD::VP_FP_ROUND, ISD::VP_FP_EXTEND}, VT, Custom);
        setOperationAction({ISD::STRICT_FP_ROUND, ISD::STRICT_FP_EXTEND}, VT,
                           Custom);
        setOperationAction({ISD::CONCAT_VECTORS, ISD::INSERT_SUBVECTOR,
                            ISD::EXTRACT_SUBVECTOR},
                           VT, Custom);
        setOperationAction({ISD::LOAD, ISD::STORE}, VT, Custom);
        if (Subtarget.hasStdExtZfbfmin())
          setOperationAction(ISD::SPLAT_VECTOR, VT, Custom);
        setOperationAction({ISD::VP_MERGE, ISD::VP_SELECT, ISD::SELECT}, VT,
                           Custom);
        setOperationAction(ISD::SELECT_CC, VT, Expand);
        // TODO: Promote to fp32.
      }
    }

    if (Subtarget.hasVInstructionsF32()) {
      for (MVT VT : F32VecVTs) {
        if (!isTypeLegal(VT))
          continue;
        SetCommonVFPActions(VT);
        SetCommonVFPExtLoadTruncStoreActions(VT, F16VecVTs);
      }
    }

    if (Subtarget.hasVInstructionsF64()) {
      for (MVT VT : F64VecVTs) {
        if (!isTypeLegal(VT))
          continue;
        SetCommonVFPActions(VT);
        SetCommonVFPExtLoadTruncStoreActions(VT, F16VecVTs);
        SetCommonVFPExtLoadTruncStoreActions(VT, F32VecVTs);
      }
    }

    if (Subtarget.useRVVForFixedLengthVectors()) {
      for (MVT VT : MVT::integer_fixedlen_vector_valuetypes()) {
        if (!useRVVForFixedLengthVectorVT(VT))
          continue;

        // By default everything must be expanded.
        for (unsigned Op = 0; Op < ISD::BUILTIN_OP_END; ++Op)
          setOperationAction(Op, VT, Expand);
        for (MVT OtherVT : MVT::integer_fixedlen_vector_valuetypes()) {
          setTruncStoreAction(VT, OtherVT, Expand);
          setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, VT,
                           OtherVT, Expand);
        }

        // Custom lower fixed vector undefs to scalable vector undefs to avoid
        // expansion to a build_vector of 0s.
        setOperationAction(ISD::UNDEF, VT, Custom);

        // We use EXTRACT_SUBVECTOR as a "cast" from scalable to fixed.
        setOperationAction({ISD::INSERT_SUBVECTOR, ISD::EXTRACT_SUBVECTOR}, VT,
                           Custom);

        setOperationAction({ISD::BUILD_VECTOR, ISD::CONCAT_VECTORS}, VT,
                           Custom);

        setOperationAction({ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT},
                           VT, Custom);

        setOperationAction(ISD::SCALAR_TO_VECTOR, VT, Custom);

        setOperationAction({ISD::LOAD, ISD::STORE}, VT, Custom);

        setOperationAction(ISD::SETCC, VT, Custom);

        setOperationAction(ISD::SELECT, VT, Custom);

        setOperationAction(ISD::TRUNCATE, VT, Custom);

        setOperationAction(ISD::BITCAST, VT, Custom);

        setOperationAction(
            {ISD::VECREDUCE_AND, ISD::VECREDUCE_OR, ISD::VECREDUCE_XOR}, VT,
            Custom);

        setOperationAction(
            {ISD::VP_REDUCE_AND, ISD::VP_REDUCE_OR, ISD::VP_REDUCE_XOR}, VT,
            Custom);

        setOperationAction(
            {
                ISD::SINT_TO_FP,
                ISD::UINT_TO_FP,
                ISD::FP_TO_SINT,
                ISD::FP_TO_UINT,
                ISD::STRICT_SINT_TO_FP,
                ISD::STRICT_UINT_TO_FP,
                ISD::STRICT_FP_TO_SINT,
                ISD::STRICT_FP_TO_UINT,
            },
            VT, Custom);
        setOperationAction({ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT}, VT,
                           Custom);

        setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);

        // Operations below are different for between masks and other vectors.
        if (VT.getVectorElementType() == MVT::i1) {
          setOperationAction({ISD::VP_AND, ISD::VP_OR, ISD::VP_XOR, ISD::AND,
                              ISD::OR, ISD::XOR},
                             VT, Custom);

          setOperationAction({ISD::VP_FP_TO_SINT, ISD::VP_FP_TO_UINT,
                              ISD::VP_SETCC, ISD::VP_TRUNCATE},
                             VT, Custom);

          setOperationAction(ISD::EXPERIMENTAL_VP_SPLICE, VT, Custom);
          setOperationAction(ISD::EXPERIMENTAL_VP_REVERSE, VT, Custom);
          continue;
        }

        // Make SPLAT_VECTOR Legal so DAGCombine will convert splat vectors to
        // it before type legalization for i64 vectors on RV32. It will then be
        // type legalized to SPLAT_VECTOR_PARTS which we need to Custom handle.
        // FIXME: Use SPLAT_VECTOR for all types? DAGCombine probably needs
        // improvements first.
        if (!Subtarget.is64Bit() && VT.getVectorElementType() == MVT::i64) {
          setOperationAction(ISD::SPLAT_VECTOR, VT, Legal);
          setOperationAction(ISD::SPLAT_VECTOR_PARTS, VT, Custom);
        }

        setOperationAction(
            {ISD::MLOAD, ISD::MSTORE, ISD::MGATHER, ISD::MSCATTER}, VT, Custom);

        setOperationAction({ISD::VP_LOAD, ISD::VP_STORE,
                            ISD::EXPERIMENTAL_VP_STRIDED_LOAD,
                            ISD::EXPERIMENTAL_VP_STRIDED_STORE, ISD::VP_GATHER,
                            ISD::VP_SCATTER},
                           VT, Custom);

        setOperationAction({ISD::ADD, ISD::MUL, ISD::SUB, ISD::AND, ISD::OR,
                            ISD::XOR, ISD::SDIV, ISD::SREM, ISD::UDIV,
                            ISD::UREM, ISD::SHL, ISD::SRA, ISD::SRL},
                           VT, Custom);

        setOperationAction(
            {ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX, ISD::ABS}, VT, Custom);

        setOperationAction({ISD::ABDS, ISD::ABDU}, VT, Custom);

        // vXi64 MULHS/MULHU requires the V extension instead of Zve64*.
        if (VT.getVectorElementType() != MVT::i64 || Subtarget.hasStdExtV())
          setOperationAction({ISD::MULHS, ISD::MULHU}, VT, Custom);

        setOperationAction({ISD::AVGFLOORS, ISD::AVGFLOORU, ISD::AVGCEILS,
                            ISD::AVGCEILU, ISD::SADDSAT, ISD::UADDSAT,
                            ISD::SSUBSAT, ISD::USUBSAT},
                           VT, Custom);

        setOperationAction(ISD::VSELECT, VT, Custom);

        setOperationAction(
            {ISD::ANY_EXTEND, ISD::SIGN_EXTEND, ISD::ZERO_EXTEND}, VT, Custom);

        // Custom-lower reduction operations to set up the corresponding custom
        // nodes' operands.
        setOperationAction({ISD::VECREDUCE_ADD, ISD::VECREDUCE_SMAX,
                            ISD::VECREDUCE_SMIN, ISD::VECREDUCE_UMAX,
                            ISD::VECREDUCE_UMIN},
                           VT, Custom);

        setOperationAction(IntegerVPOps, VT, Custom);

        if (Subtarget.hasStdExtZvkb())
          setOperationAction({ISD::BSWAP, ISD::ROTL, ISD::ROTR}, VT, Custom);

        if (Subtarget.hasStdExtZvbb()) {
          setOperationAction({ISD::BITREVERSE, ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF,
                              ISD::CTTZ, ISD::CTTZ_ZERO_UNDEF, ISD::CTPOP},
                             VT, Custom);
        } else {
          // Lower CTLZ_ZERO_UNDEF and CTTZ_ZERO_UNDEF if element of VT in the
          // range of f32.
          EVT FloatVT = MVT::getVectorVT(MVT::f32, VT.getVectorElementCount());
          if (isTypeLegal(FloatVT))
            setOperationAction(
                {ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF, ISD::CTTZ_ZERO_UNDEF}, VT,
                Custom);
        }
      }

      for (MVT VT : MVT::fp_fixedlen_vector_valuetypes()) {
        // There are no extending loads or truncating stores.
        for (MVT InnerVT : MVT::fp_fixedlen_vector_valuetypes()) {
          setLoadExtAction(ISD::EXTLOAD, VT, InnerVT, Expand);
          setTruncStoreAction(VT, InnerVT, Expand);
        }

        if (!useRVVForFixedLengthVectorVT(VT))
          continue;

        // By default everything must be expanded.
        for (unsigned Op = 0; Op < ISD::BUILTIN_OP_END; ++Op)
          setOperationAction(Op, VT, Expand);

        // Custom lower fixed vector undefs to scalable vector undefs to avoid
        // expansion to a build_vector of 0s.
        setOperationAction(ISD::UNDEF, VT, Custom);

        setOperationAction({ISD::CONCAT_VECTORS, ISD::INSERT_SUBVECTOR,
                            ISD::EXTRACT_SUBVECTOR},
                           VT, Custom);

        // FIXME: mload, mstore, mgather, mscatter, vp_load/store,
        // vp_stride_load/store, vp_gather/scatter can be hoisted to here.
        setOperationAction({ISD::LOAD, ISD::STORE}, VT, Custom);

        setOperationAction({ISD::FP_ROUND, ISD::FP_EXTEND}, VT, Custom);
        setOperationAction({ISD::STRICT_FP_ROUND, ISD::STRICT_FP_EXTEND}, VT,
                           Custom);

        if (VT.getVectorElementType() == MVT::f16 &&
            !Subtarget.hasVInstructionsF16()) {
          setOperationAction({ISD::VP_FP_ROUND, ISD::VP_FP_EXTEND}, VT, Custom);
          setOperationAction(
              {ISD::VP_MERGE, ISD::VP_SELECT, ISD::VSELECT, ISD::SELECT}, VT,
              Custom);
          setOperationAction({ISD::SINT_TO_FP, ISD::UINT_TO_FP,
                              ISD::VP_SINT_TO_FP, ISD::VP_UINT_TO_FP},
                             VT, Custom);
          setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);
          if (Subtarget.hasStdExtZfhmin()) {
            // FIXME: We should prefer BUILD_VECTOR over SPLAT_VECTOR.
            setOperationAction(ISD::SPLAT_VECTOR, VT, Custom);
          } else {
            // We need to custom legalize f16 build vectors if Zfhmin isn't
            // available.
            setOperationAction(ISD::BUILD_VECTOR, MVT::f16, Custom);
          }
          MVT F32VecVT = MVT::getVectorVT(MVT::f32, VT.getVectorElementCount());
          // Don't promote f16 vector operations to f32 if f32 vector type is
          // not legal.
          // TODO: could split the f16 vector into two vectors and do promotion.
          if (!isTypeLegal(F32VecVT))
            continue;
          setOperationPromotedToType(ZvfhminPromoteOps, VT, F32VecVT);
          setOperationPromotedToType(ZvfhminPromoteVPOps, VT, F32VecVT);
          continue;
        }

        if (VT.getVectorElementType() == MVT::bf16) {
          setOperationAction({ISD::VP_FP_ROUND, ISD::VP_FP_EXTEND}, VT, Custom);
          // FIXME: We should prefer BUILD_VECTOR over SPLAT_VECTOR.
          setOperationAction(ISD::SPLAT_VECTOR, VT, Custom);
          setOperationAction(
              {ISD::VP_MERGE, ISD::VP_SELECT, ISD::VSELECT, ISD::SELECT}, VT,
              Custom);
          // TODO: Promote to fp32.
          continue;
        }

        setOperationAction({ISD::BUILD_VECTOR, ISD::VECTOR_SHUFFLE,
                            ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT},
                           VT, Custom);

        setOperationAction(
            {ISD::MLOAD, ISD::MSTORE, ISD::MGATHER, ISD::MSCATTER}, VT, Custom);

        setOperationAction({ISD::VP_LOAD, ISD::VP_STORE,
                            ISD::EXPERIMENTAL_VP_STRIDED_LOAD,
                            ISD::EXPERIMENTAL_VP_STRIDED_STORE, ISD::VP_GATHER,
                            ISD::VP_SCATTER},
                           VT, Custom);

        setOperationAction({ISD::FADD, ISD::FSUB, ISD::FMUL, ISD::FDIV,
                            ISD::FNEG, ISD::FABS, ISD::FCOPYSIGN, ISD::FSQRT,
                            ISD::FMA, ISD::FMINNUM, ISD::FMAXNUM,
                            ISD::IS_FPCLASS, ISD::FMAXIMUM, ISD::FMINIMUM},
                           VT, Custom);

        setOperationAction({ISD::FTRUNC, ISD::FCEIL, ISD::FFLOOR, ISD::FROUND,
                            ISD::FROUNDEVEN, ISD::FRINT, ISD::FNEARBYINT},
                           VT, Custom);

        setCondCodeAction(VFPCCToExpand, VT, Expand);

        setOperationAction(ISD::SETCC, VT, Custom);
        setOperationAction({ISD::VSELECT, ISD::SELECT}, VT, Custom);

        setOperationAction(ISD::BITCAST, VT, Custom);

        setOperationAction(FloatingPointVecReduceOps, VT, Custom);

        setOperationAction(FloatingPointVPOps, VT, Custom);

        setOperationAction(
            {ISD::STRICT_FADD, ISD::STRICT_FSUB, ISD::STRICT_FMUL,
             ISD::STRICT_FDIV, ISD::STRICT_FSQRT, ISD::STRICT_FMA,
             ISD::STRICT_FSETCC, ISD::STRICT_FSETCCS, ISD::STRICT_FTRUNC,
             ISD::STRICT_FCEIL, ISD::STRICT_FFLOOR, ISD::STRICT_FROUND,
             ISD::STRICT_FROUNDEVEN, ISD::STRICT_FNEARBYINT},
            VT, Custom);
      }

      // Custom-legalize bitcasts from fixed-length vectors to scalar types.
      setOperationAction(ISD::BITCAST, {MVT::i8, MVT::i16, MVT::i32}, Custom);
      if (Subtarget.is64Bit())
        setOperationAction(ISD::BITCAST, MVT::i64, Custom);
      if (Subtarget.hasStdExtZfhminOrZhinxmin())
        setOperationAction(ISD::BITCAST, MVT::f16, Custom);
      if (Subtarget.hasStdExtFOrZfinx())
        setOperationAction(ISD::BITCAST, MVT::f32, Custom);
      if (Subtarget.hasStdExtDOrZdinx())
        setOperationAction(ISD::BITCAST, MVT::f64, Custom);
    }
  }

  if (Subtarget.hasStdExtA()) {
    setOperationAction(ISD::ATOMIC_LOAD_SUB, XLenVT, Expand);
    if (RV64LegalI32 && Subtarget.is64Bit())
      setOperationAction(ISD::ATOMIC_LOAD_SUB, MVT::i32, Expand);
  }

  if (Subtarget.hasForcedAtomics()) {
    // Force __sync libcalls to be emitted for atomic rmw/cas operations.
    setOperationAction(
        {ISD::ATOMIC_CMP_SWAP, ISD::ATOMIC_SWAP, ISD::ATOMIC_LOAD_ADD,
         ISD::ATOMIC_LOAD_SUB, ISD::ATOMIC_LOAD_AND, ISD::ATOMIC_LOAD_OR,
         ISD::ATOMIC_LOAD_XOR, ISD::ATOMIC_LOAD_NAND, ISD::ATOMIC_LOAD_MIN,
         ISD::ATOMIC_LOAD_MAX, ISD::ATOMIC_LOAD_UMIN, ISD::ATOMIC_LOAD_UMAX},
        XLenVT, LibCall);
  }

  if (Subtarget.hasVendorXTHeadMemIdx()) {
    for (unsigned im : {ISD::PRE_INC, ISD::POST_INC}) {
      setIndexedLoadAction(im, MVT::i8, Legal);
      setIndexedStoreAction(im, MVT::i8, Legal);
      setIndexedLoadAction(im, MVT::i16, Legal);
      setIndexedStoreAction(im, MVT::i16, Legal);
      setIndexedLoadAction(im, MVT::i32, Legal);
      setIndexedStoreAction(im, MVT::i32, Legal);

      if (Subtarget.is64Bit()) {
        setIndexedLoadAction(im, MVT::i64, Legal);
        setIndexedStoreAction(im, MVT::i64, Legal);
      }
    }
  }

  if (Subtarget.hasVendorXCVmem()) {
    setIndexedLoadAction(ISD::POST_INC, MVT::i8, Legal);
    setIndexedLoadAction(ISD::POST_INC, MVT::i16, Legal);
    setIndexedLoadAction(ISD::POST_INC, MVT::i32, Legal);

    setIndexedStoreAction(ISD::POST_INC, MVT::i8, Legal);
    setIndexedStoreAction(ISD::POST_INC, MVT::i16, Legal);
    setIndexedStoreAction(ISD::POST_INC, MVT::i32, Legal);
  }

  if (Subtarget.hasVendorXCValu()) {
    setOperationAction(ISD::ABS, XLenVT, Legal);
    setOperationAction(ISD::SMIN, XLenVT, Legal);
    setOperationAction(ISD::UMIN, XLenVT, Legal);
    setOperationAction(ISD::SMAX, XLenVT, Legal);
    setOperationAction(ISD::UMAX, XLenVT, Legal);
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Legal);
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Legal);
  }

  // Function alignments.
  const Align FunctionAlignment(Subtarget.hasStdExtCOrZca() ? 2 : 4);
  setMinFunctionAlignment(FunctionAlignment);
  // Set preferred alignments.
  setPrefFunctionAlignment(Subtarget.getPrefFunctionAlignment());
  setPrefLoopAlignment(Subtarget.getPrefLoopAlignment());

  setTargetDAGCombine({ISD::INTRINSIC_VOID, ISD::INTRINSIC_W_CHAIN,
                       ISD::INTRINSIC_WO_CHAIN, ISD::ADD, ISD::SUB, ISD::MUL,
                       ISD::AND, ISD::OR, ISD::XOR, ISD::SETCC, ISD::SELECT});
  if (Subtarget.is64Bit())
    setTargetDAGCombine(ISD::SRA);

  if (Subtarget.hasStdExtFOrZfinx())
    setTargetDAGCombine({ISD::FADD, ISD::FMAXNUM, ISD::FMINNUM});

  if (Subtarget.hasStdExtZbb())
    setTargetDAGCombine({ISD::UMAX, ISD::UMIN, ISD::SMAX, ISD::SMIN});

  if ((Subtarget.hasStdExtZbs() && Subtarget.is64Bit()) ||
      Subtarget.hasStdExtV())
    setTargetDAGCombine(ISD::TRUNCATE);

  if (Subtarget.hasStdExtZbkb())
    setTargetDAGCombine(ISD::BITREVERSE);
  if (Subtarget.hasStdExtZfhminOrZhinxmin())
    setTargetDAGCombine(ISD::SIGN_EXTEND_INREG);
  if (Subtarget.hasStdExtFOrZfinx())
    setTargetDAGCombine({ISD::ZERO_EXTEND, ISD::FP_TO_SINT, ISD::FP_TO_UINT,
                         ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT});
  if (Subtarget.hasVInstructions())
    setTargetDAGCombine({ISD::FCOPYSIGN, ISD::MGATHER, ISD::MSCATTER,
                         ISD::VP_GATHER, ISD::VP_SCATTER, ISD::SRA, ISD::SRL,
                         ISD::SHL, ISD::STORE, ISD::SPLAT_VECTOR,
                         ISD::BUILD_VECTOR, ISD::CONCAT_VECTORS,
                         ISD::EXPERIMENTAL_VP_REVERSE, ISD::MUL,
                         ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM,
                         ISD::INSERT_VECTOR_ELT, ISD::ABS});
  if (Subtarget.hasVendorXTHeadMemPair())
    setTargetDAGCombine({ISD::LOAD, ISD::STORE});
  if (Subtarget.useRVVForFixedLengthVectors())
    setTargetDAGCombine(ISD::BITCAST);

  setLibcallName(RTLIB::FPEXT_F16_F32, "__extendhfsf2");
  setLibcallName(RTLIB::FPROUND_F32_F16, "__truncsfhf2");

  // Disable strict node mutation.
  IsStrictFPEnabled = true;

  // Let the subtarget decide if a predictable select is more expensive than the
  // corresponding branch. This information is used in CGP/SelectOpt to decide
  // when to convert selects into branches.
  PredictableSelectIsExpensive = Subtarget.predictableSelectIsExpensive();
}

EVT RISCVTargetLowering::getSetCCResultType(const DataLayout &DL,
                                            LLVMContext &Context,
                                            EVT VT) const {
  if (!VT.isVector())
    return getPointerTy(DL);
  if (Subtarget.hasVInstructions() &&
      (VT.isScalableVector() || Subtarget.useRVVForFixedLengthVectors()))
    return EVT::getVectorVT(Context, MVT::i1, VT.getVectorElementCount());
  return VT.changeVectorElementTypeToInteger();
}

MVT RISCVTargetLowering::getVPExplicitVectorLengthTy() const {
  return Subtarget.getXLenVT();
}

// Return false if we can lower get_vector_length to a vsetvli intrinsic.
bool RISCVTargetLowering::shouldExpandGetVectorLength(EVT TripCountVT,
                                                      unsigned VF,
                                                      bool IsScalable) const {
  if (!Subtarget.hasVInstructions())
    return true;

  if (!IsScalable)
    return true;

  if (TripCountVT != MVT::i32 && TripCountVT != Subtarget.getXLenVT())
    return true;

  // Don't allow VF=1 if those types are't legal.
  if (VF < RISCV::RVVBitsPerBlock / Subtarget.getELen())
    return true;

  // VLEN=32 support is incomplete.
  if (Subtarget.getRealMinVLen() < RISCV::RVVBitsPerBlock)
    return true;

  // The maximum VF is for the smallest element width with LMUL=8.
  // VF must be a power of 2.
  unsigned MaxVF = (RISCV::RVVBitsPerBlock / 8) * 8;
  return VF > MaxVF || !isPowerOf2_32(VF);
}

bool RISCVTargetLowering::shouldExpandCttzElements(EVT VT) const {
  return !Subtarget.hasVInstructions() ||
         VT.getVectorElementType() != MVT::i1 || !isTypeLegal(VT);
}

bool RISCVTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                             const CallInst &I,
                                             MachineFunction &MF,
                                             unsigned Intrinsic) const {
  auto &DL = I.getDataLayout();

  auto SetRVVLoadStoreInfo = [&](unsigned PtrOp, bool IsStore,
                                 bool IsUnitStrided, bool UsePtrVal = false) {
    Info.opc = IsStore ? ISD::INTRINSIC_VOID : ISD::INTRINSIC_W_CHAIN;
    // We can't use ptrVal if the intrinsic can access memory before the
    // pointer. This means we can't use it for strided or indexed intrinsics.
    if (UsePtrVal)
      Info.ptrVal = I.getArgOperand(PtrOp);
    else
      Info.fallbackAddressSpace =
          I.getArgOperand(PtrOp)->getType()->getPointerAddressSpace();
    Type *MemTy;
    if (IsStore) {
      // Store value is the first operand.
      MemTy = I.getArgOperand(0)->getType();
    } else {
      // Use return type. If it's segment load, return type is a struct.
      MemTy = I.getType();
      if (MemTy->isStructTy())
        MemTy = MemTy->getStructElementType(0);
    }
    if (!IsUnitStrided)
      MemTy = MemTy->getScalarType();

    Info.memVT = getValueType(DL, MemTy);
    Info.align = Align(DL.getTypeSizeInBits(MemTy->getScalarType()) / 8);
    Info.size = MemoryLocation::UnknownSize;
    Info.flags |=
        IsStore ? MachineMemOperand::MOStore : MachineMemOperand::MOLoad;
    return true;
  };

  if (I.hasMetadata(LLVMContext::MD_nontemporal))
    Info.flags |= MachineMemOperand::MONonTemporal;

  Info.flags |= RISCVTargetLowering::getTargetMMOFlags(I);
  switch (Intrinsic) {
  default:
    return false;
  case Intrinsic::riscv_masked_atomicrmw_xchg_i32:
  case Intrinsic::riscv_masked_atomicrmw_add_i32:
  case Intrinsic::riscv_masked_atomicrmw_sub_i32:
  case Intrinsic::riscv_masked_atomicrmw_nand_i32:
  case Intrinsic::riscv_masked_atomicrmw_max_i32:
  case Intrinsic::riscv_masked_atomicrmw_min_i32:
  case Intrinsic::riscv_masked_atomicrmw_umax_i32:
  case Intrinsic::riscv_masked_atomicrmw_umin_i32:
  case Intrinsic::riscv_masked_cmpxchg_i32:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Align(4);
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore |
                 MachineMemOperand::MOVolatile;
    return true;
  case Intrinsic::riscv_masked_strided_load:
    return SetRVVLoadStoreInfo(/*PtrOp*/ 1, /*IsStore*/ false,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_masked_strided_store:
    return SetRVVLoadStoreInfo(/*PtrOp*/ 1, /*IsStore*/ true,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_seg2_load:
  case Intrinsic::riscv_seg3_load:
  case Intrinsic::riscv_seg4_load:
  case Intrinsic::riscv_seg5_load:
  case Intrinsic::riscv_seg6_load:
  case Intrinsic::riscv_seg7_load:
  case Intrinsic::riscv_seg8_load:
    return SetRVVLoadStoreInfo(/*PtrOp*/ 0, /*IsStore*/ false,
                               /*IsUnitStrided*/ false, /*UsePtrVal*/ true);
  case Intrinsic::riscv_seg2_store:
  case Intrinsic::riscv_seg3_store:
  case Intrinsic::riscv_seg4_store:
  case Intrinsic::riscv_seg5_store:
  case Intrinsic::riscv_seg6_store:
  case Intrinsic::riscv_seg7_store:
  case Intrinsic::riscv_seg8_store:
    // Operands are (vec, ..., vec, ptr, vl)
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 2,
                               /*IsStore*/ true,
                               /*IsUnitStrided*/ false, /*UsePtrVal*/ true);
  case Intrinsic::riscv_vle:
  case Intrinsic::riscv_vle_mask:
  case Intrinsic::riscv_vleff:
  case Intrinsic::riscv_vleff_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ 1,
                               /*IsStore*/ false,
                               /*IsUnitStrided*/ true,
                               /*UsePtrVal*/ true);
  case Intrinsic::riscv_vse:
  case Intrinsic::riscv_vse_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ 1,
                               /*IsStore*/ true,
                               /*IsUnitStrided*/ true,
                               /*UsePtrVal*/ true);
  case Intrinsic::riscv_vlse:
  case Intrinsic::riscv_vlse_mask:
  case Intrinsic::riscv_vloxei:
  case Intrinsic::riscv_vloxei_mask:
  case Intrinsic::riscv_vluxei:
  case Intrinsic::riscv_vluxei_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ 1,
                               /*IsStore*/ false,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_vsse:
  case Intrinsic::riscv_vsse_mask:
  case Intrinsic::riscv_vsoxei:
  case Intrinsic::riscv_vsoxei_mask:
  case Intrinsic::riscv_vsuxei:
  case Intrinsic::riscv_vsuxei_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ 1,
                               /*IsStore*/ true,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_vlseg2:
  case Intrinsic::riscv_vlseg3:
  case Intrinsic::riscv_vlseg4:
  case Intrinsic::riscv_vlseg5:
  case Intrinsic::riscv_vlseg6:
  case Intrinsic::riscv_vlseg7:
  case Intrinsic::riscv_vlseg8:
  case Intrinsic::riscv_vlseg2ff:
  case Intrinsic::riscv_vlseg3ff:
  case Intrinsic::riscv_vlseg4ff:
  case Intrinsic::riscv_vlseg5ff:
  case Intrinsic::riscv_vlseg6ff:
  case Intrinsic::riscv_vlseg7ff:
  case Intrinsic::riscv_vlseg8ff:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 2,
                               /*IsStore*/ false,
                               /*IsUnitStrided*/ false, /*UsePtrVal*/ true);
  case Intrinsic::riscv_vlseg2_mask:
  case Intrinsic::riscv_vlseg3_mask:
  case Intrinsic::riscv_vlseg4_mask:
  case Intrinsic::riscv_vlseg5_mask:
  case Intrinsic::riscv_vlseg6_mask:
  case Intrinsic::riscv_vlseg7_mask:
  case Intrinsic::riscv_vlseg8_mask:
  case Intrinsic::riscv_vlseg2ff_mask:
  case Intrinsic::riscv_vlseg3ff_mask:
  case Intrinsic::riscv_vlseg4ff_mask:
  case Intrinsic::riscv_vlseg5ff_mask:
  case Intrinsic::riscv_vlseg6ff_mask:
  case Intrinsic::riscv_vlseg7ff_mask:
  case Intrinsic::riscv_vlseg8ff_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 4,
                               /*IsStore*/ false,
                               /*IsUnitStrided*/ false, /*UsePtrVal*/ true);
  case Intrinsic::riscv_vlsseg2:
  case Intrinsic::riscv_vlsseg3:
  case Intrinsic::riscv_vlsseg4:
  case Intrinsic::riscv_vlsseg5:
  case Intrinsic::riscv_vlsseg6:
  case Intrinsic::riscv_vlsseg7:
  case Intrinsic::riscv_vlsseg8:
  case Intrinsic::riscv_vloxseg2:
  case Intrinsic::riscv_vloxseg3:
  case Intrinsic::riscv_vloxseg4:
  case Intrinsic::riscv_vloxseg5:
  case Intrinsic::riscv_vloxseg6:
  case Intrinsic::riscv_vloxseg7:
  case Intrinsic::riscv_vloxseg8:
  case Intrinsic::riscv_vluxseg2:
  case Intrinsic::riscv_vluxseg3:
  case Intrinsic::riscv_vluxseg4:
  case Intrinsic::riscv_vluxseg5:
  case Intrinsic::riscv_vluxseg6:
  case Intrinsic::riscv_vluxseg7:
  case Intrinsic::riscv_vluxseg8:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 3,
                               /*IsStore*/ false,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_vlsseg2_mask:
  case Intrinsic::riscv_vlsseg3_mask:
  case Intrinsic::riscv_vlsseg4_mask:
  case Intrinsic::riscv_vlsseg5_mask:
  case Intrinsic::riscv_vlsseg6_mask:
  case Intrinsic::riscv_vlsseg7_mask:
  case Intrinsic::riscv_vlsseg8_mask:
  case Intrinsic::riscv_vloxseg2_mask:
  case Intrinsic::riscv_vloxseg3_mask:
  case Intrinsic::riscv_vloxseg4_mask:
  case Intrinsic::riscv_vloxseg5_mask:
  case Intrinsic::riscv_vloxseg6_mask:
  case Intrinsic::riscv_vloxseg7_mask:
  case Intrinsic::riscv_vloxseg8_mask:
  case Intrinsic::riscv_vluxseg2_mask:
  case Intrinsic::riscv_vluxseg3_mask:
  case Intrinsic::riscv_vluxseg4_mask:
  case Intrinsic::riscv_vluxseg5_mask:
  case Intrinsic::riscv_vluxseg6_mask:
  case Intrinsic::riscv_vluxseg7_mask:
  case Intrinsic::riscv_vluxseg8_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 5,
                               /*IsStore*/ false,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_vsseg2:
  case Intrinsic::riscv_vsseg3:
  case Intrinsic::riscv_vsseg4:
  case Intrinsic::riscv_vsseg5:
  case Intrinsic::riscv_vsseg6:
  case Intrinsic::riscv_vsseg7:
  case Intrinsic::riscv_vsseg8:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 2,
                               /*IsStore*/ true,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_vsseg2_mask:
  case Intrinsic::riscv_vsseg3_mask:
  case Intrinsic::riscv_vsseg4_mask:
  case Intrinsic::riscv_vsseg5_mask:
  case Intrinsic::riscv_vsseg6_mask:
  case Intrinsic::riscv_vsseg7_mask:
  case Intrinsic::riscv_vsseg8_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 3,
                               /*IsStore*/ true,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_vssseg2:
  case Intrinsic::riscv_vssseg3:
  case Intrinsic::riscv_vssseg4:
  case Intrinsic::riscv_vssseg5:
  case Intrinsic::riscv_vssseg6:
  case Intrinsic::riscv_vssseg7:
  case Intrinsic::riscv_vssseg8:
  case Intrinsic::riscv_vsoxseg2:
  case Intrinsic::riscv_vsoxseg3:
  case Intrinsic::riscv_vsoxseg4:
  case Intrinsic::riscv_vsoxseg5:
  case Intrinsic::riscv_vsoxseg6:
  case Intrinsic::riscv_vsoxseg7:
  case Intrinsic::riscv_vsoxseg8:
  case Intrinsic::riscv_vsuxseg2:
  case Intrinsic::riscv_vsuxseg3:
  case Intrinsic::riscv_vsuxseg4:
  case Intrinsic::riscv_vsuxseg5:
  case Intrinsic::riscv_vsuxseg6:
  case Intrinsic::riscv_vsuxseg7:
  case Intrinsic::riscv_vsuxseg8:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 3,
                               /*IsStore*/ true,
                               /*IsUnitStrided*/ false);
  case Intrinsic::riscv_vssseg2_mask:
  case Intrinsic::riscv_vssseg3_mask:
  case Intrinsic::riscv_vssseg4_mask:
  case Intrinsic::riscv_vssseg5_mask:
  case Intrinsic::riscv_vssseg6_mask:
  case Intrinsic::riscv_vssseg7_mask:
  case Intrinsic::riscv_vssseg8_mask:
  case Intrinsic::riscv_vsoxseg2_mask:
  case Intrinsic::riscv_vsoxseg3_mask:
  case Intrinsic::riscv_vsoxseg4_mask:
  case Intrinsic::riscv_vsoxseg5_mask:
  case Intrinsic::riscv_vsoxseg6_mask:
  case Intrinsic::riscv_vsoxseg7_mask:
  case Intrinsic::riscv_vsoxseg8_mask:
  case Intrinsic::riscv_vsuxseg2_mask:
  case Intrinsic::riscv_vsuxseg3_mask:
  case Intrinsic::riscv_vsuxseg4_mask:
  case Intrinsic::riscv_vsuxseg5_mask:
  case Intrinsic::riscv_vsuxseg6_mask:
  case Intrinsic::riscv_vsuxseg7_mask:
  case Intrinsic::riscv_vsuxseg8_mask:
    return SetRVVLoadStoreInfo(/*PtrOp*/ I.arg_size() - 4,
                               /*IsStore*/ true,
                               /*IsUnitStrided*/ false);
  }
}

bool RISCVTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                const AddrMode &AM, Type *Ty,
                                                unsigned AS,
                                                Instruction *I) const {
  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  // RVV instructions only support register addressing.
  if (Subtarget.hasVInstructions() && isa<VectorType>(Ty))
    return AM.HasBaseReg && AM.Scale == 0 && !AM.BaseOffs;

  // Require a 12-bit signed offset.
  if (!isInt<12>(AM.BaseOffs))
    return false;

  switch (AM.Scale) {
  case 0: // "r+i" or just "i", depending on HasBaseReg.
    break;
  case 1:
    if (!AM.HasBaseReg) // allow "r+i".
      break;
    return false; // disallow "r+r" or "r+r+i".
  default:
    return false;
  }

  return true;
}

bool RISCVTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  return isInt<12>(Imm);
}

bool RISCVTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  return isInt<12>(Imm);
}

// On RV32, 64-bit integers are split into their high and low parts and held
// in two different registers, so the trunc is free since the low register can
// just be used.
// FIXME: Should we consider i64->i32 free on RV64 to match the EVT version of
// isTruncateFree?
bool RISCVTargetLowering::isTruncateFree(Type *SrcTy, Type *DstTy) const {
  if (Subtarget.is64Bit() || !SrcTy->isIntegerTy() || !DstTy->isIntegerTy())
    return false;
  unsigned SrcBits = SrcTy->getPrimitiveSizeInBits();
  unsigned DestBits = DstTy->getPrimitiveSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool RISCVTargetLowering::isTruncateFree(EVT SrcVT, EVT DstVT) const {
  // We consider i64->i32 free on RV64 since we have good selection of W
  // instructions that make promoting operations back to i64 free in many cases.
  if (SrcVT.isVector() || DstVT.isVector() || !SrcVT.isInteger() ||
      !DstVT.isInteger())
    return false;
  unsigned SrcBits = SrcVT.getSizeInBits();
  unsigned DestBits = DstVT.getSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool RISCVTargetLowering::isTruncateFree(SDValue Val, EVT VT2) const {
  EVT SrcVT = Val.getValueType();
  // free truncate from vnsrl and vnsra
  if (Subtarget.hasStdExtV() &&
      (Val.getOpcode() == ISD::SRL || Val.getOpcode() == ISD::SRA) &&
      SrcVT.isVector() && VT2.isVector()) {
    unsigned SrcBits = SrcVT.getVectorElementType().getSizeInBits();
    unsigned DestBits = VT2.getVectorElementType().getSizeInBits();
    if (SrcBits == DestBits * 2) {
      return true;
    }
  }
  return TargetLowering::isTruncateFree(Val, VT2);
}

bool RISCVTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  // Zexts are free if they can be combined with a load.
  // Don't advertise i32->i64 zextload as being free for RV64. It interacts
  // poorly with type legalization of compares preferring sext.
  if (auto *LD = dyn_cast<LoadSDNode>(Val)) {
    EVT MemVT = LD->getMemoryVT();
    if ((MemVT == MVT::i8 || MemVT == MVT::i16) &&
        (LD->getExtensionType() == ISD::NON_EXTLOAD ||
         LD->getExtensionType() == ISD::ZEXTLOAD))
      return true;
  }

  return TargetLowering::isZExtFree(Val, VT2);
}

bool RISCVTargetLowering::isSExtCheaperThanZExt(EVT SrcVT, EVT DstVT) const {
  return Subtarget.is64Bit() && SrcVT == MVT::i32 && DstVT == MVT::i64;
}

bool RISCVTargetLowering::signExtendConstant(const ConstantInt *CI) const {
  return Subtarget.is64Bit() && CI->getType()->isIntegerTy(32);
}

bool RISCVTargetLowering::isCheapToSpeculateCttz(Type *Ty) const {
  return Subtarget.hasStdExtZbb() || Subtarget.hasVendorXCVbitmanip();
}

bool RISCVTargetLowering::isCheapToSpeculateCtlz(Type *Ty) const {
  return Subtarget.hasStdExtZbb() || Subtarget.hasVendorXTHeadBb() ||
         Subtarget.hasVendorXCVbitmanip();
}

bool RISCVTargetLowering::isMaskAndCmp0FoldingBeneficial(
    const Instruction &AndI) const {
  // We expect to be able to match a bit extraction instruction if the Zbs
  // extension is supported and the mask is a power of two. However, we
  // conservatively return false if the mask would fit in an ANDI instruction,
  // on the basis that it's possible the sinking+duplication of the AND in
  // CodeGenPrepare triggered by this hook wouldn't decrease the instruction
  // count and would increase code size (e.g. ANDI+BNEZ => BEXTI+BNEZ).
  if (!Subtarget.hasStdExtZbs() && !Subtarget.hasVendorXTHeadBs())
    return false;
  ConstantInt *Mask = dyn_cast<ConstantInt>(AndI.getOperand(1));
  if (!Mask)
    return false;
  return !Mask->getValue().isSignedIntN(12) && Mask->getValue().isPowerOf2();
}

bool RISCVTargetLowering::hasAndNotCompare(SDValue Y) const {
  EVT VT = Y.getValueType();

  // FIXME: Support vectors once we have tests.
  if (VT.isVector())
    return false;

  return (Subtarget.hasStdExtZbb() || Subtarget.hasStdExtZbkb()) &&
         (!isa<ConstantSDNode>(Y) || cast<ConstantSDNode>(Y)->isOpaque());
}

bool RISCVTargetLowering::hasBitTest(SDValue X, SDValue Y) const {
  // Zbs provides BEXT[_I], which can be used with SEQZ/SNEZ as a bit test.
  if (Subtarget.hasStdExtZbs())
    return X.getValueType().isScalarInteger();
  auto *C = dyn_cast<ConstantSDNode>(Y);
  // XTheadBs provides th.tst (similar to bexti), if Y is a constant
  if (Subtarget.hasVendorXTHeadBs())
    return C != nullptr;
  // We can use ANDI+SEQZ/SNEZ as a bit test. Y contains the bit position.
  return C && C->getAPIntValue().ule(10);
}

bool RISCVTargetLowering::shouldFoldSelectWithIdentityConstant(unsigned Opcode,
                                                               EVT VT) const {
  // Only enable for rvv.
  if (!VT.isVector() || !Subtarget.hasVInstructions())
    return false;

  if (VT.isFixedLengthVector() && !isTypeLegal(VT))
    return false;

  return true;
}

bool RISCVTargetLowering::shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                            Type *Ty) const {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getIntegerBitWidth();
  if (BitSize > Subtarget.getXLen())
    return false;

  // Fast path, assume 32-bit immediates are cheap.
  int64_t Val = Imm.getSExtValue();
  if (isInt<32>(Val))
    return true;

  // A constant pool entry may be more aligned thant he load we're trying to
  // replace. If we don't support unaligned scalar mem, prefer the constant
  // pool.
  // TODO: Can the caller pass down the alignment?
  if (!Subtarget.enableUnalignedScalarMem())
    return true;

  // Prefer to keep the load if it would require many instructions.
  // This uses the same threshold we use for constant pools but doesn't
  // check useConstantPoolForLargeInts.
  // TODO: Should we keep the load only when we're definitely going to emit a
  // constant pool?

  RISCVMatInt::InstSeq Seq = RISCVMatInt::generateInstSeq(Val, Subtarget);
  return Seq.size() <= Subtarget.getMaxBuildIntsCost();
}

bool RISCVTargetLowering::
    shouldProduceAndByConstByHoistingConstFromShiftsLHSOfAnd(
        SDValue X, ConstantSDNode *XC, ConstantSDNode *CC, SDValue Y,
        unsigned OldShiftOpcode, unsigned NewShiftOpcode,
        SelectionDAG &DAG) const {
  // One interesting pattern that we'd want to form is 'bit extract':
  //   ((1 >> Y) & 1) ==/!= 0
  // But we also need to be careful not to try to reverse that fold.

  // Is this '((1 >> Y) & 1)'?
  if (XC && OldShiftOpcode == ISD::SRL && XC->isOne())
    return false; // Keep the 'bit extract' pattern.

  // Will this be '((1 >> Y) & 1)' after the transform?
  if (NewShiftOpcode == ISD::SRL && CC->isOne())
    return true; // Do form the 'bit extract' pattern.

  // If 'X' is a constant, and we transform, then we will immediately
  // try to undo the fold, thus causing endless combine loop.
  // So only do the transform if X is not a constant. This matches the default
  // implementation of this function.
  return !XC;
}

bool RISCVTargetLowering::canSplatOperand(unsigned Opcode, int Operand) const {
  switch (Opcode) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::ICmp:
  case Instruction::FCmp:
    return true;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::Select:
    return Operand == 1;
  default:
    return false;
  }
}


bool RISCVTargetLowering::canSplatOperand(Instruction *I, int Operand) const {
  if (!I->getType()->isVectorTy() || !Subtarget.hasVInstructions())
    return false;

  if (canSplatOperand(I->getOpcode(), Operand))
    return true;

  auto *II = dyn_cast<IntrinsicInst>(I);
  if (!II)
    return false;

  switch (II->getIntrinsicID()) {
  case Intrinsic::fma:
  case Intrinsic::vp_fma:
    return Operand == 0 || Operand == 1;
  case Intrinsic::vp_shl:
  case Intrinsic::vp_lshr:
  case Intrinsic::vp_ashr:
  case Intrinsic::vp_udiv:
  case Intrinsic::vp_sdiv:
  case Intrinsic::vp_urem:
  case Intrinsic::vp_srem:
  case Intrinsic::ssub_sat:
  case Intrinsic::vp_ssub_sat:
  case Intrinsic::usub_sat:
  case Intrinsic::vp_usub_sat:
    return Operand == 1;
    // These intrinsics are commutative.
  case Intrinsic::vp_add:
  case Intrinsic::vp_mul:
  case Intrinsic::vp_and:
  case Intrinsic::vp_or:
  case Intrinsic::vp_xor:
  case Intrinsic::vp_fadd:
  case Intrinsic::vp_fmul:
  case Intrinsic::vp_icmp:
  case Intrinsic::vp_fcmp:
  case Intrinsic::smin:
  case Intrinsic::vp_smin:
  case Intrinsic::umin:
  case Intrinsic::vp_umin:
  case Intrinsic::smax:
  case Intrinsic::vp_smax:
  case Intrinsic::umax:
  case Intrinsic::vp_umax:
  case Intrinsic::sadd_sat:
  case Intrinsic::vp_sadd_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::vp_uadd_sat:
    // These intrinsics have 'vr' versions.
  case Intrinsic::vp_sub:
  case Intrinsic::vp_fsub:
  case Intrinsic::vp_fdiv:
    return Operand == 0 || Operand == 1;
  default:
    return false;
  }
}

/// Check if sinking \p I's operands to I's basic block is profitable, because
/// the operands can be folded into a target instruction, e.g.
/// splats of scalars can fold into vector instructions.
bool RISCVTargetLowering::shouldSinkOperands(
    Instruction *I, SmallVectorImpl<Use *> &Ops) const {
  using namespace llvm::PatternMatch;

  if (!I->getType()->isVectorTy() || !Subtarget.hasVInstructions())
    return false;

  // Don't sink splat operands if the target prefers it. Some targets requires
  // S2V transfer buffers and we can run out of them copying the same value
  // repeatedly.
  // FIXME: It could still be worth doing if it would improve vector register
  // pressure and prevent a vector spill.
  if (!Subtarget.sinkSplatOperands())
    return false;

  for (auto OpIdx : enumerate(I->operands())) {
    if (!canSplatOperand(I, OpIdx.index()))
      continue;

    Instruction *Op = dyn_cast<Instruction>(OpIdx.value().get());
    // Make sure we are not already sinking this operand
    if (!Op || any_of(Ops, [&](Use *U) { return U->get() == Op; }))
      continue;

    // We are looking for a splat that can be sunk.
    if (!match(Op, m_Shuffle(m_InsertElt(m_Undef(), m_Value(), m_ZeroInt()),
                             m_Undef(), m_ZeroMask())))
      continue;

    // Don't sink i1 splats.
    if (cast<VectorType>(Op->getType())->getElementType()->isIntegerTy(1))
      continue;

    // All uses of the shuffle should be sunk to avoid duplicating it across gpr
    // and vector registers
    for (Use &U : Op->uses()) {
      Instruction *Insn = cast<Instruction>(U.getUser());
      if (!canSplatOperand(Insn, U.getOperandNo()))
        return false;
    }

    Ops.push_back(&Op->getOperandUse(0));
    Ops.push_back(&OpIdx.value());
  }
  return true;
}

bool RISCVTargetLowering::shouldScalarizeBinop(SDValue VecOp) const {
  unsigned Opc = VecOp.getOpcode();

  // Assume target opcodes can't be scalarized.
  // TODO - do we have any exceptions?
  if (Opc >= ISD::BUILTIN_OP_END)
    return false;

  // If the vector op is not supported, try to convert to scalar.
  EVT VecVT = VecOp.getValueType();
  if (!isOperationLegalOrCustomOrPromote(Opc, VecVT))
    return true;

  // If the vector op is supported, but the scalar op is not, the transform may
  // not be worthwhile.
  // Permit a vector binary operation can be converted to scalar binary
  // operation which is custom lowered with illegal type.
  EVT ScalarVT = VecVT.getScalarType();
  return isOperationLegalOrCustomOrPromote(Opc, ScalarVT) ||
         isOperationCustom(Opc, ScalarVT);
}

bool RISCVTargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  // In order to maximise the opportunity for common subexpression elimination,
  // keep a separate ADD node for the global address offset instead of folding
  // it in the global address node. Later peephole optimisations may choose to
  // fold it back in when profitable.
  return false;
}

// Return one of the followings:
// (1) `{0-31 value, false}` if FLI is available for Imm's type and FP value.
// (2) `{0-31 value, true}` if Imm is negative and FLI is available for its
// positive counterpart, which will be materialized from the first returned
// element. The second returned element indicated that there should be a FNEG
// followed.
// (3) `{-1, _}` if there is no way FLI can be used to materialize Imm.
std::pair<int, bool> RISCVTargetLowering::getLegalZfaFPImm(const APFloat &Imm,
                                                           EVT VT) const {
  if (!Subtarget.hasStdExtZfa())
    return std::make_pair(-1, false);

  bool IsSupportedVT = false;
  if (VT == MVT::f16) {
    IsSupportedVT = Subtarget.hasStdExtZfh() || Subtarget.hasStdExtZvfh();
  } else if (VT == MVT::f32) {
    IsSupportedVT = true;
  } else if (VT == MVT::f64) {
    assert(Subtarget.hasStdExtD() && "Expect D extension");
    IsSupportedVT = true;
  }

  if (!IsSupportedVT)
    return std::make_pair(-1, false);

  int Index = RISCVLoadFPImm::getLoadFPImm(Imm);
  if (Index < 0 && Imm.isNegative())
    // Try the combination of its positive counterpart + FNEG.
    return std::make_pair(RISCVLoadFPImm::getLoadFPImm(-Imm), true);
  else
    return std::make_pair(Index, false);
}

bool RISCVTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT,
                                       bool ForCodeSize) const {
  bool IsLegalVT = false;
  if (VT == MVT::f16)
    IsLegalVT = Subtarget.hasStdExtZfhminOrZhinxmin();
  else if (VT == MVT::f32)
    IsLegalVT = Subtarget.hasStdExtFOrZfinx();
  else if (VT == MVT::f64)
    IsLegalVT = Subtarget.hasStdExtDOrZdinx();
  else if (VT == MVT::bf16)
    IsLegalVT = Subtarget.hasStdExtZfbfmin();

  if (!IsLegalVT)
    return false;

  if (getLegalZfaFPImm(Imm, VT).first >= 0)
    return true;

  // Cannot create a 64 bit floating-point immediate value for rv32.
  if (Subtarget.getXLen() < VT.getScalarSizeInBits()) {
    // td can handle +0.0 or -0.0 already.
    // -0.0 can be created by fmv + fneg.
    return Imm.isZero();
  }

  // Special case: fmv + fneg
  if (Imm.isNegZero())
    return true;

  // Building an integer and then converting requires a fmv at the end of
  // the integer sequence.
  const int Cost =
      1 + RISCVMatInt::getIntMatCost(Imm.bitcastToAPInt(), Subtarget.getXLen(),
                                     Subtarget);
  return Cost <= FPImmCost;
}

// TODO: This is very conservative.
bool RISCVTargetLowering::isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                                  unsigned Index) const {
  if (!isOperationLegalOrCustom(ISD::EXTRACT_SUBVECTOR, ResVT))
    return false;

  // Only support extracting a fixed from a fixed vector for now.
  if (ResVT.isScalableVector() || SrcVT.isScalableVector())
    return false;

  EVT EltVT = ResVT.getVectorElementType();
  assert(EltVT == SrcVT.getVectorElementType() && "Should hold for node");

  // The smallest type we can slide is i8.
  // TODO: We can extract index 0 from a mask vector without a slide.
  if (EltVT == MVT::i1)
    return false;

  unsigned ResElts = ResVT.getVectorNumElements();
  unsigned SrcElts = SrcVT.getVectorNumElements();

  unsigned MinVLen = Subtarget.getRealMinVLen();
  unsigned MinVLMAX = MinVLen / EltVT.getSizeInBits();

  // If we're extracting only data from the first VLEN bits of the source
  // then we can always do this with an m1 vslidedown.vx.  Restricting the
  // Index ensures we can use a vslidedown.vi.
  // TODO: We can generalize this when the exact VLEN is known.
  if (Index + ResElts <= MinVLMAX && Index < 31)
    return true;

  // Convervatively only handle extracting half of a vector.
  // TODO: For sizes which aren't multiples of VLEN sizes, this may not be
  // a cheap extract.  However, this case is important in practice for
  // shuffled extracts of longer vectors.  How resolve?
  if ((ResElts * 2) != SrcElts)
    return false;

  // Slide can support arbitrary index, but we only treat vslidedown.vi as
  // cheap.
  if (Index >= 32)
    return false;

  // TODO: We can do arbitrary slidedowns, but for now only support extracting
  // the upper half of a vector until we have more test coverage.
  return Index == 0 || Index == ResElts;
}

MVT RISCVTargetLowering::getRegisterTypeForCallingConv(LLVMContext &Context,
                                                      CallingConv::ID CC,
                                                      EVT VT) const {
  // Use f32 to pass f16 if it is legal and Zfh/Zfhmin is not enabled.
  // We might still end up using a GPR but that will be decided based on ABI.
  if (VT == MVT::f16 && Subtarget.hasStdExtFOrZfinx() &&
      !Subtarget.hasStdExtZfhminOrZhinxmin())
    return MVT::f32;

  MVT PartVT = TargetLowering::getRegisterTypeForCallingConv(Context, CC, VT);

  if (RV64LegalI32 && Subtarget.is64Bit() && PartVT == MVT::i32)
    return MVT::i64;

  return PartVT;
}

unsigned RISCVTargetLowering::getNumRegistersForCallingConv(LLVMContext &Context,
                                                           CallingConv::ID CC,
                                                           EVT VT) const {
  // Use f32 to pass f16 if it is legal and Zfh/Zfhmin is not enabled.
  // We might still end up using a GPR but that will be decided based on ABI.
  if (VT == MVT::f16 && Subtarget.hasStdExtFOrZfinx() &&
      !Subtarget.hasStdExtZfhminOrZhinxmin())
    return 1;

  return TargetLowering::getNumRegistersForCallingConv(Context, CC, VT);
}

unsigned RISCVTargetLowering::getVectorTypeBreakdownForCallingConv(
    LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
    unsigned &NumIntermediates, MVT &RegisterVT) const {
  unsigned NumRegs = TargetLowering::getVectorTypeBreakdownForCallingConv(
      Context, CC, VT, IntermediateVT, NumIntermediates, RegisterVT);

  if (RV64LegalI32 && Subtarget.is64Bit() && IntermediateVT == MVT::i32)
    IntermediateVT = MVT::i64;

  if (RV64LegalI32 && Subtarget.is64Bit() && RegisterVT == MVT::i32)
    RegisterVT = MVT::i64;

  return NumRegs;
}

// Changes the condition code and swaps operands if necessary, so the SetCC
// operation matches one of the comparisons supported directly by branches
// in the RISC-V ISA. May adjust compares to favor compare with 0 over compare
// with 1/-1.
static void translateSetCCForBranch(const SDLoc &DL, SDValue &LHS, SDValue &RHS,
                                    ISD::CondCode &CC, SelectionDAG &DAG) {
  // If this is a single bit test that can't be handled by ANDI, shift the
  // bit to be tested to the MSB and perform a signed compare with 0.
  if (isIntEqualitySetCC(CC) && isNullConstant(RHS) &&
      LHS.getOpcode() == ISD::AND && LHS.hasOneUse() &&
      isa<ConstantSDNode>(LHS.getOperand(1))) {
    uint64_t Mask = LHS.getConstantOperandVal(1);
    if ((isPowerOf2_64(Mask) || isMask_64(Mask)) && !isInt<12>(Mask)) {
      unsigned ShAmt = 0;
      if (isPowerOf2_64(Mask)) {
        CC = CC == ISD::SETEQ ? ISD::SETGE : ISD::SETLT;
        ShAmt = LHS.getValueSizeInBits() - 1 - Log2_64(Mask);
      } else {
        ShAmt = LHS.getValueSizeInBits() - llvm::bit_width(Mask);
      }

      LHS = LHS.getOperand(0);
      if (ShAmt != 0)
        LHS = DAG.getNode(ISD::SHL, DL, LHS.getValueType(), LHS,
                          DAG.getConstant(ShAmt, DL, LHS.getValueType()));
      return;
    }
  }

  if (auto *RHSC = dyn_cast<ConstantSDNode>(RHS)) {
    int64_t C = RHSC->getSExtValue();
    switch (CC) {
    default: break;
    case ISD::SETGT:
      // Convert X > -1 to X >= 0.
      if (C == -1) {
        RHS = DAG.getConstant(0, DL, RHS.getValueType());
        CC = ISD::SETGE;
        return;
      }
      break;
    case ISD::SETLT:
      // Convert X < 1 to 0 >= X.
      if (C == 1) {
        RHS = LHS;
        LHS = DAG.getConstant(0, DL, RHS.getValueType());
        CC = ISD::SETGE;
        return;
      }
      break;
    }
  }

  switch (CC) {
  default:
    break;
  case ISD::SETGT:
  case ISD::SETLE:
  case ISD::SETUGT:
  case ISD::SETULE:
    CC = ISD::getSetCCSwappedOperands(CC);
    std::swap(LHS, RHS);
    break;
  }
}

RISCVII::VLMUL RISCVTargetLowering::getLMUL(MVT VT) {
  assert(VT.isScalableVector() && "Expecting a scalable vector type");
  unsigned KnownSize = VT.getSizeInBits().getKnownMinValue();
  if (VT.getVectorElementType() == MVT::i1)
    KnownSize *= 8;

  switch (KnownSize) {
  default:
    llvm_unreachable("Invalid LMUL.");
  case 8:
    return RISCVII::VLMUL::LMUL_F8;
  case 16:
    return RISCVII::VLMUL::LMUL_F4;
  case 32:
    return RISCVII::VLMUL::LMUL_F2;
  case 64:
    return RISCVII::VLMUL::LMUL_1;
  case 128:
    return RISCVII::VLMUL::LMUL_2;
  case 256:
    return RISCVII::VLMUL::LMUL_4;
  case 512:
    return RISCVII::VLMUL::LMUL_8;
  }
}

unsigned RISCVTargetLowering::getRegClassIDForLMUL(RISCVII::VLMUL LMul) {
  switch (LMul) {
  default:
    llvm_unreachable("Invalid LMUL.");
  case RISCVII::VLMUL::LMUL_F8:
  case RISCVII::VLMUL::LMUL_F4:
  case RISCVII::VLMUL::LMUL_F2:
  case RISCVII::VLMUL::LMUL_1:
    return RISCV::VRRegClassID;
  case RISCVII::VLMUL::LMUL_2:
    return RISCV::VRM2RegClassID;
  case RISCVII::VLMUL::LMUL_4:
    return RISCV::VRM4RegClassID;
  case RISCVII::VLMUL::LMUL_8:
    return RISCV::VRM8RegClassID;
  }
}

unsigned RISCVTargetLowering::getSubregIndexByMVT(MVT VT, unsigned Index) {
  RISCVII::VLMUL LMUL = getLMUL(VT);
  if (LMUL == RISCVII::VLMUL::LMUL_F8 ||
      LMUL == RISCVII::VLMUL::LMUL_F4 ||
      LMUL == RISCVII::VLMUL::LMUL_F2 ||
      LMUL == RISCVII::VLMUL::LMUL_1) {
    static_assert(RISCV::sub_vrm1_7 == RISCV::sub_vrm1_0 + 7,
                  "Unexpected subreg numbering");
    return RISCV::sub_vrm1_0 + Index;
  }
  if (LMUL == RISCVII::VLMUL::LMUL_2) {
    static_assert(RISCV::sub_vrm2_3 == RISCV::sub_vrm2_0 + 3,
                  "Unexpected subreg numbering");
    return RISCV::sub_vrm2_0 + Index;
  }
  if (LMUL == RISCVII::VLMUL::LMUL_4) {
    static_assert(RISCV::sub_vrm4_1 == RISCV::sub_vrm4_0 + 1,
                  "Unexpected subreg numbering");
    return RISCV::sub_vrm4_0 + Index;
  }
  llvm_unreachable("Invalid vector type.");
}

unsigned RISCVTargetLowering::getRegClassIDForVecVT(MVT VT) {
  if (VT.getVectorElementType() == MVT::i1)
    return RISCV::VRRegClassID;
  return getRegClassIDForLMUL(getLMUL(VT));
}

// Attempt to decompose a subvector insert/extract between VecVT and
// SubVecVT via subregister indices. Returns the subregister index that
// can perform the subvector insert/extract with the given element index, as
// well as the index corresponding to any leftover subvectors that must be
// further inserted/extracted within the register class for SubVecVT.
std::pair<unsigned, unsigned>
RISCVTargetLowering::decomposeSubvectorInsertExtractToSubRegs(
    MVT VecVT, MVT SubVecVT, unsigned InsertExtractIdx,
    const RISCVRegisterInfo *TRI) {
  static_assert((RISCV::VRM8RegClassID > RISCV::VRM4RegClassID &&
                 RISCV::VRM4RegClassID > RISCV::VRM2RegClassID &&
                 RISCV::VRM2RegClassID > RISCV::VRRegClassID),
                "Register classes not ordered");
  unsigned VecRegClassID = getRegClassIDForVecVT(VecVT);
  unsigned SubRegClassID = getRegClassIDForVecVT(SubVecVT);
  // Try to compose a subregister index that takes us from the incoming
  // LMUL>1 register class down to the outgoing one. At each step we half
  // the LMUL:
  //   nxv16i32@12 -> nxv2i32: sub_vrm4_1_then_sub_vrm2_1_then_sub_vrm1_0
  // Note that this is not guaranteed to find a subregister index, such as
  // when we are extracting from one VR type to another.
  unsigned SubRegIdx = RISCV::NoSubRegister;
  for (const unsigned RCID :
       {RISCV::VRM4RegClassID, RISCV::VRM2RegClassID, RISCV::VRRegClassID})
    if (VecRegClassID > RCID && SubRegClassID <= RCID) {
      VecVT = VecVT.getHalfNumVectorElementsVT();
      bool IsHi =
          InsertExtractIdx >= VecVT.getVectorElementCount().getKnownMinValue();
      SubRegIdx = TRI->composeSubRegIndices(SubRegIdx,
                                            getSubregIndexByMVT(VecVT, IsHi));
      if (IsHi)
        InsertExtractIdx -= VecVT.getVectorElementCount().getKnownMinValue();
    }
  return {SubRegIdx, InsertExtractIdx};
}

// Permit combining of mask vectors as BUILD_VECTOR never expands to scalar
// stores for those types.
bool RISCVTargetLowering::mergeStoresAfterLegalization(EVT VT) const {
  return !Subtarget.useRVVForFixedLengthVectors() ||
         (VT.isFixedLengthVector() && VT.getVectorElementType() == MVT::i1);
}

bool RISCVTargetLowering::isLegalElementTypeForRVV(EVT ScalarTy) const {
  if (!ScalarTy.isSimple())
    return false;
  switch (ScalarTy.getSimpleVT().SimpleTy) {
  case MVT::iPTR:
    return Subtarget.is64Bit() ? Subtarget.hasVInstructionsI64() : true;
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
    return true;
  case MVT::i64:
    return Subtarget.hasVInstructionsI64();
  case MVT::f16:
    return Subtarget.hasVInstructionsF16();
  case MVT::f32:
    return Subtarget.hasVInstructionsF32();
  case MVT::f64:
    return Subtarget.hasVInstructionsF64();
  default:
    return false;
  }
}


unsigned RISCVTargetLowering::combineRepeatedFPDivisors() const {
  return NumRepeatedDivisors;
}

static SDValue getVLOperand(SDValue Op) {
  assert((Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN) &&
         "Unexpected opcode");
  bool HasChain = Op.getOpcode() == ISD::INTRINSIC_W_CHAIN;
  unsigned IntNo = Op.getConstantOperandVal(HasChain ? 1 : 0);
  const RISCVVIntrinsicsTable::RISCVVIntrinsicInfo *II =
      RISCVVIntrinsicsTable::getRISCVVIntrinsicInfo(IntNo);
  if (!II)
    return SDValue();
  return Op.getOperand(II->VLOperand + 1 + HasChain);
}

static bool useRVVForFixedLengthVectorVT(MVT VT,
                                         const RISCVSubtarget &Subtarget) {
  assert(VT.isFixedLengthVector() && "Expected a fixed length vector type!");
  if (!Subtarget.useRVVForFixedLengthVectors())
    return false;

  // We only support a set of vector types with a consistent maximum fixed size
  // across all supported vector element types to avoid legalization issues.
  // Therefore -- since the largest is v1024i8/v512i16/etc -- the largest
  // fixed-length vector type we support is 1024 bytes.
  if (VT.getFixedSizeInBits() > 1024 * 8)
    return false;

  unsigned MinVLen = Subtarget.getRealMinVLen();

  MVT EltVT = VT.getVectorElementType();

  // Don't use RVV for vectors we cannot scalarize if required.
  switch (EltVT.SimpleTy) {
  // i1 is supported but has different rules.
  default:
    return false;
  case MVT::i1:
    // Masks can only use a single register.
    if (VT.getVectorNumElements() > MinVLen)
      return false;
    MinVLen /= 8;
    break;
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
    break;
  case MVT::i64:
    if (!Subtarget.hasVInstructionsI64())
      return false;
    break;
  case MVT::f16:
    if (!Subtarget.hasVInstructionsF16Minimal())
      return false;
    break;
  case MVT::bf16:
    if (!Subtarget.hasVInstructionsBF16())
      return false;
    break;
  case MVT::f32:
    if (!Subtarget.hasVInstructionsF32())
      return false;
    break;
  case MVT::f64:
    if (!Subtarget.hasVInstructionsF64())
      return false;
    break;
  }

  // Reject elements larger than ELEN.
  if (EltVT.getSizeInBits() > Subtarget.getELen())
    return false;

  unsigned LMul = divideCeil(VT.getSizeInBits(), MinVLen);
  // Don't use RVV for types that don't fit.
  if (LMul > Subtarget.getMaxLMULForFixedLengthVectors())
    return false;

  // TODO: Perhaps an artificial restriction, but worth having whilst getting
  // the base fixed length RVV support in place.
  if (!VT.isPow2VectorType())
    return false;

  return true;
}

bool RISCVTargetLowering::useRVVForFixedLengthVectorVT(MVT VT) const {
  return ::useRVVForFixedLengthVectorVT(VT, Subtarget);
}

// Return the largest legal scalable vector type that matches VT's element type.
static MVT getContainerForFixedLengthVector(const TargetLowering &TLI, MVT VT,
                                            const RISCVSubtarget &Subtarget) {
  // This may be called before legal types are setup.
  assert(((VT.isFixedLengthVector() && TLI.isTypeLegal(VT)) ||
          useRVVForFixedLengthVectorVT(VT, Subtarget)) &&
         "Expected legal fixed length vector!");

  unsigned MinVLen = Subtarget.getRealMinVLen();
  unsigned MaxELen = Subtarget.getELen();

  MVT EltVT = VT.getVectorElementType();
  switch (EltVT.SimpleTy) {
  default:
    llvm_unreachable("unexpected element type for RVV container");
  case MVT::i1:
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
  case MVT::i64:
  case MVT::bf16:
  case MVT::f16:
  case MVT::f32:
  case MVT::f64: {
    // We prefer to use LMUL=1 for VLEN sized types. Use fractional lmuls for
    // narrower types. The smallest fractional LMUL we support is 8/ELEN. Within
    // each fractional LMUL we support SEW between 8 and LMUL*ELEN.
    unsigned NumElts =
        (VT.getVectorNumElements() * RISCV::RVVBitsPerBlock) / MinVLen;
    NumElts = std::max(NumElts, RISCV::RVVBitsPerBlock / MaxELen);
    assert(isPowerOf2_32(NumElts) && "Expected power of 2 NumElts");
    return MVT::getScalableVectorVT(EltVT, NumElts);
  }
  }
}

static MVT getContainerForFixedLengthVector(SelectionDAG &DAG, MVT VT,
                                            const RISCVSubtarget &Subtarget) {
  return getContainerForFixedLengthVector(DAG.getTargetLoweringInfo(), VT,
                                          Subtarget);
}

MVT RISCVTargetLowering::getContainerForFixedLengthVector(MVT VT) const {
  return ::getContainerForFixedLengthVector(*this, VT, getSubtarget());
}

// Grow V to consume an entire RVV register.
static SDValue convertToScalableVector(EVT VT, SDValue V, SelectionDAG &DAG,
                                       const RISCVSubtarget &Subtarget) {
  assert(VT.isScalableVector() &&
         "Expected to convert into a scalable vector!");
  assert(V.getValueType().isFixedLengthVector() &&
         "Expected a fixed length vector operand!");
  SDLoc DL(V);
  SDValue Zero = DAG.getVectorIdxConstant(0, DL);
  return DAG.getNode(ISD::INSERT_SUBVECTOR, DL, VT, DAG.getUNDEF(VT), V, Zero);
}

// Shrink V so it's just big enough to maintain a VT's worth of data.
static SDValue convertFromScalableVector(EVT VT, SDValue V, SelectionDAG &DAG,
                                         const RISCVSubtarget &Subtarget) {
  assert(VT.isFixedLengthVector() &&
         "Expected to convert into a fixed length vector!");
  assert(V.getValueType().isScalableVector() &&
         "Expected a scalable vector operand!");
  SDLoc DL(V);
  SDValue Zero = DAG.getConstant(0, DL, Subtarget.getXLenVT());
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, V, Zero);
}

/// Return the type of the mask type suitable for masking the provided
/// vector type.  This is simply an i1 element type vector of the same
/// (possibly scalable) length.
static MVT getMaskTypeFor(MVT VecVT) {
  assert(VecVT.isVector());
  ElementCount EC = VecVT.getVectorElementCount();
  return MVT::getVectorVT(MVT::i1, EC);
}

/// Creates an all ones mask suitable for masking a vector of type VecTy with
/// vector length VL.  .
static SDValue getAllOnesMask(MVT VecVT, SDValue VL, const SDLoc &DL,
                              SelectionDAG &DAG) {
  MVT MaskVT = getMaskTypeFor(VecVT);
  return DAG.getNode(RISCVISD::VMSET_VL, DL, MaskVT, VL);
}

static SDValue getVLOp(uint64_t NumElts, MVT ContainerVT, const SDLoc &DL,
                       SelectionDAG &DAG, const RISCVSubtarget &Subtarget) {
  // If we know the exact VLEN, and our VL is exactly equal to VLMAX,
  // canonicalize the representation.  InsertVSETVLI will pick the immediate
  // encoding later if profitable.
  const auto [MinVLMAX, MaxVLMAX] =
      RISCVTargetLowering::computeVLMAXBounds(ContainerVT, Subtarget);
  if (MinVLMAX == MaxVLMAX && NumElts == MinVLMAX)
    return DAG.getRegister(RISCV::X0, Subtarget.getXLenVT());

  return DAG.getConstant(NumElts, DL, Subtarget.getXLenVT());
}

static std::pair<SDValue, SDValue>
getDefaultScalableVLOps(MVT VecVT, const SDLoc &DL, SelectionDAG &DAG,
                        const RISCVSubtarget &Subtarget) {
  assert(VecVT.isScalableVector() && "Expecting a scalable vector");
  SDValue VL = DAG.getRegister(RISCV::X0, Subtarget.getXLenVT());
  SDValue Mask = getAllOnesMask(VecVT, VL, DL, DAG);
  return {Mask, VL};
}

static std::pair<SDValue, SDValue>
getDefaultVLOps(uint64_t NumElts, MVT ContainerVT, const SDLoc &DL,
                SelectionDAG &DAG, const RISCVSubtarget &Subtarget) {
  assert(ContainerVT.isScalableVector() && "Expecting scalable container type");
  SDValue VL = getVLOp(NumElts, ContainerVT, DL, DAG, Subtarget);
  SDValue Mask = getAllOnesMask(ContainerVT, VL, DL, DAG);
  return {Mask, VL};
}

// Gets the two common "VL" operands: an all-ones mask and the vector length.
// VecVT is a vector type, either fixed-length or scalable, and ContainerVT is
// the vector type that the fixed-length vector is contained in. Otherwise if
// VecVT is scalable, then ContainerVT should be the same as VecVT.
static std::pair<SDValue, SDValue>
getDefaultVLOps(MVT VecVT, MVT ContainerVT, const SDLoc &DL, SelectionDAG &DAG,
                const RISCVSubtarget &Subtarget) {
  if (VecVT.isFixedLengthVector())
    return getDefaultVLOps(VecVT.getVectorNumElements(), ContainerVT, DL, DAG,
                           Subtarget);
  assert(ContainerVT.isScalableVector() && "Expecting scalable container type");
  return getDefaultScalableVLOps(ContainerVT, DL, DAG, Subtarget);
}

SDValue RISCVTargetLowering::computeVLMax(MVT VecVT, const SDLoc &DL,
                                          SelectionDAG &DAG) const {
  assert(VecVT.isScalableVector() && "Expected scalable vector");
  return DAG.getElementCount(DL, Subtarget.getXLenVT(),
                             VecVT.getVectorElementCount());
}

std::pair<unsigned, unsigned>
RISCVTargetLowering::computeVLMAXBounds(MVT VecVT,
                                        const RISCVSubtarget &Subtarget) {
  assert(VecVT.isScalableVector() && "Expected scalable vector");

  unsigned EltSize = VecVT.getScalarSizeInBits();
  unsigned MinSize = VecVT.getSizeInBits().getKnownMinValue();

  unsigned VectorBitsMax = Subtarget.getRealMaxVLen();
  unsigned MaxVLMAX =
      RISCVTargetLowering::computeVLMAX(VectorBitsMax, EltSize, MinSize);

  unsigned VectorBitsMin = Subtarget.getRealMinVLen();
  unsigned MinVLMAX =
      RISCVTargetLowering::computeVLMAX(VectorBitsMin, EltSize, MinSize);

  return std::make_pair(MinVLMAX, MaxVLMAX);
}

// The state of RVV BUILD_VECTOR and VECTOR_SHUFFLE lowering is that very few
// of either is (currently) supported. This can get us into an infinite loop
// where we try to lower a BUILD_VECTOR as a VECTOR_SHUFFLE as a BUILD_VECTOR
// as a ..., etc.
// Until either (or both) of these can reliably lower any node, reporting that
// we don't want to expand BUILD_VECTORs via VECTOR_SHUFFLEs at least breaks
// the infinite loop. Note that this lowers BUILD_VECTOR through the stack,
// which is not desirable.
bool RISCVTargetLowering::shouldExpandBuildVectorWithShuffles(
    EVT VT, unsigned DefinedValues) const {
  return false;
}

InstructionCost RISCVTargetLowering::getLMULCost(MVT VT) const {
  // TODO: Here assume reciprocal throughput is 1 for LMUL_1, it is
  // implementation-defined.
  if (!VT.isVector())
    return InstructionCost::getInvalid();
  unsigned DLenFactor = Subtarget.getDLenFactor();
  unsigned Cost;
  if (VT.isScalableVector()) {
    unsigned LMul;
    bool Fractional;
    std::tie(LMul, Fractional) =
        RISCVVType::decodeVLMUL(RISCVTargetLowering::getLMUL(VT));
    if (Fractional)
      Cost = LMul <= DLenFactor ? (DLenFactor / LMul) : 1;
    else
      Cost = (LMul * DLenFactor);
  } else {
    Cost = divideCeil(VT.getSizeInBits(), Subtarget.getRealMinVLen() / DLenFactor);
  }
  return Cost;
}


/// Return the cost of a vrgather.vv instruction for the type VT.  vrgather.vv
/// is generally quadratic in the number of vreg implied by LMUL.  Note that
/// operand (index and possibly mask) are handled separately.
InstructionCost RISCVTargetLowering::getVRGatherVVCost(MVT VT) const {
  return getLMULCost(VT) * getLMULCost(VT);
}

/// Return the cost of a vrgather.vi (or vx) instruction for the type VT.
/// vrgather.vi/vx may be linear in the number of vregs implied by LMUL,
/// or may track the vrgather.vv cost. It is implementation-dependent.
InstructionCost RISCVTargetLowering::getVRGatherVICost(MVT VT) const {
  return getLMULCost(VT);
}

/// Return the cost of a vslidedown.vx or vslideup.vx instruction
/// for the type VT.  (This does not cover the vslide1up or vslide1down
/// variants.)  Slides may be linear in the number of vregs implied by LMUL,
/// or may track the vrgather.vv cost. It is implementation-dependent.
InstructionCost RISCVTargetLowering::getVSlideVXCost(MVT VT) const {
  return getLMULCost(VT);
}

/// Return the cost of a vslidedown.vi or vslideup.vi instruction
/// for the type VT.  (This does not cover the vslide1up or vslide1down
/// variants.)  Slides may be linear in the number of vregs implied by LMUL,
/// or may track the vrgather.vv cost. It is implementation-dependent.
InstructionCost RISCVTargetLowering::getVSlideVICost(MVT VT) const {
  return getLMULCost(VT);
}

static SDValue lowerFP_TO_INT_SAT(SDValue Op, SelectionDAG &DAG,
                                  const RISCVSubtarget &Subtarget) {
  // RISC-V FP-to-int conversions saturate to the destination register size, but
  // don't produce 0 for nan. We can use a conversion instruction and fix the
  // nan case with a compare and a select.
  SDValue Src = Op.getOperand(0);

  MVT DstVT = Op.getSimpleValueType();
  EVT SatVT = cast<VTSDNode>(Op.getOperand(1))->getVT();

  bool IsSigned = Op.getOpcode() == ISD::FP_TO_SINT_SAT;

  if (!DstVT.isVector()) {
    // For bf16 or for f16 in absense of Zfh, promote to f32, then saturate
    // the result.
    if ((Src.getValueType() == MVT::f16 && !Subtarget.hasStdExtZfhOrZhinx()) ||
        Src.getValueType() == MVT::bf16) {
      Src = DAG.getNode(ISD::FP_EXTEND, SDLoc(Op), MVT::f32, Src);
    }

    unsigned Opc;
    if (SatVT == DstVT)
      Opc = IsSigned ? RISCVISD::FCVT_X : RISCVISD::FCVT_XU;
    else if (DstVT == MVT::i64 && SatVT == MVT::i32)
      Opc = IsSigned ? RISCVISD::FCVT_W_RV64 : RISCVISD::FCVT_WU_RV64;
    else
      return SDValue();
    // FIXME: Support other SatVTs by clamping before or after the conversion.

    SDLoc DL(Op);
    SDValue FpToInt = DAG.getNode(
        Opc, DL, DstVT, Src,
        DAG.getTargetConstant(RISCVFPRndMode::RTZ, DL, Subtarget.getXLenVT()));

    if (Opc == RISCVISD::FCVT_WU_RV64)
      FpToInt = DAG.getZeroExtendInReg(FpToInt, DL, MVT::i32);

    SDValue ZeroInt = DAG.getConstant(0, DL, DstVT);
    return DAG.getSelectCC(DL, Src, Src, ZeroInt, FpToInt,
                           ISD::CondCode::SETUO);
  }

  // Vectors.

  MVT DstEltVT = DstVT.getVectorElementType();
  MVT SrcVT = Src.getSimpleValueType();
  MVT SrcEltVT = SrcVT.getVectorElementType();
  unsigned SrcEltSize = SrcEltVT.getSizeInBits();
  unsigned DstEltSize = DstEltVT.getSizeInBits();

  // Only handle saturating to the destination type.
  if (SatVT != DstEltVT)
    return SDValue();

  // FIXME: Don't support narrowing by more than 1 steps for now.
  if (SrcEltSize > (2 * DstEltSize))
    return SDValue();

  MVT DstContainerVT = DstVT;
  MVT SrcContainerVT = SrcVT;
  if (DstVT.isFixedLengthVector()) {
    DstContainerVT = getContainerForFixedLengthVector(DAG, DstVT, Subtarget);
    SrcContainerVT = getContainerForFixedLengthVector(DAG, SrcVT, Subtarget);
    assert(DstContainerVT.getVectorElementCount() ==
               SrcContainerVT.getVectorElementCount() &&
           "Expected same element count");
    Src = convertToScalableVector(SrcContainerVT, Src, DAG, Subtarget);
  }

  SDLoc DL(Op);

  auto [Mask, VL] = getDefaultVLOps(DstVT, DstContainerVT, DL, DAG, Subtarget);

  SDValue IsNan = DAG.getNode(RISCVISD::SETCC_VL, DL, Mask.getValueType(),
                              {Src, Src, DAG.getCondCode(ISD::SETNE),
                               DAG.getUNDEF(Mask.getValueType()), Mask, VL});

  // Need to widen by more than 1 step, promote the FP type, then do a widening
  // convert.
  if (DstEltSize > (2 * SrcEltSize)) {
    assert(SrcContainerVT.getVectorElementType() == MVT::f16 && "Unexpected VT!");
    MVT InterVT = SrcContainerVT.changeVectorElementType(MVT::f32);
    Src = DAG.getNode(RISCVISD::FP_EXTEND_VL, DL, InterVT, Src, Mask, VL);
  }

  unsigned RVVOpc =
      IsSigned ? RISCVISD::VFCVT_RTZ_X_F_VL : RISCVISD::VFCVT_RTZ_XU_F_VL;
  SDValue Res = DAG.getNode(RVVOpc, DL, DstContainerVT, Src, Mask, VL);

  SDValue SplatZero = DAG.getNode(
      RISCVISD::VMV_V_X_VL, DL, DstContainerVT, DAG.getUNDEF(DstContainerVT),
      DAG.getConstant(0, DL, Subtarget.getXLenVT()), VL);
  Res = DAG.getNode(RISCVISD::VMERGE_VL, DL, DstContainerVT, IsNan, SplatZero,
                    Res, DAG.getUNDEF(DstContainerVT), VL);

  if (DstVT.isFixedLengthVector())
    Res = convertFromScalableVector(DstVT, Res, DAG, Subtarget);

  return Res;
}

static RISCVFPRndMode::RoundingMode matchRoundingOp(unsigned Opc) {
  switch (Opc) {
  case ISD::FROUNDEVEN:
  case ISD::STRICT_FROUNDEVEN:
  case ISD::VP_FROUNDEVEN:
    return RISCVFPRndMode::RNE;
  case ISD::FTRUNC:
  case ISD::STRICT_FTRUNC:
  case ISD::VP_FROUNDTOZERO:
    return RISCVFPRndMode::RTZ;
  case ISD::FFLOOR:
  case ISD::STRICT_FFLOOR:
  case ISD::VP_FFLOOR:
    return RISCVFPRndMode::RDN;
  case ISD::FCEIL:
  case ISD::STRICT_FCEIL:
  case ISD::VP_FCEIL:
    return RISCVFPRndMode::RUP;
  case ISD::FROUND:
  case ISD::STRICT_FROUND:
  case ISD::VP_FROUND:
    return RISCVFPRndMode::RMM;
  case ISD::FRINT:
    return RISCVFPRndMode::DYN;
  }

  return RISCVFPRndMode::Invalid;
}

// Expand vector FTRUNC, FCEIL, FFLOOR, FROUND, VP_FCEIL, VP_FFLOOR, VP_FROUND
// VP_FROUNDEVEN, VP_FROUNDTOZERO, VP_FRINT and VP_FNEARBYINT by converting to
// the integer domain and back. Taking care to avoid converting values that are
// nan or already correct.
static SDValue
lowerVectorFTRUNC_FCEIL_FFLOOR_FROUND(SDValue Op, SelectionDAG &DAG,
                                      const RISCVSubtarget &Subtarget) {
  MVT VT = Op.getSimpleValueType();
  assert(VT.isVector() && "Unexpected type");

  SDLoc DL(Op);

  SDValue Src = Op.getOperand(0);

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
    Src = convertToScalableVector(ContainerVT, Src, DAG, Subtarget);
  }

  SDValue Mask, VL;
  if (Op->isVPOpcode()) {
    Mask = Op.getOperand(1);
    if (VT.isFixedLengthVector())
      Mask = convertToScalableVector(getMaskTypeFor(ContainerVT), Mask, DAG,
                                     Subtarget);
    VL = Op.getOperand(2);
  } else {
    std::tie(Mask, VL) = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);
  }

  // Freeze the source since we are increasing the number of uses.
  Src = DAG.getFreeze(Src);

  // We do the conversion on the absolute value and fix the sign at the end.
  SDValue Abs = DAG.getNode(RISCVISD::FABS_VL, DL, ContainerVT, Src, Mask, VL);

  // Determine the largest integer that can be represented exactly. This and
  // values larger than it don't have any fractional bits so don't need to
  // be converted.
  const fltSemantics &FltSem = DAG.EVTToAPFloatSemantics(ContainerVT);
  unsigned Precision = APFloat::semanticsPrecision(FltSem);
  APFloat MaxVal = APFloat(FltSem);
  MaxVal.convertFromAPInt(APInt::getOneBitSet(Precision, Precision - 1),
                          /*IsSigned*/ false, APFloat::rmNearestTiesToEven);
  SDValue MaxValNode =
      DAG.getConstantFP(MaxVal, DL, ContainerVT.getVectorElementType());
  SDValue MaxValSplat = DAG.getNode(RISCVISD::VFMV_V_F_VL, DL, ContainerVT,
                                    DAG.getUNDEF(ContainerVT), MaxValNode, VL);

  // If abs(Src) was larger than MaxVal or nan, keep it.
  MVT SetccVT = MVT::getVectorVT(MVT::i1, ContainerVT.getVectorElementCount());
  Mask =
      DAG.getNode(RISCVISD::SETCC_VL, DL, SetccVT,
                  {Abs, MaxValSplat, DAG.getCondCode(ISD::SETOLT),
                   Mask, Mask, VL});

  // Truncate to integer and convert back to FP.
  MVT IntVT = ContainerVT.changeVectorElementTypeToInteger();
  MVT XLenVT = Subtarget.getXLenVT();
  SDValue Truncated;

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode");
  case ISD::FCEIL:
  case ISD::VP_FCEIL:
  case ISD::FFLOOR:
  case ISD::VP_FFLOOR:
  case ISD::FROUND:
  case ISD::FROUNDEVEN:
  case ISD::VP_FROUND:
  case ISD::VP_FROUNDEVEN:
  case ISD::VP_FROUNDTOZERO: {
    RISCVFPRndMode::RoundingMode FRM = matchRoundingOp(Op.getOpcode());
    assert(FRM != RISCVFPRndMode::Invalid);
    Truncated = DAG.getNode(RISCVISD::VFCVT_RM_X_F_VL, DL, IntVT, Src, Mask,
                            DAG.getTargetConstant(FRM, DL, XLenVT), VL);
    break;
  }
  case ISD::FTRUNC:
    Truncated = DAG.getNode(RISCVISD::VFCVT_RTZ_X_F_VL, DL, IntVT, Src,
                            Mask, VL);
    break;
  case ISD::FRINT:
  case ISD::VP_FRINT:
    Truncated = DAG.getNode(RISCVISD::VFCVT_X_F_VL, DL, IntVT, Src, Mask, VL);
    break;
  case ISD::FNEARBYINT:
  case ISD::VP_FNEARBYINT:
    Truncated = DAG.getNode(RISCVISD::VFROUND_NOEXCEPT_VL, DL, ContainerVT, Src,
                            Mask, VL);
    break;
  }

  // VFROUND_NOEXCEPT_VL includes SINT_TO_FP_VL.
  if (Truncated.getOpcode() != RISCVISD::VFROUND_NOEXCEPT_VL)
    Truncated = DAG.getNode(RISCVISD::SINT_TO_FP_VL, DL, ContainerVT, Truncated,
                            Mask, VL);

  // Restore the original sign so that -0.0 is preserved.
  Truncated = DAG.getNode(RISCVISD::FCOPYSIGN_VL, DL, ContainerVT, Truncated,
                          Src, Src, Mask, VL);

  if (!VT.isFixedLengthVector())
    return Truncated;

  return convertFromScalableVector(VT, Truncated, DAG, Subtarget);
}

// Expand vector STRICT_FTRUNC, STRICT_FCEIL, STRICT_FFLOOR, STRICT_FROUND
// STRICT_FROUNDEVEN and STRICT_FNEARBYINT by converting sNan of the source to
// qNan and coverting the new source to integer and back to FP.
static SDValue
lowerVectorStrictFTRUNC_FCEIL_FFLOOR_FROUND(SDValue Op, SelectionDAG &DAG,
                                            const RISCVSubtarget &Subtarget) {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue Chain = Op.getOperand(0);
  SDValue Src = Op.getOperand(1);

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
    Src = convertToScalableVector(ContainerVT, Src, DAG, Subtarget);
  }

  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  // Freeze the source since we are increasing the number of uses.
  Src = DAG.getFreeze(Src);

  // Covert sNan to qNan by executing x + x for all unordered elemenet x in Src.
  MVT MaskVT = Mask.getSimpleValueType();
  SDValue Unorder = DAG.getNode(RISCVISD::STRICT_FSETCC_VL, DL,
                                DAG.getVTList(MaskVT, MVT::Other),
                                {Chain, Src, Src, DAG.getCondCode(ISD::SETUNE),
                                 DAG.getUNDEF(MaskVT), Mask, VL});
  Chain = Unorder.getValue(1);
  Src = DAG.getNode(RISCVISD::STRICT_FADD_VL, DL,
                    DAG.getVTList(ContainerVT, MVT::Other),
                    {Chain, Src, Src, Src, Unorder, VL});
  Chain = Src.getValue(1);

  // We do the conversion on the absolute value and fix the sign at the end.
  SDValue Abs = DAG.getNode(RISCVISD::FABS_VL, DL, ContainerVT, Src, Mask, VL);

  // Determine the largest integer that can be represented exactly. This and
  // values larger than it don't have any fractional bits so don't need to
  // be converted.
  const fltSemantics &FltSem = DAG.EVTToAPFloatSemantics(ContainerVT);
  unsigned Precision = APFloat::semanticsPrecision(FltSem);
  APFloat MaxVal = APFloat(FltSem);
  MaxVal.convertFromAPInt(APInt::getOneBitSet(Precision, Precision - 1),
                          /*IsSigned*/ false, APFloat::rmNearestTiesToEven);
  SDValue MaxValNode =
      DAG.getConstantFP(MaxVal, DL, ContainerVT.getVectorElementType());
  SDValue MaxValSplat = DAG.getNode(RISCVISD::VFMV_V_F_VL, DL, ContainerVT,
                                    DAG.getUNDEF(ContainerVT), MaxValNode, VL);

  // If abs(Src) was larger than MaxVal or nan, keep it.
  Mask = DAG.getNode(
      RISCVISD::SETCC_VL, DL, MaskVT,
      {Abs, MaxValSplat, DAG.getCondCode(ISD::SETOLT), Mask, Mask, VL});

  // Truncate to integer and convert back to FP.
  MVT IntVT = ContainerVT.changeVectorElementTypeToInteger();
  MVT XLenVT = Subtarget.getXLenVT();
  SDValue Truncated;

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode");
  case ISD::STRICT_FCEIL:
  case ISD::STRICT_FFLOOR:
  case ISD::STRICT_FROUND:
  case ISD::STRICT_FROUNDEVEN: {
    RISCVFPRndMode::RoundingMode FRM = matchRoundingOp(Op.getOpcode());
    assert(FRM != RISCVFPRndMode::Invalid);
    Truncated = DAG.getNode(
        RISCVISD::STRICT_VFCVT_RM_X_F_VL, DL, DAG.getVTList(IntVT, MVT::Other),
        {Chain, Src, Mask, DAG.getTargetConstant(FRM, DL, XLenVT), VL});
    break;
  }
  case ISD::STRICT_FTRUNC:
    Truncated =
        DAG.getNode(RISCVISD::STRICT_VFCVT_RTZ_X_F_VL, DL,
                    DAG.getVTList(IntVT, MVT::Other), Chain, Src, Mask, VL);
    break;
  case ISD::STRICT_FNEARBYINT:
    Truncated = DAG.getNode(RISCVISD::STRICT_VFROUND_NOEXCEPT_VL, DL,
                            DAG.getVTList(ContainerVT, MVT::Other), Chain, Src,
                            Mask, VL);
    break;
  }
  Chain = Truncated.getValue(1);

  // VFROUND_NOEXCEPT_VL includes SINT_TO_FP_VL.
  if (Op.getOpcode() != ISD::STRICT_FNEARBYINT) {
    Truncated = DAG.getNode(RISCVISD::STRICT_SINT_TO_FP_VL, DL,
                            DAG.getVTList(ContainerVT, MVT::Other), Chain,
                            Truncated, Mask, VL);
    Chain = Truncated.getValue(1);
  }

  // Restore the original sign so that -0.0 is preserved.
  Truncated = DAG.getNode(RISCVISD::FCOPYSIGN_VL, DL, ContainerVT, Truncated,
                          Src, Src, Mask, VL);

  if (VT.isFixedLengthVector())
    Truncated = convertFromScalableVector(VT, Truncated, DAG, Subtarget);
  return DAG.getMergeValues({Truncated, Chain}, DL);
}

static SDValue
lowerFTRUNC_FCEIL_FFLOOR_FROUND(SDValue Op, SelectionDAG &DAG,
                                const RISCVSubtarget &Subtarget) {
  MVT VT = Op.getSimpleValueType();
  if (VT.isVector())
    return lowerVectorFTRUNC_FCEIL_FFLOOR_FROUND(Op, DAG, Subtarget);

  if (DAG.shouldOptForSize())
    return SDValue();

  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);

  // Create an integer the size of the mantissa with the MSB set. This and all
  // values larger than it don't have any fractional bits so don't need to be
  // converted.
  const fltSemantics &FltSem = DAG.EVTToAPFloatSemantics(VT);
  unsigned Precision = APFloat::semanticsPrecision(FltSem);
  APFloat MaxVal = APFloat(FltSem);
  MaxVal.convertFromAPInt(APInt::getOneBitSet(Precision, Precision - 1),
                          /*IsSigned*/ false, APFloat::rmNearestTiesToEven);
  SDValue MaxValNode = DAG.getConstantFP(MaxVal, DL, VT);

  RISCVFPRndMode::RoundingMode FRM = matchRoundingOp(Op.getOpcode());
  return DAG.getNode(RISCVISD::FROUND, DL, VT, Src, MaxValNode,
                     DAG.getTargetConstant(FRM, DL, Subtarget.getXLenVT()));
}

// Expand vector LRINT and LLRINT by converting to the integer domain.
static SDValue lowerVectorXRINT(SDValue Op, SelectionDAG &DAG,
                                const RISCVSubtarget &Subtarget) {
  MVT VT = Op.getSimpleValueType();
  assert(VT.isVector() && "Unexpected type");

  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  MVT ContainerVT = VT;

  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
    Src = convertToScalableVector(ContainerVT, Src, DAG, Subtarget);
  }

  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);
  SDValue Truncated =
      DAG.getNode(RISCVISD::VFCVT_X_F_VL, DL, ContainerVT, Src, Mask, VL);

  if (!VT.isFixedLengthVector())
    return Truncated;

  return convertFromScalableVector(VT, Truncated, DAG, Subtarget);
}

static SDValue
getVSlidedown(SelectionDAG &DAG, const RISCVSubtarget &Subtarget,
              const SDLoc &DL, EVT VT, SDValue Merge, SDValue Op,
              SDValue Offset, SDValue Mask, SDValue VL,
              unsigned Policy = RISCVII::TAIL_UNDISTURBED_MASK_UNDISTURBED) {
  if (Merge.isUndef())
    Policy = RISCVII::TAIL_AGNOSTIC | RISCVII::MASK_AGNOSTIC;
  SDValue PolicyOp = DAG.getTargetConstant(Policy, DL, Subtarget.getXLenVT());
  SDValue Ops[] = {Merge, Op, Offset, Mask, VL, PolicyOp};
  return DAG.getNode(RISCVISD::VSLIDEDOWN_VL, DL, VT, Ops);
}

static SDValue
getVSlideup(SelectionDAG &DAG, const RISCVSubtarget &Subtarget, const SDLoc &DL,
            EVT VT, SDValue Merge, SDValue Op, SDValue Offset, SDValue Mask,
            SDValue VL,
            unsigned Policy = RISCVII::TAIL_UNDISTURBED_MASK_UNDISTURBED) {
  if (Merge.isUndef())
    Policy = RISCVII::TAIL_AGNOSTIC | RISCVII::MASK_AGNOSTIC;
  SDValue PolicyOp = DAG.getTargetConstant(Policy, DL, Subtarget.getXLenVT());
  SDValue Ops[] = {Merge, Op, Offset, Mask, VL, PolicyOp};
  return DAG.getNode(RISCVISD::VSLIDEUP_VL, DL, VT, Ops);
}

static MVT getLMUL1VT(MVT VT) {
  assert(VT.getVectorElementType().getSizeInBits() <= 64 &&
         "Unexpected vector MVT");
  return MVT::getScalableVectorVT(
      VT.getVectorElementType(),
      RISCV::RVVBitsPerBlock / VT.getVectorElementType().getSizeInBits());
}

struct VIDSequence {
  int64_t StepNumerator;
  unsigned StepDenominator;
  int64_t Addend;
};

static std::optional<uint64_t> getExactInteger(const APFloat &APF,
                                               uint32_t BitWidth) {
  // We will use a SINT_TO_FP to materialize this constant so we should use a
  // signed APSInt here.
  APSInt ValInt(BitWidth, /*IsUnsigned*/ false);
  // We use an arbitrary rounding mode here. If a floating-point is an exact
  // integer (e.g., 1.0), the rounding mode does not affect the output value. If
  // the rounding mode changes the output value, then it is not an exact
  // integer.
  RoundingMode ArbitraryRM = RoundingMode::TowardZero;
  bool IsExact;
  // If it is out of signed integer range, it will return an invalid operation.
  // If it is not an exact integer, IsExact is false.
  if ((APF.convertToInteger(ValInt, ArbitraryRM, &IsExact) ==
       APFloatBase::opInvalidOp) ||
      !IsExact)
    return std::nullopt;
  return ValInt.extractBitsAsZExtValue(BitWidth, 0);
}

// Try to match an arithmetic-sequence BUILD_VECTOR [X,X+S,X+2*S,...,X+(N-1)*S]
// to the (non-zero) step S and start value X. This can be then lowered as the
// RVV sequence (VID * S) + X, for example.
// The step S is represented as an integer numerator divided by a positive
// denominator. Note that the implementation currently only identifies
// sequences in which either the numerator is +/- 1 or the denominator is 1. It
// cannot detect 2/3, for example.
// Note that this method will also match potentially unappealing index
// sequences, like <i32 0, i32 50939494>, however it is left to the caller to
// determine whether this is worth generating code for.
static std::optional<VIDSequence> isSimpleVIDSequence(SDValue Op,
                                                      unsigned EltSizeInBits) {
  assert(Op.getOpcode() == ISD::BUILD_VECTOR && "Unexpected BUILD_VECTOR");
  if (!cast<BuildVectorSDNode>(Op)->isConstant())
    return std::nullopt;
  bool IsInteger = Op.getValueType().isInteger();

  std::optional<unsigned> SeqStepDenom;
  std::optional<int64_t> SeqStepNum, SeqAddend;
  std::optional<std::pair<uint64_t, unsigned>> PrevElt;
  assert(EltSizeInBits >= Op.getValueType().getScalarSizeInBits());

  // First extract the ops into a list of constant integer values. This may not
  // be possible for floats if they're not all representable as integers.
  SmallVector<std::optional<uint64_t>> Elts(Op.getNumOperands());
  const unsigned OpSize = Op.getScalarValueSizeInBits();
  for (auto [Idx, Elt] : enumerate(Op->op_values())) {
    if (Elt.isUndef()) {
      Elts[Idx] = std::nullopt;
      continue;
    }
    if (IsInteger) {
      Elts[Idx] = Elt->getAsZExtVal() & maskTrailingOnes<uint64_t>(OpSize);
    } else {
      auto ExactInteger =
          getExactInteger(cast<ConstantFPSDNode>(Elt)->getValueAPF(), OpSize);
      if (!ExactInteger)
        return std::nullopt;
      Elts[Idx] = *ExactInteger;
    }
  }

  for (auto [Idx, Elt] : enumerate(Elts)) {
    // Assume undef elements match the sequence; we just have to be careful
    // when interpolating across them.
    if (!Elt)
      continue;

    if (PrevElt) {
      // Calculate the step since the last non-undef element, and ensure
      // it's consistent across the entire sequence.
      unsigned IdxDiff = Idx - PrevElt->second;
      int64_t ValDiff = SignExtend64(*Elt - PrevElt->first, EltSizeInBits);

      // A zero-value value difference means that we're somewhere in the middle
      // of a fractional step, e.g. <0,0,0*,0,1,1,1,1>. Wait until we notice a
      // step change before evaluating the sequence.
      if (ValDiff == 0)
        continue;

      int64_t Remainder = ValDiff % IdxDiff;
      // Normalize the step if it's greater than 1.
      if (Remainder != ValDiff) {
        // The difference must cleanly divide the element span.
        if (Remainder != 0)
          return std::nullopt;
        ValDiff /= IdxDiff;
        IdxDiff = 1;
      }

      if (!SeqStepNum)
        SeqStepNum = ValDiff;
      else if (ValDiff != SeqStepNum)
        return std::nullopt;

      if (!SeqStepDenom)
        SeqStepDenom = IdxDiff;
      else if (IdxDiff != *SeqStepDenom)
        return std::nullopt;
    }

    // Record this non-undef element for later.
    if (!PrevElt || PrevElt->first != *Elt)
      PrevElt = std::make_pair(*Elt, Idx);
  }

  // We need to have logged a step for this to count as a legal index sequence.
  if (!SeqStepNum || !SeqStepDenom)
    return std::nullopt;

  // Loop back through the sequence and validate elements we might have skipped
  // while waiting for a valid step. While doing this, log any sequence addend.
  for (auto [Idx, Elt] : enumerate(Elts)) {
    if (!Elt)
      continue;
    uint64_t ExpectedVal =
        (int64_t)(Idx * (uint64_t)*SeqStepNum) / *SeqStepDenom;
    int64_t Addend = SignExtend64(*Elt - ExpectedVal, EltSizeInBits);
    if (!SeqAddend)
      SeqAddend = Addend;
    else if (Addend != SeqAddend)
      return std::nullopt;
  }

  assert(SeqAddend && "Must have an addend if we have a step");

  return VIDSequence{*SeqStepNum, *SeqStepDenom, *SeqAddend};
}

// Match a splatted value (SPLAT_VECTOR/BUILD_VECTOR) of an EXTRACT_VECTOR_ELT
// and lower it as a VRGATHER_VX_VL from the source vector.
static SDValue matchSplatAsGather(SDValue SplatVal, MVT VT, const SDLoc &DL,
                                  SelectionDAG &DAG,
                                  const RISCVSubtarget &Subtarget) {
  if (SplatVal.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    return SDValue();
  SDValue Vec = SplatVal.getOperand(0);
  // Only perform this optimization on vectors of the same size for simplicity.
  // Don't perform this optimization for i1 vectors.
  // FIXME: Support i1 vectors, maybe by promoting to i8?
  if (Vec.getValueType() != VT || VT.getVectorElementType() == MVT::i1)
    return SDValue();
  SDValue Idx = SplatVal.getOperand(1);
  // The index must be a legal type.
  if (Idx.getValueType() != Subtarget.getXLenVT())
    return SDValue();

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
    Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
  }

  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  SDValue Gather = DAG.getNode(RISCVISD::VRGATHER_VX_VL, DL, ContainerVT, Vec,
                               Idx, DAG.getUNDEF(ContainerVT), Mask, VL);

  if (!VT.isFixedLengthVector())
    return Gather;

  return convertFromScalableVector(VT, Gather, DAG, Subtarget);
}


/// Try and optimize BUILD_VECTORs with "dominant values" - these are values
/// which constitute a large proportion of the elements. In such cases we can
/// splat a vector with the dominant element and make up the shortfall with
/// INSERT_VECTOR_ELTs.  Returns SDValue if not profitable.
/// Note that this includes vectors of 2 elements by association. The
/// upper-most element is the "dominant" one, allowing us to use a splat to
/// "insert" the upper element, and an insert of the lower element at position
/// 0, which improves codegen.
static SDValue lowerBuildVectorViaDominantValues(SDValue Op, SelectionDAG &DAG,
                                                 const RISCVSubtarget &Subtarget) {
  MVT VT = Op.getSimpleValueType();
  assert(VT.isFixedLengthVector() && "Unexpected vector!");

  MVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);

  SDLoc DL(Op);
  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  MVT XLenVT = Subtarget.getXLenVT();
  unsigned NumElts = Op.getNumOperands();

  SDValue DominantValue;
  unsigned MostCommonCount = 0;
  DenseMap<SDValue, unsigned> ValueCounts;
  unsigned NumUndefElts =
      count_if(Op->op_values(), [](const SDValue &V) { return V.isUndef(); });

  // Track the number of scalar loads we know we'd be inserting, estimated as
  // any non-zero floating-point constant. Other kinds of element are either
  // already in registers or are materialized on demand. The threshold at which
  // a vector load is more desirable than several scalar materializion and
  // vector-insertion instructions is not known.
  unsigned NumScalarLoads = 0;

  for (SDValue V : Op->op_values()) {
    if (V.isUndef())
      continue;

    ValueCounts.insert(std::make_pair(V, 0));
    unsigned &Count = ValueCounts[V];
    if (0 == Count)
      if (auto *CFP = dyn_cast<ConstantFPSDNode>(V))
        NumScalarLoads += !CFP->isExactlyValue(+0.0);

    // Is this value dominant? In case of a tie, prefer the highest element as
    // it's cheaper to insert near the beginning of a vector than it is at the
    // end.
    if (++Count >= MostCommonCount) {
      DominantValue = V;
      MostCommonCount = Count;
    }
  }

  assert(DominantValue && "Not expecting an all-undef BUILD_VECTOR");
  unsigned NumDefElts = NumElts - NumUndefElts;
  unsigned DominantValueCountThreshold = NumDefElts <= 2 ? 0 : NumDefElts - 2;

  // Don't perform this optimization when optimizing for size, since
  // materializing elements and inserting them tends to cause code bloat.
  if (!DAG.shouldOptForSize() && NumScalarLoads < NumElts &&
      (NumElts != 2 || ISD::isBuildVectorOfConstantSDNodes(Op.getNode())) &&
      ((MostCommonCount > DominantValueCountThreshold) ||
       (ValueCounts.size() <= Log2_32(NumDefElts)))) {
    // Start by splatting the most common element.
    SDValue Vec = DAG.getSplatBuildVector(VT, DL, DominantValue);

    DenseSet<SDValue> Processed{DominantValue};

    // We can handle an insert into the last element (of a splat) via
    // v(f)slide1down.  This is slightly better than the vslideup insert
    // lowering as it avoids the need for a vector group temporary.  It
    // is also better than using vmerge.vx as it avoids the need to
    // materialize the mask in a vector register.
    if (SDValue LastOp = Op->getOperand(Op->getNumOperands() - 1);
        !LastOp.isUndef() && ValueCounts[LastOp] == 1 &&
        LastOp != DominantValue) {
      Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
      auto OpCode =
        VT.isFloatingPoint() ? RISCVISD::VFSLIDE1DOWN_VL : RISCVISD::VSLIDE1DOWN_VL;
      if (!VT.isFloatingPoint())
        LastOp = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, LastOp);
      Vec = DAG.getNode(OpCode, DL, ContainerVT, DAG.getUNDEF(ContainerVT), Vec,
                        LastOp, Mask, VL);
      Vec = convertFromScalableVector(VT, Vec, DAG, Subtarget);
      Processed.insert(LastOp);
    }

    MVT SelMaskTy = VT.changeVectorElementType(MVT::i1);
    for (const auto &OpIdx : enumerate(Op->ops())) {
      const SDValue &V = OpIdx.value();
      if (V.isUndef() || !Processed.insert(V).second)
        continue;
      if (ValueCounts[V] == 1) {
        Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, VT, Vec, V,
                          DAG.getVectorIdxConstant(OpIdx.index(), DL));
      } else {
        // Blend in all instances of this value using a VSELECT, using a
        // mask where each bit signals whether that element is the one
        // we're after.
        SmallVector<SDValue> Ops;
        transform(Op->op_values(), std::back_inserter(Ops), [&](SDValue V1) {
          return DAG.getConstant(V == V1, DL, XLenVT);
        });
        Vec = DAG.getNode(ISD::VSELECT, DL, VT,
                          DAG.getBuildVector(SelMaskTy, DL, Ops),
                          DAG.getSplatBuildVector(VT, DL, V), Vec);
      }
    }

    return Vec;
  }

  return SDValue();
}

static SDValue lowerBuildVectorOfConstants(SDValue Op, SelectionDAG &DAG,
                                           const RISCVSubtarget &Subtarget) {
  MVT VT = Op.getSimpleValueType();
  assert(VT.isFixedLengthVector() && "Unexpected vector!");

  MVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);

  SDLoc DL(Op);
  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  MVT XLenVT = Subtarget.getXLenVT();
  unsigned NumElts = Op.getNumOperands();

  if (VT.getVectorElementType() == MVT::i1) {
    if (ISD::isBuildVectorAllZeros(Op.getNode())) {
      SDValue VMClr = DAG.getNode(RISCVISD::VMCLR_VL, DL, ContainerVT, VL);
      return convertFromScalableVector(VT, VMClr, DAG, Subtarget);
    }

    if (ISD::isBuildVectorAllOnes(Op.getNode())) {
      SDValue VMSet = DAG.getNode(RISCVISD::VMSET_VL, DL, ContainerVT, VL);
      return convertFromScalableVector(VT, VMSet, DAG, Subtarget);
    }

    // Lower constant mask BUILD_VECTORs via an integer vector type, in
    // scalar integer chunks whose bit-width depends on the number of mask
    // bits and XLEN.
    // First, determine the most appropriate scalar integer type to use. This
    // is at most XLenVT, but may be shrunk to a smaller vector element type
    // according to the size of the final vector - use i8 chunks rather than
    // XLenVT if we're producing a v8i1. This results in more consistent
    // codegen across RV32 and RV64.
    unsigned NumViaIntegerBits = std::clamp(NumElts, 8u, Subtarget.getXLen());
    NumViaIntegerBits = std::min(NumViaIntegerBits, Subtarget.getELen());
    // If we have to use more than one INSERT_VECTOR_ELT then this
    // optimization is likely to increase code size; avoid peforming it in
    // such a case. We can use a load from a constant pool in this case.
    if (DAG.shouldOptForSize() && NumElts > NumViaIntegerBits)
      return SDValue();
    // Now we can create our integer vector type. Note that it may be larger
    // than the resulting mask type: v4i1 would use v1i8 as its integer type.
    unsigned IntegerViaVecElts = divideCeil(NumElts, NumViaIntegerBits);
    MVT IntegerViaVecVT =
      MVT::getVectorVT(MVT::getIntegerVT(NumViaIntegerBits),
                       IntegerViaVecElts);

    uint64_t Bits = 0;
    unsigned BitPos = 0, IntegerEltIdx = 0;
    SmallVector<SDValue, 8> Elts(IntegerViaVecElts);

    for (unsigned I = 0; I < NumElts;) {
      SDValue V = Op.getOperand(I);
      bool BitValue = !V.isUndef() && V->getAsZExtVal();
      Bits |= ((uint64_t)BitValue << BitPos);
      ++BitPos;
      ++I;

      // Once we accumulate enough bits to fill our scalar type or process the
      // last element, insert into our vector and clear our accumulated data.
      if (I % NumViaIntegerBits == 0 || I == NumElts) {
        if (NumViaIntegerBits <= 32)
          Bits = SignExtend64<32>(Bits);
        SDValue Elt = DAG.getConstant(Bits, DL, XLenVT);
        Elts[IntegerEltIdx] = Elt;
        Bits = 0;
        BitPos = 0;
        IntegerEltIdx++;
      }
    }

    SDValue Vec = DAG.getBuildVector(IntegerViaVecVT, DL, Elts);

    if (NumElts < NumViaIntegerBits) {
      // If we're producing a smaller vector than our minimum legal integer
      // type, bitcast to the equivalent (known-legal) mask type, and extract
      // our final mask.
      assert(IntegerViaVecVT == MVT::v1i8 && "Unexpected mask vector type");
      Vec = DAG.getBitcast(MVT::v8i1, Vec);
      Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Vec,
                        DAG.getConstant(0, DL, XLenVT));
    } else {
      // Else we must have produced an integer type with the same size as the
      // mask type; bitcast for the final result.
      assert(VT.getSizeInBits() == IntegerViaVecVT.getSizeInBits());
      Vec = DAG.getBitcast(VT, Vec);
    }

    return Vec;
  }

  if (SDValue Splat = cast<BuildVectorSDNode>(Op)->getSplatValue()) {
    unsigned Opc = VT.isFloatingPoint() ? RISCVISD::VFMV_V_F_VL
                                        : RISCVISD::VMV_V_X_VL;
    if (!VT.isFloatingPoint())
      Splat = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, Splat);
    Splat =
        DAG.getNode(Opc, DL, ContainerVT, DAG.getUNDEF(ContainerVT), Splat, VL);
    return convertFromScalableVector(VT, Splat, DAG, Subtarget);
  }

  // Try and match index sequences, which we can lower to the vid instruction
  // with optional modifications. An all-undef vector is matched by
  // getSplatValue, above.
  if (auto SimpleVID = isSimpleVIDSequence(Op, Op.getScalarValueSizeInBits())) {
    int64_t StepNumerator = SimpleVID->StepNumerator;
    unsigned StepDenominator = SimpleVID->StepDenominator;
    int64_t Addend = SimpleVID->Addend;

    assert(StepNumerator != 0 && "Invalid step");
    bool Negate = false;
    int64_t SplatStepVal = StepNumerator;
    unsigned StepOpcode = ISD::MUL;
    // Exclude INT64_MIN to avoid passing it to std::abs. We won't optimize it
    // anyway as the shift of 63 won't fit in uimm5.
    if (StepNumerator != 1 && StepNumerator != INT64_MIN &&
        isPowerOf2_64(std::abs(StepNumerator))) {
      Negate = StepNumerator < 0;
      StepOpcode = ISD::SHL;
      SplatStepVal = Log2_64(std::abs(StepNumerator));
    }

    // Only emit VIDs with suitably-small steps/addends. We use imm5 is a
    // threshold since it's the immediate value many RVV instructions accept.
    // There is no vmul.vi instruction so ensure multiply constant can fit in
    // a single addi instruction.
    if (((StepOpcode == ISD::MUL && isInt<12>(SplatStepVal)) ||
         (StepOpcode == ISD::SHL && isUInt<5>(SplatStepVal))) &&
        isPowerOf2_32(StepDenominator) &&
        (SplatStepVal >= 0 || StepDenominator == 1) && isInt<5>(Addend)) {
      MVT VIDVT =
          VT.isFloatingPoint() ? VT.changeVectorElementTypeToInteger() : VT;
      MVT VIDContainerVT =
          getContainerForFixedLengthVector(DAG, VIDVT, Subtarget);
      SDValue VID = DAG.getNode(RISCVISD::VID_VL, DL, VIDContainerVT, Mask, VL);
      // Convert right out of the scalable type so we can use standard ISD
      // nodes for the rest of the computation. If we used scalable types with
      // these, we'd lose the fixed-length vector info and generate worse
      // vsetvli code.
      VID = convertFromScalableVector(VIDVT, VID, DAG, Subtarget);
      if ((StepOpcode == ISD::MUL && SplatStepVal != 1) ||
          (StepOpcode == ISD::SHL && SplatStepVal != 0)) {
        SDValue SplatStep = DAG.getConstant(SplatStepVal, DL, VIDVT);
        VID = DAG.getNode(StepOpcode, DL, VIDVT, VID, SplatStep);
      }
      if (StepDenominator != 1) {
        SDValue SplatStep =
            DAG.getConstant(Log2_64(StepDenominator), DL, VIDVT);
        VID = DAG.getNode(ISD::SRL, DL, VIDVT, VID, SplatStep);
      }
      if (Addend != 0 || Negate) {
        SDValue SplatAddend = DAG.getConstant(Addend, DL, VIDVT);
        VID = DAG.getNode(Negate ? ISD::SUB : ISD::ADD, DL, VIDVT, SplatAddend,
                          VID);
      }
      if (VT.isFloatingPoint()) {
        // TODO: Use vfwcvt to reduce register pressure.
        VID = DAG.getNode(ISD::SINT_TO_FP, DL, VT, VID);
      }
      return VID;
    }
  }

  // For very small build_vectors, use a single scalar insert of a constant.
  // TODO: Base this on constant rematerialization cost, not size.
  const unsigned EltBitSize = VT.getScalarSizeInBits();
  if (VT.getSizeInBits() <= 32 &&
      ISD::isBuildVectorOfConstantSDNodes(Op.getNode())) {
    MVT ViaIntVT = MVT::getIntegerVT(VT.getSizeInBits());
    assert((ViaIntVT == MVT::i16 || ViaIntVT == MVT::i32) &&
           "Unexpected sequence type");
    // If we can use the original VL with the modified element type, this
    // means we only have a VTYPE toggle, not a VL toggle.  TODO: Should this
    // be moved into InsertVSETVLI?
    unsigned ViaVecLen =
      (Subtarget.getRealMinVLen() >= VT.getSizeInBits() * NumElts) ? NumElts : 1;
    MVT ViaVecVT = MVT::getVectorVT(ViaIntVT, ViaVecLen);

    uint64_t EltMask = maskTrailingOnes<uint64_t>(EltBitSize);
    uint64_t SplatValue = 0;
    // Construct the amalgamated value at this larger vector type.
    for (const auto &OpIdx : enumerate(Op->op_values())) {
      const auto &SeqV = OpIdx.value();
      if (!SeqV.isUndef())
        SplatValue |=
            ((SeqV->getAsZExtVal() & EltMask) << (OpIdx.index() * EltBitSize));
    }

    // On RV64, sign-extend from 32 to 64 bits where possible in order to
    // achieve better constant materializion.
    if (Subtarget.is64Bit() && ViaIntVT == MVT::i32)
      SplatValue = SignExtend64<32>(SplatValue);

    SDValue Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, ViaVecVT,
                              DAG.getUNDEF(ViaVecVT),
                              DAG.getConstant(SplatValue, DL, XLenVT),
                              DAG.getVectorIdxConstant(0, DL));
    if (ViaVecLen != 1)
      Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL,
                        MVT::getVectorVT(ViaIntVT, 1), Vec,
                        DAG.getConstant(0, DL, XLenVT));
    return DAG.getBitcast(VT, Vec);
  }


  // Attempt to detect "hidden" splats, which only reveal themselves as splats
  // when re-interpreted as a vector with a larger element type. For example,
  //   v4i16 = build_vector i16 0, i16 1, i16 0, i16 1
  // could be instead splat as
  //   v2i32 = build_vector i32 0x00010000, i32 0x00010000
  // TODO: This optimization could also work on non-constant splats, but it
  // would require bit-manipulation instructions to construct the splat value.
  SmallVector<SDValue> Sequence;
  const auto *BV = cast<BuildVectorSDNode>(Op);
  if (VT.isInteger() && EltBitSize < Subtarget.getELen() &&
      ISD::isBuildVectorOfConstantSDNodes(Op.getNode()) &&
      BV->getRepeatedSequence(Sequence) &&
      (Sequence.size() * EltBitSize) <= Subtarget.getELen()) {
    unsigned SeqLen = Sequence.size();
    MVT ViaIntVT = MVT::getIntegerVT(EltBitSize * SeqLen);
    assert((ViaIntVT == MVT::i16 || ViaIntVT == MVT::i32 ||
            ViaIntVT == MVT::i64) &&
           "Unexpected sequence type");

    // If we can use the original VL with the modified element type, this
    // means we only have a VTYPE toggle, not a VL toggle.  TODO: Should this
    // be moved into InsertVSETVLI?
    const unsigned RequiredVL = NumElts / SeqLen;
    const unsigned ViaVecLen =
      (Subtarget.getRealMinVLen() >= ViaIntVT.getSizeInBits() * NumElts) ?
      NumElts : RequiredVL;
    MVT ViaVecVT = MVT::getVectorVT(ViaIntVT, ViaVecLen);

    unsigned EltIdx = 0;
    uint64_t EltMask = maskTrailingOnes<uint64_t>(EltBitSize);
    uint64_t SplatValue = 0;
    // Construct the amalgamated value which can be splatted as this larger
    // vector type.
    for (const auto &SeqV : Sequence) {
      if (!SeqV.isUndef())
        SplatValue |=
            ((SeqV->getAsZExtVal() & EltMask) << (EltIdx * EltBitSize));
      EltIdx++;
    }

    // On RV64, sign-extend from 32 to 64 bits where possible in order to
    // achieve better constant materializion.
    if (Subtarget.is64Bit() && ViaIntVT == MVT::i32)
      SplatValue = SignExtend64<32>(SplatValue);

    // Since we can't introduce illegal i64 types at this stage, we can only
    // perform an i64 splat on RV32 if it is its own sign-extended value. That
    // way we can use RVV instructions to splat.
    assert((ViaIntVT.bitsLE(XLenVT) ||
            (!Subtarget.is64Bit() && ViaIntVT == MVT::i64)) &&
           "Unexpected bitcast sequence");
    if (ViaIntVT.bitsLE(XLenVT) || isInt<32>(SplatValue)) {
      SDValue ViaVL =
          DAG.getConstant(ViaVecVT.getVectorNumElements(), DL, XLenVT);
      MVT ViaContainerVT =
          getContainerForFixedLengthVector(DAG, ViaVecVT, Subtarget);
      SDValue Splat =
          DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ViaContainerVT,
                      DAG.getUNDEF(ViaContainerVT),
                      DAG.getConstant(SplatValue, DL, XLenVT), ViaVL);
      Splat = convertFromScalableVector(ViaVecVT, Splat, DAG, Subtarget);
      if (ViaVecLen != RequiredVL)
        Splat = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL,
                            MVT::getVectorVT(ViaIntVT, RequiredVL), Splat,
                            DAG.getConstant(0, DL, XLenVT));
      return DAG.getBitcast(VT, Splat);
    }
  }

  // If the number of signbits allows, see if we can lower as a <N x i8>.
  // Our main goal here is to reduce LMUL (and thus work) required to
  // build the constant, but we will also narrow if the resulting
  // narrow vector is known to materialize cheaply.
  // TODO: We really should be costing the smaller vector.  There are
  // profitable cases this misses.
  if (EltBitSize > 8 && VT.isInteger() &&
      (NumElts <= 4 || VT.getSizeInBits() > Subtarget.getRealMinVLen())) {
    unsigned SignBits = DAG.ComputeNumSignBits(Op);
    if (EltBitSize - SignBits < 8) {
      SDValue Source = DAG.getBuildVector(VT.changeVectorElementType(MVT::i8),
                                          DL, Op->ops());
      Source = convertToScalableVector(ContainerVT.changeVectorElementType(MVT::i8),
                                       Source, DAG, Subtarget);
      SDValue Res = DAG.getNode(RISCVISD::VSEXT_VL, DL, ContainerVT, Source, Mask, VL);
      return convertFromScalableVector(VT, Res, DAG, Subtarget);
    }
  }

  if (SDValue Res = lowerBuildVectorViaDominantValues(Op, DAG, Subtarget))
    return Res;

  // For constant vectors, use generic constant pool lowering.  Otherwise,
  // we'd have to materialize constants in GPRs just to move them into the
  // vector.
  return SDValue();
}

static unsigned getPACKOpcode(unsigned DestBW,
                              const RISCVSubtarget &Subtarget) {
  switch (DestBW) {
  default:
    llvm_unreachable("Unsupported pack size");
  case 16:
    return RISCV::PACKH;
  case 32:
    return Subtarget.is64Bit() ? RISCV::PACKW : RISCV::PACK;
  case 64:
    assert(Subtarget.is64Bit());
    return RISCV::PACK;
  }
}

/// Double the element size of the build vector to reduce the number
/// of vslide1down in the build vector chain.  In the worst case, this
/// trades three scalar operations for 1 vector operation.  Scalar
/// operations are generally lower latency, and for out-of-order cores
/// we also benefit from additional parallelism.
static SDValue lowerBuildVectorViaPacking(SDValue Op, SelectionDAG &DAG,
                                          const RISCVSubtarget &Subtarget) {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  assert(VT.isFixedLengthVector() && "Unexpected vector!");
  MVT ElemVT = VT.getVectorElementType();
  if (!ElemVT.isInteger())
    return SDValue();

  // TODO: Relax these architectural restrictions, possibly with costing
  // of the actual instructions required.
  if (!Subtarget.hasStdExtZbb() || !Subtarget.hasStdExtZba())
    return SDValue();

  unsigned NumElts = VT.getVectorNumElements();
  unsigned ElemSizeInBits = ElemVT.getSizeInBits();
  if (ElemSizeInBits >= std::min(Subtarget.getELen(), Subtarget.getXLen()) ||
      NumElts % 2 != 0)
    return SDValue();

  // Produce [B,A] packed into a type twice as wide.  Note that all
  // scalars are XLenVT, possibly masked (see below).
  MVT XLenVT = Subtarget.getXLenVT();
  SDValue Mask = DAG.getConstant(
      APInt::getLowBitsSet(XLenVT.getSizeInBits(), ElemSizeInBits), DL, XLenVT);
  auto pack = [&](SDValue A, SDValue B) {
    // Bias the scheduling of the inserted operations to near the
    // definition of the element - this tends to reduce register
    // pressure overall.
    SDLoc ElemDL(B);
    if (Subtarget.hasStdExtZbkb())
      // Note that we're relying on the high bits of the result being
      // don't care.  For PACKW, the result is *sign* extended.
      return SDValue(
          DAG.getMachineNode(getPACKOpcode(ElemSizeInBits * 2, Subtarget),
                             ElemDL, XLenVT, A, B),
          0);

    A = DAG.getNode(ISD::AND, SDLoc(A), XLenVT, A, Mask);
    B = DAG.getNode(ISD::AND, SDLoc(B), XLenVT, B, Mask);
    SDValue ShtAmt = DAG.getConstant(ElemSizeInBits, ElemDL, XLenVT);
    SDNodeFlags Flags;
    Flags.setDisjoint(true);
    return DAG.getNode(ISD::OR, ElemDL, XLenVT, A,
                       DAG.getNode(ISD::SHL, ElemDL, XLenVT, B, ShtAmt), Flags);
  };

  SmallVector<SDValue> NewOperands;
  NewOperands.reserve(NumElts / 2);
  for (unsigned i = 0; i < VT.getVectorNumElements(); i += 2)
    NewOperands.push_back(pack(Op.getOperand(i), Op.getOperand(i + 1)));
  assert(NumElts == NewOperands.size() * 2);
  MVT WideVT = MVT::getIntegerVT(ElemSizeInBits * 2);
  MVT WideVecVT = MVT::getVectorVT(WideVT, NumElts / 2);
  return DAG.getNode(ISD::BITCAST, DL, VT,
                     DAG.getBuildVector(WideVecVT, DL, NewOperands));
}

// Convert to an vXf16 build_vector to vXi16 with bitcasts.
static SDValue lowerBUILD_VECTORvXf16(SDValue Op, SelectionDAG &DAG) {
  MVT VT = Op.getSimpleValueType();
  MVT IVT = VT.changeVectorElementType(MVT::i16);
  SmallVector<SDValue, 16> NewOps(Op.getNumOperands());
  for (unsigned I = 0, E = Op.getNumOperands(); I != E; ++I)
    NewOps[I] = DAG.getBitcast(MVT::i16, Op.getOperand(I));
  SDValue Res = DAG.getNode(ISD::BUILD_VECTOR, SDLoc(Op), IVT, NewOps);
  return DAG.getBitcast(VT, Res);
}

static SDValue lowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  MVT VT = Op.getSimpleValueType();
  assert(VT.isFixedLengthVector() && "Unexpected vector!");

  // If we don't have scalar f16, we need to bitcast to an i16 vector.
  if (VT.getVectorElementType() == MVT::f16 &&
      !Subtarget.hasStdExtZfhmin())
    return lowerBUILD_VECTORvXf16(Op, DAG);

  if (ISD::isBuildVectorOfConstantSDNodes(Op.getNode()) ||
      ISD::isBuildVectorOfConstantFPSDNodes(Op.getNode()))
    return lowerBuildVectorOfConstants(Op, DAG, Subtarget);

  MVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);

  SDLoc DL(Op);
  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  MVT XLenVT = Subtarget.getXLenVT();

  if (VT.getVectorElementType() == MVT::i1) {
    // A BUILD_VECTOR can be lowered as a SETCC. For each fixed-length mask
    // vector type, we have a legal equivalently-sized i8 type, so we can use
    // that.
    MVT WideVecVT = VT.changeVectorElementType(MVT::i8);
    SDValue VecZero = DAG.getConstant(0, DL, WideVecVT);

    SDValue WideVec;
    if (SDValue Splat = cast<BuildVectorSDNode>(Op)->getSplatValue()) {
      // For a splat, perform a scalar truncate before creating the wider
      // vector.
      Splat = DAG.getNode(ISD::AND, DL, Splat.getValueType(), Splat,
                          DAG.getConstant(1, DL, Splat.getValueType()));
      WideVec = DAG.getSplatBuildVector(WideVecVT, DL, Splat);
    } else {
      SmallVector<SDValue, 8> Ops(Op->op_values());
      WideVec = DAG.getBuildVector(WideVecVT, DL, Ops);
      SDValue VecOne = DAG.getConstant(1, DL, WideVecVT);
      WideVec = DAG.getNode(ISD::AND, DL, WideVecVT, WideVec, VecOne);
    }

    return DAG.getSetCC(DL, VT, WideVec, VecZero, ISD::SETNE);
  }

  if (SDValue Splat = cast<BuildVectorSDNode>(Op)->getSplatValue()) {
    if (auto Gather = matchSplatAsGather(Splat, VT, DL, DAG, Subtarget))
      return Gather;
    unsigned Opc = VT.isFloatingPoint() ? RISCVISD::VFMV_V_F_VL
                                        : RISCVISD::VMV_V_X_VL;
    if (!VT.isFloatingPoint())
      Splat = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, Splat);
    Splat =
        DAG.getNode(Opc, DL, ContainerVT, DAG.getUNDEF(ContainerVT), Splat, VL);
    return convertFromScalableVector(VT, Splat, DAG, Subtarget);
  }

  if (SDValue Res = lowerBuildVectorViaDominantValues(Op, DAG, Subtarget))
    return Res;

  // If we're compiling for an exact VLEN value, we can split our work per
  // register in the register group.
  if (const auto VLen = Subtarget.getRealVLen();
      VLen && VT.getSizeInBits().getKnownMinValue() > *VLen) {
    MVT ElemVT = VT.getVectorElementType();
    unsigned ElemsPerVReg = *VLen / ElemVT.getFixedSizeInBits();
    EVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
    MVT OneRegVT = MVT::getVectorVT(ElemVT, ElemsPerVReg);
    MVT M1VT = getContainerForFixedLengthVector(DAG, OneRegVT, Subtarget);
    assert(M1VT == getLMUL1VT(M1VT));

    // The following semantically builds up a fixed length concat_vector
    // of the component build_vectors.  We eagerly lower to scalable and
    // insert_subvector here to avoid DAG combining it back to a large
    // build_vector.
    SmallVector<SDValue> BuildVectorOps(Op->op_begin(), Op->op_end());
    unsigned NumOpElts = M1VT.getVectorMinNumElements();
    SDValue Vec = DAG.getUNDEF(ContainerVT);
    for (unsigned i = 0; i < VT.getVectorNumElements(); i += ElemsPerVReg) {
      auto OneVRegOfOps = ArrayRef(BuildVectorOps).slice(i, ElemsPerVReg);
      SDValue SubBV =
          DAG.getNode(ISD::BUILD_VECTOR, DL, OneRegVT, OneVRegOfOps);
      SubBV = convertToScalableVector(M1VT, SubBV, DAG, Subtarget);
      unsigned InsertIdx = (i / ElemsPerVReg) * NumOpElts;
      Vec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ContainerVT, Vec, SubBV,
                        DAG.getVectorIdxConstant(InsertIdx, DL));
    }
    return convertFromScalableVector(VT, Vec, DAG, Subtarget);
  }

  // If we're about to resort to vslide1down (or stack usage), pack our
  // elements into the widest scalar type we can.  This will force a VL/VTYPE
  // toggle, but reduces the critical path, the number of vslide1down ops
  // required, and possibly enables scalar folds of the values.
  if (SDValue Res = lowerBuildVectorViaPacking(Op, DAG, Subtarget))
    return Res;

  // For m1 vectors, if we have non-undef values in both halves of our vector,
  // split the vector into low and high halves, build them separately, then
  // use a vselect to combine them.  For long vectors, this cuts the critical
  // path of the vslide1down sequence in half, and gives us an opportunity
  // to special case each half independently.  Note that we don't change the
  // length of the sub-vectors here, so if both fallback to the generic
  // vslide1down path, we should be able to fold the vselect into the final
  // vslidedown (for the undef tail) for the first half w/ masking.
  unsigned NumElts = VT.getVectorNumElements();
  unsigned NumUndefElts =
      count_if(Op->op_values(), [](const SDValue &V) { return V.isUndef(); });
  unsigned NumDefElts = NumElts - NumUndefElts;
  if (NumDefElts >= 8 && NumDefElts > NumElts / 2 &&
      ContainerVT.bitsLE(getLMUL1VT(ContainerVT))) {
    SmallVector<SDValue> SubVecAOps, SubVecBOps;
    SmallVector<SDValue> MaskVals;
    SDValue UndefElem = DAG.getUNDEF(Op->getOperand(0)->getValueType(0));
    SubVecAOps.reserve(NumElts);
    SubVecBOps.reserve(NumElts);
    for (unsigned i = 0; i < NumElts; i++) {
      SDValue Elem = Op->getOperand(i);
      if (i < NumElts / 2) {
        SubVecAOps.push_back(Elem);
        SubVecBOps.push_back(UndefElem);
      } else {
        SubVecAOps.push_back(UndefElem);
        SubVecBOps.push_back(Elem);
      }
      bool SelectMaskVal = (i < NumElts / 2);
      MaskVals.push_back(DAG.getConstant(SelectMaskVal, DL, XLenVT));
    }
    assert(SubVecAOps.size() == NumElts && SubVecBOps.size() == NumElts &&
           MaskVals.size() == NumElts);

    SDValue SubVecA = DAG.getBuildVector(VT, DL, SubVecAOps);
    SDValue SubVecB = DAG.getBuildVector(VT, DL, SubVecBOps);
    MVT MaskVT = MVT::getVectorVT(MVT::i1, NumElts);
    SDValue SelectMask = DAG.getBuildVector(MaskVT, DL, MaskVals);
    return DAG.getNode(ISD::VSELECT, DL, VT, SelectMask, SubVecA, SubVecB);
  }

  // Cap the cost at a value linear to the number of elements in the vector.
  // The default lowering is to use the stack.  The vector store + scalar loads
  // is linear in VL.  However, at high lmuls vslide1down and vslidedown end up
  // being (at least) linear in LMUL.  As a result, using the vslidedown
  // lowering for every element ends up being VL*LMUL..
  // TODO: Should we be directly costing the stack alternative?  Doing so might
  // give us a more accurate upper bound.
  InstructionCost LinearBudget = VT.getVectorNumElements() * 2;

  // TODO: unify with TTI getSlideCost.
  InstructionCost PerSlideCost = 1;
  switch (RISCVTargetLowering::getLMUL(ContainerVT)) {
  default: break;
  case RISCVII::VLMUL::LMUL_2:
    PerSlideCost = 2;
    break;
  case RISCVII::VLMUL::LMUL_4:
    PerSlideCost = 4;
    break;
  case RISCVII::VLMUL::LMUL_8:
    PerSlideCost = 8;
    break;
  }

  // TODO: Should we be using the build instseq then cost + evaluate scheme
  // we use for integer constants here?
  unsigned UndefCount = 0;
  for (const SDValue &V : Op->ops()) {
    if (V.isUndef()) {
      UndefCount++;
      continue;
    }
    if (UndefCount) {
      LinearBudget -= PerSlideCost;
      UndefCount = 0;
    }
    LinearBudget -= PerSlideCost;
  }
  if (UndefCount) {
    LinearBudget -= PerSlideCost;
  }

  if (LinearBudget < 0)
    return SDValue();

  assert((!VT.isFloatingPoint() ||
          VT.getVectorElementType().getSizeInBits() <= Subtarget.getFLen()) &&
         "Illegal type which will result in reserved encoding");

  const unsigned Policy = RISCVII::TAIL_AGNOSTIC | RISCVII::MASK_AGNOSTIC;

  SDValue Vec;
  UndefCount = 0;
  for (SDValue V : Op->ops()) {
    if (V.isUndef()) {
      UndefCount++;
      continue;
    }

    // Start our sequence with a TA splat in the hopes that hardware is able to
    // recognize there's no dependency on the prior value of our temporary
    // register.
    if (!Vec) {
      Vec = DAG.getSplatVector(VT, DL, V);
      Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
      UndefCount = 0;
      continue;
    }

    if (UndefCount) {
      const SDValue Offset = DAG.getConstant(UndefCount, DL, Subtarget.getXLenVT());
      Vec = getVSlidedown(DAG, Subtarget, DL, ContainerVT, DAG.getUNDEF(ContainerVT),
                          Vec, Offset, Mask, VL, Policy);
      UndefCount = 0;
    }
    auto OpCode =
      VT.isFloatingPoint() ? RISCVISD::VFSLIDE1DOWN_VL : RISCVISD::VSLIDE1DOWN_VL;
    if (!VT.isFloatingPoint())
      V = DAG.getNode(ISD::ANY_EXTEND, DL, Subtarget.getXLenVT(), V);
    Vec = DAG.getNode(OpCode, DL, ContainerVT, DAG.getUNDEF(ContainerVT), Vec,
                      V, Mask, VL);
  }
  if (UndefCount) {
    const SDValue Offset = DAG.getConstant(UndefCount, DL, Subtarget.getXLenVT());
    Vec = getVSlidedown(DAG, Subtarget, DL, ContainerVT, DAG.getUNDEF(ContainerVT),
                        Vec, Offset, Mask, VL, Policy);
  }
  return convertFromScalableVector(VT, Vec, DAG, Subtarget);
}

static SDValue splatPartsI64WithVL(const SDLoc &DL, MVT VT, SDValue Passthru,
                                   SDValue Lo, SDValue Hi, SDValue VL,
                                   SelectionDAG &DAG) {
  if (!Passthru)
    Passthru = DAG.getUNDEF(VT);
  if (isa<ConstantSDNode>(Lo) && isa<ConstantSDNode>(Hi)) {
    int32_t LoC = cast<ConstantSDNode>(Lo)->getSExtValue();
    int32_t HiC = cast<ConstantSDNode>(Hi)->getSExtValue();
    // If Hi constant is all the same sign bit as Lo, lower this as a custom
    // node in order to try and match RVV vector/scalar instructions.
    if ((LoC >> 31) == HiC)
      return DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, Passthru, Lo, VL);

    // If vl is equal to VLMAX or fits in 4 bits and Hi constant is equal to Lo,
    // we could use vmv.v.x whose EEW = 32 to lower it. This allows us to use
    // vlmax vsetvli or vsetivli to change the VL.
    // FIXME: Support larger constants?
    // FIXME: Support non-constant VLs by saturating?
    if (LoC == HiC) {
      SDValue NewVL;
      if (isAllOnesConstant(VL) ||
          (isa<RegisterSDNode>(VL) &&
           cast<RegisterSDNode>(VL)->getReg() == RISCV::X0))
        NewVL = DAG.getRegister(RISCV::X0, MVT::i32);
      else if (isa<ConstantSDNode>(VL) && isUInt<4>(VL->getAsZExtVal()))
        NewVL = DAG.getNode(ISD::ADD, DL, VL.getValueType(), VL, VL);

      if (NewVL) {
        MVT InterVT =
            MVT::getVectorVT(MVT::i32, VT.getVectorElementCount() * 2);
        auto InterVec = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, InterVT,
                                    DAG.getUNDEF(InterVT), Lo, NewVL);
        return DAG.getNode(ISD::BITCAST, DL, VT, InterVec);
      }
    }
  }

  // Detect cases where Hi is (SRA Lo, 31) which means Hi is Lo sign extended.
  if (Hi.getOpcode() == ISD::SRA && Hi.getOperand(0) == Lo &&
      isa<ConstantSDNode>(Hi.getOperand(1)) &&
      Hi.getConstantOperandVal(1) == 31)
    return DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, Passthru, Lo, VL);

  // If the hi bits of the splat are undefined, then it's fine to just splat Lo
  // even if it might be sign extended.
  if (Hi.isUndef())
    return DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, Passthru, Lo, VL);

  // Fall back to a stack store and stride x0 vector load.
  return DAG.getNode(RISCVISD::SPLAT_VECTOR_SPLIT_I64_VL, DL, VT, Passthru, Lo,
                     Hi, VL);
}

// Called by type legalization to handle splat of i64 on RV32.
// FIXME: We can optimize this when the type has sign or zero bits in one
// of the halves.
static SDValue splatSplitI64WithVL(const SDLoc &DL, MVT VT, SDValue Passthru,
                                   SDValue Scalar, SDValue VL,
                                   SelectionDAG &DAG) {
  assert(Scalar.getValueType() == MVT::i64 && "Unexpected VT!");
  SDValue Lo, Hi;
  std::tie(Lo, Hi) = DAG.SplitScalar(Scalar, DL, MVT::i32, MVT::i32);
  return splatPartsI64WithVL(DL, VT, Passthru, Lo, Hi, VL, DAG);
}

// This function lowers a splat of a scalar operand Splat with the vector
// length VL. It ensures the final sequence is type legal, which is useful when
// lowering a splat after type legalization.
static SDValue lowerScalarSplat(SDValue Passthru, SDValue Scalar, SDValue VL,
                                MVT VT, const SDLoc &DL, SelectionDAG &DAG,
                                const RISCVSubtarget &Subtarget) {
  bool HasPassthru = Passthru && !Passthru.isUndef();
  if (!HasPassthru && !Passthru)
    Passthru = DAG.getUNDEF(VT);
  if (VT.isFloatingPoint())
    return DAG.getNode(RISCVISD::VFMV_V_F_VL, DL, VT, Passthru, Scalar, VL);

  MVT XLenVT = Subtarget.getXLenVT();

  // Simplest case is that the operand needs to be promoted to XLenVT.
  if (Scalar.getValueType().bitsLE(XLenVT)) {
    // If the operand is a constant, sign extend to increase our chances
    // of being able to use a .vi instruction. ANY_EXTEND would become a
    // a zero extend and the simm5 check in isel would fail.
    // FIXME: Should we ignore the upper bits in isel instead?
    unsigned ExtOpc =
        isa<ConstantSDNode>(Scalar) ? ISD::SIGN_EXTEND : ISD::ANY_EXTEND;
    Scalar = DAG.getNode(ExtOpc, DL, XLenVT, Scalar);
    return DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, Passthru, Scalar, VL);
  }

  assert(XLenVT == MVT::i32 && Scalar.getValueType() == MVT::i64 &&
         "Unexpected scalar for splat lowering!");

  if (isOneConstant(VL) && isNullConstant(Scalar))
    return DAG.getNode(RISCVISD::VMV_S_X_VL, DL, VT, Passthru,
                       DAG.getConstant(0, DL, XLenVT), VL);

  // Otherwise use the more complicated splatting algorithm.
  return splatSplitI64WithVL(DL, VT, Passthru, Scalar, VL, DAG);
}

// This function lowers an insert of a scalar operand Scalar into lane
// 0 of the vector regardless of the value of VL.  The contents of the
// remaining lanes of the result vector are unspecified.  VL is assumed
// to be non-zero.
static SDValue lowerScalarInsert(SDValue Scalar, SDValue VL, MVT VT,
                                 const SDLoc &DL, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  assert(VT.isScalableVector() && "Expect VT is scalable vector type.");

  const MVT XLenVT = Subtarget.getXLenVT();
  SDValue Passthru = DAG.getUNDEF(VT);

  if (Scalar.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
      isNullConstant(Scalar.getOperand(1))) {
    SDValue ExtractedVal = Scalar.getOperand(0);
    // The element types must be the same.
    if (ExtractedVal.getValueType().getVectorElementType() ==
        VT.getVectorElementType()) {
      MVT ExtractedVT = ExtractedVal.getSimpleValueType();
      MVT ExtractedContainerVT = ExtractedVT;
      if (ExtractedContainerVT.isFixedLengthVector()) {
        ExtractedContainerVT = getContainerForFixedLengthVector(
            DAG, ExtractedContainerVT, Subtarget);
        ExtractedVal = convertToScalableVector(ExtractedContainerVT,
                                               ExtractedVal, DAG, Subtarget);
      }
      if (ExtractedContainerVT.bitsLE(VT))
        return DAG.getNode(ISD::INSERT_SUBVECTOR, DL, VT, Passthru,
                           ExtractedVal, DAG.getVectorIdxConstant(0, DL));
      return DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, ExtractedVal,
                         DAG.getVectorIdxConstant(0, DL));
    }
  }


  if (VT.isFloatingPoint())
    return DAG.getNode(RISCVISD::VFMV_S_F_VL, DL, VT,
                       DAG.getUNDEF(VT), Scalar, VL);

  // Avoid the tricky legalization cases by falling back to using the
  // splat code which already handles it gracefully.
  if (!Scalar.getValueType().bitsLE(XLenVT))
    return lowerScalarSplat(DAG.getUNDEF(VT), Scalar,
                            DAG.getConstant(1, DL, XLenVT),
                            VT, DL, DAG, Subtarget);

  // If the operand is a constant, sign extend to increase our chances
  // of being able to use a .vi instruction. ANY_EXTEND would become a
  // a zero extend and the simm5 check in isel would fail.
  // FIXME: Should we ignore the upper bits in isel instead?
  unsigned ExtOpc =
    isa<ConstantSDNode>(Scalar) ? ISD::SIGN_EXTEND : ISD::ANY_EXTEND;
  Scalar = DAG.getNode(ExtOpc, DL, XLenVT, Scalar);
  return DAG.getNode(RISCVISD::VMV_S_X_VL, DL, VT,
                     DAG.getUNDEF(VT), Scalar, VL);
}

// Is this a shuffle extracts either the even or odd elements of a vector?
// That is, specifically, either (a) or (b) below.
// t34: v8i8 = extract_subvector t11, Constant:i64<0>
// t33: v8i8 = extract_subvector t11, Constant:i64<8>
// a) t35: v8i8 = vector_shuffle<0,2,4,6,8,10,12,14> t34, t33
// b) t35: v8i8 = vector_shuffle<1,3,5,7,9,11,13,15> t34, t33
// Returns {Src Vector, Even Elements} om success
static bool isDeinterleaveShuffle(MVT VT, MVT ContainerVT, SDValue V1,
                                  SDValue V2, ArrayRef<int> Mask,
                                  const RISCVSubtarget &Subtarget) {
  // Need to be able to widen the vector.
  if (VT.getScalarSizeInBits() >= Subtarget.getELen())
    return false;

  // Both input must be extracts.
  if (V1.getOpcode() != ISD::EXTRACT_SUBVECTOR ||
      V2.getOpcode() != ISD::EXTRACT_SUBVECTOR)
    return false;

  // Extracting from the same source.
  SDValue Src = V1.getOperand(0);
  if (Src != V2.getOperand(0))
    return false;

  // Src needs to have twice the number of elements.
  if (Src.getValueType().getVectorNumElements() != (Mask.size() * 2))
    return false;

  // The extracts must extract the two halves of the source.
  if (V1.getConstantOperandVal(1) != 0 ||
      V2.getConstantOperandVal(1) != Mask.size())
    return false;

  // First index must be the first even or odd element from V1.
  if (Mask[0] != 0 && Mask[0] != 1)
    return false;

  // The others must increase by 2 each time.
  // TODO: Support undef elements?
  for (unsigned i = 1; i != Mask.size(); ++i)
    if (Mask[i] != Mask[i - 1] + 2)
      return false;

  return true;
}

/// Is this shuffle interleaving contiguous elements from one vector into the
/// even elements and contiguous elements from another vector into the odd
/// elements. \p EvenSrc will contain the element that should be in the first
/// even element. \p OddSrc will contain the element that should be in the first
/// odd element. These can be the first element in a source or the element half
/// way through the source.
static bool isInterleaveShuffle(ArrayRef<int> Mask, MVT VT, int &EvenSrc,
                                int &OddSrc, const RISCVSubtarget &Subtarget) {
  // We need to be able to widen elements to the next larger integer type.
  if (VT.getScalarSizeInBits() >= Subtarget.getELen())
    return false;

  int Size = Mask.size();
  int NumElts = VT.getVectorNumElements();
  assert(Size == (int)NumElts && "Unexpected mask size");

  SmallVector<unsigned, 2> StartIndexes;
  if (!ShuffleVectorInst::isInterleaveMask(Mask, 2, Size * 2, StartIndexes))
    return false;

  EvenSrc = StartIndexes[0];
  OddSrc = StartIndexes[1];

  // One source should be low half of first vector.
  if (EvenSrc != 0 && OddSrc != 0)
    return false;

  // Subvectors will be subtracted from either at the start of the two input
  // vectors, or at the start and middle of the first vector if it's an unary
  // interleave.
  // In both cases, HalfNumElts will be extracted.
  // We need to ensure that the extract indices are 0 or HalfNumElts otherwise
  // we'll create an illegal extract_subvector.
  // FIXME: We could support other values using a slidedown first.
  int HalfNumElts = NumElts / 2;
  return ((EvenSrc % HalfNumElts) == 0) && ((OddSrc % HalfNumElts) == 0);
}

/// Match shuffles that concatenate two vectors, rotate the concatenation,
/// and then extract the original number of elements from the rotated result.
/// This is equivalent to vector.splice or X86's PALIGNR instruction. The
/// returned rotation amount is for a rotate right, where elements move from
/// higher elements to lower elements. \p LoSrc indicates the first source
/// vector of the rotate or -1 for undef. \p HiSrc indicates the second vector
/// of the rotate or -1 for undef. At least one of \p LoSrc and \p HiSrc will be
/// 0 or 1 if a rotation is found.
///
/// NOTE: We talk about rotate to the right which matches how bit shift and
/// rotate instructions are described where LSBs are on the right, but LLVM IR
/// and the table below write vectors with the lowest elements on the left.
static int isElementRotate(int &LoSrc, int &HiSrc, ArrayRef<int> Mask) {
  int Size = Mask.size();

  // We need to detect various ways of spelling a rotation:
  //   [11, 12, 13, 14, 15,  0,  1,  2]
  //   [-1, 12, 13, 14, -1, -1,  1, -1]
  //   [-1, -1, -1, -1, -1, -1,  1,  2]
  //   [ 3,  4,  5,  6,  7,  8,  9, 10]
  //   [-1,  4,  5,  6, -1, -1,  9, -1]
  //   [-1,  4,  5,  6, -1, -1, -1, -1]
  int Rotation = 0;
  LoSrc = -1;
  HiSrc = -1;
  for (int i = 0; i != Size; ++i) {
    int M = Mask[i];
    if (M < 0)
      continue;

    // Determine where a rotate vector would have started.
    int StartIdx = i - (M % Size);
    // The identity rotation isn't interesting, stop.
    if (StartIdx == 0)
      return -1;

    // If we found the tail of a vector the rotation must be the missing
    // front. If we found the head of a vector, it must be how much of the
    // head.
    int CandidateRotation = StartIdx < 0 ? -StartIdx : Size - StartIdx;

    if (Rotation == 0)
      Rotation = CandidateRotation;
    else if (Rotation != CandidateRotation)
      // The rotations don't match, so we can't match this mask.
      return -1;

    // Compute which value this mask is pointing at.
    int MaskSrc = M < Size ? 0 : 1;

    // Compute which of the two target values this index should be assigned to.
    // This reflects whether the high elements are remaining or the low elemnts
    // are remaining.
    int &TargetSrc = StartIdx < 0 ? HiSrc : LoSrc;

    // Either set up this value if we've not encountered it before, or check
    // that it remains consistent.
    if (TargetSrc < 0)
      TargetSrc = MaskSrc;
    else if (TargetSrc != MaskSrc)
      // This may be a rotation, but it pulls from the inputs in some
      // unsupported interleaving.
      return -1;
  }

  // Check that we successfully analyzed the mask, and normalize the results.
  assert(Rotation != 0 && "Failed to locate a viable rotation!");
  assert((LoSrc >= 0 || HiSrc >= 0) &&
         "Failed to find a rotated input vector!");

  return Rotation;
}

// Lower a deinterleave shuffle to vnsrl.
// [a, p, b, q, c, r, d, s] -> [a, b, c, d] (EvenElts == true)
//                          -> [p, q, r, s] (EvenElts == false)
// VT is the type of the vector to return, <[vscale x ]n x ty>
// Src is the vector to deinterleave of type <[vscale x ]n*2 x ty>
static SDValue getDeinterleaveViaVNSRL(const SDLoc &DL, MVT VT, SDValue Src,
                                       bool EvenElts,
                                       const RISCVSubtarget &Subtarget,
                                       SelectionDAG &DAG) {
  // The result is a vector of type <m x n x ty>
  MVT ContainerVT = VT;
  // Convert fixed vectors to scalable if needed
  if (ContainerVT.isFixedLengthVector()) {
    assert(Src.getSimpleValueType().isFixedLengthVector());
    ContainerVT = getContainerForFixedLengthVector(DAG, ContainerVT, Subtarget);

    // The source is a vector of type <m x n*2 x ty>
    MVT SrcContainerVT =
        MVT::getVectorVT(ContainerVT.getVectorElementType(),
                         ContainerVT.getVectorElementCount() * 2);
    Src = convertToScalableVector(SrcContainerVT, Src, DAG, Subtarget);
  }

  auto [TrueMask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  // Bitcast the source vector from <m x n*2 x ty> -> <m x n x ty*2>
  // This also converts FP to int.
  unsigned EltBits = ContainerVT.getScalarSizeInBits();
  MVT WideSrcContainerVT = MVT::getVectorVT(
      MVT::getIntegerVT(EltBits * 2), ContainerVT.getVectorElementCount());
  Src = DAG.getBitcast(WideSrcContainerVT, Src);

  // The integer version of the container type.
  MVT IntContainerVT = ContainerVT.changeVectorElementTypeToInteger();

  // If we want even elements, then the shift amount is 0. Otherwise, shift by
  // the original element size.
  unsigned Shift = EvenElts ? 0 : EltBits;
  SDValue SplatShift = DAG.getNode(
      RISCVISD::VMV_V_X_VL, DL, IntContainerVT, DAG.getUNDEF(ContainerVT),
      DAG.getConstant(Shift, DL, Subtarget.getXLenVT()), VL);
  SDValue Res =
      DAG.getNode(RISCVISD::VNSRL_VL, DL, IntContainerVT, Src, SplatShift,
                  DAG.getUNDEF(IntContainerVT), TrueMask, VL);
  // Cast back to FP if needed.
  Res = DAG.getBitcast(ContainerVT, Res);

  if (VT.isFixedLengthVector())
    Res = convertFromScalableVector(VT, Res, DAG, Subtarget);
  return Res;
}

// Lower the following shuffle to vslidedown.
// a)
// t49: v8i8 = extract_subvector t13, Constant:i64<0>
// t109: v8i8 = extract_subvector t13, Constant:i64<8>
// t108: v8i8 = vector_shuffle<1,2,3,4,5,6,7,8> t49, t106
// b)
// t69: v16i16 = extract_subvector t68, Constant:i64<0>
// t23: v8i16 = extract_subvector t69, Constant:i64<0>
// t29: v4i16 = extract_subvector t23, Constant:i64<4>
// t26: v8i16 = extract_subvector t69, Constant:i64<8>
// t30: v4i16 = extract_subvector t26, Constant:i64<0>
// t54: v4i16 = vector_shuffle<1,2,3,4> t29, t30
static SDValue lowerVECTOR_SHUFFLEAsVSlidedown(const SDLoc &DL, MVT VT,
                                               SDValue V1, SDValue V2,
                                               ArrayRef<int> Mask,
                                               const RISCVSubtarget &Subtarget,
                                               SelectionDAG &DAG) {
  auto findNonEXTRACT_SUBVECTORParent =
      [](SDValue Parent) -> std::pair<SDValue, uint64_t> {
    uint64_t Offset = 0;
    while (Parent.getOpcode() == ISD::EXTRACT_SUBVECTOR &&
           // EXTRACT_SUBVECTOR can be used to extract a fixed-width vector from
           // a scalable vector. But we don't want to match the case.
           Parent.getOperand(0).getSimpleValueType().isFixedLengthVector()) {
      Offset += Parent.getConstantOperandVal(1);
      Parent = Parent.getOperand(0);
    }
    return std::make_pair(Parent, Offset);
  };

  auto [V1Src, V1IndexOffset] = findNonEXTRACT_SUBVECTORParent(V1);
  auto [V2Src, V2IndexOffset] = findNonEXTRACT_SUBVECTORParent(V2);

  // Extracting from the same source.
  SDValue Src = V1Src;
  if (Src != V2Src)
    return SDValue();

  // Rebuild mask because Src may be from multiple EXTRACT_SUBVECTORs.
  SmallVector<int, 16> NewMask(Mask);
  for (size_t i = 0; i != NewMask.size(); ++i) {
    if (NewMask[i] == -1)
      continue;

    if (static_cast<size_t>(NewMask[i]) < NewMask.size()) {
      NewMask[i] = NewMask[i] + V1IndexOffset;
    } else {
      // Minus NewMask.size() is needed. Otherwise, the b case would be
      // <5,6,7,12> instead of <5,6,7,8>.
      NewMask[i] = NewMask[i] - NewMask.size() + V2IndexOffset;
    }
  }

  // First index must be known and non-zero. It will be used as the slidedown
  // amount.
  if (NewMask[0] <= 0)
    return SDValue();

  // NewMask is also continuous.
  for (unsigned i = 1; i != NewMask.size(); ++i)
    if (NewMask[i - 1] + 1 != NewMask[i])
      return SDValue();

  MVT XLenVT = Subtarget.getXLenVT();
  MVT SrcVT = Src.getSimpleValueType();
  MVT ContainerVT = getContainerForFixedLengthVector(DAG, SrcVT, Subtarget);
  auto [TrueMask, VL] = getDefaultVLOps(SrcVT, ContainerVT, DL, DAG, Subtarget);
  SDValue Slidedown =
      getVSlidedown(DAG, Subtarget, DL, ContainerVT, DAG.getUNDEF(ContainerVT),
                    convertToScalableVector(ContainerVT, Src, DAG, Subtarget),
                    DAG.getConstant(NewMask[0], DL, XLenVT), TrueMask, VL);
  return DAG.getNode(
      ISD::EXTRACT_SUBVECTOR, DL, VT,
      convertFromScalableVector(SrcVT, Slidedown, DAG, Subtarget),
      DAG.getConstant(0, DL, XLenVT));
}

// Because vslideup leaves the destination elements at the start intact, we can
// use it to perform shuffles that insert subvectors:
//
// vector_shuffle v8:v8i8, v9:v8i8, <0, 1, 2, 3, 8, 9, 10, 11>
// ->
// vsetvli zero, 8, e8, mf2, ta, ma
// vslideup.vi v8, v9, 4
//
// vector_shuffle v8:v8i8, v9:v8i8 <0, 1, 8, 9, 10, 5, 6, 7>
// ->
// vsetvli zero, 5, e8, mf2, tu, ma
// vslideup.v1 v8, v9, 2
static SDValue lowerVECTOR_SHUFFLEAsVSlideup(const SDLoc &DL, MVT VT,
                                             SDValue V1, SDValue V2,
                                             ArrayRef<int> Mask,
                                             const RISCVSubtarget &Subtarget,
                                             SelectionDAG &DAG) {
  unsigned NumElts = VT.getVectorNumElements();
  int NumSubElts, Index;
  if (!ShuffleVectorInst::isInsertSubvectorMask(Mask, NumElts, NumSubElts,
                                                Index))
    return SDValue();

  bool OpsSwapped = Mask[Index] < (int)NumElts;
  SDValue InPlace = OpsSwapped ? V2 : V1;
  SDValue ToInsert = OpsSwapped ? V1 : V2;

  MVT XLenVT = Subtarget.getXLenVT();
  MVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
  auto TrueMask = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).first;
  // We slide up by the index that the subvector is being inserted at, and set
  // VL to the index + the number of elements being inserted.
  unsigned Policy = RISCVII::TAIL_UNDISTURBED_MASK_UNDISTURBED | RISCVII::MASK_AGNOSTIC;
  // If the we're adding a suffix to the in place vector, i.e. inserting right
  // up to the very end of it, then we don't actually care about the tail.
  if (NumSubElts + Index >= (int)NumElts)
    Policy |= RISCVII::TAIL_AGNOSTIC;

  InPlace = convertToScalableVector(ContainerVT, InPlace, DAG, Subtarget);
  ToInsert = convertToScalableVector(ContainerVT, ToInsert, DAG, Subtarget);
  SDValue VL = DAG.getConstant(NumSubElts + Index, DL, XLenVT);

  SDValue Res;
  // If we're inserting into the lowest elements, use a tail undisturbed
  // vmv.v.v.
  if (Index == 0)
    Res = DAG.getNode(RISCVISD::VMV_V_V_VL, DL, ContainerVT, InPlace, ToInsert,
                      VL);
  else
    Res = getVSlideup(DAG, Subtarget, DL, ContainerVT, InPlace, ToInsert,
                      DAG.getConstant(Index, DL, XLenVT), TrueMask, VL, Policy);
  return convertFromScalableVector(VT, Res, DAG, Subtarget);
}

/// Match v(f)slide1up/down idioms.  These operations involve sliding
/// N-1 elements to make room for an inserted scalar at one end.
static SDValue lowerVECTOR_SHUFFLEAsVSlide1(const SDLoc &DL, MVT VT,
                                            SDValue V1, SDValue V2,
                                            ArrayRef<int> Mask,
                                            const RISCVSubtarget &Subtarget,
                                            SelectionDAG &DAG) {
  bool OpsSwapped = false;
  if (!isa<BuildVectorSDNode>(V1)) {
    if (!isa<BuildVectorSDNode>(V2))
      return SDValue();
    std::swap(V1, V2);
    OpsSwapped = true;
  }
  SDValue Splat = cast<BuildVectorSDNode>(V1)->getSplatValue();
  if (!Splat)
    return SDValue();

  // Return true if the mask could describe a slide of Mask.size() - 1
  // elements from concat_vector(V1, V2)[Base:] to [Offset:].
  auto isSlideMask = [](ArrayRef<int> Mask, unsigned Base, int Offset) {
    const unsigned S = (Offset > 0) ? 0 : -Offset;
    const unsigned E = Mask.size() - ((Offset > 0) ? Offset : 0);
    for (unsigned i = S; i != E; ++i)
      if (Mask[i] >= 0 && (unsigned)Mask[i] != Base + i + Offset)
        return false;
    return true;
  };

  const unsigned NumElts = VT.getVectorNumElements();
  bool IsVSlidedown = isSlideMask(Mask, OpsSwapped ? 0 : NumElts, 1);
  if (!IsVSlidedown && !isSlideMask(Mask, OpsSwapped ? 0 : NumElts, -1))
    return SDValue();

  const int InsertIdx = Mask[IsVSlidedown ? (NumElts - 1) : 0];
  // Inserted lane must come from splat, undef scalar is legal but not profitable.
  if (InsertIdx < 0 || InsertIdx / NumElts != (unsigned)OpsSwapped)
    return SDValue();

  MVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
  auto [TrueMask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);
  auto OpCode = IsVSlidedown ?
    (VT.isFloatingPoint() ? RISCVISD::VFSLIDE1DOWN_VL : RISCVISD::VSLIDE1DOWN_VL) :
    (VT.isFloatingPoint() ? RISCVISD::VFSLIDE1UP_VL : RISCVISD::VSLIDE1UP_VL);
  if (!VT.isFloatingPoint())
    Splat = DAG.getNode(ISD::ANY_EXTEND, DL, Subtarget.getXLenVT(), Splat);
  auto Vec = DAG.getNode(OpCode, DL, ContainerVT,
                         DAG.getUNDEF(ContainerVT),
                         convertToScalableVector(ContainerVT, V2, DAG, Subtarget),
                         Splat, TrueMask, VL);
  return convertFromScalableVector(VT, Vec, DAG, Subtarget);
}

// Given two input vectors of <[vscale x ]n x ty>, use vwaddu.vv and vwmaccu.vx
// to create an interleaved vector of <[vscale x] n*2 x ty>.
// This requires that the size of ty is less than the subtarget's maximum ELEN.
static SDValue getWideningInterleave(SDValue EvenV, SDValue OddV,
                                     const SDLoc &DL, SelectionDAG &DAG,
                                     const RISCVSubtarget &Subtarget) {
  MVT VecVT = EvenV.getSimpleValueType();
  MVT VecContainerVT = VecVT; // <vscale x n x ty>
  // Convert fixed vectors to scalable if needed
  if (VecContainerVT.isFixedLengthVector()) {
    VecContainerVT = getContainerForFixedLengthVector(DAG, VecVT, Subtarget);
    EvenV = convertToScalableVector(VecContainerVT, EvenV, DAG, Subtarget);
    OddV = convertToScalableVector(VecContainerVT, OddV, DAG, Subtarget);
  }

  assert(VecVT.getScalarSizeInBits() < Subtarget.getELen());

  // We're working with a vector of the same size as the resulting
  // interleaved vector, but with half the number of elements and
  // twice the SEW (Hence the restriction on not using the maximum
  // ELEN)
  MVT WideVT =
      MVT::getVectorVT(MVT::getIntegerVT(VecVT.getScalarSizeInBits() * 2),
                       VecVT.getVectorElementCount());
  MVT WideContainerVT = WideVT; // <vscale x n x ty*2>
  if (WideContainerVT.isFixedLengthVector())
    WideContainerVT = getContainerForFixedLengthVector(DAG, WideVT, Subtarget);

  // Bitcast the input vectors to integers in case they are FP
  VecContainerVT = VecContainerVT.changeTypeToInteger();
  EvenV = DAG.getBitcast(VecContainerVT, EvenV);
  OddV = DAG.getBitcast(VecContainerVT, OddV);

  auto [Mask, VL] = getDefaultVLOps(VecVT, VecContainerVT, DL, DAG, Subtarget);
  SDValue Passthru = DAG.getUNDEF(WideContainerVT);

  SDValue Interleaved;
  if (OddV.isUndef()) {
    // If OddV is undef, this is a zero extend.
    // FIXME: Not only does this optimize the code, it fixes some correctness
    // issues because MIR does not have freeze.
    Interleaved =
        DAG.getNode(RISCVISD::VZEXT_VL, DL, WideContainerVT, EvenV, Mask, VL);
  } else if (Subtarget.hasStdExtZvbb()) {
    // Interleaved = (OddV << VecVT.getScalarSizeInBits()) + EvenV.
    SDValue OffsetVec =
        DAG.getConstant(VecVT.getScalarSizeInBits(), DL, VecContainerVT);
    Interleaved = DAG.getNode(RISCVISD::VWSLL_VL, DL, WideContainerVT, OddV,
                              OffsetVec, Passthru, Mask, VL);
    if (!EvenV.isUndef())
      Interleaved = DAG.getNode(RISCVISD::VWADDU_W_VL, DL, WideContainerVT,
                                Interleaved, EvenV, Passthru, Mask, VL);
  } else if (EvenV.isUndef()) {
    Interleaved =
        DAG.getNode(RISCVISD::VZEXT_VL, DL, WideContainerVT, OddV, Mask, VL);

    SDValue OffsetVec =
        DAG.getConstant(VecVT.getScalarSizeInBits(), DL, WideContainerVT);
    Interleaved = DAG.getNode(RISCVISD::SHL_VL, DL, WideContainerVT,
                              Interleaved, OffsetVec, Passthru, Mask, VL);
  } else {
    // FIXME: We should freeze the odd vector here. We already handled the case
    // of provably undef/poison above.

    // Widen EvenV and OddV with 0s and add one copy of OddV to EvenV with
    // vwaddu.vv
    Interleaved = DAG.getNode(RISCVISD::VWADDU_VL, DL, WideContainerVT, EvenV,
                              OddV, Passthru, Mask, VL);

    // Then get OddV * by 2^(VecVT.getScalarSizeInBits() - 1)
    SDValue AllOnesVec = DAG.getSplatVector(
        VecContainerVT, DL, DAG.getAllOnesConstant(DL, Subtarget.getXLenVT()));
    SDValue OddsMul = DAG.getNode(RISCVISD::VWMULU_VL, DL, WideContainerVT,
                                  OddV, AllOnesVec, Passthru, Mask, VL);

    // Add the two together so we get
    //   (OddV * 0xff...ff) + (OddV + EvenV)
    // = (OddV * 0x100...00) + EvenV
    // = (OddV << VecVT.getScalarSizeInBits()) + EvenV
    // Note the ADD_VL and VLMULU_VL should get selected as vwmaccu.vx
    Interleaved = DAG.getNode(RISCVISD::ADD_VL, DL, WideContainerVT,
                              Interleaved, OddsMul, Passthru, Mask, VL);
  }

  // Bitcast from <vscale x n * ty*2> to <vscale x 2*n x ty>
  MVT ResultContainerVT = MVT::getVectorVT(
      VecVT.getVectorElementType(), // Make sure to use original type
      VecContainerVT.getVectorElementCount().multiplyCoefficientBy(2));
  Interleaved = DAG.getBitcast(ResultContainerVT, Interleaved);

  // Convert back to a fixed vector if needed
  MVT ResultVT =
      MVT::getVectorVT(VecVT.getVectorElementType(),
                       VecVT.getVectorElementCount().multiplyCoefficientBy(2));
  if (ResultVT.isFixedLengthVector())
    Interleaved =
        convertFromScalableVector(ResultVT, Interleaved, DAG, Subtarget);

  return Interleaved;
}

// If we have a vector of bits that we want to reverse, we can use a vbrev on a
// larger element type, e.g. v32i1 can be reversed with a v1i32 bitreverse.
static SDValue lowerBitreverseShuffle(ShuffleVectorSDNode *SVN,
                                      SelectionDAG &DAG,
                                      const RISCVSubtarget &Subtarget) {
  SDLoc DL(SVN);
  MVT VT = SVN->getSimpleValueType(0);
  SDValue V = SVN->getOperand(0);
  unsigned NumElts = VT.getVectorNumElements();

  assert(VT.getVectorElementType() == MVT::i1);

  if (!ShuffleVectorInst::isReverseMask(SVN->getMask(),
                                        SVN->getMask().size()) ||
      !SVN->getOperand(1).isUndef())
    return SDValue();

  unsigned ViaEltSize = std::max((uint64_t)8, PowerOf2Ceil(NumElts));
  EVT ViaVT = EVT::getVectorVT(
      *DAG.getContext(), EVT::getIntegerVT(*DAG.getContext(), ViaEltSize), 1);
  EVT ViaBitVT =
      EVT::getVectorVT(*DAG.getContext(), MVT::i1, ViaVT.getScalarSizeInBits());

  // If we don't have zvbb or the larger element type > ELEN, the operation will
  // be illegal.
  if (!Subtarget.getTargetLowering()->isOperationLegalOrCustom(ISD::BITREVERSE,
                                                               ViaVT) ||
      !Subtarget.getTargetLowering()->isTypeLegal(ViaBitVT))
    return SDValue();

  // If the bit vector doesn't fit exactly into the larger element type, we need
  // to insert it into the larger vector and then shift up the reversed bits
  // afterwards to get rid of the gap introduced.
  if (ViaEltSize > NumElts)
    V = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ViaBitVT, DAG.getUNDEF(ViaBitVT),
                    V, DAG.getVectorIdxConstant(0, DL));

  SDValue Res =
      DAG.getNode(ISD::BITREVERSE, DL, ViaVT, DAG.getBitcast(ViaVT, V));

  // Shift up the reversed bits if the vector didn't exactly fit into the larger
  // element type.
  if (ViaEltSize > NumElts)
    Res = DAG.getNode(ISD::SRL, DL, ViaVT, Res,
                      DAG.getConstant(ViaEltSize - NumElts, DL, ViaVT));

  Res = DAG.getBitcast(ViaBitVT, Res);

  if (ViaEltSize > NumElts)
    Res = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Res,
                      DAG.getVectorIdxConstant(0, DL));
  return Res;
}

static bool isLegalBitRotate(ShuffleVectorSDNode *SVN,
                             SelectionDAG &DAG,
                             const RISCVSubtarget &Subtarget,
                             MVT &RotateVT, unsigned &RotateAmt) {
  SDLoc DL(SVN);

  EVT VT = SVN->getValueType(0);
  unsigned NumElts = VT.getVectorNumElements();
  unsigned EltSizeInBits = VT.getScalarSizeInBits();
  unsigned NumSubElts;
  if (!ShuffleVectorInst::isBitRotateMask(SVN->getMask(), EltSizeInBits, 2,
                                          NumElts, NumSubElts, RotateAmt))
    return false;
  RotateVT = MVT::getVectorVT(MVT::getIntegerVT(EltSizeInBits * NumSubElts),
                                  NumElts / NumSubElts);

  // We might have a RotateVT that isn't legal, e.g. v4i64 on zve32x.
  return Subtarget.getTargetLowering()->isTypeLegal(RotateVT);
}

// Given a shuffle mask like <3, 0, 1, 2, 7, 4, 5, 6> for v8i8, we can
// reinterpret it as a v2i32 and rotate it right by 8 instead. We can lower this
// as a vror.vi if we have Zvkb, or otherwise as a vsll, vsrl and vor.
static SDValue lowerVECTOR_SHUFFLEAsRotate(ShuffleVectorSDNode *SVN,
                                           SelectionDAG &DAG,
                                           const RISCVSubtarget &Subtarget) {
  SDLoc DL(SVN);

  EVT VT = SVN->getValueType(0);
  unsigned RotateAmt;
  MVT RotateVT;
  if (!isLegalBitRotate(SVN, DAG, Subtarget, RotateVT, RotateAmt))
    return SDValue();

  SDValue Op = DAG.getBitcast(RotateVT, SVN->getOperand(0));

  SDValue Rotate;
  // A rotate of an i16 by 8 bits either direction is equivalent to a byteswap,
  // so canonicalize to vrev8.
  if (RotateVT.getScalarType() == MVT::i16 && RotateAmt == 8)
    Rotate = DAG.getNode(ISD::BSWAP, DL, RotateVT, Op);
  else
    Rotate = DAG.getNode(ISD::ROTL, DL, RotateVT, Op,
                         DAG.getConstant(RotateAmt, DL, RotateVT));

  return DAG.getBitcast(VT, Rotate);
}

// If compiling with an exactly known VLEN, see if we can split a
// shuffle on m2 or larger into a small number of m1 sized shuffles
// which write each destination registers exactly once.
static SDValue lowerShuffleViaVRegSplitting(ShuffleVectorSDNode *SVN,
                                            SelectionDAG &DAG,
                                            const RISCVSubtarget &Subtarget) {
  SDLoc DL(SVN);
  MVT VT = SVN->getSimpleValueType(0);
  SDValue V1 = SVN->getOperand(0);
  SDValue V2 = SVN->getOperand(1);
  ArrayRef<int> Mask = SVN->getMask();
  unsigned NumElts = VT.getVectorNumElements();

  // If we don't know exact data layout, not much we can do.  If this
  // is already m1 or smaller, no point in splitting further.
  const auto VLen = Subtarget.getRealVLen();
  if (!VLen || VT.getSizeInBits().getFixedValue() <= *VLen)
    return SDValue();

  // Avoid picking up bitrotate patterns which we have a linear-in-lmul
  // expansion for.
  unsigned RotateAmt;
  MVT RotateVT;
  if (isLegalBitRotate(SVN, DAG, Subtarget, RotateVT, RotateAmt))
    return SDValue();

  MVT ElemVT = VT.getVectorElementType();
  unsigned ElemsPerVReg = *VLen / ElemVT.getFixedSizeInBits();
  unsigned VRegsPerSrc = NumElts / ElemsPerVReg;

  SmallVector<std::pair<int, SmallVector<int>>>
    OutMasks(VRegsPerSrc, {-1, {}});

  // Check if our mask can be done as a 1-to-1 mapping from source
  // to destination registers in the group without needing to
  // write each destination more than once.
  for (unsigned DstIdx = 0; DstIdx < Mask.size(); DstIdx++) {
    int DstVecIdx = DstIdx / ElemsPerVReg;
    int DstSubIdx = DstIdx % ElemsPerVReg;
    int SrcIdx = Mask[DstIdx];
    if (SrcIdx < 0 || (unsigned)SrcIdx >= 2 * NumElts)
      continue;
    int SrcVecIdx = SrcIdx / ElemsPerVReg;
    int SrcSubIdx = SrcIdx % ElemsPerVReg;
    if (OutMasks[DstVecIdx].first == -1)
      OutMasks[DstVecIdx].first = SrcVecIdx;
    if (OutMasks[DstVecIdx].first != SrcVecIdx)
      // Note: This case could easily be handled by keeping track of a chain
      // of source values and generating two element shuffles below.  This is
      // less an implementation question, and more a profitability one.
      return SDValue();

    OutMasks[DstVecIdx].second.resize(ElemsPerVReg, -1);
    OutMasks[DstVecIdx].second[DstSubIdx] = SrcSubIdx;
  }

  EVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
  MVT OneRegVT = MVT::getVectorVT(ElemVT, ElemsPerVReg);
  MVT M1VT = getContainerForFixedLengthVector(DAG, OneRegVT, Subtarget);
  assert(M1VT == getLMUL1VT(M1VT));
  unsigned NumOpElts = M1VT.getVectorMinNumElements();
  SDValue Vec = DAG.getUNDEF(ContainerVT);
  // The following semantically builds up a fixed length concat_vector
  // of the component shuffle_vectors.  We eagerly lower to scalable here
  // to avoid DAG combining it back to a large shuffle_vector again.
  V1 = convertToScalableVector(ContainerVT, V1, DAG, Subtarget);
  V2 = convertToScalableVector(ContainerVT, V2, DAG, Subtarget);
  for (unsigned DstVecIdx = 0 ; DstVecIdx < OutMasks.size(); DstVecIdx++) {
    auto &[SrcVecIdx, SrcSubMask] = OutMasks[DstVecIdx];
    if (SrcVecIdx == -1)
      continue;
    unsigned ExtractIdx = (SrcVecIdx % VRegsPerSrc) * NumOpElts;
    SDValue SrcVec = (unsigned)SrcVecIdx >= VRegsPerSrc ? V2 : V1;
    SDValue SubVec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, M1VT, SrcVec,
                                 DAG.getVectorIdxConstant(ExtractIdx, DL));
    SubVec = convertFromScalableVector(OneRegVT, SubVec, DAG, Subtarget);
    SubVec = DAG.getVectorShuffle(OneRegVT, DL, SubVec, SubVec, SrcSubMask);
    SubVec = convertToScalableVector(M1VT, SubVec, DAG, Subtarget);
    unsigned InsertIdx = DstVecIdx * NumOpElts;
    Vec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ContainerVT, Vec, SubVec,
                      DAG.getVectorIdxConstant(InsertIdx, DL));
  }
  return convertFromScalableVector(VT, Vec, DAG, Subtarget);
}

static SDValue lowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG,
                                   const RISCVSubtarget &Subtarget) {
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();
  MVT VT = Op.getSimpleValueType();
  unsigned NumElts = VT.getVectorNumElements();
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(Op.getNode());

  if (VT.getVectorElementType() == MVT::i1) {
    // Lower to a vror.vi of a larger element type if possible before we promote
    // i1s to i8s.
    if (SDValue V = lowerVECTOR_SHUFFLEAsRotate(SVN, DAG, Subtarget))
      return V;
    if (SDValue V = lowerBitreverseShuffle(SVN, DAG, Subtarget))
      return V;

    // Promote i1 shuffle to i8 shuffle.
    MVT WidenVT = MVT::getVectorVT(MVT::i8, VT.getVectorElementCount());
    V1 = DAG.getNode(ISD::ZERO_EXTEND, DL, WidenVT, V1);
    V2 = V2.isUndef() ? DAG.getUNDEF(WidenVT)
                      : DAG.getNode(ISD::ZERO_EXTEND, DL, WidenVT, V2);
    SDValue Shuffled = DAG.getVectorShuffle(WidenVT, DL, V1, V2, SVN->getMask());
    return DAG.getSetCC(DL, VT, Shuffled, DAG.getConstant(0, DL, WidenVT),
                        ISD::SETNE);
  }

  MVT ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);

  auto [TrueMask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  if (SVN->isSplat()) {
    const int Lane = SVN->getSplatIndex();
    if (Lane >= 0) {
      MVT SVT = VT.getVectorElementType();

      // Turn splatted vector load into a strided load with an X0 stride.
      SDValue V = V1;
      // Peek through CONCAT_VECTORS as VectorCombine can concat a vector
      // with undef.
      // FIXME: Peek through INSERT_SUBVECTOR, EXTRACT_SUBVECTOR, bitcasts?
      int Offset = Lane;
      if (V.getOpcode() == ISD::CONCAT_VECTORS) {
        int OpElements =
            V.getOperand(0).getSimpleValueType().getVectorNumElements();
        V = V.getOperand(Offset / OpElements);
        Offset %= OpElements;
      }

      // We need to ensure the load isn't atomic or volatile.
      if (ISD::isNormalLoad(V.getNode()) && cast<LoadSDNode>(V)->isSimple()) {
        auto *Ld = cast<LoadSDNode>(V);
        Offset *= SVT.getStoreSize();
        SDValue NewAddr = DAG.getMemBasePlusOffset(
            Ld->getBasePtr(), TypeSize::getFixed(Offset), DL);

        // If this is SEW=64 on RV32, use a strided load with a stride of x0.
        if (SVT.isInteger() && SVT.bitsGT(XLenVT)) {
          SDVTList VTs = DAG.getVTList({ContainerVT, MVT::Other});
          SDValue IntID =
              DAG.getTargetConstant(Intrinsic::riscv_vlse, DL, XLenVT);
          SDValue Ops[] = {Ld->getChain(),
                           IntID,
                           DAG.getUNDEF(ContainerVT),
                           NewAddr,
                           DAG.getRegister(RISCV::X0, XLenVT),
                           VL};
          SDValue NewLoad = DAG.getMemIntrinsicNode(
              ISD::INTRINSIC_W_CHAIN, DL, VTs, Ops, SVT,
              DAG.getMachineFunction().getMachineMemOperand(
                  Ld->getMemOperand(), Offset, SVT.getStoreSize()));
          DAG.makeEquivalentMemoryOrdering(Ld, NewLoad);
          return convertFromScalableVector(VT, NewLoad, DAG, Subtarget);
        }

        MVT SplatVT = ContainerVT;

        // If we don't have Zfh, we need to use an integer scalar load.
        if (SVT == MVT::f16 && !Subtarget.hasStdExtZfh()) {
          SVT = MVT::i16;
          SplatVT = ContainerVT.changeVectorElementType(SVT);
        }

        // Otherwise use a scalar load and splat. This will give the best
        // opportunity to fold a splat into the operation. ISel can turn it into
        // the x0 strided load if we aren't able to fold away the select.
        if (SVT.isFloatingPoint())
          V = DAG.getLoad(SVT, DL, Ld->getChain(), NewAddr,
                          Ld->getPointerInfo().getWithOffset(Offset),
                          Ld->getOriginalAlign(),
                          Ld->getMemOperand()->getFlags());
        else
          V = DAG.getExtLoad(ISD::EXTLOAD, DL, XLenVT, Ld->getChain(), NewAddr,
                             Ld->getPointerInfo().getWithOffset(Offset), SVT,
                             Ld->getOriginalAlign(),
                             Ld->getMemOperand()->getFlags());
        DAG.makeEquivalentMemoryOrdering(Ld, V);

        unsigned Opc = SplatVT.isFloatingPoint() ? RISCVISD::VFMV_V_F_VL
                                                 : RISCVISD::VMV_V_X_VL;
        SDValue Splat =
            DAG.getNode(Opc, DL, SplatVT, DAG.getUNDEF(ContainerVT), V, VL);
        Splat = DAG.getBitcast(ContainerVT, Splat);
        return convertFromScalableVector(VT, Splat, DAG, Subtarget);
      }

      V1 = convertToScalableVector(ContainerVT, V1, DAG, Subtarget);
      assert(Lane < (int)NumElts && "Unexpected lane!");
      SDValue Gather = DAG.getNode(RISCVISD::VRGATHER_VX_VL, DL, ContainerVT,
                                   V1, DAG.getConstant(Lane, DL, XLenVT),
                                   DAG.getUNDEF(ContainerVT), TrueMask, VL);
      return convertFromScalableVector(VT, Gather, DAG, Subtarget);
    }
  }

  // For exact VLEN m2 or greater, try to split to m1 operations if we
  // can split cleanly.
  if (SDValue V = lowerShuffleViaVRegSplitting(SVN, DAG, Subtarget))
    return V;

  ArrayRef<int> Mask = SVN->getMask();

  if (SDValue V =
          lowerVECTOR_SHUFFLEAsVSlide1(DL, VT, V1, V2, Mask, Subtarget, DAG))
    return V;

  if (SDValue V =
          lowerVECTOR_SHUFFLEAsVSlidedown(DL, VT, V1, V2, Mask, Subtarget, DAG))
    return V;

  // A bitrotate will be one instruction on Zvkb, so try to lower to it first if
  // available.
  if (Subtarget.hasStdExtZvkb())
    if (SDValue V = lowerVECTOR_SHUFFLEAsRotate(SVN, DAG, Subtarget))
      return V;

  // Lower rotations to a SLIDEDOWN and a SLIDEUP. One of the source vectors may
  // be undef which can be handled with a single SLIDEDOWN/UP.
  int LoSrc, HiSrc;
  int Rotation = isElementRotate(LoSrc, HiSrc, Mask);
  if (Rotation > 0) {
    SDValue LoV, HiV;
    if (LoSrc >= 0) {
      LoV = LoSrc == 0 ? V1 : V2;
      LoV = convertToScalableVector(ContainerVT, LoV, DAG, Subtarget);
    }
    if (HiSrc >= 0) {
      HiV = HiSrc == 0 ? V1 : V2;
      HiV = convertToScalableVector(ContainerVT, HiV, DAG, Subtarget);
    }

    // We found a rotation. We need to slide HiV down by Rotation. Then we need
    // to slide LoV up by (NumElts - Rotation).
    unsigned InvRotate = NumElts - Rotation;

    SDValue Res = DAG.getUNDEF(ContainerVT);
    if (HiV) {
      // Even though we could use a smaller VL, don't to avoid a vsetivli
      // toggle.
      Res = getVSlidedown(DAG, Subtarget, DL, ContainerVT, Res, HiV,
                          DAG.getConstant(Rotation, DL, XLenVT), TrueMask, VL);
    }
    if (LoV)
      Res = getVSlideup(DAG, Subtarget, DL, ContainerVT, Res, LoV,
                        DAG.getConstant(InvRotate, DL, XLenVT), TrueMask, VL,
                        RISCVII::TAIL_AGNOSTIC);

    return convertFromScalableVector(VT, Res, DAG, Subtarget);
  }

  // If this is a deinterleave and we can widen the vector, then we can use
  // vnsrl to deinterleave.
  if (isDeinterleaveShuffle(VT, ContainerVT, V1, V2, Mask, Subtarget)) {
    return getDeinterleaveViaVNSRL(DL, VT, V1.getOperand(0), Mask[0] == 0,
                                   Subtarget, DAG);
  }

  if (SDValue V =
          lowerVECTOR_SHUFFLEAsVSlideup(DL, VT, V1, V2, Mask, Subtarget, DAG))
    return V;

  // Detect an interleave shuffle and lower to
  // (vmaccu.vx (vwaddu.vx lohalf(V1), lohalf(V2)), lohalf(V2), (2^eltbits - 1))
  int EvenSrc, OddSrc;
  if (isInterleaveShuffle(Mask, VT, EvenSrc, OddSrc, Subtarget)) {
    // Extract the halves of the vectors.
    MVT HalfVT = VT.getHalfNumVectorElementsVT();

    int Size = Mask.size();
    SDValue EvenV, OddV;
    assert(EvenSrc >= 0 && "Undef source?");
    EvenV = (EvenSrc / Size) == 0 ? V1 : V2;
    EvenV = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, HalfVT, EvenV,
                        DAG.getVectorIdxConstant(EvenSrc % Size, DL));

    assert(OddSrc >= 0 && "Undef source?");
    OddV = (OddSrc / Size) == 0 ? V1 : V2;
    OddV = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, HalfVT, OddV,
                       DAG.getVectorIdxConstant(OddSrc % Size, DL));

    return getWideningInterleave(EvenV, OddV, DL, DAG, Subtarget);
  }


  // Handle any remaining single source shuffles
  assert(!V1.isUndef() && "Unexpected shuffle canonicalization");
  if (V2.isUndef()) {
    // We might be able to express the shuffle as a bitrotate. But even if we
    // don't have Zvkb and have to expand, the expanded sequence of approx. 2
    // shifts and a vor will have a higher throughput than a vrgather.
    if (SDValue V = lowerVECTOR_SHUFFLEAsRotate(SVN, DAG, Subtarget))
      return V;

    if (VT.getScalarSizeInBits() == 8 &&
        any_of(Mask, [&](const auto &Idx) { return Idx > 255; })) {
      // On such a vector we're unable to use i8 as the index type.
      // FIXME: We could promote the index to i16 and use vrgatherei16, but that
      // may involve vector splitting if we're already at LMUL=8, or our
      // user-supplied maximum fixed-length LMUL.
      return SDValue();
    }

    // Base case for the two operand recursion below - handle the worst case
    // single source shuffle.
    unsigned GatherVVOpc = RISCVISD::VRGATHER_VV_VL;
    MVT IndexVT = VT.changeTypeToInteger();
    // Since we can't introduce illegal index types at this stage, use i16 and
    // vrgatherei16 if the corresponding index type for plain vrgather is greater
    // than XLenVT.
    if (IndexVT.getScalarType().bitsGT(XLenVT)) {
      GatherVVOpc = RISCVISD::VRGATHEREI16_VV_VL;
      IndexVT = IndexVT.changeVectorElementType(MVT::i16);
    }

    // If the mask allows, we can do all the index computation in 16 bits.  This
    // requires less work and less register pressure at high LMUL, and creates
    // smaller constants which may be cheaper to materialize.
    if (IndexVT.getScalarType().bitsGT(MVT::i16) && isUInt<16>(NumElts - 1) &&
        (IndexVT.getSizeInBits() / Subtarget.getRealMinVLen()) > 1) {
      GatherVVOpc = RISCVISD::VRGATHEREI16_VV_VL;
      IndexVT = IndexVT.changeVectorElementType(MVT::i16);
    }

    MVT IndexContainerVT =
      ContainerVT.changeVectorElementType(IndexVT.getScalarType());

    V1 = convertToScalableVector(ContainerVT, V1, DAG, Subtarget);
    SmallVector<SDValue> GatherIndicesLHS;
    for (int MaskIndex : Mask) {
      bool IsLHSIndex = MaskIndex < (int)NumElts && MaskIndex >= 0;
      GatherIndicesLHS.push_back(IsLHSIndex
                                 ? DAG.getConstant(MaskIndex, DL, XLenVT)
                                 : DAG.getUNDEF(XLenVT));
    }
    SDValue LHSIndices = DAG.getBuildVector(IndexVT, DL, GatherIndicesLHS);
    LHSIndices = convertToScalableVector(IndexContainerVT, LHSIndices, DAG,
                                         Subtarget);
    SDValue Gather = DAG.getNode(GatherVVOpc, DL, ContainerVT, V1, LHSIndices,
                                 DAG.getUNDEF(ContainerVT), TrueMask, VL);
    return convertFromScalableVector(VT, Gather, DAG, Subtarget);
  }

  // As a backup, shuffles can be lowered via a vrgather instruction, possibly
  // merged with a second vrgather.
  SmallVector<int> ShuffleMaskLHS, ShuffleMaskRHS;

  // Now construct the mask that will be used by the blended vrgather operation.
  // Construct the appropriate indices into each vector.
  for (int MaskIndex : Mask) {
    bool IsLHSOrUndefIndex = MaskIndex < (int)NumElts;
    ShuffleMaskLHS.push_back(IsLHSOrUndefIndex && MaskIndex >= 0
                             ? MaskIndex : -1);
    ShuffleMaskRHS.push_back(IsLHSOrUndefIndex ? -1 : (MaskIndex - NumElts));
  }

  // Try to pick a profitable operand order.
  bool SwapOps = DAG.isSplatValue(V2) && !DAG.isSplatValue(V1);
  SwapOps = SwapOps ^ ShuffleVectorInst::isIdentityMask(ShuffleMaskRHS, NumElts);

  // Recursively invoke lowering for each operand if we had two
  // independent single source shuffles, and then combine the result via a
  // vselect.  Note that the vselect will likely be folded back into the
  // second permute (vrgather, or other) by the post-isel combine.
  V1 = DAG.getVectorShuffle(VT, DL, V1, DAG.getUNDEF(VT), ShuffleMaskLHS);
  V2 = DAG.getVectorShuffle(VT, DL, V2, DAG.getUNDEF(VT), ShuffleMaskRHS);

  SmallVector<SDValue> MaskVals;
  for (int MaskIndex : Mask) {
    bool SelectMaskVal = (MaskIndex < (int)NumElts) ^ !SwapOps;
    MaskVals.push_back(DAG.getConstant(SelectMaskVal, DL, XLenVT));
  }

  assert(MaskVals.size() == NumElts && "Unexpected select-like shuffle");
  MVT MaskVT = MVT::getVectorVT(MVT::i1, NumElts);
  SDValue SelectMask = DAG.getBuildVector(MaskVT, DL, MaskVals);

  if (SwapOps)
    return DAG.getNode(ISD::VSELECT, DL, VT, SelectMask, V1, V2);
  return DAG.getNode(ISD::VSELECT, DL, VT, SelectMask, V2, V1);
}

bool RISCVTargetLowering::isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const {
  // Support splats for any type. These should type legalize well.
  if (ShuffleVectorSDNode::isSplatMask(M.data(), VT))
    return true;

  // Only support legal VTs for other shuffles for now.
  if (!isTypeLegal(VT))
    return false;

  MVT SVT = VT.getSimpleVT();

  // Not for i1 vectors.
  if (SVT.getScalarType() == MVT::i1)
    return false;

  int Dummy1, Dummy2;
  return (isElementRotate(Dummy1, Dummy2, M) > 0) ||
         isInterleaveShuffle(M, SVT, Dummy1, Dummy2, Subtarget);
}

// Lower CTLZ_ZERO_UNDEF or CTTZ_ZERO_UNDEF by converting to FP and extracting
// the exponent.
SDValue
RISCVTargetLowering::lowerCTLZ_CTTZ_ZERO_UNDEF(SDValue Op,
                                               SelectionDAG &DAG) const {
  MVT VT = Op.getSimpleValueType();
  unsigned EltSize = VT.getScalarSizeInBits();
  SDValue Src = Op.getOperand(0);
  SDLoc DL(Op);
  MVT ContainerVT = VT;

  SDValue Mask, VL;
  if (Op->isVPOpcode()) {
    Mask = Op.getOperand(1);
    if (VT.isFixedLengthVector())
      Mask = convertToScalableVector(getMaskTypeFor(ContainerVT), Mask, DAG,
                                     Subtarget);
    VL = Op.getOperand(2);
  }

  // We choose FP type that can represent the value if possible. Otherwise, we
  // use rounding to zero conversion for correct exponent of the result.
  // TODO: Use f16 for i8 when possible?
  MVT FloatEltVT = (EltSize >= 32) ? MVT::f64 : MVT::f32;
  if (!isTypeLegal(MVT::getVectorVT(FloatEltVT, VT.getVectorElementCount())))
    FloatEltVT = MVT::f32;
  MVT FloatVT = MVT::getVectorVT(FloatEltVT, VT.getVectorElementCount());

  // Legal types should have been checked in the RISCVTargetLowering
  // constructor.
  // TODO: Splitting may make sense in some cases.
  assert(DAG.getTargetLoweringInfo().isTypeLegal(FloatVT) &&
         "Expected legal float type!");

  // For CTTZ_ZERO_UNDEF, we need to extract the lowest set bit using X & -X.
  // The trailing zero count is equal to log2 of this single bit value.
  if (Op.getOpcode() == ISD::CTTZ_ZERO_UNDEF) {
    SDValue Neg = DAG.getNegative(Src, DL, VT);
    Src = DAG.getNode(ISD::AND, DL, VT, Src, Neg);
  } else if (Op.getOpcode() == ISD::VP_CTTZ_ZERO_UNDEF) {
    SDValue Neg = DAG.getNode(ISD::VP_SUB, DL, VT, DAG.getConstant(0, DL, VT),
                              Src, Mask, VL);
    Src = DAG.getNode(ISD::VP_AND, DL, VT, Src, Neg, Mask, VL);
  }

  // We have a legal FP type, convert to it.
  SDValue FloatVal;
  if (FloatVT.bitsGT(VT)) {
    if (Op->isVPOpcode())
      FloatVal = DAG.getNode(ISD::VP_UINT_TO_FP, DL, FloatVT, Src, Mask, VL);
    else
      FloatVal = DAG.getNode(ISD::UINT_TO_FP, DL, FloatVT, Src);
  } else {
    // Use RTZ to avoid rounding influencing exponent of FloatVal.
    if (VT.isFixedLengthVector()) {
      ContainerVT = getContainerForFixedLengthVector(VT);
      Src = convertToScalableVector(ContainerVT, Src, DAG, Subtarget);
    }
    if (!Op->isVPOpcode())
      std::tie(Mask, VL) = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);
    SDValue RTZRM =
        DAG.getTargetConstant(RISCVFPRndMode::RTZ, DL, Subtarget.getXLenVT());
    MVT ContainerFloatVT =
        MVT::getVectorVT(FloatEltVT, ContainerVT.getVectorElementCount());
    FloatVal = DAG.getNode(RISCVISD::VFCVT_RM_F_XU_VL, DL, ContainerFloatVT,
                           Src, Mask, RTZRM, VL);
    if (VT.isFixedLengthVector())
      FloatVal = convertFromScalableVector(FloatVT, FloatVal, DAG, Subtarget);
  }
  // Bitcast to integer and shift the exponent to the LSB.
  EVT IntVT = FloatVT.changeVectorElementTypeToInteger();
  SDValue Bitcast = DAG.getBitcast(IntVT, FloatVal);
  unsigned ShiftAmt = FloatEltVT == MVT::f64 ? 52 : 23;

  SDValue Exp;
  // Restore back to original type. Truncation after SRL is to generate vnsrl.
  if (Op->isVPOpcode()) {
    Exp = DAG.getNode(ISD::VP_SRL, DL, IntVT, Bitcast,
                      DAG.getConstant(ShiftAmt, DL, IntVT), Mask, VL);
    Exp = DAG.getVPZExtOrTrunc(DL, VT, Exp, Mask, VL);
  } else {
    Exp = DAG.getNode(ISD::SRL, DL, IntVT, Bitcast,
                      DAG.getConstant(ShiftAmt, DL, IntVT));
    if (IntVT.bitsLT(VT))
      Exp = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Exp);
    else if (IntVT.bitsGT(VT))
      Exp = DAG.getNode(ISD::TRUNCATE, DL, VT, Exp);
  }

  // The exponent contains log2 of the value in biased form.
  unsigned ExponentBias = FloatEltVT == MVT::f64 ? 1023 : 127;
  // For trailing zeros, we just need to subtract the bias.
  if (Op.getOpcode() == ISD::CTTZ_ZERO_UNDEF)
    return DAG.getNode(ISD::SUB, DL, VT, Exp,
                       DAG.getConstant(ExponentBias, DL, VT));
  if (Op.getOpcode() == ISD::VP_CTTZ_ZERO_UNDEF)
    return DAG.getNode(ISD::VP_SUB, DL, VT, Exp,
                       DAG.getConstant(ExponentBias, DL, VT), Mask, VL);

  // For leading zeros, we need to remove the bias and convert from log2 to
  // leading zeros. We can do this by subtracting from (Bias + (EltSize - 1)).
  unsigned Adjust = ExponentBias + (EltSize - 1);
  SDValue Res;
  if (Op->isVPOpcode())
    Res = DAG.getNode(ISD::VP_SUB, DL, VT, DAG.getConstant(Adjust, DL, VT), Exp,
                      Mask, VL);
  else
    Res = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(Adjust, DL, VT), Exp);

  // The above result with zero input equals to Adjust which is greater than
  // EltSize. Hence, we can do min(Res, EltSize) for CTLZ.
  if (Op.getOpcode() == ISD::CTLZ)
    Res = DAG.getNode(ISD::UMIN, DL, VT, Res, DAG.getConstant(EltSize, DL, VT));
  else if (Op.getOpcode() == ISD::VP_CTLZ)
    Res = DAG.getNode(ISD::VP_UMIN, DL, VT, Res,
                      DAG.getConstant(EltSize, DL, VT), Mask, VL);
  return Res;
}

SDValue RISCVTargetLowering::lowerVPCttzElements(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();
  SDValue Source = Op->getOperand(0);
  MVT SrcVT = Source.getSimpleValueType();
  SDValue Mask = Op->getOperand(1);
  SDValue EVL = Op->getOperand(2);

  if (SrcVT.isFixedLengthVector()) {
    MVT ContainerVT = getContainerForFixedLengthVector(SrcVT);
    Source = convertToScalableVector(ContainerVT, Source, DAG, Subtarget);
    Mask = convertToScalableVector(getMaskTypeFor(ContainerVT), Mask, DAG,
                                   Subtarget);
    SrcVT = ContainerVT;
  }

  // Convert to boolean vector.
  if (SrcVT.getScalarType() != MVT::i1) {
    SDValue AllZero = DAG.getConstant(0, DL, SrcVT);
    SrcVT = MVT::getVectorVT(MVT::i1, SrcVT.getVectorElementCount());
    Source = DAG.getNode(RISCVISD::SETCC_VL, DL, SrcVT,
                         {Source, AllZero, DAG.getCondCode(ISD::SETNE),
                          DAG.getUNDEF(SrcVT), Mask, EVL});
  }

  SDValue Res = DAG.getNode(RISCVISD::VFIRST_VL, DL, XLenVT, Source, Mask, EVL);
  if (Op->getOpcode() == ISD::VP_CTTZ_ELTS_ZERO_UNDEF)
    // In this case, we can interpret poison as -1, so nothing to do further.
    return Res;

  // Convert -1 to VL.
  SDValue SetCC =
      DAG.getSetCC(DL, XLenVT, Res, DAG.getConstant(0, DL, XLenVT), ISD::SETLT);
  Res = DAG.getSelect(DL, XLenVT, SetCC, EVL, Res);
  return DAG.getNode(ISD::TRUNCATE, DL, Op.getValueType(), Res);
}

// While RVV has alignment restrictions, we should always be able to load as a
// legal equivalently-sized byte-typed vector instead. This method is
// responsible for re-expressing a ISD::LOAD via a correctly-aligned type. If
// the load is already correctly-aligned, it returns SDValue().
SDValue RISCVTargetLowering::expandUnalignedRVVLoad(SDValue Op,
                                                    SelectionDAG &DAG) const {
  auto *Load = cast<LoadSDNode>(Op);
  assert(Load && Load->getMemoryVT().isVector() && "Expected vector load");

  if (allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                     Load->getMemoryVT(),
                                     *Load->getMemOperand()))
    return SDValue();

  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  unsigned EltSizeBits = VT.getScalarSizeInBits();
  assert((EltSizeBits == 16 || EltSizeBits == 32 || EltSizeBits == 64) &&
         "Unexpected unaligned RVV load type");
  MVT NewVT =
      MVT::getVectorVT(MVT::i8, VT.getVectorElementCount() * (EltSizeBits / 8));
  assert(NewVT.isValid() &&
         "Expecting equally-sized RVV vector types to be legal");
  SDValue L = DAG.getLoad(NewVT, DL, Load->getChain(), Load->getBasePtr(),
                          Load->getPointerInfo(), Load->getOriginalAlign(),
                          Load->getMemOperand()->getFlags());
  return DAG.getMergeValues({DAG.getBitcast(VT, L), L.getValue(1)}, DL);
}

// While RVV has alignment restrictions, we should always be able to store as a
// legal equivalently-sized byte-typed vector instead. This method is
// responsible for re-expressing a ISD::STORE via a correctly-aligned type. It
// returns SDValue() if the store is already correctly aligned.
SDValue RISCVTargetLowering::expandUnalignedRVVStore(SDValue Op,
                                                     SelectionDAG &DAG) const {
  auto *Store = cast<StoreSDNode>(Op);
  assert(Store && Store->getValue().getValueType().isVector() &&
         "Expected vector store");

  if (allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                     Store->getMemoryVT(),
                                     *Store->getMemOperand()))
    return SDValue();

  SDLoc DL(Op);
  SDValue StoredVal = Store->getValue();
  MVT VT = StoredVal.getSimpleValueType();
  unsigned EltSizeBits = VT.getScalarSizeInBits();
  assert((EltSizeBits == 16 || EltSizeBits == 32 || EltSizeBits == 64) &&
         "Unexpected unaligned RVV store type");
  MVT NewVT =
      MVT::getVectorVT(MVT::i8, VT.getVectorElementCount() * (EltSizeBits / 8));
  assert(NewVT.isValid() &&
         "Expecting equally-sized RVV vector types to be legal");
  StoredVal = DAG.getBitcast(NewVT, StoredVal);
  return DAG.getStore(Store->getChain(), DL, StoredVal, Store->getBasePtr(),
                      Store->getPointerInfo(), Store->getOriginalAlign(),
                      Store->getMemOperand()->getFlags());
}

static SDValue lowerConstant(SDValue Op, SelectionDAG &DAG,
                             const RISCVSubtarget &Subtarget) {
  assert(Op.getValueType() == MVT::i64 && "Unexpected VT");

  int64_t Imm = cast<ConstantSDNode>(Op)->getSExtValue();

  // All simm32 constants should be handled by isel.
  // NOTE: The getMaxBuildIntsCost call below should return a value >= 2 making
  // this check redundant, but small immediates are common so this check
  // should have better compile time.
  if (isInt<32>(Imm))
    return Op;

  // We only need to cost the immediate, if constant pool lowering is enabled.
  if (!Subtarget.useConstantPoolForLargeInts())
    return Op;

  RISCVMatInt::InstSeq Seq = RISCVMatInt::generateInstSeq(Imm, Subtarget);
  if (Seq.size() <= Subtarget.getMaxBuildIntsCost())
    return Op;

  // Optimizations below are disabled for opt size. If we're optimizing for
  // size, use a constant pool.
  if (DAG.shouldOptForSize())
    return SDValue();

  // Special case. See if we can build the constant as (ADD (SLLI X, C), X) do
  // that if it will avoid a constant pool.
  // It will require an extra temporary register though.
  // If we have Zba we can use (ADD_UW X, (SLLI X, 32)) to handle cases where
  // low and high 32 bits are the same and bit 31 and 63 are set.
  unsigned ShiftAmt, AddOpc;
  RISCVMatInt::InstSeq SeqLo =
      RISCVMatInt::generateTwoRegInstSeq(Imm, Subtarget, ShiftAmt, AddOpc);
  if (!SeqLo.empty() && (SeqLo.size() + 2) <= Subtarget.getMaxBuildIntsCost())
    return Op;

  return SDValue();
}

static SDValue LowerATOMIC_FENCE(SDValue Op, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  SDLoc dl(Op);
  AtomicOrdering FenceOrdering =
      static_cast<AtomicOrdering>(Op.getConstantOperandVal(1));
  SyncScope::ID FenceSSID =
      static_cast<SyncScope::ID>(Op.getConstantOperandVal(2));

  if (Subtarget.hasStdExtZtso()) {
    // The only fence that needs an instruction is a sequentially-consistent
    // cross-thread fence.
    if (FenceOrdering == AtomicOrdering::SequentiallyConsistent &&
        FenceSSID == SyncScope::System)
      return Op;

    // MEMBARRIER is a compiler barrier; it codegens to a no-op.
    return DAG.getNode(ISD::MEMBARRIER, dl, MVT::Other, Op.getOperand(0));
  }

  // singlethread fences only synchronize with signal handlers on the same
  // thread and thus only need to preserve instruction order, not actually
  // enforce memory ordering.
  if (FenceSSID == SyncScope::SingleThread)
    // MEMBARRIER is a compiler barrier; it codegens to a no-op.
    return DAG.getNode(ISD::MEMBARRIER, dl, MVT::Other, Op.getOperand(0));

  return Op;
}

static SDValue lowerSADDSAT_SSUBSAT(SDValue Op, SelectionDAG &DAG) {
  assert(Op.getValueType() == MVT::i32 && RV64LegalI32 &&
         "Unexpected custom legalisation");

  // With Zbb, we can widen to i64 and smin/smax with INT32_MAX/MIN.
  bool IsAdd = Op.getOpcode() == ISD::SADDSAT;
  SDLoc DL(Op);
  SDValue LHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(0));
  SDValue RHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(1));
  SDValue Result =
      DAG.getNode(IsAdd ? ISD::ADD : ISD::SUB, DL, MVT::i64, LHS, RHS);

  APInt MinVal = APInt::getSignedMinValue(32).sext(64);
  APInt MaxVal = APInt::getSignedMaxValue(32).sext(64);
  SDValue SatMin = DAG.getConstant(MinVal, DL, MVT::i64);
  SDValue SatMax = DAG.getConstant(MaxVal, DL, MVT::i64);
  Result = DAG.getNode(ISD::SMIN, DL, MVT::i64, Result, SatMax);
  Result = DAG.getNode(ISD::SMAX, DL, MVT::i64, Result, SatMin);
  return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Result);
}

static SDValue lowerUADDSAT_USUBSAT(SDValue Op, SelectionDAG &DAG) {
  assert(Op.getValueType() == MVT::i32 && RV64LegalI32 &&
         "Unexpected custom legalisation");

  // With Zbb we can sign extend and let LegalizeDAG use minu/maxu. Using
  // sign extend allows overflow of the lower 32 bits to be detected on
  // the promoted size.
  SDLoc DL(Op);
  SDValue LHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(0));
  SDValue RHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(1));
  SDValue WideOp = DAG.getNode(Op.getOpcode(), DL, MVT::i64, LHS, RHS);
  return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, WideOp);
}

// Custom lower i32 SADDO/SSUBO with RV64LegalI32 so we take advantage of addw.
static SDValue lowerSADDO_SSUBO(SDValue Op, SelectionDAG &DAG) {
  assert(Op.getValueType() == MVT::i32 && RV64LegalI32 &&
         "Unexpected custom legalisation");
  if (isa<ConstantSDNode>(Op.getOperand(1)))
    return SDValue();

  bool IsAdd = Op.getOpcode() == ISD::SADDO;
  SDLoc DL(Op);
  SDValue LHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(0));
  SDValue RHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(1));
  SDValue WideOp =
      DAG.getNode(IsAdd ? ISD::ADD : ISD::SUB, DL, MVT::i64, LHS, RHS);
  SDValue Res = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, WideOp);
  SDValue SExt = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, WideOp,
                             DAG.getValueType(MVT::i32));
  SDValue Ovf = DAG.getSetCC(DL, Op.getValue(1).getValueType(), WideOp, SExt,
                             ISD::SETNE);
  return DAG.getMergeValues({Res, Ovf}, DL);
}

// Custom lower i32 SMULO with RV64LegalI32 so we take advantage of mulw.
static SDValue lowerSMULO(SDValue Op, SelectionDAG &DAG) {
  assert(Op.getValueType() == MVT::i32 && RV64LegalI32 &&
         "Unexpected custom legalisation");
  SDLoc DL(Op);
  SDValue LHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(0));
  SDValue RHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op.getOperand(1));
  SDValue Mul = DAG.getNode(ISD::MUL, DL, MVT::i64, LHS, RHS);
  SDValue Res = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Mul);
  SDValue SExt = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, Mul,
                             DAG.getValueType(MVT::i32));
  SDValue Ovf = DAG.getSetCC(DL, Op.getValue(1).getValueType(), Mul, SExt,
                             ISD::SETNE);
  return DAG.getMergeValues({Res, Ovf}, DL);
}

SDValue RISCVTargetLowering::LowerIS_FPCLASS(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();
  unsigned Check = Op.getConstantOperandVal(1);
  unsigned TDCMask = 0;
  if (Check & fcSNan)
    TDCMask |= RISCV::FPMASK_Signaling_NaN;
  if (Check & fcQNan)
    TDCMask |= RISCV::FPMASK_Quiet_NaN;
  if (Check & fcPosInf)
    TDCMask |= RISCV::FPMASK_Positive_Infinity;
  if (Check & fcNegInf)
    TDCMask |= RISCV::FPMASK_Negative_Infinity;
  if (Check & fcPosNormal)
    TDCMask |= RISCV::FPMASK_Positive_Normal;
  if (Check & fcNegNormal)
    TDCMask |= RISCV::FPMASK_Negative_Normal;
  if (Check & fcPosSubnormal)
    TDCMask |= RISCV::FPMASK_Positive_Subnormal;
  if (Check & fcNegSubnormal)
    TDCMask |= RISCV::FPMASK_Negative_Subnormal;
  if (Check & fcPosZero)
    TDCMask |= RISCV::FPMASK_Positive_Zero;
  if (Check & fcNegZero)
    TDCMask |= RISCV::FPMASK_Negative_Zero;

  bool IsOneBitMask = isPowerOf2_32(TDCMask);

  SDValue TDCMaskV = DAG.getConstant(TDCMask, DL, XLenVT);

  if (VT.isVector()) {
    SDValue Op0 = Op.getOperand(0);
    MVT VT0 = Op.getOperand(0).getSimpleValueType();

    if (VT.isScalableVector()) {
      MVT DstVT = VT0.changeVectorElementTypeToInteger();
      auto [Mask, VL] = getDefaultScalableVLOps(VT0, DL, DAG, Subtarget);
      if (Op.getOpcode() == ISD::VP_IS_FPCLASS) {
        Mask = Op.getOperand(2);
        VL = Op.getOperand(3);
      }
      SDValue FPCLASS = DAG.getNode(RISCVISD::FCLASS_VL, DL, DstVT, Op0, Mask,
                                    VL, Op->getFlags());
      if (IsOneBitMask)
        return DAG.getSetCC(DL, VT, FPCLASS,
                            DAG.getConstant(TDCMask, DL, DstVT),
                            ISD::CondCode::SETEQ);
      SDValue AND = DAG.getNode(ISD::AND, DL, DstVT, FPCLASS,
                                DAG.getConstant(TDCMask, DL, DstVT));
      return DAG.getSetCC(DL, VT, AND, DAG.getConstant(0, DL, DstVT),
                          ISD::SETNE);
    }

    MVT ContainerVT0 = getContainerForFixedLengthVector(VT0);
    MVT ContainerVT = getContainerForFixedLengthVector(VT);
    MVT ContainerDstVT = ContainerVT0.changeVectorElementTypeToInteger();
    auto [Mask, VL] = getDefaultVLOps(VT0, ContainerVT0, DL, DAG, Subtarget);
    if (Op.getOpcode() == ISD::VP_IS_FPCLASS) {
      Mask = Op.getOperand(2);
      MVT MaskContainerVT =
          getContainerForFixedLengthVector(Mask.getSimpleValueType());
      Mask = convertToScalableVector(MaskContainerVT, Mask, DAG, Subtarget);
      VL = Op.getOperand(3);
    }
    Op0 = convertToScalableVector(ContainerVT0, Op0, DAG, Subtarget);

    SDValue FPCLASS = DAG.getNode(RISCVISD::FCLASS_VL, DL, ContainerDstVT, Op0,
                                  Mask, VL, Op->getFlags());

    TDCMaskV = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerDstVT,
                           DAG.getUNDEF(ContainerDstVT), TDCMaskV, VL);
    if (IsOneBitMask) {
      SDValue VMSEQ =
          DAG.getNode(RISCVISD::SETCC_VL, DL, ContainerVT,
                      {FPCLASS, TDCMaskV, DAG.getCondCode(ISD::SETEQ),
                       DAG.getUNDEF(ContainerVT), Mask, VL});
      return convertFromScalableVector(VT, VMSEQ, DAG, Subtarget);
    }
    SDValue AND = DAG.getNode(RISCVISD::AND_VL, DL, ContainerDstVT, FPCLASS,
                              TDCMaskV, DAG.getUNDEF(ContainerDstVT), Mask, VL);

    SDValue SplatZero = DAG.getConstant(0, DL, XLenVT);
    SplatZero = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerDstVT,
                            DAG.getUNDEF(ContainerDstVT), SplatZero, VL);

    SDValue VMSNE = DAG.getNode(RISCVISD::SETCC_VL, DL, ContainerVT,
                                {AND, SplatZero, DAG.getCondCode(ISD::SETNE),
                                 DAG.getUNDEF(ContainerVT), Mask, VL});
    return convertFromScalableVector(VT, VMSNE, DAG, Subtarget);
  }

  SDValue FCLASS = DAG.getNode(RISCVISD::FCLASS, DL, XLenVT, Op.getOperand(0));
  SDValue AND = DAG.getNode(ISD::AND, DL, XLenVT, FCLASS, TDCMaskV);
  SDValue Res = DAG.getSetCC(DL, XLenVT, AND, DAG.getConstant(0, DL, XLenVT),
                             ISD::CondCode::SETNE);
  return DAG.getNode(ISD::TRUNCATE, DL, VT, Res);
}

// Lower fmaximum and fminimum. Unlike our fmax and fmin instructions, these
// operations propagate nans.
static SDValue lowerFMAXIMUM_FMINIMUM(SDValue Op, SelectionDAG &DAG,
                                      const RISCVSubtarget &Subtarget) {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();

  SDValue X = Op.getOperand(0);
  SDValue Y = Op.getOperand(1);

  if (!VT.isVector()) {
    MVT XLenVT = Subtarget.getXLenVT();

    // If X is a nan, replace Y with X. If Y is a nan, replace X with Y. This
    // ensures that when one input is a nan, the other will also be a nan
    // allowing the nan to propagate. If both inputs are nan, this will swap the
    // inputs which is harmless.

    SDValue NewY = Y;
    if (!Op->getFlags().hasNoNaNs() && !DAG.isKnownNeverNaN(X)) {
      SDValue XIsNonNan = DAG.getSetCC(DL, XLenVT, X, X, ISD::SETOEQ);
      NewY = DAG.getSelect(DL, VT, XIsNonNan, Y, X);
    }

    SDValue NewX = X;
    if (!Op->getFlags().hasNoNaNs() && !DAG.isKnownNeverNaN(Y)) {
      SDValue YIsNonNan = DAG.getSetCC(DL, XLenVT, Y, Y, ISD::SETOEQ);
      NewX = DAG.getSelect(DL, VT, YIsNonNan, X, Y);
    }

    unsigned Opc =
        Op.getOpcode() == ISD::FMAXIMUM ? RISCVISD::FMAX : RISCVISD::FMIN;
    return DAG.getNode(Opc, DL, VT, NewX, NewY);
  }

  // Check no NaNs before converting to fixed vector scalable.
  bool XIsNeverNan = Op->getFlags().hasNoNaNs() || DAG.isKnownNeverNaN(X);
  bool YIsNeverNan = Op->getFlags().hasNoNaNs() || DAG.isKnownNeverNaN(Y);

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(DAG, VT, Subtarget);
    X = convertToScalableVector(ContainerVT, X, DAG, Subtarget);
    Y = convertToScalableVector(ContainerVT, Y, DAG, Subtarget);
  }

  SDValue Mask, VL;
  if (Op->isVPOpcode()) {
    Mask = Op.getOperand(2);
    if (VT.isFixedLengthVector())
      Mask = convertToScalableVector(getMaskTypeFor(ContainerVT), Mask, DAG,
                                     Subtarget);
    VL = Op.getOperand(3);
  } else {
    std::tie(Mask, VL) = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);
  }

  SDValue NewY = Y;
  if (!XIsNeverNan) {
    SDValue XIsNonNan = DAG.getNode(RISCVISD::SETCC_VL, DL, Mask.getValueType(),
                                    {X, X, DAG.getCondCode(ISD::SETOEQ),
                                     DAG.getUNDEF(ContainerVT), Mask, VL});
    NewY = DAG.getNode(RISCVISD::VMERGE_VL, DL, ContainerVT, XIsNonNan, Y, X,
                       DAG.getUNDEF(ContainerVT), VL);
  }

  SDValue NewX = X;
  if (!YIsNeverNan) {
    SDValue YIsNonNan = DAG.getNode(RISCVISD::SETCC_VL, DL, Mask.getValueType(),
                                    {Y, Y, DAG.getCondCode(ISD::SETOEQ),
                                     DAG.getUNDEF(ContainerVT), Mask, VL});
    NewX = DAG.getNode(RISCVISD::VMERGE_VL, DL, ContainerVT, YIsNonNan, X, Y,
                       DAG.getUNDEF(ContainerVT), VL);
  }

  unsigned Opc =
      Op.getOpcode() == ISD::FMAXIMUM || Op->getOpcode() == ISD::VP_FMAXIMUM
          ? RISCVISD::VFMAX_VL
          : RISCVISD::VFMIN_VL;
  SDValue Res = DAG.getNode(Opc, DL, ContainerVT, NewX, NewY,
                            DAG.getUNDEF(ContainerVT), Mask, VL);
  if (VT.isFixedLengthVector())
    Res = convertFromScalableVector(VT, Res, DAG, Subtarget);
  return Res;
}

/// Get a RISC-V target specified VL op for a given SDNode.
static unsigned getRISCVVLOp(SDValue Op) {
#define OP_CASE(NODE)                                                          \
  case ISD::NODE:                                                              \
    return RISCVISD::NODE##_VL;
#define VP_CASE(NODE)                                                          \
  case ISD::VP_##NODE:                                                         \
    return RISCVISD::NODE##_VL;
  // clang-format off
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("don't have RISC-V specified VL op for this SDNode");
  OP_CASE(ADD)
  OP_CASE(SUB)
  OP_CASE(MUL)
  OP_CASE(MULHS)
  OP_CASE(MULHU)
  OP_CASE(SDIV)
  OP_CASE(SREM)
  OP_CASE(UDIV)
  OP_CASE(UREM)
  OP_CASE(SHL)
  OP_CASE(SRA)
  OP_CASE(SRL)
  OP_CASE(ROTL)
  OP_CASE(ROTR)
  OP_CASE(BSWAP)
  OP_CASE(CTTZ)
  OP_CASE(CTLZ)
  OP_CASE(CTPOP)
  OP_CASE(BITREVERSE)
  OP_CASE(SADDSAT)
  OP_CASE(UADDSAT)
  OP_CASE(SSUBSAT)
  OP_CASE(USUBSAT)
  OP_CASE(AVGFLOORS)
  OP_CASE(AVGFLOORU)
  OP_CASE(AVGCEILS)
  OP_CASE(AVGCEILU)
  OP_CASE(FADD)
  OP_CASE(FSUB)
  OP_CASE(FMUL)
  OP_CASE(FDIV)
  OP_CASE(FNEG)
  OP_CASE(FABS)
  OP_CASE(FSQRT)
  OP_CASE(SMIN)
  OP_CASE(SMAX)
  OP_CASE(UMIN)
  OP_CASE(UMAX)
  OP_CASE(STRICT_FADD)
  OP_CASE(STRICT_FSUB)
  OP_CASE(STRICT_FMUL)
  OP_CASE(STRICT_FDIV)
  OP_CASE(STRICT_FSQRT)
  VP_CASE(ADD)        // VP_ADD
  VP_CASE(SUB)        // VP_SUB
  VP_CASE(MUL)        // VP_MUL
  VP_CASE(SDIV)       // VP_SDIV
  VP_CASE(SREM)       // VP_SREM
  VP_CASE(UDIV)       // VP_UDIV
  VP_CASE(UREM)       // VP_UREM
  VP_CASE(SHL)        // VP_SHL
  VP_CASE(FADD)       // VP_FADD
  VP_CASE(FSUB)       // VP_FSUB
  VP_CASE(FMUL)       // VP_FMUL
  VP_CASE(FDIV)       // VP_FDIV
  VP_CASE(FNEG)       // VP_FNEG
  VP_CASE(FABS)       // VP_FABS
  VP_CASE(SMIN)       // VP_SMIN
  VP_CASE(SMAX)       // VP_SMAX
  VP_CASE(UMIN)       // VP_UMIN
  VP_CASE(UMAX)       // VP_UMAX
  VP_CASE(FCOPYSIGN)  // VP_FCOPYSIGN
  VP_CASE(SETCC)      // VP_SETCC
  VP_CASE(SINT_TO_FP) // VP_SINT_TO_FP
  VP_CASE(UINT_TO_FP) // VP_UINT_TO_FP
  VP_CASE(BITREVERSE) // VP_BITREVERSE
  VP_CASE(SADDSAT)    // VP_SADDSAT
  VP_CASE(UADDSAT)    // VP_UADDSAT
  VP_CASE(SSUBSAT)    // VP_SSUBSAT
  VP_CASE(USUBSAT)    // VP_USUBSAT
  VP_CASE(BSWAP)      // VP_BSWAP
  VP_CASE(CTLZ)       // VP_CTLZ
  VP_CASE(CTTZ)       // VP_CTTZ
  VP_CASE(CTPOP)      // VP_CTPOP
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::VP_CTLZ_ZERO_UNDEF:
    return RISCVISD::CTLZ_VL;
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::VP_CTTZ_ZERO_UNDEF:
    return RISCVISD::CTTZ_VL;
  case ISD::FMA:
  case ISD::VP_FMA:
    return RISCVISD::VFMADD_VL;
  case ISD::STRICT_FMA:
    return RISCVISD::STRICT_VFMADD_VL;
  case ISD::AND:
  case ISD::VP_AND:
    if (Op.getSimpleValueType().getVectorElementType() == MVT::i1)
      return RISCVISD::VMAND_VL;
    return RISCVISD::AND_VL;
  case ISD::OR:
  case ISD::VP_OR:
    if (Op.getSimpleValueType().getVectorElementType() == MVT::i1)
      return RISCVISD::VMOR_VL;
    return RISCVISD::OR_VL;
  case ISD::XOR:
  case ISD::VP_XOR:
    if (Op.getSimpleValueType().getVectorElementType() == MVT::i1)
      return RISCVISD::VMXOR_VL;
    return RISCVISD::XOR_VL;
  case ISD::VP_SELECT:
  case ISD::VP_MERGE:
    return RISCVISD::VMERGE_VL;
  case ISD::VP_SRA:
    return RISCVISD::SRA_VL;
  case ISD::VP_SRL:
    return RISCVISD::SRL_VL;
  case ISD::VP_SQRT:
    return RISCVISD::FSQRT_VL;
  case ISD::VP_SIGN_EXTEND:
    return RISCVISD::VSEXT_VL;
  case ISD::VP_ZERO_EXTEND:
    return RISCVISD::VZEXT_VL;
  case ISD::VP_FP_TO_SINT:
    return RISCVISD::VFCVT_RTZ_X_F_VL;
  case ISD::VP_FP_TO_UINT:
    return RISCVISD::VFCVT_RTZ_XU_F_VL;
  case ISD::FMINNUM:
  case ISD::VP_FMINNUM:
    return RISCVISD::VFMIN_VL;
  case ISD::FMAXNUM:
  case ISD::VP_FMAXNUM:
    return RISCVISD::VFMAX_VL;
  case ISD::LRINT:
  case ISD::VP_LRINT:
  case ISD::LLRINT:
  case ISD::VP_LLRINT:
    return RISCVISD::VFCVT_X_F_VL;
  }
  // clang-format on
#undef OP_CASE
#undef VP_CASE
}

/// Return true if a RISC-V target specified op has a merge operand.
static bool hasMergeOp(unsigned Opcode) {
  assert(Opcode > RISCVISD::FIRST_NUMBER &&
         Opcode <= RISCVISD::LAST_RISCV_STRICTFP_OPCODE &&
         "not a RISC-V target specific op");
  static_assert(RISCVISD::LAST_VL_VECTOR_OP - RISCVISD::FIRST_VL_VECTOR_OP ==
                    130 &&
                RISCVISD::LAST_RISCV_STRICTFP_OPCODE -
                        ISD::FIRST_TARGET_STRICTFP_OPCODE ==
                    21 &&
                "adding target specific op should update this function");
  if (Opcode >= RISCVISD::ADD_VL && Opcode <= RISCVISD::VFMAX_VL)
    return true;
  if (Opcode == RISCVISD::FCOPYSIGN_VL)
    return true;
  if (Opcode >= RISCVISD::VWMUL_VL && Opcode <= RISCVISD::VFWSUB_W_VL)
    return true;
  if (Opcode == RISCVISD::SETCC_VL)
    return true;
  if (Opcode >= RISCVISD::STRICT_FADD_VL && Opcode <= RISCVISD::STRICT_FDIV_VL)
    return true;
  if (Opcode == RISCVISD::VMERGE_VL)
    return true;
  return false;
}

/// Return true if a RISC-V target specified op has a mask operand.
static bool hasMaskOp(unsigned Opcode) {
  assert(Opcode > RISCVISD::FIRST_NUMBER &&
         Opcode <= RISCVISD::LAST_RISCV_STRICTFP_OPCODE &&
         "not a RISC-V target specific op");
  static_assert(RISCVISD::LAST_VL_VECTOR_OP - RISCVISD::FIRST_VL_VECTOR_OP ==
                    130 &&
                RISCVISD::LAST_RISCV_STRICTFP_OPCODE -
                        ISD::FIRST_TARGET_STRICTFP_OPCODE ==
                    21 &&
                "adding target specific op should update this function");
  if (Opcode >= RISCVISD::TRUNCATE_VECTOR_VL && Opcode <= RISCVISD::SETCC_VL)
    return true;
  if (Opcode >= RISCVISD::VRGATHER_VX_VL && Opcode <= RISCVISD::VFIRST_VL)
    return true;
  if (Opcode >= RISCVISD::STRICT_FADD_VL &&
      Opcode <= RISCVISD::STRICT_VFROUND_NOEXCEPT_VL)
    return true;
  return false;
}

static SDValue SplitVectorOp(SDValue Op, SelectionDAG &DAG) {
  auto [LoVT, HiVT] = DAG.GetSplitDestVTs(Op.getValueType());
  SDLoc DL(Op);

  SmallVector<SDValue, 4> LoOperands(Op.getNumOperands());
  SmallVector<SDValue, 4> HiOperands(Op.getNumOperands());

  for (unsigned j = 0; j != Op.getNumOperands(); ++j) {
    if (!Op.getOperand(j).getValueType().isVector()) {
      LoOperands[j] = Op.getOperand(j);
      HiOperands[j] = Op.getOperand(j);
      continue;
    }
    std::tie(LoOperands[j], HiOperands[j]) =
        DAG.SplitVector(Op.getOperand(j), DL);
  }

  SDValue LoRes =
      DAG.getNode(Op.getOpcode(), DL, LoVT, LoOperands, Op->getFlags());
  SDValue HiRes =
      DAG.getNode(Op.getOpcode(), DL, HiVT, HiOperands, Op->getFlags());

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, Op.getValueType(), LoRes, HiRes);
}

static SDValue SplitVPOp(SDValue Op, SelectionDAG &DAG) {
  assert(ISD::isVPOpcode(Op.getOpcode()) && "Not a VP op");
  auto [LoVT, HiVT] = DAG.GetSplitDestVTs(Op.getValueType());
  SDLoc DL(Op);

  SmallVector<SDValue, 4> LoOperands(Op.getNumOperands());
  SmallVector<SDValue, 4> HiOperands(Op.getNumOperands());

  for (unsigned j = 0; j != Op.getNumOperands(); ++j) {
    if (ISD::getVPExplicitVectorLengthIdx(Op.getOpcode()) == j) {
      std::tie(LoOperands[j], HiOperands[j]) =
          DAG.SplitEVL(Op.getOperand(j), Op.getValueType(), DL);
      continue;
    }
    if (!Op.getOperand(j).getValueType().isVector()) {
      LoOperands[j] = Op.getOperand(j);
      HiOperands[j] = Op.getOperand(j);
      continue;
    }
    std::tie(LoOperands[j], HiOperands[j]) =
        DAG.SplitVector(Op.getOperand(j), DL);
  }

  SDValue LoRes =
      DAG.getNode(Op.getOpcode(), DL, LoVT, LoOperands, Op->getFlags());
  SDValue HiRes =
      DAG.getNode(Op.getOpcode(), DL, HiVT, HiOperands, Op->getFlags());

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, Op.getValueType(), LoRes, HiRes);
}

static SDValue SplitVectorReductionOp(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);

  auto [Lo, Hi] = DAG.SplitVector(Op.getOperand(1), DL);
  auto [MaskLo, MaskHi] = DAG.SplitVector(Op.getOperand(2), DL);
  auto [EVLLo, EVLHi] =
      DAG.SplitEVL(Op.getOperand(3), Op.getOperand(1).getValueType(), DL);

  SDValue ResLo =
      DAG.getNode(Op.getOpcode(), DL, Op.getValueType(),
                  {Op.getOperand(0), Lo, MaskLo, EVLLo}, Op->getFlags());
  return DAG.getNode(Op.getOpcode(), DL, Op.getValueType(),
                     {ResLo, Hi, MaskHi, EVLHi}, Op->getFlags());
}

static SDValue SplitStrictFPVectorOp(SDValue Op, SelectionDAG &DAG) {

  assert(Op->isStrictFPOpcode());

  auto [LoVT, HiVT] = DAG.GetSplitDestVTs(Op->getValueType(0));

  SDVTList LoVTs = DAG.getVTList(LoVT, Op->getValueType(1));
  SDVTList HiVTs = DAG.getVTList(HiVT, Op->getValueType(1));

  SDLoc DL(Op);

  SmallVector<SDValue, 4> LoOperands(Op.getNumOperands());
  SmallVector<SDValue, 4> HiOperands(Op.getNumOperands());

  for (unsigned j = 0; j != Op.getNumOperands(); ++j) {
    if (!Op.getOperand(j).getValueType().isVector()) {
      LoOperands[j] = Op.getOperand(j);
      HiOperands[j] = Op.getOperand(j);
      continue;
    }
    std::tie(LoOperands[j], HiOperands[j]) =
        DAG.SplitVector(Op.getOperand(j), DL);
  }

  SDValue LoRes =
      DAG.getNode(Op.getOpcode(), DL, LoVTs, LoOperands, Op->getFlags());
  HiOperands[0] = LoRes.getValue(1);
  SDValue HiRes =
      DAG.getNode(Op.getOpcode(), DL, HiVTs, HiOperands, Op->getFlags());

  SDValue V = DAG.getNode(ISD::CONCAT_VECTORS, DL, Op->getValueType(0),
                          LoRes.getValue(0), HiRes.getValue(0));
  return DAG.getMergeValues({V, HiRes.getValue(1)}, DL);
}

SDValue RISCVTargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    report_fatal_error("unimplemented operand");
  case ISD::ATOMIC_FENCE:
    return LowerATOMIC_FENCE(Op, DAG, Subtarget);
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:
    return lowerConstantPool(Op, DAG);
  case ISD::JumpTable:
    return lowerJumpTable(Op, DAG);
  case ISD::GlobalTLSAddress:
    return lowerGlobalTLSAddress(Op, DAG);
  case ISD::Constant:
    return lowerConstant(Op, DAG, Subtarget);
  case ISD::SELECT:
    return lowerSELECT(Op, DAG);
  case ISD::BRCOND:
    return lowerBRCOND(Op, DAG);
  case ISD::VASTART:
    return lowerVASTART(Op, DAG);
  case ISD::FRAMEADDR:
    return lowerFRAMEADDR(Op, DAG);
  case ISD::RETURNADDR:
    return lowerRETURNADDR(Op, DAG);
  case ISD::SADDO:
  case ISD::SSUBO:
    return lowerSADDO_SSUBO(Op, DAG);
  case ISD::SMULO:
    return lowerSMULO(Op, DAG);
  case ISD::SHL_PARTS:
    return lowerShiftLeftParts(Op, DAG);
  case ISD::SRA_PARTS:
    return lowerShiftRightParts(Op, DAG, true);
  case ISD::SRL_PARTS:
    return lowerShiftRightParts(Op, DAG, false);
  case ISD::ROTL:
  case ISD::ROTR:
    if (Op.getValueType().isFixedLengthVector()) {
      assert(Subtarget.hasStdExtZvkb());
      return lowerToScalableOp(Op, DAG);
    }
    assert(Subtarget.hasVendorXTHeadBb() &&
           !(Subtarget.hasStdExtZbb() || Subtarget.hasStdExtZbkb()) &&
           "Unexpected custom legalization");
    // XTHeadBb only supports rotate by constant.
    if (!isa<ConstantSDNode>(Op.getOperand(1)))
      return SDValue();
    return Op;
  case ISD::BITCAST: {
    SDLoc DL(Op);
    EVT VT = Op.getValueType();
    SDValue Op0 = Op.getOperand(0);
    EVT Op0VT = Op0.getValueType();
    MVT XLenVT = Subtarget.getXLenVT();
    if (VT == MVT::f16 && Op0VT == MVT::i16 &&
        Subtarget.hasStdExtZfhminOrZhinxmin()) {
      SDValue NewOp0 = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, Op0);
      SDValue FPConv = DAG.getNode(RISCVISD::FMV_H_X, DL, MVT::f16, NewOp0);
      return FPConv;
    }
    if (VT == MVT::bf16 && Op0VT == MVT::i16 &&
        Subtarget.hasStdExtZfbfmin()) {
      SDValue NewOp0 = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, Op0);
      SDValue FPConv = DAG.getNode(RISCVISD::FMV_H_X, DL, MVT::bf16, NewOp0);
      return FPConv;
    }
    if (VT == MVT::f32 && Op0VT == MVT::i32 && Subtarget.is64Bit() &&
        Subtarget.hasStdExtFOrZfinx()) {
      SDValue NewOp0 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op0);
      SDValue FPConv =
          DAG.getNode(RISCVISD::FMV_W_X_RV64, DL, MVT::f32, NewOp0);
      return FPConv;
    }
    if (VT == MVT::f64 && Op0VT == MVT::i64 && !Subtarget.is64Bit() &&
        Subtarget.hasStdExtDOrZdinx()) {
      SDValue Lo, Hi;
      std::tie(Lo, Hi) = DAG.SplitScalar(Op0, DL, MVT::i32, MVT::i32);
      SDValue RetReg =
          DAG.getNode(RISCVISD::BuildPairF64, DL, MVT::f64, Lo, Hi);
      return RetReg;
    }

    // Consider other scalar<->scalar casts as legal if the types are legal.
    // Otherwise expand them.
    if (!VT.isVector() && !Op0VT.isVector()) {
      if (isTypeLegal(VT) && isTypeLegal(Op0VT))
        return Op;
      return SDValue();
    }

    assert(!VT.isScalableVector() && !Op0VT.isScalableVector() &&
           "Unexpected types");

    if (VT.isFixedLengthVector()) {
      // We can handle fixed length vector bitcasts with a simple replacement
      // in isel.
      if (Op0VT.isFixedLengthVector())
        return Op;
      // When bitcasting from scalar to fixed-length vector, insert the scalar
      // into a one-element vector of the result type, and perform a vector
      // bitcast.
      if (!Op0VT.isVector()) {
        EVT BVT = EVT::getVectorVT(*DAG.getContext(), Op0VT, 1);
        if (!isTypeLegal(BVT))
          return SDValue();
        return DAG.getBitcast(VT, DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, BVT,
                                              DAG.getUNDEF(BVT), Op0,
                                              DAG.getVectorIdxConstant(0, DL)));
      }
      return SDValue();
    }
    // Custom-legalize bitcasts from fixed-length vector types to scalar types
    // thus: bitcast the vector to a one-element vector type whose element type
    // is the same as the result type, and extract the first element.
    if (!VT.isVector() && Op0VT.isFixedLengthVector()) {
      EVT BVT = EVT::getVectorVT(*DAG.getContext(), VT, 1);
      if (!isTypeLegal(BVT))
        return SDValue();
      SDValue BVec = DAG.getBitcast(BVT, Op0);
      return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, BVec,
                         DAG.getVectorIdxConstant(0, DL));
    }
    return SDValue();
  }
  case ISD::INTRINSIC_WO_CHAIN:
    return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::INTRINSIC_W_CHAIN:
    return LowerINTRINSIC_W_CHAIN(Op, DAG);
  case ISD::INTRINSIC_VOID:
    return LowerINTRINSIC_VOID(Op, DAG);
  case ISD::IS_FPCLASS:
    return LowerIS_FPCLASS(Op, DAG);
  case ISD::BITREVERSE: {
    MVT VT = Op.getSimpleValueType();
    if (VT.isFixedLengthVector()) {
      assert(Subtarget.hasStdExtZvbb());
      return lowerToScalableOp(Op, DAG);
    }
    SDLoc DL(Op);
    assert(Subtarget.hasStdExtZbkb() && "Unexpected custom legalization");
    assert(Op.getOpcode() == ISD::BITREVERSE && "Unexpected opcode");
    // Expand bitreverse to a bswap(rev8) followed by brev8.
    SDValue BSwap = DAG.getNode(ISD::BSWAP, DL, VT, Op.getOperand(0));
    return DAG.getNode(RISCVISD::BREV8, DL, VT, BSwap);
  }
  case ISD::TRUNCATE:
    // Only custom-lower vector truncates
    if (!Op.getSimpleValueType().isVector())
      return Op;
    return lowerVectorTruncLike(Op, DAG);
  case ISD::ANY_EXTEND:
  case ISD::ZERO_EXTEND:
    if (Op.getOperand(0).getValueType().isVector() &&
        Op.getOperand(0).getValueType().getVectorElementType() == MVT::i1)
      return lowerVectorMaskExt(Op, DAG, /*ExtVal*/ 1);
    return lowerFixedLengthVectorExtendToRVV(Op, DAG, RISCVISD::VZEXT_VL);
  case ISD::SIGN_EXTEND:
    if (Op.getOperand(0).getValueType().isVector() &&
        Op.getOperand(0).getValueType().getVectorElementType() == MVT::i1)
      return lowerVectorMaskExt(Op, DAG, /*ExtVal*/ -1);
    return lowerFixedLengthVectorExtendToRVV(Op, DAG, RISCVISD::VSEXT_VL);
  case ISD::SPLAT_VECTOR_PARTS:
    return lowerSPLAT_VECTOR_PARTS(Op, DAG);
  case ISD::INSERT_VECTOR_ELT:
    return lowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT:
    return lowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::SCALAR_TO_VECTOR: {
    MVT VT = Op.getSimpleValueType();
    SDLoc DL(Op);
    SDValue Scalar = Op.getOperand(0);
    if (VT.getVectorElementType() == MVT::i1) {
      MVT WideVT = VT.changeVectorElementType(MVT::i8);
      SDValue V = DAG.getNode(ISD::SCALAR_TO_VECTOR, DL, WideVT, Scalar);
      return DAG.getNode(ISD::TRUNCATE, DL, VT, V);
    }
    MVT ContainerVT = VT;
    if (VT.isFixedLengthVector())
      ContainerVT = getContainerForFixedLengthVector(VT);
    SDValue VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;
    Scalar = DAG.getNode(ISD::ANY_EXTEND, DL, Subtarget.getXLenVT(), Scalar);
    SDValue V = DAG.getNode(RISCVISD::VMV_S_X_VL, DL, ContainerVT,
                            DAG.getUNDEF(ContainerVT), Scalar, VL);
    if (VT.isFixedLengthVector())
      V = convertFromScalableVector(VT, V, DAG, Subtarget);
    return V;
  }
  case ISD::VSCALE: {
    MVT XLenVT = Subtarget.getXLenVT();
    MVT VT = Op.getSimpleValueType();
    SDLoc DL(Op);
    SDValue Res = DAG.getNode(RISCVISD::READ_VLENB, DL, XLenVT);
    // We define our scalable vector types for lmul=1 to use a 64 bit known
    // minimum size. e.g. <vscale x 2 x i32>. VLENB is in bytes so we calculate
    // vscale as VLENB / 8.
    static_assert(RISCV::RVVBitsPerBlock == 64, "Unexpected bits per block!");
    if (Subtarget.getRealMinVLen() < RISCV::RVVBitsPerBlock)
      report_fatal_error("Support for VLEN==32 is incomplete.");
    // We assume VLENB is a multiple of 8. We manually choose the best shift
    // here because SimplifyDemandedBits isn't always able to simplify it.
    uint64_t Val = Op.getConstantOperandVal(0);
    if (isPowerOf2_64(Val)) {
      uint64_t Log2 = Log2_64(Val);
      if (Log2 < 3)
        Res = DAG.getNode(ISD::SRL, DL, XLenVT, Res,
                          DAG.getConstant(3 - Log2, DL, VT));
      else if (Log2 > 3)
        Res = DAG.getNode(ISD::SHL, DL, XLenVT, Res,
                          DAG.getConstant(Log2 - 3, DL, XLenVT));
    } else if ((Val % 8) == 0) {
      // If the multiplier is a multiple of 8, scale it down to avoid needing
      // to shift the VLENB value.
      Res = DAG.getNode(ISD::MUL, DL, XLenVT, Res,
                        DAG.getConstant(Val / 8, DL, XLenVT));
    } else {
      SDValue VScale = DAG.getNode(ISD::SRL, DL, XLenVT, Res,
                                   DAG.getConstant(3, DL, XLenVT));
      Res = DAG.getNode(ISD::MUL, DL, XLenVT, VScale,
                        DAG.getConstant(Val, DL, XLenVT));
    }
    return DAG.getNode(ISD::TRUNCATE, DL, VT, Res);
  }
  case ISD::FPOWI: {
    // Custom promote f16 powi with illegal i32 integer type on RV64. Once
    // promoted this will be legalized into a libcall by LegalizeIntegerTypes.
    if (Op.getValueType() == MVT::f16 && Subtarget.is64Bit() &&
        Op.getOperand(1).getValueType() == MVT::i32) {
      SDLoc DL(Op);
      SDValue Op0 = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, Op.getOperand(0));
      SDValue Powi =
          DAG.getNode(ISD::FPOWI, DL, MVT::f32, Op0, Op.getOperand(1));
      return DAG.getNode(ISD::FP_ROUND, DL, MVT::f16, Powi,
                         DAG.getIntPtrConstant(0, DL, /*isTarget=*/true));
    }
    return SDValue();
  }
  case ISD::FMAXIMUM:
  case ISD::FMINIMUM:
    if (Op.getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVectorOp(Op, DAG);
    return lowerFMAXIMUM_FMINIMUM(Op, DAG, Subtarget);
  case ISD::FP_EXTEND: {
    SDLoc DL(Op);
    EVT VT = Op.getValueType();
    SDValue Op0 = Op.getOperand(0);
    EVT Op0VT = Op0.getValueType();
    if (VT == MVT::f32 && Op0VT == MVT::bf16 && Subtarget.hasStdExtZfbfmin())
      return DAG.getNode(RISCVISD::FP_EXTEND_BF16, DL, MVT::f32, Op0);
    if (VT == MVT::f64 && Op0VT == MVT::bf16 && Subtarget.hasStdExtZfbfmin()) {
      SDValue FloatVal =
          DAG.getNode(RISCVISD::FP_EXTEND_BF16, DL, MVT::f32, Op0);
      return DAG.getNode(ISD::FP_EXTEND, DL, MVT::f64, FloatVal);
    }

    if (!Op.getValueType().isVector())
      return Op;
    return lowerVectorFPExtendOrRoundLike(Op, DAG);
  }
  case ISD::FP_ROUND: {
    SDLoc DL(Op);
    EVT VT = Op.getValueType();
    SDValue Op0 = Op.getOperand(0);
    EVT Op0VT = Op0.getValueType();
    if (VT == MVT::bf16 && Op0VT == MVT::f32 && Subtarget.hasStdExtZfbfmin())
      return DAG.getNode(RISCVISD::FP_ROUND_BF16, DL, MVT::bf16, Op0);
    if (VT == MVT::bf16 && Op0VT == MVT::f64 && Subtarget.hasStdExtZfbfmin() &&
        Subtarget.hasStdExtDOrZdinx()) {
      SDValue FloatVal =
          DAG.getNode(ISD::FP_ROUND, DL, MVT::f32, Op0,
                      DAG.getIntPtrConstant(0, DL, /*isTarget=*/true));
      return DAG.getNode(RISCVISD::FP_ROUND_BF16, DL, MVT::bf16, FloatVal);
    }

    if (!Op.getValueType().isVector())
      return Op;
    return lowerVectorFPExtendOrRoundLike(Op, DAG);
  }
  case ISD::STRICT_FP_ROUND:
  case ISD::STRICT_FP_EXTEND:
    return lowerStrictFPExtendOrRoundLike(Op, DAG);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    if (Op.getValueType().isVector() &&
        Op.getValueType().getScalarType() == MVT::f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16())) {
      if (Op.getValueType() == MVT::nxv32f16)
        return SplitVectorOp(Op, DAG);
      // int -> f32
      SDLoc DL(Op);
      MVT NVT =
          MVT::getVectorVT(MVT::f32, Op.getValueType().getVectorElementCount());
      SDValue NC = DAG.getNode(Op.getOpcode(), DL, NVT, Op->ops());
      // f32 -> f16
      return DAG.getNode(ISD::FP_ROUND, DL, Op.getValueType(), NC,
                         DAG.getIntPtrConstant(0, DL, /*isTarget=*/true));
    }
    [[fallthrough]];
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    if (SDValue Op1 = Op.getOperand(0);
        Op1.getValueType().isVector() &&
        Op1.getValueType().getScalarType() == MVT::f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16())) {
      if (Op1.getValueType() == MVT::nxv32f16)
        return SplitVectorOp(Op, DAG);
      // f16 -> f32
      SDLoc DL(Op);
      MVT NVT = MVT::getVectorVT(MVT::f32,
                                 Op1.getValueType().getVectorElementCount());
      SDValue WidenVec = DAG.getNode(ISD::FP_EXTEND, DL, NVT, Op1);
      // f32 -> int
      return DAG.getNode(Op.getOpcode(), DL, Op.getValueType(), WidenVec);
    }
    [[fallthrough]];
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP: {
    // RVV can only do fp<->int conversions to types half/double the size as
    // the source. We custom-lower any conversions that do two hops into
    // sequences.
    MVT VT = Op.getSimpleValueType();
    if (!VT.isVector())
      return Op;
    SDLoc DL(Op);
    bool IsStrict = Op->isStrictFPOpcode();
    SDValue Src = Op.getOperand(0 + IsStrict);
    MVT EltVT = VT.getVectorElementType();
    MVT SrcVT = Src.getSimpleValueType();
    MVT SrcEltVT = SrcVT.getVectorElementType();
    unsigned EltSize = EltVT.getSizeInBits();
    unsigned SrcEltSize = SrcEltVT.getSizeInBits();
    assert(isPowerOf2_32(EltSize) && isPowerOf2_32(SrcEltSize) &&
           "Unexpected vector element types");

    bool IsInt2FP = SrcEltVT.isInteger();
    // Widening conversions
    if (EltSize > (2 * SrcEltSize)) {
      if (IsInt2FP) {
        // Do a regular integer sign/zero extension then convert to float.
        MVT IVecVT = MVT::getVectorVT(MVT::getIntegerVT(EltSize / 2),
                                      VT.getVectorElementCount());
        unsigned ExtOpcode = (Op.getOpcode() == ISD::UINT_TO_FP ||
                              Op.getOpcode() == ISD::STRICT_UINT_TO_FP)
                                 ? ISD::ZERO_EXTEND
                                 : ISD::SIGN_EXTEND;
        SDValue Ext = DAG.getNode(ExtOpcode, DL, IVecVT, Src);
        if (IsStrict)
          return DAG.getNode(Op.getOpcode(), DL, Op->getVTList(),
                             Op.getOperand(0), Ext);
        return DAG.getNode(Op.getOpcode(), DL, VT, Ext);
      }
      // FP2Int
      assert(SrcEltVT == MVT::f16 && "Unexpected FP_TO_[US]INT lowering");
      // Do one doubling fp_extend then complete the operation by converting
      // to int.
      MVT InterimFVT = MVT::getVectorVT(MVT::f32, VT.getVectorElementCount());
      if (IsStrict) {
        auto [FExt, Chain] =
            DAG.getStrictFPExtendOrRound(Src, Op.getOperand(0), DL, InterimFVT);
        return DAG.getNode(Op.getOpcode(), DL, Op->getVTList(), Chain, FExt);
      }
      SDValue FExt = DAG.getFPExtendOrRound(Src, DL, InterimFVT);
      return DAG.getNode(Op.getOpcode(), DL, VT, FExt);
    }

    // Narrowing conversions
    if (SrcEltSize > (2 * EltSize)) {
      if (IsInt2FP) {
        // One narrowing int_to_fp, then an fp_round.
        assert(EltVT == MVT::f16 && "Unexpected [US]_TO_FP lowering");
        MVT InterimFVT = MVT::getVectorVT(MVT::f32, VT.getVectorElementCount());
        if (IsStrict) {
          SDValue Int2FP = DAG.getNode(Op.getOpcode(), DL,
                                       DAG.getVTList(InterimFVT, MVT::Other),
                                       Op.getOperand(0), Src);
          SDValue Chain = Int2FP.getValue(1);
          return DAG.getStrictFPExtendOrRound(Int2FP, Chain, DL, VT).first;
        }
        SDValue Int2FP = DAG.getNode(Op.getOpcode(), DL, InterimFVT, Src);
        return DAG.getFPExtendOrRound(Int2FP, DL, VT);
      }
      // FP2Int
      // One narrowing fp_to_int, then truncate the integer. If the float isn't
      // representable by the integer, the result is poison.
      MVT IVecVT = MVT::getVectorVT(MVT::getIntegerVT(SrcEltSize / 2),
                                    VT.getVectorElementCount());
      if (IsStrict) {
        SDValue FP2Int =
            DAG.getNode(Op.getOpcode(), DL, DAG.getVTList(IVecVT, MVT::Other),
                        Op.getOperand(0), Src);
        SDValue Res = DAG.getNode(ISD::TRUNCATE, DL, VT, FP2Int);
        return DAG.getMergeValues({Res, FP2Int.getValue(1)}, DL);
      }
      SDValue FP2Int = DAG.getNode(Op.getOpcode(), DL, IVecVT, Src);
      return DAG.getNode(ISD::TRUNCATE, DL, VT, FP2Int);
    }

    // Scalable vectors can exit here. Patterns will handle equally-sized
    // conversions halving/doubling ones.
    if (!VT.isFixedLengthVector())
      return Op;

    // For fixed-length vectors we lower to a custom "VL" node.
    unsigned RVVOpc = 0;
    switch (Op.getOpcode()) {
    default:
      llvm_unreachable("Impossible opcode");
    case ISD::FP_TO_SINT:
      RVVOpc = RISCVISD::VFCVT_RTZ_X_F_VL;
      break;
    case ISD::FP_TO_UINT:
      RVVOpc = RISCVISD::VFCVT_RTZ_XU_F_VL;
      break;
    case ISD::SINT_TO_FP:
      RVVOpc = RISCVISD::SINT_TO_FP_VL;
      break;
    case ISD::UINT_TO_FP:
      RVVOpc = RISCVISD::UINT_TO_FP_VL;
      break;
    case ISD::STRICT_FP_TO_SINT:
      RVVOpc = RISCVISD::STRICT_VFCVT_RTZ_X_F_VL;
      break;
    case ISD::STRICT_FP_TO_UINT:
      RVVOpc = RISCVISD::STRICT_VFCVT_RTZ_XU_F_VL;
      break;
    case ISD::STRICT_SINT_TO_FP:
      RVVOpc = RISCVISD::STRICT_SINT_TO_FP_VL;
      break;
    case ISD::STRICT_UINT_TO_FP:
      RVVOpc = RISCVISD::STRICT_UINT_TO_FP_VL;
      break;
    }

    MVT ContainerVT = getContainerForFixedLengthVector(VT);
    MVT SrcContainerVT = getContainerForFixedLengthVector(SrcVT);
    assert(ContainerVT.getVectorElementCount() == SrcContainerVT.getVectorElementCount() &&
           "Expected same element count");

    auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

    Src = convertToScalableVector(SrcContainerVT, Src, DAG, Subtarget);
    if (IsStrict) {
      Src = DAG.getNode(RVVOpc, DL, DAG.getVTList(ContainerVT, MVT::Other),
                        Op.getOperand(0), Src, Mask, VL);
      SDValue SubVec = convertFromScalableVector(VT, Src, DAG, Subtarget);
      return DAG.getMergeValues({SubVec, Src.getValue(1)}, DL);
    }
    Src = DAG.getNode(RVVOpc, DL, ContainerVT, Src, Mask, VL);
    return convertFromScalableVector(VT, Src, DAG, Subtarget);
  }
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    return lowerFP_TO_INT_SAT(Op, DAG, Subtarget);
  case ISD::FP_TO_BF16: {
    // Custom lower to ensure the libcall return is passed in an FPR on hard
    // float ABIs.
    assert(!Subtarget.isSoftFPABI() && "Unexpected custom legalization");
    SDLoc DL(Op);
    MakeLibCallOptions CallOptions;
    RTLIB::Libcall LC =
        RTLIB::getFPROUND(Op.getOperand(0).getValueType(), MVT::bf16);
    SDValue Res =
        makeLibCall(DAG, LC, MVT::f32, Op.getOperand(0), CallOptions, DL).first;
    if (Subtarget.is64Bit() && !RV64LegalI32)
      return DAG.getNode(RISCVISD::FMV_X_ANYEXTW_RV64, DL, MVT::i64, Res);
    return DAG.getBitcast(MVT::i32, Res);
  }
  case ISD::BF16_TO_FP: {
    assert(Subtarget.hasStdExtFOrZfinx() && "Unexpected custom legalization");
    MVT VT = Op.getSimpleValueType();
    SDLoc DL(Op);
    Op = DAG.getNode(
        ISD::SHL, DL, Op.getOperand(0).getValueType(), Op.getOperand(0),
        DAG.getShiftAmountConstant(16, Op.getOperand(0).getValueType(), DL));
    SDValue Res = Subtarget.is64Bit()
                      ? DAG.getNode(RISCVISD::FMV_W_X_RV64, DL, MVT::f32, Op)
                      : DAG.getBitcast(MVT::f32, Op);
    // fp_extend if the target VT is bigger than f32.
    if (VT != MVT::f32)
      return DAG.getNode(ISD::FP_EXTEND, DL, VT, Res);
    return Res;
  }
  case ISD::FP_TO_FP16: {
    // Custom lower to ensure the libcall return is passed in an FPR on hard
    // float ABIs.
    assert(Subtarget.hasStdExtFOrZfinx() && "Unexpected custom legalisation");
    SDLoc DL(Op);
    MakeLibCallOptions CallOptions;
    RTLIB::Libcall LC =
        RTLIB::getFPROUND(Op.getOperand(0).getValueType(), MVT::f16);
    SDValue Res =
        makeLibCall(DAG, LC, MVT::f32, Op.getOperand(0), CallOptions, DL).first;
    if (Subtarget.is64Bit() && !RV64LegalI32)
      return DAG.getNode(RISCVISD::FMV_X_ANYEXTW_RV64, DL, MVT::i64, Res);
    return DAG.getBitcast(MVT::i32, Res);
  }
  case ISD::FP16_TO_FP: {
    // Custom lower to ensure the libcall argument is passed in an FPR on hard
    // float ABIs.
    assert(Subtarget.hasStdExtFOrZfinx() && "Unexpected custom legalisation");
    SDLoc DL(Op);
    MakeLibCallOptions CallOptions;
    SDValue Arg = Subtarget.is64Bit()
                      ? DAG.getNode(RISCVISD::FMV_W_X_RV64, DL, MVT::f32,
                                    Op.getOperand(0))
                      : DAG.getBitcast(MVT::f32, Op.getOperand(0));
    SDValue Res =
        makeLibCall(DAG, RTLIB::FPEXT_F16_F32, MVT::f32, Arg, CallOptions, DL)
            .first;
    return Res;
  }
  case ISD::FTRUNC:
  case ISD::FCEIL:
  case ISD::FFLOOR:
  case ISD::FNEARBYINT:
  case ISD::FRINT:
  case ISD::FROUND:
  case ISD::FROUNDEVEN:
    return lowerFTRUNC_FCEIL_FFLOOR_FROUND(Op, DAG, Subtarget);
  case ISD::LRINT:
  case ISD::LLRINT:
    return lowerVectorXRINT(Op, DAG, Subtarget);
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_SMIN:
    return lowerVECREDUCE(Op, DAG);
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
    if (Op.getOperand(0).getValueType().getVectorElementType() == MVT::i1)
      return lowerVectorMaskVecReduction(Op, DAG, /*IsVP*/ false);
    return lowerVECREDUCE(Op, DAG);
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    return lowerFPVECREDUCE(Op, DAG);
  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_UMIN:
  case ISD::VP_REDUCE_SMIN:
  case ISD::VP_REDUCE_FADD:
  case ISD::VP_REDUCE_SEQ_FADD:
  case ISD::VP_REDUCE_FMIN:
  case ISD::VP_REDUCE_FMAX:
  case ISD::VP_REDUCE_FMINIMUM:
  case ISD::VP_REDUCE_FMAXIMUM:
    if (Op.getOperand(1).getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVectorReductionOp(Op, DAG);
    return lowerVPREDUCE(Op, DAG);
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
    if (Op.getOperand(1).getValueType().getVectorElementType() == MVT::i1)
      return lowerVectorMaskVecReduction(Op, DAG, /*IsVP*/ true);
    return lowerVPREDUCE(Op, DAG);
  case ISD::VP_CTTZ_ELTS:
  case ISD::VP_CTTZ_ELTS_ZERO_UNDEF:
    return lowerVPCttzElements(Op, DAG);
  case ISD::UNDEF: {
    MVT ContainerVT = getContainerForFixedLengthVector(Op.getSimpleValueType());
    return convertFromScalableVector(Op.getSimpleValueType(),
                                     DAG.getUNDEF(ContainerVT), DAG, Subtarget);
  }
  case ISD::INSERT_SUBVECTOR:
    return lowerINSERT_SUBVECTOR(Op, DAG);
  case ISD::EXTRACT_SUBVECTOR:
    return lowerEXTRACT_SUBVECTOR(Op, DAG);
  case ISD::VECTOR_DEINTERLEAVE:
    return lowerVECTOR_DEINTERLEAVE(Op, DAG);
  case ISD::VECTOR_INTERLEAVE:
    return lowerVECTOR_INTERLEAVE(Op, DAG);
  case ISD::STEP_VECTOR:
    return lowerSTEP_VECTOR(Op, DAG);
  case ISD::VECTOR_REVERSE:
    return lowerVECTOR_REVERSE(Op, DAG);
  case ISD::VECTOR_SPLICE:
    return lowerVECTOR_SPLICE(Op, DAG);
  case ISD::BUILD_VECTOR:
    return lowerBUILD_VECTOR(Op, DAG, Subtarget);
  case ISD::SPLAT_VECTOR:
    if ((Op.getValueType().getScalarType() == MVT::f16 &&
         (Subtarget.hasVInstructionsF16Minimal() &&
          Subtarget.hasStdExtZfhminOrZhinxmin() &&
          !Subtarget.hasVInstructionsF16())) ||
        (Op.getValueType().getScalarType() == MVT::bf16 &&
         (Subtarget.hasVInstructionsBF16() && Subtarget.hasStdExtZfbfmin()))) {
      if (Op.getValueType() == MVT::nxv32f16 ||
          Op.getValueType() == MVT::nxv32bf16)
        return SplitVectorOp(Op, DAG);
      SDLoc DL(Op);
      SDValue NewScalar =
          DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, Op.getOperand(0));
      SDValue NewSplat = DAG.getNode(
          ISD::SPLAT_VECTOR, DL,
          MVT::getVectorVT(MVT::f32, Op.getValueType().getVectorElementCount()),
          NewScalar);
      return DAG.getNode(ISD::FP_ROUND, DL, Op.getValueType(), NewSplat,
                         DAG.getIntPtrConstant(0, DL, /*isTarget=*/true));
    }
    if (Op.getValueType().getVectorElementType() == MVT::i1)
      return lowerVectorMaskSplat(Op, DAG);
    return SDValue();
  case ISD::VECTOR_SHUFFLE:
    return lowerVECTOR_SHUFFLE(Op, DAG, Subtarget);
  case ISD::CONCAT_VECTORS: {
    // Split CONCAT_VECTORS into a series of INSERT_SUBVECTOR nodes. This is
    // better than going through the stack, as the default expansion does.
    SDLoc DL(Op);
    MVT VT = Op.getSimpleValueType();
    MVT ContainerVT = VT;
    if (VT.isFixedLengthVector())
      ContainerVT = ::getContainerForFixedLengthVector(DAG, VT, Subtarget);

    // Recursively split concat_vectors with more than 2 operands:
    //
    // concat_vector op1, op2, op3, op4
    // ->
    // concat_vector (concat_vector op1, op2), (concat_vector op3, op4)
    //
    // This reduces the length of the chain of vslideups and allows us to
    // perform the vslideups at a smaller LMUL, limited to MF2.
    if (Op.getNumOperands() > 2 &&
        ContainerVT.bitsGE(getLMUL1VT(ContainerVT))) {
      MVT HalfVT = VT.getHalfNumVectorElementsVT();
      assert(isPowerOf2_32(Op.getNumOperands()));
      size_t HalfNumOps = Op.getNumOperands() / 2;
      SDValue Lo = DAG.getNode(ISD::CONCAT_VECTORS, DL, HalfVT,
                               Op->ops().take_front(HalfNumOps));
      SDValue Hi = DAG.getNode(ISD::CONCAT_VECTORS, DL, HalfVT,
                               Op->ops().drop_front(HalfNumOps));
      return DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, Lo, Hi);
    }

    unsigned NumOpElts =
        Op.getOperand(0).getSimpleValueType().getVectorMinNumElements();
    SDValue Vec = DAG.getUNDEF(VT);
    for (const auto &OpIdx : enumerate(Op->ops())) {
      SDValue SubVec = OpIdx.value();
      // Don't insert undef subvectors.
      if (SubVec.isUndef())
        continue;
      Vec =
          DAG.getNode(ISD::INSERT_SUBVECTOR, DL, VT, Vec, SubVec,
                      DAG.getVectorIdxConstant(OpIdx.index() * NumOpElts, DL));
    }
    return Vec;
  }
  case ISD::LOAD:
    if (auto V = expandUnalignedRVVLoad(Op, DAG))
      return V;
    if (Op.getValueType().isFixedLengthVector())
      return lowerFixedLengthVectorLoadToRVV(Op, DAG);
    return Op;
  case ISD::STORE:
    if (auto V = expandUnalignedRVVStore(Op, DAG))
      return V;
    if (Op.getOperand(1).getValueType().isFixedLengthVector())
      return lowerFixedLengthVectorStoreToRVV(Op, DAG);
    return Op;
  case ISD::MLOAD:
  case ISD::VP_LOAD:
    return lowerMaskedLoad(Op, DAG);
  case ISD::MSTORE:
  case ISD::VP_STORE:
    return lowerMaskedStore(Op, DAG);
  case ISD::SELECT_CC: {
    // This occurs because we custom legalize SETGT and SETUGT for setcc. That
    // causes LegalizeDAG to think we need to custom legalize select_cc. Expand
    // into separate SETCC+SELECT just like LegalizeDAG.
    SDValue Tmp1 = Op.getOperand(0);
    SDValue Tmp2 = Op.getOperand(1);
    SDValue True = Op.getOperand(2);
    SDValue False = Op.getOperand(3);
    EVT VT = Op.getValueType();
    SDValue CC = Op.getOperand(4);
    EVT CmpVT = Tmp1.getValueType();
    EVT CCVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), CmpVT);
    SDLoc DL(Op);
    SDValue Cond =
        DAG.getNode(ISD::SETCC, DL, CCVT, Tmp1, Tmp2, CC, Op->getFlags());
    return DAG.getSelect(DL, VT, Cond, True, False);
  }
  case ISD::SETCC: {
    MVT OpVT = Op.getOperand(0).getSimpleValueType();
    if (OpVT.isScalarInteger()) {
      MVT VT = Op.getSimpleValueType();
      SDValue LHS = Op.getOperand(0);
      SDValue RHS = Op.getOperand(1);
      ISD::CondCode CCVal = cast<CondCodeSDNode>(Op.getOperand(2))->get();
      assert((CCVal == ISD::SETGT || CCVal == ISD::SETUGT) &&
             "Unexpected CondCode");

      SDLoc DL(Op);

      // If the RHS is a constant in the range [-2049, 0) or (0, 2046], we can
      // convert this to the equivalent of (set(u)ge X, C+1) by using
      // (xori (slti(u) X, C+1), 1). This avoids materializing a small constant
      // in a register.
      if (isa<ConstantSDNode>(RHS)) {
        int64_t Imm = cast<ConstantSDNode>(RHS)->getSExtValue();
        if (Imm != 0 && isInt<12>((uint64_t)Imm + 1)) {
          // If this is an unsigned compare and the constant is -1, incrementing
          // the constant would change behavior. The result should be false.
          if (CCVal == ISD::SETUGT && Imm == -1)
            return DAG.getConstant(0, DL, VT);
          // Using getSetCCSwappedOperands will convert SET(U)GT->SET(U)LT.
          CCVal = ISD::getSetCCSwappedOperands(CCVal);
          SDValue SetCC = DAG.getSetCC(
              DL, VT, LHS, DAG.getConstant(Imm + 1, DL, OpVT), CCVal);
          return DAG.getLogicalNOT(DL, SetCC, VT);
        }
      }

      // Not a constant we could handle, swap the operands and condition code to
      // SETLT/SETULT.
      CCVal = ISD::getSetCCSwappedOperands(CCVal);
      return DAG.getSetCC(DL, VT, RHS, LHS, CCVal);
    }

    if (Op.getOperand(0).getSimpleValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVectorOp(Op, DAG);

    return lowerFixedLengthVectorSetccToRVV(Op, DAG);
  }
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SDIV:
  case ISD::SREM:
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::BSWAP:
  case ISD::CTPOP:
    return lowerToScalableOp(Op, DAG);
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    if (Op.getSimpleValueType().isFixedLengthVector())
      return lowerToScalableOp(Op, DAG);
    // This can be called for an i32 shift amount that needs to be promoted.
    assert(Op.getOperand(1).getValueType() == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    return SDValue();
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV:
  case ISD::FNEG:
  case ISD::FABS:
  case ISD::FSQRT:
  case ISD::FMA:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
    if (Op.getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVectorOp(Op, DAG);
    [[fallthrough]];
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
    return lowerToScalableOp(Op, DAG);
  case ISD::UADDSAT:
  case ISD::USUBSAT:
    if (!Op.getValueType().isVector())
      return lowerUADDSAT_USUBSAT(Op, DAG);
    return lowerToScalableOp(Op, DAG);
  case ISD::SADDSAT:
  case ISD::SSUBSAT:
    if (!Op.getValueType().isVector())
      return lowerSADDSAT_SSUBSAT(Op, DAG);
    return lowerToScalableOp(Op, DAG);
  case ISD::ABDS:
  case ISD::ABDU: {
    SDLoc dl(Op);
    EVT VT = Op->getValueType(0);
    SDValue LHS = DAG.getFreeze(Op->getOperand(0));
    SDValue RHS = DAG.getFreeze(Op->getOperand(1));
    bool IsSigned = Op->getOpcode() == ISD::ABDS;

    // abds(lhs, rhs) -> sub(smax(lhs,rhs), smin(lhs,rhs))
    // abdu(lhs, rhs) -> sub(umax(lhs,rhs), umin(lhs,rhs))
    unsigned MaxOpc = IsSigned ? ISD::SMAX : ISD::UMAX;
    unsigned MinOpc = IsSigned ? ISD::SMIN : ISD::UMIN;
    SDValue Max = DAG.getNode(MaxOpc, dl, VT, LHS, RHS);
    SDValue Min = DAG.getNode(MinOpc, dl, VT, LHS, RHS);
    return DAG.getNode(ISD::SUB, dl, VT, Max, Min);
  }
  case ISD::ABS:
  case ISD::VP_ABS:
    return lowerABS(Op, DAG);
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
    if (Subtarget.hasStdExtZvbb())
      return lowerToScalableOp(Op, DAG);
    assert(Op.getOpcode() != ISD::CTTZ);
    return lowerCTLZ_CTTZ_ZERO_UNDEF(Op, DAG);
  case ISD::VSELECT:
    return lowerFixedLengthVectorSelectToRVV(Op, DAG);
  case ISD::FCOPYSIGN:
    if (Op.getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVectorOp(Op, DAG);
    return lowerFixedLengthVectorFCOPYSIGNToRVV(Op, DAG);
  case ISD::STRICT_FADD:
  case ISD::STRICT_FSUB:
  case ISD::STRICT_FMUL:
  case ISD::STRICT_FDIV:
  case ISD::STRICT_FSQRT:
  case ISD::STRICT_FMA:
    if (Op.getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitStrictFPVectorOp(Op, DAG);
    return lowerToScalableOp(Op, DAG);
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
    return lowerVectorStrictFSetcc(Op, DAG);
  case ISD::STRICT_FCEIL:
  case ISD::STRICT_FRINT:
  case ISD::STRICT_FFLOOR:
  case ISD::STRICT_FTRUNC:
  case ISD::STRICT_FNEARBYINT:
  case ISD::STRICT_FROUND:
  case ISD::STRICT_FROUNDEVEN:
    return lowerVectorStrictFTRUNC_FCEIL_FFLOOR_FROUND(Op, DAG, Subtarget);
  case ISD::MGATHER:
  case ISD::VP_GATHER:
    return lowerMaskedGather(Op, DAG);
  case ISD::MSCATTER:
  case ISD::VP_SCATTER:
    return lowerMaskedScatter(Op, DAG);
  case ISD::GET_ROUNDING:
    return lowerGET_ROUNDING(Op, DAG);
  case ISD::SET_ROUNDING:
    return lowerSET_ROUNDING(Op, DAG);
  case ISD::EH_DWARF_CFA:
    return lowerEH_DWARF_CFA(Op, DAG);
  case ISD::VP_SELECT:
  case ISD::VP_MERGE:
  case ISD::VP_ADD:
  case ISD::VP_SUB:
  case ISD::VP_MUL:
  case ISD::VP_SDIV:
  case ISD::VP_UDIV:
  case ISD::VP_SREM:
  case ISD::VP_UREM:
  case ISD::VP_UADDSAT:
  case ISD::VP_USUBSAT:
  case ISD::VP_SADDSAT:
  case ISD::VP_SSUBSAT:
  case ISD::VP_LRINT:
  case ISD::VP_LLRINT:
    return lowerVPOp(Op, DAG);
  case ISD::VP_AND:
  case ISD::VP_OR:
  case ISD::VP_XOR:
    return lowerLogicVPOp(Op, DAG);
  case ISD::VP_FADD:
  case ISD::VP_FSUB:
  case ISD::VP_FMUL:
  case ISD::VP_FDIV:
  case ISD::VP_FNEG:
  case ISD::VP_FABS:
  case ISD::VP_SQRT:
  case ISD::VP_FMA:
  case ISD::VP_FMINNUM:
  case ISD::VP_FMAXNUM:
  case ISD::VP_FCOPYSIGN:
    if (Op.getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVPOp(Op, DAG);
    [[fallthrough]];
  case ISD::VP_SRA:
  case ISD::VP_SRL:
  case ISD::VP_SHL:
    return lowerVPOp(Op, DAG);
  case ISD::VP_IS_FPCLASS:
    return LowerIS_FPCLASS(Op, DAG);
  case ISD::VP_SIGN_EXTEND:
  case ISD::VP_ZERO_EXTEND:
    if (Op.getOperand(0).getSimpleValueType().getVectorElementType() == MVT::i1)
      return lowerVPExtMaskOp(Op, DAG);
    return lowerVPOp(Op, DAG);
  case ISD::VP_TRUNCATE:
    return lowerVectorTruncLike(Op, DAG);
  case ISD::VP_FP_EXTEND:
  case ISD::VP_FP_ROUND:
    return lowerVectorFPExtendOrRoundLike(Op, DAG);
  case ISD::VP_SINT_TO_FP:
  case ISD::VP_UINT_TO_FP:
    if (Op.getValueType().isVector() &&
        Op.getValueType().getScalarType() == MVT::f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16())) {
      if (Op.getValueType() == MVT::nxv32f16)
        return SplitVPOp(Op, DAG);
      // int -> f32
      SDLoc DL(Op);
      MVT NVT =
          MVT::getVectorVT(MVT::f32, Op.getValueType().getVectorElementCount());
      auto NC = DAG.getNode(Op.getOpcode(), DL, NVT, Op->ops());
      // f32 -> f16
      return DAG.getNode(ISD::FP_ROUND, DL, Op.getValueType(), NC,
                         DAG.getIntPtrConstant(0, DL, /*isTarget=*/true));
    }
    [[fallthrough]];
  case ISD::VP_FP_TO_SINT:
  case ISD::VP_FP_TO_UINT:
    if (SDValue Op1 = Op.getOperand(0);
        Op1.getValueType().isVector() &&
        Op1.getValueType().getScalarType() == MVT::f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16())) {
      if (Op1.getValueType() == MVT::nxv32f16)
        return SplitVPOp(Op, DAG);
      // f16 -> f32
      SDLoc DL(Op);
      MVT NVT = MVT::getVectorVT(MVT::f32,
                                 Op1.getValueType().getVectorElementCount());
      SDValue WidenVec = DAG.getNode(ISD::FP_EXTEND, DL, NVT, Op1);
      // f32 -> int
      return DAG.getNode(Op.getOpcode(), DL, Op.getValueType(),
                         {WidenVec, Op.getOperand(1), Op.getOperand(2)});
    }
    return lowerVPFPIntConvOp(Op, DAG);
  case ISD::VP_SETCC:
    if (Op.getOperand(0).getSimpleValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVPOp(Op, DAG);
    if (Op.getOperand(0).getSimpleValueType().getVectorElementType() == MVT::i1)
      return lowerVPSetCCMaskOp(Op, DAG);
    [[fallthrough]];
  case ISD::VP_SMIN:
  case ISD::VP_SMAX:
  case ISD::VP_UMIN:
  case ISD::VP_UMAX:
  case ISD::VP_BITREVERSE:
  case ISD::VP_BSWAP:
    return lowerVPOp(Op, DAG);
  case ISD::VP_CTLZ:
  case ISD::VP_CTLZ_ZERO_UNDEF:
    if (Subtarget.hasStdExtZvbb())
      return lowerVPOp(Op, DAG);
    return lowerCTLZ_CTTZ_ZERO_UNDEF(Op, DAG);
  case ISD::VP_CTTZ:
  case ISD::VP_CTTZ_ZERO_UNDEF:
    if (Subtarget.hasStdExtZvbb())
      return lowerVPOp(Op, DAG);
    return lowerCTLZ_CTTZ_ZERO_UNDEF(Op, DAG);
  case ISD::VP_CTPOP:
    return lowerVPOp(Op, DAG);
  case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
    return lowerVPStridedLoad(Op, DAG);
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    return lowerVPStridedStore(Op, DAG);
  case ISD::VP_FCEIL:
  case ISD::VP_FFLOOR:
  case ISD::VP_FRINT:
  case ISD::VP_FNEARBYINT:
  case ISD::VP_FROUND:
  case ISD::VP_FROUNDEVEN:
  case ISD::VP_FROUNDTOZERO:
    if (Op.getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVPOp(Op, DAG);
    return lowerVectorFTRUNC_FCEIL_FFLOOR_FROUND(Op, DAG, Subtarget);
  case ISD::VP_FMAXIMUM:
  case ISD::VP_FMINIMUM:
    if (Op.getValueType() == MVT::nxv32f16 &&
        (Subtarget.hasVInstructionsF16Minimal() &&
         !Subtarget.hasVInstructionsF16()))
      return SplitVPOp(Op, DAG);
    return lowerFMAXIMUM_FMINIMUM(Op, DAG, Subtarget);
  case ISD::EXPERIMENTAL_VP_SPLICE:
    return lowerVPSpliceExperimental(Op, DAG);
  case ISD::EXPERIMENTAL_VP_REVERSE:
    return lowerVPReverseExperimental(Op, DAG);
  case ISD::EXPERIMENTAL_VP_SPLAT:
    return lowerVPSplatExperimental(Op, DAG);
  case ISD::CLEAR_CACHE: {
    assert(getTargetMachine().getTargetTriple().isOSLinux() &&
           "llvm.clear_cache only needs custom lower on Linux targets");
    SDLoc DL(Op);
    SDValue Flags = DAG.getConstant(0, DL, Subtarget.getXLenVT());
    return emitFlushICache(DAG, Op.getOperand(0), Op.getOperand(1),
                           Op.getOperand(2), Flags, DL);
  }
  }
}

SDValue RISCVTargetLowering::emitFlushICache(SelectionDAG &DAG, SDValue InChain,
                                             SDValue Start, SDValue End,
                                             SDValue Flags, SDLoc DL) const {
  MakeLibCallOptions CallOptions;
  std::pair<SDValue, SDValue> CallResult =
      makeLibCall(DAG, RTLIB::RISCV_FLUSH_ICACHE, MVT::isVoid,
                  {Start, End, Flags}, CallOptions, DL, InChain);

  // This function returns void so only the out chain matters.
  return CallResult.second;
}

static SDValue getTargetNode(GlobalAddressSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetGlobalAddress(N->getGlobal(), DL, Ty, 0, Flags);
}

static SDValue getTargetNode(BlockAddressSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetBlockAddress(N->getBlockAddress(), Ty, N->getOffset(),
                                   Flags);
}

static SDValue getTargetNode(ConstantPoolSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetConstantPool(N->getConstVal(), Ty, N->getAlign(),
                                   N->getOffset(), Flags);
}

static SDValue getTargetNode(JumpTableSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetJumpTable(N->getIndex(), Ty, Flags);
}

template <class NodeTy>
SDValue RISCVTargetLowering::getAddr(NodeTy *N, SelectionDAG &DAG,
                                     bool IsLocal, bool IsExternWeak) const {
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());

  // When HWASAN is used and tagging of global variables is enabled
  // they should be accessed via the GOT, since the tagged address of a global
  // is incompatible with existing code models. This also applies to non-pic
  // mode.
  if (isPositionIndependent() || Subtarget.allowTaggedGlobals()) {
    SDValue Addr = getTargetNode(N, DL, Ty, DAG, 0);
    if (IsLocal && !Subtarget.allowTaggedGlobals())
      // Use PC-relative addressing to access the symbol. This generates the
      // pattern (PseudoLLA sym), which expands to (addi (auipc %pcrel_hi(sym))
      // %pcrel_lo(auipc)).
      return DAG.getNode(RISCVISD::LLA, DL, Ty, Addr);

    // Use PC-relative addressing to access the GOT for this symbol, then load
    // the address from the GOT. This generates the pattern (PseudoLGA sym),
    // which expands to (ld (addi (auipc %got_pcrel_hi(sym)) %pcrel_lo(auipc))).
    SDValue Load =
        SDValue(DAG.getMachineNode(RISCV::PseudoLGA, DL, Ty, Addr), 0);
    MachineFunction &MF = DAG.getMachineFunction();
    MachineMemOperand *MemOp = MF.getMachineMemOperand(
        MachinePointerInfo::getGOT(MF),
        MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
            MachineMemOperand::MOInvariant,
        LLT(Ty.getSimpleVT()), Align(Ty.getFixedSizeInBits() / 8));
    DAG.setNodeMemRefs(cast<MachineSDNode>(Load.getNode()), {MemOp});
    return Load;
  }

  switch (getTargetMachine().getCodeModel()) {
  default:
    report_fatal_error("Unsupported code model for lowering");
  case CodeModel::Small: {
    // Generate a sequence for accessing addresses within the first 2 GiB of
    // address space. This generates the pattern (addi (lui %hi(sym)) %lo(sym)).
    SDValue AddrHi = getTargetNode(N, DL, Ty, DAG, RISCVII::MO_HI);
    SDValue AddrLo = getTargetNode(N, DL, Ty, DAG, RISCVII::MO_LO);
    SDValue MNHi = DAG.getNode(RISCVISD::HI, DL, Ty, AddrHi);
    return DAG.getNode(RISCVISD::ADD_LO, DL, Ty, MNHi, AddrLo);
  }
  case CodeModel::Medium: {
    SDValue Addr = getTargetNode(N, DL, Ty, DAG, 0);
    if (IsExternWeak) {
      // An extern weak symbol may be undefined, i.e. have value 0, which may
      // not be within 2GiB of PC, so use GOT-indirect addressing to access the
      // symbol. This generates the pattern (PseudoLGA sym), which expands to
      // (ld (addi (auipc %got_pcrel_hi(sym)) %pcrel_lo(auipc))).
      SDValue Load =
          SDValue(DAG.getMachineNode(RISCV::PseudoLGA, DL, Ty, Addr), 0);
      MachineFunction &MF = DAG.getMachineFunction();
      MachineMemOperand *MemOp = MF.getMachineMemOperand(
          MachinePointerInfo::getGOT(MF),
          MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
              MachineMemOperand::MOInvariant,
          LLT(Ty.getSimpleVT()), Align(Ty.getFixedSizeInBits() / 8));
      DAG.setNodeMemRefs(cast<MachineSDNode>(Load.getNode()), {MemOp});
      return Load;
    }

    // Generate a sequence for accessing addresses within any 2GiB range within
    // the address space. This generates the pattern (PseudoLLA sym), which
    // expands to (addi (auipc %pcrel_hi(sym)) %pcrel_lo(auipc)).
    return DAG.getNode(RISCVISD::LLA, DL, Ty, Addr);
  }
  }
}

SDValue RISCVTargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  assert(N->getOffset() == 0 && "unexpected offset in global node");
  const GlobalValue *GV = N->getGlobal();
  return getAddr(N, DAG, GV->isDSOLocal(), GV->hasExternalWeakLinkage());
}

SDValue RISCVTargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  BlockAddressSDNode *N = cast<BlockAddressSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue RISCVTargetLowering::lowerConstantPool(SDValue Op,
                                               SelectionDAG &DAG) const {
  ConstantPoolSDNode *N = cast<ConstantPoolSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue RISCVTargetLowering::lowerJumpTable(SDValue Op,
                                            SelectionDAG &DAG) const {
  JumpTableSDNode *N = cast<JumpTableSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue RISCVTargetLowering::getStaticTLSAddr(GlobalAddressSDNode *N,
                                              SelectionDAG &DAG,
                                              bool UseGOT) const {
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  const GlobalValue *GV = N->getGlobal();
  MVT XLenVT = Subtarget.getXLenVT();

  if (UseGOT) {
    // Use PC-relative addressing to access the GOT for this TLS symbol, then
    // load the address from the GOT and add the thread pointer. This generates
    // the pattern (PseudoLA_TLS_IE sym), which expands to
    // (ld (auipc %tls_ie_pcrel_hi(sym)) %pcrel_lo(auipc)).
    SDValue Addr = DAG.getTargetGlobalAddress(GV, DL, Ty, 0, 0);
    SDValue Load =
        SDValue(DAG.getMachineNode(RISCV::PseudoLA_TLS_IE, DL, Ty, Addr), 0);
    MachineFunction &MF = DAG.getMachineFunction();
    MachineMemOperand *MemOp = MF.getMachineMemOperand(
        MachinePointerInfo::getGOT(MF),
        MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
            MachineMemOperand::MOInvariant,
        LLT(Ty.getSimpleVT()), Align(Ty.getFixedSizeInBits() / 8));
    DAG.setNodeMemRefs(cast<MachineSDNode>(Load.getNode()), {MemOp});

    // Add the thread pointer.
    SDValue TPReg = DAG.getRegister(RISCV::X4, XLenVT);
    return DAG.getNode(ISD::ADD, DL, Ty, Load, TPReg);
  }

  // Generate a sequence for accessing the address relative to the thread
  // pointer, with the appropriate adjustment for the thread pointer offset.
  // This generates the pattern
  // (add (add_tprel (lui %tprel_hi(sym)) tp %tprel_add(sym)) %tprel_lo(sym))
  SDValue AddrHi =
      DAG.getTargetGlobalAddress(GV, DL, Ty, 0, RISCVII::MO_TPREL_HI);
  SDValue AddrAdd =
      DAG.getTargetGlobalAddress(GV, DL, Ty, 0, RISCVII::MO_TPREL_ADD);
  SDValue AddrLo =
      DAG.getTargetGlobalAddress(GV, DL, Ty, 0, RISCVII::MO_TPREL_LO);

  SDValue MNHi = DAG.getNode(RISCVISD::HI, DL, Ty, AddrHi);
  SDValue TPReg = DAG.getRegister(RISCV::X4, XLenVT);
  SDValue MNAdd =
      DAG.getNode(RISCVISD::ADD_TPREL, DL, Ty, MNHi, TPReg, AddrAdd);
  return DAG.getNode(RISCVISD::ADD_LO, DL, Ty, MNAdd, AddrLo);
}

SDValue RISCVTargetLowering::getDynamicTLSAddr(GlobalAddressSDNode *N,
                                               SelectionDAG &DAG) const {
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  IntegerType *CallTy = Type::getIntNTy(*DAG.getContext(), Ty.getSizeInBits());
  const GlobalValue *GV = N->getGlobal();

  // Use a PC-relative addressing mode to access the global dynamic GOT address.
  // This generates the pattern (PseudoLA_TLS_GD sym), which expands to
  // (addi (auipc %tls_gd_pcrel_hi(sym)) %pcrel_lo(auipc)).
  SDValue Addr = DAG.getTargetGlobalAddress(GV, DL, Ty, 0, 0);
  SDValue Load =
      SDValue(DAG.getMachineNode(RISCV::PseudoLA_TLS_GD, DL, Ty, Addr), 0);

  // Prepare argument list to generate call.
  ArgListTy Args;
  ArgListEntry Entry;
  Entry.Node = Load;
  Entry.Ty = CallTy;
  Args.push_back(Entry);

  // Setup call to __tls_get_addr.
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, CallTy,
                    DAG.getExternalSymbol("__tls_get_addr", Ty),
                    std::move(Args));

  return LowerCallTo(CLI).first;
}

SDValue RISCVTargetLowering::getTLSDescAddr(GlobalAddressSDNode *N,
                                            SelectionDAG &DAG) const {
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  const GlobalValue *GV = N->getGlobal();

  // Use a PC-relative addressing mode to access the global dynamic GOT address.
  // This generates the pattern (PseudoLA_TLSDESC sym), which expands to
  //
  // auipc tX, %tlsdesc_hi(symbol)         // R_RISCV_TLSDESC_HI20(symbol)
  // lw    tY, tX, %tlsdesc_load_lo(label) // R_RISCV_TLSDESC_LOAD_LO12(label)
  // addi  a0, tX, %tlsdesc_add_lo(label)  // R_RISCV_TLSDESC_ADD_LO12(label)
  // jalr  t0, tY                          // R_RISCV_TLSDESC_CALL(label)
  SDValue Addr = DAG.getTargetGlobalAddress(GV, DL, Ty, 0, 0);
  return SDValue(DAG.getMachineNode(RISCV::PseudoLA_TLSDESC, DL, Ty, Addr), 0);
}

SDValue RISCVTargetLowering::lowerGlobalTLSAddress(SDValue Op,
                                                   SelectionDAG &DAG) const {
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  assert(N->getOffset() == 0 && "unexpected offset in global node");

  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(N, DAG);

  TLSModel::Model Model = getTargetMachine().getTLSModel(N->getGlobal());

  if (DAG.getMachineFunction().getFunction().getCallingConv() ==
      CallingConv::GHC)
    report_fatal_error("In GHC calling convention TLS is not supported");

  SDValue Addr;
  switch (Model) {
  case TLSModel::LocalExec:
    Addr = getStaticTLSAddr(N, DAG, /*UseGOT=*/false);
    break;
  case TLSModel::InitialExec:
    Addr = getStaticTLSAddr(N, DAG, /*UseGOT=*/true);
    break;
  case TLSModel::LocalDynamic:
  case TLSModel::GeneralDynamic:
    Addr = DAG.getTarget().useTLSDESC() ? getTLSDescAddr(N, DAG)
                                        : getDynamicTLSAddr(N, DAG);
    break;
  }

  return Addr;
}

// Return true if Val is equal to (setcc LHS, RHS, CC).
// Return false if Val is the inverse of (setcc LHS, RHS, CC).
// Otherwise, return std::nullopt.
static std::optional<bool> matchSetCC(SDValue LHS, SDValue RHS,
                                      ISD::CondCode CC, SDValue Val) {
  assert(Val->getOpcode() == ISD::SETCC);
  SDValue LHS2 = Val.getOperand(0);
  SDValue RHS2 = Val.getOperand(1);
  ISD::CondCode CC2 = cast<CondCodeSDNode>(Val.getOperand(2))->get();

  if (LHS == LHS2 && RHS == RHS2) {
    if (CC == CC2)
      return true;
    if (CC == ISD::getSetCCInverse(CC2, LHS2.getValueType()))
      return false;
  } else if (LHS == RHS2 && RHS == LHS2) {
    CC2 = ISD::getSetCCSwappedOperands(CC2);
    if (CC == CC2)
      return true;
    if (CC == ISD::getSetCCInverse(CC2, LHS2.getValueType()))
      return false;
  }

  return std::nullopt;
}

static SDValue combineSelectToBinOp(SDNode *N, SelectionDAG &DAG,
                                    const RISCVSubtarget &Subtarget) {
  SDValue CondV = N->getOperand(0);
  SDValue TrueV = N->getOperand(1);
  SDValue FalseV = N->getOperand(2);
  MVT VT = N->getSimpleValueType(0);
  SDLoc DL(N);

  if (!Subtarget.hasConditionalMoveFusion()) {
    // (select c, -1, y) -> -c | y
    if (isAllOnesConstant(TrueV)) {
      SDValue Neg = DAG.getNegative(CondV, DL, VT);
      return DAG.getNode(ISD::OR, DL, VT, Neg, DAG.getFreeze(FalseV));
    }
    // (select c, y, -1) -> (c-1) | y
    if (isAllOnesConstant(FalseV)) {
      SDValue Neg = DAG.getNode(ISD::ADD, DL, VT, CondV,
                                DAG.getAllOnesConstant(DL, VT));
      return DAG.getNode(ISD::OR, DL, VT, Neg, DAG.getFreeze(TrueV));
    }

    // (select c, 0, y) -> (c-1) & y
    if (isNullConstant(TrueV)) {
      SDValue Neg = DAG.getNode(ISD::ADD, DL, VT, CondV,
                                DAG.getAllOnesConstant(DL, VT));
      return DAG.getNode(ISD::AND, DL, VT, Neg, DAG.getFreeze(FalseV));
    }
    // (select c, y, 0) -> -c & y
    if (isNullConstant(FalseV)) {
      SDValue Neg = DAG.getNegative(CondV, DL, VT);
      return DAG.getNode(ISD::AND, DL, VT, Neg, DAG.getFreeze(TrueV));
    }
  }

  // select c, ~x, x --> xor -c, x
  if (isa<ConstantSDNode>(TrueV) && isa<ConstantSDNode>(FalseV)) {
    const APInt &TrueVal = TrueV->getAsAPIntVal();
    const APInt &FalseVal = FalseV->getAsAPIntVal();
    if (~TrueVal == FalseVal) {
      SDValue Neg = DAG.getNegative(CondV, DL, VT);
      return DAG.getNode(ISD::XOR, DL, VT, Neg, FalseV);
    }
  }

  // Try to fold (select (setcc lhs, rhs, cc), truev, falsev) into bitwise ops
  // when both truev and falsev are also setcc.
  if (CondV.getOpcode() == ISD::SETCC && TrueV.getOpcode() == ISD::SETCC &&
      FalseV.getOpcode() == ISD::SETCC) {
    SDValue LHS = CondV.getOperand(0);
    SDValue RHS = CondV.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(CondV.getOperand(2))->get();

    // (select x, x, y) -> x | y
    // (select !x, x, y) -> x & y
    if (std::optional<bool> MatchResult = matchSetCC(LHS, RHS, CC, TrueV)) {
      return DAG.getNode(*MatchResult ? ISD::OR : ISD::AND, DL, VT, TrueV,
                         DAG.getFreeze(FalseV));
    }
    // (select x, y, x) -> x & y
    // (select !x, y, x) -> x | y
    if (std::optional<bool> MatchResult = matchSetCC(LHS, RHS, CC, FalseV)) {
      return DAG.getNode(*MatchResult ? ISD::AND : ISD::OR, DL, VT,
                         DAG.getFreeze(TrueV), FalseV);
    }
  }

  return SDValue();
}

// Transform `binOp (select cond, x, c0), c1` where `c0` and `c1` are constants
// into `select cond, binOp(x, c1), binOp(c0, c1)` if profitable.
// For now we only consider transformation profitable if `binOp(c0, c1)` ends up
// being `0` or `-1`. In such cases we can replace `select` with `and`.
// TODO: Should we also do this if `binOp(c0, c1)` is cheaper to materialize
// than `c0`?
static SDValue
foldBinOpIntoSelectIfProfitable(SDNode *BO, SelectionDAG &DAG,
                                const RISCVSubtarget &Subtarget) {
  if (Subtarget.hasShortForwardBranchOpt())
    return SDValue();

  unsigned SelOpNo = 0;
  SDValue Sel = BO->getOperand(0);
  if (Sel.getOpcode() != ISD::SELECT || !Sel.hasOneUse()) {
    SelOpNo = 1;
    Sel = BO->getOperand(1);
  }

  if (Sel.getOpcode() != ISD::SELECT || !Sel.hasOneUse())
    return SDValue();

  unsigned ConstSelOpNo = 1;
  unsigned OtherSelOpNo = 2;
  if (!dyn_cast<ConstantSDNode>(Sel->getOperand(ConstSelOpNo))) {
    ConstSelOpNo = 2;
    OtherSelOpNo = 1;
  }
  SDValue ConstSelOp = Sel->getOperand(ConstSelOpNo);
  ConstantSDNode *ConstSelOpNode = dyn_cast<ConstantSDNode>(ConstSelOp);
  if (!ConstSelOpNode || ConstSelOpNode->isOpaque())
    return SDValue();

  SDValue ConstBinOp = BO->getOperand(SelOpNo ^ 1);
  ConstantSDNode *ConstBinOpNode = dyn_cast<ConstantSDNode>(ConstBinOp);
  if (!ConstBinOpNode || ConstBinOpNode->isOpaque())
    return SDValue();

  SDLoc DL(Sel);
  EVT VT = BO->getValueType(0);

  SDValue NewConstOps[2] = {ConstSelOp, ConstBinOp};
  if (SelOpNo == 1)
    std::swap(NewConstOps[0], NewConstOps[1]);

  SDValue NewConstOp =
      DAG.FoldConstantArithmetic(BO->getOpcode(), DL, VT, NewConstOps);
  if (!NewConstOp)
    return SDValue();

  const APInt &NewConstAPInt = NewConstOp->getAsAPIntVal();
  if (!NewConstAPInt.isZero() && !NewConstAPInt.isAllOnes())
    return SDValue();

  SDValue OtherSelOp = Sel->getOperand(OtherSelOpNo);
  SDValue NewNonConstOps[2] = {OtherSelOp, ConstBinOp};
  if (SelOpNo == 1)
    std::swap(NewNonConstOps[0], NewNonConstOps[1]);
  SDValue NewNonConstOp = DAG.getNode(BO->getOpcode(), DL, VT, NewNonConstOps);

  SDValue NewT = (ConstSelOpNo == 1) ? NewConstOp : NewNonConstOp;
  SDValue NewF = (ConstSelOpNo == 1) ? NewNonConstOp : NewConstOp;
  return DAG.getSelect(DL, VT, Sel.getOperand(0), NewT, NewF);
}

SDValue RISCVTargetLowering::lowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDValue CondV = Op.getOperand(0);
  SDValue TrueV = Op.getOperand(1);
  SDValue FalseV = Op.getOperand(2);
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  // Lower vector SELECTs to VSELECTs by splatting the condition.
  if (VT.isVector()) {
    MVT SplatCondVT = VT.changeVectorElementType(MVT::i1);
    SDValue CondSplat = DAG.getSplat(SplatCondVT, DL, CondV);
    return DAG.getNode(ISD::VSELECT, DL, VT, CondSplat, TrueV, FalseV);
  }

  // When Zicond or XVentanaCondOps is present, emit CZERO_EQZ and CZERO_NEZ
  // nodes to implement the SELECT. Performing the lowering here allows for
  // greater control over when CZERO_{EQZ/NEZ} are used vs another branchless
  // sequence or RISCVISD::SELECT_CC node (branch-based select).
  if ((Subtarget.hasStdExtZicond() || Subtarget.hasVendorXVentanaCondOps()) &&
      VT.isScalarInteger()) {
    // (select c, t, 0) -> (czero_eqz t, c)
    if (isNullConstant(FalseV))
      return DAG.getNode(RISCVISD::CZERO_EQZ, DL, VT, TrueV, CondV);
    // (select c, 0, f) -> (czero_nez f, c)
    if (isNullConstant(TrueV))
      return DAG.getNode(RISCVISD::CZERO_NEZ, DL, VT, FalseV, CondV);

    // (select c, (and f, x), f) -> (or (and f, x), (czero_nez f, c))
    if (TrueV.getOpcode() == ISD::AND &&
        (TrueV.getOperand(0) == FalseV || TrueV.getOperand(1) == FalseV))
      return DAG.getNode(
          ISD::OR, DL, VT, TrueV,
          DAG.getNode(RISCVISD::CZERO_NEZ, DL, VT, FalseV, CondV));
    // (select c, t, (and t, x)) -> (or (czero_eqz t, c), (and t, x))
    if (FalseV.getOpcode() == ISD::AND &&
        (FalseV.getOperand(0) == TrueV || FalseV.getOperand(1) == TrueV))
      return DAG.getNode(
          ISD::OR, DL, VT, FalseV,
          DAG.getNode(RISCVISD::CZERO_EQZ, DL, VT, TrueV, CondV));

    // Try some other optimizations before falling back to generic lowering.
    if (SDValue V = combineSelectToBinOp(Op.getNode(), DAG, Subtarget))
      return V;

    // (select c, c1, c2) -> (add (czero_nez c2 - c1, c), c1)
    // (select c, c1, c2) -> (add (czero_eqz c1 - c2, c), c2)
    if (isa<ConstantSDNode>(TrueV) && isa<ConstantSDNode>(FalseV)) {
      const APInt &TrueVal = TrueV->getAsAPIntVal();
      const APInt &FalseVal = FalseV->getAsAPIntVal();
      const int TrueValCost = RISCVMatInt::getIntMatCost(
          TrueVal, Subtarget.getXLen(), Subtarget, /*CompressionCost=*/true);
      const int FalseValCost = RISCVMatInt::getIntMatCost(
          FalseVal, Subtarget.getXLen(), Subtarget, /*CompressionCost=*/true);
      bool IsCZERO_NEZ = TrueValCost <= FalseValCost;
      SDValue LHSVal = DAG.getConstant(
          IsCZERO_NEZ ? FalseVal - TrueVal : TrueVal - FalseVal, DL, VT);
      SDValue RHSVal =
          DAG.getConstant(IsCZERO_NEZ ? TrueVal : FalseVal, DL, VT);
      SDValue CMOV =
          DAG.getNode(IsCZERO_NEZ ? RISCVISD::CZERO_NEZ : RISCVISD::CZERO_EQZ,
                      DL, VT, LHSVal, CondV);
      return DAG.getNode(ISD::ADD, DL, VT, CMOV, RHSVal);
    }

    // (select c, t, f) -> (or (czero_eqz t, c), (czero_nez f, c))
    // Unless we have the short forward branch optimization.
    if (!Subtarget.hasConditionalMoveFusion())
      return DAG.getNode(
          ISD::OR, DL, VT,
          DAG.getNode(RISCVISD::CZERO_EQZ, DL, VT, TrueV, CondV),
          DAG.getNode(RISCVISD::CZERO_NEZ, DL, VT, FalseV, CondV));
  }

  if (SDValue V = combineSelectToBinOp(Op.getNode(), DAG, Subtarget))
    return V;

  if (Op.hasOneUse()) {
    unsigned UseOpc = Op->use_begin()->getOpcode();
    if (isBinOp(UseOpc) && DAG.isSafeToSpeculativelyExecute(UseOpc)) {
      SDNode *BinOp = *Op->use_begin();
      if (SDValue NewSel = foldBinOpIntoSelectIfProfitable(*Op->use_begin(),
                                                           DAG, Subtarget)) {
        DAG.ReplaceAllUsesWith(BinOp, &NewSel);
        // Opcode check is necessary because foldBinOpIntoSelectIfProfitable
        // may return a constant node and cause crash in lowerSELECT.
        if (NewSel.getOpcode() == ISD::SELECT)
          return lowerSELECT(NewSel, DAG);
        return NewSel;
      }
    }
  }

  // (select cc, 1.0, 0.0) -> (sint_to_fp (zext cc))
  // (select cc, 0.0, 1.0) -> (sint_to_fp (zext (xor cc, 1)))
  const ConstantFPSDNode *FPTV = dyn_cast<ConstantFPSDNode>(TrueV);
  const ConstantFPSDNode *FPFV = dyn_cast<ConstantFPSDNode>(FalseV);
  if (FPTV && FPFV) {
    if (FPTV->isExactlyValue(1.0) && FPFV->isExactlyValue(0.0))
      return DAG.getNode(ISD::SINT_TO_FP, DL, VT, CondV);
    if (FPTV->isExactlyValue(0.0) && FPFV->isExactlyValue(1.0)) {
      SDValue XOR = DAG.getNode(ISD::XOR, DL, XLenVT, CondV,
                                DAG.getConstant(1, DL, XLenVT));
      return DAG.getNode(ISD::SINT_TO_FP, DL, VT, XOR);
    }
  }

  // If the condition is not an integer SETCC which operates on XLenVT, we need
  // to emit a RISCVISD::SELECT_CC comparing the condition to zero. i.e.:
  // (select condv, truev, falsev)
  // -> (riscvisd::select_cc condv, zero, setne, truev, falsev)
  if (CondV.getOpcode() != ISD::SETCC ||
      CondV.getOperand(0).getSimpleValueType() != XLenVT) {
    SDValue Zero = DAG.getConstant(0, DL, XLenVT);
    SDValue SetNE = DAG.getCondCode(ISD::SETNE);

    SDValue Ops[] = {CondV, Zero, SetNE, TrueV, FalseV};

    return DAG.getNode(RISCVISD::SELECT_CC, DL, VT, Ops);
  }

  // If the CondV is the output of a SETCC node which operates on XLenVT inputs,
  // then merge the SETCC node into the lowered RISCVISD::SELECT_CC to take
  // advantage of the integer compare+branch instructions. i.e.:
  // (select (setcc lhs, rhs, cc), truev, falsev)
  // -> (riscvisd::select_cc lhs, rhs, cc, truev, falsev)
  SDValue LHS = CondV.getOperand(0);
  SDValue RHS = CondV.getOperand(1);
  ISD::CondCode CCVal = cast<CondCodeSDNode>(CondV.getOperand(2))->get();

  // Special case for a select of 2 constants that have a diffence of 1.
  // Normally this is done by DAGCombine, but if the select is introduced by
  // type legalization or op legalization, we miss it. Restricting to SETLT
  // case for now because that is what signed saturating add/sub need.
  // FIXME: We don't need the condition to be SETLT or even a SETCC,
  // but we would probably want to swap the true/false values if the condition
  // is SETGE/SETLE to avoid an XORI.
  if (isa<ConstantSDNode>(TrueV) && isa<ConstantSDNode>(FalseV) &&
      CCVal == ISD::SETLT) {
    const APInt &TrueVal = TrueV->getAsAPIntVal();
    const APInt &FalseVal = FalseV->getAsAPIntVal();
    if (TrueVal - 1 == FalseVal)
      return DAG.getNode(ISD::ADD, DL, VT, CondV, FalseV);
    if (TrueVal + 1 == FalseVal)
      return DAG.getNode(ISD::SUB, DL, VT, FalseV, CondV);
  }

  translateSetCCForBranch(DL, LHS, RHS, CCVal, DAG);
  // 1 < x ? x : 1 -> 0 < x ? x : 1
  if (isOneConstant(LHS) && (CCVal == ISD::SETLT || CCVal == ISD::SETULT) &&
      RHS == TrueV && LHS == FalseV) {
    LHS = DAG.getConstant(0, DL, VT);
    // 0 <u x is the same as x != 0.
    if (CCVal == ISD::SETULT) {
      std::swap(LHS, RHS);
      CCVal = ISD::SETNE;
    }
  }

  // x <s -1 ? x : -1 -> x <s 0 ? x : -1
  if (isAllOnesConstant(RHS) && CCVal == ISD::SETLT && LHS == TrueV &&
      RHS == FalseV) {
    RHS = DAG.getConstant(0, DL, VT);
  }

  SDValue TargetCC = DAG.getCondCode(CCVal);

  if (isa<ConstantSDNode>(TrueV) && !isa<ConstantSDNode>(FalseV)) {
    // (select (setcc lhs, rhs, CC), constant, falsev)
    // -> (select (setcc lhs, rhs, InverseCC), falsev, constant)
    std::swap(TrueV, FalseV);
    TargetCC = DAG.getCondCode(ISD::getSetCCInverse(CCVal, LHS.getValueType()));
  }

  SDValue Ops[] = {LHS, RHS, TargetCC, TrueV, FalseV};
  return DAG.getNode(RISCVISD::SELECT_CC, DL, VT, Ops);
}

SDValue RISCVTargetLowering::lowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  SDValue CondV = Op.getOperand(1);
  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();

  if (CondV.getOpcode() == ISD::SETCC &&
      CondV.getOperand(0).getValueType() == XLenVT) {
    SDValue LHS = CondV.getOperand(0);
    SDValue RHS = CondV.getOperand(1);
    ISD::CondCode CCVal = cast<CondCodeSDNode>(CondV.getOperand(2))->get();

    translateSetCCForBranch(DL, LHS, RHS, CCVal, DAG);

    SDValue TargetCC = DAG.getCondCode(CCVal);
    return DAG.getNode(RISCVISD::BR_CC, DL, Op.getValueType(), Op.getOperand(0),
                       LHS, RHS, TargetCC, Op.getOperand(2));
  }

  return DAG.getNode(RISCVISD::BR_CC, DL, Op.getValueType(), Op.getOperand(0),
                     CondV, DAG.getConstant(0, DL, XLenVT),
                     DAG.getCondCode(ISD::SETNE), Op.getOperand(2));
}

SDValue RISCVTargetLowering::lowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  RISCVMachineFunctionInfo *FuncInfo = MF.getInfo<RISCVMachineFunctionInfo>();

  SDLoc DL(Op);
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                 getPointerTy(MF.getDataLayout()));

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FI, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue RISCVTargetLowering::lowerFRAMEADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  const RISCVRegisterInfo &RI = *Subtarget.getRegisterInfo();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);
  Register FrameReg = RI.getFrameRegister(MF);
  int XLenInBytes = Subtarget.getXLen() / 8;

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), DL, FrameReg, VT);
  unsigned Depth = Op.getConstantOperandVal(0);
  while (Depth--) {
    int Offset = -(XLenInBytes * 2);
    SDValue Ptr = DAG.getNode(ISD::ADD, DL, VT, FrameAddr,
                              DAG.getIntPtrConstant(Offset, DL));
    FrameAddr =
        DAG.getLoad(VT, DL, DAG.getEntryNode(), Ptr, MachinePointerInfo());
  }
  return FrameAddr;
}

SDValue RISCVTargetLowering::lowerRETURNADDR(SDValue Op,
                                             SelectionDAG &DAG) const {
  const RISCVRegisterInfo &RI = *Subtarget.getRegisterInfo();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);
  MVT XLenVT = Subtarget.getXLenVT();
  int XLenInBytes = Subtarget.getXLen() / 8;

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Depth = Op.getConstantOperandVal(0);
  if (Depth) {
    int Off = -XLenInBytes;
    SDValue FrameAddr = lowerFRAMEADDR(Op, DAG);
    SDValue Offset = DAG.getConstant(Off, DL, VT);
    return DAG.getLoad(VT, DL, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, DL, VT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Return the value of the return address register, marking it an implicit
  // live-in.
  Register Reg = MF.addLiveIn(RI.getRARegister(), getRegClassFor(XLenVT));
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, XLenVT);
}

SDValue RISCVTargetLowering::lowerShiftLeftParts(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);
  EVT VT = Lo.getValueType();

  // if Shamt-XLEN < 0: // Shamt < XLEN
  //   Lo = Lo << Shamt
  //   Hi = (Hi << Shamt) | ((Lo >>u 1) >>u (XLEN-1 - Shamt))
  // else:
  //   Lo = 0
  //   Hi = Lo << (Shamt-XLEN)

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue MinusXLen = DAG.getConstant(-(int)Subtarget.getXLen(), DL, VT);
  SDValue XLenMinus1 = DAG.getConstant(Subtarget.getXLen() - 1, DL, VT);
  SDValue ShamtMinusXLen = DAG.getNode(ISD::ADD, DL, VT, Shamt, MinusXLen);
  SDValue XLenMinus1Shamt = DAG.getNode(ISD::SUB, DL, VT, XLenMinus1, Shamt);

  SDValue LoTrue = DAG.getNode(ISD::SHL, DL, VT, Lo, Shamt);
  SDValue ShiftRight1Lo = DAG.getNode(ISD::SRL, DL, VT, Lo, One);
  SDValue ShiftRightLo =
      DAG.getNode(ISD::SRL, DL, VT, ShiftRight1Lo, XLenMinus1Shamt);
  SDValue ShiftLeftHi = DAG.getNode(ISD::SHL, DL, VT, Hi, Shamt);
  SDValue HiTrue = DAG.getNode(ISD::OR, DL, VT, ShiftLeftHi, ShiftRightLo);
  SDValue HiFalse = DAG.getNode(ISD::SHL, DL, VT, Lo, ShamtMinusXLen);

  SDValue CC = DAG.getSetCC(DL, VT, ShamtMinusXLen, Zero, ISD::SETLT);

  Lo = DAG.getNode(ISD::SELECT, DL, VT, CC, LoTrue, Zero);
  Hi = DAG.getNode(ISD::SELECT, DL, VT, CC, HiTrue, HiFalse);

  SDValue Parts[2] = {Lo, Hi};
  return DAG.getMergeValues(Parts, DL);
}

SDValue RISCVTargetLowering::lowerShiftRightParts(SDValue Op, SelectionDAG &DAG,
                                                  bool IsSRA) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);
  EVT VT = Lo.getValueType();

  // SRA expansion:
  //   if Shamt-XLEN < 0: // Shamt < XLEN
  //     Lo = (Lo >>u Shamt) | ((Hi << 1) << (XLEN-1 - ShAmt))
  //     Hi = Hi >>s Shamt
  //   else:
  //     Lo = Hi >>s (Shamt-XLEN);
  //     Hi = Hi >>s (XLEN-1)
  //
  // SRL expansion:
  //   if Shamt-XLEN < 0: // Shamt < XLEN
  //     Lo = (Lo >>u Shamt) | ((Hi << 1) << (XLEN-1 - ShAmt))
  //     Hi = Hi >>u Shamt
  //   else:
  //     Lo = Hi >>u (Shamt-XLEN);
  //     Hi = 0;

  unsigned ShiftRightOp = IsSRA ? ISD::SRA : ISD::SRL;

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue MinusXLen = DAG.getConstant(-(int)Subtarget.getXLen(), DL, VT);
  SDValue XLenMinus1 = DAG.getConstant(Subtarget.getXLen() - 1, DL, VT);
  SDValue ShamtMinusXLen = DAG.getNode(ISD::ADD, DL, VT, Shamt, MinusXLen);
  SDValue XLenMinus1Shamt = DAG.getNode(ISD::SUB, DL, VT, XLenMinus1, Shamt);

  SDValue ShiftRightLo = DAG.getNode(ISD::SRL, DL, VT, Lo, Shamt);
  SDValue ShiftLeftHi1 = DAG.getNode(ISD::SHL, DL, VT, Hi, One);
  SDValue ShiftLeftHi =
      DAG.getNode(ISD::SHL, DL, VT, ShiftLeftHi1, XLenMinus1Shamt);
  SDValue LoTrue = DAG.getNode(ISD::OR, DL, VT, ShiftRightLo, ShiftLeftHi);
  SDValue HiTrue = DAG.getNode(ShiftRightOp, DL, VT, Hi, Shamt);
  SDValue LoFalse = DAG.getNode(ShiftRightOp, DL, VT, Hi, ShamtMinusXLen);
  SDValue HiFalse =
      IsSRA ? DAG.getNode(ISD::SRA, DL, VT, Hi, XLenMinus1) : Zero;

  SDValue CC = DAG.getSetCC(DL, VT, ShamtMinusXLen, Zero, ISD::SETLT);

  Lo = DAG.getNode(ISD::SELECT, DL, VT, CC, LoTrue, LoFalse);
  Hi = DAG.getNode(ISD::SELECT, DL, VT, CC, HiTrue, HiFalse);

  SDValue Parts[2] = {Lo, Hi};
  return DAG.getMergeValues(Parts, DL);
}

// Lower splats of i1 types to SETCC. For each mask vector type, we have a
// legal equivalently-sized i8 type, so we can use that as a go-between.
SDValue RISCVTargetLowering::lowerVectorMaskSplat(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue SplatVal = Op.getOperand(0);
  // All-zeros or all-ones splats are handled specially.
  if (ISD::isConstantSplatVectorAllOnes(Op.getNode())) {
    SDValue VL = getDefaultScalableVLOps(VT, DL, DAG, Subtarget).second;
    return DAG.getNode(RISCVISD::VMSET_VL, DL, VT, VL);
  }
  if (ISD::isConstantSplatVectorAllZeros(Op.getNode())) {
    SDValue VL = getDefaultScalableVLOps(VT, DL, DAG, Subtarget).second;
    return DAG.getNode(RISCVISD::VMCLR_VL, DL, VT, VL);
  }
  MVT InterVT = VT.changeVectorElementType(MVT::i8);
  SplatVal = DAG.getNode(ISD::AND, DL, SplatVal.getValueType(), SplatVal,
                         DAG.getConstant(1, DL, SplatVal.getValueType()));
  SDValue LHS = DAG.getSplatVector(InterVT, DL, SplatVal);
  SDValue Zero = DAG.getConstant(0, DL, InterVT);
  return DAG.getSetCC(DL, VT, LHS, Zero, ISD::SETNE);
}

// Custom-lower a SPLAT_VECTOR_PARTS where XLEN<SEW, as the SEW element type is
// illegal (currently only vXi64 RV32).
// FIXME: We could also catch non-constant sign-extended i32 values and lower
// them to VMV_V_X_VL.
SDValue RISCVTargetLowering::lowerSPLAT_VECTOR_PARTS(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VecVT = Op.getSimpleValueType();
  assert(!Subtarget.is64Bit() && VecVT.getVectorElementType() == MVT::i64 &&
         "Unexpected SPLAT_VECTOR_PARTS lowering");

  assert(Op.getNumOperands() == 2 && "Unexpected number of operands!");
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);

  MVT ContainerVT = VecVT;
  if (VecVT.isFixedLengthVector())
    ContainerVT = getContainerForFixedLengthVector(VecVT);

  auto VL = getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget).second;

  SDValue Res =
      splatPartsI64WithVL(DL, ContainerVT, SDValue(), Lo, Hi, VL, DAG);

  if (VecVT.isFixedLengthVector())
    Res = convertFromScalableVector(VecVT, Res, DAG, Subtarget);

  return Res;
}

// Custom-lower extensions from mask vectors by using a vselect either with 1
// for zero/any-extension or -1 for sign-extension:
//   (vXiN = (s|z)ext vXi1:vmask) -> (vXiN = vselect vmask, (-1 or 1), 0)
// Note that any-extension is lowered identically to zero-extension.
SDValue RISCVTargetLowering::lowerVectorMaskExt(SDValue Op, SelectionDAG &DAG,
                                                int64_t ExtTrueVal) const {
  SDLoc DL(Op);
  MVT VecVT = Op.getSimpleValueType();
  SDValue Src = Op.getOperand(0);
  // Only custom-lower extensions from mask types
  assert(Src.getValueType().isVector() &&
         Src.getValueType().getVectorElementType() == MVT::i1);

  if (VecVT.isScalableVector()) {
    SDValue SplatZero = DAG.getConstant(0, DL, VecVT);
    SDValue SplatTrueVal = DAG.getConstant(ExtTrueVal, DL, VecVT);
    return DAG.getNode(ISD::VSELECT, DL, VecVT, Src, SplatTrueVal, SplatZero);
  }

  MVT ContainerVT = getContainerForFixedLengthVector(VecVT);
  MVT I1ContainerVT =
      MVT::getVectorVT(MVT::i1, ContainerVT.getVectorElementCount());

  SDValue CC = convertToScalableVector(I1ContainerVT, Src, DAG, Subtarget);

  SDValue VL = getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget).second;

  MVT XLenVT = Subtarget.getXLenVT();
  SDValue SplatZero = DAG.getConstant(0, DL, XLenVT);
  SDValue SplatTrueVal = DAG.getConstant(ExtTrueVal, DL, XLenVT);

  SplatZero = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                          DAG.getUNDEF(ContainerVT), SplatZero, VL);
  SplatTrueVal = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                             DAG.getUNDEF(ContainerVT), SplatTrueVal, VL);
  SDValue Select =
      DAG.getNode(RISCVISD::VMERGE_VL, DL, ContainerVT, CC, SplatTrueVal,
                  SplatZero, DAG.getUNDEF(ContainerVT), VL);

  return convertFromScalableVector(VecVT, Select, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerFixedLengthVectorExtendToRVV(
    SDValue Op, SelectionDAG &DAG, unsigned ExtendOpc) const {
  MVT ExtVT = Op.getSimpleValueType();
  // Only custom-lower extensions from fixed-length vector types.
  if (!ExtVT.isFixedLengthVector())
    return Op;
  MVT VT = Op.getOperand(0).getSimpleValueType();
  // Grab the canonical container type for the extended type. Infer the smaller
  // type from that to ensure the same number of vector elements, as we know
  // the LMUL will be sufficient to hold the smaller type.
  MVT ContainerExtVT = getContainerForFixedLengthVector(ExtVT);
  // Get the extended container type manually to ensure the same number of
  // vector elements between source and dest.
  MVT ContainerVT = MVT::getVectorVT(VT.getVectorElementType(),
                                     ContainerExtVT.getVectorElementCount());

  SDValue Op1 =
      convertToScalableVector(ContainerVT, Op.getOperand(0), DAG, Subtarget);

  SDLoc DL(Op);
  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  SDValue Ext = DAG.getNode(ExtendOpc, DL, ContainerExtVT, Op1, Mask, VL);

  return convertFromScalableVector(ExtVT, Ext, DAG, Subtarget);
}

// Custom-lower truncations from vectors to mask vectors by using a mask and a
// setcc operation:
//   (vXi1 = trunc vXiN vec) -> (vXi1 = setcc (and vec, 1), 0, ne)
SDValue RISCVTargetLowering::lowerVectorMaskTruncLike(SDValue Op,
                                                      SelectionDAG &DAG) const {
  bool IsVPTrunc = Op.getOpcode() == ISD::VP_TRUNCATE;
  SDLoc DL(Op);
  EVT MaskVT = Op.getValueType();
  // Only expect to custom-lower truncations to mask types
  assert(MaskVT.isVector() && MaskVT.getVectorElementType() == MVT::i1 &&
         "Unexpected type for vector mask lowering");
  SDValue Src = Op.getOperand(0);
  MVT VecVT = Src.getSimpleValueType();
  SDValue Mask, VL;
  if (IsVPTrunc) {
    Mask = Op.getOperand(1);
    VL = Op.getOperand(2);
  }
  // If this is a fixed vector, we need to convert it to a scalable vector.
  MVT ContainerVT = VecVT;

  if (VecVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VecVT);
    Src = convertToScalableVector(ContainerVT, Src, DAG, Subtarget);
    if (IsVPTrunc) {
      MVT MaskContainerVT =
          getContainerForFixedLengthVector(Mask.getSimpleValueType());
      Mask = convertToScalableVector(MaskContainerVT, Mask, DAG, Subtarget);
    }
  }

  if (!IsVPTrunc) {
    std::tie(Mask, VL) =
        getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget);
  }

  SDValue SplatOne = DAG.getConstant(1, DL, Subtarget.getXLenVT());
  SDValue SplatZero = DAG.getConstant(0, DL, Subtarget.getXLenVT());

  SplatOne = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                         DAG.getUNDEF(ContainerVT), SplatOne, VL);
  SplatZero = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                          DAG.getUNDEF(ContainerVT), SplatZero, VL);

  MVT MaskContainerVT = ContainerVT.changeVectorElementType(MVT::i1);
  SDValue Trunc = DAG.getNode(RISCVISD::AND_VL, DL, ContainerVT, Src, SplatOne,
                              DAG.getUNDEF(ContainerVT), Mask, VL);
  Trunc = DAG.getNode(RISCVISD::SETCC_VL, DL, MaskContainerVT,
                      {Trunc, SplatZero, DAG.getCondCode(ISD::SETNE),
                       DAG.getUNDEF(MaskContainerVT), Mask, VL});
  if (MaskVT.isFixedLengthVector())
    Trunc = convertFromScalableVector(MaskVT, Trunc, DAG, Subtarget);
  return Trunc;
}

SDValue RISCVTargetLowering::lowerVectorTruncLike(SDValue Op,
                                                  SelectionDAG &DAG) const {
  bool IsVPTrunc = Op.getOpcode() == ISD::VP_TRUNCATE;
  SDLoc DL(Op);

  MVT VT = Op.getSimpleValueType();
  // Only custom-lower vector truncates
  assert(VT.isVector() && "Unexpected type for vector truncate lowering");

  // Truncates to mask types are handled differently
  if (VT.getVectorElementType() == MVT::i1)
    return lowerVectorMaskTruncLike(Op, DAG);

  // RVV only has truncates which operate from SEW*2->SEW, so lower arbitrary
  // truncates as a series of "RISCVISD::TRUNCATE_VECTOR_VL" nodes which
  // truncate by one power of two at a time.
  MVT DstEltVT = VT.getVectorElementType();

  SDValue Src = Op.getOperand(0);
  MVT SrcVT = Src.getSimpleValueType();
  MVT SrcEltVT = SrcVT.getVectorElementType();

  assert(DstEltVT.bitsLT(SrcEltVT) && isPowerOf2_64(DstEltVT.getSizeInBits()) &&
         isPowerOf2_64(SrcEltVT.getSizeInBits()) &&
         "Unexpected vector truncate lowering");

  MVT ContainerVT = SrcVT;
  SDValue Mask, VL;
  if (IsVPTrunc) {
    Mask = Op.getOperand(1);
    VL = Op.getOperand(2);
  }
  if (SrcVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(SrcVT);
    Src = convertToScalableVector(ContainerVT, Src, DAG, Subtarget);
    if (IsVPTrunc) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }
  }

  SDValue Result = Src;
  if (!IsVPTrunc) {
    std::tie(Mask, VL) =
        getDefaultVLOps(SrcVT, ContainerVT, DL, DAG, Subtarget);
  }

  LLVMContext &Context = *DAG.getContext();
  const ElementCount Count = ContainerVT.getVectorElementCount();
  do {
    SrcEltVT = MVT::getIntegerVT(SrcEltVT.getSizeInBits() / 2);
    EVT ResultVT = EVT::getVectorVT(Context, SrcEltVT, Count);
    Result = DAG.getNode(RISCVISD::TRUNCATE_VECTOR_VL, DL, ResultVT, Result,
                         Mask, VL);
  } while (SrcEltVT != DstEltVT);

  if (SrcVT.isFixedLengthVector())
    Result = convertFromScalableVector(VT, Result, DAG, Subtarget);

  return Result;
}

SDValue
RISCVTargetLowering::lowerStrictFPExtendOrRoundLike(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Src = Op.getOperand(1);
  MVT VT = Op.getSimpleValueType();
  MVT SrcVT = Src.getSimpleValueType();
  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    MVT SrcContainerVT = getContainerForFixedLengthVector(SrcVT);
    ContainerVT =
        SrcContainerVT.changeVectorElementType(VT.getVectorElementType());
    Src = convertToScalableVector(SrcContainerVT, Src, DAG, Subtarget);
  }

  auto [Mask, VL] = getDefaultVLOps(SrcVT, ContainerVT, DL, DAG, Subtarget);

  // RVV can only widen/truncate fp to types double/half the size as the source.
  if ((VT.getVectorElementType() == MVT::f64 &&
       (SrcVT.getVectorElementType() == MVT::f16 ||
        SrcVT.getVectorElementType() == MVT::bf16)) ||
      ((VT.getVectorElementType() == MVT::f16 ||
        VT.getVectorElementType() == MVT::bf16) &&
       SrcVT.getVectorElementType() == MVT::f64)) {
    // For double rounding, the intermediate rounding should be round-to-odd.
    unsigned InterConvOpc = Op.getOpcode() == ISD::STRICT_FP_EXTEND
                                ? RISCVISD::STRICT_FP_EXTEND_VL
                                : RISCVISD::STRICT_VFNCVT_ROD_VL;
    MVT InterVT = ContainerVT.changeVectorElementType(MVT::f32);
    Src = DAG.getNode(InterConvOpc, DL, DAG.getVTList(InterVT, MVT::Other),
                      Chain, Src, Mask, VL);
    Chain = Src.getValue(1);
  }

  unsigned ConvOpc = Op.getOpcode() == ISD::STRICT_FP_EXTEND
                         ? RISCVISD::STRICT_FP_EXTEND_VL
                         : RISCVISD::STRICT_FP_ROUND_VL;
  SDValue Res = DAG.getNode(ConvOpc, DL, DAG.getVTList(ContainerVT, MVT::Other),
                            Chain, Src, Mask, VL);
  if (VT.isFixedLengthVector()) {
    // StrictFP operations have two result values. Their lowered result should
    // have same result count.
    SDValue SubVec = convertFromScalableVector(VT, Res, DAG, Subtarget);
    Res = DAG.getMergeValues({SubVec, Res.getValue(1)}, DL);
  }
  return Res;
}

SDValue
RISCVTargetLowering::lowerVectorFPExtendOrRoundLike(SDValue Op,
                                                    SelectionDAG &DAG) const {
  bool IsVP =
      Op.getOpcode() == ISD::VP_FP_ROUND || Op.getOpcode() == ISD::VP_FP_EXTEND;
  bool IsExtend =
      Op.getOpcode() == ISD::VP_FP_EXTEND || Op.getOpcode() == ISD::FP_EXTEND;
  // RVV can only do truncate fp to types half the size as the source. We
  // custom-lower f64->f16 rounds via RVV's round-to-odd float
  // conversion instruction.
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();

  assert(VT.isVector() && "Unexpected type for vector truncate lowering");

  SDValue Src = Op.getOperand(0);
  MVT SrcVT = Src.getSimpleValueType();

  bool IsDirectExtend =
      IsExtend && (VT.getVectorElementType() != MVT::f64 ||
                   (SrcVT.getVectorElementType() != MVT::f16 &&
                    SrcVT.getVectorElementType() != MVT::bf16));
  bool IsDirectTrunc = !IsExtend && ((VT.getVectorElementType() != MVT::f16 &&
                                      VT.getVectorElementType() != MVT::bf16) ||
                                     SrcVT.getVectorElementType() != MVT::f64);

  bool IsDirectConv = IsDirectExtend || IsDirectTrunc;

  // Prepare any fixed-length vector operands.
  MVT ContainerVT = VT;
  SDValue Mask, VL;
  if (IsVP) {
    Mask = Op.getOperand(1);
    VL = Op.getOperand(2);
  }
  if (VT.isFixedLengthVector()) {
    MVT SrcContainerVT = getContainerForFixedLengthVector(SrcVT);
    ContainerVT =
        SrcContainerVT.changeVectorElementType(VT.getVectorElementType());
    Src = convertToScalableVector(SrcContainerVT, Src, DAG, Subtarget);
    if (IsVP) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }
  }

  if (!IsVP)
    std::tie(Mask, VL) =
        getDefaultVLOps(SrcVT, ContainerVT, DL, DAG, Subtarget);

  unsigned ConvOpc = IsExtend ? RISCVISD::FP_EXTEND_VL : RISCVISD::FP_ROUND_VL;

  if (IsDirectConv) {
    Src = DAG.getNode(ConvOpc, DL, ContainerVT, Src, Mask, VL);
    if (VT.isFixedLengthVector())
      Src = convertFromScalableVector(VT, Src, DAG, Subtarget);
    return Src;
  }

  unsigned InterConvOpc =
      IsExtend ? RISCVISD::FP_EXTEND_VL : RISCVISD::VFNCVT_ROD_VL;

  MVT InterVT = ContainerVT.changeVectorElementType(MVT::f32);
  SDValue IntermediateConv =
      DAG.getNode(InterConvOpc, DL, InterVT, Src, Mask, VL);
  SDValue Result =
      DAG.getNode(ConvOpc, DL, ContainerVT, IntermediateConv, Mask, VL);
  if (VT.isFixedLengthVector())
    return convertFromScalableVector(VT, Result, DAG, Subtarget);
  return Result;
}

// Given a scalable vector type and an index into it, returns the type for the
// smallest subvector that the index fits in. This can be used to reduce LMUL
// for operations like vslidedown.
//
// E.g. With Zvl128b, index 3 in a nxv4i32 fits within the first nxv2i32.
static std::optional<MVT>
getSmallestVTForIndex(MVT VecVT, unsigned MaxIdx, SDLoc DL, SelectionDAG &DAG,
                      const RISCVSubtarget &Subtarget) {
  assert(VecVT.isScalableVector());
  const unsigned EltSize = VecVT.getScalarSizeInBits();
  const unsigned VectorBitsMin = Subtarget.getRealMinVLen();
  const unsigned MinVLMAX = VectorBitsMin / EltSize;
  MVT SmallerVT;
  if (MaxIdx < MinVLMAX)
    SmallerVT = getLMUL1VT(VecVT);
  else if (MaxIdx < MinVLMAX * 2)
    SmallerVT = getLMUL1VT(VecVT).getDoubleNumVectorElementsVT();
  else if (MaxIdx < MinVLMAX * 4)
    SmallerVT = getLMUL1VT(VecVT)
                    .getDoubleNumVectorElementsVT()
                    .getDoubleNumVectorElementsVT();
  if (!SmallerVT.isValid() || !VecVT.bitsGT(SmallerVT))
    return std::nullopt;
  return SmallerVT;
}

// Custom-legalize INSERT_VECTOR_ELT so that the value is inserted into the
// first position of a vector, and that vector is slid up to the insert index.
// By limiting the active vector length to index+1 and merging with the
// original vector (with an undisturbed tail policy for elements >= VL), we
// achieve the desired result of leaving all elements untouched except the one
// at VL-1, which is replaced with the desired value.
SDValue RISCVTargetLowering::lowerINSERT_VECTOR_ELT(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VecVT = Op.getSimpleValueType();
  SDValue Vec = Op.getOperand(0);
  SDValue Val = Op.getOperand(1);
  SDValue Idx = Op.getOperand(2);

  if (VecVT.getVectorElementType() == MVT::i1) {
    // FIXME: For now we just promote to an i8 vector and insert into that,
    // but this is probably not optimal.
    MVT WideVT = MVT::getVectorVT(MVT::i8, VecVT.getVectorElementCount());
    Vec = DAG.getNode(ISD::ZERO_EXTEND, DL, WideVT, Vec);
    Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, WideVT, Vec, Val, Idx);
    return DAG.getNode(ISD::TRUNCATE, DL, VecVT, Vec);
  }

  MVT ContainerVT = VecVT;
  // If the operand is a fixed-length vector, convert to a scalable one.
  if (VecVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VecVT);
    Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
  }

  // If we know the index we're going to insert at, we can shrink Vec so that
  // we're performing the scalar inserts and slideup on a smaller LMUL.
  MVT OrigContainerVT = ContainerVT;
  SDValue OrigVec = Vec;
  SDValue AlignedIdx;
  if (auto *IdxC = dyn_cast<ConstantSDNode>(Idx)) {
    const unsigned OrigIdx = IdxC->getZExtValue();
    // Do we know an upper bound on LMUL?
    if (auto ShrunkVT = getSmallestVTForIndex(ContainerVT, OrigIdx,
                                              DL, DAG, Subtarget)) {
      ContainerVT = *ShrunkVT;
      AlignedIdx = DAG.getVectorIdxConstant(0, DL);
    }

    // If we're compiling for an exact VLEN value, we can always perform
    // the insert in m1 as we can determine the register corresponding to
    // the index in the register group.
    const MVT M1VT = getLMUL1VT(ContainerVT);
    if (auto VLEN = Subtarget.getRealVLen();
        VLEN && ContainerVT.bitsGT(M1VT)) {
      EVT ElemVT = VecVT.getVectorElementType();
      unsigned ElemsPerVReg = *VLEN / ElemVT.getFixedSizeInBits();
      unsigned RemIdx = OrigIdx % ElemsPerVReg;
      unsigned SubRegIdx = OrigIdx / ElemsPerVReg;
      unsigned ExtractIdx =
          SubRegIdx * M1VT.getVectorElementCount().getKnownMinValue();
      AlignedIdx = DAG.getVectorIdxConstant(ExtractIdx, DL);
      Idx = DAG.getVectorIdxConstant(RemIdx, DL);
      ContainerVT = M1VT;
    }

    if (AlignedIdx)
      Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ContainerVT, Vec,
                        AlignedIdx);
  }

  MVT XLenVT = Subtarget.getXLenVT();

  bool IsLegalInsert = Subtarget.is64Bit() || Val.getValueType() != MVT::i64;
  // Even i64-element vectors on RV32 can be lowered without scalar
  // legalization if the most-significant 32 bits of the value are not affected
  // by the sign-extension of the lower 32 bits.
  // TODO: We could also catch sign extensions of a 32-bit value.
  if (!IsLegalInsert && isa<ConstantSDNode>(Val)) {
    const auto *CVal = cast<ConstantSDNode>(Val);
    if (isInt<32>(CVal->getSExtValue())) {
      IsLegalInsert = true;
      Val = DAG.getConstant(CVal->getSExtValue(), DL, MVT::i32);
    }
  }

  auto [Mask, VL] = getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget);

  SDValue ValInVec;

  if (IsLegalInsert) {
    unsigned Opc =
        VecVT.isFloatingPoint() ? RISCVISD::VFMV_S_F_VL : RISCVISD::VMV_S_X_VL;
    if (isNullConstant(Idx)) {
      if (!VecVT.isFloatingPoint())
        Val = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, Val);
      Vec = DAG.getNode(Opc, DL, ContainerVT, Vec, Val, VL);

      if (AlignedIdx)
        Vec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, OrigContainerVT, OrigVec,
                          Vec, AlignedIdx);
      if (!VecVT.isFixedLengthVector())
        return Vec;
      return convertFromScalableVector(VecVT, Vec, DAG, Subtarget);
    }
    ValInVec = lowerScalarInsert(Val, VL, ContainerVT, DL, DAG, Subtarget);
  } else {
    // On RV32, i64-element vectors must be specially handled to place the
    // value at element 0, by using two vslide1down instructions in sequence on
    // the i32 split lo/hi value. Use an equivalently-sized i32 vector for
    // this.
    SDValue ValLo, ValHi;
    std::tie(ValLo, ValHi) = DAG.SplitScalar(Val, DL, MVT::i32, MVT::i32);
    MVT I32ContainerVT =
        MVT::getVectorVT(MVT::i32, ContainerVT.getVectorElementCount() * 2);
    SDValue I32Mask =
        getDefaultScalableVLOps(I32ContainerVT, DL, DAG, Subtarget).first;
    // Limit the active VL to two.
    SDValue InsertI64VL = DAG.getConstant(2, DL, XLenVT);
    // If the Idx is 0 we can insert directly into the vector.
    if (isNullConstant(Idx)) {
      // First slide in the lo value, then the hi in above it. We use slide1down
      // to avoid the register group overlap constraint of vslide1up.
      ValInVec = DAG.getNode(RISCVISD::VSLIDE1DOWN_VL, DL, I32ContainerVT,
                             Vec, Vec, ValLo, I32Mask, InsertI64VL);
      // If the source vector is undef don't pass along the tail elements from
      // the previous slide1down.
      SDValue Tail = Vec.isUndef() ? Vec : ValInVec;
      ValInVec = DAG.getNode(RISCVISD::VSLIDE1DOWN_VL, DL, I32ContainerVT,
                             Tail, ValInVec, ValHi, I32Mask, InsertI64VL);
      // Bitcast back to the right container type.
      ValInVec = DAG.getBitcast(ContainerVT, ValInVec);

      if (AlignedIdx)
        ValInVec =
            DAG.getNode(ISD::INSERT_SUBVECTOR, DL, OrigContainerVT, OrigVec,
                        ValInVec, AlignedIdx);
      if (!VecVT.isFixedLengthVector())
        return ValInVec;
      return convertFromScalableVector(VecVT, ValInVec, DAG, Subtarget);
    }

    // First slide in the lo value, then the hi in above it. We use slide1down
    // to avoid the register group overlap constraint of vslide1up.
    ValInVec = DAG.getNode(RISCVISD::VSLIDE1DOWN_VL, DL, I32ContainerVT,
                           DAG.getUNDEF(I32ContainerVT),
                           DAG.getUNDEF(I32ContainerVT), ValLo,
                           I32Mask, InsertI64VL);
    ValInVec = DAG.getNode(RISCVISD::VSLIDE1DOWN_VL, DL, I32ContainerVT,
                           DAG.getUNDEF(I32ContainerVT), ValInVec, ValHi,
                           I32Mask, InsertI64VL);
    // Bitcast back to the right container type.
    ValInVec = DAG.getBitcast(ContainerVT, ValInVec);
  }

  // Now that the value is in a vector, slide it into position.
  SDValue InsertVL =
      DAG.getNode(ISD::ADD, DL, XLenVT, Idx, DAG.getConstant(1, DL, XLenVT));

  // Use tail agnostic policy if Idx is the last index of Vec.
  unsigned Policy = RISCVII::TAIL_UNDISTURBED_MASK_UNDISTURBED;
  if (VecVT.isFixedLengthVector() && isa<ConstantSDNode>(Idx) &&
      Idx->getAsZExtVal() + 1 == VecVT.getVectorNumElements())
    Policy = RISCVII::TAIL_AGNOSTIC;
  SDValue Slideup = getVSlideup(DAG, Subtarget, DL, ContainerVT, Vec, ValInVec,
                                Idx, Mask, InsertVL, Policy);

  if (AlignedIdx)
    Slideup = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, OrigContainerVT, OrigVec,
                          Slideup, AlignedIdx);
  if (!VecVT.isFixedLengthVector())
    return Slideup;
  return convertFromScalableVector(VecVT, Slideup, DAG, Subtarget);
}

// Custom-lower EXTRACT_VECTOR_ELT operations to slide the vector down, then
// extract the first element: (extractelt (slidedown vec, idx), 0). For integer
// types this is done using VMV_X_S to allow us to glean information about the
// sign bits of the result.
SDValue RISCVTargetLowering::lowerEXTRACT_VECTOR_ELT(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Idx = Op.getOperand(1);
  SDValue Vec = Op.getOperand(0);
  EVT EltVT = Op.getValueType();
  MVT VecVT = Vec.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  if (VecVT.getVectorElementType() == MVT::i1) {
    // Use vfirst.m to extract the first bit.
    if (isNullConstant(Idx)) {
      MVT ContainerVT = VecVT;
      if (VecVT.isFixedLengthVector()) {
        ContainerVT = getContainerForFixedLengthVector(VecVT);
        Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
      }
      auto [Mask, VL] = getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget);
      SDValue Vfirst =
          DAG.getNode(RISCVISD::VFIRST_VL, DL, XLenVT, Vec, Mask, VL);
      SDValue Res = DAG.getSetCC(DL, XLenVT, Vfirst,
                                 DAG.getConstant(0, DL, XLenVT), ISD::SETEQ);
      return DAG.getNode(ISD::TRUNCATE, DL, EltVT, Res);
    }
    if (VecVT.isFixedLengthVector()) {
      unsigned NumElts = VecVT.getVectorNumElements();
      if (NumElts >= 8) {
        MVT WideEltVT;
        unsigned WidenVecLen;
        SDValue ExtractElementIdx;
        SDValue ExtractBitIdx;
        unsigned MaxEEW = Subtarget.getELen();
        MVT LargestEltVT = MVT::getIntegerVT(
            std::min(MaxEEW, unsigned(XLenVT.getSizeInBits())));
        if (NumElts <= LargestEltVT.getSizeInBits()) {
          assert(isPowerOf2_32(NumElts) &&
                 "the number of elements should be power of 2");
          WideEltVT = MVT::getIntegerVT(NumElts);
          WidenVecLen = 1;
          ExtractElementIdx = DAG.getConstant(0, DL, XLenVT);
          ExtractBitIdx = Idx;
        } else {
          WideEltVT = LargestEltVT;
          WidenVecLen = NumElts / WideEltVT.getSizeInBits();
          // extract element index = index / element width
          ExtractElementIdx = DAG.getNode(
              ISD::SRL, DL, XLenVT, Idx,
              DAG.getConstant(Log2_64(WideEltVT.getSizeInBits()), DL, XLenVT));
          // mask bit index = index % element width
          ExtractBitIdx = DAG.getNode(
              ISD::AND, DL, XLenVT, Idx,
              DAG.getConstant(WideEltVT.getSizeInBits() - 1, DL, XLenVT));
        }
        MVT WideVT = MVT::getVectorVT(WideEltVT, WidenVecLen);
        Vec = DAG.getNode(ISD::BITCAST, DL, WideVT, Vec);
        SDValue ExtractElt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, XLenVT,
                                         Vec, ExtractElementIdx);
        // Extract the bit from GPR.
        SDValue ShiftRight =
            DAG.getNode(ISD::SRL, DL, XLenVT, ExtractElt, ExtractBitIdx);
        SDValue Res = DAG.getNode(ISD::AND, DL, XLenVT, ShiftRight,
                                  DAG.getConstant(1, DL, XLenVT));
        return DAG.getNode(ISD::TRUNCATE, DL, EltVT, Res);
      }
    }
    // Otherwise, promote to an i8 vector and extract from that.
    MVT WideVT = MVT::getVectorVT(MVT::i8, VecVT.getVectorElementCount());
    Vec = DAG.getNode(ISD::ZERO_EXTEND, DL, WideVT, Vec);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Vec, Idx);
  }

  // If this is a fixed vector, we need to convert it to a scalable vector.
  MVT ContainerVT = VecVT;
  if (VecVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VecVT);
    Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
  }

  // If we're compiling for an exact VLEN value and we have a known
  // constant index, we can always perform the extract in m1 (or
  // smaller) as we can determine the register corresponding to
  // the index in the register group.
  const auto VLen = Subtarget.getRealVLen();
  if (auto *IdxC = dyn_cast<ConstantSDNode>(Idx);
      IdxC && VLen && VecVT.getSizeInBits().getKnownMinValue() > *VLen) {
    MVT M1VT = getLMUL1VT(ContainerVT);
    unsigned OrigIdx = IdxC->getZExtValue();
    EVT ElemVT = VecVT.getVectorElementType();
    unsigned ElemsPerVReg = *VLen / ElemVT.getFixedSizeInBits();
    unsigned RemIdx = OrigIdx % ElemsPerVReg;
    unsigned SubRegIdx = OrigIdx / ElemsPerVReg;
    unsigned ExtractIdx =
      SubRegIdx * M1VT.getVectorElementCount().getKnownMinValue();
    Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, M1VT, Vec,
                      DAG.getVectorIdxConstant(ExtractIdx, DL));
    Idx = DAG.getVectorIdxConstant(RemIdx, DL);
    ContainerVT = M1VT;
  }

  // Reduce the LMUL of our slidedown and vmv.x.s to the smallest LMUL which
  // contains our index.
  std::optional<uint64_t> MaxIdx;
  if (VecVT.isFixedLengthVector())
    MaxIdx = VecVT.getVectorNumElements() - 1;
  if (auto *IdxC = dyn_cast<ConstantSDNode>(Idx))
    MaxIdx = IdxC->getZExtValue();
  if (MaxIdx) {
    if (auto SmallerVT =
            getSmallestVTForIndex(ContainerVT, *MaxIdx, DL, DAG, Subtarget)) {
      ContainerVT = *SmallerVT;
      Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ContainerVT, Vec,
                        DAG.getConstant(0, DL, XLenVT));
    }
  }

  // If after narrowing, the required slide is still greater than LMUL2,
  // fallback to generic expansion and go through the stack.  This is done
  // for a subtle reason: extracting *all* elements out of a vector is
  // widely expected to be linear in vector size, but because vslidedown
  // is linear in LMUL, performing N extracts using vslidedown becomes
  // O(n^2) / (VLEN/ETYPE) work.  On the surface, going through the stack
  // seems to have the same problem (the store is linear in LMUL), but the
  // generic expansion *memoizes* the store, and thus for many extracts of
  // the same vector we end up with one store and a bunch of loads.
  // TODO: We don't have the same code for insert_vector_elt because we
  // have BUILD_VECTOR and handle the degenerate case there.  Should we
  // consider adding an inverse BUILD_VECTOR node?
  MVT LMUL2VT = getLMUL1VT(ContainerVT).getDoubleNumVectorElementsVT();
  if (ContainerVT.bitsGT(LMUL2VT) && VecVT.isFixedLengthVector())
    return SDValue();

  // If the index is 0, the vector is already in the right position.
  if (!isNullConstant(Idx)) {
    // Use a VL of 1 to avoid processing more elements than we need.
    auto [Mask, VL] = getDefaultVLOps(1, ContainerVT, DL, DAG, Subtarget);
    Vec = getVSlidedown(DAG, Subtarget, DL, ContainerVT,
                        DAG.getUNDEF(ContainerVT), Vec, Idx, Mask, VL);
  }

  if (!EltVT.isInteger()) {
    // Floating-point extracts are handled in TableGen.
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Vec,
                       DAG.getVectorIdxConstant(0, DL));
  }

  SDValue Elt0 = DAG.getNode(RISCVISD::VMV_X_S, DL, XLenVT, Vec);
  return DAG.getNode(ISD::TRUNCATE, DL, EltVT, Elt0);
}

// Some RVV intrinsics may claim that they want an integer operand to be
// promoted or expanded.
static SDValue lowerVectorIntrinsicScalars(SDValue Op, SelectionDAG &DAG,
                                           const RISCVSubtarget &Subtarget) {
  assert((Op.getOpcode() == ISD::INTRINSIC_VOID ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN) &&
         "Unexpected opcode");

  if (!Subtarget.hasVInstructions())
    return SDValue();

  bool HasChain = Op.getOpcode() == ISD::INTRINSIC_VOID ||
                  Op.getOpcode() == ISD::INTRINSIC_W_CHAIN;
  unsigned IntNo = Op.getConstantOperandVal(HasChain ? 1 : 0);

  SDLoc DL(Op);

  const RISCVVIntrinsicsTable::RISCVVIntrinsicInfo *II =
      RISCVVIntrinsicsTable::getRISCVVIntrinsicInfo(IntNo);
  if (!II || !II->hasScalarOperand())
    return SDValue();

  unsigned SplatOp = II->ScalarOperand + 1 + HasChain;
  assert(SplatOp < Op.getNumOperands());

  SmallVector<SDValue, 8> Operands(Op->op_begin(), Op->op_end());
  SDValue &ScalarOp = Operands[SplatOp];
  MVT OpVT = ScalarOp.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  // If this isn't a scalar, or its type is XLenVT we're done.
  if (!OpVT.isScalarInteger() || OpVT == XLenVT)
    return SDValue();

  // Simplest case is that the operand needs to be promoted to XLenVT.
  if (OpVT.bitsLT(XLenVT)) {
    // If the operand is a constant, sign extend to increase our chances
    // of being able to use a .vi instruction. ANY_EXTEND would become a
    // a zero extend and the simm5 check in isel would fail.
    // FIXME: Should we ignore the upper bits in isel instead?
    unsigned ExtOpc =
        isa<ConstantSDNode>(ScalarOp) ? ISD::SIGN_EXTEND : ISD::ANY_EXTEND;
    ScalarOp = DAG.getNode(ExtOpc, DL, XLenVT, ScalarOp);
    return DAG.getNode(Op->getOpcode(), DL, Op->getVTList(), Operands);
  }

  // Use the previous operand to get the vXi64 VT. The result might be a mask
  // VT for compares. Using the previous operand assumes that the previous
  // operand will never have a smaller element size than a scalar operand and
  // that a widening operation never uses SEW=64.
  // NOTE: If this fails the below assert, we can probably just find the
  // element count from any operand or result and use it to construct the VT.
  assert(II->ScalarOperand > 0 && "Unexpected splat operand!");
  MVT VT = Op.getOperand(SplatOp - 1).getSimpleValueType();

  // The more complex case is when the scalar is larger than XLenVT.
  assert(XLenVT == MVT::i32 && OpVT == MVT::i64 &&
         VT.getVectorElementType() == MVT::i64 && "Unexpected VTs!");

  // If this is a sign-extended 32-bit value, we can truncate it and rely on the
  // instruction to sign-extend since SEW>XLEN.
  if (DAG.ComputeNumSignBits(ScalarOp) > 32) {
    ScalarOp = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, ScalarOp);
    return DAG.getNode(Op->getOpcode(), DL, Op->getVTList(), Operands);
  }

  switch (IntNo) {
  case Intrinsic::riscv_vslide1up:
  case Intrinsic::riscv_vslide1down:
  case Intrinsic::riscv_vslide1up_mask:
  case Intrinsic::riscv_vslide1down_mask: {
    // We need to special case these when the scalar is larger than XLen.
    unsigned NumOps = Op.getNumOperands();
    bool IsMasked = NumOps == 7;

    // Convert the vector source to the equivalent nxvXi32 vector.
    MVT I32VT = MVT::getVectorVT(MVT::i32, VT.getVectorElementCount() * 2);
    SDValue Vec = DAG.getBitcast(I32VT, Operands[2]);
    SDValue ScalarLo, ScalarHi;
    std::tie(ScalarLo, ScalarHi) =
        DAG.SplitScalar(ScalarOp, DL, MVT::i32, MVT::i32);

    // Double the VL since we halved SEW.
    SDValue AVL = getVLOperand(Op);
    SDValue I32VL;

    // Optimize for constant AVL
    if (isa<ConstantSDNode>(AVL)) {
      const auto [MinVLMAX, MaxVLMAX] =
          RISCVTargetLowering::computeVLMAXBounds(VT, Subtarget);

      uint64_t AVLInt = AVL->getAsZExtVal();
      if (AVLInt <= MinVLMAX) {
        I32VL = DAG.getConstant(2 * AVLInt, DL, XLenVT);
      } else if (AVLInt >= 2 * MaxVLMAX) {
        // Just set vl to VLMAX in this situation
        RISCVII::VLMUL Lmul = RISCVTargetLowering::getLMUL(I32VT);
        SDValue LMUL = DAG.getConstant(Lmul, DL, XLenVT);
        unsigned Sew = RISCVVType::encodeSEW(I32VT.getScalarSizeInBits());
        SDValue SEW = DAG.getConstant(Sew, DL, XLenVT);
        SDValue SETVLMAX = DAG.getTargetConstant(
            Intrinsic::riscv_vsetvlimax, DL, MVT::i32);
        I32VL = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, XLenVT, SETVLMAX, SEW,
                            LMUL);
      } else {
        // For AVL between (MinVLMAX, 2 * MaxVLMAX), the actual working vl
        // is related to the hardware implementation.
        // So let the following code handle
      }
    }
    if (!I32VL) {
      RISCVII::VLMUL Lmul = RISCVTargetLowering::getLMUL(VT);
      SDValue LMUL = DAG.getConstant(Lmul, DL, XLenVT);
      unsigned Sew = RISCVVType::encodeSEW(VT.getScalarSizeInBits());
      SDValue SEW = DAG.getConstant(Sew, DL, XLenVT);
      SDValue SETVL =
          DAG.getTargetConstant(Intrinsic::riscv_vsetvli, DL, MVT::i32);
      // Using vsetvli instruction to get actually used length which related to
      // the hardware implementation
      SDValue VL = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, XLenVT, SETVL, AVL,
                               SEW, LMUL);
      I32VL =
          DAG.getNode(ISD::SHL, DL, XLenVT, VL, DAG.getConstant(1, DL, XLenVT));
    }

    SDValue I32Mask = getAllOnesMask(I32VT, I32VL, DL, DAG);

    // Shift the two scalar parts in using SEW=32 slide1up/slide1down
    // instructions.
    SDValue Passthru;
    if (IsMasked)
      Passthru = DAG.getUNDEF(I32VT);
    else
      Passthru = DAG.getBitcast(I32VT, Operands[1]);

    if (IntNo == Intrinsic::riscv_vslide1up ||
        IntNo == Intrinsic::riscv_vslide1up_mask) {
      Vec = DAG.getNode(RISCVISD::VSLIDE1UP_VL, DL, I32VT, Passthru, Vec,
                        ScalarHi, I32Mask, I32VL);
      Vec = DAG.getNode(RISCVISD::VSLIDE1UP_VL, DL, I32VT, Passthru, Vec,
                        ScalarLo, I32Mask, I32VL);
    } else {
      Vec = DAG.getNode(RISCVISD::VSLIDE1DOWN_VL, DL, I32VT, Passthru, Vec,
                        ScalarLo, I32Mask, I32VL);
      Vec = DAG.getNode(RISCVISD::VSLIDE1DOWN_VL, DL, I32VT, Passthru, Vec,
                        ScalarHi, I32Mask, I32VL);
    }

    // Convert back to nxvXi64.
    Vec = DAG.getBitcast(VT, Vec);

    if (!IsMasked)
      return Vec;
    // Apply mask after the operation.
    SDValue Mask = Operands[NumOps - 3];
    SDValue MaskedOff = Operands[1];
    // Assume Policy operand is the last operand.
    uint64_t Policy = Operands[NumOps - 1]->getAsZExtVal();
    // We don't need to select maskedoff if it's undef.
    if (MaskedOff.isUndef())
      return Vec;
    // TAMU
    if (Policy == RISCVII::TAIL_AGNOSTIC)
      return DAG.getNode(RISCVISD::VMERGE_VL, DL, VT, Mask, Vec, MaskedOff,
                         DAG.getUNDEF(VT), AVL);
    // TUMA or TUMU: Currently we always emit tumu policy regardless of tuma.
    // It's fine because vmerge does not care mask policy.
    return DAG.getNode(RISCVISD::VMERGE_VL, DL, VT, Mask, Vec, MaskedOff,
                       MaskedOff, AVL);
  }
  }

  // We need to convert the scalar to a splat vector.
  SDValue VL = getVLOperand(Op);
  assert(VL.getValueType() == XLenVT);
  ScalarOp = splatSplitI64WithVL(DL, VT, SDValue(), ScalarOp, VL, DAG);
  return DAG.getNode(Op->getOpcode(), DL, Op->getVTList(), Operands);
}

// Lower the llvm.get.vector.length intrinsic to vsetvli. We only support
// scalable vector llvm.get.vector.length for now.
//
// We need to convert from a scalable VF to a vsetvli with VLMax equal to
// (vscale * VF). The vscale and VF are independent of element width. We use
// SEW=8 for the vsetvli because it is the only element width that supports all
// fractional LMULs. The LMUL is choosen so that with SEW=8 the VLMax is
// (vscale * VF). Where vscale is defined as VLEN/RVVBitsPerBlock. The
// InsertVSETVLI pass can fix up the vtype of the vsetvli if a different
// SEW and LMUL are better for the surrounding vector instructions.
static SDValue lowerGetVectorLength(SDNode *N, SelectionDAG &DAG,
                                    const RISCVSubtarget &Subtarget) {
  MVT XLenVT = Subtarget.getXLenVT();

  // The smallest LMUL is only valid for the smallest element width.
  const unsigned ElementWidth = 8;

  // Determine the VF that corresponds to LMUL 1 for ElementWidth.
  unsigned LMul1VF = RISCV::RVVBitsPerBlock / ElementWidth;
  // We don't support VF==1 with ELEN==32.
  [[maybe_unused]] unsigned MinVF =
      RISCV::RVVBitsPerBlock / Subtarget.getELen();

  [[maybe_unused]] unsigned VF = N->getConstantOperandVal(2);
  assert(VF >= MinVF && VF <= (LMul1VF * 8) && isPowerOf2_32(VF) &&
         "Unexpected VF");

  bool Fractional = VF < LMul1VF;
  unsigned LMulVal = Fractional ? LMul1VF / VF : VF / LMul1VF;
  unsigned VLMUL = (unsigned)RISCVVType::encodeLMUL(LMulVal, Fractional);
  unsigned VSEW = RISCVVType::encodeSEW(ElementWidth);

  SDLoc DL(N);

  SDValue LMul = DAG.getTargetConstant(VLMUL, DL, XLenVT);
  SDValue Sew = DAG.getTargetConstant(VSEW, DL, XLenVT);

  SDValue AVL = DAG.getNode(ISD::ZERO_EXTEND, DL, XLenVT, N->getOperand(1));

  SDValue ID = DAG.getTargetConstant(Intrinsic::riscv_vsetvli, DL, XLenVT);
  SDValue Res =
      DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, XLenVT, ID, AVL, Sew, LMul);
  return DAG.getNode(ISD::TRUNCATE, DL, N->getValueType(0), Res);
}

static SDValue lowerCttzElts(SDNode *N, SelectionDAG &DAG,
                             const RISCVSubtarget &Subtarget) {
  SDValue Op0 = N->getOperand(1);
  MVT OpVT = Op0.getSimpleValueType();
  MVT ContainerVT = OpVT;
  if (OpVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(DAG, OpVT, Subtarget);
    Op0 = convertToScalableVector(ContainerVT, Op0, DAG, Subtarget);
  }
  MVT XLenVT = Subtarget.getXLenVT();
  SDLoc DL(N);
  auto [Mask, VL] = getDefaultVLOps(OpVT, ContainerVT, DL, DAG, Subtarget);
  SDValue Res = DAG.getNode(RISCVISD::VFIRST_VL, DL, XLenVT, Op0, Mask, VL);
  if (isOneConstant(N->getOperand(2)))
    return Res;

  // Convert -1 to VL.
  SDValue Setcc =
      DAG.getSetCC(DL, XLenVT, Res, DAG.getConstant(0, DL, XLenVT), ISD::SETLT);
  VL = DAG.getElementCount(DL, XLenVT, OpVT.getVectorElementCount());
  return DAG.getSelect(DL, XLenVT, Setcc, VL, Res);
}

static inline void promoteVCIXScalar(const SDValue &Op,
                                     SmallVectorImpl<SDValue> &Operands,
                                     SelectionDAG &DAG) {
  const RISCVSubtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<RISCVSubtarget>();

  bool HasChain = Op.getOpcode() == ISD::INTRINSIC_VOID ||
                  Op.getOpcode() == ISD::INTRINSIC_W_CHAIN;
  unsigned IntNo = Op.getConstantOperandVal(HasChain ? 1 : 0);
  SDLoc DL(Op);

  const RISCVVIntrinsicsTable::RISCVVIntrinsicInfo *II =
      RISCVVIntrinsicsTable::getRISCVVIntrinsicInfo(IntNo);
  if (!II || !II->hasScalarOperand())
    return;

  unsigned SplatOp = II->ScalarOperand + 1;
  assert(SplatOp < Op.getNumOperands());

  SDValue &ScalarOp = Operands[SplatOp];
  MVT OpVT = ScalarOp.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  // The code below is partially copied from lowerVectorIntrinsicScalars.
  // If this isn't a scalar, or its type is XLenVT we're done.
  if (!OpVT.isScalarInteger() || OpVT == XLenVT)
    return;

  // Manually emit promote operation for scalar operation.
  if (OpVT.bitsLT(XLenVT)) {
    unsigned ExtOpc =
        isa<ConstantSDNode>(ScalarOp) ? ISD::SIGN_EXTEND : ISD::ANY_EXTEND;
    ScalarOp = DAG.getNode(ExtOpc, DL, XLenVT, ScalarOp);
  }

  return;
}

static void processVCIXOperands(SDValue &OrigOp,
                                SmallVectorImpl<SDValue> &Operands,
                                SelectionDAG &DAG) {
  promoteVCIXScalar(OrigOp, Operands, DAG);
  const RISCVSubtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<RISCVSubtarget>();
  for (SDValue &V : Operands) {
    EVT ValType = V.getValueType();
    if (ValType.isVector() && ValType.isFloatingPoint()) {
      MVT InterimIVT =
          MVT::getVectorVT(MVT::getIntegerVT(ValType.getScalarSizeInBits()),
                           ValType.getVectorElementCount());
      V = DAG.getBitcast(InterimIVT, V);
    }
    if (ValType.isFixedLengthVector()) {
      MVT OpContainerVT = getContainerForFixedLengthVector(
          DAG, V.getSimpleValueType(), Subtarget);
      V = convertToScalableVector(OpContainerVT, V, DAG, Subtarget);
    }
  }
}

// LMUL * VLEN should be greater than or equal to EGS * SEW
static inline bool isValidEGW(int EGS, EVT VT,
                              const RISCVSubtarget &Subtarget) {
  return (Subtarget.getRealMinVLen() *
             VT.getSizeInBits().getKnownMinValue()) / RISCV::RVVBitsPerBlock >=
         EGS * VT.getScalarSizeInBits();
}

SDValue RISCVTargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                     SelectionDAG &DAG) const {
  unsigned IntNo = Op.getConstantOperandVal(0);
  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();

  switch (IntNo) {
  default:
    break; // Don't custom lower most intrinsics.
  case Intrinsic::thread_pointer: {
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    return DAG.getRegister(RISCV::X4, PtrVT);
  }
  case Intrinsic::riscv_orc_b:
  case Intrinsic::riscv_brev8:
  case Intrinsic::riscv_sha256sig0:
  case Intrinsic::riscv_sha256sig1:
  case Intrinsic::riscv_sha256sum0:
  case Intrinsic::riscv_sha256sum1:
  case Intrinsic::riscv_sm3p0:
  case Intrinsic::riscv_sm3p1: {
    unsigned Opc;
    switch (IntNo) {
    case Intrinsic::riscv_orc_b:      Opc = RISCVISD::ORC_B;      break;
    case Intrinsic::riscv_brev8:      Opc = RISCVISD::BREV8;      break;
    case Intrinsic::riscv_sha256sig0: Opc = RISCVISD::SHA256SIG0; break;
    case Intrinsic::riscv_sha256sig1: Opc = RISCVISD::SHA256SIG1; break;
    case Intrinsic::riscv_sha256sum0: Opc = RISCVISD::SHA256SUM0; break;
    case Intrinsic::riscv_sha256sum1: Opc = RISCVISD::SHA256SUM1; break;
    case Intrinsic::riscv_sm3p0:      Opc = RISCVISD::SM3P0;      break;
    case Intrinsic::riscv_sm3p1:      Opc = RISCVISD::SM3P1;      break;
    }

    if (RV64LegalI32 && Subtarget.is64Bit() && Op.getValueType() == MVT::i32) {
      SDValue NewOp =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(1));
      SDValue Res = DAG.getNode(Opc, DL, MVT::i64, NewOp);
      return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res);
    }

    return DAG.getNode(Opc, DL, XLenVT, Op.getOperand(1));
  }
  case Intrinsic::riscv_sm4ks:
  case Intrinsic::riscv_sm4ed: {
    unsigned Opc =
        IntNo == Intrinsic::riscv_sm4ks ? RISCVISD::SM4KS : RISCVISD::SM4ED;

    if (RV64LegalI32 && Subtarget.is64Bit() && Op.getValueType() == MVT::i32) {
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(2));
      SDValue Res =
          DAG.getNode(Opc, DL, MVT::i64, NewOp0, NewOp1, Op.getOperand(3));
      return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res);
    }

    return DAG.getNode(Opc, DL, XLenVT, Op.getOperand(1), Op.getOperand(2),
                       Op.getOperand(3));
  }
  case Intrinsic::riscv_zip:
  case Intrinsic::riscv_unzip: {
    unsigned Opc =
        IntNo == Intrinsic::riscv_zip ? RISCVISD::ZIP : RISCVISD::UNZIP;
    return DAG.getNode(Opc, DL, XLenVT, Op.getOperand(1));
  }
  case Intrinsic::riscv_mopr: {
    if (RV64LegalI32 && Subtarget.is64Bit() && Op.getValueType() == MVT::i32) {
      SDValue NewOp =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(1));
      SDValue Res = DAG.getNode(
          RISCVISD::MOPR, DL, MVT::i64, NewOp,
          DAG.getTargetConstant(Op.getConstantOperandVal(2), DL, MVT::i64));
      return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res);
    }
    return DAG.getNode(RISCVISD::MOPR, DL, XLenVT, Op.getOperand(1),
                       Op.getOperand(2));
  }

  case Intrinsic::riscv_moprr: {
    if (RV64LegalI32 && Subtarget.is64Bit() && Op.getValueType() == MVT::i32) {
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(2));
      SDValue Res = DAG.getNode(
          RISCVISD::MOPRR, DL, MVT::i64, NewOp0, NewOp1,
          DAG.getTargetConstant(Op.getConstantOperandVal(3), DL, MVT::i64));
      return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res);
    }
    return DAG.getNode(RISCVISD::MOPRR, DL, XLenVT, Op.getOperand(1),
                       Op.getOperand(2), Op.getOperand(3));
  }
  case Intrinsic::riscv_clmul:
    if (RV64LegalI32 && Subtarget.is64Bit() && Op.getValueType() == MVT::i32) {
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(2));
      SDValue Res = DAG.getNode(RISCVISD::CLMUL, DL, MVT::i64, NewOp0, NewOp1);
      return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res);
    }
    return DAG.getNode(RISCVISD::CLMUL, DL, XLenVT, Op.getOperand(1),
                       Op.getOperand(2));
  case Intrinsic::riscv_clmulh:
  case Intrinsic::riscv_clmulr: {
    unsigned Opc =
        IntNo == Intrinsic::riscv_clmulh ? RISCVISD::CLMULH : RISCVISD::CLMULR;
    if (RV64LegalI32 && Subtarget.is64Bit() && Op.getValueType() == MVT::i32) {
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op.getOperand(2));
      NewOp0 = DAG.getNode(ISD::SHL, DL, MVT::i64, NewOp0,
                           DAG.getConstant(32, DL, MVT::i64));
      NewOp1 = DAG.getNode(ISD::SHL, DL, MVT::i64, NewOp1,
                           DAG.getConstant(32, DL, MVT::i64));
      SDValue Res = DAG.getNode(Opc, DL, MVT::i64, NewOp0, NewOp1);
      Res = DAG.getNode(ISD::SRL, DL, MVT::i64, Res,
                        DAG.getConstant(32, DL, MVT::i64));
      return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res);
    }

    return DAG.getNode(Opc, DL, XLenVT, Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::experimental_get_vector_length:
    return lowerGetVectorLength(Op.getNode(), DAG, Subtarget);
  case Intrinsic::experimental_cttz_elts:
    return lowerCttzElts(Op.getNode(), DAG, Subtarget);
  case Intrinsic::riscv_vmv_x_s: {
    SDValue Res = DAG.getNode(RISCVISD::VMV_X_S, DL, XLenVT, Op.getOperand(1));
    return DAG.getNode(ISD::TRUNCATE, DL, Op.getValueType(), Res);
  }
  case Intrinsic::riscv_vfmv_f_s:
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, Op.getValueType(),
                       Op.getOperand(1), DAG.getVectorIdxConstant(0, DL));
  case Intrinsic::riscv_vmv_v_x:
    return lowerScalarSplat(Op.getOperand(1), Op.getOperand(2),
                            Op.getOperand(3), Op.getSimpleValueType(), DL, DAG,
                            Subtarget);
  case Intrinsic::riscv_vfmv_v_f:
    return DAG.getNode(RISCVISD::VFMV_V_F_VL, DL, Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  case Intrinsic::riscv_vmv_s_x: {
    SDValue Scalar = Op.getOperand(2);

    if (Scalar.getValueType().bitsLE(XLenVT)) {
      Scalar = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, Scalar);
      return DAG.getNode(RISCVISD::VMV_S_X_VL, DL, Op.getValueType(),
                         Op.getOperand(1), Scalar, Op.getOperand(3));
    }

    assert(Scalar.getValueType() == MVT::i64 && "Unexpected scalar VT!");

    // This is an i64 value that lives in two scalar registers. We have to
    // insert this in a convoluted way. First we build vXi64 splat containing
    // the two values that we assemble using some bit math. Next we'll use
    // vid.v and vmseq to build a mask with bit 0 set. Then we'll use that mask
    // to merge element 0 from our splat into the source vector.
    // FIXME: This is probably not the best way to do this, but it is
    // consistent with INSERT_VECTOR_ELT lowering so it is a good starting
    // point.
    //   sw lo, (a0)
    //   sw hi, 4(a0)
    //   vlse vX, (a0)
    //
    //   vid.v      vVid
    //   vmseq.vx   mMask, vVid, 0
    //   vmerge.vvm vDest, vSrc, vVal, mMask
    MVT VT = Op.getSimpleValueType();
    SDValue Vec = Op.getOperand(1);
    SDValue VL = getVLOperand(Op);

    SDValue SplattedVal = splatSplitI64WithVL(DL, VT, SDValue(), Scalar, VL, DAG);
    if (Op.getOperand(1).isUndef())
      return SplattedVal;
    SDValue SplattedIdx =
        DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, DAG.getUNDEF(VT),
                    DAG.getConstant(0, DL, MVT::i32), VL);

    MVT MaskVT = getMaskTypeFor(VT);
    SDValue Mask = getAllOnesMask(VT, VL, DL, DAG);
    SDValue VID = DAG.getNode(RISCVISD::VID_VL, DL, VT, Mask, VL);
    SDValue SelectCond =
        DAG.getNode(RISCVISD::SETCC_VL, DL, MaskVT,
                    {VID, SplattedIdx, DAG.getCondCode(ISD::SETEQ),
                     DAG.getUNDEF(MaskVT), Mask, VL});
    return DAG.getNode(RISCVISD::VMERGE_VL, DL, VT, SelectCond, SplattedVal,
                       Vec, DAG.getUNDEF(VT), VL);
  }
  case Intrinsic::riscv_vfmv_s_f:
    return DAG.getNode(RISCVISD::VFMV_S_F_VL, DL, Op.getSimpleValueType(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  // EGS * EEW >= 128 bits
  case Intrinsic::riscv_vaesdf_vv:
  case Intrinsic::riscv_vaesdf_vs:
  case Intrinsic::riscv_vaesdm_vv:
  case Intrinsic::riscv_vaesdm_vs:
  case Intrinsic::riscv_vaesef_vv:
  case Intrinsic::riscv_vaesef_vs:
  case Intrinsic::riscv_vaesem_vv:
  case Intrinsic::riscv_vaesem_vs:
  case Intrinsic::riscv_vaeskf1:
  case Intrinsic::riscv_vaeskf2:
  case Intrinsic::riscv_vaesz_vs:
  case Intrinsic::riscv_vsm4k:
  case Intrinsic::riscv_vsm4r_vv:
  case Intrinsic::riscv_vsm4r_vs: {
    if (!isValidEGW(4, Op.getSimpleValueType(), Subtarget) ||
        !isValidEGW(4, Op->getOperand(1).getSimpleValueType(), Subtarget) ||
        !isValidEGW(4, Op->getOperand(2).getSimpleValueType(), Subtarget))
      report_fatal_error("EGW should be greater than or equal to 4 * SEW.");
    return Op;
  }
  // EGS * EEW >= 256 bits
  case Intrinsic::riscv_vsm3c:
  case Intrinsic::riscv_vsm3me: {
    if (!isValidEGW(8, Op.getSimpleValueType(), Subtarget) ||
        !isValidEGW(8, Op->getOperand(1).getSimpleValueType(), Subtarget))
      report_fatal_error("EGW should be greater than or equal to 8 * SEW.");
    return Op;
  }
  // zvknha(SEW=32)/zvknhb(SEW=[32|64])
  case Intrinsic::riscv_vsha2ch:
  case Intrinsic::riscv_vsha2cl:
  case Intrinsic::riscv_vsha2ms: {
    if (Op->getSimpleValueType(0).getScalarSizeInBits() == 64 &&
        !Subtarget.hasStdExtZvknhb())
      report_fatal_error("SEW=64 needs Zvknhb to be enabled.");
    if (!isValidEGW(4, Op.getSimpleValueType(), Subtarget) ||
        !isValidEGW(4, Op->getOperand(1).getSimpleValueType(), Subtarget) ||
        !isValidEGW(4, Op->getOperand(2).getSimpleValueType(), Subtarget))
      report_fatal_error("EGW should be greater than or equal to 4 * SEW.");
    return Op;
  }
  case Intrinsic::riscv_sf_vc_v_x:
  case Intrinsic::riscv_sf_vc_v_i:
  case Intrinsic::riscv_sf_vc_v_xv:
  case Intrinsic::riscv_sf_vc_v_iv:
  case Intrinsic::riscv_sf_vc_v_vv:
  case Intrinsic::riscv_sf_vc_v_fv:
  case Intrinsic::riscv_sf_vc_v_xvv:
  case Intrinsic::riscv_sf_vc_v_ivv:
  case Intrinsic::riscv_sf_vc_v_vvv:
  case Intrinsic::riscv_sf_vc_v_fvv:
  case Intrinsic::riscv_sf_vc_v_xvw:
  case Intrinsic::riscv_sf_vc_v_ivw:
  case Intrinsic::riscv_sf_vc_v_vvw:
  case Intrinsic::riscv_sf_vc_v_fvw: {
    MVT VT = Op.getSimpleValueType();

    SmallVector<SDValue> Operands{Op->op_values()};
    processVCIXOperands(Op, Operands, DAG);

    MVT RetVT = VT;
    if (VT.isFixedLengthVector())
      RetVT = getContainerForFixedLengthVector(VT);
    else if (VT.isFloatingPoint())
      RetVT = MVT::getVectorVT(MVT::getIntegerVT(VT.getScalarSizeInBits()),
                               VT.getVectorElementCount());

    SDValue NewNode = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, RetVT, Operands);

    if (VT.isFixedLengthVector())
      NewNode = convertFromScalableVector(VT, NewNode, DAG, Subtarget);
    else if (VT.isFloatingPoint())
      NewNode = DAG.getBitcast(VT, NewNode);

    if (Op == NewNode)
      break;

    return NewNode;
  }
  }

  return lowerVectorIntrinsicScalars(Op, DAG, Subtarget);
}

static inline SDValue getVCIXISDNodeWCHAIN(SDValue &Op, SelectionDAG &DAG,
                                           unsigned Type) {
  SDLoc DL(Op);
  SmallVector<SDValue> Operands{Op->op_values()};
  Operands.erase(Operands.begin() + 1);

  const RISCVSubtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<RISCVSubtarget>();
  MVT VT = Op.getSimpleValueType();
  MVT RetVT = VT;
  MVT FloatVT = VT;

  if (VT.isFloatingPoint()) {
    RetVT = MVT::getVectorVT(MVT::getIntegerVT(VT.getScalarSizeInBits()),
                             VT.getVectorElementCount());
    FloatVT = RetVT;
  }
  if (VT.isFixedLengthVector())
    RetVT = getContainerForFixedLengthVector(DAG.getTargetLoweringInfo(), RetVT,
                                             Subtarget);

  processVCIXOperands(Op, Operands, DAG);

  SDVTList VTs = DAG.getVTList({RetVT, MVT::Other});
  SDValue NewNode = DAG.getNode(Type, DL, VTs, Operands);
  SDValue Chain = NewNode.getValue(1);

  if (VT.isFixedLengthVector())
    NewNode = convertFromScalableVector(FloatVT, NewNode, DAG, Subtarget);
  if (VT.isFloatingPoint())
    NewNode = DAG.getBitcast(VT, NewNode);

  NewNode = DAG.getMergeValues({NewNode, Chain}, DL);

  return NewNode;
}

static inline SDValue getVCIXISDNodeVOID(SDValue &Op, SelectionDAG &DAG,
                                         unsigned Type) {
  SmallVector<SDValue> Operands{Op->op_values()};
  Operands.erase(Operands.begin() + 1);
  processVCIXOperands(Op, Operands, DAG);

  return DAG.getNode(Type, SDLoc(Op), Op.getValueType(), Operands);
}

SDValue RISCVTargetLowering::LowerINTRINSIC_W_CHAIN(SDValue Op,
                                                    SelectionDAG &DAG) const {
  unsigned IntNo = Op.getConstantOperandVal(1);
  switch (IntNo) {
  default:
    break;
  case Intrinsic::riscv_masked_strided_load: {
    SDLoc DL(Op);
    MVT XLenVT = Subtarget.getXLenVT();

    // If the mask is known to be all ones, optimize to an unmasked intrinsic;
    // the selection of the masked intrinsics doesn't do this for us.
    SDValue Mask = Op.getOperand(5);
    bool IsUnmasked = ISD::isConstantSplatVectorAllOnes(Mask.getNode());

    MVT VT = Op->getSimpleValueType(0);
    MVT ContainerVT = VT;
    if (VT.isFixedLengthVector())
      ContainerVT = getContainerForFixedLengthVector(VT);

    SDValue PassThru = Op.getOperand(2);
    if (!IsUnmasked) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      if (VT.isFixedLengthVector()) {
        Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
        PassThru = convertToScalableVector(ContainerVT, PassThru, DAG, Subtarget);
      }
    }

    auto *Load = cast<MemIntrinsicSDNode>(Op);
    SDValue VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;
    SDValue Ptr = Op.getOperand(3);
    SDValue Stride = Op.getOperand(4);
    SDValue Result, Chain;

    // TODO: We restrict this to unmasked loads currently in consideration of
    // the complexity of handling all falses masks.
    MVT ScalarVT = ContainerVT.getVectorElementType();
    if (IsUnmasked && isNullConstant(Stride) && ContainerVT.isInteger()) {
      SDValue ScalarLoad =
          DAG.getExtLoad(ISD::EXTLOAD, DL, XLenVT, Load->getChain(), Ptr,
                         ScalarVT, Load->getMemOperand());
      Chain = ScalarLoad.getValue(1);
      Result = lowerScalarSplat(SDValue(), ScalarLoad, VL, ContainerVT, DL, DAG,
                                Subtarget);
    } else if (IsUnmasked && isNullConstant(Stride) && isTypeLegal(ScalarVT)) {
      SDValue ScalarLoad = DAG.getLoad(ScalarVT, DL, Load->getChain(), Ptr,
                                       Load->getMemOperand());
      Chain = ScalarLoad.getValue(1);
      Result = DAG.getSplat(ContainerVT, DL, ScalarLoad);
    } else {
      SDValue IntID = DAG.getTargetConstant(
          IsUnmasked ? Intrinsic::riscv_vlse : Intrinsic::riscv_vlse_mask, DL,
          XLenVT);

      SmallVector<SDValue, 8> Ops{Load->getChain(), IntID};
      if (IsUnmasked)
        Ops.push_back(DAG.getUNDEF(ContainerVT));
      else
        Ops.push_back(PassThru);
      Ops.push_back(Ptr);
      Ops.push_back(Stride);
      if (!IsUnmasked)
        Ops.push_back(Mask);
      Ops.push_back(VL);
      if (!IsUnmasked) {
        SDValue Policy =
            DAG.getTargetConstant(RISCVII::TAIL_AGNOSTIC, DL, XLenVT);
        Ops.push_back(Policy);
      }

      SDVTList VTs = DAG.getVTList({ContainerVT, MVT::Other});
      Result =
          DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, VTs, Ops,
                                  Load->getMemoryVT(), Load->getMemOperand());
      Chain = Result.getValue(1);
    }
    if (VT.isFixedLengthVector())
      Result = convertFromScalableVector(VT, Result, DAG, Subtarget);
    return DAG.getMergeValues({Result, Chain}, DL);
  }
  case Intrinsic::riscv_seg2_load:
  case Intrinsic::riscv_seg3_load:
  case Intrinsic::riscv_seg4_load:
  case Intrinsic::riscv_seg5_load:
  case Intrinsic::riscv_seg6_load:
  case Intrinsic::riscv_seg7_load:
  case Intrinsic::riscv_seg8_load: {
    SDLoc DL(Op);
    static const Intrinsic::ID VlsegInts[7] = {
        Intrinsic::riscv_vlseg2, Intrinsic::riscv_vlseg3,
        Intrinsic::riscv_vlseg4, Intrinsic::riscv_vlseg5,
        Intrinsic::riscv_vlseg6, Intrinsic::riscv_vlseg7,
        Intrinsic::riscv_vlseg8};
    unsigned NF = Op->getNumValues() - 1;
    assert(NF >= 2 && NF <= 8 && "Unexpected seg number");
    MVT XLenVT = Subtarget.getXLenVT();
    MVT VT = Op->getSimpleValueType(0);
    MVT ContainerVT = getContainerForFixedLengthVector(VT);

    SDValue VL = getVLOp(VT.getVectorNumElements(), ContainerVT, DL, DAG,
                         Subtarget);
    SDValue IntID = DAG.getTargetConstant(VlsegInts[NF - 2], DL, XLenVT);
    auto *Load = cast<MemIntrinsicSDNode>(Op);
    SmallVector<EVT, 9> ContainerVTs(NF, ContainerVT);
    ContainerVTs.push_back(MVT::Other);
    SDVTList VTs = DAG.getVTList(ContainerVTs);
    SmallVector<SDValue, 12> Ops = {Load->getChain(), IntID};
    Ops.insert(Ops.end(), NF, DAG.getUNDEF(ContainerVT));
    Ops.push_back(Op.getOperand(2));
    Ops.push_back(VL);
    SDValue Result =
        DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, VTs, Ops,
                                Load->getMemoryVT(), Load->getMemOperand());
    SmallVector<SDValue, 9> Results;
    for (unsigned int RetIdx = 0; RetIdx < NF; RetIdx++)
      Results.push_back(convertFromScalableVector(VT, Result.getValue(RetIdx),
                                                  DAG, Subtarget));
    Results.push_back(Result.getValue(NF));
    return DAG.getMergeValues(Results, DL);
  }
  case Intrinsic::riscv_sf_vc_v_x_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_X_SE);
  case Intrinsic::riscv_sf_vc_v_i_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_I_SE);
  case Intrinsic::riscv_sf_vc_v_xv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_XV_SE);
  case Intrinsic::riscv_sf_vc_v_iv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_IV_SE);
  case Intrinsic::riscv_sf_vc_v_vv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_VV_SE);
  case Intrinsic::riscv_sf_vc_v_fv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_FV_SE);
  case Intrinsic::riscv_sf_vc_v_xvv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_XVV_SE);
  case Intrinsic::riscv_sf_vc_v_ivv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_IVV_SE);
  case Intrinsic::riscv_sf_vc_v_vvv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_VVV_SE);
  case Intrinsic::riscv_sf_vc_v_fvv_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_FVV_SE);
  case Intrinsic::riscv_sf_vc_v_xvw_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_XVW_SE);
  case Intrinsic::riscv_sf_vc_v_ivw_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_IVW_SE);
  case Intrinsic::riscv_sf_vc_v_vvw_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_VVW_SE);
  case Intrinsic::riscv_sf_vc_v_fvw_se:
    return getVCIXISDNodeWCHAIN(Op, DAG, RISCVISD::SF_VC_V_FVW_SE);
  }

  return lowerVectorIntrinsicScalars(Op, DAG, Subtarget);
}

SDValue RISCVTargetLowering::LowerINTRINSIC_VOID(SDValue Op,
                                                 SelectionDAG &DAG) const {
  unsigned IntNo = Op.getConstantOperandVal(1);
  switch (IntNo) {
  default:
    break;
  case Intrinsic::riscv_masked_strided_store: {
    SDLoc DL(Op);
    MVT XLenVT = Subtarget.getXLenVT();

    // If the mask is known to be all ones, optimize to an unmasked intrinsic;
    // the selection of the masked intrinsics doesn't do this for us.
    SDValue Mask = Op.getOperand(5);
    bool IsUnmasked = ISD::isConstantSplatVectorAllOnes(Mask.getNode());

    SDValue Val = Op.getOperand(2);
    MVT VT = Val.getSimpleValueType();
    MVT ContainerVT = VT;
    if (VT.isFixedLengthVector()) {
      ContainerVT = getContainerForFixedLengthVector(VT);
      Val = convertToScalableVector(ContainerVT, Val, DAG, Subtarget);
    }
    if (!IsUnmasked) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      if (VT.isFixedLengthVector())
        Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }

    SDValue VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;

    SDValue IntID = DAG.getTargetConstant(
        IsUnmasked ? Intrinsic::riscv_vsse : Intrinsic::riscv_vsse_mask, DL,
        XLenVT);

    auto *Store = cast<MemIntrinsicSDNode>(Op);
    SmallVector<SDValue, 8> Ops{Store->getChain(), IntID};
    Ops.push_back(Val);
    Ops.push_back(Op.getOperand(3)); // Ptr
    Ops.push_back(Op.getOperand(4)); // Stride
    if (!IsUnmasked)
      Ops.push_back(Mask);
    Ops.push_back(VL);

    return DAG.getMemIntrinsicNode(ISD::INTRINSIC_VOID, DL, Store->getVTList(),
                                   Ops, Store->getMemoryVT(),
                                   Store->getMemOperand());
  }
  case Intrinsic::riscv_seg2_store:
  case Intrinsic::riscv_seg3_store:
  case Intrinsic::riscv_seg4_store:
  case Intrinsic::riscv_seg5_store:
  case Intrinsic::riscv_seg6_store:
  case Intrinsic::riscv_seg7_store:
  case Intrinsic::riscv_seg8_store: {
    SDLoc DL(Op);
    static const Intrinsic::ID VssegInts[] = {
        Intrinsic::riscv_vsseg2, Intrinsic::riscv_vsseg3,
        Intrinsic::riscv_vsseg4, Intrinsic::riscv_vsseg5,
        Intrinsic::riscv_vsseg6, Intrinsic::riscv_vsseg7,
        Intrinsic::riscv_vsseg8};
    // Operands are (chain, int_id, vec*, ptr, vl)
    unsigned NF = Op->getNumOperands() - 4;
    assert(NF >= 2 && NF <= 8 && "Unexpected seg number");
    MVT XLenVT = Subtarget.getXLenVT();
    MVT VT = Op->getOperand(2).getSimpleValueType();
    MVT ContainerVT = getContainerForFixedLengthVector(VT);

    SDValue VL = getVLOp(VT.getVectorNumElements(), ContainerVT, DL, DAG,
                         Subtarget);
    SDValue IntID = DAG.getTargetConstant(VssegInts[NF - 2], DL, XLenVT);
    SDValue Ptr = Op->getOperand(NF + 2);

    auto *FixedIntrinsic = cast<MemIntrinsicSDNode>(Op);
    SmallVector<SDValue, 12> Ops = {FixedIntrinsic->getChain(), IntID};
    for (unsigned i = 0; i < NF; i++)
      Ops.push_back(convertToScalableVector(
          ContainerVT, FixedIntrinsic->getOperand(2 + i), DAG, Subtarget));
    Ops.append({Ptr, VL});

    return DAG.getMemIntrinsicNode(
        ISD::INTRINSIC_VOID, DL, DAG.getVTList(MVT::Other), Ops,
        FixedIntrinsic->getMemoryVT(), FixedIntrinsic->getMemOperand());
  }
  case Intrinsic::riscv_sf_vc_xv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_XV_SE);
  case Intrinsic::riscv_sf_vc_iv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_IV_SE);
  case Intrinsic::riscv_sf_vc_vv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_VV_SE);
  case Intrinsic::riscv_sf_vc_fv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_FV_SE);
  case Intrinsic::riscv_sf_vc_xvv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_XVV_SE);
  case Intrinsic::riscv_sf_vc_ivv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_IVV_SE);
  case Intrinsic::riscv_sf_vc_vvv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_VVV_SE);
  case Intrinsic::riscv_sf_vc_fvv_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_FVV_SE);
  case Intrinsic::riscv_sf_vc_xvw_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_XVW_SE);
  case Intrinsic::riscv_sf_vc_ivw_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_IVW_SE);
  case Intrinsic::riscv_sf_vc_vvw_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_VVW_SE);
  case Intrinsic::riscv_sf_vc_fvw_se:
    return getVCIXISDNodeVOID(Op, DAG, RISCVISD::SF_VC_FVW_SE);
  }

  return lowerVectorIntrinsicScalars(Op, DAG, Subtarget);
}

static unsigned getRVVReductionOp(unsigned ISDOpcode) {
  switch (ISDOpcode) {
  default:
    llvm_unreachable("Unhandled reduction");
  case ISD::VP_REDUCE_ADD:
  case ISD::VECREDUCE_ADD:
    return RISCVISD::VECREDUCE_ADD_VL;
  case ISD::VP_REDUCE_UMAX:
  case ISD::VECREDUCE_UMAX:
    return RISCVISD::VECREDUCE_UMAX_VL;
  case ISD::VP_REDUCE_SMAX:
  case ISD::VECREDUCE_SMAX:
    return RISCVISD::VECREDUCE_SMAX_VL;
  case ISD::VP_REDUCE_UMIN:
  case ISD::VECREDUCE_UMIN:
    return RISCVISD::VECREDUCE_UMIN_VL;
  case ISD::VP_REDUCE_SMIN:
  case ISD::VECREDUCE_SMIN:
    return RISCVISD::VECREDUCE_SMIN_VL;
  case ISD::VP_REDUCE_AND:
  case ISD::VECREDUCE_AND:
    return RISCVISD::VECREDUCE_AND_VL;
  case ISD::VP_REDUCE_OR:
  case ISD::VECREDUCE_OR:
    return RISCVISD::VECREDUCE_OR_VL;
  case ISD::VP_REDUCE_XOR:
  case ISD::VECREDUCE_XOR:
    return RISCVISD::VECREDUCE_XOR_VL;
  case ISD::VP_REDUCE_FADD:
    return RISCVISD::VECREDUCE_FADD_VL;
  case ISD::VP_REDUCE_SEQ_FADD:
    return RISCVISD::VECREDUCE_SEQ_FADD_VL;
  case ISD::VP_REDUCE_FMAX:
  case ISD::VP_REDUCE_FMAXIMUM:
    return RISCVISD::VECREDUCE_FMAX_VL;
  case ISD::VP_REDUCE_FMIN:
  case ISD::VP_REDUCE_FMINIMUM:
    return RISCVISD::VECREDUCE_FMIN_VL;
  }

}

SDValue RISCVTargetLowering::lowerVectorMaskVecReduction(SDValue Op,
                                                         SelectionDAG &DAG,
                                                         bool IsVP) const {
  SDLoc DL(Op);
  SDValue Vec = Op.getOperand(IsVP ? 1 : 0);
  MVT VecVT = Vec.getSimpleValueType();
  assert((Op.getOpcode() == ISD::VECREDUCE_AND ||
          Op.getOpcode() == ISD::VECREDUCE_OR ||
          Op.getOpcode() == ISD::VECREDUCE_XOR ||
          Op.getOpcode() == ISD::VP_REDUCE_AND ||
          Op.getOpcode() == ISD::VP_REDUCE_OR ||
          Op.getOpcode() == ISD::VP_REDUCE_XOR) &&
         "Unexpected reduction lowering");

  MVT XLenVT = Subtarget.getXLenVT();

  MVT ContainerVT = VecVT;
  if (VecVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VecVT);
    Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
  }

  SDValue Mask, VL;
  if (IsVP) {
    Mask = Op.getOperand(2);
    VL = Op.getOperand(3);
  } else {
    std::tie(Mask, VL) =
        getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget);
  }

  unsigned BaseOpc;
  ISD::CondCode CC;
  SDValue Zero = DAG.getConstant(0, DL, XLenVT);

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unhandled reduction");
  case ISD::VECREDUCE_AND:
  case ISD::VP_REDUCE_AND: {
    // vcpop ~x == 0
    SDValue TrueMask = DAG.getNode(RISCVISD::VMSET_VL, DL, ContainerVT, VL);
    Vec = DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Vec, TrueMask, VL);
    Vec = DAG.getNode(RISCVISD::VCPOP_VL, DL, XLenVT, Vec, Mask, VL);
    CC = ISD::SETEQ;
    BaseOpc = ISD::AND;
    break;
  }
  case ISD::VECREDUCE_OR:
  case ISD::VP_REDUCE_OR:
    // vcpop x != 0
    Vec = DAG.getNode(RISCVISD::VCPOP_VL, DL, XLenVT, Vec, Mask, VL);
    CC = ISD::SETNE;
    BaseOpc = ISD::OR;
    break;
  case ISD::VECREDUCE_XOR:
  case ISD::VP_REDUCE_XOR: {
    // ((vcpop x) & 1) != 0
    SDValue One = DAG.getConstant(1, DL, XLenVT);
    Vec = DAG.getNode(RISCVISD::VCPOP_VL, DL, XLenVT, Vec, Mask, VL);
    Vec = DAG.getNode(ISD::AND, DL, XLenVT, Vec, One);
    CC = ISD::SETNE;
    BaseOpc = ISD::XOR;
    break;
  }
  }

  SDValue SetCC = DAG.getSetCC(DL, XLenVT, Vec, Zero, CC);
  SetCC = DAG.getNode(ISD::TRUNCATE, DL, Op.getValueType(), SetCC);

  if (!IsVP)
    return SetCC;

  // Now include the start value in the operation.
  // Note that we must return the start value when no elements are operated
  // upon. The vcpop instructions we've emitted in each case above will return
  // 0 for an inactive vector, and so we've already received the neutral value:
  // AND gives us (0 == 0) -> 1 and OR/XOR give us (0 != 0) -> 0. Therefore we
  // can simply include the start value.
  return DAG.getNode(BaseOpc, DL, Op.getValueType(), SetCC, Op.getOperand(0));
}

static bool isNonZeroAVL(SDValue AVL) {
  auto *RegisterAVL = dyn_cast<RegisterSDNode>(AVL);
  auto *ImmAVL = dyn_cast<ConstantSDNode>(AVL);
  return (RegisterAVL && RegisterAVL->getReg() == RISCV::X0) ||
         (ImmAVL && ImmAVL->getZExtValue() >= 1);
}

/// Helper to lower a reduction sequence of the form:
/// scalar = reduce_op vec, scalar_start
static SDValue lowerReductionSeq(unsigned RVVOpcode, MVT ResVT,
                                 SDValue StartValue, SDValue Vec, SDValue Mask,
                                 SDValue VL, const SDLoc &DL, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  const MVT VecVT = Vec.getSimpleValueType();
  const MVT M1VT = getLMUL1VT(VecVT);
  const MVT XLenVT = Subtarget.getXLenVT();
  const bool NonZeroAVL = isNonZeroAVL(VL);

  // The reduction needs an LMUL1 input; do the splat at either LMUL1
  // or the original VT if fractional.
  auto InnerVT = VecVT.bitsLE(M1VT) ? VecVT : M1VT;
  // We reuse the VL of the reduction to reduce vsetvli toggles if we can
  // prove it is non-zero.  For the AVL=0 case, we need the scalar to
  // be the result of the reduction operation.
  auto InnerVL = NonZeroAVL ? VL : DAG.getConstant(1, DL, XLenVT);
  SDValue InitialValue = lowerScalarInsert(StartValue, InnerVL, InnerVT, DL,
                                           DAG, Subtarget);
  if (M1VT != InnerVT)
    InitialValue =
        DAG.getNode(ISD::INSERT_SUBVECTOR, DL, M1VT, DAG.getUNDEF(M1VT),
                    InitialValue, DAG.getVectorIdxConstant(0, DL));
  SDValue PassThru = NonZeroAVL ? DAG.getUNDEF(M1VT) : InitialValue;
  SDValue Policy = DAG.getTargetConstant(RISCVII::TAIL_AGNOSTIC, DL, XLenVT);
  SDValue Ops[] = {PassThru, Vec, InitialValue, Mask, VL, Policy};
  SDValue Reduction = DAG.getNode(RVVOpcode, DL, M1VT, Ops);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, ResVT, Reduction,
                     DAG.getVectorIdxConstant(0, DL));
}

SDValue RISCVTargetLowering::lowerVECREDUCE(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Vec = Op.getOperand(0);
  EVT VecEVT = Vec.getValueType();

  unsigned BaseOpc = ISD::getVecReduceBaseOpcode(Op.getOpcode());

  // Due to ordering in legalize types we may have a vector type that needs to
  // be split. Do that manually so we can get down to a legal type.
  while (getTypeAction(*DAG.getContext(), VecEVT) ==
         TargetLowering::TypeSplitVector) {
    auto [Lo, Hi] = DAG.SplitVector(Vec, DL);
    VecEVT = Lo.getValueType();
    Vec = DAG.getNode(BaseOpc, DL, VecEVT, Lo, Hi);
  }

  // TODO: The type may need to be widened rather than split. Or widened before
  // it can be split.
  if (!isTypeLegal(VecEVT))
    return SDValue();

  MVT VecVT = VecEVT.getSimpleVT();
  MVT VecEltVT = VecVT.getVectorElementType();
  unsigned RVVOpcode = getRVVReductionOp(Op.getOpcode());

  MVT ContainerVT = VecVT;
  if (VecVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VecVT);
    Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
  }

  auto [Mask, VL] = getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget);

  SDValue StartV = DAG.getNeutralElement(BaseOpc, DL, VecEltVT, SDNodeFlags());
  switch (BaseOpc) {
  case ISD::AND:
  case ISD::OR:
  case ISD::UMAX:
  case ISD::UMIN:
  case ISD::SMAX:
  case ISD::SMIN:
    StartV = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VecEltVT, Vec,
                         DAG.getVectorIdxConstant(0, DL));
  }
  return lowerReductionSeq(RVVOpcode, Op.getSimpleValueType(), StartV, Vec,
                           Mask, VL, DL, DAG, Subtarget);
}

// Given a reduction op, this function returns the matching reduction opcode,
// the vector SDValue and the scalar SDValue required to lower this to a
// RISCVISD node.
static std::tuple<unsigned, SDValue, SDValue>
getRVVFPReductionOpAndOperands(SDValue Op, SelectionDAG &DAG, EVT EltVT,
                               const RISCVSubtarget &Subtarget) {
  SDLoc DL(Op);
  auto Flags = Op->getFlags();
  unsigned Opcode = Op.getOpcode();
  switch (Opcode) {
  default:
    llvm_unreachable("Unhandled reduction");
  case ISD::VECREDUCE_FADD: {
    // Use positive zero if we can. It is cheaper to materialize.
    SDValue Zero =
        DAG.getConstantFP(Flags.hasNoSignedZeros() ? 0.0 : -0.0, DL, EltVT);
    return std::make_tuple(RISCVISD::VECREDUCE_FADD_VL, Op.getOperand(0), Zero);
  }
  case ISD::VECREDUCE_SEQ_FADD:
    return std::make_tuple(RISCVISD::VECREDUCE_SEQ_FADD_VL, Op.getOperand(1),
                           Op.getOperand(0));
  case ISD::VECREDUCE_FMINIMUM:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAX: {
    SDValue Front =
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Op.getOperand(0),
                    DAG.getVectorIdxConstant(0, DL));
    unsigned RVVOpc =
        (Opcode == ISD::VECREDUCE_FMIN || Opcode == ISD::VECREDUCE_FMINIMUM)
            ? RISCVISD::VECREDUCE_FMIN_VL
            : RISCVISD::VECREDUCE_FMAX_VL;
    return std::make_tuple(RVVOpc, Op.getOperand(0), Front);
  }
  }
}

SDValue RISCVTargetLowering::lowerFPVECREDUCE(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VecEltVT = Op.getSimpleValueType();

  unsigned RVVOpcode;
  SDValue VectorVal, ScalarVal;
  std::tie(RVVOpcode, VectorVal, ScalarVal) =
      getRVVFPReductionOpAndOperands(Op, DAG, VecEltVT, Subtarget);
  MVT VecVT = VectorVal.getSimpleValueType();

  MVT ContainerVT = VecVT;
  if (VecVT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VecVT);
    VectorVal = convertToScalableVector(ContainerVT, VectorVal, DAG, Subtarget);
  }

  MVT ResVT = Op.getSimpleValueType();
  auto [Mask, VL] = getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget);
  SDValue Res = lowerReductionSeq(RVVOpcode, ResVT, ScalarVal, VectorVal, Mask,
                                  VL, DL, DAG, Subtarget);
  if (Op.getOpcode() != ISD::VECREDUCE_FMINIMUM &&
      Op.getOpcode() != ISD::VECREDUCE_FMAXIMUM)
    return Res;

  if (Op->getFlags().hasNoNaNs())
    return Res;

  // Force output to NaN if any element is Nan.
  SDValue IsNan =
      DAG.getNode(RISCVISD::SETCC_VL, DL, Mask.getValueType(),
                  {VectorVal, VectorVal, DAG.getCondCode(ISD::SETNE),
                   DAG.getUNDEF(Mask.getValueType()), Mask, VL});
  MVT XLenVT = Subtarget.getXLenVT();
  SDValue CPop = DAG.getNode(RISCVISD::VCPOP_VL, DL, XLenVT, IsNan, Mask, VL);
  SDValue NoNaNs = DAG.getSetCC(DL, XLenVT, CPop,
                                DAG.getConstant(0, DL, XLenVT), ISD::SETEQ);
  return DAG.getSelect(
      DL, ResVT, NoNaNs, Res,
      DAG.getConstantFP(APFloat::getNaN(DAG.EVTToAPFloatSemantics(ResVT)), DL,
                        ResVT));
}

SDValue RISCVTargetLowering::lowerVPREDUCE(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  unsigned Opc = Op.getOpcode();
  SDValue Start = Op.getOperand(0);
  SDValue Vec = Op.getOperand(1);
  EVT VecEVT = Vec.getValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  // TODO: The type may need to be widened rather than split. Or widened before
  // it can be split.
  if (!isTypeLegal(VecEVT))
    return SDValue();

  MVT VecVT = VecEVT.getSimpleVT();
  unsigned RVVOpcode = getRVVReductionOp(Opc);

  if (VecVT.isFixedLengthVector()) {
    auto ContainerVT = getContainerForFixedLengthVector(VecVT);
    Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
  }

  SDValue VL = Op.getOperand(3);
  SDValue Mask = Op.getOperand(2);
  SDValue Res =
      lowerReductionSeq(RVVOpcode, Op.getSimpleValueType(), Op.getOperand(0),
                        Vec, Mask, VL, DL, DAG, Subtarget);
  if ((Opc != ISD::VP_REDUCE_FMINIMUM && Opc != ISD::VP_REDUCE_FMAXIMUM) ||
      Op->getFlags().hasNoNaNs())
    return Res;

  // Propagate NaNs.
  MVT PredVT = getMaskTypeFor(Vec.getSimpleValueType());
  // Check if any of the elements in Vec is NaN.
  SDValue IsNaN = DAG.getNode(
      RISCVISD::SETCC_VL, DL, PredVT,
      {Vec, Vec, DAG.getCondCode(ISD::SETNE), DAG.getUNDEF(PredVT), Mask, VL});
  SDValue VCPop = DAG.getNode(RISCVISD::VCPOP_VL, DL, XLenVT, IsNaN, Mask, VL);
  // Check if the start value is NaN.
  SDValue StartIsNaN = DAG.getSetCC(DL, XLenVT, Start, Start, ISD::SETUO);
  VCPop = DAG.getNode(ISD::OR, DL, XLenVT, VCPop, StartIsNaN);
  SDValue NoNaNs = DAG.getSetCC(DL, XLenVT, VCPop,
                                DAG.getConstant(0, DL, XLenVT), ISD::SETEQ);
  MVT ResVT = Res.getSimpleValueType();
  return DAG.getSelect(
      DL, ResVT, NoNaNs, Res,
      DAG.getConstantFP(APFloat::getNaN(DAG.EVTToAPFloatSemantics(ResVT)), DL,
                        ResVT));
}

SDValue RISCVTargetLowering::lowerINSERT_SUBVECTOR(SDValue Op,
                                                   SelectionDAG &DAG) const {
  SDValue Vec = Op.getOperand(0);
  SDValue SubVec = Op.getOperand(1);
  MVT VecVT = Vec.getSimpleValueType();
  MVT SubVecVT = SubVec.getSimpleValueType();

  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();
  unsigned OrigIdx = Op.getConstantOperandVal(2);
  const RISCVRegisterInfo *TRI = Subtarget.getRegisterInfo();

  // We don't have the ability to slide mask vectors up indexed by their i1
  // elements; the smallest we can do is i8. Often we are able to bitcast to
  // equivalent i8 vectors. Note that when inserting a fixed-length vector
  // into a scalable one, we might not necessarily have enough scalable
  // elements to safely divide by 8: nxv1i1 = insert nxv1i1, v4i1 is valid.
  if (SubVecVT.getVectorElementType() == MVT::i1 &&
      (OrigIdx != 0 || !Vec.isUndef())) {
    if (VecVT.getVectorMinNumElements() >= 8 &&
        SubVecVT.getVectorMinNumElements() >= 8) {
      assert(OrigIdx % 8 == 0 && "Invalid index");
      assert(VecVT.getVectorMinNumElements() % 8 == 0 &&
             SubVecVT.getVectorMinNumElements() % 8 == 0 &&
             "Unexpected mask vector lowering");
      OrigIdx /= 8;
      SubVecVT =
          MVT::getVectorVT(MVT::i8, SubVecVT.getVectorMinNumElements() / 8,
                           SubVecVT.isScalableVector());
      VecVT = MVT::getVectorVT(MVT::i8, VecVT.getVectorMinNumElements() / 8,
                               VecVT.isScalableVector());
      Vec = DAG.getBitcast(VecVT, Vec);
      SubVec = DAG.getBitcast(SubVecVT, SubVec);
    } else {
      // We can't slide this mask vector up indexed by its i1 elements.
      // This poses a problem when we wish to insert a scalable vector which
      // can't be re-expressed as a larger type. Just choose the slow path and
      // extend to a larger type, then truncate back down.
      MVT ExtVecVT = VecVT.changeVectorElementType(MVT::i8);
      MVT ExtSubVecVT = SubVecVT.changeVectorElementType(MVT::i8);
      Vec = DAG.getNode(ISD::ZERO_EXTEND, DL, ExtVecVT, Vec);
      SubVec = DAG.getNode(ISD::ZERO_EXTEND, DL, ExtSubVecVT, SubVec);
      Vec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ExtVecVT, Vec, SubVec,
                        Op.getOperand(2));
      SDValue SplatZero = DAG.getConstant(0, DL, ExtVecVT);
      return DAG.getSetCC(DL, VecVT, Vec, SplatZero, ISD::SETNE);
    }
  }

  // If the subvector vector is a fixed-length type and we don't know VLEN
  // exactly, we cannot use subregister manipulation to simplify the codegen; we
  // don't know which register of a LMUL group contains the specific subvector
  // as we only know the minimum register size. Therefore we must slide the
  // vector group up the full amount.
  const auto VLen = Subtarget.getRealVLen();
  if (SubVecVT.isFixedLengthVector() && !VLen) {
    if (OrigIdx == 0 && Vec.isUndef() && !VecVT.isFixedLengthVector())
      return Op;
    MVT ContainerVT = VecVT;
    if (VecVT.isFixedLengthVector()) {
      ContainerVT = getContainerForFixedLengthVector(VecVT);
      Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
    }

    if (OrigIdx == 0 && Vec.isUndef() && VecVT.isFixedLengthVector()) {
      SubVec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ContainerVT,
                           DAG.getUNDEF(ContainerVT), SubVec,
                           DAG.getVectorIdxConstant(0, DL));
      SubVec = convertFromScalableVector(VecVT, SubVec, DAG, Subtarget);
      return DAG.getBitcast(Op.getValueType(), SubVec);
    }

    SubVec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ContainerVT,
                         DAG.getUNDEF(ContainerVT), SubVec,
                         DAG.getVectorIdxConstant(0, DL));
    SDValue Mask =
        getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget).first;
    // Set the vector length to only the number of elements we care about. Note
    // that for slideup this includes the offset.
    unsigned EndIndex = OrigIdx + SubVecVT.getVectorNumElements();
    SDValue VL = getVLOp(EndIndex, ContainerVT, DL, DAG, Subtarget);

    // Use tail agnostic policy if we're inserting over Vec's tail.
    unsigned Policy = RISCVII::TAIL_UNDISTURBED_MASK_UNDISTURBED;
    if (VecVT.isFixedLengthVector() && EndIndex == VecVT.getVectorNumElements())
      Policy = RISCVII::TAIL_AGNOSTIC;

    // If we're inserting into the lowest elements, use a tail undisturbed
    // vmv.v.v.
    if (OrigIdx == 0) {
      SubVec =
          DAG.getNode(RISCVISD::VMV_V_V_VL, DL, ContainerVT, Vec, SubVec, VL);
    } else {
      SDValue SlideupAmt = DAG.getConstant(OrigIdx, DL, XLenVT);
      SubVec = getVSlideup(DAG, Subtarget, DL, ContainerVT, Vec, SubVec,
                           SlideupAmt, Mask, VL, Policy);
    }

    if (VecVT.isFixedLengthVector())
      SubVec = convertFromScalableVector(VecVT, SubVec, DAG, Subtarget);
    return DAG.getBitcast(Op.getValueType(), SubVec);
  }

  MVT ContainerVecVT = VecVT;
  if (VecVT.isFixedLengthVector()) {
    ContainerVecVT = getContainerForFixedLengthVector(VecVT);
    Vec = convertToScalableVector(ContainerVecVT, Vec, DAG, Subtarget);
  }

  MVT ContainerSubVecVT = SubVecVT;
  if (SubVecVT.isFixedLengthVector()) {
    ContainerSubVecVT = getContainerForFixedLengthVector(SubVecVT);
    SubVec = convertToScalableVector(ContainerSubVecVT, SubVec, DAG, Subtarget);
  }

  unsigned SubRegIdx;
  ElementCount RemIdx;
  // insert_subvector scales the index by vscale if the subvector is scalable,
  // and decomposeSubvectorInsertExtractToSubRegs takes this into account. So if
  // we have a fixed length subvector, we need to adjust the index by 1/vscale.
  if (SubVecVT.isFixedLengthVector()) {
    assert(VLen);
    unsigned Vscale = *VLen / RISCV::RVVBitsPerBlock;
    auto Decompose =
        RISCVTargetLowering::decomposeSubvectorInsertExtractToSubRegs(
            ContainerVecVT, ContainerSubVecVT, OrigIdx / Vscale, TRI);
    SubRegIdx = Decompose.first;
    RemIdx = ElementCount::getFixed((Decompose.second * Vscale) +
                                    (OrigIdx % Vscale));
  } else {
    auto Decompose =
        RISCVTargetLowering::decomposeSubvectorInsertExtractToSubRegs(
            ContainerVecVT, ContainerSubVecVT, OrigIdx, TRI);
    SubRegIdx = Decompose.first;
    RemIdx = ElementCount::getScalable(Decompose.second);
  }

  TypeSize VecRegSize = TypeSize::getScalable(RISCV::RVVBitsPerBlock);
  assert(isPowerOf2_64(
      Subtarget.expandVScale(SubVecVT.getSizeInBits()).getKnownMinValue()));
  bool ExactlyVecRegSized =
      Subtarget.expandVScale(SubVecVT.getSizeInBits())
          .isKnownMultipleOf(Subtarget.expandVScale(VecRegSize));

  // 1. If the Idx has been completely eliminated and this subvector's size is
  // a vector register or a multiple thereof, or the surrounding elements are
  // undef, then this is a subvector insert which naturally aligns to a vector
  // register. These can easily be handled using subregister manipulation.
  // 2. If the subvector isn't an exact multiple of a valid register group size,
  // then the insertion must preserve the undisturbed elements of the register.
  // We do this by lowering to an EXTRACT_SUBVECTOR grabbing the nearest LMUL=1
  // vector type (which resolves to a subregister copy), performing a VSLIDEUP
  // to place the subvector within the vector register, and an INSERT_SUBVECTOR
  // of that LMUL=1 type back into the larger vector (resolving to another
  // subregister operation). See below for how our VSLIDEUP works. We go via a
  // LMUL=1 type to avoid allocating a large register group to hold our
  // subvector.
  if (RemIdx.isZero() && (ExactlyVecRegSized || Vec.isUndef())) {
    if (SubVecVT.isFixedLengthVector()) {
      // We may get NoSubRegister if inserting at index 0 and the subvec
      // container is the same as the vector, e.g. vec=v4i32,subvec=v4i32,idx=0
      if (SubRegIdx == RISCV::NoSubRegister) {
        assert(OrigIdx == 0);
        return Op;
      }

      SDValue Insert =
          DAG.getTargetInsertSubreg(SubRegIdx, DL, ContainerVecVT, Vec, SubVec);
      if (VecVT.isFixedLengthVector())
        Insert = convertFromScalableVector(VecVT, Insert, DAG, Subtarget);
      return Insert;
    }
    return Op;
  }

  // VSLIDEUP works by leaving elements 0<i<OFFSET undisturbed, elements
  // OFFSET<=i<VL set to the "subvector" and vl<=i<VLMAX set to the tail policy
  // (in our case undisturbed). This means we can set up a subvector insertion
  // where OFFSET is the insertion offset, and the VL is the OFFSET plus the
  // size of the subvector.
  MVT InterSubVT = ContainerVecVT;
  SDValue AlignedExtract = Vec;
  unsigned AlignedIdx = OrigIdx - RemIdx.getKnownMinValue();
  if (SubVecVT.isFixedLengthVector())
    AlignedIdx /= *VLen / RISCV::RVVBitsPerBlock;
  if (ContainerVecVT.bitsGT(getLMUL1VT(ContainerVecVT))) {
    InterSubVT = getLMUL1VT(ContainerVecVT);
    // Extract a subvector equal to the nearest full vector register type. This
    // should resolve to a EXTRACT_SUBREG instruction.
    AlignedExtract = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, InterSubVT, Vec,
                                 DAG.getVectorIdxConstant(AlignedIdx, DL));
  }

  SubVec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, InterSubVT,
                       DAG.getUNDEF(InterSubVT), SubVec,
                       DAG.getVectorIdxConstant(0, DL));

  auto [Mask, VL] = getDefaultVLOps(VecVT, ContainerVecVT, DL, DAG, Subtarget);

  ElementCount EndIndex = RemIdx + SubVecVT.getVectorElementCount();
  VL = DAG.getElementCount(DL, XLenVT, SubVecVT.getVectorElementCount());

  // Use tail agnostic policy if we're inserting over InterSubVT's tail.
  unsigned Policy = RISCVII::TAIL_UNDISTURBED_MASK_UNDISTURBED;
  if (Subtarget.expandVScale(EndIndex) ==
      Subtarget.expandVScale(InterSubVT.getVectorElementCount()))
    Policy = RISCVII::TAIL_AGNOSTIC;

  // If we're inserting into the lowest elements, use a tail undisturbed
  // vmv.v.v.
  if (RemIdx.isZero()) {
    SubVec = DAG.getNode(RISCVISD::VMV_V_V_VL, DL, InterSubVT, AlignedExtract,
                         SubVec, VL);
  } else {
    SDValue SlideupAmt = DAG.getElementCount(DL, XLenVT, RemIdx);

    // Construct the vector length corresponding to RemIdx + length(SubVecVT).
    VL = DAG.getNode(ISD::ADD, DL, XLenVT, SlideupAmt, VL);

    SubVec = getVSlideup(DAG, Subtarget, DL, InterSubVT, AlignedExtract, SubVec,
                         SlideupAmt, Mask, VL, Policy);
  }

  // If required, insert this subvector back into the correct vector register.
  // This should resolve to an INSERT_SUBREG instruction.
  if (ContainerVecVT.bitsGT(InterSubVT))
    SubVec = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ContainerVecVT, Vec, SubVec,
                         DAG.getVectorIdxConstant(AlignedIdx, DL));

  if (VecVT.isFixedLengthVector())
    SubVec = convertFromScalableVector(VecVT, SubVec, DAG, Subtarget);

  // We might have bitcast from a mask type: cast back to the original type if
  // required.
  return DAG.getBitcast(Op.getSimpleValueType(), SubVec);
}

SDValue RISCVTargetLowering::lowerEXTRACT_SUBVECTOR(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDValue Vec = Op.getOperand(0);
  MVT SubVecVT = Op.getSimpleValueType();
  MVT VecVT = Vec.getSimpleValueType();

  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();
  unsigned OrigIdx = Op.getConstantOperandVal(1);
  const RISCVRegisterInfo *TRI = Subtarget.getRegisterInfo();

  // We don't have the ability to slide mask vectors down indexed by their i1
  // elements; the smallest we can do is i8. Often we are able to bitcast to
  // equivalent i8 vectors. Note that when extracting a fixed-length vector
  // from a scalable one, we might not necessarily have enough scalable
  // elements to safely divide by 8: v8i1 = extract nxv1i1 is valid.
  if (SubVecVT.getVectorElementType() == MVT::i1 && OrigIdx != 0) {
    if (VecVT.getVectorMinNumElements() >= 8 &&
        SubVecVT.getVectorMinNumElements() >= 8) {
      assert(OrigIdx % 8 == 0 && "Invalid index");
      assert(VecVT.getVectorMinNumElements() % 8 == 0 &&
             SubVecVT.getVectorMinNumElements() % 8 == 0 &&
             "Unexpected mask vector lowering");
      OrigIdx /= 8;
      SubVecVT =
          MVT::getVectorVT(MVT::i8, SubVecVT.getVectorMinNumElements() / 8,
                           SubVecVT.isScalableVector());
      VecVT = MVT::getVectorVT(MVT::i8, VecVT.getVectorMinNumElements() / 8,
                               VecVT.isScalableVector());
      Vec = DAG.getBitcast(VecVT, Vec);
    } else {
      // We can't slide this mask vector down, indexed by its i1 elements.
      // This poses a problem when we wish to extract a scalable vector which
      // can't be re-expressed as a larger type. Just choose the slow path and
      // extend to a larger type, then truncate back down.
      // TODO: We could probably improve this when extracting certain fixed
      // from fixed, where we can extract as i8 and shift the correct element
      // right to reach the desired subvector?
      MVT ExtVecVT = VecVT.changeVectorElementType(MVT::i8);
      MVT ExtSubVecVT = SubVecVT.changeVectorElementType(MVT::i8);
      Vec = DAG.getNode(ISD::ZERO_EXTEND, DL, ExtVecVT, Vec);
      Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ExtSubVecVT, Vec,
                        Op.getOperand(1));
      SDValue SplatZero = DAG.getConstant(0, DL, ExtSubVecVT);
      return DAG.getSetCC(DL, SubVecVT, Vec, SplatZero, ISD::SETNE);
    }
  }

  // With an index of 0 this is a cast-like subvector, which can be performed
  // with subregister operations.
  if (OrigIdx == 0)
    return Op;

  const auto VLen = Subtarget.getRealVLen();

  // If the subvector vector is a fixed-length type and we don't know VLEN
  // exactly, we cannot use subregister manipulation to simplify the codegen; we
  // don't know which register of a LMUL group contains the specific subvector
  // as we only know the minimum register size. Therefore we must slide the
  // vector group down the full amount.
  if (SubVecVT.isFixedLengthVector() && !VLen) {
    MVT ContainerVT = VecVT;
    if (VecVT.isFixedLengthVector()) {
      ContainerVT = getContainerForFixedLengthVector(VecVT);
      Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
    }

    // Shrink down Vec so we're performing the slidedown on a smaller LMUL.
    unsigned LastIdx = OrigIdx + SubVecVT.getVectorNumElements() - 1;
    if (auto ShrunkVT =
            getSmallestVTForIndex(ContainerVT, LastIdx, DL, DAG, Subtarget)) {
      ContainerVT = *ShrunkVT;
      Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ContainerVT, Vec,
                        DAG.getVectorIdxConstant(0, DL));
    }

    SDValue Mask =
        getDefaultVLOps(VecVT, ContainerVT, DL, DAG, Subtarget).first;
    // Set the vector length to only the number of elements we care about. This
    // avoids sliding down elements we're going to discard straight away.
    SDValue VL = getVLOp(SubVecVT.getVectorNumElements(), ContainerVT, DL, DAG,
                         Subtarget);
    SDValue SlidedownAmt = DAG.getConstant(OrigIdx, DL, XLenVT);
    SDValue Slidedown =
        getVSlidedown(DAG, Subtarget, DL, ContainerVT,
                      DAG.getUNDEF(ContainerVT), Vec, SlidedownAmt, Mask, VL);
    // Now we can use a cast-like subvector extract to get the result.
    Slidedown = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SubVecVT, Slidedown,
                            DAG.getVectorIdxConstant(0, DL));
    return DAG.getBitcast(Op.getValueType(), Slidedown);
  }

  if (VecVT.isFixedLengthVector()) {
    VecVT = getContainerForFixedLengthVector(VecVT);
    Vec = convertToScalableVector(VecVT, Vec, DAG, Subtarget);
  }

  MVT ContainerSubVecVT = SubVecVT;
  if (SubVecVT.isFixedLengthVector())
    ContainerSubVecVT = getContainerForFixedLengthVector(SubVecVT);

  unsigned SubRegIdx;
  ElementCount RemIdx;
  // extract_subvector scales the index by vscale if the subvector is scalable,
  // and decomposeSubvectorInsertExtractToSubRegs takes this into account. So if
  // we have a fixed length subvector, we need to adjust the index by 1/vscale.
  if (SubVecVT.isFixedLengthVector()) {
    assert(VLen);
    unsigned Vscale = *VLen / RISCV::RVVBitsPerBlock;
    auto Decompose =
        RISCVTargetLowering::decomposeSubvectorInsertExtractToSubRegs(
            VecVT, ContainerSubVecVT, OrigIdx / Vscale, TRI);
    SubRegIdx = Decompose.first;
    RemIdx = ElementCount::getFixed((Decompose.second * Vscale) +
                                    (OrigIdx % Vscale));
  } else {
    auto Decompose =
        RISCVTargetLowering::decomposeSubvectorInsertExtractToSubRegs(
            VecVT, ContainerSubVecVT, OrigIdx, TRI);
    SubRegIdx = Decompose.first;
    RemIdx = ElementCount::getScalable(Decompose.second);
  }

  // If the Idx has been completely eliminated then this is a subvector extract
  // which naturally aligns to a vector register. These can easily be handled
  // using subregister manipulation.
  if (RemIdx.isZero()) {
    if (SubVecVT.isFixedLengthVector()) {
      Vec = DAG.getTargetExtractSubreg(SubRegIdx, DL, ContainerSubVecVT, Vec);
      return convertFromScalableVector(SubVecVT, Vec, DAG, Subtarget);
    }
    return Op;
  }

  // Else SubVecVT is M1 or smaller and may need to be slid down: if SubVecVT
  // was > M1 then the index would need to be a multiple of VLMAX, and so would
  // divide exactly.
  assert(RISCVVType::decodeVLMUL(getLMUL(ContainerSubVecVT)).second ||
         getLMUL(ContainerSubVecVT) == RISCVII::VLMUL::LMUL_1);

  // If the vector type is an LMUL-group type, extract a subvector equal to the
  // nearest full vector register type.
  MVT InterSubVT = VecVT;
  if (VecVT.bitsGT(getLMUL1VT(VecVT))) {
    // If VecVT has an LMUL > 1, then SubVecVT should have a smaller LMUL, and
    // we should have successfully decomposed the extract into a subregister.
    assert(SubRegIdx != RISCV::NoSubRegister);
    InterSubVT = getLMUL1VT(VecVT);
    Vec = DAG.getTargetExtractSubreg(SubRegIdx, DL, InterSubVT, Vec);
  }

  // Slide this vector register down by the desired number of elements in order
  // to place the desired subvector starting at element 0.
  SDValue SlidedownAmt = DAG.getElementCount(DL, XLenVT, RemIdx);
  auto [Mask, VL] = getDefaultScalableVLOps(InterSubVT, DL, DAG, Subtarget);
  if (SubVecVT.isFixedLengthVector())
    VL = getVLOp(SubVecVT.getVectorNumElements(), InterSubVT, DL, DAG,
                 Subtarget);
  SDValue Slidedown =
      getVSlidedown(DAG, Subtarget, DL, InterSubVT, DAG.getUNDEF(InterSubVT),
                    Vec, SlidedownAmt, Mask, VL);

  // Now the vector is in the right position, extract our final subvector. This
  // should resolve to a COPY.
  Slidedown = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SubVecVT, Slidedown,
                          DAG.getVectorIdxConstant(0, DL));

  // We might have bitcast from a mask type: cast back to the original type if
  // required.
  return DAG.getBitcast(Op.getSimpleValueType(), Slidedown);
}

// Widen a vector's operands to i8, then truncate its results back to the
// original type, typically i1.  All operand and result types must be the same.
static SDValue widenVectorOpsToi8(SDValue N, const SDLoc &DL,
                                  SelectionDAG &DAG) {
  MVT VT = N.getSimpleValueType();
  MVT WideVT = VT.changeVectorElementType(MVT::i8);
  SmallVector<SDValue, 4> WideOps;
  for (SDValue Op : N->ops()) {
    assert(Op.getSimpleValueType() == VT &&
           "Operands and result must be same type");
    WideOps.push_back(DAG.getNode(ISD::ZERO_EXTEND, DL, WideVT, Op));
  }

  unsigned NumVals = N->getNumValues();

  SDVTList VTs = DAG.getVTList(SmallVector<EVT, 4>(
      NumVals, N.getValueType().changeVectorElementType(MVT::i8)));
  SDValue WideN = DAG.getNode(N.getOpcode(), DL, VTs, WideOps);
  SmallVector<SDValue, 4> TruncVals;
  for (unsigned I = 0; I < NumVals; I++) {
    TruncVals.push_back(
        DAG.getSetCC(DL, N->getSimpleValueType(I), WideN.getValue(I),
                     DAG.getConstant(0, DL, WideVT), ISD::SETNE));
  }

  if (TruncVals.size() > 1)
    return DAG.getMergeValues(TruncVals, DL);
  return TruncVals.front();
}

SDValue RISCVTargetLowering::lowerVECTOR_DEINTERLEAVE(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VecVT = Op.getSimpleValueType();

  assert(VecVT.isScalableVector() &&
         "vector_interleave on non-scalable vector!");

  // 1 bit element vectors need to be widened to e8
  if (VecVT.getVectorElementType() == MVT::i1)
    return widenVectorOpsToi8(Op, DL, DAG);

  // If the VT is LMUL=8, we need to split and reassemble.
  if (VecVT.getSizeInBits().getKnownMinValue() ==
      (8 * RISCV::RVVBitsPerBlock)) {
    auto [Op0Lo, Op0Hi] = DAG.SplitVectorOperand(Op.getNode(), 0);
    auto [Op1Lo, Op1Hi] = DAG.SplitVectorOperand(Op.getNode(), 1);
    EVT SplitVT = Op0Lo.getValueType();

    SDValue ResLo = DAG.getNode(ISD::VECTOR_DEINTERLEAVE, DL,
                                DAG.getVTList(SplitVT, SplitVT), Op0Lo, Op0Hi);
    SDValue ResHi = DAG.getNode(ISD::VECTOR_DEINTERLEAVE, DL,
                                DAG.getVTList(SplitVT, SplitVT), Op1Lo, Op1Hi);

    SDValue Even = DAG.getNode(ISD::CONCAT_VECTORS, DL, VecVT,
                               ResLo.getValue(0), ResHi.getValue(0));
    SDValue Odd = DAG.getNode(ISD::CONCAT_VECTORS, DL, VecVT, ResLo.getValue(1),
                              ResHi.getValue(1));
    return DAG.getMergeValues({Even, Odd}, DL);
  }

  // Concatenate the two vectors as one vector to deinterleave
  MVT ConcatVT =
      MVT::getVectorVT(VecVT.getVectorElementType(),
                       VecVT.getVectorElementCount().multiplyCoefficientBy(2));
  SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, DL, ConcatVT,
                               Op.getOperand(0), Op.getOperand(1));

  // We want to operate on all lanes, so get the mask and VL and mask for it
  auto [Mask, VL] = getDefaultScalableVLOps(ConcatVT, DL, DAG, Subtarget);
  SDValue Passthru = DAG.getUNDEF(ConcatVT);

  // We can deinterleave through vnsrl.wi if the element type is smaller than
  // ELEN
  if (VecVT.getScalarSizeInBits() < Subtarget.getELen()) {
    SDValue Even =
        getDeinterleaveViaVNSRL(DL, VecVT, Concat, true, Subtarget, DAG);
    SDValue Odd =
        getDeinterleaveViaVNSRL(DL, VecVT, Concat, false, Subtarget, DAG);
    return DAG.getMergeValues({Even, Odd}, DL);
  }

  // For the indices, use the same SEW to avoid an extra vsetvli
  MVT IdxVT = ConcatVT.changeVectorElementTypeToInteger();
  // Create a vector of even indices {0, 2, 4, ...}
  SDValue EvenIdx =
      DAG.getStepVector(DL, IdxVT, APInt(IdxVT.getScalarSizeInBits(), 2));
  // Create a vector of odd indices {1, 3, 5, ... }
  SDValue OddIdx =
      DAG.getNode(ISD::ADD, DL, IdxVT, EvenIdx, DAG.getConstant(1, DL, IdxVT));

  // Gather the even and odd elements into two separate vectors
  SDValue EvenWide = DAG.getNode(RISCVISD::VRGATHER_VV_VL, DL, ConcatVT,
                                 Concat, EvenIdx, Passthru, Mask, VL);
  SDValue OddWide = DAG.getNode(RISCVISD::VRGATHER_VV_VL, DL, ConcatVT,
                                Concat, OddIdx, Passthru, Mask, VL);

  // Extract the result half of the gather for even and odd
  SDValue Even = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VecVT, EvenWide,
                             DAG.getVectorIdxConstant(0, DL));
  SDValue Odd = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VecVT, OddWide,
                            DAG.getVectorIdxConstant(0, DL));

  return DAG.getMergeValues({Even, Odd}, DL);
}

SDValue RISCVTargetLowering::lowerVECTOR_INTERLEAVE(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VecVT = Op.getSimpleValueType();

  assert(VecVT.isScalableVector() &&
         "vector_interleave on non-scalable vector!");

  // i1 vectors need to be widened to i8
  if (VecVT.getVectorElementType() == MVT::i1)
    return widenVectorOpsToi8(Op, DL, DAG);

  MVT XLenVT = Subtarget.getXLenVT();
  SDValue VL = DAG.getRegister(RISCV::X0, XLenVT);

  // If the VT is LMUL=8, we need to split and reassemble.
  if (VecVT.getSizeInBits().getKnownMinValue() == (8 * RISCV::RVVBitsPerBlock)) {
    auto [Op0Lo, Op0Hi] = DAG.SplitVectorOperand(Op.getNode(), 0);
    auto [Op1Lo, Op1Hi] = DAG.SplitVectorOperand(Op.getNode(), 1);
    EVT SplitVT = Op0Lo.getValueType();

    SDValue ResLo = DAG.getNode(ISD::VECTOR_INTERLEAVE, DL,
                                DAG.getVTList(SplitVT, SplitVT), Op0Lo, Op1Lo);
    SDValue ResHi = DAG.getNode(ISD::VECTOR_INTERLEAVE, DL,
                                DAG.getVTList(SplitVT, SplitVT), Op0Hi, Op1Hi);

    SDValue Lo = DAG.getNode(ISD::CONCAT_VECTORS, DL, VecVT,
                             ResLo.getValue(0), ResLo.getValue(1));
    SDValue Hi = DAG.getNode(ISD::CONCAT_VECTORS, DL, VecVT,
                             ResHi.getValue(0), ResHi.getValue(1));
    return DAG.getMergeValues({Lo, Hi}, DL);
  }

  SDValue Interleaved;

  // If the element type is smaller than ELEN, then we can interleave with
  // vwaddu.vv and vwmaccu.vx
  if (VecVT.getScalarSizeInBits() < Subtarget.getELen()) {
    Interleaved = getWideningInterleave(Op.getOperand(0), Op.getOperand(1), DL,
                                        DAG, Subtarget);
  } else {
    // Otherwise, fallback to using vrgathere16.vv
    MVT ConcatVT =
      MVT::getVectorVT(VecVT.getVectorElementType(),
                       VecVT.getVectorElementCount().multiplyCoefficientBy(2));
    SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, DL, ConcatVT,
                                 Op.getOperand(0), Op.getOperand(1));

    MVT IdxVT = ConcatVT.changeVectorElementType(MVT::i16);

    // 0 1 2 3 4 5 6 7 ...
    SDValue StepVec = DAG.getStepVector(DL, IdxVT);

    // 1 1 1 1 1 1 1 1 ...
    SDValue Ones = DAG.getSplatVector(IdxVT, DL, DAG.getConstant(1, DL, XLenVT));

    // 1 0 1 0 1 0 1 0 ...
    SDValue OddMask = DAG.getNode(ISD::AND, DL, IdxVT, StepVec, Ones);
    OddMask = DAG.getSetCC(
        DL, IdxVT.changeVectorElementType(MVT::i1), OddMask,
        DAG.getSplatVector(IdxVT, DL, DAG.getConstant(0, DL, XLenVT)),
        ISD::CondCode::SETNE);

    SDValue VLMax = DAG.getSplatVector(IdxVT, DL, computeVLMax(VecVT, DL, DAG));

    // Build up the index vector for interleaving the concatenated vector
    //      0      0      1      1      2      2      3      3 ...
    SDValue Idx = DAG.getNode(ISD::SRL, DL, IdxVT, StepVec, Ones);
    //      0      n      1    n+1      2    n+2      3    n+3 ...
    Idx =
        DAG.getNode(RISCVISD::ADD_VL, DL, IdxVT, Idx, VLMax, Idx, OddMask, VL);

    // Then perform the interleave
    //   v[0]   v[n]   v[1] v[n+1]   v[2] v[n+2]   v[3] v[n+3] ...
    SDValue TrueMask = getAllOnesMask(IdxVT, VL, DL, DAG);
    Interleaved = DAG.getNode(RISCVISD::VRGATHEREI16_VV_VL, DL, ConcatVT,
                              Concat, Idx, DAG.getUNDEF(ConcatVT), TrueMask, VL);
  }

  // Extract the two halves from the interleaved result
  SDValue Lo = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VecVT, Interleaved,
                           DAG.getVectorIdxConstant(0, DL));
  SDValue Hi = DAG.getNode(
      ISD::EXTRACT_SUBVECTOR, DL, VecVT, Interleaved,
      DAG.getVectorIdxConstant(VecVT.getVectorMinNumElements(), DL));

  return DAG.getMergeValues({Lo, Hi}, DL);
}

// Lower step_vector to the vid instruction. Any non-identity step value must
// be accounted for my manual expansion.
SDValue RISCVTargetLowering::lowerSTEP_VECTOR(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  assert(VT.isScalableVector() && "Expected scalable vector");
  MVT XLenVT = Subtarget.getXLenVT();
  auto [Mask, VL] = getDefaultScalableVLOps(VT, DL, DAG, Subtarget);
  SDValue StepVec = DAG.getNode(RISCVISD::VID_VL, DL, VT, Mask, VL);
  uint64_t StepValImm = Op.getConstantOperandVal(0);
  if (StepValImm != 1) {
    if (isPowerOf2_64(StepValImm)) {
      SDValue StepVal =
          DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, DAG.getUNDEF(VT),
                      DAG.getConstant(Log2_64(StepValImm), DL, XLenVT), VL);
      StepVec = DAG.getNode(ISD::SHL, DL, VT, StepVec, StepVal);
    } else {
      SDValue StepVal = lowerScalarSplat(
          SDValue(), DAG.getConstant(StepValImm, DL, VT.getVectorElementType()),
          VL, VT, DL, DAG, Subtarget);
      StepVec = DAG.getNode(ISD::MUL, DL, VT, StepVec, StepVal);
    }
  }
  return StepVec;
}

// Implement vector_reverse using vrgather.vv with indices determined by
// subtracting the id of each element from (VLMAX-1). This will convert
// the indices like so:
// (0, 1,..., VLMAX-2, VLMAX-1) -> (VLMAX-1, VLMAX-2,..., 1, 0).
// TODO: This code assumes VLMAX <= 65536 for LMUL=8 SEW=16.
SDValue RISCVTargetLowering::lowerVECTOR_REVERSE(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VecVT = Op.getSimpleValueType();
  if (VecVT.getVectorElementType() == MVT::i1) {
    MVT WidenVT = MVT::getVectorVT(MVT::i8, VecVT.getVectorElementCount());
    SDValue Op1 = DAG.getNode(ISD::ZERO_EXTEND, DL, WidenVT, Op.getOperand(0));
    SDValue Op2 = DAG.getNode(ISD::VECTOR_REVERSE, DL, WidenVT, Op1);
    return DAG.getNode(ISD::TRUNCATE, DL, VecVT, Op2);
  }
  unsigned EltSize = VecVT.getScalarSizeInBits();
  unsigned MinSize = VecVT.getSizeInBits().getKnownMinValue();
  unsigned VectorBitsMax = Subtarget.getRealMaxVLen();
  unsigned MaxVLMAX =
    RISCVTargetLowering::computeVLMAX(VectorBitsMax, EltSize, MinSize);

  unsigned GatherOpc = RISCVISD::VRGATHER_VV_VL;
  MVT IntVT = VecVT.changeVectorElementTypeToInteger();

  // If this is SEW=8 and VLMAX is potentially more than 256, we need
  // to use vrgatherei16.vv.
  // TODO: It's also possible to use vrgatherei16.vv for other types to
  // decrease register width for the index calculation.
  if (MaxVLMAX > 256 && EltSize == 8) {
    // If this is LMUL=8, we have to split before can use vrgatherei16.vv.
    // Reverse each half, then reassemble them in reverse order.
    // NOTE: It's also possible that after splitting that VLMAX no longer
    // requires vrgatherei16.vv.
    if (MinSize == (8 * RISCV::RVVBitsPerBlock)) {
      auto [Lo, Hi] = DAG.SplitVectorOperand(Op.getNode(), 0);
      auto [LoVT, HiVT] = DAG.GetSplitDestVTs(VecVT);
      Lo = DAG.getNode(ISD::VECTOR_REVERSE, DL, LoVT, Lo);
      Hi = DAG.getNode(ISD::VECTOR_REVERSE, DL, HiVT, Hi);
      // Reassemble the low and high pieces reversed.
      // FIXME: This is a CONCAT_VECTORS.
      SDValue Res =
          DAG.getNode(ISD::INSERT_SUBVECTOR, DL, VecVT, DAG.getUNDEF(VecVT), Hi,
                      DAG.getVectorIdxConstant(0, DL));
      return DAG.getNode(
          ISD::INSERT_SUBVECTOR, DL, VecVT, Res, Lo,
          DAG.getVectorIdxConstant(LoVT.getVectorMinNumElements(), DL));
    }

    // Just promote the int type to i16 which will double the LMUL.
    IntVT = MVT::getVectorVT(MVT::i16, VecVT.getVectorElementCount());
    GatherOpc = RISCVISD::VRGATHEREI16_VV_VL;
  }

  MVT XLenVT = Subtarget.getXLenVT();
  auto [Mask, VL] = getDefaultScalableVLOps(VecVT, DL, DAG, Subtarget);

  // Calculate VLMAX-1 for the desired SEW.
  SDValue VLMinus1 = DAG.getNode(ISD::SUB, DL, XLenVT,
                                 computeVLMax(VecVT, DL, DAG),
                                 DAG.getConstant(1, DL, XLenVT));

  // Splat VLMAX-1 taking care to handle SEW==64 on RV32.
  bool IsRV32E64 =
      !Subtarget.is64Bit() && IntVT.getVectorElementType() == MVT::i64;
  SDValue SplatVL;
  if (!IsRV32E64)
    SplatVL = DAG.getSplatVector(IntVT, DL, VLMinus1);
  else
    SplatVL = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, IntVT, DAG.getUNDEF(IntVT),
                          VLMinus1, DAG.getRegister(RISCV::X0, XLenVT));

  SDValue VID = DAG.getNode(RISCVISD::VID_VL, DL, IntVT, Mask, VL);
  SDValue Indices = DAG.getNode(RISCVISD::SUB_VL, DL, IntVT, SplatVL, VID,
                                DAG.getUNDEF(IntVT), Mask, VL);

  return DAG.getNode(GatherOpc, DL, VecVT, Op.getOperand(0), Indices,
                     DAG.getUNDEF(VecVT), Mask, VL);
}

SDValue RISCVTargetLowering::lowerVECTOR_SPLICE(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  MVT XLenVT = Subtarget.getXLenVT();
  MVT VecVT = Op.getSimpleValueType();

  SDValue VLMax = computeVLMax(VecVT, DL, DAG);

  int64_t ImmValue = cast<ConstantSDNode>(Op.getOperand(2))->getSExtValue();
  SDValue DownOffset, UpOffset;
  if (ImmValue >= 0) {
    // The operand is a TargetConstant, we need to rebuild it as a regular
    // constant.
    DownOffset = DAG.getConstant(ImmValue, DL, XLenVT);
    UpOffset = DAG.getNode(ISD::SUB, DL, XLenVT, VLMax, DownOffset);
  } else {
    // The operand is a TargetConstant, we need to rebuild it as a regular
    // constant rather than negating the original operand.
    UpOffset = DAG.getConstant(-ImmValue, DL, XLenVT);
    DownOffset = DAG.getNode(ISD::SUB, DL, XLenVT, VLMax, UpOffset);
  }

  SDValue TrueMask = getAllOnesMask(VecVT, VLMax, DL, DAG);

  SDValue SlideDown =
      getVSlidedown(DAG, Subtarget, DL, VecVT, DAG.getUNDEF(VecVT), V1,
                    DownOffset, TrueMask, UpOffset);
  return getVSlideup(DAG, Subtarget, DL, VecVT, SlideDown, V2, UpOffset,
                     TrueMask, DAG.getRegister(RISCV::X0, XLenVT),
                     RISCVII::TAIL_AGNOSTIC);
}

SDValue
RISCVTargetLowering::lowerFixedLengthVectorLoadToRVV(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDLoc DL(Op);
  auto *Load = cast<LoadSDNode>(Op);

  assert(allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                        Load->getMemoryVT(),
                                        *Load->getMemOperand()) &&
         "Expecting a correctly-aligned load");

  MVT VT = Op.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();
  MVT ContainerVT = getContainerForFixedLengthVector(VT);

  // If we know the exact VLEN and our fixed length vector completely fills
  // the container, use a whole register load instead.
  const auto [MinVLMAX, MaxVLMAX] =
      RISCVTargetLowering::computeVLMAXBounds(ContainerVT, Subtarget);
  if (MinVLMAX == MaxVLMAX && MinVLMAX == VT.getVectorNumElements() &&
      getLMUL1VT(ContainerVT).bitsLE(ContainerVT)) {
    MachineMemOperand *MMO = Load->getMemOperand();
    SDValue NewLoad =
        DAG.getLoad(ContainerVT, DL, Load->getChain(), Load->getBasePtr(),
                    MMO->getPointerInfo(), MMO->getBaseAlign(), MMO->getFlags(),
                    MMO->getAAInfo(), MMO->getRanges());
    SDValue Result = convertFromScalableVector(VT, NewLoad, DAG, Subtarget);
    return DAG.getMergeValues({Result, NewLoad.getValue(1)}, DL);
  }

  SDValue VL = getVLOp(VT.getVectorNumElements(), ContainerVT, DL, DAG, Subtarget);

  bool IsMaskOp = VT.getVectorElementType() == MVT::i1;
  SDValue IntID = DAG.getTargetConstant(
      IsMaskOp ? Intrinsic::riscv_vlm : Intrinsic::riscv_vle, DL, XLenVT);
  SmallVector<SDValue, 4> Ops{Load->getChain(), IntID};
  if (!IsMaskOp)
    Ops.push_back(DAG.getUNDEF(ContainerVT));
  Ops.push_back(Load->getBasePtr());
  Ops.push_back(VL);
  SDVTList VTs = DAG.getVTList({ContainerVT, MVT::Other});
  SDValue NewLoad =
      DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, VTs, Ops,
                              Load->getMemoryVT(), Load->getMemOperand());

  SDValue Result = convertFromScalableVector(VT, NewLoad, DAG, Subtarget);
  return DAG.getMergeValues({Result, NewLoad.getValue(1)}, DL);
}

SDValue
RISCVTargetLowering::lowerFixedLengthVectorStoreToRVV(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDLoc DL(Op);
  auto *Store = cast<StoreSDNode>(Op);

  assert(allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                        Store->getMemoryVT(),
                                        *Store->getMemOperand()) &&
         "Expecting a correctly-aligned store");

  SDValue StoreVal = Store->getValue();
  MVT VT = StoreVal.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  // If the size less than a byte, we need to pad with zeros to make a byte.
  if (VT.getVectorElementType() == MVT::i1 && VT.getVectorNumElements() < 8) {
    VT = MVT::v8i1;
    StoreVal =
        DAG.getNode(ISD::INSERT_SUBVECTOR, DL, VT, DAG.getConstant(0, DL, VT),
                    StoreVal, DAG.getVectorIdxConstant(0, DL));
  }

  MVT ContainerVT = getContainerForFixedLengthVector(VT);

  SDValue NewValue =
      convertToScalableVector(ContainerVT, StoreVal, DAG, Subtarget);


  // If we know the exact VLEN and our fixed length vector completely fills
  // the container, use a whole register store instead.
  const auto [MinVLMAX, MaxVLMAX] =
      RISCVTargetLowering::computeVLMAXBounds(ContainerVT, Subtarget);
  if (MinVLMAX == MaxVLMAX && MinVLMAX == VT.getVectorNumElements() &&
      getLMUL1VT(ContainerVT).bitsLE(ContainerVT)) {
    MachineMemOperand *MMO = Store->getMemOperand();
    return DAG.getStore(Store->getChain(), DL, NewValue, Store->getBasePtr(),
                        MMO->getPointerInfo(), MMO->getBaseAlign(),
                        MMO->getFlags(), MMO->getAAInfo());
  }

  SDValue VL = getVLOp(VT.getVectorNumElements(), ContainerVT, DL, DAG,
                       Subtarget);

  bool IsMaskOp = VT.getVectorElementType() == MVT::i1;
  SDValue IntID = DAG.getTargetConstant(
      IsMaskOp ? Intrinsic::riscv_vsm : Intrinsic::riscv_vse, DL, XLenVT);
  return DAG.getMemIntrinsicNode(
      ISD::INTRINSIC_VOID, DL, DAG.getVTList(MVT::Other),
      {Store->getChain(), IntID, NewValue, Store->getBasePtr(), VL},
      Store->getMemoryVT(), Store->getMemOperand());
}

SDValue RISCVTargetLowering::lowerMaskedLoad(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();

  const auto *MemSD = cast<MemSDNode>(Op);
  EVT MemVT = MemSD->getMemoryVT();
  MachineMemOperand *MMO = MemSD->getMemOperand();
  SDValue Chain = MemSD->getChain();
  SDValue BasePtr = MemSD->getBasePtr();

  SDValue Mask, PassThru, VL;
  if (const auto *VPLoad = dyn_cast<VPLoadSDNode>(Op)) {
    Mask = VPLoad->getMask();
    PassThru = DAG.getUNDEF(VT);
    VL = VPLoad->getVectorLength();
  } else {
    const auto *MLoad = cast<MaskedLoadSDNode>(Op);
    Mask = MLoad->getMask();
    PassThru = MLoad->getPassThru();
  }

  bool IsUnmasked = ISD::isConstantSplatVectorAllOnes(Mask.getNode());

  MVT XLenVT = Subtarget.getXLenVT();

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    PassThru = convertToScalableVector(ContainerVT, PassThru, DAG, Subtarget);
    if (!IsUnmasked) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }
  }

  if (!VL)
    VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;

  unsigned IntID =
      IsUnmasked ? Intrinsic::riscv_vle : Intrinsic::riscv_vle_mask;
  SmallVector<SDValue, 8> Ops{Chain, DAG.getTargetConstant(IntID, DL, XLenVT)};
  if (IsUnmasked)
    Ops.push_back(DAG.getUNDEF(ContainerVT));
  else
    Ops.push_back(PassThru);
  Ops.push_back(BasePtr);
  if (!IsUnmasked)
    Ops.push_back(Mask);
  Ops.push_back(VL);
  if (!IsUnmasked)
    Ops.push_back(DAG.getTargetConstant(RISCVII::TAIL_AGNOSTIC, DL, XLenVT));

  SDVTList VTs = DAG.getVTList({ContainerVT, MVT::Other});

  SDValue Result =
      DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, VTs, Ops, MemVT, MMO);
  Chain = Result.getValue(1);

  if (VT.isFixedLengthVector())
    Result = convertFromScalableVector(VT, Result, DAG, Subtarget);

  return DAG.getMergeValues({Result, Chain}, DL);
}

SDValue RISCVTargetLowering::lowerMaskedStore(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);

  const auto *MemSD = cast<MemSDNode>(Op);
  EVT MemVT = MemSD->getMemoryVT();
  MachineMemOperand *MMO = MemSD->getMemOperand();
  SDValue Chain = MemSD->getChain();
  SDValue BasePtr = MemSD->getBasePtr();
  SDValue Val, Mask, VL;

  bool IsCompressingStore = false;
  if (const auto *VPStore = dyn_cast<VPStoreSDNode>(Op)) {
    Val = VPStore->getValue();
    Mask = VPStore->getMask();
    VL = VPStore->getVectorLength();
  } else {
    const auto *MStore = cast<MaskedStoreSDNode>(Op);
    Val = MStore->getValue();
    Mask = MStore->getMask();
    IsCompressingStore = MStore->isCompressingStore();
  }

  bool IsUnmasked =
      ISD::isConstantSplatVectorAllOnes(Mask.getNode()) || IsCompressingStore;

  MVT VT = Val.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);

    Val = convertToScalableVector(ContainerVT, Val, DAG, Subtarget);
    if (!IsUnmasked || IsCompressingStore) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }
  }

  if (!VL)
    VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;

  if (IsCompressingStore) {
    Val = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, ContainerVT,
                      DAG.getConstant(Intrinsic::riscv_vcompress, DL, XLenVT),
                      DAG.getUNDEF(ContainerVT), Val, Mask, VL);
    VL =
        DAG.getNode(RISCVISD::VCPOP_VL, DL, XLenVT, Mask,
                    getAllOnesMask(Mask.getSimpleValueType(), VL, DL, DAG), VL);
  }

  unsigned IntID =
      IsUnmasked ? Intrinsic::riscv_vse : Intrinsic::riscv_vse_mask;
  SmallVector<SDValue, 8> Ops{Chain, DAG.getTargetConstant(IntID, DL, XLenVT)};
  Ops.push_back(Val);
  Ops.push_back(BasePtr);
  if (!IsUnmasked)
    Ops.push_back(Mask);
  Ops.push_back(VL);

  return DAG.getMemIntrinsicNode(ISD::INTRINSIC_VOID, DL,
                                 DAG.getVTList(MVT::Other), Ops, MemVT, MMO);
}

SDValue
RISCVTargetLowering::lowerFixedLengthVectorSetccToRVV(SDValue Op,
                                                      SelectionDAG &DAG) const {
  MVT InVT = Op.getOperand(0).getSimpleValueType();
  MVT ContainerVT = getContainerForFixedLengthVector(InVT);

  MVT VT = Op.getSimpleValueType();

  SDValue Op1 =
      convertToScalableVector(ContainerVT, Op.getOperand(0), DAG, Subtarget);
  SDValue Op2 =
      convertToScalableVector(ContainerVT, Op.getOperand(1), DAG, Subtarget);

  SDLoc DL(Op);
  auto [Mask, VL] = getDefaultVLOps(VT.getVectorNumElements(), ContainerVT, DL,
                                    DAG, Subtarget);
  MVT MaskVT = getMaskTypeFor(ContainerVT);

  SDValue Cmp =
      DAG.getNode(RISCVISD::SETCC_VL, DL, MaskVT,
                  {Op1, Op2, Op.getOperand(2), DAG.getUNDEF(MaskVT), Mask, VL});

  return convertFromScalableVector(VT, Cmp, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerVectorStrictFSetcc(SDValue Op,
                                                     SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  SDValue Op2 = Op.getOperand(2);
  SDValue CC = Op.getOperand(3);
  ISD::CondCode CCVal = cast<CondCodeSDNode>(CC)->get();
  MVT VT = Op.getSimpleValueType();
  MVT InVT = Op1.getSimpleValueType();

  // RVV VMFEQ/VMFNE ignores qNan, so we expand strict_fsetccs with OEQ/UNE
  // condition code.
  if (Opc == ISD::STRICT_FSETCCS) {
    // Expand strict_fsetccs(x, oeq) to
    // (and strict_fsetccs(x, y, oge), strict_fsetccs(x, y, ole))
    SDVTList VTList = Op->getVTList();
    if (CCVal == ISD::SETEQ || CCVal == ISD::SETOEQ) {
      SDValue OLECCVal = DAG.getCondCode(ISD::SETOLE);
      SDValue Tmp1 = DAG.getNode(ISD::STRICT_FSETCCS, DL, VTList, Chain, Op1,
                                 Op2, OLECCVal);
      SDValue Tmp2 = DAG.getNode(ISD::STRICT_FSETCCS, DL, VTList, Chain, Op2,
                                 Op1, OLECCVal);
      SDValue OutChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other,
                                     Tmp1.getValue(1), Tmp2.getValue(1));
      // Tmp1 and Tmp2 might be the same node.
      if (Tmp1 != Tmp2)
        Tmp1 = DAG.getNode(ISD::AND, DL, VT, Tmp1, Tmp2);
      return DAG.getMergeValues({Tmp1, OutChain}, DL);
    }

    // Expand (strict_fsetccs x, y, une) to (not (strict_fsetccs x, y, oeq))
    if (CCVal == ISD::SETNE || CCVal == ISD::SETUNE) {
      SDValue OEQCCVal = DAG.getCondCode(ISD::SETOEQ);
      SDValue OEQ = DAG.getNode(ISD::STRICT_FSETCCS, DL, VTList, Chain, Op1,
                                Op2, OEQCCVal);
      SDValue Res = DAG.getNOT(DL, OEQ, VT);
      return DAG.getMergeValues({Res, OEQ.getValue(1)}, DL);
    }
  }

  MVT ContainerInVT = InVT;
  if (InVT.isFixedLengthVector()) {
    ContainerInVT = getContainerForFixedLengthVector(InVT);
    Op1 = convertToScalableVector(ContainerInVT, Op1, DAG, Subtarget);
    Op2 = convertToScalableVector(ContainerInVT, Op2, DAG, Subtarget);
  }
  MVT MaskVT = getMaskTypeFor(ContainerInVT);

  auto [Mask, VL] = getDefaultVLOps(InVT, ContainerInVT, DL, DAG, Subtarget);

  SDValue Res;
  if (Opc == ISD::STRICT_FSETCC &&
      (CCVal == ISD::SETLT || CCVal == ISD::SETOLT || CCVal == ISD::SETLE ||
       CCVal == ISD::SETOLE)) {
    // VMFLT/VMFLE/VMFGT/VMFGE raise exception for qNan. Generate a mask to only
    // active when both input elements are ordered.
    SDValue True = getAllOnesMask(ContainerInVT, VL, DL, DAG);
    SDValue OrderMask1 = DAG.getNode(
        RISCVISD::STRICT_FSETCC_VL, DL, DAG.getVTList(MaskVT, MVT::Other),
        {Chain, Op1, Op1, DAG.getCondCode(ISD::SETOEQ), DAG.getUNDEF(MaskVT),
         True, VL});
    SDValue OrderMask2 = DAG.getNode(
        RISCVISD::STRICT_FSETCC_VL, DL, DAG.getVTList(MaskVT, MVT::Other),
        {Chain, Op2, Op2, DAG.getCondCode(ISD::SETOEQ), DAG.getUNDEF(MaskVT),
         True, VL});
    Mask =
        DAG.getNode(RISCVISD::VMAND_VL, DL, MaskVT, OrderMask1, OrderMask2, VL);
    // Use Mask as the merge operand to let the result be 0 if either of the
    // inputs is unordered.
    Res = DAG.getNode(RISCVISD::STRICT_FSETCCS_VL, DL,
                      DAG.getVTList(MaskVT, MVT::Other),
                      {Chain, Op1, Op2, CC, Mask, Mask, VL});
  } else {
    unsigned RVVOpc = Opc == ISD::STRICT_FSETCC ? RISCVISD::STRICT_FSETCC_VL
                                                : RISCVISD::STRICT_FSETCCS_VL;
    Res = DAG.getNode(RVVOpc, DL, DAG.getVTList(MaskVT, MVT::Other),
                      {Chain, Op1, Op2, CC, DAG.getUNDEF(MaskVT), Mask, VL});
  }

  if (VT.isFixedLengthVector()) {
    SDValue SubVec = convertFromScalableVector(VT, Res, DAG, Subtarget);
    return DAG.getMergeValues({SubVec, Res.getValue(1)}, DL);
  }
  return Res;
}

// Lower vector ABS to smax(X, sub(0, X)).
SDValue RISCVTargetLowering::lowerABS(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue X = Op.getOperand(0);

  assert((Op.getOpcode() == ISD::VP_ABS || VT.isFixedLengthVector()) &&
         "Unexpected type for ISD::ABS");

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    X = convertToScalableVector(ContainerVT, X, DAG, Subtarget);
  }

  SDValue Mask, VL;
  if (Op->getOpcode() == ISD::VP_ABS) {
    Mask = Op->getOperand(1);
    if (VT.isFixedLengthVector())
      Mask = convertToScalableVector(getMaskTypeFor(ContainerVT), Mask, DAG,
                                     Subtarget);
    VL = Op->getOperand(2);
  } else
    std::tie(Mask, VL) = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  SDValue SplatZero = DAG.getNode(
      RISCVISD::VMV_V_X_VL, DL, ContainerVT, DAG.getUNDEF(ContainerVT),
      DAG.getConstant(0, DL, Subtarget.getXLenVT()), VL);
  SDValue NegX = DAG.getNode(RISCVISD::SUB_VL, DL, ContainerVT, SplatZero, X,
                             DAG.getUNDEF(ContainerVT), Mask, VL);
  SDValue Max = DAG.getNode(RISCVISD::SMAX_VL, DL, ContainerVT, X, NegX,
                            DAG.getUNDEF(ContainerVT), Mask, VL);

  if (VT.isFixedLengthVector())
    Max = convertFromScalableVector(VT, Max, DAG, Subtarget);
  return Max;
}

SDValue RISCVTargetLowering::lowerFixedLengthVectorFCOPYSIGNToRVV(
    SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue Mag = Op.getOperand(0);
  SDValue Sign = Op.getOperand(1);
  assert(Mag.getValueType() == Sign.getValueType() &&
         "Can only handle COPYSIGN with matching types.");

  MVT ContainerVT = getContainerForFixedLengthVector(VT);
  Mag = convertToScalableVector(ContainerVT, Mag, DAG, Subtarget);
  Sign = convertToScalableVector(ContainerVT, Sign, DAG, Subtarget);

  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);

  SDValue CopySign = DAG.getNode(RISCVISD::FCOPYSIGN_VL, DL, ContainerVT, Mag,
                                 Sign, DAG.getUNDEF(ContainerVT), Mask, VL);

  return convertFromScalableVector(VT, CopySign, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerFixedLengthVectorSelectToRVV(
    SDValue Op, SelectionDAG &DAG) const {
  MVT VT = Op.getSimpleValueType();
  MVT ContainerVT = getContainerForFixedLengthVector(VT);

  MVT I1ContainerVT =
      MVT::getVectorVT(MVT::i1, ContainerVT.getVectorElementCount());

  SDValue CC =
      convertToScalableVector(I1ContainerVT, Op.getOperand(0), DAG, Subtarget);
  SDValue Op1 =
      convertToScalableVector(ContainerVT, Op.getOperand(1), DAG, Subtarget);
  SDValue Op2 =
      convertToScalableVector(ContainerVT, Op.getOperand(2), DAG, Subtarget);

  SDLoc DL(Op);
  SDValue VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;

  SDValue Select = DAG.getNode(RISCVISD::VMERGE_VL, DL, ContainerVT, CC, Op1,
                               Op2, DAG.getUNDEF(ContainerVT), VL);

  return convertFromScalableVector(VT, Select, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerToScalableOp(SDValue Op,
                                               SelectionDAG &DAG) const {
  unsigned NewOpc = getRISCVVLOp(Op);
  bool HasMergeOp = hasMergeOp(NewOpc);
  bool HasMask = hasMaskOp(NewOpc);

  MVT VT = Op.getSimpleValueType();
  MVT ContainerVT = getContainerForFixedLengthVector(VT);

  // Create list of operands by converting existing ones to scalable types.
  SmallVector<SDValue, 6> Ops;
  for (const SDValue &V : Op->op_values()) {
    assert(!isa<VTSDNode>(V) && "Unexpected VTSDNode node!");

    // Pass through non-vector operands.
    if (!V.getValueType().isVector()) {
      Ops.push_back(V);
      continue;
    }

    // "cast" fixed length vector to a scalable vector.
    assert(useRVVForFixedLengthVectorVT(V.getSimpleValueType()) &&
           "Only fixed length vectors are supported!");
    Ops.push_back(convertToScalableVector(ContainerVT, V, DAG, Subtarget));
  }

  SDLoc DL(Op);
  auto [Mask, VL] = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget);
  if (HasMergeOp)
    Ops.push_back(DAG.getUNDEF(ContainerVT));
  if (HasMask)
    Ops.push_back(Mask);
  Ops.push_back(VL);

  // StrictFP operations have two result values. Their lowered result should
  // have same result count.
  if (Op->isStrictFPOpcode()) {
    SDValue ScalableRes =
        DAG.getNode(NewOpc, DL, DAG.getVTList(ContainerVT, MVT::Other), Ops,
                    Op->getFlags());
    SDValue SubVec = convertFromScalableVector(VT, ScalableRes, DAG, Subtarget);
    return DAG.getMergeValues({SubVec, ScalableRes.getValue(1)}, DL);
  }

  SDValue ScalableRes =
      DAG.getNode(NewOpc, DL, ContainerVT, Ops, Op->getFlags());
  return convertFromScalableVector(VT, ScalableRes, DAG, Subtarget);
}

// Lower a VP_* ISD node to the corresponding RISCVISD::*_VL node:
// * Operands of each node are assumed to be in the same order.
// * The EVL operand is promoted from i32 to i64 on RV64.
// * Fixed-length vectors are converted to their scalable-vector container
//   types.
SDValue RISCVTargetLowering::lowerVPOp(SDValue Op, SelectionDAG &DAG) const {
  unsigned RISCVISDOpc = getRISCVVLOp(Op);
  bool HasMergeOp = hasMergeOp(RISCVISDOpc);

  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  SmallVector<SDValue, 4> Ops;

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector())
    ContainerVT = getContainerForFixedLengthVector(VT);

  for (const auto &OpIdx : enumerate(Op->ops())) {
    SDValue V = OpIdx.value();
    assert(!isa<VTSDNode>(V) && "Unexpected VTSDNode node!");
    // Add dummy merge value before the mask. Or if there isn't a mask, before
    // EVL.
    if (HasMergeOp) {
      auto MaskIdx = ISD::getVPMaskIdx(Op.getOpcode());
      if (MaskIdx) {
        if (*MaskIdx == OpIdx.index())
          Ops.push_back(DAG.getUNDEF(ContainerVT));
      } else if (ISD::getVPExplicitVectorLengthIdx(Op.getOpcode()) ==
                 OpIdx.index()) {
        if (Op.getOpcode() == ISD::VP_MERGE) {
          // For VP_MERGE, copy the false operand instead of an undef value.
          Ops.push_back(Ops.back());
        } else {
          assert(Op.getOpcode() == ISD::VP_SELECT);
          // For VP_SELECT, add an undef value.
          Ops.push_back(DAG.getUNDEF(ContainerVT));
        }
      }
    }
    // Pass through operands which aren't fixed-length vectors.
    if (!V.getValueType().isFixedLengthVector()) {
      Ops.push_back(V);
      continue;
    }
    // "cast" fixed length vector to a scalable vector.
    MVT OpVT = V.getSimpleValueType();
    MVT ContainerVT = getContainerForFixedLengthVector(OpVT);
    assert(useRVVForFixedLengthVectorVT(OpVT) &&
           "Only fixed length vectors are supported!");
    Ops.push_back(convertToScalableVector(ContainerVT, V, DAG, Subtarget));
  }

  if (!VT.isFixedLengthVector())
    return DAG.getNode(RISCVISDOpc, DL, VT, Ops, Op->getFlags());

  SDValue VPOp = DAG.getNode(RISCVISDOpc, DL, ContainerVT, Ops, Op->getFlags());

  return convertFromScalableVector(VT, VPOp, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerVPExtMaskOp(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();

  SDValue Src = Op.getOperand(0);
  // NOTE: Mask is dropped.
  SDValue VL = Op.getOperand(2);

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    MVT SrcVT = MVT::getVectorVT(MVT::i1, ContainerVT.getVectorElementCount());
    Src = convertToScalableVector(SrcVT, Src, DAG, Subtarget);
  }

  MVT XLenVT = Subtarget.getXLenVT();
  SDValue Zero = DAG.getConstant(0, DL, XLenVT);
  SDValue ZeroSplat = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                                  DAG.getUNDEF(ContainerVT), Zero, VL);

  SDValue SplatValue = DAG.getConstant(
      Op.getOpcode() == ISD::VP_ZERO_EXTEND ? 1 : -1, DL, XLenVT);
  SDValue Splat = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                              DAG.getUNDEF(ContainerVT), SplatValue, VL);

  SDValue Result = DAG.getNode(RISCVISD::VMERGE_VL, DL, ContainerVT, Src, Splat,
                               ZeroSplat, DAG.getUNDEF(ContainerVT), VL);
  if (!VT.isFixedLengthVector())
    return Result;
  return convertFromScalableVector(VT, Result, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerVPSetCCMaskOp(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();

  SDValue Op1 = Op.getOperand(0);
  SDValue Op2 = Op.getOperand(1);
  ISD::CondCode Condition = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  // NOTE: Mask is dropped.
  SDValue VL = Op.getOperand(4);

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    Op1 = convertToScalableVector(ContainerVT, Op1, DAG, Subtarget);
    Op2 = convertToScalableVector(ContainerVT, Op2, DAG, Subtarget);
  }

  SDValue Result;
  SDValue AllOneMask = DAG.getNode(RISCVISD::VMSET_VL, DL, ContainerVT, VL);

  switch (Condition) {
  default:
    break;
  // X != Y  --> (X^Y)
  case ISD::SETNE:
    Result = DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Op1, Op2, VL);
    break;
  // X == Y  --> ~(X^Y)
  case ISD::SETEQ: {
    SDValue Temp =
        DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Op1, Op2, VL);
    Result =
        DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Temp, AllOneMask, VL);
    break;
  }
  // X >s Y   -->  X == 0 & Y == 1  -->  ~X & Y
  // X <u Y   -->  X == 0 & Y == 1  -->  ~X & Y
  case ISD::SETGT:
  case ISD::SETULT: {
    SDValue Temp =
        DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Op1, AllOneMask, VL);
    Result = DAG.getNode(RISCVISD::VMAND_VL, DL, ContainerVT, Temp, Op2, VL);
    break;
  }
  // X <s Y   --> X == 1 & Y == 0  -->  ~Y & X
  // X >u Y   --> X == 1 & Y == 0  -->  ~Y & X
  case ISD::SETLT:
  case ISD::SETUGT: {
    SDValue Temp =
        DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Op2, AllOneMask, VL);
    Result = DAG.getNode(RISCVISD::VMAND_VL, DL, ContainerVT, Op1, Temp, VL);
    break;
  }
  // X >=s Y  --> X == 0 | Y == 1  -->  ~X | Y
  // X <=u Y  --> X == 0 | Y == 1  -->  ~X | Y
  case ISD::SETGE:
  case ISD::SETULE: {
    SDValue Temp =
        DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Op1, AllOneMask, VL);
    Result = DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Temp, Op2, VL);
    break;
  }
  // X <=s Y  --> X == 1 | Y == 0  -->  ~Y | X
  // X >=u Y  --> X == 1 | Y == 0  -->  ~Y | X
  case ISD::SETLE:
  case ISD::SETUGE: {
    SDValue Temp =
        DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Op2, AllOneMask, VL);
    Result = DAG.getNode(RISCVISD::VMXOR_VL, DL, ContainerVT, Temp, Op1, VL);
    break;
  }
  }

  if (!VT.isFixedLengthVector())
    return Result;
  return convertFromScalableVector(VT, Result, DAG, Subtarget);
}

// Lower Floating-Point/Integer Type-Convert VP SDNodes
SDValue RISCVTargetLowering::lowerVPFPIntConvOp(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);

  SDValue Src = Op.getOperand(0);
  SDValue Mask = Op.getOperand(1);
  SDValue VL = Op.getOperand(2);
  unsigned RISCVISDOpc = getRISCVVLOp(Op);

  MVT DstVT = Op.getSimpleValueType();
  MVT SrcVT = Src.getSimpleValueType();
  if (DstVT.isFixedLengthVector()) {
    DstVT = getContainerForFixedLengthVector(DstVT);
    SrcVT = getContainerForFixedLengthVector(SrcVT);
    Src = convertToScalableVector(SrcVT, Src, DAG, Subtarget);
    MVT MaskVT = getMaskTypeFor(DstVT);
    Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
  }

  unsigned DstEltSize = DstVT.getScalarSizeInBits();
  unsigned SrcEltSize = SrcVT.getScalarSizeInBits();

  SDValue Result;
  if (DstEltSize >= SrcEltSize) { // Single-width and widening conversion.
    if (SrcVT.isInteger()) {
      assert(DstVT.isFloatingPoint() && "Wrong input/output vector types");

      unsigned RISCVISDExtOpc = RISCVISDOpc == RISCVISD::SINT_TO_FP_VL
                                    ? RISCVISD::VSEXT_VL
                                    : RISCVISD::VZEXT_VL;

      // Do we need to do any pre-widening before converting?
      if (SrcEltSize == 1) {
        MVT IntVT = DstVT.changeVectorElementTypeToInteger();
        MVT XLenVT = Subtarget.getXLenVT();
        SDValue Zero = DAG.getConstant(0, DL, XLenVT);
        SDValue ZeroSplat = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, IntVT,
                                        DAG.getUNDEF(IntVT), Zero, VL);
        SDValue One = DAG.getConstant(
            RISCVISDExtOpc == RISCVISD::VZEXT_VL ? 1 : -1, DL, XLenVT);
        SDValue OneSplat = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, IntVT,
                                       DAG.getUNDEF(IntVT), One, VL);
        Src = DAG.getNode(RISCVISD::VMERGE_VL, DL, IntVT, Src, OneSplat,
                          ZeroSplat, DAG.getUNDEF(IntVT), VL);
      } else if (DstEltSize > (2 * SrcEltSize)) {
        // Widen before converting.
        MVT IntVT = MVT::getVectorVT(MVT::getIntegerVT(DstEltSize / 2),
                                     DstVT.getVectorElementCount());
        Src = DAG.getNode(RISCVISDExtOpc, DL, IntVT, Src, Mask, VL);
      }

      Result = DAG.getNode(RISCVISDOpc, DL, DstVT, Src, Mask, VL);
    } else {
      assert(SrcVT.isFloatingPoint() && DstVT.isInteger() &&
             "Wrong input/output vector types");

      // Convert f16 to f32 then convert f32 to i64.
      if (DstEltSize > (2 * SrcEltSize)) {
        assert(SrcVT.getVectorElementType() == MVT::f16 && "Unexpected type!");
        MVT InterimFVT =
            MVT::getVectorVT(MVT::f32, DstVT.getVectorElementCount());
        Src =
            DAG.getNode(RISCVISD::FP_EXTEND_VL, DL, InterimFVT, Src, Mask, VL);
      }

      Result = DAG.getNode(RISCVISDOpc, DL, DstVT, Src, Mask, VL);
    }
  } else { // Narrowing + Conversion
    if (SrcVT.isInteger()) {
      assert(DstVT.isFloatingPoint() && "Wrong input/output vector types");
      // First do a narrowing convert to an FP type half the size, then round
      // the FP type to a small FP type if needed.

      MVT InterimFVT = DstVT;
      if (SrcEltSize > (2 * DstEltSize)) {
        assert(SrcEltSize == (4 * DstEltSize) && "Unexpected types!");
        assert(DstVT.getVectorElementType() == MVT::f16 && "Unexpected type!");
        InterimFVT = MVT::getVectorVT(MVT::f32, DstVT.getVectorElementCount());
      }

      Result = DAG.getNode(RISCVISDOpc, DL, InterimFVT, Src, Mask, VL);

      if (InterimFVT != DstVT) {
        Src = Result;
        Result = DAG.getNode(RISCVISD::FP_ROUND_VL, DL, DstVT, Src, Mask, VL);
      }
    } else {
      assert(SrcVT.isFloatingPoint() && DstVT.isInteger() &&
             "Wrong input/output vector types");
      // First do a narrowing conversion to an integer half the size, then
      // truncate if needed.

      if (DstEltSize == 1) {
        // First convert to the same size integer, then convert to mask using
        // setcc.
        assert(SrcEltSize >= 16 && "Unexpected FP type!");
        MVT InterimIVT = MVT::getVectorVT(MVT::getIntegerVT(SrcEltSize),
                                          DstVT.getVectorElementCount());
        Result = DAG.getNode(RISCVISDOpc, DL, InterimIVT, Src, Mask, VL);

        // Compare the integer result to 0. The integer should be 0 or 1/-1,
        // otherwise the conversion was undefined.
        MVT XLenVT = Subtarget.getXLenVT();
        SDValue SplatZero = DAG.getConstant(0, DL, XLenVT);
        SplatZero = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, InterimIVT,
                                DAG.getUNDEF(InterimIVT), SplatZero, VL);
        Result = DAG.getNode(RISCVISD::SETCC_VL, DL, DstVT,
                             {Result, SplatZero, DAG.getCondCode(ISD::SETNE),
                              DAG.getUNDEF(DstVT), Mask, VL});
      } else {
        MVT InterimIVT = MVT::getVectorVT(MVT::getIntegerVT(SrcEltSize / 2),
                                          DstVT.getVectorElementCount());

        Result = DAG.getNode(RISCVISDOpc, DL, InterimIVT, Src, Mask, VL);

        while (InterimIVT != DstVT) {
          SrcEltSize /= 2;
          Src = Result;
          InterimIVT = MVT::getVectorVT(MVT::getIntegerVT(SrcEltSize / 2),
                                        DstVT.getVectorElementCount());
          Result = DAG.getNode(RISCVISD::TRUNCATE_VECTOR_VL, DL, InterimIVT,
                               Src, Mask, VL);
        }
      }
    }
  }

  MVT VT = Op.getSimpleValueType();
  if (!VT.isFixedLengthVector())
    return Result;
  return convertFromScalableVector(VT, Result, DAG, Subtarget);
}

SDValue
RISCVTargetLowering::lowerVPSpliceExperimental(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);

  SDValue Op1 = Op.getOperand(0);
  SDValue Op2 = Op.getOperand(1);
  SDValue Offset = Op.getOperand(2);
  SDValue Mask = Op.getOperand(3);
  SDValue EVL1 = Op.getOperand(4);
  SDValue EVL2 = Op.getOperand(5);

  const MVT XLenVT = Subtarget.getXLenVT();
  MVT VT = Op.getSimpleValueType();
  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    Op1 = convertToScalableVector(ContainerVT, Op1, DAG, Subtarget);
    Op2 = convertToScalableVector(ContainerVT, Op2, DAG, Subtarget);
    MVT MaskVT = getMaskTypeFor(ContainerVT);
    Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
  }

  // EVL1 may need to be extended to XLenVT with RV64LegalI32.
  EVL1 = DAG.getNode(ISD::ZERO_EXTEND, DL, XLenVT, EVL1);

  bool IsMaskVector = VT.getVectorElementType() == MVT::i1;
  if (IsMaskVector) {
    ContainerVT = ContainerVT.changeVectorElementType(MVT::i8);

    // Expand input operands
    SDValue SplatOneOp1 = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                                      DAG.getUNDEF(ContainerVT),
                                      DAG.getConstant(1, DL, XLenVT), EVL1);
    SDValue SplatZeroOp1 = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                                       DAG.getUNDEF(ContainerVT),
                                       DAG.getConstant(0, DL, XLenVT), EVL1);
    Op1 = DAG.getNode(RISCVISD::VMERGE_VL, DL, ContainerVT, Op1, SplatOneOp1,
                      SplatZeroOp1, DAG.getUNDEF(ContainerVT), EVL1);

    SDValue SplatOneOp2 = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                                      DAG.getUNDEF(ContainerVT),
                                      DAG.getConstant(1, DL, XLenVT), EVL2);
    SDValue SplatZeroOp2 = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                                       DAG.getUNDEF(ContainerVT),
                                       DAG.getConstant(0, DL, XLenVT), EVL2);
    Op2 = DAG.getNode(RISCVISD::VMERGE_VL, DL, ContainerVT, Op2, SplatOneOp2,
                      SplatZeroOp2, DAG.getUNDEF(ContainerVT), EVL2);
  }

  int64_t ImmValue = cast<ConstantSDNode>(Offset)->getSExtValue();
  SDValue DownOffset, UpOffset;
  if (ImmValue >= 0) {
    // The operand is a TargetConstant, we need to rebuild it as a regular
    // constant.
    DownOffset = DAG.getConstant(ImmValue, DL, XLenVT);
    UpOffset = DAG.getNode(ISD::SUB, DL, XLenVT, EVL1, DownOffset);
  } else {
    // The operand is a TargetConstant, we need to rebuild it as a regular
    // constant rather than negating the original operand.
    UpOffset = DAG.getConstant(-ImmValue, DL, XLenVT);
    DownOffset = DAG.getNode(ISD::SUB, DL, XLenVT, EVL1, UpOffset);
  }

  SDValue SlideDown =
      getVSlidedown(DAG, Subtarget, DL, ContainerVT, DAG.getUNDEF(ContainerVT),
                    Op1, DownOffset, Mask, UpOffset);
  SDValue Result = getVSlideup(DAG, Subtarget, DL, ContainerVT, SlideDown, Op2,
                               UpOffset, Mask, EVL2, RISCVII::TAIL_AGNOSTIC);

  if (IsMaskVector) {
    // Truncate Result back to a mask vector (Result has same EVL as Op2)
    Result = DAG.getNode(
        RISCVISD::SETCC_VL, DL, ContainerVT.changeVectorElementType(MVT::i1),
        {Result, DAG.getConstant(0, DL, ContainerVT),
         DAG.getCondCode(ISD::SETNE), DAG.getUNDEF(getMaskTypeFor(ContainerVT)),
         Mask, EVL2});
  }

  if (!VT.isFixedLengthVector())
    return Result;
  return convertFromScalableVector(VT, Result, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerVPSplatExperimental(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Val = Op.getOperand(0);
  SDValue Mask = Op.getOperand(1);
  SDValue VL = Op.getOperand(2);
  MVT VT = Op.getSimpleValueType();

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    MVT MaskVT = getMaskTypeFor(ContainerVT);
    Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
  }

  SDValue Result =
      lowerScalarSplat(SDValue(), Val, VL, ContainerVT, DL, DAG, Subtarget);

  if (!VT.isFixedLengthVector())
    return Result;
  return convertFromScalableVector(VT, Result, DAG, Subtarget);
}

SDValue
RISCVTargetLowering::lowerVPReverseExperimental(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  SDValue Op1 = Op.getOperand(0);
  SDValue Mask = Op.getOperand(1);
  SDValue EVL = Op.getOperand(2);

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    Op1 = convertToScalableVector(ContainerVT, Op1, DAG, Subtarget);
    MVT MaskVT = getMaskTypeFor(ContainerVT);
    Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
  }

  MVT GatherVT = ContainerVT;
  MVT IndicesVT = ContainerVT.changeVectorElementTypeToInteger();
  // Check if we are working with mask vectors
  bool IsMaskVector = ContainerVT.getVectorElementType() == MVT::i1;
  if (IsMaskVector) {
    GatherVT = IndicesVT = ContainerVT.changeVectorElementType(MVT::i8);

    // Expand input operand
    SDValue SplatOne = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, IndicesVT,
                                   DAG.getUNDEF(IndicesVT),
                                   DAG.getConstant(1, DL, XLenVT), EVL);
    SDValue SplatZero = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, IndicesVT,
                                    DAG.getUNDEF(IndicesVT),
                                    DAG.getConstant(0, DL, XLenVT), EVL);
    Op1 = DAG.getNode(RISCVISD::VMERGE_VL, DL, IndicesVT, Op1, SplatOne,
                      SplatZero, DAG.getUNDEF(IndicesVT), EVL);
  }

  unsigned EltSize = GatherVT.getScalarSizeInBits();
  unsigned MinSize = GatherVT.getSizeInBits().getKnownMinValue();
  unsigned VectorBitsMax = Subtarget.getRealMaxVLen();
  unsigned MaxVLMAX =
      RISCVTargetLowering::computeVLMAX(VectorBitsMax, EltSize, MinSize);

  unsigned GatherOpc = RISCVISD::VRGATHER_VV_VL;
  // If this is SEW=8 and VLMAX is unknown or more than 256, we need
  // to use vrgatherei16.vv.
  // TODO: It's also possible to use vrgatherei16.vv for other types to
  // decrease register width for the index calculation.
  // NOTE: This code assumes VLMAX <= 65536 for LMUL=8 SEW=16.
  if (MaxVLMAX > 256 && EltSize == 8) {
    // If this is LMUL=8, we have to split before using vrgatherei16.vv.
    // Split the vector in half and reverse each half using a full register
    // reverse.
    // Swap the halves and concatenate them.
    // Slide the concatenated result by (VLMax - VL).
    if (MinSize == (8 * RISCV::RVVBitsPerBlock)) {
      auto [LoVT, HiVT] = DAG.GetSplitDestVTs(GatherVT);
      auto [Lo, Hi] = DAG.SplitVector(Op1, DL);

      SDValue LoRev = DAG.getNode(ISD::VECTOR_REVERSE, DL, LoVT, Lo);
      SDValue HiRev = DAG.getNode(ISD::VECTOR_REVERSE, DL, HiVT, Hi);

      // Reassemble the low and high pieces reversed.
      // NOTE: this Result is unmasked (because we do not need masks for
      // shuffles). If in the future this has to change, we can use a SELECT_VL
      // between Result and UNDEF using the mask originally passed to VP_REVERSE
      SDValue Result =
          DAG.getNode(ISD::CONCAT_VECTORS, DL, GatherVT, HiRev, LoRev);

      // Slide off any elements from past EVL that were reversed into the low
      // elements.
      unsigned MinElts = GatherVT.getVectorMinNumElements();
      SDValue VLMax =
          DAG.getVScale(DL, XLenVT, APInt(XLenVT.getSizeInBits(), MinElts));
      SDValue Diff = DAG.getNode(ISD::SUB, DL, XLenVT, VLMax, EVL);

      Result = getVSlidedown(DAG, Subtarget, DL, GatherVT,
                             DAG.getUNDEF(GatherVT), Result, Diff, Mask, EVL);

      if (IsMaskVector) {
        // Truncate Result back to a mask vector
        Result =
            DAG.getNode(RISCVISD::SETCC_VL, DL, ContainerVT,
                        {Result, DAG.getConstant(0, DL, GatherVT),
                         DAG.getCondCode(ISD::SETNE),
                         DAG.getUNDEF(getMaskTypeFor(ContainerVT)), Mask, EVL});
      }

      if (!VT.isFixedLengthVector())
        return Result;
      return convertFromScalableVector(VT, Result, DAG, Subtarget);
    }

    // Just promote the int type to i16 which will double the LMUL.
    IndicesVT = MVT::getVectorVT(MVT::i16, IndicesVT.getVectorElementCount());
    GatherOpc = RISCVISD::VRGATHEREI16_VV_VL;
  }

  SDValue VID = DAG.getNode(RISCVISD::VID_VL, DL, IndicesVT, Mask, EVL);
  SDValue VecLen =
      DAG.getNode(ISD::SUB, DL, XLenVT, EVL, DAG.getConstant(1, DL, XLenVT));
  SDValue VecLenSplat = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, IndicesVT,
                                    DAG.getUNDEF(IndicesVT), VecLen, EVL);
  SDValue VRSUB = DAG.getNode(RISCVISD::SUB_VL, DL, IndicesVT, VecLenSplat, VID,
                              DAG.getUNDEF(IndicesVT), Mask, EVL);
  SDValue Result = DAG.getNode(GatherOpc, DL, GatherVT, Op1, VRSUB,
                               DAG.getUNDEF(GatherVT), Mask, EVL);

  if (IsMaskVector) {
    // Truncate Result back to a mask vector
    Result = DAG.getNode(
        RISCVISD::SETCC_VL, DL, ContainerVT,
        {Result, DAG.getConstant(0, DL, GatherVT), DAG.getCondCode(ISD::SETNE),
         DAG.getUNDEF(getMaskTypeFor(ContainerVT)), Mask, EVL});
  }

  if (!VT.isFixedLengthVector())
    return Result;
  return convertFromScalableVector(VT, Result, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerLogicVPOp(SDValue Op,
                                            SelectionDAG &DAG) const {
  MVT VT = Op.getSimpleValueType();
  if (VT.getVectorElementType() != MVT::i1)
    return lowerVPOp(Op, DAG);

  // It is safe to drop mask parameter as masked-off elements are undef.
  SDValue Op1 = Op->getOperand(0);
  SDValue Op2 = Op->getOperand(1);
  SDValue VL = Op->getOperand(3);

  MVT ContainerVT = VT;
  const bool IsFixed = VT.isFixedLengthVector();
  if (IsFixed) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    Op1 = convertToScalableVector(ContainerVT, Op1, DAG, Subtarget);
    Op2 = convertToScalableVector(ContainerVT, Op2, DAG, Subtarget);
  }

  SDLoc DL(Op);
  SDValue Val = DAG.getNode(getRISCVVLOp(Op), DL, ContainerVT, Op1, Op2, VL);
  if (!IsFixed)
    return Val;
  return convertFromScalableVector(VT, Val, DAG, Subtarget);
}

SDValue RISCVTargetLowering::lowerVPStridedLoad(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();
  MVT VT = Op.getSimpleValueType();
  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector())
    ContainerVT = getContainerForFixedLengthVector(VT);

  SDVTList VTs = DAG.getVTList({ContainerVT, MVT::Other});

  auto *VPNode = cast<VPStridedLoadSDNode>(Op);
  // Check if the mask is known to be all ones
  SDValue Mask = VPNode->getMask();
  bool IsUnmasked = ISD::isConstantSplatVectorAllOnes(Mask.getNode());

  SDValue IntID = DAG.getTargetConstant(IsUnmasked ? Intrinsic::riscv_vlse
                                                   : Intrinsic::riscv_vlse_mask,
                                        DL, XLenVT);
  SmallVector<SDValue, 8> Ops{VPNode->getChain(), IntID,
                              DAG.getUNDEF(ContainerVT), VPNode->getBasePtr(),
                              VPNode->getStride()};
  if (!IsUnmasked) {
    if (VT.isFixedLengthVector()) {
      MVT MaskVT = ContainerVT.changeVectorElementType(MVT::i1);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }
    Ops.push_back(Mask);
  }
  Ops.push_back(VPNode->getVectorLength());
  if (!IsUnmasked) {
    SDValue Policy = DAG.getTargetConstant(RISCVII::TAIL_AGNOSTIC, DL, XLenVT);
    Ops.push_back(Policy);
  }

  SDValue Result =
      DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, VTs, Ops,
                              VPNode->getMemoryVT(), VPNode->getMemOperand());
  SDValue Chain = Result.getValue(1);

  if (VT.isFixedLengthVector())
    Result = convertFromScalableVector(VT, Result, DAG, Subtarget);

  return DAG.getMergeValues({Result, Chain}, DL);
}

SDValue RISCVTargetLowering::lowerVPStridedStore(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();

  auto *VPNode = cast<VPStridedStoreSDNode>(Op);
  SDValue StoreVal = VPNode->getValue();
  MVT VT = StoreVal.getSimpleValueType();
  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    StoreVal = convertToScalableVector(ContainerVT, StoreVal, DAG, Subtarget);
  }

  // Check if the mask is known to be all ones
  SDValue Mask = VPNode->getMask();
  bool IsUnmasked = ISD::isConstantSplatVectorAllOnes(Mask.getNode());

  SDValue IntID = DAG.getTargetConstant(IsUnmasked ? Intrinsic::riscv_vsse
                                                   : Intrinsic::riscv_vsse_mask,
                                        DL, XLenVT);
  SmallVector<SDValue, 8> Ops{VPNode->getChain(), IntID, StoreVal,
                              VPNode->getBasePtr(), VPNode->getStride()};
  if (!IsUnmasked) {
    if (VT.isFixedLengthVector()) {
      MVT MaskVT = ContainerVT.changeVectorElementType(MVT::i1);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }
    Ops.push_back(Mask);
  }
  Ops.push_back(VPNode->getVectorLength());

  return DAG.getMemIntrinsicNode(ISD::INTRINSIC_VOID, DL, VPNode->getVTList(),
                                 Ops, VPNode->getMemoryVT(),
                                 VPNode->getMemOperand());
}

// Custom lower MGATHER/VP_GATHER to a legalized form for RVV. It will then be
// matched to a RVV indexed load. The RVV indexed load instructions only
// support the "unsigned unscaled" addressing mode; indices are implicitly
// zero-extended or truncated to XLEN and are treated as byte offsets. Any
// signed or scaled indexing is extended to the XLEN value type and scaled
// accordingly.
SDValue RISCVTargetLowering::lowerMaskedGather(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();

  const auto *MemSD = cast<MemSDNode>(Op.getNode());
  EVT MemVT = MemSD->getMemoryVT();
  MachineMemOperand *MMO = MemSD->getMemOperand();
  SDValue Chain = MemSD->getChain();
  SDValue BasePtr = MemSD->getBasePtr();

  [[maybe_unused]] ISD::LoadExtType LoadExtType;
  SDValue Index, Mask, PassThru, VL;

  if (auto *VPGN = dyn_cast<VPGatherSDNode>(Op.getNode())) {
    Index = VPGN->getIndex();
    Mask = VPGN->getMask();
    PassThru = DAG.getUNDEF(VT);
    VL = VPGN->getVectorLength();
    // VP doesn't support extending loads.
    LoadExtType = ISD::NON_EXTLOAD;
  } else {
    // Else it must be a MGATHER.
    auto *MGN = cast<MaskedGatherSDNode>(Op.getNode());
    Index = MGN->getIndex();
    Mask = MGN->getMask();
    PassThru = MGN->getPassThru();
    LoadExtType = MGN->getExtensionType();
  }

  MVT IndexVT = Index.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  assert(VT.getVectorElementCount() == IndexVT.getVectorElementCount() &&
         "Unexpected VTs!");
  assert(BasePtr.getSimpleValueType() == XLenVT && "Unexpected pointer type");
  // Targets have to explicitly opt-in for extending vector loads.
  assert(LoadExtType == ISD::NON_EXTLOAD &&
         "Unexpected extending MGATHER/VP_GATHER");

  // If the mask is known to be all ones, optimize to an unmasked intrinsic;
  // the selection of the masked intrinsics doesn't do this for us.
  bool IsUnmasked = ISD::isConstantSplatVectorAllOnes(Mask.getNode());

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    IndexVT = MVT::getVectorVT(IndexVT.getVectorElementType(),
                               ContainerVT.getVectorElementCount());

    Index = convertToScalableVector(IndexVT, Index, DAG, Subtarget);

    if (!IsUnmasked) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
      PassThru = convertToScalableVector(ContainerVT, PassThru, DAG, Subtarget);
    }
  }

  if (!VL)
    VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;

  if (XLenVT == MVT::i32 && IndexVT.getVectorElementType().bitsGT(XLenVT)) {
    IndexVT = IndexVT.changeVectorElementType(XLenVT);
    Index = DAG.getNode(ISD::TRUNCATE, DL, IndexVT, Index);
  }

  unsigned IntID =
      IsUnmasked ? Intrinsic::riscv_vluxei : Intrinsic::riscv_vluxei_mask;
  SmallVector<SDValue, 8> Ops{Chain, DAG.getTargetConstant(IntID, DL, XLenVT)};
  if (IsUnmasked)
    Ops.push_back(DAG.getUNDEF(ContainerVT));
  else
    Ops.push_back(PassThru);
  Ops.push_back(BasePtr);
  Ops.push_back(Index);
  if (!IsUnmasked)
    Ops.push_back(Mask);
  Ops.push_back(VL);
  if (!IsUnmasked)
    Ops.push_back(DAG.getTargetConstant(RISCVII::TAIL_AGNOSTIC, DL, XLenVT));

  SDVTList VTs = DAG.getVTList({ContainerVT, MVT::Other});
  SDValue Result =
      DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, VTs, Ops, MemVT, MMO);
  Chain = Result.getValue(1);

  if (VT.isFixedLengthVector())
    Result = convertFromScalableVector(VT, Result, DAG, Subtarget);

  return DAG.getMergeValues({Result, Chain}, DL);
}

// Custom lower MSCATTER/VP_SCATTER to a legalized form for RVV. It will then be
// matched to a RVV indexed store. The RVV indexed store instructions only
// support the "unsigned unscaled" addressing mode; indices are implicitly
// zero-extended or truncated to XLEN and are treated as byte offsets. Any
// signed or scaled indexing is extended to the XLEN value type and scaled
// accordingly.
SDValue RISCVTargetLowering::lowerMaskedScatter(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const auto *MemSD = cast<MemSDNode>(Op.getNode());
  EVT MemVT = MemSD->getMemoryVT();
  MachineMemOperand *MMO = MemSD->getMemOperand();
  SDValue Chain = MemSD->getChain();
  SDValue BasePtr = MemSD->getBasePtr();

  [[maybe_unused]] bool IsTruncatingStore = false;
  SDValue Index, Mask, Val, VL;

  if (auto *VPSN = dyn_cast<VPScatterSDNode>(Op.getNode())) {
    Index = VPSN->getIndex();
    Mask = VPSN->getMask();
    Val = VPSN->getValue();
    VL = VPSN->getVectorLength();
    // VP doesn't support truncating stores.
    IsTruncatingStore = false;
  } else {
    // Else it must be a MSCATTER.
    auto *MSN = cast<MaskedScatterSDNode>(Op.getNode());
    Index = MSN->getIndex();
    Mask = MSN->getMask();
    Val = MSN->getValue();
    IsTruncatingStore = MSN->isTruncatingStore();
  }

  MVT VT = Val.getSimpleValueType();
  MVT IndexVT = Index.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  assert(VT.getVectorElementCount() == IndexVT.getVectorElementCount() &&
         "Unexpected VTs!");
  assert(BasePtr.getSimpleValueType() == XLenVT && "Unexpected pointer type");
  // Targets have to explicitly opt-in for extending vector loads and
  // truncating vector stores.
  assert(!IsTruncatingStore && "Unexpected truncating MSCATTER/VP_SCATTER");

  // If the mask is known to be all ones, optimize to an unmasked intrinsic;
  // the selection of the masked intrinsics doesn't do this for us.
  bool IsUnmasked = ISD::isConstantSplatVectorAllOnes(Mask.getNode());

  MVT ContainerVT = VT;
  if (VT.isFixedLengthVector()) {
    ContainerVT = getContainerForFixedLengthVector(VT);
    IndexVT = MVT::getVectorVT(IndexVT.getVectorElementType(),
                               ContainerVT.getVectorElementCount());

    Index = convertToScalableVector(IndexVT, Index, DAG, Subtarget);
    Val = convertToScalableVector(ContainerVT, Val, DAG, Subtarget);

    if (!IsUnmasked) {
      MVT MaskVT = getMaskTypeFor(ContainerVT);
      Mask = convertToScalableVector(MaskVT, Mask, DAG, Subtarget);
    }
  }

  if (!VL)
    VL = getDefaultVLOps(VT, ContainerVT, DL, DAG, Subtarget).second;

  if (XLenVT == MVT::i32 && IndexVT.getVectorElementType().bitsGT(XLenVT)) {
    IndexVT = IndexVT.changeVectorElementType(XLenVT);
    Index = DAG.getNode(ISD::TRUNCATE, DL, IndexVT, Index);
  }

  unsigned IntID =
      IsUnmasked ? Intrinsic::riscv_vsoxei : Intrinsic::riscv_vsoxei_mask;
  SmallVector<SDValue, 8> Ops{Chain, DAG.getTargetConstant(IntID, DL, XLenVT)};
  Ops.push_back(Val);
  Ops.push_back(BasePtr);
  Ops.push_back(Index);
  if (!IsUnmasked)
    Ops.push_back(Mask);
  Ops.push_back(VL);

  return DAG.getMemIntrinsicNode(ISD::INTRINSIC_VOID, DL,
                                 DAG.getVTList(MVT::Other), Ops, MemVT, MMO);
}

SDValue RISCVTargetLowering::lowerGET_ROUNDING(SDValue Op,
                                               SelectionDAG &DAG) const {
  const MVT XLenVT = Subtarget.getXLenVT();
  SDLoc DL(Op);
  SDValue Chain = Op->getOperand(0);
  SDValue SysRegNo = DAG.getTargetConstant(
      RISCVSysReg::lookupSysRegByName("FRM")->Encoding, DL, XLenVT);
  SDVTList VTs = DAG.getVTList(XLenVT, MVT::Other);
  SDValue RM = DAG.getNode(RISCVISD::READ_CSR, DL, VTs, Chain, SysRegNo);

  // Encoding used for rounding mode in RISC-V differs from that used in
  // FLT_ROUNDS. To convert it the RISC-V rounding mode is used as an index in a
  // table, which consists of a sequence of 4-bit fields, each representing
  // corresponding FLT_ROUNDS mode.
  static const int Table =
      (int(RoundingMode::NearestTiesToEven) << 4 * RISCVFPRndMode::RNE) |
      (int(RoundingMode::TowardZero) << 4 * RISCVFPRndMode::RTZ) |
      (int(RoundingMode::TowardNegative) << 4 * RISCVFPRndMode::RDN) |
      (int(RoundingMode::TowardPositive) << 4 * RISCVFPRndMode::RUP) |
      (int(RoundingMode::NearestTiesToAway) << 4 * RISCVFPRndMode::RMM);

  SDValue Shift =
      DAG.getNode(ISD::SHL, DL, XLenVT, RM, DAG.getConstant(2, DL, XLenVT));
  SDValue Shifted = DAG.getNode(ISD::SRL, DL, XLenVT,
                                DAG.getConstant(Table, DL, XLenVT), Shift);
  SDValue Masked = DAG.getNode(ISD::AND, DL, XLenVT, Shifted,
                               DAG.getConstant(7, DL, XLenVT));

  return DAG.getMergeValues({Masked, Chain}, DL);
}

SDValue RISCVTargetLowering::lowerSET_ROUNDING(SDValue Op,
                                               SelectionDAG &DAG) const {
  const MVT XLenVT = Subtarget.getXLenVT();
  SDLoc DL(Op);
  SDValue Chain = Op->getOperand(0);
  SDValue RMValue = Op->getOperand(1);
  SDValue SysRegNo = DAG.getTargetConstant(
      RISCVSysReg::lookupSysRegByName("FRM")->Encoding, DL, XLenVT);

  // Encoding used for rounding mode in RISC-V differs from that used in
  // FLT_ROUNDS. To convert it the C rounding mode is used as an index in
  // a table, which consists of a sequence of 4-bit fields, each representing
  // corresponding RISC-V mode.
  static const unsigned Table =
      (RISCVFPRndMode::RNE << 4 * int(RoundingMode::NearestTiesToEven)) |
      (RISCVFPRndMode::RTZ << 4 * int(RoundingMode::TowardZero)) |
      (RISCVFPRndMode::RDN << 4 * int(RoundingMode::TowardNegative)) |
      (RISCVFPRndMode::RUP << 4 * int(RoundingMode::TowardPositive)) |
      (RISCVFPRndMode::RMM << 4 * int(RoundingMode::NearestTiesToAway));

  RMValue = DAG.getNode(ISD::ZERO_EXTEND, DL, XLenVT, RMValue);

  SDValue Shift = DAG.getNode(ISD::SHL, DL, XLenVT, RMValue,
                              DAG.getConstant(2, DL, XLenVT));
  SDValue Shifted = DAG.getNode(ISD::SRL, DL, XLenVT,
                                DAG.getConstant(Table, DL, XLenVT), Shift);
  RMValue = DAG.getNode(ISD::AND, DL, XLenVT, Shifted,
                        DAG.getConstant(0x7, DL, XLenVT));
  return DAG.getNode(RISCVISD::WRITE_CSR, DL, MVT::Other, Chain, SysRegNo,
                     RMValue);
}

SDValue RISCVTargetLowering::lowerEH_DWARF_CFA(SDValue Op,
                                               SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  bool isRISCV64 = Subtarget.is64Bit();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  int FI = MF.getFrameInfo().CreateFixedObject(isRISCV64 ? 8 : 4, 0, false);
  return DAG.getFrameIndex(FI, PtrVT);
}

// Returns the opcode of the target-specific SDNode that implements the 32-bit
// form of the given Opcode.
static RISCVISD::NodeType getRISCVWOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    llvm_unreachable("Unexpected opcode");
  case ISD::SHL:
    return RISCVISD::SLLW;
  case ISD::SRA:
    return RISCVISD::SRAW;
  case ISD::SRL:
    return RISCVISD::SRLW;
  case ISD::SDIV:
    return RISCVISD::DIVW;
  case ISD::UDIV:
    return RISCVISD::DIVUW;
  case ISD::UREM:
    return RISCVISD::REMUW;
  case ISD::ROTL:
    return RISCVISD::ROLW;
  case ISD::ROTR:
    return RISCVISD::RORW;
  }
}

// Converts the given i8/i16/i32 operation to a target-specific SelectionDAG
// node. Because i8/i16/i32 isn't a legal type for RV64, these operations would
// otherwise be promoted to i64, making it difficult to select the
// SLLW/DIVUW/.../*W later one because the fact the operation was originally of
// type i8/i16/i32 is lost.
static SDValue customLegalizeToWOp(SDNode *N, SelectionDAG &DAG,
                                   unsigned ExtOpc = ISD::ANY_EXTEND) {
  SDLoc DL(N);
  RISCVISD::NodeType WOpcode = getRISCVWOpcode(N->getOpcode());
  SDValue NewOp0 = DAG.getNode(ExtOpc, DL, MVT::i64, N->getOperand(0));
  SDValue NewOp1 = DAG.getNode(ExtOpc, DL, MVT::i64, N->getOperand(1));
  SDValue NewRes = DAG.getNode(WOpcode, DL, MVT::i64, NewOp0, NewOp1);
  // ReplaceNodeResults requires we maintain the same type for the return value.
  return DAG.getNode(ISD::TRUNCATE, DL, N->getValueType(0), NewRes);
}

// Converts the given 32-bit operation to a i64 operation with signed extension
// semantic to reduce the signed extension instructions.
static SDValue customLegalizeToWOpWithSExt(SDNode *N, SelectionDAG &DAG) {
  SDLoc DL(N);
  SDValue NewOp0 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(0));
  SDValue NewOp1 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
  SDValue NewWOp = DAG.getNode(N->getOpcode(), DL, MVT::i64, NewOp0, NewOp1);
  SDValue NewRes = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, NewWOp,
                               DAG.getValueType(MVT::i32));
  return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, NewRes);
}

void RISCVTargetLowering::ReplaceNodeResults(SDNode *N,
                                             SmallVectorImpl<SDValue> &Results,
                                             SelectionDAG &DAG) const {
  SDLoc DL(N);
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Don't know how to custom type legalize this operation!");
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT: {
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    bool IsStrict = N->isStrictFPOpcode();
    bool IsSigned = N->getOpcode() == ISD::FP_TO_SINT ||
                    N->getOpcode() == ISD::STRICT_FP_TO_SINT;
    SDValue Op0 = IsStrict ? N->getOperand(1) : N->getOperand(0);
    if (getTypeAction(*DAG.getContext(), Op0.getValueType()) !=
        TargetLowering::TypeSoftenFloat) {
      if (!isTypeLegal(Op0.getValueType()))
        return;
      if (IsStrict) {
        SDValue Chain = N->getOperand(0);
        // In absense of Zfh, promote f16 to f32, then convert.
        if (Op0.getValueType() == MVT::f16 &&
            !Subtarget.hasStdExtZfhOrZhinx()) {
          Op0 = DAG.getNode(ISD::STRICT_FP_EXTEND, DL, {MVT::f32, MVT::Other},
                            {Chain, Op0});
          Chain = Op0.getValue(1);
        }
        unsigned Opc = IsSigned ? RISCVISD::STRICT_FCVT_W_RV64
                                : RISCVISD::STRICT_FCVT_WU_RV64;
        SDVTList VTs = DAG.getVTList(MVT::i64, MVT::Other);
        SDValue Res = DAG.getNode(
            Opc, DL, VTs, Chain, Op0,
            DAG.getTargetConstant(RISCVFPRndMode::RTZ, DL, MVT::i64));
        Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
        Results.push_back(Res.getValue(1));
        return;
      }
      // For bf16, or f16 in absense of Zfh, promote [b]f16 to f32 and then
      // convert.
      if ((Op0.getValueType() == MVT::f16 &&
           !Subtarget.hasStdExtZfhOrZhinx()) ||
          Op0.getValueType() == MVT::bf16)
        Op0 = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, Op0);

      unsigned Opc = IsSigned ? RISCVISD::FCVT_W_RV64 : RISCVISD::FCVT_WU_RV64;
      SDValue Res =
          DAG.getNode(Opc, DL, MVT::i64, Op0,
                      DAG.getTargetConstant(RISCVFPRndMode::RTZ, DL, MVT::i64));
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    // If the FP type needs to be softened, emit a library call using the 'si'
    // version. If we left it to default legalization we'd end up with 'di'. If
    // the FP type doesn't need to be softened just let generic type
    // legalization promote the result type.
    RTLIB::Libcall LC;
    if (IsSigned)
      LC = RTLIB::getFPTOSINT(Op0.getValueType(), N->getValueType(0));
    else
      LC = RTLIB::getFPTOUINT(Op0.getValueType(), N->getValueType(0));
    MakeLibCallOptions CallOptions;
    EVT OpVT = Op0.getValueType();
    CallOptions.setTypeListBeforeSoften(OpVT, N->getValueType(0), true);
    SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
    SDValue Result;
    std::tie(Result, Chain) =
        makeLibCall(DAG, LC, N->getValueType(0), Op0, CallOptions, DL, Chain);
    Results.push_back(Result);
    if (IsStrict)
      Results.push_back(Chain);
    break;
  }
  case ISD::LROUND: {
    SDValue Op0 = N->getOperand(0);
    EVT Op0VT = Op0.getValueType();
    if (getTypeAction(*DAG.getContext(), Op0.getValueType()) !=
        TargetLowering::TypeSoftenFloat) {
      if (!isTypeLegal(Op0VT))
        return;

      // In absense of Zfh, promote f16 to f32, then convert.
      if (Op0.getValueType() == MVT::f16 && !Subtarget.hasStdExtZfhOrZhinx())
        Op0 = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, Op0);

      SDValue Res =
          DAG.getNode(RISCVISD::FCVT_W_RV64, DL, MVT::i64, Op0,
                      DAG.getTargetConstant(RISCVFPRndMode::RMM, DL, MVT::i64));
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    // If the FP type needs to be softened, emit a library call to lround. We'll
    // need to truncate the result. We assume any value that doesn't fit in i32
    // is allowed to return an unspecified value.
    RTLIB::Libcall LC =
        Op0.getValueType() == MVT::f64 ? RTLIB::LROUND_F64 : RTLIB::LROUND_F32;
    MakeLibCallOptions CallOptions;
    EVT OpVT = Op0.getValueType();
    CallOptions.setTypeListBeforeSoften(OpVT, MVT::i64, true);
    SDValue Result = makeLibCall(DAG, LC, MVT::i64, Op0, CallOptions, DL).first;
    Result = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Result);
    Results.push_back(Result);
    break;
  }
  case ISD::READCYCLECOUNTER:
  case ISD::READSTEADYCOUNTER: {
    assert(!Subtarget.is64Bit() && "READCYCLECOUNTER/READSTEADYCOUNTER only "
                                   "has custom type legalization on riscv32");

    SDValue LoCounter, HiCounter;
    MVT XLenVT = Subtarget.getXLenVT();
    if (N->getOpcode() == ISD::READCYCLECOUNTER) {
      LoCounter = DAG.getTargetConstant(
          RISCVSysReg::lookupSysRegByName("CYCLE")->Encoding, DL, XLenVT);
      HiCounter = DAG.getTargetConstant(
          RISCVSysReg::lookupSysRegByName("CYCLEH")->Encoding, DL, XLenVT);
    } else {
      LoCounter = DAG.getTargetConstant(
          RISCVSysReg::lookupSysRegByName("TIME")->Encoding, DL, XLenVT);
      HiCounter = DAG.getTargetConstant(
          RISCVSysReg::lookupSysRegByName("TIMEH")->Encoding, DL, XLenVT);
    }
    SDVTList VTs = DAG.getVTList(MVT::i32, MVT::i32, MVT::Other);
    SDValue RCW = DAG.getNode(RISCVISD::READ_COUNTER_WIDE, DL, VTs,
                              N->getOperand(0), LoCounter, HiCounter);

    Results.push_back(
        DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, RCW, RCW.getValue(1)));
    Results.push_back(RCW.getValue(2));
    break;
  }
  case ISD::LOAD: {
    if (!ISD::isNON_EXTLoad(N))
      return;

    // Use a SEXTLOAD instead of the default EXTLOAD. Similar to the
    // sext_inreg we emit for ADD/SUB/MUL/SLLI.
    LoadSDNode *Ld = cast<LoadSDNode>(N);

    SDLoc dl(N);
    SDValue Res = DAG.getExtLoad(ISD::SEXTLOAD, dl, MVT::i64, Ld->getChain(),
                                 Ld->getBasePtr(), Ld->getMemoryVT(),
                                 Ld->getMemOperand());
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Res));
    Results.push_back(Res.getValue(1));
    return;
  }
  case ISD::MUL: {
    unsigned Size = N->getSimpleValueType(0).getSizeInBits();
    unsigned XLen = Subtarget.getXLen();
    // This multiply needs to be expanded, try to use MULHSU+MUL if possible.
    if (Size > XLen) {
      assert(Size == (XLen * 2) && "Unexpected custom legalisation");
      SDValue LHS = N->getOperand(0);
      SDValue RHS = N->getOperand(1);
      APInt HighMask = APInt::getHighBitsSet(Size, XLen);

      bool LHSIsU = DAG.MaskedValueIsZero(LHS, HighMask);
      bool RHSIsU = DAG.MaskedValueIsZero(RHS, HighMask);
      // We need exactly one side to be unsigned.
      if (LHSIsU == RHSIsU)
        return;

      auto MakeMULPair = [&](SDValue S, SDValue U) {
        MVT XLenVT = Subtarget.getXLenVT();
        S = DAG.getNode(ISD::TRUNCATE, DL, XLenVT, S);
        U = DAG.getNode(ISD::TRUNCATE, DL, XLenVT, U);
        SDValue Lo = DAG.getNode(ISD::MUL, DL, XLenVT, S, U);
        SDValue Hi = DAG.getNode(RISCVISD::MULHSU, DL, XLenVT, S, U);
        return DAG.getNode(ISD::BUILD_PAIR, DL, N->getValueType(0), Lo, Hi);
      };

      bool LHSIsS = DAG.ComputeNumSignBits(LHS) > XLen;
      bool RHSIsS = DAG.ComputeNumSignBits(RHS) > XLen;

      // The other operand should be signed, but still prefer MULH when
      // possible.
      if (RHSIsU && LHSIsS && !RHSIsS)
        Results.push_back(MakeMULPair(LHS, RHS));
      else if (LHSIsU && RHSIsS && !LHSIsS)
        Results.push_back(MakeMULPair(RHS, LHS));

      return;
    }
    [[fallthrough]];
  }
  case ISD::ADD:
  case ISD::SUB:
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    Results.push_back(customLegalizeToWOpWithSExt(N, DAG));
    break;
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    if (N->getOperand(1).getOpcode() != ISD::Constant) {
      // If we can use a BSET instruction, allow default promotion to apply.
      if (N->getOpcode() == ISD::SHL && Subtarget.hasStdExtZbs() &&
          isOneConstant(N->getOperand(0)))
        break;
      Results.push_back(customLegalizeToWOp(N, DAG));
      break;
    }

    // Custom legalize ISD::SHL by placing a SIGN_EXTEND_INREG after. This is
    // similar to customLegalizeToWOpWithSExt, but we must zero_extend the
    // shift amount.
    if (N->getOpcode() == ISD::SHL) {
      SDLoc DL(N);
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(0));
      SDValue NewOp1 =
          DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue NewWOp = DAG.getNode(ISD::SHL, DL, MVT::i64, NewOp0, NewOp1);
      SDValue NewRes = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, NewWOp,
                                   DAG.getValueType(MVT::i32));
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, NewRes));
    }

    break;
  case ISD::ROTL:
  case ISD::ROTR:
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    assert((Subtarget.hasStdExtZbb() || Subtarget.hasStdExtZbkb() ||
            Subtarget.hasVendorXTHeadBb()) &&
           "Unexpected custom legalization");
    if (!isa<ConstantSDNode>(N->getOperand(1)) &&
        !(Subtarget.hasStdExtZbb() || Subtarget.hasStdExtZbkb()))
      return;
    Results.push_back(customLegalizeToWOp(N, DAG));
    break;
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF: {
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");

    SDValue NewOp0 =
        DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(0));
    bool IsCTZ =
        N->getOpcode() == ISD::CTTZ || N->getOpcode() == ISD::CTTZ_ZERO_UNDEF;
    unsigned Opc = IsCTZ ? RISCVISD::CTZW : RISCVISD::CLZW;
    SDValue Res = DAG.getNode(Opc, DL, MVT::i64, NewOp0);
    Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
    return;
  }
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::UREM: {
    MVT VT = N->getSimpleValueType(0);
    assert((VT == MVT::i8 || VT == MVT::i16 || VT == MVT::i32) &&
           Subtarget.is64Bit() && Subtarget.hasStdExtM() &&
           "Unexpected custom legalisation");
    // Don't promote division/remainder by constant since we should expand those
    // to multiply by magic constant.
    AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
    if (N->getOperand(1).getOpcode() == ISD::Constant &&
        !isIntDivCheap(N->getValueType(0), Attr))
      return;

    // If the input is i32, use ANY_EXTEND since the W instructions don't read
    // the upper 32 bits. For other types we need to sign or zero extend
    // based on the opcode.
    unsigned ExtOpc = ISD::ANY_EXTEND;
    if (VT != MVT::i32)
      ExtOpc = N->getOpcode() == ISD::SDIV ? ISD::SIGN_EXTEND
                                           : ISD::ZERO_EXTEND;

    Results.push_back(customLegalizeToWOp(N, DAG, ExtOpc));
    break;
  }
  case ISD::SADDO: {
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");

    // If the RHS is a constant, we can simplify ConditionRHS below. Otherwise
    // use the default legalization.
    if (!isa<ConstantSDNode>(N->getOperand(1)))
      return;

    SDValue LHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, N->getOperand(0));
    SDValue RHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, N->getOperand(1));
    SDValue Res = DAG.getNode(ISD::ADD, DL, MVT::i64, LHS, RHS);
    Res = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, Res,
                      DAG.getValueType(MVT::i32));

    SDValue Zero = DAG.getConstant(0, DL, MVT::i64);

    // For an addition, the result should be less than one of the operands (LHS)
    // if and only if the other operand (RHS) is negative, otherwise there will
    // be overflow.
    // For a subtraction, the result should be less than one of the operands
    // (LHS) if and only if the other operand (RHS) is (non-zero) positive,
    // otherwise there will be overflow.
    EVT OType = N->getValueType(1);
    SDValue ResultLowerThanLHS = DAG.getSetCC(DL, OType, Res, LHS, ISD::SETLT);
    SDValue ConditionRHS = DAG.getSetCC(DL, OType, RHS, Zero, ISD::SETLT);

    SDValue Overflow =
        DAG.getNode(ISD::XOR, DL, OType, ConditionRHS, ResultLowerThanLHS);
    Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
    Results.push_back(Overflow);
    return;
  }
  case ISD::UADDO:
  case ISD::USUBO: {
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    bool IsAdd = N->getOpcode() == ISD::UADDO;
    // Create an ADDW or SUBW.
    SDValue LHS = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(0));
    SDValue RHS = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
    SDValue Res =
        DAG.getNode(IsAdd ? ISD::ADD : ISD::SUB, DL, MVT::i64, LHS, RHS);
    Res = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, Res,
                      DAG.getValueType(MVT::i32));

    SDValue Overflow;
    if (IsAdd && isOneConstant(RHS)) {
      // Special case uaddo X, 1 overflowed if the addition result is 0.
      // The general case (X + C) < C is not necessarily beneficial. Although we
      // reduce the live range of X, we may introduce the materialization of
      // constant C, especially when the setcc result is used by branch. We have
      // no compare with constant and branch instructions.
      Overflow = DAG.getSetCC(DL, N->getValueType(1), Res,
                              DAG.getConstant(0, DL, MVT::i64), ISD::SETEQ);
    } else if (IsAdd && isAllOnesConstant(RHS)) {
      // Special case uaddo X, -1 overflowed if X != 0.
      Overflow = DAG.getSetCC(DL, N->getValueType(1), N->getOperand(0),
                              DAG.getConstant(0, DL, MVT::i32), ISD::SETNE);
    } else {
      // Sign extend the LHS and perform an unsigned compare with the ADDW
      // result. Since the inputs are sign extended from i32, this is equivalent
      // to comparing the lower 32 bits.
      LHS = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, N->getOperand(0));
      Overflow = DAG.getSetCC(DL, N->getValueType(1), Res, LHS,
                              IsAdd ? ISD::SETULT : ISD::SETUGT);
    }

    Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
    Results.push_back(Overflow);
    return;
  }
  case ISD::UADDSAT:
  case ISD::USUBSAT: {
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    if (Subtarget.hasStdExtZbb()) {
      // With Zbb we can sign extend and let LegalizeDAG use minu/maxu. Using
      // sign extend allows overflow of the lower 32 bits to be detected on
      // the promoted size.
      SDValue LHS =
          DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, N->getOperand(0));
      SDValue RHS =
          DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue Res = DAG.getNode(N->getOpcode(), DL, MVT::i64, LHS, RHS);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }

    // Without Zbb, expand to UADDO/USUBO+select which will trigger our custom
    // promotion for UADDO/USUBO.
    Results.push_back(expandAddSubSat(N, DAG));
    return;
  }
  case ISD::SADDSAT:
  case ISD::SSUBSAT: {
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");
    Results.push_back(expandAddSubSat(N, DAG));
    return;
  }
  case ISD::ABS: {
    assert(N->getValueType(0) == MVT::i32 && Subtarget.is64Bit() &&
           "Unexpected custom legalisation");

    if (Subtarget.hasStdExtZbb()) {
      // Emit a special ABSW node that will be expanded to NEGW+MAX at isel.
      // This allows us to remember that the result is sign extended. Expanding
      // to NEGW+MAX here requires a Freeze which breaks ComputeNumSignBits.
      SDValue Src = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64,
                                N->getOperand(0));
      SDValue Abs = DAG.getNode(RISCVISD::ABSW, DL, MVT::i64, Src);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Abs));
      return;
    }

    // Expand abs to Y = (sraiw X, 31); subw(xor(X, Y), Y)
    SDValue Src = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(0));

    // Freeze the source so we can increase it's use count.
    Src = DAG.getFreeze(Src);

    // Copy sign bit to all bits using the sraiw pattern.
    SDValue SignFill = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, Src,
                                   DAG.getValueType(MVT::i32));
    SignFill = DAG.getNode(ISD::SRA, DL, MVT::i64, SignFill,
                           DAG.getConstant(31, DL, MVT::i64));

    SDValue NewRes = DAG.getNode(ISD::XOR, DL, MVT::i64, Src, SignFill);
    NewRes = DAG.getNode(ISD::SUB, DL, MVT::i64, NewRes, SignFill);

    // NOTE: The result is only required to be anyextended, but sext is
    // consistent with type legalization of sub.
    NewRes = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, NewRes,
                         DAG.getValueType(MVT::i32));
    Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, NewRes));
    return;
  }
  case ISD::BITCAST: {
    EVT VT = N->getValueType(0);
    assert(VT.isInteger() && !VT.isVector() && "Unexpected VT!");
    SDValue Op0 = N->getOperand(0);
    EVT Op0VT = Op0.getValueType();
    MVT XLenVT = Subtarget.getXLenVT();
    if (VT == MVT::i16 && Op0VT == MVT::f16 &&
        Subtarget.hasStdExtZfhminOrZhinxmin()) {
      SDValue FPConv = DAG.getNode(RISCVISD::FMV_X_ANYEXTH, DL, XLenVT, Op0);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, FPConv));
    } else if (VT == MVT::i16 && Op0VT == MVT::bf16 &&
               Subtarget.hasStdExtZfbfmin()) {
      SDValue FPConv = DAG.getNode(RISCVISD::FMV_X_ANYEXTH, DL, XLenVT, Op0);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, FPConv));
    } else if (VT == MVT::i32 && Op0VT == MVT::f32 && Subtarget.is64Bit() &&
               Subtarget.hasStdExtFOrZfinx()) {
      SDValue FPConv =
          DAG.getNode(RISCVISD::FMV_X_ANYEXTW_RV64, DL, MVT::i64, Op0);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, FPConv));
    } else if (VT == MVT::i64 && Op0VT == MVT::f64 && !Subtarget.is64Bit() &&
               Subtarget.hasStdExtDOrZdinx()) {
      SDValue NewReg = DAG.getNode(RISCVISD::SplitF64, DL,
                                   DAG.getVTList(MVT::i32, MVT::i32), Op0);
      SDValue RetReg = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64,
                                   NewReg.getValue(0), NewReg.getValue(1));
      Results.push_back(RetReg);
    } else if (!VT.isVector() && Op0VT.isFixedLengthVector() &&
               isTypeLegal(Op0VT)) {
      // Custom-legalize bitcasts from fixed-length vector types to illegal
      // scalar types in order to improve codegen. Bitcast the vector to a
      // one-element vector type whose element type is the same as the result
      // type, and extract the first element.
      EVT BVT = EVT::getVectorVT(*DAG.getContext(), VT, 1);
      if (isTypeLegal(BVT)) {
        SDValue BVec = DAG.getBitcast(BVT, Op0);
        Results.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, BVec,
                                      DAG.getVectorIdxConstant(0, DL)));
      }
    }
    break;
  }
  case RISCVISD::BREV8:
  case RISCVISD::ORC_B: {
    MVT VT = N->getSimpleValueType(0);
    MVT XLenVT = Subtarget.getXLenVT();
    assert((VT == MVT::i16 || (VT == MVT::i32 && Subtarget.is64Bit())) &&
           "Unexpected custom legalisation");
    assert(((N->getOpcode() == RISCVISD::BREV8 && Subtarget.hasStdExtZbkb()) ||
            (N->getOpcode() == RISCVISD::ORC_B && Subtarget.hasStdExtZbb())) &&
           "Unexpected extension");
    SDValue NewOp = DAG.getNode(ISD::ANY_EXTEND, DL, XLenVT, N->getOperand(0));
    SDValue NewRes = DAG.getNode(N->getOpcode(), DL, XLenVT, NewOp);
    // ReplaceNodeResults requires we maintain the same type for the return
    // value.
    Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, VT, NewRes));
    break;
  }
  case ISD::EXTRACT_VECTOR_ELT: {
    // Custom-legalize an EXTRACT_VECTOR_ELT where XLEN<SEW, as the SEW element
    // type is illegal (currently only vXi64 RV32).
    // With vmv.x.s, when SEW > XLEN, only the least-significant XLEN bits are
    // transferred to the destination register. We issue two of these from the
    // upper- and lower- halves of the SEW-bit vector element, slid down to the
    // first element.
    SDValue Vec = N->getOperand(0);
    SDValue Idx = N->getOperand(1);

    // The vector type hasn't been legalized yet so we can't issue target
    // specific nodes if it needs legalization.
    // FIXME: We would manually legalize if it's important.
    if (!isTypeLegal(Vec.getValueType()))
      return;

    MVT VecVT = Vec.getSimpleValueType();

    assert(!Subtarget.is64Bit() && N->getValueType(0) == MVT::i64 &&
           VecVT.getVectorElementType() == MVT::i64 &&
           "Unexpected EXTRACT_VECTOR_ELT legalization");

    // If this is a fixed vector, we need to convert it to a scalable vector.
    MVT ContainerVT = VecVT;
    if (VecVT.isFixedLengthVector()) {
      ContainerVT = getContainerForFixedLengthVector(VecVT);
      Vec = convertToScalableVector(ContainerVT, Vec, DAG, Subtarget);
    }

    MVT XLenVT = Subtarget.getXLenVT();

    // Use a VL of 1 to avoid processing more elements than we need.
    auto [Mask, VL] = getDefaultVLOps(1, ContainerVT, DL, DAG, Subtarget);

    // Unless the index is known to be 0, we must slide the vector down to get
    // the desired element into index 0.
    if (!isNullConstant(Idx)) {
      Vec = getVSlidedown(DAG, Subtarget, DL, ContainerVT,
                          DAG.getUNDEF(ContainerVT), Vec, Idx, Mask, VL);
    }

    // Extract the lower XLEN bits of the correct vector element.
    SDValue EltLo = DAG.getNode(RISCVISD::VMV_X_S, DL, XLenVT, Vec);

    // To extract the upper XLEN bits of the vector element, shift the first
    // element right by 32 bits and re-extract the lower XLEN bits.
    SDValue ThirtyTwoV = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, ContainerVT,
                                     DAG.getUNDEF(ContainerVT),
                                     DAG.getConstant(32, DL, XLenVT), VL);
    SDValue LShr32 =
        DAG.getNode(RISCVISD::SRL_VL, DL, ContainerVT, Vec, ThirtyTwoV,
                    DAG.getUNDEF(ContainerVT), Mask, VL);

    SDValue EltHi = DAG.getNode(RISCVISD::VMV_X_S, DL, XLenVT, LShr32);

    Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, EltLo, EltHi));
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntNo = N->getConstantOperandVal(0);
    switch (IntNo) {
    default:
      llvm_unreachable(
          "Don't know how to custom type legalize this intrinsic!");
    case Intrinsic::experimental_get_vector_length: {
      SDValue Res = lowerGetVectorLength(N, DAG, Subtarget);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    case Intrinsic::experimental_cttz_elts: {
      SDValue Res = lowerCttzElts(N, DAG, Subtarget);
      Results.push_back(
          DAG.getNode(ISD::TRUNCATE, DL, N->getValueType(0), Res));
      return;
    }
    case Intrinsic::riscv_orc_b:
    case Intrinsic::riscv_brev8:
    case Intrinsic::riscv_sha256sig0:
    case Intrinsic::riscv_sha256sig1:
    case Intrinsic::riscv_sha256sum0:
    case Intrinsic::riscv_sha256sum1:
    case Intrinsic::riscv_sm3p0:
    case Intrinsic::riscv_sm3p1: {
      if (!Subtarget.is64Bit() || N->getValueType(0) != MVT::i32)
        return;
      unsigned Opc;
      switch (IntNo) {
      case Intrinsic::riscv_orc_b:      Opc = RISCVISD::ORC_B;      break;
      case Intrinsic::riscv_brev8:      Opc = RISCVISD::BREV8;      break;
      case Intrinsic::riscv_sha256sig0: Opc = RISCVISD::SHA256SIG0; break;
      case Intrinsic::riscv_sha256sig1: Opc = RISCVISD::SHA256SIG1; break;
      case Intrinsic::riscv_sha256sum0: Opc = RISCVISD::SHA256SUM0; break;
      case Intrinsic::riscv_sha256sum1: Opc = RISCVISD::SHA256SUM1; break;
      case Intrinsic::riscv_sm3p0:      Opc = RISCVISD::SM3P0;      break;
      case Intrinsic::riscv_sm3p1:      Opc = RISCVISD::SM3P1;      break;
      }

      SDValue NewOp =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue Res = DAG.getNode(Opc, DL, MVT::i64, NewOp);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    case Intrinsic::riscv_sm4ks:
    case Intrinsic::riscv_sm4ed: {
      unsigned Opc =
          IntNo == Intrinsic::riscv_sm4ks ? RISCVISD::SM4KS : RISCVISD::SM4ED;
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(2));
      SDValue Res =
          DAG.getNode(Opc, DL, MVT::i64, NewOp0, NewOp1, N->getOperand(3));
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    case Intrinsic::riscv_mopr: {
      if (!Subtarget.is64Bit() || N->getValueType(0) != MVT::i32)
        return;
      SDValue NewOp =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue Res = DAG.getNode(
          RISCVISD::MOPR, DL, MVT::i64, NewOp,
          DAG.getTargetConstant(N->getConstantOperandVal(2), DL, MVT::i64));
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    case Intrinsic::riscv_moprr: {
      if (!Subtarget.is64Bit() || N->getValueType(0) != MVT::i32)
        return;
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(2));
      SDValue Res = DAG.getNode(
          RISCVISD::MOPRR, DL, MVT::i64, NewOp0, NewOp1,
          DAG.getTargetConstant(N->getConstantOperandVal(3), DL, MVT::i64));
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    case Intrinsic::riscv_clmul: {
      if (!Subtarget.is64Bit() || N->getValueType(0) != MVT::i32)
        return;

      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(2));
      SDValue Res = DAG.getNode(RISCVISD::CLMUL, DL, MVT::i64, NewOp0, NewOp1);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    case Intrinsic::riscv_clmulh:
    case Intrinsic::riscv_clmulr: {
      if (!Subtarget.is64Bit() || N->getValueType(0) != MVT::i32)
        return;

      // Extend inputs to XLen, and shift by 32. This will add 64 trailing zeros
      // to the full 128-bit clmul result of multiplying two xlen values.
      // Perform clmulr or clmulh on the shifted values. Finally, extract the
      // upper 32 bits.
      //
      // The alternative is to mask the inputs to 32 bits and use clmul, but
      // that requires two shifts to mask each input without zext.w.
      // FIXME: If the inputs are known zero extended or could be freely
      // zero extended, the mask form would be better.
      SDValue NewOp0 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(1));
      SDValue NewOp1 =
          DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N->getOperand(2));
      NewOp0 = DAG.getNode(ISD::SHL, DL, MVT::i64, NewOp0,
                           DAG.getConstant(32, DL, MVT::i64));
      NewOp1 = DAG.getNode(ISD::SHL, DL, MVT::i64, NewOp1,
                           DAG.getConstant(32, DL, MVT::i64));
      unsigned Opc = IntNo == Intrinsic::riscv_clmulh ? RISCVISD::CLMULH
                                                      : RISCVISD::CLMULR;
      SDValue Res = DAG.getNode(Opc, DL, MVT::i64, NewOp0, NewOp1);
      Res = DAG.getNode(ISD::SRL, DL, MVT::i64, Res,
                        DAG.getConstant(32, DL, MVT::i64));
      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Res));
      return;
    }
    case Intrinsic::riscv_vmv_x_s: {
      EVT VT = N->getValueType(0);
      MVT XLenVT = Subtarget.getXLenVT();
      if (VT.bitsLT(XLenVT)) {
        // Simple case just extract using vmv.x.s and truncate.
        SDValue Extract = DAG.getNode(RISCVISD::VMV_X_S, DL,
                                      Subtarget.getXLenVT(), N->getOperand(1));
        Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, VT, Extract));
        return;
      }

      assert(VT == MVT::i64 && !Subtarget.is64Bit() &&
             "Unexpected custom legalization");

      // We need to do the move in two steps.
      SDValue Vec = N->getOperand(1);
      MVT VecVT = Vec.getSimpleValueType();

      // First extract the lower XLEN bits of the element.
      SDValue EltLo = DAG.getNode(RISCVISD::VMV_X_S, DL, XLenVT, Vec);

      // To extract the upper XLEN bits of the vector element, shift the first
      // element right by 32 bits and re-extract the lower XLEN bits.
      auto [Mask, VL] = getDefaultVLOps(1, VecVT, DL, DAG, Subtarget);

      SDValue ThirtyTwoV =
          DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VecVT, DAG.getUNDEF(VecVT),
                      DAG.getConstant(32, DL, XLenVT), VL);
      SDValue LShr32 = DAG.getNode(RISCVISD::SRL_VL, DL, VecVT, Vec, ThirtyTwoV,
                                   DAG.getUNDEF(VecVT), Mask, VL);
      SDValue EltHi = DAG.getNode(RISCVISD::VMV_X_S, DL, XLenVT, LShr32);

      Results.push_back(
          DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, EltLo, EltHi));
      break;
    }
    }
    break;
  }
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMIN:
    if (SDValue V = lowerVECREDUCE(SDValue(N, 0), DAG))
      Results.push_back(V);
    break;
  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_SMIN:
  case ISD::VP_REDUCE_UMIN:
    if (SDValue V = lowerVPREDUCE(SDValue(N, 0), DAG))
      Results.push_back(V);
    break;
  case ISD::GET_ROUNDING: {
    SDVTList VTs = DAG.getVTList(Subtarget.getXLenVT(), MVT::Other);
    SDValue Res = DAG.getNode(ISD::GET_ROUNDING, DL, VTs, N->getOperand(0));
    Results.push_back(Res.getValue(0));
    Results.push_back(Res.getValue(1));
    break;
  }
  }
}

/// Given a binary operator, return the *associative* generic ISD::VECREDUCE_OP
/// which corresponds to it.
static unsigned getVecReduceOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Unhandled binary to transfrom reduction");
  case ISD::ADD:
    return ISD::VECREDUCE_ADD;
  case ISD::UMAX:
    return ISD::VECREDUCE_UMAX;
  case ISD::SMAX:
    return ISD::VECREDUCE_SMAX;
  case ISD::UMIN:
    return ISD::VECREDUCE_UMIN;
  case ISD::SMIN:
    return ISD::VECREDUCE_SMIN;
  case ISD::AND:
    return ISD::VECREDUCE_AND;
  case ISD::OR:
    return ISD::VECREDUCE_OR;
  case ISD::XOR:
    return ISD::VECREDUCE_XOR;
  case ISD::FADD:
    // Note: This is the associative form of the generic reduction opcode.
    return ISD::VECREDUCE_FADD;
  }
}

/// Perform two related transforms whose purpose is to incrementally recognize
/// an explode_vector followed by scalar reduction as a vector reduction node.
/// This exists to recover from a deficiency in SLP which can't handle
/// forests with multiple roots sharing common nodes.  In some cases, one
/// of the trees will be vectorized, and the other will remain (unprofitably)
/// scalarized.
static SDValue
combineBinOpOfExtractToReduceTree(SDNode *N, SelectionDAG &DAG,
                                  const RISCVSubtarget &Subtarget) {

  // This transforms need to run before all integer types have been legalized
  // to i64 (so that the vector element type matches the add type), and while
  // it's safe to introduce odd sized vector types.
  if (DAG.NewNodesMustHaveLegalTypes)
    return SDValue();

  // Without V, this transform isn't useful.  We could form the (illegal)
  // operations and let them be scalarized again, but there's really no point.
  if (!Subtarget.hasVInstructions())
    return SDValue();

  const SDLoc DL(N);
  const EVT VT = N->getValueType(0);
  const unsigned Opc = N->getOpcode();

  // For FADD, we only handle the case with reassociation allowed.  We
  // could handle strict reduction order, but at the moment, there's no
  // known reason to, and the complexity isn't worth it.
  // TODO: Handle fminnum and fmaxnum here
  if (!VT.isInteger() &&
      (Opc != ISD::FADD || !N->getFlags().hasAllowReassociation()))
    return SDValue();

  const unsigned ReduceOpc = getVecReduceOpcode(Opc);
  assert(Opc == ISD::getVecReduceBaseOpcode(ReduceOpc) &&
         "Inconsistent mappings");
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  if (!LHS.hasOneUse() || !RHS.hasOneUse())
    return SDValue();

  if (RHS.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    std::swap(LHS, RHS);

  if (RHS.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
      !isa<ConstantSDNode>(RHS.getOperand(1)))
    return SDValue();

  uint64_t RHSIdx = cast<ConstantSDNode>(RHS.getOperand(1))->getLimitedValue();
  SDValue SrcVec = RHS.getOperand(0);
  EVT SrcVecVT = SrcVec.getValueType();
  assert(SrcVecVT.getVectorElementType() == VT);
  if (SrcVecVT.isScalableVector())
    return SDValue();

  if (SrcVecVT.getScalarSizeInBits() > Subtarget.getELen())
    return SDValue();

  // match binop (extract_vector_elt V, 0), (extract_vector_elt V, 1) to
  // reduce_op (extract_subvector [2 x VT] from V).  This will form the
  // root of our reduction tree. TODO: We could extend this to any two
  // adjacent aligned constant indices if desired.
  if (LHS.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
      LHS.getOperand(0) == SrcVec && isa<ConstantSDNode>(LHS.getOperand(1))) {
    uint64_t LHSIdx =
      cast<ConstantSDNode>(LHS.getOperand(1))->getLimitedValue();
    if (0 == std::min(LHSIdx, RHSIdx) && 1 == std::max(LHSIdx, RHSIdx)) {
      EVT ReduceVT = EVT::getVectorVT(*DAG.getContext(), VT, 2);
      SDValue Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ReduceVT, SrcVec,
                                DAG.getVectorIdxConstant(0, DL));
      return DAG.getNode(ReduceOpc, DL, VT, Vec, N->getFlags());
    }
  }

  // Match (binop (reduce (extract_subvector V, 0),
  //                      (extract_vector_elt V, sizeof(SubVec))))
  // into a reduction of one more element from the original vector V.
  if (LHS.getOpcode() != ReduceOpc)
    return SDValue();

  SDValue ReduceVec = LHS.getOperand(0);
  if (ReduceVec.getOpcode() == ISD::EXTRACT_SUBVECTOR &&
      ReduceVec.hasOneUse() && ReduceVec.getOperand(0) == RHS.getOperand(0) &&
      isNullConstant(ReduceVec.getOperand(1)) &&
      ReduceVec.getValueType().getVectorNumElements() == RHSIdx) {
    // For illegal types (e.g. 3xi32), most will be combined again into a
    // wider (hopefully legal) type.  If this is a terminal state, we are
    // relying on type legalization here to produce something reasonable
    // and this lowering quality could probably be improved. (TODO)
    EVT ReduceVT = EVT::getVectorVT(*DAG.getContext(), VT, RHSIdx + 1);
    SDValue Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ReduceVT, SrcVec,
                              DAG.getVectorIdxConstant(0, DL));
    auto Flags = ReduceVec->getFlags();
    Flags.intersectWith(N->getFlags());
    return DAG.getNode(ReduceOpc, DL, VT, Vec, Flags);
  }

  return SDValue();
}


// Try to fold (<bop> x, (reduction.<bop> vec, start))
static SDValue combineBinOpToReduce(SDNode *N, SelectionDAG &DAG,
                                    const RISCVSubtarget &Subtarget) {
  auto BinOpToRVVReduce = [](unsigned Opc) {
    switch (Opc) {
    default:
      llvm_unreachable("Unhandled binary to transfrom reduction");
    case ISD::ADD:
      return RISCVISD::VECREDUCE_ADD_VL;
    case ISD::UMAX:
      return RISCVISD::VECREDUCE_UMAX_VL;
    case ISD::SMAX:
      return RISCVISD::VECREDUCE_SMAX_VL;
    case ISD::UMIN:
      return RISCVISD::VECREDUCE_UMIN_VL;
    case ISD::SMIN:
      return RISCVISD::VECREDUCE_SMIN_VL;
    case ISD::AND:
      return RISCVISD::VECREDUCE_AND_VL;
    case ISD::OR:
      return RISCVISD::VECREDUCE_OR_VL;
    case ISD::XOR:
      return RISCVISD::VECREDUCE_XOR_VL;
    case ISD::FADD:
      return RISCVISD::VECREDUCE_FADD_VL;
    case ISD::FMAXNUM:
      return RISCVISD::VECREDUCE_FMAX_VL;
    case ISD::FMINNUM:
      return RISCVISD::VECREDUCE_FMIN_VL;
    }
  };

  auto IsReduction = [&BinOpToRVVReduce](SDValue V, unsigned Opc) {
    return V.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
           isNullConstant(V.getOperand(1)) &&
           V.getOperand(0).getOpcode() == BinOpToRVVReduce(Opc);
  };

  unsigned Opc = N->getOpcode();
  unsigned ReduceIdx;
  if (IsReduction(N->getOperand(0), Opc))
    ReduceIdx = 0;
  else if (IsReduction(N->getOperand(1), Opc))
    ReduceIdx = 1;
  else
    return SDValue();

  // Skip if FADD disallows reassociation but the combiner needs.
  if (Opc == ISD::FADD && !N->getFlags().hasAllowReassociation())
    return SDValue();

  SDValue Extract = N->getOperand(ReduceIdx);
  SDValue Reduce = Extract.getOperand(0);
  if (!Extract.hasOneUse() || !Reduce.hasOneUse())
    return SDValue();

  SDValue ScalarV = Reduce.getOperand(2);
  EVT ScalarVT = ScalarV.getValueType();
  if (ScalarV.getOpcode() == ISD::INSERT_SUBVECTOR &&
      ScalarV.getOperand(0)->isUndef() &&
      isNullConstant(ScalarV.getOperand(2)))
    ScalarV = ScalarV.getOperand(1);

  // Make sure that ScalarV is a splat with VL=1.
  if (ScalarV.getOpcode() != RISCVISD::VFMV_S_F_VL &&
      ScalarV.getOpcode() != RISCVISD::VMV_S_X_VL &&
      ScalarV.getOpcode() != RISCVISD::VMV_V_X_VL)
    return SDValue();

  if (!isNonZeroAVL(ScalarV.getOperand(2)))
    return SDValue();

  // Check the scalar of ScalarV is neutral element
  // TODO: Deal with value other than neutral element.
  if (!isNeutralConstant(N->getOpcode(), N->getFlags(), ScalarV.getOperand(1),
                         0))
    return SDValue();

  // If the AVL is zero, operand 0 will be returned. So it's not safe to fold.
  // FIXME: We might be able to improve this if operand 0 is undef.
  if (!isNonZeroAVL(Reduce.getOperand(5)))
    return SDValue();

  SDValue NewStart = N->getOperand(1 - ReduceIdx);

  SDLoc DL(N);
  SDValue NewScalarV =
      lowerScalarInsert(NewStart, ScalarV.getOperand(2),
                        ScalarV.getSimpleValueType(), DL, DAG, Subtarget);

  // If we looked through an INSERT_SUBVECTOR we need to restore it.
  if (ScalarVT != ScalarV.getValueType())
    NewScalarV =
        DAG.getNode(ISD::INSERT_SUBVECTOR, DL, ScalarVT, DAG.getUNDEF(ScalarVT),
                    NewScalarV, DAG.getVectorIdxConstant(0, DL));

  SDValue Ops[] = {Reduce.getOperand(0), Reduce.getOperand(1),
                   NewScalarV,           Reduce.getOperand(3),
                   Reduce.getOperand(4), Reduce.getOperand(5)};
  SDValue NewReduce =
      DAG.getNode(Reduce.getOpcode(), DL, Reduce.getValueType(), Ops);
  return DAG.getNode(Extract.getOpcode(), DL, Extract.getValueType(), NewReduce,
                     Extract.getOperand(1));
}

// Optimize (add (shl x, c0), (shl y, c1)) ->
//          (SLLI (SH*ADD x, y), c0), if c1-c0 equals to [1|2|3].
static SDValue transformAddShlImm(SDNode *N, SelectionDAG &DAG,
                                  const RISCVSubtarget &Subtarget) {
  // Perform this optimization only in the zba extension.
  if (!Subtarget.hasStdExtZba())
    return SDValue();

  // Skip for vector types and larger types.
  EVT VT = N->getValueType(0);
  if (VT.isVector() || VT.getSizeInBits() > Subtarget.getXLen())
    return SDValue();

  // The two operand nodes must be SHL and have no other use.
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (N0->getOpcode() != ISD::SHL || N1->getOpcode() != ISD::SHL ||
      !N0->hasOneUse() || !N1->hasOneUse())
    return SDValue();

  // Check c0 and c1.
  auto *N0C = dyn_cast<ConstantSDNode>(N0->getOperand(1));
  auto *N1C = dyn_cast<ConstantSDNode>(N1->getOperand(1));
  if (!N0C || !N1C)
    return SDValue();
  int64_t C0 = N0C->getSExtValue();
  int64_t C1 = N1C->getSExtValue();
  if (C0 <= 0 || C1 <= 0)
    return SDValue();

  // Skip if SH1ADD/SH2ADD/SH3ADD are not applicable.
  int64_t Bits = std::min(C0, C1);
  int64_t Diff = std::abs(C0 - C1);
  if (Diff != 1 && Diff != 2 && Diff != 3)
    return SDValue();

  // Build nodes.
  SDLoc DL(N);
  SDValue NS = (C0 < C1) ? N0->getOperand(0) : N1->getOperand(0);
  SDValue NL = (C0 > C1) ? N0->getOperand(0) : N1->getOperand(0);
  SDValue SHADD = DAG.getNode(RISCVISD::SHL_ADD, DL, VT, NL,
                              DAG.getConstant(Diff, DL, VT), NS);
  return DAG.getNode(ISD::SHL, DL, VT, SHADD, DAG.getConstant(Bits, DL, VT));
}

// Combine a constant select operand into its use:
//
// (and (select cond, -1, c), x)
//   -> (select cond, x, (and x, c))  [AllOnes=1]
// (or  (select cond, 0, c), x)
//   -> (select cond, x, (or x, c))  [AllOnes=0]
// (xor (select cond, 0, c), x)
//   -> (select cond, x, (xor x, c))  [AllOnes=0]
// (add (select cond, 0, c), x)
//   -> (select cond, x, (add x, c))  [AllOnes=0]
// (sub x, (select cond, 0, c))
//   -> (select cond, x, (sub x, c))  [AllOnes=0]
static SDValue combineSelectAndUse(SDNode *N, SDValue Slct, SDValue OtherOp,
                                   SelectionDAG &DAG, bool AllOnes,
                                   const RISCVSubtarget &Subtarget) {
  EVT VT = N->getValueType(0);

  // Skip vectors.
  if (VT.isVector())
    return SDValue();

  if (!Subtarget.hasConditionalMoveFusion()) {
    // (select cond, x, (and x, c)) has custom lowering with Zicond.
    if ((!Subtarget.hasStdExtZicond() &&
         !Subtarget.hasVendorXVentanaCondOps()) ||
        N->getOpcode() != ISD::AND)
      return SDValue();

    // Maybe harmful when condition code has multiple use.
    if (Slct.getOpcode() == ISD::SELECT && !Slct.getOperand(0).hasOneUse())
      return SDValue();

    // Maybe harmful when VT is wider than XLen.
    if (VT.getSizeInBits() > Subtarget.getXLen())
      return SDValue();
  }

  if ((Slct.getOpcode() != ISD::SELECT &&
       Slct.getOpcode() != RISCVISD::SELECT_CC) ||
      !Slct.hasOneUse())
    return SDValue();

  auto isZeroOrAllOnes = [](SDValue N, bool AllOnes) {
    return AllOnes ? isAllOnesConstant(N) : isNullConstant(N);
  };

  bool SwapSelectOps;
  unsigned OpOffset = Slct.getOpcode() == RISCVISD::SELECT_CC ? 2 : 0;
  SDValue TrueVal = Slct.getOperand(1 + OpOffset);
  SDValue FalseVal = Slct.getOperand(2 + OpOffset);
  SDValue NonConstantVal;
  if (isZeroOrAllOnes(TrueVal, AllOnes)) {
    SwapSelectOps = false;
    NonConstantVal = FalseVal;
  } else if (isZeroOrAllOnes(FalseVal, AllOnes)) {
    SwapSelectOps = true;
    NonConstantVal = TrueVal;
  } else
    return SDValue();

  // Slct is now know to be the desired identity constant when CC is true.
  TrueVal = OtherOp;
  FalseVal = DAG.getNode(N->getOpcode(), SDLoc(N), VT, OtherOp, NonConstantVal);
  // Unless SwapSelectOps says the condition should be false.
  if (SwapSelectOps)
    std::swap(TrueVal, FalseVal);

  if (Slct.getOpcode() == RISCVISD::SELECT_CC)
    return DAG.getNode(RISCVISD::SELECT_CC, SDLoc(N), VT,
                       {Slct.getOperand(0), Slct.getOperand(1),
                        Slct.getOperand(2), TrueVal, FalseVal});

  return DAG.getNode(ISD::SELECT, SDLoc(N), VT,
                     {Slct.getOperand(0), TrueVal, FalseVal});
}

// Attempt combineSelectAndUse on each operand of a commutative operator N.
static SDValue combineSelectAndUseCommutative(SDNode *N, SelectionDAG &DAG,
                                              bool AllOnes,
                                              const RISCVSubtarget &Subtarget) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (SDValue Result = combineSelectAndUse(N, N0, N1, DAG, AllOnes, Subtarget))
    return Result;
  if (SDValue Result = combineSelectAndUse(N, N1, N0, DAG, AllOnes, Subtarget))
    return Result;
  return SDValue();
}

// Transform (add (mul x, c0), c1) ->
//           (add (mul (add x, c1/c0), c0), c1%c0).
// if c1/c0 and c1%c0 are simm12, while c1 is not. A special corner case
// that should be excluded is when c0*(c1/c0) is simm12, which will lead
// to an infinite loop in DAGCombine if transformed.
// Or transform (add (mul x, c0), c1) ->
//              (add (mul (add x, c1/c0+1), c0), c1%c0-c0),
// if c1/c0+1 and c1%c0-c0 are simm12, while c1 is not. A special corner
// case that should be excluded is when c0*(c1/c0+1) is simm12, which will
// lead to an infinite loop in DAGCombine if transformed.
// Or transform (add (mul x, c0), c1) ->
//              (add (mul (add x, c1/c0-1), c0), c1%c0+c0),
// if c1/c0-1 and c1%c0+c0 are simm12, while c1 is not. A special corner
// case that should be excluded is when c0*(c1/c0-1) is simm12, which will
// lead to an infinite loop in DAGCombine if transformed.
// Or transform (add (mul x, c0), c1) ->
//              (mul (add x, c1/c0), c0).
// if c1%c0 is zero, and c1/c0 is simm12 while c1 is not.
static SDValue transformAddImmMulImm(SDNode *N, SelectionDAG &DAG,
                                     const RISCVSubtarget &Subtarget) {
  // Skip for vector types and larger types.
  EVT VT = N->getValueType(0);
  if (VT.isVector() || VT.getSizeInBits() > Subtarget.getXLen())
    return SDValue();
  // The first operand node must be a MUL and has no other use.
  SDValue N0 = N->getOperand(0);
  if (!N0->hasOneUse() || N0->getOpcode() != ISD::MUL)
    return SDValue();
  // Check if c0 and c1 match above conditions.
  auto *N0C = dyn_cast<ConstantSDNode>(N0->getOperand(1));
  auto *N1C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!N0C || !N1C)
    return SDValue();
  // If N0C has multiple uses it's possible one of the cases in
  // DAGCombiner::isMulAddWithConstProfitable will be true, which would result
  // in an infinite loop.
  if (!N0C->hasOneUse())
    return SDValue();
  int64_t C0 = N0C->getSExtValue();
  int64_t C1 = N1C->getSExtValue();
  int64_t CA, CB;
  if (C0 == -1 || C0 == 0 || C0 == 1 || isInt<12>(C1))
    return SDValue();
  // Search for proper CA (non-zero) and CB that both are simm12.
  if ((C1 / C0) != 0 && isInt<12>(C1 / C0) && isInt<12>(C1 % C0) &&
      !isInt<12>(C0 * (C1 / C0))) {
    CA = C1 / C0;
    CB = C1 % C0;
  } else if ((C1 / C0 + 1) != 0 && isInt<12>(C1 / C0 + 1) &&
             isInt<12>(C1 % C0 - C0) && !isInt<12>(C0 * (C1 / C0 + 1))) {
    CA = C1 / C0 + 1;
    CB = C1 % C0 - C0;
  } else if ((C1 / C0 - 1) != 0 && isInt<12>(C1 / C0 - 1) &&
             isInt<12>(C1 % C0 + C0) && !isInt<12>(C0 * (C1 / C0 - 1))) {
    CA = C1 / C0 - 1;
    CB = C1 % C0 + C0;
  } else
    return SDValue();
  // Build new nodes (add (mul (add x, c1/c0), c0), c1%c0).
  SDLoc DL(N);
  SDValue New0 = DAG.getNode(ISD::ADD, DL, VT, N0->getOperand(0),
                             DAG.getConstant(CA, DL, VT));
  SDValue New1 =
      DAG.getNode(ISD::MUL, DL, VT, New0, DAG.getConstant(C0, DL, VT));
  return DAG.getNode(ISD::ADD, DL, VT, New1, DAG.getConstant(CB, DL, VT));
}

// add (zext, zext) -> zext (add (zext, zext))
// sub (zext, zext) -> sext (sub (zext, zext))
// mul (zext, zext) -> zext (mul (zext, zext))
// sdiv (zext, zext) -> zext (sdiv (zext, zext))
// udiv (zext, zext) -> zext (udiv (zext, zext))
// srem (zext, zext) -> zext (srem (zext, zext))
// urem (zext, zext) -> zext (urem (zext, zext))
//
// where the sum of the extend widths match, and the the range of the bin op
// fits inside the width of the narrower bin op. (For profitability on rvv, we
// use a power of two for both inner and outer extend.)
static SDValue combineBinOpOfZExt(SDNode *N, SelectionDAG &DAG) {

  EVT VT = N->getValueType(0);
  if (!VT.isVector() || !DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (N0.getOpcode() != ISD::ZERO_EXTEND || N1.getOpcode() != ISD::ZERO_EXTEND)
    return SDValue();
  if (!N0.hasOneUse() || !N1.hasOneUse())
    return SDValue();

  SDValue Src0 = N0.getOperand(0);
  SDValue Src1 = N1.getOperand(0);
  EVT SrcVT = Src0.getValueType();
  if (!DAG.getTargetLoweringInfo().isTypeLegal(SrcVT) ||
      SrcVT != Src1.getValueType() || SrcVT.getScalarSizeInBits() < 8 ||
      SrcVT.getScalarSizeInBits() >= VT.getScalarSizeInBits() / 2)
    return SDValue();

  LLVMContext &C = *DAG.getContext();
  EVT ElemVT = VT.getVectorElementType().getHalfSizedIntegerVT(C);
  EVT NarrowVT = EVT::getVectorVT(C, ElemVT, VT.getVectorElementCount());

  Src0 = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(Src0), NarrowVT, Src0);
  Src1 = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(Src1), NarrowVT, Src1);

  // Src0 and Src1 are zero extended, so they're always positive if signed.
  //
  // sub can produce a negative from two positive operands, so it needs sign
  // extended. Other nodes produce a positive from two positive operands, so
  // zero extend instead.
  unsigned OuterExtend =
      N->getOpcode() == ISD::SUB ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;

  return DAG.getNode(
      OuterExtend, SDLoc(N), VT,
      DAG.getNode(N->getOpcode(), SDLoc(N), NarrowVT, Src0, Src1));
}

// Try to turn (add (xor bool, 1) -1) into (neg bool).
static SDValue combineAddOfBooleanXor(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // RHS should be -1.
  if (!isAllOnesConstant(N1))
    return SDValue();

  // Look for (xor X, 1).
  if (N0.getOpcode() != ISD::XOR || !isOneConstant(N0.getOperand(1)))
    return SDValue();

  // First xor input should be 0 or 1.
  APInt Mask = APInt::getBitsSetFrom(VT.getSizeInBits(), 1);
  if (!DAG.MaskedValueIsZero(N0.getOperand(0), Mask))
    return SDValue();

  // Emit a negate of the setcc.
  return DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT),
                     N0.getOperand(0));
}

static SDValue performADDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const RISCVSubtarget &Subtarget) {
  SelectionDAG &DAG = DCI.DAG;
  if (SDValue V = combineAddOfBooleanXor(N, DAG))
    return V;
  if (SDValue V = transformAddImmMulImm(N, DAG, Subtarget))
    return V;
  if (!DCI.isBeforeLegalize() && !DCI.isCalledByLegalizer())
    if (SDValue V = transformAddShlImm(N, DAG, Subtarget))
      return V;
  if (SDValue V = combineBinOpToReduce(N, DAG, Subtarget))
    return V;
  if (SDValue V = combineBinOpOfExtractToReduceTree(N, DAG, Subtarget))
    return V;
  if (SDValue V = combineBinOpOfZExt(N, DAG))
    return V;

  // fold (add (select lhs, rhs, cc, 0, y), x) ->
  //      (select lhs, rhs, cc, x, (add x, y))
  return combineSelectAndUseCommutative(N, DAG, /*AllOnes*/ false, Subtarget);
}

// Try to turn a sub boolean RHS and constant LHS into an addi.
static SDValue combineSubOfBoolean(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // Require a constant LHS.
  auto *N0C = dyn_cast<ConstantSDNode>(N0);
  if (!N0C)
    return SDValue();

  // All our optimizations involve subtracting 1 from the immediate and forming
  // an ADDI. Make sure the new immediate is valid for an ADDI.
  APInt ImmValMinus1 = N0C->getAPIntValue() - 1;
  if (!ImmValMinus1.isSignedIntN(12))
    return SDValue();

  SDValue NewLHS;
  if (N1.getOpcode() == ISD::SETCC && N1.hasOneUse()) {
    // (sub constant, (setcc x, y, eq/neq)) ->
    // (add (setcc x, y, neq/eq), constant - 1)
    ISD::CondCode CCVal = cast<CondCodeSDNode>(N1.getOperand(2))->get();
    EVT SetCCOpVT = N1.getOperand(0).getValueType();
    if (!isIntEqualitySetCC(CCVal) || !SetCCOpVT.isInteger())
      return SDValue();
    CCVal = ISD::getSetCCInverse(CCVal, SetCCOpVT);
    NewLHS =
        DAG.getSetCC(SDLoc(N1), VT, N1.getOperand(0), N1.getOperand(1), CCVal);
  } else if (N1.getOpcode() == ISD::XOR && isOneConstant(N1.getOperand(1)) &&
             N1.getOperand(0).getOpcode() == ISD::SETCC) {
    // (sub C, (xor (setcc), 1)) -> (add (setcc), C-1).
    // Since setcc returns a bool the xor is equivalent to 1-setcc.
    NewLHS = N1.getOperand(0);
  } else
    return SDValue();

  SDValue NewRHS = DAG.getConstant(ImmValMinus1, DL, VT);
  return DAG.getNode(ISD::ADD, DL, VT, NewLHS, NewRHS);
}

// Looks for (sub (shl X, 8), X) where only bits 8, 16, 24, 32, etc. of X are
// non-zero. Replace with orc.b.
static SDValue combineSubShiftToOrcB(SDNode *N, SelectionDAG &DAG,
                                     const RISCVSubtarget &Subtarget) {
  if (!Subtarget.hasStdExtZbb())
    return SDValue();

  EVT VT = N->getValueType(0);

  if (VT != Subtarget.getXLenVT() && VT != MVT::i32 && VT != MVT::i16)
    return SDValue();

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  if (N0.getOpcode() != ISD::SHL || N0.getOperand(0) != N1 || !N0.hasOneUse())
    return SDValue();

  auto *ShAmtC = dyn_cast<ConstantSDNode>(N0.getOperand(1));
  if (!ShAmtC || ShAmtC->getZExtValue() != 8)
    return SDValue();

  APInt Mask = APInt::getSplat(VT.getSizeInBits(), APInt(8, 0xfe));
  if (!DAG.MaskedValueIsZero(N1, Mask))
    return SDValue();

  return DAG.getNode(RISCVISD::ORC_B, SDLoc(N), VT, N1);
}

static SDValue performSUBCombine(SDNode *N, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  if (SDValue V = combineSubOfBoolean(N, DAG))
    return V;

  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  // fold (sub 0, (setcc x, 0, setlt)) -> (sra x, xlen - 1)
  if (isNullConstant(N0) && N1.getOpcode() == ISD::SETCC && N1.hasOneUse() &&
      isNullConstant(N1.getOperand(1))) {
    ISD::CondCode CCVal = cast<CondCodeSDNode>(N1.getOperand(2))->get();
    if (CCVal == ISD::SETLT) {
      SDLoc DL(N);
      unsigned ShAmt = N0.getValueSizeInBits() - 1;
      return DAG.getNode(ISD::SRA, DL, VT, N1.getOperand(0),
                         DAG.getConstant(ShAmt, DL, VT));
    }
  }

  if (SDValue V = combineBinOpOfZExt(N, DAG))
    return V;
  if (SDValue V = combineSubShiftToOrcB(N, DAG, Subtarget))
    return V;

  // fold (sub x, (select lhs, rhs, cc, 0, y)) ->
  //      (select lhs, rhs, cc, x, (sub x, y))
  return combineSelectAndUse(N, N1, N0, DAG, /*AllOnes*/ false, Subtarget);
}

// Apply DeMorgan's law to (and/or (xor X, 1), (xor Y, 1)) if X and Y are 0/1.
// Legalizing setcc can introduce xors like this. Doing this transform reduces
// the number of xors and may allow the xor to fold into a branch condition.
static SDValue combineDeMorganOfBoolean(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  bool IsAnd = N->getOpcode() == ISD::AND;

  if (N0.getOpcode() != ISD::XOR || N1.getOpcode() != ISD::XOR)
    return SDValue();

  if (!N0.hasOneUse() || !N1.hasOneUse())
    return SDValue();

  SDValue N01 = N0.getOperand(1);
  SDValue N11 = N1.getOperand(1);

  // For AND, SimplifyDemandedBits may have turned one of the (xor X, 1) into
  // (xor X, -1) based on the upper bits of the other operand being 0. If the
  // operation is And, allow one of the Xors to use -1.
  if (isOneConstant(N01)) {
    if (!isOneConstant(N11) && !(IsAnd && isAllOnesConstant(N11)))
      return SDValue();
  } else if (isOneConstant(N11)) {
    // N01 and N11 being 1 was already handled. Handle N11==1 and N01==-1.
    if (!(IsAnd && isAllOnesConstant(N01)))
      return SDValue();
  } else
    return SDValue();

  EVT VT = N->getValueType(0);

  SDValue N00 = N0.getOperand(0);
  SDValue N10 = N1.getOperand(0);

  // The LHS of the xors needs to be 0/1.
  APInt Mask = APInt::getBitsSetFrom(VT.getSizeInBits(), 1);
  if (!DAG.MaskedValueIsZero(N00, Mask) || !DAG.MaskedValueIsZero(N10, Mask))
    return SDValue();

  // Invert the opcode and insert a new xor.
  SDLoc DL(N);
  unsigned Opc = IsAnd ? ISD::OR : ISD::AND;
  SDValue Logic = DAG.getNode(Opc, DL, VT, N00, N10);
  return DAG.getNode(ISD::XOR, DL, VT, Logic, DAG.getConstant(1, DL, VT));
}

// Fold (vXi8 (trunc (vselect (setltu, X, 256), X, (sext (setgt X, 0))))) to
// (vXi8 (trunc (smin (smax X, 0), 255))). This represents saturating a signed
// value to an unsigned value. This will be lowered to vmax and series of
// vnclipu instructions later. This can be extended to other truncated types
// other than i8 by replacing 256 and 255 with the equivalent constants for the
// type.
static SDValue combineTruncSelectToSMaxUSat(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  EVT SrcVT = N0.getValueType();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!VT.isVector() || !TLI.isTypeLegal(VT) || !TLI.isTypeLegal(SrcVT))
    return SDValue();

  if (N0.getOpcode() != ISD::VSELECT || !N0.hasOneUse())
    return SDValue();

  SDValue Cond = N0.getOperand(0);
  SDValue True = N0.getOperand(1);
  SDValue False = N0.getOperand(2);

  if (Cond.getOpcode() != ISD::SETCC)
    return SDValue();

  // FIXME: Support the version of this pattern with the select operands
  // swapped.
  ISD::CondCode CCVal = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
  if (CCVal != ISD::SETULT)
    return SDValue();

  SDValue CondLHS = Cond.getOperand(0);
  SDValue CondRHS = Cond.getOperand(1);

  if (CondLHS != True)
    return SDValue();

  unsigned ScalarBits = VT.getScalarSizeInBits();

  // FIXME: Support other constants.
  ConstantSDNode *CondRHSC = isConstOrConstSplat(CondRHS);
  if (!CondRHSC || CondRHSC->getAPIntValue() != (1ULL << ScalarBits))
    return SDValue();

  if (False.getOpcode() != ISD::SIGN_EXTEND)
    return SDValue();

  False = False.getOperand(0);

  if (False.getOpcode() != ISD::SETCC || False.getOperand(0) != True)
    return SDValue();

  ConstantSDNode *FalseRHSC = isConstOrConstSplat(False.getOperand(1));
  if (!FalseRHSC || !FalseRHSC->isZero())
    return SDValue();

  ISD::CondCode CCVal2 = cast<CondCodeSDNode>(False.getOperand(2))->get();
  if (CCVal2 != ISD::SETGT)
    return SDValue();

  // Emit the signed to unsigned saturation pattern.
  SDLoc DL(N);
  SDValue Max =
      DAG.getNode(ISD::SMAX, DL, SrcVT, True, DAG.getConstant(0, DL, SrcVT));
  SDValue Min =
      DAG.getNode(ISD::SMIN, DL, SrcVT, Max,
                  DAG.getConstant((1ULL << ScalarBits) - 1, DL, SrcVT));
  return DAG.getNode(ISD::TRUNCATE, DL, VT, Min);
}

static SDValue performTRUNCATECombine(SDNode *N, SelectionDAG &DAG,
                                      const RISCVSubtarget &Subtarget) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // Pre-promote (i1 (truncate (srl X, Y))) on RV64 with Zbs without zero
  // extending X. This is safe since we only need the LSB after the shift and
  // shift amounts larger than 31 would produce poison. If we wait until
  // type legalization, we'll create RISCVISD::SRLW and we can't recover it
  // to use a BEXT instruction.
  if (!RV64LegalI32 && Subtarget.is64Bit() && Subtarget.hasStdExtZbs() && VT == MVT::i1 &&
      N0.getValueType() == MVT::i32 && N0.getOpcode() == ISD::SRL &&
      !isa<ConstantSDNode>(N0.getOperand(1)) && N0.hasOneUse()) {
    SDLoc DL(N0);
    SDValue Op0 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N0.getOperand(0));
    SDValue Op1 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N0.getOperand(1));
    SDValue Srl = DAG.getNode(ISD::SRL, DL, MVT::i64, Op0, Op1);
    return DAG.getNode(ISD::TRUNCATE, SDLoc(N), VT, Srl);
  }

  return combineTruncSelectToSMaxUSat(N, DAG);
}

// Combines two comparison operation and logic operation to one selection
// operation(min, max) and logic operation. Returns new constructed Node if
// conditions for optimization are satisfied.
static SDValue performANDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const RISCVSubtarget &Subtarget) {
  SelectionDAG &DAG = DCI.DAG;

  SDValue N0 = N->getOperand(0);
  // Pre-promote (i32 (and (srl X, Y), 1)) on RV64 with Zbs without zero
  // extending X. This is safe since we only need the LSB after the shift and
  // shift amounts larger than 31 would produce poison. If we wait until
  // type legalization, we'll create RISCVISD::SRLW and we can't recover it
  // to use a BEXT instruction.
  if (!RV64LegalI32 && Subtarget.is64Bit() && Subtarget.hasStdExtZbs() &&
      N->getValueType(0) == MVT::i32 && isOneConstant(N->getOperand(1)) &&
      N0.getOpcode() == ISD::SRL && !isa<ConstantSDNode>(N0.getOperand(1)) &&
      N0.hasOneUse()) {
    SDLoc DL(N);
    SDValue Op0 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N0.getOperand(0));
    SDValue Op1 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N0.getOperand(1));
    SDValue Srl = DAG.getNode(ISD::SRL, DL, MVT::i64, Op0, Op1);
    SDValue And = DAG.getNode(ISD::AND, DL, MVT::i64, Srl,
                              DAG.getConstant(1, DL, MVT::i64));
    return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, And);
  }

  if (SDValue V = combineBinOpToReduce(N, DAG, Subtarget))
    return V;
  if (SDValue V = combineBinOpOfExtractToReduceTree(N, DAG, Subtarget))
    return V;

  if (DCI.isAfterLegalizeDAG())
    if (SDValue V = combineDeMorganOfBoolean(N, DAG))
      return V;

  // fold (and (select lhs, rhs, cc, -1, y), x) ->
  //      (select lhs, rhs, cc, x, (and x, y))
  return combineSelectAndUseCommutative(N, DAG, /*AllOnes*/ true, Subtarget);
}

// Try to pull an xor with 1 through a select idiom that uses czero_eqz/nez.
// FIXME: Generalize to other binary operators with same operand.
static SDValue combineOrOfCZERO(SDNode *N, SDValue N0, SDValue N1,
                                SelectionDAG &DAG) {
  assert(N->getOpcode() == ISD::OR && "Unexpected opcode");

  if (N0.getOpcode() != RISCVISD::CZERO_EQZ ||
      N1.getOpcode() != RISCVISD::CZERO_NEZ ||
      !N0.hasOneUse() || !N1.hasOneUse())
    return SDValue();

  // Should have the same condition.
  SDValue Cond = N0.getOperand(1);
  if (Cond != N1.getOperand(1))
    return SDValue();

  SDValue TrueV = N0.getOperand(0);
  SDValue FalseV = N1.getOperand(0);

  if (TrueV.getOpcode() != ISD::XOR || FalseV.getOpcode() != ISD::XOR ||
      TrueV.getOperand(1) != FalseV.getOperand(1) ||
      !isOneConstant(TrueV.getOperand(1)) ||
      !TrueV.hasOneUse() || !FalseV.hasOneUse())
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  SDValue NewN0 = DAG.getNode(RISCVISD::CZERO_EQZ, DL, VT, TrueV.getOperand(0),
                              Cond);
  SDValue NewN1 = DAG.getNode(RISCVISD::CZERO_NEZ, DL, VT, FalseV.getOperand(0),
                              Cond);
  SDValue NewOr = DAG.getNode(ISD::OR, DL, VT, NewN0, NewN1);
  return DAG.getNode(ISD::XOR, DL, VT, NewOr, TrueV.getOperand(1));
}

static SDValue performORCombine(SDNode *N, TargetLowering::DAGCombinerInfo &DCI,
                                const RISCVSubtarget &Subtarget) {
  SelectionDAG &DAG = DCI.DAG;

  if (SDValue V = combineBinOpToReduce(N, DAG, Subtarget))
    return V;
  if (SDValue V = combineBinOpOfExtractToReduceTree(N, DAG, Subtarget))
    return V;

  if (DCI.isAfterLegalizeDAG())
    if (SDValue V = combineDeMorganOfBoolean(N, DAG))
      return V;

  // Look for Or of CZERO_EQZ/NEZ with same condition which is the select idiom.
  // We may be able to pull a common operation out of the true and false value.
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (SDValue V = combineOrOfCZERO(N, N0, N1, DAG))
    return V;
  if (SDValue V = combineOrOfCZERO(N, N1, N0, DAG))
    return V;

  // fold (or (select cond, 0, y), x) ->
  //      (select cond, x, (or x, y))
  return combineSelectAndUseCommutative(N, DAG, /*AllOnes*/ false, Subtarget);
}

static SDValue performXORCombine(SDNode *N, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Pre-promote (i32 (xor (shl -1, X), ~0)) on RV64 with Zbs so we can use
  // (ADDI (BSET X0, X), -1). If we wait until/ type legalization, we'll create
  // RISCVISD:::SLLW and we can't recover it to use a BSET instruction.
  if (!RV64LegalI32 && Subtarget.is64Bit() && Subtarget.hasStdExtZbs() &&
      N->getValueType(0) == MVT::i32 && isAllOnesConstant(N1) &&
      N0.getOpcode() == ISD::SHL && isAllOnesConstant(N0.getOperand(0)) &&
      !isa<ConstantSDNode>(N0.getOperand(1)) && N0.hasOneUse()) {
    SDLoc DL(N);
    SDValue Op0 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, N0.getOperand(0));
    SDValue Op1 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N0.getOperand(1));
    SDValue Shl = DAG.getNode(ISD::SHL, DL, MVT::i64, Op0, Op1);
    SDValue And = DAG.getNOT(DL, Shl, MVT::i64);
    return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, And);
  }

  // fold (xor (sllw 1, x), -1) -> (rolw ~1, x)
  // NOTE: Assumes ROL being legal means ROLW is legal.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (N0.getOpcode() == RISCVISD::SLLW &&
      isAllOnesConstant(N1) && isOneConstant(N0.getOperand(0)) &&
      TLI.isOperationLegal(ISD::ROTL, MVT::i64)) {
    SDLoc DL(N);
    return DAG.getNode(RISCVISD::ROLW, DL, MVT::i64,
                       DAG.getConstant(~1, DL, MVT::i64), N0.getOperand(1));
  }

  // Fold (xor (setcc constant, y, setlt), 1) -> (setcc y, constant + 1, setlt)
  if (N0.getOpcode() == ISD::SETCC && isOneConstant(N1) && N0.hasOneUse()) {
    auto *ConstN00 = dyn_cast<ConstantSDNode>(N0.getOperand(0));
    ISD::CondCode CC = cast<CondCodeSDNode>(N0.getOperand(2))->get();
    if (ConstN00 && CC == ISD::SETLT) {
      EVT VT = N0.getValueType();
      SDLoc DL(N0);
      const APInt &Imm = ConstN00->getAPIntValue();
      if ((Imm + 1).isSignedIntN(12))
        return DAG.getSetCC(DL, VT, N0.getOperand(1),
                            DAG.getConstant(Imm + 1, DL, VT), CC);
    }
  }

  // Combine (xor (trunc (X cc Y)) 1) -> (trunc (X !cc Y)). This is needed with
  // RV64LegalI32 when the setcc is created after type legalization. An i1 xor
  // would have been promoted to i32, but the setcc would have i64 result.
  if (N->getValueType(0) == MVT::i32 && N0.getOpcode() == ISD::TRUNCATE &&
      isOneConstant(N1) && N0.getOperand(0).getOpcode() == ISD::SETCC) {
    SDValue N00 = N0.getOperand(0);
    SDLoc DL(N);
    SDValue LHS = N00.getOperand(0);
    SDValue RHS = N00.getOperand(1);
    SDValue CC = N00.getOperand(2);
    ISD::CondCode NotCC = ISD::getSetCCInverse(cast<CondCodeSDNode>(CC)->get(),
                                               LHS.getValueType());
    SDValue Setcc = DAG.getSetCC(SDLoc(N00), N0.getOperand(0).getValueType(),
                                 LHS, RHS, NotCC);
    return DAG.getNode(ISD::TRUNCATE, SDLoc(N0), N->getValueType(0), Setcc);
  }

  if (SDValue V = combineBinOpToReduce(N, DAG, Subtarget))
    return V;
  if (SDValue V = combineBinOpOfExtractToReduceTree(N, DAG, Subtarget))
    return V;

  // fold (xor (select cond, 0, y), x) ->
  //      (select cond, x, (xor x, y))
  return combineSelectAndUseCommutative(N, DAG, /*AllOnes*/ false, Subtarget);
}

// Try to expand a scalar multiply to a faster sequence.
static SDValue expandMul(SDNode *N, SelectionDAG &DAG,
                         TargetLowering::DAGCombinerInfo &DCI,
                         const RISCVSubtarget &Subtarget) {

  EVT VT = N->getValueType(0);

  // LI + MUL is usually smaller than the alternative sequence.
  if (DAG.getMachineFunction().getFunction().hasMinSize())
    return SDValue();

  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  if (VT != Subtarget.getXLenVT())
    return SDValue();

  const bool HasShlAdd =
      Subtarget.hasStdExtZba() || Subtarget.hasVendorXTHeadBa();

  ConstantSDNode *CNode = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!CNode)
    return SDValue();
  uint64_t MulAmt = CNode->getZExtValue();

  // WARNING: The code below is knowingly incorrect with regards to undef semantics.
  // We're adding additional uses of X here, and in principle, we should be freezing
  // X before doing so.  However, adding freeze here causes real regressions, and no
  // other target properly freezes X in these cases either.
  SDValue X = N->getOperand(0);

  if (HasShlAdd) {
    for (uint64_t Divisor : {3, 5, 9}) {
      if (MulAmt % Divisor != 0)
        continue;
      uint64_t MulAmt2 = MulAmt / Divisor;
      // 3/5/9 * 2^N ->  shl (shXadd X, X), N
      if (isPowerOf2_64(MulAmt2)) {
        SDLoc DL(N);
        SDValue X = N->getOperand(0);
        // Put the shift first if we can fold a zext into the
        // shift forming a slli.uw.
        if (X.getOpcode() == ISD::AND && isa<ConstantSDNode>(X.getOperand(1)) &&
            X.getConstantOperandVal(1) == UINT64_C(0xffffffff)) {
          SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, X,
                                    DAG.getConstant(Log2_64(MulAmt2), DL, VT));
          return DAG.getNode(RISCVISD::SHL_ADD, DL, VT, Shl,
                             DAG.getConstant(Log2_64(Divisor - 1), DL, VT),
                             Shl);
        }
        // Otherwise, put rhe shl second so that it can fold with following
        // instructions (e.g. sext or add).
        SDValue Mul359 =
            DAG.getNode(RISCVISD::SHL_ADD, DL, VT, X,
                        DAG.getConstant(Log2_64(Divisor - 1), DL, VT), X);
        return DAG.getNode(ISD::SHL, DL, VT, Mul359,
                           DAG.getConstant(Log2_64(MulAmt2), DL, VT));
      }

      // 3/5/9 * 3/5/9 -> shXadd (shYadd X, X), (shYadd X, X)
      if (MulAmt2 == 3 || MulAmt2 == 5 || MulAmt2 == 9) {
        SDLoc DL(N);
        SDValue Mul359 =
            DAG.getNode(RISCVISD::SHL_ADD, DL, VT, X,
                        DAG.getConstant(Log2_64(Divisor - 1), DL, VT), X);
        return DAG.getNode(RISCVISD::SHL_ADD, DL, VT, Mul359,
                           DAG.getConstant(Log2_64(MulAmt2 - 1), DL, VT),
                           Mul359);
      }
    }

    // If this is a power 2 + 2/4/8, we can use a shift followed by a single
    // shXadd. First check if this a sum of two power of 2s because that's
    // easy. Then count how many zeros are up to the first bit.
    if (isPowerOf2_64(MulAmt & (MulAmt - 1))) {
      unsigned ScaleShift = llvm::countr_zero(MulAmt);
      if (ScaleShift >= 1 && ScaleShift < 4) {
        unsigned ShiftAmt = Log2_64((MulAmt & (MulAmt - 1)));
        SDLoc DL(N);
        SDValue Shift1 =
            DAG.getNode(ISD::SHL, DL, VT, X, DAG.getConstant(ShiftAmt, DL, VT));
        return DAG.getNode(RISCVISD::SHL_ADD, DL, VT, X,
                           DAG.getConstant(ScaleShift, DL, VT), Shift1);
      }
    }

    // 2^(1,2,3) * 3,5,9 + 1 -> (shXadd (shYadd x, x), x)
    // This is the two instruction form, there are also three instruction
    // variants we could implement.  e.g.
    //   (2^(1,2,3) * 3,5,9 + 1) << C2
    //   2^(C1>3) * 3,5,9 +/- 1
    for (uint64_t Divisor : {3, 5, 9}) {
      uint64_t C = MulAmt - 1;
      if (C <= Divisor)
        continue;
      unsigned TZ = llvm::countr_zero(C);
      if ((C >> TZ) == Divisor && (TZ == 1 || TZ == 2 || TZ == 3)) {
        SDLoc DL(N);
        SDValue Mul359 =
            DAG.getNode(RISCVISD::SHL_ADD, DL, VT, X,
                        DAG.getConstant(Log2_64(Divisor - 1), DL, VT), X);
        return DAG.getNode(RISCVISD::SHL_ADD, DL, VT, Mul359,
                           DAG.getConstant(TZ, DL, VT), X);
      }
    }

    // 2^n + 2/4/8 + 1 -> (add (shl X, C1), (shXadd X, X))
    if (MulAmt > 2 && isPowerOf2_64((MulAmt - 1) & (MulAmt - 2))) {
      unsigned ScaleShift = llvm::countr_zero(MulAmt - 1);
      if (ScaleShift >= 1 && ScaleShift < 4) {
        unsigned ShiftAmt = Log2_64(((MulAmt - 1) & (MulAmt - 2)));
        SDLoc DL(N);
        SDValue Shift1 =
            DAG.getNode(ISD::SHL, DL, VT, X, DAG.getConstant(ShiftAmt, DL, VT));
        return DAG.getNode(ISD::ADD, DL, VT, Shift1,
                           DAG.getNode(RISCVISD::SHL_ADD, DL, VT, X,
                                       DAG.getConstant(ScaleShift, DL, VT), X));
      }
    }

    // 2^N - 3/5/9 --> (sub (shl X, C1), (shXadd X, x))
    for (uint64_t Offset : {3, 5, 9}) {
      if (isPowerOf2_64(MulAmt + Offset)) {
        SDLoc DL(N);
        SDValue Shift1 =
            DAG.getNode(ISD::SHL, DL, VT, X,
                        DAG.getConstant(Log2_64(MulAmt + Offset), DL, VT));
        SDValue Mul359 =
            DAG.getNode(RISCVISD::SHL_ADD, DL, VT, X,
                        DAG.getConstant(Log2_64(Offset - 1), DL, VT), X);
        return DAG.getNode(ISD::SUB, DL, VT, Shift1, Mul359);
      }
    }
  }

  // 2^N - 2^M -> (sub (shl X, C1), (shl X, C2))
  uint64_t MulAmtLowBit = MulAmt & (-MulAmt);
  if (isPowerOf2_64(MulAmt + MulAmtLowBit)) {
    uint64_t ShiftAmt1 = MulAmt + MulAmtLowBit;
    SDLoc DL(N);
    SDValue Shift1 = DAG.getNode(ISD::SHL, DL, VT, N->getOperand(0),
                                 DAG.getConstant(Log2_64(ShiftAmt1), DL, VT));
    SDValue Shift2 =
        DAG.getNode(ISD::SHL, DL, VT, N->getOperand(0),
                    DAG.getConstant(Log2_64(MulAmtLowBit), DL, VT));
    return DAG.getNode(ISD::SUB, DL, VT, Shift1, Shift2);
  }

  return SDValue();
}

// Combine vXi32 (mul (and (lshr X, 15), 0x10001), 0xffff) ->
// (bitcast (sra (v2Xi16 (bitcast X)), 15))
// Same for other equivalent types with other equivalent constants.
static SDValue combineVectorMulToSraBitcast(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // Do this for legal vectors unless they are i1 or i8 vectors.
  if (!VT.isVector() || !TLI.isTypeLegal(VT) || VT.getScalarSizeInBits() < 16)
    return SDValue();

  if (N->getOperand(0).getOpcode() != ISD::AND ||
      N->getOperand(0).getOperand(0).getOpcode() != ISD::SRL)
    return SDValue();

  SDValue And = N->getOperand(0);
  SDValue Srl = And.getOperand(0);

  APInt V1, V2, V3;
  if (!ISD::isConstantSplatVector(N->getOperand(1).getNode(), V1) ||
      !ISD::isConstantSplatVector(And.getOperand(1).getNode(), V2) ||
      !ISD::isConstantSplatVector(Srl.getOperand(1).getNode(), V3))
    return SDValue();

  unsigned HalfSize = VT.getScalarSizeInBits() / 2;
  if (!V1.isMask(HalfSize) || V2 != (1ULL | 1ULL << HalfSize) ||
      V3 != (HalfSize - 1))
    return SDValue();

  EVT HalfVT = EVT::getVectorVT(*DAG.getContext(),
                                EVT::getIntegerVT(*DAG.getContext(), HalfSize),
                                VT.getVectorElementCount() * 2);
  SDLoc DL(N);
  SDValue Cast = DAG.getNode(ISD::BITCAST, DL, HalfVT, Srl.getOperand(0));
  SDValue Sra = DAG.getNode(ISD::SRA, DL, HalfVT, Cast,
                            DAG.getConstant(HalfSize - 1, DL, HalfVT));
  return DAG.getNode(ISD::BITCAST, DL, VT, Sra);
}

static SDValue performMULCombine(SDNode *N, SelectionDAG &DAG,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const RISCVSubtarget &Subtarget) {
  EVT VT = N->getValueType(0);
  if (!VT.isVector())
    return expandMul(N, DAG, DCI, Subtarget);

  SDLoc DL(N);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue MulOper;
  unsigned AddSubOpc;

  // vmadd: (mul (add x, 1), y) -> (add (mul x, y), y)
  //        (mul x, add (y, 1)) -> (add x, (mul x, y))
  // vnmsub: (mul (sub 1, x), y) -> (sub y, (mul x, y))
  //         (mul x, (sub 1, y)) -> (sub x, (mul x, y))
  auto IsAddSubWith1 = [&](SDValue V) -> bool {
    AddSubOpc = V->getOpcode();
    if ((AddSubOpc == ISD::ADD || AddSubOpc == ISD::SUB) && V->hasOneUse()) {
      SDValue Opnd = V->getOperand(1);
      MulOper = V->getOperand(0);
      if (AddSubOpc == ISD::SUB)
        std::swap(Opnd, MulOper);
      if (isOneOrOneSplat(Opnd))
        return true;
    }
    return false;
  };

  if (IsAddSubWith1(N0)) {
    SDValue MulVal = DAG.getNode(ISD::MUL, DL, VT, N1, MulOper);
    return DAG.getNode(AddSubOpc, DL, VT, N1, MulVal);
  }

  if (IsAddSubWith1(N1)) {
    SDValue MulVal = DAG.getNode(ISD::MUL, DL, VT, N0, MulOper);
    return DAG.getNode(AddSubOpc, DL, VT, N0, MulVal);
  }

  if (SDValue V = combineBinOpOfZExt(N, DAG))
    return V;

  if (SDValue V = combineVectorMulToSraBitcast(N, DAG))
    return V;

  return SDValue();
}

/// According to the property that indexed load/store instructions zero-extend
/// their indices, try to narrow the type of index operand.
static bool narrowIndex(SDValue &N, ISD::MemIndexType IndexType, SelectionDAG &DAG) {
  if (isIndexTypeSigned(IndexType))
    return false;

  if (!N->hasOneUse())
    return false;

  EVT VT = N.getValueType();
  SDLoc DL(N);

  // In general, what we're doing here is seeing if we can sink a truncate to
  // a smaller element type into the expression tree building our index.
  // TODO: We can generalize this and handle a bunch more cases if useful.

  // Narrow a buildvector to the narrowest element type.  This requires less
  // work and less register pressure at high LMUL, and creates smaller constants
  // which may be cheaper to materialize.
  if (ISD::isBuildVectorOfConstantSDNodes(N.getNode())) {
    KnownBits Known = DAG.computeKnownBits(N);
    unsigned ActiveBits = std::max(8u, Known.countMaxActiveBits());
    LLVMContext &C = *DAG.getContext();
    EVT ResultVT = EVT::getIntegerVT(C, ActiveBits).getRoundIntegerType(C);
    if (ResultVT.bitsLT(VT.getVectorElementType())) {
      N = DAG.getNode(ISD::TRUNCATE, DL,
                      VT.changeVectorElementType(ResultVT), N);
      return true;
    }
  }

  // Handle the pattern (shl (zext x to ty), C) and bits(x) + C < bits(ty).
  if (N.getOpcode() != ISD::SHL)
    return false;

  SDValue N0 = N.getOperand(0);
  if (N0.getOpcode() != ISD::ZERO_EXTEND &&
      N0.getOpcode() != RISCVISD::VZEXT_VL)
    return false;
  if (!N0->hasOneUse())
    return false;

  APInt ShAmt;
  SDValue N1 = N.getOperand(1);
  if (!ISD::isConstantSplatVector(N1.getNode(), ShAmt))
    return false;

  SDValue Src = N0.getOperand(0);
  EVT SrcVT = Src.getValueType();
  unsigned SrcElen = SrcVT.getScalarSizeInBits();
  unsigned ShAmtV = ShAmt.getZExtValue();
  unsigned NewElen = PowerOf2Ceil(SrcElen + ShAmtV);
  NewElen = std::max(NewElen, 8U);

  // Skip if NewElen is not narrower than the original extended type.
  if (NewElen >= N0.getValueType().getScalarSizeInBits())
    return false;

  EVT NewEltVT = EVT::getIntegerVT(*DAG.getContext(), NewElen);
  EVT NewVT = SrcVT.changeVectorElementType(NewEltVT);

  SDValue NewExt = DAG.getNode(N0->getOpcode(), DL, NewVT, N0->ops());
  SDValue NewShAmtVec = DAG.getConstant(ShAmtV, DL, NewVT);
  N = DAG.getNode(ISD::SHL, DL, NewVT, NewExt, NewShAmtVec);
  return true;
}

// Replace (seteq (i64 (and X, 0xffffffff)), C1) with
// (seteq (i64 (sext_inreg (X, i32)), C1')) where C1' is C1 sign extended from
// bit 31. Same for setne. C1' may be cheaper to materialize and the sext_inreg
// can become a sext.w instead of a shift pair.
static SDValue performSETCCCombine(SDNode *N, SelectionDAG &DAG,
                                   const RISCVSubtarget &Subtarget) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  EVT OpVT = N0.getValueType();

  if (OpVT != MVT::i64 || !Subtarget.is64Bit())
    return SDValue();

  // RHS needs to be a constant.
  auto *N1C = dyn_cast<ConstantSDNode>(N1);
  if (!N1C)
    return SDValue();

  // LHS needs to be (and X, 0xffffffff).
  if (N0.getOpcode() != ISD::AND || !N0.hasOneUse() ||
      !isa<ConstantSDNode>(N0.getOperand(1)) ||
      N0.getConstantOperandVal(1) != UINT64_C(0xffffffff))
    return SDValue();

  // Looking for an equality compare.
  ISD::CondCode Cond = cast<CondCodeSDNode>(N->getOperand(2))->get();
  if (!isIntEqualitySetCC(Cond))
    return SDValue();

  // Don't do this if the sign bit is provably zero, it will be turned back into
  // an AND.
  APInt SignMask = APInt::getOneBitSet(64, 31);
  if (DAG.MaskedValueIsZero(N0.getOperand(0), SignMask))
    return SDValue();

  const APInt &C1 = N1C->getAPIntValue();

  SDLoc dl(N);
  // If the constant is larger than 2^32 - 1 it is impossible for both sides
  // to be equal.
  if (C1.getActiveBits() > 32)
    return DAG.getBoolConstant(Cond == ISD::SETNE, dl, VT, OpVT);

  SDValue SExtOp = DAG.getNode(ISD::SIGN_EXTEND_INREG, N, OpVT,
                               N0.getOperand(0), DAG.getValueType(MVT::i32));
  return DAG.getSetCC(dl, VT, SExtOp, DAG.getConstant(C1.trunc(32).sext(64),
                                                      dl, OpVT), Cond);
}

static SDValue
performSIGN_EXTEND_INREGCombine(SDNode *N, SelectionDAG &DAG,
                                const RISCVSubtarget &Subtarget) {
  SDValue Src = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // Fold (sext_inreg (fmv_x_anyexth X), i16) -> (fmv_x_signexth X)
  if (Src.getOpcode() == RISCVISD::FMV_X_ANYEXTH &&
      cast<VTSDNode>(N->getOperand(1))->getVT().bitsGE(MVT::i16))
    return DAG.getNode(RISCVISD::FMV_X_SIGNEXTH, SDLoc(N), VT,
                       Src.getOperand(0));

  return SDValue();
}

namespace {
// Forward declaration of the structure holding the necessary information to
// apply a combine.
struct CombineResult;

enum ExtKind : uint8_t { ZExt = 1 << 0, SExt = 1 << 1, FPExt = 1 << 2 };
/// Helper class for folding sign/zero extensions.
/// In particular, this class is used for the following combines:
/// add | add_vl | or disjoint -> vwadd(u) | vwadd(u)_w
/// sub | sub_vl -> vwsub(u) | vwsub(u)_w
/// mul | mul_vl -> vwmul(u) | vwmul_su
/// shl | shl_vl -> vwsll
/// fadd -> vfwadd | vfwadd_w
/// fsub -> vfwsub | vfwsub_w
/// fmul -> vfwmul
/// An object of this class represents an operand of the operation we want to
/// combine.
/// E.g., when trying to combine `mul_vl a, b`, we will have one instance of
/// NodeExtensionHelper for `a` and one for `b`.
///
/// This class abstracts away how the extension is materialized and
/// how its number of users affect the combines.
///
/// In particular:
/// - VWADD_W is conceptually == add(op0, sext(op1))
/// - VWADDU_W == add(op0, zext(op1))
/// - VWSUB_W == sub(op0, sext(op1))
/// - VWSUBU_W == sub(op0, zext(op1))
/// - VFWADD_W == fadd(op0, fpext(op1))
/// - VFWSUB_W == fsub(op0, fpext(op1))
/// And VMV_V_X_VL, depending on the value, is conceptually equivalent to
/// zext|sext(smaller_value).
struct NodeExtensionHelper {
  /// Records if this operand is like being zero extended.
  bool SupportsZExt;
  /// Records if this operand is like being sign extended.
  /// Note: SupportsZExt and SupportsSExt are not mutually exclusive. For
  /// instance, a splat constant (e.g., 3), would support being both sign and
  /// zero extended.
  bool SupportsSExt;
  /// Records if this operand is like being floating-Point extended.
  bool SupportsFPExt;
  /// This boolean captures whether we care if this operand would still be
  /// around after the folding happens.
  bool EnforceOneUse;
  /// Original value that this NodeExtensionHelper represents.
  SDValue OrigOperand;

  /// Get the value feeding the extension or the value itself.
  /// E.g., for zext(a), this would return a.
  SDValue getSource() const {
    switch (OrigOperand.getOpcode()) {
    case ISD::ZERO_EXTEND:
    case ISD::SIGN_EXTEND:
    case RISCVISD::VSEXT_VL:
    case RISCVISD::VZEXT_VL:
    case RISCVISD::FP_EXTEND_VL:
      return OrigOperand.getOperand(0);
    default:
      return OrigOperand;
    }
  }

  /// Check if this instance represents a splat.
  bool isSplat() const {
    return OrigOperand.getOpcode() == RISCVISD::VMV_V_X_VL ||
           OrigOperand.getOpcode() == ISD::SPLAT_VECTOR;
  }

  /// Get the extended opcode.
  unsigned getExtOpc(ExtKind SupportsExt) const {
    switch (SupportsExt) {
    case ExtKind::SExt:
      return RISCVISD::VSEXT_VL;
    case ExtKind::ZExt:
      return RISCVISD::VZEXT_VL;
    case ExtKind::FPExt:
      return RISCVISD::FP_EXTEND_VL;
    }
    llvm_unreachable("Unknown ExtKind enum");
  }

  /// Get or create a value that can feed \p Root with the given extension \p
  /// SupportsExt. If \p SExt is std::nullopt, this returns the source of this
  /// operand. \see ::getSource().
  SDValue getOrCreateExtendedOp(SDNode *Root, SelectionDAG &DAG,
                                const RISCVSubtarget &Subtarget,
                                std::optional<ExtKind> SupportsExt) const {
    if (!SupportsExt.has_value())
      return OrigOperand;

    MVT NarrowVT = getNarrowType(Root, *SupportsExt);

    SDValue Source = getSource();
    assert(Subtarget.getTargetLowering()->isTypeLegal(Source.getValueType()));
    if (Source.getValueType() == NarrowVT)
      return Source;

    unsigned ExtOpc = getExtOpc(*SupportsExt);

    // If we need an extension, we should be changing the type.
    SDLoc DL(OrigOperand);
    auto [Mask, VL] = getMaskAndVL(Root, DAG, Subtarget);
    switch (OrigOperand.getOpcode()) {
    case ISD::ZERO_EXTEND:
    case ISD::SIGN_EXTEND:
    case RISCVISD::VSEXT_VL:
    case RISCVISD::VZEXT_VL:
    case RISCVISD::FP_EXTEND_VL:
      return DAG.getNode(ExtOpc, DL, NarrowVT, Source, Mask, VL);
    case ISD::SPLAT_VECTOR:
      return DAG.getSplat(NarrowVT, DL, Source.getOperand(0));
    case RISCVISD::VMV_V_X_VL:
      return DAG.getNode(RISCVISD::VMV_V_X_VL, DL, NarrowVT,
                         DAG.getUNDEF(NarrowVT), Source.getOperand(1), VL);
    case RISCVISD::VFMV_V_F_VL:
      Source = Source.getOperand(1);
      assert(Source.getOpcode() == ISD::FP_EXTEND && "Unexpected source");
      Source = Source.getOperand(0);
      assert(Source.getValueType() == NarrowVT.getVectorElementType());
      return DAG.getNode(RISCVISD::VFMV_V_F_VL, DL, NarrowVT,
                         DAG.getUNDEF(NarrowVT), Source, VL);
    default:
      // Other opcodes can only come from the original LHS of VW(ADD|SUB)_W_VL
      // and that operand should already have the right NarrowVT so no
      // extension should be required at this point.
      llvm_unreachable("Unsupported opcode");
    }
  }

  /// Helper function to get the narrow type for \p Root.
  /// The narrow type is the type of \p Root where we divided the size of each
  /// element by 2. E.g., if Root's type <2xi16> -> narrow type <2xi8>.
  /// \pre Both the narrow type and the original type should be legal.
  static MVT getNarrowType(const SDNode *Root, ExtKind SupportsExt) {
    MVT VT = Root->getSimpleValueType(0);

    // Determine the narrow size.
    unsigned NarrowSize = VT.getScalarSizeInBits() / 2;

    MVT EltVT = SupportsExt == ExtKind::FPExt
                    ? MVT::getFloatingPointVT(NarrowSize)
                    : MVT::getIntegerVT(NarrowSize);

    assert((int)NarrowSize >= (SupportsExt == ExtKind::FPExt ? 16 : 8) &&
           "Trying to extend something we can't represent");
    MVT NarrowVT = MVT::getVectorVT(EltVT, VT.getVectorElementCount());
    return NarrowVT;
  }

  /// Get the opcode to materialize:
  /// Opcode(sext(a), sext(b)) -> newOpcode(a, b)
  static unsigned getSExtOpcode(unsigned Opcode) {
    switch (Opcode) {
    case ISD::ADD:
    case RISCVISD::ADD_VL:
    case RISCVISD::VWADD_W_VL:
    case RISCVISD::VWADDU_W_VL:
    case ISD::OR:
      return RISCVISD::VWADD_VL;
    case ISD::SUB:
    case RISCVISD::SUB_VL:
    case RISCVISD::VWSUB_W_VL:
    case RISCVISD::VWSUBU_W_VL:
      return RISCVISD::VWSUB_VL;
    case ISD::MUL:
    case RISCVISD::MUL_VL:
      return RISCVISD::VWMUL_VL;
    default:
      llvm_unreachable("Unexpected opcode");
    }
  }

  /// Get the opcode to materialize:
  /// Opcode(zext(a), zext(b)) -> newOpcode(a, b)
  static unsigned getZExtOpcode(unsigned Opcode) {
    switch (Opcode) {
    case ISD::ADD:
    case RISCVISD::ADD_VL:
    case RISCVISD::VWADD_W_VL:
    case RISCVISD::VWADDU_W_VL:
    case ISD::OR:
      return RISCVISD::VWADDU_VL;
    case ISD::SUB:
    case RISCVISD::SUB_VL:
    case RISCVISD::VWSUB_W_VL:
    case RISCVISD::VWSUBU_W_VL:
      return RISCVISD::VWSUBU_VL;
    case ISD::MUL:
    case RISCVISD::MUL_VL:
      return RISCVISD::VWMULU_VL;
    case ISD::SHL:
    case RISCVISD::SHL_VL:
      return RISCVISD::VWSLL_VL;
    default:
      llvm_unreachable("Unexpected opcode");
    }
  }

  /// Get the opcode to materialize:
  /// Opcode(fpext(a), fpext(b)) -> newOpcode(a, b)
  static unsigned getFPExtOpcode(unsigned Opcode) {
    switch (Opcode) {
    case RISCVISD::FADD_VL:
    case RISCVISD::VFWADD_W_VL:
      return RISCVISD::VFWADD_VL;
    case RISCVISD::FSUB_VL:
    case RISCVISD::VFWSUB_W_VL:
      return RISCVISD::VFWSUB_VL;
    case RISCVISD::FMUL_VL:
      return RISCVISD::VFWMUL_VL;
    default:
      llvm_unreachable("Unexpected opcode");
    }
  }

  /// Get the opcode to materialize \p Opcode(sext(a), zext(b)) ->
  /// newOpcode(a, b).
  static unsigned getSUOpcode(unsigned Opcode) {
    assert((Opcode == RISCVISD::MUL_VL || Opcode == ISD::MUL) &&
           "SU is only supported for MUL");
    return RISCVISD::VWMULSU_VL;
  }

  /// Get the opcode to materialize
  /// \p Opcode(a, s|z|fpext(b)) -> newOpcode(a, b).
  static unsigned getWOpcode(unsigned Opcode, ExtKind SupportsExt) {
    switch (Opcode) {
    case ISD::ADD:
    case RISCVISD::ADD_VL:
    case ISD::OR:
      return SupportsExt == ExtKind::SExt ? RISCVISD::VWADD_W_VL
                                          : RISCVISD::VWADDU_W_VL;
    case ISD::SUB:
    case RISCVISD::SUB_VL:
      return SupportsExt == ExtKind::SExt ? RISCVISD::VWSUB_W_VL
                                          : RISCVISD::VWSUBU_W_VL;
    case RISCVISD::FADD_VL:
      return RISCVISD::VFWADD_W_VL;
    case RISCVISD::FSUB_VL:
      return RISCVISD::VFWSUB_W_VL;
    default:
      llvm_unreachable("Unexpected opcode");
    }
  }

  using CombineToTry = std::function<std::optional<CombineResult>(
      SDNode * /*Root*/, const NodeExtensionHelper & /*LHS*/,
      const NodeExtensionHelper & /*RHS*/, SelectionDAG &,
      const RISCVSubtarget &)>;

  /// Check if this node needs to be fully folded or extended for all users.
  bool needToPromoteOtherUsers() const { return EnforceOneUse; }

  void fillUpExtensionSupportForSplat(SDNode *Root, SelectionDAG &DAG,
                                      const RISCVSubtarget &Subtarget) {
    unsigned Opc = OrigOperand.getOpcode();
    MVT VT = OrigOperand.getSimpleValueType();

    assert((Opc == ISD::SPLAT_VECTOR || Opc == RISCVISD::VMV_V_X_VL) &&
           "Unexpected Opcode");

    // The pasthru must be undef for tail agnostic.
    if (Opc == RISCVISD::VMV_V_X_VL && !OrigOperand.getOperand(0).isUndef())
      return;

    // Get the scalar value.
    SDValue Op = Opc == ISD::SPLAT_VECTOR ? OrigOperand.getOperand(0)
                                          : OrigOperand.getOperand(1);

    // See if we have enough sign bits or zero bits in the scalar to use a
    // widening opcode by splatting to smaller element size.
    unsigned EltBits = VT.getScalarSizeInBits();
    unsigned ScalarBits = Op.getValueSizeInBits();
    // If we're not getting all bits from the element, we need special handling.
    if (ScalarBits < EltBits) {
      // This should only occur on RV32.
      assert(Opc == RISCVISD::VMV_V_X_VL && EltBits == 64 && ScalarBits == 32 &&
             !Subtarget.is64Bit() && "Unexpected splat");
      // vmv.v.x sign extends narrow inputs.
      SupportsSExt = true;

      // If the input is positive, then sign extend is also zero extend.
      if (DAG.SignBitIsZero(Op))
        SupportsZExt = true;

      EnforceOneUse = false;
      return;
    }

    unsigned NarrowSize = EltBits / 2;
    // If the narrow type cannot be expressed with a legal VMV,
    // this is not a valid candidate.
    if (NarrowSize < 8)
      return;

    if (DAG.ComputeMaxSignificantBits(Op) <= NarrowSize)
      SupportsSExt = true;

    if (DAG.MaskedValueIsZero(Op,
                              APInt::getBitsSetFrom(ScalarBits, NarrowSize)))
      SupportsZExt = true;

    EnforceOneUse = false;
  }

  /// Helper method to set the various fields of this struct based on the
  /// type of \p Root.
  void fillUpExtensionSupport(SDNode *Root, SelectionDAG &DAG,
                              const RISCVSubtarget &Subtarget) {
    SupportsZExt = false;
    SupportsSExt = false;
    SupportsFPExt = false;
    EnforceOneUse = true;
    unsigned Opc = OrigOperand.getOpcode();
    // For the nodes we handle below, we end up using their inputs directly: see
    // getSource(). However since they either don't have a passthru or we check
    // that their passthru is undef, we can safely ignore their mask and VL.
    switch (Opc) {
    case ISD::ZERO_EXTEND:
    case ISD::SIGN_EXTEND: {
      MVT VT = OrigOperand.getSimpleValueType();
      if (!VT.isVector())
        break;

      SDValue NarrowElt = OrigOperand.getOperand(0);
      MVT NarrowVT = NarrowElt.getSimpleValueType();
      // i1 types are legal but we can't select V{S,Z}EXT_VLs with them.
      if (NarrowVT.getVectorElementType() == MVT::i1)
        break;

      SupportsZExt = Opc == ISD::ZERO_EXTEND;
      SupportsSExt = Opc == ISD::SIGN_EXTEND;
      break;
    }
    case RISCVISD::VZEXT_VL:
      SupportsZExt = true;
      break;
    case RISCVISD::VSEXT_VL:
      SupportsSExt = true;
      break;
    case RISCVISD::FP_EXTEND_VL:
      SupportsFPExt = true;
      break;
    case ISD::SPLAT_VECTOR:
    case RISCVISD::VMV_V_X_VL:
      fillUpExtensionSupportForSplat(Root, DAG, Subtarget);
      break;
    case RISCVISD::VFMV_V_F_VL: {
      MVT VT = OrigOperand.getSimpleValueType();

      if (!OrigOperand.getOperand(0).isUndef())
        break;

      SDValue Op = OrigOperand.getOperand(1);
      if (Op.getOpcode() != ISD::FP_EXTEND)
        break;

      unsigned NarrowSize = VT.getScalarSizeInBits() / 2;
      unsigned ScalarBits = Op.getOperand(0).getValueSizeInBits();
      if (NarrowSize != ScalarBits)
        break;

      SupportsFPExt = true;
      break;
    }
    default:
      break;
    }
  }

  /// Check if \p Root supports any extension folding combines.
  static bool isSupportedRoot(const SDNode *Root,
                              const RISCVSubtarget &Subtarget) {
    switch (Root->getOpcode()) {
    case ISD::ADD:
    case ISD::SUB:
    case ISD::MUL: {
      return Root->getValueType(0).isScalableVector();
    }
    case ISD::OR: {
      return Root->getValueType(0).isScalableVector() &&
             Root->getFlags().hasDisjoint();
    }
    // Vector Widening Integer Add/Sub/Mul Instructions
    case RISCVISD::ADD_VL:
    case RISCVISD::MUL_VL:
    case RISCVISD::VWADD_W_VL:
    case RISCVISD::VWADDU_W_VL:
    case RISCVISD::SUB_VL:
    case RISCVISD::VWSUB_W_VL:
    case RISCVISD::VWSUBU_W_VL:
    // Vector Widening Floating-Point Add/Sub/Mul Instructions
    case RISCVISD::FADD_VL:
    case RISCVISD::FSUB_VL:
    case RISCVISD::FMUL_VL:
    case RISCVISD::VFWADD_W_VL:
    case RISCVISD::VFWSUB_W_VL:
      return true;
    case ISD::SHL:
      return Root->getValueType(0).isScalableVector() &&
             Subtarget.hasStdExtZvbb();
    case RISCVISD::SHL_VL:
      return Subtarget.hasStdExtZvbb();
    default:
      return false;
    }
  }

  /// Build a NodeExtensionHelper for \p Root.getOperand(\p OperandIdx).
  NodeExtensionHelper(SDNode *Root, unsigned OperandIdx, SelectionDAG &DAG,
                      const RISCVSubtarget &Subtarget) {
    assert(isSupportedRoot(Root, Subtarget) &&
           "Trying to build an helper with an "
           "unsupported root");
    assert(OperandIdx < 2 && "Requesting something else than LHS or RHS");
    assert(DAG.getTargetLoweringInfo().isTypeLegal(Root->getValueType(0)));
    OrigOperand = Root->getOperand(OperandIdx);

    unsigned Opc = Root->getOpcode();
    switch (Opc) {
    // We consider
    // VW<ADD|SUB>_W(LHS, RHS) -> <ADD|SUB>(LHS, SEXT(RHS))
    // VW<ADD|SUB>U_W(LHS, RHS) -> <ADD|SUB>(LHS, ZEXT(RHS))
    // VFW<ADD|SUB>_W(LHS, RHS) -> F<ADD|SUB>(LHS, FPEXT(RHS))
    case RISCVISD::VWADD_W_VL:
    case RISCVISD::VWADDU_W_VL:
    case RISCVISD::VWSUB_W_VL:
    case RISCVISD::VWSUBU_W_VL:
    case RISCVISD::VFWADD_W_VL:
    case RISCVISD::VFWSUB_W_VL:
      if (OperandIdx == 1) {
        SupportsZExt =
            Opc == RISCVISD::VWADDU_W_VL || Opc == RISCVISD::VWSUBU_W_VL;
        SupportsSExt =
            Opc == RISCVISD::VWADD_W_VL || Opc == RISCVISD::VWSUB_W_VL;
        SupportsFPExt =
            Opc == RISCVISD::VFWADD_W_VL || Opc == RISCVISD::VFWSUB_W_VL;
        // There's no existing extension here, so we don't have to worry about
        // making sure it gets removed.
        EnforceOneUse = false;
        break;
      }
      [[fallthrough]];
    default:
      fillUpExtensionSupport(Root, DAG, Subtarget);
      break;
    }
  }

  /// Helper function to get the Mask and VL from \p Root.
  static std::pair<SDValue, SDValue>
  getMaskAndVL(const SDNode *Root, SelectionDAG &DAG,
               const RISCVSubtarget &Subtarget) {
    assert(isSupportedRoot(Root, Subtarget) && "Unexpected root");
    switch (Root->getOpcode()) {
    case ISD::ADD:
    case ISD::SUB:
    case ISD::MUL:
    case ISD::OR:
    case ISD::SHL: {
      SDLoc DL(Root);
      MVT VT = Root->getSimpleValueType(0);
      return getDefaultScalableVLOps(VT, DL, DAG, Subtarget);
    }
    default:
      return std::make_pair(Root->getOperand(3), Root->getOperand(4));
    }
  }

  /// Helper function to check if \p N is commutative with respect to the
  /// foldings that are supported by this class.
  static bool isCommutative(const SDNode *N) {
    switch (N->getOpcode()) {
    case ISD::ADD:
    case ISD::MUL:
    case ISD::OR:
    case RISCVISD::ADD_VL:
    case RISCVISD::MUL_VL:
    case RISCVISD::VWADD_W_VL:
    case RISCVISD::VWADDU_W_VL:
    case RISCVISD::FADD_VL:
    case RISCVISD::FMUL_VL:
    case RISCVISD::VFWADD_W_VL:
      return true;
    case ISD::SUB:
    case RISCVISD::SUB_VL:
    case RISCVISD::VWSUB_W_VL:
    case RISCVISD::VWSUBU_W_VL:
    case RISCVISD::FSUB_VL:
    case RISCVISD::VFWSUB_W_VL:
    case ISD::SHL:
    case RISCVISD::SHL_VL:
      return false;
    default:
      llvm_unreachable("Unexpected opcode");
    }
  }

  /// Get a list of combine to try for folding extensions in \p Root.
  /// Note that each returned CombineToTry function doesn't actually modify
  /// anything. Instead they produce an optional CombineResult that if not None,
  /// need to be materialized for the combine to be applied.
  /// \see CombineResult::materialize.
  /// If the related CombineToTry function returns std::nullopt, that means the
  /// combine didn't match.
  static SmallVector<CombineToTry> getSupportedFoldings(const SDNode *Root);
};

/// Helper structure that holds all the necessary information to materialize a
/// combine that does some extension folding.
struct CombineResult {
  /// Opcode to be generated when materializing the combine.
  unsigned TargetOpcode;
  // No value means no extension is needed.
  std::optional<ExtKind> LHSExt;
  std::optional<ExtKind> RHSExt;
  /// Root of the combine.
  SDNode *Root;
  /// LHS of the TargetOpcode.
  NodeExtensionHelper LHS;
  /// RHS of the TargetOpcode.
  NodeExtensionHelper RHS;

  CombineResult(unsigned TargetOpcode, SDNode *Root,
                const NodeExtensionHelper &LHS, std::optional<ExtKind> LHSExt,
                const NodeExtensionHelper &RHS, std::optional<ExtKind> RHSExt)
      : TargetOpcode(TargetOpcode), LHSExt(LHSExt), RHSExt(RHSExt), Root(Root),
        LHS(LHS), RHS(RHS) {}

  /// Return a value that uses TargetOpcode and that can be used to replace
  /// Root.
  /// The actual replacement is *not* done in that method.
  SDValue materialize(SelectionDAG &DAG,
                      const RISCVSubtarget &Subtarget) const {
    SDValue Mask, VL, Merge;
    std::tie(Mask, VL) =
        NodeExtensionHelper::getMaskAndVL(Root, DAG, Subtarget);
    switch (Root->getOpcode()) {
    default:
      Merge = Root->getOperand(2);
      break;
    case ISD::ADD:
    case ISD::SUB:
    case ISD::MUL:
    case ISD::OR:
    case ISD::SHL:
      Merge = DAG.getUNDEF(Root->getValueType(0));
      break;
    }
    return DAG.getNode(TargetOpcode, SDLoc(Root), Root->getValueType(0),
                       LHS.getOrCreateExtendedOp(Root, DAG, Subtarget, LHSExt),
                       RHS.getOrCreateExtendedOp(Root, DAG, Subtarget, RHSExt),
                       Merge, Mask, VL);
  }
};

/// Check if \p Root follows a pattern Root(ext(LHS), ext(RHS))
/// where `ext` is the same for both LHS and RHS (i.e., both are sext or both
/// are zext) and LHS and RHS can be folded into Root.
/// AllowExtMask define which form `ext` can take in this pattern.
///
/// \note If the pattern can match with both zext and sext, the returned
/// CombineResult will feature the zext result.
///
/// \returns std::nullopt if the pattern doesn't match or a CombineResult that
/// can be used to apply the pattern.
static std::optional<CombineResult>
canFoldToVWWithSameExtensionImpl(SDNode *Root, const NodeExtensionHelper &LHS,
                                 const NodeExtensionHelper &RHS,
                                 uint8_t AllowExtMask, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  if ((AllowExtMask & ExtKind::ZExt) && LHS.SupportsZExt && RHS.SupportsZExt)
    return CombineResult(NodeExtensionHelper::getZExtOpcode(Root->getOpcode()),
                         Root, LHS, /*LHSExt=*/{ExtKind::ZExt}, RHS,
                         /*RHSExt=*/{ExtKind::ZExt});
  if ((AllowExtMask & ExtKind::SExt) && LHS.SupportsSExt && RHS.SupportsSExt)
    return CombineResult(NodeExtensionHelper::getSExtOpcode(Root->getOpcode()),
                         Root, LHS, /*LHSExt=*/{ExtKind::SExt}, RHS,
                         /*RHSExt=*/{ExtKind::SExt});
  if ((AllowExtMask & ExtKind::FPExt) && LHS.SupportsFPExt && RHS.SupportsFPExt)
    return CombineResult(NodeExtensionHelper::getFPExtOpcode(Root->getOpcode()),
                         Root, LHS, /*LHSExt=*/{ExtKind::FPExt}, RHS,
                         /*RHSExt=*/{ExtKind::FPExt});
  return std::nullopt;
}

/// Check if \p Root follows a pattern Root(ext(LHS), ext(RHS))
/// where `ext` is the same for both LHS and RHS (i.e., both are sext or both
/// are zext) and LHS and RHS can be folded into Root.
///
/// \returns std::nullopt if the pattern doesn't match or a CombineResult that
/// can be used to apply the pattern.
static std::optional<CombineResult>
canFoldToVWWithSameExtension(SDNode *Root, const NodeExtensionHelper &LHS,
                             const NodeExtensionHelper &RHS, SelectionDAG &DAG,
                             const RISCVSubtarget &Subtarget) {
  return canFoldToVWWithSameExtensionImpl(
      Root, LHS, RHS, ExtKind::ZExt | ExtKind::SExt | ExtKind::FPExt, DAG,
      Subtarget);
}

/// Check if \p Root follows a pattern Root(LHS, ext(RHS))
///
/// \returns std::nullopt if the pattern doesn't match or a CombineResult that
/// can be used to apply the pattern.
static std::optional<CombineResult>
canFoldToVW_W(SDNode *Root, const NodeExtensionHelper &LHS,
              const NodeExtensionHelper &RHS, SelectionDAG &DAG,
              const RISCVSubtarget &Subtarget) {
  if (RHS.SupportsFPExt)
    return CombineResult(
        NodeExtensionHelper::getWOpcode(Root->getOpcode(), ExtKind::FPExt),
        Root, LHS, /*LHSExt=*/std::nullopt, RHS, /*RHSExt=*/{ExtKind::FPExt});

  // FIXME: Is it useful to form a vwadd.wx or vwsub.wx if it removes a scalar
  // sext/zext?
  // Control this behavior behind an option (AllowSplatInVW_W) for testing
  // purposes.
  if (RHS.SupportsZExt && (!RHS.isSplat() || AllowSplatInVW_W))
    return CombineResult(
        NodeExtensionHelper::getWOpcode(Root->getOpcode(), ExtKind::ZExt), Root,
        LHS, /*LHSExt=*/std::nullopt, RHS, /*RHSExt=*/{ExtKind::ZExt});
  if (RHS.SupportsSExt && (!RHS.isSplat() || AllowSplatInVW_W))
    return CombineResult(
        NodeExtensionHelper::getWOpcode(Root->getOpcode(), ExtKind::SExt), Root,
        LHS, /*LHSExt=*/std::nullopt, RHS, /*RHSExt=*/{ExtKind::SExt});
  return std::nullopt;
}

/// Check if \p Root follows a pattern Root(sext(LHS), sext(RHS))
///
/// \returns std::nullopt if the pattern doesn't match or a CombineResult that
/// can be used to apply the pattern.
static std::optional<CombineResult>
canFoldToVWWithSEXT(SDNode *Root, const NodeExtensionHelper &LHS,
                    const NodeExtensionHelper &RHS, SelectionDAG &DAG,
                    const RISCVSubtarget &Subtarget) {
  return canFoldToVWWithSameExtensionImpl(Root, LHS, RHS, ExtKind::SExt, DAG,
                                          Subtarget);
}

/// Check if \p Root follows a pattern Root(zext(LHS), zext(RHS))
///
/// \returns std::nullopt if the pattern doesn't match or a CombineResult that
/// can be used to apply the pattern.
static std::optional<CombineResult>
canFoldToVWWithZEXT(SDNode *Root, const NodeExtensionHelper &LHS,
                    const NodeExtensionHelper &RHS, SelectionDAG &DAG,
                    const RISCVSubtarget &Subtarget) {
  return canFoldToVWWithSameExtensionImpl(Root, LHS, RHS, ExtKind::ZExt, DAG,
                                          Subtarget);
}

/// Check if \p Root follows a pattern Root(fpext(LHS), fpext(RHS))
///
/// \returns std::nullopt if the pattern doesn't match or a CombineResult that
/// can be used to apply the pattern.
static std::optional<CombineResult>
canFoldToVWWithFPEXT(SDNode *Root, const NodeExtensionHelper &LHS,
                     const NodeExtensionHelper &RHS, SelectionDAG &DAG,
                     const RISCVSubtarget &Subtarget) {
  return canFoldToVWWithSameExtensionImpl(Root, LHS, RHS, ExtKind::FPExt, DAG,
                                          Subtarget);
}

/// Check if \p Root follows a pattern Root(sext(LHS), zext(RHS))
///
/// \returns std::nullopt if the pattern doesn't match or a CombineResult that
/// can be used to apply the pattern.
static std::optional<CombineResult>
canFoldToVW_SU(SDNode *Root, const NodeExtensionHelper &LHS,
               const NodeExtensionHelper &RHS, SelectionDAG &DAG,
               const RISCVSubtarget &Subtarget) {

  if (!LHS.SupportsSExt || !RHS.SupportsZExt)
    return std::nullopt;
  return CombineResult(NodeExtensionHelper::getSUOpcode(Root->getOpcode()),
                       Root, LHS, /*LHSExt=*/{ExtKind::SExt}, RHS,
                       /*RHSExt=*/{ExtKind::ZExt});
}

SmallVector<NodeExtensionHelper::CombineToTry>
NodeExtensionHelper::getSupportedFoldings(const SDNode *Root) {
  SmallVector<CombineToTry> Strategies;
  switch (Root->getOpcode()) {
  case ISD::ADD:
  case ISD::SUB:
  case ISD::OR:
  case RISCVISD::ADD_VL:
  case RISCVISD::SUB_VL:
  case RISCVISD::FADD_VL:
  case RISCVISD::FSUB_VL:
    // add|sub|fadd|fsub-> vwadd(u)|vwsub(u)|vfwadd|vfwsub
    Strategies.push_back(canFoldToVWWithSameExtension);
    // add|sub|fadd|fsub -> vwadd(u)_w|vwsub(u)_w}|vfwadd_w|vfwsub_w
    Strategies.push_back(canFoldToVW_W);
    break;
  case RISCVISD::FMUL_VL:
    Strategies.push_back(canFoldToVWWithSameExtension);
    break;
  case ISD::MUL:
  case RISCVISD::MUL_VL:
    // mul -> vwmul(u)
    Strategies.push_back(canFoldToVWWithSameExtension);
    // mul -> vwmulsu
    Strategies.push_back(canFoldToVW_SU);
    break;
  case ISD::SHL:
  case RISCVISD::SHL_VL:
    // shl -> vwsll
    Strategies.push_back(canFoldToVWWithZEXT);
    break;
  case RISCVISD::VWADD_W_VL:
  case RISCVISD::VWSUB_W_VL:
    // vwadd_w|vwsub_w -> vwadd|vwsub
    Strategies.push_back(canFoldToVWWithSEXT);
    break;
  case RISCVISD::VWADDU_W_VL:
  case RISCVISD::VWSUBU_W_VL:
    // vwaddu_w|vwsubu_w -> vwaddu|vwsubu
    Strategies.push_back(canFoldToVWWithZEXT);
    break;
  case RISCVISD::VFWADD_W_VL:
  case RISCVISD::VFWSUB_W_VL:
    // vfwadd_w|vfwsub_w -> vfwadd|vfwsub
    Strategies.push_back(canFoldToVWWithFPEXT);
    break;
  default:
    llvm_unreachable("Unexpected opcode");
  }
  return Strategies;
}
} // End anonymous namespace.

/// Combine a binary operation to its equivalent VW or VW_W form.
/// The supported combines are:
/// add | add_vl | or disjoint -> vwadd(u) | vwadd(u)_w
/// sub | sub_vl -> vwsub(u) | vwsub(u)_w
/// mul | mul_vl -> vwmul(u) | vwmul_su
/// shl | shl_vl -> vwsll
/// fadd_vl ->  vfwadd | vfwadd_w
/// fsub_vl ->  vfwsub | vfwsub_w
/// fmul_vl ->  vfwmul
/// vwadd_w(u) -> vwadd(u)
/// vwsub_w(u) -> vwsub(u)
/// vfwadd_w -> vfwadd
/// vfwsub_w -> vfwsub
static SDValue combineBinOp_VLToVWBinOp_VL(SDNode *N,
                                           TargetLowering::DAGCombinerInfo &DCI,
                                           const RISCVSubtarget &Subtarget) {
  SelectionDAG &DAG = DCI.DAG;
  if (DCI.isBeforeLegalize())
    return SDValue();

  if (!NodeExtensionHelper::isSupportedRoot(N, Subtarget))
    return SDValue();

  SmallVector<SDNode *> Worklist;
  SmallSet<SDNode *, 8> Inserted;
  Worklist.push_back(N);
  Inserted.insert(N);
  SmallVector<CombineResult> CombinesToApply;

  while (!Worklist.empty()) {
    SDNode *Root = Worklist.pop_back_val();
    if (!NodeExtensionHelper::isSupportedRoot(Root, Subtarget))
      return SDValue();

    NodeExtensionHelper LHS(Root, 0, DAG, Subtarget);
    NodeExtensionHelper RHS(Root, 1, DAG, Subtarget);
    auto AppendUsersIfNeeded = [&Worklist,
                                &Inserted](const NodeExtensionHelper &Op) {
      if (Op.needToPromoteOtherUsers()) {
        for (SDNode *TheUse : Op.OrigOperand->uses()) {
          if (Inserted.insert(TheUse).second)
            Worklist.push_back(TheUse);
        }
      }
    };

    // Control the compile time by limiting the number of node we look at in
    // total.
    if (Inserted.size() > ExtensionMaxWebSize)
      return SDValue();

    SmallVector<NodeExtensionHelper::CombineToTry> FoldingStrategies =
        NodeExtensionHelper::getSupportedFoldings(Root);

    assert(!FoldingStrategies.empty() && "Nothing to be folded");
    bool Matched = false;
    for (int Attempt = 0;
         (Attempt != 1 + NodeExtensionHelper::isCommutative(Root)) && !Matched;
         ++Attempt) {

      for (NodeExtensionHelper::CombineToTry FoldingStrategy :
           FoldingStrategies) {
        std::optional<CombineResult> Res =
            FoldingStrategy(Root, LHS, RHS, DAG, Subtarget);
        if (Res) {
          Matched = true;
          CombinesToApply.push_back(*Res);
          // All the inputs that are extended need to be folded, otherwise
          // we would be leaving the old input (since it is may still be used),
          // and the new one.
          if (Res->LHSExt.has_value())
            AppendUsersIfNeeded(LHS);
          if (Res->RHSExt.has_value())
            AppendUsersIfNeeded(RHS);
          break;
        }
      }
      std::swap(LHS, RHS);
    }
    // Right now we do an all or nothing approach.
    if (!Matched)
      return SDValue();
  }
  // Store the value for the replacement of the input node separately.
  SDValue InputRootReplacement;
  // We do the RAUW after we materialize all the combines, because some replaced
  // nodes may be feeding some of the yet-to-be-replaced nodes. Put differently,
  // some of these nodes may appear in the NodeExtensionHelpers of some of the
  // yet-to-be-visited CombinesToApply roots.
  SmallVector<std::pair<SDValue, SDValue>> ValuesToReplace;
  ValuesToReplace.reserve(CombinesToApply.size());
  for (CombineResult Res : CombinesToApply) {
    SDValue NewValue = Res.materialize(DAG, Subtarget);
    if (!InputRootReplacement) {
      assert(Res.Root == N &&
             "First element is expected to be the current node");
      InputRootReplacement = NewValue;
    } else {
      ValuesToReplace.emplace_back(SDValue(Res.Root, 0), NewValue);
    }
  }
  for (std::pair<SDValue, SDValue> OldNewValues : ValuesToReplace) {
    DAG.ReplaceAllUsesOfValueWith(OldNewValues.first, OldNewValues.second);
    DCI.AddToWorklist(OldNewValues.second.getNode());
  }
  return InputRootReplacement;
}

// Fold (vwadd(u).wv y, (vmerge cond, x, 0)) -> vwadd(u).wv y, x, y, cond
//      (vwsub(u).wv y, (vmerge cond, x, 0)) -> vwsub(u).wv y, x, y, cond
// y will be the Passthru and cond will be the Mask.
static SDValue combineVWADDSUBWSelect(SDNode *N, SelectionDAG &DAG) {
  unsigned Opc = N->getOpcode();
  assert(Opc == RISCVISD::VWADD_W_VL || Opc == RISCVISD::VWADDU_W_VL ||
         Opc == RISCVISD::VWSUB_W_VL || Opc == RISCVISD::VWSUBU_W_VL);

  SDValue Y = N->getOperand(0);
  SDValue MergeOp = N->getOperand(1);
  unsigned MergeOpc = MergeOp.getOpcode();

  if (MergeOpc != RISCVISD::VMERGE_VL && MergeOpc != ISD::VSELECT)
    return SDValue();

  SDValue X = MergeOp->getOperand(1);

  if (!MergeOp.hasOneUse())
    return SDValue();

  // Passthru should be undef
  SDValue Passthru = N->getOperand(2);
  if (!Passthru.isUndef())
    return SDValue();

  // Mask should be all ones
  SDValue Mask = N->getOperand(3);
  if (Mask.getOpcode() != RISCVISD::VMSET_VL)
    return SDValue();

  // False value of MergeOp should be all zeros
  SDValue Z = MergeOp->getOperand(2);

  if (Z.getOpcode() == ISD::INSERT_SUBVECTOR &&
      (isNullOrNullSplat(Z.getOperand(0)) || Z.getOperand(0).isUndef()))
    Z = Z.getOperand(1);

  if (!ISD::isConstantSplatVectorAllZeros(Z.getNode()))
    return SDValue();

  return DAG.getNode(Opc, SDLoc(N), N->getValueType(0),
                     {Y, X, Y, MergeOp->getOperand(0), N->getOperand(4)},
                     N->getFlags());
}

static SDValue performVWADDSUBW_VLCombine(SDNode *N,
                                          TargetLowering::DAGCombinerInfo &DCI,
                                          const RISCVSubtarget &Subtarget) {
  [[maybe_unused]] unsigned Opc = N->getOpcode();
  assert(Opc == RISCVISD::VWADD_W_VL || Opc == RISCVISD::VWADDU_W_VL ||
         Opc == RISCVISD::VWSUB_W_VL || Opc == RISCVISD::VWSUBU_W_VL);

  if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
    return V;

  return combineVWADDSUBWSelect(N, DCI.DAG);
}

// Helper function for performMemPairCombine.
// Try to combine the memory loads/stores LSNode1 and LSNode2
// into a single memory pair operation.
static SDValue tryMemPairCombine(SelectionDAG &DAG, LSBaseSDNode *LSNode1,
                                 LSBaseSDNode *LSNode2, SDValue BasePtr,
                                 uint64_t Imm) {
  SmallPtrSet<const SDNode *, 32> Visited;
  SmallVector<const SDNode *, 8> Worklist = {LSNode1, LSNode2};

  if (SDNode::hasPredecessorHelper(LSNode1, Visited, Worklist) ||
      SDNode::hasPredecessorHelper(LSNode2, Visited, Worklist))
    return SDValue();

  MachineFunction &MF = DAG.getMachineFunction();
  const RISCVSubtarget &Subtarget = MF.getSubtarget<RISCVSubtarget>();

  // The new operation has twice the width.
  MVT XLenVT = Subtarget.getXLenVT();
  EVT MemVT = LSNode1->getMemoryVT();
  EVT NewMemVT = (MemVT == MVT::i32) ? MVT::i64 : MVT::i128;
  MachineMemOperand *MMO = LSNode1->getMemOperand();
  MachineMemOperand *NewMMO = MF.getMachineMemOperand(
      MMO, MMO->getPointerInfo(), MemVT == MVT::i32 ? 8 : 16);

  if (LSNode1->getOpcode() == ISD::LOAD) {
    auto Ext = cast<LoadSDNode>(LSNode1)->getExtensionType();
    unsigned Opcode;
    if (MemVT == MVT::i32)
      Opcode = (Ext == ISD::ZEXTLOAD) ? RISCVISD::TH_LWUD : RISCVISD::TH_LWD;
    else
      Opcode = RISCVISD::TH_LDD;

    SDValue Res = DAG.getMemIntrinsicNode(
        Opcode, SDLoc(LSNode1), DAG.getVTList({XLenVT, XLenVT, MVT::Other}),
        {LSNode1->getChain(), BasePtr,
         DAG.getConstant(Imm, SDLoc(LSNode1), XLenVT)},
        NewMemVT, NewMMO);

    SDValue Node1 =
        DAG.getMergeValues({Res.getValue(0), Res.getValue(2)}, SDLoc(LSNode1));
    SDValue Node2 =
        DAG.getMergeValues({Res.getValue(1), Res.getValue(2)}, SDLoc(LSNode2));

    DAG.ReplaceAllUsesWith(LSNode2, Node2.getNode());
    return Node1;
  } else {
    unsigned Opcode = (MemVT == MVT::i32) ? RISCVISD::TH_SWD : RISCVISD::TH_SDD;

    SDValue Res = DAG.getMemIntrinsicNode(
        Opcode, SDLoc(LSNode1), DAG.getVTList(MVT::Other),
        {LSNode1->getChain(), LSNode1->getOperand(1), LSNode2->getOperand(1),
         BasePtr, DAG.getConstant(Imm, SDLoc(LSNode1), XLenVT)},
        NewMemVT, NewMMO);

    DAG.ReplaceAllUsesWith(LSNode2, Res.getNode());
    return Res;
  }
}

// Try to combine two adjacent loads/stores to a single pair instruction from
// the XTHeadMemPair vendor extension.
static SDValue performMemPairCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  MachineFunction &MF = DAG.getMachineFunction();
  const RISCVSubtarget &Subtarget = MF.getSubtarget<RISCVSubtarget>();

  // Target does not support load/store pair.
  if (!Subtarget.hasVendorXTHeadMemPair())
    return SDValue();

  LSBaseSDNode *LSNode1 = cast<LSBaseSDNode>(N);
  EVT MemVT = LSNode1->getMemoryVT();
  unsigned OpNum = LSNode1->getOpcode() == ISD::LOAD ? 1 : 2;

  // No volatile, indexed or atomic loads/stores.
  if (!LSNode1->isSimple() || LSNode1->isIndexed())
    return SDValue();

  // Function to get a base + constant representation from a memory value.
  auto ExtractBaseAndOffset = [](SDValue Ptr) -> std::pair<SDValue, uint64_t> {
    if (Ptr->getOpcode() == ISD::ADD)
      if (auto *C1 = dyn_cast<ConstantSDNode>(Ptr->getOperand(1)))
        return {Ptr->getOperand(0), C1->getZExtValue()};
    return {Ptr, 0};
  };

  auto [Base1, Offset1] = ExtractBaseAndOffset(LSNode1->getOperand(OpNum));

  SDValue Chain = N->getOperand(0);
  for (SDNode::use_iterator UI = Chain->use_begin(), UE = Chain->use_end();
       UI != UE; ++UI) {
    SDUse &Use = UI.getUse();
    if (Use.getUser() != N && Use.getResNo() == 0 &&
        Use.getUser()->getOpcode() == N->getOpcode()) {
      LSBaseSDNode *LSNode2 = cast<LSBaseSDNode>(Use.getUser());

      // No volatile, indexed or atomic loads/stores.
      if (!LSNode2->isSimple() || LSNode2->isIndexed())
        continue;

      // Check if LSNode1 and LSNode2 have the same type and extension.
      if (LSNode1->getOpcode() == ISD::LOAD)
        if (cast<LoadSDNode>(LSNode2)->getExtensionType() !=
            cast<LoadSDNode>(LSNode1)->getExtensionType())
          continue;

      if (LSNode1->getMemoryVT() != LSNode2->getMemoryVT())
        continue;

      auto [Base2, Offset2] = ExtractBaseAndOffset(LSNode2->getOperand(OpNum));

      // Check if the base pointer is the same for both instruction.
      if (Base1 != Base2)
        continue;

      // Check if the offsets match the XTHeadMemPair encoding contraints.
      bool Valid = false;
      if (MemVT == MVT::i32) {
        // Check for adjacent i32 values and a 2-bit index.
        if ((Offset1 + 4 == Offset2) && isShiftedUInt<2, 3>(Offset1))
          Valid = true;
      } else if (MemVT == MVT::i64) {
        // Check for adjacent i64 values and a 2-bit index.
        if ((Offset1 + 8 == Offset2) && isShiftedUInt<2, 4>(Offset1))
          Valid = true;
      }

      if (!Valid)
        continue;

      // Try to combine.
      if (SDValue Res =
              tryMemPairCombine(DAG, LSNode1, LSNode2, Base1, Offset1))
        return Res;
    }
  }

  return SDValue();
}

// Fold
//   (fp_to_int (froundeven X)) -> fcvt X, rne
//   (fp_to_int (ftrunc X))     -> fcvt X, rtz
//   (fp_to_int (ffloor X))     -> fcvt X, rdn
//   (fp_to_int (fceil X))      -> fcvt X, rup
//   (fp_to_int (fround X))     -> fcvt X, rmm
//   (fp_to_int (frint X))      -> fcvt X
static SDValue performFP_TO_INTCombine(SDNode *N,
                                       TargetLowering::DAGCombinerInfo &DCI,
                                       const RISCVSubtarget &Subtarget) {
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  MVT XLenVT = Subtarget.getXLenVT();

  SDValue Src = N->getOperand(0);

  // Don't do this for strict-fp Src.
  if (Src->isStrictFPOpcode() || Src->isTargetStrictFPOpcode())
    return SDValue();

  // Ensure the FP type is legal.
  if (!TLI.isTypeLegal(Src.getValueType()))
    return SDValue();

  // Don't do this for f16 with Zfhmin and not Zfh.
  if (Src.getValueType() == MVT::f16 && !Subtarget.hasStdExtZfh())
    return SDValue();

  RISCVFPRndMode::RoundingMode FRM = matchRoundingOp(Src.getOpcode());
  // If the result is invalid, we didn't find a foldable instruction.
  if (FRM == RISCVFPRndMode::Invalid)
    return SDValue();

  SDLoc DL(N);
  bool IsSigned = N->getOpcode() == ISD::FP_TO_SINT;
  EVT VT = N->getValueType(0);

  if (VT.isVector() && TLI.isTypeLegal(VT)) {
    MVT SrcVT = Src.getSimpleValueType();
    MVT SrcContainerVT = SrcVT;
    MVT ContainerVT = VT.getSimpleVT();
    SDValue XVal = Src.getOperand(0);

    // For widening and narrowing conversions we just combine it into a
    // VFCVT_..._VL node, as there are no specific VFWCVT/VFNCVT VL nodes. They
    // end up getting lowered to their appropriate pseudo instructions based on
    // their operand types
    if (VT.getScalarSizeInBits() > SrcVT.getScalarSizeInBits() * 2 ||
        VT.getScalarSizeInBits() * 2 < SrcVT.getScalarSizeInBits())
      return SDValue();

    // Make fixed-length vectors scalable first
    if (SrcVT.isFixedLengthVector()) {
      SrcContainerVT = getContainerForFixedLengthVector(DAG, SrcVT, Subtarget);
      XVal = convertToScalableVector(SrcContainerVT, XVal, DAG, Subtarget);
      ContainerVT =
          getContainerForFixedLengthVector(DAG, ContainerVT, Subtarget);
    }

    auto [Mask, VL] =
        getDefaultVLOps(SrcVT, SrcContainerVT, DL, DAG, Subtarget);

    SDValue FpToInt;
    if (FRM == RISCVFPRndMode::RTZ) {
      // Use the dedicated trunc static rounding mode if we're truncating so we
      // don't need to generate calls to fsrmi/fsrm
      unsigned Opc =
          IsSigned ? RISCVISD::VFCVT_RTZ_X_F_VL : RISCVISD::VFCVT_RTZ_XU_F_VL;
      FpToInt = DAG.getNode(Opc, DL, ContainerVT, XVal, Mask, VL);
    } else if (FRM == RISCVFPRndMode::DYN) {
      unsigned Opc =
          IsSigned ? RISCVISD::VFCVT_X_F_VL : RISCVISD::VFCVT_XU_F_VL;
      FpToInt = DAG.getNode(Opc, DL, ContainerVT, XVal, Mask, VL);
    } else {
      unsigned Opc =
          IsSigned ? RISCVISD::VFCVT_RM_X_F_VL : RISCVISD::VFCVT_RM_XU_F_VL;
      FpToInt = DAG.getNode(Opc, DL, ContainerVT, XVal, Mask,
                            DAG.getTargetConstant(FRM, DL, XLenVT), VL);
    }

    // If converted from fixed-length to scalable, convert back
    if (VT.isFixedLengthVector())
      FpToInt = convertFromScalableVector(VT, FpToInt, DAG, Subtarget);

    return FpToInt;
  }

  // Only handle XLen or i32 types. Other types narrower than XLen will
  // eventually be legalized to XLenVT.
  if (VT != MVT::i32 && VT != XLenVT)
    return SDValue();

  unsigned Opc;
  if (VT == XLenVT)
    Opc = IsSigned ? RISCVISD::FCVT_X : RISCVISD::FCVT_XU;
  else
    Opc = IsSigned ? RISCVISD::FCVT_W_RV64 : RISCVISD::FCVT_WU_RV64;

  SDValue FpToInt = DAG.getNode(Opc, DL, XLenVT, Src.getOperand(0),
                                DAG.getTargetConstant(FRM, DL, XLenVT));
  return DAG.getNode(ISD::TRUNCATE, DL, VT, FpToInt);
}

// Fold
//   (fp_to_int_sat (froundeven X)) -> (select X == nan, 0, (fcvt X, rne))
//   (fp_to_int_sat (ftrunc X))     -> (select X == nan, 0, (fcvt X, rtz))
//   (fp_to_int_sat (ffloor X))     -> (select X == nan, 0, (fcvt X, rdn))
//   (fp_to_int_sat (fceil X))      -> (select X == nan, 0, (fcvt X, rup))
//   (fp_to_int_sat (fround X))     -> (select X == nan, 0, (fcvt X, rmm))
//   (fp_to_int_sat (frint X))      -> (select X == nan, 0, (fcvt X, dyn))
static SDValue performFP_TO_INT_SATCombine(SDNode *N,
                                       TargetLowering::DAGCombinerInfo &DCI,
                                       const RISCVSubtarget &Subtarget) {
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  MVT XLenVT = Subtarget.getXLenVT();

  // Only handle XLen types. Other types narrower than XLen will eventually be
  // legalized to XLenVT.
  EVT DstVT = N->getValueType(0);
  if (DstVT != XLenVT)
    return SDValue();

  SDValue Src = N->getOperand(0);

  // Don't do this for strict-fp Src.
  if (Src->isStrictFPOpcode() || Src->isTargetStrictFPOpcode())
    return SDValue();

  // Ensure the FP type is also legal.
  if (!TLI.isTypeLegal(Src.getValueType()))
    return SDValue();

  // Don't do this for f16 with Zfhmin and not Zfh.
  if (Src.getValueType() == MVT::f16 && !Subtarget.hasStdExtZfh())
    return SDValue();

  EVT SatVT = cast<VTSDNode>(N->getOperand(1))->getVT();

  RISCVFPRndMode::RoundingMode FRM = matchRoundingOp(Src.getOpcode());
  if (FRM == RISCVFPRndMode::Invalid)
    return SDValue();

  bool IsSigned = N->getOpcode() == ISD::FP_TO_SINT_SAT;

  unsigned Opc;
  if (SatVT == DstVT)
    Opc = IsSigned ? RISCVISD::FCVT_X : RISCVISD::FCVT_XU;
  else if (DstVT == MVT::i64 && SatVT == MVT::i32)
    Opc = IsSigned ? RISCVISD::FCVT_W_RV64 : RISCVISD::FCVT_WU_RV64;
  else
    return SDValue();
  // FIXME: Support other SatVTs by clamping before or after the conversion.

  Src = Src.getOperand(0);

  SDLoc DL(N);
  SDValue FpToInt = DAG.getNode(Opc, DL, XLenVT, Src,
                                DAG.getTargetConstant(FRM, DL, XLenVT));

  // fcvt.wu.* sign extends bit 31 on RV64. FP_TO_UINT_SAT expects to zero
  // extend.
  if (Opc == RISCVISD::FCVT_WU_RV64)
    FpToInt = DAG.getZeroExtendInReg(FpToInt, DL, MVT::i32);

  // RISC-V FP-to-int conversions saturate to the destination register size, but
  // don't produce 0 for nan.
  SDValue ZeroInt = DAG.getConstant(0, DL, DstVT);
  return DAG.getSelectCC(DL, Src, Src, ZeroInt, FpToInt, ISD::CondCode::SETUO);
}

// Combine (bitreverse (bswap X)) to the BREV8 GREVI encoding if the type is
// smaller than XLenVT.
static SDValue performBITREVERSECombine(SDNode *N, SelectionDAG &DAG,
                                        const RISCVSubtarget &Subtarget) {
  assert(Subtarget.hasStdExtZbkb() && "Unexpected extension");

  SDValue Src = N->getOperand(0);
  if (Src.getOpcode() != ISD::BSWAP)
    return SDValue();

  EVT VT = N->getValueType(0);
  if (!VT.isScalarInteger() || VT.getSizeInBits() >= Subtarget.getXLen() ||
      !llvm::has_single_bit<uint32_t>(VT.getSizeInBits()))
    return SDValue();

  SDLoc DL(N);
  return DAG.getNode(RISCVISD::BREV8, DL, VT, Src.getOperand(0));
}

// Convert from one FMA opcode to another based on whether we are negating the
// multiply result and/or the accumulator.
// NOTE: Only supports RVV operations with VL.
static unsigned negateFMAOpcode(unsigned Opcode, bool NegMul, bool NegAcc) {
  // Negating the multiply result changes ADD<->SUB and toggles 'N'.
  if (NegMul) {
    // clang-format off
    switch (Opcode) {
    default: llvm_unreachable("Unexpected opcode");
    case RISCVISD::VFMADD_VL:  Opcode = RISCVISD::VFNMSUB_VL; break;
    case RISCVISD::VFNMSUB_VL: Opcode = RISCVISD::VFMADD_VL;  break;
    case RISCVISD::VFNMADD_VL: Opcode = RISCVISD::VFMSUB_VL;  break;
    case RISCVISD::VFMSUB_VL:  Opcode = RISCVISD::VFNMADD_VL; break;
    case RISCVISD::STRICT_VFMADD_VL:  Opcode = RISCVISD::STRICT_VFNMSUB_VL; break;
    case RISCVISD::STRICT_VFNMSUB_VL: Opcode = RISCVISD::STRICT_VFMADD_VL;  break;
    case RISCVISD::STRICT_VFNMADD_VL: Opcode = RISCVISD::STRICT_VFMSUB_VL;  break;
    case RISCVISD::STRICT_VFMSUB_VL:  Opcode = RISCVISD::STRICT_VFNMADD_VL; break;
    }
    // clang-format on
  }

  // Negating the accumulator changes ADD<->SUB.
  if (NegAcc) {
    // clang-format off
    switch (Opcode) {
    default: llvm_unreachable("Unexpected opcode");
    case RISCVISD::VFMADD_VL:  Opcode = RISCVISD::VFMSUB_VL;  break;
    case RISCVISD::VFMSUB_VL:  Opcode = RISCVISD::VFMADD_VL;  break;
    case RISCVISD::VFNMADD_VL: Opcode = RISCVISD::VFNMSUB_VL; break;
    case RISCVISD::VFNMSUB_VL: Opcode = RISCVISD::VFNMADD_VL; break;
    case RISCVISD::STRICT_VFMADD_VL:  Opcode = RISCVISD::STRICT_VFMSUB_VL;  break;
    case RISCVISD::STRICT_VFMSUB_VL:  Opcode = RISCVISD::STRICT_VFMADD_VL;  break;
    case RISCVISD::STRICT_VFNMADD_VL: Opcode = RISCVISD::STRICT_VFNMSUB_VL; break;
    case RISCVISD::STRICT_VFNMSUB_VL: Opcode = RISCVISD::STRICT_VFNMADD_VL; break;
    }
    // clang-format on
  }

  return Opcode;
}

static SDValue combineVFMADD_VLWithVFNEG_VL(SDNode *N, SelectionDAG &DAG) {
  // Fold FNEG_VL into FMA opcodes.
  // The first operand of strict-fp is chain.
  unsigned Offset = N->isTargetStrictFPOpcode();
  SDValue A = N->getOperand(0 + Offset);
  SDValue B = N->getOperand(1 + Offset);
  SDValue C = N->getOperand(2 + Offset);
  SDValue Mask = N->getOperand(3 + Offset);
  SDValue VL = N->getOperand(4 + Offset);

  auto invertIfNegative = [&Mask, &VL](SDValue &V) {
    if (V.getOpcode() == RISCVISD::FNEG_VL && V.getOperand(1) == Mask &&
        V.getOperand(2) == VL) {
      // Return the negated input.
      V = V.getOperand(0);
      return true;
    }

    return false;
  };

  bool NegA = invertIfNegative(A);
  bool NegB = invertIfNegative(B);
  bool NegC = invertIfNegative(C);

  // If no operands are negated, we're done.
  if (!NegA && !NegB && !NegC)
    return SDValue();

  unsigned NewOpcode = negateFMAOpcode(N->getOpcode(), NegA != NegB, NegC);
  if (N->isTargetStrictFPOpcode())
    return DAG.getNode(NewOpcode, SDLoc(N), N->getVTList(),
                       {N->getOperand(0), A, B, C, Mask, VL});
  return DAG.getNode(NewOpcode, SDLoc(N), N->getValueType(0), A, B, C, Mask,
                     VL);
}

static SDValue performVFMADD_VLCombine(SDNode *N, SelectionDAG &DAG,
                                       const RISCVSubtarget &Subtarget) {
  if (SDValue V = combineVFMADD_VLWithVFNEG_VL(N, DAG))
    return V;

  if (N->getValueType(0).getVectorElementType() == MVT::f32 &&
      !Subtarget.hasVInstructionsF16())
    return SDValue();

  // FIXME: Ignore strict opcodes for now.
  if (N->isTargetStrictFPOpcode())
    return SDValue();

  // Try to form widening FMA.
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  SDValue Mask = N->getOperand(3);
  SDValue VL = N->getOperand(4);

  if (Op0.getOpcode() != RISCVISD::FP_EXTEND_VL ||
      Op1.getOpcode() != RISCVISD::FP_EXTEND_VL)
    return SDValue();

  // TODO: Refactor to handle more complex cases similar to
  // combineBinOp_VLToVWBinOp_VL.
  if ((!Op0.hasOneUse() || !Op1.hasOneUse()) &&
      (Op0 != Op1 || !Op0->hasNUsesOfValue(2, 0)))
    return SDValue();

  // Check the mask and VL are the same.
  if (Op0.getOperand(1) != Mask || Op0.getOperand(2) != VL ||
      Op1.getOperand(1) != Mask || Op1.getOperand(2) != VL)
    return SDValue();

  unsigned NewOpc;
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode");
  case RISCVISD::VFMADD_VL:
    NewOpc = RISCVISD::VFWMADD_VL;
    break;
  case RISCVISD::VFNMSUB_VL:
    NewOpc = RISCVISD::VFWNMSUB_VL;
    break;
  case RISCVISD::VFNMADD_VL:
    NewOpc = RISCVISD::VFWNMADD_VL;
    break;
  case RISCVISD::VFMSUB_VL:
    NewOpc = RISCVISD::VFWMSUB_VL;
    break;
  }

  Op0 = Op0.getOperand(0);
  Op1 = Op1.getOperand(0);

  return DAG.getNode(NewOpc, SDLoc(N), N->getValueType(0), Op0, Op1,
                     N->getOperand(2), Mask, VL);
}

static SDValue performSRACombine(SDNode *N, SelectionDAG &DAG,
                                 const RISCVSubtarget &Subtarget) {
  assert(N->getOpcode() == ISD::SRA && "Unexpected opcode");

  if (N->getValueType(0) != MVT::i64 || !Subtarget.is64Bit())
    return SDValue();

  if (!isa<ConstantSDNode>(N->getOperand(1)))
    return SDValue();
  uint64_t ShAmt = N->getConstantOperandVal(1);
  if (ShAmt > 32)
    return SDValue();

  SDValue N0 = N->getOperand(0);

  // Combine (sra (sext_inreg (shl X, C1), i32), C2) ->
  // (sra (shl X, C1+32), C2+32) so it gets selected as SLLI+SRAI instead of
  // SLLIW+SRAIW. SLLI+SRAI have compressed forms.
  if (ShAmt < 32 &&
      N0.getOpcode() == ISD::SIGN_EXTEND_INREG && N0.hasOneUse() &&
      cast<VTSDNode>(N0.getOperand(1))->getVT() == MVT::i32 &&
      N0.getOperand(0).getOpcode() == ISD::SHL && N0.getOperand(0).hasOneUse() &&
      isa<ConstantSDNode>(N0.getOperand(0).getOperand(1))) {
    uint64_t LShAmt = N0.getOperand(0).getConstantOperandVal(1);
    if (LShAmt < 32) {
      SDLoc ShlDL(N0.getOperand(0));
      SDValue Shl = DAG.getNode(ISD::SHL, ShlDL, MVT::i64,
                                N0.getOperand(0).getOperand(0),
                                DAG.getConstant(LShAmt + 32, ShlDL, MVT::i64));
      SDLoc DL(N);
      return DAG.getNode(ISD::SRA, DL, MVT::i64, Shl,
                         DAG.getConstant(ShAmt + 32, DL, MVT::i64));
    }
  }

  // Combine (sra (shl X, 32), 32 - C) -> (shl (sext_inreg X, i32), C)
  // FIXME: Should this be a generic combine? There's a similar combine on X86.
  //
  // Also try these folds where an add or sub is in the middle.
  // (sra (add (shl X, 32), C1), 32 - C) -> (shl (sext_inreg (add X, C1), C)
  // (sra (sub C1, (shl X, 32)), 32 - C) -> (shl (sext_inreg (sub C1, X), C)
  SDValue Shl;
  ConstantSDNode *AddC = nullptr;

  // We might have an ADD or SUB between the SRA and SHL.
  bool IsAdd = N0.getOpcode() == ISD::ADD;
  if ((IsAdd || N0.getOpcode() == ISD::SUB)) {
    // Other operand needs to be a constant we can modify.
    AddC = dyn_cast<ConstantSDNode>(N0.getOperand(IsAdd ? 1 : 0));
    if (!AddC)
      return SDValue();

    // AddC needs to have at least 32 trailing zeros.
    if (AddC->getAPIntValue().countr_zero() < 32)
      return SDValue();

    // All users should be a shift by constant less than or equal to 32. This
    // ensures we'll do this optimization for each of them to produce an
    // add/sub+sext_inreg they can all share.
    for (SDNode *U : N0->uses()) {
      if (U->getOpcode() != ISD::SRA ||
          !isa<ConstantSDNode>(U->getOperand(1)) ||
          U->getConstantOperandVal(1) > 32)
        return SDValue();
    }

    Shl = N0.getOperand(IsAdd ? 0 : 1);
  } else {
    // Not an ADD or SUB.
    Shl = N0;
  }

  // Look for a shift left by 32.
  if (Shl.getOpcode() != ISD::SHL || !isa<ConstantSDNode>(Shl.getOperand(1)) ||
      Shl.getConstantOperandVal(1) != 32)
    return SDValue();

  // We if we didn't look through an add/sub, then the shl should have one use.
  // If we did look through an add/sub, the sext_inreg we create is free so
  // we're only creating 2 new instructions. It's enough to only remove the
  // original sra+add/sub.
  if (!AddC && !Shl.hasOneUse())
    return SDValue();

  SDLoc DL(N);
  SDValue In = Shl.getOperand(0);

  // If we looked through an ADD or SUB, we need to rebuild it with the shifted
  // constant.
  if (AddC) {
    SDValue ShiftedAddC =
        DAG.getConstant(AddC->getAPIntValue().lshr(32), DL, MVT::i64);
    if (IsAdd)
      In = DAG.getNode(ISD::ADD, DL, MVT::i64, In, ShiftedAddC);
    else
      In = DAG.getNode(ISD::SUB, DL, MVT::i64, ShiftedAddC, In);
  }

  SDValue SExt = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i64, In,
                             DAG.getValueType(MVT::i32));
  if (ShAmt == 32)
    return SExt;

  return DAG.getNode(
      ISD::SHL, DL, MVT::i64, SExt,
      DAG.getConstant(32 - ShAmt, DL, MVT::i64));
}

// Invert (and/or (set cc X, Y), (xor Z, 1)) to (or/and (set !cc X, Y)), Z) if
// the result is used as the conditon of a br_cc or select_cc we can invert,
// inverting the setcc is free, and Z is 0/1. Caller will invert the
// br_cc/select_cc.
static SDValue tryDemorganOfBooleanCondition(SDValue Cond, SelectionDAG &DAG) {
  bool IsAnd = Cond.getOpcode() == ISD::AND;
  if (!IsAnd && Cond.getOpcode() != ISD::OR)
    return SDValue();

  if (!Cond.hasOneUse())
    return SDValue();

  SDValue Setcc = Cond.getOperand(0);
  SDValue Xor = Cond.getOperand(1);
  // Canonicalize setcc to LHS.
  if (Setcc.getOpcode() != ISD::SETCC)
    std::swap(Setcc, Xor);
  // LHS should be a setcc and RHS should be an xor.
  if (Setcc.getOpcode() != ISD::SETCC || !Setcc.hasOneUse() ||
      Xor.getOpcode() != ISD::XOR || !Xor.hasOneUse())
    return SDValue();

  // If the condition is an And, SimplifyDemandedBits may have changed
  // (xor Z, 1) to (not Z).
  SDValue Xor1 = Xor.getOperand(1);
  if (!isOneConstant(Xor1) && !(IsAnd && isAllOnesConstant(Xor1)))
    return SDValue();

  EVT VT = Cond.getValueType();
  SDValue Xor0 = Xor.getOperand(0);

  // The LHS of the xor needs to be 0/1.
  APInt Mask = APInt::getBitsSetFrom(VT.getSizeInBits(), 1);
  if (!DAG.MaskedValueIsZero(Xor0, Mask))
    return SDValue();

  // We can only invert integer setccs.
  EVT SetCCOpVT = Setcc.getOperand(0).getValueType();
  if (!SetCCOpVT.isScalarInteger())
    return SDValue();

  ISD::CondCode CCVal = cast<CondCodeSDNode>(Setcc.getOperand(2))->get();
  if (ISD::isIntEqualitySetCC(CCVal)) {
    CCVal = ISD::getSetCCInverse(CCVal, SetCCOpVT);
    Setcc = DAG.getSetCC(SDLoc(Setcc), VT, Setcc.getOperand(0),
                         Setcc.getOperand(1), CCVal);
  } else if (CCVal == ISD::SETLT && isNullConstant(Setcc.getOperand(0))) {
    // Invert (setlt 0, X) by converting to (setlt X, 1).
    Setcc = DAG.getSetCC(SDLoc(Setcc), VT, Setcc.getOperand(1),
                         DAG.getConstant(1, SDLoc(Setcc), VT), CCVal);
  } else if (CCVal == ISD::SETLT && isOneConstant(Setcc.getOperand(1))) {
    // (setlt X, 1) by converting to (setlt 0, X).
    Setcc = DAG.getSetCC(SDLoc(Setcc), VT,
                         DAG.getConstant(0, SDLoc(Setcc), VT),
                         Setcc.getOperand(0), CCVal);
  } else
    return SDValue();

  unsigned Opc = IsAnd ? ISD::OR : ISD::AND;
  return DAG.getNode(Opc, SDLoc(Cond), VT, Setcc, Xor.getOperand(0));
}

// Perform common combines for BR_CC and SELECT_CC condtions.
static bool combine_CC(SDValue &LHS, SDValue &RHS, SDValue &CC, const SDLoc &DL,
                       SelectionDAG &DAG, const RISCVSubtarget &Subtarget) {
  ISD::CondCode CCVal = cast<CondCodeSDNode>(CC)->get();

  // As far as arithmetic right shift always saves the sign,
  // shift can be omitted.
  // Fold setlt (sra X, N), 0 -> setlt X, 0 and
  // setge (sra X, N), 0 -> setge X, 0
  if (isNullConstant(RHS) && (CCVal == ISD::SETGE || CCVal == ISD::SETLT) &&
      LHS.getOpcode() == ISD::SRA) {
    LHS = LHS.getOperand(0);
    return true;
  }

  if (!ISD::isIntEqualitySetCC(CCVal))
    return false;

  // Fold ((setlt X, Y), 0, ne) -> (X, Y, lt)
  // Sometimes the setcc is introduced after br_cc/select_cc has been formed.
  if (LHS.getOpcode() == ISD::SETCC && isNullConstant(RHS) &&
      LHS.getOperand(0).getValueType() == Subtarget.getXLenVT()) {
    // If we're looking for eq 0 instead of ne 0, we need to invert the
    // condition.
    bool Invert = CCVal == ISD::SETEQ;
    CCVal = cast<CondCodeSDNode>(LHS.getOperand(2))->get();
    if (Invert)
      CCVal = ISD::getSetCCInverse(CCVal, LHS.getValueType());

    RHS = LHS.getOperand(1);
    LHS = LHS.getOperand(0);
    translateSetCCForBranch(DL, LHS, RHS, CCVal, DAG);

    CC = DAG.getCondCode(CCVal);
    return true;
  }

  // Fold ((xor X, Y), 0, eq/ne) -> (X, Y, eq/ne)
  if (LHS.getOpcode() == ISD::XOR && isNullConstant(RHS)) {
    RHS = LHS.getOperand(1);
    LHS = LHS.getOperand(0);
    return true;
  }

  // Fold ((srl (and X, 1<<C), C), 0, eq/ne) -> ((shl X, XLen-1-C), 0, ge/lt)
  if (isNullConstant(RHS) && LHS.getOpcode() == ISD::SRL && LHS.hasOneUse() &&
      LHS.getOperand(1).getOpcode() == ISD::Constant) {
    SDValue LHS0 = LHS.getOperand(0);
    if (LHS0.getOpcode() == ISD::AND &&
        LHS0.getOperand(1).getOpcode() == ISD::Constant) {
      uint64_t Mask = LHS0.getConstantOperandVal(1);
      uint64_t ShAmt = LHS.getConstantOperandVal(1);
      if (isPowerOf2_64(Mask) && Log2_64(Mask) == ShAmt) {
        CCVal = CCVal == ISD::SETEQ ? ISD::SETGE : ISD::SETLT;
        CC = DAG.getCondCode(CCVal);

        ShAmt = LHS.getValueSizeInBits() - 1 - ShAmt;
        LHS = LHS0.getOperand(0);
        if (ShAmt != 0)
          LHS =
              DAG.getNode(ISD::SHL, DL, LHS.getValueType(), LHS0.getOperand(0),
                          DAG.getConstant(ShAmt, DL, LHS.getValueType()));
        return true;
      }
    }
  }

  // (X, 1, setne) -> // (X, 0, seteq) if we can prove X is 0/1.
  // This can occur when legalizing some floating point comparisons.
  APInt Mask = APInt::getBitsSetFrom(LHS.getValueSizeInBits(), 1);
  if (isOneConstant(RHS) && DAG.MaskedValueIsZero(LHS, Mask)) {
    CCVal = ISD::getSetCCInverse(CCVal, LHS.getValueType());
    CC = DAG.getCondCode(CCVal);
    RHS = DAG.getConstant(0, DL, LHS.getValueType());
    return true;
  }

  if (isNullConstant(RHS)) {
    if (SDValue NewCond = tryDemorganOfBooleanCondition(LHS, DAG)) {
      CCVal = ISD::getSetCCInverse(CCVal, LHS.getValueType());
      CC = DAG.getCondCode(CCVal);
      LHS = NewCond;
      return true;
    }
  }

  return false;
}

// Fold
// (select C, (add Y, X), Y) -> (add Y, (select C, X, 0)).
// (select C, (sub Y, X), Y) -> (sub Y, (select C, X, 0)).
// (select C, (or Y, X), Y)  -> (or Y, (select C, X, 0)).
// (select C, (xor Y, X), Y) -> (xor Y, (select C, X, 0)).
static SDValue tryFoldSelectIntoOp(SDNode *N, SelectionDAG &DAG,
                                   SDValue TrueVal, SDValue FalseVal,
                                   bool Swapped) {
  bool Commutative = true;
  unsigned Opc = TrueVal.getOpcode();
  switch (Opc) {
  default:
    return SDValue();
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::SUB:
    Commutative = false;
    break;
  case ISD::ADD:
  case ISD::OR:
  case ISD::XOR:
    break;
  }

  if (!TrueVal.hasOneUse() || isa<ConstantSDNode>(FalseVal))
    return SDValue();

  unsigned OpToFold;
  if (FalseVal == TrueVal.getOperand(0))
    OpToFold = 0;
  else if (Commutative && FalseVal == TrueVal.getOperand(1))
    OpToFold = 1;
  else
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  SDValue OtherOp = TrueVal.getOperand(1 - OpToFold);
  EVT OtherOpVT = OtherOp.getValueType();
  SDValue IdentityOperand =
      DAG.getNeutralElement(Opc, DL, OtherOpVT, N->getFlags());
  if (!Commutative)
    IdentityOperand = DAG.getConstant(0, DL, OtherOpVT);
  assert(IdentityOperand && "No identity operand!");

  if (Swapped)
    std::swap(OtherOp, IdentityOperand);
  SDValue NewSel =
      DAG.getSelect(DL, OtherOpVT, N->getOperand(0), OtherOp, IdentityOperand);
  return DAG.getNode(TrueVal.getOpcode(), DL, VT, FalseVal, NewSel);
}

// This tries to get rid of `select` and `icmp` that are being used to handle
// `Targets` that do not support `cttz(0)`/`ctlz(0)`.
static SDValue foldSelectOfCTTZOrCTLZ(SDNode *N, SelectionDAG &DAG) {
  SDValue Cond = N->getOperand(0);

  // This represents either CTTZ or CTLZ instruction.
  SDValue CountZeroes;

  SDValue ValOnZero;

  if (Cond.getOpcode() != ISD::SETCC)
    return SDValue();

  if (!isNullConstant(Cond->getOperand(1)))
    return SDValue();

  ISD::CondCode CCVal = cast<CondCodeSDNode>(Cond->getOperand(2))->get();
  if (CCVal == ISD::CondCode::SETEQ) {
    CountZeroes = N->getOperand(2);
    ValOnZero = N->getOperand(1);
  } else if (CCVal == ISD::CondCode::SETNE) {
    CountZeroes = N->getOperand(1);
    ValOnZero = N->getOperand(2);
  } else {
    return SDValue();
  }

  if (CountZeroes.getOpcode() == ISD::TRUNCATE ||
      CountZeroes.getOpcode() == ISD::ZERO_EXTEND)
    CountZeroes = CountZeroes.getOperand(0);

  if (CountZeroes.getOpcode() != ISD::CTTZ &&
      CountZeroes.getOpcode() != ISD::CTTZ_ZERO_UNDEF &&
      CountZeroes.getOpcode() != ISD::CTLZ &&
      CountZeroes.getOpcode() != ISD::CTLZ_ZERO_UNDEF)
    return SDValue();

  if (!isNullConstant(ValOnZero))
    return SDValue();

  SDValue CountZeroesArgument = CountZeroes->getOperand(0);
  if (Cond->getOperand(0) != CountZeroesArgument)
    return SDValue();

  if (CountZeroes.getOpcode() == ISD::CTTZ_ZERO_UNDEF) {
    CountZeroes = DAG.getNode(ISD::CTTZ, SDLoc(CountZeroes),
                              CountZeroes.getValueType(), CountZeroesArgument);
  } else if (CountZeroes.getOpcode() == ISD::CTLZ_ZERO_UNDEF) {
    CountZeroes = DAG.getNode(ISD::CTLZ, SDLoc(CountZeroes),
                              CountZeroes.getValueType(), CountZeroesArgument);
  }

  unsigned BitWidth = CountZeroes.getValueSizeInBits();
  SDValue BitWidthMinusOne =
      DAG.getConstant(BitWidth - 1, SDLoc(N), CountZeroes.getValueType());

  auto AndNode = DAG.getNode(ISD::AND, SDLoc(N), CountZeroes.getValueType(),
                             CountZeroes, BitWidthMinusOne);
  return DAG.getZExtOrTrunc(AndNode, SDLoc(N), N->getValueType(0));
}

static SDValue useInversedSetcc(SDNode *N, SelectionDAG &DAG,
                                const RISCVSubtarget &Subtarget) {
  SDValue Cond = N->getOperand(0);
  SDValue True = N->getOperand(1);
  SDValue False = N->getOperand(2);
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  EVT CondVT = Cond.getValueType();

  if (Cond.getOpcode() != ISD::SETCC || !Cond.hasOneUse())
    return SDValue();

  // Replace (setcc eq (and x, C)) with (setcc ne (and x, C))) to generate
  // BEXTI, where C is power of 2.
  if (Subtarget.hasStdExtZbs() && VT.isScalarInteger() &&
      (Subtarget.hasStdExtZicond() || Subtarget.hasVendorXVentanaCondOps())) {
    SDValue LHS = Cond.getOperand(0);
    SDValue RHS = Cond.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
    if (CC == ISD::SETEQ && LHS.getOpcode() == ISD::AND &&
        isa<ConstantSDNode>(LHS.getOperand(1)) && isNullConstant(RHS)) {
      const APInt &MaskVal = LHS.getConstantOperandAPInt(1);
      if (MaskVal.isPowerOf2() && !MaskVal.isSignedIntN(12))
        return DAG.getSelect(DL, VT,
                             DAG.getSetCC(DL, CondVT, LHS, RHS, ISD::SETNE),
                             False, True);
    }
  }
  return SDValue();
}

static SDValue performSELECTCombine(SDNode *N, SelectionDAG &DAG,
                                    const RISCVSubtarget &Subtarget) {
  if (SDValue Folded = foldSelectOfCTTZOrCTLZ(N, DAG))
    return Folded;

  if (SDValue V = useInversedSetcc(N, DAG, Subtarget))
    return V;

  if (Subtarget.hasConditionalMoveFusion())
    return SDValue();

  SDValue TrueVal = N->getOperand(1);
  SDValue FalseVal = N->getOperand(2);
  if (SDValue V = tryFoldSelectIntoOp(N, DAG, TrueVal, FalseVal, /*Swapped*/false))
    return V;
  return tryFoldSelectIntoOp(N, DAG, FalseVal, TrueVal, /*Swapped*/true);
}

/// If we have a build_vector where each lane is binop X, C, where C
/// is a constant (but not necessarily the same constant on all lanes),
/// form binop (build_vector x1, x2, ...), (build_vector c1, c2, c3, ..).
/// We assume that materializing a constant build vector will be no more
/// expensive that performing O(n) binops.
static SDValue performBUILD_VECTORCombine(SDNode *N, SelectionDAG &DAG,
                                          const RISCVSubtarget &Subtarget,
                                          const RISCVTargetLowering &TLI) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  assert(!VT.isScalableVector() && "unexpected build vector");

  if (VT.getVectorNumElements() == 1)
    return SDValue();

  const unsigned Opcode = N->op_begin()->getNode()->getOpcode();
  if (!TLI.isBinOp(Opcode))
    return SDValue();

  if (!TLI.isOperationLegalOrCustom(Opcode, VT) || !TLI.isTypeLegal(VT))
    return SDValue();

  // This BUILD_VECTOR involves an implicit truncation, and sinking
  // truncates through binops is non-trivial.
  if (N->op_begin()->getValueType() != VT.getVectorElementType())
    return SDValue();

  SmallVector<SDValue> LHSOps;
  SmallVector<SDValue> RHSOps;
  for (SDValue Op : N->ops()) {
    if (Op.isUndef()) {
      // We can't form a divide or remainder from undef.
      if (!DAG.isSafeToSpeculativelyExecute(Opcode))
        return SDValue();

      LHSOps.push_back(Op);
      RHSOps.push_back(Op);
      continue;
    }

    // TODO: We can handle operations which have an neutral rhs value
    // (e.g. x + 0, a * 1 or a << 0), but we then have to keep track
    // of profit in a more explicit manner.
    if (Op.getOpcode() != Opcode || !Op.hasOneUse())
      return SDValue();

    LHSOps.push_back(Op.getOperand(0));
    if (!isa<ConstantSDNode>(Op.getOperand(1)) &&
        !isa<ConstantFPSDNode>(Op.getOperand(1)))
      return SDValue();
    // FIXME: Return failure if the RHS type doesn't match the LHS. Shifts may
    // have different LHS and RHS types.
    if (Op.getOperand(0).getValueType() != Op.getOperand(1).getValueType())
      return SDValue();

    RHSOps.push_back(Op.getOperand(1));
  }

  return DAG.getNode(Opcode, DL, VT, DAG.getBuildVector(VT, DL, LHSOps),
                     DAG.getBuildVector(VT, DL, RHSOps));
}

static SDValue performINSERT_VECTOR_ELTCombine(SDNode *N, SelectionDAG &DAG,
                                               const RISCVSubtarget &Subtarget,
                                               const RISCVTargetLowering &TLI) {
  SDValue InVec = N->getOperand(0);
  SDValue InVal = N->getOperand(1);
  SDValue EltNo = N->getOperand(2);
  SDLoc DL(N);

  EVT VT = InVec.getValueType();
  if (VT.isScalableVector())
    return SDValue();

  if (!InVec.hasOneUse())
    return SDValue();

  // Given insert_vector_elt (binop a, VecC), (same_binop b, C2), Elt
  // move the insert_vector_elts into the arms of the binop.  Note that
  // the new RHS must be a constant.
  const unsigned InVecOpcode = InVec->getOpcode();
  if (InVecOpcode == InVal->getOpcode() && TLI.isBinOp(InVecOpcode) &&
      InVal.hasOneUse()) {
    SDValue InVecLHS = InVec->getOperand(0);
    SDValue InVecRHS = InVec->getOperand(1);
    SDValue InValLHS = InVal->getOperand(0);
    SDValue InValRHS = InVal->getOperand(1);

    if (!ISD::isBuildVectorOfConstantSDNodes(InVecRHS.getNode()))
      return SDValue();
    if (!isa<ConstantSDNode>(InValRHS) && !isa<ConstantFPSDNode>(InValRHS))
      return SDValue();
    // FIXME: Return failure if the RHS type doesn't match the LHS. Shifts may
    // have different LHS and RHS types.
    if (InVec.getOperand(0).getValueType() != InVec.getOperand(1).getValueType())
      return SDValue();
    SDValue LHS = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, VT,
                              InVecLHS, InValLHS, EltNo);
    SDValue RHS = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, VT,
                              InVecRHS, InValRHS, EltNo);
    return DAG.getNode(InVecOpcode, DL, VT, LHS, RHS);
  }

  // Given insert_vector_elt (concat_vectors ...), InVal, Elt
  // move the insert_vector_elt to the source operand of the concat_vector.
  if (InVec.getOpcode() != ISD::CONCAT_VECTORS)
    return SDValue();

  auto *IndexC = dyn_cast<ConstantSDNode>(EltNo);
  if (!IndexC)
    return SDValue();
  unsigned Elt = IndexC->getZExtValue();

  EVT ConcatVT = InVec.getOperand(0).getValueType();
  if (ConcatVT.getVectorElementType() != InVal.getValueType())
    return SDValue();
  unsigned ConcatNumElts = ConcatVT.getVectorNumElements();
  SDValue NewIdx = DAG.getVectorIdxConstant(Elt % ConcatNumElts, DL);

  unsigned ConcatOpIdx = Elt / ConcatNumElts;
  SDValue ConcatOp = InVec.getOperand(ConcatOpIdx);
  ConcatOp = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, ConcatVT,
                         ConcatOp, InVal, NewIdx);

  SmallVector<SDValue> ConcatOps;
  ConcatOps.append(InVec->op_begin(), InVec->op_end());
  ConcatOps[ConcatOpIdx] = ConcatOp;
  return DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, ConcatOps);
}

// If we're concatenating a series of vector loads like
// concat_vectors (load v4i8, p+0), (load v4i8, p+n), (load v4i8, p+n*2) ...
// Then we can turn this into a strided load by widening the vector elements
// vlse32 p, stride=n
static SDValue performCONCAT_VECTORSCombine(SDNode *N, SelectionDAG &DAG,
                                            const RISCVSubtarget &Subtarget,
                                            const RISCVTargetLowering &TLI) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  // Only perform this combine on legal MVTs.
  if (!TLI.isTypeLegal(VT))
    return SDValue();

  // TODO: Potentially extend this to scalable vectors
  if (VT.isScalableVector())
    return SDValue();

  auto *BaseLd = dyn_cast<LoadSDNode>(N->getOperand(0));
  if (!BaseLd || !BaseLd->isSimple() || !ISD::isNormalLoad(BaseLd) ||
      !SDValue(BaseLd, 0).hasOneUse())
    return SDValue();

  EVT BaseLdVT = BaseLd->getValueType(0);

  // Go through the loads and check that they're strided
  SmallVector<LoadSDNode *> Lds;
  Lds.push_back(BaseLd);
  Align Align = BaseLd->getAlign();
  for (SDValue Op : N->ops().drop_front()) {
    auto *Ld = dyn_cast<LoadSDNode>(Op);
    if (!Ld || !Ld->isSimple() || !Op.hasOneUse() ||
        Ld->getChain() != BaseLd->getChain() || !ISD::isNormalLoad(Ld) ||
        Ld->getValueType(0) != BaseLdVT)
      return SDValue();

    Lds.push_back(Ld);

    // The common alignment is the most restrictive (smallest) of all the loads
    Align = std::min(Align, Ld->getAlign());
  }

  using PtrDiff = std::pair<std::variant<int64_t, SDValue>, bool>;
  auto GetPtrDiff = [&DAG](LoadSDNode *Ld1,
                           LoadSDNode *Ld2) -> std::optional<PtrDiff> {
    // If the load ptrs can be decomposed into a common (Base + Index) with a
    // common constant stride, then return the constant stride.
    BaseIndexOffset BIO1 = BaseIndexOffset::match(Ld1, DAG);
    BaseIndexOffset BIO2 = BaseIndexOffset::match(Ld2, DAG);
    if (BIO1.equalBaseIndex(BIO2, DAG))
      return {{BIO2.getOffset() - BIO1.getOffset(), false}};

    // Otherwise try to match (add LastPtr, Stride) or (add NextPtr, Stride)
    SDValue P1 = Ld1->getBasePtr();
    SDValue P2 = Ld2->getBasePtr();
    if (P2.getOpcode() == ISD::ADD && P2.getOperand(0) == P1)
      return {{P2.getOperand(1), false}};
    if (P1.getOpcode() == ISD::ADD && P1.getOperand(0) == P2)
      return {{P1.getOperand(1), true}};

    return std::nullopt;
  };

  // Get the distance between the first and second loads
  auto BaseDiff = GetPtrDiff(Lds[0], Lds[1]);
  if (!BaseDiff)
    return SDValue();

  // Check all the loads are the same distance apart
  for (auto *It = Lds.begin() + 1; It != Lds.end() - 1; It++)
    if (GetPtrDiff(*It, *std::next(It)) != BaseDiff)
      return SDValue();

  // TODO: At this point, we've successfully matched a generalized gather
  // load.  Maybe we should emit that, and then move the specialized
  // matchers above and below into a DAG combine?

  // Get the widened scalar type, e.g. v4i8 -> i64
  unsigned WideScalarBitWidth =
      BaseLdVT.getScalarSizeInBits() * BaseLdVT.getVectorNumElements();
  MVT WideScalarVT = MVT::getIntegerVT(WideScalarBitWidth);

  // Get the vector type for the strided load, e.g. 4 x v4i8 -> v4i64
  MVT WideVecVT = MVT::getVectorVT(WideScalarVT, N->getNumOperands());
  if (!TLI.isTypeLegal(WideVecVT))
    return SDValue();

  // Check that the operation is legal
  if (!TLI.isLegalStridedLoadStore(WideVecVT, Align))
    return SDValue();

  auto [StrideVariant, MustNegateStride] = *BaseDiff;
  SDValue Stride = std::holds_alternative<SDValue>(StrideVariant)
                       ? std::get<SDValue>(StrideVariant)
                       : DAG.getConstant(std::get<int64_t>(StrideVariant), DL,
                                         Lds[0]->getOffset().getValueType());
  if (MustNegateStride)
    Stride = DAG.getNegative(Stride, DL, Stride.getValueType());

  SDValue AllOneMask =
    DAG.getSplat(WideVecVT.changeVectorElementType(MVT::i1), DL,
                 DAG.getConstant(1, DL, MVT::i1));

  uint64_t MemSize;
  if (auto *ConstStride = dyn_cast<ConstantSDNode>(Stride);
      ConstStride && ConstStride->getSExtValue() >= 0)
    // total size = (elsize * n) + (stride - elsize) * (n-1)
    //            = elsize + stride * (n-1)
    MemSize = WideScalarVT.getSizeInBits() +
              ConstStride->getSExtValue() * (N->getNumOperands() - 1);
  else
    // If Stride isn't constant, then we can't know how much it will load
    MemSize = MemoryLocation::UnknownSize;

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      BaseLd->getPointerInfo(), BaseLd->getMemOperand()->getFlags(), MemSize,
      Align);

  SDValue StridedLoad = DAG.getStridedLoadVP(
      WideVecVT, DL, BaseLd->getChain(), BaseLd->getBasePtr(), Stride,
      AllOneMask,
      DAG.getConstant(N->getNumOperands(), DL, Subtarget.getXLenVT()), MMO);

  for (SDValue Ld : N->ops())
    DAG.makeEquivalentMemoryOrdering(cast<LoadSDNode>(Ld), StridedLoad);

  return DAG.getBitcast(VT.getSimpleVT(), StridedLoad);
}

static SDValue combineToVWMACC(SDNode *N, SelectionDAG &DAG,
                               const RISCVSubtarget &Subtarget) {

  assert(N->getOpcode() == RISCVISD::ADD_VL || N->getOpcode() == ISD::ADD);

  if (N->getValueType(0).isFixedLengthVector())
    return SDValue();

  SDValue Addend = N->getOperand(0);
  SDValue MulOp = N->getOperand(1);

  if (N->getOpcode() == RISCVISD::ADD_VL) {
    SDValue AddMergeOp = N->getOperand(2);
    if (!AddMergeOp.isUndef())
      return SDValue();
  }

  auto IsVWMulOpc = [](unsigned Opc) {
    switch (Opc) {
    case RISCVISD::VWMUL_VL:
    case RISCVISD::VWMULU_VL:
    case RISCVISD::VWMULSU_VL:
      return true;
    default:
      return false;
    }
  };

  if (!IsVWMulOpc(MulOp.getOpcode()))
    std::swap(Addend, MulOp);

  if (!IsVWMulOpc(MulOp.getOpcode()))
    return SDValue();

  SDValue MulMergeOp = MulOp.getOperand(2);

  if (!MulMergeOp.isUndef())
    return SDValue();

  auto [AddMask, AddVL] = [](SDNode *N, SelectionDAG &DAG,
                             const RISCVSubtarget &Subtarget) {
    if (N->getOpcode() == ISD::ADD) {
      SDLoc DL(N);
      return getDefaultScalableVLOps(N->getSimpleValueType(0), DL, DAG,
                                     Subtarget);
    }
    return std::make_pair(N->getOperand(3), N->getOperand(4));
  }(N, DAG, Subtarget);

  SDValue MulMask = MulOp.getOperand(3);
  SDValue MulVL = MulOp.getOperand(4);

  if (AddMask != MulMask || AddVL != MulVL)
    return SDValue();

  unsigned Opc = RISCVISD::VWMACC_VL + MulOp.getOpcode() - RISCVISD::VWMUL_VL;
  static_assert(RISCVISD::VWMACC_VL + 1 == RISCVISD::VWMACCU_VL,
                "Unexpected opcode after VWMACC_VL");
  static_assert(RISCVISD::VWMACC_VL + 2 == RISCVISD::VWMACCSU_VL,
                "Unexpected opcode after VWMACC_VL!");
  static_assert(RISCVISD::VWMUL_VL + 1 == RISCVISD::VWMULU_VL,
                "Unexpected opcode after VWMUL_VL!");
  static_assert(RISCVISD::VWMUL_VL + 2 == RISCVISD::VWMULSU_VL,
                "Unexpected opcode after VWMUL_VL!");

  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  SDValue Ops[] = {MulOp.getOperand(0), MulOp.getOperand(1), Addend, AddMask,
                   AddVL};
  return DAG.getNode(Opc, DL, VT, Ops);
}

static bool legalizeScatterGatherIndexType(SDLoc DL, SDValue &Index,
                                           ISD::MemIndexType &IndexType,
                                           RISCVTargetLowering::DAGCombinerInfo &DCI) {
  if (!DCI.isBeforeLegalize())
    return false;

  SelectionDAG &DAG = DCI.DAG;
  const MVT XLenVT =
    DAG.getMachineFunction().getSubtarget<RISCVSubtarget>().getXLenVT();

  const EVT IndexVT = Index.getValueType();

  // RISC-V indexed loads only support the "unsigned unscaled" addressing
  // mode, so anything else must be manually legalized.
  if (!isIndexTypeSigned(IndexType))
    return false;

  if (IndexVT.getVectorElementType().bitsLT(XLenVT)) {
    // Any index legalization should first promote to XLenVT, so we don't lose
    // bits when scaling. This may create an illegal index type so we let
    // LLVM's legalization take care of the splitting.
    // FIXME: LLVM can't split VP_GATHER or VP_SCATTER yet.
    Index = DAG.getNode(ISD::SIGN_EXTEND, DL,
                        IndexVT.changeVectorElementType(XLenVT), Index);
  }
  IndexType = ISD::UNSIGNED_SCALED;
  return true;
}

/// Match the index vector of a scatter or gather node as the shuffle mask
/// which performs the rearrangement if possible.  Will only match if
/// all lanes are touched, and thus replacing the scatter or gather with
/// a unit strided access and shuffle is legal.
static bool matchIndexAsShuffle(EVT VT, SDValue Index, SDValue Mask,
                                SmallVector<int> &ShuffleMask) {
  if (!ISD::isConstantSplatVectorAllOnes(Mask.getNode()))
    return false;
  if (!ISD::isBuildVectorOfConstantSDNodes(Index.getNode()))
    return false;

  const unsigned ElementSize = VT.getScalarStoreSize();
  const unsigned NumElems = VT.getVectorNumElements();

  // Create the shuffle mask and check all bits active
  assert(ShuffleMask.empty());
  BitVector ActiveLanes(NumElems);
  for (unsigned i = 0; i < Index->getNumOperands(); i++) {
    // TODO: We've found an active bit of UB, and could be
    // more aggressive here if desired.
    if (Index->getOperand(i)->isUndef())
      return false;
    uint64_t C = Index->getConstantOperandVal(i);
    if (C % ElementSize != 0)
      return false;
    C = C / ElementSize;
    if (C >= NumElems)
      return false;
    ShuffleMask.push_back(C);
    ActiveLanes.set(C);
  }
  return ActiveLanes.all();
}

/// Match the index of a gather or scatter operation as an operation
/// with twice the element width and half the number of elements.  This is
/// generally profitable (if legal) because these operations are linear
/// in VL, so even if we cause some extract VTYPE/VL toggles, we still
/// come out ahead.
static bool matchIndexAsWiderOp(EVT VT, SDValue Index, SDValue Mask,
                                Align BaseAlign, const RISCVSubtarget &ST) {
  if (!ISD::isConstantSplatVectorAllOnes(Mask.getNode()))
    return false;
  if (!ISD::isBuildVectorOfConstantSDNodes(Index.getNode()))
    return false;

  // Attempt a doubling.  If we can use a element type 4x or 8x in
  // size, this will happen via multiply iterations of the transform.
  const unsigned NumElems = VT.getVectorNumElements();
  if (NumElems % 2 != 0)
    return false;

  const unsigned ElementSize = VT.getScalarStoreSize();
  const unsigned WiderElementSize = ElementSize * 2;
  if (WiderElementSize > ST.getELen()/8)
    return false;

  if (!ST.enableUnalignedVectorMem() && BaseAlign < WiderElementSize)
    return false;

  for (unsigned i = 0; i < Index->getNumOperands(); i++) {
    // TODO: We've found an active bit of UB, and could be
    // more aggressive here if desired.
    if (Index->getOperand(i)->isUndef())
      return false;
    // TODO: This offset check is too strict if we support fully
    // misaligned memory operations.
    uint64_t C = Index->getConstantOperandVal(i);
    if (i % 2 == 0) {
      if (C % WiderElementSize != 0)
        return false;
      continue;
    }
    uint64_t Last = Index->getConstantOperandVal(i-1);
    if (C != Last + ElementSize)
      return false;
  }
  return true;
}

// trunc (sra sext (X), zext (Y)) -> sra (X, smin (Y, scalarsize(Y) - 1))
// This would be benefit for the cases where X and Y are both the same value
// type of low precision vectors. Since the truncate would be lowered into
// n-levels TRUNCATE_VECTOR_VL to satisfy RVV's SEW*2->SEW truncate
// restriction, such pattern would be expanded into a series of "vsetvli"
// and "vnsrl" instructions later to reach this point.
static SDValue combineTruncOfSraSext(SDNode *N, SelectionDAG &DAG) {
  SDValue Mask = N->getOperand(1);
  SDValue VL = N->getOperand(2);

  bool IsVLMAX = isAllOnesConstant(VL) ||
                 (isa<RegisterSDNode>(VL) &&
                  cast<RegisterSDNode>(VL)->getReg() == RISCV::X0);
  if (!IsVLMAX || Mask.getOpcode() != RISCVISD::VMSET_VL ||
      Mask.getOperand(0) != VL)
    return SDValue();

  auto IsTruncNode = [&](SDValue V) {
    return V.getOpcode() == RISCVISD::TRUNCATE_VECTOR_VL &&
           V.getOperand(1) == Mask && V.getOperand(2) == VL;
  };

  SDValue Op = N->getOperand(0);

  // We need to first find the inner level of TRUNCATE_VECTOR_VL node
  // to distinguish such pattern.
  while (IsTruncNode(Op)) {
    if (!Op.hasOneUse())
      return SDValue();
    Op = Op.getOperand(0);
  }

  if (Op.getOpcode() != ISD::SRA || !Op.hasOneUse())
    return SDValue();

  SDValue N0 = Op.getOperand(0);
  SDValue N1 = Op.getOperand(1);
  if (N0.getOpcode() != ISD::SIGN_EXTEND || !N0.hasOneUse() ||
      N1.getOpcode() != ISD::ZERO_EXTEND || !N1.hasOneUse())
    return SDValue();

  SDValue N00 = N0.getOperand(0);
  SDValue N10 = N1.getOperand(0);
  if (!N00.getValueType().isVector() ||
      N00.getValueType() != N10.getValueType() ||
      N->getValueType(0) != N10.getValueType())
    return SDValue();

  unsigned MaxShAmt = N10.getValueType().getScalarSizeInBits() - 1;
  SDValue SMin =
      DAG.getNode(ISD::SMIN, SDLoc(N1), N->getValueType(0), N10,
                  DAG.getConstant(MaxShAmt, SDLoc(N1), N->getValueType(0)));
  return DAG.getNode(ISD::SRA, SDLoc(N), N->getValueType(0), N00, SMin);
}

// Combine (truncate_vector_vl (umin X, C)) -> (vnclipu_vl X) if C is the
// maximum value for the truncated type.
// Combine (truncate_vector_vl (smin (smax X, C2), C1)) -> (vnclip_vl X) if C1
// is the signed maximum value for the truncated type and C2 is the signed
// minimum value.
static SDValue combineTruncToVnclip(SDNode *N, SelectionDAG &DAG,
                                    const RISCVSubtarget &Subtarget) {
  assert(N->getOpcode() == RISCVISD::TRUNCATE_VECTOR_VL);

  MVT VT = N->getSimpleValueType(0);

  SDValue Mask = N->getOperand(1);
  SDValue VL = N->getOperand(2);

  auto MatchMinMax = [&VL, &Mask](SDValue V, unsigned Opc, unsigned OpcVL,
                                  APInt &SplatVal) {
    if (V.getOpcode() != Opc &&
        !(V.getOpcode() == OpcVL && V.getOperand(2).isUndef() &&
          V.getOperand(3) == Mask && V.getOperand(4) == VL))
      return SDValue();

    SDValue Op = V.getOperand(1);

    // Peek through conversion between fixed and scalable vectors.
    if (Op.getOpcode() == ISD::INSERT_SUBVECTOR && Op.getOperand(0).isUndef() &&
        isNullConstant(Op.getOperand(2)) &&
        Op.getOperand(1).getValueType().isFixedLengthVector() &&
        Op.getOperand(1).getOpcode() == ISD::EXTRACT_SUBVECTOR &&
        Op.getOperand(1).getOperand(0).getValueType() == Op.getValueType() &&
        isNullConstant(Op.getOperand(1).getOperand(1)))
      Op = Op.getOperand(1).getOperand(0);

    if (ISD::isConstantSplatVector(Op.getNode(), SplatVal))
      return V.getOperand(0);

    if (Op.getOpcode() == RISCVISD::VMV_V_X_VL && Op.getOperand(0).isUndef() &&
        Op.getOperand(2) == VL) {
      if (auto *Op1 = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
        SplatVal =
            Op1->getAPIntValue().sextOrTrunc(Op.getScalarValueSizeInBits());
        return V.getOperand(0);
      }
    }

    return SDValue();
  };

  SDLoc DL(N);

  auto DetectUSatPattern = [&](SDValue V) {
    APInt LoC, HiC;

    // Simple case, V is a UMIN.
    if (SDValue UMinOp = MatchMinMax(V, ISD::UMIN, RISCVISD::UMIN_VL, HiC))
      if (HiC.isMask(VT.getScalarSizeInBits()))
        return UMinOp;

    // If we have an SMAX that removes negative numbers first, then we can match
    // SMIN instead of UMIN.
    if (SDValue SMinOp = MatchMinMax(V, ISD::SMIN, RISCVISD::SMIN_VL, HiC))
      if (SDValue SMaxOp =
              MatchMinMax(SMinOp, ISD::SMAX, RISCVISD::SMAX_VL, LoC))
        if (LoC.isNonNegative() && HiC.isMask(VT.getScalarSizeInBits()))
          return SMinOp;

    // If we have an SMIN before an SMAX and the SMAX constant is less than or
    // equal to the SMIN constant, we can use vnclipu if we insert a new SMAX
    // first.
    if (SDValue SMaxOp = MatchMinMax(V, ISD::SMAX, RISCVISD::SMAX_VL, LoC))
      if (SDValue SMinOp =
              MatchMinMax(SMaxOp, ISD::SMIN, RISCVISD::SMIN_VL, HiC))
        if (LoC.isNonNegative() && HiC.isMask(VT.getScalarSizeInBits()) &&
            HiC.uge(LoC))
          return DAG.getNode(RISCVISD::SMAX_VL, DL, V.getValueType(), SMinOp,
                             V.getOperand(1), DAG.getUNDEF(V.getValueType()),
                             Mask, VL);

    return SDValue();
  };

  auto DetectSSatPattern = [&](SDValue V) {
    unsigned NumDstBits = VT.getScalarSizeInBits();
    unsigned NumSrcBits = V.getScalarValueSizeInBits();
    APInt SignedMax = APInt::getSignedMaxValue(NumDstBits).sext(NumSrcBits);
    APInt SignedMin = APInt::getSignedMinValue(NumDstBits).sext(NumSrcBits);

    APInt HiC, LoC;
    if (SDValue SMinOp = MatchMinMax(V, ISD::SMIN, RISCVISD::SMIN_VL, HiC))
      if (SDValue SMaxOp =
              MatchMinMax(SMinOp, ISD::SMAX, RISCVISD::SMAX_VL, LoC))
        if (HiC == SignedMax && LoC == SignedMin)
          return SMaxOp;

    if (SDValue SMaxOp = MatchMinMax(V, ISD::SMAX, RISCVISD::SMAX_VL, LoC))
      if (SDValue SMinOp =
              MatchMinMax(SMaxOp, ISD::SMIN, RISCVISD::SMIN_VL, HiC))
        if (HiC == SignedMax && LoC == SignedMin)
          return SMinOp;

    return SDValue();
  };

  SDValue Src = N->getOperand(0);

  // Look through multiple layers of truncates.
  while (Src.getOpcode() == RISCVISD::TRUNCATE_VECTOR_VL &&
         Src.getOperand(1) == Mask && Src.getOperand(2) == VL &&
         Src.hasOneUse())
    Src = Src.getOperand(0);

  SDValue Val;
  unsigned ClipOpc;
  if ((Val = DetectUSatPattern(Src)))
    ClipOpc = RISCVISD::VNCLIPU_VL;
  else if ((Val = DetectSSatPattern(Src)))
    ClipOpc = RISCVISD::VNCLIP_VL;
  else
    return SDValue();

  MVT ValVT = Val.getSimpleValueType();

  do {
    MVT ValEltVT = MVT::getIntegerVT(ValVT.getScalarSizeInBits() / 2);
    ValVT = ValVT.changeVectorElementType(ValEltVT);
    // Rounding mode here is arbitrary since we aren't shifting out any bits.
    Val = DAG.getNode(
        ClipOpc, DL, ValVT,
        {Val, DAG.getConstant(0, DL, ValVT), DAG.getUNDEF(VT), Mask,
         DAG.getTargetConstant(RISCVVXRndMode::RNU, DL, Subtarget.getXLenVT()),
         VL});
  } while (ValVT != VT);

  return Val;
}

SDValue RISCVTargetLowering::PerformDAGCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  const MVT XLenVT = Subtarget.getXLenVT();
  SDLoc DL(N);

  // Helper to call SimplifyDemandedBits on an operand of N where only some low
  // bits are demanded. N will be added to the Worklist if it was not deleted.
  // Caller should return SDValue(N, 0) if this returns true.
  auto SimplifyDemandedLowBitsHelper = [&](unsigned OpNo, unsigned LowBits) {
    SDValue Op = N->getOperand(OpNo);
    APInt Mask = APInt::getLowBitsSet(Op.getValueSizeInBits(), LowBits);
    if (!SimplifyDemandedBits(Op, Mask, DCI))
      return false;

    if (N->getOpcode() != ISD::DELETED_NODE)
      DCI.AddToWorklist(N);
    return true;
  };

  switch (N->getOpcode()) {
  default:
    break;
  case RISCVISD::SplitF64: {
    SDValue Op0 = N->getOperand(0);
    // If the input to SplitF64 is just BuildPairF64 then the operation is
    // redundant. Instead, use BuildPairF64's operands directly.
    if (Op0->getOpcode() == RISCVISD::BuildPairF64)
      return DCI.CombineTo(N, Op0.getOperand(0), Op0.getOperand(1));

    if (Op0->isUndef()) {
      SDValue Lo = DAG.getUNDEF(MVT::i32);
      SDValue Hi = DAG.getUNDEF(MVT::i32);
      return DCI.CombineTo(N, Lo, Hi);
    }

    // It's cheaper to materialise two 32-bit integers than to load a double
    // from the constant pool and transfer it to integer registers through the
    // stack.
    if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Op0)) {
      APInt V = C->getValueAPF().bitcastToAPInt();
      SDValue Lo = DAG.getConstant(V.trunc(32), DL, MVT::i32);
      SDValue Hi = DAG.getConstant(V.lshr(32).trunc(32), DL, MVT::i32);
      return DCI.CombineTo(N, Lo, Hi);
    }

    // This is a target-specific version of a DAGCombine performed in
    // DAGCombiner::visitBITCAST. It performs the equivalent of:
    // fold (bitconvert (fneg x)) -> (xor (bitconvert x), signbit)
    // fold (bitconvert (fabs x)) -> (and (bitconvert x), (not signbit))
    if (!(Op0.getOpcode() == ISD::FNEG || Op0.getOpcode() == ISD::FABS) ||
        !Op0.getNode()->hasOneUse())
      break;
    SDValue NewSplitF64 =
        DAG.getNode(RISCVISD::SplitF64, DL, DAG.getVTList(MVT::i32, MVT::i32),
                    Op0.getOperand(0));
    SDValue Lo = NewSplitF64.getValue(0);
    SDValue Hi = NewSplitF64.getValue(1);
    APInt SignBit = APInt::getSignMask(32);
    if (Op0.getOpcode() == ISD::FNEG) {
      SDValue NewHi = DAG.getNode(ISD::XOR, DL, MVT::i32, Hi,
                                  DAG.getConstant(SignBit, DL, MVT::i32));
      return DCI.CombineTo(N, Lo, NewHi);
    }
    assert(Op0.getOpcode() == ISD::FABS);
    SDValue NewHi = DAG.getNode(ISD::AND, DL, MVT::i32, Hi,
                                DAG.getConstant(~SignBit, DL, MVT::i32));
    return DCI.CombineTo(N, Lo, NewHi);
  }
  case RISCVISD::SLLW:
  case RISCVISD::SRAW:
  case RISCVISD::SRLW:
  case RISCVISD::RORW:
  case RISCVISD::ROLW: {
    // Only the lower 32 bits of LHS and lower 5 bits of RHS are read.
    if (SimplifyDemandedLowBitsHelper(0, 32) ||
        SimplifyDemandedLowBitsHelper(1, 5))
      return SDValue(N, 0);

    break;
  }
  case RISCVISD::CLZW:
  case RISCVISD::CTZW: {
    // Only the lower 32 bits of the first operand are read
    if (SimplifyDemandedLowBitsHelper(0, 32))
      return SDValue(N, 0);
    break;
  }
  case RISCVISD::FMV_W_X_RV64: {
    // If the input to FMV_W_X_RV64 is just FMV_X_ANYEXTW_RV64 the the
    // conversion is unnecessary and can be replaced with the
    // FMV_X_ANYEXTW_RV64 operand.
    SDValue Op0 = N->getOperand(0);
    if (Op0.getOpcode() == RISCVISD::FMV_X_ANYEXTW_RV64)
      return Op0.getOperand(0);
    break;
  }
  case RISCVISD::FMV_X_ANYEXTH:
  case RISCVISD::FMV_X_ANYEXTW_RV64: {
    SDLoc DL(N);
    SDValue Op0 = N->getOperand(0);
    MVT VT = N->getSimpleValueType(0);
    // If the input to FMV_X_ANYEXTW_RV64 is just FMV_W_X_RV64 then the
    // conversion is unnecessary and can be replaced with the FMV_W_X_RV64
    // operand. Similar for FMV_X_ANYEXTH and FMV_H_X.
    if ((N->getOpcode() == RISCVISD::FMV_X_ANYEXTW_RV64 &&
         Op0->getOpcode() == RISCVISD::FMV_W_X_RV64) ||
        (N->getOpcode() == RISCVISD::FMV_X_ANYEXTH &&
         Op0->getOpcode() == RISCVISD::FMV_H_X)) {
      assert(Op0.getOperand(0).getValueType() == VT &&
             "Unexpected value type!");
      return Op0.getOperand(0);
    }

    // This is a target-specific version of a DAGCombine performed in
    // DAGCombiner::visitBITCAST. It performs the equivalent of:
    // fold (bitconvert (fneg x)) -> (xor (bitconvert x), signbit)
    // fold (bitconvert (fabs x)) -> (and (bitconvert x), (not signbit))
    if (!(Op0.getOpcode() == ISD::FNEG || Op0.getOpcode() == ISD::FABS) ||
        !Op0.getNode()->hasOneUse())
      break;
    SDValue NewFMV = DAG.getNode(N->getOpcode(), DL, VT, Op0.getOperand(0));
    unsigned FPBits = N->getOpcode() == RISCVISD::FMV_X_ANYEXTW_RV64 ? 32 : 16;
    APInt SignBit = APInt::getSignMask(FPBits).sext(VT.getSizeInBits());
    if (Op0.getOpcode() == ISD::FNEG)
      return DAG.getNode(ISD::XOR, DL, VT, NewFMV,
                         DAG.getConstant(SignBit, DL, VT));

    assert(Op0.getOpcode() == ISD::FABS);
    return DAG.getNode(ISD::AND, DL, VT, NewFMV,
                       DAG.getConstant(~SignBit, DL, VT));
  }
  case ISD::ABS: {
    EVT VT = N->getValueType(0);
    SDValue N0 = N->getOperand(0);
    // abs (sext) -> zext (abs)
    // abs (zext) -> zext (handled elsewhere)
    if (VT.isVector() && N0.hasOneUse() && N0.getOpcode() == ISD::SIGN_EXTEND) {
      SDValue Src = N0.getOperand(0);
      SDLoc DL(N);
      return DAG.getNode(ISD::ZERO_EXTEND, DL, VT,
                         DAG.getNode(ISD::ABS, DL, Src.getValueType(), Src));
    }
    break;
  }
  case ISD::ADD: {
    if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
      return V;
    if (SDValue V = combineToVWMACC(N, DAG, Subtarget))
      return V;
    return performADDCombine(N, DCI, Subtarget);
  }
  case ISD::SUB: {
    if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
      return V;
    return performSUBCombine(N, DAG, Subtarget);
  }
  case ISD::AND:
    return performANDCombine(N, DCI, Subtarget);
  case ISD::OR: {
    if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
      return V;
    return performORCombine(N, DCI, Subtarget);
  }
  case ISD::XOR:
    return performXORCombine(N, DAG, Subtarget);
  case ISD::MUL:
    if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
      return V;
    return performMULCombine(N, DAG, DCI, Subtarget);
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM:
    if (SDValue V = combineBinOpOfZExt(N, DAG))
      return V;
    break;
  case ISD::FADD:
  case ISD::UMAX:
  case ISD::UMIN:
  case ISD::SMAX:
  case ISD::SMIN:
  case ISD::FMAXNUM:
  case ISD::FMINNUM: {
    if (SDValue V = combineBinOpToReduce(N, DAG, Subtarget))
      return V;
    if (SDValue V = combineBinOpOfExtractToReduceTree(N, DAG, Subtarget))
      return V;
    return SDValue();
  }
  case ISD::SETCC:
    return performSETCCCombine(N, DAG, Subtarget);
  case ISD::SIGN_EXTEND_INREG:
    return performSIGN_EXTEND_INREGCombine(N, DAG, Subtarget);
  case ISD::ZERO_EXTEND:
    // Fold (zero_extend (fp_to_uint X)) to prevent forming fcvt+zexti32 during
    // type legalization. This is safe because fp_to_uint produces poison if
    // it overflows.
    if (N->getValueType(0) == MVT::i64 && Subtarget.is64Bit()) {
      SDValue Src = N->getOperand(0);
      if (Src.getOpcode() == ISD::FP_TO_UINT &&
          isTypeLegal(Src.getOperand(0).getValueType()))
        return DAG.getNode(ISD::FP_TO_UINT, SDLoc(N), MVT::i64,
                           Src.getOperand(0));
      if (Src.getOpcode() == ISD::STRICT_FP_TO_UINT && Src.hasOneUse() &&
          isTypeLegal(Src.getOperand(1).getValueType())) {
        SDVTList VTs = DAG.getVTList(MVT::i64, MVT::Other);
        SDValue Res = DAG.getNode(ISD::STRICT_FP_TO_UINT, SDLoc(N), VTs,
                                  Src.getOperand(0), Src.getOperand(1));
        DCI.CombineTo(N, Res);
        DAG.ReplaceAllUsesOfValueWith(Src.getValue(1), Res.getValue(1));
        DCI.recursivelyDeleteUnusedNodes(Src.getNode());
        return SDValue(N, 0); // Return N so it doesn't get rechecked.
      }
    }
    return SDValue();
  case RISCVISD::TRUNCATE_VECTOR_VL:
    if (SDValue V = combineTruncOfSraSext(N, DAG))
      return V;
    return combineTruncToVnclip(N, DAG, Subtarget);
  case ISD::TRUNCATE:
    return performTRUNCATECombine(N, DAG, Subtarget);
  case ISD::SELECT:
    return performSELECTCombine(N, DAG, Subtarget);
  case RISCVISD::CZERO_EQZ:
  case RISCVISD::CZERO_NEZ: {
    SDValue Val = N->getOperand(0);
    SDValue Cond = N->getOperand(1);

    unsigned Opc = N->getOpcode();

    // czero_eqz x, x -> x
    if (Opc == RISCVISD::CZERO_EQZ && Val == Cond)
      return Val;

    unsigned InvOpc =
        Opc == RISCVISD::CZERO_EQZ ? RISCVISD::CZERO_NEZ : RISCVISD::CZERO_EQZ;

    // czero_eqz X, (xor Y, 1) -> czero_nez X, Y if Y is 0 or 1.
    // czero_nez X, (xor Y, 1) -> czero_eqz X, Y if Y is 0 or 1.
    if (Cond.getOpcode() == ISD::XOR && isOneConstant(Cond.getOperand(1))) {
      SDValue NewCond = Cond.getOperand(0);
      APInt Mask = APInt::getBitsSetFrom(NewCond.getValueSizeInBits(), 1);
      if (DAG.MaskedValueIsZero(NewCond, Mask))
        return DAG.getNode(InvOpc, SDLoc(N), N->getValueType(0), Val, NewCond);
    }
    // czero_eqz x, (setcc y, 0, ne) -> czero_eqz x, y
    // czero_nez x, (setcc y, 0, ne) -> czero_nez x, y
    // czero_eqz x, (setcc y, 0, eq) -> czero_nez x, y
    // czero_nez x, (setcc y, 0, eq) -> czero_eqz x, y
    if (Cond.getOpcode() == ISD::SETCC && isNullConstant(Cond.getOperand(1))) {
      ISD::CondCode CCVal = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
      if (ISD::isIntEqualitySetCC(CCVal))
        return DAG.getNode(CCVal == ISD::SETNE ? Opc : InvOpc, SDLoc(N),
                           N->getValueType(0), Val, Cond.getOperand(0));
    }
    return SDValue();
  }
  case RISCVISD::SELECT_CC: {
    // Transform
    SDValue LHS = N->getOperand(0);
    SDValue RHS = N->getOperand(1);
    SDValue CC = N->getOperand(2);
    ISD::CondCode CCVal = cast<CondCodeSDNode>(CC)->get();
    SDValue TrueV = N->getOperand(3);
    SDValue FalseV = N->getOperand(4);
    SDLoc DL(N);
    EVT VT = N->getValueType(0);

    // If the True and False values are the same, we don't need a select_cc.
    if (TrueV == FalseV)
      return TrueV;

    // (select (x < 0), y, z)  -> x >> (XLEN - 1) & (y - z) + z
    // (select (x >= 0), y, z) -> x >> (XLEN - 1) & (z - y) + y
    if (!Subtarget.hasShortForwardBranchOpt() && isa<ConstantSDNode>(TrueV) &&
        isa<ConstantSDNode>(FalseV) && isNullConstant(RHS) &&
        (CCVal == ISD::CondCode::SETLT || CCVal == ISD::CondCode::SETGE)) {
      if (CCVal == ISD::CondCode::SETGE)
        std::swap(TrueV, FalseV);

      int64_t TrueSImm = cast<ConstantSDNode>(TrueV)->getSExtValue();
      int64_t FalseSImm = cast<ConstantSDNode>(FalseV)->getSExtValue();
      // Only handle simm12, if it is not in this range, it can be considered as
      // register.
      if (isInt<12>(TrueSImm) && isInt<12>(FalseSImm) &&
          isInt<12>(TrueSImm - FalseSImm)) {
        SDValue SRA =
            DAG.getNode(ISD::SRA, DL, VT, LHS,
                        DAG.getConstant(Subtarget.getXLen() - 1, DL, VT));
        SDValue AND =
            DAG.getNode(ISD::AND, DL, VT, SRA,
                        DAG.getConstant(TrueSImm - FalseSImm, DL, VT));
        return DAG.getNode(ISD::ADD, DL, VT, AND, FalseV);
      }

      if (CCVal == ISD::CondCode::SETGE)
        std::swap(TrueV, FalseV);
    }

    if (combine_CC(LHS, RHS, CC, DL, DAG, Subtarget))
      return DAG.getNode(RISCVISD::SELECT_CC, DL, N->getValueType(0),
                         {LHS, RHS, CC, TrueV, FalseV});

    if (!Subtarget.hasConditionalMoveFusion()) {
      // (select c, -1, y) -> -c | y
      if (isAllOnesConstant(TrueV)) {
        SDValue C = DAG.getSetCC(DL, VT, LHS, RHS, CCVal);
        SDValue Neg = DAG.getNegative(C, DL, VT);
        return DAG.getNode(ISD::OR, DL, VT, Neg, FalseV);
      }
      // (select c, y, -1) -> -!c | y
      if (isAllOnesConstant(FalseV)) {
        SDValue C =
            DAG.getSetCC(DL, VT, LHS, RHS, ISD::getSetCCInverse(CCVal, VT));
        SDValue Neg = DAG.getNegative(C, DL, VT);
        return DAG.getNode(ISD::OR, DL, VT, Neg, TrueV);
      }

      // (select c, 0, y) -> -!c & y
      if (isNullConstant(TrueV)) {
        SDValue C =
            DAG.getSetCC(DL, VT, LHS, RHS, ISD::getSetCCInverse(CCVal, VT));
        SDValue Neg = DAG.getNegative(C, DL, VT);
        return DAG.getNode(ISD::AND, DL, VT, Neg, FalseV);
      }
      // (select c, y, 0) -> -c & y
      if (isNullConstant(FalseV)) {
        SDValue C = DAG.getSetCC(DL, VT, LHS, RHS, CCVal);
        SDValue Neg = DAG.getNegative(C, DL, VT);
        return DAG.getNode(ISD::AND, DL, VT, Neg, TrueV);
      }
      // (riscvisd::select_cc x, 0, ne, x, 1) -> (add x, (setcc x, 0, eq))
      // (riscvisd::select_cc x, 0, eq, 1, x) -> (add x, (setcc x, 0, eq))
      if (((isOneConstant(FalseV) && LHS == TrueV &&
            CCVal == ISD::CondCode::SETNE) ||
           (isOneConstant(TrueV) && LHS == FalseV &&
            CCVal == ISD::CondCode::SETEQ)) &&
          isNullConstant(RHS)) {
        // freeze it to be safe.
        LHS = DAG.getFreeze(LHS);
        SDValue C = DAG.getSetCC(DL, VT, LHS, RHS, ISD::CondCode::SETEQ);
        return DAG.getNode(ISD::ADD, DL, VT, LHS, C);
      }
    }

    // If both true/false are an xor with 1, pull through the select.
    // This can occur after op legalization if both operands are setccs that
    // require an xor to invert.
    // FIXME: Generalize to other binary ops with identical operand?
    if (TrueV.getOpcode() == ISD::XOR && FalseV.getOpcode() == ISD::XOR &&
        TrueV.getOperand(1) == FalseV.getOperand(1) &&
        isOneConstant(TrueV.getOperand(1)) &&
        TrueV.hasOneUse() && FalseV.hasOneUse()) {
      SDValue NewSel = DAG.getNode(RISCVISD::SELECT_CC, DL, VT, LHS, RHS, CC,
                                   TrueV.getOperand(0), FalseV.getOperand(0));
      return DAG.getNode(ISD::XOR, DL, VT, NewSel, TrueV.getOperand(1));
    }

    return SDValue();
  }
  case RISCVISD::BR_CC: {
    SDValue LHS = N->getOperand(1);
    SDValue RHS = N->getOperand(2);
    SDValue CC = N->getOperand(3);
    SDLoc DL(N);

    if (combine_CC(LHS, RHS, CC, DL, DAG, Subtarget))
      return DAG.getNode(RISCVISD::BR_CC, DL, N->getValueType(0),
                         N->getOperand(0), LHS, RHS, CC, N->getOperand(4));

    return SDValue();
  }
  case ISD::BITREVERSE:
    return performBITREVERSECombine(N, DAG, Subtarget);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return performFP_TO_INTCombine(N, DCI, Subtarget);
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    return performFP_TO_INT_SATCombine(N, DCI, Subtarget);
  case ISD::FCOPYSIGN: {
    EVT VT = N->getValueType(0);
    if (!VT.isVector())
      break;
    // There is a form of VFSGNJ which injects the negated sign of its second
    // operand. Try and bubble any FNEG up after the extend/round to produce
    // this optimized pattern. Avoid modifying cases where FP_ROUND and
    // TRUNC=1.
    SDValue In2 = N->getOperand(1);
    // Avoid cases where the extend/round has multiple uses, as duplicating
    // those is typically more expensive than removing a fneg.
    if (!In2.hasOneUse())
      break;
    if (In2.getOpcode() != ISD::FP_EXTEND &&
        (In2.getOpcode() != ISD::FP_ROUND || In2.getConstantOperandVal(1) != 0))
      break;
    In2 = In2.getOperand(0);
    if (In2.getOpcode() != ISD::FNEG)
      break;
    SDLoc DL(N);
    SDValue NewFPExtRound = DAG.getFPExtendOrRound(In2.getOperand(0), DL, VT);
    return DAG.getNode(ISD::FCOPYSIGN, DL, VT, N->getOperand(0),
                       DAG.getNode(ISD::FNEG, DL, VT, NewFPExtRound));
  }
  case ISD::MGATHER: {
    const auto *MGN = cast<MaskedGatherSDNode>(N);
    const EVT VT = N->getValueType(0);
    SDValue Index = MGN->getIndex();
    SDValue ScaleOp = MGN->getScale();
    ISD::MemIndexType IndexType = MGN->getIndexType();
    assert(!MGN->isIndexScaled() &&
           "Scaled gather/scatter should not be formed");

    SDLoc DL(N);
    if (legalizeScatterGatherIndexType(DL, Index, IndexType, DCI))
      return DAG.getMaskedGather(
          N->getVTList(), MGN->getMemoryVT(), DL,
          {MGN->getChain(), MGN->getPassThru(), MGN->getMask(),
           MGN->getBasePtr(), Index, ScaleOp},
          MGN->getMemOperand(), IndexType, MGN->getExtensionType());

    if (narrowIndex(Index, IndexType, DAG))
      return DAG.getMaskedGather(
          N->getVTList(), MGN->getMemoryVT(), DL,
          {MGN->getChain(), MGN->getPassThru(), MGN->getMask(),
           MGN->getBasePtr(), Index, ScaleOp},
          MGN->getMemOperand(), IndexType, MGN->getExtensionType());

    if (Index.getOpcode() == ISD::BUILD_VECTOR &&
        MGN->getExtensionType() == ISD::NON_EXTLOAD && isTypeLegal(VT)) {
      // The sequence will be XLenVT, not the type of Index. Tell
      // isSimpleVIDSequence this so we avoid overflow.
      if (std::optional<VIDSequence> SimpleVID =
              isSimpleVIDSequence(Index, Subtarget.getXLen());
          SimpleVID && SimpleVID->StepDenominator == 1) {
        const int64_t StepNumerator = SimpleVID->StepNumerator;
        const int64_t Addend = SimpleVID->Addend;

        // Note: We don't need to check alignment here since (by assumption
        // from the existance of the gather), our offsets must be sufficiently
        // aligned.

        const EVT PtrVT = getPointerTy(DAG.getDataLayout());
        assert(MGN->getBasePtr()->getValueType(0) == PtrVT);
        assert(IndexType == ISD::UNSIGNED_SCALED);
        SDValue BasePtr = DAG.getNode(ISD::ADD, DL, PtrVT, MGN->getBasePtr(),
                                      DAG.getConstant(Addend, DL, PtrVT));

        SDValue EVL = DAG.getElementCount(DL, Subtarget.getXLenVT(),
                                          VT.getVectorElementCount());
        SDValue StridedLoad =
            DAG.getStridedLoadVP(VT, DL, MGN->getChain(), BasePtr,
                                 DAG.getConstant(StepNumerator, DL, XLenVT),
                                 MGN->getMask(), EVL, MGN->getMemOperand());
        SDValue VPSelect = DAG.getNode(ISD::VP_SELECT, DL, VT, MGN->getMask(),
                                       StridedLoad, MGN->getPassThru(), EVL);
        return DAG.getMergeValues({VPSelect, SDValue(StridedLoad.getNode(), 1)},
                                  DL);
      }
    }

    SmallVector<int> ShuffleMask;
    if (MGN->getExtensionType() == ISD::NON_EXTLOAD &&
        matchIndexAsShuffle(VT, Index, MGN->getMask(), ShuffleMask)) {
      SDValue Load = DAG.getMaskedLoad(VT, DL, MGN->getChain(),
                                       MGN->getBasePtr(), DAG.getUNDEF(XLenVT),
                                       MGN->getMask(), DAG.getUNDEF(VT),
                                       MGN->getMemoryVT(), MGN->getMemOperand(),
                                       ISD::UNINDEXED, ISD::NON_EXTLOAD);
      SDValue Shuffle =
        DAG.getVectorShuffle(VT, DL, Load, DAG.getUNDEF(VT), ShuffleMask);
      return DAG.getMergeValues({Shuffle, Load.getValue(1)}, DL);
    }

    if (MGN->getExtensionType() == ISD::NON_EXTLOAD &&
        matchIndexAsWiderOp(VT, Index, MGN->getMask(),
                            MGN->getMemOperand()->getBaseAlign(), Subtarget)) {
      SmallVector<SDValue> NewIndices;
      for (unsigned i = 0; i < Index->getNumOperands(); i += 2)
        NewIndices.push_back(Index.getOperand(i));
      EVT IndexVT = Index.getValueType()
        .getHalfNumVectorElementsVT(*DAG.getContext());
      Index = DAG.getBuildVector(IndexVT, DL, NewIndices);

      unsigned ElementSize = VT.getScalarStoreSize();
      EVT WideScalarVT = MVT::getIntegerVT(ElementSize * 8 * 2);
      auto EltCnt = VT.getVectorElementCount();
      assert(EltCnt.isKnownEven() && "Splitting vector, but not in half!");
      EVT WideVT = EVT::getVectorVT(*DAG.getContext(), WideScalarVT,
                                    EltCnt.divideCoefficientBy(2));
      SDValue Passthru = DAG.getBitcast(WideVT, MGN->getPassThru());
      EVT MaskVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                                    EltCnt.divideCoefficientBy(2));
      SDValue Mask = DAG.getSplat(MaskVT, DL, DAG.getConstant(1, DL, MVT::i1));

      SDValue Gather =
        DAG.getMaskedGather(DAG.getVTList(WideVT, MVT::Other), WideVT, DL,
                            {MGN->getChain(), Passthru, Mask, MGN->getBasePtr(),
                             Index, ScaleOp},
                            MGN->getMemOperand(), IndexType, ISD::NON_EXTLOAD);
      SDValue Result = DAG.getBitcast(VT, Gather.getValue(0));
      return DAG.getMergeValues({Result, Gather.getValue(1)}, DL);
    }
    break;
  }
  case ISD::MSCATTER:{
    const auto *MSN = cast<MaskedScatterSDNode>(N);
    SDValue Index = MSN->getIndex();
    SDValue ScaleOp = MSN->getScale();
    ISD::MemIndexType IndexType = MSN->getIndexType();
    assert(!MSN->isIndexScaled() &&
           "Scaled gather/scatter should not be formed");

    SDLoc DL(N);
    if (legalizeScatterGatherIndexType(DL, Index, IndexType, DCI))
      return DAG.getMaskedScatter(
          N->getVTList(), MSN->getMemoryVT(), DL,
          {MSN->getChain(), MSN->getValue(), MSN->getMask(), MSN->getBasePtr(),
           Index, ScaleOp},
          MSN->getMemOperand(), IndexType, MSN->isTruncatingStore());

    if (narrowIndex(Index, IndexType, DAG))
      return DAG.getMaskedScatter(
          N->getVTList(), MSN->getMemoryVT(), DL,
          {MSN->getChain(), MSN->getValue(), MSN->getMask(), MSN->getBasePtr(),
           Index, ScaleOp},
          MSN->getMemOperand(), IndexType, MSN->isTruncatingStore());

    EVT VT = MSN->getValue()->getValueType(0);
    SmallVector<int> ShuffleMask;
    if (!MSN->isTruncatingStore() &&
        matchIndexAsShuffle(VT, Index, MSN->getMask(), ShuffleMask)) {
      SDValue Shuffle = DAG.getVectorShuffle(VT, DL, MSN->getValue(),
                                             DAG.getUNDEF(VT), ShuffleMask);
      return DAG.getMaskedStore(MSN->getChain(), DL, Shuffle, MSN->getBasePtr(),
                                DAG.getUNDEF(XLenVT), MSN->getMask(),
                                MSN->getMemoryVT(), MSN->getMemOperand(),
                                ISD::UNINDEXED, false);
    }
    break;
  }
  case ISD::VP_GATHER: {
    const auto *VPGN = cast<VPGatherSDNode>(N);
    SDValue Index = VPGN->getIndex();
    SDValue ScaleOp = VPGN->getScale();
    ISD::MemIndexType IndexType = VPGN->getIndexType();
    assert(!VPGN->isIndexScaled() &&
           "Scaled gather/scatter should not be formed");

    SDLoc DL(N);
    if (legalizeScatterGatherIndexType(DL, Index, IndexType, DCI))
      return DAG.getGatherVP(N->getVTList(), VPGN->getMemoryVT(), DL,
                             {VPGN->getChain(), VPGN->getBasePtr(), Index,
                              ScaleOp, VPGN->getMask(),
                              VPGN->getVectorLength()},
                             VPGN->getMemOperand(), IndexType);

    if (narrowIndex(Index, IndexType, DAG))
      return DAG.getGatherVP(N->getVTList(), VPGN->getMemoryVT(), DL,
                             {VPGN->getChain(), VPGN->getBasePtr(), Index,
                              ScaleOp, VPGN->getMask(),
                              VPGN->getVectorLength()},
                             VPGN->getMemOperand(), IndexType);

    break;
  }
  case ISD::VP_SCATTER: {
    const auto *VPSN = cast<VPScatterSDNode>(N);
    SDValue Index = VPSN->getIndex();
    SDValue ScaleOp = VPSN->getScale();
    ISD::MemIndexType IndexType = VPSN->getIndexType();
    assert(!VPSN->isIndexScaled() &&
           "Scaled gather/scatter should not be formed");

    SDLoc DL(N);
    if (legalizeScatterGatherIndexType(DL, Index, IndexType, DCI))
      return DAG.getScatterVP(N->getVTList(), VPSN->getMemoryVT(), DL,
                              {VPSN->getChain(), VPSN->getValue(),
                               VPSN->getBasePtr(), Index, ScaleOp,
                               VPSN->getMask(), VPSN->getVectorLength()},
                              VPSN->getMemOperand(), IndexType);

    if (narrowIndex(Index, IndexType, DAG))
      return DAG.getScatterVP(N->getVTList(), VPSN->getMemoryVT(), DL,
                              {VPSN->getChain(), VPSN->getValue(),
                               VPSN->getBasePtr(), Index, ScaleOp,
                               VPSN->getMask(), VPSN->getVectorLength()},
                              VPSN->getMemOperand(), IndexType);
    break;
  }
  case RISCVISD::SHL_VL:
    if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
      return V;
    [[fallthrough]];
  case RISCVISD::SRA_VL:
  case RISCVISD::SRL_VL: {
    SDValue ShAmt = N->getOperand(1);
    if (ShAmt.getOpcode() == RISCVISD::SPLAT_VECTOR_SPLIT_I64_VL) {
      // We don't need the upper 32 bits of a 64-bit element for a shift amount.
      SDLoc DL(N);
      SDValue VL = N->getOperand(4);
      EVT VT = N->getValueType(0);
      ShAmt = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, DAG.getUNDEF(VT),
                          ShAmt.getOperand(1), VL);
      return DAG.getNode(N->getOpcode(), DL, VT, N->getOperand(0), ShAmt,
                         N->getOperand(2), N->getOperand(3), N->getOperand(4));
    }
    break;
  }
  case ISD::SRA:
    if (SDValue V = performSRACombine(N, DAG, Subtarget))
      return V;
    [[fallthrough]];
  case ISD::SRL:
  case ISD::SHL: {
    if (N->getOpcode() == ISD::SHL) {
      if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
        return V;
    }
    SDValue ShAmt = N->getOperand(1);
    if (ShAmt.getOpcode() == RISCVISD::SPLAT_VECTOR_SPLIT_I64_VL) {
      // We don't need the upper 32 bits of a 64-bit element for a shift amount.
      SDLoc DL(N);
      EVT VT = N->getValueType(0);
      ShAmt = DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, DAG.getUNDEF(VT),
                          ShAmt.getOperand(1),
                          DAG.getRegister(RISCV::X0, Subtarget.getXLenVT()));
      return DAG.getNode(N->getOpcode(), DL, VT, N->getOperand(0), ShAmt);
    }
    break;
  }
  case RISCVISD::ADD_VL:
    if (SDValue V = combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget))
      return V;
    return combineToVWMACC(N, DAG, Subtarget);
  case RISCVISD::VWADD_W_VL:
  case RISCVISD::VWADDU_W_VL:
  case RISCVISD::VWSUB_W_VL:
  case RISCVISD::VWSUBU_W_VL:
    return performVWADDSUBW_VLCombine(N, DCI, Subtarget);
  case RISCVISD::SUB_VL:
  case RISCVISD::MUL_VL:
    return combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget);
  case RISCVISD::VFMADD_VL:
  case RISCVISD::VFNMADD_VL:
  case RISCVISD::VFMSUB_VL:
  case RISCVISD::VFNMSUB_VL:
  case RISCVISD::STRICT_VFMADD_VL:
  case RISCVISD::STRICT_VFNMADD_VL:
  case RISCVISD::STRICT_VFMSUB_VL:
  case RISCVISD::STRICT_VFNMSUB_VL:
    return performVFMADD_VLCombine(N, DAG, Subtarget);
  case RISCVISD::FADD_VL:
  case RISCVISD::FSUB_VL:
  case RISCVISD::FMUL_VL:
  case RISCVISD::VFWADD_W_VL:
  case RISCVISD::VFWSUB_W_VL: {
    if (N->getValueType(0).getVectorElementType() == MVT::f32 &&
        !Subtarget.hasVInstructionsF16())
      return SDValue();
    return combineBinOp_VLToVWBinOp_VL(N, DCI, Subtarget);
  }
  case ISD::LOAD:
  case ISD::STORE: {
    if (DCI.isAfterLegalizeDAG())
      if (SDValue V = performMemPairCombine(N, DCI))
        return V;

    if (N->getOpcode() != ISD::STORE)
      break;

    auto *Store = cast<StoreSDNode>(N);
    SDValue Chain = Store->getChain();
    EVT MemVT = Store->getMemoryVT();
    SDValue Val = Store->getValue();
    SDLoc DL(N);

    bool IsScalarizable =
        MemVT.isFixedLengthVector() && ISD::isNormalStore(Store) &&
        Store->isSimple() &&
        MemVT.getVectorElementType().bitsLE(Subtarget.getXLenVT()) &&
        isPowerOf2_64(MemVT.getSizeInBits()) &&
        MemVT.getSizeInBits() <= Subtarget.getXLen();

    // If sufficiently aligned we can scalarize stores of constant vectors of
    // any power-of-two size up to XLen bits, provided that they aren't too
    // expensive to materialize.
    //   vsetivli   zero, 2, e8, m1, ta, ma
    //   vmv.v.i    v8, 4
    //   vse64.v    v8, (a0)
    // ->
    //   li     a1, 1028
    //   sh     a1, 0(a0)
    if (DCI.isBeforeLegalize() && IsScalarizable &&
        ISD::isBuildVectorOfConstantSDNodes(Val.getNode())) {
      // Get the constant vector bits
      APInt NewC(Val.getValueSizeInBits(), 0);
      uint64_t EltSize = Val.getScalarValueSizeInBits();
      for (unsigned i = 0; i < Val.getNumOperands(); i++) {
        if (Val.getOperand(i).isUndef())
          continue;
        NewC.insertBits(Val.getConstantOperandAPInt(i).trunc(EltSize),
                        i * EltSize);
      }
      MVT NewVT = MVT::getIntegerVT(MemVT.getSizeInBits());

      if (RISCVMatInt::getIntMatCost(NewC, Subtarget.getXLen(), Subtarget,
                                     true) <= 2 &&
          allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                         NewVT, *Store->getMemOperand())) {
        SDValue NewV = DAG.getConstant(NewC, DL, NewVT);
        return DAG.getStore(Chain, DL, NewV, Store->getBasePtr(),
                            Store->getPointerInfo(), Store->getOriginalAlign(),
                            Store->getMemOperand()->getFlags());
      }
    }

    // Similarly, if sufficiently aligned we can scalarize vector copies, e.g.
    //   vsetivli   zero, 2, e16, m1, ta, ma
    //   vle16.v    v8, (a0)
    //   vse16.v    v8, (a1)
    if (auto *L = dyn_cast<LoadSDNode>(Val);
        L && DCI.isBeforeLegalize() && IsScalarizable && L->isSimple() &&
        L->hasNUsesOfValue(1, 0) && L->hasNUsesOfValue(1, 1) &&
        Store->getChain() == SDValue(L, 1) && ISD::isNormalLoad(L) &&
        L->getMemoryVT() == MemVT) {
      MVT NewVT = MVT::getIntegerVT(MemVT.getSizeInBits());
      if (allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                         NewVT, *Store->getMemOperand()) &&
          allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                         NewVT, *L->getMemOperand())) {
        SDValue NewL = DAG.getLoad(NewVT, DL, L->getChain(), L->getBasePtr(),
                                   L->getPointerInfo(), L->getOriginalAlign(),
                                   L->getMemOperand()->getFlags());
        return DAG.getStore(Chain, DL, NewL, Store->getBasePtr(),
                            Store->getPointerInfo(), Store->getOriginalAlign(),
                            Store->getMemOperand()->getFlags());
      }
    }

    // Combine store of vmv.x.s/vfmv.f.s to vse with VL of 1.
    // vfmv.f.s is represented as extract element from 0. Match it late to avoid
    // any illegal types.
    if (Val.getOpcode() == RISCVISD::VMV_X_S ||
        (DCI.isAfterLegalizeDAG() &&
         Val.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
         isNullConstant(Val.getOperand(1)))) {
      SDValue Src = Val.getOperand(0);
      MVT VecVT = Src.getSimpleValueType();
      // VecVT should be scalable and memory VT should match the element type.
      if (!Store->isIndexed() && VecVT.isScalableVector() &&
          MemVT == VecVT.getVectorElementType()) {
        SDLoc DL(N);
        MVT MaskVT = getMaskTypeFor(VecVT);
        return DAG.getStoreVP(
            Store->getChain(), DL, Src, Store->getBasePtr(), Store->getOffset(),
            DAG.getConstant(1, DL, MaskVT),
            DAG.getConstant(1, DL, Subtarget.getXLenVT()), MemVT,
            Store->getMemOperand(), Store->getAddressingMode(),
            Store->isTruncatingStore(), /*IsCompress*/ false);
      }
    }

    break;
  }
  case ISD::SPLAT_VECTOR: {
    EVT VT = N->getValueType(0);
    // Only perform this combine on legal MVT types.
    if (!isTypeLegal(VT))
      break;
    if (auto Gather = matchSplatAsGather(N->getOperand(0), VT.getSimpleVT(), N,
                                         DAG, Subtarget))
      return Gather;
    break;
  }
  case ISD::BUILD_VECTOR:
    if (SDValue V = performBUILD_VECTORCombine(N, DAG, Subtarget, *this))
      return V;
    break;
  case ISD::CONCAT_VECTORS:
    if (SDValue V = performCONCAT_VECTORSCombine(N, DAG, Subtarget, *this))
      return V;
    break;
  case ISD::INSERT_VECTOR_ELT:
    if (SDValue V = performINSERT_VECTOR_ELTCombine(N, DAG, Subtarget, *this))
      return V;
    break;
  case RISCVISD::VFMV_V_F_VL: {
    const MVT VT = N->getSimpleValueType(0);
    SDValue Passthru = N->getOperand(0);
    SDValue Scalar = N->getOperand(1);
    SDValue VL = N->getOperand(2);

    // If VL is 1, we can use vfmv.s.f.
    if (isOneConstant(VL))
      return DAG.getNode(RISCVISD::VFMV_S_F_VL, DL, VT, Passthru, Scalar, VL);
    break;
  }
  case RISCVISD::VMV_V_X_VL: {
    const MVT VT = N->getSimpleValueType(0);
    SDValue Passthru = N->getOperand(0);
    SDValue Scalar = N->getOperand(1);
    SDValue VL = N->getOperand(2);

    // Tail agnostic VMV.V.X only demands the vector element bitwidth from the
    // scalar input.
    unsigned ScalarSize = Scalar.getValueSizeInBits();
    unsigned EltWidth = VT.getScalarSizeInBits();
    if (ScalarSize > EltWidth && Passthru.isUndef())
      if (SimplifyDemandedLowBitsHelper(1, EltWidth))
        return SDValue(N, 0);

    // If VL is 1 and the scalar value won't benefit from immediate, we can
    // use vmv.s.x.
    ConstantSDNode *Const = dyn_cast<ConstantSDNode>(Scalar);
    if (isOneConstant(VL) &&
        (!Const || Const->isZero() ||
         !Const->getAPIntValue().sextOrTrunc(EltWidth).isSignedIntN(5)))
      return DAG.getNode(RISCVISD::VMV_S_X_VL, DL, VT, Passthru, Scalar, VL);

    break;
  }
  case RISCVISD::VFMV_S_F_VL: {
    SDValue Src = N->getOperand(1);
    // Try to remove vector->scalar->vector if the scalar->vector is inserting
    // into an undef vector.
    // TODO: Could use a vslide or vmv.v.v for non-undef.
    if (N->getOperand(0).isUndef() &&
        Src.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
        isNullConstant(Src.getOperand(1)) &&
        Src.getOperand(0).getValueType().isScalableVector()) {
      EVT VT = N->getValueType(0);
      EVT SrcVT = Src.getOperand(0).getValueType();
      assert(SrcVT.getVectorElementType() == VT.getVectorElementType());
      // Widths match, just return the original vector.
      if (SrcVT == VT)
        return Src.getOperand(0);
      // TODO: Use insert_subvector/extract_subvector to change widen/narrow?
    }
    [[fallthrough]];
  }
  case RISCVISD::VMV_S_X_VL: {
    const MVT VT = N->getSimpleValueType(0);
    SDValue Passthru = N->getOperand(0);
    SDValue Scalar = N->getOperand(1);
    SDValue VL = N->getOperand(2);

    if (Scalar.getOpcode() == RISCVISD::VMV_X_S && Passthru.isUndef() &&
        Scalar.getOperand(0).getValueType() == N->getValueType(0))
      return Scalar.getOperand(0);

    // Use M1 or smaller to avoid over constraining register allocation
    const MVT M1VT = getLMUL1VT(VT);
    if (M1VT.bitsLT(VT)) {
      SDValue M1Passthru =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, M1VT, Passthru,
                      DAG.getVectorIdxConstant(0, DL));
      SDValue Result =
          DAG.getNode(N->getOpcode(), DL, M1VT, M1Passthru, Scalar, VL);
      Result = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, VT, Passthru, Result,
                           DAG.getVectorIdxConstant(0, DL));
      return Result;
    }

    // We use a vmv.v.i if possible.  We limit this to LMUL1.  LMUL2 or
    // higher would involve overly constraining the register allocator for
    // no purpose.
    if (ConstantSDNode *Const = dyn_cast<ConstantSDNode>(Scalar);
        Const && !Const->isZero() && isInt<5>(Const->getSExtValue()) &&
        VT.bitsLE(getLMUL1VT(VT)) && Passthru.isUndef())
      return DAG.getNode(RISCVISD::VMV_V_X_VL, DL, VT, Passthru, Scalar, VL);

    break;
  }
  case RISCVISD::VMV_X_S: {
    SDValue Vec = N->getOperand(0);
    MVT VecVT = N->getOperand(0).getSimpleValueType();
    const MVT M1VT = getLMUL1VT(VecVT);
    if (M1VT.bitsLT(VecVT)) {
      Vec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, M1VT, Vec,
                        DAG.getVectorIdxConstant(0, DL));
      return DAG.getNode(RISCVISD::VMV_X_S, DL, N->getSimpleValueType(0), Vec);
    }
    break;
  }
  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntOpNo = N->getOpcode() == ISD::INTRINSIC_WO_CHAIN ? 0 : 1;
    unsigned IntNo = N->getConstantOperandVal(IntOpNo);
    switch (IntNo) {
      // By default we do not combine any intrinsic.
    default:
      return SDValue();
    case Intrinsic::riscv_masked_strided_load: {
      MVT VT = N->getSimpleValueType(0);
      auto *Load = cast<MemIntrinsicSDNode>(N);
      SDValue PassThru = N->getOperand(2);
      SDValue Base = N->getOperand(3);
      SDValue Stride = N->getOperand(4);
      SDValue Mask = N->getOperand(5);

      // If the stride is equal to the element size in bytes,  we can use
      // a masked.load.
      const unsigned ElementSize = VT.getScalarStoreSize();
      if (auto *StrideC = dyn_cast<ConstantSDNode>(Stride);
          StrideC && StrideC->getZExtValue() == ElementSize)
        return DAG.getMaskedLoad(VT, DL, Load->getChain(), Base,
                                 DAG.getUNDEF(XLenVT), Mask, PassThru,
                                 Load->getMemoryVT(), Load->getMemOperand(),
                                 ISD::UNINDEXED, ISD::NON_EXTLOAD);
      return SDValue();
    }
    case Intrinsic::riscv_masked_strided_store: {
      auto *Store = cast<MemIntrinsicSDNode>(N);
      SDValue Value = N->getOperand(2);
      SDValue Base = N->getOperand(3);
      SDValue Stride = N->getOperand(4);
      SDValue Mask = N->getOperand(5);

      // If the stride is equal to the element size in bytes,  we can use
      // a masked.store.
      const unsigned ElementSize = Value.getValueType().getScalarStoreSize();
      if (auto *StrideC = dyn_cast<ConstantSDNode>(Stride);
          StrideC && StrideC->getZExtValue() == ElementSize)
        return DAG.getMaskedStore(Store->getChain(), DL, Value, Base,
                                  DAG.getUNDEF(XLenVT), Mask,
                                  Value.getValueType(), Store->getMemOperand(),
                                  ISD::UNINDEXED, false);
      return SDValue();
    }
    case Intrinsic::riscv_vcpop:
    case Intrinsic::riscv_vcpop_mask:
    case Intrinsic::riscv_vfirst:
    case Intrinsic::riscv_vfirst_mask: {
      SDValue VL = N->getOperand(2);
      if (IntNo == Intrinsic::riscv_vcpop_mask ||
          IntNo == Intrinsic::riscv_vfirst_mask)
        VL = N->getOperand(3);
      if (!isNullConstant(VL))
        return SDValue();
      // If VL is 0, vcpop -> li 0, vfirst -> li -1.
      SDLoc DL(N);
      EVT VT = N->getValueType(0);
      if (IntNo == Intrinsic::riscv_vfirst ||
          IntNo == Intrinsic::riscv_vfirst_mask)
        return DAG.getConstant(-1, DL, VT);
      return DAG.getConstant(0, DL, VT);
    }
    }
  }
  case ISD::BITCAST: {
    assert(Subtarget.useRVVForFixedLengthVectors());
    SDValue N0 = N->getOperand(0);
    EVT VT = N->getValueType(0);
    EVT SrcVT = N0.getValueType();
    // If this is a bitcast between a MVT::v4i1/v2i1/v1i1 and an illegal integer
    // type, widen both sides to avoid a trip through memory.
    if ((SrcVT == MVT::v1i1 || SrcVT == MVT::v2i1 || SrcVT == MVT::v4i1) &&
        VT.isScalarInteger()) {
      unsigned NumConcats = 8 / SrcVT.getVectorNumElements();
      SmallVector<SDValue, 4> Ops(NumConcats, DAG.getUNDEF(SrcVT));
      Ops[0] = N0;
      SDLoc DL(N);
      N0 = DAG.getNode(ISD::CONCAT_VECTORS, DL, MVT::v8i1, Ops);
      N0 = DAG.getBitcast(MVT::i8, N0);
      return DAG.getNode(ISD::TRUNCATE, DL, VT, N0);
    }

    return SDValue();
  }
  }

  return SDValue();
}

bool RISCVTargetLowering::shouldTransformSignedTruncationCheck(
    EVT XVT, unsigned KeptBits) const {
  // For vectors, we don't have a preference..
  if (XVT.isVector())
    return false;

  if (XVT != MVT::i32 && XVT != MVT::i64)
    return false;

  // We can use sext.w for RV64 or an srai 31 on RV32.
  if (KeptBits == 32 || KeptBits == 64)
    return true;

  // With Zbb we can use sext.h/sext.b.
  return Subtarget.hasStdExtZbb() &&
         ((KeptBits == 8 && XVT == MVT::i64 && !Subtarget.is64Bit()) ||
          KeptBits == 16);
}

bool RISCVTargetLowering::isDesirableToCommuteWithShift(
    const SDNode *N, CombineLevel Level) const {
  assert((N->getOpcode() == ISD::SHL || N->getOpcode() == ISD::SRA ||
          N->getOpcode() == ISD::SRL) &&
         "Expected shift op");

  // The following folds are only desirable if `(OP _, c1 << c2)` can be
  // materialised in fewer instructions than `(OP _, c1)`:
  //
  //   (shl (add x, c1), c2) -> (add (shl x, c2), c1 << c2)
  //   (shl (or x, c1), c2) -> (or (shl x, c2), c1 << c2)
  SDValue N0 = N->getOperand(0);
  EVT Ty = N0.getValueType();
  if (Ty.isScalarInteger() &&
      (N0.getOpcode() == ISD::ADD || N0.getOpcode() == ISD::OR)) {
    auto *C1 = dyn_cast<ConstantSDNode>(N0->getOperand(1));
    auto *C2 = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (C1 && C2) {
      const APInt &C1Int = C1->getAPIntValue();
      APInt ShiftedC1Int = C1Int << C2->getAPIntValue();

      // We can materialise `c1 << c2` into an add immediate, so it's "free",
      // and the combine should happen, to potentially allow further combines
      // later.
      if (ShiftedC1Int.getSignificantBits() <= 64 &&
          isLegalAddImmediate(ShiftedC1Int.getSExtValue()))
        return true;

      // We can materialise `c1` in an add immediate, so it's "free", and the
      // combine should be prevented.
      if (C1Int.getSignificantBits() <= 64 &&
          isLegalAddImmediate(C1Int.getSExtValue()))
        return false;

      // Neither constant will fit into an immediate, so find materialisation
      // costs.
      int C1Cost =
          RISCVMatInt::getIntMatCost(C1Int, Ty.getSizeInBits(), Subtarget,
                                     /*CompressionCost*/ true);
      int ShiftedC1Cost = RISCVMatInt::getIntMatCost(
          ShiftedC1Int, Ty.getSizeInBits(), Subtarget,
          /*CompressionCost*/ true);

      // Materialising `c1` is cheaper than materialising `c1 << c2`, so the
      // combine should be prevented.
      if (C1Cost < ShiftedC1Cost)
        return false;
    }
  }
  return true;
}

bool RISCVTargetLowering::targetShrinkDemandedConstant(
    SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
    TargetLoweringOpt &TLO) const {
  // Delay this optimization as late as possible.
  if (!TLO.LegalOps)
    return false;

  EVT VT = Op.getValueType();
  if (VT.isVector())
    return false;

  unsigned Opcode = Op.getOpcode();
  if (Opcode != ISD::AND && Opcode != ISD::OR && Opcode != ISD::XOR)
    return false;

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!C)
    return false;

  const APInt &Mask = C->getAPIntValue();

  // Clear all non-demanded bits initially.
  APInt ShrunkMask = Mask & DemandedBits;

  // Try to make a smaller immediate by setting undemanded bits.

  APInt ExpandedMask = Mask | ~DemandedBits;

  auto IsLegalMask = [ShrunkMask, ExpandedMask](const APInt &Mask) -> bool {
    return ShrunkMask.isSubsetOf(Mask) && Mask.isSubsetOf(ExpandedMask);
  };
  auto UseMask = [Mask, Op, &TLO](const APInt &NewMask) -> bool {
    if (NewMask == Mask)
      return true;
    SDLoc DL(Op);
    SDValue NewC = TLO.DAG.getConstant(NewMask, DL, Op.getValueType());
    SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), DL, Op.getValueType(),
                                    Op.getOperand(0), NewC);
    return TLO.CombineTo(Op, NewOp);
  };

  // If the shrunk mask fits in sign extended 12 bits, let the target
  // independent code apply it.
  if (ShrunkMask.isSignedIntN(12))
    return false;

  // And has a few special cases for zext.
  if (Opcode == ISD::AND) {
    // Preserve (and X, 0xffff), if zext.h exists use zext.h,
    // otherwise use SLLI + SRLI.
    APInt NewMask = APInt(Mask.getBitWidth(), 0xffff);
    if (IsLegalMask(NewMask))
      return UseMask(NewMask);

    // Try to preserve (and X, 0xffffffff), the (zext_inreg X, i32) pattern.
    if (VT == MVT::i64) {
      APInt NewMask = APInt(64, 0xffffffff);
      if (IsLegalMask(NewMask))
        return UseMask(NewMask);
    }
  }

  // For the remaining optimizations, we need to be able to make a negative
  // number through a combination of mask and undemanded bits.
  if (!ExpandedMask.isNegative())
    return false;

  // What is the fewest number of bits we need to represent the negative number.
  unsigned MinSignedBits = ExpandedMask.getSignificantBits();

  // Try to make a 12 bit negative immediate. If that fails try to make a 32
  // bit negative immediate unless the shrunk immediate already fits in 32 bits.
  // If we can't create a simm12, we shouldn't change opaque constants.
  APInt NewMask = ShrunkMask;
  if (MinSignedBits <= 12)
    NewMask.setBitsFrom(11);
  else if (!C->isOpaque() && MinSignedBits <= 32 && !ShrunkMask.isSignedIntN(32))
    NewMask.setBitsFrom(31);
  else
    return false;

  // Check that our new mask is a subset of the demanded mask.
  assert(IsLegalMask(NewMask));
  return UseMask(NewMask);
}

static uint64_t computeGREVOrGORC(uint64_t x, unsigned ShAmt, bool IsGORC) {
  static const uint64_t GREVMasks[] = {
      0x5555555555555555ULL, 0x3333333333333333ULL, 0x0F0F0F0F0F0F0F0FULL,
      0x00FF00FF00FF00FFULL, 0x0000FFFF0000FFFFULL, 0x00000000FFFFFFFFULL};

  for (unsigned Stage = 0; Stage != 6; ++Stage) {
    unsigned Shift = 1 << Stage;
    if (ShAmt & Shift) {
      uint64_t Mask = GREVMasks[Stage];
      uint64_t Res = ((x & Mask) << Shift) | ((x >> Shift) & Mask);
      if (IsGORC)
        Res |= x;
      x = Res;
    }
  }

  return x;
}

void RISCVTargetLowering::computeKnownBitsForTargetNode(const SDValue Op,
                                                        KnownBits &Known,
                                                        const APInt &DemandedElts,
                                                        const SelectionDAG &DAG,
                                                        unsigned Depth) const {
  unsigned BitWidth = Known.getBitWidth();
  unsigned Opc = Op.getOpcode();
  assert((Opc >= ISD::BUILTIN_OP_END ||
          Opc == ISD::INTRINSIC_WO_CHAIN ||
          Opc == ISD::INTRINSIC_W_CHAIN ||
          Opc == ISD::INTRINSIC_VOID) &&
         "Should use MaskedValueIsZero if you don't know whether Op"
         " is a target node!");

  Known.resetAll();
  switch (Opc) {
  default: break;
  case RISCVISD::SELECT_CC: {
    Known = DAG.computeKnownBits(Op.getOperand(4), Depth + 1);
    // If we don't know any bits, early out.
    if (Known.isUnknown())
      break;
    KnownBits Known2 = DAG.computeKnownBits(Op.getOperand(3), Depth + 1);

    // Only known if known in both the LHS and RHS.
    Known = Known.intersectWith(Known2);
    break;
  }
  case RISCVISD::CZERO_EQZ:
  case RISCVISD::CZERO_NEZ:
    Known = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    // Result is either all zero or operand 0. We can propagate zeros, but not
    // ones.
    Known.One.clearAllBits();
    break;
  case RISCVISD::REMUW: {
    KnownBits Known2;
    Known = DAG.computeKnownBits(Op.getOperand(0), DemandedElts, Depth + 1);
    Known2 = DAG.computeKnownBits(Op.getOperand(1), DemandedElts, Depth + 1);
    // We only care about the lower 32 bits.
    Known = KnownBits::urem(Known.trunc(32), Known2.trunc(32));
    // Restore the original width by sign extending.
    Known = Known.sext(BitWidth);
    break;
  }
  case RISCVISD::DIVUW: {
    KnownBits Known2;
    Known = DAG.computeKnownBits(Op.getOperand(0), DemandedElts, Depth + 1);
    Known2 = DAG.computeKnownBits(Op.getOperand(1), DemandedElts, Depth + 1);
    // We only care about the lower 32 bits.
    Known = KnownBits::udiv(Known.trunc(32), Known2.trunc(32));
    // Restore the original width by sign extending.
    Known = Known.sext(BitWidth);
    break;
  }
  case RISCVISD::SLLW: {
    KnownBits Known2;
    Known = DAG.computeKnownBits(Op.getOperand(0), DemandedElts, Depth + 1);
    Known2 = DAG.computeKnownBits(Op.getOperand(1), DemandedElts, Depth + 1);
    Known = KnownBits::shl(Known.trunc(32), Known2.trunc(5).zext(32));
    // Restore the original width by sign extending.
    Known = Known.sext(BitWidth);
    break;
  }
  case RISCVISD::CTZW: {
    KnownBits Known2 = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    unsigned PossibleTZ = Known2.trunc(32).countMaxTrailingZeros();
    unsigned LowBits = llvm::bit_width(PossibleTZ);
    Known.Zero.setBitsFrom(LowBits);
    break;
  }
  case RISCVISD::CLZW: {
    KnownBits Known2 = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    unsigned PossibleLZ = Known2.trunc(32).countMaxLeadingZeros();
    unsigned LowBits = llvm::bit_width(PossibleLZ);
    Known.Zero.setBitsFrom(LowBits);
    break;
  }
  case RISCVISD::BREV8:
  case RISCVISD::ORC_B: {
    // FIXME: This is based on the non-ratified Zbp GREV and GORC where a
    // control value of 7 is equivalent to brev8 and orc.b.
    Known = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    bool IsGORC = Op.getOpcode() == RISCVISD::ORC_B;
    // To compute zeros, we need to invert the value and invert it back after.
    Known.Zero =
        ~computeGREVOrGORC(~Known.Zero.getZExtValue(), 7, IsGORC);
    Known.One = computeGREVOrGORC(Known.One.getZExtValue(), 7, IsGORC);
    break;
  }
  case RISCVISD::READ_VLENB: {
    // We can use the minimum and maximum VLEN values to bound VLENB.  We
    // know VLEN must be a power of two.
    const unsigned MinVLenB = Subtarget.getRealMinVLen() / 8;
    const unsigned MaxVLenB = Subtarget.getRealMaxVLen() / 8;
    assert(MinVLenB > 0 && "READ_VLENB without vector extension enabled?");
    Known.Zero.setLowBits(Log2_32(MinVLenB));
    Known.Zero.setBitsFrom(Log2_32(MaxVLenB)+1);
    if (MaxVLenB == MinVLenB)
      Known.One.setBit(Log2_32(MinVLenB));
    break;
  }
  case RISCVISD::FCLASS: {
    // fclass will only set one of the low 10 bits.
    Known.Zero.setBitsFrom(10);
    break;
  }
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntNo =
        Op.getConstantOperandVal(Opc == ISD::INTRINSIC_WO_CHAIN ? 0 : 1);
    switch (IntNo) {
    default:
      // We can't do anything for most intrinsics.
      break;
    case Intrinsic::riscv_vsetvli:
    case Intrinsic::riscv_vsetvlimax: {
      bool HasAVL = IntNo == Intrinsic::riscv_vsetvli;
      unsigned VSEW = Op.getConstantOperandVal(HasAVL + 1);
      RISCVII::VLMUL VLMUL =
          static_cast<RISCVII::VLMUL>(Op.getConstantOperandVal(HasAVL + 2));
      unsigned SEW = RISCVVType::decodeVSEW(VSEW);
      auto [LMul, Fractional] = RISCVVType::decodeVLMUL(VLMUL);
      uint64_t MaxVL = Subtarget.getRealMaxVLen() / SEW;
      MaxVL = (Fractional) ? MaxVL / LMul : MaxVL * LMul;

      // Result of vsetvli must be not larger than AVL.
      if (HasAVL && isa<ConstantSDNode>(Op.getOperand(1)))
        MaxVL = std::min(MaxVL, Op.getConstantOperandVal(1));

      unsigned KnownZeroFirstBit = Log2_32(MaxVL) + 1;
      if (BitWidth > KnownZeroFirstBit)
        Known.Zero.setBitsFrom(KnownZeroFirstBit);
      break;
    }
    }
    break;
  }
  }
}

unsigned RISCVTargetLowering::ComputeNumSignBitsForTargetNode(
    SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
    unsigned Depth) const {
  switch (Op.getOpcode()) {
  default:
    break;
  case RISCVISD::SELECT_CC: {
    unsigned Tmp =
        DAG.ComputeNumSignBits(Op.getOperand(3), DemandedElts, Depth + 1);
    if (Tmp == 1) return 1;  // Early out.
    unsigned Tmp2 =
        DAG.ComputeNumSignBits(Op.getOperand(4), DemandedElts, Depth + 1);
    return std::min(Tmp, Tmp2);
  }
  case RISCVISD::CZERO_EQZ:
  case RISCVISD::CZERO_NEZ:
    // Output is either all zero or operand 0. We can propagate sign bit count
    // from operand 0.
    return DAG.ComputeNumSignBits(Op.getOperand(0), DemandedElts, Depth + 1);
  case RISCVISD::ABSW: {
    // We expand this at isel to negw+max. The result will have 33 sign bits
    // if the input has at least 33 sign bits.
    unsigned Tmp =
        DAG.ComputeNumSignBits(Op.getOperand(0), DemandedElts, Depth + 1);
    if (Tmp < 33) return 1;
    return 33;
  }
  case RISCVISD::SLLW:
  case RISCVISD::SRAW:
  case RISCVISD::SRLW:
  case RISCVISD::DIVW:
  case RISCVISD::DIVUW:
  case RISCVISD::REMUW:
  case RISCVISD::ROLW:
  case RISCVISD::RORW:
  case RISCVISD::FCVT_W_RV64:
  case RISCVISD::FCVT_WU_RV64:
  case RISCVISD::STRICT_FCVT_W_RV64:
  case RISCVISD::STRICT_FCVT_WU_RV64:
    // TODO: As the result is sign-extended, this is conservatively correct. A
    // more precise answer could be calculated for SRAW depending on known
    // bits in the shift amount.
    return 33;
  case RISCVISD::VMV_X_S: {
    // The number of sign bits of the scalar result is computed by obtaining the
    // element type of the input vector operand, subtracting its width from the
    // XLEN, and then adding one (sign bit within the element type). If the
    // element type is wider than XLen, the least-significant XLEN bits are
    // taken.
    unsigned XLen = Subtarget.getXLen();
    unsigned EltBits = Op.getOperand(0).getScalarValueSizeInBits();
    if (EltBits <= XLen)
      return XLen - EltBits + 1;
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    unsigned IntNo = Op.getConstantOperandVal(1);
    switch (IntNo) {
    default:
      break;
    case Intrinsic::riscv_masked_atomicrmw_xchg_i64:
    case Intrinsic::riscv_masked_atomicrmw_add_i64:
    case Intrinsic::riscv_masked_atomicrmw_sub_i64:
    case Intrinsic::riscv_masked_atomicrmw_nand_i64:
    case Intrinsic::riscv_masked_atomicrmw_max_i64:
    case Intrinsic::riscv_masked_atomicrmw_min_i64:
    case Intrinsic::riscv_masked_atomicrmw_umax_i64:
    case Intrinsic::riscv_masked_atomicrmw_umin_i64:
    case Intrinsic::riscv_masked_cmpxchg_i64:
      // riscv_masked_{atomicrmw_*,cmpxchg} intrinsics represent an emulated
      // narrow atomic operation. These are implemented using atomic
      // operations at the minimum supported atomicrmw/cmpxchg width whose
      // result is then sign extended to XLEN. With +A, the minimum width is
      // 32 for both 64 and 32.
      assert(Subtarget.getXLen() == 64);
      assert(getMinCmpXchgSizeInBits() == 32);
      assert(Subtarget.hasStdExtA());
      return 33;
    }
    break;
  }
  }

  return 1;
}

bool RISCVTargetLowering::canCreateUndefOrPoisonForTargetNode(
    SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
    bool PoisonOnly, bool ConsiderFlags, unsigned Depth) const {

  // TODO: Add more target nodes.
  switch (Op.getOpcode()) {
  case RISCVISD::SELECT_CC:
    // Integer select_cc cannot create poison.
    // TODO: What are the FP poison semantics?
    // TODO: This instruction blocks poison from the unselected operand, can
    // we do anything with that?
    return !Op.getValueType().isInteger();
  }
  return TargetLowering::canCreateUndefOrPoisonForTargetNode(
      Op, DemandedElts, DAG, PoisonOnly, ConsiderFlags, Depth);
}

const Constant *
RISCVTargetLowering::getTargetConstantFromLoad(LoadSDNode *Ld) const {
  assert(Ld && "Unexpected null LoadSDNode");
  if (!ISD::isNormalLoad(Ld))
    return nullptr;

  SDValue Ptr = Ld->getBasePtr();

  // Only constant pools with no offset are supported.
  auto GetSupportedConstantPool = [](SDValue Ptr) -> ConstantPoolSDNode * {
    auto *CNode = dyn_cast<ConstantPoolSDNode>(Ptr);
    if (!CNode || CNode->isMachineConstantPoolEntry() ||
        CNode->getOffset() != 0)
      return nullptr;

    return CNode;
  };

  // Simple case, LLA.
  if (Ptr.getOpcode() == RISCVISD::LLA) {
    auto *CNode = GetSupportedConstantPool(Ptr);
    if (!CNode || CNode->getTargetFlags() != 0)
      return nullptr;

    return CNode->getConstVal();
  }

  // Look for a HI and ADD_LO pair.
  if (Ptr.getOpcode() != RISCVISD::ADD_LO ||
      Ptr.getOperand(0).getOpcode() != RISCVISD::HI)
    return nullptr;

  auto *CNodeLo = GetSupportedConstantPool(Ptr.getOperand(1));
  auto *CNodeHi = GetSupportedConstantPool(Ptr.getOperand(0).getOperand(0));

  if (!CNodeLo || CNodeLo->getTargetFlags() != RISCVII::MO_LO ||
      !CNodeHi || CNodeHi->getTargetFlags() != RISCVII::MO_HI)
    return nullptr;

  if (CNodeLo->getConstVal() != CNodeHi->getConstVal())
    return nullptr;

  return CNodeLo->getConstVal();
}

static MachineBasicBlock *emitReadCounterWidePseudo(MachineInstr &MI,
                                                    MachineBasicBlock *BB) {
  assert(MI.getOpcode() == RISCV::ReadCounterWide && "Unexpected instruction");

  // To read a 64-bit counter CSR on a 32-bit target, we read the two halves.
  // Should the count have wrapped while it was being read, we need to try
  // again.
  // For example:
  // ```
  // read:
  //   csrrs x3, counterh # load high word of counter
  //   csrrs x2, counter # load low word of counter
  //   csrrs x4, counterh # load high word of counter
  //   bne x3, x4, read # check if high word reads match, otherwise try again
  // ```

  MachineFunction &MF = *BB->getParent();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  MachineBasicBlock *LoopMBB = MF.CreateMachineBasicBlock(LLVMBB);
  MF.insert(It, LoopMBB);

  MachineBasicBlock *DoneMBB = MF.CreateMachineBasicBlock(LLVMBB);
  MF.insert(It, DoneMBB);

  // Transfer the remainder of BB and its successor edges to DoneMBB.
  DoneMBB->splice(DoneMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  DoneMBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(LoopMBB);

  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  Register ReadAgainReg = RegInfo.createVirtualRegister(&RISCV::GPRRegClass);
  Register LoReg = MI.getOperand(0).getReg();
  Register HiReg = MI.getOperand(1).getReg();
  int64_t LoCounter = MI.getOperand(2).getImm();
  int64_t HiCounter = MI.getOperand(3).getImm();
  DebugLoc DL = MI.getDebugLoc();

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  BuildMI(LoopMBB, DL, TII->get(RISCV::CSRRS), HiReg)
      .addImm(HiCounter)
      .addReg(RISCV::X0);
  BuildMI(LoopMBB, DL, TII->get(RISCV::CSRRS), LoReg)
      .addImm(LoCounter)
      .addReg(RISCV::X0);
  BuildMI(LoopMBB, DL, TII->get(RISCV::CSRRS), ReadAgainReg)
      .addImm(HiCounter)
      .addReg(RISCV::X0);

  BuildMI(LoopMBB, DL, TII->get(RISCV::BNE))
      .addReg(HiReg)
      .addReg(ReadAgainReg)
      .addMBB(LoopMBB);

  LoopMBB->addSuccessor(LoopMBB);
  LoopMBB->addSuccessor(DoneMBB);

  MI.eraseFromParent();

  return DoneMBB;
}

static MachineBasicBlock *emitSplitF64Pseudo(MachineInstr &MI,
                                             MachineBasicBlock *BB,
                                             const RISCVSubtarget &Subtarget) {
  assert(MI.getOpcode() == RISCV::SplitF64Pseudo && "Unexpected instruction");

  MachineFunction &MF = *BB->getParent();
  DebugLoc DL = MI.getDebugLoc();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();
  Register LoReg = MI.getOperand(0).getReg();
  Register HiReg = MI.getOperand(1).getReg();
  Register SrcReg = MI.getOperand(2).getReg();

  const TargetRegisterClass *SrcRC = &RISCV::FPR64RegClass;
  int FI = MF.getInfo<RISCVMachineFunctionInfo>()->getMoveF64FrameIndex(MF);

  TII.storeRegToStackSlot(*BB, MI, SrcReg, MI.getOperand(2).isKill(), FI, SrcRC,
                          RI, Register());
  MachinePointerInfo MPI = MachinePointerInfo::getFixedStack(MF, FI);
  MachineMemOperand *MMOLo =
      MF.getMachineMemOperand(MPI, MachineMemOperand::MOLoad, 4, Align(8));
  MachineMemOperand *MMOHi = MF.getMachineMemOperand(
      MPI.getWithOffset(4), MachineMemOperand::MOLoad, 4, Align(8));
  BuildMI(*BB, MI, DL, TII.get(RISCV::LW), LoReg)
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMOLo);
  BuildMI(*BB, MI, DL, TII.get(RISCV::LW), HiReg)
      .addFrameIndex(FI)
      .addImm(4)
      .addMemOperand(MMOHi);
  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

static MachineBasicBlock *emitBuildPairF64Pseudo(MachineInstr &MI,
                                                 MachineBasicBlock *BB,
                                                 const RISCVSubtarget &Subtarget) {
  assert(MI.getOpcode() == RISCV::BuildPairF64Pseudo &&
         "Unexpected instruction");

  MachineFunction &MF = *BB->getParent();
  DebugLoc DL = MI.getDebugLoc();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();
  Register DstReg = MI.getOperand(0).getReg();
  Register LoReg = MI.getOperand(1).getReg();
  Register HiReg = MI.getOperand(2).getReg();

  const TargetRegisterClass *DstRC = &RISCV::FPR64RegClass;
  int FI = MF.getInfo<RISCVMachineFunctionInfo>()->getMoveF64FrameIndex(MF);

  MachinePointerInfo MPI = MachinePointerInfo::getFixedStack(MF, FI);
  MachineMemOperand *MMOLo =
      MF.getMachineMemOperand(MPI, MachineMemOperand::MOStore, 4, Align(8));
  MachineMemOperand *MMOHi = MF.getMachineMemOperand(
      MPI.getWithOffset(4), MachineMemOperand::MOStore, 4, Align(8));
  BuildMI(*BB, MI, DL, TII.get(RISCV::SW))
      .addReg(LoReg, getKillRegState(MI.getOperand(1).isKill()))
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMOLo);
  BuildMI(*BB, MI, DL, TII.get(RISCV::SW))
      .addReg(HiReg, getKillRegState(MI.getOperand(2).isKill()))
      .addFrameIndex(FI)
      .addImm(4)
      .addMemOperand(MMOHi);
  TII.loadRegFromStackSlot(*BB, MI, DstReg, FI, DstRC, RI, Register());
  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

static bool isSelectPseudo(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case RISCV::Select_GPR_Using_CC_GPR:
  case RISCV::Select_GPR_Using_CC_Imm:
  case RISCV::Select_FPR16_Using_CC_GPR:
  case RISCV::Select_FPR16INX_Using_CC_GPR:
  case RISCV::Select_FPR32_Using_CC_GPR:
  case RISCV::Select_FPR32INX_Using_CC_GPR:
  case RISCV::Select_FPR64_Using_CC_GPR:
  case RISCV::Select_FPR64INX_Using_CC_GPR:
  case RISCV::Select_FPR64IN32X_Using_CC_GPR:
    return true;
  }
}

static MachineBasicBlock *emitQuietFCMP(MachineInstr &MI, MachineBasicBlock *BB,
                                        unsigned RelOpcode, unsigned EqOpcode,
                                        const RISCVSubtarget &Subtarget) {
  DebugLoc DL = MI.getDebugLoc();
  Register DstReg = MI.getOperand(0).getReg();
  Register Src1Reg = MI.getOperand(1).getReg();
  Register Src2Reg = MI.getOperand(2).getReg();
  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  Register SavedFFlags = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();

  // Save the current FFLAGS.
  BuildMI(*BB, MI, DL, TII.get(RISCV::ReadFFLAGS), SavedFFlags);

  auto MIB = BuildMI(*BB, MI, DL, TII.get(RelOpcode), DstReg)
                 .addReg(Src1Reg)
                 .addReg(Src2Reg);
  if (MI.getFlag(MachineInstr::MIFlag::NoFPExcept))
    MIB->setFlag(MachineInstr::MIFlag::NoFPExcept);

  // Restore the FFLAGS.
  BuildMI(*BB, MI, DL, TII.get(RISCV::WriteFFLAGS))
      .addReg(SavedFFlags, RegState::Kill);

  // Issue a dummy FEQ opcode to raise exception for signaling NaNs.
  auto MIB2 = BuildMI(*BB, MI, DL, TII.get(EqOpcode), RISCV::X0)
                  .addReg(Src1Reg, getKillRegState(MI.getOperand(1).isKill()))
                  .addReg(Src2Reg, getKillRegState(MI.getOperand(2).isKill()));
  if (MI.getFlag(MachineInstr::MIFlag::NoFPExcept))
    MIB2->setFlag(MachineInstr::MIFlag::NoFPExcept);

  // Erase the pseudoinstruction.
  MI.eraseFromParent();
  return BB;
}

static MachineBasicBlock *
EmitLoweredCascadedSelect(MachineInstr &First, MachineInstr &Second,
                          MachineBasicBlock *ThisMBB,
                          const RISCVSubtarget &Subtarget) {
  // Select_FPRX_ (rs1, rs2, imm, rs4, (Select_FPRX_ rs1, rs2, imm, rs4, rs5)
  // Without this, custom-inserter would have generated:
  //
  //   A
  //   | \
  //   |  B
  //   | /
  //   C
  //   | \
  //   |  D
  //   | /
  //   E
  //
  // A: X = ...; Y = ...
  // B: empty
  // C: Z = PHI [X, A], [Y, B]
  // D: empty
  // E: PHI [X, C], [Z, D]
  //
  // If we lower both Select_FPRX_ in a single step, we can instead generate:
  //
  //   A
  //   | \
  //   |  C
  //   | /|
  //   |/ |
  //   |  |
  //   |  D
  //   | /
  //   E
  //
  // A: X = ...; Y = ...
  // D: empty
  // E: PHI [X, A], [X, C], [Y, D]

  const RISCVInstrInfo &TII = *Subtarget.getInstrInfo();
  const DebugLoc &DL = First.getDebugLoc();
  const BasicBlock *LLVM_BB = ThisMBB->getBasicBlock();
  MachineFunction *F = ThisMBB->getParent();
  MachineBasicBlock *FirstMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *SecondMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *SinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineFunction::iterator It = ++ThisMBB->getIterator();
  F->insert(It, FirstMBB);
  F->insert(It, SecondMBB);
  F->insert(It, SinkMBB);

  // Transfer the remainder of ThisMBB and its successor edges to SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), ThisMBB,
                  std::next(MachineBasicBlock::iterator(First)),
                  ThisMBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(ThisMBB);

  // Fallthrough block for ThisMBB.
  ThisMBB->addSuccessor(FirstMBB);
  // Fallthrough block for FirstMBB.
  FirstMBB->addSuccessor(SecondMBB);
  ThisMBB->addSuccessor(SinkMBB);
  FirstMBB->addSuccessor(SinkMBB);
  // This is fallthrough.
  SecondMBB->addSuccessor(SinkMBB);

  auto FirstCC = static_cast<RISCVCC::CondCode>(First.getOperand(3).getImm());
  Register FLHS = First.getOperand(1).getReg();
  Register FRHS = First.getOperand(2).getReg();
  // Insert appropriate branch.
  BuildMI(FirstMBB, DL, TII.getBrCond(FirstCC))
      .addReg(FLHS)
      .addReg(FRHS)
      .addMBB(SinkMBB);

  Register SLHS = Second.getOperand(1).getReg();
  Register SRHS = Second.getOperand(2).getReg();
  Register Op1Reg4 = First.getOperand(4).getReg();
  Register Op1Reg5 = First.getOperand(5).getReg();

  auto SecondCC = static_cast<RISCVCC::CondCode>(Second.getOperand(3).getImm());
  // Insert appropriate branch.
  BuildMI(ThisMBB, DL, TII.getBrCond(SecondCC))
      .addReg(SLHS)
      .addReg(SRHS)
      .addMBB(SinkMBB);

  Register DestReg = Second.getOperand(0).getReg();
  Register Op2Reg4 = Second.getOperand(4).getReg();
  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(RISCV::PHI), DestReg)
      .addReg(Op2Reg4)
      .addMBB(ThisMBB)
      .addReg(Op1Reg4)
      .addMBB(FirstMBB)
      .addReg(Op1Reg5)
      .addMBB(SecondMBB);

  // Now remove the Select_FPRX_s.
  First.eraseFromParent();
  Second.eraseFromParent();
  return SinkMBB;
}

static MachineBasicBlock *emitSelectPseudo(MachineInstr &MI,
                                           MachineBasicBlock *BB,
                                           const RISCVSubtarget &Subtarget) {
  // To "insert" Select_* instructions, we actually have to insert the triangle
  // control-flow pattern.  The incoming instructions know the destination vreg
  // to set, the condition code register to branch on, the true/false values to
  // select between, and the condcode to use to select the appropriate branch.
  //
  // We produce the following control flow:
  //     HeadMBB
  //     |  \
  //     |  IfFalseMBB
  //     | /
  //    TailMBB
  //
  // When we find a sequence of selects we attempt to optimize their emission
  // by sharing the control flow. Currently we only handle cases where we have
  // multiple selects with the exact same condition (same LHS, RHS and CC).
  // The selects may be interleaved with other instructions if the other
  // instructions meet some requirements we deem safe:
  // - They are not pseudo instructions.
  // - They are debug instructions. Otherwise,
  // - They do not have side-effects, do not access memory and their inputs do
  //   not depend on the results of the select pseudo-instructions.
  // The TrueV/FalseV operands of the selects cannot depend on the result of
  // previous selects in the sequence.
  // These conditions could be further relaxed. See the X86 target for a
  // related approach and more information.
  //
  // Select_FPRX_ (rs1, rs2, imm, rs4, (Select_FPRX_ rs1, rs2, imm, rs4, rs5))
  // is checked here and handled by a separate function -
  // EmitLoweredCascadedSelect.

  auto Next = next_nodbg(MI.getIterator(), BB->instr_end());
  if ((MI.getOpcode() != RISCV::Select_GPR_Using_CC_GPR &&
       MI.getOpcode() != RISCV::Select_GPR_Using_CC_Imm) &&
      Next != BB->end() && Next->getOpcode() == MI.getOpcode() &&
      Next->getOperand(5).getReg() == MI.getOperand(0).getReg() &&
      Next->getOperand(5).isKill())
    return EmitLoweredCascadedSelect(MI, *Next, BB, Subtarget);

  Register LHS = MI.getOperand(1).getReg();
  Register RHS;
  if (MI.getOperand(2).isReg())
    RHS = MI.getOperand(2).getReg();
  auto CC = static_cast<RISCVCC::CondCode>(MI.getOperand(3).getImm());

  SmallVector<MachineInstr *, 4> SelectDebugValues;
  SmallSet<Register, 4> SelectDests;
  SelectDests.insert(MI.getOperand(0).getReg());

  MachineInstr *LastSelectPseudo = &MI;
  for (auto E = BB->end(), SequenceMBBI = MachineBasicBlock::iterator(MI);
       SequenceMBBI != E; ++SequenceMBBI) {
    if (SequenceMBBI->isDebugInstr())
      continue;
    if (isSelectPseudo(*SequenceMBBI)) {
      if (SequenceMBBI->getOperand(1).getReg() != LHS ||
          !SequenceMBBI->getOperand(2).isReg() ||
          SequenceMBBI->getOperand(2).getReg() != RHS ||
          SequenceMBBI->getOperand(3).getImm() != CC ||
          SelectDests.count(SequenceMBBI->getOperand(4).getReg()) ||
          SelectDests.count(SequenceMBBI->getOperand(5).getReg()))
        break;
      LastSelectPseudo = &*SequenceMBBI;
      SequenceMBBI->collectDebugValues(SelectDebugValues);
      SelectDests.insert(SequenceMBBI->getOperand(0).getReg());
      continue;
    }
    if (SequenceMBBI->hasUnmodeledSideEffects() ||
        SequenceMBBI->mayLoadOrStore() ||
        SequenceMBBI->usesCustomInsertionHook())
      break;
    if (llvm::any_of(SequenceMBBI->operands(), [&](MachineOperand &MO) {
          return MO.isReg() && MO.isUse() && SelectDests.count(MO.getReg());
        }))
      break;
  }

  const RISCVInstrInfo &TII = *Subtarget.getInstrInfo();
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction::iterator I = ++BB->getIterator();

  MachineBasicBlock *HeadMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *TailMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *IfFalseMBB = F->CreateMachineBasicBlock(LLVM_BB);

  F->insert(I, IfFalseMBB);
  F->insert(I, TailMBB);

  // Set the call frame size on entry to the new basic blocks.
  unsigned CallFrameSize = TII.getCallFrameSizeAt(*LastSelectPseudo);
  IfFalseMBB->setCallFrameSize(CallFrameSize);
  TailMBB->setCallFrameSize(CallFrameSize);

  // Transfer debug instructions associated with the selects to TailMBB.
  for (MachineInstr *DebugInstr : SelectDebugValues) {
    TailMBB->push_back(DebugInstr->removeFromParent());
  }

  // Move all instructions after the sequence to TailMBB.
  TailMBB->splice(TailMBB->end(), HeadMBB,
                  std::next(LastSelectPseudo->getIterator()), HeadMBB->end());
  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block which will contain the Phi nodes for the selects.
  TailMBB->transferSuccessorsAndUpdatePHIs(HeadMBB);
  // Set the successors for HeadMBB.
  HeadMBB->addSuccessor(IfFalseMBB);
  HeadMBB->addSuccessor(TailMBB);

  // Insert appropriate branch.
  if (MI.getOperand(2).isImm())
    BuildMI(HeadMBB, DL, TII.getBrCond(CC, MI.getOperand(2).isImm()))
        .addReg(LHS)
        .addImm(MI.getOperand(2).getImm())
        .addMBB(TailMBB);
  else
    BuildMI(HeadMBB, DL, TII.getBrCond(CC))
        .addReg(LHS)
        .addReg(RHS)
        .addMBB(TailMBB);

  // IfFalseMBB just falls through to TailMBB.
  IfFalseMBB->addSuccessor(TailMBB);

  // Create PHIs for all of the select pseudo-instructions.
  auto SelectMBBI = MI.getIterator();
  auto SelectEnd = std::next(LastSelectPseudo->getIterator());
  auto InsertionPoint = TailMBB->begin();
  while (SelectMBBI != SelectEnd) {
    auto Next = std::next(SelectMBBI);
    if (isSelectPseudo(*SelectMBBI)) {
      // %Result = phi [ %TrueValue, HeadMBB ], [ %FalseValue, IfFalseMBB ]
      BuildMI(*TailMBB, InsertionPoint, SelectMBBI->getDebugLoc(),
              TII.get(RISCV::PHI), SelectMBBI->getOperand(0).getReg())
          .addReg(SelectMBBI->getOperand(4).getReg())
          .addMBB(HeadMBB)
          .addReg(SelectMBBI->getOperand(5).getReg())
          .addMBB(IfFalseMBB);
      SelectMBBI->eraseFromParent();
    }
    SelectMBBI = Next;
  }

  F->getProperties().reset(MachineFunctionProperties::Property::NoPHIs);
  return TailMBB;
}

// Helper to find Masked Pseudo instruction from MC instruction, LMUL and SEW.
static const RISCV::RISCVMaskedPseudoInfo *
lookupMaskedIntrinsic(uint16_t MCOpcode, RISCVII::VLMUL LMul, unsigned SEW) {
  const RISCVVInversePseudosTable::PseudoInfo *Inverse =
      RISCVVInversePseudosTable::getBaseInfo(MCOpcode, LMul, SEW);
  assert(Inverse && "Unexpected LMUL and SEW pair for instruction");
  const RISCV::RISCVMaskedPseudoInfo *Masked =
      RISCV::lookupMaskedIntrinsicByUnmasked(Inverse->Pseudo);
  assert(Masked && "Could not find masked instruction for LMUL and SEW pair");
  return Masked;
}

static MachineBasicBlock *emitVFROUND_NOEXCEPT_MASK(MachineInstr &MI,
                                                    MachineBasicBlock *BB,
                                                    unsigned CVTXOpc) {
  DebugLoc DL = MI.getDebugLoc();

  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();

  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  Register SavedFFLAGS = MRI.createVirtualRegister(&RISCV::GPRRegClass);

  // Save the old value of FFLAGS.
  BuildMI(*BB, MI, DL, TII.get(RISCV::ReadFFLAGS), SavedFFLAGS);

  assert(MI.getNumOperands() == 7);

  // Emit a VFCVT_X_F
  const TargetRegisterInfo *TRI =
      BB->getParent()->getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC = MI.getRegClassConstraint(0, &TII, TRI);
  Register Tmp = MRI.createVirtualRegister(RC);
  BuildMI(*BB, MI, DL, TII.get(CVTXOpc), Tmp)
      .add(MI.getOperand(1))
      .add(MI.getOperand(2))
      .add(MI.getOperand(3))
      .add(MachineOperand::CreateImm(7)) // frm = DYN
      .add(MI.getOperand(4))
      .add(MI.getOperand(5))
      .add(MI.getOperand(6))
      .add(MachineOperand::CreateReg(RISCV::FRM,
                                     /*IsDef*/ false,
                                     /*IsImp*/ true));

  // Emit a VFCVT_F_X
  RISCVII::VLMUL LMul = RISCVII::getLMul(MI.getDesc().TSFlags);
  unsigned Log2SEW = MI.getOperand(RISCVII::getSEWOpNum(MI.getDesc())).getImm();
  // There is no E8 variant for VFCVT_F_X.
  assert(Log2SEW >= 4);
  unsigned CVTFOpc =
      lookupMaskedIntrinsic(RISCV::VFCVT_F_X_V, LMul, 1 << Log2SEW)
          ->MaskedPseudo;

  BuildMI(*BB, MI, DL, TII.get(CVTFOpc))
      .add(MI.getOperand(0))
      .add(MI.getOperand(1))
      .addReg(Tmp)
      .add(MI.getOperand(3))
      .add(MachineOperand::CreateImm(7)) // frm = DYN
      .add(MI.getOperand(4))
      .add(MI.getOperand(5))
      .add(MI.getOperand(6))
      .add(MachineOperand::CreateReg(RISCV::FRM,
                                     /*IsDef*/ false,
                                     /*IsImp*/ true));

  // Restore FFLAGS.
  BuildMI(*BB, MI, DL, TII.get(RISCV::WriteFFLAGS))
      .addReg(SavedFFLAGS, RegState::Kill);

  // Erase the pseudoinstruction.
  MI.eraseFromParent();
  return BB;
}

static MachineBasicBlock *emitFROUND(MachineInstr &MI, MachineBasicBlock *MBB,
                                     const RISCVSubtarget &Subtarget) {
  unsigned CmpOpc, F2IOpc, I2FOpc, FSGNJOpc, FSGNJXOpc;
  const TargetRegisterClass *RC;
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode");
  case RISCV::PseudoFROUND_H:
    CmpOpc = RISCV::FLT_H;
    F2IOpc = RISCV::FCVT_W_H;
    I2FOpc = RISCV::FCVT_H_W;
    FSGNJOpc = RISCV::FSGNJ_H;
    FSGNJXOpc = RISCV::FSGNJX_H;
    RC = &RISCV::FPR16RegClass;
    break;
  case RISCV::PseudoFROUND_H_INX:
    CmpOpc = RISCV::FLT_H_INX;
    F2IOpc = RISCV::FCVT_W_H_INX;
    I2FOpc = RISCV::FCVT_H_W_INX;
    FSGNJOpc = RISCV::FSGNJ_H_INX;
    FSGNJXOpc = RISCV::FSGNJX_H_INX;
    RC = &RISCV::GPRF16RegClass;
    break;
  case RISCV::PseudoFROUND_S:
    CmpOpc = RISCV::FLT_S;
    F2IOpc = RISCV::FCVT_W_S;
    I2FOpc = RISCV::FCVT_S_W;
    FSGNJOpc = RISCV::FSGNJ_S;
    FSGNJXOpc = RISCV::FSGNJX_S;
    RC = &RISCV::FPR32RegClass;
    break;
  case RISCV::PseudoFROUND_S_INX:
    CmpOpc = RISCV::FLT_S_INX;
    F2IOpc = RISCV::FCVT_W_S_INX;
    I2FOpc = RISCV::FCVT_S_W_INX;
    FSGNJOpc = RISCV::FSGNJ_S_INX;
    FSGNJXOpc = RISCV::FSGNJX_S_INX;
    RC = &RISCV::GPRF32RegClass;
    break;
  case RISCV::PseudoFROUND_D:
    assert(Subtarget.is64Bit() && "Expected 64-bit GPR.");
    CmpOpc = RISCV::FLT_D;
    F2IOpc = RISCV::FCVT_L_D;
    I2FOpc = RISCV::FCVT_D_L;
    FSGNJOpc = RISCV::FSGNJ_D;
    FSGNJXOpc = RISCV::FSGNJX_D;
    RC = &RISCV::FPR64RegClass;
    break;
  case RISCV::PseudoFROUND_D_INX:
    assert(Subtarget.is64Bit() && "Expected 64-bit GPR.");
    CmpOpc = RISCV::FLT_D_INX;
    F2IOpc = RISCV::FCVT_L_D_INX;
    I2FOpc = RISCV::FCVT_D_L_INX;
    FSGNJOpc = RISCV::FSGNJ_D_INX;
    FSGNJXOpc = RISCV::FSGNJX_D_INX;
    RC = &RISCV::GPRRegClass;
    break;
  }

  const BasicBlock *BB = MBB->getBasicBlock();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction::iterator I = ++MBB->getIterator();

  MachineFunction *F = MBB->getParent();
  MachineBasicBlock *CvtMBB = F->CreateMachineBasicBlock(BB);
  MachineBasicBlock *DoneMBB = F->CreateMachineBasicBlock(BB);

  F->insert(I, CvtMBB);
  F->insert(I, DoneMBB);
  // Move all instructions after the sequence to DoneMBB.
  DoneMBB->splice(DoneMBB->end(), MBB, MachineBasicBlock::iterator(MI),
                  MBB->end());
  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block which will contain the Phi nodes for the selects.
  DoneMBB->transferSuccessorsAndUpdatePHIs(MBB);
  // Set the successors for MBB.
  MBB->addSuccessor(CvtMBB);
  MBB->addSuccessor(DoneMBB);

  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  Register MaxReg = MI.getOperand(2).getReg();
  int64_t FRM = MI.getOperand(3).getImm();

  const RISCVInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();

  Register FabsReg = MRI.createVirtualRegister(RC);
  BuildMI(MBB, DL, TII.get(FSGNJXOpc), FabsReg).addReg(SrcReg).addReg(SrcReg);

  // Compare the FP value to the max value.
  Register CmpReg = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  auto MIB =
      BuildMI(MBB, DL, TII.get(CmpOpc), CmpReg).addReg(FabsReg).addReg(MaxReg);
  if (MI.getFlag(MachineInstr::MIFlag::NoFPExcept))
    MIB->setFlag(MachineInstr::MIFlag::NoFPExcept);

  // Insert branch.
  BuildMI(MBB, DL, TII.get(RISCV::BEQ))
      .addReg(CmpReg)
      .addReg(RISCV::X0)
      .addMBB(DoneMBB);

  CvtMBB->addSuccessor(DoneMBB);

  // Convert to integer.
  Register F2IReg = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  MIB = BuildMI(CvtMBB, DL, TII.get(F2IOpc), F2IReg).addReg(SrcReg).addImm(FRM);
  if (MI.getFlag(MachineInstr::MIFlag::NoFPExcept))
    MIB->setFlag(MachineInstr::MIFlag::NoFPExcept);

  // Convert back to FP.
  Register I2FReg = MRI.createVirtualRegister(RC);
  MIB = BuildMI(CvtMBB, DL, TII.get(I2FOpc), I2FReg).addReg(F2IReg).addImm(FRM);
  if (MI.getFlag(MachineInstr::MIFlag::NoFPExcept))
    MIB->setFlag(MachineInstr::MIFlag::NoFPExcept);

  // Restore the sign bit.
  Register CvtReg = MRI.createVirtualRegister(RC);
  BuildMI(CvtMBB, DL, TII.get(FSGNJOpc), CvtReg).addReg(I2FReg).addReg(SrcReg);

  // Merge the results.
  BuildMI(*DoneMBB, DoneMBB->begin(), DL, TII.get(RISCV::PHI), DstReg)
      .addReg(SrcReg)
      .addMBB(MBB)
      .addReg(CvtReg)
      .addMBB(CvtMBB);

  MI.eraseFromParent();
  return DoneMBB;
}

MachineBasicBlock *
RISCVTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected instr type to insert");
  case RISCV::ReadCounterWide:
    assert(!Subtarget.is64Bit() &&
           "ReadCounterWide is only to be used on riscv32");
    return emitReadCounterWidePseudo(MI, BB);
  case RISCV::Select_GPR_Using_CC_GPR:
  case RISCV::Select_GPR_Using_CC_Imm:
  case RISCV::Select_FPR16_Using_CC_GPR:
  case RISCV::Select_FPR16INX_Using_CC_GPR:
  case RISCV::Select_FPR32_Using_CC_GPR:
  case RISCV::Select_FPR32INX_Using_CC_GPR:
  case RISCV::Select_FPR64_Using_CC_GPR:
  case RISCV::Select_FPR64INX_Using_CC_GPR:
  case RISCV::Select_FPR64IN32X_Using_CC_GPR:
    return emitSelectPseudo(MI, BB, Subtarget);
  case RISCV::BuildPairF64Pseudo:
    return emitBuildPairF64Pseudo(MI, BB, Subtarget);
  case RISCV::SplitF64Pseudo:
    return emitSplitF64Pseudo(MI, BB, Subtarget);
  case RISCV::PseudoQuietFLE_H:
    return emitQuietFCMP(MI, BB, RISCV::FLE_H, RISCV::FEQ_H, Subtarget);
  case RISCV::PseudoQuietFLE_H_INX:
    return emitQuietFCMP(MI, BB, RISCV::FLE_H_INX, RISCV::FEQ_H_INX, Subtarget);
  case RISCV::PseudoQuietFLT_H:
    return emitQuietFCMP(MI, BB, RISCV::FLT_H, RISCV::FEQ_H, Subtarget);
  case RISCV::PseudoQuietFLT_H_INX:
    return emitQuietFCMP(MI, BB, RISCV::FLT_H_INX, RISCV::FEQ_H_INX, Subtarget);
  case RISCV::PseudoQuietFLE_S:
    return emitQuietFCMP(MI, BB, RISCV::FLE_S, RISCV::FEQ_S, Subtarget);
  case RISCV::PseudoQuietFLE_S_INX:
    return emitQuietFCMP(MI, BB, RISCV::FLE_S_INX, RISCV::FEQ_S_INX, Subtarget);
  case RISCV::PseudoQuietFLT_S:
    return emitQuietFCMP(MI, BB, RISCV::FLT_S, RISCV::FEQ_S, Subtarget);
  case RISCV::PseudoQuietFLT_S_INX:
    return emitQuietFCMP(MI, BB, RISCV::FLT_S_INX, RISCV::FEQ_S_INX, Subtarget);
  case RISCV::PseudoQuietFLE_D:
    return emitQuietFCMP(MI, BB, RISCV::FLE_D, RISCV::FEQ_D, Subtarget);
  case RISCV::PseudoQuietFLE_D_INX:
    return emitQuietFCMP(MI, BB, RISCV::FLE_D_INX, RISCV::FEQ_D_INX, Subtarget);
  case RISCV::PseudoQuietFLE_D_IN32X:
    return emitQuietFCMP(MI, BB, RISCV::FLE_D_IN32X, RISCV::FEQ_D_IN32X,
                         Subtarget);
  case RISCV::PseudoQuietFLT_D:
    return emitQuietFCMP(MI, BB, RISCV::FLT_D, RISCV::FEQ_D, Subtarget);
  case RISCV::PseudoQuietFLT_D_INX:
    return emitQuietFCMP(MI, BB, RISCV::FLT_D_INX, RISCV::FEQ_D_INX, Subtarget);
  case RISCV::PseudoQuietFLT_D_IN32X:
    return emitQuietFCMP(MI, BB, RISCV::FLT_D_IN32X, RISCV::FEQ_D_IN32X,
                         Subtarget);

  case RISCV::PseudoVFROUND_NOEXCEPT_V_M1_MASK:
    return emitVFROUND_NOEXCEPT_MASK(MI, BB, RISCV::PseudoVFCVT_X_F_V_M1_MASK);
  case RISCV::PseudoVFROUND_NOEXCEPT_V_M2_MASK:
    return emitVFROUND_NOEXCEPT_MASK(MI, BB, RISCV::PseudoVFCVT_X_F_V_M2_MASK);
  case RISCV::PseudoVFROUND_NOEXCEPT_V_M4_MASK:
    return emitVFROUND_NOEXCEPT_MASK(MI, BB, RISCV::PseudoVFCVT_X_F_V_M4_MASK);
  case RISCV::PseudoVFROUND_NOEXCEPT_V_M8_MASK:
    return emitVFROUND_NOEXCEPT_MASK(MI, BB, RISCV::PseudoVFCVT_X_F_V_M8_MASK);
  case RISCV::PseudoVFROUND_NOEXCEPT_V_MF2_MASK:
    return emitVFROUND_NOEXCEPT_MASK(MI, BB, RISCV::PseudoVFCVT_X_F_V_MF2_MASK);
  case RISCV::PseudoVFROUND_NOEXCEPT_V_MF4_MASK:
    return emitVFROUND_NOEXCEPT_MASK(MI, BB, RISCV::PseudoVFCVT_X_F_V_MF4_MASK);
  case RISCV::PseudoFROUND_H:
  case RISCV::PseudoFROUND_H_INX:
  case RISCV::PseudoFROUND_S:
  case RISCV::PseudoFROUND_S_INX:
  case RISCV::PseudoFROUND_D:
  case RISCV::PseudoFROUND_D_INX:
  case RISCV::PseudoFROUND_D_IN32X:
    return emitFROUND(MI, BB, Subtarget);
  case TargetOpcode::STATEPOINT:
    // STATEPOINT is a pseudo instruction which has no implicit defs/uses
    // while jal call instruction (where statepoint will be lowered at the end)
    // has implicit def. This def is early-clobber as it will be set at
    // the moment of the call and earlier than any use is read.
    // Add this implicit dead def here as a workaround.
    MI.addOperand(*MI.getMF(),
                  MachineOperand::CreateReg(
                      RISCV::X1, /*isDef*/ true,
                      /*isImp*/ true, /*isKill*/ false, /*isDead*/ true,
                      /*isUndef*/ false, /*isEarlyClobber*/ true));
    [[fallthrough]];
  case TargetOpcode::STACKMAP:
  case TargetOpcode::PATCHPOINT:
    if (!Subtarget.is64Bit())
      report_fatal_error("STACKMAP, PATCHPOINT and STATEPOINT are only "
                         "supported on 64-bit targets");
    return emitPatchPoint(MI, BB);
  }
}

void RISCVTargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                        SDNode *Node) const {
  // Add FRM dependency to any instructions with dynamic rounding mode.
  int Idx = RISCV::getNamedOperandIdx(MI.getOpcode(), RISCV::OpName::frm);
  if (Idx < 0) {
    // Vector pseudos have FRM index indicated by TSFlags.
    Idx = RISCVII::getFRMOpNum(MI.getDesc());
    if (Idx < 0)
      return;
  }
  if (MI.getOperand(Idx).getImm() != RISCVFPRndMode::DYN)
    return;
  // If the instruction already reads FRM, don't add another read.
  if (MI.readsRegister(RISCV::FRM, /*TRI=*/nullptr))
    return;
  MI.addOperand(
      MachineOperand::CreateReg(RISCV::FRM, /*isDef*/ false, /*isImp*/ true));
}

// Calling Convention Implementation.
// The expectations for frontend ABI lowering vary from target to target.
// Ideally, an LLVM frontend would be able to avoid worrying about many ABI
// details, but this is a longer term goal. For now, we simply try to keep the
// role of the frontend as simple and well-defined as possible. The rules can
// be summarised as:
// * Never split up large scalar arguments. We handle them here.
// * If a hardfloat calling convention is being used, and the struct may be
// passed in a pair of registers (fp+fp, int+fp), and both registers are
// available, then pass as two separate arguments. If either the GPRs or FPRs
// are exhausted, then pass according to the rule below.
// * If a struct could never be passed in registers or directly in a stack
// slot (as it is larger than 2*XLEN and the floating point rules don't
// apply), then pass it using a pointer with the byval attribute.
// * If a struct is less than 2*XLEN, then coerce to either a two-element
// word-sized array or a 2*XLEN scalar (depending on alignment).
// * The frontend can determine whether a struct is returned by reference or
// not based on its size and fields. If it will be returned by reference, the
// frontend must modify the prototype so a pointer with the sret annotation is
// passed as the first argument. This is not necessary for large scalar
// returns.
// * Struct return values and varargs should be coerced to structs containing
// register-size fields in the same situations they would be for fixed
// arguments.

static const MCPhysReg ArgFPR16s[] = {
  RISCV::F10_H, RISCV::F11_H, RISCV::F12_H, RISCV::F13_H,
  RISCV::F14_H, RISCV::F15_H, RISCV::F16_H, RISCV::F17_H
};
static const MCPhysReg ArgFPR32s[] = {
  RISCV::F10_F, RISCV::F11_F, RISCV::F12_F, RISCV::F13_F,
  RISCV::F14_F, RISCV::F15_F, RISCV::F16_F, RISCV::F17_F
};
static const MCPhysReg ArgFPR64s[] = {
  RISCV::F10_D, RISCV::F11_D, RISCV::F12_D, RISCV::F13_D,
  RISCV::F14_D, RISCV::F15_D, RISCV::F16_D, RISCV::F17_D
};
// This is an interim calling convention and it may be changed in the future.
static const MCPhysReg ArgVRs[] = {
    RISCV::V8,  RISCV::V9,  RISCV::V10, RISCV::V11, RISCV::V12, RISCV::V13,
    RISCV::V14, RISCV::V15, RISCV::V16, RISCV::V17, RISCV::V18, RISCV::V19,
    RISCV::V20, RISCV::V21, RISCV::V22, RISCV::V23};
static const MCPhysReg ArgVRM2s[] = {RISCV::V8M2,  RISCV::V10M2, RISCV::V12M2,
                                     RISCV::V14M2, RISCV::V16M2, RISCV::V18M2,
                                     RISCV::V20M2, RISCV::V22M2};
static const MCPhysReg ArgVRM4s[] = {RISCV::V8M4, RISCV::V12M4, RISCV::V16M4,
                                     RISCV::V20M4};
static const MCPhysReg ArgVRM8s[] = {RISCV::V8M8, RISCV::V16M8};

ArrayRef<MCPhysReg> RISCV::getArgGPRs(const RISCVABI::ABI ABI) {
  // The GPRs used for passing arguments in the ILP32* and LP64* ABIs, except
  // the ILP32E ABI.
  static const MCPhysReg ArgIGPRs[] = {RISCV::X10, RISCV::X11, RISCV::X12,
                                       RISCV::X13, RISCV::X14, RISCV::X15,
                                       RISCV::X16, RISCV::X17};
  // The GPRs used for passing arguments in the ILP32E/ILP64E ABI.
  static const MCPhysReg ArgEGPRs[] = {RISCV::X10, RISCV::X11, RISCV::X12,
                                       RISCV::X13, RISCV::X14, RISCV::X15};

  if (ABI == RISCVABI::ABI_ILP32E || ABI == RISCVABI::ABI_LP64E)
    return ArrayRef(ArgEGPRs);

  return ArrayRef(ArgIGPRs);
}

static ArrayRef<MCPhysReg> getFastCCArgGPRs(const RISCVABI::ABI ABI) {
  // The GPRs used for passing arguments in the FastCC, X5 and X6 might be used
  // for save-restore libcall, so we don't use them.
  // Don't use X7 for fastcc, since Zicfilp uses X7 as the label register.
  static const MCPhysReg FastCCIGPRs[] = {
      RISCV::X10, RISCV::X11, RISCV::X12, RISCV::X13, RISCV::X14, RISCV::X15,
      RISCV::X16, RISCV::X17, RISCV::X28, RISCV::X29, RISCV::X30, RISCV::X31};

  // The GPRs used for passing arguments in the FastCC when using ILP32E/ILP64E.
  static const MCPhysReg FastCCEGPRs[] = {RISCV::X10, RISCV::X11, RISCV::X12,
                                          RISCV::X13, RISCV::X14, RISCV::X15};

  if (ABI == RISCVABI::ABI_ILP32E || ABI == RISCVABI::ABI_LP64E)
    return ArrayRef(FastCCEGPRs);

  return ArrayRef(FastCCIGPRs);
}

// Pass a 2*XLEN argument that has been split into two XLEN values through
// registers or the stack as necessary.
static bool CC_RISCVAssign2XLen(unsigned XLen, CCState &State, CCValAssign VA1,
                                ISD::ArgFlagsTy ArgFlags1, unsigned ValNo2,
                                MVT ValVT2, MVT LocVT2,
                                ISD::ArgFlagsTy ArgFlags2, bool EABI) {
  unsigned XLenInBytes = XLen / 8;
  const RISCVSubtarget &STI =
      State.getMachineFunction().getSubtarget<RISCVSubtarget>();
  ArrayRef<MCPhysReg> ArgGPRs = RISCV::getArgGPRs(STI.getTargetABI());

  if (Register Reg = State.AllocateReg(ArgGPRs)) {
    // At least one half can be passed via register.
    State.addLoc(CCValAssign::getReg(VA1.getValNo(), VA1.getValVT(), Reg,
                                     VA1.getLocVT(), CCValAssign::Full));
  } else {
    // Both halves must be passed on the stack, with proper alignment.
    // TODO: To be compatible with GCC's behaviors, we force them to have 4-byte
    // alignment. This behavior may be changed when RV32E/ILP32E is ratified.
    Align StackAlign(XLenInBytes);
    if (!EABI || XLen != 32)
      StackAlign = std::max(StackAlign, ArgFlags1.getNonZeroOrigAlign());
    State.addLoc(
        CCValAssign::getMem(VA1.getValNo(), VA1.getValVT(),
                            State.AllocateStack(XLenInBytes, StackAlign),
                            VA1.getLocVT(), CCValAssign::Full));
    State.addLoc(CCValAssign::getMem(
        ValNo2, ValVT2, State.AllocateStack(XLenInBytes, Align(XLenInBytes)),
        LocVT2, CCValAssign::Full));
    return false;
  }

  if (Register Reg = State.AllocateReg(ArgGPRs)) {
    // The second half can also be passed via register.
    State.addLoc(
        CCValAssign::getReg(ValNo2, ValVT2, Reg, LocVT2, CCValAssign::Full));
  } else {
    // The second half is passed via the stack, without additional alignment.
    State.addLoc(CCValAssign::getMem(
        ValNo2, ValVT2, State.AllocateStack(XLenInBytes, Align(XLenInBytes)),
        LocVT2, CCValAssign::Full));
  }

  return false;
}

// Implements the RISC-V calling convention. Returns true upon failure.
bool RISCV::CC_RISCV(const DataLayout &DL, RISCVABI::ABI ABI, unsigned ValNo,
                     MVT ValVT, MVT LocVT, CCValAssign::LocInfo LocInfo,
                     ISD::ArgFlagsTy ArgFlags, CCState &State, bool IsFixed,
                     bool IsRet, Type *OrigTy, const RISCVTargetLowering &TLI,
                     RVVArgDispatcher &RVVDispatcher) {
  unsigned XLen = DL.getLargestLegalIntTypeSizeInBits();
  assert(XLen == 32 || XLen == 64);
  MVT XLenVT = XLen == 32 ? MVT::i32 : MVT::i64;

  // Static chain parameter must not be passed in normal argument registers,
  // so we assign t2 for it as done in GCC's __builtin_call_with_static_chain
  if (ArgFlags.isNest()) {
    if (unsigned Reg = State.AllocateReg(RISCV::X7)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  // Any return value split in to more than two values can't be returned
  // directly. Vectors are returned via the available vector registers.
  if (!LocVT.isVector() && IsRet && ValNo > 1)
    return true;

  // UseGPRForF16_F32 if targeting one of the soft-float ABIs, if passing a
  // variadic argument, or if no F16/F32 argument registers are available.
  bool UseGPRForF16_F32 = true;
  // UseGPRForF64 if targeting soft-float ABIs or an FLEN=32 ABI, if passing a
  // variadic argument, or if no F64 argument registers are available.
  bool UseGPRForF64 = true;

  switch (ABI) {
  default:
    llvm_unreachable("Unexpected ABI");
  case RISCVABI::ABI_ILP32:
  case RISCVABI::ABI_ILP32E:
  case RISCVABI::ABI_LP64:
  case RISCVABI::ABI_LP64E:
    break;
  case RISCVABI::ABI_ILP32F:
  case RISCVABI::ABI_LP64F:
    UseGPRForF16_F32 = !IsFixed;
    break;
  case RISCVABI::ABI_ILP32D:
  case RISCVABI::ABI_LP64D:
    UseGPRForF16_F32 = !IsFixed;
    UseGPRForF64 = !IsFixed;
    break;
  }

  // FPR16, FPR32, and FPR64 alias each other.
  if (State.getFirstUnallocated(ArgFPR32s) == std::size(ArgFPR32s)) {
    UseGPRForF16_F32 = true;
    UseGPRForF64 = true;
  }

  // From this point on, rely on UseGPRForF16_F32, UseGPRForF64 and
  // similar local variables rather than directly checking against the target
  // ABI.

  if (UseGPRForF16_F32 &&
      (ValVT == MVT::f16 || ValVT == MVT::bf16 || ValVT == MVT::f32)) {
    LocVT = XLenVT;
    LocInfo = CCValAssign::BCvt;
  } else if (UseGPRForF64 && XLen == 64 && ValVT == MVT::f64) {
    LocVT = MVT::i64;
    LocInfo = CCValAssign::BCvt;
  }

  ArrayRef<MCPhysReg> ArgGPRs = RISCV::getArgGPRs(ABI);

  // If this is a variadic argument, the RISC-V calling convention requires
  // that it is assigned an 'even' or 'aligned' register if it has 8-byte
  // alignment (RV32) or 16-byte alignment (RV64). An aligned register should
  // be used regardless of whether the original argument was split during
  // legalisation or not. The argument will not be passed by registers if the
  // original type is larger than 2*XLEN, so the register alignment rule does
  // not apply.
  // TODO: To be compatible with GCC's behaviors, we don't align registers
  // currently if we are using ILP32E calling convention. This behavior may be
  // changed when RV32E/ILP32E is ratified.
  unsigned TwoXLenInBytes = (2 * XLen) / 8;
  if (!IsFixed && ArgFlags.getNonZeroOrigAlign() == TwoXLenInBytes &&
      DL.getTypeAllocSize(OrigTy) == TwoXLenInBytes &&
      ABI != RISCVABI::ABI_ILP32E) {
    unsigned RegIdx = State.getFirstUnallocated(ArgGPRs);
    // Skip 'odd' register if necessary.
    if (RegIdx != std::size(ArgGPRs) && RegIdx % 2 == 1)
      State.AllocateReg(ArgGPRs);
  }

  SmallVectorImpl<CCValAssign> &PendingLocs = State.getPendingLocs();
  SmallVectorImpl<ISD::ArgFlagsTy> &PendingArgFlags =
      State.getPendingArgFlags();

  assert(PendingLocs.size() == PendingArgFlags.size() &&
         "PendingLocs and PendingArgFlags out of sync");

  // Handle passing f64 on RV32D with a soft float ABI or when floating point
  // registers are exhausted.
  if (UseGPRForF64 && XLen == 32 && ValVT == MVT::f64) {
    assert(PendingLocs.empty() && "Can't lower f64 if it is split");
    // Depending on available argument GPRS, f64 may be passed in a pair of
    // GPRs, split between a GPR and the stack, or passed completely on the
    // stack. LowerCall/LowerFormalArguments/LowerReturn must recognise these
    // cases.
    Register Reg = State.AllocateReg(ArgGPRs);
    if (!Reg) {
      unsigned StackOffset = State.AllocateStack(8, Align(8));
      State.addLoc(
          CCValAssign::getMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
      return false;
    }
    LocVT = MVT::i32;
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    Register HiReg = State.AllocateReg(ArgGPRs);
    if (HiReg) {
      State.addLoc(
          CCValAssign::getCustomReg(ValNo, ValVT, HiReg, LocVT, LocInfo));
    } else {
      unsigned StackOffset = State.AllocateStack(4, Align(4));
      State.addLoc(
          CCValAssign::getCustomMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
    }
    return false;
  }

  // Fixed-length vectors are located in the corresponding scalable-vector
  // container types.
  if (ValVT.isFixedLengthVector())
    LocVT = TLI.getContainerForFixedLengthVector(LocVT);

  // Split arguments might be passed indirectly, so keep track of the pending
  // values. Split vectors are passed via a mix of registers and indirectly, so
  // treat them as we would any other argument.
  if (ValVT.isScalarInteger() && (ArgFlags.isSplit() || !PendingLocs.empty())) {
    LocVT = XLenVT;
    LocInfo = CCValAssign::Indirect;
    PendingLocs.push_back(
        CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));
    PendingArgFlags.push_back(ArgFlags);
    if (!ArgFlags.isSplitEnd()) {
      return false;
    }
  }

  // If the split argument only had two elements, it should be passed directly
  // in registers or on the stack.
  if (ValVT.isScalarInteger() && ArgFlags.isSplitEnd() &&
      PendingLocs.size() <= 2) {
    assert(PendingLocs.size() == 2 && "Unexpected PendingLocs.size()");
    // Apply the normal calling convention rules to the first half of the
    // split argument.
    CCValAssign VA = PendingLocs[0];
    ISD::ArgFlagsTy AF = PendingArgFlags[0];
    PendingLocs.clear();
    PendingArgFlags.clear();
    return CC_RISCVAssign2XLen(
        XLen, State, VA, AF, ValNo, ValVT, LocVT, ArgFlags,
        ABI == RISCVABI::ABI_ILP32E || ABI == RISCVABI::ABI_LP64E);
  }

  // Allocate to a register if possible, or else a stack slot.
  Register Reg;
  unsigned StoreSizeBytes = XLen / 8;
  Align StackAlign = Align(XLen / 8);

  if ((ValVT == MVT::f16 || ValVT == MVT::bf16) && !UseGPRForF16_F32)
    Reg = State.AllocateReg(ArgFPR16s);
  else if (ValVT == MVT::f32 && !UseGPRForF16_F32)
    Reg = State.AllocateReg(ArgFPR32s);
  else if (ValVT == MVT::f64 && !UseGPRForF64)
    Reg = State.AllocateReg(ArgFPR64s);
  else if (ValVT.isVector()) {
    Reg = RVVDispatcher.getNextPhysReg();
    if (!Reg) {
      // For return values, the vector must be passed fully via registers or
      // via the stack.
      // FIXME: The proposed vector ABI only mandates v8-v15 for return values,
      // but we're using all of them.
      if (IsRet)
        return true;
      // Try using a GPR to pass the address
      if ((Reg = State.AllocateReg(ArgGPRs))) {
        LocVT = XLenVT;
        LocInfo = CCValAssign::Indirect;
      } else if (ValVT.isScalableVector()) {
        LocVT = XLenVT;
        LocInfo = CCValAssign::Indirect;
      } else {
        // Pass fixed-length vectors on the stack.
        LocVT = ValVT;
        StoreSizeBytes = ValVT.getStoreSize();
        // Align vectors to their element sizes, being careful for vXi1
        // vectors.
        StackAlign = MaybeAlign(ValVT.getScalarSizeInBits() / 8).valueOrOne();
      }
    }
  } else {
    Reg = State.AllocateReg(ArgGPRs);
  }

  unsigned StackOffset =
      Reg ? 0 : State.AllocateStack(StoreSizeBytes, StackAlign);

  // If we reach this point and PendingLocs is non-empty, we must be at the
  // end of a split argument that must be passed indirectly.
  if (!PendingLocs.empty()) {
    assert(ArgFlags.isSplitEnd() && "Expected ArgFlags.isSplitEnd()");
    assert(PendingLocs.size() > 2 && "Unexpected PendingLocs.size()");

    for (auto &It : PendingLocs) {
      if (Reg)
        It.convertToReg(Reg);
      else
        It.convertToMem(StackOffset);
      State.addLoc(It);
    }
    PendingLocs.clear();
    PendingArgFlags.clear();
    return false;
  }

  assert((!UseGPRForF16_F32 || !UseGPRForF64 || LocVT == XLenVT ||
          (TLI.getSubtarget().hasVInstructions() && ValVT.isVector())) &&
         "Expected an XLenVT or vector types at this stage");

  if (Reg) {
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return false;
  }

  // When a scalar floating-point value is passed on the stack, no
  // bit-conversion is needed.
  if (ValVT.isFloatingPoint() && LocInfo != CCValAssign::Indirect) {
    assert(!ValVT.isVector());
    LocVT = ValVT;
    LocInfo = CCValAssign::Full;
  }
  State.addLoc(CCValAssign::getMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
  return false;
}

template <typename ArgTy>
static std::optional<unsigned> preAssignMask(const ArgTy &Args) {
  for (const auto &ArgIdx : enumerate(Args)) {
    MVT ArgVT = ArgIdx.value().VT;
    if (ArgVT.isVector() && ArgVT.getVectorElementType() == MVT::i1)
      return ArgIdx.index();
  }
  return std::nullopt;
}

void RISCVTargetLowering::analyzeInputArgs(
    MachineFunction &MF, CCState &CCInfo,
    const SmallVectorImpl<ISD::InputArg> &Ins, bool IsRet,
    RISCVCCAssignFn Fn) const {
  unsigned NumArgs = Ins.size();
  FunctionType *FType = MF.getFunction().getFunctionType();

  RVVArgDispatcher Dispatcher;
  if (IsRet) {
    Dispatcher = RVVArgDispatcher{&MF, this, ArrayRef(Ins)};
  } else {
    SmallVector<Type *, 4> TypeList;
    for (const Argument &Arg : MF.getFunction().args())
      TypeList.push_back(Arg.getType());
    Dispatcher = RVVArgDispatcher{&MF, this, ArrayRef(TypeList)};
  }

  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT ArgVT = Ins[i].VT;
    ISD::ArgFlagsTy ArgFlags = Ins[i].Flags;

    Type *ArgTy = nullptr;
    if (IsRet)
      ArgTy = FType->getReturnType();
    else if (Ins[i].isOrigArg())
      ArgTy = FType->getParamType(Ins[i].getOrigArgIndex());

    RISCVABI::ABI ABI = MF.getSubtarget<RISCVSubtarget>().getTargetABI();
    if (Fn(MF.getDataLayout(), ABI, i, ArgVT, ArgVT, CCValAssign::Full,
           ArgFlags, CCInfo, /*IsFixed=*/true, IsRet, ArgTy, *this,
           Dispatcher)) {
      LLVM_DEBUG(dbgs() << "InputArg #" << i << " has unhandled type "
                        << ArgVT << '\n');
      llvm_unreachable(nullptr);
    }
  }
}

void RISCVTargetLowering::analyzeOutputArgs(
    MachineFunction &MF, CCState &CCInfo,
    const SmallVectorImpl<ISD::OutputArg> &Outs, bool IsRet,
    CallLoweringInfo *CLI, RISCVCCAssignFn Fn) const {
  unsigned NumArgs = Outs.size();

  SmallVector<Type *, 4> TypeList;
  if (IsRet)
    TypeList.push_back(MF.getFunction().getReturnType());
  else if (CLI)
    for (const TargetLowering::ArgListEntry &Arg : CLI->getArgs())
      TypeList.push_back(Arg.Ty);
  RVVArgDispatcher Dispatcher{&MF, this, ArrayRef(TypeList)};

  for (unsigned i = 0; i != NumArgs; i++) {
    MVT ArgVT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    Type *OrigTy = CLI ? CLI->getArgs()[Outs[i].OrigArgIndex].Ty : nullptr;

    RISCVABI::ABI ABI = MF.getSubtarget<RISCVSubtarget>().getTargetABI();
    if (Fn(MF.getDataLayout(), ABI, i, ArgVT, ArgVT, CCValAssign::Full,
           ArgFlags, CCInfo, Outs[i].IsFixed, IsRet, OrigTy, *this,
           Dispatcher)) {
      LLVM_DEBUG(dbgs() << "OutputArg #" << i << " has unhandled type "
                        << ArgVT << "\n");
      llvm_unreachable(nullptr);
    }
  }
}

// Convert Val to a ValVT. Should not be called for CCValAssign::Indirect
// values.
static SDValue convertLocVTToValVT(SelectionDAG &DAG, SDValue Val,
                                   const CCValAssign &VA, const SDLoc &DL,
                                   const RISCVSubtarget &Subtarget) {
  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
    if (VA.getValVT().isFixedLengthVector() && VA.getLocVT().isScalableVector())
      Val = convertFromScalableVector(VA.getValVT(), Val, DAG, Subtarget);
    break;
  case CCValAssign::BCvt:
    if (VA.getLocVT().isInteger() &&
        (VA.getValVT() == MVT::f16 || VA.getValVT() == MVT::bf16)) {
      Val = DAG.getNode(RISCVISD::FMV_H_X, DL, VA.getValVT(), Val);
    } else if (VA.getLocVT() == MVT::i64 && VA.getValVT() == MVT::f32) {
      if (RV64LegalI32) {
        Val = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Val);
        Val = DAG.getNode(ISD::BITCAST, DL, MVT::f32, Val);
      } else {
        Val = DAG.getNode(RISCVISD::FMV_W_X_RV64, DL, MVT::f32, Val);
      }
    } else {
      Val = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), Val);
    }
    break;
  }
  return Val;
}

// The caller is responsible for loading the full value if the argument is
// passed with CCValAssign::Indirect.
static SDValue unpackFromRegLoc(SelectionDAG &DAG, SDValue Chain,
                                const CCValAssign &VA, const SDLoc &DL,
                                const ISD::InputArg &In,
                                const RISCVTargetLowering &TLI) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  EVT LocVT = VA.getLocVT();
  SDValue Val;
  const TargetRegisterClass *RC = TLI.getRegClassFor(LocVT.getSimpleVT());
  Register VReg = RegInfo.createVirtualRegister(RC);
  RegInfo.addLiveIn(VA.getLocReg(), VReg);
  Val = DAG.getCopyFromReg(Chain, DL, VReg, LocVT);

  // If input is sign extended from 32 bits, note it for the SExtWRemoval pass.
  if (In.isOrigArg()) {
    Argument *OrigArg = MF.getFunction().getArg(In.getOrigArgIndex());
    if (OrigArg->getType()->isIntegerTy()) {
      unsigned BitWidth = OrigArg->getType()->getIntegerBitWidth();
      // An input zero extended from i31 can also be considered sign extended.
      if ((BitWidth <= 32 && In.Flags.isSExt()) ||
          (BitWidth < 32 && In.Flags.isZExt())) {
        RISCVMachineFunctionInfo *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();
        RVFI->addSExt32Register(VReg);
      }
    }
  }

  if (VA.getLocInfo() == CCValAssign::Indirect)
    return Val;

  return convertLocVTToValVT(DAG, Val, VA, DL, TLI.getSubtarget());
}

static SDValue convertValVTToLocVT(SelectionDAG &DAG, SDValue Val,
                                   const CCValAssign &VA, const SDLoc &DL,
                                   const RISCVSubtarget &Subtarget) {
  EVT LocVT = VA.getLocVT();

  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
    if (VA.getValVT().isFixedLengthVector() && LocVT.isScalableVector())
      Val = convertToScalableVector(LocVT, Val, DAG, Subtarget);
    break;
  case CCValAssign::BCvt:
    if (LocVT.isInteger() &&
        (VA.getValVT() == MVT::f16 || VA.getValVT() == MVT::bf16)) {
      Val = DAG.getNode(RISCVISD::FMV_X_ANYEXTH, DL, LocVT, Val);
    } else if (LocVT == MVT::i64 && VA.getValVT() == MVT::f32) {
      if (RV64LegalI32) {
        Val = DAG.getNode(ISD::BITCAST, DL, MVT::i32, Val);
        Val = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Val);
      } else {
        Val = DAG.getNode(RISCVISD::FMV_X_ANYEXTW_RV64, DL, MVT::i64, Val);
      }
    } else {
      Val = DAG.getNode(ISD::BITCAST, DL, LocVT, Val);
    }
    break;
  }
  return Val;
}

// The caller is responsible for loading the full value if the argument is
// passed with CCValAssign::Indirect.
static SDValue unpackFromMemLoc(SelectionDAG &DAG, SDValue Chain,
                                const CCValAssign &VA, const SDLoc &DL) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  EVT LocVT = VA.getLocVT();
  EVT ValVT = VA.getValVT();
  EVT PtrVT = MVT::getIntegerVT(DAG.getDataLayout().getPointerSizeInBits(0));
  if (ValVT.isScalableVector()) {
    // When the value is a scalable vector, we save the pointer which points to
    // the scalable vector value in the stack. The ValVT will be the pointer
    // type, instead of the scalable vector type.
    ValVT = LocVT;
  }
  int FI = MFI.CreateFixedObject(ValVT.getStoreSize(), VA.getLocMemOffset(),
                                 /*IsImmutable=*/true);
  SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
  SDValue Val;

  ISD::LoadExtType ExtType;
  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
  case CCValAssign::Indirect:
  case CCValAssign::BCvt:
    ExtType = ISD::NON_EXTLOAD;
    break;
  }
  Val = DAG.getExtLoad(
      ExtType, DL, LocVT, Chain, FIN,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI), ValVT);
  return Val;
}

static SDValue unpackF64OnRV32DSoftABI(SelectionDAG &DAG, SDValue Chain,
                                       const CCValAssign &VA,
                                       const CCValAssign &HiVA,
                                       const SDLoc &DL) {
  assert(VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64 &&
         "Unexpected VA");
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  assert(VA.isRegLoc() && "Expected register VA assignment");

  Register LoVReg = RegInfo.createVirtualRegister(&RISCV::GPRRegClass);
  RegInfo.addLiveIn(VA.getLocReg(), LoVReg);
  SDValue Lo = DAG.getCopyFromReg(Chain, DL, LoVReg, MVT::i32);
  SDValue Hi;
  if (HiVA.isMemLoc()) {
    // Second half of f64 is passed on the stack.
    int FI = MFI.CreateFixedObject(4, HiVA.getLocMemOffset(),
                                   /*IsImmutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);
    Hi = DAG.getLoad(MVT::i32, DL, Chain, FIN,
                     MachinePointerInfo::getFixedStack(MF, FI));
  } else {
    // Second half of f64 is passed in another GPR.
    Register HiVReg = RegInfo.createVirtualRegister(&RISCV::GPRRegClass);
    RegInfo.addLiveIn(HiVA.getLocReg(), HiVReg);
    Hi = DAG.getCopyFromReg(Chain, DL, HiVReg, MVT::i32);
  }
  return DAG.getNode(RISCVISD::BuildPairF64, DL, MVT::f64, Lo, Hi);
}

// FastCC has less than 1% performance improvement for some particular
// benchmark. But theoretically, it may has benenfit for some cases.
bool RISCV::CC_RISCV_FastCC(const DataLayout &DL, RISCVABI::ABI ABI,
                            unsigned ValNo, MVT ValVT, MVT LocVT,
                            CCValAssign::LocInfo LocInfo,
                            ISD::ArgFlagsTy ArgFlags, CCState &State,
                            bool IsFixed, bool IsRet, Type *OrigTy,
                            const RISCVTargetLowering &TLI,
                            RVVArgDispatcher &RVVDispatcher) {
  if (LocVT == MVT::i32 || LocVT == MVT::i64) {
    if (unsigned Reg = State.AllocateReg(getFastCCArgGPRs(ABI))) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  const RISCVSubtarget &Subtarget = TLI.getSubtarget();

  if (LocVT == MVT::f16 &&
      (Subtarget.hasStdExtZfh() || Subtarget.hasStdExtZfhmin())) {
    static const MCPhysReg FPR16List[] = {
        RISCV::F10_H, RISCV::F11_H, RISCV::F12_H, RISCV::F13_H, RISCV::F14_H,
        RISCV::F15_H, RISCV::F16_H, RISCV::F17_H, RISCV::F0_H,  RISCV::F1_H,
        RISCV::F2_H,  RISCV::F3_H,  RISCV::F4_H,  RISCV::F5_H,  RISCV::F6_H,
        RISCV::F7_H,  RISCV::F28_H, RISCV::F29_H, RISCV::F30_H, RISCV::F31_H};
    if (unsigned Reg = State.AllocateReg(FPR16List)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  if (LocVT == MVT::f32 && Subtarget.hasStdExtF()) {
    static const MCPhysReg FPR32List[] = {
        RISCV::F10_F, RISCV::F11_F, RISCV::F12_F, RISCV::F13_F, RISCV::F14_F,
        RISCV::F15_F, RISCV::F16_F, RISCV::F17_F, RISCV::F0_F,  RISCV::F1_F,
        RISCV::F2_F,  RISCV::F3_F,  RISCV::F4_F,  RISCV::F5_F,  RISCV::F6_F,
        RISCV::F7_F,  RISCV::F28_F, RISCV::F29_F, RISCV::F30_F, RISCV::F31_F};
    if (unsigned Reg = State.AllocateReg(FPR32List)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  if (LocVT == MVT::f64 && Subtarget.hasStdExtD()) {
    static const MCPhysReg FPR64List[] = {
        RISCV::F10_D, RISCV::F11_D, RISCV::F12_D, RISCV::F13_D, RISCV::F14_D,
        RISCV::F15_D, RISCV::F16_D, RISCV::F17_D, RISCV::F0_D,  RISCV::F1_D,
        RISCV::F2_D,  RISCV::F3_D,  RISCV::F4_D,  RISCV::F5_D,  RISCV::F6_D,
        RISCV::F7_D,  RISCV::F28_D, RISCV::F29_D, RISCV::F30_D, RISCV::F31_D};
    if (unsigned Reg = State.AllocateReg(FPR64List)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  // Check if there is an available GPR before hitting the stack.
  if ((LocVT == MVT::f16 &&
       (Subtarget.hasStdExtZhinx() || Subtarget.hasStdExtZhinxmin())) ||
      (LocVT == MVT::f32 && Subtarget.hasStdExtZfinx()) ||
      (LocVT == MVT::f64 && Subtarget.is64Bit() &&
       Subtarget.hasStdExtZdinx())) {
    if (unsigned Reg = State.AllocateReg(getFastCCArgGPRs(ABI))) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  if (LocVT == MVT::f16) {
    unsigned Offset2 = State.AllocateStack(2, Align(2));
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset2, LocVT, LocInfo));
    return false;
  }

  if (LocVT == MVT::i32 || LocVT == MVT::f32) {
    unsigned Offset4 = State.AllocateStack(4, Align(4));
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset4, LocVT, LocInfo));
    return false;
  }

  if (LocVT == MVT::i64 || LocVT == MVT::f64) {
    unsigned Offset5 = State.AllocateStack(8, Align(8));
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset5, LocVT, LocInfo));
    return false;
  }

  if (LocVT.isVector()) {
    MCPhysReg AllocatedVReg = RVVDispatcher.getNextPhysReg();
    if (AllocatedVReg) {
      // Fixed-length vectors are located in the corresponding scalable-vector
      // container types.
      if (ValVT.isFixedLengthVector())
        LocVT = TLI.getContainerForFixedLengthVector(LocVT);
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, AllocatedVReg, LocVT, LocInfo));
    } else {
      // Try and pass the address via a "fast" GPR.
      if (unsigned GPRReg = State.AllocateReg(getFastCCArgGPRs(ABI))) {
        LocInfo = CCValAssign::Indirect;
        LocVT = TLI.getSubtarget().getXLenVT();
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, GPRReg, LocVT, LocInfo));
      } else if (ValVT.isFixedLengthVector()) {
        auto StackAlign =
            MaybeAlign(ValVT.getScalarSizeInBits() / 8).valueOrOne();
        unsigned StackOffset =
            State.AllocateStack(ValVT.getStoreSize(), StackAlign);
        State.addLoc(
            CCValAssign::getMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
      } else {
        // Can't pass scalable vectors on the stack.
        return true;
      }
    }

    return false;
  }

  return true; // CC didn't match.
}

bool RISCV::CC_RISCV_GHC(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo,
                         ISD::ArgFlagsTy ArgFlags, CCState &State) {
  if (ArgFlags.isNest()) {
    report_fatal_error(
        "Attribute 'nest' is not supported in GHC calling convention");
  }

  static const MCPhysReg GPRList[] = {
      RISCV::X9,  RISCV::X18, RISCV::X19, RISCV::X20, RISCV::X21, RISCV::X22,
      RISCV::X23, RISCV::X24, RISCV::X25, RISCV::X26, RISCV::X27};

  if (LocVT == MVT::i32 || LocVT == MVT::i64) {
    // Pass in STG registers: Base, Sp, Hp, R1, R2, R3, R4, R5, R6, R7, SpLim
    //                        s1    s2  s3  s4  s5  s6  s7  s8  s9  s10 s11
    if (unsigned Reg = State.AllocateReg(GPRList)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  const RISCVSubtarget &Subtarget =
      State.getMachineFunction().getSubtarget<RISCVSubtarget>();

  if (LocVT == MVT::f32 && Subtarget.hasStdExtF()) {
    // Pass in STG registers: F1, ..., F6
    //                        fs0 ... fs5
    static const MCPhysReg FPR32List[] = {RISCV::F8_F, RISCV::F9_F,
                                          RISCV::F18_F, RISCV::F19_F,
                                          RISCV::F20_F, RISCV::F21_F};
    if (unsigned Reg = State.AllocateReg(FPR32List)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  if (LocVT == MVT::f64 && Subtarget.hasStdExtD()) {
    // Pass in STG registers: D1, ..., D6
    //                        fs6 ... fs11
    static const MCPhysReg FPR64List[] = {RISCV::F22_D, RISCV::F23_D,
                                          RISCV::F24_D, RISCV::F25_D,
                                          RISCV::F26_D, RISCV::F27_D};
    if (unsigned Reg = State.AllocateReg(FPR64List)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  if ((LocVT == MVT::f32 && Subtarget.hasStdExtZfinx()) ||
      (LocVT == MVT::f64 && Subtarget.hasStdExtZdinx() &&
       Subtarget.is64Bit())) {
    if (unsigned Reg = State.AllocateReg(GPRList)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  }

  report_fatal_error("No registers left in GHC calling convention");
  return true;
}

// Transform physical registers into virtual registers.
SDValue RISCVTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  MachineFunction &MF = DAG.getMachineFunction();

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::SPIR_KERNEL:
  case CallingConv::GRAAL:
  case CallingConv::RISCV_VectorCall:
    break;
  case CallingConv::GHC:
    if (Subtarget.hasStdExtE())
      report_fatal_error("GHC calling convention is not supported on RVE!");
    if (!Subtarget.hasStdExtFOrZfinx() || !Subtarget.hasStdExtDOrZdinx())
      report_fatal_error("GHC calling convention requires the (Zfinx/F) and "
                         "(Zdinx/D) instruction set extensions");
  }

  const Function &Func = MF.getFunction();
  if (Func.hasFnAttribute("interrupt")) {
    if (!Func.arg_empty())
      report_fatal_error(
        "Functions with the interrupt attribute cannot have arguments!");

    StringRef Kind =
      MF.getFunction().getFnAttribute("interrupt").getValueAsString();

    if (!(Kind == "user" || Kind == "supervisor" || Kind == "machine"))
      report_fatal_error(
        "Function interrupt attribute argument not supported!");
  }

  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  MVT XLenVT = Subtarget.getXLenVT();
  unsigned XLenInBytes = Subtarget.getXLen() / 8;
  // Used with vargs to acumulate store chains.
  std::vector<SDValue> OutChains;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  if (CallConv == CallingConv::GHC)
    CCInfo.AnalyzeFormalArguments(Ins, RISCV::CC_RISCV_GHC);
  else
    analyzeInputArgs(MF, CCInfo, Ins, /*IsRet=*/false,
                     CallConv == CallingConv::Fast ? RISCV::CC_RISCV_FastCC
                                                   : RISCV::CC_RISCV);

  for (unsigned i = 0, e = ArgLocs.size(), InsIdx = 0; i != e; ++i, ++InsIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue ArgValue;
    // Passing f64 on RV32D with a soft float ABI must be handled as a special
    // case.
    if (VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64) {
      assert(VA.needsCustom());
      ArgValue = unpackF64OnRV32DSoftABI(DAG, Chain, VA, ArgLocs[++i], DL);
    } else if (VA.isRegLoc())
      ArgValue = unpackFromRegLoc(DAG, Chain, VA, DL, Ins[InsIdx], *this);
    else
      ArgValue = unpackFromMemLoc(DAG, Chain, VA, DL);

    if (VA.getLocInfo() == CCValAssign::Indirect) {
      // If the original argument was split and passed by reference (e.g. i128
      // on RV32), we need to load all parts of it here (using the same
      // address). Vectors may be partly split to registers and partly to the
      // stack, in which case the base address is partly offset and subsequent
      // stores are relative to that.
      InVals.push_back(DAG.getLoad(VA.getValVT(), DL, Chain, ArgValue,
                                   MachinePointerInfo()));
      unsigned ArgIndex = Ins[InsIdx].OrigArgIndex;
      unsigned ArgPartOffset = Ins[InsIdx].PartOffset;
      assert(VA.getValVT().isVector() || ArgPartOffset == 0);
      while (i + 1 != e && Ins[InsIdx + 1].OrigArgIndex == ArgIndex) {
        CCValAssign &PartVA = ArgLocs[i + 1];
        unsigned PartOffset = Ins[InsIdx + 1].PartOffset - ArgPartOffset;
        SDValue Offset = DAG.getIntPtrConstant(PartOffset, DL);
        if (PartVA.getValVT().isScalableVector())
          Offset = DAG.getNode(ISD::VSCALE, DL, XLenVT, Offset);
        SDValue Address = DAG.getNode(ISD::ADD, DL, PtrVT, ArgValue, Offset);
        InVals.push_back(DAG.getLoad(PartVA.getValVT(), DL, Chain, Address,
                                     MachinePointerInfo()));
        ++i;
        ++InsIdx;
      }
      continue;
    }
    InVals.push_back(ArgValue);
  }

  if (any_of(ArgLocs,
             [](CCValAssign &VA) { return VA.getLocVT().isScalableVector(); }))
    MF.getInfo<RISCVMachineFunctionInfo>()->setIsVectorCall();

  if (IsVarArg) {
    ArrayRef<MCPhysReg> ArgRegs = RISCV::getArgGPRs(Subtarget.getTargetABI());
    unsigned Idx = CCInfo.getFirstUnallocated(ArgRegs);
    const TargetRegisterClass *RC = &RISCV::GPRRegClass;
    MachineFrameInfo &MFI = MF.getFrameInfo();
    MachineRegisterInfo &RegInfo = MF.getRegInfo();
    RISCVMachineFunctionInfo *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();

    // Size of the vararg save area. For now, the varargs save area is either
    // zero or large enough to hold a0-a7.
    int VarArgsSaveSize = XLenInBytes * (ArgRegs.size() - Idx);
    int FI;

    // If all registers are allocated, then all varargs must be passed on the
    // stack and we don't need to save any argregs.
    if (VarArgsSaveSize == 0) {
      int VaArgOffset = CCInfo.getStackSize();
      FI = MFI.CreateFixedObject(XLenInBytes, VaArgOffset, true);
    } else {
      int VaArgOffset = -VarArgsSaveSize;
      FI = MFI.CreateFixedObject(VarArgsSaveSize, VaArgOffset, true);

      // If saving an odd number of registers then create an extra stack slot to
      // ensure that the frame pointer is 2*XLEN-aligned, which in turn ensures
      // offsets to even-numbered registered remain 2*XLEN-aligned.
      if (Idx % 2) {
        MFI.CreateFixedObject(
            XLenInBytes, VaArgOffset - static_cast<int>(XLenInBytes), true);
        VarArgsSaveSize += XLenInBytes;
      }

      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);

      // Copy the integer registers that may have been used for passing varargs
      // to the vararg save area.
      for (unsigned I = Idx; I < ArgRegs.size(); ++I) {
        const Register Reg = RegInfo.createVirtualRegister(RC);
        RegInfo.addLiveIn(ArgRegs[I], Reg);
        SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, XLenVT);
        SDValue Store = DAG.getStore(
            Chain, DL, ArgValue, FIN,
            MachinePointerInfo::getFixedStack(MF, FI, (I - Idx) * XLenInBytes));
        OutChains.push_back(Store);
        FIN =
            DAG.getMemBasePlusOffset(FIN, TypeSize::getFixed(XLenInBytes), DL);
      }
    }

    // Record the frame index of the first variable argument
    // which is a value necessary to VASTART.
    RVFI->setVarArgsFrameIndex(FI);
    RVFI->setVarArgsSaveSize(VarArgsSaveSize);
  }

  // All stores are grouped in one node to allow the matching between
  // the size of Ins and InVals. This only happens for vararg functions.
  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  }

  return Chain;
}

/// isEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization.
/// Note: This is modelled after ARM's IsEligibleForTailCallOptimization.
bool RISCVTargetLowering::isEligibleForTailCallOptimization(
    CCState &CCInfo, CallLoweringInfo &CLI, MachineFunction &MF,
    const SmallVector<CCValAssign, 16> &ArgLocs) const {

  auto CalleeCC = CLI.CallConv;
  auto &Outs = CLI.Outs;
  auto &Caller = MF.getFunction();
  auto CallerCC = Caller.getCallingConv();

  // Exception-handling functions need a special set of instructions to
  // indicate a return to the hardware. Tail-calling another function would
  // probably break this.
  // TODO: The "interrupt" attribute isn't currently defined by RISC-V. This
  // should be expanded as new function attributes are introduced.
  if (Caller.hasFnAttribute("interrupt"))
    return false;

  // Do not tail call opt if the stack is used to pass parameters.
  if (CCInfo.getStackSize() != 0)
    return false;

  // Do not tail call opt if any parameters need to be passed indirectly.
  // Since long doubles (fp128) and i128 are larger than 2*XLEN, they are
  // passed indirectly. So the address of the value will be passed in a
  // register, or if not available, then the address is put on the stack. In
  // order to pass indirectly, space on the stack often needs to be allocated
  // in order to store the value. In this case the CCInfo.getNextStackOffset()
  // != 0 check is not enough and we need to check if any CCValAssign ArgsLocs
  // are passed CCValAssign::Indirect.
  for (auto &VA : ArgLocs)
    if (VA.getLocInfo() == CCValAssign::Indirect)
      return false;

  // Do not tail call opt if either caller or callee uses struct return
  // semantics.
  auto IsCallerStructRet = Caller.hasStructRetAttr();
  auto IsCalleeStructRet = Outs.empty() ? false : Outs[0].Flags.isSRet();
  if (IsCallerStructRet || IsCalleeStructRet)
    return false;

  // The callee has to preserve all registers the caller needs to preserve.
  const RISCVRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  if (CalleeCC != CallerCC) {
    const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
    if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
      return false;
  }

  // Byval parameters hand the function a pointer directly into the stack area
  // we want to reuse during a tail call. Working around this *is* possible
  // but less efficient and uglier in LowerCall.
  for (auto &Arg : Outs)
    if (Arg.Flags.isByVal())
      return false;

  return true;
}

static Align getPrefTypeAlign(EVT VT, SelectionDAG &DAG) {
  return DAG.getDataLayout().getPrefTypeAlign(
      VT.getTypeForEVT(*DAG.getContext()));
}

// Lower a call to a callseq_start + CALL + callseq_end chain, and add input
// and output parameter nodes.
SDValue RISCVTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  MVT XLenVT = Subtarget.getXLenVT();

  MachineFunction &MF = DAG.getMachineFunction();

  // Analyze the operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState ArgCCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  if (CallConv == CallingConv::GHC) {
    if (Subtarget.hasStdExtE())
      report_fatal_error("GHC calling convention is not supported on RVE!");
    ArgCCInfo.AnalyzeCallOperands(Outs, RISCV::CC_RISCV_GHC);
  } else
    analyzeOutputArgs(MF, ArgCCInfo, Outs, /*IsRet=*/false, &CLI,
                      CallConv == CallingConv::Fast ? RISCV::CC_RISCV_FastCC
                                                    : RISCV::CC_RISCV);

  // Check if it's really possible to do a tail call.
  if (IsTailCall)
    IsTailCall = isEligibleForTailCallOptimization(ArgCCInfo, CLI, MF, ArgLocs);

  if (IsTailCall)
    ++NumTailCalls;
  else if (CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("failed to perform tail call elimination on a call "
                       "site marked musttail");

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = ArgCCInfo.getStackSize();

  // Create local copies for byval args
  SmallVector<SDValue, 8> ByValArgs;
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    if (!Flags.isByVal())
      continue;

    SDValue Arg = OutVals[i];
    unsigned Size = Flags.getByValSize();
    Align Alignment = Flags.getNonZeroByValAlign();

    int FI =
        MF.getFrameInfo().CreateStackObject(Size, Alignment, /*isSS=*/false);
    SDValue FIPtr = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    SDValue SizeNode = DAG.getConstant(Size, DL, XLenVT);

    Chain = DAG.getMemcpy(Chain, DL, FIPtr, Arg, SizeNode, Alignment,
                          /*IsVolatile=*/false,
                          /*AlwaysInline=*/false, /*CI*/ nullptr, IsTailCall,
                          MachinePointerInfo(), MachinePointerInfo());
    ByValArgs.push_back(FIPtr);
  }

  if (!IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, CLI.DL);

  // Copy argument values to their designated locations.
  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;
  for (unsigned i = 0, j = 0, e = ArgLocs.size(), OutIdx = 0; i != e;
       ++i, ++OutIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue ArgValue = OutVals[OutIdx];
    ISD::ArgFlagsTy Flags = Outs[OutIdx].Flags;

    // Handle passing f64 on RV32D with a soft float ABI as a special case.
    if (VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64) {
      assert(VA.isRegLoc() && "Expected register VA assignment");
      assert(VA.needsCustom());
      SDValue SplitF64 = DAG.getNode(
          RISCVISD::SplitF64, DL, DAG.getVTList(MVT::i32, MVT::i32), ArgValue);
      SDValue Lo = SplitF64.getValue(0);
      SDValue Hi = SplitF64.getValue(1);

      Register RegLo = VA.getLocReg();
      RegsToPass.push_back(std::make_pair(RegLo, Lo));

      // Get the CCValAssign for the Hi part.
      CCValAssign &HiVA = ArgLocs[++i];

      if (HiVA.isMemLoc()) {
        // Second half of f64 is passed on the stack.
        if (!StackPtr.getNode())
          StackPtr = DAG.getCopyFromReg(Chain, DL, RISCV::X2, PtrVT);
        SDValue Address =
            DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr,
                        DAG.getIntPtrConstant(HiVA.getLocMemOffset(), DL));
        // Emit the store.
        MemOpChains.push_back(
            DAG.getStore(Chain, DL, Hi, Address, MachinePointerInfo()));
      } else {
        // Second half of f64 is passed in another GPR.
        Register RegHigh = HiVA.getLocReg();
        RegsToPass.push_back(std::make_pair(RegHigh, Hi));
      }
      continue;
    }

    // Promote the value if needed.
    // For now, only handle fully promoted and indirect arguments.
    if (VA.getLocInfo() == CCValAssign::Indirect) {
      // Store the argument in a stack slot and pass its address.
      Align StackAlign =
          std::max(getPrefTypeAlign(Outs[OutIdx].ArgVT, DAG),
                   getPrefTypeAlign(ArgValue.getValueType(), DAG));
      TypeSize StoredSize = ArgValue.getValueType().getStoreSize();
      // If the original argument was split (e.g. i128), we need
      // to store the required parts of it here (and pass just one address).
      // Vectors may be partly split to registers and partly to the stack, in
      // which case the base address is partly offset and subsequent stores are
      // relative to that.
      unsigned ArgIndex = Outs[OutIdx].OrigArgIndex;
      unsigned ArgPartOffset = Outs[OutIdx].PartOffset;
      assert(VA.getValVT().isVector() || ArgPartOffset == 0);
      // Calculate the total size to store. We don't have access to what we're
      // actually storing other than performing the loop and collecting the
      // info.
      SmallVector<std::pair<SDValue, SDValue>> Parts;
      while (i + 1 != e && Outs[OutIdx + 1].OrigArgIndex == ArgIndex) {
        SDValue PartValue = OutVals[OutIdx + 1];
        unsigned PartOffset = Outs[OutIdx + 1].PartOffset - ArgPartOffset;
        SDValue Offset = DAG.getIntPtrConstant(PartOffset, DL);
        EVT PartVT = PartValue.getValueType();
        if (PartVT.isScalableVector())
          Offset = DAG.getNode(ISD::VSCALE, DL, XLenVT, Offset);
        StoredSize += PartVT.getStoreSize();
        StackAlign = std::max(StackAlign, getPrefTypeAlign(PartVT, DAG));
        Parts.push_back(std::make_pair(PartValue, Offset));
        ++i;
        ++OutIdx;
      }
      SDValue SpillSlot = DAG.CreateStackTemporary(StoredSize, StackAlign);
      int FI = cast<FrameIndexSDNode>(SpillSlot)->getIndex();
      MemOpChains.push_back(
          DAG.getStore(Chain, DL, ArgValue, SpillSlot,
                       MachinePointerInfo::getFixedStack(MF, FI)));
      for (const auto &Part : Parts) {
        SDValue PartValue = Part.first;
        SDValue PartOffset = Part.second;
        SDValue Address =
            DAG.getNode(ISD::ADD, DL, PtrVT, SpillSlot, PartOffset);
        MemOpChains.push_back(
            DAG.getStore(Chain, DL, PartValue, Address,
                         MachinePointerInfo::getFixedStack(MF, FI)));
      }
      ArgValue = SpillSlot;
    } else {
      ArgValue = convertValVTToLocVT(DAG, ArgValue, VA, DL, Subtarget);
    }

    // Use local copy if it is a byval arg.
    if (Flags.isByVal())
      ArgValue = ByValArgs[j++];

    if (VA.isRegLoc()) {
      // Queue up the argument copies and emit them at the end.
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), ArgValue));
    } else {
      assert(VA.isMemLoc() && "Argument not register or memory");
      assert(!IsTailCall && "Tail call not allowed if stack is used "
                            "for passing parameters");

      // Work out the address of the stack slot.
      if (!StackPtr.getNode())
        StackPtr = DAG.getCopyFromReg(Chain, DL, RISCV::X2, PtrVT);
      SDValue Address =
          DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr,
                      DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));

      // Emit the store.
      MemOpChains.push_back(
          DAG.getStore(Chain, DL, ArgValue, Address, MachinePointerInfo()));
    }
  }

  // Join the stores, which are independent of one another.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue Glue;

  // Build a sequence of copy-to-reg nodes, chained and glued together.
  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg.first, Reg.second, Glue);
    Glue = Chain.getValue(1);
  }

  // Validate that none of the argument registers have been marked as
  // reserved, if so report an error. Do the same for the return address if this
  // is not a tailcall.
  validateCCReservedRegs(RegsToPass, MF);
  if (!IsTailCall &&
      MF.getSubtarget<RISCVSubtarget>().isRegisterReservedByUser(RISCV::X1))
    MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
        MF.getFunction(),
        "Return address register required, but has been reserved."});

  // If the callee is a GlobalAddress/ExternalSymbol node, turn it into a
  // TargetGlobalAddress/TargetExternalSymbol node so that legalize won't
  // split it and then direct call can be matched by PseudoCALL.
  if (GlobalAddressSDNode *S = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = S->getGlobal();
    Callee = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, RISCVII::MO_CALL);
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), PtrVT, RISCVII::MO_CALL);
  }

  // The first call operand is the chain and the second is the target address.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));

  if (!IsTailCall) {
    // Add a register mask operand representing the call-preserved registers.
    const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
    const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
    assert(Mask && "Missing call preserved mask for calling convention");
    Ops.push_back(DAG.getRegisterMask(Mask));
  }

  // Glue the call to the argument copies, if any.
  if (Glue.getNode())
    Ops.push_back(Glue);

  assert((!CLI.CFIType || CLI.CB->isIndirectCall()) &&
         "Unexpected CFI type for a direct call");

  // Emit the call.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  if (IsTailCall) {
    MF.getFrameInfo().setHasTailCall();
    SDValue Ret = DAG.getNode(RISCVISD::TAIL, DL, NodeTys, Ops);
    if (CLI.CFIType)
      Ret.getNode()->setCFIType(CLI.CFIType->getZExtValue());
    DAG.addNoMergeSiteInfo(Ret.getNode(), CLI.NoMerge);
    return Ret;
  }

  Chain = DAG.getNode(RISCVISD::CALL, DL, NodeTys, Ops);
  if (CLI.CFIType)
    Chain.getNode()->setCFIType(CLI.CFIType->getZExtValue());
  DAG.addNoMergeSiteInfo(Chain.getNode(), CLI.NoMerge);
  Glue = Chain.getValue(1);

  // Mark the end of the call, which is glued to the call itself.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RetCCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());
  analyzeInputArgs(MF, RetCCInfo, Ins, /*IsRet=*/true, RISCV::CC_RISCV);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    auto &VA = RVLocs[i];
    // Copy the value out
    SDValue RetValue =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    // Glue the RetValue to the end of the call sequence
    Chain = RetValue.getValue(1);
    Glue = RetValue.getValue(2);

    if (VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64) {
      assert(VA.needsCustom());
      SDValue RetValue2 = DAG.getCopyFromReg(Chain, DL, RVLocs[++i].getLocReg(),
                                             MVT::i32, Glue);
      Chain = RetValue2.getValue(1);
      Glue = RetValue2.getValue(2);
      RetValue = DAG.getNode(RISCVISD::BuildPairF64, DL, MVT::f64, RetValue,
                             RetValue2);
    }

    RetValue = convertLocVTToValVT(DAG, RetValue, VA, DL, Subtarget);

    InVals.push_back(RetValue);
  }

  return Chain;
}

bool RISCVTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);

  RVVArgDispatcher Dispatcher{&MF, this, ArrayRef(Outs)};

  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    MVT VT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    RISCVABI::ABI ABI = MF.getSubtarget<RISCVSubtarget>().getTargetABI();
    if (RISCV::CC_RISCV(MF.getDataLayout(), ABI, i, VT, VT, CCValAssign::Full,
                        ArgFlags, CCInfo, /*IsFixed=*/true, /*IsRet=*/true,
                        nullptr, *this, Dispatcher))
      return false;
  }
  return true;
}

SDValue
RISCVTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const RISCVSubtarget &STI = MF.getSubtarget<RISCVSubtarget>();

  // Stores the assignment of the return value to a location.
  SmallVector<CCValAssign, 16> RVLocs;

  // Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  analyzeOutputArgs(DAG.getMachineFunction(), CCInfo, Outs, /*IsRet=*/true,
                    nullptr, RISCV::CC_RISCV);

  if (CallConv == CallingConv::GHC && !RVLocs.empty())
    report_fatal_error("GHC functions return void only");

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0, e = RVLocs.size(), OutIdx = 0; i < e; ++i, ++OutIdx) {
    SDValue Val = OutVals[OutIdx];
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    if (VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64) {
      // Handle returning f64 on RV32D with a soft float ABI.
      assert(VA.isRegLoc() && "Expected return via registers");
      assert(VA.needsCustom());
      SDValue SplitF64 = DAG.getNode(RISCVISD::SplitF64, DL,
                                     DAG.getVTList(MVT::i32, MVT::i32), Val);
      SDValue Lo = SplitF64.getValue(0);
      SDValue Hi = SplitF64.getValue(1);
      Register RegLo = VA.getLocReg();
      Register RegHi = RVLocs[++i].getLocReg();

      if (STI.isRegisterReservedByUser(RegLo) ||
          STI.isRegisterReservedByUser(RegHi))
        MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
            MF.getFunction(),
            "Return value register required, but has been reserved."});

      Chain = DAG.getCopyToReg(Chain, DL, RegLo, Lo, Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(RegLo, MVT::i32));
      Chain = DAG.getCopyToReg(Chain, DL, RegHi, Hi, Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(RegHi, MVT::i32));
    } else {
      // Handle a 'normal' return.
      Val = convertValVTToLocVT(DAG, Val, VA, DL, Subtarget);
      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);

      if (STI.isRegisterReservedByUser(VA.getLocReg()))
        MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
            MF.getFunction(),
            "Return value register required, but has been reserved."});

      // Guarantee that all emitted copies are stuck together.
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
    }
  }

  RetOps[0] = Chain; // Update chain.

  // Add the glue node if we have it.
  if (Glue.getNode()) {
    RetOps.push_back(Glue);
  }

  if (any_of(RVLocs,
             [](CCValAssign &VA) { return VA.getLocVT().isScalableVector(); }))
    MF.getInfo<RISCVMachineFunctionInfo>()->setIsVectorCall();

  unsigned RetOpc = RISCVISD::RET_GLUE;
  // Interrupt service routines use different return instructions.
  const Function &Func = DAG.getMachineFunction().getFunction();
  if (Func.hasFnAttribute("interrupt")) {
    if (!Func.getReturnType()->isVoidTy())
      report_fatal_error(
          "Functions with the interrupt attribute must have void return type!");

    MachineFunction &MF = DAG.getMachineFunction();
    StringRef Kind =
      MF.getFunction().getFnAttribute("interrupt").getValueAsString();

    if (Kind == "supervisor")
      RetOpc = RISCVISD::SRET_GLUE;
    else
      RetOpc = RISCVISD::MRET_GLUE;
  }

  return DAG.getNode(RetOpc, DL, MVT::Other, RetOps);
}

void RISCVTargetLowering::validateCCReservedRegs(
    const SmallVectorImpl<std::pair<llvm::Register, llvm::SDValue>> &Regs,
    MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  const RISCVSubtarget &STI = MF.getSubtarget<RISCVSubtarget>();

  if (llvm::any_of(Regs, [&STI](auto Reg) {
        return STI.isRegisterReservedByUser(Reg.first);
      }))
    F.getContext().diagnose(DiagnosticInfoUnsupported{
        F, "Argument register required, but has been reserved."});
}

// Check if the result of the node is only used as a return value, as
// otherwise we can't perform a tail-call.
bool RISCVTargetLowering::isUsedByReturnOnly(SDNode *N, SDValue &Chain) const {
  if (N->getNumValues() != 1)
    return false;
  if (!N->hasNUsesOfValue(1, 0))
    return false;

  SDNode *Copy = *N->use_begin();

  if (Copy->getOpcode() == ISD::BITCAST) {
    return isUsedByReturnOnly(Copy, Chain);
  }

  // TODO: Handle additional opcodes in order to support tail-calling libcalls
  // with soft float ABIs.
  if (Copy->getOpcode() != ISD::CopyToReg) {
    return false;
  }

  // If the ISD::CopyToReg has a glue operand, we conservatively assume it
  // isn't safe to perform a tail call.
  if (Copy->getOperand(Copy->getNumOperands() - 1).getValueType() == MVT::Glue)
    return false;

  // The copy must be used by a RISCVISD::RET_GLUE, and nothing else.
  bool HasRet = false;
  for (SDNode *Node : Copy->uses()) {
    if (Node->getOpcode() != RISCVISD::RET_GLUE)
      return false;
    HasRet = true;
  }
  if (!HasRet)
    return false;

  Chain = Copy->getOperand(0);
  return true;
}

bool RISCVTargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  return CI->isTailCall();
}

const char *RISCVTargetLowering::getTargetNodeName(unsigned Opcode) const {
#define NODE_NAME_CASE(NODE)                                                   \
  case RISCVISD::NODE:                                                         \
    return "RISCVISD::" #NODE;
  // clang-format off
  switch ((RISCVISD::NodeType)Opcode) {
  case RISCVISD::FIRST_NUMBER:
    break;
  NODE_NAME_CASE(RET_GLUE)
  NODE_NAME_CASE(SRET_GLUE)
  NODE_NAME_CASE(MRET_GLUE)
  NODE_NAME_CASE(CALL)
  NODE_NAME_CASE(SELECT_CC)
  NODE_NAME_CASE(BR_CC)
  NODE_NAME_CASE(BuildPairF64)
  NODE_NAME_CASE(SplitF64)
  NODE_NAME_CASE(TAIL)
  NODE_NAME_CASE(ADD_LO)
  NODE_NAME_CASE(HI)
  NODE_NAME_CASE(LLA)
  NODE_NAME_CASE(ADD_TPREL)
  NODE_NAME_CASE(MULHSU)
  NODE_NAME_CASE(SHL_ADD)
  NODE_NAME_CASE(SLLW)
  NODE_NAME_CASE(SRAW)
  NODE_NAME_CASE(SRLW)
  NODE_NAME_CASE(DIVW)
  NODE_NAME_CASE(DIVUW)
  NODE_NAME_CASE(REMUW)
  NODE_NAME_CASE(ROLW)
  NODE_NAME_CASE(RORW)
  NODE_NAME_CASE(CLZW)
  NODE_NAME_CASE(CTZW)
  NODE_NAME_CASE(ABSW)
  NODE_NAME_CASE(FMV_H_X)
  NODE_NAME_CASE(FMV_X_ANYEXTH)
  NODE_NAME_CASE(FMV_X_SIGNEXTH)
  NODE_NAME_CASE(FMV_W_X_RV64)
  NODE_NAME_CASE(FMV_X_ANYEXTW_RV64)
  NODE_NAME_CASE(FCVT_X)
  NODE_NAME_CASE(FCVT_XU)
  NODE_NAME_CASE(FCVT_W_RV64)
  NODE_NAME_CASE(FCVT_WU_RV64)
  NODE_NAME_CASE(STRICT_FCVT_W_RV64)
  NODE_NAME_CASE(STRICT_FCVT_WU_RV64)
  NODE_NAME_CASE(FP_ROUND_BF16)
  NODE_NAME_CASE(FP_EXTEND_BF16)
  NODE_NAME_CASE(FROUND)
  NODE_NAME_CASE(FCLASS)
  NODE_NAME_CASE(FMAX)
  NODE_NAME_CASE(FMIN)
  NODE_NAME_CASE(READ_COUNTER_WIDE)
  NODE_NAME_CASE(BREV8)
  NODE_NAME_CASE(ORC_B)
  NODE_NAME_CASE(ZIP)
  NODE_NAME_CASE(UNZIP)
  NODE_NAME_CASE(CLMUL)
  NODE_NAME_CASE(CLMULH)
  NODE_NAME_CASE(CLMULR)
  NODE_NAME_CASE(MOPR)
  NODE_NAME_CASE(MOPRR)
  NODE_NAME_CASE(SHA256SIG0)
  NODE_NAME_CASE(SHA256SIG1)
  NODE_NAME_CASE(SHA256SUM0)
  NODE_NAME_CASE(SHA256SUM1)
  NODE_NAME_CASE(SM4KS)
  NODE_NAME_CASE(SM4ED)
  NODE_NAME_CASE(SM3P0)
  NODE_NAME_CASE(SM3P1)
  NODE_NAME_CASE(TH_LWD)
  NODE_NAME_CASE(TH_LWUD)
  NODE_NAME_CASE(TH_LDD)
  NODE_NAME_CASE(TH_SWD)
  NODE_NAME_CASE(TH_SDD)
  NODE_NAME_CASE(VMV_V_V_VL)
  NODE_NAME_CASE(VMV_V_X_VL)
  NODE_NAME_CASE(VFMV_V_F_VL)
  NODE_NAME_CASE(VMV_X_S)
  NODE_NAME_CASE(VMV_S_X_VL)
  NODE_NAME_CASE(VFMV_S_F_VL)
  NODE_NAME_CASE(SPLAT_VECTOR_SPLIT_I64_VL)
  NODE_NAME_CASE(READ_VLENB)
  NODE_NAME_CASE(TRUNCATE_VECTOR_VL)
  NODE_NAME_CASE(VSLIDEUP_VL)
  NODE_NAME_CASE(VSLIDE1UP_VL)
  NODE_NAME_CASE(VSLIDEDOWN_VL)
  NODE_NAME_CASE(VSLIDE1DOWN_VL)
  NODE_NAME_CASE(VFSLIDE1UP_VL)
  NODE_NAME_CASE(VFSLIDE1DOWN_VL)
  NODE_NAME_CASE(VID_VL)
  NODE_NAME_CASE(VFNCVT_ROD_VL)
  NODE_NAME_CASE(VECREDUCE_ADD_VL)
  NODE_NAME_CASE(VECREDUCE_UMAX_VL)
  NODE_NAME_CASE(VECREDUCE_SMAX_VL)
  NODE_NAME_CASE(VECREDUCE_UMIN_VL)
  NODE_NAME_CASE(VECREDUCE_SMIN_VL)
  NODE_NAME_CASE(VECREDUCE_AND_VL)
  NODE_NAME_CASE(VECREDUCE_OR_VL)
  NODE_NAME_CASE(VECREDUCE_XOR_VL)
  NODE_NAME_CASE(VECREDUCE_FADD_VL)
  NODE_NAME_CASE(VECREDUCE_SEQ_FADD_VL)
  NODE_NAME_CASE(VECREDUCE_FMIN_VL)
  NODE_NAME_CASE(VECREDUCE_FMAX_VL)
  NODE_NAME_CASE(ADD_VL)
  NODE_NAME_CASE(AND_VL)
  NODE_NAME_CASE(MUL_VL)
  NODE_NAME_CASE(OR_VL)
  NODE_NAME_CASE(SDIV_VL)
  NODE_NAME_CASE(SHL_VL)
  NODE_NAME_CASE(SREM_VL)
  NODE_NAME_CASE(SRA_VL)
  NODE_NAME_CASE(SRL_VL)
  NODE_NAME_CASE(ROTL_VL)
  NODE_NAME_CASE(ROTR_VL)
  NODE_NAME_CASE(SUB_VL)
  NODE_NAME_CASE(UDIV_VL)
  NODE_NAME_CASE(UREM_VL)
  NODE_NAME_CASE(XOR_VL)
  NODE_NAME_CASE(AVGFLOORS_VL)
  NODE_NAME_CASE(AVGFLOORU_VL)
  NODE_NAME_CASE(AVGCEILS_VL)
  NODE_NAME_CASE(AVGCEILU_VL)
  NODE_NAME_CASE(SADDSAT_VL)
  NODE_NAME_CASE(UADDSAT_VL)
  NODE_NAME_CASE(SSUBSAT_VL)
  NODE_NAME_CASE(USUBSAT_VL)
  NODE_NAME_CASE(VNCLIP_VL)
  NODE_NAME_CASE(VNCLIPU_VL)
  NODE_NAME_CASE(FADD_VL)
  NODE_NAME_CASE(FSUB_VL)
  NODE_NAME_CASE(FMUL_VL)
  NODE_NAME_CASE(FDIV_VL)
  NODE_NAME_CASE(FNEG_VL)
  NODE_NAME_CASE(FABS_VL)
  NODE_NAME_CASE(FSQRT_VL)
  NODE_NAME_CASE(FCLASS_VL)
  NODE_NAME_CASE(VFMADD_VL)
  NODE_NAME_CASE(VFNMADD_VL)
  NODE_NAME_CASE(VFMSUB_VL)
  NODE_NAME_CASE(VFNMSUB_VL)
  NODE_NAME_CASE(VFWMADD_VL)
  NODE_NAME_CASE(VFWNMADD_VL)
  NODE_NAME_CASE(VFWMSUB_VL)
  NODE_NAME_CASE(VFWNMSUB_VL)
  NODE_NAME_CASE(FCOPYSIGN_VL)
  NODE_NAME_CASE(SMIN_VL)
  NODE_NAME_CASE(SMAX_VL)
  NODE_NAME_CASE(UMIN_VL)
  NODE_NAME_CASE(UMAX_VL)
  NODE_NAME_CASE(BITREVERSE_VL)
  NODE_NAME_CASE(BSWAP_VL)
  NODE_NAME_CASE(CTLZ_VL)
  NODE_NAME_CASE(CTTZ_VL)
  NODE_NAME_CASE(CTPOP_VL)
  NODE_NAME_CASE(VFMIN_VL)
  NODE_NAME_CASE(VFMAX_VL)
  NODE_NAME_CASE(MULHS_VL)
  NODE_NAME_CASE(MULHU_VL)
  NODE_NAME_CASE(VFCVT_RTZ_X_F_VL)
  NODE_NAME_CASE(VFCVT_RTZ_XU_F_VL)
  NODE_NAME_CASE(VFCVT_RM_X_F_VL)
  NODE_NAME_CASE(VFCVT_RM_XU_F_VL)
  NODE_NAME_CASE(VFCVT_X_F_VL)
  NODE_NAME_CASE(VFCVT_XU_F_VL)
  NODE_NAME_CASE(VFROUND_NOEXCEPT_VL)
  NODE_NAME_CASE(SINT_TO_FP_VL)
  NODE_NAME_CASE(UINT_TO_FP_VL)
  NODE_NAME_CASE(VFCVT_RM_F_XU_VL)
  NODE_NAME_CASE(VFCVT_RM_F_X_VL)
  NODE_NAME_CASE(FP_EXTEND_VL)
  NODE_NAME_CASE(FP_ROUND_VL)
  NODE_NAME_CASE(STRICT_FADD_VL)
  NODE_NAME_CASE(STRICT_FSUB_VL)
  NODE_NAME_CASE(STRICT_FMUL_VL)
  NODE_NAME_CASE(STRICT_FDIV_VL)
  NODE_NAME_CASE(STRICT_FSQRT_VL)
  NODE_NAME_CASE(STRICT_VFMADD_VL)
  NODE_NAME_CASE(STRICT_VFNMADD_VL)
  NODE_NAME_CASE(STRICT_VFMSUB_VL)
  NODE_NAME_CASE(STRICT_VFNMSUB_VL)
  NODE_NAME_CASE(STRICT_FP_ROUND_VL)
  NODE_NAME_CASE(STRICT_FP_EXTEND_VL)
  NODE_NAME_CASE(STRICT_VFNCVT_ROD_VL)
  NODE_NAME_CASE(STRICT_SINT_TO_FP_VL)
  NODE_NAME_CASE(STRICT_UINT_TO_FP_VL)
  NODE_NAME_CASE(STRICT_VFCVT_RM_X_F_VL)
  NODE_NAME_CASE(STRICT_VFCVT_RTZ_X_F_VL)
  NODE_NAME_CASE(STRICT_VFCVT_RTZ_XU_F_VL)
  NODE_NAME_CASE(STRICT_FSETCC_VL)
  NODE_NAME_CASE(STRICT_FSETCCS_VL)
  NODE_NAME_CASE(STRICT_VFROUND_NOEXCEPT_VL)
  NODE_NAME_CASE(VWMUL_VL)
  NODE_NAME_CASE(VWMULU_VL)
  NODE_NAME_CASE(VWMULSU_VL)
  NODE_NAME_CASE(VWADD_VL)
  NODE_NAME_CASE(VWADDU_VL)
  NODE_NAME_CASE(VWSUB_VL)
  NODE_NAME_CASE(VWSUBU_VL)
  NODE_NAME_CASE(VWADD_W_VL)
  NODE_NAME_CASE(VWADDU_W_VL)
  NODE_NAME_CASE(VWSUB_W_VL)
  NODE_NAME_CASE(VWSUBU_W_VL)
  NODE_NAME_CASE(VWSLL_VL)
  NODE_NAME_CASE(VFWMUL_VL)
  NODE_NAME_CASE(VFWADD_VL)
  NODE_NAME_CASE(VFWSUB_VL)
  NODE_NAME_CASE(VFWADD_W_VL)
  NODE_NAME_CASE(VFWSUB_W_VL)
  NODE_NAME_CASE(VWMACC_VL)
  NODE_NAME_CASE(VWMACCU_VL)
  NODE_NAME_CASE(VWMACCSU_VL)
  NODE_NAME_CASE(VNSRL_VL)
  NODE_NAME_CASE(SETCC_VL)
  NODE_NAME_CASE(VMERGE_VL)
  NODE_NAME_CASE(VMAND_VL)
  NODE_NAME_CASE(VMOR_VL)
  NODE_NAME_CASE(VMXOR_VL)
  NODE_NAME_CASE(VMCLR_VL)
  NODE_NAME_CASE(VMSET_VL)
  NODE_NAME_CASE(VRGATHER_VX_VL)
  NODE_NAME_CASE(VRGATHER_VV_VL)
  NODE_NAME_CASE(VRGATHEREI16_VV_VL)
  NODE_NAME_CASE(VSEXT_VL)
  NODE_NAME_CASE(VZEXT_VL)
  NODE_NAME_CASE(VCPOP_VL)
  NODE_NAME_CASE(VFIRST_VL)
  NODE_NAME_CASE(READ_CSR)
  NODE_NAME_CASE(WRITE_CSR)
  NODE_NAME_CASE(SWAP_CSR)
  NODE_NAME_CASE(CZERO_EQZ)
  NODE_NAME_CASE(CZERO_NEZ)
  NODE_NAME_CASE(SW_GUARDED_BRIND)
  NODE_NAME_CASE(SF_VC_XV_SE)
  NODE_NAME_CASE(SF_VC_IV_SE)
  NODE_NAME_CASE(SF_VC_VV_SE)
  NODE_NAME_CASE(SF_VC_FV_SE)
  NODE_NAME_CASE(SF_VC_XVV_SE)
  NODE_NAME_CASE(SF_VC_IVV_SE)
  NODE_NAME_CASE(SF_VC_VVV_SE)
  NODE_NAME_CASE(SF_VC_FVV_SE)
  NODE_NAME_CASE(SF_VC_XVW_SE)
  NODE_NAME_CASE(SF_VC_IVW_SE)
  NODE_NAME_CASE(SF_VC_VVW_SE)
  NODE_NAME_CASE(SF_VC_FVW_SE)
  NODE_NAME_CASE(SF_VC_V_X_SE)
  NODE_NAME_CASE(SF_VC_V_I_SE)
  NODE_NAME_CASE(SF_VC_V_XV_SE)
  NODE_NAME_CASE(SF_VC_V_IV_SE)
  NODE_NAME_CASE(SF_VC_V_VV_SE)
  NODE_NAME_CASE(SF_VC_V_FV_SE)
  NODE_NAME_CASE(SF_VC_V_XVV_SE)
  NODE_NAME_CASE(SF_VC_V_IVV_SE)
  NODE_NAME_CASE(SF_VC_V_VVV_SE)
  NODE_NAME_CASE(SF_VC_V_FVV_SE)
  NODE_NAME_CASE(SF_VC_V_XVW_SE)
  NODE_NAME_CASE(SF_VC_V_IVW_SE)
  NODE_NAME_CASE(SF_VC_V_VVW_SE)
  NODE_NAME_CASE(SF_VC_V_FVW_SE)
  }
  // clang-format on
  return nullptr;
#undef NODE_NAME_CASE
}

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
RISCVTargetLowering::ConstraintType
RISCVTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'f':
      return C_RegisterClass;
    case 'I':
    case 'J':
    case 'K':
      return C_Immediate;
    case 'A':
      return C_Memory;
    case 's':
    case 'S': // A symbolic address
      return C_Other;
    }
  } else {
    if (Constraint == "vr" || Constraint == "vm")
      return C_RegisterClass;
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
RISCVTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  // First, see if this is a constraint that directly corresponds to a RISC-V
  // register class.
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      // TODO: Support fixed vectors up to XLen for P extension?
      if (VT.isVector())
        break;
      if (VT == MVT::f16 && Subtarget.hasStdExtZhinxmin())
        return std::make_pair(0U, &RISCV::GPRF16RegClass);
      if (VT == MVT::f32 && Subtarget.hasStdExtZfinx())
        return std::make_pair(0U, &RISCV::GPRF32RegClass);
      if (VT == MVT::f64 && Subtarget.hasStdExtZdinx() && !Subtarget.is64Bit())
        return std::make_pair(0U, &RISCV::GPRPairRegClass);
      return std::make_pair(0U, &RISCV::GPRNoX0RegClass);
    case 'f':
      if (Subtarget.hasStdExtZfhmin() && VT == MVT::f16)
        return std::make_pair(0U, &RISCV::FPR16RegClass);
      if (Subtarget.hasStdExtF() && VT == MVT::f32)
        return std::make_pair(0U, &RISCV::FPR32RegClass);
      if (Subtarget.hasStdExtD() && VT == MVT::f64)
        return std::make_pair(0U, &RISCV::FPR64RegClass);
      break;
    default:
      break;
    }
  } else if (Constraint == "vr") {
    for (const auto *RC : {&RISCV::VRRegClass, &RISCV::VRM2RegClass,
                           &RISCV::VRM4RegClass, &RISCV::VRM8RegClass}) {
      if (TRI->isTypeLegalForClass(*RC, VT.SimpleTy))
        return std::make_pair(0U, RC);
    }
  } else if (Constraint == "vm") {
    if (TRI->isTypeLegalForClass(RISCV::VMV0RegClass, VT.SimpleTy))
      return std::make_pair(0U, &RISCV::VMV0RegClass);
  }

  // Clang will correctly decode the usage of register name aliases into their
  // official names. However, other frontends like `rustc` do not. This allows
  // users of these frontends to use the ABI names for registers in LLVM-style
  // register constraints.
  unsigned XRegFromAlias = StringSwitch<unsigned>(Constraint.lower())
                               .Case("{zero}", RISCV::X0)
                               .Case("{ra}", RISCV::X1)
                               .Case("{sp}", RISCV::X2)
                               .Case("{gp}", RISCV::X3)
                               .Case("{tp}", RISCV::X4)
                               .Case("{t0}", RISCV::X5)
                               .Case("{t1}", RISCV::X6)
                               .Case("{t2}", RISCV::X7)
                               .Cases("{s0}", "{fp}", RISCV::X8)
                               .Case("{s1}", RISCV::X9)
                               .Case("{a0}", RISCV::X10)
                               .Case("{a1}", RISCV::X11)
                               .Case("{a2}", RISCV::X12)
                               .Case("{a3}", RISCV::X13)
                               .Case("{a4}", RISCV::X14)
                               .Case("{a5}", RISCV::X15)
                               .Case("{a6}", RISCV::X16)
                               .Case("{a7}", RISCV::X17)
                               .Case("{s2}", RISCV::X18)
                               .Case("{s3}", RISCV::X19)
                               .Case("{s4}", RISCV::X20)
                               .Case("{s5}", RISCV::X21)
                               .Case("{s6}", RISCV::X22)
                               .Case("{s7}", RISCV::X23)
                               .Case("{s8}", RISCV::X24)
                               .Case("{s9}", RISCV::X25)
                               .Case("{s10}", RISCV::X26)
                               .Case("{s11}", RISCV::X27)
                               .Case("{t3}", RISCV::X28)
                               .Case("{t4}", RISCV::X29)
                               .Case("{t5}", RISCV::X30)
                               .Case("{t6}", RISCV::X31)
                               .Default(RISCV::NoRegister);
  if (XRegFromAlias != RISCV::NoRegister)
    return std::make_pair(XRegFromAlias, &RISCV::GPRRegClass);

  // Since TargetLowering::getRegForInlineAsmConstraint uses the name of the
  // TableGen record rather than the AsmName to choose registers for InlineAsm
  // constraints, plus we want to match those names to the widest floating point
  // register type available, manually select floating point registers here.
  //
  // The second case is the ABI name of the register, so that frontends can also
  // use the ABI names in register constraint lists.
  if (Subtarget.hasStdExtF()) {
    unsigned FReg = StringSwitch<unsigned>(Constraint.lower())
                        .Cases("{f0}", "{ft0}", RISCV::F0_F)
                        .Cases("{f1}", "{ft1}", RISCV::F1_F)
                        .Cases("{f2}", "{ft2}", RISCV::F2_F)
                        .Cases("{f3}", "{ft3}", RISCV::F3_F)
                        .Cases("{f4}", "{ft4}", RISCV::F4_F)
                        .Cases("{f5}", "{ft5}", RISCV::F5_F)
                        .Cases("{f6}", "{ft6}", RISCV::F6_F)
                        .Cases("{f7}", "{ft7}", RISCV::F7_F)
                        .Cases("{f8}", "{fs0}", RISCV::F8_F)
                        .Cases("{f9}", "{fs1}", RISCV::F9_F)
                        .Cases("{f10}", "{fa0}", RISCV::F10_F)
                        .Cases("{f11}", "{fa1}", RISCV::F11_F)
                        .Cases("{f12}", "{fa2}", RISCV::F12_F)
                        .Cases("{f13}", "{fa3}", RISCV::F13_F)
                        .Cases("{f14}", "{fa4}", RISCV::F14_F)
                        .Cases("{f15}", "{fa5}", RISCV::F15_F)
                        .Cases("{f16}", "{fa6}", RISCV::F16_F)
                        .Cases("{f17}", "{fa7}", RISCV::F17_F)
                        .Cases("{f18}", "{fs2}", RISCV::F18_F)
                        .Cases("{f19}", "{fs3}", RISCV::F19_F)
                        .Cases("{f20}", "{fs4}", RISCV::F20_F)
                        .Cases("{f21}", "{fs5}", RISCV::F21_F)
                        .Cases("{f22}", "{fs6}", RISCV::F22_F)
                        .Cases("{f23}", "{fs7}", RISCV::F23_F)
                        .Cases("{f24}", "{fs8}", RISCV::F24_F)
                        .Cases("{f25}", "{fs9}", RISCV::F25_F)
                        .Cases("{f26}", "{fs10}", RISCV::F26_F)
                        .Cases("{f27}", "{fs11}", RISCV::F27_F)
                        .Cases("{f28}", "{ft8}", RISCV::F28_F)
                        .Cases("{f29}", "{ft9}", RISCV::F29_F)
                        .Cases("{f30}", "{ft10}", RISCV::F30_F)
                        .Cases("{f31}", "{ft11}", RISCV::F31_F)
                        .Default(RISCV::NoRegister);
    if (FReg != RISCV::NoRegister) {
      assert(RISCV::F0_F <= FReg && FReg <= RISCV::F31_F && "Unknown fp-reg");
      if (Subtarget.hasStdExtD() && (VT == MVT::f64 || VT == MVT::Other)) {
        unsigned RegNo = FReg - RISCV::F0_F;
        unsigned DReg = RISCV::F0_D + RegNo;
        return std::make_pair(DReg, &RISCV::FPR64RegClass);
      }
      if (VT == MVT::f32 || VT == MVT::Other)
        return std::make_pair(FReg, &RISCV::FPR32RegClass);
      if (Subtarget.hasStdExtZfhmin() && VT == MVT::f16) {
        unsigned RegNo = FReg - RISCV::F0_F;
        unsigned HReg = RISCV::F0_H + RegNo;
        return std::make_pair(HReg, &RISCV::FPR16RegClass);
      }
    }
  }

  if (Subtarget.hasVInstructions()) {
    Register VReg = StringSwitch<Register>(Constraint.lower())
                        .Case("{v0}", RISCV::V0)
                        .Case("{v1}", RISCV::V1)
                        .Case("{v2}", RISCV::V2)
                        .Case("{v3}", RISCV::V3)
                        .Case("{v4}", RISCV::V4)
                        .Case("{v5}", RISCV::V5)
                        .Case("{v6}", RISCV::V6)
                        .Case("{v7}", RISCV::V7)
                        .Case("{v8}", RISCV::V8)
                        .Case("{v9}", RISCV::V9)
                        .Case("{v10}", RISCV::V10)
                        .Case("{v11}", RISCV::V11)
                        .Case("{v12}", RISCV::V12)
                        .Case("{v13}", RISCV::V13)
                        .Case("{v14}", RISCV::V14)
                        .Case("{v15}", RISCV::V15)
                        .Case("{v16}", RISCV::V16)
                        .Case("{v17}", RISCV::V17)
                        .Case("{v18}", RISCV::V18)
                        .Case("{v19}", RISCV::V19)
                        .Case("{v20}", RISCV::V20)
                        .Case("{v21}", RISCV::V21)
                        .Case("{v22}", RISCV::V22)
                        .Case("{v23}", RISCV::V23)
                        .Case("{v24}", RISCV::V24)
                        .Case("{v25}", RISCV::V25)
                        .Case("{v26}", RISCV::V26)
                        .Case("{v27}", RISCV::V27)
                        .Case("{v28}", RISCV::V28)
                        .Case("{v29}", RISCV::V29)
                        .Case("{v30}", RISCV::V30)
                        .Case("{v31}", RISCV::V31)
                        .Default(RISCV::NoRegister);
    if (VReg != RISCV::NoRegister) {
      if (TRI->isTypeLegalForClass(RISCV::VMRegClass, VT.SimpleTy))
        return std::make_pair(VReg, &RISCV::VMRegClass);
      if (TRI->isTypeLegalForClass(RISCV::VRRegClass, VT.SimpleTy))
        return std::make_pair(VReg, &RISCV::VRRegClass);
      for (const auto *RC :
           {&RISCV::VRM2RegClass, &RISCV::VRM4RegClass, &RISCV::VRM8RegClass}) {
        if (TRI->isTypeLegalForClass(*RC, VT.SimpleTy)) {
          VReg = TRI->getMatchingSuperReg(VReg, RISCV::sub_vrm1_0, RC);
          return std::make_pair(VReg, RC);
        }
      }
    }
  }

  std::pair<Register, const TargetRegisterClass *> Res =
      TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);

  // If we picked one of the Zfinx register classes, remap it to the GPR class.
  // FIXME: When Zfinx is supported in CodeGen this will need to take the
  // Subtarget into account.
  if (Res.second == &RISCV::GPRF16RegClass ||
      Res.second == &RISCV::GPRF32RegClass ||
      Res.second == &RISCV::GPRPairRegClass)
    return std::make_pair(Res.first, &RISCV::GPRRegClass);

  return Res;
}

InlineAsm::ConstraintCode
RISCVTargetLowering::getInlineAsmMemConstraint(StringRef ConstraintCode) const {
  // Currently only support length 1 constraints.
  if (ConstraintCode.size() == 1) {
    switch (ConstraintCode[0]) {
    case 'A':
      return InlineAsm::ConstraintCode::A;
    default:
      break;
    }
  }

  return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
}

void RISCVTargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, StringRef Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  // Currently only support length 1 constraints.
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'I':
      // Validate & create a 12-bit signed immediate operand.
      if (auto *C = dyn_cast<ConstantSDNode>(Op)) {
        uint64_t CVal = C->getSExtValue();
        if (isInt<12>(CVal))
          Ops.push_back(
              DAG.getTargetConstant(CVal, SDLoc(Op), Subtarget.getXLenVT()));
      }
      return;
    case 'J':
      // Validate & create an integer zero operand.
      if (isNullConstant(Op))
        Ops.push_back(
            DAG.getTargetConstant(0, SDLoc(Op), Subtarget.getXLenVT()));
      return;
    case 'K':
      // Validate & create a 5-bit unsigned immediate operand.
      if (auto *C = dyn_cast<ConstantSDNode>(Op)) {
        uint64_t CVal = C->getZExtValue();
        if (isUInt<5>(CVal))
          Ops.push_back(
              DAG.getTargetConstant(CVal, SDLoc(Op), Subtarget.getXLenVT()));
      }
      return;
    case 'S':
      TargetLowering::LowerAsmOperandForConstraint(Op, "s", Ops, DAG);
      return;
    default:
      break;
    }
  }
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

Instruction *RISCVTargetLowering::emitLeadingFence(IRBuilderBase &Builder,
                                                   Instruction *Inst,
                                                   AtomicOrdering Ord) const {
  if (Subtarget.hasStdExtZtso()) {
    if (isa<LoadInst>(Inst) && Ord == AtomicOrdering::SequentiallyConsistent)
      return Builder.CreateFence(Ord);
    return nullptr;
  }

  if (isa<LoadInst>(Inst) && Ord == AtomicOrdering::SequentiallyConsistent)
    return Builder.CreateFence(Ord);
  if (isa<StoreInst>(Inst) && isReleaseOrStronger(Ord))
    return Builder.CreateFence(AtomicOrdering::Release);
  return nullptr;
}

Instruction *RISCVTargetLowering::emitTrailingFence(IRBuilderBase &Builder,
                                                    Instruction *Inst,
                                                    AtomicOrdering Ord) const {
  if (Subtarget.hasStdExtZtso()) {
    if (isa<StoreInst>(Inst) && Ord == AtomicOrdering::SequentiallyConsistent)
      return Builder.CreateFence(Ord);
    return nullptr;
  }

  if (isa<LoadInst>(Inst) && isAcquireOrStronger(Ord))
    return Builder.CreateFence(AtomicOrdering::Acquire);
  if (Subtarget.enableTrailingSeqCstFence() && isa<StoreInst>(Inst) &&
      Ord == AtomicOrdering::SequentiallyConsistent)
    return Builder.CreateFence(AtomicOrdering::SequentiallyConsistent);
  return nullptr;
}

TargetLowering::AtomicExpansionKind
RISCVTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  // atomicrmw {fadd,fsub} must be expanded to use compare-exchange, as floating
  // point operations can't be used in an lr/sc sequence without breaking the
  // forward-progress guarantee.
  if (AI->isFloatingPointOperation() ||
      AI->getOperation() == AtomicRMWInst::UIncWrap ||
      AI->getOperation() == AtomicRMWInst::UDecWrap)
    return AtomicExpansionKind::CmpXChg;

  // Don't expand forced atomics, we want to have __sync libcalls instead.
  if (Subtarget.hasForcedAtomics())
    return AtomicExpansionKind::None;

  unsigned Size = AI->getType()->getPrimitiveSizeInBits();
  if (AI->getOperation() == AtomicRMWInst::Nand) {
    if (Subtarget.hasStdExtZacas() &&
        (Size >= 32 || Subtarget.hasStdExtZabha()))
      return AtomicExpansionKind::CmpXChg;
    if (Size < 32)
      return AtomicExpansionKind::MaskedIntrinsic;
  }

  if (Size < 32 && !Subtarget.hasStdExtZabha())
    return AtomicExpansionKind::MaskedIntrinsic;

  return AtomicExpansionKind::None;
}

static Intrinsic::ID
getIntrinsicForMaskedAtomicRMWBinOp(unsigned XLen, AtomicRMWInst::BinOp BinOp) {
  if (XLen == 32) {
    switch (BinOp) {
    default:
      llvm_unreachable("Unexpected AtomicRMW BinOp");
    case AtomicRMWInst::Xchg:
      return Intrinsic::riscv_masked_atomicrmw_xchg_i32;
    case AtomicRMWInst::Add:
      return Intrinsic::riscv_masked_atomicrmw_add_i32;
    case AtomicRMWInst::Sub:
      return Intrinsic::riscv_masked_atomicrmw_sub_i32;
    case AtomicRMWInst::Nand:
      return Intrinsic::riscv_masked_atomicrmw_nand_i32;
    case AtomicRMWInst::Max:
      return Intrinsic::riscv_masked_atomicrmw_max_i32;
    case AtomicRMWInst::Min:
      return Intrinsic::riscv_masked_atomicrmw_min_i32;
    case AtomicRMWInst::UMax:
      return Intrinsic::riscv_masked_atomicrmw_umax_i32;
    case AtomicRMWInst::UMin:
      return Intrinsic::riscv_masked_atomicrmw_umin_i32;
    }
  }

  if (XLen == 64) {
    switch (BinOp) {
    default:
      llvm_unreachable("Unexpected AtomicRMW BinOp");
    case AtomicRMWInst::Xchg:
      return Intrinsic::riscv_masked_atomicrmw_xchg_i64;
    case AtomicRMWInst::Add:
      return Intrinsic::riscv_masked_atomicrmw_add_i64;
    case AtomicRMWInst::Sub:
      return Intrinsic::riscv_masked_atomicrmw_sub_i64;
    case AtomicRMWInst::Nand:
      return Intrinsic::riscv_masked_atomicrmw_nand_i64;
    case AtomicRMWInst::Max:
      return Intrinsic::riscv_masked_atomicrmw_max_i64;
    case AtomicRMWInst::Min:
      return Intrinsic::riscv_masked_atomicrmw_min_i64;
    case AtomicRMWInst::UMax:
      return Intrinsic::riscv_masked_atomicrmw_umax_i64;
    case AtomicRMWInst::UMin:
      return Intrinsic::riscv_masked_atomicrmw_umin_i64;
    }
  }

  llvm_unreachable("Unexpected XLen\n");
}

Value *RISCVTargetLowering::emitMaskedAtomicRMWIntrinsic(
    IRBuilderBase &Builder, AtomicRMWInst *AI, Value *AlignedAddr, Value *Incr,
    Value *Mask, Value *ShiftAmt, AtomicOrdering Ord) const {
  // In the case of an atomicrmw xchg with a constant 0/-1 operand, replace
  // the atomic instruction with an AtomicRMWInst::And/Or with appropriate
  // mask, as this produces better code than the LR/SC loop emitted by
  // int_riscv_masked_atomicrmw_xchg.
  if (AI->getOperation() == AtomicRMWInst::Xchg &&
      isa<ConstantInt>(AI->getValOperand())) {
    ConstantInt *CVal = cast<ConstantInt>(AI->getValOperand());
    if (CVal->isZero())
      return Builder.CreateAtomicRMW(AtomicRMWInst::And, AlignedAddr,
                                     Builder.CreateNot(Mask, "Inv_Mask"),
                                     AI->getAlign(), Ord);
    if (CVal->isMinusOne())
      return Builder.CreateAtomicRMW(AtomicRMWInst::Or, AlignedAddr, Mask,
                                     AI->getAlign(), Ord);
  }

  unsigned XLen = Subtarget.getXLen();
  Value *Ordering =
      Builder.getIntN(XLen, static_cast<uint64_t>(AI->getOrdering()));
  Type *Tys[] = {AlignedAddr->getType()};
  Function *LrwOpScwLoop = Intrinsic::getDeclaration(
      AI->getModule(),
      getIntrinsicForMaskedAtomicRMWBinOp(XLen, AI->getOperation()), Tys);

  if (XLen == 64) {
    Incr = Builder.CreateSExt(Incr, Builder.getInt64Ty());
    Mask = Builder.CreateSExt(Mask, Builder.getInt64Ty());
    ShiftAmt = Builder.CreateSExt(ShiftAmt, Builder.getInt64Ty());
  }

  Value *Result;

  // Must pass the shift amount needed to sign extend the loaded value prior
  // to performing a signed comparison for min/max. ShiftAmt is the number of
  // bits to shift the value into position. Pass XLen-ShiftAmt-ValWidth, which
  // is the number of bits to left+right shift the value in order to
  // sign-extend.
  if (AI->getOperation() == AtomicRMWInst::Min ||
      AI->getOperation() == AtomicRMWInst::Max) {
    const DataLayout &DL = AI->getDataLayout();
    unsigned ValWidth =
        DL.getTypeStoreSizeInBits(AI->getValOperand()->getType());
    Value *SextShamt =
        Builder.CreateSub(Builder.getIntN(XLen, XLen - ValWidth), ShiftAmt);
    Result = Builder.CreateCall(LrwOpScwLoop,
                                {AlignedAddr, Incr, Mask, SextShamt, Ordering});
  } else {
    Result =
        Builder.CreateCall(LrwOpScwLoop, {AlignedAddr, Incr, Mask, Ordering});
  }

  if (XLen == 64)
    Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
  return Result;
}

TargetLowering::AtomicExpansionKind
RISCVTargetLowering::shouldExpandAtomicCmpXchgInIR(
    AtomicCmpXchgInst *CI) const {
  // Don't expand forced atomics, we want to have __sync libcalls instead.
  if (Subtarget.hasForcedAtomics())
    return AtomicExpansionKind::None;

  unsigned Size = CI->getCompareOperand()->getType()->getPrimitiveSizeInBits();
  if (!(Subtarget.hasStdExtZabha() && Subtarget.hasStdExtZacas()) &&
      (Size == 8 || Size == 16))
    return AtomicExpansionKind::MaskedIntrinsic;
  return AtomicExpansionKind::None;
}

Value *RISCVTargetLowering::emitMaskedAtomicCmpXchgIntrinsic(
    IRBuilderBase &Builder, AtomicCmpXchgInst *CI, Value *AlignedAddr,
    Value *CmpVal, Value *NewVal, Value *Mask, AtomicOrdering Ord) const {
  unsigned XLen = Subtarget.getXLen();
  Value *Ordering = Builder.getIntN(XLen, static_cast<uint64_t>(Ord));
  Intrinsic::ID CmpXchgIntrID = Intrinsic::riscv_masked_cmpxchg_i32;
  if (XLen == 64) {
    CmpVal = Builder.CreateSExt(CmpVal, Builder.getInt64Ty());
    NewVal = Builder.CreateSExt(NewVal, Builder.getInt64Ty());
    Mask = Builder.CreateSExt(Mask, Builder.getInt64Ty());
    CmpXchgIntrID = Intrinsic::riscv_masked_cmpxchg_i64;
  }
  Type *Tys[] = {AlignedAddr->getType()};
  Function *MaskedCmpXchg =
      Intrinsic::getDeclaration(CI->getModule(), CmpXchgIntrID, Tys);
  Value *Result = Builder.CreateCall(
      MaskedCmpXchg, {AlignedAddr, CmpVal, NewVal, Mask, Ordering});
  if (XLen == 64)
    Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
  return Result;
}

bool RISCVTargetLowering::shouldRemoveExtendFromGSIndex(SDValue Extend,
                                                        EVT DataVT) const {
  // We have indexed loads for all supported EEW types. Indices are always
  // zero extended.
  return Extend.getOpcode() == ISD::ZERO_EXTEND &&
         isTypeLegal(Extend.getValueType()) &&
         isTypeLegal(Extend.getOperand(0).getValueType()) &&
         Extend.getOperand(0).getValueType().getVectorElementType() != MVT::i1;
}

bool RISCVTargetLowering::shouldConvertFpToSat(unsigned Op, EVT FPVT,
                                               EVT VT) const {
  if (!isOperationLegalOrCustom(Op, VT) || !FPVT.isSimple())
    return false;

  switch (FPVT.getSimpleVT().SimpleTy) {
  case MVT::f16:
    return Subtarget.hasStdExtZfhmin();
  case MVT::f32:
    return Subtarget.hasStdExtF();
  case MVT::f64:
    return Subtarget.hasStdExtD();
  default:
    return false;
  }
}

unsigned RISCVTargetLowering::getJumpTableEncoding() const {
  // If we are using the small code model, we can reduce size of jump table
  // entry to 4 bytes.
  if (Subtarget.is64Bit() && !isPositionIndependent() &&
      getTargetMachine().getCodeModel() == CodeModel::Small) {
    return MachineJumpTableInfo::EK_Custom32;
  }
  return TargetLowering::getJumpTableEncoding();
}

const MCExpr *RISCVTargetLowering::LowerCustomJumpTableEntry(
    const MachineJumpTableInfo *MJTI, const MachineBasicBlock *MBB,
    unsigned uid, MCContext &Ctx) const {
  assert(Subtarget.is64Bit() && !isPositionIndependent() &&
         getTargetMachine().getCodeModel() == CodeModel::Small);
  return MCSymbolRefExpr::create(MBB->getSymbol(), Ctx);
}

bool RISCVTargetLowering::isVScaleKnownToBeAPowerOfTwo() const {
  // We define vscale to be VLEN/RVVBitsPerBlock.  VLEN is always a power
  // of two >= 64, and RVVBitsPerBlock is 64.  Thus, vscale must be
  // a power of two as well.
  // FIXME: This doesn't work for zve32, but that's already broken
  // elsewhere for the same reason.
  assert(Subtarget.getRealMinVLen() >= 64 && "zve32* unsupported");
  static_assert(RISCV::RVVBitsPerBlock == 64,
                "RVVBitsPerBlock changed, audit needed");
  return true;
}

bool RISCVTargetLowering::getIndexedAddressParts(SDNode *Op, SDValue &Base,
                                                 SDValue &Offset,
                                                 ISD::MemIndexedMode &AM,
                                                 SelectionDAG &DAG) const {
  // Target does not support indexed loads.
  if (!Subtarget.hasVendorXTHeadMemIdx())
    return false;

  if (Op->getOpcode() != ISD::ADD && Op->getOpcode() != ISD::SUB)
    return false;

  Base = Op->getOperand(0);
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1))) {
    int64_t RHSC = RHS->getSExtValue();
    if (Op->getOpcode() == ISD::SUB)
      RHSC = -(uint64_t)RHSC;

    // The constants that can be encoded in the THeadMemIdx instructions
    // are of the form (sign_extend(imm5) << imm2).
    bool isLegalIndexedOffset = false;
    for (unsigned i = 0; i < 4; i++)
      if (isInt<5>(RHSC >> i) && ((RHSC % (1LL << i)) == 0)) {
        isLegalIndexedOffset = true;
        break;
      }

    if (!isLegalIndexedOffset)
      return false;

    Offset = Op->getOperand(1);
    return true;
  }

  return false;
}

bool RISCVTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                    SDValue &Offset,
                                                    ISD::MemIndexedMode &AM,
                                                    SelectionDAG &DAG) const {
  EVT VT;
  SDValue Ptr;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
  } else
    return false;

  if (!getIndexedAddressParts(Ptr.getNode(), Base, Offset, AM, DAG))
    return false;

  AM = ISD::PRE_INC;
  return true;
}

bool RISCVTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                     SDValue &Base,
                                                     SDValue &Offset,
                                                     ISD::MemIndexedMode &AM,
                                                     SelectionDAG &DAG) const {
  if (Subtarget.hasVendorXCVmem()) {
    if (Op->getOpcode() != ISD::ADD)
      return false;

    if (LSBaseSDNode *LS = dyn_cast<LSBaseSDNode>(N))
      Base = LS->getBasePtr();
    else
      return false;

    if (Base == Op->getOperand(0))
      Offset = Op->getOperand(1);
    else if (Base == Op->getOperand(1))
      Offset = Op->getOperand(0);
    else
      return false;

    AM = ISD::POST_INC;
    return true;
  }

  EVT VT;
  SDValue Ptr;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
  } else
    return false;

  if (!getIndexedAddressParts(Op, Base, Offset, AM, DAG))
    return false;
  // Post-indexing updates the base, so it's not a valid transform
  // if that's not the same as the load's pointer.
  if (Ptr != Base)
    return false;

  AM = ISD::POST_INC;
  return true;
}

bool RISCVTargetLowering::isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                                     EVT VT) const {
  EVT SVT = VT.getScalarType();

  if (!SVT.isSimple())
    return false;

  switch (SVT.getSimpleVT().SimpleTy) {
  case MVT::f16:
    return VT.isVector() ? Subtarget.hasVInstructionsF16()
                         : Subtarget.hasStdExtZfhOrZhinx();
  case MVT::f32:
    return Subtarget.hasStdExtFOrZfinx();
  case MVT::f64:
    return Subtarget.hasStdExtDOrZdinx();
  default:
    break;
  }

  return false;
}

ISD::NodeType RISCVTargetLowering::getExtendForAtomicCmpSwapArg() const {
  // Zacas will use amocas.w which does not require extension.
  return Subtarget.hasStdExtZacas() ? ISD::ANY_EXTEND : ISD::SIGN_EXTEND;
}

Register RISCVTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return RISCV::X10;
}

Register RISCVTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return RISCV::X11;
}

bool RISCVTargetLowering::shouldExtendTypeInLibCall(EVT Type) const {
  // Return false to suppress the unnecessary extensions if the LibCall
  // arguments or return value is a float narrower than XLEN on a soft FP ABI.
  if (Subtarget.isSoftFPABI() && (Type.isFloatingPoint() && !Type.isVector() &&
                                  Type.getSizeInBits() < Subtarget.getXLen()))
    return false;

  return true;
}

bool RISCVTargetLowering::shouldSignExtendTypeInLibCall(EVT Type, bool IsSigned) const {
  if (Subtarget.is64Bit() && Type == MVT::i32)
    return true;

  return IsSigned;
}

bool RISCVTargetLowering::decomposeMulByConstant(LLVMContext &Context, EVT VT,
                                                 SDValue C) const {
  // Check integral scalar types.
  const bool HasZmmul = Subtarget.hasStdExtZmmul();
  if (!VT.isScalarInteger())
    return false;

  // Omit the optimization if the sub target has the M extension and the data
  // size exceeds XLen.
  if (HasZmmul && VT.getSizeInBits() > Subtarget.getXLen())
    return false;

  if (auto *ConstNode = dyn_cast<ConstantSDNode>(C.getNode())) {
    // Break the MUL to a SLLI and an ADD/SUB.
    const APInt &Imm = ConstNode->getAPIntValue();
    if ((Imm + 1).isPowerOf2() || (Imm - 1).isPowerOf2() ||
        (1 - Imm).isPowerOf2() || (-1 - Imm).isPowerOf2())
      return true;

    // Optimize the MUL to (SH*ADD x, (SLLI x, bits)) if Imm is not simm12.
    if (Subtarget.hasStdExtZba() && !Imm.isSignedIntN(12) &&
        ((Imm - 2).isPowerOf2() || (Imm - 4).isPowerOf2() ||
         (Imm - 8).isPowerOf2()))
      return true;

    // Break the MUL to two SLLI instructions and an ADD/SUB, if Imm needs
    // a pair of LUI/ADDI.
    if (!Imm.isSignedIntN(12) && Imm.countr_zero() < 12 &&
        ConstNode->hasOneUse()) {
      APInt ImmS = Imm.ashr(Imm.countr_zero());
      if ((ImmS + 1).isPowerOf2() || (ImmS - 1).isPowerOf2() ||
          (1 - ImmS).isPowerOf2())
        return true;
    }
  }

  return false;
}

bool RISCVTargetLowering::isMulAddWithConstProfitable(SDValue AddNode,
                                                      SDValue ConstNode) const {
  // Let the DAGCombiner decide for vectors.
  EVT VT = AddNode.getValueType();
  if (VT.isVector())
    return true;

  // Let the DAGCombiner decide for larger types.
  if (VT.getScalarSizeInBits() > Subtarget.getXLen())
    return true;

  // It is worse if c1 is simm12 while c1*c2 is not.
  ConstantSDNode *C1Node = cast<ConstantSDNode>(AddNode.getOperand(1));
  ConstantSDNode *C2Node = cast<ConstantSDNode>(ConstNode);
  const APInt &C1 = C1Node->getAPIntValue();
  const APInt &C2 = C2Node->getAPIntValue();
  if (C1.isSignedIntN(12) && !(C1 * C2).isSignedIntN(12))
    return false;

  // Default to true and let the DAGCombiner decide.
  return true;
}

bool RISCVTargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned AddrSpace, Align Alignment, MachineMemOperand::Flags Flags,
    unsigned *Fast) const {
  if (!VT.isVector()) {
    if (Fast)
      *Fast = Subtarget.enableUnalignedScalarMem();
    return Subtarget.enableUnalignedScalarMem();
  }

  // All vector implementations must support element alignment
  EVT ElemVT = VT.getVectorElementType();
  if (Alignment >= ElemVT.getStoreSize()) {
    if (Fast)
      *Fast = 1;
    return true;
  }

  // Note: We lower an unmasked unaligned vector access to an equally sized
  // e8 element type access.  Given this, we effectively support all unmasked
  // misaligned accesses.  TODO: Work through the codegen implications of
  // allowing such accesses to be formed, and considered fast.
  if (Fast)
    *Fast = Subtarget.enableUnalignedVectorMem();
  return Subtarget.enableUnalignedVectorMem();
}


EVT RISCVTargetLowering::getOptimalMemOpType(const MemOp &Op,
                                             const AttributeList &FuncAttributes) const {
  if (!Subtarget.hasVInstructions())
    return MVT::Other;

  if (FuncAttributes.hasFnAttr(Attribute::NoImplicitFloat))
    return MVT::Other;

  // We use LMUL1 memory operations here for a non-obvious reason.  Our caller
  // has an expansion threshold, and we want the number of hardware memory
  // operations to correspond roughly to that threshold.  LMUL>1 operations
  // are typically expanded linearly internally, and thus correspond to more
  // than one actual memory operation.  Note that store merging and load
  // combining will typically form larger LMUL operations from the LMUL1
  // operations emitted here, and that's okay because combining isn't
  // introducing new memory operations; it's just merging existing ones.
  const unsigned MinVLenInBytes = Subtarget.getRealMinVLen()/8;
  if (Op.size() < MinVLenInBytes)
    // TODO: Figure out short memops.  For the moment, do the default thing
    // which ends up using scalar sequences.
    return MVT::Other;

  // Prefer i8 for non-zero memset as it allows us to avoid materializing
  // a large scalar constant and instead use vmv.v.x/i to do the
  // broadcast.  For everything else, prefer ELenVT to minimize VL and thus
  // maximize the chance we can encode the size in the vsetvli.
  MVT ELenVT = MVT::getIntegerVT(Subtarget.getELen());
  MVT PreferredVT = (Op.isMemset() && !Op.isZeroMemset()) ? MVT::i8 : ELenVT;

  // Do we have sufficient alignment for our preferred VT?  If not, revert
  // to largest size allowed by our alignment criteria.
  if (PreferredVT != MVT::i8 && !Subtarget.enableUnalignedVectorMem()) {
    Align RequiredAlign(PreferredVT.getStoreSize());
    if (Op.isFixedDstAlign())
      RequiredAlign = std::min(RequiredAlign, Op.getDstAlign());
    if (Op.isMemcpy())
      RequiredAlign = std::min(RequiredAlign, Op.getSrcAlign());
    PreferredVT = MVT::getIntegerVT(RequiredAlign.value() * 8);
  }
  return MVT::getVectorVT(PreferredVT, MinVLenInBytes/PreferredVT.getStoreSize());
}

bool RISCVTargetLowering::splitValueIntoRegisterParts(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
    unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC) const {
  bool IsABIRegCopy = CC.has_value();
  EVT ValueVT = Val.getValueType();
  if (IsABIRegCopy && (ValueVT == MVT::f16 || ValueVT == MVT::bf16) &&
      PartVT == MVT::f32) {
    // Cast the [b]f16 to i16, extend to i32, pad with ones to make a float
    // nan, and cast to f32.
    Val = DAG.getNode(ISD::BITCAST, DL, MVT::i16, Val);
    Val = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Val);
    Val = DAG.getNode(ISD::OR, DL, MVT::i32, Val,
                      DAG.getConstant(0xFFFF0000, DL, MVT::i32));
    Val = DAG.getNode(ISD::BITCAST, DL, MVT::f32, Val);
    Parts[0] = Val;
    return true;
  }

  if (ValueVT.isScalableVector() && PartVT.isScalableVector()) {
    LLVMContext &Context = *DAG.getContext();
    EVT ValueEltVT = ValueVT.getVectorElementType();
    EVT PartEltVT = PartVT.getVectorElementType();
    unsigned ValueVTBitSize = ValueVT.getSizeInBits().getKnownMinValue();
    unsigned PartVTBitSize = PartVT.getSizeInBits().getKnownMinValue();
    if (PartVTBitSize % ValueVTBitSize == 0) {
      assert(PartVTBitSize >= ValueVTBitSize);
      // If the element types are different, bitcast to the same element type of
      // PartVT first.
      // Give an example here, we want copy a <vscale x 1 x i8> value to
      // <vscale x 4 x i16>.
      // We need to convert <vscale x 1 x i8> to <vscale x 8 x i8> by insert
      // subvector, then we can bitcast to <vscale x 4 x i16>.
      if (ValueEltVT != PartEltVT) {
        if (PartVTBitSize > ValueVTBitSize) {
          unsigned Count = PartVTBitSize / ValueEltVT.getFixedSizeInBits();
          assert(Count != 0 && "The number of element should not be zero.");
          EVT SameEltTypeVT =
              EVT::getVectorVT(Context, ValueEltVT, Count, /*IsScalable=*/true);
          Val = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, SameEltTypeVT,
                            DAG.getUNDEF(SameEltTypeVT), Val,
                            DAG.getVectorIdxConstant(0, DL));
        }
        Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
      } else {
        Val =
            DAG.getNode(ISD::INSERT_SUBVECTOR, DL, PartVT, DAG.getUNDEF(PartVT),
                        Val, DAG.getVectorIdxConstant(0, DL));
      }
      Parts[0] = Val;
      return true;
    }
  }
  return false;
}

SDValue RISCVTargetLowering::joinRegisterPartsIntoValue(
    SelectionDAG &DAG, const SDLoc &DL, const SDValue *Parts, unsigned NumParts,
    MVT PartVT, EVT ValueVT, std::optional<CallingConv::ID> CC) const {
  bool IsABIRegCopy = CC.has_value();
  if (IsABIRegCopy && (ValueVT == MVT::f16 || ValueVT == MVT::bf16) &&
      PartVT == MVT::f32) {
    SDValue Val = Parts[0];

    // Cast the f32 to i32, truncate to i16, and cast back to [b]f16.
    Val = DAG.getNode(ISD::BITCAST, DL, MVT::i32, Val);
    Val = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Val);
    Val = DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);
    return Val;
  }

  if (ValueVT.isScalableVector() && PartVT.isScalableVector()) {
    LLVMContext &Context = *DAG.getContext();
    SDValue Val = Parts[0];
    EVT ValueEltVT = ValueVT.getVectorElementType();
    EVT PartEltVT = PartVT.getVectorElementType();
    unsigned ValueVTBitSize = ValueVT.getSizeInBits().getKnownMinValue();
    unsigned PartVTBitSize = PartVT.getSizeInBits().getKnownMinValue();
    if (PartVTBitSize % ValueVTBitSize == 0) {
      assert(PartVTBitSize >= ValueVTBitSize);
      EVT SameEltTypeVT = ValueVT;
      // If the element types are different, convert it to the same element type
      // of PartVT.
      // Give an example here, we want copy a <vscale x 1 x i8> value from
      // <vscale x 4 x i16>.
      // We need to convert <vscale x 4 x i16> to <vscale x 8 x i8> first,
      // then we can extract <vscale x 1 x i8>.
      if (ValueEltVT != PartEltVT) {
        unsigned Count = PartVTBitSize / ValueEltVT.getFixedSizeInBits();
        assert(Count != 0 && "The number of element should not be zero.");
        SameEltTypeVT =
            EVT::getVectorVT(Context, ValueEltVT, Count, /*IsScalable=*/true);
        Val = DAG.getNode(ISD::BITCAST, DL, SameEltTypeVT, Val);
      }
      Val = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ValueVT, Val,
                        DAG.getVectorIdxConstant(0, DL));
      return Val;
    }
  }
  return SDValue();
}

bool RISCVTargetLowering::isIntDivCheap(EVT VT, AttributeList Attr) const {
  // When aggressively optimizing for code size, we prefer to use a div
  // instruction, as it is usually smaller than the alternative sequence.
  // TODO: Add vector division?
  bool OptSize = Attr.hasFnAttr(Attribute::MinSize);
  return OptSize && !VT.isVector();
}

bool RISCVTargetLowering::preferScalarizeSplat(SDNode *N) const {
  // Scalarize zero_ext and sign_ext might stop match to widening instruction in
  // some situation.
  unsigned Opc = N->getOpcode();
  if (Opc == ISD::ZERO_EXTEND || Opc == ISD::SIGN_EXTEND)
    return false;
  return true;
}

static Value *useTpOffset(IRBuilderBase &IRB, unsigned Offset) {
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  Function *ThreadPointerFunc =
      Intrinsic::getDeclaration(M, Intrinsic::thread_pointer);
  return IRB.CreateConstGEP1_32(IRB.getInt8Ty(),
                                IRB.CreateCall(ThreadPointerFunc), Offset);
}

Value *RISCVTargetLowering::getIRStackGuard(IRBuilderBase &IRB) const {
  // Fuchsia provides a fixed TLS slot for the stack cookie.
  // <zircon/tls.h> defines ZX_TLS_STACK_GUARD_OFFSET with this value.
  if (Subtarget.isTargetFuchsia())
    return useTpOffset(IRB, -0x10);

  // Android provides a fixed TLS slot for the stack cookie. See the definition
  // of TLS_SLOT_STACK_GUARD in
  // https://android.googlesource.com/platform/bionic/+/main/libc/platform/bionic/tls_defines.h
  if (Subtarget.isTargetAndroid())
    return useTpOffset(IRB, -0x18);

  return TargetLowering::getIRStackGuard(IRB);
}

bool RISCVTargetLowering::isLegalInterleavedAccessType(
    VectorType *VTy, unsigned Factor, Align Alignment, unsigned AddrSpace,
    const DataLayout &DL) const {
  EVT VT = getValueType(DL, VTy);
  // Don't lower vlseg/vsseg for vector types that can't be split.
  if (!isTypeLegal(VT))
    return false;

  if (!isLegalElementTypeForRVV(VT.getScalarType()) ||
      !allowsMemoryAccessForAlignment(VTy->getContext(), DL, VT, AddrSpace,
                                      Alignment))
    return false;

  MVT ContainerVT = VT.getSimpleVT();

  if (auto *FVTy = dyn_cast<FixedVectorType>(VTy)) {
    if (!Subtarget.useRVVForFixedLengthVectors())
      return false;
    // Sometimes the interleaved access pass picks up splats as interleaves of
    // one element. Don't lower these.
    if (FVTy->getNumElements() < 2)
      return false;

    ContainerVT = getContainerForFixedLengthVector(VT.getSimpleVT());
  } else {
    // The intrinsics for scalable vectors are not overloaded on pointer type
    // and can only handle the default address space.
    if (AddrSpace)
      return false;
  }

  // Need to make sure that EMUL * NFIELDS ≤ 8
  auto [LMUL, Fractional] = RISCVVType::decodeVLMUL(getLMUL(ContainerVT));
  if (Fractional)
    return true;
  return Factor * LMUL <= 8;
}

bool RISCVTargetLowering::isLegalStridedLoadStore(EVT DataType,
                                                  Align Alignment) const {
  if (!Subtarget.hasVInstructions())
    return false;

  // Only support fixed vectors if we know the minimum vector size.
  if (DataType.isFixedLengthVector() && !Subtarget.useRVVForFixedLengthVectors())
    return false;

  EVT ScalarType = DataType.getScalarType();
  if (!isLegalElementTypeForRVV(ScalarType))
    return false;

  if (!Subtarget.enableUnalignedVectorMem() &&
      Alignment < ScalarType.getStoreSize())
    return false;

  return true;
}

static const Intrinsic::ID FixedVlsegIntrIds[] = {
    Intrinsic::riscv_seg2_load, Intrinsic::riscv_seg3_load,
    Intrinsic::riscv_seg4_load, Intrinsic::riscv_seg5_load,
    Intrinsic::riscv_seg6_load, Intrinsic::riscv_seg7_load,
    Intrinsic::riscv_seg8_load};

/// Lower an interleaved load into a vlsegN intrinsic.
///
/// E.g. Lower an interleaved load (Factor = 2):
/// %wide.vec = load <8 x i32>, <8 x i32>* %ptr
/// %v0 = shuffle %wide.vec, undef, <0, 2, 4, 6>  ; Extract even elements
/// %v1 = shuffle %wide.vec, undef, <1, 3, 5, 7>  ; Extract odd elements
///
/// Into:
/// %ld2 = { <4 x i32>, <4 x i32> } call llvm.riscv.seg2.load.v4i32.p0.i64(
///                                        %ptr, i64 4)
/// %vec0 = extractelement { <4 x i32>, <4 x i32> } %ld2, i32 0
/// %vec1 = extractelement { <4 x i32>, <4 x i32> } %ld2, i32 1
bool RISCVTargetLowering::lowerInterleavedLoad(
    LoadInst *LI, ArrayRef<ShuffleVectorInst *> Shuffles,
    ArrayRef<unsigned> Indices, unsigned Factor) const {
  IRBuilder<> Builder(LI);

  auto *VTy = cast<FixedVectorType>(Shuffles[0]->getType());
  if (!isLegalInterleavedAccessType(VTy, Factor, LI->getAlign(),
                                    LI->getPointerAddressSpace(),
                                    LI->getDataLayout()))
    return false;

  auto *XLenTy = Type::getIntNTy(LI->getContext(), Subtarget.getXLen());

  Function *VlsegNFunc =
      Intrinsic::getDeclaration(LI->getModule(), FixedVlsegIntrIds[Factor - 2],
                                {VTy, LI->getPointerOperandType(), XLenTy});

  Value *VL = ConstantInt::get(XLenTy, VTy->getNumElements());

  CallInst *VlsegN =
      Builder.CreateCall(VlsegNFunc, {LI->getPointerOperand(), VL});

  for (unsigned i = 0; i < Shuffles.size(); i++) {
    Value *SubVec = Builder.CreateExtractValue(VlsegN, Indices[i]);
    Shuffles[i]->replaceAllUsesWith(SubVec);
  }

  return true;
}

static const Intrinsic::ID FixedVssegIntrIds[] = {
    Intrinsic::riscv_seg2_store, Intrinsic::riscv_seg3_store,
    Intrinsic::riscv_seg4_store, Intrinsic::riscv_seg5_store,
    Intrinsic::riscv_seg6_store, Intrinsic::riscv_seg7_store,
    Intrinsic::riscv_seg8_store};

/// Lower an interleaved store into a vssegN intrinsic.
///
/// E.g. Lower an interleaved store (Factor = 3):
/// %i.vec = shuffle <8 x i32> %v0, <8 x i32> %v1,
///                  <0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11>
/// store <12 x i32> %i.vec, <12 x i32>* %ptr
///
/// Into:
/// %sub.v0 = shuffle <8 x i32> %v0, <8 x i32> v1, <0, 1, 2, 3>
/// %sub.v1 = shuffle <8 x i32> %v0, <8 x i32> v1, <4, 5, 6, 7>
/// %sub.v2 = shuffle <8 x i32> %v0, <8 x i32> v1, <8, 9, 10, 11>
/// call void llvm.riscv.seg3.store.v4i32.p0.i64(%sub.v0, %sub.v1, %sub.v2,
///                                              %ptr, i32 4)
///
/// Note that the new shufflevectors will be removed and we'll only generate one
/// vsseg3 instruction in CodeGen.
bool RISCVTargetLowering::lowerInterleavedStore(StoreInst *SI,
                                                ShuffleVectorInst *SVI,
                                                unsigned Factor) const {
  IRBuilder<> Builder(SI);
  auto *ShuffleVTy = cast<FixedVectorType>(SVI->getType());
  // Given SVI : <n*factor x ty>, then VTy : <n x ty>
  auto *VTy = FixedVectorType::get(ShuffleVTy->getElementType(),
                                   ShuffleVTy->getNumElements() / Factor);
  if (!isLegalInterleavedAccessType(VTy, Factor, SI->getAlign(),
                                    SI->getPointerAddressSpace(),
                                    SI->getDataLayout()))
    return false;

  auto *XLenTy = Type::getIntNTy(SI->getContext(), Subtarget.getXLen());

  Function *VssegNFunc =
      Intrinsic::getDeclaration(SI->getModule(), FixedVssegIntrIds[Factor - 2],
                                {VTy, SI->getPointerOperandType(), XLenTy});

  auto Mask = SVI->getShuffleMask();
  SmallVector<Value *, 10> Ops;

  for (unsigned i = 0; i < Factor; i++) {
    Value *Shuffle = Builder.CreateShuffleVector(
        SVI->getOperand(0), SVI->getOperand(1),
        createSequentialMask(Mask[i], VTy->getNumElements(), 0));
    Ops.push_back(Shuffle);
  }
  // This VL should be OK (should be executable in one vsseg instruction,
  // potentially under larger LMULs) because we checked that the fixed vector
  // type fits in isLegalInterleavedAccessType
  Value *VL = ConstantInt::get(XLenTy, VTy->getNumElements());
  Ops.append({SI->getPointerOperand(), VL});

  Builder.CreateCall(VssegNFunc, Ops);

  return true;
}

bool RISCVTargetLowering::lowerDeinterleaveIntrinsicToLoad(IntrinsicInst *DI,
                                                           LoadInst *LI) const {
  assert(LI->isSimple());
  IRBuilder<> Builder(LI);

  // Only deinterleave2 supported at present.
  if (DI->getIntrinsicID() != Intrinsic::vector_deinterleave2)
    return false;

  unsigned Factor = 2;

  VectorType *VTy = cast<VectorType>(DI->getOperand(0)->getType());
  VectorType *ResVTy = cast<VectorType>(DI->getType()->getContainedType(0));

  if (!isLegalInterleavedAccessType(ResVTy, Factor, LI->getAlign(),
                                    LI->getPointerAddressSpace(),
                                    LI->getDataLayout()))
    return false;

  Function *VlsegNFunc;
  Value *VL;
  Type *XLenTy = Type::getIntNTy(LI->getContext(), Subtarget.getXLen());
  SmallVector<Value *, 10> Ops;

  if (auto *FVTy = dyn_cast<FixedVectorType>(VTy)) {
    VlsegNFunc = Intrinsic::getDeclaration(
        LI->getModule(), FixedVlsegIntrIds[Factor - 2],
        {ResVTy, LI->getPointerOperandType(), XLenTy});
    VL = ConstantInt::get(XLenTy, FVTy->getNumElements());
  } else {
    static const Intrinsic::ID IntrIds[] = {
        Intrinsic::riscv_vlseg2, Intrinsic::riscv_vlseg3,
        Intrinsic::riscv_vlseg4, Intrinsic::riscv_vlseg5,
        Intrinsic::riscv_vlseg6, Intrinsic::riscv_vlseg7,
        Intrinsic::riscv_vlseg8};

    VlsegNFunc = Intrinsic::getDeclaration(LI->getModule(), IntrIds[Factor - 2],
                                           {ResVTy, XLenTy});
    VL = Constant::getAllOnesValue(XLenTy);
    Ops.append(Factor, PoisonValue::get(ResVTy));
  }

  Ops.append({LI->getPointerOperand(), VL});

  Value *Vlseg = Builder.CreateCall(VlsegNFunc, Ops);
  DI->replaceAllUsesWith(Vlseg);

  return true;
}

bool RISCVTargetLowering::lowerInterleaveIntrinsicToStore(IntrinsicInst *II,
                                                          StoreInst *SI) const {
  assert(SI->isSimple());
  IRBuilder<> Builder(SI);

  // Only interleave2 supported at present.
  if (II->getIntrinsicID() != Intrinsic::vector_interleave2)
    return false;

  unsigned Factor = 2;

  VectorType *VTy = cast<VectorType>(II->getType());
  VectorType *InVTy = cast<VectorType>(II->getOperand(0)->getType());

  if (!isLegalInterleavedAccessType(InVTy, Factor, SI->getAlign(),
                                    SI->getPointerAddressSpace(),
                                    SI->getDataLayout()))
    return false;

  Function *VssegNFunc;
  Value *VL;
  Type *XLenTy = Type::getIntNTy(SI->getContext(), Subtarget.getXLen());

  if (auto *FVTy = dyn_cast<FixedVectorType>(VTy)) {
    VssegNFunc = Intrinsic::getDeclaration(
        SI->getModule(), FixedVssegIntrIds[Factor - 2],
        {InVTy, SI->getPointerOperandType(), XLenTy});
    VL = ConstantInt::get(XLenTy, FVTy->getNumElements());
  } else {
    static const Intrinsic::ID IntrIds[] = {
        Intrinsic::riscv_vsseg2, Intrinsic::riscv_vsseg3,
        Intrinsic::riscv_vsseg4, Intrinsic::riscv_vsseg5,
        Intrinsic::riscv_vsseg6, Intrinsic::riscv_vsseg7,
        Intrinsic::riscv_vsseg8};

    VssegNFunc = Intrinsic::getDeclaration(SI->getModule(), IntrIds[Factor - 2],
                                           {InVTy, XLenTy});
    VL = Constant::getAllOnesValue(XLenTy);
  }

  Builder.CreateCall(VssegNFunc, {II->getOperand(0), II->getOperand(1),
                                  SI->getPointerOperand(), VL});

  return true;
}

MachineInstr *
RISCVTargetLowering::EmitKCFICheck(MachineBasicBlock &MBB,
                                   MachineBasicBlock::instr_iterator &MBBI,
                                   const TargetInstrInfo *TII) const {
  assert(MBBI->isCall() && MBBI->getCFIType() &&
         "Invalid call instruction for a KCFI check");
  assert(is_contained({RISCV::PseudoCALLIndirect, RISCV::PseudoTAILIndirect},
                      MBBI->getOpcode()));

  MachineOperand &Target = MBBI->getOperand(0);
  Target.setIsRenamable(false);

  return BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(RISCV::KCFI_CHECK))
      .addReg(Target.getReg())
      .addImm(MBBI->getCFIType())
      .getInstr();
}

#define GET_REGISTER_MATCHER
#include "RISCVGenAsmMatcher.inc"

Register
RISCVTargetLowering::getRegisterByName(const char *RegName, LLT VT,
                                       const MachineFunction &MF) const {
  Register Reg = MatchRegisterAltName(RegName);
  if (Reg == RISCV::NoRegister)
    Reg = MatchRegisterName(RegName);
  if (Reg == RISCV::NoRegister)
    report_fatal_error(
        Twine("Invalid register name \"" + StringRef(RegName) + "\"."));
  BitVector ReservedRegs = Subtarget.getRegisterInfo()->getReservedRegs(MF);
  if (!ReservedRegs.test(Reg) && !Subtarget.isRegisterReservedByUser(Reg))
    report_fatal_error(Twine("Trying to obtain non-reserved register \"" +
                             StringRef(RegName) + "\"."));
  return Reg;
}

MachineMemOperand::Flags
RISCVTargetLowering::getTargetMMOFlags(const Instruction &I) const {
  const MDNode *NontemporalInfo = I.getMetadata(LLVMContext::MD_nontemporal);

  if (NontemporalInfo == nullptr)
    return MachineMemOperand::MONone;

  // 1 for default value work as __RISCV_NTLH_ALL
  // 2 -> __RISCV_NTLH_INNERMOST_PRIVATE
  // 3 -> __RISCV_NTLH_ALL_PRIVATE
  // 4 -> __RISCV_NTLH_INNERMOST_SHARED
  // 5 -> __RISCV_NTLH_ALL
  int NontemporalLevel = 5;
  const MDNode *RISCVNontemporalInfo =
      I.getMetadata("riscv-nontemporal-domain");
  if (RISCVNontemporalInfo != nullptr)
    NontemporalLevel =
        cast<ConstantInt>(
            cast<ConstantAsMetadata>(RISCVNontemporalInfo->getOperand(0))
                ->getValue())
            ->getZExtValue();

  assert((1 <= NontemporalLevel && NontemporalLevel <= 5) &&
         "RISC-V target doesn't support this non-temporal domain.");

  NontemporalLevel -= 2;
  MachineMemOperand::Flags Flags = MachineMemOperand::MONone;
  if (NontemporalLevel & 0b1)
    Flags |= MONontemporalBit0;
  if (NontemporalLevel & 0b10)
    Flags |= MONontemporalBit1;

  return Flags;
}

MachineMemOperand::Flags
RISCVTargetLowering::getTargetMMOFlags(const MemSDNode &Node) const {

  MachineMemOperand::Flags NodeFlags = Node.getMemOperand()->getFlags();
  MachineMemOperand::Flags TargetFlags = MachineMemOperand::MONone;
  TargetFlags |= (NodeFlags & MONontemporalBit0);
  TargetFlags |= (NodeFlags & MONontemporalBit1);
  return TargetFlags;
}

bool RISCVTargetLowering::areTwoSDNodeTargetMMOFlagsMergeable(
    const MemSDNode &NodeX, const MemSDNode &NodeY) const {
  return getTargetMMOFlags(NodeX) == getTargetMMOFlags(NodeY);
}

bool RISCVTargetLowering::isCtpopFast(EVT VT) const {
  if (VT.isScalableVector())
    return isTypeLegal(VT) && Subtarget.hasStdExtZvbb();
  if (VT.isFixedLengthVector() && Subtarget.hasStdExtZvbb())
    return true;
  return Subtarget.hasStdExtZbb() &&
         (VT == MVT::i32 || VT == MVT::i64 || VT.isFixedLengthVector());
}

unsigned RISCVTargetLowering::getCustomCtpopCost(EVT VT,
                                                 ISD::CondCode Cond) const {
  return isCtpopFast(VT) ? 0 : 1;
}

bool RISCVTargetLowering::fallBackToDAGISel(const Instruction &Inst) const {

  // GISel support is in progress or complete for these opcodes.
  unsigned Op = Inst.getOpcode();
  if (Op == Instruction::Add || Op == Instruction::Sub ||
      Op == Instruction::And || Op == Instruction::Or ||
      Op == Instruction::Xor || Op == Instruction::InsertElement ||
      Op == Instruction::ShuffleVector || Op == Instruction::Load ||
      Op == Instruction::Freeze || Op == Instruction::Store)
    return false;

  if (Inst.getType()->isScalableTy())
    return true;

  for (unsigned i = 0; i < Inst.getNumOperands(); ++i)
    if (Inst.getOperand(i)->getType()->isScalableTy() &&
        !isa<ReturnInst>(&Inst))
      return true;

  if (const AllocaInst *AI = dyn_cast<AllocaInst>(&Inst)) {
    if (AI->getAllocatedType()->isScalableTy())
      return true;
  }

  return false;
}

SDValue
RISCVTargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                   SelectionDAG &DAG,
                                   SmallVectorImpl<SDNode *> &Created) const {
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  if (isIntDivCheap(N->getValueType(0), Attr))
    return SDValue(N, 0); // Lower SDIV as SDIV

  // Only perform this transform if short forward branch opt is supported.
  if (!Subtarget.hasShortForwardBranchOpt())
    return SDValue();
  EVT VT = N->getValueType(0);
  if (!(VT == MVT::i32 || (VT == MVT::i64 && Subtarget.is64Bit())))
    return SDValue();

  // Ensure 2**k-1 < 2048 so that we can just emit a single addi/addiw.
  if (Divisor.sgt(2048) || Divisor.slt(-2048))
    return SDValue();
  return TargetLowering::buildSDIVPow2WithCMov(N, Divisor, DAG, Created);
}

bool RISCVTargetLowering::shouldFoldSelectWithSingleBitTest(
    EVT VT, const APInt &AndMask) const {
  if (Subtarget.hasStdExtZicond() || Subtarget.hasVendorXVentanaCondOps())
    return !Subtarget.hasStdExtZbs() && AndMask.ugt(1024);
  return TargetLowering::shouldFoldSelectWithSingleBitTest(VT, AndMask);
}

unsigned RISCVTargetLowering::getMinimumJumpTableEntries() const {
  return Subtarget.getMinimumJumpTableEntries();
}

// Handle single arg such as return value.
template <typename Arg>
void RVVArgDispatcher::constructArgInfos(ArrayRef<Arg> ArgList) {
  // This lambda determines whether an array of types are constructed by
  // homogeneous vector types.
  auto isHomogeneousScalableVectorType = [](ArrayRef<Arg> ArgList) {
    // First, extract the first element in the argument type.
    auto It = ArgList.begin();
    MVT FirstArgRegType = It->VT;

    // Return if there is no return or the type needs split.
    if (It == ArgList.end() || It->Flags.isSplit())
      return false;

    ++It;

    // Return if this argument type contains only 1 element, or it's not a
    // vector type.
    if (It == ArgList.end() || !FirstArgRegType.isScalableVector())
      return false;

    // Second, check if the following elements in this argument type are all the
    // same.
    for (; It != ArgList.end(); ++It)
      if (It->Flags.isSplit() || It->VT != FirstArgRegType)
        return false;

    return true;
  };

  if (isHomogeneousScalableVectorType(ArgList)) {
    // Handle as tuple type
    RVVArgInfos.push_back({(unsigned)ArgList.size(), ArgList[0].VT, false});
  } else {
    // Handle as normal vector type
    bool FirstVMaskAssigned = false;
    for (const auto &OutArg : ArgList) {
      MVT RegisterVT = OutArg.VT;

      // Skip non-RVV register type
      if (!RegisterVT.isVector())
        continue;

      if (RegisterVT.isFixedLengthVector())
        RegisterVT = TLI->getContainerForFixedLengthVector(RegisterVT);

      if (!FirstVMaskAssigned && RegisterVT.getVectorElementType() == MVT::i1) {
        RVVArgInfos.push_back({1, RegisterVT, true});
        FirstVMaskAssigned = true;
        continue;
      }

      RVVArgInfos.push_back({1, RegisterVT, false});
    }
  }
}

// Handle multiple args.
template <>
void RVVArgDispatcher::constructArgInfos<Type *>(ArrayRef<Type *> TypeList) {
  const DataLayout &DL = MF->getDataLayout();
  const Function &F = MF->getFunction();
  LLVMContext &Context = F.getContext();

  bool FirstVMaskAssigned = false;
  for (Type *Ty : TypeList) {
    StructType *STy = dyn_cast<StructType>(Ty);
    if (STy && STy->containsHomogeneousScalableVectorTypes()) {
      Type *ElemTy = STy->getTypeAtIndex(0U);
      EVT VT = TLI->getValueType(DL, ElemTy);
      MVT RegisterVT =
          TLI->getRegisterTypeForCallingConv(Context, F.getCallingConv(), VT);
      unsigned NumRegs =
          TLI->getNumRegistersForCallingConv(Context, F.getCallingConv(), VT);

      RVVArgInfos.push_back(
          {NumRegs * STy->getNumElements(), RegisterVT, false});
    } else {
      SmallVector<EVT, 4> ValueVTs;
      ComputeValueVTs(*TLI, DL, Ty, ValueVTs);

      for (unsigned Value = 0, NumValues = ValueVTs.size(); Value != NumValues;
           ++Value) {
        EVT VT = ValueVTs[Value];
        MVT RegisterVT =
            TLI->getRegisterTypeForCallingConv(Context, F.getCallingConv(), VT);
        unsigned NumRegs =
            TLI->getNumRegistersForCallingConv(Context, F.getCallingConv(), VT);

        // Skip non-RVV register type
        if (!RegisterVT.isVector())
          continue;

        if (RegisterVT.isFixedLengthVector())
          RegisterVT = TLI->getContainerForFixedLengthVector(RegisterVT);

        if (!FirstVMaskAssigned &&
            RegisterVT.getVectorElementType() == MVT::i1) {
          RVVArgInfos.push_back({1, RegisterVT, true});
          FirstVMaskAssigned = true;
          --NumRegs;
        }

        RVVArgInfos.insert(RVVArgInfos.end(), NumRegs, {1, RegisterVT, false});
      }
    }
  }
}

void RVVArgDispatcher::allocatePhysReg(unsigned NF, unsigned LMul,
                                       unsigned StartReg) {
  assert((StartReg % LMul) == 0 &&
         "Start register number should be multiple of lmul");
  const MCPhysReg *VRArrays;
  switch (LMul) {
  default:
    report_fatal_error("Invalid lmul");
  case 1:
    VRArrays = ArgVRs;
    break;
  case 2:
    VRArrays = ArgVRM2s;
    break;
  case 4:
    VRArrays = ArgVRM4s;
    break;
  case 8:
    VRArrays = ArgVRM8s;
    break;
  }

  for (unsigned i = 0; i < NF; ++i)
    if (StartReg)
      AllocatedPhysRegs.push_back(VRArrays[(StartReg - 8) / LMul + i]);
    else
      AllocatedPhysRegs.push_back(MCPhysReg());
}

/// This function determines if each RVV argument is passed by register, if the
/// argument can be assigned to a VR, then give it a specific register.
/// Otherwise, assign the argument to 0 which is a invalid MCPhysReg.
void RVVArgDispatcher::compute() {
  uint32_t AssignedMap = 0;
  auto allocate = [&](const RVVArgInfo &ArgInfo) {
    // Allocate first vector mask argument to V0.
    if (ArgInfo.FirstVMask) {
      AllocatedPhysRegs.push_back(RISCV::V0);
      return;
    }

    unsigned RegsNeeded = divideCeil(
        ArgInfo.VT.getSizeInBits().getKnownMinValue(), RISCV::RVVBitsPerBlock);
    unsigned TotalRegsNeeded = ArgInfo.NF * RegsNeeded;
    for (unsigned StartReg = 0; StartReg + TotalRegsNeeded <= NumArgVRs;
         StartReg += RegsNeeded) {
      uint32_t Map = ((1 << TotalRegsNeeded) - 1) << StartReg;
      if ((AssignedMap & Map) == 0) {
        allocatePhysReg(ArgInfo.NF, RegsNeeded, StartReg + 8);
        AssignedMap |= Map;
        return;
      }
    }

    allocatePhysReg(ArgInfo.NF, RegsNeeded, 0);
  };

  for (unsigned i = 0; i < RVVArgInfos.size(); ++i)
    allocate(RVVArgInfos[i]);
}

MCPhysReg RVVArgDispatcher::getNextPhysReg() {
  assert(CurIdx < AllocatedPhysRegs.size() && "Index out of range");
  return AllocatedPhysRegs[CurIdx++];
}

SDValue RISCVTargetLowering::expandIndirectJTBranch(const SDLoc &dl,
                                                    SDValue Value, SDValue Addr,
                                                    int JTI,
                                                    SelectionDAG &DAG) const {
  if (Subtarget.hasStdExtZicfilp()) {
    // When Zicfilp enabled, we need to use software guarded branch for jump
    // table branch.
    SDValue JTInfo = DAG.getJumpTableDebugInfo(JTI, Value, dl);
    return DAG.getNode(RISCVISD::SW_GUARDED_BRIND, dl, MVT::Other, JTInfo,
                       Addr);
  }
  return TargetLowering::expandIndirectJTBranch(dl, Value, Addr, JTI, DAG);
}

namespace llvm::RISCVVIntrinsicsTable {

#define GET_RISCVVIntrinsicsTable_IMPL
#include "RISCVGenSearchableTables.inc"

} // namespace llvm::RISCVVIntrinsicsTable
