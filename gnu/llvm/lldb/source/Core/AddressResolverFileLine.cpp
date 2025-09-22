//===-- AddressResolverFileLine.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/AddressResolverFileLine.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"

#include <cinttypes>
#include <vector>

using namespace lldb;
using namespace lldb_private;

// AddressResolverFileLine:
AddressResolverFileLine::AddressResolverFileLine(
    SourceLocationSpec location_spec)
    : AddressResolver(), m_src_location_spec(location_spec) {}

AddressResolverFileLine::~AddressResolverFileLine() = default;

Searcher::CallbackReturn
AddressResolverFileLine::SearchCallback(SearchFilter &filter,
                                        SymbolContext &context, Address *addr) {
  SymbolContextList sc_list;
  CompileUnit *cu = context.comp_unit;

  Log *log = GetLog(LLDBLog::Breakpoints);

  // TODO: Handle SourceLocationSpec column information
  cu->ResolveSymbolContext(m_src_location_spec, eSymbolContextEverything,
                           sc_list);
  for (const SymbolContext &sc : sc_list) {
    Address line_start = sc.line_entry.range.GetBaseAddress();
    addr_t byte_size = sc.line_entry.range.GetByteSize();
    if (line_start.IsValid()) {
      AddressRange new_range(line_start, byte_size);
      m_address_ranges.push_back(new_range);
    } else {
      LLDB_LOGF(log,
                "error: Unable to resolve address at file address 0x%" PRIx64
                " for %s:%d\n",
                line_start.GetFileAddress(),
                m_src_location_spec.GetFileSpec().GetFilename().AsCString(
                    "<Unknown>"),
                m_src_location_spec.GetLine().value_or(0));
    }
  }
  return Searcher::eCallbackReturnContinue;
}

lldb::SearchDepth AddressResolverFileLine::GetDepth() {
  return lldb::eSearchDepthCompUnit;
}

void AddressResolverFileLine::GetDescription(Stream *s) {
  s->Printf(
      "File and line address - file: \"%s\" line: %u",
      m_src_location_spec.GetFileSpec().GetFilename().AsCString("<Unknown>"),
      m_src_location_spec.GetLine().value_or(0));
}
