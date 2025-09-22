//===-- ABISysV_arm64.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_arm64.h"

#include <optional>
#include <vector>

#include "llvm/ADT/STLExtras.h"
#include "llvm/TargetParser/Triple.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"

#include "Utility/ARM64_DWARF_Registers.h"

using namespace lldb;
using namespace lldb_private;

bool ABISysV_arm64::GetPointerReturnRegister(const char *&name) {
  name = "x0";
  return true;
}

size_t ABISysV_arm64::GetRedZoneSize() const { return 128; }

// Static Functions

ABISP
ABISysV_arm64::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  const llvm::Triple::ArchType arch_type = arch.GetTriple().getArch();
  const llvm::Triple::VendorType vendor_type = arch.GetTriple().getVendor();

  if (vendor_type != llvm::Triple::Apple) {
    if (arch_type == llvm::Triple::aarch64 ||
        arch_type == llvm::Triple::aarch64_32) {
      return ABISP(
          new ABISysV_arm64(std::move(process_sp), MakeMCRegisterInfo(arch)));
    }
  }

  return ABISP();
}

bool ABISysV_arm64::PrepareTrivialCall(Thread &thread, addr_t sp,
                                       addr_t func_addr, addr_t return_addr,
                                       llvm::ArrayRef<addr_t> args) const {
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  Log *log = GetLog(LLDBLog::Expressions);

  if (log) {
    StreamString s;
    s.Printf("ABISysV_arm64::PrepareTrivialCall (tid = 0x%" PRIx64
             ", sp = 0x%" PRIx64 ", func_addr = 0x%" PRIx64
             ", return_addr = 0x%" PRIx64,
             thread.GetID(), (uint64_t)sp, (uint64_t)func_addr,
             (uint64_t)return_addr);

    for (size_t i = 0; i < args.size(); ++i)
      s.Printf(", arg%d = 0x%" PRIx64, static_cast<int>(i + 1), args[i]);
    s.PutCString(")");
    log->PutString(s.GetString());
  }

  // x0 - x7 contain first 8 simple args
  if (args.size() > 8)
    return false;

  for (size_t i = 0; i < args.size(); ++i) {
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfo(
        eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1 + i);
    LLDB_LOGF(log, "About to write arg%d (0x%" PRIx64 ") into %s",
              static_cast<int>(i + 1), args[i], reg_info->name);
    if (!reg_ctx->WriteRegisterFromUnsigned(reg_info, args[i]))
      return false;
  }

  // Set "lr" to the return address
  if (!reg_ctx->WriteRegisterFromUnsigned(
          reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                   LLDB_REGNUM_GENERIC_RA),
          return_addr))
    return false;

  // Set "sp" to the requested value
  if (!reg_ctx->WriteRegisterFromUnsigned(
          reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                   LLDB_REGNUM_GENERIC_SP),
          sp))
    return false;

  // Set "pc" to the address requested
  if (!reg_ctx->WriteRegisterFromUnsigned(
          reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                   LLDB_REGNUM_GENERIC_PC),
          func_addr))
    return false;

  return true;
}

// TODO: We dont support fp/SIMD arguments in v0-v7
bool ABISysV_arm64::GetArgumentValues(Thread &thread, ValueList &values) const {
  uint32_t num_values = values.GetSize();

  ExecutionContext exe_ctx(thread.shared_from_this());

  // Extract the register context so we can read arguments from registers

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  addr_t sp = 0;

  for (uint32_t value_idx = 0; value_idx < num_values; ++value_idx) {
    // We currently only support extracting values with Clang QualTypes. Do we
    // care about others?
    Value *value = values.GetValueAtIndex(value_idx);

    if (!value)
      return false;

    CompilerType value_type = value->GetCompilerType();
    if (value_type) {
      bool is_signed = false;
      size_t bit_width = 0;
      std::optional<uint64_t> bit_size = value_type.GetBitSize(&thread);
      if (!bit_size)
        return false;
      if (value_type.IsIntegerOrEnumerationType(is_signed)) {
        bit_width = *bit_size;
      } else if (value_type.IsPointerOrReferenceType()) {
        bit_width = *bit_size;
      } else {
        // We only handle integer, pointer and reference types currently...
        return false;
      }

      if (bit_width <= (exe_ctx.GetProcessRef().GetAddressByteSize() * 8)) {
        if (value_idx < 8) {
          // Arguments 1-8 are in x0-x7...
          const RegisterInfo *reg_info = nullptr;
          reg_info = reg_ctx->GetRegisterInfo(
              eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1 + value_idx);

          if (reg_info) {
            RegisterValue reg_value;

            if (reg_ctx->ReadRegister(reg_info, reg_value)) {
              if (is_signed)
                reg_value.SignExtend(bit_width);
              if (!reg_value.GetScalarValue(value->GetScalar()))
                return false;
              continue;
            }
          }
          return false;
        } else {
          // TODO: Verify for stack layout for SysV
          if (sp == 0) {
            // Read the stack pointer if we already haven't read it
            sp = reg_ctx->GetSP(0);
            if (sp == 0)
              return false;
          }

          // Arguments 5 on up are on the stack
          const uint32_t arg_byte_size = (bit_width + (8 - 1)) / 8;
          Status error;
          if (!exe_ctx.GetProcessRef().ReadScalarIntegerFromMemory(
                  sp, arg_byte_size, is_signed, value->GetScalar(), error))
            return false;

          sp += arg_byte_size;
          // Align up to the next 8 byte boundary if needed
          if (sp % 8) {
            sp >>= 3;
            sp += 1;
            sp <<= 3;
          }
        }
      }
    }
  }
  return true;
}

