/*	$KAME: ipsec.c,v 1.33 2003/07/25 09:54:32 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005 NTT Multimedia Communications Laboratories, Inc.
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
 */
/*-
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
static char sccsid[] = "@(#)inet.c	8.5 (Berkeley) 5/24/95";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <netinet/in.h>

#ifdef IPSEC
#include <netipsec/ipsec.h>
#include <netipsec/ah_var.h>
#include <netipsec/esp_var.h>
#include <netipsec/ipcomp_var.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libxo/xo.h>
#include "netstat.h"

#ifdef IPSEC
struct val2str {
	int val;
	const char *str;
};

static struct val2str ipsec_ahnames[] = {
	{ SADB_AALG_NONE, "none", },
	{ SADB_AALG_MD5HMAC, "hmac-md5", },
	{ SADB_AALG_SHA1HMAC, "hmac-sha1", },
	{ SADB_X_AALG_MD5, "md5", },
	{ SADB_X_AALG_SHA, "sha", },
	{ SADB_X_AALG_NULL, "null", },
#ifdef SADB_X_AALG_SHA2_256
	{ SADB_X_AALG_SHA2_256, "hmac-sha2-256", },
#endif
#ifdef SADB_X_AALG_SHA2_384
	{ SADB_X_AALG_SHA2_384, "hmac-sha2-384", },
#endif
#ifdef SADB_X_AALG_SHA2_512
	{ SADB_X_AALG_SHA2_512, "hmac-sha2-512", },
#endif
#ifdef SADB_X_AALG_RIPEMD160HMAC
	{ SADB_X_AALG_RIPEMD160HMAC, "hmac-ripemd160", },
#endif
#ifdef SADB_X_AALG_AES_XCBC_MAC
	{ SADB_X_AALG_AES_XCBC_MAC, "aes-xcbc-mac", },
#endif
#ifdef SADB_X_AALG_AES128GMAC
	{ SADB_X_AALG_AES128GMAC, "aes-gmac-128", },
#endif
#ifdef SADB_X_AALG_AES192GMAC
	{ SADB_X_AALG_AES192GMAC, "aes-gmac-192", },
#endif
#ifdef SADB_X_AALG_AES256GMAC
	{ SADB_X_AALG_AES256GMAC, "aes-gmac-256", },
#endif
	{ -1, NULL },
};

static struct val2str ipsec_espnames[] = {
	{ SADB_EALG_NONE, "none", },
	{ SADB_EALG_DESCBC, "des-cbc", },
	{ SADB_EALG_3DESCBC, "3des-cbc", },
	{ SADB_EALG_NULL, "null", },
	{ SADB_X_EALG_CAST128CBC, "cast128-cbc", },
	{ SADB_X_EALG_BLOWFISHCBC, "blowfish-cbc", },
#ifdef SADB_X_EALG_RIJNDAELCBC
	{ SADB_X_EALG_RIJNDAELCBC, "rijndael-cbc", },
#endif
#ifdef SADB_X_EALG_AESCTR
	{ SADB_X_EALG_AESCTR, "aes-ctr", },
#endif
#ifdef SADB_X_EALG_AESGCM16
	{ SADB_X_EALG_AESGCM16, "aes-gcm-16", },
#endif
	{ -1, NULL },
};

static struct val2str ipsec_compnames[] = {
	{ SADB_X_CALG_NONE, "none", },
	{ SADB_X_CALG_OUI, "oui", },
	{ SADB_X_CALG_DEFLATE, "deflate", },
	{ SADB_X_CALG_LZS, "lzs", },
	{ -1, NULL },
};

static void print_ipsecstats(const struct ipsecstat *ipsecstat);

static void
print_ipsecstats(const struct ipsecstat *ipsecstat)
{
	xo_open_container("ipsec-statistics");

#define	p(f, m) if (ipsecstat->f || sflag <= 1) \
	xo_emit(m, (uintmax_t)ipsecstat->f, plural(ipsecstat->f))
#define	p2(f, m) if (ipsecstat->f || sflag <= 1) \
	xo_emit(m, (uintmax_t)ipsecstat->f, plurales(ipsecstat->f))

	p(ips_in_polvio, "\t{:dropped-policy-violation/%ju} "
	    "{N:/inbound packet%s violated process security policy}\n");
	p(ips_in_nomem, "\t{:dropped-no-memory/%ju} "
	    "{N:/inbound packet%s failed due to insufficient memory}\n");
	p(ips_in_inval, "\t{:dropped-invalid/%ju} "
	    "{N:/invalid inbound packet%s}\n");
	p(ips_out_polvio, "\t{:discarded-policy-violation/%ju} "
	    "{N:/outbound packet%s violated process security policy}\n");
	p(ips_out_nosa, "\t{:discarded-no-sa/%ju} "
	    "{N:/outbound packet%s with no SA available}\n");
	p(ips_out_nomem, "\t{:discarded-no-memory/%ju} "
	    "{N:/outbound packet%s failed due to insufficient memory}\n");
	p(ips_out_noroute, "\t{:discarded-no-route/%ju} "
	    "{N:/outbound packet%s with no route available}\n");
	p(ips_out_inval, "\t{:discarded-invalid/%ju} "
	    "{N:/invalid outbound packet%s}\n");
	p(ips_out_bundlesa, "\t{:send-bundled-sa/%ju} "
	    "{N:/outbound packet%s with bundled SAs}\n");
	p(ips_spdcache_hits, "\t{:spdcache-hits/%ju} "
	    "{N:/spd cache hit%s}\n");
	p2(ips_spdcache_misses, "\t{:spdcache-misses/%ju} "
	    "{N:/spd cache miss%s}\n");
	p(ips_clcopied, "\t{:clusters-copied-during-clone/%ju} "
	    "{N:/cluster%s copied during clone}\n");
	p(ips_mbinserted, "\t{:mbufs-inserted/%ju} "
	    "{N:/mbuf%s inserted during makespace}\n");
#undef p2
#undef p
	xo_close_container("ipsec-statistics");
}

void
ipsec_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct ipsecstat ipsecstat;

	if (strcmp(name, "ipsec6") == 0) {
		if (fetch_stats("net.inet6.ipsec6.ipsecstats", off,&ipsecstat,
				sizeof(ipsecstat), kread_counters) != 0)
			return;
	} else {
		if (fetch_stats("net.inet.ipsec.ipsecstats", off, &ipsecstat,
				sizeof(ipsecstat), kread_counters) != 0)
			return;
	}

	xo_emit("{T:/%s}:\n", name);

	print_ipsecstats(&ipsecstat);
}


static void print_ahstats(const struct ahstat *ahstat);
static void print_espstats(const struct espstat *espstat);
static void print_ipcompstats(const struct ipcompstat *ipcompstat);

/*
 * Dump IPSEC statistics structure.
 */
