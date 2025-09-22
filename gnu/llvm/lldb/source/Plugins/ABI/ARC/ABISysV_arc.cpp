//===-- ABISysV_arc.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_arc.h"

// C Includes
// C++ Includes
#include <array>
#include <limits>
#include <type_traits>

// Other libraries and framework includes
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Core/ValueObjectRegister.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#define DEFINE_REG_NAME(reg_num)      ConstString(#reg_num).GetCString()
#define DEFINE_REG_NAME_STR(reg_name) ConstString(reg_name).GetCString()

// The ABI is not a source of such information as size, offset, encoding, etc.
// of a register. Just provides correct dwarf and eh_frame numbers.

#define DEFINE_GENERIC_REGISTER_STUB(dwarf_num, str_name, generic_num)        \
  {                                                                           \
    DEFINE_REG_NAME(dwarf_num), DEFINE_REG_NAME_STR(str_name),                \
    0, 0, eEncodingInvalid, eFormatDefault,                                   \
    { dwarf_num, dwarf_num, generic_num, LLDB_INVALID_REGNUM, dwarf_num },    \
    nullptr, nullptr, nullptr,                                                \
  }

#define DEFINE_REGISTER_STUB(dwarf_num, str_name) \
  DEFINE_GENERIC_REGISTER_STUB(dwarf_num, str_name, LLDB_INVALID_REGNUM)

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_arc, ABIARC)

