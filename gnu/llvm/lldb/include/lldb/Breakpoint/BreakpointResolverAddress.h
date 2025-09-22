//===-- BreakpointResolverAddress.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_BREAKPOINTRESOLVERADDRESS_H
#define LLDB_BREAKPOINT_BREAKPOINTRESOLVERADDRESS_H

#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Core/ModuleSpec.h"

namespace lldb_private {

/// \class BreakpointResolverAddress BreakpointResolverAddress.h
/// "lldb/Breakpoint/BreakpointResolverAddress.h" This class sets breakpoints
/// on a given Address.  This breakpoint only takes once, and then it won't
/// attempt to reset itself.

class BreakpointResolverAddress : public BreakpointResolver {
public:
  BreakpointResolverAddress(const lldb::BreakpointSP &bkpt,
                            const Address &addr);

  BreakpointResolverAddress(const lldb::BreakpointSP &bkpt,
                            const Address &addr,
                            const FileSpec &module_spec);

  ~BreakpointResolverAddress() override = default;

  static lldb::BreakpointResolverSP
  CreateFromStructuredData(const StructuredData::Dictionary &options_dict,
                           Status &error);

  StructuredData::ObjectSP SerializeToStructuredData() override;

  void ResolveBreakpoint(SearchFilter &filter) override;

  void ResolveBreakpointInModules(SearchFilter &filter,
                                  ModuleList &modules) override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context,
                                          Address *addr) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

  void Dump(Stream *s) const override;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const BreakpointResolverAddress *) { return true; }
  static inline bool classof(const BreakpointResolver *V) {
    return V->getResolverID() == BreakpointResolver::AddressResolver;
  }

  lldb::BreakpointResolverSP
  CopyForBreakpoint(lldb::BreakpointSP &breakpoint) override;

protected:
  Address m_addr;               // The address - may be Section Offset or
                                // may be just an offset
  lldb::addr_t m_resolved_addr; // The current value of the resolved load
                                // address for this breakpoint,
  FileSpec m_module_filespec;   // If this filespec is Valid, and m_addr is an
                                // offset, then it will be converted
  // to a Section+Offset address in this module, whenever that module gets
  // around to being loaded.
private:
  BreakpointResolverAddress(const BreakpointResolverAddress &) = delete;
  const BreakpointResolverAddress &
  operator=(const BreakpointResolverAddress &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_BREAKPOINTRESOLVERADDRESS_H
