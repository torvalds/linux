//===----------------------------------------------------------------------===//
// Vector Instructions
//===----------------------------------------------------------------------===//

// Pseudo instructions for VM/VM512 spill/restore
//
// These pseudo instructions are used for only spill/restore since
// InlineSpiller assumes storeRegToStackSlot/loadRegFromStackSlot
// functions emit only single instruction.  Those functions emit a
// single store/load instruction or one of these pseudo store/load
// instructions.
//
// Specifies hasSideEffects = 0 to disable UnmodeledSideEffects.

let mayLoad = 1, hasSideEffects = 0 in {
def LDVMrii : Pseudo<
    (outs VM:$vmx), (ins MEMrii:$addr),
    "# pseudo ldvm $vmx, $addr", []>;
def LDVM512rii : Pseudo<
    (outs VM512:$vmx), (ins MEMrii:$addr),
    "# pseudo ldvm512 $vmx, $addr", []>;
}
let mayStore = 1, hasSideEffects = 0 in {
def STVMrii : Pseudo<
    (outs), (ins MEMrii:$addr, VM:$vmx),
    "# pseudo stvm $addr, $vmx", []>;
def STVM512rii : Pseudo<
    (outs), (ins MEMrii:$addr, VM512:$vmx),
    "# pseudo stvm512 $addr, $vmx", []>;
}

//===----------------------------------------------------------------------===//
// Pseudo instructions for VM512 modifications
//===----------------------------------------------------------------------===//

// LVM/SVM instructions using VM512
let hasSideEffects = 0, isCodeGenOnly = 1 in {
  let Constraints = "$vx = $vd", DisableEncoding = "$vd" in {
    def LVMyir_y : Pseudo<(outs VM512:$vx), (ins uimm3:$sy, I64:$sz, VM512:$vd),
                          "# pseudo LVM $vx, $sy, $sz, $vd">;
    def LVMyim_y : Pseudo<(outs VM512:$vx),
                          (ins uimm3:$sy, mimm:$sz, VM512:$vd),
                          "# pseudo LVM $vx, $sy, $sz, $vd">;
  }
  def LVMyir : Pseudo<(outs VM512:$vx), (ins uimm3:$sy, I64:$sz),
                      "# pseudo LVM $vx, $sy, $sz">;
  def LVMyim : Pseudo<(outs VM512:$vx), (ins uimm3:$sy, mimm:$sz),
                      "# pseudo LVM $vx, $sy, $sz">;
  def SVMyi : Pseudo<(outs I64:$sx), (ins VM512:$vz, uimm3:$sy),
                     "# pseudo SVM $sx, $vz, $sy">;
}

// VFMK/VFMKW/VFMKS instructions using VM512
let hasSideEffects = 0, isCodeGenOnly = 1, DisableEncoding = "$vl" in {
  def VFMKyal : Pseudo<(outs VM512:$vmx), (ins I32:$vl),
                       "# pseudo-vfmk.at $vmx">;
  def VFMKynal : Pseudo<(outs VM512:$vmx), (ins I32:$vl),
                        "# pseudo-vfmk.af $vmx">;
  def VFMKWyvl  : Pseudo<(outs VM512:$vmx),
                         (ins CCOp:$cf, V64:$vz, I32:$vl),
                         "# pseudo-vfmk.w.$cf $vmx, $vz">;
  def VFMKWyvyl : Pseudo<(outs VM512:$vmx),
                         (ins CCOp:$cf, V64:$vz, VM512:$vm, I32:$vl),
                         "# pseudo-vfmk.w.$cf $vmx, $vz, $vm">;
  def VFMKSyvl  : Pseudo<(outs VM512:$vmx),
                         (ins CCOp:$cf, V64:$vz, I32:$vl),
                         "# pseudo-vfmk.s.$cf $vmx, $vz">;
  def VFMKSyvyl : Pseudo<(outs VM512:$vmx),
                         (ins CCOp:$cf, V64:$vz, VM512:$vm, I32:$vl),
                         "# pseudo-vfmk.s.$cf $vmx, $vz, $vm">;
}

// ANDM/ORM/XORM/EQVM/NNDM/NEGM instructions using VM512
let hasSideEffects = 0, isCodeGenOnly = 1 in {
  def ANDMyy : Pseudo<(outs VM512:$vmx), (ins VM512:$vmy, VM512:$vmz),
                      "# andm $vmx, $vmy, $vmz">;
  def ORMyy : Pseudo<(outs VM512:$vmx), (ins VM512:$vmy, VM512:$vmz),
                     "# orm $vmx, $vmy, $vmz">;
  def XORMyy : Pseudo<(outs VM512:$vmx), (ins VM512:$vmy, VM512:$vmz),
                      "# xorm $vmx, $vmy, $vmz">;
  def EQVMyy : Pseudo<(outs VM512:$vmx), (ins VM512:$vmy, VM512:$vmz),
                      "# eqvm $vmx, $vmy, $vmz">;
  def NNDMyy : Pseudo<(outs VM512:$vmx), (ins VM512:$vmy, VM512:$vmz),
                      "# nndm $vmx, $vmy, $vmz">;
  def NEGMy : Pseudo<(outs VM512:$vmx), (ins VM512:$vmy),
                     "# negm $vmx, $vmy">;
}

//===----------------------------------------------------------------------===//
// Instructions
//
// Define all vector instructions defined in SX-Aurora TSUBASA Architecture
// Guide here.  As those mnemonics, we use mnemonics defined in Vector Engine
// Assembly Language Reference Manual.
//
// Some instructions can update existing data by following instructions
// sequence.
//
//   lea %s0, 256
//   lea %s1, 128
//   lvl %s0
//   vbrd %v0, 2 # v0 = { 2, 2, 2, ..., 2, 2, 2 }
//   lvl %s1
//   vbrd %v0, 3 # v0 = { 3, 3, 3, ..., 3, 2, 2, 2, ..., 2, 2, 2 }
//
// In order to represent above with a virtual register, we defines instructions
// with an additional base register and `_v` suffiex in mnemonic.
//
//   lea t0, 256
//   lea t1, 128
//   lea t0
//   vbrd tv0, 2
//   lvl t1
//   vbrd_v tv1, 2, tv0
//
// We also have some instructions uses VL register with an pseudo VL value
// with following suffixes in mnemonic.
//
//   l: have an additional I32 register to represent the VL value.
//   L: have an additional VL register to represent the VL value.
//===----------------------------------------------------------------------===//

//-----------------------------------------------------------------------------
// Section 8.9 - Vector Load/Store and Move Instructions
//-----------------------------------------------------------------------------

// Multiclass for VLD instructions
let mayLoad = 1, hasSideEffects = 0, Uses = [VL] in
multiclass VLDbm<string opcStr, bits<8>opc, RegisterClass RC, dag dag_in,
                 string disEnc = ""> {
  let DisableEncoding = disEnc in
  def "" : RVM<opc, (outs RC:$vx), dag_in,
               !strconcat(opcStr, " $vx, $sy, $sz")>;
  let Constraints = "$vx = $base", DisableEncoding = disEnc#"$base",
      isCodeGenOnly = 1 in
  def _v : RVM<opc, (outs RC:$vx), !con(dag_in, (ins RC:$base)),
               !strconcat(opcStr, " $vx, $sy, $sz")>;
}
multiclass VLDlm<string opcStr, bits<8>opc, RegisterClass RC, dag dag_in> {
  defm "" : VLDbm<opcStr, opc, RC, dag_in>;
  let isCodeGenOnly = 1, VE_VLInUse = 1 in {
    defm l : VLDbm<opcStr, opc, RC, !con(dag_in, (ins I32:$vl)), "$vl,">;
    defm L : VLDbm<opcStr, opc, RC, !con(dag_in, (ins VLS:$vl)), "$vl,">;
  }
}
let VE_VLIndex = 3 in
multiclass VLDtgm<string opcStr, bits<8>opc, RegisterClass RC> {
  defm rr : VLDlm<opcStr, opc, RC, (ins I64:$sy, I64:$sz)>;
  let cy = 0 in
  defm ir : VLDlm<opcStr, opc, RC, (ins simm7:$sy, I64:$sz)>;
  let cz = 0 in
  defm rz : VLDlm<opcStr, opc, RC, (ins I64:$sy, zero:$sz)>;
  let cy = 0, cz = 0 in
  defm iz : VLDlm<opcStr, opc, RC, (ins simm7:$sy, zero:$sz)>;
}
multiclass VLDm<string opcStr, bits<8>opc, RegisterClass RC> {
  let vc = 1 in defm "" : VLDtgm<opcStr, opc, RC>;
  let vc = 0 in defm NC : VLDtgm<opcStr#".nc", opc, RC>;
}

// Section 8.9.1 - VLD (Vector Load)
defm VLD : VLDm<"vld", 0x81, V64>;

// Section 8.9.2 - VLDU (Vector Load Upper)
defm VLDU : VLDm<"vldu", 0x82, V64>;

// Section 8.9.3 - VLDL (Vector Load Lower)
defm VLDLSX : VLDm<"vldl.sx", 0x83, V64>;
let cx = 1 in defm VLDLZX : VLDm<"vldl.zx", 0x83, V64>;

// Section 8.9.4 - VLD2D (Vector Load 2D)
defm VLD2D : VLDm<"vld2d", 0xc1, V64>;

// Section 8.9.5 - VLDU2D (Vector Load Upper 2D)
defm VLDU2D : VLDm<"vldu2d", 0xc2, V64>;

// Section 8.9.6 - VLDL2D (Vector Load Lower 2D)
defm VLDL2DSX : VLDm<"vldl2d.sx", 0xc3, V64>;
let cx = 1 in defm VLDL2DZX : VLDm<"vldl2d.zx", 0xc3, V64>;

// Multiclass for VST instructions
let mayStore = 1, hasSideEffects = 0, Uses = [VL] in
multiclass VSTbm<string opcStr, string argStr, bits<8>opc, dag dag_in> {
  def "" : RVM<opc, (outs), dag_in, !strconcat(opcStr, argStr)>;
  let DisableEncoding = "$vl", isCodeGenOnly = 1, VE_VLInUse = 1 in {
    def l : RVM<opc, (outs), !con(dag_in, (ins I32:$vl)),
                !strconcat(opcStr, argStr)>;
    def L : RVM<opc, (outs), !con(dag_in, (ins VLS:$vl)),
                !strconcat(opcStr, argStr)>;
  }
}
multiclass VSTmm<string opcStr, bits<8>opc, dag dag_in> {
  defm "" : VSTbm<opcStr, " $vx, $sy, $sz", opc, dag_in>;
  let m = ?, VE_VLWithMask = 1 in
  defm m : VSTbm<opcStr, " $vx, $sy, $sz, $m", opc, !con(dag_in, (ins VM:$m))>;
}
let VE_VLIndex = 3 in
multiclass VSTtgm<string opcStr, bits<8>opc, RegisterClass RC> {
  defm rrv : VSTmm<opcStr, opc, (ins I64:$sy, I64:$sz, RC:$vx)>;
  let cy = 0 in
  defm irv : VSTmm<opcStr, opc, (ins simm7:$sy, I64:$sz, RC:$vx)>;
  let cz = 0 in
  defm rzv : VSTmm<opcStr, opc, (ins I64:$sy, zero:$sz, RC:$vx)>;
  let cy = 0, cz = 0 in
  defm izv : VSTmm<opcStr, opc, (ins simm7:$sy, zero:$sz, RC:$vx)>;
}
multiclass VSTm<string opcStr, bits<8>opc, RegisterClass RC> {
  let vc = 1, cx = 0 in defm "" : VSTtgm<opcStr, opc, RC>;
  let vc = 0, cx = 0 in defm NC : VSTtgm<opcStr#".nc", opc, RC>;
  let vc = 1, cx = 1 in defm OT : VSTtgm<opcStr#".ot", opc, RC>;
  let vc = 0, cx = 1 in defm NCOT : VSTtgm<opcStr#".nc.ot", opc, RC>;
}

