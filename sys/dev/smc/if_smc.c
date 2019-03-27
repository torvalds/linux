/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Benno Rice.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for SMSC LAN91C111, may work for older variants.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_mib.h>
#include <net/if_media.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#include <net/bpf.h>
#include <net/bpfdesc.h>

#include <dev/smc/if_smcreg.h>
#include <dev/smc/if_smcvar.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#define	SMC_LOCK(sc)		mtx_lock(&(sc)->smc_mtx)
#define	SMC_UNLOCK(sc)		mtx_unlock(&(sc)->smc_mtx)
#define	SMC_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->smc_mtx, MA_OWNED)

#define	SMC_INTR_PRIORITY	0
#define	SMC_RX_PRIORITY		5
#define	SMC_TX_PRIORITY		10

devclass_t	smc_devclass;

static const char *smc_chip_ids[16] = {
	NULL, NULL, NULL,
	/* 3 */ "SMSC LAN91C90 or LAN91C92",
	/* 4 */ "SMSC LAN91C94",
	/* 5 */ "SMSC LAN91C95",
	/* 6 */ "SMSC LAN91C96",
	/* 7 */ "SMSC LAN91C100",
	/* 8 */	"SMSC LAN91C100FD",
	/* 9 */ "SMSC LAN91C110FD or LAN91C111FD",
	NULL, NULL, NULL,
	NULL, NULL, NULL
};

static void	smc_init(void *);
static void	smc_start(struct ifnet *);
static void	smc_stop(struct smc_softc *);
static int	smc_ioctl(struct ifnet *, u_long, caddr_t);

static void	smc_init_locked(struct smc_softc *);
static void	smc_start_locked(struct ifnet *);
static void	smc_reset(struct smc_softc *);
static int	smc_mii_ifmedia_upd(struct ifnet *);
static void	smc_mii_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void	smc_mii_tick(void *);
static void	smc_mii_mediachg(struct smc_softc *);
static int	smc_mii_mediaioctl(struct smc_softc *, struct ifreq *, u_long);

static void	smc_task_intr(void *, int);
static void	smc_task_rx(void *, int);
static void	smc_task_tx(void *, int);

static driver_filter_t	smc_intr;
static timeout_t	smc_watchdog;
#ifdef DEVICE_POLLING
static poll_handler_t	smc_poll;
#endif

/*
 * MII bit-bang glue
 */
static uint32_t smc_mii_bitbang_read(device_t);
static void smc_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops smc_mii_bitbang_ops = {
	smc_mii_bitbang_read,
	smc_mii_bitbang_write,
	{
		MGMT_MDO,	/* MII_BIT_MDO */
		MGMT_MDI,	/* MII_BIT_MDI */
		MGMT_MCLK,	/* MII_BIT_MDC */
		MGMT_MDOE,	/* MII_BIT_DIR_HOST_PHY */
		0,		/* MII_BIT_DIR_PHY_HOST */
	}
};

