/*	$OpenBSD: print-pim.c,v 1.9 2020/01/24 22:46:37 procter Exp $	*/

/*
 * Copyright (c) 1995, 1996
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

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "interface.h"
#include "addrtoname.h"

void
pim_print(const u_char *bp, u_int len)
{
    const u_char *ep;
    u_char type;

    ep = (const u_char *)snapend;
    if (bp >= ep)
	return;

    type = bp[1];

    switch (type) {
    case 0:
	printf(" Query");
	break;

    case 1:
	printf(" Register");
	break;

    case 2:
	printf(" Register-Stop");
	break;

    case 3:
	printf(" Join/Prune");
	break;

    case 4:
	printf(" RP-reachable");
	break;

    case 5:
	printf(" Assert");
	break;

    case 6:
	printf(" Graft");
	break;

    case 7:
	printf(" Graft-ACK");
	break;

    case 8:
	printf(" Mode");
	break;

    default:
	printf(" [type %d]", type);
	break;
    }
}
