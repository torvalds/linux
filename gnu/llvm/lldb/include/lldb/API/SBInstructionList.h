//===-- SBInstructionList.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBINSTRUCTIONLIST_H
#define LLDB_API_SBINSTRUCTIONLIST_H

#include "lldb/API/SBDefines.h"

#include <cstdio>

namespace lldb {

class LLDB_API SBInstructionList {
public:
  SBInstructionList();

  SBInstructionList(const SBInstructionList &rhs);

  const SBInstructionList &operator=(const SBInstructionList &rhs);

  ~SBInstructionList();

  explicit operator bool() const;

  bool IsValid() const;

  size_t GetSize();

  lldb::SBInstruction GetInstructionAtIndex(uint32_t idx);

  // Returns the number of instructions between the start and end address. If
  // canSetBreakpoint is true then the count will be the number of
  // instructions on which a breakpoint can be set.
  size_t GetInstructionsCount(const SBAddress &start,
                              const SBAddress &end,
                              bool canSetBreakpoint = false);                                   

  void Clear();

  void AppendInstruction(lldb::SBInstruction inst);

#ifndef SWIG
  void Print(FILE *out);
#endif

  void Print(SBFile out);

  void Print(FileSP BORROWED);

  bool GetDescription(lldb::SBStream &description);

  bool DumpEmulationForAllInstructions(const char *triple);

protected:
  friend class SBFunction;
  friend class SBSymbol;
  friend class SBTarget;

  void SetDisassembler(const lldb::DisassemblerSP &opaque_sp);
  bool GetDescription(lldb_private::Stream &description);


private:
  lldb::DisassemblerSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBINSTRUCTIONLIST_H
