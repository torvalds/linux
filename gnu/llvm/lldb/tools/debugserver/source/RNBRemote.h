
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 12/12/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBREMOTE_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBREMOTE_H

#include "DNB.h"
#include "PThreadMutex.h"
#include "RNBContext.h"
#include "RNBDefs.h"
#include "RNBSocket.h"
#include <deque>
#include <map>
#include <string>
#include <vector>

class RNBSocket;
class RNBContext;
class PThreadEvents;

enum event_loop_mode { debug_nub, gdb_remote_protocol, done };

enum class compression_types { zlib_deflate, lz4, lzma, lzfse, none };

class RNBRemote {
public:
  // clang-format off
  enum PacketEnum {
    invalid_packet = 0,
    ack,                           // '+'
    nack,                          // '-'
    halt,                          // ^C  (async halt)
    use_extended_mode,             // '!'
    why_halted,                    // '?'
    set_argv,                      // 'A'
    set_bp,                        // 'B'
    cont,                          // 'c'
    continue_with_sig,             // 'C'
    detach,                        // 'D'
    read_general_regs,             // 'g'
    write_general_regs,            // 'G'
    set_thread,                    // 'H'
    step_inferior_one_cycle,       // 'i'
    signal_and_step_inf_one_cycle, // 'I'
    kill,                          // 'k'
    read_memory,                   // 'm'
    write_memory,                  // 'M'
    read_register,                 // 'p'
    write_register,                // 'P'
    restart,                       // 'R'
    single_step,                   // 's'
    single_step_with_sig,          // 'S'
    search_mem_backwards,          // 't'
    thread_alive_p,                // 'T'
    vattach,                       // 'vAttach;pid'
    vattachwait,   // 'vAttachWait:XX...' where XX is one or more hex encoded
                   // process name ASCII bytes
    vattachorwait, // 'vAttachOrWait:XX...' where XX is one or more hex encoded
                   // process name ASCII bytes
    vattachname,   // 'vAttachName:XX...' where XX is one or more hex encoded
                   // process name ASCII bytes
    vcont,         // 'vCont'
    vcont_list_actions,     // 'vCont?'
    read_data_from_memory,  // 'x'
    write_data_to_memory,   // 'X'
    insert_mem_bp,          // 'Z0'
    remove_mem_bp,          // 'z0'
    insert_hardware_bp,     // 'Z1'
    remove_hardware_bp,     // 'z1'
    insert_write_watch_bp,  // 'Z2'
    remove_write_watch_bp,  // 'z2'
    insert_read_watch_bp,   // 'Z3'
    remove_read_watch_bp,   // 'z3'
    insert_access_watch_bp, // 'Z4'
    remove_access_watch_bp, // 'z4'

