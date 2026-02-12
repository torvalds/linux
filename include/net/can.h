/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * net/can.h
 *
 * Definitions for the CAN network socket buffer extensions
 *
 * Copyright (C) 2026 Oliver Hartkopp <socketcan@hartkopp.net>
 *
 */

#ifndef _NET_CAN_H
#define _NET_CAN_H

/**
 * struct can_skb_ext - skb extensions for CAN specific content
 * @can_iif: ifindex of the first interface the CAN frame appeared on
 * @can_framelen: cached echo CAN frame length for bql
 * @can_gw_hops: can-gw CAN frame time-to-live counter
 * @can_ext_flags: CAN skb extensions flags
 */
struct can_skb_ext {
	int	can_iif;
	u16	can_framelen;
	u8	can_gw_hops;
	u8	can_ext_flags;
};

#endif /* _NET_CAN_H */
