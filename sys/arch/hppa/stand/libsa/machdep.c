/*	$OpenBSD: machdep.c,v 1.9 2004/04/07 18:24:20 mickey Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/reboot.h>
#include "libsa.h"
#include <machine/iomod.h>
#include <machine/pdc.h>

#include "dev_hppa.h"

extern struct	stable_storage sstor;	/* contents of Stable Storage */
int howto;
dev_t bootdev;

void
machdep()
{
	pdc_init();
#ifdef notyet
	debug_init();
#endif
	cninit();

#ifdef PDCDEBUG
	if (debug) {
		int i;

		printf("SSTOR:\n");
		printf("pri_boot=");	DEVPATH_PRINT(&sstor.ss_pri_boot);
		printf("alt_boot=");	DEVPATH_PRINT(&sstor.ss_alt_boot);
		printf("console =");	DEVPATH_PRINT(&sstor.ss_console);
		printf("keyboard=");	DEVPATH_PRINT(&sstor.ss_keyboard);
		printf("mem=%d, fn=%s, osver=%d\nos={",
		       sstor.ss_fast_size, sstor.ss_filenames,
		       sstor.ss_os_version);
		for (i = 0; i < sizeof(sstor.ss_os); i++)
			printf ("%x%c", sstor.ss_os[i], (i%8)? ',' : '\n');

		printf("}\nPAGE0:\n");
		printf("ivec=%x, pf=%p[%u], toc=%p[%u], rndz=%p, clk/10ms=%u\n",
		       PAGE0->ivec_special, PAGE0->ivec_mempf,
		       PAGE0->ivec_mempflen, PAGE0->ivec_toc,
		       PAGE0->ivec_toclen, PAGE0->ivec_rendz,
		       PAGE0->mem_10msec);
		printf ("mem: cont=%u, phys=%u, pdc_spa=%u, resv=%u, free=%x\n"
			"cpu_hpa=%x, pdc=%p, imm_hpa=%p[%u,%u], soft=%u\n",
		       PAGE0->memc_cont, PAGE0->memc_phsize, PAGE0->memc_adsize,
		       PAGE0->memc_resv, PAGE0->mem_free, PAGE0->mem_hpa,
		       PAGE0->mem_pdc, PAGE0->imm_hpa, PAGE0->imm_spa_size,
		       PAGE0->imm_max_mem, PAGE0->imm_soft_boot);

		printf("console:  ");	PZDEV_PRINT(&PAGE0->mem_cons);
		printf("boot:     ");	PZDEV_PRINT(&PAGE0->mem_boot);
		printf("keyboard: ");	PZDEV_PRINT(&PAGE0->mem_kbd);
	}
#endif
}
