//===-- ASanStackFrameLayout.cpp - helper for AddressSanitizer ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definition of ComputeASanStackFrameLayout (see ASanStackFrameLayout.h).
//
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Utils/ASanStackFrameLayout.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

namespace llvm {

// We sort the stack variables by alignment (largest first) to minimize
// unnecessary large gaps due to alignment.
// It is tempting to also sort variables by size so that larger variables
// have larger redzones at both ends. But reordering will make report analysis
// harder, especially when temporary unnamed variables are present.
// So, until we can provide more information (type, line number, etc)
// for the stack variables we avoid reordering them too much.
static inline bool CompareVars(const ASanStackVariableDescription &a,
                               const ASanStackVariableDescription &b) {
  return a.Alignment > b.Alignment;
}

// We also force minimal alignment for all vars to kMinAlignment so that vars
// with e.g. alignment 1 and alignment 16 do not get reordered by CompareVars.
static const uint64_t kMinAlignment = 16;

// We want to add a full redzone after every variable.
// The larger the variable Size the larger is the redzone.
// The resulting frame size is a multiple of Alignment.
static uint64_t VarAndRedzoneSize(uint64_t Size, uint64_t Granularity,
                                  uint64_t Alignment) {
  uint64_t Res = 0;
  if (Size <= 4)  Res = 16;
  else if (Size <= 16) Res = 32;
  else if (Size <= 128) Res = Size + 32;
  else if (Size <= 512) Res = Size + 64;
  else if (Size <= 4096) Res = Size + 128;
  else                   Res = Size + 256;
  return alignTo(std::max(Res, 2 * Granularity), Alignment);
}

ASanStackFrameLayout
ComputeASanStackFrameLayout(SmallVectorImpl<ASanStackVariableDescription> &Vars,
                            uint64_t Granularity, uint64_t MinHeaderSize) {
  assert(Granularity >= 8 && Granularity <= 64 &&
         (Granularity & (Granularity - 1)) == 0);
  assert(MinHeaderSize >= 16 && (MinHeaderSize & (MinHeaderSize - 1)) == 0 &&
         MinHeaderSize >= Granularity);
  const size_t NumVars = Vars.size();
  assert(NumVars > 0);
  for (size_t i = 0; i < NumVars; i++)
    Vars[i].Alignment = std::max(Vars[i].Alignment, kMinAlignment);

  llvm::stable_sort(Vars, CompareVars);

  ASanStackFrameLayout Layout;
  Layout.Granularity = Granularity;
  Layout.FrameAlignment = std::max(Granularity, Vars[0].Alignment);
  uint64_t Offset =
      std::max(std::max(MinHeaderSize, Granularity), Vars[0].Alignment);
  assert((Offset % Granularity) == 0);
  for (size_t i = 0; i < NumVars; i++) {
    bool IsLast = i == NumVars - 1;
    uint64_t Alignment = std::max(Granularity, Vars[i].Alignment);
    (void)Alignment;  // Used only in asserts.
    uint64_t Size = Vars[i].Size;
    assert((Alignment & (Alignment - 1)) == 0);
    assert(Layout.FrameAlignment >= Alignment);
    assert((Offset % Alignment) == 0);
    assert(Size > 0);
    uint64_t NextAlignment =
        IsLast ? Granularity : std::max(Granularity, Vars[i + 1].Alignment);
    uint64_t SizeWithRedzone =
        VarAndRedzoneSize(Size, Granularity, NextAlignment);
    Vars[i].Offset = Offset;
    Offset += SizeWithRedzone;
  }
  if (Offset % MinHeaderSize) {
    Offset += MinHeaderSize - (Offset % MinHeaderSize);
  }
  Layout.FrameSize = Offset;
  assert((Layout.FrameSize % MinHeaderSize) == 0);
  return Layout;
}

SmallString<64> ComputeASanStackFrameDescription(
    const SmallVectorImpl<ASanStackVariableDescription> &Vars) {
  SmallString<2048> StackDescriptionStorage;
  raw_svector_ostream StackDescription(StackDescriptionStorage);
  StackDescription << Vars.size();

  for (const auto &Var : Vars) {
    std::string Name = Var.Name;
    if (Var.Line) {
      Name += ":";
      Name += to_string(Var.Line);
    }
    StackDescription << " " << Var.Offset << " " << Var.Size << " "
                     << Name.size() << " " << Name;
  }
  return StackDescription.str();
}

SmallVector<uint8_t, 64>
GetShadowBytes(const SmallVectorImpl<ASanStackVariableDescription> &Vars,
               const ASanStackFrameLayout &Layout) {
  assert(Vars.size() > 0);
  SmallVector<uint8_t, 64> SB;
  SB.clear();
  const uint64_t Granularity = Layout.Granularity;
  SB.resize(Vars[0].Offset / Granularity, kAsanStackLeftRedzoneMagic);
  for (const auto &Var : Vars) {
    SB.resize(Var.Offset / Granularity, kAsanStackMidRedzoneMagic);

    SB.resize(SB.size() + Var.Size / Granularity, 0);
    if (Var.Size % Granularity)
      SB.push_back(Var.Size % Granularity);
  }
  SB.resize(Layout.FrameSize / Granularity, kAsanStackRightRedzoneMagic);
  return SB;
}

SmallVector<uint8_t, 64> GetShadowBytesAfterScope(
    const SmallVectorImpl<ASanStackVariableDescription> &Vars,
    const ASanStackFrameLayout &Layout) {
  SmallVector<uint8_t, 64> SB = GetShadowBytes(Vars, Layout);
  const uint64_t Granularity = Layout.Granularity;

  for (const auto &Var : Vars) {
    assert(Var.LifetimeSize <= Var.Size);
    const uint64_t LifetimeShadowSize =
        (Var.LifetimeSize + Granularity - 1) / Granularity;
    const uint64_t Offset = Var.Offset / Granularity;
    std::fill(SB.begin() + Offset, SB.begin() + Offset + LifetimeShadowSize,
              kAsanStackUseAfterScopeMagic);
  }

  return SB;
}

} // llvm namespace
