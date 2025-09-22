//===-- GDBRemoteCommunicationClient.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONCLIENT_H
#define LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONCLIENT_H

#include "GDBRemoteClientBase.h"

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lldb/Host/File.h"
#include "lldb/Utility/AddressableBits.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/GDBRemote.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/TraceGDBRemotePackets.h"
#include "lldb/Utility/UUID.h"
#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#endif

#include "llvm/Support/VersionTuple.h"

namespace lldb_private {
namespace process_gdb_remote {

/// The offsets used by the target when relocating the executable. Decoded from
/// qOffsets packet response.
struct QOffsets {
  /// If true, the offsets field describes segments. Otherwise, it describes
  /// sections.
  bool segments;

  /// The individual offsets. Section offsets have two or three members.
  /// Segment offsets have either one of two.
  std::vector<uint64_t> offsets;
};
inline bool operator==(const QOffsets &a, const QOffsets &b) {
  return a.segments == b.segments && a.offsets == b.offsets;
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const QOffsets &offsets);

// A trivial struct used to return a pair of PID and TID.
struct PidTid {
  uint64_t pid;
  uint64_t tid;
};

class GDBRemoteCommunicationClient : public GDBRemoteClientBase {
public:
  GDBRemoteCommunicationClient();

  ~GDBRemoteCommunicationClient() override;

  // After connecting, send the handshake to the server to make sure
  // we are communicating with it.
  bool HandshakeWithServer(Status *error_ptr);

  bool GetThreadSuffixSupported();

  // This packet is usually sent first and the boolean return value
  // indicates if the packet was send and any response was received
  // even in the response is UNIMPLEMENTED. If the packet failed to
  // get a response, then false is returned. This quickly tells us
  // if we were able to connect and communicate with the remote GDB
  // server
  bool QueryNoAckModeSupported();

  void GetListThreadsInStopReplySupported();

  lldb::pid_t GetCurrentProcessID(bool allow_lazy = true);

  bool LaunchGDBServer(const char *remote_accept_hostname, lldb::pid_t &pid,
                       uint16_t &port, std::string &socket_name);

  size_t QueryGDBServer(
      std::vector<std::pair<uint16_t, std::string>> &connection_urls);

  bool KillSpawnedProcess(lldb::pid_t pid);

  /// Launch the process using the provided arguments.
  ///
  /// \param[in] args
  ///     A list of program arguments. The first entry is the program being run.
  llvm::Error LaunchProcess(const Args &args);

  /// Sends a "QEnvironment:NAME=VALUE" packet that will build up the
  /// environment that will get used when launching an application
  /// in conjunction with the 'A' packet. This function can be called
  /// multiple times in a row in order to pass on the desired
  /// environment that the inferior should be launched with.
  ///
  /// \param[in] name_equal_value
  ///     A NULL terminated C string that contains a single environment
  ///     in the format "NAME=VALUE".
  ///
  /// \return
  ///     Zero if the response was "OK", a positive value if the
  ///     the response was "Exx" where xx are two hex digits, or
  ///     -1 if the call is unsupported or any other unexpected
  ///     response was received.
  int SendEnvironmentPacket(char const *name_equal_value);
  int SendEnvironment(const Environment &env);

  int SendLaunchArchPacket(const char *arch);

  int SendLaunchEventDataPacket(const char *data,
                                bool *was_supported = nullptr);

  /// Sends a GDB remote protocol 'I' packet that delivers stdin
  /// data to the remote process.
  ///
  /// \param[in] data
  ///     A pointer to stdin data.
  ///
  /// \param[in] data_len
  ///     The number of bytes available at \a data.
  ///
  /// \return
  ///     Zero if the attach was successful, or an error indicating
  ///     an error code.
  int SendStdinNotification(const char *data, size_t data_len);

  /// Sets the path to use for stdin/out/err for a process
  /// that will be launched with the 'A' packet.
  ///
  /// \param[in] file_spec
  ///     The path to use for stdin/out/err
  ///
  /// \return
  ///     Zero if the for success, or an error code for failure.
  int SetSTDIN(const FileSpec &file_spec);
  int SetSTDOUT(const FileSpec &file_spec);
  int SetSTDERR(const FileSpec &file_spec);

