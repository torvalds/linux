//===-- DynamicRegisterInfo.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/DynamicRegisterInfo.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StringExtractor.h"
#include "lldb/Utility/StructuredData.h"

using namespace lldb;
using namespace lldb_private;

std::unique_ptr<DynamicRegisterInfo>
DynamicRegisterInfo::Create(const StructuredData::Dictionary &dict,
                            const ArchSpec &arch) {
  auto dyn_reg_info = std::make_unique<DynamicRegisterInfo>();
  if (!dyn_reg_info)
    return nullptr;

  if (dyn_reg_info->SetRegisterInfo(dict, arch) == 0)
    return nullptr;

  return dyn_reg_info;
}

DynamicRegisterInfo::DynamicRegisterInfo(DynamicRegisterInfo &&info) {
  MoveFrom(std::move(info));
}

DynamicRegisterInfo &
DynamicRegisterInfo::operator=(DynamicRegisterInfo &&info) {
  MoveFrom(std::move(info));
  return *this;
}

void DynamicRegisterInfo::MoveFrom(DynamicRegisterInfo &&info) {
  m_regs = std::move(info.m_regs);
  m_sets = std::move(info.m_sets);
  m_set_reg_nums = std::move(info.m_set_reg_nums);
  m_set_names = std::move(info.m_set_names);
  m_value_regs_map = std::move(info.m_value_regs_map);
  m_invalidate_regs_map = std::move(info.m_invalidate_regs_map);

  m_reg_data_byte_size = info.m_reg_data_byte_size;
  m_finalized = info.m_finalized;

  if (m_finalized) {
    const size_t num_sets = m_sets.size();
    for (size_t set = 0; set < num_sets; ++set)
      m_sets[set].registers = m_set_reg_nums[set].data();
  }

  info.Clear();
}

llvm::Expected<uint32_t> DynamicRegisterInfo::ByteOffsetFromSlice(
    uint32_t index, llvm::StringRef slice_str, lldb::ByteOrder byte_order) {
  // Slices use the following format:
  //  REGNAME[MSBIT:LSBIT]
  // REGNAME - name of the register to grab a slice of
  // MSBIT - the most significant bit at which the current register value
  // starts at
  // LSBIT - the least significant bit at which the current register value
  // ends at
  static llvm::Regex g_bitfield_regex(
      "([A-Za-z_][A-Za-z0-9_]*)\\[([0-9]+):([0-9]+)\\]");
  llvm::SmallVector<llvm::StringRef, 4> matches;
  if (!g_bitfield_regex.match(slice_str, &matches))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "failed to match against register bitfield regex (slice: %s)",
        slice_str.str().c_str());

  llvm::StringRef reg_name_str = matches[1];
  llvm::StringRef msbit_str = matches[2];
  llvm::StringRef lsbit_str = matches[3];
  uint32_t msbit;
  uint32_t lsbit;
  if (!llvm::to_integer(msbit_str, msbit) ||
      !llvm::to_integer(lsbit_str, lsbit))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(), "msbit (%s) or lsbit (%s) are invalid",
        msbit_str.str().c_str(), lsbit_str.str().c_str());

  if (msbit <= lsbit)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "msbit (%u) must be greater than lsbit (%u)",
                                   msbit, lsbit);

  const uint32_t msbyte = msbit / 8;
  const uint32_t lsbyte = lsbit / 8;

  const RegisterInfo *containing_reg_info = GetRegisterInfo(reg_name_str);
  if (!containing_reg_info)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid concrete register \"%s\"",
                                   reg_name_str.str().c_str());

  const uint32_t max_bit = containing_reg_info->byte_size * 8;

  if (msbit > max_bit)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "msbit (%u) must be less than the bitsize of the register \"%s\" (%u)",
        msbit, reg_name_str.str().c_str(), max_bit);
  if (lsbit > max_bit)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "lsbit (%u) must be less than the bitsize of the register \"%s\" (%u)",
        lsbit, reg_name_str.str().c_str(), max_bit);

  m_invalidate_regs_map[containing_reg_info->kinds[eRegisterKindLLDB]]
      .push_back(index);
  m_value_regs_map[index].push_back(
      containing_reg_info->kinds[eRegisterKindLLDB]);
  m_invalidate_regs_map[index].push_back(
      containing_reg_info->kinds[eRegisterKindLLDB]);

  if (byte_order == eByteOrderLittle)
    return containing_reg_info->byte_offset + lsbyte;
  if (byte_order == eByteOrderBig)
    return containing_reg_info->byte_offset + msbyte;
  llvm_unreachable("Invalid byte order");
}

