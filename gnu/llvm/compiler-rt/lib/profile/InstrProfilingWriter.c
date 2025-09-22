/*===- InstrProfilingWriter.c - Write instrumentation to a file or buffer -===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

// Note: This is linked into the Darwin kernel, and must remain compatible
// with freestanding compilation. See `darwin_add_builtin_libraries`.

#ifdef _MSC_VER
/* For _alloca */
#include <malloc.h>
#endif
#include <string.h>

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"
#include "InstrProfilingPort.h"

#define INSTR_PROF_VALUE_PROF_DATA
#include "profile/InstrProfData.inc"

COMPILER_RT_VISIBILITY void (*FreeHook)(void *) = NULL;
static ProfBufferIO TheBufferIO;
#define VP_BUFFER_SIZE 8 * 1024
static uint8_t BufferIOBuffer[VP_BUFFER_SIZE];
static InstrProfValueData VPDataArray[16];
static uint32_t VPDataArraySize = sizeof(VPDataArray) / sizeof(*VPDataArray);

COMPILER_RT_VISIBILITY uint8_t *DynamicBufferIOBuffer = 0;
COMPILER_RT_VISIBILITY uint32_t VPBufferSize = 0;

/* The buffer writer is responsible in keeping writer state
 * across the call.
 */
COMPILER_RT_VISIBILITY uint32_t lprofBufferWriter(ProfDataWriter *This,
                                                  ProfDataIOVec *IOVecs,
                                                  uint32_t NumIOVecs) {
  uint32_t I;
  char **Buffer = (char **)&This->WriterCtx;
  for (I = 0; I < NumIOVecs; I++) {
    size_t Length = IOVecs[I].ElmSize * IOVecs[I].NumElm;
    if (IOVecs[I].Data)
      memcpy(*Buffer, IOVecs[I].Data, Length);
    else if (IOVecs[I].UseZeroPadding) {
      /* Allocating the buffer should zero fill. */
    }
    *Buffer += Length;
  }
  return 0;
}

static void llvmInitBufferIO(ProfBufferIO *BufferIO, ProfDataWriter *FileWriter,
                             uint8_t *Buffer, uint32_t BufferSz) {
  BufferIO->FileWriter = FileWriter;
  BufferIO->OwnFileWriter = 0;
  BufferIO->BufferStart = Buffer;
  BufferIO->BufferSz = BufferSz;
  BufferIO->CurOffset = 0;
}

COMPILER_RT_VISIBILITY ProfBufferIO *
lprofCreateBufferIO(ProfDataWriter *FileWriter) {
  uint8_t *Buffer = DynamicBufferIOBuffer;
  uint32_t BufferSize = VPBufferSize;
  if (!Buffer) {
    Buffer = &BufferIOBuffer[0];
    BufferSize = sizeof(BufferIOBuffer);
  }
  llvmInitBufferIO(&TheBufferIO, FileWriter, Buffer, BufferSize);
  return &TheBufferIO;
}

COMPILER_RT_VISIBILITY void lprofDeleteBufferIO(ProfBufferIO *BufferIO) {
  if (BufferIO->OwnFileWriter)
    FreeHook(BufferIO->FileWriter);
  if (DynamicBufferIOBuffer) {
    FreeHook(DynamicBufferIOBuffer);
    DynamicBufferIOBuffer = 0;
    VPBufferSize = 0;
  }
}

COMPILER_RT_VISIBILITY int
lprofBufferIOWrite(ProfBufferIO *BufferIO, const uint8_t *Data, uint32_t Size) {
  /* Buffer is not large enough, it is time to flush.  */
  if (Size + BufferIO->CurOffset > BufferIO->BufferSz) {
    if (lprofBufferIOFlush(BufferIO) != 0)
      return -1;
  }
  /* Special case, bypass the buffer completely. */
  ProfDataIOVec IO[] = {{Data, sizeof(uint8_t), Size, 0}};
  if (Size > BufferIO->BufferSz) {
    if (BufferIO->FileWriter->Write(BufferIO->FileWriter, IO, 1))
      return -1;
  } else {
    /* Write the data to buffer */
    uint8_t *Buffer = BufferIO->BufferStart + BufferIO->CurOffset;
    ProfDataWriter BufferWriter;
    initBufferWriter(&BufferWriter, (char *)Buffer);
    lprofBufferWriter(&BufferWriter, IO, 1);
    BufferIO->CurOffset =
        (uint8_t *)BufferWriter.WriterCtx - BufferIO->BufferStart;
  }
  return 0;
}

