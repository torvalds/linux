/*	$OpenBSD: ip.c,v 1.34 2024/11/12 09:23:07 tb Exp $ */
/*
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/socket.h>
#include <arpa/inet.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#define	PREFIX_SIZE(x)	(((x) + 7) / 8)

/*
 * Parse an IP address family.
 * This is defined in different places in the ROA/X509 standards, but
 * it's the same thing.
 * We prohibit all but IPv4 and IPv6, without SAFI.
 * Return zero on failure, non-zero on success.
 */
int
ip_addr_afi_parse(const char *fn, const ASN1_OCTET_STRING *p, enum afi *afi)
{
	uint16_t v;

	if (p->length == 0 || p->length > 3) {
		warnx("%s: invalid field length, want 1--3, have %d",
		    fn, p->length);
		return 0;
	}

	memcpy(&v, p->data, sizeof(v));
	v = ntohs(v);

	/* Only accept IPv4 and IPv6 AFIs. */

	if (v != AFI_IPV4 && v != AFI_IPV6) {
		warnx("%s: only AFI for IPV4 (1) and IPV6 (2) allowed: "
		    "have %hd", fn, v);
		return 0;
	}

	/* Disallow the optional SAFI. */

	if (p->length == 3) {
		warnx("%s: SAFI not allowed", fn);
		return 0;
	}

	*afi = v;
	return 1;
}

/*
 * See if a given IP prefix is covered by the IP prefixes or ranges
 * specified in the "ips" array.
 * This means that the IP prefix must be strictly within the ranges or
 * singletons given in the array.
 * Return 0 if we're inheriting from the issuer, >0 if we're covered,
 * or <0 if we're not covered.
 */
int
ip_addr_check_covered(enum afi afi,
    const unsigned char *min, const unsigned char *max,
    const struct cert_ip *ips, size_t num_ips)
{
	size_t	 i, sz = AFI_IPV4 == afi ? 4 : 16;

	for (i = 0; i < num_ips; i++) {
		if (ips[i].afi != afi)
			continue;
		if (ips[i].type == CERT_IP_INHERIT)
			return 0;
		if (memcmp(ips[i].min, min, sz) <= 0 &&
		    memcmp(ips[i].max, max, sz) >= 0)
			return 1;
	}

	return -1;
}

/*
 * Given a newly-parsed IP address or range "ip", make sure that "ip"
 * does not overlap with any addresses or ranges in the "ips" array.
 * This is defined by RFC 3779 section 2.2.3.6.
 * Returns zero on failure, non-zero on success.
 */
int
ip_addr_check_overlap(const struct cert_ip *ip, const char *fn,
    const struct cert_ip *ips, size_t num_ips, int quiet)
{
	size_t	 i, sz = ip->afi == AFI_IPV4 ? 4 : 16;
	int	 inherit_v4 = 0, inherit_v6 = 0;
	int	 has_v4 = 0, has_v6 = 0;

	/*
	 * FIXME: cache this by having a flag on the cert_ip, else we're
	 * going to need to do a lot of scanning for big allocations.
	 */

	for (i = 0; i < num_ips; i++)
		if (ips[i].type == CERT_IP_INHERIT) {
			if (ips[i].afi == AFI_IPV4)
				inherit_v4 = 1;
			else
				inherit_v6 = 1;
		} else {
			if (ips[i].afi == AFI_IPV4)
				has_v4 = 1;
			else
				has_v6 = 1;
		}

	/* Disallow multiple inheritance per type. */

	if ((inherit_v4 && ip->afi == AFI_IPV4) ||
	    (inherit_v6 && ip->afi == AFI_IPV6) ||
	    (has_v4 && ip->afi == AFI_IPV4 &&
	     ip->type == CERT_IP_INHERIT) ||
	    (has_v6 && ip->afi == AFI_IPV6 &&
	     ip->type == CERT_IP_INHERIT)) {
		if (!quiet) {
			warnx("%s: RFC 3779 section 2.2.3.5: "
			    "cannot have multiple inheritance or inheritance "
			    "and addresses of the same class", fn);
		}
		return 0;
	}

	/* Check our ranges. */

	for (i = 0; i < num_ips; i++) {
		if (ips[i].afi != ip->afi)
			continue;
		if (memcmp(ips[i].max, ip->min, sz) <= 0 ||
		    memcmp(ips[i].min, ip->max, sz) >= 0)
			continue;
		if (!quiet) {
			warnx("%s: RFC 3779 section 2.2.3.5: "
			    "cannot have overlapping IP addresses", fn);
			ip_warn(fn, "certificate IP", ip);
			ip_warn(fn, "offending IP", &ips[i]);
		}
		return 0;
	}

	return 1;
}

