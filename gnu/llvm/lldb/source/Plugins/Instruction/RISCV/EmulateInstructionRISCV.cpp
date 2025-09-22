//===-- EmulateInstructionRISCV.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionRISCV.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_riscv64.h"
#include "Plugins/Process/Utility/lldb-riscv-register-enums.h"
#include "RISCVCInstructions.h"
#include "RISCVInstructions.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Interpreter/OptionValueArray.h"
#include "lldb/Interpreter/OptionValueDictionary.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MathExtras.h"
#include <optional>

using namespace llvm;
using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(EmulateInstructionRISCV, InstructionRISCV)

namespace lldb_private {

/// Returns all values wrapped in Optional, or std::nullopt if any of the values
/// is std::nullopt.
template <typename... Ts>
static std::optional<std::tuple<Ts...>> zipOpt(std::optional<Ts> &&...ts) {
  if ((ts.has_value() && ...))
    return std::optional<std::tuple<Ts...>>(std::make_tuple(std::move(*ts)...));
  else
    return std::nullopt;
}

// The funct3 is the type of compare in B<CMP> instructions.
// funct3 means "3-bits function selector", which RISC-V ISA uses as minor
// opcode. It reuses the major opcode encoding space.
constexpr uint32_t BEQ = 0b000;
constexpr uint32_t BNE = 0b001;
constexpr uint32_t BLT = 0b100;
constexpr uint32_t BGE = 0b101;
constexpr uint32_t BLTU = 0b110;
constexpr uint32_t BGEU = 0b111;

// used in decoder
constexpr int32_t SignExt(uint32_t imm) { return int32_t(imm); }

// used in executor
template <typename T>
constexpr std::enable_if_t<sizeof(T) <= 4, uint64_t> SextW(T value) {
  return uint64_t(int64_t(int32_t(value)));
}

// used in executor
template <typename T> constexpr uint64_t ZextD(T value) {
  return uint64_t(value);
}

constexpr uint32_t DecodeJImm(uint32_t inst) {
  return (uint64_t(int64_t(int32_t(inst & 0x80000000)) >> 11)) // imm[20]
         | (inst & 0xff000)                                    // imm[19:12]
         | ((inst >> 9) & 0x800)                               // imm[11]
         | ((inst >> 20) & 0x7fe);                             // imm[10:1]
}

constexpr uint32_t DecodeIImm(uint32_t inst) {
  return int64_t(int32_t(inst)) >> 20; // imm[11:0]
}

constexpr uint32_t DecodeBImm(uint32_t inst) {
  return (uint64_t(int64_t(int32_t(inst & 0x80000000)) >> 19)) // imm[12]
         | ((inst & 0x80) << 4)                                // imm[11]
         | ((inst >> 20) & 0x7e0)                              // imm[10:5]
         | ((inst >> 7) & 0x1e);                               // imm[4:1]
}

constexpr uint32_t DecodeSImm(uint32_t inst) {
  return (uint64_t(int64_t(int32_t(inst & 0xFE000000)) >> 20)) // imm[11:5]
         | ((inst & 0xF80) >> 7);                              // imm[4:0]
}

constexpr uint32_t DecodeUImm(uint32_t inst) {
  return SextW(inst & 0xFFFFF000); // imm[31:12]
}

static uint32_t GPREncodingToLLDB(uint32_t reg_encode) {
  if (reg_encode == 0)
    return gpr_x0_riscv;
  if (reg_encode >= 1 && reg_encode <= 31)
    return gpr_x1_riscv + reg_encode - 1;
  return LLDB_INVALID_REGNUM;
}

static uint32_t FPREncodingToLLDB(uint32_t reg_encode) {
  if (reg_encode <= 31)
    return fpr_f0_riscv + reg_encode;
  return LLDB_INVALID_REGNUM;
}

bool Rd::Write(EmulateInstructionRISCV &emulator, uint64_t value) {
  uint32_t lldb_reg = GPREncodingToLLDB(rd);
  EmulateInstruction::Context ctx;
  ctx.type = EmulateInstruction::eContextRegisterStore;
  ctx.SetNoArgs();
  RegisterValue registerValue;
  registerValue.SetUInt64(value);
  return emulator.WriteRegister(ctx, eRegisterKindLLDB, lldb_reg,
                                registerValue);
}

bool Rd::WriteAPFloat(EmulateInstructionRISCV &emulator, APFloat value) {
  uint32_t lldb_reg = FPREncodingToLLDB(rd);
  EmulateInstruction::Context ctx;
  ctx.type = EmulateInstruction::eContextRegisterStore;
  ctx.SetNoArgs();
  RegisterValue registerValue;
  registerValue.SetUInt64(value.bitcastToAPInt().getZExtValue());
  return emulator.WriteRegister(ctx, eRegisterKindLLDB, lldb_reg,
                                registerValue);
}

std::optional<uint64_t> Rs::Read(EmulateInstructionRISCV &emulator) {
  uint32_t lldbReg = GPREncodingToLLDB(rs);
  RegisterValue value;
  return emulator.ReadRegister(eRegisterKindLLDB, lldbReg, value)
             ? std::optional<uint64_t>(value.GetAsUInt64())
             : std::nullopt;
}

std::optional<int32_t> Rs::ReadI32(EmulateInstructionRISCV &emulator) {
  return transformOptional(
      Read(emulator), [](uint64_t value) { return int32_t(uint32_t(value)); });
}

std::optional<int64_t> Rs::ReadI64(EmulateInstructionRISCV &emulator) {
  return transformOptional(Read(emulator),
                           [](uint64_t value) { return int64_t(value); });
}

std::optional<uint32_t> Rs::ReadU32(EmulateInstructionRISCV &emulator) {
  return transformOptional(Read(emulator),
                           [](uint64_t value) { return uint32_t(value); });
}

std::optional<APFloat> Rs::ReadAPFloat(EmulateInstructionRISCV &emulator,
                                       bool isDouble) {
  uint32_t lldbReg = FPREncodingToLLDB(rs);
  RegisterValue value;
  if (!emulator.ReadRegister(eRegisterKindLLDB, lldbReg, value))
    return std::nullopt;
  uint64_t bits = value.GetAsUInt64();
  APInt api(64, bits, false);
  return APFloat(isDouble ? APFloat(api.bitsToDouble())
                          : APFloat(api.bitsToFloat()));
}

static bool CompareB(uint64_t rs1, uint64_t rs2, uint32_t funct3) {
  switch (funct3) {
  case BEQ:
    return rs1 == rs2;
  case BNE:
    return rs1 != rs2;
  case BLT:
    return int64_t(rs1) < int64_t(rs2);
  case BGE:
    return int64_t(rs1) >= int64_t(rs2);
  case BLTU:
    return rs1 < rs2;
  case BGEU:
    return rs1 >= rs2;
  default:
    llvm_unreachable("unexpected funct3");
  }
}

template <typename T>
constexpr bool is_load =
    std::is_same_v<T, LB> || std::is_same_v<T, LH> || std::is_same_v<T, LW> ||
    std::is_same_v<T, LD> || std::is_same_v<T, LBU> || std::is_same_v<T, LHU> ||
    std::is_same_v<T, LWU>;

template <typename T>
constexpr bool is_store = std::is_same_v<T, SB> || std::is_same_v<T, SH> ||
                          std::is_same_v<T, SW> || std::is_same_v<T, SD>;

template <typename T>
constexpr bool is_amo_add =
    std::is_same_v<T, AMOADD_W> || std::is_same_v<T, AMOADD_D>;

template <typename T>
constexpr bool is_amo_bit_op =
    std::is_same_v<T, AMOXOR_W> || std::is_same_v<T, AMOXOR_D> ||
    std::is_same_v<T, AMOAND_W> || std::is_same_v<T, AMOAND_D> ||
    std::is_same_v<T, AMOOR_W> || std::is_same_v<T, AMOOR_D>;

template <typename T>
constexpr bool is_amo_swap =
    std::is_same_v<T, AMOSWAP_W> || std::is_same_v<T, AMOSWAP_D>;

template <typename T>
constexpr bool is_amo_cmp =
    std::is_same_v<T, AMOMIN_W> || std::is_same_v<T, AMOMIN_D> ||
    std::is_same_v<T, AMOMAX_W> || std::is_same_v<T, AMOMAX_D> ||
    std::is_same_v<T, AMOMINU_W> || std::is_same_v<T, AMOMINU_D> ||
    std::is_same_v<T, AMOMAXU_W> || std::is_same_v<T, AMOMAXU_D>;

template <typename I>
static std::enable_if_t<is_load<I> || is_store<I>, std::optional<uint64_t>>
LoadStoreAddr(EmulateInstructionRISCV &emulator, I inst) {
  return transformOptional(inst.rs1.Read(emulator), [&](uint64_t rs1) {
    return rs1 + uint64_t(SignExt(inst.imm));
  });
}

// Read T from memory, then load its sign-extended value m_emu to register.
template <typename I, typename T, typename E>
static std::enable_if_t<is_load<I>, bool>
Load(EmulateInstructionRISCV &emulator, I inst, uint64_t (*extend)(E)) {
  auto addr = LoadStoreAddr(emulator, inst);
  if (!addr)
    return false;
  return transformOptional(
             emulator.ReadMem<T>(*addr),
             [&](T t) { return inst.rd.Write(emulator, extend(E(t))); })
      .value_or(false);
}

template <typename I, typename T>
static std::enable_if_t<is_store<I>, bool>
Store(EmulateInstructionRISCV &emulator, I inst) {
  auto addr = LoadStoreAddr(emulator, inst);
  if (!addr)
    return false;
  return transformOptional(
             inst.rs2.Read(emulator),
             [&](uint64_t rs2) { return emulator.WriteMem<T>(*addr, rs2); })
      .value_or(false);
}

template <typename I>
static std::enable_if_t<is_amo_add<I> || is_amo_bit_op<I> || is_amo_swap<I> ||
                            is_amo_cmp<I>,
                        std::optional<uint64_t>>
AtomicAddr(EmulateInstructionRISCV &emulator, I inst, unsigned int align) {
  return transformOptional(inst.rs1.Read(emulator),
                           [&](uint64_t rs1) {
                             return rs1 % align == 0
                                        ? std::optional<uint64_t>(rs1)
                                        : std::nullopt;
                           })
      .value_or(std::nullopt);
}

template <typename I, typename T>
static std::enable_if_t<is_amo_swap<I>, bool>
AtomicSwap(EmulateInstructionRISCV &emulator, I inst, int align,
           uint64_t (*extend)(T)) {
  auto addr = AtomicAddr(emulator, inst, align);
  if (!addr)
    return false;
  return transformOptional(
             zipOpt(emulator.ReadMem<T>(*addr), inst.rs2.Read(emulator)),
             [&](auto &&tup) {
               auto [tmp, rs2] = tup;
               return emulator.WriteMem<T>(*addr, T(rs2)) &&
                      inst.rd.Write(emulator, extend(tmp));
             })
      .value_or(false);
}

template <typename I, typename T>
static std::enable_if_t<is_amo_add<I>, bool>
AtomicADD(EmulateInstructionRISCV &emulator, I inst, int align,
          uint64_t (*extend)(T)) {
  auto addr = AtomicAddr(emulator, inst, align);
  if (!addr)
    return false;
  return transformOptional(
             zipOpt(emulator.ReadMem<T>(*addr), inst.rs2.Read(emulator)),
             [&](auto &&tup) {
               auto [tmp, rs2] = tup;
               return emulator.WriteMem<T>(*addr, T(tmp + rs2)) &&
                      inst.rd.Write(emulator, extend(tmp));
             })
      .value_or(false);
}

template <typename I, typename T>
static std::enable_if_t<is_amo_bit_op<I>, bool>
AtomicBitOperate(EmulateInstructionRISCV &emulator, I inst, int align,
                 uint64_t (*extend)(T), T (*operate)(T, T)) {
  auto addr = AtomicAddr(emulator, inst, align);
  if (!addr)
    return false;
  return transformOptional(
             zipOpt(emulator.ReadMem<T>(*addr), inst.rs2.Read(emulator)),
             [&](auto &&tup) {
               auto [value, rs2] = tup;
               return emulator.WriteMem<T>(*addr, operate(value, T(rs2))) &&
                      inst.rd.Write(emulator, extend(value));
             })
      .value_or(false);
}

template <typename I, typename T>
static std::enable_if_t<is_amo_cmp<I>, bool>
AtomicCmp(EmulateInstructionRISCV &emulator, I inst, int align,
          uint64_t (*extend)(T), T (*cmp)(T, T)) {
  auto addr = AtomicAddr(emulator, inst, align);
  if (!addr)
    return false;
  return transformOptional(
             zipOpt(emulator.ReadMem<T>(*addr), inst.rs2.Read(emulator)),
             [&](auto &&tup) {
               auto [value, rs2] = tup;
               return emulator.WriteMem<T>(*addr, cmp(value, T(rs2))) &&
                      inst.rd.Write(emulator, extend(value));
             })
      .value_or(false);
}

bool AtomicSequence(EmulateInstructionRISCV &emulator) {
  // The atomic sequence is always 4 instructions long:
  // example:
  //   110cc:	100427af          	lr.w	a5,(s0)
  //   110d0:	00079663          	bnez	a5,110dc
  //   110d4:	1ce426af          	sc.w.aq	a3,a4,(s0)
  //   110d8:	fe069ae3          	bnez	a3,110cc
  //   110dc:   ........          	<next instruction>
  const auto pc = emulator.ReadPC();
  if (!pc)
    return false;
  auto current_pc = *pc;
  const auto entry_pc = current_pc;

  // The first instruction should be LR.W or LR.D
  auto inst = emulator.ReadInstructionAt(current_pc);
  if (!inst || (!std::holds_alternative<LR_W>(inst->decoded) &&
                !std::holds_alternative<LR_D>(inst->decoded)))
    return false;

  // The second instruction should be BNE to exit address
  inst = emulator.ReadInstructionAt(current_pc += 4);
  if (!inst || !std::holds_alternative<B>(inst->decoded))
    return false;
  auto bne_exit = std::get<B>(inst->decoded);
  if (bne_exit.funct3 != BNE)
    return false;
  // save the exit address to check later
  const auto exit_pc = current_pc + SextW(bne_exit.imm);

  // The third instruction should be SC.W or SC.D
  inst = emulator.ReadInstructionAt(current_pc += 4);
  if (!inst || (!std::holds_alternative<SC_W>(inst->decoded) &&
                !std::holds_alternative<SC_D>(inst->decoded)))
    return false;

  // The fourth instruction should be BNE to entry address
  inst = emulator.ReadInstructionAt(current_pc += 4);
  if (!inst || !std::holds_alternative<B>(inst->decoded))
    return false;
  auto bne_start = std::get<B>(inst->decoded);
  if (bne_start.funct3 != BNE)
    return false;
  if (entry_pc != current_pc + SextW(bne_start.imm))
    return false;

  current_pc += 4;
  // check the exit address and jump to it
  return exit_pc == current_pc && emulator.WritePC(current_pc);
}

template <typename T> static RISCVInst DecodeUType(uint32_t inst) {
  return T{Rd{DecodeRD(inst)}, DecodeUImm(inst)};
}

template <typename T> static RISCVInst DecodeJType(uint32_t inst) {
  return T{Rd{DecodeRD(inst)}, DecodeJImm(inst)};
}

template <typename T> static RISCVInst DecodeIType(uint32_t inst) {
  return T{Rd{DecodeRD(inst)}, Rs{DecodeRS1(inst)}, DecodeIImm(inst)};
}

template <typename T> static RISCVInst DecodeBType(uint32_t inst) {
  return T{Rs{DecodeRS1(inst)}, Rs{DecodeRS2(inst)}, DecodeBImm(inst),
           DecodeFunct3(inst)};
}

template <typename T> static RISCVInst DecodeSType(uint32_t inst) {
  return T{Rs{DecodeRS1(inst)}, Rs{DecodeRS2(inst)}, DecodeSImm(inst)};
}

template <typename T> static RISCVInst DecodeRType(uint32_t inst) {
  return T{Rd{DecodeRD(inst)}, Rs{DecodeRS1(inst)}, Rs{DecodeRS2(inst)}};
}

template <typename T> static RISCVInst DecodeRShamtType(uint32_t inst) {
  return T{Rd{DecodeRD(inst)}, Rs{DecodeRS1(inst)}, DecodeRS2(inst)};
}

template <typename T> static RISCVInst DecodeRRS1Type(uint32_t inst) {
  return T{Rd{DecodeRD(inst)}, Rs{DecodeRS1(inst)}};
}

template <typename T> static RISCVInst DecodeR4Type(uint32_t inst) {
  return T{Rd{DecodeRD(inst)}, Rs{DecodeRS1(inst)}, Rs{DecodeRS2(inst)},
           Rs{DecodeRS3(inst)}, DecodeRM(inst)};
}

static const InstrPattern PATTERNS[] = {
    // RV32I & RV64I (The base integer ISA) //
    {"LUI", 0x7F, 0x37, DecodeUType<LUI>},
    {"AUIPC", 0x7F, 0x17, DecodeUType<AUIPC>},
    {"JAL", 0x7F, 0x6F, DecodeJType<JAL>},
    {"JALR", 0x707F, 0x67, DecodeIType<JALR>},
    {"B", 0x7F, 0x63, DecodeBType<B>},
    {"LB", 0x707F, 0x3, DecodeIType<LB>},
    {"LH", 0x707F, 0x1003, DecodeIType<LH>},
    {"LW", 0x707F, 0x2003, DecodeIType<LW>},
    {"LBU", 0x707F, 0x4003, DecodeIType<LBU>},
    {"LHU", 0x707F, 0x5003, DecodeIType<LHU>},
    {"SB", 0x707F, 0x23, DecodeSType<SB>},
    {"SH", 0x707F, 0x1023, DecodeSType<SH>},
    {"SW", 0x707F, 0x2023, DecodeSType<SW>},
    {"ADDI", 0x707F, 0x13, DecodeIType<ADDI>},
    {"SLTI", 0x707F, 0x2013, DecodeIType<SLTI>},
    {"SLTIU", 0x707F, 0x3013, DecodeIType<SLTIU>},
    {"XORI", 0x707F, 0x4013, DecodeIType<XORI>},
    {"ORI", 0x707F, 0x6013, DecodeIType<ORI>},
    {"ANDI", 0x707F, 0x7013, DecodeIType<ANDI>},
    {"SLLI", 0xF800707F, 0x1013, DecodeRShamtType<SLLI>},
    {"SRLI", 0xF800707F, 0x5013, DecodeRShamtType<SRLI>},
    {"SRAI", 0xF800707F, 0x40005013, DecodeRShamtType<SRAI>},
    {"ADD", 0xFE00707F, 0x33, DecodeRType<ADD>},
    {"SUB", 0xFE00707F, 0x40000033, DecodeRType<SUB>},
    {"SLL", 0xFE00707F, 0x1033, DecodeRType<SLL>},
    {"SLT", 0xFE00707F, 0x2033, DecodeRType<SLT>},
    {"SLTU", 0xFE00707F, 0x3033, DecodeRType<SLTU>},
    {"XOR", 0xFE00707F, 0x4033, DecodeRType<XOR>},
    {"SRL", 0xFE00707F, 0x5033, DecodeRType<SRL>},
    {"SRA", 0xFE00707F, 0x40005033, DecodeRType<SRA>},
    {"OR", 0xFE00707F, 0x6033, DecodeRType<OR>},
    {"AND", 0xFE00707F, 0x7033, DecodeRType<AND>},
    {"LWU", 0x707F, 0x6003, DecodeIType<LWU>},
    {"LD", 0x707F, 0x3003, DecodeIType<LD>},
    {"SD", 0x707F, 0x3023, DecodeSType<SD>},
    {"ADDIW", 0x707F, 0x1B, DecodeIType<ADDIW>},
    {"SLLIW", 0xFE00707F, 0x101B, DecodeRShamtType<SLLIW>},
    {"SRLIW", 0xFE00707F, 0x501B, DecodeRShamtType<SRLIW>},
    {"SRAIW", 0xFE00707F, 0x4000501B, DecodeRShamtType<SRAIW>},
    {"ADDW", 0xFE00707F, 0x3B, DecodeRType<ADDW>},
    {"SUBW", 0xFE00707F, 0x4000003B, DecodeRType<SUBW>},
    {"SLLW", 0xFE00707F, 0x103B, DecodeRType<SLLW>},
    {"SRLW", 0xFE00707F, 0x503B, DecodeRType<SRLW>},
    {"SRAW", 0xFE00707F, 0x4000503B, DecodeRType<SRAW>},

    // RV32M & RV64M (The integer multiplication and division extension) //
    {"MUL", 0xFE00707F, 0x2000033, DecodeRType<MUL>},
    {"MULH", 0xFE00707F, 0x2001033, DecodeRType<MULH>},
    {"MULHSU", 0xFE00707F, 0x2002033, DecodeRType<MULHSU>},
    {"MULHU", 0xFE00707F, 0x2003033, DecodeRType<MULHU>},
    {"DIV", 0xFE00707F, 0x2004033, DecodeRType<DIV>},
    {"DIVU", 0xFE00707F, 0x2005033, DecodeRType<DIVU>},
    {"REM", 0xFE00707F, 0x2006033, DecodeRType<REM>},
    {"REMU", 0xFE00707F, 0x2007033, DecodeRType<REMU>},
    {"MULW", 0xFE00707F, 0x200003B, DecodeRType<MULW>},
    {"DIVW", 0xFE00707F, 0x200403B, DecodeRType<DIVW>},
    {"DIVUW", 0xFE00707F, 0x200503B, DecodeRType<DIVUW>},
    {"REMW", 0xFE00707F, 0x200603B, DecodeRType<REMW>},
    {"REMUW", 0xFE00707F, 0x200703B, DecodeRType<REMUW>},

    // RV32A & RV64A (The standard atomic instruction extension) //
    {"LR_W", 0xF9F0707F, 0x1000202F, DecodeRRS1Type<LR_W>},
    {"LR_D", 0xF9F0707F, 0x1000302F, DecodeRRS1Type<LR_D>},
    {"SC_W", 0xF800707F, 0x1800202F, DecodeRType<SC_W>},
    {"SC_D", 0xF800707F, 0x1800302F, DecodeRType<SC_D>},
    {"AMOSWAP_W", 0xF800707F, 0x800202F, DecodeRType<AMOSWAP_W>},
    {"AMOADD_W", 0xF800707F, 0x202F, DecodeRType<AMOADD_W>},
    {"AMOXOR_W", 0xF800707F, 0x2000202F, DecodeRType<AMOXOR_W>},
    {"AMOAND_W", 0xF800707F, 0x6000202F, DecodeRType<AMOAND_W>},
    {"AMOOR_W", 0xF800707F, 0x4000202F, DecodeRType<AMOOR_W>},
    {"AMOMIN_W", 0xF800707F, 0x8000202F, DecodeRType<AMOMIN_W>},
    {"AMOMAX_W", 0xF800707F, 0xA000202F, DecodeRType<AMOMAX_W>},
    {"AMOMINU_W", 0xF800707F, 0xC000202F, DecodeRType<AMOMINU_W>},
    {"AMOMAXU_W", 0xF800707F, 0xE000202F, DecodeRType<AMOMAXU_W>},
    {"AMOSWAP_D", 0xF800707F, 0x800302F, DecodeRType<AMOSWAP_D>},
    {"AMOADD_D", 0xF800707F, 0x302F, DecodeRType<AMOADD_D>},
    {"AMOXOR_D", 0xF800707F, 0x2000302F, DecodeRType<AMOXOR_D>},
    {"AMOAND_D", 0xF800707F, 0x6000302F, DecodeRType<AMOAND_D>},
    {"AMOOR_D", 0xF800707F, 0x4000302F, DecodeRType<AMOOR_D>},
    {"AMOMIN_D", 0xF800707F, 0x8000302F, DecodeRType<AMOMIN_D>},
    {"AMOMAX_D", 0xF800707F, 0xA000302F, DecodeRType<AMOMAX_D>},
    {"AMOMINU_D", 0xF800707F, 0xC000302F, DecodeRType<AMOMINU_D>},
    {"AMOMAXU_D", 0xF800707F, 0xE000302F, DecodeRType<AMOMAXU_D>},

    // RVC (Compressed Instructions) //
    {"C_LWSP", 0xE003, 0x4002, DecodeC_LWSP},
    {"C_LDSP", 0xE003, 0x6002, DecodeC_LDSP, RV64 | RV128},
    {"C_SWSP", 0xE003, 0xC002, DecodeC_SWSP},
    {"C_SDSP", 0xE003, 0xE002, DecodeC_SDSP, RV64 | RV128},
    {"C_LW", 0xE003, 0x4000, DecodeC_LW},
    {"C_LD", 0xE003, 0x6000, DecodeC_LD, RV64 | RV128},
    {"C_SW", 0xE003, 0xC000, DecodeC_SW},
    {"C_SD", 0xE003, 0xE000, DecodeC_SD, RV64 | RV128},
    {"C_J", 0xE003, 0xA001, DecodeC_J},
    {"C_JR", 0xF07F, 0x8002, DecodeC_JR},
    {"C_JALR", 0xF07F, 0x9002, DecodeC_JALR},
    {"C_BNEZ", 0xE003, 0xE001, DecodeC_BNEZ},
    {"C_BEQZ", 0xE003, 0xC001, DecodeC_BEQZ},
    {"C_LI", 0xE003, 0x4001, DecodeC_LI},
    {"C_LUI_ADDI16SP", 0xE003, 0x6001, DecodeC_LUI_ADDI16SP},
    {"C_ADDI", 0xE003, 0x1, DecodeC_ADDI},
    {"C_ADDIW", 0xE003, 0x2001, DecodeC_ADDIW, RV64 | RV128},
    {"C_ADDI4SPN", 0xE003, 0x0, DecodeC_ADDI4SPN},
    {"C_SLLI", 0xE003, 0x2, DecodeC_SLLI, RV64 | RV128},
    {"C_SRLI", 0xEC03, 0x8001, DecodeC_SRLI, RV64 | RV128},
    {"C_SRAI", 0xEC03, 0x8401, DecodeC_SRAI, RV64 | RV128},
    {"C_ANDI", 0xEC03, 0x8801, DecodeC_ANDI},
    {"C_MV", 0xF003, 0x8002, DecodeC_MV},
    {"C_ADD", 0xF003, 0x9002, DecodeC_ADD},
    {"C_AND", 0xFC63, 0x8C61, DecodeC_AND},
    {"C_OR", 0xFC63, 0x8C41, DecodeC_OR},
    {"C_XOR", 0xFC63, 0x8C21, DecodeC_XOR},
    {"C_SUB", 0xFC63, 0x8C01, DecodeC_SUB},
    {"C_SUBW", 0xFC63, 0x9C01, DecodeC_SUBW, RV64 | RV128},
    {"C_ADDW", 0xFC63, 0x9C21, DecodeC_ADDW, RV64 | RV128},
    // RV32FC //
    {"FLW", 0xE003, 0x6000, DecodeC_FLW, RV32},
    {"FSW", 0xE003, 0xE000, DecodeC_FSW, RV32},
    {"FLWSP", 0xE003, 0x6002, DecodeC_FLWSP, RV32},
    {"FSWSP", 0xE003, 0xE002, DecodeC_FSWSP, RV32},
    // RVDC //
    {"FLDSP", 0xE003, 0x2002, DecodeC_FLDSP, RV32 | RV64},
    {"FSDSP", 0xE003, 0xA002, DecodeC_FSDSP, RV32 | RV64},
    {"FLD", 0xE003, 0x2000, DecodeC_FLD, RV32 | RV64},
    {"FSD", 0xE003, 0xA000, DecodeC_FSD, RV32 | RV64},

    // RV32F (Extension for Single-Precision Floating-Point) //
    {"FLW", 0x707F, 0x2007, DecodeIType<FLW>},
    {"FSW", 0x707F, 0x2027, DecodeSType<FSW>},
    {"FMADD_S", 0x600007F, 0x43, DecodeR4Type<FMADD_S>},
    {"FMSUB_S", 0x600007F, 0x47, DecodeR4Type<FMSUB_S>},
    {"FNMSUB_S", 0x600007F, 0x4B, DecodeR4Type<FNMSUB_S>},
    {"FNMADD_S", 0x600007F, 0x4F, DecodeR4Type<FNMADD_S>},
    {"FADD_S", 0xFE00007F, 0x53, DecodeRType<FADD_S>},
    {"FSUB_S", 0xFE00007F, 0x8000053, DecodeRType<FSUB_S>},
    {"FMUL_S", 0xFE00007F, 0x10000053, DecodeRType<FMUL_S>},
    {"FDIV_S", 0xFE00007F, 0x18000053, DecodeRType<FDIV_S>},
    {"FSQRT_S", 0xFFF0007F, 0x58000053, DecodeIType<FSQRT_S>},
    {"FSGNJ_S", 0xFE00707F, 0x20000053, DecodeRType<FSGNJ_S>},
    {"FSGNJN_S", 0xFE00707F, 0x20001053, DecodeRType<FSGNJN_S>},
    {"FSGNJX_S", 0xFE00707F, 0x20002053, DecodeRType<FSGNJX_S>},
    {"FMIN_S", 0xFE00707F, 0x28000053, DecodeRType<FMIN_S>},
    {"FMAX_S", 0xFE00707F, 0x28001053, DecodeRType<FMAX_S>},
    {"FCVT_W_S", 0xFFF0007F, 0xC0000053, DecodeIType<FCVT_W_S>},
    {"FCVT_WU_S", 0xFFF0007F, 0xC0100053, DecodeIType<FCVT_WU_S>},
    {"FMV_X_W", 0xFFF0707F, 0xE0000053, DecodeIType<FMV_X_W>},
    {"FEQ_S", 0xFE00707F, 0xA0002053, DecodeRType<FEQ_S>},
    {"FLT_S", 0xFE00707F, 0xA0001053, DecodeRType<FLT_S>},
    {"FLE_S", 0xFE00707F, 0xA0000053, DecodeRType<FLE_S>},
    {"FCLASS_S", 0xFFF0707F, 0xE0001053, DecodeIType<FCLASS_S>},
    {"FCVT_S_W", 0xFFF0007F, 0xD0000053, DecodeIType<FCVT_S_W>},
    {"FCVT_S_WU", 0xFFF0007F, 0xD0100053, DecodeIType<FCVT_S_WU>},
    {"FMV_W_X", 0xFFF0707F, 0xF0000053, DecodeIType<FMV_W_X>},

    // RV64F (Extension for Single-Precision Floating-Point) //
    {"FCVT_L_S", 0xFFF0007F, 0xC0200053, DecodeIType<FCVT_L_S>},
    {"FCVT_LU_S", 0xFFF0007F, 0xC0300053, DecodeIType<FCVT_LU_S>},
    {"FCVT_S_L", 0xFFF0007F, 0xD0200053, DecodeIType<FCVT_S_L>},
    {"FCVT_S_LU", 0xFFF0007F, 0xD0300053, DecodeIType<FCVT_S_LU>},

    // RV32D (Extension for Double-Precision Floating-Point) //
    {"FLD", 0x707F, 0x3007, DecodeIType<FLD>},
    {"FSD", 0x707F, 0x3027, DecodeSType<FSD>},
    {"FMADD_D", 0x600007F, 0x2000043, DecodeR4Type<FMADD_D>},
    {"FMSUB_D", 0x600007F, 0x2000047, DecodeR4Type<FMSUB_D>},
    {"FNMSUB_D", 0x600007F, 0x200004B, DecodeR4Type<FNMSUB_D>},
    {"FNMADD_D", 0x600007F, 0x200004F, DecodeR4Type<FNMADD_D>},
    {"FADD_D", 0xFE00007F, 0x2000053, DecodeRType<FADD_D>},
    {"FSUB_D", 0xFE00007F, 0xA000053, DecodeRType<FSUB_D>},
    {"FMUL_D", 0xFE00007F, 0x12000053, DecodeRType<FMUL_D>},
    {"FDIV_D", 0xFE00007F, 0x1A000053, DecodeRType<FDIV_D>},
    {"FSQRT_D", 0xFFF0007F, 0x5A000053, DecodeIType<FSQRT_D>},
    {"FSGNJ_D", 0xFE00707F, 0x22000053, DecodeRType<FSGNJ_D>},
    {"FSGNJN_D", 0xFE00707F, 0x22001053, DecodeRType<FSGNJN_D>},
    {"FSGNJX_D", 0xFE00707F, 0x22002053, DecodeRType<FSGNJX_D>},
    {"FMIN_D", 0xFE00707F, 0x2A000053, DecodeRType<FMIN_D>},
    {"FMAX_D", 0xFE00707F, 0x2A001053, DecodeRType<FMAX_D>},
    {"FCVT_S_D", 0xFFF0007F, 0x40100053, DecodeIType<FCVT_S_D>},
    {"FCVT_D_S", 0xFFF0007F, 0x42000053, DecodeIType<FCVT_D_S>},
    {"FEQ_D", 0xFE00707F, 0xA2002053, DecodeRType<FEQ_D>},
    {"FLT_D", 0xFE00707F, 0xA2001053, DecodeRType<FLT_D>},
    {"FLE_D", 0xFE00707F, 0xA2000053, DecodeRType<FLE_D>},
    {"FCLASS_D", 0xFFF0707F, 0xE2001053, DecodeIType<FCLASS_D>},
    {"FCVT_W_D", 0xFFF0007F, 0xC2000053, DecodeIType<FCVT_W_D>},
    {"FCVT_WU_D", 0xFFF0007F, 0xC2100053, DecodeIType<FCVT_WU_D>},
    {"FCVT_D_W", 0xFFF0007F, 0xD2000053, DecodeIType<FCVT_D_W>},
    {"FCVT_D_WU", 0xFFF0007F, 0xD2100053, DecodeIType<FCVT_D_WU>},

    // RV64D (Extension for Double-Precision Floating-Point) //
    {"FCVT_L_D", 0xFFF0007F, 0xC2200053, DecodeIType<FCVT_L_D>},
    {"FCVT_LU_D", 0xFFF0007F, 0xC2300053, DecodeIType<FCVT_LU_D>},
    {"FMV_X_D", 0xFFF0707F, 0xE2000053, DecodeIType<FMV_X_D>},
    {"FCVT_D_L", 0xFFF0007F, 0xD2200053, DecodeIType<FCVT_D_L>},
    {"FCVT_D_LU", 0xFFF0007F, 0xD2300053, DecodeIType<FCVT_D_LU>},
    {"FMV_D_X", 0xFFF0707F, 0xF2000053, DecodeIType<FMV_D_X>},
};

std::optional<DecodeResult> EmulateInstructionRISCV::Decode(uint32_t inst) {
  Log *log = GetLog(LLDBLog::Unwind);

  uint16_t try_rvc = uint16_t(inst & 0x0000ffff);
  uint8_t inst_type = RV64;

  // Try to get size of RISCV instruction.
  // 1.2 Instruction Length Encoding
  bool is_16b = (inst & 0b11) != 0b11;
  bool is_32b = (inst & 0x1f) != 0x1f;
  bool is_48b = (inst & 0x3f) != 0x1f;
  bool is_64b = (inst & 0x7f) != 0x3f;
  if (is_16b)
    m_last_size = 2;
  else if (is_32b)
    m_last_size = 4;
  else if (is_48b)
    m_last_size = 6;
  else if (is_64b)
    m_last_size = 8;
  else
    // Not Valid
    m_last_size = std::nullopt;

  // if we have ArchSpec::eCore_riscv128 in the future,
  // we also need to check it here
  if (m_arch.GetCore() == ArchSpec::eCore_riscv32)
    inst_type = RV32;

  for (const InstrPattern &pat : PATTERNS) {
    if ((inst & pat.type_mask) == pat.eigen &&
        (inst_type & pat.inst_type) != 0) {
      LLDB_LOGF(
          log, "EmulateInstructionRISCV::%s: inst(%x at %" PRIx64 ") was decoded to %s",
          __FUNCTION__, inst, m_addr, pat.name);
      auto decoded = is_16b ? pat.decode(try_rvc) : pat.decode(inst);
      return DecodeResult{decoded, inst, is_16b, pat};
    }
  }
  LLDB_LOGF(log, "EmulateInstructionRISCV::%s: inst(0x%x) was unsupported",
            __FUNCTION__, inst);
  return std::nullopt;
}

class Executor {
  EmulateInstructionRISCV &m_emu;
  bool m_ignore_cond;
  bool m_is_rvc;

public:
  // also used in EvaluateInstruction()
  static uint64_t size(bool is_rvc) { return is_rvc ? 2 : 4; }

private:
  uint64_t delta() { return size(m_is_rvc); }

public:
  Executor(EmulateInstructionRISCV &emulator, bool ignoreCond, bool is_rvc)
      : m_emu(emulator), m_ignore_cond(ignoreCond), m_is_rvc(is_rvc) {}

