//===----- PerfSupportPlugin.cpp --- Utils for perf support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handles support for registering code with perf
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Debugging/PerfSupportPlugin.h"

#include "llvm/ExecutionEngine/JITLink/x86_64.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebugInfoSupport.h"
#include "llvm/ExecutionEngine/Orc/LookupAndRecordAddrs.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"

#define DEBUG_TYPE "orc"

using namespace llvm;
using namespace llvm::orc;
using namespace llvm::jitlink;

namespace {

// Creates an EH frame header prepared for a 32-bit relative relocation
// to the start of the .eh_frame section. Absolute injects a 64-bit absolute
// address space offset 4 bytes from the start instead of 4 bytes
Expected<std::string> createX64EHFrameHeader(Section &EHFrame,
                                             llvm::endianness endianness,
                                             bool absolute) {
  uint8_t Version = 1;
  uint8_t EhFramePtrEnc = 0;
  if (absolute) {
    EhFramePtrEnc |= dwarf::DW_EH_PE_sdata8 | dwarf::DW_EH_PE_absptr;
  } else {
    EhFramePtrEnc |= dwarf::DW_EH_PE_sdata4 | dwarf::DW_EH_PE_datarel;
  }
  uint8_t FDECountEnc = dwarf::DW_EH_PE_omit;
  uint8_t TableEnc = dwarf::DW_EH_PE_omit;
  // X86_64_64 relocation to the start of the .eh_frame section
  uint32_t EHFrameRelocation = 0;
  // uint32_t FDECount = 0;
  // Skip the FDE binary search table
  // We'd have to reprocess the CIEs to get this information,
  // which seems like more trouble than it's worth
  // TODO consider implementing this.
  // binary search table goes here

  size_t HeaderSize =
      (sizeof(Version) + sizeof(EhFramePtrEnc) + sizeof(FDECountEnc) +
       sizeof(TableEnc) +
       (absolute ? sizeof(uint64_t) : sizeof(EHFrameRelocation)));
  std::string HeaderContent(HeaderSize, '\0');
  BinaryStreamWriter Writer(
      MutableArrayRef<uint8_t>(
          reinterpret_cast<uint8_t *>(HeaderContent.data()), HeaderSize),
      endianness);
  if (auto Err = Writer.writeInteger(Version))
    return std::move(Err);
  if (auto Err = Writer.writeInteger(EhFramePtrEnc))
    return std::move(Err);
  if (auto Err = Writer.writeInteger(FDECountEnc))
    return std::move(Err);
  if (auto Err = Writer.writeInteger(TableEnc))
    return std::move(Err);
  if (absolute) {
    uint64_t EHFrameAddr = SectionRange(EHFrame).getStart().getValue();
    if (auto Err = Writer.writeInteger(EHFrameAddr))
      return std::move(Err);
  } else {
    if (auto Err = Writer.writeInteger(EHFrameRelocation))
      return std::move(Err);
  }
  return HeaderContent;
}

constexpr StringRef RegisterPerfStartSymbolName =
    "llvm_orc_registerJITLoaderPerfStart";
constexpr StringRef RegisterPerfEndSymbolName =
    "llvm_orc_registerJITLoaderPerfEnd";
constexpr StringRef RegisterPerfImplSymbolName =
    "llvm_orc_registerJITLoaderPerfImpl";

static PerfJITCodeLoadRecord
getCodeLoadRecord(const Symbol &Sym, std::atomic<uint64_t> &CodeIndex) {
  PerfJITCodeLoadRecord Record;
  auto Name = Sym.getName();
  auto Addr = Sym.getAddress();
  auto Size = Sym.getSize();
  Record.Prefix.Id = PerfJITRecordType::JIT_CODE_LOAD;
  // Runtime sets PID
  Record.Pid = 0;
  // Runtime sets TID
  Record.Tid = 0;
  Record.Vma = Addr.getValue();
  Record.CodeAddr = Addr.getValue();
  Record.CodeSize = Size;
  Record.CodeIndex = CodeIndex++;
  Record.Name = Name.str();
  // Initialize last, once all the other fields are filled
  Record.Prefix.TotalSize =
      (2 * sizeof(uint32_t)   // id, total_size
       + sizeof(uint64_t)     // timestamp
       + 2 * sizeof(uint32_t) // pid, tid
       + 4 * sizeof(uint64_t) // vma, code_addr, code_size, code_index
       + Name.size() + 1      // symbol name
       + Record.CodeSize      // code
      );
  return Record;
}

static std::optional<PerfJITDebugInfoRecord>
getDebugInfoRecord(const Symbol &Sym, DWARFContext &DC) {
  auto &Section = Sym.getBlock().getSection();
  auto Addr = Sym.getAddress();
  auto Size = Sym.getSize();
  auto SAddr = object::SectionedAddress{Addr.getValue(), Section.getOrdinal()};
  LLVM_DEBUG(dbgs() << "Getting debug info for symbol " << Sym.getName()
                    << " at address " << Addr.getValue() << " with size "
                    << Size << "\n"
                    << "Section ordinal: " << Section.getOrdinal() << "\n");
  auto LInfo = DC.getLineInfoForAddressRange(
      SAddr, Size, DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
  if (LInfo.empty()) {
    // No line info available
    LLVM_DEBUG(dbgs() << "No line info available\n");
    return std::nullopt;
  }
  PerfJITDebugInfoRecord Record;
  Record.Prefix.Id = PerfJITRecordType::JIT_CODE_DEBUG_INFO;
  Record.CodeAddr = Addr.getValue();
  for (const auto &Entry : LInfo) {
    auto Addr = Entry.first;
    // The function re-created by perf is preceded by a elf
    // header. Need to adjust for that, otherwise the results are
    // wrong.
    Addr += 0x40;
    Record.Entries.push_back({Addr, Entry.second.Line,
                              Entry.second.Discriminator,
                              Entry.second.FileName});
  }
  size_t EntriesBytes = (2   // record header
                         + 2 // record fields
                         ) *
                        sizeof(uint64_t);
  for (const auto &Entry : Record.Entries) {
    EntriesBytes +=
        sizeof(uint64_t) + 2 * sizeof(uint32_t); // Addr, Line/Discrim
    EntriesBytes += Entry.Name.size() + 1;       // Name
  }
  Record.Prefix.TotalSize = EntriesBytes;
  LLVM_DEBUG(dbgs() << "Created debug info record\n"
                    << "Total size: " << Record.Prefix.TotalSize << "\n"
                    << "Nr entries: " << Record.Entries.size() << "\n");
  return Record;
}

static Expected<PerfJITCodeUnwindingInfoRecord>
getUnwindingRecord(LinkGraph &G) {
  PerfJITCodeUnwindingInfoRecord Record;
  Record.Prefix.Id = PerfJITRecordType::JIT_CODE_UNWINDING_INFO;
  Record.Prefix.TotalSize = 0;
  auto Eh_frame = G.findSectionByName(".eh_frame");
  if (!Eh_frame) {
    LLVM_DEBUG(dbgs() << "No .eh_frame section found\n");
    return Record;
  }
  if (!G.getTargetTriple().isOSBinFormatELF()) {
    LLVM_DEBUG(dbgs() << "Not an ELF file, will not emit unwinding info\n");
    return Record;
  }
  auto SR = SectionRange(*Eh_frame);
  auto EHFrameSize = SR.getSize();
  auto Eh_frame_hdr = G.findSectionByName(".eh_frame_hdr");
  if (!Eh_frame_hdr) {
    if (G.getTargetTriple().getArch() == Triple::x86_64) {
      auto Hdr = createX64EHFrameHeader(*Eh_frame, G.getEndianness(), true);
      if (!Hdr)
        return Hdr.takeError();
      Record.EHFrameHdr = std::move(*Hdr);
    } else {
      LLVM_DEBUG(dbgs() << "No .eh_frame_hdr section found\n");
      return Record;
    }
    Record.EHFrameHdrAddr = 0;
    Record.EHFrameHdrSize = Record.EHFrameHdr.size();
    Record.UnwindDataSize = EHFrameSize + Record.EHFrameHdrSize;
    Record.MappedSize = 0; // Because the EHFrame header was not mapped
  } else {
    auto SR = SectionRange(*Eh_frame_hdr);
    Record.EHFrameHdrAddr = SR.getStart().getValue();
    Record.EHFrameHdrSize = SR.getSize();
    Record.UnwindDataSize = EHFrameSize + Record.EHFrameHdrSize;
    Record.MappedSize = Record.UnwindDataSize;
  }
  Record.EHFrameAddr = SR.getStart().getValue();
  Record.Prefix.TotalSize =
      (2 * sizeof(uint32_t) // id, total_size
       + sizeof(uint64_t)   // timestamp
       +
       3 * sizeof(uint64_t) // unwind_data_size, eh_frame_hdr_size, mapped_size
       + Record.UnwindDataSize // eh_frame_hdr, eh_frame
      );
  LLVM_DEBUG(dbgs() << "Created unwind record\n"
                    << "Total size: " << Record.Prefix.TotalSize << "\n"
                    << "Unwind size: " << Record.UnwindDataSize << "\n"
                    << "EHFrame size: " << EHFrameSize << "\n"
                    << "EHFrameHdr size: " << Record.EHFrameHdrSize << "\n");
  return Record;
}

static PerfJITRecordBatch getRecords(ExecutionSession &ES, LinkGraph &G,
                                     std::atomic<uint64_t> &CodeIndex,
                                     bool EmitDebugInfo, bool EmitUnwindInfo) {
  std::unique_ptr<DWARFContext> DC;
  StringMap<std::unique_ptr<MemoryBuffer>> DCBacking;
  if (EmitDebugInfo) {
    auto EDC = createDWARFContext(G);
    if (!EDC) {
      ES.reportError(EDC.takeError());
      EmitDebugInfo = false;
    } else {
      DC = std::move(EDC->first);
      DCBacking = std::move(EDC->second);
    }
  }
  PerfJITRecordBatch Batch;
  for (auto Sym : G.defined_symbols()) {
    if (!Sym->hasName() || !Sym->isCallable())
      continue;
    if (EmitDebugInfo) {
      auto DebugInfo = getDebugInfoRecord(*Sym, *DC);
      if (DebugInfo)
        Batch.DebugInfoRecords.push_back(std::move(*DebugInfo));
    }
    Batch.CodeLoadRecords.push_back(getCodeLoadRecord(*Sym, CodeIndex));
  }
  if (EmitUnwindInfo) {
    auto UWR = getUnwindingRecord(G);
    if (!UWR) {
      ES.reportError(UWR.takeError());
    } else {
      Batch.UnwindingRecord = std::move(*UWR);
    }
  } else {
    Batch.UnwindingRecord.Prefix.TotalSize = 0;
  }
  return Batch;
}
} // namespace

PerfSupportPlugin::PerfSupportPlugin(ExecutorProcessControl &EPC,
                                     ExecutorAddr RegisterPerfStartAddr,
                                     ExecutorAddr RegisterPerfEndAddr,
                                     ExecutorAddr RegisterPerfImplAddr,
                                     bool EmitDebugInfo, bool EmitUnwindInfo)
    : EPC(EPC), RegisterPerfStartAddr(RegisterPerfStartAddr),
      RegisterPerfEndAddr(RegisterPerfEndAddr),
      RegisterPerfImplAddr(RegisterPerfImplAddr), CodeIndex(0),
      EmitDebugInfo(EmitDebugInfo), EmitUnwindInfo(EmitUnwindInfo) {
  cantFail(EPC.callSPSWrapper<void()>(RegisterPerfStartAddr));
}
PerfSupportPlugin::~PerfSupportPlugin() {
  cantFail(EPC.callSPSWrapper<void()>(RegisterPerfEndAddr));
}

void PerfSupportPlugin::modifyPassConfig(MaterializationResponsibility &MR,
                                         LinkGraph &G,
                                         PassConfiguration &Config) {
  Config.PostFixupPasses.push_back([this](LinkGraph &G) {
    auto Batch = getRecords(EPC.getExecutionSession(), G, CodeIndex,
                            EmitDebugInfo, EmitUnwindInfo);
    G.allocActions().push_back(
        {cantFail(shared::WrapperFunctionCall::Create<
                  shared::SPSArgList<shared::SPSPerfJITRecordBatch>>(
             RegisterPerfImplAddr, Batch)),
         {}});
    return Error::success();
  });
}

Expected<std::unique_ptr<PerfSupportPlugin>>
PerfSupportPlugin::Create(ExecutorProcessControl &EPC, JITDylib &JD,
                          bool EmitDebugInfo, bool EmitUnwindInfo) {
  if (!EPC.getTargetTriple().isOSBinFormatELF()) {
    return make_error<StringError>(
        "Perf support only available for ELF LinkGraphs!",
        inconvertibleErrorCode());
  }
  auto &ES = EPC.getExecutionSession();
  ExecutorAddr StartAddr, EndAddr, ImplAddr;
  if (auto Err = lookupAndRecordAddrs(
          ES, LookupKind::Static, makeJITDylibSearchOrder({&JD}),
          {{ES.intern(RegisterPerfStartSymbolName), &StartAddr},
           {ES.intern(RegisterPerfEndSymbolName), &EndAddr},
           {ES.intern(RegisterPerfImplSymbolName), &ImplAddr}}))
    return std::move(Err);
  return std::make_unique<PerfSupportPlugin>(EPC, StartAddr, EndAddr, ImplAddr,
                                             EmitDebugInfo, EmitUnwindInfo);
}