/*
 * Parse an IP address, RFC 3779, 2.2.3.8.
 * Return zero on failure, non-zero on success.
 */
int
ip_addr_parse(const ASN1_BIT_STRING *p,
    enum afi afi, const char *fn, struct ip_addr *addr)
{
	long	 unused = 0;

	/* Weird OpenSSL-ism to get unused bit count. */

	if ((p->flags & ASN1_STRING_FLAG_BITS_LEFT))
		unused = p->flags & 0x07;

	if (p->length == 0 && unused != 0) {
		warnx("%s: RFC 3779 section 2.2.3.8: "
		    "unused bit count must be zero if length is zero", fn);
		return 0;
	}

	/*
	 * Check that the unused bits are set to zero.
	 * If we don't do this, stray bits will corrupt our composition
	 * of the [minimum] address ranges.
	 */

	if (p->length != 0 &&
	    (p->data[p->length - 1] & ((1 << unused) - 1))) {
		warnx("%s: RFC 3779 section 2.2.3.8: "
		    "unused bits must be set to zero", fn);
		return 0;
	}

	/* Limit possible sizes of addresses. */

	if ((afi == AFI_IPV4 && p->length > 4) ||
	    (afi == AFI_IPV6 && p->length > 16)) {
		warnx("%s: RFC 3779 section 2.2.3.8: "
		    "IP address too long", fn);
		return 0;
	}

	memset(addr, 0, sizeof(struct ip_addr));
	addr->prefixlen = p->length * 8 - unused;
	memcpy(addr->addr, p->data, p->length);
	return 1;
}

/*
 * Convert a ip_addr into a NUL-terminated CIDR notation string
 * conforming to RFC 4632 or 4291.
 * The size of the buffer must be at least 64 (inclusive).
 */
void
ip_addr_print(const struct ip_addr *addr,
    enum afi afi, char *buf, size_t bufsz)
{
	char ipbuf[INET6_ADDRSTRLEN];
	int ret, af;

	switch (afi) {
	case AFI_IPV4:
		af = AF_INET;
		break;
	case AFI_IPV6:
		af = AF_INET6;
		break;
	default:
		errx(1, "unsupported address family identifier");
	}

	if (inet_ntop(af, addr->addr, ipbuf, sizeof(ipbuf)) == NULL)
		err(1, "inet_ntop");
	ret = snprintf(buf, bufsz, "%s/%hhu", ipbuf, addr->prefixlen);
	if (ret < 0 || (size_t)ret >= bufsz)
		err(1, "malformed IP address");
}

/*
 * Convert a ip_addr into a NUL-terminated range notation string.
 * The size of the buffer must be at least 95 (inclusive).
 */
static void
ip_addr_range_print(const struct ip_addr_range *range,
    enum afi afi, char *buf, size_t bufsz)
{
	struct cert_ip ip;
	char min[INET6_ADDRSTRLEN], max[INET6_ADDRSTRLEN];
	int ret, af;

	switch (afi) {
	case AFI_IPV4:
		af = AF_INET;
		break;
	case AFI_IPV6:
		af = AF_INET6;
		break;
	default:
		errx(1, "unsupported address family identifier");
	}

