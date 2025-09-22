//===- HexagonPatternsHVX.td - Selection Patterns for HVX --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def HQ8:    PatLeaf<(VecQ8   HvxQR:$R)>;
def HQ16:   PatLeaf<(VecQ16  HvxQR:$R)>;
def HQ32:   PatLeaf<(VecQ32  HvxQR:$R)>;

def HVI8:   PatLeaf<(VecI8   HvxVR:$R)>;
def HVI16:  PatLeaf<(VecI16  HvxVR:$R)>;
def HVI32:  PatLeaf<(VecI32  HvxVR:$R)>;
def HVF16:  PatLeaf<(VecF16  HvxVR:$R)>;
def HVF32:  PatLeaf<(VecF32  HvxVR:$R)>;

def HWI8:   PatLeaf<(VecPI8  HvxWR:$R)>;
def HWI16:  PatLeaf<(VecPI16 HvxWR:$R)>;
def HWI32:  PatLeaf<(VecPI32 HvxWR:$R)>;
def HWF16:  PatLeaf<(VecPF16 HvxWR:$R)>;
def HWF32:  PatLeaf<(VecPF32 HvxWR:$R)>;

def SDTVecUnaryOp:
  SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisVec<1>]>;

def SDTVecBinOp:
  SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>, SDTCisSameAs<1,2>]>;

def SDTHexagonVEXTRACTW: SDTypeProfile<1, 2,
  [SDTCisVT<0, i32>, SDTCisVec<1>, SDTCisVT<2, i32>]>;
def HexagonVEXTRACTW : SDNode<"HexagonISD::VEXTRACTW", SDTHexagonVEXTRACTW>;

def SDTHexagonVINSERTW0: SDTypeProfile<1, 2,
  [SDTCisVec<0>, SDTCisSameAs<0, 1>, SDTCisVT<2, i32>]>;
def HexagonVINSERTW0: SDNode<"HexagonISD::VINSERTW0", SDTHexagonVINSERTW0>;

def HwLen2: SDNodeXForm<imm, [{
  const auto &ST = CurDAG->getSubtarget<HexagonSubtarget>();
  return CurDAG->getTargetConstant(ST.getVectorLength()/2, SDLoc(N), MVT::i32);
}]>;

def Q2V: OutPatFrag<(ops node:$Qs), (V6_vandqrt $Qs, (ToI32 -1))>;

def Combinev: OutPatFrag<(ops node:$Vs, node:$Vt),
  (REG_SEQUENCE HvxWR, $Vs, vsub_hi, $Vt, vsub_lo)>;

def Combineq: OutPatFrag<(ops node:$Qs, node:$Qt),
  (V6_vandvrt
    (V6_vor
      (V6_vror (V6_vpackeb (V6_vd0), (Q2V $Qs)),
               (ToI32 (HwLen2 (i32 0)))),  // Half the vector length
      (V6_vpackeb (V6_vd0), (Q2V $Qt))),
    (ToI32 -1))>;

def LoVec: OutPatFrag<(ops node:$Vs), (EXTRACT_SUBREG $Vs, vsub_lo)>;
def HiVec: OutPatFrag<(ops node:$Vs), (EXTRACT_SUBREG $Vs, vsub_hi)>;

def HexagonQCAT:       SDNode<"HexagonISD::QCAT",       SDTVecBinOp>;
def HexagonQTRUE:      SDNode<"HexagonISD::QTRUE",      SDTVecLeaf>;
def HexagonQFALSE:     SDNode<"HexagonISD::QFALSE",     SDTVecLeaf>;

def vzero:  PatFrags<(ops), [(splat_vector (i32 0)), (splat_vector (f32zero))]>;
def qtrue:  PatFrag<(ops), (HexagonQTRUE)>;
def qfalse: PatFrag<(ops), (HexagonQFALSE)>;
def qcat:   PatFrag<(ops node:$Qs, node:$Qt),
                    (HexagonQCAT node:$Qs, node:$Qt)>;

def qnot:     PatFrag<(ops node:$Qs), (xor node:$Qs, qtrue)>;

def VSxtb: OutPatFrag<(ops node:$Vs), (V6_vunpackb  $Vs)>;
def VSxth: OutPatFrag<(ops node:$Vs), (V6_vunpackh  $Vs)>;
def VZxtb: OutPatFrag<(ops node:$Vs), (V6_vunpackub $Vs)>;
def VZxth: OutPatFrag<(ops node:$Vs), (V6_vunpackuh $Vs)>;

class VSubi<InstHexagon VSub, InstHexagon VSplati>:
  OutPatFrag<(ops node:$Imm, node:$Vs), (VSub (VSplati (i32 $Imm)), $Vs)>;

def VSubib: VSubi<V6_vsubb, PS_vsplatib>;
def VSubih: VSubi<V6_vsubh, PS_vsplatih>;
def VSubiw: VSubi<V6_vsubw, PS_vsplatiw>;

def VNegb: OutPatFrag<(ops node:$Vs), (VSubib 0, $Vs)>;
def VNegh: OutPatFrag<(ops node:$Vs), (VSubih 0, $Vs)>;
def VNegw: OutPatFrag<(ops node:$Vs), (VSubiw 0, $Vs)>;

class pf3<SDNode Op>: PatFrag<(ops node:$a, node:$b, node:$c),
                              (Op node:$a, node:$b, node:$c)>;

def Mfshl: pf3<HexagonMFSHL>;
def Mfshr: pf3<HexagonMFSHR>;

def IsVecOff : PatLeaf<(i32 imm), [{
  int32_t V = N->getSExtValue();
  int32_t VecSize = HRI->getSpillSize(Hexagon::HvxVRRegClass);
  assert(isPowerOf2_32(VecSize));
  if ((uint32_t(V) & (uint32_t(VecSize)-1)) != 0)
    return false;
  int32_t L = Log2_32(VecSize);
  return isInt<4>(V >> L);
}]>;


def alignedload: PatFrag<(ops node:$a), (load $a), [{
  return isAlignedMemNode(cast<MemSDNode>(N));
}]>;

def unalignedload: PatFrag<(ops node:$a), (load $a), [{
  return !isAlignedMemNode(cast<MemSDNode>(N));
}]>;

def alignedstore: PatFrag<(ops node:$v, node:$a), (store $v, $a), [{
  return isAlignedMemNode(cast<MemSDNode>(N));
}]>;

def unalignedstore: PatFrag<(ops node:$v, node:$a), (store $v, $a), [{
  return !isAlignedMemNode(cast<MemSDNode>(N));
}]>;


// HVX loads

multiclass HvxLdfi_pat<InstHexagon MI, PatFrag Load, ValueType ResType,
                       PatFrag ImmPred> {
  def: Pat<(ResType (Load (add (i32 AddrFI:$fi), ImmPred:$Off))),
           (MI AddrFI:$fi, imm:$Off)>;
  def: Pat<(ResType (Load (IsOrAdd (i32 AddrFI:$fi), ImmPred:$Off))),
           (MI AddrFI:$fi, imm:$Off)>;
  def: Pat<(ResType (Load AddrFI:$fi)), (ResType (MI AddrFI:$fi, 0))>;
}

multiclass HvxLdgi_pat<InstHexagon MI, PatFrag Load, ValueType ResType,
                     PatFrag ImmPred> {
  def: Pat<(ResType (Load (add I32:$Rt, ImmPred:$Off))),
           (MI I32:$Rt, imm:$Off)>;
  def: Pat<(ResType (Load I32:$Rt)),
           (MI I32:$Rt, 0)>;
}

multiclass HvxLdc_pat<InstHexagon MI, PatFrag Load, ValueType ResType> {
  // The HVX selection code for shuffles can generate vector constants.
  // Calling "Select" on the resulting loads from CP fails without these
  // patterns.
  def: Pat<(ResType (Load (HexagonCP tconstpool:$Addr))),
           (MI (ToI32 imm:$Addr), 0)>;
  def: Pat<(ResType (Load (HexagonAtPcrel tconstpool:$Addr))),
           (MI (C4_addipc imm:$Addr), 0)>;
}

multiclass HvxLd_pat<InstHexagon MI, PatFrag Load, ValueType ResType,
                     PatFrag ImmPred> {
  defm: HvxLdfi_pat<MI, Load, ResType, ImmPred>;
  defm: HvxLdgi_pat<MI, Load, ResType, ImmPred>;
  defm: HvxLdc_pat <MI, Load, ResType>;
}

