/*
 ** Copyright (c) 2015, Asim Jamshed, Robin Sommer, Seth Hall
 ** and the International Computer Science Institute. All rights reserved.
 **
 ** Redistribution and use in source and binary forms, with or without
 ** modification, are permitted provided that the following conditions are met:
 **
 ** (1) Redistributions of source code must retain the above copyright
 **     notice, this list of conditions and the following disclaimer.
 **
 ** (2) Redistributions in binary form must reproduce the above copyright
 **     notice, this list of conditions and the following disclaimer in the
 **     documentation and/or other materials provided with the distribution.
 **
 **
 ** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 ** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 ** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ** ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 ** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 ** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 ** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 ** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 ** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 ** POSSIBILITY OF SUCH DAMAGE.
 **/
/* $FreeBSD$ */
/* for func prototypes */
#include "pkt_hash.h"

/* Make Linux headers choose BSD versions of some of the data structures */
#define __FAVOR_BSD

/* for types */
#include <sys/types.h>
/* for [n/h]to[h/n][ls] */
#include <netinet/in.h>
/* iphdr */
#include <netinet/ip.h>
/* ipv6hdr */
#include <netinet/ip6.h>
/* tcphdr */
#include <netinet/tcp.h>
/* udphdr */
#include <netinet/udp.h>
/* eth hdr */
#include <net/ethernet.h>
/* for memset */
#include <string.h>

#include <stdio.h>
#include <assert.h>

//#include <libnet.h>
/*---------------------------------------------------------------------*/
/**
 *  * The cache table is used to pick a nice seed for the hash value. It is
 *   * built only once when sym_hash_fn is called for the very first time
 *    */
static void
build_sym_key_cache(uint32_t *cache, int cache_len)
{
	static const uint8_t key[] = { 0x50, 0x6d };

        uint32_t result = (((uint32_t)key[0]) << 24) |
                (((uint32_t)key[1]) << 16) |
                (((uint32_t)key[0]) << 8)  |
                ((uint32_t)key[1]);

        uint32_t idx = 32;
        int i;

        for (i = 0; i < cache_len; i++, idx++) {
                uint8_t shift = (idx % 8);
                uint32_t bit;

                cache[i] = result;
                bit = ((key[(idx/8) & 1] << shift) & 0x80) ? 1 : 0;
                result = ((result << 1) | bit);
        }
}

static void
build_byte_cache(uint32_t byte_cache[256][4])
{
#define KEY_CACHE_LEN			96
	int i, j, k;
	uint32_t key_cache[KEY_CACHE_LEN];

	build_sym_key_cache(key_cache, KEY_CACHE_LEN);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 256; j++) {
			uint8_t b = j;
			byte_cache[j][i] = 0;
			for (k = 0; k < 8; k++) {
				if (b & 0x80)
					byte_cache[j][i] ^= key_cache[8 * i + k];
				b <<= 1U;
			}
		}
	}
}


/*---------------------------------------------------------------------*/
/**
 ** Computes symmetric hash based on the 4-tuple header data
 **/
static uint32_t
sym_hash_fn(uint32_t sip, uint32_t dip, uint16_t sp, uint32_t dp)
{
	uint32_t rc = 0;
	static int first_time = 1;
	static uint32_t byte_cache[256][4];
	uint8_t *sip_b = (uint8_t *)&sip,
		*dip_b = (uint8_t *)&dip,
		*sp_b  = (uint8_t *)&sp,
		*dp_b  = (uint8_t *)&dp;

	if (first_time) {
		build_byte_cache(byte_cache);
		first_time = 0;
	}

	rc = byte_cache[sip_b[3]][0] ^
	     byte_cache[sip_b[2]][1] ^
	     byte_cache[sip_b[1]][2] ^
	     byte_cache[sip_b[0]][3] ^
	     byte_cache[dip_b[3]][0] ^
	     byte_cache[dip_b[2]][1] ^
	     byte_cache[dip_b[1]][2] ^
	     byte_cache[dip_b[0]][3] ^
	     byte_cache[sp_b[1]][0] ^
	     byte_cache[sp_b[0]][1] ^
	     byte_cache[dp_b[1]][2] ^
	     byte_cache[dp_b[0]][3];

	return rc;
}
static uint32_t decode_gre_hash(const uint8_t *, uint8_t, uint8_t);
/*---------------------------------------------------------------------*/
/**
 ** Parser + hash function for the IPv4 packet
 **/
