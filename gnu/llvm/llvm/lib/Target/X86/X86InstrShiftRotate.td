//===-- X86InstrShiftRotate.td - Shift and Rotate Instrs ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the shift and rotate instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Shift/Rotate instructions
//===----------------------------------------------------------------------===//

multiclass ShiftRotate<string m, Format RegMRM, Format MemMRM, SDPatternOperator node,
                       SchedReadWrite rCL, SchedReadWrite ri, SchedReadWrite mCL,
                       SchedReadWrite mi, list<Register> uses = []> {

  let Uses = uses in {
    let isConvertibleToThreeAddress = !if(!eq(m, "shl"), 1, 0) in {
      let Predicates = [NoNDD] in {
        def 8ri  : BinOpRI8U_R<m, RegMRM, Xi8, node>, Sched<[ri]>, DefEFLAGS;
        def 16ri : BinOpRI8U_R<m, RegMRM, Xi16, node>, Sched<[ri]>, DefEFLAGS, OpSize16;
        def 32ri : BinOpRI8U_R<m, RegMRM, Xi32, node>, Sched<[ri]>, DefEFLAGS, OpSize32;
        def 64ri : BinOpRI8U_R<m, RegMRM, Xi64, node>, Sched<[ri]>, DefEFLAGS;
      }
      let Predicates = [HasNDD, In64BitMode] in {
        def 8ri_ND  : BinOpRI8U_R<m, RegMRM, Xi8, node, 1>, Sched<[ri]>, DefEFLAGS;
        def 16ri_ND : BinOpRI8U_R<m, RegMRM, Xi16, node, 1>, Sched<[ri]>, DefEFLAGS, PD;
        def 32ri_ND : BinOpRI8U_R<m, RegMRM, Xi32, node, 1>, Sched<[ri]>, DefEFLAGS;
        def 64ri_ND : BinOpRI8U_R<m, RegMRM, Xi64, node, 1>, Sched<[ri]>, DefEFLAGS;
      }
      let Predicates = [In64BitMode] in {
        def 8ri_EVEX  : BinOpRI8U_R<m, RegMRM, Xi8>, Sched<[ri]>, DefEFLAGS, PL;
        def 16ri_EVEX : BinOpRI8U_R<m, RegMRM, Xi16>, Sched<[ri]>, DefEFLAGS, PL, PD;
        def 32ri_EVEX : BinOpRI8U_R<m, RegMRM, Xi32>, Sched<[ri]>, DefEFLAGS, PL;
        def 64ri_EVEX : BinOpRI8U_R<m, RegMRM, Xi64>, Sched<[ri]>, DefEFLAGS, PL;
      }
    }

    def 8mi  : BinOpMI8U_M<m, MemMRM, Xi8, node>, Sched<[mi, WriteRMW]>, DefEFLAGS;
    def 16mi : BinOpMI8U_M<m, MemMRM, Xi16, node>, Sched<[mi, WriteRMW]>, DefEFLAGS, OpSize16;
    def 32mi : BinOpMI8U_M<m, MemMRM, Xi32, node>, Sched<[mi, WriteRMW]>, DefEFLAGS, OpSize32;
    def 64mi : BinOpMI8U_M<m, MemMRM, Xi64, node>, Sched<[mi, WriteRMW]>, DefEFLAGS, Requires<[In64BitMode]>;
    let Predicates = [HasNDD, In64BitMode] in {
      def 8mi_ND  : BinOpMI8U_R<m, MemMRM, Xi8, node>, Sched<[mi, ri]>, DefEFLAGS;
      def 16mi_ND : BinOpMI8U_R<m, MemMRM, Xi16, node>, Sched<[mi, ri]>, DefEFLAGS, PD;
      def 32mi_ND : BinOpMI8U_R<m, MemMRM, Xi32, node>, Sched<[mi, ri]>, DefEFLAGS;
      def 64mi_ND : BinOpMI8U_R<m, MemMRM, Xi64, node>, Sched<[mi, ri]>, DefEFLAGS;
    }
    let Predicates = [In64BitMode] in {
      def 8mi_EVEX  : BinOpMI8U_M<m, MemMRM, Xi8>, Sched<[mi, WriteRMW]>, DefEFLAGS, PL;
      def 16mi_EVEX : BinOpMI8U_M<m, MemMRM, Xi16>, Sched<[mi, WriteRMW]>, DefEFLAGS, PL, PD;
      def 32mi_EVEX : BinOpMI8U_M<m, MemMRM, Xi32>, Sched<[mi, WriteRMW]>, DefEFLAGS, PL;
      def 64mi_EVEX : BinOpMI8U_M<m, MemMRM, Xi64>, Sched<[mi, WriteRMW]>, DefEFLAGS, PL;
    }

    let SchedRW = [ri] in {
      def 8r1  : UnaryOpR_RF<0xD1, RegMRM, m, Xi8>;
      def 16r1 : UnaryOpR_RF<0xD1, RegMRM, m, Xi16>, OpSize16;
      def 32r1 : UnaryOpR_RF<0xD1, RegMRM, m, Xi32>, OpSize32;
      def 64r1 : UnaryOpR_RF<0xD1, RegMRM, m, Xi64>;

      // FIXME: Assembler can't tell whether it's 8r1_ND or 8rCL when the source register is cl, e.g.
      //
      //  shlb %cl, %al
      //
      // GNU binutils distinguish them by adding an explicit $1 to asm string of 8r1_ND. But we haven't support
      // constant immediate in asm string for X86 in TD. So we add DisassembleOnly for 8r1_ND for the time being.
      let Predicates = [In64BitMode] in {
        def 8r1_ND  : UnaryOpR_RF<0xD1, RegMRM, m, Xi8, null_frag, 1>, DisassembleOnly;
        def 16r1_ND : UnaryOpR_RF<0xD1, RegMRM, m, Xi16, null_frag, 1>, PD;
        def 32r1_ND : UnaryOpR_RF<0xD1, RegMRM, m, Xi32, null_frag, 1>;
        def 64r1_ND : UnaryOpR_RF<0xD1, RegMRM, m, Xi64, null_frag, 1>;

        def 8r1_EVEX  : UnaryOpR_RF<0xD1, RegMRM, m, Xi8>, PL;
        def 16r1_EVEX : UnaryOpR_RF<0xD1, RegMRM, m, Xi16>, PL, PD;
        def 32r1_EVEX : UnaryOpR_RF<0xD1, RegMRM, m, Xi32>, PL;
        def 64r1_EVEX : UnaryOpR_RF<0xD1, RegMRM, m, Xi64>, PL;
      }
    }

    let SchedRW = [mi, WriteRMW] in {
      def 8m1  : UnaryOpM_MF<0xD1, MemMRM, m, Xi8>;
      def 16m1 : UnaryOpM_MF<0xD1, MemMRM, m, Xi16>, OpSize16;
      def 32m1 : UnaryOpM_MF<0xD1, MemMRM, m, Xi32>, OpSize32;
      def 64m1 : UnaryOpM_MF<0xD1, MemMRM, m, Xi64>, Requires<[In64BitMode]>;

      let Predicates = [In64BitMode] in {
        def 8m1_EVEX  : UnaryOpM_MF<0xD1, MemMRM, m, Xi8>, PL;
        def 16m1_EVEX : UnaryOpM_MF<0xD1, MemMRM, m, Xi16>, PL, PD;
        def 32m1_EVEX : UnaryOpM_MF<0xD1, MemMRM, m, Xi32>, PL;
        def 64m1_EVEX : UnaryOpM_MF<0xD1, MemMRM, m, Xi64>, PL;
      }
    }
    let SchedRW = [mi, ri], Predicates = [In64BitMode] in {
      def 8m1_ND  : UnaryOpM_RF<0xD1, MemMRM, m, Xi8>;
      def 16m1_ND : UnaryOpM_RF<0xD1, MemMRM, m, Xi16>, PD;
      def 32m1_ND : UnaryOpM_RF<0xD1, MemMRM, m, Xi32>;
      def 64m1_ND : UnaryOpM_RF<0xD1, MemMRM, m, Xi64>;
    }
  }

  let Uses = !listconcat([CL], uses), Defs = [EFLAGS] in {
    let Predicates = [NoNDD] in {
      def 8rCL  : BinOpRC_R<m, RegMRM, Xi8, node>, Sched<[rCL]>;
      def 16rCL : BinOpRC_R<m, RegMRM, Xi16, node>, Sched<[rCL]>, OpSize16;
      def 32rCL : BinOpRC_R<m, RegMRM, Xi32, node>, Sched<[rCL]>, OpSize32;
      def 64rCL : BinOpRC_R<m, RegMRM, Xi64, node>, Sched<[rCL]>;
    }
    let Predicates = [HasNDD, In64BitMode] in {
      def 8rCL_ND  : BinOpRC_R<m, RegMRM, Xi8, node, 1>, Sched<[rCL]>;
      def 16rCL_ND : BinOpRC_R<m, RegMRM, Xi16, node, 1>, Sched<[rCL]>, PD;
      def 32rCL_ND : BinOpRC_R<m, RegMRM, Xi32, node, 1>, Sched<[rCL]>;
      def 64rCL_ND : BinOpRC_R<m, RegMRM, Xi64, node, 1>, Sched<[rCL]>;
    }
    let Predicates = [In64BitMode] in {
      def 8rCL_EVEX  : BinOpRC_R<m, RegMRM, Xi8>, Sched<[rCL]>, PL;
      def 16rCL_EVEX : BinOpRC_R<m, RegMRM, Xi16>, Sched<[rCL]>, PL, PD;
      def 32rCL_EVEX : BinOpRC_R<m, RegMRM, Xi32>, Sched<[rCL]>, PL;
      def 64rCL_EVEX : BinOpRC_R<m, RegMRM, Xi64>, Sched<[rCL]>, PL;
    }

    def 8mCL  : BinOpMC_M<m, MemMRM, Xi8, node>, Sched<[mCL, WriteRMW]>;
    def 16mCL : BinOpMC_M<m, MemMRM, Xi16, node>, Sched<[mCL, WriteRMW]>, OpSize16;
    def 32mCL : BinOpMC_M<m, MemMRM, Xi32, node>, Sched<[mCL, WriteRMW]>, OpSize32;
    def 64mCL : BinOpMC_M<m, MemMRM, Xi64, node>, Sched<[mCL, WriteRMW]>, Requires<[In64BitMode]>;

    let Predicates = [HasNDD, In64BitMode] in {
      def 8mCL_ND  : BinOpMC_R<m, MemMRM, Xi8, node>, Sched<[mCL, rCL]>;
      def 16mCL_ND : BinOpMC_R<m, MemMRM, Xi16, node>, Sched<[mCL, rCL]>, PD;
      def 32mCL_ND : BinOpMC_R<m, MemMRM, Xi32, node>, Sched<[mCL, rCL]>;
      def 64mCL_ND : BinOpMC_R<m, MemMRM, Xi64, node>, Sched<[mCL, rCL]>;
    }

    let Predicates = [In64BitMode] in {
      def 8mCL_EVEX  : BinOpMC_M<m, MemMRM, Xi8>, Sched<[mCL, WriteRMW]>, PL;
      def 16mCL_EVEX : BinOpMC_M<m, MemMRM, Xi16>, Sched<[mCL, WriteRMW]>, PL, PD;
      def 32mCL_EVEX : BinOpMC_M<m, MemMRM, Xi32>, Sched<[mCL, WriteRMW]>, PL;
      def 64mCL_EVEX : BinOpMC_M<m, MemMRM, Xi64>, Sched<[mCL, WriteRMW]>, PL;
    }
  }
}

