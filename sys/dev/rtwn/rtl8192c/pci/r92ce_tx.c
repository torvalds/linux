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
#include <dev/rtwn/rtl8192c/pci/r92ce_tx_desc.h>


void
r92ce_setup_tx_desc(struct rtwn_pci_softc *pc, void *desc,
    uint32_t next_desc_addr)
{
	struct r92ce_tx_desc *txd = desc;

	/* setup tx desc */
	txd->nextdescaddr = htole32(next_desc_addr);
}

void
r92ce_tx_postsetup(struct rtwn_pci_softc *pc, void *desc,
    bus_dma_segment_t segs[])
{
	struct r92ce_tx_desc *txd = desc;

	txd->txbufaddr = htole32(segs[0].ds_addr);
	txd->txbufsize = txd->pktlen;
	bus_space_barrier(pc->pc_st, pc->pc_sh, 0, pc->pc_mapsize,
	    BUS_SPACE_BARRIER_WRITE);
}

void
r92ce_copy_tx_desc(void *dest, const void *src)
{
	struct r92ce_tx_desc *txd = dest;
	size_t len = sizeof(struct r92c_tx_desc) +
	    sizeof(txd->txbufsize) + sizeof(txd->pad);

	if (src != NULL)
		memcpy(dest, src, len);
	else
		memset(dest, 0, len);
}

void
r92ce_dump_tx_desc(struct rtwn_softc *sc, const void *desc)
{
#ifdef RTWN_DEBUG
	const struct r92ce_tx_desc *txd = desc;

	RTWN_DPRINTF(sc, RTWN_DEBUG_XMIT_DESC,
	    "%s: len %d, off %d, flags0 %02X, dw: 1 %08X, 2 %08X, 3 %04X "
	    "(seq %04X), 4 %08X, 5 %08X, 6 %08X, size %04X, pad %04X, "
	    "addr: %08X (64: %08X), next: %08X (64: %08X), "
	    "rsvd: %08X %08X %08X %08X\n",
	    __func__, le16toh(txd->pktlen), txd->offset, txd->flags0,
	    le32toh(txd->txdw1), le32toh(txd->txdw2), le16toh(txd->txdw3),
	    le16toh(txd->txdseq), le32toh(txd->txdw4), le32toh(txd->txdw5),
	    le32toh(txd->txdw6), le16toh(txd->txbufsize), le16toh(txd->pad),
	    le32toh(txd->txbufaddr), le32toh(txd->txbufaddr64),
	    le32toh(txd->nextdescaddr), le32toh(txd->nextdescaddr64),
	    le32toh(txd->reserved[0]), le32toh(txd->reserved[1]),
	    le32toh(txd->reserved[2]), le32toh(txd->reserved[3]));
#endif
}
