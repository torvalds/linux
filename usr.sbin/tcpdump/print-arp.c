/*	$OpenBSD: print-arp.c,v 1.17 2021/12/01 18:28:45 deraadt Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "ethertype.h"
#include "extract.h"			/* must come after interface.h */

/* Compatibility */
#ifndef REVARP_REQUEST
#define REVARP_REQUEST		3
#endif
#ifndef REVARP_REPLY
#define REVARP_REPLY		4
#endif

static u_char ezero[6];

void
arp_print(const u_char *bp, u_int length, u_int caplen)
{
	const struct ether_arp *ap;
	const struct ether_header *eh;
	u_short pro, hrd, op;

	ap = (struct ether_arp *)bp;
	if ((u_char *)(ap + 1) > snapend) {
		printf("[|arp]");
		return;
	}
	if (length < sizeof(struct ether_arp)) {
		printf("truncated-arp");
		default_print((u_char *)ap, length);
		return;
	}

	pro = EXTRACT_16BITS(&ap->arp_pro);
	hrd = EXTRACT_16BITS(&ap->arp_hrd);
	op = EXTRACT_16BITS(&ap->arp_op);

	if ((pro != ETHERTYPE_IP && pro != ETHERTYPE_TRAIL)
	    || ap->arp_hln != sizeof(SHA(ap))
	    || ap->arp_pln != sizeof(SPA(ap))) {
		printf("arp-#%d for proto #%d (%d) hardware #%d (%d)",
		    op, pro, ap->arp_pln, hrd, ap->arp_hln);
		return;
	}
	if (pro == ETHERTYPE_TRAIL)
		printf("trailer-");
	eh = (struct ether_header *)packetp;
	switch (op) {

	case ARPOP_REQUEST:
		printf("arp who-has %s", ipaddr_string(TPA(ap)));
		if (memcmp((char *)ezero, (char *)THA(ap), 6) != 0)
			printf(" (%s)", etheraddr_string(THA(ap)));
		printf(" tell %s", ipaddr_string(SPA(ap)));
		if (memcmp((char *)ESRC(eh), (char *)SHA(ap), 6) != 0)
			printf(" (%s)", etheraddr_string(SHA(ap)));
		break;

	case ARPOP_REPLY:
		printf("arp reply %s", ipaddr_string(SPA(ap)));
		if (memcmp((char *)ESRC(eh), (char *)SHA(ap), 6) != 0)
			printf(" (%s)", etheraddr_string(SHA(ap)));
		printf(" is-at %s", etheraddr_string(SHA(ap)));
		if (memcmp((char *)EDST(eh), (char *)THA(ap), 6) != 0)
			printf(" (%s)", etheraddr_string(THA(ap)));
		break;

	case REVARP_REQUEST:
		printf("rarp who-is %s tell %s",
		    etheraddr_string(THA(ap)),
		    etheraddr_string(SHA(ap)));
		break;

	case REVARP_REPLY:
		printf("rarp reply %s at %s",
		    etheraddr_string(THA(ap)),
		    ipaddr_string(TPA(ap)));
		break;

	default:
		printf("arp-#%d", op);
		default_print((u_char *)ap, caplen);
		return;
	}
	if (hrd != ARPHRD_ETHER)
		printf(" hardware #%d", hrd);
}
