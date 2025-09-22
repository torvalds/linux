/*===- InstrProfilingFile.c - Write instrumentation to a file -------------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#if !defined(__Fuchsia__)

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
/* For _alloca. */
#include <malloc.h>
#endif
#if defined(_WIN32)
#include "WindowsMMap.h"
/* For _chsize_s */
#include <io.h>
#include <process.h>
#else
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/types.h>
#endif
#endif

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"
#include "InstrProfilingPort.h"
#include "InstrProfilingUtil.h"

/* From where is profile name specified.
 * The order the enumerators define their
 * precedence. Re-order them may lead to
 * runtime behavior change. */
typedef enum ProfileNameSpecifier {
  PNS_unknown = 0,
  PNS_default,
  PNS_command_line,
  PNS_environment,
  PNS_runtime_api
} ProfileNameSpecifier;

static const char *getPNSStr(ProfileNameSpecifier PNS) {
  switch (PNS) {
  case PNS_default:
    return "default setting";
  case PNS_command_line:
    return "command line";
  case PNS_environment:
    return "environment variable";
  case PNS_runtime_api:
    return "runtime API";
  default:
    return "Unknown";
  }
}

#define MAX_PID_SIZE 16
/* Data structure holding the result of parsed filename pattern. */
typedef struct lprofFilename {
  /* File name string possibly with %p or %h specifiers. */
  const char *FilenamePat;
  /* A flag indicating if FilenamePat's memory is allocated
   * by runtime. */
  unsigned OwnsFilenamePat;
  const char *ProfilePathPrefix;
  char PidChars[MAX_PID_SIZE];
  char *TmpDir;
  char Hostname[COMPILER_RT_MAX_HOSTLEN];
  unsigned NumPids;
  unsigned NumHosts;
  /* When in-process merging is enabled, this parameter specifies
   * the total number of profile data files shared by all the processes
   * spawned from the same binary. By default the value is 1. If merging
   * is not enabled, its value should be 0. This parameter is specified
   * by the %[0-9]m specifier. For instance %2m enables merging using
   * 2 profile data files. %1m is equivalent to %m. Also %m specifier
   * can only appear once at the end of the name pattern. */
  unsigned MergePoolSize;
  ProfileNameSpecifier PNS;
} lprofFilename;

static lprofFilename lprofCurFilename = {0,   0, 0, {0}, NULL,
                                         {0}, 0, 0, 0,   PNS_unknown};

static int ProfileMergeRequested = 0;
static int getProfileFileSizeForMerging(FILE *ProfileFile,
                                        uint64_t *ProfileFileSize);

#if defined(__APPLE__)
static const int ContinuousModeSupported = 1;
static const int UseBiasVar = 0;
static const char *FileOpenMode = "a+b";
static void *BiasAddr = NULL;
static void *BiasDefaultAddr = NULL;
static int mmapForContinuousMode(uint64_t CurrentFileOffset, FILE *File) {
  /* Get the sizes of various profile data sections. Taken from
   * __llvm_profile_get_size_for_buffer(). */
  const __llvm_profile_data *DataBegin = __llvm_profile_begin_data();
  const __llvm_profile_data *DataEnd = __llvm_profile_end_data();
  const char *CountersBegin = __llvm_profile_begin_counters();
  const char *CountersEnd = __llvm_profile_end_counters();
  const char *BitmapBegin = __llvm_profile_begin_bitmap();
  const char *BitmapEnd = __llvm_profile_end_bitmap();
  const char *NamesBegin = __llvm_profile_begin_names();
  const char *NamesEnd = __llvm_profile_end_names();
  const uint64_t NamesSize = (NamesEnd - NamesBegin) * sizeof(char);
  uint64_t DataSize = __llvm_profile_get_data_size(DataBegin, DataEnd);
  uint64_t CountersSize =
      __llvm_profile_get_counters_size(CountersBegin, CountersEnd);
  uint64_t NumBitmapBytes =
      __llvm_profile_get_num_bitmap_bytes(BitmapBegin, BitmapEnd);

  /* Check that the counter, bitmap, and data sections in this image are
   * page-aligned. */
  unsigned PageSize = getpagesize();
  if ((intptr_t)CountersBegin % PageSize != 0) {
    PROF_ERR("Counters section not page-aligned (start = %p, pagesz = %u).\n",
             CountersBegin, PageSize);
    return 1;
  }
  if ((intptr_t)BitmapBegin % PageSize != 0) {
    PROF_ERR("Bitmap section not page-aligned (start = %p, pagesz = %u).\n",
             BitmapBegin, PageSize);
    return 1;
  }
  if ((intptr_t)DataBegin % PageSize != 0) {
    PROF_ERR("Data section not page-aligned (start = %p, pagesz = %u).\n",
             DataBegin, PageSize);
    return 1;
  }

  int Fileno = fileno(File);
  /* Determine how much padding is needed before/after the counters and
   * after the names. */
  uint64_t PaddingBytesBeforeCounters, PaddingBytesAfterCounters,
      PaddingBytesAfterNames, PaddingBytesAfterBitmapBytes,
      PaddingBytesAfterVTable, PaddingBytesAfterVNames;
  __llvm_profile_get_padding_sizes_for_counters(
      DataSize, CountersSize, NumBitmapBytes, NamesSize, /*VTableSize=*/0,
      /*VNameSize=*/0, &PaddingBytesBeforeCounters, &PaddingBytesAfterCounters,
      &PaddingBytesAfterBitmapBytes, &PaddingBytesAfterNames,
      &PaddingBytesAfterVTable, &PaddingBytesAfterVNames);

  uint64_t PageAlignedCountersLength = CountersSize + PaddingBytesAfterCounters;
  uint64_t FileOffsetToCounters = CurrentFileOffset +
                                  sizeof(__llvm_profile_header) + DataSize +
                                  PaddingBytesBeforeCounters;
  void *CounterMmap = mmap((void *)CountersBegin, PageAlignedCountersLength,
                           PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
                           Fileno, FileOffsetToCounters);
  if (CounterMmap != CountersBegin) {
    PROF_ERR(
        "Continuous counter sync mode is enabled, but mmap() failed (%s).\n"
        "  - CountersBegin: %p\n"
        "  - PageAlignedCountersLength: %" PRIu64 "\n"
        "  - Fileno: %d\n"
        "  - FileOffsetToCounters: %" PRIu64 "\n",
        strerror(errno), CountersBegin, PageAlignedCountersLength, Fileno,
        FileOffsetToCounters);
    return 1;
  }

  /* Also mmap MCDC bitmap bytes. If there aren't any bitmap bytes, mmap()
   * will fail with EINVAL. */
  if (NumBitmapBytes == 0)
    return 0;

  uint64_t PageAlignedBitmapLength =
      NumBitmapBytes + PaddingBytesAfterBitmapBytes;
  uint64_t FileOffsetToBitmap =
      FileOffsetToCounters + CountersSize + PaddingBytesAfterCounters;
  void *BitmapMmap =
      mmap((void *)BitmapBegin, PageAlignedBitmapLength, PROT_READ | PROT_WRITE,
           MAP_FIXED | MAP_SHARED, Fileno, FileOffsetToBitmap);
  if (BitmapMmap != BitmapBegin) {
    PROF_ERR(
        "Continuous counter sync mode is enabled, but mmap() failed (%s).\n"
        "  - BitmapBegin: %p\n"
        "  - PageAlignedBitmapLength: %" PRIu64 "\n"
        "  - Fileno: %d\n"
        "  - FileOffsetToBitmap: %" PRIu64 "\n",
        strerror(errno), BitmapBegin, PageAlignedBitmapLength, Fileno,
        FileOffsetToBitmap);
    return 1;
  }
  return 0;
}
#elif defined(__ELF__) || defined(_WIN32)