llvm::Expected<uint32_t> DynamicRegisterInfo::ByteOffsetFromComposite(
    uint32_t index, StructuredData::Array &composite_reg_list,
    lldb::ByteOrder byte_order) {
  const size_t num_composite_regs = composite_reg_list.GetSize();
  if (num_composite_regs == 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "\"composite\" list is empty");

  uint32_t composite_offset = UINT32_MAX;
  for (uint32_t composite_idx = 0; composite_idx < num_composite_regs;
       ++composite_idx) {
    std::optional<llvm::StringRef> maybe_composite_reg_name =
        composite_reg_list.GetItemAtIndexAsString(composite_idx);
    if (!maybe_composite_reg_name)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "\"composite\" list value is not a Python string at index %d",
          composite_idx);

    const RegisterInfo *composite_reg_info =
        GetRegisterInfo(*maybe_composite_reg_name);
    if (!composite_reg_info)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "failed to find composite register by name: \"%s\"",
          maybe_composite_reg_name->str().c_str());

    composite_offset =
        std::min(composite_offset, composite_reg_info->byte_offset);
    m_value_regs_map[index].push_back(
        composite_reg_info->kinds[eRegisterKindLLDB]);
    m_invalidate_regs_map[composite_reg_info->kinds[eRegisterKindLLDB]]
        .push_back(index);
    m_invalidate_regs_map[index].push_back(
        composite_reg_info->kinds[eRegisterKindLLDB]);
  }

  return composite_offset;
}

llvm::Expected<uint32_t> DynamicRegisterInfo::ByteOffsetFromRegInfoDict(
    uint32_t index, StructuredData::Dictionary &reg_info_dict,
    lldb::ByteOrder byte_order) {
  uint32_t byte_offset;
  if (reg_info_dict.GetValueForKeyAsInteger("offset", byte_offset))
    return byte_offset;

  // No offset for this register, see if the register has a value
  // expression which indicates this register is part of another register.
  // Value expressions are things like "rax[31:0]" which state that the
  // current register's value is in a concrete register "rax" in bits 31:0.
  // If there is a value expression we can calculate the offset
  llvm::StringRef slice_str;
  if (reg_info_dict.GetValueForKeyAsString("slice", slice_str, nullptr))
    return ByteOffsetFromSlice(index, slice_str, byte_order);

  StructuredData::Array *composite_reg_list;
  if (reg_info_dict.GetValueForKeyAsArray("composite", composite_reg_list))
    return ByteOffsetFromComposite(index, *composite_reg_list, byte_order);

  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "insufficient data to calculate byte offset");
}

