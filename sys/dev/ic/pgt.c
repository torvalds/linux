/*	$OpenBSD: pgt.c,v 1.104 2023/11/10 15:51:20 bluhm Exp $  */

/*
 * Copyright (c) 2006 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2006 Marcus Glocker <mglocker@openbsd.org>
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

/*
 * Copyright (c) 2004 Fujitsu Laboratories of America, Inc.
 * Copyright (c) 2004 Brian Fundakowski Feldman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/endian.h>
#include <sys/sockio.h>
#include <sys/kthread.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_llc.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/pgtreg.h>
#include <dev/ic/pgtvar.h>

#include <dev/ic/if_wireg.h>
#include <dev/ic/if_wi_ieee.h>
#include <dev/ic/if_wivar.h>

#ifdef PGT_DEBUG
#define DPRINTF(x)	do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

#define	SETOID(oid, var, size) {					\
	if (pgt_oid_set(sc, oid, var, size) != 0)			\
		break;							\
}

/*
 * This is a driver for the Intersil Prism family of 802.11g network cards,
 * based upon version 1.2 of the Linux driver.
 */

#define SCAN_TIMEOUT			5	/* 5 seconds */

struct cfdriver pgt_cd = {
        NULL, "pgt", DV_IFNET
};

void	 pgt_media_status(struct ifnet *ifp, struct ifmediareq *imr);
int	 pgt_media_change(struct ifnet *ifp);
void	 pgt_write_memory_barrier(struct pgt_softc *);
uint32_t pgt_read_4(struct pgt_softc *, uint16_t);
void	 pgt_write_4(struct pgt_softc *, uint16_t, uint32_t);
void	 pgt_write_4_flush(struct pgt_softc *, uint16_t, uint32_t);
void	 pgt_debug_events(struct pgt_softc *, const char *);
uint32_t pgt_queue_frags_pending(struct pgt_softc *, enum pgt_queue);
void	 pgt_reinit_rx_desc_frag(struct pgt_softc *, struct pgt_desc *);
int	 pgt_load_tx_desc_frag(struct pgt_softc *, enum pgt_queue,
	     struct pgt_desc *);
void	 pgt_unload_tx_desc_frag(struct pgt_softc *, struct pgt_desc *);
int	 pgt_load_firmware(struct pgt_softc *);
void	 pgt_cleanup_queue(struct pgt_softc *, enum pgt_queue,
	     struct pgt_frag *);
int	 pgt_reset(struct pgt_softc *);
void	 pgt_stop(struct pgt_softc *, unsigned int);
void	 pgt_reboot(struct pgt_softc *);
void	 pgt_init_intr(struct pgt_softc *);
void	 pgt_update_intr(struct pgt_softc *, int);
struct mbuf
	*pgt_ieee80211_encap(struct pgt_softc *, struct ether_header *,
	     struct mbuf *, struct ieee80211_node **);
void	 pgt_input_frames(struct pgt_softc *, struct mbuf *);
void	 pgt_wakeup_intr(struct pgt_softc *);
void	 pgt_sleep_intr(struct pgt_softc *);
void	 pgt_empty_traps(struct pgt_softc_kthread *);
void	 pgt_per_device_kthread(void *);
void	 pgt_async_reset(struct pgt_softc *);
void	 pgt_async_update(struct pgt_softc *);
void	 pgt_txdone(struct pgt_softc *, enum pgt_queue);
void	 pgt_rxdone(struct pgt_softc *, enum pgt_queue);
void	 pgt_trap_received(struct pgt_softc *, uint32_t, void *, size_t);
void	 pgt_mgmtrx_completion(struct pgt_softc *, struct pgt_mgmt_desc *);
struct mbuf
	*pgt_datarx_completion(struct pgt_softc *, enum pgt_queue);
int	 pgt_oid_get(struct pgt_softc *, enum pgt_oid, void *, size_t);
int	 pgt_oid_retrieve(struct pgt_softc *, enum pgt_oid, void *, size_t);
int	 pgt_oid_set(struct pgt_softc *, enum pgt_oid, const void *, size_t);
void	 pgt_state_dump(struct pgt_softc *);
int	 pgt_mgmt_request(struct pgt_softc *, struct pgt_mgmt_desc *);
void	 pgt_desc_transmit(struct pgt_softc *, enum pgt_queue,
	     struct pgt_desc *, uint16_t, int);
void	 pgt_maybe_trigger(struct pgt_softc *, enum pgt_queue);
struct ieee80211_node
	*pgt_ieee80211_node_alloc(struct ieee80211com *);
void	 pgt_ieee80211_newassoc(struct ieee80211com *,
	     struct ieee80211_node *, int);
void	 pgt_ieee80211_node_free(struct ieee80211com *,
	    struct ieee80211_node *);
void	 pgt_ieee80211_node_copy(struct ieee80211com *,
	     struct ieee80211_node *,
	     const struct ieee80211_node *);
int	 pgt_ieee80211_send_mgmt(struct ieee80211com *,
	     struct ieee80211_node *, int, int, int);
int	 pgt_net_attach(struct pgt_softc *);
void	 pgt_start(struct ifnet *);
int	 pgt_ioctl(struct ifnet *, u_long, caddr_t);
void	 pgt_obj_bss2scanres(struct pgt_softc *,
	     struct pgt_obj_bss *, struct wi_scan_res *, uint32_t);
void	 node_mark_active_ap(void *, struct ieee80211_node *);
void	 node_mark_active_adhoc(void *, struct ieee80211_node *);
void	 pgt_watchdog(struct ifnet *);
int	 pgt_init(struct ifnet *);
void	 pgt_update_hw_from_sw(struct pgt_softc *, int);
void	 pgt_hostap_handle_mlme(struct pgt_softc *, uint32_t,
	     struct pgt_obj_mlme *);
void	 pgt_update_sw_from_hw(struct pgt_softc *,
	     struct pgt_async_trap *, struct mbuf *);
int	 pgt_newstate(struct ieee80211com *, enum ieee80211_state, int);
int	 pgt_drain_tx_queue(struct pgt_softc *, enum pgt_queue);
int	 pgt_dma_alloc(struct pgt_softc *);
int	 pgt_dma_alloc_queue(struct pgt_softc *sc, enum pgt_queue pq);
void	 pgt_dma_free(struct pgt_softc *);
void	 pgt_dma_free_queue(struct pgt_softc *sc, enum pgt_queue pq);
void	 pgt_wakeup(struct pgt_softc *);

void
pgt_write_memory_barrier(struct pgt_softc *sc)
{
	bus_space_barrier(sc->sc_iotag, sc->sc_iohandle, 0, 0,
	    BUS_SPACE_BARRIER_WRITE);
}

u_int32_t
pgt_read_4(struct pgt_softc *sc, uint16_t offset)
{
	return (bus_space_read_4(sc->sc_iotag, sc->sc_iohandle, offset));
}

void
pgt_write_4(struct pgt_softc *sc, uint16_t offset, uint32_t value)
{
	bus_space_write_4(sc->sc_iotag, sc->sc_iohandle, offset, value);
}

/*
 * Write out 4 bytes and cause a PCI flush by reading back in on a
 * harmless register.
 */
void
pgt_write_4_flush(struct pgt_softc *sc, uint16_t offset, uint32_t value)
{
	bus_space_write_4(sc->sc_iotag, sc->sc_iohandle, offset, value);
	(void)bus_space_read_4(sc->sc_iotag, sc->sc_iohandle, PGT_REG_INT_EN);
}

/*
 * Print the state of events in the queues from an interrupt or a trigger.
 */
void
pgt_debug_events(struct pgt_softc *sc, const char *when)
{
#define	COUNT(i)							\
	letoh32(sc->sc_cb->pcb_driver_curfrag[i]) -			\
	letoh32(sc->sc_cb->pcb_device_curfrag[i])
	if (sc->sc_debug & SC_DEBUG_EVENTS)
		DPRINTF(("%s: ev%s: %u %u %u %u %u %u\n",
		    sc->sc_dev.dv_xname, when, COUNT(0), COUNT(1), COUNT(2),
		    COUNT(3), COUNT(4), COUNT(5)));
#undef COUNT
}

uint32_t
pgt_queue_frags_pending(struct pgt_softc *sc, enum pgt_queue pq)
{
	return (letoh32(sc->sc_cb->pcb_driver_curfrag[pq]) -
	    letoh32(sc->sc_cb->pcb_device_curfrag[pq]));
}

void
pgt_reinit_rx_desc_frag(struct pgt_softc *sc, struct pgt_desc *pd)
{
	pd->pd_fragp->pf_addr = htole32((uint32_t)pd->pd_dmaaddr);
	pd->pd_fragp->pf_size = htole16(PGT_FRAG_SIZE);
	pd->pd_fragp->pf_flags = 0;

	bus_dmamap_sync(sc->sc_dmat, pd->pd_dmam, 0, pd->pd_dmam->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
}

int
pgt_load_tx_desc_frag(struct pgt_softc *sc, enum pgt_queue pq,
    struct pgt_desc *pd)
{
	int error;

	error = bus_dmamap_load(sc->sc_dmat, pd->pd_dmam, pd->pd_mem,
	    PGT_FRAG_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error) {
		DPRINTF(("%s: unable to load %s tx DMA: %d\n",
		    sc->sc_dev.dv_xname,
		    pgt_queue_is_data(pq) ? "data" : "mgmt", error));
		return (error);
	}
	pd->pd_dmaaddr = pd->pd_dmam->dm_segs[0].ds_addr;
	pd->pd_fragp->pf_addr = htole32((uint32_t)pd->pd_dmaaddr);
	pd->pd_fragp->pf_size = htole16(PGT_FRAG_SIZE);
	pd->pd_fragp->pf_flags = htole16(0);

	bus_dmamap_sync(sc->sc_dmat, pd->pd_dmam, 0, pd->pd_dmam->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	return (0);
}

void
pgt_unload_tx_desc_frag(struct pgt_softc *sc, struct pgt_desc *pd)
{
        bus_dmamap_unload(sc->sc_dmat, pd->pd_dmam);
	pd->pd_dmaaddr = 0;
}

int
pgt_load_firmware(struct pgt_softc *sc)
{
	int error, reg, dirreg, fwoff, ucodeoff, fwlen;
	uint8_t *ucode;
	uint32_t *uc;
	size_t size;
	char *name;

	if (sc->sc_flags & SC_ISL3877)
		name = "pgt-isl3877";
	else
		name = "pgt-isl3890";	/* includes isl3880 */

	error = loadfirmware(name, &ucode, &size);

	if (error != 0) {
		DPRINTF(("%s: error %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, name));
		return (EIO);
	}

	if (size & 3) {
		DPRINTF(("%s: bad firmware size %u\n",
		    sc->sc_dev.dv_xname, size));
		free(ucode, M_DEVBUF, 0);
		return (EINVAL);
	}

	pgt_reboot(sc);

	fwoff = 0;
	ucodeoff = 0;
	uc = (uint32_t *)ucode;
	reg = PGT_FIRMWARE_INTERNAL_OFFSET;
	while (fwoff < size) {
		pgt_write_4_flush(sc, PGT_REG_DIR_MEM_BASE, reg);

		if ((size - fwoff) >= PGT_DIRECT_MEMORY_SIZE)
			fwlen = PGT_DIRECT_MEMORY_SIZE;
		else
			fwlen = size - fwoff;

		dirreg = PGT_DIRECT_MEMORY_OFFSET;
		while (fwlen > 4) {
			pgt_write_4(sc, dirreg, uc[ucodeoff]);
			fwoff += 4;
			dirreg += 4;
			reg += 4;
			fwlen -= 4;
			ucodeoff++;
		}
		pgt_write_4_flush(sc, dirreg, uc[ucodeoff]);
		fwoff += 4;
		dirreg += 4;
		reg += 4;
		fwlen -= 4;
		ucodeoff++;
	}
	DPRINTF(("%s: %d bytes microcode loaded from %s\n",
	    sc->sc_dev.dv_xname, fwoff, name));

	reg = pgt_read_4(sc, PGT_REG_CTRL_STAT);
	reg &= ~(PGT_CTRL_STAT_RESET | PGT_CTRL_STAT_CLOCKRUN);
	reg |= PGT_CTRL_STAT_RAMBOOT;
	pgt_write_4_flush(sc, PGT_REG_CTRL_STAT, reg);
	pgt_write_memory_barrier(sc);
	DELAY(PGT_WRITEIO_DELAY);

	reg |= PGT_CTRL_STAT_RESET;
	pgt_write_4(sc, PGT_REG_CTRL_STAT, reg);
	pgt_write_memory_barrier(sc);
	DELAY(PGT_WRITEIO_DELAY);

	reg &= ~PGT_CTRL_STAT_RESET;
	pgt_write_4(sc, PGT_REG_CTRL_STAT, reg);
	pgt_write_memory_barrier(sc);
	DELAY(PGT_WRITEIO_DELAY);

	free(ucode, M_DEVBUF, 0);
	
	return (0);
}

void
pgt_cleanup_queue(struct pgt_softc *sc, enum pgt_queue pq,
    struct pgt_frag *pqfrags)
{
	struct pgt_desc *pd;
	unsigned int i;

	sc->sc_cb->pcb_device_curfrag[pq] = 0;
	i = 0;
	/* XXX why only freeq ??? */
	TAILQ_FOREACH(pd, &sc->sc_freeq[pq], pd_link) {
		pd->pd_fragnum = i;
		pd->pd_fragp = &pqfrags[i];
		if (pgt_queue_is_rx(pq))
			pgt_reinit_rx_desc_frag(sc, pd);
		i++;
	}
	sc->sc_freeq_count[pq] = i;
	/*
	 * The ring buffer describes how many free buffers are available from
	 * the host (for receive queues) or how many are pending (for
	 * transmit queues).
	 */
	if (pgt_queue_is_rx(pq))
		sc->sc_cb->pcb_driver_curfrag[pq] = htole32(i);
	else
		sc->sc_cb->pcb_driver_curfrag[pq] = 0;
}

/*
 * Turn off interrupts, reset the device (possibly loading firmware),
 * and put everything in a known state.
 */
int
pgt_reset(struct pgt_softc *sc)
{
	int error;

	/* disable all interrupts */
	pgt_write_4_flush(sc, PGT_REG_INT_EN, 0);
	DELAY(PGT_WRITEIO_DELAY);

	/*
	 * Set up the management receive queue, assuming there are no
	 * requests in progress.
	 */
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_PREWRITE);
	pgt_cleanup_queue(sc, PGT_QUEUE_DATA_LOW_RX,
	    &sc->sc_cb->pcb_data_low_rx[0]);
	pgt_cleanup_queue(sc, PGT_QUEUE_DATA_LOW_TX,
	    &sc->sc_cb->pcb_data_low_tx[0]);
	pgt_cleanup_queue(sc, PGT_QUEUE_DATA_HIGH_RX,
	    &sc->sc_cb->pcb_data_high_rx[0]);
	pgt_cleanup_queue(sc, PGT_QUEUE_DATA_HIGH_TX,
	    &sc->sc_cb->pcb_data_high_tx[0]);
	pgt_cleanup_queue(sc, PGT_QUEUE_MGMT_RX,
	    &sc->sc_cb->pcb_mgmt_rx[0]);
	pgt_cleanup_queue(sc, PGT_QUEUE_MGMT_TX,
	    &sc->sc_cb->pcb_mgmt_tx[0]);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_PREREAD);

	/* load firmware */
	if (sc->sc_flags & SC_NEEDS_FIRMWARE) {
		error = pgt_load_firmware(sc);
		if (error) {
			printf("%s: firmware load failed\n",
			    sc->sc_dev.dv_xname);
			return (error);
		}
		sc->sc_flags &= ~SC_NEEDS_FIRMWARE;
		DPRINTF(("%s: firmware loaded\n", sc->sc_dev.dv_xname));
	}

	/* upload the control block's DMA address */
	pgt_write_4_flush(sc, PGT_REG_CTRL_BLK_BASE,
	    htole32((uint32_t)sc->sc_cbdmam->dm_segs[0].ds_addr));
	DELAY(PGT_WRITEIO_DELAY);

	/* send a reset event */
	pgt_write_4_flush(sc, PGT_REG_DEV_INT, PGT_DEV_INT_RESET);
	DELAY(PGT_WRITEIO_DELAY);

	/* await only the initialization interrupt */
	pgt_write_4_flush(sc, PGT_REG_INT_EN, PGT_INT_STAT_INIT);	
	DELAY(PGT_WRITEIO_DELAY);

	return (0);
}

/*
 * If we're trying to reset and the device has seemingly not been detached,
 * we'll spend a minute seeing if we can't do the reset.
 */
void
pgt_stop(struct pgt_softc *sc, unsigned int flag)
{
	struct ieee80211com *ic;
	unsigned int wokeup;
	int tryagain = 0;

	ic = &sc->sc_ic;

	ic->ic_if.if_flags &= ~IFF_RUNNING;
	sc->sc_flags |= SC_UNINITIALIZED;
	sc->sc_flags |= flag;

	pgt_drain_tx_queue(sc, PGT_QUEUE_DATA_LOW_TX);
	pgt_drain_tx_queue(sc, PGT_QUEUE_DATA_HIGH_TX);
	pgt_drain_tx_queue(sc, PGT_QUEUE_MGMT_TX);

trying_again:
	/* disable all interrupts */
	pgt_write_4_flush(sc, PGT_REG_INT_EN, 0);
	DELAY(PGT_WRITEIO_DELAY);

	/* reboot card */
	pgt_reboot(sc);

	do {
		wokeup = 0;
		/*
		 * We don't expect to be woken up, just to drop the lock
		 * and time out.  Only tx queues can have anything valid
		 * on them outside of an interrupt.
		 */
		while (!TAILQ_EMPTY(&sc->sc_mgmtinprog)) {
			struct pgt_mgmt_desc *pmd;

			pmd = TAILQ_FIRST(&sc->sc_mgmtinprog);
			TAILQ_REMOVE(&sc->sc_mgmtinprog, pmd, pmd_link);
			pmd->pmd_error = ENETRESET;
			wakeup_one(pmd);
			if (sc->sc_debug & SC_DEBUG_MGMT)
				DPRINTF(("%s: queue: mgmt %p <- %#x "
				    "(drained)\n", sc->sc_dev.dv_xname,
				    pmd, pmd->pmd_oid));
			wokeup++;
		}
		if (wokeup > 0) {
			if (flag == SC_NEEDS_RESET && sc->sc_flags & SC_DYING) {
				sc->sc_flags &= ~flag;
				return;
			}
		}
	} while (wokeup > 0);

	if (flag == SC_NEEDS_RESET) {
		int error;

		DPRINTF(("%s: resetting\n", sc->sc_dev.dv_xname));
		sc->sc_flags &= ~SC_POWERSAVE;
		sc->sc_flags |= SC_NEEDS_FIRMWARE;
		error = pgt_reset(sc);
		if (error == 0) {
			tsleep_nsec(&sc->sc_flags, 0, "pgtres", SEC_TO_NSEC(1));
			if (sc->sc_flags & SC_UNINITIALIZED) {
				printf("%s: not responding\n",
				    sc->sc_dev.dv_xname);
				/* Thud.  It was probably removed. */
				if (tryagain)
					panic("pgt went for lunch"); /* XXX */
				tryagain = 1;
			} else {
				/* await all interrupts */
				pgt_write_4_flush(sc, PGT_REG_INT_EN,
				    PGT_INT_STAT_SOURCES);	
				DELAY(PGT_WRITEIO_DELAY);
				ic->ic_if.if_flags |= IFF_RUNNING;
			}
		}

		if (tryagain)
			goto trying_again;

		sc->sc_flags &= ~flag;
		if (ic->ic_if.if_flags & IFF_RUNNING)
			pgt_update_hw_from_sw(sc,
			    ic->ic_state != IEEE80211_S_INIT);
	}

	ic->ic_if.if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ic->ic_if.if_snd);
	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);
}

