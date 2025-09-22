//===-- ABISysV_ppc64.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_ppc64.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/TargetParser/Triple.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "Utility/PPC64LE_DWARF_Registers.h"
#include "Utility/PPC64_DWARF_Registers.h"
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

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"

#define DECLARE_REGISTER_INFOS_PPC64_STRUCT
#include "Plugins/Process/Utility/RegisterInfos_ppc64.h"
#undef DECLARE_REGISTER_INFOS_PPC64_STRUCT

#define DECLARE_REGISTER_INFOS_PPC64LE_STRUCT
#include "Plugins/Process/Utility/RegisterInfos_ppc64le.h"
#undef DECLARE_REGISTER_INFOS_PPC64LE_STRUCT
#include <optional>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ABISysV_ppc64)

const lldb_private::RegisterInfo *
ABISysV_ppc64::GetRegisterInfoArray(uint32_t &count) {
  if (GetByteOrder() == lldb::eByteOrderLittle) {
    count = std::size(g_register_infos_ppc64le);
    return g_register_infos_ppc64le;
  } else {
    count = std::size(g_register_infos_ppc64);
    return g_register_infos_ppc64;
  }
}

size_t ABISysV_ppc64::GetRedZoneSize() const { return 224; }

lldb::ByteOrder ABISysV_ppc64::GetByteOrder() const {
  return GetProcessSP()->GetByteOrder();
}

// Static Functions

ABISP
ABISysV_ppc64::CreateInstance(lldb::ProcessSP process_sp,
                              const ArchSpec &arch) {
  if (arch.GetTriple().isPPC64())
    return ABISP(
        new ABISysV_ppc64(std::move(process_sp), MakeMCRegisterInfo(arch)));
  return ABISP();
}

bool ABISysV_ppc64::PrepareTrivialCall(Thread &thread, addr_t sp,
                                       addr_t func_addr, addr_t return_addr,
                                       llvm::ArrayRef<addr_t> args) const {
  Log *log = GetLog(LLDBLog::Expressions);

  if (log) {
    StreamString s;
    s.Printf("ABISysV_ppc64::PrepareTrivialCall (tid = 0x%" PRIx64
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

  if (args.size() > 8) // TODO handle more than 8 arguments
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

  sp -= 544; // allocate frame to save TOC, RA and SP.

  Status error;
  uint64_t reg_value;
  const RegisterInfo *pc_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *sp_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  ProcessSP process_sp(thread.GetProcess());
  const RegisterInfo *lr_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA);
  const RegisterInfo *r2_reg_info = reg_ctx->GetRegisterInfoAtIndex(2);
  const RegisterInfo *r12_reg_info = reg_ctx->GetRegisterInfoAtIndex(12);

  // Save return address onto the stack.
  LLDB_LOGF(log,
            "Pushing the return address onto the stack: 0x%" PRIx64
            "(+16): 0x%" PRIx64,
            (uint64_t)sp, (uint64_t)return_addr);
  if (!process_sp->WritePointerToMemory(sp + 16, return_addr, error))
    return false;

  // Write the return address to link register.
  LLDB_LOGF(log, "Writing LR: 0x%" PRIx64, (uint64_t)return_addr);
  if (!reg_ctx->WriteRegisterFromUnsigned(lr_reg_info, return_addr))
    return false;

  // Write target address to %r12 register.
  LLDB_LOGF(log, "Writing R12: 0x%" PRIx64, (uint64_t)func_addr);
  if (!reg_ctx->WriteRegisterFromUnsigned(r12_reg_info, func_addr))
    return false;

  // Read TOC pointer value.
  reg_value = reg_ctx->ReadRegisterAsUnsigned(r2_reg_info, 0);

  // Write TOC pointer onto the stack.
  uint64_t stack_offset;
  if (GetByteOrder() == lldb::eByteOrderLittle)
    stack_offset = 24;
  else
    stack_offset = 40;

  LLDB_LOGF(log, "Writing R2 (TOC) at SP(0x%" PRIx64 ")+%d: 0x%" PRIx64,
            (uint64_t)(sp + stack_offset), (int)stack_offset,
            (uint64_t)reg_value);
  if (!process_sp->WritePointerToMemory(sp + stack_offset, reg_value, error))
    return false;

  // Read the current SP value.
  reg_value = reg_ctx->ReadRegisterAsUnsigned(sp_reg_info, 0);

  // Save current SP onto the stack.
  LLDB_LOGF(log, "Writing SP at SP(0x%" PRIx64 ")+0: 0x%" PRIx64, (uint64_t)sp,
            (uint64_t)reg_value);
  if (!process_sp->WritePointerToMemory(sp, reg_value, error))
    return false;

  // %r1 is set to the actual stack value.
  LLDB_LOGF(log, "Writing SP: 0x%" PRIx64, (uint64_t)sp);

  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_info, sp))
    return false;

  // %pc is set to the address of the called function.

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

  if (current_argument_register < 6) {
    scalar = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
        argument_register_ids[current_argument_register], 0);
    current_argument_register++;
    if (is_signed)
      scalar.SignExtend(bit_width);
  } else {
    uint32_t byte_size = (bit_width + (8 - 1)) / 8;
    Status error;
    if (thread.GetProcess()->ReadScalarIntegerFromMemory(
            current_stack_argument, byte_size, is_signed, scalar, error)) {
      current_stack_argument += byte_size;
      return true;
    }
    return false;
  }
  return true;
}