static void
ipsec_hist_new(const uint64_t *hist, size_t histmax,
    const struct val2str *name, const char *title, const char *cname)
{
	int first;
	size_t proto;
	const struct val2str *p;

	first = 1;
	for (proto = 0; proto < histmax; proto++) {
		if (hist[proto] <= 0)
			continue;
		if (first) {
			xo_open_list(cname);
			xo_emit("\t{T:/%s histogram}:\n", title);
			first = 0;
		}
		xo_open_instance(cname);
		for (p = name; p && p->str; p++) {
			if (p->val == (int)proto)
				break;
		}
		if (p && p->str) {
			xo_emit("\t\t{k:name}: {:count/%ju}\n", p->str,
			    (uintmax_t)hist[proto]);
		} else {
			xo_emit("\t\t#{k:name/%lu}: {:count/%ju}\n",
			    (unsigned long)proto, (uintmax_t)hist[proto]);
		}
		xo_close_instance(cname);
	}
	if (!first)
		xo_close_list(cname);
}

static void
print_ahstats(const struct ahstat *ahstat)
{
	xo_open_container("ah-statictics");

#define	p(f, n, m) if (ahstat->f || sflag <= 1) \
	xo_emit("\t{:" n "/%ju} {N:/" m "}\n",	\
	    (uintmax_t)ahstat->f, plural(ahstat->f))
#define	hist(f, n, t, c) \
	ipsec_hist_new((f), sizeof(f)/sizeof(f[0]), (n), (t), (c))

	p(ahs_hdrops, "dropped-short-header",
	    "packet%s shorter than header shows");
	p(ahs_nopf, "dropped-bad-protocol",
	    "packet%s dropped; protocol family not supported");
	p(ahs_notdb, "dropped-no-tdb", "packet%s dropped; no TDB");
	p(ahs_badkcr, "dropped-bad-kcr", "packet%s dropped; bad KCR");
	p(ahs_qfull, "dropped-queue-full", "packet%s dropped; queue full");
	p(ahs_noxform, "dropped-no-transform",
	    "packet%s dropped; no transform");
	p(ahs_wrap, "replay-counter-wraps", "replay counter wrap%s");
	p(ahs_badauth, "dropped-bad-auth",
	    "packet%s dropped; bad authentication detected");
	p(ahs_badauthl, "dropped-bad-auth-level",
	    "packet%s dropped; bad authentication length");
	p(ahs_replay, "possile-replay-detected",
	    "possible replay packet%s detected");
	p(ahs_input, "received-packets", "packet%s in");
	p(ahs_output, "send-packets", "packet%s out");
	p(ahs_invalid, "dropped-bad-tdb", "packet%s dropped; invalid TDB");
	p(ahs_ibytes, "received-bytes", "byte%s in");
	p(ahs_obytes, "send-bytes", "byte%s out");
	p(ahs_toobig, "dropped-too-large",
	    "packet%s dropped; larger than IP_MAXPACKET");
	p(ahs_pdrops, "dropped-policy-violation",
	    "packet%s blocked due to policy");
	p(ahs_crypto, "crypto-failures", "crypto processing failure%s");
	p(ahs_tunnel, "tunnel-failures", "tunnel sanity check failure%s");
	hist(ahstat->ahs_hist, ipsec_ahnames,
	    "AH output", "ah-output-histogram");

#undef p
#undef hist
	xo_close_container("ah-statictics");
}

