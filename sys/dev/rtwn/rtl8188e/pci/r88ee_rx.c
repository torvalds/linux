/*-
 * Copyright (c) 2017 Farhan Khan <khanzf@gmail.com>
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

#include <dev/rtwn/rtl8188e/pci/r88ee.h>
#include <dev/rtwn/rtl8188e/pci/r88ee_reg.h>

int
r88ee_get_intr_status(struct rtwn_pci_softc *pc, int *rings)
{
	struct rtwn_softc *sc = &pc->pc_sc;
	uint32_t status, status_ex;
	int ret;

	*rings = 0;
	status = rtwn_read_4(sc, R88E_HISR);
	status_ex = rtwn_read_4(sc, R88E_HISRE);
	RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: HISR %08X, HISRE %08X\n",
	    __func__, status, status_ex);
	if ((status == 0 || status == 0xffffffff) &&
	    (status_ex == 0 || status_ex == 0xffffffff))
		return (0);

	/* Disable interrupts */
	rtwn_write_4(sc, R88E_HIMR, 0);
	rtwn_write_4(sc, R88E_HIMRE, 0);

	/* Ack interrupts */
	rtwn_write_4(sc, R88E_HISR, status);
	rtwn_write_4(sc, R88E_HISRE, status_ex);

	if (status & R88E_HIMR_HIGHDOK)
		*rings |= (1 << RTWN_PCI_HIGH_QUEUE);
	if (status & R88E_HIMR_MGNTDOK)
		*rings |= (1 << RTWN_PCI_MGNT_QUEUE);
	if (status & R88E_HIMR_BKDOK)
		*rings |= (1 << RTWN_PCI_BK_QUEUE);
	if (status & R88E_HIMR_BEDOK)
		*rings |= (1 << RTWN_PCI_BE_QUEUE);
	if (status & R88E_HIMR_VIDOK)
		*rings |= (1 << RTWN_PCI_VI_QUEUE);
	if (status & R88E_HIMR_VODOK)
		*rings |= (1 << RTWN_PCI_VO_QUEUE);

	ret = 0;
	if (status_ex & R88E_HIMRE_RXERR)
		ret |= RTWN_PCI_INTR_RX_ERROR;
	if (status_ex & R88E_HIMRE_RXFOVW)
		ret |= RTWN_PCI_INTR_RX_OVERFLOW;
	if (status & R88E_HIMR_RDU)
		ret |= RTWN_PCI_INTR_RX_DESC_UNAVAIL;
	if (status & R88E_HIMR_ROK)
		ret |= RTWN_PCI_INTR_RX_DONE;
	if (status_ex & R88E_HIMRE_TXERR)
		ret |= RTWN_PCI_INTR_TX_ERROR;
	if (status_ex & R88E_HIMRE_TXFOVW)
		ret |= RTWN_PCI_INTR_TX_OVERFLOW;
	if (status & R88E_HIMR_TXRPT)
		ret |= RTWN_PCI_INTR_TX_REPORT;
	if (status & R88E_HIMR_PSTIMEOUT)
		ret |= RTWN_PCI_INTR_PS_TIMEOUT;

	return (ret);
}

#define	R88E_INT_ENABLE	(R88E_HIMR_ROK | R88E_HIMR_RDU | R88E_HIMR_VODOK | \
			 R88E_HIMR_VIDOK | R88E_HIMR_BEDOK | \
			 R88E_HIMR_BKDOK | R88E_HIMR_MGNTDOK | \
			 R88E_HIMR_HIGHDOK | R88E_HIMR_TXRPT)

#define	R88E_INT_ENABLE_EX	(R88E_HIMRE_RXFOVW | R88E_HIMRE_RXERR)

void
r88ee_enable_intr(struct rtwn_pci_softc *pc)
{
	struct rtwn_softc *sc = &pc->pc_sc;

	/* Enable interrupts */
	rtwn_write_4(sc, R88E_HIMR, R88E_INT_ENABLE);
	rtwn_write_4(sc, R88E_HIMRE, R88E_INT_ENABLE_EX);
}

void
r88ee_start_xfers(struct rtwn_softc *sc)
{
	/* Clear pending interrupts. */
	rtwn_write_4(sc, R88E_HISR, 0xffffffff);
	rtwn_write_4(sc, R88E_HISRE, 0xffffffff);

	/* Enable interrupts. */
	rtwn_write_4(sc, R88E_HIMR, R88E_INT_ENABLE);
	rtwn_write_4(sc, R88E_HIMRE, R88E_INT_ENABLE_EX);
}

#undef R88E_INT_ENABLE
#undef R88E_INT_ENABLE_EX
