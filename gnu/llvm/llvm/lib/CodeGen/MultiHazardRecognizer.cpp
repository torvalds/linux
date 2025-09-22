//===- MultiHazardRecognizer.cpp - Scheduler Support ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MultiHazardRecognizer class, which is a wrapper
// for a set of ScheduleHazardRecognizer instances
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MultiHazardRecognizer.h"
#include "llvm/ADT/STLExtras.h"
#include <algorithm>
#include <functional>
#include <numeric>

using namespace llvm;

void MultiHazardRecognizer::AddHazardRecognizer(
    std::unique_ptr<ScheduleHazardRecognizer> &&R) {
  MaxLookAhead = std::max(MaxLookAhead, R->getMaxLookAhead());
  Recognizers.push_back(std::move(R));
}

bool MultiHazardRecognizer::atIssueLimit() const {
  return llvm::any_of(Recognizers,
                      std::mem_fn(&ScheduleHazardRecognizer::atIssueLimit));
}

ScheduleHazardRecognizer::HazardType
MultiHazardRecognizer::getHazardType(SUnit *SU, int Stalls) {
  for (auto &R : Recognizers) {
    auto res = R->getHazardType(SU, Stalls);
    if (res != NoHazard)
      return res;
  }
  return NoHazard;
}

void MultiHazardRecognizer::Reset() {
  for (auto &R : Recognizers)
    R->Reset();
}

void MultiHazardRecognizer::EmitInstruction(SUnit *SU) {
  for (auto &R : Recognizers)
    R->EmitInstruction(SU);
}

void MultiHazardRecognizer::EmitInstruction(MachineInstr *MI) {
  for (auto &R : Recognizers)
    R->EmitInstruction(MI);
}

unsigned MultiHazardRecognizer::PreEmitNoops(SUnit *SU) {
  auto MN = [=](unsigned a, std::unique_ptr<ScheduleHazardRecognizer> &R) {
    return std::max(a, R->PreEmitNoops(SU));
  };
  return std::accumulate(Recognizers.begin(), Recognizers.end(), 0u, MN);
}

unsigned MultiHazardRecognizer::PreEmitNoops(MachineInstr *MI) {
  auto MN = [=](unsigned a, std::unique_ptr<ScheduleHazardRecognizer> &R) {
    return std::max(a, R->PreEmitNoops(MI));
  };
  return std::accumulate(Recognizers.begin(), Recognizers.end(), 0u, MN);
}

bool MultiHazardRecognizer::ShouldPreferAnother(SUnit *SU) {
  auto SPA = [=](std::unique_ptr<ScheduleHazardRecognizer> &R) {
    return R->ShouldPreferAnother(SU);
  };
  return llvm::any_of(Recognizers, SPA);
}

void MultiHazardRecognizer::AdvanceCycle() {
  for (auto &R : Recognizers)
    R->AdvanceCycle();
}

void MultiHazardRecognizer::RecedeCycle() {
  for (auto &R : Recognizers)
    R->RecedeCycle();
}

void MultiHazardRecognizer::EmitNoop() {
  for (auto &R : Recognizers)
    R->EmitNoop();
}
