/* SPDX-License-Identifier: GPL-2.0-only */
/* include/net/ax88796.h
 *
 * Copyright 2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
*/

#ifndef __NET_AX88796_PLAT_H
#define __NET_AX88796_PLAT_H

struct sk_buff;
struct net_device;
struct platform_device;

#define AXFLG_HAS_EEPROM		(1<<0)
#define AXFLG_MAC_FROMDEV		(1<<1)	/* device already has MAC */
#define AXFLG_HAS_93CX6			(1<<2)	/* use eeprom_93cx6 driver */
#define AXFLG_MAC_FROMPLATFORM		(1<<3)	/* MAC given by platform data */

struct ax_plat_data {
	unsigned int	 flags;
	unsigned char	 wordlength;	/* 1 or 2 */
	unsigned char	 dcr_val;	/* default value for DCR */
	unsigned char	 rcr_val;	/* default value for RCR */
	unsigned char	 gpoc_val;	/* default value for GPOC */
	u32		*reg_offsets;	/* register offsets */
	u8		*mac_addr;	/* MAC addr (only used when
					   AXFLG_MAC_FROMPLATFORM is used */

	/* uses default ax88796 buffer if set to NULL */
	void (*block_output)(struct net_device *dev, int count,
			const unsigned char *buf, int star_page);
	void (*block_input)(struct net_device *dev, int count,
			struct sk_buff *skb, int ring_offset);
	/* returns nonzero if a pending interrupt request might by caused by
	 * the ax88786. Handles all interrupts if set to NULL
	 */
	int (*check_irq)(struct platform_device *pdev);
};

#endif /* __NET_AX88796_PLAT_H */
