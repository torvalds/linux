/*	$OpenBSD: util.c,v 1.95 2025/03/26 15:29:30 claudio Exp $ */

/*
 * Copyright (c) 2006 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vis.h>

#include "bgpd.h"
#include "rde.h"

const char *
log_addr(const struct bgpd_addr *addr)
{
	static char	buf[74];
	struct sockaddr *sa;
	socklen_t	len;

	sa = addr2sa(addr, 0, &len);
	switch (addr->aid) {
	case AID_INET:
	case AID_INET6:
		return log_sockaddr(sa, len);
	case AID_VPN_IPv4:
	case AID_VPN_IPv6:
		snprintf(buf, sizeof(buf), "%s %s", log_rd(addr->rd),
		    log_sockaddr(sa, len));
		return (buf);
	case AID_EVPN:
		return log_evpnaddr(addr, sa, len);
		break;
	}
	return ("???");
}

static const char *
log_mac(const uint8_t mac[ETHER_ADDR_LEN])
{
	static char buf[18];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],
	    mac[1], mac[2], mac[3], mac[4], mac[5]);

	return (buf);
}

static const uint8_t zero_esi[ESI_ADDR_LEN];

static const char *
log_esi(const uint8_t esi[ESI_ADDR_LEN])
{
	static char buf[30];

	if (memcmp(esi, zero_esi, sizeof(zero_esi)) == 0)
		return ("");

	snprintf(buf, sizeof(buf),
	    "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", esi[0],
	    esi[1], esi[2], esi[3], esi[4], esi[5], esi[6], esi[7], esi[8],
	    esi[9]);

	return (buf);
}

const char *
log_evpnaddr(const struct bgpd_addr *addr, struct sockaddr *sa,
    socklen_t salen)
{
	static char	buf[138];
	uint32_t	vni;
	uint8_t		len;

	switch (addr->evpn.type) {
	case EVPN_ROUTE_TYPE_2:
		memcpy(&vni, addr->labelstack, addr->labellen);
		snprintf(buf, sizeof(buf), "[2]:[%s]:[%s]:[%d]:[48]:[%s]",
		    log_rd(addr->rd), log_esi(addr->evpn.esi), htonl(vni) >> 8,
		    log_mac(addr->evpn.mac));
		if (sa != NULL) {
			len = strlen(buf);
			snprintf(buf+len, sizeof(buf)-len, ":[%d]:[%s]",
			    sa->sa_family == AF_INET ? 32 : 128,
			    log_sockaddr(sa, salen));
		}
		break;
	case EVPN_ROUTE_TYPE_3:
		if (sa != NULL) {
			memcpy(&vni, addr->labelstack, addr->labellen);
			snprintf(buf, sizeof(buf), "[3]:[%s]:[%d]:[%s]",
			    log_rd(addr->rd),
			    sa->sa_family == AF_INET ? 32 : 128,
			    log_sockaddr(sa, salen));
		}
		break;
	default:
		break;
	}
	return (buf);
}

const char *
log_in6addr(const struct in6_addr *addr)
{
	struct sockaddr_in6	sa_in6;

	memset(&sa_in6, 0, sizeof(sa_in6));
	sa_in6.sin6_family = AF_INET6;
	memcpy(&sa_in6.sin6_addr, addr, sizeof(sa_in6.sin6_addr));

#ifdef __KAME__
	/* XXX thanks, KAME, for this ugliness... adopted from route/show.c */
	if ((IN6_IS_ADDR_LINKLOCAL(&sa_in6.sin6_addr) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&sa_in6.sin6_addr) ||
	    IN6_IS_ADDR_MC_NODELOCAL(&sa_in6.sin6_addr)) &&
	    sa_in6.sin6_scope_id == 0) {
		uint16_t tmp16;
		memcpy(&tmp16, &sa_in6.sin6_addr.s6_addr[2], sizeof(tmp16));
		sa_in6.sin6_scope_id = ntohs(tmp16);
		sa_in6.sin6_addr.s6_addr[2] = 0;
		sa_in6.sin6_addr.s6_addr[3] = 0;
	}
#endif

	return (log_sockaddr((struct sockaddr *)&sa_in6, sizeof(sa_in6)));
}

const char *
log_sockaddr(struct sockaddr *sa, socklen_t len)
{
	static char	buf[4][NI_MAXHOST];
	static int	bufidx;

	bufidx = (bufidx + 1) % 4;
	if (sa == NULL || getnameinfo(sa, len, buf[bufidx], sizeof(buf[0]),
	    NULL, 0, NI_NUMERICHOST))
		return ("(unknown)");
	else
		return (buf[bufidx]);
}

const char *
log_as(uint32_t as)
{
	static char	buf[11];	/* "4294967294\0" */

	if (snprintf(buf, sizeof(buf), "%u", as) < 0)
		return ("?");

	return (buf);
}