#define INSTR_PROF_PROFILE_COUNTER_BIAS_DEFAULT_VAR                            \
  INSTR_PROF_CONCAT(INSTR_PROF_PROFILE_COUNTER_BIAS_VAR, _default)
COMPILER_RT_VISIBILITY intptr_t INSTR_PROF_PROFILE_COUNTER_BIAS_DEFAULT_VAR = 0;

/* This variable is a weak external reference which could be used to detect
 * whether or not the compiler defined this symbol. */
#if defined(_MSC_VER)
COMPILER_RT_VISIBILITY extern intptr_t INSTR_PROF_PROFILE_COUNTER_BIAS_VAR;
#if defined(_M_IX86) || defined(__i386__)
#define WIN_SYM_PREFIX "_"
#else
#define WIN_SYM_PREFIX
#endif
#pragma comment(                                                               \
    linker, "/alternatename:" WIN_SYM_PREFIX INSTR_PROF_QUOTE(                 \
                INSTR_PROF_PROFILE_COUNTER_BIAS_VAR) "=" WIN_SYM_PREFIX        \
                INSTR_PROF_QUOTE(INSTR_PROF_PROFILE_COUNTER_BIAS_DEFAULT_VAR))
#else
COMPILER_RT_VISIBILITY extern intptr_t INSTR_PROF_PROFILE_COUNTER_BIAS_VAR
    __attribute__((weak, alias(INSTR_PROF_QUOTE(
                             INSTR_PROF_PROFILE_COUNTER_BIAS_DEFAULT_VAR))));
#endif
static const int ContinuousModeSupported = 1;
static const int UseBiasVar = 1;
/* TODO: If there are two DSOs, the second DSO initilization will truncate the
 * first profile file. */
static const char *FileOpenMode = "w+b";
/* This symbol is defined by the compiler when runtime counter relocation is
 * used and runtime provides a weak alias so we can check if it's defined. */
static void *BiasAddr = &INSTR_PROF_PROFILE_COUNTER_BIAS_VAR;
static void *BiasDefaultAddr = &INSTR_PROF_PROFILE_COUNTER_BIAS_DEFAULT_VAR;
static int mmapForContinuousMode(uint64_t CurrentFileOffset, FILE *File) {
  /* Get the sizes of various profile data sections. Taken from
   * __llvm_profile_get_size_for_buffer(). */
  const __llvm_profile_data *DataBegin = __llvm_profile_begin_data();
  const __llvm_profile_data *DataEnd = __llvm_profile_end_data();
  const char *CountersBegin = __llvm_profile_begin_counters();
  const char *CountersEnd = __llvm_profile_end_counters();
  const char *BitmapBegin = __llvm_profile_begin_bitmap();
  const char *BitmapEnd = __llvm_profile_end_bitmap();
  uint64_t DataSize = __llvm_profile_get_data_size(DataBegin, DataEnd);
  /* Get the file size. */
  uint64_t FileSize = 0;
  if (getProfileFileSizeForMerging(File, &FileSize))
    return 1;

  int Fileno = fileno(File);
  uint64_t FileOffsetToCounters =
      sizeof(__llvm_profile_header) + __llvm_write_binary_ids(NULL) + DataSize;

  /* Map the profile. */
  char *Profile = (char *)mmap(NULL, FileSize, PROT_READ | PROT_WRITE,
                               MAP_SHARED, Fileno, 0);
  if (Profile == MAP_FAILED) {
    PROF_ERR("Unable to mmap profile: %s\n", strerror(errno));
    return 1;
  }
  /* Update the profile fields based on the current mapping. */
  INSTR_PROF_PROFILE_COUNTER_BIAS_VAR =
      (intptr_t)Profile - (uintptr_t)CountersBegin + FileOffsetToCounters;

  /* Return the memory allocated for counters to OS. */
  lprofReleaseMemoryPagesToOS((uintptr_t)CountersBegin, (uintptr_t)CountersEnd);

  /* BIAS MODE not supported yet for Bitmap (MCDC). */

  /* Return the memory allocated for counters to OS. */
  lprofReleaseMemoryPagesToOS((uintptr_t)BitmapBegin, (uintptr_t)BitmapEnd);
  return 0;
}
#else
static const int ContinuousModeSupported = 0;
static const int UseBiasVar = 0;
static const char *FileOpenMode = "a+b";
static void *BiasAddr = NULL;
static void *BiasDefaultAddr = NULL;
static int mmapForContinuousMode(uint64_t CurrentFileOffset, FILE *File) {
  return 0;
}
#endif

