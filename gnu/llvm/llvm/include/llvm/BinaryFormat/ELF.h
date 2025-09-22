//===- llvm/BinaryFormat/ELF.h - ELF constants and structures ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header contains common, non-processor-specific data structures and
// constants for the ELF file format.
//
// The details of the ELF32 bits in this file are largely based on the Tool
// Interface Standard (TIS) Executable and Linking Format (ELF) Specification
// Version 1.2, May 1995. The ELF64 stuff is based on ELF-64 Object File Format
// Version 1.5, Draft 2, May 1998 as well as OpenBSD header files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_ELF_H
#define LLVM_BINARYFORMAT_ELF_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace llvm {
namespace ELF {

using Elf32_Addr = uint32_t; // Program address
using Elf32_Off = uint32_t;  // File offset
using Elf32_Half = uint16_t;
using Elf32_Word = uint32_t;
using Elf32_Sword = int32_t;

using Elf64_Addr = uint64_t;
using Elf64_Off = uint64_t;
using Elf64_Half = uint16_t;
using Elf64_Word = uint32_t;
using Elf64_Sword = int32_t;
using Elf64_Xword = uint64_t;
using Elf64_Sxword = int64_t;

// Object file magic string.
static const char ElfMagic[] = {0x7f, 'E', 'L', 'F', '\0'};

// e_ident size and indices.
enum {
  EI_MAG0 = 0,       // File identification index.
  EI_MAG1 = 1,       // File identification index.
  EI_MAG2 = 2,       // File identification index.
  EI_MAG3 = 3,       // File identification index.
  EI_CLASS = 4,      // File class.
  EI_DATA = 5,       // Data encoding.
  EI_VERSION = 6,    // File version.
  EI_OSABI = 7,      // OS/ABI identification.
  EI_ABIVERSION = 8, // ABI version.
  EI_PAD = 9,        // Start of padding bytes.
  EI_NIDENT = 16     // Number of bytes in e_ident.
};

struct Elf32_Ehdr {
  unsigned char e_ident[EI_NIDENT]; // ELF Identification bytes
  Elf32_Half e_type;                // Type of file (see ET_* below)
  Elf32_Half e_machine;   // Required architecture for this file (see EM_*)
  Elf32_Word e_version;   // Must be equal to 1
  Elf32_Addr e_entry;     // Address to jump to in order to start program
  Elf32_Off e_phoff;      // Program header table's file offset, in bytes
  Elf32_Off e_shoff;      // Section header table's file offset, in bytes
  Elf32_Word e_flags;     // Processor-specific flags
  Elf32_Half e_ehsize;    // Size of ELF header, in bytes
  Elf32_Half e_phentsize; // Size of an entry in the program header table
  Elf32_Half e_phnum;     // Number of entries in the program header table
  Elf32_Half e_shentsize; // Size of an entry in the section header table
  Elf32_Half e_shnum;     // Number of entries in the section header table
  Elf32_Half e_shstrndx;  // Sect hdr table index of sect name string table

  bool checkMagic() const {
    return (memcmp(e_ident, ElfMagic, strlen(ElfMagic))) == 0;
  }

  unsigned char getFileClass() const { return e_ident[EI_CLASS]; }
  unsigned char getDataEncoding() const { return e_ident[EI_DATA]; }
};

// 64-bit ELF header. Fields are the same as for ELF32, but with different
// types (see above).
struct Elf64_Ehdr {
  unsigned char e_ident[EI_NIDENT];
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;
  Elf64_Off e_phoff;
  Elf64_Off e_shoff;
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;

  bool checkMagic() const {
    return (memcmp(e_ident, ElfMagic, strlen(ElfMagic))) == 0;
  }

