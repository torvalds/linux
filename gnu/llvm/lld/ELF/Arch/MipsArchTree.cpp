//===- MipsArchTree.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file contains a helper function for the Writer.
//
//===---------------------------------------------------------------------===//

#include "InputFiles.h"
#include "SymbolTable.h"
#include "Writer.h"

#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/MipsABIFlags.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;

using namespace lld;
using namespace lld::elf;

namespace {
struct ArchTreeEdge {
  uint32_t child;
  uint32_t parent;
};

struct FileFlags {
  InputFile *file;
  uint32_t flags;
};
} // namespace

static StringRef getAbiName(uint32_t flags) {
  switch (flags) {
  case 0:
    return "n64";
  case EF_MIPS_ABI2:
    return "n32";
  case EF_MIPS_ABI_O32:
    return "o32";
  case EF_MIPS_ABI_O64:
    return "o64";
  case EF_MIPS_ABI_EABI32:
    return "eabi32";
  case EF_MIPS_ABI_EABI64:
    return "eabi64";
  default:
    return "unknown";
  }
}

static StringRef getNanName(bool isNan2008) {
  return isNan2008 ? "2008" : "legacy";
}

static StringRef getFpName(bool isFp64) { return isFp64 ? "64" : "32"; }

static void checkFlags(ArrayRef<FileFlags> files) {
  assert(!files.empty() && "expected non-empty file list");

  uint32_t abi = files[0].flags & (EF_MIPS_ABI | EF_MIPS_ABI2);
  bool nan = files[0].flags & EF_MIPS_NAN2008;
  bool fp = files[0].flags & EF_MIPS_FP64;

  for (const FileFlags &f : files) {
    if (config->is64 && f.flags & EF_MIPS_MICROMIPS)
      error(toString(f.file) + ": microMIPS 64-bit is not supported");

    uint32_t abi2 = f.flags & (EF_MIPS_ABI | EF_MIPS_ABI2);
    if (abi != abi2)
      error(toString(f.file) + ": ABI '" + getAbiName(abi2) +
            "' is incompatible with target ABI '" + getAbiName(abi) + "'");

    bool nan2 = f.flags & EF_MIPS_NAN2008;
    if (nan != nan2)
      error(toString(f.file) + ": -mnan=" + getNanName(nan2) +
            " is incompatible with target -mnan=" + getNanName(nan));

    bool fp2 = f.flags & EF_MIPS_FP64;
    if (fp != fp2)
      error(toString(f.file) + ": -mfp" + getFpName(fp2) +
            " is incompatible with target -mfp" + getFpName(fp));
  }
}

static uint32_t getMiscFlags(ArrayRef<FileFlags> files) {
  uint32_t ret = 0;
  for (const FileFlags &f : files)
    ret |= f.flags &
           (EF_MIPS_ABI | EF_MIPS_ABI2 | EF_MIPS_ARCH_ASE | EF_MIPS_NOREORDER |
            EF_MIPS_MICROMIPS | EF_MIPS_NAN2008 | EF_MIPS_32BITMODE);
  return ret;
}

static uint32_t getPicFlags(ArrayRef<FileFlags> files) {
  // Check PIC/non-PIC compatibility.
  bool isPic = files[0].flags & (EF_MIPS_PIC | EF_MIPS_CPIC);
  for (const FileFlags &f : files.slice(1)) {
    bool isPic2 = f.flags & (EF_MIPS_PIC | EF_MIPS_CPIC);
    if (isPic && !isPic2)
      warn(toString(f.file) +
           ": linking non-abicalls code with abicalls code " +
           toString(files[0].file));
    if (!isPic && isPic2)
      warn(toString(f.file) +
           ": linking abicalls code with non-abicalls code " +
           toString(files[0].file));
  }

  // Compute the result PIC/non-PIC flag.
  uint32_t ret = files[0].flags & (EF_MIPS_PIC | EF_MIPS_CPIC);
  for (const FileFlags &f : files.slice(1))
    ret &= f.flags & (EF_MIPS_PIC | EF_MIPS_CPIC);

  // PIC code is inherently CPIC and may not set CPIC flag explicitly.
  if (ret & EF_MIPS_PIC)
    ret |= EF_MIPS_CPIC;
  return ret;
}

