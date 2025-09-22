//===- llvm/BinaryFormat/Magic.h - File magic identification ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MAGIC_H
#define LLVM_BINARYFORMAT_MAGIC_H

#include <system_error>

namespace llvm {
class StringRef;
class Twine;

/// file_magic - An "enum class" enumeration of file types based on magic (the
/// first N bytes of the file).
struct file_magic {
  enum Impl {
    unknown = 0,       ///< Unrecognized file
    bitcode,           ///< Bitcode file
    clang_ast,         ///< Clang PCH or PCM
    archive,           ///< ar style archive file
    elf,               ///< ELF Unknown type
    elf_relocatable,   ///< ELF Relocatable object file
    elf_executable,    ///< ELF Executable image
    elf_shared_object, ///< ELF dynamically linked shared lib
    elf_core,          ///< ELF core image
    goff_object,       ///< GOFF object file
    macho_object,      ///< Mach-O Object file
    macho_executable,  ///< Mach-O Executable
    macho_fixed_virtual_memory_shared_lib,    ///< Mach-O Shared Lib, FVM
    macho_core,                               ///< Mach-O Core File
    macho_preload_executable,                 ///< Mach-O Preloaded Executable
    macho_dynamically_linked_shared_lib,      ///< Mach-O dynlinked shared lib
    macho_dynamic_linker,                     ///< The Mach-O dynamic linker
    macho_bundle,                             ///< Mach-O Bundle file
    macho_dynamically_linked_shared_lib_stub, ///< Mach-O Shared lib stub
    macho_dsym_companion,                     ///< Mach-O dSYM companion file
    macho_kext_bundle,                        ///< Mach-O kext bundle file
    macho_universal_binary,                   ///< Mach-O universal binary
    macho_file_set,                           ///< Mach-O file set binary
    minidump,                                 ///< Windows minidump file
    coff_cl_gl_object,         ///< Microsoft cl.exe's intermediate code file
    coff_object,               ///< COFF object file
    coff_import_library,       ///< COFF import library
    pecoff_executable,         ///< PECOFF executable file
    windows_resource,          ///< Windows compiled resource file (.res)
    xcoff_object_32,           ///< 32-bit XCOFF object file
    xcoff_object_64,           ///< 64-bit XCOFF object file
    wasm_object,               ///< WebAssembly Object file
    pdb,                       ///< Windows PDB debug info file
    tapi_file,                 ///< Text-based Dynamic Library Stub file
    cuda_fatbinary,            ///< CUDA Fatbinary object file
    offload_binary,            ///< LLVM offload object file
    dxcontainer_object,        ///< DirectX container file
    offload_bundle,            ///< Clang offload bundle file
    offload_bundle_compressed, ///< Compressed clang offload bundle file
    spirv_object,              ///< A binary SPIR-V file
  };

  bool is_object() const { return V != unknown; }

  file_magic() = default;
  file_magic(Impl V) : V(V) {}
  operator Impl() const { return V; }

private:
  Impl V = unknown;
};

/// Identify the type of a binary file based on how magical it is.
file_magic identify_magic(StringRef magic);

/// Get and identify \a path's type based on its content.
///
/// @param path Input path.
/// @param result Set to the type of file, or file_magic::unknown.
/// @returns errc::success if result has been successfully set, otherwise a
///          platform-specific error_code.
std::error_code identify_magic(const Twine &path, file_magic &result);
} // namespace llvm

#endif