// Aligned loads: everything, plus loads with valignaddr node.
multiclass HvxLda_pat<InstHexagon MI, PatFrag Load, ValueType ResType,
                      PatFrag ImmPred> {
  let AddedComplexity = 50 in {
    def: Pat<(ResType (Load (valignaddr I32:$Rt))),
             (MI I32:$Rt, 0)>;
    def: Pat<(ResType (Load (add (valignaddr I32:$Rt), ImmPred:$Off))),
             (MI I32:$Rt, imm:$Off)>;
  }
  defm: HvxLd_pat<MI, Load, ResType, ImmPred>;
}

let Predicates = [UseHVX] in {
  // alignedload will match a non-temporal load as well, so try non-temporal
  // first.
  defm: HvxLda_pat<V6_vL32b_nt_ai, alignednontemporalload, VecI8,  IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_nt_ai, alignednontemporalload, VecI16, IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_nt_ai, alignednontemporalload, VecI32, IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_ai,               alignedload, VecI8,  IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_ai,               alignedload, VecI16, IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_ai,               alignedload, VecI32, IsVecOff>;
  defm: HvxLd_pat<V6_vL32Ub_ai,             unalignedload, VecI8,  IsVecOff>;
  defm: HvxLd_pat<V6_vL32Ub_ai,             unalignedload, VecI16, IsVecOff>;
  defm: HvxLd_pat<V6_vL32Ub_ai,             unalignedload, VecI32, IsVecOff>;
}

let Predicates = [UseHVXV68] in {
  defm: HvxLda_pat<V6_vL32b_nt_ai, alignednontemporalload, VecF16, IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_nt_ai, alignednontemporalload, VecF32, IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_ai,               alignedload, VecF16, IsVecOff>;
  defm: HvxLda_pat<V6_vL32b_ai,               alignedload, VecF32, IsVecOff>;
  defm: HvxLd_pat<V6_vL32Ub_ai,             unalignedload, VecF16, IsVecOff>;
  defm: HvxLd_pat<V6_vL32Ub_ai,             unalignedload, VecF32, IsVecOff>;
}

// HVX stores

multiclass HvxStfi_pat<InstHexagon MI, PatFrag Store, PatFrag Value,
                       PatFrag ImmPred> {
  def: Pat<(Store Value:$Vs, (add (i32 AddrFI:$fi), ImmPred:$Off)),
           (MI AddrFI:$fi, imm:$Off, Value:$Vs)>;
  def: Pat<(Store Value:$Vs, (IsOrAdd (i32 AddrFI:$fi), ImmPred:$Off)),
           (MI AddrFI:$fi, imm:$Off, Value:$Vs)>;
  def: Pat<(Store Value:$Vs, AddrFI:$fi),
           (MI AddrFI:$fi, 0, Value:$Vs)>;
}

multiclass HvxStgi_pat<InstHexagon MI, PatFrag Store, PatFrag Value,
                       PatFrag ImmPred> {
  def: Pat<(Store Value:$Vs, (add I32:$Rt, ImmPred:$Off)),
           (MI I32:$Rt, imm:$Off, Value:$Vs)>;
  def: Pat<(Store Value:$Vs, (IsOrAdd I32:$Rt, ImmPred:$Off)),
           (MI I32:$Rt, imm:$Off, Value:$Vs)>;
  def: Pat<(Store Value:$Vs, I32:$Rt),
           (MI I32:$Rt, 0, Value:$Vs)>;
}

multiclass HvxSt_pat<InstHexagon MI, PatFrag Store, PatFrag Value,
                     PatFrag ImmPred> {
  defm: HvxStfi_pat<MI, Store, Value, ImmPred>;
  defm: HvxStgi_pat<MI, Store, Value, ImmPred>;
}

let Predicates = [UseHVX] in {
  // alignedstore will match a non-temporal store as well, so try non-temporal
  // first.
  defm: HvxSt_pat<V6_vS32b_nt_ai, alignednontemporalstore,  HVI8, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_nt_ai, alignednontemporalstore, HVI16, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_nt_ai, alignednontemporalstore, HVI32, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_ai,               alignedstore,  HVI8, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_ai,               alignedstore, HVI16, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_ai,               alignedstore, HVI32, IsVecOff>;
  defm: HvxSt_pat<V6_vS32Ub_ai,            unalignedstore,  HVI8, IsVecOff>;
  defm: HvxSt_pat<V6_vS32Ub_ai,            unalignedstore, HVI16, IsVecOff>;
  defm: HvxSt_pat<V6_vS32Ub_ai,            unalignedstore, HVI32, IsVecOff>;
}

let Predicates = [UseHVXV68] in {
  defm: HvxSt_pat<V6_vS32b_nt_ai, alignednontemporalstore, HVF16, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_nt_ai, alignednontemporalstore, HVF32, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_ai,               alignedstore, HVF16, IsVecOff>;
  defm: HvxSt_pat<V6_vS32b_ai,               alignedstore, HVF32, IsVecOff>;
  defm: HvxSt_pat<V6_vS32Ub_ai,            unalignedstore, HVF16, IsVecOff>;
  defm: HvxSt_pat<V6_vS32Ub_ai,            unalignedstore, HVF32, IsVecOff>;
}

// Bitcasts between same-size vector types are no-ops, except for the
// actual type change.
let Predicates = [UseHVX] in {
  defm: NopCast_pat<VecI8,   VecI16,  HvxVR>;
  defm: NopCast_pat<VecI8,   VecI32,  HvxVR>;
  defm: NopCast_pat<VecI16,  VecI32,  HvxVR>;

  defm: NopCast_pat<VecPI8,  VecPI16, HvxWR>;
  defm: NopCast_pat<VecPI8,  VecPI32, HvxWR>;
  defm: NopCast_pat<VecPI16, VecPI32, HvxWR>;
}

let Predicates = [UseHVX, UseHVXFloatingPoint] in {
  defm: NopCast_pat<VecI8,   VecF16,  HvxVR>;
  defm: NopCast_pat<VecI8,   VecF32,  HvxVR>;
  defm: NopCast_pat<VecI16,  VecF16,  HvxVR>;
  defm: NopCast_pat<VecI16,  VecF32,  HvxVR>;
  defm: NopCast_pat<VecI32,  VecF16,  HvxVR>;
  defm: NopCast_pat<VecI32,  VecF32,  HvxVR>;
  defm: NopCast_pat<VecF16,  VecF32,  HvxVR>;

  defm: NopCast_pat<VecPI8,  VecPF16, HvxWR>;
  defm: NopCast_pat<VecPI8,  VecPF32, HvxWR>;
  defm: NopCast_pat<VecPI16, VecPF16, HvxWR>;
  defm: NopCast_pat<VecPI16, VecPF32, HvxWR>;
  defm: NopCast_pat<VecPI32, VecPF16, HvxWR>;
  defm: NopCast_pat<VecPI32, VecPF32, HvxWR>;
  defm: NopCast_pat<VecPF16, VecPF32, HvxWR>;
}