static ArchTreeEdge archTree[] = {
    // MIPS32R6 and MIPS64R6 are not compatible with other extensions
    // MIPS64R2 extensions.
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_OCTEON3, EF_MIPS_ARCH_64R2},
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_OCTEON2, EF_MIPS_ARCH_64R2},
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_OCTEON, EF_MIPS_ARCH_64R2},
    {EF_MIPS_ARCH_64R2 | EF_MIPS_MACH_LS3A, EF_MIPS_ARCH_64R2},
    // MIPS64 extensions.
    {EF_MIPS_ARCH_64 | EF_MIPS_MACH_SB1, EF_MIPS_ARCH_64},
    {EF_MIPS_ARCH_64 | EF_MIPS_MACH_XLR, EF_MIPS_ARCH_64},
    {EF_MIPS_ARCH_64R2, EF_MIPS_ARCH_64},
    // MIPS V extensions.
    {EF_MIPS_ARCH_64, EF_MIPS_ARCH_5},
    // R5000 extensions.
    {EF_MIPS_ARCH_4 | EF_MIPS_MACH_5500, EF_MIPS_ARCH_4 | EF_MIPS_MACH_5400},
    // MIPS IV extensions.
    {EF_MIPS_ARCH_4 | EF_MIPS_MACH_5400, EF_MIPS_ARCH_4},
    {EF_MIPS_ARCH_4 | EF_MIPS_MACH_9000, EF_MIPS_ARCH_4},
    {EF_MIPS_ARCH_5, EF_MIPS_ARCH_4},
    // VR4100 extensions.
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4111, EF_MIPS_ARCH_3 | EF_MIPS_MACH_4100},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4120, EF_MIPS_ARCH_3 | EF_MIPS_MACH_4100},
    // MIPS III extensions.
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4010, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4100, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_4650, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_5900, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_LS2E, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_3 | EF_MIPS_MACH_LS2F, EF_MIPS_ARCH_3},
    {EF_MIPS_ARCH_4, EF_MIPS_ARCH_3},
    // MIPS32 extensions.
    {EF_MIPS_ARCH_32R2, EF_MIPS_ARCH_32},
    // MIPS II extensions.
    {EF_MIPS_ARCH_3, EF_MIPS_ARCH_2},
    {EF_MIPS_ARCH_32, EF_MIPS_ARCH_2},
    // MIPS I extensions.
    {EF_MIPS_ARCH_1 | EF_MIPS_MACH_3900, EF_MIPS_ARCH_1},
    {EF_MIPS_ARCH_2, EF_MIPS_ARCH_1},
};

static bool isArchMatched(uint32_t newFlags, uint32_t res) {
  if (newFlags == res)
    return true;
  if (newFlags == EF_MIPS_ARCH_32 && isArchMatched(EF_MIPS_ARCH_64, res))
    return true;
  if (newFlags == EF_MIPS_ARCH_32R2 && isArchMatched(EF_MIPS_ARCH_64R2, res))
    return true;
  for (const auto &edge : archTree) {
    if (res == edge.child) {
      res = edge.parent;
      if (res == newFlags)
        return true;
    }
  }
  return false;
}

