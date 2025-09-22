//===---- Delinearization.h - MultiDimensional Index Delinearization ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements an analysis pass that tries to delinearize all GEP
// instructions in all loops using the SCEV analysis functionality. This pass is
// only used for testing purposes: if your pass needs delinearization, please
// use the on-demand SCEVAddRecExpr::delinearize() function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DELINEARIZATION_H
#define LLVM_ANALYSIS_DELINEARIZATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class raw_ostream;
template <typename T> class SmallVectorImpl;
class GetElementPtrInst;
class Instruction;
class ScalarEvolution;
class SCEV;

/// Compute the array dimensions Sizes from the set of Terms extracted from
/// the memory access function of this SCEVAddRecExpr (second step of
/// delinearization).
void findArrayDimensions(ScalarEvolution &SE,
                         SmallVectorImpl<const SCEV *> &Terms,
                         SmallVectorImpl<const SCEV *> &Sizes,
                         const SCEV *ElementSize);

/// Collect parametric terms occurring in step expressions (first step of
/// delinearization).
void collectParametricTerms(ScalarEvolution &SE, const SCEV *Expr,
                            SmallVectorImpl<const SCEV *> &Terms);

/// Return in Subscripts the access functions for each dimension in Sizes
/// (third step of delinearization).
void computeAccessFunctions(ScalarEvolution &SE, const SCEV *Expr,
                            SmallVectorImpl<const SCEV *> &Subscripts,
                            SmallVectorImpl<const SCEV *> &Sizes);
/// Split this SCEVAddRecExpr into two vectors of SCEVs representing the
/// subscripts and sizes of an array access.
///
/// The delinearization is a 3 step process: the first two steps compute the
/// sizes of each subscript and the third step computes the access functions
/// for the delinearized array:
///
/// 1. Find the terms in the step functions
/// 2. Compute the array size
/// 3. Compute the access function: divide the SCEV by the array size
///    starting with the innermost dimensions found in step 2. The Quotient
///    is the SCEV to be divided in the next step of the recursion. The
///    Remainder is the subscript of the innermost dimension. Loop over all
///    array dimensions computed in step 2.
///
/// To compute a uniform array size for several memory accesses to the same
/// object, one can collect in step 1 all the step terms for all the memory
/// accesses, and compute in step 2 a unique array shape. This guarantees
/// that the array shape will be the same across all memory accesses.
///
/// FIXME: We could derive the result of steps 1 and 2 from a description of
/// the array shape given in metadata.
///
/// Example:
///
/// A[][n][m]
///
/// for i
///   for j
///     for k
///       A[j+k][2i][5i] =
///
/// The initial SCEV:
///
/// A[{{{0,+,2*m+5}_i, +, n*m}_j, +, n*m}_k]
///
/// 1. Find the different terms in the step functions:
/// -> [2*m, 5, n*m, n*m]
///
/// 2. Compute the array size: sort and unique them
/// -> [n*m, 2*m, 5]
/// find the GCD of all the terms = 1
/// divide by the GCD and erase constant terms
/// -> [n*m, 2*m]
/// GCD = m
/// divide by GCD -> [n, 2]
/// remove constant terms
/// -> [n]
/// size of the array is A[unknown][n][m]
///
/// 3. Compute the access function
/// a. Divide {{{0,+,2*m+5}_i, +, n*m}_j, +, n*m}_k by the innermost size m
/// Quotient: {{{0,+,2}_i, +, n}_j, +, n}_k
/// Remainder: {{{0,+,5}_i, +, 0}_j, +, 0}_k
/// The remainder is the subscript of the innermost array dimension: [5i].
///
/// b. Divide Quotient: {{{0,+,2}_i, +, n}_j, +, n}_k by next outer size n
/// Quotient: {{{0,+,0}_i, +, 1}_j, +, 1}_k
/// Remainder: {{{0,+,2}_i, +, 0}_j, +, 0}_k
/// The Remainder is the subscript of the next array dimension: [2i].
///
/// The subscript of the outermost dimension is the Quotient: [j+k].
///
/// Overall, we have: A[][n][m], and the access function: A[j+k][2i][5i].
void delinearize(ScalarEvolution &SE, const SCEV *Expr,
                 SmallVectorImpl<const SCEV *> &Subscripts,
                 SmallVectorImpl<const SCEV *> &Sizes, const SCEV *ElementSize);

/// Gathers the individual index expressions from a GEP instruction.
///
/// This function optimistically assumes the GEP references into a fixed size
/// array. If this is actually true, this function returns a list of array
/// subscript expressions in \p Subscripts and a list of integers describing
/// the size of the individual array dimensions in \p Sizes. Both lists have
/// either equal length or the size list is one element shorter in case there
/// is no known size available for the outermost array dimension. Returns true
/// if successful and false otherwise.
bool getIndexExpressionsFromGEP(ScalarEvolution &SE,
                                const GetElementPtrInst *GEP,
                                SmallVectorImpl<const SCEV *> &Subscripts,
                                SmallVectorImpl<int> &Sizes);

/// Implementation of fixed size array delinearization. Try to delinearize
/// access function for a fixed size multi-dimensional array, by deriving
/// subscripts from GEP instructions. Returns true upon success and false
/// otherwise. \p Inst is the load/store instruction whose pointer operand is
/// the one we want to delinearize. \p AccessFn is its corresponding SCEV
/// expression w.r.t. the surrounding loop.
bool tryDelinearizeFixedSizeImpl(ScalarEvolution *SE, Instruction *Inst,
                                 const SCEV *AccessFn,
                                 SmallVectorImpl<const SCEV *> &Subscripts,
                                 SmallVectorImpl<int> &Sizes);

struct DelinearizationPrinterPass
    : public PassInfoMixin<DelinearizationPrinterPass> {
  explicit DelinearizationPrinterPass(raw_ostream &OS);
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  raw_ostream &OS;
};
} // namespace llvm

#endif // LLVM_ANALYSIS_DELINEARIZATION_H
