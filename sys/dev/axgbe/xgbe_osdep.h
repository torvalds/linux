/*-
 * Copyright (c) 2016,2017 SoftIron Inc.
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * the sponsorship of SoftIron Inc.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _XGBE_OSDEP_H_
#define	_XGBE_OSDEP_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t __le32;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct {
	struct mtx lock;
} spinlock_t;

static inline void
spin_lock_init(spinlock_t *spinlock)
{

	mtx_init(&spinlock->lock, "axgbe_spin", NULL, MTX_DEF);
}

#define	spin_lock_irqsave(spinlock, flags)				\
do {									\
	(flags) = intr_disable();					\
	mtx_lock(&(spinlock)->lock);					\
} while (0)

#define	spin_unlock_irqrestore(spinlock, flags)				\
do {									\
	mtx_unlock(&(spinlock)->lock);					\
	intr_restore(flags);						\
} while (0)

#define	BIT(pos)		(1ul << pos)

static inline void
clear_bit(int pos, unsigned long *p)
{

	atomic_clear_long(p, 1ul << pos);
}

static inline int
test_bit(int pos, unsigned long *p)
{
	unsigned long val;

	val = *p;
	return ((val & 1ul << pos) != 0);
}

static inline void
set_bit(int pos, unsigned long *p)
{

	atomic_set_long(p, 1ul << pos);
}

#define	lower_32_bits(x)	((x) & 0xffffffffu)
#define	upper_32_bits(x)	(((x) >> 32) & 0xffffffffu)
#define	cpu_to_le32(x)		le32toh(x)
#define	le32_to_cpu(x)		htole32(x)

MALLOC_DECLARE(M_AXGBE);

#define	ADVERTISED_Pause		0x01
#define	ADVERTISED_Asym_Pause		0x02
#define	ADVERTISED_Autoneg		0x04
#define	ADVERTISED_Backplane		0x08
#define	ADVERTISED_10000baseKR_Full	0x10
#define	ADVERTISED_2500baseX_Full	0x20
#define	ADVERTISED_1000baseKX_Full	0x40

#define	AUTONEG_DISABLE			0
#define	AUTONEG_ENABLE			1

#define	DUPLEX_UNKNOWN			1
#define	DUPLEX_FULL			2

#define	SPEED_UNKNOWN			1
#define	SPEED_10000			2
#define	SPEED_2500			3
#define	SPEED_1000			4

#define	SUPPORTED_Autoneg		0x01
#define	SUPPORTED_Pause			0x02
#define	SUPPORTED_Asym_Pause		0x04
#define	SUPPORTED_Backplane		0x08
#define	SUPPORTED_10000baseKR_Full	0x10
#define	SUPPORTED_1000baseKX_Full	0x20
#define	SUPPORTED_2500baseX_Full	0x40
#define	SUPPORTED_10000baseR_FEC	0x80

#define	BMCR_SPEED100			0x2000

#define	MDIO_MMD_PMAPMD			1
#define	MDIO_MMD_PCS			3
#define	MDIO_MMD_AN			7
#define	MDIO_PMA_10GBR_FECABLE 		170
#define	MDIO_PMA_10GBR_FECABLE_ABLE     0x0001
#define	MDIO_PMA_10GBR_FECABLE_ERRABLE  0x0002
#define	MII_ADDR_C45			(1<<30)

#define	MDIO_CTRL1			0x00 /* MII_BMCR */
#define	MDIO_CTRL1_RESET		0x8000 /* BMCR_RESET */
#define	MDIO_CTRL1_SPEEDSELEXT		0x2040 /* BMCR_SPEED1000|BMCR_SPEED100*/
#define	MDIO_CTRL1_SPEEDSEL		(MDIO_CTRL1_SPEEDSELEXT | 0x3c)
#define	MDIO_AN_CTRL1_ENABLE		0x1000 /* BMCR_AUTOEN */
#define	MDIO_CTRL1_LPOWER		0x0800 /* BMCR_PDOWN */
#define	MDIO_AN_CTRL1_RESTART		0x0200 /* BMCR_STARTNEG */

#define	MDIO_CTRL1_SPEED10G		(MDIO_CTRL1_SPEEDSELEXT | 0x00)

#define	MDIO_STAT1			1 /* MII_BMSR */
#define	MDIO_STAT1_LSTATUS		0x0004 /* BMSR_LINK */

#define	MDIO_CTRL2			0x07
#define	MDIO_PCS_CTRL2_10GBR		0x0000
#define	MDIO_PCS_CTRL2_10GBX		0x0001
#define	MDIO_PCS_CTRL2_TYPE		0x0003

#define	MDIO_AN_ADVERTISE		16

#define	MDIO_AN_LPA			19

#define	ETH_ALEN		ETHER_ADDR_LEN
#define	ETH_HLEN		ETHER_HDR_LEN
#define	ETH_FCS_LEN		4
#define	VLAN_HLEN		ETHER_VLAN_ENCAP_LEN

#define	ARRAY_SIZE(x)		nitems(x)

#define	BITS_PER_LONG		(sizeof(long) * CHAR_BIT)
#define	BITS_TO_LONGS(n)	howmany((n), BITS_PER_LONG)

#define	NSEC_PER_SEC		1000000000ul

#define	min_t(t, a, b)		MIN((t)(a), (t)(b))
#define	max_t(t, a, b)		MAX((t)(a), (t)(b))

#endif /* _XGBE_OSDEP_H_ */
