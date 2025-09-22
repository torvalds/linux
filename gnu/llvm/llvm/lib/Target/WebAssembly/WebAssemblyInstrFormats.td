//=- WebAssemblyInstrFormats.td - WebAssembly Instr. Formats -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly instruction format definitions.
///
//===----------------------------------------------------------------------===//

// WebAssembly Instruction Format.
// We instantiate 2 of these for every actual instruction (register based
// and stack based), see below.
class WebAssemblyInst<bits<32> inst, string asmstr, bit stack, bit is64>
  : StackRel, RegisterRel, Wasm64Rel, Instruction {
  bits<32> Inst = inst; // Instruction encoding.
  bit StackBased = stack;
  string BaseName = NAME;
  bit IsWasm64 = is64;
  string Wasm32Name = !subst("_A64", "_A32", NAME);
  let Namespace   = "WebAssembly";
  let Pattern     = [];
  let AsmString   = asmstr;
  // When there are multiple instructions that map to the same encoding (in
  // e.g. the disassembler use case) prefer the one where IsCanonical == 1.
  bit IsCanonical = 0;
}

// Normal instructions. Default instantiation of a WebAssemblyInst.
class NI<dag oops, dag iops, list<dag> pattern, bit stack,
         string asmstr = "", bits<32> inst = -1, bit is64 = false>
    : WebAssemblyInst<inst, asmstr, stack, is64> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let Pattern        = pattern;
  let Defs           = [ARGUMENTS];
}

// Generates both register and stack based versions of one actual instruction.
// We have 2 sets of operands (oops & iops) for the register and stack
// based version of this instruction, as well as the corresponding asmstr.
// The register versions have virtual-register operands which correspond to wasm
// locals or stack locations. Each use and def of the register corresponds to an
// implicit local.get / local.set or access of stack operands in wasm. These
// instructions are used for ISel and all MI passes. The stack versions of the
// instructions do not have register operands (they implicitly operate on the
// stack), and local.gets and local.sets are explicit. The register instructions
// are converted to their corresponding stack instructions before lowering to
// MC.
// Every instruction should want to be based on this multi-class to guarantee
// there is always an equivalent pair of instructions.
multiclass I<dag oops_r, dag iops_r, dag oops_s, dag iops_s,
             list<dag> pattern_r, string asmstr_r = "", string asmstr_s = "",
             bits<32> inst = -1, bit is64 = false> {
  let isCodeGenOnly = 1 in
  def "" : NI<oops_r, iops_r, pattern_r, false, asmstr_r, inst, is64>;
  let BaseName = NAME in
  def _S : NI<oops_s, iops_s, [], true, asmstr_s, inst, is64>;
}

// For instructions that have no register ops, so both sets are the same.
multiclass NRI<dag oops, dag iops, list<dag> pattern, string asmstr = "",
               bits<32> inst = -1> {
  defm "": I<oops, iops, oops, iops, pattern, asmstr, asmstr, inst>;
}