  bool operator()(LUI inst) { return inst.rd.Write(m_emu, SignExt(inst.imm)); }
  bool operator()(AUIPC inst) {
    return transformOptional(m_emu.ReadPC(),
                             [&](uint64_t pc) {
                               return inst.rd.Write(m_emu,
                                                    SignExt(inst.imm) + pc);
                             })
        .value_or(false);
  }
  bool operator()(JAL inst) {
    return transformOptional(m_emu.ReadPC(),
                             [&](uint64_t pc) {
                               return inst.rd.Write(m_emu, pc + delta()) &&
                                      m_emu.WritePC(SignExt(inst.imm) + pc);
                             })
        .value_or(false);
  }
  bool operator()(JALR inst) {
    return transformOptional(zipOpt(m_emu.ReadPC(), inst.rs1.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [pc, rs1] = tup;
                               return inst.rd.Write(m_emu, pc + delta()) &&
                                      m_emu.WritePC((SignExt(inst.imm) + rs1) &
                                                    ~1);
                             })
        .value_or(false);
  }
  bool operator()(B inst) {
    return transformOptional(zipOpt(m_emu.ReadPC(), inst.rs1.Read(m_emu),
                                    inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [pc, rs1, rs2] = tup;
                               if (m_ignore_cond ||
                                   CompareB(rs1, rs2, inst.funct3))
                                 return m_emu.WritePC(SignExt(inst.imm) + pc);
                               return true;
                             })
        .value_or(false);
  }
  bool operator()(LB inst) {
    return Load<LB, uint8_t, int8_t>(m_emu, inst, SextW);
  }
  bool operator()(LH inst) {
    return Load<LH, uint16_t, int16_t>(m_emu, inst, SextW);
  }
  bool operator()(LW inst) {
    return Load<LW, uint32_t, int32_t>(m_emu, inst, SextW);
  }
  bool operator()(LBU inst) {
    return Load<LBU, uint8_t, uint8_t>(m_emu, inst, ZextD);
  }
  bool operator()(LHU inst) {
    return Load<LHU, uint16_t, uint16_t>(m_emu, inst, ZextD);
  }
  bool operator()(SB inst) { return Store<SB, uint8_t>(m_emu, inst); }
  bool operator()(SH inst) { return Store<SH, uint16_t>(m_emu, inst); }
  bool operator()(SW inst) { return Store<SW, uint32_t>(m_emu, inst); }
  bool operator()(ADDI inst) {
    return transformOptional(inst.rs1.ReadI64(m_emu),
                             [&](int64_t rs1) {
                               return inst.rd.Write(
                                   m_emu, rs1 + int64_t(SignExt(inst.imm)));
                             })
        .value_or(false);
  }
  bool operator()(SLTI inst) {
    return transformOptional(inst.rs1.ReadI64(m_emu),
                             [&](int64_t rs1) {
                               return inst.rd.Write(
                                   m_emu, rs1 < int64_t(SignExt(inst.imm)));
                             })
        .value_or(false);
  }
  bool operator()(SLTIU inst) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](uint64_t rs1) {
                               return inst.rd.Write(
                                   m_emu, rs1 < uint64_t(SignExt(inst.imm)));
                             })
        .value_or(false);
  }
  bool operator()(XORI inst) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](uint64_t rs1) {
                               return inst.rd.Write(
                                   m_emu, rs1 ^ uint64_t(SignExt(inst.imm)));
                             })
        .value_or(false);
  }
  bool operator()(ORI inst) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](uint64_t rs1) {
                               return inst.rd.Write(
                                   m_emu, rs1 | uint64_t(SignExt(inst.imm)));
                             })
        .value_or(false);
  }
  bool operator()(ANDI inst) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](uint64_t rs1) {
                               return inst.rd.Write(
                                   m_emu, rs1 & uint64_t(SignExt(inst.imm)));
                             })
        .value_or(false);
  }
  bool operator()(ADD inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu, rs1 + rs2);
                             })
        .value_or(false);
  }
  bool operator()(SUB inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu, rs1 - rs2);
                             })
        .value_or(false);
  }
  bool operator()(SLL inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu,
                                                    rs1 << (rs2 & 0b111111));
                             })
        .value_or(false);
  }
  bool operator()(SLT inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI64(m_emu), inst.rs2.ReadI64(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 return inst.rd.Write(m_emu, rs1 < rs2);
               })
        .value_or(false);
  }
  bool operator()(SLTU inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu, rs1 < rs2);
                             })
        .value_or(false);
  }
  bool operator()(XOR inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu, rs1 ^ rs2);
                             })
        .value_or(false);
  }
  bool operator()(SRL inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu,
                                                    rs1 >> (rs2 & 0b111111));
                             })
        .value_or(false);
  }
  bool operator()(SRA inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI64(m_emu), inst.rs2.Read(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 return inst.rd.Write(m_emu, rs1 >> (rs2 & 0b111111));
               })
        .value_or(false);
  }
  bool operator()(OR inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu, rs1 | rs2);
                             })
        .value_or(false);
  }
  bool operator()(AND inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu, rs1 & rs2);
                             })
        .value_or(false);
  }
  bool operator()(LWU inst) {
    return Load<LWU, uint32_t, uint32_t>(m_emu, inst, ZextD);
  }
  bool operator()(LD inst) {
    return Load<LD, uint64_t, uint64_t>(m_emu, inst, ZextD);
  }
  bool operator()(SD inst) { return Store<SD, uint64_t>(m_emu, inst); }
  bool operator()(SLLI inst) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](uint64_t rs1) {
                               return inst.rd.Write(m_emu, rs1 << inst.shamt);
                             })
        .value_or(false);
  }
  bool operator()(SRLI inst) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](uint64_t rs1) {
                               return inst.rd.Write(m_emu, rs1 >> inst.shamt);
                             })
        .value_or(false);
  }
  bool operator()(SRAI inst) {
    return transformOptional(inst.rs1.ReadI64(m_emu),
                             [&](int64_t rs1) {
                               return inst.rd.Write(m_emu, rs1 >> inst.shamt);
                             })
        .value_or(false);
  }
  bool operator()(ADDIW inst) {
    return transformOptional(inst.rs1.ReadI32(m_emu),
                             [&](int32_t rs1) {
                               return inst.rd.Write(
                                   m_emu, SextW(rs1 + SignExt(inst.imm)));
                             })
        .value_or(false);
  }
  bool operator()(SLLIW inst) {
    return transformOptional(inst.rs1.ReadU32(m_emu),
                             [&](uint32_t rs1) {
                               return inst.rd.Write(m_emu,
                                                    SextW(rs1 << inst.shamt));
                             })
        .value_or(false);
  }
  bool operator()(SRLIW inst) {
    return transformOptional(inst.rs1.ReadU32(m_emu),
                             [&](uint32_t rs1) {
                               return inst.rd.Write(m_emu,
                                                    SextW(rs1 >> inst.shamt));
                             })
        .value_or(false);
  }
  bool operator()(SRAIW inst) {
    return transformOptional(inst.rs1.ReadI32(m_emu),
                             [&](int32_t rs1) {
                               return inst.rd.Write(m_emu,
                                                    SextW(rs1 >> inst.shamt));
                             })
        .value_or(false);
  }
  bool operator()(ADDW inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu,
                                                    SextW(uint32_t(rs1 + rs2)));
                             })
        .value_or(false);
  }
  bool operator()(SUBW inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu,
                                                    SextW(uint32_t(rs1 - rs2)));
                             })
        .value_or(false);
  }
  bool operator()(SLLW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadU32(m_emu), inst.rs2.ReadU32(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 return inst.rd.Write(m_emu, SextW(rs1 << (rs2 & 0b11111)));
               })
        .value_or(false);
  }
  bool operator()(SRLW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadU32(m_emu), inst.rs2.ReadU32(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 return inst.rd.Write(m_emu, SextW(rs1 >> (rs2 & 0b11111)));
               })
        .value_or(false);
  }
  bool operator()(SRAW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI32(m_emu), inst.rs2.Read(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 return inst.rd.Write(m_emu, SextW(rs1 >> (rs2 & 0b11111)));
               })
        .value_or(false);
  }
  // RV32M & RV64M (Integer Multiplication and Division Extension) //
  bool operator()(MUL inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               return inst.rd.Write(m_emu, rs1 * rs2);
                             })
        .value_or(false);
  }
  bool operator()(MULH inst) {
    return transformOptional(
               zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 // signed * signed
                 auto mul = APInt(128, rs1, true) * APInt(128, rs2, true);
                 return inst.rd.Write(m_emu,
                                      mul.ashr(64).trunc(64).getZExtValue());
               })
        .value_or(false);
  }
  bool operator()(MULHSU inst) {
    return transformOptional(
               zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 // signed * unsigned
                 auto mul =
                     APInt(128, rs1, true).zext(128) * APInt(128, rs2, false);
                 return inst.rd.Write(m_emu,
                                      mul.lshr(64).trunc(64).getZExtValue());
               })
        .value_or(false);
  }
  bool operator()(MULHU inst) {
    return transformOptional(
               zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 // unsigned * unsigned
                 auto mul = APInt(128, rs1, false) * APInt(128, rs2, false);
                 return inst.rd.Write(m_emu,
                                      mul.lshr(64).trunc(64).getZExtValue());
               })
        .value_or(false);
  }
  bool operator()(DIV inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI64(m_emu), inst.rs2.ReadI64(m_emu)),
               [&](auto &&tup) {
                 auto [dividend, divisor] = tup;

                 if (divisor == 0)
                   return inst.rd.Write(m_emu, UINT64_MAX);

                 if (dividend == INT64_MIN && divisor == -1)
                   return inst.rd.Write(m_emu, dividend);

                 return inst.rd.Write(m_emu, dividend / divisor);
               })
        .value_or(false);
  }
  bool operator()(DIVU inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [dividend, divisor] = tup;

                               if (divisor == 0)
                                 return inst.rd.Write(m_emu, UINT64_MAX);

                               return inst.rd.Write(m_emu, dividend / divisor);
                             })
        .value_or(false);
  }
  bool operator()(REM inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI64(m_emu), inst.rs2.ReadI64(m_emu)),
               [&](auto &&tup) {
                 auto [dividend, divisor] = tup;

                 if (divisor == 0)
                   return inst.rd.Write(m_emu, dividend);

                 if (dividend == INT64_MIN && divisor == -1)
                   return inst.rd.Write(m_emu, 0);

                 return inst.rd.Write(m_emu, dividend % divisor);
               })
        .value_or(false);
  }
  bool operator()(REMU inst) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu), inst.rs2.Read(m_emu)),
                             [&](auto &&tup) {
                               auto [dividend, divisor] = tup;

                               if (divisor == 0)
                                 return inst.rd.Write(m_emu, dividend);

                               return inst.rd.Write(m_emu, dividend % divisor);
                             })
        .value_or(false);
  }
  bool operator()(MULW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI32(m_emu), inst.rs2.ReadI32(m_emu)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 return inst.rd.Write(m_emu, SextW(rs1 * rs2));
               })
        .value_or(false);
  }
  bool operator()(DIVW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI32(m_emu), inst.rs2.ReadI32(m_emu)),
               [&](auto &&tup) {
                 auto [dividend, divisor] = tup;

                 if (divisor == 0)
                   return inst.rd.Write(m_emu, UINT64_MAX);

                 if (dividend == INT32_MIN && divisor == -1)
                   return inst.rd.Write(m_emu, SextW(dividend));

                 return inst.rd.Write(m_emu, SextW(dividend / divisor));
               })
        .value_or(false);
  }
  bool operator()(DIVUW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadU32(m_emu), inst.rs2.ReadU32(m_emu)),
               [&](auto &&tup) {
                 auto [dividend, divisor] = tup;

                 if (divisor == 0)
                   return inst.rd.Write(m_emu, UINT64_MAX);

                 return inst.rd.Write(m_emu, SextW(dividend / divisor));
               })
        .value_or(false);
  }
  bool operator()(REMW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadI32(m_emu), inst.rs2.ReadI32(m_emu)),
               [&](auto &&tup) {
                 auto [dividend, divisor] = tup;

                 if (divisor == 0)
                   return inst.rd.Write(m_emu, SextW(dividend));

                 if (dividend == INT32_MIN && divisor == -1)
                   return inst.rd.Write(m_emu, 0);

                 return inst.rd.Write(m_emu, SextW(dividend % divisor));
               })
        .value_or(false);
  }
  bool operator()(REMUW inst) {
    return transformOptional(
               zipOpt(inst.rs1.ReadU32(m_emu), inst.rs2.ReadU32(m_emu)),
               [&](auto &&tup) {
                 auto [dividend, divisor] = tup;

                 if (divisor == 0)
                   return inst.rd.Write(m_emu, SextW(dividend));

                 return inst.rd.Write(m_emu, SextW(dividend % divisor));
               })
        .value_or(false);
  }
  // RV32A & RV64A (The standard atomic instruction extension) //
  bool operator()(LR_W) { return AtomicSequence(m_emu); }
  bool operator()(LR_D) { return AtomicSequence(m_emu); }
  bool operator()(SC_W) {
    llvm_unreachable("should be handled in AtomicSequence");
  }
  bool operator()(SC_D) {
    llvm_unreachable("should be handled in AtomicSequence");
  }
  bool operator()(AMOSWAP_W inst) {
    return AtomicSwap<AMOSWAP_W, uint32_t>(m_emu, inst, 4, SextW);
  }
  bool operator()(AMOADD_W inst) {
    return AtomicADD<AMOADD_W, uint32_t>(m_emu, inst, 4, SextW);
  }
  bool operator()(AMOXOR_W inst) {
    return AtomicBitOperate<AMOXOR_W, uint32_t>(
        m_emu, inst, 4, SextW, [](uint32_t a, uint32_t b) { return a ^ b; });
  }
  bool operator()(AMOAND_W inst) {
    return AtomicBitOperate<AMOAND_W, uint32_t>(
        m_emu, inst, 4, SextW, [](uint32_t a, uint32_t b) { return a & b; });
  }
  bool operator()(AMOOR_W inst) {
    return AtomicBitOperate<AMOOR_W, uint32_t>(
        m_emu, inst, 4, SextW, [](uint32_t a, uint32_t b) { return a | b; });
  }
  bool operator()(AMOMIN_W inst) {
    return AtomicCmp<AMOMIN_W, uint32_t>(
        m_emu, inst, 4, SextW, [](uint32_t a, uint32_t b) {
          return uint32_t(std::min(int32_t(a), int32_t(b)));
        });
  }
  bool operator()(AMOMAX_W inst) {
    return AtomicCmp<AMOMAX_W, uint32_t>(
        m_emu, inst, 4, SextW, [](uint32_t a, uint32_t b) {
          return uint32_t(std::max(int32_t(a), int32_t(b)));
        });
  }
  bool operator()(AMOMINU_W inst) {
    return AtomicCmp<AMOMINU_W, uint32_t>(
        m_emu, inst, 4, SextW,
        [](uint32_t a, uint32_t b) { return std::min(a, b); });
  }
  bool operator()(AMOMAXU_W inst) {
    return AtomicCmp<AMOMAXU_W, uint32_t>(
        m_emu, inst, 4, SextW,
        [](uint32_t a, uint32_t b) { return std::max(a, b); });
  }
  bool operator()(AMOSWAP_D inst) {
    return AtomicSwap<AMOSWAP_D, uint64_t>(m_emu, inst, 8, ZextD);
  }
  bool operator()(AMOADD_D inst) {
    return AtomicADD<AMOADD_D, uint64_t>(m_emu, inst, 8, ZextD);
  }
  bool operator()(AMOXOR_D inst) {
    return AtomicBitOperate<AMOXOR_D, uint64_t>(
        m_emu, inst, 8, ZextD, [](uint64_t a, uint64_t b) { return a ^ b; });
  }
  bool operator()(AMOAND_D inst) {
    return AtomicBitOperate<AMOAND_D, uint64_t>(
        m_emu, inst, 8, ZextD, [](uint64_t a, uint64_t b) { return a & b; });
  }
  bool operator()(AMOOR_D inst) {
    return AtomicBitOperate<AMOOR_D, uint64_t>(
        m_emu, inst, 8, ZextD, [](uint64_t a, uint64_t b) { return a | b; });
  }
  bool operator()(AMOMIN_D inst) {
    return AtomicCmp<AMOMIN_D, uint64_t>(
        m_emu, inst, 8, ZextD, [](uint64_t a, uint64_t b) {
          return uint64_t(std::min(int64_t(a), int64_t(b)));
        });
  }
  bool operator()(AMOMAX_D inst) {
    return AtomicCmp<AMOMAX_D, uint64_t>(
        m_emu, inst, 8, ZextD, [](uint64_t a, uint64_t b) {
          return uint64_t(std::max(int64_t(a), int64_t(b)));
        });
  }
  bool operator()(AMOMINU_D inst) {
    return AtomicCmp<AMOMINU_D, uint64_t>(
        m_emu, inst, 8, ZextD,
        [](uint64_t a, uint64_t b) { return std::min(a, b); });
  }
  bool operator()(AMOMAXU_D inst) {
    return AtomicCmp<AMOMAXU_D, uint64_t>(
        m_emu, inst, 8, ZextD,
        [](uint64_t a, uint64_t b) { return std::max(a, b); });
  }
  template <typename T>
  bool F_Load(T inst, const fltSemantics &(*semantics)(),
              unsigned int numBits) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](auto &&rs1) {
                               uint64_t addr = rs1 + uint64_t(inst.imm);
                               uint64_t bits = *m_emu.ReadMem<uint64_t>(addr);
                               APFloat f(semantics(), APInt(numBits, bits));
                               return inst.rd.WriteAPFloat(m_emu, f);
                             })
        .value_or(false);
  }
  bool operator()(FLW inst) { return F_Load(inst, &APFloat::IEEEsingle, 32); }
  template <typename T> bool F_Store(T inst, bool isDouble) {
    return transformOptional(zipOpt(inst.rs1.Read(m_emu),
                                    inst.rs2.ReadAPFloat(m_emu, isDouble)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               uint64_t addr = rs1 + uint64_t(inst.imm);
                               uint64_t bits =
                                   rs2.bitcastToAPInt().getZExtValue();
                               return m_emu.WriteMem<uint64_t>(addr, bits);
                             })
        .value_or(false);
  }
  bool operator()(FSW inst) { return F_Store(inst, false); }
  std::tuple<bool, APFloat> FusedMultiplyAdd(APFloat rs1, APFloat rs2,
                                             APFloat rs3) {
    auto opStatus = rs1.fusedMultiplyAdd(rs2, rs3, m_emu.GetRoundingMode());
    auto res = m_emu.SetAccruedExceptions(opStatus);
    return {res, rs1};
  }
  template <typename T>
  bool FMA(T inst, bool isDouble, float rs2_sign, float rs3_sign) {
    return transformOptional(zipOpt(inst.rs1.ReadAPFloat(m_emu, isDouble),
                                    inst.rs2.ReadAPFloat(m_emu, isDouble),
                                    inst.rs3.ReadAPFloat(m_emu, isDouble)),
                             [&](auto &&tup) {
                               auto [rs1, rs2, rs3] = tup;
                               rs2.copySign(APFloat(rs2_sign));
                               rs3.copySign(APFloat(rs3_sign));
                               auto [res, f] = FusedMultiplyAdd(rs1, rs2, rs3);
                               return res && inst.rd.WriteAPFloat(m_emu, f);
                             })
        .value_or(false);
  }
  bool operator()(FMADD_S inst) { return FMA(inst, false, 1.0f, 1.0f); }
  bool operator()(FMSUB_S inst) { return FMA(inst, false, 1.0f, -1.0f); }
  bool operator()(FNMSUB_S inst) { return FMA(inst, false, -1.0f, 1.0f); }
  bool operator()(FNMADD_S inst) { return FMA(inst, false, -1.0f, -1.0f); }
  template <typename T>
  bool F_Op(T inst, bool isDouble,
            APFloat::opStatus (APFloat::*f)(const APFloat &RHS,
                                            APFloat::roundingMode RM)) {
    return transformOptional(zipOpt(inst.rs1.ReadAPFloat(m_emu, isDouble),
                                    inst.rs2.ReadAPFloat(m_emu, isDouble)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               auto res =
                                   ((&rs1)->*f)(rs2, m_emu.GetRoundingMode());
                               inst.rd.WriteAPFloat(m_emu, rs1);
                               return m_emu.SetAccruedExceptions(res);
                             })
        .value_or(false);
  }
  bool operator()(FADD_S inst) { return F_Op(inst, false, &APFloat::add); }
  bool operator()(FSUB_S inst) { return F_Op(inst, false, &APFloat::subtract); }
  bool operator()(FMUL_S inst) { return F_Op(inst, false, &APFloat::multiply); }
  bool operator()(FDIV_S inst) { return F_Op(inst, false, &APFloat::divide); }
  bool operator()(FSQRT_S inst) {
    // TODO: APFloat doesn't have a sqrt function.
    return false;
  }
  template <typename T> bool F_SignInj(T inst, bool isDouble, bool isNegate) {
    return transformOptional(zipOpt(inst.rs1.ReadAPFloat(m_emu, isDouble),
                                    inst.rs2.ReadAPFloat(m_emu, isDouble)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               if (isNegate)
                                 rs2.changeSign();
                               rs1.copySign(rs2);
                               return inst.rd.WriteAPFloat(m_emu, rs1);
                             })
        .value_or(false);
  }
  bool operator()(FSGNJ_S inst) { return F_SignInj(inst, false, false); }
  bool operator()(FSGNJN_S inst) { return F_SignInj(inst, false, true); }
  template <typename T> bool F_SignInjXor(T inst, bool isDouble) {
    return transformOptional(zipOpt(inst.rs1.ReadAPFloat(m_emu, isDouble),
                                    inst.rs2.ReadAPFloat(m_emu, isDouble)),
                             [&](auto &&tup) {
                               auto [rs1, rs2] = tup;
                               // spec: the sign bit is the XOR of the sign bits
                               // of rs1 and rs2. if rs1 and rs2 have the same
                               // signs set rs1 to positive else set rs1 to
                               // negative
                               if (rs1.isNegative() == rs2.isNegative()) {
                                 rs1.clearSign();
                               } else {
                                 rs1.clearSign();
                                 rs1.changeSign();
                               }
                               return inst.rd.WriteAPFloat(m_emu, rs1);
                             })
        .value_or(false);
  }
  bool operator()(FSGNJX_S inst) { return F_SignInjXor(inst, false); }
  template <typename T>
  bool F_MAX_MIN(T inst, bool isDouble,
                 APFloat (*f)(const APFloat &A, const APFloat &B)) {
    return transformOptional(
               zipOpt(inst.rs1.ReadAPFloat(m_emu, isDouble),
                      inst.rs2.ReadAPFloat(m_emu, isDouble)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 // If both inputs are NaNs, the result is the canonical NaN.
                 // If only one operand is a NaN, the result is the non-NaN
                 // operand. Signaling NaN inputs set the invalid operation
                 // exception flag, even when the result is not NaN.
                 if (rs1.isNaN() || rs2.isNaN())
                   m_emu.SetAccruedExceptions(APFloat::opInvalidOp);
                 if (rs1.isNaN() && rs2.isNaN()) {
                   auto canonicalNaN = APFloat::getQNaN(rs1.getSemantics());
                   return inst.rd.WriteAPFloat(m_emu, canonicalNaN);
                 }
                 return inst.rd.WriteAPFloat(m_emu, f(rs1, rs2));
               })
        .value_or(false);
  }
  bool operator()(FMIN_S inst) { return F_MAX_MIN(inst, false, minnum); }
  bool operator()(FMAX_S inst) { return F_MAX_MIN(inst, false, maxnum); }
  bool operator()(FCVT_W_S inst) {
    return FCVT_i2f<FCVT_W_S, int32_t, float>(inst, false,
                                              &APFloat::convertToFloat);
  }
  bool operator()(FCVT_WU_S inst) {
    return FCVT_i2f<FCVT_WU_S, uint32_t, float>(inst, false,
                                                &APFloat::convertToFloat);
  }
  template <typename T> bool FMV_f2i(T inst, bool isDouble) {
    return transformOptional(
               inst.rs1.ReadAPFloat(m_emu, isDouble),
               [&](auto &&rs1) {
                 if (rs1.isNaN()) {
                   if (isDouble)
                     return inst.rd.Write(m_emu, 0x7ff8'0000'0000'0000);
                   else
                     return inst.rd.Write(m_emu, 0x7fc0'0000);
                 }
                 auto bits = rs1.bitcastToAPInt().getZExtValue();
                 if (isDouble)
                   return inst.rd.Write(m_emu, bits);
                 else
                   return inst.rd.Write(m_emu, uint64_t(bits & 0xffff'ffff));
               })
        .value_or(false);
  }
  bool operator()(FMV_X_W inst) { return FMV_f2i(inst, false); }
  enum F_CMP {
    FEQ,
    FLT,
    FLE,
  };
  template <typename T> bool F_Compare(T inst, bool isDouble, F_CMP cmp) {
    return transformOptional(
               zipOpt(inst.rs1.ReadAPFloat(m_emu, isDouble),
                      inst.rs2.ReadAPFloat(m_emu, isDouble)),
               [&](auto &&tup) {
                 auto [rs1, rs2] = tup;
                 if (rs1.isNaN() || rs2.isNaN()) {
                   if (cmp == FEQ) {
                     if (rs1.isSignaling() || rs2.isSignaling()) {
                       auto res =
                           m_emu.SetAccruedExceptions(APFloat::opInvalidOp);
                       return res && inst.rd.Write(m_emu, 0);
                     }
                   }
                   auto res = m_emu.SetAccruedExceptions(APFloat::opInvalidOp);
                   return res && inst.rd.Write(m_emu, 0);
                 }
                 switch (cmp) {
                 case FEQ:
                   return inst.rd.Write(m_emu,
                                        rs1.compare(rs2) == APFloat::cmpEqual);
                 case FLT:
                   return inst.rd.Write(m_emu, rs1.compare(rs2) ==
                                                   APFloat::cmpLessThan);
                 case FLE:
                   return inst.rd.Write(m_emu, rs1.compare(rs2) !=
                                                   APFloat::cmpGreaterThan);
                 }
                 llvm_unreachable("unsupported F_CMP");
               })
        .value_or(false);
  }

  bool operator()(FEQ_S inst) { return F_Compare(inst, false, FEQ); }
  bool operator()(FLT_S inst) { return F_Compare(inst, false, FLT); }
  bool operator()(FLE_S inst) { return F_Compare(inst, false, FLE); }
  template <typename T> bool FCLASS(T inst, bool isDouble) {
    return transformOptional(inst.rs1.ReadAPFloat(m_emu, isDouble),
                             [&](auto &&rs1) {
                               uint64_t result = 0;
                               if (rs1.isInfinity() && rs1.isNegative())
                                 result |= 1 << 0;
                               // neg normal
                               if (rs1.isNormal() && rs1.isNegative())
                                 result |= 1 << 1;
                               // neg subnormal
                               if (rs1.isDenormal() && rs1.isNegative())
                                 result |= 1 << 2;
                               if (rs1.isNegZero())
                                 result |= 1 << 3;
                               if (rs1.isPosZero())
                                 result |= 1 << 4;
                               // pos normal
                               if (rs1.isNormal() && !rs1.isNegative())
                                 result |= 1 << 5;
                               // pos subnormal
                               if (rs1.isDenormal() && !rs1.isNegative())
                                 result |= 1 << 6;
                               if (rs1.isInfinity() && !rs1.isNegative())
                                 result |= 1 << 7;
                               if (rs1.isNaN()) {
                                 if (rs1.isSignaling())
                                   result |= 1 << 8;
                                 else
                                   result |= 1 << 9;
                               }
                               return inst.rd.Write(m_emu, result);
                             })
        .value_or(false);
  }
  bool operator()(FCLASS_S inst) { return FCLASS(inst, false); }
  template <typename T, typename E>
  bool FCVT_f2i(T inst, std::optional<E> (Rs::*f)(EmulateInstructionRISCV &emu),
                const fltSemantics &semantics) {
    return transformOptional(((&inst.rs1)->*f)(m_emu),
                             [&](auto &&rs1) {
                               APFloat apf(semantics, rs1);
                               return inst.rd.WriteAPFloat(m_emu, apf);
                             })
        .value_or(false);
  }
  bool operator()(FCVT_S_W inst) {
    return FCVT_f2i(inst, &Rs::ReadI32, APFloat::IEEEsingle());
  }
  bool operator()(FCVT_S_WU inst) {
    return FCVT_f2i(inst, &Rs::ReadU32, APFloat::IEEEsingle());
  }
  template <typename T, typename E>
  bool FMV_i2f(T inst, unsigned int numBits, E (APInt::*f)() const) {
    return transformOptional(inst.rs1.Read(m_emu),
                             [&](auto &&rs1) {
                               APInt apInt(numBits, rs1);
                               if (numBits == 32) // a.k.a. float
                                 apInt = APInt(numBits, NanUnBoxing(rs1));
                               APFloat apf((&apInt->*f)());
                               return inst.rd.WriteAPFloat(m_emu, apf);
                             })
        .value_or(false);
  }
  bool operator()(FMV_W_X inst) {
    return FMV_i2f(inst, 32, &APInt::bitsToFloat);
  }
  template <typename I, typename E, typename T>
  bool FCVT_i2f(I inst, bool isDouble, T (APFloat::*f)() const) {
    return transformOptional(inst.rs1.ReadAPFloat(m_emu, isDouble),
                             [&](auto &&rs1) {
                               E res = E((&rs1->*f)());
                               return inst.rd.Write(m_emu, uint64_t(res));
                             })
        .value_or(false);
  }
  bool operator()(FCVT_L_S inst) {
    return FCVT_i2f<FCVT_L_S, int64_t, float>(inst, false,
                                              &APFloat::convertToFloat);
  }
  bool operator()(FCVT_LU_S inst) {
    return FCVT_i2f<FCVT_LU_S, uint64_t, float>(inst, false,
                                                &APFloat::convertToFloat);
  }
  bool operator()(FCVT_S_L inst) {
    return FCVT_f2i(inst, &Rs::ReadI64, APFloat::IEEEsingle());
  }
  bool operator()(FCVT_S_LU inst) {
    return FCVT_f2i(inst, &Rs::Read, APFloat::IEEEsingle());
  }
  bool operator()(FLD inst) { return F_Load(inst, &APFloat::IEEEdouble, 64); }
  bool operator()(FSD inst) { return F_Store(inst, true); }
  bool operator()(FMADD_D inst) { return FMA(inst, true, 1.0f, 1.0f); }
  bool operator()(FMSUB_D inst) { return FMA(inst, true, 1.0f, -1.0f); }
  bool operator()(FNMSUB_D inst) { return FMA(inst, true, -1.0f, 1.0f); }
  bool operator()(FNMADD_D inst) { return FMA(inst, true, -1.0f, -1.0f); }
  bool operator()(FADD_D inst) { return F_Op(inst, true, &APFloat::add); }
  bool operator()(FSUB_D inst) { return F_Op(inst, true, &APFloat::subtract); }
  bool operator()(FMUL_D inst) { return F_Op(inst, true, &APFloat::multiply); }
  bool operator()(FDIV_D inst) { return F_Op(inst, true, &APFloat::divide); }
  bool operator()(FSQRT_D inst) {
    // TODO: APFloat doesn't have a sqrt function.
    return false;
  }
  bool operator()(FSGNJ_D inst) { return F_SignInj(inst, true, false); }
  bool operator()(FSGNJN_D inst) { return F_SignInj(inst, true, true); }
  bool operator()(FSGNJX_D inst) { return F_SignInjXor(inst, true); }
  bool operator()(FMIN_D inst) { return F_MAX_MIN(inst, true, minnum); }
  bool operator()(FMAX_D inst) { return F_MAX_MIN(inst, true, maxnum); }
  bool operator()(FCVT_S_D inst) {
    return transformOptional(inst.rs1.ReadAPFloat(m_emu, true),
                             [&](auto &&rs1) {
                               double d = rs1.convertToDouble();
                               APFloat apf((float(d)));
                               return inst.rd.WriteAPFloat(m_emu, apf);
                             })
        .value_or(false);
  }
  bool operator()(FCVT_D_S inst) {
    return transformOptional(inst.rs1.ReadAPFloat(m_emu, false),
                             [&](auto &&rs1) {
                               float f = rs1.convertToFloat();
                               APFloat apf((double(f)));
                               return inst.rd.WriteAPFloat(m_emu, apf);
                             })
        .value_or(false);
  }
  bool operator()(FEQ_D inst) { return F_Compare(inst, true, FEQ); }
  bool operator()(FLT_D inst) { return F_Compare(inst, true, FLT); }
  bool operator()(FLE_D inst) { return F_Compare(inst, true, FLE); }
  bool operator()(FCLASS_D inst) { return FCLASS(inst, true); }
  bool operator()(FCVT_W_D inst) {
    return FCVT_i2f<FCVT_W_D, int32_t, double>(inst, true,
                                               &APFloat::convertToDouble);
  }
  bool operator()(FCVT_WU_D inst) {
    return FCVT_i2f<FCVT_WU_D, uint32_t, double>(inst, true,
                                                 &APFloat::convertToDouble);
  }
  bool operator()(FCVT_D_W inst) {
    return FCVT_f2i(inst, &Rs::ReadI32, APFloat::IEEEdouble());
  }
  bool operator()(FCVT_D_WU inst) {
    return FCVT_f2i(inst, &Rs::ReadU32, APFloat::IEEEdouble());
  }
  bool operator()(FCVT_L_D inst) {
    return FCVT_i2f<FCVT_L_D, int64_t, double>(inst, true,
                                               &APFloat::convertToDouble);
  }
  bool operator()(FCVT_LU_D inst) {
    return FCVT_i2f<FCVT_LU_D, uint64_t, double>(inst, true,
                                                 &APFloat::convertToDouble);
  }
  bool operator()(FMV_X_D inst) { return FMV_f2i(inst, true); }
  bool operator()(FCVT_D_L inst) {
    return FCVT_f2i(inst, &Rs::ReadI64, APFloat::IEEEdouble());
  }
  bool operator()(FCVT_D_LU inst) {
    return FCVT_f2i(inst, &Rs::Read, APFloat::IEEEdouble());
  }
  bool operator()(FMV_D_X inst) {
    return FMV_i2f(inst, 64, &APInt::bitsToDouble);
  }
  bool operator()(INVALID inst) { return false; }
  bool operator()(RESERVED inst) { return false; }
  bool operator()(EBREAK inst) { return false; }
  bool operator()(HINT inst) { return true; }
  bool operator()(NOP inst) { return true; }
};

bool EmulateInstructionRISCV::Execute(DecodeResult inst, bool ignore_cond) {
  return std::visit(Executor(*this, ignore_cond, inst.is_rvc), inst.decoded);
}

bool EmulateInstructionRISCV::EvaluateInstruction(uint32_t options) {
  bool increase_pc = options & eEmulateInstructionOptionAutoAdvancePC;
  bool ignore_cond = options & eEmulateInstructionOptionIgnoreConditions;

  if (!increase_pc)
    return Execute(m_decoded, ignore_cond);

  auto old_pc = ReadPC();
  if (!old_pc)
    return false;

  bool success = Execute(m_decoded, ignore_cond);
  if (!success)
    return false;

  auto new_pc = ReadPC();
  if (!new_pc)
    return false;

  // If the pc is not updated during execution, we do it here.
  return new_pc != old_pc ||
         WritePC(*old_pc + Executor::size(m_decoded.is_rvc));
}

std::optional<DecodeResult>
EmulateInstructionRISCV::ReadInstructionAt(addr_t addr) {
  return transformOptional(ReadMem<uint32_t>(addr),
                           [&](uint32_t inst) { return Decode(inst); })
      .value_or(std::nullopt);
}

bool EmulateInstructionRISCV::ReadInstruction() {
  auto addr = ReadPC();
  m_addr = addr.value_or(LLDB_INVALID_ADDRESS);
  if (!addr)
    return false;
  auto inst = ReadInstructionAt(*addr);
  if (!inst)
    return false;
  m_decoded = *inst;
  if (inst->is_rvc)
    m_opcode.SetOpcode16(inst->inst, GetByteOrder());
  else
    m_opcode.SetOpcode32(inst->inst, GetByteOrder());
  return true;
}

std::optional<addr_t> EmulateInstructionRISCV::ReadPC() {
  bool success = false;
  auto addr = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC,
                                   LLDB_INVALID_ADDRESS, &success);
  return success ? std::optional<addr_t>(addr) : std::nullopt;
}

bool EmulateInstructionRISCV::WritePC(addr_t pc) {
  EmulateInstruction::Context ctx;
  ctx.type = eContextAdvancePC;
  ctx.SetNoArgs();
  return WriteRegisterUnsigned(ctx, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_PC, pc);
}

RoundingMode EmulateInstructionRISCV::GetRoundingMode() {
  bool success = false;
  auto fcsr = ReadRegisterUnsigned(eRegisterKindLLDB, fpr_fcsr_riscv,
                                   LLDB_INVALID_ADDRESS, &success);
  if (!success)
    return RoundingMode::Invalid;
  auto frm = (fcsr >> 5) & 0x7;
  switch (frm) {
  case 0b000:
    return RoundingMode::NearestTiesToEven;
  case 0b001:
    return RoundingMode::TowardZero;
  case 0b010:
    return RoundingMode::TowardNegative;
  case 0b011:
    return RoundingMode::TowardPositive;
  case 0b111:
    return RoundingMode::Dynamic;
  default:
    // Reserved for future use.
    return RoundingMode::Invalid;
  }
}

bool EmulateInstructionRISCV::SetAccruedExceptions(
    APFloatBase::opStatus opStatus) {
  bool success = false;
  auto fcsr = ReadRegisterUnsigned(eRegisterKindLLDB, fpr_fcsr_riscv,
                                   LLDB_INVALID_ADDRESS, &success);
  if (!success)
    return false;
  switch (opStatus) {
  case APFloatBase::opInvalidOp:
    fcsr |= 1 << 4;
    break;
  case APFloatBase::opDivByZero:
    fcsr |= 1 << 3;
    break;
  case APFloatBase::opOverflow:
    fcsr |= 1 << 2;
    break;
  case APFloatBase::opUnderflow:
    fcsr |= 1 << 1;
    break;
  case APFloatBase::opInexact:
    fcsr |= 1 << 0;
    break;
  case APFloatBase::opOK:
    break;
  }
  EmulateInstruction::Context ctx;
  ctx.type = eContextRegisterStore;
  ctx.SetNoArgs();
  return WriteRegisterUnsigned(ctx, eRegisterKindLLDB, fpr_fcsr_riscv, fcsr);
}

std::optional<RegisterInfo>
EmulateInstructionRISCV::GetRegisterInfo(RegisterKind reg_kind,
                                         uint32_t reg_index) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_index) {
    case LLDB_REGNUM_GENERIC_PC:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_pc_riscv;
      break;
    case LLDB_REGNUM_GENERIC_SP:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_sp_riscv;
      break;
    case LLDB_REGNUM_GENERIC_FP:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_fp_riscv;
      break;
    case LLDB_REGNUM_GENERIC_RA:
      reg_kind = eRegisterKindLLDB;
      reg_index = gpr_ra_riscv;
      break;
    // We may handle LLDB_REGNUM_GENERIC_ARGx when more instructions are
    // supported.
    default:
      llvm_unreachable("unsupported register");
    }
  }

  const RegisterInfo *array =
      RegisterInfoPOSIX_riscv64::GetRegisterInfoPtr(m_arch);
  const uint32_t length =
      RegisterInfoPOSIX_riscv64::GetRegisterInfoCount(m_arch);

  if (reg_index >= length || reg_kind != eRegisterKindLLDB)
    return {};

  return array[reg_index];
}

bool EmulateInstructionRISCV::SetTargetTriple(const ArchSpec &arch) {
  return SupportsThisArch(arch);
}

bool EmulateInstructionRISCV::TestEmulation(Stream &out_stream, ArchSpec &arch,
                                            OptionValueDictionary *test_data) {
  return false;
}

void EmulateInstructionRISCV::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionRISCV::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::EmulateInstruction *
EmulateInstructionRISCV::CreateInstance(const ArchSpec &arch,
                                        InstructionType inst_type) {
  if (EmulateInstructionRISCV::SupportsThisInstructionType(inst_type) &&
      SupportsThisArch(arch)) {
    return new EmulateInstructionRISCV(arch);
  }

  return nullptr;
}

bool EmulateInstructionRISCV::SupportsThisArch(const ArchSpec &arch) {
  return arch.GetTriple().isRISCV();
}

} // namespace lldb_private
