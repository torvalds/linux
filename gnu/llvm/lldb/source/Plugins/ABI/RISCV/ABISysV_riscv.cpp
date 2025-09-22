//===-- ABISysV_riscv.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "ABISysV_riscv.h"

#include <array>
#include <limits>

#include "llvm/IR/DerivedTypes.h"

#include "Utility/RISCV_DWARF_Registers.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"

#define DEFINE_REG_NAME(reg_num) ConstString(#reg_num).GetCString()
#define DEFINE_REG_NAME_STR(reg_name) ConstString(reg_name).GetCString()

// The ABI is not a source of such information as size, offset, encoding, etc.
// of a register. Just provides correct dwarf and eh_frame numbers.

#define DEFINE_GENERIC_REGISTER_STUB(dwarf_num, str_name, generic_num)         \
  {                                                                            \
    DEFINE_REG_NAME(dwarf_num), DEFINE_REG_NAME_STR(str_name), 0, 0,           \
        eEncodingInvalid, eFormatDefault,                                      \
        {dwarf_num, dwarf_num, generic_num, LLDB_INVALID_REGNUM, dwarf_num},   \
        nullptr, nullptr, nullptr,                                             \
  }

#define DEFINE_REGISTER_STUB(dwarf_num, str_name)                              \
  DEFINE_GENERIC_REGISTER_STUB(dwarf_num, str_name, LLDB_INVALID_REGNUM)

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_riscv, ABIRISCV)

namespace {
namespace dwarf {
enum regnums {
  zero,
  ra,
  sp,
  gp,
  tp,
  t0,
  t1,
  t2,
  fp,
  s0 = fp,
  s1,
  a0,
  a1,
  a2,
  a3,
  a4,
  a5,
  a6,
  a7,
  s2,
  s3,
  s4,
  s5,
  s6,
  s7,
  s8,
  s9,
  s10,
  s11,
  t3,
  t4,
  t5,
  t6,
  pc
};

static const std::array<RegisterInfo, 33> g_register_infos = {
    {DEFINE_REGISTER_STUB(zero, nullptr),
     DEFINE_GENERIC_REGISTER_STUB(ra, nullptr, LLDB_REGNUM_GENERIC_RA),
     DEFINE_GENERIC_REGISTER_STUB(sp, nullptr, LLDB_REGNUM_GENERIC_SP),
     DEFINE_REGISTER_STUB(gp, nullptr),
     DEFINE_REGISTER_STUB(tp, nullptr),
     DEFINE_REGISTER_STUB(t0, nullptr),
     DEFINE_REGISTER_STUB(t1, nullptr),
     DEFINE_REGISTER_STUB(t2, nullptr),
     DEFINE_GENERIC_REGISTER_STUB(fp, nullptr, LLDB_REGNUM_GENERIC_FP),
     DEFINE_REGISTER_STUB(s1, nullptr),
     DEFINE_GENERIC_REGISTER_STUB(a0, nullptr, LLDB_REGNUM_GENERIC_ARG1),
     DEFINE_GENERIC_REGISTER_STUB(a1, nullptr, LLDB_REGNUM_GENERIC_ARG2),
     DEFINE_GENERIC_REGISTER_STUB(a2, nullptr, LLDB_REGNUM_GENERIC_ARG3),
     DEFINE_GENERIC_REGISTER_STUB(a3, nullptr, LLDB_REGNUM_GENERIC_ARG4),
     DEFINE_GENERIC_REGISTER_STUB(a4, nullptr, LLDB_REGNUM_GENERIC_ARG5),
     DEFINE_GENERIC_REGISTER_STUB(a5, nullptr, LLDB_REGNUM_GENERIC_ARG6),
     DEFINE_GENERIC_REGISTER_STUB(a6, nullptr, LLDB_REGNUM_GENERIC_ARG7),
     DEFINE_GENERIC_REGISTER_STUB(a7, nullptr, LLDB_REGNUM_GENERIC_ARG8),
     DEFINE_REGISTER_STUB(s2, nullptr),
     DEFINE_REGISTER_STUB(s3, nullptr),
     DEFINE_REGISTER_STUB(s4, nullptr),
     DEFINE_REGISTER_STUB(s5, nullptr),
     DEFINE_REGISTER_STUB(s6, nullptr),
     DEFINE_REGISTER_STUB(s7, nullptr),
     DEFINE_REGISTER_STUB(s8, nullptr),
     DEFINE_REGISTER_STUB(s9, nullptr),
     DEFINE_REGISTER_STUB(s10, nullptr),
     DEFINE_REGISTER_STUB(s11, nullptr),
     DEFINE_REGISTER_STUB(t3, nullptr),
     DEFINE_REGISTER_STUB(t4, nullptr),
     DEFINE_REGISTER_STUB(t5, nullptr),
     DEFINE_REGISTER_STUB(t6, nullptr),
     DEFINE_GENERIC_REGISTER_STUB(pc, nullptr, LLDB_REGNUM_GENERIC_PC)}};
} // namespace dwarf
} // namespace

