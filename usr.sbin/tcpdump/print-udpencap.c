/*	$OpenBSD: print-udpencap.c,v 1.7 2020/01/24 22:46:37 procter Exp $	*/

/*
 * Copyright (c) 2003 Markus Friedl.  All rights reserved.
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

#include <sys/types.h>
#include <stdio.h>

#include "interface.h"

void
udpencap_print(const u_char *bp, u_int len, const u_char *bp2)
{
	u_int32_t *spi;

	/* Recognize NAT-T Keepalive msgs. (draft-ietf-ipsec-udp-encaps-nn) */
	if (len == 1 && *bp == 0xFF) {
		printf("NAT-T Keepalive");
		return;
	}

	if (len < sizeof(u_int32_t)) {
		printf("[|udpencap]");
		return;
	}
	printf("udpencap: ");
	spi = (u_int32_t *)(bp);
	if (*spi == 0)
		ike_print(bp + sizeof(u_int32_t), len - sizeof(u_int32_t));
	else
		esp_print(bp, len, bp2);
}
