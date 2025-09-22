//===---------------------- MicroOpQueueStage.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a stage that implements a queue of micro opcodes.
/// It can be used to simulate a hardware micro-op queue that serves opcodes to
/// the out of order backend.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_MICROOPQUEUESTAGE_H
#define LLVM_MCA_STAGES_MICROOPQUEUESTAGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

/// A stage that simulates a queue of instruction opcodes.
class MicroOpQueueStage : public Stage {
  SmallVector<InstRef, 8> Buffer;
  unsigned NextAvailableSlotIdx;
  unsigned CurrentInstructionSlotIdx;

  // Limits the number of instructions that can be written to this buffer every
  // cycle. A value of zero means that there is no limit to the instruction
  // throughput in input.
  const unsigned MaxIPC;
  unsigned CurrentIPC;

  // Number of entries that are available during this cycle.
  unsigned AvailableEntries;

  // True if instructions dispatched to this stage don't need to wait for the
  // next cycle before moving to the next stage.
  // False if this buffer acts as a one cycle delay in the execution pipeline.
  bool IsZeroLatencyStage;

  MicroOpQueueStage(const MicroOpQueueStage &Other) = delete;
  MicroOpQueueStage &operator=(const MicroOpQueueStage &Other) = delete;

  // By default, an instruction consumes a number of buffer entries equal to its
  // number of micro opcodes (see field `InstrDesc::NumMicroOpcodes`).  The
  // number of entries consumed by an instruction is normalized to the
  // minimum value between NumMicroOpcodes and the buffer size. This is to avoid
  // problems with (microcoded) instructions that generate a number of micro
  // opcodes than doesn't fit in the buffer.
  unsigned getNormalizedOpcodes(const InstRef &IR) const {
    unsigned NormalizedOpcodes =
        std::min(static_cast<unsigned>(Buffer.size()),
                 IR.getInstruction()->getDesc().NumMicroOps);
    return NormalizedOpcodes ? NormalizedOpcodes : 1U;
  }

  Error moveInstructions();

public:
  MicroOpQueueStage(unsigned Size, unsigned IPC = 0,
                    bool ZeroLatencyStage = true);

  bool isAvailable(const InstRef &IR) const override {
    if (MaxIPC && CurrentIPC == MaxIPC)
      return false;
    unsigned NormalizedOpcodes = getNormalizedOpcodes(IR);
    if (NormalizedOpcodes > AvailableEntries)
      return false;
    return true;
  }

  bool hasWorkToComplete() const override {
    return AvailableEntries != Buffer.size();
  }

  Error execute(InstRef &IR) override;
  Error cycleStart() override;
  Error cycleEnd() override;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_MICROOPQUEUESTAGE_H