size_t
DynamicRegisterInfo::SetRegisterInfo(const StructuredData::Dictionary &dict,
                                     const ArchSpec &arch) {
  Log *log = GetLog(LLDBLog::Object);
  assert(!m_finalized);
  StructuredData::Array *sets = nullptr;
  if (dict.GetValueForKeyAsArray("sets", sets)) {
    const uint32_t num_sets = sets->GetSize();
    for (uint32_t i = 0; i < num_sets; ++i) {
      std::optional<llvm::StringRef> maybe_set_name =
          sets->GetItemAtIndexAsString(i);
      if (maybe_set_name && !maybe_set_name->empty()) {
        m_sets.push_back(
            {ConstString(*maybe_set_name).AsCString(), nullptr, 0, nullptr});
      } else {
        Clear();
        printf("error: register sets must have valid names\n");
        return 0;
      }
    }
    m_set_reg_nums.resize(m_sets.size());
  }

  StructuredData::Array *regs = nullptr;
  if (!dict.GetValueForKeyAsArray("registers", regs))
    return 0;

  const ByteOrder byte_order = arch.GetByteOrder();

  const uint32_t num_regs = regs->GetSize();
  //        typedef std::map<std::string, std::vector<std::string> >
  //        InvalidateNameMap;
  //        InvalidateNameMap invalidate_map;
  for (uint32_t i = 0; i < num_regs; ++i) {
    std::optional<StructuredData::Dictionary *> maybe_reg_info_dict =
        regs->GetItemAtIndexAsDictionary(i);
    if (!maybe_reg_info_dict) {
      Clear();
      printf("error: items in the 'registers' array must be dictionaries\n");
      regs->DumpToStdout();
      return 0;
    }
    StructuredData::Dictionary *reg_info_dict = *maybe_reg_info_dict;

    // { 'name':'rcx'       , 'bitsize' :  64, 'offset' :  16,
    // 'encoding':'uint' , 'format':'hex'         , 'set': 0, 'ehframe' : 2,
    // 'dwarf' : 2, 'generic':'arg4', 'alt-name':'arg4', },
    RegisterInfo reg_info;
    std::vector<uint32_t> value_regs;
    std::vector<uint32_t> invalidate_regs;
    memset(&reg_info, 0, sizeof(reg_info));

    llvm::StringRef name_val;
    if (!reg_info_dict->GetValueForKeyAsString("name", name_val)) {
      Clear();
      printf("error: registers must have valid names and offsets\n");
      reg_info_dict->DumpToStdout();
      return 0;
    }
    reg_info.name = ConstString(name_val).GetCString();

    llvm::StringRef alt_name_val;
    if (reg_info_dict->GetValueForKeyAsString("alt-name", alt_name_val))
      reg_info.alt_name = ConstString(alt_name_val).GetCString();
    else
      reg_info.alt_name = nullptr;

    llvm::Expected<uint32_t> byte_offset =
        ByteOffsetFromRegInfoDict(i, *reg_info_dict, byte_order);
    if (byte_offset)
      reg_info.byte_offset = byte_offset.get();
    else {
      LLDB_LOG_ERROR(log, byte_offset.takeError(),
                     "error while parsing register {1}: {0}", reg_info.name);
      Clear();
      reg_info_dict->DumpToStdout();
      return 0;
    }

    uint64_t bitsize = 0;
    if (!reg_info_dict->GetValueForKeyAsInteger("bitsize", bitsize)) {
      Clear();
      printf("error: invalid or missing 'bitsize' key/value pair in register "
             "dictionary\n");
      reg_info_dict->DumpToStdout();
      return 0;
    }

    reg_info.byte_size = bitsize / 8;

    llvm::StringRef format_str;
    if (reg_info_dict->GetValueForKeyAsString("format", format_str, nullptr)) {
      if (OptionArgParser::ToFormat(format_str.str().c_str(), reg_info.format,
                                    nullptr)
              .Fail()) {
        Clear();
        printf("error: invalid 'format' value in register dictionary\n");
        reg_info_dict->DumpToStdout();
        return 0;
      }
    } else {
      reg_info_dict->GetValueForKeyAsInteger("format", reg_info.format,
                                             eFormatHex);
    }

    llvm::StringRef encoding_str;
    if (reg_info_dict->GetValueForKeyAsString("encoding", encoding_str))
      reg_info.encoding = Args::StringToEncoding(encoding_str, eEncodingUint);
    else
      reg_info_dict->GetValueForKeyAsInteger("encoding", reg_info.encoding,
                                             eEncodingUint);

    size_t set = 0;
    if (!reg_info_dict->GetValueForKeyAsInteger("set", set) ||
        set >= m_sets.size()) {
      Clear();
      printf("error: invalid 'set' value in register dictionary, valid values "
             "are 0 - %i\n",
             (int)set);
      reg_info_dict->DumpToStdout();
      return 0;
    }

    // Fill in the register numbers
    reg_info.kinds[lldb::eRegisterKindLLDB] = i;
    reg_info.kinds[lldb::eRegisterKindProcessPlugin] = i;
    uint32_t eh_frame_regno = LLDB_INVALID_REGNUM;
    reg_info_dict->GetValueForKeyAsInteger("gcc", eh_frame_regno,
                                           LLDB_INVALID_REGNUM);
    if (eh_frame_regno == LLDB_INVALID_REGNUM)
      reg_info_dict->GetValueForKeyAsInteger("ehframe", eh_frame_regno,
                                             LLDB_INVALID_REGNUM);
    reg_info.kinds[lldb::eRegisterKindEHFrame] = eh_frame_regno;
    reg_info_dict->GetValueForKeyAsInteger(
        "dwarf", reg_info.kinds[lldb::eRegisterKindDWARF], LLDB_INVALID_REGNUM);
    llvm::StringRef generic_str;
    if (reg_info_dict->GetValueForKeyAsString("generic", generic_str))
      reg_info.kinds[lldb::eRegisterKindGeneric] =
          Args::StringToGenericRegister(generic_str);
    else
      reg_info_dict->GetValueForKeyAsInteger(
          "generic", reg_info.kinds[lldb::eRegisterKindGeneric],
          LLDB_INVALID_REGNUM);

    // Check if this register invalidates any other register values when it is
    // modified
    StructuredData::Array *invalidate_reg_list = nullptr;
    if (reg_info_dict->GetValueForKeyAsArray("invalidate-regs",
                                             invalidate_reg_list)) {
      const size_t num_regs = invalidate_reg_list->GetSize();
      if (num_regs > 0) {
        for (uint32_t idx = 0; idx < num_regs; ++idx) {
          if (auto maybe_invalidate_reg_name =
                  invalidate_reg_list->GetItemAtIndexAsString(idx)) {
            const RegisterInfo *invalidate_reg_info =
                GetRegisterInfo(*maybe_invalidate_reg_name);
            if (invalidate_reg_info) {
              m_invalidate_regs_map[i].push_back(
                  invalidate_reg_info->kinds[eRegisterKindLLDB]);
            } else {
              // TODO: print error invalid slice string that doesn't follow the
              // format
              printf("error: failed to find a 'invalidate-regs' register for "
                     "\"%s\" while parsing register \"%s\"\n",
                     maybe_invalidate_reg_name->str().c_str(), reg_info.name);
            }
          } else if (auto maybe_invalidate_reg_num =
                         invalidate_reg_list->GetItemAtIndexAsInteger<uint64_t>(
                             idx)) {
            if (*maybe_invalidate_reg_num != UINT64_MAX)
              m_invalidate_regs_map[i].push_back(*maybe_invalidate_reg_num);
            else
              printf("error: 'invalidate-regs' list value wasn't a valid "
                     "integer\n");
          } else {
            printf("error: 'invalidate-regs' list value wasn't a python string "
                   "or integer\n");
          }
        }
      } else {
        printf("error: 'invalidate-regs' contained an empty list\n");
      }
    }

    // Calculate the register offset
    const size_t end_reg_offset = reg_info.byte_offset + reg_info.byte_size;
    if (m_reg_data_byte_size < end_reg_offset)
      m_reg_data_byte_size = end_reg_offset;

    m_regs.push_back(reg_info);
    m_set_reg_nums[set].push_back(i);
  }
  Finalize(arch);
  return m_regs.size();
}