multiclass ShiftRotate_NF<string m, Format RegMRM, Format MemMRM, SchedReadWrite rCL,
                          SchedReadWrite ri, SchedReadWrite mCL, SchedReadWrite mi> {
  let Predicates = [In64BitMode] in {
    let isConvertibleToThreeAddress = !if(!eq(m, "shl"), 1, 0) in {
      def 8ri_NF  : BinOpRI8U_R<m, RegMRM, Xi8>, Sched<[ri]>, NF;
      def 16ri_NF : BinOpRI8U_R<m, RegMRM, Xi16>, Sched<[ri]>, NF, PD;
      def 32ri_NF : BinOpRI8U_R<m, RegMRM, Xi32>, Sched<[ri]>, NF;
      def 64ri_NF : BinOpRI8U_R<m, RegMRM, Xi64>, Sched<[ri]>, NF;

      def 8ri_NF_ND  : BinOpRI8U_R<m, RegMRM, Xi8, null_frag, 1>, Sched<[ri]>, EVEX_NF;
      def 16ri_NF_ND : BinOpRI8U_R<m, RegMRM, Xi16, null_frag, 1>, Sched<[ri]>, EVEX_NF, PD;
      def 32ri_NF_ND : BinOpRI8U_R<m, RegMRM, Xi32, null_frag, 1>, Sched<[ri]>, EVEX_NF;
      def 64ri_NF_ND : BinOpRI8U_R<m, RegMRM, Xi64, null_frag, 1>, Sched<[ri]>, EVEX_NF;
    }

    def 8mi_NF  : BinOpMI8U_M<m, MemMRM, Xi8>, Sched<[mi, WriteRMW]>, NF;
    def 16mi_NF : BinOpMI8U_M<m, MemMRM, Xi16>, Sched<[mi, WriteRMW]>, NF, PD;
    def 32mi_NF : BinOpMI8U_M<m, MemMRM, Xi32>, Sched<[mi, WriteRMW]>, NF;
    def 64mi_NF : BinOpMI8U_M<m, MemMRM, Xi64>, Sched<[mi, WriteRMW]>, NF;

    def 8mi_NF_ND  : BinOpMI8U_R<m, MemMRM, Xi8>, Sched<[mi, ri]>, EVEX_NF;
    def 16mi_NF_ND : BinOpMI8U_R<m, MemMRM, Xi16>, Sched<[mi, ri]>, EVEX_NF, PD;
    def 32mi_NF_ND : BinOpMI8U_R<m, MemMRM, Xi32>, Sched<[mi, ri]>, EVEX_NF;
    def 64mi_NF_ND : BinOpMI8U_R<m, MemMRM, Xi64>, Sched<[mi, ri]>, EVEX_NF;

    let SchedRW = [ri] in {
      // FIXME: Assembler can't tell whether it's 8r1_NF_ND or 8rCL_NF when the source register is cl, e.g.
      //
      //  {nf} shlb %cl, %al
      //
      // GNU binutils distinguish them by adding an explicit $1 to asm string of 8r1_NF_ND. But we haven't support
      // constant immediate in asm string for X86 in TD. So we add DisassembleOnly for 8r1_NF_ND for the time being.
      def 8r1_NF  : UnaryOpR_R<0xD1, RegMRM, m, Xi8>, NF;
      def 16r1_NF : UnaryOpR_R<0xD1, RegMRM, m, Xi16>, NF, PD;
      def 32r1_NF : UnaryOpR_R<0xD1, RegMRM, m, Xi32>, NF;
      def 64r1_NF : UnaryOpR_R<0xD1, RegMRM, m, Xi64>, NF;

      def 8r1_NF_ND  : UnaryOpR_R<0xD1, RegMRM, m, Xi8, null_frag, 1>, EVEX_NF, DisassembleOnly;
      def 16r1_NF_ND : UnaryOpR_R<0xD1, RegMRM, m, Xi16, null_frag, 1>, EVEX_NF, PD;
      def 32r1_NF_ND : UnaryOpR_R<0xD1, RegMRM, m, Xi32, null_frag, 1>, EVEX_NF;
      def 64r1_NF_ND : UnaryOpR_R<0xD1, RegMRM, m, Xi64, null_frag, 1>, EVEX_NF;
    }

    let SchedRW = [mi, WriteRMW] in {
      def 8m1_NF  : UnaryOpM_M<0xD1, MemMRM, m, Xi8>, NF;
      def 16m1_NF : UnaryOpM_M<0xD1, MemMRM, m, Xi16>, NF, PD;
      def 32m1_NF : UnaryOpM_M<0xD1, MemMRM, m, Xi32>, NF;
      def 64m1_NF : UnaryOpM_M<0xD1, MemMRM, m, Xi64>, NF;
    }
    let SchedRW = [mi, ri] in {
      def 8m1_NF_ND  : UnaryOpM_R<0xD1, MemMRM, m, Xi8>, EVEX_NF;
      def 16m1_NF_ND : UnaryOpM_R<0xD1, MemMRM, m, Xi16>, EVEX_NF, PD;
      def 32m1_NF_ND : UnaryOpM_R<0xD1, MemMRM, m, Xi32>, EVEX_NF;
      def 64m1_NF_ND : UnaryOpM_R<0xD1, MemMRM, m, Xi64>, EVEX_NF;
    }

    let Uses = [CL] in {
      def 8rCL_NF  : BinOpRC_R<m, RegMRM, Xi8>, Sched<[rCL]>, NF;
      def 16rCL_NF : BinOpRC_R<m, RegMRM, Xi16>, Sched<[rCL]>, NF, PD;
      def 32rCL_NF : BinOpRC_R<m, RegMRM, Xi32>, Sched<[rCL]>, NF;
      def 64rCL_NF : BinOpRC_R<m, RegMRM, Xi64>, Sched<[rCL]>, NF;

      def 8rCL_NF_ND  : BinOpRC_R<m, RegMRM, Xi8, null_frag, 1>, Sched<[rCL]>, EVEX_NF;
      def 16rCL_NF_ND : BinOpRC_R<m, RegMRM, Xi16, null_frag, 1>, Sched<[rCL]>, EVEX_NF, PD;
      def 32rCL_NF_ND : BinOpRC_R<m, RegMRM, Xi32, null_frag, 1>, Sched<[rCL]>, EVEX_NF;
      def 64rCL_NF_ND : BinOpRC_R<m, RegMRM, Xi64, null_frag, 1>, Sched<[rCL]>, EVEX_NF;

      def 8mCL_NF  : BinOpMC_M<m, MemMRM, Xi8>, Sched<[mCL, WriteRMW]>, NF;
      def 16mCL_NF : BinOpMC_M<m, MemMRM, Xi16>, Sched<[mCL, WriteRMW]>, NF, PD;
      def 32mCL_NF : BinOpMC_M<m, MemMRM, Xi32>, Sched<[mCL, WriteRMW]>, NF;
      def 64mCL_NF : BinOpMC_M<m, MemMRM, Xi64>, Sched<[mCL, WriteRMW]>, NF;

      def 8mCL_NF_ND  : BinOpMC_R<m, MemMRM, Xi8>, Sched<[mCL, rCL]>, EVEX_NF;
      def 16mCL_NF_ND : BinOpMC_R<m, MemMRM, Xi16>, Sched<[mCL, rCL]>, EVEX_NF, PD;
      def 32mCL_NF_ND : BinOpMC_R<m, MemMRM, Xi32>, Sched<[mCL, rCL]>, EVEX_NF;
      def 64mCL_NF_ND : BinOpMC_R<m, MemMRM, Xi64>, Sched<[mCL, rCL]>, EVEX_NF;
    }
  }
}
defm SHL: ShiftRotate<"shl", MRM4r, MRM4m, shl, WriteShiftCL, WriteShift, WriteShiftCLLd, WriteShiftLd>;
defm SHR: ShiftRotate<"shr", MRM5r, MRM5m, srl, WriteShiftCL, WriteShift, WriteShiftCLLd, WriteShiftLd>;
defm SAR: ShiftRotate<"sar", MRM7r, MRM7m, sra, WriteShiftCL, WriteShift, WriteShiftCLLd, WriteShiftLd>;