let Predicates = [UseHVX] in {
  let AddedComplexity = 100 in {
    // These should be preferred over a vsplat of 0.
    def: Pat<(VecI8   vzero), (V6_vd0)>;
    def: Pat<(VecI16  vzero), (V6_vd0)>;
    def: Pat<(VecI32  vzero), (V6_vd0)>;
    def: Pat<(VecPI8  vzero), (PS_vdd0)>;
    def: Pat<(VecPI16 vzero), (PS_vdd0)>;
    def: Pat<(VecPI32 vzero), (PS_vdd0)>;
    def: Pat<(VecPF32 vzero), (PS_vdd0)>;

    def: Pat<(concat_vectors  (VecI8 vzero),  (VecI8 vzero)), (PS_vdd0)>;
    def: Pat<(concat_vectors (VecI16 vzero), (VecI16 vzero)), (PS_vdd0)>;
    def: Pat<(concat_vectors (VecI32 vzero), (VecI32 vzero)), (PS_vdd0)>;
  }

  def: Pat<(VecPI8 (concat_vectors HVI8:$Vs, HVI8:$Vt)),
           (Combinev HvxVR:$Vt, HvxVR:$Vs)>;
  def: Pat<(VecPI16 (concat_vectors HVI16:$Vs, HVI16:$Vt)),
           (Combinev HvxVR:$Vt, HvxVR:$Vs)>;
  def: Pat<(VecPI32 (concat_vectors HVI32:$Vs, HVI32:$Vt)),
           (Combinev HvxVR:$Vt, HvxVR:$Vs)>;

  def: Pat<(VecQ8  (qcat HQ16:$Qs, HQ16:$Qt)), (Combineq $Qt, $Qs)>;
  def: Pat<(VecQ16 (qcat HQ32:$Qs, HQ32:$Qt)), (Combineq $Qt, $Qs)>;

  def: Pat<(HexagonVEXTRACTW HVI8:$Vu, I32:$Rs),
           (V6_extractw HvxVR:$Vu, I32:$Rs)>;
  def: Pat<(HexagonVEXTRACTW HVI16:$Vu, I32:$Rs),
           (V6_extractw HvxVR:$Vu, I32:$Rs)>;
  def: Pat<(HexagonVEXTRACTW HVI32:$Vu, I32:$Rs),
           (V6_extractw HvxVR:$Vu, I32:$Rs)>;

  def: Pat<(HexagonVINSERTW0 HVI8:$Vu,  I32:$Rt),
           (V6_vinsertwr HvxVR:$Vu, I32:$Rt)>;
  def: Pat<(HexagonVINSERTW0 HVI16:$Vu, I32:$Rt),
           (V6_vinsertwr HvxVR:$Vu, I32:$Rt)>;
  def: Pat<(HexagonVINSERTW0 HVI32:$Vu, I32:$Rt),
           (V6_vinsertwr HvxVR:$Vu, I32:$Rt)>;
}

let Predicates = [UseHVX, UseHVXFloatingPoint] in {
  let AddedComplexity = 100 in {
    def: Pat<(VecF16  vzero), (V6_vd0)>;
    def: Pat<(VecF32  vzero), (V6_vd0)>;
    def: Pat<(VecPF16 vzero), (PS_vdd0)>;
    def: Pat<(VecPF32 vzero), (PS_vdd0)>;

    def: Pat<(concat_vectors (VecF16 vzero), (VecF16 vzero)), (PS_vdd0)>;
    def: Pat<(concat_vectors (VecF32 vzero), (VecF32 vzero)), (PS_vdd0)>;
  }

  def: Pat<(VecPF16 (concat_vectors HVF16:$Vs, HVF16:$Vt)),
           (Combinev HvxVR:$Vt, HvxVR:$Vs)>;
  def: Pat<(VecPF32 (concat_vectors HVF32:$Vs, HVF32:$Vt)),
           (Combinev HvxVR:$Vt, HvxVR:$Vs)>;

  def: Pat<(HexagonVINSERTW0 HVF16:$Vu, I32:$Rt),
           (V6_vinsertwr HvxVR:$Vu, I32:$Rt)>;
  def: Pat<(HexagonVINSERTW0 HVF32:$Vu, I32:$Rt),
           (V6_vinsertwr HvxVR:$Vu, I32:$Rt)>;
}

def Rep: OutPatFrag<(ops node:$N), (Combinev $N, $N)>;

let Predicates = [UseHVX] in {
  let AddedComplexity = 10 in {
    def: Pat<(VecI8   (splat_vector u8_0ImmPred:$V)),  (PS_vsplatib imm:$V)>;
    def: Pat<(VecI16  (splat_vector u16_0ImmPred:$V)), (PS_vsplatih imm:$V)>;
    def: Pat<(VecI32  (splat_vector anyimm:$V)),       (PS_vsplatiw imm:$V)>;
    def: Pat<(VecPI8  (splat_vector u8_0ImmPred:$V)),  (Rep (PS_vsplatib imm:$V))>;
    def: Pat<(VecPI16 (splat_vector u16_0ImmPred:$V)), (Rep (PS_vsplatih imm:$V))>;
    def: Pat<(VecPI32 (splat_vector anyimm:$V)),       (Rep (PS_vsplatiw imm:$V))>;
  }
  def: Pat<(VecI8   (splat_vector I32:$Rs)), (PS_vsplatrb $Rs)>;
  def: Pat<(VecI16  (splat_vector I32:$Rs)), (PS_vsplatrh $Rs)>;
  def: Pat<(VecI32  (splat_vector I32:$Rs)), (PS_vsplatrw $Rs)>;
  def: Pat<(VecPI8  (splat_vector I32:$Rs)), (Rep (PS_vsplatrb $Rs))>;
  def: Pat<(VecPI16 (splat_vector I32:$Rs)), (Rep (PS_vsplatrh $Rs))>;
  def: Pat<(VecPI32 (splat_vector I32:$Rs)), (Rep (PS_vsplatrw $Rs))>;
}
let Predicates = [UseHVXV68, UseHVXFloatingPoint] in {
  let AddedComplexity = 30 in {
    def: Pat<(VecF16  (splat_vector u16_0ImmPred:$V)), (PS_vsplatih imm:$V)>;
    def: Pat<(VecF32  (splat_vector anyint:$V)),       (PS_vsplatiw imm:$V)>;
    def: Pat<(VecF32  (splat_vector f32ImmPred:$V)),   (PS_vsplatiw (ftoi $V))>;
  }
  let AddedComplexity = 20 in {
    def: Pat<(VecF16  (splat_vector I32:$Rs)), (PS_vsplatrh $Rs)>;
    def: Pat<(VecF32  (splat_vector I32:$Rs)), (PS_vsplatrw $Rs)>;
    def: Pat<(VecF32  (splat_vector F32:$Rs)), (PS_vsplatrw $Rs)>;
  }
}

class Vneg1<ValueType VecTy>
  : PatFrag<(ops), (VecTy (splat_vector (i32 -1)))>;

class Vnot<ValueType VecTy>
  : PatFrag<(ops node:$Vs), (xor $Vs, Vneg1<VecTy>)>;

