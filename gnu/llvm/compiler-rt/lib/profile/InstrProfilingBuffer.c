/*===- InstrProfilingBuffer.c - Write instrumentation to a memory buffer --===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

// Note: This is linked into the Darwin kernel, and must remain compatible
// with freestanding compilation. See `darwin_add_builtin_libraries`.

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"
#include "InstrProfilingPort.h"

/* When continuous mode is enabled (%c), this parameter is set to 1.
 *
 * This parameter is defined here in InstrProfilingBuffer.o, instead of in
 * InstrProfilingFile.o, to sequester all libc-dependent code in
 * InstrProfilingFile.o. The test `instrprof-without-libc` will break if this
 * layering is violated. */
static int ContinuouslySyncProfile = 0;

/* The system page size. Only valid when non-zero. If 0, the page size is
 * unavailable. */
static unsigned PageSize = 0;

COMPILER_RT_VISIBILITY int __llvm_profile_is_continuous_mode_enabled(void) {
  return ContinuouslySyncProfile && PageSize;
}

COMPILER_RT_VISIBILITY void __llvm_profile_enable_continuous_mode(void) {
  ContinuouslySyncProfile = 1;
}

COMPILER_RT_VISIBILITY void __llvm_profile_disable_continuous_mode(void) {
  ContinuouslySyncProfile = 0;
}

