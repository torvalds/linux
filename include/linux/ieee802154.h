/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE802.15.4-2003 specification
 *
 * Copyright (C) 2007, 2008 Siemens AG
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Maxim Osipov <maxim.osipov@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#ifndef LINUX_IEEE802154_H
#define LINUX_IEEE802154_H

#include <linux/types.h>
#include <linux/random.h>

#define IEEE802154_MTU			127
#define IEEE802154_ACK_PSDU_LEN		5
#define IEEE802154_MIN_PSDU_LEN		9
#define IEEE802154_FCS_LEN		2
#define IEEE802154_MAX_AUTH_TAG_LEN	16
#define IEEE802154_FC_LEN		2
#define IEEE802154_SEQ_LEN		1

/*  General MAC frame format:
 *  2 bytes: Frame Control
 *  1 byte:  Sequence Number
 * 20 bytes: Addressing fields
 * 14 bytes: Auxiliary Security Header
 */
#define IEEE802154_MAX_HEADER_LEN	(2 + 1 + 20 + 14)
#define IEEE802154_MIN_HEADER_LEN	(IEEE802154_ACK_PSDU_LEN - \
					 IEEE802154_FCS_LEN)

#define IEEE802154_PAN_ID_BROADCAST	0xffff
#define IEEE802154_ADDR_SHORT_BROADCAST	0xffff
#define IEEE802154_ADDR_SHORT_UNSPEC	0xfffe

#define IEEE802154_EXTENDED_ADDR_LEN	8
#define IEEE802154_SHORT_ADDR_LEN	2
#define IEEE802154_PAN_ID_LEN		2

#define IEEE802154_LIFS_PERIOD		40
#define IEEE802154_SIFS_PERIOD		12
#define IEEE802154_MAX_SIFS_FRAME_SIZE	18

#define IEEE802154_MAX_CHANNEL		26
#define IEEE802154_MAX_PAGE		31

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

#define IEEE802154_FC_SECEN_SHIFT	3
#define IEEE802154_FC_SECEN		(1 << IEEE802154_FC_SECEN_SHIFT)
#define IEEE802154_FC_FRPEND_SHIFT	4
#define IEEE802154_FC_FRPEND		(1 << IEEE802154_FC_FRPEND_SHIFT)
#define IEEE802154_FC_ACK_REQ_SHIFT	5
#define IEEE802154_FC_ACK_REQ		(1 << IEEE802154_FC_ACK_REQ_SHIFT)
#define IEEE802154_FC_INTRA_PAN_SHIFT	6
#define IEEE802154_FC_INTRA_PAN		(1 << IEEE802154_FC_INTRA_PAN_SHIFT)

#define IEEE802154_FC_SAMODE_SHIFT	14
#define IEEE802154_FC_SAMODE_MASK	(3 << IEEE802154_FC_SAMODE_SHIFT)
#define IEEE802154_FC_DAMODE_SHIFT	10
#define IEEE802154_FC_DAMODE_MASK	(3 << IEEE802154_FC_DAMODE_SHIFT)

#define IEEE802154_FC_VERSION_SHIFT	12
#define IEEE802154_FC_VERSION_MASK	(3 << IEEE802154_FC_VERSION_SHIFT)
#define IEEE802154_FC_VERSION(x)	((x & IEEE802154_FC_VERSION_MASK) >> IEEE802154_FC_VERSION_SHIFT)

#define IEEE802154_FC_SAMODE(x)		\
	(((x) & IEEE802154_FC_SAMODE_MASK) >> IEEE802154_FC_SAMODE_SHIFT)

#define IEEE802154_FC_DAMODE(x)		\
	(((x) & IEEE802154_FC_DAMODE_MASK) >> IEEE802154_FC_DAMODE_SHIFT)

