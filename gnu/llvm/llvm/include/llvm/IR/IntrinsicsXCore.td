//==- IntrinsicsXCore.td - XCore intrinsics                 -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the XCore-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "xcore" in {  // All intrinsics start with "llvm.xcore.".
  // Miscellaneous instructions.
  def int_xcore_bitrev : Intrinsic<[llvm_i32_ty],[llvm_i32_ty],[IntrNoMem]>,
                         ClangBuiltin<"__builtin_bitrev">;
  def int_xcore_crc8 : Intrinsic<[llvm_i32_ty, llvm_i32_ty],
                                 [llvm_i32_ty,llvm_i32_ty,llvm_i32_ty],
                                 [IntrNoMem]>;
  def int_xcore_crc32 : Intrinsic<[llvm_i32_ty],
                                  [llvm_i32_ty,llvm_i32_ty,llvm_i32_ty],
                                  [IntrNoMem]>;
  def int_xcore_sext : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                                 [IntrNoMem]>;
  def int_xcore_zext : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                                 [IntrNoMem]>;
  def int_xcore_getid : Intrinsic<[llvm_i32_ty],[],[IntrNoMem]>,
                        ClangBuiltin<"__builtin_getid">;
  def int_xcore_getps : Intrinsic<[llvm_i32_ty],[llvm_i32_ty]>,
                        ClangBuiltin<"__builtin_getps">;
  def int_xcore_setps : Intrinsic<[],[llvm_i32_ty, llvm_i32_ty]>,
                        ClangBuiltin<"__builtin_setps">;
  def int_xcore_geted : Intrinsic<[llvm_i32_ty],[]>;
  def int_xcore_getet : Intrinsic<[llvm_i32_ty],[]>;
  def int_xcore_setsr : Intrinsic<[],[llvm_i32_ty]>;
  def int_xcore_clrsr : Intrinsic<[],[llvm_i32_ty]>;

  // Resource instructions.
  def int_xcore_getr : Intrinsic<[llvm_anyptr_ty],[llvm_i32_ty]>;
  def int_xcore_freer : Intrinsic<[],[llvm_anyptr_ty],
                                   [NoCapture<ArgIndex<0>>]>;
  def int_xcore_in : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],[NoCapture<ArgIndex<0>>]>;
  def int_xcore_int : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],
                                [NoCapture<ArgIndex<0>>]>;
  def int_xcore_inct : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],
                                 [NoCapture<ArgIndex<0>>]>;
  def int_xcore_out : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                [NoCapture<ArgIndex<0>>]>;
  def int_xcore_outt : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                 [NoCapture<ArgIndex<0>>]>;
  def int_xcore_outct : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_chkct : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_testct : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],
                                   [NoCapture<ArgIndex<0>>]>;
  def int_xcore_testwct : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],
                                    [NoCapture<ArgIndex<0>>]>;
  def int_xcore_setd : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_setc : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_inshr : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_outshr : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_setpt : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_clrpt : Intrinsic<[],[llvm_anyptr_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_getts : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_syncr : Intrinsic<[],[llvm_anyptr_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_settw : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_setv : Intrinsic<[],[llvm_anyptr_ty, llvm_ptr_ty],
                                 [NoCapture<ArgIndex<0>>]>;
  def int_xcore_setev : Intrinsic<[],[llvm_anyptr_ty, llvm_ptr_ty],
                                  [NoCapture<ArgIndex<0>>]>;
  def int_xcore_eeu : Intrinsic<[],[llvm_anyptr_ty], [NoCapture<ArgIndex<0>>]>;
  def int_xcore_edu : Intrinsic<[],[llvm_anyptr_ty], [NoCapture<ArgIndex<0>>]>;
  def int_xcore_setclk : Intrinsic<[],[llvm_anyptr_ty, llvm_anyptr_ty],
                                   [NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>]>;
  def int_xcore_setrdy : Intrinsic<[],[llvm_anyptr_ty, llvm_anyptr_ty],
                                   [NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>]>;
  def int_xcore_setpsc : Intrinsic<[],[llvm_anyptr_ty, llvm_i32_ty],
                                   [NoCapture<ArgIndex<0>>]>;
  def int_xcore_peek : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],
                                 [NoCapture<ArgIndex<0>>]>;
  def int_xcore_endin : Intrinsic<[llvm_i32_ty],[llvm_anyptr_ty],
                                 [NoCapture<ArgIndex<0>>]>;

  // Intrinsics for events.
  def int_xcore_waitevent : Intrinsic<[llvm_ptr_ty],[], [IntrReadMem]>;

  // If any of the resources owned by the thread are ready this returns the
  // vector of one of the ready resources. If no resources owned by the thread
  // are ready then the operand passed to the intrinsic is returned.
  def int_xcore_checkevent : Intrinsic<[llvm_ptr_ty],[llvm_ptr_ty]>;

  def int_xcore_clre : Intrinsic<[],[],[]>;

  // Intrinsics for threads.
  def int_xcore_getst : Intrinsic <[llvm_anyptr_ty],[llvm_anyptr_ty],
                                   [NoCapture<ArgIndex<0>>]>;
  def int_xcore_msync : Intrinsic <[],[llvm_anyptr_ty], [NoCapture<ArgIndex<0>>]>;
  def int_xcore_ssync : Intrinsic <[],[]>;
  def int_xcore_mjoin : Intrinsic <[],[llvm_anyptr_ty], [NoCapture<ArgIndex<0>>]>;
  def int_xcore_initsp : Intrinsic <[],[llvm_anyptr_ty, llvm_ptr_ty],
                                    [NoCapture<ArgIndex<0>>]>;
  def int_xcore_initpc : Intrinsic <[],[llvm_anyptr_ty, llvm_ptr_ty],
                                    [NoCapture<ArgIndex<0>>]>;
  def int_xcore_initlr : Intrinsic <[],[llvm_anyptr_ty, llvm_ptr_ty],
                                    [NoCapture<ArgIndex<0>>]>;
  def int_xcore_initcp : Intrinsic <[],[llvm_anyptr_ty, llvm_ptr_ty],
                                    [NoCapture<ArgIndex<0>>]>;
  def int_xcore_initdp : Intrinsic <[],[llvm_anyptr_ty, llvm_ptr_ty],
                                    [NoCapture<ArgIndex<0>>]>;
}