    query_monitor,                                 // 'qRcmd'
    query_current_thread_id,                       // 'qC'
    query_get_pid,                                 // 'qGetPid'
    query_echo,                                    // 'qEcho'
    query_thread_ids_first,                        // 'qfThreadInfo'
    query_thread_ids_subsequent,                   // 'qsThreadInfo'
    query_thread_extra_info,                       // 'qThreadExtraInfo'
    query_thread_stop_info,                        // 'qThreadStopInfo'
    query_image_offsets,                           // 'qOffsets'
    query_symbol_lookup,                           // 'qSymbol'
    query_launch_success,                          // 'qLaunchSuccess'
    query_register_info,                           // 'qRegisterInfo'
    query_shlib_notify_info_addr,                  // 'qShlibInfoAddr'
    query_step_packet_supported,                   // 'qStepPacketSupported'
    query_supported_features,                      // 'qSupported'
    query_vattachorwait_supported,                 // 'qVAttachOrWaitSupported'
    query_sync_thread_state_supported,             // 'QSyncThreadState'
    query_host_info,                               // 'qHostInfo'
    query_gdb_server_version,                      // 'qGDBServerVersion'
    query_process_info,                            // 'qProcessInfo'
    json_query_thread_extended_info,               // 'jThreadExtendedInfo'
    json_query_get_loaded_dynamic_libraries_infos, // 'jGetLoadedDynamicLibrariesInfos'
    json_query_threads_info,                       // 'jThreadsInfo'
    json_query_get_shared_cache_info,              // 'jGetSharedCacheInfo'
    pass_signals_to_inferior,                      // 'QPassSignals'
    start_noack_mode,                              // 'QStartNoAckMode'
    prefix_reg_packets_with_tid,        // 'QPrefixRegisterPacketsWithThreadID
    set_logging_mode,                   // 'QSetLogging:'
    set_ignored_exceptions,             // 'QSetIgnoredExceptions'           
    set_max_packet_size,                // 'QSetMaxPacketSize:'
    set_max_payload_size,               // 'QSetMaxPayloadSize:'
    set_environment_variable,           // 'QEnvironment:'
    set_environment_variable_hex,       // 'QEnvironmentHexEncoded:'
    set_launch_arch,                    // 'QLaunchArch:'
    set_disable_aslr,                   // 'QSetDisableASLR:'
    set_stdin,                          // 'QSetSTDIN:'
    set_stdout,                         // 'QSetSTDOUT:'
    set_stderr,                         // 'QSetSTDERR:'
    set_working_dir,                    // 'QSetWorkingDir:'
    set_list_threads_in_stop_reply,     // 'QListThreadsInStopReply:'
    sync_thread_state,                  // 'QSyncThreadState:'
    memory_region_info,                 // 'qMemoryRegionInfo:'
    get_profile_data,                   // 'qGetProfileData'
    set_enable_profiling,               // 'QSetEnableAsyncProfiling'
    enable_compression,                 // 'QEnableCompression:'
    watchpoint_support_info,            // 'qWatchpointSupportInfo:'
    allocate_memory,                    // '_M'
    deallocate_memory,                  // '_m'
    set_process_event,                  // 'QSetProcessEvent:'
    save_register_state,                // '_g'
    restore_register_state,             // '_G'
    speed_test,                         // 'qSpeedTest:'
    set_detach_on_error,                // 'QSetDetachOnError:'
    query_transfer,                     // 'qXfer:'
    json_query_dyld_process_state,      // 'jGetDyldProcessState'
    enable_error_strings,               // 'QEnableErrorStrings'
    unknown_type
  };
  // clang-format on

  typedef rnb_err_t (RNBRemote::*HandlePacketCallback)(const char *p);

  RNBRemote();
  ~RNBRemote();

  void Initialize();

  bool InitializeRegisters(bool force = false);

  rnb_err_t HandleAsyncPacket(PacketEnum *type = NULL);
  rnb_err_t HandleReceivedPacket(PacketEnum *type = NULL);

  nub_thread_t GetContinueThread() const { return m_continue_thread; }

  void SetContinueThread(nub_thread_t tid) { m_continue_thread = tid; }

  nub_thread_t GetCurrentThread() const {
    if (m_thread == 0 || m_thread == (nub_thread_t)-1)
      return DNBProcessGetCurrentThread(m_ctx.ProcessID());
    return m_thread;
  }

  void SetCurrentThread(nub_thread_t tid) {
    DNBProcessSetCurrentThread(m_ctx.ProcessID(), tid);
    m_thread = tid;
  }

  static void *ThreadFunctionReadRemoteData(void *arg);
  void StartReadRemoteDataThread();
  void StopReadRemoteDataThread();

  void NotifyThatProcessStopped(void);