static StringRef getMachName(uint32_t flags) {
  switch (flags & EF_MIPS_MACH) {
  case EF_MIPS_MACH_NONE:
    return "";
  case EF_MIPS_MACH_3900:
    return "r3900";
  case EF_MIPS_MACH_4010:
    return "r4010";
  case EF_MIPS_MACH_4100:
    return "r4100";
  case EF_MIPS_MACH_4650:
    return "r4650";
  case EF_MIPS_MACH_4120:
    return "r4120";
  case EF_MIPS_MACH_4111:
    return "r4111";
  case EF_MIPS_MACH_5400:
    return "vr5400";
  case EF_MIPS_MACH_5900:
    return "vr5900";
  case EF_MIPS_MACH_5500:
    return "vr5500";
  case EF_MIPS_MACH_9000:
    return "rm9000";
  case EF_MIPS_MACH_LS2E:
    return "loongson2e";
  case EF_MIPS_MACH_LS2F:
    return "loongson2f";
  case EF_MIPS_MACH_LS3A:
    return "loongson3a";
  case EF_MIPS_MACH_OCTEON:
    return "octeon";
  case EF_MIPS_MACH_OCTEON2:
    return "octeon2";
  case EF_MIPS_MACH_OCTEON3:
    return "octeon3";
  case EF_MIPS_MACH_SB1:
    return "sb1";
  case EF_MIPS_MACH_XLR:
    return "xlr";
  default:
    return "unknown machine";
  }
}

static StringRef getArchName(uint32_t flags) {
  switch (flags & EF_MIPS_ARCH) {
  case EF_MIPS_ARCH_1:
    return "mips1";
  case EF_MIPS_ARCH_2:
    return "mips2";
  case EF_MIPS_ARCH_3:
    return "mips3";
  case EF_MIPS_ARCH_4:
    return "mips4";
  case EF_MIPS_ARCH_5:
    return "mips5";
  case EF_MIPS_ARCH_32:
    return "mips32";
  case EF_MIPS_ARCH_64:
    return "mips64";
  case EF_MIPS_ARCH_32R2:
    return "mips32r2";
  case EF_MIPS_ARCH_64R2:
    return "mips64r2";
  case EF_MIPS_ARCH_32R6:
    return "mips32r6";
  case EF_MIPS_ARCH_64R6:
    return "mips64r6";
  default:
    return "unknown arch";
  }
}

static std::string getFullArchName(uint32_t flags) {
  StringRef arch = getArchName(flags);
  StringRef mach = getMachName(flags);
  if (mach.empty())
    return arch.str();
  return (arch + " (" + mach + ")").str();
}

// There are (arguably too) many MIPS ISAs out there. Their relationships
// can be represented as a forest. If all input files have ISAs which
// reachable by repeated proceeding from the single child to the parent,
// these input files are compatible. In that case we need to return "highest"
// ISA. If there are incompatible input files, we show an error.
// For example, mips1 is a "parent" of mips2 and such files are compatible.
// Output file gets EF_MIPS_ARCH_2 flag. From the other side mips3 and mips32
// are incompatible because nor mips3 is a parent for misp32, nor mips32
// is a parent for mips3.
static uint32_t getArchFlags(ArrayRef<FileFlags> files) {
  uint32_t ret = files[0].flags & (EF_MIPS_ARCH | EF_MIPS_MACH);

  for (const FileFlags &f : files.slice(1)) {
    uint32_t newFlags = f.flags & (EF_MIPS_ARCH | EF_MIPS_MACH);

    // Check ISA compatibility.
    if (isArchMatched(newFlags, ret))
      continue;
    if (!isArchMatched(ret, newFlags)) {
      error("incompatible target ISA:\n>>> " + toString(files[0].file) + ": " +
            getFullArchName(ret) + "\n>>> " + toString(f.file) + ": " +
            getFullArchName(newFlags));
      return 0;
    }
    ret = newFlags;
  }
  return ret;
}

template <class ELFT> uint32_t elf::calcMipsEFlags() {
  std::vector<FileFlags> v;
  for (InputFile *f : ctx.objectFiles)
    v.push_back({f, cast<ObjFile<ELFT>>(f)->getObj().getHeader().e_flags});
  if (v.empty()) {
    // If we don't have any input files, we'll have to rely on the information
    // we can derive from emulation information, since this at least gets us
    // ABI.
    if (config->emulation.empty() || config->is64)
      return 0;
    return config->mipsN32Abi ? EF_MIPS_ABI2 : EF_MIPS_ABI_O32;
  }
  checkFlags(v);
  return getMiscFlags(v) | getPicFlags(v) | getArchFlags(v);
}