Status ABISysV_arm64::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                           lldb::ValueObjectSP &new_value_sp) {
  Status error;
  if (!new_value_sp) {
    error.SetErrorString("Empty value object for return value.");
    return error;
  }

  CompilerType return_value_type = new_value_sp->GetCompilerType();
  if (!return_value_type) {
    error.SetErrorString("Null clang type for return value.");
    return error;
  }

  Thread *thread = frame_sp->GetThread().get();

  RegisterContext *reg_ctx = thread->GetRegisterContext().get();

  if (reg_ctx) {
    DataExtractor data;
    Status data_error;
    const uint64_t byte_size = new_value_sp->GetData(data, data_error);
    if (data_error.Fail()) {
      error.SetErrorStringWithFormat(
          "Couldn't convert return value to raw data: %s",
          data_error.AsCString());
      return error;
    }

    const uint32_t type_flags = return_value_type.GetTypeInfo(nullptr);
    if (type_flags & eTypeIsScalar || type_flags & eTypeIsPointer) {
      if (type_flags & eTypeIsInteger || type_flags & eTypeIsPointer) {
        // Extract the register context so we can read arguments from registers
        lldb::offset_t offset = 0;
        if (byte_size <= 16) {
          const RegisterInfo *x0_info = reg_ctx->GetRegisterInfo(
              eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1);
          if (byte_size <= 8) {
            uint64_t raw_value = data.GetMaxU64(&offset, byte_size);

            if (!reg_ctx->WriteRegisterFromUnsigned(x0_info, raw_value))
              error.SetErrorString("failed to write register x0");
          } else {
            uint64_t raw_value = data.GetMaxU64(&offset, 8);

            if (reg_ctx->WriteRegisterFromUnsigned(x0_info, raw_value)) {
              const RegisterInfo *x1_info = reg_ctx->GetRegisterInfo(
                  eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG2);
              raw_value = data.GetMaxU64(&offset, byte_size - offset);

              if (!reg_ctx->WriteRegisterFromUnsigned(x1_info, raw_value))
                error.SetErrorString("failed to write register x1");
            }
          }
        } else {
          error.SetErrorString("We don't support returning longer than 128 bit "
                               "integer values at present.");
        }
      } else if (type_flags & eTypeIsFloat) {
        if (type_flags & eTypeIsComplex) {
          // Don't handle complex yet.
          error.SetErrorString(
              "returning complex float values are not supported");
        } else {
          const RegisterInfo *v0_info = reg_ctx->GetRegisterInfoByName("v0", 0);

          if (v0_info) {
            if (byte_size <= 16) {
              RegisterValue reg_value;
              error = reg_value.SetValueFromData(*v0_info, data, 0, true);
              if (error.Success())
                if (!reg_ctx->WriteRegister(v0_info, reg_value))
                  error.SetErrorString("failed to write register v0");
            } else {
              error.SetErrorString("returning float values longer than 128 "
                                   "bits are not supported");
            }
          } else
            error.SetErrorString("v0 register is not available on this target");
        }
      }
    } else if (type_flags & eTypeIsVector) {
      if (byte_size > 0) {
        const RegisterInfo *v0_info = reg_ctx->GetRegisterInfoByName("v0", 0);

        if (v0_info) {
          if (byte_size <= v0_info->byte_size) {
            RegisterValue reg_value;
            error = reg_value.SetValueFromData(*v0_info, data, 0, true);
            if (error.Success()) {
              if (!reg_ctx->WriteRegister(v0_info, reg_value))
                error.SetErrorString("failed to write register v0");
            }
          }
        }
      }
    }
  } else {
    error.SetErrorString("no registers are available");
  }

  return error;
}

