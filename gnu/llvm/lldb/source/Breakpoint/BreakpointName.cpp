//===-- BreakpointName.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Casting.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointOptions.h"
#include "lldb/Breakpoint/BreakpointLocationCollection.h"
#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Breakpoint/BreakpointResolverFileLine.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

const Flags::ValueType BreakpointName::Permissions::permissions_mask
    [BreakpointName::Permissions::PermissionKinds::allPerms + 1] =  { 
      (1u << 0),
      (1u << 1),
      (1u << 2),
      (0x5u)
};

bool BreakpointName::Permissions::GetDescription(Stream *s,
                                                 lldb::DescriptionLevel level) {
    if (!AnySet())
      return false;
    s->IndentMore();
    s->Indent();
    if (IsSet(listPerm))
      s->Printf("list: %s", GetAllowList() ? "allowed" : "disallowed");
  
    if (IsSet(disablePerm))
      s->Printf("disable: %s", GetAllowDisable() ? "allowed" : "disallowed");
  
    if (IsSet(deletePerm))
      s->Printf("delete: %s", GetAllowDelete() ? "allowed" : "disallowed");
    s->IndentLess();
    return true;
}

bool BreakpointName::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  bool printed_any = false;
  if (!m_help.empty())
    s->Printf("Help: %s\n", m_help.c_str());

  if (GetOptions().AnySet())
  {
    s->PutCString("Options: \n");
    s->IndentMore();
    s->Indent();
    GetOptions().GetDescription(s, level);
    printed_any = true;
    s->IndentLess();
  }
  if (GetPermissions().AnySet())
  {
    s->PutCString("Permissions: \n");
    s->IndentMore();
    s->Indent();
    GetPermissions().GetDescription(s, level);
    printed_any = true;
    s->IndentLess();
 }
  return printed_any;
}

void BreakpointName::ConfigureBreakpoint(lldb::BreakpointSP bp_sp)
{
  bp_sp->GetOptions().CopyOverSetOptions(GetOptions());
  bp_sp->GetPermissions().MergeInto(GetPermissions());
}
