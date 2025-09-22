//===-- OptionValueArray.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueArray.h"

#include "lldb/Utility/Args.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueArray::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                 uint32_t dump_mask) {
  const Type array_element_type = ConvertTypeMaskToType(m_type_mask);
  if (dump_mask & eDumpOptionType) {
    if ((GetType() == eTypeArray) && (m_type_mask != eTypeInvalid))
      strm.Printf("(%s of %ss)", GetTypeAsCString(),
                  GetBuiltinTypeAsCString(array_element_type));
    else
      strm.Printf("(%s)", GetTypeAsCString());
  }
  if (dump_mask & eDumpOptionValue) {
    const bool one_line = dump_mask & eDumpOptionCommand;
    const uint32_t size = m_values.size();
    if (dump_mask & eDumpOptionType)
      strm.Printf(" =%s", (m_values.size() > 0 && !one_line) ? "\n" : "");
    if (!one_line)
      strm.IndentMore();
    for (uint32_t i = 0; i < size; ++i) {
      if (!one_line) {
        strm.Indent();
        strm.Printf("[%u]: ", i);
      }
      const uint32_t extra_dump_options = m_raw_value_dump ? eDumpOptionRaw : 0;
      switch (array_element_type) {
      default:
      case eTypeArray:
      case eTypeDictionary:
      case eTypeProperties:
      case eTypeFileSpecList:
      case eTypePathMap:
        m_values[i]->DumpValue(exe_ctx, strm, dump_mask | extra_dump_options);
        break;

      case eTypeBoolean:
      case eTypeChar:
      case eTypeEnum:
      case eTypeFileSpec:
      case eTypeFileLineColumn:
      case eTypeFormat:
      case eTypeSInt64:
      case eTypeString:
      case eTypeUInt64:
      case eTypeUUID:
        // No need to show the type for dictionaries of simple items
        m_values[i]->DumpValue(exe_ctx, strm, (dump_mask & (~eDumpOptionType)) |
                                                  extra_dump_options);
        break;
      }

      if (!one_line) {
        if (i < (size - 1))
          strm.EOL();
      } else {
        strm << ' ';
      }
    }
    if (!one_line)
      strm.IndentLess();
  }
}

llvm::json::Value OptionValueArray::ToJSON(const ExecutionContext *exe_ctx) {
  llvm::json::Array json_array;
  const uint32_t size = m_values.size();
  for (uint32_t i = 0; i < size; ++i)
    json_array.emplace_back(m_values[i]->ToJSON(exe_ctx));
  return json_array;
}

Status OptionValueArray::SetValueFromString(llvm::StringRef value,
                                            VarSetOperationType op) {
  Args args(value.str());
  Status error = SetArgs(args, op);
  if (error.Success())
    NotifyValueChanged();
  return error;
}

lldb::OptionValueSP
OptionValueArray::GetSubValue(const ExecutionContext *exe_ctx,
                              llvm::StringRef name, Status &error) const {
  if (name.empty() || name.front() != '[') {
    error.SetErrorStringWithFormat(
      "invalid value path '%s', %s values only support '[<index>]' subvalues "
      "where <index> is a positive or negative array index",
      name.str().c_str(), GetTypeAsCString());
    return nullptr;
  }

  name = name.drop_front();
  llvm::StringRef index, sub_value;
  std::tie(index, sub_value) = name.split(']');
  if (index.size() == name.size()) {
    // Couldn't find a closing bracket
    return nullptr;
  }

  const size_t array_count = m_values.size();
  int32_t idx = 0;
  if (index.getAsInteger(0, idx))
    return nullptr;

  uint32_t new_idx = UINT32_MAX;
  if (idx < 0) {
    // Access from the end of the array if the index is negative
    new_idx = array_count - idx;
  } else {
    // Just a standard index
    new_idx = idx;
  }

  if (new_idx < array_count) {
    if (m_values[new_idx]) {
      if (!sub_value.empty())
        return m_values[new_idx]->GetSubValue(exe_ctx, sub_value, error);
      else
        return m_values[new_idx];
    }
  } else {
    if (array_count == 0)
      error.SetErrorStringWithFormat(
          "index %i is not valid for an empty array", idx);
    else if (idx > 0)
      error.SetErrorStringWithFormat(
          "index %i out of range, valid values are 0 through %" PRIu64,
          idx, (uint64_t)(array_count - 1));
    else
      error.SetErrorStringWithFormat("negative index %i out of range, "
                                      "valid values are -1 through "
                                      "-%" PRIu64,
                                      idx, (uint64_t)array_count);
  }
  return OptionValueSP();
}

size_t OptionValueArray::GetArgs(Args &args) const {
  args.Clear();
  const uint32_t size = m_values.size();
  for (uint32_t i = 0; i < size; ++i) {
    auto string_value = m_values[i]->GetValueAs<llvm::StringRef>();
    if (string_value)
      args.AppendArgument(*string_value);
  }

  return args.GetArgumentCount();
}

