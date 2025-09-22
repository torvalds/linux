//===- llvm/BinaryFormat/Magic.cpp - File magic identification --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Magic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MemoryBuffer.h"

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::sys::fs;

template <size_t N>
static bool startswith(StringRef Magic, const char (&S)[N]) {
  return Magic.starts_with(StringRef(S, N - 1));
}

/// Identify the magic in magic.
file_magic llvm::identify_magic(StringRef Magic) {
  if (Magic.size() < 4)
    return file_magic::unknown;
  switch ((unsigned char)Magic[0]) {
  case 0x00: {
    // COFF bigobj, CL.exe's LTO object file, or short import library file
    if (startswith(Magic, "\0\0\xFF\xFF")) {
      size_t MinSize =
          offsetof(COFF::BigObjHeader, UUID) + sizeof(COFF::BigObjMagic);
      if (Magic.size() < MinSize)
        return file_magic::coff_import_library;

      const char *Start = Magic.data() + offsetof(COFF::BigObjHeader, UUID);
      if (memcmp(Start, COFF::BigObjMagic, sizeof(COFF::BigObjMagic)) == 0)
        return file_magic::coff_object;
      if (memcmp(Start, COFF::ClGlObjMagic, sizeof(COFF::BigObjMagic)) == 0)
        return file_magic::coff_cl_gl_object;
      return file_magic::coff_import_library;
    }
    // Windows resource file
    if (Magic.size() >= sizeof(COFF::WinResMagic) &&
        memcmp(Magic.data(), COFF::WinResMagic, sizeof(COFF::WinResMagic)) == 0)
      return file_magic::windows_resource;
    // 0x0000 = COFF unknown machine type
    if (Magic[1] == 0)
      return file_magic::coff_object;
    if (startswith(Magic, "\0asm"))
      return file_magic::wasm_object;
    break;
  }

  case 0x01:
    // XCOFF format
    if (startswith(Magic, "\x01\xDF"))
      return file_magic::xcoff_object_32;
    if (startswith(Magic, "\x01\xF7"))
      return file_magic::xcoff_object_64;
    break;

  case 0x03:
    if (startswith(Magic, "\x03\xF0\x00"))
      return file_magic::goff_object;
    // SPIR-V format in little-endian mode.
    if (startswith(Magic, "\x03\x02\x23\x07"))
      return file_magic::spirv_object;
    break;

  case 0x07: // SPIR-V format in big-endian mode.
    if (startswith(Magic, "\x07\x23\x02\x03"))
      return file_magic::spirv_object;
    break;

  case 0x10:
    if (startswith(Magic, "\x10\xFF\x10\xAD"))
      return file_magic::offload_binary;
    break;

  case 0xDE: // 0x0B17C0DE = BC wraper
    if (startswith(Magic, "\xDE\xC0\x17\x0B"))
      return file_magic::bitcode;
    break;
  case 'B':
    if (startswith(Magic, "BC\xC0\xDE"))
      return file_magic::bitcode;
    break;
  case 'C':
    if (startswith(Magic, "CCOB"))
      return file_magic::offload_bundle_compressed;
    if (startswith(Magic, "CPCH"))
      return file_magic::clang_ast;
    break;
  case '!':
    if (startswith(Magic, "!<arch>\n") || startswith(Magic, "!<thin>\n"))
      return file_magic::archive;
    break;
  case '<':
    if (startswith(Magic, "<bigaf>\n"))
      return file_magic::archive;
    break;
  case '\177':
    if (startswith(Magic, "\177ELF") && Magic.size() >= 18) {
      bool Data2MSB = Magic[5] == 2;
      unsigned high = Data2MSB ? 16 : 17;
      unsigned low = Data2MSB ? 17 : 16;
      if (Magic[high] == 0) {
        switch (Magic[low]) {
        default:
          return file_magic::elf;
        case 1:
          return file_magic::elf_relocatable;
        case 2:
          return file_magic::elf_executable;
        case 3:
          return file_magic::elf_shared_object;
        case 4:
          return file_magic::elf_core;
        }
      }
      // It's still some type of ELF file.
      return file_magic::elf;
    }
    break;

  case 0xCA:
    if (startswith(Magic, "\xCA\xFE\xBA\xBE") ||
        startswith(Magic, "\xCA\xFE\xBA\xBF")) {
      // This is complicated by an overlap with Java class files.
      // See the Mach-O section in /usr/share/file/magic for details.
      if (Magic.size() >= 8 && Magic[7] < 43)
        return file_magic::macho_universal_binary;
    }
    break;

  // The two magic numbers for mach-o are:
  // 0xfeedface - 32-bit mach-o
  // 0xfeedfacf - 64-bit mach-o
  case 0xFE:
  case 0xCE:
  case 0xCF: {
    uint16_t type = 0;
    if (startswith(Magic, "\xFE\xED\xFA\xCE") ||
        startswith(Magic, "\xFE\xED\xFA\xCF")) {
      /* Native endian */
      size_t MinSize;
      if (Magic[3] == char(0xCE))
        MinSize = sizeof(MachO::mach_header);
      else
        MinSize = sizeof(MachO::mach_header_64);
      if (Magic.size() >= MinSize)
        type = Magic[12] << 24 | Magic[13] << 12 | Magic[14] << 8 | Magic[15];
    } else if (startswith(Magic, "\xCE\xFA\xED\xFE") ||
               startswith(Magic, "\xCF\xFA\xED\xFE")) {
      /* Reverse endian */
      size_t MinSize;
      if (Magic[0] == char(0xCE))
        MinSize = sizeof(MachO::mach_header);
      else
        MinSize = sizeof(MachO::mach_header_64);
      if (Magic.size() >= MinSize)
        type = Magic[15] << 24 | Magic[14] << 12 | Magic[13] << 8 | Magic[12];
    }
    switch (type) {
    default:
      break;
    case 1:
      return file_magic::macho_object;
    case 2:
      return file_magic::macho_executable;
    case 3:
      return file_magic::macho_fixed_virtual_memory_shared_lib;
    case 4:
      return file_magic::macho_core;
    case 5:
      return file_magic::macho_preload_executable;
    case 6:
      return file_magic::macho_dynamically_linked_shared_lib;
    case 7:
      return file_magic::macho_dynamic_linker;
    case 8:
      return file_magic::macho_bundle;
    case 9:
      return file_magic::macho_dynamically_linked_shared_lib_stub;
    case 10:
      return file_magic::macho_dsym_companion;
    case 11:
      return file_magic::macho_kext_bundle;
    case 12:
      return file_magic::macho_file_set;
    }
    break;
  }
  case 0xF0: // PowerPC Windows
  case 0x83: // Alpha 32-bit
  case 0x84: // Alpha 64-bit
  case 0x66: // MPS R4000 Windows
  case 0x50: // mc68K
    if (startswith(Magic, "\x50\xed\x55\xba"))
      return file_magic::cuda_fatbinary;
    [[fallthrough]];

  case 0x4c: // 80386 Windows
  case 0xc4: // ARMNT Windows
    if (Magic[1] == 0x01)
      return file_magic::coff_object;
    [[fallthrough]];

  case 0x90: // PA-RISC Windows
  case 0x68: // mc68K Windows
    if (Magic[1] == 0x02)
      return file_magic::coff_object;
    break;

  case 'M': // Possible MS-DOS stub on Windows PE file, MSF/PDB file or a
            // Minidump file.
    if (startswith(Magic, "MZ") && Magic.size() >= 0x3c + 4) {
      uint32_t off = read32le(Magic.data() + 0x3c);
      // PE/COFF file, either EXE or DLL.
      if (Magic.substr(off).starts_with(
              StringRef(COFF::PEMagic, sizeof(COFF::PEMagic))))
        return file_magic::pecoff_executable;
    }
    if (Magic.starts_with("Microsoft C/C++ MSF 7.00\r\n"))
      return file_magic::pdb;
    if (startswith(Magic, "MDMP"))
      return file_magic::minidump;
    break;

  case 0x64: // x86-64 or ARM64 Windows.
    if (Magic[1] == char(0x86) || Magic[1] == char(0xaa))
      return file_magic::coff_object;
    break;

  case 0x2d: // YAML '-' MachO TBD.
    if (startswith(Magic, "--- !tapi") || startswith(Magic, "---\narchs:"))
      return file_magic::tapi_file;
    break;
  case 0x7b: // JSON '{' MachO TBD.
    return file_magic::tapi_file;
    break;

  case 'D': // DirectX container file - DXBC
    if (startswith(Magic, "DXBC"))
      return file_magic::dxcontainer_object;
    break;

  case 0x41: // ARM64EC windows
    if (Magic[1] == char(0xA6))
      return file_magic::coff_object;
    break;

  case 0x4e: // ARM64X windows
    if (Magic[1] == char(0xA6))
      return file_magic::coff_object;
    break;

  case '_': {
    const char OBMagic[] = "__CLANG_OFFLOAD_BUNDLE__";
    if (Magic.size() >= sizeof(OBMagic) && startswith(Magic, OBMagic))
      return file_magic::offload_bundle;
    break;
  }

  default:
    break;
  }
  return file_magic::unknown;
}

std::error_code llvm::identify_magic(const Twine &Path, file_magic &Result) {
  auto FileOrError = MemoryBuffer::getFile(Path, /*IsText=*/false,
                                           /*RequiresNullTerminator=*/false);
  if (!FileOrError)
    return FileOrError.getError();

  std::unique_ptr<MemoryBuffer> FileBuffer = std::move(*FileOrError);
  Result = identify_magic(FileBuffer->getBuffer());

  return std::error_code();
}
