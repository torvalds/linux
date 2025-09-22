//===-- sanitizer_coverage_win_sections.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines delimiters for Sanitizer Coverage's section. It contains
// Windows specific tricks to coax the linker into giving us the start and stop
// addresses of a section, as ELF linkers can do, to get the size of certain
// arrays. According to https://msdn.microsoft.com/en-us/library/7977wcck.aspx
// sections with the same name before "$" are sorted alphabetically by the
// string that comes after "$" and merged into one section. We take advantage
// of this by putting data we want the size of into the middle (M) of a section,
// by using the letter "M" after "$". We get the start of this data (ie:
// __start_section_name) by making the start variable come at the start of the
// section (using the letter A after "$"). We do the same to get the end of the
// data by using the letter "Z" after "$" to make the end variable come after
// the data. Note that because of our technique the address of the start
// variable is actually the address of data that comes before our middle
// section. We also need to prevent the linker from adding any padding. Each
// technique we use for this is explained in the comments below.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_WINDOWS
#include <stdint.h>

extern "C" {
// Use uint64_t so the linker won't need to add any padding if it tries to word
// align the start of the 8-bit counters array. The array will always start 8
// bytes after __start_sancov_cntrs.
#pragma section(".SCOV$CA", read, write)
__declspec(allocate(".SCOV$CA")) uint64_t __start___sancov_cntrs = 0;

// Even though we said not to align __stop__sancov_cntrs (using the "align"
// declspec), MSVC's linker may try to align the section, .SCOV$CZ, containing
// it. This can cause a mismatch between the number of PCs and counters since
// each PCTable element is 8 bytes (unlike counters which are 1 byte) so no
// padding would be added to align .SCOVP$Z, However, if .SCOV$CZ section is 1
// byte, the linker won't try to align it on an 8-byte boundary, so use a
// uint8_t for __stop_sancov_cntrs.
#pragma section(".SCOV$CZ", read, write)
__declspec(allocate(".SCOV$CZ")) __declspec(align(1)) uint8_t
    __stop___sancov_cntrs = 0;

#pragma section(".SCOV$GA", read, write)
__declspec(allocate(".SCOV$GA")) uint64_t __start___sancov_guards = 0;
#pragma section(".SCOV$GZ", read, write)
__declspec(allocate(".SCOV$GZ")) __declspec(align(1)) uint8_t
    __stop___sancov_guards = 0;

// The guard array and counter array should both be merged into the .data
// section to reduce the number of PE sections. However, because PCTable is
// constant it should be merged with the .rdata section.
#pragma comment(linker, "/MERGE:.SCOV=.data")

#pragma section(".SCOVP$A", read)
__declspec(allocate(".SCOVP$A")) uint64_t __start___sancov_pcs = 0;
#pragma section(".SCOVP$Z", read)
__declspec(allocate(".SCOVP$Z")) __declspec(align(1)) uint8_t
    __stop___sancov_pcs = 0;

#pragma comment(linker, "/MERGE:.SCOVP=.rdata")
}
#endif  // SANITIZER_WINDOWS
