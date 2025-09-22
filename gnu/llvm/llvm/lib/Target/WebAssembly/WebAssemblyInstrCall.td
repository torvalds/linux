//===- WebAssemblyInstrCall.td-WebAssembly Call codegen support -*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly Call operand code-gen constructs.
///
//===----------------------------------------------------------------------===//

// TODO: addr64: These currently assume the callee address is 32-bit.
// FIXME: add $type to first call_indirect asmstr (and maybe $flags)

// Call sequence markers. These have an immediate which represents the amount of
// stack space to allocate or free, which is used for varargs lowering.
let Uses = [SP32, SP64], Defs = [SP32, SP64], isCodeGenOnly = 1 in {
defm ADJCALLSTACKDOWN : NRI<(outs), (ins i32imm:$amt, i32imm:$amt2),
                            [(WebAssemblycallseq_start timm:$amt, timm:$amt2)]>;
defm ADJCALLSTACKUP : NRI<(outs), (ins i32imm:$amt, i32imm:$amt2),
                          [(WebAssemblycallseq_end timm:$amt, timm:$amt2)]>;
} // Uses = [SP32, SP64], Defs = [SP32, SP64], isCodeGenOnly = 1


let Uses = [SP32, SP64], isCall = 1 in {

// CALL should take both variadic arguments and produce variadic results, but
// this is not possible to model directly. Instead, we select calls to a
// CALL_PARAMS taking variadic arguments linked with a CALL_RESULTS that handles
// producing the call's variadic results. We recombine the two in a custom
// inserter hook after DAG ISel, so passes over MachineInstrs will only ever
// observe CALL nodes with all of the expected variadic uses and defs.
let isPseudo = 1 in
defm CALL_PARAMS :
  I<(outs), (ins function32_op:$callee, variable_ops),
    (outs), (ins function32_op:$callee), [],
    "call_params\t$callee", "call_params\t$callee", -1>;

let variadicOpsAreDefs = 1, usesCustomInserter = 1, isPseudo = 1 in
defm CALL_RESULTS :
  I<(outs), (ins variable_ops), (outs), (ins), [],
     "call_results", "call_results", -1>;

let variadicOpsAreDefs = 1, usesCustomInserter = 1, isPseudo = 1 in
defm RET_CALL_RESULTS :
  I<(outs), (ins variable_ops), (outs), (ins), [],
     "return_call_results", "return_call_results", -1>;

// Note that instructions with variable_ops have custom printers in
// WebAssemblyInstPrinter.cpp.

let variadicOpsAreDefs = 1 in
defm CALL :
  I<(outs), (ins function32_op:$callee, variable_ops),
    (outs), (ins function32_op:$callee), [],
    "call", "call\t$callee", 0x10>;

let variadicOpsAreDefs = 1 in
defm CALL_INDIRECT :
  I<(outs),
    (ins TypeIndex:$type, table32_op:$table, variable_ops),
    (outs),
    (ins TypeIndex:$type, table32_op:$table),
    [],
    "call_indirect", "call_indirect\t$type, $table", 0x11>;

let isReturn = 1, isTerminator = 1, hasCtrlDep = 1, isBarrier = 1 in
defm RET_CALL :
  I<(outs), (ins function32_op:$callee, variable_ops),
    (outs), (ins function32_op:$callee), [],
    "return_call    \t$callee", "return_call\t$callee", 0x12>,
  Requires<[HasTailCall]>;

let isReturn = 1, isTerminator = 1, hasCtrlDep = 1, isBarrier = 1 in
defm RET_CALL_INDIRECT :
  I<(outs), (ins TypeIndex:$type, table32_op:$table, variable_ops),
    (outs), (ins TypeIndex:$type, table32_op:$table), [],
    "return_call_indirect\t", "return_call_indirect\t$type, $table",
    0x13>,
  Requires<[HasTailCall]>;

} // Uses = [SP32,SP64], isCall = 1
