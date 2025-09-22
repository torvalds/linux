//===- X86InstrVecCompiler.td - Vector Compiler Patterns ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the various vector pseudo instructions used by the
// compiler, as well as Pat patterns used during instruction selection.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Non-instruction patterns
//===----------------------------------------------------------------------===//

let Predicates = [NoAVX512] in {
  // A vector extract of the first f32/f64 position is a subregister copy
  def : Pat<(f16 (extractelt (v8f16 VR128:$src), (iPTR 0))),
            (COPY_TO_REGCLASS (v8f16 VR128:$src), FR16)>;
  def : Pat<(f32 (extractelt (v4f32 VR128:$src), (iPTR 0))),
            (COPY_TO_REGCLASS (v4f32 VR128:$src), FR32)>;
  def : Pat<(f64 (extractelt (v2f64 VR128:$src), (iPTR 0))),
            (COPY_TO_REGCLASS (v2f64 VR128:$src), FR64)>;
}

let Predicates = [HasAVX512] in {
  // A vector extract of the first f32/f64 position is a subregister copy
  def : Pat<(f16 (extractelt (v8f16 VR128X:$src), (iPTR 0))),
            (COPY_TO_REGCLASS (v8f16 VR128X:$src), FR16X)>;
  def : Pat<(f32 (extractelt (v4f32 VR128X:$src), (iPTR 0))),
            (COPY_TO_REGCLASS (v4f32 VR128X:$src), FR32X)>;
  def : Pat<(f64 (extractelt (v2f64 VR128X:$src), (iPTR 0))),
            (COPY_TO_REGCLASS (v2f64 VR128X:$src), FR64X)>;
}

let Predicates = [NoVLX] in {
  def : Pat<(v8f16 (scalar_to_vector FR16:$src)),
            (COPY_TO_REGCLASS FR16:$src, VR128)>;
  // Implicitly promote a 32-bit scalar to a vector.
  def : Pat<(v4f32 (scalar_to_vector FR32:$src)),
            (COPY_TO_REGCLASS FR32:$src, VR128)>;
  // Implicitly promote a 64-bit scalar to a vector.
  def : Pat<(v2f64 (scalar_to_vector FR64:$src)),
            (COPY_TO_REGCLASS FR64:$src, VR128)>;
}

let Predicates = [HasVLX] in {
  def : Pat<(v8f16 (scalar_to_vector FR16X:$src)),
            (COPY_TO_REGCLASS FR16X:$src, VR128X)>;
  // Implicitly promote a 32-bit scalar to a vector.
  def : Pat<(v4f32 (scalar_to_vector FR32X:$src)),
            (COPY_TO_REGCLASS FR32X:$src, VR128X)>;
  // Implicitly promote a 64-bit scalar to a vector.
  def : Pat<(v2f64 (scalar_to_vector FR64X:$src)),
            (COPY_TO_REGCLASS FR64X:$src, VR128X)>;
}

//===----------------------------------------------------------------------===//
// Subvector tricks
//===----------------------------------------------------------------------===//

// Patterns for insert_subvector/extract_subvector to/from index=0
multiclass subvector_subreg_lowering<RegisterClass subRC, ValueType subVT,
                                     RegisterClass RC, ValueType VT,
                                     SubRegIndex subIdx> {
  def : Pat<(subVT (extract_subvector (VT RC:$src), (iPTR 0))),
            (subVT (EXTRACT_SUBREG RC:$src, subIdx))>;

  def : Pat<(VT (insert_subvector undef_or_freeze_undef, subRC:$src, (iPTR 0))),
            (VT (INSERT_SUBREG (IMPLICIT_DEF), subRC:$src, subIdx))>;
}

