//===-- SelectHelper.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__APPLE__)
// Enable this special support for Apple builds where we can have unlimited
// select bounds. We tried switching to poll() and kqueue and we were panicing
// the kernel, so we have to stick with select for now.
#define _DARWIN_UNLIMITED_SELECT
#endif

#include "lldb/Utility/SelectHelper.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/DenseMap.h"

#include <algorithm>
#include <chrono>
#include <optional>

#include <cerrno>
#if defined(_WIN32)
// Define NOMINMAX to avoid macros that conflict with std::min and std::max
#define NOMINMAX
#include <winsock2.h>
#else
#include <sys/time.h>
#include <sys/select.h>
#endif


SelectHelper::SelectHelper()
    : m_fd_map(), m_end_time() // Infinite timeout unless
                               // SelectHelper::SetTimeout() gets called
{}

void SelectHelper::SetTimeout(const std::chrono::microseconds &timeout) {
  using namespace std::chrono;
  m_end_time = steady_clock::time_point(steady_clock::now() + timeout);
}

void SelectHelper::FDSetRead(lldb::socket_t fd) {
  m_fd_map[fd].read_set = true;
}

void SelectHelper::FDSetWrite(lldb::socket_t fd) {
  m_fd_map[fd].write_set = true;
}

void SelectHelper::FDSetError(lldb::socket_t fd) {
  m_fd_map[fd].error_set = true;
}

bool SelectHelper::FDIsSetRead(lldb::socket_t fd) const {
  auto pos = m_fd_map.find(fd);
  if (pos != m_fd_map.end())
    return pos->second.read_is_set;
  else
    return false;
}

bool SelectHelper::FDIsSetWrite(lldb::socket_t fd) const {
  auto pos = m_fd_map.find(fd);
  if (pos != m_fd_map.end())
    return pos->second.write_is_set;
  else
    return false;
}

bool SelectHelper::FDIsSetError(lldb::socket_t fd) const {
  auto pos = m_fd_map.find(fd);
  if (pos != m_fd_map.end())
    return pos->second.error_is_set;
  else
    return false;
}

static void updateMaxFd(std::optional<lldb::socket_t> &vold,
                        lldb::socket_t vnew) {
  if (!vold)
    vold = vnew;
  else
    vold = std::max(*vold, vnew);
}

