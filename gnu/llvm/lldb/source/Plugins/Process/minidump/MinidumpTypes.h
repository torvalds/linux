//===-- MinidumpTypes.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_MINIDUMPTYPES_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_MINIDUMPTYPES_H

#include "lldb/Utility/Status.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Minidump.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include <optional>

// C includes
// C++ includes

// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms679293(v=vs.85).aspx
// https://chromium.googlesource.com/breakpad/breakpad/

namespace lldb_private {

namespace minidump {

using namespace llvm::minidump;

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

enum class CvSignature : uint32_t {
  Pdb70 = 0x53445352, // RSDS
  ElfBuildId = 0x4270454c, // BpEL (Breakpad/Crashpad minidumps)
};

enum class MinidumpMiscInfoFlags : uint32_t {
  ProcessID = (1 << 0),
  ProcessTimes = (1 << 1),
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ ProcessTimes)
};

template <typename T>
Status consumeObject(llvm::ArrayRef<uint8_t> &Buffer, const T *&Object) {
  Status error;
  if (Buffer.size() < sizeof(T)) {
    error.SetErrorString("Insufficient buffer!");
    return error;
  }

  Object = reinterpret_cast<const T *>(Buffer.data());
  Buffer = Buffer.drop_front(sizeof(T));
  return error;
}

struct MinidumpMemoryDescriptor64 {
  llvm::support::ulittle64_t start_of_memory_range;
  llvm::support::ulittle64_t data_size;

  static std::pair<llvm::ArrayRef<MinidumpMemoryDescriptor64>, uint64_t>
  ParseMemory64List(llvm::ArrayRef<uint8_t> &data);
};
static_assert(sizeof(MinidumpMemoryDescriptor64) == 16,
              "sizeof MinidumpMemoryDescriptor64 is not correct!");

// TODO misc2, misc3 ?
// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680389(v=vs.85).aspx
struct MinidumpMiscInfo {
  llvm::support::ulittle32_t size;
  // flags1 represents what info in the struct is valid
  llvm::support::ulittle32_t flags1;
  llvm::support::ulittle32_t process_id;
  llvm::support::ulittle32_t process_create_time;
  llvm::support::ulittle32_t process_user_time;
  llvm::support::ulittle32_t process_kernel_time;

  static const MinidumpMiscInfo *Parse(llvm::ArrayRef<uint8_t> &data);

  std::optional<lldb::pid_t> GetPid() const;
};
static_assert(sizeof(MinidumpMiscInfo) == 24,
              "sizeof MinidumpMiscInfo is not correct!");

// The /proc/pid/status is saved as an ascii string in the file
class LinuxProcStatus {
public:
  llvm::StringRef proc_status;
  lldb::pid_t pid;

  static std::optional<LinuxProcStatus> Parse(llvm::ArrayRef<uint8_t> &data);

  lldb::pid_t GetPid() const;

private:
  LinuxProcStatus() = default;
};

} // namespace minidump
} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_MINIDUMPTYPES_H