// A 128-bit subvector extract from the first 256-bit vector position is a
// subregister copy that needs no instruction. Likewise, a 128-bit subvector
// insert to the first 256-bit vector position is a subregister copy that needs
// no instruction.
defm : subvector_subreg_lowering<VR128, v4i32, VR256, v8i32,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v4f32, VR256, v8f32,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v2i64, VR256, v4i64,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v2f64, VR256, v4f64,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v8i16, VR256, v16i16, sub_xmm>;
defm : subvector_subreg_lowering<VR128, v16i8, VR256, v32i8,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v8f16, VR256, v16f16, sub_xmm>;
defm : subvector_subreg_lowering<VR128, v8bf16, VR256, v16bf16, sub_xmm>;

// A 128-bit subvector extract from the first 512-bit vector position is a
// subregister copy that needs no instruction. Likewise, a 128-bit subvector
// insert to the first 512-bit vector position is a subregister copy that needs
// no instruction.
defm : subvector_subreg_lowering<VR128, v4i32, VR512, v16i32, sub_xmm>;
defm : subvector_subreg_lowering<VR128, v4f32, VR512, v16f32, sub_xmm>;
defm : subvector_subreg_lowering<VR128, v2i64, VR512, v8i64,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v2f64, VR512, v8f64,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v8i16, VR512, v32i16, sub_xmm>;
defm : subvector_subreg_lowering<VR128, v16i8, VR512, v64i8,  sub_xmm>;
defm : subvector_subreg_lowering<VR128, v8f16, VR512, v32f16, sub_xmm>;
defm : subvector_subreg_lowering<VR128, v8bf16, VR512, v32bf16, sub_xmm>;

// A 128-bit subvector extract from the first 512-bit vector position is a
// subregister copy that needs no instruction. Likewise, a 128-bit subvector
// insert to the first 512-bit vector position is a subregister copy that needs
// no instruction.
defm : subvector_subreg_lowering<VR256, v8i32,  VR512, v16i32, sub_ymm>;
defm : subvector_subreg_lowering<VR256, v8f32,  VR512, v16f32, sub_ymm>;
defm : subvector_subreg_lowering<VR256, v4i64,  VR512, v8i64,  sub_ymm>;
defm : subvector_subreg_lowering<VR256, v4f64,  VR512, v8f64,  sub_ymm>;
defm : subvector_subreg_lowering<VR256, v16i16, VR512, v32i16, sub_ymm>;
defm : subvector_subreg_lowering<VR256, v32i8,  VR512, v64i8,  sub_ymm>;
defm : subvector_subreg_lowering<VR256, v16f16, VR512, v32f16, sub_ymm>;
defm : subvector_subreg_lowering<VR256, v16bf16, VR512, v32bf16, sub_ymm>;