  /// Sets the disable ASLR flag to \a enable for a process that will
  /// be launched with the 'A' packet.
  ///
  /// \param[in] enable
  ///     A boolean value indicating whether to disable ASLR or not.
  ///
  /// \return
  ///     Zero if the for success, or an error code for failure.
  int SetDisableASLR(bool enable);

  /// Sets the DetachOnError flag to \a enable for the process controlled by the
  /// stub.
  ///
  /// \param[in] enable
  ///     A boolean value indicating whether to detach on error or not.
  ///
  /// \return
  ///     Zero if the for success, or an error code for failure.
  int SetDetachOnError(bool enable);

  /// Sets the working directory to \a path for a process that will
  /// be launched with the 'A' packet for non platform based
  /// connections. If this packet is sent to a GDB server that
  /// implements the platform, it will change the current working
  /// directory for the platform process.
  ///
  /// \param[in] working_dir
  ///     The path to a directory to use when launching our process
  ///
  /// \return
  ///     Zero if the for success, or an error code for failure.
  int SetWorkingDir(const FileSpec &working_dir);

  /// Gets the current working directory of a remote platform GDB
  /// server.
  ///
  /// \param[out] working_dir
  ///     The current working directory on the remote platform.
  ///
  /// \return
  ///     Boolean for success
  bool GetWorkingDir(FileSpec &working_dir);

  lldb::addr_t AllocateMemory(size_t size, uint32_t permissions);

  bool DeallocateMemory(lldb::addr_t addr);

  Status Detach(bool keep_stopped, lldb::pid_t pid = LLDB_INVALID_PROCESS_ID);

  Status GetMemoryRegionInfo(lldb::addr_t addr, MemoryRegionInfo &range_info);

  std::optional<uint32_t> GetWatchpointSlotCount();

  std::optional<bool> GetWatchpointReportedAfter();

  WatchpointHardwareFeature GetSupportedWatchpointTypes();

  const ArchSpec &GetHostArchitecture();

  std::chrono::seconds GetHostDefaultPacketTimeout();

  const ArchSpec &GetProcessArchitecture();

  bool GetProcessStandaloneBinary(UUID &uuid, lldb::addr_t &value,
                                  bool &value_is_offset);

  std::vector<lldb::addr_t> GetProcessStandaloneBinaries();

  void GetRemoteQSupported();

  bool GetVContSupported(char flavor);

  bool GetpPacketSupported(lldb::tid_t tid);

  bool GetxPacketSupported();

  bool GetVAttachOrWaitSupported();

  bool GetSyncThreadStateSupported();

  void ResetDiscoverableSettings(bool did_exec);

  bool GetHostInfo(bool force = false);

  bool GetDefaultThreadId(lldb::tid_t &tid);

  llvm::VersionTuple GetOSVersion();

  llvm::VersionTuple GetMacCatalystVersion();

  std::optional<std::string> GetOSBuildString();

  std::optional<std::string> GetOSKernelDescription();

  ArchSpec GetSystemArchitecture();

  lldb_private::AddressableBits GetAddressableBits();

  bool GetHostname(std::string &s);

  lldb::addr_t GetShlibInfoAddr();

  bool GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &process_info);

  uint32_t FindProcesses(const ProcessInstanceInfoMatch &process_match_info,
                         ProcessInstanceInfoList &process_infos);

  bool GetUserName(uint32_t uid, std::string &name);

  bool GetGroupName(uint32_t gid, std::string &name);

  bool HasFullVContSupport() { return GetVContSupported('A'); }

  bool HasAnyVContSupport() { return GetVContSupported('a'); }

  bool GetStopReply(StringExtractorGDBRemote &response);

  bool GetThreadStopInfo(lldb::tid_t tid, StringExtractorGDBRemote &response);

  bool SupportsGDBStoppointPacket(GDBStoppointType type) {
    switch (type) {
    case eBreakpointSoftware:
      return m_supports_z0;
    case eBreakpointHardware:
      return m_supports_z1;
    case eWatchpointWrite:
      return m_supports_z2;
    case eWatchpointRead:
      return m_supports_z3;
    case eWatchpointReadWrite:
      return m_supports_z4;
    default:
      return false;
    }
  }