namespace {
namespace dwarf {
enum regnums {
  r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16,
  r17, r18, r19, r20, r21, r22, r23, r24, r25, r26,
  r27, fp = r27, r28, sp = r28, r29, r30, r31, blink = r31,
  r32, r33, r34, r35, r36, r37, r38, r39, r40, r41, r42, r43, r44, r45, r46,
  r47, r48, r49, r50, r51, r52, r53, r54, r55, r56, r57, r58, r59, r60,
  /*reserved,*/ /*limm indicator,*/ r63 = 63, pc = 70, status32 = 74
};

static const std::array<RegisterInfo, 64> g_register_infos = { {
    DEFINE_GENERIC_REGISTER_STUB(r0, nullptr, LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GENERIC_REGISTER_STUB(r1, nullptr, LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GENERIC_REGISTER_STUB(r2, nullptr, LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GENERIC_REGISTER_STUB(r3, nullptr, LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GENERIC_REGISTER_STUB(r4, nullptr, LLDB_REGNUM_GENERIC_ARG5),
    DEFINE_GENERIC_REGISTER_STUB(r5, nullptr, LLDB_REGNUM_GENERIC_ARG6),
    DEFINE_GENERIC_REGISTER_STUB(r6, nullptr, LLDB_REGNUM_GENERIC_ARG7),
    DEFINE_GENERIC_REGISTER_STUB(r7, nullptr, LLDB_REGNUM_GENERIC_ARG8),
    DEFINE_REGISTER_STUB(r8, nullptr),
    DEFINE_REGISTER_STUB(r9, nullptr),
    DEFINE_REGISTER_STUB(r10, nullptr),
    DEFINE_REGISTER_STUB(r11, nullptr),
    DEFINE_REGISTER_STUB(r12, nullptr),
    DEFINE_REGISTER_STUB(r13, nullptr),
    DEFINE_REGISTER_STUB(r14, nullptr),
    DEFINE_REGISTER_STUB(r15, nullptr),
    DEFINE_REGISTER_STUB(r16, nullptr),
    DEFINE_REGISTER_STUB(r17, nullptr),
    DEFINE_REGISTER_STUB(r18, nullptr),
    DEFINE_REGISTER_STUB(r19, nullptr),
    DEFINE_REGISTER_STUB(r20, nullptr),
    DEFINE_REGISTER_STUB(r21, nullptr),
    DEFINE_REGISTER_STUB(r22, nullptr),
    DEFINE_REGISTER_STUB(r23, nullptr),
    DEFINE_REGISTER_STUB(r24, nullptr),
    DEFINE_REGISTER_STUB(r25, nullptr),
    DEFINE_REGISTER_STUB(r26, "gp"),
    DEFINE_GENERIC_REGISTER_STUB(r27, "fp", LLDB_REGNUM_GENERIC_FP),
    DEFINE_GENERIC_REGISTER_STUB(r28, "sp", LLDB_REGNUM_GENERIC_SP),
    DEFINE_REGISTER_STUB(r29, "ilink"),
    DEFINE_REGISTER_STUB(r30, nullptr),
    DEFINE_GENERIC_REGISTER_STUB(r31, "blink", LLDB_REGNUM_GENERIC_RA),
    DEFINE_REGISTER_STUB(r32, nullptr),
    DEFINE_REGISTER_STUB(r33, nullptr),
    DEFINE_REGISTER_STUB(r34, nullptr),
    DEFINE_REGISTER_STUB(r35, nullptr),
    DEFINE_REGISTER_STUB(r36, nullptr),
    DEFINE_REGISTER_STUB(r37, nullptr),
    DEFINE_REGISTER_STUB(r38, nullptr),
    DEFINE_REGISTER_STUB(r39, nullptr),
    DEFINE_REGISTER_STUB(r40, nullptr),
    DEFINE_REGISTER_STUB(r41, nullptr),
    DEFINE_REGISTER_STUB(r42, nullptr),
    DEFINE_REGISTER_STUB(r43, nullptr),
    DEFINE_REGISTER_STUB(r44, nullptr),
    DEFINE_REGISTER_STUB(r45, nullptr),
    DEFINE_REGISTER_STUB(r46, nullptr),
    DEFINE_REGISTER_STUB(r47, nullptr),
    DEFINE_REGISTER_STUB(r48, nullptr),
    DEFINE_REGISTER_STUB(r49, nullptr),
    DEFINE_REGISTER_STUB(r50, nullptr),
    DEFINE_REGISTER_STUB(r51, nullptr),
    DEFINE_REGISTER_STUB(r52, nullptr),
    DEFINE_REGISTER_STUB(r53, nullptr),
    DEFINE_REGISTER_STUB(r54, nullptr),
    DEFINE_REGISTER_STUB(r55, nullptr),
    DEFINE_REGISTER_STUB(r56, nullptr),
    DEFINE_REGISTER_STUB(r57, nullptr),
    DEFINE_REGISTER_STUB(r58, "accl"),
    DEFINE_REGISTER_STUB(r59, "acch"),
    DEFINE_REGISTER_STUB(r60, "lp_count"),
    DEFINE_REGISTER_STUB(r63, "pcl"),
    DEFINE_GENERIC_REGISTER_STUB(pc, nullptr, LLDB_REGNUM_GENERIC_PC),
    DEFINE_GENERIC_REGISTER_STUB(status32, nullptr, LLDB_REGNUM_GENERIC_FLAGS)} };
} // namespace dwarf
} // namespace

const RegisterInfo *ABISysV_arc::GetRegisterInfoArray(uint32_t &count) {
  count = dwarf::g_register_infos.size();
  return dwarf::g_register_infos.data();
}

size_t ABISysV_arc::GetRedZoneSize() const { return 0; }

bool ABISysV_arc::IsRegisterFileReduced(RegisterContext &reg_ctx) const {
  if (!m_is_reg_file_reduced) {
    const auto *const rf_build_reg = reg_ctx.GetRegisterInfoByName("rf_build");

    const auto reg_value = reg_ctx.ReadRegisterAsUnsigned(rf_build_reg,
                                                          /*fail_value*/ 0);
    // RF_BUILD "Number of Entries" bit.
    const uint32_t rf_entries_bit = 1U << 9U;
    m_is_reg_file_reduced = (reg_value & rf_entries_bit) != 0;
  }

  return m_is_reg_file_reduced.value_or(false);
}

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP ABISysV_arc::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  return llvm::Triple::arc == arch.GetTriple().getArch() ?
      ABISP(new ABISysV_arc(std::move(process_sp), MakeMCRegisterInfo(arch))) :
      ABISP();
}

static const size_t word_size = 4U;
static const size_t reg_size = word_size;

static inline size_t AugmentArgSize(size_t size_in_bytes) {
  return llvm::alignTo(size_in_bytes, word_size);
}

static size_t
TotalArgsSizeInWords(const llvm::ArrayRef<ABI::CallArgument> &args) {
  size_t total_size = 0;
  for (const auto &arg : args)
    total_size +=
        (ABI::CallArgument::TargetValue == arg.type ? AugmentArgSize(arg.size)
                                                    : reg_size) /
        word_size;

  return total_size;
}

bool ABISysV_arc::PrepareTrivialCall(Thread &thread, addr_t sp,
                                     addr_t func_addr, addr_t return_addr,
                                     llvm::ArrayRef<addr_t> args) const {
  // We don't use the traditional trivial call specialized for jit.
  return false;
}

bool ABISysV_arc::PrepareTrivialCall(Thread &thread, addr_t sp, addr_t pc,
    addr_t ra, llvm::Type &prototype,
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

  // Push host data onto target.
  for (const auto &arg : args) {
    // Skip over target values.
    if (arg.type == ABI::CallArgument::TargetValue)
      continue;

    // Create space on the stack for this data 4-byte aligned.
    sp -= AugmentArgSize(arg.size);

    if (process->WriteMemory(sp, arg.data_up.get(), arg.size, error) < arg.size
        || error.Fail())
      return false;

    // Update the argument with the target pointer.
    *const_cast<addr_t *>(&arg.value) = sp;
  }

  // Make sure number of parameters matches prototype.
  assert(!prototype.isFunctionVarArg());
  assert(prototype.getFunctionNumParams() == args.size());

  const size_t regs_for_args_count = IsRegisterFileReduced(*reg_ctx) ? 4U : 8U;

  // Number of arguments passed on stack.
  auto args_size = TotalArgsSizeInWords(args);
  auto on_stack =
      args_size <= regs_for_args_count ? 0 : args_size - regs_for_args_count;
  auto offset = on_stack * word_size;

  uint8_t reg_value[reg_size];
  size_t reg_index = LLDB_REGNUM_GENERIC_ARG1;

  for (const auto &arg : args) {
    auto value = reinterpret_cast<const uint8_t *>(&arg.value);
    auto size =
        ABI::CallArgument::TargetValue == arg.type ? arg.size : reg_size;

    // Pass arguments via registers.
    while (size > 0 && reg_index < regs_for_args_count) {
      size_t byte_index = 0;
      auto end = size < reg_size ? size : reg_size;

      while (byte_index < end) {
        reg_value[byte_index++] = *(value++);
        --size;
      }

      while (byte_index < reg_size) {
        reg_value[byte_index++] = 0;
      }

      RegisterValue reg_val_obj(llvm::ArrayRef(reg_value, reg_size),
                                eByteOrderLittle);
      if (!reg_ctx->WriteRegister(
            reg_ctx->GetRegisterInfo(eRegisterKindGeneric, reg_index),
            reg_val_obj))
        return false;

      // NOTE: It's unsafe to iterate through LLDB_REGNUM_GENERICs.
      ++reg_index;
    }

    if (reg_index < regs_for_args_count || size == 0)
      continue;

    // Remaining arguments are passed on the stack.
    if (process->WriteMemory(sp - offset, value, size, error) < size ||
        !error.Success())
      return false;

    offset -= AugmentArgSize(size);
  }

  // Set stack pointer immediately below arguments.
  sp -= on_stack * word_size;

  // Update registers with current function call state.
  reg_ctx->WriteRegisterFromUnsigned(pc_reg, pc);
  reg_ctx->WriteRegisterFromUnsigned(ra_reg, ra);
  reg_ctx->WriteRegisterFromUnsigned(sp_reg, sp);

  return true;
}

bool ABISysV_arc::GetArgumentValues(Thread &thread, ValueList &values) const {
  return false;
}

Status ABISysV_arc::SetReturnValueObject(StackFrameSP &frame_sp,
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

    raw_value >>= 32;
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

static uint64_t ReadRawValue(const RegisterContextSP &reg_ctx,
                             uint8_t size_in_bytes) {
  auto reg_info_r0 =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1);

  // Extract the register context so we can read arguments from registers.
  uint64_t raw_value =
      reg_ctx->ReadRegisterAsUnsigned(reg_info_r0, 0) & UINT32_MAX;

  if (sizeof(uint64_t) == size_in_bytes)
    raw_value |= (reg_ctx->ReadRegisterAsUnsigned(
                      reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                               LLDB_REGNUM_GENERIC_ARG2), 0) &
                  UINT64_MAX) << 32U;

  return raw_value;
}

ValueObjectSP
ABISysV_arc::GetReturnValueObjectSimple(Thread &thread,
                                        CompilerType &compiler_type) const {
  if (!compiler_type)
    return ValueObjectSP();

  auto reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return ValueObjectSP();

  Value value;
  value.SetCompilerType(compiler_type);

  const uint32_t type_flags = compiler_type.GetTypeInfo();
  // Integer return type.
  if (type_flags & eTypeIsInteger) {
    const size_t byte_size = compiler_type.GetByteSize(&thread).value_or(0);
    auto raw_value = ReadRawValue(reg_ctx, byte_size);

    const bool is_signed = (type_flags & eTypeIsSigned) != 0;
    if (!SetSizedInteger(value.GetScalar(), raw_value, byte_size, is_signed))
      return ValueObjectSP();

    value.SetValueType(Value::ValueType::Scalar);
  }
  // Pointer return type.
  else if (type_flags & eTypeIsPointer) {
    auto reg_info_r0 = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_ARG1);
    value.GetScalar() = reg_ctx->ReadRegisterAsUnsigned(reg_info_r0, 0);

    value.SetValueType(Value::ValueType::Scalar);
  }
  // Floating point return type.
  else if (type_flags & eTypeIsFloat) {
    uint32_t float_count = 0;
    bool is_complex = false;

    if (compiler_type.IsFloatingPointType(float_count, is_complex) &&
        1 == float_count && !is_complex) {
      const size_t byte_size = compiler_type.GetByteSize(&thread).value_or(0);
      auto raw_value = ReadRawValue(reg_ctx, byte_size);

      if (!SetSizedFloat(value.GetScalar(), raw_value, byte_size))
        return ValueObjectSP();
    }
  }
  // Unsupported return type.
  else
    return ValueObjectSP();