const RegisterInfo *ABISysV_riscv::GetRegisterInfoArray(uint32_t &count) {
  count = dwarf::g_register_infos.size();
  return dwarf::g_register_infos.data();
}

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_riscv::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  llvm::Triple::ArchType machine = arch.GetTriple().getArch();

  if (llvm::Triple::riscv32 != machine && llvm::Triple::riscv64 != machine)
    return ABISP();
  
  ABISysV_riscv *abi = new ABISysV_riscv(std::move(process_sp),
                                         MakeMCRegisterInfo(arch));
  if (abi)
    abi->SetIsRV64((llvm::Triple::riscv64 == machine) ? true : false);
  return ABISP(abi);
}

static inline size_t AugmentArgSize(bool is_rv64, size_t size_in_bytes) {
  size_t word_size = is_rv64 ? 8 : 4;
  return llvm::alignTo(size_in_bytes, word_size);
}

static size_t
TotalArgsSizeInWords(bool is_rv64,
                     const llvm::ArrayRef<ABI::CallArgument> &args) {
  size_t reg_size = is_rv64 ? 8 : 4;
  size_t word_size = reg_size;
  size_t total_size = 0;
  for (const auto &arg : args)
    total_size +=
        (ABI::CallArgument::TargetValue == arg.type ? AugmentArgSize(is_rv64,
                                                                     arg.size)
                                                    : reg_size) /
        word_size;

  return total_size;
}

bool ABISysV_riscv::PrepareTrivialCall(Thread &thread, addr_t sp,
                                       addr_t func_addr, addr_t return_addr,
                                       llvm::ArrayRef<addr_t> args) const {
  // TODO: Implement
  return false;
}