// Section 8.9.7 - VST (Vector Store)
defm VST : VSTm<"vst", 0x91, V64>;

// Section 8.9.8 - VST (Vector Store Upper)
defm VSTU : VSTm<"vstu", 0x92, V64>;

// Section 8.9.9 - VSTL (Vector Store Lower)
defm VSTL : VSTm<"vstl", 0x93, V64>;

// Section 8.9.10 - VST2D (Vector Store 2D)
defm VST2D : VSTm<"vst2d", 0xd1, V64>;

// Section 8.9.11 - VSTU2D (Vector Store Upper 2D)
defm VSTU2D : VSTm<"vstu2d", 0xd2, V64>;

// Section 8.9.12 - VSTL2D (Vector Store Lower 2D)
defm VSTL2D : VSTm<"vstl2d", 0xd3, V64>;

// Multiclass for VGT instructions
let mayLoad = 1, hasSideEffects = 0, Uses = [VL] in
multiclass VGTbm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                 dag dag_in, string disEnc = ""> {
  let DisableEncoding = disEnc in
  def "" : RVM<opc, (outs RC:$vx), dag_in,
               !strconcat(opcStr, " $vx, ", argStr)>;
  let Constraints = "$vx = $base", DisableEncoding = disEnc#"$base",
      isCodeGenOnly = 1 in
  def _v : RVM<opc, (outs RC:$vx), !con(dag_in, (ins RC:$base)),
               !strconcat(opcStr, " $vx, ", argStr)>;
}
multiclass VGTlm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                 dag dag_in> {
  defm "" : VGTbm<opcStr, argStr, opc, RC, dag_in>;
  let isCodeGenOnly = 1, VE_VLInUse = 1 in {
    defm l : VGTbm<opcStr, argStr, opc, RC, !con(dag_in, (ins I32:$vl)),
                   "$vl,">;
    defm L : VGTbm<opcStr, argStr, opc, RC, !con(dag_in, (ins VLS:$vl)),
                   "$vl,">;
  }
}
multiclass VGTmm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                 dag dag_in> {
  defm "" : VGTlm<opcStr, argStr, opc, RC, dag_in>;
  let m = ?, VE_VLWithMask = 1 in
  defm m : VGTlm<opcStr, argStr#", $m", opc, RC, !con(dag_in, (ins VM:$m))>;
}
let VE_VLIndex = 4 in
multiclass VGTlhm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                  dag dag_in> {
  defm rr : VGTmm<opcStr, argStr#", $sy, $sz", opc, RC,
                  !con(dag_in, (ins I64:$sy, I64:$sz))>;
  let cy = 0 in
  defm ir : VGTmm<opcStr, argStr#", $sy, $sz", opc, RC,
                  !con(dag_in, (ins simm7:$sy, I64:$sz))>;
  let cz = 0 in
  defm rz : VGTmm<opcStr, argStr#", $sy, $sz", opc, RC,
                  !con(dag_in, (ins I64:$sy, zero:$sz))>;
  let cy = 0, cz = 0 in
  defm iz : VGTmm<opcStr, argStr#", $sy, $sz", opc, RC,
                  !con(dag_in, (ins simm7:$sy, zero:$sz))>;
}
multiclass VGTtgm<string opcStr, bits<8>opc, RegisterClass RC> {
  let vy = ? in defm v : VGTlhm<opcStr, "$vy", opc, RC, (ins V64:$vy)>;
  let cs = 1, sw = ? in defm s : VGTlhm<opcStr, "$sw", opc, RC, (ins I64:$sw)>;
}
multiclass VGTm<string opcStr, bits<8>opc, RegisterClass RC> {
  let vc = 1 in defm "" : VGTtgm<opcStr, opc, RC>;
  let vc = 0 in defm NC : VGTtgm<opcStr#".nc", opc, RC>;
}

// Section 8.9.13 - VGT (Vector Gather)
defm VGT : VGTm<"vgt", 0xa1, V64>;

// Section 8.9.14 - VGTU (Vector Gather Upper)
defm VGTU : VGTm<"vgtu", 0xa2, V64>;

// Section 8.9.15 - VGTL (Vector Gather Lower)
defm VGTLSX : VGTm<"vgtl.sx", 0xa3, V64>;
let cx = 1 in defm VGTLZX : VGTm<"vgtl.zx", 0xa3, V64>;
def : MnemonicAlias<"vgtl", "vgtl.zx">;
def : MnemonicAlias<"vgtl.nc", "vgtl.zx.nc">;

// Multiclass for VSC instructions
let mayStore = 1, hasSideEffects = 0, Uses = [VL] in
multiclass VSCbm<string opcStr, string argStr, bits<8>opc, dag dag_in> {
  def "" : RVM<opc, (outs), dag_in, !strconcat(opcStr, argStr)>;
  let DisableEncoding = "$vl", isCodeGenOnly = 1, VE_VLInUse = 1 in {
    def l : RVM<opc, (outs), !con(dag_in, (ins I32:$vl)),
                !strconcat(opcStr, argStr)>;
    def L : RVM<opc, (outs), !con(dag_in, (ins VLS:$vl)),
                !strconcat(opcStr, argStr)>;
  }
}
multiclass VSCmm<string opcStr, string argStr, bits<8>opc, dag dag_in> {
  defm "" : VSCbm<opcStr, argStr, opc, dag_in>;
  let m = ?, VE_VLWithMask = 1 in
  defm m : VSCbm<opcStr, argStr#", $m", opc, !con(dag_in, (ins VM:$m))>;
}
let VE_VLIndex = 4 in
multiclass VSClhm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                  dag dag_in> {
  defm rrv : VSCmm<opcStr, " $vx, "#argStr#", $sy, $sz", opc,
                   !con(dag_in, (ins I64:$sy, I64:$sz, RC:$vx))>;
  let cy = 0 in
  defm irv : VSCmm<opcStr, " $vx, "#argStr#", $sy, $sz", opc,
                   !con(dag_in, (ins simm7:$sy, I64:$sz, RC:$vx))>;
  let cz = 0 in
  defm rzv : VSCmm<opcStr, " $vx, "#argStr#", $sy, $sz", opc,
                   !con(dag_in, (ins I64:$sy, zero:$sz, RC:$vx))>;
  let cy = 0, cz = 0 in
  defm izv : VSCmm<opcStr, " $vx, "#argStr#", $sy, $sz", opc,
                   !con(dag_in, (ins simm7:$sy, zero:$sz, RC:$vx))>;
}
multiclass VSCtgm<string opcStr, bits<8>opc, RegisterClass RC> {
  let vy = ? in defm v : VSClhm<opcStr, "$vy", opc, RC, (ins V64:$vy)>;
  let cs = 1, sw = ? in defm s : VSClhm<opcStr, "$sw", opc, RC, (ins I64:$sw)>;
}
multiclass VSCm<string opcStr, bits<8>opc, RegisterClass RC> {
  let vc = 1, cx = 0 in defm "" : VSCtgm<opcStr, opc, RC>;
  let vc = 0, cx = 0 in defm NC : VSCtgm<opcStr#".nc", opc, RC>;
  let vc = 1, cx = 1 in defm OT : VSCtgm<opcStr#".ot", opc, RC>;
  let vc = 0, cx = 1 in defm NCOT : VSCtgm<opcStr#".nc.ot", opc, RC>;
}

// Section 8.9.16 - VSC (Vector Scatter)
defm VSC : VSCm<"vsc", 0xb1, V64>;

// Section 8.9.17 - VSCU (Vector Scatter Upper)
defm VSCU : VSCm<"vscu", 0xb2, V64>;

// Section 8.9.18 - VSCL (Vector Scatter Lower)
defm VSCL : VSCm<"vscl", 0xb3, V64>;

// Section 8.9.19 - PFCHV (Prefetch Vector)
let Uses = [VL] in
multiclass PFCHVbm<string opcStr, string argStr, bits<8>opc, dag dag_in> {
  def "" : RVM<opc, (outs), dag_in, !strconcat(opcStr, argStr)>;
  let DisableEncoding = "$vl", isCodeGenOnly = 1, VE_VLInUse = 1 in {
    def l : RVM<opc, (outs), !con(dag_in, (ins I32:$vl)),
                !strconcat(opcStr, argStr)>;
    def L : RVM<opc, (outs), !con(dag_in, (ins VLS:$vl)),
                !strconcat(opcStr, argStr)>;
  }
}
let VE_VLIndex = 2 in
multiclass PFCHVm<string opcStr, bits<8>opc> {
  defm rr : PFCHVbm<opcStr, " $sy, $sz", opc, (ins I64:$sy, I64:$sz)>;
  let cy = 0 in
  defm ir : PFCHVbm<opcStr, " $sy, $sz", opc, (ins simm7:$sy, I64:$sz)>;
  let cz = 0 in
  defm rz : PFCHVbm<opcStr, " $sy, $sz", opc, (ins I64:$sy, zero:$sz)>;
  let cy = 0, cz = 0 in
  defm iz : PFCHVbm<opcStr, " $sy, $sz", opc, (ins simm7:$sy, zero:$sz)>;
}
let vc = 1, vx = 0 in defm PFCHV : PFCHVm<"pfchv", 0x80>;
let vc = 0, vx = 0 in defm PFCHVNC : PFCHVm<"pfchv.nc", 0x80>;

// Section 8.9.20 - LSV (Load S to V)
let sx = 0, vx = ?, hasSideEffects = 0 in
multiclass LSVbm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                 dag dag_in> {
  def "" : RR<opc, (outs RC:$vx), dag_in, !strconcat(opcStr, " ${vx}", argStr)>;
  let Constraints = "$vx = $base", DisableEncoding = "$base",
      isCodeGenOnly = 1 in
  def _v : RR<opc, (outs RC:$vx), !con(dag_in, (ins RC:$base)),
               !strconcat(opcStr, " ${vx}", argStr)>;
}
multiclass LSVm<string opcStr, bits<8>opc, RegisterClass RC> {
  defm rr : LSVbm<opcStr, "(${sy}), $sz", opc, RC, (ins I64:$sy, I64:$sz)>;
  let cy = 0 in
  defm ir : LSVbm<opcStr, "(${sy}), $sz", opc, RC, (ins uimm7:$sy, I64:$sz)>;
  let cz = 0 in
  defm rm : LSVbm<opcStr, "(${sy}), $sz", opc, RC, (ins I64:$sy, mimm:$sz)>;
  let cy = 0, cz = 0 in
  defm im : LSVbm<opcStr, "(${sy}), $sz", opc, RC, (ins uimm7:$sy, mimm:$sz)>;
}
defm LSV : LSVm<"lsv", 0x8e, V64>;

// Section 8.9.21 - LVS (Load V to S)
let cz = 0, sz = 0, vx = ?, hasSideEffects = 0 in
multiclass LVSm<string opcStr, bits<8>opc, RegisterClass RC> {
  def vr : RR<opc, (outs I64:$sx), (ins RC:$vx, I64:$sy),
              opcStr#" $sx, ${vx}(${sy})">;
  let cy = 0 in
  def vi : RR<opc, (outs I64:$sx), (ins RC:$vx, uimm7:$sy),
              opcStr#" $sx, ${vx}(${sy})">;
}
defm LVS : LVSm<"lvs", 0x9e, V64>;

