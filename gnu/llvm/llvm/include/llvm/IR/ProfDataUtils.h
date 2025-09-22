//===- llvm/IR/ProfDataUtils.h - Profiling Metadata Utilities ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations for profiling metadata utility
/// functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PROFDATAUTILS_H
#define LLVM_IR_PROFDATAUTILS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Metadata.h"

namespace llvm {

/// Checks if an Instruction has MD_prof Metadata
bool hasProfMD(const Instruction &I);

/// Checks if an MDNode contains Branch Weight Metadata
bool isBranchWeightMD(const MDNode *ProfileData);

/// Checks if an instructions has Branch Weight Metadata
///
/// \param I The instruction to check
/// \returns True if I has an MD_prof node containing Branch Weights. False
/// otherwise.
bool hasBranchWeightMD(const Instruction &I);

/// Checks if an instructions has valid Branch Weight Metadata
///
/// \param I The instruction to check
/// \returns True if I has an MD_prof node containing valid Branch Weights,
/// i.e., one weight for each successor. False otherwise.
bool hasValidBranchWeightMD(const Instruction &I);

/// Get the branch weights metadata node
///
/// \param I The Instruction to get the weights from.
/// \returns A pointer to I's branch weights metadata node, if it exists.
/// Nullptr otherwise.
MDNode *getBranchWeightMDNode(const Instruction &I);

/// Get the valid branch weights metadata node
///
/// \param I The Instruction to get the weights from.
/// \returns A pointer to I's valid branch weights metadata node, if it exists.
/// Nullptr otherwise.
MDNode *getValidBranchWeightMDNode(const Instruction &I);

/// Check if Branch Weight Metadata has an "expected" field from an llvm.expect*
/// intrinsic
bool hasBranchWeightOrigin(const Instruction &I);

/// Check if Branch Weight Metadata has an "expected" field from an llvm.expect*
/// intrinsic
bool hasBranchWeightOrigin(const MDNode *ProfileData);

/// Return the offset to the first branch weight data
unsigned getBranchWeightOffset(const MDNode *ProfileData);

unsigned getNumBranchWeights(const MDNode &ProfileData);

/// Extract branch weights from MD_prof metadata
///
/// \param ProfileData A pointer to an MDNode.
/// \param [out] Weights An output vector to fill with branch weights
/// \returns True if weights were extracted, False otherwise. When false Weights
/// will be cleared.
bool extractBranchWeights(const MDNode *ProfileData,
                          SmallVectorImpl<uint32_t> &Weights);

/// Faster version of extractBranchWeights() that skips checks and must only
/// be called with "branch_weights" metadata nodes. Supports uint32_t.
void extractFromBranchWeightMD32(const MDNode *ProfileData,
                                 SmallVectorImpl<uint32_t> &Weights);

/// Faster version of extractBranchWeights() that skips checks and must only
/// be called with "branch_weights" metadata nodes. Supports uint64_t.
void extractFromBranchWeightMD64(const MDNode *ProfileData,
                                 SmallVectorImpl<uint64_t> &Weights);

/// Extract branch weights attatched to an Instruction
///
/// \param I The Instruction to extract weights from.
/// \param [out] Weights An output vector to fill with branch weights
/// \returns True if weights were extracted, False otherwise. When false Weights
/// will be cleared.
bool extractBranchWeights(const Instruction &I,
                          SmallVectorImpl<uint32_t> &Weights);

/// Extract branch weights from a conditional branch or select Instruction.
///
/// \param I The instruction to extract branch weights from.
/// \param [out] TrueVal will contain the branch weight for the True branch
/// \param [out] FalseVal will contain the branch weight for the False branch
/// \returns True on success with profile weights filled in. False if no
/// metadata or invalid metadata was found.
bool extractBranchWeights(const Instruction &I, uint64_t &TrueVal,
                          uint64_t &FalseVal);

/// Retrieve the total of all weights from MD_prof data.
///
/// \param ProfileData The profile data to extract the total weight from
/// \param [out] TotalWeights input variable to fill with total weights
/// \returns True on success with profile total weights filled in. False if no
/// metadata was found.
bool extractProfTotalWeight(const MDNode *ProfileData, uint64_t &TotalWeights);

/// Retrieve the total of all weights from an instruction.
///
/// \param I The instruction to extract the total weight from
/// \param [out] TotalWeights input variable to fill with total weights
/// \returns True on success with profile total weights filled in. False if no
/// metadata was found.
bool extractProfTotalWeight(const Instruction &I, uint64_t &TotalWeights);

/// Create a new `branch_weights` metadata node and add or overwrite
/// a `prof` metadata reference to instruction `I`.
/// \param I the Instruction to set branch weights on.
/// \param Weights an array of weights to set on instruction I.
/// \param IsExpected were these weights added from an llvm.expect* intrinsic.
void setBranchWeights(Instruction &I, ArrayRef<uint32_t> Weights,
                      bool IsExpected);

/// Scaling the profile data attached to 'I' using the ratio of S/T.
void scaleProfData(Instruction &I, uint64_t S, uint64_t T);

} // namespace llvm
#endif