bool ABISysV_riscv::PrepareTrivialCall(
    Thread &thread, addr_t sp, addr_t pc, addr_t ra, llvm::Type &prototype,
    llvm::ArrayRef<ABI::CallArgument> args) const {
  auto reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return false;

  uint32_t pc_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  if (pc_reg == LLDB_INVALID_REGNUM)
    return false;

  uint32_t ra_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA);
  if (ra_reg == LLDB_INVALID_REGNUM)
    return false;

  uint32_t sp_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  if (sp_reg == LLDB_INVALID_REGNUM)
    return false;

  Status error;
  ProcessSP process = thread.GetProcess();
  if (!process)
    return false;

  size_t reg_size = m_is_rv64 ? 8 : 4;
  size_t word_size = reg_size;
  // Push host data onto target.
  for (const auto &arg : args) {
    // Skip over target values.
    if (arg.type == ABI::CallArgument::TargetValue)
      continue;

    // Create space on the host stack for this data 4-byte aligned.
    sp -= AugmentArgSize(m_is_rv64, arg.size);

    if (process->WriteMemory(sp, arg.data_up.get(), arg.size, error) <
            arg.size ||
        error.Fail())
      return false;

    // Update the argument with the target pointer.
    *const_cast<addr_t *>(&arg.value) = sp;
  }

  // Make sure number of parameters matches prototype.
  assert(prototype.getFunctionNumParams() == args.size());

  const size_t num_args = args.size();
  const size_t regs_for_args_count = 8U;
  const size_t num_args_in_regs =
      num_args > regs_for_args_count ?  regs_for_args_count : num_args;

  // Number of arguments passed on stack.
  size_t args_size = TotalArgsSizeInWords(m_is_rv64, args);
  auto on_stack =
      args_size <= regs_for_args_count ? 0 : args_size - regs_for_args_count;
  auto offset = on_stack * word_size;

  uint8_t reg_value[8];
  size_t reg_index = LLDB_REGNUM_GENERIC_ARG1;

  for (size_t i = 0; i < args_size; ++i) {
    auto value = reinterpret_cast<const uint8_t *>(&args[i].value);
    auto size =
        ABI::CallArgument::TargetValue == args[i].type ? args[i].size : reg_size;

    // Pass arguments via registers.
    if (i < num_args_in_regs) {
      // copy value to register, padding if arg is smaller than register
      auto end = size < reg_size ? size : reg_size;
      memcpy(reg_value, value, end);
      if (reg_size > end)
        memset(reg_value + end, 0, reg_size - end);

      RegisterValue reg_val_obj(llvm::ArrayRef(reg_value, reg_size),
                                eByteOrderLittle);
      if (!reg_ctx->WriteRegister(
              reg_ctx->GetRegisterInfo(eRegisterKindGeneric, reg_index),
              reg_val_obj))
        return false;

      // NOTE: It's unsafe to iterate through LLDB_REGNUM_GENERICs
      // But the "a" registers are sequential in the RISC-V register space
      ++reg_index;
    }

    if (reg_index < regs_for_args_count || size == 0)
      continue;

    // Remaining arguments are passed on the stack.
    if (process->WriteMemory(sp - offset, value, size, error) < size ||
        !error.Success())
      return false;

    offset -= AugmentArgSize(m_is_rv64, size);
  }

  // Set stack pointer immediately below arguments.
  sp -= on_stack * word_size;

  // Update registers with current function call state.
  reg_ctx->WriteRegisterFromUnsigned(pc_reg, pc);
  reg_ctx->WriteRegisterFromUnsigned(ra_reg, ra);
  reg_ctx->WriteRegisterFromUnsigned(sp_reg, sp);

  return true;
}

bool ABISysV_riscv::GetArgumentValues(Thread &thread, ValueList &values) const {
  // TODO: Implement
  return false;
}

Status ABISysV_riscv::SetReturnValueObject(StackFrameSP &frame_sp,
                                           ValueObjectSP &new_value_sp) {
  Status result;
  if (!new_value_sp) {
    result.SetErrorString("Empty value object for return value.");
    return result;
  }

  CompilerType compiler_type = new_value_sp->GetCompilerType();
  if (!compiler_type) {
    result.SetErrorString("Null clang type for return value.");
    return result;
  }

  auto &reg_ctx = *frame_sp->GetThread()->GetRegisterContext();

  bool is_signed = false;
  if (!compiler_type.IsIntegerOrEnumerationType(is_signed) &&
      !compiler_type.IsPointerType()) {
    result.SetErrorString("We don't support returning other types at present");
    return result;
  }

  DataExtractor data;
  size_t num_bytes = new_value_sp->GetData(data, result);

  if (result.Fail()) {
    result.SetErrorStringWithFormat(
        "Couldn't convert return value to raw data: %s", result.AsCString());
    return result;
  }

  size_t reg_size = m_is_rv64 ? 8 : 4;
  if (num_bytes <= 2 * reg_size) {
    offset_t offset = 0;
    uint64_t raw_value = data.GetMaxU64(&offset, num_bytes);

    auto reg_info =
        reg_ctx.GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1);
    if (!reg_ctx.WriteRegisterFromUnsigned(reg_info, raw_value)) {
      result.SetErrorStringWithFormat("Couldn't write value to register %s",
                                      reg_info->name);
      return result;
    }

    if (num_bytes <= reg_size)
      return result; // Successfully written.

    // for riscv32, get the upper 32 bits from raw_value and write them
    // for riscv64, get the next 64 bits from data and write them
    if (4 == reg_size)
      raw_value >>= 32;
    else
      raw_value = data.GetMaxU64(&offset, num_bytes - reg_size);
    reg_info =
        reg_ctx.GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG2);
    if (!reg_ctx.WriteRegisterFromUnsigned(reg_info, raw_value)) {
      result.SetErrorStringWithFormat("Couldn't write value to register %s",
                                      reg_info->name);
    }

    return result;
  }

  result.SetErrorString(
      "We don't support returning large integer values at present.");
  return result;
}

