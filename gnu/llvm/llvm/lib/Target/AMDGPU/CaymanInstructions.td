//===-- CaymanInstructions.td - CM Instruction defs  -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen definitions for instructions which are available only on Cayman
// family GPUs.
//
//===----------------------------------------------------------------------===//

def isCayman : Predicate<"Subtarget->hasCaymanISA()">;

//===----------------------------------------------------------------------===//
// Cayman Instructions
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isCayman in {

def MULADD_INT24_cm : R600_3OP <0x08, "MULADD_INT24",
  [(set i32:$dst, (AMDGPUmad_i24 i32:$src0, i32:$src1, i32:$src2))], VecALU
>;
def MUL_INT24_cm : R600_2OP <0x5B, "MUL_INT24",
  [(set i32:$dst, (AMDGPUmul_i24 i32:$src0, i32:$src1))], VecALU
>;

def : IMad24Pat<MULADD_INT24_cm>;

let isVector = 1 in {

def RECIP_IEEE_cm : RECIP_IEEE_Common<0x86>;

def MULLO_INT_cm : MULLO_INT_Common<0x8F>;
def MULHI_INT_cm : MULHI_INT_Common<0x90>;
def MULLO_UINT_cm : MULLO_UINT_Common<0x91>;
def MULHI_UINT_cm : MULHI_UINT_Common<0x92>;
def MULHI_INT_cm24 : MULHI_INT24_Common<0x5c>;
def MULHI_UINT_cm24 : MULHI_UINT24_Common<0xb2>;

def RECIPSQRT_CLAMPED_cm : RECIPSQRT_CLAMPED_Common<0x87>;
def EXP_IEEE_cm : EXP_IEEE_Common<0x81>;
def LOG_IEEE_cm : LOG_IEEE_Common<0x83>;
def RECIP_CLAMPED_cm : RECIP_CLAMPED_Common<0x84>;
def RECIPSQRT_IEEE_cm : RECIPSQRT_IEEE_Common<0x89>;
def SIN_cm : SIN_Common<0x8D>;
def COS_cm : COS_Common<0x8E>;
} // End isVector = 1

def : SqrtPat<RECIPSQRT_IEEE_cm, RECIP_IEEE_cm>;

def : POW_Common <LOG_IEEE_cm, EXP_IEEE_cm, MUL>;

defm DIV_cm : DIV_Common<RECIP_IEEE_cm>;

// RECIP_UINT emulation for Cayman
// The multiplication scales from [0,1) to the unsigned integer range,
// rounding down a bit to avoid unwanted overflow.
def : R600Pat <
  (AMDGPUurecip i32:$src0),
  (FLT_TO_UINT_eg (MUL_IEEE (RECIP_IEEE_cm (UINT_TO_FLT_eg $src0)),
                            (MOV_IMM_I32 CONST.FP_4294966784)))
>;

def CF_END_CM : CF_CLAUSE_EG<32, (ins), "CF_END"> {
    let ADDR = 0;
    let POP_COUNT = 0;
    let COUNT = 0;
  }



class RAT_STORE_DWORD <RegisterClass rc, ValueType vt, bits<4> mask> :
  CF_MEM_RAT_CACHELESS <0x14, 0, mask,
                        (ins rc:$rw_gpr, R600_TReg32_X:$index_gpr),
                        "STORE_DWORD $rw_gpr, $index_gpr",
                        [(store_global vt:$rw_gpr, i32:$index_gpr)]> {
  let eop = 0; // This bit is not used on Cayman.
}

def RAT_STORE_DWORD32 : RAT_STORE_DWORD <R600_TReg32_X, i32, 0x1>;
def RAT_STORE_DWORD64 : RAT_STORE_DWORD <R600_Reg64, v2i32, 0x3>;
def RAT_STORE_DWORD128 : RAT_STORE_DWORD <R600_Reg128, v4i32, 0xf>;

def RAT_STORE_TYPED_cm: CF_MEM_RAT_STORE_TYPED<0> {
  let eop = 0; // This bit is not used on Cayman.
}

class VTX_READ_cm <string name, dag outs>
    : VTX_WORD0_cm, VTX_READ<name, outs, []> {

  // Static fields
  let VC_INST = 0;
  let FETCH_TYPE = 2;
  let FETCH_WHOLE_QUAD = 0;
  let SRC_REL = 0;
  // XXX: We can infer this field based on the SRC_GPR.  This would allow us
  // to store vertex addresses in any channel, not just X.
  let SRC_SEL_X = 0;
  let SRC_SEL_Y = 0;
  let STRUCTURED_READ = 0;
  let LDS_REQ = 0;
  let COALESCED_READ = 0;

  let Inst{31-0} = Word0;
}

def VTX_READ_8_cm
    : VTX_READ_cm <"VTX_READ_8 $dst_gpr, $src_gpr",
                   (outs R600_TReg32_X:$dst_gpr)> {

  let DST_SEL_X = 0;
  let DST_SEL_Y = 7;   // Masked
  let DST_SEL_Z = 7;   // Masked
  let DST_SEL_W = 7;   // Masked
  let DATA_FORMAT = 1; // FMT_8
}

def VTX_READ_16_cm
    : VTX_READ_cm <"VTX_READ_16 $dst_gpr, $src_gpr",
                   (outs R600_TReg32_X:$dst_gpr)> {
  let DST_SEL_X = 0;
  let DST_SEL_Y = 7;   // Masked
  let DST_SEL_Z = 7;   // Masked
  let DST_SEL_W = 7;   // Masked
  let DATA_FORMAT = 5; // FMT_16

}

def VTX_READ_32_cm
    : VTX_READ_cm <"VTX_READ_32 $dst_gpr, $src_gpr",
                   (outs R600_TReg32_X:$dst_gpr)> {

  let DST_SEL_X        = 0;
  let DST_SEL_Y        = 7;   // Masked
  let DST_SEL_Z        = 7;   // Masked
  let DST_SEL_W        = 7;   // Masked
  let DATA_FORMAT      = 0xD; // COLOR_32

  // This is not really necessary, but there were some GPU hangs that appeared
  // to be caused by ALU instructions in the next instruction group that wrote
  // to the $src_gpr registers of the VTX_READ.
  // e.g.
  // %t3_x = VTX_READ_PARAM_32_eg killed %t2_x, 24
  // %t2_x = MOV %zero
  //Adding this constraint prevents this from happening.
  let Constraints = "$src_gpr.ptr = $dst_gpr";
}

def VTX_READ_64_cm
    : VTX_READ_cm <"VTX_READ_64 $dst_gpr.XY, $src_gpr",
                   (outs R600_Reg64:$dst_gpr)> {

  let DST_SEL_X        = 0;
  let DST_SEL_Y        = 1;
  let DST_SEL_Z        = 7;
  let DST_SEL_W        = 7;
  let DATA_FORMAT      = 0x1D; // COLOR_32_32
}

def VTX_READ_128_cm
    : VTX_READ_cm <"VTX_READ_128 $dst_gpr.XYZW, $src_gpr",
                   (outs R600_Reg128:$dst_gpr)> {

  let DST_SEL_X        =  0;
  let DST_SEL_Y        =  1;
  let DST_SEL_Z        =  2;
  let DST_SEL_W        =  3;
  let DATA_FORMAT      =  0x22; // COLOR_32_32_32_32

  // XXX: Need to force VTX_READ_128 instructions to write to the same register
  // that holds its buffer address to avoid potential hangs.  We can't use
  // the same constraint as VTX_READ_32_eg, because the $src_gpr.ptr and $dst
  // registers are different sizes.
}

//===----------------------------------------------------------------------===//
// VTX Read from parameter memory space
//===----------------------------------------------------------------------===//
def : R600Pat<(i32:$dst_gpr (vtx_id3_az_extloadi8 ADDRVTX_READ:$src_gpr)),
          (VTX_READ_8_cm MEMxi:$src_gpr, 3)>;
def : R600Pat<(i32:$dst_gpr (vtx_id3_az_extloadi16 ADDRVTX_READ:$src_gpr)),
          (VTX_READ_16_cm MEMxi:$src_gpr, 3)>;
def : R600Pat<(i32:$dst_gpr (vtx_id3_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_32_cm MEMxi:$src_gpr, 3)>;
def : R600Pat<(v2i32:$dst_gpr (vtx_id3_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_64_cm MEMxi:$src_gpr, 3)>;
def : R600Pat<(v4i32:$dst_gpr (vtx_id3_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_128_cm MEMxi:$src_gpr, 3)>;

//===----------------------------------------------------------------------===//
// VTX Read from constant memory space
//===----------------------------------------------------------------------===//
def : R600Pat<(i32:$dst_gpr (vtx_id2_az_extloadi8 ADDRVTX_READ:$src_gpr)),
          (VTX_READ_8_cm MEMxi:$src_gpr, 2)>;
def : R600Pat<(i32:$dst_gpr (vtx_id2_az_extloadi16 ADDRVTX_READ:$src_gpr)),
          (VTX_READ_16_cm MEMxi:$src_gpr, 2)>;
def : R600Pat<(i32:$dst_gpr (vtx_id2_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_32_cm MEMxi:$src_gpr, 2)>;
def : R600Pat<(v2i32:$dst_gpr (vtx_id2_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_64_cm MEMxi:$src_gpr, 2)>;
def : R600Pat<(v4i32:$dst_gpr (vtx_id2_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_128_cm MEMxi:$src_gpr, 2)>;

//===----------------------------------------------------------------------===//
// VTX Read from global memory space
//===----------------------------------------------------------------------===//
def : R600Pat<(i32:$dst_gpr (vtx_id1_az_extloadi8 ADDRVTX_READ:$src_gpr)),
          (VTX_READ_8_cm MEMxi:$src_gpr, 1)>;
def : R600Pat<(i32:$dst_gpr (vtx_id1_az_extloadi16 ADDRVTX_READ:$src_gpr)),
          (VTX_READ_16_cm MEMxi:$src_gpr, 1)>;
def : R600Pat<(i32:$dst_gpr (vtx_id1_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_32_cm MEMxi:$src_gpr, 1)>;
def : R600Pat<(v2i32:$dst_gpr (vtx_id1_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_64_cm MEMxi:$src_gpr, 1)>;
def : R600Pat<(v4i32:$dst_gpr (vtx_id1_load ADDRVTX_READ:$src_gpr)),
          (VTX_READ_128_cm MEMxi:$src_gpr, 1)>;

} // End let SubtargetPredicate = isCayman
