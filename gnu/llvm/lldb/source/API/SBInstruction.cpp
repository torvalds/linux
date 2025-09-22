//===-- SBInstruction.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBInstruction.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBFile.h"

#include "lldb/API/SBInstruction.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTarget.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Core/Module.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"

#include <memory>

// We recently fixed a leak in one of the Instruction subclasses where the
// instruction will only hold a weak reference to the disassembler to avoid a
// cycle that was keeping both objects alive (leak) and we need the
// InstructionImpl class to make sure our public API behaves as users would
// expect. Calls in our public API allow clients to do things like:
//
// 1  lldb::SBInstruction inst;
// 2  inst = target.ReadInstructions(pc, 1).GetInstructionAtIndex(0)
// 3  if (inst.DoesBranch())
// 4  ...
//
// There was a temporary lldb::DisassemblerSP object created in the
// SBInstructionList that was returned by lldb.target.ReadInstructions() that
// will go away after line 2 but the "inst" object should be able to still
// answer questions about itself. So we make sure that any SBInstruction
// objects that are given out have a strong reference to the disassembler and
// the instruction so that the object can live and successfully respond to all
// queries.
class InstructionImpl {
public:
  InstructionImpl(const lldb::DisassemblerSP &disasm_sp,
                  const lldb::InstructionSP &inst_sp)
      : m_disasm_sp(disasm_sp), m_inst_sp(inst_sp) {}

  lldb::InstructionSP GetSP() const { return m_inst_sp; }

  bool IsValid() const { return (bool)m_inst_sp; }

protected:
  lldb::DisassemblerSP m_disasm_sp; // Can be empty/invalid
  lldb::InstructionSP m_inst_sp;
};

using namespace lldb;
using namespace lldb_private;

SBInstruction::SBInstruction() { LLDB_INSTRUMENT_VA(this); }

SBInstruction::SBInstruction(const lldb::DisassemblerSP &disasm_sp,
                             const lldb::InstructionSP &inst_sp)
    : m_opaque_sp(new InstructionImpl(disasm_sp, inst_sp)) {}

SBInstruction::SBInstruction(const SBInstruction &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBInstruction &SBInstruction::operator=(const SBInstruction &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBInstruction::~SBInstruction() = default;

bool SBInstruction::IsValid() {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBInstruction::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp && m_opaque_sp->IsValid();
}

SBAddress SBInstruction::GetAddress() {
  LLDB_INSTRUMENT_VA(this);

  SBAddress sb_addr;
  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp && inst_sp->GetAddress().IsValid())
    sb_addr.SetAddress(inst_sp->GetAddress());
  return sb_addr;
}

const char *SBInstruction::GetMnemonic(SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (!inst_sp)
    return nullptr;

  ExecutionContext exe_ctx;
  TargetSP target_sp(target.GetSP());
  std::unique_lock<std::recursive_mutex> lock;
  if (target_sp) {
    lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());

    target_sp->CalculateExecutionContext(exe_ctx);
    exe_ctx.SetProcessSP(target_sp->GetProcessSP());
  }
  return ConstString(inst_sp->GetMnemonic(&exe_ctx)).GetCString();
}

const char *SBInstruction::GetOperands(SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (!inst_sp)
    return nullptr;

  ExecutionContext exe_ctx;
  TargetSP target_sp(target.GetSP());
  std::unique_lock<std::recursive_mutex> lock;
  if (target_sp) {
    lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());

    target_sp->CalculateExecutionContext(exe_ctx);
    exe_ctx.SetProcessSP(target_sp->GetProcessSP());
  }
  return ConstString(inst_sp->GetOperands(&exe_ctx)).GetCString();
}

const char *SBInstruction::GetComment(SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (!inst_sp)
    return nullptr;

  ExecutionContext exe_ctx;
  TargetSP target_sp(target.GetSP());
  std::unique_lock<std::recursive_mutex> lock;
  if (target_sp) {
    lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());

    target_sp->CalculateExecutionContext(exe_ctx);
    exe_ctx.SetProcessSP(target_sp->GetProcessSP());
  }
  return ConstString(inst_sp->GetComment(&exe_ctx)).GetCString();
}

lldb::InstructionControlFlowKind SBInstruction::GetControlFlowKind(lldb::SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp) {
    ExecutionContext exe_ctx;
    TargetSP target_sp(target.GetSP());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp) {
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());

      target_sp->CalculateExecutionContext(exe_ctx);
      exe_ctx.SetProcessSP(target_sp->GetProcessSP());
    }
    return inst_sp->GetControlFlowKind(&exe_ctx);
  }
  return lldb::eInstructionControlFlowKindUnknown;
}

size_t SBInstruction::GetByteSize() {
  LLDB_INSTRUMENT_VA(this);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp)
    return inst_sp->GetOpcode().GetByteSize();
  return 0;
}

