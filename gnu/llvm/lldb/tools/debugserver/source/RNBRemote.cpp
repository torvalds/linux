//===-- RNBRemote.cpp -------------------------------------------*- C++ -*-===//
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

#include "RNBRemote.h"

#include <bsm/audit.h>
#include <bsm/audit_session.h>
#include <cerrno>
#include <csignal>
#include <libproc.h>
#include <mach-o/loader.h>
#include <mach/exception_types.h>
#include <mach/mach_vm.h>
#include <mach/task_info.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif

#include "DNB.h"
#include "DNBDataRef.h"
#include "DNBLog.h"
#include "DNBThreadResumeActions.h"
#include "JSON.h"
#include "JSONGenerator.h"
#include "JSONGenerator.h"
#include "MacOSX/Genealogy.h"
#include "OsLogger.h"
#include "RNBContext.h"
#include "RNBServices.h"
#include "RNBSocket.h"
#include "StdStringExtractor.h"

#include <compression.h>

#include <TargetConditionals.h>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_set>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

// constants

static const std::string OS_LOG_EVENTS_KEY_NAME("events");
static const std::string JSON_ASYNC_TYPE_KEY_NAME("type");

// std::iostream formatting macros
#define RAW_HEXBASE std::setfill('0') << std::hex << std::right
#define HEXBASE '0' << 'x' << RAW_HEXBASE
#define RAWHEX8(x) RAW_HEXBASE << std::setw(2) << ((uint32_t)((uint8_t)x))
#define RAWHEX16 RAW_HEXBASE << std::setw(4)
#define RAWHEX32 RAW_HEXBASE << std::setw(8)
#define RAWHEX64 RAW_HEXBASE << std::setw(16)
#define HEX8(x) HEXBASE << std::setw(2) << ((uint32_t)(x))
#define HEX16 HEXBASE << std::setw(4)
#define HEX32 HEXBASE << std::setw(8)
#define HEX64 HEXBASE << std::setw(16)
#define RAW_HEX(x) RAW_HEXBASE << std::setw(sizeof(x) * 2) << (x)
#define HEX(x) HEXBASE << std::setw(sizeof(x) * 2) << (x)
#define RAWHEX_SIZE(x, sz) RAW_HEXBASE << std::setw((sz)) << (x)
#define HEX_SIZE(x, sz) HEXBASE << std::setw((sz)) << (x)
#define STRING_WIDTH(w) std::setfill(' ') << std::setw(w)
#define LEFT_STRING_WIDTH(s, w)                                                \
  std::left << std::setfill(' ') << std::setw(w) << (s) << std::right
#define DECIMAL std::dec << std::setfill(' ')
#define DECIMAL_WIDTH(w) DECIMAL << std::setw(w)
#define FLOAT(n, d)                                                            \
  std::setfill(' ') << std::setw((n) + (d) + 1) << std::setprecision(d)        \
                    << std::showpoint << std::fixed
#define INDENT_WITH_SPACES(iword_idx)                                          \
  std::setfill(' ') << std::setw((iword_idx)) << ""
#define INDENT_WITH_TABS(iword_idx)                                            \
  std::setfill('\t') << std::setw((iword_idx)) << ""
// Class to handle communications via gdb remote protocol.

// Prototypes

static std::string binary_encode_string(const std::string &s);

// Decode a single hex character and return the hex value as a number or
// -1 if "ch" is not a hex character.
static inline int xdigit_to_sint(char ch) {
  if (ch >= 'a' && ch <= 'f')
    return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F')
    return 10 + ch - 'A';
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  return -1;
}

// Decode a single hex ASCII byte. Return -1 on failure, a value 0-255
// on success.
static inline int decoded_hex_ascii_char(const char *p) {
  const int hi_nibble = xdigit_to_sint(p[0]);
  if (hi_nibble == -1)
    return -1;
  const int lo_nibble = xdigit_to_sint(p[1]);
  if (lo_nibble == -1)
    return -1;
  return (uint8_t)((hi_nibble << 4) + lo_nibble);
}

// Decode a hex ASCII string back into a string
static std::string decode_hex_ascii_string(const char *p,
                                           uint32_t max_length = UINT32_MAX) {
  std::string arg;
  if (p) {
    for (const char *c = p; ((c - p) / 2) < max_length; c += 2) {
      int ch = decoded_hex_ascii_char(c);
      if (ch == -1)
        break;
      else
        arg.push_back(ch);
    }
  }
  return arg;
}

uint64_t decode_uint64(const char *p, int base, char **end = nullptr,
                       uint64_t fail_value = 0) {
  nub_addr_t addr = strtoull(p, end, 16);
  if (addr == 0 && errno != 0)
    return fail_value;
  return addr;
}

void append_hex_value(std::ostream &ostrm, const void *buf, size_t buf_size,
                      bool swap) {
  int i;
  const uint8_t *p = (const uint8_t *)buf;
  if (swap) {
    for (i = static_cast<int>(buf_size) - 1; i >= 0; i--)
      ostrm << RAWHEX8(p[i]);
  } else {
    for (size_t i = 0; i < buf_size; i++)
      ostrm << RAWHEX8(p[i]);
  }
}

std::string cstring_to_asciihex_string(const char *str) {
  std::string hex_str;
  hex_str.reserve(strlen(str) * 2);
  while (str && *str) {
    uint8_t c = *str++;
    char hexbuf[5];
    snprintf(hexbuf, sizeof(hexbuf), "%02x", c);
    hex_str += hexbuf;
  }
  return hex_str;
}

void append_hexified_string(std::ostream &ostrm, const std::string &string) {
  size_t string_size = string.size();
  const char *string_buf = string.c_str();
  for (size_t i = 0; i < string_size; i++) {
    ostrm << RAWHEX8(*(string_buf + i));
  }
}

extern void ASLLogCallback(void *baton, uint32_t flags, const char *format,
                           va_list args);

// from System.framework/Versions/B/PrivateHeaders/sys/codesign.h
extern "C" {
#define CS_OPS_STATUS 0       /* return status */
#define CS_RESTRICT 0x0000800 /* tell dyld to treat restricted */
int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

// from rootless.h
bool rootless_allows_task_for_pid(pid_t pid);

// from sys/csr.h
typedef uint32_t csr_config_t;
#define CSR_ALLOW_TASK_FOR_PID (1 << 2)
int csr_check(csr_config_t mask);
}

RNBRemote::RNBRemote()
    : m_ctx(), m_comm(), m_arch(), m_continue_thread(-1), m_thread(-1),
      m_mutex(), m_dispatch_queue_offsets(),
      m_dispatch_queue_offsets_addr(INVALID_NUB_ADDRESS),
      m_qSymbol_index(UINT32_MAX), m_packets_recvd(0), m_packets(),
      m_rx_packets(), m_rx_partial_data(), m_rx_pthread(0),
      m_max_payload_size(DEFAULT_GDB_REMOTE_PROTOCOL_BUFSIZE - 4),
      m_extended_mode(false), m_noack_mode(false),
      m_thread_suffix_supported(false), m_list_threads_in_stop_reply(false),
      m_compression_minsize(384), m_enable_compression_next_send_packet(false),
      m_compression_mode(compression_types::none),
      m_enable_error_strings(false) {
  DNBLogThreadedIf(LOG_RNB_REMOTE, "%s", __PRETTY_FUNCTION__);
  CreatePacketTable();
}

RNBRemote::~RNBRemote() {
  DNBLogThreadedIf(LOG_RNB_REMOTE, "%s", __PRETTY_FUNCTION__);
  StopReadRemoteDataThread();
}

void RNBRemote::CreatePacketTable() {
  // Step required to add new packets:
  // 1 - Add new enumeration to RNBRemote::PacketEnum
  // 2 - Create the RNBRemote::HandlePacket_ function if a new function is
  // needed
  // 3 - Register the Packet definition with any needed callbacks in this
  // function
  //          - If no response is needed for a command, then use NULL for the
  //          normal callback
  //          - If the packet is not supported while the target is running, use
  //          NULL for the async callback
  // 4 - If the packet is a standard packet (starts with a '$' character
  //      followed by the payload and then '#' and checksum, then you are done
  //      else go on to step 5
  // 5 - if the packet is a fixed length packet:
  //      - modify the switch statement for the first character in the payload
  //        in RNBRemote::CommDataReceived so it doesn't reject the new packet
  //        type as invalid
  //      - modify the switch statement for the first character in the payload
  //        in RNBRemote::GetPacketPayload and make sure the payload of the
  //        packet
  //        is returned correctly

  std::vector<Packet> &t = m_packets;
  t.push_back(Packet(ack, NULL, NULL, "+", "ACK"));
  t.push_back(Packet(nack, NULL, NULL, "-", "!ACK"));
  t.push_back(Packet(read_memory, &RNBRemote::HandlePacket_m, NULL, "m",
                     "Read memory"));
  t.push_back(Packet(read_register, &RNBRemote::HandlePacket_p, NULL, "p",
                     "Read one register"));
  t.push_back(Packet(read_general_regs, &RNBRemote::HandlePacket_g, NULL, "g",
                     "Read registers"));
  t.push_back(Packet(write_memory, &RNBRemote::HandlePacket_M, NULL, "M",
                     "Write memory"));
  t.push_back(Packet(write_register, &RNBRemote::HandlePacket_P, NULL, "P",
                     "Write one register"));
  t.push_back(Packet(write_general_regs, &RNBRemote::HandlePacket_G, NULL, "G",
                     "Write registers"));
  t.push_back(Packet(insert_mem_bp, &RNBRemote::HandlePacket_z, NULL, "Z0",
                     "Insert memory breakpoint"));
  t.push_back(Packet(remove_mem_bp, &RNBRemote::HandlePacket_z, NULL, "z0",
                     "Remove memory breakpoint"));
  t.push_back(Packet(single_step, &RNBRemote::HandlePacket_s, NULL, "s",
                     "Single step"));
  t.push_back(Packet(cont, &RNBRemote::HandlePacket_c, NULL, "c", "continue"));
  t.push_back(Packet(single_step_with_sig, &RNBRemote::HandlePacket_S, NULL,
                     "S", "Single step with signal"));
  t.push_back(
      Packet(set_thread, &RNBRemote::HandlePacket_H, NULL, "H", "Set thread"));
  t.push_back(Packet(halt, &RNBRemote::HandlePacket_last_signal,
                     &RNBRemote::HandlePacket_stop_process, "\x03", "^C"));
  //  t.push_back (Packet (use_extended_mode,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "!", "Use extended mode"));
  t.push_back(Packet(why_halted, &RNBRemote::HandlePacket_last_signal, NULL,
                     "?", "Why did target halt"));
  t.push_back(
      Packet(set_argv, &RNBRemote::HandlePacket_A, NULL, "A", "Set argv"));
  //  t.push_back (Packet (set_bp,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "B", "Set/clear
  //  breakpoint"));
  t.push_back(Packet(continue_with_sig, &RNBRemote::HandlePacket_C, NULL, "C",
                     "Continue with signal"));
  t.push_back(Packet(detach, &RNBRemote::HandlePacket_D, NULL, "D",
                     "Detach gdb from remote system"));
  //  t.push_back (Packet (step_inferior_one_cycle,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "i", "Step inferior by one
  //  clock cycle"));
  //  t.push_back (Packet (signal_and_step_inf_one_cycle,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "I", "Signal inferior, then
  //  step one clock cycle"));
  t.push_back(Packet(kill, &RNBRemote::HandlePacket_k, NULL, "k", "Kill"));
  //  t.push_back (Packet (restart,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "R", "Restart inferior"));
  //  t.push_back (Packet (search_mem_backwards,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "t", "Search memory
  //  backwards"));
  t.push_back(Packet(thread_alive_p, &RNBRemote::HandlePacket_T, NULL, "T",
                     "Is thread alive"));
  t.push_back(Packet(query_supported_features,
                     &RNBRemote::HandlePacket_qSupported, NULL, "qSupported",
                     "Query about supported features"));
  t.push_back(Packet(vattach, &RNBRemote::HandlePacket_v, NULL, "vAttach",
                     "Attach to a new process"));
  t.push_back(Packet(vattachwait, &RNBRemote::HandlePacket_v, NULL,
                     "vAttachWait",
                     "Wait for a process to start up then attach to it"));
  t.push_back(Packet(vattachorwait, &RNBRemote::HandlePacket_v, NULL,
                     "vAttachOrWait", "Attach to the process or if it doesn't "
                                      "exist, wait for the process to start up "
                                      "then attach to it"));
  t.push_back(Packet(vattachname, &RNBRemote::HandlePacket_v, NULL,
                     "vAttachName", "Attach to an existing process by name"));
  t.push_back(Packet(vcont_list_actions, &RNBRemote::HandlePacket_v, NULL,
                     "vCont;", "Verbose resume with thread actions"));
  t.push_back(Packet(vcont_list_actions, &RNBRemote::HandlePacket_v, NULL,
                     "vCont?",
                     "List valid continue-with-thread-actions actions"));
  t.push_back(Packet(read_data_from_memory, &RNBRemote::HandlePacket_x, NULL,
                     "x", "Read data from memory"));
  t.push_back(Packet(write_data_to_memory, &RNBRemote::HandlePacket_X, NULL,
                     "X", "Write data to memory"));
  t.push_back(Packet(insert_hardware_bp, &RNBRemote::HandlePacket_z, NULL, "Z1",
                     "Insert hardware breakpoint"));
  t.push_back(Packet(remove_hardware_bp, &RNBRemote::HandlePacket_z, NULL, "z1",
                     "Remove hardware breakpoint"));
  t.push_back(Packet(insert_write_watch_bp, &RNBRemote::HandlePacket_z, NULL,
                     "Z2", "Insert write watchpoint"));
  t.push_back(Packet(remove_write_watch_bp, &RNBRemote::HandlePacket_z, NULL,
                     "z2", "Remove write watchpoint"));
  t.push_back(Packet(insert_read_watch_bp, &RNBRemote::HandlePacket_z, NULL,
                     "Z3", "Insert read watchpoint"));
  t.push_back(Packet(remove_read_watch_bp, &RNBRemote::HandlePacket_z, NULL,
                     "z3", "Remove read watchpoint"));
  t.push_back(Packet(insert_access_watch_bp, &RNBRemote::HandlePacket_z, NULL,
                     "Z4", "Insert access watchpoint"));
  t.push_back(Packet(remove_access_watch_bp, &RNBRemote::HandlePacket_z, NULL,
                     "z4", "Remove access watchpoint"));
  t.push_back(Packet(query_monitor, &RNBRemote::HandlePacket_qRcmd, NULL,
                     "qRcmd", "Monitor command"));
  t.push_back(Packet(query_current_thread_id, &RNBRemote::HandlePacket_qC, NULL,
                     "qC", "Query current thread ID"));
  t.push_back(Packet(query_echo, &RNBRemote::HandlePacket_qEcho, NULL, "qEcho:",
                     "Echo the packet back to allow the debugger to sync up "
                     "with this server"));
  t.push_back(Packet(query_get_pid, &RNBRemote::HandlePacket_qGetPid, NULL,
                     "qGetPid", "Query process id"));
  t.push_back(Packet(query_thread_ids_first,
                     &RNBRemote::HandlePacket_qThreadInfo, NULL, "qfThreadInfo",
                     "Get list of active threads (first req)"));
  t.push_back(Packet(query_thread_ids_subsequent,
                     &RNBRemote::HandlePacket_qThreadInfo, NULL, "qsThreadInfo",
                     "Get list of active threads (subsequent req)"));
  // APPLE LOCAL: qThreadStopInfo
  // syntax: qThreadStopInfoTTTT
  //  TTTT is hex thread ID
  t.push_back(Packet(query_thread_stop_info,
                     &RNBRemote::HandlePacket_qThreadStopInfo, NULL,
                     "qThreadStopInfo",
                     "Get detailed info on why the specified thread stopped"));
  t.push_back(Packet(query_thread_extra_info,
                     &RNBRemote::HandlePacket_qThreadExtraInfo, NULL,
                     "qThreadExtraInfo", "Get printable status of a thread"));
  //  t.push_back (Packet (query_image_offsets,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "qOffsets", "Report offset
  //  of loaded program"));
  t.push_back(Packet(
      query_launch_success, &RNBRemote::HandlePacket_qLaunchSuccess, NULL,
      "qLaunchSuccess", "Report the success or failure of the launch attempt"));
  t.push_back(
      Packet(query_register_info, &RNBRemote::HandlePacket_qRegisterInfo, NULL,
             "qRegisterInfo",
             "Dynamically discover remote register context information."));
  t.push_back(Packet(
      query_shlib_notify_info_addr, &RNBRemote::HandlePacket_qShlibInfoAddr,
      NULL, "qShlibInfoAddr", "Returns the address that contains info needed "
                              "for getting shared library notifications"));
  t.push_back(Packet(query_step_packet_supported,
                     &RNBRemote::HandlePacket_qStepPacketSupported, NULL,
                     "qStepPacketSupported",
                     "Replys with OK if the 's' packet is supported."));
  t.push_back(
      Packet(query_vattachorwait_supported,
             &RNBRemote::HandlePacket_qVAttachOrWaitSupported, NULL,
             "qVAttachOrWaitSupported",
             "Replys with OK if the 'vAttachOrWait' packet is supported."));
  t.push_back(
      Packet(query_sync_thread_state_supported,
             &RNBRemote::HandlePacket_qSyncThreadStateSupported, NULL,
             "qSyncThreadStateSupported",
             "Replys with OK if the 'QSyncThreadState:' packet is supported."));
  t.push_back(Packet(
      query_host_info, &RNBRemote::HandlePacket_qHostInfo, NULL, "qHostInfo",
      "Replies with multiple 'key:value;' tuples appended to each other."));
  t.push_back(Packet(
      query_gdb_server_version, &RNBRemote::HandlePacket_qGDBServerVersion,
      NULL, "qGDBServerVersion",
      "Replies with multiple 'key:value;' tuples appended to each other."));
  t.push_back(Packet(
      query_process_info, &RNBRemote::HandlePacket_qProcessInfo, NULL,
      "qProcessInfo",
      "Replies with multiple 'key:value;' tuples appended to each other."));
  t.push_back(Packet(
      query_symbol_lookup, &RNBRemote::HandlePacket_qSymbol, NULL, "qSymbol:",
      "Notify that host debugger is ready to do symbol lookups"));
  t.push_back(Packet(enable_error_strings,
                     &RNBRemote::HandlePacket_QEnableErrorStrings, NULL,
                     "QEnableErrorStrings",
                     "Tell " DEBUGSERVER_PROGRAM_NAME
                     " it can append descriptive error messages in replies."));
  t.push_back(Packet(json_query_thread_extended_info,
                     &RNBRemote::HandlePacket_jThreadExtendedInfo, NULL,
                     "jThreadExtendedInfo",
                     "Replies with JSON data of thread extended information."));
  t.push_back(Packet(json_query_get_loaded_dynamic_libraries_infos,
                     &RNBRemote::HandlePacket_jGetLoadedDynamicLibrariesInfos,
                     NULL, "jGetLoadedDynamicLibrariesInfos",
                     "Replies with JSON data of all the shared libraries "
                     "loaded in this process."));
  t.push_back(
      Packet(json_query_threads_info, &RNBRemote::HandlePacket_jThreadsInfo,
             NULL, "jThreadsInfo",
             "Replies with JSON data with information about all threads."));
  t.push_back(Packet(json_query_get_shared_cache_info,
                     &RNBRemote::HandlePacket_jGetSharedCacheInfo, NULL,
                     "jGetSharedCacheInfo", "Replies with JSON data about the "
                                            "location and uuid of the shared "
                                            "cache in the inferior process."));
  t.push_back(Packet(start_noack_mode, &RNBRemote::HandlePacket_QStartNoAckMode,
                     NULL, "QStartNoAckMode",
                     "Request that " DEBUGSERVER_PROGRAM_NAME
                     " stop acking remote protocol packets"));
  t.push_back(Packet(prefix_reg_packets_with_tid,
                     &RNBRemote::HandlePacket_QThreadSuffixSupported, NULL,
                     "QThreadSuffixSupported",
                     "Check if thread specific packets (register packets 'g', "
                     "'G', 'p', and 'P') support having the thread ID appended "
                     "to the end of the command"));
  t.push_back(Packet(set_logging_mode, &RNBRemote::HandlePacket_QSetLogging,
                     NULL, "QSetLogging:", "Turn on log channels in debugserver"));
  t.push_back(Packet(set_ignored_exceptions, &RNBRemote::HandlePacket_QSetIgnoredExceptions,
                     NULL, "QSetIgnoredExceptions:", "Set the exception types "
                                           "debugserver won't wait for, allowing "
                                           "them to be turned into the equivalent "
                                           "BSD signals by the normal means."));
  t.push_back(Packet(
      set_max_packet_size, &RNBRemote::HandlePacket_QSetMaxPacketSize, NULL,
      "QSetMaxPacketSize:",
      "Tell " DEBUGSERVER_PROGRAM_NAME " the max sized packet gdb can handle"));
  t.push_back(Packet(
      set_max_payload_size, &RNBRemote::HandlePacket_QSetMaxPayloadSize, NULL,
      "QSetMaxPayloadSize:", "Tell " DEBUGSERVER_PROGRAM_NAME
                             " the max sized payload gdb can handle"));
  t.push_back(
      Packet(set_environment_variable, &RNBRemote::HandlePacket_QEnvironment,
             NULL, "QEnvironment:",
             "Add an environment variable to the inferior's environment"));
  t.push_back(
      Packet(set_environment_variable_hex,
             &RNBRemote::HandlePacket_QEnvironmentHexEncoded, NULL,
             "QEnvironmentHexEncoded:",
             "Add an environment variable to the inferior's environment"));
  t.push_back(Packet(set_launch_arch, &RNBRemote::HandlePacket_QLaunchArch,
                     NULL, "QLaunchArch:", "Set the architecture to use when "
                                           "launching a process for hosts that "
                                           "can run multiple architecture "
                                           "slices from universal files."));
  t.push_back(Packet(set_disable_aslr, &RNBRemote::HandlePacket_QSetDisableASLR,
                     NULL, "QSetDisableASLR:",
                     "Set whether to disable ASLR when launching the process "
                     "with the set argv ('A') packet"));
  t.push_back(Packet(set_stdin, &RNBRemote::HandlePacket_QSetSTDIO, NULL,
                     "QSetSTDIN:", "Set the standard input for a process to be "
                                   "launched with the 'A' packet"));
  t.push_back(Packet(set_stdout, &RNBRemote::HandlePacket_QSetSTDIO, NULL,
                     "QSetSTDOUT:", "Set the standard output for a process to "
                                    "be launched with the 'A' packet"));
  t.push_back(Packet(set_stderr, &RNBRemote::HandlePacket_QSetSTDIO, NULL,
                     "QSetSTDERR:", "Set the standard error for a process to "
                                    "be launched with the 'A' packet"));
  t.push_back(Packet(set_working_dir, &RNBRemote::HandlePacket_QSetWorkingDir,
                     NULL, "QSetWorkingDir:", "Set the working directory for a "
                                              "process to be launched with the "
                                              "'A' packet"));
  t.push_back(Packet(set_list_threads_in_stop_reply,
                     &RNBRemote::HandlePacket_QListThreadsInStopReply, NULL,
                     "QListThreadsInStopReply",
                     "Set if the 'threads' key should be added to the stop "
                     "reply packets with a list of all thread IDs."));
  t.push_back(Packet(
      sync_thread_state, &RNBRemote::HandlePacket_QSyncThreadState, NULL,
      "QSyncThreadState:", "Do whatever is necessary to make sure 'thread' is "
                           "in a safe state to call functions on."));
  //  t.push_back (Packet (pass_signals_to_inferior,
  //  &RNBRemote::HandlePacket_UNIMPLEMENTED, NULL, "QPassSignals:", "Specify
  //  which signals are passed to the inferior"));
  t.push_back(Packet(allocate_memory, &RNBRemote::HandlePacket_AllocateMemory,
                     NULL, "_M", "Allocate memory in the inferior process."));
  t.push_back(Packet(deallocate_memory,
                     &RNBRemote::HandlePacket_DeallocateMemory, NULL, "_m",
                     "Deallocate memory in the inferior process."));
  t.push_back(Packet(
      save_register_state, &RNBRemote::HandlePacket_SaveRegisterState, NULL,
      "QSaveRegisterState", "Save the register state for the current thread "
                            "and return a decimal save ID."));
  t.push_back(Packet(restore_register_state,
                     &RNBRemote::HandlePacket_RestoreRegisterState, NULL,
                     "QRestoreRegisterState:",
                     "Restore the register state given a save ID previously "
                     "returned from a call to QSaveRegisterState."));
  t.push_back(Packet(
      memory_region_info, &RNBRemote::HandlePacket_MemoryRegionInfo, NULL,
      "qMemoryRegionInfo", "Return size and attributes of a memory region that "
                           "contains the given address"));
  t.push_back(Packet(get_profile_data, &RNBRemote::HandlePacket_GetProfileData,
                     NULL, "qGetProfileData",
                     "Return profiling data of the current target."));
  t.push_back(Packet(set_enable_profiling,
                     &RNBRemote::HandlePacket_SetEnableAsyncProfiling, NULL,
                     "QSetEnableAsyncProfiling",
                     "Enable or disable the profiling of current target."));
  t.push_back(Packet(enable_compression,
                     &RNBRemote::HandlePacket_QEnableCompression, NULL,
                     "QEnableCompression:",
                     "Enable compression for the remainder of the connection"));
  t.push_back(Packet(watchpoint_support_info,
                     &RNBRemote::HandlePacket_WatchpointSupportInfo, NULL,
                     "qWatchpointSupportInfo",
                     "Return the number of supported hardware watchpoints"));
  t.push_back(Packet(set_process_event,
                     &RNBRemote::HandlePacket_QSetProcessEvent, NULL,
                     "QSetProcessEvent:", "Set a process event, to be passed "
                                          "to the process, can be set before "
                                          "the process is started, or after."));
  t.push_back(
      Packet(set_detach_on_error, &RNBRemote::HandlePacket_QSetDetachOnError,
             NULL, "QSetDetachOnError:",
             "Set whether debugserver will detach (1) or kill (0) from the "
             "process it is controlling if it loses connection to lldb."));
  t.push_back(Packet(
      speed_test, &RNBRemote::HandlePacket_qSpeedTest, NULL, "qSpeedTest:",
      "Test the maximum speed at which packet can be sent/received."));
  t.push_back(Packet(query_transfer, &RNBRemote::HandlePacket_qXfer, NULL,
                     "qXfer:", "Support the qXfer packet."));
  t.push_back(Packet(json_query_dyld_process_state,
                     &RNBRemote::HandlePacket_jGetDyldProcessState, NULL,
                     "jGetDyldProcessState",
                     "Query the process state from dyld."));
}

void RNBRemote::FlushSTDIO() {
  if (m_ctx.HasValidProcessID()) {
    nub_process_t pid = m_ctx.ProcessID();
    char buf[256];
    nub_size_t count;
    do {
      count = DNBProcessGetAvailableSTDOUT(pid, buf, sizeof(buf));
      if (count > 0) {
        SendSTDOUTPacket(buf, count);
      }
    } while (count > 0);

    do {
      count = DNBProcessGetAvailableSTDERR(pid, buf, sizeof(buf));
      if (count > 0) {
        SendSTDERRPacket(buf, count);
      }
    } while (count > 0);
  }
}

void RNBRemote::SendAsyncProfileData() {
  if (m_ctx.HasValidProcessID()) {
    nub_process_t pid = m_ctx.ProcessID();
    char buf[1024];
    nub_size_t count;
    do {
      count = DNBProcessGetAvailableProfileData(pid, buf, sizeof(buf));
      if (count > 0) {
        SendAsyncProfileDataPacket(buf, count);
      }
    } while (count > 0);
  }
}

rnb_err_t RNBRemote::SendHexEncodedBytePacket(const char *header,
                                              const void *buf, size_t buf_len,
                                              const char *footer) {
  std::ostringstream packet_sstrm;
  // Append the header cstr if there was one
  if (header && header[0])
    packet_sstrm << header;
  nub_size_t i;
  const uint8_t *ubuf8 = (const uint8_t *)buf;
  for (i = 0; i < buf_len; i++) {
    packet_sstrm << RAWHEX8(ubuf8[i]);
  }
  // Append the footer cstr if there was one
  if (footer && footer[0])
    packet_sstrm << footer;

  return SendPacket(packet_sstrm.str());
}

rnb_err_t RNBRemote::SendSTDOUTPacket(char *buf, nub_size_t buf_size) {
  if (buf_size == 0)
    return rnb_success;
  return SendHexEncodedBytePacket("O", buf, buf_size, NULL);
}

rnb_err_t RNBRemote::SendSTDERRPacket(char *buf, nub_size_t buf_size) {
  if (buf_size == 0)
    return rnb_success;
  return SendHexEncodedBytePacket("O", buf, buf_size, NULL);
}

// This makes use of asynchronous bit 'A' in the gdb remote protocol.
rnb_err_t RNBRemote::SendAsyncProfileDataPacket(char *buf,
                                                nub_size_t buf_size) {
  if (buf_size == 0)
    return rnb_success;

  std::string packet("A");
  packet.append(buf, buf_size);
  return SendPacket(packet);
}

