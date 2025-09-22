//===- FuzzerIO.h - Internal header for IO utils ----------------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// IO interface.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_IO_H
#define LLVM_FUZZER_IO_H

#include "FuzzerDefs.h"

namespace fuzzer {

long GetEpoch(const std::string &Path);

Unit FileToVector(const std::string &Path, size_t MaxSize = 0,
                  bool ExitOnError = true);

std::string FileToString(const std::string &Path);

void CopyFileToErr(const std::string &Path);

void WriteToFile(const uint8_t *Data, size_t Size, const std::string &Path);
// Write Data.c_str() to the file without terminating null character.
void WriteToFile(const std::string &Data, const std::string &Path);
void WriteToFile(const Unit &U, const std::string &Path);

void AppendToFile(const uint8_t *Data, size_t Size, const std::string &Path);
void AppendToFile(const std::string &Data, const std::string &Path);

void ReadDirToVectorOfUnits(const char *Path, std::vector<Unit> *V, long *Epoch,
                            size_t MaxSize, bool ExitOnError,
                            std::vector<std::string> *VPaths = 0);

// Returns "Dir/FileName" or equivalent for the current OS.
std::string DirPlusFile(const std::string &DirPath,
                        const std::string &FileName);

// Returns the name of the dir, similar to the 'dirname' utility.
std::string DirName(const std::string &FileName);

// Returns path to a TmpDir.
std::string TmpDir();

std::string TempPath(const char *Prefix, const char *Extension);

bool IsInterestingCoverageFile(const std::string &FileName);

void DupAndCloseStderr();

void CloseStdout();

// For testing.
FILE *GetOutputFile();
void SetOutputFile(FILE *NewOutputFile);

void Puts(const char *Str);
void Printf(const char *Fmt, ...);
void VPrintf(bool Verbose, const char *Fmt, ...);

// Print using raw syscalls, useful when printing at early init stages.
void RawPrint(const char *Str);

// Platform specific functions:
bool IsFile(const std::string &Path);
bool IsDirectory(const std::string &Path);
size_t FileSize(const std::string &Path);

void ListFilesInDirRecursive(const std::string &Dir, long *Epoch,
                             std::vector<std::string> *V, bool TopDir);

bool MkDirRecursive(const std::string &Dir);
void RmDirRecursive(const std::string &Dir);

// Iterate files and dirs inside Dir, recursively.
// Call DirPreCallback/DirPostCallback on dirs before/after
// calling FileCallback on files.
void IterateDirRecursive(const std::string &Dir,
                         void (*DirPreCallback)(const std::string &Dir),
                         void (*DirPostCallback)(const std::string &Dir),
                         void (*FileCallback)(const std::string &Dir));

struct SizedFile {
  std::string File;
  size_t Size;
  bool operator<(const SizedFile &B) const { return Size < B.Size; }
};

void GetSizedFilesFromDir(const std::string &Dir, std::vector<SizedFile> *V);

char GetSeparator();
bool IsSeparator(char C);
// Similar to the basename utility: returns the file name w/o the dir prefix.
std::string Basename(const std::string &Path);

FILE* OpenFile(int Fd, const char *Mode);

int CloseFile(int Fd);

int DuplicateFile(int Fd);

void RemoveFile(const std::string &Path);
void RenameFile(const std::string &OldPath, const std::string &NewPath);

intptr_t GetHandleFromFd(int fd);

void MkDir(const std::string &Path);
void RmDir(const std::string &Path);

const std::string &getDevNull();

}  // namespace fuzzer

#endif  // LLVM_FUZZER_IO_H
