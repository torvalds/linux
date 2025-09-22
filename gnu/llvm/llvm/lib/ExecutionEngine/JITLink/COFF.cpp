//===-------------- COFF.cpp - JIT linker function for COFF -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// COFF jit-link function.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/COFF.h"

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/ExecutionEngine/JITLink/COFF_x86_64.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstring>

using namespace llvm;

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

static StringRef getMachineName(uint16_t Machine) {
  switch (Machine) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return "i386";
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return "x86_64";
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return "ARM";
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return "ARM64";
  default:
    return "unknown";
  }
}

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromCOFFObject(MemoryBufferRef ObjectBuffer) {
  StringRef Data = ObjectBuffer.getBuffer();

  // Check magic
  auto Magic = identify_magic(ObjectBuffer.getBuffer());
  if (Magic != file_magic::coff_object)
    return make_error<JITLinkError>("Invalid COFF buffer");

  if (Data.size() < sizeof(object::coff_file_header))
    return make_error<JITLinkError>("Truncated COFF buffer");

  uint64_t CurPtr = 0;
  bool IsPE = false;

  // Check if this is a PE/COFF file.
  if (Data.size() >= sizeof(object::dos_header) + sizeof(COFF::PEMagic)) {
    const auto *DH =
        reinterpret_cast<const object::dos_header *>(Data.data() + CurPtr);
    if (DH->Magic[0] == 'M' && DH->Magic[1] == 'Z') {
      // Check the PE magic bytes. ("PE\0\0")
      CurPtr = DH->AddressOfNewExeHeader;
      if (memcmp(Data.data() + CurPtr, COFF::PEMagic, sizeof(COFF::PEMagic)) !=
          0) {
        return make_error<JITLinkError>("Incorrect PE magic");
      }
      CurPtr += sizeof(COFF::PEMagic);
      IsPE = true;
    }
  }
  if (Data.size() < CurPtr + sizeof(object::coff_file_header))
    return make_error<JITLinkError>("Truncated COFF buffer");

  const object::coff_file_header *COFFHeader =
      reinterpret_cast<const object::coff_file_header *>(Data.data() + CurPtr);
  const object::coff_bigobj_file_header *COFFBigObjHeader = nullptr;

  // Deal with bigobj file
  if (!IsPE && COFFHeader->Machine == COFF::IMAGE_FILE_MACHINE_UNKNOWN &&
      COFFHeader->NumberOfSections == uint16_t(0xffff) &&
      Data.size() >= sizeof(object::coff_bigobj_file_header)) {
    if (Data.size() < sizeof(object::coff_file_header)) {
      return make_error<JITLinkError>("Truncated COFF buffer");
    }
    COFFBigObjHeader =
        reinterpret_cast<const object::coff_bigobj_file_header *>(Data.data() +
                                                                  CurPtr);

    // Verify that we are dealing with bigobj.
    if (COFFBigObjHeader->Version >= COFF::BigObjHeader::MinBigObjectVersion &&
        std::memcmp(COFFBigObjHeader->UUID, COFF::BigObjMagic,
                    sizeof(COFF::BigObjMagic)) == 0) {
      COFFHeader = nullptr;
      CurPtr += sizeof(object::coff_bigobj_file_header);
    } else
      COFFBigObjHeader = nullptr;
  }

  uint16_t Machine =
      COFFHeader ? COFFHeader->Machine : COFFBigObjHeader->Machine;
  LLVM_DEBUG({
    dbgs() << "jitLink_COFF: PE = " << (IsPE ? "yes" : "no")
           << ", bigobj = " << (COFFBigObjHeader ? "yes" : "no")
           << ", identifier = \"" << ObjectBuffer.getBufferIdentifier() << "\" "
           << "machine = " << getMachineName(Machine) << "\n";
  });

  switch (Machine) {
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return createLinkGraphFromCOFFObject_x86_64(ObjectBuffer);
  default:
    return make_error<JITLinkError>(
        "Unsupported target machine architecture in COFF object " +
        ObjectBuffer.getBufferIdentifier() + ": " + getMachineName(Machine));
  }
}

void link_COFF(std::unique_ptr<LinkGraph> G,
               std::unique_ptr<JITLinkContext> Ctx) {
  switch (G->getTargetTriple().getArch()) {
  case Triple::x86_64:
    link_COFF_x86_64(std::move(G), std::move(Ctx));
    return;
  default:
    Ctx->notifyFailed(make_error<JITLinkError>(
        "Unsupported target machine architecture in COFF link graph " +
        G->getName()));
    return;
  }
}

} // end namespace jitlink
} // end namespace llvm
