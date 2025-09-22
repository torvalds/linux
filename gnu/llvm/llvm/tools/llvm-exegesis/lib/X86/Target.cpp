//===-- Target.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "../Target.h"

#include "../Error.h"
#include "../MmapUtils.h"
#include "../ParallelSnippetGenerator.h"
#include "../SerialSnippetGenerator.h"
#include "../SnippetGenerator.h"
#include "../SubprocessMemory.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86.h"
#include "X86Counter.h"
#include "X86RegisterInfo.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/TargetParser/Host.h"

#include <memory>
#include <string>
#include <vector>
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <immintrin.h>
#include <intrin.h>
#endif
#if defined(_MSC_VER) && defined(_M_X64)
#include <float.h> // For _clearfp in ~X86SavedState().
#endif

#ifdef __linux__
#ifdef __x86_64__
#include <asm/prctl.h>
#endif // __x86_64__
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifdef HAVE_LIBPFM
#include <perfmon/perf_event.h>
#endif // HAVE_LIBPFM
#endif

#define GET_AVAILABLE_OPCODE_CHECKER
#include "X86GenInstrInfo.inc"

namespace llvm {
namespace exegesis {

// If a positive value is specified, we are going to use the LBR in
// latency-mode.
//
// Note:
//  -  A small value is preferred, but too low a value could result in
//     throttling.
//  -  A prime number is preferred to avoid always skipping certain blocks.
//
static cl::opt<unsigned> LbrSamplingPeriod(
    "x86-lbr-sample-period",
    cl::desc("The sample period (nbranches/sample), used for LBR sampling"),
    cl::cat(BenchmarkOptions), cl::init(0));

static cl::opt<bool>
    DisableUpperSSERegisters("x86-disable-upper-sse-registers",
                             cl::desc("Disable XMM8-XMM15 register usage"),
                             cl::cat(BenchmarkOptions), cl::init(false));

// FIXME: Validates that repetition-mode is loop if LBR is requested.

// Returns a non-null reason if we cannot handle the memory references in this
// instruction.
static const char *isInvalidMemoryInstr(const Instruction &Instr) {
  switch (Instr.Description.TSFlags & X86II::FormMask) {
  default:
    return "Unknown FormMask value";
  // These have no memory access.
  case X86II::Pseudo:
  case X86II::RawFrm:
  case X86II::AddCCFrm:
  case X86II::PrefixByte:
  case X86II::MRMDestReg:
  case X86II::MRMSrcReg:
  case X86II::MRMSrcReg4VOp3:
  case X86II::MRMSrcRegOp4:
  case X86II::MRMSrcRegCC:
  case X86II::MRMXrCC:
  case X86II::MRMr0:
  case X86II::MRMXr:
  case X86II::MRM0r:
  case X86II::MRM1r:
  case X86II::MRM2r:
  case X86II::MRM3r:
  case X86II::MRM4r:
  case X86II::MRM5r:
  case X86II::MRM6r:
  case X86II::MRM7r:
  case X86II::MRM0X:
  case X86II::MRM1X:
  case X86II::MRM2X:
  case X86II::MRM3X:
  case X86II::MRM4X:
  case X86II::MRM5X:
  case X86II::MRM6X:
  case X86II::MRM7X:
  case X86II::MRM_C0:
  case X86II::MRM_C1:
  case X86II::MRM_C2:
  case X86II::MRM_C3:
  case X86II::MRM_C4:
  case X86II::MRM_C5:
  case X86II::MRM_C6:
  case X86II::MRM_C7:
  case X86II::MRM_C8:
  case X86II::MRM_C9:
  case X86II::MRM_CA:
  case X86II::MRM_CB:
  case X86II::MRM_CC:
  case X86II::MRM_CD:
  case X86II::MRM_CE:
  case X86II::MRM_CF:
  case X86II::MRM_D0:
  case X86II::MRM_D1:
  case X86II::MRM_D2:
  case X86II::MRM_D3:
  case X86II::MRM_D4:
  case X86II::MRM_D5:
  case X86II::MRM_D6:
  case X86II::MRM_D7:
  case X86II::MRM_D8:
  case X86II::MRM_D9:
  case X86II::MRM_DA:
  case X86II::MRM_DB:
  case X86II::MRM_DC:
  case X86II::MRM_DD:
  case X86II::MRM_DE:
  case X86II::MRM_DF:
  case X86II::MRM_E0:
  case X86II::MRM_E1:
  case X86II::MRM_E2:
  case X86II::MRM_E3:
  case X86II::MRM_E4:
  case X86II::MRM_E5:
  case X86II::MRM_E6:
  case X86II::MRM_E7:
  case X86II::MRM_E8:
  case X86II::MRM_E9:
  case X86II::MRM_EA:
  case X86II::MRM_EB:
  case X86II::MRM_EC:
  case X86II::MRM_ED:
  case X86II::MRM_EE:
  case X86II::MRM_EF:
  case X86II::MRM_F0:
  case X86II::MRM_F1:
  case X86II::MRM_F2:
  case X86II::MRM_F3:
  case X86II::MRM_F4:
  case X86II::MRM_F5:
  case X86II::MRM_F6:
  case X86II::MRM_F7:
  case X86II::MRM_F8:
  case X86II::MRM_F9:
  case X86II::MRM_FA:
  case X86II::MRM_FB:
  case X86II::MRM_FC:
  case X86II::MRM_FD:
  case X86II::MRM_FE:
  case X86II::MRM_FF:
  case X86II::RawFrmImm8:
    return nullptr;
  case X86II::AddRegFrm:
    return (Instr.Description.Opcode == X86::POP16r ||
            Instr.Description.Opcode == X86::POP32r ||
            Instr.Description.Opcode == X86::PUSH16r ||
            Instr.Description.Opcode == X86::PUSH32r)
               ? "unsupported opcode: unsupported memory access"
               : nullptr;
  // These access memory and are handled.
  case X86II::MRMDestMem:
  case X86II::MRMSrcMem:
  case X86II::MRMSrcMem4VOp3:
  case X86II::MRMSrcMemOp4:
  case X86II::MRMSrcMemCC:
  case X86II::MRMXmCC:
  case X86II::MRMXm:
  case X86II::MRM0m:
  case X86II::MRM1m:
  case X86II::MRM2m:
  case X86II::MRM3m:
  case X86II::MRM4m:
  case X86II::MRM5m:
  case X86II::MRM6m:
  case X86II::MRM7m:
    return nullptr;
  // These access memory and are not handled yet.
  case X86II::RawFrmImm16:
  case X86II::RawFrmMemOffs:
  case X86II::RawFrmSrc:
  case X86II::RawFrmDst:
  case X86II::RawFrmDstSrc:
    return "unsupported opcode: non uniform memory access";
  }
}

// If the opcode is invalid, returns a pointer to a character literal indicating
// the reason. nullptr indicates a valid opcode.
static const char *isInvalidOpcode(const Instruction &Instr) {
  const auto OpcodeName = Instr.Name;
  if ((Instr.Description.TSFlags & X86II::FormMask) == X86II::Pseudo)
    return "unsupported opcode: pseudo instruction";
  if ((OpcodeName.starts_with("POP") && !OpcodeName.starts_with("POPCNT")) ||
      OpcodeName.starts_with("PUSH") ||
      OpcodeName.starts_with("ADJCALLSTACK") || OpcodeName.starts_with("LEAVE"))
    return "unsupported opcode: Push/Pop/AdjCallStack/Leave";
  switch (Instr.Description.Opcode) {
  case X86::LFS16rm:
  case X86::LFS32rm:
  case X86::LFS64rm:
  case X86::LGS16rm:
  case X86::LGS32rm:
  case X86::LGS64rm:
  case X86::LSS16rm:
  case X86::LSS32rm:
  case X86::LSS64rm:
  case X86::SYSENTER:
  case X86::WRFSBASE:
  case X86::WRFSBASE64:
    return "unsupported opcode";
  default:
    break;
  }
  if (const auto reason = isInvalidMemoryInstr(Instr))
    return reason;
  // We do not handle instructions with OPERAND_PCREL.
  for (const Operand &Op : Instr.Operands)
    if (Op.isExplicit() &&
        Op.getExplicitOperandInfo().OperandType == MCOI::OPERAND_PCREL)
      return "unsupported opcode: PC relative operand";
  // We do not handle second-form X87 instructions. We only handle first-form
  // ones (_Fp), see comment in X86InstrFPStack.td.
  for (const Operand &Op : Instr.Operands)
    if (Op.isReg() && Op.isExplicit() &&
        Op.getExplicitOperandInfo().RegClass == X86::RSTRegClassID)
      return "unsupported second-form X87 instruction";
  return nullptr;
}

static unsigned getX86FPFlags(const Instruction &Instr) {
  return Instr.Description.TSFlags & X86II::FPTypeMask;
}

// Helper to fill a memory operand with a value.
static void setMemOp(InstructionTemplate &IT, int OpIdx,
                     const MCOperand &OpVal) {
  const auto Op = IT.getInstr().Operands[OpIdx];
  assert(Op.isExplicit() && "invalid memory pattern");
  IT.getValueFor(Op) = OpVal;
}

// Common (latency, uops) code for LEA templates. `GetDestReg` takes the
// addressing base and index registers and returns the LEA destination register.
static Expected<std::vector<CodeTemplate>> generateLEATemplatesCommon(
    const Instruction &Instr, const BitVector &ForbiddenRegisters,
    const LLVMState &State, const SnippetGenerator::Options &Opts,
    std::function<void(unsigned, unsigned, BitVector &CandidateDestRegs)>
        RestrictDestRegs) {
  assert(Instr.Operands.size() == 6 && "invalid LEA");
  assert(X86II::getMemoryOperandNo(Instr.Description.TSFlags) == 1 &&
         "invalid LEA");

  constexpr const int kDestOp = 0;
  constexpr const int kBaseOp = 1;
  constexpr const int kIndexOp = 3;
  auto PossibleDestRegs =
      Instr.Operands[kDestOp].getRegisterAliasing().sourceBits();
  remove(PossibleDestRegs, ForbiddenRegisters);
  auto PossibleBaseRegs =
      Instr.Operands[kBaseOp].getRegisterAliasing().sourceBits();
  remove(PossibleBaseRegs, ForbiddenRegisters);
  auto PossibleIndexRegs =
      Instr.Operands[kIndexOp].getRegisterAliasing().sourceBits();
  remove(PossibleIndexRegs, ForbiddenRegisters);

  const auto &RegInfo = State.getRegInfo();
  std::vector<CodeTemplate> Result;
  for (const unsigned BaseReg : PossibleBaseRegs.set_bits()) {
    for (const unsigned IndexReg : PossibleIndexRegs.set_bits()) {
      for (int LogScale = 0; LogScale <= 3; ++LogScale) {
        // FIXME: Add an option for controlling how we explore immediates.
        for (const int Disp : {0, 42}) {
          InstructionTemplate IT(&Instr);
          const int64_t Scale = 1ull << LogScale;
          setMemOp(IT, 1, MCOperand::createReg(BaseReg));
          setMemOp(IT, 2, MCOperand::createImm(Scale));
          setMemOp(IT, 3, MCOperand::createReg(IndexReg));
          setMemOp(IT, 4, MCOperand::createImm(Disp));
          // SegmentReg must be 0 for LEA.
          setMemOp(IT, 5, MCOperand::createReg(0));

          // Output reg candidates are selected by the caller.
          auto PossibleDestRegsNow = PossibleDestRegs;
          RestrictDestRegs(BaseReg, IndexReg, PossibleDestRegsNow);
          assert(PossibleDestRegsNow.set_bits().begin() !=
                     PossibleDestRegsNow.set_bits().end() &&
                 "no remaining registers");
          setMemOp(
              IT, 0,
              MCOperand::createReg(*PossibleDestRegsNow.set_bits().begin()));

          CodeTemplate CT;
          CT.Instructions.push_back(std::move(IT));
          CT.Config = formatv("{3}(%{0}, %{1}, {2})", RegInfo.getName(BaseReg),
                              RegInfo.getName(IndexReg), Scale, Disp)
                          .str();
          Result.push_back(std::move(CT));
          if (Result.size() >= Opts.MaxConfigsPerOpcode)
            return std::move(Result);
        }
      }
    }
  }

  return std::move(Result);
}

namespace {
class X86SerialSnippetGenerator : public SerialSnippetGenerator {
public:
  using SerialSnippetGenerator::SerialSnippetGenerator;