template <typename T>
static void SetInteger(Scalar &scalar, uint64_t raw_value, bool is_signed) {
  raw_value &= std::numeric_limits<T>::max();
  if (is_signed)
    scalar = static_cast<typename std::make_signed<T>::type>(raw_value);
  else
    scalar = static_cast<T>(raw_value);
}

static bool SetSizedInteger(Scalar &scalar, uint64_t raw_value,
                            uint8_t size_in_bytes, bool is_signed) {
  switch (size_in_bytes) {
  default:
    return false;

  case sizeof(uint64_t):
    SetInteger<uint64_t>(scalar, raw_value, is_signed);
    break;

  case sizeof(uint32_t):
    SetInteger<uint32_t>(scalar, raw_value, is_signed);
    break;

  case sizeof(uint16_t):
    SetInteger<uint16_t>(scalar, raw_value, is_signed);
    break;

  case sizeof(uint8_t):
    SetInteger<uint8_t>(scalar, raw_value, is_signed);
    break;
  }

  return true;
}

static bool SetSizedFloat(Scalar &scalar, uint64_t raw_value,
                          uint8_t size_in_bytes) {
  switch (size_in_bytes) {
  default:
    return false;

  case sizeof(uint64_t):
    scalar = *reinterpret_cast<double *>(&raw_value);
    break;

  case sizeof(uint32_t):
    scalar = *reinterpret_cast<float *>(&raw_value);
    break;
  }

  return true;
}

