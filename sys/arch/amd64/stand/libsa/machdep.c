/*	$OpenBSD: machdep.c,v 1.8 2019/05/10 21:20:43 mlarkin Exp $	*/

/*
 * Copyright (c) 2004 Tom Cosgrove
 * Copyright (c) 1997-1999 Michael Shalayeff
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

#include "libsa.h"
#include "biosdev.h"
#include <machine/apmvar.h>
#include <machine/biosvar.h>
#include <machine/specialreg.h>
#include <machine/vmmvar.h>

volatile struct BIOS_regs	BIOS_regs;

#if defined(DEBUG)
#define CKPT(c)	(*(u_int16_t*)0xb8148 = 0x4700 + (c))
#else
#define CKPT(c) /* c */
#endif

const char *vmm_hv_signature = VMM_HV_SIGNATURE;

void
machdep(void)
{
	int i, j, vmm = 0;
	struct i386_boot_probes *pr;
	uint32_t dummy, ebx, ecx, edx;
	dev_t dev;

	/*
	 * The list of probe routines is now in conf.c.
	 */
	for (i = 0; i < nibprobes; i++) {
		pr = &probe_list[i];
		if (pr != NULL) {
			printf("%s:", pr->name);

			for (j = 0; j < pr->count; j++) {
				(*(pr->probes)[j])();
			}

			printf("\n");
		}
	}

	CPUID(0x1, dummy, dummy, ecx, dummy);
	if (ecx & CPUIDECX_HV) {
		CPUID(0x40000000, dummy, ebx, ecx, edx);
		if (memcmp(&ebx, &vmm_hv_signature[0], sizeof(uint32_t)) == 0 &&
		    memcmp(&ecx, &vmm_hv_signature[4], sizeof(uint32_t)) == 0 &&
		    memcmp(&edx, &vmm_hv_signature[8], sizeof(uint32_t)) == 0)
			vmm = 1;
	}

	/* Set console to com0/115200 by default in vmm */
	if (vmm) {
		dev = ttydev("com0");
		cnspeed(dev, 115200);
		cnset(dev);
	}
}

int
check_skip_conf(void)
{
	/* Return non-zero (skip boot.conf) if Control "shift" key down */
	return (pc_getshifts(0) & 0x04);
}