void
pgt_attach(struct device *self)
{
	struct pgt_softc *sc = (struct pgt_softc *)self;
	int error;

	/* debug flags */
	//sc->sc_debug |= SC_DEBUG_QUEUES;	/* super verbose */
	//sc->sc_debug |= SC_DEBUG_MGMT;
	sc->sc_debug |= SC_DEBUG_UNEXPECTED;
	//sc->sc_debug |= SC_DEBUG_TRIGGER;	/* verbose */
	//sc->sc_debug |= SC_DEBUG_EVENTS;	/* super verbose */
	//sc->sc_debug |= SC_DEBUG_POWER;
	sc->sc_debug |= SC_DEBUG_TRAP;
	sc->sc_debug |= SC_DEBUG_LINK;
	//sc->sc_debug |= SC_DEBUG_RXANNEX;
	//sc->sc_debug |= SC_DEBUG_RXFRAG;
	//sc->sc_debug |= SC_DEBUG_RXETHER;

	/* enable card if possible */
	if (sc->sc_enable != NULL)
		(*sc->sc_enable)(sc);

	error = pgt_dma_alloc(sc);
	if (error)
		return;

	sc->sc_ic.ic_if.if_softc = sc;
	TAILQ_INIT(&sc->sc_mgmtinprog);
	TAILQ_INIT(&sc->sc_kthread.sck_traps);
	sc->sc_flags |= SC_NEEDS_FIRMWARE | SC_UNINITIALIZED;
	sc->sc_80211_ioc_auth = IEEE80211_AUTH_OPEN;

	error = pgt_reset(sc);
	if (error)
		return;

	tsleep_nsec(&sc->sc_flags, 0, "pgtres", SEC_TO_NSEC(1));
	if (sc->sc_flags & SC_UNINITIALIZED) {
		printf("%s: not responding\n", sc->sc_dev.dv_xname);
		sc->sc_flags |= SC_NEEDS_FIRMWARE;
		return;
	} else {
		/* await all interrupts */
		pgt_write_4_flush(sc, PGT_REG_INT_EN, PGT_INT_STAT_SOURCES);
		DELAY(PGT_WRITEIO_DELAY);
	}

	error = pgt_net_attach(sc);
	if (error)
		return;

	if (kthread_create(pgt_per_device_kthread, sc, NULL,
	    sc->sc_dev.dv_xname) != 0)
		return;

	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);
}

int
pgt_detach(struct pgt_softc *sc)
{
	if (sc->sc_flags & SC_NEEDS_FIRMWARE || sc->sc_flags & SC_UNINITIALIZED)
		/* device was not initialized correctly, so leave early */
		goto out;

	/* stop card */
	pgt_stop(sc, SC_DYING);
	pgt_reboot(sc);

	ieee80211_ifdetach(&sc->sc_ic.ic_if);
	if_detach(&sc->sc_ic.ic_if);

out:
	/* disable card if possible */
	if (sc->sc_disable != NULL)
		(*sc->sc_disable)(sc);

	pgt_dma_free(sc);

	return (0);
}

void
pgt_reboot(struct pgt_softc *sc)
{
	uint32_t reg;

	reg = pgt_read_4(sc, PGT_REG_CTRL_STAT);
	reg &= ~(PGT_CTRL_STAT_RESET | PGT_CTRL_STAT_RAMBOOT);
	pgt_write_4(sc, PGT_REG_CTRL_STAT, reg);
	pgt_write_memory_barrier(sc);
	DELAY(PGT_WRITEIO_DELAY);

	reg |= PGT_CTRL_STAT_RESET;
	pgt_write_4(sc, PGT_REG_CTRL_STAT, reg);
	pgt_write_memory_barrier(sc);
	DELAY(PGT_WRITEIO_DELAY);

	reg &= ~PGT_CTRL_STAT_RESET;
	pgt_write_4(sc, PGT_REG_CTRL_STAT, reg);
	pgt_write_memory_barrier(sc);
	DELAY(PGT_RESET_DELAY);
}

void
pgt_init_intr(struct pgt_softc *sc)
{
	if ((sc->sc_flags & SC_UNINITIALIZED) == 0) {
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			DPRINTF(("%s: spurious initialization\n",
			    sc->sc_dev.dv_xname));
	} else {
		sc->sc_flags &= ~SC_UNINITIALIZED;
		wakeup(&sc->sc_flags);
	}
}

/*
 * If called with a NULL last_nextpkt, only the mgmt queue will be checked
 * for new packets.
 */
void
pgt_update_intr(struct pgt_softc *sc, int hack)
{
	/* priority order */
	enum pgt_queue pqs[PGT_QUEUE_COUNT] = {
	    PGT_QUEUE_MGMT_TX, PGT_QUEUE_MGMT_RX, 
	    PGT_QUEUE_DATA_HIGH_TX, PGT_QUEUE_DATA_HIGH_RX, 
	    PGT_QUEUE_DATA_LOW_TX, PGT_QUEUE_DATA_LOW_RX
	};
	struct mbuf *m;
	uint32_t npend;
	unsigned int dirtycount;
	int i;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_PREWRITE);
	pgt_debug_events(sc, "intr");
	/*
	 * Check for completion of tx in their dirty queues.
	 * Check completion of rx into their dirty queues.
	 */
	for (i = 0; i < PGT_QUEUE_COUNT; i++) {
		size_t qdirty, qfree;

		qdirty = sc->sc_dirtyq_count[pqs[i]];
		qfree = sc->sc_freeq_count[pqs[i]];
		/*
		 * We want the wrap-around here.
		 */
		if (pgt_queue_is_rx(pqs[i])) {
			int data;

			data = pgt_queue_is_data(pqs[i]);
#ifdef PGT_BUGGY_INTERRUPT_RECOVERY
			if (hack && data)
				continue;
#endif
			npend = pgt_queue_frags_pending(sc, pqs[i]);
			/*
			 * Receive queues clean up below, so qdirty must
			 * always be 0.
			 */
			if (npend > qfree) {
				if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
					DPRINTF(("%s: rx queue [%u] "
					    "overflowed by %u\n",
					    sc->sc_dev.dv_xname, pqs[i],
					    npend - qfree));
				sc->sc_flags |= SC_INTR_RESET;
				break;
			}
			while (qfree-- > npend)
				pgt_rxdone(sc, pqs[i]);
		} else {
			npend = pgt_queue_frags_pending(sc, pqs[i]);
			if (npend > qdirty) {
				if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
					DPRINTF(("%s: tx queue [%u] "
					    "underflowed by %u\n",
					    sc->sc_dev.dv_xname, pqs[i],
					    npend - qdirty));
				sc->sc_flags |= SC_INTR_RESET;
				break;
			}
			/*
			 * If the free queue was empty, or the data transmit
			 * queue just became empty, wake up any waiters.
			 */
			if (qdirty > npend) {
				if (pgt_queue_is_data(pqs[i])) {
					sc->sc_ic.ic_if.if_timer = 0;
					ifq_clr_oactive(
					    &sc->sc_ic.ic_if.if_snd);
				}
				while (qdirty-- > npend)
					pgt_txdone(sc, pqs[i]);
			}
		}
	}

	/*
	 * This is the deferred completion for received management frames
	 * and where we queue network frames for stack input. 
	 */
	dirtycount = sc->sc_dirtyq_count[PGT_QUEUE_MGMT_RX];
	while (!TAILQ_EMPTY(&sc->sc_dirtyq[PGT_QUEUE_MGMT_RX])) {
		struct pgt_mgmt_desc *pmd;

		pmd = TAILQ_FIRST(&sc->sc_mgmtinprog);
		/*
		 * If there is no mgmt request in progress or the operation
		 * returned is explicitly a trap, this pmd will essentially
		 * be ignored.
		 */
		pgt_mgmtrx_completion(sc, pmd);
	}
	sc->sc_cb->pcb_driver_curfrag[PGT_QUEUE_MGMT_RX] =
	    htole32(dirtycount +
		letoh32(sc->sc_cb->pcb_driver_curfrag[PGT_QUEUE_MGMT_RX]));

	dirtycount = sc->sc_dirtyq_count[PGT_QUEUE_DATA_HIGH_RX];
	while (!TAILQ_EMPTY(&sc->sc_dirtyq[PGT_QUEUE_DATA_HIGH_RX])) {
		if ((m = pgt_datarx_completion(sc, PGT_QUEUE_DATA_HIGH_RX)))
			pgt_input_frames(sc, m);
	}
	sc->sc_cb->pcb_driver_curfrag[PGT_QUEUE_DATA_HIGH_RX] =
	    htole32(dirtycount +
		letoh32(sc->sc_cb->pcb_driver_curfrag[PGT_QUEUE_DATA_HIGH_RX]));

	dirtycount = sc->sc_dirtyq_count[PGT_QUEUE_DATA_LOW_RX];
	while (!TAILQ_EMPTY(&sc->sc_dirtyq[PGT_QUEUE_DATA_LOW_RX])) {
		if ((m = pgt_datarx_completion(sc, PGT_QUEUE_DATA_LOW_RX)))
			pgt_input_frames(sc, m);
	}
	sc->sc_cb->pcb_driver_curfrag[PGT_QUEUE_DATA_LOW_RX] =
	    htole32(dirtycount +
		letoh32(sc->sc_cb->pcb_driver_curfrag[PGT_QUEUE_DATA_LOW_RX]));

	/*
	 * Write out what we've finished with.
	 */
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_PREREAD);
}

struct mbuf *
pgt_ieee80211_encap(struct pgt_softc *sc, struct ether_header *eh,
    struct mbuf *m, struct ieee80211_node **ni)
{
	struct ieee80211com *ic;
	struct ieee80211_frame *frame;
	struct llc *snap;

	ic = &sc->sc_ic;
	if (ni != NULL && ic->ic_opmode == IEEE80211_M_MONITOR) {
		*ni = ieee80211_ref_node(ic->ic_bss);
		(*ni)->ni_inact = 0;
		return (m);
	}