#define IEEE802154_SCF_SECLEVEL_MASK		7
#define IEEE802154_SCF_SECLEVEL_SHIFT		0
#define IEEE802154_SCF_SECLEVEL(x)		(x & IEEE802154_SCF_SECLEVEL_MASK)
#define IEEE802154_SCF_KEY_ID_MODE_SHIFT	3
#define IEEE802154_SCF_KEY_ID_MODE_MASK		(3 << IEEE802154_SCF_KEY_ID_MODE_SHIFT)
#define IEEE802154_SCF_KEY_ID_MODE(x)		\
	((x & IEEE802154_SCF_KEY_ID_MODE_MASK) >> IEEE802154_SCF_KEY_ID_MODE_SHIFT)

#define IEEE802154_SCF_KEY_IMPLICIT		0
#define IEEE802154_SCF_KEY_INDEX		1
#define IEEE802154_SCF_KEY_SHORT_INDEX		2
#define IEEE802154_SCF_KEY_HW_INDEX		3

#define IEEE802154_SCF_SECLEVEL_NONE		0
#define IEEE802154_SCF_SECLEVEL_MIC32		1
#define IEEE802154_SCF_SECLEVEL_MIC64		2
#define IEEE802154_SCF_SECLEVEL_MIC128		3
#define IEEE802154_SCF_SECLEVEL_ENC		4
#define IEEE802154_SCF_SECLEVEL_ENC_MIC32	5
#define IEEE802154_SCF_SECLEVEL_ENC_MIC64	6
#define IEEE802154_SCF_SECLEVEL_ENC_MIC128	7

/* MAC footer size */
#define IEEE802154_MFR_SIZE	2 /* 2 octets */

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
	/* The requested operation failed. */
	IEEE802154_MAC_ERROR = 0x1,
	/* The requested operation has been cancelled. */
	IEEE802154_CANCELLED = 0x2,
	/*
	 * Device is ready to poll the coordinator for data in a non beacon
	 * enabled PAN.
	 */
	IEEE802154_READY_FOR_POLL = 0x3,
	/* Wrong frame counter. */
	IEEE802154_COUNTER_ERROR = 0xdb,
	/*
	 * The frame does not conforms to the incoming key usage policy checking
	 * procedure.
	 */
	IEEE802154_IMPROPER_KEY_TYPE = 0xdc,
	/*
	 * The frame does not conforms to the incoming security level usage
	 * policy checking procedure.
	 */
	IEEE802154_IMPROPER_SECURITY_LEVEL = 0xdd,
	/* Secured frame received with an empty Frame Version field. */
	IEEE802154_UNSUPPORTED_LEGACY = 0xde,
	/*
	 * A secured frame is received or must be sent but security is not
	 * enabled in the device. Or, the Auxiliary Security Header has security
	 * level of zero in it.
	 */
	IEEE802154_UNSUPPORTED_SECURITY = 0xdf,
	/* The beacon was lost following a synchronization request. */
	IEEE802154_BEACON_LOST = 0xe0,
	/*
	 * A transmission could not take place due to activity on the
	 * channel, i.e., the CSMA-CA mechanism has failed.
	 */
	IEEE802154_CHANNEL_ACCESS_FAILURE = 0xe1,
	/* The GTS request has been denied by the PAN coordinator. */
	IEEE802154_DENIED = 0xe2,
	/* The attempt to disable the transceiver has failed. */
	IEEE802154_DISABLE_TRX_FAILURE = 0xe3,
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
	IEEE802154_PAN_ID_CONFLICT = 0xee,
	/* A coordinator realignment command has been received. */
	IEEE802154_REALIGNMENT = 0xef,
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
	IEEE802154_UNSUPPORTED_ATTRIBUTE = 0xf4,
	/* Missing source or destination address or address mode. */
	IEEE802154_INVALID_ADDRESS = 0xf5,
	/*
	 * MLME asked to turn the receiver on, but the on time duration is too
	 * big compared to the macBeaconOrder.
	 */
	IEEE802154_ON_TIME_TOO_LONG = 0xf6,
	/*
	 * MLME asaked to turn the receiver on, but the request was delayed for
	 * too long before getting processed.
	 */
	IEEE802154_PAST_TIME = 0xf7,
	/*
	 * The StartTime parameter is nonzero, and the MLME is not currently
	 * tracking the beacon of the coordinator through which it is
	 * associated.
	 */
	IEEE802154_TRACKING_OFF = 0xf8,
	/*
	 * The index inside the hierarchical values in PIBAttribute is out of
	 * range.
	 */
	IEEE802154_INVALID_INDEX = 0xf9,
	/*
	 * The number of PAN descriptors discovered during a scan has been
	 * reached.
	 */
	IEEE802154_LIMIT_REACHED = 0xfa,
	/*
	 * The PIBAttribute parameter specifies an attribute that is a read-only
	 * attribute.
	 */
	IEEE802154_READ_ONLY = 0xfb,
	/*
	 * A request to perform a scan operation failed because the MLME was
	 * in the process of performing a previously initiated scan operation.
	 */
	IEEE802154_SCAN_IN_PROGRESS = 0xfc,
	/* The outgoing superframe overlaps the incoming superframe. */
	IEEE802154_SUPERFRAME_OVERLAP = 0xfd,
	/* Any other error situation. */
	IEEE802154_SYSTEM_ERROR = 0xff,
};