  Expected<std::vector<CodeTemplate>>
  generateCodeTemplates(InstructionTemplate Variant,
                        const BitVector &ForbiddenRegisters) const override;
};
} // namespace

Expected<std::vector<CodeTemplate>>
X86SerialSnippetGenerator::generateCodeTemplates(
    InstructionTemplate Variant, const BitVector &ForbiddenRegisters) const {
  const Instruction &Instr = Variant.getInstr();

  if (const auto reason = isInvalidOpcode(Instr))
    return make_error<Failure>(reason);

  // LEA gets special attention.
  const auto Opcode = Instr.Description.getOpcode();
  if (Opcode == X86::LEA64r || Opcode == X86::LEA64_32r) {
    return generateLEATemplatesCommon(
        Instr, ForbiddenRegisters, State, Opts,
        [this](unsigned BaseReg, unsigned IndexReg,
               BitVector &CandidateDestRegs) {
          // We just select a destination register that aliases the base
          // register.
          CandidateDestRegs &=
              State.getRATC().getRegister(BaseReg).aliasedBits();
        });
  }

  if (Instr.hasMemoryOperands())
    return make_error<Failure>(
        "unsupported memory operand in latency measurements");

  switch (getX86FPFlags(Instr)) {
  case X86II::NotFP:
    return SerialSnippetGenerator::generateCodeTemplates(Variant,
                                                         ForbiddenRegisters);
  case X86II::ZeroArgFP:
  case X86II::OneArgFP:
  case X86II::SpecialFP:
  case X86II::CompareFP:
  case X86II::CondMovFP:
    return make_error<Failure>("Unsupported x87 Instruction");
  case X86II::OneArgFPRW:
  case X86II::TwoArgFP:
    // These are instructions like
    //   - `ST(0) = fsqrt(ST(0))` (OneArgFPRW)
    //   - `ST(0) = ST(0) + ST(i)` (TwoArgFP)
    // They are intrinsically serial and do not modify the state of the stack.
    return generateSelfAliasingCodeTemplates(Variant, ForbiddenRegisters);
  default:
    llvm_unreachable("Unknown FP Type!");
  }
}

namespace {
class X86ParallelSnippetGenerator : public ParallelSnippetGenerator {
public:
  using ParallelSnippetGenerator::ParallelSnippetGenerator;

