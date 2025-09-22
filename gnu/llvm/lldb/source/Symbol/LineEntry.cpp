//===-- LineEntry.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb_private;

LineEntry::LineEntry()
    : range(), file_sp(std::make_shared<SupportFile>()),
      original_file_sp(std::make_shared<SupportFile>()),
      is_start_of_statement(0), is_start_of_basic_block(0), is_prologue_end(0),
      is_epilogue_begin(0), is_terminal_entry(0) {}

void LineEntry::Clear() {
  range.Clear();
  file_sp = std::make_shared<SupportFile>();
  original_file_sp = std::make_shared<SupportFile>();
  line = LLDB_INVALID_LINE_NUMBER;
  column = 0;
  is_start_of_statement = 0;
  is_start_of_basic_block = 0;
  is_prologue_end = 0;
  is_epilogue_begin = 0;
  is_terminal_entry = 0;
}

bool LineEntry::IsValid() const {
  return range.GetBaseAddress().IsValid() && line != LLDB_INVALID_LINE_NUMBER;
}

bool LineEntry::DumpStopContext(Stream *s, bool show_fullpaths) const {
  const FileSpec &file = file_sp->GetSpecOnly();
  if (file) {
    if (show_fullpaths)
      file.Dump(s->AsRawOstream());
    else
      file.GetFilename().Dump(s);

    if (line)
      s->PutChar(':');
  }
  if (line) {
    s->Printf("%u", line);
    if (column) {
      s->PutChar(':');
      s->Printf("%u", column);
    }
  }
  return file || line;
}

bool LineEntry::Dump(Stream *s, Target *target, bool show_file,
                     Address::DumpStyle style,
                     Address::DumpStyle fallback_style, bool show_range) const {
  if (show_range) {
    // Show address range
    if (!range.Dump(s, target, style, fallback_style))
      return false;
  } else {
    // Show address only
    if (!range.GetBaseAddress().Dump(s, target, style, fallback_style))
      return false;
  }
  if (show_file)
    *s << ", file = " << GetFile();
  if (line)
    s->Printf(", line = %u", line);
  if (column)
    s->Printf(", column = %u", column);
  if (is_start_of_statement)
    *s << ", is_start_of_statement = TRUE";

  if (is_start_of_basic_block)
    *s << ", is_start_of_basic_block = TRUE";

  if (is_prologue_end)
    *s << ", is_prologue_end = TRUE";

  if (is_epilogue_begin)
    *s << ", is_epilogue_begin = TRUE";

  if (is_terminal_entry)
    *s << ", is_terminal_entry = TRUE";
  return true;
}

bool LineEntry::GetDescription(Stream *s, lldb::DescriptionLevel level,
                               CompileUnit *cu, Target *target,
                               bool show_address_only) const {

  if (level == lldb::eDescriptionLevelBrief ||
      level == lldb::eDescriptionLevelFull) {
    if (show_address_only) {
      range.GetBaseAddress().Dump(s, target, Address::DumpStyleLoadAddress,
                                  Address::DumpStyleFileAddress);
    } else {
      range.Dump(s, target, Address::DumpStyleLoadAddress,
                 Address::DumpStyleFileAddress);
    }

    *s << ": " << GetFile();

    if (line) {
      s->Printf(":%u", line);
      if (column)
        s->Printf(":%u", column);
    }

    if (level == lldb::eDescriptionLevelFull) {
      if (is_start_of_statement)
        *s << ", is_start_of_statement = TRUE";

      if (is_start_of_basic_block)
        *s << ", is_start_of_basic_block = TRUE";

      if (is_prologue_end)
        *s << ", is_prologue_end = TRUE";

      if (is_epilogue_begin)
        *s << ", is_epilogue_begin = TRUE";

      if (is_terminal_entry)
        *s << ", is_terminal_entry = TRUE";
    } else {
      if (is_terminal_entry)
        s->EOL();
    }
  } else {
    return Dump(s, target, true, Address::DumpStyleLoadAddress,
                Address::DumpStyleModuleWithFileAddress, true);
  }
  return true;
}

