//===-- VectorType.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_VECTORTYPE_H
#define LLDB_DATAFORMATTERS_VECTORTYPE_H

#include "lldb/lldb-forward.h"

namespace lldb_private {
namespace formatters {
bool VectorTypeSummaryProvider(ValueObject &, Stream &,
                               const TypeSummaryOptions &);

SyntheticChildrenFrontEnd *
VectorTypeSyntheticFrontEndCreator(CXXSyntheticChildren *, lldb::ValueObjectSP);
} // namespace formatters
} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_VECTORTYPE_H
