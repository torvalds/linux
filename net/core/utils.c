/*
 *	Generic address resultion entity
 *
 *	Authors:
 *	net_random Alan Cox
 *	net_ratelimit Andi Kleen
 *	in{4,6}_pton YOSHIFUJI Hideaki, Copyright (C)2006 USAGI/WIDE Project
 *
 *	Created by Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/ratelimit.h>

#include <net/sock.h>
#include <net/net_ratelimit.h>

#include <asm/byteorder.h>
#include <linux/uaccess.h>

DEFINE_RATELIMIT_STATE(net_ratelimit_state, 5 * HZ, 10);
/*
 * All net warning printk()s should be guarded by this function.
 */
int net_ratelimit(void)
{
	return __ratelimit(&net_ratelimit_state);
}
EXPORT_SYMBOL(net_ratelimit);

/*
 * Convert an ASCII string to binary IP.
 * This is outside of net/ipv4/ because various code that uses IP addresses
 * is otherwise not dependent on the TCP/IP stack.
 */

__be32 in_aton(const char *str)
{
	unsigned long l;
	unsigned int val;
	int i;

	l = 0;
	for (i = 0; i < 4; i++)	{
		l <<= 8;
		if (*str != '\0') {
			val = 0;
			while (*str != '\0' && *str != '.' && *str != '\n') {
				val *= 10;
				val += *str - '0';
				str++;
			}
			l |= val;
			if (*str != '\0')
				str++;
		}
	}
	return htonl(l);
}
EXPORT_SYMBOL(in_aton);

#define IN6PTON_XDIGIT		0x00010000
#define IN6PTON_DIGIT		0x00020000
#define IN6PTON_COLON_MASK	0x00700000
#define IN6PTON_COLON_1		0x00100000	/* single : requested */
#define IN6PTON_COLON_2		0x00200000	/* second : requested */
#define IN6PTON_COLON_1_2	0x00400000	/* :: requested */
#define IN6PTON_DOT		0x00800000	/* . */
#define IN6PTON_DELIM		0x10000000
#define IN6PTON_NULL		0x20000000	/* first/tail */
#define IN6PTON_UNKNOWN		0x40000000

static inline int xdigit2bin(char c, int delim)
{
	int val;

	if (c == delim || c == '\0')
		return IN6PTON_DELIM;
	if (c == ':')
		return IN6PTON_COLON_MASK;
	if (c == '.')
		return IN6PTON_DOT;

	val = hex_to_bin(c);
	if (val >= 0)
		return val | IN6PTON_XDIGIT | (val < 10 ? IN6PTON_DIGIT : 0);

	if (delim == -1)
		return IN6PTON_DELIM;
	return IN6PTON_UNKNOWN;
}

/**
 * in4_pton - convert an IPv4 address from literal to binary representation
 * @src: the start of the IPv4 address string
 * @srclen: the length of the string, -1 means strlen(src)
 * @dst: the binary (u8[4] array) representation of the IPv4 address
 * @delim: the delimiter of the IPv4 address in @src, -1 means no delimiter
 * @end: A pointer to the end of the parsed string will be placed here
 *
 * Return one on success, return zero when any error occurs
 * and @end will point to the end of the parsed string.
 *
 */
