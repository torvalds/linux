/*	$OpenBSD: roa.c,v 1.87 2025/09/09 08:23:24 job Exp $ */
/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/stack.h>
#include <openssl/safestack.h>
#include <openssl/x509.h>

#include "extern.h"
#include "rpki-asn1.h"

/*
 * ROA eContent definition in RFC 9582, section 4.
 */

ASN1_ITEM_EXP RouteOriginAttestation_it;
ASN1_ITEM_EXP ROAIPAddressFamily_it;
ASN1_ITEM_EXP ROAIPAddress_it;

ASN1_SEQUENCE(RouteOriginAttestation) = {
	ASN1_EXP_OPT(RouteOriginAttestation, version, ASN1_INTEGER, 0),
	ASN1_SIMPLE(RouteOriginAttestation, asid, ASN1_INTEGER),
	ASN1_SEQUENCE_OF(RouteOriginAttestation, ipAddrBlocks,
	    ROAIPAddressFamily),
} ASN1_SEQUENCE_END(RouteOriginAttestation);

IMPLEMENT_ASN1_FUNCTIONS(RouteOriginAttestation);

ASN1_SEQUENCE(ROAIPAddressFamily) = {
	ASN1_SIMPLE(ROAIPAddressFamily, addressFamily, ASN1_OCTET_STRING),
	ASN1_SEQUENCE_OF(ROAIPAddressFamily, addresses, ROAIPAddress),
} ASN1_SEQUENCE_END(ROAIPAddressFamily);

ASN1_SEQUENCE(ROAIPAddress) = {
	ASN1_SIMPLE(ROAIPAddress, address, ASN1_BIT_STRING),
	ASN1_OPT(ROAIPAddress, maxLength, ASN1_INTEGER),
} ASN1_SEQUENCE_END(ROAIPAddress);

/*
 * Parses the eContent section of an ROA file, RFC 9582, section 4.
 * Returns zero on failure, non-zero on success.
 */
