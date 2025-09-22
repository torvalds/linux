//===-- SBInstructionList.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBInstructionList.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBInstruction.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

SBInstructionList::SBInstructionList() { LLDB_INSTRUMENT_VA(this); }

SBInstructionList::SBInstructionList(const SBInstructionList &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBInstructionList &SBInstructionList::
operator=(const SBInstructionList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBInstructionList::~SBInstructionList() = default;

bool SBInstructionList::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBInstructionList::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

size_t SBInstructionList::GetSize() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    return m_opaque_sp->GetInstructionList().GetSize();
  return 0;
}

SBInstruction SBInstructionList::GetInstructionAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBInstruction inst;
  if (m_opaque_sp && idx < m_opaque_sp->GetInstructionList().GetSize())
    inst.SetOpaque(
        m_opaque_sp,
        m_opaque_sp->GetInstructionList().GetInstructionAtIndex(idx));
  return inst;
}

size_t SBInstructionList::GetInstructionsCount(const SBAddress &start,
                                               const SBAddress &end,
                                               bool canSetBreakpoint) {
  LLDB_INSTRUMENT_VA(this, start, end, canSetBreakpoint);

  size_t num_instructions = GetSize();
  size_t i = 0;
  SBAddress addr;
  size_t lower_index = 0;
  size_t upper_index = 0;
  size_t instructions_to_skip = 0;
  for (i = 0; i < num_instructions; ++i) {
    addr = GetInstructionAtIndex(i).GetAddress();
    if (start == addr)
      lower_index = i;
    if (end == addr)
      upper_index = i;
  }
  if (canSetBreakpoint)
    for (i = lower_index; i <= upper_index; ++i) {
      SBInstruction insn = GetInstructionAtIndex(i);
      if (!insn.CanSetBreakpoint())
        ++instructions_to_skip;
    }
  return upper_index - lower_index - instructions_to_skip;
}

void SBInstructionList::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_sp.reset();
}

void SBInstructionList::AppendInstruction(SBInstruction insn) {
  LLDB_INSTRUMENT_VA(this, insn);
}

void SBInstructionList::SetDisassembler(const lldb::DisassemblerSP &opaque_sp) {
  m_opaque_sp = opaque_sp;
}

void SBInstructionList::Print(FILE *out) {
  LLDB_INSTRUMENT_VA(this, out);
  if (out == nullptr)
    return;
  StreamFile stream(out, false);
  GetDescription(stream);
}

void SBInstructionList::Print(SBFile out) {
  LLDB_INSTRUMENT_VA(this, out);
  if (!out.IsValid())
    return;
  StreamFile stream(out.m_opaque_sp);
  GetDescription(stream);
}

void SBInstructionList::Print(FileSP out_sp) {
  LLDB_INSTRUMENT_VA(this, out_sp);
  if (!out_sp || !out_sp->IsValid())
    return;
  StreamFile stream(out_sp);
  GetDescription(stream);
}

bool SBInstructionList::GetDescription(lldb::SBStream &stream) {
  LLDB_INSTRUMENT_VA(this, stream);
  return GetDescription(stream.ref());
}

bool SBInstructionList::GetDescription(Stream &sref) {

  if (m_opaque_sp) {
    size_t num_instructions = GetSize();
    if (num_instructions) {
      // Call the ref() to make sure a stream is created if one deesn't exist
      // already inside description...
      const uint32_t max_opcode_byte_size =
          m_opaque_sp->GetInstructionList().GetMaxOpcocdeByteSize();
      FormatEntity::Entry format;
      FormatEntity::Parse("${addr}: ", format);
      SymbolContext sc;
      SymbolContext prev_sc;
      for (size_t i = 0; i < num_instructions; ++i) {
        Instruction *inst =
            m_opaque_sp->GetInstructionList().GetInstructionAtIndex(i).get();
        if (inst == nullptr)
          break;

        const Address &addr = inst->GetAddress();
        prev_sc = sc;
        ModuleSP module_sp(addr.GetModule());
        if (module_sp) {
          module_sp->ResolveSymbolContextForAddress(
              addr, eSymbolContextEverything, sc);
        }

        inst->Dump(&sref, max_opcode_byte_size, true, false,
                   /*show_control_flow_kind=*/false, nullptr, &sc, &prev_sc,
                   &format, 0);
        sref.EOL();
      }
      return true;
    }
  }
  return false;
}

bool SBInstructionList::DumpEmulationForAllInstructions(const char *triple) {
  LLDB_INSTRUMENT_VA(this, triple);

  if (m_opaque_sp) {
    size_t len = GetSize();
    for (size_t i = 0; i < len; ++i) {
      if (!GetInstructionAtIndex((uint32_t)i).DumpEmulation(triple))
        return false;
    }
  }
  return true;
}
