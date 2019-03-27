/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2000  Brian S. Dean <bsd@bsdhome.com>
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BRIAN S. DEAN ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL BRIAN S. DEAN BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <machine/reg.h>
#include <machine/sysarch.h>

int
i386_set_watch(int watchnum, unsigned int watchaddr, int size,
               int access, struct dbreg * d)
{
	int i;
	unsigned int mask;

	if (watchnum == -1) {
		for (i = 0, mask = 0x3; i < 4; i++, mask <<= 2)
			if ((DBREG_DRX(d,7) & mask) == 0)
				break;
		if (i < 4)
			watchnum = i;
		else
			return -1;
	}

	switch (access) {
	case DBREG_DR7_EXEC:
		size = 1; /* size must be 1 for an execution breakpoint */
		/* fall through */
	case DBREG_DR7_WRONLY:
	case DBREG_DR7_RDWR:
		break;
	default : return -1; break;
	}

	/*
	 * we can watch a 1, 2, or 4 byte sized location
	 */
	switch (size) {
	case 1  : mask = 0x00; break;
	case 2  : mask = 0x01 << 2; break;
	case 4  : mask = 0x03 << 2; break;
	default : return -1; break;
	}

	mask |= access;

	/* clear the bits we are about to affect */
	DBREG_DRX(d,7) &= ~((0x3 << (watchnum*2)) | (0x0f << (watchnum*4+16)));

	/* set drN register to the address, N=watchnum */
	DBREG_DRX(d,watchnum) = watchaddr;

	/* enable the watchpoint */
	DBREG_DRX(d,7) |= (0x2 << (watchnum*2)) | (mask << (watchnum*4+16));

	return watchnum;
}