  rnb_err_t HandlePacket_A(const char *p);
  rnb_err_t HandlePacket_H(const char *p);
  rnb_err_t HandlePacket_qC(const char *p);
  rnb_err_t HandlePacket_qRcmd(const char *p);
  rnb_err_t HandlePacket_qGetPid(const char *p);
  rnb_err_t HandlePacket_qEcho(const char *p);
  rnb_err_t HandlePacket_qLaunchSuccess(const char *p);
  rnb_err_t HandlePacket_qRegisterInfo(const char *p);
  rnb_err_t HandlePacket_qShlibInfoAddr(const char *p);
  rnb_err_t HandlePacket_qStepPacketSupported(const char *p);
  rnb_err_t HandlePacket_qVAttachOrWaitSupported(const char *p);
  rnb_err_t HandlePacket_qSyncThreadStateSupported(const char *p);
  rnb_err_t HandlePacket_qThreadInfo(const char *p);
  rnb_err_t HandlePacket_jThreadExtendedInfo(const char *p);
  rnb_err_t HandlePacket_jGetLoadedDynamicLibrariesInfos(const char *p);
  rnb_err_t HandlePacket_jThreadsInfo(const char *p);
  rnb_err_t HandlePacket_jGetSharedCacheInfo(const char *p);
  rnb_err_t HandlePacket_qThreadExtraInfo(const char *p);
  rnb_err_t HandlePacket_qThreadStopInfo(const char *p);
  rnb_err_t HandlePacket_qHostInfo(const char *p);
  rnb_err_t HandlePacket_qGDBServerVersion(const char *p);
  rnb_err_t HandlePacket_qProcessInfo(const char *p);
  rnb_err_t HandlePacket_qSymbol(const char *p);
  rnb_err_t HandlePacket_QEnableErrorStrings(const char *p);
  rnb_err_t HandlePacket_QStartNoAckMode(const char *p);
  rnb_err_t HandlePacket_QThreadSuffixSupported(const char *p);
  rnb_err_t HandlePacket_QSetLogging(const char *p);
  rnb_err_t HandlePacket_QSetIgnoredExceptions(const char *p);
  rnb_err_t HandlePacket_QSetDisableASLR(const char *p);
  rnb_err_t HandlePacket_QSetSTDIO(const char *p);
  rnb_err_t HandlePacket_QSetWorkingDir(const char *p);
  rnb_err_t HandlePacket_QSetMaxPayloadSize(const char *p);
  rnb_err_t HandlePacket_QSetMaxPacketSize(const char *p);
  rnb_err_t HandlePacket_QEnvironment(const char *p);
  rnb_err_t HandlePacket_QEnvironmentHexEncoded(const char *p);
  rnb_err_t HandlePacket_QLaunchArch(const char *p);
  rnb_err_t HandlePacket_QListThreadsInStopReply(const char *p);
  rnb_err_t HandlePacket_QSyncThreadState(const char *p);
  rnb_err_t HandlePacket_QPrefixRegisterPacketsWithThreadID(const char *p);
  rnb_err_t HandlePacket_QSetProcessEvent(const char *p);
  rnb_err_t HandlePacket_last_signal(const char *p);
  rnb_err_t HandlePacket_m(const char *p);
  rnb_err_t HandlePacket_M(const char *p);
  rnb_err_t HandlePacket_x(const char *p);
  rnb_err_t HandlePacket_X(const char *p);
  rnb_err_t HandlePacket_g(const char *p);
  rnb_err_t HandlePacket_G(const char *p);
  rnb_err_t HandlePacket_z(const char *p);
  rnb_err_t HandlePacket_T(const char *p);
  rnb_err_t HandlePacket_p(const char *p);
  rnb_err_t HandlePacket_P(const char *p);
  rnb_err_t HandlePacket_c(const char *p);
  rnb_err_t HandlePacket_C(const char *p);
  rnb_err_t HandlePacket_D(const char *p);
  rnb_err_t HandlePacket_k(const char *p);
  rnb_err_t HandlePacket_s(const char *p);
  rnb_err_t HandlePacket_S(const char *p);
  rnb_err_t HandlePacket_qSupported(const char *p);
  rnb_err_t HandlePacket_v(const char *p);
  rnb_err_t HandlePacket_UNIMPLEMENTED(const char *p);
  rnb_err_t HandlePacket_ILLFORMED(const char *file, int line, const char *p,
                                   const char *description);
  rnb_err_t HandlePacket_AllocateMemory(const char *p);
  rnb_err_t HandlePacket_DeallocateMemory(const char *p);
  rnb_err_t HandlePacket_SaveRegisterState(const char *p);
  rnb_err_t HandlePacket_RestoreRegisterState(const char *p);
  rnb_err_t HandlePacket_MemoryRegionInfo(const char *p);
  rnb_err_t HandlePacket_GetProfileData(const char *p);
  rnb_err_t HandlePacket_SetEnableAsyncProfiling(const char *p);
  rnb_err_t HandlePacket_QEnableCompression(const char *p);
  rnb_err_t HandlePacket_WatchpointSupportInfo(const char *p);
  rnb_err_t HandlePacket_qSpeedTest(const char *p);
  rnb_err_t HandlePacket_qXfer(const char *p);
  rnb_err_t HandlePacket_stop_process(const char *p);
  rnb_err_t HandlePacket_QSetDetachOnError(const char *p);
  rnb_err_t HandlePacket_jGetDyldProcessState(const char *p);
  rnb_err_t SendStopReplyPacketForThread(nub_thread_t tid);
  rnb_err_t SendHexEncodedBytePacket(const char *header, const void *buf,
                                     size_t buf_len, const char *footer);
  rnb_err_t SendSTDOUTPacket(char *buf, nub_size_t buf_size);
  rnb_err_t SendSTDERRPacket(char *buf, nub_size_t buf_size);
  void FlushSTDIO();
  void SendAsyncProfileData();
  rnb_err_t SendAsyncProfileDataPacket(char *buf, nub_size_t buf_size);
  rnb_err_t SendAsyncJSONPacket(const JSONGenerator::Dictionary &dictionary);

