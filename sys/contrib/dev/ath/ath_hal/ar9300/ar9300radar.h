/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _ATH_AR9300_RADAR_H_
#define _ATH_AR9300_RADAR_H_

#define	HAL_RADAR_SMASK		0x0000FFFF	/* Sequence number mask */
#define	HAL_RADAR_SSHIFT	16		/* Shift for Reader seq # stored in upper
						   16 bits, writer's is lower 16 bits */
#define	HAL_RADAR_IMASK		0x0000FFFF	/* Index number mask */
#define	HAL_RADAR_ISHIFT	16		/* Shift for index stored in upper 16 bits
						   of reader reset value */
#define HAL_RADAR_FIRPWR	-45
#define HAL_RADAR_RRSSI		14
#define HAL_RADAR_HEIGHT	20
#define HAL_RADAR_PRSSI		24
#define HAL_RADAR_INBAND	6

#define HAL_RADAR_TSMASK	0x7FFF		/* Mask for time stamp from descriptor */
#define	HAL_RADAR_TSSHIFT	15		/* Shift for time stamp from descriptor */

#define	HAL_AR_RADAR_RSSI_THR		5	/* in dB */
#define	HAL_AR_RADAR_RESET_INT		1	/* in secs */
#define	HAL_AR_RADAR_MAX_HISTORY	500
#define	HAL_AR_REGION_WIDTH		128
#define	HAL_AR_RSSI_THRESH_STRONG_PKTS	17	/* in dB */
#define	HAL_AR_RSSI_DOUBLE_THRESHOLD	15	/* in dB */
#define	HAL_AR_MAX_NUM_ACK_REGIONS	9
#define	HAL_AR_ACK_DETECT_PAR_THRESH	20
#define	HAL_AR_PKT_COUNT_THRESH		20

#endif
