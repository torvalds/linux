/*	$OpenBSD: linkaddr.c,v 1.9 2016/12/08 03:20:50 millert Exp $ */
/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <string.h>

static const char hexlist[] = "0123456789abcdef";

char *
link_ntoa(const struct sockaddr_dl *sdl)
{
	static char obuf[64];
	char *out;
	const u_char *in, *inlim;
	int namelen, i, rem;

	namelen = (sdl->sdl_nlen <= IFNAMSIZ) ? sdl->sdl_nlen : IFNAMSIZ;

	out = obuf;
	rem = sizeof(obuf);
	if (namelen > 0) {
		memcpy(out, sdl->sdl_data, namelen);
		out += namelen;
		rem -= namelen;
		if (sdl->sdl_alen > 0) {
			*out++ = ':';
			rem--;
		}
	}

	in = (const u_char *)sdl->sdl_data + sdl->sdl_nlen;
	inlim = in + sdl->sdl_alen;

	while (in < inlim && rem > 1) {
		if (in != (const u_char *)sdl->sdl_data + sdl->sdl_nlen) {
			*out++ = '.';
			rem--;
		}
		i = *in++;
		if (i > 0xf) {
			if (rem < 3)
				break;
			*out++ = hexlist[i >> 4];
			*out++ = hexlist[i & 0xf];
			rem -= 2;
		} else {
			if (rem < 2)
				break;
			*out++ = hexlist[i];
			rem--;
		}
	}
	*out = 0;
	return (obuf);
}