  RNBContext &Context() { return m_ctx; }
  RNBSocket &Comm() { return m_comm; }

private:
  RNBRemote(const RNBRemote &) = delete;

protected:
  rnb_err_t GetCommData();
  void CommDataReceived(const std::string &data);
  struct Packet {
    typedef std::vector<Packet> collection;
    typedef collection::iterator iterator;
    typedef collection::const_iterator const_iterator;
    PacketEnum type;
    HandlePacketCallback normal; // Function to call when inferior is halted
    HandlePacketCallback async;  // Function to call when inferior is running
    std::string abbrev;
    std::string printable_name;

    bool IsPlatformPacket() const {
      switch (type) {
      case set_logging_mode:
      case query_host_info:
        return true;
      default:
        break;
      }
      return false;
    }
    Packet()
        : type(invalid_packet), normal(NULL), async(NULL), abbrev(),
          printable_name() {}

    Packet(PacketEnum in_type, HandlePacketCallback in_normal,
           HandlePacketCallback in_async, const char *in_abbrev,
           const char *in_printable_name)
        : type(in_type), normal(in_normal), async(in_async), abbrev(in_abbrev),
          printable_name(in_printable_name) {}
  };

  struct DispatchQueueOffsets {
    uint16_t dqo_version;
    uint16_t dqo_label;
    uint16_t dqo_label_size;
    uint16_t dqo_flags;
    uint16_t dqo_flags_size;
    uint16_t dqo_serialnum;
    uint16_t dqo_serialnum_size;
    uint16_t dqo_width;
    uint16_t dqo_width_size;
    uint16_t dqo_running;
    uint16_t dqo_running_size;
    uint16_t dqo_suspend_cnt; // version 5 and later, starting with Mac OS X
                              // 10.10/iOS 8
    uint16_t dqo_suspend_cnt_size; // version 5 and later, starting with Mac OS
                                   // X 10.10/iOS 8
    uint16_t dqo_target_queue; // version 5 and later, starting with Mac OS X
                               // 10.10/iOS 8
    uint16_t dqo_target_queue_size; // version 5 and later, starting with Mac OS
                                    // X 10.10/iOS 8
    uint16_t
        dqo_priority; // version 5 and later, starting with Mac OS X 10.10/iOS 8
    uint16_t dqo_priority_size; // version 5 and later, starting with Mac OS X
                                // 10.10/iOS 8

    DispatchQueueOffsets() { Clear(); }

    void Clear() {
      dqo_version = UINT16_MAX;
      dqo_label = UINT16_MAX;
      dqo_label_size = UINT16_MAX;
      dqo_flags = UINT16_MAX;
      dqo_flags_size = UINT16_MAX;
      dqo_serialnum = UINT16_MAX;
      dqo_serialnum_size = UINT16_MAX;
      dqo_width = UINT16_MAX;
      dqo_width_size = UINT16_MAX;
      dqo_running = UINT16_MAX;
      dqo_running_size = UINT16_MAX;
      dqo_suspend_cnt = UINT16_MAX;
      dqo_suspend_cnt_size = UINT16_MAX;
      dqo_target_queue = UINT16_MAX;
      dqo_target_queue_size = UINT16_MAX;
      dqo_priority = UINT16_MAX;
      dqo_priority_size = UINT16_MAX;
    }

