//===-- TargetMacroFusion.td - Target Macro Fusion ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TableGen-based macro fusion classes.

// The target instruction that FusionPredicate will be evaluated on.
class FusionTarget;
def first_fusion_target : FusionTarget;
def second_fusion_target : FusionTarget;
def both_fusion_target : FusionTarget;

// Base class of FusionPredicate, etc. The avaliable variables are:
// * const TargetInstrInfo &TII
// * const TargetSubtargetInfo &STI
// * const MachineRegisterInfo &MRI
// * const MachineInstr *FirstMI
// * const MachineInstr &SecondMI
class FusionPredicate<FusionTarget target> {
  FusionTarget Target = target;
}
class FirstFusionPredicate: FusionPredicate<first_fusion_target>;
class SecondFusionPredicate: FusionPredicate<second_fusion_target>;
class BothFusionPredicate: FusionPredicate<both_fusion_target>;

// FusionPredicate with raw code predicate.
class FusionPredicateWithCode<code pred> : FusionPredicate<both_fusion_target> {
  code Predicate = pred;
}

// FusionPredicate with MCInstPredicate.
class FusionPredicateWithMCInstPredicate<FusionTarget target, MCInstPredicate pred>
  : FusionPredicate<target> {
  MCInstPredicate Predicate = pred;
}
class FirstFusionPredicateWithMCInstPredicate<MCInstPredicate pred>
  : FusionPredicateWithMCInstPredicate<first_fusion_target, pred>;
class SecondFusionPredicateWithMCInstPredicate<MCInstPredicate pred>
  : FusionPredicateWithMCInstPredicate<second_fusion_target, pred>;
// The pred will be applied on both firstMI and secondMI.
class BothFusionPredicateWithMCInstPredicate<MCInstPredicate pred>
  : FusionPredicateWithMCInstPredicate<both_fusion_target, pred>;

// Tie firstOpIdx and secondOpIdx. The operand of `FirstMI` at position
// `firstOpIdx` should be the same as the operand of `SecondMI` at position
// `secondOpIdx`.
// If the fusion has `IsCommutable` being true and the operand at `secondOpIdx`
// has commutable operand, then the commutable operand will be checked too.
class TieReg<int firstOpIdx, int secondOpIdx> : BothFusionPredicate {
  int FirstOpIdx = firstOpIdx;
  int SecondOpIdx = secondOpIdx;
}

// The operand of `SecondMI` at position `firstOpIdx` should be the same as the
// operand at position `secondOpIdx`.
// If the fusion has `IsCommutable` being true and the operand at `secondOpIdx`
// has commutable operand, then the commutable operand will be checked too.
class SameReg<int firstOpIdx, int secondOpIdx> : SecondFusionPredicate {
  int FirstOpIdx = firstOpIdx;
  int SecondOpIdx = secondOpIdx;
}

// A predicate for wildcard. The generated code will be like:
// ```
// if (!FirstMI)
//   return ReturnValue;
// ```
class WildcardPred<bit ret> : FirstFusionPredicate {
  bit ReturnValue = ret;
}
def WildcardFalse : WildcardPred<0>;
def WildcardTrue : WildcardPred<1>;

// Indicates that the destination register of `FirstMI` should have one use if
// it is a virtual register.
class OneUsePred : FirstFusionPredicate;
def OneUse : OneUsePred;

// Handled by MacroFusionPredicatorEmitter backend.
// The generated predicator will be like:
// ```
// bool isNAME(const TargetInstrInfo &TII,
//             const TargetSubtargetInfo &STI,
//             const MachineInstr *FirstMI,
//             const MachineInstr &SecondMI) {
//   auto &MRI = SecondMI.getMF()->getRegInfo();
//   /* Predicates */
//   return true;
// }
// ```
//
// `IsCommutable` means whether we should handle commutable operands.
class Fusion<string name, string fieldName, string desc, list<FusionPredicate> predicates>
  : SubtargetFeature<name, fieldName, "true", desc> {
  list<FusionPredicate> Predicates = predicates;
  bit IsCommutable = 0;
}

// The generated predicator will be like:
// ```
// bool isNAME(const TargetInstrInfo &TII,
//             const TargetSubtargetInfo &STI,
//             const MachineInstr *FirstMI,
//             const MachineInstr &SecondMI) {
//   auto &MRI = SecondMI.getMF()->getRegInfo();
//   /* Prolog */
//   /* Predicate for `SecondMI` */
//   /* Wildcard */
//   /* Predicate for `FirstMI` */
//   /* Check same registers */
//   /* Check One Use */
//   /* Tie registers */
//   /* Epilog */
//   return true;
// }
// ```
class SimpleFusion<string name, string fieldName, string desc,
                   MCInstPredicate firstPred, MCInstPredicate secondPred,
                   list<FusionPredicate> prolog = [],
                   list<FusionPredicate> epilog = []>
  : Fusion<name, fieldName, desc,
           !listconcat(
              prolog,
              [
                SecondFusionPredicateWithMCInstPredicate<secondPred>,
                WildcardTrue,
                FirstFusionPredicateWithMCInstPredicate<firstPred>,
                SameReg<0, 1>,
                OneUse,
                TieReg<0, 1>,
              ],
              epilog)>;

class SingleFusion<string name, string fieldName, string desc,
                   Instruction firstInst, Instruction secondInst,
                   MCInstPredicate firstInstPred = TruePred,
                   MCInstPredicate secondInstPred = TruePred,
                   list<FusionPredicate> prolog = [],
                   list<FusionPredicate> epilog = []>
  : SimpleFusion<name, fieldName, desc,
                 CheckAll<!listconcat(
                            [CheckOpcode<[firstInst]>],
                            [firstInstPred])>,
                 CheckAll<!listconcat(
                            [CheckOpcode<[secondInst]>],
                            [secondInstPred])>,
                 prolog, epilog> {
  let IsCommutable = secondInst.isCommutable;
}
