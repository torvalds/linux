/*	$OpenBSD: print-etherip.c,v 1.10 2018/02/10 10:00:32 dlg Exp $	*/

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Format and print etherip packets
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>
#include <netinet/if_ether.h>
#include <netinet/ip_ether.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

#include "addrtoname.h"
#include "interface.h"
#include "extract.h"		    /* must come after interface.h */

extern u_short extracted_ethertype;

void
etherip_print(const u_char *bp, u_int caplen, u_int len)
{
	struct ether_header *eh;
	const u_char *pbuf = bp;
	u_int plen = caplen, hlen;
	u_int16_t etype;

	printf("etherip ");

	if (plen < sizeof(struct etherip_header)) {
		printf("[|etherip]");
		return;
	}

	switch (*pbuf >> 4) {
	case 2:
		hlen = 1;
		printf("%d", 2);
		break;
	case 3:
		hlen = 2;
		printf("%d", 3);
		break;
	default:
		hlen = 0;
		printf("unknown");
		break;
	}
	printf(" len %d", len);
	if (hlen == 0)
		return;

	printf(": ");

	if (plen < hlen) {
		printf("[|etherip]");
		return;
	}
	pbuf += hlen;
	plen -= hlen;
	len -= hlen;

	if (eflag)
		ether_print(pbuf, len);
	eh = (struct ether_header *)pbuf;
	if (plen < sizeof(struct ether_header)) {
		printf("[|ether]");
		return;
	}
	etype = EXTRACT_16BITS(pbuf + offsetof(struct ether_header, ether_type));
	pbuf += sizeof(struct ether_header);
	plen -= sizeof(struct ether_header);
	len -= sizeof(struct ether_header);

	/* XXX LLC? */
	extracted_ethertype = 0;
	if (etype <= ETHERMTU) {
		if (llc_print(pbuf, len, plen, ESRC(eh), EDST(eh)) == 0) {
			if (!eflag)
				ether_print((u_char *)eh, len);
			if (extracted_ethertype) {
				printf("LLC %s",
				    etherproto_string(htons(extracted_ethertype)));
			}
			if (!xflag && !qflag)
				default_print(pbuf, plen);
		}
	} else if (ether_encap_print(etype, pbuf, len, plen) == 0) {
		if (!eflag)
			ether_print((u_char *)eh, len + sizeof(*eh));
		if (!xflag && !qflag)
			default_print(pbuf, plen);
	}
}