// If we're inserting into an all zeros vector, just use a plain move which
// will zero the upper bits. A post-isel hook will take care of removing
// any moves that we can prove are unnecessary.
multiclass subvec_zero_lowering<string MoveStr,
                                RegisterClass RC, ValueType DstTy,
                                ValueType SrcTy, SubRegIndex SubIdx> {
  def : Pat<(DstTy (insert_subvector immAllZerosV,
                                     (SrcTy RC:$src), (iPTR 0))),
            (SUBREG_TO_REG (i64 0),
             (SrcTy (!cast<Instruction>("VMOV"#MoveStr#"rr") RC:$src)), SubIdx)>;
}

let Predicates = [HasAVX, NoVLX] in {
  defm : subvec_zero_lowering<"APD", VR128, v4f64, v2f64, sub_xmm>;
  defm : subvec_zero_lowering<"APS", VR128, v8f32, v4f32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v4i64, v2i64, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v8i32, v4i32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v16i16, v8i16, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v32i8, v16i8, sub_xmm>;
}

let Predicates = [HasAVXNECONVERT, NoVLX] in
  defm : subvec_zero_lowering<"DQA", VR128, v16bf16, v8bf16, sub_xmm>;

let Predicates = [HasVLX] in {
  defm : subvec_zero_lowering<"APDZ128", VR128X, v4f64, v2f64, sub_xmm>;
  defm : subvec_zero_lowering<"APSZ128", VR128X, v8f32, v4f32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v4i64, v2i64, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v8i32, v4i32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v16i16, v8i16, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v32i8, v16i8, sub_xmm>;

  defm : subvec_zero_lowering<"APDZ128", VR128X, v8f64, v2f64, sub_xmm>;
  defm : subvec_zero_lowering<"APSZ128", VR128X, v16f32, v4f32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v8i64, v2i64, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v16i32, v4i32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v32i16, v8i16, sub_xmm>;
  defm : subvec_zero_lowering<"DQA64Z128", VR128X, v64i8, v16i8, sub_xmm>;

  defm : subvec_zero_lowering<"APDZ256", VR256X, v8f64, v4f64, sub_ymm>;
  defm : subvec_zero_lowering<"APSZ256", VR256X, v16f32, v8f32, sub_ymm>;
  defm : subvec_zero_lowering<"DQA64Z256", VR256X, v8i64, v4i64, sub_ymm>;
  defm : subvec_zero_lowering<"DQA64Z256", VR256X, v16i32, v8i32, sub_ymm>;
  defm : subvec_zero_lowering<"DQA64Z256", VR256X, v32i16, v16i16, sub_ymm>;
  defm : subvec_zero_lowering<"DQA64Z256", VR256X, v64i8, v32i8, sub_ymm>;
}

let Predicates = [HasAVX512, NoVLX] in {
  defm : subvec_zero_lowering<"APD", VR128, v8f64, v2f64, sub_xmm>;
  defm : subvec_zero_lowering<"APS", VR128, v16f32, v4f32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v8i64, v2i64, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v16i32, v4i32, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v32i16, v8i16, sub_xmm>;
  defm : subvec_zero_lowering<"DQA", VR128, v64i8, v16i8, sub_xmm>;

  defm : subvec_zero_lowering<"APDY", VR256, v8f64, v4f64, sub_ymm>;
  defm : subvec_zero_lowering<"APSY", VR256, v16f32, v8f32, sub_ymm>;
  defm : subvec_zero_lowering<"DQAY", VR256, v8i64, v4i64, sub_ymm>;
  defm : subvec_zero_lowering<"DQAY", VR256, v16i32, v8i32, sub_ymm>;
  defm : subvec_zero_lowering<"DQAY", VR256, v32i16, v16i16, sub_ymm>;
  defm : subvec_zero_lowering<"DQAY", VR256, v64i8, v32i8, sub_ymm>;
}

let Predicates = [HasFP16, HasVLX] in {
  defm : subvec_zero_lowering<"APSZ128", VR128X, v16f16, v8f16, sub_xmm>;
  defm : subvec_zero_lowering<"APSZ128", VR128X, v32f16, v8f16, sub_xmm>;
  defm : subvec_zero_lowering<"APSZ256", VR256X, v32f16, v16f16, sub_ymm>;
}

let Predicates = [HasBF16, HasVLX] in {
  defm : subvec_zero_lowering<"APSZ128", VR128X, v16bf16, v8bf16, sub_xmm>;
  defm : subvec_zero_lowering<"APSZ128", VR128X, v32bf16, v8bf16, sub_xmm>;
  defm : subvec_zero_lowering<"APSZ256", VR256X, v32bf16, v16bf16, sub_ymm>;
}

class maskzeroupper<ValueType vt, RegisterClass RC> :
  PatLeaf<(vt RC:$src), [{
    return isMaskZeroExtended(N);
  }]>;

def maskzeroupperv1i1  : maskzeroupper<v1i1,  VK1>;
def maskzeroupperv2i1  : maskzeroupper<v2i1,  VK2>;
def maskzeroupperv4i1  : maskzeroupper<v4i1,  VK4>;
def maskzeroupperv8i1  : maskzeroupper<v8i1,  VK8>;
def maskzeroupperv16i1 : maskzeroupper<v16i1, VK16>;
def maskzeroupperv32i1 : maskzeroupper<v32i1, VK32>;

// The patterns determine if we can depend on the upper bits of a mask register
// being zeroed by the previous operation so that we can skip explicit
// zeroing.
let Predicates = [HasBWI] in {
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     maskzeroupperv1i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK1:$src, VK32)>;
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     maskzeroupperv8i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK8:$src, VK32)>;
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     maskzeroupperv16i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK16:$src, VK32)>;

  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     maskzeroupperv1i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK1:$src, VK64)>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     maskzeroupperv8i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK8:$src, VK64)>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     maskzeroupperv16i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK16:$src, VK64)>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     maskzeroupperv32i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK32:$src, VK64)>;
}

