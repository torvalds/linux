//===-- Perf.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Perf.h"

#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Host/linux/Support.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include <linux/version.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace lldb_private;
using namespace process_linux;
using namespace llvm;

Expected<LinuxPerfZeroTscConversion>
lldb_private::process_linux::LoadPerfTscConversionParameters() {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
  lldb::pid_t pid = getpid();
  perf_event_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.type = PERF_TYPE_SOFTWARE;
  attr.config = PERF_COUNT_SW_DUMMY;

  Expected<PerfEvent> perf_event = PerfEvent::Init(attr, pid);
  if (!perf_event)
    return perf_event.takeError();
  if (Error mmap_err =
          perf_event->MmapMetadataAndBuffers(/*num_data_pages=*/0,
                                             /*num_aux_pages=*/0,
                                             /*data_buffer_write=*/false))
    return std::move(mmap_err);

  perf_event_mmap_page &mmap_metada = perf_event->GetMetadataPage();
  if (mmap_metada.cap_user_time && mmap_metada.cap_user_time_zero) {
    return LinuxPerfZeroTscConversion{
        mmap_metada.time_mult, mmap_metada.time_shift, {mmap_metada.time_zero}};
  } else {
    auto err_cap =
        !mmap_metada.cap_user_time ? "cap_user_time" : "cap_user_time_zero";
    std::string err_msg =
        llvm::formatv("Can't get TSC to real time conversion values. "
                      "perf_event capability '{0}' not supported.",
                      err_cap);
    return llvm::createStringError(llvm::inconvertibleErrorCode(), err_msg);
  }
#else
  std::string err_msg = "PERF_COUNT_SW_DUMMY requires Linux 3.12";
  return llvm::createStringError(llvm::inconvertibleErrorCode(), err_msg);
#endif
}

void resource_handle::MmapDeleter::operator()(void *ptr) {
  if (m_bytes && ptr != nullptr)
    munmap(ptr, m_bytes);
}

void resource_handle::FileDescriptorDeleter::operator()(long *ptr) {
  if (ptr == nullptr)
    return;
  if (*ptr == -1)
    return;
  close(*ptr);
  std::default_delete<long>()(ptr);
}

llvm::Expected<PerfEvent> PerfEvent::Init(perf_event_attr &attr,
                                          std::optional<lldb::pid_t> pid,
                                          std::optional<lldb::cpu_id_t> cpu,
                                          std::optional<long> group_fd,
                                          unsigned long flags) {
  errno = 0;
  long fd = syscall(SYS_perf_event_open, &attr, pid.value_or(-1),
                    cpu.value_or(-1), group_fd.value_or(-1), flags);
  if (fd == -1) {
    std::string err_msg =
        llvm::formatv("perf event syscall failed: {0}", std::strerror(errno));
    return llvm::createStringError(llvm::inconvertibleErrorCode(), err_msg);
  }
  return PerfEvent(fd, !attr.disabled);
}

llvm::Expected<PerfEvent> PerfEvent::Init(perf_event_attr &attr,
                                          std::optional<lldb::pid_t> pid,
                                          std::optional<lldb::cpu_id_t> cpu) {
  return Init(attr, pid, cpu, -1, 0);
}

llvm::Expected<resource_handle::MmapUP>
PerfEvent::DoMmap(void *addr, size_t length, int prot, int flags,
                  long int offset, llvm::StringRef buffer_name) {
  errno = 0;
  auto mmap_result = ::mmap(addr, length, prot, flags, GetFd(), offset);

  if (mmap_result == MAP_FAILED) {
    std::string err_msg =
        llvm::formatv("perf event mmap allocation failed for {0}: {1}",
                      buffer_name, std::strerror(errno));
    return createStringError(inconvertibleErrorCode(), err_msg);
  }
  return resource_handle::MmapUP(mmap_result, length);
}

llvm::Error PerfEvent::MmapMetadataAndDataBuffer(size_t num_data_pages,
                                                 bool data_buffer_write) {
  size_t mmap_size = (num_data_pages + 1) * getpagesize();
  if (Expected<resource_handle::MmapUP> mmap_metadata_data = DoMmap(
          nullptr, mmap_size, PROT_READ | (data_buffer_write ? PROT_WRITE : 0),
          MAP_SHARED, 0, "metadata and data buffer")) {
    m_metadata_data_base = std::move(mmap_metadata_data.get());
    return Error::success();
  } else
    return mmap_metadata_data.takeError();
}

