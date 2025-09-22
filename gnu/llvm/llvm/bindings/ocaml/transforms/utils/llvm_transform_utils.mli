(*===-- llvm_transform_utils.mli - LLVM OCaml Interface -------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

(** Transform Utilities.

    This interface provides an OCaml API for LLVM transform utilities, the
    classes in the [LLVMTransformUtils] library. *)

(** [clone_module m] returns an exact copy of module [m].
    See the [llvm::CloneModule] function. *)
external clone_module : Llvm.llmodule -> Llvm.llmodule = "llvm_clone_module"
