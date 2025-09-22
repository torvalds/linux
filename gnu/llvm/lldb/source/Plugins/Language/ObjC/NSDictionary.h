//===-- NSDictionary.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSDICTIONARY_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSDICTIONARY_H

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

#include <map>
#include <memory>

namespace lldb_private {
namespace formatters {
template <bool name_entries>
bool NSDictionarySummaryProvider(ValueObject &valobj, Stream &stream,
                                 const TypeSummaryOptions &options);

extern template bool
NSDictionarySummaryProvider<true>(ValueObject &, Stream &,
                                  const TypeSummaryOptions &);

extern template bool
NSDictionarySummaryProvider<false>(ValueObject &, Stream &,
                                   const TypeSummaryOptions &);

SyntheticChildrenFrontEnd *
NSDictionarySyntheticFrontEndCreator(CXXSyntheticChildren *,
                                     lldb::ValueObjectSP);

class NSDictionary_Additionals {
public:
  class AdditionalFormatterMatching {
  public:
    class Matcher {
    public:
      virtual ~Matcher() = default;
      virtual bool Match(ConstString class_name) = 0;

      typedef std::unique_ptr<Matcher> UP;
    };
    class Prefix : public Matcher {
    public:
      Prefix(ConstString p);
      ~Prefix() override = default;
      bool Match(ConstString class_name) override;

    private:
      ConstString m_prefix;
    };
    class Full : public Matcher {
    public:
      Full(ConstString n);
      ~Full() override = default;
      bool Match(ConstString class_name) override;

    private:
      ConstString m_name;
    };
    typedef Matcher::UP MatcherUP;

    MatcherUP GetFullMatch(ConstString n) { return std::make_unique<Full>(n); }

    MatcherUP GetPrefixMatch(ConstString p) {
      return std::make_unique<Prefix>(p);
    }
  };

  template <typename FormatterType>
  using AdditionalFormatter =
      std::pair<AdditionalFormatterMatching::MatcherUP, FormatterType>;

  template <typename FormatterType>
  using AdditionalFormatters = std::vector<AdditionalFormatter<FormatterType>>;

  static AdditionalFormatters<CXXFunctionSummaryFormat::Callback> &
  GetAdditionalSummaries();

  static AdditionalFormatters<CXXSyntheticChildren::CreateFrontEndCallback> &
  GetAdditionalSynthetics();
};
} // namespace formatters
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_OBJC_NSDICTIONARY_H