Status OptionValueArray::SetArgs(const Args &args, VarSetOperationType op) {
  Status error;
  const size_t argc = args.GetArgumentCount();
  switch (op) {
  case eVarSetOperationInvalid:
    error.SetErrorString("unsupported operation");
    break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
    if (argc > 1) {
      uint32_t idx;
      const uint32_t count = GetSize();
      if (!llvm::to_integer(args.GetArgumentAtIndex(0), idx) || idx > count) {
        error.SetErrorStringWithFormat(
            "invalid insert array index %s, index must be 0 through %u",
            args.GetArgumentAtIndex(0), count);
      } else {
        if (op == eVarSetOperationInsertAfter)
          ++idx;
        for (size_t i = 1; i < argc; ++i, ++idx) {
          lldb::OptionValueSP value_sp(CreateValueFromCStringForTypeMask(
              args.GetArgumentAtIndex(i), m_type_mask, error));
          if (value_sp) {
            if (error.Fail())
              return error;
            if (idx >= m_values.size())
              m_values.push_back(value_sp);
            else
              m_values.insert(m_values.begin() + idx, value_sp);
          } else {
            error.SetErrorString(
                "array of complex types must subclass OptionValueArray");
            return error;
          }
        }
      }
    } else {
      error.SetErrorString("insert operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationRemove:
    if (argc > 0) {
      const uint32_t size = m_values.size();
      std::vector<int> remove_indexes;
      bool all_indexes_valid = true;
      size_t i;
      for (i = 0; i < argc; ++i) {
        size_t idx;
        if (!llvm::to_integer(args.GetArgumentAtIndex(i), idx) || idx >= size) {
          all_indexes_valid = false;
          break;
        } else
          remove_indexes.push_back(idx);
      }

      if (all_indexes_valid) {
        size_t num_remove_indexes = remove_indexes.size();
        if (num_remove_indexes) {
          // Sort and then erase in reverse so indexes are always valid
          if (num_remove_indexes > 1) {
            llvm::sort(remove_indexes);
            for (std::vector<int>::const_reverse_iterator
                     pos = remove_indexes.rbegin(),
                     end = remove_indexes.rend();
                 pos != end; ++pos) {
              m_values.erase(m_values.begin() + *pos);
            }
          } else {
            // Only one index
            m_values.erase(m_values.begin() + remove_indexes.front());
          }
        }
      } else {
        error.SetErrorStringWithFormat(
            "invalid array index '%s', aborting remove operation",
            args.GetArgumentAtIndex(i));
      }
    } else {
      error.SetErrorString("remove operation takes one or more array indices");
    }
    break;

  case eVarSetOperationClear:
    Clear();
    break;

  case eVarSetOperationReplace:
    if (argc > 1) {
      uint32_t idx;
      const uint32_t count = GetSize();
      if (!llvm::to_integer(args.GetArgumentAtIndex(0), idx) || idx > count) {
        error.SetErrorStringWithFormat(
            "invalid replace array index %s, index must be 0 through %u",
            args.GetArgumentAtIndex(0), count);
      } else {
        for (size_t i = 1; i < argc; ++i, ++idx) {
          lldb::OptionValueSP value_sp(CreateValueFromCStringForTypeMask(
              args.GetArgumentAtIndex(i), m_type_mask, error));
          if (value_sp) {
            if (error.Fail())
              return error;
            if (idx < count)
              m_values[idx] = value_sp;
            else
              m_values.push_back(value_sp);
          } else {
            error.SetErrorString(
                "array of complex types must subclass OptionValueArray");
            return error;
          }
        }
      }
    } else {
      error.SetErrorString("replace operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationAssign:
    m_values.clear();
    // Fall through to append case
    [[fallthrough]];
  case eVarSetOperationAppend:
    for (size_t i = 0; i < argc; ++i) {
      lldb::OptionValueSP value_sp(CreateValueFromCStringForTypeMask(
          args.GetArgumentAtIndex(i), m_type_mask, error));
      if (value_sp) {
        if (error.Fail())
          return error;
        m_value_was_set = true;
        AppendValue(value_sp);
      } else {
        error.SetErrorString(
            "array of complex types must subclass OptionValueArray");
      }
    }
    break;
  }
  return error;
}

OptionValueSP
OptionValueArray::DeepCopy(const OptionValueSP &new_parent) const {
  auto copy_sp = OptionValue::DeepCopy(new_parent);
  // copy_sp->GetAsArray cannot be used here as it doesn't work for derived
  // types that override GetType returning a different value.
  auto *array_value_ptr = static_cast<OptionValueArray *>(copy_sp.get());
  lldbassert(array_value_ptr);

  for (auto &value : array_value_ptr->m_values)
    value = value->DeepCopy(copy_sp);

  return copy_sp;
}
