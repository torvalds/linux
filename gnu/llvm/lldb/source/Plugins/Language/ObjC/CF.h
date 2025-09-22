//===-- CF.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_CF_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_CF_H

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {
bool CFBagSummaryProvider(ValueObject &valobj, Stream &stream,
                          const TypeSummaryOptions &options);

bool CFBinaryHeapSummaryProvider(ValueObject &valobj, Stream &stream,
                                 const TypeSummaryOptions &options);

bool CFBitVectorSummaryProvider(ValueObject &valobj, Stream &stream,
                                const TypeSummaryOptions &options);

bool CFAbsoluteTimeSummaryProvider(ValueObject &valobj, Stream &stream,
                                   const TypeSummaryOptions &options);
} // namespace formatters
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_CF_H