  uint8_t SendGDBStoppointTypePacket(
      GDBStoppointType type, // Type of breakpoint or watchpoint
      bool insert,           // Insert or remove?
      lldb::addr_t addr,     // Address of breakpoint or watchpoint
      uint32_t length,       // Byte Size of breakpoint or watchpoint
      std::chrono::seconds interrupt_timeout); // Time to wait for an interrupt

  void TestPacketSpeed(const uint32_t num_packets, uint32_t max_send,
                       uint32_t max_recv, uint64_t recv_amount, bool json,
                       Stream &strm);

  // This packet is for testing the speed of the interface only. Both
  // the client and server need to support it, but this allows us to
  // measure the packet speed without any other work being done on the
  // other end and avoids any of that work affecting the packet send
  // and response times.
  bool SendSpeedTestPacket(uint32_t send_size, uint32_t recv_size);

  std::optional<PidTid> SendSetCurrentThreadPacket(uint64_t tid, uint64_t pid,
                                                   char op);

  bool SetCurrentThread(uint64_t tid,
                        lldb::pid_t pid = LLDB_INVALID_PROCESS_ID);

  bool SetCurrentThreadForRun(uint64_t tid,
                              lldb::pid_t pid = LLDB_INVALID_PROCESS_ID);

  bool GetQXferAuxvReadSupported();

  void EnableErrorStringInPacket();

  bool GetQXferLibrariesReadSupported();

  bool GetQXferLibrariesSVR4ReadSupported();

  uint64_t GetRemoteMaxPacketSize();

  bool GetEchoSupported();

  bool GetQPassSignalsSupported();

  bool GetAugmentedLibrariesSVR4ReadSupported();

  bool GetQXferFeaturesReadSupported();

  bool GetQXferMemoryMapReadSupported();

  bool GetQXferSigInfoReadSupported();

  bool GetMultiprocessSupported();

  LazyBool SupportsAllocDeallocMemory() // const
  {
    // Uncomment this to have lldb pretend the debug server doesn't respond to
    // alloc/dealloc memory packets.
    // m_supports_alloc_dealloc_memory = lldb_private::eLazyBoolNo;
    return m_supports_alloc_dealloc_memory;
  }

  std::vector<std::pair<lldb::pid_t, lldb::tid_t>>
  GetCurrentProcessAndThreadIDs(bool &sequence_mutex_unavailable);

  size_t GetCurrentThreadIDs(std::vector<lldb::tid_t> &thread_ids,
                             bool &sequence_mutex_unavailable);

  lldb::user_id_t OpenFile(const FileSpec &file_spec, File::OpenOptions flags,
                           mode_t mode, Status &error);

  bool CloseFile(lldb::user_id_t fd, Status &error);

  std::optional<GDBRemoteFStatData> FStat(lldb::user_id_t fd);

  // NB: this is just a convenience wrapper over open() + fstat().  It does not
  // work if the file cannot be opened.
  std::optional<GDBRemoteFStatData> Stat(const FileSpec &file_spec);

  lldb::user_id_t GetFileSize(const FileSpec &file_spec);

  void AutoCompleteDiskFileOrDirectory(CompletionRequest &request,
                                       bool only_dir);

  Status GetFilePermissions(const FileSpec &file_spec,
                            uint32_t &file_permissions);

  Status SetFilePermissions(const FileSpec &file_spec,
                            uint32_t file_permissions);

  uint64_t ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                    uint64_t dst_len, Status &error);

  uint64_t WriteFile(lldb::user_id_t fd, uint64_t offset, const void *src,
                     uint64_t src_len, Status &error);

  Status CreateSymlink(const FileSpec &src, const FileSpec &dst);

  Status Unlink(const FileSpec &file_spec);

  Status MakeDirectory(const FileSpec &file_spec, uint32_t mode);

  bool GetFileExists(const FileSpec &file_spec);

  Status RunShellCommand(
      llvm::StringRef command,
      const FileSpec &working_dir, // Pass empty FileSpec to use the current
                                   // working directory
      int *status_ptr, // Pass nullptr if you don't want the process exit status
      int *signo_ptr,  // Pass nullptr if you don't want the signal that caused
                       // the process to exit
      std::string
          *command_output, // Pass nullptr if you don't want the command output
      const Timeout<std::micro> &timeout);

