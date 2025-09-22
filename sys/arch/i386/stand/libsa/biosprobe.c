/*	$OpenBSD: biosprobe.c,v 1.5 2014/03/29 18:09:29 guenther Exp $	*/

/*
 * Copyright (c) 2002 Tobias Weingartner
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <machine/biosvar.h>
#include <machine/pio.h>
#include <dev/cons.h>
#include <sys/disklabel.h>
#include "disk.h"
#include "debug.h"
#include "libsa.h"
#include "biosdev.h"


void *
getSYSCONFaddr(void)
{
	u_int32_t status;
	u_int8_t *vers;

	__asm volatile(DOINT(0x15) "\n\t"
	    "setc %%al\n\t"
	    : "=a" (status)
	    : "0" (0xC000)
	    : "%ebx", "%ecx", "%edx", "%esi", "%edi", "cc");

	/* On failure we go for a NULL */
	if (status)
		return NULL;

	/* Calculate where the version bytes are */
	vers = (void*)((BIOS_regs.biosr_es << 4) | BIOS_regs.biosr_bx);
	return vers;
}

void *
getEBDAaddr(void)
{
	u_int32_t status;
	u_int8_t *info;

	info = getSYSCONFaddr();

	if (!info)
		return NULL;

	__asm volatile(DOINT(0x15) "\n\t"
	    "setc %%al"
	    : "=a" (status)
	    : "0" (0xC100)
	    : "%ebx", "%ecx", "%edx", "%esi", "%edi", "cc");

	if (status)
		return NULL;

	info = (void *)(BIOS_regs.biosr_es << 4);

	return info;
}