bool lldb_private::operator<(const LineEntry &a, const LineEntry &b) {
  return LineEntry::Compare(a, b) < 0;
}

int LineEntry::Compare(const LineEntry &a, const LineEntry &b) {
  int result = Address::CompareFileAddress(a.range.GetBaseAddress(),
                                           b.range.GetBaseAddress());
  if (result != 0)
    return result;

  const lldb::addr_t a_byte_size = a.range.GetByteSize();
  const lldb::addr_t b_byte_size = b.range.GetByteSize();

  if (a_byte_size < b_byte_size)
    return -1;
  if (a_byte_size > b_byte_size)
    return +1;

  // Check for an end sequence entry mismatch after we have determined that the
  // address values are equal. If one of the items is an end sequence, we don't
  // care about the line, file, or column info.
  if (a.is_terminal_entry > b.is_terminal_entry)
    return -1;
  if (a.is_terminal_entry < b.is_terminal_entry)
    return +1;

  if (a.line < b.line)
    return -1;
  if (a.line > b.line)
    return +1;

  if (a.column < b.column)
    return -1;
  if (a.column > b.column)
    return +1;

  return FileSpec::Compare(a.GetFile(), b.GetFile(), true);
}

AddressRange LineEntry::GetSameLineContiguousAddressRange(
    bool include_inlined_functions) const {
  // Add each LineEntry's range to complete_line_range until we find a
  // different file / line number.
  AddressRange complete_line_range = range;
  auto symbol_context_scope = lldb::eSymbolContextLineEntry;
  Declaration start_call_site(original_file_sp->GetSpecOnly(), line);
  if (include_inlined_functions)
    symbol_context_scope |= lldb::eSymbolContextBlock;

  while (true) {
    SymbolContext next_line_sc;
    Address range_end(complete_line_range.GetBaseAddress());
    range_end.Slide(complete_line_range.GetByteSize());
    range_end.CalculateSymbolContext(&next_line_sc, symbol_context_scope);

    if (!next_line_sc.line_entry.IsValid() ||
        next_line_sc.line_entry.range.GetByteSize() == 0)
      break;

    if (original_file_sp->Equal(*next_line_sc.line_entry.original_file_sp,
                                SupportFile::eEqualFileSpecAndChecksumIfSet) &&
        (next_line_sc.line_entry.line == 0 ||
         line == next_line_sc.line_entry.line)) {
      // Include any line 0 entries - they indicate that this is compiler-
      // generated code that does not correspond to user source code.
      // next_line_sc is the same file & line as this LineEntry, so extend
      // our AddressRange by its size and continue to see if there are more
      // LineEntries that we can combine. However, if there was nothing to
      // extend we're done.
      if (!complete_line_range.Extend(next_line_sc.line_entry.range))
        break;
      continue;
    }

    if (include_inlined_functions && next_line_sc.block &&
        next_line_sc.block->GetContainingInlinedBlock() != nullptr) {
      // The next_line_sc might be in a different file if it's an inlined
      // function. If this is the case then we still want to expand our line
      // range to include them if the inlined function is at the same call site
      // as this line entry. The current block could represent a nested inline
      // function call so we need to need to check up the block tree to see if
      // we find one.
      auto inlined_parent_block =
          next_line_sc.block->GetContainingInlinedBlockWithCallSite(
              start_call_site);
      if (!inlined_parent_block)
        // We didn't find any parent inlined block with a call site at this line
        // entry so this inlined function is probably at another line.
        break;
      // Extend our AddressRange by the size of the inlined block, but if there
      // was nothing to add then we're done.
      if (!complete_line_range.Extend(next_line_sc.line_entry.range))
        break;
      continue;
    }

    break;
  }
  return complete_line_range;
}

void LineEntry::ApplyFileMappings(lldb::TargetSP target_sp) {
  if (target_sp) {
    // Apply any file remappings to our file.
    if (auto new_file_spec = target_sp->GetSourcePathMap().FindFile(
            original_file_sp->GetSpecOnly())) {
      file_sp = std::make_shared<SupportFile>(*new_file_spec,
                                              original_file_sp->GetChecksum());
    }
  }
}