static int isProfileMergeRequested(void) { return ProfileMergeRequested; }
static void setProfileMergeRequested(int EnableMerge) {
  ProfileMergeRequested = EnableMerge;
}

static FILE *ProfileFile = NULL;
static FILE *getProfileFile(void) { return ProfileFile; }
static void setProfileFile(FILE *File) { ProfileFile = File; }

static int getCurFilenameLength(void);
static const char *getCurFilename(char *FilenameBuf, int ForceUseBuf);
static unsigned doMerging(void) {
  return lprofCurFilename.MergePoolSize || isProfileMergeRequested();
}

/* Return 1 if there is an error, otherwise return  0.  */
static uint32_t fileWriter(ProfDataWriter *This, ProfDataIOVec *IOVecs,
                           uint32_t NumIOVecs) {
  uint32_t I;
  FILE *File = (FILE *)This->WriterCtx;
  char Zeroes[sizeof(uint64_t)] = {0};
  for (I = 0; I < NumIOVecs; I++) {
    if (IOVecs[I].Data) {
      if (fwrite(IOVecs[I].Data, IOVecs[I].ElmSize, IOVecs[I].NumElm, File) !=
          IOVecs[I].NumElm)
        return 1;
    } else if (IOVecs[I].UseZeroPadding) {
      size_t BytesToWrite = IOVecs[I].ElmSize * IOVecs[I].NumElm;
      while (BytesToWrite > 0) {
        size_t PartialWriteLen =
            (sizeof(uint64_t) > BytesToWrite) ? BytesToWrite : sizeof(uint64_t);
        if (fwrite(Zeroes, sizeof(uint8_t), PartialWriteLen, File) !=
            PartialWriteLen) {
          return 1;
        }
        BytesToWrite -= PartialWriteLen;
      }
    } else {
      if (fseek(File, IOVecs[I].ElmSize * IOVecs[I].NumElm, SEEK_CUR) == -1)
        return 1;
    }
  }
  return 0;
}

/* TODO: make buffer size controllable by an internal option, and compiler can pass the size
   to runtime via a variable. */
static uint32_t orderFileWriter(FILE *File, const uint32_t *DataStart) {
  if (fwrite(DataStart, sizeof(uint32_t), INSTR_ORDER_FILE_BUFFER_SIZE, File) !=
      INSTR_ORDER_FILE_BUFFER_SIZE)
    return 1;
  return 0;
}

static void initFileWriter(ProfDataWriter *This, FILE *File) {
  This->Write = fileWriter;
  This->WriterCtx = File;
}

COMPILER_RT_VISIBILITY ProfBufferIO *
lprofCreateBufferIOInternal(void *File, uint32_t BufferSz) {
  FreeHook = &free;
  DynamicBufferIOBuffer = (uint8_t *)calloc(1, BufferSz);
  VPBufferSize = BufferSz;
  ProfDataWriter *fileWriter =
      (ProfDataWriter *)calloc(1, sizeof(ProfDataWriter));
  initFileWriter(fileWriter, File);
  ProfBufferIO *IO = lprofCreateBufferIO(fileWriter);
  IO->OwnFileWriter = 1;
  return IO;
}

static void setupIOBuffer(void) {
  const char *BufferSzStr = 0;
  BufferSzStr = getenv("LLVM_VP_BUFFER_SIZE");
  if (BufferSzStr && BufferSzStr[0]) {
    VPBufferSize = atoi(BufferSzStr);
    DynamicBufferIOBuffer = (uint8_t *)calloc(VPBufferSize, 1);
  }
}

/* Get the size of the profile file. If there are any errors, print the
 * message under the assumption that the profile is being read for merging
 * purposes, and return -1. Otherwise return the file size in the inout param
 * \p ProfileFileSize. */
static int getProfileFileSizeForMerging(FILE *ProfileFile,
                                        uint64_t *ProfileFileSize) {
  if (fseek(ProfileFile, 0L, SEEK_END) == -1) {
    PROF_ERR("Unable to merge profile data, unable to get size: %s\n",
             strerror(errno));
    return -1;
  }
  *ProfileFileSize = ftell(ProfileFile);

  /* Restore file offset.  */
  if (fseek(ProfileFile, 0L, SEEK_SET) == -1) {
    PROF_ERR("Unable to merge profile data, unable to rewind: %s\n",
             strerror(errno));
    return -1;
  }

  if (*ProfileFileSize > 0 &&
      *ProfileFileSize < sizeof(__llvm_profile_header)) {
    PROF_WARN("Unable to merge profile data: %s\n",
              "source profile file is too small.");
    return -1;
  }
  return 0;
}

/* mmap() \p ProfileFile for profile merging purposes, assuming that an
 * exclusive lock is held on the file and that \p ProfileFileSize is the
 * length of the file. Return the mmap'd buffer in the inout variable
 * \p ProfileBuffer. Returns -1 on failure. On success, the caller is
 * responsible for unmapping the mmap'd buffer in \p ProfileBuffer. */
static int mmapProfileForMerging(FILE *ProfileFile, uint64_t ProfileFileSize,
                                 char **ProfileBuffer) {
  *ProfileBuffer = mmap(NULL, ProfileFileSize, PROT_READ, MAP_SHARED | MAP_FILE,
                        fileno(ProfileFile), 0);
  if (*ProfileBuffer == MAP_FAILED) {
    PROF_ERR("Unable to merge profile data, mmap failed: %s\n",
             strerror(errno));
    return -1;
  }

  if (__llvm_profile_check_compatibility(*ProfileBuffer, ProfileFileSize)) {
    (void)munmap(*ProfileBuffer, ProfileFileSize);
    PROF_WARN("Unable to merge profile data: %s\n",
              "source profile file is not compatible.");
    return -1;
  }
  return 0;
}

