/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_DWARF_REGS_H_
#define _PERF_DWARF_REGS_H_
#include "annotate.h"
#include <elf.h>

#ifndef EM_AARCH64
#define EM_AARCH64	183  /* ARM 64 bit */
#endif

#ifndef EM_CSKY
#define EM_CSKY		252  /* C-SKY */
#endif
#ifndef EF_CSKY_ABIV1
#define EF_CSKY_ABIV1	0X10000000
#endif
#ifndef EF_CSKY_ABIV2
#define EF_CSKY_ABIV2	0X20000000
#endif

#ifndef EM_LOONGARCH
#define EM_LOONGARCH	258 /* LoongArch */
#endif

/* EM_HOST gives the ELF machine for host, EF_HOST gives additional flags. */
#if defined(__x86_64__)
  #define EM_HOST EM_X86_64
#elif defined(__i386__)
  #define EM_HOST EM_386
#elif defined(__aarch64__)
  #define EM_HOST EM_AARCH64
#elif defined(__arm__)
  #define EM_HOST EM_ARM
#elif defined(__alpha__)
  #define EM_HOST EM_ALPHA
#elif defined(__arc__)
  #define EM_HOST EM_ARC
#elif defined(__AVR__)
  #define EM_HOST EM_AVR
#elif defined(__AVR32__)
  #define EM_HOST EM_AVR32
#elif defined(__bfin__)
  #define EM_HOST EM_BLACKFIN
#elif defined(__csky__)
  #define EM_HOST EM_CSKY
  #if defined(__CSKYABIV2__)
    #define EF_HOST EF_CSKY_ABIV2
  #else
    #define EF_HOST EF_CSKY_ABIV1
  #endif
#elif defined(__cris__)
  #define EM_HOST EM_CRIS
#elif defined(__hppa__) // HP PA-RISC
  #define EM_HOST EM_PARISC
#elif defined(__loongarch__)
  #define EM_HOST EM_LOONGARCH
#elif defined(__mips__)
  #define EM_HOST EM_MIPS
#elif defined(__m32r__)
  #define EM_HOST EM_M32R
#elif defined(__microblaze__)
  #define EM_HOST EM_MICROBLAZE
#elif defined(__MSP430__)
  #define EM_HOST EM_MSP430
#elif defined(__powerpc64__)
  #define EM_HOST EM_PPC64
#elif defined(__powerpc__)
  #define EM_HOST EM_PPC
#elif defined(__riscv)
  #define EM_HOST EM_RISCV
#elif defined(__s390x__)
  #define EM_HOST EM_S390
#elif defined(__sh__)
  #define EM_HOST EM_SH
#elif defined(__sparc64__) || defined(__sparc__)
  #define EM_HOST EM_SPARC
#elif defined(__xtensa__)
  #define EM_HOST EM_XTENSA
#else
  /* Unknown host ELF machine type. */
  #define EM_HOST EM_NONE
#endif

#if !defined(EF_HOST)
  #define EF_HOST 0
#endif

#define DWARF_REG_PC  0xd3af9c /* random number */
#define DWARF_REG_FB  0xd3affb /* random number */

#ifdef HAVE_LIBDW_SUPPORT
const char *get_csky_regstr(unsigned int n, unsigned int flags);

/**
 * get_dwarf_regstr() - Returns ftrace register string from DWARF regnum.
 * @n: DWARF register number.
 * @machine: ELF machine signature (EM_*).
 * @flags: ELF flags for things like ABI differences.
 */
const char *get_dwarf_regstr(unsigned int n, unsigned int machine, unsigned int flags);

int get_x86_regnum(const char *name);

#if !defined(__x86_64__) && !defined(__i386__)
int get_arch_regnum(const char *name);
#endif

/*
 * get_dwarf_regnum - Returns DWARF regnum from register name
 * name: architecture register name
 * machine: ELF machine signature (EM_*)
 */
int get_dwarf_regnum(const char *name, unsigned int machine, unsigned int flags);

void get_powerpc_regs(u32 raw_insn, int is_source, struct annotated_op_loc *op_loc);

#else /* HAVE_LIBDW_SUPPORT */

static inline int get_dwarf_regnum(const char *name __maybe_unused,
				   unsigned int machine __maybe_unused,
				   unsigned int flags __maybe_unused)
{
	return -1;
}

static inline void get_powerpc_regs(u32 raw_insn __maybe_unused, int is_source __maybe_unused,
		struct annotated_op_loc *op_loc __maybe_unused)
{
	return;
}
#endif

#endif
