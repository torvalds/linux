//===-- NSSet.h ---------------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSSET_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSSET_H

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {
namespace formatters {
template <bool cf_style>
bool NSSetSummaryProvider(ValueObject &valobj, Stream &stream,
                          const TypeSummaryOptions &options);

SyntheticChildrenFrontEnd *NSSetSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                                         lldb::ValueObjectSP);

class NSSet_Additionals {
public:
  static std::map<ConstString, CXXFunctionSummaryFormat::Callback> &
  GetAdditionalSummaries();

  static std::map<ConstString, CXXSyntheticChildren::CreateFrontEndCallback> &
  GetAdditionalSynthetics();
};
} // namespace formatters
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSSET_H
