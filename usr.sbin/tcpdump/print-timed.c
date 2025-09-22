/*	$OpenBSD: print-timed.c,v 1.8 2020/01/24 22:46:37 procter Exp $	*/

/*
 * Copyright (c) 2000 Ben Smithurst <ben@scientia.demon.co.uk>
 * All rights reserved.
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
 * from tcpdump.org:
 * Header: /tcpdump/master/tcpdump/print-timed.c,v 1.1 2000/10/06 05:35:37 guy Exp
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>

#include <netinet/in.h>

#include <protocols/timed.h>

#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"                    /* must come after interface.h */

static const char *tsptype[TSPTYPENUMBER] =
  { "ANY", "ADJTIME", "ACK", "MASTERREQ", "MASTERACK", "SETTIME", "MASTERUP",
  "SLAVEUP", "ELECTION", "ACCEPT", "REFUSE", "CONFLICT", "RESOLVE", "QUIT",
  "DATE", "DATEREQ", "DATEACK", "TRACEON", "TRACEOFF", "MSITE", "MSITEREQ",
  "TEST", "SETDATE", "SETDATEREQ", "LOOP" };

void
timed_print(const u_char *bp, u_int length)
{
#define endof(x) ((u_char *)&(x) + sizeof (x))
	struct tsp *tsp = (struct tsp *)bp;
	long sec, usec;
	const u_char *end;

	TCHECK(tsp->tsp_type);
	if (tsp->tsp_type < TSPTYPENUMBER)
		printf("TSP_%s", tsptype[tsp->tsp_type]);
	else
		printf("(tsp_type %#x)", tsp->tsp_type);

	TCHECK(tsp->tsp_vers);
	printf(" vers %d", tsp->tsp_vers);

	TCHECK(tsp->tsp_seq);
	printf(" seq %d", tsp->tsp_seq);

	if (tsp->tsp_type == TSP_LOOP) {
		TCHECK(tsp->tsp_hopcnt);
		printf(" hopcnt %d", tsp->tsp_hopcnt);
	} else if (tsp->tsp_type == TSP_SETTIME ||
	    tsp->tsp_type == TSP_ADJTIME ||
	    tsp->tsp_type == TSP_SETDATE ||
	    tsp->tsp_type == TSP_SETDATEREQ) {
		TCHECK(tsp->tsp_time);
		sec = EXTRACT_32BITS(&tsp->tsp_time.tv_sec);
		usec = EXTRACT_32BITS(&tsp->tsp_time.tv_usec);
		if (usec < 0)
			/* corrupt, skip the rest of the packet */
			return;
		printf(" time ");
		if (sec < 0 && usec != 0) {
			sec++;
			if (sec == 0)
				fputc('-', stdout);
			usec = 1000000 - usec;
		}
		printf("%ld.%06ld", sec, usec);
	}

	end = endof(tsp->tsp_name) > snapend ? snapend : endof(tsp->tsp_name);
	printf(" name ");
	if (fn_print(tsp->tsp_name, end))
		goto trunc;
	return;
trunc:
	printf(" [|timed]");
}
