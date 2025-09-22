//===- Binary.cpp - A generic binary file ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Binary class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/Binary.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/Minidump.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Object/TapiUniversal.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <system_error>

using namespace llvm;
using namespace object;

Binary::~Binary() = default;

Binary::Binary(unsigned int Type, MemoryBufferRef Source)
    : TypeID(Type), Data(Source) {}

StringRef Binary::getData() const { return Data.getBuffer(); }

StringRef Binary::getFileName() const { return Data.getBufferIdentifier(); }

MemoryBufferRef Binary::getMemoryBufferRef() const { return Data; }

Expected<std::unique_ptr<Binary>> object::createBinary(MemoryBufferRef Buffer,
                                                       LLVMContext *Context,
                                                       bool InitContent) {
  file_magic Type = identify_magic(Buffer.getBuffer());

  switch (Type) {
  case file_magic::archive:
    return Archive::create(Buffer);
  case file_magic::elf:
  case file_magic::elf_relocatable:
  case file_magic::elf_executable:
  case file_magic::elf_shared_object:
  case file_magic::elf_core:
  case file_magic::goff_object:
  case file_magic::macho_object:
  case file_magic::macho_executable:
  case file_magic::macho_fixed_virtual_memory_shared_lib:
  case file_magic::macho_core:
  case file_magic::macho_preload_executable:
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamic_linker:
  case file_magic::macho_bundle:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::macho_dsym_companion:
  case file_magic::macho_kext_bundle:
  case file_magic::macho_file_set:
  case file_magic::coff_object:
  case file_magic::coff_import_library:
  case file_magic::pecoff_executable:
  case file_magic::bitcode:
  case file_magic::xcoff_object_32:
  case file_magic::xcoff_object_64:
  case file_magic::wasm_object:
    return ObjectFile::createSymbolicFile(Buffer, Type, Context, InitContent);
  case file_magic::macho_universal_binary:
    return MachOUniversalBinary::create(Buffer);
  case file_magic::windows_resource:
    return WindowsResource::createWindowsResource(Buffer);
  case file_magic::pdb:
    // PDB does not support the Binary interface.
    return errorCodeToError(object_error::invalid_file_type);
  case file_magic::unknown:
  case file_magic::clang_ast:
  case file_magic::cuda_fatbinary:
  case file_magic::coff_cl_gl_object:
  case file_magic::dxcontainer_object:
  case file_magic::offload_bundle:
  case file_magic::offload_bundle_compressed:
  case file_magic::spirv_object:
    // Unrecognized object file format.
    return errorCodeToError(object_error::invalid_file_type);
  case file_magic::offload_binary:
    return OffloadBinary::create(Buffer);
  case file_magic::minidump:
    return MinidumpFile::create(Buffer);
  case file_magic::tapi_file:
    return TapiUniversal::create(Buffer);
  }
  llvm_unreachable("Unexpected Binary File Type");
}

Expected<OwningBinary<Binary>>
object::createBinary(StringRef Path, LLVMContext *Context, bool InitContent) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(Path, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (std::error_code EC = FileOrErr.getError())
    return errorCodeToError(EC);
  std::unique_ptr<MemoryBuffer> &Buffer = FileOrErr.get();

  Expected<std::unique_ptr<Binary>> BinOrErr =
      createBinary(Buffer->getMemBufferRef(), Context, InitContent);
  if (!BinOrErr)
    return BinOrErr.takeError();
  std::unique_ptr<Binary> &Bin = BinOrErr.get();

  return OwningBinary<Binary>(std::move(Bin), std::move(Buffer));
}