  unsigned char getFileClass() const { return e_ident[EI_CLASS]; }
  unsigned char getDataEncoding() const { return e_ident[EI_DATA]; }
};

// File types.
// See current registered ELF types at:
//    http://www.sco.com/developers/gabi/latest/ch4.eheader.html
enum {
  ET_NONE = 0,        // No file type
  ET_REL = 1,         // Relocatable file
  ET_EXEC = 2,        // Executable file
  ET_DYN = 3,         // Shared object file
  ET_CORE = 4,        // Core file
  ET_LOOS = 0xfe00,   // Beginning of operating system-specific codes
  ET_HIOS = 0xfeff,   // Operating system-specific
  ET_LOPROC = 0xff00, // Beginning of processor-specific codes
  ET_HIPROC = 0xffff  // Processor-specific
};

// Versioning
enum { EV_NONE = 0, EV_CURRENT = 1 };

// Machine architectures
// See current registered ELF machine architectures at:
//    http://www.uxsglobal.com/developers/gabi/latest/ch4.eheader.html
enum {
  EM_NONE = 0,           // No machine
  EM_M32 = 1,            // AT&T WE 32100
  EM_SPARC = 2,          // SPARC
  EM_386 = 3,            // Intel 386
  EM_68K = 4,            // Motorola 68000
  EM_88K = 5,            // Motorola 88000
  EM_IAMCU = 6,          // Intel MCU
  EM_860 = 7,            // Intel 80860
  EM_MIPS = 8,           // MIPS R3000
  EM_S370 = 9,           // IBM System/370
  EM_MIPS_RS3_LE = 10,   // MIPS RS3000 Little-endian
  EM_PARISC = 15,        // Hewlett-Packard PA-RISC
  EM_VPP500 = 17,        // Fujitsu VPP500
  EM_SPARC32PLUS = 18,   // Enhanced instruction set SPARC
  EM_960 = 19,           // Intel 80960
  EM_PPC = 20,           // PowerPC
  EM_PPC64 = 21,         // PowerPC64
  EM_S390 = 22,          // IBM System/390
  EM_SPU = 23,           // IBM SPU/SPC
  EM_V800 = 36,          // NEC V800
  EM_FR20 = 37,          // Fujitsu FR20
  EM_RH32 = 38,          // TRW RH-32
  EM_RCE = 39,           // Motorola RCE
  EM_ARM = 40,           // ARM
  EM_ALPHA = 41,         // DEC Alpha
  EM_SH = 42,            // Hitachi SH
  EM_SPARCV9 = 43,       // SPARC V9
  EM_TRICORE = 44,       // Siemens TriCore
  EM_ARC = 45,           // Argonaut RISC Core
  EM_H8_300 = 46,        // Hitachi H8/300
  EM_H8_300H = 47,       // Hitachi H8/300H
  EM_H8S = 48,           // Hitachi H8S
  EM_H8_500 = 49,        // Hitachi H8/500
  EM_IA_64 = 50,         // Intel IA-64 processor architecture
  EM_MIPS_X = 51,        // Stanford MIPS-X
  EM_COLDFIRE = 52,      // Motorola ColdFire
  EM_68HC12 = 53,        // Motorola M68HC12
  EM_MMA = 54,           // Fujitsu MMA Multimedia Accelerator
  EM_PCP = 55,           // Siemens PCP
  EM_NCPU = 56,          // Sony nCPU embedded RISC processor
  EM_NDR1 = 57,          // Denso NDR1 microprocessor
  EM_STARCORE = 58,      // Motorola Star*Core processor
  EM_ME16 = 59,          // Toyota ME16 processor
  EM_ST100 = 60,         // STMicroelectronics ST100 processor
  EM_TINYJ = 61,         // Advanced Logic Corp. TinyJ embedded processor family
  EM_X86_64 = 62,        // AMD x86-64 architecture
  EM_PDSP = 63,          // Sony DSP Processor
  EM_PDP10 = 64,         // Digital Equipment Corp. PDP-10
  EM_PDP11 = 65,         // Digital Equipment Corp. PDP-11
  EM_FX66 = 66,          // Siemens FX66 microcontroller
  EM_ST9PLUS = 67,       // STMicroelectronics ST9+ 8/16 bit microcontroller
  EM_ST7 = 68,           // STMicroelectronics ST7 8-bit microcontroller
  EM_68HC16 = 69,        // Motorola MC68HC16 Microcontroller
  EM_68HC11 = 70,        // Motorola MC68HC11 Microcontroller
  EM_68HC08 = 71,        // Motorola MC68HC08 Microcontroller
  EM_68HC05 = 72,        // Motorola MC68HC05 Microcontroller
  EM_SVX = 73,           // Silicon Graphics SVx
  EM_ST19 = 74,          // STMicroelectronics ST19 8-bit microcontroller
  EM_VAX = 75,           // Digital VAX
  EM_CRIS = 76,          // Axis Communications 32-bit embedded processor
  EM_JAVELIN = 77,       // Infineon Technologies 32-bit embedded processor
  EM_FIREPATH = 78,      // Element 14 64-bit DSP Processor
  EM_ZSP = 79,           // LSI Logic 16-bit DSP Processor
  EM_MMIX = 80,          // Donald Knuth's educational 64-bit processor
  EM_HUANY = 81,         // Harvard University machine-independent object files
  EM_PRISM = 82,         // SiTera Prism
  EM_AVR = 83,           // Atmel AVR 8-bit microcontroller
  EM_FR30 = 84,          // Fujitsu FR30
  EM_D10V = 85,          // Mitsubishi D10V
  EM_D30V = 86,          // Mitsubishi D30V
  EM_V850 = 87,          // NEC v850
  EM_M32R = 88,          // Mitsubishi M32R
  EM_MN10300 = 89,       // Matsushita MN10300
  EM_MN10200 = 90,       // Matsushita MN10200
  EM_PJ = 91,            // picoJava
  EM_OPENRISC = 92,      // OpenRISC 32-bit embedded processor
  EM_ARC_COMPACT = 93,   // ARC International ARCompact processor (old
                         // spelling/synonym: EM_ARC_A5)
  EM_XTENSA = 94,        // Tensilica Xtensa Architecture
  EM_VIDEOCORE = 95,     // Alphamosaic VideoCore processor
  EM_TMM_GPP = 96,       // Thompson Multimedia General Purpose Processor
  EM_NS32K = 97,         // National Semiconductor 32000 series
  EM_TPC = 98,           // Tenor Network TPC processor
  EM_SNP1K = 99,         // Trebia SNP 1000 processor
  EM_ST200 = 100,        // STMicroelectronics (www.st.com) ST200
  EM_IP2K = 101,         // Ubicom IP2xxx microcontroller family
  EM_MAX = 102,          // MAX Processor
  EM_CR = 103,           // National Semiconductor CompactRISC microprocessor
  EM_F2MC16 = 104,       // Fujitsu F2MC16
  EM_MSP430 = 105,       // Texas Instruments embedded microcontroller msp430
  EM_BLACKFIN = 106,     // Analog Devices Blackfin (DSP) processor
  EM_SE_C33 = 107,       // S1C33 Family of Seiko Epson processors
  EM_SEP = 108,          // Sharp embedded microprocessor
  EM_ARCA = 109,         // Arca RISC Microprocessor
  EM_UNICORE = 110,      // Microprocessor series from PKU-Unity Ltd. and MPRC
                         // of Peking University
  EM_EXCESS = 111,       // eXcess: 16/32/64-bit configurable embedded CPU
  EM_DXP = 112,          // Icera Semiconductor Inc. Deep Execution Processor
  EM_ALTERA_NIOS2 = 113, // Altera Nios II soft-core processor
  EM_CRX = 114,          // National Semiconductor CompactRISC CRX
  EM_XGATE = 115,        // Motorola XGATE embedded processor
  EM_C166 = 116,         // Infineon C16x/XC16x processor
  EM_M16C = 117,         // Renesas M16C series microprocessors
  EM_DSPIC30F = 118,     // Microchip Technology dsPIC30F Digital Signal
                         // Controller
  EM_CE = 119,           // Freescale Communication Engine RISC core
  EM_M32C = 120,         // Renesas M32C series microprocessors
  EM_TSK3000 = 131,      // Altium TSK3000 core
  EM_RS08 = 132,         // Freescale RS08 embedded processor
  EM_SHARC = 133,        // Analog Devices SHARC family of 32-bit DSP
                         // processors
  EM_ECOG2 = 134,        // Cyan Technology eCOG2 microprocessor
  EM_SCORE7 = 135,       // Sunplus S+core7 RISC processor
  EM_DSP24 = 136,        // New Japan Radio (NJR) 24-bit DSP Processor
  EM_VIDEOCORE3 = 137,   // Broadcom VideoCore III processor
  EM_LATTICEMICO32 = 138, // RISC processor for Lattice FPGA architecture
  EM_SE_C17 = 139,        // Seiko Epson C17 family
  EM_TI_C6000 = 140,      // The Texas Instruments TMS320C6000 DSP family
  EM_TI_C2000 = 141,      // The Texas Instruments TMS320C2000 DSP family
  EM_TI_C5500 = 142,      // The Texas Instruments TMS320C55x DSP family
  EM_MMDSP_PLUS = 160,    // STMicroelectronics 64bit VLIW Data Signal Processor
  EM_CYPRESS_M8C = 161,   // Cypress M8C microprocessor
  EM_R32C = 162,          // Renesas R32C series microprocessors
  EM_TRIMEDIA = 163,      // NXP Semiconductors TriMedia architecture family
  EM_HEXAGON = 164,       // Qualcomm Hexagon processor
  EM_8051 = 165,          // Intel 8051 and variants
  EM_STXP7X = 166,        // STMicroelectronics STxP7x family of configurable
                          // and extensible RISC processors
  EM_NDS32 = 167,         // Andes Technology compact code size embedded RISC
                          // processor family
  EM_ECOG1 = 168,         // Cyan Technology eCOG1X family
  EM_ECOG1X = 168,        // Cyan Technology eCOG1X family
  EM_MAXQ30 = 169,        // Dallas Semiconductor MAXQ30 Core Micro-controllers
  EM_XIMO16 = 170,        // New Japan Radio (NJR) 16-bit DSP Processor
  EM_MANIK = 171,         // M2000 Reconfigurable RISC Microprocessor
  EM_CRAYNV2 = 172,       // Cray Inc. NV2 vector architecture
  EM_RX = 173,            // Renesas RX family
  EM_METAG = 174,         // Imagination Technologies META processor
                          // architecture
  EM_MCST_ELBRUS = 175,   // MCST Elbrus general purpose hardware architecture
  EM_ECOG16 = 176,        // Cyan Technology eCOG16 family
  EM_CR16 = 177,          // National Semiconductor CompactRISC CR16 16-bit
                          // microprocessor
  EM_ETPU = 178,          // Freescale Extended Time Processing Unit
  EM_SLE9X = 179,         // Infineon Technologies SLE9X core
  EM_L10M = 180,          // Intel L10M
  EM_K10M = 181,          // Intel K10M
  EM_AARCH64 = 183,       // ARM AArch64
  EM_AVR32 = 185,         // Atmel Corporation 32-bit microprocessor family
  EM_STM8 = 186,          // STMicroeletronics STM8 8-bit microcontroller
  EM_TILE64 = 187,        // Tilera TILE64 multicore architecture family
  EM_TILEPRO = 188,       // Tilera TILEPro multicore architecture family
  EM_MICROBLAZE = 189,    // Xilinx MicroBlaze 32-bit RISC soft processor core
  EM_CUDA = 190,          // NVIDIA CUDA architecture
  EM_TILEGX = 191,        // Tilera TILE-Gx multicore architecture family
  EM_CLOUDSHIELD = 192,   // CloudShield architecture family
  EM_COREA_1ST = 193,     // KIPO-KAIST Core-A 1st generation processor family
  EM_COREA_2ND = 194,     // KIPO-KAIST Core-A 2nd generation processor family
  EM_ARC_COMPACT2 = 195,  // Synopsys ARCompact V2
  EM_OPEN8 = 196,         // Open8 8-bit RISC soft processor core
  EM_RL78 = 197,          // Renesas RL78 family
  EM_VIDEOCORE5 = 198,    // Broadcom VideoCore V processor
  EM_78KOR = 199,         // Renesas 78KOR family
  EM_56800EX = 200,       // Freescale 56800EX Digital Signal Controller (DSC)
  EM_BA1 = 201,           // Beyond BA1 CPU architecture
  EM_BA2 = 202,           // Beyond BA2 CPU architecture
  EM_XCORE = 203,         // XMOS xCORE processor family
  EM_MCHP_PIC = 204,      // Microchip 8-bit PIC(r) family
  EM_INTEL205 = 205,      // Reserved by Intel
  EM_INTEL206 = 206,      // Reserved by Intel
  EM_INTEL207 = 207,      // Reserved by Intel
  EM_INTEL208 = 208,      // Reserved by Intel
  EM_INTEL209 = 209,      // Reserved by Intel
  EM_KM32 = 210,          // KM211 KM32 32-bit processor
  EM_KMX32 = 211,         // KM211 KMX32 32-bit processor
  EM_KMX16 = 212,         // KM211 KMX16 16-bit processor
  EM_KMX8 = 213,          // KM211 KMX8 8-bit processor
  EM_KVARC = 214,         // KM211 KVARC processor
  EM_CDP = 215,           // Paneve CDP architecture family
  EM_COGE = 216,          // Cognitive Smart Memory Processor
  EM_COOL = 217,          // iCelero CoolEngine
  EM_NORC = 218,          // Nanoradio Optimized RISC
  EM_CSR_KALIMBA = 219,   // CSR Kalimba architecture family
  EM_AMDGPU = 224,        // AMD GPU architecture
  EM_RISCV = 243,         // RISC-V
  EM_LANAI = 244,         // Lanai 32-bit processor
  EM_BPF = 247,           // Linux kernel bpf virtual machine
  EM_VE = 251,            // NEC SX-Aurora VE
  EM_CSKY = 252,          // C-SKY 32-bit processor
  EM_LOONGARCH = 258,     // LoongArch
};

// Object file classes.
enum {
  ELFCLASSNONE = 0,
  ELFCLASS32 = 1, // 32-bit object file
  ELFCLASS64 = 2  // 64-bit object file
};

// Object file byte orderings.
enum {
  ELFDATANONE = 0, // Invalid data encoding.
  ELFDATA2LSB = 1, // Little-endian object file
  ELFDATA2MSB = 2  // Big-endian object file
};

// OS ABI identification.
enum {
  ELFOSABI_NONE = 0,           // UNIX System V ABI
  ELFOSABI_HPUX = 1,           // HP-UX operating system
  ELFOSABI_NETBSD = 2,         // NetBSD
  ELFOSABI_GNU = 3,            // GNU/Linux
  ELFOSABI_LINUX = 3,          // Historical alias for ELFOSABI_GNU.
  ELFOSABI_HURD = 4,           // GNU/Hurd
  ELFOSABI_SOLARIS = 6,        // Solaris
  ELFOSABI_AIX = 7,            // AIX
  ELFOSABI_IRIX = 8,           // IRIX
  ELFOSABI_FREEBSD = 9,        // FreeBSD
  ELFOSABI_TRU64 = 10,         // TRU64 UNIX
  ELFOSABI_MODESTO = 11,       // Novell Modesto
  ELFOSABI_OPENBSD = 12,       // OpenBSD
  ELFOSABI_OPENVMS = 13,       // OpenVMS
  ELFOSABI_NSK = 14,           // Hewlett-Packard Non-Stop Kernel
  ELFOSABI_AROS = 15,          // AROS
  ELFOSABI_FENIXOS = 16,       // FenixOS
  ELFOSABI_CLOUDABI = 17,      // Nuxi CloudABI
  ELFOSABI_CUDA = 51,          // NVIDIA CUDA architecture.
  ELFOSABI_FIRST_ARCH = 64,    // First architecture-specific OS ABI
  ELFOSABI_AMDGPU_HSA = 64,    // AMD HSA runtime
  ELFOSABI_AMDGPU_PAL = 65,    // AMD PAL runtime
  ELFOSABI_AMDGPU_MESA3D = 66, // AMD GCN GPUs (GFX6+) for MESA runtime
  ELFOSABI_ARM = 97,           // ARM
  ELFOSABI_ARM_FDPIC = 65,     // ARM FDPIC
  ELFOSABI_C6000_ELFABI = 64,  // Bare-metal TMS320C6000
  ELFOSABI_C6000_LINUX = 65,   // Linux TMS320C6000
  ELFOSABI_STANDALONE = 255,   // Standalone (embedded) application
  ELFOSABI_LAST_ARCH = 255     // Last Architecture-specific OS ABI
};

// AMDGPU OS ABI Version identification.
enum {
  // ELFABIVERSION_AMDGPU_HSA_V1 does not exist because OS ABI identification
  // was never defined for V1.
  ELFABIVERSION_AMDGPU_HSA_V2 = 0,
  ELFABIVERSION_AMDGPU_HSA_V3 = 1,
  ELFABIVERSION_AMDGPU_HSA_V4 = 2,
  ELFABIVERSION_AMDGPU_HSA_V5 = 3,
  ELFABIVERSION_AMDGPU_HSA_V6 = 4,
};

#define ELF_RELOC(name, value) name = value,

// X86_64 relocations.
enum {
#include "ELFRelocs/x86_64.def"
};

// i386 relocations.
enum {
#include "ELFRelocs/i386.def"
};

// ELF Relocation types for PPC32
enum {
#include "ELFRelocs/PowerPC.def"
};

// Specific e_flags for PPC64
enum {
  // e_flags bits specifying ABI:
  // 1 for original ABI using function descriptors,
  // 2 for revised ABI without function descriptors,
  // 0 for unspecified or not using any features affected by the differences.
  EF_PPC64_ABI = 3
};

// Special values for the st_other field in the symbol table entry for PPC64.
enum {
  STO_PPC64_LOCAL_BIT = 5,
  STO_PPC64_LOCAL_MASK = (7 << STO_PPC64_LOCAL_BIT)
};
static inline int64_t decodePPC64LocalEntryOffset(unsigned Other) {
  unsigned Val = (Other & STO_PPC64_LOCAL_MASK) >> STO_PPC64_LOCAL_BIT;
  return ((1 << Val) >> 2) << 2;
}

// ELF Relocation types for PPC64
enum {
#include "ELFRelocs/PowerPC64.def"
};

// ELF Relocation types for AArch64
enum {
#include "ELFRelocs/AArch64.def"
};

// Special values for the st_other field in the symbol table entry for AArch64.
enum {
  // Symbol may follow different calling convention than base PCS.
  STO_AARCH64_VARIANT_PCS = 0x80
};

// ARM Specific e_flags
enum : unsigned {
  EF_ARM_SOFT_FLOAT = 0x00000200U,     // Legacy pre EABI_VER5
  EF_ARM_ABI_FLOAT_SOFT = 0x00000200U, // EABI_VER5
  EF_ARM_VFP_FLOAT = 0x00000400U,      // Legacy pre EABI_VER5
  EF_ARM_ABI_FLOAT_HARD = 0x00000400U, // EABI_VER5
  EF_ARM_BE8 = 0x00800000U,
  EF_ARM_EABI_UNKNOWN = 0x00000000U,
  EF_ARM_EABI_VER1 = 0x01000000U,
  EF_ARM_EABI_VER2 = 0x02000000U,
  EF_ARM_EABI_VER3 = 0x03000000U,
  EF_ARM_EABI_VER4 = 0x04000000U,
  EF_ARM_EABI_VER5 = 0x05000000U,
  EF_ARM_EABIMASK = 0xFF000000U
};

// ELF Relocation types for ARM
enum {
#include "ELFRelocs/ARM.def"
};

// ARC Specific e_flags
enum : unsigned {
  EF_ARC_MACH_MSK = 0x000000ff,
  EF_ARC_OSABI_MSK = 0x00000f00,
  E_ARC_MACH_ARC600 = 0x00000002,
  E_ARC_MACH_ARC601 = 0x00000004,
  E_ARC_MACH_ARC700 = 0x00000003,
  EF_ARC_CPU_ARCV2EM = 0x00000005,
  EF_ARC_CPU_ARCV2HS = 0x00000006,
  E_ARC_OSABI_ORIG = 0x00000000,
  E_ARC_OSABI_V2 = 0x00000200,
  E_ARC_OSABI_V3 = 0x00000300,
  E_ARC_OSABI_V4 = 0x00000400,
  EF_ARC_PIC = 0x00000100
};

// ELF Relocation types for ARC
enum {
#include "ELFRelocs/ARC.def"
};

// AVR specific e_flags
enum : unsigned {
  EF_AVR_ARCH_AVR1 = 1,
  EF_AVR_ARCH_AVR2 = 2,
  EF_AVR_ARCH_AVR25 = 25,
  EF_AVR_ARCH_AVR3 = 3,
  EF_AVR_ARCH_AVR31 = 31,
  EF_AVR_ARCH_AVR35 = 35,
  EF_AVR_ARCH_AVR4 = 4,
  EF_AVR_ARCH_AVR5 = 5,
  EF_AVR_ARCH_AVR51 = 51,
  EF_AVR_ARCH_AVR6 = 6,
  EF_AVR_ARCH_AVRTINY = 100,
  EF_AVR_ARCH_XMEGA1 = 101,
  EF_AVR_ARCH_XMEGA2 = 102,
  EF_AVR_ARCH_XMEGA3 = 103,
  EF_AVR_ARCH_XMEGA4 = 104,
  EF_AVR_ARCH_XMEGA5 = 105,
  EF_AVR_ARCH_XMEGA6 = 106,
  EF_AVR_ARCH_XMEGA7 = 107,