COMPILER_RT_VISIBILITY int lprofBufferIOFlush(ProfBufferIO *BufferIO) {
  if (BufferIO->CurOffset) {
    ProfDataIOVec IO[] = {
        {BufferIO->BufferStart, sizeof(uint8_t), BufferIO->CurOffset, 0}};
    if (BufferIO->FileWriter->Write(BufferIO->FileWriter, IO, 1))
      return -1;
    BufferIO->CurOffset = 0;
  }
  return 0;
}

/* Write out value profile data for function specified with \c Data.
 * The implementation does not use the method \c serializeValueProfData
 * which depends on dynamic memory allocation. In this implementation,
 * value profile data is written out to \c BufferIO piecemeal.
 */
static int writeOneValueProfData(ProfBufferIO *BufferIO,
                                 VPDataReaderType *VPDataReader,
                                 const __llvm_profile_data *Data) {
  unsigned I, NumValueKinds = 0;
  ValueProfData VPHeader;
  uint8_t *SiteCountArray[IPVK_Last + 1];

  for (I = 0; I <= IPVK_Last; I++) {
    if (!Data->NumValueSites[I])
      SiteCountArray[I] = 0;
    else {
      uint32_t Sz =
          VPDataReader->GetValueProfRecordHeaderSize(Data->NumValueSites[I]) -
          offsetof(ValueProfRecord, SiteCountArray);
      /* Only use alloca for this small byte array to avoid excessive
       * stack growth.  */
      SiteCountArray[I] = (uint8_t *)COMPILER_RT_ALLOCA(Sz);
      memset(SiteCountArray[I], 0, Sz);
    }
  }

  /* If NumValueKinds returned is 0, there is nothing to write, report
     success and return. This should match the raw profile reader's behavior. */
  if (!(NumValueKinds = VPDataReader->InitRTRecord(Data, SiteCountArray)))
    return 0;

  /* First write the header structure. */
  VPHeader.TotalSize = VPDataReader->GetValueProfDataSize();
  VPHeader.NumValueKinds = NumValueKinds;
  if (lprofBufferIOWrite(BufferIO, (const uint8_t *)&VPHeader,
                         sizeof(ValueProfData)))
    return -1;

  /* Make sure nothing else needs to be written before value profile
   * records. */
  if ((void *)VPDataReader->GetFirstValueProfRecord(&VPHeader) !=
      (void *)(&VPHeader + 1))
    return -1;

  /* Write out the value profile record for each value kind
   * one by one. */
  for (I = 0; I <= IPVK_Last; I++) {
    uint32_t J;
    ValueProfRecord RecordHeader;
    /* The size of the value prof record header without counting the
     * site count array .*/
    uint32_t RecordHeaderSize = offsetof(ValueProfRecord, SiteCountArray);
    uint32_t SiteCountArraySize;

    if (!Data->NumValueSites[I])
      continue;

    /* Write out the record header.  */
    RecordHeader.Kind = I;
    RecordHeader.NumValueSites = Data->NumValueSites[I];
    if (lprofBufferIOWrite(BufferIO, (const uint8_t *)&RecordHeader,
                           RecordHeaderSize))
      return -1;

    /* Write out the site value count array including padding space. */
    SiteCountArraySize =
        VPDataReader->GetValueProfRecordHeaderSize(Data->NumValueSites[I]) -
        RecordHeaderSize;
    if (lprofBufferIOWrite(BufferIO, SiteCountArray[I], SiteCountArraySize))
      return -1;

    /* Write out the value profile data for each value site.  */
    for (J = 0; J < Data->NumValueSites[I]; J++) {
      uint32_t NRead, NRemain;
      ValueProfNode *NextStartNode = 0;
      NRemain = VPDataReader->GetNumValueDataForSite(I, J);
      if (!NRemain)
        continue;
      /* Read and write out value data in small chunks till it is done. */
      do {
        NRead = (NRemain > VPDataArraySize ? VPDataArraySize : NRemain);
        NextStartNode =
            VPDataReader->GetValueData(I, /* ValueKind */
                                       J, /* Site */
                                       &VPDataArray[0], NextStartNode, NRead);
        if (lprofBufferIOWrite(BufferIO, (const uint8_t *)&VPDataArray[0],
                               NRead * sizeof(InstrProfValueData)))
          return -1;
        NRemain -= NRead;
      } while (NRemain != 0);
    }
  }
  /* All done report success.  */
  return 0;
}