/* Read profile data in \c ProfileFile and merge with in-memory
   profile counters. Returns -1 if there is fatal error, otheriwse
   0 is returned. Returning 0 does not mean merge is actually
   performed. If merge is actually done, *MergeDone is set to 1.
*/
static int doProfileMerging(FILE *ProfileFile, int *MergeDone) {
  uint64_t ProfileFileSize;
  char *ProfileBuffer;

  /* Get the size of the profile on disk. */
  if (getProfileFileSizeForMerging(ProfileFile, &ProfileFileSize) == -1)
    return -1;

  /* Nothing to merge.  */
  if (!ProfileFileSize)
    return 0;

  /* mmap() the profile and check that it is compatible with the data in
   * the current image. */
  if (mmapProfileForMerging(ProfileFile, ProfileFileSize, &ProfileBuffer) == -1)
    return -1;

  /* Now start merging */
  if (__llvm_profile_merge_from_buffer(ProfileBuffer, ProfileFileSize)) {
    PROF_ERR("%s\n", "Invalid profile data to merge");
    (void)munmap(ProfileBuffer, ProfileFileSize);
    return -1;
  }

  // Truncate the file in case merging of value profile did not happen to
  // prevent from leaving garbage data at the end of the profile file.
  (void)COMPILER_RT_FTRUNCATE(ProfileFile,
                              __llvm_profile_get_size_for_buffer());

  (void)munmap(ProfileBuffer, ProfileFileSize);
  *MergeDone = 1;

  return 0;
}

/* Create the directory holding the file, if needed. */
static void createProfileDir(const char *Filename) {
  size_t Length = strlen(Filename);
  if (lprofFindFirstDirSeparator(Filename)) {
    char *Copy = (char *)COMPILER_RT_ALLOCA(Length + 1);
    strncpy(Copy, Filename, Length + 1);
    __llvm_profile_recursive_mkdir(Copy);
  }
}

/* Open the profile data for merging. It opens the file in r+b mode with
 * file locking.  If the file has content which is compatible with the
 * current process, it also reads in the profile data in the file and merge
 * it with in-memory counters. After the profile data is merged in memory,
 * the original profile data is truncated and gets ready for the profile
 * dumper. With profile merging enabled, each executable as well as any of
 * its instrumented shared libraries dump profile data into their own data file.
*/
static FILE *openFileForMerging(const char *ProfileFileName, int *MergeDone) {
  FILE *ProfileFile = getProfileFile();
  int rc;
  // initializeProfileForContinuousMode will lock the profile, but if
  // ProfileFile is set by user via __llvm_profile_set_file_object, it's assumed
  // unlocked at this point.
  if (ProfileFile && !__llvm_profile_is_continuous_mode_enabled()) {
    lprofLockFileHandle(ProfileFile);
  }
  if (!ProfileFile) {
    createProfileDir(ProfileFileName);
    ProfileFile = lprofOpenFileEx(ProfileFileName);
  }
  if (!ProfileFile)
    return NULL;

  rc = doProfileMerging(ProfileFile, MergeDone);
  if (rc || (!*MergeDone && COMPILER_RT_FTRUNCATE(ProfileFile, 0L)) ||
      fseek(ProfileFile, 0L, SEEK_SET) == -1) {
    PROF_ERR("Profile Merging of file %s failed: %s\n", ProfileFileName,
             strerror(errno));
    fclose(ProfileFile);
    return NULL;
  }
  return ProfileFile;
}

static FILE *getFileObject(const char *OutputName) {
  FILE *File;
  File = getProfileFile();
  if (File != NULL) {
    return File;
  }

  return fopen(OutputName, "ab");
}

/* Write profile data to file \c OutputName.  */
static int writeFile(const char *OutputName) {
  int RetVal;
  FILE *OutputFile;

  int MergeDone = 0;
  VPMergeHook = &lprofMergeValueProfData;
  if (doMerging())
    OutputFile = openFileForMerging(OutputName, &MergeDone);
  else
    OutputFile = getFileObject(OutputName);

  if (!OutputFile)
    return -1;

  FreeHook = &free;
  setupIOBuffer();
  ProfDataWriter fileWriter;
  initFileWriter(&fileWriter, OutputFile);
  RetVal = lprofWriteData(&fileWriter, lprofGetVPDataReader(), MergeDone);

  if (OutputFile == getProfileFile()) {
    fflush(OutputFile);
    if (doMerging() && !__llvm_profile_is_continuous_mode_enabled()) {
      lprofUnlockFileHandle(OutputFile);
    }
  } else {
    fclose(OutputFile);
  }

  return RetVal;
}

/* Write order data to file \c OutputName.  */
static int writeOrderFile(const char *OutputName) {
  int RetVal;
  FILE *OutputFile;

  OutputFile = fopen(OutputName, "w");

  if (!OutputFile) {
    PROF_WARN("can't open file with mode ab: %s\n", OutputName);
    return -1;
  }

  FreeHook = &free;
  setupIOBuffer();
  const uint32_t *DataBegin = __llvm_profile_begin_orderfile();
  RetVal = orderFileWriter(OutputFile, DataBegin);

  fclose(OutputFile);
  return RetVal;
}

#define LPROF_INIT_ONCE_ENV "__LLVM_PROFILE_RT_INIT_ONCE"

static void truncateCurrentFile(void) {
  const char *Filename;
  char *FilenameBuf;
  FILE *File;
  int Length;

  Length = getCurFilenameLength();
  FilenameBuf = (char *)COMPILER_RT_ALLOCA(Length + 1);
  Filename = getCurFilename(FilenameBuf, 0);
  if (!Filename)
    return;

  /* Only create the profile directory and truncate an existing profile once.
   * In continuous mode, this is necessary, as the profile is written-to by the
   * runtime initializer. */
  int initialized = getenv(LPROF_INIT_ONCE_ENV) != NULL;
  if (initialized)
    return;
#if defined(_WIN32)
  _putenv(LPROF_INIT_ONCE_ENV "=" LPROF_INIT_ONCE_ENV);
#else
  setenv(LPROF_INIT_ONCE_ENV, LPROF_INIT_ONCE_ENV, 1);
#endif

  /* Create the profile dir (even if online merging is enabled), so that
   * the profile file can be set up if continuous mode is enabled. */
  createProfileDir(Filename);

  /* By pass file truncation to allow online raw profile merging. */
  if (lprofCurFilename.MergePoolSize)
    return;

  /* Truncate the file.  Later we'll reopen and append. */
  File = fopen(Filename, "w");
  if (!File)
    return;
  fclose(File);
}

