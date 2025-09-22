/*
 * testcode/unitmain.c - unit test main program for unbound.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
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
 * Unit test main program. Calls all the other unit tests.
 * Exits with code 1 on a failure. 0 if all unit tests are successful.
 */

#include "config.h"
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

#ifdef HAVE_OPENSSL_CONF_H
#include <openssl/conf.h>
#endif

#ifdef HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif

#ifdef HAVE_NSS
/* nss3 */
#include "nss.h"
#endif

#include "sldns/rrdef.h"
#include "sldns/keyraw.h"
#include "util/log.h"
#include "testcode/unitmain.h"

/** number of tests done */
int testcount = 0;

#include "util/alloc.h"
/** test alloc code */
static void
alloc_test(void) {
	alloc_special_type *t1, *t2;
	struct alloc_cache major, minor1, minor2;
	int i;

	unit_show_feature("alloc_special_obtain");
	alloc_init(&major, NULL, 0);
	alloc_init(&minor1, &major, 0);
	alloc_init(&minor2, &major, 1);

	t1 = alloc_special_obtain(&minor1);
	alloc_clear(&minor1);

	alloc_special_release(&minor2, t1);
	t2 = alloc_special_obtain(&minor2);
	unit_assert( t1 == t2 ); /* reused */
	alloc_special_release(&minor2, t1);

	for(i=0; i<100; i++) {
		t1 = alloc_special_obtain(&minor1);
		alloc_special_release(&minor2, t1);
	}
	if(0) {
		alloc_stats(&minor1);
		alloc_stats(&minor2);
		alloc_stats(&major);
	}
	/* reuse happened */
	unit_assert(minor1.num_quar + minor2.num_quar + major.num_quar == 11);

	alloc_clear(&minor1);
	alloc_clear(&minor2);
	unit_assert(major.num_quar == 11);
	alloc_clear(&major);
}

