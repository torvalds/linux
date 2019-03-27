/*	$OpenBSD: if_rtwn.c,v 1.6 2015/08/28 00:03:53 deraadt Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/pci/rtwn_pci_var.h>

#include <dev/rtwn/rtl8192c/pci/r92ce.h>
#include <dev/rtwn/rtl8192c/pci/r92ce_reg.h>


int
r92ce_get_intr_status(struct rtwn_pci_softc *pc, int *rings)
{
	struct rtwn_softc *sc = &pc->pc_sc;
	uint32_t status;
	int ret;

	*rings = 0;
	status = rtwn_read_4(sc, R92C_HISR);
	RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: HISR %08X, HISRE %04X\n",
	    __func__, status, rtwn_read_2(sc, R92C_HISRE));
	if (status == 0 || status == 0xffffffff)
		return (0);

	/* Disable interrupts. */
	rtwn_write_4(sc, R92C_HIMR, 0);

	/* Ack interrupts. */
	rtwn_write_4(sc, R92C_HISR, status);

	if (status & R92C_IMR_BDOK)
		*rings |= (1 << RTWN_PCI_BEACON_QUEUE);
	if (status & R92C_IMR_HIGHDOK)
		*rings |= (1 << RTWN_PCI_HIGH_QUEUE);
	if (status & R92C_IMR_MGNTDOK)
		*rings |= (1 << RTWN_PCI_MGNT_QUEUE);
	if (status & R92C_IMR_BKDOK)
		*rings |= (1 << RTWN_PCI_BK_QUEUE);
	if (status & R92C_IMR_BEDOK)
		*rings |= (1 << RTWN_PCI_BE_QUEUE);
	if (status & R92C_IMR_VIDOK)
		*rings |= (1 << RTWN_PCI_VI_QUEUE);
	if (status & R92C_IMR_VODOK)
		*rings |= (1 << RTWN_PCI_VO_QUEUE);

	ret = 0;
	if (status & R92C_IMR_RXFOVW)
		ret |= RTWN_PCI_INTR_RX_OVERFLOW;
	if (status & R92C_IMR_RDU)
		ret |= RTWN_PCI_INTR_RX_DESC_UNAVAIL;
	if (status & R92C_IMR_ROK)
		ret |= RTWN_PCI_INTR_RX_DONE;
	if (status & R92C_IMR_TXFOVW)
		ret |= RTWN_PCI_INTR_TX_OVERFLOW;
	if (status & R92C_IMR_PSTIMEOUT)
		ret |= RTWN_PCI_INTR_PS_TIMEOUT;

	return (ret);
}

#define R92C_INT_ENABLE (R92C_IMR_ROK | R92C_IMR_VODOK | R92C_IMR_VIDOK | \
			R92C_IMR_BEDOK | R92C_IMR_BKDOK | R92C_IMR_MGNTDOK | \
			R92C_IMR_HIGHDOK | R92C_IMR_BDOK | R92C_IMR_RDU | \
			R92C_IMR_RXFOVW)
void
r92ce_enable_intr(struct rtwn_pci_softc *pc)
{
	struct rtwn_softc *sc = &pc->pc_sc;

	/* Enable interrupts. */
	rtwn_write_4(sc, R92C_HIMR, R92C_INT_ENABLE);
}

void
r92ce_start_xfers(struct rtwn_softc *sc)
{
	/* Clear pending interrupts. */
	rtwn_write_4(sc, R92C_HISR, 0xffffffff);

	/* Enable interrupts. */
	rtwn_write_4(sc, R92C_HIMR, R92C_INT_ENABLE);
}
#undef R92C_INT_ENABLE