bool ABISysV_ppc64::GetArgumentValues(Thread &thread, ValueList &values) const {
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

  uint64_t stack_offset;
  if (GetByteOrder() == lldb::eByteOrderLittle)
    stack_offset = 32;
  else
    stack_offset = 48;

  // jump over return address.
  addr_t current_stack_argument = sp + stack_offset;
  uint32_t argument_register_ids[8];

  for (size_t i = 0; i < 8; ++i) {
    argument_register_ids[i] =
        reg_ctx
            ->GetRegisterInfo(eRegisterKindGeneric,
                              LLDB_REGNUM_GENERIC_ARG1 + i)
            ->kinds[eRegisterKindLLDB];
  }

  unsigned int current_argument_register = 0;

  for (value_index = 0; value_index < num_values; ++value_index) {
    Value *value = values.GetValueAtIndex(value_index);

    if (!value)
      return false;

    // We currently only support extracting values with Clang QualTypes. Do we
    // care about others?
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

Status ABISysV_ppc64::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
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
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName("r3", 0);

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
        error.SetErrorString("can't get size of type");
        return error;
      }
      if (*bit_width <= 64) {
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
        set_it_simple = true;
      } else {
        // FIXME - don't know how to do 80 bit long doubles yet.
        error.SetErrorString(
            "We don't support returning float values > 64 bits at present");
      }
    }
  }

  if (!set_it_simple) {
    // Okay we've got a structure or something that doesn't fit in a simple
    // register. We should figure out where it really goes, but we don't
    // support this yet.
    error.SetErrorString("We only support setting simple integer and float "
                         "return types at present.");
  }

  return error;
}

//
// ReturnValueExtractor
//

namespace {

#define LOG_PREFIX "ReturnValueExtractor: "

class ReturnValueExtractor {
  // This class represents a register, from which data may be extracted.
  //
  // It may be constructed by directly specifying its index (where 0 is the
  // first register used to return values) or by specifying the offset of a
  // given struct field, in which case the appropriated register index will be
  // calculated.
  class Register {
  public:
    enum Type {
      GPR, // General Purpose Register
      FPR  // Floating Point Register
    };

    // main constructor
    //
    // offs - field offset in struct
    Register(Type ty, uint32_t index, uint32_t offs, RegisterContext *reg_ctx,
             ByteOrder byte_order)
        : m_index(index), m_offs(offs % sizeof(uint64_t)),
          m_avail(sizeof(uint64_t) - m_offs), m_type(ty), m_reg_ctx(reg_ctx),
          m_byte_order(byte_order) {}

    // explicit index, no offset
    Register(Type ty, uint32_t index, RegisterContext *reg_ctx,
             ByteOrder byte_order)
        : Register(ty, index, 0, reg_ctx, byte_order) {}

    // GPR, calculate index from offs
    Register(uint32_t offs, RegisterContext *reg_ctx, ByteOrder byte_order)
        : Register(GPR, offs / sizeof(uint64_t), offs, reg_ctx, byte_order) {}

    uint32_t Index() const { return m_index; }

    // register offset where data is located
    uint32_t Offs() const { return m_offs; }

    // available bytes in this register
    uint32_t Avail() const { return m_avail; }