/**
 * enum ieee802154_filtering_level - Filtering levels applicable to a PHY
 *
 * @IEEE802154_FILTERING_NONE: No filtering at all, what is received is
 *	forwarded to the softMAC
 * @IEEE802154_FILTERING_1_FCS: First filtering level, frames with an invalid
 *	FCS should be dropped
 * @IEEE802154_FILTERING_2_PROMISCUOUS: Second filtering level, promiscuous
 *	mode as described in the spec, identical in terms of filtering to the
 *	level one on PHY side, but at the MAC level the frame should be
 *	forwarded to the upper layer directly
 * @IEEE802154_FILTERING_3_SCAN: Third filtering level, scan related, where
 *	only beacons must be processed, all remaining traffic gets dropped
 * @IEEE802154_FILTERING_4_FRAME_FIELDS: Fourth filtering level actually
 *	enforcing the validity of the content of the frame with various checks
 */
enum ieee802154_filtering_level {
	IEEE802154_FILTERING_NONE,
	IEEE802154_FILTERING_1_FCS,
	IEEE802154_FILTERING_2_PROMISCUOUS,
	IEEE802154_FILTERING_3_SCAN,
	IEEE802154_FILTERING_4_FRAME_FIELDS,
};

/* frame control handling */
#define IEEE802154_FCTL_FTYPE		0x0003
#define IEEE802154_FCTL_ACKREQ		0x0020
#define IEEE802154_FCTL_SECEN		0x0004
#define IEEE802154_FCTL_INTRA_PAN	0x0040
#define IEEE802154_FCTL_DADDR		0x0c00
#define IEEE802154_FCTL_SADDR		0xc000

#define IEEE802154_FTYPE_DATA		0x0001

#define IEEE802154_FCTL_ADDR_NONE	0x0000
#define IEEE802154_FCTL_DADDR_SHORT	0x0800
#define IEEE802154_FCTL_DADDR_EXTENDED	0x0c00
#define IEEE802154_FCTL_SADDR_SHORT	0x8000
#define IEEE802154_FCTL_SADDR_EXTENDED	0xc000

/*
 * ieee802154_is_data - check if type is IEEE802154_FTYPE_DATA
 * @fc: frame control bytes in little-endian byteorder
 */
static inline int ieee802154_is_data(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE802154_FCTL_FTYPE)) ==
		cpu_to_le16(IEEE802154_FTYPE_DATA);
}