static __inline void
smc_select_bank(struct smc_softc *sc, uint16_t bank)
{

	bus_barrier(sc->smc_reg, BSR, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_2(sc->smc_reg, BSR, bank & BSR_BANK_MASK);
	bus_barrier(sc->smc_reg, BSR, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/* Never call this when not in bank 2. */
static __inline void
smc_mmu_wait(struct smc_softc *sc)
{

	KASSERT((bus_read_2(sc->smc_reg, BSR) &
	    BSR_BANK_MASK) == 2, ("%s: smc_mmu_wait called when not in bank 2",
	    device_get_nameunit(sc->smc_dev)));
	while (bus_read_2(sc->smc_reg, MMUCR) & MMUCR_BUSY)
		;
}

static __inline uint8_t
smc_read_1(struct smc_softc *sc, bus_size_t offset)
{

	return (bus_read_1(sc->smc_reg, offset));
}

static __inline void
smc_write_1(struct smc_softc *sc, bus_size_t offset, uint8_t val)
{

	bus_write_1(sc->smc_reg, offset, val);
}

static __inline uint16_t
smc_read_2(struct smc_softc *sc, bus_size_t offset)
{

	return (bus_read_2(sc->smc_reg, offset));
}

static __inline void
smc_write_2(struct smc_softc *sc, bus_size_t offset, uint16_t val)
{

	bus_write_2(sc->smc_reg, offset, val);
}

static __inline void
smc_read_multi_2(struct smc_softc *sc, bus_size_t offset, uint16_t *datap,
    bus_size_t count)
{

	bus_read_multi_2(sc->smc_reg, offset, datap, count);
}

static __inline void
smc_write_multi_2(struct smc_softc *sc, bus_size_t offset, uint16_t *datap,
    bus_size_t count)
{

	bus_write_multi_2(sc->smc_reg, offset, datap, count);
}

static __inline void
smc_barrier(struct smc_softc *sc, bus_size_t offset, bus_size_t length,
    int flags)
{

	bus_barrier(sc->smc_reg, offset, length, flags);
}

int
smc_probe(device_t dev)
{
	int			rid, type, error;
	uint16_t		val;
	struct smc_softc	*sc;
	struct resource		*reg;

	sc = device_get_softc(dev);
	rid = 0;
	type = SYS_RES_IOPORT;
	error = 0;

	if (sc->smc_usemem)
		type = SYS_RES_MEMORY;

	reg = bus_alloc_resource_anywhere(dev, type, &rid, 16, RF_ACTIVE);
	if (reg == NULL) {
		if (bootverbose)
			device_printf(dev,
			    "could not allocate I/O resource for probe\n");
		return (ENXIO);
	}

	/* Check for the identification value in the BSR. */
	val = bus_read_2(reg, BSR);
	if ((val & BSR_IDENTIFY_MASK) != BSR_IDENTIFY) {
		if (bootverbose)
			device_printf(dev, "identification value not in BSR\n");
		error = ENXIO;
		goto done;
	}

	/*
	 * Try switching banks and make sure we still get the identification
	 * value.
	 */
	bus_write_2(reg, BSR, 0);
	val = bus_read_2(reg, BSR);
	if ((val & BSR_IDENTIFY_MASK) != BSR_IDENTIFY) {
		if (bootverbose)
			device_printf(dev,
			    "identification value not in BSR after write\n");
		error = ENXIO;
		goto done;
	}

#if 0
	/* Check the BAR. */
	bus_write_2(reg, BSR, 1);
	val = bus_read_2(reg, BAR);
	val = BAR_ADDRESS(val);
	if (rman_get_start(reg) != val) {
		if (bootverbose)
			device_printf(dev, "BAR address %x does not match "
			    "I/O resource address %lx\n", val,
			    rman_get_start(reg));
		error = ENXIO;
		goto done;
	}
#endif

	/* Compare REV against known chip revisions. */
	bus_write_2(reg, BSR, 3);
	val = bus_read_2(reg, REV);
	val = (val & REV_CHIP_MASK) >> REV_CHIP_SHIFT;
	if (smc_chip_ids[val] == NULL) {
		if (bootverbose)
			device_printf(dev, "Unknown chip revision: %d\n", val);
		error = ENXIO;
		goto done;
	}

	device_set_desc(dev, smc_chip_ids[val]);

done:
	bus_release_resource(dev, type, rid, reg);
	return (error);
}

int
smc_attach(device_t dev)
{
	int			type, error;
	uint16_t		val;
	u_char			eaddr[ETHER_ADDR_LEN];
	struct smc_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	error = 0;

	sc->smc_dev = dev;

	ifp = sc->smc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		error = ENOSPC;
		goto done;
	}

	mtx_init(&sc->smc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/* Set up watchdog callout. */
	callout_init_mtx(&sc->smc_watchdog, &sc->smc_mtx, 0);

	type = SYS_RES_IOPORT;
	if (sc->smc_usemem)
		type = SYS_RES_MEMORY;

	sc->smc_reg_rid = 0;
	sc->smc_reg = bus_alloc_resource_anywhere(dev, type, &sc->smc_reg_rid,
	    16, RF_ACTIVE);
	if (sc->smc_reg == NULL) {
		error = ENXIO;
		goto done;
	}

	sc->smc_irq = bus_alloc_resource_anywhere(dev, SYS_RES_IRQ,
	    &sc->smc_irq_rid, 1, RF_ACTIVE | RF_SHAREABLE);
	if (sc->smc_irq == NULL) {
		error = ENXIO;
		goto done;
	}

	SMC_LOCK(sc);
	smc_reset(sc);
	SMC_UNLOCK(sc);

	smc_select_bank(sc, 3);
	val = smc_read_2(sc, REV);
	sc->smc_chip = (val & REV_CHIP_MASK) >> REV_CHIP_SHIFT;
	sc->smc_rev = (val * REV_REV_MASK) >> REV_REV_SHIFT;
	if (bootverbose)
		device_printf(dev, "revision %x\n", sc->smc_rev);

	callout_init_mtx(&sc->smc_mii_tick_ch, &sc->smc_mtx,
	    CALLOUT_RETURNUNLOCKED);
	if (sc->smc_chip >= REV_CHIP_91110FD) {
		(void)mii_attach(dev, &sc->smc_miibus, ifp,
		    smc_mii_ifmedia_upd, smc_mii_ifmedia_sts, BMSR_DEFCAPMASK,
		    MII_PHY_ANY, MII_OFFSET_ANY, 0);
		if (sc->smc_miibus != NULL) {
			sc->smc_mii_tick = smc_mii_tick;
			sc->smc_mii_mediachg = smc_mii_mediachg;
			sc->smc_mii_mediaioctl = smc_mii_mediaioctl;
		}
	}

	smc_select_bank(sc, 1);
	eaddr[0] = smc_read_1(sc, IAR0);
	eaddr[1] = smc_read_1(sc, IAR1);
	eaddr[2] = smc_read_1(sc, IAR2);
	eaddr[3] = smc_read_1(sc, IAR3);
	eaddr[4] = smc_read_1(sc, IAR4);
	eaddr[5] = smc_read_1(sc, IAR5);

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = smc_init;
	ifp->if_ioctl = smc_ioctl;
	ifp->if_start = smc_start;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	IFQ_SET_READY(&ifp->if_snd);

	ifp->if_capabilities = ifp->if_capenable = 0;

#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif

	ether_ifattach(ifp, eaddr);

	/* Set up taskqueue */
	TASK_INIT(&sc->smc_intr, SMC_INTR_PRIORITY, smc_task_intr, ifp);
	TASK_INIT(&sc->smc_rx, SMC_RX_PRIORITY, smc_task_rx, ifp);
	TASK_INIT(&sc->smc_tx, SMC_TX_PRIORITY, smc_task_tx, ifp);
	sc->smc_tq = taskqueue_create_fast("smc_taskq", M_NOWAIT,
	    taskqueue_thread_enqueue, &sc->smc_tq);
	taskqueue_start_threads(&sc->smc_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->smc_dev));

	/* Mask all interrupts. */
	sc->smc_mask = 0;
	smc_write_1(sc, MSK, 0);

	/* Wire up interrupt */
	error = bus_setup_intr(dev, sc->smc_irq,
	    INTR_TYPE_NET|INTR_MPSAFE, smc_intr, NULL, sc, &sc->smc_ih);
	if (error != 0)
		goto done;

done:
	if (error != 0)
		smc_detach(dev);
	return (error);
}

int
smc_detach(device_t dev)
{
	int			type;
	struct smc_softc	*sc;

	sc = device_get_softc(dev);
	SMC_LOCK(sc);
	smc_stop(sc);
	SMC_UNLOCK(sc);

	if (sc->smc_ifp != NULL) {
		ether_ifdetach(sc->smc_ifp);
	}
	
	callout_drain(&sc->smc_watchdog);
	callout_drain(&sc->smc_mii_tick_ch);
	
#ifdef DEVICE_POLLING
	if (sc->smc_ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(sc->smc_ifp);
#endif

	if (sc->smc_ih != NULL)
		bus_teardown_intr(sc->smc_dev, sc->smc_irq, sc->smc_ih);

	if (sc->smc_tq != NULL) {
		taskqueue_drain(sc->smc_tq, &sc->smc_intr);
		taskqueue_drain(sc->smc_tq, &sc->smc_rx);
		taskqueue_drain(sc->smc_tq, &sc->smc_tx);
		taskqueue_free(sc->smc_tq);
		sc->smc_tq = NULL;
	}

	if (sc->smc_ifp != NULL) {
		if_free(sc->smc_ifp);
	}

	if (sc->smc_miibus != NULL) {
		device_delete_child(sc->smc_dev, sc->smc_miibus);
		bus_generic_detach(sc->smc_dev);
	}

	if (sc->smc_reg != NULL) {
		type = SYS_RES_IOPORT;
		if (sc->smc_usemem)
			type = SYS_RES_MEMORY;

		bus_release_resource(sc->smc_dev, type, sc->smc_reg_rid,
		    sc->smc_reg);
	}

	if (sc->smc_irq != NULL)
		bus_release_resource(sc->smc_dev, SYS_RES_IRQ, sc->smc_irq_rid,
		   sc->smc_irq);

	if (mtx_initialized(&sc->smc_mtx))
		mtx_destroy(&sc->smc_mtx);

	return (0);
}

static void
smc_start(struct ifnet *ifp)
{
	struct smc_softc	*sc;

	sc = ifp->if_softc;
	SMC_LOCK(sc);
	smc_start_locked(ifp);
	SMC_UNLOCK(sc);
}

static void
smc_start_locked(struct ifnet *ifp)
{
	struct smc_softc	*sc;
	struct mbuf		*m;
	u_int			len, npages, spin_count;

	sc = ifp->if_softc;
	SMC_ASSERT_LOCKED(sc);

	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;
	if (IFQ_IS_EMPTY(&ifp->if_snd))
		return;

	/*
	 * Grab the next packet.  If it's too big, drop it.
	 */
	IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
	len = m_length(m, NULL);
	len += (len & 1);
	if (len > ETHER_MAX_LEN - ETHER_CRC_LEN) {
		if_printf(ifp, "large packet discarded\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m);
		return; /* XXX readcheck? */
	}

	/*
	 * Flag that we're busy.
	 */
	ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	sc->smc_pending = m;

	/*
	 * Work out how many 256 byte "pages" we need.  We have to include the
	 * control data for the packet in this calculation.
	 */
	npages = (len + PKT_CTRL_DATA_LEN) >> 8;
	if (npages == 0)
		npages = 1;

	/*
	 * Request memory.
	 */
	smc_select_bank(sc, 2);
	smc_mmu_wait(sc);
	smc_write_2(sc, MMUCR, MMUCR_CMD_TX_ALLOC | npages);

	/*
	 * Spin briefly to see if the allocation succeeds.
	 */
	spin_count = TX_ALLOC_WAIT_TIME;
	do {
		if (smc_read_1(sc, IST) & ALLOC_INT) {
			smc_write_1(sc, ACK, ALLOC_INT);
			break;
		}
	} while (--spin_count);

	/*
	 * If the allocation is taking too long, unmask the alloc interrupt
	 * and wait.
	 */
	if (spin_count == 0) {
		sc->smc_mask |= ALLOC_INT;
		if ((ifp->if_capenable & IFCAP_POLLING) == 0)
			smc_write_1(sc, MSK, sc->smc_mask);
		return;
	}

	taskqueue_enqueue(sc->smc_tq, &sc->smc_tx);
}

static void
smc_task_tx(void *context, int pending)
{
	struct ifnet		*ifp;
	struct smc_softc	*sc;
	struct mbuf		*m, *m0;
	u_int			packet, len;
	int			last_len;
	uint8_t			*data;

	(void)pending;
	ifp = (struct ifnet *)context;
	sc = ifp->if_softc;

	SMC_LOCK(sc);
	
	if (sc->smc_pending == NULL) {
		SMC_UNLOCK(sc);
		goto next_packet;
	}

	m = m0 = sc->smc_pending;
	sc->smc_pending = NULL;
	smc_select_bank(sc, 2);

	/*
	 * Check the allocation result.
	 */
	packet = smc_read_1(sc, ARR);

	/*
	 * If the allocation failed, requeue the packet and retry.
	 */
	if (packet & ARR_FAILED) {
		IFQ_DRV_PREPEND(&ifp->if_snd, m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		smc_start_locked(ifp);
		SMC_UNLOCK(sc);
		return;
	}

	/*
	 * Tell the device to write to our packet number.
	 */
	smc_write_1(sc, PNR, packet);
	smc_write_2(sc, PTR, 0 | PTR_AUTO_INCR);

	/*
	 * Tell the device how long the packet is (including control data).
	 */
	len = m_length(m, 0);
	len += PKT_CTRL_DATA_LEN;
	smc_write_2(sc, DATA0, 0);
	smc_write_2(sc, DATA0, len);

	/*
	 * Push the data out to the device.
	 */
	data = NULL;
	last_len = 0;
	for (; m != NULL; m = m->m_next) {
		data = mtod(m, uint8_t *);
		smc_write_multi_2(sc, DATA0, (uint16_t *)data, m->m_len / 2);
		last_len = m->m_len;
	}

	/*
	 * Push out the control byte and and the odd byte if needed.
	 */
	if ((len & 1) != 0 && data != NULL)
		smc_write_2(sc, DATA0, (CTRL_ODD << 8) | data[last_len - 1]);
	else
		smc_write_2(sc, DATA0, 0);

	/*
	 * Unmask the TX empty interrupt.
	 */
	sc->smc_mask |= TX_EMPTY_INT;
	if ((ifp->if_capenable & IFCAP_POLLING) == 0)
		smc_write_1(sc, MSK, sc->smc_mask);

	/*
	 * Enqueue the packet.
	 */
	smc_mmu_wait(sc);
	smc_write_2(sc, MMUCR, MMUCR_CMD_ENQUEUE);
	callout_reset(&sc->smc_watchdog, hz * 2, smc_watchdog, sc);

	/*
	 * Finish up.
	 */
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	SMC_UNLOCK(sc);
	BPF_MTAP(ifp, m0);
	m_freem(m0);

next_packet:
	/*
	 * See if there's anything else to do.
	 */
	smc_start(ifp);
}

static void
smc_task_rx(void *context, int pending)
{
	u_int			packet, status, len;
	uint8_t			*data;
	struct ifnet		*ifp;
	struct smc_softc	*sc;
	struct mbuf		*m, *mhead, *mtail;

	(void)pending;
	ifp = (struct ifnet *)context;
	sc = ifp->if_softc;
	mhead = mtail = NULL;

	SMC_LOCK(sc);

	packet = smc_read_1(sc, FIFO_RX);
	while ((packet & FIFO_EMPTY) == 0) {
		/*
		 * Grab an mbuf and attach a cluster.
		 */
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL) {
			break;
		}
		if (!(MCLGET(m, M_NOWAIT))) {
			m_freem(m);
			break;
		}
	
		/*
		 * Point to the start of the packet.
		 */
		smc_select_bank(sc, 2);
		smc_write_1(sc, PNR, packet);
		smc_write_2(sc, PTR, 0 | PTR_READ | PTR_RCV | PTR_AUTO_INCR);

		/*
		 * Grab status and packet length.
		 */
		status = smc_read_2(sc, DATA0);
		len = smc_read_2(sc, DATA0) & RX_LEN_MASK;
		len -= 6;
		if (status & RX_ODDFRM)
			len += 1;

		/*
		 * Check for errors.
		 */
		if (status & (RX_TOOSHORT | RX_TOOLNG | RX_BADCRC | RX_ALGNERR)) {
			smc_mmu_wait(sc);
			smc_write_2(sc, MMUCR, MMUCR_CMD_RELEASE);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			m_freem(m);
			break;
		}
	
		/*
		 * Set the mbuf up the way we want it.
		 */
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = len + 2; /* XXX: Is this right? */
		m_adj(m, ETHER_ALIGN);
	
		/*
		 * Pull the packet out of the device.  Make sure we're in the
		 * right bank first as things may have changed while we were
		 * allocating our mbuf.
		 */
		smc_select_bank(sc, 2);
		smc_write_1(sc, PNR, packet);
		smc_write_2(sc, PTR, 4 | PTR_READ | PTR_RCV | PTR_AUTO_INCR);
		data = mtod(m, uint8_t *);
		smc_read_multi_2(sc, DATA0, (uint16_t *)data, len >> 1);
		if (len & 1) {
			data += len & ~1;
			*data = smc_read_1(sc, DATA0);
		}

		/*
		 * Tell the device we're done.
		 */
		smc_mmu_wait(sc);
		smc_write_2(sc, MMUCR, MMUCR_CMD_RELEASE);
		if (m == NULL) {
			break;
		}
		
		if (mhead == NULL) {
			mhead = mtail = m;
			m->m_next = NULL;
		} else {
			mtail->m_next = m;
			mtail = m;
		}
		packet = smc_read_1(sc, FIFO_RX);
	}

	sc->smc_mask |= RCV_INT;
	if ((ifp->if_capenable & IFCAP_POLLING) == 0)
		smc_write_1(sc, MSK, sc->smc_mask);

	SMC_UNLOCK(sc);

	while (mhead != NULL) {
		m = mhead;
		mhead = mhead->m_next;
		m->m_next = NULL;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		(*ifp->if_input)(ifp, m);
	}
}

#ifdef DEVICE_POLLING
static int
smc_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct smc_softc	*sc;

	sc = ifp->if_softc;

	SMC_LOCK(sc);
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		SMC_UNLOCK(sc);
		return (0);
	}
	SMC_UNLOCK(sc);

	if (cmd == POLL_AND_CHECK_STATUS)
		taskqueue_enqueue(sc->smc_tq, &sc->smc_intr);
        return (0);
}
#endif

static int
smc_intr(void *context)
{
	struct smc_softc	*sc;
	uint32_t curbank;

	sc = (struct smc_softc *)context;

	/*
	 * Save current bank and restore later in this function
	 */
	curbank = (smc_read_2(sc, BSR) & BSR_BANK_MASK);

	/*
	 * Block interrupts in order to let smc_task_intr to kick in
	 */
	smc_select_bank(sc, 2);
	smc_write_1(sc, MSK, 0);

	/* Restore bank */
	smc_select_bank(sc, curbank);

	taskqueue_enqueue(sc->smc_tq, &sc->smc_intr);
	return (FILTER_HANDLED);
}

static void
smc_task_intr(void *context, int pending)
{
	struct smc_softc	*sc;
	struct ifnet		*ifp;
	u_int			status, packet, counter, tcr;

	(void)pending;
	ifp = (struct ifnet *)context;
	sc = ifp->if_softc;

	SMC_LOCK(sc);
	
	smc_select_bank(sc, 2);

	/*
	 * Find out what interrupts are flagged.
	 */
	status = smc_read_1(sc, IST) & sc->smc_mask;

	/*
	 * Transmit error
	 */
	if (status & TX_INT) {
		/*
		 * Kill off the packet if there is one and re-enable transmit.
		 */
		packet = smc_read_1(sc, FIFO_TX);
		if ((packet & FIFO_EMPTY) == 0) {
			callout_stop(&sc->smc_watchdog);
			smc_select_bank(sc, 2);
			smc_write_1(sc, PNR, packet);
			smc_write_2(sc, PTR, 0 | PTR_READ | 
			    PTR_AUTO_INCR);
			smc_select_bank(sc, 0);
			tcr = smc_read_2(sc, EPHSR);
#if 0
			if ((tcr & EPHSR_TX_SUC) == 0)
				device_printf(sc->smc_dev,
				    "bad packet\n");
#endif
			smc_select_bank(sc, 2);
			smc_mmu_wait(sc);
			smc_write_2(sc, MMUCR, MMUCR_CMD_RELEASE_PKT);

			smc_select_bank(sc, 0);
			tcr = smc_read_2(sc, TCR);
			tcr |= TCR_TXENA | TCR_PAD_EN;
			smc_write_2(sc, TCR, tcr);
			smc_select_bank(sc, 2);
			taskqueue_enqueue(sc->smc_tq, &sc->smc_tx);
		}

		/*
		 * Ack the interrupt.
		 */
		smc_write_1(sc, ACK, TX_INT);
	}

	/*
	 * Receive
	 */
	if (status & RCV_INT) {
		smc_write_1(sc, ACK, RCV_INT);
		sc->smc_mask &= ~RCV_INT;
		taskqueue_enqueue(sc->smc_tq, &sc->smc_rx);
	}

	/*
	 * Allocation
	 */
	if (status & ALLOC_INT) {
		smc_write_1(sc, ACK, ALLOC_INT);
		sc->smc_mask &= ~ALLOC_INT;
		taskqueue_enqueue(sc->smc_tq, &sc->smc_tx);
	}

	/*
	 * Receive overrun
	 */
	if (status & RX_OVRN_INT) {
		smc_write_1(sc, ACK, RX_OVRN_INT);
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
	}

	/*
	 * Transmit empty
	 */
	if (status & TX_EMPTY_INT) {
		smc_write_1(sc, ACK, TX_EMPTY_INT);
		sc->smc_mask &= ~TX_EMPTY_INT;
		callout_stop(&sc->smc_watchdog);

		/*
		 * Update collision stats.
		 */
		smc_select_bank(sc, 0);
		counter = smc_read_2(sc, ECR);
		smc_select_bank(sc, 2);
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
		    ((counter & ECR_SNGLCOL_MASK) >> ECR_SNGLCOL_SHIFT) +
		    ((counter & ECR_MULCOL_MASK) >> ECR_MULCOL_SHIFT));

		/*
		 * See if there are any packets to transmit.
		 */
		taskqueue_enqueue(sc->smc_tq, &sc->smc_tx);
	}

	/*
	 * Update the interrupt mask.
	 */
	smc_select_bank(sc, 2);
	if ((ifp->if_capenable & IFCAP_POLLING) == 0)
		smc_write_1(sc, MSK, sc->smc_mask);

	SMC_UNLOCK(sc);
}

static uint32_t
smc_mii_bitbang_read(device_t dev)
{
	struct smc_softc	*sc;
	uint32_t		val;

	sc = device_get_softc(dev);

	SMC_ASSERT_LOCKED(sc);
	KASSERT((smc_read_2(sc, BSR) & BSR_BANK_MASK) == 3,
	    ("%s: smc_mii_bitbang_read called with bank %d (!= 3)",
	    device_get_nameunit(sc->smc_dev),
	    smc_read_2(sc, BSR) & BSR_BANK_MASK));

	val = smc_read_2(sc, MGMT);
	smc_barrier(sc, MGMT, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (val);
}

static void
smc_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct smc_softc	*sc;

	sc = device_get_softc(dev);

	SMC_ASSERT_LOCKED(sc);
	KASSERT((smc_read_2(sc, BSR) & BSR_BANK_MASK) == 3,
	    ("%s: smc_mii_bitbang_write called with bank %d (!= 3)",
	    device_get_nameunit(sc->smc_dev),
	    smc_read_2(sc, BSR) & BSR_BANK_MASK));

	smc_write_2(sc, MGMT, val);
	smc_barrier(sc, MGMT, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

int
smc_miibus_readreg(device_t dev, int phy, int reg)
{
	struct smc_softc	*sc;
	int			val;

	sc = device_get_softc(dev);

	SMC_LOCK(sc);

	smc_select_bank(sc, 3);

	val = mii_bitbang_readreg(dev, &smc_mii_bitbang_ops, phy, reg);

	SMC_UNLOCK(sc);
	return (val);
}

int
smc_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct smc_softc	*sc;

	sc = device_get_softc(dev);

	SMC_LOCK(sc);

	smc_select_bank(sc, 3);

	mii_bitbang_writereg(dev, &smc_mii_bitbang_ops, phy, reg, data);

	SMC_UNLOCK(sc);
	return (0);
}

void
smc_miibus_statchg(device_t dev)
{
	struct smc_softc	*sc;
	struct mii_data		*mii;
	uint16_t		tcr;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->smc_miibus);

	SMC_LOCK(sc);

	smc_select_bank(sc, 0);
	tcr = smc_read_2(sc, TCR);

	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
		tcr |= TCR_SWFDUP;
	else
		tcr &= ~TCR_SWFDUP;

	smc_write_2(sc, TCR, tcr);

	SMC_UNLOCK(sc);
}

static int
smc_mii_ifmedia_upd(struct ifnet *ifp)
{
	struct smc_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	if (sc->smc_miibus == NULL)
		return (ENXIO);

	mii = device_get_softc(sc->smc_miibus);
	return (mii_mediachg(mii));
}

static void
smc_mii_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct smc_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	if (sc->smc_miibus == NULL)
		return;

	mii = device_get_softc(sc->smc_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static void
smc_mii_tick(void *context)
{
	struct smc_softc	*sc;

	sc = (struct smc_softc *)context;

	if (sc->smc_miibus == NULL)
		return;

	SMC_UNLOCK(sc);

	mii_tick(device_get_softc(sc->smc_miibus));
	callout_reset(&sc->smc_mii_tick_ch, hz, smc_mii_tick, sc);
}

static void
smc_mii_mediachg(struct smc_softc *sc)
{

	if (sc->smc_miibus == NULL)
		return;
	mii_mediachg(device_get_softc(sc->smc_miibus));
}

static int
smc_mii_mediaioctl(struct smc_softc *sc, struct ifreq *ifr, u_long command)
{
	struct mii_data	*mii;

	if (sc->smc_miibus == NULL)
		return (EINVAL);

	mii = device_get_softc(sc->smc_miibus);
	return (ifmedia_ioctl(sc->smc_ifp, ifr, &mii->mii_media, command));
}

static void
smc_reset(struct smc_softc *sc)
{
	u_int	ctr;

	SMC_ASSERT_LOCKED(sc);

	smc_select_bank(sc, 2);

	/*
	 * Mask all interrupts.
	 */
	smc_write_1(sc, MSK, 0);

	/*
	 * Tell the device to reset.
	 */
	smc_select_bank(sc, 0);
	smc_write_2(sc, RCR, RCR_SOFT_RST);

	/*
	 * Set up the configuration register.
	 */
	smc_select_bank(sc, 1);
	smc_write_2(sc, CR, CR_EPH_POWER_EN);
	DELAY(1);

	/*
	 * Turn off transmit and receive.
	 */
	smc_select_bank(sc, 0);
	smc_write_2(sc, TCR, 0);
	smc_write_2(sc, RCR, 0);

	/*
	 * Set up the control register.
	 */
	smc_select_bank(sc, 1);
	ctr = smc_read_2(sc, CTR);
	ctr |= CTR_LE_ENABLE | CTR_AUTO_RELEASE;
	smc_write_2(sc, CTR, ctr);

	/*
	 * Reset the MMU.
	 */
	smc_select_bank(sc, 2);
	smc_mmu_wait(sc);
	smc_write_2(sc, MMUCR, MMUCR_CMD_MMU_RESET);
}

static void
smc_enable(struct smc_softc *sc)
{
	struct ifnet		*ifp;

	SMC_ASSERT_LOCKED(sc);
	ifp = sc->smc_ifp;

	/*
	 * Set up the receive/PHY control register.
	 */
	smc_select_bank(sc, 0);
	smc_write_2(sc, RPCR, RPCR_ANEG | (RPCR_LED_LINK_ANY << RPCR_LSA_SHIFT)
	    | (RPCR_LED_ACT_ANY << RPCR_LSB_SHIFT));

	/*
	 * Set up the transmit and receive control registers.
	 */
	smc_write_2(sc, TCR, TCR_TXENA | TCR_PAD_EN);
	smc_write_2(sc, RCR, RCR_RXEN | RCR_STRIP_CRC);

	/*
	 * Set up the interrupt mask.
	 */
	smc_select_bank(sc, 2);
	sc->smc_mask = EPH_INT | RX_OVRN_INT | RCV_INT | TX_INT;
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		smc_write_1(sc, MSK, sc->smc_mask);
}

static void
smc_stop(struct smc_softc *sc)
{

	SMC_ASSERT_LOCKED(sc);

	/*
	 * Turn off callouts.
	 */
	callout_stop(&sc->smc_watchdog);
	callout_stop(&sc->smc_mii_tick_ch);

	/*
	 * Mask all interrupts.
	 */
	smc_select_bank(sc, 2);
	sc->smc_mask = 0;
	smc_write_1(sc, MSK, 0);
#ifdef DEVICE_POLLING
	ether_poll_deregister(sc->smc_ifp);
	sc->smc_ifp->if_capenable &= ~IFCAP_POLLING;
#endif

	/*
	 * Disable transmit and receive.
	 */
	smc_select_bank(sc, 0);
	smc_write_2(sc, TCR, 0);
	smc_write_2(sc, RCR, 0);

	sc->smc_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
}

static void
smc_watchdog(void *arg)
{
	struct smc_softc	*sc;
	
	sc = (struct smc_softc *)arg;
	device_printf(sc->smc_dev, "watchdog timeout\n");
	taskqueue_enqueue(sc->smc_tq, &sc->smc_intr);
}

static void
smc_init(void *context)
{
	struct smc_softc	*sc;

	sc = (struct smc_softc *)context;
	SMC_LOCK(sc);
	smc_init_locked(sc);
	SMC_UNLOCK(sc);
}

static void
smc_init_locked(struct smc_softc *sc)
{
	struct ifnet	*ifp;

	SMC_ASSERT_LOCKED(sc);
	ifp = sc->smc_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	smc_reset(sc);
	smc_enable(sc);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	smc_start_locked(ifp);

	if (sc->smc_mii_tick != NULL)
		callout_reset(&sc->smc_mii_tick_ch, hz, sc->smc_mii_tick, sc);

#ifdef DEVICE_POLLING
	SMC_UNLOCK(sc);
	ether_poll_register(smc_poll, ifp);
	SMC_LOCK(sc);
	ifp->if_capenable |= IFCAP_POLLING;
#endif
}

static int
smc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct smc_softc	*sc;
	int			error;

	sc = ifp->if_softc;
	error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
			SMC_LOCK(sc);
			smc_stop(sc);
			SMC_UNLOCK(sc);
		} else {
			smc_init(sc);
			if (sc->smc_mii_mediachg != NULL)
				sc->smc_mii_mediachg(sc);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* XXX
		SMC_LOCK(sc);
		smc_setmcast(sc);
		SMC_UNLOCK(sc);
		*/
		error = EINVAL;
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->smc_mii_mediaioctl == NULL) {
			error = EINVAL;
			break;
		}
		sc->smc_mii_mediaioctl(sc, (struct ifreq *)data, cmd);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}