#include "util/net_help.h"
/** test net code */
static void 
net_test(void)
{
	const char* t4[] = {"\000\000\000\000",
		"\200\000\000\000",
		"\300\000\000\000",
		"\340\000\000\000",
		"\360\000\000\000",
		"\370\000\000\000",
		"\374\000\000\000",
		"\376\000\000\000",
		"\377\000\000\000",
		"\377\200\000\000",
		"\377\300\000\000",
		"\377\340\000\000",
		"\377\360\000\000",
		"\377\370\000\000",
		"\377\374\000\000",
		"\377\376\000\000",
		"\377\377\000\000",
		"\377\377\200\000",
		"\377\377\300\000",
		"\377\377\340\000",
		"\377\377\360\000",
		"\377\377\370\000",
		"\377\377\374\000",
		"\377\377\376\000",
		"\377\377\377\000",
		"\377\377\377\200",
		"\377\377\377\300",
		"\377\377\377\340",
		"\377\377\377\360",
		"\377\377\377\370",
		"\377\377\377\374",
		"\377\377\377\376",
		"\377\377\377\377",
		"\377\377\377\377",
		"\377\377\377\377",
	};
	unit_show_func("util/net_help.c", "str_is_ip6");
	unit_assert( str_is_ip6("::") );
	unit_assert( str_is_ip6("::1") );
	unit_assert( str_is_ip6("2001:7b8:206:1:240:f4ff:fe37:8810") );
	unit_assert( str_is_ip6("fe80::240:f4ff:fe37:8810") );
	unit_assert( !str_is_ip6("0.0.0.0") );
	unit_assert( !str_is_ip6("213.154.224.12") );
	unit_assert( !str_is_ip6("213.154.224.255") );
	unit_assert( !str_is_ip6("255.255.255.0") );
	unit_show_func("util/net_help.c", "is_pow2");
	unit_assert( is_pow2(0) );
	unit_assert( is_pow2(1) );
	unit_assert( is_pow2(2) );
	unit_assert( is_pow2(4) );
	unit_assert( is_pow2(8) );
	unit_assert( is_pow2(16) );
	unit_assert( is_pow2(1024) );
	unit_assert( is_pow2(1024*1024) );
	unit_assert( is_pow2(1024*1024*1024) );
	unit_assert( !is_pow2(3) );
	unit_assert( !is_pow2(5) );
	unit_assert( !is_pow2(6) );
	unit_assert( !is_pow2(7) );
	unit_assert( !is_pow2(9) );
	unit_assert( !is_pow2(10) );
	unit_assert( !is_pow2(11) );
	unit_assert( !is_pow2(17) );
	unit_assert( !is_pow2(23) );
	unit_assert( !is_pow2(257) );
	unit_assert( !is_pow2(259) );

	/* test addr_mask */
	unit_show_func("util/net_help.c", "addr_mask");
	if(1) {
		struct sockaddr_in a4;
		struct sockaddr_in6 a6;
		socklen_t l4 = (socklen_t)sizeof(a4);
		socklen_t l6 = (socklen_t)sizeof(a6);
		int i;
		a4.sin_family = AF_INET;
		a6.sin6_family = AF_INET6;
		for(i=0; i<35; i++) {
			/* address 255.255.255.255 */
			memcpy(&a4.sin_addr, "\377\377\377\377", 4);
			addr_mask((struct sockaddr_storage*)&a4, l4, i);
			unit_assert(memcmp(&a4.sin_addr, t4[i], 4) == 0);
		}
		memcpy(&a6.sin6_addr, "\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377", 16);
		addr_mask((struct sockaddr_storage*)&a6, l6, 128);
		unit_assert(memcmp(&a6.sin6_addr, "\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377", 16) == 0);
		addr_mask((struct sockaddr_storage*)&a6, l6, 122);
		unit_assert(memcmp(&a6.sin6_addr, "\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\300", 16) == 0);
		addr_mask((struct sockaddr_storage*)&a6, l6, 120);
		unit_assert(memcmp(&a6.sin6_addr, "\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\000", 16) == 0);
		addr_mask((struct sockaddr_storage*)&a6, l6, 64);
		unit_assert(memcmp(&a6.sin6_addr, "\377\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000", 16) == 0);
		addr_mask((struct sockaddr_storage*)&a6, l6, 0);
		unit_assert(memcmp(&a6.sin6_addr, "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16) == 0);
	}

	/* test addr_in_common */
	unit_show_func("util/net_help.c", "addr_in_common");
	if(1) {
		struct sockaddr_in a4, b4;
		struct sockaddr_in6 a6, b6;
		socklen_t l4 = (socklen_t)sizeof(a4);
		socklen_t l6 = (socklen_t)sizeof(a6);
		int i;
		a4.sin_family = AF_INET;
		b4.sin_family = AF_INET;
		a6.sin6_family = AF_INET6;
		b6.sin6_family = AF_INET6;
		memcpy(&a4.sin_addr, "abcd", 4);
		memcpy(&b4.sin_addr, "abcd", 4);
		unit_assert(addr_in_common((struct sockaddr_storage*)&a4, 32,
			(struct sockaddr_storage*)&b4, 32, l4) == 32);
		unit_assert(addr_in_common((struct sockaddr_storage*)&a4, 34,
			(struct sockaddr_storage*)&b4, 32, l4) == 32);
		for(i=0; i<=32; i++) {
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&a4, 32,
				(struct sockaddr_storage*)&b4, i, l4) == i);
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&a4, i,
				(struct sockaddr_storage*)&b4, 32, l4) == i);
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&a4, i,
				(struct sockaddr_storage*)&b4, i, l4) == i);
		}
		for(i=0; i<=32; i++) {
			memcpy(&a4.sin_addr, "\377\377\377\377", 4);
			memcpy(&b4.sin_addr, t4[i], 4);
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&a4, 32,
				(struct sockaddr_storage*)&b4, 32, l4) == i);
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&b4, 32,
				(struct sockaddr_storage*)&a4, 32, l4) == i);
		}
		memcpy(&a6.sin6_addr, "abcdefghabcdefgh", 16);
		memcpy(&b6.sin6_addr, "abcdefghabcdefgh", 16);
		unit_assert(addr_in_common((struct sockaddr_storage*)&a6, 128,
			(struct sockaddr_storage*)&b6, 128, l6) == 128);
		unit_assert(addr_in_common((struct sockaddr_storage*)&a6, 129,
			(struct sockaddr_storage*)&b6, 128, l6) == 128);
		for(i=0; i<=128; i++) {
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&a6, 128,
				(struct sockaddr_storage*)&b6, i, l6) == i);
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&a6, i,
				(struct sockaddr_storage*)&b6, 128, l6) == i);
			unit_assert(addr_in_common(
				(struct sockaddr_storage*)&a6, i,
				(struct sockaddr_storage*)&b6, i, l6) == i);
		}
	}
	/* test sockaddr_cmp_addr */
	unit_show_func("util/net_help.c", "sockaddr_cmp_addr");
	if(1) {
		struct sockaddr_storage a, b;
		socklen_t alen = (socklen_t)sizeof(a);
		socklen_t blen = (socklen_t)sizeof(b);
		unit_assert(ipstrtoaddr("127.0.0.0", 53, &a, &alen));
		unit_assert(ipstrtoaddr("127.255.255.255", 53, &b, &blen));
		unit_assert(sockaddr_cmp_addr(&a, alen, &b, blen) < 0);
		unit_assert(sockaddr_cmp_addr(&b, blen, &a, alen) > 0);
		unit_assert(sockaddr_cmp_addr(&a, alen, &a, alen) == 0);
		unit_assert(sockaddr_cmp_addr(&b, blen, &b, blen) == 0);
		unit_assert(ipstrtoaddr("192.168.121.5", 53, &a, &alen));
		unit_assert(sockaddr_cmp_addr(&a, alen, &b, blen) > 0);
		unit_assert(sockaddr_cmp_addr(&b, blen, &a, alen) < 0);
		unit_assert(sockaddr_cmp_addr(&a, alen, &a, alen) == 0);
		unit_assert(ipstrtoaddr("2001:3578:ffeb::99", 53, &b, &blen));
		unit_assert(sockaddr_cmp_addr(&b, blen, &b, blen) == 0);
		unit_assert(sockaddr_cmp_addr(&a, alen, &b, blen) < 0);
		unit_assert(sockaddr_cmp_addr(&b, blen, &a, alen) > 0);
	}
	/* test addr_is_ip4mapped */
	unit_show_func("util/net_help.c", "addr_is_ip4mapped");
	if(1) {
		struct sockaddr_storage a;
		socklen_t l = (socklen_t)sizeof(a);
		unit_assert(ipstrtoaddr("12.13.14.15", 53, &a, &l));
		unit_assert(!addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("fe80::217:31ff:fe91:df", 53, &a, &l));
		unit_assert(!addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("ffff::217:31ff:fe91:df", 53, &a, &l));
		unit_assert(!addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("::ffff:31ff:fe91:df", 53, &a, &l));
		unit_assert(!addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("::fffe:fe91:df", 53, &a, &l));
		unit_assert(!addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("::ffff:127.0.0.1", 53, &a, &l));
		unit_assert(addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("::ffff:127.0.0.2", 53, &a, &l));
		unit_assert(addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("::ffff:192.168.0.2", 53, &a, &l));
		unit_assert(addr_is_ip4mapped(&a, l));
		unit_assert(ipstrtoaddr("2::ffff:192.168.0.2", 53, &a, &l));
		unit_assert(!addr_is_ip4mapped(&a, l));
	}
	/* test addr_is_any */
	unit_show_func("util/net_help.c", "addr_is_any");
	if(1) {
		struct sockaddr_storage a;
		socklen_t l = (socklen_t)sizeof(a);
		unit_assert(ipstrtoaddr("0.0.0.0", 53, &a, &l));
		unit_assert(addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("0.0.0.0", 10053, &a, &l));
		unit_assert(addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("0.0.0.0", 0, &a, &l));
		unit_assert(addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("::0", 0, &a, &l));
		unit_assert(addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("::0", 53, &a, &l));
		unit_assert(addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("::1", 53, &a, &l));
		unit_assert(!addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("2001:1667::1", 0, &a, &l));
		unit_assert(!addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("2001::0", 0, &a, &l));
		unit_assert(!addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("10.0.0.0", 0, &a, &l));
		unit_assert(!addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("0.0.0.10", 0, &a, &l));
		unit_assert(!addr_is_any(&a, l));
		unit_assert(ipstrtoaddr("192.0.2.1", 0, &a, &l));
		unit_assert(!addr_is_any(&a, l));
	}
}

#include "util/config_file.h"
/** test config_file: cfg_parse_memsize */
static void
config_memsize_test(void) 
{
	size_t v = 0;
	unit_show_func("util/config_file.c", "cfg_parse_memsize");
	if(0) {
		/* these emit errors */
		unit_assert( cfg_parse_memsize("", &v) == 0);
		unit_assert( cfg_parse_memsize("bla", &v) == 0);
		unit_assert( cfg_parse_memsize("nop", &v) == 0);
		unit_assert( cfg_parse_memsize("n0b", &v) == 0);
		unit_assert( cfg_parse_memsize("gb", &v) == 0);
		unit_assert( cfg_parse_memsize("b", &v) == 0);
		unit_assert( cfg_parse_memsize("kb", &v) == 0);
		unit_assert( cfg_parse_memsize("kk kb", &v) == 0);
	}
	unit_assert( cfg_parse_memsize("0", &v) && v==0);
	unit_assert( cfg_parse_memsize("1", &v) && v==1);
	unit_assert( cfg_parse_memsize("10", &v) && v==10);
	unit_assert( cfg_parse_memsize("10b", &v) && v==10);
	unit_assert( cfg_parse_memsize("5b", &v) && v==5);
	unit_assert( cfg_parse_memsize("1024", &v) && v==1024);
	unit_assert( cfg_parse_memsize("1k", &v) && v==1024);
	unit_assert( cfg_parse_memsize("1K", &v) && v==1024);
	unit_assert( cfg_parse_memsize("1Kb", &v) && v==1024);
	unit_assert( cfg_parse_memsize("1kb", &v) && v==1024);
	unit_assert( cfg_parse_memsize("1 kb", &v) && v==1024);
	unit_assert( cfg_parse_memsize("10 kb", &v) && v==10240);
	unit_assert( cfg_parse_memsize("2k", &v) && v==2048);
	unit_assert( cfg_parse_memsize("2m", &v) && v==2048*1024);
	unit_assert( cfg_parse_memsize("3M", &v) && v==3072*1024);
	unit_assert( cfg_parse_memsize("40m", &v) && v==40960*1024);
	unit_assert( cfg_parse_memsize("1G", &v) && v==1024*1024*1024);
	unit_assert( cfg_parse_memsize("1 Gb", &v) && v==1024*1024*1024);
	unit_assert( cfg_parse_memsize("0 Gb", &v) && v==0*1024*1024);
}

