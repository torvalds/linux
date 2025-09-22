#include "PECallFrameInfo.h"

#include "ObjectFilePECOFF.h"

#include "Plugins/Process/Utility/lldb-x86-register-enums.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "llvm/Support/Win64EH.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm::Win64EH;

template <typename T>
static const T *TypedRead(const DataExtractor &data_extractor, offset_t &offset,
                          offset_t size = sizeof(T)) {
  return static_cast<const T *>(data_extractor.GetData(&offset, size));
}

struct EHInstruction {
  enum class Type {
    PUSH_REGISTER,
    ALLOCATE,
    SET_FRAME_POINTER_REGISTER,
    SAVE_REGISTER
  };

  uint8_t offset;
  Type type;
  uint32_t reg;
  uint32_t frame_offset;
};

using EHProgram = std::vector<EHInstruction>;

class UnwindCodesIterator {
public:
  UnwindCodesIterator(ObjectFilePECOFF &object_file, uint32_t unwind_info_rva);

  bool GetNext();
  bool IsError() const { return m_error; }

  const UnwindInfo *GetUnwindInfo() const { return m_unwind_info; }
  const UnwindCode *GetUnwindCode() const { return m_unwind_code; }
  bool IsChained() const { return m_chained; }

private:
  ObjectFilePECOFF &m_object_file;

  bool m_error = false;

  uint32_t m_unwind_info_rva;
  DataExtractor m_unwind_info_data;
  const UnwindInfo *m_unwind_info = nullptr;

  DataExtractor m_unwind_code_data;
  offset_t m_unwind_code_offset;
  const UnwindCode *m_unwind_code = nullptr;

  bool m_chained = false;
};

UnwindCodesIterator::UnwindCodesIterator(ObjectFilePECOFF &object_file,
                                         uint32_t unwind_info_rva)
    : m_object_file(object_file),
      m_unwind_info_rva(unwind_info_rva), m_unwind_code_offset{} {}

bool UnwindCodesIterator::GetNext() {
  static constexpr int UNWIND_INFO_SIZE = 4;

  m_error = false;
  m_unwind_code = nullptr;
  while (!m_unwind_code) {
    if (!m_unwind_info) {
      m_unwind_info_data =
          m_object_file.ReadImageDataByRVA(m_unwind_info_rva, UNWIND_INFO_SIZE);

      offset_t offset = 0;
      m_unwind_info =
          TypedRead<UnwindInfo>(m_unwind_info_data, offset, UNWIND_INFO_SIZE);
      if (!m_unwind_info) {
        m_error = true;
        break;
      }

      m_unwind_code_data = m_object_file.ReadImageDataByRVA(
          m_unwind_info_rva + UNWIND_INFO_SIZE,
          m_unwind_info->NumCodes * sizeof(UnwindCode));
      m_unwind_code_offset = 0;
    }

    if (m_unwind_code_offset < m_unwind_code_data.GetByteSize()) {
      m_unwind_code =
          TypedRead<UnwindCode>(m_unwind_code_data, m_unwind_code_offset);
      m_error = !m_unwind_code;
      break;
    }

    if (!(m_unwind_info->getFlags() & UNW_ChainInfo))
      break;

    uint32_t runtime_function_rva =
        m_unwind_info_rva + UNWIND_INFO_SIZE +
        ((m_unwind_info->NumCodes + 1) & ~1) * sizeof(UnwindCode);
    DataExtractor runtime_function_data = m_object_file.ReadImageDataByRVA(
        runtime_function_rva, sizeof(RuntimeFunction));

    offset_t offset = 0;
    const auto *runtime_function =
        TypedRead<RuntimeFunction>(runtime_function_data, offset);
    if (!runtime_function) {
      m_error = true;
      break;
    }

    m_unwind_info_rva = runtime_function->UnwindInfoOffset;
    m_unwind_info = nullptr;
    m_chained = true;
  }

  return !!m_unwind_code;
}

class EHProgramBuilder {
public:
  EHProgramBuilder(ObjectFilePECOFF &object_file, uint32_t unwind_info_rva);

  bool Build();

  const EHProgram &GetProgram() const { return m_program; }

private:
  static uint32_t ConvertMachineToLLDBRegister(uint8_t machine_reg);
  static uint32_t ConvertXMMToLLDBRegister(uint8_t xmm_reg);

  bool ProcessUnwindCode(UnwindCode code);
  void Finalize();

