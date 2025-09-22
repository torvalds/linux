/*	$OpenBSD: pciprobe.c,v 1.11 2023/07/24 14:02:36 jsg Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <stand/boot/bootarg.h>
#include <machine/biosvar.h>
#include "libsa.h"

#define PCI_SIG 0x20494350		/* PCI Signature */

void
pciprobe(void)
{
	bios_pciinfo_t bpi;
	u_int32_t hw_chars, rev, rc, sig;
	u_int32_t entry32, lastbus;

	/* PCI BIOS v2.0c+ - Installation Check */
	__asm volatile(DOINT(0x1A)
		: "=a" (hw_chars), "=b" (rev), "=c" (lastbus),
		  "=d" (sig), "=D" (entry32), "=@ccc" (rc)
		: "0" (0xB101), "4" (0x0)
		: "cc");

	if (rc & 0xff || hw_chars & 0xff00)
		return;
	if (sig != PCI_SIG)
		return;

	printf(" pci");
#ifdef DEBUG
	printf("[V%d.%d, %x 0x%x %d]", (rev>>8)&0xFF, (rev&0xFF),
		hw_chars, entry32, (rc>>8)&0xFF);
#endif

	bpi.pci_chars = hw_chars & 0xFFFF;
	bpi.pci_rev = rev & 0xFFFF;
	bpi.pci_entry32 = entry32;
	bpi.pci_lastbus = lastbus & 0xFF;

	addbootarg(BOOTARG_PCIINFO, sizeof(bios_pciinfo_t), &bpi);
}
