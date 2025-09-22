/*
 * testcode/unitinfra.c - unit test for infra cache.
 *
 * Copyright (c) 2025, NLnet Labs. All rights reserved.
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/**
 * \file
 * Tests the infra functionality.
 */

#include "config.h"
#include "testcode/unitmain.h"
#include "iterator/iterator.h"
#include "services/cache/infra.h"
#include "util/config_file.h"
#include "util/net_help.h"

/* lookup and get key and data structs easily */
static struct infra_data* infra_lookup_host(struct infra_cache* infra,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* zone,
	size_t zonelen, int wr, time_t now, struct infra_key** k)
{
	struct infra_data* d;
	struct lruhash_entry* e = infra_lookup_nottl(infra, addr, addrlen,
		zone, zonelen, wr);
	if(!e) return NULL;
	d = (struct infra_data*)e->data;
	if(d->ttl < now) {
		lock_rw_unlock(&e->lock);
		return NULL;
	}
	*k = (struct infra_key*)e->key;
	return d;
}

static void test_keep_probing(struct infra_cache* slab,
	struct config_file* cfg, struct sockaddr_storage one, socklen_t onelen,
	uint8_t* zone, size_t zonelen, time_t *now, int keep_probing,
	int rtt_max_timeout)
{
	uint8_t edns_lame;
	int vs, to, lame, dnsseclame, reclame, probedelay;
	struct infra_key* k;
	struct infra_data* d;

	/* configure */
	cfg->infra_cache_max_rtt = rtt_max_timeout;
	config_apply_max_rtt(rtt_max_timeout);
	slab->infra_keep_probing = keep_probing;

	/* expired previous entry */
	*now += cfg->host_ttl + 10;
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			*now, &vs, &edns_lame, &to) );

	/* simulate timeouts until the USEFUL_SERVER_TOP_TIMEOUT is reached */
	while(to < USEFUL_SERVER_TOP_TIMEOUT) {
		unit_assert( infra_rtt_update(slab, &one, onelen, zone, zonelen,
			LDNS_RR_TYPE_A, -1, to, *now) );
		unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			*now, &vs, &edns_lame, &to) );
		unit_assert( vs == 0 && to <= USEFUL_SERVER_TOP_TIMEOUT && edns_lame == 0 );
	}
	unit_assert( vs == 0 && to == USEFUL_SERVER_TOP_TIMEOUT && edns_lame == 0 );

	/* don't let the record expire */
	unit_assert( (d=infra_lookup_host(slab, &one, onelen, zone, zonelen, 0, *now, &k)) );
	unit_assert( d->timeout_A >= TIMEOUT_COUNT_MAX );
	unit_assert( d->probedelay > 0 );
	probedelay = d->probedelay;
	lock_rw_unlock(&k->entry.lock);
	cfg->host_ttl = cfg->host_ttl + *now < probedelay
		?cfg->host_ttl :probedelay + 10;

	/* advance time and check that probing is as expected; we already had a
	 * lot of A timeouts (checked above). */
	*now = probedelay;
	unit_assert( infra_get_lame_rtt(slab, &one, onelen, zone, zonelen,
		LDNS_RR_TYPE_A, &lame, &dnsseclame, &reclame, &to, *now) );
	unit_assert( lame == 0 && dnsseclame == 0 && reclame == 0
		&& to == keep_probing ?still_useful_timeout() :USEFUL_SERVER_TOP_TIMEOUT);
}