defm ROL: ShiftRotate<"rol", MRM0r, MRM0m, rotl, WriteRotateCL, WriteRotate, WriteRotateCLLd, WriteRotateLd>;
defm ROR: ShiftRotate<"ror", MRM1r, MRM1m, rotr, WriteRotateCL, WriteRotate, WriteRotateCLLd, WriteRotateLd>;
defm RCL: ShiftRotate<"rcl", MRM2r, MRM2m, null_frag, WriteRotateCL, WriteRotate, WriteRotateCLLd, WriteRotateLd, [EFLAGS]>;
defm RCR: ShiftRotate<"rcr", MRM3r, MRM3m, null_frag, WriteRotateCL, WriteRotate, WriteRotateCLLd, WriteRotateLd, [EFLAGS]>;

defm SHL: ShiftRotate_NF<"shl", MRM4r, MRM4m, WriteShiftCL, WriteShift, WriteShiftCLLd, WriteShiftLd>;
defm SHR: ShiftRotate_NF<"shr", MRM5r, MRM5m, WriteShiftCL, WriteShift, WriteShiftCLLd, WriteShiftLd>;
defm SAR: ShiftRotate_NF<"sar", MRM7r, MRM7m, WriteShiftCL, WriteShift, WriteShiftCLLd, WriteShiftLd>;

