//===- NVPTXInstrFormats.td - NVPTX Instruction Formats-------*- tblgen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe NVPTX instructions format
//
//===----------------------------------------------------------------------===//

// Vector instruction type enum
class VecInstTypeEnum<bits<4> val> {
  bits<4> Value=val;
}
def VecNOP : VecInstTypeEnum<0>;

// Generic NVPTX Format

class NVPTXInst<dag outs, dag ins, string asmstr, list<dag> pattern>
  : Instruction {
  field bits<14> Inst;

  let Namespace = "NVPTX";
  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;

  // TSFlagFields
  bits<4> VecInstType = VecNOP.Value;
  bit IsSimpleMove = false;
  bit IsLoad = false;
  bit IsStore = false;

  bit IsTex = false;
  bit IsSust = false;
  bit IsSurfTexQuery = false;
  bit IsTexModeUnified = false;

  // The following field is encoded as log2 of the vector size minus one,
  // with 0 meaning the operation is not a surface instruction.  For example,
  // if IsSuld == 2, then the instruction is a suld instruction with vector size
  // 2**(2-1) = 2.
  bits<2> IsSuld = 0;

  let TSFlags{3...0}   = VecInstType;
  let TSFlags{4...4}   = IsSimpleMove;
  let TSFlags{5...5}   = IsLoad;
  let TSFlags{6...6}   = IsStore;
  let TSFlags{7}       = IsTex;
  let TSFlags{9...8}   = IsSuld;
  let TSFlags{10}      = IsSust;
  let TSFlags{11}      = IsSurfTexQuery;
  let TSFlags{12}      = IsTexModeUnified;
}