	M_PREPEND(m, sizeof(*frame) + sizeof(*snap), M_DONTWAIT);
	if (m == NULL)
		return (m);
	if (m->m_len < sizeof(*frame) + sizeof(*snap)) {
		m = m_pullup(m, sizeof(*frame) + sizeof(*snap));
		if (m == NULL)
			return (m);
	}
	frame = mtod(m, struct ieee80211_frame *);
	snap = (struct llc *)&frame[1];
	if (ni != NULL) {
		if (ic->ic_opmode == IEEE80211_M_STA) {
			*ni = ieee80211_ref_node(ic->ic_bss);
		}
#ifndef IEEE80211_STA_ONLY
		else {
			*ni = ieee80211_find_node(ic, eh->ether_shost);
			/*
			 * Make up associations for ad-hoc mode.  To support
			 * ad-hoc WPA, we'll need to maintain a bounded
			 * pool of ad-hoc stations.
			 */
			if (*ni == NULL &&
			    ic->ic_opmode != IEEE80211_M_HOSTAP) {
				*ni = ieee80211_dup_bss(ic, eh->ether_shost);
				if (*ni != NULL) {
					(*ni)->ni_associd = 1;
					ic->ic_newassoc(ic, *ni, 1);
				}
			}
			if (*ni == NULL) {
				m_freem(m);
				return (NULL);
			}
		}
#endif
		(*ni)->ni_inact = 0;
	}
	snap->llc_dsap = snap->llc_ssap = LLC_SNAP_LSAP;
	snap->llc_control = LLC_UI;
	snap->llc_snap.org_code[0] = 0;
	snap->llc_snap.org_code[1] = 0;
	snap->llc_snap.org_code[2] = 0;
	snap->llc_snap.ether_type = eh->ether_type;
	frame->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA;
	/* Doesn't look like much of the 802.11 header is available. */
	*(uint16_t *)frame->i_dur = *(uint16_t *)frame->i_seq = 0;
	/*
	 * Translate the addresses; WDS is not handled.
	 */
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		frame->i_fc[1] = IEEE80211_FC1_DIR_FROMDS;
		IEEE80211_ADDR_COPY(frame->i_addr1, eh->ether_dhost);
		IEEE80211_ADDR_COPY(frame->i_addr2, ic->ic_bss->ni_bssid);
		IEEE80211_ADDR_COPY(frame->i_addr3, eh->ether_shost);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
		frame->i_fc[1] = IEEE80211_FC1_DIR_NODS;
		IEEE80211_ADDR_COPY(frame->i_addr1, eh->ether_dhost);
		IEEE80211_ADDR_COPY(frame->i_addr2, eh->ether_shost);
		IEEE80211_ADDR_COPY(frame->i_addr3, ic->ic_bss->ni_bssid);
		break;
	case IEEE80211_M_HOSTAP:
		/* HostAP forwarding defaults to being done on firmware. */
		frame->i_fc[1] = IEEE80211_FC1_DIR_TODS;
		IEEE80211_ADDR_COPY(frame->i_addr1, ic->ic_bss->ni_bssid);
		IEEE80211_ADDR_COPY(frame->i_addr2, eh->ether_shost);
		IEEE80211_ADDR_COPY(frame->i_addr3, eh->ether_dhost);
		break;
#endif
	default:
		break;
	}
	return (m);
}

void
pgt_input_frames(struct pgt_softc *sc, struct mbuf *m)
{
	struct ether_header eh;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ifnet *ifp;
	struct ieee80211_channel *chan;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct ieee80211com *ic;
	struct pgt_rx_annex *pra;
	struct pgt_rx_header *pha;
	struct mbuf *next;
	unsigned int n;
	uint32_t rstamp;
	uint8_t rssi;

	ic = &sc->sc_ic;
	ifp = &ic->ic_if;
	for (next = m; m != NULL; m = next) {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;

		if (ic->ic_opmode == IEEE80211_M_MONITOR) {
			if (m->m_len < sizeof(*pha)) {
				m = m_pullup(m, sizeof(*pha));
				if (m == NULL) {
					if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
						DPRINTF(("%s: m_pullup "
						    "failure\n",
						    sc->sc_dev.dv_xname));
					ifp->if_ierrors++;
					continue;
				}
			}
			pha = mtod(m, struct pgt_rx_header *);
			pra = NULL;
			goto input;
		}

		if (m->m_len < sizeof(*pra)) {
			m = m_pullup(m, sizeof(*pra));
			if (m == NULL) {
				if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
					DPRINTF(("%s: m_pullup failure\n",
					    sc->sc_dev.dv_xname));
				ifp->if_ierrors++;
				continue;
			}
		}
		pra = mtod(m, struct pgt_rx_annex *);
		pha = &pra->pra_header;
		if (sc->sc_debug & SC_DEBUG_RXANNEX)
			DPRINTF(("%s: rx annex: ? %04x "
			    "len %u clock %u flags %02x ? %02x rate %u ? %02x "
			    "freq %u ? %04x rssi %u pad %02x%02x%02x\n",
			    sc->sc_dev.dv_xname,
			    letoh16(pha->pra_unknown0),
			    letoh16(pha->pra_length),
			    letoh32(pha->pra_clock), pha->pra_flags,
			    pha->pra_unknown1, pha->pra_rate,
			    pha->pra_unknown2, letoh32(pha->pra_frequency),
			    pha->pra_unknown3, pha->pra_rssi,
			    pha->pra_pad[0], pha->pra_pad[1], pha->pra_pad[2]));
		if (sc->sc_debug & SC_DEBUG_RXETHER)
			DPRINTF(("%s: rx ether: %s < %s 0x%04x\n",
			    sc->sc_dev.dv_xname,
			    ether_sprintf(pra->pra_ether_dhost),
			    ether_sprintf(pra->pra_ether_shost),
			    ntohs(pra->pra_ether_type)));

		memcpy(eh.ether_dhost, pra->pra_ether_dhost, ETHER_ADDR_LEN);
		memcpy(eh.ether_shost, pra->pra_ether_shost, ETHER_ADDR_LEN);
		eh.ether_type = pra->pra_ether_type;

input:
		/*
		 * This flag is set if e.g. packet could not be decrypted.
		 */
		if (pha->pra_flags & PRA_FLAG_BAD) {
			ifp->if_ierrors++;
			m_freem(m);
			continue;
		}

		/*
		 * After getting what we want, chop off the annex, then
		 * turn into something that looks like it really was
		 * 802.11.
		 */
		rssi = pha->pra_rssi;
		rstamp = letoh32(pha->pra_clock);
		n = ieee80211_mhz2ieee(letoh32(pha->pra_frequency), 0);
		if (n <= IEEE80211_CHAN_MAX)
			chan = &ic->ic_channels[n];
		else
			chan = ic->ic_bss->ni_chan;
		/* Send to 802.3 listeners. */
		if (pra) {
			m_adj(m, sizeof(*pra));
		} else
			m_adj(m, sizeof(*pha));

		m = pgt_ieee80211_encap(sc, &eh, m, &ni);
		if (m != NULL) {
#if NBPFILTER > 0
			if (sc->sc_drvbpf != NULL) {
				struct mbuf mb;
				struct pgt_rx_radiotap_hdr *tap = &sc->sc_rxtap;

				tap->wr_flags = 0;
				tap->wr_chan_freq = htole16(chan->ic_freq);
				tap->wr_chan_flags = htole16(chan->ic_flags);
				tap->wr_rssi = rssi;
				tap->wr_max_rssi = ic->ic_max_rssi;

				mb.m_data = (caddr_t)tap;
				mb.m_len = sc->sc_rxtap_len;
				mb.m_next = m;
				mb.m_nextpkt = NULL;
				mb.m_type = 0;
				mb.m_flags = 0;
				bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
			}
#endif
			memset(&rxi, 0, sizeof(rxi));
			ni->ni_rssi = rxi.rxi_rssi = rssi;
			ni->ni_rstamp = rxi.rxi_tstamp = rstamp;
			ieee80211_inputm(ifp, m, ni, &rxi, &ml);
			/*
			 * The frame may have caused the node to be marked for
			 * reclamation (e.g. in response to a DEAUTH message)
			 * so use free_node here instead of unref_node.
			 */
			if (ni == ic->ic_bss)
				ieee80211_unref_node(&ni);
			else
				ieee80211_release_node(&sc->sc_ic, ni);
		} else {
			ifp->if_ierrors++;
		}
	}
	if_input(ifp, &ml);
}

void
pgt_wakeup_intr(struct pgt_softc *sc)
{
	int shouldupdate;
	int i;

	shouldupdate = 0;
	/* Check for any queues being empty before updating. */
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);
	for (i = 0; !shouldupdate && i < PGT_QUEUE_COUNT; i++) {
		if (pgt_queue_is_tx(i))
			shouldupdate = pgt_queue_frags_pending(sc, i);
		else
			shouldupdate = pgt_queue_frags_pending(sc, i) <
			    sc->sc_freeq_count[i];
	}
	if (!TAILQ_EMPTY(&sc->sc_mgmtinprog))
		shouldupdate = 1;
	if (sc->sc_debug & SC_DEBUG_POWER)
		DPRINTF(("%s: wakeup interrupt (update = %d)\n",
		    sc->sc_dev.dv_xname, shouldupdate));
	sc->sc_flags &= ~SC_POWERSAVE;
	if (shouldupdate) {
		pgt_write_4_flush(sc, PGT_REG_DEV_INT, PGT_DEV_INT_UPDATE);
		DELAY(PGT_WRITEIO_DELAY);
	}
}

void
pgt_sleep_intr(struct pgt_softc *sc)
{
	int allowed;
	int i;

	allowed = 1;
	/* Check for any queues not being empty before allowing. */
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);
	for (i = 0; allowed && i < PGT_QUEUE_COUNT; i++) {
		if (pgt_queue_is_tx(i))
			allowed = pgt_queue_frags_pending(sc, i) == 0;
		else
			allowed = pgt_queue_frags_pending(sc, i) >=
			    sc->sc_freeq_count[i];
	}
	if (!TAILQ_EMPTY(&sc->sc_mgmtinprog))
		allowed = 0;
	if (sc->sc_debug & SC_DEBUG_POWER)
		DPRINTF(("%s: sleep interrupt (allowed = %d)\n",
		    sc->sc_dev.dv_xname, allowed));
	if (allowed && sc->sc_ic.ic_flags & IEEE80211_F_PMGTON) {
		sc->sc_flags |= SC_POWERSAVE;
		pgt_write_4_flush(sc, PGT_REG_DEV_INT, PGT_DEV_INT_SLEEP);
		DELAY(PGT_WRITEIO_DELAY);
	}
}

void
pgt_empty_traps(struct pgt_softc_kthread *sck)
{
	struct pgt_async_trap *pa;
	struct mbuf *m;

	while (!TAILQ_EMPTY(&sck->sck_traps)) {
		pa = TAILQ_FIRST(&sck->sck_traps);
		TAILQ_REMOVE(&sck->sck_traps, pa, pa_link);
		m = pa->pa_mbuf;
		m_freem(m);
	}
}

void
pgt_per_device_kthread(void *argp)
{
	struct pgt_softc *sc;
	struct pgt_softc_kthread *sck;
	struct pgt_async_trap *pa;
	struct mbuf *m;
	int s;

	sc = argp;
	sck = &sc->sc_kthread;
	while (!sck->sck_exit) {
		if (!sck->sck_update && !sck->sck_reset &&
		    TAILQ_EMPTY(&sck->sck_traps))
			tsleep_nsec(&sc->sc_kthread, 0, "pgtkth", INFSLP);
		if (sck->sck_reset) {
			DPRINTF(("%s: [thread] async reset\n",
			    sc->sc_dev.dv_xname));
			sck->sck_reset = 0;
			sck->sck_update = 0;
			pgt_empty_traps(sck);
			s = splnet();
			pgt_stop(sc, SC_NEEDS_RESET);
			splx(s);
		} else if (!TAILQ_EMPTY(&sck->sck_traps)) {
			DPRINTF(("%s: [thread] got a trap\n",
			    sc->sc_dev.dv_xname));
			pa = TAILQ_FIRST(&sck->sck_traps);
			TAILQ_REMOVE(&sck->sck_traps, pa, pa_link);
			m = pa->pa_mbuf;
			m_adj(m, sizeof(*pa));
			pgt_update_sw_from_hw(sc, pa, m);
			m_freem(m);
		} else if (sck->sck_update) {
			sck->sck_update = 0;
			pgt_update_sw_from_hw(sc, NULL, NULL);
		}
	}
	pgt_empty_traps(sck);
	kthread_exit(0);
}

void
pgt_async_reset(struct pgt_softc *sc)
{
	if (sc->sc_flags & (SC_DYING | SC_NEEDS_RESET))
		return;
	sc->sc_kthread.sck_reset = 1;
	wakeup(&sc->sc_kthread);
}

void
pgt_async_update(struct pgt_softc *sc)
{
	if (sc->sc_flags & SC_DYING)
		return;
	sc->sc_kthread.sck_update = 1;
	wakeup(&sc->sc_kthread);
}

int
pgt_intr(void *arg)
{
	struct pgt_softc *sc;
	struct ifnet *ifp;
	u_int32_t reg;

	sc = arg;
	ifp = &sc->sc_ic.ic_if;

	/*
	 * Here the Linux driver ands in the value of the INT_EN register,
	 * and masks off everything but the documented interrupt bits.  Why?
	 *
	 * Unknown bit 0x4000 is set upon initialization, 0x8000000 some
	 * other times.
	 */
	if (sc->sc_ic.ic_flags & IEEE80211_F_PMGTON &&
	    sc->sc_flags & SC_POWERSAVE) {
		/*
		 * Don't try handling the interrupt in sleep mode.
		 */
		reg = pgt_read_4(sc, PGT_REG_CTRL_STAT);
		if (reg & PGT_CTRL_STAT_SLEEPMODE)
			return (0);
	}
	reg = pgt_read_4(sc, PGT_REG_INT_STAT);
	if (reg == 0)
		return (0); /* This interrupt is not from us */

	pgt_write_4_flush(sc, PGT_REG_INT_ACK, reg);
	if (reg & PGT_INT_STAT_INIT)
		pgt_init_intr(sc);
	if (reg & PGT_INT_STAT_UPDATE) {
		pgt_update_intr(sc, 0);
		/*
		 * If we got an update, it's not really asleep.
		 */
		sc->sc_flags &= ~SC_POWERSAVE;
		/*
		 * Pretend I have any idea what the documentation
		 * would say, and just give it a shot sending an
		 * "update" after acknowledging the interrupt
		 * bits and writing out the new control block.
		 */
		pgt_write_4_flush(sc, PGT_REG_DEV_INT, PGT_DEV_INT_UPDATE);
		DELAY(PGT_WRITEIO_DELAY);
	}
	if (reg & PGT_INT_STAT_SLEEP && !(reg & PGT_INT_STAT_WAKEUP))
		pgt_sleep_intr(sc);
	if (reg & PGT_INT_STAT_WAKEUP)
		pgt_wakeup_intr(sc);

	if (sc->sc_flags & SC_INTR_RESET) {
		sc->sc_flags &= ~SC_INTR_RESET;
		pgt_async_reset(sc);
	}

	if (reg & ~PGT_INT_STAT_SOURCES && sc->sc_debug & SC_DEBUG_UNEXPECTED) {
		DPRINTF(("%s: unknown interrupt bits %#x (stat %#x)\n",
		    sc->sc_dev.dv_xname,
		    reg & ~PGT_INT_STAT_SOURCES,
		    pgt_read_4(sc, PGT_REG_CTRL_STAT)));
	}

	if (!ifq_empty(&ifp->if_snd))
		pgt_start(ifp);

	return (1);
}