    bool IsValid() const {
      if (m_index > 7) {
        LLDB_LOG(m_log, LOG_PREFIX
                 "No more than 8 registers should be used to return values");
        return false;
      }
      return true;
    }

    std::string GetName() const {
      if (m_type == GPR)
        return ("r" + llvm::Twine(m_index + 3)).str();
      else
        return ("f" + llvm::Twine(m_index + 1)).str();
    }

    // get raw register data
    bool GetRawData(uint64_t &raw_data) {
      const RegisterInfo *reg_info =
          m_reg_ctx->GetRegisterInfoByName(GetName());
      if (!reg_info) {
        LLDB_LOG(m_log, LOG_PREFIX "Failed to get RegisterInfo");
        return false;
      }

      RegisterValue reg_val;
      if (!m_reg_ctx->ReadRegister(reg_info, reg_val)) {
        LLDB_LOG(m_log, LOG_PREFIX "ReadRegister() failed");
        return false;
      }

      Status error;
      uint32_t rc = reg_val.GetAsMemoryData(
          *reg_info, &raw_data, sizeof(raw_data), m_byte_order, error);
      if (rc != sizeof(raw_data)) {
        LLDB_LOG(m_log, LOG_PREFIX "GetAsMemoryData() failed");
        return false;
      }

      return true;
    }

  private:
    uint32_t m_index;
    uint32_t m_offs;
    uint32_t m_avail;
    Type m_type;
    RegisterContext *m_reg_ctx;
    ByteOrder m_byte_order;
    Log *m_log = GetLog(LLDBLog::Expressions);
  };

  Register GetGPR(uint32_t index) const {
    return Register(Register::GPR, index, m_reg_ctx, m_byte_order);
  }

  Register GetFPR(uint32_t index) const {
    return Register(Register::FPR, index, m_reg_ctx, m_byte_order);
  }

  Register GetGPRByOffs(uint32_t offs) const {
    return Register(offs, m_reg_ctx, m_byte_order);
  }

public:
  // factory
  static llvm::Expected<ReturnValueExtractor> Create(Thread &thread,
                                                     CompilerType &type) {
    RegisterContext *reg_ctx = thread.GetRegisterContext().get();
    if (!reg_ctx)
      return llvm::createStringError(LOG_PREFIX
                                     "Failed to get RegisterContext");

    ProcessSP process_sp = thread.GetProcess();
    if (!process_sp)
      return llvm::createStringError(LOG_PREFIX "GetProcess() failed");

    return ReturnValueExtractor(thread, type, reg_ctx, process_sp);
  }

  // main method: get value of the type specified at construction time
  ValueObjectSP GetValue() {
    const uint32_t type_flags = m_type.GetTypeInfo();

    // call the appropriate type handler
    ValueSP value_sp;
    ValueObjectSP valobj_sp;
    if (type_flags & eTypeIsScalar) {
      if (type_flags & eTypeIsInteger) {
        value_sp = GetIntegerValue(0);
      } else if (type_flags & eTypeIsFloat) {
        if (type_flags & eTypeIsComplex) {
          LLDB_LOG(m_log, LOG_PREFIX "Complex numbers are not supported yet");
          return ValueObjectSP();
        } else {
          value_sp = GetFloatValue(m_type, 0);
        }
      }
    } else if (type_flags & eTypeIsPointer) {
      value_sp = GetPointerValue(0);
    }

    if (value_sp) {
      valobj_sp = ValueObjectConstResult::Create(
          m_thread.GetStackFrameAtIndex(0).get(), *value_sp, ConstString(""));
    } else if (type_flags & eTypeIsVector) {
      valobj_sp = GetVectorValueObject();
    } else if (type_flags & eTypeIsStructUnion || type_flags & eTypeIsClass) {
      valobj_sp = GetStructValueObject();
    }

    return valobj_sp;
  }

private:
  // data
  Thread &m_thread;
  CompilerType &m_type;
  uint64_t m_byte_size;
  std::unique_ptr<DataBufferHeap> m_data_up;
  int32_t m_src_offs = 0;
  int32_t m_dst_offs = 0;
  bool m_packed = false;
  Log *m_log = GetLog(LLDBLog::Expressions);
  RegisterContext *m_reg_ctx;
  ProcessSP m_process_sp;
  ByteOrder m_byte_order;
  uint32_t m_addr_size;

