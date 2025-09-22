//===-- LinuxProcMaps.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinuxProcMaps.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StringExtractor.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

using namespace lldb_private;

enum class MapsKind { Maps, SMaps };

static llvm::Expected<MemoryRegionInfo> ProcMapError(const char *msg,
                                                     MapsKind kind) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(), msg,
                                 kind == MapsKind::Maps ? "maps" : "smaps");
}

static llvm::Expected<MemoryRegionInfo>
ParseMemoryRegionInfoFromProcMapsLine(llvm::StringRef maps_line,
                                      MapsKind maps_kind) {
  MemoryRegionInfo region;
  StringExtractor line_extractor(maps_line);

  // Format: {address_start_hex}-{address_end_hex} perms offset  dev   inode
  // pathname perms: rwxp   (letter is present if set, '-' if not, final
  // character is p=private, s=shared).

  // Parse out the starting address
  lldb::addr_t start_address = line_extractor.GetHexMaxU64(false, 0);

  // Parse out hyphen separating start and end address from range.
  if (!line_extractor.GetBytesLeft() || (line_extractor.GetChar() != '-'))
    return ProcMapError(
        "malformed /proc/{pid}/%s entry, missing dash between address range",
        maps_kind);

  // Parse out the ending address
  lldb::addr_t end_address = line_extractor.GetHexMaxU64(false, start_address);

  // Parse out the space after the address.
  if (!line_extractor.GetBytesLeft() || (line_extractor.GetChar() != ' '))
    return ProcMapError(
        "malformed /proc/{pid}/%s entry, missing space after range", maps_kind);

  // Save the range.
  region.GetRange().SetRangeBase(start_address);
  region.GetRange().SetRangeEnd(end_address);

  // Any memory region in /proc/{pid}/(maps|smaps) is by definition mapped
  // into the process.
  region.SetMapped(MemoryRegionInfo::OptionalBool::eYes);

  // Parse out each permission entry.
  if (line_extractor.GetBytesLeft() < 4)
    return ProcMapError(
        "malformed /proc/{pid}/%s entry, missing some portion of "
        "permissions",
        maps_kind);

  // Handle read permission.
  const char read_perm_char = line_extractor.GetChar();
  if (read_perm_char == 'r')
    region.SetReadable(MemoryRegionInfo::OptionalBool::eYes);
  else if (read_perm_char == '-')
    region.SetReadable(MemoryRegionInfo::OptionalBool::eNo);
  else
    return ProcMapError("unexpected /proc/{pid}/%s read permission char",
                        maps_kind);

  // Handle write permission.
  const char write_perm_char = line_extractor.GetChar();
  if (write_perm_char == 'w')
    region.SetWritable(MemoryRegionInfo::OptionalBool::eYes);
  else if (write_perm_char == '-')
    region.SetWritable(MemoryRegionInfo::OptionalBool::eNo);
  else
    return ProcMapError("unexpected /proc/{pid}/%s write permission char",
                        maps_kind);

  // Handle execute permission.
  const char exec_perm_char = line_extractor.GetChar();
  if (exec_perm_char == 'x')
    region.SetExecutable(MemoryRegionInfo::OptionalBool::eYes);
  else if (exec_perm_char == '-')
    region.SetExecutable(MemoryRegionInfo::OptionalBool::eNo);
  else
    return ProcMapError("unexpected /proc/{pid}/%s exec permission char",
                        maps_kind);

  // Handle sharing status (private/shared).
  const char sharing_char = line_extractor.GetChar();
  if (sharing_char == 's')
    region.SetShared(MemoryRegionInfo::OptionalBool::eYes);
  else if (sharing_char == 'p')
    region.SetShared(MemoryRegionInfo::OptionalBool::eNo);
  else
    region.SetShared(MemoryRegionInfo::OptionalBool::eDontKnow);

  line_extractor.SkipSpaces();           // Skip the separator
  line_extractor.GetHexMaxU64(false, 0); // Read the offset
  line_extractor.GetHexMaxU64(false, 0); // Read the major device number
  line_extractor.GetChar();              // Read the device id separator
  line_extractor.GetHexMaxU64(false, 0); // Read the major device number
  line_extractor.SkipSpaces();           // Skip the separator
  line_extractor.GetU64(0, 10);          // Read the inode number

  line_extractor.SkipSpaces();
  const char *name = line_extractor.Peek();
  if (name)
    region.SetName(name);

  return region;
}

void lldb_private::ParseLinuxMapRegions(llvm::StringRef linux_map,
                                        LinuxMapCallback const &callback) {
  llvm::StringRef lines(linux_map);
  llvm::StringRef line;
  while (!lines.empty()) {
    std::tie(line, lines) = lines.split('\n');
    if (!callback(ParseMemoryRegionInfoFromProcMapsLine(line, MapsKind::Maps)))
      break;
  }
}

void lldb_private::ParseLinuxSMapRegions(llvm::StringRef linux_smap,
                                         LinuxMapCallback const &callback) {
  // Entries in /smaps look like:
  // 00400000-0048a000 r-xp 00000000 fd:03 960637
  // Size:                552 kB
  // Rss:                 460 kB
  // <...>
  // VmFlags: rd ex mr mw me dw
  // 00500000-0058a000 rwxp 00000000 fd:03 960637
  // <...>
  //
  // Where the first line is identical to the /maps format
  // and VmFlags is only printed for kernels >= 3.8.

  llvm::StringRef lines(linux_smap);
  llvm::StringRef line;
  std::optional<MemoryRegionInfo> region;

  while (lines.size()) {
    std::tie(line, lines) = lines.split('\n');

    // A property line looks like:
    // <word>: <value>
    // (no spaces on the left hand side)
    // A header will have a ':' but the LHS will contain spaces
    llvm::StringRef name;
    llvm::StringRef value;
    std::tie(name, value) = line.split(':');

    // If this line is a property line
    if (!name.contains(' ')) {
      if (region) {
        if (name == "VmFlags") {
          if (value.contains("mt"))
            region->SetMemoryTagged(MemoryRegionInfo::eYes);
          else
            region->SetMemoryTagged(MemoryRegionInfo::eNo);
        }
        // Ignore anything else
      } else {
        // Orphaned settings line
        callback(ProcMapError(
            "Found a property line without a corresponding mapping "
            "in /proc/{pid}/%s",
            MapsKind::SMaps));
        return;
      }
    } else {
      // Must be a new region header
      if (region) {
        // Save current region
        callback(*region);
        region.reset();
      }

      // Try to start a new region
      llvm::Expected<MemoryRegionInfo> new_region =
          ParseMemoryRegionInfoFromProcMapsLine(line, MapsKind::SMaps);
      if (new_region) {
        region = *new_region;
      } else {
        // Stop at first invalid region header
        callback(new_region.takeError());
        return;
      }
    }
  }

  // Catch last region
  if (region)
    callback(*region);
}
