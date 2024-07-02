// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#ifndef _TEST_TCP_SYNCOOKIE_H
#define _TEST_TCP_SYNCOOKIE_H

#define __packed __attribute__((__packed__))
#define __force

#define ARRAY_SIZE(arr)	(sizeof(arr) / sizeof((arr)[0]))

#define swap(a, b)				\
	do {					\
		typeof(a) __tmp = (a);		\
		(a) = (b);			\
		(b) = __tmp;			\
	} while (0)

#define swap_array(a, b)				\
	do {						\
		typeof(a) __tmp[sizeof(a)];		\
		__builtin_memcpy(__tmp, a, sizeof(a));	\
		__builtin_memcpy(a, b, sizeof(a));	\
		__builtin_memcpy(b, __tmp, sizeof(a));	\
	} while (0)

/* asm-generic/unaligned.h */
#define __get_unaligned_t(type, ptr) ({						\
	const struct { type x; } __packed * __pptr = (typeof(__pptr))(ptr);	\
	__pptr->x;								\
})

#define get_unaligned(ptr) __get_unaligned_t(typeof(*(ptr)), (ptr))

static inline u16 get_unaligned_be16(const void *p)
{
	return bpf_ntohs(__get_unaligned_t(__be16, p));
}

static inline u32 get_unaligned_be32(const void *p)
{
	return bpf_ntohl(__get_unaligned_t(__be32, p));
}

/* lib/checksum.c */
static inline u32 from64to32(u64 x)
{
	/* add up 32-bit and 32-bit for 32+c bit */
	x = (x & 0xffffffff) + (x >> 32);
	/* add up carry.. */
	x = (x & 0xffffffff) + (x >> 32);
	return (u32)x;
}

static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto, __wsum sum)
{
	unsigned long long s = (__force u32)sum;

	s += (__force u32)saddr;
	s += (__force u32)daddr;
#ifdef __BIG_ENDIAN
	s += proto + len;
#else
	s += (proto + len) << 8;
#endif
	return (__force __wsum)from64to32(s);
}

/* asm-generic/checksum.h */
static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return (__force __sum16)~sum;
}

static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len,
					__u8 proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/* net/ipv6/ip6_checksum.c */
static inline __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
				      const struct in6_addr *daddr,
				      __u32 len, __u8 proto, __wsum csum)
{
	int carry;
	__u32 ulen;
	__u32 uproto;
	__u32 sum = (__force u32)csum;

	sum += (__force u32)saddr->in6_u.u6_addr32[0];
	carry = (sum < (__force u32)saddr->in6_u.u6_addr32[0]);
	sum += carry;

	sum += (__force u32)saddr->in6_u.u6_addr32[1];
	carry = (sum < (__force u32)saddr->in6_u.u6_addr32[1]);
	sum += carry;

	sum += (__force u32)saddr->in6_u.u6_addr32[2];
	carry = (sum < (__force u32)saddr->in6_u.u6_addr32[2]);
	sum += carry;

	sum += (__force u32)saddr->in6_u.u6_addr32[3];
	carry = (sum < (__force u32)saddr->in6_u.u6_addr32[3]);
	sum += carry;

	sum += (__force u32)daddr->in6_u.u6_addr32[0];
	carry = (sum < (__force u32)daddr->in6_u.u6_addr32[0]);
	sum += carry;

	sum += (__force u32)daddr->in6_u.u6_addr32[1];
	carry = (sum < (__force u32)daddr->in6_u.u6_addr32[1]);
	sum += carry;

	sum += (__force u32)daddr->in6_u.u6_addr32[2];
	carry = (sum < (__force u32)daddr->in6_u.u6_addr32[2]);
	sum += carry;

	sum += (__force u32)daddr->in6_u.u6_addr32[3];
	carry = (sum < (__force u32)daddr->in6_u.u6_addr32[3]);
	sum += carry;

	ulen = (__force u32)bpf_htonl((__u32)len);
	sum += ulen;
	carry = (sum < ulen);
	sum += carry;

	uproto = (__force u32)bpf_htonl(proto);
	sum += uproto;
	carry = (sum < uproto);
	sum += carry;

	return csum_fold((__force __wsum)sum);
}
#endif