static uint32_t
decode_ip_n_hash(struct ip *iph, uint8_t hash_split, uint8_t seed)
{
	uint32_t rc = 0;

	if (hash_split == 2) {
		rc = sym_hash_fn(ntohl(iph->ip_src.s_addr),
			ntohl(iph->ip_dst.s_addr),
			ntohs(0xFFFD) + seed,
			ntohs(0xFFFE) + seed);
	} else {
		struct tcphdr *tcph = NULL;
		struct udphdr *udph = NULL;

		switch (iph->ip_p) {
		case IPPROTO_TCP:
			tcph = (struct tcphdr *)((uint8_t *)iph + (iph->ip_hl<<2));
			rc = sym_hash_fn(ntohl(iph->ip_src.s_addr),
					 ntohl(iph->ip_dst.s_addr),
					 ntohs(tcph->th_sport) + seed,
					 ntohs(tcph->th_dport) + seed);
			break;
		case IPPROTO_UDP:
			udph = (struct udphdr *)((uint8_t *)iph + (iph->ip_hl<<2));
			rc = sym_hash_fn(ntohl(iph->ip_src.s_addr),
					 ntohl(iph->ip_dst.s_addr),
					 ntohs(udph->uh_sport) + seed,
					 ntohs(udph->uh_dport) + seed);
			break;
		case IPPROTO_IPIP:
			/* tunneling */
			rc = decode_ip_n_hash((struct ip *)((uint8_t *)iph + (iph->ip_hl<<2)),
					      hash_split, seed);
			break;
		case IPPROTO_GRE:
			rc = decode_gre_hash((uint8_t *)iph + (iph->ip_hl<<2),
					hash_split, seed);
			break;
		case IPPROTO_ICMP:
		case IPPROTO_ESP:
		case IPPROTO_PIM:
		case IPPROTO_IGMP:
		default:
			/*
			 ** the hash strength (although weaker but) should still hold
			 ** even with 2 fields
			 **/
			rc = sym_hash_fn(ntohl(iph->ip_src.s_addr),
					 ntohl(iph->ip_dst.s_addr),
					 ntohs(0xFFFD) + seed,
					 ntohs(0xFFFE) + seed);
			break;
		}
	}
	return rc;
}
/*---------------------------------------------------------------------*/
/**
 ** Parser + hash function for the IPv6 packet
 **/
static uint32_t
decode_ipv6_n_hash(struct ip6_hdr *ipv6h, uint8_t hash_split, uint8_t seed)
{
	uint32_t saddr, daddr;
	uint32_t rc = 0;

	/* Get only the first 4 octets */
	saddr = ipv6h->ip6_src.s6_addr[0] |
		(ipv6h->ip6_src.s6_addr[1] << 8) |
		(ipv6h->ip6_src.s6_addr[2] << 16) |
		(ipv6h->ip6_src.s6_addr[3] << 24);
	daddr = ipv6h->ip6_dst.s6_addr[0] |
		(ipv6h->ip6_dst.s6_addr[1] << 8) |
		(ipv6h->ip6_dst.s6_addr[2] << 16) |
		(ipv6h->ip6_dst.s6_addr[3] << 24);

	if (hash_split == 2) {
		rc = sym_hash_fn(ntohl(saddr),
				 ntohl(daddr),
				 ntohs(0xFFFD) + seed,
				 ntohs(0xFFFE) + seed);
	} else {
		struct tcphdr *tcph = NULL;
		struct udphdr *udph = NULL;

		switch(ntohs(ipv6h->ip6_ctlun.ip6_un1.ip6_un1_nxt)) {
		case IPPROTO_TCP:
			tcph = (struct tcphdr *)(ipv6h + 1);
			rc = sym_hash_fn(ntohl(saddr),
					 ntohl(daddr),
					 ntohs(tcph->th_sport) + seed,
					 ntohs(tcph->th_dport) + seed);
			break;
		case IPPROTO_UDP:
			udph = (struct udphdr *)(ipv6h + 1);
			rc = sym_hash_fn(ntohl(saddr),
					 ntohl(daddr),
					 ntohs(udph->uh_sport) + seed,
					 ntohs(udph->uh_dport) + seed);
			break;
		case IPPROTO_IPIP:
			/* tunneling */
			rc = decode_ip_n_hash((struct ip *)(ipv6h + 1),
					      hash_split, seed);
			break;
		case IPPROTO_IPV6:
			/* tunneling */
			rc = decode_ipv6_n_hash((struct ip6_hdr *)(ipv6h + 1),
						hash_split, seed);
			break;
		case IPPROTO_GRE:
			rc = decode_gre_hash((uint8_t *)(ipv6h + 1), hash_split, seed);
			break;
		case IPPROTO_ICMP:
		case IPPROTO_ESP:
		case IPPROTO_PIM:
		case IPPROTO_IGMP:
		default:
			/*
			 ** the hash strength (although weaker but) should still hold
			 ** even with 2 fields
			 **/
			rc = sym_hash_fn(ntohl(saddr),
					 ntohl(daddr),
					 ntohs(0xFFFD) + seed,
					 ntohs(0xFFFE) + seed);
		}
	}
	return rc;
}
/*---------------------------------------------------------------------*/
/**
 *  *  A temp solution while hash for other protocols are filled...
 *   * (See decode_vlan_n_hash & pkt_hdr_hash functions).
 *    */
