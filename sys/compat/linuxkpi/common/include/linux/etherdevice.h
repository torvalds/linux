/*-
 * Copyright (c) 2015-2016 Mellanox Technologies, Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _LINUX_ETHERDEVICE
#define	_LINUX_ETHERDEVICE

#include <linux/types.h>

#include <sys/random.h>
#include <sys/libkern.h>

#define	ETH_MODULE_SFF_8079		1
#define	ETH_MODULE_SFF_8079_LEN		256
#define	ETH_MODULE_SFF_8472		2
#define	ETH_MODULE_SFF_8472_LEN		512
#define	ETH_MODULE_SFF_8636		3
#define	ETH_MODULE_SFF_8636_LEN		256
#define	ETH_MODULE_SFF_8436		4
#define	ETH_MODULE_SFF_8436_LEN		256

struct ethtool_eeprom {
	u32	offset;
	u32	len;
};

struct ethtool_modinfo {
	u32	type;
	u32	eeprom_len;
};

static inline bool
is_zero_ether_addr(const u8 * addr)
{
	return ((addr[0] + addr[1] + addr[2] + addr[3] + addr[4] + addr[5]) == 0x00);
}

static inline bool
is_multicast_ether_addr(const u8 * addr)
{
	return (0x01 & addr[0]);
}

static inline bool
is_broadcast_ether_addr(const u8 * addr)
{
	return ((addr[0] + addr[1] + addr[2] + addr[3] + addr[4] + addr[5]) == (6 * 0xff));
}

static inline bool
is_valid_ether_addr(const u8 * addr)
{
	return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr);
}

static inline void
ether_addr_copy(u8 * dst, const u8 * src)
{
	memcpy(dst, src, 6);
}

static inline bool
ether_addr_equal(const u8 *pa, const u8 *pb)
{
	return (memcmp(pa, pb, 6) == 0);
}

static inline bool
ether_addr_equal_64bits(const u8 *pa, const u8 *pb)
{
	return (memcmp(pa, pb, 6) == 0);
}

static inline void
eth_broadcast_addr(u8 *pa)
{
	memset(pa, 0xff, 6);
}

static inline void
eth_zero_addr(u8 *pa)
{
	memset(pa, 0, 6);
}

static inline void
random_ether_addr(u8 * dst)
{
	if (read_random(dst, 6) == 0)
		arc4rand(dst, 6, 0);

	dst[0] &= 0xfe;
	dst[0] |= 0x02;
}

#endif					/* _LINUX_ETHERDEVICE */
