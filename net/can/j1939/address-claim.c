// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2010-2011 EIA Electronics,
//                         Kurt Van Dijck <kurt.van.dijck@eia.be>
// Copyright (c) 2010-2011 EIA Electronics,
//                         Pieter Beyens <pieter.beyens@eia.be>
// Copyright (c) 2017-2019 Pengutronix,
//                         Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (c) 2017-2019 Pengutronix,
//                         Oleksij Rempel <kernel@pengutronix.de>

/* J1939 Address Claiming.
 * Address Claiming in the kernel
 * - keeps track of the AC states of ECU's,
 * - resolves NAME<=>SA taking into account the AC states of ECU's.
 *
 * All Address Claim msgs (including host-originated msg) are processed
 * at the receive path (a sent msg is always received again via CAN echo).
 * As such, the processing of AC msgs is done in the order on which msgs
 * are sent on the bus.
 *
 * This module doesn't send msgs itself (e.g. replies on Address Claims),
 * this is the responsibility of a user space application or daemon.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "j1939-priv.h"

static inline name_t j1939_skb_to_name(const struct sk_buff *skb)
{
	return le64_to_cpup((__le64 *)skb->data);
}

static inline bool j1939_ac_msg_is_request(struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	int req_pgn;

	if (skb->len < 3 || skcb->addr.pgn != J1939_PGN_REQUEST)
		return false;

	req_pgn = skb->data[0] | (skb->data[1] << 8) | (skb->data[2] << 16);

	return req_pgn == J1939_PGN_ADDRESS_CLAIMED;
}

static int j1939_ac_verify_outgoing(struct j1939_priv *priv,
				    struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);

	if (skb->len != 8) {
		netdev_notice(priv->ndev, "tx address claim with dlc %i\n",
			      skb->len);
		return -EPROTO;
	}

	if (skcb->addr.src_name != j1939_skb_to_name(skb)) {
		netdev_notice(priv->ndev, "tx address claim with different name\n");
		return -EPROTO;
	}

	if (skcb->addr.sa == J1939_NO_ADDR) {
		netdev_notice(priv->ndev, "tx address claim with broadcast sa\n");
		return -EPROTO;
	}

	/* ac must always be a broadcast */
	if (skcb->addr.dst_name || skcb->addr.da != J1939_NO_ADDR) {
		netdev_notice(priv->ndev, "tx address claim with dest, not broadcast\n");
		return -EPROTO;
	}
	return 0;
}

int j1939_ac_fixup(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	int ret;
	u8 addr;

	/* network mgmt: address claiming msgs */
	if (skcb->addr.pgn == J1939_PGN_ADDRESS_CLAIMED) {
		struct j1939_ecu *ecu;

		ret = j1939_ac_verify_outgoing(priv, skb);
		/* return both when failure & when successful */
		if (ret < 0)
			return ret;
		ecu = j1939_ecu_get_by_name(priv, skcb->addr.src_name);
		if (!ecu)
			return -ENODEV;

		if (ecu->addr != skcb->addr.sa)
			/* hold further traffic for ecu, remove from parent */
			j1939_ecu_unmap(ecu);
		j1939_ecu_put(ecu);
	} else if (skcb->addr.src_name) {
		/* assign source address */
		addr = j1939_name_to_addr(priv, skcb->addr.src_name);
		if (!j1939_address_is_unicast(addr) &&
		    !j1939_ac_msg_is_request(skb)) {
			netdev_notice(priv->ndev, "tx drop: invalid sa for name 0x%016llx\n",
				      skcb->addr.src_name);
			return -EADDRNOTAVAIL;
		}
		skcb->addr.sa = addr;
	}

	/* assign destination address */
	if (skcb->addr.dst_name) {
		addr = j1939_name_to_addr(priv, skcb->addr.dst_name);
		if (!j1939_address_is_unicast(addr)) {
			netdev_notice(priv->ndev, "tx drop: invalid da for name 0x%016llx\n",
				      skcb->addr.dst_name);
			return -EADDRNOTAVAIL;
		}
		skcb->addr.da = addr;
	}
	return 0;
}

