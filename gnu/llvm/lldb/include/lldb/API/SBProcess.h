//===-- SBProcess.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBPROCESS_H
#define LLDB_API_SBPROCESS_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBProcessInfo.h"
#include "lldb/API/SBQueue.h"
#include "lldb/API/SBTarget.h"
#include <cstdio>

namespace lldb_private {
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class SBEvent;

class LLDB_API SBProcess {
public:
  /// Broadcaster event bits definitions.
  FLAGS_ANONYMOUS_ENUM(){eBroadcastBitStateChanged = (1 << 0),
                         eBroadcastBitInterrupt = (1 << 1),
                         eBroadcastBitSTDOUT = (1 << 2),
                         eBroadcastBitSTDERR = (1 << 3),
                         eBroadcastBitProfileData = (1 << 4),
                         eBroadcastBitStructuredData = (1 << 5)};

  SBProcess();

  SBProcess(const lldb::SBProcess &rhs);

  const lldb::SBProcess &operator=(const lldb::SBProcess &rhs);

  ~SBProcess();

  static const char *GetBroadcasterClassName();

  const char *GetPluginName();

  LLDB_DEPRECATED_FIXME("Use GetPluginName()", "GetPluginName()")
  const char *GetShortPluginName();

  void Clear();

  explicit operator bool() const;

  bool IsValid() const;

  lldb::SBTarget GetTarget() const;

  lldb::ByteOrder GetByteOrder() const;

  size_t PutSTDIN(const char *src, size_t src_len);

  size_t GetSTDOUT(char *dst, size_t dst_len) const;

  size_t GetSTDERR(char *dst, size_t dst_len) const;

  size_t GetAsyncProfileData(char *dst, size_t dst_len) const;

#ifndef SWIG
  void ReportEventState(const lldb::SBEvent &event, FILE *out) const;
#endif

  void ReportEventState(const lldb::SBEvent &event, SBFile file) const;

  void ReportEventState(const lldb::SBEvent &event, FileSP BORROWED) const;

  void AppendEventStateReport(const lldb::SBEvent &event,
                              lldb::SBCommandReturnObject &result);

  /// Remote connection related functions. These will fail if the
  /// process is not in eStateConnected. They are intended for use
  /// when connecting to an externally managed debugserver instance.
  bool RemoteAttachToProcessWithID(lldb::pid_t pid, lldb::SBError &error);

  bool RemoteLaunch(char const **argv, char const **envp,
                    const char *stdin_path, const char *stdout_path,
                    const char *stderr_path, const char *working_directory,
                    uint32_t launch_flags, bool stop_at_entry,
                    lldb::SBError &error);

  // Thread related functions
  uint32_t GetNumThreads();

  lldb::SBThread GetThreadAtIndex(size_t index);

  lldb::SBThread GetThreadByID(lldb::tid_t sb_thread_id);

  lldb::SBThread GetThreadByIndexID(uint32_t index_id);

  lldb::SBThread GetSelectedThread() const;

  // Function for lazily creating a thread using the current OS plug-in. This
  // function will be removed in the future when there are APIs to create
  // SBThread objects through the interface and add them to the process through
  // the SBProcess API.
  lldb::SBThread CreateOSPluginThread(lldb::tid_t tid, lldb::addr_t context);

  bool SetSelectedThread(const lldb::SBThread &thread);

  bool SetSelectedThreadByID(lldb::tid_t tid);

  bool SetSelectedThreadByIndexID(uint32_t index_id);

  // Queue related functions
  uint32_t GetNumQueues();

  lldb::SBQueue GetQueueAtIndex(size_t index);

  // Stepping related functions

  lldb::StateType GetState();

  int GetExitStatus();

  const char *GetExitDescription();

  /// Gets the process ID
  ///
  /// Returns the process identifier for the process as it is known
  /// on the system on which the process is running. For unix systems
  /// this is typically the same as if you called "getpid()" in the
  /// process.
  ///
  /// \return
  ///     Returns LLDB_INVALID_PROCESS_ID if this object does not
  ///     contain a valid process object, or if the process has not
  ///     been launched. Returns a valid process ID if the process is
  ///     valid.
  lldb::pid_t GetProcessID();

