/*-
 * Copyright (c) 2002, 2003	Sam Leffler, Errno Consulting
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

/*
 * Little program to dump the crypto statistics block and, optionally,
 * zero all the stats or just the timing stuff.
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <crypto/cryptodev.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void
printt(const char* tag, struct cryptotstat *ts)
{
	uint64_t avg, min, max;

	if (ts->count == 0)
		return;
	avg = (1000000000LL*ts->acc.tv_sec + ts->acc.tv_nsec) / ts->count;
	min = 1000000000LL*ts->min.tv_sec + ts->min.tv_nsec;
	max = 1000000000LL*ts->max.tv_sec + ts->max.tv_nsec;
	printf("%16.16s: avg %6llu ns : min %6llu ns : max %7llu ns [%u samps]\n",
		tag, avg, min, max, ts->count);
}

int
main(int argc, char *argv[])
{
	struct cryptostats stats;
	size_t slen;

	slen = sizeof (stats);
	if (sysctlbyname("kern.crypto_stats", &stats, &slen, NULL, 0) < 0)
		err(1, "kern.cryptostats");

	if (argc > 1 && strcmp(argv[1], "-z") == 0) {
		bzero(&stats.cs_invoke, sizeof (stats.cs_invoke));
		bzero(&stats.cs_done, sizeof (stats.cs_done));
		bzero(&stats.cs_cb, sizeof (stats.cs_cb));
		bzero(&stats.cs_finis, sizeof (stats.cs_finis));
		stats.cs_invoke.min.tv_sec = 10000;
		stats.cs_done.min.tv_sec = 10000;
		stats.cs_cb.min.tv_sec = 10000;
		stats.cs_finis.min.tv_sec = 10000;
		if (sysctlbyname("kern.crypto_stats", NULL, NULL, &stats, sizeof (stats)) < 0)
			err(1, "kern.cryptostats");
		exit(0);
	}
	if (argc > 1 && strcmp(argv[1], "-Z") == 0) {
		bzero(&stats, sizeof (stats));
		stats.cs_invoke.min.tv_sec = 10000;
		stats.cs_done.min.tv_sec = 10000;
		stats.cs_cb.min.tv_sec = 10000;
		stats.cs_finis.min.tv_sec = 10000;
		if (sysctlbyname("kern.crypto_stats", NULL, NULL, &stats, sizeof (stats)) < 0)
			err(1, "kern.cryptostats");
		exit(0);
	}


	printf("%u symmetric crypto ops (%u errors, %u times driver blocked)\n"
		, stats.cs_ops, stats.cs_errs, stats.cs_blocks);
	printf("%u key ops (%u errors, %u times driver blocked)\n"
		, stats.cs_kops, stats.cs_kerrs, stats.cs_kblocks);
	printf("%u crypto dispatch thread activations\n", stats.cs_intrs);
	printf("%u crypto return thread activations\n", stats.cs_rets);
	if (stats.cs_invoke.count) {
		printf("\n");
		printt("dispatch->invoke", &stats.cs_invoke);
		printt("invoke->done", &stats.cs_done);
		printt("done->cb", &stats.cs_cb);
		printt("cb->finis", &stats.cs_finis);
	}
	return 0;
}