defm ROL: ShiftRotate_NF<"rol", MRM0r, MRM0m, WriteRotateCL, WriteRotate, WriteRotateCLLd, WriteRotateLd>;
defm ROR: ShiftRotate_NF<"ror", MRM1r, MRM1m, WriteRotateCL, WriteRotate, WriteRotateCLLd, WriteRotateLd>;

// Use the opposite rotate if allows us to use the rotate by 1 instruction.
let Predicates = [NoNDD] in {
  def : Pat<(rotl GR8:$src1,  (i8 7)),  (ROR8r1  GR8:$src1)>;
  def : Pat<(rotl GR16:$src1, (i8 15)), (ROR16r1 GR16:$src1)>;
  def : Pat<(rotl GR32:$src1, (i8 31)), (ROR32r1 GR32:$src1)>;
  def : Pat<(rotl GR64:$src1, (i8 63)), (ROR64r1 GR64:$src1)>;
  def : Pat<(rotr GR8:$src1,  (i8 7)),  (ROL8r1  GR8:$src1)>;
  def : Pat<(rotr GR16:$src1, (i8 15)), (ROL16r1 GR16:$src1)>;
  def : Pat<(rotr GR32:$src1, (i8 31)), (ROL32r1 GR32:$src1)>;
  def : Pat<(rotr GR64:$src1, (i8 63)), (ROL64r1 GR64:$src1)>;
}
let Predicates = [HasNDD] in {
  def : Pat<(rotl GR8:$src1,  (i8 7)),  (ROR8r1_ND  GR8:$src1)>;
  def : Pat<(rotl GR16:$src1, (i8 15)), (ROR16r1_ND GR16:$src1)>;
  def : Pat<(rotl GR32:$src1, (i8 31)), (ROR32r1_ND GR32:$src1)>;
  def : Pat<(rotl GR64:$src1, (i8 63)), (ROR64r1_ND GR64:$src1)>;
  def : Pat<(rotr GR8:$src1,  (i8 7)),  (ROL8r1_ND  GR8:$src1)>;
  def : Pat<(rotr GR16:$src1, (i8 15)), (ROL16r1_ND GR16:$src1)>;
  def : Pat<(rotr GR32:$src1, (i8 31)), (ROL32r1_ND GR32:$src1)>;
  def : Pat<(rotr GR64:$src1, (i8 63)), (ROL64r1_ND GR64:$src1)>;
}

def : Pat<(store (rotl (loadi8 addr:$dst), (i8 7)), addr:$dst),
          (ROR8m1 addr:$dst)>;
def : Pat<(store (rotl (loadi16 addr:$dst), (i8 15)), addr:$dst),
          (ROR16m1 addr:$dst)>;
def : Pat<(store (rotl (loadi32 addr:$dst), (i8 31)), addr:$dst),
          (ROR32m1 addr:$dst)>;
def : Pat<(store (rotl (loadi64 addr:$dst), (i8 63)), addr:$dst),
          (ROR64m1 addr:$dst)>, Requires<[In64BitMode]>;

def : Pat<(store (rotr (loadi8 addr:$dst), (i8 7)), addr:$dst),
          (ROL8m1 addr:$dst)>;
def : Pat<(store (rotr (loadi16 addr:$dst), (i8 15)), addr:$dst),
          (ROL16m1 addr:$dst)>;
def : Pat<(store (rotr (loadi32 addr:$dst), (i8 31)), addr:$dst),
          (ROL32m1 addr:$dst)>;
def : Pat<(store (rotr (loadi64 addr:$dst), (i8 63)), addr:$dst),
          (ROL64m1 addr:$dst)>, Requires<[In64BitMode]>;