/* Write a partial profile to \p Filename, which is required to be backed by
 * the open file object \p File. */
static int writeProfileWithFileObject(const char *Filename, FILE *File) {
  setProfileFile(File);
  int rc = writeFile(Filename);
  if (rc)
    PROF_ERR("Failed to write file \"%s\": %s\n", Filename, strerror(errno));
  setProfileFile(NULL);
  return rc;
}

static void initializeProfileForContinuousMode(void) {
  if (!__llvm_profile_is_continuous_mode_enabled())
    return;
  if (!ContinuousModeSupported) {
    PROF_ERR("%s\n", "continuous mode is unsupported on this platform");
    return;
  }
  if (UseBiasVar && BiasAddr == BiasDefaultAddr) {
    PROF_ERR("%s\n", "__llvm_profile_counter_bias is undefined");
    return;
  }

  /* Get the sizes of counter section. */
  uint64_t CountersSize = __llvm_profile_get_counters_size(
      __llvm_profile_begin_counters(), __llvm_profile_end_counters());

  int Length = getCurFilenameLength();
  char *FilenameBuf = (char *)COMPILER_RT_ALLOCA(Length + 1);
  const char *Filename = getCurFilename(FilenameBuf, 0);
  if (!Filename)
    return;

  FILE *File = NULL;
  uint64_t CurrentFileOffset = 0;
  if (doMerging()) {
    /* We are merging profiles. Map the counter section as shared memory into
     * the profile, i.e. into each participating process. An increment in one
     * process should be visible to every other process with the same counter
     * section mapped. */
    File = lprofOpenFileEx(Filename);
    if (!File)
      return;

    uint64_t ProfileFileSize = 0;
    if (getProfileFileSizeForMerging(File, &ProfileFileSize) == -1) {
      lprofUnlockFileHandle(File);
      fclose(File);
      return;
    }
    if (ProfileFileSize == 0) {
      /* Grow the profile so that mmap() can succeed.  Leak the file handle, as
       * the file should stay open. */
      if (writeProfileWithFileObject(Filename, File) != 0) {
        lprofUnlockFileHandle(File);
        fclose(File);
        return;
      }
    } else {
      /* The merged profile has a non-zero length. Check that it is compatible
       * with the data in this process. */
      char *ProfileBuffer;
      if (mmapProfileForMerging(File, ProfileFileSize, &ProfileBuffer) == -1) {
        lprofUnlockFileHandle(File);
        fclose(File);
        return;
      }
      (void)munmap(ProfileBuffer, ProfileFileSize);
    }
  } else {
    File = fopen(Filename, FileOpenMode);
    if (!File)
      return;
    /* Check that the offset within the file is page-aligned. */
    CurrentFileOffset = ftell(File);
    unsigned PageSize = getpagesize();
    if (CurrentFileOffset % PageSize != 0) {
      PROF_ERR("Continuous counter sync mode is enabled, but raw profile is not"
               "page-aligned. CurrentFileOffset = %" PRIu64 ", pagesz = %u.\n",
               (uint64_t)CurrentFileOffset, PageSize);
      fclose(File);
      return;
    }
    if (writeProfileWithFileObject(Filename, File) != 0) {
      fclose(File);
      return;
    }
  }

  /* mmap() the profile counters so long as there is at least one counter.
   * If there aren't any counters, mmap() would fail with EINVAL. */
  if (CountersSize > 0)
    mmapForContinuousMode(CurrentFileOffset, File);

  if (doMerging()) {
    lprofUnlockFileHandle(File);
  }
  if (File != NULL) {
    fclose(File);
  }
}

static const char *DefaultProfileName = "default.profraw";
static void resetFilenameToDefault(void) {
  if (lprofCurFilename.FilenamePat && lprofCurFilename.OwnsFilenamePat) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
    free((void *)lprofCurFilename.FilenamePat);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
  }
  memset(&lprofCurFilename, 0, sizeof(lprofCurFilename));
  lprofCurFilename.FilenamePat = DefaultProfileName;
  lprofCurFilename.PNS = PNS_default;
}

static unsigned getMergePoolSize(const char *FilenamePat, int *I) {
  unsigned J = 0, Num = 0;
  for (;; ++J) {
    char C = FilenamePat[*I + J];
    if (C == 'm') {
      *I += J;
      return Num ? Num : 1;
    }
    if (C < '0' || C > '9')
      break;
    Num = Num * 10 + C - '0';

    /* If FilenamePat[*I+J] is between '0' and '9', the next byte is guaranteed
     * to be in-bound as the string is null terminated. */
  }
  return 0;
}

/* Assert that Idx does index past a string null terminator. Return the
 * result of the check. */
static int checkBounds(int Idx, int Strlen) {
  assert(Idx <= Strlen && "Indexing past string null terminator");
  return Idx <= Strlen;
}

/* Parses the pattern string \p FilenamePat and stores the result to
 * lprofcurFilename structure. */
