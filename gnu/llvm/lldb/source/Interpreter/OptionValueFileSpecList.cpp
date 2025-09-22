//===-- OptionValueFileSpecList.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueFileSpecList.h"

#include "lldb/Utility/Args.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueFileSpecList::DumpValue(const ExecutionContext *exe_ctx,
                                        Stream &strm, uint32_t dump_mask) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    const bool one_line = dump_mask & eDumpOptionCommand;
    const uint32_t size = m_current_value.GetSize();
    if (dump_mask & eDumpOptionType)
      strm.Printf(" =%s",
                  (m_current_value.GetSize() > 0 && !one_line) ? "\n" : "");
    if (!one_line)
      strm.IndentMore();
    for (uint32_t i = 0; i < size; ++i) {
      if (!one_line) {
        strm.Indent();
        strm.Printf("[%u]: ", i);
      }
      m_current_value.GetFileSpecAtIndex(i).Dump(strm.AsRawOstream());
      if (one_line)
        strm << ' ';
    }
    if (!one_line)
      strm.IndentLess();
  }
}

Status OptionValueFileSpecList::SetValueFromString(llvm::StringRef value,
                                                   VarSetOperationType op) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  Status error;
  Args args(value.str());
  const size_t argc = args.GetArgumentCount();

  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
    if (argc > 1) {
      uint32_t idx;
      const uint32_t count = m_current_value.GetSize();
      if (!llvm::to_integer(args.GetArgumentAtIndex(0), idx) || idx > count) {
        error.SetErrorStringWithFormat(
            "invalid file list index %s, index must be 0 through %u",
            args.GetArgumentAtIndex(0), count);
      } else {
        for (size_t i = 1; i < argc; ++i, ++idx) {
          FileSpec file(args.GetArgumentAtIndex(i));
          if (idx < count)
            m_current_value.Replace(idx, file);
          else
            m_current_value.Append(file);
        }
        NotifyValueChanged();
      }
    } else {
      error.SetErrorString("replace operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationAssign:
    m_current_value.Clear();
    // Fall through to append case
    [[fallthrough]];
  case eVarSetOperationAppend:
    if (argc > 0) {
      m_value_was_set = true;
      for (size_t i = 0; i < argc; ++i) {
        FileSpec file(args.GetArgumentAtIndex(i));
        m_current_value.Append(file);
      }
      NotifyValueChanged();
    } else {
      error.SetErrorString(
          "assign operation takes at least one file path argument");
    }
    break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
    if (argc > 1) {
      uint32_t idx;
      const uint32_t count = m_current_value.GetSize();
      if (!llvm::to_integer(args.GetArgumentAtIndex(0), idx) || idx > count) {
        error.SetErrorStringWithFormat(
            "invalid insert file list index %s, index must be 0 through %u",
            args.GetArgumentAtIndex(0), count);
      } else {
        if (op == eVarSetOperationInsertAfter)
          ++idx;
        for (size_t i = 1; i < argc; ++i, ++idx) {
          FileSpec file(args.GetArgumentAtIndex(i));
          m_current_value.Insert(idx, file);
        }
        NotifyValueChanged();
      }
    } else {
      error.SetErrorString("insert operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationRemove:
    if (argc > 0) {
      std::vector<int> remove_indexes;
      bool all_indexes_valid = true;
      size_t i;
      for (i = 0; all_indexes_valid && i < argc; ++i) {
        int idx;
        if (!llvm::to_integer(args.GetArgumentAtIndex(i), idx))
          all_indexes_valid = false;
        else
          remove_indexes.push_back(idx);
      }

      if (all_indexes_valid) {
        size_t num_remove_indexes = remove_indexes.size();
        if (num_remove_indexes) {
          // Sort and then erase in reverse so indexes are always valid
          llvm::sort(remove_indexes);
          for (size_t j = num_remove_indexes - 1; j < num_remove_indexes; ++j) {
            m_current_value.Remove(j);
          }
        }
        NotifyValueChanged();
      } else {
        error.SetErrorStringWithFormat(
            "invalid array index '%s', aborting remove operation",
            args.GetArgumentAtIndex(i));
      }
    } else {
      error.SetErrorString("remove operation takes one or more array index");
    }
    break;

  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value, op);
    break;
  }
  return error;
}

OptionValueSP OptionValueFileSpecList::Clone() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return Cloneable::Clone();
}
