// WebAssemblyInstrBulkMemory.td - bulk memory codegen support --*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly bulk memory codegen constructs.
///
//===----------------------------------------------------------------------===//

// Instruction requiring HasBulkMemory and the bulk memory prefix byte
multiclass BULK_I<dag oops_r, dag iops_r, dag oops_s, dag iops_s,
                  list<dag> pattern_r, string asmstr_r = "",
                  string asmstr_s = "", bits<32> simdop = -1> {
  defm "" : I<oops_r, iops_r, oops_s, iops_s, pattern_r, asmstr_r, asmstr_s,
              !or(0xfc00, !and(0xff, simdop))>,
            Requires<[HasBulkMemory]>;
}

// Bespoke types and nodes for bulk memory ops
def wasm_memcpy_t : SDTypeProfile<0, 5,
  [SDTCisInt<0>, SDTCisInt<1>, SDTCisPtrTy<2>, SDTCisPtrTy<3>, SDTCisInt<4>]
>;
def wasm_memcpy : SDNode<"WebAssemblyISD::MEMORY_COPY", wasm_memcpy_t,
                         [SDNPHasChain, SDNPMayLoad, SDNPMayStore]>;

def wasm_memset_t : SDTypeProfile<0, 4,
  [SDTCisInt<0>, SDTCisPtrTy<1>, SDTCisInt<2>, SDTCisInt<3>]
>;
def wasm_memset : SDNode<"WebAssemblyISD::MEMORY_FILL", wasm_memset_t,
                         [SDNPHasChain, SDNPMayStore]>;

multiclass BulkMemoryOps<WebAssemblyRegClass rc, string B> {

let mayStore = 1, hasSideEffects = 1 in
defm MEMORY_INIT_A#B :
  BULK_I<(outs),
         (ins i32imm_op:$seg, i32imm_op:$idx, rc:$dest,
              I32:$offset, I32:$size),
         (outs), (ins i32imm_op:$seg, i32imm_op:$idx),
         [],
         "memory.init\t$seg, $idx, $dest, $offset, $size",
         "memory.init\t$seg, $idx", 0x08>;

let hasSideEffects = 1 in
defm DATA_DROP :
  BULK_I<(outs), (ins i32imm_op:$seg), (outs), (ins i32imm_op:$seg),
         [],
         "data.drop\t$seg", "data.drop\t$seg", 0x09>;

let mayLoad = 1, mayStore = 1 in
defm MEMORY_COPY_A#B :
  BULK_I<(outs), (ins i32imm_op:$src_idx, i32imm_op:$dst_idx,
                      rc:$dst, rc:$src, rc:$len),
         (outs), (ins i32imm_op:$src_idx, i32imm_op:$dst_idx),
         [(wasm_memcpy (i32 imm:$src_idx), (i32 imm:$dst_idx),
           rc:$dst, rc:$src, rc:$len
         )],
         "memory.copy\t$src_idx, $dst_idx, $dst, $src, $len",
         "memory.copy\t$src_idx, $dst_idx", 0x0a>;

let mayStore = 1 in
defm MEMORY_FILL_A#B :
  BULK_I<(outs), (ins i32imm_op:$idx, rc:$dst, I32:$value, rc:$size),
         (outs), (ins i32imm_op:$idx),
         [(wasm_memset (i32 imm:$idx), rc:$dst, I32:$value, rc:$size)],
         "memory.fill\t$idx, $dst, $value, $size",
         "memory.fill\t$idx", 0x0b>;
}

defm : BulkMemoryOps<I32, "32">;
defm : BulkMemoryOps<I64, "64">;