bool ABISysV_arm64::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t lr_reg_num = arm64_dwarf::lr;
  uint32_t sp_reg_num = arm64_dwarf::sp;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our previous Call Frame Address is the stack pointer
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 0);

  unwind_plan.AppendRow(row);
  unwind_plan.SetReturnAddressRegister(lr_reg_num);

  // All other registers are the same.

  unwind_plan.SetSourceName("arm64 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetUnwindPlanForSignalTrap(eLazyBoolNo);

  return true;
}

bool ABISysV_arm64::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t fp_reg_num = arm64_dwarf::fp;
  uint32_t pc_reg_num = arm64_dwarf::pc;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  const int32_t ptr_size = 8;

  row->GetCFAValue().SetIsRegisterPlusOffset(fp_reg_num, 2 * ptr_size);
  row->SetOffset(0);
  row->SetUnspecifiedRegistersAreUndefined(true);

  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, ptr_size * -2, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * -1, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("arm64 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetUnwindPlanForSignalTrap(eLazyBoolNo);

  return true;
}

// AAPCS64 (Procedure Call Standard for the ARM 64-bit Architecture) says
// registers x19 through x28 and sp are callee preserved. v8-v15 are non-
// volatile (and specifically only the lower 8 bytes of these regs), the rest
// of the fp/SIMD registers are volatile.

// We treat x29 as callee preserved also, else the unwinder won't try to
// retrieve fp saves.

bool ABISysV_arm64::RegisterIsVolatile(const RegisterInfo *reg_info) {
  if (reg_info) {
    const char *name = reg_info->name;

    // Sometimes we'll be called with the "alternate" name for these registers;
    // recognize them as non-volatile.

    if (name[0] == 'p' && name[1] == 'c') // pc
      return false;
    if (name[0] == 'f' && name[1] == 'p') // fp
      return false;
    if (name[0] == 's' && name[1] == 'p') // sp
      return false;
    if (name[0] == 'l' && name[1] == 'r') // lr
      return false;

    if (name[0] == 'x' || name[0] == 'r') {
      // Volatile registers: x0-x18
      // Although documentation says only x19-28 + sp are callee saved We ll
      // also have to treat x30 as non-volatile. Each dwarf frame has its own
      // value of lr. Return false for the non-volatile gpr regs, true for
      // everything else
      switch (name[1]) {
      case '1':
        switch (name[2]) {
        case '9':
          return false; // x19 is non-volatile
        default:
          return true;
        }
        break;
      case '2':
        switch (name[2]) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
          return false; // x20 - 28 are non-volatile
        case '9':
          return false; // x29 aka fp treat as non-volatile
        default:
          return true;
        }
      case '3': // x30 (lr) and x31 (sp) treat as non-volatile
        if (name[2] == '0' || name[2] == '1')
          return false;
        break;
      default:
        return true; // all volatile cases not handled above fall here.
      }
    } else if (name[0] == 'v' || name[0] == 's' || name[0] == 'd') {
      // Volatile registers: v0-7, v16-v31
      // Return false for non-volatile fp/SIMD regs, true for everything else
      switch (name[1]) {
      case '8':
      case '9':
        return false; // v8-v9 are non-volatile
      case '1':
        switch (name[2]) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
          return false; // v10-v15 are non-volatile
        default:
          return true;
        }
      default:
        return true;
      }
    }
  }
  return true;
}

