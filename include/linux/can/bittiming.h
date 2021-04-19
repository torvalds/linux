/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 */

#ifndef _CAN_BITTIMING_H
#define _CAN_BITTIMING_H

#include <linux/netdevice.h>
#include <linux/can/netlink.h>

#define CAN_SYNC_SEG 1

#ifdef CONFIG_CAN_CALC_BITTIMING
int can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt,
		       const struct can_bittiming_const *btc);
#else /* !CONFIG_CAN_CALC_BITTIMING */
static inline int
can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt,
		   const struct can_bittiming_const *btc)
{
	netdev_err(dev, "bit-timing calculation not available\n");
	return -EINVAL;
}
#endif /* CONFIG_CAN_CALC_BITTIMING */

int can_get_bittiming(struct net_device *dev, struct can_bittiming *bt,
		      const struct can_bittiming_const *btc,
		      const u32 *bitrate_const,
		      const unsigned int bitrate_const_cnt);

/*
 * can_bit_time() - Duration of one bit
 *
 * Please refer to ISO 11898-1:2015, section 11.3.1.1 "Bit time" for
 * additional information.
 *
 * Return: the number of time quanta in one bit.
 */
static inline unsigned int can_bit_time(const struct can_bittiming *bt)
{
	return CAN_SYNC_SEG + bt->prop_seg + bt->phase_seg1 + bt->phase_seg2;
}

#endif /* !_CAN_BITTIMING_H */
