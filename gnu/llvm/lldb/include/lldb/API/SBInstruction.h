//===-- SBInstruction.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBINSTRUCTION_H
#define LLDB_API_SBINSTRUCTION_H

#include "lldb/API/SBData.h"
#include "lldb/API/SBDefines.h"

#include <cstdio>

// There's a lot to be fixed here, but need to wait for underlying insn
// implementation to be revised & settle down first.

class InstructionImpl;

namespace lldb {

class LLDB_API SBInstruction {
public:
  SBInstruction();

  SBInstruction(const SBInstruction &rhs);

  const SBInstruction &operator=(const SBInstruction &rhs);

  ~SBInstruction();

  explicit operator bool() const;

  bool IsValid();

  SBAddress GetAddress();

  const char *GetMnemonic(lldb::SBTarget target);

  const char *GetOperands(lldb::SBTarget target);

  const char *GetComment(lldb::SBTarget target);

  lldb::InstructionControlFlowKind GetControlFlowKind(lldb::SBTarget target);

  lldb::SBData GetData(lldb::SBTarget target);

  size_t GetByteSize();

  bool DoesBranch();

  bool HasDelaySlot();

  bool CanSetBreakpoint();

#ifndef SWIG
  void Print(FILE *out);
#endif

  void Print(SBFile out);

  void Print(FileSP BORROWED);

  bool GetDescription(lldb::SBStream &description);

  bool EmulateWithFrame(lldb::SBFrame &frame, uint32_t evaluate_options);

  bool DumpEmulation(const char *triple); // triple is to specify the
                                          // architecture, e.g. 'armv6' or
                                          // 'armv7-apple-ios'

  bool TestEmulation(lldb::SBStream &output_stream, const char *test_file);

protected:
  friend class SBInstructionList;

  SBInstruction(const lldb::DisassemblerSP &disasm_sp,
                const lldb::InstructionSP &inst_sp);

  void SetOpaque(const lldb::DisassemblerSP &disasm_sp,
                 const lldb::InstructionSP &inst_sp);

  lldb::InstructionSP GetOpaque();

private:
  std::shared_ptr<InstructionImpl> m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBINSTRUCTION_H
