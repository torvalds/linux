/*	$OpenBSD: netboot.c,v 1.9 2019/04/10 04:17:33 deraadt Exp $	*/
/*	$NetBSD: netboot.c,v 1.1 1996/09/18 20:03:12 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)boot.c	8.1 (Berkeley) 6/10/93
 */

#include <lib/libkern/libkern.h>
#include <lib/libsa/stand.h>

#include <sys/param.h>
#include <sys/exec.h>

#include <machine/rpb.h>
#include <machine/prom.h>

char boot_file[128];
char boot_flags[128];

extern char bootprog_name[], bootprog_rev[];

vaddr_t ptbr_save;

int debug;

void
main()
{
	u_int64_t entry;
	uint64_t marks[MARK_MAX];
	int rc;

	/* Init prom callback vector. */
	init_prom_calls();

	/* print a banner */
	printf("%s %s\n", bootprog_name, bootprog_rev);

	/* switch to OSF pal code. */
	OSFpal();

	prom_getenv(PROM_E_BOOTED_FILE, boot_file, sizeof(boot_file));
	prom_getenv(PROM_E_BOOTED_OSFLAGS, boot_flags, sizeof(boot_flags));

	if (boot_file[0] == '\0')
		bcopy("bsd", boot_file, sizeof "bsd");
	else
		(void)printf("Boot: %s %s\n", boot_file, boot_flags);

	(void)printf("Loading %s...\n", boot_file);
	marks[MARK_START] = 0;
	rc = loadfile(boot_file, &marks, LOAD_KERNEL | COUNT_KERNEL);
	(void)printf("\n");
	if (rc == 0) {
		entry = marks[MARK_START];
		(void)printf("Entering kernel at 0x%lx...\n", entry);
		(*(void (*)(u_int64_t, u_int64_t, u_int64_t))entry)
		    (0, ptbr_save, 0);
	}

	(void)printf("Boot failed!  Halting...\n");
	halt();
}