  bool ParseBigOrScaledFrameOffset(uint32_t &result, bool big, uint32_t scale);
  bool ParseBigFrameOffset(uint32_t &result);
  bool ParseFrameOffset(uint32_t &result);

  UnwindCodesIterator m_iterator;
  EHProgram m_program;
};

EHProgramBuilder::EHProgramBuilder(ObjectFilePECOFF &object_file,
                                   uint32_t unwind_info_rva)
    : m_iterator(object_file, unwind_info_rva) {}

bool EHProgramBuilder::Build() {
  while (m_iterator.GetNext())
    if (!ProcessUnwindCode(*m_iterator.GetUnwindCode()))
      return false;

  if (m_iterator.IsError())
    return false;

  Finalize();

  return true;
}

uint32_t EHProgramBuilder::ConvertMachineToLLDBRegister(uint8_t machine_reg) {
  static uint32_t machine_to_lldb_register[] = {
      lldb_rax_x86_64, lldb_rcx_x86_64, lldb_rdx_x86_64, lldb_rbx_x86_64,
      lldb_rsp_x86_64, lldb_rbp_x86_64, lldb_rsi_x86_64, lldb_rdi_x86_64,
      lldb_r8_x86_64,  lldb_r9_x86_64,  lldb_r10_x86_64, lldb_r11_x86_64,
      lldb_r12_x86_64, lldb_r13_x86_64, lldb_r14_x86_64, lldb_r15_x86_64};

  if (machine_reg >= std::size(machine_to_lldb_register))
    return LLDB_INVALID_REGNUM;

  return machine_to_lldb_register[machine_reg];
}

uint32_t EHProgramBuilder::ConvertXMMToLLDBRegister(uint8_t xmm_reg) {
  static uint32_t xmm_to_lldb_register[] = {
      lldb_xmm0_x86_64,  lldb_xmm1_x86_64,  lldb_xmm2_x86_64,
      lldb_xmm3_x86_64,  lldb_xmm4_x86_64,  lldb_xmm5_x86_64,
      lldb_xmm6_x86_64,  lldb_xmm7_x86_64,  lldb_xmm8_x86_64,
      lldb_xmm9_x86_64,  lldb_xmm10_x86_64, lldb_xmm11_x86_64,
      lldb_xmm12_x86_64, lldb_xmm13_x86_64, lldb_xmm14_x86_64,
      lldb_xmm15_x86_64};

  if (xmm_reg >= std::size(xmm_to_lldb_register))
    return LLDB_INVALID_REGNUM;

  return xmm_to_lldb_register[xmm_reg];
}

bool EHProgramBuilder::ProcessUnwindCode(UnwindCode code) {
  uint8_t o = m_iterator.IsChained() ? 0 : code.u.CodeOffset;
  uint8_t unwind_operation = code.getUnwindOp();
  uint8_t operation_info = code.getOpInfo();

  switch (unwind_operation) {
  case UOP_PushNonVol: {
    uint32_t r = ConvertMachineToLLDBRegister(operation_info);
    if (r == LLDB_INVALID_REGNUM)
      return false;

    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::PUSH_REGISTER, r, 8});

    return true;
  }
  case UOP_AllocLarge: {
    uint32_t fo;
    if (!ParseBigOrScaledFrameOffset(fo, operation_info, 8))
      return false;

    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::ALLOCATE,
                                         LLDB_INVALID_REGNUM, fo});

    return true;
  }
  case UOP_AllocSmall: {
    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::ALLOCATE, LLDB_INVALID_REGNUM,
                      static_cast<uint32_t>(operation_info) * 8 + 8});
    return true;
  }
  case UOP_SetFPReg: {
    uint32_t fpr = LLDB_INVALID_REGNUM;
    if (m_iterator.GetUnwindInfo()->getFrameRegister())
      fpr = ConvertMachineToLLDBRegister(
          m_iterator.GetUnwindInfo()->getFrameRegister());
    if (fpr == LLDB_INVALID_REGNUM)
      return false;

    uint32_t fpro =
        static_cast<uint32_t>(m_iterator.GetUnwindInfo()->getFrameOffset()) *
        16;

    m_program.emplace_back(EHInstruction{
        o, EHInstruction::Type::SET_FRAME_POINTER_REGISTER, fpr, fpro});

    return true;
  }
  case UOP_SaveNonVol:
  case UOP_SaveNonVolBig: {
    uint32_t r = ConvertMachineToLLDBRegister(operation_info);
    if (r == LLDB_INVALID_REGNUM)
      return false;

    uint32_t fo;
    if (!ParseBigOrScaledFrameOffset(fo, unwind_operation == UOP_SaveNonVolBig,
                                     8))
      return false;

    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::SAVE_REGISTER, r, fo});

    return true;
  }
  case UOP_Epilog: {
    return m_iterator.GetNext();
  }
  case UOP_SpareCode: {
    // ReSharper disable once CppIdenticalOperandsInBinaryExpression
    return m_iterator.GetNext() && m_iterator.GetNext();
  }
  case UOP_SaveXMM128:
  case UOP_SaveXMM128Big: {
    uint32_t r = ConvertXMMToLLDBRegister(operation_info);
    if (r == LLDB_INVALID_REGNUM)
      return false;

    uint32_t fo;
    if (!ParseBigOrScaledFrameOffset(fo, unwind_operation == UOP_SaveXMM128Big,
                                     16))
      return false;

    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::SAVE_REGISTER, r, fo});

    return true;
  }
  case UOP_PushMachFrame: {
    if (operation_info)
      m_program.emplace_back(EHInstruction{o, EHInstruction::Type::ALLOCATE,
                                           LLDB_INVALID_REGNUM, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_rip_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_cs_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_rflags_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_rsp_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_ss_x86_64, 8});

    return true;
  }
  default:
    return false;
  }
}

