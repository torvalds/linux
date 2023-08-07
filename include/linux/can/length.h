/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Oliver Hartkopp <socketcan@hartkopp.net>
 * Copyright (C) 2020 Marc Kleine-Budde <kernel@pengutronix.de>
 * Copyright (C) 2020, 2023 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#ifndef _CAN_LENGTH_H
#define _CAN_LENGTH_H

#include <linux/bits.h>
#include <linux/can.h>
#include <linux/can/netlink.h>
#include <linux/math.h>

/*
 * Size of a Classical CAN Standard Frame header in bits
 *
 * Name of Field				Bits
 * ---------------------------------------------------------
 * Start Of Frame (SOF)				1
 * Arbitration field:
 *	base ID					11
 *	Remote Transmission Request (RTR)	1
 * Control field:
 *	IDentifier Extension bit (IDE)		1
 *	FD Format indicator (FDF)		1
 *	Data Length Code (DLC)			4
 *
 * including all fields preceding the data field, ignoring bitstuffing
 */
#define CAN_FRAME_HEADER_SFF_BITS 19

/*
 * Size of a Classical CAN Extended Frame header in bits
 *
 * Name of Field				Bits
 * ---------------------------------------------------------
 * Start Of Frame (SOF)				1
 * Arbitration field:
 *	base ID					11
 *	Substitute Remote Request (SRR)		1
 *	IDentifier Extension bit (IDE)		1
 *	ID extension				18
 *	Remote Transmission Request (RTR)	1
 * Control field:
 *	FD Format indicator (FDF)		1
 *	Reserved bit (r0)			1
 *	Data length code (DLC)			4
 *
 * including all fields preceding the data field, ignoring bitstuffing
 */
#define CAN_FRAME_HEADER_EFF_BITS 39

/*
 * Size of a CAN-FD Standard Frame in bits
 *
 * Name of Field				Bits
 * ---------------------------------------------------------
 * Start Of Frame (SOF)				1
 * Arbitration field:
 *	base ID					11
 *	Remote Request Substitution (RRS)	1
 * Control field:
 *	IDentifier Extension bit (IDE)		1
 *	FD Format indicator (FDF)		1
 *	Reserved bit (res)			1
 *	Bit Rate Switch (BRS)			1
 *	Error Status Indicator (ESI)		1
 *	Data length code (DLC)			4
 *
 * including all fields preceding the data field, ignoring bitstuffing
 */
#define CANFD_FRAME_HEADER_SFF_BITS 22

/*
 * Size of a CAN-FD Extended Frame in bits
 *
 * Name of Field				Bits
 * ---------------------------------------------------------
 * Start Of Frame (SOF)				1
 * Arbitration field:
 *	base ID					11
 *	Substitute Remote Request (SRR)		1
 *	IDentifier Extension bit (IDE)		1
 *	ID extension				18
 *	Remote Request Substitution (RRS)	1
 * Control field:
 *	FD Format indicator (FDF)		1
 *	Reserved bit (res)			1
 *	Bit Rate Switch (BRS)			1
 *	Error Status Indicator (ESI)		1
 *	Data length code (DLC)			4
 *
 * including all fields preceding the data field, ignoring bitstuffing
 */
#define CANFD_FRAME_HEADER_EFF_BITS 41

/*
 * Size of a CAN CRC Field in bits
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * CRC sequence (CRC15)			15
 * CRC Delimiter			1
 *
 * ignoring bitstuffing
 */
#define CAN_FRAME_CRC_FIELD_BITS 16

/*
 * Size of a CAN-FD CRC17 Field in bits (length: 0..16)
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * Stuff Count				4
 * CRC Sequence (CRC17)			17
 * CRC Delimiter			1
 * Fixed stuff bits			6
 */
#define CANFD_FRAME_CRC17_FIELD_BITS 28

/*
 * Size of a CAN-FD CRC21 Field in bits (length: 20..64)
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * Stuff Count				4
 * CRC sequence (CRC21)			21
 * CRC Delimiter			1
 * Fixed stuff bits			7
 */
#define CANFD_FRAME_CRC21_FIELD_BITS 33

/*
 * Size of a CAN(-FD) Frame footer in bits
 *
 * Name of Field			Bits
 * ---------------------------------------------------------
 * ACK slot				1
 * ACK delimiter			1
 * End Of Frame (EOF)			7
 *
 * including all fields following the CRC field
 */
#define CAN_FRAME_FOOTER_BITS 9

/*
 * First part of the Inter Frame Space
 * (a.k.a. IMF - intermission field)
 */