SBData SBInstruction::GetData(SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  lldb::SBData sb_data;
  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp) {
    DataExtractorSP data_extractor_sp(new DataExtractor());
    if (inst_sp->GetData(*data_extractor_sp)) {
      sb_data.SetOpaque(data_extractor_sp);
    }
  }
  return sb_data;
}

bool SBInstruction::DoesBranch() {
  LLDB_INSTRUMENT_VA(this);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp)
    return inst_sp->DoesBranch();
  return false;
}

bool SBInstruction::HasDelaySlot() {
  LLDB_INSTRUMENT_VA(this);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp)
    return inst_sp->HasDelaySlot();
  return false;
}

bool SBInstruction::CanSetBreakpoint() {
  LLDB_INSTRUMENT_VA(this);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp)
    return inst_sp->CanSetBreakpoint();
  return false;
}

lldb::InstructionSP SBInstruction::GetOpaque() {
  if (m_opaque_sp)
    return m_opaque_sp->GetSP();
  else
    return lldb::InstructionSP();
}

void SBInstruction::SetOpaque(const lldb::DisassemblerSP &disasm_sp,
                              const lldb::InstructionSP &inst_sp) {
  m_opaque_sp = std::make_shared<InstructionImpl>(disasm_sp, inst_sp);
}

bool SBInstruction::GetDescription(lldb::SBStream &s) {
  LLDB_INSTRUMENT_VA(this, s);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp) {
    SymbolContext sc;
    const Address &addr = inst_sp->GetAddress();
    ModuleSP module_sp(addr.GetModule());
    if (module_sp)
      module_sp->ResolveSymbolContextForAddress(addr, eSymbolContextEverything,
                                                sc);
    // Use the "ref()" instead of the "get()" accessor in case the SBStream
    // didn't have a stream already created, one will get created...
    FormatEntity::Entry format;
    FormatEntity::Parse("${addr}: ", format);
    inst_sp->Dump(&s.ref(), 0, true, false, /*show_control_flow_kind=*/false,
                  nullptr, &sc, nullptr, &format, 0);
    return true;
  }
  return false;
}

void SBInstruction::Print(FILE *outp) {
  LLDB_INSTRUMENT_VA(this, outp);
  FileSP out = std::make_shared<NativeFile>(outp, /*take_ownership=*/false);
  Print(out);
}

void SBInstruction::Print(SBFile out) {
  LLDB_INSTRUMENT_VA(this, out);
  Print(out.m_opaque_sp);
}

void SBInstruction::Print(FileSP out_sp) {
  LLDB_INSTRUMENT_VA(this, out_sp);

  if (!out_sp || !out_sp->IsValid())
    return;

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp) {
    SymbolContext sc;
    const Address &addr = inst_sp->GetAddress();
    ModuleSP module_sp(addr.GetModule());
    if (module_sp)
      module_sp->ResolveSymbolContextForAddress(addr, eSymbolContextEverything,
                                                sc);
    StreamFile out_stream(out_sp);
    FormatEntity::Entry format;
    FormatEntity::Parse("${addr}: ", format);
    inst_sp->Dump(&out_stream, 0, true, false, /*show_control_flow_kind=*/false,
                  nullptr, &sc, nullptr, &format, 0);
  }
}

bool SBInstruction::EmulateWithFrame(lldb::SBFrame &frame,
                                     uint32_t evaluate_options) {
  LLDB_INSTRUMENT_VA(this, frame, evaluate_options);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp) {
    lldb::StackFrameSP frame_sp(frame.GetFrameSP());

    if (frame_sp) {
      lldb_private::ExecutionContext exe_ctx;
      frame_sp->CalculateExecutionContext(exe_ctx);
      lldb_private::Target *target = exe_ctx.GetTargetPtr();
      lldb_private::ArchSpec arch = target->GetArchitecture();

      return inst_sp->Emulate(
          arch, evaluate_options, (void *)frame_sp.get(),
          &lldb_private::EmulateInstruction::ReadMemoryFrame,
          &lldb_private::EmulateInstruction::WriteMemoryFrame,
          &lldb_private::EmulateInstruction::ReadRegisterFrame,
          &lldb_private::EmulateInstruction::WriteRegisterFrame);
    }
  }
  return false;
}

bool SBInstruction::DumpEmulation(const char *triple) {
  LLDB_INSTRUMENT_VA(this, triple);

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp && triple) {
    return inst_sp->DumpEmulation(HostInfo::GetAugmentedArchSpec(triple));
  }
  return false;
}

bool SBInstruction::TestEmulation(lldb::SBStream &output_stream,
                                  const char *test_file) {
  LLDB_INSTRUMENT_VA(this, output_stream, test_file);

  if (!m_opaque_sp)
    SetOpaque(lldb::DisassemblerSP(),
              lldb::InstructionSP(new PseudoInstruction()));

  lldb::InstructionSP inst_sp(GetOpaque());
  if (inst_sp)
    return inst_sp->TestEmulation(output_stream.ref(), test_file);
  return false;
}