static int parseFilenamePattern(const char *FilenamePat,
                                unsigned CopyFilenamePat) {
  int NumPids = 0, NumHosts = 0, I;
  char *PidChars = &lprofCurFilename.PidChars[0];
  char *Hostname = &lprofCurFilename.Hostname[0];
  int MergingEnabled = 0;
  int FilenamePatLen = strlen(FilenamePat);

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
  /* Clean up cached prefix and filename.  */
  if (lprofCurFilename.ProfilePathPrefix)
    free((void *)lprofCurFilename.ProfilePathPrefix);

  if (lprofCurFilename.FilenamePat && lprofCurFilename.OwnsFilenamePat) {
    free((void *)lprofCurFilename.FilenamePat);
  }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

  memset(&lprofCurFilename, 0, sizeof(lprofCurFilename));

  if (!CopyFilenamePat)
    lprofCurFilename.FilenamePat = FilenamePat;
  else {
    lprofCurFilename.FilenamePat = strdup(FilenamePat);
    lprofCurFilename.OwnsFilenamePat = 1;
  }
  /* Check the filename for "%p", which indicates a pid-substitution. */
  for (I = 0; checkBounds(I, FilenamePatLen) && FilenamePat[I]; ++I) {
    if (FilenamePat[I] == '%') {
      ++I; /* Advance to the next character. */
      if (!checkBounds(I, FilenamePatLen))
        break;
      if (FilenamePat[I] == 'p') {
        if (!NumPids++) {
          if (snprintf(PidChars, MAX_PID_SIZE, "%ld", (long)getpid()) <= 0) {
            PROF_WARN("Unable to get pid for filename pattern %s. Using the "
                      "default name.",
                      FilenamePat);
            return -1;
          }
        }
      } else if (FilenamePat[I] == 'h') {
        if (!NumHosts++)
          if (COMPILER_RT_GETHOSTNAME(Hostname, COMPILER_RT_MAX_HOSTLEN)) {
            PROF_WARN("Unable to get hostname for filename pattern %s. Using "
                      "the default name.",
                      FilenamePat);
            return -1;
          }
      } else if (FilenamePat[I] == 't') {
        lprofCurFilename.TmpDir = getenv("TMPDIR");
        if (!lprofCurFilename.TmpDir) {
          PROF_WARN("Unable to get the TMPDIR environment variable, referenced "
                    "in %s. Using the default path.",
                    FilenamePat);
          return -1;
        }
      } else if (FilenamePat[I] == 'c') {
        if (__llvm_profile_is_continuous_mode_enabled()) {
          PROF_WARN("%%c specifier can only be specified once in %s.\n",
                    FilenamePat);
          __llvm_profile_disable_continuous_mode();
          return -1;
        }
#if defined(__APPLE__) || defined(__ELF__) || defined(_WIN32)
        __llvm_profile_set_page_size(getpagesize());
        __llvm_profile_enable_continuous_mode();
#else
        PROF_WARN("%s", "Continous mode is currently only supported for Mach-O,"
                        " ELF and COFF formats.");
        return -1;
#endif
      } else {
        unsigned MergePoolSize = getMergePoolSize(FilenamePat, &I);
        if (!MergePoolSize)
          continue;
        if (MergingEnabled) {
          PROF_WARN("%%m specifier can only be specified once in %s.\n",
                    FilenamePat);
          return -1;
        }
        MergingEnabled = 1;
        lprofCurFilename.MergePoolSize = MergePoolSize;
      }
    }
  }

  lprofCurFilename.NumPids = NumPids;
  lprofCurFilename.NumHosts = NumHosts;
  return 0;
}

static void parseAndSetFilename(const char *FilenamePat,
                                ProfileNameSpecifier PNS,
                                unsigned CopyFilenamePat) {

  const char *OldFilenamePat = lprofCurFilename.FilenamePat;
  ProfileNameSpecifier OldPNS = lprofCurFilename.PNS;

  /* The old profile name specifier takes precedence over the old one. */
  if (PNS < OldPNS)
    return;

  if (!FilenamePat)
    FilenamePat = DefaultProfileName;

  if (OldFilenamePat && !strcmp(OldFilenamePat, FilenamePat)) {
    lprofCurFilename.PNS = PNS;
    return;
  }

  /* When PNS >= OldPNS, the last one wins. */
  if (!FilenamePat || parseFilenamePattern(FilenamePat, CopyFilenamePat))
    resetFilenameToDefault();
  lprofCurFilename.PNS = PNS;

  if (!OldFilenamePat) {
    if (getenv("LLVM_PROFILE_VERBOSE"))
      PROF_NOTE("Set profile file path to \"%s\" via %s.\n",
                lprofCurFilename.FilenamePat, getPNSStr(PNS));
  } else {
    if (getenv("LLVM_PROFILE_VERBOSE"))
      PROF_NOTE("Override old profile path \"%s\" via %s to \"%s\" via %s.\n",
                OldFilenamePat, getPNSStr(OldPNS), lprofCurFilename.FilenamePat,
                getPNSStr(PNS));
  }

  truncateCurrentFile();
  if (__llvm_profile_is_continuous_mode_enabled())
    initializeProfileForContinuousMode();
}

/* Return buffer length that is required to store the current profile
 * filename with PID and hostname substitutions. */
/* The length to hold uint64_t followed by 3 digits pool id including '_' */
#define SIGLEN 24
static int getCurFilenameLength(void) {
  int Len;
  if (!lprofCurFilename.FilenamePat || !lprofCurFilename.FilenamePat[0])
    return 0;

  if (!(lprofCurFilename.NumPids || lprofCurFilename.NumHosts ||
        lprofCurFilename.TmpDir || lprofCurFilename.MergePoolSize))
    return strlen(lprofCurFilename.FilenamePat);

  Len = strlen(lprofCurFilename.FilenamePat) +
        lprofCurFilename.NumPids * (strlen(lprofCurFilename.PidChars) - 2) +
        lprofCurFilename.NumHosts * (strlen(lprofCurFilename.Hostname) - 2) +
        (lprofCurFilename.TmpDir ? (strlen(lprofCurFilename.TmpDir) - 1) : 0);
  if (lprofCurFilename.MergePoolSize)
    Len += SIGLEN;
  return Len;
}

/* Return the pointer to the current profile file name (after substituting
 * PIDs and Hostnames in filename pattern. \p FilenameBuf is the buffer
 * to store the resulting filename. If no substitution is needed, the
 * current filename pattern string is directly returned, unless ForceUseBuf
 * is enabled. */