  Expected<std::vector<CodeTemplate>>
  generateCodeTemplates(InstructionTemplate Variant,
                        const BitVector &ForbiddenRegisters) const override;
};

} // namespace

Expected<std::vector<CodeTemplate>>
X86ParallelSnippetGenerator::generateCodeTemplates(
    InstructionTemplate Variant, const BitVector &ForbiddenRegisters) const {
  const Instruction &Instr = Variant.getInstr();

  if (const auto reason = isInvalidOpcode(Instr))
    return make_error<Failure>(reason);

  // LEA gets special attention.
  const auto Opcode = Instr.Description.getOpcode();
  if (Opcode == X86::LEA64r || Opcode == X86::LEA64_32r) {
    return generateLEATemplatesCommon(
        Instr, ForbiddenRegisters, State, Opts,
        [this](unsigned BaseReg, unsigned IndexReg,
               BitVector &CandidateDestRegs) {
          // Any destination register that is not used for addressing is fine.
          remove(CandidateDestRegs,
                 State.getRATC().getRegister(BaseReg).aliasedBits());
          remove(CandidateDestRegs,
                 State.getRATC().getRegister(IndexReg).aliasedBits());
        });
  }

  switch (getX86FPFlags(Instr)) {
  case X86II::NotFP:
    return ParallelSnippetGenerator::generateCodeTemplates(Variant,
                                                           ForbiddenRegisters);
  case X86II::ZeroArgFP:
  case X86II::OneArgFP:
  case X86II::SpecialFP:
    return make_error<Failure>("Unsupported x87 Instruction");
  case X86II::OneArgFPRW:
  case X86II::TwoArgFP:
    // These are instructions like
    //   - `ST(0) = fsqrt(ST(0))` (OneArgFPRW)
    //   - `ST(0) = ST(0) + ST(i)` (TwoArgFP)
    // They are intrinsically serial and do not modify the state of the stack.
    // We generate the same code for latency and uops.
    return generateSelfAliasingCodeTemplates(Variant, ForbiddenRegisters);
  case X86II::CompareFP:
  case X86II::CondMovFP:
    // We can compute uops for any FP instruction that does not grow or shrink
    // the stack (either do not touch the stack or push as much as they pop).
    return generateUnconstrainedCodeTemplates(
        Variant, "instruction does not grow/shrink the FP stack");
  default:
    llvm_unreachable("Unknown FP Type!");
  }
}

static unsigned getLoadImmediateOpcode(unsigned RegBitWidth) {
  switch (RegBitWidth) {
  case 8:
    return X86::MOV8ri;
  case 16:
    return X86::MOV16ri;
  case 32:
    return X86::MOV32ri;
  case 64:
    return X86::MOV64ri;
  }
  llvm_unreachable("Invalid Value Width");
}

// Generates instruction to load an immediate value into a register.
static MCInst loadImmediate(unsigned Reg, unsigned RegBitWidth,
                            const APInt &Value) {
  if (Value.getBitWidth() > RegBitWidth)
    llvm_unreachable("Value must fit in the Register");
  return MCInstBuilder(getLoadImmediateOpcode(RegBitWidth))
      .addReg(Reg)
      .addImm(Value.getZExtValue());
}

// Allocates scratch memory on the stack.
static MCInst allocateStackSpace(unsigned Bytes) {
  return MCInstBuilder(X86::SUB64ri8)
      .addReg(X86::RSP)
      .addReg(X86::RSP)
      .addImm(Bytes);
}

// Fills scratch memory at offset `OffsetBytes` with value `Imm`.
static MCInst fillStackSpace(unsigned MovOpcode, unsigned OffsetBytes,
                             uint64_t Imm) {
  return MCInstBuilder(MovOpcode)
      // Address = ESP
      .addReg(X86::RSP)    // BaseReg
      .addImm(1)           // ScaleAmt
      .addReg(0)           // IndexReg
      .addImm(OffsetBytes) // Disp
      .addReg(0)           // Segment
      // Immediate.
      .addImm(Imm);
}

// Loads scratch memory into register `Reg` using opcode `RMOpcode`.
static MCInst loadToReg(unsigned Reg, unsigned RMOpcode) {
  return MCInstBuilder(RMOpcode)
      .addReg(Reg)
      // Address = ESP
      .addReg(X86::RSP) // BaseReg
      .addImm(1)        // ScaleAmt
      .addReg(0)        // IndexReg
      .addImm(0)        // Disp
      .addReg(0);       // Segment
}

// Releases scratch memory.
static MCInst releaseStackSpace(unsigned Bytes) {
  return MCInstBuilder(X86::ADD64ri8)
      .addReg(X86::RSP)
      .addReg(X86::RSP)
      .addImm(Bytes);
}

// Reserves some space on the stack, fills it with the content of the provided
// constant and provide methods to load the stack value into a register.
namespace {
struct ConstantInliner {
  explicit ConstantInliner(const APInt &Constant) : Constant_(Constant) {}

  std::vector<MCInst> loadAndFinalize(unsigned Reg, unsigned RegBitWidth,
                                      unsigned Opcode);

  std::vector<MCInst> loadX87STAndFinalize(unsigned Reg);

  std::vector<MCInst> loadX87FPAndFinalize(unsigned Reg);

  std::vector<MCInst> popFlagAndFinalize();

  std::vector<MCInst> loadImplicitRegAndFinalize(unsigned Opcode,
                                                 unsigned Value);

private:
  ConstantInliner &add(const MCInst &Inst) {
    Instructions.push_back(Inst);
    return *this;
  }

  void initStack(unsigned Bytes);

  static constexpr const unsigned kF80Bytes = 10; // 80 bits.