  EF_AVR_ARCH_MASK = 0x7f, // EF_AVR_ARCH_xxx selection mask

  EF_AVR_LINKRELAX_PREPARED = 0x80, // The file is prepared for linker
                                    // relaxation to be applied
};

// ELF Relocation types for AVR
enum {
#include "ELFRelocs/AVR.def"
};

// Mips Specific e_flags
enum : unsigned {
  EF_MIPS_NOREORDER = 0x00000001, // Don't reorder instructions
  EF_MIPS_PIC = 0x00000002,       // Position independent code
  EF_MIPS_CPIC = 0x00000004,      // Call object with Position independent code
  EF_MIPS_ABI2 = 0x00000020,      // File uses N32 ABI
  EF_MIPS_32BITMODE = 0x00000100, // Code compiled for a 64-bit machine
                                  // in 32-bit mode
  EF_MIPS_FP64 = 0x00000200,      // Code compiled for a 32-bit machine
                                  // but uses 64-bit FP registers
  EF_MIPS_NAN2008 = 0x00000400,   // Uses IEE 754-2008 NaN encoding

  // ABI flags
  EF_MIPS_ABI_O32 = 0x00001000, // This file follows the first MIPS 32 bit ABI
  EF_MIPS_ABI_O64 = 0x00002000, // O32 ABI extended for 64-bit architecture.
  EF_MIPS_ABI_EABI32 = 0x00003000, // EABI in 32 bit mode.
  EF_MIPS_ABI_EABI64 = 0x00004000, // EABI in 64 bit mode.
  EF_MIPS_ABI = 0x0000f000,        // Mask for selecting EF_MIPS_ABI_ variant.

  // MIPS machine variant
  EF_MIPS_MACH_NONE = 0x00000000,    // A standard MIPS implementation.
  EF_MIPS_MACH_3900 = 0x00810000,    // Toshiba R3900
  EF_MIPS_MACH_4010 = 0x00820000,    // LSI R4010
  EF_MIPS_MACH_4100 = 0x00830000,    // NEC VR4100
  EF_MIPS_MACH_4650 = 0x00850000,    // MIPS R4650
  EF_MIPS_MACH_4120 = 0x00870000,    // NEC VR4120
  EF_MIPS_MACH_4111 = 0x00880000,    // NEC VR4111/VR4181
  EF_MIPS_MACH_SB1 = 0x008a0000,     // Broadcom SB-1
  EF_MIPS_MACH_OCTEON = 0x008b0000,  // Cavium Networks Octeon
  EF_MIPS_MACH_XLR = 0x008c0000,     // RMI Xlr
  EF_MIPS_MACH_OCTEON2 = 0x008d0000, // Cavium Networks Octeon2
  EF_MIPS_MACH_OCTEON3 = 0x008e0000, // Cavium Networks Octeon3
  EF_MIPS_MACH_5400 = 0x00910000,    // NEC VR5400
  EF_MIPS_MACH_5900 = 0x00920000,    // MIPS R5900
  EF_MIPS_MACH_5500 = 0x00980000,    // NEC VR5500
  EF_MIPS_MACH_9000 = 0x00990000,    // Unknown
  EF_MIPS_MACH_LS2E = 0x00a00000,    // ST Microelectronics Loongson 2E
  EF_MIPS_MACH_LS2F = 0x00a10000,    // ST Microelectronics Loongson 2F
  EF_MIPS_MACH_LS3A = 0x00a20000,    // Loongson 3A
  EF_MIPS_MACH = 0x00ff0000,         // EF_MIPS_MACH_xxx selection mask

  // ARCH_ASE
  EF_MIPS_MICROMIPS = 0x02000000,     // microMIPS
  EF_MIPS_ARCH_ASE_M16 = 0x04000000,  // Has Mips-16 ISA extensions
  EF_MIPS_ARCH_ASE_MDMX = 0x08000000, // Has MDMX multimedia extensions
  EF_MIPS_ARCH_ASE = 0x0f000000,      // Mask for EF_MIPS_ARCH_ASE_xxx flags

  // ARCH
  EF_MIPS_ARCH_1 = 0x00000000,    // MIPS1 instruction set
  EF_MIPS_ARCH_2 = 0x10000000,    // MIPS2 instruction set
  EF_MIPS_ARCH_3 = 0x20000000,    // MIPS3 instruction set
  EF_MIPS_ARCH_4 = 0x30000000,    // MIPS4 instruction set
  EF_MIPS_ARCH_5 = 0x40000000,    // MIPS5 instruction set
  EF_MIPS_ARCH_32 = 0x50000000,   // MIPS32 instruction set per linux not elf.h
  EF_MIPS_ARCH_64 = 0x60000000,   // MIPS64 instruction set per linux not elf.h
  EF_MIPS_ARCH_32R2 = 0x70000000, // mips32r2, mips32r3, mips32r5
  EF_MIPS_ARCH_64R2 = 0x80000000, // mips64r2, mips64r3, mips64r5
  EF_MIPS_ARCH_32R6 = 0x90000000, // mips32r6
  EF_MIPS_ARCH_64R6 = 0xa0000000, // mips64r6
  EF_MIPS_ARCH = 0xf0000000       // Mask for applying EF_MIPS_ARCH_ variant
};

// MIPS-specific section indexes
enum {
  SHN_MIPS_ACOMMON = 0xff00,   // Common symbols which are defined and allocated
  SHN_MIPS_TEXT = 0xff01,      // Not ABI compliant
  SHN_MIPS_DATA = 0xff02,      // Not ABI compliant
  SHN_MIPS_SCOMMON = 0xff03,   // Common symbols for global data area
  SHN_MIPS_SUNDEFINED = 0xff04 // Undefined symbols for global data area
};

// ELF Relocation types for Mips
enum {
#include "ELFRelocs/Mips.def"
};

// Special values for the st_other field in the symbol table entry for MIPS.
enum {
  STO_MIPS_OPTIONAL = 0x04,  // Symbol whose definition is optional
  STO_MIPS_PLT = 0x08,       // PLT entry related dynamic table record
  STO_MIPS_PIC = 0x20,       // PIC func in an object mixes PIC/non-PIC
  STO_MIPS_MICROMIPS = 0x80, // MIPS Specific ISA for MicroMips
  STO_MIPS_MIPS16 = 0xf0     // MIPS Specific ISA for Mips16
};

// .MIPS.options section descriptor kinds
enum {
  ODK_NULL = 0,       // Undefined
  ODK_REGINFO = 1,    // Register usage information
  ODK_EXCEPTIONS = 2, // Exception processing options
  ODK_PAD = 3,        // Section padding options
  ODK_HWPATCH = 4,    // Hardware patches applied
  ODK_FILL = 5,       // Linker fill value
  ODK_TAGS = 6,       // Space for tool identification
  ODK_HWAND = 7,      // Hardware AND patches applied
  ODK_HWOR = 8,       // Hardware OR patches applied
  ODK_GP_GROUP = 9,   // GP group to use for text/data sections
  ODK_IDENT = 10,     // ID information
  ODK_PAGESIZE = 11   // Page size information
};

// Hexagon-specific e_flags
enum {
  // Object processor version flags, bits[11:0]
  EF_HEXAGON_MACH_V2 = 0x00000001,   // Hexagon V2
  EF_HEXAGON_MACH_V3 = 0x00000002,   // Hexagon V3
  EF_HEXAGON_MACH_V4 = 0x00000003,   // Hexagon V4
  EF_HEXAGON_MACH_V5 = 0x00000004,   // Hexagon V5
  EF_HEXAGON_MACH_V55 = 0x00000005,  // Hexagon V55
  EF_HEXAGON_MACH_V60 = 0x00000060,  // Hexagon V60
  EF_HEXAGON_MACH_V62 = 0x00000062,  // Hexagon V62
  EF_HEXAGON_MACH_V65 = 0x00000065,  // Hexagon V65
  EF_HEXAGON_MACH_V66 = 0x00000066,  // Hexagon V66
  EF_HEXAGON_MACH_V67 = 0x00000067,  // Hexagon V67
  EF_HEXAGON_MACH_V67T = 0x00008067, // Hexagon V67T
  EF_HEXAGON_MACH_V68 = 0x00000068,  // Hexagon V68
  EF_HEXAGON_MACH_V69 = 0x00000069,  // Hexagon V69
  EF_HEXAGON_MACH_V71 = 0x00000071,  // Hexagon V71
  EF_HEXAGON_MACH_V71T = 0x00008071, // Hexagon V71T
  EF_HEXAGON_MACH_V73 = 0x00000073,  // Hexagon V73
  EF_HEXAGON_MACH = 0x000003ff,      // Hexagon V..

  // Highest ISA version flags
  EF_HEXAGON_ISA_MACH = 0x00000000, // Same as specified in bits[11:0]
                                    // of e_flags
  EF_HEXAGON_ISA_V2 = 0x00000010,   // Hexagon V2 ISA
  EF_HEXAGON_ISA_V3 = 0x00000020,   // Hexagon V3 ISA
  EF_HEXAGON_ISA_V4 = 0x00000030,   // Hexagon V4 ISA
  EF_HEXAGON_ISA_V5 = 0x00000040,   // Hexagon V5 ISA
  EF_HEXAGON_ISA_V55 = 0x00000050,  // Hexagon V55 ISA
  EF_HEXAGON_ISA_V60 = 0x00000060,  // Hexagon V60 ISA
  EF_HEXAGON_ISA_V62 = 0x00000062,  // Hexagon V62 ISA
  EF_HEXAGON_ISA_V65 = 0x00000065,  // Hexagon V65 ISA
  EF_HEXAGON_ISA_V66 = 0x00000066,  // Hexagon V66 ISA
  EF_HEXAGON_ISA_V67 = 0x00000067,  // Hexagon V67 ISA
  EF_HEXAGON_ISA_V68 = 0x00000068,  // Hexagon V68 ISA
  EF_HEXAGON_ISA_V69 = 0x00000069,  // Hexagon V69 ISA
  EF_HEXAGON_ISA_V71 = 0x00000071,  // Hexagon V71 ISA
  EF_HEXAGON_ISA_V73 = 0x00000073,  // Hexagon V73 ISA
  EF_HEXAGON_ISA_V75 = 0x00000075,  // Hexagon V75 ISA
  EF_HEXAGON_ISA = 0x000003ff,      // Hexagon V.. ISA
};

// Hexagon-specific section indexes for common small data
enum {
  SHN_HEXAGON_SCOMMON = 0xff00,   // Other access sizes
  SHN_HEXAGON_SCOMMON_1 = 0xff01, // Byte-sized access
  SHN_HEXAGON_SCOMMON_2 = 0xff02, // Half-word-sized access
  SHN_HEXAGON_SCOMMON_4 = 0xff03, // Word-sized access
  SHN_HEXAGON_SCOMMON_8 = 0xff04  // Double-word-size access
};

// ELF Relocation types for Hexagon
enum {
#include "ELFRelocs/Hexagon.def"
};

// ELF Relocation type for Lanai.
enum {
#include "ELFRelocs/Lanai.def"
};

// RISCV Specific e_flags
enum : unsigned {
  EF_RISCV_RVC = 0x0001,
  EF_RISCV_FLOAT_ABI = 0x0006,
  EF_RISCV_FLOAT_ABI_SOFT = 0x0000,
  EF_RISCV_FLOAT_ABI_SINGLE = 0x0002,
  EF_RISCV_FLOAT_ABI_DOUBLE = 0x0004,
  EF_RISCV_FLOAT_ABI_QUAD = 0x0006,
  EF_RISCV_RVE = 0x0008,
  EF_RISCV_TSO = 0x0010,
};

// ELF Relocation types for RISC-V
enum {
#include "ELFRelocs/RISCV.def"
};

enum {
  // Symbol may follow different calling convention than the standard calling
  // convention.
  STO_RISCV_VARIANT_CC = 0x80
};

// ELF Relocation types for S390/zSeries
enum {
#include "ELFRelocs/SystemZ.def"
};

// ELF Relocation type for Sparc.
enum {
#include "ELFRelocs/Sparc.def"
};

// AMDGPU specific e_flags.
enum : unsigned {
  // Processor selection mask for EF_AMDGPU_MACH_* values.
  EF_AMDGPU_MACH = 0x0ff,

