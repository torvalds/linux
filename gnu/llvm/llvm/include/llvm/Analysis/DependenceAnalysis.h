//===-- llvm/Analysis/DependenceAnalysis.h -------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DependenceAnalysis is an LLVM pass that analyses dependences between memory
// accesses. Currently, it is an implementation of the approach described in
//
//            Practical Dependence Testing
//            Goff, Kennedy, Tseng
//            PLDI 1991
//
// There's a single entry point that analyzes the dependence between a pair
// of memory references in a function, returning either NULL, for no dependence,
// or a more-or-less detailed description of the dependence between them.
//
// This pass exists to support the DependenceGraph pass. There are two separate
// passes because there's a useful separation of concerns. A dependence exists
// if two conditions are met:
//
//    1) Two instructions reference the same memory location, and
//    2) There is a flow of control leading from one instruction to the other.
//
// DependenceAnalysis attacks the first condition; DependenceGraph will attack
// the second (it's not yet ready).
//
// Please note that this is work in progress and the interface is subject to
// change.
//
// Plausible changes:
//    Return a set of more precise dependences instead of just one dependence
//    summarizing all.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DEPENDENCEANALYSIS_H
#define LLVM_ANALYSIS_DEPENDENCEANALYSIS_H

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {
  class AAResults;
  template <typename T> class ArrayRef;
  class Loop;
  class LoopInfo;
  class ScalarEvolution;
  class SCEV;
  class SCEVConstant;
  class raw_ostream;

  /// Dependence - This class represents a dependence between two memory
  /// memory references in a function. It contains minimal information and
  /// is used in the very common situation where the compiler is unable to
  /// determine anything beyond the existence of a dependence; that is, it
  /// represents a confused dependence (see also FullDependence). In most
  /// cases (for output, flow, and anti dependences), the dependence implies
  /// an ordering, where the source must precede the destination; in contrast,
  /// input dependences are unordered.
  ///
  /// When a dependence graph is built, each Dependence will be a member of
  /// the set of predecessor edges for its destination instruction and a set
  /// if successor edges for its source instruction. These sets are represented
  /// as singly-linked lists, with the "next" fields stored in the dependence
  /// itelf.
  class Dependence {
  protected:
    Dependence(Dependence &&) = default;
    Dependence &operator=(Dependence &&) = default;

  public:
    Dependence(Instruction *Source, Instruction *Destination)
        : Src(Source), Dst(Destination) {}
    virtual ~Dependence() = default;

    /// Dependence::DVEntry - Each level in the distance/direction vector
    /// has a direction (or perhaps a union of several directions), and
    /// perhaps a distance.
    struct DVEntry {
      enum : unsigned char {
        NONE = 0,
        LT = 1,
        EQ = 2,
        LE = 3,
        GT = 4,
        NE = 5,
        GE = 6,
        ALL = 7
      };
      unsigned char Direction : 3; // Init to ALL, then refine.
      bool Scalar    : 1; // Init to true.
      bool PeelFirst : 1; // Peeling the first iteration will break dependence.
      bool PeelLast  : 1; // Peeling the last iteration will break the dependence.
      bool Splitable : 1; // Splitting the loop will break dependence.
      const SCEV *Distance = nullptr; // NULL implies no distance available.
      DVEntry()
          : Direction(ALL), Scalar(true), PeelFirst(false), PeelLast(false),
            Splitable(false) {}
    };

    /// getSrc - Returns the source instruction for this dependence.
    ///
    Instruction *getSrc() const { return Src; }

    /// getDst - Returns the destination instruction for this dependence.
    ///
    Instruction *getDst() const { return Dst; }

    /// isInput - Returns true if this is an input dependence.
    ///
    bool isInput() const;

    /// isOutput - Returns true if this is an output dependence.
    ///
    bool isOutput() const;

    /// isFlow - Returns true if this is a flow (aka true) dependence.
    ///
    bool isFlow() const;

    /// isAnti - Returns true if this is an anti dependence.
    ///
    bool isAnti() const;

    /// isOrdered - Returns true if dependence is Output, Flow, or Anti
    ///
    bool isOrdered() const { return isOutput() || isFlow() || isAnti(); }

    /// isUnordered - Returns true if dependence is Input
    ///
    bool isUnordered() const { return isInput(); }

    /// isLoopIndependent - Returns true if this is a loop-independent
    /// dependence.
    virtual bool isLoopIndependent() const { return true; }

    /// isConfused - Returns true if this dependence is confused
    /// (the compiler understands nothing and makes worst-case
    /// assumptions).
    virtual bool isConfused() const { return true; }

    /// isConsistent - Returns true if this dependence is consistent
    /// (occurs every time the source and destination are executed).
    virtual bool isConsistent() const { return false; }

    /// getLevels - Returns the number of common loops surrounding the
    /// source and destination of the dependence.
    virtual unsigned getLevels() const { return 0; }

    /// getDirection - Returns the direction associated with a particular
    /// level.
    virtual unsigned getDirection(unsigned Level) const { return DVEntry::ALL; }

    /// getDistance - Returns the distance (or NULL) associated with a
    /// particular level.
    virtual const SCEV *getDistance(unsigned Level) const { return nullptr; }

    /// Check if the direction vector is negative. A negative direction
    /// vector means Src and Dst are reversed in the actual program.
    virtual bool isDirectionNegative() const { return false; }

    /// If the direction vector is negative, normalize the direction
    /// vector to make it non-negative. Normalization is done by reversing
    /// Src and Dst, plus reversing the dependence directions and distances
    /// in the vector.
    virtual bool normalize(ScalarEvolution *SE) { return false; }

    /// isPeelFirst - Returns true if peeling the first iteration from
    /// this loop will break this dependence.
    virtual bool isPeelFirst(unsigned Level) const { return false; }

    /// isPeelLast - Returns true if peeling the last iteration from
    /// this loop will break this dependence.
    virtual bool isPeelLast(unsigned Level) const { return false; }

    /// isSplitable - Returns true if splitting this loop will break
    /// the dependence.
    virtual bool isSplitable(unsigned Level) const { return false; }

    /// isScalar - Returns true if a particular level is scalar; that is,
    /// if no subscript in the source or destination mention the induction
    /// variable associated with the loop at this level.
    virtual bool isScalar(unsigned Level) const;

    /// getNextPredecessor - Returns the value of the NextPredecessor
    /// field.
    const Dependence *getNextPredecessor() const { return NextPredecessor; }

    /// getNextSuccessor - Returns the value of the NextSuccessor
    /// field.
    const Dependence *getNextSuccessor() const { return NextSuccessor; }

    /// setNextPredecessor - Sets the value of the NextPredecessor
    /// field.
    void setNextPredecessor(const Dependence *pred) { NextPredecessor = pred; }

    /// setNextSuccessor - Sets the value of the NextSuccessor
    /// field.
    void setNextSuccessor(const Dependence *succ) { NextSuccessor = succ; }

    /// dump - For debugging purposes, dumps a dependence to OS.
    ///
    void dump(raw_ostream &OS) const;

  protected:
    Instruction *Src, *Dst;

  private:
    const Dependence *NextPredecessor = nullptr, *NextSuccessor = nullptr;
    friend class DependenceInfo;
  };

  /// FullDependence - This class represents a dependence between two memory
  /// references in a function. It contains detailed information about the
  /// dependence (direction vectors, etc.) and is used when the compiler is
  /// able to accurately analyze the interaction of the references; that is,
  /// it is not a confused dependence (see Dependence). In most cases
  /// (for output, flow, and anti dependences), the dependence implies an
  /// ordering, where the source must precede the destination; in contrast,
  /// input dependences are unordered.
  class FullDependence final : public Dependence {
  public:
    FullDependence(Instruction *Src, Instruction *Dst, bool LoopIndependent,
                   unsigned Levels);

    /// isLoopIndependent - Returns true if this is a loop-independent
    /// dependence.
    bool isLoopIndependent() const override { return LoopIndependent; }

    /// isConfused - Returns true if this dependence is confused
    /// (the compiler understands nothing and makes worst-case
    /// assumptions).
    bool isConfused() const override { return false; }

    /// isConsistent - Returns true if this dependence is consistent
    /// (occurs every time the source and destination are executed).
    bool isConsistent() const override { return Consistent; }

    /// getLevels - Returns the number of common loops surrounding the
    /// source and destination of the dependence.
    unsigned getLevels() const override { return Levels; }

    /// getDirection - Returns the direction associated with a particular
    /// level.
    unsigned getDirection(unsigned Level) const override;

    /// getDistance - Returns the distance (or NULL) associated with a
    /// particular level.
    const SCEV *getDistance(unsigned Level) const override;

    /// Check if the direction vector is negative. A negative direction
    /// vector means Src and Dst are reversed in the actual program.
    bool isDirectionNegative() const override;

    /// If the direction vector is negative, normalize the direction
    /// vector to make it non-negative. Normalization is done by reversing
    /// Src and Dst, plus reversing the dependence directions and distances
    /// in the vector.
    bool normalize(ScalarEvolution *SE) override;

    /// isPeelFirst - Returns true if peeling the first iteration from
    /// this loop will break this dependence.
    bool isPeelFirst(unsigned Level) const override;

    /// isPeelLast - Returns true if peeling the last iteration from
    /// this loop will break this dependence.
    bool isPeelLast(unsigned Level) const override;

    /// isSplitable - Returns true if splitting the loop will break
    /// the dependence.
    bool isSplitable(unsigned Level) const override;

    /// isScalar - Returns true if a particular level is scalar; that is,
    /// if no subscript in the source or destination mention the induction
    /// variable associated with the loop at this level.
    bool isScalar(unsigned Level) const override;

  private:
    unsigned short Levels;
    bool LoopIndependent;
    bool Consistent; // Init to true, then refine.
    std::unique_ptr<DVEntry[]> DV;
    friend class DependenceInfo;
  };

  /// DependenceInfo - This class is the main dependence-analysis driver.
  ///
  class DependenceInfo {
  public:
    DependenceInfo(Function *F, AAResults *AA, ScalarEvolution *SE,
                   LoopInfo *LI)
        : AA(AA), SE(SE), LI(LI), F(F) {}

    /// Handle transitive invalidation when the cached analysis results go away.
    bool invalidate(Function &F, const PreservedAnalyses &PA,
                    FunctionAnalysisManager::Invalidator &Inv);

    /// depends - Tests for a dependence between the Src and Dst instructions.
    /// Returns NULL if no dependence; otherwise, returns a Dependence (or a
    /// FullDependence) with as much information as can be gleaned.
    /// The flag PossiblyLoopIndependent should be set by the caller
    /// if it appears that control flow can reach from Src to Dst
    /// without traversing a loop back edge.
    std::unique_ptr<Dependence> depends(Instruction *Src,
                                        Instruction *Dst,
                                        bool PossiblyLoopIndependent);

    /// getSplitIteration - Give a dependence that's splittable at some
    /// particular level, return the iteration that should be used to split
    /// the loop.
    ///
    /// Generally, the dependence analyzer will be used to build
    /// a dependence graph for a function (basically a map from instructions
    /// to dependences). Looking for cycles in the graph shows us loops
    /// that cannot be trivially vectorized/parallelized.
    ///
    /// We can try to improve the situation by examining all the dependences
    /// that make up the cycle, looking for ones we can break.
    /// Sometimes, peeling the first or last iteration of a loop will break
    /// dependences, and there are flags for those possibilities.
    /// Sometimes, splitting a loop at some other iteration will do the trick,
    /// and we've got a flag for that case. Rather than waste the space to
    /// record the exact iteration (since we rarely know), we provide
    /// a method that calculates the iteration. It's a drag that it must work
    /// from scratch, but wonderful in that it's possible.
    ///
    /// Here's an example:
    ///
    ///    for (i = 0; i < 10; i++)
    ///        A[i] = ...
    ///        ... = A[11 - i]
    ///
    /// There's a loop-carried flow dependence from the store to the load,
    /// found by the weak-crossing SIV test. The dependence will have a flag,
    /// indicating that the dependence can be broken by splitting the loop.
    /// Calling getSplitIteration will return 5.
    /// Splitting the loop breaks the dependence, like so:
    ///
    ///    for (i = 0; i <= 5; i++)
    ///        A[i] = ...
    ///        ... = A[11 - i]
    ///    for (i = 6; i < 10; i++)
    ///        A[i] = ...
    ///        ... = A[11 - i]
    ///
    /// breaks the dependence and allows us to vectorize/parallelize
    /// both loops.
    const SCEV *getSplitIteration(const Dependence &Dep, unsigned Level);

    Function *getFunction() const { return F; }

  private:
    AAResults *AA;
    ScalarEvolution *SE;
    LoopInfo *LI;
    Function *F;

    /// Subscript - This private struct represents a pair of subscripts from
    /// a pair of potentially multi-dimensional array references. We use a
    /// vector of them to guide subscript partitioning.
    struct Subscript {
      const SCEV *Src;
      const SCEV *Dst;
      enum ClassificationKind { ZIV, SIV, RDIV, MIV, NonLinear } Classification;
      SmallBitVector Loops;
      SmallBitVector GroupLoops;
      SmallBitVector Group;
    };

    struct CoefficientInfo {
      const SCEV *Coeff;
      const SCEV *PosPart;
      const SCEV *NegPart;
      const SCEV *Iterations;
    };

    struct BoundInfo {
      const SCEV *Iterations;
      const SCEV *Upper[8];
      const SCEV *Lower[8];
      unsigned char Direction;
      unsigned char DirSet;
    };

    /// Constraint - This private class represents a constraint, as defined
    /// in the paper
    ///
    ///           Practical Dependence Testing
    ///           Goff, Kennedy, Tseng
    ///           PLDI 1991
    ///
    /// There are 5 kinds of constraint, in a hierarchy.
    ///   1) Any - indicates no constraint, any dependence is possible.
    ///   2) Line - A line ax + by = c, where a, b, and c are parameters,
    ///             representing the dependence equation.
    ///   3) Distance - The value d of the dependence distance;
    ///   4) Point - A point <x, y> representing the dependence from
    ///              iteration x to iteration y.
    ///   5) Empty - No dependence is possible.
    class Constraint {
    private:
      enum ConstraintKind { Empty, Point, Distance, Line, Any } Kind;
      ScalarEvolution *SE;
      const SCEV *A;
      const SCEV *B;
      const SCEV *C;
      const Loop *AssociatedLoop;

    public:
      /// isEmpty - Return true if the constraint is of kind Empty.
      bool isEmpty() const { return Kind == Empty; }

      /// isPoint - Return true if the constraint is of kind Point.
      bool isPoint() const { return Kind == Point; }

      /// isDistance - Return true if the constraint is of kind Distance.
      bool isDistance() const { return Kind == Distance; }

      /// isLine - Return true if the constraint is of kind Line.
      /// Since Distance's can also be represented as Lines, we also return
      /// true if the constraint is of kind Distance.
      bool isLine() const { return Kind == Line || Kind == Distance; }

      /// isAny - Return true if the constraint is of kind Any;
      bool isAny() const { return Kind == Any; }

      /// getX - If constraint is a point <X, Y>, returns X.
      /// Otherwise assert.
      const SCEV *getX() const;

      /// getY - If constraint is a point <X, Y>, returns Y.
      /// Otherwise assert.
      const SCEV *getY() const;

      /// getA - If constraint is a line AX + BY = C, returns A.
      /// Otherwise assert.
      const SCEV *getA() const;

      /// getB - If constraint is a line AX + BY = C, returns B.
      /// Otherwise assert.
      const SCEV *getB() const;

      /// getC - If constraint is a line AX + BY = C, returns C.
      /// Otherwise assert.
      const SCEV *getC() const;

      /// getD - If constraint is a distance, returns D.
      /// Otherwise assert.
      const SCEV *getD() const;

      /// getAssociatedLoop - Returns the loop associated with this constraint.
      const Loop *getAssociatedLoop() const;

      /// setPoint - Change a constraint to Point.
      void setPoint(const SCEV *X, const SCEV *Y, const Loop *CurrentLoop);

      /// setLine - Change a constraint to Line.
      void setLine(const SCEV *A, const SCEV *B,
                   const SCEV *C, const Loop *CurrentLoop);

      /// setDistance - Change a constraint to Distance.
      void setDistance(const SCEV *D, const Loop *CurrentLoop);

      /// setEmpty - Change a constraint to Empty.
      void setEmpty();

      /// setAny - Change a constraint to Any.
      void setAny(ScalarEvolution *SE);

      /// dump - For debugging purposes. Dumps the constraint
      /// out to OS.
      void dump(raw_ostream &OS) const;
    };

    /// establishNestingLevels - Examines the loop nesting of the Src and Dst
    /// instructions and establishes their shared loops. Sets the variables
    /// CommonLevels, SrcLevels, and MaxLevels.
    /// The source and destination instructions needn't be contained in the same
    /// loop. The routine establishNestingLevels finds the level of most deeply
    /// nested loop that contains them both, CommonLevels. An instruction that's
    /// not contained in a loop is at level = 0. MaxLevels is equal to the level
    /// of the source plus the level of the destination, minus CommonLevels.
    /// This lets us allocate vectors MaxLevels in length, with room for every
    /// distinct loop referenced in both the source and destination subscripts.
    /// The variable SrcLevels is the nesting depth of the source instruction.
    /// It's used to help calculate distinct loops referenced by the destination.
    /// Here's the map from loops to levels:
    ///            0 - unused
    ///            1 - outermost common loop
    ///          ... - other common loops
    /// CommonLevels - innermost common loop
    ///          ... - loops containing Src but not Dst
    ///    SrcLevels - innermost loop containing Src but not Dst
    ///          ... - loops containing Dst but not Src
    ///    MaxLevels - innermost loop containing Dst but not Src
    /// Consider the follow code fragment:
    ///    for (a = ...) {
    ///      for (b = ...) {
    ///        for (c = ...) {
    ///          for (d = ...) {
    ///            A[] = ...;
    ///          }
    ///        }
    ///        for (e = ...) {
    ///          for (f = ...) {
    ///            for (g = ...) {
    ///              ... = A[];
    ///            }
    ///          }
    ///        }
    ///      }
    ///    }
    /// If we're looking at the possibility of a dependence between the store
    /// to A (the Src) and the load from A (the Dst), we'll note that they
    /// have 2 loops in common, so CommonLevels will equal 2 and the direction
    /// vector for Result will have 2 entries. SrcLevels = 4 and MaxLevels = 7.
    /// A map from loop names to level indices would look like
    ///     a - 1
    ///     b - 2 = CommonLevels
    ///     c - 3
    ///     d - 4 = SrcLevels
    ///     e - 5
    ///     f - 6
    ///     g - 7 = MaxLevels
    void establishNestingLevels(const Instruction *Src,
                                const Instruction *Dst);

    unsigned CommonLevels, SrcLevels, MaxLevels;

    /// mapSrcLoop - Given one of the loops containing the source, return
    /// its level index in our numbering scheme.
    unsigned mapSrcLoop(const Loop *SrcLoop) const;

    /// mapDstLoop - Given one of the loops containing the destination,
    /// return its level index in our numbering scheme.
    unsigned mapDstLoop(const Loop *DstLoop) const;

    /// isLoopInvariant - Returns true if Expression is loop invariant
    /// in LoopNest.
    bool isLoopInvariant(const SCEV *Expression, const Loop *LoopNest) const;

    /// Makes sure all subscript pairs share the same integer type by
    /// sign-extending as necessary.
    /// Sign-extending a subscript is safe because getelementptr assumes the
    /// array subscripts are signed.
    void unifySubscriptType(ArrayRef<Subscript *> Pairs);

    /// removeMatchingExtensions - Examines a subscript pair.
    /// If the source and destination are identically sign (or zero)
    /// extended, it strips off the extension in an effort to
    /// simplify the actual analysis.
    void removeMatchingExtensions(Subscript *Pair);

    /// collectCommonLoops - Finds the set of loops from the LoopNest that
    /// have a level <= CommonLevels and are referred to by the SCEV Expression.
    void collectCommonLoops(const SCEV *Expression,
                            const Loop *LoopNest,
                            SmallBitVector &Loops) const;

    /// checkSrcSubscript - Examines the SCEV Src, returning true iff it's
    /// linear. Collect the set of loops mentioned by Src.
    bool checkSrcSubscript(const SCEV *Src,
                           const Loop *LoopNest,
                           SmallBitVector &Loops);

    /// checkDstSubscript - Examines the SCEV Dst, returning true iff it's
    /// linear. Collect the set of loops mentioned by Dst.
    bool checkDstSubscript(const SCEV *Dst,
                           const Loop *LoopNest,
                           SmallBitVector &Loops);

    /// isKnownPredicate - Compare X and Y using the predicate Pred.
    /// Basically a wrapper for SCEV::isKnownPredicate,
    /// but tries harder, especially in the presence of sign and zero
    /// extensions and symbolics.
    bool isKnownPredicate(ICmpInst::Predicate Pred,
                          const SCEV *X,
                          const SCEV *Y) const;

    /// isKnownLessThan - Compare to see if S is less than Size
    /// Another wrapper for isKnownNegative(S - max(Size, 1)) with some extra
    /// checking if S is an AddRec and we can prove lessthan using the loop
    /// bounds.
    bool isKnownLessThan(const SCEV *S, const SCEV *Size) const;

    /// isKnownNonNegative - Compare to see if S is known not to be negative
    /// Uses the fact that S comes from Ptr, which may be an inbound GEP,
    /// Proving there is no wrapping going on.
    bool isKnownNonNegative(const SCEV *S, const Value *Ptr) const;

    /// collectUpperBound - All subscripts are the same type (on my machine,
    /// an i64). The loop bound may be a smaller type. collectUpperBound
    /// find the bound, if available, and zero extends it to the Type T.
    /// (I zero extend since the bound should always be >= 0.)
    /// If no upper bound is available, return NULL.
    const SCEV *collectUpperBound(const Loop *l, Type *T) const;

    /// collectConstantUpperBound - Calls collectUpperBound(), then
    /// attempts to cast it to SCEVConstant. If the cast fails,
    /// returns NULL.
    const SCEVConstant *collectConstantUpperBound(const Loop *l, Type *T) const;

    /// classifyPair - Examines the subscript pair (the Src and Dst SCEVs)
    /// and classifies it as either ZIV, SIV, RDIV, MIV, or Nonlinear.
    /// Collects the associated loops in a set.
    Subscript::ClassificationKind classifyPair(const SCEV *Src,
                                           const Loop *SrcLoopNest,
                                           const SCEV *Dst,
                                           const Loop *DstLoopNest,
                                           SmallBitVector &Loops);

    /// testZIV - Tests the ZIV subscript pair (Src and Dst) for dependence.
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// If the dependence isn't proven to exist,
    /// marks the Result as inconsistent.
    bool testZIV(const SCEV *Src,
                 const SCEV *Dst,
                 FullDependence &Result) const;

    /// testSIV - Tests the SIV subscript pair (Src and Dst) for dependence.
    /// Things of the form [c1 + a1*i] and [c2 + a2*j], where
    /// i and j are induction variables, c1 and c2 are loop invariant,
    /// and a1 and a2 are constant.
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// Sets appropriate direction vector entry and, when possible,
    /// the distance vector entry.
    /// If the dependence isn't proven to exist,
    /// marks the Result as inconsistent.
    bool testSIV(const SCEV *Src,
                 const SCEV *Dst,
                 unsigned &Level,
                 FullDependence &Result,
                 Constraint &NewConstraint,
                 const SCEV *&SplitIter) const;

    /// testRDIV - Tests the RDIV subscript pair (Src and Dst) for dependence.
    /// Things of the form [c1 + a1*i] and [c2 + a2*j]
    /// where i and j are induction variables, c1 and c2 are loop invariant,
    /// and a1 and a2 are constant.
    /// With minor algebra, this test can also be used for things like
    /// [c1 + a1*i + a2*j][c2].
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// Marks the Result as inconsistent.
    bool testRDIV(const SCEV *Src,
                  const SCEV *Dst,
                  FullDependence &Result) const;

    /// testMIV - Tests the MIV subscript pair (Src and Dst) for dependence.
    /// Returns true if dependence disproved.
    /// Can sometimes refine direction vectors.
    bool testMIV(const SCEV *Src,
                 const SCEV *Dst,
                 const SmallBitVector &Loops,
                 FullDependence &Result) const;

    /// strongSIVtest - Tests the strong SIV subscript pair (Src and Dst)
    /// for dependence.
    /// Things of the form [c1 + a*i] and [c2 + a*i],
    /// where i is an induction variable, c1 and c2 are loop invariant,
    /// and a is a constant
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// Sets appropriate direction and distance.
    bool strongSIVtest(const SCEV *Coeff,
                       const SCEV *SrcConst,
                       const SCEV *DstConst,
                       const Loop *CurrentLoop,
                       unsigned Level,
                       FullDependence &Result,
                       Constraint &NewConstraint) const;

    /// weakCrossingSIVtest - Tests the weak-crossing SIV subscript pair
    /// (Src and Dst) for dependence.
    /// Things of the form [c1 + a*i] and [c2 - a*i],
    /// where i is an induction variable, c1 and c2 are loop invariant,
    /// and a is a constant.
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// Sets appropriate direction entry.
    /// Set consistent to false.
    /// Marks the dependence as splitable.
    bool weakCrossingSIVtest(const SCEV *SrcCoeff,
                             const SCEV *SrcConst,
                             const SCEV *DstConst,
                             const Loop *CurrentLoop,
                             unsigned Level,
                             FullDependence &Result,
                             Constraint &NewConstraint,
                             const SCEV *&SplitIter) const;

    /// ExactSIVtest - Tests the SIV subscript pair
    /// (Src and Dst) for dependence.
    /// Things of the form [c1 + a1*i] and [c2 + a2*i],
    /// where i is an induction variable, c1 and c2 are loop invariant,
    /// and a1 and a2 are constant.
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// Sets appropriate direction entry.
    /// Set consistent to false.
    bool exactSIVtest(const SCEV *SrcCoeff,
                      const SCEV *DstCoeff,
                      const SCEV *SrcConst,
                      const SCEV *DstConst,
                      const Loop *CurrentLoop,
                      unsigned Level,
                      FullDependence &Result,
                      Constraint &NewConstraint) const;

    /// weakZeroSrcSIVtest - Tests the weak-zero SIV subscript pair
    /// (Src and Dst) for dependence.
    /// Things of the form [c1] and [c2 + a*i],
    /// where i is an induction variable, c1 and c2 are loop invariant,
    /// and a is a constant. See also weakZeroDstSIVtest.
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// Sets appropriate direction entry.
    /// Set consistent to false.
    /// If loop peeling will break the dependence, mark appropriately.
    bool weakZeroSrcSIVtest(const SCEV *DstCoeff,
                            const SCEV *SrcConst,
                            const SCEV *DstConst,
                            const Loop *CurrentLoop,
                            unsigned Level,
                            FullDependence &Result,
                            Constraint &NewConstraint) const;

    /// weakZeroDstSIVtest - Tests the weak-zero SIV subscript pair
    /// (Src and Dst) for dependence.
    /// Things of the form [c1 + a*i] and [c2],
    /// where i is an induction variable, c1 and c2 are loop invariant,
    /// and a is a constant. See also weakZeroSrcSIVtest.
    /// Returns true if any possible dependence is disproved.
    /// If there might be a dependence, returns false.
    /// Sets appropriate direction entry.
    /// Set consistent to false.
    /// If loop peeling will break the dependence, mark appropriately.
    bool weakZeroDstSIVtest(const SCEV *SrcCoeff,
                            const SCEV *SrcConst,
                            const SCEV *DstConst,
                            const Loop *CurrentLoop,
                            unsigned Level,
                            FullDependence &Result,
                            Constraint &NewConstraint) const;

    /// exactRDIVtest - Tests the RDIV subscript pair for dependence.
    /// Things of the form [c1 + a*i] and [c2 + b*j],
    /// where i and j are induction variable, c1 and c2 are loop invariant,
    /// and a and b are constants.
    /// Returns true if any possible dependence is disproved.
    /// Marks the result as inconsistent.
    /// Works in some cases that symbolicRDIVtest doesn't,
    /// and vice versa.
    bool exactRDIVtest(const SCEV *SrcCoeff,
                       const SCEV *DstCoeff,
                       const SCEV *SrcConst,
                       const SCEV *DstConst,
                       const Loop *SrcLoop,
                       const Loop *DstLoop,
                       FullDependence &Result) const;

    /// symbolicRDIVtest - Tests the RDIV subscript pair for dependence.
    /// Things of the form [c1 + a*i] and [c2 + b*j],
    /// where i and j are induction variable, c1 and c2 are loop invariant,
    /// and a and b are constants.
    /// Returns true if any possible dependence is disproved.
    /// Marks the result as inconsistent.
    /// Works in some cases that exactRDIVtest doesn't,
    /// and vice versa. Can also be used as a backup for
    /// ordinary SIV tests.
    bool symbolicRDIVtest(const SCEV *SrcCoeff,
                          const SCEV *DstCoeff,
                          const SCEV *SrcConst,
                          const SCEV *DstConst,
                          const Loop *SrcLoop,
                          const Loop *DstLoop) const;

    /// gcdMIVtest - Tests an MIV subscript pair for dependence.
    /// Returns true if any possible dependence is disproved.
    /// Marks the result as inconsistent.
    /// Can sometimes disprove the equal direction for 1 or more loops.
    //  Can handle some symbolics that even the SIV tests don't get,
    /// so we use it as a backup for everything.
    bool gcdMIVtest(const SCEV *Src,
                    const SCEV *Dst,
                    FullDependence &Result) const;

    /// banerjeeMIVtest - Tests an MIV subscript pair for dependence.
    /// Returns true if any possible dependence is disproved.
    /// Marks the result as inconsistent.
    /// Computes directions.
    bool banerjeeMIVtest(const SCEV *Src,
                         const SCEV *Dst,
                         const SmallBitVector &Loops,
                         FullDependence &Result) const;

    /// collectCoefficientInfo - Walks through the subscript,
    /// collecting each coefficient, the associated loop bounds,
    /// and recording its positive and negative parts for later use.
    CoefficientInfo *collectCoeffInfo(const SCEV *Subscript,
                                      bool SrcFlag,
                                      const SCEV *&Constant) const;

    /// getPositivePart - X^+ = max(X, 0).
    ///
    const SCEV *getPositivePart(const SCEV *X) const;

    /// getNegativePart - X^- = min(X, 0).
    ///
    const SCEV *getNegativePart(const SCEV *X) const;

    /// getLowerBound - Looks through all the bounds info and
    /// computes the lower bound given the current direction settings
    /// at each level.
    const SCEV *getLowerBound(BoundInfo *Bound) const;

    /// getUpperBound - Looks through all the bounds info and
    /// computes the upper bound given the current direction settings
    /// at each level.
    const SCEV *getUpperBound(BoundInfo *Bound) const;

    /// exploreDirections - Hierarchically expands the direction vector
    /// search space, combining the directions of discovered dependences
    /// in the DirSet field of Bound. Returns the number of distinct
    /// dependences discovered. If the dependence is disproved,
    /// it will return 0.
    unsigned exploreDirections(unsigned Level,
                               CoefficientInfo *A,
                               CoefficientInfo *B,
                               BoundInfo *Bound,
                               const SmallBitVector &Loops,
                               unsigned &DepthExpanded,
                               const SCEV *Delta) const;

    /// testBounds - Returns true iff the current bounds are plausible.
    bool testBounds(unsigned char DirKind,
                    unsigned Level,
                    BoundInfo *Bound,
                    const SCEV *Delta) const;

    /// findBoundsALL - Computes the upper and lower bounds for level K
    /// using the * direction. Records them in Bound.
    void findBoundsALL(CoefficientInfo *A,
                       CoefficientInfo *B,
                       BoundInfo *Bound,
                       unsigned K) const;

    /// findBoundsLT - Computes the upper and lower bounds for level K
    /// using the < direction. Records them in Bound.
    void findBoundsLT(CoefficientInfo *A,
                      CoefficientInfo *B,
                      BoundInfo *Bound,
                      unsigned K) const;

    /// findBoundsGT - Computes the upper and lower bounds for level K
    /// using the > direction. Records them in Bound.
    void findBoundsGT(CoefficientInfo *A,
                      CoefficientInfo *B,
                      BoundInfo *Bound,
                      unsigned K) const;

    /// findBoundsEQ - Computes the upper and lower bounds for level K
    /// using the = direction. Records them in Bound.
    void findBoundsEQ(CoefficientInfo *A,
                      CoefficientInfo *B,
                      BoundInfo *Bound,
                      unsigned K) const;

    /// intersectConstraints - Updates X with the intersection
    /// of the Constraints X and Y. Returns true if X has changed.
    bool intersectConstraints(Constraint *X,
                              const Constraint *Y);

    /// propagate - Review the constraints, looking for opportunities
    /// to simplify a subscript pair (Src and Dst).
    /// Return true if some simplification occurs.
    /// If the simplification isn't exact (that is, if it is conservative
    /// in terms of dependence), set consistent to false.
    bool propagate(const SCEV *&Src,
                   const SCEV *&Dst,
                   SmallBitVector &Loops,
                   SmallVectorImpl<Constraint> &Constraints,
                   bool &Consistent);

    /// propagateDistance - Attempt to propagate a distance
    /// constraint into a subscript pair (Src and Dst).
    /// Return true if some simplification occurs.
    /// If the simplification isn't exact (that is, if it is conservative
    /// in terms of dependence), set consistent to false.
    bool propagateDistance(const SCEV *&Src,
                           const SCEV *&Dst,
                           Constraint &CurConstraint,
                           bool &Consistent);

    /// propagatePoint - Attempt to propagate a point
    /// constraint into a subscript pair (Src and Dst).
    /// Return true if some simplification occurs.
    bool propagatePoint(const SCEV *&Src,
                        const SCEV *&Dst,
                        Constraint &CurConstraint);

    /// propagateLine - Attempt to propagate a line
    /// constraint into a subscript pair (Src and Dst).
    /// Return true if some simplification occurs.
    /// If the simplification isn't exact (that is, if it is conservative
    /// in terms of dependence), set consistent to false.
    bool propagateLine(const SCEV *&Src,
                       const SCEV *&Dst,
                       Constraint &CurConstraint,
                       bool &Consistent);

    /// findCoefficient - Given a linear SCEV,
    /// return the coefficient corresponding to specified loop.
    /// If there isn't one, return the SCEV constant 0.
    /// For example, given a*i + b*j + c*k, returning the coefficient
    /// corresponding to the j loop would yield b.
    const SCEV *findCoefficient(const SCEV *Expr,
                                const Loop *TargetLoop) const;

    /// zeroCoefficient - Given a linear SCEV,
    /// return the SCEV given by zeroing out the coefficient
    /// corresponding to the specified loop.
    /// For example, given a*i + b*j + c*k, zeroing the coefficient
    /// corresponding to the j loop would yield a*i + c*k.
    const SCEV *zeroCoefficient(const SCEV *Expr,
                                const Loop *TargetLoop) const;

    /// addToCoefficient - Given a linear SCEV Expr,
    /// return the SCEV given by adding some Value to the
    /// coefficient corresponding to the specified TargetLoop.
    /// For example, given a*i + b*j + c*k, adding 1 to the coefficient
    /// corresponding to the j loop would yield a*i + (b+1)*j + c*k.
    const SCEV *addToCoefficient(const SCEV *Expr,
                                 const Loop *TargetLoop,
                                 const SCEV *Value)  const;

    /// updateDirection - Update direction vector entry
    /// based on the current constraint.
    void updateDirection(Dependence::DVEntry &Level,
                         const Constraint &CurConstraint) const;

    /// Given a linear access function, tries to recover subscripts
    /// for each dimension of the array element access.
    bool tryDelinearize(Instruction *Src, Instruction *Dst,
                        SmallVectorImpl<Subscript> &Pair);

    /// Tries to delinearize \p Src and \p Dst access functions for a fixed size
    /// multi-dimensional array. Calls tryDelinearizeFixedSizeImpl() to
    /// delinearize \p Src and \p Dst separately,
    bool tryDelinearizeFixedSize(Instruction *Src, Instruction *Dst,
                                 const SCEV *SrcAccessFn,
                                 const SCEV *DstAccessFn,
                                 SmallVectorImpl<const SCEV *> &SrcSubscripts,
                                 SmallVectorImpl<const SCEV *> &DstSubscripts);

    /// Tries to delinearize access function for a multi-dimensional array with
    /// symbolic runtime sizes.
    /// Returns true upon success and false otherwise.
    bool tryDelinearizeParametricSize(
        Instruction *Src, Instruction *Dst, const SCEV *SrcAccessFn,
        const SCEV *DstAccessFn, SmallVectorImpl<const SCEV *> &SrcSubscripts,
        SmallVectorImpl<const SCEV *> &DstSubscripts);

    /// checkSubscript - Helper function for checkSrcSubscript and
    /// checkDstSubscript to avoid duplicate code
    bool checkSubscript(const SCEV *Expr, const Loop *LoopNest,
                        SmallBitVector &Loops, bool IsSrc);
  }; // class DependenceInfo

  /// AnalysisPass to compute dependence information in a function
  class DependenceAnalysis : public AnalysisInfoMixin<DependenceAnalysis> {
  public:
    typedef DependenceInfo Result;
    Result run(Function &F, FunctionAnalysisManager &FAM);

  private:
    static AnalysisKey Key;
    friend struct AnalysisInfoMixin<DependenceAnalysis>;
  }; // class DependenceAnalysis

  /// Printer pass to dump DA results.
  struct DependenceAnalysisPrinterPass
      : public PassInfoMixin<DependenceAnalysisPrinterPass> {
    DependenceAnalysisPrinterPass(raw_ostream &OS,
                                  bool NormalizeResults = false)
        : OS(OS), NormalizeResults(NormalizeResults) {}

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

    static bool isRequired() { return true; }

  private:
    raw_ostream &OS;
    bool NormalizeResults;
  }; // class DependenceAnalysisPrinterPass

  /// Legacy pass manager pass to access dependence information
  class DependenceAnalysisWrapperPass : public FunctionPass {
  public:
    static char ID; // Class identification, replacement for typeinfo
    DependenceAnalysisWrapperPass();

    bool runOnFunction(Function &F) override;
    void releaseMemory() override;
    void getAnalysisUsage(AnalysisUsage &) const override;
    void print(raw_ostream &, const Module * = nullptr) const override;
    DependenceInfo &getDI() const;

  private:
    std::unique_ptr<DependenceInfo> info;
  }; // class DependenceAnalysisWrapperPass

  /// createDependenceAnalysisPass - This creates an instance of the
  /// DependenceAnalysis wrapper pass.
  FunctionPass *createDependenceAnalysisWrapperPass();

} // namespace llvm

#endif
