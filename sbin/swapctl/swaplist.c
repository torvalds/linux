/*	$OpenBSD: swaplist.c,v 1.14 2019/12/05 12:46:54 mpi Exp $	*/
/*	$NetBSD: swaplist.c,v 1.8 1998/10/08 10:00:31 mrg Exp $	*/

/*
 * Copyright (c) 1997 Matthew R. Green
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>	/* dbtob */
#include <sys/types.h>
#include <sys/swap.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "swapctl.h"

#define	dbtoqb(b) dbtob((int64_t)(b))

void
list_swap(int pri, int kflag, int pflag, int dolong)
{
	struct	swapent *sep, *fsep;
	long	blocksize;
	char	*header;
	size_t	l;
	int	hlen, totalsize, size, totalinuse, inuse, ncounted, pathmax;
	int	rnswap, nswap, i;

	nswap = swapctl(SWAP_NSWAP, 0, 0);
	if (nswap < 1)
		errx(1, "no swap devices configured");

	fsep = sep = calloc(nswap, sizeof(*sep));
	if (sep == NULL)
		err(1, "calloc");
	rnswap = swapctl(SWAP_STATS, (void *)sep, nswap);
	if (rnswap == -1)
		err(1, "SWAP_STATS");
	if (nswap != rnswap)
		warnx("SWAP_STATS different to SWAP_NSWAP (%d != %d)",
		    rnswap, nswap);

	pathmax = 11;
	if (kflag) {
		header = "1K-blocks";
		blocksize = 1024;
		hlen = strlen(header);
	} else
		header = getbsize(&hlen, &blocksize);

	if (dolong) {
		for (i = rnswap; i-- > 0; sep++)
			if (pathmax < (l = strlen(sep->se_path)))
				pathmax = l;
		sep = fsep;
		(void)printf("%-*s %*s %8s %8s %8s  %s\n",
		    pathmax, "Device", hlen, header,
		    "Used", "Avail", "Capacity", "Priority");
	}
	totalsize = totalinuse = ncounted = 0;
	for (; rnswap-- > 0; sep++) {
		if (pflag && sep->se_priority != pri)
			continue;
		ncounted++;
		size = sep->se_nblks;
		inuse = sep->se_inuse;
		totalsize += size;
		totalinuse += inuse;

		if (dolong) {
			(void)printf("%-*s %*ld ", pathmax, sep->se_path, hlen,
			    (long)(dbtoqb(size) / blocksize));

			(void)printf("%8ld %8ld %5.0f%%    %d\n",
			    (long)(dbtoqb(inuse) / blocksize),
			    (long)(dbtoqb(size - inuse) / blocksize),
			    (double)inuse / (double)size * 100.0,
			    sep->se_priority);
		}
	}
	if (dolong == 0)
		printf("total: %ld %*s allocated, %ld used, "
		    "%ld available\n",
		    (long)(dbtoqb(totalsize) / blocksize),
		    hlen, header,
		    (long)(dbtoqb(totalinuse) / blocksize),
		    (long)(dbtoqb(totalsize - totalinuse) / blocksize));
	else if (ncounted > 1)
		(void)printf("%-*s %*ld %8ld %8ld %5.0f%%\n", pathmax, "Total",
		    hlen,
		    (long)(dbtoqb(totalsize) / blocksize),
		    (long)(dbtoqb(totalinuse) / blocksize),
		    (long)(dbtoqb(totalsize - totalinuse) / blocksize),
		    (double)(totalinuse) / (double)totalsize * 100.0);
	free(fsep);
}