  // methods

  // constructor
  ReturnValueExtractor(Thread &thread, CompilerType &type,
                       RegisterContext *reg_ctx, ProcessSP process_sp)
      : m_thread(thread), m_type(type),
        m_byte_size(m_type.GetByteSize(&thread).value_or(0)),
        m_data_up(new DataBufferHeap(m_byte_size, 0)), m_reg_ctx(reg_ctx),
        m_process_sp(process_sp), m_byte_order(process_sp->GetByteOrder()),
        m_addr_size(
            process_sp->GetTarget().GetArchitecture().GetAddressByteSize()) {}

  // build a new scalar value
  ValueSP NewScalarValue(CompilerType &type) {
    ValueSP value_sp(new Value);
    value_sp->SetCompilerType(type);
    value_sp->SetValueType(Value::ValueType::Scalar);
    return value_sp;
  }

  // get an integer value in the specified register
  ValueSP GetIntegerValue(uint32_t reg_index) {
    uint64_t raw_value;
    auto reg = GetGPR(reg_index);
    if (!reg.GetRawData(raw_value))
      return ValueSP();

    // build value from data
    ValueSP value_sp(NewScalarValue(m_type));

    uint32_t type_flags = m_type.GetTypeInfo();
    bool is_signed = (type_flags & eTypeIsSigned) != 0;

    switch (m_byte_size) {
    case sizeof(uint64_t):
      if (is_signed)
        value_sp->GetScalar() = (int64_t)(raw_value);
      else
        value_sp->GetScalar() = (uint64_t)(raw_value);
      break;

    case sizeof(uint32_t):
      if (is_signed)
        value_sp->GetScalar() = (int32_t)(raw_value & UINT32_MAX);
      else
        value_sp->GetScalar() = (uint32_t)(raw_value & UINT32_MAX);
      break;

    case sizeof(uint16_t):
      if (is_signed)
        value_sp->GetScalar() = (int16_t)(raw_value & UINT16_MAX);
      else
        value_sp->GetScalar() = (uint16_t)(raw_value & UINT16_MAX);
      break;

    case sizeof(uint8_t):
      if (is_signed)
        value_sp->GetScalar() = (int8_t)(raw_value & UINT8_MAX);
      else
        value_sp->GetScalar() = (uint8_t)(raw_value & UINT8_MAX);
      break;

    default:
      llvm_unreachable("Invalid integer size");
    }

    return value_sp;
  }

  // get a floating point value on the specified register
  ValueSP GetFloatValue(CompilerType &type, uint32_t reg_index) {
    uint64_t raw_data;
    auto reg = GetFPR(reg_index);
    if (!reg.GetRawData(raw_data))
      return {};

    // build value from data
    ValueSP value_sp(NewScalarValue(type));

    DataExtractor de(&raw_data, sizeof(raw_data), m_byte_order, m_addr_size);

    offset_t offset = 0;
    std::optional<uint64_t> byte_size = type.GetByteSize(m_process_sp.get());
    if (!byte_size)
      return {};
    switch (*byte_size) {
    case sizeof(float):
      value_sp->GetScalar() = (float)de.GetDouble(&offset);
      break;

    case sizeof(double):
      value_sp->GetScalar() = de.GetDouble(&offset);
      break;

    default:
      llvm_unreachable("Invalid floating point size");
    }

    return value_sp;
  }

  // get pointer value from register
  ValueSP GetPointerValue(uint32_t reg_index) {
    uint64_t raw_data;
    auto reg = GetGPR(reg_index);
    if (!reg.GetRawData(raw_data))
      return ValueSP();

    // build value from raw data
    ValueSP value_sp(NewScalarValue(m_type));
    value_sp->GetScalar() = raw_data;
    return value_sp;
  }

  // build the ValueObject from our data buffer
  ValueObjectSP BuildValueObject() {
    DataExtractor de(DataBufferSP(m_data_up.release()), m_byte_order,
                     m_addr_size);
    return ValueObjectConstResult::Create(&m_thread, m_type, ConstString(""),
                                          de);
  }