rnb_err_t
RNBRemote::SendAsyncJSONPacket(const JSONGenerator::Dictionary &dictionary) {
  std::ostringstream stream;
  // We're choosing something that is easy to spot if we somehow get one
  // of these coming out at the wrong time (i.e. when the remote side
  // is not waiting for a process control completion response).
  stream << "JSON-async:";
  dictionary.DumpBinaryEscaped(stream);
  return SendPacket(stream.str());
}

// Given a std::string packet contents to send, possibly encode/compress it.
// If compression is enabled, the returned std::string will be in one of two
// forms:
//
//    N<original packet contents uncompressed>
//    C<size of original decompressed packet>:<packet compressed with the
//    requested compression scheme>
//
// If compression is not requested, the original packet contents are returned

std::string RNBRemote::CompressString(const std::string &orig) {
  std::string compressed;
  compression_types compression_type = GetCompressionType();
  if (compression_type != compression_types::none) {
    bool compress_this_packet = false;

    if (orig.size() > m_compression_minsize) {
      compress_this_packet = true;
    }

    if (compress_this_packet) {
      const size_t encoded_data_buf_size = orig.size() + 128;
      std::vector<uint8_t> encoded_data(encoded_data_buf_size);
      size_t compressed_size = 0;

      // Allocate a scratch buffer for libcompression the first
      // time we see a different compression type; reuse it in 
      // all compression_encode_buffer calls so it doesn't need
      // to allocate / free its own scratch buffer each time.
      // This buffer will only be freed when compression type
      // changes; otherwise it will persist until debugserver
      // exit.

      static compression_types g_libcompress_scratchbuf_type = compression_types::none;
      static void *g_libcompress_scratchbuf = nullptr;

      if (g_libcompress_scratchbuf_type != compression_type) {
        if (g_libcompress_scratchbuf) {
          free (g_libcompress_scratchbuf);
          g_libcompress_scratchbuf = nullptr;
        }
        size_t scratchbuf_size = 0;
        switch (compression_type) {
          case compression_types::lz4: 
            scratchbuf_size = compression_encode_scratch_buffer_size (COMPRESSION_LZ4_RAW);
            break;
          case compression_types::zlib_deflate: 
            scratchbuf_size = compression_encode_scratch_buffer_size (COMPRESSION_ZLIB);
            break;
          case compression_types::lzma: 
            scratchbuf_size = compression_encode_scratch_buffer_size (COMPRESSION_LZMA);
            break;
          case compression_types::lzfse: 
            scratchbuf_size = compression_encode_scratch_buffer_size (COMPRESSION_LZFSE);
            break;
          default:
            break;
        }
        if (scratchbuf_size > 0) {
          g_libcompress_scratchbuf = (void*) malloc (scratchbuf_size);
          g_libcompress_scratchbuf_type = compression_type;
        }
      }

      if (compression_type == compression_types::lz4) {
        compressed_size = compression_encode_buffer(
            encoded_data.data(), encoded_data_buf_size,
            (const uint8_t *)orig.c_str(), orig.size(), 
            g_libcompress_scratchbuf,
            COMPRESSION_LZ4_RAW);
      }
      if (compression_type == compression_types::zlib_deflate) {
        compressed_size = compression_encode_buffer(
            encoded_data.data(), encoded_data_buf_size,
            (const uint8_t *)orig.c_str(), orig.size(), 
            g_libcompress_scratchbuf,
            COMPRESSION_ZLIB);
      }
      if (compression_type == compression_types::lzma) {
        compressed_size = compression_encode_buffer(
            encoded_data.data(), encoded_data_buf_size,
            (const uint8_t *)orig.c_str(), orig.size(), 
            g_libcompress_scratchbuf,
            COMPRESSION_LZMA);
      }
      if (compression_type == compression_types::lzfse) {
        compressed_size = compression_encode_buffer(
            encoded_data.data(), encoded_data_buf_size,
            (const uint8_t *)orig.c_str(), orig.size(), 
            g_libcompress_scratchbuf,
            COMPRESSION_LZFSE);
      }

      if (compressed_size > 0) {
        compressed.clear();
        compressed.reserve(compressed_size);
        compressed = "C";
        char numbuf[16];
        snprintf(numbuf, sizeof(numbuf), "%zu:", orig.size());
        numbuf[sizeof(numbuf) - 1] = '\0';
        compressed.append(numbuf);

        for (size_t i = 0; i < compressed_size; i++) {
          uint8_t byte = encoded_data[i];
          if (byte == '#' || byte == '$' || byte == '}' || byte == '*' ||
              byte == '\0') {
            compressed.push_back(0x7d);
            compressed.push_back(byte ^ 0x20);
          } else {
            compressed.push_back(byte);
          }
        }
      } else {
        compressed = "N" + orig;
      }
    } else {
      compressed = "N" + orig;
    }
  } else {
    compressed = orig;
  }

  return compressed;
}

rnb_err_t RNBRemote::SendPacket(const std::string &s) {
  DNBLogThreadedIf(LOG_RNB_MAX, "%8d RNBRemote::%s (%s) called",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                   __FUNCTION__, s.c_str());

  std::string s_compressed = CompressString(s);

  std::string sendpacket = "$" + s_compressed + "#";
  int cksum = 0;
  char hexbuf[5];

  if (m_noack_mode) {
    sendpacket += "00";
  } else {
    for (size_t i = 0; i != s_compressed.size(); ++i)
      cksum += s_compressed[i];
    snprintf(hexbuf, sizeof hexbuf, "%02x", cksum & 0xff);
    sendpacket += hexbuf;
  }

  rnb_err_t err = m_comm.Write(sendpacket.c_str(), sendpacket.size());
  if (err != rnb_success)
    return err;

  if (m_noack_mode)
    return rnb_success;

  std::string reply;
  RNBRemote::Packet packet;
  err = GetPacket(reply, packet, true);

  if (err != rnb_success) {
    DNBLogThreadedIf(LOG_RNB_REMOTE,
                     "%8d RNBRemote::%s (%s) got error trying to get reply...",
                     (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                     __FUNCTION__, sendpacket.c_str());
    return err;
  }

  DNBLogThreadedIf(LOG_RNB_MAX, "%8d RNBRemote::%s (%s) got reply: '%s'",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                   __FUNCTION__, sendpacket.c_str(), reply.c_str());

  if (packet.type == ack)
    return rnb_success;

  // Should we try to resend the packet at this layer?
  //  if (packet.command == nack)
  return rnb_err;
}

rnb_err_t RNBRemote::SendErrorPacket(std::string errcode,
                                     const std::string &errmsg) {
  if (m_enable_error_strings && !errmsg.empty()) {
    errcode += ";";
    errcode += cstring_to_asciihex_string(errmsg.c_str());
  }
  return SendPacket(errcode);
}

/* Get a packet via gdb remote protocol.
 Strip off the prefix/suffix, verify the checksum to make sure
 a valid packet was received, send an ACK if they match.  */

rnb_err_t RNBRemote::GetPacketPayload(std::string &return_packet) {
  // DNBLogThreadedIf (LOG_RNB_MAX, "%8u RNBRemote::%s called",
  // (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__);

  {
    PThreadMutex::Locker locker(m_mutex);
    if (m_rx_packets.empty()) {
      // Only reset the remote command available event if we have no more
      // packets
      m_ctx.Events().ResetEvents(RNBContext::event_read_packet_available);
      // DNBLogThreadedIf (LOG_RNB_MAX, "%8u RNBRemote::%s error: no packets
      // available...", (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
      // __FUNCTION__);
      return rnb_err;
    }

    // DNBLogThreadedIf (LOG_RNB_MAX, "%8u RNBRemote::%s has %u queued packets",
    // (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__,
    // m_rx_packets.size());
    return_packet.swap(m_rx_packets.front());
    m_rx_packets.pop_front();

    if (m_rx_packets.empty()) {
      // Reset the remote command available event if we have no more packets
      m_ctx.Events().ResetEvents(RNBContext::event_read_packet_available);
    }
  }

  // DNBLogThreadedIf (LOG_RNB_MEDIUM, "%8u RNBRemote::%s: '%s'",
  // (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__,
  // return_packet.c_str());

  switch (return_packet[0]) {
  case '+':
  case '-':
  case '\x03':
    break;

  case '$': {
    long packet_checksum = 0;
    if (!m_noack_mode) {
      for (size_t i = return_packet.size() - 2; i < return_packet.size(); ++i) {
        char checksum_char = tolower(return_packet[i]);
        if (!isxdigit(checksum_char)) {
          m_comm.Write("-", 1);
          DNBLogThreadedIf(LOG_RNB_REMOTE, "%8u RNBRemote::%s error: packet "
                                           "with invalid checksum characters: "
                                           "%s",
                           (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                           __FUNCTION__, return_packet.c_str());
          return rnb_err;
        }
      }
      packet_checksum =
          strtol(&return_packet[return_packet.size() - 2], NULL, 16);
    }

    return_packet.erase(0, 1);                     // Strip the leading '$'
    return_packet.erase(return_packet.size() - 3); // Strip the #XX checksum

    if (!m_noack_mode) {
      // Compute the checksum
      int computed_checksum = 0;
      for (std::string::iterator it = return_packet.begin();
           it != return_packet.end(); ++it) {
        computed_checksum += *it;
      }

      if (packet_checksum == (computed_checksum & 0xff)) {
        // DNBLogThreadedIf (LOG_RNB_MEDIUM, "%8u RNBRemote::%s sending ACK for
        // '%s'", (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
        // __FUNCTION__, return_packet.c_str());
        m_comm.Write("+", 1);
      } else {
        DNBLogThreadedIf(
            LOG_RNB_MEDIUM, "%8u RNBRemote::%s sending ACK for '%s' (error: "
                            "packet checksum mismatch  (0x%2.2lx != 0x%2.2x))",
            (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__,
            return_packet.c_str(), packet_checksum, computed_checksum);
        m_comm.Write("-", 1);
        return rnb_err;
      }
    }
  } break;

  default:
    DNBLogThreadedIf(LOG_RNB_REMOTE,
                     "%8u RNBRemote::%s tossing unexpected packet???? %s",
                     (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                     __FUNCTION__, return_packet.c_str());
    if (!m_noack_mode)
      m_comm.Write("-", 1);
    return rnb_err;
  }

  return rnb_success;
}

rnb_err_t RNBRemote::HandlePacket_UNIMPLEMENTED(const char *p) {
  DNBLogThreadedIf(LOG_RNB_MAX, "%8u RNBRemote::%s(\"%s\")",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                   __FUNCTION__, p ? p : "NULL");
  return SendPacket("");
}

rnb_err_t RNBRemote::HandlePacket_ILLFORMED(const char *file, int line,
                                            const char *p,
                                            const char *description) {
  DNBLogThreadedIf(LOG_RNB_PACKETS, "%8u %s:%i ILLFORMED: '%s' (%s)",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), file,
                   line, __FUNCTION__, p);
  return SendErrorPacket("E03");
}

rnb_err_t RNBRemote::GetPacket(std::string &packet_payload,
                               RNBRemote::Packet &packet_info, bool wait) {
  std::string payload;
  rnb_err_t err = GetPacketPayload(payload);
  if (err != rnb_success) {
    PThreadEvent &events = m_ctx.Events();
    nub_event_t set_events = events.GetEventBits();
    // TODO: add timeout version of GetPacket?? We would then need to pass
    // that timeout value along to DNBProcessTimedWaitForEvent.
    if (!wait || ((set_events & RNBContext::event_read_thread_running) == 0))
      return err;

    const nub_event_t events_to_wait_for =
        RNBContext::event_read_packet_available |
        RNBContext::event_read_thread_exiting;

    while ((set_events = events.WaitForSetEvents(events_to_wait_for)) != 0) {
      if (set_events & RNBContext::event_read_packet_available) {
        // Try the queue again now that we got an event
        err = GetPacketPayload(payload);
        if (err == rnb_success)
          break;
      }

      if (set_events & RNBContext::event_read_thread_exiting)
        err = rnb_not_connected;

      if (err == rnb_not_connected)
        return err;
    }
    while (err == rnb_err)
      ;

    if (set_events == 0)
      err = rnb_not_connected;
  }

  if (err == rnb_success) {
    Packet::iterator it;
    for (it = m_packets.begin(); it != m_packets.end(); ++it) {
      if (payload.compare(0, it->abbrev.size(), it->abbrev) == 0)
        break;
    }

    // A packet we don't have an entry for. This can happen when we
    // get a packet that we don't know about or support. We just reply
    // accordingly and go on.
    if (it == m_packets.end()) {
      DNBLogThreadedIf(LOG_RNB_PACKETS, "unimplemented packet: '%s'",
                       payload.c_str());
      HandlePacket_UNIMPLEMENTED(payload.c_str());
      return rnb_err;
    } else {
      packet_info = *it;
      packet_payload = payload;
    }
  }
  return err;
}

rnb_err_t RNBRemote::HandleAsyncPacket(PacketEnum *type) {
  DNBLogThreadedIf(LOG_RNB_REMOTE, "%8u RNBRemote::%s",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                   __FUNCTION__);
  static DNBTimer g_packetTimer(true);
  rnb_err_t err = rnb_err;
  std::string packet_data;
  RNBRemote::Packet packet_info;
  err = GetPacket(packet_data, packet_info, false);

  if (err == rnb_success) {
    if (!packet_data.empty() && isprint(packet_data[0]))
      DNBLogThreadedIf(LOG_RNB_REMOTE | LOG_RNB_PACKETS,
                       "HandleAsyncPacket (\"%s\");", packet_data.c_str());
    else
      DNBLogThreadedIf(LOG_RNB_REMOTE | LOG_RNB_PACKETS,
                       "HandleAsyncPacket (%s);",
                       packet_info.printable_name.c_str());

    HandlePacketCallback packet_callback = packet_info.async;
    if (packet_callback != NULL) {
      if (type != NULL)
        *type = packet_info.type;
      return (this->*packet_callback)(packet_data.c_str());
    }
  }

  return err;
}

rnb_err_t RNBRemote::HandleReceivedPacket(PacketEnum *type) {
  static DNBTimer g_packetTimer(true);

  //  DNBLogThreadedIf (LOG_RNB_REMOTE, "%8u RNBRemote::%s",
  //  (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__);
  rnb_err_t err = rnb_err;
  std::string packet_data;
  RNBRemote::Packet packet_info;
  err = GetPacket(packet_data, packet_info, false);

  if (err == rnb_success) {
    DNBLogThreadedIf(LOG_RNB_REMOTE, "HandleReceivedPacket (\"%s\");",
                     packet_data.c_str());
    HandlePacketCallback packet_callback = packet_info.normal;
    if (packet_callback != NULL) {
      if (type != NULL)
        *type = packet_info.type;
      return (this->*packet_callback)(packet_data.c_str());
    } else {
      // Do not fall through to end of this function, if we have valid
      // packet_info and it has a NULL callback, then we need to respect
      // that it may not want any response or anything to be done.
      return err;
    }
  }
  return rnb_err;
}

void RNBRemote::CommDataReceived(const std::string &new_data) {
  //  DNBLogThreadedIf (LOG_RNB_REMOTE, "%8d RNBRemote::%s called",
  //  (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__);
  {
    // Put the packet data into the buffer in a thread safe fashion
    PThreadMutex::Locker locker(m_mutex);

    std::string data;
    // See if we have any left over data from a previous call to this
    // function?
    if (!m_rx_partial_data.empty()) {
      // We do, so lets start with that data
      data.swap(m_rx_partial_data);
    }
    // Append the new incoming data
    data += new_data;

    // Parse up the packets into gdb remote packets
    size_t idx = 0;
    const size_t data_size = data.size();

    while (idx < data_size) {
      // end_idx must be one past the last valid packet byte. Start
      // it off with an invalid value that is the same as the current
      // index.
      size_t end_idx = idx;

      switch (data[idx]) {
      case '+':            // Look for ack
      case '-':            // Look for cancel
      case '\x03':         // ^C to halt target
        end_idx = idx + 1; // The command is one byte long...
        break;

      case '$':
        // Look for a standard gdb packet?
        end_idx = data.find('#', idx + 1);
        if (end_idx == std::string::npos || end_idx + 3 > data_size) {
          end_idx = std::string::npos;
        } else {
          // Add two for the checksum bytes and 1 to point to the
          // byte just past the end of this packet
          end_idx += 3;
        }
        break;

      default:
        break;
      }

      if (end_idx == std::string::npos) {
        // Not all data may be here for the packet yet, save it for
        // next time through this function.
        m_rx_partial_data += data.substr(idx);
        // DNBLogThreadedIf (LOG_RNB_MAX, "%8d RNBRemote::%s saving data for
        // later[%u, npos):
        // '%s'",(uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
        // __FUNCTION__, idx, m_rx_partial_data.c_str());
        idx = end_idx;
      } else if (idx < end_idx) {
        m_packets_recvd++;
        // Hack to get rid of initial '+' ACK???
        if (m_packets_recvd == 1 && (end_idx == idx + 1) && data[idx] == '+') {
          // DNBLogThreadedIf (LOG_RNB_REMOTE, "%8d RNBRemote::%s throwing first
          // ACK away....[%u, npos):
          // '+'",(uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
          // __FUNCTION__, idx);
        } else {
          // We have a valid packet...
          m_rx_packets.push_back(data.substr(idx, end_idx - idx));
          DNBLogThreadedIf(LOG_RNB_PACKETS, "getpkt: %s",
                           m_rx_packets.back().c_str());
        }
        idx = end_idx;
      } else {
        DNBLogThreadedIf(LOG_RNB_MAX,
                         "%8d RNBRemote::%s tossing junk byte at %c",
                         (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                         __FUNCTION__, data[idx]);
        idx = idx + 1;
      }
    }
  }

  if (!m_rx_packets.empty()) {
    // Let the main thread know we have received a packet

    // DNBLogThreadedIf (LOG_RNB_EVENTS, "%8d RNBRemote::%s   called
    // events.SetEvent(RNBContext::event_read_packet_available)",
    // (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__);
    PThreadEvent &events = m_ctx.Events();
    events.SetEvents(RNBContext::event_read_packet_available);
  }
}

rnb_err_t RNBRemote::GetCommData() {
  //  DNBLogThreadedIf (LOG_RNB_REMOTE, "%8d RNBRemote::%s called",
  //  (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__);
  std::string comm_data;
  rnb_err_t err = m_comm.Read(comm_data);
  if (err == rnb_success) {
    if (!comm_data.empty())
      CommDataReceived(comm_data);
  }
  return err;
}

void RNBRemote::StartReadRemoteDataThread() {
  DNBLogThreadedIf(LOG_RNB_REMOTE, "%8u RNBRemote::%s called",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                   __FUNCTION__);
  PThreadEvent &events = m_ctx.Events();
  if ((events.GetEventBits() & RNBContext::event_read_thread_running) == 0) {
    events.ResetEvents(RNBContext::event_read_thread_exiting);
    int err = ::pthread_create(&m_rx_pthread, NULL,
                               ThreadFunctionReadRemoteData, this);
    if (err == 0) {
      // Our thread was successfully kicked off, wait for it to
      // set the started event so we can safely continue
      events.WaitForSetEvents(RNBContext::event_read_thread_running);
    } else {
      events.ResetEvents(RNBContext::event_read_thread_running);
      events.SetEvents(RNBContext::event_read_thread_exiting);
    }
  }
}

void RNBRemote::StopReadRemoteDataThread() {
  DNBLogThreadedIf(LOG_RNB_REMOTE, "%8u RNBRemote::%s called",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                   __FUNCTION__);
  PThreadEvent &events = m_ctx.Events();
  if ((events.GetEventBits() & RNBContext::event_read_thread_running) ==
      RNBContext::event_read_thread_running) {
    DNBLog("debugserver about to shut down packet communications to lldb.");
    m_comm.Disconnect(true);
    struct timespec timeout_abstime;
    DNBTimer::OffsetTimeOfDay(&timeout_abstime, 2, 0);

    // Wait for 2 seconds for the remote data thread to exit
    if (events.WaitForSetEvents(RNBContext::event_read_thread_exiting,
                                &timeout_abstime) == 0) {
      // Kill the remote data thread???
    }
  }
}

void *RNBRemote::ThreadFunctionReadRemoteData(void *arg) {
  // Keep a shared pointer reference so this doesn't go away on us before the
  // thread is killed.
  DNBLogThreadedIf(LOG_RNB_REMOTE, "RNBRemote::%s (%p): thread starting...",
                   __FUNCTION__, arg);
  RNBRemoteSP remoteSP(g_remoteSP);
  if (remoteSP.get() != NULL) {

#if defined(__APPLE__)
    pthread_setname_np("read gdb-remote packets thread");
#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
    struct sched_param thread_param;
    int thread_sched_policy;
    if (pthread_getschedparam(pthread_self(), &thread_sched_policy,
                              &thread_param) == 0) {
      thread_param.sched_priority = 47;
      pthread_setschedparam(pthread_self(), thread_sched_policy, &thread_param);
    }
#endif
#endif

    RNBRemote *remote = remoteSP.get();
    PThreadEvent &events = remote->Context().Events();
    events.SetEvents(RNBContext::event_read_thread_running);
    // START: main receive remote command thread loop
    bool done = false;
    while (!done) {
      rnb_err_t err = remote->GetCommData();

      switch (err) {
      case rnb_success:
        break;

      case rnb_err:
        DNBLogThreadedIf(LOG_RNB_REMOTE,
                         "RNBSocket::GetCommData returned error %u", err);
        done = true;
        break;

      case rnb_not_connected:
        DNBLogThreadedIf(LOG_RNB_REMOTE,
                         "RNBSocket::GetCommData returned not connected...");
        done = true;
        break;
      }
    }
    // START: main receive remote command thread loop
    events.ResetEvents(RNBContext::event_read_thread_running);
    events.SetEvents(RNBContext::event_read_thread_exiting);
  }
  DNBLogThreadedIf(LOG_RNB_REMOTE, "RNBRemote::%s (%p): thread exiting...",
                   __FUNCTION__, arg);
  return NULL;
}

// If we fail to get back a valid CPU type for the remote process,
// make a best guess for the CPU type based on the currently running
// debugserver binary -- the debugger may not handle the case of an
// un-specified process CPU type correctly.

static cpu_type_t best_guess_cpu_type() {
#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  if (sizeof(char *) == 8) {
    return CPU_TYPE_ARM64;
  } else {
#if defined (__ARM64_ARCH_8_32__)
    return CPU_TYPE_ARM64_32;
#endif
    return CPU_TYPE_ARM;
  }
#elif defined(__i386__) || defined(__x86_64__)
  if (sizeof(char *) == 8) {
    return CPU_TYPE_X86_64;
  } else {
    return CPU_TYPE_I386;
  }
#endif
  return 0;
}

/* Read the bytes in STR which are GDB Remote Protocol binary encoded bytes
 (8-bit bytes).
 This encoding uses 0x7d ('}') as an escape character for
 0x7d ('}'), 0x23 ('#'), 0x24 ('$'), 0x2a ('*').
 LEN is the number of bytes to be processed.  If a character is escaped,
 it is 2 characters for LEN.  A LEN of -1 means decode-until-nul-byte
 (end of string).  */

std::vector<uint8_t> decode_binary_data(const char *str, size_t len) {
  std::vector<uint8_t> bytes;
  if (len == 0) {
    return bytes;
  }
  if (len == (size_t)-1)
    len = strlen(str);

  while (len--) {
    unsigned char c = *str++;
    if (c == 0x7d && len > 0) {
      len--;
      c = *str++ ^ 0x20;
    }
    bytes.push_back(c);
  }
  return bytes;
}

// Quote any meta characters in a std::string as per the binary
// packet convention in the gdb-remote protocol.

static std::string binary_encode_string(const std::string &s) {
  std::string output;
  const size_t s_size = s.size();
  const char *s_chars = s.c_str();

  for (size_t i = 0; i < s_size; i++) {
    unsigned char ch = *(s_chars + i);
    if (ch == '#' || ch == '$' || ch == '}' || ch == '*') {
      output.push_back('}'); // 0x7d
      output.push_back(ch ^ 0x20);
    } else {
      output.push_back(ch);
    }
  }
  return output;
}

// If the value side of a key-value pair in JSON is a string,
// and that string has a " character in it, the " character must
// be escaped.

std::string json_string_quote_metachars(const std::string &s) {
  if (s.find('"') == std::string::npos)
    return s;

  std::string output;
  const size_t s_size = s.size();
  const char *s_chars = s.c_str();
  for (size_t i = 0; i < s_size; i++) {
    unsigned char ch = *(s_chars + i);
    if (ch == '"') {
      output.push_back('\\');
    }
    output.push_back(ch);
  }
  return output;
}

typedef struct register_map_entry {
  uint32_t debugserver_regnum; // debugserver register number
  uint32_t offset; // Offset in bytes into the register context data with no
                   // padding between register values
  DNBRegisterInfo nub_info; // debugnub register info
  std::vector<uint32_t> value_regnums;
  std::vector<uint32_t> invalidate_regnums;
} register_map_entry_t;

// If the notion of registers differs from what is handed out by the
// architecture, then flavors can be defined here.

static std::vector<register_map_entry_t> g_dynamic_register_map;
static register_map_entry_t *g_reg_entries = NULL;
static size_t g_num_reg_entries = 0;

void RNBRemote::Initialize() { DNBInitialize(); }

bool RNBRemote::InitializeRegisters(bool force) {
  pid_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return false;

  DNBLogThreadedIf(
      LOG_RNB_PROC,
      "RNBRemote::%s() getting native registers from DNB interface",
      __FUNCTION__);
  // Discover the registers by querying the DNB interface and letting it
  // state the registers that it would like to export. This allows the
  // registers to be discovered using multiple qRegisterInfo calls to get
  // all register information after the architecture for the process is
  // determined.
  if (force) {
    g_dynamic_register_map.clear();
    g_reg_entries = NULL;
    g_num_reg_entries = 0;
  }

  if (g_dynamic_register_map.empty()) {
    nub_size_t num_reg_sets = 0;
    const DNBRegisterSetInfo *reg_sets = DNBGetRegisterSetInfo(&num_reg_sets);

    assert(num_reg_sets > 0 && reg_sets != NULL);

    uint32_t regnum = 0;
    uint32_t reg_data_offset = 0;
    typedef std::map<std::string, uint32_t> NameToRegNum;
    NameToRegNum name_to_regnum;
    for (nub_size_t set = 0; set < num_reg_sets; ++set) {
      if (reg_sets[set].registers == NULL)
        continue;

      for (uint32_t reg = 0; reg < reg_sets[set].num_registers; ++reg) {
        register_map_entry_t reg_entry = {
            regnum++, // register number starts at zero and goes up with no gaps
            reg_data_offset, // Offset into register context data, no gaps
                             // between registers
            reg_sets[set].registers[reg], // DNBRegisterInfo
            {},
            {},
        };

        name_to_regnum[reg_entry.nub_info.name] = reg_entry.debugserver_regnum;

        if (reg_entry.nub_info.value_regs == NULL) {
          reg_data_offset += reg_entry.nub_info.size;
        }

        g_dynamic_register_map.push_back(reg_entry);
      }
    }

    // Now we must find any registers whose values are in other registers and
    // fix up
    // the offsets since we removed all gaps...
    for (auto &reg_entry : g_dynamic_register_map) {
      if (reg_entry.nub_info.value_regs) {
        uint32_t new_offset = UINT32_MAX;
        for (size_t i = 0; reg_entry.nub_info.value_regs[i] != NULL; ++i) {
          const char *name = reg_entry.nub_info.value_regs[i];
          auto pos = name_to_regnum.find(name);
          if (pos != name_to_regnum.end()) {
            regnum = pos->second;
            reg_entry.value_regnums.push_back(regnum);
            if (regnum < g_dynamic_register_map.size()) {
              // The offset for value_regs registers is the offset within the
              // register with the lowest offset
              const uint32_t reg_offset =
                  g_dynamic_register_map[regnum].offset +
                  reg_entry.nub_info.offset;
              if (new_offset > reg_offset)
                new_offset = reg_offset;
            }
          }
        }

        if (new_offset != UINT32_MAX) {
          reg_entry.offset = new_offset;
        } else {
          DNBLogThreaded("no offset was calculated entry for register %s",
                         reg_entry.nub_info.name);
          reg_entry.offset = UINT32_MAX;
        }
      }

      if (reg_entry.nub_info.update_regs) {
        for (size_t i = 0; reg_entry.nub_info.update_regs[i] != NULL; ++i) {
          const char *name = reg_entry.nub_info.update_regs[i];
          auto pos = name_to_regnum.find(name);
          if (pos != name_to_regnum.end()) {
            regnum = pos->second;
            reg_entry.invalidate_regnums.push_back(regnum);
          }
        }
      }
    }

    //        for (auto &reg_entry: g_dynamic_register_map)
    //        {
    //            DNBLogThreaded("%4i: size = %3u, pseudo = %i, name = %s",
    //                           reg_entry.offset,
    //                           reg_entry.nub_info.size,
    //                           reg_entry.nub_info.value_regs != NULL,
    //                           reg_entry.nub_info.name);
    //        }

    g_reg_entries = g_dynamic_register_map.data();
    g_num_reg_entries = g_dynamic_register_map.size();
  }
  return true;
}

/* The inferior has stopped executing; send a packet
 to gdb to let it know.  */

void RNBRemote::NotifyThatProcessStopped(void) {
  RNBRemote::HandlePacket_last_signal(NULL);
  return;
}

/* 'A arglen,argnum,arg,...'
 Update the inferior context CTX with the program name and arg
 list.
 The documentation for this packet is underwhelming but my best reading
 of this is that it is a series of (len, position #, arg)'s, one for
 each argument with "arg" hex encoded (two 0-9a-f chars?).
 Why we need BOTH a "len" and a hex encoded "arg" is beyond me - either
 is sufficient to get around the "," position separator escape issue.

 e.g. our best guess for a valid 'A' packet for "gdb -q a.out" is

 6,0,676462,4,1,2d71,10,2,612e6f7574

 Note that "argnum" and "arglen" are numbers in base 10.  Again, that's
 not documented either way but I'm assuming it's so.  */

rnb_err_t RNBRemote::HandlePacket_A(const char *p) {
  if (p == NULL || *p == '\0') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Null packet for 'A' pkt");
  }
  p++;
  if (*p == '\0' || !isdigit(*p)) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "arglen not specified on 'A' pkt");
  }

  /* I promise I don't modify it anywhere in this function.  strtoul()'s
   2nd arg has to be non-const which makes it problematic to step
   through the string easily.  */
  char *buf = const_cast<char *>(p);

  RNBContext &ctx = Context();

  while (*buf != '\0') {
    unsigned long arglen, argnum;
    std::string arg;
    char *c;

    errno = 0;
    arglen = strtoul(buf, &c, 10);
    if (errno != 0 && arglen == 0) {
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "arglen not a number on 'A' pkt");
    }
    if (*c != ',') {
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "arglen not followed by comma on 'A' pkt");
    }
    buf = c + 1;

    errno = 0;
    argnum = strtoul(buf, &c, 10);
    if (errno != 0 && argnum == 0) {
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "argnum not a number on 'A' pkt");
    }
    if (*c != ',') {
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "arglen not followed by comma on 'A' pkt");
    }
    buf = c + 1;

    c = buf;
    buf = buf + arglen;
    while (c < buf && *c != '\0' && c + 1 < buf && *(c + 1) != '\0') {
      char smallbuf[3];
      smallbuf[0] = *c;
      smallbuf[1] = *(c + 1);
      smallbuf[2] = '\0';

      errno = 0;
      int ch = static_cast<int>(strtoul(smallbuf, NULL, 16));
      if (errno != 0 && ch == 0) {
        return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                      "non-hex char in arg on 'A' pkt");
      }

      arg.push_back(ch);
      c += 2;
    }

    ctx.PushArgument(arg.c_str());
    if (*buf == ',')
      buf++;
  }
  SendPacket("OK");

  return rnb_success;
}

