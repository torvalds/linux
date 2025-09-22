//===- IntrinsicsNVVM.td - Defines NVVM intrinsics ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the NVVM-specific intrinsics for use with NVPTX.
//
//===----------------------------------------------------------------------===//

// The following intrinsics were once defined here, but are now auto-upgraded
// to target-generic LLVM intrinsics.
//
//   * llvm.nvvm.brev32  --> llvm.bitreverse.i32
//   * llvm.nvvm.brev64  --> llvm.bitreverse.i64
//   * llvm.nvvm.clz.i   --> llvm.ctlz.i32
//   * llvm.nvvm.clz.ll  --> trunc i64 llvm.ctlz.i64(x) to i32
//   * llvm.nvvm.popc.i  --> llvm.ctpop.i32
//   * llvm.nvvm.popc.ll --> trunc i64 llvm.ctpop.i64 to i32
//   * llvm.nvvm.abs.i   --> select(x >= -x, x, -x)
//   * llvm.nvvm.abs.ll  --> ibid.
//   * llvm.nvvm.max.i   --> select(x sge y, x, y)
//   * llvm.nvvm.max.ll  --> ibid.
//   * llvm.nvvm.max.ui  --> select(x uge y, x, y)
//   * llvm.nvvm.max.ull --> ibid.
//   * llvm.nvvm.max.i   --> select(x sle y, x, y)
//   * llvm.nvvm.max.ll  --> ibid.
//   * llvm.nvvm.max.ui  --> select(x ule y, x, y)
//   * llvm.nvvm.max.ull --> ibid.
//   * llvm.nvvm.h2f     --> llvm.convert.to.fp16.f32

def llvm_global_ptr_ty  : LLVMQualPointerType<1>;  // (global)ptr
def llvm_shared_ptr_ty  : LLVMQualPointerType<3>;  // (shared)ptr

//
// MISC
//

// Helper class that represents a 'fragment' of an NVPTX *MMA instruction.
// Geom: m<M>n<N>k<K>. E.g. m8n32k16
// Frag: [a|b|c|d] ([x1|x2|x4] for ldmatrix)
// PtxEltType: PTX type for the element.
class WMMA_REGS<string Geom, string Frag, string PtxEltType> {
  string geom = Geom;
  string frag = Frag;
  string ptx_elt_type = PtxEltType;
  string gft = Geom#":"#Frag#":"#ptx_elt_type;
  string ft = frag#":"#ptx_elt_type;
  list<LLVMType> regs = !cond(
    // mma fp ops use smaller fragments than wmma fp ops
    !eq(gft,"m8n8k4:a:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m8n8k4:b:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m16n8k8:a:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m16n8k8:b:f16") : [llvm_v2f16_ty],
    !eq(gft,"m16n8k8:c:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m16n8k8:d:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m16n8k8:c:f32") : !listsplat(llvm_float_ty, 4),
    !eq(gft,"m16n8k8:d:f32") : !listsplat(llvm_float_ty, 4),
    !eq(gft,"m16n8k16:a:f16") : !listsplat(llvm_v2f16_ty, 4),
    !eq(gft,"m16n8k16:b:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m16n8k16:c:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m16n8k16:d:f16") : !listsplat(llvm_v2f16_ty, 2),
    !eq(gft,"m16n8k16:c:f32") : !listsplat(llvm_float_ty, 4),
    !eq(gft,"m16n8k16:d:f32") : !listsplat(llvm_float_ty, 4),
    !eq(gft,"m16n8k4:c:f32") : !listsplat(llvm_float_ty, 4),
    !eq(gft,"m16n8k4:d:f32") : !listsplat(llvm_float_ty, 4),

    // wmma fp16 -> fp16/fp32 @  m16n16k16/m8n32k16/m32n8k16
    // All other supported geometries use the same fragment format for f32 and
    // f16, so we only need to consider {fragment, type}.
    !eq(ft,"a:f16") : !listsplat(llvm_v2f16_ty, 8),
    !eq(ft,"b:f16") : !listsplat(llvm_v2f16_ty, 8),
    !eq(ft,"c:f16") : !listsplat(llvm_v2f16_ty, 4),
    !eq(ft,"d:f16") : !listsplat(llvm_v2f16_ty, 4),
    !eq(ft,"c:f32") : !listsplat(llvm_float_ty, 8),
    !eq(ft,"d:f32") : !listsplat(llvm_float_ty, 8),

    // wmma tf32 -> s32 @ m16n16k8
    !eq(gft,"m16n16k8:a:tf32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n16k8:b:tf32") : !listsplat(llvm_i32_ty, 4),

    // mma tf32 -> s32 @ m16n16k8/m16n8k8
    !eq(gft,"m16n8k4:a:tf32") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k4:b:tf32") : [llvm_i32_ty],
    !eq(gft,"m16n8k8:a:tf32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k8:b:tf32") : !listsplat(llvm_i32_ty, 2),

    !eq(gft,"m8n8k4:a:f64") : [llvm_double_ty],
    !eq(gft,"m8n8k4:b:f64") : [llvm_double_ty],
    !eq(gft,"m8n8k4:c:f64") : !listsplat(llvm_double_ty, 2),
    !eq(gft,"m8n8k4:d:f64") : !listsplat(llvm_double_ty, 2),

    // wmma bf16 -> s32 @ m16n16k16/m8n32k16/m32n8k16
    !eq(gft,"m16n16k16:a:bf16") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n16k16:b:bf16") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m8n32k16:a:bf16") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m8n32k16:b:bf16") : !listsplat(llvm_i32_ty, 8),
    !eq(gft,"m32n8k16:a:bf16") : !listsplat(llvm_i32_ty, 8),
    !eq(gft,"m32n8k16:b:bf16") : !listsplat(llvm_i32_ty, 2),

    // mma bf16 -> s32 @ m16n8k16/m16n8k8
    !eq(gft,"m16n8k16:a:bf16") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k16:b:bf16") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k8:a:bf16") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k8:b:bf16") : [llvm_i32_ty],

    // wmma u8/s8 -> s32 @ m16n16k16/m8n32k16/m32n8k16
    !eq(gft,"m16n16k16:a:u8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n16k16:a:s8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n16k16:b:u8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n16k16:b:s8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n16k16:c:s32") : !listsplat(llvm_i32_ty, 8),
    !eq(gft,"m16n16k16:d:s32") : !listsplat(llvm_i32_ty, 8),

    !eq(gft,"m8n32k16:a:u8") : [llvm_i32_ty],
    !eq(gft,"m8n32k16:a:s8") : [llvm_i32_ty],
    !eq(gft,"m8n32k16:b:u8") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m8n32k16:b:s8") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m8n32k16:c:s32") : !listsplat(llvm_i32_ty, 8),
    !eq(gft,"m8n32k16:d:s32") : !listsplat(llvm_i32_ty, 8),

    !eq(gft,"m32n8k16:a:u8") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m32n8k16:a:s8") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m32n8k16:b:u8") : [llvm_i32_ty],
    !eq(gft,"m32n8k16:b:s8") : [llvm_i32_ty],
    !eq(gft,"m32n8k16:c:s32") : !listsplat(llvm_i32_ty, 8),
    !eq(gft,"m32n8k16:d:s32") : !listsplat(llvm_i32_ty, 8),

    // mma u8/s8 -> s32 @ m8n8k16/m16n8k16/m16n8k32
    !eq(gft,"m8n8k16:a:u8") : [llvm_i32_ty],
    !eq(gft,"m8n8k16:a:s8") : [llvm_i32_ty],
    !eq(gft,"m8n8k16:b:u8") : [llvm_i32_ty],
    !eq(gft,"m8n8k16:b:s8") : [llvm_i32_ty],
    !eq(gft,"m8n8k16:c:s32") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m8n8k16:d:s32") : !listsplat(llvm_i32_ty, 2),

    !eq(gft,"m16n8k16:a:u8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k16:a:s8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k16:b:u8") : [llvm_i32_ty],
    !eq(gft,"m16n8k16:b:s8") : [llvm_i32_ty],
    !eq(gft,"m16n8k16:c:s32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k16:d:s32") : !listsplat(llvm_i32_ty, 4),

    !eq(gft,"m16n8k32:a:u8") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k32:a:s8") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k32:b:u8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k32:b:s8") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k32:c:s32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k32:d:s32") : !listsplat(llvm_i32_ty, 4),

    // wmma/mma u4/s4 -> s32 @ m8n8k32 (u4/s4)
    !eq(gft,"m8n8k32:a:u4") : [llvm_i32_ty],
    !eq(gft,"m8n8k32:a:s4") : [llvm_i32_ty],
    !eq(gft,"m8n8k32:b:u4") : [llvm_i32_ty],
    !eq(gft,"m8n8k32:b:s4") : [llvm_i32_ty],
    !eq(gft,"m8n8k32:c:s32") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m8n8k32:d:s32") : !listsplat(llvm_i32_ty, 2),

    !eq(gft,"m16n8k32:a:u4") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k32:a:s4") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k32:b:u4") : [llvm_i32_ty],
    !eq(gft,"m16n8k32:b:s4") : [llvm_i32_ty],
    !eq(gft,"m16n8k32:c:s32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k32:d:s32") : !listsplat(llvm_i32_ty, 4),

    !eq(gft,"m16n8k64:a:u4") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k64:a:s4") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k64:b:u4") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k64:b:s4") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k64:c:s32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k64:d:s32") : !listsplat(llvm_i32_ty, 4),

    // wmma/mma b1 -> s32 @ m8n8k128(b1)
    !eq(gft,"m8n8k128:a:b1") : [llvm_i32_ty],
    !eq(gft,"m8n8k128:b:b1") : [llvm_i32_ty],
    !eq(gft,"m8n8k128:c:s32") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m8n8k128:d:s32") : !listsplat(llvm_i32_ty, 2),

    !eq(gft,"m16n8k128:a:b1") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k128:b:b1") : [llvm_i32_ty],
    !eq(gft,"m16n8k128:c:s32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k128:d:s32") : !listsplat(llvm_i32_ty, 4),

    !eq(gft,"m16n8k256:a:b1") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k256:b:b1") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m16n8k256:c:s32") : !listsplat(llvm_i32_ty, 4),
    !eq(gft,"m16n8k256:d:s32") : !listsplat(llvm_i32_ty, 4),

    // ldmatrix b16 -> s32 @ m8n8
    !eq(gft,"m8n8:x1:b16") : !listsplat(llvm_i32_ty, 1),
    !eq(gft,"m8n8:x2:b16") : !listsplat(llvm_i32_ty, 2),
    !eq(gft,"m8n8:x4:b16") : !listsplat(llvm_i32_ty, 4),
  );
}

class WMMA_NAME_LDST<string Op, WMMA_REGS Frag, string Layout, int WithStride> {
  string intr = "llvm.nvvm.wmma."
                # Frag.geom
                # "." # Op
                # "." # Frag.frag
                # "." # Layout
                # !if(WithStride, ".stride", "")
                # "." # Frag.ptx_elt_type
                ;
  // TODO(tra): record name should ideally use the same field order as the intrinsic.
  // E.g. string record = !subst("llvm", "int",
  //                      !subst(".", "_", llvm));
  string record = "int_nvvm_wmma_"
                # Frag.geom
                # "_" # Op
                # "_" # Frag.frag
                # "_" # Frag.ptx_elt_type
                # "_" # Layout
                # !if(WithStride, "_stride", "");
}

class MMA_SIGNATURE<WMMA_REGS A, WMMA_REGS B, WMMA_REGS C, WMMA_REGS D> {
  list<WMMA_REGS> id_frags = !cond(
     // FP16 ops are identified by accumulator & result type.
     !eq(A.ptx_elt_type, "f16") : [D, C],
     // other ops are identified by input types.
     !ne(A.ptx_elt_type, B.ptx_elt_type): [A, B],
     true: [A]
     );
   string ret = !foldl("", id_frags, a, b, !strconcat(a, ".", b.ptx_elt_type));
}

class WMMA_NAME<string ALayout, string BLayout, int Satfinite, string Rnd, string b1op,
                WMMA_REGS A, WMMA_REGS B, WMMA_REGS C, WMMA_REGS D> {
  string signature = MMA_SIGNATURE<A, B, C, D>.ret;
  string llvm = "llvm.nvvm.wmma."
                # A.geom
                # ".mma"
                # b1op
                # "." # ALayout
                # "." # BLayout
                # !if(!ne(Rnd, ""), !strconcat(".", Rnd), "")
                # signature
                # !if(Satfinite, ".satfinite", "");

  string record = !subst(".", "_",
                  !subst("llvm.", "int_", llvm));
}

class MMA_NAME<string ALayout, string BLayout, int Satfinite, string b1op,
               WMMA_REGS A, WMMA_REGS B, WMMA_REGS C, WMMA_REGS D> {
  string signature = MMA_SIGNATURE<A, B, C, D>.ret;
  string llvm = "llvm.nvvm.mma"
                # b1op
                # "." # A.geom
                # "." # ALayout
                # "." # BLayout
                # !if(Satfinite, ".satfinite", "")
                # signature;
  string record = !subst(".", "_",
                  !subst("llvm.", "int_", llvm));
}

class LDMATRIX_NAME<WMMA_REGS Frag, int Trans> {
  string intr = "llvm.nvvm.ldmatrix.sync.aligned"
                # "." # Frag.geom
                # "." # Frag.frag
                # !if(Trans, ".trans", "")
                # "." # Frag.ptx_elt_type
                ;
  string record = !subst(".", "_",
                  !subst("llvm.", "int_", intr));
}

// Generates list of 4-tuples of WMMA_REGS representing a valid MMA op.
//   Geom: list of supported geometries.
//   TypeN: PTX type of the corresponding fragment's element.
//   TypeB and TypeD may be empty if it must match that of TypeA or TypeC.
class MMA_OPS<list<string> Geom, list<string> TypeA, list<string> TypeB,
            list<string> TypeC, list<string> TypeD> {
  list<list<WMMA_REGS>> ret =
     !foldl([]<list<WMMA_REGS>>, Geom, t1, geom, !listconcat(t1,
     !foldl([]<list<WMMA_REGS>>, TypeA, t2, type_a, !listconcat(t2,
     !foldl([]<list<WMMA_REGS>>, !if(!size(TypeB), TypeB, [type_a]), t3, type_b, !listconcat(t3,
     !foldl([]<list<WMMA_REGS>>, TypeC, t4, type_c, !listconcat(t4,
     !foldl([]<list<WMMA_REGS>>, !if(!size(TypeD), TypeD, [type_c]), t5, type_d, !listconcat(t5,
            [[WMMA_REGS<geom, "a", type_a>,
              WMMA_REGS<geom, "b", type_b>,
              WMMA_REGS<geom, "c", type_c>,
              WMMA_REGS<geom, "d", type_d>]]))))))))));
   // Debugging aid for readable representation of the list above.
   list<list<string>> ops = !foreach(x, ret, [x[0].gft, x[1].gft, x[2].gft, x[3].gft]);
}

class MMA_LDST_OPS<list<string> Geom, list<string> Frags, list<string> Types> {
  list<WMMA_REGS> ret =
     !foldl([]<WMMA_REGS>, Geom, t1, geom, !listconcat(t1,
     !foldl([]<WMMA_REGS>, Frags, t2, frag, !listconcat(t2,
     !foldl([]<WMMA_REGS>, Types, t3, type, !listconcat(t3,
            [WMMA_REGS<geom, frag, type>]))))));
   // Debugging aid for readable representation of the list above.
   list<string> ops = !foreach(x, ret, x.gft);
}

class LDMATRIX_OPS<list<string> Geom, list<string> Frags, list<string> Types> {
  list<WMMA_REGS> ret =
     !foldl([]<WMMA_REGS>, Geom, t1, geom, !listconcat(t1,
     !foldl([]<WMMA_REGS>, Frags, t2, frag, !listconcat(t2,
     !foldl([]<WMMA_REGS>, Types, t3, type, !listconcat(t3,
            [WMMA_REGS<geom, frag, type>]))))));
   // Debugging aid for readable representation of the list above.
   list<string> ops = !foreach(x, ret, x.gft);
}

// Creates list of valid combinations of fragments. This is the main list that
// drives generation of corresponding intrinsics and instructions.
class NVVM_MMA_OPS {
  list<list<WMMA_REGS>> tf32_wmma_ops = MMA_OPS<
            ["m16n16k8"],
            ["tf32"], [], ["f32"], []>.ret;
  list<list<WMMA_REGS>> bf16_wmma_ops = MMA_OPS<
            ["m16n16k16", "m32n8k16", "m8n32k16"],
            ["bf16"], [], ["f32"], []>.ret;
  list<list<WMMA_REGS>> f64_wmma_ops = MMA_OPS<
            ["m8n8k4"],
            ["f64"], [], ["f64"], []>.ret;
  list<list<WMMA_REGS>> fp_wmma_ops = MMA_OPS<
            ["m16n16k16", "m32n8k16", "m8n32k16"],
            ["f16"], [], ["f16", "f32"], ["f16", "f32"]>.ret;
  list<list<WMMA_REGS>> int_wmma_ops = MMA_OPS<
            ["m16n16k16", "m32n8k16", "m8n32k16"],
            ["s8", "u8"], [], ["s32"], []>.ret;
  list<list<WMMA_REGS>> subint_wmma_ops = MMA_OPS<
            ["m8n8k32"],
            ["s4", "u4"], [], ["s32"], []>.ret;
  list<list<WMMA_REGS>> bit_wmma_ops = MMA_OPS<
            ["m8n8k128"],
            ["b1"], [], ["s32"], []>.ret;
  list<list<WMMA_REGS>> all_wmma_ops = !listconcat(
            tf32_wmma_ops, bf16_wmma_ops, f64_wmma_ops,
            fp_wmma_ops, int_wmma_ops, subint_wmma_ops, bit_wmma_ops);

