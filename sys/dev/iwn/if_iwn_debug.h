/*-
 * Copyright (c) 2013 Cedric GROSS <c.gross@kreiz-it.fr>
 * Copyright (c) 2011 Intel Corporation
 * Copyright (c) 2007-2009
 *      Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2008
 *      Benjamin Close <benjsc@FreeBSD.org>
 * Copyright (c) 2008 Sam Leffler, Errno Consulting
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#ifndef	__IF_IWN_DEBUG_H__
#define	__IF_IWN_DEBUG_H__

#ifdef	IWN_DEBUG
enum {
	IWN_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	IWN_DEBUG_RECV		= 0x00000002,	/* basic recv operation */
	IWN_DEBUG_STATE		= 0x00000004,	/* 802.11 state transitions */
	IWN_DEBUG_TXPOW		= 0x00000008,	/* tx power processing */
	IWN_DEBUG_RESET		= 0x00000010,	/* reset processing */
	IWN_DEBUG_OPS		= 0x00000020,	/* iwn_ops processing */
	IWN_DEBUG_BEACON 	= 0x00000040,	/* beacon handling */
	IWN_DEBUG_WATCHDOG 	= 0x00000080,	/* watchdog timeout */
	IWN_DEBUG_INTR		= 0x00000100,	/* ISR */
	IWN_DEBUG_CALIBRATE	= 0x00000200,	/* periodic calibration */
	IWN_DEBUG_NODE		= 0x00000400,	/* node management */
	IWN_DEBUG_LED		= 0x00000800,	/* led management */
	IWN_DEBUG_CMD		= 0x00001000,	/* cmd submission */
	IWN_DEBUG_TXRATE	= 0x00002000,	/* TX rate debugging */
	IWN_DEBUG_PWRSAVE	= 0x00004000,	/* Power save operations */
	IWN_DEBUG_SCAN		= 0x00008000,	/* Scan related operations */
	IWN_DEBUG_STATS		= 0x00010000,	/* Statistics updates */
	IWN_DEBUG_AMPDU		= 0x00020000,	/* A-MPDU specific Tx */
	IWN_DEBUG_REGISTER	= 0x20000000,	/* print chipset register */
	IWN_DEBUG_TRACE		= 0x40000000,	/* Print begin and start driver function */
	IWN_DEBUG_FATAL		= 0x80000000,	/* fatal errors */
	IWN_DEBUG_ANY		= 0xffffffff
};

#define DPRINTF(sc, m, fmt, ...) do {			\
	if (sc->sc_debug & (m))				\
		printf(fmt, __VA_ARGS__);		\
} while (0)

