/*	$OpenBSD: print-netbios.c,v 1.10 2015/01/16 06:40:21 deraadt Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996
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
 *
 * Format and print NETBIOS packets.
 * Contributed by Brad Parker (brad@fcr.com).
 */

#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "netbios.h"
#include "extract.h"

/*
 * Print NETBIOS packets.
 */
void
netbios_print(struct p8022Hdr *nb, u_int length)
{
	if (length < p8022Size) {
		printf(" truncated-netbios %d", length);
		return;
	}

	TCHECK(*nb);

	if (nb->flags == UI)
		printf("802.1 UI ");
	else
		printf("802.1 CONN ");

#if 0
	netbios_decode(nb, (u_char *)nb + p8022Size, length - p8022Size);
#endif

trunc:
	printf("[|netbios]");
}