void
pgt_txdone(struct pgt_softc *sc, enum pgt_queue pq)
{
	struct pgt_desc *pd;

	pd = TAILQ_FIRST(&sc->sc_dirtyq[pq]);
	TAILQ_REMOVE(&sc->sc_dirtyq[pq], pd, pd_link);
	sc->sc_dirtyq_count[pq]--;
	TAILQ_INSERT_TAIL(&sc->sc_freeq[pq], pd, pd_link);
	sc->sc_freeq_count[pq]++;
	bus_dmamap_sync(sc->sc_dmat, pd->pd_dmam, 0,
	    pd->pd_dmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);
	/* Management frames want completion information. */
	if (sc->sc_debug & SC_DEBUG_QUEUES) {
		DPRINTF(("%s: queue: tx %u <- [%u]\n",
		    sc->sc_dev.dv_xname, pd->pd_fragnum, pq));
		if (sc->sc_debug & SC_DEBUG_MGMT && pgt_queue_is_mgmt(pq)) {
			struct pgt_mgmt_frame *pmf;

			pmf = (struct pgt_mgmt_frame *)pd->pd_mem;
			DPRINTF(("%s: queue: txmgmt %p <- "
			    "(ver %u, op %u, flags %#x)\n",
			    sc->sc_dev.dv_xname,
			    pd, pmf->pmf_version, pmf->pmf_operation,
			    pmf->pmf_flags));
		}
	}
	pgt_unload_tx_desc_frag(sc, pd);
}

void
pgt_rxdone(struct pgt_softc *sc, enum pgt_queue pq)
{
	struct pgt_desc *pd;

	pd = TAILQ_FIRST(&sc->sc_freeq[pq]);
	TAILQ_REMOVE(&sc->sc_freeq[pq], pd, pd_link);
	sc->sc_freeq_count[pq]--;
	TAILQ_INSERT_TAIL(&sc->sc_dirtyq[pq], pd, pd_link);
	sc->sc_dirtyq_count[pq]++;
	bus_dmamap_sync(sc->sc_dmat, pd->pd_dmam, 0,
	    pd->pd_dmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);
	if (sc->sc_debug & SC_DEBUG_QUEUES)
		DPRINTF(("%s: queue: rx %u <- [%u]\n",
		    sc->sc_dev.dv_xname, pd->pd_fragnum, pq));
	if (sc->sc_debug & SC_DEBUG_UNEXPECTED &&
	    pd->pd_fragp->pf_flags & ~htole16(PF_FLAG_MF))
		DPRINTF(("%s: unknown flags on rx [%u]: %#x\n",
		    sc->sc_dev.dv_xname, pq, letoh16(pd->pd_fragp->pf_flags)));
}

/*
 * Traps are generally used for the firmware to report changes in state
 * back to the host.  Mostly this processes changes in link state, but
 * it needs to also be used to initiate WPA and other authentication
 * schemes in terms of client (station) or server (access point).
 */
void
pgt_trap_received(struct pgt_softc *sc, uint32_t oid, void *trapdata,
    size_t size)
{
	struct pgt_async_trap *pa;
	struct mbuf *m;
	char *p;
	size_t total;

	if (sc->sc_flags & SC_DYING)
		return;

	total = sizeof(oid) + size + sizeof(struct pgt_async_trap);
	if (total > MLEN) {
		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL)
			return;
		MCLGET(m, M_DONTWAIT);
		if (!(m->m_flags & M_EXT)) {
			m_freem(m);
			m = NULL;
		}
	} else
		m = m_get(M_DONTWAIT, MT_DATA);

	if (m == NULL)
		return;
	else
		m->m_len = total;

	pa = mtod(m, struct pgt_async_trap *);
	p = mtod(m, char *) + sizeof(*pa);
	*(uint32_t *)p = oid;
	p += sizeof(uint32_t);
	memcpy(p, trapdata, size);
	pa->pa_mbuf = m;

	TAILQ_INSERT_TAIL(&sc->sc_kthread.sck_traps, pa, pa_link);
	wakeup(&sc->sc_kthread);
}

/*
 * Process a completed management response (all requests should be
 * responded to, quickly) or an event (trap).
 */
void
pgt_mgmtrx_completion(struct pgt_softc *sc, struct pgt_mgmt_desc *pmd)
{
	struct pgt_desc *pd;
	struct pgt_mgmt_frame *pmf;
	uint32_t oid, size;

	pd = TAILQ_FIRST(&sc->sc_dirtyq[PGT_QUEUE_MGMT_RX]);
	TAILQ_REMOVE(&sc->sc_dirtyq[PGT_QUEUE_MGMT_RX], pd, pd_link);
	sc->sc_dirtyq_count[PGT_QUEUE_MGMT_RX]--;
	TAILQ_INSERT_TAIL(&sc->sc_freeq[PGT_QUEUE_MGMT_RX],
	    pd, pd_link);
	sc->sc_freeq_count[PGT_QUEUE_MGMT_RX]++;
	if (letoh16(pd->pd_fragp->pf_size) < sizeof(*pmf)) {
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			DPRINTF(("%s: mgmt desc too small: %u\n",
			    sc->sc_dev.dv_xname,
			    letoh16(pd->pd_fragp->pf_size)));
		goto out_nopmd;
	}
	pmf = (struct pgt_mgmt_frame *)pd->pd_mem;
	if (pmf->pmf_version != PMF_VER) {
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			DPRINTF(("%s: unknown mgmt version %u\n",
			    sc->sc_dev.dv_xname, pmf->pmf_version));
		goto out_nopmd;
	}
	if (pmf->pmf_device != PMF_DEV) {
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			DPRINTF(("%s: unknown mgmt dev %u\n",
			    sc->sc_dev.dv_xname, pmf->pmf_device));
		goto out;
	}
	if (pmf->pmf_flags & ~PMF_FLAG_VALID) {
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			DPRINTF(("%s: unknown mgmt flags %x\n",
			    sc->sc_dev.dv_xname,
			    pmf->pmf_flags & ~PMF_FLAG_VALID));
		goto out;
	}
	if (pmf->pmf_flags & PMF_FLAG_LE) {
		oid = letoh32(pmf->pmf_oid);
		size = letoh32(pmf->pmf_size);
	} else {
		oid = betoh32(pmf->pmf_oid);
		size = betoh32(pmf->pmf_size);
	}
	if (pmf->pmf_operation == PMF_OP_TRAP) {
		pmd = NULL; /* ignored */
		DPRINTF(("%s: mgmt trap received (op %u, oid %#x, len %u)\n",
		    sc->sc_dev.dv_xname,
		    pmf->pmf_operation, oid, size));
		pgt_trap_received(sc, oid, (char *)pmf + sizeof(*pmf),
		    min(size, PGT_FRAG_SIZE - sizeof(*pmf)));
		goto out_nopmd;
	}
	if (pmd == NULL) {
		if (sc->sc_debug & (SC_DEBUG_UNEXPECTED | SC_DEBUG_MGMT))
			DPRINTF(("%s: spurious mgmt received "
			    "(op %u, oid %#x, len %u)\n", sc->sc_dev.dv_xname,
			    pmf->pmf_operation, oid, size));
		goto out_nopmd;
	}
	switch (pmf->pmf_operation) {
	case PMF_OP_RESPONSE:
		pmd->pmd_error = 0;
		break;
	case PMF_OP_ERROR:
		pmd->pmd_error = EPERM;
		goto out;
	default:
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			DPRINTF(("%s: unknown mgmt op %u\n",
			    sc->sc_dev.dv_xname, pmf->pmf_operation));
		pmd->pmd_error = EIO;
		goto out;
	}
	if (oid != pmd->pmd_oid) {
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			DPRINTF(("%s: mgmt oid changed from %#x -> %#x\n",
			    sc->sc_dev.dv_xname, pmd->pmd_oid, oid));
		pmd->pmd_oid = oid;
	}
	if (pmd->pmd_recvbuf != NULL) {
		if (size > PGT_FRAG_SIZE) {
			if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
				DPRINTF(("%s: mgmt oid %#x has bad size %u\n",
				    sc->sc_dev.dv_xname, oid, size));
			pmd->pmd_error = EIO;
			goto out;
		}
		if (size > pmd->pmd_len)
			pmd->pmd_error = ENOMEM;
		else
			memcpy(pmd->pmd_recvbuf, (char *)pmf + sizeof(*pmf),
			    size);
		pmd->pmd_len = size;
	}

out:
	TAILQ_REMOVE(&sc->sc_mgmtinprog, pmd, pmd_link);
	wakeup_one(pmd);
	if (sc->sc_debug & SC_DEBUG_MGMT)
		DPRINTF(("%s: queue: mgmt %p <- (op %u, oid %#x, len %u)\n",
		    sc->sc_dev.dv_xname, pmd, pmf->pmf_operation,
		    pmd->pmd_oid, pmd->pmd_len));
out_nopmd:
	pgt_reinit_rx_desc_frag(sc, pd);
}

/*
 * Queue packets for reception and defragmentation.  I don't know now
 * whether the rx queue being full enough to start, but not finish,
 * queueing a fragmented packet, can happen.
 */
struct mbuf *
pgt_datarx_completion(struct pgt_softc *sc, enum pgt_queue pq)
{
	struct ifnet *ifp;
	struct pgt_desc *pd;
	struct mbuf *top, **mp, *m;
	size_t datalen;
	uint16_t morefrags, dataoff;
	int tlen = 0;

	ifp = &sc->sc_ic.ic_if;
	m = NULL;
	top = NULL;
	mp = &top;

	while ((pd = TAILQ_FIRST(&sc->sc_dirtyq[pq])) != NULL) {
		TAILQ_REMOVE(&sc->sc_dirtyq[pq], pd, pd_link);
		sc->sc_dirtyq_count[pq]--;
		datalen = letoh16(pd->pd_fragp->pf_size);
		dataoff = letoh32(pd->pd_fragp->pf_addr) - pd->pd_dmaaddr;
		morefrags = pd->pd_fragp->pf_flags & htole16(PF_FLAG_MF);

		if (sc->sc_debug & SC_DEBUG_RXFRAG)
			DPRINTF(("%s: rx frag: len %u memoff %u flags %x\n",
			    sc->sc_dev.dv_xname, datalen, dataoff,
			    pd->pd_fragp->pf_flags));

		/* Add the (two+?) bytes for the header. */
		if (datalen + dataoff > PGT_FRAG_SIZE) {
			if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
				DPRINTF(("%s data rx too big: %u\n",
				    sc->sc_dev.dv_xname, datalen));
			goto fail;
		}

		if (m == NULL)
			MGETHDR(m, M_DONTWAIT, MT_DATA);
		else
			m = m_get(M_DONTWAIT, MT_DATA);

		if (m == NULL)
			goto fail;
		if (datalen > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if (!(m->m_flags & M_EXT)) {
				m_free(m);
				goto fail;
			}
		}
		bcopy(pd->pd_mem + dataoff, mtod(m, char *), datalen);
		m->m_len = datalen;
		tlen += datalen;

		*mp = m;
		mp = &m->m_next;

		TAILQ_INSERT_TAIL(&sc->sc_freeq[pq], pd, pd_link);
		sc->sc_freeq_count[pq]++;
		pgt_reinit_rx_desc_frag(sc, pd);

		if (!morefrags)
			break;
	}

	if (top) {
		top->m_pkthdr.len = tlen;
	}
	return (top);

fail:
	TAILQ_INSERT_TAIL(&sc->sc_freeq[pq], pd, pd_link);
	sc->sc_freeq_count[pq]++;
	pgt_reinit_rx_desc_frag(sc, pd);

	ifp->if_ierrors++;
	m_freem(top);
	return (NULL);
}

int
pgt_oid_get(struct pgt_softc *sc, enum pgt_oid oid,
    void *arg, size_t arglen)
{
	struct pgt_mgmt_desc pmd;
	int error;

	bzero(&pmd, sizeof(pmd));
	pmd.pmd_recvbuf = arg;
	pmd.pmd_len = arglen;
	pmd.pmd_oid = oid;

	error = pgt_mgmt_request(sc, &pmd);
	if (error == 0)
		error = pmd.pmd_error;
	if (error != 0 && error != EPERM && sc->sc_debug & SC_DEBUG_UNEXPECTED)
		DPRINTF(("%s: failure getting oid %#x: %d\n",
		    sc->sc_dev.dv_xname, oid, error));

	return (error);
}

int
pgt_oid_retrieve(struct pgt_softc *sc, enum pgt_oid oid,
    void *arg, size_t arglen)
{
	struct pgt_mgmt_desc pmd;
	int error;

	bzero(&pmd, sizeof(pmd));
	pmd.pmd_sendbuf = arg;
	pmd.pmd_recvbuf = arg;
	pmd.pmd_len = arglen;
	pmd.pmd_oid = oid;

	error = pgt_mgmt_request(sc, &pmd);
	if (error == 0)
		error = pmd.pmd_error;
	if (error != 0 && error != EPERM && sc->sc_debug & SC_DEBUG_UNEXPECTED)
		DPRINTF(("%s: failure retrieving oid %#x: %d\n",
		    sc->sc_dev.dv_xname, oid, error));

	return (error);
}

int
pgt_oid_set(struct pgt_softc *sc, enum pgt_oid oid,
    const void *arg, size_t arglen)
{
	struct pgt_mgmt_desc pmd;
	int error;

	bzero(&pmd, sizeof(pmd));
	pmd.pmd_sendbuf = arg;
	pmd.pmd_len = arglen;
	pmd.pmd_oid = oid;

	error = pgt_mgmt_request(sc, &pmd);
	if (error == 0)
		error = pmd.pmd_error;
	if (error != 0 && error != EPERM && sc->sc_debug & SC_DEBUG_UNEXPECTED)
		DPRINTF(("%s: failure setting oid %#x: %d\n",
		    sc->sc_dev.dv_xname, oid, error));

	return (error);
}