static const char *getCurFilename(char *FilenameBuf, int ForceUseBuf) {
  int I, J, PidLength, HostNameLength, TmpDirLength, FilenamePatLength;
  const char *FilenamePat = lprofCurFilename.FilenamePat;

  if (!lprofCurFilename.FilenamePat || !lprofCurFilename.FilenamePat[0])
    return 0;

  if (!(lprofCurFilename.NumPids || lprofCurFilename.NumHosts ||
        lprofCurFilename.TmpDir || lprofCurFilename.MergePoolSize ||
        __llvm_profile_is_continuous_mode_enabled())) {
    if (!ForceUseBuf)
      return lprofCurFilename.FilenamePat;

    FilenamePatLength = strlen(lprofCurFilename.FilenamePat);
    memcpy(FilenameBuf, lprofCurFilename.FilenamePat, FilenamePatLength);
    FilenameBuf[FilenamePatLength] = '\0';
    return FilenameBuf;
  }

  PidLength = strlen(lprofCurFilename.PidChars);
  HostNameLength = strlen(lprofCurFilename.Hostname);
  TmpDirLength = lprofCurFilename.TmpDir ? strlen(lprofCurFilename.TmpDir) : 0;
  /* Construct the new filename. */
  for (I = 0, J = 0; FilenamePat[I]; ++I)
    if (FilenamePat[I] == '%') {
      if (FilenamePat[++I] == 'p') {
        memcpy(FilenameBuf + J, lprofCurFilename.PidChars, PidLength);
        J += PidLength;
      } else if (FilenamePat[I] == 'h') {
        memcpy(FilenameBuf + J, lprofCurFilename.Hostname, HostNameLength);
        J += HostNameLength;
      } else if (FilenamePat[I] == 't') {
        memcpy(FilenameBuf + J, lprofCurFilename.TmpDir, TmpDirLength);
        FilenameBuf[J + TmpDirLength] = DIR_SEPARATOR;
        J += TmpDirLength + 1;
      } else {
        if (!getMergePoolSize(FilenamePat, &I))
          continue;
        char LoadModuleSignature[SIGLEN + 1];
        int S;
        int ProfilePoolId = getpid() % lprofCurFilename.MergePoolSize;
        S = snprintf(LoadModuleSignature, SIGLEN + 1, "%" PRIu64 "_%d",
                     lprofGetLoadModuleSignature(), ProfilePoolId);
        if (S == -1 || S > SIGLEN)
          S = SIGLEN;
        memcpy(FilenameBuf + J, LoadModuleSignature, S);
        J += S;
      }
      /* Drop any unknown substitutions. */
    } else
      FilenameBuf[J++] = FilenamePat[I];
  FilenameBuf[J] = 0;

  return FilenameBuf;
}

/* Returns the pointer to the environment variable
 * string. Returns null if the env var is not set. */
static const char *getFilenamePatFromEnv(void) {
  const char *Filename = getenv("LLVM_PROFILE_FILE");
  if (!Filename || !Filename[0])
    return 0;
  return Filename;
}

COMPILER_RT_VISIBILITY
const char *__llvm_profile_get_path_prefix(void) {
  int Length;
  char *FilenameBuf, *Prefix;
  const char *Filename, *PrefixEnd;

  if (lprofCurFilename.ProfilePathPrefix)
    return lprofCurFilename.ProfilePathPrefix;

  Length = getCurFilenameLength();
  FilenameBuf = (char *)COMPILER_RT_ALLOCA(Length + 1);
  Filename = getCurFilename(FilenameBuf, 0);
  if (!Filename)
    return "\0";

  PrefixEnd = lprofFindLastDirSeparator(Filename);
  if (!PrefixEnd)
    return "\0";

  Length = PrefixEnd - Filename + 1;
  Prefix = (char *)malloc(Length + 1);
  if (!Prefix) {
    PROF_ERR("Failed to %s\n", "allocate memory.");
    return "\0";
  }
  memcpy(Prefix, Filename, Length);
  Prefix[Length] = '\0';
  lprofCurFilename.ProfilePathPrefix = Prefix;
  return Prefix;
}

COMPILER_RT_VISIBILITY
const char *__llvm_profile_get_filename(void) {
  int Length;
  char *FilenameBuf;
  const char *Filename;

  Length = getCurFilenameLength();
  FilenameBuf = (char *)malloc(Length + 1);
  if (!FilenameBuf) {
    PROF_ERR("Failed to %s\n", "allocate memory.");
    return "\0";
  }
  Filename = getCurFilename(FilenameBuf, 1);
  if (!Filename)
    return "\0";

  return FilenameBuf;
}

/* This API initializes the file handling, both user specified
 * profile path via -fprofile-instr-generate= and LLVM_PROFILE_FILE
 * environment variable can override this default value.
 */
COMPILER_RT_VISIBILITY
void __llvm_profile_initialize_file(void) {
  const char *EnvFilenamePat;
  const char *SelectedPat = NULL;
  ProfileNameSpecifier PNS = PNS_unknown;
  int hasCommandLineOverrider = (INSTR_PROF_PROFILE_NAME_VAR[0] != 0);

  EnvFilenamePat = getFilenamePatFromEnv();
  if (EnvFilenamePat) {
    /* Pass CopyFilenamePat = 1, to ensure that the filename would be valid
       at the  moment when __llvm_profile_write_file() gets executed. */
    parseAndSetFilename(EnvFilenamePat, PNS_environment, 1);
    return;
  } else if (hasCommandLineOverrider) {
    SelectedPat = INSTR_PROF_PROFILE_NAME_VAR;
    PNS = PNS_command_line;
  } else {
    SelectedPat = NULL;
    PNS = PNS_default;
  }

  parseAndSetFilename(SelectedPat, PNS, 0);
}

/* This method is invoked by the runtime initialization hook
 * InstrProfilingRuntime.o if it is linked in.
 */
COMPILER_RT_VISIBILITY
void __llvm_profile_initialize(void) {
  __llvm_profile_initialize_file();
  if (!__llvm_profile_is_continuous_mode_enabled())
    __llvm_profile_register_write_file_atexit();
}

/* This API is directly called by the user application code. It has the
 * highest precedence compared with LLVM_PROFILE_FILE environment variable
 * and command line option -fprofile-instr-generate=<profile_name>.
 */
COMPILER_RT_VISIBILITY
void __llvm_profile_set_filename(const char *FilenamePat) {
  if (__llvm_profile_is_continuous_mode_enabled())
    return;
  parseAndSetFilename(FilenamePat, PNS_runtime_api, 1);
}

/* The public API for writing profile data into the file with name
 * set by previous calls to __llvm_profile_set_filename or
 * __llvm_profile_override_default_filename or
 * __llvm_profile_initialize_file. */
