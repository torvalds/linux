/*	$OpenBSD: in_cksum.c,v 1.5 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff
 * All rights reserved.
 *
 * based on a sparc version of Zubin Dittia.
 * Copyright (c) 1995 Zubin Dittia.
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
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <netinet/in.h>

/*
 * Checksum routine for Internet Protocol family headers.
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 *
 * HPPA version.
 */

#define ADD32	asm volatile(	"ldw 0x00(%1), %%r19! ldw 0x04(%1), %%r20\n\t" \
				"add  %0, %%r19, %0 ! addc  %0, %%r20, %0\n\t" \
				"ldw 0x08(%1), %%r19! ldw 0x0c(%1), %%r20\n\t" \
				"addc %0, %%r19, %0 ! addc  %0, %%r20, %0\n\t" \
				"ldw 0x10(%1), %%r19! ldw 0x14(%1), %%r20\n\t" \
				"addc %0, %%r19, %0 ! addc  %0, %%r20, %0\n\t" \
				"ldw 0x18(%1), %%r19! ldw 0x1c(%1), %%r20\n\t" \
				"addc %0, %%r19, %0 ! addc  %0, %%r20, %0\n\t" \
				"ldo 0x20(%1), %1   ! addc  %0, %%r0 , %0" \
				: "+r" (sum), "+r" (w) :: "r20", "r19")
#define ADD16	asm volatile(	"ldw 0x00(%1), %%r19! ldw 0x04(%1), %%r20\n\t" \
				"add   %0, %%r19, %0! addc  %0, %%r20, %0\n\t" \
				"ldw 0x08(%1), %%r19! ldw 0x0c(%1), %%r20\n\t" \
				"addc  %0, %%r19, %0! addc  %0, %%r20, %0\n\t" \
				"ldo 0x10(%1), %1   ! addc  %0, %%r0 , %0" \
				: "+r" (sum), "+r" (w) :: "r20", "r19")

#define ADDCARRY	{if (sum > 0xffff) sum -= 0xffff;}
#define REDUCE		{sum = (sum & 0xffff) + (sum >> 16); ADDCARRY}
#define ROL		asm volatile ("shd %0, %0, 8, %0" : "+r" (sum))
#define ADDBYTE		{ROL; sum += *w++; bins++; mlen--;}
#define ADDSHORT	{sum += *(u_short *)w; w += 2; mlen -= 2;}
#define ADDWORD	asm volatile(	"ldwm 4(%1), %%r19! add %0, %%r19, %0\n\t" \
				"ldo -4(%2), %2   ! addc    %0, 0, %0" \
				: "+r" (sum), "+r" (w), "+r" (mlen) :: "r19")

int
in_cksum(struct mbuf *m, int len)
{
	register u_int sum = 0;
	register u_int bins = 0;

	for (; m && len; m = m->m_next) {
		register int mlen = m->m_len;
		register u_char *w;

		if (!mlen)
			continue;
		if (len < mlen)
			mlen = len;
		len -= mlen;
		w = mtod(m, u_char *);

		if (mlen > 16) {
			/*
			 * If we are aligned on a doubleword boundary
			 * do 32 bit bundled operations
			 */
			if ((7 & (u_long)w) != 0) {
				if ((1 & (u_long)w) != 0)
					ADDBYTE;
				if ((2 & (u_long)w) != 0)
					ADDSHORT;
				if ((4 & (u_long)w) != 0)
					ADDWORD;
			}

			while ((mlen -= 32) >= 0)
				ADD32;

			mlen += 32;
			if (mlen >= 16) {
				ADD16;
				mlen -= 16;
			}
		}

		while (mlen > 0)
			ADDBYTE;
	}
	if (bins & 1)
		ROL;
	REDUCE;

	return (0xffff ^ sum);
}