/** test config_file: test tag code */
static void
config_tag_test(void) 
{
	unit_show_func("util/config_file.c", "taglist_intersect");
	unit_assert( taglist_intersect(
		(uint8_t*)"\000\000\000", 3, (uint8_t*)"\001\000\001", 3
		) == 0);
	unit_assert( taglist_intersect(
		(uint8_t*)"\000\000\001", 3, (uint8_t*)"\001\000\001", 3
		) == 1);
	unit_assert( taglist_intersect(
		(uint8_t*)"\001\000\000", 3, (uint8_t*)"\001\000\001", 3
		) == 1);
	unit_assert( taglist_intersect(
		(uint8_t*)"\001", 1, (uint8_t*)"\001\000\001", 3
		) == 1);
	unit_assert( taglist_intersect(
		(uint8_t*)"\001\000\001", 3, (uint8_t*)"\001", 1
		) == 1);
}
	
#include "util/rtt.h"
#include "util/timehist.h"
#include "iterator/iterator.h"
#include "libunbound/unbound.h"
/** test RTT code */
static void
rtt_test(void)
{
	int init = UNKNOWN_SERVER_NICENESS;
	int i;
	struct rtt_info r;
	unit_show_func("util/rtt.c", "rtt_timeout");
	rtt_init(&r);
	/* initial value sensible */
	unit_assert( rtt_timeout(&r) == init );
	rtt_lost(&r, init);
	unit_assert( rtt_timeout(&r) == init*2 );
	rtt_lost(&r, init*2);
	unit_assert( rtt_timeout(&r) == init*4 );
	rtt_update(&r, 4000);
	unit_assert( rtt_timeout(&r) >= 2000 );
	rtt_lost(&r, rtt_timeout(&r) );
	for(i=0; i<100; i++) {
		rtt_lost(&r, rtt_timeout(&r) ); 
		unit_assert( rtt_timeout(&r) > RTT_MIN_TIMEOUT-1);
		unit_assert( rtt_timeout(&r) < RTT_MAX_TIMEOUT+1);
	}
	/* must be the same, timehist bucket is used in stats */
	unit_assert(UB_STATS_BUCKET_NUM == NUM_BUCKETS_HIST);
}

#include "util/edns.h"
/* Complete version-invalid client cookie; needs a new one.
 * Based on edns_cookie_rfc9018_a2 */
static void
edns_cookie_invalid_version(void)
{
	uint32_t timestamp = 1559734385;
	uint8_t client_cookie[] = {
		0x24, 0x64, 0xc4, 0xab, 0xcf, 0x10, 0xc9, 0x57,
		0x99, 0x00, 0x00, 0x00,
		0x5c, 0xf7, 0x9f, 0x11,
		0x1f, 0x81, 0x30, 0xc3, 0xee, 0xe2, 0x94, 0x80 };
	uint8_t server_cookie[] = {
		0x24, 0x64, 0xc4, 0xab, 0xcf, 0x10, 0xc9, 0x57,
		0x01, 0x00, 0x00, 0x00,
		0x5c, 0xf7, 0xa8, 0x71,
		0xd4, 0xa5, 0x64, 0xa1, 0x44, 0x2a, 0xca, 0x77 };
	uint8_t server_secret[] = {
		0xe5, 0xe9, 0x73, 0xe5, 0xa6, 0xb2, 0xa4, 0x3f,
		0x48, 0xe7, 0xdc, 0x84, 0x9e, 0x37, 0xbf, 0xcf };
	uint8_t buf[32];
	/* copy client cookie|version|reserved|timestamp */
	memcpy(buf, client_cookie, 8 + 4 + 4);
	/* copy ip 198.51.100.100 */
	memcpy(buf + 16, "\306\063\144\144", 4);
	unit_assert(edns_cookie_server_validate(client_cookie,
		sizeof(client_cookie), server_secret, sizeof(server_secret), 1,
		buf, timestamp) == COOKIE_STATUS_INVALID);
	edns_cookie_server_write(buf, server_secret, 1, timestamp);
	unit_assert(memcmp(server_cookie, buf, 24) == 0);
}

/* Complete hash-invalid client cookie; needs a new one. */
static void
edns_cookie_invalid_hash(void)
{
	uint32_t timestamp = 0;
	uint8_t client_cookie[] = {
		0xfc, 0x93, 0xfc, 0x62, 0x80, 0x7d, 0xdb, 0x86,
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x32, 0xF2, 0x43, 0xB9, 0xBC, 0xFE, 0xC4, 0x06 };
	uint8_t server_cookie[] = {
		0xfc, 0x93, 0xfc, 0x62, 0x80, 0x7d, 0xdb, 0x86,
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0xBA, 0x0D, 0x82, 0x90, 0x8F, 0xAA, 0xEB, 0xBD };
	uint8_t server_secret[] = {
		0xe5, 0xe9, 0x73, 0xe5, 0xa6, 0xb2, 0xa4, 0x3f,
		0x48, 0xe7, 0xdc, 0x84, 0x9e, 0x37, 0xbf, 0xcf };
	uint8_t buf[32];
	/* copy client cookie|version|reserved|timestamp */
	memcpy(buf, client_cookie, 8 + 4 + 4);
	/* copy ip 203.0.113.203 */
	memcpy(buf + 16, "\313\000\161\313", 4);
	unit_assert(edns_cookie_server_validate(client_cookie,
		sizeof(client_cookie), server_secret, sizeof(server_secret), 1,
		buf, timestamp) == COOKIE_STATUS_INVALID);
	edns_cookie_server_write(buf, server_secret, 1, timestamp);
	unit_assert(memcmp(server_cookie, buf, 24) == 0);
}

/* Complete hash-valid client cookie; more than 30 minutes old; needs a
 * refreshed server cookie.
 * A slightly better variation of edns_cookie_rfc9018_a3 for Unbound to check
 * that RESERVED bits do not influence cookie validation. */