llvm::Error PerfEvent::MmapAuxBuffer(size_t num_aux_pages) {
#ifndef PERF_ATTR_SIZE_VER5
  return createStringError(inconvertibleErrorCode(),
                           "Intel PT Linux perf event not supported");
#else
  if (num_aux_pages == 0)
    return Error::success();

  perf_event_mmap_page &metadata_page = GetMetadataPage();

  metadata_page.aux_offset =
      metadata_page.data_offset + metadata_page.data_size;
  metadata_page.aux_size = num_aux_pages * getpagesize();

  if (Expected<resource_handle::MmapUP> mmap_aux =
          DoMmap(nullptr, metadata_page.aux_size, PROT_READ, MAP_SHARED,
                 metadata_page.aux_offset, "aux buffer")) {
    m_aux_base = std::move(mmap_aux.get());
    return Error::success();
  } else
    return mmap_aux.takeError();
#endif
}

llvm::Error PerfEvent::MmapMetadataAndBuffers(size_t num_data_pages,
                                              size_t num_aux_pages,
                                              bool data_buffer_write) {
  if (num_data_pages != 0 && !isPowerOf2_64(num_data_pages))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        llvm::formatv("Number of data pages must be a power of 2, got: {0}",
                      num_data_pages));
  if (num_aux_pages != 0 && !isPowerOf2_64(num_aux_pages))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        llvm::formatv("Number of aux pages must be a power of 2, got: {0}",
                      num_aux_pages));
  if (Error err = MmapMetadataAndDataBuffer(num_data_pages, data_buffer_write))
    return err;
  if (Error err = MmapAuxBuffer(num_aux_pages))
    return err;
  return Error::success();
}

long PerfEvent::GetFd() const { return *(m_fd.get()); }

perf_event_mmap_page &PerfEvent::GetMetadataPage() const {
  return *reinterpret_cast<perf_event_mmap_page *>(m_metadata_data_base.get());
}

ArrayRef<uint8_t> PerfEvent::GetDataBuffer() const {
#ifndef PERF_ATTR_SIZE_VER5
  llvm_unreachable("Intel PT Linux perf event not supported");
#else
  perf_event_mmap_page &mmap_metadata = GetMetadataPage();
  return {reinterpret_cast<uint8_t *>(m_metadata_data_base.get()) +
              mmap_metadata.data_offset,
          static_cast<size_t>(mmap_metadata.data_size)};
#endif
}

ArrayRef<uint8_t> PerfEvent::GetAuxBuffer() const {
#ifndef PERF_ATTR_SIZE_VER5
  llvm_unreachable("Intel PT Linux perf event not supported");
#else
  perf_event_mmap_page &mmap_metadata = GetMetadataPage();
  return {reinterpret_cast<uint8_t *>(m_aux_base.get()),
          static_cast<size_t>(mmap_metadata.aux_size)};
#endif
}

Expected<std::vector<uint8_t>> PerfEvent::GetReadOnlyDataBuffer() {
  // The following code assumes that the protection level of the DATA page
  // is PROT_READ. If PROT_WRITE is used, then reading would require that
  // this piece of code updates some pointers. See more about data_tail
  // in https://man7.org/linux/man-pages/man2/perf_event_open.2.html.

#ifndef PERF_ATTR_SIZE_VER5
  return createStringError(inconvertibleErrorCode(),
                           "Intel PT Linux perf event not supported");
#else
  bool was_enabled = m_enabled;
  if (Error err = DisableWithIoctl())
    return std::move(err);

  /**
   * The data buffer and aux buffer have different implementations
   * with respect to their definition of head pointer when using PROD_READ only.
   * In the case of Aux data buffer the head always wraps around the aux buffer
   * and we don't need to care about it, whereas the data_head keeps
   * increasing and needs to be wrapped by modulus operator
   */
  perf_event_mmap_page &mmap_metadata = GetMetadataPage();

  ArrayRef<uint8_t> data = GetDataBuffer();
  uint64_t data_head = mmap_metadata.data_head;
  uint64_t data_size = mmap_metadata.data_size;
  std::vector<uint8_t> output;
  output.reserve(data.size());

  if (data_head > data_size) {
    uint64_t actual_data_head = data_head % data_size;
    // The buffer has wrapped, so we first the oldest chunk of data
    output.insert(output.end(), data.begin() + actual_data_head, data.end());
    // And we read the most recent chunk of data
    output.insert(output.end(), data.begin(), data.begin() + actual_data_head);
  } else {
    // There's been no wrapping, so we just read linearly
    output.insert(output.end(), data.begin(), data.begin() + data_head);
  }

  if (was_enabled) {
    if (Error err = EnableWithIoctl())
      return std::move(err);
  }

  return output;
#endif
}

