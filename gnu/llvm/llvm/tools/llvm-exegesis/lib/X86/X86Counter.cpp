//===-- X86Counter.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "X86Counter.h"

#if defined(__linux__) && defined(HAVE_LIBPFM) &&                              \
    defined(LIBPFM_HAS_FIELD_CYCLES)

// FIXME: Use appropriate wrappers for poll.h and mman.h
// to support Windows and remove this linux-only guard.

#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"

#include <perfmon/perf_event.h>
#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_perf_event.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

namespace llvm {
namespace exegesis {

// Number of entries in the LBR.
static constexpr int kLbrEntries = 16;
static constexpr size_t kBufferPages = 8;
static const size_t kDataBufferSize = kBufferPages * getpagesize();

// First page is reserved for perf_event_mmap_page. Data buffer starts on
// the next page, so we allocate one more page.
static const size_t kMappedBufferSize = (kBufferPages + 1) * getpagesize();

// Waits for the LBR perf events.
static int pollLbrPerfEvent(const int FileDescriptor) {
  struct pollfd PollFd;
  PollFd.fd = FileDescriptor;
  PollFd.events = POLLIN;
  PollFd.revents = 0;
  return poll(&PollFd, 1 /* num of fds */, 10000 /* timeout in ms */);
}

// Copies the data-buffer into Buf, given the pointer to MMapped.
static void copyDataBuffer(void *MMappedBuffer, char *Buf, uint64_t Tail,
                           size_t DataSize) {
  // First page is reserved for perf_event_mmap_page. Data buffer starts on
  // the next page.
  char *Start = reinterpret_cast<char *>(MMappedBuffer) + getpagesize();
  // The LBR buffer is a cyclic buffer, we copy data to another buffer.
  uint64_t Offset = Tail % kDataBufferSize;
  size_t CopySize = kDataBufferSize - Offset;
  memcpy(Buf, Start + Offset, CopySize);
  if (CopySize >= DataSize)
    return;

  memcpy(Buf + CopySize, Start, Offset);
  return;
}

// Parses the given data-buffer for stats and fill the CycleArray.
// If data has been extracted successfully, also modifies the code to jump
// out the benchmark loop.
static Error parseDataBuffer(const char *DataBuf, size_t DataSize,
                             const void *From, const void *To,
                             SmallVector<int64_t, 4> *CycleArray) {
  const char *DataPtr = DataBuf;
  while (DataPtr < DataBuf + DataSize) {
    struct perf_event_header Header;
    memcpy(&Header, DataPtr, sizeof(struct perf_event_header));
    if (Header.type != PERF_RECORD_SAMPLE) {
      // Ignores non-sample records.
      DataPtr += Header.size;
      continue;
    }
    DataPtr += sizeof(Header);
    uint64_t Count = support::endian::read64(DataPtr, endianness::native);
    DataPtr += sizeof(Count);

    struct perf_branch_entry Entry;
    memcpy(&Entry, DataPtr, sizeof(struct perf_branch_entry));

    // Read the perf_branch_entry array.
    for (uint64_t i = 0; i < Count; ++i) {
      const uint64_t BlockStart = From == nullptr
                                      ? std::numeric_limits<uint64_t>::min()
                                      : reinterpret_cast<uint64_t>(From);
      const uint64_t BlockEnd = To == nullptr
                                    ? std::numeric_limits<uint64_t>::max()
                                    : reinterpret_cast<uint64_t>(To);

      if (BlockStart <= Entry.from && BlockEnd >= Entry.to)
        CycleArray->push_back(Entry.cycles);

      if (i == Count - 1)
        // We've reached the last entry.
        return Error::success();

      // Advance to next entry
      DataPtr += sizeof(Entry);
      memcpy(&Entry, DataPtr, sizeof(struct perf_branch_entry));
    }
  }
  return make_error<StringError>("Unable to parse databuffer.", errc::io_error);
}

X86LbrPerfEvent::X86LbrPerfEvent(unsigned SamplingPeriod) {
  assert(SamplingPeriod > 0 && "SamplingPeriod must be positive");
  EventString = "BR_INST_RETIRED.NEAR_TAKEN";
  Attr = new perf_event_attr();
  Attr->size = sizeof(*Attr);
  Attr->type = PERF_TYPE_RAW;
  // FIXME This is SKL's encoding. Not sure if it'll change.
  Attr->config = 0x20c4; // BR_INST_RETIRED.NEAR_TAKEN
  Attr->sample_type = PERF_SAMPLE_BRANCH_STACK;
  // Don't need to specify "USER" because we've already excluded HV and Kernel.
  Attr->branch_sample_type = PERF_SAMPLE_BRANCH_ANY;
  Attr->sample_period = SamplingPeriod;
  Attr->wakeup_events = 1; // We need this even when using ioctl REFRESH.
  Attr->disabled = 1;
  Attr->exclude_kernel = 1;
  Attr->exclude_hv = 1;
  Attr->read_format = PERF_FORMAT_GROUP;

  FullQualifiedEventString = EventString;
}

X86LbrCounter::X86LbrCounter(pfm::PerfEvent &&NewEvent)
    : CounterGroup(std::move(NewEvent), {}) {
  MMappedBuffer = mmap(nullptr, kMappedBufferSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED, getFileDescriptor(), 0);
  if (MMappedBuffer == MAP_FAILED)
    errs() << "Failed to mmap buffer.";
}

X86LbrCounter::~X86LbrCounter() {
  if (0 != munmap(MMappedBuffer, kMappedBufferSize))
    errs() << "Failed to munmap buffer.";
}

void X86LbrCounter::start() {
  ioctl(getFileDescriptor(), PERF_EVENT_IOC_REFRESH, 1024 /* kMaxPollsPerFd */);
}

Error X86LbrCounter::checkLbrSupport() {
  // Do a sample read and check if the results contain non-zero values.

  X86LbrCounter counter(X86LbrPerfEvent(123));
  counter.start();

  // Prevent the compiler from unrolling the loop and get rid of all the
  // branches. We need at least 16 iterations.
  int Sum = 0;
  int V = 1;

  volatile int *P = &V;
  auto TimeLimit =
      std::chrono::high_resolution_clock::now() + std::chrono::microseconds(5);

  for (int I = 0;
       I < kLbrEntries || std::chrono::high_resolution_clock::now() < TimeLimit;
       ++I) {
    Sum += *P;
  }

  counter.stop();
  (void)Sum;

  auto ResultOrError = counter.doReadCounter(nullptr, nullptr);
  if (ResultOrError)
    if (!ResultOrError.get().empty())
      // If there is at least one non-zero entry, then LBR is supported.
      for (const int64_t &Value : ResultOrError.get())
        if (Value != 0)
          return Error::success();

  return make_error<StringError>(
      "LBR format with cycles is not suppported on the host.",
      errc::not_supported);
}

Expected<SmallVector<int64_t, 4>>
X86LbrCounter::readOrError(StringRef FunctionBytes) const {
  // Disable the event before reading
  ioctl(getFileDescriptor(), PERF_EVENT_IOC_DISABLE, 0);

  // Find the boundary of the function so that we could filter the LBRs
  // to keep only the relevant records.
  if (FunctionBytes.empty())
    return make_error<StringError>("Empty function bytes",
                                   errc::invalid_argument);
  const void *From = reinterpret_cast<const void *>(FunctionBytes.data());
  const void *To = reinterpret_cast<const void *>(FunctionBytes.data() +
                                                  FunctionBytes.size());
  return doReadCounter(From, To);
}

Expected<SmallVector<int64_t, 4>>
X86LbrCounter::doReadCounter(const void *From, const void *To) const {
  // The max number of time-outs/retries before we give up.
  static constexpr int kMaxTimeouts = 160;

  // Parses the LBR buffer and fills CycleArray with the sequence of cycle
  // counts from the buffer.
  SmallVector<int64_t, 4> CycleArray;
  auto DataBuf = std::make_unique<char[]>(kDataBufferSize);
  int NumTimeouts = 0;
  int PollResult = 0;

  while (PollResult <= 0) {
    PollResult = pollLbrPerfEvent(getFileDescriptor());
    if (PollResult > 0)
      break;
    if (PollResult == -1)
      return make_error<StringError>("Cannot poll LBR perf event.",
                                     errc::io_error);
    if (NumTimeouts++ >= kMaxTimeouts)
      return make_error<StringError>(
          "LBR polling still timed out after max number of attempts.",
          errc::device_or_resource_busy);
  }

  struct perf_event_mmap_page Page;
  memcpy(&Page, MMappedBuffer, sizeof(struct perf_event_mmap_page));

  const uint64_t DataTail = Page.data_tail;
  const uint64_t DataHead = Page.data_head;
  // We're supposed to use a barrier after reading data_head.
  std::atomic_thread_fence(std::memory_order_acq_rel);
  const size_t DataSize = DataHead - DataTail;
  if (DataSize > kDataBufferSize)
    return make_error<StringError>("DataSize larger than buffer size.",
                                   errc::invalid_argument);

  copyDataBuffer(MMappedBuffer, DataBuf.get(), DataTail, DataSize);
  Error error = parseDataBuffer(DataBuf.get(), DataSize, From, To, &CycleArray);
  if (!error)
    return CycleArray;
  return std::move(error);
}

} // namespace exegesis
} // namespace llvm

#endif // defined(__linux__) && defined(HAVE_LIBPFM) &&
       // defined(LIBPFM_HAS_FIELD_CYCLES)
