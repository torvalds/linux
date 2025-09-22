//===- CodeGenCommonISel.h - Common code between ISels ---------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares common utilities that are shared between SelectionDAG and
// GlobalISel frameworks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CODEGENCOMMONISEL_H
#define LLVM_CODEGEN_CODEGENCOMMONISEL_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include <cassert>
namespace llvm {

class BasicBlock;
enum FPClassTest : unsigned;

/// Encapsulates all of the information needed to generate a stack protector
/// check, and signals to isel when initialized that one needs to be generated.
///
/// *NOTE* The following is a high level documentation of SelectionDAG Stack
/// Protector Generation. This is now also ported be shared with GlobalISel,
/// but without any significant changes.
///
/// High Level Overview of ISel Stack Protector Generation:
///
/// Previously, the "stack protector" IR pass handled stack protector
/// generation. This necessitated splitting basic blocks at the IR level to
/// create the success/failure basic blocks in the tail of the basic block in
/// question. As a result of this, calls that would have qualified for the
/// sibling call optimization were no longer eligible for optimization since
/// said calls were no longer right in the "tail position" (i.e. the immediate
/// predecessor of a ReturnInst instruction).
///
/// Since the sibling call optimization causes the callee to reuse the caller's
/// stack, if we could delay the generation of the stack protector check until
/// later in CodeGen after the sibling call decision was made, we get both the
/// tail call optimization and the stack protector check!
///
/// A few goals in solving this problem were:
///
///   1. Preserve the architecture independence of stack protector generation.
///
///   2. Preserve the normal IR level stack protector check for platforms like
///      OpenBSD for which we support platform-specific stack protector
///      generation.
///
/// The main problem that guided the present solution is that one can not
/// solve this problem in an architecture independent manner at the IR level
/// only. This is because:
///
///   1. The decision on whether or not to perform a sibling call on certain
///      platforms (for instance i386) requires lower level information
///      related to available registers that can not be known at the IR level.
///
///   2. Even if the previous point were not true, the decision on whether to
///      perform a tail call is done in LowerCallTo in SelectionDAG (or
///      CallLowering in GlobalISel) which occurs after the Stack Protector
///      Pass. As a result, one would need to put the relevant callinst into the
///      stack protector check success basic block (where the return inst is
///      placed) and then move it back later at ISel/MI time before the
///      stack protector check if the tail call optimization failed. The MI
///      level option was nixed immediately since it would require
///      platform-specific pattern matching. The ISel level option was
///      nixed because SelectionDAG only processes one IR level basic block at a
///      time implying one could not create a DAG Combine to move the callinst.
///
/// To get around this problem:
///
///   1. SelectionDAG can only process one block at a time, we can generate
///      multiple machine basic blocks for one IR level basic block.
///      This is how we handle bit tests and switches.
///
///   2. At the MI level, tail calls are represented via a special return
///      MIInst called "tcreturn". Thus if we know the basic block in which we
///      wish to insert the stack protector check, we get the correct behavior
///      by always inserting the stack protector check right before the return
///      statement. This is a "magical transformation" since no matter where
///      the stack protector check intrinsic is, we always insert the stack
///      protector check code at the end of the BB.
///
/// Given the aforementioned constraints, the following solution was devised:
///
///   1. On platforms that do not support ISel stack protector check
///      generation, allow for the normal IR level stack protector check
///      generation to continue.
///
///   2. On platforms that do support ISel stack protector check
///      generation:
///
///     a. Use the IR level stack protector pass to decide if a stack
///        protector is required/which BB we insert the stack protector check
///        in by reusing the logic already therein.
///
///     b. After we finish selecting the basic block, we produce the validation
///        code with one of these techniques:
///          1) with a call to a guard check function
///          2) with inlined instrumentation
///
///        1) We insert a call to the check function before the terminator.
///
///        2) We first find a splice point in the parent basic block
///        before the terminator and then splice the terminator of said basic
///        block into the success basic block. Then we code-gen a new tail for
///        the parent basic block consisting of the two loads, the comparison,
///        and finally two branches to the success/failure basic blocks. We
///        conclude by code-gening the failure basic block if we have not
///        code-gened it already (all stack protector checks we generate in
///        the same function, use the same failure basic block).
class StackProtectorDescriptor {
public:
  StackProtectorDescriptor() = default;

  /// Returns true if all fields of the stack protector descriptor are
  /// initialized implying that we should/are ready to emit a stack protector.
  bool shouldEmitStackProtector() const {
    return ParentMBB && SuccessMBB && FailureMBB;
  }

  bool shouldEmitFunctionBasedCheckStackProtector() const {
    return ParentMBB && !SuccessMBB && !FailureMBB;
  }

