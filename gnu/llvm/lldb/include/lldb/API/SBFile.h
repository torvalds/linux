//===-- SBFile.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBFILE_H
#define LLDB_API_SBFILE_H

#include "lldb/API/SBDefines.h"

#include <cstdio>

namespace lldb {

class LLDB_API SBFile {
  friend class SBInstruction;
  friend class SBInstructionList;
  friend class SBDebugger;
  friend class SBCommandReturnObject;
  friend class SBProcess;

public:
  SBFile();
  SBFile(FileSP file_sp);
#ifndef SWIG
  SBFile(const SBFile &rhs);
  SBFile(FILE *file, bool transfer_ownership);
#endif
  SBFile(int fd, const char *mode, bool transfer_ownership);
  ~SBFile();

  SBFile &operator=(const SBFile &rhs);

  SBError Read(uint8_t *buf, size_t num_bytes, size_t *OUTPUT);
  SBError Write(const uint8_t *buf, size_t num_bytes, size_t *OUTPUT);
  SBError Flush();
  bool IsValid() const;
  SBError Close();

  operator bool() const;
#ifndef SWIG
  bool operator!() const;
#endif

  FileSP GetFile() const;

private:
  FileSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBFILE_H
