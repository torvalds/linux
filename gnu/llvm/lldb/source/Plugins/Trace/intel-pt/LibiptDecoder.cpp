//===-- LibiptDecoder.cpp --======-----------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibiptDecoder.h"
#include "TraceIntelPT.h"
#include "lldb/Target/Process.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

bool IsLibiptError(int status) { return status < 0; }

bool IsEndOfStream(int status) {
  assert(status >= 0 && "We can't check if we reached the end of the stream if "
                        "we got a failed status");
  return status & pts_eos;
}

bool HasEvents(int status) {
  assert(status >= 0 && "We can't check for events if we got a failed status");
  return status & pts_event_pending;
}

// RAII deleter for libipt's decoders
auto InsnDecoderDeleter = [](pt_insn_decoder *decoder) {
  pt_insn_free_decoder(decoder);
};

auto QueryDecoderDeleter = [](pt_query_decoder *decoder) {
  pt_qry_free_decoder(decoder);
};

using PtInsnDecoderUP =
    std::unique_ptr<pt_insn_decoder, decltype(InsnDecoderDeleter)>;

using PtQueryDecoderUP =
    std::unique_ptr<pt_query_decoder, decltype(QueryDecoderDeleter)>;

/// Create a basic configuration object limited to a given buffer that can be
/// used for many different decoders.
static Expected<pt_config> CreateBasicLibiptConfig(TraceIntelPT &trace_intel_pt,
                                                   ArrayRef<uint8_t> buffer) {
  Expected<pt_cpu> cpu_info = trace_intel_pt.GetCPUInfo();
  if (!cpu_info)
    return cpu_info.takeError();

  pt_config config;
  pt_config_init(&config);
  config.cpu = *cpu_info;

  int status = pt_cpu_errata(&config.errata, &config.cpu);
  if (IsLibiptError(status))
    return make_error<IntelPTError>(status);

  // The libipt library does not modify the trace buffer, hence the
  // following casts are safe.
  config.begin = const_cast<uint8_t *>(buffer.data());
  config.end = const_cast<uint8_t *>(buffer.data() + buffer.size());
  return config;
}

/// Callback used by libipt for reading the process memory.
///
/// More information can be found in
/// https://github.com/intel/libipt/blob/master/doc/man/pt_image_set_callback.3.md
static int ReadProcessMemory(uint8_t *buffer, size_t size,
                             const pt_asid * /* unused */, uint64_t pc,
                             void *context) {
  Process *process = static_cast<Process *>(context);

  Status error;
  int bytes_read = process->ReadMemory(pc, buffer, size, error);
  if (error.Fail())
    return -pte_nomap;
  return bytes_read;
}

/// Set up the memory image callback for the given decoder.
static Error SetupMemoryImage(pt_insn_decoder *decoder, Process &process) {
  pt_image *image = pt_insn_get_image(decoder);

  int status = pt_image_set_callback(image, ReadProcessMemory, &process);
  if (IsLibiptError(status))
    return make_error<IntelPTError>(status);
  return Error::success();
}

/// Create an instruction decoder for the given buffer and the given process.
static Expected<PtInsnDecoderUP>
CreateInstructionDecoder(TraceIntelPT &trace_intel_pt, ArrayRef<uint8_t> buffer,
                         Process &process) {
  Expected<pt_config> config = CreateBasicLibiptConfig(trace_intel_pt, buffer);
  if (!config)
    return config.takeError();

  pt_insn_decoder *decoder_ptr = pt_insn_alloc_decoder(&*config);
  if (!decoder_ptr)
    return make_error<IntelPTError>(-pte_nomem);

  PtInsnDecoderUP decoder_up(decoder_ptr, InsnDecoderDeleter);

  if (Error err = SetupMemoryImage(decoder_ptr, process))
    return std::move(err);

  return decoder_up;
}

/// Create a query decoder for the given buffer. The query decoder is the
/// highest level decoder that operates directly on packets and doesn't perform
/// actual instruction decoding. That's why it can be useful for inspecting a
/// raw trace without pinning it to a particular process.
static Expected<PtQueryDecoderUP>
CreateQueryDecoder(TraceIntelPT &trace_intel_pt, ArrayRef<uint8_t> buffer) {
  Expected<pt_config> config = CreateBasicLibiptConfig(trace_intel_pt, buffer);
  if (!config)
    return config.takeError();

  pt_query_decoder *decoder_ptr = pt_qry_alloc_decoder(&*config);
  if (!decoder_ptr)
    return make_error<IntelPTError>(-pte_nomem);

  return PtQueryDecoderUP(decoder_ptr, QueryDecoderDeleter);
}