static void
edns_cookie_rfc9018_a3_better(void)
{
	uint32_t timestamp = 1800 + 1;
	uint8_t client_cookie[] = {
		0xfc, 0x93, 0xfc, 0x62, 0x80, 0x7d, 0xdb, 0x86,
		0x01, 0xab, 0xcd, 0xef,
		0x00, 0x00, 0x00, 0x00,
		0x32, 0xF2, 0x43, 0xB9, 0xBC, 0xFE, 0xC4, 0x06 };
	uint8_t server_cookie[] = {
		0xfc, 0x93, 0xfc, 0x62, 0x80, 0x7d, 0xdb, 0x86,
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x07, 0x09,
		0x62, 0xD5, 0x93, 0x09, 0x14, 0x5C, 0x23, 0x9D };
	uint8_t server_secret[] = {
		0xe5, 0xe9, 0x73, 0xe5, 0xa6, 0xb2, 0xa4, 0x3f,
		0x48, 0xe7, 0xdc, 0x84, 0x9e, 0x37, 0xbf, 0xcf };
	uint8_t buf[32];
	/* copy client cookie|version|reserved|timestamp */
	memcpy(buf, client_cookie, 8 + 4 + 4);
	/* copy ip 203.0.113.203 */
	memcpy(buf + 16, "\313\000\161\313", 4);
	unit_assert(edns_cookie_server_validate(client_cookie,
		sizeof(client_cookie), server_secret, sizeof(server_secret), 1,
		buf, timestamp) == COOKIE_STATUS_VALID_RENEW);
	edns_cookie_server_write(buf, server_secret, 1, timestamp);
	unit_assert(memcmp(server_cookie, buf, 24) == 0);
}

/* Complete hash-valid client cookie; more than 60 minutes old (expired);
 * needs a refreshed server cookie. */
static void
edns_cookie_rfc9018_a3(void)
{
	uint32_t timestamp = 1559734700;
	uint8_t client_cookie[] = {
		0xfc, 0x93, 0xfc, 0x62, 0x80, 0x7d, 0xdb, 0x86,
		0x01, 0xab, 0xcd, 0xef,
		0x5c, 0xf7, 0x8f, 0x71,
		0xa3, 0x14, 0x22, 0x7b, 0x66, 0x79, 0xeb, 0xf5 };
	uint8_t server_cookie[] = {
		0xfc, 0x93, 0xfc, 0x62, 0x80, 0x7d, 0xdb, 0x86,
		0x01, 0x00, 0x00, 0x00,
		0x5c, 0xf7, 0xa9, 0xac,
		0xf7, 0x3a, 0x78, 0x10, 0xac, 0xa2, 0x38, 0x1e };
	uint8_t server_secret[] = {
		0xe5, 0xe9, 0x73, 0xe5, 0xa6, 0xb2, 0xa4, 0x3f,
		0x48, 0xe7, 0xdc, 0x84, 0x9e, 0x37, 0xbf, 0xcf };
	uint8_t buf[32];
	/* copy client cookie|version|reserved|timestamp */
	memcpy(buf, client_cookie, 8 + 4 + 4);
	/* copy ip 203.0.113.203 */
	memcpy(buf + 16, "\313\000\161\313", 4);
	unit_assert(edns_cookie_server_validate(client_cookie,
		sizeof(client_cookie), server_secret, sizeof(server_secret), 1,
		buf, timestamp) == COOKIE_STATUS_EXPIRED);
	edns_cookie_server_write(buf, server_secret, 1, timestamp);
	unit_assert(memcmp(server_cookie, buf, 24) == 0);
}

/* Complete hash-valid client cookie; more than 30 minutes old; needs a
 * refreshed server cookie. */
static void
edns_cookie_rfc9018_a2(void)
{
	uint32_t timestamp = 1559734385;
	uint8_t client_cookie[] = {
		0x24, 0x64, 0xc4, 0xab, 0xcf, 0x10, 0xc9, 0x57,
		0x01, 0x00, 0x00, 0x00,
		0x5c, 0xf7, 0x9f, 0x11,
		0x1f, 0x81, 0x30, 0xc3, 0xee, 0xe2, 0x94, 0x80 };
	uint8_t server_cookie[] = {
		0x24, 0x64, 0xc4, 0xab, 0xcf, 0x10, 0xc9, 0x57,
		0x01, 0x00, 0x00, 0x00,
		0x5c, 0xf7, 0xa8, 0x71,
		0xd4, 0xa5, 0x64, 0xa1, 0x44, 0x2a, 0xca, 0x77 };
	uint8_t server_secret[] = {
		0xe5, 0xe9, 0x73, 0xe5, 0xa6, 0xb2, 0xa4, 0x3f,
		0x48, 0xe7, 0xdc, 0x84, 0x9e, 0x37, 0xbf, 0xcf };
	uint8_t buf[32];
	/* copy client cookie|version|reserved|timestamp */
	memcpy(buf, client_cookie, 8 + 4 + 4);
	/* copy ip 198.51.100.100 */
	memcpy(buf + 16, "\306\063\144\144", 4);
	unit_assert(edns_cookie_server_validate(client_cookie,
		sizeof(client_cookie), server_secret, sizeof(server_secret), 1,
		buf, timestamp) == COOKIE_STATUS_VALID_RENEW);
	edns_cookie_server_write(buf, server_secret, 1, timestamp);
	unit_assert(memcmp(server_cookie, buf, 24) == 0);
}

/* Only client cookie; needs a complete server cookie. */
static void
edns_cookie_rfc9018_a1(void)
{
	uint32_t timestamp = 1559731985;
	uint8_t client_cookie[] = {
		0x24, 0x64, 0xc4, 0xab, 0xcf, 0x10, 0xc9, 0x57 };
	uint8_t server_cookie[] = {
		0x24, 0x64, 0xc4, 0xab, 0xcf, 0x10, 0xc9, 0x57,
		0x01, 0x00, 0x00, 0x00,
		0x5c, 0xf7, 0x9f, 0x11,
		0x1f, 0x81, 0x30, 0xc3, 0xee, 0xe2, 0x94, 0x80 };
	uint8_t server_secret[] = {
		0xe5, 0xe9, 0x73, 0xe5, 0xa6, 0xb2, 0xa4, 0x3f,
		0x48, 0xe7, 0xdc, 0x84, 0x9e, 0x37, 0xbf, 0xcf };
	uint8_t buf[32];
	/* copy client cookie|version|reserved|timestamp */
	memcpy(buf, server_cookie, 8 + 4 + 4);
	/* copy ip 198.51.100.100 */
	memcpy(buf + 16, "\306\063\144\144", 4);
	unit_assert(edns_cookie_server_validate(client_cookie,
		sizeof(client_cookie),
		/* these will not be used; it will return invalid
		 * because of the size. */
		NULL, 0, 1, NULL, 0) == COOKIE_STATUS_CLIENT_ONLY);
	edns_cookie_server_write(buf, server_secret, 1, timestamp);
	unit_assert(memcmp(server_cookie, buf, 24) == 0);
}