let Predicates = [HasNDD] in {
def : Pat<(rotl (loadi8 addr:$src), (i8 7)),
          (ROR8m1_ND addr:$src)>;
def : Pat<(rotl (loadi16 addr:$src), (i8 15)),
          (ROR16m1_ND addr:$src)>;
def : Pat<(rotl (loadi32 addr:$src), (i8 31)),
          (ROR32m1_ND addr:$src)>;
def : Pat<(rotl (loadi64 addr:$src), (i8 63)),
          (ROR64m1_ND addr:$src)>;

def : Pat<(rotr (loadi8 addr:$src), (i8 7)),
          (ROL8m1_ND addr:$src)>;
def : Pat<(rotr (loadi16 addr:$src), (i8 15)),
          (ROL16m1_ND addr:$src)>;
def : Pat<(rotr (loadi32 addr:$src), (i8 31)),
          (ROL32m1_ND addr:$src)>;
def : Pat<(rotr (loadi64 addr:$src), (i8 63)),
          (ROL64m1_ND addr:$src)>;
}

// Patterns for rotate with relocImm for the immediate field.
let Predicates = [NoNDD] in {
  def : Pat<(rotl GR8:$src1, (i8 relocImm:$src2)),
            (ROL8ri GR8:$src1, relocImm:$src2)>;
  def : Pat<(rotl GR16:$src1, (i8 relocImm:$src2)),
            (ROL16ri GR16:$src1, relocImm:$src2)>;
  def : Pat<(rotl GR32:$src1, (i8 relocImm:$src2)),
            (ROL32ri GR32:$src1, relocImm:$src2)>;
  def : Pat<(rotl GR64:$src1, (i8 relocImm:$src2)),
            (ROL64ri GR64:$src1, relocImm:$src2)>;

  def : Pat<(rotr GR8:$src1, (i8 relocImm:$src2)),
            (ROR8ri GR8:$src1, relocImm:$src2)>;
  def : Pat<(rotr GR16:$src1, (i8 relocImm:$src2)),
            (ROR16ri GR16:$src1, relocImm:$src2)>;
  def : Pat<(rotr GR32:$src1, (i8 relocImm:$src2)),
            (ROR32ri GR32:$src1, relocImm:$src2)>;
  def : Pat<(rotr GR64:$src1, (i8 relocImm:$src2)),
            (ROR64ri GR64:$src1, relocImm:$src2)>;
}
let Predicates = [HasNDD] in {
  def : Pat<(rotl GR8:$src1, (i8 relocImm:$src2)),
            (ROL8ri_ND GR8:$src1, relocImm:$src2)>;
  def : Pat<(rotl GR16:$src1, (i8 relocImm:$src2)),
            (ROL16ri_ND GR16:$src1, relocImm:$src2)>;
  def : Pat<(rotl GR32:$src1, (i8 relocImm:$src2)),
            (ROL32ri_ND GR32:$src1, relocImm:$src2)>;
  def : Pat<(rotl GR64:$src1, (i8 relocImm:$src2)),
            (ROL64ri_ND GR64:$src1, relocImm:$src2)>;

  def : Pat<(rotr GR8:$src1, (i8 relocImm:$src2)),
            (ROR8ri_ND GR8:$src1, relocImm:$src2)>;
  def : Pat<(rotr GR16:$src1, (i8 relocImm:$src2)),
            (ROR16ri_ND GR16:$src1, relocImm:$src2)>;
  def : Pat<(rotr GR32:$src1, (i8 relocImm:$src2)),
            (ROR32ri_ND GR32:$src1, relocImm:$src2)>;
  def : Pat<(rotr GR64:$src1, (i8 relocImm:$src2)),
            (ROR64ri_ND GR64:$src1, relocImm:$src2)>;
}

//===----------------------------------------------------------------------===//
// Double precision shift instructions (generalizations of rotate)
//===----------------------------------------------------------------------===//

class ShlrdOpRRI8U_R<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag, bit ndd = 0>
  : ITy<o, MRMDestReg, t, (outs t.RegClass:$dst),
        (ins t.RegClass:$src1, t.RegClass:$src2, u8imm:$src3), m, !if(!eq(ndd, 0), triop_args, triop_ndd_args),
        []>, NDD<ndd> {
  let isCommutable = 1;
  let ImmT = Imm8;
  let SchedRW = [WriteSHDrri];
  let Pattern = !if(!eq(m, "shld"),
                    [(set t.RegClass:$dst, (node t.RegClass:$src1, t.RegClass:$src2, (i8 imm:$src3)))],
                    [(set t.RegClass:$dst, (node t.RegClass:$src2, t.RegClass:$src1, (i8 imm:$src3)))]);
}

class ShlrdOpRRC_R<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag, bit ndd = 0>
  : BinOpRR<o, m, !if(!eq(ndd, 0), triop_cl_args, triop_cl_ndd_args), t, (outs t.RegClass:$dst), []>, NDD<ndd> {
  let Uses = [CL];
  let SchedRW = [WriteSHDrrcl];
  let Pattern = !if(!eq(m, "shld"),
                    [(set t.RegClass:$dst, (node t.RegClass:$src1, t.RegClass:$src2, CL))],
                    [(set t.RegClass:$dst, (node t.RegClass:$src2, t.RegClass:$src1, CL))]);
}

class ShlrdOpMRI8U_M<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag>
  : ITy<o, MRMDestMem, t, (outs), (ins t.MemOperand:$src1, t.RegClass:$src2, u8imm:$src3),
        m, triop_args, []>, TB {
  let ImmT = Imm8;
  let SchedRW = [WriteSHDmri];
  let mayLoad = 1;
  let mayStore = 1;
  let Pattern = !if(!eq(m, "shld"),
                    [(store (node (t.LoadNode addr:$src1), t.RegClass:$src2, (i8 imm:$src3)), addr:$src1)],
                    [(store (node t.RegClass:$src2, (t.LoadNode addr:$src1), (i8 imm:$src3)), addr:$src1)]);
}

class ShlrdOpMRC_M<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag>
  : BinOpMR<o, m, triop_cl_args, t, (outs), []>, TB {
  let Uses = [CL];
  let SchedRW = [WriteSHDmrcl];
  let mayStore = 1;
  let Pattern = !if(!eq(m, "shld"),
                    [(store (node (t.LoadNode addr:$src1), t.RegClass:$src2, CL), addr:$src1)],
                    [(store (node t.RegClass:$src2, (t.LoadNode addr:$src1), CL), addr:$src1)]);
}

