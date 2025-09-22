/*	$OpenBSD: radius_eapmsk.c,v 1.3 2024/02/09 11:59:23 yasuoka Exp $ */

/*-
 * Copyright (c) 2013 Internet Initiative Japan Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE"AUTHOR" AND CONTRIBUTORS AS IS'' AND
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
 */

#include <stdint.h>
#include <string.h>

#include "radius.h"

#include "radius_local.h"

int 
radius_get_eap_msk(const RADIUS_PACKET * packet, void *buf, size_t *len,
    const char *secret)
{
	uint8_t	 buf0[256];
	uint8_t	 buf1[256];
	size_t	 len0, len1, msklen;

	/* RFC 3748 defines the MSK minimum size is 64 bytes */
	if (*len < 64)
		return (-1);
	len0 = sizeof(buf0);
	len1 = sizeof(buf1);
	if (radius_get_mppe_recv_key_attr(packet, buf0, &len0, secret) == 0 &&
	    radius_get_mppe_send_key_attr(packet, buf1, &len1, secret) == 0) {
		/* addition cannot overflow since len{0,1} are limited to 256 */
		msklen = len0 + len1;
		if (msklen > *len)
			return (-1);	/* not enougth */
		memcpy(buf, buf0, len0);
		memcpy(((char *)buf) + len0, buf1, len1);
		/* zero padding to the minimum size, 64 bytes.  */
		if (msklen < 64) {
			memset(((char *)buf) + msklen, 0, 64 - msklen);
			msklen = 64;
		}
		*len = msklen;
		return (0);
	}

	return (-1);
}