void
ah_stats(u_long off, const char *name, int family __unused, int proto __unused)
{
	struct ahstat ahstat;

	if (fetch_stats("net.inet.ah.stats", off, &ahstat,
	    sizeof(ahstat), kread_counters) != 0)
		return;

	xo_emit("{T:/%s}:\n", name);

	print_ahstats(&ahstat);
}

static void
print_espstats(const struct espstat *espstat)
{
	xo_open_container("esp-statictics");
#define	p(f, n, m) if (espstat->f || sflag <= 1)	\
	xo_emit("\t{:" n "/%ju} {N:/" m "}\n",		\
	    (uintmax_t)espstat->f, plural(espstat->f))
#define	hist(f, n, t, c) \
	ipsec_hist_new((f), sizeof(f)/sizeof(f[0]), (n), (t), (c));

	p(esps_hdrops, "dropped-short-header",
	    "packet%s shorter than header shows");
	p(esps_nopf, "dropped-bad-protocol",
	    "packet%s dropped; protocol family not supported");
	p(esps_notdb, "dropped-no-tdb", "packet%s dropped; no TDB");
	p(esps_badkcr, "dropped-bad-kcr", "packet%s dropped; bad KCR");
	p(esps_qfull, "dropped-queue-full", "packet%s dropped; queue full");
	p(esps_noxform, "dropped-no-transform",
	    "packet%s dropped; no transform");
	p(esps_badilen, "dropped-bad-length", "packet%s dropped; bad ilen");
	p(esps_wrap, "replay-counter-wraps", "replay counter wrap%s");
	p(esps_badenc, "dropped-bad-crypto",
	    "packet%s dropped; bad encryption detected");
	p(esps_badauth, "dropped-bad-auth",
	    "packet%s dropped; bad authentication detected");
	p(esps_replay, "possible-replay-detected",
	    "possible replay packet%s detected");
	p(esps_input, "received-packets", "packet%s in");
	p(esps_output, "sent-packets", "packet%s out");
	p(esps_invalid, "dropped-bad-tdb", "packet%s dropped; invalid TDB");
	p(esps_ibytes, "receieve-bytes", "byte%s in");
	p(esps_obytes, "sent-bytes", "byte%s out");
	p(esps_toobig, "dropped-too-large",
	    "packet%s dropped; larger than IP_MAXPACKET");
	p(esps_pdrops, "dropped-policy-violation",
	    "packet%s blocked due to policy");
	p(esps_crypto, "crypto-failures", "crypto processing failure%s");
	p(esps_tunnel, "tunnel-failures", "tunnel sanity check failure%s");
	hist(espstat->esps_hist, ipsec_espnames,
	    "ESP output", "esp-output-histogram");

#undef p
#undef hist
	xo_close_container("esp-statictics");
}