	memset(&ip, 0, sizeof(ip));

	ip.afi = afi;
	ip.type = CERT_IP_RANGE;
	ip.range = *range;
	if (!ip_cert_compose_ranges(&ip))
		errx(1, "failed to compose ranges");

	if (inet_ntop(af, ip.min, min, sizeof(min)) == NULL)
		err(1, "inet_ntop");
	if (inet_ntop(af, ip.max, max, sizeof(max)) == NULL)
		err(1, "inet_ntop");

	ret = snprintf(buf, bufsz, "%s--%s", min, max);
	if (ret < 0 || (size_t)ret >= bufsz)
		err(1, "malformed IP address");
}

/*
 * Given the addresses (range or IP) in cert_ip, fill in the "min" and
 * "max" fields with the minimum and maximum possible IP addresses given
 * those ranges (or singleton prefixed range).
 * This does nothing if CERT_IP_INHERIT.
 * Returns zero on failure (misordered ranges), non-zero on success.
 */
int
ip_cert_compose_ranges(struct cert_ip *p)
{
	size_t sz;

	switch (p->type) {
	case CERT_IP_ADDR:
		sz = PREFIX_SIZE(p->ip.prefixlen);
		memset(p->min, 0x0, sizeof(p->min));
		memcpy(p->min, p->ip.addr, sz);
		memset(p->max, 0xff, sizeof(p->max));
		memcpy(p->max, p->ip.addr, sz);
		if (sz > 0 && p->ip.prefixlen % 8 != 0)
			p->max[sz - 1] |= (1 << (8 - p->ip.prefixlen % 8)) - 1;
		break;
	case CERT_IP_RANGE:
		memset(p->min, 0x0, sizeof(p->min));
		sz = PREFIX_SIZE(p->range.min.prefixlen);
		memcpy(p->min, p->range.min.addr, sz);
		memset(p->max, 0xff, sizeof(p->max));
		sz = PREFIX_SIZE(p->range.max.prefixlen);
		memcpy(p->max, p->range.max.addr, sz);
		if (sz > 0 && p->range.max.prefixlen % 8 != 0)
			p->max[sz - 1] |=
			    (1 << (8 - p->range.max.prefixlen % 8)) - 1;
		break;
	default:
		return 1;
	}

	sz = p->afi == AFI_IPV4 ? 4 : 16;
	return memcmp(p->min, p->max, sz) <= 0;
}

/*
 * Given the ROA's acceptable prefix, compute the minimum and maximum
 * address accepted by the prefix.
 */
void
ip_roa_compose_ranges(struct roa_ip *p)
{
	size_t sz = PREFIX_SIZE(p->addr.prefixlen);

	memset(p->min, 0x0, sizeof(p->min));
	memcpy(p->min, p->addr.addr, sz);
	memset(p->max, 0xff, sizeof(p->max));
	memcpy(p->max, p->addr.addr, sz);
	if (sz > 0 && p->addr.prefixlen % 8 != 0)
		p->max[sz - 1] |= (1 << (8 - p->addr.prefixlen % 8)) - 1;
}

void
ip_warn(const char *fn, const char *msg, const struct cert_ip *ip)
{
	char buf[128];

	switch (ip->type) {
	case CERT_IP_ADDR:
		ip_addr_print(&ip->ip, ip->afi, buf, sizeof(buf));
		warnx("%s: %s: %s", fn, msg, buf);
		break;
	case CERT_IP_RANGE:
		ip_addr_range_print(&ip->range, ip->afi, buf, sizeof(buf));
		warnx("%s: %s: %s", fn, msg, buf);
		break;
	case CERT_IP_INHERIT:
		warnx("%s: %s: IP (inherit)", fn, msg);
		break;
	default:
		warnx("%s: corrupt cert", fn);
		break;
	}
}