/** test interoperable DNS cookies (RFC9018) */
static void
edns_cookie_test(void)
{
	unit_show_feature("interoperable dns cookies");
	/* Check RFC9018 appendix test vectors */
	edns_cookie_rfc9018_a1();
	edns_cookie_rfc9018_a2();
	edns_cookie_rfc9018_a3();
	/* More tests */
	edns_cookie_rfc9018_a3_better();
	edns_cookie_invalid_hash();
	edns_cookie_invalid_version();
}

#include "util/random.h"
/** test randomness */
static void
rnd_test(void)
{
	struct ub_randstate* r;
	int num = 1000, i;
	long int a[1000];
	unit_show_feature("ub_random");
	unit_assert( (r = ub_initstate(NULL)) );
	for(i=0; i<num; i++) {
		a[i] = ub_random(r);
		unit_assert(a[i] >= 0);
		unit_assert((size_t)a[i] <= (size_t)0x7fffffff);
		if(i > 5)
			unit_assert(a[i] != a[i-1] || a[i] != a[i-2] ||
				a[i] != a[i-3] || a[i] != a[i-4] ||
				a[i] != a[i-5] || a[i] != a[i-6]);
	}
	a[0] = ub_random_max(r, 1);
	unit_assert(a[0] >= 0 && a[0] < 1);
	a[0] = ub_random_max(r, 10000);
	unit_assert(a[0] >= 0 && a[0] < 10000);
	for(i=0; i<num; i++) {
		a[i] = ub_random_max(r, 10);
		unit_assert(a[i] >= 0 && a[i] < 10);
	}
	ub_randfree(r);
}

#include "respip/respip.h"
#include "services/localzone.h"
#include "util/data/packed_rrset.h"
typedef struct addr_action {char* ip; char* sact; enum respip_action act;}
	addr_action_t;

/** Utility function that verifies that the respip set has actions as expected */
static void
verify_respip_set_actions(struct respip_set* set, addr_action_t actions[],
	int actions_len)
{
	int i = 0;
	struct rbtree_type* tree = respip_set_get_tree(set);
	for (i=0; i<actions_len; i++) {
		struct sockaddr_storage addr;
		int net;
		socklen_t addrlen;
		struct resp_addr* node;
		netblockstrtoaddr(actions[i].ip, UNBOUND_DNS_PORT, &addr,
			&addrlen, &net);
		node = (struct resp_addr*)addr_tree_find(tree, &addr, addrlen, net);

		/** we have the node and the node has the correct action
		  * and has no data */
		unit_assert(node);
		unit_assert(actions[i].act ==
			resp_addr_get_action(node));
		unit_assert(resp_addr_get_rrset(node) == NULL);
	}
	unit_assert(actions_len && i == actions_len);
	unit_assert(actions_len == (int)tree->count);
}

/** Global respip actions test; apply raw config data and verify that
  * all the nodes in the respip set, looked up by address, have expected
  * actions */
static void
respip_conf_actions_test(void)
{
	addr_action_t config_response_ip[] = {
		{"192.0.1.0/24", "deny", respip_deny},
		{"192.0.2.0/24", "redirect", respip_redirect},
		{"192.0.3.0/26", "inform", respip_inform},
		{"192.0.4.0/27", "inform_deny", respip_inform_deny},
		{"2001:db8:1::/48", "always_transparent", respip_always_transparent},
		{"2001:db8:2::/49", "always_refuse", respip_always_refuse},
		{"2001:db8:3::/50", "always_nxdomain", respip_always_nxdomain},
	};
	int i;
	struct respip_set* set = respip_set_create();
	struct config_file cfg;
	int clen = (int)(sizeof(config_response_ip) / sizeof(addr_action_t));

	unit_assert(set);
	unit_show_feature("global respip config actions apply");
	memset(&cfg, 0, sizeof(cfg));
	for(i=0; i<clen; i++) {
		char* ip = strdup(config_response_ip[i].ip);
		char* sact = strdup(config_response_ip[i].sact);
		unit_assert(ip && sact);
		if(!cfg_str2list_insert(&cfg.respip_actions, ip, sact))
			unit_assert(0);
	}
	unit_assert(respip_global_apply_cfg(set, &cfg));
	verify_respip_set_actions(set, config_response_ip, clen);

	respip_set_delete(set);
	config_deldblstrlist(cfg.respip_actions);
}

/** Per-view respip actions test; apply raw configuration with two views
  * and verify that actions are as expected in respip sets of both views */
static void
respip_view_conf_actions_test(void)
{
	addr_action_t config_response_ip_view1[] = {
		{"192.0.1.0/24", "deny", respip_deny},
		{"192.0.2.0/24", "redirect", respip_redirect},
		{"192.0.3.0/26", "inform", respip_inform},
		{"192.0.4.0/27", "inform_deny", respip_inform_deny},
	};
	addr_action_t config_response_ip_view2[] = {
		{"2001:db8:1::/48", "always_transparent", respip_always_transparent},
		{"2001:db8:2::/49", "always_refuse", respip_always_refuse},
		{"2001:db8:3::/50", "always_nxdomain", respip_always_nxdomain},
	};
	int i;
	struct config_file cfg;
	int clen1 = (int)(sizeof(config_response_ip_view1) / sizeof(addr_action_t));
	int clen2 = (int)(sizeof(config_response_ip_view2) / sizeof(addr_action_t));
	struct config_view* cv1;
	struct config_view* cv2;
	int have_respip_cfg = 0;
	struct views* views = NULL;
	struct view* v = NULL;

	unit_show_feature("per-view respip config actions apply");
	memset(&cfg, 0, sizeof(cfg));
	cv1 = (struct config_view*)calloc(1, sizeof(struct config_view));
	cv2 = (struct config_view*)calloc(1, sizeof(struct config_view));
	unit_assert(cv1 && cv2);
	cv1->name = strdup("view1");
	cv2->name = strdup("view2");
	unit_assert(cv1->name && cv2->name);
	cv1->next = cv2;
	cfg.views = cv1;

	for(i=0; i<clen1; i++) {
		char* ip = strdup(config_response_ip_view1[i].ip);
		char* sact = strdup(config_response_ip_view1[i].sact);
		unit_assert(ip && sact);
		if(!cfg_str2list_insert(&cv1->respip_actions, ip, sact))
			unit_assert(0);
	}
	for(i=0; i<clen2; i++) {
		char* ip = strdup(config_response_ip_view2[i].ip);
		char* sact = strdup(config_response_ip_view2[i].sact);
		unit_assert(ip && sact);
		if(!cfg_str2list_insert(&cv2->respip_actions, ip, sact))
			unit_assert(0);
	}
	views = views_create();
	unit_assert(views);
	unit_assert(views_apply_cfg(views, &cfg));
	unit_assert(respip_views_apply_cfg(views, &cfg, &have_respip_cfg));

	/* now verify the respip sets in each view */
	v = views_find_view(views, "view1", 0);
	unit_assert(v);
	verify_respip_set_actions(v->respip_set, config_response_ip_view1, clen1);
	lock_rw_unlock(&v->lock);
	v = views_find_view(views, "view2", 0);
	unit_assert(v);
	verify_respip_set_actions(v->respip_set, config_response_ip_view2, clen2);
	lock_rw_unlock(&v->lock);

	views_delete(views);
	free(cv1->name);
	free(cv1);
	free(cv2->name);
	free(cv2);
}