void EHProgramBuilder::Finalize() {
  for (const EHInstruction &i : m_program)
    if (i.reg == lldb_rip_x86_64)
      return;

  m_program.emplace_back(
      EHInstruction{0, EHInstruction::Type::PUSH_REGISTER, lldb_rip_x86_64, 8});
}

bool EHProgramBuilder::ParseBigOrScaledFrameOffset(uint32_t &result, bool big,
                                                   uint32_t scale) {
  if (big) {
    if (!ParseBigFrameOffset(result))
      return false;
  } else {
    if (!ParseFrameOffset(result))
      return false;

    result *= scale;
  }

  return true;
}

bool EHProgramBuilder::ParseBigFrameOffset(uint32_t &result) {
  if (!m_iterator.GetNext())
    return false;

  result = m_iterator.GetUnwindCode()->FrameOffset;

  if (!m_iterator.GetNext())
    return false;

  result += static_cast<uint32_t>(m_iterator.GetUnwindCode()->FrameOffset)
            << 16;

  return true;
}

bool EHProgramBuilder::ParseFrameOffset(uint32_t &result) {
  if (!m_iterator.GetNext())
    return false;

  result = m_iterator.GetUnwindCode()->FrameOffset;

  return true;
}

class EHProgramRange {
public:
  EHProgramRange(EHProgram::const_iterator begin,
                 EHProgram::const_iterator end);

  std::unique_ptr<UnwindPlan::Row> BuildUnwindPlanRow() const;

private:
  int32_t GetCFAFrameOffset() const;

  EHProgram::const_iterator m_begin;
  EHProgram::const_iterator m_end;
};

EHProgramRange::EHProgramRange(EHProgram::const_iterator begin,
                               EHProgram::const_iterator end)
    : m_begin(begin), m_end(end) {}

std::unique_ptr<UnwindPlan::Row> EHProgramRange::BuildUnwindPlanRow() const {
  std::unique_ptr<UnwindPlan::Row> row = std::make_unique<UnwindPlan::Row>();

  if (m_begin != m_end)
    row->SetOffset(m_begin->offset);

  int32_t cfa_frame_offset = GetCFAFrameOffset();

  bool frame_pointer_found = false;
  for (EHProgram::const_iterator it = m_begin; it != m_end; ++it) {
    switch (it->type) {
    case EHInstruction::Type::SET_FRAME_POINTER_REGISTER:
      row->GetCFAValue().SetIsRegisterPlusOffset(it->reg, cfa_frame_offset -
                                                              it->frame_offset);
      frame_pointer_found = true;
      break;
    default:
      break;
    }
    if (frame_pointer_found)
      break;
  }
  if (!frame_pointer_found)
    row->GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64,
                                               cfa_frame_offset);

  int32_t rsp_frame_offset = 0;
  for (EHProgram::const_iterator it = m_begin; it != m_end; ++it) {
    switch (it->type) {
    case EHInstruction::Type::PUSH_REGISTER:
      row->SetRegisterLocationToAtCFAPlusOffset(
          it->reg, rsp_frame_offset - cfa_frame_offset, false);
      rsp_frame_offset += it->frame_offset;
      break;
    case EHInstruction::Type::ALLOCATE:
      rsp_frame_offset += it->frame_offset;
      break;
    case EHInstruction::Type::SAVE_REGISTER:
      row->SetRegisterLocationToAtCFAPlusOffset(
          it->reg, it->frame_offset - cfa_frame_offset, false);
      break;
    default:
      break;
    }
  }

  row->SetRegisterLocationToIsCFAPlusOffset(lldb_rsp_x86_64, 0, false);

  return row;
}