Expected<std::vector<uint8_t>> PerfEvent::GetReadOnlyAuxBuffer() {
  // The following code assumes that the protection level of the AUX page
  // is PROT_READ. If PROT_WRITE is used, then reading would require that
  // this piece of code updates some pointers. See more about aux_tail
  // in https://man7.org/linux/man-pages/man2/perf_event_open.2.html.

#ifndef PERF_ATTR_SIZE_VER5
  return createStringError(inconvertibleErrorCode(),
                           "Intel PT Linux perf event not supported");
#else
  bool was_enabled = m_enabled;
  if (Error err = DisableWithIoctl())
    return std::move(err);

  perf_event_mmap_page &mmap_metadata = GetMetadataPage();

  ArrayRef<uint8_t> data = GetAuxBuffer();
  uint64_t aux_head = mmap_metadata.aux_head;
  std::vector<uint8_t> output;
  output.reserve(data.size());

  /**
   * When configured as ring buffer, the aux buffer keeps wrapping around
   * the buffer and its not possible to detect how many times the buffer
   * wrapped. Initially the buffer is filled with zeros,as shown below
   * so in order to get complete buffer we first copy firstpartsize, followed
   * by any left over part from beginning to aux_head
   *
   * aux_offset [d,d,d,d,d,d,d,d,0,0,0,0,0,0,0,0,0,0,0] aux_size
   *                 aux_head->||<- firstpartsize  ->|
   *
   * */

  output.insert(output.end(), data.begin() + aux_head, data.end());
  output.insert(output.end(), data.begin(), data.begin() + aux_head);

  if (was_enabled) {
    if (Error err = EnableWithIoctl())
      return std::move(err);
  }

  return output;
#endif
}

Error PerfEvent::DisableWithIoctl() {
  if (!m_enabled)
    return Error::success();

  if (ioctl(*m_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) < 0)
    return createStringError(inconvertibleErrorCode(),
                             "Can't disable perf event. %s",
                             std::strerror(errno));

  m_enabled = false;
  return Error::success();
}

bool PerfEvent::IsEnabled() const { return m_enabled; }

Error PerfEvent::EnableWithIoctl() {
  if (m_enabled)
    return Error::success();

  if (ioctl(*m_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) < 0)
    return createStringError(inconvertibleErrorCode(),
                             "Can't enable perf event. %s",
                             std::strerror(errno));

  m_enabled = true;
  return Error::success();
}

size_t PerfEvent::GetEffectiveDataBufferSize() const {
#ifndef PERF_ATTR_SIZE_VER5
  llvm_unreachable("Intel PT Linux perf event not supported");
#else
  perf_event_mmap_page &mmap_metadata = GetMetadataPage();
  if (mmap_metadata.data_head < mmap_metadata.data_size)
    return mmap_metadata.data_head;
  else
    return mmap_metadata.data_size; // The buffer has wrapped.
#endif
}

Expected<PerfEvent>
lldb_private::process_linux::CreateContextSwitchTracePerfEvent(
    lldb::cpu_id_t cpu_id, const PerfEvent *parent_perf_event) {
  Log *log = GetLog(POSIXLog::Trace);
#ifndef PERF_ATTR_SIZE_VER5
  return createStringError(inconvertibleErrorCode(),
                           "Intel PT Linux perf event not supported");
#else
  perf_event_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sample_type = PERF_SAMPLE_TID | PERF_SAMPLE_TIME;
  attr.type = PERF_TYPE_SOFTWARE;
  attr.context_switch = 1;
  attr.exclude_kernel = 1;
  attr.sample_id_all = 1;
  attr.exclude_hv = 1;
  attr.disabled = parent_perf_event ? !parent_perf_event->IsEnabled() : false;

  // The given perf configuration will produce context switch records of 32
  // bytes each. Assuming that every context switch will be emitted twice (one
  // for context switch ins and another one for context switch outs), and that a
  // context switch will happen at least every half a millisecond per core, we
  // need 500 * 32 bytes (~16 KB) for a trace of one second, which is much more
  // than what a regular intel pt trace can get. Pessimistically we pick as
  // 32KiB for the size of our context switch trace.

  uint64_t data_buffer_size = 32768;
  uint64_t data_buffer_numpages = data_buffer_size / getpagesize();

  LLDB_LOG(log, "Will create context switch trace buffer of size {0}",
           data_buffer_size);

  std::optional<long> group_fd;
  if (parent_perf_event)
    group_fd = parent_perf_event->GetFd();

  if (Expected<PerfEvent> perf_event = PerfEvent::Init(
          attr, /*pid=*/std::nullopt, cpu_id, group_fd, /*flags=*/0)) {
    if (Error mmap_err = perf_event->MmapMetadataAndBuffers(
            data_buffer_numpages, 0, /*data_buffer_write=*/false)) {
      return std::move(mmap_err);
    }
    return perf_event;
  } else {
    return perf_event.takeError();
  }
#endif
}