  list<list<WMMA_REGS>> tf32_mma_ops = MMA_OPS<
            ["m16n8k4", "m16n8k8"],
            ["tf32"], [], ["f32"], []>.ret;
  list<list<WMMA_REGS>> bf16_mma_ops = MMA_OPS<
            ["m16n8k16", "m16n8k8"],
            ["bf16"], [], ["f32"], []>.ret;
  list<list<WMMA_REGS>> f64_mma_ops = MMA_OPS<
            ["m8n8k4"],
            ["f64"], [], ["f64"], []>.ret;
  list<list<WMMA_REGS>> fp_mma_ops = MMA_OPS<
            ["m8n8k4", "m16n8k8", "m16n8k16"],
            ["f16"], [], ["f16", "f32"], ["f16", "f32"]>.ret;
  list<list<WMMA_REGS>> int_mma_ops = MMA_OPS<
            ["m8n8k16", "m16n8k16", "m16n8k32"],
            ["s8", "u8"], ["s8", "u8"], ["s32"], []>.ret;
  list<list<WMMA_REGS>> subint_mma_ops = MMA_OPS<
            ["m8n8k32", "m16n8k32", "m16n8k64"],
            ["s4", "u4"], ["s4", "u4"], ["s32"], []>.ret;
  list<list<WMMA_REGS>> bit_mma_ops = MMA_OPS<
            ["m8n8k128", "m16n8k128", "m16n8k256"],
            ["b1"], [], ["s32"], []>.ret;
  list<list<WMMA_REGS>> all_mma_ops = !listconcat(
            tf32_mma_ops, bf16_mma_ops, f64_mma_ops,
            fp_mma_ops, int_mma_ops, subint_mma_ops, bit_mma_ops);

  list<WMMA_REGS> ldst_ab_ops = MMA_LDST_OPS<
            ["m16n16k16", "m32n8k16", "m8n32k16"],
            ["a", "b"], ["f16", "u8", "s8", "bf16"]>.ret;
  list<WMMA_REGS> ldst_cd_ops = MMA_LDST_OPS<
            ["m16n16k16", "m32n8k16", "m8n32k16"],
            ["c", "d"], ["f16", "f32", "s32"]>.ret;
  list<WMMA_REGS> ldst_tf32_ab_ops = MMA_LDST_OPS<
            ["m16n16k8"],
            ["a", "b"], ["tf32"]>.ret;
  list<WMMA_REGS> ldst_tf32_cd_ops = MMA_LDST_OPS<
            ["m16n16k8"],
            ["c", "d"], ["f32"]>.ret;
  list<WMMA_REGS> ldst_f64_abcd_ops = MMA_LDST_OPS<
            ["m8n8k4"],
            ["a", "b", "c", "d"], ["f64"]>.ret;
  list<WMMA_REGS> ldst_subint_ab_ops = MMA_LDST_OPS<
            ["m8n8k32"], ["a", "b"], ["s4","u4"]>.ret;
  list<WMMA_REGS> ldst_bit_ab_ops = MMA_LDST_OPS<
            ["m8n8k128"], ["a", "b"], ["b1"]>.ret;
  list<WMMA_REGS> ldst_subint_cd_ops = MMA_LDST_OPS<
            ["m8n8k32", "m8n8k128"],  ["c", "d"], ["s32"]>.ret;
  list<WMMA_REGS> all_ldst_ops = !listconcat(ldst_ab_ops, ldst_cd_ops,
                                             ldst_tf32_ab_ops,
                                             ldst_tf32_cd_ops,
                                             ldst_f64_abcd_ops,
                                             ldst_subint_ab_ops,
                                             ldst_bit_ab_ops,
                                             ldst_subint_cd_ops);
  // Separate A/B/C fragments (loads) from D (stores).
  list<WMMA_REGS> all_ld_ops = !filter(op, all_ldst_ops, !ne(op.frag, "d"));
  list<WMMA_REGS> all_st_ops = !filter(op, all_ldst_ops, !eq(op.frag, "d"));

  list<WMMA_REGS> ldmatrix_b16_ops = LDMATRIX_OPS<
    ["m8n8"], ["x1", "x2", "x4"], ["b16"]>.ret;
  list<WMMA_REGS> all_ldmatrix_ops = ldmatrix_b16_ops;
}

def NVVM_MMA_OPS : NVVM_MMA_OPS;

// Returns true if this combination of fragment and layout for WMMA load/store
// ops is supported; false otherwise.
// E.g.
// if NVVM_WMMA_LDST_SUPPORTED<...>.ret then
//   def : FOO<>; // The record will only be defined for supported ops.
//
class NVVM_WMMA_LDST_SUPPORTED<WMMA_REGS frag, string layout> {
  string f = frag.frag;
  string t = frag.ptx_elt_type;

  bit ret = !cond(
    // Sub-int load and store requires A fragment to be of row layout and B
    // fragments to be of column layout.
    !and(!or(!eq(t, "b1"),
             !eq(t, "u4"),
             !eq(t, "s4")),
         !or(!and(!eq(f, "a"),
                  !ne(layout, "row")),
             !and(!eq(f, "b"),
                  !ne(layout, "col")))) : false,
    true: true
  );
}

// Returns true if this combination of layout/satf/rnd for WMMA ops is
// supported; false otherwise.
// E.g.
// if NVVM_WMMA_SUPPORTED<...>.ret then
//   def : FOO<>; // The record will only be defined for supported ops.
//
class NVVM_WMMA_SUPPORTED<list<WMMA_REGS> frags, string layout_a, string layout_b, int satf, string rnd> {
  // WMMA ops check both layouts.
  string layout = layout_a # ":" # layout_b;
  string t = frags[0].ptx_elt_type;

  bit ret = !cond(
    // only f64 wmma functions support rnd options
    // any non f64 type that uses a rnd value is invalid
    !and(!ne(t, "f64"), !ne(rnd, "")) : false,

    // satf is only valid for select types
    !and(!eq(satf, 1),
         !ne(t, "s8"),
         !ne(t, "u8"),
         !ne(t, "s4"),
         !ne(t, "u4"),
         !ne(t, "f16")): false,

    // Sub-int wmma requires row/column layout
    !and(!or(!eq(t, "s4"),
             !eq(t, "u4"),
             !eq(t, "b1")),
         !ne(layout, "row:col")) : false,
    true: true
  );
}

class NVVM_MMA_B1OPS<list<WMMA_REGS> frags> {
  list<string> ret = !cond(
    !eq(frags[0].ptx_elt_type, "b1") : [".xor.popc", ".and.popc"],
    true: [""]
  );
}

// Returns true if this combination of layout/satf for MMA ops is supported;
// false otherwise.
// E.g.
// if NVVM_MMA_SUPPORTED<...>.ret then
//   def : FOO<>; // The record will only be defined for supported ops.
//
class NVVM_MMA_SUPPORTED<list<WMMA_REGS> frags, string layout_a, string layout_b, int satf> {
  // MMA ops check both layouts.
  string layout = layout_a # ":" # layout_b;
  string a_type = frags[0].ptx_elt_type;
  string b_type = frags[1].ptx_elt_type;
  string c_type = frags[2].ptx_elt_type;
  string d_type = frags[3].ptx_elt_type;
  string geom = frags[0].geom;

  // gcd is a shortcut used to identify instructions that depend on
  // geom+frag_c+frag_d.
  string gcd = geom # ":" # c_type # d_type;
  bit ret = !cond(

    // Limit satf to valid types
    !and(!eq(satf, 1),
         !ne(a_type, "s8"),
         !ne(a_type, "u8"),
         !ne(a_type, "s4"),
         !ne(a_type, "u4")): false,

    // m8n8k4 has no C=f32 D=f16 variant.
    !eq(gcd, "m8n8k4:f32f16"): false,

    // only m8n8k4 for f16 does not require row:col layout
    !and(!ne(layout, "row:col"),
         !or(!ne(geom, "m8n8k4"),
             !ne(a_type, "f16"))) : false,

    // m16n8k8 requires A and B to be the same type and C and D to be the same
    // type.
    !and(!eq(geom, "m16n8k8"),
         !or(!ne(a_type, b_type),
             !ne(c_type, d_type))): false,

    // m16n8k8 requires C and D to be the same type.
    !and(!eq(geom, "m16n8k8"),
         !ne(c_type, d_type)): false,

    // All other are OK.
    true: true
  );
}

// Returns true if the fragment is valid for ldmatrix ops is supported;
// false otherwise.
// E.g.
// if NVVM_LDMATRIX_SUPPORTED<...>.ret then
//   def : FOO<>; // The record will only be defined for supported ops.
//
class NVVM_LDMATRIX_SUPPORTED<WMMA_REGS frag> {
  string g = frag.geom;
  string t = frag.ptx_elt_type;

  bit ret = !cond(
    // Only currently support m8n8 and b16
    !and(!eq(g, "m8n8"), !eq(t, "b16")): true,
    true: false
  );
}

class SHFL_INFO<bit sync, string mode, string type, bit return_pred> {
  string Suffix = !if(sync, "sync_", "")
                  # mode # "_"
                  # type
                  # !if(return_pred, "p", "");

  string Name = "int_nvvm_shfl_" # Suffix;
  string Builtin = "__nvvm_shfl_" # Suffix;
  string IntrName = "llvm.nvvm.shfl." # !subst("_",".", Suffix);
  bit withGccBuiltin = !not(return_pred);
  bit withoutGccBuiltin = return_pred;
  LLVMType OpType = !cond(
    !eq(type,"i32"): llvm_i32_ty,
    !eq(type,"f32"): llvm_float_ty);
  list<LLVMType> RetTy = !if(return_pred, [OpType, llvm_i1_ty], [OpType]);
  list<LLVMType> ArgsTy = !if(sync,
    [llvm_i32_ty, OpType, llvm_i32_ty, llvm_i32_ty],
    [OpType, llvm_i32_ty, llvm_i32_ty]);
}

let TargetPrefix = "nvvm" in {
  def int_nvvm_prmt : ClangBuiltin<"__nvvm_prmt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_nanosleep : ClangBuiltin<"__nvvm_nanosleep">,
      DefaultAttrsIntrinsic<[], [llvm_i32_ty],
                            [IntrConvergent, IntrNoMem, IntrHasSideEffects]>;

//
// Min Max
//

  foreach operation = ["min", "max"] in {
    def int_nvvm_f # operation # _d :
      ClangBuiltin<!strconcat("__nvvm_f", operation, "_d")>,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

    foreach variant = ["_f", "_ftz_f", "_nan_f", "_ftz_nan_f",
      "_xorsign_abs_f", "_ftz_xorsign_abs_f", "_nan_xorsign_abs_f",
      "_ftz_nan_xorsign_abs_f"] in {
      def int_nvvm_f # operation # variant :
        ClangBuiltin<!strconcat("__nvvm_f", operation, variant)>,
        DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
          [IntrNoMem, IntrSpeculatable, Commutative]>;
    }

    foreach variant = ["_f16", "_ftz_f16", "_nan_f16", "_ftz_nan_f16",
      "_xorsign_abs_f16", "_ftz_xorsign_abs_f16", "_nan_xorsign_abs_f16",
      "_ftz_nan_xorsign_abs_f16"] in {
      def int_nvvm_f # operation # variant :
        DefaultAttrsIntrinsic<[llvm_half_ty], [llvm_half_ty, llvm_half_ty],
          [IntrNoMem, IntrSpeculatable, Commutative]>;
    }

    foreach variant = ["_f16x2", "_ftz_f16x2", "_nan_f16x2",
      "_ftz_nan_f16x2", "_xorsign_abs_f16x2", "_ftz_xorsign_abs_f16x2",
      "_nan_xorsign_abs_f16x2", "_ftz_nan_xorsign_abs_f16x2"] in {
      def int_nvvm_f # operation # variant :
        DefaultAttrsIntrinsic<[llvm_v2f16_ty], [llvm_v2f16_ty, llvm_v2f16_ty],
          [IntrNoMem, IntrSpeculatable, Commutative]>;
    }

    foreach variant = ["_bf16", "_ftz_bf16", "_nan_bf16", "_ftz_nan_bf16",
      "_xorsign_abs_bf16", "_ftz_xorsign_abs_bf16", "_nan_xorsign_abs_bf16",
      "_ftz_nan_xorsign_abs_bf16"] in {
      def int_nvvm_f # operation # variant :
        ClangBuiltin<!strconcat("__nvvm_f", operation, variant)>,
        DefaultAttrsIntrinsic<[llvm_bfloat_ty], [llvm_bfloat_ty, llvm_bfloat_ty],
          [IntrNoMem, IntrSpeculatable, Commutative]>;
    }

    foreach variant = ["_bf16x2", "_ftz_bf16x2", "_nan_bf16x2",
      "_ftz_nan_bf16x2", "_xorsign_abs_bf16x2", "_ftz_xorsign_abs_bf16x2",
      "_nan_xorsign_abs_bf16x2", "_ftz_nan_xorsign_abs_bf16x2"]  in {
      def int_nvvm_f # operation # variant :
        ClangBuiltin<!strconcat("__nvvm_f", operation, variant)>,
        DefaultAttrsIntrinsic<[llvm_v2bf16_ty], [llvm_v2bf16_ty, llvm_v2bf16_ty],
          [IntrNoMem, IntrSpeculatable, Commutative]>;
    }
  }

//
// Multiplication
//

  def int_nvvm_mulhi_s : ClangBuiltin<"__nvvm_mulhi_s">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_i16_ty, llvm_i16_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mulhi_us : ClangBuiltin<"__nvvm_mulhi_us">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_i16_ty, llvm_i16_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

  def int_nvvm_mulhi_i : ClangBuiltin<"__nvvm_mulhi_i">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mulhi_ui : ClangBuiltin<"__nvvm_mulhi_ui">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

  def int_nvvm_mulhi_ll : ClangBuiltin<"__nvvm_mulhi_ll">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mulhi_ull : ClangBuiltin<"__nvvm_mulhi_ull">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

  def int_nvvm_mul_rn_ftz_f : ClangBuiltin<"__nvvm_mul_rn_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rn_f : ClangBuiltin<"__nvvm_mul_rn_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rz_ftz_f : ClangBuiltin<"__nvvm_mul_rz_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rz_f : ClangBuiltin<"__nvvm_mul_rz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rm_ftz_f : ClangBuiltin<"__nvvm_mul_rm_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rm_f : ClangBuiltin<"__nvvm_mul_rm_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rp_ftz_f : ClangBuiltin<"__nvvm_mul_rp_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rp_f : ClangBuiltin<"__nvvm_mul_rp_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

  def int_nvvm_mul_rn_d : ClangBuiltin<"__nvvm_mul_rn_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rz_d : ClangBuiltin<"__nvvm_mul_rz_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rm_d : ClangBuiltin<"__nvvm_mul_rm_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul_rp_d : ClangBuiltin<"__nvvm_mul_rp_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

  def int_nvvm_mul24_i : ClangBuiltin<"__nvvm_mul24_i">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_mul24_ui : ClangBuiltin<"__nvvm_mul24_ui">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

//
// Div
//

  def int_nvvm_div_approx_ftz_f : ClangBuiltin<"__nvvm_div_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;
  def int_nvvm_div_approx_f : ClangBuiltin<"__nvvm_div_approx_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;

  def int_nvvm_div_rn_ftz_f : ClangBuiltin<"__nvvm_div_rn_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;
  def int_nvvm_div_rn_f : ClangBuiltin<"__nvvm_div_rn_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;

  def int_nvvm_div_rz_ftz_f : ClangBuiltin<"__nvvm_div_rz_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;
  def int_nvvm_div_rz_f : ClangBuiltin<"__nvvm_div_rz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;

  def int_nvvm_div_rm_ftz_f : ClangBuiltin<"__nvvm_div_rm_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;
  def int_nvvm_div_rm_f : ClangBuiltin<"__nvvm_div_rm_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;

  def int_nvvm_div_rp_ftz_f : ClangBuiltin<"__nvvm_div_rp_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;
  def int_nvvm_div_rp_f : ClangBuiltin<"__nvvm_div_rp_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem]>;

  def int_nvvm_div_rn_d : ClangBuiltin<"__nvvm_div_rn_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem]>;
  def int_nvvm_div_rz_d : ClangBuiltin<"__nvvm_div_rz_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem]>;
  def int_nvvm_div_rm_d : ClangBuiltin<"__nvvm_div_rm_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem]>;
  def int_nvvm_div_rp_d : ClangBuiltin<"__nvvm_div_rp_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem]>;

//
// Sad
//

  def int_nvvm_sad_s : ClangBuiltin<"__nvvm_sad_s">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
        [IntrNoMem, Commutative, IntrSpeculatable]>;
  def int_nvvm_sad_us : ClangBuiltin<"__nvvm_sad_us">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
        [IntrNoMem, Commutative, IntrSpeculatable]>;

  def int_nvvm_sad_i : ClangBuiltin<"__nvvm_sad_i">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, Commutative, IntrSpeculatable]>;
  def int_nvvm_sad_ui : ClangBuiltin<"__nvvm_sad_ui">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, Commutative, IntrSpeculatable]>;

  def int_nvvm_sad_ll : ClangBuiltin<"__nvvm_sad_ll">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty],
        [IntrNoMem, Commutative, IntrSpeculatable]>;
  def int_nvvm_sad_ull : ClangBuiltin<"__nvvm_sad_ull">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty],
        [IntrNoMem, Commutative, IntrSpeculatable]>;