  APInt Constant_;
  std::vector<MCInst> Instructions;
};
} // namespace

std::vector<MCInst> ConstantInliner::loadAndFinalize(unsigned Reg,
                                                     unsigned RegBitWidth,
                                                     unsigned Opcode) {
  assert((RegBitWidth & 7) == 0 && "RegBitWidth must be a multiple of 8 bits");
  initStack(RegBitWidth / 8);
  add(loadToReg(Reg, Opcode));
  add(releaseStackSpace(RegBitWidth / 8));
  return std::move(Instructions);
}

std::vector<MCInst> ConstantInliner::loadX87STAndFinalize(unsigned Reg) {
  initStack(kF80Bytes);
  add(MCInstBuilder(X86::LD_F80m)
          // Address = ESP
          .addReg(X86::RSP) // BaseReg
          .addImm(1)        // ScaleAmt
          .addReg(0)        // IndexReg
          .addImm(0)        // Disp
          .addReg(0));      // Segment
  if (Reg != X86::ST0)
    add(MCInstBuilder(X86::ST_Frr).addReg(Reg));
  add(releaseStackSpace(kF80Bytes));
  return std::move(Instructions);
}

std::vector<MCInst> ConstantInliner::loadX87FPAndFinalize(unsigned Reg) {
  initStack(kF80Bytes);
  add(MCInstBuilder(X86::LD_Fp80m)
          .addReg(Reg)
          // Address = ESP
          .addReg(X86::RSP) // BaseReg
          .addImm(1)        // ScaleAmt
          .addReg(0)        // IndexReg
          .addImm(0)        // Disp
          .addReg(0));      // Segment
  add(releaseStackSpace(kF80Bytes));
  return std::move(Instructions);
}

std::vector<MCInst> ConstantInliner::popFlagAndFinalize() {
  initStack(8);
  add(MCInstBuilder(X86::POPF64));
  return std::move(Instructions);
}

std::vector<MCInst>
ConstantInliner::loadImplicitRegAndFinalize(unsigned Opcode, unsigned Value) {
  add(allocateStackSpace(4));
  add(fillStackSpace(X86::MOV32mi, 0, Value)); // Mask all FP exceptions
  add(MCInstBuilder(Opcode)
          // Address = ESP
          .addReg(X86::RSP) // BaseReg
          .addImm(1)        // ScaleAmt
          .addReg(0)        // IndexReg
          .addImm(0)        // Disp
          .addReg(0));      // Segment
  add(releaseStackSpace(4));
  return std::move(Instructions);
}

void ConstantInliner::initStack(unsigned Bytes) {
  assert(Constant_.getBitWidth() <= Bytes * 8 &&
         "Value does not have the correct size");
  const APInt WideConstant = Constant_.getBitWidth() < Bytes * 8
                                 ? Constant_.sext(Bytes * 8)
                                 : Constant_;
  add(allocateStackSpace(Bytes));
  size_t ByteOffset = 0;
  for (; Bytes - ByteOffset >= 4; ByteOffset += 4)
    add(fillStackSpace(
        X86::MOV32mi, ByteOffset,
        WideConstant.extractBits(32, ByteOffset * 8).getZExtValue()));
  if (Bytes - ByteOffset >= 2) {
    add(fillStackSpace(
        X86::MOV16mi, ByteOffset,
        WideConstant.extractBits(16, ByteOffset * 8).getZExtValue()));
    ByteOffset += 2;
  }
  if (Bytes - ByteOffset >= 1)
    add(fillStackSpace(
        X86::MOV8mi, ByteOffset,
        WideConstant.extractBits(8, ByteOffset * 8).getZExtValue()));
}

#include "X86GenExegesis.inc"

namespace {

class X86SavedState : public ExegesisTarget::SavedState {
public:
  X86SavedState() {
#if defined(_MSC_VER) && defined(_M_X64)
    _fxsave64(FPState);
    Eflags = __readeflags();
#elif defined(__GNUC__) && defined(__x86_64__)
    __builtin_ia32_fxsave64(FPState);
    Eflags = __builtin_ia32_readeflags_u64();
#else
    report_fatal_error("X86 exegesis running on unsupported target");
#endif
  }

  ~X86SavedState() {
    // Restoring the X87 state does not flush pending exceptions, make sure
    // these exceptions are flushed now.
#if defined(_MSC_VER) && defined(_M_X64)
    _clearfp();
    _fxrstor64(FPState);
    __writeeflags(Eflags);
#elif defined(__GNUC__) && defined(__x86_64__)
    asm volatile("fwait");
    __builtin_ia32_fxrstor64(FPState);
    __builtin_ia32_writeeflags_u64(Eflags);
#else
    report_fatal_error("X86 exegesis running on unsupported target");
#endif
  }

private:
#if defined(__x86_64__) || defined(_M_X64)
  alignas(16) char FPState[512];
  uint64_t Eflags;
#endif
};

class ExegesisX86Target : public ExegesisTarget {
public:
  ExegesisX86Target()
      : ExegesisTarget(X86CpuPfmCounters, X86_MC::isOpcodeAvailable) {}

  Expected<std::unique_ptr<pfm::CounterGroup>>
  createCounter(StringRef CounterName, const LLVMState &State,
                ArrayRef<const char *> ValidationCounters,
                const pid_t ProcessID) const override {
    // If LbrSamplingPeriod was provided, then ignore the
    // CounterName because we only have one for LBR.
    if (LbrSamplingPeriod > 0) {
      // Can't use LBR without HAVE_LIBPFM, LIBPFM_HAS_FIELD_CYCLES, or without
      // __linux__ (for now)
#if defined(HAVE_LIBPFM) && defined(LIBPFM_HAS_FIELD_CYCLES) &&                \
    defined(__linux__)
      // TODO(boomanaiden154): Add in support for using validation counters when
      // using LBR counters.
      if (ValidationCounters.size() > 0)
        return make_error<StringError>(
            "Using LBR is not currently supported with validation counters",
            errc::invalid_argument);

      return std::make_unique<X86LbrCounter>(
          X86LbrPerfEvent(LbrSamplingPeriod));
#else
      return make_error<StringError>(
          "LBR counter requested without HAVE_LIBPFM, LIBPFM_HAS_FIELD_CYCLES, "
          "or running on Linux.",
          errc::invalid_argument);
#endif
    }
    return ExegesisTarget::createCounter(CounterName, State, ValidationCounters,
                                         ProcessID);
  }

  enum ArgumentRegisters { CodeSize = X86::R12, AuxiliaryMemoryFD = X86::R13 };

private:
  void addTargetSpecificPasses(PassManagerBase &PM) const override;

  unsigned getScratchMemoryRegister(const Triple &TT) const override;

  unsigned getDefaultLoopCounterRegister(const Triple &) const override;

  unsigned getMaxMemoryAccessSize() const override { return 64; }

  Error randomizeTargetMCOperand(const Instruction &Instr, const Variable &Var,
                                 MCOperand &AssignedValue,
                                 const BitVector &ForbiddenRegs) const override;

  void fillMemoryOperands(InstructionTemplate &IT, unsigned Reg,
                          unsigned Offset) const override;

  void decrementLoopCounterAndJump(MachineBasicBlock &MBB,
                                   MachineBasicBlock &TargetMBB,
                                   const MCInstrInfo &MII,
                                   unsigned LoopRegister) const override;

  std::vector<MCInst> setRegTo(const MCSubtargetInfo &STI, unsigned Reg,
                               const APInt &Value) const override;

#ifdef __linux__
  void generateLowerMunmap(std::vector<MCInst> &GeneratedCode) const override;

  void generateUpperMunmap(std::vector<MCInst> &GeneratedCode) const override;

  std::vector<MCInst> generateExitSyscall(unsigned ExitCode) const override;

  std::vector<MCInst>
  generateMmap(intptr_t Address, size_t Length,
               intptr_t FileDescriptorAddress) const override;