/// Class used to identify anomalies in traces, which should often indicate a
/// fatal error in the trace.
class PSBBlockAnomalyDetector {
public:
  PSBBlockAnomalyDetector(pt_insn_decoder &decoder,
                          TraceIntelPT &trace_intel_pt,
                          DecodedThread &decoded_thread)
      : m_decoder(decoder), m_decoded_thread(decoded_thread) {
    m_infinite_decoding_loop_threshold =
        trace_intel_pt.GetGlobalProperties()
            .GetInfiniteDecodingLoopVerificationThreshold();
    m_extremely_large_decoding_threshold =
        trace_intel_pt.GetGlobalProperties()
            .GetExtremelyLargeDecodingThreshold();
    m_next_infinite_decoding_loop_threshold =
        m_infinite_decoding_loop_threshold;
  }

  /// \return
  ///   An \a llvm::Error if an anomaly that includes the last instruction item
  ///   in the trace, or \a llvm::Error::success otherwise.
  Error DetectAnomaly() {
    RefreshPacketOffset();
    uint64_t insn_added_since_last_packet_offset =
        m_decoded_thread.GetTotalInstructionCount() -
        m_insn_count_at_last_packet_offset;

    // We want to check if we might have fallen in an infinite loop. As this
    // check is not a no-op, we want to do it when we have a strong suggestion
    // that things went wrong. First, we check how many instructions we have
    // decoded since we processed an Intel PT packet for the last time. This
    // number should be low, because at some point we should see branches, jumps
    // or interrupts that require a new packet to be processed. Once we reach
    // certain threshold we start analyzing the trace.
    //
    // We use the number of decoded instructions since the last Intel PT packet
    // as a proxy because, in fact, we don't expect a single packet to give,
    // say, 100k instructions. That would mean that there are 100k sequential
    // instructions without any single branch, which is highly unlikely, or that
    // we found an infinite loop using direct jumps, e.g.
    //
    //   0x0A: nop or pause
    //   0x0C: jump to 0x0A
    //
    // which is indeed code that is found in the kernel. I presume we reach
    // this kind of code in the decoder because we don't handle self-modified
    // code in post-mortem kernel traces.
    //
    // We are right now only signaling the anomaly as a trace error, but it
    // would be more conservative to also discard all the trace items found in
    // this PSB. I prefer not to do that for the time being to give more
    // exposure to this kind of anomalies and help debugging. Discarding the
    // trace items would just make investigation harded.
    //
    // Finally, if the user wants to see if a specific thread has an anomaly,
    // it's enough to run the `thread trace dump info` command and look for the
    // count of this kind of errors.

    if (insn_added_since_last_packet_offset >=
        m_extremely_large_decoding_threshold) {
      // In this case, we have decoded a massive amount of sequential
      // instructions that don't loop. Honestly I wonder if this will ever
      // happen, but better safe than sorry.
      return createStringError(
          inconvertibleErrorCode(),
          "anomalous trace: possible infinite trace detected");
    }
    if (insn_added_since_last_packet_offset ==
        m_next_infinite_decoding_loop_threshold) {
      if (std::optional<uint64_t> loop_size = TryIdentifyInfiniteLoop()) {
        return createStringError(
            inconvertibleErrorCode(),
            "anomalous trace: possible infinite loop detected of size %" PRIu64,
            *loop_size);
      }
      m_next_infinite_decoding_loop_threshold *= 2;
    }
    return Error::success();
  }

private:
  std::optional<uint64_t> TryIdentifyInfiniteLoop() {
    // The infinite decoding loops we'll encounter are due to sequential
    // instructions that repeat themselves due to direct jumps, therefore in a
    // cycle each individual address will only appear once. We use this
    // information to detect cycles by finding the last 2 ocurrences of the last
    // instruction added to the trace. Then we traverse the trace making sure
    // that these two instructions where the ends of a repeating loop.

    // This is a utility that returns the most recent instruction index given a
    // position in the trace. If the given position is an instruction, that
    // position is returned. It skips non-instruction items.
    auto most_recent_insn_index =
        [&](uint64_t item_index) -> std::optional<uint64_t> {
      while (true) {
        if (m_decoded_thread.GetItemKindByIndex(item_index) ==
            lldb::eTraceItemKindInstruction) {
          return item_index;
        }
        if (item_index == 0)
          return std::nullopt;
        item_index--;
      }
      return std::nullopt;
    };
    // Similar to most_recent_insn_index but skips the starting position.
    auto prev_insn_index = [&](uint64_t item_index) -> std::optional<uint64_t> {
      if (item_index == 0)
        return std::nullopt;
      return most_recent_insn_index(item_index - 1);
    };

    // We first find the most recent instruction.
    std::optional<uint64_t> last_insn_index_opt =
        *prev_insn_index(m_decoded_thread.GetItemsCount());
    if (!last_insn_index_opt)
      return std::nullopt;
    uint64_t last_insn_index = *last_insn_index_opt;

    // We then find the most recent previous occurrence of that last
    // instruction.
    std::optional<uint64_t> last_insn_copy_index =
        prev_insn_index(last_insn_index);
    uint64_t loop_size = 1;
    while (last_insn_copy_index &&
           m_decoded_thread.GetInstructionLoadAddress(*last_insn_copy_index) !=
               m_decoded_thread.GetInstructionLoadAddress(last_insn_index)) {
      last_insn_copy_index = prev_insn_index(*last_insn_copy_index);
      loop_size++;
    }
    if (!last_insn_copy_index)
      return std::nullopt;

    // Now we check if the segment between these last positions of the last
    // instruction address is in fact a repeating loop.
    uint64_t loop_elements_visited = 1;
    uint64_t insn_index_a = last_insn_index,
             insn_index_b = *last_insn_copy_index;
    while (loop_elements_visited < loop_size) {
      if (std::optional<uint64_t> prev = prev_insn_index(insn_index_a))
        insn_index_a = *prev;
      else
        return std::nullopt;
      if (std::optional<uint64_t> prev = prev_insn_index(insn_index_b))
        insn_index_b = *prev;
      else
        return std::nullopt;
      if (m_decoded_thread.GetInstructionLoadAddress(insn_index_a) !=
          m_decoded_thread.GetInstructionLoadAddress(insn_index_b))
        return std::nullopt;
      loop_elements_visited++;
    }
    return loop_size;
  }