static const char *
iwn_intr_str(uint8_t cmd)
{
	switch (cmd) {
	/* Notifications */
	case IWN_UC_READY:		return "UC_READY";
	case IWN_ADD_NODE_DONE:		return "ADD_NODE_DONE";
	case IWN_TX_DONE:		return "TX_DONE";
	case IWN_START_SCAN:		return "START_SCAN";
	case IWN_STOP_SCAN:		return "STOP_SCAN";
	case IWN_RX_STATISTICS:		return "RX_STATS";
	case IWN_BEACON_STATISTICS:	return "BEACON_STATS";
	case IWN_STATE_CHANGED:		return "STATE_CHANGED";
	case IWN_BEACON_MISSED:		return "BEACON_MISSED";
	case IWN_RX_PHY:		return "RX_PHY";
	case IWN_MPDU_RX_DONE:		return "MPDU_RX_DONE";
	case IWN_RX_DONE:		return "RX_DONE";
	case IWN_RX_COMPRESSED_BA:	return "RX_COMPRESSED_BA";

	/* Command Notifications */
	case IWN_CMD_RXON:		return "IWN_CMD_RXON";
	case IWN_CMD_RXON_ASSOC:	return "IWN_CMD_RXON_ASSOC";
	case IWN_CMD_EDCA_PARAMS:	return "IWN_CMD_EDCA_PARAMS";
	case IWN_CMD_TIMING:		return "IWN_CMD_TIMING";
	case IWN_CMD_LINK_QUALITY:	return "IWN_CMD_LINK_QUALITY";
	case IWN_CMD_SET_LED:		return "IWN_CMD_SET_LED";
	case IWN5000_CMD_WIMAX_COEX:	return "IWN5000_CMD_WIMAX_COEX";
	case IWN_TEMP_NOTIFICATION:	return "IWN_TEMP_NOTIFICATION";
	case IWN5000_CMD_CALIB_CONFIG:	return "IWN5000_CMD_CALIB_CONFIG";
	case IWN5000_CMD_CALIB_RESULT:	return "IWN5000_CMD_CALIB_RESULT";
	case IWN5000_CMD_CALIB_COMPLETE: return "IWN5000_CMD_CALIB_COMPLETE";
	case IWN_CMD_SET_POWER_MODE:	return "IWN_CMD_SET_POWER_MODE";
	case IWN_CMD_SCAN:		return "IWN_CMD_SCAN";
	case IWN_CMD_SCAN_RESULTS:	return "IWN_CMD_SCAN_RESULTS";
	case IWN_CMD_TXPOWER:		return "IWN_CMD_TXPOWER";
	case IWN_CMD_TXPOWER_DBM:	return "IWN_CMD_TXPOWER_DBM";
	case IWN5000_CMD_TX_ANT_CONFIG:	return "IWN5000_CMD_TX_ANT_CONFIG";
	case IWN_CMD_BT_COEX:		return "IWN_CMD_BT_COEX";
	case IWN_CMD_SET_CRITICAL_TEMP:	return "IWN_CMD_SET_CRITICAL_TEMP";
	case IWN_CMD_SET_SENSITIVITY:	return "IWN_CMD_SET_SENSITIVITY";
	case IWN_CMD_PHY_CALIB:		return "IWN_CMD_PHY_CALIB";

	/* Bluetooth commands */
	case IWN_CMD_BT_COEX_PRIOTABLE:	return "IWN_CMD_BT_COEX_PRIOTABLE";
	case IWN_CMD_BT_COEX_PROT:	return "IWN_CMD_BT_COEX_PROT";
	case IWN_CMD_BT_COEX_NOTIF:	return "IWN_CMD_BT_COEX_NOTIF";

	/* PAN commands */
	case IWN_CMD_WIPAN_PARAMS:	return "IWN_CMD_WIPAN_PARAMS";
	case IWN_CMD_WIPAN_RXON:	return "IWN_CMD_WIPAN_RXON";
	case IWN_CMD_WIPAN_RXON_TIMING:	return "IWN_CMD_WIPAN_RXON_TIMING";
	case IWN_CMD_WIPAN_RXON_ASSOC:	return "IWN_CMD_WIPAN_RXON_ASSOC";
	case IWN_CMD_WIPAN_QOS_PARAM:	return "IWN_CMD_WIPAN_QOS_PARAM";
	case IWN_CMD_WIPAN_WEPKEY:	return "IWN_CMD_WIPAN_WEPKEY";
	case IWN_CMD_WIPAN_P2P_CHANNEL_SWITCH:
		return "IWN_CMD_WIPAN_P2P_CHANNEL_SWITCH";
	case IWN_CMD_WIPAN_NOA_NOTIFICATION:
		return "IWN_CMD_WIPAN_NOA_NOTIFICATION";
	case IWN_CMD_WIPAN_DEACTIVATION_COMPLETE:
		return "IWN_CMD_WIPAN_DEACTIVATION_COMPLETE";
	}
	return "UNKNOWN INTR NOTIF/CMD";
}
#else
#define DPRINTF(sc, m, fmt, ...) do { (void) sc; } while (0)
#endif

#endif	/* __IF_IWN_DEBUG_H__ */
