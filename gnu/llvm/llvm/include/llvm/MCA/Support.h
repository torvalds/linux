//===--------------------- Support.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Helper functions used by various pipeline components.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_SUPPORT_H
#define LLVM_MCA_SUPPORT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {
namespace mca {

template <typename T>
class InstructionError : public ErrorInfo<InstructionError<T>> {
public:
  static char ID;
  std::string Message;
  const T &Inst;

  InstructionError(std::string M, const T &MCI)
      : Message(std::move(M)), Inst(MCI) {}

  void log(raw_ostream &OS) const override { OS << Message; }

  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }
};

template <typename T> char InstructionError<T>::ID;

/// This class represents the number of cycles per resource (fractions of
/// cycles).  That quantity is managed here as a ratio, and accessed via the
/// double cast-operator below.  The two quantities, number of cycles and
/// number of resources, are kept separate.  This is used by the
/// ResourcePressureView to calculate the average resource cycles
/// per instruction/iteration.
class ReleaseAtCycles {
  unsigned Numerator, Denominator;

public:
  ReleaseAtCycles() : Numerator(0), Denominator(1) {}
  ReleaseAtCycles(unsigned Cycles, unsigned ResourceUnits = 1)
      : Numerator(Cycles), Denominator(ResourceUnits) {}

  operator double() const {
    assert(Denominator && "Invalid denominator (must be non-zero).");
    return (Denominator == 1) ? Numerator : (double)Numerator / Denominator;
  }

  unsigned getNumerator() const { return Numerator; }
  unsigned getDenominator() const { return Denominator; }

  // Add the components of RHS to this instance.  Instead of calculating
  // the final value here, we keep track of the numerator and denominator
  // separately, to reduce floating point error.
  ReleaseAtCycles &operator+=(const ReleaseAtCycles &RHS);
};

/// Populates vector Masks with processor resource masks.
///
/// The number of bits set in a mask depends on the processor resource type.
/// Each processor resource mask has at least one bit set. For groups, the
/// number of bits set in the mask is equal to the cardinality of the group plus
/// one. Excluding the most significant bit, the remaining bits in the mask
/// identify processor resources that are part of the group.
///
/// Example:
///
///  ResourceA  -- Mask: 0b001
///  ResourceB  -- Mask: 0b010
///  ResourceAB -- Mask: 0b100 U (ResourceA::Mask | ResourceB::Mask) == 0b111
///
/// ResourceAB is a processor resource group containing ResourceA and ResourceB.
/// Each resource mask uniquely identifies a resource; both ResourceA and
/// ResourceB only have one bit set.
/// ResourceAB is a group; excluding the most significant bit in the mask, the
/// remaining bits identify the composition of the group.
///
/// Resource masks are used by the ResourceManager to solve set membership
/// problems with simple bit manipulation operations.
void computeProcResourceMasks(const MCSchedModel &SM,
                              MutableArrayRef<uint64_t> Masks);

// Returns the index of the highest bit set. For resource masks, the position of
// the highest bit set can be used to construct a resource mask identifier.
inline unsigned getResourceStateIndex(uint64_t Mask) {
  assert(Mask && "Processor Resource Mask cannot be zero!");
  return llvm::Log2_64(Mask);
}

/// Compute the reciprocal block throughput from a set of processor resource
/// cycles. The reciprocal block throughput is computed as the MAX between:
///  - NumMicroOps / DispatchWidth
///  - ProcReleaseAtCycles / #ProcResourceUnits  (for every consumed resource).
double computeBlockRThroughput(const MCSchedModel &SM, unsigned DispatchWidth,
                               unsigned NumMicroOps,
                               ArrayRef<unsigned> ProcResourceUsage);
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_SUPPORT_H
