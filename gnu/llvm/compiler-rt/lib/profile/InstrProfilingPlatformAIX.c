/*===- InstrProfilingPlatformAIX.c - Profile data AIX platform ------------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#if defined(_AIX)

#ifdef __64BIT__
#define __XCOFF64__
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ldr.h>
#include <xcoff.h>

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"

#define BIN_ID_PREFIX "xcoff_binary_id:"

// If found, write the build-id into the Result buffer.
static size_t FindBinaryId(char *Result, size_t Size) {
  unsigned long EntryAddr = (unsigned long)__builtin_return_address(0);

  // Use loadquery to get information about loaded modules; loadquery writes
  // its result into a buffer of unknown size.
  char Buf[1024];
  size_t BufSize = sizeof(Buf);
  char *BufPtr = Buf;
  int RC = -1;

  errno = 0;
  RC = loadquery(L_GETXINFO | L_IGNOREUNLOAD, BufPtr, (unsigned int)BufSize);
  if (RC == -1 && errno == ENOMEM) {
    BufSize = 64000; // should be plenty for any program.
    BufPtr = malloc(BufSize);
    if (BufPtr != 0)
      RC = loadquery(L_GETXINFO | L_IGNOREUNLOAD, BufPtr, (unsigned int)BufSize);
  }

  if (RC == -1)
    goto done;

  // Locate the ld_xinfo corresponding to this module.
  struct ld_xinfo *CurInfo = (struct ld_xinfo *)BufPtr;
  while (1) {
    unsigned long CurTextStart = (uint64_t)CurInfo->ldinfo_textorg;
    unsigned long CurTextEnd = CurTextStart + CurInfo->ldinfo_textsize;
    if (CurTextStart <= EntryAddr && EntryAddr < CurTextEnd) {
      // Found my slot. Now search for the build-id.
      char *p = (char *)CurInfo->ldinfo_textorg;

      FILHDR *f = (FILHDR *)p;
      AOUTHDR *a = (AOUTHDR *)(p + FILHSZ);
      SCNHDR *s =
          (SCNHDR *)(p + FILHSZ + f->f_opthdr + SCNHSZ * (a->o_snloader - 1));
      LDHDR *ldhdr = (LDHDR *)(p + s->s_scnptr);
      // This is the loader string table
      char *lstr = (char *)ldhdr + ldhdr->l_stoff;

      // If the build-id exists, it's the first entry.
      // Each entry is comprised of a 2-byte size component, followed by the
      // data.
      size_t len = *(short *)lstr;
      char *str = (char *)(lstr + 2);
      size_t PrefixLen = sizeof(BIN_ID_PREFIX) - 1;
      if (len > PrefixLen && (len - PrefixLen) <= Size &&
          strncmp(str, BIN_ID_PREFIX, PrefixLen) == 0) {
        memcpy(Result, str + PrefixLen, len - PrefixLen);
        RC = len - PrefixLen;
        goto done;
      }
      break;
    }
    if (CurInfo->ldinfo_next == 0u)
      break;
    CurInfo = (struct ld_xinfo *)((char *)CurInfo + CurInfo->ldinfo_next);
  }
done:
  if (BufSize != sizeof(Buf) && BufPtr != 0)
    free(BufPtr);
  return RC;
}

static int StrToHexError = 0;
static uint8_t StrToHex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 0xa;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 0xa;
  StrToHexError = 1;
  return 0;
}

COMPILER_RT_VISIBILITY int __llvm_write_binary_ids(ProfDataWriter *Writer) {
  // 200 bytes should be enough for the build-id hex string.
  static char Buf[200];
  // Profile reading tools expect this to be 8-bytes long.
  static int64_t BinaryIdLen = 0;
  static uint8_t *BinaryIdData = 0;

  // -1 means we already checked for a BinaryId and didn't find one.
  if (BinaryIdLen == -1)
    return 0;

  // Are we being called for the first time?
  if (BinaryIdLen == 0) {
    if (getenv("LLVM_PROFILE_NO_BUILD_ID"))
      goto fail;

    int BuildIdLen = FindBinaryId(Buf, sizeof(Buf));
    if (BuildIdLen <= 0)
      goto fail;

    if (Buf[BuildIdLen - 1] == '\0')
      BuildIdLen--;

    // assume even number of digits/chars, so 0xabc must be 0x0abc
    if ((BuildIdLen % 2) != 0 || BuildIdLen == 0)
      goto fail;

    // The numeric ID is represented as an ascii string in the loader section,
    // so convert it to raw binary.
    BinaryIdLen = BuildIdLen / 2;
    BinaryIdData = (uint8_t *)Buf;

    // Skip "0x" prefix if it exists.
    if (Buf[0] == '0' && Buf[1] == 'x') {
      BinaryIdLen -= 1;
      BinaryIdData += 2;
    }

    StrToHexError = 0;
    for (int i = 0; i < BinaryIdLen; i++)
      BinaryIdData[i] = (StrToHex(BinaryIdData[2 * i]) << 4) +
                        StrToHex(BinaryIdData[2 * i + 1]);

    if (StrToHexError)
      goto fail;

    if (getenv("LLVM_PROFILE_VERBOSE")) {
      char *StrBuf = (char *)COMPILER_RT_ALLOCA(2 * BinaryIdLen + 1);
      for (int i = 0; i < (int)BinaryIdLen; i++)
        sprintf(&StrBuf[2 * i], "%02x", BinaryIdData[i]);
      PROF_NOTE("Writing binary id: %s\n", StrBuf);
    }
  }

  uint8_t BinaryIdPadding = __llvm_profile_get_num_padding_bytes(BinaryIdLen);
  if (Writer && lprofWriteOneBinaryId(Writer, BinaryIdLen, BinaryIdData,
                                      BinaryIdPadding) == -1)
    return -1; // Return -1 rather goto fail to match the NT_GNU_BUILD_ID path.

  return sizeof(BinaryIdLen) + BinaryIdLen + BinaryIdPadding;

fail:
  if (getenv("LLVM_PROFILE_VERBOSE"))
    fprintf(stderr, "no or invalid binary id: %.*s\n", (int)sizeof(Buf), Buf);
  BinaryIdLen = -1;
  return 0;
}

// Empty stubs to allow linking object files using the registration-based scheme
COMPILER_RT_VISIBILITY
void __llvm_profile_register_function(void *Data_) {}

COMPILER_RT_VISIBILITY
void __llvm_profile_register_names_function(void *NamesStart,
                                            uint64_t NamesSize) {}

// The __start_SECNAME and __stop_SECNAME symbols (for SECNAME \in
// {"__llvm_prf_cnts", "__llvm_prf_data", "__llvm_prf_name", "__llvm_prf_vnds",
// "__llvm_prf_vns", "__llvm_prf_vtab"})
// are always live when linking on AIX, regardless if the .o's being linked
// reference symbols from the profile library (for example when no files were
// compiled with -fprofile-generate). That's because these symbols are kept
// alive through references in constructor functions that are always live in the
// default linking model on AIX (-bcdtors:all). The __start_SECNAME and
// __stop_SECNAME symbols are only resolved by the linker when the SECNAME
// section exists. So for the scenario where the user objects have no such
// section (i.e. when they are compiled with -fno-profile-generate), we always
// define these zero length variables in each of the above 4 sections.
static int dummy_cnts[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_CNTS_SECT_NAME);
static int dummy_bits[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_BITS_SECT_NAME);
static int dummy_data[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_DATA_SECT_NAME);
static const int dummy_name[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_NAME_SECT_NAME);
static int dummy_vnds[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_VNODES_SECT_NAME);
static int dummy_orderfile[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_ORDERFILE_SECT_NAME);
static int dummy_vname[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_VNAME_SECT_NAME);
static int dummy_vtab[0] COMPILER_RT_SECTION(
    COMPILER_RT_SEG INSTR_PROF_VTAB_SECT_NAME);

// To avoid GC'ing of the dummy variables by the linker, reference them in an
// array and reference the array in the runtime registration code
// (InstrProfilingRuntime.cpp)
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
COMPILER_RT_VISIBILITY
void *__llvm_profile_keep[] = {(void *)&dummy_cnts,  (void *)&dummy_bits,
                               (void *)&dummy_data,  (void *)&dummy_name,
                               (void *)&dummy_vnds,  (void *)&dummy_orderfile,
                               (void *)&dummy_vname, (void *)&dummy_vtab};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#endif