  void generateMmapAuxMem(std::vector<MCInst> &GeneratedCode) const override;

  void moveArgumentRegisters(std::vector<MCInst> &GeneratedCode) const override;

  std::vector<MCInst> generateMemoryInitialSetup() const override;

  std::vector<MCInst> setStackRegisterToAuxMem() const override;

  intptr_t getAuxiliaryMemoryStartAddress() const override;

  std::vector<MCInst> configurePerfCounter(long Request, bool SaveRegisters) const override;

  std::vector<unsigned> getArgumentRegisters() const override;

  std::vector<unsigned> getRegistersNeedSaving() const override;
#endif // __linux__

  ArrayRef<unsigned> getUnavailableRegisters() const override {
    if (DisableUpperSSERegisters)
      return ArrayRef(kUnavailableRegistersSSE);

    return ArrayRef(kUnavailableRegisters);
  }

  bool allowAsBackToBack(const Instruction &Instr) const override {
    const unsigned Opcode = Instr.Description.Opcode;
    return !isInvalidOpcode(Instr) && Opcode != X86::LEA64r &&
           Opcode != X86::LEA64_32r && Opcode != X86::LEA16r;
  }

  std::vector<InstructionTemplate>
  generateInstructionVariants(const Instruction &Instr,
                              unsigned MaxConfigsPerOpcode) const override;

  std::unique_ptr<SnippetGenerator> createSerialSnippetGenerator(
      const LLVMState &State,
      const SnippetGenerator::Options &Opts) const override {
    return std::make_unique<X86SerialSnippetGenerator>(State, Opts);
  }

  std::unique_ptr<SnippetGenerator> createParallelSnippetGenerator(
      const LLVMState &State,
      const SnippetGenerator::Options &Opts) const override {
    return std::make_unique<X86ParallelSnippetGenerator>(State, Opts);
  }

  bool matchesArch(Triple::ArchType Arch) const override {
    return Arch == Triple::x86_64 || Arch == Triple::x86;
  }

  Error checkFeatureSupport() const override {
    // LBR is the only feature we conditionally support now.
    // So if LBR is not requested, then we should be able to run the benchmarks.
    if (LbrSamplingPeriod == 0)
      return Error::success();

#if defined(__linux__) && defined(HAVE_LIBPFM) &&                              \
    defined(LIBPFM_HAS_FIELD_CYCLES)
      // FIXME: Fix this.
      // https://bugs.llvm.org/show_bug.cgi?id=48918
      // For now, only do the check if we see an Intel machine because
      // the counter uses some intel-specific magic and it could
      // be confuse and think an AMD machine actually has LBR support.
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) ||            \
    defined(_M_X64)
    using namespace sys::detail::x86;

    if (getVendorSignature() == VendorSignatures::GENUINE_INTEL)
      // If the kernel supports it, the hardware still may not have it.
      return X86LbrCounter::checkLbrSupport();
#else
    report_fatal_error("Running X86 exegesis on unsupported target");
#endif
#endif
    return make_error<StringError>(
        "LBR not supported on this kernel and/or platform",
        errc::not_supported);
  }

  std::unique_ptr<SavedState> withSavedState() const override {
    return std::make_unique<X86SavedState>();
  }

