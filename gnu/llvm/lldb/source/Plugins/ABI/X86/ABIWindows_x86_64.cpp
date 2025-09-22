//===-- ABIWindows_x86_64.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIWindows_x86_64.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
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
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ABIWindows_x86_64)

enum dwarf_regnums {
  dwarf_rax = 0,
  dwarf_rdx,
  dwarf_rcx,
  dwarf_rbx,
  dwarf_rsi,
  dwarf_rdi,
  dwarf_rbp,
  dwarf_rsp,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,
  dwarf_r11,
  dwarf_r12,
  dwarf_r13,
  dwarf_r14,
  dwarf_r15,
  dwarf_rip,
  dwarf_xmm0,
  dwarf_xmm1,
  dwarf_xmm2,
  dwarf_xmm3,
  dwarf_xmm4,
  dwarf_xmm5,
  dwarf_xmm6,
  dwarf_xmm7,
  dwarf_xmm8,
  dwarf_xmm9,
  dwarf_xmm10,
  dwarf_xmm11,
  dwarf_xmm12,
  dwarf_xmm13,
  dwarf_xmm14,
  dwarf_xmm15,
  dwarf_stmm0,
  dwarf_stmm1,
  dwarf_stmm2,
  dwarf_stmm3,
  dwarf_stmm4,
  dwarf_stmm5,
  dwarf_stmm6,
  dwarf_stmm7,
  dwarf_ymm0,
  dwarf_ymm1,
  dwarf_ymm2,
  dwarf_ymm3,
  dwarf_ymm4,
  dwarf_ymm5,
  dwarf_ymm6,
  dwarf_ymm7,
  dwarf_ymm8,
  dwarf_ymm9,
  dwarf_ymm10,
  dwarf_ymm11,
  dwarf_ymm12,
  dwarf_ymm13,
  dwarf_ymm14,
  dwarf_ymm15,
  dwarf_bnd0 = 126,
  dwarf_bnd1,
  dwarf_bnd2,
  dwarf_bnd3
};

bool ABIWindows_x86_64::GetPointerReturnRegister(const char *&name) {
  name = "rax";
  return true;
}

size_t ABIWindows_x86_64::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABIWindows_x86_64::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::x86_64 &&
      arch.GetTriple().isOSWindows()) {
    return ABISP(
        new ABIWindows_x86_64(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABIWindows_x86_64::PrepareTrivialCall(Thread &thread, addr_t sp,
                                           addr_t func_addr, addr_t return_addr,
                                           llvm::ArrayRef<addr_t> args) const {
  Log *log = GetLog(LLDBLog::Expressions);

  if (log) {
    StreamString s;
    s.Printf("ABIWindows_x86_64::PrepareTrivialCall (tid = 0x%" PRIx64
             ", sp = 0x%" PRIx64 ", func_addr = 0x%" PRIx64
             ", return_addr = 0x%" PRIx64,
             thread.GetID(), (uint64_t)sp, (uint64_t)func_addr,
             (uint64_t)return_addr);

    for (size_t i = 0; i < args.size(); ++i)
      s.Printf(", arg%" PRIu64 " = 0x%" PRIx64, static_cast<uint64_t>(i + 1),
               args[i]);
    s.PutCString(")");
    log->PutString(s.GetString());
  }

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  const RegisterInfo *reg_info = nullptr;

  if (args.size() > 4) // Windows x64 only put first 4 arguments into registers
    return false;

  for (size_t i = 0; i < args.size(); ++i) {
    reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                        LLDB_REGNUM_GENERIC_ARG1 + i);
    LLDB_LOGF(log, "About to write arg%" PRIu64 " (0x%" PRIx64 ") into %s",
              static_cast<uint64_t>(i + 1), args[i], reg_info->name);
    if (!reg_ctx->WriteRegisterFromUnsigned(reg_info, args[i]))
      return false;
  }

  // First, align the SP

  LLDB_LOGF(log, "16-byte aligning SP: 0x%" PRIx64 " to 0x%" PRIx64,
            (uint64_t)sp, (uint64_t)(sp & ~0xfull));

  sp &= ~(0xfull); // 16-byte alignment

  sp -= 8; // return address

  Status error;
  const RegisterInfo *pc_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *sp_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  ProcessSP process_sp(thread.GetProcess());

  RegisterValue reg_value;
  LLDB_LOGF(log,
            "Pushing the return address onto the stack: 0x%" PRIx64
            ": 0x%" PRIx64,
            (uint64_t)sp, (uint64_t)return_addr);

  // Save return address onto the stack
  if (!process_sp->WritePointerToMemory(sp, return_addr, error))
    return false;

  // %rsp is set to the actual stack value.

  LLDB_LOGF(log, "Writing SP: 0x%" PRIx64, (uint64_t)sp);

  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_info, sp))
    return false;

  // %rip is set to the address of the called function.

  LLDB_LOGF(log, "Writing IP: 0x%" PRIx64, (uint64_t)func_addr);

  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_info, func_addr))
    return false;

  return true;
}