size_t DynamicRegisterInfo::SetRegisterInfo(
    std::vector<DynamicRegisterInfo::Register> &&regs,
    const ArchSpec &arch) {
  assert(!m_finalized);

  for (auto it : llvm::enumerate(regs)) {
    uint32_t local_regnum = it.index();
    const DynamicRegisterInfo::Register &reg = it.value();

    assert(reg.name);
    assert(reg.set_name);

    if (!reg.value_regs.empty())
      m_value_regs_map[local_regnum] = std::move(reg.value_regs);
    if (!reg.invalidate_regs.empty())
      m_invalidate_regs_map[local_regnum] = std::move(reg.invalidate_regs);
    if (reg.value_reg_offset != 0) {
      assert(reg.value_regs.size() == 1);
      m_value_reg_offset_map[local_regnum] = reg.value_reg_offset;
    }

    struct RegisterInfo reg_info {
      reg.name.AsCString(), reg.alt_name.AsCString(), reg.byte_size,
          reg.byte_offset, reg.encoding, reg.format,
          {reg.regnum_ehframe, reg.regnum_dwarf, reg.regnum_generic,
           reg.regnum_remote, local_regnum},
          // value_regs and invalidate_regs are filled by Finalize()
          nullptr, nullptr, reg.flags_type
    };

    m_regs.push_back(reg_info);

    uint32_t set = GetRegisterSetIndexByName(reg.set_name, true);
    assert(set < m_sets.size());
    assert(set < m_set_reg_nums.size());
    assert(set < m_set_names.size());
    m_set_reg_nums[set].push_back(local_regnum);
  };

  Finalize(arch);
  return m_regs.size();
}

