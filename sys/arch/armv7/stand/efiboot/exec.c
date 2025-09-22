/*	$OpenBSD: exec.c,v 1.18 2024/03/10 15:37:54 kettenis Exp $	*/

/*
 * Copyright (c) 2006, 2016 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/reboot.h>
#include <dev/cons.h>

#include <lib/libkern/libkern.h>
#include <lib/libsa/loadfile.h>
#include <sys/exec_elf.h>

#include <efi.h>
#include <stand/boot/cmd.h>

#include <arm/armreg.h>

#include "efiboot.h"
#include "libsa.h"

typedef void (*startfuncp)(void *, void *, void *) __attribute__ ((noreturn));

#define CLIDR_LOC(x)		((x >> 24) & 0x7)
#define CLIDR_CTYPE(x, n)	((x >> (n * 3)) & 0x7)
#define CLIDR_CTYPE_NOCACHE	0x0
#define CLIDR_CTYPE_ICACHE	0x1
#define CLIDR_CTYPE_DCACHE	0x2
#define CLIDR_CTYPE_SEP_CACHE	0x3
#define CLIDR_CTYPE_UNI_CACHE	0x4
#define CCSIDR_NUMSETS(x)	((x >> 13) & 0x7fff)
#define CCSIDR_ASSOCIATIVITY(x)	((x >> 3) & 0x3ff)
#define CCSIDR_LINESZ(x)	(x & 0x7)

void
dcache_wbinv_all(void)
{
	uint32_t clidr;
	uint32_t ccsidr;
	uint32_t val;
	int nways, nsets;
	int wshift, sshift;
	int way, set;
	int level;
	
	__asm volatile("mrc p15, 1, %0, c0, c0, 1" : "=r"(clidr));
	for (level = 0; level < CLIDR_LOC(clidr); level++) {
		if (CLIDR_CTYPE(clidr, level) == CLIDR_CTYPE_NOCACHE)
			break;
		if (CLIDR_CTYPE(clidr, level) == CLIDR_CTYPE_ICACHE)
			continue;

		__asm volatile("mcr p15, 2, %0, c0, c0, 0" :: "r"(level << 1));
		__asm volatile("isb");
		__asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));

		nways = CCSIDR_ASSOCIATIVITY(ccsidr) + 1;
		nsets = CCSIDR_NUMSETS(ccsidr) + 1;

		sshift = CCSIDR_LINESZ(ccsidr) + 4;
		wshift = __builtin_clz(CCSIDR_ASSOCIATIVITY(ccsidr));

		for (way = 0; way < nways; way++) {
			for (set = 0; set < nsets; set++) {
				val = (way << wshift) | (set << sshift) |
				    (level << 1);
				__asm volatile("mcr p15, 0, %0, c7, c14, 2"
				    :: "r"(val));
			}
		}
	}

	__asm volatile("dsb");
}

void
icache_inv_all(void)
{
	__asm volatile("mcr p15, 0, r0, c7, c5, 0"); /* ICIALLU */
	__asm volatile("dsb");
	__asm volatile("isb");
}

void
dcache_disable(void)
{
	uint32_t sctlr;

	__asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
	sctlr &= ~CPU_CONTROL_DC_ENABLE;
	__asm volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
	__asm volatile("dsb");
	__asm volatile("isb");
}

void
icache_disable(void)
{
	uint32_t sctlr;

	__asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
	sctlr &= ~CPU_CONTROL_IC_ENABLE;
	__asm volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
	__asm volatile("dsb");
	__asm volatile("isb");
}

void
mmu_disable(void)
{
	uint32_t sctlr;

	__asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
	sctlr &= ~CPU_CONTROL_MMU_ENABLE;
	__asm volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));

	__asm volatile("mcr p15, 0, r0, c8, c7, 0"); /* TLBIALL */
	__asm volatile("mcr p15, 0, r0, c7, c5, 6"); /* BPIALL */
	__asm volatile("dsb");
	__asm volatile("isb");
}

void
run_loadfile(uint64_t *marks, int howto)
{
	Elf_Ehdr *elf = (Elf_Ehdr *)marks[MARK_SYM];
	Elf_Shdr *shp = (Elf_Shdr *)(marks[MARK_SYM] + elf->e_shoff);
	u_long esym = marks[MARK_END] & 0x0fffffff;
	u_long offset = 0;
	char args[256];
	char *cp;
	void *fdt;
	int i;

	/*
	 * Tell locore.S where the symbol table ends by setting
	 * 'esym', which should be the first word in the .data
	 * section.
	 */
	for (i = 0; i < elf->e_shnum; i++) {
		/* XXX Assume .data is the first writable segment. */
		if (shp[i].sh_flags & SHF_WRITE) {
			/* XXX We have to store the virtual address. */
			esym |= shp[i].sh_addr & 0xf0000000;
			*(u_long *)(LOADADDR(shp[i].sh_addr)) = esym;
			break;
		}
	}

	snprintf(args, sizeof(args) - 8, "%s:%s", cmd.bootdev, cmd.image);
	cp = args + strlen(args);

	*cp++ = ' ';
	*cp = '-';
        if (howto & RB_ASKNAME)
                *++cp = 'a';
        if (howto & RB_CONFIG)
                *++cp = 'c';
        if (howto & RB_SINGLE)
                *++cp = 's';
        if (howto & RB_KDB)
                *++cp = 'd';
        if (*cp == '-')
		*--cp = 0;
	else
		*++cp = 0;

	fdt = efi_makebootargs(args, howto);

	efi_cleanup();

	dcache_wbinv_all();
	dcache_disable();
	icache_inv_all();
	icache_disable();
	mmu_disable();

	(*(startfuncp)(marks[MARK_ENTRY]))((void *)esym, NULL, fdt);

	/* NOTREACHED */
}