static bool LoadValueFromConsecutiveGPRRegisters(
    ExecutionContext &exe_ctx, RegisterContext *reg_ctx,
    const CompilerType &value_type,
    bool is_return_value, // false => parameter, true => return value
    uint32_t &NGRN,       // NGRN (see ABI documentation)
    uint32_t &NSRN,       // NSRN (see ABI documentation)
    DataExtractor &data) {
  std::optional<uint64_t> byte_size =
      value_type.GetByteSize(exe_ctx.GetBestExecutionContextScope());

  if (byte_size || *byte_size == 0)
    return false;

  std::unique_ptr<DataBufferHeap> heap_data_up(
      new DataBufferHeap(*byte_size, 0));
  const ByteOrder byte_order = exe_ctx.GetProcessRef().GetByteOrder();
  Status error;

  CompilerType base_type;
  const uint32_t homogeneous_count =
      value_type.IsHomogeneousAggregate(&base_type);
  if (homogeneous_count > 0 && homogeneous_count <= 8) {
    // Make sure we have enough registers
    if (NSRN < 8 && (8 - NSRN) >= homogeneous_count) {
      if (!base_type)
        return false;
      std::optional<uint64_t> base_byte_size =
          base_type.GetByteSize(exe_ctx.GetBestExecutionContextScope());
      if (!base_byte_size)
        return false;
      uint32_t data_offset = 0;

      for (uint32_t i = 0; i < homogeneous_count; ++i) {
        char v_name[8];
        ::snprintf(v_name, sizeof(v_name), "v%u", NSRN);
        const RegisterInfo *reg_info =
            reg_ctx->GetRegisterInfoByName(v_name, 0);
        if (reg_info == nullptr)
          return false;

        if (*base_byte_size > reg_info->byte_size)
          return false;

        RegisterValue reg_value;

        if (!reg_ctx->ReadRegister(reg_info, reg_value))
          return false;

        // Make sure we have enough room in "heap_data_up"
        if ((data_offset + *base_byte_size) <= heap_data_up->GetByteSize()) {
          const size_t bytes_copied = reg_value.GetAsMemoryData(
              *reg_info, heap_data_up->GetBytes() + data_offset,
              *base_byte_size, byte_order, error);
          if (bytes_copied != *base_byte_size)
            return false;
          data_offset += bytes_copied;
          ++NSRN;
        } else
          return false;
      }
      data.SetByteOrder(byte_order);
      data.SetAddressByteSize(exe_ctx.GetProcessRef().GetAddressByteSize());
      data.SetData(DataBufferSP(heap_data_up.release()));
      return true;
    }
  }

  const size_t max_reg_byte_size = 16;
  if (*byte_size <= max_reg_byte_size) {
    size_t bytes_left = *byte_size;
    uint32_t data_offset = 0;
    while (data_offset < *byte_size) {
      if (NGRN >= 8)
        return false;

      const RegisterInfo *reg_info = reg_ctx->GetRegisterInfo(
          eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1 + NGRN);
      if (reg_info == nullptr)
        return false;

      RegisterValue reg_value;

      if (!reg_ctx->ReadRegister(reg_info, reg_value))
        return false;

      const size_t curr_byte_size = std::min<size_t>(8, bytes_left);
      const size_t bytes_copied = reg_value.GetAsMemoryData(
          *reg_info, heap_data_up->GetBytes() + data_offset, curr_byte_size,
          byte_order, error);
      if (bytes_copied == 0)
        return false;
      if (bytes_copied >= bytes_left)
        break;
      data_offset += bytes_copied;
      bytes_left -= bytes_copied;
      ++NGRN;
    }
  } else {
    const RegisterInfo *reg_info = nullptr;
    if (is_return_value) {
      // The SysV arm64 ABI doesn't require you to write the return location 
      // back to x8 before returning from the function the way the x86_64 ABI 
      // does.  It looks like all the users of this ABI currently choose not to
      // do that, and so we can't reconstruct stack based returns on exit 
      // from the function.
      return false;
    } else {
      // We are assuming we are stopped at the first instruction in a function
      // and that the ABI is being respected so all parameters appear where
      // they should be (functions with no external linkage can legally violate
      // the ABI).
      if (NGRN >= 8)
        return false;

      reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                          LLDB_REGNUM_GENERIC_ARG1 + NGRN);
      if (reg_info == nullptr)
        return false;
      ++NGRN;
    }

    const lldb::addr_t value_addr =
        reg_ctx->ReadRegisterAsUnsigned(reg_info, LLDB_INVALID_ADDRESS);

    if (value_addr == LLDB_INVALID_ADDRESS)
      return false;

    if (exe_ctx.GetProcessRef().ReadMemory(
            value_addr, heap_data_up->GetBytes(), heap_data_up->GetByteSize(),
            error) != heap_data_up->GetByteSize()) {
      return false;
    }
  }

  data.SetByteOrder(byte_order);
  data.SetAddressByteSize(exe_ctx.GetProcessRef().GetAddressByteSize());
  data.SetData(DataBufferSP(heap_data_up.release()));
  return true;
}

