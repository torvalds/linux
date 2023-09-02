/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Oliver Hartkopp <socketcan@hartkopp.net>
 * Copyright (C) 2020 Marc Kleine-Budde <kernel@pengutronix.de>
 */

#ifndef _CAN_LENGTH_H
#define _CAN_LENGTH_H

/*
 * Size of a Classical CAN Standard Frame
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * Start-of-frame			1
 * Identifier				11
 * Remote transmission request (RTR)	1
 * Identifier extension bit (IDE)	1
 * Reserved bit (r0)			1
 * Data length code (DLC)		4
 * Data field				0...64
 * CRC					15
 * CRC delimiter			1
 * ACK slot				1
 * ACK delimiter			1
 * End-of-frame (EOF)			7
 * Inter frame spacing			3
 *
 * rounded up and ignoring bitstuffing
 */
#define CAN_FRAME_OVERHEAD_SFF DIV_ROUND_UP(47, 8)

/*
 * Size of a Classical CAN Extended Frame
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * Start-of-frame			1
 * Identifier A				11
 * Substitute remote request (SRR)	1
 * Identifier extension bit (IDE)	1
 * Identifier B				18
 * Remote transmission request (RTR)	1
 * Reserved bits (r1, r0)		2
 * Data length code (DLC)		4
 * Data field				0...64
 * CRC					15
 * CRC delimiter			1
 * ACK slot				1
 * ACK delimiter			1
 * End-of-frame (EOF)			7
 * Inter frame spacing			3
 *
 * rounded up and ignoring bitstuffing
 */
#define CAN_FRAME_OVERHEAD_EFF DIV_ROUND_UP(67, 8)

/*
 * Size of a CAN-FD Standard Frame
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * Start-of-frame			1
 * Identifier				11
 * Reserved bit (r1)			1
 * Identifier extension bit (IDE)	1
 * Flexible data rate format (FDF)	1
 * Reserved bit (r0)			1
 * Bit Rate Switch (BRS)		1
 * Error Status Indicator (ESI)		1
 * Data length code (DLC)		4
 * Data field				0...512
 * Stuff Bit Count (SBC)		4
 * CRC					0...16: 17 20...64:21
 * CRC delimiter (CD)			1
 * Fixed Stuff bits (FSB)		0...16: 6 20...64:7
 * ACK slot (AS)			1
 * ACK delimiter (AD)			1
 * End-of-frame (EOF)			7
 * Inter frame spacing			3
 *
 * assuming CRC21, rounded up and ignoring dynamic bitstuffing
 */
#define CANFD_FRAME_OVERHEAD_SFF DIV_ROUND_UP(67, 8)

/*
 * Size of a CAN-FD Extended Frame
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * Start-of-frame			1
 * Identifier A				11
 * Substitute remote request (SRR)	1
 * Identifier extension bit (IDE)	1
 * Identifier B				18
 * Reserved bit (r1)			1
 * Flexible data rate format (FDF)	1
 * Reserved bit (r0)			1
 * Bit Rate Switch (BRS)		1
 * Error Status Indicator (ESI)		1
 * Data length code (DLC)		4
 * Data field				0...512
 * Stuff Bit Count (SBC)		4
 * CRC					0...16: 17 20...64:21
 * CRC delimiter (CD)			1
 * Fixed Stuff bits (FSB)		0...16: 6 20...64:7
 * ACK slot (AS)			1
 * ACK delimiter (AD)			1
 * End-of-frame (EOF)			7
 * Inter frame spacing			3
 *
 * assuming CRC21, rounded up and ignoring dynamic bitstuffing
 */
#define CANFD_FRAME_OVERHEAD_EFF DIV_ROUND_UP(86, 8)

/*
 * Maximum size of a Classical CAN frame
 * (rounded up and ignoring bitstuffing)
 */
#define CAN_FRAME_LEN_MAX (CAN_FRAME_OVERHEAD_EFF + CAN_MAX_DLEN)

/*
 * Maximum size of a CAN-FD frame
 * (rounded up and ignoring bitstuffing)
 */
#define CANFD_FRAME_LEN_MAX (CANFD_FRAME_OVERHEAD_EFF + CANFD_MAX_DLEN)

/*
 * can_cc_dlc2len(value) - convert a given data length code (dlc) of a
 * Classical CAN frame into a valid data length of max. 8 bytes.
 *
 * To be used in the CAN netdriver receive path to ensure conformance with
 * ISO 11898-1 Chapter 8.4.2.3 (DLC field)
 */
#define can_cc_dlc2len(dlc)	(min_t(u8, (dlc), CAN_MAX_DLEN))

/* helper to get the data length code (DLC) for Classical CAN raw DLC access */
static inline u8 can_get_cc_dlc(const struct can_frame *cf, const u32 ctrlmode)
{
	/* return len8_dlc as dlc value only if all conditions apply */
	if ((ctrlmode & CAN_CTRLMODE_CC_LEN8_DLC) &&
	    (cf->len == CAN_MAX_DLEN) &&
	    (cf->len8_dlc > CAN_MAX_DLEN && cf->len8_dlc <= CAN_MAX_RAW_DLC))
		return cf->len8_dlc;

	/* return the payload length as dlc value */
	return cf->len;
}

/* helper to set len and len8_dlc value for Classical CAN raw DLC access */
static inline void can_frame_set_cc_len(struct can_frame *cf, const u8 dlc,
					const u32 ctrlmode)
{
	/* the caller already ensured that dlc is a value from 0 .. 15 */
	if (ctrlmode & CAN_CTRLMODE_CC_LEN8_DLC && dlc > CAN_MAX_DLEN)
		cf->len8_dlc = dlc;

	/* limit the payload length 'len' to CAN_MAX_DLEN */
	cf->len = can_cc_dlc2len(dlc);
}

/* get data length from raw data length code (DLC) */
u8 can_fd_dlc2len(u8 dlc);

/* map the sanitized data length to an appropriate data length code */
u8 can_fd_len2dlc(u8 len);

/* calculate the CAN Frame length in bytes of a given skb */
unsigned int can_skb_get_frame_len(const struct sk_buff *skb);

/* map the data length to an appropriate data link layer length */
static inline u8 canfd_sanitize_len(u8 len)
{
	return can_fd_dlc2len(can_fd_len2dlc(len));
}

#endif /* !_CAN_LENGTH_H */