  /// Gets the unique ID associated with this process object
  ///
  /// Unique IDs start at 1 and increment up with each new process
  /// instance. Since starting a process on a system might always
  /// create a process with the same process ID, there needs to be a
  /// way to tell two process instances apart.
  ///
  /// \return
  ///     Returns a non-zero integer ID if this object contains a
  ///     valid process object, zero if this object does not contain
  ///     a valid process object.
  uint32_t GetUniqueID();

  uint32_t GetAddressByteSize() const;

  lldb::SBError Destroy();

  lldb::SBError Continue();

  lldb::SBError Stop();

  lldb::SBError Kill();

  lldb::SBError Detach();

  lldb::SBError Detach(bool keep_stopped);

  lldb::SBError Signal(int signal);

  lldb::SBUnixSignals GetUnixSignals();

  void SendAsyncInterrupt();

  uint32_t GetStopID(bool include_expression_stops = false);

  /// Gets the stop event corresponding to stop ID.
  //
  /// Note that it wasn't fully implemented and tracks only the stop
  /// event for the last natural stop ID.
  ///
  /// \param [in] stop_id
  ///   The ID of the stop event to return.
  ///
  /// \return
  ///   The stop event corresponding to stop ID.
  lldb::SBEvent GetStopEventForStopID(uint32_t stop_id);

  /// If the process is a scripted process, changes its state to the new state.
  /// No-op otherwise.
  ///
  /// \param [in] new_state
  ///   The new state that the scripted process should be set to.
  ///
  void ForceScriptedState(StateType new_state);

  size_t ReadMemory(addr_t addr, void *buf, size_t size, lldb::SBError &error);

  size_t WriteMemory(addr_t addr, const void *buf, size_t size,
                     lldb::SBError &error);

  size_t ReadCStringFromMemory(addr_t addr, void *char_buf, size_t size,
                               lldb::SBError &error);

  uint64_t ReadUnsignedFromMemory(addr_t addr, uint32_t byte_size,
                                  lldb::SBError &error);

  lldb::addr_t ReadPointerFromMemory(addr_t addr, lldb::SBError &error);

  lldb::SBAddressRangeList FindRangesInMemory(const void *buf, uint64_t size,
                                              const SBAddressRangeList &ranges,
                                              uint32_t alignment,
                                              uint32_t max_matches,
                                              SBError &error);

  lldb::addr_t FindInMemory(const void *buf, uint64_t size,
                            const SBAddressRange &range, uint32_t alignment,
                            SBError &error);

  // Events
  static lldb::StateType GetStateFromEvent(const lldb::SBEvent &event);

  static bool GetRestartedFromEvent(const lldb::SBEvent &event);

  static size_t GetNumRestartedReasonsFromEvent(const lldb::SBEvent &event);

  static const char *
  GetRestartedReasonAtIndexFromEvent(const lldb::SBEvent &event, size_t idx);

  static lldb::SBProcess GetProcessFromEvent(const lldb::SBEvent &event);

  static bool GetInterruptedFromEvent(const lldb::SBEvent &event);

  static lldb::SBStructuredData
  GetStructuredDataFromEvent(const lldb::SBEvent &event);

  static bool EventIsProcessEvent(const lldb::SBEvent &event);

  static bool EventIsStructuredDataEvent(const lldb::SBEvent &event);

  lldb::SBBroadcaster GetBroadcaster() const;

  static const char *GetBroadcasterClass();

  bool GetDescription(lldb::SBStream &description);

  SBStructuredData GetExtendedCrashInformation();

  uint32_t GetNumSupportedHardwareWatchpoints(lldb::SBError &error) const;

  /// Load a shared library into this process.
  ///
  /// \param[in] remote_image_spec
  ///     The path for the shared library on the target what you want
  ///     to load.
  ///
  /// \param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to load the shared library.
  ///
  /// \return
  ///     A token that represents the shared library that can be
  ///     later used to unload the shared library. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  uint32_t LoadImage(lldb::SBFileSpec &remote_image_spec, lldb::SBError &error);