static void j1939_ac_process(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_ecu *ecu, *prev;
	name_t name;

	if (skb->len != 8) {
		netdev_notice(priv->ndev, "rx address claim with wrong dlc %i\n",
			      skb->len);
		return;
	}

	name = j1939_skb_to_name(skb);
	skcb->addr.src_name = name;
	if (!name) {
		netdev_notice(priv->ndev, "rx address claim without name\n");
		return;
	}

	if (!j1939_address_is_valid(skcb->addr.sa)) {
		netdev_notice(priv->ndev, "rx address claim with broadcast sa\n");
		return;
	}

	write_lock_bh(&priv->lock);

	/* Few words on the ECU ref counting:
	 *
	 * First we get an ECU handle, either with
	 * j1939_ecu_get_by_name_locked() (increments the ref counter)
	 * or j1939_ecu_create_locked() (initializes an ECU object
	 * with a ref counter of 1).
	 *
	 * j1939_ecu_unmap_locked() will decrement the ref counter,
	 * but only if the ECU was mapped before. So "ecu" still
	 * belongs to us.
	 *
	 * j1939_ecu_timer_start() will increment the ref counter
	 * before it starts the timer, so we can put the ecu when
	 * leaving this function.
	 */
	ecu = j1939_ecu_get_by_name_locked(priv, name);

	if (ecu && ecu->addr == skcb->addr.sa) {
		/* The ISO 11783-5 standard, in "4.5.2 - Address claim
		 * requirements", states:
		 *   d) No CF shall begin, or resume, transmission on the
		 *      network until 250 ms after it has successfully claimed
		 *      an address except when responding to a request for
		 *      address-claimed.
		 *
		 * But "Figure 6" and "Figure 7" in "4.5.4.2 - Address-claim
		 * prioritization" show that the CF begins the transmission
		 * after 250 ms from the first AC (address-claimed) message
		 * even if it sends another AC message during that time window
		 * to resolve the address contention with another CF.
		 *
		 * As stated in "4.4.2.3 - Address-claimed message":
		 *   In order to successfully claim an address, the CF sending
		 *   an address claimed message shall not receive a contending
		 *   claim from another CF for at least 250 ms.
		 *
		 * As stated in "4.4.3.2 - NAME management (NM) message":
		 *   1) A commanding CF can
		 *      d) request that a CF with a specified NAME transmit
		 *         the address-claimed message with its current NAME.
		 *   2) A target CF shall
		 *      d) send an address-claimed message in response to a
		 *         request for a matching NAME
		 *
		 * Taking the above arguments into account, the 250 ms wait is
		 * requested only during network initialization.
		 *
		 * Do not restart the timer on AC message if both the NAME and
		 * the address match and so if the address has already been
		 * claimed (timer has expired) or the AC message has been sent
		 * to resolve the contention with another CF (timer is still
		 * running).
		 */
		goto out_ecu_put;
	}

	if (!ecu && j1939_address_is_unicast(skcb->addr.sa))
		ecu = j1939_ecu_create_locked(priv, name);

	if (IS_ERR_OR_NULL(ecu))
		goto out_unlock_bh;

	/* cancel pending (previous) address claim */
	j1939_ecu_timer_cancel(ecu);

	if (j1939_address_is_idle(skcb->addr.sa)) {
		j1939_ecu_unmap_locked(ecu);
		goto out_ecu_put;
	}

	/* save new addr */
	if (ecu->addr != skcb->addr.sa)
		j1939_ecu_unmap_locked(ecu);
	ecu->addr = skcb->addr.sa;

	prev = j1939_ecu_get_by_addr_locked(priv, skcb->addr.sa);
	if (prev) {
		if (ecu->name > prev->name) {
			j1939_ecu_unmap_locked(ecu);
			j1939_ecu_put(prev);
			goto out_ecu_put;
		} else {
			/* kick prev if less or equal */
			j1939_ecu_unmap_locked(prev);
			j1939_ecu_put(prev);
		}
	}

	j1939_ecu_timer_start(ecu);
 out_ecu_put:
	j1939_ecu_put(ecu);
 out_unlock_bh:
	write_unlock_bh(&priv->lock);
}

void j1939_ac_recv(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_ecu *ecu;

	/* network mgmt */
	if (skcb->addr.pgn == J1939_PGN_ADDRESS_CLAIMED) {
		j1939_ac_process(priv, skb);
	} else if (j1939_address_is_unicast(skcb->addr.sa)) {
		/* assign source name */
		ecu = j1939_ecu_get_by_addr(priv, skcb->addr.sa);
		if (ecu) {
			skcb->addr.src_name = ecu->name;
			j1939_ecu_put(ecu);
		}
	}

	/* assign destination name */
	ecu = j1939_ecu_get_by_addr(priv, skcb->addr.da);
	if (ecu) {
		skcb->addr.dst_name = ecu->name;
		j1939_ecu_put(ecu);
	}
}