let Predicates = [UseHVX] in {
  let AddedComplexity = 200 in {
    def: Pat<(Vnot<VecI8>   HVI8:$Vs), (V6_vnot HvxVR:$Vs)>;
    def: Pat<(Vnot<VecI16> HVI16:$Vs), (V6_vnot HvxVR:$Vs)>;
    def: Pat<(Vnot<VecI32> HVI32:$Vs), (V6_vnot HvxVR:$Vs)>;
  }

  def: OpR_RR_pat<V6_vaddb,    Add,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vaddh,    Add,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vaddw,    Add,  VecI32, HVI32>;
  def: OpR_RR_pat<V6_vaddb_dv, Add,  VecPI8,  HWI8>;
  def: OpR_RR_pat<V6_vaddh_dv, Add, VecPI16, HWI16>;
  def: OpR_RR_pat<V6_vaddw_dv, Add, VecPI32, HWI32>;
  def: OpR_RR_pat<V6_vsubb,    Sub,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vsubh,    Sub,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vsubw,    Sub,  VecI32, HVI32>;
  def: OpR_RR_pat<V6_vsubb_dv, Sub,  VecPI8,  HWI8>;
  def: OpR_RR_pat<V6_vsubh_dv, Sub, VecPI16, HWI16>;
  def: OpR_RR_pat<V6_vsubw_dv, Sub, VecPI32, HWI32>;
  def: OpR_RR_pat<V6_vand,     And,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vand,     And,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vand,     And,  VecI32, HVI32>;
  def: OpR_RR_pat<V6_vor,       Or,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vor,       Or,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vor,       Or,  VecI32, HVI32>;
  def: OpR_RR_pat<V6_vxor,     Xor,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vxor,     Xor,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vxor,     Xor,  VecI32, HVI32>;

  def: OpR_RR_pat<V6_vminb,   Smin,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vmaxb,   Smax,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vminub,  Umin,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vmaxub,  Umax,   VecI8,  HVI8>;
  def: OpR_RR_pat<V6_vminh,   Smin,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vmaxh,   Smax,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vminuh,  Umin,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vmaxuh,  Umax,  VecI16, HVI16>;
  def: OpR_RR_pat<V6_vminw,   Smin,  VecI32, HVI32>;
  def: OpR_RR_pat<V6_vmaxw,   Smax,  VecI32, HVI32>;

  def: Pat<(vselect HQ8:$Qu, HVI8:$Vs, HVI8:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(vselect HQ16:$Qu, HVI16:$Vs, HVI16:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(vselect HQ32:$Qu, HVI32:$Vs, HVI32:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vs, HvxVR:$Vt)>;

  def: Pat<(vselect (qnot HQ8:$Qu), HVI8:$Vs, HVI8:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vt, HvxVR:$Vs)>;
  def: Pat<(vselect (qnot HQ16:$Qu), HVI16:$Vs, HVI16:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vt, HvxVR:$Vs)>;
  def: Pat<(vselect (qnot HQ32:$Qu), HVI32:$Vs, HVI32:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vt, HvxVR:$Vs)>;
}

// For now, we always deal with vector floating point in SF mode.
class OpR_RR_pat_conv<InstHexagon MI, PatFrag Op, ValueType ResType,
                      PatFrag RsPred, PatFrag RtPred = RsPred>
  : Pat<(ResType (Op RsPred:$Rs, RtPred:$Rt)),
        (V6_vconv_sf_qf32 (VecF32 (MI RsPred:$Rs, RtPred:$Rt)))>;

class OpR_RR_pat_conv_hf<InstHexagon MI, PatFrag Op, ValueType ResType,
                      PatFrag RsPred, PatFrag RtPred = RsPred>
  : Pat<(ResType (Op RsPred:$Rs, RtPred:$Rt)),
        (V6_vconv_hf_qf16 (VecF16 (MI RsPred:$Rs, RtPred:$Rt)))>;

let Predicates = [UseHVXV68, UseHVXQFloat] in {
  def: OpR_RR_pat_conv_hf<V6_vsub_hf,        pf2<fsub>,  VecF16, HVF16>;
  def: OpR_RR_pat_conv_hf<V6_vadd_hf,        pf2<fadd>,  VecF16, HVF16>;
  def: OpR_RR_pat_conv_hf<V6_vmpy_qf16_hf,   pf2<fmul>,  VecF16, HVF16>;
  def: OpR_RR_pat_conv<V6_vsub_sf,        pf2<fsub>,  VecF32, HVF32>;
  def: OpR_RR_pat_conv<V6_vadd_sf,        pf2<fadd>,  VecF32, HVF32>;
  def: OpR_RR_pat_conv<V6_vmpy_qf32_sf,   pf2<fmul>,  VecF32, HVF32>;

  // For now we assume that the fp32 register is always coming in as IEEE float
  // since the qfloat arithmetic instructions above always generate the
  // accompanying conversions as part of their pattern
  def: Pat<(VecF16 (pf1<fpround> HWF32:$Vuu)),
           (V6_vdealh (V6_vconv_hf_qf32
             (VecPF32 (Combinev (V6_vadd_sf (HiVec HvxWR:$Vuu), (V6_vd0)),
                                (V6_vadd_sf (LoVec HvxWR:$Vuu), (V6_vd0))
             ))))>;
  // fpextend for QFloat is handled manually in HexagonISelLoweringHVX.cpp.
}

// HVX IEEE arithmetic Instructions
let Predicates = [UseHVXV68, UseHVXIEEEFP] in {
  def: Pat<(fadd HVF16:$Rs, HVF16:$Rt),
           (V6_vadd_hf_hf HVF16:$Rs, HVF16:$Rt)>;
  def: Pat<(fadd HVF32:$Rs, HVF32:$Rt),
           (V6_vadd_sf_sf HVF32:$Rs, HVF32:$Rt)>;
  def: Pat<(fsub HVF16:$Rs, HVF16:$Rt),
           (V6_vsub_hf_hf HVF16:$Rs, HVF16:$Rt)>;
  def: Pat<(fsub HVF32:$Rs, HVF32:$Rt),
           (V6_vsub_sf_sf HVF32:$Rs, HVF32:$Rt)>;
  def: Pat<(fmul HVF16:$Rs, HVF16:$Rt),
           (V6_vmpy_hf_hf HVF16:$Rs, HVF16:$Rt)>;
  def: Pat<(fmul HVF32:$Rs, HVF32:$Rt),
           (V6_vmpy_sf_sf HVF32:$Rs, HVF32:$Rt)>;

  def: Pat<(VecF16 (pf1<fpround> HWF32:$Vuu)),
           (V6_vdealh (V6_vcvt_hf_sf (HiVec HvxWR:$Vuu), (LoVec HvxWR:$Vuu)))>;
  def: Pat<(VecPF32 (pf1<fpextend> HVF16:$Vu)),
           (V6_vcvt_sf_hf (V6_vshuffh HvxVR:$Vu))>;

  def: OpR_R_pat<V6_vcvt_h_hf,  Fptosi, VecI16, HVF16>;
  def: OpR_R_pat<V6_vcvt_uh_hf, Fptoui, VecI16, HVF16>;
  def: OpR_R_pat<V6_vcvt_hf_h,  Sitofp, VecF16, HVI16>;
  def: OpR_R_pat<V6_vcvt_hf_uh, Uitofp, VecF16, HVI16>;

  def: Pat<(VecI8 (Fptosi HWF16:$Vu)),
           (V6_vcvt_b_hf (HiVec $Vu), (LoVec $Vu))>;
  def: Pat<(VecI8 (Fptoui HWF16:$Vu)),
           (V6_vcvt_ub_hf (HiVec $Vu), (LoVec $Vu))>;
  def: Pat<(VecPF16 (Sitofp HVI8:$Vu)), (V6_vcvt_hf_b HvxVR:$Vu)>;
  def: Pat<(VecPF16 (Uitofp HVI8:$Vu)), (V6_vcvt_hf_ub HvxVR:$Vu)>;
}

let Predicates = [UseHVXV68, UseHVXFloatingPoint] in {
  def: Pat<(vselect HQ16:$Qu, HVF16:$Vs, HVF16:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(vselect (qnot HQ16:$Qu), HVF16:$Vs, HVF16:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vt, HvxVR:$Vs)>;

  def: Pat<(vselect HQ32:$Qu, HVF32:$Vs, HVF32:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(vselect (qnot HQ32:$Qu), HVF32:$Vs, HVF32:$Vt),
           (V6_vmux HvxQR:$Qu, HvxVR:$Vt, HvxVR:$Vs)>;
}

let Predicates = [UseHVXV68, UseHVX128B, UseHVXQFloat] in {
  let AddedComplexity = 220 in {
    defm: MinMax_pats<V6_vmin_hf, V6_vmax_hf, vselect,  setgt, VecQ16, HVF16>;
    defm: MinMax_pats<V6_vmin_hf, V6_vmax_hf, vselect, setogt, VecQ16, HVF16>;
    defm: MinMax_pats<V6_vmin_sf, V6_vmax_sf, vselect,  setgt, VecQ32, HVF32>;
    defm: MinMax_pats<V6_vmin_sf, V6_vmax_sf, vselect, setogt, VecQ32, HVF32>;
  }
  def: OpR_RR_pat<V6_vmin_hf, pf2<fminnum>, VecF16, HVF16>;
  def: OpR_RR_pat<V6_vmax_hf, pf2<fmaxnum>, VecF16, HVF16>;
  def: OpR_RR_pat<V6_vmin_sf, pf2<fminnum>, VecF32, HVF32>;
  def: OpR_RR_pat<V6_vmax_sf, pf2<fmaxnum>, VecF32, HVF32>;
}

let Predicates = [UseHVXV68, UseHVX128B, UseHVXIEEEFP] in {
  let AddedComplexity = 220 in {
    defm: MinMax_pats<V6_vfmin_hf, V6_vfmax_hf, vselect,  setgt, VecQ16, HVF16>;
    defm: MinMax_pats<V6_vfmin_hf, V6_vfmax_hf, vselect, setogt, VecQ16, HVF16>;
    defm: MinMax_pats<V6_vfmin_sf, V6_vfmax_sf, vselect,  setgt, VecQ32, HVF32>;
    defm: MinMax_pats<V6_vfmin_sf, V6_vfmax_sf, vselect, setogt, VecQ32, HVF32>;
  }
  def: OpR_RR_pat<V6_vfmin_hf, pf2<fminnum>, VecF16, HVF16>;
  def: OpR_RR_pat<V6_vfmax_hf, pf2<fmaxnum>, VecF16, HVF16>;
  def: OpR_RR_pat<V6_vfmin_sf, pf2<fminnum>, VecF32, HVF32>;
  def: OpR_RR_pat<V6_vfmax_sf, pf2<fmaxnum>, VecF32, HVF32>;
}

let Predicates = [UseHVX] in {
  // For i8 vectors Vs = (a0, a1, ...), Vt = (b0, b1, ...),
  // V6_vmpybv Vs, Vt produces a pair of i16 vectors Hi:Lo,
  // where Lo = (a0*b0, a2*b2, ...), Hi = (a1*b1, a3*b3, ...).
  def: Pat<(mul HVI8:$Vs, HVI8:$Vt),
           (V6_vshuffeb (HiVec (V6_vmpybv HvxVR:$Vs, HvxVR:$Vt)),
                        (LoVec (V6_vmpybv HvxVR:$Vs, HvxVR:$Vt)))>;
  def: Pat<(mul HVI16:$Vs, HVI16:$Vt),
           (V6_vmpyih HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(mul HVI32:$Vs, HVI32:$Vt),
           (V6_vmpyiewuh_acc (V6_vmpyieoh HvxVR:$Vs, HvxVR:$Vt),
                             HvxVR:$Vs, HvxVR:$Vt)>;
}

let Predicates = [UseHVX] in {
  def: Pat<(VecPI16 (sext HVI8:$Vs)),  (VSxtb $Vs)>;
  def: Pat<(VecPI32 (sext HVI16:$Vs)), (VSxth $Vs)>;
  def: Pat<(VecPI16 (zext HVI8:$Vs)),  (VZxtb $Vs)>;
  def: Pat<(VecPI32 (zext HVI16:$Vs)), (VZxth $Vs)>;

  def: Pat<(VecI16 (sext_invec HVI8:$Vs)),  (LoVec (VSxtb $Vs))>;
  def: Pat<(VecI32 (sext_invec HVI16:$Vs)), (LoVec (VSxth $Vs))>;
  def: Pat<(VecI32 (sext_invec HVI8:$Vs)),
           (LoVec (VSxth (LoVec (VSxtb $Vs))))>;
  def: Pat<(VecPI16 (sext_invec HWI8:$Vss)),  (VSxtb (LoVec $Vss))>;
  def: Pat<(VecPI32 (sext_invec HWI16:$Vss)), (VSxth (LoVec $Vss))>;
  def: Pat<(VecPI32 (sext_invec HWI8:$Vss)),
           (VSxth (LoVec (VSxtb (LoVec $Vss))))>;

  def: Pat<(VecI16 (zext_invec HVI8:$Vs)),  (LoVec (VZxtb $Vs))>;
  def: Pat<(VecI32 (zext_invec HVI16:$Vs)), (LoVec (VZxth $Vs))>;
  def: Pat<(VecI32 (zext_invec HVI8:$Vs)),
           (LoVec (VZxth (LoVec (VZxtb $Vs))))>;
  def: Pat<(VecPI16 (zext_invec HWI8:$Vss)),  (VZxtb (LoVec $Vss))>;
  def: Pat<(VecPI32 (zext_invec HWI16:$Vss)), (VZxth (LoVec $Vss))>;
  def: Pat<(VecPI32 (zext_invec HWI8:$Vss)),
           (VZxth (LoVec (VZxtb (LoVec $Vss))))>;

  def: Pat<(VecI8 (trunc HWI16:$Vss)),
           (V6_vpackeb (HiVec $Vss), (LoVec $Vss))>;
  def: Pat<(VecI16 (trunc HWI32:$Vss)),
           (V6_vpackeh (HiVec $Vss), (LoVec $Vss))>;
  // Pattern for (v32i8 (trunc v32i32:$Vs)) after widening:
  def: Pat<(VecI8 (trunc
              (concat_vectors
                (VecI16 (trunc (concat_vectors HVI32:$Vs, undef))),
                undef))),
           (V6_vdealb4w (IMPLICIT_DEF), HvxVR:$Vs)>;

  def: Pat<(VecQ8 (trunc HVI8:$Vs)),
           (V6_vandvrt HvxVR:$Vs, (ToI32 0x01010101))>;
  def: Pat<(VecQ16 (trunc HVI16:$Vs)),
           (V6_vandvrt HvxVR:$Vs, (ToI32 0x01010101))>;
  def: Pat<(VecQ32 (trunc HVI32:$Vs)),
           (V6_vandvrt HvxVR:$Vs, (ToI32 0x01010101))>;
}

let Predicates = [UseHVX] in {
  // The "source" types are not legal, and there are no parameterized
  // definitions for them, but they are length-specific.
  let Predicates = [UseHVX,UseHVX64B] in {
    def: Pat<(VecI16 (sext_inreg HVI16:$Vs, v32i8)),
             (V6_vasrh (V6_vaslh HVI16:$Vs, (ToI32 8)), (ToI32 8))>;
    def: Pat<(VecI32 (sext_inreg HVI32:$Vs, v16i8)),
             (V6_vasrw (V6_vaslw HVI32:$Vs, (ToI32 24)), (ToI32 24))>;
    def: Pat<(VecI32 (sext_inreg HVI32:$Vs, v16i16)),
             (V6_vasrw (V6_vaslw HVI32:$Vs, (ToI32 16)), (ToI32 16))>;
  }
  let Predicates = [UseHVX,UseHVX128B] in {
    def: Pat<(VecI16 (sext_inreg HVI16:$Vs, v64i8)),
             (V6_vasrh (V6_vaslh HVI16:$Vs, (ToI32 8)), (ToI32 8))>;
    def: Pat<(VecI32 (sext_inreg HVI32:$Vs, v32i8)),
             (V6_vasrw (V6_vaslw HVI32:$Vs, (ToI32 24)), (ToI32 24))>;
    def: Pat<(VecI32 (sext_inreg HVI32:$Vs, v32i16)),
             (V6_vasrw (V6_vaslw HVI32:$Vs, (ToI32 16)), (ToI32 16))>;
  }

  // Take a pair of vectors Vt:Vs and shift them towards LSB by (Rt & HwLen).
  def: Pat<(VecI8 (valign HVI8:$Vt, HVI8:$Vs, I32:$Rt)),
           (LoVec (V6_valignb HvxVR:$Vt, HvxVR:$Vs, I32:$Rt))>;
  def: Pat<(VecI16 (valign HVI16:$Vt, HVI16:$Vs, I32:$Rt)),
           (LoVec (V6_valignb HvxVR:$Vt, HvxVR:$Vs, I32:$Rt))>;
  def: Pat<(VecI32 (valign HVI32:$Vt, HVI32:$Vs, I32:$Rt)),
           (LoVec (V6_valignb HvxVR:$Vt, HvxVR:$Vs, I32:$Rt))>;

  def: Pat<(HexagonVASL HVI8:$Vs, I32:$Rt),
           (V6_vshuffeb (V6_vaslh (HiVec (V6_vzb HvxVR:$Vs)), I32:$Rt),
                        (V6_vaslh (LoVec (V6_vzb HvxVR:$Vs)), I32:$Rt))>;
  def: Pat<(HexagonVASR HVI8:$Vs, I32:$Rt),
           (V6_vshuffeb (V6_vasrh (HiVec (V6_vsb HvxVR:$Vs)), I32:$Rt),
                        (V6_vasrh (LoVec (V6_vsb HvxVR:$Vs)), I32:$Rt))>;
  def: Pat<(HexagonVLSR HVI8:$Vs, I32:$Rt),
           (V6_vshuffeb (V6_vlsrh (HiVec (V6_vzb HvxVR:$Vs)), I32:$Rt),
                        (V6_vlsrh (LoVec (V6_vzb HvxVR:$Vs)), I32:$Rt))>;

  def: Pat<(HexagonVASL HVI16:$Vs, I32:$Rt), (V6_vaslh HvxVR:$Vs, I32:$Rt)>;
  def: Pat<(HexagonVASL HVI32:$Vs, I32:$Rt), (V6_vaslw HvxVR:$Vs, I32:$Rt)>;
  def: Pat<(HexagonVASR HVI16:$Vs, I32:$Rt), (V6_vasrh HvxVR:$Vs, I32:$Rt)>;
  def: Pat<(HexagonVASR HVI32:$Vs, I32:$Rt), (V6_vasrw HvxVR:$Vs, I32:$Rt)>;
  def: Pat<(HexagonVLSR HVI16:$Vs, I32:$Rt), (V6_vlsrh HvxVR:$Vs, I32:$Rt)>;
  def: Pat<(HexagonVLSR HVI32:$Vs, I32:$Rt), (V6_vlsrw HvxVR:$Vs, I32:$Rt)>;

  def: Pat<(add HVI32:$Vx, (HexagonVASL HVI32:$Vu, I32:$Rt)),
           (V6_vaslw_acc HvxVR:$Vx, HvxVR:$Vu, I32:$Rt)>;
  def: Pat<(add HVI32:$Vx, (HexagonVASR HVI32:$Vu, I32:$Rt)),
           (V6_vasrw_acc HvxVR:$Vx, HvxVR:$Vu, I32:$Rt)>;

  def: Pat<(shl HVI8:$Vs, HVI8:$Vt),
           (V6_vshuffeb (V6_vaslhv (HiVec (V6_vzb $Vs)), (HiVec (V6_vzb $Vt))),
                        (V6_vaslhv (LoVec (V6_vzb $Vs)), (LoVec (V6_vzb $Vt))))>;
  def: Pat<(sra HVI8:$Vs, HVI8:$Vt),
           (V6_vshuffeb (V6_vasrhv (HiVec (V6_vsb $Vs)), (HiVec (V6_vzb $Vt))),
                        (V6_vasrhv (LoVec (V6_vsb $Vs)), (LoVec (V6_vzb $Vt))))>;
  def: Pat<(srl HVI8:$Vs, HVI8:$Vt),
           (V6_vshuffeb (V6_vlsrhv (HiVec (V6_vzb $Vs)), (HiVec (V6_vzb $Vt))),
                        (V6_vlsrhv (LoVec (V6_vzb $Vs)), (LoVec (V6_vzb $Vt))))>;

  def: Pat<(shl HVI16:$Vs, HVI16:$Vt), (V6_vaslhv HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(shl HVI32:$Vs, HVI32:$Vt), (V6_vaslwv HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(sra HVI16:$Vs, HVI16:$Vt), (V6_vasrhv HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(sra HVI32:$Vs, HVI32:$Vt), (V6_vasrwv HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(srl HVI16:$Vs, HVI16:$Vt), (V6_vlsrhv HvxVR:$Vs, HvxVR:$Vt)>;
  def: Pat<(srl HVI32:$Vs, HVI32:$Vt), (V6_vlsrwv HvxVR:$Vs, HvxVR:$Vt)>;

  // Mfshl hi, lo, amt
  def: Pat<(Mfshl HVI8:$Vu, HVI8:$Vv, HVI8:$Vs),
           (V6_vshuffob (V6_vaslhv (HiVec (V6_vshufoeb $Vu, $Vv)),
                                   (HiVec (V6_vzb $Vs))),
                        (V6_vaslhv (LoVec (V6_vshufoeb $Vu, $Vv)),
                                   (LoVec (V6_vzb $Vs))))>;
  let Predicates = [UseHVX,UseHVXV60] in {
    // V60 doesn't produce 0 on shifts by bitwidth, e.g. Vv.h << 16-0
    def: Pat<(Mfshl HVI16:$Vu, HVI16:$Vv, HVI16:$Vs),
             (V6_vmux (V6_veqh $Vs, (V6_vd0)),
                      $Vu,
                      (V6_vor (V6_vaslhv $Vu, $Vs),
                              (V6_vlsrhv $Vv, (VSubih 16, $Vs))))>;
    def: Pat<(Mfshl HVI32:$Vu, HVI32:$Vv, HVI32:$Vs),
             (V6_vmux (V6_veqw (V6_vand $Vs, (PS_vsplatiw (i32 31))), (V6_vd0)),
                      $Vu,
                      (V6_vor (V6_vaslwv $Vu, $Vs),
                              (V6_vlsrwv $Vv, (VSubiw 32, $Vs))))>;
  }
  let Predicates = [UseHVX,UseHVXV62], AddedComplexity = 10 in {
    // Do it as (Vu << Vs) | (Vv >> (BW-Vs)).
    // For Vs == 0 becomes Vu | (Vv >> -BW), since the shift amount is
    // sign-extended. Then this becomes Vu | (Vv << BW) == Vu.
    def: Pat<(Mfshl HVI16:$Vu, HVI16:$Vv, HVI16:$Vs),
             (V6_vor (V6_vaslhv $Vu, $Vs),
                     (V6_vlsrhv $Vv, (VSubih 16, $Vs)))>;
    def: Pat<(Mfshl HVI32:$Vu, HVI32:$Vv, HVI32:$Vs),
             (V6_vor (V6_vaslwv $Vu, $Vs),
                     (V6_vlsrwv $Vv, (VSubiw 32, $Vs)))>;
  }
  let Predicates = [UseHVX,UseHVXV66], AddedComplexity = 20 in {
    // Assume Vs > 0 (and within bit width)
    // Vx[1]:Vx[0] = V6_vasr_into Vx[0], Vv, Vs
    //   -->  (Vx[0]:Vx[0] & (ffffffff << -Vs)) | (Vv:00000000 << -Vs)
    // i.e. Vx[1] = insert ((Vv << -Vs) -> Vx[0])
    def: Pat<(Mfshl HVI32:$Vu, HVI32:$Vv, HVI32:$Vs),
             (HiVec (V6_vasr_into (Combinev (VecI32 (IMPLICIT_DEF)),
                                            (V6_vlsrwv $Vv, (VSubiw 32, $Vs))),
                                  $Vu,
                                  (V6_vsubw (V6_vd0), $Vs)))>;
  }

  // Mfshr hi, lo, amt
  def: Pat<(Mfshr HVI8:$Vu, HVI8:$Vv, HVI8:$Vs),
           (V6_vshuffeb (V6_vlsrhv (HiVec (V6_vshufoeb $Vu, $Vv)),
                                   (HiVec (V6_vzb $Vs))),
                        (V6_vlsrhv (LoVec (V6_vshufoeb $Vu, $Vv)),
                                   (LoVec (V6_vzb $Vs))))>;
  let Predicates = [UseHVX,UseHVXV60] in {
    def: Pat<(Mfshr HVI16:$Vu, HVI16:$Vv, HVI16:$Vs),
             (V6_vmux (V6_veqh $Vs, (V6_vd0)),
                      $Vv,
                      (V6_vor (V6_vaslhv $Vu, (VSubih 16, $Vs)),
                              (V6_vlsrhv $Vv, $Vs)))>;
    def: Pat<(Mfshr HVI32:$Vu, HVI32:$Vv, HVI32:$Vs),
             (V6_vmux (V6_veqw $Vs, (V6_vd0)),
                      $Vv,
                      (V6_vor (V6_vaslwv $Vu, (VSubiw 32, $Vs)),
                              (V6_vlsrwv $Vv, $Vs)))>;
  }
  let Predicates = [UseHVX,UseHVXV62], AddedComplexity = 10 in {
    // Do it as (Vu >> -(BW-Vs)) | (Vv >> Vs).
    // For Vs == 0 becomes (Vu << BW) | Vs == 0 | Vv
    def: Pat<(Mfshr HVI16:$Vu, HVI16:$Vv, HVI16:$Vs),
             (V6_vor (V6_vlsrhv $Vu, (V6_vsubh $Vs, (PS_vsplatih (i32 16)))),
                     (V6_vlsrhv $Vv, $Vs))>;
    def: Pat<(Mfshr HVI32:$Vu, HVI32:$Vv, HVI32:$Vs),
             (V6_vor (V6_vlsrwv $Vu, (V6_vsubw $Vs, (PS_vsplatiw (i32 32)))),
                     (V6_vlsrwv $Vv, $Vs))>;
  }
  let Predicates = [UseHVX,UseHVXV66], AddedComplexity = 20 in {
    // Assume Vs > 0 (and within bit width)
    // Vx[1]:Vx[0] = V6_vasr_into Vx[0], Vv, Vs
    //   -->  (Vx[0]:Vx[0] & (ffffffff >> Vs)) | (Vv:00000000 >> Vs)
    // i.e. Vx[0] = insert ((Vv >> Vs) -> Vx[0])
    def: Pat<(Mfshr HVI32:$Vu, HVI32:$Vv, HVI32:$Vs),
             (LoVec (V6_vasr_into (Combinev (VecI32 (IMPLICIT_DEF)),
                                            (V6_vlsrwv $Vv, $Vs)),
                                  $Vu,
                                  $Vs))>;
  }

  def: Pat<(VecI16 (bswap HVI16:$Vs)),
           (V6_vdelta HvxVR:$Vs, (PS_vsplatib (i32 0x01)))>;
  def: Pat<(VecI32 (bswap HVI32:$Vs)),
           (V6_vdelta HvxVR:$Vs, (PS_vsplatib (i32 0x03)))>;

  def: Pat<(VecI8 (ctpop HVI8:$Vs)),
           (V6_vshuffeb (V6_vpopcounth (HiVec (V6_vzb HvxVR:$Vs))),
                        (V6_vpopcounth (LoVec (V6_vzb HvxVR:$Vs))))>;
  def: Pat<(VecI16 (ctpop HVI16:$Vs)), (V6_vpopcounth HvxVR:$Vs)>;
  def: Pat<(VecI32 (ctpop HVI32:$Vs)),
           (V6_vaddw (LoVec (V6_vzh (V6_vpopcounth HvxVR:$Vs))),
                     (HiVec (V6_vzh (V6_vpopcounth HvxVR:$Vs))))>;

  def: Pat<(VecI8 (ctlz HVI8:$Vs)),
           (V6_vsubb (V6_vshuffeb (V6_vcl0h (HiVec (V6_vzb HvxVR:$Vs))),
                                  (V6_vcl0h (LoVec (V6_vzb HvxVR:$Vs)))),
                     (PS_vsplatib (i32 0x08)))>;

  def: Pat<(VecI16 (ctlz HVI16:$Vs)), (V6_vcl0h HvxVR:$Vs)>;
  def: Pat<(VecI32 (ctlz HVI32:$Vs)), (V6_vcl0w HvxVR:$Vs)>;
}

class HvxSel_pat<InstHexagon MI, PatFrag RegPred>
  : Pat<(select I1:$Pu, RegPred:$Vs, RegPred:$Vt),
        (MI I1:$Pu, RegPred:$Vs, RegPred:$Vt)>;

let Predicates = [UseHVX] in {
  def: HvxSel_pat<PS_vselect, HVI8>;
  def: HvxSel_pat<PS_vselect, HVI16>;
  def: HvxSel_pat<PS_vselect, HVI32>;
  def: HvxSel_pat<PS_wselect, HWI8>;
  def: HvxSel_pat<PS_wselect, HWI16>;
  def: HvxSel_pat<PS_wselect, HWI32>;
}

def V2Q: OutPatFrag<(ops node:$Vs), (V6_vandvrt $Vs, (ToI32 -1))>;

let Predicates = [UseHVX] in {
  def: Pat<(select I1:$Pu, VecQ8:$Qs, VecQ8:$Qt),
           (V2Q (PS_vselect $Pu, (Q2V $Qs), (Q2V $Qt)))>;
  def: Pat<(select I1:$Pu, VecQ16:$Qs, VecQ16:$Qt),
           (V2Q (PS_vselect $Pu, (Q2V $Qs), (Q2V $Qt)))>;
  def: Pat<(select I1:$Pu, VecQ32:$Qs, VecQ32:$Qt),
           (V2Q (PS_vselect $Pu, (Q2V $Qs), (Q2V $Qt)))>;
}

let Predicates = [UseHVX] in {
  def: Pat<(VecQ8   (qtrue)), (PS_qtrue)>;
  def: Pat<(VecQ16  (qtrue)), (PS_qtrue)>;
  def: Pat<(VecQ32  (qtrue)), (PS_qtrue)>;
  def: Pat<(VecQ8  (qfalse)), (PS_qfalse)>;
  def: Pat<(VecQ16 (qfalse)), (PS_qfalse)>;
  def: Pat<(VecQ32 (qfalse)), (PS_qfalse)>;

  def: Pat<(vnot  HQ8:$Qs), (V6_pred_not HvxQR:$Qs)>;
  def: Pat<(vnot HQ16:$Qs), (V6_pred_not HvxQR:$Qs)>;
  def: Pat<(vnot HQ32:$Qs), (V6_pred_not HvxQR:$Qs)>;
  def: Pat<(qnot  HQ8:$Qs), (V6_pred_not HvxQR:$Qs)>;
  def: Pat<(qnot HQ16:$Qs), (V6_pred_not HvxQR:$Qs)>;
  def: Pat<(qnot HQ32:$Qs), (V6_pred_not HvxQR:$Qs)>;

  def: OpR_RR_pat<V6_pred_and,  And,  VecQ8,   HQ8>;
  def: OpR_RR_pat<V6_pred_and,  And, VecQ16,  HQ16>;
  def: OpR_RR_pat<V6_pred_and,  And, VecQ32,  HQ32>;
  def: OpR_RR_pat<V6_pred_or,    Or,  VecQ8,   HQ8>;
  def: OpR_RR_pat<V6_pred_or,    Or, VecQ16,  HQ16>;
  def: OpR_RR_pat<V6_pred_or,    Or, VecQ32,  HQ32>;
  def: OpR_RR_pat<V6_pred_xor,  Xor,  VecQ8,   HQ8>;
  def: OpR_RR_pat<V6_pred_xor,  Xor, VecQ16,  HQ16>;
  def: OpR_RR_pat<V6_pred_xor,  Xor, VecQ32,  HQ32>;

  def: OpR_RR_pat<V6_pred_and_n,  VNot2<And, qnot>,  VecQ8,   HQ8>;
  def: OpR_RR_pat<V6_pred_and_n,  VNot2<And, qnot>, VecQ16,  HQ16>;
  def: OpR_RR_pat<V6_pred_and_n,  VNot2<And, qnot>, VecQ32,  HQ32>;
  def: OpR_RR_pat<V6_pred_or_n,    VNot2<Or, qnot>,  VecQ8,   HQ8>;
  def: OpR_RR_pat<V6_pred_or_n,    VNot2<Or, qnot>, VecQ16,  HQ16>;
  def: OpR_RR_pat<V6_pred_or_n,    VNot2<Or, qnot>, VecQ32,  HQ32>;

  def: OpR_RR_pat<V6_veqb,      seteq,  VecQ8,  HVI8>;
  def: OpR_RR_pat<V6_veqh,      seteq, VecQ16, HVI16>;
  def: OpR_RR_pat<V6_veqw,      seteq, VecQ32, HVI32>;
  def: OpR_RR_pat<V6_vgtb,      setgt,  VecQ8,  HVI8>;
  def: OpR_RR_pat<V6_vgth,      setgt, VecQ16, HVI16>;
  def: OpR_RR_pat<V6_vgtw,      setgt, VecQ32, HVI32>;
  def: OpR_RR_pat<V6_vgtub,    setugt,  VecQ8,  HVI8>;
  def: OpR_RR_pat<V6_vgtuh,    setugt, VecQ16, HVI16>;
  def: OpR_RR_pat<V6_vgtuw,    setugt, VecQ32, HVI32>;

  def: AccRRR_pat<V6_veqb_and,    And,  seteq,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_veqb_or,      Or,  seteq,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_veqb_xor,    Xor,  seteq,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_veqh_and,    And,  seteq,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_veqh_or,      Or,  seteq,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_veqh_xor,    Xor,  seteq,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_veqw_and,    And,  seteq,   HQ32, HVI32, HVI32>;
  def: AccRRR_pat<V6_veqw_or,      Or,  seteq,   HQ32, HVI32, HVI32>;
  def: AccRRR_pat<V6_veqw_xor,    Xor,  seteq,   HQ32, HVI32, HVI32>;

  def: AccRRR_pat<V6_vgtb_and,    And,  setgt,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_vgtb_or,      Or,  setgt,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_vgtb_xor,    Xor,  setgt,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_vgth_and,    And,  setgt,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_vgth_or,      Or,  setgt,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_vgth_xor,    Xor,  setgt,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_vgtw_and,    And,  setgt,   HQ32, HVI32, HVI32>;
  def: AccRRR_pat<V6_vgtw_or,      Or,  setgt,   HQ32, HVI32, HVI32>;
  def: AccRRR_pat<V6_vgtw_xor,    Xor,  setgt,   HQ32, HVI32, HVI32>;

  def: AccRRR_pat<V6_vgtub_and,   And, setugt,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_vgtub_or,     Or, setugt,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_vgtub_xor,   Xor, setugt,    HQ8,  HVI8,  HVI8>;
  def: AccRRR_pat<V6_vgtuh_and,   And, setugt,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_vgtuh_or,     Or, setugt,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_vgtuh_xor,   Xor, setugt,   HQ16, HVI16, HVI16>;
  def: AccRRR_pat<V6_vgtuw_and,   And, setugt,   HQ32, HVI32, HVI32>;
  def: AccRRR_pat<V6_vgtuw_or,     Or, setugt,   HQ32, HVI32, HVI32>;
  def: AccRRR_pat<V6_vgtuw_xor,   Xor, setugt,   HQ32, HVI32, HVI32>;
}

let Predicates = [UseHVXV68, UseHVXFloatingPoint] in {
  def: OpR_RR_pat<V6_veqh,              seteq,  VecQ16, HVF16>;
  def: OpR_RR_pat<V6_veqh,             setoeq,  VecQ16, HVF16>;
  def: OpR_RR_pat<V6_veqh,             setueq,  VecQ16, HVF16>;
  def: OpR_RR_pat<V6_vgthf,             setgt,  VecQ16, HVF16>;
  def: OpR_RR_pat<V6_vgthf,            setogt,  VecQ16, HVF16>;
  def: OpR_RR_pat<V6_vgthf,            setugt,  VecQ16, HVF16>;

  def: OpR_RR_pat<V6_veqw,              seteq,  VecQ32, HVF32>;
  def: OpR_RR_pat<V6_veqw,             setoeq,  VecQ32, HVF32>;
  def: OpR_RR_pat<V6_veqw,             setueq,  VecQ32, HVF32>;
  def: OpR_RR_pat<V6_vgtsf,             setgt,  VecQ32, HVF32>;
  def: OpR_RR_pat<V6_vgtsf,            setogt,  VecQ32, HVF32>;
  def: OpR_RR_pat<V6_vgtsf,            setugt,  VecQ32, HVF32>;

  def: AccRRR_pat<V6_veqh_and,    And,          seteq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_or,      Or,          seteq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_xor,    Xor,          seteq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_and,    And,         setoeq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_or,      Or,         setoeq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_xor,    Xor,         setoeq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_and,    And,         setueq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_or,      Or,         setueq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_veqh_xor,    Xor,         setueq,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_and,   And,          setgt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_or,     Or,          setgt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_xor,   Xor,          setgt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_and,   And,         setogt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_or,     Or,         setogt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_xor,   Xor,         setogt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_and,   And,         setugt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_or,     Or,         setugt,  HQ16, HVF16, HVF16>;
  def: AccRRR_pat<V6_vgthf_xor,   Xor,         setugt,  HQ16, HVF16, HVF16>;

  def: AccRRR_pat<V6_veqw_and,    And,          seteq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_or,      Or,          seteq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_xor,    Xor,          seteq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_and,    And,         setoeq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_or,      Or,         setoeq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_xor,    Xor,         setoeq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_and,    And,         setueq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_or,      Or,         setueq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_veqw_xor,    Xor,         setueq,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_and,   And,          setgt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_or,     Or,          setgt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_xor,   Xor,          setgt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_and,   And,         setogt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_or,     Or,         setogt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_xor,   Xor,         setogt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_and,   And,         setugt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_or,     Or,         setugt,  HQ32, HVF32, HVF32>;
  def: AccRRR_pat<V6_vgtsf_xor,   Xor,         setugt,  HQ32, HVF32, HVF32>;

  def: Pat<(VecQ16 (setone HVF16:$Vt, HVF16:$Vu)),
           (V6_pred_not (V6_veqh HvxVR:$Vt, HvxVR:$Vu))>;

  def: Pat<(VecQ32 (setone HVF32:$Vt, HVF32:$Vu)),
           (V6_pred_not (V6_veqw HvxVR:$Vt, HvxVR:$Vu))>;
}

// Multiply high for non-i32 types
def: Pat<(VecI8  (mulhs  HVI8:$Vu,  HVI8:$Vv)),
         (V6_vshuffob (HiVec (V6_vmpybv $Vu, $Vv)),
                      (LoVec (V6_vmpybv $Vu, $Vv)))>;
def: Pat<(VecI16 (mulhs HVI16:$Vu, HVI16:$Vv)),
         (V6_vshufoh (HiVec (V6_vmpyhv $Vu, $Vv)),
                     (LoVec (V6_vmpyhv $Vu, $Vv)))>;
def: Pat<(VecI8  (mulhu  HVI8:$Vu,  HVI8:$Vv)),
         (V6_vshuffob (HiVec (V6_vmpyubv $Vu, $Vv)),
                      (LoVec (V6_vmpyubv $Vu, $Vv)))>;
def: Pat<(VecI16 (mulhu HVI16:$Vu, HVI16:$Vv)),
         (V6_vshufoh (HiVec (V6_vmpyuhv $Vu, $Vv)),
                     (LoVec (V6_vmpyuhv $Vu, $Vv)))>;
let Predicates = [UseHVXV69], AddedComplexity = 20 in {
  def: Pat<(VecI16 (mulhu HVI16:$Vu, HVI16:$Vv)),
           (V6_vmpyuhvs $Vu, $Vv)>;
}

let Predicates = [UseHVXV60] in {
  // V60 doesn't have vabsb or byte shifts.
  // Do the "mask = x >> width-1; abs = (x + mask) ^ mask" trick.
  // v31:30.h = vsxt(Inp.b)             ; generate masks in odd bytes in
  //                                    ; interleaved half-words
  // v29:28.b = vshuffoe(v31.b,v30.b)   ; collect odd/even bytes, masks = v29
  // v27.b    = vadd(Inp.b,v29.b)       ; x + masks
  // Abs      = vxor(v27,v29)           ;   ^ masks
  def: Pat<(VecI8 (abs HVI8:$Vs)),
    (V6_vxor HvxVR:$Vs,
      (V6_vaddb HvxVR:$Vs,
        (HiVec
          (V6_vshufoeb
            (HiVec (V6_vsb HvxVR:$Vs)),
            (LoVec (V6_vsb HvxVR:$Vs))))))>;
}

let Predicates = [UseHVXV62], AddedComplexity = 20 in {
  def: Pat<(VecI8 (abs HVI8:$Vs)), (V6_vabsb HvxVR:$Vs)>;
}

def: Pat<(VecI16 (abs HVI16:$Vs)), (V6_vabsh HvxVR:$Vs)>;
def: Pat<(VecI32 (abs HVI32:$Vs)), (V6_vabsw HvxVR:$Vs)>;

// If a node takes an MVT type as a parameter, the argument must be
// a name of a member of MVT.
multiclass Saturates<ValueType HvxTy_i8, ValueType HvxTy_i16> {
  def: Pat<(VecI8 (ssat HWI16:$Vss, HvxTy_i8)),
           (V6_vpackhb_sat (HiVec $Vss), (LoVec $Vss))>;
  def: Pat<(VecI8 (ssat (concat_vectors HWI32:$Vss, HWI32:$Vtt), HvxTy_i8)),
           (V6_vpackhb_sat (V6_vpackwh_sat (HiVec $Vtt), (LoVec $Vtt)),
                           (V6_vpackwh_sat (HiVec $Vss), (LoVec $Vss)))>;
  def: Pat<(VecI16 (ssat HWI32:$Vss, HvxTy_i16)),
           (V6_vpackwh_sat (HiVec $Vss), (LoVec $Vss))>;

  def: Pat<(VecI8 (usat HWI16:$Vss, HvxTy_i8)),
           (V6_vpackhub_sat (HiVec $Vss), (LoVec $Vss))>;
  def: Pat<(VecI8 (usat (concat_vectors HWI32:$Vss, HWI32:$Vtt), HvxTy_i8)),
           (V6_vpackhub_sat (V6_vpackwuh_sat (HiVec $Vtt), (LoVec $Vtt)),
                            (V6_vpackwuh_sat (HiVec $Vss), (LoVec $Vss)))>;
  def: Pat<(VecI16 (usat HWI32:$Vss, HvxTy_i16)),
           (V6_vpackwuh_sat (HiVec $Vss), (LoVec $Vss))>;
}
let Predicates = [UseHVX64B] in {
  defm: Saturates<v64i8, v32i16>;
}
let Predicates = [UseHVX128B] in {
  defm: Saturates<v128i8, v64i16>;
}