/* 'H c t'
 Set the thread for subsequent actions; 'c' for step/continue ops,
 'g' for other ops.  -1 means all threads, 0 means any thread.  */

rnb_err_t RNBRemote::HandlePacket_H(const char *p) {
  p++; // skip 'H'
  if (*p != 'c' && *p != 'g') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Missing 'c' or 'g' type in H packet");
  }

  if (!m_ctx.HasValidProcessID()) {
    // We allow gdb to connect to a server that hasn't started running
    // the target yet.  gdb still wants to ask questions about it and
    // freaks out if it gets an error.  So just return OK here.
  }

  errno = 0;
  nub_thread_t tid = strtoul(p + 1, NULL, 16);
  if (errno != 0 && tid == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid thread number in H packet");
  }
  if (*p == 'c')
    SetContinueThread(tid);
  if (*p == 'g')
    SetCurrentThread(tid);

  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_qLaunchSuccess(const char *p) {
  if (m_ctx.HasValidProcessID() || m_ctx.LaunchStatus().Status() == 0)
    return SendPacket("OK");
  std::string status_str;
  return SendErrorPacket("E89", m_ctx.LaunchStatusAsString(status_str));
}

rnb_err_t RNBRemote::HandlePacket_qShlibInfoAddr(const char *p) {
  if (m_ctx.HasValidProcessID()) {
    nub_addr_t shlib_info_addr =
        DNBProcessGetSharedLibraryInfoAddress(m_ctx.ProcessID());
    if (shlib_info_addr != INVALID_NUB_ADDRESS) {
      std::ostringstream ostrm;
      ostrm << RAW_HEXBASE << shlib_info_addr;
      return SendPacket(ostrm.str());
    }
  }
  return SendErrorPacket("E44");
}

