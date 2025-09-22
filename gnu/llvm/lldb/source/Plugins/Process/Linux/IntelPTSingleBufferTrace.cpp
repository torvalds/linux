//===-- IntelPTSingleBufferTrace.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IntelPTSingleBufferTrace.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Host.h"
#include <linux/perf_event.h>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>

using namespace lldb;
using namespace lldb_private;
using namespace process_linux;
using namespace llvm;

const char kOSEventIntelPTTypeFile[] =
    "/sys/bus/event_source/devices/intel_pt/type";

const char kPSBPeriodCapFile[] =
    "/sys/bus/event_source/devices/intel_pt/caps/psb_cyc";

const char kPSBPeriodValidValuesFile[] =
    "/sys/bus/event_source/devices/intel_pt/caps/psb_periods";

const char kPSBPeriodBitOffsetFile[] =
    "/sys/bus/event_source/devices/intel_pt/format/psb_period";

const char kTSCBitOffsetFile[] =
    "/sys/bus/event_source/devices/intel_pt/format/tsc";

enum IntelPTConfigFileType {
  Hex = 0,
  // 0 or 1
  ZeroOne,
  Decimal,
  // a bit index file always starts with the prefix config: following by an int,
  // which represents the offset of the perf_event_attr.config value where to
  // store a given configuration.
  BitOffset
};

static Expected<uint32_t> ReadIntelPTConfigFile(const char *file,
                                                IntelPTConfigFileType type) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> stream =
      MemoryBuffer::getFileAsStream(file);

  if (!stream)
    return createStringError(inconvertibleErrorCode(),
                             "Can't open the file '%s'", file);

  uint32_t value = 0;
  StringRef text_buffer = stream.get()->getBuffer();

  if (type == BitOffset) {
    const char *prefix = "config:";
    if (!text_buffer.starts_with(prefix))
      return createStringError(inconvertibleErrorCode(),
                               "The file '%s' contents doesn't start with '%s'",
                               file, prefix);
    text_buffer = text_buffer.substr(strlen(prefix));
  }

  auto getRadix = [&]() {
    switch (type) {
    case Hex:
      return 16;
    case ZeroOne:
    case Decimal:
    case BitOffset:
      return 10;
    }
    llvm_unreachable("Fully covered switch above!");
  };

  auto createError = [&](const char *expected_value_message) {
    return createStringError(
        inconvertibleErrorCode(),
        "The file '%s' has an invalid value. It should be %s.", file,
        expected_value_message);
  };

  if (text_buffer.trim().consumeInteger(getRadix(), value) ||
      (type == ZeroOne && value != 0 && value != 1)) {
    switch (type) {
    case Hex:
      return createError("an unsigned hexadecimal int");
    case ZeroOne:
      return createError("0 or 1");
    case Decimal:
    case BitOffset:
      return createError("an unsigned decimal int");
    }
  }
  return value;
}

/// Return the Linux perf event type for Intel PT.
Expected<uint32_t> process_linux::GetIntelPTOSEventType() {
  return ReadIntelPTConfigFile(kOSEventIntelPTTypeFile,
                               IntelPTConfigFileType::Decimal);
}

static Error CheckPsbPeriod(size_t psb_period) {
  Expected<uint32_t> cap =
      ReadIntelPTConfigFile(kPSBPeriodCapFile, IntelPTConfigFileType::ZeroOne);
  if (!cap)
    return cap.takeError();
  if (*cap == 0)
    return createStringError(inconvertibleErrorCode(),
                             "psb_period is unsupported in the system.");

  Expected<uint32_t> valid_values = ReadIntelPTConfigFile(
      kPSBPeriodValidValuesFile, IntelPTConfigFileType::Hex);
  if (!valid_values)
    return valid_values.takeError();

  if (valid_values.get() & (1 << psb_period))
    return Error::success();

  std::ostringstream error;
  // 0 is always a valid value
  error << "Invalid psb_period. Valid values are: 0";
  uint32_t mask = valid_values.get();
  while (mask) {
    int index = __builtin_ctz(mask);
    if (index > 0)
      error << ", " << index;
    // clear the lowest bit
    mask &= mask - 1;
  }
  error << ".";
  return createStringError(inconvertibleErrorCode(), error.str().c_str());
}

#ifdef PERF_ATTR_SIZE_VER5
static Expected<uint64_t>
GeneratePerfEventConfigValue(bool enable_tsc,
                             std::optional<uint64_t> psb_period) {
  uint64_t config = 0;
  // tsc is always supported
  if (enable_tsc) {
    if (Expected<uint32_t> offset = ReadIntelPTConfigFile(
            kTSCBitOffsetFile, IntelPTConfigFileType::BitOffset))
      config |= 1 << *offset;
    else
      return offset.takeError();
  }
  if (psb_period) {
    if (Error error = CheckPsbPeriod(*psb_period))
      return std::move(error);

    if (Expected<uint32_t> offset = ReadIntelPTConfigFile(
            kPSBPeriodBitOffsetFile, IntelPTConfigFileType::BitOffset))
      config |= *psb_period << *offset;
    else
      return offset.takeError();
  }
  return config;
}