COMPILER_RT_VISIBILITY void __llvm_profile_set_page_size(unsigned PS) {
  PageSize = PS;
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_size_for_buffer(void) {
  const __llvm_profile_data *DataBegin = __llvm_profile_begin_data();
  const __llvm_profile_data *DataEnd = __llvm_profile_end_data();
  const char *CountersBegin = __llvm_profile_begin_counters();
  const char *CountersEnd = __llvm_profile_end_counters();
  const char *BitmapBegin = __llvm_profile_begin_bitmap();
  const char *BitmapEnd = __llvm_profile_end_bitmap();
  const char *NamesBegin = __llvm_profile_begin_names();
  const char *NamesEnd = __llvm_profile_end_names();
  const VTableProfData *VTableBegin = __llvm_profile_begin_vtables();
  const VTableProfData *VTableEnd = __llvm_profile_end_vtables();
  const char *VNamesBegin = __llvm_profile_begin_vtabnames();
  const char *VNamesEnd = __llvm_profile_end_vtabnames();

  return __llvm_profile_get_size_for_buffer_internal(
      DataBegin, DataEnd, CountersBegin, CountersEnd, BitmapBegin, BitmapEnd,
      NamesBegin, NamesEnd, VTableBegin, VTableEnd, VNamesBegin, VNamesEnd);
}

// NOTE: Caller should guarantee that `Begin` and `End` specifies a half-open
// interval [Begin, End). Namely, `End` is one-byte past the end of the array.
COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_num_data(const __llvm_profile_data *Begin,
                                     const __llvm_profile_data *End) {
  intptr_t BeginI = (intptr_t)Begin, EndI = (intptr_t)End;
  return ((EndI + sizeof(__llvm_profile_data) - 1) - BeginI) /
         sizeof(__llvm_profile_data);
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_data_size(const __llvm_profile_data *Begin,
                                      const __llvm_profile_data *End) {
  return __llvm_profile_get_num_data(Begin, End) * sizeof(__llvm_profile_data);
}

// Counts the number of `VTableProfData` elements within the range of [Begin,
// End). Caller should guarantee that End points to one byte past the inclusive
// range.
// FIXME: Add a compiler-rt test to make sure the number of vtables in the
// raw profile is the same as the number of vtable elements in the instrumented
// binary.
COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_num_vtable(const VTableProfData *Begin,
                                       const VTableProfData *End) {
  // Convert pointers to intptr_t to use integer arithmetic.
  intptr_t EndI = (intptr_t)End, BeginI = (intptr_t)Begin;
  return (EndI - BeginI) / sizeof(VTableProfData);
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_vtable_section_size(const VTableProfData *Begin,
                                                const VTableProfData *End) {
  return (intptr_t)(End) - (intptr_t)(Begin);
}

COMPILER_RT_VISIBILITY size_t __llvm_profile_counter_entry_size(void) {
  if (__llvm_profile_get_version() & VARIANT_MASK_BYTE_COVERAGE)
    return sizeof(uint8_t);
  return sizeof(uint64_t);
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_num_counters(const char *Begin, const char *End) {
  intptr_t BeginI = (intptr_t)Begin, EndI = (intptr_t)End;
  return ((EndI + __llvm_profile_counter_entry_size() - 1) - BeginI) /
         __llvm_profile_counter_entry_size();
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_counters_size(const char *Begin, const char *End) {
  return __llvm_profile_get_num_counters(Begin, End) *
         __llvm_profile_counter_entry_size();
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_num_bitmap_bytes(const char *Begin,
                                             const char *End) {
  return (End - Begin);
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_name_size(const char *Begin, const char *End) {
  return End - Begin;
}

/// Calculate the number of padding bytes needed to add to \p Offset in order
/// for (\p Offset + Padding) to be page-aligned.
static uint64_t calculateBytesNeededToPageAlign(uint64_t Offset) {
  uint64_t OffsetModPage = Offset % PageSize;
  if (OffsetModPage > 0)
    return PageSize - OffsetModPage;
  return 0;
}

static int needsCounterPadding(void) {
#if defined(__APPLE__)
  return __llvm_profile_is_continuous_mode_enabled();
#else
  return 0;
#endif
}

COMPILER_RT_VISIBILITY
int __llvm_profile_get_padding_sizes_for_counters(
    uint64_t DataSize, uint64_t CountersSize, uint64_t NumBitmapBytes,
    uint64_t NamesSize, uint64_t VTableSize, uint64_t VNameSize,
    uint64_t *PaddingBytesBeforeCounters, uint64_t *PaddingBytesAfterCounters,
    uint64_t *PaddingBytesAfterBitmapBytes, uint64_t *PaddingBytesAfterNames,
    uint64_t *PaddingBytesAfterVTable, uint64_t *PaddingBytesAfterVName) {
  // Counter padding is needed only if continuous mode is enabled.
  if (!needsCounterPadding()) {
    *PaddingBytesBeforeCounters = 0;
    *PaddingBytesAfterCounters =
        __llvm_profile_get_num_padding_bytes(CountersSize);
    *PaddingBytesAfterBitmapBytes =
        __llvm_profile_get_num_padding_bytes(NumBitmapBytes);
    *PaddingBytesAfterNames = __llvm_profile_get_num_padding_bytes(NamesSize);
    if (PaddingBytesAfterVTable != NULL)
      *PaddingBytesAfterVTable =
          __llvm_profile_get_num_padding_bytes(VTableSize);
    if (PaddingBytesAfterVName != NULL)
      *PaddingBytesAfterVName = __llvm_profile_get_num_padding_bytes(VNameSize);
    return 0;
  }

  // Value profiling not supported in continuous mode at profile-write time.
  // Return -1 to alert the incompatibility.
  if (VTableSize != 0 || VNameSize != 0)
    return -1;

  // In continuous mode, the file offsets for headers and for the start of
  // counter sections need to be page-aligned.
  *PaddingBytesBeforeCounters =
      calculateBytesNeededToPageAlign(sizeof(__llvm_profile_header) + DataSize);
  *PaddingBytesAfterCounters = calculateBytesNeededToPageAlign(CountersSize);
  *PaddingBytesAfterBitmapBytes =
      calculateBytesNeededToPageAlign(NumBitmapBytes);
  *PaddingBytesAfterNames = calculateBytesNeededToPageAlign(NamesSize);
  // Set these two variables to zero to avoid uninitialized variables
  // even if VTableSize and VNameSize are known to be zero.
  if (PaddingBytesAfterVTable != NULL)
    *PaddingBytesAfterVTable = 0;
  if (PaddingBytesAfterVName != NULL)
    *PaddingBytesAfterVName = 0;
  return 0;
}

COMPILER_RT_VISIBILITY
uint64_t __llvm_profile_get_size_for_buffer_internal(
    const __llvm_profile_data *DataBegin, const __llvm_profile_data *DataEnd,
    const char *CountersBegin, const char *CountersEnd, const char *BitmapBegin,
    const char *BitmapEnd, const char *NamesBegin, const char *NamesEnd,
    const VTableProfData *VTableBegin, const VTableProfData *VTableEnd,
    const char *VNamesBegin, const char *VNamesEnd) {
  /* Match logic in __llvm_profile_write_buffer(). */
  const uint64_t NamesSize = (NamesEnd - NamesBegin) * sizeof(char);
  uint64_t DataSize = __llvm_profile_get_data_size(DataBegin, DataEnd);
  uint64_t CountersSize =
      __llvm_profile_get_counters_size(CountersBegin, CountersEnd);
  const uint64_t NumBitmapBytes =
      __llvm_profile_get_num_bitmap_bytes(BitmapBegin, BitmapEnd);
  const uint64_t VTableSize =
      __llvm_profile_get_vtable_section_size(VTableBegin, VTableEnd);
  const uint64_t VNameSize =
      __llvm_profile_get_name_size(VNamesBegin, VNamesEnd);

  /* Determine how much padding is needed before/after the counters and after
   * the names. */
  uint64_t PaddingBytesBeforeCounters, PaddingBytesAfterCounters,
      PaddingBytesAfterNames, PaddingBytesAfterBitmapBytes,
      PaddingBytesAfterVTable, PaddingBytesAfterVNames;
  __llvm_profile_get_padding_sizes_for_counters(
      DataSize, CountersSize, NumBitmapBytes, NamesSize, 0 /* VTableSize */,
      0 /* VNameSize */, &PaddingBytesBeforeCounters,
      &PaddingBytesAfterCounters, &PaddingBytesAfterBitmapBytes,
      &PaddingBytesAfterNames, &PaddingBytesAfterVTable,
      &PaddingBytesAfterVNames);

  return sizeof(__llvm_profile_header) + __llvm_write_binary_ids(NULL) +
         DataSize + PaddingBytesBeforeCounters + CountersSize +
         PaddingBytesAfterCounters + NumBitmapBytes +
         PaddingBytesAfterBitmapBytes + NamesSize + PaddingBytesAfterNames +
         VTableSize + PaddingBytesAfterVTable + VNameSize +
         PaddingBytesAfterVNames;
}

COMPILER_RT_VISIBILITY
void initBufferWriter(ProfDataWriter *BufferWriter, char *Buffer) {
  BufferWriter->Write = lprofBufferWriter;
  BufferWriter->WriterCtx = Buffer;
}

COMPILER_RT_VISIBILITY int __llvm_profile_write_buffer(char *Buffer) {
  ProfDataWriter BufferWriter;
  initBufferWriter(&BufferWriter, Buffer);
  return lprofWriteData(&BufferWriter, 0, 0);
}

COMPILER_RT_VISIBILITY int __llvm_profile_write_buffer_internal(
    char *Buffer, const __llvm_profile_data *DataBegin,
    const __llvm_profile_data *DataEnd, const char *CountersBegin,
    const char *CountersEnd, const char *BitmapBegin, const char *BitmapEnd,
    const char *NamesBegin, const char *NamesEnd) {
  ProfDataWriter BufferWriter;
  initBufferWriter(&BufferWriter, Buffer);
  // Set virtual table arguments to NULL since they are not supported yet.
  return lprofWriteDataImpl(
      &BufferWriter, DataBegin, DataEnd, CountersBegin, CountersEnd,
      BitmapBegin, BitmapEnd, /*VPDataReader=*/0, NamesBegin, NamesEnd,
      /*VTableBegin=*/NULL, /*VTableEnd=*/NULL, /*VNamesBegin=*/NULL,
      /*VNamesEnd=*/NULL, /*SkipNameDataWrite=*/0);
}