const char *
log_rd(uint64_t rd)
{
	static char	buf[32];
	struct in_addr	addr;
	uint32_t	u32;
	uint16_t	u16;

	rd = be64toh(rd);
	switch (rd >> 48) {
	case EXT_COMMUNITY_TRANS_TWO_AS:
		u32 = rd & 0xffffffff;
		u16 = (rd >> 32) & 0xffff;
		snprintf(buf, sizeof(buf), "rd %hu:%u", u16, u32);
		break;
	case EXT_COMMUNITY_TRANS_FOUR_AS:
		u32 = (rd >> 16) & 0xffffffff;
		u16 = rd & 0xffff;
		snprintf(buf, sizeof(buf), "rd %s:%hu", log_as(u32), u16);
		break;
	case EXT_COMMUNITY_TRANS_IPV4:
		u32 = (rd >> 16) & 0xffffffff;
		u16 = rd & 0xffff;
		addr.s_addr = htonl(u32);
		snprintf(buf, sizeof(buf), "rd %s:%hu", inet_ntoa(addr), u16);
		break;
	default:
		snprintf(buf, sizeof(buf), "rd #%016llx",
		    (unsigned long long)rd);
		break;
	}
	return (buf);
}

const struct ext_comm_pairs iana_ext_comms[] = IANA_EXT_COMMUNITIES;

/* NOTE: this function does not check if the type/subtype combo is
 * actually valid. */
const char *
log_ext_subtype(int type, uint8_t subtype)
{
	static char etype[16];
	const struct ext_comm_pairs *cp;

	for (cp = iana_ext_comms; cp->subname != NULL; cp++) {
		if ((type == cp->type || type == -1) && subtype == cp->subtype)
			return (cp->subname);
	}
	if (type == -1)
		return ("???");
	snprintf(etype, sizeof(etype), "[%hhx:%hhx]", (uint8_t)type, subtype);
	return (etype);
}

const char *
log_reason(const char *communication) {
	static char buf[(REASON_LEN - 1) * 4 + 1];

	strnvis(buf, communication, sizeof(buf), VIS_NL | VIS_OCTAL);

	return buf;
}

static const char *
log_expires(time_t expires)
{
	static char buf[32];

	buf[0] = '\0';
	if (expires != 0)
		snprintf(buf, sizeof(buf), " expires %lld", (long long)expires);
	return buf;
}

const char *
log_roa(struct roa *roa)
{
	static char buf[256];
	char maxbuf[32];
#if defined(__GNUC__) && __GNUC__ < 4
	struct bgpd_addr addr = { .aid = roa->aid };
	addr.v6 = roa->prefix.inet6;
#else
	struct bgpd_addr addr = { .aid = roa->aid, .v6 = roa->prefix.inet6 };
#endif

	maxbuf[0] = '\0';
	if (roa->prefixlen != roa->maxlen)
		snprintf(maxbuf, sizeof(maxbuf), " maxlen %u", roa->maxlen);
	snprintf(buf, sizeof(buf), "%s/%u%s source-as %u%s", log_addr(&addr),
	    roa->prefixlen, maxbuf, roa->asnum, log_expires(roa->expires));
	return buf;
}

const char *
log_aspa(struct aspa_set *aspa)
{
	static char errbuf[256];
	static char *buf;
	static size_t len;
	char asbuf[16];
	size_t needed;
	uint32_t i;

	/* include enough space for header and trailer */
	if ((uint64_t)aspa->num > (SIZE_MAX / sizeof(asbuf) - 72))
		goto fail;
	needed = aspa->num * sizeof(asbuf) + 72;
	if (needed > len) {
		char *nbuf;

		if ((nbuf = realloc(buf, needed)) == NULL)
			goto fail;
		len = needed;
		buf = nbuf;
	}

	snprintf(buf, len, "customer-as %s%s provider-as { ",
	    log_as(aspa->as), log_expires(aspa->expires));

	for (i = 0; i < aspa->num; i++) {
		snprintf(asbuf, sizeof(asbuf), "%s ", log_as(aspa->tas[i]));
		if (strlcat(buf, asbuf, len) >= len)
			goto fail;
	}
	if (strlcat(buf, "}", len) >= len)
		goto fail;
	return buf;

 fail:
	free(buf);
	buf = NULL;
	len = 0;
	snprintf(errbuf, sizeof(errbuf), "customer-as %s%s provider-as { ... }",
	    log_as(aspa->as), log_expires(aspa->expires));
	return errbuf;
}

const char *
log_aspath_error(int error)
{
	static char buf[20];

	switch (error) {
	case AS_ERR_LEN:
		return "inconsistent length";
	case AS_ERR_TYPE:
		return "unknown segment type";
	case AS_ERR_BAD:
		return "invalid encoding";
	case AS_ERR_SOFT:
		return "soft failure";
	default:
		snprintf(buf, sizeof(buf), "unknown %d", error);
		return buf;
	}
}

