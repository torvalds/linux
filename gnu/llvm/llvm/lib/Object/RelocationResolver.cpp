//===- RelocationResolver.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities to resolve relocations in object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/RelocationResolver.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>

namespace llvm {
namespace object {

static int64_t getELFAddend(RelocationRef R) {
  Expected<int64_t> AddendOrErr = ELFRelocationRef(R).getAddend();
  handleAllErrors(AddendOrErr.takeError(), [](const ErrorInfoBase &EI) {
    report_fatal_error(Twine(EI.message()));
  });
  return *AddendOrErr;
}

static bool supportsX86_64(uint64_t Type) {
  switch (Type) {
  case ELF::R_X86_64_NONE:
  case ELF::R_X86_64_64:
  case ELF::R_X86_64_DTPOFF32:
  case ELF::R_X86_64_DTPOFF64:
  case ELF::R_X86_64_PC32:
  case ELF::R_X86_64_PC64:
  case ELF::R_X86_64_32:
  case ELF::R_X86_64_32S:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveX86_64(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t LocData, int64_t Addend) {
  switch (Type) {
  case ELF::R_X86_64_NONE:
    return LocData;
  case ELF::R_X86_64_64:
  case ELF::R_X86_64_DTPOFF32:
  case ELF::R_X86_64_DTPOFF64:
    return S + Addend;
  case ELF::R_X86_64_PC32:
  case ELF::R_X86_64_PC64:
    return S + Addend - Offset;
  case ELF::R_X86_64_32:
  case ELF::R_X86_64_32S:
    return (S + Addend) & 0xFFFFFFFF;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsAArch64(uint64_t Type) {
  switch (Type) {
  case ELF::R_AARCH64_ABS32:
  case ELF::R_AARCH64_ABS64:
  case ELF::R_AARCH64_PREL16:
  case ELF::R_AARCH64_PREL32:
  case ELF::R_AARCH64_PREL64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveAArch64(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_AARCH64_ABS32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_AARCH64_ABS64:
    return S + Addend;
  case ELF::R_AARCH64_PREL16:
    return (S + Addend - Offset) & 0xFFFF;
  case ELF::R_AARCH64_PREL32:
    return (S + Addend - Offset) & 0xFFFFFFFF;
  case ELF::R_AARCH64_PREL64:
    return S + Addend - Offset;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsBPF(uint64_t Type) {
  switch (Type) {
  case ELF::R_BPF_64_ABS32:
  case ELF::R_BPF_64_ABS64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveBPF(uint64_t Type, uint64_t Offset, uint64_t S,
                           uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case ELF::R_BPF_64_ABS32:
    return (S + LocData) & 0xFFFFFFFF;
  case ELF::R_BPF_64_ABS64:
    return S + LocData;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsMips64(uint64_t Type) {
  switch (Type) {
  case ELF::R_MIPS_32:
  case ELF::R_MIPS_64:
  case ELF::R_MIPS_TLS_DTPREL64:
  case ELF::R_MIPS_PC32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveMips64(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_MIPS_32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_MIPS_64:
    return S + Addend;
  case ELF::R_MIPS_TLS_DTPREL64:
    return S + Addend - 0x8000;
  case ELF::R_MIPS_PC32:
    return S + Addend - Offset;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsMSP430(uint64_t Type) {
  switch (Type) {
  case ELF::R_MSP430_32:
  case ELF::R_MSP430_16_BYTE:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveMSP430(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_MSP430_32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_MSP430_16_BYTE:
    return (S + Addend) & 0xFFFF;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsPPC64(uint64_t Type) {
  switch (Type) {
  case ELF::R_PPC64_ADDR32:
  case ELF::R_PPC64_ADDR64:
  case ELF::R_PPC64_REL32:
  case ELF::R_PPC64_REL64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolvePPC64(uint64_t Type, uint64_t Offset, uint64_t S,
                             uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_PPC64_ADDR32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_PPC64_ADDR64:
    return S + Addend;
  case ELF::R_PPC64_REL32:
    return (S + Addend - Offset) & 0xFFFFFFFF;
  case ELF::R_PPC64_REL64:
    return S + Addend - Offset;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsSystemZ(uint64_t Type) {
  switch (Type) {
  case ELF::R_390_32:
  case ELF::R_390_64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveSystemZ(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_390_32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_390_64:
    return S + Addend;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsSparc64(uint64_t Type) {
  switch (Type) {
  case ELF::R_SPARC_32:
  case ELF::R_SPARC_64:
  case ELF::R_SPARC_UA32:
  case ELF::R_SPARC_UA64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveSparc64(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_SPARC_32:
  case ELF::R_SPARC_64:
  case ELF::R_SPARC_UA32:
  case ELF::R_SPARC_UA64:
    return S + Addend;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

/// Returns true if \c Obj is an AMDGPU code object based solely on the value
/// of e_machine.
///
/// AMDGPU code objects with an e_machine of EF_AMDGPU_MACH_NONE do not
/// identify their arch as either r600 or amdgcn, but we can still handle
/// their relocations. When we identify an ELF object with an UnknownArch,
/// we use isAMDGPU to check for this case.
static bool isAMDGPU(const ObjectFile &Obj) {
  if (const auto *ELFObj = dyn_cast<ELFObjectFileBase>(&Obj))
    return ELFObj->getEMachine() == ELF::EM_AMDGPU;
  return false;
}

static bool supportsAmdgpu(uint64_t Type) {
  switch (Type) {
  case ELF::R_AMDGPU_ABS32:
  case ELF::R_AMDGPU_ABS64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveAmdgpu(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_AMDGPU_ABS32:
  case ELF::R_AMDGPU_ABS64:
    return S + Addend;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsX86(uint64_t Type) {
  switch (Type) {
  case ELF::R_386_NONE:
  case ELF::R_386_32:
  case ELF::R_386_PC32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveX86(uint64_t Type, uint64_t Offset, uint64_t S,
                           uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case ELF::R_386_NONE:
    return LocData;
  case ELF::R_386_32:
    return S + LocData;
  case ELF::R_386_PC32:
    return S - Offset + LocData;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsPPC32(uint64_t Type) {
  switch (Type) {
  case ELF::R_PPC_ADDR32:
  case ELF::R_PPC_REL32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolvePPC32(uint64_t Type, uint64_t Offset, uint64_t S,
                             uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_PPC_ADDR32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_PPC_REL32:
    return (S + Addend - Offset) & 0xFFFFFFFF;
  }
  llvm_unreachable("Invalid relocation type");
}

static bool supportsARM(uint64_t Type) {
  switch (Type) {
  case ELF::R_ARM_ABS32:
  case ELF::R_ARM_REL32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveARM(uint64_t Type, uint64_t Offset, uint64_t S,
                           uint64_t LocData, int64_t Addend) {
  // Support both RELA and REL relocations. The caller is responsible
  // for supplying the correct values for LocData and Addend, i.e.
  // Addend == 0 for REL and LocData == 0 for RELA.
  assert((LocData == 0 || Addend == 0) &&
         "one of LocData and Addend must be 0");
  switch (Type) {
  case ELF::R_ARM_ABS32:
    return (S + LocData + Addend) & 0xFFFFFFFF;
  case ELF::R_ARM_REL32:
    return (S + LocData + Addend - Offset) & 0xFFFFFFFF;
  }
  llvm_unreachable("Invalid relocation type");
}

static bool supportsAVR(uint64_t Type) {
  switch (Type) {
  case ELF::R_AVR_16:
  case ELF::R_AVR_32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveAVR(uint64_t Type, uint64_t Offset, uint64_t S,
                           uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_AVR_16:
    return (S + Addend) & 0xFFFF;
  case ELF::R_AVR_32:
    return (S + Addend) & 0xFFFFFFFF;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsLanai(uint64_t Type) {
  return Type == ELF::R_LANAI_32;
}

static uint64_t resolveLanai(uint64_t Type, uint64_t Offset, uint64_t S,
                             uint64_t /*LocData*/, int64_t Addend) {
  if (Type == ELF::R_LANAI_32)
    return (S + Addend) & 0xFFFFFFFF;
  llvm_unreachable("Invalid relocation type");
}

static bool supportsMips32(uint64_t Type) {
  switch (Type) {
  case ELF::R_MIPS_32:
  case ELF::R_MIPS_TLS_DTPREL32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveMips32(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t LocData, int64_t /*Addend*/) {
  // FIXME: Take in account implicit addends to get correct results.
  if (Type == ELF::R_MIPS_32)
    return (S + LocData) & 0xFFFFFFFF;
  if (Type == ELF::R_MIPS_TLS_DTPREL32)
    return (S + LocData) & 0xFFFFFFFF;
  llvm_unreachable("Invalid relocation type");
}

static bool supportsSparc32(uint64_t Type) {
  switch (Type) {
  case ELF::R_SPARC_32:
  case ELF::R_SPARC_UA32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveSparc32(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t LocData, int64_t Addend) {
  if (Type == ELF::R_SPARC_32 || Type == ELF::R_SPARC_UA32)
    return S + Addend;
  return LocData;
}

static bool supportsHexagon(uint64_t Type) {
  return Type == ELF::R_HEX_32;
}

static uint64_t resolveHexagon(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t /*LocData*/, int64_t Addend) {
  if (Type == ELF::R_HEX_32)
    return S + Addend;
  llvm_unreachable("Invalid relocation type");
}

static bool supportsRISCV(uint64_t Type) {
  switch (Type) {
  case ELF::R_RISCV_NONE:
  case ELF::R_RISCV_32:
  case ELF::R_RISCV_32_PCREL:
  case ELF::R_RISCV_64:
  case ELF::R_RISCV_SET6:
  case ELF::R_RISCV_SET8:
  case ELF::R_RISCV_SUB6:
  case ELF::R_RISCV_ADD8:
  case ELF::R_RISCV_SUB8:
  case ELF::R_RISCV_SET16:
  case ELF::R_RISCV_ADD16:
  case ELF::R_RISCV_SUB16:
  case ELF::R_RISCV_SET32:
  case ELF::R_RISCV_ADD32:
  case ELF::R_RISCV_SUB32:
  case ELF::R_RISCV_ADD64:
  case ELF::R_RISCV_SUB64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveRISCV(uint64_t Type, uint64_t Offset, uint64_t S,
                             uint64_t LocData, int64_t Addend) {
  int64_t RA = Addend;
  uint64_t A = LocData;
  switch (Type) {
  case ELF::R_RISCV_NONE:
    return LocData;
  case ELF::R_RISCV_32:
    return (S + RA) & 0xFFFFFFFF;
  case ELF::R_RISCV_32_PCREL:
    return (S + RA - Offset) & 0xFFFFFFFF;
  case ELF::R_RISCV_64:
    return S + RA;
  case ELF::R_RISCV_SET6:
    return (A & 0xC0) | ((S + RA) & 0x3F);
  case ELF::R_RISCV_SUB6:
    return (A & 0xC0) | (((A & 0x3F) - (S + RA)) & 0x3F);
  case ELF::R_RISCV_SET8:
    return (S + RA) & 0xFF;
  case ELF::R_RISCV_ADD8:
    return (A + (S + RA)) & 0xFF;
  case ELF::R_RISCV_SUB8:
    return (A - (S + RA)) & 0xFF;
  case ELF::R_RISCV_SET16:
    return (S + RA) & 0xFFFF;
  case ELF::R_RISCV_ADD16:
    return (A + (S + RA)) & 0xFFFF;
  case ELF::R_RISCV_SUB16:
    return (A - (S + RA)) & 0xFFFF;
  case ELF::R_RISCV_SET32:
    return (S + RA) & 0xFFFFFFFF;
  case ELF::R_RISCV_ADD32:
    return (A + (S + RA)) & 0xFFFFFFFF;
  case ELF::R_RISCV_SUB32:
    return (A - (S + RA)) & 0xFFFFFFFF;
  case ELF::R_RISCV_ADD64:
    return (A + (S + RA));
  case ELF::R_RISCV_SUB64:
    return (A - (S + RA));
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsCSKY(uint64_t Type) {
  switch (Type) {
  case ELF::R_CKCORE_NONE:
  case ELF::R_CKCORE_ADDR32:
  case ELF::R_CKCORE_PCREL32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveCSKY(uint64_t Type, uint64_t Offset, uint64_t S,
                            uint64_t LocData, int64_t Addend) {
  switch (Type) {
  case ELF::R_CKCORE_NONE:
    return LocData;
  case ELF::R_CKCORE_ADDR32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_CKCORE_PCREL32:
    return (S + Addend - Offset) & 0xFFFFFFFF;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsLoongArch(uint64_t Type) {
  switch (Type) {
  case ELF::R_LARCH_NONE:
  case ELF::R_LARCH_32:
  case ELF::R_LARCH_32_PCREL:
  case ELF::R_LARCH_64:
  case ELF::R_LARCH_ADD6:
  case ELF::R_LARCH_SUB6:
  case ELF::R_LARCH_ADD8:
  case ELF::R_LARCH_SUB8:
  case ELF::R_LARCH_ADD16:
  case ELF::R_LARCH_SUB16:
  case ELF::R_LARCH_ADD32:
  case ELF::R_LARCH_SUB32:
  case ELF::R_LARCH_ADD64:
  case ELF::R_LARCH_SUB64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveLoongArch(uint64_t Type, uint64_t Offset, uint64_t S,
                                 uint64_t LocData, int64_t Addend) {
  switch (Type) {
  case ELF::R_LARCH_NONE:
    return LocData;
  case ELF::R_LARCH_32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_LARCH_32_PCREL:
    return (S + Addend - Offset) & 0xFFFFFFFF;
  case ELF::R_LARCH_64:
    return S + Addend;
  case ELF::R_LARCH_ADD6:
    return (LocData & 0xC0) | ((LocData + S + Addend) & 0x3F);
  case ELF::R_LARCH_SUB6:
    return (LocData & 0xC0) | ((LocData - (S + Addend)) & 0x3F);
  case ELF::R_LARCH_ADD8:
    return (LocData + (S + Addend)) & 0xFF;
  case ELF::R_LARCH_SUB8:
    return (LocData - (S + Addend)) & 0xFF;
  case ELF::R_LARCH_ADD16:
    return (LocData + (S + Addend)) & 0xFFFF;
  case ELF::R_LARCH_SUB16:
    return (LocData - (S + Addend)) & 0xFFFF;
  case ELF::R_LARCH_ADD32:
    return (LocData + (S + Addend)) & 0xFFFFFFFF;
  case ELF::R_LARCH_SUB32:
    return (LocData - (S + Addend)) & 0xFFFFFFFF;
  case ELF::R_LARCH_ADD64:
    return (LocData + (S + Addend));
  case ELF::R_LARCH_SUB64:
    return (LocData - (S + Addend));
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsCOFFX86(uint64_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_I386_SECREL:
  case COFF::IMAGE_REL_I386_DIR32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveCOFFX86(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case COFF::IMAGE_REL_I386_SECREL:
  case COFF::IMAGE_REL_I386_DIR32:
    return (S + LocData) & 0xFFFFFFFF;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsCOFFX86_64(uint64_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_AMD64_SECREL:
  case COFF::IMAGE_REL_AMD64_ADDR64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveCOFFX86_64(uint64_t Type, uint64_t Offset, uint64_t S,
                                  uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case COFF::IMAGE_REL_AMD64_SECREL:
    return (S + LocData) & 0xFFFFFFFF;
  case COFF::IMAGE_REL_AMD64_ADDR64:
    return S + LocData;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsCOFFARM(uint64_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_ARM_SECREL:
  case COFF::IMAGE_REL_ARM_ADDR32:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveCOFFARM(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case COFF::IMAGE_REL_ARM_SECREL:
  case COFF::IMAGE_REL_ARM_ADDR32:
    return (S + LocData) & 0xFFFFFFFF;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsCOFFARM64(uint64_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_ARM64_SECREL:
  case COFF::IMAGE_REL_ARM64_ADDR64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveCOFFARM64(uint64_t Type, uint64_t Offset, uint64_t S,
                                 uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case COFF::IMAGE_REL_ARM64_SECREL:
    return (S + LocData) & 0xFFFFFFFF;
  case COFF::IMAGE_REL_ARM64_ADDR64:
    return S + LocData;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsMachOX86_64(uint64_t Type) {
  return Type == MachO::X86_64_RELOC_UNSIGNED;
}

static uint64_t resolveMachOX86_64(uint64_t Type, uint64_t Offset, uint64_t S,
                                   uint64_t LocData, int64_t /*Addend*/) {
  if (Type == MachO::X86_64_RELOC_UNSIGNED)
    return S;
  llvm_unreachable("Invalid relocation type");
}

static bool supportsWasm32(uint64_t Type) {
  switch (Type) {
  case wasm::R_WASM_FUNCTION_INDEX_LEB:
  case wasm::R_WASM_TABLE_INDEX_SLEB:
  case wasm::R_WASM_TABLE_INDEX_I32:
  case wasm::R_WASM_MEMORY_ADDR_LEB:
  case wasm::R_WASM_MEMORY_ADDR_SLEB:
  case wasm::R_WASM_MEMORY_ADDR_I32:
  case wasm::R_WASM_TYPE_INDEX_LEB:
  case wasm::R_WASM_GLOBAL_INDEX_LEB:
  case wasm::R_WASM_FUNCTION_OFFSET_I32:
  case wasm::R_WASM_SECTION_OFFSET_I32:
  case wasm::R_WASM_TAG_INDEX_LEB:
  case wasm::R_WASM_GLOBAL_INDEX_I32:
  case wasm::R_WASM_TABLE_NUMBER_LEB:
  case wasm::R_WASM_MEMORY_ADDR_LOCREL_I32:
    return true;
  default:
    return false;
  }
}

static bool supportsWasm64(uint64_t Type) {
  switch (Type) {
  case wasm::R_WASM_MEMORY_ADDR_LEB64:
  case wasm::R_WASM_MEMORY_ADDR_SLEB64:
  case wasm::R_WASM_MEMORY_ADDR_I64:
  case wasm::R_WASM_TABLE_INDEX_SLEB64:
  case wasm::R_WASM_TABLE_INDEX_I64:
  case wasm::R_WASM_FUNCTION_OFFSET_I64:
    return true;
  default:
    return supportsWasm32(Type);
  }
}

static uint64_t resolveWasm32(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case wasm::R_WASM_FUNCTION_INDEX_LEB:
  case wasm::R_WASM_TABLE_INDEX_SLEB:
  case wasm::R_WASM_TABLE_INDEX_I32:
  case wasm::R_WASM_MEMORY_ADDR_LEB:
  case wasm::R_WASM_MEMORY_ADDR_SLEB:
  case wasm::R_WASM_MEMORY_ADDR_I32:
  case wasm::R_WASM_TYPE_INDEX_LEB:
  case wasm::R_WASM_GLOBAL_INDEX_LEB:
  case wasm::R_WASM_FUNCTION_OFFSET_I32:
  case wasm::R_WASM_SECTION_OFFSET_I32:
  case wasm::R_WASM_TAG_INDEX_LEB:
  case wasm::R_WASM_GLOBAL_INDEX_I32:
  case wasm::R_WASM_TABLE_NUMBER_LEB:
  case wasm::R_WASM_MEMORY_ADDR_LOCREL_I32:
    // For wasm section, its offset at 0 -- ignoring Value
    return LocData;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static uint64_t resolveWasm64(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t LocData, int64_t Addend) {
  switch (Type) {
  case wasm::R_WASM_MEMORY_ADDR_LEB64:
  case wasm::R_WASM_MEMORY_ADDR_SLEB64:
  case wasm::R_WASM_MEMORY_ADDR_I64:
  case wasm::R_WASM_TABLE_INDEX_SLEB64:
  case wasm::R_WASM_TABLE_INDEX_I64:
  case wasm::R_WASM_FUNCTION_OFFSET_I64:
    // For wasm section, its offset at 0 -- ignoring Value
    return LocData;
  default:
    return resolveWasm32(Type, Offset, S, LocData, Addend);
  }
}

std::pair<SupportsRelocation, RelocationResolver>
getRelocationResolver(const ObjectFile &Obj) {
  if (Obj.isCOFF()) {
    switch (Obj.getArch()) {
    case Triple::x86_64:
      return {supportsCOFFX86_64, resolveCOFFX86_64};
    case Triple::x86:
      return {supportsCOFFX86, resolveCOFFX86};
    case Triple::arm:
    case Triple::thumb:
      return {supportsCOFFARM, resolveCOFFARM};
    case Triple::aarch64:
      return {supportsCOFFARM64, resolveCOFFARM64};
    default:
      return {nullptr, nullptr};
    }
  } else if (Obj.isELF()) {
    if (Obj.getBytesInAddress() == 8) {
      switch (Obj.getArch()) {
      case Triple::x86_64:
        return {supportsX86_64, resolveX86_64};
      case Triple::aarch64:
      case Triple::aarch64_be:
        return {supportsAArch64, resolveAArch64};
      case Triple::bpfel:
      case Triple::bpfeb:
        return {supportsBPF, resolveBPF};
      case Triple::loongarch64:
        return {supportsLoongArch, resolveLoongArch};
      case Triple::mips64el:
      case Triple::mips64:
        return {supportsMips64, resolveMips64};
      case Triple::ppc64le:
      case Triple::ppc64:
        return {supportsPPC64, resolvePPC64};
      case Triple::systemz:
        return {supportsSystemZ, resolveSystemZ};
      case Triple::sparcv9:
        return {supportsSparc64, resolveSparc64};
      case Triple::amdgcn:
        return {supportsAmdgpu, resolveAmdgpu};
      case Triple::riscv64:
        return {supportsRISCV, resolveRISCV};
      default:
        if (isAMDGPU(Obj))
          return {supportsAmdgpu, resolveAmdgpu};
        return {nullptr, nullptr};
      }
    }

    // 32-bit object file
    assert(Obj.getBytesInAddress() == 4 &&
           "Invalid word size in object file");

    switch (Obj.getArch()) {
    case Triple::x86:
      return {supportsX86, resolveX86};
    case Triple::ppcle:
    case Triple::ppc:
      return {supportsPPC32, resolvePPC32};
    case Triple::arm:
    case Triple::armeb:
      return {supportsARM, resolveARM};
    case Triple::avr:
      return {supportsAVR, resolveAVR};
    case Triple::lanai:
      return {supportsLanai, resolveLanai};
    case Triple::loongarch32:
      return {supportsLoongArch, resolveLoongArch};
    case Triple::mipsel:
    case Triple::mips:
      return {supportsMips32, resolveMips32};
    case Triple::msp430:
      return {supportsMSP430, resolveMSP430};
    case Triple::sparc:
      return {supportsSparc32, resolveSparc32};
    case Triple::hexagon:
      return {supportsHexagon, resolveHexagon};
    case Triple::r600:
      return {supportsAmdgpu, resolveAmdgpu};
    case Triple::riscv32:
      return {supportsRISCV, resolveRISCV};
    case Triple::csky:
      return {supportsCSKY, resolveCSKY};
    default:
      if (isAMDGPU(Obj))
        return {supportsAmdgpu, resolveAmdgpu};
      return {nullptr, nullptr};
    }
  } else if (Obj.isMachO()) {
    if (Obj.getArch() == Triple::x86_64)
      return {supportsMachOX86_64, resolveMachOX86_64};
    return {nullptr, nullptr};
  } else if (Obj.isWasm()) {
    if (Obj.getArch() == Triple::wasm32)
      return {supportsWasm32, resolveWasm32};
    if (Obj.getArch() == Triple::wasm64)
      return {supportsWasm64, resolveWasm64};
    return {nullptr, nullptr};
  }

  llvm_unreachable("Invalid object file");
}

uint64_t resolveRelocation(RelocationResolver Resolver, const RelocationRef &R,
                           uint64_t S, uint64_t LocData) {
  if (const ObjectFile *Obj = R.getObject()) {
    int64_t Addend = 0;
    if (Obj->isELF()) {
      auto GetRelSectionType = [&]() -> unsigned {
        if (auto *Elf32LEObj = dyn_cast<ELF32LEObjectFile>(Obj))
          return Elf32LEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
        if (auto *Elf64LEObj = dyn_cast<ELF64LEObjectFile>(Obj))
          return Elf64LEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
        if (auto *Elf32BEObj = dyn_cast<ELF32BEObjectFile>(Obj))
          return Elf32BEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
        auto *Elf64BEObj = cast<ELF64BEObjectFile>(Obj);
        return Elf64BEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
      };

      if (GetRelSectionType() == ELF::SHT_RELA) {
        Addend = getELFAddend(R);
        // LoongArch and RISCV relocations use both LocData and Addend.
        if (Obj->getArch() != Triple::loongarch32 &&
            Obj->getArch() != Triple::loongarch64 &&
            Obj->getArch() != Triple::riscv32 &&
            Obj->getArch() != Triple::riscv64)
          LocData = 0;
      }
    }

    return Resolver(R.getType(), R.getOffset(), S, LocData, Addend);
  }

  // Sometimes the caller might want to use its own specific implementation of
  // the resolver function. E.g. this is used by LLD when it resolves debug
  // relocations and assumes that all of them have the same computation (S + A).
  // The relocation R has no owner object in this case and we don't need to
  // provide Type and Offset fields. It is also assumed the DataRefImpl.p
  // contains the addend, provided by the caller.
  return Resolver(/*Type=*/0, /*Offset=*/0, S, LocData,
                  R.getRawDataRefImpl().p);
}

} // namespace object
} // namespace llvm