rnb_err_t RNBRemote::HandlePacket_qStepPacketSupported(const char *p) {
  // Normally the "s" packet is mandatory, yet in gdb when using ARM, they
  // get around the need for this packet by implementing software single
  // stepping from gdb. Current versions of debugserver do support the "s"
  // packet, yet some older versions do not. We need a way to tell if this
  // packet is supported so we can disable software single stepping in gdb
  // for remote targets (so the "s" packet will get used).
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_qSyncThreadStateSupported(const char *p) {
  // We support attachOrWait meaning attach if the process exists, otherwise
  // wait to attach.
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_qVAttachOrWaitSupported(const char *p) {
  // We support attachOrWait meaning attach if the process exists, otherwise
  // wait to attach.
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_qThreadStopInfo(const char *p) {
  p += strlen("qThreadStopInfo");
  nub_thread_t tid = strtoul(p, 0, 16);
  return SendStopReplyPacketForThread(tid);
}

rnb_err_t RNBRemote::HandlePacket_qThreadInfo(const char *p) {
  // We allow gdb to connect to a server that hasn't started running
  // the target yet.  gdb still wants to ask questions about it and
  // freaks out if it gets an error.  So just return OK here.
  nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendPacket("OK");

  // Only "qfThreadInfo" and "qsThreadInfo" get into this function so
  // we only need to check the second byte to tell which is which
  if (p[1] == 'f') {
    nub_size_t numthreads = DNBProcessGetNumThreads(pid);
    std::ostringstream ostrm;
    ostrm << "m";
    bool first = true;
    for (nub_size_t i = 0; i < numthreads; ++i) {
      if (first)
        first = false;
      else
        ostrm << ",";
      nub_thread_t th = DNBProcessGetThreadAtIndex(pid, i);
      ostrm << std::hex << th;
    }
    return SendPacket(ostrm.str());
  } else {
    return SendPacket("l");
  }
}

rnb_err_t RNBRemote::HandlePacket_qThreadExtraInfo(const char *p) {
  // We allow gdb to connect to a server that hasn't started running
  // the target yet.  gdb still wants to ask questions about it and
  // freaks out if it gets an error.  So just return OK here.
  nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendPacket("OK");

  /* This is supposed to return a string like 'Runnable' or
   'Blocked on Mutex'.
   The returned string is formatted like the "A" packet - a
   sequence of letters encoded in as 2-hex-chars-per-letter.  */
  p += strlen("qThreadExtraInfo");
  if (*p++ != ',')
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Illformed qThreadExtraInfo packet");
  errno = 0;
  nub_thread_t tid = strtoul(p, NULL, 16);
  if (errno != 0 && tid == 0) {
    return HandlePacket_ILLFORMED(
        __FILE__, __LINE__, p,
        "Invalid thread number in qThreadExtraInfo packet");
  }

  const char *threadInfo = DNBThreadGetInfo(pid, tid);
  if (threadInfo != NULL && threadInfo[0]) {
    return SendHexEncodedBytePacket(NULL, threadInfo, strlen(threadInfo), NULL);
  } else {
    // "OK" == 4f6b
    // Return "OK" as a ASCII hex byte stream if things go wrong
    return SendPacket("4f6b");
  }

  return SendPacket("");
}

const char *k_space_delimiters = " \t";
static void skip_spaces(std::string &line) {
  if (!line.empty()) {
    size_t space_pos = line.find_first_not_of(k_space_delimiters);
    if (space_pos > 0)
      line.erase(0, space_pos);
  }
}

static std::string get_identifier(std::string &line) {
  std::string word;
  skip_spaces(line);
  const size_t line_size = line.size();
  size_t end_pos;
  for (end_pos = 0; end_pos < line_size; ++end_pos) {
    if (end_pos == 0) {
      if (isalpha(line[end_pos]) || line[end_pos] == '_')
        continue;
    } else if (isalnum(line[end_pos]) || line[end_pos] == '_')
      continue;
    break;
  }
  word.assign(line, 0, end_pos);
  line.erase(0, end_pos);
  return word;
}

static std::string get_operator(std::string &line) {
  std::string op;
  skip_spaces(line);
  if (!line.empty()) {
    if (line[0] == '=') {
      op = '=';
      line.erase(0, 1);
    }
  }
  return op;
}

static std::string get_value(std::string &line) {
  std::string value;
  skip_spaces(line);
  if (!line.empty()) {
    value.swap(line);
  }
  return value;
}

extern void FileLogCallback(void *baton, uint32_t flags, const char *format,
                            va_list args);
extern void ASLLogCallback(void *baton, uint32_t flags, const char *format,
                           va_list args);

rnb_err_t RNBRemote::HandlePacket_qRcmd(const char *p) {
  const char *c = p + strlen("qRcmd,");
  std::string line;
  while (c[0] && c[1]) {
    char smallbuf[3] = {c[0], c[1], '\0'};
    errno = 0;
    int ch = static_cast<int>(strtoul(smallbuf, NULL, 16));
    if (errno != 0 && ch == 0)
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "non-hex char in payload of qRcmd packet");
    line.push_back(ch);
    c += 2;
  }
  if (*c == '\0') {
    std::string command = get_identifier(line);
    if (command == "set") {
      std::string variable = get_identifier(line);
      std::string op = get_operator(line);
      std::string value = get_value(line);
      if (variable == "logfile") {
        FILE *log_file = fopen(value.c_str(), "w");
        if (log_file) {
          DNBLogSetLogCallback(FileLogCallback, log_file);
          return SendPacket("OK");
        }
        return SendErrorPacket("E71");
      } else if (variable == "logmask") {
        char *end;
        errno = 0;
        uint32_t logmask =
            static_cast<uint32_t>(strtoul(value.c_str(), &end, 0));
        if (errno == 0 && end && *end == '\0') {
          DNBLogSetLogMask(logmask);
          if (!DNBLogGetLogCallback())
            DNBLogSetLogCallback(ASLLogCallback, NULL);
          return SendPacket("OK");
        }
        errno = 0;
        logmask = static_cast<uint32_t>(strtoul(value.c_str(), &end, 16));
        if (errno == 0 && end && *end == '\0') {
          DNBLogSetLogMask(logmask);
          return SendPacket("OK");
        }
        return SendErrorPacket("E72");
      }
      return SendErrorPacket("E70");
    }
    return SendErrorPacket("E69");
  }
  return SendErrorPacket("E73");
}

rnb_err_t RNBRemote::HandlePacket_qC(const char *p) {
  nub_thread_t tid;
  std::ostringstream rep;
  // If we haven't run the process yet, we tell the debugger the
  // pid is 0.  That way it can know to tell use to run later on.
  if (!m_ctx.HasValidProcessID())
    tid = 0;
  else {
    // Grab the current thread.
    tid = DNBProcessGetCurrentThread(m_ctx.ProcessID());
    // Make sure we set the current thread so g and p packets return
    // the data the gdb will expect.
    SetCurrentThread(tid);
  }
  rep << "QC" << std::hex << tid;
  return SendPacket(rep.str());
}

rnb_err_t RNBRemote::HandlePacket_qEcho(const char *p) {
  // Just send the exact same packet back that we received to
  // synchronize the response packets after a previous packet
  // timed out. This allows the debugger to get back on track
  // with responses after a packet timeout.
  return SendPacket(p);
}

rnb_err_t RNBRemote::HandlePacket_qGetPid(const char *p) {
  nub_process_t pid;
  std::ostringstream rep;
  // If we haven't run the process yet, we tell the debugger the
  // pid is 0.  That way it can know to tell use to run later on.
  if (m_ctx.HasValidProcessID())
    pid = m_ctx.ProcessID();
  else
    pid = 0;
  rep << std::hex << pid;
  return SendPacket(rep.str());
}

rnb_err_t RNBRemote::HandlePacket_qRegisterInfo(const char *p) {
  if (g_num_reg_entries == 0)
    InitializeRegisters();

  p += strlen("qRegisterInfo");

  nub_size_t num_reg_sets = 0;
  const DNBRegisterSetInfo *reg_set_info = DNBGetRegisterSetInfo(&num_reg_sets);
  uint32_t reg_num = static_cast<uint32_t>(strtoul(p, 0, 16));

  if (reg_num < g_num_reg_entries) {
    const register_map_entry_t *reg_entry = &g_reg_entries[reg_num];
    std::ostringstream ostrm;
    if (reg_entry->nub_info.name)
      ostrm << "name:" << reg_entry->nub_info.name << ';';
    if (reg_entry->nub_info.alt)
      ostrm << "alt-name:" << reg_entry->nub_info.alt << ';';

    ostrm << "bitsize:" << std::dec << reg_entry->nub_info.size * 8 << ';';
    ostrm << "offset:" << std::dec << reg_entry->offset << ';';

    switch (reg_entry->nub_info.type) {
    case Uint:
      ostrm << "encoding:uint;";
      break;
    case Sint:
      ostrm << "encoding:sint;";
      break;
    case IEEE754:
      ostrm << "encoding:ieee754;";
      break;
    case Vector:
      ostrm << "encoding:vector;";
      break;
    }

    switch (reg_entry->nub_info.format) {
    case Binary:
      ostrm << "format:binary;";
      break;
    case Decimal:
      ostrm << "format:decimal;";
      break;
    case Hex:
      ostrm << "format:hex;";
      break;
    case Float:
      ostrm << "format:float;";
      break;
    case VectorOfSInt8:
      ostrm << "format:vector-sint8;";
      break;
    case VectorOfUInt8:
      ostrm << "format:vector-uint8;";
      break;
    case VectorOfSInt16:
      ostrm << "format:vector-sint16;";
      break;
    case VectorOfUInt16:
      ostrm << "format:vector-uint16;";
      break;
    case VectorOfSInt32:
      ostrm << "format:vector-sint32;";
      break;
    case VectorOfUInt32:
      ostrm << "format:vector-uint32;";
      break;
    case VectorOfFloat32:
      ostrm << "format:vector-float32;";
      break;
    case VectorOfUInt128:
      ostrm << "format:vector-uint128;";
      break;
    };

    if (reg_set_info && reg_entry->nub_info.set < num_reg_sets)
      ostrm << "set:" << reg_set_info[reg_entry->nub_info.set].name << ';';

    if (reg_entry->nub_info.reg_ehframe != INVALID_NUB_REGNUM)
      ostrm << "ehframe:" << std::dec << reg_entry->nub_info.reg_ehframe << ';';

    if (reg_entry->nub_info.reg_dwarf != INVALID_NUB_REGNUM)
      ostrm << "dwarf:" << std::dec << reg_entry->nub_info.reg_dwarf << ';';

    switch (reg_entry->nub_info.reg_generic) {
    case GENERIC_REGNUM_FP:
      ostrm << "generic:fp;";
      break;
    case GENERIC_REGNUM_PC:
      ostrm << "generic:pc;";
      break;
    case GENERIC_REGNUM_SP:
      ostrm << "generic:sp;";
      break;
    case GENERIC_REGNUM_RA:
      ostrm << "generic:ra;";
      break;
    case GENERIC_REGNUM_FLAGS:
      ostrm << "generic:flags;";
      break;
    case GENERIC_REGNUM_ARG1:
      ostrm << "generic:arg1;";
      break;
    case GENERIC_REGNUM_ARG2:
      ostrm << "generic:arg2;";
      break;
    case GENERIC_REGNUM_ARG3:
      ostrm << "generic:arg3;";
      break;
    case GENERIC_REGNUM_ARG4:
      ostrm << "generic:arg4;";
      break;
    case GENERIC_REGNUM_ARG5:
      ostrm << "generic:arg5;";
      break;
    case GENERIC_REGNUM_ARG6:
      ostrm << "generic:arg6;";
      break;
    case GENERIC_REGNUM_ARG7:
      ostrm << "generic:arg7;";
      break;
    case GENERIC_REGNUM_ARG8:
      ostrm << "generic:arg8;";
      break;
    default:
      break;
    }

    if (!reg_entry->value_regnums.empty()) {
      ostrm << "container-regs:";
      for (size_t i = 0, n = reg_entry->value_regnums.size(); i < n; ++i) {
        if (i > 0)
          ostrm << ',';
        ostrm << RAW_HEXBASE << reg_entry->value_regnums[i];
      }
      ostrm << ';';
    }

    if (!reg_entry->invalidate_regnums.empty()) {
      ostrm << "invalidate-regs:";
      for (size_t i = 0, n = reg_entry->invalidate_regnums.size(); i < n; ++i) {
        if (i > 0)
          ostrm << ',';
        ostrm << RAW_HEXBASE << reg_entry->invalidate_regnums[i];
      }
      ostrm << ';';
    }

    return SendPacket(ostrm.str());
  }
  return SendErrorPacket("E45");
}

/* This expects a packet formatted like

 QSetLogging:bitmask=LOG_ALL|LOG_RNB_REMOTE;

 with the "QSetLogging:" already removed from the start.  Maybe in the
 future this packet will include other keyvalue pairs like

 QSetLogging:bitmask=LOG_ALL;mode=asl;
 */

rnb_err_t set_logging(const char *p) {
  int bitmask = 0;
  while (p && *p != '\0') {
    if (strncmp(p, "bitmask=", sizeof("bitmask=") - 1) == 0) {
      p += sizeof("bitmask=") - 1;
      while (p && *p != '\0' && *p != ';') {
        if (*p == '|')
          p++;

        // to regenerate the LOG_ entries (not including the LOG_RNB entries)
        // $ for logname in `grep '^#define LOG_' DNBDefs.h | egrep -v
        // 'LOG_HI|LOG_LO' | awk '{print $2}'`
        // do
        //   echo "                else if (strncmp (p, \"$logname\", sizeof
        //   (\"$logname\") - 1) == 0)"
        //   echo "                {"
        //   echo "                    p += sizeof (\"$logname\") - 1;"
        //   echo "                    bitmask |= $logname;"
        //   echo "                }"
        // done
        if (strncmp(p, "LOG_VERBOSE", sizeof("LOG_VERBOSE") - 1) == 0) {
          p += sizeof("LOG_VERBOSE") - 1;
          bitmask |= LOG_VERBOSE;
        } else if (strncmp(p, "LOG_PROCESS", sizeof("LOG_PROCESS") - 1) == 0) {
          p += sizeof("LOG_PROCESS") - 1;
          bitmask |= LOG_PROCESS;
        } else if (strncmp(p, "LOG_THREAD", sizeof("LOG_THREAD") - 1) == 0) {
          p += sizeof("LOG_THREAD") - 1;
          bitmask |= LOG_THREAD;
        } else if (strncmp(p, "LOG_EXCEPTIONS", sizeof("LOG_EXCEPTIONS") - 1) ==
                   0) {
          p += sizeof("LOG_EXCEPTIONS") - 1;
          bitmask |= LOG_EXCEPTIONS;
        } else if (strncmp(p, "LOG_SHLIB", sizeof("LOG_SHLIB") - 1) == 0) {
          p += sizeof("LOG_SHLIB") - 1;
          bitmask |= LOG_SHLIB;
        } else if (strncmp(p, "LOG_MEMORY_DATA_SHORT",
                           sizeof("LOG_MEMORY_DATA_SHORT") - 1) == 0) {
          p += sizeof("LOG_MEMORY_DATA_SHORT") - 1;
          bitmask |= LOG_MEMORY_DATA_SHORT;
        } else if (strncmp(p, "LOG_MEMORY_DATA_LONG",
                           sizeof("LOG_MEMORY_DATA_LONG") - 1) == 0) {
          p += sizeof("LOG_MEMORY_DATA_LONG") - 1;
          bitmask |= LOG_MEMORY_DATA_LONG;
        } else if (strncmp(p, "LOG_MEMORY_PROTECTIONS",
                           sizeof("LOG_MEMORY_PROTECTIONS") - 1) == 0) {
          p += sizeof("LOG_MEMORY_PROTECTIONS") - 1;
          bitmask |= LOG_MEMORY_PROTECTIONS;
        } else if (strncmp(p, "LOG_MEMORY", sizeof("LOG_MEMORY") - 1) == 0) {
          p += sizeof("LOG_MEMORY") - 1;
          bitmask |= LOG_MEMORY;
        } else if (strncmp(p, "LOG_BREAKPOINTS",
                           sizeof("LOG_BREAKPOINTS") - 1) == 0) {
          p += sizeof("LOG_BREAKPOINTS") - 1;
          bitmask |= LOG_BREAKPOINTS;
        } else if (strncmp(p, "LOG_EVENTS", sizeof("LOG_EVENTS") - 1) == 0) {
          p += sizeof("LOG_EVENTS") - 1;
          bitmask |= LOG_EVENTS;
        } else if (strncmp(p, "LOG_WATCHPOINTS",
                           sizeof("LOG_WATCHPOINTS") - 1) == 0) {
          p += sizeof("LOG_WATCHPOINTS") - 1;
          bitmask |= LOG_WATCHPOINTS;
        } else if (strncmp(p, "LOG_STEP", sizeof("LOG_STEP") - 1) == 0) {
          p += sizeof("LOG_STEP") - 1;
          bitmask |= LOG_STEP;
        } else if (strncmp(p, "LOG_TASK", sizeof("LOG_TASK") - 1) == 0) {
          p += sizeof("LOG_TASK") - 1;
          bitmask |= LOG_TASK;
        } else if (strncmp(p, "LOG_ALL", sizeof("LOG_ALL") - 1) == 0) {
          p += sizeof("LOG_ALL") - 1;
          bitmask |= LOG_ALL;
        } else if (strncmp(p, "LOG_DEFAULT", sizeof("LOG_DEFAULT") - 1) == 0) {
          p += sizeof("LOG_DEFAULT") - 1;
          bitmask |= LOG_DEFAULT;
        }
        // end of auto-generated entries

        else if (strncmp(p, "LOG_NONE", sizeof("LOG_NONE") - 1) == 0) {
          p += sizeof("LOG_NONE") - 1;
          bitmask = 0;
        } else if (strncmp(p, "LOG_RNB_MINIMAL",
                           sizeof("LOG_RNB_MINIMAL") - 1) == 0) {
          p += sizeof("LOG_RNB_MINIMAL") - 1;
          bitmask |= LOG_RNB_MINIMAL;
        } else if (strncmp(p, "LOG_RNB_MEDIUM", sizeof("LOG_RNB_MEDIUM") - 1) ==
                   0) {
          p += sizeof("LOG_RNB_MEDIUM") - 1;
          bitmask |= LOG_RNB_MEDIUM;
        } else if (strncmp(p, "LOG_RNB_MAX", sizeof("LOG_RNB_MAX") - 1) == 0) {
          p += sizeof("LOG_RNB_MAX") - 1;
          bitmask |= LOG_RNB_MAX;
        } else if (strncmp(p, "LOG_RNB_COMM", sizeof("LOG_RNB_COMM") - 1) ==
                   0) {
          p += sizeof("LOG_RNB_COMM") - 1;
          bitmask |= LOG_RNB_COMM;
        } else if (strncmp(p, "LOG_RNB_REMOTE", sizeof("LOG_RNB_REMOTE") - 1) ==
                   0) {
          p += sizeof("LOG_RNB_REMOTE") - 1;
          bitmask |= LOG_RNB_REMOTE;
        } else if (strncmp(p, "LOG_RNB_EVENTS", sizeof("LOG_RNB_EVENTS") - 1) ==
                   0) {
          p += sizeof("LOG_RNB_EVENTS") - 1;
          bitmask |= LOG_RNB_EVENTS;
        } else if (strncmp(p, "LOG_RNB_PROC", sizeof("LOG_RNB_PROC") - 1) ==
                   0) {
          p += sizeof("LOG_RNB_PROC") - 1;
          bitmask |= LOG_RNB_PROC;
        } else if (strncmp(p, "LOG_RNB_PACKETS",
                           sizeof("LOG_RNB_PACKETS") - 1) == 0) {
          p += sizeof("LOG_RNB_PACKETS") - 1;
          bitmask |= LOG_RNB_PACKETS;
        } else if (strncmp(p, "LOG_RNB_ALL", sizeof("LOG_RNB_ALL") - 1) == 0) {
          p += sizeof("LOG_RNB_ALL") - 1;
          bitmask |= LOG_RNB_ALL;
        } else if (strncmp(p, "LOG_RNB_DEFAULT",
                           sizeof("LOG_RNB_DEFAULT") - 1) == 0) {
          p += sizeof("LOG_RNB_DEFAULT") - 1;
          bitmask |= LOG_RNB_DEFAULT;
        } else if (strncmp(p, "LOG_DARWIN_LOG", sizeof("LOG_DARWIN_LOG") - 1) ==
                   0) {
          p += sizeof("LOG_DARWIN_LOG") - 1;
          bitmask |= LOG_DARWIN_LOG;
        } else if (strncmp(p, "LOG_RNB_NONE", sizeof("LOG_RNB_NONE") - 1) ==
                   0) {
          p += sizeof("LOG_RNB_NONE") - 1;
          bitmask = 0;
        } else {
          /* Unrecognized logging bit; ignore it.  */
          const char *c = strchr(p, '|');
          if (c) {
            p = c;
          } else {
            c = strchr(p, ';');
            if (c) {
              p = c;
            } else {
              // Improperly terminated word; just go to end of str
              p = strchr(p, '\0');
            }
          }
        }
      }
      // Did we get a properly formatted logging bitmask?
      if (p && *p == ';') {
        // Enable DNB logging.
        // Use the existing log callback if one was already configured.
        if (!DNBLogGetLogCallback()) {
          // Use the os_log()-based logger if available; otherwise,
          // fallback to ASL.
          auto log_callback = OsLogger::GetLogFunction();
          if (log_callback)
            DNBLogSetLogCallback(log_callback, nullptr);
          else
            DNBLogSetLogCallback(ASLLogCallback, nullptr);
        }

        // Update logging to use the configured log channel bitmask.
        DNBLogSetLogMask(bitmask);
        p++;
      }
    }
// We're not going to support logging to a file for now.  All logging
// goes through ASL or the previously arranged log callback.
#if 0
        else if (strncmp (p, "mode=", sizeof ("mode=") - 1) == 0)
        {
            p += sizeof ("mode=") - 1;
            if (strncmp (p, "asl;", sizeof ("asl;") - 1) == 0)
            {
                DNBLogToASL ();
                p += sizeof ("asl;") - 1;
            }
            else if (strncmp (p, "file;", sizeof ("file;") - 1) == 0)
            {
                DNBLogToFile ();
                p += sizeof ("file;") - 1;
            }
            else
            {
                // Ignore unknown argument
                const char *c = strchr (p, ';');
                if (c)
                    p = c + 1;
                else
                    p = strchr (p, '\0');
            }
        }
        else if (strncmp (p, "filename=", sizeof ("filename=") - 1) == 0)
        {
            p += sizeof ("filename=") - 1;
            const char *c = strchr (p, ';');
            if (c == NULL)
            {
                c = strchr (p, '\0');
                continue;
            }
            char *fn = (char *) alloca (c - p + 1);
            strlcpy (fn, p, c - p);
            fn[c - p] = '\0';

            // A file name of "asl" is special and is another way to indicate
            // that logging should be done via ASL, not by file.
            if (strcmp (fn, "asl") == 0)
            {
                DNBLogToASL ();
            }
            else
            {
                FILE *f = fopen (fn, "w");
                if (f)
                {
                    DNBLogSetLogFile (f);
                    DNBEnableLogging (f, DNBLogGetLogMask ());
                    DNBLogToFile ();
                }
            }
            p = c + 1;
        }
#endif /* #if 0 to enforce ASL logging only.  */
    else {
      // Ignore unknown argument
      const char *c = strchr(p, ';');
      if (c)
        p = c + 1;
      else
        p = strchr(p, '\0');
    }
  }

  return rnb_success;
}

rnb_err_t RNBRemote::HandlePacket_QSetIgnoredExceptions(const char *p) {
  // We can't set the ignored exceptions if we have a running process:
  if (m_ctx.HasValidProcessID())
    return SendErrorPacket("E35");

  p += sizeof("QSetIgnoredExceptions:") - 1;
  bool success = true;
  while(1) {
    const char *bar  = strchr(p, '|');
    if (bar == nullptr) {
      success = m_ctx.AddIgnoredException(p);
      break;
    } else {
      std::string exc_str(p, bar - p);
      if (exc_str.empty()) {
        success = false;
        break;
      }

      success = m_ctx.AddIgnoredException(exc_str.c_str());
      if (!success)
        break;
      p = bar + 1;
    }
  }
  if (success)
    return SendPacket("OK");
  else
    return SendErrorPacket("E36");
}

rnb_err_t RNBRemote::HandlePacket_QThreadSuffixSupported(const char *p) {
  m_thread_suffix_supported = true;
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_QStartNoAckMode(const char *p) {
  // Send the OK packet first so the correct checksum is appended...
  rnb_err_t result = SendPacket("OK");
  m_noack_mode = true;
  return result;
}

rnb_err_t RNBRemote::HandlePacket_QSetLogging(const char *p) {
  p += sizeof("QSetLogging:") - 1;
  rnb_err_t result = set_logging(p);
  if (result == rnb_success)
    return SendPacket("OK");
  else
    return SendErrorPacket("E35");
}

rnb_err_t RNBRemote::HandlePacket_QSetDisableASLR(const char *p) {
  extern int g_disable_aslr;
  p += sizeof("QSetDisableASLR:") - 1;
  switch (*p) {
  case '0':
    g_disable_aslr = 0;
    break;
  case '1':
    g_disable_aslr = 1;
    break;
  default:
    return SendErrorPacket("E56");
  }
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_QSetSTDIO(const char *p) {
  // Only set stdin/out/err if we don't already have a process
  if (!m_ctx.HasValidProcessID()) {
    bool success = false;
    // Check the seventh character since the packet will be one of:
    // QSetSTDIN
    // QSetSTDOUT
    // QSetSTDERR
    StdStringExtractor packet(p);
    packet.SetFilePos(7);
    char ch = packet.GetChar();
    while (packet.GetChar() != ':')
      /* Do nothing. */;

    switch (ch) {
    case 'I': // STDIN
      packet.GetHexByteString(m_ctx.GetSTDIN());
      success = !m_ctx.GetSTDIN().empty();
      break;

    case 'O': // STDOUT
      packet.GetHexByteString(m_ctx.GetSTDOUT());
      success = !m_ctx.GetSTDOUT().empty();
      break;

    case 'E': // STDERR
      packet.GetHexByteString(m_ctx.GetSTDERR());
      success = !m_ctx.GetSTDERR().empty();
      break;

    default:
      break;
    }
    if (success)
      return SendPacket("OK");
    return SendErrorPacket("E57");
  }
  return SendErrorPacket("E58");
}

rnb_err_t RNBRemote::HandlePacket_QSetWorkingDir(const char *p) {
  // Only set the working directory if we don't already have a process
  if (!m_ctx.HasValidProcessID()) {
    StdStringExtractor packet(p += sizeof("QSetWorkingDir:") - 1);
    if (packet.GetHexByteString(m_ctx.GetWorkingDir())) {
      struct stat working_dir_stat;
      if (::stat(m_ctx.GetWorkingDirPath(), &working_dir_stat) == -1) {
        m_ctx.GetWorkingDir().clear();
        return SendErrorPacket("E61"); // Working directory doesn't exist...
      } else if ((working_dir_stat.st_mode & S_IFMT) == S_IFDIR) {
        return SendPacket("OK");
      } else {
        m_ctx.GetWorkingDir().clear();
        return SendErrorPacket("E62"); // Working directory isn't a directory...
      }
    }
    return SendErrorPacket("E59"); // Invalid path
  }
  return SendPacket(
      "E60"); // Already had a process, too late to set working dir
}

rnb_err_t RNBRemote::HandlePacket_QSyncThreadState(const char *p) {
  if (!m_ctx.HasValidProcessID()) {
    // We allow gdb to connect to a server that hasn't started running
    // the target yet.  gdb still wants to ask questions about it and
    // freaks out if it gets an error.  So just return OK here.
    return SendPacket("OK");
  }

  errno = 0;
  p += strlen("QSyncThreadState:");
  nub_thread_t tid = strtoul(p, NULL, 16);
  if (errno != 0 && tid == 0) {
    return HandlePacket_ILLFORMED(
        __FILE__, __LINE__, p,
        "Invalid thread number in QSyncThreadState packet");
  }
  if (DNBProcessSyncThreadState(m_ctx.ProcessID(), tid))
    return SendPacket("OK");
  else
    return SendErrorPacket("E61");
}

rnb_err_t RNBRemote::HandlePacket_QSetDetachOnError(const char *p) {
  p += sizeof("QSetDetachOnError:") - 1;
  bool should_detach = true;
  switch (*p) {
  case '0':
    should_detach = false;
    break;
  case '1':
    should_detach = true;
    break;
  default:
    return HandlePacket_ILLFORMED(
        __FILE__, __LINE__, p,
        "Invalid value for QSetDetachOnError - should be 0 or 1");
    break;
  }

  m_ctx.SetDetachOnError(should_detach);
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_QListThreadsInStopReply(const char *p) {
  // If this packet is received, it allows us to send an extra key/value
  // pair in the stop reply packets where we will list all of the thread IDs
  // separated by commas:
  //
  //  "threads:10a,10b,10c;"
  //
  // This will get included in the stop reply packet as something like:
  //
  //  "T11thread:10a;00:00000000;01:00010203:threads:10a,10b,10c;"
  //
  // This can save two packets on each stop: qfThreadInfo/qsThreadInfo and
  // speed things up a bit.
  //
  // Send the OK packet first so the correct checksum is appended...
  rnb_err_t result = SendPacket("OK");
  m_list_threads_in_stop_reply = true;

  return result;
}

rnb_err_t RNBRemote::HandlePacket_QSetMaxPayloadSize(const char *p) {
  /* The number of characters in a packet payload that gdb is
   prepared to accept.  The packet-start char, packet-end char,
   2 checksum chars and terminating null character are not included
   in this size.  */
  p += sizeof("QSetMaxPayloadSize:") - 1;
  errno = 0;
  uint32_t size = static_cast<uint32_t>(strtoul(p, NULL, 16));
  if (errno != 0 && size == 0) {
    return HandlePacket_ILLFORMED(
        __FILE__, __LINE__, p, "Invalid length in QSetMaxPayloadSize packet");
  }
  m_max_payload_size = size;
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_QSetMaxPacketSize(const char *p) {
  /* This tells us the largest packet that gdb can handle.
   i.e. the size of gdb's packet-reading buffer.
   QSetMaxPayloadSize is preferred because it is less ambiguous.  */
  p += sizeof("QSetMaxPacketSize:") - 1;
  errno = 0;
  uint32_t size = static_cast<uint32_t>(strtoul(p, NULL, 16));
  if (errno != 0 && size == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid length in QSetMaxPacketSize packet");
  }
  m_max_payload_size = size - 5;
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_QEnvironment(const char *p) {
  /* This sets the environment for the target program.  The packet is of the
   form:

   QEnvironment:VARIABLE=VALUE

   */

  DNBLogThreadedIf(
      LOG_RNB_REMOTE, "%8u RNBRemote::%s Handling QEnvironment: \"%s\"",
      (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__, p);

  p += sizeof("QEnvironment:") - 1;
  RNBContext &ctx = Context();

  ctx.PushEnvironment(p);
  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_QEnvironmentHexEncoded(const char *p) {
  /* This sets the environment for the target program.  The packet is of the
     form:

      QEnvironmentHexEncoded:VARIABLE=VALUE

      The VARIABLE=VALUE part is sent hex-encoded so characters like '#' with
     special
      meaning in the remote protocol won't break it.
  */

  DNBLogThreadedIf(LOG_RNB_REMOTE,
                   "%8u RNBRemote::%s Handling QEnvironmentHexEncoded: \"%s\"",
                   (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true),
                   __FUNCTION__, p);

  p += sizeof("QEnvironmentHexEncoded:") - 1;

  std::string arg;
  const char *c;
  c = p;
  while (*c != '\0') {
    if (*(c + 1) == '\0') {
      return HandlePacket_ILLFORMED(
          __FILE__, __LINE__, p,
          "non-hex char in arg on 'QEnvironmentHexEncoded' pkt");
    }
    char smallbuf[3];
    smallbuf[0] = *c;
    smallbuf[1] = *(c + 1);
    smallbuf[2] = '\0';
    errno = 0;
    int ch = static_cast<int>(strtoul(smallbuf, NULL, 16));
    if (errno != 0 && ch == 0) {
      return HandlePacket_ILLFORMED(
          __FILE__, __LINE__, p,
          "non-hex char in arg on 'QEnvironmentHexEncoded' pkt");
    }
    arg.push_back(ch);
    c += 2;
  }

  RNBContext &ctx = Context();
  if (arg.length() > 0)
    ctx.PushEnvironment(arg.c_str());

  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_QLaunchArch(const char *p) {
  p += sizeof("QLaunchArch:") - 1;
  if (DNBSetArchitecture(p))
    return SendPacket("OK");
  return SendErrorPacket("E63");
}

rnb_err_t RNBRemote::HandlePacket_QSetProcessEvent(const char *p) {
  p += sizeof("QSetProcessEvent:") - 1;
  // If the process is running, then send the event to the process, otherwise
  // store it in the context.
  if (Context().HasValidProcessID()) {
    if (DNBProcessSendEvent(Context().ProcessID(), p))
      return SendPacket("OK");
    else
      return SendErrorPacket("E80");
  } else {
    Context().PushProcessEvent(p);
  }
  return SendPacket("OK");
}

void register_value_in_hex_fixed_width(std::ostream &ostrm, nub_process_t pid,
                                       nub_thread_t tid,
                                       const register_map_entry_t *reg,
                                       const DNBRegisterValue *reg_value_ptr) {
  if (reg != NULL) {
    DNBRegisterValue reg_value;
    if (reg_value_ptr == NULL) {
      if (DNBThreadGetRegisterValueByID(pid, tid, reg->nub_info.set,
                                        reg->nub_info.reg, &reg_value))
        reg_value_ptr = &reg_value;
    }

    if (reg_value_ptr) {
      append_hex_value(ostrm, reg_value_ptr->value.v_uint8, reg->nub_info.size,
                       false);
    } else {
      // If we fail to read a register value, check if it has a default
      // fail value. If it does, return this instead in case some of
      // the registers are not available on the current system.
      if (reg->nub_info.size > 0) {
        std::vector<uint8_t> zeros(reg->nub_info.size, '\0');
        append_hex_value(ostrm, zeros.data(), zeros.size(), false);
      }
    }
  }
}

void debugserver_regnum_with_fixed_width_hex_register_value(
    std::ostream &ostrm, nub_process_t pid, nub_thread_t tid,
    const register_map_entry_t *reg, const DNBRegisterValue *reg_value_ptr) {
  // Output the register number as 'NN:VVVVVVVV;' where NN is a 2 bytes HEX
  // gdb register number, and VVVVVVVV is the correct number of hex bytes
  // as ASCII for the register value.
  if (reg != NULL) {
    ostrm << RAWHEX8(reg->debugserver_regnum) << ':';
    register_value_in_hex_fixed_width(ostrm, pid, tid, reg, reg_value_ptr);
    ostrm << ';';
  }
}

void RNBRemote::DispatchQueueOffsets::GetThreadQueueInfo(
    nub_process_t pid, nub_addr_t dispatch_qaddr, nub_addr_t &dispatch_queue_t,
    std::string &queue_name, uint64_t &queue_width,
    uint64_t &queue_serialnum) const {
  queue_name.clear();
  queue_width = 0;
  queue_serialnum = 0;

  if (IsValid() && dispatch_qaddr != INVALID_NUB_ADDRESS &&
      dispatch_qaddr != 0) {
    dispatch_queue_t = DNBProcessMemoryReadPointer(pid, dispatch_qaddr);
    if (dispatch_queue_t) {
      queue_width = DNBProcessMemoryReadInteger(
          pid, dispatch_queue_t + dqo_width, dqo_width_size, 0);
      queue_serialnum = DNBProcessMemoryReadInteger(
          pid, dispatch_queue_t + dqo_serialnum, dqo_serialnum_size, 0);

      if (dqo_version >= 4) {
        // libdispatch versions 4+, pointer to dispatch name is in the
        // queue structure.
        nub_addr_t pointer_to_label_address = dispatch_queue_t + dqo_label;
        nub_addr_t label_addr =
            DNBProcessMemoryReadPointer(pid, pointer_to_label_address);
        if (label_addr)
          queue_name = DNBProcessMemoryReadCString(pid, label_addr);
      } else {
        // libdispatch versions 1-3, dispatch name is a fixed width char array
        // in the queue structure.
        queue_name = DNBProcessMemoryReadCStringFixed(
            pid, dispatch_queue_t + dqo_label, dqo_label_size);
      }
    }
  }
}

struct StackMemory {
  uint8_t bytes[2 * sizeof(nub_addr_t)];
  nub_size_t length;
};
typedef std::map<nub_addr_t, StackMemory> StackMemoryMap;

static void ReadStackMemory(nub_process_t pid, nub_thread_t tid,
                            StackMemoryMap &stack_mmap,
                            uint32_t backtrace_limit = 256) {
  DNBRegisterValue reg_value;
  if (DNBThreadGetRegisterValueByID(pid, tid, REGISTER_SET_GENERIC,
                                    GENERIC_REGNUM_FP, &reg_value)) {
    uint32_t frame_count = 0;
    uint64_t fp = 0;
    if (reg_value.info.size == 4)
      fp = reg_value.value.uint32;
    else
      fp = reg_value.value.uint64;
    while (fp != 0) {
      // Make sure we never recurse more than 256 times so we don't recurse too
      // far or
      // store up too much memory in the expedited cache
      if (++frame_count > backtrace_limit)
        break;

      const nub_size_t read_size = reg_value.info.size * 2;
      StackMemory stack_memory;
      stack_memory.length = read_size;
      if (DNBProcessMemoryRead(pid, fp, read_size, stack_memory.bytes) !=
          read_size)
        break;
      // Make sure we don't try to put the same stack memory in more than once
      if (stack_mmap.find(fp) != stack_mmap.end())
        break;
      // Put the entry into the cache
      stack_mmap[fp] = stack_memory;
      // Dereference the frame pointer to get to the previous frame pointer
      if (reg_value.info.size == 4)
        fp = ((uint32_t *)stack_memory.bytes)[0];
      else
        fp = ((uint64_t *)stack_memory.bytes)[0];
    }
  }
}

rnb_err_t RNBRemote::SendStopReplyPacketForThread(nub_thread_t tid) {
  const nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendErrorPacket("E50");

  struct DNBThreadStopInfo tid_stop_info;

  /* Fill the remaining space in this packet with as many registers
   as we can stuff in there.  */

  if (DNBThreadGetStopReason(pid, tid, &tid_stop_info)) {
    const bool did_exec = tid_stop_info.reason == eStopTypeExec;
    if (did_exec) {
      RNBRemote::InitializeRegisters(true);

      // Reset any symbols that need resetting when we exec
      m_dispatch_queue_offsets_addr = INVALID_NUB_ADDRESS;
      m_dispatch_queue_offsets.Clear();
    }

    std::ostringstream ostrm;
    // Output the T packet with the thread
    ostrm << 'T';
    int signum = tid_stop_info.details.signal.signo;
    DNBLogThreadedIf(
        LOG_RNB_PROC, "%8d %s got signal signo = %u, exc_type = %u",
        (uint32_t)m_comm.Timer().ElapsedMicroSeconds(true), __FUNCTION__,
        signum, tid_stop_info.details.exception.type);

    // Translate any mach exceptions to gdb versions, unless they are
    // common exceptions like a breakpoint or a soft signal.
    switch (tid_stop_info.details.exception.type) {
    default:
      signum = 0;
      break;
    case EXC_BREAKPOINT:
      signum = SIGTRAP;
      break;
    case EXC_BAD_ACCESS:
      signum = TARGET_EXC_BAD_ACCESS;
      break;
    case EXC_BAD_INSTRUCTION:
      signum = TARGET_EXC_BAD_INSTRUCTION;
      break;
    case EXC_ARITHMETIC:
      signum = TARGET_EXC_ARITHMETIC;
      break;
    case EXC_EMULATION:
      signum = TARGET_EXC_EMULATION;
      break;
    case EXC_SOFTWARE:
      if (tid_stop_info.details.exception.data_count == 2 &&
          tid_stop_info.details.exception.data[0] == EXC_SOFT_SIGNAL)
        signum = static_cast<int>(tid_stop_info.details.exception.data[1]);
      else
        signum = TARGET_EXC_SOFTWARE;
      break;
    }

    ostrm << RAWHEX8(signum & 0xff);

    ostrm << std::hex << "thread:" << tid << ';';

    const char *thread_name = DNBThreadGetName(pid, tid);
    if (thread_name && thread_name[0]) {
      size_t thread_name_len = strlen(thread_name);

      if (::strcspn(thread_name, "$#+-;:") == thread_name_len)
        ostrm << std::hex << "name:" << thread_name << ';';
      else {
        // the thread name contains special chars, send as hex bytes
        ostrm << std::hex << "hexname:";
        const uint8_t *u_thread_name = (const uint8_t *)thread_name;
        for (size_t i = 0; i < thread_name_len; i++)
          ostrm << RAWHEX8(u_thread_name[i]);
        ostrm << ';';
      }
    }

    // If a 'QListThreadsInStopReply' was sent to enable this feature, we
    // will send all thread IDs back in the "threads" key whose value is
    // a list of hex thread IDs separated by commas:
    //  "threads:10a,10b,10c;"
    // This will save the debugger from having to send a pair of qfThreadInfo
    // and qsThreadInfo packets, but it also might take a lot of room in the
    // stop reply packet, so it must be enabled only on systems where there
    // are no limits on packet lengths.
    if (m_list_threads_in_stop_reply) {
      const nub_size_t numthreads = DNBProcessGetNumThreads(pid);
      if (numthreads > 0) {
        std::vector<uint64_t> pc_values;
        ostrm << std::hex << "threads:";
        for (nub_size_t i = 0; i < numthreads; ++i) {
          nub_thread_t th = DNBProcessGetThreadAtIndex(pid, i);
          if (i > 0)
            ostrm << ',';
          ostrm << std::hex << th;
          DNBRegisterValue pc_regval;
          if (DNBThreadGetRegisterValueByID(pid, th, REGISTER_SET_GENERIC,
                                            GENERIC_REGNUM_PC, &pc_regval)) {
            uint64_t pc = INVALID_NUB_ADDRESS;
            if (pc_regval.value.uint64 != INVALID_NUB_ADDRESS) {
              if (pc_regval.info.size == 4) {
                pc = pc_regval.value.uint32;
              } else if (pc_regval.info.size == 8) {
                pc = pc_regval.value.uint64;
              }
              if (pc != INVALID_NUB_ADDRESS) {
                pc_values.push_back(pc);
              }
            }
          }
        }
        ostrm << ';';

        // If we failed to get any of the thread pc values, the size of our
        // vector will not
        // be the same as the # of threads.  Don't provide any expedited thread
        // pc values in
        // that case.  This should not happen.
        if (pc_values.size() == numthreads) {
          ostrm << std::hex << "thread-pcs:";
          for (nub_size_t i = 0; i < numthreads; ++i) {
            if (i > 0)
              ostrm << ',';
            ostrm << std::hex << pc_values[i];
          }
          ostrm << ';';
        }
      }

      // Include JSON info that describes the stop reason for any threads
      // that actually have stop reasons. We use the new "jstopinfo" key
      // whose values is hex ascii JSON that contains the thread IDs
      // thread stop info only for threads that have stop reasons. Only send
      // this if we have more than one thread otherwise this packet has all
      // the info it needs.
      if (numthreads > 1) {
        const bool threads_with_valid_stop_info_only = true;
        JSONGenerator::ObjectSP threads_info_sp =
            GetJSONThreadsInfo(threads_with_valid_stop_info_only);
        if (threads_info_sp) {
          ostrm << std::hex << "jstopinfo:";
          std::ostringstream json_strm;
          threads_info_sp->Dump(json_strm);
          threads_info_sp->Clear();
          append_hexified_string(ostrm, json_strm.str());
          ostrm << ';';
        }
      }
    }

    if (g_num_reg_entries == 0)
      InitializeRegisters();

    if (g_reg_entries != NULL) {
      auto interesting_regset = [](int regset) -> bool {
#if defined(__arm64__) || defined(__aarch64__)
        // GPRs and exception registers, helpful for debugging
        // from packet logs.
        return regset == 1 || regset == 3;
#else
        return regset == 1;
#endif
      };

      DNBRegisterValue reg_value;
      for (uint32_t reg = 0; reg < g_num_reg_entries; reg++) {
        // Expedite all registers in the first register set that aren't
        // contained in other registers
        if (interesting_regset(g_reg_entries[reg].nub_info.set) &&
            g_reg_entries[reg].nub_info.value_regs == NULL) {
          if (!DNBThreadGetRegisterValueByID(
                  pid, tid, g_reg_entries[reg].nub_info.set,
                  g_reg_entries[reg].nub_info.reg, &reg_value))
            continue;

          debugserver_regnum_with_fixed_width_hex_register_value(
              ostrm, pid, tid, &g_reg_entries[reg], &reg_value);
        }
      }
    }

    if (did_exec) {
      ostrm << "reason:exec;";
    } else if (tid_stop_info.reason == eStopTypeWatchpoint) {
      ostrm << "reason:watchpoint;";
      ostrm << "description:";
      std::ostringstream wp_desc;
      wp_desc << tid_stop_info.details.watchpoint.addr << " ";
      wp_desc << tid_stop_info.details.watchpoint.hw_idx << " ";
      wp_desc << tid_stop_info.details.watchpoint.mach_exception_addr;
      append_hexified_string(ostrm, wp_desc.str());
      ostrm << ";";

      // Temporarily, print all of the fields we've parsed out of the ESR
      // on a watchpoint exception.  Normally this is something we would
      // log for LOG_WATCHPOINTS only, but this was implemented from the
      // ARM ARM spec and hasn't been exercised on real hardware that can
      // set most of these fields yet.  It may need to be debugged in the
      // future, so include all of these purely for debugging by reading
      // the packet logs; lldb isn't using these fields.
      ostrm << "watch_addr:" << std::hex
            << tid_stop_info.details.watchpoint.addr << ";";
      ostrm << "me_watch_addr:" << std::hex
            << tid_stop_info.details.watchpoint.mach_exception_addr << ";";
      ostrm << "wp_hw_idx:" << std::hex
            << tid_stop_info.details.watchpoint.hw_idx << ";";
      if (tid_stop_info.details.watchpoint.esr_fields_set) {
        ostrm << "wp_esr_iss:" << std::hex
              << tid_stop_info.details.watchpoint.esr_fields.iss << ";";
        ostrm << "wp_esr_wpt:" << std::hex
              << tid_stop_info.details.watchpoint.esr_fields.wpt << ";";
        ostrm << "wp_esr_wptv:"
              << tid_stop_info.details.watchpoint.esr_fields.wptv << ";";
        ostrm << "wp_esr_wpf:"
              << tid_stop_info.details.watchpoint.esr_fields.wpf << ";";
        ostrm << "wp_esr_fnp:"
              << tid_stop_info.details.watchpoint.esr_fields.fnp << ";";
        ostrm << "wp_esr_vncr:"
              << tid_stop_info.details.watchpoint.esr_fields.vncr << ";";
        ostrm << "wp_esr_fnv:"
              << tid_stop_info.details.watchpoint.esr_fields.fnv << ";";
        ostrm << "wp_esr_cm:" << tid_stop_info.details.watchpoint.esr_fields.cm
              << ";";
        ostrm << "wp_esr_wnr:"
              << tid_stop_info.details.watchpoint.esr_fields.wnr << ";";
        ostrm << "wp_esr_dfsc:" << std::hex
              << tid_stop_info.details.watchpoint.esr_fields.dfsc << ";";
      }
    } else if (tid_stop_info.details.exception.type) {
      ostrm << "metype:" << std::hex << tid_stop_info.details.exception.type
            << ';';
      ostrm << "mecount:" << std::hex
            << tid_stop_info.details.exception.data_count << ';';
      for (nub_size_t i = 0; i < tid_stop_info.details.exception.data_count;
           ++i)
        ostrm << "medata:" << std::hex
              << tid_stop_info.details.exception.data[i] << ';';
    }

    // Add expedited stack memory so stack backtracing doesn't need to read
    // anything from the
    // frame pointer chain.
    StackMemoryMap stack_mmap;
    ReadStackMemory(pid, tid, stack_mmap, 2);
    if (!stack_mmap.empty()) {
      for (const auto &stack_memory : stack_mmap) {
        ostrm << "memory:" << HEXBASE << stack_memory.first << '=';
        append_hex_value(ostrm, stack_memory.second.bytes,
                         stack_memory.second.length, false);
        ostrm << ';';
      }
    }

    return SendPacket(ostrm.str());
  }
  return SendErrorPacket("E51");
}

/* '?'
 The stop reply packet - tell gdb what the status of the inferior is.
 Often called the questionmark_packet.  */

rnb_err_t RNBRemote::HandlePacket_last_signal(const char *unused) {
  if (!m_ctx.HasValidProcessID()) {
    // Inferior is not yet specified/running
    return SendErrorPacket("E02");
  }

  nub_process_t pid = m_ctx.ProcessID();
  nub_state_t pid_state = DNBProcessGetState(pid);

  switch (pid_state) {
  case eStateAttaching:
  case eStateLaunching:
  case eStateRunning:
  case eStateStepping:
  case eStateDetached:
    return rnb_success; // Ignore

  case eStateSuspended:
  case eStateStopped:
  case eStateCrashed: {
    nub_thread_t tid = DNBProcessGetCurrentThread(pid);
    // Make sure we set the current thread so g and p packets return
    // the data the gdb will expect.
    SetCurrentThread(tid);

    SendStopReplyPacketForThread(tid);
  } break;

  case eStateInvalid:
  case eStateUnloaded:
  case eStateExited: {
    char pid_exited_packet[16] = "";
    int pid_status = 0;
    // Process exited with exit status
    if (!DNBProcessGetExitStatus(pid, &pid_status))
      pid_status = 0;

    if (pid_status) {
      if (WIFEXITED(pid_status))
        snprintf(pid_exited_packet, sizeof(pid_exited_packet), "W%02x",
                 WEXITSTATUS(pid_status));
      else if (WIFSIGNALED(pid_status))
        snprintf(pid_exited_packet, sizeof(pid_exited_packet), "X%02x",
                 WTERMSIG(pid_status));
      else if (WIFSTOPPED(pid_status))
        snprintf(pid_exited_packet, sizeof(pid_exited_packet), "S%02x",
                 WSTOPSIG(pid_status));
    }

    // If we have an empty exit packet, lets fill one in to be safe.
    if (!pid_exited_packet[0]) {
      strlcpy(pid_exited_packet, "W00", sizeof(pid_exited_packet) - 1);
      pid_exited_packet[sizeof(pid_exited_packet) - 1] = '\0';
    }

    const char *exit_info = DNBProcessGetExitInfo(pid);
    if (exit_info != NULL && *exit_info != '\0') {
      std::ostringstream exit_packet;
      exit_packet << pid_exited_packet;
      exit_packet << ';';
      exit_packet << RAW_HEXBASE << "description";
      exit_packet << ':';
      for (size_t i = 0; exit_info[i] != '\0'; i++)
        exit_packet << RAWHEX8(exit_info[i]);
      exit_packet << ';';
      return SendPacket(exit_packet.str());
    } else
      return SendPacket(pid_exited_packet);
  } break;
  }
  return rnb_success;
}

rnb_err_t RNBRemote::HandlePacket_M(const char *p) {
  if (p == NULL || p[0] == '\0' || strlen(p) < 3) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p, "Too short M packet");
  }

  char *c;
  p++;
  errno = 0;
  nub_addr_t addr = strtoull(p, &c, 16);
  if (errno != 0 && addr == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid address in M packet");
  }
  if (*c != ',') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Comma sep missing in M packet");
  }

  /* Advance 'p' to the length part of the packet.  */
  p += (c - p) + 1;

  errno = 0;
  unsigned long length = strtoul(p, &c, 16);
  if (errno != 0 && length == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid length in M packet");
  }
  if (length == 0) {
    return SendPacket("OK");
  }

  if (*c != ':') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Missing colon in M packet");
  }
  /* Advance 'p' to the data part of the packet.  */
  p += (c - p) + 1;

  size_t datalen = strlen(p);
  if (datalen & 0x1) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Uneven # of hex chars for data in M packet");
  }
  if (datalen == 0) {
    return SendPacket("OK");
  }

  uint8_t *buf = (uint8_t *)alloca(datalen / 2);
  uint8_t *i = buf;

  while (*p != '\0' && *(p + 1) != '\0') {
    char hexbuf[3];
    hexbuf[0] = *p;
    hexbuf[1] = *(p + 1);
    hexbuf[2] = '\0';
    errno = 0;
    uint8_t byte = strtoul(hexbuf, NULL, 16);
    if (errno != 0 && byte == 0) {
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "Invalid hex byte in M packet");
    }
    *i++ = byte;
    p += 2;
  }

  nub_size_t wrote =
      DNBProcessMemoryWrite(m_ctx.ProcessID(), addr, length, buf);
  if (wrote != length)
    return SendErrorPacket("E09");
  else
    return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_m(const char *p) {
  if (p == NULL || p[0] == '\0' || strlen(p) < 3) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p, "Too short m packet");
  }

  char *c;
  p++;
  errno = 0;
  nub_addr_t addr = strtoull(p, &c, 16);
  if (errno != 0 && addr == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid address in m packet");
  }
  if (*c != ',') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Comma sep missing in m packet");
  }

  /* Advance 'p' to the length part of the packet.  */
  p += (c - p) + 1;

  errno = 0;
  auto length = strtoul(p, NULL, 16);
  if (errno != 0 && length == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid length in m packet");
  }
  if (length == 0) {
    return SendPacket("");
  }

  std::string buf(length, '\0');
  if (buf.empty()) {
    return SendErrorPacket("E78");
  }
  nub_size_t bytes_read =
      DNBProcessMemoryRead(m_ctx.ProcessID(), addr, buf.size(), &buf[0]);
  if (bytes_read == 0) {
    return SendErrorPacket("E08");
  }

  // "The reply may contain fewer bytes than requested if the server was able
  //  to read only part of the region of memory."
  length = bytes_read;

  std::ostringstream ostrm;
  for (unsigned long i = 0; i < length; i++)
    ostrm << RAWHEX8(buf[i]);
  return SendPacket(ostrm.str());
}

// Read memory, sent it up as binary data.
// Usage:  xADDR,LEN
// ADDR and LEN are both base 16.

// Responds with 'OK' for zero-length request
// or
//
// DATA
//
// where DATA is the binary data payload.

rnb_err_t RNBRemote::HandlePacket_x(const char *p) {
  if (p == NULL || p[0] == '\0' || strlen(p) < 3) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p, "Too short X packet");
  }

  char *c;
  p++;
  errno = 0;
  nub_addr_t addr = strtoull(p, &c, 16);
  if (errno != 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid address in X packet");
  }
  if (*c != ',') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Comma sep missing in X packet");
  }

  /* Advance 'p' to the number of bytes to be read.  */
  p += (c - p) + 1;

  errno = 0;
  auto length = strtoul(p, NULL, 16);
  if (errno != 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid length in x packet");
  }

  // zero length read means this is a test of whether that packet is implemented
  // or not.
  if (length == 0) {
    return SendPacket("OK");
  }

  std::vector<uint8_t> buf(length);

  if (buf.capacity() != length) {
    return SendErrorPacket("E79");
  }
  nub_size_t bytes_read =
      DNBProcessMemoryRead(m_ctx.ProcessID(), addr, buf.size(), &buf[0]);
  if (bytes_read == 0) {
    return SendErrorPacket("E80");
  }

  std::vector<uint8_t> buf_quoted;
  buf_quoted.reserve(bytes_read + 30);
  for (nub_size_t i = 0; i < bytes_read; i++) {
    if (buf[i] == '#' || buf[i] == '$' || buf[i] == '}' || buf[i] == '*') {
      buf_quoted.push_back(0x7d);
      buf_quoted.push_back(buf[i] ^ 0x20);
    } else {
      buf_quoted.push_back(buf[i]);
    }
  }
  length = buf_quoted.size();

  std::ostringstream ostrm;
  for (unsigned long i = 0; i < length; i++)
    ostrm << buf_quoted[i];

  return SendPacket(ostrm.str());
}

