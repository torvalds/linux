/*	$OpenBSD: in_cksum.c,v 1.1 2015/10/02 04:26:47 renato Exp $	*/
/*	$NetBSD: in_cksum.c,v 1.3 1995/04/22 13:53:48 cgd Exp $	*/

/*
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
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
 * @(#) Header: in_cksum.c,v 1.1 92/09/11 01:15:55 leres Exp  (LBL)
 */

#include <sys/types.h>

#include "eigrpd.h"
#include "log.h"

/*
 * Checksum routine for Internet Protocol family headers.
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 * In particular, it should not be this one.
 */
uint16_t
in_cksum(void *p, size_t l)
{
	unsigned int	 sum = 0;
	int		 len;
	unsigned char	*cp = p;

	/* ensure that < 2^16 bytes being summed */
	if (l >= (1 << 16))
		fatalx("in_cksum: packet too big");
	len = (int)l;

	if (((long)cp & 1) == 0) {
		while (len > 1) {
			sum += htons(*(unsigned short *)cp);
			cp += 2;
			len -= 2;
		}
	} else {
		while (len > 1) {
			sum += *cp++ << 8;
			sum += *cp++;
			len -= 2;
		}
	}
	if (len == 1)
		sum += *cp << 8;

	sum = (sum >> 16) + (sum & 0xffff); /* add in accumulated carries */
	sum += sum >> 16;		/* add potential last carry */
	sum = ntohs(sum);
	return (0xffff & ~sum);
}
