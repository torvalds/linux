//===-- ABISysV_i386.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "ABISysV_i386.h"

#include "llvm/ADT/STLExtras.h"
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
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ABISysV_i386)

//   This source file uses the following document as a reference:
//====================================================================
//             System V Application Binary Interface
//    Intel386 Architecture Processor Supplement, Version 1.0
//                         Edited by
//      H.J. Lu, David L Kreitzer, Milind Girkar, Zia Ansari
//
//                        (Based on
//           System V Application Binary Interface,
//          AMD64 Architecture Processor Supplement,
//                         Edited by
//     H.J. Lu, Michael Matz, Milind Girkar, Jan Hubicka,
//               Andreas Jaeger, Mark Mitchell)
//
//                     February 3, 2015
//====================================================================

// DWARF Register Number Mapping
// See Table 2.14 of the reference document (specified on top of this file)
// Comment: Table 2.14 is followed till 'mm' entries. After that, all entries
// are ignored here.

enum dwarf_regnums {
  dwarf_eax = 0,
  dwarf_ecx,
  dwarf_edx,
  dwarf_ebx,
  dwarf_esp,
  dwarf_ebp,
  dwarf_esi,
  dwarf_edi,
  dwarf_eip,
};

// Static Functions

ABISP
ABISysV_i386::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getVendor() != llvm::Triple::Apple) {
    if (arch.GetTriple().getArch() == llvm::Triple::x86) {
      return ABISP(
          new ABISysV_i386(std::move(process_sp), MakeMCRegisterInfo(arch)));
    }
  }
  return ABISP();
}

bool ABISysV_i386::PrepareTrivialCall(Thread &thread, addr_t sp,
                                      addr_t func_addr, addr_t return_addr,
                                      llvm::ArrayRef<addr_t> args) const {
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  uint32_t pc_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  uint32_t sp_reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);

  // While using register info to write a register value to memory, the
  // register info just needs to have the correct size of a 32 bit register,
  // the actual register it pertains to is not important, just the size needs
  // to be correct. "eax" is used here for this purpose.
  const RegisterInfo *reg_info_32 = reg_ctx->GetRegisterInfoByName("eax");
  if (!reg_info_32)
    return false; // TODO this should actually never happen

  Status error;
  RegisterValue reg_value;

  // Make room for the argument(s) on the stack
  sp -= 4 * args.size();

  // SP Alignment
  sp &= ~(16ull - 1ull); // 16-byte alignment

  // Write arguments onto the stack
  addr_t arg_pos = sp;
  for (addr_t arg : args) {
    reg_value.SetUInt32(arg);
    error = reg_ctx->WriteRegisterValueToMemory(
        reg_info_32, arg_pos, reg_info_32->byte_size, reg_value);
    if (error.Fail())
      return false;
    arg_pos += 4;
  }

  // The return address is pushed onto the stack
  sp -= 4;
  reg_value.SetUInt32(return_addr);
  error = reg_ctx->WriteRegisterValueToMemory(
      reg_info_32, sp, reg_info_32->byte_size, reg_value);
  if (error.Fail())
    return false;

  // Setting %esp to the actual stack value.
  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_num, sp))
    return false;

  // Setting %eip to the address of the called function.
  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_num, func_addr))
    return false;

  return true;
}

static bool ReadIntegerArgument(Scalar &scalar, unsigned int bit_width,
                                bool is_signed, Process *process,
                                addr_t &current_stack_argument) {
  uint32_t byte_size = (bit_width + (8 - 1)) / 8;
  Status error;

  if (!process)
    return false;

  if (process->ReadScalarIntegerFromMemory(current_stack_argument, byte_size,
                                           is_signed, scalar, error)) {
    current_stack_argument += byte_size;
    return true;
  }
  return false;
}

bool ABISysV_i386::GetArgumentValues(Thread &thread, ValueList &values) const {
  unsigned int num_values = values.GetSize();
  unsigned int value_index;

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  // Get pointer to the first stack argument
  addr_t sp = reg_ctx->GetSP(0);
  if (!sp)
    return false;

  addr_t current_stack_argument = sp + 4; // jump over return address

  for (value_index = 0; value_index < num_values; ++value_index) {
    Value *value = values.GetValueAtIndex(value_index);

    if (!value)
      return false;

    // Currently: Support for extracting values with Clang QualTypes only.
    CompilerType compiler_type(value->GetCompilerType());
    std::optional<uint64_t> bit_size = compiler_type.GetBitSize(&thread);
    if (bit_size) {
      bool is_signed;
      if (compiler_type.IsIntegerOrEnumerationType(is_signed)) {
        ReadIntegerArgument(value->GetScalar(), *bit_size, is_signed,
                            thread.GetProcess().get(), current_stack_argument);
      } else if (compiler_type.IsPointerType()) {
        ReadIntegerArgument(value->GetScalar(), *bit_size, false,
                            thread.GetProcess().get(), current_stack_argument);
      }
    }
  }
  return true;
}

