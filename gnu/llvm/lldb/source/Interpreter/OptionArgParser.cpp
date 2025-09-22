//===-- OptionArgParser.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;
using namespace lldb;

bool OptionArgParser::ToBoolean(llvm::StringRef ref, bool fail_value,
                                bool *success_ptr) {
  if (success_ptr)
    *success_ptr = true;
  ref = ref.trim();
  if (ref.equals_insensitive("false") || ref.equals_insensitive("off") ||
      ref.equals_insensitive("no") || ref.equals_insensitive("0")) {
    return false;
  } else if (ref.equals_insensitive("true") || ref.equals_insensitive("on") ||
             ref.equals_insensitive("yes") || ref.equals_insensitive("1")) {
    return true;
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

llvm::Expected<bool> OptionArgParser::ToBoolean(llvm::StringRef option_name,
                                                llvm::StringRef option_arg) {
  bool parse_success;
  const bool option_value =
      ToBoolean(option_arg, false /* doesn't matter */, &parse_success);
  if (parse_success)
    return option_value;
  else
    return llvm::createStringError(
        "Invalid boolean value for option '%s': '%s'",
        option_name.str().c_str(),
        option_arg.empty() ? "<null>" : option_arg.str().c_str());
}

char OptionArgParser::ToChar(llvm::StringRef s, char fail_value,
                             bool *success_ptr) {
  if (success_ptr)
    *success_ptr = false;
  if (s.size() != 1)
    return fail_value;

  if (success_ptr)
    *success_ptr = true;
  return s[0];
}

int64_t OptionArgParser::ToOptionEnum(llvm::StringRef s,
                                      const OptionEnumValues &enum_values,
                                      int32_t fail_value, Status &error) {
  error.Clear();
  if (enum_values.empty()) {
    error.SetErrorString("invalid enumeration argument");
    return fail_value;
  }

  if (s.empty()) {
    error.SetErrorString("empty enumeration string");
    return fail_value;
  }

  for (const auto &enum_value : enum_values) {
    llvm::StringRef this_enum(enum_value.string_value);
    if (this_enum.starts_with(s))
      return enum_value.value;
  }

  StreamString strm;
  strm.PutCString("invalid enumeration value, valid values are: ");
  bool is_first = true;
  for (const auto &enum_value : enum_values) {
    strm.Printf("%s\"%s\"",
        is_first ? is_first = false,"" : ", ", enum_value.string_value);
  }
  error.SetErrorString(strm.GetString());
  return fail_value;
}

Status OptionArgParser::ToFormat(const char *s, lldb::Format &format,
                                 size_t *byte_size_ptr) {
  format = eFormatInvalid;
  Status error;

  if (s && s[0]) {
    if (byte_size_ptr) {
      if (isdigit(s[0])) {
        char *format_char = nullptr;
        unsigned long byte_size = ::strtoul(s, &format_char, 0);
        if (byte_size != ULONG_MAX)
          *byte_size_ptr = byte_size;
        s = format_char;
      } else
        *byte_size_ptr = 0;
    }

    if (!FormatManager::GetFormatFromCString(s, format)) {
      StreamString error_strm;
      error_strm.Printf(
          "Invalid format character or name '%s'. Valid values are:\n", s);
      for (Format f = eFormatDefault; f < kNumFormats; f = Format(f + 1)) {
        char format_char = FormatManager::GetFormatAsFormatChar(f);
        if (format_char)
          error_strm.Printf("'%c' or ", format_char);

        error_strm.Printf("\"%s\"", FormatManager::GetFormatAsCString(f));
        error_strm.EOL();
      }

      if (byte_size_ptr)
        error_strm.PutCString(
            "An optional byte size can precede the format character.\n");
      error.SetErrorString(error_strm.GetString());
    }

    if (error.Fail())
      return error;
  } else {
    error.SetErrorStringWithFormat("%s option string", s ? "empty" : "invalid");
  }
  return error;
}

lldb::ScriptLanguage OptionArgParser::ToScriptLanguage(
    llvm::StringRef s, lldb::ScriptLanguage fail_value, bool *success_ptr) {
  if (success_ptr)
    *success_ptr = true;

  if (s.equals_insensitive("python"))
    return eScriptLanguagePython;
  if (s.equals_insensitive("lua"))
    return eScriptLanguageLua;
  if (s.equals_insensitive("default"))
    return eScriptLanguageDefault;
  if (s.equals_insensitive("none"))
    return eScriptLanguageNone;

  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

lldb::addr_t OptionArgParser::ToRawAddress(const ExecutionContext *exe_ctx,
                                           llvm::StringRef s,
                                           lldb::addr_t fail_value,
                                           Status *error_ptr) {
  std::optional<lldb::addr_t> maybe_addr = DoToAddress(exe_ctx, s, error_ptr);
  return maybe_addr ? *maybe_addr : fail_value;
}

lldb::addr_t OptionArgParser::ToAddress(const ExecutionContext *exe_ctx,
                                        llvm::StringRef s,
                                        lldb::addr_t fail_value,
                                        Status *error_ptr) {
  std::optional<lldb::addr_t> maybe_addr = DoToAddress(exe_ctx, s, error_ptr);
  if (!maybe_addr)
    return fail_value;

  lldb::addr_t addr = *maybe_addr;

  if (Process *process = exe_ctx->GetProcessPtr())
    if (ABISP abi_sp = process->GetABI())
      addr = abi_sp->FixCodeAddress(addr);

  return addr;
}

std::optional<lldb::addr_t>
OptionArgParser::DoToAddress(const ExecutionContext *exe_ctx, llvm::StringRef s,
                             Status *error_ptr) {
  if (s.empty()) {
    if (error_ptr)
      error_ptr->SetErrorStringWithFormat("invalid address expression \"%s\"",
                                          s.str().c_str());
    return {};
  }

  llvm::StringRef sref = s;

  lldb::addr_t addr = LLDB_INVALID_ADDRESS;
  if (!s.getAsInteger(0, addr)) {
    if (error_ptr)
      error_ptr->Clear();

    return addr;
  }

  // Try base 16 with no prefix...
  if (!s.getAsInteger(16, addr)) {
    if (error_ptr)
      error_ptr->Clear();
    return addr;
  }

  Target *target = nullptr;
  if (!exe_ctx || !(target = exe_ctx->GetTargetPtr())) {
    if (error_ptr)
      error_ptr->SetErrorStringWithFormat("invalid address expression \"%s\"",
                                          s.str().c_str());
    return {};
  }

  lldb::ValueObjectSP valobj_sp;
  EvaluateExpressionOptions options;
  options.SetCoerceToId(false);
  options.SetUnwindOnError(true);
  options.SetKeepInMemory(false);
  options.SetTryAllThreads(true);

  ExpressionResults expr_result =
      target->EvaluateExpression(s, exe_ctx->GetFramePtr(), valobj_sp, options);

  bool success = false;
  if (expr_result == eExpressionCompleted) {
    if (valobj_sp)
      valobj_sp = valobj_sp->GetQualifiedRepresentationIfAvailable(
          valobj_sp->GetDynamicValueType(), true);
    // Get the address to watch.
    if (valobj_sp)
      addr = valobj_sp->GetValueAsUnsigned(0, &success);
    if (success) {
      if (error_ptr)
        error_ptr->Clear();
      return addr;
    }
    if (error_ptr)
      error_ptr->SetErrorStringWithFormat(
          "address expression \"%s\" resulted in a value whose type "
          "can't be converted to an address: %s",
          s.str().c_str(), valobj_sp->GetTypeName().GetCString());
    return {};
  }

  // Since the compiler can't handle things like "main + 12" we should try to
  // do this for now. The compiler doesn't like adding offsets to function
  // pointer types.
  // Some languages also don't have a natural representation for register
  // values (e.g. swift) so handle simple uses of them here as well.
  // We use a regex to parse these forms, the regex handles:
  // $reg_name
  // $reg_name+offset
  // symbol_name+offset
  //
  // The important matching elements in the regex below are:
  // 1: The reg name if there's no +offset
  // 3: The symbol/reg name if there is an offset
  // 4: +/-
  // 5: The offset value.
  static RegularExpression g_symbol_plus_offset_regex(
      "^(\\$[^ +-]+)|(([^ +-]+)([-\\+])[[:space:]]*(0x[0-9A-Fa-f]+|[0-9]+)[[:space:]]*)$");

  llvm::SmallVector<llvm::StringRef, 4> matches;
  if (g_symbol_plus_offset_regex.Execute(sref, &matches)) {
    uint64_t offset = 0;
    llvm::StringRef name;
    if (!matches[1].empty())
      name = matches[1];
    else
      name = matches[3];

    llvm::StringRef sign = matches[4];
    llvm::StringRef str_offset = matches[5];

    // Some languages don't have a natural type for register values, but it
    // is still useful to look them up here:
    std::optional<lldb::addr_t> register_value;
    StackFrame *frame = exe_ctx->GetFramePtr();
    llvm::StringRef reg_name = name;
    if (frame && reg_name.consume_front("$")) {
      RegisterContextSP reg_ctx_sp = frame->GetRegisterContext();
      if (reg_ctx_sp) {
        const RegisterInfo *reg_info = reg_ctx_sp->GetRegisterInfoByName(reg_name);
        if (reg_info) {
          RegisterValue reg_val;
          bool success = reg_ctx_sp->ReadRegister(reg_info, reg_val);
          if (success && reg_val.GetType() != RegisterValue::eTypeInvalid) {
            register_value = reg_val.GetAsUInt64(0, &success);
            if (!success)
              register_value.reset();
          }
        }
      } 
    }
    if (!str_offset.empty() && !str_offset.getAsInteger(0, offset)) {
      Status error;
      if (register_value)
        addr = register_value.value();
      else
        addr = ToAddress(exe_ctx, name, LLDB_INVALID_ADDRESS, &error);
      if (addr != LLDB_INVALID_ADDRESS) {
        if (sign[0] == '+')
          return addr + offset;
        return addr - offset;
      }
    } else if (register_value)
      // In the case of register values, someone might just want to get the 
      // value in a language whose expression parser doesn't support registers.
      return register_value.value();
  }

  if (error_ptr)
    error_ptr->SetErrorStringWithFormat(
        "address expression \"%s\" evaluation failed", s.str().c_str());
  return {};
}