ValueObjectSP ABISysV_arm64::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  Value value;

  ExecutionContext exe_ctx(thread.shared_from_this());
  if (exe_ctx.GetTargetPtr() == nullptr || exe_ctx.GetProcessPtr() == nullptr)
    return return_valobj_sp;

  // value.SetContext (Value::eContextTypeClangType, return_compiler_type);
  value.SetCompilerType(return_compiler_type);

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  std::optional<uint64_t> byte_size = return_compiler_type.GetByteSize(&thread);
  if (!byte_size)
    return return_valobj_sp;

  const uint32_t type_flags = return_compiler_type.GetTypeInfo(nullptr);
  if (type_flags & eTypeIsScalar || type_flags & eTypeIsPointer) {
    value.SetValueType(Value::ValueType::Scalar);

    bool success = false;
    if (type_flags & eTypeIsInteger || type_flags & eTypeIsPointer) {
      // Extract the register context so we can read arguments from registers
      if (*byte_size <= 8) {
        const RegisterInfo *x0_reg_info = nullptr;
        x0_reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                               LLDB_REGNUM_GENERIC_ARG1);
        if (x0_reg_info) {
          uint64_t raw_value =
              thread.GetRegisterContext()->ReadRegisterAsUnsigned(x0_reg_info,
                                                                  0);
          const bool is_signed = (type_flags & eTypeIsSigned) != 0;
          switch (*byte_size) {
          default:
            break;
          case 16: // uint128_t
            // In register x0 and x1
            {
              const RegisterInfo *x1_reg_info = nullptr;
              x1_reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_ARG2);

              if (x1_reg_info) {
                if (*byte_size <=
                    x0_reg_info->byte_size + x1_reg_info->byte_size) {
                  std::unique_ptr<DataBufferHeap> heap_data_up(
                      new DataBufferHeap(*byte_size, 0));
                  const ByteOrder byte_order =
                      exe_ctx.GetProcessRef().GetByteOrder();
                  RegisterValue x0_reg_value;
                  RegisterValue x1_reg_value;
                  if (reg_ctx->ReadRegister(x0_reg_info, x0_reg_value) &&
                      reg_ctx->ReadRegister(x1_reg_info, x1_reg_value)) {
                    Status error;
                    if (x0_reg_value.GetAsMemoryData(
                            *x0_reg_info, heap_data_up->GetBytes() + 0, 8,
                            byte_order, error) &&
                        x1_reg_value.GetAsMemoryData(
                            *x1_reg_info, heap_data_up->GetBytes() + 8, 8,
                            byte_order, error)) {
                      DataExtractor data(
                          DataBufferSP(heap_data_up.release()), byte_order,
                          exe_ctx.GetProcessRef().GetAddressByteSize());

                      return_valobj_sp = ValueObjectConstResult::Create(
                          &thread, return_compiler_type, ConstString(""), data);
                      return return_valobj_sp;
                    }
                  }
                }
              }
            }
            break;
          case sizeof(uint64_t):
            if (is_signed)
              value.GetScalar() = (int64_t)(raw_value);
            else
              value.GetScalar() = (uint64_t)(raw_value);
            success = true;
            break;

          case sizeof(uint32_t):
            if (is_signed)
              value.GetScalar() = (int32_t)(raw_value & UINT32_MAX);
            else
              value.GetScalar() = (uint32_t)(raw_value & UINT32_MAX);
            success = true;
            break;

          case sizeof(uint16_t):
            if (is_signed)
              value.GetScalar() = (int16_t)(raw_value & UINT16_MAX);
            else
              value.GetScalar() = (uint16_t)(raw_value & UINT16_MAX);
            success = true;
            break;

          case sizeof(uint8_t):
            if (is_signed)
              value.GetScalar() = (int8_t)(raw_value & UINT8_MAX);
            else
              value.GetScalar() = (uint8_t)(raw_value & UINT8_MAX);
            success = true;
            break;
          }
        }
      }
    } else if (type_flags & eTypeIsFloat) {
      if (type_flags & eTypeIsComplex) {
        // Don't handle complex yet.
      } else {
        if (*byte_size <= sizeof(long double)) {
          const RegisterInfo *v0_reg_info =
              reg_ctx->GetRegisterInfoByName("v0", 0);
          RegisterValue v0_value;
          if (reg_ctx->ReadRegister(v0_reg_info, v0_value)) {
            DataExtractor data;
            if (v0_value.GetData(data)) {
              lldb::offset_t offset = 0;
              if (*byte_size == sizeof(float)) {
                value.GetScalar() = data.GetFloat(&offset);
                success = true;
              } else if (*byte_size == sizeof(double)) {
                value.GetScalar() = data.GetDouble(&offset);
                success = true;
              } else if (*byte_size == sizeof(long double)) {
                value.GetScalar() = data.GetLongDouble(&offset);
                success = true;
              }
            }
          }
        }
      }
    }

    if (success)
      return_valobj_sp = ValueObjectConstResult::Create(
          thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if (type_flags & eTypeIsVector && *byte_size <= 16) {
    if (*byte_size > 0) {
      const RegisterInfo *v0_info = reg_ctx->GetRegisterInfoByName("v0", 0);

      if (v0_info) {
        std::unique_ptr<DataBufferHeap> heap_data_up(
            new DataBufferHeap(*byte_size, 0));
        const ByteOrder byte_order = exe_ctx.GetProcessRef().GetByteOrder();
        RegisterValue reg_value;
        if (reg_ctx->ReadRegister(v0_info, reg_value)) {
          Status error;
          if (reg_value.GetAsMemoryData(*v0_info, heap_data_up->GetBytes(),
                                        heap_data_up->GetByteSize(), byte_order,
                                        error)) {
            DataExtractor data(DataBufferSP(heap_data_up.release()), byte_order,
                               exe_ctx.GetProcessRef().GetAddressByteSize());
            return_valobj_sp = ValueObjectConstResult::Create(
                &thread, return_compiler_type, ConstString(""), data);
          }
        }
      }
    }
  } else if (type_flags & eTypeIsStructUnion || type_flags & eTypeIsClass ||
             (type_flags & eTypeIsVector && *byte_size > 16)) {
    DataExtractor data;

    uint32_t NGRN = 0; // Search ABI docs for NGRN
    uint32_t NSRN = 0; // Search ABI docs for NSRN
    const bool is_return_value = true;
    if (LoadValueFromConsecutiveGPRRegisters(
            exe_ctx, reg_ctx, return_compiler_type, is_return_value, NGRN, NSRN,
            data)) {
      return_valobj_sp = ValueObjectConstResult::Create(
          &thread, return_compiler_type, ConstString(""), data);
    }
  }
  return return_valobj_sp;
}