//
// Floor  Ceil
//

  def int_nvvm_floor_ftz_f : ClangBuiltin<"__nvvm_floor_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_floor_f : ClangBuiltin<"__nvvm_floor_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_floor_d : ClangBuiltin<"__nvvm_floor_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_ceil_ftz_f : ClangBuiltin<"__nvvm_ceil_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ceil_f : ClangBuiltin<"__nvvm_ceil_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ceil_d : ClangBuiltin<"__nvvm_ceil_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

//
// Abs
//

  def int_nvvm_fabs_ftz_f : ClangBuiltin<"__nvvm_fabs_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_fabs_f : ClangBuiltin<"__nvvm_fabs_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_fabs_d : ClangBuiltin<"__nvvm_fabs_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

//
// Abs, Neg bf16, bf16x2
//

  foreach unary = ["abs", "neg"] in {
    def int_nvvm_ # unary # _bf16 :
      ClangBuiltin<!strconcat("__nvvm_", unary, "_bf16")>,
      DefaultAttrsIntrinsic<[llvm_bfloat_ty], [llvm_bfloat_ty], [IntrNoMem]>;
    def int_nvvm_ # unary # _bf16x2 :
      ClangBuiltin<!strconcat("__nvvm_", unary, "_bf16x2")>,
      DefaultAttrsIntrinsic<[llvm_v2bf16_ty], [llvm_v2bf16_ty], [IntrNoMem]>;
  }

//
// Round
//

  def int_nvvm_round_ftz_f : ClangBuiltin<"__nvvm_round_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_round_f : ClangBuiltin<"__nvvm_round_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_round_d : ClangBuiltin<"__nvvm_round_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

//
// Trunc
//

  def int_nvvm_trunc_ftz_f : ClangBuiltin<"__nvvm_trunc_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_trunc_f : ClangBuiltin<"__nvvm_trunc_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_trunc_d : ClangBuiltin<"__nvvm_trunc_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

//
// Saturate
//

  def int_nvvm_saturate_ftz_f : ClangBuiltin<"__nvvm_saturate_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_saturate_f : ClangBuiltin<"__nvvm_saturate_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_saturate_d : ClangBuiltin<"__nvvm_saturate_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

//
// Exp2  Log2
//

  def int_nvvm_ex2_approx_ftz_f : ClangBuiltin<"__nvvm_ex2_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_ex2_approx_f : ClangBuiltin<"__nvvm_ex2_approx_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_ex2_approx_d : ClangBuiltin<"__nvvm_ex2_approx_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_ex2_approx_f16 :
      DefaultAttrsIntrinsic<[llvm_half_ty], [llvm_half_ty], [IntrNoMem]>;
  def int_nvvm_ex2_approx_f16x2 :
      DefaultAttrsIntrinsic<[llvm_v2f16_ty], [llvm_v2f16_ty], [IntrNoMem]>;

  def int_nvvm_lg2_approx_ftz_f : ClangBuiltin<"__nvvm_lg2_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_lg2_approx_f : ClangBuiltin<"__nvvm_lg2_approx_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_lg2_approx_d : ClangBuiltin<"__nvvm_lg2_approx_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;

//
// Sin  Cos
//

  def int_nvvm_sin_approx_ftz_f : ClangBuiltin<"__nvvm_sin_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sin_approx_f : ClangBuiltin<"__nvvm_sin_approx_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;

  def int_nvvm_cos_approx_ftz_f : ClangBuiltin<"__nvvm_cos_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_cos_approx_f : ClangBuiltin<"__nvvm_cos_approx_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;

//
// Fma
//

  foreach variant = ["_rn_f16", "_rn_ftz_f16", "_rn_sat_f16",
    "_rn_ftz_sat_f16", "_rn_relu_f16", "_rn_ftz_relu_f16"] in {
    def int_nvvm_fma # variant : DefaultAttrsIntrinsic<[llvm_half_ty],
      [llvm_half_ty, llvm_half_ty, llvm_half_ty],
      [IntrNoMem, IntrSpeculatable]>;
  }

  foreach variant = ["_rn_f16x2", "_rn_ftz_f16x2", "_rn_sat_f16x2",
    "_rn_ftz_sat_f16x2", "_rn_relu_f16x2", "_rn_ftz_relu_f16x2"] in {
    def int_nvvm_fma # variant : DefaultAttrsIntrinsic<[llvm_v2f16_ty],
      [llvm_v2f16_ty, llvm_v2f16_ty, llvm_v2f16_ty],
      [IntrNoMem, IntrSpeculatable]>;
  }

  foreach variant = ["_rn_bf16", "_rn_ftz_bf16", "_rn_sat_bf16",
    "_rn_ftz_sat_bf16", "_rn_relu_bf16", "_rn_ftz_relu_bf16"] in {
    def int_nvvm_fma # variant : ClangBuiltin<!strconcat("__nvvm_fma", variant)>,
      DefaultAttrsIntrinsic<[llvm_bfloat_ty],
        [llvm_bfloat_ty, llvm_bfloat_ty, llvm_bfloat_ty],
        [IntrNoMem, IntrSpeculatable]>;
  }

  foreach variant = ["_rn_bf16x2", "_rn_ftz_bf16x2", "_rn_sat_bf16x2",
    "_rn_ftz_sat_bf16x2", "_rn_relu_bf16x2", "_rn_ftz_relu_bf16x2"] in {
    def int_nvvm_fma # variant : ClangBuiltin<!strconcat("__nvvm_fma", variant)>,
      DefaultAttrsIntrinsic<[llvm_v2bf16_ty],
        [llvm_v2bf16_ty, llvm_v2bf16_ty, llvm_v2bf16_ty],
        [IntrNoMem, IntrSpeculatable]>;
  }

  foreach variant = ["_rn_ftz_f", "_rn_f", "_rz_ftz_f", "_rz_f", "_rm_ftz_f",
    "_rm_f", "_rp_ftz_f", "_rp_f"] in {
    def int_nvvm_fma # variant : ClangBuiltin<!strconcat("__nvvm_fma", variant)>,
      DefaultAttrsIntrinsic<[llvm_float_ty],
        [llvm_float_ty, llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable]>;
  }

  foreach variant = ["_rn_d", "_rz_d", "_rm_d", "_rp_d"] in {
    def int_nvvm_fma # variant : ClangBuiltin<!strconcat("__nvvm_fma", variant)>,
      DefaultAttrsIntrinsic<[llvm_double_ty],
        [llvm_double_ty, llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable]>;
  }

//
// Rcp
//

  def int_nvvm_rcp_rn_ftz_f : ClangBuiltin<"__nvvm_rcp_rn_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rn_f : ClangBuiltin<"__nvvm_rcp_rn_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rz_ftz_f : ClangBuiltin<"__nvvm_rcp_rz_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rz_f : ClangBuiltin<"__nvvm_rcp_rz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rm_ftz_f : ClangBuiltin<"__nvvm_rcp_rm_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rm_f : ClangBuiltin<"__nvvm_rcp_rm_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rp_ftz_f : ClangBuiltin<"__nvvm_rcp_rp_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rp_f : ClangBuiltin<"__nvvm_rcp_rp_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;

  def int_nvvm_rcp_rn_d : ClangBuiltin<"__nvvm_rcp_rn_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rz_d : ClangBuiltin<"__nvvm_rcp_rz_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rm_d : ClangBuiltin<"__nvvm_rcp_rm_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_rcp_rp_d : ClangBuiltin<"__nvvm_rcp_rp_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;

  def int_nvvm_rcp_approx_ftz_f : ClangBuiltin<"__nvvm_rcp_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rcp_approx_ftz_d : ClangBuiltin<"__nvvm_rcp_approx_ftz_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;

//
// Sqrt
//

  def int_nvvm_sqrt_f : ClangBuiltin<"__nvvm_sqrt_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rn_ftz_f : ClangBuiltin<"__nvvm_sqrt_rn_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rn_f : ClangBuiltin<"__nvvm_sqrt_rn_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rz_ftz_f : ClangBuiltin<"__nvvm_sqrt_rz_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rz_f : ClangBuiltin<"__nvvm_sqrt_rz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rm_ftz_f : ClangBuiltin<"__nvvm_sqrt_rm_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rm_f : ClangBuiltin<"__nvvm_sqrt_rm_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rp_ftz_f : ClangBuiltin<"__nvvm_sqrt_rp_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rp_f : ClangBuiltin<"__nvvm_sqrt_rp_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_approx_ftz_f : ClangBuiltin<"__nvvm_sqrt_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_approx_f : ClangBuiltin<"__nvvm_sqrt_approx_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;

  def int_nvvm_sqrt_rn_d : ClangBuiltin<"__nvvm_sqrt_rn_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rz_d : ClangBuiltin<"__nvvm_sqrt_rz_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rm_d : ClangBuiltin<"__nvvm_sqrt_rm_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_sqrt_rp_d : ClangBuiltin<"__nvvm_sqrt_rp_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;

//
// Rsqrt
//

  def int_nvvm_rsqrt_approx_ftz_f : ClangBuiltin<"__nvvm_rsqrt_approx_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rsqrt_approx_ftz_d : ClangBuiltin<"__nvvm_rsqrt_approx_ftz_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_nvvm_rsqrt_approx_f : ClangBuiltin<"__nvvm_rsqrt_approx_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_nvvm_rsqrt_approx_d : ClangBuiltin<"__nvvm_rsqrt_approx_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;

//
// Add
//

  def int_nvvm_add_rn_ftz_f : ClangBuiltin<"__nvvm_add_rn_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rn_f : ClangBuiltin<"__nvvm_add_rn_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rz_ftz_f : ClangBuiltin<"__nvvm_add_rz_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rz_f : ClangBuiltin<"__nvvm_add_rz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rm_ftz_f : ClangBuiltin<"__nvvm_add_rm_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rm_f : ClangBuiltin<"__nvvm_add_rm_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rp_ftz_f : ClangBuiltin<"__nvvm_add_rp_ftz_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rp_f : ClangBuiltin<"__nvvm_add_rp_f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

  def int_nvvm_add_rn_d : ClangBuiltin<"__nvvm_add_rn_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rz_d : ClangBuiltin<"__nvvm_add_rz_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rm_d : ClangBuiltin<"__nvvm_add_rm_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;
  def int_nvvm_add_rp_d : ClangBuiltin<"__nvvm_add_rp_d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

