/*
 * xdp-util.h -- set of xdp related helpers
 *
 * Copyright (c) 2024, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef XDP_UTIL_H
#define XDP_UTIL_H

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <linux/udp.h>

/*
 * Get number of combined or rx queues available on ifname.
 */
int ethtool_channels_get(char const *ifname);

/*
 * Set capabilities to only include the ones needed for using AF_XDP/
 */
void set_caps(int unset_setid_caps);

/*
 * Add u16 to checksum value and preserve one's complement sum.
 */
static inline __sum16 csum16_add(__sum16 csum, __be16 addend) {
	uint16_t res = (uint16_t)csum;

	res += (__u16)addend;
	return (__sum16)(res + (res < (__u16)addend));
}

/*
 * Subtract u16 from checksum value and preserve one's complement sum.
 */
static inline __sum16 csum16_sub(__sum16 csum, __be16 addend) {
	return csum16_add(csum, ~addend);
}

/*
 * Replace u16 from checksum value and preserve one's complement sum.
 */
static inline void csum16_replace(__sum16 *sum, __be16 old, __be16 new) {
	*sum = ~csum16_add(csum16_sub(~(*sum), old), new);
}

/*
 * Sum up _data_len amount of 16-bit words in _data and add to result.
 */
static inline void csum_add_data(uint32_t *result,
                          const void *_data,
                          uint32_t len) {
	const uint16_t *data = _data;
	while (len > 1) {
		*result += *data++;
		len -= 2;
	}
	if (len)
		*result += *data & 0xff;
		// *result += *(uint8_t *)data;
}

/*
 * Add single 16-bit words to result.
 */
static inline void csum_add_u16(uint32_t *result, uint16_t x) { *result += x; }

/*
 * Apply one's complement to result.
 */
static inline void csum_reduce(uint32_t *result) {
	while (*result >> 16)
		*result = (*result & 0xffff) + (*result >> 16);
}

/*
 * Calculate UDP checksum with IPv6 pseudo-header
 */
static inline uint16_t calc_csum_udp6(struct udphdr *udp, struct ipv6hdr *ipv6) {
	uint32_t sum = 0;
	sum += udp->len;
	sum += htons(IPPROTO_UDP);
	csum_add_data(&sum, &ipv6->saddr, sizeof(ipv6->saddr));
	csum_add_data(&sum, &ipv6->daddr, sizeof(ipv6->daddr));

	udp->check = 0;
	csum_add_data(&sum, udp, ntohs(udp->len));
	/* maybe restore previous checksum to remove side effects? */

	// reduces sum to 16bit
	csum_reduce(&sum);

	if (sum != 0xffff)
		return (uint16_t) ~sum;
	else
		return (uint16_t) sum;
}

/*
 * Calculate UDP checksum with IPv4 pseudo-header
 */
static inline uint16_t calc_csum_udp4(struct udphdr *udp, struct iphdr *ipv4) {
	uint32_t sum = 0;
	sum += udp->len;
	sum += htons(IPPROTO_UDP);
	csum_add_data(&sum, &ipv4->saddr, sizeof(ipv4->saddr));
	csum_add_data(&sum, &ipv4->daddr, sizeof(ipv4->daddr));

	udp->check = 0;
	csum_add_data(&sum, udp, ntohs(udp->len));
	/* maybe restore previous checksum to remove side effects? */

	// reduces sum to 16bit
	csum_reduce(&sum);

	if (sum != 0xffff)
		return (uint16_t) ~sum;
	else
		return (uint16_t) sum;
}

#endif /* XDP_UTIL_H */