  // Refresh the internal counters if a new packet offset has been visited
  void RefreshPacketOffset() {
    lldb::addr_t new_packet_offset;
    if (!IsLibiptError(pt_insn_get_offset(&m_decoder, &new_packet_offset)) &&
        new_packet_offset != m_last_packet_offset) {
      m_last_packet_offset = new_packet_offset;
      m_next_infinite_decoding_loop_threshold =
          m_infinite_decoding_loop_threshold;
      m_insn_count_at_last_packet_offset =
          m_decoded_thread.GetTotalInstructionCount();
    }
  }

  pt_insn_decoder &m_decoder;
  DecodedThread &m_decoded_thread;
  lldb::addr_t m_last_packet_offset = LLDB_INVALID_ADDRESS;
  uint64_t m_insn_count_at_last_packet_offset = 0;
  uint64_t m_infinite_decoding_loop_threshold;
  uint64_t m_next_infinite_decoding_loop_threshold;
  uint64_t m_extremely_large_decoding_threshold;
};

/// Class that decodes a raw buffer for a single PSB block using the low level
/// libipt library. It assumes that kernel and user mode instructions are not
/// mixed in the same PSB block.
///
/// Throughout this code, the status of the decoder will be used to identify
/// events needed to be processed or errors in the decoder. The values can be
/// - negative: actual errors
/// - positive or zero: not an error, but a list of bits signaling the status
/// of the decoder, e.g. whether there are events that need to be decoded or
/// not.
class PSBBlockDecoder {
public:
  /// \param[in] decoder
  ///     A decoder configured to start and end within the boundaries of the
  ///     given \p psb_block.
  ///
  /// \param[in] psb_block
  ///     The PSB block to decode.
  ///
  /// \param[in] next_block_ip
  ///     The starting ip at the next PSB block of the same thread if available.
  ///
  /// \param[in] decoded_thread
  ///     A \a DecodedThread object where the decoded instructions will be
  ///     appended to. It might have already some instructions.
  ///
  /// \param[in] tsc_upper_bound
  ///   Maximum allowed value of TSCs decoded from this PSB block.
  ///   Any of this PSB's data occurring after this TSC will be excluded.
  PSBBlockDecoder(PtInsnDecoderUP &&decoder_up, const PSBBlock &psb_block,
                  std::optional<lldb::addr_t> next_block_ip,
                  DecodedThread &decoded_thread, TraceIntelPT &trace_intel_pt,
                  std::optional<DecodedThread::TSC> tsc_upper_bound)
      : m_decoder_up(std::move(decoder_up)), m_psb_block(psb_block),
        m_next_block_ip(next_block_ip), m_decoded_thread(decoded_thread),
        m_anomaly_detector(*m_decoder_up, trace_intel_pt, decoded_thread),
        m_tsc_upper_bound(tsc_upper_bound) {}

