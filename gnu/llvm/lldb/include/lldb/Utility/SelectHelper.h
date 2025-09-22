//===-- SelectHelper.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_SELECTHELPER_H
#define LLDB_UTILITY_SELECTHELPER_H

#include "lldb/Utility/Status.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/DenseMap.h"

#include <chrono>
#include <optional>

class SelectHelper {
public:
  // Defaults to infinite wait for select unless you call SetTimeout()
  SelectHelper();

  // Call SetTimeout() before calling SelectHelper::Select() to set the timeout
  // based on the current time + the timeout. This allows multiple calls to
  // SelectHelper::Select() without having to worry about the absolute timeout
  // as this class manages to set the relative timeout correctly.
  void SetTimeout(const std::chrono::microseconds &timeout);

  // Call the FDSet*() functions before calling SelectHelper::Select() to set
  // the file descriptors that we will watch for when calling select. This will
  // cause FD_SET() to be called prior to calling select using the "fd"
  // provided.
  void FDSetRead(lldb::socket_t fd);
  void FDSetWrite(lldb::socket_t fd);
  void FDSetError(lldb::socket_t fd);

  // Call the FDIsSet*() functions after calling SelectHelper::Select() to
  // check which file descriptors are ready for read/write/error. This will
  // contain the result of FD_ISSET after calling select for a given file
  // descriptor.
  bool FDIsSetRead(lldb::socket_t fd) const;
  bool FDIsSetWrite(lldb::socket_t fd) const;
  bool FDIsSetError(lldb::socket_t fd) const;

  // Call the system's select() to wait for descriptors using timeout provided
  // in a call the SelectHelper::SetTimeout(), or infinite wait if no timeout
  // was set.
  lldb_private::Status Select();

protected:
  struct FDInfo {
    FDInfo()
        : read_set(false), write_set(false), error_set(false),
          read_is_set(false), write_is_set(false), error_is_set(false) {}

    void PrepareForSelect() {
      read_is_set = false;
      write_is_set = false;
      error_is_set = false;
    }

    bool read_set : 1, write_set : 1, error_set : 1, read_is_set : 1,
        write_is_set : 1, error_is_set : 1;
  };
  llvm::DenseMap<lldb::socket_t, FDInfo> m_fd_map;
  std::optional<std::chrono::steady_clock::time_point> m_end_time;
};

#endif // LLDB_UTILITY_SELECTHELPER_H