typedef struct addr_data {char* ip; char* data;} addr_data_t;

/** find the respip address node in the specified tree (by address lookup)
  * and verify type and address of the specified rdata (by index) in this
  * node's rrset */
static void
verify_rrset(struct respip_set* set, const char* ipstr,
	const char* rdatastr, size_t rdi, uint16_t type)
{
	struct sockaddr_storage addr;
	int net;
	char buf[65536];
	socklen_t addrlen;
	struct rbtree_type* tree;
	struct resp_addr* node;
	const struct ub_packed_rrset_key* rrs;

	netblockstrtoaddr(ipstr, UNBOUND_DNS_PORT, &addr, &addrlen, &net);
	tree = respip_set_get_tree(set);
	node = (struct resp_addr*)addr_tree_find(tree, &addr, addrlen, net);
	unit_assert(node);
	unit_assert((rrs = resp_addr_get_rrset(node)));
	unit_assert(ntohs(rrs->rk.type) == type);
	packed_rr_to_string((struct ub_packed_rrset_key*)rrs,
		rdi, 0, buf, sizeof(buf));
	unit_assert(strstr(buf, rdatastr));
}

/** Dataset used to test redirect rrset initialization for both
  * global and per-view respip redirect configuration */
static addr_data_t config_response_ip_data[] = {
	{"192.0.1.0/24", "A 1.2.3.4"},
	{"192.0.1.0/24", "A 11.12.13.14"},
	{"192.0.2.0/24", "CNAME www.example.com."},
	{"2001:db8:1::/48", "AAAA 2001:db8:1::2:1"},
};

/** Populate raw respip redirect config data, used for both global and
  * view-based respip redirect test case */
static void
cfg_insert_respip_data(struct config_str2list** respip_actions,
	struct config_str2list** respip_data)
{
	int clen = (int)(sizeof(config_response_ip_data) / sizeof(addr_data_t));
	int i = 0;

	/* insert actions (duplicate netblocks don't matter) */
	for(i=0; i<clen; i++) {
		char* ip = strdup(config_response_ip_data[i].ip);
		char* sact = strdup("redirect");
		unit_assert(ip && sact);
		if(!cfg_str2list_insert(respip_actions, ip, sact))
			unit_assert(0);
	}
	/* insert data */
	for(i=0; i<clen; i++) {
		char* ip = strdup(config_response_ip_data[i].ip);
		char* data = strdup(config_response_ip_data[i].data);
		unit_assert(ip && data);
		if(!cfg_str2list_insert(respip_data, ip, data))
			unit_assert(0);
	}
}

/** Test global respip redirect w/ data directives */
static void
respip_conf_data_test(void)
{
	struct respip_set* set = respip_set_create();
	struct config_file cfg;

	unit_show_feature("global respip config data apply");
	memset(&cfg, 0, sizeof(cfg));

	cfg_insert_respip_data(&cfg.respip_actions, &cfg.respip_data);

	/* apply configuration and verify rrsets */
	unit_assert(respip_global_apply_cfg(set, &cfg));
	verify_rrset(set, "192.0.1.0/24", "1.2.3.4", 0, LDNS_RR_TYPE_A);
	verify_rrset(set, "192.0.1.0/24", "11.12.13.14", 1, LDNS_RR_TYPE_A);
	verify_rrset(set, "192.0.2.0/24", "www.example.com", 0, LDNS_RR_TYPE_CNAME);
	verify_rrset(set, "2001:db8:1::/48", "2001:db8:1::2:1", 0, LDNS_RR_TYPE_AAAA);

	respip_set_delete(set);
}

/** Test per-view respip redirect w/ data directives */
static void
respip_view_conf_data_test(void)
{
	struct config_file cfg;
	struct config_view* cv;
	int have_respip_cfg = 0;
	struct views* views = NULL;
	struct view* v = NULL;

	unit_show_feature("per-view respip config data apply");
	memset(&cfg, 0, sizeof(cfg));
	cv = (struct config_view*)calloc(1, sizeof(struct config_view));
	unit_assert(cv);
	cv->name = strdup("view1");
	unit_assert(cv->name);
	cfg.views = cv;
	cfg_insert_respip_data(&cv->respip_actions, &cv->respip_data);
	views = views_create();
	unit_assert(views);
	unit_assert(views_apply_cfg(views, &cfg));

	/* apply configuration and verify rrsets */
	unit_assert(respip_views_apply_cfg(views, &cfg, &have_respip_cfg));
	v = views_find_view(views, "view1", 0);
	unit_assert(v);
	verify_rrset(v->respip_set, "192.0.1.0/24", "1.2.3.4",
		0, LDNS_RR_TYPE_A);
	verify_rrset(v->respip_set, "192.0.1.0/24", "11.12.13.14",
		1, LDNS_RR_TYPE_A);
	verify_rrset(v->respip_set, "192.0.2.0/24", "www.example.com",
		0, LDNS_RR_TYPE_CNAME);
	verify_rrset(v->respip_set, "2001:db8:1::/48", "2001:db8:1::2:1",
		0, LDNS_RR_TYPE_AAAA);
	lock_rw_unlock(&v->lock);

	views_delete(views);
	free(cv->name);
	free(cv);
}

/** respip unit tests */
static void respip_test(void)
{
	respip_view_conf_data_test();
	respip_conf_data_test();
	respip_view_conf_actions_test();
	respip_conf_actions_test();
}

#include "util/regional.h"
#include "sldns/sbuffer.h"
#include "util/data/dname.h"
#include "util/data/msgreply.h"
#include "util/data/msgencode.h"
#include "sldns/str2wire.h"

static void edns_ede_encode_setup(struct edns_data* edns,
	struct regional* region)
{
	memset(edns, 0, sizeof(*edns));
	edns->edns_present = 1;
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->bits &= EDNS_DO;
	/* Fill up opt_list_out with EDEs */
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_out, region,
		LDNS_EDE_BLOCKED, "Too long blocked text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_out, region,
		LDNS_EDE_BLOCKED, "Too long blocked text"));
	/* Fill up opt_list_inplace_cb_out with EDEs */
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_inplace_cb_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_inplace_cb_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_inplace_cb_out, region,
		LDNS_EDE_BLOCKED, "Too long blocked text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_inplace_cb_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_inplace_cb_out, region,
		LDNS_EDE_BLOCKED, "Too long blocked text"));
	/* append another EDNS option to both lists */
	unit_assert(
		edns_opt_list_append(&edns->opt_list_out,
		LDNS_EDNS_UNBOUND_CACHEDB_TESTFRAME_TEST, 0, NULL, region));
	unit_assert(
		edns_opt_list_append(&edns->opt_list_inplace_cb_out,
		LDNS_EDNS_UNBOUND_CACHEDB_TESTFRAME_TEST, 0, NULL, region));
	/* append LDNS_EDE_OTHER at the end of both lists */
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
	unit_assert(
		edns_opt_list_append_ede(&edns->opt_list_inplace_cb_out, region,
		LDNS_EDE_OTHER, "Too long other text"));
}