  // Not specified processor.
  EF_AMDGPU_MACH_NONE = 0x000,

  // R600-based processors.

  // Radeon HD 2000/3000 Series (R600).
  EF_AMDGPU_MACH_R600_R600 = 0x001,
  EF_AMDGPU_MACH_R600_R630 = 0x002,
  EF_AMDGPU_MACH_R600_RS880 = 0x003,
  EF_AMDGPU_MACH_R600_RV670 = 0x004,
  // Radeon HD 4000 Series (R700).
  EF_AMDGPU_MACH_R600_RV710 = 0x005,
  EF_AMDGPU_MACH_R600_RV730 = 0x006,
  EF_AMDGPU_MACH_R600_RV770 = 0x007,
  // Radeon HD 5000 Series (Evergreen).
  EF_AMDGPU_MACH_R600_CEDAR = 0x008,
  EF_AMDGPU_MACH_R600_CYPRESS = 0x009,
  EF_AMDGPU_MACH_R600_JUNIPER = 0x00a,
  EF_AMDGPU_MACH_R600_REDWOOD = 0x00b,
  EF_AMDGPU_MACH_R600_SUMO = 0x00c,
  // Radeon HD 6000 Series (Northern Islands).
  EF_AMDGPU_MACH_R600_BARTS = 0x00d,
  EF_AMDGPU_MACH_R600_CAICOS = 0x00e,
  EF_AMDGPU_MACH_R600_CAYMAN = 0x00f,
  EF_AMDGPU_MACH_R600_TURKS = 0x010,

  // Reserved for R600-based processors.
  EF_AMDGPU_MACH_R600_RESERVED_FIRST = 0x011,
  EF_AMDGPU_MACH_R600_RESERVED_LAST = 0x01f,

  // First/last R600-based processors.
  EF_AMDGPU_MACH_R600_FIRST = EF_AMDGPU_MACH_R600_R600,
  EF_AMDGPU_MACH_R600_LAST = EF_AMDGPU_MACH_R600_TURKS,

  // AMDGCN-based processors.
  // clang-format off
  EF_AMDGPU_MACH_AMDGCN_GFX600          = 0x020,
  EF_AMDGPU_MACH_AMDGCN_GFX601          = 0x021,
  EF_AMDGPU_MACH_AMDGCN_GFX700          = 0x022,
  EF_AMDGPU_MACH_AMDGCN_GFX701          = 0x023,
  EF_AMDGPU_MACH_AMDGCN_GFX702          = 0x024,
  EF_AMDGPU_MACH_AMDGCN_GFX703          = 0x025,
  EF_AMDGPU_MACH_AMDGCN_GFX704          = 0x026,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X27   = 0x027,
  EF_AMDGPU_MACH_AMDGCN_GFX801          = 0x028,
  EF_AMDGPU_MACH_AMDGCN_GFX802          = 0x029,
  EF_AMDGPU_MACH_AMDGCN_GFX803          = 0x02a,
  EF_AMDGPU_MACH_AMDGCN_GFX810          = 0x02b,
  EF_AMDGPU_MACH_AMDGCN_GFX900          = 0x02c,
  EF_AMDGPU_MACH_AMDGCN_GFX902          = 0x02d,
  EF_AMDGPU_MACH_AMDGCN_GFX904          = 0x02e,
  EF_AMDGPU_MACH_AMDGCN_GFX906          = 0x02f,
  EF_AMDGPU_MACH_AMDGCN_GFX908          = 0x030,
  EF_AMDGPU_MACH_AMDGCN_GFX909          = 0x031,
  EF_AMDGPU_MACH_AMDGCN_GFX90C          = 0x032,
  EF_AMDGPU_MACH_AMDGCN_GFX1010         = 0x033,
  EF_AMDGPU_MACH_AMDGCN_GFX1011         = 0x034,
  EF_AMDGPU_MACH_AMDGCN_GFX1012         = 0x035,
  EF_AMDGPU_MACH_AMDGCN_GFX1030         = 0x036,
  EF_AMDGPU_MACH_AMDGCN_GFX1031         = 0x037,
  EF_AMDGPU_MACH_AMDGCN_GFX1032         = 0x038,
  EF_AMDGPU_MACH_AMDGCN_GFX1033         = 0x039,
  EF_AMDGPU_MACH_AMDGCN_GFX602          = 0x03a,
  EF_AMDGPU_MACH_AMDGCN_GFX705          = 0x03b,
  EF_AMDGPU_MACH_AMDGCN_GFX805          = 0x03c,
  EF_AMDGPU_MACH_AMDGCN_GFX1035         = 0x03d,
  EF_AMDGPU_MACH_AMDGCN_GFX1034         = 0x03e,
  EF_AMDGPU_MACH_AMDGCN_GFX90A          = 0x03f,
  EF_AMDGPU_MACH_AMDGCN_GFX940          = 0x040,
  EF_AMDGPU_MACH_AMDGCN_GFX1100         = 0x041,
  EF_AMDGPU_MACH_AMDGCN_GFX1013         = 0x042,
  EF_AMDGPU_MACH_AMDGCN_GFX1150         = 0x043,
  EF_AMDGPU_MACH_AMDGCN_GFX1103         = 0x044,
  EF_AMDGPU_MACH_AMDGCN_GFX1036         = 0x045,
  EF_AMDGPU_MACH_AMDGCN_GFX1101         = 0x046,
  EF_AMDGPU_MACH_AMDGCN_GFX1102         = 0x047,
  EF_AMDGPU_MACH_AMDGCN_GFX1200         = 0x048,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X49   = 0x049,
  EF_AMDGPU_MACH_AMDGCN_GFX1151         = 0x04a,
  EF_AMDGPU_MACH_AMDGCN_GFX941          = 0x04b,
  EF_AMDGPU_MACH_AMDGCN_GFX942          = 0x04c,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X4D   = 0x04d,
  EF_AMDGPU_MACH_AMDGCN_GFX1201         = 0x04e,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X4F   = 0x04f,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X50   = 0x050,
  EF_AMDGPU_MACH_AMDGCN_GFX9_GENERIC    = 0x051,
  EF_AMDGPU_MACH_AMDGCN_GFX10_1_GENERIC = 0x052,
  EF_AMDGPU_MACH_AMDGCN_GFX10_3_GENERIC = 0x053,
  EF_AMDGPU_MACH_AMDGCN_GFX11_GENERIC   = 0x054,
  EF_AMDGPU_MACH_AMDGCN_GFX1152         = 0x055,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X56   = 0x056,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X57   = 0x057,
  EF_AMDGPU_MACH_AMDGCN_RESERVED_0X58   = 0x058,
  EF_AMDGPU_MACH_AMDGCN_GFX12_GENERIC   = 0x059,
  // clang-format on

  // First/last AMDGCN-based processors.
  EF_AMDGPU_MACH_AMDGCN_FIRST = EF_AMDGPU_MACH_AMDGCN_GFX600,
  EF_AMDGPU_MACH_AMDGCN_LAST = EF_AMDGPU_MACH_AMDGCN_GFX12_GENERIC,

  // Indicates if the "xnack" target feature is enabled for all code contained
  // in the object.
  //
  // Only valid for ELFOSABI_AMDGPU_HSA and ELFABIVERSION_AMDGPU_HSA_V2.
  EF_AMDGPU_FEATURE_XNACK_V2 = 0x01,
  // Indicates if the trap handler is enabled for all code contained
  // in the object.
  //
  // Only valid for ELFOSABI_AMDGPU_HSA and ELFABIVERSION_AMDGPU_HSA_V2.
  EF_AMDGPU_FEATURE_TRAP_HANDLER_V2 = 0x02,

  // Indicates if the "xnack" target feature is enabled for all code contained
  // in the object.
  //
  // Only valid for ELFOSABI_AMDGPU_HSA and ELFABIVERSION_AMDGPU_HSA_V3.
  EF_AMDGPU_FEATURE_XNACK_V3 = 0x100,
  // Indicates if the "sramecc" target feature is enabled for all code
  // contained in the object.
  //
  // Only valid for ELFOSABI_AMDGPU_HSA and ELFABIVERSION_AMDGPU_HSA_V3.
  EF_AMDGPU_FEATURE_SRAMECC_V3 = 0x200,

  // XNACK selection mask for EF_AMDGPU_FEATURE_XNACK_* values.
  //
  // Only valid for ELFOSABI_AMDGPU_HSA and ELFABIVERSION_AMDGPU_HSA_V4.
  EF_AMDGPU_FEATURE_XNACK_V4 = 0x300,
  // XNACK is not supported.
  EF_AMDGPU_FEATURE_XNACK_UNSUPPORTED_V4 = 0x000,
  // XNACK is any/default/unspecified.
  EF_AMDGPU_FEATURE_XNACK_ANY_V4 = 0x100,
  // XNACK is off.
  EF_AMDGPU_FEATURE_XNACK_OFF_V4 = 0x200,
  // XNACK is on.
  EF_AMDGPU_FEATURE_XNACK_ON_V4 = 0x300,

  // SRAMECC selection mask for EF_AMDGPU_FEATURE_SRAMECC_* values.
  //
  // Only valid for ELFOSABI_AMDGPU_HSA and ELFABIVERSION_AMDGPU_HSA_V4.
  EF_AMDGPU_FEATURE_SRAMECC_V4 = 0xc00,
  // SRAMECC is not supported.
  EF_AMDGPU_FEATURE_SRAMECC_UNSUPPORTED_V4 = 0x000,
  // SRAMECC is any/default/unspecified.
  EF_AMDGPU_FEATURE_SRAMECC_ANY_V4 = 0x400,
  // SRAMECC is off.
  EF_AMDGPU_FEATURE_SRAMECC_OFF_V4 = 0x800,
  // SRAMECC is on.
  EF_AMDGPU_FEATURE_SRAMECC_ON_V4 = 0xc00,

  // Generic target versioning. This is contained in the list byte of EFLAGS.
  EF_AMDGPU_GENERIC_VERSION = 0xff000000,
  EF_AMDGPU_GENERIC_VERSION_OFFSET = 24,
  EF_AMDGPU_GENERIC_VERSION_MIN = 1,
  EF_AMDGPU_GENERIC_VERSION_MAX = 0xff,
};

// ELF Relocation types for AMDGPU
enum {
#include "ELFRelocs/AMDGPU.def"
};

// NVPTX specific e_flags.
enum : unsigned {
  // Processor selection mask for EF_CUDA_SM* values.
  EF_CUDA_SM = 0xff,

  // SM based processor values.
  EF_CUDA_SM20 = 0x14,
  EF_CUDA_SM21 = 0x15,
  EF_CUDA_SM30 = 0x1e,
  EF_CUDA_SM32 = 0x20,
  EF_CUDA_SM35 = 0x23,
  EF_CUDA_SM37 = 0x25,
  EF_CUDA_SM50 = 0x32,
  EF_CUDA_SM52 = 0x34,
  EF_CUDA_SM53 = 0x35,
  EF_CUDA_SM60 = 0x3c,
  EF_CUDA_SM61 = 0x3d,
  EF_CUDA_SM62 = 0x3e,
  EF_CUDA_SM70 = 0x46,
  EF_CUDA_SM72 = 0x48,
  EF_CUDA_SM75 = 0x4b,
  EF_CUDA_SM80 = 0x50,
  EF_CUDA_SM86 = 0x56,
  EF_CUDA_SM87 = 0x57,
  EF_CUDA_SM89 = 0x59,
  // The sm_90a variant uses the same machine flag.
  EF_CUDA_SM90 = 0x5a,

  // Unified texture binding is enabled.
  EF_CUDA_TEXMODE_UNIFIED = 0x100,
  // Independent texture binding is enabled.
  EF_CUDA_TEXMODE_INDEPENDANT = 0x200,
  // The target is using 64-bit addressing.
  EF_CUDA_64BIT_ADDRESS = 0x400,
  // Set when using the sm_90a processor.
  EF_CUDA_ACCELERATORS = 0x800,
  // Undocumented software feature.
  EF_CUDA_SW_FLAG_V2 = 0x1000,