/// Create a \a perf_event_attr configured for
/// an IntelPT event.
///
/// \return
///   A \a perf_event_attr if successful,
///   or an \a llvm::Error otherwise.
static Expected<perf_event_attr>
CreateIntelPTPerfEventConfiguration(bool enable_tsc,
                                    std::optional<uint64_t> psb_period) {
  perf_event_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.exclude_idle = 1;

  if (Expected<uint64_t> config_value =
          GeneratePerfEventConfigValue(enable_tsc, psb_period))
    attr.config = *config_value;
  else
    return config_value.takeError();

  if (Expected<uint32_t> intel_pt_type = GetIntelPTOSEventType())
    attr.type = *intel_pt_type;
  else
    return intel_pt_type.takeError();

  return attr;
}
#endif

size_t IntelPTSingleBufferTrace::GetIptTraceSize() const {
  return m_perf_event.GetAuxBuffer().size();
}

Error IntelPTSingleBufferTrace::Pause() {
  return m_perf_event.DisableWithIoctl();
}

Error IntelPTSingleBufferTrace::Resume() {
  return m_perf_event.EnableWithIoctl();
}

Expected<std::vector<uint8_t>> IntelPTSingleBufferTrace::GetIptTrace() {
  // Disable the perf event to force a flush out of the CPU's internal buffer.
  // Besides, we can guarantee that the CPU won't override any data as we are
  // reading the buffer.
  // The Intel documentation says:
  //
  // Packets are first buffered internally and then written out
  // asynchronously. To collect packet output for postprocessing, a collector
  // needs first to ensure that all packet data has been flushed from internal
  // buffers. Software can ensure this by stopping packet generation by
  // clearing IA32_RTIT_CTL.TraceEn (see “Disabling Packet Generation” in
  // Section 35.2.7.2).
  //
  // This is achieved by the PERF_EVENT_IOC_DISABLE ioctl request, as
  // mentioned in the man page of perf_event_open.
  return m_perf_event.GetReadOnlyAuxBuffer();
}

Expected<IntelPTSingleBufferTrace>
IntelPTSingleBufferTrace::Start(const TraceIntelPTStartRequest &request,
                                std::optional<lldb::tid_t> tid,
                                std::optional<cpu_id_t> cpu_id, bool disabled,
                                std::optional<int> cgroup_fd) {
#ifndef PERF_ATTR_SIZE_VER5
  return createStringError(inconvertibleErrorCode(),
                           "Intel PT Linux perf event not supported");
#else
  Log *log = GetLog(POSIXLog::Trace);

  LLDB_LOG(log, "Will start tracing thread id {0} and cpu id {1}", tid, cpu_id);

  if (__builtin_popcount(request.ipt_trace_size) != 1 ||
      request.ipt_trace_size < 4096) {
    return createStringError(
        inconvertibleErrorCode(),
        "The intel pt trace size must be a power of 2 greater than or equal to "
        "4096 (2^12) bytes. It was %" PRIu64 ".",
        request.ipt_trace_size);
  }
  uint64_t page_size = getpagesize();
  uint64_t aux_buffer_numpages = static_cast<uint64_t>(llvm::bit_floor(
      (request.ipt_trace_size + page_size - 1) / page_size));

  Expected<perf_event_attr> attr = CreateIntelPTPerfEventConfiguration(
      request.enable_tsc,
      llvm::transformOptional(request.psb_period, [](int value) {
        return static_cast<uint64_t>(value);
      }));
  if (!attr)
    return attr.takeError();
  attr->disabled = disabled;

  LLDB_LOG(log, "Will create intel pt trace buffer of size {0}",
           request.ipt_trace_size);
  unsigned long flags = 0;
  if (cgroup_fd) {
    tid = *cgroup_fd;
    flags |= PERF_FLAG_PID_CGROUP;
  }

  if (Expected<PerfEvent> perf_event =
          PerfEvent::Init(*attr, tid, cpu_id, -1, flags)) {
    if (Error mmap_err = perf_event->MmapMetadataAndBuffers(
            /*num_data_pages=*/0, aux_buffer_numpages,
            /*data_buffer_write=*/true)) {
      return std::move(mmap_err);
    }
    return IntelPTSingleBufferTrace(std::move(*perf_event));
  } else {
    return perf_event.takeError();
  }
#endif
}

const PerfEvent &IntelPTSingleBufferTrace::GetPerfEvent() const {
  return m_perf_event;
}