  /// \param[in] trace_intel_pt
  ///     The main Trace object that own the PSB block.
  ///
  /// \param[in] decoder
  ///     A decoder configured to start and end within the boundaries of the
  ///     given \p psb_block.
  ///
  /// \param[in] psb_block
  ///     The PSB block to decode.
  ///
  /// \param[in] buffer
  ///     The raw intel pt trace for this block.
  ///
  /// \param[in] process
  ///     The process to decode. It provides the memory image to use for
  ///     decoding.
  ///
  /// \param[in] next_block_ip
  ///     The starting ip at the next PSB block of the same thread if available.
  ///
  /// \param[in] decoded_thread
  ///     A \a DecodedThread object where the decoded instructions will be
  ///     appended to. It might have already some instructions.
  static Expected<PSBBlockDecoder>
  Create(TraceIntelPT &trace_intel_pt, const PSBBlock &psb_block,
         ArrayRef<uint8_t> buffer, Process &process,
         std::optional<lldb::addr_t> next_block_ip,
         DecodedThread &decoded_thread,
         std::optional<DecodedThread::TSC> tsc_upper_bound) {
    Expected<PtInsnDecoderUP> decoder_up =
        CreateInstructionDecoder(trace_intel_pt, buffer, process);
    if (!decoder_up)
      return decoder_up.takeError();

    return PSBBlockDecoder(std::move(*decoder_up), psb_block, next_block_ip,
                           decoded_thread, trace_intel_pt, tsc_upper_bound);
  }

  void DecodePSBBlock() {
    int status = pt_insn_sync_forward(m_decoder_up.get());
    assert(status >= 0 &&
           "Synchronization shouldn't fail because this PSB was previously "
           "decoded correctly.");

    // We emit a TSC before a sync event to more easily associate a timestamp to
    // the sync event. If present, the current block's TSC would be the first
    // TSC we'll see when processing events.
    if (m_psb_block.tsc)
      m_decoded_thread.NotifyTsc(*m_psb_block.tsc);

    m_decoded_thread.NotifySyncPoint(m_psb_block.psb_offset);

    DecodeInstructionsAndEvents(status);
  }

private:
  /// Append an instruction and return \b false if and only if a serious anomaly
  /// has been detected.
  bool AppendInstructionAndDetectAnomalies(const pt_insn &insn) {
    m_decoded_thread.AppendInstruction(insn);

    if (Error err = m_anomaly_detector.DetectAnomaly()) {
      m_decoded_thread.AppendCustomError(toString(std::move(err)),
                                         /*fatal=*/true);
      return false;
    }
    return true;
  }
  /// Decode all the instructions and events of the given PSB block. The
  /// decoding loop might stop abruptly if an infinite decoding loop is
  /// detected.
  void DecodeInstructionsAndEvents(int status) {
    pt_insn insn;

    while (true) {
      status = ProcessPTEvents(status);

      if (IsLibiptError(status))
        return;
      else if (IsEndOfStream(status))
        break;

      // The status returned by pt_insn_next will need to be processed
      // by ProcessPTEvents in the next loop if it is not an error.
      std::memset(&insn, 0, sizeof insn);
      status = pt_insn_next(m_decoder_up.get(), &insn, sizeof(insn));

      if (IsLibiptError(status)) {
        m_decoded_thread.AppendError(IntelPTError(status, insn.ip));
        return;
      } else if (IsEndOfStream(status)) {
        break;
      }

      if (!AppendInstructionAndDetectAnomalies(insn))
        return;
    }

    // We need to keep querying non-branching instructions until we hit the
    // starting point of the next PSB. We won't see events at this point. This
    // is based on
    // https://github.com/intel/libipt/blob/master/doc/howto_libipt.md#parallel-decode
    if (m_next_block_ip && insn.ip != 0) {
      while (insn.ip != *m_next_block_ip) {
        if (!AppendInstructionAndDetectAnomalies(insn))
          return;

        status = pt_insn_next(m_decoder_up.get(), &insn, sizeof(insn));

        if (IsLibiptError(status)) {
          m_decoded_thread.AppendError(IntelPTError(status, insn.ip));
          return;
        }
      }
    }
  }