// Section 8.9.22 - LVM (Load VM)
let sx = 0, vx = ?, hasSideEffects = 0 in
multiclass LVMbm<string opcStr, string argStr, bits<8>opc, RegisterClass RCM,
                 dag dag_in> {
  def "" : RR<opc, (outs RCM:$vx), dag_in,
              !strconcat(opcStr, " $vx, ", argStr)>;
  let Constraints = "$vx = $base", DisableEncoding = "$base",
      isCodeGenOnly = 1 in {
    def _m : RR<opc, (outs RCM:$vx), !con(dag_in, (ins RCM:$base)),
                !strconcat(opcStr, " $vx, ", argStr)>;
  }
}
multiclass LVMom<string opcStr, bits<8>opc, RegisterClass RCM> {
  defm rr : LVMbm<opcStr, "$sy, $sz", opc, RCM, (ins I64:$sy, I64:$sz)>;
  let cy = 0 in
  defm ir : LVMbm<opcStr, "$sy, $sz", opc, RCM, (ins uimm2:$sy, I64:$sz)>;
  let cz = 0 in
  defm rm : LVMbm<opcStr, "$sy, $sz", opc, RCM, (ins I64:$sy, mimm:$sz)>;
  let cy = 0, cz = 0 in
  defm im : LVMbm<opcStr, "$sy, $sz", opc, RCM, (ins uimm2:$sy, mimm:$sz)>;
}
multiclass LVMm<string opcStr, bits<8>opc, RegisterClass RCM> {
  defm "" : LVMom<opcStr, opc, RCM>;
}
defm LVM : LVMm<"lvm", 0xb7, VM>;

// Section 8.9.23 - SVM (Save VM)
let cz = 0, sz = 0, vz = ?, hasSideEffects = 0 in
multiclass SVMm<string opcStr, bits<8>opc, RegisterClass RCM> {
  def mr : RR<opc, (outs I64:$sx), (ins RCM:$vz, I64:$sy),
              opcStr#" $sx, $vz, $sy">;
  let cy = 0 in
  def mi : RR<opc, (outs I64:$sx), (ins RCM:$vz, uimm2:$sy),
              opcStr#" $sx, $vz, $sy">;
}
defm SVM : SVMm<"svm", 0xa7, VM>;

// Section 8.9.24 - VBRD (Vector Broadcast)
let vx = ?, hasSideEffects = 0, Uses = [VL] in
multiclass VBRDbm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                  dag dag_in, string disEnc = ""> {
  let DisableEncoding = disEnc in
  def "" : RV<opc, (outs RC:$vx), dag_in,
              !strconcat(opcStr, " $vx, ", argStr)>;
  let Constraints = "$vx = $base", DisableEncoding = disEnc#"$base",
      isCodeGenOnly = 1 in
  def _v : RV<opc, (outs RC:$vx), !con(dag_in, (ins RC:$base)),
              !strconcat(opcStr, " $vx, ", argStr)>;
}
multiclass VBRDlm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                  dag dag_in> {
  defm "" : VBRDbm<opcStr, argStr, opc, RC, dag_in>;
  let isCodeGenOnly = 1, VE_VLInUse = 1 in {
    defm l : VBRDbm<opcStr, argStr, opc, RC, !con(dag_in, (ins I32:$vl)),
                   "$vl,">;
    defm L : VBRDbm<opcStr, argStr, opc, RC, !con(dag_in, (ins VLS:$vl)),
                   "$vl,">;
  }
}
multiclass VBRDmm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                  RegisterClass RCM, dag dag_in> {
  defm "" : VBRDlm<opcStr, argStr, opc, RC, dag_in>;
  let m = ?, VE_VLWithMask = 1 in
  defm m : VBRDlm<opcStr, argStr#", $m", opc, RC, !con(dag_in, (ins RCM:$m))>;
}
let VE_VLIndex = 2 in
multiclass VBRDm<string opcStr, bits<8>opc, RegisterClass VRC, RegisterClass RC,
                 RegisterClass RCM> {
  defm r : VBRDmm<opcStr, "$sy", opc, VRC, RCM, (ins RC:$sy)>;
  let cy = 0 in
  defm i : VBRDmm<opcStr, "$sy", opc, VRC, RCM, (ins simm7:$sy)>;
}
let cx = 0, cx2 = 0 in
defm VBRD : VBRDm<"vbrd", 0x8c, V64, I64, VM>;
let cx = 0, cx2 = 1 in
defm VBRDL : VBRDm<"vbrdl", 0x8c, V64, I32, VM>;
let cx = 1, cx2 = 0 in
defm VBRDU : VBRDm<"vbrdu", 0x8c, V64, F32, VM>;
let cx = 1, cx2 = 1 in
defm PVBRD : VBRDm<"pvbrd", 0x8c, V64, I64, VM512>;

// Section 8.9.25 - VMV (Vector Move)
let vx = ?, vz = ?, hasSideEffects = 0, Uses = [VL] in
multiclass VMVbm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                 dag dag_in, string disEnc = ""> {
  let DisableEncoding = disEnc in
  def "" : RV<opc, (outs RC:$vx), dag_in,
              !strconcat(opcStr, " $vx, ", argStr)>;
  let Constraints = "$vx = $base", DisableEncoding = disEnc#"$base",
      isCodeGenOnly = 1 in
  def _v : RV<opc, (outs RC:$vx), !con(dag_in, (ins RC:$base)),
              !strconcat(opcStr, " $vx, ", argStr)>;
}
multiclass VMVlm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                 dag dag_in> {
  defm "" : VMVbm<opcStr, argStr, opc, RC, dag_in>;
  let isCodeGenOnly = 1, VE_VLInUse = 1 in {
    defm l : VMVbm<opcStr, argStr, opc, RC, !con(dag_in, (ins I32:$vl)),
                   "$vl,">;
    defm L : VMVbm<opcStr, argStr, opc, RC, !con(dag_in, (ins VLS:$vl)),
                   "$vl,">;
  }
}
multiclass VMVmm<string opcStr, bits<8>opc, RegisterClass RC,
                 RegisterClass RCM, dag dag_in> {
  defm "" : VMVlm<opcStr, "$sy, $vz", opc, RC, dag_in>;
  let m = ?, VE_VLWithMask = 1 in
  defm m : VMVlm<opcStr, "$sy, $vz, $m", opc, RC, !con(dag_in, (ins RCM:$m))>;
}
let VE_VLIndex = 3 in
multiclass VMVm<string opcStr, bits<8>opc, RegisterClass RC,
                RegisterClass RCM> {
  defm rv : VMVmm<opcStr, opc, RC, RCM, (ins I64:$sy, RC:$vz)>;
  let cy = 0 in
  defm iv : VMVmm<opcStr, opc, RC, RCM, (ins uimm7:$sy, RC:$vz)>;
}
defm VMV : VMVm<"vmv", 0x9c, V64, VM>;

//-----------------------------------------------------------------------------
// Section 8.10 - Vector Fixed-Point Arithmetic Instructions
//-----------------------------------------------------------------------------

