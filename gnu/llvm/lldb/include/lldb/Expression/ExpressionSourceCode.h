//===-- ExpressionSourceCode.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_EXPRESSIONSOURCECODE_H
#define LLDB_EXPRESSION_EXPRESSIONSOURCECODE_H

#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace lldb_private {

class ExpressionSourceCode {
protected:
  enum Wrapping : bool {
    Wrap = true,
    NoWrap = false,
  };

public:
  bool NeedsWrapping() const { return m_wrap == Wrap; }

  const char *GetName() const { return m_name.c_str(); }

protected:
  ExpressionSourceCode(llvm::StringRef name, llvm::StringRef prefix,
                       llvm::StringRef body, Wrapping wrap)
      : m_name(name.str()), m_prefix(prefix.str()), m_body(body.str()),
        m_wrap(wrap) {}

  std::string m_name;
  std::string m_prefix;
  std::string m_body;
  Wrapping m_wrap;
};

} // namespace lldb_private

#endif
