/*	$OpenBSD: print-carp.c,v 1.7 2015/11/15 20:35:36 mmcc Exp $	*/

/*
 * Copyright (c) 2000 William C. Fenner.
 *                All rights reserved.
 *
 * Kevin Steves <ks@hp.se> July 2000
 * Modified to:
 * - print version, type string and packet length
 * - print IP address count if > 1 (-v)
 * - verify checksum (-v)
 * - print authentication string (-v)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * The name of William C. Fenner may not be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.  THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * from tcpdump.org:
 * Header: /tcpdump/master/tcpdump/print-vrrp.c,v 1.3 2000/10/10 05:05:08 guy Exp
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <netinet/in.h>

#include "interface.h"
#include "extract.h"
#include "addrtoname.h"

void
carp_print(const u_char *bp, u_int len, int ttl)
{
	int version, type;
	char *type_s;

	TCHECK(bp[0]);
	version = (bp[0] & 0xf0) >> 4;
	type = bp[0] & 0x0f;
	if (type == 1)
		type_s = "advertise";
	else
		type_s = "unknown";
	printf("CARPv%d-%s %d: ", version, type_s, len);
	if (ttl != 255)
		printf("[ttl=%d!] ", ttl);
	if (version != 2 || type != 1)
		return;
	TCHECK(bp[2]);
	TCHECK(bp[5]);
	printf("vhid=%d advbase=%d advskew=%d demote=%d",
	    bp[1], bp[5], bp[2], bp[4]);
	if (vflag) {
		if (TTEST2(bp[0], len) && in_cksum((const u_short*)bp, len, 0))
			printf(" (bad carp cksum %x!)",
				EXTRACT_16BITS(&bp[6]));
	}
	return;
trunc:
	printf("[|carp]");
}
