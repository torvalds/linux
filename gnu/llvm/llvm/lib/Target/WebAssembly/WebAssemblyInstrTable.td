// WebAssemblyInstrTable.td - WebAssembly Table codegen support -*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly Table operand code-gen constructs.
/// Instructions that handle tables
//===----------------------------------------------------------------------===//

def WebAssemblyTableSet_t : SDTypeProfile<0, 3, [SDTCisPtrTy<1>]>;
def WebAssemblyTableSet : SDNode<"WebAssemblyISD::TABLE_SET", WebAssemblyTableSet_t,
                                 [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def WebAssemblyTableGet_t : SDTypeProfile<1, 2, [SDTCisPtrTy<1>]>;
def WebAssemblyTableGet : SDNode<"WebAssemblyISD::TABLE_GET", WebAssemblyTableGet_t,
                                 [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;


multiclass TABLE<WebAssemblyRegClass rc, string suffix> {
  let mayLoad = 1 in
  defm TABLE_GET_#rc : I<(outs rc:$res), (ins table32_op:$table, I32:$i),
                         (outs), (ins table32_op:$table),
                         [(set rc:$res, (!cast<Intrinsic>("int_wasm_table_get_" # suffix) (WebAssemblyWrapper tglobaladdr:$table), I32:$i))],
                         "table.get\t$res, $table, $i",
                         "table.get\t$table",
                         0x25>;

  let mayStore = 1 in
  defm TABLE_SET_#rc : I<(outs), (ins table32_op:$table, I32:$i, rc:$val),
                         (outs), (ins table32_op:$table),
                         [(!cast<Intrinsic>("int_wasm_table_set_" # suffix) (WebAssemblyWrapper tglobaladdr:$table), I32:$i, rc:$val)],
                         "table.set\t$table, $i, $val",
                         "table.set\t$table",
                         0x26>;

  defm TABLE_GROW_#rc : I<(outs I32:$sz), (ins table32_op:$table, rc:$val, I32:$n),
                          (outs), (ins table32_op:$table),
                          [(set I32:$sz, (!cast<Intrinsic>("int_wasm_table_grow_" # suffix) (WebAssemblyWrapper tglobaladdr:$table), rc:$val, I32:$n))],
                          "table.grow\t$sz, $table, $val, $n",
                          "table.grow\t$table",
                          0xfc0f>;

  defm TABLE_FILL_#rc : I<(outs), (ins table32_op:$table, I32:$i, rc:$val, I32:$n),
                          (outs), (ins table32_op:$table),
                          [(!cast<Intrinsic>("int_wasm_table_fill_" # suffix) (WebAssemblyWrapper tglobaladdr:$table), I32:$i, rc:$val, I32:$n)],
                          "table.fill\t$table, $i, $val, $n",
                          "table.fill\t$table",
                          0xfc11>;

  foreach vt = rc.RegTypes in {
    def : Pat<(vt (WebAssemblyTableGet (WebAssemblyWrapper tglobaladdr:$table), i32:$idx)),
              (!cast<NI>("TABLE_GET_" # rc) tglobaladdr:$table, i32:$idx)>;
    def : Pat<(WebAssemblyTableSet
               (WebAssemblyWrapper tglobaladdr:$table),
               i32:$idx,
               vt:$src),
              (!cast<NI>("TABLE_SET_" # rc) tglobaladdr:$table, i32:$idx, vt:$src)>;
  }
}

defm "" : TABLE<FUNCREF, "funcref">, Requires<[HasReferenceTypes]>;
defm "" : TABLE<EXTERNREF, "externref">, Requires<[HasReferenceTypes]>;
defm "" : TABLE<EXNREF, "exnref">,
          Requires<[HasReferenceTypes, HasExceptionHandling]>;

def : Pat<(WebAssemblyTableSet mcsym:$table, i32:$idx, funcref:$r),
          (TABLE_SET_FUNCREF mcsym:$table, i32:$idx, funcref:$r)>,
          Requires<[HasReferenceTypes]>;

defm TABLE_SIZE : I<(outs I32:$sz), (ins table32_op:$table),
                    (outs), (ins table32_op:$table),
                    [(set I32:$sz, (int_wasm_table_size (WebAssemblyWrapper tglobaladdr:$table)))],
                    "table.size\t$sz, $table",
                    "table.size\t$table",
                    0xfc10>,
                    Requires<[HasReferenceTypes]>;


defm TABLE_COPY : I<(outs), (ins table32_op:$table1, table32_op:$table2, I32:$d, I32:$s, I32:$n),
                    (outs), (ins table32_op:$table1, table32_op:$table2),
                    [(int_wasm_table_copy (WebAssemblyWrapper tglobaladdr:$table1),
                                          (WebAssemblyWrapper tglobaladdr:$table2),
                                          I32:$d, I32:$s, I32:$n)],
                    "table.copy\t$table1, $table2, $d, $s, $n",
                    "table.copy\t$table1, $table2",
                    0xfc0e>,
                    Requires<[HasReferenceTypes]>;