  // Virtual processor selection mask for EF_CUDA_VIRTUAL_SM* values.
  EF_CUDA_VIRTUAL_SM = 0xff0000,
};

// ELF Relocation types for BPF
enum {
#include "ELFRelocs/BPF.def"
};

// ELF Relocation types for M68k
enum {
#include "ELFRelocs/M68k.def"
};

// MSP430 specific e_flags
enum : unsigned {
  EF_MSP430_MACH_MSP430x11 = 11,
  EF_MSP430_MACH_MSP430x11x1 = 110,
  EF_MSP430_MACH_MSP430x12 = 12,
  EF_MSP430_MACH_MSP430x13 = 13,
  EF_MSP430_MACH_MSP430x14 = 14,
  EF_MSP430_MACH_MSP430x15 = 15,
  EF_MSP430_MACH_MSP430x16 = 16,
  EF_MSP430_MACH_MSP430x20 = 20,
  EF_MSP430_MACH_MSP430x22 = 22,
  EF_MSP430_MACH_MSP430x23 = 23,
  EF_MSP430_MACH_MSP430x24 = 24,
  EF_MSP430_MACH_MSP430x26 = 26,
  EF_MSP430_MACH_MSP430x31 = 31,
  EF_MSP430_MACH_MSP430x32 = 32,
  EF_MSP430_MACH_MSP430x33 = 33,
  EF_MSP430_MACH_MSP430x41 = 41,
  EF_MSP430_MACH_MSP430x42 = 42,
  EF_MSP430_MACH_MSP430x43 = 43,
  EF_MSP430_MACH_MSP430x44 = 44,
  EF_MSP430_MACH_MSP430X = 45,
  EF_MSP430_MACH_MSP430x46 = 46,
  EF_MSP430_MACH_MSP430x47 = 47,
  EF_MSP430_MACH_MSP430x54 = 54,
};

// ELF Relocation types for MSP430
enum {
#include "ELFRelocs/MSP430.def"
};

// ELF Relocation type for VE.
enum {
#include "ELFRelocs/VE.def"
};

// CSKY Specific e_flags
enum : unsigned {
  EF_CSKY_801 = 0xa,
  EF_CSKY_802 = 0x10,
  EF_CSKY_803 = 0x9,
  EF_CSKY_805 = 0x11,
  EF_CSKY_807 = 0x6,
  EF_CSKY_810 = 0x8,
  EF_CSKY_860 = 0xb,
  EF_CSKY_800 = 0x1f,
  EF_CSKY_FLOAT = 0x2000,
  EF_CSKY_DSP = 0x4000,
  EF_CSKY_ABIV2 = 0x20000000,
  EF_CSKY_EFV1 = 0x1000000,
  EF_CSKY_EFV2 = 0x2000000,
  EF_CSKY_EFV3 = 0x3000000
};

// ELF Relocation types for CSKY
enum {
#include "ELFRelocs/CSKY.def"
};

// LoongArch Specific e_flags
enum : unsigned {
  // Definitions from LoongArch ELF psABI v2.01.
  // Reference: https://github.com/loongson/LoongArch-Documentation
  // (commit hash 296de4def055c871809068e0816325a4ac04eb12)

  // Base ABI Modifiers
  EF_LOONGARCH_ABI_SOFT_FLOAT    = 0x1,
  EF_LOONGARCH_ABI_SINGLE_FLOAT  = 0x2,
  EF_LOONGARCH_ABI_DOUBLE_FLOAT  = 0x3,
  EF_LOONGARCH_ABI_MODIFIER_MASK = 0x7,

  // Object file ABI versions
  EF_LOONGARCH_OBJABI_V0   = 0x0,
  EF_LOONGARCH_OBJABI_V1   = 0x40,
  EF_LOONGARCH_OBJABI_MASK = 0xC0,
};

// ELF Relocation types for LoongArch
enum {
#include "ELFRelocs/LoongArch.def"
};

// Xtensa specific e_flags
enum : unsigned {
  // Four-bit Xtensa machine type mask.
  EF_XTENSA_MACH = 0x0000000f,
  // Various CPU types.
  EF_XTENSA_MACH_NONE = 0x00000000, // A base Xtensa implementation
  EF_XTENSA_XT_INSN = 0x00000100,
  EF_XTENSA_XT_LIT = 0x00000200,
};

// ELF Relocation types for Xtensa
enum {
#include "ELFRelocs/Xtensa.def"
};

#undef ELF_RELOC

// Section header.
struct Elf32_Shdr {
  Elf32_Word sh_name;      // Section name (index into string table)
  Elf32_Word sh_type;      // Section type (SHT_*)
  Elf32_Word sh_flags;     // Section flags (SHF_*)
  Elf32_Addr sh_addr;      // Address where section is to be loaded
  Elf32_Off sh_offset;     // File offset of section data, in bytes
  Elf32_Word sh_size;      // Size of section, in bytes
  Elf32_Word sh_link;      // Section type-specific header table index link
  Elf32_Word sh_info;      // Section type-specific extra information
  Elf32_Word sh_addralign; // Section address alignment
  Elf32_Word sh_entsize;   // Size of records contained within the section
};

// Section header for ELF64 - same fields as ELF32, different types.
struct Elf64_Shdr {
  Elf64_Word sh_name;
  Elf64_Word sh_type;
  Elf64_Xword sh_flags;
  Elf64_Addr sh_addr;
  Elf64_Off sh_offset;
  Elf64_Xword sh_size;
  Elf64_Word sh_link;
  Elf64_Word sh_info;
  Elf64_Xword sh_addralign;
  Elf64_Xword sh_entsize;
};

// Special section indices.
enum {
  SHN_UNDEF = 0,          // Undefined, missing, irrelevant, or meaningless
  SHN_LORESERVE = 0xff00, // Lowest reserved index
  SHN_LOPROC = 0xff00,    // Lowest processor-specific index
  SHN_HIPROC = 0xff1f,    // Highest processor-specific index
  SHN_LOOS = 0xff20,      // Lowest operating system-specific index
  SHN_HIOS = 0xff3f,      // Highest operating system-specific index
  SHN_ABS = 0xfff1,       // Symbol has absolute value; does not need relocation
  SHN_COMMON = 0xfff2,    // FORTRAN COMMON or C external global variables
  SHN_XINDEX = 0xffff,    // Mark that the index is >= SHN_LORESERVE
  SHN_HIRESERVE = 0xffff  // Highest reserved index
};

// Section types.
enum : unsigned {
  SHT_NULL = 0,           // No associated section (inactive entry).
  SHT_PROGBITS = 1,       // Program-defined contents.
  SHT_SYMTAB = 2,         // Symbol table.
  SHT_STRTAB = 3,         // String table.
  SHT_RELA = 4,           // Relocation entries; explicit addends.
  SHT_HASH = 5,           // Symbol hash table.
  SHT_DYNAMIC = 6,        // Information for dynamic linking.
  SHT_NOTE = 7,           // Information about the file.
  SHT_NOBITS = 8,         // Data occupies no space in the file.
  SHT_REL = 9,            // Relocation entries; no explicit addends.
  SHT_SHLIB = 10,         // Reserved.
  SHT_DYNSYM = 11,        // Symbol table.
  SHT_INIT_ARRAY = 14,    // Pointers to initialization functions.
  SHT_FINI_ARRAY = 15,    // Pointers to termination functions.
  SHT_PREINIT_ARRAY = 16, // Pointers to pre-init functions.
  SHT_GROUP = 17,         // Section group.
  SHT_SYMTAB_SHNDX = 18,  // Indices for SHN_XINDEX entries.
  // Experimental support for SHT_RELR sections. For details, see proposal
  // at https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
  SHT_RELR = 19, // Relocation entries; only offsets.
  // TODO: Experimental CREL relocations. LLVM will change the value and
  // break compatibility in the future.
  SHT_CREL = 0x40000014,
  SHT_LOOS = 0x60000000, // Lowest operating system-specific type.
  // Android packed relocation section types.
  // https://android.googlesource.com/platform/bionic/+/6f12bfece5dcc01325e0abba56a46b1bcf991c69/tools/relocation_packer/src/elf_file.cc#37
  SHT_ANDROID_REL = 0x60000001,
  SHT_ANDROID_RELA = 0x60000002,
  SHT_LLVM_ODRTAB = 0x6fff4c00,         // LLVM ODR table.
  SHT_LLVM_LINKER_OPTIONS = 0x6fff4c01, // LLVM Linker Options.
  SHT_LLVM_ADDRSIG = 0x6fff4c03,        // List of address-significant symbols
                                        // for safe ICF.
  SHT_LLVM_DEPENDENT_LIBRARIES =
      0x6fff4c04,                  // LLVM Dependent Library Specifiers.
  SHT_LLVM_SYMPART = 0x6fff4c05,   // Symbol partition specification.
  SHT_LLVM_PART_EHDR = 0x6fff4c06, // ELF header for loadable partition.
  SHT_LLVM_PART_PHDR = 0x6fff4c07, // Phdrs for loadable partition.
  SHT_LLVM_BB_ADDR_MAP_V0 =
      0x6fff4c08, // LLVM Basic Block Address Map (old version kept for
                  // backward-compatibility).
  SHT_LLVM_CALL_GRAPH_PROFILE = 0x6fff4c09, // LLVM Call Graph Profile.
  SHT_LLVM_BB_ADDR_MAP = 0x6fff4c0a,        // LLVM Basic Block Address Map.
  SHT_LLVM_OFFLOADING = 0x6fff4c0b,         // LLVM device offloading data.
  SHT_LLVM_LTO = 0x6fff4c0c,                // .llvm.lto for fat LTO.
  // Android's experimental support for SHT_RELR sections.
  // https://android.googlesource.com/platform/bionic/+/b7feec74547f84559a1467aca02708ff61346d2a/libc/include/elf.h#512
  SHT_ANDROID_RELR = 0x6fffff00,   // Relocation entries; only offsets.
  SHT_GNU_ATTRIBUTES = 0x6ffffff5, // Object attributes.
  SHT_GNU_HASH = 0x6ffffff6,       // GNU-style hash table.
  SHT_GNU_verdef = 0x6ffffffd,     // GNU version definitions.
  SHT_GNU_verneed = 0x6ffffffe,    // GNU version references.
  SHT_GNU_versym = 0x6fffffff,     // GNU symbol versions table.
  SHT_HIOS = 0x6fffffff,           // Highest operating system-specific type.
  SHT_LOPROC = 0x70000000,         // Lowest processor arch-specific type.
  // Fixme: All this is duplicated in MCSectionELF. Why??
  // Exception Index table
  SHT_ARM_EXIDX = 0x70000001U,
  // BPABI DLL dynamic linking pre-emption map
  SHT_ARM_PREEMPTMAP = 0x70000002U,
  //  Object file compatibility attributes
  SHT_ARM_ATTRIBUTES = 0x70000003U,
  SHT_ARM_DEBUGOVERLAY = 0x70000004U,
  SHT_ARM_OVERLAYSECTION = 0x70000005U,
  // Special aarch64-specific section for MTE support, as described in:
  // https://github.com/ARM-software/abi-aa/blob/main/pauthabielf64/pauthabielf64.rst#section-types
  SHT_AARCH64_AUTH_RELR = 0x70000004U,
  // Special aarch64-specific sections for MTE support, as described in:
  // https://github.com/ARM-software/abi-aa/blob/main/memtagabielf64/memtagabielf64.rst#7section-types
  SHT_AARCH64_MEMTAG_GLOBALS_STATIC = 0x70000007U,
  SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC = 0x70000008U,
  SHT_HEX_ORDERED = 0x70000000,   // Link editor is to sort the entries in
                                  // this section based on their sizes
  SHT_X86_64_UNWIND = 0x70000001, // Unwind information

  SHT_MIPS_REGINFO = 0x70000006,  // Register usage information
  SHT_MIPS_OPTIONS = 0x7000000d,  // General options
  SHT_MIPS_DWARF = 0x7000001e,    // DWARF debugging section.
  SHT_MIPS_ABIFLAGS = 0x7000002a, // ABI information.

  SHT_MSP430_ATTRIBUTES = 0x70000003U,

  SHT_RISCV_ATTRIBUTES = 0x70000003U,