const char *
log_rtr_error(enum rtr_error err)
{
	static char buf[20];

	switch (err) {
	case NO_ERROR:
		return "No Error";
	case CORRUPT_DATA:
		return "Corrupt Data";
	case INTERNAL_ERROR:
		return "Internal Error";
	case NO_DATA_AVAILABLE:
		return "No Data Available";
	case INVALID_REQUEST:
		return "Invalid Request";
	case UNSUPP_PROTOCOL_VERS:
		return "Unsupported Protocol Version";
	case UNSUPP_PDU_TYPE:
		return "Unsupported PDU Type";
	case UNK_REC_WDRAWL:
		return "Withdrawal of Unknown Record";
	case DUP_REC_RECV:
		return "Duplicate Announcement Received";
	case UNEXP_PROTOCOL_VERS:
		return "Unexpected Protocol Version";
	default:
		snprintf(buf, sizeof(buf), "unknown %u", err);
		return buf;
	}
}

const char *
log_policy(enum role role)
{
	switch (role) {
	case ROLE_PROVIDER:
		return "provider";
	case ROLE_RS:
		return "rs";
	case ROLE_RS_CLIENT:
		return "rs-client";
	case ROLE_CUSTOMER:
		return "customer";
	case ROLE_PEER:
		return "peer";
	default:
		return "unknown";
	}
}

const char *
log_capability(uint8_t capa)
{
	static char buf[20];

	switch (capa) {
	case CAPA_MP:
		return "Multiprotocol Extensions";
	case CAPA_REFRESH:
		return "Route Refresh";
	case CAPA_EXT_NEXTHOP:
		return "Extended Nexhop Encoding";
	case CAPA_EXT_MSG:
		return "Extended Message";
	case CAPA_ROLE:
		return "BGP Role";
	case CAPA_RESTART:
		return "Graceful Restart";
	case CAPA_AS4BYTE:
		return "4-octet AS number";
	case CAPA_ADD_PATH:
		return "ADD-PATH";
	case CAPA_ENHANCED_RR:
		return "Enhanced Route Refresh";
	default:
		snprintf(buf, sizeof(buf), "unknown %u", capa);
		return buf;
	}
}

static const char *
aspath_delim(uint8_t seg_type, int closing)
{
	static char db[8];

	switch (seg_type) {
	case AS_SET:
		if (!closing)
			return ("{ ");
		else
			return (" }");
	case AS_SEQUENCE:
		return ("");
	case AS_CONFED_SEQUENCE:
		if (!closing)
			return ("( ");
		else
			return (" )");
	case AS_CONFED_SET:
		if (!closing)
			return ("[ ");
		else
			return (" ]");
	default:
		if (!closing)
			snprintf(db, sizeof(db), "!%u ", seg_type);
		else
			snprintf(db, sizeof(db), " !%u", seg_type);
		return (db);
	}
}

static int
aspath_snprint(char *buf, size_t size, struct ibuf *in)
{
#define UPDATE()						\
	do {							\
		if (r < 0 || (unsigned int)r >= size)		\
			return (-1);				\
		size -= r;					\
		buf += r;					\
	} while (0)

	struct ibuf	data;
	uint32_t	as;
	int		r, n = 0;
	uint8_t		i, seg_type, seg_len;

	ibuf_from_ibuf(&data, in);
	while (ibuf_size(&data) > 0) {
		if (ibuf_get_n8(&data, &seg_type) == -1 ||
		    ibuf_get_n8(&data, &seg_len) == -1 ||
		    seg_len == 0)
			return (-1);

		r = snprintf(buf, size, "%s%s", n++ != 0 ? " " : "",
		    aspath_delim(seg_type, 0));
		UPDATE();

		for (i = 0; i < seg_len; i++) {
			if (ibuf_get_n32(&data, &as) == -1)
				return -1;

			r = snprintf(buf, size, "%s", log_as(as));
			UPDATE();
			if (i + 1 < seg_len) {
				r = snprintf(buf, size, " ");
				UPDATE();
			}
		}
		r = snprintf(buf, size, "%s", aspath_delim(seg_type, 1));
		UPDATE();
	}
	/* ensure that we have a valid C-string especially for empty as path */
	*buf = '\0';
	return (0);
#undef UPDATE
}

static ssize_t
aspath_strsize(struct ibuf *in)
{
	struct ibuf	 buf;
	ssize_t		 total_size = 0;
	uint32_t	 as;
	uint8_t		 i, seg_type, seg_len;

	ibuf_from_ibuf(&buf, in);
	while (ibuf_size(&buf) > 0) {
		if (ibuf_get_n8(&buf, &seg_type) == -1 ||
		    ibuf_get_n8(&buf, &seg_len) == -1 ||
		    seg_len == 0)
			return (-1);

		if (total_size != 0)
			total_size += 1;
		total_size += strlen(aspath_delim(seg_type, 0));

		for (i = 0; i < seg_len; i++) {
			if (ibuf_get_n32(&buf, &as) == -1)
				return (-1);

			do {
				total_size++;
			} while ((as = as / 10) != 0);
		}
		total_size += seg_len - 1;

		total_size += strlen(aspath_delim(seg_type, 1));
	}
	return (total_size + 1);
}