  /// Process the TSC of a decoded PT event. Specifically, check if this TSC
  /// is below the TSC upper bound for this PSB. If the TSC exceeds the upper
  /// bound, return an error to abort decoding. Otherwise add the it to the
  /// underlying DecodedThread and decoding should continue as expected.
  ///
  /// \param[in] tsc
  ///   The TSC of the a decoded event.
  Error ProcessPTEventTSC(DecodedThread::TSC tsc) {
    if (m_tsc_upper_bound && tsc >= *m_tsc_upper_bound) {
      // This event and all the remaining events of this PSB have a TSC
      // outside the range of the "owning" ThreadContinuousExecution. For
      // now we drop all of these events/instructions, future work can
      // improve upon this by determining the "owning"
      // ThreadContinuousExecution of the remaining PSB data.
      std::string err_msg = formatv("decoding truncated: TSC {0} exceeds "
                                    "maximum TSC value {1}, will skip decoding"
                                    " the remaining data of the PSB",
                                    tsc, *m_tsc_upper_bound)
                                .str();

      uint64_t offset;
      int status = pt_insn_get_offset(m_decoder_up.get(), &offset);
      if (!IsLibiptError(status)) {
        err_msg = formatv("{2} (skipping {0} of {1} bytes)", offset,
                          m_psb_block.size, err_msg)
                      .str();
      }
      m_decoded_thread.AppendCustomError(err_msg);
      return createStringError(inconvertibleErrorCode(), err_msg);
    } else {
      m_decoded_thread.NotifyTsc(tsc);
      return Error::success();
    }
  }

  /// Before querying instructions, we need to query the events associated with
  /// that instruction, e.g. timing and trace disablement events.
  ///
  /// \param[in] status
  ///   The status gotten from the previous instruction decoding or PSB
  ///   synchronization.
  ///
  /// \return
  ///     The pte_status after decoding events.
  int ProcessPTEvents(int status) {
    while (HasEvents(status)) {
      pt_event event;
      std::memset(&event, 0, sizeof event);
      status = pt_insn_event(m_decoder_up.get(), &event, sizeof(event));

      if (IsLibiptError(status)) {
        m_decoded_thread.AppendError(IntelPTError(status));
        return status;
      }

      if (event.has_tsc) {
        if (Error err = ProcessPTEventTSC(event.tsc)) {
          consumeError(std::move(err));
          return -pte_internal;
        }
      }

      switch (event.type) {
      case ptev_disabled:
        // The CPU paused tracing the program, e.g. due to ip filtering.
        m_decoded_thread.AppendEvent(lldb::eTraceEventDisabledHW);
        break;
      case ptev_async_disabled:
        // The kernel or user code paused tracing the program, e.g.
        // a breakpoint or a ioctl invocation pausing the trace, or a
        // context switch happened.
        m_decoded_thread.AppendEvent(lldb::eTraceEventDisabledSW);
        break;
      case ptev_overflow:
        // The CPU internal buffer had an overflow error and some instructions
        // were lost. A OVF packet comes with an FUP packet (harcoded address)
        // according to the documentation, so we'll continue seeing instructions
        // after this event.
        m_decoded_thread.AppendError(IntelPTError(-pte_overflow));
        break;
      default:
        break;
      }
    }

    return status;
  }

private:
  PtInsnDecoderUP m_decoder_up;
  PSBBlock m_psb_block;
  std::optional<lldb::addr_t> m_next_block_ip;
  DecodedThread &m_decoded_thread;
  PSBBlockAnomalyDetector m_anomaly_detector;
  std::optional<DecodedThread::TSC> m_tsc_upper_bound;
};

