/*	$OpenBSD: exec.c,v 1.1 2021/04/28 19:01:00 drahn Exp $	*/

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

#include "efiboot.h"
#include "libsa.h"
#include "fdt.h"

typedef void (*startfuncp)(void *, void *, void *) __attribute__ ((noreturn));

unsigned int cpu_get_dcache_line_size(void);
void cpu_flush_dcache(vaddr_t, vsize_t);
void cpu_inval_icache(void);

void
cpu_flush_dcache(vaddr_t addr, vsize_t len)
{
	__asm volatile("fence" ::: "memory");
}

void
cpu_inval_icache(void)
{
	__asm volatile("fence.i" ::: "memory");
}

void
run_loadfile(uint64_t *marks, int howto)
{
	char args[256];
	char *cp;
	void *fdt;

	strlcpy(args, cmd.path, sizeof(args));
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

	cpu_flush_dcache(marks[MARK_ENTRY], marks[MARK_END] - marks[MARK_ENTRY]);
	cpu_inval_icache();

	cpu_flush_dcache((vaddr_t)fdt, fdt_get_size(fdt));

	(*(startfuncp)(marks[MARK_ENTRY]))((void *)marks[MARK_END], 0, fdt);

	/* NOTREACHED */
}