rnb_err_t RNBRemote::HandlePacket_X(const char *p) {
  if (p == NULL || p[0] == '\0' || strlen(p) < 3) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p, "Too short X packet");
  }

  char *c;
  p++;
  errno = 0;
  nub_addr_t addr = strtoull(p, &c, 16);
  if (errno != 0 && addr == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid address in X packet");
  }
  if (*c != ',') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Comma sep missing in X packet");
  }

  /* Advance 'p' to the length part of the packet.  NB this is the length of the
     packet
     including any escaped chars.  The data payload may be a little bit smaller
     after
     decoding.  */
  p += (c - p) + 1;

  errno = 0;
  auto length = strtoul(p, NULL, 16);
  if (errno != 0 && length == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid length in X packet");
  }

  // I think gdb sends a zero length write request to test whether this
  // packet is accepted.
  if (length == 0) {
    return SendPacket("OK");
  }

  std::vector<uint8_t> data = decode_binary_data(c, -1);
  std::vector<uint8_t>::const_iterator it;
  uint8_t *buf = (uint8_t *)alloca(data.size());
  uint8_t *i = buf;
  for (it = data.begin(); it != data.end(); ++it) {
    *i++ = *it;
  }

  nub_size_t wrote =
      DNBProcessMemoryWrite(m_ctx.ProcessID(), addr, data.size(), buf);
  if (wrote != data.size())
    return SendErrorPacket("E08");
  return SendPacket("OK");
}

/* 'g' -- read registers
 Get the contents of the registers for the current thread,
 send them to gdb.
 Should the setting of the Hg packet determine which thread's registers
 are returned?  */

rnb_err_t RNBRemote::HandlePacket_g(const char *p) {
  std::ostringstream ostrm;
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E11");
  }

  if (g_num_reg_entries == 0)
    InitializeRegisters();

  nub_process_t pid = m_ctx.ProcessID();
  nub_thread_t tid = ExtractThreadIDFromThreadSuffix(p + 1);
  if (tid == INVALID_NUB_THREAD)
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "No thread specified in p packet");

  // Get the register context size first by calling with NULL buffer
  nub_size_t reg_ctx_size = DNBThreadGetRegisterContext(pid, tid, NULL, 0);
  if (reg_ctx_size) {
    // Now allocate enough space for the entire register context
    std::vector<uint8_t> reg_ctx;
    reg_ctx.resize(reg_ctx_size);
    // Now read the register context
    reg_ctx_size =
        DNBThreadGetRegisterContext(pid, tid, &reg_ctx[0], reg_ctx.size());
    if (reg_ctx_size) {
      append_hex_value(ostrm, reg_ctx.data(), reg_ctx.size(), false);
      return SendPacket(ostrm.str());
    }
  }
  return SendErrorPacket("E74");
}

/* 'G XXX...' -- write registers
 How is the thread for these specified, beyond "the current thread"?
 Does gdb actually use the Hg packet to set this?  */

rnb_err_t RNBRemote::HandlePacket_G(const char *p) {
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E11");
  }

  if (g_num_reg_entries == 0)
    InitializeRegisters();

  StdStringExtractor packet(p);
  packet.SetFilePos(1); // Skip the 'G'

  nub_process_t pid = m_ctx.ProcessID();
  nub_thread_t tid = ExtractThreadIDFromThreadSuffix(p);
  if (tid == INVALID_NUB_THREAD)
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "No thread specified in p packet");

  // Get the register context size first by calling with NULL buffer
  nub_size_t reg_ctx_size = DNBThreadGetRegisterContext(pid, tid, NULL, 0);
  if (reg_ctx_size) {
    // Now allocate enough space for the entire register context
    std::vector<uint8_t> reg_ctx;
    reg_ctx.resize(reg_ctx_size);

    const nub_size_t bytes_extracted =
        packet.GetHexBytes(&reg_ctx[0], reg_ctx.size(), 0xcc);
    if (bytes_extracted == reg_ctx.size()) {
      // Now write the register context
      reg_ctx_size =
          DNBThreadSetRegisterContext(pid, tid, reg_ctx.data(), reg_ctx.size());
      if (reg_ctx_size == reg_ctx.size())
        return SendPacket("OK");
      else
        return SendErrorPacket("E55");
    } else {
      DNBLogError("RNBRemote::HandlePacket_G(%s): extracted %llu of %llu "
                  "bytes, size mismatch\n",
                  p, (uint64_t)bytes_extracted, (uint64_t)reg_ctx_size);
      return SendErrorPacket("E64");
    }
  }
  return SendErrorPacket("E65");
}

static bool RNBRemoteShouldCancelCallback(void *not_used) {
  RNBRemoteSP remoteSP(g_remoteSP);
  if (remoteSP.get() != NULL) {
    RNBRemote *remote = remoteSP.get();
    return !remote->Comm().IsConnected();
  }
  return true;
}

// FORMAT: _MXXXXXX,PPP
//      XXXXXX: big endian hex chars
//      PPP: permissions can be any combo of r w x chars
//
// RESPONSE: XXXXXX
//      XXXXXX: hex address of the newly allocated memory
//      EXX: error code
//
// EXAMPLES:
//      _M123000,rw
//      _M123000,rwx
//      _M123000,xw

rnb_err_t RNBRemote::HandlePacket_AllocateMemory(const char *p) {
  StdStringExtractor packet(p);
  packet.SetFilePos(2); // Skip the "_M"

  nub_addr_t size = packet.GetHexMaxU64(StdStringExtractor::BigEndian, 0);
  if (size != 0) {
    if (packet.GetChar() == ',') {
      uint32_t permissions = 0;
      char ch;
      bool success = true;
      while (success && (ch = packet.GetChar()) != '\0') {
        switch (ch) {
        case 'r':
          permissions |= eMemoryPermissionsReadable;
          break;
        case 'w':
          permissions |= eMemoryPermissionsWritable;
          break;
        case 'x':
          permissions |= eMemoryPermissionsExecutable;
          break;
        default:
          success = false;
          break;
        }
      }

      if (success) {
        nub_addr_t addr =
            DNBProcessMemoryAllocate(m_ctx.ProcessID(), size, permissions);
        if (addr != INVALID_NUB_ADDRESS) {
          std::ostringstream ostrm;
          ostrm << RAW_HEXBASE << addr;
          return SendPacket(ostrm.str());
        }
      }
    }
  }
  return SendErrorPacket("E53");
}

// FORMAT: _mXXXXXX
//      XXXXXX: address that was previously allocated
//
// RESPONSE: XXXXXX
//      OK: address was deallocated
//      EXX: error code
//
// EXAMPLES:
//      _m123000

rnb_err_t RNBRemote::HandlePacket_DeallocateMemory(const char *p) {
  StdStringExtractor packet(p);
  packet.SetFilePos(2); // Skip the "_m"
  nub_addr_t addr =
      packet.GetHexMaxU64(StdStringExtractor::BigEndian, INVALID_NUB_ADDRESS);

  if (addr != INVALID_NUB_ADDRESS) {
    if (DNBProcessMemoryDeallocate(m_ctx.ProcessID(), addr))
      return SendPacket("OK");
  }
  return SendErrorPacket("E54");
}

// FORMAT: QSaveRegisterState;thread:TTTT;  (when thread suffix is supported)
// FORMAT: QSaveRegisterState               (when thread suffix is NOT
// supported)
//      TTTT: thread ID in hex
//
// RESPONSE:
//      SAVEID: Where SAVEID is a decimal number that represents the save ID
//              that can be passed back into a "QRestoreRegisterState" packet
//      EXX: error code
//
// EXAMPLES:
//      QSaveRegisterState;thread:1E34;     (when thread suffix is supported)
//      QSaveRegisterState                  (when thread suffix is NOT
//      supported)

rnb_err_t RNBRemote::HandlePacket_SaveRegisterState(const char *p) {
  nub_process_t pid = m_ctx.ProcessID();
  nub_thread_t tid = ExtractThreadIDFromThreadSuffix(p);
  if (tid == INVALID_NUB_THREAD) {
    if (m_thread_suffix_supported)
      return HandlePacket_ILLFORMED(
          __FILE__, __LINE__, p,
          "No thread specified in QSaveRegisterState packet");
    else
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "No thread was is set with the Hg packet");
  }

  // Get the register context size first by calling with NULL buffer
  const uint32_t save_id = DNBThreadSaveRegisterState(pid, tid);
  if (save_id != 0) {
    char response[64];
    snprintf(response, sizeof(response), "%u", save_id);
    return SendPacket(response);
  } else {
    return SendErrorPacket("E75");
  }
}
// FORMAT: QRestoreRegisterState:SAVEID;thread:TTTT;  (when thread suffix is
// supported)
// FORMAT: QRestoreRegisterState:SAVEID               (when thread suffix is NOT
// supported)
//      TTTT: thread ID in hex
//      SAVEID: a decimal number that represents the save ID that was
//              returned from a call to "QSaveRegisterState"
//
// RESPONSE:
//      OK: successfully restored registers for the specified thread
//      EXX: error code
//
// EXAMPLES:
//      QRestoreRegisterState:1;thread:1E34;     (when thread suffix is
//      supported)
//      QRestoreRegisterState:1                  (when thread suffix is NOT
//      supported)

rnb_err_t RNBRemote::HandlePacket_RestoreRegisterState(const char *p) {
  nub_process_t pid = m_ctx.ProcessID();
  nub_thread_t tid = ExtractThreadIDFromThreadSuffix(p);
  if (tid == INVALID_NUB_THREAD) {
    if (m_thread_suffix_supported)
      return HandlePacket_ILLFORMED(
          __FILE__, __LINE__, p,
          "No thread specified in QSaveRegisterState packet");
    else
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "No thread was is set with the Hg packet");
  }

  StdStringExtractor packet(p);
  packet.SetFilePos(
      strlen("QRestoreRegisterState:")); // Skip the "QRestoreRegisterState:"
  const uint32_t save_id = packet.GetU32(0);

  if (save_id != 0) {
    // Get the register context size first by calling with NULL buffer
    if (DNBThreadRestoreRegisterState(pid, tid, save_id))
      return SendPacket("OK");
    else
      return SendErrorPacket("E77");
  }
  return SendErrorPacket("E76");
}

static bool GetProcessNameFrom_vAttach(const char *&p,
                                       std::string &attach_name) {
  bool return_val = true;
  while (*p != '\0') {
    char smallbuf[3];
    smallbuf[0] = *p;
    smallbuf[1] = *(p + 1);
    smallbuf[2] = '\0';

    errno = 0;
    int ch = static_cast<int>(strtoul(smallbuf, NULL, 16));
    if (errno != 0 && ch == 0) {
      return_val = false;
      break;
    }

    attach_name.push_back(ch);
    p += 2;
  }
  return return_val;
}

rnb_err_t RNBRemote::HandlePacket_qSupported(const char *p) {
  uint32_t max_packet_size = 128 * 1024; // 128KBytes is a reasonable max packet
                                         // size--debugger can always use less
  std::stringstream reply;
  reply << "qXfer:features:read+;PacketSize=" << std::hex << max_packet_size
        << ";";
  reply << "qEcho+;native-signals+;";

  bool enable_compression = false;
  (void)enable_compression;

#if (defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1) ||                      \
    (defined(TARGET_OS_IOS) && TARGET_OS_IOS == 1) ||                          \
    (defined(TARGET_OS_TV) && TARGET_OS_TV == 1) ||                            \
    (defined(TARGET_OS_BRIDGE) && TARGET_OS_BRIDGE == 1) ||                    \
    (defined(TARGET_OS_XR) && TARGET_OS_XR == 1)
  enable_compression = true;
#endif

  if (enable_compression) {
    reply << "SupportedCompressions=lzfse,zlib-deflate,lz4,lzma;";
  }

#if (defined(__arm64__) || defined(__aarch64__))
  reply << "SupportedWatchpointTypes=aarch64-mask,aarch64-bas;";
#endif
#if defined(__x86_64__)
  reply << "SupportedWatchpointTypes=x86_64;";
#endif

  return SendPacket(reply.str().c_str());
}

static bool process_does_not_exist (nub_process_t pid) {
  std::vector<struct kinfo_proc> proc_infos;
  DNBGetAllInfos (proc_infos);
  const size_t infos_size = proc_infos.size();
  for (size_t i = 0; i < infos_size; i++)
    if (proc_infos[i].kp_proc.p_pid == pid)
      return false;

  return true; // process does not exist
}

// my_uid and process_uid are only initialized if this function
// returns true -- that there was a uid mismatch -- and those
// id's may want to be used in the error message.
// 
// NOTE: this should only be called after process_does_not_exist().
// This sysctl will return uninitialized data if we ask for a pid
// that doesn't exist.  The alternative would be to fetch all
// processes and step through to find the one we're looking for
// (as process_does_not_exist() does).
static bool attach_failed_due_to_uid_mismatch (nub_process_t pid,
                                               uid_t &my_uid,
                                               uid_t &process_uid) {
  struct kinfo_proc kinfo;
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
  size_t len = sizeof(struct kinfo_proc);
  if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), &kinfo, &len, NULL, 0) != 0) {
    return false; // pid doesn't exist? can't check uid mismatch - it was fine
  }
  my_uid = geteuid();
  if (my_uid == 0)
    return false; // if we're root, attach didn't fail because of uid mismatch
  process_uid = kinfo.kp_eproc.e_ucred.cr_uid;

  // If my uid != the process' uid, then the attach probably failed because
  // of that.
  if (my_uid != process_uid)
    return true;
  else
    return false;
}

// NOTE: this should only be called after process_does_not_exist().
// This sysctl will return uninitialized data if we ask for a pid
// that doesn't exist.  The alternative would be to fetch all
// processes and step through to find the one we're looking for
// (as process_does_not_exist() does).
static bool process_is_already_being_debugged (nub_process_t pid) {
  if (DNBProcessIsBeingDebugged(pid) && DNBGetParentProcessID(pid) != getpid())
    return true;
  else
    return false;
}

// Test if this current login session has a connection to the
// window server (if it does not have that access, it cannot ask
// for debug permission by popping up a dialog box and attach
// may fail outright).
static bool login_session_has_gui_access () {
  // I believe this API only works on macOS.
#if TARGET_OS_OSX == 0
  return true;
#else
  auditinfo_addr_t info;
  getaudit_addr(&info, sizeof(info));
  if (info.ai_flags & AU_SESSION_FLAG_HAS_GRAPHIC_ACCESS)
    return true;
  else
    return false;
#endif
}

// Checking for 
//
//  {
//    'class' : 'rule',
//    'comment' : 'For use by Apple.  WARNING: administrators are advised
//              not to modify this right.',
//    'k-of-n' : '1',
//    'rule' : [
//      'is-admin',
//      'is-developer',
//      'authenticate-developer'
//    ]
//  }
//
// $ security authorizationdb read system.privilege.taskport.debug

static bool developer_mode_enabled () {
  // This API only exists on macOS.
#if TARGET_OS_OSX == 0
  return true;
#else
 CFDictionaryRef currentRightDict = NULL;
 const char *debug_right = "system.privilege.taskport.debug";
 // caller must free dictionary initialized by the following
 OSStatus status = AuthorizationRightGet(debug_right, &currentRightDict);
 if (status != errAuthorizationSuccess) {
   // could not check authorization
   return true;
 }

 bool devmode_enabled = true;

 if (!CFDictionaryContainsKey(currentRightDict, CFSTR("k-of-n"))) {
   devmode_enabled = false;
 } else {
   CFNumberRef item = (CFNumberRef) CFDictionaryGetValue(currentRightDict, CFSTR("k-of-n"));
   if (item && CFGetTypeID(item) == CFNumberGetTypeID()) {
      int64_t num = 0;
      ::CFNumberGetValue(item, kCFNumberSInt64Type, &num);
      if (num != 1) {
        devmode_enabled = false;
      }
   } else {
     devmode_enabled = false;
   }
 }

 if (!CFDictionaryContainsKey(currentRightDict, CFSTR("class"))) {
   devmode_enabled = false;
 } else {
   CFStringRef item = (CFStringRef) CFDictionaryGetValue(currentRightDict, CFSTR("class"));
   if (item && CFGetTypeID(item) == CFStringGetTypeID()) {
     char tmpbuf[128];
     if (CFStringGetCString (item, tmpbuf, sizeof(tmpbuf), CFStringGetSystemEncoding())) {
       tmpbuf[sizeof (tmpbuf) - 1] = '\0';
       if (strcmp (tmpbuf, "rule") != 0) {
         devmode_enabled = false;
       }
     } else {
       devmode_enabled = false;
     }
   } else {
     devmode_enabled = false;
   }
 }

 if (!CFDictionaryContainsKey(currentRightDict, CFSTR("rule"))) {
   devmode_enabled = false;
 } else {
   CFArrayRef item = (CFArrayRef) CFDictionaryGetValue(currentRightDict, CFSTR("rule"));
   if (item && CFGetTypeID(item) == CFArrayGetTypeID()) {
     int count = ::CFArrayGetCount(item);
      CFRange range = CFRangeMake (0, count);
     if (!::CFArrayContainsValue (item, range, CFSTR("is-admin")))
       devmode_enabled = false;
     if (!::CFArrayContainsValue (item, range, CFSTR("is-developer")))
       devmode_enabled = false;
     if (!::CFArrayContainsValue (item, range, CFSTR("authenticate-developer")))
       devmode_enabled = false;
   } else {
     devmode_enabled = false;
   }
 }
 ::CFRelease(currentRightDict);

 return devmode_enabled;
#endif // TARGET_OS_OSX
}

/*
 vAttach;pid

 Attach to a new process with the specified process ID. pid is a hexadecimal
 integer
 identifying the process. If the stub is currently controlling a process, it is
 killed. The attached process is stopped.This packet is only available in
 extended
 mode (see extended mode).

 Reply:
 "ENN"                      for an error
 "Any Stop Reply Packet"     for success
 */

rnb_err_t RNBRemote::HandlePacket_v(const char *p) {
  if (strcmp(p, "vCont;c") == 0) {
    // Simple continue
    return RNBRemote::HandlePacket_c("c");
  } else if (strcmp(p, "vCont;s") == 0) {
    // Simple step
    return RNBRemote::HandlePacket_s("s");
  } else if (strstr(p, "vCont") == p) {
    DNBThreadResumeActions thread_actions;
    char *c = const_cast<char *>(p += strlen("vCont"));
    char *c_end = c + strlen(c);
    if (*c == '?')
      return SendPacket("vCont;c;C;s;S");

    while (c < c_end && *c == ';') {
      ++c; // Skip the semi-colon
      DNBThreadResumeAction thread_action;
      thread_action.tid = INVALID_NUB_THREAD;
      thread_action.state = eStateInvalid;
      thread_action.signal = 0;
      thread_action.addr = INVALID_NUB_ADDRESS;

      char action = *c++;

      switch (action) {
      case 'C':
        errno = 0;
        thread_action.signal = static_cast<int>(strtoul(c, &c, 16));
        if (errno != 0)
          return HandlePacket_ILLFORMED(
              __FILE__, __LINE__, p, "Could not parse signal in vCont packet");
      // Fall through to next case...
        [[clang::fallthrough]];
      case 'c':
        // Continue
        thread_action.state = eStateRunning;
        break;

      case 'S':
        errno = 0;
        thread_action.signal = static_cast<int>(strtoul(c, &c, 16));
        if (errno != 0)
          return HandlePacket_ILLFORMED(
              __FILE__, __LINE__, p, "Could not parse signal in vCont packet");
      // Fall through to next case...
        [[clang::fallthrough]];
      case 's':
        // Step
        thread_action.state = eStateStepping;
        break;

      default:
        HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                               "Unsupported action in vCont packet");
        break;
      }
      if (*c == ':') {
        errno = 0;
        thread_action.tid = strtoul(++c, &c, 16);
        if (errno != 0)
          return HandlePacket_ILLFORMED(
              __FILE__, __LINE__, p,
              "Could not parse thread number in vCont packet");
      }

      thread_actions.Append(thread_action);
    }

    // If a default action for all other threads wasn't mentioned
    // then we should stop the threads
    thread_actions.SetDefaultThreadActionIfNeeded(eStateStopped, 0);
    DNBProcessResume(m_ctx.ProcessID(), thread_actions.GetFirst(),
                     thread_actions.GetSize());
    return rnb_success;
  } else if (strstr(p, "vAttach") == p) {
    nub_process_t attach_pid =
        INVALID_NUB_PROCESS; // attach_pid will be set to 0 if the attach fails
    nub_process_t pid_attaching_to =
        INVALID_NUB_PROCESS; // pid_attaching_to is the original pid specified
    char err_str[1024] = {'\0'};
    std::string attach_name;

    if (strstr(p, "vAttachWait;") == p) {
      p += strlen("vAttachWait;");
      if (!GetProcessNameFrom_vAttach(p, attach_name)) {
        return HandlePacket_ILLFORMED(
            __FILE__, __LINE__, p, "non-hex char in arg on 'vAttachWait' pkt");
      }
      DNBLog("[LaunchAttach] START %d vAttachWait for process name '%s'",
             getpid(), attach_name.c_str());
      const bool ignore_existing = true;
      attach_pid = DNBProcessAttachWait(
          &m_ctx, attach_name.c_str(), ignore_existing, NULL, 1000, err_str,
          sizeof(err_str), RNBRemoteShouldCancelCallback);

    } else if (strstr(p, "vAttachOrWait;") == p) {
      p += strlen("vAttachOrWait;");
      if (!GetProcessNameFrom_vAttach(p, attach_name)) {
        return HandlePacket_ILLFORMED(
            __FILE__, __LINE__, p,
            "non-hex char in arg on 'vAttachOrWait' pkt");
      }
      const bool ignore_existing = false;
      DNBLog("[LaunchAttach] START %d vAttachWaitOrWait for process name "
             "'%s'",
             getpid(), attach_name.c_str());
      attach_pid = DNBProcessAttachWait(
          &m_ctx, attach_name.c_str(), ignore_existing, NULL, 1000, err_str,
          sizeof(err_str), RNBRemoteShouldCancelCallback);
    } else if (strstr(p, "vAttachName;") == p) {
      p += strlen("vAttachName;");
      if (!GetProcessNameFrom_vAttach(p, attach_name)) {
        return HandlePacket_ILLFORMED(
            __FILE__, __LINE__, p, "non-hex char in arg on 'vAttachName' pkt");
      }

      DNBLog("[LaunchAttach] START %d vAttachName attach to process name "
             "'%s'",
             getpid(), attach_name.c_str());
      attach_pid = DNBProcessAttachByName(attach_name.c_str(), NULL,
                                          Context().GetIgnoredExceptions(), 
                                          err_str, sizeof(err_str));

    } else if (strstr(p, "vAttach;") == p) {
      p += strlen("vAttach;");
      char *end = NULL;
      pid_attaching_to = static_cast<int>(
          strtoul(p, &end, 16)); // PID will be in hex, so use base 16 to decode
      if (p != end && *end == '\0') {
        // Wait at most 30 second for attach
        struct timespec attach_timeout_abstime;
        DNBTimer::OffsetTimeOfDay(&attach_timeout_abstime, 30, 0);
        DNBLog("[LaunchAttach] START %d vAttach to pid %d", getpid(),
               pid_attaching_to);
        attach_pid = DNBProcessAttach(pid_attaching_to, &attach_timeout_abstime,
                                      m_ctx.GetIgnoredExceptions(), 
                                      err_str, sizeof(err_str));
      }
    } else {
      return HandlePacket_UNIMPLEMENTED(p);
    }

    if (attach_pid == INVALID_NUB_PROCESS_ARCH) {
      DNBLogError("debugserver is x86_64 binary running in translation, attach "
                  "failed.");
      return SendErrorPacket("E96", "debugserver is x86_64 binary running in "
                                    "translation, attach failed.");
    }

    if (attach_pid != INVALID_NUB_PROCESS) {
      if (m_ctx.ProcessID() != attach_pid)
        m_ctx.SetProcessID(attach_pid);
      DNBLog("Successfully attached to pid %d", attach_pid);
      // Send a stop reply packet to indicate we successfully attached!
      NotifyThatProcessStopped();
      return rnb_success;
    } else {
      DNBLogError("Attach failed");
      m_ctx.LaunchStatus().SetError(-1, DNBError::Generic);
      if (err_str[0])
        m_ctx.LaunchStatus().SetErrorString(err_str);
      else
        m_ctx.LaunchStatus().SetErrorString("attach failed");

      if (pid_attaching_to == INVALID_NUB_PROCESS && !attach_name.empty()) {
        pid_attaching_to = DNBProcessGetPIDByName(attach_name.c_str());
      }

      // attach_pid is INVALID_NUB_PROCESS - we did not succeed in attaching
      // if the original request, pid_attaching_to, is available, see if we
      // can figure out why we couldn't attach.  Return an informative error
      // string to lldb.

      if (pid_attaching_to != INVALID_NUB_PROCESS) {
        // The order of these checks is important.  
        if (process_does_not_exist (pid_attaching_to)) {
          DNBLogError("Tried to attach to pid that doesn't exist");
          return SendErrorPacket("E96", "no such process");
        }
        if (process_is_already_being_debugged (pid_attaching_to)) {
          DNBLogError("Tried to attach to process already being debugged");
          return SendErrorPacket("E96", "tried to attach to "
                                        "process already being debugged");
        }
        uid_t my_uid, process_uid;
        if (attach_failed_due_to_uid_mismatch (pid_attaching_to, 
                                               my_uid, process_uid)) {
          std::string my_username = "uid " + std::to_string (my_uid);
          std::string process_username = "uid " + std::to_string (process_uid);
          struct passwd *pw = getpwuid (my_uid);
          if (pw && pw->pw_name) {
            my_username = pw->pw_name;
          }
          pw = getpwuid (process_uid);
          if (pw && pw->pw_name) {
            process_username = pw->pw_name;
          }
          DNBLogError("Tried to attach to process with uid mismatch");
          std::string msg = "tried to attach to process as user '" +
                            my_username +
                            "' and process is running "
                            "as user '" +
                            process_username + "'";
          return SendErrorPacket("E96", msg);
        }
        if (!login_session_has_gui_access() && !developer_mode_enabled()) {
          DNBLogError("Developer mode is not enabled and this is a "
                      "non-interactive session");
          return SendErrorPacket("E96", "developer mode is "
                                        "not enabled on this machine "
                                        "and this is a non-interactive "
                                        "debug session.");
        }
        if (!login_session_has_gui_access()) {
          DNBLogError("This is a non-interactive session");
          return SendErrorPacket("E96", "this is a "
                                        "non-interactive debug session, "
                                        "cannot get permission to debug "
                                        "processes.");
        }
      }

      std::string error_explainer = "attach failed";
      if (err_str[0] != '\0') {
        // This is not a super helpful message for end users
        if (strcmp (err_str, "unable to start the exception thread") == 0) {
          snprintf (err_str, sizeof (err_str) - 1,
                    "Not allowed to attach to process.  Look in the console "
                    "messages (Console.app), near the debugserver entries, "
                    "when the attach failed.  The subsystem that denied "
                    "the attach permission will likely have logged an "
                    "informative message about why it was denied.");
          err_str[sizeof (err_str) - 1] = '\0';
        }
        error_explainer += " (";
        error_explainer += err_str;
        error_explainer += ")";
      }
      DNBLogError("Attach failed: \"%s\".", err_str);
      return SendErrorPacket("E96", error_explainer);
    }
  }

  // All other failures come through here
  return HandlePacket_UNIMPLEMENTED(p);
}