Error lldb_private::trace_intel_pt::DecodeSingleTraceForThread(
    DecodedThread &decoded_thread, TraceIntelPT &trace_intel_pt,
    ArrayRef<uint8_t> buffer) {
  Expected<std::vector<PSBBlock>> blocks =
      SplitTraceIntoPSBBlock(trace_intel_pt, buffer, /*expect_tscs=*/false);
  if (!blocks)
    return blocks.takeError();

  for (size_t i = 0; i < blocks->size(); i++) {
    PSBBlock &block = blocks->at(i);

    Expected<PSBBlockDecoder> decoder = PSBBlockDecoder::Create(
        trace_intel_pt, block, buffer.slice(block.psb_offset, block.size),
        *decoded_thread.GetThread()->GetProcess(),
        i + 1 < blocks->size() ? blocks->at(i + 1).starting_ip : std::nullopt,
        decoded_thread, std::nullopt);
    if (!decoder)
      return decoder.takeError();

    decoder->DecodePSBBlock();
  }

  return Error::success();
}

Error lldb_private::trace_intel_pt::DecodeSystemWideTraceForThread(
    DecodedThread &decoded_thread, TraceIntelPT &trace_intel_pt,
    const DenseMap<lldb::cpu_id_t, llvm::ArrayRef<uint8_t>> &buffers,
    const std::vector<IntelPTThreadContinousExecution> &executions) {
  bool has_seen_psbs = false;
  for (size_t i = 0; i < executions.size(); i++) {
    const IntelPTThreadContinousExecution &execution = executions[i];

    auto variant = execution.thread_execution.variant;

    // We emit the first valid tsc
    if (execution.psb_blocks.empty()) {
      decoded_thread.NotifyTsc(execution.thread_execution.GetLowestKnownTSC());
    } else {
      assert(execution.psb_blocks.front().tsc &&
             "per cpu decoding expects TSCs");
      decoded_thread.NotifyTsc(
          std::min(execution.thread_execution.GetLowestKnownTSC(),
                   *execution.psb_blocks.front().tsc));
    }

    // We then emit the CPU, which will be correctly associated with a tsc.
    decoded_thread.NotifyCPU(execution.thread_execution.cpu_id);

    // If we haven't seen a PSB yet, then it's fine not to show errors
    if (has_seen_psbs) {
      if (execution.psb_blocks.empty()) {
        decoded_thread.AppendCustomError(
            formatv("Unable to find intel pt data a thread "
                    "execution on cpu id = {0}",
                    execution.thread_execution.cpu_id)
                .str());
      }

      // A hinted start is a non-initial execution that doesn't have a switch
      // in. An only end is an initial execution that doesn't have a switch in.
      // Any of those cases represent a gap because we have seen a PSB before.
      if (variant == ThreadContinuousExecution::Variant::HintedStart ||
          variant == ThreadContinuousExecution::Variant::OnlyEnd) {
        decoded_thread.AppendCustomError(
            formatv("Unable to find the context switch in for a thread "
                    "execution on cpu id = {0}",
                    execution.thread_execution.cpu_id)
                .str());
      }
    }

    for (size_t j = 0; j < execution.psb_blocks.size(); j++) {
      const PSBBlock &psb_block = execution.psb_blocks[j];

      Expected<PSBBlockDecoder> decoder = PSBBlockDecoder::Create(
          trace_intel_pt, psb_block,
          buffers.lookup(execution.thread_execution.cpu_id)
              .slice(psb_block.psb_offset, psb_block.size),
          *decoded_thread.GetThread()->GetProcess(),
          j + 1 < execution.psb_blocks.size()
              ? execution.psb_blocks[j + 1].starting_ip
              : std::nullopt,
          decoded_thread, execution.thread_execution.GetEndTSC());
      if (!decoder)
        return decoder.takeError();

      has_seen_psbs = true;
      decoder->DecodePSBBlock();
    }

    // If we haven't seen a PSB yet, then it's fine not to show errors
    if (has_seen_psbs) {
      // A hinted end is a non-ending execution that doesn't have a switch out.
      // An only start is an ending execution that doesn't have a switch out.
      // Any of those cases represent a gap if we still have executions to
      // process and we have seen a PSB before.
      if (i + 1 != executions.size() &&
          (variant == ThreadContinuousExecution::Variant::OnlyStart ||
           variant == ThreadContinuousExecution::Variant::HintedEnd)) {
        decoded_thread.AppendCustomError(
            formatv("Unable to find the context switch out for a thread "
                    "execution on cpu id = {0}",
                    execution.thread_execution.cpu_id)
                .str());
      }
    }
  }
  return Error::success();
}