Status ABISysV_i386::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                          lldb::ValueObjectSP &new_value_sp) {
  Status error;
  if (!new_value_sp) {
    error.SetErrorString("Empty value object for return value.");
    return error;
  }

  CompilerType compiler_type = new_value_sp->GetCompilerType();
  if (!compiler_type) {
    error.SetErrorString("Null clang type for return value.");
    return error;
  }

  const uint32_t type_flags = compiler_type.GetTypeInfo();
  Thread *thread = frame_sp->GetThread().get();
  RegisterContext *reg_ctx = thread->GetRegisterContext().get();
  DataExtractor data;
  Status data_error;
  size_t num_bytes = new_value_sp->GetData(data, data_error);
  bool register_write_successful = true;

  if (data_error.Fail()) {
    error.SetErrorStringWithFormat(
        "Couldn't convert return value to raw data: %s",
        data_error.AsCString());
    return error;
  }

  // Following "IF ELSE" block categorizes various 'Fundamental Data Types'.
  // The terminology 'Fundamental Data Types' used here is adopted from Table
  // 2.1 of the reference document (specified on top of this file)

  if (type_flags & eTypeIsPointer) // 'Pointer'
  {
    if (num_bytes != sizeof(uint32_t)) {
      error.SetErrorString("Pointer to be returned is not 4 bytes wide");
      return error;
    }
    lldb::offset_t offset = 0;
    const RegisterInfo *eax_info = reg_ctx->GetRegisterInfoByName("eax", 0);
    uint32_t raw_value = data.GetMaxU32(&offset, num_bytes);
    register_write_successful =
        reg_ctx->WriteRegisterFromUnsigned(eax_info, raw_value);
  } else if ((type_flags & eTypeIsScalar) ||
             (type_flags & eTypeIsEnumeration)) //'Integral' + 'Floating Point'
  {
    lldb::offset_t offset = 0;
    const RegisterInfo *eax_info = reg_ctx->GetRegisterInfoByName("eax", 0);

    if (type_flags & eTypeIsInteger) // 'Integral' except enum
    {
      switch (num_bytes) {
      default:
        break;
      case 16:
        // For clang::BuiltinType::UInt128 & Int128 ToDo: Need to decide how to
        // handle it
        break;
      case 8: {
        uint32_t raw_value_low = data.GetMaxU32(&offset, 4);
        const RegisterInfo *edx_info = reg_ctx->GetRegisterInfoByName("edx", 0);
        uint32_t raw_value_high = data.GetMaxU32(&offset, num_bytes - offset);
        register_write_successful =
            (reg_ctx->WriteRegisterFromUnsigned(eax_info, raw_value_low) &&
             reg_ctx->WriteRegisterFromUnsigned(edx_info, raw_value_high));
        break;
      }
      case 4:
      case 2:
      case 1: {
        uint32_t raw_value = data.GetMaxU32(&offset, num_bytes);
        register_write_successful =
            reg_ctx->WriteRegisterFromUnsigned(eax_info, raw_value);
        break;
      }
      }
    } else if (type_flags & eTypeIsEnumeration) // handles enum
    {
      uint32_t raw_value = data.GetMaxU32(&offset, num_bytes);
      register_write_successful =
          reg_ctx->WriteRegisterFromUnsigned(eax_info, raw_value);
    } else if (type_flags & eTypeIsFloat) // 'Floating Point'
    {
      RegisterValue st0_value, fstat_value, ftag_value;
      const RegisterInfo *st0_info = reg_ctx->GetRegisterInfoByName("st0", 0);
      const RegisterInfo *fstat_info =
          reg_ctx->GetRegisterInfoByName("fstat", 0);
      const RegisterInfo *ftag_info = reg_ctx->GetRegisterInfoByName("ftag", 0);

      /* According to Page 3-12 of document
      System V Application Binary Interface, Intel386 Architecture Processor
      Supplement, Fourth Edition
      To return Floating Point values, all st% registers except st0 should be
      empty after exiting from
      a function. This requires setting fstat and ftag registers to specific
      values.
      fstat: The TOP field of fstat should be set to a value [0,7]. ABI doesn't
      specify the specific
      value of TOP in case of function return. Hence, we set the TOP field to 7
      by our choice. */
      uint32_t value_fstat_u32 = 0x00003800;

      /* ftag: Implication of setting TOP to 7 and indicating all st% registers
      empty except st0 is to set
      7th bit of 4th byte of FXSAVE area to 1 and all other bits of this byte to
      0. This is in accordance
      with the document Intel 64 and IA-32 Architectures Software Developer's
      Manual, January 2015 */
      uint32_t value_ftag_u32 = 0x00000080;

      if (num_bytes <= 12) // handles float, double, long double, __float80
      {
        long double value_long_dbl = 0.0;
        if (num_bytes == 4)
          value_long_dbl = data.GetFloat(&offset);
        else if (num_bytes == 8)
          value_long_dbl = data.GetDouble(&offset);
        else if (num_bytes == 12)
          value_long_dbl = data.GetLongDouble(&offset);
        else {
          error.SetErrorString("Invalid number of bytes for this return type");
          return error;
        }
        st0_value.SetLongDouble(value_long_dbl);
        fstat_value.SetUInt32(value_fstat_u32);
        ftag_value.SetUInt32(value_ftag_u32);
        register_write_successful =
            reg_ctx->WriteRegister(st0_info, st0_value) &&
            reg_ctx->WriteRegister(fstat_info, fstat_value) &&
            reg_ctx->WriteRegister(ftag_info, ftag_value);
      } else if (num_bytes == 16) // handles __float128
      {
        error.SetErrorString("Implementation is missing for this clang type.");
      }
    } else {
      // Neither 'Integral' nor 'Floating Point'. If flow reaches here then
      // check type_flags. This type_flags is not a valid type.
      error.SetErrorString("Invalid clang type");
    }
  } else {
    /* 'Complex Floating Point', 'Packed', 'Decimal Floating Point' and
    'Aggregate' data types
    are yet to be implemented */
    error.SetErrorString("Currently only Integral and Floating Point clang "
                         "types are supported.");
  }
  if (!register_write_successful)
    error.SetErrorString("Register writing failed");
  return error;
}

