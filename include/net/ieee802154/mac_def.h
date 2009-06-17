/*
 * IEEE802.15.4-2003 specification
 *
 * Copyright (C) 2007, 2008 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Maxim Osipov <maxim.osipov@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 */

#ifndef IEEE802154_MAC_DEF_H
#define IEEE802154_MAC_DEF_H

#define IEEE802154_FC_TYPE_BEACON	0x0	/* Frame is beacon */
#define	IEEE802154_FC_TYPE_DATA		0x1	/* Frame is data */
#define IEEE802154_FC_TYPE_ACK		0x2	/* Frame is acknowledgment */
#define IEEE802154_FC_TYPE_MAC_CMD	0x3	/* Frame is MAC command */

#define IEEE802154_FC_TYPE_SHIFT		0
#define IEEE802154_FC_TYPE_MASK		((1 << 3) - 1)
#define IEEE802154_FC_TYPE(x)		((x & IEEE802154_FC_TYPE_MASK) >> IEEE802154_FC_TYPE_SHIFT)
#define IEEE802154_FC_SET_TYPE(v, x)	do {	\
	v = (((v) & ~IEEE802154_FC_TYPE_MASK) | \
	    (((x) << IEEE802154_FC_TYPE_SHIFT) & IEEE802154_FC_TYPE_MASK)); \
	} while (0)

#define IEEE802154_FC_SECEN		(1 << 3)
#define IEEE802154_FC_FRPEND		(1 << 4)
#define IEEE802154_FC_ACK_REQ		(1 << 5)
#define IEEE802154_FC_INTRA_PAN		(1 << 6)

#define IEEE802154_FC_SAMODE_SHIFT	14
#define IEEE802154_FC_SAMODE_MASK	(3 << IEEE802154_FC_SAMODE_SHIFT)
#define IEEE802154_FC_DAMODE_SHIFT	10
#define IEEE802154_FC_DAMODE_MASK	(3 << IEEE802154_FC_DAMODE_SHIFT)

#define IEEE802154_FC_SAMODE(x)		\
	(((x) & IEEE802154_FC_SAMODE_MASK) >> IEEE802154_FC_SAMODE_SHIFT)

#define IEEE802154_FC_DAMODE(x)		\
	(((x) & IEEE802154_FC_DAMODE_MASK) >> IEEE802154_FC_DAMODE_SHIFT)


/* MAC's Command Frames Identifiers */
#define IEEE802154_CMD_ASSOCIATION_REQ		0x01
#define IEEE802154_CMD_ASSOCIATION_RESP		0x02
#define IEEE802154_CMD_DISASSOCIATION_NOTIFY	0x03
#define IEEE802154_CMD_DATA_REQ			0x04
#define IEEE802154_CMD_PANID_CONFLICT_NOTIFY	0x05
#define IEEE802154_CMD_ORPHAN_NOTIFY		0x06
#define IEEE802154_CMD_BEACON_REQ		0x07
#define IEEE802154_CMD_COORD_REALIGN_NOTIFY	0x08
#define IEEE802154_CMD_GTS_REQ			0x09

/*
 * The return values of MAC operations
 */
enum {
	/*
	 * The requested operation was completed successfully.
	 * For a transmission request, this value indicates
	 * a successful transmission.
	 */
	IEEE802154_SUCCESS = 0x0,

	/* The beacon was lost following a synchronization request. */
	IEEE802154_BEACON_LOSS = 0xe0,
	/*
	 * A transmission could not take place due to activity on the
	 * channel, i.e., the CSMA-CA mechanism has failed.
	 */
	IEEE802154_CHNL_ACCESS_FAIL = 0xe1,
	/* The GTS request has been denied by the PAN coordinator. */
	IEEE802154_DENINED = 0xe2,
	/* The attempt to disable the transceiver has failed. */
	IEEE802154_DISABLE_TRX_FAIL = 0xe3,
	/*
	 * The received frame induces a failed security check according to
	 * the security suite.
	 */
	IEEE802154_FAILED_SECURITY_CHECK = 0xe4,
	/*
	 * The frame resulting from secure processing has a length that is
	 * greater than aMACMaxFrameSize.
	 */
	IEEE802154_FRAME_TOO_LONG = 0xe5,
	/*
	 * The requested GTS transmission failed because the specified GTS
	 * either did not have a transmit GTS direction or was not defined.
	 */
	IEEE802154_INVALID_GTS = 0xe6,
	/*
	 * A request to purge an MSDU from the transaction queue was made using
	 * an MSDU handle that was not found in the transaction table.
	 */
	IEEE802154_INVALID_HANDLE = 0xe7,
	/* A parameter in the primitive is out of the valid range.*/
	IEEE802154_INVALID_PARAMETER = 0xe8,
	/* No acknowledgment was received after aMaxFrameRetries. */
	IEEE802154_NO_ACK = 0xe9,
	/* A scan operation failed to find any network beacons.*/
	IEEE802154_NO_BEACON = 0xea,
	/* No response data were available following a request. */
	IEEE802154_NO_DATA = 0xeb,
	/* The operation failed because a short address was not allocated. */
	IEEE802154_NO_SHORT_ADDRESS = 0xec,
	/*
	 * A receiver enable request was unsuccessful because it could not be
	 * completed within the CAP.
	 */
	IEEE802154_OUT_OF_CAP = 0xed,
	/*
	 * A PAN identifier conflict has been detected and communicated to the
	 * PAN coordinator.
	 */
	IEEE802154_PANID_CONFLICT = 0xee,
	/* A coordinator realignment command has been received. */
	IEEE802154_REALIGMENT = 0xef,
	/* The transaction has expired and its information discarded. */
	IEEE802154_TRANSACTION_EXPIRED = 0xf0,
	/* There is no capacity to store the transaction. */
	IEEE802154_TRANSACTION_OVERFLOW = 0xf1,
	/*
	 * The transceiver was in the transmitter enabled state when the
	 * receiver was requested to be enabled.
	 */
	IEEE802154_TX_ACTIVE = 0xf2,
	/* The appropriate key is not available in the ACL. */
	IEEE802154_UNAVAILABLE_KEY = 0xf3,
	/*
	 * A SET/GET request was issued with the identifier of a PIB attribute
	 * that is not supported.
	 */
	IEEE802154_UNSUPPORTED_ATTR = 0xf4,
	/*
	 * A request to perform a scan operation failed because the MLME was
	 * in the process of performing a previously initiated scan operation.
	 */
	IEEE802154_SCAN_IN_PROGRESS = 0xfc,
};


#endif


