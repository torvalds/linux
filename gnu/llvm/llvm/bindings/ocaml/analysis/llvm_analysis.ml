(*===-- llvm_analysis.ml - LLVM OCaml Interface ---------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)


external verify_module : Llvm.llmodule -> string option = "llvm_verify_module"

external verify_function : Llvm.llvalue -> bool = "llvm_verify_function"

external assert_valid_module : Llvm.llmodule -> unit
                             = "llvm_assert_valid_module"

external assert_valid_function : Llvm.llvalue -> unit
                               = "llvm_assert_valid_function"
external view_function_cfg : Llvm.llvalue -> unit = "llvm_view_function_cfg"
external view_function_cfg_only : Llvm.llvalue -> unit
                                = "llvm_view_function_cfg_only"