int
aspath_asprint(char **ret, struct ibuf *data)
{
	ssize_t	slen;

	if ((slen = aspath_strsize(data)) == -1) {
		*ret = NULL;
		errno = EINVAL;
		return (-1);
	}

	*ret = malloc(slen);
	if (*ret == NULL)
		return (-1);

	if (aspath_snprint(*ret, slen, data) == -1) {
		free(*ret);
		*ret = NULL;
		errno = EINVAL;
		return (-1);
	}

	return (0);
}

/*
 * Extract the asnum out of the as segment at the specified position.
 * Direct access is not possible because of non-aligned reads.
 * Only works on verified 4-byte AS paths.
 */
uint32_t
aspath_extract(const void *seg, int pos)
{
	const u_char	*ptr = seg;
	uint32_t	 as;

	/* minimal pos check, return 0 since that is an invalid ASN */
	if (pos < 0 || pos >= ptr[1])
		return (0);
	ptr += 2 + sizeof(uint32_t) * pos;
	memcpy(&as, ptr, sizeof(uint32_t));
	return (ntohl(as));
}

/*
 * Verify that the aspath is correctly encoded.
 */
int
aspath_verify(struct ibuf *in, int as4byte, int permit_set)
{
	struct ibuf	 buf;
	int		 pos, error = 0;
	uint8_t		 seg_len, seg_type;

	ibuf_from_ibuf(&buf, in);
	if (ibuf_size(&buf) & 1) {
		/* odd length aspath are invalid */
		error = AS_ERR_BAD;
		goto done;
	}

	while (ibuf_size(&buf) > 0) {
		if (ibuf_get_n8(&buf, &seg_type) == -1 ||
		    ibuf_get_n8(&buf, &seg_len) == -1) {
			error = AS_ERR_LEN;
			goto done;
		}

		if (seg_len == 0) {
			/* empty aspath segments are not allowed */
			error = AS_ERR_BAD;
			goto done;
		}

		/*
		 * BGP confederations should not show up but consider them
		 * as a soft error which invalidates the path but keeps the
		 * bgp session running.
		 */
		if (seg_type == AS_CONFED_SEQUENCE || seg_type == AS_CONFED_SET)
			error = AS_ERR_SOFT;
		/*
		 * If AS_SET filtering (RFC6472) is on, error out on AS_SET
		 * as well.
		 */
		if (!permit_set && seg_type == AS_SET)
			error = AS_ERR_SOFT;
		if (seg_type != AS_SET && seg_type != AS_SEQUENCE &&
		    seg_type != AS_CONFED_SEQUENCE &&
		    seg_type != AS_CONFED_SET) {
			error = AS_ERR_TYPE;
			goto done;
		}

		/* RFC 7607 - AS 0 is considered malformed */
		for (pos = 0; pos < seg_len; pos++) {
			uint32_t as;

			if (as4byte) {
				if (ibuf_get_n32(&buf, &as) == -1) {
					error = AS_ERR_LEN;
					goto done;
				}
			} else {
				uint16_t tmp;
				if (ibuf_get_n16(&buf, &tmp) == -1) {
					error = AS_ERR_LEN;
					goto done;
				}
				as = tmp;
			}
			if (as == 0)
				error = AS_ERR_SOFT;
		}
	}

 done:
	return (error);	/* aspath is valid but probably not loop free */
}

/*
 * convert a 2 byte aspath to a 4 byte one.
 */
struct ibuf *
aspath_inflate(struct ibuf *in)
{
	struct ibuf	*out;
	uint16_t	 short_as;
	uint8_t		 seg_type, seg_len;

	/*
	 * Allocate enough space for the worst case.
	 * XXX add 1 byte for the empty ASPATH case since we can't
	 * allocate an ibuf of 0 length.
	 */
	if ((out = ibuf_open(ibuf_size(in) * 2 + 1)) == NULL)
		return (NULL);

	/* then copy the aspath */
	while (ibuf_size(in) > 0) {
		if (ibuf_get_n8(in, &seg_type) == -1 ||
		    ibuf_get_n8(in, &seg_len) == -1 ||
		    seg_len == 0)
			goto fail;
		if (ibuf_add_n8(out, seg_type) == -1 ||
		    ibuf_add_n8(out, seg_len) == -1)
			goto fail;

		for (; seg_len > 0; seg_len--) {
			if (ibuf_get_n16(in, &short_as) == -1)
				goto fail;
			if (ibuf_add_n32(out, short_as) == -1)
				goto fail;
		}
	}

	return (out);

fail:
	ibuf_free(out);
	return (NULL);
}

