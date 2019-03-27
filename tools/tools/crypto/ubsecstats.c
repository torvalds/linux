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

#include "../../../sys/dev/ubsec/ubsecvar.h"

/*
 * Little program to dump the statistics block for the ubsec driver.
 */
int
main(int argc, char *argv[])
{
	struct ubsec_stats stats;
	size_t slen;

	slen = sizeof (stats);
	if (sysctlbyname("hw.ubsec.stats", &stats, &slen, NULL, 0) < 0)
		err(1, "kern.ubsec_stats");

	printf("input %llu bytes %u packets\n",
		stats.hst_ibytes, stats.hst_ipackets);
	printf("output %llu bytes %u packets\n",
		stats.hst_obytes, stats.hst_opackets);
	printf("invalid %u badsession %u badflags %u\n",
		stats.hst_invalid, stats.hst_badsession, stats.hst_badflags);
	printf("nodesc %u badalg %u nomem %u queuefull %u\n",
		stats.hst_nodesc, stats.hst_badalg, stats.hst_nomem,
		stats.hst_queuefull);
	printf("dmaerr %u mcrerr %u nodmafree %u\n",
		stats.hst_dmaerr, stats.hst_mcrerr, stats.hst_nodmafree);
	printf("lenmismatch %u skipmisatch %u iovmisalined %u\n",
		stats.hst_lenmismatch, stats.hst_skipmismatch,
		stats.hst_iovmisaligned);
	printf("noirq %u unaligned %u nomap %u noload %u nomcl %u\n",
		stats.hst_noirq, stats.hst_unaligned, stats.hst_nomap,
		stats.hst_noload, stats.hst_nomcl);
	printf("totbatch %u maxbatch %u\n",
		stats.hst_totbatch, stats.hst_maxbatch);
	printf("maxqueue %u maxqchip %u mcr1full %u\n",
		stats.hst_maxqueue, stats.hst_maxqchip, stats.hst_mcr1full);
	printf("rng %u modexp %u moexpcrt %u\n",
		stats.hst_rng, stats.hst_modexp, stats.hst_modexpcrt);
	return 0;
}