  llvm::ErrorOr<llvm::MD5::MD5Result> CalculateMD5(const FileSpec &file_spec);

  lldb::DataBufferSP ReadRegister(
      lldb::tid_t tid,
      uint32_t
          reg_num); // Must be the eRegisterKindProcessPlugin register number

  lldb::DataBufferSP ReadAllRegisters(lldb::tid_t tid);

  bool
  WriteRegister(lldb::tid_t tid,
                uint32_t reg_num, // eRegisterKindProcessPlugin register number
                llvm::ArrayRef<uint8_t> data);

  bool WriteAllRegisters(lldb::tid_t tid, llvm::ArrayRef<uint8_t> data);

  bool SaveRegisterState(lldb::tid_t tid, uint32_t &save_id);

  bool RestoreRegisterState(lldb::tid_t tid, uint32_t save_id);

  bool SyncThreadState(lldb::tid_t tid);

  const char *GetGDBServerProgramName();

  uint32_t GetGDBServerProgramVersion();

  bool AvoidGPackets(ProcessGDBRemote *process);

  StructuredData::ObjectSP GetThreadsInfo();

  bool GetThreadExtendedInfoSupported();

  bool GetLoadedDynamicLibrariesInfosSupported();

  bool GetSharedCacheInfoSupported();

  bool GetDynamicLoaderProcessStateSupported();

  bool GetMemoryTaggingSupported();

  bool UsesNativeSignals();

  lldb::DataBufferSP ReadMemoryTags(lldb::addr_t addr, size_t len,
                                    int32_t type);

  Status WriteMemoryTags(lldb::addr_t addr, size_t len, int32_t type,
                         const std::vector<uint8_t> &tags);

  /// Use qOffsets to query the offset used when relocating the target
  /// executable. If successful, the returned structure will contain at least
  /// one value in the offsets field.
  std::optional<QOffsets> GetQOffsets();

  bool GetModuleInfo(const FileSpec &module_file_spec,
                     const ArchSpec &arch_spec, ModuleSpec &module_spec);

  std::optional<std::vector<ModuleSpec>>
  GetModulesInfo(llvm::ArrayRef<FileSpec> module_file_specs,
                 const llvm::Triple &triple);

  llvm::Expected<std::string> ReadExtFeature(llvm::StringRef object,
                                             llvm::StringRef annex);

  void ServeSymbolLookups(lldb_private::Process *process);

  // Sends QPassSignals packet to the server with given signals to ignore.
  Status SendSignalsToIgnore(llvm::ArrayRef<int32_t> signals);

  /// Return the feature set supported by the gdb-remote server.
  ///
  /// This method returns the remote side's response to the qSupported
  /// packet.  The response is the complete string payload returned
  /// to the client.
  ///
  /// \return
  ///     The string returned by the server to the qSupported query.
  const std::string &GetServerSupportedFeatures() const {
    return m_qSupported_response;
  }

  /// Return the array of async JSON packet types supported by the remote.
  ///
  /// This method returns the remote side's array of supported JSON
  /// packet types as a list of type names.  Each of the results are
  /// expected to have an Enable{type_name} command to enable and configure
  /// the related feature.  Each type_name for an enabled feature will
  /// possibly send async-style packets that contain a payload of a
  /// binhex-encoded JSON dictionary.  The dictionary will have a
  /// string field named 'type', that contains the type_name of the
  /// supported packet type.
  ///
  /// There is a Plugin category called structured-data plugins.
  /// A plugin indicates whether it knows how to handle a type_name.
  /// If so, it can be used to process the async JSON packet.
  ///
  /// \return
  ///     The string returned by the server to the qSupported query.
  lldb_private::StructuredData::Array *GetSupportedStructuredDataPlugins();

  /// Configure a StructuredData feature on the remote end.
  ///
  /// \see \b Process::ConfigureStructuredData(...) for details.
  Status
  ConfigureRemoteStructuredData(llvm::StringRef type_name,
                                const StructuredData::ObjectSP &config_sp);

  llvm::Expected<TraceSupportedResponse>
  SendTraceSupported(std::chrono::seconds interrupt_timeout);

  llvm::Error SendTraceStart(const llvm::json::Value &request,
                             std::chrono::seconds interrupt_timeout);

  llvm::Error SendTraceStop(const TraceStopRequest &request,
                            std::chrono::seconds interrupt_timeout);