  SHT_CSKY_ATTRIBUTES = 0x70000001U,

  SHT_HEXAGON_ATTRIBUTES = 0x70000003U,

  SHT_HIPROC = 0x7fffffff, // Highest processor arch-specific type.
  SHT_LOUSER = 0x80000000, // Lowest type reserved for applications.
  SHT_HIUSER = 0xffffffff  // Highest type reserved for applications.
};

// Section flags.
enum : unsigned {
  // Section data should be writable during execution.
  SHF_WRITE = 0x1,

  // Section occupies memory during program execution.
  SHF_ALLOC = 0x2,

  // Section contains executable machine instructions.
  SHF_EXECINSTR = 0x4,

  // The data in this section may be merged.
  SHF_MERGE = 0x10,

  // The data in this section is null-terminated strings.
  SHF_STRINGS = 0x20,

  // A field in this section holds a section header table index.
  SHF_INFO_LINK = 0x40U,

  // Adds special ordering requirements for link editors.
  SHF_LINK_ORDER = 0x80U,

  // This section requires special OS-specific processing to avoid incorrect
  // behavior.
  SHF_OS_NONCONFORMING = 0x100U,

  // This section is a member of a section group.
  SHF_GROUP = 0x200U,

  // This section holds Thread-Local Storage.
  SHF_TLS = 0x400U,

  // Identifies a section containing compressed data.
  SHF_COMPRESSED = 0x800U,

  // This section should not be garbage collected by the linker.
  SHF_GNU_RETAIN = 0x200000,

  // This section is excluded from the final executable or shared library.
  SHF_EXCLUDE = 0x80000000U,

  // Start of target-specific flags.

  SHF_MASKOS = 0x0ff00000,

  // Solaris equivalent of SHF_GNU_RETAIN.
  SHF_SUNW_NODISCARD = 0x00100000,

  // Bits indicating processor-specific flags.
  SHF_MASKPROC = 0xf0000000,

  /// All sections with the "d" flag are grouped together by the linker to form
  /// the data section and the dp register is set to the start of the section by
  /// the boot code.
  XCORE_SHF_DP_SECTION = 0x10000000,

  /// All sections with the "c" flag are grouped together by the linker to form
  /// the constant pool and the cp register is set to the start of the constant
  /// pool by the boot code.
  XCORE_SHF_CP_SECTION = 0x20000000,

  // If an object file section does not have this flag set, then it may not hold
  // more than 2GB and can be freely referred to in objects using smaller code
  // models. Otherwise, only objects using larger code models can refer to them.
  // For example, a medium code model object can refer to data in a section that
  // sets this flag besides being able to refer to data in a section that does
  // not set it; likewise, a small code model object can refer only to code in a
  // section that does not set this flag.
  SHF_X86_64_LARGE = 0x10000000,

  // All sections with the GPREL flag are grouped into a global data area
  // for faster accesses
  SHF_HEX_GPREL = 0x10000000,

  // Section contains text/data which may be replicated in other sections.
  // Linker must retain only one copy.
  SHF_MIPS_NODUPES = 0x01000000,

  // Linker must generate implicit hidden weak names.
  SHF_MIPS_NAMES = 0x02000000,

  // Section data local to process.
  SHF_MIPS_LOCAL = 0x04000000,

  // Do not strip this section.
  SHF_MIPS_NOSTRIP = 0x08000000,

  // Section must be part of global data area.
  SHF_MIPS_GPREL = 0x10000000,

  // This section should be merged.
  SHF_MIPS_MERGE = 0x20000000,

  // Address size to be inferred from section entry size.
  SHF_MIPS_ADDR = 0x40000000,

  // Section data is string data by default.
  SHF_MIPS_STRING = 0x80000000,

  // Make code section unreadable when in execute-only mode
  SHF_ARM_PURECODE = 0x20000000
};

// Section Group Flags
enum : unsigned {
  GRP_COMDAT = 0x1,
  GRP_MASKOS = 0x0ff00000,
  GRP_MASKPROC = 0xf0000000
};

// Symbol table entries for ELF32.
struct Elf32_Sym {
  Elf32_Word st_name;     // Symbol name (index into string table)
  Elf32_Addr st_value;    // Value or address associated with the symbol
  Elf32_Word st_size;     // Size of the symbol
  unsigned char st_info;  // Symbol's type and binding attributes
  unsigned char st_other; // Must be zero; reserved
  Elf32_Half st_shndx;    // Which section (header table index) it's defined in

  // These accessors and mutators correspond to the ELF32_ST_BIND,
  // ELF32_ST_TYPE, and ELF32_ST_INFO macros defined in the ELF specification:
  unsigned char getBinding() const { return st_info >> 4; }
  unsigned char getType() const { return st_info & 0x0f; }
  void setBinding(unsigned char b) { setBindingAndType(b, getType()); }
  void setType(unsigned char t) { setBindingAndType(getBinding(), t); }
  void setBindingAndType(unsigned char b, unsigned char t) {
    st_info = (b << 4) + (t & 0x0f);
  }
};

// Symbol table entries for ELF64.
struct Elf64_Sym {
  Elf64_Word st_name;     // Symbol name (index into string table)
  unsigned char st_info;  // Symbol's type and binding attributes
  unsigned char st_other; // Must be zero; reserved
  Elf64_Half st_shndx;    // Which section (header tbl index) it's defined in
  Elf64_Addr st_value;    // Value or address associated with the symbol
  Elf64_Xword st_size;    // Size of the symbol

  // These accessors and mutators are identical to those defined for ELF32
  // symbol table entries.
  unsigned char getBinding() const { return st_info >> 4; }
  unsigned char getType() const { return st_info & 0x0f; }
  void setBinding(unsigned char b) { setBindingAndType(b, getType()); }
  void setType(unsigned char t) { setBindingAndType(getBinding(), t); }
  void setBindingAndType(unsigned char b, unsigned char t) {
    st_info = (b << 4) + (t & 0x0f);
  }
};

// The size (in bytes) of symbol table entries.
enum {
  SYMENTRY_SIZE32 = 16, // 32-bit symbol entry size
  SYMENTRY_SIZE64 = 24  // 64-bit symbol entry size.
};

// Symbol bindings.
enum {
  STB_LOCAL = 0,  // Local symbol, not visible outside obj file containing def
  STB_GLOBAL = 1, // Global symbol, visible to all object files being combined
  STB_WEAK = 2,   // Weak symbol, like global but lower-precedence
  STB_GNU_UNIQUE = 10,
  STB_LOOS = 10,   // Lowest operating system-specific binding type
  STB_HIOS = 12,   // Highest operating system-specific binding type
  STB_LOPROC = 13, // Lowest processor-specific binding type
  STB_HIPROC = 15  // Highest processor-specific binding type
};

// Symbol types.
enum {
  STT_NOTYPE = 0,     // Symbol's type is not specified
  STT_OBJECT = 1,     // Symbol is a data object (variable, array, etc.)
  STT_FUNC = 2,       // Symbol is executable code (function, etc.)
  STT_SECTION = 3,    // Symbol refers to a section
  STT_FILE = 4,       // Local, absolute symbol that refers to a file
  STT_COMMON = 5,     // An uninitialized common block
  STT_TLS = 6,        // Thread local data object
  STT_GNU_IFUNC = 10, // GNU indirect function
  STT_LOOS = 10,      // Lowest operating system-specific symbol type
  STT_HIOS = 12,      // Highest operating system-specific symbol type
  STT_LOPROC = 13,    // Lowest processor-specific symbol type
  STT_HIPROC = 15,    // Highest processor-specific symbol type

  // AMDGPU symbol types
  STT_AMDGPU_HSA_KERNEL = 10
};

enum {
  STV_DEFAULT = 0,  // Visibility is specified by binding type
  STV_INTERNAL = 1, // Defined by processor supplements
  STV_HIDDEN = 2,   // Not visible to other components
  STV_PROTECTED = 3 // Visible in other components but not preemptable
};

// Symbol number.
enum { STN_UNDEF = 0 };

// Special relocation symbols used in the MIPS64 ELF relocation entries
enum {
  RSS_UNDEF = 0, // None
  RSS_GP = 1,    // Value of gp
  RSS_GP0 = 2,   // Value of gp used to create object being relocated
  RSS_LOC = 3    // Address of location being relocated
};

// Relocation entry, without explicit addend.
struct Elf32_Rel {
  Elf32_Addr r_offset; // Location (file byte offset, or program virtual addr)
  Elf32_Word r_info;   // Symbol table index and type of relocation to apply

  // These accessors and mutators correspond to the ELF32_R_SYM, ELF32_R_TYPE,
  // and ELF32_R_INFO macros defined in the ELF specification:
  Elf32_Word getSymbol() const { return (r_info >> 8); }
  unsigned char getType() const { return (unsigned char)(r_info & 0x0ff); }
  void setSymbol(Elf32_Word s) { setSymbolAndType(s, getType()); }
  void setType(unsigned char t) { setSymbolAndType(getSymbol(), t); }
  void setSymbolAndType(Elf32_Word s, unsigned char t) {
    r_info = (s << 8) + t;
  }
};

// Relocation entry with explicit addend.
struct Elf32_Rela {
  Elf32_Addr r_offset;  // Location (file byte offset, or program virtual addr)
  Elf32_Word r_info;    // Symbol table index and type of relocation to apply
  Elf32_Sword r_addend; // Compute value for relocatable field by adding this

  // These accessors and mutators correspond to the ELF32_R_SYM, ELF32_R_TYPE,
  // and ELF32_R_INFO macros defined in the ELF specification:
  Elf32_Word getSymbol() const { return (r_info >> 8); }
  unsigned char getType() const { return (unsigned char)(r_info & 0x0ff); }
  void setSymbol(Elf32_Word s) { setSymbolAndType(s, getType()); }
  void setType(unsigned char t) { setSymbolAndType(getSymbol(), t); }
  void setSymbolAndType(Elf32_Word s, unsigned char t) {
    r_info = (s << 8) + t;
  }
};

// Relocation entry without explicit addend or info (relative relocations only).
typedef Elf32_Word Elf32_Relr; // offset/bitmap for relative relocations

// Relocation entry, without explicit addend.
struct Elf64_Rel {
  Elf64_Addr r_offset; // Location (file byte offset, or program virtual addr).
  Elf64_Xword r_info;  // Symbol table index and type of relocation to apply.

  // These accessors and mutators correspond to the ELF64_R_SYM, ELF64_R_TYPE,
  // and ELF64_R_INFO macros defined in the ELF specification:
  Elf64_Word getSymbol() const { return (r_info >> 32); }
  Elf64_Word getType() const { return (Elf64_Word)(r_info & 0xffffffffL); }
  void setSymbol(Elf64_Word s) { setSymbolAndType(s, getType()); }
  void setType(Elf64_Word t) { setSymbolAndType(getSymbol(), t); }
  void setSymbolAndType(Elf64_Word s, Elf64_Word t) {
    r_info = ((Elf64_Xword)s << 32) + (t & 0xffffffffL);
  }
};

// Relocation entry with explicit addend.
struct Elf64_Rela {
  Elf64_Addr r_offset; // Location (file byte offset, or program virtual addr).
  Elf64_Xword r_info;  // Symbol table index and type of relocation to apply.
  Elf64_Sxword r_addend; // Compute value for relocatable field by adding this.