/** test host cache */
void infra_test(void)
{
	struct sockaddr_storage one;
	socklen_t onelen;
	uint8_t* zone = (uint8_t*)"\007example\003com\000";
	size_t zonelen = 13;
	struct infra_cache* slab;
	struct config_file* cfg = config_create();
	time_t now = 0;
	uint8_t edns_lame;
	int vs, to;
	struct infra_key* k;
	struct infra_data* d;
	int init = UNKNOWN_SERVER_NICENESS;
	int default_max_rtt = USEFUL_SERVER_TOP_TIMEOUT;

	unit_show_feature("infra cache");
	unit_assert(ipstrtoaddr("127.0.0.1", 53, &one, &onelen));

	slab = infra_create(cfg);
	/* insert new record */
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen, now,
		&vs, &edns_lame, &to) );
	unit_assert( vs == 0 && to == init && edns_lame == 0 );

	/* simulate no answer */
	unit_assert( infra_rtt_update(slab, &one, onelen, zone, zonelen, LDNS_RR_TYPE_A, -1, init, now) );
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			now, &vs, &edns_lame, &to) );
	unit_assert( vs == 0 && to == init*2 && edns_lame == 0 );

	/* simulate EDNS lame */
	unit_assert( infra_edns_update(slab, &one, onelen, zone, zonelen, -1, now) );
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			now, &vs, &edns_lame, &to) );
	unit_assert( vs == -1 && to == init*2  && edns_lame == 1);

	/* simulate cache expiry */
	now += cfg->host_ttl + 10;
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			now, &vs, &edns_lame, &to) );
	unit_assert( vs == 0 && to == init && edns_lame == 0 );

	/* simulate no lame answer */
	unit_assert( infra_set_lame(slab, &one, onelen,
		zone, zonelen,  now, 0, 0, LDNS_RR_TYPE_A) );
	unit_assert( (d=infra_lookup_host(slab, &one, onelen, zone, zonelen, 0, now, &k)) );
	unit_assert( d->ttl == now+cfg->host_ttl );
	unit_assert( d->edns_version == 0 );
	unit_assert(!d->isdnsseclame && !d->rec_lame && d->lame_type_A &&
		!d->lame_other);
	lock_rw_unlock(&k->entry.lock);

	/* test merge of data */
	unit_assert( infra_set_lame(slab, &one, onelen,
		zone, zonelen,  now, 0, 0, LDNS_RR_TYPE_AAAA) );
	unit_assert( (d=infra_lookup_host(slab, &one, onelen, zone, zonelen, 0, now, &k)) );
	unit_assert(!d->isdnsseclame && !d->rec_lame && d->lame_type_A &&
		d->lame_other);
	lock_rw_unlock(&k->entry.lock);

	/* test that noEDNS cannot overwrite known-yesEDNS */
	now += cfg->host_ttl + 10;
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			now, &vs, &edns_lame, &to) );
	unit_assert( vs == 0 && to == init && edns_lame == 0 );

	unit_assert( infra_edns_update(slab, &one, onelen, zone, zonelen, 0, now) );
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			now, &vs, &edns_lame, &to) );
	unit_assert( vs == 0 && to == init && edns_lame == 1 );

	unit_assert( infra_edns_update(slab, &one, onelen, zone, zonelen, -1, now) );
	unit_assert( infra_host(slab, &one, onelen, zone, zonelen,
			now, &vs, &edns_lame, &to) );
	unit_assert( vs == 0 && to == init && edns_lame == 1 );

	unit_show_feature("infra cache probing (keep-probing off, default infra-cache-max-rtt)");
	test_keep_probing(slab, cfg, one, onelen, zone, zonelen, &now, 0, default_max_rtt);

	unit_show_feature("infra cache probing (keep-probing on,  default infra-cache-max-rtt)");
	test_keep_probing(slab, cfg, one, onelen, zone, zonelen, &now, 1, default_max_rtt);

	unit_show_feature("infra cache probing (keep-probing off, low infra-cache-max-rtt)");
	test_keep_probing(slab, cfg, one, onelen, zone, zonelen, &now, 0, 3000);

	unit_show_feature("infra cache probing (keep-probing on,  low infra-cache-max-rtt)");
	test_keep_probing(slab, cfg, one, onelen, zone, zonelen, &now, 1, 3000);

	/* Re-apply defaults for other unit tests that follow */
	config_apply_max_rtt(default_max_rtt);

	infra_delete(slab);
	config_delete(cfg);
}