int32_t EHProgramRange::GetCFAFrameOffset() const {
  int32_t result = 0;

  for (EHProgram::const_iterator it = m_begin; it != m_end; ++it) {
    switch (it->type) {
    case EHInstruction::Type::PUSH_REGISTER:
    case EHInstruction::Type::ALLOCATE:
      result += it->frame_offset;
      break;
    default:
      break;
    }
  }

  return result;
}

PECallFrameInfo::PECallFrameInfo(ObjectFilePECOFF &object_file,
                                 uint32_t exception_dir_rva,
                                 uint32_t exception_dir_size)
    : m_object_file(object_file),
      m_exception_dir(object_file.ReadImageDataByRVA(exception_dir_rva,
                                                      exception_dir_size)) {}

bool PECallFrameInfo::GetAddressRange(Address addr, AddressRange &range) {
  range.Clear();

  const RuntimeFunction *runtime_function =
      FindRuntimeFunctionIntersectsWithRange(AddressRange(addr, 1));
  if (!runtime_function)
    return false;

  range.GetBaseAddress() =
      m_object_file.GetAddress(runtime_function->StartAddress);
  range.SetByteSize(runtime_function->EndAddress -
                    runtime_function->StartAddress);

  return true;
}

bool PECallFrameInfo::GetUnwindPlan(const Address &addr,
                                    UnwindPlan &unwind_plan) {
  return GetUnwindPlan(AddressRange(addr, 1), unwind_plan);
}

bool PECallFrameInfo::GetUnwindPlan(const AddressRange &range,
                                    UnwindPlan &unwind_plan) {
  unwind_plan.Clear();

  unwind_plan.SetSourceName("PE EH info");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolYes);
  unwind_plan.SetRegisterKind(eRegisterKindLLDB);

  const RuntimeFunction *runtime_function =
      FindRuntimeFunctionIntersectsWithRange(range);
  if (!runtime_function)
    return false;

  EHProgramBuilder builder(m_object_file, runtime_function->UnwindInfoOffset);
  if (!builder.Build())
    return false;

  std::vector<UnwindPlan::RowSP> rows;

  uint32_t last_offset = UINT32_MAX;
  for (auto it = builder.GetProgram().begin(); it != builder.GetProgram().end();
       ++it) {
    if (it->offset == last_offset)
      continue;

    EHProgramRange program_range =
        EHProgramRange(it, builder.GetProgram().end());
    rows.push_back(program_range.BuildUnwindPlanRow());

    last_offset = it->offset;
  }

  for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    unwind_plan.AppendRow(*it);

  unwind_plan.SetPlanValidAddressRange(AddressRange(
      m_object_file.GetAddress(runtime_function->StartAddress),
      runtime_function->EndAddress - runtime_function->StartAddress));
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);

  return true;
}

const RuntimeFunction *PECallFrameInfo::FindRuntimeFunctionIntersectsWithRange(
    const AddressRange &range) const {
  uint32_t rva = m_object_file.GetRVA(range.GetBaseAddress());
  addr_t size = range.GetByteSize();

  uint32_t begin = 0;
  uint32_t end = m_exception_dir.GetByteSize() / sizeof(RuntimeFunction);
  while (begin < end) {
    uint32_t curr = (begin + end) / 2;

    offset_t offset = curr * sizeof(RuntimeFunction);
    const auto *runtime_function =
        TypedRead<RuntimeFunction>(m_exception_dir, offset);
    if (!runtime_function)
      break;

    if (runtime_function->StartAddress < rva + size &&
        runtime_function->EndAddress > rva)
      return runtime_function;

    if (runtime_function->StartAddress >= rva + size)
      end = curr;

    if (runtime_function->EndAddress <= rva)
      begin = curr + 1;
  }

  return nullptr;
}