void
esp_stats(u_long off, const char *name, int family __unused, int proto __unused)
{
	struct espstat espstat;

	if (fetch_stats("net.inet.esp.stats", off, &espstat,
	    sizeof(espstat), kread_counters) != 0)
		return;

	xo_emit("{T:/%s}:\n", name);

	print_espstats(&espstat);
}

static void
print_ipcompstats(const struct ipcompstat *ipcompstat)
{
	xo_open_container("ipcomp-statictics");

#define	p(f, n, m) if (ipcompstat->f || sflag <= 1)	\
	xo_emit("\t{:" n "/%ju} {N:/" m "}\n",		\
	    (uintmax_t)ipcompstat->f, plural(ipcompstat->f))
#define	hist(f, n, t, c) \
	ipsec_hist_new((f), sizeof(f)/sizeof(f[0]), (n), (t), (c));

	p(ipcomps_hdrops, "dropped-short-header",
	    "packet%s shorter than header shows");
	p(ipcomps_nopf, "dropped-bad-protocol",
	    "packet%s dropped; protocol family not supported");
	p(ipcomps_notdb, "dropped-no-tdb", "packet%s dropped; no TDB");
	p(ipcomps_badkcr, "dropped-bad-kcr", "packet%s dropped; bad KCR");
	p(ipcomps_qfull, "dropped-queue-full", "packet%s dropped; queue full");
	p(ipcomps_noxform, "dropped-no-transform",
	    "packet%s dropped; no transform");
	p(ipcomps_wrap, "replay-counter-wraps", "replay counter wrap%s");
	p(ipcomps_input, "receieve-packets", "packet%s in");
	p(ipcomps_output, "sent-packets", "packet%s out");
	p(ipcomps_invalid, "dropped-bad-tdb", "packet%s dropped; invalid TDB");
	p(ipcomps_ibytes, "receieved-bytes", "byte%s in");
	p(ipcomps_obytes, "sent-bytes", "byte%s out");
	p(ipcomps_toobig, "dropped-too-large",
	    "packet%s dropped; larger than IP_MAXPACKET");
	p(ipcomps_pdrops, "dropped-policy-violation",
	    "packet%s blocked due to policy");
	p(ipcomps_crypto, "crypto-failure", "crypto processing failure%s");
	hist(ipcompstat->ipcomps_hist, ipsec_compnames,
	    "COMP output", "comp-output-histogram");
	p(ipcomps_threshold, "sent-uncompressed-small-packets",
	    "packet%s sent uncompressed; size < compr. algo. threshold");
	p(ipcomps_uncompr, "sent-uncompressed-useless-packets",
	    "packet%s sent uncompressed; compression was useless");

#undef p
#undef hist
	xo_close_container("ipcomp-statictics");
}

void
ipcomp_stats(u_long off, const char *name, int family __unused,
    int proto __unused)
{
	struct ipcompstat ipcompstat;

	if (fetch_stats("net.inet.ipcomp.stats", off, &ipcompstat,
	    sizeof(ipcompstat), kread_counters) != 0)
		return;

	xo_emit("{T:/%s}:\n", name);

	print_ipcompstats(&ipcompstat);
}

#endif /*IPSEC*/