static const u_char	addrmask[] = { 0x00, 0x80, 0xc0, 0xe0, 0xf0,
			    0xf8, 0xfc, 0xfe, 0xff };

/* NLRI functions to extract prefixes from the NLRI blobs */
int
extract_prefix(const u_char *p, int len, void *va, uint8_t pfxlen, uint8_t max)
{
	u_char		*a = va;
	int		 plen;

	plen = PREFIX_SIZE(pfxlen) - 1;
	if (len < plen || max < plen)
		return -1;

	while (pfxlen > 0) {
		if (pfxlen < 8) {
			*a++ = *p++ & addrmask[pfxlen];
			break;
		} else {
			*a++ = *p++;
			pfxlen -= 8;
		}
	}
	return (plen);
}

static int
extract_prefix_buf(struct ibuf *buf, void *va, uint8_t pfxlen, uint8_t max)
{
	u_char		*a = va;
	unsigned int	 plen;
	uint8_t		 tmp;

	plen = PREFIX_SIZE(pfxlen) - 1;
	if (ibuf_size(buf) < plen || max < plen)
		return -1;

	while (pfxlen > 0) {
		if (ibuf_get_n8(buf, &tmp) == -1)
			return -1;

		if (pfxlen < 8) {
			*a++ = tmp & addrmask[pfxlen];
			break;
		} else {
			*a++ = tmp;
			pfxlen -= 8;
		}
	}
	return (0);
}

int
nlri_get_prefix(struct ibuf *buf, struct bgpd_addr *prefix, uint8_t *prefixlen)
{
	uint8_t	 pfxlen;

	if (ibuf_get_n8(buf, &pfxlen) == -1)
		return (-1);
	if (pfxlen > 32)
		return (-1);

	memset(prefix, 0, sizeof(struct bgpd_addr));
	prefix->aid = AID_INET;

	if (extract_prefix_buf(buf, &prefix->v4, pfxlen,
	    sizeof(prefix->v4)) == -1)
		return (-1);

	*prefixlen = pfxlen;
	return (0);
}

int
nlri_get_prefix6(struct ibuf *buf, struct bgpd_addr *prefix, uint8_t *prefixlen)
{
	uint8_t	pfxlen;

	if (ibuf_get_n8(buf, &pfxlen) == -1)
		return (-1);
	if (pfxlen > 128)
		return (-1);

	memset(prefix, 0, sizeof(struct bgpd_addr));
	prefix->aid = AID_INET6;

	if (extract_prefix_buf(buf, &prefix->v6, pfxlen,
	    sizeof(prefix->v6)) == -1)
		return (-1);

	*prefixlen = pfxlen;
	return (0);
}

int
nlri_get_vpn4(struct ibuf *buf, struct bgpd_addr *prefix,
    uint8_t *prefixlen, int withdraw)
{
	int		 done = 0;
	uint8_t		 pfxlen;

	if (ibuf_get_n8(buf, &pfxlen) == -1)
		return (-1);

	memset(prefix, 0, sizeof(struct bgpd_addr));
	prefix->aid = AID_VPN_IPv4;

	/* label stack */
	do {
		if (prefix->labellen + 3U > sizeof(prefix->labelstack) ||
		    pfxlen < 3 * 8)
			return (-1);
		if (withdraw) {
			/* on withdraw ignore the labelstack all together */
			if (ibuf_skip(buf, 3) == -1)
				return (-1);
			pfxlen -= 3 * 8;
			break;
		}
		if (ibuf_get(buf, &prefix->labelstack[prefix->labellen], 3) ==
		    -1)
			return -1;
		if (prefix->labelstack[prefix->labellen + 2] &
		    BGP_MPLS_BOS)
			done = 1;
		prefix->labellen += 3;
		pfxlen -= 3 * 8;
	} while (!done);

	/* RD */
	if (pfxlen < sizeof(uint64_t) * 8 ||
	    ibuf_get_h64(buf, &prefix->rd) == -1)
		return (-1);
	pfxlen -= sizeof(uint64_t) * 8;

	/* prefix */
	if (pfxlen > 32)
		return (-1);
	if (extract_prefix_buf(buf, &prefix->v4, pfxlen,
	    sizeof(prefix->v4)) == -1)
		return (-1);

	*prefixlen = pfxlen;
	return (0);
}