void
pgt_state_dump(struct pgt_softc *sc)
{
	printf("%s: state dump: control 0x%08x interrupt 0x%08x\n",
	    sc->sc_dev.dv_xname,
	    pgt_read_4(sc, PGT_REG_CTRL_STAT),
	    pgt_read_4(sc, PGT_REG_INT_STAT));

	printf("%s: state dump: driver curfrag[]\n",
	    sc->sc_dev.dv_xname);

	printf("%s: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
	    sc->sc_dev.dv_xname,
	    letoh32(sc->sc_cb->pcb_driver_curfrag[0]),
	    letoh32(sc->sc_cb->pcb_driver_curfrag[1]),
	    letoh32(sc->sc_cb->pcb_driver_curfrag[2]),
	    letoh32(sc->sc_cb->pcb_driver_curfrag[3]),
	    letoh32(sc->sc_cb->pcb_driver_curfrag[4]),
	    letoh32(sc->sc_cb->pcb_driver_curfrag[5]));

	printf("%s: state dump: device curfrag[]\n",
	    sc->sc_dev.dv_xname);

	printf("%s: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
	    sc->sc_dev.dv_xname,
	    letoh32(sc->sc_cb->pcb_device_curfrag[0]),
	    letoh32(sc->sc_cb->pcb_device_curfrag[1]),
	    letoh32(sc->sc_cb->pcb_device_curfrag[2]),
	    letoh32(sc->sc_cb->pcb_device_curfrag[3]),
	    letoh32(sc->sc_cb->pcb_device_curfrag[4]),
	    letoh32(sc->sc_cb->pcb_device_curfrag[5]));
}

int
pgt_mgmt_request(struct pgt_softc *sc, struct pgt_mgmt_desc *pmd)
{
	struct pgt_desc *pd;
	struct pgt_mgmt_frame *pmf;
	int error, i, ret;

	if (sc->sc_flags & (SC_DYING | SC_NEEDS_RESET))
		return (EIO);
	if (pmd->pmd_len > PGT_FRAG_SIZE - sizeof(*pmf))
		return (ENOMEM);
	pd = TAILQ_FIRST(&sc->sc_freeq[PGT_QUEUE_MGMT_TX]);
	if (pd == NULL)
		return (ENOMEM);
	error = pgt_load_tx_desc_frag(sc, PGT_QUEUE_MGMT_TX, pd);
	if (error)
		return (error);
	pmf = (struct pgt_mgmt_frame *)pd->pd_mem;
	pmf->pmf_version = PMF_VER;
	/* "get" and "retrieve" operations look the same */
	if (pmd->pmd_recvbuf != NULL)
		pmf->pmf_operation = PMF_OP_GET;
	else
		pmf->pmf_operation = PMF_OP_SET;
	pmf->pmf_oid = htobe32(pmd->pmd_oid);
	pmf->pmf_device = PMF_DEV;
	pmf->pmf_flags = 0;
	pmf->pmf_size = htobe32(pmd->pmd_len);
	/* "set" and "retrieve" operations both send data */
	if (pmd->pmd_sendbuf != NULL)
		memcpy(pmf + 1, pmd->pmd_sendbuf, pmd->pmd_len);
	else
		bzero(pmf + 1, pmd->pmd_len);
	pmd->pmd_error = EINPROGRESS;
	TAILQ_INSERT_TAIL(&sc->sc_mgmtinprog, pmd, pmd_link);
	if (sc->sc_debug & SC_DEBUG_MGMT)
		DPRINTF(("%s: queue: mgmt %p -> (op %u, oid %#x, len %u)\n",
		    sc->sc_dev.dv_xname,
		    pmd, pmf->pmf_operation,
		    pmd->pmd_oid, pmd->pmd_len));
	pgt_desc_transmit(sc, PGT_QUEUE_MGMT_TX, pd,
	    sizeof(*pmf) + pmd->pmd_len, 0);
	/*
	 * Try for one second, triggering 10 times.
	 *
	 * Do our best to work around seemingly buggy CardBus controllers
	 * on Soekris 4521 that fail to get interrupts with alarming
	 * regularity: run as if an interrupt occurred and service every
	 * queue except for mbuf reception.
	 */
	i = 0;
	do {
		ret = tsleep_nsec(pmd, 0, "pgtmgm", MSEC_TO_NSEC(100));
		if (ret != EWOULDBLOCK)
			break;
		if (pmd->pmd_error != EINPROGRESS)
			break;
		if (sc->sc_flags & (SC_DYING | SC_NEEDS_RESET)) {
			pmd->pmd_error = EIO;
			TAILQ_REMOVE(&sc->sc_mgmtinprog, pmd, pmd_link);
			break;
		}
		if (i != 9)
			pgt_maybe_trigger(sc, PGT_QUEUE_MGMT_RX);
#ifdef PGT_BUGGY_INTERRUPT_RECOVERY
		pgt_update_intr(sc, 0);
#endif
	} while (i++ < 10);

	if (pmd->pmd_error == EINPROGRESS) {
		printf("%s: timeout waiting for management "
		    "packet response to %#x\n",
		    sc->sc_dev.dv_xname, pmd->pmd_oid);
		TAILQ_REMOVE(&sc->sc_mgmtinprog, pmd, pmd_link);
		if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
			pgt_state_dump(sc);
		pgt_async_reset(sc);
		error = ETIMEDOUT;
	} else
		error = 0;

	return (error);
}

void
pgt_desc_transmit(struct pgt_softc *sc, enum pgt_queue pq, struct pgt_desc *pd,
    uint16_t len, int morecoming)
{
	TAILQ_REMOVE(&sc->sc_freeq[pq], pd, pd_link);
	sc->sc_freeq_count[pq]--;
	TAILQ_INSERT_TAIL(&sc->sc_dirtyq[pq], pd, pd_link);
	sc->sc_dirtyq_count[pq]++;
	if (sc->sc_debug & SC_DEBUG_QUEUES)
		DPRINTF(("%s: queue: tx %u -> [%u]\n", sc->sc_dev.dv_xname,
		    pd->pd_fragnum, pq));
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_PREWRITE);
	if (morecoming)
		pd->pd_fragp->pf_flags |= htole16(PF_FLAG_MF);
	pd->pd_fragp->pf_size = htole16(len);
	bus_dmamap_sync(sc->sc_dmat, pd->pd_dmam, 0,
	    pd->pd_dmam->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	sc->sc_cb->pcb_driver_curfrag[pq] =
	    htole32(letoh32(sc->sc_cb->pcb_driver_curfrag[pq]) + 1);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_PREREAD);
	if (!morecoming)
		pgt_maybe_trigger(sc, pq);
}

void
pgt_maybe_trigger(struct pgt_softc *sc, enum pgt_queue pq)
{
	unsigned int tries = 1000000 / PGT_WRITEIO_DELAY; /* one second */
	uint32_t reg;

	if (sc->sc_debug & SC_DEBUG_TRIGGER)
		DPRINTF(("%s: triggered by queue [%u]\n",
		    sc->sc_dev.dv_xname, pq));
	pgt_debug_events(sc, "trig");
	if (sc->sc_flags & SC_POWERSAVE) {
		/* Magic values ahoy? */
		if (pgt_read_4(sc, PGT_REG_INT_STAT) == 0xabadface) {
			do {
				reg = pgt_read_4(sc, PGT_REG_CTRL_STAT);
				if (!(reg & PGT_CTRL_STAT_SLEEPMODE))
					DELAY(PGT_WRITEIO_DELAY);
			} while (tries-- != 0);
			if (!(reg & PGT_CTRL_STAT_SLEEPMODE)) {
				if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
					DPRINTF(("%s: timeout triggering from "
					    "sleep mode\n",
					    sc->sc_dev.dv_xname));
				pgt_async_reset(sc);
				return;
			}
		}
		pgt_write_4_flush(sc, PGT_REG_DEV_INT,
		    PGT_DEV_INT_WAKEUP);
		DELAY(PGT_WRITEIO_DELAY);
		/* read the status back in */
		(void)pgt_read_4(sc, PGT_REG_CTRL_STAT);
		DELAY(PGT_WRITEIO_DELAY);
	} else {
		pgt_write_4_flush(sc, PGT_REG_DEV_INT, PGT_DEV_INT_UPDATE);
		DELAY(PGT_WRITEIO_DELAY);
	}
}

struct ieee80211_node *
pgt_ieee80211_node_alloc(struct ieee80211com *ic)
{
	struct pgt_ieee80211_node *pin;

	pin = malloc(sizeof(*pin), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pin != NULL) {
		pin->pin_dot1x_auth = PIN_DOT1X_UNAUTHORIZED;
	}
	return (struct ieee80211_node *)pin;
}

void
pgt_ieee80211_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni,
    int reallynew)
{
	ieee80211_ref_node(ni);
}

void
pgt_ieee80211_node_free(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct pgt_ieee80211_node *pin;

	pin = (struct pgt_ieee80211_node *)ni;
	free(pin, M_DEVBUF, 0);
}

void
pgt_ieee80211_node_copy(struct ieee80211com *ic, struct ieee80211_node *dst,
    const struct ieee80211_node *src)
{
	const struct pgt_ieee80211_node *psrc;
	struct pgt_ieee80211_node *pdst;

	psrc = (const struct pgt_ieee80211_node *)src;
	pdst = (struct pgt_ieee80211_node *)dst;
	bcopy(psrc, pdst, sizeof(*psrc));
}

int
pgt_ieee80211_send_mgmt(struct ieee80211com *ic, struct ieee80211_node *ni,
    int type, int arg1, int arg2)
{
	return (EOPNOTSUPP);
}

int
pgt_net_attach(struct pgt_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_rateset *rs;
	uint8_t rates[IEEE80211_RATE_MAXSIZE];
	struct pgt_obj_buffer psbuffer;
	struct pgt_obj_frequencies *freqs;
	uint32_t phymode, country;
	unsigned int chan, i, j, firstchan = -1;
	int error;

	psbuffer.pob_size = htole32(PGT_FRAG_SIZE * PGT_PSM_BUFFER_FRAME_COUNT);
	psbuffer.pob_addr = htole32(sc->sc_psmdmam->dm_segs[0].ds_addr);
	error = pgt_oid_set(sc, PGT_OID_PSM_BUFFER, &psbuffer, sizeof(country));
	if (error)
		return (error);
	error = pgt_oid_get(sc, PGT_OID_PHY, &phymode, sizeof(phymode));
	if (error)
		return (error);
	error = pgt_oid_get(sc, PGT_OID_MAC_ADDRESS, ic->ic_myaddr,
	    sizeof(ic->ic_myaddr));
	if (error)
		return (error);
	error = pgt_oid_get(sc, PGT_OID_COUNTRY, &country, sizeof(country));
	if (error)
		return (error);

	ifp->if_softc = sc;
	ifp->if_ioctl = pgt_ioctl;
	ifp->if_start = pgt_start;
	ifp->if_watchdog = pgt_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	ifq_init_maxlen(&ifp->if_snd, IFQ_MAXLEN);

	/*
	 * Set channels
	 *
	 * Prism hardware likes to report supported frequencies that are
	 * not actually available for the country of origin.
	 */
	j = sizeof(*freqs) + (IEEE80211_CHAN_MAX + 1) * sizeof(uint16_t);
	freqs = malloc(j, M_DEVBUF, M_WAITOK);
	error = pgt_oid_get(sc, PGT_OID_SUPPORTED_FREQUENCIES, freqs, j);
	if (error) {
		free(freqs, M_DEVBUF, 0);
		return (error);
	}

	for (i = 0, j = letoh16(freqs->pof_count); i < j; i++) {
		chan = ieee80211_mhz2ieee(letoh16(freqs->pof_freqlist_mhz[i]),
		    0);

		if (chan > IEEE80211_CHAN_MAX) {
			printf("%s: reported bogus channel (%uMHz)\n",
			    sc->sc_dev.dv_xname, chan);
			free(freqs, M_DEVBUF, 0);
			return (EIO);
		}

		if (letoh16(freqs->pof_freqlist_mhz[i]) < 5000) {
			if (!(phymode & htole32(PGT_OID_PHY_2400MHZ)))
				continue;
			if (country == letoh32(PGT_COUNTRY_USA)) {
				if (chan >= 12 && chan <= 14)
					continue;
			}
			if (chan <= 14)
				ic->ic_channels[chan].ic_flags |=
				    IEEE80211_CHAN_B;
			ic->ic_channels[chan].ic_flags |= IEEE80211_CHAN_PUREG;
		} else {
			if (!(phymode & htole32(PGT_OID_PHY_5000MHZ)))
				continue;
			ic->ic_channels[chan].ic_flags |= IEEE80211_CHAN_A;
		}

		ic->ic_channels[chan].ic_freq =
		    letoh16(freqs->pof_freqlist_mhz[i]);

		if (firstchan == -1)
			firstchan = chan;

		DPRINTF(("%s: set channel %d to freq %uMHz\n",
		    sc->sc_dev.dv_xname, chan,
		    letoh16(freqs->pof_freqlist_mhz[i])));
	}
	free(freqs, M_DEVBUF, 0);
	if (firstchan == -1) {
		printf("%s: no channels found\n", sc->sc_dev.dv_xname);
		return (EIO);
	}

	/*
	 * Set rates
	 */
	bzero(rates, sizeof(rates));
	error = pgt_oid_get(sc, PGT_OID_SUPPORTED_RATES, rates, sizeof(rates));
	if (error)
		return (error);
	for (i = 0; i < sizeof(rates) && rates[i] != 0; i++) {
		switch (rates[i]) {
		case 2:
		case 4:
		case 11:
		case 22:
		case 44: /* maybe */
			if (phymode & htole32(PGT_OID_PHY_2400MHZ)) {
				rs = &ic->ic_sup_rates[IEEE80211_MODE_11B];
				rs->rs_rates[rs->rs_nrates++] = rates[i];
			}
		default:
			if (phymode & htole32(PGT_OID_PHY_2400MHZ)) {
				rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
				rs->rs_rates[rs->rs_nrates++] = rates[i];
			}
			if (phymode & htole32(PGT_OID_PHY_5000MHZ)) {
				rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
				rs->rs_rates[rs->rs_nrates++] = rates[i];
			}
			rs = &ic->ic_sup_rates[IEEE80211_MODE_AUTO];
			rs->rs_rates[rs->rs_nrates++] = rates[i];
		}
	}

	ic->ic_caps = IEEE80211_C_WEP | IEEE80211_C_PMGT | IEEE80211_C_TXPMGT |
	    IEEE80211_C_SHSLOT | IEEE80211_C_SHPREAMBLE | IEEE80211_C_MONITOR;
#ifndef IEEE80211_STA_ONLY
	ic->ic_caps |= IEEE80211_C_IBSS | IEEE80211_C_HOSTAP;
#endif
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;

	if_attach(ifp);
	ieee80211_ifattach(ifp);

	/* setup post-attach/pre-lateattach vector functions */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = pgt_newstate;
	ic->ic_node_alloc = pgt_ieee80211_node_alloc;
	ic->ic_newassoc = pgt_ieee80211_newassoc;
	ic->ic_node_free = pgt_ieee80211_node_free;
	ic->ic_node_copy = pgt_ieee80211_node_copy;
	ic->ic_send_mgmt = pgt_ieee80211_send_mgmt;
	ic->ic_max_rssi = 255;	/* rssi is a u_int8_t */

	/* let net80211 handle switching around the media + resetting */
	ieee80211_media_init(ifp, pgt_media_change, pgt_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(PGT_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(PGT_TX_RADIOTAP_PRESENT);
#endif
	return (0);
}

int
pgt_media_change(struct ifnet *ifp)
{
	struct pgt_softc *sc = ifp->if_softc;
	int error;

        error = ieee80211_media_change(ifp);
        if (error == ENETRESET) {
                pgt_update_hw_from_sw(sc, 0);
                error = 0;
        }

        return (error);
}

void
pgt_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct pgt_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t rate;
	int s;

	imr->ifm_status = 0;
	imr->ifm_active = IFM_IEEE80211 | IFM_NONE;

	if (!(ifp->if_flags & IFF_UP))
		return;

	s = splnet();

	if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
	} else {
		if (pgt_oid_get(sc, PGT_OID_LINK_STATE, &rate, sizeof(rate)))
			goto out;
		rate = letoh32(rate);
		if (sc->sc_debug & SC_DEBUG_LINK) {
			DPRINTF(("%s: %s: link rate %u\n",
			    sc->sc_dev.dv_xname, __func__, rate));
		}
		if (rate == 0)
			goto out;
	}

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;
	if (ic->ic_state == IEEE80211_S_RUN)
		imr->ifm_status |= IFM_ACTIVE;

	imr->ifm_active |= ieee80211_rate2media(ic, rate, ic->ic_curmode);

	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_ADHOC;
		break;
	case IEEE80211_M_AHDEMO:
		imr->ifm_active |= IFM_IEEE80211_ADHOC | IFM_FLAG0;
		break;
	case IEEE80211_M_HOSTAP:
		imr->ifm_active |= IFM_IEEE80211_HOSTAP;
		break;
#endif
	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;
	default:
		break;
	}

