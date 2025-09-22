//===-- M68kInstrAtomics.td - Atomics Instructions ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

foreach size = [8, 16, 32] in {
  def : Pat<(!cast<SDPatternOperator>("atomic_load_"#size) MxCP_ARI:$ptr),
            (!cast<MxInst>("MOV"#size#"dj") !cast<MxMemOp>("MxARI"#size):$ptr)>;

  def : Pat<(!cast<SDPatternOperator>("atomic_store_"#size) !cast<MxRegOp>("MxDRD"#size):$val, MxCP_ARI:$ptr),
            (!cast<MxInst>("MOV"#size#"jd") !cast<MxMemOp>("MxARI"#size):$ptr,
                                            !cast<MxRegOp>("MxDRD"#size):$val)>;
}

let Predicates = [AtLeastM68020] in {
class MxCASOp<bits<2> size_encoding, MxType type>
    : MxInst<(outs type.ROp:$out),
             (ins type.ROp:$dc, type.ROp:$du, !cast<MxMemOp>("MxARI"#type.Size):$mem),
             "cas."#type.Prefix#" $dc, $du, $mem"> {
  let Inst = (ascend
                (descend 0b00001, size_encoding, 0b011, MxEncAddrMode_j<"mem">.EA),
                (descend 0b0000000, (operand "$du", 3), 0b000, (operand "$dc", 3))
              );
  let Constraints = "$out = $dc";
  let mayLoad = 1;
  let mayStore = 1;
}

def CAS8  : MxCASOp<0x1, MxType8d>;
def CAS16 : MxCASOp<0x2, MxType16d>;
def CAS32 : MxCASOp<0x3, MxType32d>;


foreach size = [8, 16, 32] in {
  def : Pat<(!cast<SDPatternOperator>("atomic_cmp_swap_i"#size) MxCP_ARI:$ptr,
                                                                !cast<MxRegOp>("MxDRD"#size):$cmp,
                                                                !cast<MxRegOp>("MxDRD"#size):$new),
            (!cast<MxInst>("CAS"#size) !cast<MxRegOp>("MxDRD"#size):$cmp,
                                       !cast<MxRegOp>("MxDRD"#size):$new,
                                       !cast<MxMemOp>("MxARI"#size):$ptr)>;
}
} // let Predicates = [AtLeastM68020]