int
nlri_get_vpn6(struct ibuf *buf, struct bgpd_addr *prefix,
    uint8_t *prefixlen, int withdraw)
{
	int		done = 0;
	uint8_t		pfxlen;

	if (ibuf_get_n8(buf, &pfxlen) == -1)
		return (-1);

	memset(prefix, 0, sizeof(struct bgpd_addr));
	prefix->aid = AID_VPN_IPv6;

	/* label stack */
	do {
		if (prefix->labellen + 3U > sizeof(prefix->labelstack) ||
		    pfxlen < 3 * 8)
			return (-1);
		if (withdraw) {
			/* on withdraw ignore the labelstack all together */
			if (ibuf_skip(buf, 3) == -1)
				return (-1);
			pfxlen -= 3 * 8;
			break;
		}

		if (ibuf_get(buf, &prefix->labelstack[prefix->labellen], 3) ==
		    -1)
			return (-1);
		if (prefix->labelstack[prefix->labellen + 2] &
		    BGP_MPLS_BOS)
			done = 1;
		prefix->labellen += 3;
		pfxlen -= 3 * 8;
	} while (!done);

	/* RD */
	if (pfxlen < sizeof(uint64_t) * 8 ||
	    ibuf_get_h64(buf, &prefix->rd) == -1)
		return (-1);
	pfxlen -= sizeof(uint64_t) * 8;

	/* prefix */
	if (pfxlen > 128)
		return (-1);
	if (extract_prefix_buf(buf, &prefix->v6, pfxlen,
	    sizeof(prefix->v6)) == -1)
		return (-1);

	*prefixlen = pfxlen;
	return (0);
}

int
nlri_get_evpn(struct ibuf *buf, struct bgpd_addr *prefix,
    uint8_t *prefixlen)
{
	struct ibuf	evpnbuf;
	uint8_t		nlrilen, type, pfxlen = 0, maclen = 0;

	if (ibuf_get_n8(buf, &type) == -1)
		return (-1);
	if (ibuf_get_n8(buf, &nlrilen) == -1)
		return (-1);

	memset(prefix, 0, sizeof(struct bgpd_addr));
	prefix->aid = AID_EVPN;

	switch (type) {
	case EVPN_ROUTE_TYPE_2:
		if (ibuf_get_ibuf(buf, nlrilen, &evpnbuf) == -1)
			return (-1);
		prefix->evpn.type = EVPN_ROUTE_TYPE_2;
		/* RD */
		if (ibuf_get_h64(&evpnbuf, &prefix->rd) == -1)
			return (-1);
		/* ESI */
		if (ibuf_get(&evpnbuf, &prefix->evpn.esi,
		    sizeof(prefix->evpn.esi)) == -1)
			return (-1);
		/* Ethernet Tag */
		if (ibuf_get_h32(&evpnbuf, &prefix->evpn.ethtag) == -1)
			return (-1);
		/* MAC length */
		if (ibuf_get_n8(&evpnbuf, &maclen) == -1)
			return (-1);
		if (maclen != 48)
			return (-1);
		/* MAC address */
		if (ibuf_get(&evpnbuf, &prefix->evpn.mac,
		    sizeof(prefix->evpn.mac)) == -1)
			return (-1);
		/* Prefix length */
		if (ibuf_get_n8(&evpnbuf, &pfxlen) == -1)
			return (-1);
		/* Destination */
		if (pfxlen == 0) {
			/* nothing */
		} else if (pfxlen == 32) {
			prefix->evpn.aid = AID_INET;
			if (ibuf_get(&evpnbuf, &prefix->evpn.v4,
			    sizeof(prefix->evpn.v4)) == -1)
				return (-1);
		} else if (pfxlen == 128) {
			prefix->evpn.aid = AID_INET6;
			if (ibuf_get(&evpnbuf, &prefix->evpn.v6,
			    sizeof(prefix->evpn.v6)) == -1)
				return (-1);
		} else
			return (-1);
		/* VNI */
		if (ibuf_size(&evpnbuf) != 3 && ibuf_size(&evpnbuf) != 6)
			return (-1);
		prefix->labellen = ibuf_size(&evpnbuf);
		if (ibuf_get(&evpnbuf, prefix->labelstack,
		    prefix->labellen) == -1)
			return (-1);
		break;
	case EVPN_ROUTE_TYPE_3:
		if (ibuf_get_ibuf(buf, nlrilen, &evpnbuf) == -1)
			return (-1);
		prefix->evpn.type = EVPN_ROUTE_TYPE_3;
		/* RD */
		if (ibuf_get_h64(&evpnbuf, &prefix->rd) == -1)
			return (-1);
		/* Ethernet Tag */
		if (ibuf_get_h32(&evpnbuf, &prefix->evpn.ethtag) == -1)
			return (-1);
		/* Prefix length */
		if (ibuf_get_n8(&evpnbuf, &pfxlen) == -1)
			return (-1);
		/* Destination */
		if (pfxlen == 32) {
			prefix->evpn.aid = AID_INET;
			if (ibuf_get(&evpnbuf, &prefix->evpn.v4,
			    sizeof(prefix->evpn.v4)) == -1)
				return (-1);
		} else if (pfxlen == 128) {
			prefix->evpn.aid = AID_INET6;
			if (ibuf_get(&evpnbuf, &prefix->evpn.v6,
			    sizeof(prefix->evpn.v6)) == -1)
				return (-1);
		} else
			return (-1);
		if (ibuf_size(&evpnbuf) != 0)
			return (-1);
		break;
	default:
		return (-1);
	}

	*prefixlen = pfxlen;
	return (0);
}