out:
	splx(s);
}

/*
 * Start data frames.  Critical sections surround the boundary of
 * management frame transmission / transmission acknowledgement / response
 * and data frame transmission / transmission acknowledgement.
 */
void
pgt_start(struct ifnet *ifp)
{
	struct pgt_softc *sc;
	struct ieee80211com *ic;
	struct pgt_desc *pd;
	struct mbuf *m;
	int error;

	sc = ifp->if_softc;
	ic = &sc->sc_ic;

	if (sc->sc_flags & (SC_DYING | SC_NEEDS_RESET) ||
	    !(ifp->if_flags & IFF_RUNNING) ||
	    ic->ic_state != IEEE80211_S_RUN) {
		return;
	}

	/*
	 * Management packets should probably be MLME frames
	 * (i.e. hostap "managed" mode); we don't touch the
	 * net80211 management queue.
	 */
	for (; sc->sc_dirtyq_count[PGT_QUEUE_DATA_LOW_TX] <
	    PGT_QUEUE_FULL_THRESHOLD && !ifq_empty(&ifp->if_snd);) {
		pd = TAILQ_FIRST(&sc->sc_freeq[PGT_QUEUE_DATA_LOW_TX]);
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;
		if (m->m_pkthdr.len <= PGT_FRAG_SIZE) {
			error = pgt_load_tx_desc_frag(sc,
			    PGT_QUEUE_DATA_LOW_TX, pd);
			if (error) {
				ifq_deq_rollback(&ifp->if_snd, m);
				break;
			}
			ifq_deq_commit(&ifp->if_snd, m);
			m_copydata(m, 0, m->m_pkthdr.len, pd->pd_mem);
			pgt_desc_transmit(sc, PGT_QUEUE_DATA_LOW_TX,
			    pd, m->m_pkthdr.len, 0);
		} else if (m->m_pkthdr.len <= PGT_FRAG_SIZE * 2) {
			struct pgt_desc *pd2;

			/*
			 * Transmit a fragmented frame if there is
			 * not enough room in one fragment; limit
			 * to two fragments (802.11 itself couldn't
			 * even support a full two.)
			 */
			if (sc->sc_dirtyq_count[PGT_QUEUE_DATA_LOW_TX] + 2 >
			    PGT_QUEUE_FULL_THRESHOLD) {
				ifq_deq_rollback(&ifp->if_snd, m);
				break;
			}
			pd2 = TAILQ_NEXT(pd, pd_link);
			error = pgt_load_tx_desc_frag(sc,
			    PGT_QUEUE_DATA_LOW_TX, pd);
			if (error == 0) {
				error = pgt_load_tx_desc_frag(sc,
				    PGT_QUEUE_DATA_LOW_TX, pd2);
				if (error) {
					pgt_unload_tx_desc_frag(sc, pd);
					TAILQ_INSERT_HEAD(&sc->sc_freeq[
					    PGT_QUEUE_DATA_LOW_TX], pd,
					    pd_link);
				}
			}
			if (error) {
				ifq_deq_rollback(&ifp->if_snd, m);
				break;
			}
			ifq_deq_commit(&ifp->if_snd, m);
			m_copydata(m, 0, PGT_FRAG_SIZE, pd->pd_mem);
			pgt_desc_transmit(sc, PGT_QUEUE_DATA_LOW_TX,
			    pd, PGT_FRAG_SIZE, 1);
			m_copydata(m, PGT_FRAG_SIZE,
			    m->m_pkthdr.len - PGT_FRAG_SIZE, pd2->pd_mem);
			pgt_desc_transmit(sc, PGT_QUEUE_DATA_LOW_TX,
			    pd2, m->m_pkthdr.len - PGT_FRAG_SIZE, 0);
		} else {
			ifq_deq_commit(&ifp->if_snd, m);
			ifp->if_oerrors++;
			m_freem(m);
			m = NULL;
		}
		if (m != NULL) {
			struct ieee80211_node *ni;
#if NBPFILTER > 0
			if (ifp->if_bpf != NULL)
				bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
			ifp->if_timer = 1;
			sc->sc_txtimer = 5;
			ni = ieee80211_find_txnode(&sc->sc_ic,
			    mtod(m, struct ether_header *)->ether_dhost);
			if (ni != NULL) {
				ni->ni_inact = 0;
				if (ni != ic->ic_bss)
					ieee80211_release_node(&sc->sc_ic, ni);
			}
#if NBPFILTER > 0
			if (sc->sc_drvbpf != NULL) {
				struct mbuf mb;
				struct ether_header eh;
				struct pgt_tx_radiotap_hdr *tap = &sc->sc_txtap;

				bcopy(mtod(m, struct ether_header *), &eh,
				    sizeof(eh));
				m_adj(m, sizeof(eh));
				m = pgt_ieee80211_encap(sc, &eh, m, NULL);

				tap->wt_flags = 0;
				//tap->wt_rate = rate;
				tap->wt_rate = 0;
				tap->wt_chan_freq =
				    htole16(ic->ic_bss->ni_chan->ic_freq);
				tap->wt_chan_flags =
				    htole16(ic->ic_bss->ni_chan->ic_flags);

				if (m != NULL) {
					mb.m_data = (caddr_t)tap;
					mb.m_len = sc->sc_txtap_len;
					mb.m_next = m;
					mb.m_nextpkt = NULL;
					mb.m_type = 0;
					mb.m_flags = 0;

					bpf_mtap(sc->sc_drvbpf, &mb,
					    BPF_DIRECTION_OUT);
				}
			}
#endif
			m_freem(m);
		}
	}
}

int
pgt_ioctl(struct ifnet *ifp, u_long cmd, caddr_t req)
{
	struct pgt_softc *sc = ifp->if_softc;
	struct ifreq *ifr;
	struct wi_req *wreq;
	struct ieee80211_nodereq_all *na;
	struct ieee80211com *ic;
        struct pgt_obj_bsslist *pob;
        struct wi_scan_p2_hdr *p2hdr;
        struct wi_scan_res *res;
        uint32_t noise;
	int maxscan, i, j, s, error = 0;

	ic = &sc->sc_ic;
	ifr = (struct ifreq *)req;

	s = splnet();
	switch (cmd) {
	case SIOCS80211SCAN:
		/*
		 * This chip scans always as soon as it gets initialized.
		 */
		break;
	case SIOCG80211ALLNODES: {
		struct ieee80211_nodereq *nr = NULL;
		na = (struct ieee80211_nodereq_all *)req;
		wreq = malloc(sizeof(*wreq), M_DEVBUF, M_WAITOK | M_ZERO);

		maxscan = PGT_OBJ_BSSLIST_NBSS;
		pob = malloc(sizeof(*pob) +
		    sizeof(struct pgt_obj_bss) * maxscan, M_DEVBUF, M_WAITOK);
		error = pgt_oid_get(sc, PGT_OID_NOISE_FLOOR, &noise,
		    sizeof(noise));

		if (error == 0) {
			noise = letoh32(noise);
			error = pgt_oid_get(sc, PGT_OID_BSS_LIST, pob,
			    sizeof(*pob) +
			    sizeof(struct pgt_obj_bss) * maxscan);
		}

		if (error == 0) {
			maxscan = min(PGT_OBJ_BSSLIST_NBSS,
			    letoh32(pob->pob_count));
			maxscan = min(maxscan,
			    (sizeof(wreq->wi_val) - sizeof(*p2hdr)) /
			    WI_PRISM2_RES_SIZE);
			p2hdr = (struct wi_scan_p2_hdr *)&wreq->wi_val;
			p2hdr->wi_rsvd = 0;
			p2hdr->wi_reason = 1;
			wreq->wi_len = (maxscan * WI_PRISM2_RES_SIZE) / 2 +
			    sizeof(*p2hdr) / 2;
			wreq->wi_type = WI_RID_SCAN_RES;
		}

		for (na->na_nodes = j = i = 0; i < maxscan &&
		    (na->na_size >= j + sizeof(struct ieee80211_nodereq));
		    i++) {
			/* allocate node space */
			if (nr == NULL)
				nr = malloc(sizeof(*nr), M_DEVBUF, M_WAITOK);

			/* get next BSS scan result */
			res = (struct wi_scan_res *)
			    ((char *)&wreq->wi_val + sizeof(*p2hdr) +
			    i * WI_PRISM2_RES_SIZE);
			pgt_obj_bss2scanres(sc, &pob->pob_bsslist[i],
			    res, noise);

			/* copy it to node structure for ifconfig to read */
			bzero(nr, sizeof(*nr));
			IEEE80211_ADDR_COPY(nr->nr_macaddr, res->wi_bssid);
			IEEE80211_ADDR_COPY(nr->nr_bssid, res->wi_bssid);
			nr->nr_channel = letoh16(res->wi_chan);
			nr->nr_chan_flags = IEEE80211_CHAN_B;
			nr->nr_rssi = letoh16(res->wi_signal);
			nr->nr_max_rssi = 0; /* XXX */
			nr->nr_nwid_len = letoh16(res->wi_ssid_len);
			bcopy(res->wi_ssid, nr->nr_nwid, nr->nr_nwid_len);
			nr->nr_intval = letoh16(res->wi_interval);
			nr->nr_capinfo = letoh16(res->wi_capinfo);
			nr->nr_txrate = res->wi_rate == WI_WAVELAN_RES_1M ? 2 :
			    (res->wi_rate == WI_WAVELAN_RES_2M ? 4 :
			    (res->wi_rate == WI_WAVELAN_RES_5M ? 11 :
			    (res->wi_rate == WI_WAVELAN_RES_11M ? 22 : 0)));
			nr->nr_nrates = 0;
			while (res->wi_srates[nr->nr_nrates] != 0) {
				nr->nr_rates[nr->nr_nrates] =
				    res->wi_srates[nr->nr_nrates] &
				    WI_VAR_SRATES_MASK;
				nr->nr_nrates++;
			}
			nr->nr_flags = 0;
			if (bcmp(nr->nr_macaddr, nr->nr_bssid,
			    IEEE80211_ADDR_LEN) == 0)
				nr->nr_flags |= IEEE80211_NODEREQ_AP;
			error = copyout(nr, (caddr_t)na->na_node + j,
			    sizeof(struct ieee80211_nodereq));
			if (error)
				break;

			/* point to next node entry */
			j += sizeof(struct ieee80211_nodereq);
			na->na_nodes++;
		}
		if (nr)
			free(nr, M_DEVBUF, 0);
		free(pob, M_DEVBUF, 0);
		free(wreq, M_DEVBUF, 0);
		break;
	}
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0) {
				pgt_init(ifp);
				error = ENETRESET;
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				pgt_stop(sc, SC_NEEDS_RESET);
				error = ENETRESET;
			}
		}
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > PGT_FRAG_SIZE) {
			error = EINVAL;
			break;
		}
		/* FALLTHROUGH */
	default:
		error = ieee80211_ioctl(ifp, cmd, req);
		break;
	}

	if (error == ENETRESET) {
		pgt_update_hw_from_sw(sc, 0);
		error = 0;
	}
	splx(s);

	return (error);
}

void
pgt_obj_bss2scanres(struct pgt_softc *sc, struct pgt_obj_bss *pob,
    struct wi_scan_res *scanres, uint32_t noise)
{
	struct ieee80211_rateset *rs;
	struct wi_scan_res ap;
	unsigned int i, n;

	rs = &sc->sc_ic.ic_sup_rates[IEEE80211_MODE_AUTO];
	bzero(&ap, sizeof(ap));
	ap.wi_chan = ieee80211_mhz2ieee(letoh16(pob->pob_channel), 0);
	ap.wi_noise = noise;
	ap.wi_signal = letoh16(pob->pob_rssi);
	IEEE80211_ADDR_COPY(ap.wi_bssid, pob->pob_address);
	ap.wi_interval = letoh16(pob->pob_beacon_period);
	ap.wi_capinfo = letoh16(pob->pob_capinfo);
	ap.wi_ssid_len = min(sizeof(ap.wi_ssid), pob->pob_ssid.pos_length);
	memcpy(ap.wi_ssid, pob->pob_ssid.pos_ssid, ap.wi_ssid_len);
	n = 0;
	for (i = 0; i < 16; i++) {
		if (letoh16(pob->pob_rates) & (1 << i)) {
			if (i >= rs->rs_nrates)
				break;
			ap.wi_srates[n++] = ap.wi_rate = rs->rs_rates[i];
			if (n >= sizeof(ap.wi_srates) / sizeof(ap.wi_srates[0]))
				break;
		}
	}
	memcpy(scanres, &ap, WI_PRISM2_RES_SIZE);
}

void
node_mark_active_ap(void *arg, struct ieee80211_node *ni)
{
	/*
	 * HostAP mode lets all nodes stick around unless
	 * the firmware AP kicks them off.
	 */
	ni->ni_inact = 0;
}

void
node_mark_active_adhoc(void *arg, struct ieee80211_node *ni)
{
	struct pgt_ieee80211_node *pin;

	/*
	 * As there is no association in ad-hoc, we let links just
	 * time out naturally as long they are not holding any private
	 * configuration, such as 802.1x authorization.
	 */
	pin = (struct pgt_ieee80211_node *)ni;
	if (pin->pin_dot1x_auth == PIN_DOT1X_AUTHORIZED)
		pin->pin_node.ni_inact = 0;
}