  llvm::Expected<std::string>
  SendTraceGetState(llvm::StringRef type,
                    std::chrono::seconds interrupt_timeout);

  llvm::Expected<std::vector<uint8_t>>
  SendTraceGetBinaryData(const TraceGetBinaryDataRequest &request,
                         std::chrono::seconds interrupt_timeout);

  bool GetSaveCoreSupported() const;

  llvm::Expected<int> KillProcess(lldb::pid_t pid);

protected:
  LazyBool m_supports_not_sending_acks = eLazyBoolCalculate;
  LazyBool m_supports_thread_suffix = eLazyBoolCalculate;
  LazyBool m_supports_threads_in_stop_reply = eLazyBoolCalculate;
  LazyBool m_supports_vCont_all = eLazyBoolCalculate;
  LazyBool m_supports_vCont_any = eLazyBoolCalculate;
  LazyBool m_supports_vCont_c = eLazyBoolCalculate;
  LazyBool m_supports_vCont_C = eLazyBoolCalculate;
  LazyBool m_supports_vCont_s = eLazyBoolCalculate;
  LazyBool m_supports_vCont_S = eLazyBoolCalculate;
  LazyBool m_qHostInfo_is_valid = eLazyBoolCalculate;
  LazyBool m_curr_pid_is_valid = eLazyBoolCalculate;
  LazyBool m_qProcessInfo_is_valid = eLazyBoolCalculate;
  LazyBool m_qGDBServerVersion_is_valid = eLazyBoolCalculate;
  LazyBool m_supports_alloc_dealloc_memory = eLazyBoolCalculate;
  LazyBool m_supports_memory_region_info = eLazyBoolCalculate;
  LazyBool m_supports_watchpoint_support_info = eLazyBoolCalculate;
  LazyBool m_supports_detach_stay_stopped = eLazyBoolCalculate;
  LazyBool m_watchpoints_trigger_after_instruction = eLazyBoolCalculate;
  LazyBool m_attach_or_wait_reply = eLazyBoolCalculate;
  LazyBool m_prepare_for_reg_writing_reply = eLazyBoolCalculate;
  LazyBool m_supports_p = eLazyBoolCalculate;
  LazyBool m_supports_x = eLazyBoolCalculate;
  LazyBool m_avoid_g_packets = eLazyBoolCalculate;
  LazyBool m_supports_QSaveRegisterState = eLazyBoolCalculate;
  LazyBool m_supports_qXfer_auxv_read = eLazyBoolCalculate;
  LazyBool m_supports_qXfer_libraries_read = eLazyBoolCalculate;
  LazyBool m_supports_qXfer_libraries_svr4_read = eLazyBoolCalculate;
  LazyBool m_supports_qXfer_features_read = eLazyBoolCalculate;
  LazyBool m_supports_qXfer_memory_map_read = eLazyBoolCalculate;
  LazyBool m_supports_qXfer_siginfo_read = eLazyBoolCalculate;
  LazyBool m_supports_augmented_libraries_svr4_read = eLazyBoolCalculate;
  LazyBool m_supports_jThreadExtendedInfo = eLazyBoolCalculate;
  LazyBool m_supports_jLoadedDynamicLibrariesInfos = eLazyBoolCalculate;
  LazyBool m_supports_jGetSharedCacheInfo = eLazyBoolCalculate;
  LazyBool m_supports_jGetDyldProcessState = eLazyBoolCalculate;
  LazyBool m_supports_QPassSignals = eLazyBoolCalculate;
  LazyBool m_supports_error_string_reply = eLazyBoolCalculate;
  LazyBool m_supports_multiprocess = eLazyBoolCalculate;
  LazyBool m_supports_memory_tagging = eLazyBoolCalculate;
  LazyBool m_supports_qSaveCore = eLazyBoolCalculate;
  LazyBool m_uses_native_signals = eLazyBoolCalculate;