lldb_private::Status SelectHelper::Select() {
  lldb_private::Status error;
#ifdef _WIN32
  // On windows FD_SETSIZE limits the number of file descriptors, not their
  // numeric value.
  lldbassert(m_fd_map.size() <= FD_SETSIZE);
  if (m_fd_map.size() > FD_SETSIZE)
    return lldb_private::Status("Too many file descriptors for select()");
#endif

  std::optional<lldb::socket_t> max_read_fd;
  std::optional<lldb::socket_t> max_write_fd;
  std::optional<lldb::socket_t> max_error_fd;
  std::optional<lldb::socket_t> max_fd;
  for (auto &pair : m_fd_map) {
    pair.second.PrepareForSelect();
    const lldb::socket_t fd = pair.first;
#if !defined(__APPLE__) && !defined(_WIN32)
    lldbassert(fd < static_cast<int>(FD_SETSIZE));
    if (fd >= static_cast<int>(FD_SETSIZE)) {
      error.SetErrorStringWithFormat("%i is too large for select()", fd);
      return error;
    }
#endif
    if (pair.second.read_set)
      updateMaxFd(max_read_fd, fd);
    if (pair.second.write_set)
      updateMaxFd(max_write_fd, fd);
    if (pair.second.error_set)
      updateMaxFd(max_error_fd, fd);
    updateMaxFd(max_fd, fd);
  }

  if (!max_fd) {
    error.SetErrorString("no valid file descriptors");
    return error;
  }

  const unsigned nfds = static_cast<unsigned>(*max_fd) + 1;
  fd_set *read_fdset_ptr = nullptr;
  fd_set *write_fdset_ptr = nullptr;
  fd_set *error_fdset_ptr = nullptr;
// Initialize and zero out the fdsets
#if defined(__APPLE__)
  llvm::SmallVector<fd_set, 1> read_fdset;
  llvm::SmallVector<fd_set, 1> write_fdset;
  llvm::SmallVector<fd_set, 1> error_fdset;

  if (max_read_fd.has_value()) {
    read_fdset.resize((nfds / FD_SETSIZE) + 1);
    read_fdset_ptr = read_fdset.data();
  }
  if (max_write_fd.has_value()) {
    write_fdset.resize((nfds / FD_SETSIZE) + 1);
    write_fdset_ptr = write_fdset.data();
  }
  if (max_error_fd.has_value()) {
    error_fdset.resize((nfds / FD_SETSIZE) + 1);
    error_fdset_ptr = error_fdset.data();
  }
  for (auto &fd_set : read_fdset)
    FD_ZERO(&fd_set);
  for (auto &fd_set : write_fdset)
    FD_ZERO(&fd_set);
  for (auto &fd_set : error_fdset)
    FD_ZERO(&fd_set);
#else
  fd_set read_fdset;
  fd_set write_fdset;
  fd_set error_fdset;

  if (max_read_fd) {
    FD_ZERO(&read_fdset);
    read_fdset_ptr = &read_fdset;
  }
  if (max_write_fd) {
    FD_ZERO(&write_fdset);
    write_fdset_ptr = &write_fdset;
  }
  if (max_error_fd) {
    FD_ZERO(&error_fdset);
    error_fdset_ptr = &error_fdset;
  }
#endif
  // Set the FD bits in the fdsets for read/write/error
  for (auto &pair : m_fd_map) {
    const lldb::socket_t fd = pair.first;

    if (pair.second.read_set)
      FD_SET(fd, read_fdset_ptr);

    if (pair.second.write_set)
      FD_SET(fd, write_fdset_ptr);

    if (pair.second.error_set)
      FD_SET(fd, error_fdset_ptr);
  }

  // Setup our timeout time value if needed
  struct timeval *tv_ptr = nullptr;
  struct timeval tv = {0, 0};

  while (true) {
    using namespace std::chrono;
    // Setup out relative timeout based on the end time if we have one
    if (m_end_time) {
      tv_ptr = &tv;
      const auto remaining_dur =
          duration_cast<microseconds>(*m_end_time - steady_clock::now());
      if (remaining_dur.count() > 0) {
        // Wait for a specific amount of time
        const auto dur_secs = duration_cast<seconds>(remaining_dur);
        const auto dur_usecs = remaining_dur % seconds(1);
        tv.tv_sec = dur_secs.count();
        tv.tv_usec = dur_usecs.count();
      } else {
        // Just poll once with no timeout
        tv.tv_sec = 0;
        tv.tv_usec = 0;
      }
    }
    const int num_set_fds = ::select(nfds, read_fdset_ptr, write_fdset_ptr,
                                     error_fdset_ptr, tv_ptr);
    if (num_set_fds < 0) {
      // We got an error
      error.SetErrorToErrno();
      if (error.GetError() == EINTR) {
        error.Clear();
        continue; // Keep calling select if we get EINTR
      } else
        return error;
    } else if (num_set_fds == 0) {
      // Timeout
      error.SetError(ETIMEDOUT, lldb::eErrorTypePOSIX);
      error.SetErrorString("timed out");
      return error;
    } else {
      // One or more descriptors were set, update the FDInfo::select_is_set
      // mask so users can ask the SelectHelper class so clients can call one
      // of:

      for (auto &pair : m_fd_map) {
        const int fd = pair.first;

        if (pair.second.read_set) {
          if (FD_ISSET(fd, read_fdset_ptr))
            pair.second.read_is_set = true;
        }
        if (pair.second.write_set) {
          if (FD_ISSET(fd, write_fdset_ptr))
            pair.second.write_is_set = true;
        }
        if (pair.second.error_set) {
          if (FD_ISSET(fd, error_fdset_ptr))
            pair.second.error_is_set = true;
        }
      }
      break;
    }
  }
  return error;
}