class ShlrdOpMRI8U_R<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag>
  : ITy<o, MRMDestMem, t, (outs t.RegClass:$dst), (ins t.MemOperand:$src1, t.RegClass:$src2, u8imm:$src3),
        m, triop_ndd_args, []>, NDD<1> {
  let ImmT = Imm8;
  let SchedRW = [WriteSHDmri];
  let mayLoad = 1;
  let Pattern = !if(!eq(m, "shld"),
                    [(set t.RegClass:$dst, (node (t.LoadNode addr:$src1), t.RegClass:$src2, (i8 imm:$src3)))],
                    [(set t.RegClass:$dst, (node t.RegClass:$src2, (t.LoadNode addr:$src1), (i8 imm:$src3)))]);
}

class ShlrdOpMRC_R<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node = null_frag>
  : BinOpMR<o, m, triop_cl_ndd_args, t, (outs t.RegClass:$dst), []>, NDD<1> {
  let Uses = [CL];
  let SchedRW = [WriteSHDmrcl];
  let Pattern = !if(!eq(m, "shld"),
                    [(set t.RegClass:$dst, (node (t.LoadNode addr:$src1), t.RegClass:$src2, CL))],
                    [(set t.RegClass:$dst, (node t.RegClass:$src2, (t.LoadNode addr:$src1), CL))]);
}

multiclass Shlrd<bits<8> o1, bits<8> o2, bits<8> o3, string m, SDPatternOperator node, SDPatternOperator t_node> {
  let Predicates = [NoNDD] in {
    def 16rri8 : ShlrdOpRRI8U_R<o1, m, Xi16, t_node>, TB, DefEFLAGS, OpSize16;
    def 32rri8 : ShlrdOpRRI8U_R<o1, m, Xi32, node>, TB, DefEFLAGS, OpSize32;
    def 64rri8 : ShlrdOpRRI8U_R<o1, m, Xi64, node>, TB, DefEFLAGS;

    def 16rrCL : ShlrdOpRRC_R<o2, m, Xi16, t_node>, TB, DefEFLAGS, OpSize16;
    def 32rrCL : ShlrdOpRRC_R<o2, m, Xi32, node>, TB, DefEFLAGS, OpSize32;
    def 64rrCL : ShlrdOpRRC_R<o2, m, Xi64, node>, TB, DefEFLAGS;
  }
  let Predicates = [HasNDD, In64BitMode] in {
    def 16rri8_ND : ShlrdOpRRI8U_R<o3, m, Xi16, t_node, 1>, DefEFLAGS, PD;
    def 32rri8_ND : ShlrdOpRRI8U_R<o3, m, Xi32, node, 1>, DefEFLAGS;
    def 64rri8_ND : ShlrdOpRRI8U_R<o3, m, Xi64, node, 1>, DefEFLAGS;

    def 16rrCL_ND : ShlrdOpRRC_R<o2, m, Xi16, t_node, 1>, DefEFLAGS, PD;
    def 32rrCL_ND : ShlrdOpRRC_R<o2, m, Xi32, node, 1>, DefEFLAGS;
    def 64rrCL_ND : ShlrdOpRRC_R<o2, m, Xi64, node, 1>, DefEFLAGS;
  }

  let Predicates = [In64BitMode] in {
    def 16rri8_NF : ShlrdOpRRI8U_R<o3, m, Xi16>, NF, PD;
    def 32rri8_NF : ShlrdOpRRI8U_R<o3, m, Xi32>, NF;
    def 64rri8_NF : ShlrdOpRRI8U_R<o3, m, Xi64>, NF;

    def 16rrCL_NF : ShlrdOpRRC_R<o2, m, Xi16>, NF, PD;
    def 32rrCL_NF : ShlrdOpRRC_R<o2, m, Xi32>, NF;
    def 64rrCL_NF : ShlrdOpRRC_R<o2, m, Xi64>, NF;

    def 16rri8_NF_ND : ShlrdOpRRI8U_R<o3, m, Xi16, null_frag, 1>, EVEX_NF, PD;
    def 32rri8_NF_ND : ShlrdOpRRI8U_R<o3, m, Xi32, null_frag, 1>, EVEX_NF;
    def 64rri8_NF_ND : ShlrdOpRRI8U_R<o3, m, Xi64, null_frag, 1>, EVEX_NF;

    def 16rrCL_NF_ND : ShlrdOpRRC_R<o2, m, Xi16, null_frag, 1>, EVEX_NF, PD;
    def 32rrCL_NF_ND : ShlrdOpRRC_R<o2, m, Xi32, null_frag, 1>, EVEX_NF;
    def 64rrCL_NF_ND : ShlrdOpRRC_R<o2, m, Xi64, null_frag, 1>, EVEX_NF;

    def 16rri8_EVEX : ShlrdOpRRI8U_R<o3, m, Xi16>, DefEFLAGS, PL, PD;
    def 32rri8_EVEX : ShlrdOpRRI8U_R<o3, m, Xi32>, DefEFLAGS, PL;
    def 64rri8_EVEX : ShlrdOpRRI8U_R<o3, m, Xi64>, DefEFLAGS, PL;

    def 16rrCL_EVEX : ShlrdOpRRC_R<o2, m, Xi16>, DefEFLAGS, PL, PD;
    def 32rrCL_EVEX : ShlrdOpRRC_R<o2, m, Xi32>, DefEFLAGS, PL;
    def 64rrCL_EVEX : ShlrdOpRRC_R<o2, m, Xi64>, DefEFLAGS, PL;
  }

  def 16mri8 : ShlrdOpMRI8U_M<o1, m, Xi16, t_node>, DefEFLAGS, OpSize16;
  def 32mri8 : ShlrdOpMRI8U_M<o1, m, Xi32, node>, DefEFLAGS, OpSize32;
  def 64mri8 : ShlrdOpMRI8U_M<o1, m, Xi64, node>, DefEFLAGS;

  def 16mrCL : ShlrdOpMRC_M<o2, m, Xi16, t_node>, DefEFLAGS, OpSize16;
  def 32mrCL : ShlrdOpMRC_M<o2, m, Xi32, node>, DefEFLAGS, OpSize32;
  def 64mrCL : ShlrdOpMRC_M<o2, m, Xi64, node>, DefEFLAGS;

  let Predicates = [HasNDD, In64BitMode] in {
    def 16mri8_ND : ShlrdOpMRI8U_R<o3, m, Xi16, t_node>, DefEFLAGS, PD;
    def 32mri8_ND : ShlrdOpMRI8U_R<o3, m, Xi32, node>, DefEFLAGS;
    def 64mri8_ND : ShlrdOpMRI8U_R<o3, m, Xi64, node>, DefEFLAGS;

    def 16mrCL_ND : ShlrdOpMRC_R<o2, m, Xi16, t_node>, DefEFLAGS, PD;
    def 32mrCL_ND : ShlrdOpMRC_R<o2, m, Xi32, node>, DefEFLAGS;
    def 64mrCL_ND : ShlrdOpMRC_R<o2, m, Xi64, node>, DefEFLAGS;
  }

  let Predicates = [In64BitMode] in {
    def 16mri8_NF : ShlrdOpMRI8U_M<o3, m, Xi16>, NF, PD;
    def 32mri8_NF : ShlrdOpMRI8U_M<o3, m, Xi32>, NF;
    def 64mri8_NF : ShlrdOpMRI8U_M<o3, m, Xi64>, NF;

    def 16mrCL_NF : ShlrdOpMRC_M<o2, m, Xi16>, NF, PD;
    def 32mrCL_NF : ShlrdOpMRC_M<o2, m, Xi32>, NF;
    def 64mrCL_NF : ShlrdOpMRC_M<o2, m, Xi64>, NF;

    def 16mri8_NF_ND : ShlrdOpMRI8U_R<o3, m, Xi16>, EVEX_NF, PD;
    def 32mri8_NF_ND : ShlrdOpMRI8U_R<o3, m, Xi32>, EVEX_NF;
    def 64mri8_NF_ND : ShlrdOpMRI8U_R<o3, m, Xi64>, EVEX_NF;

    def 16mrCL_NF_ND : ShlrdOpMRC_R<o2, m, Xi16>, EVEX_NF, PD;
    def 32mrCL_NF_ND : ShlrdOpMRC_R<o2, m, Xi32>, EVEX_NF;
    def 64mrCL_NF_ND : ShlrdOpMRC_R<o2, m, Xi64>, EVEX_NF;

    def 16mri8_EVEX : ShlrdOpMRI8U_M<o3, m, Xi16>, DefEFLAGS, PL, PD;
    def 32mri8_EVEX : ShlrdOpMRI8U_M<o3, m, Xi32>, DefEFLAGS, PL;
    def 64mri8_EVEX : ShlrdOpMRI8U_M<o3, m, Xi64>, DefEFLAGS, PL;

    def 16mrCL_EVEX : ShlrdOpMRC_M<o2, m, Xi16>, DefEFLAGS, PL, PD;
    def 32mrCL_EVEX : ShlrdOpMRC_M<o2, m, Xi32>, DefEFLAGS, PL;
    def 64mrCL_EVEX : ShlrdOpMRC_M<o2, m, Xi64>, DefEFLAGS, PL;
  }
}