//
// Convert
//

  def int_nvvm_d2f_rn_ftz : ClangBuiltin<"__nvvm_d2f_rn_ftz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2f_rn : ClangBuiltin<"__nvvm_d2f_rn">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2f_rz_ftz : ClangBuiltin<"__nvvm_d2f_rz_ftz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2f_rz : ClangBuiltin<"__nvvm_d2f_rz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2f_rm_ftz : ClangBuiltin<"__nvvm_d2f_rm_ftz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2f_rm : ClangBuiltin<"__nvvm_d2f_rm">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2f_rp_ftz : ClangBuiltin<"__nvvm_d2f_rp_ftz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2f_rp : ClangBuiltin<"__nvvm_d2f_rp">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_d2i_rn : ClangBuiltin<"__nvvm_d2i_rn">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2i_rz : ClangBuiltin<"__nvvm_d2i_rz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2i_rm : ClangBuiltin<"__nvvm_d2i_rm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2i_rp : ClangBuiltin<"__nvvm_d2i_rp">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_d2ui_rn : ClangBuiltin<"__nvvm_d2ui_rn">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ui_rz : ClangBuiltin<"__nvvm_d2ui_rz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ui_rm : ClangBuiltin<"__nvvm_d2ui_rm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ui_rp : ClangBuiltin<"__nvvm_d2ui_rp">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_i2d_rn : ClangBuiltin<"__nvvm_i2d_rn">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_i2d_rz : ClangBuiltin<"__nvvm_i2d_rz">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_i2d_rm : ClangBuiltin<"__nvvm_i2d_rm">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_i2d_rp : ClangBuiltin<"__nvvm_i2d_rp">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_ui2d_rn : ClangBuiltin<"__nvvm_ui2d_rn">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ui2d_rz : ClangBuiltin<"__nvvm_ui2d_rz">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ui2d_rm : ClangBuiltin<"__nvvm_ui2d_rm">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ui2d_rp : ClangBuiltin<"__nvvm_ui2d_rp">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_f2i_rn_ftz : ClangBuiltin<"__nvvm_f2i_rn_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2i_rn : ClangBuiltin<"__nvvm_f2i_rn">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2i_rz_ftz : ClangBuiltin<"__nvvm_f2i_rz_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2i_rz : ClangBuiltin<"__nvvm_f2i_rz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2i_rm_ftz : ClangBuiltin<"__nvvm_f2i_rm_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2i_rm : ClangBuiltin<"__nvvm_f2i_rm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2i_rp_ftz : ClangBuiltin<"__nvvm_f2i_rp_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2i_rp : ClangBuiltin<"__nvvm_f2i_rp">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_f2ui_rn_ftz : ClangBuiltin<"__nvvm_f2ui_rn_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ui_rn : ClangBuiltin<"__nvvm_f2ui_rn">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ui_rz_ftz : ClangBuiltin<"__nvvm_f2ui_rz_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ui_rz : ClangBuiltin<"__nvvm_f2ui_rz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ui_rm_ftz : ClangBuiltin<"__nvvm_f2ui_rm_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ui_rm : ClangBuiltin<"__nvvm_f2ui_rm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ui_rp_ftz : ClangBuiltin<"__nvvm_f2ui_rp_ftz">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ui_rp : ClangBuiltin<"__nvvm_f2ui_rp">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_i2f_rn : ClangBuiltin<"__nvvm_i2f_rn">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_i2f_rz : ClangBuiltin<"__nvvm_i2f_rz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_i2f_rm : ClangBuiltin<"__nvvm_i2f_rm">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_i2f_rp : ClangBuiltin<"__nvvm_i2f_rp">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_ui2f_rn : ClangBuiltin<"__nvvm_ui2f_rn">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ui2f_rz : ClangBuiltin<"__nvvm_ui2f_rz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ui2f_rm : ClangBuiltin<"__nvvm_ui2f_rm">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ui2f_rp : ClangBuiltin<"__nvvm_ui2f_rp">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_lohi_i2d : ClangBuiltin<"__nvvm_lohi_i2d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty, llvm_i32_ty],
        [IntrNoMem, IntrSpeculatable, Commutative]>;

  def int_nvvm_d2i_lo : ClangBuiltin<"__nvvm_d2i_lo">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2i_hi : ClangBuiltin<"__nvvm_d2i_hi">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_f2ll_rn_ftz : ClangBuiltin<"__nvvm_f2ll_rn_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ll_rn : ClangBuiltin<"__nvvm_f2ll_rn">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ll_rz_ftz : ClangBuiltin<"__nvvm_f2ll_rz_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ll_rz : ClangBuiltin<"__nvvm_f2ll_rz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ll_rm_ftz : ClangBuiltin<"__nvvm_f2ll_rm_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ll_rm : ClangBuiltin<"__nvvm_f2ll_rm">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ll_rp_ftz : ClangBuiltin<"__nvvm_f2ll_rp_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ll_rp : ClangBuiltin<"__nvvm_f2ll_rp">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_f2ull_rn_ftz : ClangBuiltin<"__nvvm_f2ull_rn_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ull_rn : ClangBuiltin<"__nvvm_f2ull_rn">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ull_rz_ftz : ClangBuiltin<"__nvvm_f2ull_rz_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ull_rz : ClangBuiltin<"__nvvm_f2ull_rz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ull_rm_ftz : ClangBuiltin<"__nvvm_f2ull_rm_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ull_rm : ClangBuiltin<"__nvvm_f2ull_rm">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ull_rp_ftz : ClangBuiltin<"__nvvm_f2ull_rp_ftz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2ull_rp : ClangBuiltin<"__nvvm_f2ull_rp">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_d2ll_rn : ClangBuiltin<"__nvvm_d2ll_rn">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ll_rz : ClangBuiltin<"__nvvm_d2ll_rz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ll_rm : ClangBuiltin<"__nvvm_d2ll_rm">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ll_rp : ClangBuiltin<"__nvvm_d2ll_rp">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_d2ull_rn : ClangBuiltin<"__nvvm_d2ull_rn">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ull_rz : ClangBuiltin<"__nvvm_d2ull_rz">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ull_rm : ClangBuiltin<"__nvvm_d2ull_rm">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_d2ull_rp : ClangBuiltin<"__nvvm_d2ull_rp">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_ll2f_rn : ClangBuiltin<"__nvvm_ll2f_rn">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ll2f_rz : ClangBuiltin<"__nvvm_ll2f_rz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ll2f_rm : ClangBuiltin<"__nvvm_ll2f_rm">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ll2f_rp : ClangBuiltin<"__nvvm_ll2f_rp">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2f_rn : ClangBuiltin<"__nvvm_ull2f_rn">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2f_rz : ClangBuiltin<"__nvvm_ull2f_rz">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2f_rm : ClangBuiltin<"__nvvm_ull2f_rm">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2f_rp : ClangBuiltin<"__nvvm_ull2f_rp">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_ll2d_rn : ClangBuiltin<"__nvvm_ll2d_rn">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ll2d_rz : ClangBuiltin<"__nvvm_ll2d_rz">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ll2d_rm : ClangBuiltin<"__nvvm_ll2d_rm">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ll2d_rp : ClangBuiltin<"__nvvm_ll2d_rp">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2d_rn : ClangBuiltin<"__nvvm_ull2d_rn">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2d_rz : ClangBuiltin<"__nvvm_ull2d_rz">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2d_rm : ClangBuiltin<"__nvvm_ull2d_rm">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_ull2d_rp : ClangBuiltin<"__nvvm_ull2d_rp">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_f2h_rn_ftz : ClangBuiltin<"__nvvm_f2h_rn_ftz">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_f2h_rn : ClangBuiltin<"__nvvm_f2h_rn">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_bf2h_rn_ftz : ClangBuiltin<"__nvvm_bf2h_rn_ftz">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_bfloat_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_bf2h_rn : ClangBuiltin<"__nvvm_bf2h_rn">,
      DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_bfloat_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_ff2bf16x2_rn : ClangBuiltin<"__nvvm_ff2bf16x2_rn">,
       Intrinsic<[llvm_v2bf16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_ff2bf16x2_rn_relu : ClangBuiltin<"__nvvm_ff2bf16x2_rn_relu">,
      Intrinsic<[llvm_v2bf16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_ff2bf16x2_rz : ClangBuiltin<"__nvvm_ff2bf16x2_rz">,
      Intrinsic<[llvm_v2bf16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_ff2bf16x2_rz_relu : ClangBuiltin<"__nvvm_ff2bf16x2_rz_relu">,
      Intrinsic<[llvm_v2bf16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem]>;

  def int_nvvm_ff2f16x2_rn : ClangBuiltin<"__nvvm_ff2f16x2_rn">,
      Intrinsic<[llvm_v2f16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_ff2f16x2_rn_relu : ClangBuiltin<"__nvvm_ff2f16x2_rn_relu">,
      Intrinsic<[llvm_v2f16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_ff2f16x2_rz : ClangBuiltin<"__nvvm_ff2f16x2_rz">,
      Intrinsic<[llvm_v2f16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_ff2f16x2_rz_relu : ClangBuiltin<"__nvvm_ff2f16x2_rz_relu">,
      Intrinsic<[llvm_v2f16_ty], [llvm_float_ty, llvm_float_ty], [IntrNoMem, IntrNoCallback]>;

  def int_nvvm_f2bf16_rn : ClangBuiltin<"__nvvm_f2bf16_rn">,
      Intrinsic<[llvm_bfloat_ty], [llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_f2bf16_rn_relu : ClangBuiltin<"__nvvm_f2bf16_rn_relu">,
      Intrinsic<[llvm_bfloat_ty], [llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_f2bf16_rz : ClangBuiltin<"__nvvm_f2bf16_rz">,
      Intrinsic<[llvm_bfloat_ty], [llvm_float_ty], [IntrNoMem, IntrNoCallback]>;
  def int_nvvm_f2bf16_rz_relu : ClangBuiltin<"__nvvm_f2bf16_rz_relu">,
       Intrinsic<[llvm_bfloat_ty], [llvm_float_ty], [IntrNoMem, IntrNoCallback]>;

  def int_nvvm_f2tf32_rna : ClangBuiltin<"__nvvm_f2tf32_rna">,
      Intrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrNoCallback]>;

//
// Bitcast
//

  def int_nvvm_bitcast_f2i : ClangBuiltin<"__nvvm_bitcast_f2i">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_bitcast_i2f : ClangBuiltin<"__nvvm_bitcast_i2f">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_i32_ty], [IntrNoMem, IntrSpeculatable]>;

  def int_nvvm_bitcast_ll2d : ClangBuiltin<"__nvvm_bitcast_ll2d">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i64_ty], [IntrNoMem, IntrSpeculatable]>;
  def int_nvvm_bitcast_d2ll : ClangBuiltin<"__nvvm_bitcast_d2ll">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem, IntrSpeculatable]>;

// FNS

  def int_nvvm_fns : ClangBuiltin<"__nvvm_fns">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                [IntrNoMem]>;

// Atomics not available as llvm intrinsics.
  def int_nvvm_atomic_load_inc_32 : Intrinsic<[llvm_i32_ty],
          [llvm_anyptr_ty, llvm_i32_ty],
                                      [IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>]>;
  def int_nvvm_atomic_load_dec_32 : Intrinsic<[llvm_i32_ty],
          [llvm_anyptr_ty, llvm_i32_ty],
                                      [IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>]>;

  class SCOPED_ATOMIC2_impl<LLVMType elty>
        : Intrinsic<[elty],
          [llvm_anyptr_ty, LLVMMatchType<0>],
          [IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>]>;
  class SCOPED_ATOMIC3_impl<LLVMType elty>
        : Intrinsic<[elty],
          [llvm_anyptr_ty, LLVMMatchType<0>,
           LLVMMatchType<0>],
          [IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>]>;

  multiclass PTXAtomicWithScope2<LLVMType elty> {
    def _cta : SCOPED_ATOMIC2_impl<elty>;
    def _sys : SCOPED_ATOMIC2_impl<elty>;
  }
  multiclass PTXAtomicWithScope3<LLVMType elty> {
    def _cta : SCOPED_ATOMIC3_impl<elty>;
    def _sys : SCOPED_ATOMIC3_impl<elty>;
  }
  multiclass PTXAtomicWithScope2_fi {
    defm _f: PTXAtomicWithScope2<llvm_anyfloat_ty>;
    defm _i: PTXAtomicWithScope2<llvm_anyint_ty>;
  }
  defm int_nvvm_atomic_add_gen   : PTXAtomicWithScope2_fi;
  defm int_nvvm_atomic_inc_gen_i : PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_dec_gen_i : PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_exch_gen_i: PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_xor_gen_i : PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_max_gen_i : PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_min_gen_i : PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_or_gen_i  : PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_and_gen_i : PTXAtomicWithScope2<llvm_anyint_ty>;
  defm int_nvvm_atomic_cas_gen_i : PTXAtomicWithScope3<llvm_anyint_ty>;

// Bar.Sync

  // The builtin for "bar.sync 0" is called __syncthreads.  Unlike most of the
  // intrinsics in this file, this one is a user-facing API.
  def int_nvvm_barrier0 : ClangBuiltin<"__syncthreads">,
      Intrinsic<[], [], [IntrConvergent, IntrNoCallback]>;
  // Synchronize all threads in the CTA at barrier 'n'.
  def int_nvvm_barrier_n : ClangBuiltin<"__nvvm_bar_n">,
      Intrinsic<[], [llvm_i32_ty], [IntrConvergent, IntrNoCallback]>;
  // Synchronize 'm', a multiple of warp size, (arg 2) threads in
  // the CTA at barrier 'n' (arg 1).
  def int_nvvm_barrier : ClangBuiltin<"__nvvm_bar">,
      Intrinsic<[], [llvm_i32_ty, llvm_i32_ty], [IntrConvergent, IntrNoCallback]>;
  def int_nvvm_barrier0_popc : ClangBuiltin<"__nvvm_bar0_popc">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrConvergent, IntrNoCallback]>;
  def int_nvvm_barrier0_and : ClangBuiltin<"__nvvm_bar0_and">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrConvergent, IntrNoCallback]>;
  def int_nvvm_barrier0_or : ClangBuiltin<"__nvvm_bar0_or">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrConvergent, IntrNoCallback]>;

  def int_nvvm_bar_sync :
      Intrinsic<[], [llvm_i32_ty], [IntrConvergent, IntrNoCallback]>,
      ClangBuiltin<"__nvvm_bar_sync">;
  def int_nvvm_bar_warp_sync :
      Intrinsic<[], [llvm_i32_ty], [IntrConvergent, IntrNoCallback]>,
      ClangBuiltin<"__nvvm_bar_warp_sync">;

  // barrier.sync id[, cnt]
  def int_nvvm_barrier_sync :
      Intrinsic<[], [llvm_i32_ty], [IntrConvergent, IntrNoCallback]>,
      ClangBuiltin<"__nvvm_barrier_sync">;
  def int_nvvm_barrier_sync_cnt :
      Intrinsic<[], [llvm_i32_ty, llvm_i32_ty], [IntrConvergent, IntrNoCallback]>,
      ClangBuiltin<"__nvvm_barrier_sync_cnt">;

  // barrier.cluster.[wait, arrive, arrive.relaxed]
  def int_nvvm_barrier_cluster_arrive :
      Intrinsic<[], [], [IntrConvergent, IntrNoCallback]>;
  def int_nvvm_barrier_cluster_arrive_relaxed :
      Intrinsic<[], [], [IntrConvergent, IntrNoCallback]>;
  def int_nvvm_barrier_cluster_wait :
      Intrinsic<[], [], [IntrConvergent, IntrNoCallback]>;

  // 'aligned' versions of the above barrier.cluster.* intrinsics
  def int_nvvm_barrier_cluster_arrive_aligned :
      Intrinsic<[], [], [IntrConvergent, IntrNoCallback]>;
  def int_nvvm_barrier_cluster_arrive_relaxed_aligned :
      Intrinsic<[], [], [IntrConvergent, IntrNoCallback]>;
  def int_nvvm_barrier_cluster_wait_aligned :
      Intrinsic<[], [], [IntrConvergent, IntrNoCallback]>;

  // Membar
  def int_nvvm_membar_cta : ClangBuiltin<"__nvvm_membar_cta">,
      Intrinsic<[], [], [IntrNoCallback]>;
  def int_nvvm_membar_gl : ClangBuiltin<"__nvvm_membar_gl">,
      Intrinsic<[], [], [IntrNoCallback]>;
  def int_nvvm_membar_sys : ClangBuiltin<"__nvvm_membar_sys">,
      Intrinsic<[], [], [IntrNoCallback]>;
  def int_nvvm_fence_sc_cluster:
      Intrinsic<[], [], [IntrNoCallback]>;

// Async Copy
def int_nvvm_cp_async_mbarrier_arrive :
    ClangBuiltin<"__nvvm_cp_async_mbarrier_arrive">,
    Intrinsic<[],[llvm_ptr_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_cp_async_mbarrier_arrive_shared :
    ClangBuiltin<"__nvvm_cp_async_mbarrier_arrive_shared">,
    Intrinsic<[],[llvm_shared_ptr_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_cp_async_mbarrier_arrive_noinc :
    ClangBuiltin<"__nvvm_cp_async_mbarrier_arrive_noinc">,
    Intrinsic<[],[llvm_ptr_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_cp_async_mbarrier_arrive_noinc_shared :
    ClangBuiltin<"__nvvm_cp_async_mbarrier_arrive_noinc_shared">,
    Intrinsic<[],[llvm_shared_ptr_ty],[IntrConvergent, IntrNoCallback]>;

multiclass CP_ASYNC_SHARED_GLOBAL<string n, string cc> {
  def NAME: Intrinsic<[],[llvm_shared_ptr_ty, llvm_global_ptr_ty],
        [IntrArgMemOnly, IntrNoCallback, NoAlias<ArgIndex<0>>, NoAlias<ArgIndex<1>>,
        WriteOnly<ArgIndex<0>>, ReadOnly<ArgIndex<1>>],
        "llvm.nvvm.cp.async." # cc # ".shared.global." # n>;
  def _s: Intrinsic<[],[llvm_shared_ptr_ty, llvm_global_ptr_ty, llvm_i32_ty],
        [IntrArgMemOnly, IntrNoCallback, NoAlias<ArgIndex<0>>, NoAlias<ArgIndex<1>>,
        WriteOnly<ArgIndex<0>>, ReadOnly<ArgIndex<1>>],
        "llvm.nvvm.cp.async." # cc # ".shared.global." # n # ".s">;
}

defm int_nvvm_cp_async_ca_shared_global_4 : CP_ASYNC_SHARED_GLOBAL<"4", "ca">;
defm int_nvvm_cp_async_ca_shared_global_8 : CP_ASYNC_SHARED_GLOBAL<"8", "ca">;
defm int_nvvm_cp_async_ca_shared_global_16 : CP_ASYNC_SHARED_GLOBAL<"16", "ca">;
defm int_nvvm_cp_async_cg_shared_global_16 : CP_ASYNC_SHARED_GLOBAL<"16", "cg">;

def int_nvvm_cp_async_commit_group :
    ClangBuiltin<"__nvvm_cp_async_commit_group">,
    Intrinsic<[],[],[]>;

def int_nvvm_cp_async_wait_group :
    ClangBuiltin<"__nvvm_cp_async_wait_group">,
    Intrinsic<[],[llvm_i32_ty],[ImmArg<ArgIndex<0>>]>;

def int_nvvm_cp_async_wait_all :
    ClangBuiltin<"__nvvm_cp_async_wait_all">,
    Intrinsic<[],[],[]>;

// cp.async.bulk variants of the commit/wait group
def int_nvvm_cp_async_bulk_commit_group :
    Intrinsic<[],[],[]>;

def int_nvvm_cp_async_bulk_wait_group :
    Intrinsic<[],[llvm_i32_ty],[ImmArg<ArgIndex<0>>]>;

def int_nvvm_cp_async_bulk_wait_group_read :
    Intrinsic<[],[llvm_i32_ty],[ImmArg<ArgIndex<0>>]>;

// mbarrier
def int_nvvm_mbarrier_init : ClangBuiltin<"__nvvm_mbarrier_init">,
    Intrinsic<[],[llvm_ptr_ty, llvm_i32_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_init_shared :
    ClangBuiltin<"__nvvm_mbarrier_init_shared">,
    Intrinsic<[],[llvm_shared_ptr_ty, llvm_i32_ty],[IntrConvergent, IntrNoCallback]>;

def int_nvvm_mbarrier_inval : ClangBuiltin<"__nvvm_mbarrier_inval">,
    Intrinsic<[],[llvm_ptr_ty],
    [IntrConvergent, IntrWriteMem, IntrArgMemOnly, IntrNoCallback,
    WriteOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>]>;
def int_nvvm_mbarrier_inval_shared :
    ClangBuiltin<"__nvvm_mbarrier_inval_shared">,
    Intrinsic<[],[llvm_shared_ptr_ty],
    [IntrConvergent, IntrWriteMem, IntrArgMemOnly, IntrNoCallback,
    WriteOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>]>;

def int_nvvm_mbarrier_arrive : ClangBuiltin<"__nvvm_mbarrier_arrive">,
    Intrinsic<[llvm_i64_ty],[llvm_ptr_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_arrive_shared :
    ClangBuiltin<"__nvvm_mbarrier_arrive_shared">,
    Intrinsic<[llvm_i64_ty],[llvm_shared_ptr_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_arrive_noComplete :
    ClangBuiltin<"__nvvm_mbarrier_arrive_noComplete">,
    Intrinsic<[llvm_i64_ty],[llvm_ptr_ty, llvm_i32_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_arrive_noComplete_shared :
    ClangBuiltin<"__nvvm_mbarrier_arrive_noComplete_shared">,
    Intrinsic<[llvm_i64_ty],[llvm_shared_ptr_ty,
    llvm_i32_ty],[IntrConvergent, IntrNoCallback]>;

def int_nvvm_mbarrier_arrive_drop :
    ClangBuiltin<"__nvvm_mbarrier_arrive_drop">,
    Intrinsic<[llvm_i64_ty],[llvm_ptr_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_arrive_drop_shared :
    ClangBuiltin<"__nvvm_mbarrier_arrive_drop_shared">,
    Intrinsic<[llvm_i64_ty],[llvm_shared_ptr_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_arrive_drop_noComplete :
    ClangBuiltin<"__nvvm_mbarrier_arrive_drop_noComplete">,
    Intrinsic<[llvm_i64_ty],[llvm_ptr_ty, llvm_i32_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_arrive_drop_noComplete_shared :
    ClangBuiltin<"__nvvm_mbarrier_arrive_drop_noComplete_shared">,
    Intrinsic<[llvm_i64_ty],[llvm_shared_ptr_ty,
    llvm_i32_ty],[IntrConvergent, IntrNoCallback]>;

def int_nvvm_mbarrier_test_wait :
    ClangBuiltin<"__nvvm_mbarrier_test_wait">,
    Intrinsic<[llvm_i1_ty],[llvm_ptr_ty, llvm_i64_ty],[IntrConvergent, IntrNoCallback]>;
def int_nvvm_mbarrier_test_wait_shared :
    ClangBuiltin<"__nvvm_mbarrier_test_wait_shared">,
    Intrinsic<[llvm_i1_ty],[llvm_shared_ptr_ty, llvm_i64_ty],[IntrConvergent, IntrNoCallback]>;

def int_nvvm_mbarrier_pending_count :
    ClangBuiltin<"__nvvm_mbarrier_pending_count">,
    Intrinsic<[llvm_i32_ty],[llvm_i64_ty],[IntrNoMem, IntrConvergent, IntrNoCallback]>;

// Generated within nvvm. Use for ldu on sm_20 or later.  Second arg is the
// pointer's alignment.
def int_nvvm_ldu_global_i : Intrinsic<[llvm_anyint_ty],
  [llvm_anyptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>],
  "llvm.nvvm.ldu.global.i">;
def int_nvvm_ldu_global_f : Intrinsic<[llvm_anyfloat_ty],
  [llvm_anyptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>],
  "llvm.nvvm.ldu.global.f">;
def int_nvvm_ldu_global_p : Intrinsic<[llvm_anyptr_ty],
  [llvm_anyptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>],
  "llvm.nvvm.ldu.global.p">;

// Generated within nvvm. Use for ldg on sm_35 or later.  Second arg is the
// pointer's alignment.
def int_nvvm_ldg_global_i : Intrinsic<[llvm_anyint_ty],
  [llvm_anyptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>],
  "llvm.nvvm.ldg.global.i">;
def int_nvvm_ldg_global_f : Intrinsic<[llvm_anyfloat_ty],
  [llvm_anyptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>],
  "llvm.nvvm.ldg.global.f">;
def int_nvvm_ldg_global_p : Intrinsic<[llvm_anyptr_ty],
  [llvm_anyptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly, IntrNoCallback, NoCapture<ArgIndex<0>>],
  "llvm.nvvm.ldg.global.p">;

// Use for generic pointers
// - These intrinsics are used to convert address spaces.
// - The input pointer and output pointer must have the same type, except for
//   the address-space. (This restriction is not enforced here as there is
//   currently no way to describe it).
// - This complements the llvm bitcast, which can be used to cast one type
//   of pointer to another type of pointer, while the address space remains
//   the same.
def int_nvvm_ptr_local_to_gen: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.local.to.gen">;
def int_nvvm_ptr_shared_to_gen: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.shared.to.gen">;
def int_nvvm_ptr_global_to_gen: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.global.to.gen">;
def int_nvvm_ptr_constant_to_gen: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.constant.to.gen">;

def int_nvvm_ptr_gen_to_global: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.gen.to.global">;
def int_nvvm_ptr_gen_to_shared: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.gen.to.shared">;
def int_nvvm_ptr_gen_to_local: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.gen.to.local">;
def int_nvvm_ptr_gen_to_constant: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                 [llvm_anyptr_ty], [IntrNoMem, IntrSpeculatable],
                 "llvm.nvvm.ptr.gen.to.constant">;

// Used in nvvm internally to help address space opt and ptx code generation
// This is for params that are passed to kernel functions by pointer by-val.
def int_nvvm_ptr_gen_to_param: Intrinsic<[llvm_anyptr_ty],
                                     [llvm_anyptr_ty],
                                   [IntrNoMem, IntrSpeculatable, IntrNoCallback],
                                   "llvm.nvvm.ptr.gen.to.param">;

// sm70+, PTX7.7+
def int_nvvm_ptr_param_to_gen: DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                                     [llvm_anyptr_ty],
                                   [IntrNoMem, IntrSpeculatable, IntrNoCallback],
                                   "llvm.nvvm.ptr.param.to.gen">;

// Move intrinsics, used in nvvm internally

def int_nvvm_move_i16 : Intrinsic<[llvm_i16_ty], [llvm_i16_ty], [IntrNoMem],
  "llvm.nvvm.move.i16">;
def int_nvvm_move_i32 : Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem],
  "llvm.nvvm.move.i32">;
def int_nvvm_move_i64 : Intrinsic<[llvm_i64_ty], [llvm_i64_ty], [IntrNoMem],
  "llvm.nvvm.move.i64">;
def int_nvvm_move_float : Intrinsic<[llvm_float_ty], [llvm_float_ty],
  [IntrNoMem], "llvm.nvvm.move.float">;
def int_nvvm_move_double : Intrinsic<[llvm_double_ty], [llvm_double_ty],
  [IntrNoMem], "llvm.nvvm.move.double">;
def int_nvvm_move_ptr : Intrinsic<[llvm_anyptr_ty], [llvm_anyptr_ty],
  [IntrNoMem, NoCapture<ArgIndex<0>>], "llvm.nvvm.move.ptr">;


// For getting the handle from a texture or surface variable
def int_nvvm_texsurf_handle
  : Intrinsic<[llvm_i64_ty], [llvm_metadata_ty, llvm_anyptr_ty],
              [IntrNoMem], "llvm.nvvm.texsurf.handle">;
def int_nvvm_texsurf_handle_internal
  : Intrinsic<[llvm_i64_ty], [llvm_anyptr_ty],
              [IntrNoMem], "llvm.nvvm.texsurf.handle.internal">;

/// Error / Warn
def int_nvvm_compiler_error :
    Intrinsic<[], [llvm_anyptr_ty], [], "llvm.nvvm.compiler.error">;
def int_nvvm_compiler_warn :
    Intrinsic<[], [llvm_anyptr_ty], [], "llvm.nvvm.compiler.warn">;

def int_nvvm_reflect :
  Intrinsic<[llvm_i32_ty], [llvm_ptr_ty], [IntrNoMem], "llvm.nvvm.reflect">,
  ClangBuiltin<"__nvvm_reflect">;

// isspacep.{const, global, local, shared}
def int_nvvm_isspacep_const
  : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.isspacep.const">,
    ClangBuiltin<"__nvvm_isspacep_const">;
def int_nvvm_isspacep_global
  : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.isspacep.global">,
    ClangBuiltin<"__nvvm_isspacep_global">;
def int_nvvm_isspacep_local
  : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.isspacep.local">,
    ClangBuiltin<"__nvvm_isspacep_local">;
def int_nvvm_isspacep_shared
  : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.isspacep.shared">,
    ClangBuiltin<"__nvvm_isspacep_shared">;
def int_nvvm_isspacep_shared_cluster
  : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.isspacep.shared.cluster">;

// Environment register read
def int_nvvm_read_ptx_sreg_envreg0
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg0">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg0">;
def int_nvvm_read_ptx_sreg_envreg1
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg1">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg1">;
def int_nvvm_read_ptx_sreg_envreg2
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg2">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg2">;
def int_nvvm_read_ptx_sreg_envreg3
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg3">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg3">;
def int_nvvm_read_ptx_sreg_envreg4
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg4">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg4">;
def int_nvvm_read_ptx_sreg_envreg5
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg5">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg5">;
def int_nvvm_read_ptx_sreg_envreg6
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg6">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg6">;
def int_nvvm_read_ptx_sreg_envreg7
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg7">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg7">;
def int_nvvm_read_ptx_sreg_envreg8
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg8">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg8">;
def int_nvvm_read_ptx_sreg_envreg9
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg9">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg9">;
def int_nvvm_read_ptx_sreg_envreg10
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg10">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg10">;
def int_nvvm_read_ptx_sreg_envreg11
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg11">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg11">;
def int_nvvm_read_ptx_sreg_envreg12
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg12">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg12">;
def int_nvvm_read_ptx_sreg_envreg13
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg13">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg13">;
def int_nvvm_read_ptx_sreg_envreg14
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg14">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg14">;
def int_nvvm_read_ptx_sreg_envreg15
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg15">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg15">;
def int_nvvm_read_ptx_sreg_envreg16
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg16">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg16">;
def int_nvvm_read_ptx_sreg_envreg17
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg17">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg17">;
def int_nvvm_read_ptx_sreg_envreg18
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg18">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg18">;
def int_nvvm_read_ptx_sreg_envreg19
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg19">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg19">;
def int_nvvm_read_ptx_sreg_envreg20
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg20">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg20">;
def int_nvvm_read_ptx_sreg_envreg21
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg21">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg21">;
def int_nvvm_read_ptx_sreg_envreg22
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg22">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg22">;
def int_nvvm_read_ptx_sreg_envreg23
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg23">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg23">;
def int_nvvm_read_ptx_sreg_envreg24
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg24">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg24">;
def int_nvvm_read_ptx_sreg_envreg25
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg25">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg25">;
def int_nvvm_read_ptx_sreg_envreg26
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg26">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg26">;
def int_nvvm_read_ptx_sreg_envreg27
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg27">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg27">;
def int_nvvm_read_ptx_sreg_envreg28
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg28">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg28">;
def int_nvvm_read_ptx_sreg_envreg29
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg29">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg29">;
def int_nvvm_read_ptx_sreg_envreg30
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg30">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg30">;
def int_nvvm_read_ptx_sreg_envreg31
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.read.ptx.sreg.envreg31">,
    ClangBuiltin<"__nvvm_read_ptx_sreg_envreg31">;


// Texture Fetch
// texmode_independent
def int_nvvm_tex_1d_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.1d.v4f32.s32">;
def int_nvvm_tex_1d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.v4f32.f32">;
def int_nvvm_tex_1d_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.level.v4f32.f32">;
def int_nvvm_tex_1d_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.1d.grad.v4f32.f32">;
def int_nvvm_tex_1d_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.1d.v4s32.s32">;
def int_nvvm_tex_1d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.v4s32.f32">;
def int_nvvm_tex_1d_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.level.v4s32.f32">;
def int_nvvm_tex_1d_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.1d.grad.v4s32.f32">;
def int_nvvm_tex_1d_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.1d.v4u32.s32">;
def int_nvvm_tex_1d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.v4u32.f32">;
def int_nvvm_tex_1d_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.level.v4u32.f32">;
def int_nvvm_tex_1d_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.1d.grad.v4u32.f32">;

def int_nvvm_tex_1d_array_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.1d.array.v4f32.s32">;
def int_nvvm_tex_1d_array_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.v4f32.f32">;
def int_nvvm_tex_1d_array_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.level.v4f32.f32">;
def int_nvvm_tex_1d_array_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.grad.v4f32.f32">;
def int_nvvm_tex_1d_array_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.1d.array.v4s32.s32">;
def int_nvvm_tex_1d_array_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.v4s32.f32">;
def int_nvvm_tex_1d_array_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.level.v4s32.f32">;
def int_nvvm_tex_1d_array_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.grad.v4s32.f32">;
def int_nvvm_tex_1d_array_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.1d.array.v4u32.s32">;
def int_nvvm_tex_1d_array_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.v4u32.f32">;
def int_nvvm_tex_1d_array_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.level.v4u32.f32">;
def int_nvvm_tex_1d_array_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.1d.array.grad.v4u32.f32">;

def int_nvvm_tex_2d_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.2d.v4f32.s32">;
def int_nvvm_tex_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.v4f32.f32">;
def int_nvvm_tex_2d_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.level.v4f32.f32">;
def int_nvvm_tex_2d_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.grad.v4f32.f32">;
def int_nvvm_tex_2d_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.2d.v4s32.s32">;
def int_nvvm_tex_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.v4s32.f32">;
def int_nvvm_tex_2d_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.level.v4s32.f32">;
def int_nvvm_tex_2d_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.grad.v4s32.f32">;
def int_nvvm_tex_2d_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.2d.v4u32.s32">;
def int_nvvm_tex_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.v4u32.f32">;
def int_nvvm_tex_2d_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.level.v4u32.f32">;
def int_nvvm_tex_2d_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.grad.v4u32.f32">;

def int_nvvm_tex_2d_array_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
               llvm_i32_ty], [],
              "llvm.nvvm.tex.2d.array.v4f32.s32">;
def int_nvvm_tex_2d_array_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.v4f32.f32">;
def int_nvvm_tex_2d_array_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.level.v4f32.f32">;
def int_nvvm_tex_2d_array_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.grad.v4f32.f32">;
def int_nvvm_tex_2d_array_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
               llvm_i32_ty], [],
              "llvm.nvvm.tex.2d.array.v4s32.s32">;
def int_nvvm_tex_2d_array_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.v4s32.f32">;
def int_nvvm_tex_2d_array_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.level.v4s32.f32">;
def int_nvvm_tex_2d_array_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.grad.v4s32.f32">;
def int_nvvm_tex_2d_array_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
               llvm_i32_ty], [],
              "llvm.nvvm.tex.2d.array.v4u32.s32">;
def int_nvvm_tex_2d_array_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.v4u32.f32">;
def int_nvvm_tex_2d_array_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.level.v4u32.f32">;
def int_nvvm_tex_2d_array_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.2d.array.grad.v4u32.f32">;

def int_nvvm_tex_3d_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [], "llvm.nvvm.tex.3d.v4f32.s32">;
def int_nvvm_tex_3d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.3d.v4f32.f32">;
def int_nvvm_tex_3d_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.3d.level.v4f32.f32">;
def int_nvvm_tex_3d_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.3d.grad.v4f32.f32">;
def int_nvvm_tex_3d_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [], "llvm.nvvm.tex.3d.v4s32.s32">;
def int_nvvm_tex_3d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.3d.v4s32.f32">;
def int_nvvm_tex_3d_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.3d.level.v4s32.f32">;
def int_nvvm_tex_3d_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.3d.grad.v4s32.f32">;
def int_nvvm_tex_3d_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [], "llvm.nvvm.tex.3d.v4u32.s32">;
def int_nvvm_tex_3d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.3d.v4u32.f32">;
def int_nvvm_tex_3d_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.3d.level.v4u32.f32">;
def int_nvvm_tex_3d_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.3d.grad.v4u32.f32">;

def int_nvvm_tex_cube_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.v4f32.f32">;
def int_nvvm_tex_cube_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.level.v4f32.f32">;
def int_nvvm_tex_cube_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.v4s32.f32">;
def int_nvvm_tex_cube_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.level.v4s32.f32">;
def int_nvvm_tex_cube_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.v4u32.f32">;
def int_nvvm_tex_cube_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.level.v4u32.f32">;

def int_nvvm_tex_cube_array_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.array.v4f32.f32">;
def int_nvvm_tex_cube_array_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.array.level.v4f32.f32">;
def int_nvvm_tex_cube_array_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.array.v4s32.f32">;
def int_nvvm_tex_cube_array_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.array.level.v4s32.f32">;
def int_nvvm_tex_cube_array_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.array.v4u32.f32">;
def int_nvvm_tex_cube_array_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.cube.array.level.v4u32.f32">;

def int_nvvm_tld4_r_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.r.2d.v4f32.f32">;
def int_nvvm_tld4_g_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.g.2d.v4f32.f32">;
def int_nvvm_tld4_b_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.b.2d.v4f32.f32">;
def int_nvvm_tld4_a_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.a.2d.v4f32.f32">;
def int_nvvm_tld4_r_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.r.2d.v4s32.f32">;
def int_nvvm_tld4_g_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.g.2d.v4s32.f32">;
def int_nvvm_tld4_b_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.b.2d.v4s32.f32">;
def int_nvvm_tld4_a_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.a.2d.v4s32.f32">;
def int_nvvm_tld4_r_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.r.2d.v4u32.f32">;
def int_nvvm_tld4_g_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.g.2d.v4u32.f32">;
def int_nvvm_tld4_b_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.b.2d.v4u32.f32">;
def int_nvvm_tld4_a_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.a.2d.v4u32.f32">;


// texmode_unified
def int_nvvm_tex_unified_1d_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.1d.v4f32.s32">;
def int_nvvm_tex_unified_1d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.v4f32.f32">;
def int_nvvm_tex_unified_1d_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.level.v4f32.f32">;
def int_nvvm_tex_unified_1d_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.grad.v4f32.f32">;
def int_nvvm_tex_unified_1d_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.1d.v4s32.s32">;
def int_nvvm_tex_unified_1d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.v4s32.f32">;
def int_nvvm_tex_unified_1d_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.level.v4s32.f32">;
def int_nvvm_tex_unified_1d_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.grad.v4s32.f32">;
def int_nvvm_tex_unified_1d_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.1d.v4u32.s32">;
def int_nvvm_tex_unified_1d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.v4u32.f32">;
def int_nvvm_tex_unified_1d_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.level.v4u32.f32">;
def int_nvvm_tex_unified_1d_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.grad.v4u32.f32">;

def int_nvvm_tex_unified_1d_array_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.1d.array.v4f32.s32">;
def int_nvvm_tex_unified_1d_array_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.v4f32.f32">;
def int_nvvm_tex_unified_1d_array_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.level.v4f32.f32">;
def int_nvvm_tex_unified_1d_array_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.grad.v4f32.f32">;
def int_nvvm_tex_unified_1d_array_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.1d.array.v4s32.s32">;
def int_nvvm_tex_unified_1d_array_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.v4s32.f32">;
def int_nvvm_tex_unified_1d_array_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.level.v4s32.f32">;
def int_nvvm_tex_unified_1d_array_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.grad.v4s32.f32">;
def int_nvvm_tex_unified_1d_array_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.1d.array.v4u32.s32">;
def int_nvvm_tex_unified_1d_array_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.v4u32.f32">;
def int_nvvm_tex_unified_1d_array_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.level.v4u32.f32">;
def int_nvvm_tex_unified_1d_array_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.1d.array.grad.v4u32.f32">;

def int_nvvm_tex_unified_2d_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.2d.v4f32.s32">;
def int_nvvm_tex_unified_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.v4f32.f32">;
def int_nvvm_tex_unified_2d_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.level.v4f32.f32">;
def int_nvvm_tex_unified_2d_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.grad.v4f32.f32">;
def int_nvvm_tex_unified_2d_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.2d.v4s32.s32">;
def int_nvvm_tex_unified_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.v4s32.f32">;
def int_nvvm_tex_unified_2d_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.level.v4s32.f32">;
def int_nvvm_tex_unified_2d_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.grad.v4s32.f32">;
def int_nvvm_tex_unified_2d_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.2d.v4u32.s32">;
def int_nvvm_tex_unified_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.v4u32.f32">;
def int_nvvm_tex_unified_2d_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.level.v4u32.f32">;
def int_nvvm_tex_unified_2d_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.grad.v4u32.f32">;

def int_nvvm_tex_unified_2d_array_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
               llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.2d.array.v4f32.s32">;
def int_nvvm_tex_unified_2d_array_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.v4f32.f32">;
def int_nvvm_tex_unified_2d_array_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.level.v4f32.f32">;
def int_nvvm_tex_unified_2d_array_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.grad.v4f32.f32">;
def int_nvvm_tex_unified_2d_array_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
               llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.2d.array.v4s32.s32">;
def int_nvvm_tex_unified_2d_array_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.v4s32.f32">;
def int_nvvm_tex_unified_2d_array_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.level.v4s32.f32">;
def int_nvvm_tex_unified_2d_array_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.grad.v4s32.f32">;
def int_nvvm_tex_unified_2d_array_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
               llvm_i32_ty], [],
              "llvm.nvvm.tex.unified.2d.array.v4u32.s32">;
def int_nvvm_tex_unified_2d_array_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.v4u32.f32">;
def int_nvvm_tex_unified_2d_array_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.level.v4u32.f32">;
def int_nvvm_tex_unified_2d_array_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.2d.array.grad.v4u32.f32">;

def int_nvvm_tex_unified_3d_v4f32_s32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [], "llvm.nvvm.tex.unified.3d.v4f32.s32">;
def int_nvvm_tex_unified_3d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.v4f32.f32">;
def int_nvvm_tex_unified_3d_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.level.v4f32.f32">;
def int_nvvm_tex_unified_3d_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.grad.v4f32.f32">;
def int_nvvm_tex_unified_3d_v4s32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [], "llvm.nvvm.tex.unified.3d.v4s32.s32">;
def int_nvvm_tex_unified_3d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.v4s32.f32">;
def int_nvvm_tex_unified_3d_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.level.v4s32.f32">;
def int_nvvm_tex_unified_3d_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.grad.v4s32.f32">;
def int_nvvm_tex_unified_3d_v4u32_s32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [], "llvm.nvvm.tex.unified.3d.v4u32.s32">;
def int_nvvm_tex_unified_3d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.v4u32.f32">;
def int_nvvm_tex_unified_3d_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.level.v4u32.f32">;
def int_nvvm_tex_unified_3d_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.3d.grad.v4u32.f32">;

def int_nvvm_tex_unified_cube_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.v4f32.f32">;
def int_nvvm_tex_unified_cube_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.level.v4f32.f32">;
def int_nvvm_tex_unified_cube_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.v4s32.f32">;
def int_nvvm_tex_unified_cube_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.level.v4s32.f32">;
def int_nvvm_tex_unified_cube_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.v4u32.f32">;
def int_nvvm_tex_unified_cube_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.level.v4u32.f32">;

def int_nvvm_tex_unified_cube_array_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.v4f32.f32">;
def int_nvvm_tex_unified_cube_array_level_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.level.v4f32.f32">;
def int_nvvm_tex_unified_cube_array_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.v4s32.f32">;
def int_nvvm_tex_unified_cube_array_level_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.level.v4s32.f32">;
def int_nvvm_tex_unified_cube_array_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.v4u32.f32">;
def int_nvvm_tex_unified_cube_array_level_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.level.v4u32.f32">;

def int_nvvm_tex_unified_cube_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.grad.v4f32.f32">;
def int_nvvm_tex_unified_cube_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.grad.v4s32.f32">;
def int_nvvm_tex_unified_cube_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.grad.v4u32.f32">;

def int_nvvm_tex_unified_cube_array_grad_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_i32_ty,
              llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.grad.v4f32.f32">;
def int_nvvm_tex_unified_cube_array_grad_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty,
              llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.grad.v4s32.f32">;
def int_nvvm_tex_unified_cube_array_grad_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty,
              llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty,
               llvm_float_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tex.unified.cube.array.grad.v4u32.f32">;

def int_nvvm_tld4_unified_r_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.r.2d.v4f32.f32">;
def int_nvvm_tld4_unified_g_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.g.2d.v4f32.f32">;
def int_nvvm_tld4_unified_b_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.b.2d.v4f32.f32">;
def int_nvvm_tld4_unified_a_2d_v4f32_f32
  : Intrinsic<[llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_float_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.a.2d.v4f32.f32">;
def int_nvvm_tld4_unified_r_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.r.2d.v4s32.f32">;
def int_nvvm_tld4_unified_g_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.g.2d.v4s32.f32">;
def int_nvvm_tld4_unified_b_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.b.2d.v4s32.f32">;
def int_nvvm_tld4_unified_a_2d_v4s32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.a.2d.v4s32.f32">;
def int_nvvm_tld4_unified_r_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.r.2d.v4u32.f32">;
def int_nvvm_tld4_unified_g_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.g.2d.v4u32.f32">;
def int_nvvm_tld4_unified_b_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.b.2d.v4u32.f32">;
def int_nvvm_tld4_unified_a_2d_v4u32_f32
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_float_ty, llvm_float_ty], [],
              "llvm.nvvm.tld4.unified.a.2d.v4u32.f32">;


//=== Surface Load
// .clamp variants
def int_nvvm_suld_1d_i8_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i8.clamp">;
def int_nvvm_suld_1d_i16_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i16.clamp">;
def int_nvvm_suld_1d_i32_clamp
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i32.clamp">;
def int_nvvm_suld_1d_i64_clamp
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i64.clamp">;
def int_nvvm_suld_1d_v2i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i8.clamp">;
def int_nvvm_suld_1d_v2i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i16.clamp">;
def int_nvvm_suld_1d_v2i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i32.clamp">;
def int_nvvm_suld_1d_v2i64_clamp
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i64.clamp">;
def int_nvvm_suld_1d_v4i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i8.clamp">;
def int_nvvm_suld_1d_v4i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i16.clamp">;
def int_nvvm_suld_1d_v4i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i32.clamp">;

def int_nvvm_suld_1d_array_i8_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i8.clamp">;
def int_nvvm_suld_1d_array_i16_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i16.clamp">;
def int_nvvm_suld_1d_array_i32_clamp
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i32.clamp">;
def int_nvvm_suld_1d_array_i64_clamp
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i64.clamp">;
def int_nvvm_suld_1d_array_v2i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i8.clamp">;
def int_nvvm_suld_1d_array_v2i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i16.clamp">;
def int_nvvm_suld_1d_array_v2i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i32.clamp">;
def int_nvvm_suld_1d_array_v2i64_clamp
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i64.clamp">;
def int_nvvm_suld_1d_array_v4i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i8.clamp">;
def int_nvvm_suld_1d_array_v4i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i16.clamp">;
def int_nvvm_suld_1d_array_v4i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i32.clamp">;

def int_nvvm_suld_2d_i8_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i8.clamp">;
def int_nvvm_suld_2d_i16_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i16.clamp">;
def int_nvvm_suld_2d_i32_clamp
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i32.clamp">;
def int_nvvm_suld_2d_i64_clamp
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i64.clamp">;
def int_nvvm_suld_2d_v2i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i8.clamp">;
def int_nvvm_suld_2d_v2i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i16.clamp">;
def int_nvvm_suld_2d_v2i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i32.clamp">;
def int_nvvm_suld_2d_v2i64_clamp
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i64.clamp">;
def int_nvvm_suld_2d_v4i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i8.clamp">;
def int_nvvm_suld_2d_v4i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i16.clamp">;
def int_nvvm_suld_2d_v4i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i32.clamp">;

def int_nvvm_suld_2d_array_i8_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i8.clamp">;
def int_nvvm_suld_2d_array_i16_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i16.clamp">;
def int_nvvm_suld_2d_array_i32_clamp
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i32.clamp">;
def int_nvvm_suld_2d_array_i64_clamp
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i64.clamp">;
def int_nvvm_suld_2d_array_v2i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i8.clamp">;
def int_nvvm_suld_2d_array_v2i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i16.clamp">;
def int_nvvm_suld_2d_array_v2i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i32.clamp">;
def int_nvvm_suld_2d_array_v2i64_clamp
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i64.clamp">;
def int_nvvm_suld_2d_array_v4i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i8.clamp">;
def int_nvvm_suld_2d_array_v4i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i16.clamp">;
def int_nvvm_suld_2d_array_v4i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i32.clamp">;

def int_nvvm_suld_3d_i8_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i8.clamp">;
def int_nvvm_suld_3d_i16_clamp
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i16.clamp">;
def int_nvvm_suld_3d_i32_clamp
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i32.clamp">;
def int_nvvm_suld_3d_i64_clamp
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i64.clamp">;
def int_nvvm_suld_3d_v2i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i8.clamp">;
def int_nvvm_suld_3d_v2i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i16.clamp">;
def int_nvvm_suld_3d_v2i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i32.clamp">;
def int_nvvm_suld_3d_v2i64_clamp
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i64.clamp">;
def int_nvvm_suld_3d_v4i8_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i8.clamp">;
def int_nvvm_suld_3d_v4i16_clamp
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i16.clamp">;
def int_nvvm_suld_3d_v4i32_clamp
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i32.clamp">;

// .trap variants
def int_nvvm_suld_1d_i8_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i8.trap">;
def int_nvvm_suld_1d_i16_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i16.trap">;
def int_nvvm_suld_1d_i32_trap
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i32.trap">;
def int_nvvm_suld_1d_i64_trap
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i64.trap">;
def int_nvvm_suld_1d_v2i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i8.trap">;
def int_nvvm_suld_1d_v2i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i16.trap">;
def int_nvvm_suld_1d_v2i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i32.trap">;
def int_nvvm_suld_1d_v2i64_trap
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i64.trap">;
def int_nvvm_suld_1d_v4i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i8.trap">;
def int_nvvm_suld_1d_v4i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i16.trap">;
def int_nvvm_suld_1d_v4i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i32.trap">;

def int_nvvm_suld_1d_array_i8_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i8.trap">;
def int_nvvm_suld_1d_array_i16_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i16.trap">;
def int_nvvm_suld_1d_array_i32_trap
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i32.trap">;
def int_nvvm_suld_1d_array_i64_trap
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i64.trap">;
def int_nvvm_suld_1d_array_v2i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i8.trap">;
def int_nvvm_suld_1d_array_v2i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i16.trap">;
def int_nvvm_suld_1d_array_v2i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i32.trap">;
def int_nvvm_suld_1d_array_v2i64_trap
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i64.trap">;
def int_nvvm_suld_1d_array_v4i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i8.trap">;
def int_nvvm_suld_1d_array_v4i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i16.trap">;
def int_nvvm_suld_1d_array_v4i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i32.trap">;

def int_nvvm_suld_2d_i8_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i8.trap">;
def int_nvvm_suld_2d_i16_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i16.trap">;
def int_nvvm_suld_2d_i32_trap
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i32.trap">;
def int_nvvm_suld_2d_i64_trap
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i64.trap">;
def int_nvvm_suld_2d_v2i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i8.trap">;
def int_nvvm_suld_2d_v2i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i16.trap">;
def int_nvvm_suld_2d_v2i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i32.trap">;
def int_nvvm_suld_2d_v2i64_trap
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i64.trap">;
def int_nvvm_suld_2d_v4i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i8.trap">;
def int_nvvm_suld_2d_v4i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i16.trap">;
def int_nvvm_suld_2d_v4i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i32.trap">;

def int_nvvm_suld_2d_array_i8_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i8.trap">;
def int_nvvm_suld_2d_array_i16_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i16.trap">;
def int_nvvm_suld_2d_array_i32_trap
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i32.trap">;
def int_nvvm_suld_2d_array_i64_trap
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i64.trap">;
def int_nvvm_suld_2d_array_v2i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i8.trap">;
def int_nvvm_suld_2d_array_v2i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i16.trap">;
def int_nvvm_suld_2d_array_v2i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i32.trap">;
def int_nvvm_suld_2d_array_v2i64_trap
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i64.trap">;
def int_nvvm_suld_2d_array_v4i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i8.trap">;
def int_nvvm_suld_2d_array_v4i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i16.trap">;
def int_nvvm_suld_2d_array_v4i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i32.trap">;

def int_nvvm_suld_3d_i8_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i8.trap">;
def int_nvvm_suld_3d_i16_trap
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i16.trap">;
def int_nvvm_suld_3d_i32_trap
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i32.trap">;
def int_nvvm_suld_3d_i64_trap
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i64.trap">;
def int_nvvm_suld_3d_v2i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i8.trap">;
def int_nvvm_suld_3d_v2i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i16.trap">;
def int_nvvm_suld_3d_v2i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i32.trap">;
def int_nvvm_suld_3d_v2i64_trap
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i64.trap">;
def int_nvvm_suld_3d_v4i8_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i8.trap">;
def int_nvvm_suld_3d_v4i16_trap
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i16.trap">;
def int_nvvm_suld_3d_v4i32_trap
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i32.trap">;

// .zero variants
def int_nvvm_suld_1d_i8_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i8.zero">;
def int_nvvm_suld_1d_i16_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i16.zero">;
def int_nvvm_suld_1d_i32_zero
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i32.zero">;
def int_nvvm_suld_1d_i64_zero
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.i64.zero">;
def int_nvvm_suld_1d_v2i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i8.zero">;
def int_nvvm_suld_1d_v2i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i16.zero">;
def int_nvvm_suld_1d_v2i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i32.zero">;
def int_nvvm_suld_1d_v2i64_zero
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v2i64.zero">;
def int_nvvm_suld_1d_v4i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i8.zero">;
def int_nvvm_suld_1d_v4i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i16.zero">;
def int_nvvm_suld_1d_v4i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.v4i32.zero">;

def int_nvvm_suld_1d_array_i8_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i8.zero">;
def int_nvvm_suld_1d_array_i16_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i16.zero">;
def int_nvvm_suld_1d_array_i32_zero
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i32.zero">;
def int_nvvm_suld_1d_array_i64_zero
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.i64.zero">;
def int_nvvm_suld_1d_array_v2i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i8.zero">;
def int_nvvm_suld_1d_array_v2i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i16.zero">;
def int_nvvm_suld_1d_array_v2i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i32.zero">;
def int_nvvm_suld_1d_array_v2i64_zero
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v2i64.zero">;
def int_nvvm_suld_1d_array_v4i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i8.zero">;
def int_nvvm_suld_1d_array_v4i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i16.zero">;
def int_nvvm_suld_1d_array_v4i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.1d.array.v4i32.zero">;

def int_nvvm_suld_2d_i8_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i8.zero">;
def int_nvvm_suld_2d_i16_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i16.zero">;
def int_nvvm_suld_2d_i32_zero
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i32.zero">;
def int_nvvm_suld_2d_i64_zero
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.i64.zero">;
def int_nvvm_suld_2d_v2i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i8.zero">;
def int_nvvm_suld_2d_v2i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i16.zero">;
def int_nvvm_suld_2d_v2i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i32.zero">;
def int_nvvm_suld_2d_v2i64_zero
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v2i64.zero">;
def int_nvvm_suld_2d_v4i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i8.zero">;
def int_nvvm_suld_2d_v4i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i16.zero">;
def int_nvvm_suld_2d_v4i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.v4i32.zero">;

def int_nvvm_suld_2d_array_i8_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i8.zero">;
def int_nvvm_suld_2d_array_i16_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i16.zero">;
def int_nvvm_suld_2d_array_i32_zero
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i32.zero">;
def int_nvvm_suld_2d_array_i64_zero
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.i64.zero">;
def int_nvvm_suld_2d_array_v2i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i8.zero">;
def int_nvvm_suld_2d_array_v2i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i16.zero">;
def int_nvvm_suld_2d_array_v2i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i32.zero">;
def int_nvvm_suld_2d_array_v2i64_zero
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v2i64.zero">;
def int_nvvm_suld_2d_array_v4i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i8.zero">;
def int_nvvm_suld_2d_array_v4i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i16.zero">;
def int_nvvm_suld_2d_array_v4i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.2d.array.v4i32.zero">;

def int_nvvm_suld_3d_i8_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i8.zero">;
def int_nvvm_suld_3d_i16_zero
  : Intrinsic<[llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i16.zero">;
def int_nvvm_suld_3d_i32_zero
  : Intrinsic<[llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i32.zero">;
def int_nvvm_suld_3d_i64_zero
  : Intrinsic<[llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.i64.zero">;
def int_nvvm_suld_3d_v2i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i8.zero">;
def int_nvvm_suld_3d_v2i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i16.zero">;
def int_nvvm_suld_3d_v2i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i32.zero">;
def int_nvvm_suld_3d_v2i64_zero
  : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v2i64.zero">;
def int_nvvm_suld_3d_v4i8_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i8.zero">;
def int_nvvm_suld_3d_v4i16_zero
  : Intrinsic<[llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i16.zero">;
def int_nvvm_suld_3d_v4i32_zero
  : Intrinsic<[llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.suld.3d.v4i32.zero">;

//===- Texture Query ------------------------------------------------------===//

def int_nvvm_txq_channel_order
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.channel.order">,
    ClangBuiltin<"__nvvm_txq_channel_order">;
def int_nvvm_txq_channel_data_type
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.channel.data.type">,
    ClangBuiltin<"__nvvm_txq_channel_data_type">;
def int_nvvm_txq_width
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.width">,
    ClangBuiltin<"__nvvm_txq_width">;
def int_nvvm_txq_height
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.height">,
    ClangBuiltin<"__nvvm_txq_height">;
def int_nvvm_txq_depth
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.depth">,
    ClangBuiltin<"__nvvm_txq_depth">;
def int_nvvm_txq_array_size
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.array.size">,
    ClangBuiltin<"__nvvm_txq_array_size">;
def int_nvvm_txq_num_samples
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.num.samples">,
    ClangBuiltin<"__nvvm_txq_num_samples">;
def int_nvvm_txq_num_mipmap_levels
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.txq.num.mipmap.levels">,
    ClangBuiltin<"__nvvm_txq_num_mipmap_levels">;

//===- Surface Query ------------------------------------------------------===//

def int_nvvm_suq_channel_order
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.suq.channel.order">,
    ClangBuiltin<"__nvvm_suq_channel_order">;
def int_nvvm_suq_channel_data_type
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.suq.channel.data.type">,
    ClangBuiltin<"__nvvm_suq_channel_data_type">;
def int_nvvm_suq_width
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.suq.width">,
    ClangBuiltin<"__nvvm_suq_width">;
def int_nvvm_suq_height
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.suq.height">,
    ClangBuiltin<"__nvvm_suq_height">;
def int_nvvm_suq_depth
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.suq.depth">,
    ClangBuiltin<"__nvvm_suq_depth">;
def int_nvvm_suq_array_size
  : Intrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.suq.array.size">,
    ClangBuiltin<"__nvvm_suq_array_size">;


//===- Handle Query -------------------------------------------------------===//

def int_nvvm_istypep_sampler
  : Intrinsic<[llvm_i1_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.istypep.sampler">,
    ClangBuiltin<"__nvvm_istypep_sampler">;
def int_nvvm_istypep_surface
  : Intrinsic<[llvm_i1_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.istypep.surface">,
    ClangBuiltin<"__nvvm_istypep_surface">;
def int_nvvm_istypep_texture
  : Intrinsic<[llvm_i1_ty], [llvm_i64_ty], [IntrNoMem],
              "llvm.nvvm.istypep.texture">,
    ClangBuiltin<"__nvvm_istypep_texture">;



//===- Surface Stores -----------------------------------------------------===//

// Unformatted
// .clamp variant
def int_nvvm_sust_b_1d_i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_i8_clamp">;
def int_nvvm_sust_b_1d_i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_i16_clamp">;
def int_nvvm_sust_b_1d_i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_i32_clamp">;
def int_nvvm_sust_b_1d_i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_i64_clamp">;
def int_nvvm_sust_b_1d_v2i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v2i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i8_clamp">;
def int_nvvm_sust_b_1d_v2i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v2i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i16_clamp">;
def int_nvvm_sust_b_1d_v2i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.v2i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i32_clamp">;
def int_nvvm_sust_b_1d_v2i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.v2i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i64_clamp">;
def int_nvvm_sust_b_1d_v4i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v4i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i8_clamp">;
def int_nvvm_sust_b_1d_v4i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v4i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i16_clamp">;
def int_nvvm_sust_b_1d_v4i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.v4i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i32_clamp">;


def int_nvvm_sust_b_1d_array_i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i8_clamp">;
def int_nvvm_sust_b_1d_array_i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i16_clamp">;
def int_nvvm_sust_b_1d_array_i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i32_clamp">;
def int_nvvm_sust_b_1d_array_i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.array.i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i64_clamp">;
def int_nvvm_sust_b_1d_array_v2i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i8_clamp">;
def int_nvvm_sust_b_1d_array_v2i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i16_clamp">;
def int_nvvm_sust_b_1d_array_v2i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i32_clamp">;
def int_nvvm_sust_b_1d_array_v2i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i64_clamp">;
def int_nvvm_sust_b_1d_array_v4i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i8_clamp">;
def int_nvvm_sust_b_1d_array_v4i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i16_clamp">;
def int_nvvm_sust_b_1d_array_v4i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i32_clamp">;


def int_nvvm_sust_b_2d_i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_i8_clamp">;
def int_nvvm_sust_b_2d_i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_i16_clamp">;
def int_nvvm_sust_b_2d_i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_i32_clamp">;
def int_nvvm_sust_b_2d_i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_i64_clamp">;
def int_nvvm_sust_b_2d_v2i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v2i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i8_clamp">;
def int_nvvm_sust_b_2d_v2i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v2i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i16_clamp">;
def int_nvvm_sust_b_2d_v2i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.v2i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i32_clamp">;
def int_nvvm_sust_b_2d_v2i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.v2i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i64_clamp">;
def int_nvvm_sust_b_2d_v4i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v4i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i8_clamp">;
def int_nvvm_sust_b_2d_v4i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v4i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i16_clamp">;
def int_nvvm_sust_b_2d_v4i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.v4i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i32_clamp">;


def int_nvvm_sust_b_2d_array_i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i8_clamp">;
def int_nvvm_sust_b_2d_array_i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i16_clamp">;
def int_nvvm_sust_b_2d_array_i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i32_clamp">;
def int_nvvm_sust_b_2d_array_i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.array.i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i64_clamp">;
def int_nvvm_sust_b_2d_array_v2i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i8_clamp">;
def int_nvvm_sust_b_2d_array_v2i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i16_clamp">;
def int_nvvm_sust_b_2d_array_v2i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i32_clamp">;
def int_nvvm_sust_b_2d_array_v2i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i64_clamp">;
def int_nvvm_sust_b_2d_array_v4i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i8_clamp">;
def int_nvvm_sust_b_2d_array_v4i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i16_clamp">;
def int_nvvm_sust_b_2d_array_v4i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i32_clamp">;


def int_nvvm_sust_b_3d_i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_i8_clamp">;
def int_nvvm_sust_b_3d_i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_i16_clamp">;
def int_nvvm_sust_b_3d_i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_i32_clamp">;
def int_nvvm_sust_b_3d_i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.3d.i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_i64_clamp">;
def int_nvvm_sust_b_3d_v2i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v2i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i8_clamp">;
def int_nvvm_sust_b_3d_v2i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v2i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i16_clamp">;
def int_nvvm_sust_b_3d_v2i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.v2i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i32_clamp">;
def int_nvvm_sust_b_3d_v2i64_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.3d.v2i64.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i64_clamp">;
def int_nvvm_sust_b_3d_v4i8_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v4i8.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i8_clamp">;
def int_nvvm_sust_b_3d_v4i16_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v4i16.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i16_clamp">;
def int_nvvm_sust_b_3d_v4i32_clamp
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.v4i32.clamp">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i32_clamp">;


// .trap variant
def int_nvvm_sust_b_1d_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_i8_trap">;
def int_nvvm_sust_b_1d_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_i16_trap">;
def int_nvvm_sust_b_1d_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_i32_trap">;
def int_nvvm_sust_b_1d_i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_i64_trap">;
def int_nvvm_sust_b_1d_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i8_trap">;
def int_nvvm_sust_b_1d_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i16_trap">;
def int_nvvm_sust_b_1d_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i32_trap">;
def int_nvvm_sust_b_1d_v2i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.v2i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i64_trap">;
def int_nvvm_sust_b_1d_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i8_trap">;
def int_nvvm_sust_b_1d_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i16_trap">;
def int_nvvm_sust_b_1d_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i32_trap">;


def int_nvvm_sust_b_1d_array_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i8_trap">;
def int_nvvm_sust_b_1d_array_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i16_trap">;
def int_nvvm_sust_b_1d_array_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i32_trap">;
def int_nvvm_sust_b_1d_array_i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.array.i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i64_trap">;
def int_nvvm_sust_b_1d_array_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i8_trap">;
def int_nvvm_sust_b_1d_array_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i16_trap">;
def int_nvvm_sust_b_1d_array_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i32_trap">;
def int_nvvm_sust_b_1d_array_v2i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i64_trap">;
def int_nvvm_sust_b_1d_array_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i8_trap">;
def int_nvvm_sust_b_1d_array_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i16_trap">;
def int_nvvm_sust_b_1d_array_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i32_trap">;


def int_nvvm_sust_b_2d_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_i8_trap">;
def int_nvvm_sust_b_2d_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_i16_trap">;
def int_nvvm_sust_b_2d_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_i32_trap">;
def int_nvvm_sust_b_2d_i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_i64_trap">;
def int_nvvm_sust_b_2d_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i8_trap">;
def int_nvvm_sust_b_2d_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i16_trap">;
def int_nvvm_sust_b_2d_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i32_trap">;
def int_nvvm_sust_b_2d_v2i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.v2i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i64_trap">;
def int_nvvm_sust_b_2d_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i8_trap">;
def int_nvvm_sust_b_2d_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i16_trap">;
def int_nvvm_sust_b_2d_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i32_trap">;


def int_nvvm_sust_b_2d_array_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i8_trap">;
def int_nvvm_sust_b_2d_array_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i16_trap">;
def int_nvvm_sust_b_2d_array_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i32_trap">;
def int_nvvm_sust_b_2d_array_i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.array.i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i64_trap">;
def int_nvvm_sust_b_2d_array_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i8_trap">;
def int_nvvm_sust_b_2d_array_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i16_trap">;
def int_nvvm_sust_b_2d_array_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i32_trap">;
def int_nvvm_sust_b_2d_array_v2i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i64_trap">;
def int_nvvm_sust_b_2d_array_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i8_trap">;
def int_nvvm_sust_b_2d_array_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i16_trap">;
def int_nvvm_sust_b_2d_array_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i32_trap">;


def int_nvvm_sust_b_3d_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_i8_trap">;
def int_nvvm_sust_b_3d_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_i16_trap">;
def int_nvvm_sust_b_3d_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_i32_trap">;
def int_nvvm_sust_b_3d_i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.3d.i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_i64_trap">;
def int_nvvm_sust_b_3d_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i8_trap">;
def int_nvvm_sust_b_3d_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i16_trap">;
def int_nvvm_sust_b_3d_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i32_trap">;
def int_nvvm_sust_b_3d_v2i64_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.3d.v2i64.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i64_trap">;
def int_nvvm_sust_b_3d_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i8_trap">;
def int_nvvm_sust_b_3d_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i16_trap">;
def int_nvvm_sust_b_3d_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i32_trap">;


// .zero variant
def int_nvvm_sust_b_1d_i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_i8_zero">;
def int_nvvm_sust_b_1d_i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_i16_zero">;
def int_nvvm_sust_b_1d_i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_i32_zero">;
def int_nvvm_sust_b_1d_i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_i64_zero">;
def int_nvvm_sust_b_1d_v2i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v2i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i8_zero">;
def int_nvvm_sust_b_1d_v2i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v2i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i16_zero">;
def int_nvvm_sust_b_1d_v2i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.v2i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i32_zero">;
def int_nvvm_sust_b_1d_v2i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.v2i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_v2i64_zero">;
def int_nvvm_sust_b_1d_v4i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v4i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i8_zero">;
def int_nvvm_sust_b_1d_v4i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.v4i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i16_zero">;
def int_nvvm_sust_b_1d_v4i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.v4i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_v4i32_zero">;


def int_nvvm_sust_b_1d_array_i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i8_zero">;
def int_nvvm_sust_b_1d_array_i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i16_zero">;
def int_nvvm_sust_b_1d_array_i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i32_zero">;
def int_nvvm_sust_b_1d_array_i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.array.i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_i64_zero">;
def int_nvvm_sust_b_1d_array_v2i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i8_zero">;
def int_nvvm_sust_b_1d_array_v2i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i16_zero">;
def int_nvvm_sust_b_1d_array_v2i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i32_zero">;
def int_nvvm_sust_b_1d_array_v2i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.1d.array.v2i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v2i64_zero">;
def int_nvvm_sust_b_1d_array_v4i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i8_zero">;
def int_nvvm_sust_b_1d_array_v4i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i16_zero">;
def int_nvvm_sust_b_1d_array_v4i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.1d.array.v4i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_1d_array_v4i32_zero">;


def int_nvvm_sust_b_2d_i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_i8_zero">;
def int_nvvm_sust_b_2d_i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_i16_zero">;
def int_nvvm_sust_b_2d_i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_i32_zero">;
def int_nvvm_sust_b_2d_i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_i64_zero">;
def int_nvvm_sust_b_2d_v2i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v2i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i8_zero">;
def int_nvvm_sust_b_2d_v2i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v2i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i16_zero">;
def int_nvvm_sust_b_2d_v2i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.v2i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i32_zero">;
def int_nvvm_sust_b_2d_v2i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.v2i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_v2i64_zero">;
def int_nvvm_sust_b_2d_v4i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v4i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i8_zero">;
def int_nvvm_sust_b_2d_v4i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.v4i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i16_zero">;
def int_nvvm_sust_b_2d_v4i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.v4i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_v4i32_zero">;


def int_nvvm_sust_b_2d_array_i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i8_zero">;
def int_nvvm_sust_b_2d_array_i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i16_zero">;
def int_nvvm_sust_b_2d_array_i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i32_zero">;
def int_nvvm_sust_b_2d_array_i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.array.i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_i64_zero">;
def int_nvvm_sust_b_2d_array_v2i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i8_zero">;
def int_nvvm_sust_b_2d_array_v2i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i16_zero">;
def int_nvvm_sust_b_2d_array_v2i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i32_zero">;
def int_nvvm_sust_b_2d_array_v2i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.2d.array.v2i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v2i64_zero">;
def int_nvvm_sust_b_2d_array_v4i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i8_zero">;
def int_nvvm_sust_b_2d_array_v4i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i16_zero">;
def int_nvvm_sust_b_2d_array_v4i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.2d.array.v4i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_2d_array_v4i32_zero">;


def int_nvvm_sust_b_3d_i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_i8_zero">;
def int_nvvm_sust_b_3d_i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_i16_zero">;
def int_nvvm_sust_b_3d_i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_i32_zero">;
def int_nvvm_sust_b_3d_i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.3d.i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_i64_zero">;
def int_nvvm_sust_b_3d_v2i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v2i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i8_zero">;
def int_nvvm_sust_b_3d_v2i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v2i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i16_zero">;
def int_nvvm_sust_b_3d_v2i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.v2i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i32_zero">;
def int_nvvm_sust_b_3d_v2i64_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i64_ty, llvm_i64_ty], [],
              "llvm.nvvm.sust.b.3d.v2i64.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_v2i64_zero">;
def int_nvvm_sust_b_3d_v4i8_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v4i8.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i8_zero">;
def int_nvvm_sust_b_3d_v4i16_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.b.3d.v4i16.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i16_zero">;
def int_nvvm_sust_b_3d_v4i32_zero
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.b.3d.v4i32.zero">,
    ClangBuiltin<"__nvvm_sust_b_3d_v4i32_zero">;



// Formatted

def int_nvvm_sust_p_1d_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_i8_trap">;
def int_nvvm_sust_p_1d_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_i16_trap">;
def int_nvvm_sust_p_1d_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.1d.i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_i32_trap">;
def int_nvvm_sust_p_1d_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_v2i8_trap">;
def int_nvvm_sust_p_1d_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_v2i16_trap">;
def int_nvvm_sust_p_1d_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.1d.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_v2i32_trap">;
def int_nvvm_sust_p_1d_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_v4i8_trap">;
def int_nvvm_sust_p_1d_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_v4i16_trap">;
def int_nvvm_sust_p_1d_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.1d.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_v4i32_trap">;


def int_nvvm_sust_p_1d_array_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.array.i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_i8_trap">;
def int_nvvm_sust_p_1d_array_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.array.i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_i16_trap">;
def int_nvvm_sust_p_1d_array_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.1d.array.i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_i32_trap">;
def int_nvvm_sust_p_1d_array_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.array.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_v2i8_trap">;
def int_nvvm_sust_p_1d_array_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.array.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_v2i16_trap">;
def int_nvvm_sust_p_1d_array_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.1d.array.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_v2i32_trap">;
def int_nvvm_sust_p_1d_array_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.array.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_v4i8_trap">;
def int_nvvm_sust_p_1d_array_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.1d.array.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_v4i16_trap">;
def int_nvvm_sust_p_1d_array_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.1d.array.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_1d_array_v4i32_trap">;


def int_nvvm_sust_p_2d_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_i8_trap">;
def int_nvvm_sust_p_2d_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_i16_trap">;
def int_nvvm_sust_p_2d_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.2d.i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_i32_trap">;
def int_nvvm_sust_p_2d_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_v2i8_trap">;
def int_nvvm_sust_p_2d_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_v2i16_trap">;
def int_nvvm_sust_p_2d_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.2d.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_v2i32_trap">;
def int_nvvm_sust_p_2d_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_v4i8_trap">;
def int_nvvm_sust_p_2d_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_v4i16_trap">;
def int_nvvm_sust_p_2d_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.2d.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_v4i32_trap">;


def int_nvvm_sust_p_2d_array_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.array.i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_i8_trap">;
def int_nvvm_sust_p_2d_array_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.array.i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_i16_trap">;
def int_nvvm_sust_p_2d_array_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.2d.array.i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_i32_trap">;
def int_nvvm_sust_p_2d_array_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.array.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_v2i8_trap">;
def int_nvvm_sust_p_2d_array_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.array.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_v2i16_trap">;
def int_nvvm_sust_p_2d_array_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.2d.array.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_v2i32_trap">;
def int_nvvm_sust_p_2d_array_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.array.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_v4i8_trap">;
def int_nvvm_sust_p_2d_array_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.2d.array.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_v4i16_trap">;
def int_nvvm_sust_p_2d_array_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.2d.array.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_2d_array_v4i32_trap">;


def int_nvvm_sust_p_3d_i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.3d.i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_i8_trap">;
def int_nvvm_sust_p_3d_i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.3d.i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_i16_trap">;
def int_nvvm_sust_p_3d_i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.3d.i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_i32_trap">;
def int_nvvm_sust_p_3d_v2i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.3d.v2i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_v2i8_trap">;
def int_nvvm_sust_p_3d_v2i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.3d.v2i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_v2i16_trap">;
def int_nvvm_sust_p_3d_v2i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.3d.v2i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_v2i32_trap">;
def int_nvvm_sust_p_3d_v4i8_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.3d.v4i8.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_v4i8_trap">;
def int_nvvm_sust_p_3d_v4i16_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty], [],
              "llvm.nvvm.sust.p.3d.v4i16.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_v4i16_trap">;
def int_nvvm_sust_p_3d_v4i32_trap
  : Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                   llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [],
              "llvm.nvvm.sust.p.3d.v4i32.trap">,
    ClangBuiltin<"__nvvm_sust_p_3d_v4i32_trap">;


def int_nvvm_rotate_b32
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem, IntrSpeculatable], "llvm.nvvm.rotate.b32">,
              ClangBuiltin<"__nvvm_rotate_b32">;

def int_nvvm_rotate_b64
  : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
             [IntrNoMem, IntrSpeculatable], "llvm.nvvm.rotate.b64">,
             ClangBuiltin<"__nvvm_rotate_b64">;

def int_nvvm_rotate_right_b64
  : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
              [IntrNoMem, IntrSpeculatable], "llvm.nvvm.rotate.right.b64">,
              ClangBuiltin<"__nvvm_rotate_right_b64">;

def int_nvvm_swap_lo_hi_b64
  : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty],
              [IntrNoMem, IntrSpeculatable], "llvm.nvvm.swap.lo.hi.b64">,
              ClangBuiltin<"__nvvm_swap_lo_hi_b64">;


// Accessing special registers.

class PTXReadSRegIntrinsicNB_r32
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>]>;
class PTXReadSRegIntrinsic_r32<string name>
  : PTXReadSRegIntrinsicNB_r32, ClangBuiltin<"__nvvm_read_ptx_sreg_" # name>;

multiclass PTXReadSRegIntrinsic_v4i32<string regname> {
// FIXME: Do we need the 128-bit integer type version?
//    def _r64   : Intrinsic<[llvm_i128_ty],   [], [IntrNoMem, IntrSpeculatable]>;

// FIXME: Enable this once v4i32 support is enabled in back-end.
//    def _v4i16 : Intrinsic<[llvm_v4i32_ty], [], [IntrNoMem, IntrSpeculatable]>;
  foreach suffix = ["_x", "_y", "_z", "_w"] in
    def suffix : PTXReadSRegIntrinsic_r32<regname # suffix>;
}

// Same, but without automatic clang builtins. It will be used for
// registers that require particular GPU or PTX version.
multiclass PTXReadSRegIntrinsicNB_v4i32 {
  foreach suffix = ["_x", "_y", "_z", "_w"] in
    def suffix : PTXReadSRegIntrinsicNB_r32;
}

class PTXReadSRegIntrinsic_r64<string name>
  : DefaultAttrsIntrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>]>,
    ClangBuiltin<"__nvvm_read_ptx_sreg_" # name>;

// Intrinsics to read registers with non-constant values. E.g. the values that
// do change over the kernel lifetime. Such reads should not be CSE'd.
class PTXReadNCSRegIntrinsic_r32<string name>
  : Intrinsic<[llvm_i32_ty], [], [IntrInaccessibleMemOnly, IntrNoCallback, NoUndef<RetIndex>]>,
    ClangBuiltin<"__nvvm_read_ptx_sreg_" # name>;
class PTXReadNCSRegIntrinsic_r64<string name>
  : Intrinsic<[llvm_i64_ty], [], [IntrInaccessibleMemOnly, IntrNoCallback, NoUndef<RetIndex>]>,
    ClangBuiltin<"__nvvm_read_ptx_sreg_" # name>;

defm int_nvvm_read_ptx_sreg_tid : PTXReadSRegIntrinsic_v4i32<"tid">;
defm int_nvvm_read_ptx_sreg_ntid : PTXReadSRegIntrinsic_v4i32<"ntid">;

def int_nvvm_read_ptx_sreg_laneid : PTXReadSRegIntrinsic_r32<"laneid">;
def int_nvvm_read_ptx_sreg_warpid : PTXReadSRegIntrinsic_r32<"warpid">;
def int_nvvm_read_ptx_sreg_nwarpid : PTXReadSRegIntrinsic_r32<"nwarpid">;

defm int_nvvm_read_ptx_sreg_ctaid : PTXReadSRegIntrinsic_v4i32<"ctaid">;
defm int_nvvm_read_ptx_sreg_nctaid : PTXReadSRegIntrinsic_v4i32<"nctaid">;

def int_nvvm_read_ptx_sreg_smid : PTXReadSRegIntrinsic_r32<"smid">;
def int_nvvm_read_ptx_sreg_nsmid : PTXReadSRegIntrinsic_r32<"nsmid">;
def int_nvvm_read_ptx_sreg_gridid : PTXReadSRegIntrinsic_r32<"gridid">;

def int_nvvm_read_ptx_sreg_lanemask_eq :
    PTXReadSRegIntrinsic_r32<"lanemask_eq">;
def int_nvvm_read_ptx_sreg_lanemask_le :
    PTXReadSRegIntrinsic_r32<"lanemask_le">;
def int_nvvm_read_ptx_sreg_lanemask_lt :
    PTXReadSRegIntrinsic_r32<"lanemask_lt">;
def int_nvvm_read_ptx_sreg_lanemask_ge :
    PTXReadSRegIntrinsic_r32<"lanemask_ge">;
def int_nvvm_read_ptx_sreg_lanemask_gt :
    PTXReadSRegIntrinsic_r32<"lanemask_gt">;

def int_nvvm_read_ptx_sreg_clock : PTXReadNCSRegIntrinsic_r32<"clock">;
def int_nvvm_read_ptx_sreg_clock64 : PTXReadNCSRegIntrinsic_r64<"clock64">;

def int_nvvm_read_ptx_sreg_globaltimer : PTXReadNCSRegIntrinsic_r64<"globaltimer">;

def int_nvvm_read_ptx_sreg_pm0 : PTXReadNCSRegIntrinsic_r32<"pm0">;
def int_nvvm_read_ptx_sreg_pm1 : PTXReadNCSRegIntrinsic_r32<"pm1">;
def int_nvvm_read_ptx_sreg_pm2 : PTXReadNCSRegIntrinsic_r32<"pm2">;
def int_nvvm_read_ptx_sreg_pm3 : PTXReadNCSRegIntrinsic_r32<"pm3">;

def int_nvvm_read_ptx_sreg_warpsize : PTXReadSRegIntrinsic_r32<"warpsize">;

// sm90+, PTX7.8+
defm int_nvvm_read_ptx_sreg_clusterid : PTXReadSRegIntrinsicNB_v4i32;
defm int_nvvm_read_ptx_sreg_nclusterid : PTXReadSRegIntrinsicNB_v4i32;
defm int_nvvm_read_ptx_sreg_cluster_ctaid : PTXReadSRegIntrinsicNB_v4i32;
defm int_nvvm_read_ptx_sreg_cluster_nctaid : PTXReadSRegIntrinsicNB_v4i32;

def int_nvvm_read_ptx_sreg_cluster_ctarank : PTXReadSRegIntrinsicNB_r32;
def int_nvvm_read_ptx_sreg_cluster_nctarank : PTXReadSRegIntrinsicNB_r32;

//
// SHUFFLE
//
// Generate intrinsics for all variants of shfl instruction.
foreach sync = [false, true] in {
  foreach mode = ["up", "down", "bfly", "idx"] in {
    foreach type = ["i32", "f32"] in {
      foreach return_pred = [false, true] in {
        foreach i = [SHFL_INFO<sync, mode, type, return_pred>] in {
          if i.withGccBuiltin then {
            def i.Name : ClangBuiltin<i.Builtin>,
                         Intrinsic<i.RetTy, i.ArgsTy,
                                   [IntrInaccessibleMemOnly, IntrConvergent,
                                   IntrNoCallback],
                                   i.IntrName>;
          }
          if i.withoutGccBuiltin then {
            def i.Name : Intrinsic<i.RetTy, i.ArgsTy,
                         [IntrInaccessibleMemOnly, IntrConvergent,
                         IntrNoCallback], i.IntrName>;
          }
        }
      }
    }
  }
}

//
// VOTE
//

// vote.all pred
def int_nvvm_vote_all :
  Intrinsic<[llvm_i1_ty], [llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.all">,
  ClangBuiltin<"__nvvm_vote_all">;
// vote.any pred
def int_nvvm_vote_any :
  Intrinsic<[llvm_i1_ty], [llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.any">,
  ClangBuiltin<"__nvvm_vote_any">;
// vote.uni pred
def int_nvvm_vote_uni :
  Intrinsic<[llvm_i1_ty], [llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.uni">,
  ClangBuiltin<"__nvvm_vote_uni">;
// vote.ballot pred
def int_nvvm_vote_ballot :
  Intrinsic<[llvm_i32_ty], [llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.ballot">,
  ClangBuiltin<"__nvvm_vote_ballot">;

//
// VOTE.SYNC
//

// vote.sync.all mask, pred
def int_nvvm_vote_all_sync :
  Intrinsic<[llvm_i1_ty], [llvm_i32_ty, llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.all.sync">,
  ClangBuiltin<"__nvvm_vote_all_sync">;
// vote.sync.any mask, pred
def int_nvvm_vote_any_sync :
  Intrinsic<[llvm_i1_ty], [llvm_i32_ty, llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.any.sync">,
  ClangBuiltin<"__nvvm_vote_any_sync">;
// vote.sync.uni mask, pred
def int_nvvm_vote_uni_sync :
  Intrinsic<[llvm_i1_ty], [llvm_i32_ty, llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.uni.sync">,
  ClangBuiltin<"__nvvm_vote_uni_sync">;
// vote.sync.ballot mask, pred
def int_nvvm_vote_ballot_sync :
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i1_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.vote.ballot.sync">,
  ClangBuiltin<"__nvvm_vote_ballot_sync">;

//
// ACTIVEMASK
//
def int_nvvm_activemask :
  Intrinsic<[llvm_i32_ty], [],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback, IntrHasSideEffects], "llvm.nvvm.activemask">,
  ClangBuiltin<"__nvvm_activemask">;

//
// MATCH.SYNC
//
// match.any.sync.b32 mask, value
def int_nvvm_match_any_sync_i32 :
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.match.any.sync.i32">,
  ClangBuiltin<"__nvvm_match_any_sync_i32">;
// match.any.sync.b64 mask, value
def int_nvvm_match_any_sync_i64 :
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i64_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.match.any.sync.i64">,
  ClangBuiltin<"__nvvm_match_any_sync_i64">;

// match.all instruction have two variants -- one returns a single value, another
// returns a pair {value, predicate}. We currently only implement the latter as
// that's the variant exposed by CUDA API.

// match.all.sync.b32p mask, value
def int_nvvm_match_all_sync_i32p :
  Intrinsic<[llvm_i32_ty, llvm_i1_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.match.all.sync.i32p">;
// match.all.sync.b64p mask, value
def int_nvvm_match_all_sync_i64p :
  Intrinsic<[llvm_i32_ty, llvm_i1_ty], [llvm_i32_ty, llvm_i64_ty],
            [IntrInaccessibleMemOnly, IntrConvergent, IntrNoCallback], "llvm.nvvm.match.all.sync.i64p">;

//
// REDUX.SYNC
//
// redux.sync.min.u32 dst, src, membermask;
def int_nvvm_redux_sync_umin : ClangBuiltin<"__nvvm_redux_sync_umin">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

// redux.sync.max.u32 dst, src, membermask;
def int_nvvm_redux_sync_umax : ClangBuiltin<"__nvvm_redux_sync_umax">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

// redux.sync.add.s32 dst, src, membermask;
def int_nvvm_redux_sync_add : ClangBuiltin<"__nvvm_redux_sync_add">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

// redux.sync.min.s32 dst, src, membermask;
def int_nvvm_redux_sync_min : ClangBuiltin<"__nvvm_redux_sync_min">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

// redux.sync.max.s32 dst, src, membermask;
def int_nvvm_redux_sync_max : ClangBuiltin<"__nvvm_redux_sync_max">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

// redux.sync.and.b32 dst, src, membermask;
def int_nvvm_redux_sync_and : ClangBuiltin<"__nvvm_redux_sync_and">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

// redux.sync.xor.b32 dst, src, membermask;
def int_nvvm_redux_sync_xor : ClangBuiltin<"__nvvm_redux_sync_xor">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

// redux.sync.or.b32 dst, src, membermask;
def int_nvvm_redux_sync_or : ClangBuiltin<"__nvvm_redux_sync_or">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrInaccessibleMemOnly, IntrNoCallback]>;

//
// WMMA instructions
//
// WMMA.LOAD
class NVVM_WMMA_LD<WMMA_REGS Frag, string Layout, int WithStride>
  : Intrinsic<Frag.regs,
              !if(WithStride, [llvm_anyptr_ty, llvm_i32_ty], [llvm_anyptr_ty]),
              [IntrReadMem, IntrArgMemOnly, IntrNoCallback, ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>],
              WMMA_NAME_LDST<"load", Frag, Layout, WithStride>.intr>;

// WMMA.STORE.D
class NVVM_WMMA_ST<WMMA_REGS Frag, string Layout, int WithStride>
  : Intrinsic<[],
              !listconcat(
                [llvm_anyptr_ty],
                Frag.regs,
                !if(WithStride, [llvm_i32_ty], [])),
              [IntrWriteMem, IntrArgMemOnly, IntrNoCallback, WriteOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>],
              WMMA_NAME_LDST<"store", Frag, Layout, WithStride>.intr>;

// Create all load/store variants
foreach layout = ["row", "col"] in {
  foreach stride = [0, 1] in {
    foreach frag = NVVM_MMA_OPS.all_ld_ops in
      if NVVM_WMMA_LDST_SUPPORTED<frag, layout>.ret then
        def WMMA_NAME_LDST<"load", frag, layout, stride>.record
             : NVVM_WMMA_LD<frag, layout, stride>;
    foreach frag = NVVM_MMA_OPS.all_st_ops in
      if NVVM_WMMA_LDST_SUPPORTED<frag, layout>.ret then
        def WMMA_NAME_LDST<"store", frag, layout, stride>.record
             : NVVM_WMMA_ST<frag, layout, stride>;
  }
}

// WMMA.MMA
class NVVM_WMMA_MMA<string ALayout, string BLayout, int Satfinite, string rnd, string b1op,
                    WMMA_REGS A, WMMA_REGS B,
                    WMMA_REGS C, WMMA_REGS D>
  : Intrinsic<D.regs,
              !listconcat(A.regs, B.regs, C.regs),
              [IntrNoMem, IntrNoCallback],
              WMMA_NAME<ALayout, BLayout, Satfinite, rnd, b1op, A, B, C, D>.llvm>;

foreach layout_a = ["row", "col"] in {
  foreach layout_b = ["row", "col"] in {
    foreach satf = [0, 1] in {
      foreach rnd = ["", "rn", "rz", "rm", "rp"] in {
        foreach op = NVVM_MMA_OPS.all_wmma_ops in {
          foreach b1op = NVVM_MMA_B1OPS<op>.ret in {
            if NVVM_WMMA_SUPPORTED<op, layout_a, layout_b, satf, rnd>.ret then {
              def WMMA_NAME<layout_a, layout_b, satf, rnd, b1op,
                                op[0], op[1], op[2], op[3]>.record
                : NVVM_WMMA_MMA<layout_a, layout_b, satf, rnd, b1op,
                                op[0], op[1], op[2], op[3]>;
            }
          } // b1op
        } // op
      } // rnd
    } // satf
  } // layout_b
} // layout_a

// MMA
class NVVM_MMA<string ALayout, string BLayout, int Satfinite, string b1op,
               WMMA_REGS A, WMMA_REGS B, WMMA_REGS C, WMMA_REGS D>
  : Intrinsic<D.regs,
              !listconcat(A.regs, B.regs, C.regs),
              [IntrNoMem, IntrNoCallback],
              MMA_NAME<ALayout, BLayout, Satfinite, b1op, A, B, C, D>.llvm>;

foreach layout_a = ["row", "col"] in {
  foreach layout_b = ["row", "col"] in {
    foreach satf = [0, 1] in {
      foreach op = NVVM_MMA_OPS.all_mma_ops in {
        foreach b1op = NVVM_MMA_B1OPS<op>.ret in {
          if NVVM_MMA_SUPPORTED<op, layout_a, layout_b, satf>.ret then {
            def MMA_NAME<layout_a, layout_b, satf, b1op, op[0], op[1], op[2], op[3]>.record
              : NVVM_MMA<layout_a, layout_b, satf, b1op, op[0], op[1], op[2], op[3]>;
          }
        } // b1op
      } // op
    } // satf
  } // layout_b
} // layout_a

// LDMATRIX
class NVVM_LDMATRIX<WMMA_REGS Frag, int Transposed>
  : Intrinsic<Frag.regs, [llvm_anyptr_ty],
              [IntrReadMem, IntrArgMemOnly, IntrNoCallback, ReadOnly<ArgIndex<0>>,
               NoCapture<ArgIndex<0>>],
              LDMATRIX_NAME<Frag, Transposed>.intr>;

foreach transposed = [0, 1] in {
  foreach frag = NVVM_MMA_OPS.all_ldmatrix_ops in {
    if NVVM_LDMATRIX_SUPPORTED<frag>.ret then {
      def LDMATRIX_NAME<frag, transposed>.record
        : NVVM_LDMATRIX<frag, transposed>;
    }
  }
}

def int_nvvm_mapa
  : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.mapa">;
def int_nvvm_mapa_shared_cluster
  : DefaultAttrsIntrinsic<[llvm_shared_ptr_ty], [llvm_shared_ptr_ty, llvm_i32_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.mapa.shared.cluster">;
def int_nvvm_getctarank
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_ptr_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.getctarank">;
def int_nvvm_getctarank_shared_cluster
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_shared_ptr_ty],
              [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>],
              "llvm.nvvm.getctarank.shared.cluster">;
def int_nvvm_is_explicit_cluster
  : DefaultAttrsIntrinsic<[llvm_i1_ty], [],
              [IntrNoMem, IntrSpeculatable, NoUndef<RetIndex>],
              "llvm.nvvm.is_explicit_cluster">;

// Setmaxnreg inc/dec intrinsics
def int_nvvm_setmaxnreg_inc_sync_aligned_u32
  : DefaultAttrsIntrinsic<[], [llvm_i32_ty],
              [IntrConvergent, IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>],
              "llvm.nvvm.setmaxnreg.inc.sync.aligned.u32">;
def int_nvvm_setmaxnreg_dec_sync_aligned_u32
  : DefaultAttrsIntrinsic<[], [llvm_i32_ty],
              [IntrConvergent, IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>],
              "llvm.nvvm.setmaxnreg.dec.sync.aligned.u32">;

// Exit
def int_nvvm_exit : ClangBuiltin<"__nvvm_exit">,
    Intrinsic<[], [], [IntrConvergent, IntrInaccessibleMemOnly, IntrNoReturn]>;

} // let TargetPrefix = "nvvm"
