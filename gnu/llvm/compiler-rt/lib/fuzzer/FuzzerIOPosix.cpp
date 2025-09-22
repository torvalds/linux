//===- FuzzerIOPosix.cpp - IO utils for Posix. ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// IO functions implementation using Posix API.
//===----------------------------------------------------------------------===//
#include "FuzzerPlatform.h"
#if LIBFUZZER_POSIX || LIBFUZZER_FUCHSIA

#include "FuzzerExtFunctions.h"
#include "FuzzerIO.h"
#include <cstdarg>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fuzzer {

bool IsFile(const std::string &Path) {
  struct stat St;
  if (stat(Path.c_str(), &St))
    return false;
  return S_ISREG(St.st_mode);
}

bool IsDirectory(const std::string &Path) {
  struct stat St;
  if (stat(Path.c_str(), &St))
    return false;
  return S_ISDIR(St.st_mode);
}

size_t FileSize(const std::string &Path) {
  struct stat St;
  if (stat(Path.c_str(), &St))
    return 0;
  return St.st_size;
}

std::string Basename(const std::string &Path) {
  size_t Pos = Path.rfind(GetSeparator());
  if (Pos == std::string::npos) return Path;
  assert(Pos < Path.size());
  return Path.substr(Pos + 1);
}

void ListFilesInDirRecursive(const std::string &Dir, long *Epoch,
                             std::vector<std::string> *V, bool TopDir) {
  auto E = GetEpoch(Dir);
  if (Epoch)
    if (E && *Epoch >= E) return;

  DIR *D = opendir(Dir.c_str());
  if (!D) {
    Printf("%s: %s; exiting\n", strerror(errno), Dir.c_str());
    exit(1);
  }
  while (auto E = readdir(D)) {
    std::string Path = DirPlusFile(Dir, E->d_name);
    if (E->d_type == DT_REG || E->d_type == DT_LNK ||
        (E->d_type == DT_UNKNOWN && IsFile(Path)))
      V->push_back(Path);
    else if ((E->d_type == DT_DIR ||
             (E->d_type == DT_UNKNOWN && IsDirectory(Path))) &&
             *E->d_name != '.')
      ListFilesInDirRecursive(Path, Epoch, V, false);
  }
  closedir(D);
  if (Epoch && TopDir)
    *Epoch = E;
}

void IterateDirRecursive(const std::string &Dir,
                         void (*DirPreCallback)(const std::string &Dir),
                         void (*DirPostCallback)(const std::string &Dir),
                         void (*FileCallback)(const std::string &Dir)) {
  DirPreCallback(Dir);
  DIR *D = opendir(Dir.c_str());
  if (!D) return;
  while (auto E = readdir(D)) {
    std::string Path = DirPlusFile(Dir, E->d_name);
    if (E->d_type == DT_REG || E->d_type == DT_LNK ||
        (E->d_type == DT_UNKNOWN && IsFile(Path)))
      FileCallback(Path);
    else if ((E->d_type == DT_DIR ||
             (E->d_type == DT_UNKNOWN && IsDirectory(Path))) &&
             *E->d_name != '.')
      IterateDirRecursive(Path, DirPreCallback, DirPostCallback, FileCallback);
  }
  closedir(D);
  DirPostCallback(Dir);
}

char GetSeparator() {
  return '/';
}

bool IsSeparator(char C) {
  return C == '/';
}

FILE* OpenFile(int Fd, const char* Mode) {
  return fdopen(Fd, Mode);
}

int CloseFile(int fd) {
  return close(fd);
}

int DuplicateFile(int Fd) {
  return dup(Fd);
}

void RemoveFile(const std::string &Path) {
  unlink(Path.c_str());
}

void RenameFile(const std::string &OldPath, const std::string &NewPath) {
  rename(OldPath.c_str(), NewPath.c_str());
}

intptr_t GetHandleFromFd(int fd) {
  return static_cast<intptr_t>(fd);
}

std::string DirName(const std::string &FileName) {
  char *Tmp = new char[FileName.size() + 1];
  memcpy(Tmp, FileName.c_str(), FileName.size() + 1);
  std::string Res = dirname(Tmp);
  delete [] Tmp;
  return Res;
}

std::string TmpDir() {
  if (auto Env = getenv("TMPDIR"))
    return Env;
  return "/tmp";
}

bool IsInterestingCoverageFile(const std::string &FileName) {
  if (FileName.find("compiler-rt/lib/") != std::string::npos)
    return false; // sanitizer internal.
  if (FileName.find("/usr/lib/") != std::string::npos)
    return false;
  if (FileName.find("/usr/include/") != std::string::npos)
    return false;
  if (FileName == "<null>")
    return false;
  return true;
}

void RawPrint(const char *Str) {
  (void)write(2, Str, strlen(Str));
}

void MkDir(const std::string &Path) {
  mkdir(Path.c_str(), 0700);
}

void RmDir(const std::string &Path) {
  rmdir(Path.c_str());
}

const std::string &getDevNull() {
  static const std::string devNull = "/dev/null";
  return devNull;
}

}  // namespace fuzzer

#endif // LIBFUZZER_POSIX