  return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                        value, ConstString(""));
}

ValueObjectSP ABISysV_arc::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;

  if (!return_compiler_type)
    return return_valobj_sp;

  ExecutionContext exe_ctx(thread.shared_from_this());
  return GetReturnValueObjectSimple(thread, return_compiler_type);
}

ValueObjectSP ABISysV_arc::GetReturnValueObjectImpl(Thread &thread,
                                                    llvm::Type &retType) const {
  auto reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return ValueObjectSP();

  Value value;
  // Void return type.
  if (retType.isVoidTy()) {
    value.GetScalar() = 0;
  }
  // Integer return type.
  else if (retType.isIntegerTy()) {
    size_t byte_size = retType.getPrimitiveSizeInBits();
    if (1 != byte_size) // For boolean type.
      byte_size /= CHAR_BIT;

    auto raw_value = ReadRawValue(reg_ctx, byte_size);

    const bool is_signed = false; // IR Type doesn't provide this info.
    if (!SetSizedInteger(value.GetScalar(), raw_value, byte_size, is_signed))
      return ValueObjectSP();
  }
  // Pointer return type.
  else if (retType.isPointerTy()) {
    auto reg_info_r0 = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_ARG1);
    value.GetScalar() = reg_ctx->ReadRegisterAsUnsigned(reg_info_r0, 0);
    value.SetValueType(Value::ValueType::Scalar);
  }
  // Floating point return type.
  else if (retType.isFloatingPointTy()) {
    const size_t byte_size = retType.getPrimitiveSizeInBits() / CHAR_BIT;
    auto raw_value = ReadRawValue(reg_ctx, byte_size);

    if (!SetSizedFloat(value.GetScalar(), raw_value, byte_size))
      return ValueObjectSP();
  }
  // Unsupported return type.
  else
    return ValueObjectSP();

  return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                        value, ConstString(""));
}

bool ABISysV_arc::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our Call Frame Address is the stack pointer value.
  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf::sp, 0);

  // The previous PC is in the BLINK.
  row->SetRegisterLocationToRegister(dwarf::pc, dwarf::blink, true);
  unwind_plan.AppendRow(row);

  // All other registers are the same.
  unwind_plan.SetSourceName("arc at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);

  return true;
}

bool ABISysV_arc::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  return false;
}

bool ABISysV_arc::RegisterIsVolatile(const RegisterInfo *reg_info) {
  if (nullptr == reg_info)
    return false;

  // Volatile registers are: r0..r12.
  uint32_t regnum = reg_info->kinds[eRegisterKindDWARF];
  if (regnum <= 12)
    return true;

  static const std::string ra_reg_name = "blink";
  return ra_reg_name == reg_info->name;
}

void ABISysV_arc::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "System V ABI for ARC targets", CreateInstance);
}

void ABISysV_arc::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
