//===- FuzzerIO.cpp - IO utils. -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// IO functions.
//===----------------------------------------------------------------------===//

#include "FuzzerDefs.h"
#include "FuzzerExtFunctions.h"
#include "FuzzerIO.h"
#include "FuzzerUtil.h"
#include <algorithm>
#include <cstdarg>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <sys/types.h>

namespace fuzzer {

static FILE *OutputFile = stderr;

FILE *GetOutputFile() {
  return OutputFile;
}

void SetOutputFile(FILE *NewOutputFile) {
  OutputFile = NewOutputFile;
}

long GetEpoch(const std::string &Path) {
  struct stat St;
  if (stat(Path.c_str(), &St))
    return 0;  // Can't stat, be conservative.
  return St.st_mtime;
}

Unit FileToVector(const std::string &Path, size_t MaxSize, bool ExitOnError) {
  std::ifstream T(Path, std::ios::binary);
  if (ExitOnError && !T) {
    Printf("No such directory: %s; exiting\n", Path.c_str());
    exit(1);
  }

  T.seekg(0, T.end);
  auto EndPos = T.tellg();
  if (EndPos < 0) return {};
  size_t FileLen = EndPos;
  if (MaxSize)
    FileLen = std::min(FileLen, MaxSize);

  T.seekg(0, T.beg);
  Unit Res(FileLen);
  T.read(reinterpret_cast<char *>(Res.data()), FileLen);
  return Res;
}

std::string FileToString(const std::string &Path) {
  std::ifstream T(Path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(T)),
                     std::istreambuf_iterator<char>());
}

void CopyFileToErr(const std::string &Path) {
  Puts(FileToString(Path).c_str());
}

void WriteToFile(const Unit &U, const std::string &Path) {
  WriteToFile(U.data(), U.size(), Path);
}

void WriteToFile(const std::string &Data, const std::string &Path) {
  WriteToFile(reinterpret_cast<const uint8_t *>(Data.c_str()), Data.size(),
              Path);
}

void WriteToFile(const uint8_t *Data, size_t Size, const std::string &Path) {
  // Use raw C interface because this function may be called from a sig handler.
  FILE *Out = fopen(Path.c_str(), "wb");
  if (!Out) return;
  fwrite(Data, sizeof(Data[0]), Size, Out);
  fclose(Out);
}

void AppendToFile(const std::string &Data, const std::string &Path) {
  AppendToFile(reinterpret_cast<const uint8_t *>(Data.data()), Data.size(),
               Path);
}

void AppendToFile(const uint8_t *Data, size_t Size, const std::string &Path) {
  FILE *Out = fopen(Path.c_str(), "a");
  if (!Out)
    return;
  fwrite(Data, sizeof(Data[0]), Size, Out);
  fclose(Out);
}

void ReadDirToVectorOfUnits(const char *Path, std::vector<Unit> *V, long *Epoch,
                            size_t MaxSize, bool ExitOnError,
                            std::vector<std::string> *VPaths) {
  long E = Epoch ? *Epoch : 0;
  std::vector<std::string> Files;
  ListFilesInDirRecursive(Path, Epoch, &Files, /*TopDir*/true);
  size_t NumLoaded = 0;
  for (size_t i = 0; i < Files.size(); i++) {
    auto &X = Files[i];
    if (Epoch && GetEpoch(X) < E) continue;
    NumLoaded++;
    if ((NumLoaded & (NumLoaded - 1)) == 0 && NumLoaded >= 1024)
      Printf("Loaded %zd/%zd files from %s\n", NumLoaded, Files.size(), Path);
    auto S = FileToVector(X, MaxSize, ExitOnError);
    if (!S.empty()) {
      V->push_back(S);
      if (VPaths)
        VPaths->push_back(X);
    }
  }
}

void GetSizedFilesFromDir(const std::string &Dir, std::vector<SizedFile> *V) {
  std::vector<std::string> Files;
  ListFilesInDirRecursive(Dir, 0, &Files, /*TopDir*/true);
  for (auto &File : Files)
    if (size_t Size = FileSize(File))
      V->push_back({File, Size});
}

std::string DirPlusFile(const std::string &DirPath,
                        const std::string &FileName) {
  return DirPath + GetSeparator() + FileName;
}

void DupAndCloseStderr() {
  int OutputFd = DuplicateFile(2);
  if (OutputFd >= 0) {
    FILE *NewOutputFile = OpenFile(OutputFd, "w");
    if (NewOutputFile) {
      OutputFile = NewOutputFile;
      if (EF->__sanitizer_set_report_fd)
        EF->__sanitizer_set_report_fd(
            reinterpret_cast<void *>(GetHandleFromFd(OutputFd)));
      DiscardOutput(2);
    }
  }
}

void CloseStdout() {
  DiscardOutput(1);
}

void Puts(const char *Str) {
  fputs(Str, OutputFile);
  fflush(OutputFile);
}

void Printf(const char *Fmt, ...) {
  va_list ap;
  va_start(ap, Fmt);
  vfprintf(OutputFile, Fmt, ap);
  va_end(ap);
  fflush(OutputFile);
}

void VPrintf(bool Verbose, const char *Fmt, ...) {
  if (!Verbose) return;
  va_list ap;
  va_start(ap, Fmt);
  vfprintf(OutputFile, Fmt, ap);
  va_end(ap);
  fflush(OutputFile);
}

static bool MkDirRecursiveInner(const std::string &Leaf) {
  // Prevent chance of potential infinite recursion
  if (Leaf == ".")
    return true;

  const std::string &Dir = DirName(Leaf);

  if (IsDirectory(Dir)) {
    MkDir(Leaf);
    return IsDirectory(Leaf);
  }

  bool ret = MkDirRecursiveInner(Dir);
  if (!ret) {
    // Give up early if a previous MkDir failed
    return ret;
  }

  MkDir(Leaf);
  return IsDirectory(Leaf);
}

bool MkDirRecursive(const std::string &Dir) {
  if (Dir.empty())
    return false;

  if (IsDirectory(Dir))
    return true;

  return MkDirRecursiveInner(Dir);
}

void RmDirRecursive(const std::string &Dir) {
  IterateDirRecursive(
      Dir, [](const std::string &Path) {},
      [](const std::string &Path) { RmDir(Path); },
      [](const std::string &Path) { RemoveFile(Path); });
}

std::string TempPath(const char *Prefix, const char *Extension) {
  return DirPlusFile(TmpDir(), std::string("libFuzzerTemp.") + Prefix +
                                   std::to_string(GetPid()) + Extension);
}

}  // namespace fuzzer
