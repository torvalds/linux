//===- Target.td - Define GlobalISel rules -----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent interfaces used to support
// SelectionDAG instruction selection patterns (specified in
// TargetSelectionDAG.td) when generating GlobalISel instruction selectors.
//
// This is intended as a compatibility layer, to enable reuse of target
// descriptions written for SelectionDAG without requiring explicit GlobalISel
// support.  It will eventually supersede SelectionDAG patterns.
//
//===----------------------------------------------------------------------===//

// Definitions that inherit from LLT define types that will be used in the
// GlobalISel matcher.
class LLT;

def s32 : LLT;
def s64 : LLT;
def v2s32 : LLT;
def v4s16 : LLT;
def v8s8 : LLT;

// Defines a matcher for complex operands. This is analogous to ComplexPattern
// from SelectionDAG.
//
// Definitions that inherit from this may also inherit from
// GIComplexPatternEquiv to enable the import of SelectionDAG patterns involving
// those ComplexPatterns.
class GIComplexOperandMatcher<LLT type, string matcherfn> {
  // The expected type of the root of the match.
  //
  // TODO: We should probably support, any-type, any-scalar, and multiple types
  //       in the future.
  LLT Type = type;

  // The function that determines whether the operand matches. It should be of
  // the form:
  //   ComplexRendererFn select(MachineOperand &Root) const;
  // where Root is the root of the match.  The function should return nullptr
  // on match failure, or a ComplexRendererFn that renders the operand in case
  // of a successful match.
  string MatcherFn = matcherfn;
}

// Defines a custom renderer. This is analogous to SDNodeXForm from
// SelectionDAG. Unlike SDNodeXForm, this matches a MachineInstr and
// renders directly to the result instruction without an intermediate node.
//
// Definitions that inherit from this may also inherit from GISDNodeXFormEquiv
// to enable the import of SelectionDAG patterns involving those SDNodeXForms.
class GICustomOperandRenderer<string rendererfn> {
  // The function renders the operand(s) of the matched instruction to
  // the specified instruction. It should be of the form:
  //   void render(MachineInstrBuilder &MIB, const MachineInstr &MI,
  //               int OpIdx = -1)
  //
  // If OpIdx is specified (i.e. not invalid/negative), this
  // references the source operand MI.getOperand(OpIdx). Otherwise,
  // this is the value defined by MI. This is to support the case
  // where there is no corresponding instruction to match.
  string RendererFn = rendererfn;
}