static int
roa_parse_econtent(const char *fn, struct roa *roa, const unsigned char *d,
    size_t dsz)
{
	const unsigned char		*oder;
	RouteOriginAttestation		*roa_asn1;
	const ROAIPAddressFamily	*addrfam;
	const STACK_OF(ROAIPAddress)	*addrs;
	int				 addrsz, ipv4_seen = 0, ipv6_seen = 0;
	enum afi			 afi;
	const ROAIPAddress		*addr;
	uint64_t			 maxlen;
	struct ip_addr			 ipaddr;
	struct roa_ip			*res;
	int				 ipaddrblocksz;
	int				 i, j, rc = 0;

	oder = d;
	if ((roa_asn1 = d2i_RouteOriginAttestation(NULL, &d, dsz)) == NULL) {
		warnx("%s: RFC 9582 section 4: failed to parse "
		    "RouteOriginAttestation", fn);
		goto out;
	}
	if (d != oder + dsz) {
		warnx("%s: %td bytes trailing garbage in eContent", fn,
		    oder + dsz - d);
		goto out;
	}

	if (!valid_econtent_version(fn, roa_asn1->version, 0))
		goto out;

	/*
	 * XXX: from here until the function end should be refactored
	 * to deduplicate similar code in ccr.c.
	 */

	if (!as_id_parse(roa_asn1->asid, &roa->asid)) {
		warnx("%s: RFC 9582 section 4.2: asID: "
		    "malformed AS identifier", fn);
		goto out;
	}

	ipaddrblocksz = sk_ROAIPAddressFamily_num(roa_asn1->ipAddrBlocks);
	if (ipaddrblocksz != 1 && ipaddrblocksz != 2) {
		warnx("%s: RFC 9582: unexpected number of ipAddrBlocks "
		    "(got %d, expected 1 or 2)", fn, ipaddrblocksz);
		goto out;
	}

	for (i = 0; i < ipaddrblocksz; i++) {
		addrfam = sk_ROAIPAddressFamily_value(roa_asn1->ipAddrBlocks,
		    i);
		addrs = addrfam->addresses;
		addrsz = sk_ROAIPAddress_num(addrs);

		if (!ip_addr_afi_parse(fn, addrfam->addressFamily, &afi)) {
			warnx("%s: RFC 9582 section 4.3: addressFamily: "
			    "invalid", fn);
			goto out;
		}

		switch (afi) {
		case AFI_IPV4:
			if (ipv4_seen++ > 0) {
				warnx("%s: RFC 9582 section 4.3.1: "
				    "IPv4 appears twice", fn);
				goto out;
			}
			break;
		case AFI_IPV6:
			if (ipv6_seen++ > 0) {
				warnx("%s: RFC 9582 section 4.3.1: "
				    "IPv6 appears twice", fn);
				goto out;
			}
			break;
		}

		if (addrsz == 0) {
			warnx("%s: RFC 9582, section 4.3.1: "
			    "empty ROAIPAddressFamily", fn);
			goto out;
		}

		if (roa->num_ips + addrsz >= MAX_IP_SIZE) {
			warnx("%s: too many ROAIPAddress entries: limit %d",
			    fn, MAX_IP_SIZE);
			goto out;
		}
		roa->ips = recallocarray(roa->ips, roa->num_ips,
		    roa->num_ips + addrsz, sizeof(struct roa_ip));
		if (roa->ips == NULL)
			err(1, NULL);

		for (j = 0; j < addrsz; j++) {
			addr = sk_ROAIPAddress_value(addrs, j);

			if (!ip_addr_parse(addr->address, afi, fn, &ipaddr)) {
				warnx("%s: RFC 9582 section 4.3.2.1: address: "
				    "invalid IP address", fn);
				goto out;
			}
			maxlen = ipaddr.prefixlen;

			if (addr->maxLength != NULL) {
				if (!ASN1_INTEGER_get_uint64(&maxlen,
				    addr->maxLength)) {
					warnx("%s: RFC 9582 section 4.3.2.2: "
					    "ASN1_INTEGER_get_uint64 failed",
					    fn);
					goto out;
				}
				if (ipaddr.prefixlen > maxlen) {
					warnx("%s: prefixlen (%d) larger than "
					    "maxLength (%llu)", fn,
					    ipaddr.prefixlen,
					    (unsigned long long)maxlen);
					goto out;
				}
				if (maxlen > ((afi == AFI_IPV4) ? 32 : 128)) {
					warnx("%s: maxLength (%llu) too large",
					    fn, (unsigned long long)maxlen);
					goto out;
				}
			}

			res = &roa->ips[roa->num_ips++];
			res->addr = ipaddr;
			res->afi = afi;
			res->maxlength = maxlen;
			ip_roa_compose_ranges(res);
		}
	}

	rc = 1;
 out:
	RouteOriginAttestation_free(roa_asn1);
	return rc;
}

/*
 * Parse a full RFC 9582 file.
 * Returns the ROA or NULL if the document was malformed.
 */
struct roa *
roa_parse(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t len)
{
	struct roa	*roa;
	struct cert	*cert = NULL;
	size_t		 cmsz;
	unsigned char	*cms;
	time_t		 signtime = 0;
	int		 rc = 0;

	assert(*out_cert == NULL);

	cms = cms_parse_validate(&cert, fn, talid, der, len, roa_oid, &cmsz,
	    &signtime);
	if (cms == NULL)
		return NULL;

	if ((roa = calloc(1, sizeof(struct roa))) == NULL)
		err(1, NULL);
	roa->signtime = signtime;

	if (!roa_parse_econtent(fn, roa, cms, cmsz))
		goto out;

	if (x509_any_inherits(cert->x509)) {
		warnx("%s: inherit elements not allowed in EE cert", fn);
		goto out;
	}

	if (cert->num_ases > 0) {
		warnx("%s: superfluous AS Resources extension present", fn);
		goto out;
	}

	if (cert->num_ips == 0) {
		warnx("%s: no IP address present", fn);
		goto out;
	}

	/*
	 * If the ROA isn't valid, we accept it anyway and depend upon
	 * the code around roa_read() to check the "valid" field itself.
	 */
	roa->valid = valid_roa(fn, cert, roa);

	*out_cert = cert;
	cert = NULL;

	rc = 1;
out:
	if (rc == 0) {
		roa_free(roa);
		roa = NULL;
	}
	cert_free(cert);
	free(cms);
	return roa;
}

/*
 * Free an ROA pointer.
 * Safe to call with NULL.
 */
void
roa_free(struct roa *p)
{

	if (p == NULL)
		return;
	free(p->ips);
	free(p);
}

/*
 * Serialise parsed ROA content.
 * See roa_read() for reader.
 */