  static const unsigned kUnavailableRegisters[4];
  static const unsigned kUnavailableRegistersSSE[12];
};

// We disable a few registers that cannot be encoded on instructions with a REX
// prefix.
const unsigned ExegesisX86Target::kUnavailableRegisters[4] = {X86::AH, X86::BH,
                                                              X86::CH, X86::DH};

// Optionally, also disable the upper (x86_64) SSE registers to reduce frontend
// decoder load.
const unsigned ExegesisX86Target::kUnavailableRegistersSSE[12] = {
    X86::AH,    X86::BH,    X86::CH,    X86::DH,    X86::XMM8,  X86::XMM9,
    X86::XMM10, X86::XMM11, X86::XMM12, X86::XMM13, X86::XMM14, X86::XMM15};

// We're using one of R8-R15 because these registers are never hardcoded in
// instructions (e.g. MOVS writes to EDI, ESI, EDX), so they have less
// conflicts.
constexpr const unsigned kDefaultLoopCounterReg = X86::R8;

} // namespace

void ExegesisX86Target::addTargetSpecificPasses(PassManagerBase &PM) const {
  // Lowers FP pseudo-instructions, e.g. ABS_Fp32 -> ABS_F.
  PM.add(createX86FloatingPointStackifierPass());
}

unsigned ExegesisX86Target::getScratchMemoryRegister(const Triple &TT) const {
  if (!TT.isArch64Bit()) {
    // FIXME: This would require popping from the stack, so we would have to
    // add some additional setup code.
    return 0;
  }
  return TT.isOSWindows() ? X86::RCX : X86::RDI;
}

unsigned
ExegesisX86Target::getDefaultLoopCounterRegister(const Triple &TT) const {
  if (!TT.isArch64Bit()) {
    return 0;
  }
  return kDefaultLoopCounterReg;
}

Error ExegesisX86Target::randomizeTargetMCOperand(
    const Instruction &Instr, const Variable &Var, MCOperand &AssignedValue,
    const BitVector &ForbiddenRegs) const {
  const Operand &Op = Instr.getPrimaryOperand(Var);
  switch (Op.getExplicitOperandInfo().OperandType) {
  case X86::OperandType::OPERAND_COND_CODE:
    AssignedValue =
        MCOperand::createImm(randomIndex(X86::CondCode::LAST_VALID_COND));
    return Error::success();
  case X86::OperandType::OPERAND_ROUNDING_CONTROL:
    AssignedValue =
        MCOperand::createImm(randomIndex(X86::STATIC_ROUNDING::TO_ZERO));
    return Error::success();
  default:
    break;
  }
  return make_error<Failure>(
      Twine("unimplemented operand type ")
          .concat(Twine(Op.getExplicitOperandInfo().OperandType)));
}

void ExegesisX86Target::fillMemoryOperands(InstructionTemplate &IT,
                                           unsigned Reg,
                                           unsigned Offset) const {
  assert(!isInvalidMemoryInstr(IT.getInstr()) &&
         "fillMemoryOperands requires a valid memory instruction");
  int MemOpIdx = X86II::getMemoryOperandNo(IT.getInstr().Description.TSFlags);
  assert(MemOpIdx >= 0 && "invalid memory operand index");
  // getMemoryOperandNo() ignores tied operands, so we have to add them back.
  MemOpIdx += X86II::getOperandBias(IT.getInstr().Description);
  setMemOp(IT, MemOpIdx + 0, MCOperand::createReg(Reg));    // BaseReg
  setMemOp(IT, MemOpIdx + 1, MCOperand::createImm(1));      // ScaleAmt
  setMemOp(IT, MemOpIdx + 2, MCOperand::createReg(0));      // IndexReg
  setMemOp(IT, MemOpIdx + 3, MCOperand::createImm(Offset)); // Disp
  setMemOp(IT, MemOpIdx + 4, MCOperand::createReg(0));      // Segment
}

void ExegesisX86Target::decrementLoopCounterAndJump(
    MachineBasicBlock &MBB, MachineBasicBlock &TargetMBB,
    const MCInstrInfo &MII, unsigned LoopRegister) const {
  BuildMI(&MBB, DebugLoc(), MII.get(X86::ADD64ri8))
      .addDef(LoopRegister)
      .addUse(LoopRegister)
      .addImm(-1);
  BuildMI(&MBB, DebugLoc(), MII.get(X86::JCC_1))
      .addMBB(&TargetMBB)
      .addImm(X86::COND_NE);
}

void generateRegisterStackPush(unsigned int Register,
                               std::vector<MCInst> &GeneratedCode) {
  GeneratedCode.push_back(MCInstBuilder(X86::PUSH64r).addReg(Register));
}

void generateRegisterStackPop(unsigned int Register,
                              std::vector<MCInst> &GeneratedCode) {
  GeneratedCode.push_back(MCInstBuilder(X86::POP64r).addReg(Register));
}

void generateSyscall(long SyscallNumber, std::vector<MCInst> &GeneratedCode) {
  GeneratedCode.push_back(
      loadImmediate(X86::RAX, 64, APInt(64, SyscallNumber)));
  GeneratedCode.push_back(MCInstBuilder(X86::SYSCALL));
}

// The functions below for saving and restoring system call registers are only
// used when llvm-exegesis is built on Linux.
#ifdef __linux__
constexpr std::array<unsigned, 6> SyscallArgumentRegisters{
    X86::RDI, X86::RSI, X86::RDX, X86::R10, X86::R8, X86::R9};

static void saveSyscallRegisters(std::vector<MCInst> &GeneratedCode,
                                 unsigned ArgumentCount) {
  assert(ArgumentCount <= 6 &&
         "System calls only X86-64 Linux can only take six arguments");
  // Preserve RCX and R11 (Clobbered by the system call).
  generateRegisterStackPush(X86::RCX, GeneratedCode);
  generateRegisterStackPush(X86::R11, GeneratedCode);
  // Preserve RAX (used for the syscall number/return value).
  generateRegisterStackPush(X86::RAX, GeneratedCode);
  // Preserve the registers used to pass arguments to the system call.
  for (unsigned I = 0; I < ArgumentCount; ++I)
    generateRegisterStackPush(SyscallArgumentRegisters[I], GeneratedCode);
}

static void restoreSyscallRegisters(std::vector<MCInst> &GeneratedCode,
                                    unsigned ArgumentCount) {
  assert(ArgumentCount <= 6 &&
         "System calls only X86-64 Linux can only take six arguments");
  // Restore the argument registers, in the opposite order of the way they are
  // saved.
  for (unsigned I = ArgumentCount; I > 0; --I) {
    generateRegisterStackPop(SyscallArgumentRegisters[I - 1], GeneratedCode);
  }
  generateRegisterStackPop(X86::RAX, GeneratedCode);
  generateRegisterStackPop(X86::R11, GeneratedCode);
  generateRegisterStackPop(X86::RCX, GeneratedCode);
}
#endif // __linux__

static std::vector<MCInst> loadImmediateSegmentRegister(unsigned Reg,
                                                        const APInt &Value) {
#if defined(__x86_64__) && defined(__linux__)
  assert(Value.getBitWidth() <= 64 && "Value must fit in the register.");
  std::vector<MCInst> loadSegmentRegisterCode;
  // Preserve the syscall registers here as we don't
  // want to make any assumptions about the ordering of what registers are
  // loaded in first, and we might have already loaded in registers that we are
  // going to be clobbering here.
  saveSyscallRegisters(loadSegmentRegisterCode, 2);
  // Generate the instructions to make the arch_prctl system call to set
  // the registers.
  int SyscallCode = 0;
  if (Reg == X86::FS)
    SyscallCode = ARCH_SET_FS;
  else if (Reg == X86::GS)
    SyscallCode = ARCH_SET_GS;
  else
    llvm_unreachable("Only the segment registers GS and FS are supported");
  loadSegmentRegisterCode.push_back(
      loadImmediate(X86::RDI, 64, APInt(64, SyscallCode)));
  loadSegmentRegisterCode.push_back(loadImmediate(X86::RSI, 64, Value));
  generateSyscall(SYS_arch_prctl, loadSegmentRegisterCode);
  // Restore the registers in reverse order
  restoreSyscallRegisters(loadSegmentRegisterCode, 2);
  return loadSegmentRegisterCode;
#else
  llvm_unreachable("Loading immediate segment registers is only supported with "
                   "x86-64 llvm-exegesis");
#endif // defined(__x86_64__) && defined(__linux__)
}

std::vector<MCInst> ExegesisX86Target::setRegTo(const MCSubtargetInfo &STI,
                                                unsigned Reg,
                                                const APInt &Value) const {
  if (X86::SEGMENT_REGRegClass.contains(Reg))
    return loadImmediateSegmentRegister(Reg, Value);
  if (X86::GR8RegClass.contains(Reg))
    return {loadImmediate(Reg, 8, Value)};
  if (X86::GR16RegClass.contains(Reg))
    return {loadImmediate(Reg, 16, Value)};
  if (X86::GR32RegClass.contains(Reg))
    return {loadImmediate(Reg, 32, Value)};
  if (X86::GR64RegClass.contains(Reg))
    return {loadImmediate(Reg, 64, Value)};
  if (X86::VK8RegClass.contains(Reg) || X86::VK16RegClass.contains(Reg) ||
      X86::VK32RegClass.contains(Reg) || X86::VK64RegClass.contains(Reg)) {
    switch (Value.getBitWidth()) {
    case 8:
      if (STI.getFeatureBits()[X86::FeatureDQI]) {
        ConstantInliner CI(Value);
        return CI.loadAndFinalize(Reg, Value.getBitWidth(), X86::KMOVBkm);
      }
      [[fallthrough]];
    case 16:
      if (STI.getFeatureBits()[X86::FeatureAVX512]) {
        ConstantInliner CI(Value.zextOrTrunc(16));
        return CI.loadAndFinalize(Reg, 16, X86::KMOVWkm);
      }
      break;
    case 32:
      if (STI.getFeatureBits()[X86::FeatureBWI]) {
        ConstantInliner CI(Value);
        return CI.loadAndFinalize(Reg, Value.getBitWidth(), X86::KMOVDkm);
      }
      break;
    case 64:
      if (STI.getFeatureBits()[X86::FeatureBWI]) {
        ConstantInliner CI(Value);
        return CI.loadAndFinalize(Reg, Value.getBitWidth(), X86::KMOVQkm);
      }
      break;
    }
  }
  ConstantInliner CI(Value);
  if (X86::VR64RegClass.contains(Reg))
    return CI.loadAndFinalize(Reg, 64, X86::MMX_MOVQ64rm);
  if (X86::VR128XRegClass.contains(Reg)) {
    if (STI.getFeatureBits()[X86::FeatureAVX512])
      return CI.loadAndFinalize(Reg, 128, X86::VMOVDQU32Z128rm);
    if (STI.getFeatureBits()[X86::FeatureAVX])
      return CI.loadAndFinalize(Reg, 128, X86::VMOVDQUrm);
    return CI.loadAndFinalize(Reg, 128, X86::MOVDQUrm);
  }
  if (X86::VR256XRegClass.contains(Reg)) {
    if (STI.getFeatureBits()[X86::FeatureAVX512])
      return CI.loadAndFinalize(Reg, 256, X86::VMOVDQU32Z256rm);
    if (STI.getFeatureBits()[X86::FeatureAVX])
      return CI.loadAndFinalize(Reg, 256, X86::VMOVDQUYrm);
  }
  if (X86::VR512RegClass.contains(Reg))
    if (STI.getFeatureBits()[X86::FeatureAVX512])
      return CI.loadAndFinalize(Reg, 512, X86::VMOVDQU32Zrm);
  if (X86::RSTRegClass.contains(Reg)) {
    return CI.loadX87STAndFinalize(Reg);
  }
  if (X86::RFP32RegClass.contains(Reg) || X86::RFP64RegClass.contains(Reg) ||
      X86::RFP80RegClass.contains(Reg)) {
    return CI.loadX87FPAndFinalize(Reg);
  }
  if (Reg == X86::EFLAGS)
    return CI.popFlagAndFinalize();
  if (Reg == X86::MXCSR)
    return CI.loadImplicitRegAndFinalize(
        STI.getFeatureBits()[X86::FeatureAVX] ? X86::VLDMXCSR : X86::LDMXCSR,
        0x1f80);
  if (Reg == X86::FPCW)
    return CI.loadImplicitRegAndFinalize(X86::FLDCW16m, 0x37f);
  return {}; // Not yet implemented.
}

#ifdef __linux__

#ifdef __arm__
static constexpr const intptr_t VAddressSpaceCeiling = 0xC0000000;
#else
static constexpr const intptr_t VAddressSpaceCeiling = 0x0000800000000000;
#endif

void generateRoundToNearestPage(unsigned int Register,
                                std::vector<MCInst> &GeneratedCode) {
  int PageSizeShift = static_cast<int>(round(log2(getpagesize())));
  // Round down to the nearest page by getting rid of the least significant bits
  // representing location in the page. Shift right to get rid of this info and
  // then shift back left.
  GeneratedCode.push_back(MCInstBuilder(X86::SHR64ri)
                              .addReg(Register)
                              .addReg(Register)
                              .addImm(PageSizeShift));
  GeneratedCode.push_back(MCInstBuilder(X86::SHL64ri)
                              .addReg(Register)
                              .addReg(Register)
                              .addImm(PageSizeShift));
}

void generateGetInstructionPointer(unsigned int ResultRegister,
                                   std::vector<MCInst> &GeneratedCode) {
  // Use a load effective address to get the current instruction pointer and put
  // it into the result register.
  GeneratedCode.push_back(MCInstBuilder(X86::LEA64r)
                              .addReg(ResultRegister)
                              .addReg(X86::RIP)
                              .addImm(1)
                              .addReg(0)
                              .addImm(0)
                              .addReg(0));
}

void ExegesisX86Target::generateLowerMunmap(
    std::vector<MCInst> &GeneratedCode) const {
  // Unmap starting at address zero
  GeneratedCode.push_back(loadImmediate(X86::RDI, 64, APInt(64, 0)));
  // Get the current instruction pointer so we know where to unmap up to.
  generateGetInstructionPointer(X86::RSI, GeneratedCode);
  generateRoundToNearestPage(X86::RSI, GeneratedCode);
  // Subtract a page from the end of the unmap so we don't unmap the currently
  // executing section.
  GeneratedCode.push_back(MCInstBuilder(X86::SUB64ri32)
                              .addReg(X86::RSI)
                              .addReg(X86::RSI)
                              .addImm(getpagesize()));
  generateSyscall(SYS_munmap, GeneratedCode);
}

void ExegesisX86Target::generateUpperMunmap(
    std::vector<MCInst> &GeneratedCode) const {
  generateGetInstructionPointer(X86::R8, GeneratedCode);
  // Load in the size of the snippet to RDI from from the argument register.
  GeneratedCode.push_back(MCInstBuilder(X86::MOV64rr)
                              .addReg(X86::RDI)
                              .addReg(ArgumentRegisters::CodeSize));
  // Add the length of the snippet (in %RDI) to the current instruction pointer
  // (%R8) to get the address where we should start unmapping at.
  GeneratedCode.push_back(MCInstBuilder(X86::ADD64rr)
                              .addReg(X86::RDI)
                              .addReg(X86::RDI)
                              .addReg(X86::R8));
  generateRoundToNearestPage(X86::RDI, GeneratedCode);
  // Add a one page to the start address to ensure that we're above the snippet
  // since the above function rounds down.
  GeneratedCode.push_back(MCInstBuilder(X86::ADD64ri32)
                              .addReg(X86::RDI)
                              .addReg(X86::RDI)
                              .addImm(getpagesize()));
  // Unmap to just one page under the ceiling of the address space.
  GeneratedCode.push_back(loadImmediate(
      X86::RSI, 64, APInt(64, VAddressSpaceCeiling - getpagesize())));
  GeneratedCode.push_back(MCInstBuilder(X86::SUB64rr)
                              .addReg(X86::RSI)
                              .addReg(X86::RSI)
                              .addReg(X86::RDI));
  generateSyscall(SYS_munmap, GeneratedCode);
}

std::vector<MCInst>
ExegesisX86Target::generateExitSyscall(unsigned ExitCode) const {
  std::vector<MCInst> ExitCallCode;
  ExitCallCode.push_back(loadImmediate(X86::RDI, 64, APInt(64, ExitCode)));
  generateSyscall(SYS_exit, ExitCallCode);
  return ExitCallCode;
}

std::vector<MCInst>
ExegesisX86Target::generateMmap(intptr_t Address, size_t Length,
                                intptr_t FileDescriptorAddress) const {
  std::vector<MCInst> MmapCode;
  MmapCode.push_back(loadImmediate(X86::RDI, 64, APInt(64, Address)));
  MmapCode.push_back(loadImmediate(X86::RSI, 64, APInt(64, Length)));
  MmapCode.push_back(
      loadImmediate(X86::RDX, 64, APInt(64, PROT_READ | PROT_WRITE)));
  MmapCode.push_back(
      loadImmediate(X86::R10, 64, APInt(64, MAP_SHARED | MAP_FIXED_NOREPLACE)));
  // Copy file descriptor location from aux memory into R8
  MmapCode.push_back(
      loadImmediate(X86::R8, 64, APInt(64, FileDescriptorAddress)));
  // Dereference file descriptor into FD argument register
  MmapCode.push_back(MCInstBuilder(X86::MOV32rm)
                         .addReg(X86::R8D)
                         .addReg(X86::R8)
                         .addImm(1)
                         .addReg(0)
                         .addImm(0)
                         .addReg(0));
  MmapCode.push_back(loadImmediate(X86::R9, 64, APInt(64, 0)));
  generateSyscall(SYS_mmap, MmapCode);
  return MmapCode;
}

void ExegesisX86Target::generateMmapAuxMem(
    std::vector<MCInst> &GeneratedCode) const {
  GeneratedCode.push_back(
      loadImmediate(X86::RDI, 64, APInt(64, getAuxiliaryMemoryStartAddress())));
  GeneratedCode.push_back(loadImmediate(
      X86::RSI, 64, APInt(64, SubprocessMemory::AuxiliaryMemorySize)));
  GeneratedCode.push_back(
      loadImmediate(X86::RDX, 64, APInt(64, PROT_READ | PROT_WRITE)));
  GeneratedCode.push_back(
      loadImmediate(X86::R10, 64, APInt(64, MAP_SHARED | MAP_FIXED_NOREPLACE)));
  GeneratedCode.push_back(MCInstBuilder(X86::MOV64rr)
                              .addReg(X86::R8)
                              .addReg(ArgumentRegisters::AuxiliaryMemoryFD));
  GeneratedCode.push_back(loadImmediate(X86::R9, 64, APInt(64, 0)));
  generateSyscall(SYS_mmap, GeneratedCode);
}

void ExegesisX86Target::moveArgumentRegisters(
    std::vector<MCInst> &GeneratedCode) const {
  GeneratedCode.push_back(MCInstBuilder(X86::MOV64rr)
                              .addReg(ArgumentRegisters::CodeSize)
                              .addReg(X86::RDI));
  GeneratedCode.push_back(MCInstBuilder(X86::MOV64rr)
                              .addReg(ArgumentRegisters::AuxiliaryMemoryFD)
                              .addReg(X86::RSI));
}

std::vector<MCInst> ExegesisX86Target::generateMemoryInitialSetup() const {
  std::vector<MCInst> MemoryInitialSetupCode;
  moveArgumentRegisters(MemoryInitialSetupCode);
  generateLowerMunmap(MemoryInitialSetupCode);
  generateUpperMunmap(MemoryInitialSetupCode);
  generateMmapAuxMem(MemoryInitialSetupCode);
  return MemoryInitialSetupCode;
}

std::vector<MCInst> ExegesisX86Target::setStackRegisterToAuxMem() const {
  // Moves %rsp to the end of the auxiliary memory
  return {MCInstBuilder(X86::MOV64ri)
              .addReg(X86::RSP)
              .addImm(getAuxiliaryMemoryStartAddress() +
                      SubprocessMemory::AuxiliaryMemorySize)};
}

intptr_t ExegesisX86Target::getAuxiliaryMemoryStartAddress() const {
  // Return the second to last page in the virtual address space to try and
  // prevent interference with memory annotations in the snippet
  return VAddressSpaceCeiling - 2 * getpagesize();
}

std::vector<MCInst>
ExegesisX86Target::configurePerfCounter(long Request, bool SaveRegisters) const {
  std::vector<MCInst> ConfigurePerfCounterCode;
  if (SaveRegisters)
    saveSyscallRegisters(ConfigurePerfCounterCode, 3);
  ConfigurePerfCounterCode.push_back(
      loadImmediate(X86::RDI, 64, APInt(64, getAuxiliaryMemoryStartAddress())));
  ConfigurePerfCounterCode.push_back(MCInstBuilder(X86::MOV32rm)
                                         .addReg(X86::EDI)
                                         .addReg(X86::RDI)
                                         .addImm(1)
                                         .addReg(0)
                                         .addImm(0)
                                         .addReg(0));
  ConfigurePerfCounterCode.push_back(
      loadImmediate(X86::RSI, 64, APInt(64, Request)));
#ifdef HAVE_LIBPFM
  ConfigurePerfCounterCode.push_back(
      loadImmediate(X86::RDX, 64, APInt(64, PERF_IOC_FLAG_GROUP)));
#endif // HAVE_LIBPFM
  generateSyscall(SYS_ioctl, ConfigurePerfCounterCode);
  if (SaveRegisters)
    restoreSyscallRegisters(ConfigurePerfCounterCode, 3);
  return ConfigurePerfCounterCode;
}

std::vector<unsigned> ExegesisX86Target::getArgumentRegisters() const {
  return {X86::RDI, X86::RSI};
}

std::vector<unsigned> ExegesisX86Target::getRegistersNeedSaving() const {
  return {X86::RAX, X86::RDI, X86::RSI, X86::RCX, X86::R11};
}

#endif // __linux__

// Instruction can have some variable operands, and we may want to see how
// different operands affect performance. So for each operand position,
// precompute all the possible choices we might care about,
// and greedily generate all the possible combinations of choices.
std::vector<InstructionTemplate> ExegesisX86Target::generateInstructionVariants(
    const Instruction &Instr, unsigned MaxConfigsPerOpcode) const {
  bool Exploration = false;
  SmallVector<SmallVector<MCOperand, 1>, 4> VariableChoices;
  VariableChoices.resize(Instr.Variables.size());
  for (auto I : zip(Instr.Variables, VariableChoices)) {
    const Variable &Var = std::get<0>(I);
    SmallVectorImpl<MCOperand> &Choices = std::get<1>(I);

    switch (Instr.getPrimaryOperand(Var).getExplicitOperandInfo().OperandType) {
    default:
      // We don't wish to explicitly explore this variable.
      Choices.emplace_back(); // But add invalid MCOperand to simplify logic.
      continue;
    case X86::OperandType::OPERAND_COND_CODE: {
      Exploration = true;
      auto CondCodes = enum_seq_inclusive(X86::CondCode::COND_O,
                                          X86::CondCode::LAST_VALID_COND,
                                          force_iteration_on_noniterable_enum);
      Choices.reserve(CondCodes.size());
      for (int CondCode : CondCodes)
        Choices.emplace_back(MCOperand::createImm(CondCode));
      break;
    }
    }
  }

  // If we don't wish to explore any variables, defer to the baseline method.
  if (!Exploration)
    return ExegesisTarget::generateInstructionVariants(Instr,
                                                       MaxConfigsPerOpcode);

  std::vector<InstructionTemplate> Variants;
  size_t NumVariants;
  CombinationGenerator<MCOperand, decltype(VariableChoices)::value_type, 4> G(
      VariableChoices);

  // How many operand combinations can we produce, within the limit?
  NumVariants = std::min(G.numCombinations(), (size_t)MaxConfigsPerOpcode);
  // And actually produce all the wanted operand combinations.
  Variants.reserve(NumVariants);
  G.generate([&](ArrayRef<MCOperand> State) -> bool {
    Variants.emplace_back(&Instr);
    Variants.back().setVariableValues(State);
    // Did we run out of space for variants?
    return Variants.size() >= NumVariants;
  });

  assert(Variants.size() == NumVariants &&
         Variants.size() <= MaxConfigsPerOpcode &&
         "Should not produce too many variants");
  return Variants;
}

static ExegesisTarget *getTheExegesisX86Target() {
  static ExegesisX86Target Target;
  return &Target;
}

void InitializeX86ExegesisTarget() {
  ExegesisTarget::registerTarget(getTheExegesisX86Target());
}

} // namespace exegesis
} // namespace llvm