  /// Initialize the stack protector descriptor structure for a new basic
  /// block.
  void initialize(const BasicBlock *BB, MachineBasicBlock *MBB,
                  bool FunctionBasedInstrumentation) {
    // Make sure we are not initialized yet.
    assert(!shouldEmitStackProtector() && "Stack Protector Descriptor is "
                                          "already initialized!");
    ParentMBB = MBB;
    if (!FunctionBasedInstrumentation) {
      SuccessMBB = addSuccessorMBB(BB, MBB, /* IsLikely */ true);
      FailureMBB = addSuccessorMBB(BB, MBB, /* IsLikely */ false, FailureMBB);
    }
  }

  /// Reset state that changes when we handle different basic blocks.
  ///
  /// This currently includes:
  ///
  /// 1. The specific basic block we are generating a
  /// stack protector for (ParentMBB).
  ///
  /// 2. The successor machine basic block that will contain the tail of
  /// parent mbb after we create the stack protector check (SuccessMBB). This
  /// BB is visited only on stack protector check success.
  void resetPerBBState() {
    ParentMBB = nullptr;
    SuccessMBB = nullptr;
  }

  /// Reset state that only changes when we switch functions.
  ///
  /// This currently includes:
  ///
  /// 1. FailureMBB since we reuse the failure code path for all stack
  /// protector checks created in an individual function.
  ///
  /// 2.The guard variable since the guard variable we are checking against is
  /// always the same.
  void resetPerFunctionState() { FailureMBB = nullptr; }

  MachineBasicBlock *getParentMBB() { return ParentMBB; }
  MachineBasicBlock *getSuccessMBB() { return SuccessMBB; }
  MachineBasicBlock *getFailureMBB() { return FailureMBB; }

private:
  /// The basic block for which we are generating the stack protector.
  ///
  /// As a result of stack protector generation, we will splice the
  /// terminators of this basic block into the successor mbb SuccessMBB and
  /// replace it with a compare/branch to the successor mbbs
  /// SuccessMBB/FailureMBB depending on whether or not the stack protector
  /// was violated.
  MachineBasicBlock *ParentMBB = nullptr;

  /// A basic block visited on stack protector check success that contains the
  /// terminators of ParentMBB.
  MachineBasicBlock *SuccessMBB = nullptr;

  /// This basic block visited on stack protector check failure that will
  /// contain a call to __stack_chk_fail().
  MachineBasicBlock *FailureMBB = nullptr;

  /// Add a successor machine basic block to ParentMBB. If the successor mbb
  /// has not been created yet (i.e. if SuccMBB = 0), then the machine basic
  /// block will be created. Assign a large weight if IsLikely is true.
  MachineBasicBlock *addSuccessorMBB(const BasicBlock *BB,
                                     MachineBasicBlock *ParentMBB,
                                     bool IsLikely,
                                     MachineBasicBlock *SuccMBB = nullptr);
};

/// Find the split point at which to splice the end of BB into its success stack
/// protector check machine basic block.
///
/// On many platforms, due to ABI constraints, terminators, even before register
/// allocation, use physical registers. This creates an issue for us since
/// physical registers at this point can not travel across basic
/// blocks. Luckily, selectiondag always moves physical registers into vregs
/// when they enter functions and moves them through a sequence of copies back
/// into the physical registers right before the terminator creating a
/// ``Terminator Sequence''. This function is searching for the beginning of the
/// terminator sequence so that we can ensure that we splice off not just the
/// terminator, but additionally the copies that move the vregs into the
/// physical registers.
MachineBasicBlock::iterator
findSplitPointForStackProtector(MachineBasicBlock *BB,
                                const TargetInstrInfo &TII);

/// Evaluates if the specified FP class test is better performed as the inverse
/// (i.e. fewer instructions should be required to lower it).  An example is the
/// test "inf|normal|subnormal|zero", which is an inversion of "nan".
/// \param Test The test as specified in 'is_fpclass' intrinsic invocation.
/// \returns The inverted test, or fcNone, if inversion does not produce a
/// simpler test.
FPClassTest invertFPClassTestIfSimpler(FPClassTest Test);

/// Assuming the instruction \p MI is going to be deleted, attempt to salvage
/// debug users of \p MI by writing the effect of \p MI in a DIExpression.
void salvageDebugInfoForDbgValue(const MachineRegisterInfo &MRI,
                                 MachineInstr &MI,
                                 ArrayRef<MachineOperand *> DbgUsers);

} // namespace llvm

#endif // LLVM_CODEGEN_CODEGENCOMMONISEL_H