static in_addr_t
prefixlen2mask(uint8_t prefixlen)
{
	if (prefixlen == 0)
		return (0);

	return (0xffffffff << (32 - prefixlen));
}

/*
 * This function will have undefined behaviour if the passed in prefixlen is
 * too large for the respective bgpd_addr address family.
 */
int
prefix_compare(const struct bgpd_addr *a, const struct bgpd_addr *b,
    int prefixlen)
{
	in_addr_t	mask, aa, ba;
	int		i;
	uint8_t		m;

	if (a->aid != b->aid)
		return (a->aid - b->aid);

	switch (a->aid) {
	case AID_VPN_IPv4:
		if (be64toh(a->rd) > be64toh(b->rd))
			return (1);
		if (be64toh(a->rd) < be64toh(b->rd))
			return (-1);
		/* FALLTHROUGH */
	case AID_INET:
		if (prefixlen == 0)
			return (0);
		if (prefixlen > 32)
			return (-1);
		mask = htonl(prefixlen2mask(prefixlen));
		aa = ntohl(a->v4.s_addr & mask);
		ba = ntohl(b->v4.s_addr & mask);
		if (aa > ba)
			return (1);
		if (aa < ba)
			return (-1);
		break;
	case AID_VPN_IPv6:
		if (be64toh(a->rd) > be64toh(b->rd))
			return (1);
		if (be64toh(a->rd) < be64toh(b->rd))
			return (-1);
		/* FALLTHROUGH */
	case AID_INET6:
		if (prefixlen == 0)
			return (0);
		if (prefixlen > 128)
			return (-1);
		for (i = 0; i < prefixlen / 8; i++)
			if (a->v6.s6_addr[i] != b->v6.s6_addr[i])
				return (a->v6.s6_addr[i] - b->v6.s6_addr[i]);
		i = prefixlen % 8;
		if (i) {
			m = 0xff00 >> i;
			if ((a->v6.s6_addr[prefixlen / 8] & m) !=
			    (b->v6.s6_addr[prefixlen / 8] & m))
				return ((a->v6.s6_addr[prefixlen / 8] & m) -
				    (b->v6.s6_addr[prefixlen / 8] & m));
		}
		break;
	default:
		return (-1);
	}

	if (a->aid == AID_VPN_IPv4 || a->aid == AID_VPN_IPv6) {
		if (a->labellen > b->labellen)
			return (1);
		if (a->labellen < b->labellen)
			return (-1);
		return (memcmp(a->labelstack, b->labelstack, a->labellen));
	}
	return (0);

}

void
inet4applymask(struct in_addr *dest, const struct in_addr *src, int prefixlen)
{
	struct in_addr mask;

	mask.s_addr = htonl(prefixlen2mask(prefixlen));
	dest->s_addr = src->s_addr & mask.s_addr;
}

void
inet6applymask(struct in6_addr *dest, const struct in6_addr *src, int prefixlen)
{
	struct in6_addr	mask;
	int		i;

	memset(&mask, 0, sizeof(mask));
	for (i = 0; i < prefixlen / 8; i++)
		mask.s6_addr[i] = 0xff;
	i = prefixlen % 8;
	if (i)
		mask.s6_addr[prefixlen / 8] = 0xff00 >> i;

	for (i = 0; i < 16; i++)
		dest->s6_addr[i] = src->s6_addr[i] & mask.s6_addr[i];
}

void
applymask(struct bgpd_addr *dest, const struct bgpd_addr *src, int prefixlen)
{
	*dest = *src;
	switch (src->aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		inet4applymask(&dest->v4, &src->v4, prefixlen);
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		inet6applymask(&dest->v6, &src->v6, prefixlen);
		break;
	}
}

/* address family translation functions */
const struct aid aid_vals[AID_MAX] = AID_VALS;

const char *
aid2str(uint8_t aid)
{
	if (aid < AID_MAX)
		return (aid_vals[aid].name);
	return ("unknown AID");
}

int
aid2afi(uint8_t aid, uint16_t *afi, uint8_t *safi)
{
	if (aid != AID_UNSPEC && aid < AID_MAX) {
		*afi = aid_vals[aid].afi;
		*safi = aid_vals[aid].safi;
		return (0);
	}
	return (-1);
}

int
afi2aid(uint16_t afi, uint8_t safi, uint8_t *aid)
{
	uint8_t i;

	for (i = AID_MIN; i < AID_MAX; i++)
		if (aid_vals[i].afi == afi && aid_vals[i].safi == safi) {
			*aid = i;
			return (0);
		}

	return (-1);
}

sa_family_t
aid2af(uint8_t aid)
{
	if (aid < AID_MAX)
		return (aid_vals[aid].af);
	return (AF_UNSPEC);
}

