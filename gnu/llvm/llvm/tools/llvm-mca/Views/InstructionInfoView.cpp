//===--------------------- InstructionInfoView.cpp --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the InstructionInfoView API.
///
//===----------------------------------------------------------------------===//

#include "Views/InstructionInfoView.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/JSON.h"

namespace llvm {
namespace mca {

void InstructionInfoView::printView(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);

  ArrayRef<llvm::MCInst> Source = getSource();
  if (!Source.size())
    return;

  IIVDVec IIVD(Source.size());
  collectData(IIVD);

  TempStream << "\n\nInstruction Info:\n";
  TempStream << "[1]: #uOps\n[2]: Latency\n[3]: RThroughput\n"
             << "[4]: MayLoad\n[5]: MayStore\n[6]: HasSideEffects (U)\n";
  if (PrintBarriers) {
    TempStream << "[7]: LoadBarrier\n[8]: StoreBarrier\n";
  }
  if (PrintEncodings) {
    if (PrintBarriers) {
      TempStream << "[9]: Encoding Size\n";
      TempStream << "\n[1]    [2]    [3]    [4]    [5]    [6]    [7]    [8]    "
                 << "[9]    Encodings:                    Instructions:\n";
    } else {
      TempStream << "[7]: Encoding Size\n";
      TempStream << "\n[1]    [2]    [3]    [4]    [5]    [6]    [7]    "
                 << "Encodings:                    Instructions:\n";
    }
  } else {
    if (PrintBarriers) {
      TempStream << "\n[1]    [2]    [3]    [4]    [5]    [6]    [7]    [8]    "
                 << "Instructions:\n";
    } else {
      TempStream << "\n[1]    [2]    [3]    [4]    [5]    [6]    "
                 << "Instructions:\n";
    }
  }

  for (const auto &[Index, IIVDEntry, Inst] : enumerate(IIVD, Source)) {
    TempStream << ' ' << IIVDEntry.NumMicroOpcodes << "    ";
    if (IIVDEntry.NumMicroOpcodes < 10)
      TempStream << "  ";
    else if (IIVDEntry.NumMicroOpcodes < 100)
      TempStream << ' ';
    TempStream << IIVDEntry.Latency << "   ";
    if (IIVDEntry.Latency < 10)
      TempStream << "  ";
    else if (IIVDEntry.Latency < 100)
      TempStream << ' ';

    if (IIVDEntry.RThroughput) {
      double RT = *IIVDEntry.RThroughput;
      TempStream << format("%.2f", RT) << ' ';
      if (RT < 10.0)
        TempStream << "  ";
      else if (RT < 100.0)
        TempStream << ' ';
    } else {
      TempStream << " -     ";
    }
    TempStream << (IIVDEntry.mayLoad ? " *     " : "       ");
    TempStream << (IIVDEntry.mayStore ? " *     " : "       ");
    TempStream << (IIVDEntry.hasUnmodeledSideEffects ? " U     " : "       ");

    if (PrintBarriers) {
      TempStream << (LoweredInsts[Index]->isALoadBarrier() ? " *     "
                                                           : "       ");
      TempStream << (LoweredInsts[Index]->isAStoreBarrier() ? " *     "
                                                            : "       ");
    }

    if (PrintEncodings) {
      StringRef Encoding(CE.getEncoding(Index));
      unsigned EncodingSize = Encoding.size();
      TempStream << " " << EncodingSize
                 << (EncodingSize < 10 ? "     " : "    ");
      TempStream.flush();
      formatted_raw_ostream FOS(TempStream);
      for (unsigned i = 0, e = Encoding.size(); i != e; ++i)
        FOS << format("%02x ", (uint8_t)Encoding[i]);
      FOS.PadToColumn(30);
      FOS.flush();
    }

    TempStream << printInstructionString(Inst) << '\n';
  }

  TempStream.flush();
  OS << Buffer;
}

void InstructionInfoView::collectData(
    MutableArrayRef<InstructionInfoViewData> IIVD) const {
  const llvm::MCSubtargetInfo &STI = getSubTargetInfo();
  const MCSchedModel &SM = STI.getSchedModel();
  for (const auto I : zip(getSource(), IIVD)) {
    const MCInst &Inst = std::get<0>(I);
    InstructionInfoViewData &IIVDEntry = std::get<1>(I);
    const MCInstrDesc &MCDesc = MCII.get(Inst.getOpcode());

    // Obtain the scheduling class information from the instruction
    // and instruments.
    auto IVecIt = InstToInstruments.find(&Inst);
    unsigned SchedClassID =
        IVecIt == InstToInstruments.end()
            ? MCDesc.getSchedClass()
            : IM.getSchedClassID(MCII, Inst, IVecIt->second);
    unsigned CPUID = SM.getProcessorID();

    // Try to solve variant scheduling classes.
    while (SchedClassID && SM.getSchedClassDesc(SchedClassID)->isVariant())
      SchedClassID =
          STI.resolveVariantSchedClass(SchedClassID, &Inst, &MCII, CPUID);

    const MCSchedClassDesc &SCDesc = *SM.getSchedClassDesc(SchedClassID);
    IIVDEntry.NumMicroOpcodes = SCDesc.NumMicroOps;
    IIVDEntry.Latency = MCSchedModel::computeInstrLatency(STI, SCDesc);
    // Add extra latency due to delays in the forwarding data paths.
    IIVDEntry.Latency += MCSchedModel::getForwardingDelayCycles(
        STI.getReadAdvanceEntries(SCDesc));
    IIVDEntry.RThroughput = MCSchedModel::getReciprocalThroughput(STI, SCDesc);
    IIVDEntry.mayLoad = MCDesc.mayLoad();
    IIVDEntry.mayStore = MCDesc.mayStore();
    IIVDEntry.hasUnmodeledSideEffects = MCDesc.hasUnmodeledSideEffects();
  }
}

// Construct a JSON object from a single InstructionInfoViewData object.
json::Object
InstructionInfoView::toJSON(const InstructionInfoViewData &IIVD) const {
  json::Object JO({{"NumMicroOpcodes", IIVD.NumMicroOpcodes},
                   {"Latency", IIVD.Latency},
                   {"mayLoad", IIVD.mayLoad},
                   {"mayStore", IIVD.mayStore},
                   {"hasUnmodeledSideEffects", IIVD.hasUnmodeledSideEffects}});
  JO.try_emplace("RThroughput", IIVD.RThroughput.value_or(0.0));
  return JO;
}

json::Value InstructionInfoView::toJSON() const {
  ArrayRef<llvm::MCInst> Source = getSource();
  if (!Source.size())
    return json::Value(0);

  IIVDVec IIVD(Source.size());
  collectData(IIVD);

  json::Array InstInfo;
  for (const auto &I : enumerate(IIVD)) {
    const InstructionInfoViewData &IIVDEntry = I.value();
    json::Object JO = toJSON(IIVDEntry);
    JO.try_emplace("Instruction", (unsigned)I.index());
    InstInfo.push_back(std::move(JO));
  }
  return json::Object({{"InstructionList", json::Value(std::move(InstInfo))}});
}
} // namespace mca.
} // namespace llvm