ValueObjectSP ABISysV_i386::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  Value value;

  if (!return_compiler_type)
    return return_valobj_sp;

  value.SetCompilerType(return_compiler_type);

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  const uint32_t type_flags = return_compiler_type.GetTypeInfo();

  unsigned eax_id =
      reg_ctx->GetRegisterInfoByName("eax", 0)->kinds[eRegisterKindLLDB];
  unsigned edx_id =
      reg_ctx->GetRegisterInfoByName("edx", 0)->kinds[eRegisterKindLLDB];

  // Following "IF ELSE" block categorizes various 'Fundamental Data Types'.
  // The terminology 'Fundamental Data Types' used here is adopted from Table
  // 2.1 of the reference document (specified on top of this file)

  if (type_flags & eTypeIsPointer) // 'Pointer'
  {
    uint32_t ptr =
        thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
        0xffffffff;
    value.SetValueType(Value::ValueType::Scalar);
    value.GetScalar() = ptr;
    return_valobj_sp = ValueObjectConstResult::Create(
        thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if ((type_flags & eTypeIsScalar) ||
             (type_flags & eTypeIsEnumeration)) //'Integral' + 'Floating Point'
  {
    value.SetValueType(Value::ValueType::Scalar);
    std::optional<uint64_t> byte_size =
        return_compiler_type.GetByteSize(&thread);
    if (!byte_size)
      return return_valobj_sp;
    bool success = false;

    if (type_flags & eTypeIsInteger) // 'Integral' except enum
    {
      const bool is_signed = ((type_flags & eTypeIsSigned) != 0);
      uint64_t raw_value =
          thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
          0xffffffff;
      raw_value |=
          (thread.GetRegisterContext()->ReadRegisterAsUnsigned(edx_id, 0) &
           0xffffffff)
          << 32;

      switch (*byte_size) {
      default:
        break;

      case 16:
        // For clang::BuiltinType::UInt128 & Int128 ToDo: Need to decide how to
        // handle it
        break;

      case 8:
        if (is_signed)
          value.GetScalar() = (int64_t)(raw_value);
        else
          value.GetScalar() = (uint64_t)(raw_value);
        success = true;
        break;

      case 4:
        if (is_signed)
          value.GetScalar() = (int32_t)(raw_value & UINT32_MAX);
        else
          value.GetScalar() = (uint32_t)(raw_value & UINT32_MAX);
        success = true;
        break;

      case 2:
        if (is_signed)
          value.GetScalar() = (int16_t)(raw_value & UINT16_MAX);
        else
          value.GetScalar() = (uint16_t)(raw_value & UINT16_MAX);
        success = true;
        break;

      case 1:
        if (is_signed)
          value.GetScalar() = (int8_t)(raw_value & UINT8_MAX);
        else
          value.GetScalar() = (uint8_t)(raw_value & UINT8_MAX);
        success = true;
        break;
      }

      if (success)
        return_valobj_sp = ValueObjectConstResult::Create(
            thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
    } else if (type_flags & eTypeIsEnumeration) // handles enum
    {
      uint32_t enm =
          thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
          0xffffffff;
      value.SetValueType(Value::ValueType::Scalar);
      value.GetScalar() = enm;
      return_valobj_sp = ValueObjectConstResult::Create(
          thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
    } else if (type_flags & eTypeIsFloat) // 'Floating Point'
    {
      if (*byte_size <= 12) // handles float, double, long double, __float80
      {
        const RegisterInfo *st0_info = reg_ctx->GetRegisterInfoByName("st0", 0);
        RegisterValue st0_value;

        if (reg_ctx->ReadRegister(st0_info, st0_value)) {
          DataExtractor data;
          if (st0_value.GetData(data)) {
            lldb::offset_t offset = 0;
            long double value_long_double = data.GetLongDouble(&offset);

            // float is 4 bytes.
            if (*byte_size == 4) {
              float value_float = (float)value_long_double;
              value.GetScalar() = value_float;
              success = true;
            } else if (*byte_size == 8) {
              // double is 8 bytes
              // On Android Platform: long double is also 8 bytes It will be
              // handled here only.
              double value_double = (double)value_long_double;
              value.GetScalar() = value_double;
              success = true;
            } else if (*byte_size == 12) {
              // long double and __float80 are 12 bytes on i386.
              value.GetScalar() = value_long_double;
              success = true;
            }
          }
        }

        if (success)
          return_valobj_sp = ValueObjectConstResult::Create(
              thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
      } else if (*byte_size == 16) // handles __float128
      {
        lldb::addr_t storage_addr = (uint32_t)(
            thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
            0xffffffff);
        return_valobj_sp = ValueObjectMemory::Create(
            &thread, "", Address(storage_addr, nullptr), return_compiler_type);
      }
    } else // Neither 'Integral' nor 'Floating Point'
    {
      // If flow reaches here then check type_flags This type_flags is
      // unhandled
    }
  } else if (type_flags & eTypeIsComplex) // 'Complex Floating Point'
  {
    // ToDo: Yet to be implemented
  } else if (type_flags & eTypeIsVector) // 'Packed'
  {
    std::optional<uint64_t> byte_size =
        return_compiler_type.GetByteSize(&thread);
    if (byte_size && *byte_size > 0) {
      const RegisterInfo *vec_reg = reg_ctx->GetRegisterInfoByName("xmm0", 0);
      if (vec_reg == nullptr)
        vec_reg = reg_ctx->GetRegisterInfoByName("mm0", 0);

      if (vec_reg) {
        if (*byte_size <= vec_reg->byte_size) {
          ProcessSP process_sp(thread.GetProcess());
          if (process_sp) {
            std::unique_ptr<DataBufferHeap> heap_data_up(
                new DataBufferHeap(*byte_size, 0));
            const ByteOrder byte_order = process_sp->GetByteOrder();
            RegisterValue reg_value;
            if (reg_ctx->ReadRegister(vec_reg, reg_value)) {
              Status error;
              if (reg_value.GetAsMemoryData(*vec_reg, heap_data_up->GetBytes(),
                                            heap_data_up->GetByteSize(),
                                            byte_order, error)) {
                DataExtractor data(DataBufferSP(heap_data_up.release()),
                                   byte_order,
                                   process_sp->GetTarget()
                                       .GetArchitecture()
                                       .GetAddressByteSize());
                return_valobj_sp = ValueObjectConstResult::Create(
                    &thread, return_compiler_type, ConstString(""), data);
              }
            }
          }
        } else if (*byte_size <= vec_reg->byte_size * 2) {
          const RegisterInfo *vec_reg2 =
              reg_ctx->GetRegisterInfoByName("xmm1", 0);
          if (vec_reg2) {
            ProcessSP process_sp(thread.GetProcess());
            if (process_sp) {
              std::unique_ptr<DataBufferHeap> heap_data_up(
                  new DataBufferHeap(*byte_size, 0));
              const ByteOrder byte_order = process_sp->GetByteOrder();
              RegisterValue reg_value;
              RegisterValue reg_value2;
              if (reg_ctx->ReadRegister(vec_reg, reg_value) &&
                  reg_ctx->ReadRegister(vec_reg2, reg_value2)) {

                Status error;
                if (reg_value.GetAsMemoryData(
                        *vec_reg, heap_data_up->GetBytes(), vec_reg->byte_size,
                        byte_order, error) &&
                    reg_value2.GetAsMemoryData(
                        *vec_reg2,
                        heap_data_up->GetBytes() + vec_reg->byte_size,
                        heap_data_up->GetByteSize() - vec_reg->byte_size,
                        byte_order, error)) {
                  DataExtractor data(DataBufferSP(heap_data_up.release()),
                                     byte_order,
                                     process_sp->GetTarget()
                                         .GetArchitecture()
                                         .GetAddressByteSize());
                  return_valobj_sp = ValueObjectConstResult::Create(
                      &thread, return_compiler_type, ConstString(""), data);
                }
              }
            }
          }
        }
      }
    }
  } else // 'Decimal Floating Point'
  {
    // ToDo: Yet to be implemented
  }
  return return_valobj_sp;
}

ValueObjectSP ABISysV_i386::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;

  if (!return_compiler_type)
    return return_valobj_sp;

  ExecutionContext exe_ctx(thread.shared_from_this());
  return_valobj_sp = GetReturnValueObjectSimple(thread, return_compiler_type);
  if (return_valobj_sp)
    return return_valobj_sp;

  RegisterContextSP reg_ctx_sp = thread.GetRegisterContext();
  if (!reg_ctx_sp)
    return return_valobj_sp;

  if (return_compiler_type.IsAggregateType()) {
    unsigned eax_id =
        reg_ctx_sp->GetRegisterInfoByName("eax", 0)->kinds[eRegisterKindLLDB];
    lldb::addr_t storage_addr = (uint32_t)(
        thread.GetRegisterContext()->ReadRegisterAsUnsigned(eax_id, 0) &
        0xffffffff);
    return_valobj_sp = ValueObjectMemory::Create(
        &thread, "", Address(storage_addr, nullptr), return_compiler_type);
  }

  return return_valobj_sp;
}

// This defines CFA as esp+4
// The saved pc is at CFA-4 (i.e. esp+0)
// The saved esp is CFA+0

bool ABISysV_i386::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = dwarf_esp;
  uint32_t pc_reg_num = dwarf_eip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 4);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -4, false);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);
  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("i386 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  return true;
}

