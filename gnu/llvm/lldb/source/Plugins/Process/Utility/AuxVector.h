//===-- AuxVector.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_AUXVECTOR_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_AUXVECTOR_H

#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include <optional>
#include <unordered_map>

class AuxVector {

public:
  AuxVector(const lldb_private::DataExtractor &data);

  /// Constants describing the type of entry.
  /// On Linux and FreeBSD, running "LD_SHOW_AUXV=1 ./executable" will spew AUX
  /// information. Added AUXV prefix to avoid potential conflicts with system-
  /// defined macros. For FreeBSD, the numbers can be found in sys/elf_common.h.
  enum EntryType {
    AUXV_AT_NULL = 0,    ///< End of auxv.
    AUXV_AT_IGNORE = 1,  ///< Ignore entry.
    AUXV_AT_EXECFD = 2,  ///< File descriptor of program.
    AUXV_AT_PHDR = 3,    ///< Program headers.
    AUXV_AT_PHENT = 4,   ///< Size of program header.
    AUXV_AT_PHNUM = 5,   ///< Number of program headers.
    AUXV_AT_PAGESZ = 6,  ///< Page size.
    AUXV_AT_BASE = 7,    ///< Interpreter base address.
    AUXV_AT_FLAGS = 8,   ///< Flags.
    AUXV_AT_ENTRY = 9,   ///< Program entry point.
    AUXV_AT_NOTELF = 10, ///< Set if program is not an ELF.
    AUXV_AT_UID = 11,    ///< UID.
    AUXV_AT_EUID = 12,   ///< Effective UID.
    AUXV_AT_GID = 13,    ///< GID.
    AUXV_AT_EGID = 14,   ///< Effective GID.

    // At this point Linux and FreeBSD diverge and many of the following values
    // are Linux specific. If you use them make sure you are in Linux specific
    // code or they have the same value on other platforms.

    AUXV_AT_CLKTCK = 17,   ///< Clock frequency (e.g. times(2)).
    AUXV_AT_PLATFORM = 15, ///< String identifying platform.
    AUXV_AT_HWCAP =
        16, ///< Machine dependent hints about processor capabilities.
    AUXV_AT_FPUCW = 18,         ///< Used FPU control word.
    AUXV_AT_DCACHEBSIZE = 19,   ///< Data cache block size.
    AUXV_AT_ICACHEBSIZE = 20,   ///< Instruction cache block size.
    AUXV_AT_UCACHEBSIZE = 21,   ///< Unified cache block size.
    AUXV_AT_IGNOREPPC = 22,     ///< Entry should be ignored.
    AUXV_AT_SECURE = 23,        ///< Boolean, was exec setuid-like?
    AUXV_AT_BASE_PLATFORM = 24, ///< String identifying real platforms.
    AUXV_AT_RANDOM = 25,        ///< Address of 16 random bytes.
    AUXV_AT_HWCAP2 = 26,        ///< Extension of AT_HWCAP.
    AUXV_AT_EXECFN = 31,        ///< Filename of executable.
    AUXV_AT_SYSINFO = 32, ///< Pointer to the global system page used for system
                          /// calls and other nice things.
    AUXV_AT_SYSINFO_EHDR = 33,
    AUXV_AT_L1I_CACHESHAPE = 34, ///< Shapes of the caches.
    AUXV_AT_L1D_CACHESHAPE = 35,
    AUXV_AT_L2_CACHESHAPE = 36,
    AUXV_AT_L3_CACHESHAPE = 37,

    // Platform specific values which may overlap the Linux values.

    AUXV_FREEBSD_AT_HWCAP = 25, ///< FreeBSD specific AT_HWCAP value.
  };

  std::optional<uint64_t> GetAuxValue(enum EntryType entry_type) const;
  void DumpToLog(lldb_private::Log *log) const;
  const char *GetEntryName(EntryType type) const;

private:
  void ParseAuxv(const lldb_private::DataExtractor &data);

  std::unordered_map<uint64_t, uint64_t> m_auxv_entries;
};

#endif