// Multiclass for generic vector calculation
let vx = ?, hasSideEffects = 0, Uses = [VL] in
multiclass RVbm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                dag dag_in, string disEnc = ""> {
  let DisableEncoding = disEnc in
  def "" : RV<opc, (outs RC:$vx), dag_in,
              !strconcat(opcStr, " $vx", argStr)>;
  let Constraints = "$vx = $base", DisableEncoding = disEnc#"$base",
      isCodeGenOnly = 1 in
  def _v : RV<opc, (outs RC:$vx), !con(dag_in, (ins RC:$base)),
              !strconcat(opcStr, " $vx", argStr)>;
}
multiclass RVlm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                dag dag_in> {
  defm "" : RVbm<opcStr, argStr, opc, RC, dag_in>;
  let isCodeGenOnly = 1, VE_VLInUse = 1 in {
    defm l : RVbm<opcStr, argStr, opc, RC, !con(dag_in, (ins I32:$vl)),
                  "$vl,">;
    defm L : RVbm<opcStr, argStr, opc, RC, !con(dag_in, (ins VLS:$vl)),
                  "$vl,">;
  }
}
multiclass RVmm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                RegisterClass RCM, dag dag_in> {
  defm "" : RVlm<opcStr, argStr, opc, RC, dag_in>;
  let m = ?, VE_VLWithMask = 1 in
  defm m : RVlm<opcStr, argStr#", $m", opc, RC, !con(dag_in, (ins RCM:$m))>;
}
// Generic RV multiclass with 2 arguments.
//   e.g. VADD, VSUB, VMPY, and etc.
let VE_VLIndex = 3 in
multiclass RVm<string opcStr, bits<8>opc, RegisterClass VRC, RegisterClass RC,
               RegisterClass RCM, Operand SIMM = simm7> {
  let cy = 0, sy = 0, vy = ?, vz = ? in
  defm vv : RVmm<opcStr, ", $vy, $vz", opc, VRC, RCM, (ins VRC:$vy, VRC:$vz)>;
  let cs = 1, vz = ? in
  defm rv : RVmm<opcStr, ", $sy, $vz", opc, VRC, RCM, (ins RC:$sy, VRC:$vz)>;
  let cs = 1, cy = 0, vz = ? in
  defm iv : RVmm<opcStr, ", $sy, $vz", opc, VRC, RCM, (ins SIMM:$sy, VRC:$vz)>;
}
// Special RV multiclass with 2 arguments using cs2.
//   e.g. VDIV, VDVS, and VDVX.
let VE_VLIndex = 3 in
multiclass RVDIVm<string opcStr, bits<8>opc, RegisterClass VRC,
                  RegisterClass RC, RegisterClass RCM, Operand SIMM = simm7> {
  let cy = 0, sy = 0, vy = ?, vz = ? in
  defm vv : RVmm<opcStr, ", $vy, $vz", opc, VRC, RCM, (ins VRC:$vy, VRC:$vz)>;
  let cs2 = 1, vy = ? in
  defm vr : RVmm<opcStr, ", $vy, $sy", opc, VRC, RCM, (ins VRC:$vy, RC:$sy)>;
  let cs2 = 1, cy = 0, vy = ? in
  defm vi : RVmm<opcStr, ", $vy, $sy", opc, VRC, RCM, (ins VRC:$vy, SIMM:$sy)>;
  let cs = 1, vz = ? in
  defm rv : RVmm<opcStr, ", $sy, $vz", opc, VRC, RCM, (ins RC:$sy, VRC:$vz)>;
  let cs = 1, cy = 0, vz = ? in
  defm iv : RVmm<opcStr, ", $sy, $vz", opc, VRC, RCM, (ins SIMM:$sy, VRC:$vz)>;
}
// Generic RV multiclass with 2 arguments for logical operations.
//   e.g. VAND, VOR, VXOR, and etc.
let VE_VLIndex = 3 in
multiclass RVLm<string opcStr, bits<8>opc, RegisterClass ScaRC,
                RegisterClass RC, RegisterClass RCM> {
  let cy = 0, sy = 0, vy = ?, vz = ? in
  defm vv : RVmm<opcStr, ", $vy, $vz", opc, RC, RCM, (ins RC:$vy, RC:$vz)>;
  let cs = 1, vz = ? in
  defm rv : RVmm<opcStr, ", $sy, $vz", opc, RC, RCM, (ins ScaRC:$sy, RC:$vz)>;
  let cs = 1, cy = 0, vz = ? in
  defm mv : RVmm<opcStr, ", $sy, $vz", opc, RC, RCM, (ins mimm:$sy, RC:$vz)>;
}
// Generic RV multiclass with 1 argument.
//   e.g. VLDZ, VPCNT, and VBRV.
let VE_VLIndex = 2 in
multiclass RV1m<string opcStr, bits<8>opc, RegisterClass RC,
                RegisterClass RCM> {
  let cy = 0, sy = 0, vz = ? in
  defm v : RVmm<opcStr, ", $vz", opc, RC, RCM, (ins RC:$vz)>;
}
// Generic RV multiclass with no argument.
//   e.g. VSEQ.
let VE_VLIndex = 1 in
multiclass RV0m<string opcStr, bits<8>opc, RegisterClass RC,
                RegisterClass RCM> {
  let cy = 0, sy = 0 in
  defm "" : RVmm<opcStr, "", opc, RC, RCM, (ins)>;
}
// Generic RV multiclass with 2 arguments for shift operations.
//   e.g. VSLL, VSRL, VSLA, and etc.
let VE_VLIndex = 3 in
multiclass RVSm<string opcStr, bits<8>opc, RegisterClass ScaRC,
                RegisterClass RC, RegisterClass RCM> {
  let cy = 0, sy = 0, vy = ?, vz = ? in
  defm vv : RVmm<opcStr, ", $vz, $vy", opc, RC, RCM, (ins RC:$vz, RC:$vy)>;
  let cs = 1, vz = ? in
  defm vr : RVmm<opcStr, ", $vz, $sy", opc, RC, RCM, (ins RC:$vz, ScaRC:$sy)>;
  let cs = 1, cy = 0, vz = ? in
  defm vi : RVmm<opcStr, ", $vz, $sy", opc, RC, RCM, (ins RC:$vz, uimm7:$sy)>;
}
// Generic RV multiclass with 3 arguments for shift operations.
//   e.g. VSLD and VSRD.
let VE_VLIndex = 4 in
multiclass RVSDm<string opcStr, bits<8>opc, RegisterClass RC,
                 RegisterClass RCM> {
  let vy = ?, vz = ? in
  defm vvr : RVmm<opcStr, ", ($vy, ${vz}), $sy", opc, RC, RCM,
                 (ins RC:$vy, RC:$vz, I64:$sy)>;
  let cy = 0, vy = ?, vz = ? in
  defm vvi : RVmm<opcStr, ", ($vy, ${vz}), $sy", opc, RC, RCM,
                 (ins RC:$vy, RC:$vz, uimm7:$sy)>;
}
// Special RV multiclass with 3 arguments.
//   e.g. VSFA
let VE_VLIndex = 4 in
multiclass RVSAm<string opcStr, bits<8>opc, RegisterClass RC,
                 RegisterClass RCM> {
  let cz = 1, sz = ?, vz = ? in
  defm vrr : RVmm<opcStr, ", $vz, $sy, $sz", opc, RC, RCM,
                  (ins RC:$vz, I64:$sy, I64:$sz)>;
  let cz = 0, sz = ?, vz = ? in
  defm vrm : RVmm<opcStr, ", $vz, $sy, $sz", opc, RC, RCM,
                  (ins RC:$vz, I64:$sy, mimm:$sz)>;
  let cy = 0, cz = 1, sz = ?, vz = ? in
  defm vir : RVmm<opcStr, ", $vz, $sy, $sz", opc, RC, RCM,
                  (ins RC:$vz, uimm3:$sy, I64:$sz)>;
  let cy = 0, cz = 0, sz = ?, vz = ? in
  defm vim : RVmm<opcStr, ", $vz, $sy, $sz", opc, RC, RCM,
                  (ins RC:$vz, uimm3:$sy, mimm:$sz)>;
}
// Generic RV multiclass with 1 argument using vy field.
//   e.g. VFSQRT, VRCP, and VRSQRT.
let VE_VLIndex = 2 in
multiclass RVF1m<string opcStr, bits<8>opc, RegisterClass RC,
                 RegisterClass RCM> {
  let cy = 0, sy = 0, vy = ? in
  defm v : RVmm<opcStr, ", $vy", opc, RC, RCM, (ins RC:$vy)>;
}
// Special RV multiclass with 3 arguments using cs2.
//   e.g. VFMAD, VFMSB, VFNMAD, and etc.
let VE_VLIndex = 4 in
multiclass RVMm<string opcStr, bits<8>opc, RegisterClass VRC, RegisterClass RC,
                RegisterClass RCM, Operand SIMM = simm7> {
  let cy = 0, sy = 0, vy = ?, vz = ?, vw = ? in
  defm vvv : RVmm<opcStr, ", $vy, $vz, $vw", opc, VRC, RCM,
                  (ins VRC:$vy, VRC:$vz, VRC:$vw)>;
  let cs2 = 1, vy = ?, vw = ? in
  defm vrv : RVmm<opcStr, ", $vy, $sy, $vw", opc, VRC, RCM,
                  (ins VRC:$vy, RC:$sy, VRC:$vw)>;
  let cs2 = 1, cy = 0, vy = ?, vw = ? in
  defm viv : RVmm<opcStr, ", $vy, $sy, $vw", opc, VRC, RCM,
                  (ins VRC:$vy, SIMM:$sy, VRC:$vw)>;
  let cs = 1, vz = ?, vw = ? in
  defm rvv : RVmm<opcStr, ", $sy, $vz, $vw", opc, VRC, RCM,
                  (ins RC:$sy, VRC:$vz, VRC:$vw)>;
  let cs = 1, cy = 0, vz = ?, vw = ? in
  defm ivv : RVmm<opcStr, ", $sy, $vz, $vw", opc, VRC, RCM,
                  (ins SIMM:$sy, VRC:$vz, VRC:$vw)>;
}
// Special RV multiclass with 2 arguments for floating point conversions.
//   e.g. VFIX and VFIXX
let hasSideEffects = 0, VE_VLIndex = 3 in
multiclass RVFIXm<string opcStr, bits<8> opc, RegisterClass RC,
                  RegisterClass RCM> {
  let cy = 0, sy = 0, vy = ?, vz = ? in
  defm v : RVmm<opcStr#"$vz", ", $vy", opc, RC, RCM, (ins RDOp:$vz, RC:$vy)>;
}
// Multiclass for generic iterative vector calculation
let vx = ?, hasSideEffects = 0, Uses = [VL] in
multiclass RVIbm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                dag dag_in, string disEnc = ""> {
  let DisableEncoding = disEnc in
  def "" : RV<opc, (outs RC:$vx), dag_in,
              !strconcat(opcStr, " $vx", argStr)>;
  let isCodeGenOnly = 1, Constraints = "$vx = $base", DisableEncoding = disEnc#"$base" in
  def _v : RV<opc, (outs RC:$vx), !con(dag_in, (ins RC:$base)),
              !strconcat(opcStr, " $vx", argStr)>;
}
multiclass RVIlm<string opcStr, string argStr, bits<8>opc, RegisterClass RC,
                 dag dag_in> {
  defm "" : RVIbm<opcStr, argStr, opc, RC, dag_in>;
  let isCodeGenOnly = 1, VE_VLInUse = 1 in {
    defm l : RVIbm<opcStr, argStr, opc, RC, !con(dag_in, (ins I32:$vl)),
                   "$vl,">;
    defm L : RVIbm<opcStr, argStr, opc, RC, !con(dag_in, (ins VLS:$vl)),
                   "$vl,">;
  }
}
// Generic RV multiclass for iterative operation with 2 argument.
//   e.g. VFIA, VFIS, and VFIM
let VE_VLIndex = 3 in
multiclass RVI2m<string opcStr, bits<8>opc, RegisterClass VRC,
                 RegisterClass RC> {
  let vy = ? in
  defm vr : RVIlm<opcStr, ", $vy, $sy", opc, VRC, (ins VRC:$vy, RC:$sy)>;
  let cy = 0, vy = ? in
  defm vi : RVIlm<opcStr, ", $vy, $sy", opc, VRC, (ins VRC:$vy, simm7fp:$sy)>;
}
// Generic RV multiclass for iterative operation with 3 argument.
//   e.g. VFIAM, VFISM, VFIMA, and etc.
let VE_VLIndex = 4 in
multiclass RVI3m<string opcStr, bits<8>opc, RegisterClass VRC,
                 RegisterClass RC> {
  let vy = ?, vz = ? in
  defm vvr : RVIlm<opcStr, ", $vy, $vz, $sy", opc, VRC,
                   (ins VRC:$vy, VRC:$vz, RC:$sy)>;
  let cy = 0, vy = ?, vz = ? in
  defm vvi : RVIlm<opcStr, ", $vy, $vz, $sy", opc, VRC,
                   (ins VRC:$vy, VRC:$vz, simm7fp:$sy)>;
}
// special RV multiclass with 3 arguments for VSHF.
//   e.g. VSHF
let vy = ?, vz = ?, VE_VLIndex = 4 in
multiclass RVSHFm<string opcStr, bits<8>opc, RegisterClass RC,
                  Operand SIMM = uimm4> {
  defm vvr : RVlm<opcStr, ", $vy, $vz, $sy", opc, RC,
                  (ins RC:$vy, RC:$vz, I64:$sy)>;
  let cy = 0 in defm vvi : RVlm<opcStr, ", $vy, $vz, $sy", opc, RC,
                                (ins RC:$vy, RC:$vz, SIMM:$sy)>;
}
// Multiclass for generic mask calculation
let vx = ?, hasSideEffects = 0, Uses = [VL] in
multiclass RVMKbm<string opcStr, string argStr, bits<8>opc, dag dag_out,
                  dag dag_in> {
  def "" : RV<opc, dag_out, dag_in, !strconcat(opcStr, argStr)>;
  let DisableEncoding = "$vl", isCodeGenOnly = 1, VE_VLInUse = 1 in {
    def l : RV<opc, dag_out, !con(dag_in, (ins I32:$vl)),
               !strconcat(opcStr, argStr)>;
    def L : RV<opc, dag_out, !con(dag_in, (ins VLS:$vl)),
               !strconcat(opcStr, argStr)>;
  }
}
multiclass RVMKlm<string opcStr, string argStr, bits<8>opc, RegisterClass RCM,
                  dag dag_in> {
  defm "" : RVMKbm<opcStr, " $vx"#argStr, opc, (outs RCM:$vx), dag_in>;
  let m = ?, VE_VLWithMask = 1 in
  defm m : RVMKbm<opcStr, " $vx"#argStr#", $m", opc, (outs RCM:$vx),
                  !con(dag_in, (ins RCM:$m))>;
}
// Generic RV multiclass for mask calculation with a condition.
//   e.g. VFMK, VFMS, and VFMF
let cy = 0, sy = 0 in
multiclass RVMKom<string opcStr, bits<8> opc, RegisterClass RC,
                 RegisterClass RCM> {
  let vy = ?, vz = ?, VE_VLIndex = 3 in
  defm v : RVMKlm<opcStr#"$vy", ", $vz", opc, RCM, (ins CCOp:$vy, RC:$vz)>;
  let vy = 15 /* AT */, VE_VLIndex = 1 in
  defm a : RVMKlm<opcStr#"at", "", opc, RCM, (ins)>;
  let vy = 0 /* AF */, VE_VLIndex = 1 in
  defm na : RVMKlm<opcStr#"af", "", opc, RCM, (ins)>;
}
multiclass RVMKm<string opcStr, bits<8> opc, RegisterClass RC,
                 RegisterClass RCM> {
  defm "" : RVMKom<opcStr, opc, RC, RCM>;
}
// Generic RV multiclass for mask calculation with 2 arguments.
//   e.g. ANDM, ORM, XORM, and etc.
let cy = 0, sy = 0, vx = ?, vy = ?, vz = ?, hasSideEffects = 0 in
multiclass RVM2m<string opcStr, bits<8> opc, RegisterClass RCM> {
  def mm : RV<opc, (outs RCM:$vx), (ins RCM:$vy, RCM:$vz),
              !strconcat(opcStr, " $vx, $vy, $vz")>;
}
// Generic RV multiclass for mask calculation with 1 argument.
//   e.g. NEGM
let cy = 0, sy = 0, vx = ?, vy = ?, hasSideEffects = 0 in
multiclass RVM1m<string opcStr, bits<8> opc, RegisterClass RCM> {
  def m : RV<opc, (outs RCM:$vx), (ins RCM:$vy),
             !strconcat(opcStr, " $vx, $vy")>;
}
// Generic RV multiclass for mask calculation with 1 argument.
//   e.g. PCVM, LZVM, and TOVM
let cy = 0, sy = 0, vy = ?, hasSideEffects = 0, Uses = [VL] in
multiclass RVMSbm<string opcStr, string argStr, bits<8>opc, dag dag_in> {
  def "" : RV<opc, (outs I64:$sx), dag_in,
              !strconcat(opcStr, " $sx,", argStr)> {
    bits<7> sx;
    let Inst{54-48} = sx;
  }
  let DisableEncoding = "$vl", isCodeGenOnly = 1, VE_VLInUse = 1 in {
    def l : RV<opc, (outs I64:$sx), !con(dag_in, (ins I32:$vl)),
               !strconcat(opcStr, " $sx,", argStr)> {
      bits<7> sx;
      let Inst{54-48} = sx;
    }
    def L : RV<opc, (outs I64:$sx), !con(dag_in, (ins VLS:$vl)),
               !strconcat(opcStr, " $sx,", argStr)> {
      bits<7> sx;
      let Inst{54-48} = sx;
    }
  }
}
let VE_VLIndex = 2 in
multiclass RVMSm<string opcStr, bits<8> opc, RegisterClass RCM> {
  defm m : RVMSbm<opcStr, " $vy", opc, (ins RCM:$vy)>;
}