/* 'T XX' -- status of thread
 Check if the specified thread is alive.
 The thread number is in hex?  */

rnb_err_t RNBRemote::HandlePacket_T(const char *p) {
  p++;
  if (p == NULL || *p == '\0') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "No thread specified in T packet");
  }
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E15");
  }
  errno = 0;
  nub_thread_t tid = strtoul(p, NULL, 16);
  if (errno != 0 && tid == 0) {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Could not parse thread number in T packet");
  }

  nub_state_t state = DNBThreadGetState(m_ctx.ProcessID(), tid);
  if (state == eStateInvalid || state == eStateExited ||
      state == eStateCrashed) {
    return SendErrorPacket("E16");
  }

  return SendPacket("OK");
}

rnb_err_t RNBRemote::HandlePacket_z(const char *p) {
  if (p == NULL || *p == '\0')
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "No thread specified in z packet");

  if (!m_ctx.HasValidProcessID())
    return SendErrorPacket("E15");

  char packet_cmd = *p++;
  char break_type = *p++;

  if (*p++ != ',')
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Comma separator missing in z packet");

  char *c = NULL;
  nub_process_t pid = m_ctx.ProcessID();
  errno = 0;
  nub_addr_t addr = strtoull(p, &c, 16);
  if (errno != 0 && addr == 0)
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid address in z packet");
  p = c;
  if (*p++ != ',')
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Comma separator missing in z packet");

  errno = 0;
  auto byte_size = strtoul(p, &c, 16);
  if (errno != 0 && byte_size == 0)
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Invalid length in z packet");

  if (packet_cmd == 'Z') {
    // set
    switch (break_type) {
    case '0': // set software breakpoint
    case '1': // set hardware breakpoint
    {
      // gdb can send multiple Z packets for the same address and
      // these calls must be ref counted.
      bool hardware = (break_type == '1');

      if (DNBBreakpointSet(pid, addr, byte_size, hardware)) {
        // We successfully created a breakpoint, now lets full out
        // a ref count structure with the breakID and add it to our
        // map.
        return SendPacket("OK");
      } else {
        // We failed to set the software breakpoint
        return SendErrorPacket("E09");
      }
    } break;

    case '2': // set write watchpoint
    case '3': // set read watchpoint
    case '4': // set access watchpoint
    {
      bool hardware = true;
      uint32_t watch_flags = 0;
      if (break_type == '2')
        watch_flags = WATCH_TYPE_WRITE;
      else if (break_type == '3')
        watch_flags = WATCH_TYPE_READ;
      else
        watch_flags = WATCH_TYPE_READ | WATCH_TYPE_WRITE;

      if (DNBWatchpointSet(pid, addr, byte_size, watch_flags, hardware)) {
        return SendPacket("OK");
      } else {
        // We failed to set the watchpoint
        return SendErrorPacket("E09");
      }
    } break;

    default:
      break;
    }
  } else if (packet_cmd == 'z') {
    // remove
    switch (break_type) {
    case '0': // remove software breakpoint
    case '1': // remove hardware breakpoint
      if (DNBBreakpointClear(pid, addr)) {
        return SendPacket("OK");
      } else {
        return SendErrorPacket("E08");
      }
      break;

    case '2': // remove write watchpoint
    case '3': // remove read watchpoint
    case '4': // remove access watchpoint
      if (DNBWatchpointClear(pid, addr)) {
        return SendPacket("OK");
      } else {
        return SendErrorPacket("E08");
      }
      break;

    default:
      break;
    }
  }
  return HandlePacket_UNIMPLEMENTED(p);
}

// Extract the thread number from the thread suffix that might be appended to
// thread specific packets. This will only be enabled if
// m_thread_suffix_supported
// is true.
nub_thread_t RNBRemote::ExtractThreadIDFromThreadSuffix(const char *p) {
  if (m_thread_suffix_supported) {
    nub_thread_t tid = INVALID_NUB_THREAD;
    if (p) {
      const char *tid_cstr = strstr(p, "thread:");
      if (tid_cstr) {
        tid_cstr += strlen("thread:");
        tid = strtoul(tid_cstr, NULL, 16);
      }
    }
    return tid;
  }
  return GetCurrentThread();
}

/* 'p XX'
 print the contents of register X */

rnb_err_t RNBRemote::HandlePacket_p(const char *p) {
  if (g_num_reg_entries == 0)
    InitializeRegisters();

  if (p == NULL || *p == '\0') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "No thread specified in p packet");
  }
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E15");
  }
  nub_process_t pid = m_ctx.ProcessID();
  errno = 0;
  char *tid_cstr = NULL;
  uint32_t reg = static_cast<uint32_t>(strtoul(p + 1, &tid_cstr, 16));
  if (errno != 0 && reg == 0) {
    return HandlePacket_ILLFORMED(
        __FILE__, __LINE__, p, "Could not parse register number in p packet");
  }

  nub_thread_t tid = ExtractThreadIDFromThreadSuffix(tid_cstr);
  if (tid == INVALID_NUB_THREAD)
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "No thread specified in p packet");

  const register_map_entry_t *reg_entry;

  if (reg < g_num_reg_entries)
    reg_entry = &g_reg_entries[reg];
  else
    reg_entry = NULL;

  std::ostringstream ostrm;
  if (reg_entry == NULL) {
    DNBLogError(
        "RNBRemote::HandlePacket_p(%s): unknown register number %u requested\n",
        p, reg);
    ostrm << "00000000";
  } else if (reg_entry->nub_info.reg == (uint32_t)-1) {
    if (reg_entry->nub_info.size > 0) {
      std::vector<uint8_t> zeros(reg_entry->nub_info.size, '\0');
      append_hex_value(ostrm, zeros.data(), zeros.size(), false);
    }
  } else {
    register_value_in_hex_fixed_width(ostrm, pid, tid, reg_entry, NULL);
  }
  return SendPacket(ostrm.str());
}

/* 'Pnn=rrrrr'
 Set register number n to value r.
 n and r are hex strings.  */

rnb_err_t RNBRemote::HandlePacket_P(const char *p) {
  if (g_num_reg_entries == 0)
    InitializeRegisters();

  if (p == NULL || *p == '\0') {
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p, "Empty P packet");
  }
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E28");
  }

  nub_process_t pid = m_ctx.ProcessID();

  StdStringExtractor packet(p);

  const char cmd_char = packet.GetChar();
  // Register ID is always in big endian
  const uint32_t reg = packet.GetHexMaxU32(false, UINT32_MAX);
  const char equal_char = packet.GetChar();

  if (cmd_char != 'P')
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "Improperly formed P packet");

  if (reg == UINT32_MAX)
    return SendErrorPacket("E29");

  if (equal_char != '=')
    return SendErrorPacket("E30");

  const register_map_entry_t *reg_entry;

  if (reg >= g_num_reg_entries)
    return SendErrorPacket("E47");

  reg_entry = &g_reg_entries[reg];

  if (reg_entry->nub_info.set == (uint32_t)-1 &&
      reg_entry->nub_info.reg == (uint32_t)-1) {
    DNBLogError(
        "RNBRemote::HandlePacket_P(%s): unknown register number %u requested\n",
        p, reg);
    return SendErrorPacket("E48");
  }

  DNBRegisterValue reg_value;
  reg_value.info = reg_entry->nub_info;
  packet.GetHexBytes(reg_value.value.v_sint8, reg_entry->nub_info.size, 0xcc);

  nub_thread_t tid = ExtractThreadIDFromThreadSuffix(p);
  if (tid == INVALID_NUB_THREAD)
    return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                  "No thread specified in p packet");

  if (!DNBThreadSetRegisterValueByID(pid, tid, reg_entry->nub_info.set,
                                     reg_entry->nub_info.reg, &reg_value)) {
    return SendErrorPacket("E32");
  }
  return SendPacket("OK");
}

/* 'c [addr]'
 Continue, optionally from a specified address. */

rnb_err_t RNBRemote::HandlePacket_c(const char *p) {
  const nub_process_t pid = m_ctx.ProcessID();

  if (pid == INVALID_NUB_PROCESS)
    return SendErrorPacket("E23");

  DNBThreadResumeAction action = {INVALID_NUB_THREAD, eStateRunning, 0,
                                  INVALID_NUB_ADDRESS};

  if (*(p + 1) != '\0') {
    action.tid = GetContinueThread();
    errno = 0;
    action.addr = strtoull(p + 1, NULL, 16);
    if (errno != 0 && action.addr == 0)
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "Could not parse address in c packet");
  }

  DNBThreadResumeActions thread_actions;
  thread_actions.Append(action);
  thread_actions.SetDefaultThreadActionIfNeeded(eStateRunning, 0);
  if (!DNBProcessResume(pid, thread_actions.GetFirst(),
                        thread_actions.GetSize()))
    return SendErrorPacket("E25");
  // Don't send an "OK" packet; response is the stopped/exited message.
  return rnb_success;
}

rnb_err_t RNBRemote::HandlePacket_MemoryRegionInfo(const char *p) {
  /* This packet will find memory attributes (e.g. readable, writable,
     executable, stack, jitted code)
     for the memory region containing a given address and return that
     information.

     Users of this packet must be prepared for three results:

         Region information is returned
         Region information is unavailable for this address because the address
     is in unmapped memory
         Region lookup cannot be performed on this platform or process is not
     yet launched
         This packet isn't implemented

     Examples of use:
        qMemoryRegionInfo:3a55140
        start:3a50000,size:100000,permissions:rwx

        qMemoryRegionInfo:0
        error:address in unmapped region

        qMemoryRegionInfo:3a551140   (on a different platform)
        error:region lookup cannot be performed

        qMemoryRegionInfo
        OK                   // this packet is implemented by the remote nub
  */

  p += sizeof("qMemoryRegionInfo") - 1;
  if (*p == '\0')
    return SendPacket("OK");
  if (*p++ != ':')
    return SendErrorPacket("E67");
  if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X'))
    p += 2;

  errno = 0;
  uint64_t address = strtoul(p, NULL, 16);
  if (errno != 0 && address == 0) {
    return HandlePacket_ILLFORMED(
        __FILE__, __LINE__, p, "Invalid address in qMemoryRegionInfo packet");
  }

  DNBRegionInfo region_info;
  DNBProcessMemoryRegionInfo(m_ctx.ProcessID(), address, &region_info);
  std::ostringstream ostrm;

  // start:3a50000,size:100000,permissions:rwx
  ostrm << "start:" << std::hex << region_info.addr << ';';

  if (region_info.size > 0)
    ostrm << "size:" << std::hex << region_info.size << ';';

  if (region_info.permissions) {
    ostrm << "permissions:";

    if (region_info.permissions & eMemoryPermissionsReadable)
      ostrm << 'r';
    if (region_info.permissions & eMemoryPermissionsWritable)
      ostrm << 'w';
    if (region_info.permissions & eMemoryPermissionsExecutable)
      ostrm << 'x';
    ostrm << ';';

    ostrm << "dirty-pages:";
    if (region_info.dirty_pages.size() > 0) {
      bool first = true;
      for (nub_addr_t addr : region_info.dirty_pages) {
        if (!first)
          ostrm << ",";
        first = false;
        ostrm << std::hex << addr;
      }
    }
    ostrm << ";";
    if (!region_info.vm_types.empty()) {
      ostrm << "type:";
      for (size_t i = 0; i < region_info.vm_types.size(); i++) {
        if (i)
          ostrm << ",";
        ostrm << region_info.vm_types[i];
      }
      ostrm << ";";
    }
  }
  return SendPacket(ostrm.str());
}

// qGetProfileData;scan_type:0xYYYYYYY
rnb_err_t RNBRemote::HandlePacket_GetProfileData(const char *p) {
  nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendPacket("OK");

  StdStringExtractor packet(p += sizeof("qGetProfileData"));
  DNBProfileDataScanType scan_type = eProfileAll;
  std::string name;
  std::string value;
  while (packet.GetNameColonValue(name, value)) {
    if (name == "scan_type") {
      std::istringstream iss(value);
      uint32_t int_value = 0;
      if (iss >> std::hex >> int_value) {
        scan_type = (DNBProfileDataScanType)int_value;
      }
    }
  }

  std::string data = DNBProcessGetProfileData(pid, scan_type);
  if (!data.empty()) {
    return SendPacket(data);
  } else {
    return SendPacket("OK");
  }
}

// QSetEnableAsyncProfiling;enable:[0|1]:interval_usec:XXXXXX;scan_type:0xYYYYYYY
rnb_err_t RNBRemote::HandlePacket_SetEnableAsyncProfiling(const char *p) {
  nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendPacket("OK");

  StdStringExtractor packet(p += sizeof("QSetEnableAsyncProfiling"));
  bool enable = false;
  uint64_t interval_usec = 0;
  DNBProfileDataScanType scan_type = eProfileAll;
  std::string name;
  std::string value;
  while (packet.GetNameColonValue(name, value)) {
    if (name == "enable") {
      enable = strtoul(value.c_str(), NULL, 10) > 0;
    } else if (name == "interval_usec") {
      interval_usec = strtoul(value.c_str(), NULL, 10);
    } else if (name == "scan_type") {
      std::istringstream iss(value);
      uint32_t int_value = 0;
      if (iss >> std::hex >> int_value) {
        scan_type = (DNBProfileDataScanType)int_value;
      }
    }
  }

  if (interval_usec == 0) {
    enable = false;
  }

  DNBProcessSetEnableAsyncProfiling(pid, enable, interval_usec, scan_type);
  return SendPacket("OK");
}

// QEnableCompression:type:<COMPRESSION-TYPE>;
//
// type: must be a type previously reported by the qXfer:features:
// SupportedCompressions list

rnb_err_t RNBRemote::HandlePacket_QEnableCompression(const char *p) {
  p += sizeof("QEnableCompression:") - 1;

  if (strstr(p, "type:zlib-deflate;") != nullptr) {
    EnableCompressionNextSendPacket(compression_types::zlib_deflate);
    return SendPacket("OK");
  } else if (strstr(p, "type:lz4;") != nullptr) {
    EnableCompressionNextSendPacket(compression_types::lz4);
    return SendPacket("OK");
  } else if (strstr(p, "type:lzma;") != nullptr) {
    EnableCompressionNextSendPacket(compression_types::lzma);
    return SendPacket("OK");
  } else if (strstr(p, "type:lzfse;") != nullptr) {
    EnableCompressionNextSendPacket(compression_types::lzfse);
    return SendPacket("OK");
  }

  return SendErrorPacket("E88");
}

rnb_err_t RNBRemote::HandlePacket_qSpeedTest(const char *p) {
  p += strlen("qSpeedTest:response_size:");
  char *end = NULL;
  errno = 0;
  uint64_t response_size = ::strtoul(p, &end, 16);
  if (errno != 0)
    return HandlePacket_ILLFORMED(
        __FILE__, __LINE__, p,
        "Didn't find response_size value at right offset");
  else if (*end == ';') {
    static char g_data[4 * 1024 * 1024 + 16];
    strcpy(g_data, "data:");
    memset(g_data + 5, 'a', response_size);
    g_data[response_size + 5] = '\0';
    return SendPacket(g_data);
  } else {
    return SendErrorPacket("E79");
  }
}

rnb_err_t RNBRemote::HandlePacket_WatchpointSupportInfo(const char *p) {
  /* This packet simply returns the number of supported hardware watchpoints.

     Examples of use:
        qWatchpointSupportInfo:
        num:4

        qWatchpointSupportInfo
        OK                   // this packet is implemented by the remote nub
  */

  p += sizeof("qWatchpointSupportInfo") - 1;
  if (*p == '\0')
    return SendPacket("OK");
  if (*p++ != ':')
    return SendErrorPacket("E67");

  errno = 0;
  uint32_t num = DNBWatchpointGetNumSupportedHWP(m_ctx.ProcessID());
  std::ostringstream ostrm;

  // size:4
  ostrm << "num:" << std::dec << num << ';';
  return SendPacket(ostrm.str());
}

/* 'C sig [;addr]'
 Resume with signal sig, optionally at address addr.  */

rnb_err_t RNBRemote::HandlePacket_C(const char *p) {
  const nub_process_t pid = m_ctx.ProcessID();

  if (pid == INVALID_NUB_PROCESS)
    return SendErrorPacket("E36");

  DNBThreadResumeAction action = {INVALID_NUB_THREAD, eStateRunning, 0,
                                  INVALID_NUB_ADDRESS};
  int process_signo = -1;
  if (*(p + 1) != '\0') {
    action.tid = GetContinueThread();
    char *end = NULL;
    errno = 0;
    process_signo = static_cast<int>(strtoul(p + 1, &end, 16));
    if (errno != 0)
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "Could not parse signal in C packet");
    else if (*end == ';') {
      errno = 0;
      action.addr = strtoull(end + 1, NULL, 16);
      if (errno != 0 && action.addr == 0)
        return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                      "Could not parse address in C packet");
    }
  }

  DNBThreadResumeActions thread_actions;
  thread_actions.Append(action);
  thread_actions.SetDefaultThreadActionIfNeeded(eStateRunning, action.signal);
  if (!DNBProcessSignal(pid, process_signo))
    return SendErrorPacket("E52");
  if (!DNBProcessResume(pid, thread_actions.GetFirst(),
                        thread_actions.GetSize()))
    return SendErrorPacket("E38");
  /* Don't send an "OK" packet; response is the stopped/exited message.  */
  return rnb_success;
}

// 'D' packet
// Detach from gdb.
rnb_err_t RNBRemote::HandlePacket_D(const char *p) {
  if (m_ctx.HasValidProcessID()) {
    DNBLog("detaching from pid %u due to D packet", m_ctx.ProcessID());
    if (DNBProcessDetach(m_ctx.ProcessID()))
      SendPacket("OK");
    else {
      DNBLog("error while detaching from pid %u due to D packet",
             m_ctx.ProcessID());
      SendErrorPacket("E01");
    }
  } else {
    SendErrorPacket("E04");
  }
  return rnb_success;
}

/* 'k'
 Kill the inferior process.  */

rnb_err_t RNBRemote::HandlePacket_k(const char *p) {
  DNBLog("Got a 'k' packet, killing the inferior process.");
  // No response to should be sent to the kill packet
  if (m_ctx.HasValidProcessID())
    DNBProcessKill(m_ctx.ProcessID());
  SendPacket("X09");
  return rnb_success;
}

rnb_err_t RNBRemote::HandlePacket_stop_process(const char *p) {
//#define TEST_EXIT_ON_INTERRUPT // This should only be uncommented to test
//exiting on interrupt
#if defined(TEST_EXIT_ON_INTERRUPT)
  rnb_err_t err = HandlePacket_k(p);
  m_comm.Disconnect(true);
  return err;
#else
  if (!DNBProcessInterrupt(m_ctx.ProcessID())) {
    // If we failed to interrupt the process, then send a stop
    // reply packet as the process was probably already stopped
    DNBLogThreaded("RNBRemote::HandlePacket_stop_process() sending extra stop "
                   "reply because DNBProcessInterrupt returned false");
    HandlePacket_last_signal(NULL);
  }
  return rnb_success;
#endif
}

/* 's'
 Step the inferior process.  */

rnb_err_t RNBRemote::HandlePacket_s(const char *p) {
  const nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendErrorPacket("E32");

  // Hardware supported stepping not supported on arm
  nub_thread_t tid = GetContinueThread();
  if (tid == 0 || tid == (nub_thread_t)-1)
    tid = GetCurrentThread();

  if (tid == INVALID_NUB_THREAD)
    return SendErrorPacket("E33");

  DNBThreadResumeActions thread_actions;
  thread_actions.AppendAction(tid, eStateStepping);

  // Make all other threads stop when we are stepping
  thread_actions.SetDefaultThreadActionIfNeeded(eStateStopped, 0);
  if (!DNBProcessResume(pid, thread_actions.GetFirst(),
                        thread_actions.GetSize()))
    return SendErrorPacket("E49");
  // Don't send an "OK" packet; response is the stopped/exited message.
  return rnb_success;
}

/* 'S sig [;addr]'
 Step with signal sig, optionally at address addr.  */

rnb_err_t RNBRemote::HandlePacket_S(const char *p) {
  const nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendErrorPacket("E36");

  DNBThreadResumeAction action = {INVALID_NUB_THREAD, eStateStepping, 0,
                                  INVALID_NUB_ADDRESS};

  if (*(p + 1) != '\0') {
    char *end = NULL;
    errno = 0;
    action.signal = static_cast<int>(strtoul(p + 1, &end, 16));
    if (errno != 0)
      return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                    "Could not parse signal in S packet");
    else if (*end == ';') {
      errno = 0;
      action.addr = strtoull(end + 1, NULL, 16);
      if (errno != 0 && action.addr == 0) {
        return HandlePacket_ILLFORMED(__FILE__, __LINE__, p,
                                      "Could not parse address in S packet");
      }
    }
  }

  action.tid = GetContinueThread();
  if (action.tid == 0 || action.tid == (nub_thread_t)-1)
    return SendErrorPacket("E40");

  nub_state_t tstate = DNBThreadGetState(pid, action.tid);
  if (tstate == eStateInvalid || tstate == eStateExited)
    return SendErrorPacket("E37");

  DNBThreadResumeActions thread_actions;
  thread_actions.Append(action);

  // Make all other threads stop when we are stepping
  thread_actions.SetDefaultThreadActionIfNeeded(eStateStopped, 0);
  if (!DNBProcessResume(pid, thread_actions.GetFirst(),
                        thread_actions.GetSize()))
    return SendErrorPacket("E39");

  // Don't send an "OK" packet; response is the stopped/exited message.
  return rnb_success;
}

static const char *GetArchName(const uint32_t cputype,
                               const uint32_t cpusubtype) {
  switch (cputype) {
  case CPU_TYPE_ARM:
    switch (cpusubtype) {
    case 5:
      return "armv4";
    case 6:
      return "armv6";
    case 7:
      return "armv5t";
    case 8:
      return "xscale";
    case 9:
      return "armv7";
    case 10:
      return "armv7f";
    case 11:
      return "armv7s";
    case 12:
      return "armv7k";
    case 14:
      return "armv6m";
    case 15:
      return "armv7m";
    case 16:
      return "armv7em";
    default:
      return "arm";
    }
    break;
  case CPU_TYPE_ARM64:
    return "arm64";
  case CPU_TYPE_ARM64_32:
    return "arm64_32";
  case CPU_TYPE_I386:
    return "i386";
  case CPU_TYPE_X86_64:
    switch (cpusubtype) {
    default:
      return "x86_64";
    case 8:
      return "x86_64h";
    }
    break;
  }
  return NULL;
}

static bool GetHostCPUType(uint32_t &cputype, uint32_t &cpusubtype,
                           uint32_t &is_64_bit_capable, bool &promoted_to_64) {
  static uint32_t g_host_cputype = 0;
  static uint32_t g_host_cpusubtype = 0;
  static uint32_t g_is_64_bit_capable = 0;
  static bool g_promoted_to_64 = false;

  if (g_host_cputype == 0) {
    g_promoted_to_64 = false;
    size_t len = sizeof(uint32_t);
    if (::sysctlbyname("hw.cputype", &g_host_cputype, &len, NULL, 0) == 0) {
      len = sizeof(uint32_t);
      if (::sysctlbyname("hw.cpu64bit_capable", &g_is_64_bit_capable, &len,
                         NULL, 0) == 0) {
        if (g_is_64_bit_capable && ((g_host_cputype & CPU_ARCH_ABI64) == 0)) {
          g_promoted_to_64 = true;
          g_host_cputype |= CPU_ARCH_ABI64;
        }
      }
#if defined (TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
      if (g_host_cputype == CPU_TYPE_ARM64 && sizeof (void*) == 4)
        g_host_cputype = CPU_TYPE_ARM64_32;
#endif
    }

    len = sizeof(uint32_t);
    if (::sysctlbyname("hw.cpusubtype", &g_host_cpusubtype, &len, NULL, 0) ==
        0) {
      if (g_promoted_to_64 && g_host_cputype == CPU_TYPE_X86_64 &&
          g_host_cpusubtype == CPU_SUBTYPE_486)
        g_host_cpusubtype = CPU_SUBTYPE_X86_64_ALL;
    }
#if defined (TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
    // on arm64_32 devices, the machine's native cpu type is
    // CPU_TYPE_ARM64 and subtype is 2 indicating arm64e.
    // But we change the cputype to CPU_TYPE_ARM64_32 because
    // the user processes are all ILP32 processes today.
    // We also need to rewrite the cpusubtype so we vend 
    // a valid cputype + cpusubtype combination.
    if (g_host_cputype == CPU_TYPE_ARM64_32)
      g_host_cpusubtype = CPU_SUBTYPE_ARM64_32_V8;
#endif
  }

  cputype = g_host_cputype;
  cpusubtype = g_host_cpusubtype;
  is_64_bit_capable = g_is_64_bit_capable;
  promoted_to_64 = g_promoted_to_64;
  return g_host_cputype != 0;
}

rnb_err_t RNBRemote::HandlePacket_qHostInfo(const char *p) {
  std::ostringstream strm;

  uint32_t cputype = 0;
  uint32_t cpusubtype = 0;
  uint32_t is_64_bit_capable = 0;
  bool promoted_to_64 = false;
  if (GetHostCPUType(cputype, cpusubtype, is_64_bit_capable, promoted_to_64)) {
    strm << "cputype:" << std::dec << cputype << ';';
    strm << "cpusubtype:" << std::dec << cpusubtype << ';';
  }

  uint32_t addressing_bits = 0;
  if (DNBGetAddressingBits(addressing_bits)) {
    strm << "addressing_bits:" << std::dec << addressing_bits << ';';
  }

  // The OS in the triple should be "ios" or "macosx" which doesn't match our
  // "Darwin" which gets returned from "kern.ostype", so we need to hardcode
  // this for now.
  if (cputype == CPU_TYPE_ARM || cputype == CPU_TYPE_ARM64
      || cputype == CPU_TYPE_ARM64_32) {
#if defined(TARGET_OS_TV) && TARGET_OS_TV == 1
    strm << "ostype:tvos;";
#elif defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
    strm << "ostype:watchos;";
#elif defined(TARGET_OS_BRIDGE) && TARGET_OS_BRIDGE == 1
    strm << "ostype:bridgeos;";
#elif defined(TARGET_OS_OSX) && TARGET_OS_OSX == 1
    strm << "ostype:macosx;";
#elif defined(TARGET_OS_XR) && TARGET_OS_XR == 1
    strm << "ostype:xros;";
#else
    strm << "ostype:ios;";
#endif

    // On armv7 we use "synchronous" watchpoints which means the exception is
    // delivered before the instruction executes.
    strm << "watchpoint_exceptions_received:before;";
  } else {
    strm << "ostype:macosx;";
    strm << "watchpoint_exceptions_received:after;";
  }
  //    char ostype[64];
  //    len = sizeof(ostype);
  //    if (::sysctlbyname("kern.ostype", &ostype, &len, NULL, 0) == 0)
  //    {
  //        len = strlen(ostype);
  //        std::transform (ostype, ostype + len, ostype, tolower);
  //        strm << "ostype:" << std::dec << ostype << ';';
  //    }

  strm << "vendor:apple;";

  uint64_t major, minor, patch;
  if (DNBGetOSVersionNumbers(&major, &minor, &patch)) {
    strm << "os_version:" << major << "." << minor;
    if (patch != UINT64_MAX)
      strm << "." << patch;
    strm << ";";
  }

  std::string maccatalyst_version = DNBGetMacCatalystVersionString();
  if (!maccatalyst_version.empty() &&
      std::all_of(maccatalyst_version.begin(), maccatalyst_version.end(),
                  [](char c) { return (c >= '0' && c <= '9') || c == '.'; }))
    strm << "maccatalyst_version:" << maccatalyst_version << ";";

#if defined(__LITTLE_ENDIAN__)
  strm << "endian:little;";
#elif defined(__BIG_ENDIAN__)
  strm << "endian:big;";
#elif defined(__PDP_ENDIAN__)
  strm << "endian:pdp;";
#endif

  if (promoted_to_64)
    strm << "ptrsize:8;";
  else
    strm << "ptrsize:" << std::dec << sizeof(void *) << ';';

#if defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
  strm << "default_packet_timeout:10;";
#endif

  strm << "vm-page-size:" << std::dec << vm_page_size << ";";

  return SendPacket(strm.str());
}