  // These accessors and mutators correspond to the ELF64_R_SYM, ELF64_R_TYPE,
  // and ELF64_R_INFO macros defined in the ELF specification:
  Elf64_Word getSymbol() const { return (r_info >> 32); }
  Elf64_Word getType() const { return (Elf64_Word)(r_info & 0xffffffffL); }
  void setSymbol(Elf64_Word s) { setSymbolAndType(s, getType()); }
  void setType(Elf64_Word t) { setSymbolAndType(getSymbol(), t); }
  void setSymbolAndType(Elf64_Word s, Elf64_Word t) {
    r_info = ((Elf64_Xword)s << 32) + (t & 0xffffffffL);
  }
};

// In-memory representation of CREL. The serialized representation uses LEB128.
template <bool Is64> struct Elf_Crel {
  std::conditional_t<Is64, uint64_t, uint32_t> r_offset;
  uint32_t r_symidx;
  uint32_t r_type;
  std::conditional_t<Is64, int64_t, int32_t> r_addend;
};

// Relocation entry without explicit addend or info (relative relocations only).
typedef Elf64_Xword Elf64_Relr; // offset/bitmap for relative relocations

// Program header for ELF32.
struct Elf32_Phdr {
  Elf32_Word p_type;   // Type of segment
  Elf32_Off p_offset;  // File offset where segment is located, in bytes
  Elf32_Addr p_vaddr;  // Virtual address of beginning of segment
  Elf32_Addr p_paddr;  // Physical address of beginning of segment (OS-specific)
  Elf32_Word p_filesz; // Num. of bytes in file image of segment (may be zero)
  Elf32_Word p_memsz;  // Num. of bytes in mem image of segment (may be zero)
  Elf32_Word p_flags;  // Segment flags
  Elf32_Word p_align;  // Segment alignment constraint
};

// Program header for ELF64.
struct Elf64_Phdr {
  Elf64_Word p_type;    // Type of segment
  Elf64_Word p_flags;   // Segment flags
  Elf64_Off p_offset;   // File offset where segment is located, in bytes
  Elf64_Addr p_vaddr;   // Virtual address of beginning of segment
  Elf64_Addr p_paddr;   // Physical addr of beginning of segment (OS-specific)
  Elf64_Xword p_filesz; // Num. of bytes in file image of segment (may be zero)
  Elf64_Xword p_memsz;  // Num. of bytes in mem image of segment (may be zero)
  Elf64_Xword p_align;  // Segment alignment constraint
};

// Segment types.
enum {
  PT_NULL = 0,            // Unused segment.
  PT_LOAD = 1,            // Loadable segment.
  PT_DYNAMIC = 2,         // Dynamic linking information.
  PT_INTERP = 3,          // Interpreter pathname.
  PT_NOTE = 4,            // Auxiliary information.
  PT_SHLIB = 5,           // Reserved.
  PT_PHDR = 6,            // The program header table itself.
  PT_TLS = 7,             // The thread-local storage template.
  PT_LOOS = 0x60000000,   // Lowest operating system-specific pt entry type.
  PT_HIOS = 0x6fffffff,   // Highest operating system-specific pt entry type.
  PT_LOPROC = 0x70000000, // Lowest processor-specific program hdr entry type.
  PT_HIPROC = 0x7fffffff, // Highest processor-specific program hdr entry type.

  // x86-64 program header types.
  // These all contain stack unwind tables.
  PT_GNU_EH_FRAME = 0x6474e550,
  PT_SUNW_EH_FRAME = 0x6474e550,
  PT_SUNW_UNWIND = 0x6464e550,

  PT_GNU_STACK = 0x6474e551,    // Indicates stack executability.
  PT_GNU_RELRO = 0x6474e552,    // Read-only after relocation.
  PT_GNU_PROPERTY = 0x6474e553, // .note.gnu.property notes sections.

  PT_OPENBSD_MUTABLE = 0x65a3dbe5,   // Like bss, but not immutable.
  PT_OPENBSD_RANDOMIZE = 0x65a3dbe6, // Fill with random data.
  PT_OPENBSD_WXNEEDED = 0x65a3dbe7,  // Program does W^X violations.
  PT_OPENBSD_NOBTCFI = 0x65a3dbe8,   // Do not enforce branch target CFI.
  PT_OPENBSD_SYSCALLS = 0x65a3dbe9,  // System call sites.
  PT_OPENBSD_BOOTDATA = 0x65a41be6,  // Section for boot arguments.

  // ARM program header types.
  PT_ARM_ARCHEXT = 0x70000000, // Platform architecture compatibility info
  // These all contain stack unwind tables.
  PT_ARM_EXIDX = 0x70000001,
  PT_ARM_UNWIND = 0x70000001,
  // MTE memory tag segment type
  PT_AARCH64_MEMTAG_MTE = 0x70000002,

  // MIPS program header types.
  PT_MIPS_REGINFO = 0x70000000,  // Register usage information.
  PT_MIPS_RTPROC = 0x70000001,   // Runtime procedure table.
  PT_MIPS_OPTIONS = 0x70000002,  // Options segment.
  PT_MIPS_ABIFLAGS = 0x70000003, // Abiflags segment.

  // RISCV program header types.
  PT_RISCV_ATTRIBUTES = 0x70000003,
};

// Segment flag bits.
enum : unsigned {
  PF_X = 1,                // Execute
  PF_W = 2,                // Write
  PF_R = 4,                // Read
  PF_MASKOS = 0x0ff00000,  // Bits for operating system-specific semantics.
  PF_MASKPROC = 0xf0000000 // Bits for processor-specific semantics.
};

// Dynamic table entry for ELF32.
struct Elf32_Dyn {
  Elf32_Sword d_tag; // Type of dynamic table entry.
  union {
    Elf32_Word d_val; // Integer value of entry.
    Elf32_Addr d_ptr; // Pointer value of entry.
  } d_un;
};

// Dynamic table entry for ELF64.
struct Elf64_Dyn {
  Elf64_Sxword d_tag; // Type of dynamic table entry.
  union {
    Elf64_Xword d_val; // Integer value of entry.
    Elf64_Addr d_ptr;  // Pointer value of entry.
  } d_un;
};

// Dynamic table entry tags.
enum {
#define DYNAMIC_TAG(name, value) DT_##name = value,
#include "DynamicTags.def"
#undef DYNAMIC_TAG
};

// DT_FLAGS values.
enum {
  DF_ORIGIN = 0x01,    // The object may reference $ORIGIN.
  DF_SYMBOLIC = 0x02,  // Search the shared lib before searching the exe.
  DF_TEXTREL = 0x04,   // Relocations may modify a non-writable segment.
  DF_BIND_NOW = 0x08,  // Process all relocations on load.
  DF_STATIC_TLS = 0x10 // Reject attempts to load dynamically.
};

// State flags selectable in the `d_un.d_val' element of the DT_FLAGS_1 entry.
enum {
  DF_1_NOW = 0x00000001,       // Set RTLD_NOW for this object.
  DF_1_GLOBAL = 0x00000002,    // Set RTLD_GLOBAL for this object.
  DF_1_GROUP = 0x00000004,     // Set RTLD_GROUP for this object.
  DF_1_NODELETE = 0x00000008,  // Set RTLD_NODELETE for this object.
  DF_1_LOADFLTR = 0x00000010,  // Trigger filtee loading at runtime.
  DF_1_INITFIRST = 0x00000020, // Set RTLD_INITFIRST for this object.
  DF_1_NOOPEN = 0x00000040,    // Set RTLD_NOOPEN for this object.
  DF_1_ORIGIN = 0x00000080,    // $ORIGIN must be handled.
  DF_1_DIRECT = 0x00000100,    // Direct binding enabled.
  DF_1_TRANS = 0x00000200,
  DF_1_INTERPOSE = 0x00000400,  // Object is used to interpose.
  DF_1_NODEFLIB = 0x00000800,   // Ignore default lib search path.
  DF_1_NODUMP = 0x00001000,     // Object can't be dldump'ed.
  DF_1_CONFALT = 0x00002000,    // Configuration alternative created.
  DF_1_ENDFILTEE = 0x00004000,  // Filtee terminates filters search.
  DF_1_DISPRELDNE = 0x00008000, // Disp reloc applied at build time.
  DF_1_DISPRELPND = 0x00010000, // Disp reloc applied at run-time.
  DF_1_NODIRECT = 0x00020000,   // Object has no-direct binding.
  DF_1_IGNMULDEF = 0x00040000,
  DF_1_NOKSYMS = 0x00080000,
  DF_1_NOHDR = 0x00100000,
  DF_1_EDITED = 0x00200000, // Object is modified after built.
  DF_1_NORELOC = 0x00400000,
  DF_1_SYMINTPOSE = 0x00800000, // Object has individual interposers.
  DF_1_GLOBAUDIT = 0x01000000,  // Global auditing required.
  DF_1_SINGLETON = 0x02000000,  // Singleton symbols are used.
  DF_1_PIE = 0x08000000,        // Object is a position-independent executable.
};

// DT_MIPS_FLAGS values.
enum {
  RHF_NONE = 0x00000000,                   // No flags.
  RHF_QUICKSTART = 0x00000001,             // Uses shortcut pointers.
  RHF_NOTPOT = 0x00000002,                 // Hash size is not a power of two.
  RHS_NO_LIBRARY_REPLACEMENT = 0x00000004, // Ignore LD_LIBRARY_PATH.
  RHF_NO_MOVE = 0x00000008,                // DSO address may not be relocated.
  RHF_SGI_ONLY = 0x00000010,               // SGI specific features.
  RHF_GUARANTEE_INIT = 0x00000020,         // Guarantee that .init will finish
                                           // executing before any non-init
                                           // code in DSO is called.
  RHF_DELTA_C_PLUS_PLUS = 0x00000040,      // Contains Delta C++ code.
  RHF_GUARANTEE_START_INIT = 0x00000080,   // Guarantee that .init will start
                                           // executing before any non-init
                                           // code in DSO is called.
  RHF_PIXIE = 0x00000100,                  // Generated by pixie.
  RHF_DEFAULT_DELAY_LOAD = 0x00000200,     // Delay-load DSO by default.
  RHF_REQUICKSTART = 0x00000400,           // Object may be requickstarted
  RHF_REQUICKSTARTED = 0x00000800,         // Object has been requickstarted
  RHF_CORD = 0x00001000,                   // Generated by cord.
  RHF_NO_UNRES_UNDEF = 0x00002000,         // Object contains no unresolved
                                           // undef symbols.
  RHF_RLD_ORDER_SAFE = 0x00004000          // Symbol table is in a safe order.
};

// ElfXX_VerDef structure version (GNU versioning)
enum { VER_DEF_NONE = 0, VER_DEF_CURRENT = 1 };

// VerDef Flags (ElfXX_VerDef::vd_flags)
enum { VER_FLG_BASE = 0x1, VER_FLG_WEAK = 0x2, VER_FLG_INFO = 0x4 };

// Special constants for the version table. (SHT_GNU_versym/.gnu.version)
enum {
  VER_NDX_LOCAL = 0,       // Unversioned local symbol
  VER_NDX_GLOBAL = 1,      // Unversioned global symbol
  VERSYM_VERSION = 0x7fff, // Version Index mask
  VERSYM_HIDDEN = 0x8000   // Hidden bit (non-default version)
};

// ElfXX_VerNeed structure version (GNU versioning)
enum { VER_NEED_NONE = 0, VER_NEED_CURRENT = 1 };

// SHT_NOTE section types.

// Generic note types.
enum : unsigned {
  NT_VERSION = 1,
  NT_ARCH = 2,
  NT_GNU_BUILD_ATTRIBUTE_OPEN = 0x100,
  NT_GNU_BUILD_ATTRIBUTE_FUNC = 0x101,
};

// Core note types.
enum : unsigned {
  NT_PRSTATUS = 1,
  NT_FPREGSET = 2,
  NT_PRPSINFO = 3,
  NT_TASKSTRUCT = 4,
  NT_AUXV = 6,
  NT_PSTATUS = 10,
  NT_FPREGS = 12,
  NT_PSINFO = 13,
  NT_LWPSTATUS = 16,
  NT_LWPSINFO = 17,
  NT_WIN32PSTATUS = 18,

  NT_PPC_VMX = 0x100,
  NT_PPC_VSX = 0x102,
  NT_PPC_TAR = 0x103,
  NT_PPC_PPR = 0x104,
  NT_PPC_DSCR = 0x105,
  NT_PPC_EBB = 0x106,
  NT_PPC_PMU = 0x107,
  NT_PPC_TM_CGPR = 0x108,
  NT_PPC_TM_CFPR = 0x109,
  NT_PPC_TM_CVMX = 0x10a,
  NT_PPC_TM_CVSX = 0x10b,
  NT_PPC_TM_SPR = 0x10c,
  NT_PPC_TM_CTAR = 0x10d,
  NT_PPC_TM_CPPR = 0x10e,
  NT_PPC_TM_CDSCR = 0x10f,

  NT_386_TLS = 0x200,
  NT_386_IOPERM = 0x201,
  NT_X86_XSTATE = 0x202,

  NT_S390_HIGH_GPRS = 0x300,
  NT_S390_TIMER = 0x301,
  NT_S390_TODCMP = 0x302,
  NT_S390_TODPREG = 0x303,
  NT_S390_CTRS = 0x304,
  NT_S390_PREFIX = 0x305,
  NT_S390_LAST_BREAK = 0x306,
  NT_S390_SYSTEM_CALL = 0x307,
  NT_S390_TDB = 0x308,
  NT_S390_VXRS_LOW = 0x309,
  NT_S390_VXRS_HIGH = 0x30a,
  NT_S390_GS_CB = 0x30b,
  NT_S390_GS_BC = 0x30c,

