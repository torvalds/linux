/*	$OpenBSD: dns64_synth.c,v 1.1 2021/01/24 18:29:15 florian Exp $	*/

/*
 * dns64/dns64.h - DNS64 module
 *
 * Copyright (c) 2009, Viagénie. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2021 Florian Obser <florian@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <net/route.h>

#include <string.h>

#include "libunbound/config.h"
#include "libunbound/util/regional.h"
#include "libunbound/util/data/msgencode.h"
#include "libunbound/util/data/msgparse.h"
#include "libunbound/util/data/msgreply.h"

#include "log.h"
#include "unwind.h"
#include "frontend.h"
#include "dns64_synth.h"

extern struct dns64_prefix	*dns64_prefixes;
extern int			 dns64_prefix_count;

static void
synthesize_aaaa(const struct in6_addr *in6, int prefixlen, const uint8_t *a,
    size_t a_len, uint8_t *aaaa, size_t aaaa_len)
{
	size_t i, pos;
	memcpy(aaaa, in6->s6_addr, sizeof(in6->s6_addr));
	for (i = 0, pos = prefixlen / 8; i < a_len && pos < aaaa_len;
	    i++, pos++) {
		if (pos == 8)
			aaaa[pos++] = 0;
		aaaa[pos] = a[i];
	}
}

/*
 * Copied from unbound/dns64/dns64.c and slightly extended to work with more
 * than one IPv6 prefix.
 */

void
dns64_synth_aaaa_data(const struct ub_packed_rrset_key* fk, const struct
    packed_rrset_data* fd, struct ub_packed_rrset_key *dk, struct
    packed_rrset_data **dd_out, struct regional *region)
{
	struct packed_rrset_data	*dd;
	size_t				 i, pos;
	int				 j;

	/*
	 * Create synthesized AAAA RR set data. We need to allocated extra
	 * memory for the RRs themselves. Each RR has a length, TTL, pointer to
	 * wireformat data, 2 bytes of data length, and 16 bytes of IPv6
	 * address.
	 */
	if(fd->count > RR_COUNT_MAX) {
		*dd_out = NULL;
		return; /* integer overflow protection in alloc */
	}
	if (!(dd = *dd_out = regional_alloc(region, sizeof(struct
	    packed_rrset_data) + fd->count * dns64_prefix_count *
	    (sizeof(size_t) + sizeof(time_t) + sizeof(uint8_t*) + 2 + 16)))) {
		log_warnx("out of memory");
		return;
	}

	/* Copy attributes from A RR set. */
	dd->ttl = fd->ttl;
	dd->count = fd->count * dns64_prefix_count;
	dd->rrsig_count = 0;
	dd->trust = fd->trust;
	dd->security = fd->security;

	/* Synthesize AAAA records. Adjust pointers in structure. */
	dd->rr_len = (size_t*)((uint8_t*)dd + sizeof(struct packed_rrset_data));
	dd->rr_data = (uint8_t**)&dd->rr_len[dd->count];
	dd->rr_ttl = (time_t*)&dd->rr_data[dd->count];
	for(i = 0, pos = 0; i < fd->count; ++i) {
		if (fd->rr_len[i] != 6 || fd->rr_data[i][0] != 0 ||
		    fd->rr_data[i][1] != 4) {
			*dd_out = NULL;
			return;
		}
		for (j = 0; j < dns64_prefix_count; j++, pos++) {
			dd->rr_len[pos] = 18;
			dd->rr_ttl[pos] = fd->rr_ttl[i];
			dd->rr_data[pos] = (uint8_t*)&dd->rr_ttl[dd->count] +
			    18 * pos;
			dd->rr_data[pos][0] = 0;
			dd->rr_data[pos][1] = 16;
			synthesize_aaaa(&dns64_prefixes[j].in6,
			    dns64_prefixes[j].prefixlen, &fd->rr_data[i][2],
			    fd->rr_len[i]-2, &dd->rr_data[pos][2],
			    dd->rr_len[pos]-2);
		}
	}

	/*
	 * Create synthesized AAAA RR set key. This is mostly just bookkeeping,
	 * nothing interesting here.
	 */
	if(!dk) {
		log_warnx("no key");
		*dd_out = NULL;
		return;
	}

	dk->rk.dname = (uint8_t*)regional_alloc_init(region, fk->rk.dname,
	    fk->rk.dname_len);

	if(!dk->rk.dname) {
		log_warnx("out of memory");
		*dd_out = NULL;
		return;
	}

	dk->rk.type = htons(LDNS_RR_TYPE_AAAA);
	memset(&dk->entry, 0, sizeof(dk->entry));
	dk->entry.key = dk;
	dk->entry.hash = rrset_key_hash(&dk->rk);
	dk->entry.data = dd;
}