// This defines CFA as ebp+8
// The saved pc is at CFA-4 (i.e. ebp+4)
// The saved ebp is at CFA-8 (i.e. ebp+0)
// The saved esp is CFA+0

bool ABISysV_i386::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t fp_reg_num = dwarf_ebp;
  uint32_t sp_reg_num = dwarf_esp;
  uint32_t pc_reg_num = dwarf_eip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  const int32_t ptr_size = 4;

  row->GetCFAValue().SetIsRegisterPlusOffset(fp_reg_num, 2 * ptr_size);
  row->SetOffset(0);
  row->SetUnspecifiedRegistersAreUndefined(true);

  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, ptr_size * -2, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * -1, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("i386 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetUnwindPlanForSignalTrap(eLazyBoolNo);
  return true;
}

// According to "Register Usage" in reference document (specified on top of
// this source file) ebx, ebp, esi, edi and esp registers are preserved i.e.
// non-volatile i.e. callee-saved on i386
bool ABISysV_i386::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info)
    return false;

  // Saved registers are ebx, ebp, esi, edi, esp, eip
  const char *name = reg_info->name;
  if (name[0] == 'e') {
    switch (name[1]) {
    case 'b':
      if (name[2] == 'x' || name[2] == 'p')
        return name[3] == '\0';
      break;
    case 'd':
      if (name[2] == 'i')
        return name[3] == '\0';
      break;
    case 'i':
      if (name[2] == 'p')
        return name[3] == '\0';
      break;
    case 's':
      if (name[2] == 'i' || name[2] == 'p')
        return name[3] == '\0';
      break;
    }
  }

  if (name[0] == 's' && name[1] == 'p' && name[2] == '\0') // sp
    return true;
  if (name[0] == 'f' && name[1] == 'p' && name[2] == '\0') // fp
    return true;
  if (name[0] == 'p' && name[1] == 'c' && name[2] == '\0') // pc
    return true;

  return false;
}

void ABISysV_i386::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for i386 targets", CreateInstance);
}

void ABISysV_i386::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
