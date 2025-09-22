//===--------------------- InstrBuilder.cpp ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the InstrBuilder interface.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/InstrBuilder.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "llvm-mca-instrbuilder"

namespace llvm {
namespace mca {

char RecycledInstErr::ID = 0;

InstrBuilder::InstrBuilder(const llvm::MCSubtargetInfo &sti,
                           const llvm::MCInstrInfo &mcii,
                           const llvm::MCRegisterInfo &mri,
                           const llvm::MCInstrAnalysis *mcia,
                           const mca::InstrumentManager &im, unsigned cl)
    : STI(sti), MCII(mcii), MRI(mri), MCIA(mcia), IM(im), FirstCallInst(true),
      FirstReturnInst(true), CallLatency(cl) {
  const MCSchedModel &SM = STI.getSchedModel();
  ProcResourceMasks.resize(SM.getNumProcResourceKinds());
  computeProcResourceMasks(STI.getSchedModel(), ProcResourceMasks);
}

static void initializeUsedResources(InstrDesc &ID,
                                    const MCSchedClassDesc &SCDesc,
                                    const MCSubtargetInfo &STI,
                                    ArrayRef<uint64_t> ProcResourceMasks) {
  const MCSchedModel &SM = STI.getSchedModel();

  // Populate resources consumed.
  using ResourcePlusCycles = std::pair<uint64_t, ResourceUsage>;
  SmallVector<ResourcePlusCycles, 4> Worklist;

  // Track cycles contributed by resources that are in a "Super" relationship.
  // This is required if we want to correctly match the behavior of method
  // SubtargetEmitter::ExpandProcResource() in Tablegen. When computing the set
  // of "consumed" processor resources and resource cycles, the logic in
  // ExpandProcResource() doesn't update the number of resource cycles
  // contributed by a "Super" resource to a group.
  // We need to take this into account when we find that a processor resource is
  // part of a group, and it is also used as the "Super" of other resources.
  // This map stores the number of cycles contributed by sub-resources that are
  // part of a "Super" resource. The key value is the "Super" resource mask ID.
  DenseMap<uint64_t, unsigned> SuperResources;

  unsigned NumProcResources = SM.getNumProcResourceKinds();
  APInt Buffers(NumProcResources, 0);

  bool AllInOrderResources = true;
  bool AnyDispatchHazards = false;
  for (unsigned I = 0, E = SCDesc.NumWriteProcResEntries; I < E; ++I) {
    const MCWriteProcResEntry *PRE = STI.getWriteProcResBegin(&SCDesc) + I;
    const MCProcResourceDesc &PR = *SM.getProcResource(PRE->ProcResourceIdx);
    if (!PRE->ReleaseAtCycle) {
#ifndef NDEBUG
      WithColor::warning()
          << "Ignoring invalid write of zero cycles on processor resource "
          << PR.Name << "\n";
      WithColor::note() << "found in scheduling class " << SCDesc.Name
                        << " (write index #" << I << ")\n";
#endif
      continue;
    }

    uint64_t Mask = ProcResourceMasks[PRE->ProcResourceIdx];
    if (PR.BufferSize < 0) {
      AllInOrderResources = false;
    } else {
      Buffers.setBit(getResourceStateIndex(Mask));
      AnyDispatchHazards |= (PR.BufferSize == 0);
      AllInOrderResources &= (PR.BufferSize <= 1);
    }

    CycleSegment RCy(0, PRE->ReleaseAtCycle, false);
    Worklist.emplace_back(ResourcePlusCycles(Mask, ResourceUsage(RCy)));
    if (PR.SuperIdx) {
      uint64_t Super = ProcResourceMasks[PR.SuperIdx];
      SuperResources[Super] += PRE->ReleaseAtCycle;
    }
  }

  ID.MustIssueImmediately = AllInOrderResources && AnyDispatchHazards;

  // Sort elements by mask popcount, so that we prioritize resource units over
  // resource groups, and smaller groups over larger groups.
  sort(Worklist, [](const ResourcePlusCycles &A, const ResourcePlusCycles &B) {
    unsigned popcntA = llvm::popcount(A.first);
    unsigned popcntB = llvm::popcount(B.first);
    if (popcntA < popcntB)
      return true;
    if (popcntA > popcntB)
      return false;
    return A.first < B.first;
  });

  uint64_t UsedResourceUnits = 0;
  uint64_t UsedResourceGroups = 0;
  uint64_t UnitsFromResourceGroups = 0;

  // Remove cycles contributed by smaller resources, and check if there
  // are partially overlapping resource groups.
  ID.HasPartiallyOverlappingGroups = false;

  for (unsigned I = 0, E = Worklist.size(); I < E; ++I) {
    ResourcePlusCycles &A = Worklist[I];
    if (!A.second.size()) {
      assert(llvm::popcount(A.first) > 1 && "Expected a group!");
      UsedResourceGroups |= llvm::bit_floor(A.first);
      continue;
    }

    ID.Resources.emplace_back(A);
    uint64_t NormalizedMask = A.first;

    if (llvm::popcount(A.first) == 1) {
      UsedResourceUnits |= A.first;
    } else {
      // Remove the leading 1 from the resource group mask.
      NormalizedMask ^= llvm::bit_floor(NormalizedMask);
      if (UnitsFromResourceGroups & NormalizedMask)
        ID.HasPartiallyOverlappingGroups = true;

      UnitsFromResourceGroups |= NormalizedMask;
      UsedResourceGroups |= (A.first ^ NormalizedMask);
    }

    for (unsigned J = I + 1; J < E; ++J) {
      ResourcePlusCycles &B = Worklist[J];
      if ((NormalizedMask & B.first) == NormalizedMask) {
        B.second.CS.subtract(A.second.size() - SuperResources[A.first]);
        if (llvm::popcount(B.first) > 1)
          B.second.NumUnits++;
      }
    }
  }

  // A SchedWrite may specify a number of cycles in which a resource group
  // is reserved. For example (on target x86; cpu Haswell):
  //
  //  SchedWriteRes<[HWPort0, HWPort1, HWPort01]> {
  //    let ReleaseAtCycles = [2, 2, 3];
  //  }
  //
  // This means:
  // Resource units HWPort0 and HWPort1 are both used for 2cy.
  // Resource group HWPort01 is the union of HWPort0 and HWPort1.
  // Since this write touches both HWPort0 and HWPort1 for 2cy, HWPort01
  // will not be usable for 2 entire cycles from instruction issue.
  //
  // On top of those 2cy, SchedWriteRes explicitly specifies an extra latency
  // of 3 cycles for HWPort01. This tool assumes that the 3cy latency is an
  // extra delay on top of the 2 cycles latency.
  // During those extra cycles, HWPort01 is not usable by other instructions.
  for (ResourcePlusCycles &RPC : ID.Resources) {
    if (llvm::popcount(RPC.first) > 1 && !RPC.second.isReserved()) {
      // Remove the leading 1 from the resource group mask.
      uint64_t Mask = RPC.first ^ llvm::bit_floor(RPC.first);
      uint64_t MaxResourceUnits = llvm::popcount(Mask);
      if (RPC.second.NumUnits > (unsigned)llvm::popcount(Mask)) {
        RPC.second.setReserved();
        RPC.second.NumUnits = MaxResourceUnits;
      }
    }
  }

  // Identify extra buffers that are consumed through super resources.
  for (const std::pair<uint64_t, unsigned> &SR : SuperResources) {
    for (unsigned I = 1, E = NumProcResources; I < E; ++I) {
      const MCProcResourceDesc &PR = *SM.getProcResource(I);
      if (PR.BufferSize == -1)
        continue;

      uint64_t Mask = ProcResourceMasks[I];
      if (Mask != SR.first && ((Mask & SR.first) == SR.first))
        Buffers.setBit(getResourceStateIndex(Mask));
    }
  }

  ID.UsedBuffers = Buffers.getZExtValue();
  ID.UsedProcResUnits = UsedResourceUnits;
  ID.UsedProcResGroups = UsedResourceGroups;

  LLVM_DEBUG({
    for (const std::pair<uint64_t, ResourceUsage> &R : ID.Resources)
      dbgs() << "\t\tResource Mask=" << format_hex(R.first, 16) << ", "
             << "Reserved=" << R.second.isReserved() << ", "
             << "#Units=" << R.second.NumUnits << ", "
             << "cy=" << R.second.size() << '\n';
    uint64_t BufferIDs = ID.UsedBuffers;
    while (BufferIDs) {
      uint64_t Current = BufferIDs & (-BufferIDs);
      dbgs() << "\t\tBuffer Mask=" << format_hex(Current, 16) << '\n';
      BufferIDs ^= Current;
    }
    dbgs() << "\t\t Used Units=" << format_hex(ID.UsedProcResUnits, 16) << '\n';
    dbgs() << "\t\tUsed Groups=" << format_hex(ID.UsedProcResGroups, 16)
           << '\n';
    dbgs() << "\t\tHasPartiallyOverlappingGroups="
           << ID.HasPartiallyOverlappingGroups << '\n';
  });
}

static void computeMaxLatency(InstrDesc &ID, const MCInstrDesc &MCDesc,
                              const MCSchedClassDesc &SCDesc,
                              const MCSubtargetInfo &STI,
                              unsigned CallLatency) {
  if (MCDesc.isCall()) {
    // We cannot estimate how long this call will take.
    // Artificially set an arbitrarily high latency.
    ID.MaxLatency = CallLatency;
    return;
  }

  int Latency = MCSchedModel::computeInstrLatency(STI, SCDesc);
  // If latency is unknown, then conservatively assume the MaxLatency set for
  // calls.
  ID.MaxLatency = Latency < 0 ? CallLatency : static_cast<unsigned>(Latency);
}

static Error verifyOperands(const MCInstrDesc &MCDesc, const MCInst &MCI) {
  // Count register definitions, and skip non register operands in the process.
  unsigned I, E;
  unsigned NumExplicitDefs = MCDesc.getNumDefs();
  for (I = 0, E = MCI.getNumOperands(); NumExplicitDefs && I < E; ++I) {
    const MCOperand &Op = MCI.getOperand(I);
    if (Op.isReg())
      --NumExplicitDefs;
  }

  if (NumExplicitDefs) {
    return make_error<InstructionError<MCInst>>(
        "Expected more register operand definitions.", MCI);
  }

  if (MCDesc.hasOptionalDef()) {
    // Always assume that the optional definition is the last operand.
    const MCOperand &Op = MCI.getOperand(MCDesc.getNumOperands() - 1);
    if (I == MCI.getNumOperands() || !Op.isReg()) {
      std::string Message =
          "expected a register operand for an optional definition. Instruction "
          "has not been correctly analyzed.";
      return make_error<InstructionError<MCInst>>(Message, MCI);
    }
  }

  return ErrorSuccess();
}

void InstrBuilder::populateWrites(InstrDesc &ID, const MCInst &MCI,
                                  unsigned SchedClassID) {
  const MCInstrDesc &MCDesc = MCII.get(MCI.getOpcode());
  const MCSchedModel &SM = STI.getSchedModel();
  const MCSchedClassDesc &SCDesc = *SM.getSchedClassDesc(SchedClassID);

  // Assumptions made by this algorithm:
  //  1. The number of explicit and implicit register definitions in a MCInst
  //     matches the number of explicit and implicit definitions according to
  //     the opcode descriptor (MCInstrDesc).
  //  2. Uses start at index #(MCDesc.getNumDefs()).
  //  3. There can only be a single optional register definition, an it is
  //     either the last operand of the sequence (excluding extra operands
  //     contributed by variadic opcodes) or one of the explicit register
  //     definitions. The latter occurs for some Thumb1 instructions.
  //
  // These assumptions work quite well for most out-of-order in-tree targets
  // like x86. This is mainly because the vast majority of instructions is
  // expanded to MCInst using a straightforward lowering logic that preserves
  // the ordering of the operands.
  //
  // About assumption 1.
  // The algorithm allows non-register operands between register operand
  // definitions. This helps to handle some special ARM instructions with
  // implicit operand increment (-mtriple=armv7):
  //
  // vld1.32  {d18, d19}, [r1]!  @ <MCInst #1463 VLD1q32wb_fixed
  //                             @  <MCOperand Reg:59>
  //                             @  <MCOperand Imm:0>     (!!)
  //                             @  <MCOperand Reg:67>
  //                             @  <MCOperand Imm:0>
  //                             @  <MCOperand Imm:14>
  //                             @  <MCOperand Reg:0>>
  //
  // MCDesc reports:
  //  6 explicit operands.
  //  1 optional definition
  //  2 explicit definitions (!!)
  //
  // The presence of an 'Imm' operand between the two register definitions
  // breaks the assumption that "register definitions are always at the
  // beginning of the operand sequence".
  //
  // To workaround this issue, this algorithm ignores (i.e. skips) any
  // non-register operands between register definitions.  The optional
  // definition is still at index #(NumOperands-1).
  //
  // According to assumption 2. register reads start at #(NumExplicitDefs-1).
  // That means, register R1 from the example is both read and written.
  unsigned NumExplicitDefs = MCDesc.getNumDefs();
  unsigned NumImplicitDefs = MCDesc.implicit_defs().size();
  unsigned NumWriteLatencyEntries = SCDesc.NumWriteLatencyEntries;
  unsigned TotalDefs = NumExplicitDefs + NumImplicitDefs;
  if (MCDesc.hasOptionalDef())
    TotalDefs++;

  unsigned NumVariadicOps = MCI.getNumOperands() - MCDesc.getNumOperands();
  ID.Writes.resize(TotalDefs + NumVariadicOps);
  // Iterate over the operands list, and skip non-register or constant register
  // operands. The first NumExplicitDefs register operands are expected to be
  // register definitions.
  unsigned CurrentDef = 0;
  unsigned OptionalDefIdx = MCDesc.getNumOperands() - 1;
  unsigned i = 0;
  for (; i < MCI.getNumOperands() && CurrentDef < NumExplicitDefs; ++i) {
    const MCOperand &Op = MCI.getOperand(i);
    if (!Op.isReg())
      continue;

    if (MCDesc.operands()[CurrentDef].isOptionalDef()) {
      OptionalDefIdx = CurrentDef++;
      continue;
    }
    if (MRI.isConstant(Op.getReg())) {
      CurrentDef++;
      continue;
    }

    WriteDescriptor &Write = ID.Writes[CurrentDef];
    Write.OpIndex = i;
    if (CurrentDef < NumWriteLatencyEntries) {
      const MCWriteLatencyEntry &WLE =
          *STI.getWriteLatencyEntry(&SCDesc, CurrentDef);
      // Conservatively default to MaxLatency.
      Write.Latency =
          WLE.Cycles < 0 ? ID.MaxLatency : static_cast<unsigned>(WLE.Cycles);
      Write.SClassOrWriteResourceID = WLE.WriteResourceID;
    } else {
      // Assign a default latency for this write.
      Write.Latency = ID.MaxLatency;
      Write.SClassOrWriteResourceID = 0;
    }
    Write.IsOptionalDef = false;
    LLVM_DEBUG({
      dbgs() << "\t\t[Def]    OpIdx=" << Write.OpIndex
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
    CurrentDef++;
  }

  assert(CurrentDef == NumExplicitDefs &&
         "Expected more register operand definitions.");
  for (CurrentDef = 0; CurrentDef < NumImplicitDefs; ++CurrentDef) {
    unsigned Index = NumExplicitDefs + CurrentDef;
    WriteDescriptor &Write = ID.Writes[Index];
    Write.OpIndex = ~CurrentDef;
    Write.RegisterID = MCDesc.implicit_defs()[CurrentDef];
    if (Index < NumWriteLatencyEntries) {
      const MCWriteLatencyEntry &WLE =
          *STI.getWriteLatencyEntry(&SCDesc, Index);
      // Conservatively default to MaxLatency.
      Write.Latency =
          WLE.Cycles < 0 ? ID.MaxLatency : static_cast<unsigned>(WLE.Cycles);
      Write.SClassOrWriteResourceID = WLE.WriteResourceID;
    } else {
      // Assign a default latency for this write.
      Write.Latency = ID.MaxLatency;
      Write.SClassOrWriteResourceID = 0;
    }

    Write.IsOptionalDef = false;
    assert(Write.RegisterID != 0 && "Expected a valid phys register!");
    LLVM_DEBUG({
      dbgs() << "\t\t[Def][I] OpIdx=" << ~Write.OpIndex
             << ", PhysReg=" << MRI.getName(Write.RegisterID)
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
  }

  if (MCDesc.hasOptionalDef()) {
    WriteDescriptor &Write = ID.Writes[NumExplicitDefs + NumImplicitDefs];
    Write.OpIndex = OptionalDefIdx;
    // Assign a default latency for this write.
    Write.Latency = ID.MaxLatency;
    Write.SClassOrWriteResourceID = 0;
    Write.IsOptionalDef = true;
    LLVM_DEBUG({
      dbgs() << "\t\t[Def][O] OpIdx=" << Write.OpIndex
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
  }

  if (!NumVariadicOps)
    return;

  bool AssumeUsesOnly = !MCDesc.variadicOpsAreDefs();
  CurrentDef = NumExplicitDefs + NumImplicitDefs + MCDesc.hasOptionalDef();
  for (unsigned I = 0, OpIndex = MCDesc.getNumOperands();
       I < NumVariadicOps && !AssumeUsesOnly; ++I, ++OpIndex) {
    const MCOperand &Op = MCI.getOperand(OpIndex);
    if (!Op.isReg())
      continue;
    if (MRI.isConstant(Op.getReg()))
      continue;

    WriteDescriptor &Write = ID.Writes[CurrentDef];
    Write.OpIndex = OpIndex;
    // Assign a default latency for this write.
    Write.Latency = ID.MaxLatency;
    Write.SClassOrWriteResourceID = 0;
    Write.IsOptionalDef = false;
    ++CurrentDef;
    LLVM_DEBUG({
      dbgs() << "\t\t[Def][V] OpIdx=" << Write.OpIndex
             << ", Latency=" << Write.Latency
             << ", WriteResourceID=" << Write.SClassOrWriteResourceID << '\n';
    });
  }

  ID.Writes.resize(CurrentDef);
}

void InstrBuilder::populateReads(InstrDesc &ID, const MCInst &MCI,
                                 unsigned SchedClassID) {
  const MCInstrDesc &MCDesc = MCII.get(MCI.getOpcode());
  unsigned NumExplicitUses = MCDesc.getNumOperands() - MCDesc.getNumDefs();
  unsigned NumImplicitUses = MCDesc.implicit_uses().size();
  // Remove the optional definition.
  if (MCDesc.hasOptionalDef())
    --NumExplicitUses;
  unsigned NumVariadicOps = MCI.getNumOperands() - MCDesc.getNumOperands();
  unsigned TotalUses = NumExplicitUses + NumImplicitUses + NumVariadicOps;
  ID.Reads.resize(TotalUses);
  unsigned CurrentUse = 0;
  for (unsigned I = 0, OpIndex = MCDesc.getNumDefs(); I < NumExplicitUses;
       ++I, ++OpIndex) {
    const MCOperand &Op = MCI.getOperand(OpIndex);
    if (!Op.isReg())
      continue;
    if (MRI.isConstant(Op.getReg()))
      continue;

    ReadDescriptor &Read = ID.Reads[CurrentUse];
    Read.OpIndex = OpIndex;
    Read.UseIndex = I;
    Read.SchedClassID = SchedClassID;
    ++CurrentUse;
    LLVM_DEBUG(dbgs() << "\t\t[Use]    OpIdx=" << Read.OpIndex
                      << ", UseIndex=" << Read.UseIndex << '\n');
  }

  // For the purpose of ReadAdvance, implicit uses come directly after explicit
  // uses. The "UseIndex" must be updated according to that implicit layout.
  for (unsigned I = 0; I < NumImplicitUses; ++I) {
    ReadDescriptor &Read = ID.Reads[CurrentUse + I];
    Read.OpIndex = ~I;
    Read.UseIndex = NumExplicitUses + I;
    Read.RegisterID = MCDesc.implicit_uses()[I];
    if (MRI.isConstant(Read.RegisterID))
      continue;
    Read.SchedClassID = SchedClassID;
    LLVM_DEBUG(dbgs() << "\t\t[Use][I] OpIdx=" << ~Read.OpIndex
                      << ", UseIndex=" << Read.UseIndex << ", RegisterID="
                      << MRI.getName(Read.RegisterID) << '\n');
  }

  CurrentUse += NumImplicitUses;

  bool AssumeDefsOnly = MCDesc.variadicOpsAreDefs();
  for (unsigned I = 0, OpIndex = MCDesc.getNumOperands();
       I < NumVariadicOps && !AssumeDefsOnly; ++I, ++OpIndex) {
    const MCOperand &Op = MCI.getOperand(OpIndex);
    if (!Op.isReg())
      continue;

    ReadDescriptor &Read = ID.Reads[CurrentUse];
    Read.OpIndex = OpIndex;
    Read.UseIndex = NumExplicitUses + NumImplicitUses + I;
    Read.SchedClassID = SchedClassID;
    ++CurrentUse;
    LLVM_DEBUG(dbgs() << "\t\t[Use][V] OpIdx=" << Read.OpIndex
                      << ", UseIndex=" << Read.UseIndex << '\n');
  }

  ID.Reads.resize(CurrentUse);
}

hash_code hashMCOperand(const MCOperand &MCO) {
  hash_code TypeHash = hash_combine(MCO.isReg(), MCO.isImm(), MCO.isSFPImm(),
                                    MCO.isDFPImm(), MCO.isExpr(), MCO.isInst());
  if (MCO.isReg())
    return hash_combine(TypeHash, MCO.getReg());

  return TypeHash;
}

hash_code hashMCInst(const MCInst &MCI) {
  hash_code InstructionHash = hash_combine(MCI.getOpcode(), MCI.getFlags());
  for (unsigned I = 0; I < MCI.getNumOperands(); ++I) {
    InstructionHash =
        hash_combine(InstructionHash, hashMCOperand(MCI.getOperand(I)));
  }
  return InstructionHash;
}

Error InstrBuilder::verifyInstrDesc(const InstrDesc &ID,
                                    const MCInst &MCI) const {
  if (ID.NumMicroOps != 0)
    return ErrorSuccess();

  bool UsesBuffers = ID.UsedBuffers;
  bool UsesResources = !ID.Resources.empty();
  if (!UsesBuffers && !UsesResources)
    return ErrorSuccess();

  // FIXME: see PR44797. We should revisit these checks and possibly move them
  // in CodeGenSchedule.cpp.
  StringRef Message = "found an inconsistent instruction that decodes to zero "
                      "opcodes and that consumes scheduler resources.";
  return make_error<InstructionError<MCInst>>(std::string(Message), MCI);
}

Expected<unsigned> InstrBuilder::getVariantSchedClassID(const MCInst &MCI,
                                                        unsigned SchedClassID) {
  const MCSchedModel &SM = STI.getSchedModel();
  unsigned CPUID = SM.getProcessorID();
  while (SchedClassID && SM.getSchedClassDesc(SchedClassID)->isVariant())
    SchedClassID =
        STI.resolveVariantSchedClass(SchedClassID, &MCI, &MCII, CPUID);

  if (!SchedClassID) {
    return make_error<InstructionError<MCInst>>(
        "unable to resolve scheduling class for write variant.", MCI);
  }

  return SchedClassID;
}

Expected<const InstrDesc &>
InstrBuilder::createInstrDescImpl(const MCInst &MCI,
                                  const SmallVector<Instrument *> &IVec) {
  assert(STI.getSchedModel().hasInstrSchedModel() &&
         "Itineraries are not yet supported!");

  // Obtain the instruction descriptor from the opcode.
  unsigned short Opcode = MCI.getOpcode();
  const MCInstrDesc &MCDesc = MCII.get(Opcode);
  const MCSchedModel &SM = STI.getSchedModel();

  // Then obtain the scheduling class information from the instruction.
  // Allow InstrumentManager to override and use a different SchedClassID
  unsigned SchedClassID = IM.getSchedClassID(MCII, MCI, IVec);
  bool IsVariant = SM.getSchedClassDesc(SchedClassID)->isVariant();

  // Try to solve variant scheduling classes.
  if (IsVariant) {
    Expected<unsigned> VariantSchedClassIDOrErr =
        getVariantSchedClassID(MCI, SchedClassID);
    if (!VariantSchedClassIDOrErr) {
      return VariantSchedClassIDOrErr.takeError();
    }

    SchedClassID = *VariantSchedClassIDOrErr;
  }

  // Check if this instruction is supported. Otherwise, report an error.
  const MCSchedClassDesc &SCDesc = *SM.getSchedClassDesc(SchedClassID);
  if (SCDesc.NumMicroOps == MCSchedClassDesc::InvalidNumMicroOps) {
    return make_error<InstructionError<MCInst>>(
        "found an unsupported instruction in the input assembly sequence", MCI);
  }

  LLVM_DEBUG(dbgs() << "\n\t\tOpcode Name= " << MCII.getName(Opcode) << '\n');
  LLVM_DEBUG(dbgs() << "\t\tSchedClassID=" << SchedClassID << '\n');
  LLVM_DEBUG(dbgs() << "\t\tOpcode=" << Opcode << '\n');

  // Create a new empty descriptor.
  std::unique_ptr<InstrDesc> ID = std::make_unique<InstrDesc>();
  ID->NumMicroOps = SCDesc.NumMicroOps;
  ID->SchedClassID = SchedClassID;

  if (MCDesc.isCall() && FirstCallInst) {
    // We don't correctly model calls.
    WithColor::warning() << "found a call in the input assembly sequence.\n";
    WithColor::note() << "call instructions are not correctly modeled. "
                      << "Assume a latency of " << CallLatency << "cy.\n";
    FirstCallInst = false;
  }

  if (MCDesc.isReturn() && FirstReturnInst) {
    WithColor::warning() << "found a return instruction in the input"
                         << " assembly sequence.\n";
    WithColor::note() << "program counter updates are ignored.\n";
    FirstReturnInst = false;
  }

  initializeUsedResources(*ID, SCDesc, STI, ProcResourceMasks);
  computeMaxLatency(*ID, MCDesc, SCDesc, STI, CallLatency);

  if (Error Err = verifyOperands(MCDesc, MCI))
    return std::move(Err);

  populateWrites(*ID, MCI, SchedClassID);
  populateReads(*ID, MCI, SchedClassID);

  LLVM_DEBUG(dbgs() << "\t\tMaxLatency=" << ID->MaxLatency << '\n');
  LLVM_DEBUG(dbgs() << "\t\tNumMicroOps=" << ID->NumMicroOps << '\n');

  // Validation check on the instruction descriptor.
  if (Error Err = verifyInstrDesc(*ID, MCI))
    return std::move(Err);

  // Now add the new descriptor.
  bool IsVariadic = MCDesc.isVariadic();
  if ((ID->IsRecyclable = !IsVariadic && !IsVariant)) {
    auto DKey = std::make_pair(MCI.getOpcode(), SchedClassID);
    Descriptors[DKey] = std::move(ID);
    return *Descriptors[DKey];
  }

  auto VDKey = std::make_pair(hashMCInst(MCI), SchedClassID);
  assert(
      !VariantDescriptors.contains(VDKey) &&
      "Expected VariantDescriptors to not already have a value for this key.");
  VariantDescriptors[VDKey] = std::move(ID);
  return *VariantDescriptors[VDKey];
}

Expected<const InstrDesc &>
InstrBuilder::getOrCreateInstrDesc(const MCInst &MCI,
                                   const SmallVector<Instrument *> &IVec) {
  // Cache lookup using SchedClassID from Instrumentation
  unsigned SchedClassID = IM.getSchedClassID(MCII, MCI, IVec);

  auto DKey = std::make_pair(MCI.getOpcode(), SchedClassID);
  if (Descriptors.find_as(DKey) != Descriptors.end())
    return *Descriptors[DKey];

  Expected<unsigned> VariantSchedClassIDOrErr =
      getVariantSchedClassID(MCI, SchedClassID);
  if (!VariantSchedClassIDOrErr) {
    return VariantSchedClassIDOrErr.takeError();
  }

  SchedClassID = *VariantSchedClassIDOrErr;

  auto VDKey = std::make_pair(hashMCInst(MCI), SchedClassID);
  if (VariantDescriptors.contains(VDKey))
    return *VariantDescriptors[VDKey];

  return createInstrDescImpl(MCI, IVec);
}

STATISTIC(NumVariantInst, "Number of MCInsts that doesn't have static Desc");

Expected<std::unique_ptr<Instruction>>
InstrBuilder::createInstruction(const MCInst &MCI,
                                const SmallVector<Instrument *> &IVec) {
  Expected<const InstrDesc &> DescOrErr = getOrCreateInstrDesc(MCI, IVec);
  if (!DescOrErr)
    return DescOrErr.takeError();
  const InstrDesc &D = *DescOrErr;
  Instruction *NewIS = nullptr;
  std::unique_ptr<Instruction> CreatedIS;
  bool IsInstRecycled = false;

  if (!D.IsRecyclable)
    ++NumVariantInst;

  if (D.IsRecyclable && InstRecycleCB) {
    if (auto *I = InstRecycleCB(D)) {
      NewIS = I;
      NewIS->reset();
      IsInstRecycled = true;
    }
  }
  if (!IsInstRecycled) {
    CreatedIS = std::make_unique<Instruction>(D, MCI.getOpcode());
    NewIS = CreatedIS.get();
  }

  const MCInstrDesc &MCDesc = MCII.get(MCI.getOpcode());
  const MCSchedClassDesc &SCDesc =
      *STI.getSchedModel().getSchedClassDesc(D.SchedClassID);

  NewIS->setMayLoad(MCDesc.mayLoad());
  NewIS->setMayStore(MCDesc.mayStore());
  NewIS->setHasSideEffects(MCDesc.hasUnmodeledSideEffects());
  NewIS->setBeginGroup(SCDesc.BeginGroup);
  NewIS->setEndGroup(SCDesc.EndGroup);
  NewIS->setRetireOOO(SCDesc.RetireOOO);

  // Check if this is a dependency breaking instruction.
  APInt Mask;

  bool IsZeroIdiom = false;
  bool IsDepBreaking = false;
  if (MCIA) {
    unsigned ProcID = STI.getSchedModel().getProcessorID();
    IsZeroIdiom = MCIA->isZeroIdiom(MCI, Mask, ProcID);
    IsDepBreaking =
        IsZeroIdiom || MCIA->isDependencyBreaking(MCI, Mask, ProcID);
    if (MCIA->isOptimizableRegisterMove(MCI, ProcID))
      NewIS->setOptimizableMove();
  }

  // Initialize Reads first.
  MCPhysReg RegID = 0;
  size_t Idx = 0U;
  for (const ReadDescriptor &RD : D.Reads) {
    if (!RD.isImplicitRead()) {
      // explicit read.
      const MCOperand &Op = MCI.getOperand(RD.OpIndex);
      // Skip non-register operands.
      if (!Op.isReg())
        continue;
      RegID = Op.getReg();
    } else {
      // Implicit read.
      RegID = RD.RegisterID;
    }

    // Skip invalid register operands.
    if (!RegID)
      continue;

    // Okay, this is a register operand. Create a ReadState for it.
    ReadState *RS = nullptr;
    if (IsInstRecycled && Idx < NewIS->getUses().size()) {
      NewIS->getUses()[Idx] = ReadState(RD, RegID);
      RS = &NewIS->getUses()[Idx++];
    } else {
      NewIS->getUses().emplace_back(RD, RegID);
      RS = &NewIS->getUses().back();
      ++Idx;
    }

    if (IsDepBreaking) {
      // A mask of all zeroes means: explicit input operands are not
      // independent.
      if (Mask.isZero()) {
        if (!RD.isImplicitRead())
          RS->setIndependentFromDef();
      } else {
        // Check if this register operand is independent according to `Mask`.
        // Note that Mask may not have enough bits to describe all explicit and
        // implicit input operands. If this register operand doesn't have a
        // corresponding bit in Mask, then conservatively assume that it is
        // dependent.
        if (Mask.getBitWidth() > RD.UseIndex) {
          // Okay. This map describe register use `RD.UseIndex`.
          if (Mask[RD.UseIndex])
            RS->setIndependentFromDef();
        }
      }
    }
  }
  if (IsInstRecycled && Idx < NewIS->getUses().size())
    NewIS->getUses().pop_back_n(NewIS->getUses().size() - Idx);

  // Early exit if there are no writes.
  if (D.Writes.empty()) {
    if (IsInstRecycled)
      return llvm::make_error<RecycledInstErr>(NewIS);
    else
      return std::move(CreatedIS);
  }

  // Track register writes that implicitly clear the upper portion of the
  // underlying super-registers using an APInt.
  APInt WriteMask(D.Writes.size(), 0);

  // Now query the MCInstrAnalysis object to obtain information about which
  // register writes implicitly clear the upper portion of a super-register.
  if (MCIA)
    MCIA->clearsSuperRegisters(MRI, MCI, WriteMask);

  // Initialize writes.
  unsigned WriteIndex = 0;
  Idx = 0U;
  for (const WriteDescriptor &WD : D.Writes) {
    RegID = WD.isImplicitWrite() ? WD.RegisterID
                                 : MCI.getOperand(WD.OpIndex).getReg();
    // Check if this is a optional definition that references NoReg or a write
    // to a constant register.
    if ((WD.IsOptionalDef && !RegID) || MRI.isConstant(RegID)) {
      ++WriteIndex;
      continue;
    }

    assert(RegID && "Expected a valid register ID!");
    if (IsInstRecycled && Idx < NewIS->getDefs().size()) {
      NewIS->getDefs()[Idx++] =
          WriteState(WD, RegID,
                     /* ClearsSuperRegs */ WriteMask[WriteIndex],
                     /* WritesZero */ IsZeroIdiom);
    } else {
      NewIS->getDefs().emplace_back(WD, RegID,
                                    /* ClearsSuperRegs */ WriteMask[WriteIndex],
                                    /* WritesZero */ IsZeroIdiom);
      ++Idx;
    }
    ++WriteIndex;
  }
  if (IsInstRecycled && Idx < NewIS->getDefs().size())
    NewIS->getDefs().pop_back_n(NewIS->getDefs().size() - Idx);

  if (IsInstRecycled)
    return llvm::make_error<RecycledInstErr>(NewIS);
  else
    return std::move(CreatedIS);
}
} // namespace mca
} // namespace llvm