int in4_pton(const char *src, int srclen,
	     u8 *dst,
	     int delim, const char **end)
{
	const char *s;
	u8 *d;
	u8 dbuf[4];
	int ret = 0;
	int i;
	int w = 0;

	if (srclen < 0)
		srclen = strlen(src);
	s = src;
	d = dbuf;
	i = 0;
	while (1) {
		int c;
		c = xdigit2bin(srclen > 0 ? *s : '\0', delim);
		if (!(c & (IN6PTON_DIGIT | IN6PTON_DOT | IN6PTON_DELIM | IN6PTON_COLON_MASK))) {
			goto out;
		}
		if (c & (IN6PTON_DOT | IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
			if (w == 0)
				goto out;
			*d++ = w & 0xff;
			w = 0;
			i++;
			if (c & (IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
				if (i != 4)
					goto out;
				break;
			}
			goto cont;
		}
		w = (w * 10) + c;
		if ((w & 0xffff) > 255) {
			goto out;
		}
cont:
		if (i >= 4)
			goto out;
		s++;
		srclen--;
	}
	ret = 1;
	memcpy(dst, dbuf, sizeof(dbuf));
out:
	if (end)
		*end = s;
	return ret;
}
EXPORT_SYMBOL(in4_pton);

/**
 * in6_pton - convert an IPv6 address from literal to binary representation
 * @src: the start of the IPv6 address string
 * @srclen: the length of the string, -1 means strlen(src)
 * @dst: the binary (u8[16] array) representation of the IPv6 address
 * @delim: the delimiter of the IPv6 address in @src, -1 means no delimiter
 * @end: A pointer to the end of the parsed string will be placed here
 *
 * Return one on success, return zero when any error occurs
 * and @end will point to the end of the parsed string.
 *
 */
int in6_pton(const char *src, int srclen,
	     u8 *dst,
	     int delim, const char **end)
{
	const char *s, *tok = NULL;
	u8 *d, *dc = NULL;
	u8 dbuf[16];
	int ret = 0;
	int i;
	int state = IN6PTON_COLON_1_2 | IN6PTON_XDIGIT | IN6PTON_NULL;
	int w = 0;

	memset(dbuf, 0, sizeof(dbuf));

	s = src;
	d = dbuf;
	if (srclen < 0)
		srclen = strlen(src);

	while (1) {
		int c;

		c = xdigit2bin(srclen > 0 ? *s : '\0', delim);
		if (!(c & state))
			goto out;
		if (c & (IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
			/* process one 16-bit word */
			if (!(state & IN6PTON_NULL)) {
				*d++ = (w >> 8) & 0xff;
				*d++ = w & 0xff;
			}
			w = 0;
			if (c & IN6PTON_DELIM) {
				/* We've processed last word */
				break;
			}
			/*
			 * COLON_1 => XDIGIT
			 * COLON_2 => XDIGIT|DELIM
			 * COLON_1_2 => COLON_2
			 */
			switch (state & IN6PTON_COLON_MASK) {
			case IN6PTON_COLON_2:
				dc = d;
				state = IN6PTON_XDIGIT | IN6PTON_DELIM;
				if (dc - dbuf >= sizeof(dbuf))
					state |= IN6PTON_NULL;
				break;
			case IN6PTON_COLON_1|IN6PTON_COLON_1_2:
				state = IN6PTON_XDIGIT | IN6PTON_COLON_2;
				break;
			case IN6PTON_COLON_1:
				state = IN6PTON_XDIGIT;
				break;
			case IN6PTON_COLON_1_2:
				state = IN6PTON_COLON_2;
				break;
			default:
				state = 0;
			}
			tok = s + 1;
			goto cont;
		}

		if (c & IN6PTON_DOT) {
			ret = in4_pton(tok ? tok : s, srclen + (int)(s - tok), d, delim, &s);
			if (ret > 0) {
				d += 4;
				break;
			}
			goto out;
		}

		w = (w << 4) | (0xff & c);
		state = IN6PTON_COLON_1 | IN6PTON_DELIM;
		if (!(w & 0xf000)) {
			state |= IN6PTON_XDIGIT;
		}
		if (!dc && d + 2 < dbuf + sizeof(dbuf)) {
			state |= IN6PTON_COLON_1_2;
			state &= ~IN6PTON_DELIM;
		}
		if (d + 2 >= dbuf + sizeof(dbuf)) {
			state &= ~(IN6PTON_COLON_1|IN6PTON_COLON_1_2);
		}
cont:
		if ((dc && d + 4 < dbuf + sizeof(dbuf)) ||
		    d + 4 == dbuf + sizeof(dbuf)) {
			state |= IN6PTON_DOT;
		}
		if (d >= dbuf + sizeof(dbuf)) {
			state &= ~(IN6PTON_XDIGIT|IN6PTON_COLON_MASK);
		}
		s++;
		srclen--;
	}

	i = 15; d--;

	if (dc) {
		while (d >= dc)
			dst[i--] = *d--;
		while (i >= dc - dbuf)
			dst[i--] = 0;
		while (i >= 0)
			dst[i--] = *d--;
	} else
		memcpy(dst, dbuf, sizeof(dbuf));

	ret = 1;
out:
	if (end)
		*end = s;
	return ret;
}
EXPORT_SYMBOL(in6_pton);

void inet_proto_csum_replace4(__sum16 *sum, struct sk_buff *skb,
			      __be32 from, __be32 to, bool pseudohdr)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		csum_replace4(sum, from, to);
		if (skb->ip_summed == CHECKSUM_COMPLETE && pseudohdr)
			skb->csum = ~csum_add(csum_sub(~(skb->csum),
						       (__force __wsum)from),
					      (__force __wsum)to);
	} else if (pseudohdr)
		*sum = ~csum_fold(csum_add(csum_sub(csum_unfold(*sum),
						    (__force __wsum)from),
					   (__force __wsum)to));
}
EXPORT_SYMBOL(inet_proto_csum_replace4);

void inet_proto_csum_replace16(__sum16 *sum, struct sk_buff *skb,
			       const __be32 *from, const __be32 *to,
			       bool pseudohdr)
{
	__be32 diff[] = {
		~from[0], ~from[1], ~from[2], ~from[3],
		to[0], to[1], to[2], to[3],
	};
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		*sum = csum_fold(csum_partial(diff, sizeof(diff),
				 ~csum_unfold(*sum)));
		if (skb->ip_summed == CHECKSUM_COMPLETE && pseudohdr)
			skb->csum = ~csum_partial(diff, sizeof(diff),
						  ~skb->csum);
	} else if (pseudohdr)
		*sum = ~csum_fold(csum_partial(diff, sizeof(diff),
				  csum_unfold(*sum)));
}
EXPORT_SYMBOL(inet_proto_csum_replace16);

void inet_proto_csum_replace_by_diff(__sum16 *sum, struct sk_buff *skb,
				     __wsum diff, bool pseudohdr)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		*sum = csum_fold(csum_add(diff, ~csum_unfold(*sum)));
		if (skb->ip_summed == CHECKSUM_COMPLETE && pseudohdr)
			skb->csum = ~csum_add(diff, ~skb->csum);
	} else if (pseudohdr) {
		*sum = ~csum_fold(csum_add(diff, csum_unfold(*sum)));
	}
}
EXPORT_SYMBOL(inet_proto_csum_replace_by_diff);
