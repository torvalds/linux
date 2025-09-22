//===-- BreakpointResolverFileRegex.h ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_BREAKPOINTRESOLVERFILEREGEX_H
#define LLDB_BREAKPOINT_BREAKPOINTRESOLVERFILEREGEX_H

#include <set>
#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Utility/ConstString.h"

namespace lldb_private {

/// \class BreakpointResolverFileRegex BreakpointResolverFileRegex.h
/// "lldb/Breakpoint/BreakpointResolverFileRegex.h" This class sets
/// breakpoints by file and line.  Optionally, it will look for inlined
/// instances of the file and line specification.

class BreakpointResolverFileRegex : public BreakpointResolver {
public:
  BreakpointResolverFileRegex(
      const lldb::BreakpointSP &bkpt, RegularExpression regex,
      const std::unordered_set<std::string> &func_name_set, bool exact_match);

  static lldb::BreakpointResolverSP
  CreateFromStructuredData(const StructuredData::Dictionary &options_dict,
                           Status &error);

  StructuredData::ObjectSP SerializeToStructuredData() override;

  ~BreakpointResolverFileRegex() override = default;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context,
                                          Address *addr) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

  void Dump(Stream *s) const override;

  void AddFunctionName(const char *func_name);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const BreakpointResolverFileRegex *) {
    return true;
  }
  static inline bool classof(const BreakpointResolver *V) {
    return V->getResolverID() == BreakpointResolver::FileRegexResolver;
  }

  lldb::BreakpointResolverSP
  CopyForBreakpoint(lldb::BreakpointSP &breakpoint) override;

protected:
  friend class Breakpoint;
  RegularExpression
      m_regex;        // This is the line expression that we are looking for.
  bool m_exact_match; // If true, then if the source we match is in a comment,
                      // we won't set a location there.
  std::unordered_set<std::string> m_function_names; // Limit the search to
                                                    // functions in the
                                                    // comp_unit passed in.

private:
  BreakpointResolverFileRegex(const BreakpointResolverFileRegex &) = delete;
  const BreakpointResolverFileRegex &
  operator=(const BreakpointResolverFileRegex &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_BREAKPOINTRESOLVERFILEREGEX_H
