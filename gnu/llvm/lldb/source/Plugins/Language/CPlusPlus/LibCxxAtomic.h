//===-- LibCxxAtomic.h -------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_LIBCXXATOMIC_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_LIBCXXATOMIC_H

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {

lldb::ValueObjectSP GetLibCxxAtomicValue(ValueObject &valobj);

bool LibCxxAtomicSummaryProvider(ValueObject &valobj, Stream &stream,
                                 const TypeSummaryOptions &options);

SyntheticChildrenFrontEnd *
LibcxxAtomicSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                     lldb::ValueObjectSP);

} // namespace formatters
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_LIBCXXATOMIC_H