let Predicates = [HasAVX512] in {
  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     maskzeroupperv1i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK1:$src, VK16)>;
  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     maskzeroupperv8i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK8:$src, VK16)>;
}

let Predicates = [HasDQI] in {
  def : Pat<(v8i1 (insert_subvector (v8i1 immAllZerosV),
                                    maskzeroupperv1i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK1:$src, VK8)>;
}

let Predicates = [HasVLX, HasDQI] in {
  def : Pat<(v8i1 (insert_subvector (v8i1 immAllZerosV),
                                    maskzeroupperv2i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK2:$src, VK8)>;
  def : Pat<(v8i1 (insert_subvector (v8i1 immAllZerosV),
                                    maskzeroupperv4i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK4:$src, VK8)>;
}

let Predicates = [HasVLX] in {
  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     maskzeroupperv2i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK2:$src, VK16)>;
  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     maskzeroupperv4i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK4:$src, VK16)>;
}

let Predicates = [HasBWI, HasVLX] in {
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     maskzeroupperv2i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK2:$src, VK32)>;
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     maskzeroupperv4i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK4:$src, VK32)>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     maskzeroupperv2i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK2:$src, VK64)>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     maskzeroupperv4i1:$src, (iPTR 0))),
            (COPY_TO_REGCLASS VK4:$src, VK64)>;
}

// If the bits are not zero we have to fall back to explicitly zeroing by
// using shifts.
let Predicates = [HasAVX512] in {
  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     (v1i1 VK1:$mask), (iPTR 0))),
            (KSHIFTRWri (KSHIFTLWri (COPY_TO_REGCLASS VK1:$mask, VK16),
                                    (i8 15)), (i8 15))>;

  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     (v2i1 VK2:$mask), (iPTR 0))),
            (KSHIFTRWri (KSHIFTLWri (COPY_TO_REGCLASS VK2:$mask, VK16),
                                    (i8 14)), (i8 14))>;

  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     (v4i1 VK4:$mask), (iPTR 0))),
            (KSHIFTRWri (KSHIFTLWri (COPY_TO_REGCLASS VK4:$mask, VK16),
                                    (i8 12)), (i8 12))>;
}

let Predicates = [HasAVX512, NoDQI] in {
  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     (v8i1 VK8:$mask), (iPTR 0))),
            (KSHIFTRWri (KSHIFTLWri (COPY_TO_REGCLASS VK8:$mask, VK16),
                                    (i8 8)), (i8 8))>;
}

let Predicates = [HasDQI] in {
  def : Pat<(v16i1 (insert_subvector (v16i1 immAllZerosV),
                                     (v8i1 VK8:$mask), (iPTR 0))),
            (COPY_TO_REGCLASS (KMOVBkk VK8:$mask), VK16)>;

  def : Pat<(v8i1 (insert_subvector (v8i1 immAllZerosV),
                                    (v1i1 VK1:$mask), (iPTR 0))),
            (KSHIFTRBri (KSHIFTLBri (COPY_TO_REGCLASS VK1:$mask, VK8),
                                    (i8 7)), (i8 7))>;
  def : Pat<(v8i1 (insert_subvector (v8i1 immAllZerosV),
                                    (v2i1 VK2:$mask), (iPTR 0))),
            (KSHIFTRBri (KSHIFTLBri (COPY_TO_REGCLASS VK2:$mask, VK8),
                                    (i8 6)), (i8 6))>;
  def : Pat<(v8i1 (insert_subvector (v8i1 immAllZerosV),
                                    (v4i1 VK4:$mask), (iPTR 0))),
            (KSHIFTRBri (KSHIFTLBri (COPY_TO_REGCLASS VK4:$mask, VK8),
                                    (i8 4)), (i8 4))>;
}

