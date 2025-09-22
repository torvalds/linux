//===-------------- ELF.cpp - JIT linker function for ELF -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF jit-link function.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ExecutionEngine/JITLink/ELF_aarch32.h"
#include "llvm/ExecutionEngine/JITLink/ELF_aarch64.h"
#include "llvm/ExecutionEngine/JITLink/ELF_i386.h"
#include "llvm/ExecutionEngine/JITLink/ELF_loongarch.h"
#include "llvm/ExecutionEngine/JITLink/ELF_ppc64.h"
#include "llvm/ExecutionEngine/JITLink/ELF_riscv.h"
#include "llvm/ExecutionEngine/JITLink/ELF_x86_64.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstring>

using namespace llvm;

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

Expected<uint16_t> readTargetMachineArch(StringRef Buffer) {
  const char *Data = Buffer.data();

  if (Data[ELF::EI_DATA] == ELF::ELFDATA2LSB) {
    if (Data[ELF::EI_CLASS] == ELF::ELFCLASS64) {
      if (auto File = llvm::object::ELF64LEFile::create(Buffer)) {
        return File->getHeader().e_machine;
      } else {
        return File.takeError();
      }
    } else if (Data[ELF::EI_CLASS] == ELF::ELFCLASS32) {
      if (auto File = llvm::object::ELF32LEFile::create(Buffer)) {
        return File->getHeader().e_machine;
      } else {
        return File.takeError();
      }
    }
  }

  if (Data[ELF::EI_DATA] == ELF::ELFDATA2MSB) {
    if (Data[ELF::EI_CLASS] == ELF::ELFCLASS64) {
      if (auto File = llvm::object::ELF64BEFile::create(Buffer)) {
        return File->getHeader().e_machine;
      } else {
        return File.takeError();
      }
    } else if (Data[ELF::EI_CLASS] == ELF::ELFCLASS32) {
      if (auto File = llvm::object::ELF32BEFile::create(Buffer)) {
        return File->getHeader().e_machine;
      } else {
        return File.takeError();
      }
    }
  }

  return ELF::EM_NONE;
}

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject(MemoryBufferRef ObjectBuffer) {
  StringRef Buffer = ObjectBuffer.getBuffer();
  if (Buffer.size() < ELF::EI_NIDENT)
    return make_error<JITLinkError>("Truncated ELF buffer");

  if (memcmp(Buffer.data(), ELF::ElfMagic, strlen(ELF::ElfMagic)) != 0)
    return make_error<JITLinkError>("ELF magic not valid");

  uint8_t DataEncoding = Buffer.data()[ELF::EI_DATA];
  Expected<uint16_t> TargetMachineArch = readTargetMachineArch(Buffer);
  if (!TargetMachineArch)
    return TargetMachineArch.takeError();

  switch (*TargetMachineArch) {
  case ELF::EM_AARCH64:
    return createLinkGraphFromELFObject_aarch64(ObjectBuffer);
  case ELF::EM_ARM:
    return createLinkGraphFromELFObject_aarch32(ObjectBuffer);
  case ELF::EM_LOONGARCH:
    return createLinkGraphFromELFObject_loongarch(ObjectBuffer);
  case ELF::EM_PPC64: {
    if (DataEncoding == ELF::ELFDATA2LSB)
      return createLinkGraphFromELFObject_ppc64le(ObjectBuffer);
    else
      return createLinkGraphFromELFObject_ppc64(ObjectBuffer);
  }
  case ELF::EM_RISCV:
    return createLinkGraphFromELFObject_riscv(ObjectBuffer);
  case ELF::EM_X86_64:
    return createLinkGraphFromELFObject_x86_64(ObjectBuffer);
  case ELF::EM_386:
    return createLinkGraphFromELFObject_i386(ObjectBuffer);
  default:
    return make_error<JITLinkError>(
        "Unsupported target machine architecture in ELF object " +
        ObjectBuffer.getBufferIdentifier());
  }
}

void link_ELF(std::unique_ptr<LinkGraph> G,
              std::unique_ptr<JITLinkContext> Ctx) {
  switch (G->getTargetTriple().getArch()) {
  case Triple::aarch64:
    link_ELF_aarch64(std::move(G), std::move(Ctx));
    return;
  case Triple::arm:
  case Triple::armeb:
  case Triple::thumb:
  case Triple::thumbeb:
    link_ELF_aarch32(std::move(G), std::move(Ctx));
    return;
  case Triple::loongarch32:
  case Triple::loongarch64:
    link_ELF_loongarch(std::move(G), std::move(Ctx));
    return;
  case Triple::ppc64:
    link_ELF_ppc64(std::move(G), std::move(Ctx));
    return;
  case Triple::ppc64le:
    link_ELF_ppc64le(std::move(G), std::move(Ctx));
    return;
  case Triple::riscv32:
  case Triple::riscv64:
    link_ELF_riscv(std::move(G), std::move(Ctx));
    return;
  case Triple::x86_64:
    link_ELF_x86_64(std::move(G), std::move(Ctx));
    return;
  case Triple::x86:
    link_ELF_i386(std::move(G), std::move(Ctx));
    return;
  default:
    Ctx->notifyFailed(make_error<JITLinkError>(
        "Unsupported target machine architecture in ELF link graph " +
        G->getName()));
    return;
  }
}

} // end namespace jitlink
} // end namespace llvm