COMPILER_RT_VISIBILITY
int __llvm_profile_write_file(void) {
  int rc, Length;
  const char *Filename;
  char *FilenameBuf;

  // Temporarily suspend getting SIGKILL when the parent exits.
  int PDeathSig = lprofSuspendSigKill();

  if (lprofProfileDumped() || __llvm_profile_is_continuous_mode_enabled()) {
    PROF_NOTE("Profile data not written to file: %s.\n", "already written");
    if (PDeathSig == 1)
      lprofRestoreSigKill();
    return 0;
  }

  Length = getCurFilenameLength();
  FilenameBuf = (char *)COMPILER_RT_ALLOCA(Length + 1);
  Filename = getCurFilename(FilenameBuf, 0);

  /* Check the filename. */
  if (!Filename) {
    PROF_ERR("Failed to write file : %s\n", "Filename not set");
    if (PDeathSig == 1)
      lprofRestoreSigKill();
    return -1;
  }

  /* Check if there is llvm/runtime version mismatch.  */
  if (GET_VERSION(__llvm_profile_get_version()) != INSTR_PROF_RAW_VERSION) {
    PROF_ERR("Runtime and instrumentation version mismatch : "
             "expected %d, but get %d\n",
             INSTR_PROF_RAW_VERSION,
             (int)GET_VERSION(__llvm_profile_get_version()));
    if (PDeathSig == 1)
      lprofRestoreSigKill();
    return -1;
  }

  /* Write profile data to the file. */
  rc = writeFile(Filename);
  if (rc)
    PROF_ERR("Failed to write file \"%s\": %s\n", Filename, strerror(errno));

  // Restore SIGKILL.
  if (PDeathSig == 1)
    lprofRestoreSigKill();

  return rc;
}

COMPILER_RT_VISIBILITY
int __llvm_profile_dump(void) {
  if (!doMerging())
    PROF_WARN("Later invocation of __llvm_profile_dump can lead to clobbering "
              " of previously dumped profile data : %s. Either use %%m "
              "in profile name or change profile name before dumping.\n",
              "online profile merging is not on");
  int rc = __llvm_profile_write_file();
  lprofSetProfileDumped(1);
  return rc;
}

/* Order file data will be saved in a file with suffx .order. */
static const char *OrderFileSuffix = ".order";

COMPILER_RT_VISIBILITY
int __llvm_orderfile_write_file(void) {
  int rc, Length, LengthBeforeAppend, SuffixLength;
  const char *Filename;
  char *FilenameBuf;

  // Temporarily suspend getting SIGKILL when the parent exits.
  int PDeathSig = lprofSuspendSigKill();

  SuffixLength = strlen(OrderFileSuffix);
  Length = getCurFilenameLength() + SuffixLength;
  FilenameBuf = (char *)COMPILER_RT_ALLOCA(Length + 1);
  Filename = getCurFilename(FilenameBuf, 1);

  /* Check the filename. */
  if (!Filename) {
    PROF_ERR("Failed to write file : %s\n", "Filename not set");
    if (PDeathSig == 1)
      lprofRestoreSigKill();
    return -1;
  }

  /* Append order file suffix */
  LengthBeforeAppend = strlen(Filename);
  memcpy(FilenameBuf + LengthBeforeAppend, OrderFileSuffix, SuffixLength);
  FilenameBuf[LengthBeforeAppend + SuffixLength] = '\0';

  /* Check if there is llvm/runtime version mismatch.  */
  if (GET_VERSION(__llvm_profile_get_version()) != INSTR_PROF_RAW_VERSION) {
    PROF_ERR("Runtime and instrumentation version mismatch : "
             "expected %d, but get %d\n",
             INSTR_PROF_RAW_VERSION,
             (int)GET_VERSION(__llvm_profile_get_version()));
    if (PDeathSig == 1)
      lprofRestoreSigKill();
    return -1;
  }

  /* Write order data to the file. */
  rc = writeOrderFile(Filename);
  if (rc)
    PROF_ERR("Failed to write file \"%s\": %s\n", Filename, strerror(errno));

  // Restore SIGKILL.
  if (PDeathSig == 1)
    lprofRestoreSigKill();

  return rc;
}

COMPILER_RT_VISIBILITY
int __llvm_orderfile_dump(void) {
  int rc = __llvm_orderfile_write_file();
  return rc;
}

static void writeFileWithoutReturn(void) { __llvm_profile_write_file(); }

COMPILER_RT_VISIBILITY
int __llvm_profile_register_write_file_atexit(void) {
  static int HasBeenRegistered = 0;

  if (HasBeenRegistered)
    return 0;

  lprofSetupValueProfiler();

  HasBeenRegistered = 1;
  return atexit(writeFileWithoutReturn);
}

COMPILER_RT_VISIBILITY int __llvm_profile_set_file_object(FILE *File,
                                                          int EnableMerge) {
  if (__llvm_profile_is_continuous_mode_enabled()) {
    if (!EnableMerge) {
      PROF_WARN("__llvm_profile_set_file_object(fd=%d) not supported in "
                "continuous sync mode when merging is disabled\n",
                fileno(File));
      return 1;
    }
    if (lprofLockFileHandle(File) != 0) {
      PROF_WARN("Data may be corrupted during profile merging : %s\n",
                "Fail to obtain file lock due to system limit.");
    }
    uint64_t ProfileFileSize = 0;
    if (getProfileFileSizeForMerging(File, &ProfileFileSize) == -1) {
      lprofUnlockFileHandle(File);
      return 1;
    }
    if (ProfileFileSize == 0) {
      FreeHook = &free;
      setupIOBuffer();
      ProfDataWriter fileWriter;
      initFileWriter(&fileWriter, File);
      if (lprofWriteData(&fileWriter, 0, 0)) {
        lprofUnlockFileHandle(File);
        PROF_ERR("Failed to write file \"%d\": %s\n", fileno(File),
                 strerror(errno));
        return 1;
      }
      fflush(File);
    } else {
      /* The merged profile has a non-zero length. Check that it is compatible
       * with the data in this process. */
      char *ProfileBuffer;
      if (mmapProfileForMerging(File, ProfileFileSize, &ProfileBuffer) == -1) {
        lprofUnlockFileHandle(File);
        return 1;
      }
      (void)munmap(ProfileBuffer, ProfileFileSize);
    }
    mmapForContinuousMode(0, File);
    lprofUnlockFileHandle(File);
  } else {
    setProfileFile(File);
    setProfileMergeRequested(EnableMerge);
  }
  return 0;
}

#endif