bool IntelPTThreadContinousExecution::operator<(
    const IntelPTThreadContinousExecution &o) const {
  // As the context switch might be incomplete, we look first for the first real
  // PSB packet, which is a valid TSC. Otherwise, We query the thread execution
  // itself for some tsc.
  auto get_tsc = [](const IntelPTThreadContinousExecution &exec) {
    return exec.psb_blocks.empty() ? exec.thread_execution.GetLowestKnownTSC()
                                   : exec.psb_blocks.front().tsc;
  };

  return get_tsc(*this) < get_tsc(o);
}

Expected<std::vector<PSBBlock>>
lldb_private::trace_intel_pt::SplitTraceIntoPSBBlock(
    TraceIntelPT &trace_intel_pt, llvm::ArrayRef<uint8_t> buffer,
    bool expect_tscs) {
  // This follows
  // https://github.com/intel/libipt/blob/master/doc/howto_libipt.md#parallel-decode

  Expected<PtQueryDecoderUP> decoder_up =
      CreateQueryDecoder(trace_intel_pt, buffer);
  if (!decoder_up)
    return decoder_up.takeError();

  pt_query_decoder *decoder = decoder_up.get().get();

  std::vector<PSBBlock> executions;

  while (true) {
    uint64_t maybe_ip = LLDB_INVALID_ADDRESS;
    int decoding_status = pt_qry_sync_forward(decoder, &maybe_ip);
    if (IsLibiptError(decoding_status))
      break;

    uint64_t psb_offset;
    int offset_status = pt_qry_get_sync_offset(decoder, &psb_offset);
    assert(offset_status >= 0 &&
           "This can't fail because we were able to synchronize");

    std::optional<uint64_t> ip;
    if (!(pts_ip_suppressed & decoding_status))
      ip = maybe_ip;

    std::optional<uint64_t> tsc;
    // Now we fetch the first TSC that comes after the PSB.
    while (HasEvents(decoding_status)) {
      pt_event event;
      decoding_status = pt_qry_event(decoder, &event, sizeof(event));
      if (IsLibiptError(decoding_status))
        break;
      if (event.has_tsc) {
        tsc = event.tsc;
        break;
      }
    }
    if (IsLibiptError(decoding_status)) {
      // We continue to the next PSB. This effectively merges this PSB with the
      // previous one, and that should be fine because this PSB might be the
      // direct continuation of the previous thread and it's better to show an
      // error in the decoded thread than to hide it. If this is the first PSB,
      // we are okay losing it. Besides that, an error at processing events
      // means that we wouldn't be able to get any instruction out of it.
      continue;
    }

    if (expect_tscs && !tsc)
      return createStringError(inconvertibleErrorCode(),
                               "Found a PSB without TSC.");

    executions.push_back({
        psb_offset,
        tsc,
        0,
        ip,
    });
  }
  if (!executions.empty()) {
    // We now adjust the sizes of each block
    executions.back().size = buffer.size() - executions.back().psb_offset;
    for (int i = (int)executions.size() - 2; i >= 0; i--) {
      executions[i].size =
          executions[i + 1].psb_offset - executions[i].psb_offset;
    }
  }
  return executions;
}

Expected<std::optional<uint64_t>>
lldb_private::trace_intel_pt::FindLowestTSCInTrace(TraceIntelPT &trace_intel_pt,
                                                   ArrayRef<uint8_t> buffer) {
  Expected<PtQueryDecoderUP> decoder_up =
      CreateQueryDecoder(trace_intel_pt, buffer);
  if (!decoder_up)
    return decoder_up.takeError();

  pt_query_decoder *decoder = decoder_up.get().get();
  uint64_t ip = LLDB_INVALID_ADDRESS;
  int status = pt_qry_sync_forward(decoder, &ip);
  if (IsLibiptError(status))
    return std::nullopt;

  while (HasEvents(status)) {
    pt_event event;
    status = pt_qry_event(decoder, &event, sizeof(event));
    if (IsLibiptError(status))
      return std::nullopt;
    if (event.has_tsc)
      return event.tsc;
  }
  return std::nullopt;
}
