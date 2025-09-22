// Define intrinsics written by hand

// VEL Intrinsic instructions.
let TargetPrefix = "ve" in {
  def int_ve_vl_pack_f32p : ClangBuiltin<"__builtin_ve_vl_pack_f32p">,
                            Intrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_ptr_ty],
                                      [IntrReadMem]>;
  def int_ve_vl_pack_f32a : ClangBuiltin<"__builtin_ve_vl_pack_f32a">,
                            Intrinsic<[llvm_i64_ty], [llvm_ptr_ty],
                                      [IntrReadMem]>;

  def int_ve_vl_extract_vm512u :
      ClangBuiltin<"__builtin_ve_vl_extract_vm512u">,
      Intrinsic<[LLVMType<v256i1>], [LLVMType<v512i1>], [IntrNoMem]>;

  def int_ve_vl_extract_vm512l :
      ClangBuiltin<"__builtin_ve_vl_extract_vm512l">,
      Intrinsic<[LLVMType<v256i1>], [LLVMType<v512i1>], [IntrNoMem]>;

  def int_ve_vl_insert_vm512u :
      ClangBuiltin<"__builtin_ve_vl_insert_vm512u">,
      Intrinsic<[LLVMType<v512i1>], [LLVMType<v512i1>, LLVMType<v256i1>],
                [IntrNoMem]>;

  def int_ve_vl_insert_vm512l :
      ClangBuiltin<"__builtin_ve_vl_insert_vm512l">,
      Intrinsic<[LLVMType<v512i1>], [LLVMType<v512i1>, LLVMType<v256i1>],
                [IntrNoMem]>;
}

// Define intrinsics automatically generated
include "llvm/IR/IntrinsicsVEVL.gen.td"
