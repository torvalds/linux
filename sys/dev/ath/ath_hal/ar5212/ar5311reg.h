/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */
#ifndef _DEV_ATH_AR5311REG_H_
#define _DEV_ATH_AR5311REG_H_

/*
 * Definitions for the Atheros 5311 chipset.
 */
#define	AR5311_QDCLKGATE	0x005c	/* MAC QCU/DCU clock gating control */
#define	AR5311_QDCLKGATE_QCU_M	0x0000FFFF /* QCU clock disable */
#define	AR5311_QDCLKGATE_DCU_M	0x07FF0000 /* DCU clock disable */

#define	AR5311_RXCFG_DEF_RX_ANTENNA	0x00000008 /* Default Receive Antenna */

/*
 * NOTE: MAC_5211/MAC_5311 difference
 * On Oahu the TX latency field has increased from 6 bits to 9 bits.
 * The RX latency field is unchanged but is shifted over 3 bits.
 */
#define	AR5311_USEC_TX_LAT_M	0x000FC000 /* tx latency (usec) */
#define	AR5311_USEC_TX_LAT_S	14
#define	AR5311_USEC_RX_LAT_M	0x03F00000 /* rx latency (usec) */
#define	AR5311_USEC_RX_LAT_S	20

/*
 * NOTE: MAC_5211/MAC_5311 difference
 * On Maui2/Spirit the frame sequence number is controlled per DCU.
 * On Oahu the frame sequence number is global across all DCUs and
 * is controlled
 */
#define	AR5311_D_MISC_SEQ_NUM_CONTROL	0x01000000 /* seq num local or global */
#define	AR5311_DIAG_USE_ECO	0x00000400	/* "super secret" enable ECO */

#endif /* _DEV_ATH_AR5311REG_H_ */