static int writeValueProfData(ProfDataWriter *Writer,
                              VPDataReaderType *VPDataReader,
                              const __llvm_profile_data *DataBegin,
                              const __llvm_profile_data *DataEnd) {
  ProfBufferIO *BufferIO;
  const __llvm_profile_data *DI = 0;

  if (!VPDataReader)
    return 0;

  BufferIO = lprofCreateBufferIO(Writer);

  for (DI = DataBegin; DI < DataEnd; DI++) {
    if (writeOneValueProfData(BufferIO, VPDataReader, DI))
      return -1;
  }

  if (lprofBufferIOFlush(BufferIO) != 0)
    return -1;
  lprofDeleteBufferIO(BufferIO);

  return 0;
}

COMPILER_RT_VISIBILITY int lprofWriteData(ProfDataWriter *Writer,
                                          VPDataReaderType *VPDataReader,
                                          int SkipNameDataWrite) {
  /* Match logic in __llvm_profile_write_buffer(). */
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
  return lprofWriteDataImpl(Writer, DataBegin, DataEnd, CountersBegin,
                            CountersEnd, BitmapBegin, BitmapEnd, VPDataReader,
                            NamesBegin, NamesEnd, VTableBegin, VTableEnd,
                            VNamesBegin, VNamesEnd, SkipNameDataWrite);
}

