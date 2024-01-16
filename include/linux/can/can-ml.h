/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/* Copyright (c) 2002-2007 Volkswagen Group Electronic Research
 * Copyright (c) 2017 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#ifndef CAN_ML_H
#define CAN_ML_H

#include <linux/can.h>
#include <linux/list.h>
#include <linux/netdevice.h>

#define CAN_SFF_RCV_ARRAY_SZ (1 << CAN_SFF_ID_BITS)
#define CAN_EFF_RCV_HASH_BITS 10
#define CAN_EFF_RCV_ARRAY_SZ (1 << CAN_EFF_RCV_HASH_BITS)

enum { RX_ERR, RX_ALL, RX_FIL, RX_INV, RX_MAX };

struct can_dev_rcv_lists {
	struct hlist_head rx[RX_MAX];
	struct hlist_head rx_sff[CAN_SFF_RCV_ARRAY_SZ];
	struct hlist_head rx_eff[CAN_EFF_RCV_ARRAY_SZ];
	int entries;
};

struct can_ml_priv {
	struct can_dev_rcv_lists dev_rcv_lists;
#ifdef CAN_J1939
	struct j1939_priv *j1939_priv;
#endif
};

static inline struct can_ml_priv *can_get_ml_priv(struct net_device *dev)
{
	return netdev_get_ml_priv(dev, ML_PRIV_CAN);
}

static inline void can_set_ml_priv(struct net_device *dev,
				   struct can_ml_priv *ml_priv)
{
	netdev_set_ml_priv(dev, ml_priv, ML_PRIV_CAN);
}

#endif /* CAN_ML_H */