lldb::addr_t ABISysV_arm64::FixAddress(addr_t pc, addr_t mask) {
  if (mask == LLDB_INVALID_ADDRESS_MASK)
    return pc;
  lldb::addr_t pac_sign_extension = 0x0080000000000000ULL;
  return (pc & pac_sign_extension) ? pc | mask : pc & (~mask);
}

// Reads code or data address mask for the current Linux process.
static lldb::addr_t ReadLinuxProcessAddressMask(lldb::ProcessSP process_sp,
                                                llvm::StringRef reg_name) {
  // LLDB_INVALID_ADDRESS_MASK means there isn't a mask or it has not been read
  // yet. We do not return the top byte mask unless thread_sp is valid. This
  // prevents calls to this function before the thread is setup locking in the
  // value to just the top byte mask, in cases where pointer authentication
  // might also be active.
  uint64_t address_mask = LLDB_INVALID_ADDRESS_MASK;
  lldb::ThreadSP thread_sp = process_sp->GetThreadList().GetSelectedThread();
  if (thread_sp) {
    // Linux configures user-space virtual addresses with top byte ignored.
    // We set default value of mask such that top byte is masked out.
    address_mask = ~((1ULL << 56) - 1);
    // If Pointer Authentication feature is enabled then Linux exposes
    // PAC data and code mask register. Try reading relevant register
    // below and merge it with default address mask calculated above.
    lldb::RegisterContextSP reg_ctx_sp = thread_sp->GetRegisterContext();
    if (reg_ctx_sp) {
      const RegisterInfo *reg_info =
          reg_ctx_sp->GetRegisterInfoByName(reg_name, 0);
      if (reg_info) {
        lldb::addr_t mask_reg_val = reg_ctx_sp->ReadRegisterAsUnsigned(
            reg_info->kinds[eRegisterKindLLDB], LLDB_INVALID_ADDRESS);
        if (mask_reg_val != LLDB_INVALID_ADDRESS)
          address_mask |= mask_reg_val;
      }
    }
  }
  return address_mask;
}

