/*	$OpenBSD: init_main.c,v 1.12 2023/02/23 19:48:22 miod Exp $	*/
/*	$NetBSD: init_main.c,v 1.6 2013/03/05 15:34:53 tsutsui Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)init_main.c	8.2 (Berkeley) 8/15/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
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
 *	@(#)init_main.c	8.2 (Berkeley) 8/15/93
 */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

const char version[] = "0.8";

#include <sys/param.h>
#include <lib/libkern/libkern.h>
#include <machine/board.h>
#include <luna88k/stand/boot/samachdep.h>
#include <stand/boot/cmd.h>

#include "dev_net.h"

static void get_fuse_rom_data(void);
static void get_nvram_data(void);
static const char *nvram_by_symbol(char *);

int cpuspeed;	/* for DELAY() macro */
int machtype;

uint32_t bootdev;

uint16_t dipswitch = 0;
int nplane;

#ifdef DEBUG
int debug;
#endif

/*
 * FUSE ROM and NVRAM data
 */
struct fuse_rom_byte {
	u_int32_t h;
	u_int32_t l;
};
#define	FUSE_ROM_SPACE		1024
#define FUSE_ROM_BYTES		(FUSE_ROM_SPACE / sizeof(struct fuse_rom_byte))
char fuse_rom_data[FUSE_ROM_BYTES];

#define NNVSYM		8
#define NVSYMLEN	16
#define NVVALLEN	16
struct nvram_t {
	char symbol[1 + NVSYMLEN];
	char value[1 + NVVALLEN];
} nvram[NNVSYM];

int
main(void)
{
	extern char *progname;	/* boot.c */
	extern int boottimeout;	/* boot.c */

	/* Determine the machine type from FUSE ROM data.  */
	get_fuse_rom_data();
	if (strncmp(fuse_rom_data, "MNAME=LUNA88K+", 14) == 0)
		machtype = LUNA_88K2;
	else
		machtype = LUNA_88K;

	/*
	 * Initialize the console before we print anything out.
	 */
	if (machtype == LUNA_88K) {
		progname = "LUNA-88K BOOT";
		cpuspeed = MHZ_25;
	} else {
		progname = "LUNA-88K2 BOOT";
		cpuspeed = MHZ_33;
	}

	nplane = *((int *)0x1114);	/* 0, 1, 4, or 8 */
	cninit();

#ifdef SUPPORT_ETHERNET
	try_bootp = 1;
#endif

        get_nvram_data();

	/* disable timeout if requested */
	if ((dipswitch & 0x8000) == 0) {
		boottimeout = 0;
	}

	boot(0);

	_rtt();
	/* NOTREACHED */
}

/* Get data from FUSE ROM */

void
get_fuse_rom_data(void)
{
	int i;
	struct fuse_rom_byte *p = (struct fuse_rom_byte *)FUSE_ROM_ADDR;

	for (i = 0; i < FUSE_ROM_BYTES; i++) {
		fuse_rom_data[i] =
		    (char)((((p->h) >> 24) & 0x000000f0) |
		           (((p->l) >> 28) & 0x0000000f));
		p++;
	}
}

/* Get data from NVRAM */

void
get_nvram_data(void)
{
	int i, j;
	u_int8_t *page;
	char buf[NVSYMLEN], *data;

	if (machtype == LUNA_88K) {
		data = (char *)(NVRAM_ADDR + 0x80);

		for (i = 0; i < NNVSYM; i++) {
			for (j = 0; j < NVSYMLEN; j++) {
				buf[j] = *data;
				data += 4;
			}
			strncpy(nvram[i].symbol, buf, sizeof(nvram[i].symbol));

			for (j = 0; j < NVVALLEN; j++) {
				buf[j] = *data;
				data += 4;
			}
			strncpy(nvram[i].value, buf, sizeof(nvram[i].value));
		}
	} else if (machtype == LUNA_88K2) {
		page = (u_int8_t *)(NVRAM_ADDR_88K2 + 0x20);

		for (i = 0; i < NNVSYM; i++) {
			*page = (u_int8_t)i;

			data = (char *)NVRAM_ADDR_88K2;
			strncpy(nvram[i].symbol, data, sizeof(nvram[i].symbol));

			data = (char *)(NVRAM_ADDR_88K2 + 0x10);
			strncpy(nvram[i].value, data, sizeof(nvram[i].value));
		}
	}
}

const char *
nvram_by_symbol(symbol)
	char *symbol;
{
	const char *value;
	int i;

	value = NULL;

	for (i = 0; i < NNVSYM; i++) {
		if (strncmp(nvram[i].symbol, symbol, NVSYMLEN) == 0) {
			value = nvram[i].value;
			break;
		}
	}

	return value;
}

void
_rtt(void)
{
	*(volatile unsigned int *)RESET_CPU_ALL = 0;
	for (;;) ;
	/* NOTREACHED */
}

/*
 * Return the default boot device.
 */
void
devboot(dev_t dev, char *path)
{
	const char *nvv;
	int unit, part;

        /* Determine the 'auto-boot' device from NVRAM data */
	nvv = nvram_by_symbol("boot_unit");
	if (nvv != NULL)
		unit = (int)strtol(nvv, NULL, 10);
	else
		unit = 0;
	nvv = nvram_by_symbol("boot_partition");
	if (nvv != NULL)
		part = (int)strtol(nvv, NULL, 10);
	else
		part = 0;

	nvv = nvram_by_symbol("boot_device");

	snprintf(path, BOOTDEVLEN, "%s(%d,%d)",
	    nvv != NULL ? nvv : "sd", unit, part);
}

void
machdep()
{
	/* Nothing to do - everything done in main() already */
}