  // get a vector return value
  ValueObjectSP GetVectorValueObject() {
    const uint32_t MAX_VRS = 2;

    // get first V register used to return values
    const RegisterInfo *vr[MAX_VRS];
    vr[0] = m_reg_ctx->GetRegisterInfoByName("vr2");
    if (!vr[0]) {
      LLDB_LOG(m_log, LOG_PREFIX "Failed to get vr2 RegisterInfo");
      return ValueObjectSP();
    }

    const uint32_t vr_size = vr[0]->byte_size;
    size_t vrs = 1;
    if (m_byte_size > 2 * vr_size) {
      LLDB_LOG(
          m_log, LOG_PREFIX
          "Returning vectors that don't fit in 2 VR regs is not supported");
      return ValueObjectSP();
    }

    // load vr3, if needed
    if (m_byte_size > vr_size) {
      vrs++;
      vr[1] = m_reg_ctx->GetRegisterInfoByName("vr3");
      if (!vr[1]) {
        LLDB_LOG(m_log, LOG_PREFIX "Failed to get vr3 RegisterInfo");
        return ValueObjectSP();
      }
    }

    // Get the whole contents of vector registers and let the logic here
    // arrange the data properly.

    RegisterValue vr_val[MAX_VRS];
    Status error;
    std::unique_ptr<DataBufferHeap> vr_data(
        new DataBufferHeap(vrs * vr_size, 0));

    for (uint32_t i = 0; i < vrs; i++) {
      if (!m_reg_ctx->ReadRegister(vr[i], vr_val[i])) {
        LLDB_LOG(m_log, LOG_PREFIX "Failed to read vector register contents");
        return ValueObjectSP();
      }
      if (!vr_val[i].GetAsMemoryData(*vr[i], vr_data->GetBytes() + i * vr_size,
                                     vr_size, m_byte_order, error)) {
        LLDB_LOG(m_log, LOG_PREFIX "Failed to extract vector register bytes");
        return ValueObjectSP();
      }
    }

    // The compiler generated code seems to always put the vector elements at
    // the end of the vector register, in case they don't occupy all of it.
    // This offset variable handles this.
    uint32_t offs = 0;
    if (m_byte_size < vr_size)
      offs = vr_size - m_byte_size;

    // copy extracted data to our buffer
    memcpy(m_data_up->GetBytes(), vr_data->GetBytes() + offs, m_byte_size);
    return BuildValueObject();
  }

  // get a struct return value
  ValueObjectSP GetStructValueObject() {
    // case 1: get from stack
    if (m_byte_size > 2 * sizeof(uint64_t)) {
      uint64_t addr;
      auto reg = GetGPR(0);
      if (!reg.GetRawData(addr))
        return {};

      Status error;
      size_t rc = m_process_sp->ReadMemory(addr, m_data_up->GetBytes(),
                                           m_byte_size, error);
      if (rc != m_byte_size) {
        LLDB_LOG(m_log, LOG_PREFIX "Failed to read memory pointed by r3");
        return ValueObjectSP();
      }
      return BuildValueObject();
    }

    // get number of children
    const bool omit_empty_base_classes = true;
    auto n_or_err = m_type.GetNumChildren(omit_empty_base_classes, nullptr);
    if (!n_or_err) {
      LLDB_LOG_ERROR(m_log, n_or_err.takeError(), LOG_PREFIX "{0}");
      return {};
    }
    uint32_t n = *n_or_err;
    if (!n) {
      LLDB_LOG(m_log, LOG_PREFIX "No children found in struct");
      return {};
    }

    // case 2: homogeneous double or float aggregate
    CompilerType elem_type;
    if (m_type.IsHomogeneousAggregate(&elem_type)) {
      uint32_t type_flags = elem_type.GetTypeInfo();
      std::optional<uint64_t> elem_size =
          elem_type.GetByteSize(m_process_sp.get());
      if (!elem_size)
        return {};
      if (type_flags & eTypeIsComplex || !(type_flags & eTypeIsFloat)) {
        LLDB_LOG(m_log,
                 LOG_PREFIX "Unexpected type found in homogeneous aggregate");
        return {};
      }

      for (uint32_t i = 0; i < n; i++) {
        ValueSP val_sp = GetFloatValue(elem_type, i);
        if (!val_sp)
          return {};

        // copy to buffer
        Status error;
        size_t rc = val_sp->GetScalar().GetAsMemoryData(
            m_data_up->GetBytes() + m_dst_offs, *elem_size, m_byte_order,
            error);
        if (rc != *elem_size) {
          LLDB_LOG(m_log, LOG_PREFIX "Failed to get float data");
          return {};
        }
        m_dst_offs += *elem_size;
      }
      return BuildValueObject();
    }

    // case 3: get from GPRs

    // first, check if this is a packed struct or not
    auto ast = m_type.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>();
    if (ast) {
      clang::RecordDecl *record_decl = TypeSystemClang::GetAsRecordDecl(m_type);

      if (record_decl) {
        auto attrs = record_decl->attrs();
        for (const auto &attr : attrs) {
          if (attr->getKind() == clang::attr::Packed) {
            m_packed = true;
            break;
          }
        }
      }
    }

    LLDB_LOG(m_log, LOG_PREFIX "{0} struct",
             m_packed ? "packed" : "not packed");

    for (uint32_t i = 0; i < n; i++) {
      std::string name;
      uint32_t size;
      (void)GetChildType(i, name, size);
      // NOTE: the offset returned by GetChildCompilerTypeAtIndex()
      //       can't be used because it never considers alignment bytes
      //       between struct fields.
      LLDB_LOG(m_log, LOG_PREFIX "field={0}, size={1}", name, size);
      if (!ExtractField(size))
        return ValueObjectSP();
    }

    return BuildValueObject();
  }