void XMLElementStart(std::ostringstream &s, uint32_t indent, const char *name,
                     bool has_attributes) {
  if (indent)
    s << INDENT_WITH_SPACES(indent);
  s << '<' << name;
  if (!has_attributes)
    s << '>' << std::endl;
}

void XMLElementStartEndAttributes(std::ostringstream &s, bool empty) {
  if (empty)
    s << '/';
  s << '>' << std::endl;
}

void XMLElementEnd(std::ostringstream &s, uint32_t indent, const char *name) {
  if (indent)
    s << INDENT_WITH_SPACES(indent);
  s << '<' << '/' << name << '>' << std::endl;
}

void XMLElementWithStringValue(std::ostringstream &s, uint32_t indent,
                               const char *name, const char *value,
                               bool close = true) {
  if (value) {
    if (indent)
      s << INDENT_WITH_SPACES(indent);
    s << '<' << name << '>' << value;
    if (close)
      XMLElementEnd(s, 0, name);
  }
}

void XMLElementWithUnsignedValue(std::ostringstream &s, uint32_t indent,
                                 const char *name, uint64_t value,
                                 bool close = true) {
  if (indent)
    s << INDENT_WITH_SPACES(indent);

  s << '<' << name << '>' << DECIMAL << value;
  if (close)
    XMLElementEnd(s, 0, name);
}

void XMLAttributeString(std::ostringstream &s, const char *name,
                        const char *value, const char *default_value = NULL) {
  if (value) {
    if (default_value && strcmp(value, default_value) == 0)
      return; // No need to emit the attribute because it matches the default
              // value
    s << ' ' << name << "=\"" << value << "\"";
  }
}

void XMLAttributeUnsignedDecimal(std::ostringstream &s, const char *name,
                                 uint64_t value) {
  s << ' ' << name << "=\"" << DECIMAL << value << "\"";
}

void GenerateTargetXMLRegister(std::ostringstream &s, const uint32_t reg_num,
                               nub_size_t num_reg_sets,
                               const DNBRegisterSetInfo *reg_set_info,
                               const register_map_entry_t &reg) {
  const char *default_lldb_encoding = "uint";
  const char *lldb_encoding = default_lldb_encoding;
  const char *gdb_group = "general";
  const char *default_gdb_type = "int";
  const char *gdb_type = default_gdb_type;
  const char *default_lldb_format = "hex";
  const char *lldb_format = default_lldb_format;

  switch (reg.nub_info.type) {
  case Uint:
    lldb_encoding = "uint";
    break;
  case Sint:
    lldb_encoding = "sint";
    break;
  case IEEE754:
    lldb_encoding = "ieee754";
    if (reg.nub_info.set > 0)
      gdb_group = "float";
    break;
  case Vector:
    lldb_encoding = "vector";
    if (reg.nub_info.set > 0)
      gdb_group = "vector";
    break;
  }

  switch (reg.nub_info.format) {
  case Binary:
    lldb_format = "binary";
    break;
  case Decimal:
    lldb_format = "decimal";
    break;
  case Hex:
    lldb_format = "hex";
    break;
  case Float:
    gdb_type = "float";
    lldb_format = "float";
    break;
  case VectorOfSInt8:
    gdb_type = "float";
    lldb_format = "vector-sint8";
    break;
  case VectorOfUInt8:
    gdb_type = "float";
    lldb_format = "vector-uint8";
    break;
  case VectorOfSInt16:
    gdb_type = "float";
    lldb_format = "vector-sint16";
    break;
  case VectorOfUInt16:
    gdb_type = "float";
    lldb_format = "vector-uint16";
    break;
  case VectorOfSInt32:
    gdb_type = "float";
    lldb_format = "vector-sint32";
    break;
  case VectorOfUInt32:
    gdb_type = "float";
    lldb_format = "vector-uint32";
    break;
  case VectorOfFloat32:
    gdb_type = "float";
    lldb_format = "vector-float32";
    break;
  case VectorOfUInt128:
    gdb_type = "float";
    lldb_format = "vector-uint128";
    break;
  };

  uint32_t indent = 2;

  XMLElementStart(s, indent, "reg", true);
  XMLAttributeString(s, "name", reg.nub_info.name);
  XMLAttributeUnsignedDecimal(s, "regnum", reg_num);
  XMLAttributeUnsignedDecimal(s, "offset", reg.offset);
  XMLAttributeUnsignedDecimal(s, "bitsize", reg.nub_info.size * 8);
  XMLAttributeString(s, "group", gdb_group);
  XMLAttributeString(s, "type", gdb_type, default_gdb_type);
  XMLAttributeString(s, "altname", reg.nub_info.alt);
  XMLAttributeString(s, "encoding", lldb_encoding, default_lldb_encoding);
  XMLAttributeString(s, "format", lldb_format, default_lldb_format);
  XMLAttributeUnsignedDecimal(s, "group_id", reg.nub_info.set);
  if (reg.nub_info.reg_ehframe != INVALID_NUB_REGNUM)
    XMLAttributeUnsignedDecimal(s, "ehframe_regnum", reg.nub_info.reg_ehframe);
  if (reg.nub_info.reg_dwarf != INVALID_NUB_REGNUM)
    XMLAttributeUnsignedDecimal(s, "dwarf_regnum", reg.nub_info.reg_dwarf);

  const char *lldb_generic = NULL;
  switch (reg.nub_info.reg_generic) {
  case GENERIC_REGNUM_FP:
    lldb_generic = "fp";
    break;
  case GENERIC_REGNUM_PC:
    lldb_generic = "pc";
    break;
  case GENERIC_REGNUM_SP:
    lldb_generic = "sp";
    break;
  case GENERIC_REGNUM_RA:
    lldb_generic = "ra";
    break;
  case GENERIC_REGNUM_FLAGS:
    lldb_generic = "flags";
    break;
  case GENERIC_REGNUM_ARG1:
    lldb_generic = "arg1";
    break;
  case GENERIC_REGNUM_ARG2:
    lldb_generic = "arg2";
    break;
  case GENERIC_REGNUM_ARG3:
    lldb_generic = "arg3";
    break;
  case GENERIC_REGNUM_ARG4:
    lldb_generic = "arg4";
    break;
  case GENERIC_REGNUM_ARG5:
    lldb_generic = "arg5";
    break;
  case GENERIC_REGNUM_ARG6:
    lldb_generic = "arg6";
    break;
  case GENERIC_REGNUM_ARG7:
    lldb_generic = "arg7";
    break;
  case GENERIC_REGNUM_ARG8:
    lldb_generic = "arg8";
    break;
  default:
    break;
  }
  XMLAttributeString(s, "generic", lldb_generic);

  bool empty = reg.value_regnums.empty() && reg.invalidate_regnums.empty();
  if (!empty) {
    if (!reg.value_regnums.empty()) {
      std::ostringstream regnums;
      bool first = true;
      regnums << DECIMAL;
      for (auto regnum : reg.value_regnums) {
        if (!first)
          regnums << ',';
        regnums << regnum;
        first = false;
      }
      XMLAttributeString(s, "value_regnums", regnums.str().c_str());
    }

    if (!reg.invalidate_regnums.empty()) {
      std::ostringstream regnums;
      bool first = true;
      regnums << DECIMAL;
      for (auto regnum : reg.invalidate_regnums) {
        if (!first)
          regnums << ',';
        regnums << regnum;
        first = false;
      }
      XMLAttributeString(s, "invalidate_regnums", regnums.str().c_str());
    }
  }
  XMLElementStartEndAttributes(s, true);
}

void GenerateTargetXMLRegisters(std::ostringstream &s) {
  nub_size_t num_reg_sets = 0;
  const DNBRegisterSetInfo *reg_sets = DNBGetRegisterSetInfo(&num_reg_sets);

  uint32_t cputype = DNBGetRegisterCPUType();
  if (cputype) {
    XMLElementStart(s, 0, "feature", true);
    std::ostringstream name_strm;
    name_strm << "com.apple.debugserver." << GetArchName(cputype, 0);
    XMLAttributeString(s, "name", name_strm.str().c_str());
    XMLElementStartEndAttributes(s, false);
    for (uint32_t reg_num = 0; reg_num < g_num_reg_entries; ++reg_num)
    //        for (const auto &reg: g_dynamic_register_map)
    {
      GenerateTargetXMLRegister(s, reg_num, num_reg_sets, reg_sets,
                                g_reg_entries[reg_num]);
    }
    XMLElementEnd(s, 0, "feature");

    if (num_reg_sets > 0) {
      XMLElementStart(s, 0, "groups", false);
      for (uint32_t set = 1; set < num_reg_sets; ++set) {
        XMLElementStart(s, 2, "group", true);
        XMLAttributeUnsignedDecimal(s, "id", set);
        XMLAttributeString(s, "name", reg_sets[set].name);
        XMLElementStartEndAttributes(s, true);
      }
      XMLElementEnd(s, 0, "groups");
    }
  }
}

static const char *g_target_xml_header = R"(<?xml version="1.0"?>
<target version="1.0">)";

static const char *g_target_xml_footer = "</target>";

static std::string g_target_xml;

void UpdateTargetXML() {
  std::ostringstream s;
  s << g_target_xml_header << std::endl;

  // Set the architecture
  //
  // On raw targets (no OS, vendor info), I've seen replies like
  // <architecture>i386:x86-64</architecture> (for x86_64 systems - from vmware)
  // <architecture>arm</architecture> (for an unspecified arm device - from a Segger JLink)
  // For good interop, I'm not sure what's expected here.  e.g. will anyone understand
  // <architecture>x86_64</architecture> ? Or is i386:x86_64 the expected phrasing?
  //
  // s << "<architecture>" << arch "</architecture>" << std::endl;

  // Set the OSABI
  // s << "<osabi>abi-name</osabi>"

  GenerateTargetXMLRegisters(s);

  s << g_target_xml_footer << std::endl;

  // Save the XML output in case it gets retrieved in chunks
  g_target_xml = s.str();
}

rnb_err_t RNBRemote::HandlePacket_qXfer(const char *command) {
  const char *p = command;
  p += strlen("qXfer:");
  const char *sep = strchr(p, ':');
  if (sep) {
    std::string object(p, sep - p); // "auxv", "backtrace", "features", etc
    p = sep + 1;
    sep = strchr(p, ':');
    if (sep) {
      std::string rw(p, sep - p); // "read" or "write"
      p = sep + 1;
      sep = strchr(p, ':');
      if (sep) {
        std::string annex(p, sep - p); // "read" or "write"

        p = sep + 1;
        sep = strchr(p, ',');
        if (sep) {
          std::string offset_str(p, sep - p); // read the length as a string
          p = sep + 1;
          std::string length_str(p); // read the offset as a string
          char *end = nullptr;
          const uint64_t offset = strtoul(offset_str.c_str(), &end,
                                          16); // convert offset_str to a offset
          if (*end == '\0') {
            const uint64_t length = strtoul(
                length_str.c_str(), &end, 16); // convert length_str to a length
            if (*end == '\0') {
              if (object == "features" && rw == "read" &&
                  annex == "target.xml") {
                std::ostringstream xml_out;

                if (offset == 0) {
                  InitializeRegisters(true);

                  UpdateTargetXML();
                  if (g_target_xml.empty())
                    return SendErrorPacket("E83");

                  if (length > g_target_xml.size()) {
                    xml_out << 'l'; // No more data
                    xml_out << binary_encode_string(g_target_xml);
                  } else {
                    xml_out << 'm'; // More data needs to be read with a
                                    // subsequent call
                    xml_out << binary_encode_string(
                        std::string(g_target_xml, offset, length));
                  }
                } else {
                  // Retrieving target XML in chunks
                  if (offset < g_target_xml.size()) {
                    std::string chunk(g_target_xml, offset, length);
                    if (chunk.size() < length)
                      xml_out << 'l'; // No more data
                    else
                      xml_out << 'm'; // More data needs to be read with a
                                      // subsequent call
                    xml_out << binary_encode_string(chunk.data());
                  }
                }
                return SendPacket(xml_out.str());
              }
              // Well formed, put not supported
              return HandlePacket_UNIMPLEMENTED(command);
            }
          }
        }
      } else {
        SendErrorPacket("E85");
      }
    } else {
      SendErrorPacket("E86");
    }
  }
  return SendErrorPacket("E82");
}

rnb_err_t RNBRemote::HandlePacket_qGDBServerVersion(const char *p) {
  std::ostringstream strm;

#if defined(DEBUGSERVER_PROGRAM_NAME)
  strm << "name:" DEBUGSERVER_PROGRAM_NAME ";";
#else
  strm << "name:debugserver;";
#endif
  strm << "version:" << DEBUGSERVER_VERSION_NUM << ";";

  return SendPacket(strm.str());
}

rnb_err_t RNBRemote::HandlePacket_jGetDyldProcessState(const char *p) {
  const nub_process_t pid = m_ctx.ProcessID();
  if (pid == INVALID_NUB_PROCESS)
    return SendErrorPacket("E87");

  JSONGenerator::ObjectSP dyld_state_sp = DNBGetDyldProcessState(pid);
  if (dyld_state_sp) {
    std::ostringstream strm;
    dyld_state_sp->DumpBinaryEscaped(strm);
    dyld_state_sp->Clear();
    if (strm.str().size() > 0)
      return SendPacket(strm.str());
  }
  return SendErrorPacket("E88");
}