static uint32_t
decode_others_n_hash(struct ether_header *ethh, uint8_t seed)
{
	uint32_t saddr, daddr, rc;

	saddr = ethh->ether_shost[5] |
		(ethh->ether_shost[4] << 8) |
		(ethh->ether_shost[3] << 16) |
		(ethh->ether_shost[2] << 24);
	daddr = ethh->ether_dhost[5] |
		(ethh->ether_dhost[4] << 8) |
		(ethh->ether_dhost[3] << 16) |
		(ethh->ether_dhost[2] << 24);

	rc = sym_hash_fn(ntohl(saddr),
			 ntohl(daddr),
			 ntohs(0xFFFD) + seed,
			 ntohs(0xFFFE) + seed);

	return rc;
}
/*---------------------------------------------------------------------*/
/**
 ** Parser + hash function for VLAN packet
 **/
static inline uint32_t
decode_vlan_n_hash(struct ether_header *ethh, uint8_t hash_split, uint8_t seed)
{
	uint32_t rc = 0;
	struct vlanhdr *vhdr = (struct vlanhdr *)(ethh + 1);

	switch (ntohs(vhdr->proto)) {
	case ETHERTYPE_IP:
		rc = decode_ip_n_hash((struct ip *)(vhdr + 1),
				      hash_split, seed);
		break;
	case ETHERTYPE_IPV6:
		rc = decode_ipv6_n_hash((struct ip6_hdr *)(vhdr + 1),
					hash_split, seed);
		break;
	case ETHERTYPE_ARP:
	default:
		/* others */
		rc = decode_others_n_hash(ethh, seed);
		break;
	}
	return rc;
}

/*---------------------------------------------------------------------*/
/**
 ** General parser + hash function...
 **/
uint32_t
pkt_hdr_hash(const unsigned char *buffer, uint8_t hash_split, uint8_t seed)
{
	uint32_t rc = 0;
	struct ether_header *ethh = (struct ether_header *)buffer;

	switch (ntohs(ethh->ether_type)) {
	case ETHERTYPE_IP:
		rc = decode_ip_n_hash((struct ip *)(ethh + 1),
				      hash_split, seed);
		break;
	case ETHERTYPE_IPV6:
		rc = decode_ipv6_n_hash((struct ip6_hdr *)(ethh + 1),
					hash_split, seed);
		break;
	case ETHERTYPE_VLAN:
		rc = decode_vlan_n_hash(ethh, hash_split, seed);
		break;
	case ETHERTYPE_ARP:
	default:
		/* others */
		rc = decode_others_n_hash(ethh, seed);
		break;
	}

	return rc;
}

/*---------------------------------------------------------------------*/
/**
 ** Parser + hash function for the GRE packet
 **/
static uint32_t
decode_gre_hash(const uint8_t *grehdr, uint8_t hash_split, uint8_t seed)
{
	uint32_t rc = 0;
	int len = 4 + 2 * (!!(*grehdr & 1) + /* Checksum */
			   !!(*grehdr & 2) + /* Routing */
			   !!(*grehdr & 4) + /* Key */
			   !!(*grehdr & 8)); /* Sequence Number */
	uint16_t proto = ntohs(*(uint16_t *)(void *)(grehdr + 2));

	switch (proto) {
	case ETHERTYPE_IP:
		rc = decode_ip_n_hash((struct ip *)(grehdr + len),
				      hash_split, seed);
		break;
	case ETHERTYPE_IPV6:
		rc = decode_ipv6_n_hash((struct ip6_hdr *)(grehdr + len),
					hash_split, seed);
		break;
	case 0x6558: /* Transparent Ethernet Bridging */
		rc = pkt_hdr_hash(grehdr + len, hash_split, seed);
		break;
	default:
		/* others */
		break;
	}
	return rc;
}
/*---------------------------------------------------------------------*/