static bool ReadIntegerArgument(Scalar &scalar, unsigned int bit_width,
                                bool is_signed, Thread &thread,
                                uint32_t *argument_register_ids,
                                unsigned int &current_argument_register,
                                addr_t &current_stack_argument) {
  if (bit_width > 64)
    return false; // Scalar can't hold large integer arguments

  if (current_argument_register < 4) { // Windows pass first 4 arguments to register
    scalar = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
        argument_register_ids[current_argument_register], 0);
    current_argument_register++;
    if (is_signed)
      scalar.SignExtend(bit_width);
  	return true;
  }
  uint32_t byte_size = (bit_width + (CHAR_BIT - 1)) / CHAR_BIT;
  Status error;
  if (thread.GetProcess()->ReadScalarIntegerFromMemory(
          current_stack_argument, byte_size, is_signed, scalar, error)) {
    current_stack_argument += byte_size;
    return true;
  }
  return false;
}

bool ABIWindows_x86_64::GetArgumentValues(Thread &thread,
                                       ValueList &values) const {
  unsigned int num_values = values.GetSize();
  unsigned int value_index;

  // Extract the register context so we can read arguments from registers

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  // Get the pointer to the first stack argument so we have a place to start
  // when reading data

  addr_t sp = reg_ctx->GetSP(0);

  if (!sp)
    return false;

  addr_t current_stack_argument = sp + 8; // jump over return address

  uint32_t argument_register_ids[4];

  argument_register_ids[0] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[1] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG2)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[2] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG3)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[3] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG4)
          ->kinds[eRegisterKindLLDB];

  unsigned int current_argument_register = 0;

  for (value_index = 0; value_index < num_values; ++value_index) {
    Value *value = values.GetValueAtIndex(value_index);

    if (!value)
      return false;

    CompilerType compiler_type = value->GetCompilerType();
    std::optional<uint64_t> bit_size = compiler_type.GetBitSize(&thread);
    if (!bit_size)
      return false;
    bool is_signed;

    if (compiler_type.IsIntegerOrEnumerationType(is_signed)) {
      ReadIntegerArgument(value->GetScalar(), *bit_size, is_signed, thread,
                          argument_register_ids, current_argument_register,
                          current_stack_argument);
    } else if (compiler_type.IsPointerType()) {
      ReadIntegerArgument(value->GetScalar(), *bit_size, false, thread,
                          argument_register_ids, current_argument_register,
                          current_stack_argument);
    }
  }

  return true;
}