static ValueObjectSP GetValObjFromIntRegs(Thread &thread,
                                          const RegisterContextSP &reg_ctx,
                                          llvm::Triple::ArchType machine,
                                          uint32_t type_flags,
                                          uint32_t byte_size) {
  Value value;
  ValueObjectSP return_valobj_sp;
  auto reg_info_a0 =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1);
  auto reg_info_a1 =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG2);
  uint64_t raw_value;

  switch (byte_size) {
  case sizeof(uint32_t):
    // Read a0 to get the arg
    raw_value = reg_ctx->ReadRegisterAsUnsigned(reg_info_a0, 0) & UINT32_MAX;
    break;
  case sizeof(uint64_t):
    // Read a0 to get the arg on riscv64, a0 and a1 on riscv32
    if (llvm::Triple::riscv32 == machine) {
      raw_value = reg_ctx->ReadRegisterAsUnsigned(reg_info_a0, 0) & UINT32_MAX;
      raw_value |=
          (reg_ctx->ReadRegisterAsUnsigned(reg_info_a1, 0) & UINT32_MAX) << 32U;
    } else {
      raw_value = reg_ctx->ReadRegisterAsUnsigned(reg_info_a0, 0);
    }
    break;
  case 16: {
    // Read a0 and a1 to get the arg on riscv64, not supported on riscv32
    if (llvm::Triple::riscv32 == machine)
      return return_valobj_sp;

    // Create the ValueObjectSP here and return
    std::unique_ptr<DataBufferHeap> heap_data_up(
        new DataBufferHeap(byte_size, 0));
    const ByteOrder byte_order = thread.GetProcess()->GetByteOrder();
    RegisterValue reg_value_a0, reg_value_a1;
    if (reg_ctx->ReadRegister(reg_info_a0, reg_value_a0) &&
        reg_ctx->ReadRegister(reg_info_a1, reg_value_a1)) {
      Status error;
      if (reg_value_a0.GetAsMemoryData(*reg_info_a0,
                                       heap_data_up->GetBytes() + 0, 8,
                                       byte_order, error) &&
          reg_value_a1.GetAsMemoryData(*reg_info_a1,
                                       heap_data_up->GetBytes() + 8, 8,
                                       byte_order, error)) {
        value.SetBytes(heap_data_up.release(), byte_size);
        return ValueObjectConstResult::Create(
            thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
      }
    }
    break;
  }
  default:
    return return_valobj_sp;
  }

  if (type_flags & eTypeIsInteger) {
    const bool is_signed = (type_flags & eTypeIsSigned) != 0;
    if (!SetSizedInteger(value.GetScalar(), raw_value, byte_size, is_signed))
      return return_valobj_sp;
  } else if (type_flags & eTypeIsFloat) {
    if (!SetSizedFloat(value.GetScalar(), raw_value, byte_size))
      return return_valobj_sp;
  } else
    return return_valobj_sp;

  value.SetValueType(Value::ValueType::Scalar);
  return_valobj_sp = ValueObjectConstResult::Create(
      thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  return return_valobj_sp;
}

static ValueObjectSP
GetValObjFromFPRegs(Thread &thread, const RegisterContextSP &reg_ctx,
                    llvm::Triple::ArchType machine, uint32_t arch_fp_flags,
                    uint32_t type_flags, uint32_t byte_size) {
  auto reg_info_fa0 = reg_ctx->GetRegisterInfoByName("fa0");
  bool use_fp_regs = false;
  ValueObjectSP return_valobj_sp;

  switch (arch_fp_flags) {
  // fp return value in integer registers a0 and possibly a1
  case ArchSpec::eRISCV_float_abi_soft:
    return_valobj_sp =
        GetValObjFromIntRegs(thread, reg_ctx, machine, type_flags, byte_size);
    return return_valobj_sp;
  // fp return value in fp register fa0 (only float)
  case ArchSpec::eRISCV_float_abi_single:
    if (byte_size <= 4)
      use_fp_regs = true;
    break;
  // fp return value in fp registers fa0 (float, double)
  case ArchSpec::eRISCV_float_abi_double:
    [[fallthrough]];
  // fp return value in fp registers fa0 (float, double, quad)
  // not implemented; act like they're doubles
  case ArchSpec::eRISCV_float_abi_quad:
    if (byte_size <= 8)
      use_fp_regs = true;
    break;
  }

  if (use_fp_regs) {
    uint64_t raw_value;
    Value value;
    raw_value = reg_ctx->ReadRegisterAsUnsigned(reg_info_fa0, 0);
    if (!SetSizedFloat(value.GetScalar(), raw_value, byte_size))
      return return_valobj_sp;
    value.SetValueType(Value::ValueType::Scalar);
    return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  }
  // we should never reach this, but if we do, use the integer registers
  return GetValObjFromIntRegs(thread, reg_ctx, machine, type_flags, byte_size);
}

ValueObjectSP
ABISysV_riscv::GetReturnValueObjectSimple(Thread &thread,
                                          CompilerType &compiler_type) const {
  ValueObjectSP return_valobj_sp;

  if (!compiler_type)
    return return_valobj_sp;

  auto reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return return_valobj_sp;

  Value value;
  value.SetCompilerType(compiler_type);

  const uint32_t type_flags = compiler_type.GetTypeInfo();
  const size_t byte_size = compiler_type.GetByteSize(&thread).value_or(0);
  const ArchSpec arch = thread.GetProcess()->GetTarget().GetArchitecture();
  const llvm::Triple::ArchType machine = arch.GetMachine();

  // Integer return type.
  if (type_flags & eTypeIsInteger) {
    return_valobj_sp =
        GetValObjFromIntRegs(thread, reg_ctx, machine, type_flags, byte_size);
    return return_valobj_sp;
  }
  // Pointer return type.
  else if (type_flags & eTypeIsPointer) {
    auto reg_info_a0 = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_ARG1);
    value.GetScalar() = reg_ctx->ReadRegisterAsUnsigned(reg_info_a0, 0);
    value.SetValueType(Value::ValueType::Scalar);
    return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  }
  // Floating point return type.
  else if (type_flags & eTypeIsFloat) {
    uint32_t float_count = 0;
    bool is_complex = false;

    if (compiler_type.IsFloatingPointType(float_count, is_complex) &&
        float_count == 1 && !is_complex) {
      const uint32_t arch_fp_flags =
          arch.GetFlags() & ArchSpec::eRISCV_float_abi_mask;
      return_valobj_sp = GetValObjFromFPRegs(
          thread, reg_ctx, machine, arch_fp_flags, type_flags, byte_size);
      return return_valobj_sp;
    }
  }
  // Unsupported return type.
  return return_valobj_sp;
}