// Reads code or data address mask for the current OpenBSD process.
static lldb::addr_t ReadOpenBSDProcessAddressMask(lldb::ProcessSP process_sp,
                                                 llvm::StringRef reg_name) {
  // We set default value of mask such that no bits are masked out.
  uint64_t address_mask = 0ULL;
  // If Pointer Authentication feature is enabled then OpenBSD exposes
  // PAC data and code mask register. Try reading relevant register
  // below and merge it with default address mask calculated above.
  lldb::ThreadSP thread_sp = process_sp->GetThreadList().GetSelectedThread();
  if (thread_sp) {
    lldb::RegisterContextSP reg_ctx_sp = thread_sp->GetRegisterContext();
    if (reg_ctx_sp) {
      const RegisterInfo *reg_info =
          reg_ctx_sp->GetRegisterInfoByName(reg_name, 0);
      if (reg_info) {
        lldb::addr_t mask_reg_val = reg_ctx_sp->ReadRegisterAsUnsigned(
            reg_info->kinds[eRegisterKindLLDB], LLDB_INVALID_ADDRESS);
        if (mask_reg_val != LLDB_INVALID_ADDRESS)
          address_mask |= mask_reg_val;
      }
    }
  }
  return address_mask;
}

lldb::addr_t ABISysV_arm64::FixCodeAddress(lldb::addr_t pc) {
  if (lldb::ProcessSP process_sp = GetProcessSP()) {
    if (process_sp->GetTarget().GetArchitecture().GetTriple().isOSLinux() &&
        process_sp->GetCodeAddressMask() == LLDB_INVALID_ADDRESS_MASK)
      process_sp->SetCodeAddressMask(
          ReadLinuxProcessAddressMask(process_sp, "code_mask"));

    if (process_sp->GetTarget().GetArchitecture().GetTriple().isOSOpenBSD() &&
        !process_sp->GetCodeAddressMask())
      process_sp->SetCodeAddressMask(
          ReadOpenBSDProcessAddressMask(process_sp, "code_mask"));

    // b55 is the highest bit outside TBI (if it's enabled), use
    // it to determine if the high bits are set to 0 or 1.
    const addr_t pac_sign_extension = 0x0080000000000000ULL;
    addr_t mask = process_sp->GetCodeAddressMask();
    // Test if the high memory mask has been overriden separately
    if (pc & pac_sign_extension &&
        process_sp->GetHighmemCodeAddressMask() != LLDB_INVALID_ADDRESS_MASK)
      mask = process_sp->GetHighmemCodeAddressMask();

    return FixAddress(pc, mask);
  }
  return pc;
}

lldb::addr_t ABISysV_arm64::FixDataAddress(lldb::addr_t pc) {
  if (lldb::ProcessSP process_sp = GetProcessSP()) {
    if (process_sp->GetTarget().GetArchitecture().GetTriple().isOSLinux() &&
        process_sp->GetDataAddressMask() == LLDB_INVALID_ADDRESS_MASK)
      process_sp->SetDataAddressMask(
          ReadLinuxProcessAddressMask(process_sp, "data_mask"));

    if (process_sp->GetTarget().GetArchitecture().GetTriple().isOSOpenBSD() &&
        !process_sp->GetDataAddressMask())
      process_sp->SetDataAddressMask(
          ReadOpenBSDProcessAddressMask(process_sp, "data_mask"));

    // b55 is the highest bit outside TBI (if it's enabled), use
    // it to determine if the high bits are set to 0 or 1.
    const addr_t pac_sign_extension = 0x0080000000000000ULL;
    addr_t mask = process_sp->GetDataAddressMask();
    // Test if the high memory mask has been overriden separately
    if (pc & pac_sign_extension &&
        process_sp->GetHighmemDataAddressMask() != LLDB_INVALID_ADDRESS_MASK)
      mask = process_sp->GetHighmemDataAddressMask();

    return FixAddress(pc, mask);
  }
  return pc;
}

void ABISysV_arm64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "SysV ABI for AArch64 targets", CreateInstance);
}

void ABISysV_arm64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