    bool IsValid() const { return dqo_version != UINT16_MAX; }

    void GetThreadQueueInfo(nub_process_t pid, nub_addr_t dispatch_qaddr,
                            nub_addr_t &dispatch_queue_t,
                            std::string &queue_name, uint64_t &queue_width,
                            uint64_t &queue_serialnum) const;
  };

  rnb_err_t GetPacket(std::string &packet_data, RNBRemote::Packet &packet_info,
                      bool wait);
  rnb_err_t SendPacket(const std::string &);
  rnb_err_t SendErrorPacket(std::string errcode,
                            const std::string &errmsg = "");
  std::string CompressString(const std::string &);

  void CreatePacketTable();
  rnb_err_t GetPacketPayload(std::string &);

  nub_thread_t ExtractThreadIDFromThreadSuffix(const char *p);

  void EnableCompressionNextSendPacket(compression_types);

  compression_types GetCompressionType();

  const DispatchQueueOffsets *GetDispatchQueueOffsets();

  JSONGenerator::ObjectSP
  GetJSONThreadsInfo(bool threads_with_valid_stop_info_only);

  RNBContext m_ctx; // process context
  RNBSocket m_comm; // communication port
  std::string m_arch;
  nub_thread_t m_continue_thread; // thread to continue; 0 for any, -1 for all
  nub_thread_t m_thread;          // thread for other ops; 0 for any, -1 for all
  PThreadMutex m_mutex;           // Mutex that protects
  DispatchQueueOffsets m_dispatch_queue_offsets;
  nub_addr_t m_dispatch_queue_offsets_addr;
  uint32_t m_qSymbol_index;
  uint32_t m_packets_recvd;
  Packet::collection m_packets;
  std::deque<std::string> m_rx_packets;
  std::string m_rx_partial_data; // For packets that may come in more than one
                                 // batch, anything left over can be left here
  pthread_t m_rx_pthread;
  uint32_t
      m_max_payload_size; // the maximum sized payload we should send to gdb
  bool m_extended_mode;   // are we in extended mode?
  bool m_noack_mode;      // are we in no-ack mode?
  bool m_thread_suffix_supported; // Set to true if the 'p', 'P', 'g', and 'G'
                                  // packets should be prefixed with the thread
                                  // ID and colon:
                                  // "$pRR;thread:TTTT;" instead of "$pRR"
  // "$PRR=VVVVVVVV;thread:TTTT;" instead of "$PRR=VVVVVVVV"
  // "$g;thread:TTTT" instead of "$g"
  // "$GVVVVVVVVVVVVVV;thread:TTTT;#00 instead of "$GVVVVVVVVVVVVVV"
  bool m_list_threads_in_stop_reply;

  size_t m_compression_minsize; // only packets larger than this size will be
                                // compressed
  bool m_enable_compression_next_send_packet;

  compression_types m_compression_mode;

  bool m_enable_error_strings; // Whether we can append asciihex error strings
                               // after Exx error replies
};

/* We translate the /usr/include/mach/exception_types.h exception types
   (e.g. EXC_BAD_ACCESS) to the fake BSD signal numbers that gdb uses
   in include/gdb/signals.h (e.g. TARGET_EXC_BAD_ACCESS).  These hard
   coded values for TARGET_EXC_BAD_ACCESS et al must match the gdb
   values in its include/gdb/signals.h.  */

#define TARGET_EXC_BAD_ACCESS 0x91
#define TARGET_EXC_BAD_INSTRUCTION 0x92
#define TARGET_EXC_ARITHMETIC 0x93
#define TARGET_EXC_EMULATION 0x94
#define TARGET_EXC_SOFTWARE 0x95
#define TARGET_EXC_BREAKPOINT 0x96

/* Generally speaking, you can't assume gdb can receive more than 399 bytes
   at a time with a random gdb.  This bufsize constant is only specifying
   how many bytes gdb can *receive* from debugserver -- it tells us nothing
   about how many bytes gdb might try to send in a single packet.  */
#define DEFAULT_GDB_REMOTE_PROTOCOL_BUFSIZE 399

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBREMOTE_H