static void edns_ede_encode_encodedecode(struct query_info* qinfo,
	struct reply_info* rep, struct regional* region,
	struct edns_data* edns, sldns_buffer* pkt)
{
	/* encode */
	unit_assert(
		reply_info_answer_encode(qinfo, rep, 1, rep->flags, pkt,
		0, 0, region, 65535, edns, 0, 0));
	/* buffer ready for reading; skip after the question section */
	sldns_buffer_skip(pkt, LDNS_HEADER_SIZE);
	(void)query_dname_len(pkt);
	sldns_buffer_skip(pkt, 2 + 2);
	/* decode */
	unit_assert(parse_edns_from_query_pkt(pkt, edns, NULL, NULL, NULL, 0,
		region, NULL) == 0);
}

static void edns_ede_encode_check(struct edns_data* edns, int* found_ede,
	int* found_ede_other, int* found_ede_txt, int* found_other_edns)
{
	struct edns_option* opt;
	for(opt = edns->opt_list_in; opt; opt = opt->next) {
		if(opt->opt_code == LDNS_EDNS_EDE) {
			(*found_ede)++;
			if(opt->opt_len > 2)
				(*found_ede_txt)++;
			if(opt->opt_len >= 2 && sldns_read_uint16(
				opt->opt_data) == LDNS_EDE_OTHER)
				(*found_ede_other)++;
		} else {
			(*found_other_edns)++;
		}
	}

}

static void edns_ede_encode_fit_test(struct query_info* qinfo,
	struct reply_info* rep, struct regional* region)
{
	struct edns_data edns;
	int found_ede = 0, found_ede_other = 0, found_ede_txt = 0;
	int found_other_edns = 0;
	sldns_buffer* pkt = sldns_buffer_new(65535);
	unit_assert(pkt);
	edns_ede_encode_setup(&edns, region);
	/* leave the pkt buffer as is; everything should fit */
	edns_ede_encode_encodedecode(qinfo, rep, region, &edns, pkt);
	edns_ede_encode_check(&edns, &found_ede, &found_ede_other,
		&found_ede_txt, &found_other_edns);
	unit_assert(found_ede == 12);
	unit_assert(found_ede_other == 8);
	unit_assert(found_ede_txt == 12);
	unit_assert(found_other_edns == 2);
	/* cleanup */
	sldns_buffer_free(pkt);
}

static void edns_ede_encode_notxt_fit_test( struct query_info* qinfo,
	struct reply_info* rep, struct regional* region)
{
	struct edns_data edns;
	sldns_buffer* pkt;
	uint16_t edns_field_size, ede_txt_size;
	int found_ede = 0, found_ede_other = 0, found_ede_txt = 0;
	int found_other_edns = 0;
	edns_ede_encode_setup(&edns, region);
	/* pkt buffer should fit everything if the ede txt is cropped.
	 * OTHER EDE should not be there since it is useless without text. */
	edns_field_size = calc_edns_field_size(&edns);
	(void)calc_ede_option_size(&edns, &ede_txt_size);
	pkt = sldns_buffer_new(LDNS_HEADER_SIZE
		+ qinfo->qname_len
		+ 2 + 2 /* qtype + qclass */
		+ 11 /* opt record */
		+ edns_field_size
		- ede_txt_size);
	unit_assert(pkt);
	edns_ede_encode_encodedecode(qinfo, rep, region, &edns, pkt);
	edns_ede_encode_check(&edns, &found_ede, &found_ede_other,
		&found_ede_txt, &found_other_edns);
	unit_assert(found_ede == 4);
	unit_assert(found_ede_other == 0);
	unit_assert(found_ede_txt == 0);
	unit_assert(found_other_edns == 2);
	/* cleanup */
	sldns_buffer_free(pkt);
}

static void edns_ede_encode_no_fit_test( struct query_info* qinfo,
	struct reply_info* rep, struct regional* region)
{
	struct edns_data edns;
	sldns_buffer* pkt;
	uint16_t edns_field_size, ede_size, ede_txt_size;
	int found_ede = 0, found_ede_other = 0, found_ede_txt = 0;
	int found_other_edns = 0;
	edns_ede_encode_setup(&edns, region);
	/* pkt buffer should fit only non-EDE options. */
	edns_field_size = calc_edns_field_size(&edns);
	ede_size = calc_ede_option_size(&edns, &ede_txt_size);
	pkt = sldns_buffer_new(LDNS_HEADER_SIZE
		+ qinfo->qname_len
		+ 2 + 2 /* qtype + qclass */
		+ 11 /* opt record */
		+ edns_field_size
		- ede_size);
	unit_assert(pkt);
	edns_ede_encode_encodedecode(qinfo, rep, region, &edns, pkt);
	edns_ede_encode_check(&edns, &found_ede, &found_ede_other,
		&found_ede_txt, &found_other_edns);
	unit_assert(found_ede == 0);
	unit_assert(found_ede_other == 0);
	unit_assert(found_ede_txt == 0);
	unit_assert(found_other_edns == 2);
	/* cleanup */
	sldns_buffer_free(pkt);
}

/** test optional EDE encoding with various buffer
 *  available sizes */
static void edns_ede_answer_encode_test(void)
{
	struct regional* region = regional_create();
	struct reply_info* rep;
	struct query_info qinfo;
	unit_show_feature("edns ede optional encoding");
	unit_assert(region);
	rep = construct_reply_info_base(region,
		LDNS_RCODE_NOERROR | BIT_QR, 1,
		3600, 3600, 3600, 0,
		0, 0, 0, 0,
		sec_status_unchecked, LDNS_EDE_NONE);
	unit_assert(rep);
	memset(&qinfo, 0, sizeof(qinfo));
	qinfo.qname = sldns_str2wire_dname("encode.ede.", &qinfo.qname_len);
	unit_assert(qinfo.qname);
	qinfo.qtype = LDNS_RR_TYPE_TXT;
	qinfo.qclass = LDNS_RR_CLASS_IN;

	edns_ede_encode_fit_test(&qinfo, rep, region);
	edns_ede_encode_notxt_fit_test(&qinfo, rep, region);
	edns_ede_encode_no_fit_test(&qinfo, rep, region);

	/* cleanup */
	free(qinfo.qname);
	regional_free_all(region);
	regional_destroy(region);
}