/**
 * ieee802154_is_secen - check if Security bit is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee802154_is_secen(__le16 fc)
{
	return fc & cpu_to_le16(IEEE802154_FCTL_SECEN);
}

/**
 * ieee802154_is_ackreq - check if acknowledgment request bit is set
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee802154_is_ackreq(__le16 fc)
{
	return fc & cpu_to_le16(IEEE802154_FCTL_ACKREQ);
}

/**
 * ieee802154_is_intra_pan - check if intra pan id communication
 * @fc: frame control bytes in little-endian byteorder
 */
static inline bool ieee802154_is_intra_pan(__le16 fc)
{
	return fc & cpu_to_le16(IEEE802154_FCTL_INTRA_PAN);
}

/*
 * ieee802154_daddr_mode - get daddr mode from fc
 * @fc: frame control bytes in little-endian byteorder
 */
static inline __le16 ieee802154_daddr_mode(__le16 fc)
{
	return fc & cpu_to_le16(IEEE802154_FCTL_DADDR);
}

/*
 * ieee802154_saddr_mode - get saddr mode from fc
 * @fc: frame control bytes in little-endian byteorder
 */
static inline __le16 ieee802154_saddr_mode(__le16 fc)
{
	return fc & cpu_to_le16(IEEE802154_FCTL_SADDR);
}

/**
 * ieee802154_is_valid_psdu_len - check if psdu len is valid
 * available lengths:
 *	0-4	Reserved
 *	5	MPDU (Acknowledgment)
 *	6-8	Reserved
 *	9-127	MPDU
 *
 * @len: psdu len with (MHR + payload + MFR)
 */
static inline bool ieee802154_is_valid_psdu_len(u8 len)
{
	return (len == IEEE802154_ACK_PSDU_LEN ||
		(len >= IEEE802154_MIN_PSDU_LEN && len <= IEEE802154_MTU));
}

/**
 * ieee802154_is_valid_extended_unicast_addr - check if extended addr is valid
 * @addr: extended addr to check
 */
static inline bool ieee802154_is_valid_extended_unicast_addr(__le64 addr)
{
	/* Bail out if the address is all zero, or if the group
	 * address bit is set.
	 */
	return ((addr != cpu_to_le64(0x0000000000000000ULL)) &&
		!(addr & cpu_to_le64(0x0100000000000000ULL)));
}

/**
 * ieee802154_is_broadcast_short_addr - check if short addr is broadcast
 * @addr: short addr to check
 */
static inline bool ieee802154_is_broadcast_short_addr(__le16 addr)
{
	return (addr == cpu_to_le16(IEEE802154_ADDR_SHORT_BROADCAST));
}

/**
 * ieee802154_is_unspec_short_addr - check if short addr is unspecified
 * @addr: short addr to check
 */
static inline bool ieee802154_is_unspec_short_addr(__le16 addr)
{
	return (addr == cpu_to_le16(IEEE802154_ADDR_SHORT_UNSPEC));
}

/**
 * ieee802154_is_valid_src_short_addr - check if source short address is valid
 * @addr: short addr to check
 */
static inline bool ieee802154_is_valid_src_short_addr(__le16 addr)
{
	return !(ieee802154_is_broadcast_short_addr(addr) ||
		 ieee802154_is_unspec_short_addr(addr));
}

/**
 * ieee802154_random_extended_addr - generates a random extended address
 * @addr: extended addr pointer to place the random address
 */
static inline void ieee802154_random_extended_addr(__le64 *addr)
{
	get_random_bytes(addr, IEEE802154_EXTENDED_ADDR_LEN);

	/* clear the group bit, and set the locally administered bit */
	((u8 *)addr)[IEEE802154_EXTENDED_ADDR_LEN - 1] &= ~0x01;
	((u8 *)addr)[IEEE802154_EXTENDED_ADDR_LEN - 1] |= 0x02;
}

#endif /* LINUX_IEEE802154_H */