let Predicates = [HasBWI] in {
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     (v16i1 VK16:$mask), (iPTR 0))),
            (COPY_TO_REGCLASS (KMOVWkk VK16:$mask), VK32)>;

  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     (v16i1 VK16:$mask), (iPTR 0))),
            (COPY_TO_REGCLASS (KMOVWkk VK16:$mask), VK64)>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     (v32i1 VK32:$mask), (iPTR 0))),
            (COPY_TO_REGCLASS (KMOVDkk VK32:$mask), VK64)>;
}

let Predicates = [HasBWI, NoDQI] in {
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     (v8i1 VK8:$mask), (iPTR 0))),
            (KSHIFTRDri (KSHIFTLDri (COPY_TO_REGCLASS VK8:$mask, VK32),
                                    (i8 24)), (i8 24))>;

  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     (v8i1 VK8:$mask), (iPTR 0))),
            (KSHIFTRQri (KSHIFTLQri (COPY_TO_REGCLASS VK8:$mask, VK64),
                                    (i8 56)), (i8 56))>;
}

let Predicates = [HasBWI, HasDQI] in {
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     (v8i1 VK8:$mask), (iPTR 0))),
            (COPY_TO_REGCLASS (KMOVBkk VK8:$mask), VK32)>;

  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     (v8i1 VK8:$mask), (iPTR 0))),
            (COPY_TO_REGCLASS (KMOVBkk VK8:$mask), VK64)>;
}

let Predicates = [HasBWI] in {
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     (v1i1 VK1:$mask), (iPTR 0))),
            (KSHIFTRDri (KSHIFTLDri (COPY_TO_REGCLASS VK1:$mask, VK32),
                                    (i8 31)), (i8 31))>;
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     (v2i1 VK2:$mask), (iPTR 0))),
            (KSHIFTRDri (KSHIFTLDri (COPY_TO_REGCLASS VK2:$mask, VK32),
                                    (i8 30)), (i8 30))>;
  def : Pat<(v32i1 (insert_subvector (v32i1 immAllZerosV),
                                     (v4i1 VK4:$mask), (iPTR 0))),
            (KSHIFTRDri (KSHIFTLDri (COPY_TO_REGCLASS VK4:$mask, VK32),
                                    (i8 28)), (i8 28))>;

  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     (v1i1 VK1:$mask), (iPTR 0))),
            (KSHIFTRQri (KSHIFTLQri (COPY_TO_REGCLASS VK1:$mask, VK64),
                                    (i8 63)), (i8 63))>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     (v2i1 VK2:$mask), (iPTR 0))),
            (KSHIFTRQri (KSHIFTLQri (COPY_TO_REGCLASS VK2:$mask, VK64),
                                    (i8 62)), (i8 62))>;
  def : Pat<(v64i1 (insert_subvector (v64i1 immAllZerosV),
                                     (v4i1 VK4:$mask), (iPTR 0))),
            (KSHIFTRQri (KSHIFTLQri (COPY_TO_REGCLASS VK4:$mask, VK64),
                                    (i8 60)), (i8 60))>;
}

//===----------------------------------------------------------------------===//
// Extra selection patterns for f128, f128mem

// movaps is shorter than movdqa. movaps is in SSE and movdqa is in SSE2.
let Predicates = [NoAVX] in {
def : Pat<(alignedstore (f128 VR128:$src), addr:$dst),
          (MOVAPSmr addr:$dst, VR128:$src)>;
def : Pat<(store (f128 VR128:$src), addr:$dst),
          (MOVUPSmr addr:$dst, VR128:$src)>;

def : Pat<(alignedloadf128 addr:$src),
          (MOVAPSrm addr:$src)>;
def : Pat<(loadf128 addr:$src),
          (MOVUPSrm addr:$src)>;
}