  bool m_supports_qProcessInfoPID : 1, m_supports_qfProcessInfo : 1,
      m_supports_qUserName : 1, m_supports_qGroupName : 1,
      m_supports_qThreadStopInfo : 1, m_supports_z0 : 1, m_supports_z1 : 1,
      m_supports_z2 : 1, m_supports_z3 : 1, m_supports_z4 : 1,
      m_supports_QEnvironment : 1, m_supports_QEnvironmentHexEncoded : 1,
      m_supports_qSymbol : 1, m_qSymbol_requests_done : 1,
      m_supports_qModuleInfo : 1, m_supports_jThreadsInfo : 1,
      m_supports_jModulesInfo : 1, m_supports_vFileSize : 1,
      m_supports_vFileMode : 1, m_supports_vFileExists : 1,
      m_supports_vRun : 1;

  /// Current gdb remote protocol process identifier for all other operations
  lldb::pid_t m_curr_pid = LLDB_INVALID_PROCESS_ID;
  /// Current gdb remote protocol process identifier for continue, step, etc
  lldb::pid_t m_curr_pid_run = LLDB_INVALID_PROCESS_ID;
  /// Current gdb remote protocol thread identifier for all other operations
  lldb::tid_t m_curr_tid = LLDB_INVALID_THREAD_ID;
  /// Current gdb remote protocol thread identifier for continue, step, etc
  lldb::tid_t m_curr_tid_run = LLDB_INVALID_THREAD_ID;

  uint32_t m_num_supported_hardware_watchpoints = 0;
  WatchpointHardwareFeature m_watchpoint_types =
      eWatchpointHardwareFeatureUnknown;
  uint32_t m_low_mem_addressing_bits = 0;
  uint32_t m_high_mem_addressing_bits = 0;

  ArchSpec m_host_arch;
  std::string m_host_distribution_id;
  ArchSpec m_process_arch;
  UUID m_process_standalone_uuid;
  lldb::addr_t m_process_standalone_value = LLDB_INVALID_ADDRESS;
  bool m_process_standalone_value_is_offset = false;
  std::vector<lldb::addr_t> m_binary_addresses;
  llvm::VersionTuple m_os_version;
  llvm::VersionTuple m_maccatalyst_version;
  std::string m_os_build;
  std::string m_os_kernel;
  std::string m_hostname;
  std::string m_gdb_server_name; // from reply to qGDBServerVersion, empty if
                                 // qGDBServerVersion is not supported
  uint32_t m_gdb_server_version =
      UINT32_MAX; // from reply to qGDBServerVersion, zero if
                  // qGDBServerVersion is not supported
  std::chrono::seconds m_default_packet_timeout;
  int m_target_vm_page_size = 0; // target system VM page size; 0 unspecified
  uint64_t m_max_packet_size = 0;    // as returned by qSupported
  std::string m_qSupported_response; // the complete response to qSupported

  bool m_supported_async_json_packets_is_valid = false;
  lldb_private::StructuredData::ObjectSP m_supported_async_json_packets_sp;

  std::vector<MemoryRegionInfo> m_qXfer_memory_map;
  bool m_qXfer_memory_map_loaded = false;

  bool GetCurrentProcessInfo(bool allow_lazy_pid = true);

  bool GetGDBServerVersion();

  // Given the list of compression types that the remote debug stub can support,
  // possibly enable compression if we find an encoding we can handle.
  void MaybeEnableCompression(
      llvm::ArrayRef<llvm::StringRef> supported_compressions);

  bool DecodeProcessInfoResponse(StringExtractorGDBRemote &response,
                                 ProcessInstanceInfo &process_info);

  void OnRunPacketSent(bool first) override;

  PacketResult SendThreadSpecificPacketAndWaitForResponse(
      lldb::tid_t tid, StreamString &&payload,
      StringExtractorGDBRemote &response);

  Status SendGetTraceDataPacket(StreamGDBRemote &packet, lldb::user_id_t uid,
                                lldb::tid_t thread_id,
                                llvm::MutableArrayRef<uint8_t> &buffer,
                                size_t offset);

  Status LoadQXferMemoryMap();

  Status GetQXferMemoryMapRegionInfo(lldb::addr_t addr,
                                     MemoryRegionInfo &region);

  LazyBool GetThreadPacketSupported(lldb::tid_t tid, llvm::StringRef packetStr);

private:
  GDBRemoteCommunicationClient(const GDBRemoteCommunicationClient &) = delete;
  const GDBRemoteCommunicationClient &
  operator=(const GDBRemoteCommunicationClient &) = delete;
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONCLIENT_H
