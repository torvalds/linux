// SPDX-License-Identifier: GPL-2.0-only
/**
 * Authors:
 * (C) 2020 Alexander Aring <alex.aring@gmail.com>
 */

#include <net/ipv6.h>
#include <net/rpl.h>

#define IPV6_PFXTAIL_LEN(x) (sizeof(struct in6_addr) - (x))

static void ipv6_rpl_addr_decompress(struct in6_addr *dst,
				     const struct in6_addr *daddr,
				     const void *post, unsigned char pfx)
{
	memcpy(dst, daddr, pfx);
	memcpy(&dst->s6_addr[pfx], post, IPV6_PFXTAIL_LEN(pfx));
}

static void ipv6_rpl_addr_compress(void *dst, const struct in6_addr *addr,
				   unsigned char pfx)
{
	memcpy(dst, &addr->s6_addr[pfx], IPV6_PFXTAIL_LEN(pfx));
}

static void *ipv6_rpl_segdata_pos(const struct ipv6_rpl_sr_hdr *hdr, int i)
{
	return (void *)&hdr->rpl_segdata[i * IPV6_PFXTAIL_LEN(hdr->cmpri)];
}

size_t ipv6_rpl_srh_size(unsigned char n, unsigned char cmpri,
			 unsigned char cmpre)
{
	return (n * IPV6_PFXTAIL_LEN(cmpri)) + IPV6_PFXTAIL_LEN(cmpre);
}

void ipv6_rpl_srh_decompress(struct ipv6_rpl_sr_hdr *outhdr,
			     const struct ipv6_rpl_sr_hdr *inhdr,
			     const struct in6_addr *daddr, unsigned char n)
{
	int i;

	outhdr->nexthdr = inhdr->nexthdr;
	outhdr->hdrlen = (((n + 1) * sizeof(struct in6_addr)) >> 3);
	outhdr->pad = 0;
	outhdr->type = inhdr->type;
	outhdr->segments_left = inhdr->segments_left;
	outhdr->cmpri = 0;
	outhdr->cmpre = 0;

	for (i = 0; i < n; i++)
		ipv6_rpl_addr_decompress(&outhdr->rpl_segaddr[i], daddr,
					 ipv6_rpl_segdata_pos(inhdr, i),
					 inhdr->cmpri);

	ipv6_rpl_addr_decompress(&outhdr->rpl_segaddr[n], daddr,
				 ipv6_rpl_segdata_pos(inhdr, n),
				 inhdr->cmpre);
}

static unsigned char ipv6_rpl_srh_calc_cmpri(const struct ipv6_rpl_sr_hdr *inhdr,
					     const struct in6_addr *daddr,
					     unsigned char n)
{
	unsigned char plen;
	int i;

	for (plen = 0; plen < sizeof(*daddr); plen++) {
		for (i = 0; i < n; i++) {
			if (daddr->s6_addr[plen] !=
			    inhdr->rpl_segaddr[i].s6_addr[plen])
				return plen;
		}
	}

	return plen;
}

static unsigned char ipv6_rpl_srh_calc_cmpre(const struct in6_addr *daddr,
					     const struct in6_addr *last_segment)
{
	unsigned int plen;

	for (plen = 0; plen < sizeof(*daddr); plen++) {
		if (daddr->s6_addr[plen] != last_segment->s6_addr[plen])
			break;
	}

	return plen;
}

void ipv6_rpl_srh_compress(struct ipv6_rpl_sr_hdr *outhdr,
			   const struct ipv6_rpl_sr_hdr *inhdr,
			   const struct in6_addr *daddr, unsigned char n)
{
	unsigned char cmpri, cmpre;
	size_t seglen;
	int i;

	cmpri = ipv6_rpl_srh_calc_cmpri(inhdr, daddr, n);
	cmpre = ipv6_rpl_srh_calc_cmpre(daddr, &inhdr->rpl_segaddr[n]);

	outhdr->nexthdr = inhdr->nexthdr;
	seglen = (n * IPV6_PFXTAIL_LEN(cmpri)) + IPV6_PFXTAIL_LEN(cmpre);
	outhdr->hdrlen = seglen >> 3;
	if (seglen & 0x7) {
		outhdr->hdrlen++;
		outhdr->pad = 8 - (seglen & 0x7);
	} else {
		outhdr->pad = 0;
	}
	outhdr->type = inhdr->type;
	outhdr->segments_left = inhdr->segments_left;
	outhdr->cmpri = cmpri;
	outhdr->cmpre = cmpre;

	for (i = 0; i < n; i++)
		ipv6_rpl_addr_compress(ipv6_rpl_segdata_pos(outhdr, i),
				       &inhdr->rpl_segaddr[i], cmpri);

	ipv6_rpl_addr_compress(ipv6_rpl_segdata_pos(outhdr, n),
			       &inhdr->rpl_segaddr[n], cmpre);
}
