/*-
 * Copyright (c) 2006,2007
 *	Damien Bergamini <damien.bergamini@free.fr>
 *	Benjamin Close <Benjamin.Close@clearchain.com>
 * Copyright (c) 2015 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#ifndef __IF_WPI_DEBUG_H__
#define __IF_WPI_DEBUG_H__

#ifdef WPI_DEBUG
enum {
	WPI_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	WPI_DEBUG_RECV		= 0x00000002,	/* basic recv operation */
	WPI_DEBUG_STATE		= 0x00000004,	/* 802.11 state transitions */
	WPI_DEBUG_HW		= 0x00000008,	/* Stage 1 (eeprom) debugging */
	WPI_DEBUG_RESET		= 0x00000010,	/* reset processing */
	WPI_DEBUG_FIRMWARE	= 0x00000020,	/* firmware(9) loading debug */
	WPI_DEBUG_BEACON	= 0x00000040,	/* beacon handling */
	WPI_DEBUG_WATCHDOG	= 0x00000080,	/* watchdog timeout */
	WPI_DEBUG_INTR		= 0x00000100,	/* ISR */
	WPI_DEBUG_SCAN		= 0x00000200,	/* Scan related operations */
	WPI_DEBUG_NOTIFY	= 0x00000400,	/* State 2 Notif intr debug */
	WPI_DEBUG_TEMP		= 0x00000800,	/* TXPower/Temp Calibration */
	WPI_DEBUG_CMD		= 0x00001000,	/* cmd submission */
	WPI_DEBUG_TRACE		= 0x00002000,	/* Print begin and start driver function */
	WPI_DEBUG_PWRSAVE	= 0x00004000,	/* Power save operations */
	WPI_DEBUG_EEPROM	= 0x00008000,	/* EEPROM info */
	WPI_DEBUG_NODE		= 0x00010000,	/* node addition/removal */
	WPI_DEBUG_KEY		= 0x00020000,	/* node key management */
	WPI_DEBUG_EDCA		= 0x00040000,	/* WME info */
	WPI_DEBUG_REGISTER	= 0x00080000,	/* print chipset register */
	WPI_DEBUG_BMISS		= 0x00100000,	/* print number of missed beacons */
	WPI_DEBUG_ANY		= 0xffffffff
};

#define DPRINTF(sc, m, ...) do {		\
	if (sc->sc_debug & (m))			\
		printf(__VA_ARGS__);		\
} while (0)

#define TRACE_STR_BEGIN		"->%s: begin\n"
#define TRACE_STR_DOING		"->Doing %s\n"
#define TRACE_STR_END		"->%s: end\n"
#define TRACE_STR_END_ERR	"->%s: end in error\n"

#define WPI_DESC(x) case x:	return #x

static const char *wpi_cmd_str(int cmd)
{
	switch (cmd) {
		/* Notifications. */
		WPI_DESC(WPI_UC_READY);
		WPI_DESC(WPI_RX_DONE);
		WPI_DESC(WPI_START_SCAN);
		WPI_DESC(WPI_SCAN_RESULTS);
		WPI_DESC(WPI_STOP_SCAN);
		WPI_DESC(WPI_BEACON_SENT);
		WPI_DESC(WPI_RX_STATISTICS);
		WPI_DESC(WPI_BEACON_STATISTICS);
		WPI_DESC(WPI_STATE_CHANGED);
		WPI_DESC(WPI_BEACON_MISSED);

		/* Command notifications. */
		WPI_DESC(WPI_CMD_RXON);
		WPI_DESC(WPI_CMD_RXON_ASSOC);
		WPI_DESC(WPI_CMD_EDCA_PARAMS);
		WPI_DESC(WPI_CMD_TIMING);
		WPI_DESC(WPI_CMD_ADD_NODE);
		WPI_DESC(WPI_CMD_DEL_NODE);
		WPI_DESC(WPI_CMD_TX_DATA);
		WPI_DESC(WPI_CMD_MRR_SETUP);
		WPI_DESC(WPI_CMD_SET_LED);
		WPI_DESC(WPI_CMD_SET_POWER_MODE);
		WPI_DESC(WPI_CMD_SCAN);
		WPI_DESC(WPI_CMD_SCAN_ABORT);
		WPI_DESC(WPI_CMD_SET_BEACON);
		WPI_DESC(WPI_CMD_TXPOWER);
		WPI_DESC(WPI_CMD_BT_COEX);

	default:
		return "UNKNOWN CMD";
	}
}

/*
 * Translate CSR code to string
 */
static const char *wpi_get_csr_string(size_t csr)
{
	switch (csr) {
		WPI_DESC(WPI_HW_IF_CONFIG);
		WPI_DESC(WPI_INT);
		WPI_DESC(WPI_INT_MASK);
		WPI_DESC(WPI_FH_INT);
		WPI_DESC(WPI_GPIO_IN);
		WPI_DESC(WPI_RESET);
		WPI_DESC(WPI_GP_CNTRL);
		WPI_DESC(WPI_EEPROM);
		WPI_DESC(WPI_EEPROM_GP);
		WPI_DESC(WPI_GIO);
		WPI_DESC(WPI_UCODE_GP1);
		WPI_DESC(WPI_UCODE_GP2);
		WPI_DESC(WPI_GIO_CHICKEN);
		WPI_DESC(WPI_ANA_PLL);
		WPI_DESC(WPI_DBG_HPET_MEM);
	default:
		KASSERT(0, ("Unknown CSR: %d\n", csr));
		return "UNKNOWN CSR";
	}
}

static const char *wpi_get_prph_string(size_t prph)
{
	switch (prph) {
		WPI_DESC(WPI_APMG_CLK_CTRL);
		WPI_DESC(WPI_APMG_PS);
		WPI_DESC(WPI_APMG_PCI_STT);
		WPI_DESC(WPI_APMG_RFKILL);
	default:
		KASSERT(0, ("Unknown register: %d\n", prph));
		return "UNKNOWN PRPH";
	}
}

#else
#define DPRINTF(sc, m, ...)	do { (void) sc; } while (0)
#endif

#endif	/* __IF_WPI_DEBUG_H__ */
