//===-- MinidumpFileBuilder.h ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Structure holding data neccessary for minidump file creation.
///
/// The class MinidumpFileWriter is used to hold the data that will eventually
/// be dumped to the file.
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_MINIDUMP_MINIDUMPFILEBUILDER_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_MINIDUMP_MINIDUMPFILEBUILDER_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <variant>

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "llvm/BinaryFormat/Minidump.h"
#include "llvm/Object/Minidump.h"

// Write std::string to minidump in the UTF16 format(with null termination char)
// with the size(without null termination char) preceding the UTF16 string.
// Empty strings are also printed with zero length and just null termination
// char.
lldb_private::Status WriteString(const std::string &to_write,
                                 lldb_private::DataBufferHeap *buffer);

/// \class MinidumpFileBuilder
/// Minidump writer for Linux
///
/// This class provides a Minidump writer that is able to
/// snapshot the current process state.
///
/// Minidumps are a Microsoft format for dumping process state.
/// This class constructs the minidump on disk starting with
/// Headers and Directories are written at the top of the file,
/// with the amount of bytes being precalculates before any writing takes place
/// Then the smaller data sections are written
/// SystemInfo, ModuleList, Misc Info.
/// Then Threads are emitted, threads are the first section that needs to be
/// 'fixed up' this happens when later we emit the memory stream, we identify if
/// that stream is the expected stack, and if so we update the stack with the
/// current RVA. Lastly the Memory lists are added. For Memory List, this will
/// contain everything that can fit within 4.2gb. MemoryList has it's
/// descriptors written at the end so it cannot be allowed to overflow.
///
/// Memory64List is a special case where it has to be begin before 4.2gb but can
/// expand forever The difference in Memory64List is there are no RVA's and all
/// the addresses are figured out by starting at the base RVA, and adding the
/// antecedent memory sections.
///
/// Because Memory64List can be arbitrarily large, this class has to write
/// chunks to disk this means we have to precalculate the descriptors and write
/// them first, and if we encounter any error, or are unable to read the same
/// number of bytes we have to go back and update them on disk.
///
/// And as the last step, after all the directories have been added, we go back
/// to the top of the file to fill in the header and the redirectory sections
/// that we preallocated.
class MinidumpFileBuilder {
public:
  MinidumpFileBuilder(lldb::FileUP &&core_file,
                      const lldb::ProcessSP &process_sp)
      : m_process_sp(process_sp), m_core_file(std::move(core_file)){};

  MinidumpFileBuilder(const MinidumpFileBuilder &) = delete;
  MinidumpFileBuilder &operator=(const MinidumpFileBuilder &) = delete;

  MinidumpFileBuilder(MinidumpFileBuilder &&other) = default;
  MinidumpFileBuilder &operator=(MinidumpFileBuilder &&other) = default;

  ~MinidumpFileBuilder() = default;

  // This method only calculates the amount of bytes the header and directories
  // will take up. It does not write the directories or headers. This function
  // must be called with a followup to fill in the data.
  lldb_private::Status AddHeaderAndCalculateDirectories();
  // Add SystemInfo stream, used for storing the most basic information
  // about the system, platform etc...
  lldb_private::Status AddSystemInfo();
  // Add ModuleList stream, containing information about all loaded modules
  // at the time of saving minidump.
  lldb_private::Status AddModuleList();
  // Add ThreadList stream, containing information about all threads running
  // at the moment of core saving. Contains information about thread
  // contexts.
  lldb_private::Status AddThreadList();
  // Add Exception streams for any threads that stopped with exceptions.
  lldb_private::Status AddExceptions();
  // Add MemoryList stream, containing dumps of important memory segments
  lldb_private::Status AddMemoryList(lldb::SaveCoreStyle core_style);
  // Add MiscInfo stream, mainly providing ProcessId
  lldb_private::Status AddMiscInfo();
  // Add informative files about a Linux process
  lldb_private::Status AddLinuxFileStreams();

  // Run cleanup and write all remaining bytes to file
  lldb_private::Status DumpFile();

private:
  // Add data to the end of the buffer, if the buffer exceeds the flush level,
  // trigger a flush.
  lldb_private::Status AddData(const void *data, uint64_t size);
  // Add MemoryList stream, containing dumps of important memory segments
  lldb_private::Status
  AddMemoryList_64(lldb_private::Process::CoreFileMemoryRanges &ranges);
  lldb_private::Status
  AddMemoryList_32(lldb_private::Process::CoreFileMemoryRanges &ranges);
  // Update the thread list on disk with the newly emitted stack RVAs.
  lldb_private::Status FixThreadStacks();
  lldb_private::Status FlushBufferToDisk();

  lldb_private::Status DumpHeader() const;
  lldb_private::Status DumpDirectories() const;
  // Add directory of StreamType pointing to the current end of the prepared
  // file with the specified size.
  lldb_private::Status AddDirectory(llvm::minidump::StreamType type,
                                    uint64_t stream_size);
  lldb::offset_t GetCurrentDataEndOffset() const;
  // Stores directories to fill in later
  std::vector<llvm::minidump::Directory> m_directories;
  // When we write off the threads for the first time, we need to clean them up
  // and give them the correct RVA once we write the stack memory list.
  // We save by the end because we only take from the stack pointer up
  // So the saved off range base can differ from the memory region the stack
  // pointer is in.
  std::unordered_map<lldb::addr_t, llvm::minidump::Thread>
      m_thread_by_range_end;
  // Main data buffer consisting of data without the minidump header and
  // directories
  lldb_private::DataBufferHeap m_data;
  lldb::ProcessSP m_process_sp;

  size_t m_expected_directories = 0;
  uint64_t m_saved_data_size = 0;
  lldb::offset_t m_thread_list_start = 0;
  // We set the max write amount to 128 mb, this is arbitrary
  // but we want to try to keep the size of m_data small
  // and we will only exceed a 128 mb buffer if we get a memory region
  // that is larger than 128 mb.
  static constexpr size_t MAX_WRITE_CHUNK_SIZE = (1024 * 1024 * 128);

  static constexpr size_t HEADER_SIZE = sizeof(llvm::minidump::Header);
  static constexpr size_t DIRECTORY_SIZE = sizeof(llvm::minidump::Directory);

  // More that one place can mention the register thread context locations,
  // so when we emit the thread contents, remember where it is so we don't have
  // to duplicate it in the exception data.
  std::unordered_map<lldb::tid_t, llvm::minidump::LocationDescriptor>
      m_tid_to_reg_ctx;
  lldb::FileUP m_core_file;
};

#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_MINIDUMP_MINIDUMPFILEBUILDER_H