int
af2aid(sa_family_t af, uint8_t safi, uint8_t *aid)
{
	uint8_t i;

	if (safi == 0) /* default to unicast subclass */
		safi = SAFI_UNICAST;

	for (i = AID_UNSPEC; i < AID_MAX; i++)
		if (aid_vals[i].af == af && aid_vals[i].safi == safi) {
			*aid = i;
			return (0);
		}

	return (-1);
}

static socklen_t
addr2sa_in6(struct sockaddr_in6 *sin6, struct in6_addr in6, uint16_t port,
    uint32_t scope_id)
{
	sin6->sin6_family = AF_INET6;
	memcpy(&sin6->sin6_addr, &in6, sizeof(sin6->sin6_addr));
	sin6->sin6_port = htons(port);
	sin6->sin6_scope_id = scope_id;
	return (sizeof(struct sockaddr_in6));
}

static socklen_t
addr2sa_in(struct sockaddr_in *sin, struct in_addr in, uint16_t port)
{
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = in.s_addr;
	sin->sin_port = htons(port);
	return (sizeof(struct sockaddr_in));
}

/*
 * Convert a struct bgpd_addr into a struct sockaddr. For VPN addresses
 * the included label stack is ignored and needs to be handled by the caller.
 */
struct sockaddr *
addr2sa(const struct bgpd_addr *addr, uint16_t port, socklen_t *len)
{
	static struct sockaddr_storage	 ss;
	struct sockaddr_in		*sa_in = (struct sockaddr_in *)&ss;
	struct sockaddr_in6		*sa_in6 = (struct sockaddr_in6 *)&ss;

	if (addr == NULL || addr->aid == AID_UNSPEC)
		return (NULL);

	memset(&ss, 0, sizeof(ss));
	switch (addr->aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		*len = addr2sa_in(sa_in, addr->v4, port);
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		*len = addr2sa_in6(sa_in6, addr->v6, port, addr->scope_id);
		break;
	case AID_EVPN:
		if (addr->evpn.aid == AID_INET)
			*len = addr2sa_in(sa_in, addr->evpn.v4, port);
		else if (addr->evpn.aid == AID_INET6)
			*len = addr2sa_in6(sa_in6, addr->evpn.v6, port,
			    addr->scope_id);
		else {
			*len = 0;
			return (NULL);
		}
		break;
	case AID_FLOWSPECv4:
	case AID_FLOWSPECv6:
	default:
		return (NULL);
	}

	return ((struct sockaddr *)&ss);
}

void
sa2addr(struct sockaddr *sa, struct bgpd_addr *addr, uint16_t *port)
{
	struct sockaddr_in		*sa_in = (struct sockaddr_in *)sa;
	struct sockaddr_in6		*sa_in6 = (struct sockaddr_in6 *)sa;

	memset(addr, 0, sizeof(*addr));
	switch (sa->sa_family) {
	case AF_INET:
		addr->aid = AID_INET;
		memcpy(&addr->v4, &sa_in->sin_addr, sizeof(addr->v4));
		if (port)
			*port = ntohs(sa_in->sin_port);
		break;
	case AF_INET6:
		addr->aid = AID_INET6;
#ifdef __KAME__
		/*
		 * XXX thanks, KAME, for this ugliness...
		 * adopted from route/show.c
		 */
		if ((IN6_IS_ADDR_LINKLOCAL(&sa_in6->sin6_addr) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(&sa_in6->sin6_addr) ||
		    IN6_IS_ADDR_MC_NODELOCAL(&sa_in6->sin6_addr)) &&
		    sa_in6->sin6_scope_id == 0) {
			uint16_t tmp16;
			memcpy(&tmp16, &sa_in6->sin6_addr.s6_addr[2],
			    sizeof(tmp16));
			sa_in6->sin6_scope_id = ntohs(tmp16);
			sa_in6->sin6_addr.s6_addr[2] = 0;
			sa_in6->sin6_addr.s6_addr[3] = 0;
		}
#endif
		memcpy(&addr->v6, &sa_in6->sin6_addr, sizeof(addr->v6));
		addr->scope_id = sa_in6->sin6_scope_id; /* I hate v6 */
		if (port)
			*port = ntohs(sa_in6->sin6_port);
		break;
	}
}

const char *
get_baudrate(unsigned long long baudrate, char *unit)
{
	static char bbuf[16];
	const unsigned long long kilo = 1000;
	const unsigned long long mega = 1000ULL * kilo;
	const unsigned long long giga = 1000ULL * mega;

	if (baudrate > giga)
		snprintf(bbuf, sizeof(bbuf), "%llu G%s",
		    baudrate / giga, unit);
	else if (baudrate > mega)
		snprintf(bbuf, sizeof(bbuf), "%llu M%s",
		    baudrate / mega, unit);
	else if (baudrate > kilo)
		snprintf(bbuf, sizeof(bbuf), "%llu K%s",
		    baudrate / kilo, unit);
	else
		snprintf(bbuf, sizeof(bbuf), "%llu %s",
		    baudrate, unit);

	return (bbuf);
}