// Section 8.10.1 - VADD (Vector Add)
let cx = 0, cx2 = 0 in
defm VADDUL : RVm<"vaddu.l", 0xc8, V64, I64, VM>;
let cx = 0, cx2 = 1 in {
  defm PVADDULO : RVm<"pvaddu.lo", 0xc8, V64, I32, VM>;
  let isCodeGenOnly = 1 in
  defm VADDUW : RVm<"vaddu.w", 0xc8, V64, I32, VM>;
}
let cx = 1, cx2 = 0 in
defm PVADDUUP : RVm<"pvaddu.up", 0xc8, V64, I64, VM>;
let cx = 1, cx2 = 1 in
defm PVADDU : RVm<"pvaddu", 0xc8, V64, I64, VM512>;
def : MnemonicAlias<"vaddu.w", "pvaddu.lo">;

// Section 8.10.2 - VADS (Vector Add Single)
let cx = 0, cx2 = 0 in
defm VADDSWSX : RVm<"vadds.w.sx", 0xca, V64, I32, VM>;
let cx = 0, cx2 = 1 in {
  defm PVADDSLO : RVm<"pvadds.lo", 0xca, V64, I32, VM>;
  let isCodeGenOnly = 1 in
  defm VADDSWZX : RVm<"vadds.w.zx", 0xca, V64, I32, VM>;
}
let cx = 1, cx2 = 0 in
defm PVADDSUP : RVm<"pvadds.up", 0xca, V64, I64, VM>;
let cx = 1, cx2 = 1 in
defm PVADDS : RVm<"pvadds", 0xca, V64, I64, VM512>;
def : MnemonicAlias<"pvadds.lo.sx", "vadds.w.sx">;
def : MnemonicAlias<"vadds.w.zx", "pvadds.lo">;
def : MnemonicAlias<"vadds.w", "pvadds.lo">;
def : MnemonicAlias<"pvadds.lo.zx", "pvadds.lo">;

// Section 8.10.3 - VADX (Vector Add)
defm VADDSL : RVm<"vadds.l", 0x8b, V64, I64, VM>;

// Section 8.10.4 - VSUB (Vector Subtract)
let cx = 0, cx2 = 0 in
defm VSUBUL : RVm<"vsubu.l", 0xd8, V64, I64, VM>;
let cx = 0, cx2 = 1 in {
  defm PVSUBULO : RVm<"pvsubu.lo", 0xd8, V64, I32, VM>;
  let isCodeGenOnly = 1 in
  defm VSUBUW : RVm<"vsubu.w", 0xd8, V64, I32, VM>;
}
let cx = 1, cx2 = 0 in
defm PVSUBUUP : RVm<"pvsubu.up", 0xd8, V64, I64, VM>;
let cx = 1, cx2 = 1 in
defm PVSUBU : RVm<"pvsubu", 0xd8, V64, I64, VM512>;
def : MnemonicAlias<"vsubu.w", "pvsubu.lo">;

// Section 8.10.5 - VSBS (Vector Subtract Single)
let cx = 0, cx2 = 0 in
defm VSUBSWSX : RVm<"vsubs.w.sx", 0xda, V64, I32, VM>;
let cx = 0, cx2 = 1 in {
  defm PVSUBSLO : RVm<"pvsubs.lo", 0xda, V64, I32, VM>;
  let isCodeGenOnly = 1 in
  defm VSUBSWZX : RVm<"vsubs.w.zx", 0xda, V64, I32, VM>;
}
let cx = 1, cx2 = 0 in
defm PVSUBSUP : RVm<"pvsubs.up", 0xda, V64, I64, VM>;
let cx = 1, cx2 = 1 in
defm PVSUBS : RVm<"pvsubs", 0xda, V64, I64, VM512>;
def : MnemonicAlias<"pvsubs.lo.sx", "vsubs.w.sx">;
def : MnemonicAlias<"vsubs.w.zx", "pvsubs.lo">;
def : MnemonicAlias<"vsubs.w", "pvsubs.lo">;
def : MnemonicAlias<"pvsubs.lo.zx", "pvsubs.lo">;

// Section 8.10.6 - VSBX (Vector Subtract)
defm VSUBSL : RVm<"vsubs.l", 0x9b, V64, I64, VM>;

// Section 8.10.7 - VMPY (Vector Multiply)
let cx2 = 0 in
defm VMULUL : RVm<"vmulu.l", 0xc9, V64, I64, VM>;
let cx2 = 1 in
defm VMULUW : RVm<"vmulu.w", 0xc9, V64, I32, VM>;

// Section 8.10.8 - VMPS (Vector Multiply Single)
let cx2 = 0 in
defm VMULSWSX : RVm<"vmuls.w.sx", 0xcb, V64, I32, VM>;
let cx2 = 1 in
defm VMULSWZX : RVm<"vmuls.w.zx", 0xcb, V64, I32, VM>;
def : MnemonicAlias<"vmuls.w", "vmuls.w.zx">;

// Section 8.10.9 - VMPX (Vector Multiply)
defm VMULSL : RVm<"vmuls.l", 0xdb, V64, I64, VM>;

// Section 8.10.10 - VMPD (Vector Multiply)
defm VMULSLW : RVm<"vmuls.l.w", 0xd9, V64, I32, VM>;

// Section 8.10.11 - VDIV (Vector Divide)
let cx2 = 0 in
defm VDIVUL : RVDIVm<"vdivu.l", 0xe9, V64, I64, VM>;
let cx2 = 1 in
defm VDIVUW : RVDIVm<"vdivu.w", 0xe9, V64, I32, VM>;

// Section 8.10.12 - VDVS (Vector Divide Single)
let cx2 = 0 in
defm VDIVSWSX : RVDIVm<"vdivs.w.sx", 0xeb, V64, I32, VM>;
let cx2 = 1 in
defm VDIVSWZX : RVDIVm<"vdivs.w.zx", 0xeb, V64, I32, VM>;
def : MnemonicAlias<"vdivs.w", "vdivs.w.zx">;

// Section 8.10.13 - VDVX (Vector Divide)
defm VDIVSL : RVDIVm<"vdivs.l", 0xfb, V64, I64, VM>;

// Section 8.10.14 - VCMP (Vector Compare)
let cx = 0, cx2 = 0 in
defm VCMPUL : RVm<"vcmpu.l", 0xb9, V64, I64, VM>;
let cx = 0, cx2 = 1 in {
  defm PVCMPULO : RVm<"pvcmpu.lo", 0xb9, V64, I32, VM>;
  let isCodeGenOnly = 1 in
  defm VCMPUW : RVm<"vcmpu.w", 0xb9, V64, I32, VM>;
}
let cx = 1, cx2 = 0 in
defm PVCMPUUP : RVm<"pvcmpu.up", 0xb9, V64, I64, VM>;
let cx = 1, cx2 = 1 in
defm PVCMPU : RVm<"pvcmpu", 0xb9, V64, I64, VM512>;
def : MnemonicAlias<"vcmpu.w", "pvcmpu.lo">;

// Section 8.10.15 - VCPS (Vector Compare Single)
let cx = 0, cx2 = 0 in
defm VCMPSWSX : RVm<"vcmps.w.sx", 0xfa, V64, I32, VM>;
let cx = 0, cx2 = 1 in {
  defm PVCMPSLO : RVm<"pvcmps.lo", 0xfa, V64, I32, VM>;
  let isCodeGenOnly = 1 in
  defm VCMPSWZX : RVm<"vcmps.w.zx", 0xfa, V64, I32, VM>;
}
let cx = 1, cx2 = 0 in
defm PVCMPSUP : RVm<"pvcmps.up", 0xfa, V64, I64, VM>;
let cx = 1, cx2 = 1 in
defm PVCMPS : RVm<"pvcmps", 0xfa, V64, I64, VM512>;
def : MnemonicAlias<"pvcmps.lo.sx", "vcmps.w.sx">;
def : MnemonicAlias<"vcmps.w.zx", "pvcmps.lo">;
def : MnemonicAlias<"vcmps.w", "pvcmps.lo">;
def : MnemonicAlias<"pvcmps.lo.zx", "pvcmps.lo">;

// Section 8.10.16 - VCPX (Vector Compare)
defm VCMPSL : RVm<"vcmps.l", 0xba, V64, I64, VM>;

