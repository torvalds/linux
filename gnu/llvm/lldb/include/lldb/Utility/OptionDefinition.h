//===-- OptionDefinition.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_OPTIONDEFINITION_H
#define LLDB_UTILITY_OPTIONDEFINITION_H

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-types.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/MathExtras.h"
#include <climits>
#include <cstdint>

namespace lldb_private {
struct OptionDefinition {
  /// Used to mark options that can be used together.  If
  /// `(1 << n & usage_mask) != 0` then this option belongs to option set n.
  uint32_t usage_mask;
  /// This option is required (in the current usage level).
  bool required;
  /// Full name for this option.
  const char *long_option;
  /// Single character for this option. If the option doesn't use a short
  /// option character, this has to be a integer value that is not a printable
  /// ASCII code point and also unique in the used set of options.
  /// @see OptionDefinition::HasShortOption
  int short_option;
  /// no_argument, required_argument or optional_argument
  int option_has_arg;
  /// If non-NULL, option is valid iff |validator->IsValid()|, otherwise
  /// always valid.
  OptionValidator *validator;
  /// If not empty, an array of enum values.
  OptionEnumValues enum_values;
  /// The kind of completion for this option.
  /// Contains values of the lldb::CompletionType enum.
  uint32_t completion_type;
  /// Type of argument this option takes.
  lldb::CommandArgumentType argument_type;
  /// Full text explaining what this options does and what (if any) argument to
  /// pass it.
  const char *usage_text;

  /// Whether this has a short option character.
  bool HasShortOption() const {
    // See the short_option documentation for more.
    return llvm::isUInt<CHAR_BIT>(short_option) &&
           llvm::isPrint(short_option);
  }
};
} // namespace lldb_private

#endif // LLDB_UTILITY_OPTIONDEFINITION_H
