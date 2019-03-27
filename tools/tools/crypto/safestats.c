/*-
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <err.h>
#include <stdio.h>

#include "../../../sys/dev/safe/safevar.h"

/*
 * Little program to dump the statistics block for the safe driver.
 */
int
main(int argc, char *argv[])
{
	struct safe_stats stats;
	size_t slen;

	slen = sizeof (stats);
	if (sysctlbyname("hw.safe.stats", &stats, &slen, NULL, 0) < 0)
		err(1, "hw.safe.stats");

	printf("input %llu bytes %u packets\n",
		stats.st_ibytes, stats.st_ipackets);
	printf("output %llu bytes %u packets\n",
		stats.st_obytes, stats.st_opackets);
	printf("invalid %u badsession %u badflags %u\n",
		stats.st_invalid, stats.st_badsession, stats.st_badflags);
	printf("nodesc %u badalg %u ringfull %u\n",
		stats.st_nodesc, stats.st_badalg, stats.st_ringfull);
	printf("peoperr %u dmaerr %u bypasstoobig %u\n",
		stats.st_peoperr, stats.st_dmaerr, stats.st_bypasstoobig);
	printf("skipmismatch %u lenmismatch %u coffmisaligned %u cofftoobig %u\n",
		stats.st_skipmismatch, stats.st_lenmismatch,
		stats.st_coffmisaligned, stats.st_cofftoobig);
	printf("iovmisaligned %u iovnotuniform %u noicvcopy %u\n",
		stats.st_iovmisaligned, stats.st_iovnotuniform,
		stats.st_noicvcopy);
	printf("unaligned %u notuniform %u nomap %u noload %u\n",
		stats.st_unaligned, stats.st_notuniform, stats.st_nomap,
		stats.st_noload);
	printf("nomcl %u mbuf %u maxqchip %u\n",
		stats.st_nomcl, stats.st_nombuf, stats.st_maxqchip);
	printf("rng %u rngalarm %u\n",
		stats.st_rng, stats.st_rngalarm);
	return 0;
}