let Predicates = [HasAVX, NoVLX] in {
def : Pat<(alignedstore (f128 VR128:$src), addr:$dst),
          (VMOVAPSmr addr:$dst, VR128:$src)>;
def : Pat<(store (f128 VR128:$src), addr:$dst),
          (VMOVUPSmr addr:$dst, VR128:$src)>;

def : Pat<(alignedloadf128 addr:$src),
          (VMOVAPSrm addr:$src)>;
def : Pat<(loadf128 addr:$src),
          (VMOVUPSrm addr:$src)>;
}

let Predicates = [HasVLX] in {
def : Pat<(alignedstore (f128 VR128X:$src), addr:$dst),
          (VMOVAPSZ128mr addr:$dst, VR128X:$src)>;
def : Pat<(store (f128 VR128X:$src), addr:$dst),
          (VMOVUPSZ128mr addr:$dst, VR128X:$src)>;

def : Pat<(alignedloadf128 addr:$src),
          (VMOVAPSZ128rm addr:$src)>;
def : Pat<(loadf128 addr:$src),
          (VMOVUPSZ128rm addr:$src)>;
}

let Predicates = [UseSSE1] in {
// andps is shorter than andpd or pand. andps is SSE and andpd/pand are in SSE2
def : Pat<(f128 (X86fand VR128:$src1, (memopf128 addr:$src2))),
          (ANDPSrm VR128:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86fand VR128:$src1, VR128:$src2)),
          (ANDPSrr VR128:$src1, VR128:$src2)>;

def : Pat<(f128 (X86for VR128:$src1, (memopf128 addr:$src2))),
          (ORPSrm VR128:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86for VR128:$src1, VR128:$src2)),
          (ORPSrr VR128:$src1, VR128:$src2)>;

def : Pat<(f128 (X86fxor VR128:$src1, (memopf128 addr:$src2))),
          (XORPSrm VR128:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86fxor VR128:$src1, VR128:$src2)),
          (XORPSrr VR128:$src1, VR128:$src2)>;
}

let Predicates = [HasAVX, NoVLX] in {
// andps is shorter than andpd or pand. andps is SSE and andpd/pand are in SSE2
def : Pat<(f128 (X86fand VR128:$src1, (loadf128 addr:$src2))),
          (VANDPSrm VR128:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86fand VR128:$src1, VR128:$src2)),
          (VANDPSrr VR128:$src1, VR128:$src2)>;

def : Pat<(f128 (X86for VR128:$src1, (loadf128 addr:$src2))),
          (VORPSrm VR128:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86for VR128:$src1, VR128:$src2)),
          (VORPSrr VR128:$src1, VR128:$src2)>;

def : Pat<(f128 (X86fxor VR128:$src1, (loadf128 addr:$src2))),
          (VXORPSrm VR128:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86fxor VR128:$src1, VR128:$src2)),
          (VXORPSrr VR128:$src1, VR128:$src2)>;
}

let Predicates = [HasVLX] in {
// andps is shorter than andpd or pand. andps is SSE and andpd/pand are in SSE2
def : Pat<(f128 (X86fand VR128X:$src1, (loadf128 addr:$src2))),
          (VANDPSZ128rm VR128X:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86fand VR128X:$src1, VR128X:$src2)),
          (VANDPSZ128rr VR128X:$src1, VR128X:$src2)>;

def : Pat<(f128 (X86for VR128X:$src1, (loadf128 addr:$src2))),
          (VORPSZ128rm VR128X:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86for VR128X:$src1, VR128X:$src2)),
          (VORPSZ128rr VR128X:$src1, VR128X:$src2)>;

def : Pat<(f128 (X86fxor VR128X:$src1, (loadf128 addr:$src2))),
          (VXORPSZ128rm VR128X:$src1, f128mem:$src2)>;

def : Pat<(f128 (X86fxor VR128X:$src1, VR128X:$src2)),
          (VXORPSZ128rr VR128X:$src1, VR128X:$src2)>;
}