  // extract 'size' bytes at 'offs' from GPRs
  bool ExtractFromRegs(int32_t offs, uint32_t size, void *buf) {
    while (size) {
      auto reg = GetGPRByOffs(offs);
      if (!reg.IsValid())
        return false;

      uint32_t n = std::min(reg.Avail(), size);
      uint64_t raw_data;

      if (!reg.GetRawData(raw_data))
        return false;

      memcpy(buf, (char *)&raw_data + reg.Offs(), n);
      offs += n;
      size -= n;
      buf = (char *)buf + n;
    }
    return true;
  }

  // extract one field from GPRs and put it in our buffer
  bool ExtractField(uint32_t size) {
    auto reg = GetGPRByOffs(m_src_offs);
    if (!reg.IsValid())
      return false;

    // handle padding
    if (!m_packed) {
      uint32_t n = m_src_offs % size;

      // not 'size' bytes aligned
      if (n) {
        LLDB_LOG(m_log,
                 LOG_PREFIX "Extracting {0} alignment bytes at offset {1}", n,
                 m_src_offs);
        // get alignment bytes
        if (!ExtractFromRegs(m_src_offs, n, m_data_up->GetBytes() + m_dst_offs))
          return false;
        m_src_offs += n;
        m_dst_offs += n;
      }
    }

    // get field
    LLDB_LOG(m_log, LOG_PREFIX "Extracting {0} field bytes at offset {1}", size,
             m_src_offs);
    if (!ExtractFromRegs(m_src_offs, size, m_data_up->GetBytes() + m_dst_offs))
      return false;
    m_src_offs += size;
    m_dst_offs += size;
    return true;
  }

  // get child
  llvm::Expected<CompilerType> GetChildType(uint32_t i, std::string &name,
                                            uint32_t &size) {
    // GetChild constant inputs
    const bool transparent_pointers = false;
    const bool omit_empty_base_classes = true;
    const bool ignore_array_bounds = false;
    // GetChild output params
    int32_t child_offs;
    uint32_t child_bitfield_bit_size;
    uint32_t child_bitfield_bit_offset;
    bool child_is_base_class;
    bool child_is_deref_of_parent;
    ValueObject *valobj = nullptr;
    uint64_t language_flags;
    ExecutionContext exe_ctx;
    m_thread.CalculateExecutionContext(exe_ctx);

    return m_type.GetChildCompilerTypeAtIndex(
        &exe_ctx, i, transparent_pointers, omit_empty_base_classes,
        ignore_array_bounds, name, size, child_offs, child_bitfield_bit_size,
        child_bitfield_bit_offset, child_is_base_class,
        child_is_deref_of_parent, valobj, language_flags);
  }
};

#undef LOG_PREFIX

} // anonymous namespace

ValueObjectSP
ABISysV_ppc64::GetReturnValueObjectSimple(Thread &thread,
                                          CompilerType &type) const {
  if (!type)
    return ValueObjectSP();

  auto exp_extractor = ReturnValueExtractor::Create(thread, type);
  if (!exp_extractor) {
    Log *log = GetLog(LLDBLog::Expressions);
    LLDB_LOG_ERROR(log, exp_extractor.takeError(),
                   "Extracting return value failed: {0}");
    return ValueObjectSP();
  }

  return exp_extractor.get().GetValue();
}