  /// Load a shared library into this process.
  ///
  /// \param[in] local_image_spec
  ///     The file spec that points to the shared library that you
  ///     want to load if the library is located on the host. The
  ///     library will be copied over to the location specified by
  ///     remote_image_spec or into the current working directory with
  ///     the same filename if the remote_image_spec isn't specified.
  ///
  /// \param[in] remote_image_spec
  ///     If local_image_spec is specified then the location where the
  ///     library should be copied over from the host. If
  ///     local_image_spec isn't specified, then the path for the
  ///     shared library on the target what you want to load.
  ///
  /// \param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to load the shared library.
  ///
  /// \return
  ///     A token that represents the shared library that can be
  ///     later used to unload the shared library. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  uint32_t LoadImage(const lldb::SBFileSpec &local_image_spec,
                     const lldb::SBFileSpec &remote_image_spec,
                     lldb::SBError &error);

  /// Load a shared library into this process, starting with a
  /// library name and a list of paths, searching along the list of
  /// paths till you find a matching library.
  ///
  /// \param[in] image_spec
  ///     The name of the shared library that you want to load.
  ///     If image_spec is a relative path, the relative path will be
  ///     appended to the search paths.
  ///     If the image_spec is an absolute path, just the basename is used.
  ///
  /// \param[in] paths
  ///     A list of paths to search for the library whose basename is
  ///     local_spec.
  ///
  /// \param[out] loaded_path
  ///     If the library was found along the paths, this will store the
  ///     full path to the found library.
  ///
  /// \param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to search for the shared library.
  ///
  /// \return
  ///     A token that represents the shared library that can be
  ///     later passed to UnloadImage. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  uint32_t LoadImageUsingPaths(const lldb::SBFileSpec &image_spec,
                               SBStringList &paths,
                               lldb::SBFileSpec &loaded_path,
                               lldb::SBError &error);

  lldb::SBError UnloadImage(uint32_t image_token);

  lldb::SBError SendEventData(const char *data);

  /// Return the number of different thread-origin extended backtraces
  /// this process can support.
  ///
  /// When the process is stopped and you have an SBThread, lldb may be
  /// able to show a backtrace of when that thread was originally created,
  /// or the work item was enqueued to it (in the case of a libdispatch
  /// queue).
  ///
  /// \return
  ///   The number of thread-origin extended backtrace types that may be
  ///   available.
  uint32_t GetNumExtendedBacktraceTypes();

  /// Return the name of one of the thread-origin extended backtrace
  /// methods.
  ///
  /// \param [in] idx
  ///   The index of the name to return.  They will be returned in
  ///   the order that the user will most likely want to see them.
  ///   e.g. if the type at index 0 is not available for a thread,
  ///   see if the type at index 1 provides an extended backtrace.
  ///
  /// \return
  ///   The name at that index.
  const char *GetExtendedBacktraceTypeAtIndex(uint32_t idx);

  lldb::SBThreadCollection GetHistoryThreads(addr_t addr);

  bool IsInstrumentationRuntimePresent(InstrumentationRuntimeType type);

  /// Save the state of the process in a core file.
  ///
  /// \param[in] file_name - The name of the file to save the core file to.
  ///
  /// \param[in] flavor - Specify the flavor of a core file plug-in to save.
  /// Currently supported flavors include "mach-o" and "minidump"
  ///
  /// \param[in] core_style - Specify the style of a core file to save.
  lldb::SBError SaveCore(const char *file_name, const char *flavor,
                         SaveCoreStyle core_style);

  /// Save the state of the process with the a flavor that matches the
  /// current process' main executable (if supported).
  ///
  /// \param[in] file_name - The name of the file to save the core file to.
  lldb::SBError SaveCore(const char *file_name);

  /// Save the state of the process with the desired settings
  /// as defined in the options object.
  ///
  /// \param[in] options - The options to use when saving the core file.
  lldb::SBError SaveCore(SBSaveCoreOptions &options);

  /// Query the address load_addr and store the details of the memory
  /// region that contains it in the supplied SBMemoryRegionInfo object.
  /// To iterate over all memory regions use GetMemoryRegionList.
  ///
  /// \param[in] load_addr
  ///     The address to be queried.
  ///
  /// \param[out] region_info
  ///     A reference to an SBMemoryRegionInfo object that will contain
  ///     the details of the memory region containing load_addr.
  ///
  /// \return
  ///     An error object describes any errors that occurred while
  ///     querying load_addr.
  lldb::SBError GetMemoryRegionInfo(lldb::addr_t load_addr,
                                    lldb::SBMemoryRegionInfo &region_info);

  /// Return the list of memory regions within the process.
  ///
  /// \return
  ///     A list of all witin the process memory regions.
  lldb::SBMemoryRegionInfoList GetMemoryRegions();

  /// Return information about the process.
  ///
  /// Valid process info will only be returned when the process is
  /// alive, use SBProcessInfo::IsValid() to check returned info is
  /// valid.
  lldb::SBProcessInfo GetProcessInfo();

  /// Get the file specification for the core file that is currently being used
  /// for the process. If the process is not loaded from a core file, then an
  /// invalid file specification will be returned.
  ///
  /// \return
  ///     The path to the core file for this target or an invalid file spec if
  ///     the process isn't loaded from a core file.
  lldb::SBFileSpec GetCoreFile();

  /// \{
  /// \group Mask Address Methods
  ///
  /// \a type
  /// All of the methods in this group take \a type argument
  /// which is an AddressMaskType enum value.
  /// There can be different address masks for code addresses and
  /// data addresses, this argument can select which to get/set,
  /// or to use when clearing non-addressable bits from an address.
  /// This choice of mask can be important for example on AArch32
  /// systems. Where instructions where instructions start on even addresses,
  /// the 0th bit may be used to indicate that a function is thumb code.  On
  /// such a target, the eAddressMaskTypeCode may clear the 0th bit from an
  /// address to get the actual address Whereas eAddressMaskTypeData would not.
  ///
  /// \a addr_range
  /// Many of the methods in this group take an \a addr_range argument
  /// which is an AddressMaskRange enum value.
  /// Needing to specify the address range is highly unusual, and the
  /// default argument can be used in nearly all circumstances.
  /// On some architectures (e.g., AArch64), it is possible to have
  /// different page table setups for low and high memory, so different
  /// numbers of bits relevant to addressing. It is possible to have
  /// a program running in one half of memory and accessing the other
  /// as heap, so we need to maintain two different sets of address masks
  /// to debug this correctly.

  /// Get the current address mask that will be applied to addresses
  /// before reading from memory.
  ///
  /// \param[in] type
  ///     See \ref Mask Address Methods description of this argument.
  ///     eAddressMaskTypeAny is often a suitable value when code and
  ///     data masks are the same on a given target.
  ///
  /// \param[in] addr_range
  ///     See \ref Mask Address Methods description of this argument.
  ///     This will default to eAddressMaskRangeLow which is the
  ///     only set of masks used normally.
  ///
  /// \return
  ///     The address mask currently in use.  Bits which are not used
  ///     for addressing will be set to 1 in the mask.
  lldb::addr_t GetAddressMask(
      lldb::AddressMaskType type,
      lldb::AddressMaskRange addr_range = lldb::eAddressMaskRangeLow);

  /// Set the current address mask that can be applied to addresses
  /// before reading from memory.
  ///
  /// \param[in] type
  ///     See \ref Mask Address Methods description of this argument.
  ///     eAddressMaskTypeAll is often a suitable value when the
  ///     same mask is being set for both code and data.
  ///
  /// \param[in] mask
  ///     The address mask to set.  Bits which are not used for addressing
  ///     should be set to 1 in the mask.
  ///
  /// \param[in] addr_range
  ///     See \ref Mask Address Methods description of this argument.
  ///     This will default to eAddressMaskRangeLow which is the
  ///     only set of masks used normally.
  void SetAddressMask(
      lldb::AddressMaskType type, lldb::addr_t mask,
      lldb::AddressMaskRange addr_range = lldb::eAddressMaskRangeLow);

  /// Set the number of bits used for addressing in this Process.
  ///
  /// On Darwin and similar systems, the addressable bits are expressed
  /// as the number of low order bits that are relevant to addressing,
  /// instead of a more general address mask.
  /// This method calculates the correct mask value for a given number
  /// of low order addressable bits.
  ///
  /// \param[in] type
  ///     See \ref Mask Address Methods description of this argument.
  ///     eAddressMaskTypeAll is often a suitable value when the
  ///     same mask is being set for both code and data.
  ///
  /// \param[in] num_bits
  ///     Number of bits that are used for addressing.
  ///     For example, a value of 42 indicates that the low 42 bits
  ///     are relevant for addressing, and that higher-order bits may
  ///     be used for various metadata like pointer authentication,
  ///     Type Byte Ignore, etc.
  ///
  /// \param[in] addr_range
  ///     See \ref Mask Address Methods description of this argument.
  ///     This will default to eAddressMaskRangeLow which is the
  ///     only set of masks used normally.
  void
  SetAddressableBits(AddressMaskType type, uint32_t num_bits,
                     AddressMaskRange addr_range = lldb::eAddressMaskRangeLow);

  /// Clear the non-address bits of an \a addr value and return a
  /// virtual address in memory.
  ///
  /// Bits that are not used in addressing may be used for other purposes;
  /// pointer authentication, or metadata in the top byte, or the 0th bit
  /// of armv7 code addresses to indicate arm/thumb are common examples.
  ///
  /// \param[in] addr
  ///     The address that should be cleared of non-address bits.
  ///
  /// \param[in] type
  ///     See \ref Mask Address Methods description of this argument.
  ///     eAddressMaskTypeAny is the default value, correct when it
  ///     is unknown if the address is a code or data address.
  lldb::addr_t
  FixAddress(lldb::addr_t addr,
             lldb::AddressMaskType type = lldb::eAddressMaskTypeAny);
  /// \}

  /// Allocate memory within the process.
  ///
  /// This function will allocate memory in the process's address space.
  ///
  /// \param[in] size
  ///     The size of the allocation requested.
  ///
  /// \param[in] permissions
  ///     Or together any of the lldb::Permissions bits.  The
  ///     permissions on a given memory allocation can't be changed
  ///     after allocation.  Note that a block that isn't set writable
  ///     can still be written from lldb, just not by the process
  ///     itself.
  ///
  /// \param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying allocate.
  ///
  /// \return
  ///     The address of the allocated buffer in the process, or
  ///     LLDB_INVALID_ADDRESS if the allocation failed.
  lldb::addr_t AllocateMemory(size_t size, uint32_t permissions,
                              lldb::SBError &error);

  /// Deallocate memory in the process.
  ///
  /// This function will deallocate memory in the process's address
  /// space that was allocated with AllocateMemory.
  ///
  /// \param[in] ptr
  ///     A return value from AllocateMemory, pointing to the memory you
  ///     want to deallocate.
  ///
  /// \return
  ///     An error object describes any errors that occurred while
  ///     deallocating.
  ///
  lldb::SBError DeallocateMemory(lldb::addr_t ptr);

  lldb::SBScriptObject GetScriptedImplementation();

  void GetStatus(SBStream &status);

protected:
  friend class SBAddress;
  friend class SBBreakpoint;
  friend class SBBreakpointCallbackBaton;
  friend class SBBreakpointLocation;
  friend class SBCommandInterpreter;
  friend class SBDebugger;
  friend class SBExecutionContext;
  friend class SBFunction;
  friend class SBModule;
  friend class SBPlatform;
  friend class SBTarget;
  friend class SBThread;
  friend class SBValue;
  friend class lldb_private::QueueImpl;

  friend class lldb_private::python::SWIGBridge;

  SBProcess(const lldb::ProcessSP &process_sp);

  lldb::ProcessSP GetSP() const;

  void SetSP(const lldb::ProcessSP &process_sp);

  lldb::ProcessWP m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_API_SBPROCESS_H