defm SHLD : Shlrd<0xA4, 0xA5, 0x24, "shld", fshl, X86fshl>;
defm SHRD : Shlrd<0xAC, 0xAD, 0x2C, "shrd", fshr, X86fshr>;

// Sandy Bridge and newer Intel processors support faster rotates using
// SHLD to avoid a partial flag update on the normal rotate instructions.
// Use a pseudo so that TwoInstructionPass and register allocation will see
// this as unary instruction.
let Predicates = [HasFastSHLDRotate], AddedComplexity = 5,
    Defs = [EFLAGS], isPseudo = 1, SchedRW = [WriteSHDrri],
    Constraints = "$src1 = $dst" in {
  def SHLDROT32ri  : I<0, Pseudo, (outs GR32:$dst),
                       (ins GR32:$src1, u8imm:$shamt), "",
                     [(set GR32:$dst, (rotl GR32:$src1, (i8 imm:$shamt)))]>;
  def SHLDROT64ri  : I<0, Pseudo, (outs GR64:$dst),
                       (ins GR64:$src1, u8imm:$shamt), "",
                     [(set GR64:$dst, (rotl GR64:$src1, (i8 imm:$shamt)))]>;

  def SHRDROT32ri  : I<0, Pseudo, (outs GR32:$dst),
                       (ins GR32:$src1, u8imm:$shamt), "",
                     [(set GR32:$dst, (rotr GR32:$src1, (i8 imm:$shamt)))]>;
  def SHRDROT64ri  : I<0, Pseudo, (outs GR64:$dst),
                       (ins GR64:$src1, u8imm:$shamt), "",
                     [(set GR64:$dst, (rotr GR64:$src1, (i8 imm:$shamt)))]>;
}

//===----------------------------------------------------------------------===//
// BMI Shift/Rotate instructions
//===----------------------------------------------------------------------===//

def ROT32L2R_imm8  : SDNodeXForm<imm, [{
  // Convert a ROTL shamt to a ROTR shamt on 32-bit integer.
  return getI8Imm(32 - N->getZExtValue(), SDLoc(N));
}]>;

def ROT64L2R_imm8  : SDNodeXForm<imm, [{
  // Convert a ROTL shamt to a ROTR shamt on 64-bit integer.
  return getI8Imm(64 - N->getZExtValue(), SDLoc(N));
}]>;

// NOTE: We use WriteShift for these rotates as they avoid the stalls
// of many of the older x86 rotate instructions.
class RorXri<X86TypeInfo t>
  : ITy<0xF0, MRMSrcReg, t, (outs t.RegClass:$dst), (ins t.RegClass:$src1, u8imm:$src2),
        "rorx", binop_ndd_args, []>, TA, XD, Sched<[WriteShift]> {
  let ImmT = Imm8;
}
class RorXmi<X86TypeInfo t>
  : ITy<0xF0, MRMSrcMem, t, (outs t.RegClass:$dst), (ins t.MemOperand:$src1, u8imm:$src2),
        "rorx", binop_ndd_args, []>, TA, XD, Sched<[WriteShiftLd]> {
  let ImmT = Imm8;
  let mayLoad = 1;
}

multiclass RorX<X86TypeInfo t> {
  let Predicates = [HasBMI2, NoEGPR] in {
    def ri : RorXri<t>, VEX;
    def mi : RorXmi<t>, VEX;
  }
  let Predicates = [HasBMI2, HasEGPR, In64BitMode] in {
    def ri_EVEX : RorXri<t>, EVEX;
    def mi_EVEX : RorXmi<t>, EVEX;
  }
}

defm RORX32: RorX<Xi32>;
defm RORX64: RorX<Xi64>;