void DynamicRegisterInfo::Finalize(const ArchSpec &arch) {
  if (m_finalized)
    return;

  m_finalized = true;
  const size_t num_sets = m_sets.size();
  for (size_t set = 0; set < num_sets; ++set) {
    assert(m_sets.size() == m_set_reg_nums.size());
    m_sets[set].num_registers = m_set_reg_nums[set].size();
    m_sets[set].registers = m_set_reg_nums[set].data();
  }

  // make sure value_regs are terminated with LLDB_INVALID_REGNUM

  for (reg_to_regs_map::iterator pos = m_value_regs_map.begin(),
                                 end = m_value_regs_map.end();
       pos != end; ++pos) {
    if (pos->second.back() != LLDB_INVALID_REGNUM)
      pos->second.push_back(LLDB_INVALID_REGNUM);
  }

  // Now update all value_regs with each register info as needed
  const size_t num_regs = m_regs.size();
  for (size_t i = 0; i < num_regs; ++i) {
    if (m_value_regs_map.find(i) != m_value_regs_map.end())
      m_regs[i].value_regs = m_value_regs_map[i].data();
    else
      m_regs[i].value_regs = nullptr;
  }

  // Expand all invalidation dependencies
  for (reg_to_regs_map::iterator pos = m_invalidate_regs_map.begin(),
                                 end = m_invalidate_regs_map.end();
       pos != end; ++pos) {
    const uint32_t reg_num = pos->first;

    if (m_regs[reg_num].value_regs) {
      reg_num_collection extra_invalid_regs;
      for (const uint32_t invalidate_reg_num : pos->second) {
        reg_to_regs_map::iterator invalidate_pos =
            m_invalidate_regs_map.find(invalidate_reg_num);
        if (invalidate_pos != m_invalidate_regs_map.end()) {
          for (const uint32_t concrete_invalidate_reg_num :
               invalidate_pos->second) {
            if (concrete_invalidate_reg_num != reg_num)
              extra_invalid_regs.push_back(concrete_invalidate_reg_num);
          }
        }
      }
      pos->second.insert(pos->second.end(), extra_invalid_regs.begin(),
                         extra_invalid_regs.end());
    }
  }

  // sort and unique all invalidate registers and make sure each is terminated
  // with LLDB_INVALID_REGNUM
  for (reg_to_regs_map::iterator pos = m_invalidate_regs_map.begin(),
                                 end = m_invalidate_regs_map.end();
       pos != end; ++pos) {
    if (pos->second.size() > 1) {
      llvm::sort(pos->second);
      reg_num_collection::iterator unique_end =
          std::unique(pos->second.begin(), pos->second.end());
      if (unique_end != pos->second.end())
        pos->second.erase(unique_end, pos->second.end());
    }
    assert(!pos->second.empty());
    if (pos->second.back() != LLDB_INVALID_REGNUM)
      pos->second.push_back(LLDB_INVALID_REGNUM);
  }

  // Now update all invalidate_regs with each register info as needed
  for (size_t i = 0; i < num_regs; ++i) {
    if (m_invalidate_regs_map.find(i) != m_invalidate_regs_map.end())
      m_regs[i].invalidate_regs = m_invalidate_regs_map[i].data();
    else
      m_regs[i].invalidate_regs = nullptr;
  }

  // Check if we need to automatically set the generic registers in case they
  // weren't set
  bool generic_regs_specified = false;
  for (const auto &reg : m_regs) {
    if (reg.kinds[eRegisterKindGeneric] != LLDB_INVALID_REGNUM) {
      generic_regs_specified = true;
      break;
    }
  }

  if (!generic_regs_specified) {
    switch (arch.GetMachine()) {
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_32:
    case llvm::Triple::aarch64_be:
      for (auto &reg : m_regs) {
        if (strcmp(reg.name, "pc") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "fp") == 0) ||
                 (strcmp(reg.name, "x29") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "lr") == 0) ||
                 (strcmp(reg.name, "x30") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
        else if ((strcmp(reg.name, "sp") == 0) ||
                 (strcmp(reg.name, "x31") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if (strcmp(reg.name, "cpsr") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
      for (auto &reg : m_regs) {
        if ((strcmp(reg.name, "pc") == 0) || (strcmp(reg.name, "r15") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "sp") == 0) ||
                 (strcmp(reg.name, "r13") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if ((strcmp(reg.name, "lr") == 0) ||
                 (strcmp(reg.name, "r14") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
        else if ((strcmp(reg.name, "r7") == 0) &&
                 arch.GetTriple().getVendor() == llvm::Triple::Apple)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "r11") == 0) &&
                 arch.GetTriple().getVendor() != llvm::Triple::Apple)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if (strcmp(reg.name, "fp") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if (strcmp(reg.name, "cpsr") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    case llvm::Triple::x86:
      for (auto &reg : m_regs) {
        if ((strcmp(reg.name, "eip") == 0) || (strcmp(reg.name, "pc") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "esp") == 0) ||
                 (strcmp(reg.name, "sp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if ((strcmp(reg.name, "ebp") == 0) ||
                 (strcmp(reg.name, "fp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "eflags") == 0) ||
                 (strcmp(reg.name, "flags") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    case llvm::Triple::x86_64:
      for (auto &reg : m_regs) {
        if ((strcmp(reg.name, "rip") == 0) || (strcmp(reg.name, "pc") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "rsp") == 0) ||
                 (strcmp(reg.name, "sp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if ((strcmp(reg.name, "rbp") == 0) ||
                 (strcmp(reg.name, "fp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "rflags") == 0) ||
                 (strcmp(reg.name, "eflags") == 0) ||
                 (strcmp(reg.name, "flags") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    default:
      break;
    }
  }

  // At this stage call ConfigureOffsets to calculate register offsets for
  // targets supporting dynamic offset calculation. It also calculates
  // total byte size of register data.
  ConfigureOffsets();

  // Check if register info is reconfigurable
  // AArch64 SVE register set has configurable register sizes, as does the ZA
  // register that SME added (the streaming state of SME reuses the SVE state).
  if (arch.GetTriple().isAArch64()) {
    for (const auto &reg : m_regs) {
      if ((strcmp(reg.name, "vg") == 0) || (strcmp(reg.name, "svg") == 0)) {
        m_is_reconfigurable = true;
        break;
      }
    }
  }
}

void DynamicRegisterInfo::ConfigureOffsets() {
  // We are going to create a map between remote (eRegisterKindProcessPlugin)
  // and local (eRegisterKindLLDB) register numbers. This map will give us
  // remote register numbers in increasing order for offset calculation.
  std::map<uint32_t, uint32_t> remote_to_local_regnum_map;
  for (const auto &reg : m_regs)
    remote_to_local_regnum_map[reg.kinds[eRegisterKindProcessPlugin]] =
        reg.kinds[eRegisterKindLLDB];

  // At this stage we manually calculate g/G packet offsets of all primary
  // registers, only if target XML or qRegisterInfo packet did not send
  // an offset explicitly.
  uint32_t reg_offset = 0;
  for (auto const &regnum_pair : remote_to_local_regnum_map) {
    if (m_regs[regnum_pair.second].byte_offset == LLDB_INVALID_INDEX32 &&
        m_regs[regnum_pair.second].value_regs == nullptr) {
      m_regs[regnum_pair.second].byte_offset = reg_offset;

      reg_offset = m_regs[regnum_pair.second].byte_offset +
                   m_regs[regnum_pair.second].byte_size;
    }
  }

  // Now update all value_regs with each register info as needed
  for (auto &reg : m_regs) {
    if (reg.value_regs != nullptr) {
      // Assign a valid offset to all pseudo registers that have only a single
      // parent register in value_regs list, if not assigned by stub.  Pseudo
      // registers with value_regs list populated will share same offset as
      // that of their corresponding parent register.
      if (reg.byte_offset == LLDB_INVALID_INDEX32) {
        uint32_t value_regnum = reg.value_regs[0];
        if (value_regnum != LLDB_INVALID_INDEX32 &&
            reg.value_regs[1] == LLDB_INVALID_INDEX32) {
          reg.byte_offset =
              GetRegisterInfoAtIndex(value_regnum)->byte_offset;
          auto it = m_value_reg_offset_map.find(reg.kinds[eRegisterKindLLDB]);
          if (it != m_value_reg_offset_map.end())
            reg.byte_offset += it->second;
        }
      }
    }

    reg_offset = reg.byte_offset + reg.byte_size;
    if (m_reg_data_byte_size < reg_offset)
      m_reg_data_byte_size = reg_offset;
  }
}

bool DynamicRegisterInfo::IsReconfigurable() { return m_is_reconfigurable; }

size_t DynamicRegisterInfo::GetNumRegisters() const { return m_regs.size(); }

size_t DynamicRegisterInfo::GetNumRegisterSets() const { return m_sets.size(); }

size_t DynamicRegisterInfo::GetRegisterDataByteSize() const {
  return m_reg_data_byte_size;
}

const RegisterInfo *
DynamicRegisterInfo::GetRegisterInfoAtIndex(uint32_t i) const {
  if (i < m_regs.size())
    return &m_regs[i];
  return nullptr;
}

const RegisterInfo *DynamicRegisterInfo::GetRegisterInfo(uint32_t kind,
                                                         uint32_t num) const {
  uint32_t reg_index = ConvertRegisterKindToRegisterNumber(kind, num);
  if (reg_index != LLDB_INVALID_REGNUM)
    return &m_regs[reg_index];
  return nullptr;
}

const RegisterSet *DynamicRegisterInfo::GetRegisterSet(uint32_t i) const {
  if (i < m_sets.size())
    return &m_sets[i];
  return nullptr;
}

uint32_t
DynamicRegisterInfo::GetRegisterSetIndexByName(const ConstString &set_name,
                                               bool can_create) {
  name_collection::iterator pos, end = m_set_names.end();
  for (pos = m_set_names.begin(); pos != end; ++pos) {
    if (*pos == set_name)
      return std::distance(m_set_names.begin(), pos);
  }

  m_set_names.push_back(set_name);
  m_set_reg_nums.resize(m_set_reg_nums.size() + 1);
  RegisterSet new_set = {set_name.AsCString(), nullptr, 0, nullptr};
  m_sets.push_back(new_set);
  return m_sets.size() - 1;
}

uint32_t
DynamicRegisterInfo::ConvertRegisterKindToRegisterNumber(uint32_t kind,
                                                         uint32_t num) const {
  reg_collection::const_iterator pos, end = m_regs.end();
  for (pos = m_regs.begin(); pos != end; ++pos) {
    if (pos->kinds[kind] == num)
      return std::distance(m_regs.begin(), pos);
  }

  return LLDB_INVALID_REGNUM;
}

void DynamicRegisterInfo::Clear() {
  m_regs.clear();
  m_sets.clear();
  m_set_reg_nums.clear();
  m_set_names.clear();
  m_value_regs_map.clear();
  m_invalidate_regs_map.clear();
  m_reg_data_byte_size = 0;
  m_finalized = false;
}

void DynamicRegisterInfo::Dump() const {
  StreamFile s(stdout, false);
  const size_t num_regs = m_regs.size();
  s.Printf("%p: DynamicRegisterInfo contains %" PRIu64 " registers:\n",
           static_cast<const void *>(this), static_cast<uint64_t>(num_regs));
  for (size_t i = 0; i < num_regs; ++i) {
    s.Printf("[%3" PRIu64 "] name = %-10s", (uint64_t)i, m_regs[i].name);
    s.Printf(", size = %2u, offset = %4u, encoding = %u, format = %-10s",
             m_regs[i].byte_size, m_regs[i].byte_offset, m_regs[i].encoding,
             FormatManager::GetFormatAsCString(m_regs[i].format));
    if (m_regs[i].kinds[eRegisterKindProcessPlugin] != LLDB_INVALID_REGNUM)
      s.Printf(", process plugin = %3u",
               m_regs[i].kinds[eRegisterKindProcessPlugin]);
    if (m_regs[i].kinds[eRegisterKindDWARF] != LLDB_INVALID_REGNUM)
      s.Printf(", dwarf = %3u", m_regs[i].kinds[eRegisterKindDWARF]);
    if (m_regs[i].kinds[eRegisterKindEHFrame] != LLDB_INVALID_REGNUM)
      s.Printf(", ehframe = %3u", m_regs[i].kinds[eRegisterKindEHFrame]);
    if (m_regs[i].kinds[eRegisterKindGeneric] != LLDB_INVALID_REGNUM)
      s.Printf(", generic = %3u", m_regs[i].kinds[eRegisterKindGeneric]);
    if (m_regs[i].alt_name)
      s.Printf(", alt-name = %s", m_regs[i].alt_name);
    if (m_regs[i].value_regs) {
      s.Printf(", value_regs = [ ");
      for (size_t j = 0; m_regs[i].value_regs[j] != LLDB_INVALID_REGNUM; ++j) {
        s.Printf("%s ", m_regs[m_regs[i].value_regs[j]].name);
      }
      s.Printf("]");
    }
    if (m_regs[i].invalidate_regs) {
      s.Printf(", invalidate_regs = [ ");
      for (size_t j = 0; m_regs[i].invalidate_regs[j] != LLDB_INVALID_REGNUM;
           ++j) {
        s.Printf("%s ", m_regs[m_regs[i].invalidate_regs[j]].name);
      }
      s.Printf("]");
    }
    s.EOL();
  }

  const size_t num_sets = m_sets.size();
  s.Printf("%p: DynamicRegisterInfo contains %" PRIu64 " register sets:\n",
           static_cast<const void *>(this), static_cast<uint64_t>(num_sets));
  for (size_t i = 0; i < num_sets; ++i) {
    s.Printf("set[%" PRIu64 "] name = %s, regs = [", (uint64_t)i,
             m_sets[i].name);
    for (size_t idx = 0; idx < m_sets[i].num_registers; ++idx) {
      s.Printf("%s ", m_regs[m_sets[i].registers[idx]].name);
    }
    s.Printf("]\n");
  }
}

const lldb_private::RegisterInfo *
DynamicRegisterInfo::GetRegisterInfo(llvm::StringRef reg_name) const {
  for (auto &reg_info : m_regs)
    if (reg_info.name == reg_name)
      return &reg_info;
  return nullptr;
}

void lldb_private::addSupplementaryRegister(
    std::vector<DynamicRegisterInfo::Register> &regs,
    DynamicRegisterInfo::Register new_reg_info) {
  assert(!new_reg_info.value_regs.empty());
  const uint32_t reg_num = regs.size();
  regs.push_back(new_reg_info);

  std::map<uint32_t, std::vector<uint32_t>> new_invalidates;
  for (uint32_t value_reg : new_reg_info.value_regs) {
    // copy value_regs to invalidate_regs
    new_invalidates[reg_num].push_back(value_reg);

    // copy invalidate_regs from the parent register
    llvm::append_range(new_invalidates[reg_num],
                       regs[value_reg].invalidate_regs);

    // add reverse invalidate entries
    for (uint32_t x : new_invalidates[reg_num])
      new_invalidates[x].push_back(reg_num);
  }

  for (const auto &x : new_invalidates)
    llvm::append_range(regs[x.first].invalidate_regs, x.second);
}