Status ABIWindows_x86_64::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
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

  Thread *thread = frame_sp->GetThread().get();

  bool is_signed;
  uint32_t count;
  bool is_complex;

  RegisterContext *reg_ctx = thread->GetRegisterContext().get();

  bool set_it_simple = false;
  if (compiler_type.IsIntegerOrEnumerationType(is_signed) ||
      compiler_type.IsPointerType()) {
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName("rax", 0);

    DataExtractor data;
    Status data_error;
    size_t num_bytes = new_value_sp->GetData(data, data_error);
    if (data_error.Fail()) {
      error.SetErrorStringWithFormat(
          "Couldn't convert return value to raw data: %s",
          data_error.AsCString());
      return error;
    }
    lldb::offset_t offset = 0;
    if (num_bytes <= 8) {
      uint64_t raw_value = data.GetMaxU64(&offset, num_bytes);

      if (reg_ctx->WriteRegisterFromUnsigned(reg_info, raw_value))
        set_it_simple = true;
    } else {
      error.SetErrorString("We don't support returning longer than 64 bit "
                           "integer values at present.");
    }
  } else if (compiler_type.IsFloatingPointType(count, is_complex)) {
    if (is_complex)
      error.SetErrorString(
          "We don't support returning complex values at present");
    else {
      std::optional<uint64_t> bit_width =
          compiler_type.GetBitSize(frame_sp.get());
      if (!bit_width) {
        error.SetErrorString("can't get type size");
        return error;
      }
      if (*bit_width <= 64) {
        const RegisterInfo *xmm0_info =
            reg_ctx->GetRegisterInfoByName("xmm0", 0);
        RegisterValue xmm0_value;
        DataExtractor data;
        Status data_error;
        size_t num_bytes = new_value_sp->GetData(data, data_error);
        if (data_error.Fail()) {
          error.SetErrorStringWithFormat(
              "Couldn't convert return value to raw data: %s",
              data_error.AsCString());
          return error;
        }

        unsigned char buffer[16];
        ByteOrder byte_order = data.GetByteOrder();

        data.CopyByteOrderedData(0, num_bytes, buffer, 16, byte_order);
        xmm0_value.SetBytes(buffer, 16, byte_order);
        reg_ctx->WriteRegister(xmm0_info, xmm0_value);
        set_it_simple = true;
      } else {
        // Windows doesn't support 80 bit FP
        error.SetErrorString(
            "Windows-x86_64 doesn't allow FP larger than 64 bits.");
      }
    }
  }

  if (!set_it_simple) {
    // Okay we've got a structure or something that doesn't fit in a simple
    // register.
    // TODO(wanyi): On Windows, if the return type is a struct:
    // 1) smaller that 64 bits and return by value -> RAX
    // 2) bigger than 64 bits, the caller will allocate memory for that struct
    // and pass the struct pointer in RCX then return the pointer in RAX
    error.SetErrorString("We only support setting simple integer and float "
                         "return types at present.");
  }

  return error;
}