// Section 8.10.17 - VCMS (Vector Compare and Select Maximum/Minimum Single)
let cx = 0, cx2 = 0 in
defm VMAXSWSX : RVm<"vmaxs.w.sx", 0x8a, V64, I32, VM>;
let cx = 0, cx2 = 1 in {
  defm PVMAXSLO : RVm<"pvmaxs.lo", 0x8a, V64, I32, VM>;
  let isCodeGenOnly = 1 in
  defm VMAXSWZX : RVm<"vmaxs.w.zx", 0x8a, V64, I32, VM>;
}
let cx = 1, cx2 = 0 in
defm PVMAXSUP : RVm<"pvmaxs.up", 0x8a, V64, I64, VM>;
let cx = 1, cx2 = 1 in
defm PVMAXS : RVm<"pvmaxs", 0x8a, V64, I64, VM512>;
let cs2 = 1 in {
  let cx = 0, cx2 = 0 in
  defm VMINSWSX : RVm<"vmins.w.sx", 0x8a, V64, I32, VM>;
  let cx = 0, cx2 = 1 in {
    defm PVMINSLO : RVm<"pvmins.lo", 0x8a, V64, I32, VM>;
    let isCodeGenOnly = 1 in
    defm VMINSWZX : RVm<"vmins.w.zx", 0x8a, V64, I32, VM>;
  }
  let cx = 1, cx2 = 0 in
  defm PVMINSUP : RVm<"pvmins.up", 0x8a, V64, I64, VM>;
  let cx = 1, cx2 = 1 in
  defm PVMINS : RVm<"pvmins", 0x8a, V64, I64, VM512>;
}
def : MnemonicAlias<"pvmaxs.lo.sx", "vmaxs.w.sx">;
def : MnemonicAlias<"vmaxs.w.zx", "pvmaxs.lo">;
def : MnemonicAlias<"vmaxs.w", "pvmaxs.lo">;
def : MnemonicAlias<"pvmaxs.lo.zx", "pvmaxs.lo">;
def : MnemonicAlias<"pvmins.lo.sx", "vmins.w.sx">;
def : MnemonicAlias<"vmins.w.zx", "pvmins.lo">;
def : MnemonicAlias<"vmins.w", "pvmins.lo">;
def : MnemonicAlias<"pvmins.lo.zx", "pvmins.lo">;

// Section 8.10.18 - VCMX (Vector Compare and Select Maximum/Minimum)
defm VMAXSL : RVm<"vmaxs.l", 0x9a, V64, I64, VM>;
let cs2 = 1 in
defm VMINSL : RVm<"vmins.l", 0x9a, V64, I64, VM>;

//-----------------------------------------------------------------------------
// Section 8.11 - Vector Logical Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.11.1 - VAND (Vector And)
let cx = 0, cx2 = 0 in defm VAND : RVLm<"vand", 0xc4, I64, V64, VM>;
let cx = 0, cx2 = 1 in defm PVANDLO : RVLm<"pvand.lo", 0xc4, I32, V64, VM>;
let cx = 1, cx2 = 0 in defm PVANDUP : RVLm<"pvand.up", 0xc4, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVAND : RVLm<"pvand", 0xc4, I64, V64, VM512>;

// Section 8.11.2 - VOR (Vector Or)
let cx = 0, cx2 = 0 in defm VOR : RVLm<"vor", 0xc5, I64, V64, VM>;
let cx = 0, cx2 = 1 in defm PVORLO : RVLm<"pvor.lo", 0xc5, I32, V64, VM>;
let cx = 1, cx2 = 0 in defm PVORUP : RVLm<"pvor.up", 0xc5, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVOR : RVLm<"pvor", 0xc5, I64, V64, VM512>;

// Section 8.11.3 - VXOR (Vector Exclusive Or)
let cx = 0, cx2 = 0 in defm VXOR : RVLm<"vxor", 0xc6, I64, V64, VM>;
let cx = 0, cx2 = 1 in defm PVXORLO : RVLm<"pvxor.lo", 0xc6, I32, V64, VM>;
let cx = 1, cx2 = 0 in defm PVXORUP : RVLm<"pvxor.up", 0xc6, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVXOR : RVLm<"pvxor", 0xc6, I64, V64, VM512>;

// Section 8.11.4 - VEQV (Vector Equivalence)
let cx = 0, cx2 = 0 in defm VEQV : RVLm<"veqv", 0xc7, I64, V64, VM>;
let cx = 0, cx2 = 1 in defm PVEQVLO : RVLm<"pveqv.lo", 0xc7, I32, V64, VM>;
let cx = 1, cx2 = 0 in defm PVEQVUP : RVLm<"pveqv.up", 0xc7, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVEQV : RVLm<"pveqv", 0xc7, I64, V64, VM512>;

// Section 8.11.5 - VLDZ (Vector Leading Zero Count)
let cx = 0, cx2 = 0 in defm VLDZ : RV1m<"vldz", 0xe7, V64, VM>;
let cx = 0, cx2 = 1 in defm PVLDZLO : RV1m<"pvldz.lo", 0xe7, V64, VM>;
let cx = 1, cx2 = 0 in defm PVLDZUP : RV1m<"pvldz.up", 0xe7, V64, VM>;
let cx = 1, cx2 = 1 in defm PVLDZ : RV1m<"pvldz", 0xe7, V64, VM512>;

// Section 8.11.6 - VPCNT (Vector Population Count)
let cx = 0, cx2 = 0 in defm VPCNT : RV1m<"vpcnt", 0xac, V64, VM>;
let cx = 0, cx2 = 1 in defm PVPCNTLO : RV1m<"pvpcnt.lo", 0xac, V64, VM>;
let cx = 1, cx2 = 0 in defm PVPCNTUP : RV1m<"pvpcnt.up", 0xac, V64, VM>;
let cx = 1, cx2 = 1 in defm PVPCNT : RV1m<"pvpcnt", 0xac, V64, VM512>;

// Section 8.11.7 - VBRV (Vector Bit Reverse)
let cx = 0, cx2 = 0 in defm VBRV : RV1m<"vbrv", 0xf7, V64, VM>;
let cx = 0, cx2 = 1 in defm PVBRVLO : RV1m<"pvbrv.lo", 0xf7, V64, VM>;
let cx = 1, cx2 = 0 in defm PVBRVUP : RV1m<"pvbrv.up", 0xf7, V64, VM>;
let cx = 1, cx2 = 1 in defm PVBRV : RV1m<"pvbrv", 0xf7, V64, VM512>;

// Section 8.11.8 - VSEQ (Vector Sequential Number)
let cx = 0, cx2 = 0 in defm VSEQ : RV0m<"vseq", 0x99, V64, VM>;
let cx = 0, cx2 = 1 in defm PVSEQLO : RV0m<"pvseq.lo", 0x99, V64, VM>;
let cx = 1, cx2 = 0 in defm PVSEQUP : RV0m<"pvseq.up", 0x99, V64, VM>;
let cx = 1, cx2 = 1 in defm PVSEQ : RV0m<"pvseq", 0x99, V64, VM512>;

//-----------------------------------------------------------------------------
// Section 8.12 - Vector Shift Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.12.1 - VSLL (Vector Shift Left Logical)
let cx = 0, cx2 = 0 in defm VSLL : RVSm<"vsll", 0xe5, I64, V64, VM>;
let cx = 0, cx2 = 1 in defm PVSLLLO : RVSm<"pvsll.lo", 0xe5, I32, V64, VM>;
let cx = 1, cx2 = 0 in defm PVSLLUP : RVSm<"pvsll.up", 0xe5, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVSLL : RVSm<"pvsll", 0xe5, I64, V64, VM512>;

// Section 8.12.2 - VSLD (Vector Shift Left Double)
defm VSLD : RVSDm<"vsld", 0xe4, V64, VM>;

// Section 8.12.3 - VSRL (Vector Shift Right Logical)
let cx = 0, cx2 = 0 in defm VSRL : RVSm<"vsrl", 0xf5, I64, V64, VM>;
let cx = 0, cx2 = 1 in defm PVSRLLO : RVSm<"pvsrl.lo", 0xf5, I32, V64, VM>;
let cx = 1, cx2 = 0 in defm PVSRLUP : RVSm<"pvsrl.up", 0xf5, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVSRL : RVSm<"pvsrl", 0xf5, I64, V64, VM512>;

// Section 8.12.4 - VSRD (Vector Shift Right Double)
defm VSRD : RVSDm<"vsrd", 0xf4, V64, VM>;

// Section 8.12.5 - VSLA (Vector Shift Left Arithmetic)
let cx = 0, cx2 = 0 in defm VSLAWSX : RVSm<"vsla.w.sx", 0xe6, I32, V64, VM>;
let cx = 0, cx2 = 1 in {
  defm PVSLALO : RVSm<"pvsla.lo", 0xe6, I32, V64, VM>;
  let isCodeGenOnly = 1 in defm VSLAWZX : RVSm<"vsla.w.zx", 0xe6, I32, V64, VM>;
}
let cx = 1, cx2 = 0 in defm PVSLAUP : RVSm<"pvsla.up", 0xe6, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVSLA : RVSm<"pvsla", 0xe6, I64, V64, VM512>;
def : MnemonicAlias<"pvsla.lo.sx", "vsla.w.sx">;
def : MnemonicAlias<"vsla.w.zx", "pvsla.lo">;
def : MnemonicAlias<"vsla.w", "pvsla.lo">;
def : MnemonicAlias<"pvsla.lo.zx", "pvsla.lo">;

// Section 8.12.6 - VSLAX (Vector Shift Left Arithmetic)
defm VSLAL : RVSm<"vsla.l", 0xd4, I64, V64, VM>;

// Section 8.12.7 - VSRA (Vector Shift Right Arithmetic)
let cx = 0, cx2 = 0 in defm VSRAWSX : RVSm<"vsra.w.sx", 0xf6, I32, V64, VM>;
let cx = 0, cx2 = 1 in {
  defm PVSRALO : RVSm<"pvsra.lo", 0xf6, I32, V64, VM>;
  let isCodeGenOnly = 1 in defm VSRAWZX : RVSm<"vsra.w.zx", 0xf6, I32, V64, VM>;
}
let cx = 1, cx2 = 0 in defm PVSRAUP : RVSm<"pvsra.up", 0xf6, F32, V64, VM>;
let cx = 1, cx2 = 1 in defm PVSRA : RVSm<"pvsra", 0xf6, I64, V64, VM512>;
def : MnemonicAlias<"pvsra.lo.sx", "vsra.w.sx">;
def : MnemonicAlias<"vsra.w.zx", "pvsra.lo">;
def : MnemonicAlias<"vsra.w", "pvsra.lo">;
def : MnemonicAlias<"pvsra.lo.zx", "pvsra.lo">;

// Section 8.12.8 - VSRAX (Vector Shift Right Arithmetic)
defm VSRAL : RVSm<"vsra.l", 0xd5, I64, V64, VM>;

// Section 8.12.9 - VSFA (Vector Shift Left and Add)
defm VSFA : RVSAm<"vsfa", 0xd7, V64, VM>;

//-----------------------------------------------------------------------------
// Section 8.13 - Vector Floating-Point Arithmetic Instructions
//-----------------------------------------------------------------------------

// Section 8.13.1 - VFAD (Vector Floating Add)
let cx = 0, cx2 = 0 in
defm VFADDD : RVm<"vfadd.d", 0xcc, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFADDLO : RVm<"pvfadd.lo", 0xcc, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFADDUP : RVm<"pvfadd.up", 0xcc, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFADDS : RVm<"vfadd.s", 0xcc, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFADD : RVm<"pvfadd", 0xcc, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfadd.s", "pvfadd.up">;

// Section 8.13.2 - VFSB (Vector Floating Subtract)
let cx = 0, cx2 = 0 in
defm VFSUBD : RVm<"vfsub.d", 0xdc, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFSUBLO : RVm<"pvfsub.lo", 0xdc, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFSUBUP : RVm<"pvfsub.up", 0xdc, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFSUBS : RVm<"vfsub.s", 0xdc, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFSUB : RVm<"pvfsub", 0xdc, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfsub.s", "pvfsub.up">;

// Section 8.13.3 - VFMP (Vector Floating Multiply)
let cx = 0, cx2 = 0 in
defm VFMULD : RVm<"vfmul.d", 0xcd, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFMULLO : RVm<"pvfmul.lo", 0xcd, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFMULUP : RVm<"pvfmul.up", 0xcd, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFMULS : RVm<"vfmul.s", 0xcd, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFMUL : RVm<"pvfmul", 0xcd, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfmul.s", "pvfmul.up">;