#include "services/localzone.h"
/* Utility function that compares two localzone trees */
static void compare_localzone_trees(struct local_zones* z1,
	struct local_zones* z2)
{
	struct local_zone *node1, *node2;
	lock_rw_rdlock(&z1->lock);
	lock_rw_rdlock(&z2->lock);
	/* size should be the same */
	unit_assert(z1->ztree.count == z2->ztree.count);
	for(node1=(struct local_zone*)rbtree_first(&z1->ztree),
		node2=(struct local_zone*)rbtree_first(&z2->ztree);
		(rbnode_type*)node1 != RBTREE_NULL &&
		(rbnode_type*)node2 != RBTREE_NULL;
		node1=(struct local_zone*)rbtree_next((rbnode_type*)node1),
		node2=(struct local_zone*)rbtree_next((rbnode_type*)node2)) {
		int labs;
		/* the same zone should be at the same nodes */
		unit_assert(!dname_lab_cmp(
			node1->name, node1->namelabs,
			node2->name, node2->namelabs,
			&labs));
		/* the zone's parent should be the same on both nodes */
		unit_assert(
			(node1->parent == NULL && node2->parent == NULL) ||
			(node1->parent != NULL && node2->parent != NULL));
		if(node1->parent) {
			unit_assert(!dname_lab_cmp(
				node1->parent->name, node1->parent->namelabs,
				node2->parent->name, node2->parent->namelabs,
				&labs));
		}
	}
	lock_rw_unlock(&z1->lock);
	lock_rw_unlock(&z2->lock);
}

/* test that zone addition results in the same tree from both the configuration
 * file and the unbound-control commands */
static void localzone_parents_test(void)
{
	struct local_zones *z1, *z2;
	size_t i;
	char* zone_data[] = {
		"one",
		"a.b.c.one",
		"b.c.one",
		"c.one",
		"two",
		"c.two",
		"b.c.two",
		"a.b.c.two",
		"a.b.c.three",
		"b.c.three",
		"c.three",
		"three",
		"c.four",
		"b.c.four",
		"a.b.c.four",
		"four",
		"."
	};
	unit_show_feature("localzones parent calculation");
	z1 = local_zones_create();
	z2 = local_zones_create();
	/* parse test data */
	for(i=0; i<sizeof(zone_data)/sizeof(zone_data[0]); i++) {
		uint8_t* nm;
		int nmlabs;
		size_t nmlen;
		struct local_zone* z;

		/* This is the config way */
		z = lz_enter_zone(z1, zone_data[i], "always_nxdomain",
			LDNS_RR_CLASS_IN);
		(void)z;  /* please compiler when no threading and no lock
		code; the following line disappears and z stays unused */
		lock_rw_unlock(&z->lock);
		lz_init_parents(z1);

		/* This is the unbound-control way */
		nm = sldns_str2wire_dname(zone_data[i], &nmlen);
		if(!nm) unit_assert(0);
		nmlabs = dname_count_size_labels(nm, &nmlen);
		lock_rw_wrlock(&z2->lock);
		local_zones_add_zone(z2, nm, nmlen, nmlabs, LDNS_RR_CLASS_IN,
			local_zone_always_nxdomain);
		lock_rw_unlock(&z2->lock);
	}
	/* The trees should be the same, iterate and check the nodes */
	compare_localzone_trees(z1, z2);

	/* cleanup */
	local_zones_delete(z1);
	local_zones_delete(z2);
}

/** localzone unit tests */
static void localzone_test(void)
{
	localzone_parents_test();
}

void unit_show_func(const char* file, const char* func)
{
	printf("test %s:%s\n", file, func);
}

void unit_show_feature(const char* feature)
{
	printf("test %s functions\n", feature);
}

#ifdef USE_ECDSA_EVP_WORKAROUND
void ecdsa_evp_workaround_init(void);
#endif

/**
 * Main unit test program. Setup, teardown and report errors.
 * @param argc: arg count.
 * @param argv: array of commandline arguments.
 * @return program failure if test fails.
 */
int 
main(int argc, char* argv[])
{
	checklock_start();
	log_init(NULL, 0, NULL);
	if(argc != 1) {
		printf("usage: %s\n", argv[0]);
		printf("\tperforms unit tests.\n");
		return 1;
	}
	/* Disable roundrobin for the unit tests */
	RRSET_ROUNDROBIN = 0;
#ifdef USE_LIBEVENT
	printf("Start of %s+libevent unit test.\n", PACKAGE_STRING);
#else
	printf("Start of %s unit test.\n", PACKAGE_STRING);
#endif
#ifdef HAVE_SSL
#  ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#  endif
#  ifdef USE_GOST
	(void)sldns_key_EVP_load_gost_id();
#  endif
#  ifdef USE_ECDSA_EVP_WORKAROUND
	ecdsa_evp_workaround_init();
#  endif
#elif defined(HAVE_NSS)
	if(NSS_NoDB_Init(".") != SECSuccess)
		fatal_exit("could not init NSS");
#endif /* HAVE_SSL or HAVE_NSS*/
	authzone_test();
	neg_test();
	rnd_test();
	respip_test();
	verify_test();
	net_test();
	config_memsize_test();
	config_tag_test();
	dname_test();
	rtt_test();
	anchors_test();
	alloc_test();
	regional_test();
	lruhash_test();
	slabhash_test();
	infra_test();
	ldns_test();
	edns_cookie_test();
	zonemd_test();
	tcpreuse_test();
	msgparse_test();
	edns_ede_answer_encode_test();
	localzone_test();
#ifdef CLIENT_SUBNET
	ecs_test();
#endif /* CLIENT_SUBNET */
#ifdef HAVE_NGTCP2
	doq_test();
#endif /* HAVE_NGTCP2 */
	if(log_get_lock()) {
		lock_basic_destroy((lock_basic_type*)log_get_lock());
	}
	checklock_stop();
	printf("%d checks ok.\n", testcount);
#ifdef HAVE_SSL
#  if defined(USE_GOST)
	sldns_key_EVP_unload_gost();
#  endif
#  ifdef HAVE_OPENSSL_CONFIG
#  ifdef HAVE_EVP_CLEANUP
	EVP_cleanup();
#  endif
#  if (OPENSSL_VERSION_NUMBER < 0x10100000) && !defined(OPENSSL_NO_ENGINE) && defined(HAVE_ENGINE_CLEANUP)
	ENGINE_cleanup();
#  endif
	CONF_modules_free();
#  endif
#  ifdef HAVE_CRYPTO_CLEANUP_ALL_EX_DATA
	CRYPTO_cleanup_all_ex_data();
#  endif
#  ifdef HAVE_ERR_FREE_STRINGS
	ERR_free_strings();
#  endif
#  ifdef HAVE_RAND_CLEANUP
	RAND_cleanup();
#  endif
#elif defined(HAVE_NSS)
	if(NSS_Shutdown() != SECSuccess)
		fatal_exit("could not shutdown NSS");
#endif /* HAVE_SSL or HAVE_NSS */
#ifdef HAVE_PTHREAD
	/* dlopen frees its thread specific state */
	pthread_exit(NULL);
#endif
	return 0;
}