static int compareMipsFpAbi(uint8_t fpA, uint8_t fpB) {
  if (fpA == fpB)
    return 0;
  if (fpB == Mips::Val_GNU_MIPS_ABI_FP_ANY)
    return 1;
  if (fpB == Mips::Val_GNU_MIPS_ABI_FP_64A &&
      fpA == Mips::Val_GNU_MIPS_ABI_FP_64)
    return 1;
  if (fpB != Mips::Val_GNU_MIPS_ABI_FP_XX)
    return -1;
  if (fpA == Mips::Val_GNU_MIPS_ABI_FP_DOUBLE ||
      fpA == Mips::Val_GNU_MIPS_ABI_FP_64 ||
      fpA == Mips::Val_GNU_MIPS_ABI_FP_64A)
    return 1;
  return -1;
}

static StringRef getMipsFpAbiName(uint8_t fpAbi) {
  switch (fpAbi) {
  case Mips::Val_GNU_MIPS_ABI_FP_ANY:
    return "any";
  case Mips::Val_GNU_MIPS_ABI_FP_DOUBLE:
    return "-mdouble-float";
  case Mips::Val_GNU_MIPS_ABI_FP_SINGLE:
    return "-msingle-float";
  case Mips::Val_GNU_MIPS_ABI_FP_SOFT:
    return "-msoft-float";
  case Mips::Val_GNU_MIPS_ABI_FP_OLD_64:
    return "-mgp32 -mfp64 (old)";
  case Mips::Val_GNU_MIPS_ABI_FP_XX:
    return "-mfpxx";
  case Mips::Val_GNU_MIPS_ABI_FP_64:
    return "-mgp32 -mfp64";
  case Mips::Val_GNU_MIPS_ABI_FP_64A:
    return "-mgp32 -mfp64 -mno-odd-spreg";
  default:
    return "unknown";
  }
}

uint8_t elf::getMipsFpAbiFlag(uint8_t oldFlag, uint8_t newFlag,
                              StringRef fileName) {
  if (compareMipsFpAbi(newFlag, oldFlag) >= 0)
    return newFlag;
  if (compareMipsFpAbi(oldFlag, newFlag) < 0)
    error(fileName + ": floating point ABI '" + getMipsFpAbiName(newFlag) +
          "' is incompatible with target floating point ABI '" +
          getMipsFpAbiName(oldFlag) + "'");
  return oldFlag;
}

template <class ELFT> static bool isN32Abi(const InputFile *f) {
  if (auto *ef = dyn_cast<ELFFileBase>(f))
    return ef->template getObj<ELFT>().getHeader().e_flags & EF_MIPS_ABI2;
  return false;
}

bool elf::isMipsN32Abi(const InputFile *f) {
  switch (config->ekind) {
  case ELF32LEKind:
    return isN32Abi<ELF32LE>(f);
  case ELF32BEKind:
    return isN32Abi<ELF32BE>(f);
  case ELF64LEKind:
    return isN32Abi<ELF64LE>(f);
  case ELF64BEKind:
    return isN32Abi<ELF64BE>(f);
  default:
    llvm_unreachable("unknown Config->EKind");
  }
}

bool elf::isMicroMips() { return config->eflags & EF_MIPS_MICROMIPS; }

bool elf::isMipsR6() {
  uint32_t arch = config->eflags & EF_MIPS_ARCH;
  return arch == EF_MIPS_ARCH_32R6 || arch == EF_MIPS_ARCH_64R6;
}

template uint32_t elf::calcMipsEFlags<ELF32LE>();
template uint32_t elf::calcMipsEFlags<ELF32BE>();
template uint32_t elf::calcMipsEFlags<ELF64LE>();
template uint32_t elf::calcMipsEFlags<ELF64BE>();
