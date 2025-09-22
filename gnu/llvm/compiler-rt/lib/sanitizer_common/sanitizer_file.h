//===-- sanitizer_file.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is shared between run-time libraries of sanitizers.
// It declares filesystem-related interfaces.  This is separate from
// sanitizer_common.h so that it's simpler to disable all the filesystem
// support code for a port that doesn't use it.
//
//===---------------------------------------------------------------------===//
#ifndef SANITIZER_FILE_H
#define SANITIZER_FILE_H

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

struct ReportFile {
  void Write(const char *buffer, uptr length);
  bool SupportsColors();
  void SetReportPath(const char *path);
  const char *GetReportPath();

  // Don't use fields directly. They are only declared public to allow
  // aggregate initialization.

  // Protects fields below.
  StaticSpinMutex *mu;
  // Opened file descriptor. Defaults to stderr. It may be equal to
  // kInvalidFd, in which case new file will be opened when necessary.
  fd_t fd;
  // Path prefix of report file, set via __sanitizer_set_report_path.
  char path_prefix[kMaxPathLength];
  // Full path to report, obtained as <path_prefix>.PID
  char full_path[kMaxPathLength];
  // PID of the process that opened fd. If a fork() occurs,
  // the PID of child will be different from fd_pid.
  uptr fd_pid;

 private:
  void ReopenIfNecessary();
};
extern ReportFile report_file;

enum FileAccessMode {
  RdOnly,
  WrOnly,
  RdWr
};

// Returns kInvalidFd on error.
fd_t OpenFile(const char *filename, FileAccessMode mode,
              error_t *errno_p = nullptr);
void CloseFile(fd_t);

// Return true on success, false on error.
bool ReadFromFile(fd_t fd, void *buff, uptr buff_size,
                  uptr *bytes_read = nullptr, error_t *error_p = nullptr);
bool WriteToFile(fd_t fd, const void *buff, uptr buff_size,
                 uptr *bytes_written = nullptr, error_t *error_p = nullptr);

// Scoped file handle closer.
struct FileCloser {
  explicit FileCloser(fd_t fd) : fd(fd) {}
  ~FileCloser() { CloseFile(fd); }
  fd_t fd;
};

bool SupportsColoredOutput(fd_t fd);

// OS
const char *GetPwd();
bool FileExists(const char *filename);
bool DirExists(const char *path);
char *FindPathToBinary(const char *name);
bool IsPathSeparator(const char c);
bool IsAbsolutePath(const char *path);
// Returns true on success, false on failure.
bool CreateDir(const char *pathname);
// Starts a subprocess and returns its pid.
// If *_fd parameters are not kInvalidFd their corresponding input/output
// streams will be redirect to the file. The files will always be closed
// in parent process even in case of an error.
// The child process will close all fds after STDERR_FILENO
// before passing control to a program.
pid_t StartSubprocess(const char *filename, const char *const argv[],
                      const char *const envp[], fd_t stdin_fd = kInvalidFd,
                      fd_t stdout_fd = kInvalidFd, fd_t stderr_fd = kInvalidFd);
// Checks if specified process is still running
bool IsProcessRunning(pid_t pid);
// Waits for the process to finish and returns its exit code.
// Returns -1 in case of an error.
int WaitForProcess(pid_t pid);

// Maps given file to virtual memory, and returns pointer to it
// (or NULL if mapping fails). Stores the size of mmaped region
// in '*buff_size'.
void *MapFileToMemory(const char *file_name, uptr *buff_size);
void *MapWritableFileToMemory(void *addr, uptr size, fd_t fd, OFF_T offset);

}  // namespace __sanitizer

#endif  // SANITIZER_FILE_H
