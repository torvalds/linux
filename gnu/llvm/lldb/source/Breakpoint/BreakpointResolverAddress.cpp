//===-- BreakpointResolverAddress.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolverAddress.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

// BreakpointResolverAddress:
BreakpointResolverAddress::BreakpointResolverAddress(
    const BreakpointSP &bkpt, const Address &addr, const FileSpec &module_spec)
    : BreakpointResolver(bkpt, BreakpointResolver::AddressResolver),
      m_addr(addr), m_resolved_addr(LLDB_INVALID_ADDRESS),
      m_module_filespec(module_spec) {}

BreakpointResolverAddress::BreakpointResolverAddress(const BreakpointSP &bkpt,
                                                     const Address &addr)
    : BreakpointResolver(bkpt, BreakpointResolver::AddressResolver),
      m_addr(addr), m_resolved_addr(LLDB_INVALID_ADDRESS) {}

BreakpointResolverSP BreakpointResolverAddress::CreateFromStructuredData(
    const StructuredData::Dictionary &options_dict, Status &error) {
  llvm::StringRef module_name;
  lldb::offset_t addr_offset;
  FileSpec module_filespec;
  bool success;

  success = options_dict.GetValueForKeyAsInteger(
      GetKey(OptionNames::AddressOffset), addr_offset);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find address offset entry.");
    return nullptr;
  }
  Address address(addr_offset);

  success = options_dict.HasKey(GetKey(OptionNames::ModuleName));
  if (success) {
    success = options_dict.GetValueForKeyAsString(
        GetKey(OptionNames::ModuleName), module_name);
    if (!success) {
      error.SetErrorString("BRA::CFSD: Couldn't read module name entry.");
      return nullptr;
    }
    module_filespec.SetFile(module_name, FileSpec::Style::native);
  }
  return std::make_shared<BreakpointResolverAddress>(nullptr, address,
                                                     module_filespec);
}

StructuredData::ObjectSP
BreakpointResolverAddress::SerializeToStructuredData() {
  StructuredData::DictionarySP options_dict_sp(
      new StructuredData::Dictionary());
  SectionSP section_sp = m_addr.GetSection();
  if (section_sp) {
    if (ModuleSP module_sp = section_sp->GetModule()) {
      const FileSpec &module_fspec = module_sp->GetFileSpec();
      options_dict_sp->AddStringItem(GetKey(OptionNames::ModuleName),
                                     module_fspec.GetPath().c_str());
    }
    options_dict_sp->AddIntegerItem(GetKey(OptionNames::AddressOffset),
                                    m_addr.GetOffset());
  } else {
    options_dict_sp->AddIntegerItem(GetKey(OptionNames::AddressOffset),
                                    m_addr.GetOffset());
    if (m_module_filespec) {
      options_dict_sp->AddStringItem(GetKey(OptionNames::ModuleName),
                                     m_module_filespec.GetPath());
    }
  }

  return WrapOptionsDict(options_dict_sp);
}

void BreakpointResolverAddress::ResolveBreakpoint(SearchFilter &filter) {
  // If the address is not section relative, then we should not try to re-
  // resolve it, it is just some random address and we wouldn't know what to do
  // on reload.  But if it is section relative, we need to re-resolve it since
  // the section it's in may have shifted on re-run.
  bool re_resolve = false;
  if (m_addr.GetSection() || m_module_filespec)
    re_resolve = true;
  else if (GetBreakpoint()->GetNumLocations() == 0)
    re_resolve = true;

  if (re_resolve)
    BreakpointResolver::ResolveBreakpoint(filter);
}

void BreakpointResolverAddress::ResolveBreakpointInModules(
    SearchFilter &filter, ModuleList &modules) {
  // See comment in ResolveBreakpoint.
  bool re_resolve = false;
  if (m_addr.GetSection())
    re_resolve = true;
  else if (GetBreakpoint()->GetNumLocations() == 0)
    re_resolve = true;

  if (re_resolve)
    BreakpointResolver::ResolveBreakpointInModules(filter, modules);
}

Searcher::CallbackReturn BreakpointResolverAddress::SearchCallback(
    SearchFilter &filter, SymbolContext &context, Address *addr) {
  BreakpointSP breakpoint_sp = GetBreakpoint();
  Breakpoint &breakpoint = *breakpoint_sp;

  if (filter.AddressPasses(m_addr)) {
    if (breakpoint.GetNumLocations() == 0) {
      // If the address is just an offset, and we're given a module, see if we
      // can find the appropriate module loaded in the binary, and fix up
      // m_addr to use that.
      if (!m_addr.IsSectionOffset() && m_module_filespec) {
        Target &target = breakpoint.GetTarget();
        ModuleSpec module_spec(m_module_filespec);
        ModuleSP module_sp = target.GetImages().FindFirstModule(module_spec);
        if (module_sp) {
          Address tmp_address;
          if (module_sp->ResolveFileAddress(m_addr.GetOffset(), tmp_address))
            m_addr = tmp_address;
        }
      }

      m_resolved_addr = m_addr.GetLoadAddress(&breakpoint.GetTarget());
      BreakpointLocationSP bp_loc_sp(AddLocation(m_addr));
      if (bp_loc_sp && !breakpoint.IsInternal()) {
        StreamString s;
        bp_loc_sp->GetDescription(&s, lldb::eDescriptionLevelVerbose);
        Log *log = GetLog(LLDBLog::Breakpoints);
        LLDB_LOGF(log, "Added location: %s\n", s.GetData());
      }
    } else {
      BreakpointLocationSP loc_sp = breakpoint.GetLocationAtIndex(0);
      lldb::addr_t cur_load_location =
          m_addr.GetLoadAddress(&breakpoint.GetTarget());
      if (cur_load_location != m_resolved_addr) {
        m_resolved_addr = cur_load_location;
        loc_sp->ClearBreakpointSite();
        loc_sp->ResolveBreakpointSite();
      }
    }
  }
  return Searcher::eCallbackReturnStop;
}

lldb::SearchDepth BreakpointResolverAddress::GetDepth() {
  return lldb::eSearchDepthTarget;
}

void BreakpointResolverAddress::GetDescription(Stream *s) {
  s->PutCString("address = ");
  m_addr.Dump(s, GetBreakpoint()->GetTarget().GetProcessSP().get(),
              Address::DumpStyleModuleWithFileAddress,
              Address::DumpStyleLoadAddress);
}

void BreakpointResolverAddress::Dump(Stream *s) const {}

lldb::BreakpointResolverSP
BreakpointResolverAddress::CopyForBreakpoint(BreakpointSP &breakpoint) {
  lldb::BreakpointResolverSP ret_sp(
      new BreakpointResolverAddress(breakpoint, m_addr));
  return ret_sp;
}