void
pgt_watchdog(struct ifnet *ifp)
{
	struct pgt_softc *sc;

	sc = ifp->if_softc;
	/*
	 * Check for timed out transmissions (and make sure to set
	 * this watchdog to fire again if there is still data in the
	 * output device queue).
	 */
	if (sc->sc_dirtyq_count[PGT_QUEUE_DATA_LOW_TX] != 0) {
		int count;

		ifp->if_timer = 1;
		if (sc->sc_txtimer && --sc->sc_txtimer == 0) {
			count = pgt_drain_tx_queue(sc, PGT_QUEUE_DATA_LOW_TX);
			if (sc->sc_debug & SC_DEBUG_UNEXPECTED)
				DPRINTF(("%s: timeout %d data transmissions\n",
				    sc->sc_dev.dv_xname, count));
		}
	}
	if (sc->sc_flags & (SC_DYING | SC_NEEDS_RESET))
		return;
	/*
	 * If we're going to kick the device out of power-save mode
	 * just to update the BSSID and such, we should not do it
	 * very often; need to determine in what way to do that.
	 */
	if (ifp->if_flags & IFF_RUNNING &&
	    sc->sc_ic.ic_state != IEEE80211_S_INIT &&
	    sc->sc_ic.ic_opmode != IEEE80211_M_MONITOR)
		pgt_async_update(sc);

#ifndef IEEE80211_STA_ONLY
	/*
	 * As a firmware-based HostAP, we should not time out
	 * nodes inside the driver additionally to the timeout
	 * that exists in the firmware.  The only things we
	 * should have to deal with timing out when doing HostAP
	 * are the privacy-related.
	 */
	switch (sc->sc_ic.ic_opmode) {
	case IEEE80211_M_HOSTAP:
		ieee80211_iterate_nodes(&sc->sc_ic,
		    node_mark_active_ap, NULL);
		break;
	case IEEE80211_M_IBSS:
		ieee80211_iterate_nodes(&sc->sc_ic,
		    node_mark_active_adhoc, NULL);
		break;
	default:
		break;
	}
#endif
	ieee80211_watchdog(ifp);
	ifp->if_timer = 1;
}

int
pgt_init(struct ifnet *ifp)
{
	struct pgt_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	/* set default channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;

	if (!(sc->sc_flags & (SC_DYING | SC_UNINITIALIZED)))
		pgt_update_hw_from_sw(sc,
		    ic->ic_state != IEEE80211_S_INIT);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Begin background scanning */
	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_SCAN, -1);

	return (0);
}

/*
 * After most every configuration change, everything needs to be fully
 * reinitialized.  For some operations (currently, WEP settings
 * in ad-hoc+802.1x mode), the change is "soft" and doesn't remove
 * "associations," and allows EAP authorization to occur again.
 * If keepassoc is specified, the reset operation should try to go
 * back to the BSS had before.
 */
void
pgt_update_hw_from_sw(struct pgt_softc *sc, int keepassoc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct arpcom *ac = &ic->ic_ac;
	struct ifnet *ifp = &ac->ac_if;
	struct pgt_obj_key keyobj;
	struct pgt_obj_ssid essid;
	uint8_t availrates[IEEE80211_RATE_MAXSIZE + 1];
	uint32_t mode, bsstype, config, profile, channel, slot, preamble;
	uint32_t wep, exunencrypted, wepkey, dot1x, auth, mlme;
	unsigned int i;
	int success, shouldbeup, s;

	config = PGT_CONFIG_MANUAL_RUN | PGT_CONFIG_RX_ANNEX;

	/*
	 * Promiscuous mode is currently a no-op since packets transmitted,
	 * while in promiscuous mode, don't ever seem to go anywhere.
	 */
	shouldbeup = ifp->if_flags & IFF_RUNNING && ifp->if_flags & IFF_UP;

	if (shouldbeup) {
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
			if (ifp->if_flags & IFF_PROMISC)
				mode = PGT_MODE_CLIENT;	/* what to do? */
			else
				mode = PGT_MODE_CLIENT;
			bsstype = PGT_BSS_TYPE_STA;
			dot1x = PGT_DOT1X_AUTH_ENABLED;
			break;
#ifndef IEEE80211_STA_ONLY
		case IEEE80211_M_IBSS:
			if (ifp->if_flags & IFF_PROMISC)
				mode = PGT_MODE_CLIENT;	/* what to do? */
			else
				mode = PGT_MODE_CLIENT;
			bsstype = PGT_BSS_TYPE_IBSS;
			dot1x = PGT_DOT1X_AUTH_ENABLED;
			break;
		case IEEE80211_M_HOSTAP:
			mode = PGT_MODE_AP;
			bsstype = PGT_BSS_TYPE_STA;
			/*
			 * For IEEE 802.1x, we need to authenticate and
			 * authorize hosts from here on or they remain
			 * associated but without the ability to send or
			 * receive normal traffic to us (courtesy the
			 * firmware AP implementation).
			 */
			dot1x = PGT_DOT1X_AUTH_ENABLED;
			/*
			 * WDS mode needs several things to work:
			 * discovery of exactly how creating the WDS
			 * links is meant to function, an interface
			 * for this, and ability to encode or decode
			 * the WDS frames.
			 */
			if (sc->sc_wds)
				config |= PGT_CONFIG_WDS;
			break;
#endif
		case IEEE80211_M_MONITOR:
			mode = PGT_MODE_PROMISCUOUS;
			bsstype = PGT_BSS_TYPE_ANY;
			dot1x = PGT_DOT1X_AUTH_NONE;
			break;
		default:
			goto badopmode;
		}
	} else {
badopmode:
		mode = PGT_MODE_CLIENT;
		bsstype = PGT_BSS_TYPE_NONE;
	}

	DPRINTF(("%s: current mode is ", sc->sc_dev.dv_xname));
	switch (ic->ic_curmode) {
	case IEEE80211_MODE_11A:
		profile = PGT_PROFILE_A_ONLY;
		preamble = PGT_OID_PREAMBLE_MODE_DYNAMIC;
		DPRINTF(("IEEE80211_MODE_11A\n"));
		break;
	case IEEE80211_MODE_11B:
		profile = PGT_PROFILE_B_ONLY;
		preamble = PGT_OID_PREAMBLE_MODE_LONG;
		DPRINTF(("IEEE80211_MODE_11B\n"));
		break;
	case IEEE80211_MODE_11G:
		profile = PGT_PROFILE_G_ONLY;
		preamble = PGT_OID_PREAMBLE_MODE_SHORT;
		DPRINTF(("IEEE80211_MODE_11G\n"));
		break;
	case IEEE80211_MODE_AUTO:
		profile = PGT_PROFILE_MIXED_G_WIFI;
		preamble = PGT_OID_PREAMBLE_MODE_DYNAMIC;
		DPRINTF(("IEEE80211_MODE_AUTO\n"));
		break;
	default:
		panic("unknown mode %d", ic->ic_curmode);
	}

	switch (sc->sc_80211_ioc_auth) {
	case IEEE80211_AUTH_NONE:
		auth = PGT_AUTH_MODE_NONE;
		break;
	case IEEE80211_AUTH_OPEN:
		auth = PGT_AUTH_MODE_OPEN;
		break;
	default:
		auth = PGT_AUTH_MODE_SHARED;
		break;
	}

	if (sc->sc_ic.ic_flags & IEEE80211_F_WEPON) {
		wep = 1;
		exunencrypted = 1;
	} else {
		wep = 0;
		exunencrypted = 0;
	}

	mlme = htole32(PGT_MLME_AUTO_LEVEL_AUTO);
	wep = htole32(wep);
	exunencrypted = htole32(exunencrypted);
	profile = htole32(profile);
	preamble = htole32(preamble);
	bsstype = htole32(bsstype);
	config = htole32(config);
	mode = htole32(mode);

	if (!wep || !sc->sc_dot1x)
		dot1x = PGT_DOT1X_AUTH_NONE;
	dot1x = htole32(dot1x);
	auth = htole32(auth);

	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		slot = htole32(PGT_OID_SLOT_MODE_SHORT);
	else
		slot = htole32(PGT_OID_SLOT_MODE_DYNAMIC);

	if (ic->ic_des_chan == IEEE80211_CHAN_ANYC) {
		if (keepassoc)
			channel = 0;
		else
			channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	} else
		channel = ieee80211_chan2ieee(ic, ic->ic_des_chan);

	DPRINTF(("%s: set rates", sc->sc_dev.dv_xname));
	for (i = 0; i < ic->ic_sup_rates[ic->ic_curmode].rs_nrates; i++) {
		availrates[i] = ic->ic_sup_rates[ic->ic_curmode].rs_rates[i];
		DPRINTF((" %d", availrates[i]));
	}
	DPRINTF(("\n"));
	availrates[i++] = 0;

	essid.pos_length = min(ic->ic_des_esslen, sizeof(essid.pos_ssid));
	memcpy(&essid.pos_ssid, ic->ic_des_essid, essid.pos_length);

	s = splnet();
	for (success = 0; success == 0; success = 1) {
		SETOID(PGT_OID_PROFILE, &profile, sizeof(profile));
		SETOID(PGT_OID_CONFIG, &config, sizeof(config));
		SETOID(PGT_OID_MLME_AUTO_LEVEL, &mlme, sizeof(mlme));

		if (!IEEE80211_ADDR_EQ(ic->ic_myaddr, ac->ac_enaddr)) {
			SETOID(PGT_OID_MAC_ADDRESS, ac->ac_enaddr,
			    sizeof(ac->ac_enaddr));
			IEEE80211_ADDR_COPY(ic->ic_myaddr, ac->ac_enaddr);
		}

		SETOID(PGT_OID_MODE, &mode, sizeof(mode));
		SETOID(PGT_OID_BSS_TYPE, &bsstype, sizeof(bsstype));

		if (channel != 0 && channel != IEEE80211_CHAN_ANY)
			SETOID(PGT_OID_CHANNEL, &channel, sizeof(channel));

		if (ic->ic_flags & IEEE80211_F_DESBSSID) {
			SETOID(PGT_OID_BSSID, ic->ic_des_bssid,
			    sizeof(ic->ic_des_bssid));
		} else if (keepassoc) {
			SETOID(PGT_OID_BSSID, ic->ic_bss->ni_bssid,
			    sizeof(ic->ic_bss->ni_bssid));
		}

		SETOID(PGT_OID_SSID, &essid, sizeof(essid));

		if (ic->ic_des_esslen > 0)
			SETOID(PGT_OID_SSID_OVERRIDE, &essid, sizeof(essid));

		SETOID(PGT_OID_RATES, &availrates, i);
		SETOID(PGT_OID_EXTENDED_RATES, &availrates, i);
		SETOID(PGT_OID_PREAMBLE_MODE, &preamble, sizeof(preamble));
		SETOID(PGT_OID_SLOT_MODE, &slot, sizeof(slot));
		SETOID(PGT_OID_AUTH_MODE, &auth, sizeof(auth));
		SETOID(PGT_OID_EXCLUDE_UNENCRYPTED, &exunencrypted,
		    sizeof(exunencrypted));
		SETOID(PGT_OID_DOT1X, &dot1x, sizeof(dot1x));
		SETOID(PGT_OID_PRIVACY_INVOKED, &wep, sizeof(wep));
		/*
		 * Setting WEP key(s)
		 */
		if (letoh32(wep) != 0) {
			keyobj.pok_type = PGT_OBJ_KEY_TYPE_WEP;
			/* key 1 */
			keyobj.pok_length = min(sizeof(keyobj.pok_key),
			    IEEE80211_KEYBUF_SIZE);
			keyobj.pok_length = min(keyobj.pok_length,
			    ic->ic_nw_keys[0].k_len);
			bcopy(ic->ic_nw_keys[0].k_key, keyobj.pok_key,
			    keyobj.pok_length);
			SETOID(PGT_OID_DEFAULT_KEY0, &keyobj, sizeof(keyobj));
			/* key 2 */
			keyobj.pok_length = min(sizeof(keyobj.pok_key),
			    IEEE80211_KEYBUF_SIZE);
			keyobj.pok_length = min(keyobj.pok_length,
			    ic->ic_nw_keys[1].k_len);
			bcopy(ic->ic_nw_keys[1].k_key, keyobj.pok_key,
			    keyobj.pok_length);
			SETOID(PGT_OID_DEFAULT_KEY1, &keyobj, sizeof(keyobj));
			/* key 3 */
			keyobj.pok_length = min(sizeof(keyobj.pok_key),
			    IEEE80211_KEYBUF_SIZE);
			keyobj.pok_length = min(keyobj.pok_length,
			    ic->ic_nw_keys[2].k_len);
			bcopy(ic->ic_nw_keys[2].k_key, keyobj.pok_key,
			    keyobj.pok_length);
			SETOID(PGT_OID_DEFAULT_KEY2, &keyobj, sizeof(keyobj));
			/* key 4 */
			keyobj.pok_length = min(sizeof(keyobj.pok_key),
			    IEEE80211_KEYBUF_SIZE);
			keyobj.pok_length = min(keyobj.pok_length,
			    ic->ic_nw_keys[3].k_len);
			bcopy(ic->ic_nw_keys[3].k_key, keyobj.pok_key,
			    keyobj.pok_length);
			SETOID(PGT_OID_DEFAULT_KEY3, &keyobj, sizeof(keyobj));

			wepkey = htole32(ic->ic_wep_txkey);
			SETOID(PGT_OID_DEFAULT_KEYNUM, &wepkey, sizeof(wepkey));
		}
		/* set mode again to commit */
		SETOID(PGT_OID_MODE, &mode, sizeof(mode));
	}
	splx(s);

	if (success) {
		if (shouldbeup)
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		else
			ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	} else {
		printf("%s: problem setting modes\n", sc->sc_dev.dv_xname);
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	}
}

void
pgt_hostap_handle_mlme(struct pgt_softc *sc, uint32_t oid,
    struct pgt_obj_mlme *mlme)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct pgt_ieee80211_node *pin;
	struct ieee80211_node *ni;

	ni = ieee80211_find_node(ic, mlme->pom_address);
	pin = (struct pgt_ieee80211_node *)ni;
	switch (oid) {
	case PGT_OID_DISASSOCIATE:
		if (ni != NULL)
			ieee80211_release_node(&sc->sc_ic, ni);
		break;
	case PGT_OID_ASSOCIATE:
		if (ni == NULL) {
			ni = ieee80211_dup_bss(ic, mlme->pom_address);
			if (ni == NULL)
				break;
			ic->ic_newassoc(ic, ni, 1);
			pin = (struct pgt_ieee80211_node *)ni;
		}
		ni->ni_associd = letoh16(mlme->pom_id);
		pin->pin_mlme_state = letoh16(mlme->pom_state);
		break;
	default:
		if (pin != NULL)
			pin->pin_mlme_state = letoh16(mlme->pom_state);
		break;
	}
}

/*
 * Either in response to an event or after a certain amount of time,
 * synchronize our idea of the network we're part of from the hardware.
 */