COMPILER_RT_VISIBILITY int
lprofWriteDataImpl(ProfDataWriter *Writer, const __llvm_profile_data *DataBegin,
                   const __llvm_profile_data *DataEnd,
                   const char *CountersBegin, const char *CountersEnd,
                   const char *BitmapBegin, const char *BitmapEnd,
                   VPDataReaderType *VPDataReader, const char *NamesBegin,
                   const char *NamesEnd, const VTableProfData *VTableBegin,
                   const VTableProfData *VTableEnd, const char *VNamesBegin,
                   const char *VNamesEnd, int SkipNameDataWrite) {
  /* Calculate size of sections. */
  const uint64_t DataSectionSize =
      __llvm_profile_get_data_size(DataBegin, DataEnd);
  const uint64_t NumData = __llvm_profile_get_num_data(DataBegin, DataEnd);
  const uint64_t CountersSectionSize =
      __llvm_profile_get_counters_size(CountersBegin, CountersEnd);
  const uint64_t NumCounters =
      __llvm_profile_get_num_counters(CountersBegin, CountersEnd);
  const uint64_t NumBitmapBytes =
      __llvm_profile_get_num_bitmap_bytes(BitmapBegin, BitmapEnd);
  const uint64_t NamesSize = __llvm_profile_get_name_size(NamesBegin, NamesEnd);
  const uint64_t NumVTables =
      __llvm_profile_get_num_vtable(VTableBegin, VTableEnd);
  const uint64_t VTableSectionSize =
      __llvm_profile_get_vtable_section_size(VTableBegin, VTableEnd);
  const uint64_t VNamesSize =
      __llvm_profile_get_name_size(VNamesBegin, VNamesEnd);

  /* Create the header. */
  __llvm_profile_header Header;

  /* Determine how much padding is needed before/after the counters and after
   * the names. */
  uint64_t PaddingBytesBeforeCounters, PaddingBytesAfterCounters,
      PaddingBytesAfterBitmapBytes, PaddingBytesAfterNames,
      PaddingBytesAfterVTable, PaddingBytesAfterVNames;
  if (__llvm_profile_get_padding_sizes_for_counters(
          DataSectionSize, CountersSectionSize, NumBitmapBytes, NamesSize,
          VTableSectionSize, VNamesSize, &PaddingBytesBeforeCounters,
          &PaddingBytesAfterCounters, &PaddingBytesAfterBitmapBytes,
          &PaddingBytesAfterNames, &PaddingBytesAfterVTable,
          &PaddingBytesAfterVNames) == -1)
    return -1;

  {
/* Initialize header structure.  */
#define INSTR_PROF_RAW_HEADER(Type, Name, Init) Header.Name = Init;
#include "profile/InstrProfData.inc"
  }

  /* On WIN64, label differences are truncated 32-bit values. Truncate
   * CountersDelta to match. */
#ifdef _WIN64
  Header.CountersDelta = (uint32_t)Header.CountersDelta;
  Header.BitmapDelta = (uint32_t)Header.BitmapDelta;
#endif

  /* The data and names sections are omitted in lightweight mode. */
  if (NumData == 0 && NamesSize == 0) {
    Header.CountersDelta = 0;
    Header.NamesDelta = 0;
  }

  /* Write the profile header. */
  ProfDataIOVec IOVec[] = {{&Header, sizeof(__llvm_profile_header), 1, 0}};
  if (Writer->Write(Writer, IOVec, sizeof(IOVec) / sizeof(*IOVec)))
    return -1;

  /* Write the binary id lengths and data. */
  if (__llvm_write_binary_ids(Writer) == -1)
    return -1;

  /* Write the profile data. */
  ProfDataIOVec IOVecData[] = {
      {DataBegin, sizeof(uint8_t), DataSectionSize, 0},
      {NULL, sizeof(uint8_t), PaddingBytesBeforeCounters, 1},
      {CountersBegin, sizeof(uint8_t), CountersSectionSize, 0},
      {NULL, sizeof(uint8_t), PaddingBytesAfterCounters, 1},
      {BitmapBegin, sizeof(uint8_t), NumBitmapBytes, 0},
      {NULL, sizeof(uint8_t), PaddingBytesAfterBitmapBytes, 1},
      {SkipNameDataWrite ? NULL : NamesBegin, sizeof(uint8_t), NamesSize, 0},
      {NULL, sizeof(uint8_t), PaddingBytesAfterNames, 1},
      {VTableBegin, sizeof(uint8_t), VTableSectionSize, 0},
      {NULL, sizeof(uint8_t), PaddingBytesAfterVTable, 1},
      {SkipNameDataWrite ? NULL : VNamesBegin, sizeof(uint8_t), VNamesSize, 0},
      {NULL, sizeof(uint8_t), PaddingBytesAfterVNames, 1}};
  if (Writer->Write(Writer, IOVecData, sizeof(IOVecData) / sizeof(*IOVecData)))
    return -1;

  /* Value profiling is not yet supported in continuous mode and profile
   * correlation mode. */
  if (__llvm_profile_is_continuous_mode_enabled() ||
      (NumData == 0 && NamesSize == 0))
    return 0;

  return writeValueProfData(Writer, VPDataReader, DataBegin, DataEnd);
}

/*
 * Write binary id length and then its data, because binary id does not
 * have a fixed length.
 */
COMPILER_RT_VISIBILITY
int lprofWriteOneBinaryId(ProfDataWriter *Writer, uint64_t BinaryIdLen,
                          const uint8_t *BinaryIdData,
                          uint64_t BinaryIdPadding) {
  ProfDataIOVec BinaryIdIOVec[] = {
      {&BinaryIdLen, sizeof(uint64_t), 1, 0},
      {BinaryIdData, sizeof(uint8_t), BinaryIdLen, 0},
      {NULL, sizeof(uint8_t), BinaryIdPadding, 1},
  };
  if (Writer->Write(Writer, BinaryIdIOVec,
                    sizeof(BinaryIdIOVec) / sizeof(*BinaryIdIOVec)))
    return -1;

  /* Successfully wrote binary id, report success. */
  return 0;
}