// A helper function that retrieves a single integer value from
// a one-level-deep JSON dictionary of key-value pairs.  e.g.
// jThreadExtendedInfo:{"plo_pthread_tsd_base_address_offset":0,"plo_pthread_tsd_base_offset":224,"plo_pthread_tsd_entry_size":8,"thread":144305}]
//
uint64_t get_integer_value_for_key_name_from_json(const char *key,
                                                  const char *json_string) {
  uint64_t retval = INVALID_NUB_ADDRESS;
  std::string key_with_quotes = "\"";
  key_with_quotes += key;
  key_with_quotes += "\"";
  const char *c = strstr(json_string, key_with_quotes.c_str());
  if (c) {
    c += key_with_quotes.size();

    while (*c != '\0' && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
      c++;

    if (*c == ':') {
      c++;

      while (*c != '\0' &&
             (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
        c++;

      errno = 0;
      retval = strtoul(c, NULL, 10);
      if (errno != 0) {
        retval = INVALID_NUB_ADDRESS;
      }
    }
  }
  return retval;
}

// A helper function that retrieves a boolean value from
// a one-level-deep JSON dictionary of key-value pairs.  e.g.
// jGetLoadedDynamicLibrariesInfos:{"fetch_all_solibs":true}]

// Returns true if it was able to find the key name, and sets the 'value'
// argument to the value found.

bool get_boolean_value_for_key_name_from_json(const char *key,
                                              const char *json_string,
                                              bool &value) {
  std::string key_with_quotes = "\"";
  key_with_quotes += key;
  key_with_quotes += "\"";
  const char *c = strstr(json_string, key_with_quotes.c_str());
  if (c) {
    c += key_with_quotes.size();

    while (*c != '\0' && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
      c++;

    if (*c == ':') {
      c++;

      while (*c != '\0' &&
             (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
        c++;

      if (strncmp(c, "true", 4) == 0) {
        value = true;
        return true;
      } else if (strncmp(c, "false", 5) == 0) {
        value = false;
        return true;
      }
    }
  }
  return false;
}

// A helper function that reads an array of uint64_t's from
// a one-level-deep JSON dictionary of key-value pairs.  e.g.
// jGetLoadedDynamicLibrariesInfos:{"solib_addrs":[31345823,7768020384,7310483024]}]

// Returns true if it was able to find the key name, false if it did not.
// "ints" will have all integers found in the array appended to it.

bool get_array_of_ints_value_for_key_name_from_json(
    const char *key, const char *json_string, std::vector<uint64_t> &ints) {
  std::string key_with_quotes = "\"";
  key_with_quotes += key;
  key_with_quotes += "\"";
  const char *c = strstr(json_string, key_with_quotes.c_str());
  if (c) {
    c += key_with_quotes.size();

    while (*c != '\0' && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
      c++;

    if (*c == ':') {
      c++;

      while (*c != '\0' &&
             (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
        c++;

      if (*c == '[') {
        c++;
        while (*c != '\0' &&
               (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
          c++;
        while (true) {
          if (!isdigit(*c)) {
            return true;
          }

          errno = 0;
          char *endptr;
          uint64_t value = strtoul(c, &endptr, 10);
          if (errno == 0) {
            ints.push_back(value);
          } else {
            break;
          }
          if (endptr == c || endptr == nullptr || *endptr == '\0') {
            break;
          }
          c = endptr;

          while (*c != '\0' &&
                 (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
            c++;
          if (*c == ',')
            c++;
          while (*c != '\0' &&
                 (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r'))
            c++;
          if (*c == ']') {
            return true;
          }
        }
      }
    }
  }
  return false;
}

JSONGenerator::ObjectSP
RNBRemote::GetJSONThreadsInfo(bool threads_with_valid_stop_info_only) {
  JSONGenerator::ArraySP threads_array_sp;
  if (m_ctx.HasValidProcessID()) {
    threads_array_sp = std::make_shared<JSONGenerator::Array>();

    nub_process_t pid = m_ctx.ProcessID();

    nub_size_t numthreads = DNBProcessGetNumThreads(pid);
    for (nub_size_t i = 0; i < numthreads; ++i) {
      nub_thread_t tid = DNBProcessGetThreadAtIndex(pid, i);

      struct DNBThreadStopInfo tid_stop_info;

      const bool stop_info_valid =
          DNBThreadGetStopReason(pid, tid, &tid_stop_info);

      // If we are doing stop info only, then we only show threads that have a
      // valid stop reason
      if (threads_with_valid_stop_info_only) {
        if (!stop_info_valid || tid_stop_info.reason == eStopTypeInvalid)
          continue;
      }

      JSONGenerator::DictionarySP thread_dict_sp(
          new JSONGenerator::Dictionary());
      thread_dict_sp->AddIntegerItem("tid", tid);

      std::string reason_value("none");

      if (stop_info_valid) {
        switch (tid_stop_info.reason) {
        case eStopTypeInvalid:
          break;

        case eStopTypeSignal:
          if (tid_stop_info.details.signal.signo != 0) {
            thread_dict_sp->AddIntegerItem("signal",
                                           tid_stop_info.details.signal.signo);
            reason_value = "signal";
          }
          break;

        case eStopTypeException:
          if (tid_stop_info.details.exception.type != 0) {
            reason_value = "exception";
            thread_dict_sp->AddIntegerItem(
                "metype", tid_stop_info.details.exception.type);
            JSONGenerator::ArraySP medata_array_sp(new JSONGenerator::Array());
            for (nub_size_t i = 0;
                 i < tid_stop_info.details.exception.data_count; ++i) {
              medata_array_sp->AddItem(
                  JSONGenerator::IntegerSP(new JSONGenerator::Integer(
                      tid_stop_info.details.exception.data[i])));
            }
            thread_dict_sp->AddItem("medata", medata_array_sp);
          }
          break;

        case eStopTypeWatchpoint: {
          reason_value = "watchpoint";
          thread_dict_sp->AddIntegerItem("watchpoint",
                                         tid_stop_info.details.watchpoint.addr);
          thread_dict_sp->AddIntegerItem(
              "me_watch_addr",
              tid_stop_info.details.watchpoint.mach_exception_addr);
          std::ostringstream wp_desc;
          wp_desc << tid_stop_info.details.watchpoint.addr << " ";
          wp_desc << tid_stop_info.details.watchpoint.hw_idx << " ";
          wp_desc << tid_stop_info.details.watchpoint.mach_exception_addr;
          thread_dict_sp->AddStringItem("description", wp_desc.str());
        } break;

        case eStopTypeExec:
          reason_value = "exec";
          break;
        }
      }

      thread_dict_sp->AddStringItem("reason", reason_value);

      if (!threads_with_valid_stop_info_only) {
        const char *thread_name = DNBThreadGetName(pid, tid);
        if (thread_name && thread_name[0])
          thread_dict_sp->AddStringItem("name", thread_name);

        thread_identifier_info_data_t thread_ident_info;
        if (DNBThreadGetIdentifierInfo(pid, tid, &thread_ident_info)) {
          if (thread_ident_info.dispatch_qaddr != 0) {
            thread_dict_sp->AddIntegerItem("qaddr",
                                           thread_ident_info.dispatch_qaddr);

            const DispatchQueueOffsets *dispatch_queue_offsets =
                GetDispatchQueueOffsets();
            if (dispatch_queue_offsets) {
              std::string queue_name;
              uint64_t queue_width = 0;
              uint64_t queue_serialnum = 0;
              nub_addr_t dispatch_queue_t = INVALID_NUB_ADDRESS;
              dispatch_queue_offsets->GetThreadQueueInfo(
                  pid, thread_ident_info.dispatch_qaddr, dispatch_queue_t,
                  queue_name, queue_width, queue_serialnum);
              if (dispatch_queue_t == 0 && queue_name.empty() &&
                  queue_serialnum == 0) {
                thread_dict_sp->AddBooleanItem("associated_with_dispatch_queue",
                                               false);
              } else {
                thread_dict_sp->AddBooleanItem("associated_with_dispatch_queue",
                                               true);
              }
              if (dispatch_queue_t != INVALID_NUB_ADDRESS &&
                  dispatch_queue_t != 0)
                thread_dict_sp->AddIntegerItem("dispatch_queue_t",
                                               dispatch_queue_t);
              if (!queue_name.empty())
                thread_dict_sp->AddStringItem("qname", queue_name);
              if (queue_width == 1)
                thread_dict_sp->AddStringItem("qkind", "serial");
              else if (queue_width > 1)
                thread_dict_sp->AddStringItem("qkind", "concurrent");
              if (queue_serialnum > 0)
                thread_dict_sp->AddIntegerItem("qserialnum", queue_serialnum);
            }
          }
        }

        DNBRegisterValue reg_value;

        if (g_reg_entries != NULL) {
          JSONGenerator::DictionarySP registers_dict_sp(
              new JSONGenerator::Dictionary());

          for (uint32_t reg = 0; reg < g_num_reg_entries; reg++) {
            // Expedite all registers in the first register set that aren't
            // contained in other registers
            if (g_reg_entries[reg].nub_info.set == 1 &&
                g_reg_entries[reg].nub_info.value_regs == NULL) {
              if (!DNBThreadGetRegisterValueByID(
                      pid, tid, g_reg_entries[reg].nub_info.set,
                      g_reg_entries[reg].nub_info.reg, &reg_value))
                continue;

              std::ostringstream reg_num;
              reg_num << std::dec << g_reg_entries[reg].debugserver_regnum;
              // Encode native byte ordered bytes as hex ascii
              registers_dict_sp->AddBytesAsHexASCIIString(
                  reg_num.str(), reg_value.value.v_uint8,
                  g_reg_entries[reg].nub_info.size);
            }
          }
          thread_dict_sp->AddItem("registers", registers_dict_sp);
        }

        // Add expedited stack memory so stack backtracing doesn't need to read
        // anything from the
        // frame pointer chain.
        StackMemoryMap stack_mmap;
        ReadStackMemory(pid, tid, stack_mmap);
        if (!stack_mmap.empty()) {
          JSONGenerator::ArraySP memory_array_sp(new JSONGenerator::Array());

          for (const auto &stack_memory : stack_mmap) {
            JSONGenerator::DictionarySP stack_memory_sp(
                new JSONGenerator::Dictionary());
            stack_memory_sp->AddIntegerItem("address", stack_memory.first);
            stack_memory_sp->AddBytesAsHexASCIIString(
                "bytes", stack_memory.second.bytes, stack_memory.second.length);
            memory_array_sp->AddItem(stack_memory_sp);
          }
          thread_dict_sp->AddItem("memory", memory_array_sp);
        }
      }

      threads_array_sp->AddItem(thread_dict_sp);
    }
  }
  return threads_array_sp;
}

rnb_err_t RNBRemote::HandlePacket_jThreadsInfo(const char *p) {
  JSONGenerator::ObjectSP threads_info_sp;
  std::ostringstream json;
  std::ostringstream reply_strm;
  // If we haven't run the process yet, return an error.
  if (m_ctx.HasValidProcessID()) {
    const bool threads_with_valid_stop_info_only = false;
    JSONGenerator::ObjectSP threads_info_sp =
        GetJSONThreadsInfo(threads_with_valid_stop_info_only);

    if (threads_info_sp) {
      std::ostringstream strm;
      threads_info_sp->DumpBinaryEscaped(strm);
      threads_info_sp->Clear();
      if (strm.str().size() > 0)
        return SendPacket(strm.str());
    }
  }
  return SendErrorPacket("E85");
}

rnb_err_t RNBRemote::HandlePacket_jThreadExtendedInfo(const char *p) {
  nub_process_t pid;
  std::ostringstream json;
  // If we haven't run the process yet, return an error.
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E81");
  }

  pid = m_ctx.ProcessID();

  const char thread_extended_info_str[] = {"jThreadExtendedInfo:{"};
  if (strncmp(p, thread_extended_info_str,
              sizeof(thread_extended_info_str) - 1) == 0) {
    p += strlen(thread_extended_info_str);

    uint64_t tid = get_integer_value_for_key_name_from_json("thread", p);
    uint64_t plo_pthread_tsd_base_address_offset =
        get_integer_value_for_key_name_from_json(
            "plo_pthread_tsd_base_address_offset", p);
    uint64_t plo_pthread_tsd_base_offset =
        get_integer_value_for_key_name_from_json("plo_pthread_tsd_base_offset",
                                                 p);
    uint64_t plo_pthread_tsd_entry_size =
        get_integer_value_for_key_name_from_json("plo_pthread_tsd_entry_size",
                                                 p);
    uint64_t dti_qos_class_index =
        get_integer_value_for_key_name_from_json("dti_qos_class_index", p);

    if (tid != INVALID_NUB_ADDRESS) {
      nub_addr_t pthread_t_value = DNBGetPThreadT(pid, tid);

      uint64_t tsd_address = INVALID_NUB_ADDRESS;
      if (plo_pthread_tsd_entry_size != INVALID_NUB_ADDRESS &&
          plo_pthread_tsd_base_offset != INVALID_NUB_ADDRESS &&
          plo_pthread_tsd_entry_size != INVALID_NUB_ADDRESS) {
        tsd_address = DNBGetTSDAddressForThread(
            pid, tid, plo_pthread_tsd_base_address_offset,
            plo_pthread_tsd_base_offset, plo_pthread_tsd_entry_size);
      }

      bool timed_out = false;
      Genealogy::ThreadActivitySP thread_activity_sp;

      // If the pthread_t value is invalid, or if we were able to fetch the
      // thread's TSD base
      // and got an invalid value back, then we have a thread in early startup
      // or shutdown and
      // it's possible that gathering the genealogy information for this thread
      // go badly.
      // Ideally fetching this info for a thread in these odd states shouldn't
      // matter - but
      // we've seen some problems with these new SPI and threads in edge-casey
      // states.

      double genealogy_fetch_time = 0;
      if (pthread_t_value != INVALID_NUB_ADDRESS &&
          tsd_address != INVALID_NUB_ADDRESS) {
        DNBTimer timer(false);
        thread_activity_sp = DNBGetGenealogyInfoForThread(pid, tid, timed_out);
        genealogy_fetch_time = timer.ElapsedMicroSeconds(false) / 1000000.0;
      }

      std::unordered_set<uint32_t>
          process_info_indexes; // an array of the process info #'s seen

      json << "{";

      bool need_to_print_comma = false;

      if (thread_activity_sp && !timed_out) {
        const Genealogy::Activity *activity =
            &thread_activity_sp->current_activity;
        bool need_vouchers_comma_sep = false;
        json << "\"activity_query_timed_out\":false,";
        if (genealogy_fetch_time != 0) {
          //  If we append the floating point value with << we'll get it in
          //  scientific
          //  notation.
          char floating_point_ascii_buffer[64];
          floating_point_ascii_buffer[0] = '\0';
          snprintf(floating_point_ascii_buffer,
                   sizeof(floating_point_ascii_buffer), "%f",
                   genealogy_fetch_time);
          if (strlen(floating_point_ascii_buffer) > 0) {
            if (need_to_print_comma)
              json << ",";
            need_to_print_comma = true;
            json << "\"activity_query_duration\":"
                 << floating_point_ascii_buffer;
          }
        }
        if (activity->activity_id != 0) {
          if (need_to_print_comma)
            json << ",";
          need_to_print_comma = true;
          need_vouchers_comma_sep = true;
          json << "\"activity\":{";
          json << "\"start\":" << activity->activity_start << ",";
          json << "\"id\":" << activity->activity_id << ",";
          json << "\"parent_id\":" << activity->parent_id << ",";
          json << "\"name\":\""
               << json_string_quote_metachars(activity->activity_name) << "\",";
          json << "\"reason\":\""
               << json_string_quote_metachars(activity->reason) << "\"";
          json << "}";
        }
        if (thread_activity_sp->messages.size() > 0) {
          need_to_print_comma = true;
          if (need_vouchers_comma_sep)
            json << ",";
          need_vouchers_comma_sep = true;
          json << "\"trace_messages\":[";
          bool printed_one_message = false;
          for (auto iter = thread_activity_sp->messages.begin();
               iter != thread_activity_sp->messages.end(); ++iter) {
            if (printed_one_message)
              json << ",";
            else
              printed_one_message = true;
            json << "{";
            json << "\"timestamp\":" << iter->timestamp << ",";
            json << "\"activity_id\":" << iter->activity_id << ",";
            json << "\"trace_id\":" << iter->trace_id << ",";
            json << "\"thread\":" << iter->thread << ",";
            json << "\"type\":" << (int)iter->type << ",";
            json << "\"process_info_index\":" << iter->process_info_index
                 << ",";
            process_info_indexes.insert(iter->process_info_index);
            json << "\"message\":\""
                 << json_string_quote_metachars(iter->message) << "\"";
            json << "}";
          }
          json << "]";
        }
        if (thread_activity_sp->breadcrumbs.size() == 1) {
          need_to_print_comma = true;
          if (need_vouchers_comma_sep)
            json << ",";
          need_vouchers_comma_sep = true;
          json << "\"breadcrumb\":{";
          for (auto iter = thread_activity_sp->breadcrumbs.begin();
               iter != thread_activity_sp->breadcrumbs.end(); ++iter) {
            json << "\"breadcrumb_id\":" << iter->breadcrumb_id << ",";
            json << "\"activity_id\":" << iter->activity_id << ",";
            json << "\"timestamp\":" << iter->timestamp << ",";
            json << "\"name\":\"" << json_string_quote_metachars(iter->name)
                 << "\"";
          }
          json << "}";
        }
        if (process_info_indexes.size() > 0) {
          need_to_print_comma = true;
          if (need_vouchers_comma_sep)
            json << ",";
          need_vouchers_comma_sep = true;
          bool printed_one_process_info = false;
          for (auto iter = process_info_indexes.begin();
               iter != process_info_indexes.end(); ++iter) {
            if (printed_one_process_info)
              json << ",";
            Genealogy::ProcessExecutableInfoSP image_info_sp;
            uint32_t idx = *iter;
            image_info_sp = DNBGetGenealogyImageInfo(pid, idx);
            if (image_info_sp) {
              if (!printed_one_process_info) {
                json << "\"process_infos\":[";
                printed_one_process_info = true;
              }

              json << "{";
              char uuid_buf[37];
              uuid_unparse_upper(image_info_sp->image_uuid, uuid_buf);
              json << "\"process_info_index\":" << idx << ",";
              json << "\"image_path\":\""
                   << json_string_quote_metachars(image_info_sp->image_path)
                   << "\",";
              json << "\"image_uuid\":\"" << uuid_buf << "\"";
              json << "}";
            }
          }
          if (printed_one_process_info)
            json << "]";
        }
      } else {
        if (timed_out) {
          if (need_to_print_comma)
            json << ",";
          need_to_print_comma = true;
          json << "\"activity_query_timed_out\":true";
          if (genealogy_fetch_time != 0) {
            //  If we append the floating point value with << we'll get it in
            //  scientific
            //  notation.
            char floating_point_ascii_buffer[64];
            floating_point_ascii_buffer[0] = '\0';
            snprintf(floating_point_ascii_buffer,
                     sizeof(floating_point_ascii_buffer), "%f",
                     genealogy_fetch_time);
            if (strlen(floating_point_ascii_buffer) > 0) {
              json << ",";
              json << "\"activity_query_duration\":"
                   << floating_point_ascii_buffer;
            }
          }
        }
      }

      if (tsd_address != INVALID_NUB_ADDRESS) {
        if (need_to_print_comma)
          json << ",";
        need_to_print_comma = true;
        json << "\"tsd_address\":" << tsd_address;

        if (dti_qos_class_index != 0 && dti_qos_class_index != UINT64_MAX) {
          ThreadInfo::QoS requested_qos = DNBGetRequestedQoSForThread(
              pid, tid, tsd_address, dti_qos_class_index);
          if (requested_qos.IsValid()) {
            if (need_to_print_comma)
              json << ",";
            need_to_print_comma = true;
            json << "\"requested_qos\":{";
            json << "\"enum_value\":" << requested_qos.enum_value << ",";
            json << "\"constant_name\":\""
                 << json_string_quote_metachars(requested_qos.constant_name)
                 << "\",";
            json << "\"printable_name\":\""
                 << json_string_quote_metachars(requested_qos.printable_name)
                 << "\"";
            json << "}";
          }
        }
      }

      if (pthread_t_value != INVALID_NUB_ADDRESS) {
        if (need_to_print_comma)
          json << ",";
        need_to_print_comma = true;
        json << "\"pthread_t\":" << pthread_t_value;
      }

      nub_addr_t dispatch_queue_t_value = DNBGetDispatchQueueT(pid, tid);
      if (dispatch_queue_t_value != INVALID_NUB_ADDRESS) {
        if (need_to_print_comma)
          json << ",";
        need_to_print_comma = true;
        json << "\"dispatch_queue_t\":" << dispatch_queue_t_value;
      }

      json << "}";
      std::string json_quoted = binary_encode_string(json.str());
      return SendPacket(json_quoted);
    }
  }
  return SendPacket("OK");
}

//  This packet may be called in one of two ways:
//
//  jGetLoadedDynamicLibrariesInfos:{"fetch_all_solibs":true}
//      Use the new dyld SPI to get a list of all the libraries loaded.
//      If "report_load_commands":false" is present, only the dyld SPI
//      provided information (load address, filepath) is returned.
//      lldb can ask for the mach-o header/load command details in a
//      separate packet.
//
//  jGetLoadedDynamicLibrariesInfos:{"solib_addresses":[8382824135,3258302053,830202858503]}
//      Use the dyld SPI and Mach-O parsing in memory to get the information
//      about the libraries loaded at these addresses.
//
rnb_err_t
RNBRemote::HandlePacket_jGetLoadedDynamicLibrariesInfos(const char *p) {
  nub_process_t pid;
  // If we haven't run the process yet, return an error.
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E83");
  }

  pid = m_ctx.ProcessID();

  const char get_loaded_dynamic_libraries_infos_str[] = {
      "jGetLoadedDynamicLibrariesInfos:{"};
  if (strncmp(p, get_loaded_dynamic_libraries_infos_str,
              sizeof(get_loaded_dynamic_libraries_infos_str) - 1) == 0) {
    p += strlen(get_loaded_dynamic_libraries_infos_str);

    JSONGenerator::ObjectSP json_sp;

    std::vector<uint64_t> macho_addresses;
    bool fetch_all_solibs = false;
    bool report_load_commands = true;
    get_boolean_value_for_key_name_from_json("report_load_commands", p,
                                             report_load_commands);

    if (get_boolean_value_for_key_name_from_json("fetch_all_solibs", p,
                                                 fetch_all_solibs) &&
        fetch_all_solibs) {
      json_sp = DNBGetAllLoadedLibrariesInfos(pid, report_load_commands);
    } else if (get_array_of_ints_value_for_key_name_from_json(
                   "solib_addresses", p, macho_addresses)) {
      json_sp = DNBGetLibrariesInfoForAddresses(pid, macho_addresses);
    }

    if (json_sp.get()) {
      std::ostringstream json_str;
      json_sp->DumpBinaryEscaped(json_str);
      json_sp->Clear();
      if (json_str.str().size() > 0) {
        return SendPacket(json_str.str());
      } else {
        SendErrorPacket("E84");
      }
    }
  }
  return SendPacket("OK");
}

// This packet does not currently take any arguments.  So the behavior is
//    jGetSharedCacheInfo:{}
//         send information about the inferior's shared cache
//    jGetSharedCacheInfo:
//         send "OK" to indicate that this packet is supported
rnb_err_t RNBRemote::HandlePacket_jGetSharedCacheInfo(const char *p) {
  nub_process_t pid;
  // If we haven't run the process yet, return an error.
  if (!m_ctx.HasValidProcessID()) {
    return SendErrorPacket("E85");
  }

  pid = m_ctx.ProcessID();

  const char get_shared_cache_info_str[] = {"jGetSharedCacheInfo:{"};
  if (strncmp(p, get_shared_cache_info_str,
              sizeof(get_shared_cache_info_str) - 1) == 0) {
    JSONGenerator::ObjectSP json_sp = DNBGetSharedCacheInfo(pid);

    if (json_sp.get()) {
      std::ostringstream json_str;
      json_sp->DumpBinaryEscaped(json_str);
      json_sp->Clear();
      if (json_str.str().size() > 0) {
        return SendPacket(json_str.str());
      } else {
        SendErrorPacket("E86");
      }
    }
  }
  return SendPacket("OK");
}

static bool MachHeaderIsMainExecutable(nub_process_t pid, uint32_t addr_size,
                                       nub_addr_t mach_header_addr,
                                       mach_header &mh) {
  DNBLogThreadedIf(LOG_RNB_PROC, "GetMachHeaderForMainExecutable(pid = %u, "
                                 "addr_size = %u, mach_header_addr = "
                                 "0x%16.16llx)",
                   pid, addr_size, mach_header_addr);
  const nub_size_t bytes_read =
      DNBProcessMemoryRead(pid, mach_header_addr, sizeof(mh), &mh);
  if (bytes_read == sizeof(mh)) {
    DNBLogThreadedIf(
        LOG_RNB_PROC, "GetMachHeaderForMainExecutable(pid = %u, addr_size = "
                      "%u, mach_header_addr = 0x%16.16llx): mh = {\n  magic = "
                      "0x%8.8x\n  cpu = 0x%8.8x\n  sub = 0x%8.8x\n  filetype = "
                      "%u\n  ncmds = %u\n  sizeofcmds = 0x%8.8x\n  flags = "
                      "0x%8.8x }",
        pid, addr_size, mach_header_addr, mh.magic, mh.cputype, mh.cpusubtype,
        mh.filetype, mh.ncmds, mh.sizeofcmds, mh.flags);
    if ((addr_size == 4 && mh.magic == MH_MAGIC) ||
        (addr_size == 8 && mh.magic == MH_MAGIC_64)) {
      if (mh.filetype == MH_EXECUTE) {
        DNBLogThreadedIf(LOG_RNB_PROC, "GetMachHeaderForMainExecutable(pid = "
                                       "%u, addr_size = %u, mach_header_addr = "
                                       "0x%16.16llx) -> this is the "
                                       "executable!!!",
                         pid, addr_size, mach_header_addr);
        return true;
      }
    }
  }
  return false;
}

static nub_addr_t GetMachHeaderForMainExecutable(const nub_process_t pid,
                                                 const uint32_t addr_size,
                                                 mach_header &mh) {
  struct AllImageInfos {
    uint32_t version;
    uint32_t dylib_info_count;
    uint64_t dylib_info_addr;
  };

  uint64_t mach_header_addr = 0;

  const nub_addr_t shlib_addr = DNBProcessGetSharedLibraryInfoAddress(pid);
  uint8_t bytes[256];
  nub_size_t bytes_read = 0;
  DNBDataRef data(bytes, sizeof(bytes), false);
  DNBDataRef::offset_t offset = 0;
  data.SetPointerSize(addr_size);

  // When we are sitting at __dyld_start, the kernel has placed the
  // address of the mach header of the main executable on the stack. If we
  // read the SP and dereference a pointer, we might find the mach header
  // for the executable. We also just make sure there is only 1 thread
  // since if we are at __dyld_start we shouldn't have multiple threads.
  if (DNBProcessGetNumThreads(pid) == 1) {
    nub_thread_t tid = DNBProcessGetThreadAtIndex(pid, 0);
    if (tid != INVALID_NUB_THREAD) {
      DNBRegisterValue sp_value;
      if (DNBThreadGetRegisterValueByID(pid, tid, REGISTER_SET_GENERIC,
                                        GENERIC_REGNUM_SP, &sp_value)) {
        uint64_t sp =
            addr_size == 8 ? sp_value.value.uint64 : sp_value.value.uint32;
        bytes_read = DNBProcessMemoryRead(pid, sp, addr_size, bytes);
        if (bytes_read == addr_size) {
          offset = 0;
          mach_header_addr = data.GetPointer(&offset);
          if (MachHeaderIsMainExecutable(pid, addr_size, mach_header_addr, mh))
            return mach_header_addr;
        }
      }
    }
  }

  // Check the dyld_all_image_info structure for a list of mach header
  // since it is a very easy thing to check
  if (shlib_addr != INVALID_NUB_ADDRESS) {
    bytes_read =
        DNBProcessMemoryRead(pid, shlib_addr, sizeof(AllImageInfos), bytes);
    if (bytes_read > 0) {
      AllImageInfos aii;
      offset = 0;
      aii.version = data.Get32(&offset);
      aii.dylib_info_count = data.Get32(&offset);
      if (aii.dylib_info_count > 0) {
        aii.dylib_info_addr = data.GetPointer(&offset);
        if (aii.dylib_info_addr != 0) {
          const size_t image_info_byte_size = 3 * addr_size;
          for (uint32_t i = 0; i < aii.dylib_info_count; ++i) {
            bytes_read = DNBProcessMemoryRead(pid, aii.dylib_info_addr +
                                                       i * image_info_byte_size,
                                              image_info_byte_size, bytes);
            if (bytes_read != image_info_byte_size)
              break;
            offset = 0;
            mach_header_addr = data.GetPointer(&offset);
            if (MachHeaderIsMainExecutable(pid, addr_size, mach_header_addr,
                                           mh))
              return mach_header_addr;
          }
        }
      }
    }
  }

  // We failed to find the executable's mach header from the all image
  // infos and by dereferencing the stack pointer. Now we fall back to
  // enumerating the memory regions and looking for regions that are
  // executable.
  DNBRegionInfo region_info;
  mach_header_addr = 0;
  while (DNBProcessMemoryRegionInfo(pid, mach_header_addr, &region_info)) {
    if (region_info.size == 0)
      break;

    if (region_info.permissions & eMemoryPermissionsExecutable) {
      DNBLogThreadedIf(
          LOG_RNB_PROC, "[0x%16.16llx - 0x%16.16llx) permissions = %c%c%c: "
                        "checking region for executable mach header",
          region_info.addr, region_info.addr + region_info.size,
          (region_info.permissions & eMemoryPermissionsReadable) ? 'r' : '-',
          (region_info.permissions & eMemoryPermissionsWritable) ? 'w' : '-',
          (region_info.permissions & eMemoryPermissionsExecutable) ? 'x' : '-');
      if (MachHeaderIsMainExecutable(pid, addr_size, mach_header_addr, mh))
        return mach_header_addr;
    } else {
      DNBLogThreadedIf(
          LOG_RNB_PROC,
          "[0x%16.16llx - 0x%16.16llx): permissions = %c%c%c: skipping region",
          region_info.addr, region_info.addr + region_info.size,
          (region_info.permissions & eMemoryPermissionsReadable) ? 'r' : '-',
          (region_info.permissions & eMemoryPermissionsWritable) ? 'w' : '-',
          (region_info.permissions & eMemoryPermissionsExecutable) ? 'x' : '-');
    }
    // Set the address to the next mapped region
    mach_header_addr = region_info.addr + region_info.size;
  }
  bzero(&mh, sizeof(mh));
  return INVALID_NUB_ADDRESS;
}

rnb_err_t RNBRemote::HandlePacket_qSymbol(const char *command) {
  const char *p = command;
  p += strlen("qSymbol:");
  const char *sep = strchr(p, ':');

  std::string symbol_name;
  std::string symbol_value_str;
  // Extract the symbol value if there is one
  if (sep > p)
    symbol_value_str.assign(p, sep - p);
  p = sep + 1;

  if (*p) {
    // We have a symbol name
    symbol_name = decode_hex_ascii_string(p);
    if (!symbol_value_str.empty()) {
      nub_addr_t symbol_value = decode_uint64(symbol_value_str.c_str(), 16);
      if (symbol_name == "dispatch_queue_offsets")
        m_dispatch_queue_offsets_addr = symbol_value;
    }
    ++m_qSymbol_index;
  } else {
    // No symbol name, set our symbol index to zero so we can
    // read any symbols that we need
    m_qSymbol_index = 0;
  }

  symbol_name.clear();

  if (m_qSymbol_index == 0) {
    if (m_dispatch_queue_offsets_addr == INVALID_NUB_ADDRESS)
      symbol_name = "dispatch_queue_offsets";
    else
      ++m_qSymbol_index;
  }

  //    // Lookup next symbol when we have one...
  //    if (m_qSymbol_index == 1)
  //    {
  //    }

  if (symbol_name.empty()) {
    // Done with symbol lookups
    return SendPacket("OK");
  } else {
    std::ostringstream reply;
    reply << "qSymbol:";
    for (size_t i = 0; i < symbol_name.size(); ++i)
      reply << RAWHEX8(symbol_name[i]);
    return SendPacket(reply.str());
  }
}

rnb_err_t RNBRemote::HandlePacket_QEnableErrorStrings(const char *p) {
  m_enable_error_strings = true;
  return SendPacket("OK");
}

static std::pair<cpu_type_t, cpu_subtype_t>
GetCPUTypesFromHost(nub_process_t pid) {
  cpu_type_t cputype = DNBProcessGetCPUType(pid);
  if (cputype == 0) {
    DNBLog("Unable to get the process cpu_type, making a best guess.");
    cputype = best_guess_cpu_type();
  }

  bool host_cpu_is_64bit = false;
  uint32_t is64bit_capable;
  size_t is64bit_capable_len = sizeof(is64bit_capable);
  if (sysctlbyname("hw.cpu64bit_capable", &is64bit_capable,
                   &is64bit_capable_len, NULL, 0) == 0)
    host_cpu_is_64bit = is64bit_capable != 0;

  uint32_t cpusubtype;
  size_t cpusubtype_len = sizeof(cpusubtype);
  if (::sysctlbyname("hw.cpusubtype", &cpusubtype, &cpusubtype_len, NULL, 0) ==
      0) {
    // If a process is CPU_TYPE_X86, then ignore the cpusubtype that we detected
    // from the host and use CPU_SUBTYPE_I386_ALL because we don't want the
    // CPU_SUBTYPE_X86_ARCH1 or CPU_SUBTYPE_X86_64_H to be used as the cpu
    // subtype
    // for i386...
    if (host_cpu_is_64bit) {
      if (cputype == CPU_TYPE_X86) {
        cpusubtype = 3; // CPU_SUBTYPE_I386_ALL
      } else if (cputype == CPU_TYPE_ARM) {
        // We can query a process' cputype but we cannot query a process'
        // cpusubtype.
        // If the process has cputype CPU_TYPE_ARM, then it is an armv7 (32-bit
        // process) and we
        // need to override the host cpusubtype (which is in the
        // CPU_SUBTYPE_ARM64 subtype namespace)
        // with a reasonable CPU_SUBTYPE_ARMV7 subtype.
        cpusubtype = 12; // CPU_SUBTYPE_ARM_V7K
      }
    }
#if defined (TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
    // on arm64_32 devices, the machine's native cpu type is
    // CPU_TYPE_ARM64 and subtype is 2 indicating arm64e.
    // But we change the cputype to CPU_TYPE_ARM64_32 because
    // the user processes are all ILP32 processes today.
    // We also need to rewrite the cpusubtype so we vend 
    // a valid cputype + cpusubtype combination.
    if (cputype == CPU_TYPE_ARM64_32 && cpusubtype == 2)
      cpusubtype = CPU_SUBTYPE_ARM64_32_V8;
#endif
  }

  return {cputype, cpusubtype};
}

// Note that all numeric values returned by qProcessInfo are hex encoded,
// including the pid and the cpu type.

rnb_err_t RNBRemote::HandlePacket_qProcessInfo(const char *p) {
  nub_process_t pid;
  std::ostringstream rep;

  // If we haven't run the process yet, return an error.
  if (!m_ctx.HasValidProcessID())
    return SendPacket("E68");

  pid = m_ctx.ProcessID();

  rep << "pid:" << std::hex << pid << ';';

  int procpid_mib[4];
  procpid_mib[0] = CTL_KERN;
  procpid_mib[1] = KERN_PROC;
  procpid_mib[2] = KERN_PROC_PID;
  procpid_mib[3] = pid;
  struct kinfo_proc proc_kinfo;
  size_t proc_kinfo_size = sizeof(struct kinfo_proc);

  if (::sysctl(procpid_mib, 4, &proc_kinfo, &proc_kinfo_size, NULL, 0) == 0) {
    if (proc_kinfo_size > 0) {
      rep << "parent-pid:" << std::hex << proc_kinfo.kp_eproc.e_ppid << ';';
      rep << "real-uid:" << std::hex << proc_kinfo.kp_eproc.e_pcred.p_ruid
          << ';';
      rep << "real-gid:" << std::hex << proc_kinfo.kp_eproc.e_pcred.p_rgid
          << ';';
      rep << "effective-uid:" << std::hex << proc_kinfo.kp_eproc.e_ucred.cr_uid
          << ';';
      if (proc_kinfo.kp_eproc.e_ucred.cr_ngroups > 0)
        rep << "effective-gid:" << std::hex
            << proc_kinfo.kp_eproc.e_ucred.cr_groups[0] << ';';
    }
  }

  cpu_type_t cputype;
  cpu_subtype_t cpusubtype;
  if (auto cputypes = DNBGetMainBinaryCPUTypes(pid))
    std::tie(cputype, cpusubtype) = *cputypes;
  else
    std::tie(cputype, cpusubtype) = GetCPUTypesFromHost(pid);

  uint32_t addr_size = 0;
  if (cputype != 0) {
    rep << "cputype:" << std::hex << cputype << ";";
    rep << "cpusubtype:" << std::hex << cpusubtype << ';';
    if (cputype & CPU_ARCH_ABI64)
      addr_size = 8;
    else
      addr_size = 4;
  }

  bool os_handled = false;
  if (addr_size > 0) {
    rep << "ptrsize:" << std::dec << addr_size << ';';
#if defined(TARGET_OS_OSX) && TARGET_OS_OSX == 1
    // Try and get the OS type by looking at the load commands in the main
    // executable and looking for a LC_VERSION_MIN load command. This is the
    // most reliable way to determine the "ostype" value when on desktop.

    mach_header mh;
    nub_addr_t exe_mach_header_addr =
        GetMachHeaderForMainExecutable(pid, addr_size, mh);
    if (exe_mach_header_addr != INVALID_NUB_ADDRESS) {
      uint64_t load_command_addr =
          exe_mach_header_addr +
          ((addr_size == 8) ? sizeof(mach_header_64) : sizeof(mach_header));
      load_command lc;
      for (uint32_t i = 0; i < mh.ncmds && !os_handled; ++i) {
        const nub_size_t bytes_read =
            DNBProcessMemoryRead(pid, load_command_addr, sizeof(lc), &lc);
        (void)bytes_read;

        bool is_executable = true;
        uint32_t major_version, minor_version, patch_version;
        std::optional<std::string> platform =
            DNBGetDeploymentInfo(pid, is_executable, lc, load_command_addr,
                                 major_version, minor_version, patch_version);
        if (platform) {
          os_handled = true;
          rep << "ostype:" << *platform << ";";
          break;
        }
        load_command_addr = load_command_addr + lc.cmdsize;
      }
    }
#endif // TARGET_OS_OSX
  }

  // If we weren't able to find the OS in a LC_VERSION_MIN load command, try
  // to set it correctly by using the cpu type and other tricks
  if (!os_handled) {
    // The OS in the triple should be "ios" or "macosx" which doesn't match our
    // "Darwin" which gets returned from "kern.ostype", so we need to hardcode
    // this for now.
    if (cputype == CPU_TYPE_ARM || cputype == CPU_TYPE_ARM64
        || cputype == CPU_TYPE_ARM64_32) {
#if defined(TARGET_OS_TV) && TARGET_OS_TV == 1
      rep << "ostype:tvos;";
#elif defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
      rep << "ostype:watchos;";
#elif defined(TARGET_OS_BRIDGE) && TARGET_OS_BRIDGE == 1
      rep << "ostype:bridgeos;";
#elif defined(TARGET_OS_OSX) && TARGET_OS_OSX == 1
      rep << "ostype:macosx;";
#else
      rep << "ostype:ios;";
#endif
    } else {
      bool is_ios_simulator = false;
      if (cputype == CPU_TYPE_X86 || cputype == CPU_TYPE_X86_64) {
        // Check for iOS simulator binaries by getting the process argument
        // and environment and checking for SIMULATOR_UDID in the environment
        int proc_args_mib[3] = {CTL_KERN, KERN_PROCARGS2, (int)pid};

        uint8_t arg_data[8192];
        size_t arg_data_size = sizeof(arg_data);
        if (::sysctl(proc_args_mib, 3, arg_data, &arg_data_size, NULL, 0) ==
            0) {
          DNBDataRef data(arg_data, arg_data_size, false);
          DNBDataRef::offset_t offset = 0;
          uint32_t argc = data.Get32(&offset);
          const char *cstr;

          cstr = data.GetCStr(&offset);
          if (cstr) {
            // Skip NULLs
            while (true) {
              const char *p = data.PeekCStr(offset);
              if ((p == NULL) || (*p != '\0'))
                break;
              ++offset;
            }
            // Now skip all arguments
            for (uint32_t i = 0; i < argc; ++i) {
              data.GetCStr(&offset);
            }

            // Now iterate across all environment variables
            while ((cstr = data.GetCStr(&offset))) {
              if (strncmp(cstr, "SIMULATOR_UDID=", strlen("SIMULATOR_UDID=")) ==
                  0) {
                is_ios_simulator = true;
                break;
              }
              if (cstr[0] == '\0')
                break;
            }
          }
        }
      }
      if (is_ios_simulator) {
#if defined(TARGET_OS_TV) && TARGET_OS_TV == 1
        rep << "ostype:tvos;";
#elif defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
        rep << "ostype:watchos;";
#elif defined(TARGET_OS_BRIDGE) && TARGET_OS_BRIDGE == 1
        rep << "ostype:bridgeos;";
#else
        rep << "ostype:ios;";
#endif
      } else {
        rep << "ostype:macosx;";
      }
    }
  }

  rep << "vendor:apple;";

#if defined(__LITTLE_ENDIAN__)
  rep << "endian:little;";
#elif defined(__BIG_ENDIAN__)
  rep << "endian:big;";
#elif defined(__PDP_ENDIAN__)
  rep << "endian:pdp;";
#endif

  if (addr_size == 0) {
#if (defined(__x86_64__) || defined(__i386__)) && defined(x86_THREAD_STATE)
    nub_thread_t thread = DNBProcessGetCurrentThreadMachPort(pid);
    kern_return_t kr;
    x86_thread_state_t gp_regs;
    mach_msg_type_number_t gp_count = x86_THREAD_STATE_COUNT;
    kr = thread_get_state(static_cast<thread_act_t>(thread), x86_THREAD_STATE,
                          (thread_state_t)&gp_regs, &gp_count);
    if (kr == KERN_SUCCESS) {
      if (gp_regs.tsh.flavor == x86_THREAD_STATE64)
        rep << "ptrsize:8;";
      else
        rep << "ptrsize:4;";
    }
#elif defined(__arm__)
    rep << "ptrsize:4;";
#elif (defined(__arm64__) || defined(__aarch64__)) &&                          \
    defined(ARM_UNIFIED_THREAD_STATE)
    nub_thread_t thread = DNBProcessGetCurrentThreadMachPort(pid);
    kern_return_t kr;
    arm_unified_thread_state_t gp_regs;
    mach_msg_type_number_t gp_count = ARM_UNIFIED_THREAD_STATE_COUNT;
    kr = thread_get_state(thread, ARM_UNIFIED_THREAD_STATE,
                          (thread_state_t)&gp_regs, &gp_count);
    if (kr == KERN_SUCCESS) {
      if (gp_regs.ash.flavor == ARM_THREAD_STATE64)
        rep << "ptrsize:8;";
      else
        rep << "ptrsize:4;";
    }
#endif
  }

  return SendPacket(rep.str());
}

const RNBRemote::DispatchQueueOffsets *RNBRemote::GetDispatchQueueOffsets() {
  if (!m_dispatch_queue_offsets.IsValid() &&
      m_dispatch_queue_offsets_addr != INVALID_NUB_ADDRESS &&
      m_ctx.HasValidProcessID()) {
    nub_process_t pid = m_ctx.ProcessID();
    nub_size_t bytes_read = DNBProcessMemoryRead(
        pid, m_dispatch_queue_offsets_addr, sizeof(m_dispatch_queue_offsets),
        &m_dispatch_queue_offsets);
    if (bytes_read != sizeof(m_dispatch_queue_offsets))
      m_dispatch_queue_offsets.Clear();
  }

  if (m_dispatch_queue_offsets.IsValid())
    return &m_dispatch_queue_offsets;
  else
    return nullptr;
}

void RNBRemote::EnableCompressionNextSendPacket(compression_types type) {
  m_compression_mode = type;
  m_enable_compression_next_send_packet = true;
}

compression_types RNBRemote::GetCompressionType() {
  // The first packet we send back to the debugger after a QEnableCompression
  // request
  // should be uncompressed -- so we can indicate whether the compression was
  // enabled
  // or not via OK / Enn returns.  After that, all packets sent will be using
  // the
  // compression protocol.

  if (m_enable_compression_next_send_packet) {
    // One time, we send back "None" as our compression type
    m_enable_compression_next_send_packet = false;
    return compression_types::none;
  }
  return m_compression_mode;
}