void
pgt_update_sw_from_hw(struct pgt_softc *sc, struct pgt_async_trap *pa,
	    struct mbuf *args)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct pgt_obj_ssid ssid;
	struct pgt_obj_bss bss;
	uint32_t channel, noise, ls;
	int error, s;

	if (pa != NULL) {
		struct pgt_obj_mlme *mlme;
		uint32_t oid;

		oid = *mtod(args, uint32_t *);
		m_adj(args, sizeof(uint32_t));
		if (sc->sc_debug & SC_DEBUG_TRAP)
			DPRINTF(("%s: trap: oid %#x len %u\n",
			    sc->sc_dev.dv_xname, oid, args->m_len));
		switch (oid) {
		case PGT_OID_LINK_STATE:
			if (args->m_len < sizeof(uint32_t))
				break;
			ls = letoh32(*mtod(args, uint32_t *));
			if (sc->sc_debug & (SC_DEBUG_TRAP | SC_DEBUG_LINK))
				DPRINTF(("%s: %s: link rate %u\n",
				    sc->sc_dev.dv_xname, __func__, ls));
			if (ls)
				ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
			else
				ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
			goto gotlinkstate;
		case PGT_OID_DEAUTHENTICATE:
		case PGT_OID_AUTHENTICATE:
		case PGT_OID_DISASSOCIATE:
		case PGT_OID_ASSOCIATE:
			if (args->m_len < sizeof(struct pgt_obj_mlme))
				break;
			mlme = mtod(args, struct pgt_obj_mlme *);
			if (sc->sc_debug & SC_DEBUG_TRAP)
				DPRINTF(("%s: mlme: address "
				    "%s id 0x%02x state 0x%02x code 0x%02x\n",
				    sc->sc_dev.dv_xname,
				    ether_sprintf(mlme->pom_address),
				    letoh16(mlme->pom_id),
				    letoh16(mlme->pom_state),
				    letoh16(mlme->pom_code)));
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_HOSTAP)
				pgt_hostap_handle_mlme(sc, oid, mlme);
#endif
			break;
		}
		return;
	}
	if (ic->ic_state == IEEE80211_S_SCAN) {
		s = splnet();
		error = pgt_oid_get(sc, PGT_OID_LINK_STATE, &ls, sizeof(ls));
		splx(s);
		if (error)
			return;
		DPRINTF(("%s: up_sw_from_hw: link %u\n", sc->sc_dev.dv_xname,
		    htole32(ls)));
		if (ls != 0)
			ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	}

gotlinkstate:
	s = splnet();
	if (pgt_oid_get(sc, PGT_OID_NOISE_FLOOR, &noise, sizeof(noise)) != 0)
		goto out;
	sc->sc_noise = letoh32(noise);
	if (ic->ic_state == IEEE80211_S_RUN) {
		if (pgt_oid_get(sc, PGT_OID_CHANNEL, &channel,
		    sizeof(channel)) != 0)
			goto out;
		channel = min(letoh32(channel), IEEE80211_CHAN_MAX);
		ic->ic_bss->ni_chan = &ic->ic_channels[channel];
		if (pgt_oid_get(sc, PGT_OID_BSSID, ic->ic_bss->ni_bssid,
		    sizeof(ic->ic_bss->ni_bssid)) != 0)
			goto out;
		IEEE80211_ADDR_COPY(&bss.pob_address, ic->ic_bss->ni_bssid);
		error = pgt_oid_retrieve(sc, PGT_OID_BSS_FIND, &bss,
		    sizeof(bss));
		if (error == 0)
			ic->ic_bss->ni_rssi = bss.pob_rssi;
		else if (error != EPERM)
			goto out;
		error = pgt_oid_get(sc, PGT_OID_SSID, &ssid, sizeof(ssid));
		if (error)
			goto out;
		ic->ic_bss->ni_esslen = min(ssid.pos_length,
		    sizeof(ic->ic_bss->ni_essid));
		memcpy(ic->ic_bss->ni_essid, ssid.pos_ssid,
		    ssid.pos_length);
	}

out:
	splx(s);
}

int
pgt_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct pgt_softc *sc = ic->ic_if.if_softc;
	enum ieee80211_state ostate;

	ostate = ic->ic_state;

	DPRINTF(("%s: newstate %s -> %s\n", sc->sc_dev.dv_xname,
	    ieee80211_state_name[ostate], ieee80211_state_name[nstate]));

	switch (nstate) {
	case IEEE80211_S_INIT:
		if (sc->sc_dirtyq_count[PGT_QUEUE_DATA_LOW_TX] == 0)
			ic->ic_if.if_timer = 0;
		ic->ic_mgt_timer = 0;
		ic->ic_flags &= ~IEEE80211_F_SIBSS;
		ieee80211_free_allnodes(ic, 1);
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);
		break;
	case IEEE80211_S_SCAN:
		ic->ic_if.if_timer = 1;
		ic->ic_mgt_timer = 0;
		ieee80211_node_cleanup(ic, ic->ic_bss);
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);
#ifndef IEEE80211_STA_ONLY
		/* Just use any old channel; we override it anyway. */
		if (ic->ic_opmode == IEEE80211_M_HOSTAP)
			ieee80211_create_ibss(ic, ic->ic_ibss_chan);
#endif
		break;
	case IEEE80211_S_RUN:
		ic->ic_if.if_timer = 1;
		break;
	default:
		break;
	}

	return (sc->sc_newstate(ic, nstate, arg));
}

int
pgt_drain_tx_queue(struct pgt_softc *sc, enum pgt_queue pq)
{
	int wokeup = 0;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_PREWRITE);
	sc->sc_cb->pcb_device_curfrag[pq] =
	    sc->sc_cb->pcb_driver_curfrag[pq];
	bus_dmamap_sync(sc->sc_dmat, sc->sc_cbdmam, 0,
	    sc->sc_cbdmam->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_PREREAD);
	while (!TAILQ_EMPTY(&sc->sc_dirtyq[pq])) {
		struct pgt_desc *pd;

		pd = TAILQ_FIRST(&sc->sc_dirtyq[pq]);
		TAILQ_REMOVE(&sc->sc_dirtyq[pq], pd, pd_link);
		sc->sc_dirtyq_count[pq]--;
		TAILQ_INSERT_TAIL(&sc->sc_freeq[pq], pd, pd_link);
		sc->sc_freeq_count[pq]++;
		pgt_unload_tx_desc_frag(sc, pd);
		if (sc->sc_debug & SC_DEBUG_QUEUES)
			DPRINTF(("%s: queue: tx %u <- [%u] (drained)\n",
			    sc->sc_dev.dv_xname, pd->pd_fragnum, pq));
		wokeup++;
		if (pgt_queue_is_data(pq))
			sc->sc_ic.ic_if.if_oerrors++;
	}

	return (wokeup);
}

int
pgt_dma_alloc(struct pgt_softc *sc)
{
	size_t size;
	int i, error, nsegs;

	for (i = 0; i < PGT_QUEUE_COUNT; i++) {
		TAILQ_INIT(&sc->sc_freeq[i]);
		TAILQ_INIT(&sc->sc_dirtyq[i]);
	}

	/*
	 * control block
	 */
	size = sizeof(struct pgt_control_block);

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->sc_cbdmam);
	if (error != 0) {
		printf("%s: can not create DMA tag for control block\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE,
	    0, &sc->sc_cbdmas, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: can not allocate DMA memory for control block\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->sc_cbdmas, nsegs,
	    size, (caddr_t *)&sc->sc_cb, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can not map DMA memory for control block\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->sc_cbdmam,
	    sc->sc_cb, size, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can not load DMA map for control block\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	/*
	 * powersave
	 */
	size = PGT_FRAG_SIZE * PGT_PSM_BUFFER_FRAME_COUNT;

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_ALLOCNOW, &sc->sc_psmdmam);
	if (error != 0) {
		printf("%s: can not create DMA tag for powersave\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE,
	   0, &sc->sc_psmdmas, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: can not allocate DMA memory for powersave\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->sc_psmdmas, nsegs,
	    size, (caddr_t *)&sc->sc_psmbuf, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can not map DMA memory for powersave\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->sc_psmdmam,
	    sc->sc_psmbuf, size, NULL, BUS_DMA_WAITOK);
	if (error != 0) {
		printf("%s: can not load DMA map for powersave\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	/*
	 * fragments
	 */
	error = pgt_dma_alloc_queue(sc, PGT_QUEUE_DATA_LOW_RX);
	if (error != 0)
		goto out;

	error = pgt_dma_alloc_queue(sc, PGT_QUEUE_DATA_LOW_TX);
	if (error != 0)
		goto out;

	error = pgt_dma_alloc_queue(sc, PGT_QUEUE_DATA_HIGH_RX);
	if (error != 0)
		goto out;

	error = pgt_dma_alloc_queue(sc, PGT_QUEUE_DATA_HIGH_TX);
	if (error != 0)
		goto out;

	error = pgt_dma_alloc_queue(sc, PGT_QUEUE_MGMT_RX);
	if (error != 0)
		goto out;

	error = pgt_dma_alloc_queue(sc, PGT_QUEUE_MGMT_TX);
	if (error != 0)
		goto out;

out:
	if (error) {
		printf("%s: error in DMA allocation\n", sc->sc_dev.dv_xname);
		pgt_dma_free(sc);
	}

	return (error);
}

int
pgt_dma_alloc_queue(struct pgt_softc *sc, enum pgt_queue pq)
{
	struct pgt_desc *pd;
	size_t i, qsize;
	int error, nsegs;

	switch (pq) {
		case PGT_QUEUE_DATA_LOW_RX:
			qsize = PGT_QUEUE_DATA_RX_SIZE;
			break;
		case PGT_QUEUE_DATA_LOW_TX:
			qsize = PGT_QUEUE_DATA_TX_SIZE;
			break;
		case PGT_QUEUE_DATA_HIGH_RX:
			qsize = PGT_QUEUE_DATA_RX_SIZE;
			break;
		case PGT_QUEUE_DATA_HIGH_TX:
			qsize = PGT_QUEUE_DATA_TX_SIZE;
			break;
		case PGT_QUEUE_MGMT_RX:
			qsize = PGT_QUEUE_MGMT_SIZE;
			break;
		case PGT_QUEUE_MGMT_TX:
			qsize = PGT_QUEUE_MGMT_SIZE;
			break;
		default:
			return (EINVAL);
	}

	for (i = 0; i < qsize; i++) {
		pd = malloc(sizeof(*pd), M_DEVBUF, M_WAITOK);

		error = bus_dmamap_create(sc->sc_dmat, PGT_FRAG_SIZE, 1,
		    PGT_FRAG_SIZE, 0, BUS_DMA_ALLOCNOW, &pd->pd_dmam);
		if (error != 0) {
			printf("%s: can not create DMA tag for fragment\n",
			    sc->sc_dev.dv_xname);
			free(pd, M_DEVBUF, 0);
			break;
		}

		error = bus_dmamem_alloc(sc->sc_dmat, PGT_FRAG_SIZE, PAGE_SIZE,
		    0, &pd->pd_dmas, 1, &nsegs, BUS_DMA_WAITOK);
		if (error != 0) {
			printf("%s: error alloc frag %zu on queue %u\n",
			    sc->sc_dev.dv_xname, i, pq);
			free(pd, M_DEVBUF, 0);
			break;
		}

		error = bus_dmamem_map(sc->sc_dmat, &pd->pd_dmas, nsegs,
		    PGT_FRAG_SIZE, (caddr_t *)&pd->pd_mem, BUS_DMA_WAITOK);
		if (error != 0) {
			printf("%s: error map frag %zu on queue %u\n",
			    sc->sc_dev.dv_xname, i, pq);
			free(pd, M_DEVBUF, 0);
			break;
		}

		if (pgt_queue_is_rx(pq)) {
			error = bus_dmamap_load(sc->sc_dmat, pd->pd_dmam,
			    pd->pd_mem, PGT_FRAG_SIZE, NULL, BUS_DMA_NOWAIT);
			if (error != 0) {
				printf("%s: error load frag %zu on queue %u\n",
				    sc->sc_dev.dv_xname, i, pq);
				bus_dmamem_free(sc->sc_dmat, &pd->pd_dmas,
				    nsegs);
				free(pd, M_DEVBUF, 0);
				break;
			}
			pd->pd_dmaaddr = pd->pd_dmam->dm_segs[0].ds_addr;
		}
		TAILQ_INSERT_TAIL(&sc->sc_freeq[pq], pd, pd_link);
	}

	return (error);
}

void
pgt_dma_free(struct pgt_softc *sc)
{
	/*
	 * fragments
	 */
	if (sc->sc_dmat != NULL) {
		pgt_dma_free_queue(sc, PGT_QUEUE_DATA_LOW_RX);
		pgt_dma_free_queue(sc, PGT_QUEUE_DATA_LOW_TX);
		pgt_dma_free_queue(sc, PGT_QUEUE_DATA_HIGH_RX);
		pgt_dma_free_queue(sc, PGT_QUEUE_DATA_HIGH_TX);
		pgt_dma_free_queue(sc, PGT_QUEUE_MGMT_RX);
		pgt_dma_free_queue(sc, PGT_QUEUE_MGMT_TX);
	}

	/*
	 * powersave
	 */
	if (sc->sc_psmbuf != NULL) {
		bus_dmamap_unload(sc->sc_dmat, sc->sc_psmdmam);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_psmdmas, 1);
		sc->sc_psmbuf = NULL;
		sc->sc_psmdmam = NULL;
	}

	/*
	 * control block
	 */
	if (sc->sc_cb != NULL) {
		bus_dmamap_unload(sc->sc_dmat, sc->sc_cbdmam);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_cbdmas, 1);
		sc->sc_cb = NULL;
		sc->sc_cbdmam = NULL;
	}
}

void
pgt_dma_free_queue(struct pgt_softc *sc, enum pgt_queue pq)
{
	struct pgt_desc	*pd;

	while (!TAILQ_EMPTY(&sc->sc_freeq[pq])) {
		pd = TAILQ_FIRST(&sc->sc_freeq[pq]);
		TAILQ_REMOVE(&sc->sc_freeq[pq], pd, pd_link);
		if (pd->pd_dmam != NULL) {
			bus_dmamap_unload(sc->sc_dmat, pd->pd_dmam);
			pd->pd_dmam = NULL;
		}
		bus_dmamem_free(sc->sc_dmat, &pd->pd_dmas, 1);
		free(pd, M_DEVBUF, 0);
	}
}

int
pgt_activate(struct device *self, int act)
{
	struct pgt_softc *sc = (struct pgt_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	DPRINTF(("%s: %s(%d)\n", sc->sc_dev.dv_xname, __func__, why));

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING) {
			pgt_stop(sc, SC_NEEDS_RESET);
			pgt_update_hw_from_sw(sc, 0);
		}
		if (sc->sc_power != NULL)
			(*sc->sc_power)(sc, act);
		break;
	case DVACT_WAKEUP:
		pgt_wakeup(sc);
		break;
	}
	return 0;
}

void
pgt_wakeup(struct pgt_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (sc->sc_power != NULL)
		(*sc->sc_power)(sc, DVACT_RESUME);

	pgt_stop(sc, SC_NEEDS_RESET);
	pgt_update_hw_from_sw(sc, 0);

	if (ifp->if_flags & IFF_UP) {
		pgt_init(ifp);
		pgt_update_hw_from_sw(sc, 0);
	}
}
