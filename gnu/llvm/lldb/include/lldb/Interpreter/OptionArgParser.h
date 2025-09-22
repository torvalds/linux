//===-- OptionArgParser.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONARGPARSER_H
#define LLDB_INTERPRETER_OPTIONARGPARSER_H

#include "lldb/lldb-private-types.h"

#include <optional>

namespace lldb_private {

struct OptionArgParser {
  /// Try to parse an address. If it succeeds return the address with the
  /// non-address bits removed.
  static lldb::addr_t ToAddress(const ExecutionContext *exe_ctx,
                                llvm::StringRef s, lldb::addr_t fail_value,
                                Status *error_ptr);

  /// As for ToAddress but do not remove non-address bits from the result.
  static lldb::addr_t ToRawAddress(const ExecutionContext *exe_ctx,
                                   llvm::StringRef s, lldb::addr_t fail_value,
                                   Status *error_ptr);

  static bool ToBoolean(llvm::StringRef s, bool fail_value, bool *success_ptr);

  static llvm::Expected<bool> ToBoolean(llvm::StringRef option_name,
                                        llvm::StringRef option_arg);

  static char ToChar(llvm::StringRef s, char fail_value, bool *success_ptr);

  static int64_t ToOptionEnum(llvm::StringRef s,
                              const OptionEnumValues &enum_values,
                              int32_t fail_value, Status &error);

  static lldb::ScriptLanguage ToScriptLanguage(llvm::StringRef s,
                                               lldb::ScriptLanguage fail_value,
                                               bool *success_ptr);

  // TODO: Use StringRef
  static Status ToFormat(const char *s, lldb::Format &format,
                         size_t *byte_size_ptr); // If non-NULL, then a
                                                 // byte size can precede
                                                 // the format character

private:
  static std::optional<lldb::addr_t>
  DoToAddress(const ExecutionContext *exe_ctx, llvm::StringRef s,
              Status *error);
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONARGPARSER_H