#define CAN_INTERMISSION_BITS 3

/**
 * can_bitstuffing_len() - Calculate the maximum length with bitstuffing
 * @destuffed_len: length of a destuffed bit stream
 *
 * The worst bit stuffing case is a sequence in which dominant and
 * recessive bits alternate every four bits:
 *
 *   Destuffed: 1 1111  0000  1111  0000  1111
 *   Stuffed:   1 1111o 0000i 1111o 0000i 1111o
 *
 * Nomenclature
 *
 *  - "0": dominant bit
 *  - "o": dominant stuff bit
 *  - "1": recessive bit
 *  - "i": recessive stuff bit
 *
 * Aside from the first bit, one stuff bit is added every four bits.
 *
 * Return: length of the stuffed bit stream in the worst case scenario.
 */
#define can_bitstuffing_len(destuffed_len)			\
	(destuffed_len + (destuffed_len - 1) / 4)

#define __can_bitstuffing_len(bitstuffing, destuffed_len)	\
	(bitstuffing ? can_bitstuffing_len(destuffed_len) :	\
		       destuffed_len)

#define __can_cc_frame_bits(is_eff, bitstuffing,		\
			    intermission, data_len)		\
(								\
	__can_bitstuffing_len(bitstuffing,			\
		(is_eff ? CAN_FRAME_HEADER_EFF_BITS :		\
			  CAN_FRAME_HEADER_SFF_BITS) +		\
		(data_len) * BITS_PER_BYTE +			\
		CAN_FRAME_CRC_FIELD_BITS) +			\
	CAN_FRAME_FOOTER_BITS +					\
	(intermission ? CAN_INTERMISSION_BITS : 0)		\
)

#define __can_fd_frame_bits(is_eff, bitstuffing,		\
			    intermission, data_len)		\
(								\
	__can_bitstuffing_len(bitstuffing,			\
		(is_eff ? CANFD_FRAME_HEADER_EFF_BITS :		\
			  CANFD_FRAME_HEADER_SFF_BITS) +	\
		(data_len) * BITS_PER_BYTE) +			\
	((data_len) <= 16 ?					\
		CANFD_FRAME_CRC17_FIELD_BITS :			\
		CANFD_FRAME_CRC21_FIELD_BITS) +			\
	CAN_FRAME_FOOTER_BITS +					\
	(intermission ? CAN_INTERMISSION_BITS : 0)		\
)

/**
 * can_frame_bits() - Calculate the number of bits on the wire in a
 *	CAN frame
 * @is_fd: true: CAN-FD frame; false: Classical CAN frame.
 * @is_eff: true: Extended frame; false: Standard frame.
 * @bitstuffing: true: calculate the bitstuffing worst case; false:
 *	calculate the bitstuffing best case (no dynamic
 *	bitstuffing). CAN-FD's fixed stuff bits are always included.
 * @intermission: if and only if true, include the inter frame space
 *	assuming no bus idle (i.e. only the intermission). Strictly
 *	speaking, the inter frame space is not part of the
 *	frame. However, it is needed when calculating the delay
 *	between the Start Of Frame of two consecutive frames.
 * @data_len: length of the data field in bytes. Correspond to
 *	can(fd)_frame->len. Should be zero for remote frames. No
 *	sanitization is done on @data_len and it shall have no side
 *	effects.
 *
 * Return: the numbers of bits on the wire of a CAN frame.
 */
#define can_frame_bits(is_fd, is_eff, bitstuffing,		\
		       intermission, data_len)			\
(								\
	is_fd ? __can_fd_frame_bits(is_eff, bitstuffing,	\
				    intermission, data_len) :	\
		__can_cc_frame_bits(is_eff, bitstuffing,	\
				    intermission, data_len)	\
)

/*
 * Number of bytes in a CAN frame
 * (rounded up, including intermission)
 */
#define can_frame_bytes(is_fd, is_eff, bitstuffing, data_len)	\
	DIV_ROUND_UP(can_frame_bits(is_fd, is_eff, bitstuffing,	\
				    true, data_len),		\
		     BITS_PER_BYTE)

/*
 * Maximum size of a Classical CAN frame
 * (rounded up, ignoring bitstuffing but including intermission)
 */
#define CAN_FRAME_LEN_MAX can_frame_bytes(false, true, false, CAN_MAX_DLEN)

/*
 * Maximum size of a CAN-FD frame
 * (rounded up, ignoring dynamic bitstuffing but including intermission)
 */
#define CANFD_FRAME_LEN_MAX can_frame_bytes(true, true, false, CANFD_MAX_DLEN)

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