// Section 8.13.4 - VFDV (Vector Floating Divide)
defm VFDIVD : RVDIVm<"vfdiv.d", 0xdd, V64, I64, VM, simm7fp>;
let cx = 1 in
defm VFDIVS : RVDIVm<"vfdiv.s", 0xdd, V64, F32, VM, simm7fp>;

// Section 8.13.5 - VFSQRT (Vector Floating Square Root)
defm VFSQRTD : RVF1m<"vfsqrt.d", 0xed, V64, VM>;
let cx = 1 in
defm VFSQRTS : RVF1m<"vfsqrt.s", 0xed, V64, VM>;

// Section 8.13.6 - VFCP (Vector Floating Compare)
let cx = 0, cx2 = 0 in
defm VFCMPD : RVm<"vfcmp.d", 0xfc, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFCMPLO : RVm<"pvfcmp.lo", 0xfc, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFCMPUP : RVm<"pvfcmp.up", 0xfc, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFCMPS : RVm<"vfcmp.s", 0xfc, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFCMP : RVm<"pvfcmp", 0xfc, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfcmp.s", "pvfcmp.up">;

// Section 8.13.7 - VFCM (Vector Floating Compare and Select Maximum/Minimum)
let cx = 0, cx2 = 0 in
defm VFMAXD : RVm<"vfmax.d", 0xbd, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFMAXLO : RVm<"pvfmax.lo", 0xbd, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFMAXUP : RVm<"pvfmax.up", 0xbd, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFMAXS : RVm<"vfmax.s", 0xbd, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFMAX : RVm<"pvfmax", 0xbd, V64, I64, VM512, simm7fp>;
let cs2 = 1 in {
  let cx = 0, cx2 = 0 in
  defm VFMIND : RVm<"vfmin.d", 0xbd, V64, I64, VM, simm7fp>;
  let cx = 0, cx2 = 1 in
  defm PVFMINLO : RVm<"pvfmin.lo", 0xbd, V64, I64, VM, simm7fp>;
  let cx = 1, cx2 = 0 in {
    defm PVFMINUP : RVm<"pvfmin.up", 0xbd, V64, F32, VM, simm7fp>;
    let isCodeGenOnly = 1 in
    defm VFMINS : RVm<"vfmin.s", 0xbd, V64, F32, VM, simm7fp>;
  }
  let cx = 1, cx2 = 1 in
  defm PVFMIN : RVm<"pvfmin", 0xbd, V64, I64, VM512, simm7fp>;
}
def : MnemonicAlias<"vfmax.s", "pvfmax.up">;
def : MnemonicAlias<"vfmin.s", "pvfmin.up">;

// Section 8.13.8 - VFMAD (Vector Floating Fused Multiply Add)
let cx = 0, cx2 = 0 in
defm VFMADD : RVMm<"vfmad.d", 0xe2, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFMADLO : RVMm<"pvfmad.lo", 0xe2, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFMADUP : RVMm<"pvfmad.up", 0xe2, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFMADS : RVMm<"vfmad.s", 0xe2, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFMAD : RVMm<"pvfmad", 0xe2, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfmad.s", "pvfmad.up">;

// Section 8.13.9 - VFMSB (Vector Floating Fused Multiply Subtract)
let cx = 0, cx2 = 0 in
defm VFMSBD : RVMm<"vfmsb.d", 0xf2, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFMSBLO : RVMm<"pvfmsb.lo", 0xf2, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFMSBUP : RVMm<"pvfmsb.up", 0xf2, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFMSBS : RVMm<"vfmsb.s", 0xf2, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFMSB : RVMm<"pvfmsb", 0xf2, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfmsb.s", "pvfmsb.up">;

// Section 8.13.10 - VFNMAD (Vector Floating Fused Negative Multiply Add)
let cx = 0, cx2 = 0 in
defm VFNMADD : RVMm<"vfnmad.d", 0xe3, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFNMADLO : RVMm<"pvfnmad.lo", 0xe3, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFNMADUP : RVMm<"pvfnmad.up", 0xe3, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFNMADS : RVMm<"vfnmad.s", 0xe3, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFNMAD : RVMm<"pvfnmad", 0xe3, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfnmad.s", "pvfnmad.up">;

// Section 8.13.11 - VFNMSB (Vector Floating Fused Negative Multiply Subtract)
let cx = 0, cx2 = 0 in
defm VFNMSBD : RVMm<"vfnmsb.d", 0xf3, V64, I64, VM, simm7fp>;
let cx = 0, cx2 = 1 in
defm PVFNMSBLO : RVMm<"pvfnmsb.lo", 0xf3, V64, I64, VM, simm7fp>;
let cx = 1, cx2 = 0 in {
  defm PVFNMSBUP : RVMm<"pvfnmsb.up", 0xf3, V64, F32, VM, simm7fp>;
  let isCodeGenOnly = 1 in
  defm VFNMSBS : RVMm<"vfnmsb.s", 0xf3, V64, F32, VM, simm7fp>;
}
let cx = 1, cx2 = 1 in
defm PVFNMSB : RVMm<"pvfnmsb", 0xf3, V64, I64, VM512, simm7fp>;
def : MnemonicAlias<"vfnmsb.s", "pvfnmsb.up">;

// Section 8.13.12 - VRCP (Vector Floating Reciprocal)
let cx = 0, cx2 = 0 in defm VRCPD : RVF1m<"vrcp.d", 0xe1, V64, VM>;
let cx = 0, cx2 = 1 in defm PVRCPLO : RVF1m<"pvrcp.lo", 0xe1, V64, VM>;
let cx = 1, cx2 = 0 in {
  defm PVRCPUP : RVF1m<"pvrcp.up", 0xe1, V64, VM>;
  let isCodeGenOnly = 1 in defm VRCPS : RVF1m<"vrcp.s", 0xe1, V64, VM>;
}
let cx = 1, cx2 = 1 in defm PVRCP : RVF1m<"pvrcp", 0xe1, V64, VM512>;
def : MnemonicAlias<"vrcp.s", "pvrcp.up">;

// Section 8.13.13 - VRSQRT (Vector Floating Reciprocal Square Root)
let cx = 0, cx2 = 0 in defm VRSQRTD : RVF1m<"vrsqrt.d", 0xf1, V64, VM>;
let cx = 0, cx2 = 1 in defm PVRSQRTLO : RVF1m<"pvrsqrt.lo", 0xf1, V64, VM>;
let cx = 1, cx2 = 0 in {
  defm PVRSQRTUP : RVF1m<"pvrsqrt.up", 0xf1, V64, VM>;
  let isCodeGenOnly = 1 in
  defm VRSQRTS : RVF1m<"vrsqrt.s", 0xf1, V64, VM>;
}
let cx = 1, cx2 = 1 in
defm PVRSQRT : RVF1m<"pvrsqrt", 0xf1, V64, VM512>;
let cs2 = 1 in {
    let cx = 0, cx2 = 0 in
    defm VRSQRTDNEX : RVF1m<"vrsqrt.d.nex", 0xf1, V64, VM>;
    let cx = 0, cx2 = 1 in
    defm PVRSQRTLONEX : RVF1m<"pvrsqrt.lo.nex", 0xf1, V64, VM>;
    let cx = 1, cx2 = 0 in {
      defm PVRSQRTUPNEX : RVF1m<"pvrsqrt.up.nex", 0xf1, V64, VM>;
      let isCodeGenOnly = 1 in
      defm VRSQRTSNEX : RVF1m<"vrsqrt.s.nex", 0xf1, V64, VM>;
    }
    let cx = 1, cx2 = 1 in
    defm PVRSQRTNEX : RVF1m<"pvrsqrt.nex", 0xf1, V64, VM512>;
}
def : MnemonicAlias<"vrsqrt.s", "pvrsqrt.up">;
def : MnemonicAlias<"vrsqrt.s.nex", "pvrsqrt.up.nex">;

// Section 8.13.14 - VFIX (Vector Convert to Fixed Pointer)
let cx = 0, cx2 = 0, cs2 = 0 in
defm VCVTWDSX : RVFIXm<"vcvt.w.d.sx", 0xe8, V64, VM>;
let cx = 0, cx2 = 1, cs2 = 0 in
defm VCVTWDZX : RVFIXm<"vcvt.w.d.zx", 0xe8, V64, VM>;
let cx = 1, cx2 = 0, cs2 = 0 in
defm VCVTWSSX : RVFIXm<"vcvt.w.s.sx", 0xe8, V64, VM>;
let cx = 1, cx2 = 1, cs2 = 0 in
defm VCVTWSZX : RVFIXm<"vcvt.w.s.zx", 0xe8, V64, VM>;
let cx = 0, cx2 = 1, cs2 = 1 in
defm PVCVTWSLO : RVFIXm<"pvcvt.w.s.lo", 0xe8, V64, VM>;
let cx = 1, cx2 = 0, cs2 = 1 in
defm PVCVTWSUP : RVFIXm<"pvcvt.w.s.up", 0xe8, V64, VM>;
let cx = 1, cx2 = 1, cs2 = 1 in
defm PVCVTWS : RVFIXm<"pvcvt.w.s", 0xe8, V64, VM512>;

// Section 8.13.15 - VFIXX (Vector Convert to Fixed Pointer)
defm VCVTLD : RVFIXm<"vcvt.l.d", 0xa8, V64, VM>;

// Section 8.13.16 - VFLT (Vector Convert to Floating Pointer)
let cx = 0, cx2 = 0, cs2 = 0 in
defm VCVTDW : RVF1m<"vcvt.d.w", 0xf8, V64, VM>;
let cx = 1, cx2 = 0, cs2 = 0 in
defm VCVTSW : RVF1m<"vcvt.s.w", 0xf8, V64, VM>;
let cx = 0, cx2 = 1, cs2 = 1 in
defm PVCVTSWLO : RVF1m<"pvcvt.s.w.lo", 0xf8, V64, VM>;
let cx = 1, cx2 = 0, cs2 = 1 in
defm PVCVTSWUP : RVF1m<"pvcvt.s.w.up", 0xf8, V64, VM>;
let cx = 1, cx2 = 1, cs2 = 1 in
defm PVCVTSW : RVF1m<"pvcvt.s.w", 0xf8, V64, VM512>;

// Section 8.13.17 - VFLTX (Vector Convert to Floating Pointer)
defm VCVTDL : RVF1m<"vcvt.d.l", 0xb8, V64, VM>;

// Section 8.13.18 - VCVS (Vector Convert to Single-format)
defm VCVTSD : RVF1m<"vcvt.s.d", 0x9f, V64, VM>;

// Section 8.13.19 - VCVD (Vector Convert to Double-format)
defm VCVTDS : RVF1m<"vcvt.d.s", 0x8f, V64, VM>;

//-----------------------------------------------------------------------------
// Section 8.14 - Vector Reduction Instructions
//-----------------------------------------------------------------------------

// Section 8.14.1 - VSUMS (Vector Sum Single)
defm VSUMWSX : RVF1m<"vsum.w.sx", 0xea, V64, VM>;
let cx2 = 1 in defm VSUMWZX : RVF1m<"vsum.w.zx", 0xea, V64, VM>;

// Section 8.14.2 - VSUMX (Vector Sum)
defm VSUML : RVF1m<"vsum.l", 0xaa, V64, VM>;

// Section 8.14.3 - VFSUM (Vector Floating Sum)
defm VFSUMD : RVF1m<"vfsum.d", 0xec, V64, VM>;
let cx = 1 in defm VFSUMS : RVF1m<"vfsum.s", 0xec, V64, VM>;