ValueObjectSP ABISysV_ppc64::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  return GetReturnValueObjectSimple(thread, return_compiler_type);
}

bool ABISysV_ppc64::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t lr_reg_num;
  uint32_t sp_reg_num;
  uint32_t pc_reg_num;

  if (GetByteOrder() == lldb::eByteOrderLittle) {
    lr_reg_num = ppc64le_dwarf::dwarf_lr_ppc64le;
    sp_reg_num = ppc64le_dwarf::dwarf_r1_ppc64le;
    pc_reg_num = ppc64le_dwarf::dwarf_pc_ppc64le;
  } else {
    lr_reg_num = ppc64_dwarf::dwarf_lr_ppc64;
    sp_reg_num = ppc64_dwarf::dwarf_r1_ppc64;
    pc_reg_num = ppc64_dwarf::dwarf_pc_ppc64;
  }

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our Call Frame Address is the stack pointer value
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 0);

  // The previous PC is in the LR
  row->SetRegisterLocationToRegister(pc_reg_num, lr_reg_num, true);
  unwind_plan.AppendRow(row);

  // All other registers are the same.

  unwind_plan.SetSourceName("ppc64 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);

  return true;
}

bool ABISysV_ppc64::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num;
  uint32_t pc_reg_num;
  uint32_t cr_reg_num;

  if (GetByteOrder() == lldb::eByteOrderLittle) {
    sp_reg_num = ppc64le_dwarf::dwarf_r1_ppc64le;
    pc_reg_num = ppc64le_dwarf::dwarf_lr_ppc64le;
    cr_reg_num = ppc64le_dwarf::dwarf_cr_ppc64le;
  } else {
    sp_reg_num = ppc64_dwarf::dwarf_r1_ppc64;
    pc_reg_num = ppc64_dwarf::dwarf_lr_ppc64;
    cr_reg_num = ppc64_dwarf::dwarf_cr_ppc64;
  }

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  const int32_t ptr_size = 8;
  row->SetUnspecifiedRegistersAreUndefined(true);
  row->GetCFAValue().SetIsRegisterDereferenced(sp_reg_num);

  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, ptr_size * 2, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);
  row->SetRegisterLocationToAtCFAPlusOffset(cr_reg_num, ptr_size, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("ppc64 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetUnwindPlanForSignalTrap(eLazyBoolNo);
  unwind_plan.SetReturnAddressRegister(pc_reg_num);
  return true;
}

bool ABISysV_ppc64::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

// See "Register Usage" in the
// "System V Application Binary Interface"
// "64-bit PowerPC ELF Application Binary Interface Supplement" current version
// is 2 released 2015 at
// https://members.openpowerfoundation.org/document/dl/576
bool ABISysV_ppc64::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (reg_info) {
    // Preserved registers are :
    //    r1,r2,r13-r31
    //    cr2-cr4 (partially preserved)
    //    f14-f31 (not yet)
    //    v20-v31 (not yet)
    //    vrsave (not yet)

    const char *name = reg_info->name;
    if (name[0] == 'r') {
      if ((name[1] == '1' || name[1] == '2') && name[2] == '\0')
        return true;
      if (name[1] == '1' && name[2] > '2')
        return true;
      if ((name[1] == '2' || name[1] == '3') && name[2] != '\0')
        return true;
    }

    if (name[0] == 'f' && name[1] >= '0' && name[2] <= '9') {
      if (name[2] == '\0')
        return false;
      if (name[1] == '1' && name[2] >= '4')
        return true;
      if ((name[1] == '2' || name[1] == '3') && name[2] != '\0')
        return true;
    }

    if (name[0] == 's' && name[1] == 'p' && name[2] == '\0') // sp
      return true;
    if (name[0] == 'f' && name[1] == 'p' && name[2] == '\0') // fp
      return false;
    if (name[0] == 'p' && name[1] == 'c' && name[2] == '\0') // pc
      return true;
  }
  return false;
}

void ABISysV_ppc64::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for ppc64 targets", CreateInstance);
}

void ABISysV_ppc64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