ValueObjectSP
ABISysV_riscv::GetReturnValueObjectImpl(lldb_private::Thread &thread,
                                        llvm::Type &type) const {
  Value value;
  ValueObjectSP return_valobj_sp;

  auto reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return return_valobj_sp;

  uint32_t type_flags = 0;
  if (type.isIntegerTy())
    type_flags = eTypeIsInteger;
  else if (type.isVoidTy())
    type_flags = eTypeIsPointer;
  else if (type.isFloatTy())
    type_flags = eTypeIsFloat;

  const uint32_t byte_size = type.getPrimitiveSizeInBits() / CHAR_BIT;
  const ArchSpec arch = thread.GetProcess()->GetTarget().GetArchitecture();
  const llvm::Triple::ArchType machine = arch.GetMachine();

  // Integer return type.
  if (type_flags & eTypeIsInteger) {
    return_valobj_sp =
        GetValObjFromIntRegs(thread, reg_ctx, machine, type_flags, byte_size);
    return return_valobj_sp;
  }
  // Pointer return type.
  else if (type_flags & eTypeIsPointer) {
    auto reg_info_a0 = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_ARG1);
    value.GetScalar() = reg_ctx->ReadRegisterAsUnsigned(reg_info_a0, 0);
    value.SetValueType(Value::ValueType::Scalar);
    return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  }
  // Floating point return type.
  else if (type_flags & eTypeIsFloat) {
    const uint32_t arch_fp_flags =
        arch.GetFlags() & ArchSpec::eRISCV_float_abi_mask;
    return_valobj_sp = GetValObjFromFPRegs(
        thread, reg_ctx, machine, arch_fp_flags, type_flags, byte_size);
    return return_valobj_sp;
  }
  // Unsupported return type.
  return return_valobj_sp;
}

ValueObjectSP ABISysV_riscv::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;

  if (!return_compiler_type)
    return return_valobj_sp;

  ExecutionContext exe_ctx(thread.shared_from_this());
  return GetReturnValueObjectSimple(thread, return_compiler_type);
}

bool ABISysV_riscv::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t pc_reg_num = riscv_dwarf::dwarf_gpr_pc;
  uint32_t sp_reg_num = riscv_dwarf::dwarf_gpr_sp;
  uint32_t ra_reg_num = riscv_dwarf::dwarf_gpr_ra;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Define CFA as the stack pointer
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 0);

  // Previous frame's pc is in ra

  row->SetRegisterLocationToRegister(pc_reg_num, ra_reg_num, true);
  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("riscv function-entry unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);

  return true;
}

