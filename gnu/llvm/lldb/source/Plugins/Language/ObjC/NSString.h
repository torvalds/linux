//===-- NSString.h ---------------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSSTRING_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSSTRING_H

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/Utility/Stream.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

namespace lldb_private {
namespace formatters {
bool NSStringSummaryProvider(ValueObject &valobj, Stream &stream,
                             const TypeSummaryOptions &options);

bool NSTaggedString_SummaryProvider(
    ValueObject &valobj, ObjCLanguageRuntime::ClassDescriptorSP descriptor,
    Stream &stream, const TypeSummaryOptions &summary_options);

bool NSAttributedStringSummaryProvider(ValueObject &valobj, Stream &stream,
                                       const TypeSummaryOptions &options);

bool NSMutableAttributedStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options);

class NSString_Additionals {
public:
  static std::map<ConstString, CXXFunctionSummaryFormat::Callback> &
  GetAdditionalSummaries();
};
} // namespace formatters
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSSTRING_H
