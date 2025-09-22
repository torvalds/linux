//===-- ExpressionTypeSystemHelper.h ---------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_EXPRESSIONTYPESYSTEMHELPER_H
#define LLDB_EXPRESSION_EXPRESSIONTYPESYSTEMHELPER_H

#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

namespace lldb_private {

/// \class ExpressionTypeSystemHelper ExpressionTypeSystemHelper.h
/// "lldb/Expression/ExpressionTypeSystemHelper.h"
/// A helper object that the Expression can pass to its ExpressionParser
/// to provide generic information that any type of expression will need to
/// supply.  It's only job is to support dyn_cast so that the expression parser
/// can cast it back to the requisite specific type.
///

class ExpressionTypeSystemHelper
    : public llvm::RTTIExtends<ExpressionTypeSystemHelper, llvm::RTTIRoot> {
public:
  /// LLVM RTTI support
  static char ID;

  virtual ~ExpressionTypeSystemHelper() = default;
};

} // namespace lldb_private

#endif // LLDB_EXPRESSION_EXPRESSIONTYPESYSTEMHELPER_H