bool ABISysV_riscv::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindGeneric);

  uint32_t pc_reg_num = LLDB_REGNUM_GENERIC_PC;
  uint32_t fp_reg_num = LLDB_REGNUM_GENERIC_FP;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Define the CFA as the current frame pointer value.
  row->GetCFAValue().SetIsRegisterPlusOffset(fp_reg_num, 0);
  row->SetOffset(0);

  int reg_size = 4;
  if (m_is_rv64)
    reg_size = 8;

  // Assume the ra reg (return pc) and caller's frame pointer 
  // have been spilled to stack already.
  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, reg_size * -2, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, reg_size * -1, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("riscv default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return true;
}

bool ABISysV_riscv::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABISysV_riscv::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info)
    return false;

  const char *name = reg_info->name;
  ArchSpec arch = GetProcessSP()->GetTarget().GetArchitecture();
  uint32_t arch_flags = arch.GetFlags();
  // floating point registers are only callee saved when using
  // F, D or Q hardware floating point ABIs
  bool is_hw_fp = (arch_flags & ArchSpec::eRISCV_float_abi_mask) != 0;

  bool is_callee_saved =
      llvm::StringSwitch<bool>(name)
          // integer ABI names
          .Cases("ra", "sp", "fp", true)
          .Cases("s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9",
                 true)
          .Cases("s10", "s11", true)
          // integer hardware names
          .Cases("x1", "x2", "x8", "x9", "x18", "x19", "x20", "x21", "x22",
                 true)
          .Cases("x23", "x24", "x25", "x26", "x27", true)
          // floating point ABI names
          .Cases("fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
                 is_hw_fp)
          .Cases("fs8", "fs9", "fs10", "fs11", is_hw_fp)
          // floating point hardware names
          .Cases("f8", "f9", "f18", "f19", "f20", "f21", "f22", "f23", is_hw_fp)
          .Cases("f24", "f25", "f26", "f27", is_hw_fp)
          .Default(false);

  return is_callee_saved;
}

void ABISysV_riscv::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for RISCV targets", CreateInstance);
}

void ABISysV_riscv::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

static uint32_t GetGenericNum(llvm::StringRef name) {
  return llvm::StringSwitch<uint32_t>(name)
      .Case("pc", LLDB_REGNUM_GENERIC_PC)
      .Cases("ra", "x1", LLDB_REGNUM_GENERIC_RA)
      .Cases("sp", "x2", LLDB_REGNUM_GENERIC_SP)
      .Cases("fp", "s0", LLDB_REGNUM_GENERIC_FP)
      .Case("a0", LLDB_REGNUM_GENERIC_ARG1)
      .Case("a1", LLDB_REGNUM_GENERIC_ARG2)
      .Case("a2", LLDB_REGNUM_GENERIC_ARG3)
      .Case("a3", LLDB_REGNUM_GENERIC_ARG4)
      .Case("a4", LLDB_REGNUM_GENERIC_ARG5)
      .Case("a5", LLDB_REGNUM_GENERIC_ARG6)
      .Case("a6", LLDB_REGNUM_GENERIC_ARG7)
      .Case("a7", LLDB_REGNUM_GENERIC_ARG8)
      .Default(LLDB_INVALID_REGNUM);
}

void ABISysV_riscv::AugmentRegisterInfo(
    std::vector<lldb_private::DynamicRegisterInfo::Register> &regs) {
  lldb_private::RegInfoBasedABI::AugmentRegisterInfo(regs);

  for (auto it : llvm::enumerate(regs)) {
    // Set alt name for certain registers for convenience
    if (it.value().name == "zero")
      it.value().alt_name.SetCString("x0");
    else if (it.value().name == "ra")
      it.value().alt_name.SetCString("x1");
    else if (it.value().name == "sp")
      it.value().alt_name.SetCString("x2");
    else if (it.value().name == "gp")
      it.value().alt_name.SetCString("x3");
    else if (it.value().name == "fp")
      it.value().alt_name.SetCString("s0");
    else if (it.value().name == "s0")
      it.value().alt_name.SetCString("x8");

    // Set generic regnum so lldb knows what the PC, etc is
    it.value().regnum_generic = GetGenericNum(it.value().name.GetStringRef());
  }
}
