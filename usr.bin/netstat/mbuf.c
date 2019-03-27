/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2005 Robert N. M. Watson
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)mbuf.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/sf_buf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <err.h>
#include <kvm.h>
#include <memstat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <libxo/xo.h>
#include "netstat.h"

/*
 * Print mbuf statistics.
 */
void
mbpr(void *kvmd, u_long mbaddr)
{
	struct memory_type_list *mtlp;
	struct memory_type *mtp;
	uintmax_t mbuf_count, mbuf_bytes, mbuf_free, mbuf_failures, mbuf_size;
	uintmax_t mbuf_sleeps;
	uintmax_t cluster_count, cluster_limit, cluster_free;
	uintmax_t cluster_failures, cluster_size, cluster_sleeps;
	uintmax_t packet_count, packet_bytes, packet_free, packet_failures;
	uintmax_t packet_sleeps;
	uintmax_t tag_bytes;
	uintmax_t jumbop_count, jumbop_limit, jumbop_free;
	uintmax_t jumbop_failures, jumbop_sleeps, jumbop_size;
	uintmax_t jumbo9_count, jumbo9_limit, jumbo9_free;
	uintmax_t jumbo9_failures, jumbo9_sleeps, jumbo9_size;
	uintmax_t jumbo16_count, jumbo16_limit, jumbo16_free;
	uintmax_t jumbo16_failures, jumbo16_sleeps, jumbo16_size;
	uintmax_t bytes_inuse, bytes_incache, bytes_total;
	int nsfbufs, nsfbufspeak, nsfbufsused;
	struct sfstat sfstat;
	size_t mlen;
	int error;

	mtlp = memstat_mtl_alloc();
	if (mtlp == NULL) {
		xo_warn("memstat_mtl_alloc");
		return;
	}

	/*
	 * Use memstat_*_all() because some mbuf-related memory is in uma(9),
	 * and some malloc(9).
	 */
	if (live) {
		if (memstat_sysctl_all(mtlp, 0) < 0) {
			xo_warnx("memstat_sysctl_all: %s",
			    memstat_strerror(memstat_mtl_geterror(mtlp)));
			goto out;
		}
	} else {
		if (memstat_kvm_all(mtlp, kvmd) < 0) {
			error = memstat_mtl_geterror(mtlp);
			if (error == MEMSTAT_ERROR_KVM)
				xo_warnx("memstat_kvm_all: %s",
				    kvm_geterr(kvmd));
			else
				xo_warnx("memstat_kvm_all: %s",
				    memstat_strerror(error));
			goto out;
		}
	}

	mtp = memstat_mtl_find(mtlp, ALLOCATOR_UMA, MBUF_MEM_NAME);
	if (mtp == NULL) {
		xo_warnx("memstat_mtl_find: zone %s not found", MBUF_MEM_NAME);
		goto out;
	}
	mbuf_count = memstat_get_count(mtp);
	mbuf_bytes = memstat_get_bytes(mtp);
	mbuf_free = memstat_get_free(mtp);
	mbuf_failures = memstat_get_failures(mtp);
	mbuf_sleeps = memstat_get_sleeps(mtp);
	mbuf_size = memstat_get_size(mtp);

	mtp = memstat_mtl_find(mtlp, ALLOCATOR_UMA, MBUF_PACKET_MEM_NAME);
	if (mtp == NULL) {
		xo_warnx("memstat_mtl_find: zone %s not found",
		    MBUF_PACKET_MEM_NAME);
		goto out;
	}
	packet_count = memstat_get_count(mtp);
	packet_bytes = memstat_get_bytes(mtp);
	packet_free = memstat_get_free(mtp);
	packet_sleeps = memstat_get_sleeps(mtp);
	packet_failures = memstat_get_failures(mtp);

	mtp = memstat_mtl_find(mtlp, ALLOCATOR_UMA, MBUF_CLUSTER_MEM_NAME);
	if (mtp == NULL) {
		xo_warnx("memstat_mtl_find: zone %s not found",
		    MBUF_CLUSTER_MEM_NAME);
		goto out;
	}
	cluster_count = memstat_get_count(mtp);
	cluster_limit = memstat_get_countlimit(mtp);
	cluster_free = memstat_get_free(mtp);
	cluster_failures = memstat_get_failures(mtp);
	cluster_sleeps = memstat_get_sleeps(mtp);
	cluster_size = memstat_get_size(mtp);

	mtp = memstat_mtl_find(mtlp, ALLOCATOR_MALLOC, MBUF_TAG_MEM_NAME);
	if (mtp == NULL) {
		xo_warnx("memstat_mtl_find: malloc type %s not found",
		    MBUF_TAG_MEM_NAME);
		goto out;
	}
	tag_bytes = memstat_get_bytes(mtp);

	mtp = memstat_mtl_find(mtlp, ALLOCATOR_UMA, MBUF_JUMBOP_MEM_NAME);
	if (mtp == NULL) {
		xo_warnx("memstat_mtl_find: zone %s not found",
		    MBUF_JUMBOP_MEM_NAME);
		goto out;
	}
	jumbop_count = memstat_get_count(mtp);
	jumbop_limit = memstat_get_countlimit(mtp);
	jumbop_free = memstat_get_free(mtp);
	jumbop_failures = memstat_get_failures(mtp);
	jumbop_sleeps = memstat_get_sleeps(mtp);
	jumbop_size = memstat_get_size(mtp);

	mtp = memstat_mtl_find(mtlp, ALLOCATOR_UMA, MBUF_JUMBO9_MEM_NAME);
	if (mtp == NULL) {
		xo_warnx("memstat_mtl_find: zone %s not found",
		    MBUF_JUMBO9_MEM_NAME);
		goto out;
	}
	jumbo9_count = memstat_get_count(mtp);
	jumbo9_limit = memstat_get_countlimit(mtp);
	jumbo9_free = memstat_get_free(mtp);
	jumbo9_failures = memstat_get_failures(mtp);
	jumbo9_sleeps = memstat_get_sleeps(mtp);
	jumbo9_size = memstat_get_size(mtp);

	mtp = memstat_mtl_find(mtlp, ALLOCATOR_UMA, MBUF_JUMBO16_MEM_NAME);
	if (mtp == NULL) {
		xo_warnx("memstat_mtl_find: zone %s not found",
		    MBUF_JUMBO16_MEM_NAME);
		goto out;
	}
	jumbo16_count = memstat_get_count(mtp);
	jumbo16_limit = memstat_get_countlimit(mtp);
	jumbo16_free = memstat_get_free(mtp);
	jumbo16_failures = memstat_get_failures(mtp);
	jumbo16_sleeps = memstat_get_sleeps(mtp);
	jumbo16_size = memstat_get_size(mtp);

	xo_open_container("mbuf-statistics");

	xo_emit("{:mbuf-current/%ju}/{:mbuf-cache/%ju}/{:mbuf-total/%ju} "
	    "{N:mbufs in use (current\\/cache\\/total)}\n",
	    mbuf_count + packet_count, mbuf_free + packet_free,
	    mbuf_count + packet_count + mbuf_free + packet_free);

	xo_emit("{:cluster-current/%ju}/{:cluster-cache/%ju}/"
	    "{:cluster-total/%ju}/{:cluster-max/%ju} "
	    "{N:mbuf clusters in use (current\\/cache\\/total\\/max)}\n",
	    cluster_count - packet_free, cluster_free + packet_free,
	    cluster_count + cluster_free, cluster_limit);

	xo_emit("{:packet-count/%ju}/{:packet-free/%ju} "
	    "{N:mbuf+clusters out of packet secondary zone in use "
	    "(current\\/cache)}\n",
	    packet_count, packet_free);

	xo_emit("{:jumbo-count/%ju}/{:jumbo-cache/%ju}/{:jumbo-total/%ju}/"
	    "{:jumbo-max/%ju} {:jumbo-page-size/%ju}{U:k} {N:(page size)} "
	    "{N:jumbo clusters in use (current\\/cache\\/total\\/max)}\n",
	    jumbop_count, jumbop_free, jumbop_count + jumbop_free,
	    jumbop_limit, jumbop_size / 1024);

	xo_emit("{:jumbo9-count/%ju}/{:jumbo9-cache/%ju}/"
	    "{:jumbo9-total/%ju}/{:jumbo9-max/%ju} "
	    "{N:9k jumbo clusters in use (current\\/cache\\/total\\/max)}\n",
	    jumbo9_count, jumbo9_free, jumbo9_count + jumbo9_free,
	    jumbo9_limit);

	xo_emit("{:jumbo16-count/%ju}/{:jumbo16-cache/%ju}/"
	    "{:jumbo16-total/%ju}/{:jumbo16-limit/%ju} "
	    "{N:16k jumbo clusters in use (current\\/cache\\/total\\/max)}\n",
	    jumbo16_count, jumbo16_free, jumbo16_count + jumbo16_free,
	    jumbo16_limit);

#if 0
	xo_emit("{:tag-count/%ju} {N:mbuf tags in use}\n", tag_count);
#endif

	/*-
	 * Calculate in-use bytes as:
	 * - straight mbuf memory
	 * - mbuf memory in packets
	 * - the clusters attached to packets
	 * - and the rest of the non-packet-attached clusters.
	 * - m_tag memory
	 * This avoids counting the clusters attached to packets in the cache.
	 * This currently excludes sf_buf space.
	 */
	bytes_inuse =
	    mbuf_bytes +			/* straight mbuf memory */
	    packet_bytes +			/* mbufs in packets */
	    (packet_count * cluster_size) +	/* clusters in packets */
	    /* other clusters */
	    ((cluster_count - packet_count - packet_free) * cluster_size) +
	    tag_bytes +
	    (jumbop_count * jumbop_size) +	/* jumbo clusters */
	    (jumbo9_count * jumbo9_size) +
	    (jumbo16_count * jumbo16_size);

	/*
	 * Calculate in-cache bytes as:
	 * - cached straught mbufs
	 * - cached packet mbufs
	 * - cached packet clusters
	 * - cached straight clusters
	 * This currently excludes sf_buf space.
	 */
	bytes_incache =
	    (mbuf_free * mbuf_size) +		/* straight free mbufs */
	    (packet_free * mbuf_size) +		/* mbufs in free packets */
	    (packet_free * cluster_size) +	/* clusters in free packets */
	    (cluster_free * cluster_size) +	/* free clusters */
	    (jumbop_free * jumbop_size) +	/* jumbo clusters */
	    (jumbo9_free * jumbo9_size) +
	    (jumbo16_free * jumbo16_size);

	/*
	 * Total is bytes in use + bytes in cache.  This doesn't take into
	 * account various other misc data structures, overhead, etc, but
	 * gives the user something useful despite that.
	 */
	bytes_total = bytes_inuse + bytes_incache;

	xo_emit("{:bytes-in-use/%ju}{U:K}/{:bytes-in-cache/%ju}{U:K}/"
	    "{:bytes-total/%ju}{U:K} "
	    "{N:bytes allocated to network (current\\/cache\\/total)}\n",
	    bytes_inuse / 1024, bytes_incache / 1024, bytes_total / 1024);

	xo_emit("{:mbuf-failures/%ju}/{:cluster-failures/%ju}/"
	    "{:packet-failures/%ju} {N:requests for mbufs denied "
	    "(mbufs\\/clusters\\/mbuf+clusters)}\n",
	    mbuf_failures, cluster_failures, packet_failures);
	xo_emit("{:mbuf-sleeps/%ju}/{:cluster-sleeps/%ju}/{:packet-sleeps/%ju} "
	    "{N:requests for mbufs delayed "
	    "(mbufs\\/clusters\\/mbuf+clusters)}\n",
	    mbuf_sleeps, cluster_sleeps, packet_sleeps);

	xo_emit("{:jumbop-sleeps/%ju}/{:jumbo9-sleeps/%ju}/"
	    "{:jumbo16-sleeps/%ju} {N:/requests for jumbo clusters delayed "
	    "(%juk\\/9k\\/16k)}\n",
	    jumbop_sleeps, jumbo9_sleeps, jumbo16_sleeps, jumbop_size / 1024);
	xo_emit("{:jumbop-failures/%ju}/{:jumbo9-failures/%ju}/"
	    "{:jumbo16-failures/%ju} {N:/requests for jumbo clusters denied "
	    "(%juk\\/9k\\/16k)}\n",
	    jumbop_failures, jumbo9_failures, jumbo16_failures,
	    jumbop_size / 1024);

	mlen = sizeof(nsfbufs);
	if (live &&
	    sysctlbyname("kern.ipc.nsfbufs", &nsfbufs, &mlen, NULL, 0) == 0 &&
	    sysctlbyname("kern.ipc.nsfbufsused", &nsfbufsused, &mlen,
	    NULL, 0) == 0 &&
	    sysctlbyname("kern.ipc.nsfbufspeak", &nsfbufspeak, &mlen,
	    NULL, 0) == 0)
		xo_emit("{:nsfbufs-current/%d}/{:nsfbufs-peak/%d}/"
		    "{:nsfbufs/%d} "
		    "{N:sfbufs in use (current\\/peak\\/max)}\n",
		    nsfbufsused, nsfbufspeak, nsfbufs);

	if (fetch_stats("kern.ipc.sfstat", mbaddr, &sfstat, sizeof(sfstat),
	    kread_counters) != 0)
		goto out;

        xo_emit("{:sendfile-syscalls/%ju} {N:sendfile syscalls}\n",
	    (uintmax_t)sfstat.sf_syscalls); 
        xo_emit("{:sendfile-no-io/%ju} "
	    "{N:sendfile syscalls completed without I\\/O request}\n", 
            (uintmax_t)sfstat.sf_noiocnt);
	xo_emit("{:sendfile-io-count/%ju} "
	    "{N:requests for I\\/O initiated by sendfile}\n",
	    (uintmax_t)sfstat.sf_iocnt);
        xo_emit("{:sendfile-pages-sent/%ju} "
	    "{N:pages read by sendfile as part of a request}\n",
            (uintmax_t)sfstat.sf_pages_read);
        xo_emit("{:sendfile-pages-valid/%ju} "
	    "{N:pages were valid at time of a sendfile request}\n",
            (uintmax_t)sfstat.sf_pages_valid);
        xo_emit("{:sendfile-pages-bogus/%ju} "
	    "{N:pages were valid and substituted to bogus page}\n",
            (uintmax_t)sfstat.sf_pages_bogus);
        xo_emit("{:sendfile-requested-readahead/%ju} "
	    "{N:pages were requested for read ahead by applications}\n",
            (uintmax_t)sfstat.sf_rhpages_requested);
        xo_emit("{:sendfile-readahead/%ju} "
	    "{N:pages were read ahead by sendfile}\n",
            (uintmax_t)sfstat.sf_rhpages_read);
	xo_emit("{:sendfile-busy-encounters/%ju} "
	    "{N:times sendfile encountered an already busy page}\n",
	    (uintmax_t)sfstat.sf_busy);
	xo_emit("{:sfbufs-alloc-failed/%ju} {N:requests for sfbufs denied}\n",
	    (uintmax_t)sfstat.sf_allocfail);
	xo_emit("{:sfbufs-alloc-wait/%ju} {N:requests for sfbufs delayed}\n",
	    (uintmax_t)sfstat.sf_allocwait);
out:
	xo_close_container("mbuf-statistics");
	memstat_mtl_free(mtlp);
}