// Section 8.14.4 - VMAXS (Vector Maximum/Minimum Single)
let cx2 = 0 in defm VRMAXSWFSTSX : RVF1m<"vrmaxs.w.fst.sx", 0xbb, V64, VM>;
let cx2 = 1 in defm VRMAXSWFSTZX : RVF1m<"vrmaxs.w.fst.zx", 0xbb, V64, VM>;
let cs = 1 in {
  let cx2 = 0 in
  defm VRMAXSWLSTSX : RVF1m<"vrmaxs.w.lst.sx", 0xbb, V64, VM>;
  let cx2 = 1 in
  defm VRMAXSWLSTZX : RVF1m<"vrmaxs.w.lst.zx", 0xbb, V64, VM>;
}
let cs2 = 1 in {
  let cx2 = 0 in
  defm VRMINSWFSTSX : RVF1m<"vrmins.w.fst.sx", 0xbb, V64, VM>;
  let cx2 = 1 in
  defm VRMINSWFSTZX : RVF1m<"vrmins.w.fst.zx", 0xbb, V64, VM>;
  let cs = 1 in {
    let cx2 = 0 in
    defm VRMINSWLSTSX : RVF1m<"vrmins.w.lst.sx", 0xbb, V64, VM>;
    let cx2 = 1 in
    defm VRMINSWLSTZX : RVF1m<"vrmins.w.lst.zx", 0xbb, V64, VM>;
  }
}

// Section 8.14.5 - VMAXX (Vector Maximum/Minimum)
let cs = 0 in defm VRMAXSLFST : RVF1m<"vrmaxs.l.fst", 0xab, V64, VM>;
let cs = 1 in defm VRMAXSLLST : RVF1m<"vrmaxs.l.lst", 0xab, V64, VM>;
let cs2 = 1 in {
  let cs = 0 in defm VRMINSLFST : RVF1m<"vrmins.l.fst", 0xab, V64, VM>;
  let cs = 1 in defm VRMINSLLST : RVF1m<"vrmins.l.lst", 0xab, V64, VM>;
}

// Section 8.14.6 - VFMAX (Vector Floating Maximum/Minimum)
let cs = 0 in defm VFRMAXDFST : RVF1m<"vfrmax.d.fst", 0xad, V64, VM>;
let cs = 1 in defm VFRMAXDLST : RVF1m<"vfrmax.d.lst", 0xad, V64, VM>;
let cs2 = 1 in {
  let cs = 0 in defm VFRMINDFST : RVF1m<"vfrmin.d.fst", 0xad, V64, VM>;
  let cs = 1 in defm VFRMINDLST : RVF1m<"vfrmin.d.lst", 0xad, V64, VM>;
}
let cx = 1 in {
  let cs = 0 in defm VFRMAXSFST : RVF1m<"vfrmax.s.fst", 0xad, V64, VM>;
  let cs = 1 in defm VFRMAXSLST : RVF1m<"vfrmax.s.lst", 0xad, V64, VM>;
  let cs2 = 1 in {
    let cs = 0 in defm VFRMINSFST : RVF1m<"vfrmin.s.fst", 0xad, V64, VM>;
    let cs = 1 in defm VFRMINSLST : RVF1m<"vfrmin.s.lst", 0xad, V64, VM>;
  }
}

// Section 8.14.7 - VRAND (Vector Reduction And)
defm VRAND : RVF1m<"vrand", 0x88, V64, VM>;

// Section 8.14.8 - VROR (Vector Reduction Or)
defm VROR : RVF1m<"vror", 0x98, V64, VM>;

// Section 8.14.9 - VRXOR (Vector Reduction Exclusive Or)
defm VRXOR : RVF1m<"vrxor", 0x89, V64, VM>;

//-----------------------------------------------------------------------------
// Section 8.15 - Vector Iterative Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.15.1 - VFIA (Vector Floating Iteration Add)
let cx = 0 in defm VFIAD : RVI2m<"vfia.d", 0xce, V64, I64>;
let cx = 1 in defm VFIAS : RVI2m<"vfia.s", 0xce, V64, F32>;

// Section 8.15.2 - VFIS (Vector Floating Iteration Subtract)
let cx = 0 in defm VFISD : RVI2m<"vfis.d", 0xde, V64, I64>;
let cx = 1 in defm VFISS : RVI2m<"vfis.s", 0xde, V64, F32>;

// Section 8.15.3 - VFIM (Vector Floating Iteration Multiply)
let cx = 0 in defm VFIMD : RVI2m<"vfim.d", 0xcf, V64, I64>;
let cx = 1 in defm VFIMS : RVI2m<"vfim.s", 0xcf, V64, F32>;

// Section 8.15.4 - VFIAM (Vector Floating Iteration Add and Multiply)
let cx = 0 in defm VFIAMD : RVI3m<"vfiam.d", 0xee, V64, I64>;
let cx = 1 in defm VFIAMS : RVI3m<"vfiam.s", 0xee, V64, F32>;

// Section 8.15.5 - VFISM (Vector Floating Iteration Subtract and Multiply)
let cx = 0 in defm VFISMD : RVI3m<"vfism.d", 0xfe, V64, I64>;
let cx = 1 in defm VFISMS : RVI3m<"vfism.s", 0xfe, V64, F32>;

// Section 8.15.6 - VFIMA (Vector Floating Iteration Multiply and Add)
let cx = 0 in defm VFIMAD : RVI3m<"vfima.d", 0xef, V64, I64>;
let cx = 1 in defm VFIMAS : RVI3m<"vfima.s", 0xef, V64, F32>;

// Section 8.15.7 - VFIMS (Vector Floating Iteration Multiply and Subtract)
let cx = 0 in defm VFIMSD : RVI3m<"vfims.d", 0xff, V64, I64>;
let cx = 1 in defm VFIMSS : RVI3m<"vfims.s", 0xff, V64, F32>;

//-----------------------------------------------------------------------------
// Section 8.16 - Vector Merger Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.16.1 - VMRG (Vector Merge)
let cx = 0 in defm VMRG : RVm<"vmrg", 0xd6, V64, I64, VM>;
// FIXME: vmrg.w should be called as pvmrg, but following assembly manual.
let cx = 1 in defm VMRGW : RVm<"vmrg.w", 0xd6, V64, I64, VM512>;
def : MnemonicAlias<"vmrg.l", "vmrg">;

// Section 8.16.2 - VSHF (Vector Shuffle)
defm VSHF : RVSHFm<"vshf", 0xbc, V64>;

// Section 8.16.3 - VCP (Vector Compress)
defm VCP : RV1m<"vcp", 0x8d, V64, VM>;

// Section 8.16.4 - VEX (Vector Expand)
defm VEX : RV1m<"vex", 0x9d, V64, VM>;

//-----------------------------------------------------------------------------
// Section 8.17 - Vector Mask Operation Instructions
//-----------------------------------------------------------------------------

// Section 8.17.1 - VFMK (Vector Form Mask)
defm VFMKL : RVMKm<"vfmk.l.", 0xb4, V64, VM>;
def : MnemonicAlias<"vfmk.l", "vfmk.l.at">;

// Section 8.17.2 - VFMS (Vector Form Mask Single)
defm VFMKW : RVMKm<"vfmk.w.", 0xb5, V64, VM>;
let isCodeGenOnly = 1 in defm PVFMKWLO : RVMKm<"vfmk.w.", 0xb5, V64, VM>;
let cx = 1 in defm PVFMKWUP : RVMKm<"pvfmk.w.up.", 0xb5, V64, VM>;
def : MnemonicAlias<"vfmk.w", "vfmk.w.at">;
def : MnemonicAlias<"pvfmk.w.up", "pvfmk.w.up.at">;
def : MnemonicAlias<"pvfmk.w.lo", "vfmk.w.at">;
foreach CC = [ "af", "gt", "lt", "ne", "eq", "ge", "le", "at" ] in {
  def : MnemonicAlias<"pvfmk.w.lo."#CC, "vfmk.w."#CC>;
}

// Section 8.17.3 - VFMF (Vector Form Mask Floating Point)
defm VFMKD : RVMKm<"vfmk.d.", 0xb6, V64, VM>;
let cx2 = 1 in defm PVFMKSLO : RVMKm<"pvfmk.s.lo.", 0xb6, V64, VM>;
let cx = 1 in {
  defm PVFMKSUP : RVMKm<"pvfmk.s.up.", 0xb6, V64, VM>;
  let isCodeGenOnly = 1 in defm VFMKS : RVMKm<"vfmk.s.", 0xb6, V64, VM>;
}
def : MnemonicAlias<"vfmk.d", "vfmk.d.at">;
def : MnemonicAlias<"pvfmk.s.lo", "pvfmk.s.lo.at">;
def : MnemonicAlias<"pvfmk.s.up", "pvfmk.s.up.at">;
def : MnemonicAlias<"vfmk.s", "pvfmk.s.up.at">;
foreach CC = [ "af", "gt", "lt", "ne", "eq", "ge", "le", "at", "num", "nan",
               "gtnan", "ltnan", "nenan", "eqnan", "genan", "lenan" ] in {
  def : MnemonicAlias<"vfmk.s."#CC, "pvfmk.s.up."#CC>;
}

// Section 8.17.4 - ANDM (And VM)
defm ANDM : RVM2m<"andm", 0x84, VM>;

// Section 8.17.5 - ORM (Or VM)
defm ORM : RVM2m<"orm", 0x85, VM>;

// Section 8.17.6 - XORM (Exclusive Or VM)
defm XORM : RVM2m<"xorm", 0x86, VM>;

// Section 8.17.7 - EQVM (Equivalence VM)
defm EQVM : RVM2m<"eqvm", 0x87, VM>;

// Section 8.17.8 - NNDM (Negate And VM)
defm NNDM : RVM2m<"nndm", 0x94, VM>;

// Section 8.17.9 - NEGM (Negate VM)
defm NEGM : RVM1m<"negm", 0x95, VM>;

// Section 8.17.10 - PCVM (Population Count of VM)
defm PCVM : RVMSm<"pcvm", 0xa4, VM>;

// Section 8.17.11 - LZVM (Leading Zero of VM)
defm LZVM : RVMSm<"lzvm", 0xa5, VM>;

// Section 8.17.12 - TOVM (Trailing One of VM)
defm TOVM : RVMSm<"tovm", 0xa6, VM>;

//-----------------------------------------------------------------------------
// Section 8.18 - Vector Control Instructions
//-----------------------------------------------------------------------------

// Section 8.18.1 - LVL (Load VL)
let sx = 0, cz = 0, sz = 0, hasSideEffects = 0, Defs = [VL] in {
  def LVLr : RR<0xbf, (outs), (ins I64:$sy), "lvl $sy">;
  let cy = 0 in def LVLi : RR<0xbf, (outs), (ins simm7:$sy), "lvl $sy">;
}

// Section 8.18.2 - SVL (Save VL)
let cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 0, Uses = [VL] in
def SVL : RR<0x2f, (outs I64:$sx), (ins), "svl $sx">;

// Section 8.18.3 - SMVL (Save Maximum Vector Length)
let cy = 0, sy = 0, cz = 0, sz = 0, hasSideEffects = 0 in
def SMVL : RR<0x2e, (outs I64:$sx), (ins), "smvl $sx">;

// Section 8.18.4 - LVIX (Load Vector Data Index)
let sx = 0, cz = 0, sz = 0, hasSideEffects = 0, Defs = [VIX] in {
  def LVIXr : RR<0xaf, (outs), (ins I64:$sy), "lvix $sy">;
  let cy = 0 in def LVIXi : RR<0xaf, (outs), (ins uimm6:$sy), "lvix $sy">;
}