class ShiftXrr<string m, X86TypeInfo t>
  : ITy<0xF7, MRMSrcReg4VOp3, t, (outs t.RegClass:$dst), (ins t.RegClass:$src1, t.RegClass:$src2),
        m, binop_ndd_args, []>, T8, Sched<[WriteShift]>;

class ShiftXrm<string m, X86TypeInfo t>
  : ITy<0xF7, MRMSrcMem4VOp3, t, (outs t.RegClass:$dst), (ins t.MemOperand:$src1, t.RegClass:$src2),
        m, binop_ndd_args, []>, T8,
    Sched<[WriteShift.Folded,
           // x86memop:$src1
           ReadDefault, ReadDefault, ReadDefault, ReadDefault,
           ReadDefault,
           // RC:$src2
           WriteShift.ReadAfterFold]> {
  let mayLoad = 1;
}


multiclass ShiftX<string m, X86TypeInfo t> {
  let Predicates = [HasBMI2, NoEGPR] in {
    def rr : ShiftXrr<m, t>, VEX;
    def rm : ShiftXrm<m, t>, VEX;
  }
  let Predicates = [HasBMI2, HasEGPR, In64BitMode] in {
    def rr_EVEX : ShiftXrr<m, t>, EVEX;
    def rm_EVEX : ShiftXrm<m, t>, EVEX;
  }
}

defm SARX32: ShiftX<"sarx", Xi32>, XS;
defm SARX64: ShiftX<"sarx", Xi64>, XS;
defm SHRX32: ShiftX<"shrx", Xi32>, XD;
defm SHRX64: ShiftX<"shrx", Xi64>, XD;
defm SHLX32: ShiftX<"shlx", Xi32>, PD;
defm SHLX64: ShiftX<"shlx", Xi64>, PD;

multiclass RORX_Pats<string suffix> {
  // Prefer RORX which is non-destructive and doesn't update EFLAGS.
  let AddedComplexity = 10 in {
    def : Pat<(rotr GR32:$src, (i8 imm:$shamt)),
              (!cast<Instruction>(RORX32ri#suffix) GR32:$src, imm:$shamt)>;
    def : Pat<(rotr GR64:$src, (i8 imm:$shamt)),
              (!cast<Instruction>(RORX64ri#suffix) GR64:$src, imm:$shamt)>;

    def : Pat<(rotl GR32:$src, (i8 imm:$shamt)),
              (!cast<Instruction>(RORX32ri#suffix) GR32:$src, (ROT32L2R_imm8 imm:$shamt))>;
    def : Pat<(rotl GR64:$src, (i8 imm:$shamt)),
              (!cast<Instruction>(RORX64ri#suffix) GR64:$src, (ROT64L2R_imm8 imm:$shamt))>;
  }

  def : Pat<(rotr (loadi32 addr:$src), (i8 imm:$shamt)),
            (!cast<Instruction>(RORX32mi#suffix) addr:$src, imm:$shamt)>;
  def : Pat<(rotr (loadi64 addr:$src), (i8 imm:$shamt)),
            (!cast<Instruction>(RORX64mi#suffix) addr:$src, imm:$shamt)>;

  def : Pat<(rotl (loadi32 addr:$src), (i8 imm:$shamt)),
            (!cast<Instruction>(RORX32mi#suffix) addr:$src, (ROT32L2R_imm8 imm:$shamt))>;
  def : Pat<(rotl (loadi64 addr:$src), (i8 imm:$shamt)),
            (!cast<Instruction>(RORX64mi#suffix) addr:$src, (ROT64L2R_imm8 imm:$shamt))>;
}

multiclass ShiftX_Pats<SDNode op, string suffix = ""> {
  // Prefer SARX/SHRX/SHLX over SAR/SHR/SHL with variable shift BUT not
  // immediate shift, i.e. the following code is considered better
  //
  //  mov %edi, %esi
  //  shl $imm, %esi
  //  ... %edi, ...
  //
  // than
  //
  //  movb $imm, %sil
  //  shlx %sil, %edi, %esi
  //  ... %edi, ...
  //
  let AddedComplexity = 1 in {
   def : Pat<(op GR32:$src1, GR8:$src2),
              (!cast<Instruction>(NAME#"32rr"#suffix) GR32:$src1,
               (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
   def : Pat<(op GR64:$src1, GR8:$src2),
              (!cast<Instruction>(NAME#"64rr"#suffix) GR64:$src1,
               (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
   def : Pat<(op GR32:$src1, (shiftMask32 GR8:$src2)),
              (!cast<Instruction>(NAME#"32rr"#suffix) GR32:$src1,
               (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
   def : Pat<(op GR64:$src1, (shiftMask64 GR8:$src2)),
              (!cast<Instruction>(NAME#"64rr"#suffix) GR64:$src1,
               (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  }
  // We prefer to use
  //  mov (%ecx), %esi
  //  shl $imm, $esi
  //
  // over
  //
  //  movb $imm, %al
  //  shlx %al, (%ecx), %esi
  //
  // This priority is enforced by IsProfitableToFoldLoad.
  def : Pat<(op (loadi32 addr:$src1), GR8:$src2),
             (!cast<Instruction>(NAME#"32rm"#suffix) addr:$src1,
              (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(op (loadi64 addr:$src1), GR8:$src2),
             (!cast<Instruction>(NAME#"64rm"#suffix) addr:$src1,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(op (loadi32 addr:$src1), (shiftMask32 GR8:$src2)),
             (!cast<Instruction>(NAME#"32rm"#suffix) addr:$src1,
              (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(op (loadi64 addr:$src1), (shiftMask64 GR8:$src2)),
             (!cast<Instruction>(NAME#"64rm"#suffix) addr:$src1,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
}

let Predicates = [HasBMI2, NoEGPR] in {
  defm : RORX_Pats<"">;
  defm SARX : ShiftX_Pats<sra>;
  defm SHRX : ShiftX_Pats<srl>;
  defm SHLX : ShiftX_Pats<shl>;
}

let Predicates = [HasBMI2, HasEGPR] in {
  defm : RORX_Pats<"_EVEX">;
  defm SARX : ShiftX_Pats<sra, "_EVEX">;
  defm SHRX : ShiftX_Pats<srl, "_EVEX">;
  defm SHLX : ShiftX_Pats<shl, "_EVEX">;
}