  NT_ARM_VFP = 0x400,
  NT_ARM_TLS = 0x401,
  NT_ARM_HW_BREAK = 0x402,
  NT_ARM_HW_WATCH = 0x403,
  NT_ARM_SVE = 0x405,
  NT_ARM_PAC_MASK = 0x406,
  NT_ARM_TAGGED_ADDR_CTRL = 0x409,
  NT_ARM_SSVE = 0x40b,
  NT_ARM_ZA = 0x40c,
  NT_ARM_ZT = 0x40d,

  NT_FILE = 0x46494c45,
  NT_PRXFPREG = 0x46e62b7f,
  NT_SIGINFO = 0x53494749,
};

// LLVM-specific notes.
enum {
  NT_LLVM_HWASAN_GLOBALS = 3,
};

// GNU note types.
enum {
  NT_GNU_ABI_TAG = 1,
  NT_GNU_HWCAP = 2,
  NT_GNU_BUILD_ID = 3,
  NT_GNU_GOLD_VERSION = 4,
  NT_GNU_PROPERTY_TYPE_0 = 5,
  FDO_PACKAGING_METADATA = 0xcafe1a7e,
};

// Android note types.
enum {
  NT_ANDROID_TYPE_IDENT = 1,
  NT_ANDROID_TYPE_KUSER = 3,
  NT_ANDROID_TYPE_MEMTAG = 4,
};

// Memory tagging values used in NT_ANDROID_TYPE_MEMTAG notes.
enum {
  // Enumeration to determine the tagging mode. In Android-land, 'SYNC' means
  // running all threads in MTE Synchronous mode, and 'ASYNC' means to use the
  // kernels auto-upgrade feature to allow for either MTE Asynchronous,
  // Asymmetric, or Synchronous mode. This allows silicon vendors to specify, on
  // a per-cpu basis what 'ASYNC' should mean. Generally, the expectation is
  // "pick the most precise mode that's very fast".
  NT_MEMTAG_LEVEL_NONE = 0,
  NT_MEMTAG_LEVEL_ASYNC = 1,
  NT_MEMTAG_LEVEL_SYNC = 2,
  NT_MEMTAG_LEVEL_MASK = 3,
  // Bits indicating whether the loader should prepare for MTE to be enabled on
  // the heap and/or stack.
  NT_MEMTAG_HEAP = 4,
  NT_MEMTAG_STACK = 8,
};

// Property types used in GNU_PROPERTY_TYPE_0 notes.
enum : unsigned {
  GNU_PROPERTY_STACK_SIZE = 1,
  GNU_PROPERTY_NO_COPY_ON_PROTECTED = 2,
  GNU_PROPERTY_AARCH64_FEATURE_1_AND = 0xc0000000,
  GNU_PROPERTY_AARCH64_FEATURE_PAUTH = 0xc0000001,
  GNU_PROPERTY_X86_FEATURE_1_AND = 0xc0000002,

  GNU_PROPERTY_X86_UINT32_OR_LO = 0xc0008000,
  GNU_PROPERTY_X86_FEATURE_2_NEEDED = GNU_PROPERTY_X86_UINT32_OR_LO + 1,
  GNU_PROPERTY_X86_ISA_1_NEEDED = GNU_PROPERTY_X86_UINT32_OR_LO + 2,

  GNU_PROPERTY_X86_UINT32_OR_AND_LO = 0xc0010000,
  GNU_PROPERTY_X86_FEATURE_2_USED = GNU_PROPERTY_X86_UINT32_OR_AND_LO + 1,
  GNU_PROPERTY_X86_ISA_1_USED = GNU_PROPERTY_X86_UINT32_OR_AND_LO + 2,
};

// aarch64 processor feature bits.
enum : unsigned {
  GNU_PROPERTY_AARCH64_FEATURE_1_BTI = 1 << 0,
  GNU_PROPERTY_AARCH64_FEATURE_1_PAC = 1 << 1,
  GNU_PROPERTY_AARCH64_FEATURE_1_GCS = 1 << 2,
};

// aarch64 PAuth platforms.
enum : unsigned {
  AARCH64_PAUTH_PLATFORM_INVALID = 0x0,
  AARCH64_PAUTH_PLATFORM_BAREMETAL = 0x1,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX = 0x10000002,
};

// Bit positions of version flags for AARCH64_PAUTH_PLATFORM_LLVM_LINUX.
enum : unsigned {
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INTRINSICS = 0,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_CALLS = 1,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_RETURNS = 2,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_AUTHTRAPS = 3,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRADDRDISCR = 4,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRTYPEDISCR = 5,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINI = 6,
  AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_LAST =
      AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINI,
};

// x86 processor feature bits.
enum : unsigned {
  GNU_PROPERTY_X86_FEATURE_1_IBT = 1 << 0,
  GNU_PROPERTY_X86_FEATURE_1_SHSTK = 1 << 1,

  GNU_PROPERTY_X86_FEATURE_2_X86 = 1 << 0,
  GNU_PROPERTY_X86_FEATURE_2_X87 = 1 << 1,
  GNU_PROPERTY_X86_FEATURE_2_MMX = 1 << 2,
  GNU_PROPERTY_X86_FEATURE_2_XMM = 1 << 3,
  GNU_PROPERTY_X86_FEATURE_2_YMM = 1 << 4,
  GNU_PROPERTY_X86_FEATURE_2_ZMM = 1 << 5,
  GNU_PROPERTY_X86_FEATURE_2_FXSR = 1 << 6,
  GNU_PROPERTY_X86_FEATURE_2_XSAVE = 1 << 7,
  GNU_PROPERTY_X86_FEATURE_2_XSAVEOPT = 1 << 8,
  GNU_PROPERTY_X86_FEATURE_2_XSAVEC = 1 << 9,

  GNU_PROPERTY_X86_ISA_1_BASELINE = 1 << 0,
  GNU_PROPERTY_X86_ISA_1_V2 = 1 << 1,
  GNU_PROPERTY_X86_ISA_1_V3 = 1 << 2,
  GNU_PROPERTY_X86_ISA_1_V4 = 1 << 3,
};

// FreeBSD note types.
enum {
  NT_FREEBSD_ABI_TAG = 1,
  NT_FREEBSD_NOINIT_TAG = 2,
  NT_FREEBSD_ARCH_TAG = 3,
  NT_FREEBSD_FEATURE_CTL = 4,
};

// NT_FREEBSD_FEATURE_CTL values (see FreeBSD's sys/sys/elf_common.h).
enum {
  NT_FREEBSD_FCTL_ASLR_DISABLE = 0x00000001,
  NT_FREEBSD_FCTL_PROTMAX_DISABLE = 0x00000002,
  NT_FREEBSD_FCTL_STKGAP_DISABLE = 0x00000004,
  NT_FREEBSD_FCTL_WXNEEDED = 0x00000008,
  NT_FREEBSD_FCTL_LA48 = 0x00000010,
  NT_FREEBSD_FCTL_ASG_DISABLE = 0x00000020,
};

// FreeBSD core note types.
enum {
  NT_FREEBSD_THRMISC = 7,
  NT_FREEBSD_PROCSTAT_PROC = 8,
  NT_FREEBSD_PROCSTAT_FILES = 9,
  NT_FREEBSD_PROCSTAT_VMMAP = 10,
  NT_FREEBSD_PROCSTAT_GROUPS = 11,
  NT_FREEBSD_PROCSTAT_UMASK = 12,
  NT_FREEBSD_PROCSTAT_RLIMIT = 13,
  NT_FREEBSD_PROCSTAT_OSREL = 14,
  NT_FREEBSD_PROCSTAT_PSSTRINGS = 15,
  NT_FREEBSD_PROCSTAT_AUXV = 16,
};

// NetBSD core note types.
enum {
  NT_NETBSDCORE_PROCINFO = 1,
  NT_NETBSDCORE_AUXV = 2,
  NT_NETBSDCORE_LWPSTATUS = 24,
};

// OpenBSD core note types.
enum {
  NT_OPENBSD_PROCINFO = 10,
  NT_OPENBSD_AUXV = 11,
  NT_OPENBSD_REGS = 20,
  NT_OPENBSD_FPREGS = 21,
  NT_OPENBSD_XFPREGS = 22,
  NT_OPENBSD_WCOOKIE = 23,
};

// AMDGPU-specific section indices.
enum {
  SHN_AMDGPU_LDS = 0xff00, // Variable in LDS; symbol encoded like SHN_COMMON
};

// AMD vendor specific notes. (Code Object V2)
enum {
  NT_AMD_HSA_CODE_OBJECT_VERSION = 1,
  NT_AMD_HSA_HSAIL = 2,
  NT_AMD_HSA_ISA_VERSION = 3,
  // Note types with values between 4 and 9 (inclusive) are reserved.
  NT_AMD_HSA_METADATA = 10,
  NT_AMD_HSA_ISA_NAME = 11,
  NT_AMD_PAL_METADATA = 12
};

// AMDGPU vendor specific notes. (Code Object V3)
enum {
  // Note types with values between 0 and 31 (inclusive) are reserved.
  NT_AMDGPU_METADATA = 32
};

// LLVMOMPOFFLOAD specific notes.
enum : unsigned {
  NT_LLVM_OPENMP_OFFLOAD_VERSION = 1,
  NT_LLVM_OPENMP_OFFLOAD_PRODUCER = 2,
  NT_LLVM_OPENMP_OFFLOAD_PRODUCER_VERSION = 3
};

enum {
  GNU_ABI_TAG_LINUX = 0,
  GNU_ABI_TAG_HURD = 1,
  GNU_ABI_TAG_SOLARIS = 2,
  GNU_ABI_TAG_FREEBSD = 3,
  GNU_ABI_TAG_NETBSD = 4,
  GNU_ABI_TAG_SYLLABLE = 5,
  GNU_ABI_TAG_NACL = 6,
};

constexpr const char *ELF_NOTE_GNU = "GNU";

// Android packed relocation group flags.
enum {
  RELOCATION_GROUPED_BY_INFO_FLAG = 1,
  RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG = 2,
  RELOCATION_GROUPED_BY_ADDEND_FLAG = 4,
  RELOCATION_GROUP_HAS_ADDEND_FLAG = 8,
};

// Compressed section header for ELF32.
struct Elf32_Chdr {
  Elf32_Word ch_type;
  Elf32_Word ch_size;
  Elf32_Word ch_addralign;
};

// Compressed section header for ELF64.
struct Elf64_Chdr {
  Elf64_Word ch_type;
  Elf64_Word ch_reserved;
  Elf64_Xword ch_size;
  Elf64_Xword ch_addralign;
};

// Note header for ELF32.
struct Elf32_Nhdr {
  Elf32_Word n_namesz;
  Elf32_Word n_descsz;
  Elf32_Word n_type;
};

// Note header for ELF64.
struct Elf64_Nhdr {
  Elf64_Word n_namesz;
  Elf64_Word n_descsz;
  Elf64_Word n_type;
};

// Legal values for ch_type field of compressed section header.
enum {
  ELFCOMPRESS_ZLIB = 1,            // ZLIB/DEFLATE algorithm.
  ELFCOMPRESS_ZSTD = 2,            // Zstandard algorithm
  ELFCOMPRESS_LOOS = 0x60000000,   // Start of OS-specific.
  ELFCOMPRESS_HIOS = 0x6fffffff,   // End of OS-specific.
  ELFCOMPRESS_LOPROC = 0x70000000, // Start of processor-specific.
  ELFCOMPRESS_HIPROC = 0x7fffffff  // End of processor-specific.
};

constexpr unsigned CREL_HDR_ADDEND = 4;

/// Convert an architecture name into ELF's e_machine value.
uint16_t convertArchNameToEMachine(StringRef Arch);

/// Convert an ELF's e_machine value into an architecture name.
StringRef convertEMachineToArchName(uint16_t EMachine);

// Convert a lowercase string identifier into an OSABI value.
uint8_t convertNameToOSABI(StringRef Name);

// Convert an OSABI value into a string that identifies the OS- or ABI-
// specific ELF extension.
StringRef convertOSABIToName(uint8_t OSABI);

} // end namespace ELF
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_ELF_H
