/*	$OpenBSD: ntfs_compr.c,v 1.7 2013/11/24 16:02:30 jsing Exp $	*/
/*	$NetBSD: ntfs_compr.c,v 1.1 2002/12/23 17:38:31 jdolecek Exp $	*/

/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	Id: ntfs_compr.c,v 1.4 1999/05/12 09:42:54 semenu Exp
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>

#include <ntfs/ntfs.h>
#include <ntfs/ntfs_compr.h>

#define GET_UINT16(addr)	(*((u_int16_t *)(addr)))

int
ntfs_uncompblock(u_int8_t *buf, u_int8_t *cbuf)
{
	u_int32_t ctag;
	int len, dshift, lmask;
	int blen, boff;
	int i, j;
	int pos, cpos;

	len = GET_UINT16(cbuf) & 0xFFF;
	DPRINTF("ntfs_uncompblock: block length: %d + 3, 0x%x,0x%04x\n",
	    len, len, GET_UINT16(cbuf));

	if (!(GET_UINT16(cbuf) & 0x8000)) {
		if ((len + 1) != NTFS_COMPBLOCK_SIZE) {
			DPRINTF("ntfs_uncompblock: len: %x instead of %d\n",
			    len, 0xfff);
		}
		memcpy(buf, cbuf + 2, len + 1);
		bzero(buf + len + 1, NTFS_COMPBLOCK_SIZE - 1 - len);
		return (len + 3);
	}
	cpos = 2;
	pos = 0;
	while ((cpos < len + 3) && (pos < NTFS_COMPBLOCK_SIZE)) {
		ctag = cbuf[cpos++];
		for (i = 0; (i < 8) && (pos < NTFS_COMPBLOCK_SIZE); i++) {
			if (ctag & 1) {
				for (j = pos - 1, lmask = 0xFFF, dshift = 12;
				     j >= 0x10; j >>= 1) {
					dshift--;
					lmask >>= 1;
				}
				boff = -1 - (GET_UINT16(cbuf + cpos) >> dshift);
				blen = 3 + (GET_UINT16(cbuf + cpos) & lmask);
				for (j = 0; (j < blen) &&
				    (pos < NTFS_COMPBLOCK_SIZE); j++) {
					buf[pos] = buf[pos + boff];
					pos++;
				}
				cpos += 2;
			} else {
				buf[pos++] = cbuf[cpos++];
			}
			ctag >>= 1;
		}
	}
	return (len + 3);
}

int
ntfs_uncompunit(struct ntfsmount *ntmp, u_int8_t *uup, u_int8_t *cup)
{
	int i;
	int off = 0;
	int new;

	for (i = 0; i * NTFS_COMPBLOCK_SIZE < ntfs_cntob(NTFS_COMPUNIT_CL);
	    i++) {
		new = ntfs_uncompblock(uup + i * NTFS_COMPBLOCK_SIZE,
		    cup + off);
		if (new == 0)
			return (EINVAL);
		off += new;
	}
	return (0);
}