ValueObjectSP ABIWindows_x86_64::GetReturnValueObjectSimple(
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
  if (type_flags & eTypeIsScalar) {
    value.SetValueType(Value::ValueType::Scalar);

    bool success = false;
    if (type_flags & eTypeIsInteger) {
      // Extract the register context so we can read arguments from registers
      std::optional<uint64_t> byte_size =
          return_compiler_type.GetByteSize(&thread);
      if (!byte_size)
        return return_valobj_sp;
      uint64_t raw_value = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
          reg_ctx->GetRegisterInfoByName("rax", 0), 0);
      const bool is_signed = (type_flags & eTypeIsSigned) != 0;
      switch (*byte_size) {
      default:
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
    } else if (type_flags & eTypeIsFloat) {
      if (type_flags & eTypeIsComplex) {
        // Don't handle complex yet.
      } else {
        std::optional<uint64_t> byte_size =
            return_compiler_type.GetByteSize(&thread);
        if (byte_size && *byte_size <= sizeof(long double)) {
          const RegisterInfo *xmm0_info =
              reg_ctx->GetRegisterInfoByName("xmm0", 0);
          RegisterValue xmm0_value;
          if (reg_ctx->ReadRegister(xmm0_info, xmm0_value)) {
            DataExtractor data;
            if (xmm0_value.GetData(data)) {
              lldb::offset_t offset = 0;
              if (*byte_size == sizeof(float)) {
                value.GetScalar() = (float)data.GetFloat(&offset);
                success = true;
              } else if (*byte_size == sizeof(double)) {
                // double and long double are the same on windows
                value.GetScalar() = (double)data.GetDouble(&offset);
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
  } else if ((type_flags & eTypeIsPointer) ||
             (type_flags & eTypeInstanceIsPointer)) {
    unsigned rax_id =
        reg_ctx->GetRegisterInfoByName("rax", 0)->kinds[eRegisterKindLLDB];
    value.GetScalar() =
        (uint64_t)thread.GetRegisterContext()->ReadRegisterAsUnsigned(rax_id,
                                                                      0);
    value.SetValueType(Value::ValueType::Scalar);
    return_valobj_sp = ValueObjectConstResult::Create(
        thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if (type_flags & eTypeIsVector) {
    std::optional<uint64_t> byte_size =
        return_compiler_type.GetByteSize(&thread);
    if (byte_size && *byte_size > 0) {
      const RegisterInfo *xmm_reg =
          reg_ctx->GetRegisterInfoByName("xmm0", 0);
      if (xmm_reg == nullptr)
        xmm_reg = reg_ctx->GetRegisterInfoByName("mm0", 0);

      if (xmm_reg) {
        if (*byte_size <= xmm_reg->byte_size) {
          ProcessSP process_sp(thread.GetProcess());
          if (process_sp) {
            std::unique_ptr<DataBufferHeap> heap_data_up(
                new DataBufferHeap(*byte_size, 0));
            const ByteOrder byte_order = process_sp->GetByteOrder();
            RegisterValue reg_value;
            if (reg_ctx->ReadRegister(xmm_reg, reg_value)) {
              Status error;
              if (reg_value.GetAsMemoryData(*xmm_reg, heap_data_up->GetBytes(),
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
        }
      }
    }
  }

  return return_valobj_sp;
}

// The compiler will flatten the nested aggregate type into single
// layer and push the value to stack
// This helper function will flatten an aggregate type
// and return true if it can be returned in register(s) by value
// return false if the aggregate is in memory
static bool FlattenAggregateType(
    Thread &thread, ExecutionContext &exe_ctx,
    CompilerType &return_compiler_type,
    uint32_t data_byte_offset,
    std::vector<uint32_t> &aggregate_field_offsets,
    std::vector<CompilerType> &aggregate_compiler_types) {

  const uint32_t num_children = return_compiler_type.GetNumFields();
  for (uint32_t idx = 0; idx < num_children; ++idx) {
    std::string name;
    bool is_signed;
    uint32_t count;
    bool is_complex;

    uint64_t field_bit_offset = 0;
    CompilerType field_compiler_type = return_compiler_type.GetFieldAtIndex(
        idx, name, &field_bit_offset, nullptr, nullptr);
    std::optional<uint64_t> field_bit_width =
        field_compiler_type.GetBitSize(&thread);

    // if we don't know the size of the field (e.g. invalid type), exit
    if (!field_bit_width || *field_bit_width == 0) {
      return false;
    }
    // If there are any unaligned fields, this is stored in memory.
    if (field_bit_offset % *field_bit_width != 0) {
      return false;
    }

    // add overall offset
    uint32_t field_byte_offset = field_bit_offset / 8 + data_byte_offset;

    const uint32_t field_type_flags = field_compiler_type.GetTypeInfo();
    if (field_compiler_type.IsIntegerOrEnumerationType(is_signed) ||
        field_compiler_type.IsPointerType() ||
        field_compiler_type.IsFloatingPointType(count, is_complex)) {
      aggregate_field_offsets.push_back(field_byte_offset);
      aggregate_compiler_types.push_back(field_compiler_type);
    } else if (field_type_flags & eTypeHasChildren) {
      if (!FlattenAggregateType(thread, exe_ctx, field_compiler_type,
                                field_byte_offset, aggregate_field_offsets,
                                aggregate_compiler_types)) {
        return false;
      }
    }
  }
  return true;
}

ValueObjectSP ABIWindows_x86_64::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;

  if (!return_compiler_type) {
    return return_valobj_sp;
  }

  // try extract value as if it's a simple type
  return_valobj_sp = GetReturnValueObjectSimple(thread, return_compiler_type);
  if (return_valobj_sp) {
    return return_valobj_sp;
  }

  RegisterContextSP reg_ctx_sp = thread.GetRegisterContext();
  if (!reg_ctx_sp) {
    return return_valobj_sp;
  }

  std::optional<uint64_t> bit_width = return_compiler_type.GetBitSize(&thread);
  if (!bit_width) {
    return return_valobj_sp;
  }

  // if it's not simple or aggregate type, then we don't know how to handle it
  if (!return_compiler_type.IsAggregateType()) {
    return return_valobj_sp;
  }

  ExecutionContext exe_ctx(thread.shared_from_this());
  Target *target = exe_ctx.GetTargetPtr();
  uint32_t max_register_value_bit_width = 64;

  // The scenario here is to have a struct/class which is POD
  // if the return struct/class size is larger than 64 bits,
  // the caller will allocate memory for it and pass the return addr in RCX
  // then return the address in RAX

  // if the struct is returned by value in register (RAX)
  // its size has to be: 1, 2, 4, 8, 16, 32, or 64 bits (aligned)
  // for floating point, the return value will be copied over to RAX
  bool is_memory = *bit_width > max_register_value_bit_width ||
                   *bit_width & (*bit_width - 1);
  std::vector<uint32_t> aggregate_field_offsets;
  std::vector<CompilerType> aggregate_compiler_types;
  if (!is_memory &&
      FlattenAggregateType(thread, exe_ctx, return_compiler_type,
                           0, aggregate_field_offsets,
                           aggregate_compiler_types)) {
    ByteOrder byte_order = target->GetArchitecture().GetByteOrder();
    WritableDataBufferSP data_sp(
        new DataBufferHeap(max_register_value_bit_width / 8, 0));
    DataExtractor return_ext(data_sp, byte_order,
        target->GetArchitecture().GetAddressByteSize());

    // The only register used to return struct/class by value
    const RegisterInfo *rax_info =
        reg_ctx_sp->GetRegisterInfoByName("rax", 0);
    RegisterValue rax_value;
    reg_ctx_sp->ReadRegister(rax_info, rax_value);
    DataExtractor rax_data;
    rax_value.GetData(rax_data);

    uint32_t used_bytes =
        0; // Tracks how much of the rax registers we've consumed so far

    // in case of the returned type is a subclass of non-abstract-base class
    // it will have a padding to skip the base content
    if (aggregate_field_offsets.size())
      used_bytes = aggregate_field_offsets[0];

    const uint32_t num_children = aggregate_compiler_types.size();
    for (uint32_t idx = 0; idx < num_children; idx++) {
      bool is_signed;
      bool is_complex;
      uint32_t count;

      CompilerType field_compiler_type = aggregate_compiler_types[idx];
      uint32_t field_byte_width = (uint32_t) (*field_compiler_type.GetByteSize(&thread));
      uint32_t field_byte_offset = aggregate_field_offsets[idx];

      // this is unlikely w/o the overall size being greater than 8 bytes
      // For now, return a nullptr return value object.
      if (used_bytes >= 8 || used_bytes + field_byte_width > 8) {
        return return_valobj_sp;
      }

      DataExtractor *copy_from_extractor = nullptr;
      uint32_t copy_from_offset = 0;
      if (field_compiler_type.IsIntegerOrEnumerationType(is_signed) ||
          field_compiler_type.IsPointerType() ||
          field_compiler_type.IsFloatingPointType(count, is_complex)) {
        copy_from_extractor = &rax_data;
        copy_from_offset = used_bytes;
        used_bytes += field_byte_width;
      }
      // These two tests are just sanity checks.  If I somehow get the type
      // calculation wrong above it is better to just return nothing than to
      // assert or crash.
      if (!copy_from_extractor) {
        return return_valobj_sp;
      }
      if (copy_from_offset + field_byte_width >
          copy_from_extractor->GetByteSize()) {
        return return_valobj_sp;
      }
      copy_from_extractor->CopyByteOrderedData(copy_from_offset,
          field_byte_width, data_sp->GetBytes() + field_byte_offset,
          field_byte_width, byte_order);
    }
    if (!is_memory) {
      // The result is in our data buffer.  Let's make a variable object out
      // of it:
      return_valobj_sp = ValueObjectConstResult::Create(
          &thread, return_compiler_type, ConstString(""), return_ext);
    }
  }

  // The Windows x86_64 ABI specifies that the return address for MEMORY
  // objects be placed in rax on exit from the function.

  // FIXME: This is just taking a guess, rax may very well no longer hold the
  // return storage location.
  // If we are going to do this right, when we make a new frame we should
  // check to see if it uses a memory return, and if we are at the first
  // instruction and if so stash away the return location.  Then we would
  // only return the memory return value if we know it is valid.
  if (is_memory) {
    unsigned rax_id =
        reg_ctx_sp->GetRegisterInfoByName("rax", 0)->kinds[eRegisterKindLLDB];
    lldb::addr_t storage_addr =
        (uint64_t)thread.GetRegisterContext()->ReadRegisterAsUnsigned(rax_id,
                                                                      0);
    return_valobj_sp = ValueObjectMemory::Create(
        &thread, "", Address(storage_addr, nullptr), return_compiler_type);
  }
  return return_valobj_sp;
}

// This defines the CFA as rsp+8
// the saved pc is at CFA-8 (i.e. rsp+0)
// The saved rsp is CFA+0

bool ABIWindows_x86_64::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = dwarf_rsp;
  uint32_t pc_reg_num = dwarf_rip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 8);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -8, false);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);
  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("x86_64 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  return true;
}

// Windows-x86_64 doesn't use %rbp
// No available Unwind information for Windows-x86_64 (section .pdata)
// Let's use SysV-x86_64 one for now
bool ABIWindows_x86_64::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t fp_reg_num = dwarf_rbp;
  uint32_t sp_reg_num = dwarf_rsp;
  uint32_t pc_reg_num = dwarf_rip;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  const int32_t ptr_size = 8;
  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf_rbp, 2 * ptr_size);
  row->SetOffset(0);
  row->SetUnspecifiedRegistersAreUndefined(true);

  row->SetRegisterLocationToAtCFAPlusOffset(fp_reg_num, ptr_size * -2, true);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * -1, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("x86_64 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);

  return true;
}

bool ABIWindows_x86_64::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABIWindows_x86_64::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info)
    return false;
  assert(reg_info->name != nullptr && "unnamed register?");
  std::string Name = std::string(reg_info->name);
  bool IsCalleeSaved =
      llvm::StringSwitch<bool>(Name)
          .Cases("rbx", "ebx", "rbp", "ebp", "rdi", "edi", "rsi", "esi", true)
          .Cases("rsp", "esp", "r12", "r13", "r14", "r15", "sp", "fp", true)
          .Cases("xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12",
                 "xmm13", "xmm14", "xmm15", true)
          .Default(false);
  return IsCalleeSaved;
}

uint32_t ABIWindows_x86_64::GetGenericNum(llvm::StringRef reg) {
  return llvm::StringSwitch<uint32_t>(reg)
      .Case("rip", LLDB_REGNUM_GENERIC_PC)
      .Case("rsp", LLDB_REGNUM_GENERIC_SP)
      .Case("rbp", LLDB_REGNUM_GENERIC_FP)
      .Case("rflags", LLDB_REGNUM_GENERIC_FLAGS)
      // gdbserver uses eflags
      .Case("eflags", LLDB_REGNUM_GENERIC_FLAGS)
      .Case("rcx", LLDB_REGNUM_GENERIC_ARG1)
      .Case("rdx", LLDB_REGNUM_GENERIC_ARG2)
      .Case("r8", LLDB_REGNUM_GENERIC_ARG3)
      .Case("r9", LLDB_REGNUM_GENERIC_ARG4)
      .Default(LLDB_INVALID_REGNUM);
}

void ABIWindows_x86_64::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "Windows ABI for x86_64 targets", CreateInstance);
}

void ABIWindows_x86_64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
