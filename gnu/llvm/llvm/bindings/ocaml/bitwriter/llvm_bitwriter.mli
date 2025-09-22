(*===-- llvm_bitwriter.mli - LLVM OCaml Interface -------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

(** Bitcode writer.

    This interface provides an OCaml API for the LLVM bitcode writer, the
    classes in the Bitwriter library. *)

(** [write_bitcode_file m path] writes the bitcode for module [m] to the file at
    [path]. Returns [true] if successful, [false] otherwise. *)
external write_bitcode_file
  : Llvm.llmodule -> string -> bool
  = "llvm_write_bitcode_file"

(** [write_bitcode_to_fd ~unbuffered fd m] writes the bitcode for module
    [m] to the channel [c]. If [unbuffered] is [true], after every write the fd
    will be flushed. Returns [true] if successful, [false] otherwise. *)
external write_bitcode_to_fd
  : ?unbuffered:bool -> Llvm.llmodule -> Unix.file_descr -> bool
  = "llvm_write_bitcode_to_fd"

(** [write_bitcode_to_memory_buffer m] returns a memory buffer containing
    the bitcode for module [m]. *)
external write_bitcode_to_memory_buffer
  : Llvm.llmodule -> Llvm.llmemorybuffer
  = "llvm_write_bitcode_to_memory_buffer"

(** [output_bitcode ~unbuffered c m] writes the bitcode for module [m]
    to the channel [c]. If [unbuffered] is [true], after every write the fd
    will be flushed. Returns [true] if successful, [false] otherwise. *)
val output_bitcode : ?unbuffered:bool -> out_channel -> Llvm.llmodule -> bool
