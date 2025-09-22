/// Pattern Matchings for VEL intrinsic instructions.

/// Intrinsic patterns written by hand.

// Pack patterns.
def : Pat<(i64 (int_ve_vl_pack_f32p ADDRrii:$addr0, ADDRrii:$addr1)),
          (ORrr (f2l (LDUrii MEMrii:$addr0)),
                (i2l (LDLZXrii MEMrii:$addr1)))>;

def : Pat<(i64 (int_ve_vl_pack_f32a ADDRrii:$addr)),
          (MULULrr
            (i2l (LDLZXrii MEMrii:$addr)),
            (LEASLrii (ANDrm (LEAzii 0, 0, (LO32 (i64 0x0000000100000001))),
                             !add(32, 64)), 0,
                      (HI32 (i64 0x0000000100000001))))>;

// The extract/insert patterns.
def : Pat<(v256i1 (int_ve_vl_extract_vm512u v512i1:$vm)),
          (EXTRACT_SUBREG v512i1:$vm, sub_vm_even)>;

def : Pat<(v256i1 (int_ve_vl_extract_vm512l v512i1:$vm)),
          (EXTRACT_SUBREG v512i1:$vm, sub_vm_odd)>;

def : Pat<(v512i1 (int_ve_vl_insert_vm512u v512i1:$vmx, v256i1:$vmy)),
          (INSERT_SUBREG v512i1:$vmx, v256i1:$vmy, sub_vm_even)>;

def : Pat<(v512i1 (int_ve_vl_insert_vm512l v512i1:$vmx, v256i1:$vmy)),
          (INSERT_SUBREG v512i1:$vmx, v256i1:$vmy, sub_vm_odd)>;

// VMRG patterns.
def : Pat<(int_ve_vl_vmrgw_vsvMl i32:$sy, v256f64:$vz, v512i1:$vm, i32:$vl),
          (VMRGWrvml (i2l i32:$sy), v256f64:$vz, v512i1:$vm, i32:$vl)>;
def : Pat<(int_ve_vl_vmrgw_vsvMvl i32:$sy, v256f64:$vz, v512i1:$vm,
                                  v256f64:$pt, i32:$vl),
          (VMRGWrvml_v (i2l i32:$sy), v256f64:$vz, v512i1:$vm, i32:$vl,
                       v256f64:$pt)>;

// VMV patterns.
def : Pat<(int_ve_vl_vmv_vsvl i32:$sy, v256f64:$vz, i32:$vl),
          (VMVrvl (i2l i32:$sy), v256f64:$vz, i32:$vl)>;
def : Pat<(int_ve_vl_vmv_vsvvl i32:$sy, v256f64:$vz, v256f64:$pt, i32:$vl),
          (VMVrvl_v (i2l i32:$sy), v256f64:$vz, i32:$vl, v256f64:$pt)>;
def : Pat<(int_ve_vl_vmv_vsvmvl i32:$sy, v256f64:$vz, v256i1:$vm, v256f64:$pt,
                                i32:$vl),
          (VMVrvml_v (i2l i32:$sy), v256f64:$vz, v256i1:$vm, i32:$vl,
                     v256f64:$pt)>;

// LSV patterns.
def : Pat<(int_ve_vl_lsv_vvss v256f64:$pt, i32:$sy, i64:$sz),
          (LSVrr_v (i2l i32:$sy), i64:$sz, v256f64:$pt)>;

// LVS patterns.
def : Pat<(int_ve_vl_lvsl_svs v256f64:$vx, i32:$sy),
          (LVSvr v256f64:$vx, (i2l i32:$sy))>;
def : Pat<(int_ve_vl_lvsd_svs v256f64:$vx, i32:$sy),
          (LVSvr v256f64:$vx, (i2l i32:$sy))>;
def : Pat<(int_ve_vl_lvss_svs v256f64:$vx, i32:$sy),
          (l2f (LVSvr v256f64:$vx, (i2l i32:$sy)))>;

/// Intrinsic patterns automatically generated.
include "VEInstrIntrinsicVL.gen.td"