void
roa_buffer(struct ibuf *b, const struct roa *p)
{
	io_simple_buffer(b, &p->valid, sizeof(p->valid));
	io_simple_buffer(b, &p->asid, sizeof(p->asid));
	io_simple_buffer(b, &p->talid, sizeof(p->talid));
	io_simple_buffer(b, &p->num_ips, sizeof(p->num_ips));
	io_simple_buffer(b, &p->expires, sizeof(p->expires));

	io_simple_buffer(b, p->ips, p->num_ips * sizeof(p->ips[0]));
}

/*
 * Read parsed ROA content from descriptor.
 * See roa_buffer() for writer.
 * Result must be passed to roa_free().
 */
struct roa *
roa_read(struct ibuf *b)
{
	struct roa	*p;

	if ((p = calloc(1, sizeof(struct roa))) == NULL)
		err(1, NULL);

	io_read_buf(b, &p->valid, sizeof(p->valid));
	io_read_buf(b, &p->asid, sizeof(p->asid));
	io_read_buf(b, &p->talid, sizeof(p->talid));
	io_read_buf(b, &p->num_ips, sizeof(p->num_ips));
	io_read_buf(b, &p->expires, sizeof(p->expires));

	if (p->num_ips > 0) {
		if ((p->ips = calloc(p->num_ips, sizeof(p->ips[0]))) == NULL)
			err(1, NULL);
		io_read_buf(b, p->ips, p->num_ips * sizeof(p->ips[0]));
	}

	return p;
}

/*
 * Add each IP address in the ROA into the VRP tree.
 * Updates "vrps" to be the number of VRPs and "uniqs" to be the unique
 * number of addresses.
 */
void
roa_insert_vrps(struct vrp_tree *tree, struct roa *roa, struct repo *rp)
{
	struct vrp	*v, *found;
	size_t		 i;

	for (i = 0; i < roa->num_ips; i++) {
		if ((v = malloc(sizeof(*v))) == NULL)
			err(1, NULL);
		v->afi = roa->ips[i].afi;
		v->addr = roa->ips[i].addr;
		v->maxlength = roa->ips[i].maxlength;
		v->asid = roa->asid;
		v->talid = roa->talid;
		v->repoid = repo_id(rp);
		v->expires = roa->expires;

		/*
		 * Check if a similar VRP already exists in the tree.
		 * If the found VRP expires sooner, update it to this
		 * ROAs later expiry moment.
		 */
		if ((found = RB_INSERT(vrp_tree, tree, v)) != NULL) {
			/* already exists */
			if (found->expires < v->expires) {
				/* update found with preferred data */
				/* adjust unique count */
				repo_stat_inc(repo_byid(found->repoid),
				    found->talid, RTYPE_ROA, STYPE_DEC_UNIQUE);
				found->expires = v->expires;
				found->talid = v->talid;
				found->repoid = v->repoid;
				repo_stat_inc(rp, v->talid, RTYPE_ROA,
				    STYPE_UNIQUE);
			}
			free(v);
		} else
			repo_stat_inc(rp, v->talid, RTYPE_ROA, STYPE_UNIQUE);

		repo_stat_inc(rp, roa->talid, RTYPE_ROA, STYPE_TOTAL);
	}
}

static inline int
vrpcmp(struct vrp *a, struct vrp *b)
{
	int rv;

	if (a->afi > b->afi)
		return 1;
	if (a->afi < b->afi)
		return -1;
	switch (a->afi) {
	case AFI_IPV4:
		rv = memcmp(&a->addr.addr, &b->addr.addr, 4);
		if (rv)
			return rv;
		break;
	case AFI_IPV6:
		rv = memcmp(&a->addr.addr, &b->addr.addr, 16);
		if (rv)
			return rv;
		break;
	default:
		break;
	}
	/* a smaller prefixlen is considered bigger, e.g. /8 vs /10 */
	if (a->addr.prefixlen < b->addr.prefixlen)
		return 1;
	if (a->addr.prefixlen > b->addr.prefixlen)
		return -1;
	if (a->maxlength < b->maxlength)
		return 1;
	if (a->maxlength > b->maxlength)
		return -1;

	if (a->asid > b->asid)
		return 1;
	if (a->asid < b->asid)
		return -1;

	return 0;
}

RB_GENERATE(vrp_tree, vrp, entry, vrpcmp);
