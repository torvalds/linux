//===-- OptionParser.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_OPTIONPARSER_H
#define LLDB_HOST_OPTIONPARSER_H

#include <mutex>
#include <string>

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ArrayRef.h"

struct option;

namespace lldb_private {

struct OptionDefinition;

struct Option {
  // The definition of the option that this refers to.
  const OptionDefinition *definition;
  // if not NULL, set *flag to val when option found
  int *flag;
  // if flag not NULL, value to set *flag to; else return value
  int val;
};

class OptionParser {
public:
  enum OptionArgument { eNoArgument = 0, eRequiredArgument, eOptionalArgument };

  static void Prepare(std::unique_lock<std::mutex> &lock);

  static void EnableError(bool error);

  /// Argv must be an argument vector "as passed to main", i.e. terminated with
  /// a nullptr.
  static int Parse(llvm::MutableArrayRef<char *> argv,
                   llvm::StringRef optstring, const Option *longopts,
                   int *longindex);

  static char *GetOptionArgument();
  static int GetOptionIndex();
  static int GetOptionErrorCause();
  static std::string GetShortOptionString(struct option *long_options);
};
}

#endif // LLDB_HOST_OPTIONPARSER_H
