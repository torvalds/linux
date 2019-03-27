/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
 * Copyright (c) 2016 Rubicon Communications, LLC (Netgate)
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

/*
 * TI Common Platform Ethernet Switch (CPSW) Driver
 * Found in TI8148 "DaVinci" and AM335x "Sitara" SoCs.
 *
 * This controller is documented in the AM335x Technical Reference
 * Manual, in the TMS320DM814x DaVinci Digital Video Processors TRM
 * and in the TMS320C6452 3 Port Switch Ethernet Subsystem TRM.
 *
 * It is basically a single Ethernet port (port 0) wired internally to
 * a 3-port store-and-forward switch connected to two independent
 * "sliver" controllers (port 1 and port 2).  You can operate the
 * controller in a variety of different ways by suitably configuring
 * the slivers and the Address Lookup Engine (ALE) that routes packets
 * between the ports.
 *
 * This code was developed and tested on a BeagleBone with
 * an AM335x SoC.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cpsw.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/stdarg.h>

#include <net/ethernet.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <arm/ti/ti_scm.h>
#include <arm/ti/am335x/am335x_scm.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
 
#ifdef CPSW_ETHERSWITCH
#include <dev/etherswitch/etherswitch.h>
#include "etherswitch_if.h"
#endif

#include "if_cpswreg.h"
#include "if_cpswvar.h"

#include "miibus_if.h"

/* Device probe/attach/detach. */
static int cpsw_probe(device_t);
static int cpsw_attach(device_t);
static int cpsw_detach(device_t);
static int cpswp_probe(device_t);
static int cpswp_attach(device_t);
static int cpswp_detach(device_t);

static phandle_t cpsw_get_node(device_t, device_t);

/* Device Init/shutdown. */
static int cpsw_shutdown(device_t);
static void cpswp_init(void *);
static void cpswp_init_locked(void *);
static void cpswp_stop_locked(struct cpswp_softc *);

/* Device Suspend/Resume. */
static int cpsw_suspend(device_t);
static int cpsw_resume(device_t);

/* Ioctl. */
static int cpswp_ioctl(struct ifnet *, u_long command, caddr_t data);

static int cpswp_miibus_readreg(device_t, int phy, int reg);
static int cpswp_miibus_writereg(device_t, int phy, int reg, int value);
static void cpswp_miibus_statchg(device_t);

/* Send/Receive packets. */
static void cpsw_intr_rx(void *arg);
static struct mbuf *cpsw_rx_dequeue(struct cpsw_softc *);
static void cpsw_rx_enqueue(struct cpsw_softc *);
static void cpswp_start(struct ifnet *);
static void cpsw_intr_tx(void *);
static void cpswp_tx_enqueue(struct cpswp_softc *);
static int cpsw_tx_dequeue(struct cpsw_softc *);

/* Misc interrupts and watchdog. */
static void cpsw_intr_rx_thresh(void *);
static void cpsw_intr_misc(void *);
static void cpswp_tick(void *);
static void cpswp_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int cpswp_ifmedia_upd(struct ifnet *);
static void cpsw_tx_watchdog(void *);

/* ALE support */
static void cpsw_ale_read_entry(struct cpsw_softc *, uint16_t, uint32_t *);
static void cpsw_ale_write_entry(struct cpsw_softc *, uint16_t, uint32_t *);
static int cpsw_ale_mc_entry_set(struct cpsw_softc *, uint8_t, int, uint8_t *);
static void cpsw_ale_dump_table(struct cpsw_softc *);
static int cpsw_ale_update_vlan_table(struct cpsw_softc *, int, int, int, int,
	int);
static int cpswp_ale_update_addresses(struct cpswp_softc *, int);

/* Statistics and sysctls. */
static void cpsw_add_sysctls(struct cpsw_softc *);
static void cpsw_stats_collect(struct cpsw_softc *);
static int cpsw_stats_sysctl(SYSCTL_HANDLER_ARGS);

#ifdef CPSW_ETHERSWITCH
static etherswitch_info_t *cpsw_getinfo(device_t);
static int cpsw_getport(device_t, etherswitch_port_t *);
static int cpsw_setport(device_t, etherswitch_port_t *);
static int cpsw_getconf(device_t, etherswitch_conf_t *);
static int cpsw_getvgroup(device_t, etherswitch_vlangroup_t *);
static int cpsw_setvgroup(device_t, etherswitch_vlangroup_t *);
static int cpsw_readreg(device_t, int);
static int cpsw_writereg(device_t, int, int);
static int cpsw_readphy(device_t, int, int);
static int cpsw_writephy(device_t, int, int, int);
#endif

/*
 * Arbitrary limit on number of segments in an mbuf to be transmitted.
 * Packets with more segments than this will be defragmented before
 * they are queued.
 */
#define	CPSW_TXFRAGS		16

/* Shared resources. */
static device_method_t cpsw_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cpsw_probe),
	DEVMETHOD(device_attach,	cpsw_attach),
	DEVMETHOD(device_detach,	cpsw_detach),
	DEVMETHOD(device_shutdown,	cpsw_shutdown),
	DEVMETHOD(device_suspend,	cpsw_suspend),
	DEVMETHOD(device_resume,	cpsw_resume),
	/* Bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	/* OFW methods */
	DEVMETHOD(ofw_bus_get_node,	cpsw_get_node),
#ifdef CPSW_ETHERSWITCH
	/* etherswitch interface */
	DEVMETHOD(etherswitch_getinfo,	cpsw_getinfo),
	DEVMETHOD(etherswitch_readreg,	cpsw_readreg),
	DEVMETHOD(etherswitch_writereg,	cpsw_writereg),
	DEVMETHOD(etherswitch_readphyreg,	cpsw_readphy),
	DEVMETHOD(etherswitch_writephyreg,	cpsw_writephy),
	DEVMETHOD(etherswitch_getport,	cpsw_getport),
	DEVMETHOD(etherswitch_setport,	cpsw_setport),
	DEVMETHOD(etherswitch_getvgroup,	cpsw_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	cpsw_setvgroup),
	DEVMETHOD(etherswitch_getconf,	cpsw_getconf),
#endif
	DEVMETHOD_END
};

static driver_t cpsw_driver = {
	"cpswss",
	cpsw_methods,
	sizeof(struct cpsw_softc),
};

static devclass_t cpsw_devclass;

DRIVER_MODULE(cpswss, simplebus, cpsw_driver, cpsw_devclass, 0, 0);

/* Port/Slave resources. */
static device_method_t cpswp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cpswp_probe),
	DEVMETHOD(device_attach,	cpswp_attach),
	DEVMETHOD(device_detach,	cpswp_detach),
	/* MII interface */
	DEVMETHOD(miibus_readreg,	cpswp_miibus_readreg),
	DEVMETHOD(miibus_writereg,	cpswp_miibus_writereg),
	DEVMETHOD(miibus_statchg,	cpswp_miibus_statchg),
	DEVMETHOD_END
};

static driver_t cpswp_driver = {
	"cpsw",
	cpswp_methods,
	sizeof(struct cpswp_softc),
};

static devclass_t cpswp_devclass;

#ifdef CPSW_ETHERSWITCH
DRIVER_MODULE(etherswitch, cpswss, etherswitch_driver, etherswitch_devclass, 0, 0);
MODULE_DEPEND(cpswss, etherswitch, 1, 1, 1);
#endif

DRIVER_MODULE(cpsw, cpswss, cpswp_driver, cpswp_devclass, 0, 0);
DRIVER_MODULE(miibus, cpsw, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(cpsw, ether, 1, 1, 1);
MODULE_DEPEND(cpsw, miibus, 1, 1, 1);

#ifdef CPSW_ETHERSWITCH
static struct cpsw_vlangroups cpsw_vgroups[CPSW_VLANS];
#endif

static uint32_t slave_mdio_addr[] = { 0x4a100200, 0x4a100300 };

static struct resource_spec irq_res_spec[] = {
	{ SYS_RES_IRQ, 0, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_IRQ, 1, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_IRQ, 2, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_IRQ, 3, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct {
	void (*cb)(void *);
} cpsw_intr_cb[] = {
	{ cpsw_intr_rx_thresh },
	{ cpsw_intr_rx },
	{ cpsw_intr_tx },
	{ cpsw_intr_misc },
};

/* Number of entries here must match size of stats
 * array in struct cpswp_softc. */
static struct cpsw_stat {
	int	reg;
	char *oid;
} cpsw_stat_sysctls[CPSW_SYSCTL_COUNT] = {
	{0x00, "GoodRxFrames"},
	{0x04, "BroadcastRxFrames"},
	{0x08, "MulticastRxFrames"},
	{0x0C, "PauseRxFrames"},
	{0x10, "RxCrcErrors"},
	{0x14, "RxAlignErrors"},
	{0x18, "OversizeRxFrames"},
	{0x1c, "RxJabbers"},
	{0x20, "ShortRxFrames"},
	{0x24, "RxFragments"},
	{0x30, "RxOctets"},
	{0x34, "GoodTxFrames"},
	{0x38, "BroadcastTxFrames"},
	{0x3c, "MulticastTxFrames"},
	{0x40, "PauseTxFrames"},
	{0x44, "DeferredTxFrames"},
	{0x48, "CollisionsTxFrames"},
	{0x4c, "SingleCollisionTxFrames"},
	{0x50, "MultipleCollisionTxFrames"},
	{0x54, "ExcessiveCollisions"},
	{0x58, "LateCollisions"},
	{0x5c, "TxUnderrun"},
	{0x60, "CarrierSenseErrors"},
	{0x64, "TxOctets"},
	{0x68, "RxTx64OctetFrames"},
	{0x6c, "RxTx65to127OctetFrames"},
	{0x70, "RxTx128to255OctetFrames"},
	{0x74, "RxTx256to511OctetFrames"},
	{0x78, "RxTx512to1024OctetFrames"},
	{0x7c, "RxTx1024upOctetFrames"},
	{0x80, "NetOctets"},
	{0x84, "RxStartOfFrameOverruns"},
	{0x88, "RxMiddleOfFrameOverruns"},
	{0x8c, "RxDmaOverruns"}
};

/*
 * Basic debug support.
 */

static void
cpsw_debugf_head(const char *funcname)
{
	int t = (int)(time_second % (24 * 60 * 60));

	printf("%02d:%02d:%02d %s ", t / (60 * 60), (t / 60) % 60, t % 60, funcname);
}

static void
cpsw_debugf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");

}

#define	CPSW_DEBUGF(_sc, a) do {					\
	if ((_sc)->debug) {						\
		cpsw_debugf_head(__func__);				\
		cpsw_debugf a;						\
	}								\
} while (0)

/*
 * Locking macros
 */
#define	CPSW_TX_LOCK(sc) do {						\
		mtx_assert(&(sc)->rx.lock, MA_NOTOWNED);		\
		mtx_lock(&(sc)->tx.lock);				\
} while (0)

#define	CPSW_TX_UNLOCK(sc)	mtx_unlock(&(sc)->tx.lock)
#define	CPSW_TX_LOCK_ASSERT(sc)	mtx_assert(&(sc)->tx.lock, MA_OWNED)

#define	CPSW_RX_LOCK(sc) do {						\
		mtx_assert(&(sc)->tx.lock, MA_NOTOWNED);		\
		mtx_lock(&(sc)->rx.lock);				\
} while (0)

#define	CPSW_RX_UNLOCK(sc)		mtx_unlock(&(sc)->rx.lock)
#define	CPSW_RX_LOCK_ASSERT(sc)	mtx_assert(&(sc)->rx.lock, MA_OWNED)

#define CPSW_PORT_LOCK(_sc) do {					\
		mtx_assert(&(_sc)->lock, MA_NOTOWNED);			\
		mtx_lock(&(_sc)->lock);					\
} while (0)

#define	CPSW_PORT_UNLOCK(_sc)	mtx_unlock(&(_sc)->lock)
#define	CPSW_PORT_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->lock, MA_OWNED)

/*
 * Read/Write macros
 */
#define	cpsw_read_4(_sc, _reg)		bus_read_4((_sc)->mem_res, (_reg))
#define	cpsw_write_4(_sc, _reg, _val)					\
	bus_write_4((_sc)->mem_res, (_reg), (_val))

#define	cpsw_cpdma_bd_offset(i)	(CPSW_CPPI_RAM_OFFSET + ((i)*16))

#define	cpsw_cpdma_bd_paddr(sc, slot)					\
	BUS_SPACE_PHYSADDR(sc->mem_res, slot->bd_offset)
#define	cpsw_cpdma_read_bd(sc, slot, val)				\
	bus_read_region_4(sc->mem_res, slot->bd_offset, (uint32_t *) val, 4)
#define	cpsw_cpdma_write_bd(sc, slot, val)				\
	bus_write_region_4(sc->mem_res, slot->bd_offset, (uint32_t *) val, 4)
#define	cpsw_cpdma_write_bd_next(sc, slot, next_slot)			\
	cpsw_write_4(sc, slot->bd_offset, cpsw_cpdma_bd_paddr(sc, next_slot))
#define	cpsw_cpdma_write_bd_flags(sc, slot, val)			\
	bus_write_2(sc->mem_res, slot->bd_offset + 14, val)
#define	cpsw_cpdma_read_bd_flags(sc, slot)				\
	bus_read_2(sc->mem_res, slot->bd_offset + 14)
#define	cpsw_write_hdp_slot(sc, queue, slot)				\
	cpsw_write_4(sc, (queue)->hdp_offset, cpsw_cpdma_bd_paddr(sc, slot))
#define	CP_OFFSET (CPSW_CPDMA_TX_CP(0) - CPSW_CPDMA_TX_HDP(0))
#define	cpsw_read_cp(sc, queue)						\
	cpsw_read_4(sc, (queue)->hdp_offset + CP_OFFSET) 
#define	cpsw_write_cp(sc, queue, val)					\
	cpsw_write_4(sc, (queue)->hdp_offset + CP_OFFSET, (val))
#define	cpsw_write_cp_slot(sc, queue, slot)				\
	cpsw_write_cp(sc, queue, cpsw_cpdma_bd_paddr(sc, slot))

#if 0
/* XXX temporary function versions for debugging. */
static void
cpsw_write_hdp_slotX(struct cpsw_softc *sc, struct cpsw_queue *queue, struct cpsw_slot *slot)
{
	uint32_t reg = queue->hdp_offset;
	uint32_t v = cpsw_cpdma_bd_paddr(sc, slot);
	CPSW_DEBUGF(("HDP <=== 0x%08x (was 0x%08x)", v, cpsw_read_4(sc, reg)));
	cpsw_write_4(sc, reg, v);
}

static void
cpsw_write_cp_slotX(struct cpsw_softc *sc, struct cpsw_queue *queue, struct cpsw_slot *slot)
{
	uint32_t v = cpsw_cpdma_bd_paddr(sc, slot);
	CPSW_DEBUGF(("CP <=== 0x%08x (expecting 0x%08x)", v, cpsw_read_cp(sc, queue)));
	cpsw_write_cp(sc, queue, v);
}
#endif

/*
 * Expanded dump routines for verbose debugging.
 */
static void
cpsw_dump_slot(struct cpsw_softc *sc, struct cpsw_slot *slot)
{
	static const char *flags[] = {"SOP", "EOP", "Owner", "EOQ",
	    "TDownCmplt", "PassCRC", "Long", "Short", "MacCtl", "Overrun",
	    "PktErr1", "PortEn/PktErr0", "RxVlanEncap", "Port2", "Port1",
	    "Port0"};
	struct cpsw_cpdma_bd bd;
	const char *sep;
	int i;

	cpsw_cpdma_read_bd(sc, slot, &bd);
	printf("BD Addr : 0x%08x   Next  : 0x%08x\n",
	    cpsw_cpdma_bd_paddr(sc, slot), bd.next);
	printf("  BufPtr: 0x%08x   BufLen: 0x%08x\n", bd.bufptr, bd.buflen);
	printf("  BufOff: 0x%08x   PktLen: 0x%08x\n", bd.bufoff, bd.pktlen);
	printf("  Flags: ");
	sep = "";
	for (i = 0; i < 16; ++i) {
		if (bd.flags & (1 << (15 - i))) {
			printf("%s%s", sep, flags[i]);
			sep = ",";
		}
	}
	printf("\n");
	if (slot->mbuf) {
		printf("  Ether:  %14D\n",
		    (char *)(slot->mbuf->m_data), " ");
		printf("  Packet: %16D\n",
		    (char *)(slot->mbuf->m_data) + 14, " ");
	}
}

#define	CPSW_DUMP_SLOT(cs, slot) do {				\
	IF_DEBUG(sc) {						\
		cpsw_dump_slot(sc, slot);			\
	}							\
} while (0)

static void
cpsw_dump_queue(struct cpsw_softc *sc, struct cpsw_slots *q)
{
	struct cpsw_slot *slot;
	int i = 0;
	int others = 0;

	STAILQ_FOREACH(slot, q, next) {
		if (i > CPSW_TXFRAGS)
			++others;
		else
			cpsw_dump_slot(sc, slot);
		++i;
	}
	if (others)
		printf(" ... and %d more.\n", others);
	printf("\n");
}

#define CPSW_DUMP_QUEUE(sc, q) do {				\
	IF_DEBUG(sc) {						\
		cpsw_dump_queue(sc, q);				\
	}							\
} while (0)

static void
cpsw_init_slots(struct cpsw_softc *sc)
{
	struct cpsw_slot *slot;
	int i;

	STAILQ_INIT(&sc->avail);

	/* Put the slot descriptors onto the global avail list. */
	for (i = 0; i < nitems(sc->_slots); i++) {
		slot = &sc->_slots[i];
		slot->bd_offset = cpsw_cpdma_bd_offset(i);
		STAILQ_INSERT_TAIL(&sc->avail, slot, next);
	}
}

static int
cpsw_add_slots(struct cpsw_softc *sc, struct cpsw_queue *queue, int requested)
{
	const int max_slots = nitems(sc->_slots);
	struct cpsw_slot *slot;
	int i;

	if (requested < 0)
		requested = max_slots;

	for (i = 0; i < requested; ++i) {
		slot = STAILQ_FIRST(&sc->avail);
		if (slot == NULL)
			return (0);
		if (bus_dmamap_create(sc->mbuf_dtag, 0, &slot->dmamap)) {
			device_printf(sc->dev, "failed to create dmamap\n");
			return (ENOMEM);
		}
		STAILQ_REMOVE_HEAD(&sc->avail, next);
		STAILQ_INSERT_TAIL(&queue->avail, slot, next);
		++queue->avail_queue_len;
		++queue->queue_slots;
	}
	return (0);
}

static void
cpsw_free_slot(struct cpsw_softc *sc, struct cpsw_slot *slot)
{
	int error;

	if (slot->dmamap) {
		if (slot->mbuf)
			bus_dmamap_unload(sc->mbuf_dtag, slot->dmamap);
		error = bus_dmamap_destroy(sc->mbuf_dtag, slot->dmamap);
		KASSERT(error == 0, ("Mapping still active"));
		slot->dmamap = NULL;
	}
	if (slot->mbuf) {
		m_freem(slot->mbuf);
		slot->mbuf = NULL;
	}
}

static void
cpsw_reset(struct cpsw_softc *sc)
{
	int i;

	callout_stop(&sc->watchdog.callout);

	/* Reset RMII/RGMII wrapper. */
	cpsw_write_4(sc, CPSW_WR_SOFT_RESET, 1);
	while (cpsw_read_4(sc, CPSW_WR_SOFT_RESET) & 1)
		;

	/* Disable TX and RX interrupts for all cores. */
	for (i = 0; i < 3; ++i) {
		cpsw_write_4(sc, CPSW_WR_C_RX_THRESH_EN(i), 0x00);
		cpsw_write_4(sc, CPSW_WR_C_TX_EN(i), 0x00);
		cpsw_write_4(sc, CPSW_WR_C_RX_EN(i), 0x00);
		cpsw_write_4(sc, CPSW_WR_C_MISC_EN(i), 0x00);
	}

	/* Reset CPSW subsystem. */
	cpsw_write_4(sc, CPSW_SS_SOFT_RESET, 1);
	while (cpsw_read_4(sc, CPSW_SS_SOFT_RESET) & 1)
		;

	/* Reset Sliver port 1 and 2 */
	for (i = 0; i < 2; i++) {
		/* Reset */
		cpsw_write_4(sc, CPSW_SL_SOFT_RESET(i), 1);
		while (cpsw_read_4(sc, CPSW_SL_SOFT_RESET(i)) & 1)
			;
	}

	/* Reset DMA controller. */
	cpsw_write_4(sc, CPSW_CPDMA_SOFT_RESET, 1);
	while (cpsw_read_4(sc, CPSW_CPDMA_SOFT_RESET) & 1)
		;

	/* Disable TX & RX DMA */
	cpsw_write_4(sc, CPSW_CPDMA_TX_CONTROL, 0);
	cpsw_write_4(sc, CPSW_CPDMA_RX_CONTROL, 0);

	/* Clear all queues. */
	for (i = 0; i < 8; i++) {
		cpsw_write_4(sc, CPSW_CPDMA_TX_HDP(i), 0);
		cpsw_write_4(sc, CPSW_CPDMA_RX_HDP(i), 0);
		cpsw_write_4(sc, CPSW_CPDMA_TX_CP(i), 0);
		cpsw_write_4(sc, CPSW_CPDMA_RX_CP(i), 0);
	}

	/* Clear all interrupt Masks */
	cpsw_write_4(sc, CPSW_CPDMA_RX_INTMASK_CLEAR, 0xFFFFFFFF);
	cpsw_write_4(sc, CPSW_CPDMA_TX_INTMASK_CLEAR, 0xFFFFFFFF);
}

static void
cpsw_init(struct cpsw_softc *sc)
{
	struct cpsw_slot *slot;
	uint32_t reg;

	/* Disable the interrupt pacing. */
	reg = cpsw_read_4(sc, CPSW_WR_INT_CONTROL);
	reg &= ~(CPSW_WR_INT_PACE_EN | CPSW_WR_INT_PRESCALE_MASK);
	cpsw_write_4(sc, CPSW_WR_INT_CONTROL, reg);

	/* Clear ALE */
	cpsw_write_4(sc, CPSW_ALE_CONTROL, CPSW_ALE_CTL_CLEAR_TBL);

	/* Enable ALE */
	reg = CPSW_ALE_CTL_ENABLE;
	if (sc->dualemac)
		reg |= CPSW_ALE_CTL_VLAN_AWARE;
	cpsw_write_4(sc, CPSW_ALE_CONTROL, reg);

	/* Set Host Port Mapping. */
	cpsw_write_4(sc, CPSW_PORT_P0_CPDMA_TX_PRI_MAP, 0x76543210);
	cpsw_write_4(sc, CPSW_PORT_P0_CPDMA_RX_CH_MAP, 0);

	/* Initialize ALE: set host port to forwarding(3). */
	cpsw_write_4(sc, CPSW_ALE_PORTCTL(0),
	    ALE_PORTCTL_INGRESS | ALE_PORTCTL_FORWARD);

	cpsw_write_4(sc, CPSW_SS_PTYPE, 0);

	/* Enable statistics for ports 0, 1 and 2 */
	cpsw_write_4(sc, CPSW_SS_STAT_PORT_EN, 7);

	/* Turn off flow control. */
	cpsw_write_4(sc, CPSW_SS_FLOW_CONTROL, 0);

	/* Make IP hdr aligned with 4 */
	cpsw_write_4(sc, CPSW_CPDMA_RX_BUFFER_OFFSET, 2);

	/* Initialize RX Buffer Descriptors */
	cpsw_write_4(sc, CPSW_CPDMA_RX_PENDTHRESH(0), 0);
	cpsw_write_4(sc, CPSW_CPDMA_RX_FREEBUFFER(0), 0);

	/* Enable TX & RX DMA */
	cpsw_write_4(sc, CPSW_CPDMA_TX_CONTROL, 1);
	cpsw_write_4(sc, CPSW_CPDMA_RX_CONTROL, 1);

	/* Enable Interrupts for core 0 */
	cpsw_write_4(sc, CPSW_WR_C_RX_THRESH_EN(0), 0xFF);
	cpsw_write_4(sc, CPSW_WR_C_RX_EN(0), 0xFF);
	cpsw_write_4(sc, CPSW_WR_C_TX_EN(0), 0xFF);
	cpsw_write_4(sc, CPSW_WR_C_MISC_EN(0), 0x1F);

	/* Enable host Error Interrupt */
	cpsw_write_4(sc, CPSW_CPDMA_DMA_INTMASK_SET, 3);

	/* Enable interrupts for RX and TX on Channel 0 */
	cpsw_write_4(sc, CPSW_CPDMA_RX_INTMASK_SET,
	    CPSW_CPDMA_RX_INT(0) | CPSW_CPDMA_RX_INT_THRESH(0));
	cpsw_write_4(sc, CPSW_CPDMA_TX_INTMASK_SET, 1);

	/* Initialze MDIO - ENABLE, PREAMBLE=0, FAULTENB, CLKDIV=0xFF */
	/* TODO Calculate MDCLK=CLK/(CLKDIV+1) */
	cpsw_write_4(sc, MDIOCONTROL, MDIOCTL_ENABLE | MDIOCTL_FAULTENB | 0xff);

	/* Select MII in GMII_SEL, Internal Delay mode */
	//ti_scm_reg_write_4(0x650, 0);

	/* Initialize active queues. */
	slot = STAILQ_FIRST(&sc->tx.active);
	if (slot != NULL)
		cpsw_write_hdp_slot(sc, &sc->tx, slot);
	slot = STAILQ_FIRST(&sc->rx.active);
	if (slot != NULL)
		cpsw_write_hdp_slot(sc, &sc->rx, slot);
	cpsw_rx_enqueue(sc);
	cpsw_write_4(sc, CPSW_CPDMA_RX_FREEBUFFER(0), sc->rx.active_queue_len);
	cpsw_write_4(sc, CPSW_CPDMA_RX_PENDTHRESH(0), CPSW_TXFRAGS);

	/* Activate network interface. */
	sc->rx.running = 1;
	sc->tx.running = 1;
	sc->watchdog.timer = 0;
	callout_init(&sc->watchdog.callout, 0);
	callout_reset(&sc->watchdog.callout, hz, cpsw_tx_watchdog, sc);
}

/*
 *
 * Device Probe, Attach, Detach.
 *
 */

static int
cpsw_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,cpsw"))
		return (ENXIO);

	device_set_desc(dev, "3-port Switch Ethernet Subsystem");
	return (BUS_PROBE_DEFAULT);
}

static int
cpsw_intr_attach(struct cpsw_softc *sc)
{
	int i;

	for (i = 0; i < CPSW_INTR_COUNT; i++) {
		if (bus_setup_intr(sc->dev, sc->irq_res[i],
		    INTR_TYPE_NET | INTR_MPSAFE, NULL,
		    cpsw_intr_cb[i].cb, sc, &sc->ih_cookie[i]) != 0) {
			return (-1);
		}
	}

	return (0);
}

static void
cpsw_intr_detach(struct cpsw_softc *sc)
{
	int i;

	for (i = 0; i < CPSW_INTR_COUNT; i++) {
		if (sc->ih_cookie[i]) {
			bus_teardown_intr(sc->dev, sc->irq_res[i],
			    sc->ih_cookie[i]);
		}
	}
}

static int
cpsw_get_fdt_data(struct cpsw_softc *sc, int port)
{
	char *name;
	int len, phy, vlan;
	pcell_t phy_id[3], vlan_id;
	phandle_t child;
	unsigned long mdio_child_addr;

	/* Find any slave with phy_id */
	phy = -1;
	vlan = -1;
	for (child = OF_child(sc->node); child != 0; child = OF_peer(child)) {
		if (OF_getprop_alloc(child, "name", (void **)&name) < 0)
			continue;
		if (sscanf(name, "slave@%lx", &mdio_child_addr) != 1) {
			OF_prop_free(name);
			continue;
		}
		OF_prop_free(name);
		if (mdio_child_addr != slave_mdio_addr[port])
			continue;

		len = OF_getproplen(child, "phy_id");
		if (len / sizeof(pcell_t) == 2) {
			/* Get phy address from fdt */
			if (OF_getencprop(child, "phy_id", phy_id, len) > 0)
				phy = phy_id[1];
		}

		len = OF_getproplen(child, "dual_emac_res_vlan");
		if (len / sizeof(pcell_t) == 1) {
			/* Get phy address from fdt */
			if (OF_getencprop(child, "dual_emac_res_vlan",
			    &vlan_id, len) > 0) {
				vlan = vlan_id;
			}
		}

		break;
	}
	if (phy == -1)
		return (ENXIO);
	sc->port[port].phy = phy;
	sc->port[port].vlan = vlan;

	return (0);
}

static int
cpsw_attach(device_t dev)
{
	int error, i;
	struct cpsw_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->node = ofw_bus_get_node(dev);
	getbinuptime(&sc->attach_uptime);

	if (OF_getencprop(sc->node, "active_slave", &sc->active_slave,
	    sizeof(sc->active_slave)) <= 0) {
		sc->active_slave = 0;
	}
	if (sc->active_slave > 1)
		sc->active_slave = 1;

	if (OF_hasprop(sc->node, "dual_emac"))
		sc->dualemac = 1;

	for (i = 0; i < CPSW_PORTS; i++) {
		if (!sc->dualemac && i != sc->active_slave)
			continue;
		if (cpsw_get_fdt_data(sc, i) != 0) {
			device_printf(dev,
			    "failed to get PHY address from FDT\n");
			return (ENXIO);
		}
	}

	/* Initialize mutexes */
	mtx_init(&sc->tx.lock, device_get_nameunit(dev),
	    "cpsw TX lock", MTX_DEF);
	mtx_init(&sc->rx.lock, device_get_nameunit(dev),
	    "cpsw RX lock", MTX_DEF);

	/* Allocate IRQ resources */
	error = bus_alloc_resources(dev, irq_res_spec, sc->irq_res);
	if (error) {
		device_printf(dev, "could not allocate IRQ resources\n");
		cpsw_detach(dev);
		return (ENXIO);
	}

	sc->mem_rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 
	    &sc->mem_rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(sc->dev, "failed to allocate memory resource\n");
		cpsw_detach(dev);
		return (ENXIO);
	}

	reg = cpsw_read_4(sc, CPSW_SS_IDVER);
	device_printf(dev, "CPSW SS Version %d.%d (%d)\n", (reg >> 8 & 0x7),
		reg & 0xFF, (reg >> 11) & 0x1F);

	cpsw_add_sysctls(sc);

	/* Allocate a busdma tag and DMA safe memory for mbufs. */
	error = bus_dma_tag_create(
		bus_get_dma_tag(sc->dev),	/* parent */
		1, 0,				/* alignment, boundary */
		BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		BUS_SPACE_MAXADDR,		/* highaddr */
		NULL, NULL,			/* filtfunc, filtfuncarg */
		MCLBYTES, CPSW_TXFRAGS,		/* maxsize, nsegments */
		MCLBYTES, 0,			/* maxsegsz, flags */
		NULL, NULL,			/* lockfunc, lockfuncarg */
		&sc->mbuf_dtag);		/* dmatag */
	if (error) {
		device_printf(dev, "bus_dma_tag_create failed\n");
		cpsw_detach(dev);
		return (error);
	}

	/* Allocate a NULL buffer for padding. */
	sc->nullpad = malloc(ETHER_MIN_LEN, M_DEVBUF, M_WAITOK | M_ZERO);

	cpsw_init_slots(sc);

	/* Allocate slots to TX and RX queues. */
	STAILQ_INIT(&sc->rx.avail);
	STAILQ_INIT(&sc->rx.active);
	STAILQ_INIT(&sc->tx.avail);
	STAILQ_INIT(&sc->tx.active);
	// For now:  128 slots to TX, rest to RX.
	// XXX TODO: start with 32/64 and grow dynamically based on demand.
	if (cpsw_add_slots(sc, &sc->tx, 128) ||
	    cpsw_add_slots(sc, &sc->rx, -1)) {
		device_printf(dev, "failed to allocate dmamaps\n");
		cpsw_detach(dev);
		return (ENOMEM);
	}
	device_printf(dev, "Initial queue size TX=%d RX=%d\n",
	    sc->tx.queue_slots, sc->rx.queue_slots);

	sc->tx.hdp_offset = CPSW_CPDMA_TX_HDP(0);
	sc->rx.hdp_offset = CPSW_CPDMA_RX_HDP(0);

	if (cpsw_intr_attach(sc) == -1) {
		device_printf(dev, "failed to setup interrupts\n");
		cpsw_detach(dev);
		return (ENXIO);
	}

#ifdef CPSW_ETHERSWITCH
	for (i = 0; i < CPSW_VLANS; i++)
		cpsw_vgroups[i].vid = -1;
#endif

	/* Reset the controller. */
	cpsw_reset(sc);
	cpsw_init(sc);

	for (i = 0; i < CPSW_PORTS; i++) {
		if (!sc->dualemac && i != sc->active_slave)
			continue;
		sc->port[i].dev = device_add_child(dev, "cpsw", i);
		if (sc->port[i].dev == NULL) {
			cpsw_detach(dev);
			return (ENXIO);
		}
	}
	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return (0);
}

static int
cpsw_detach(device_t dev)
{
	struct cpsw_softc *sc;
	int error, i;

	bus_generic_detach(dev);
 	sc = device_get_softc(dev);

	for (i = 0; i < CPSW_PORTS; i++) {
		if (sc->port[i].dev)
			device_delete_child(dev, sc->port[i].dev);
	}

	if (device_is_attached(dev)) {
		callout_stop(&sc->watchdog.callout);
		callout_drain(&sc->watchdog.callout);
	}

	/* Stop and release all interrupts */
	cpsw_intr_detach(sc);

	/* Free dmamaps and mbufs */
	for (i = 0; i < nitems(sc->_slots); ++i)
		cpsw_free_slot(sc, &sc->_slots[i]);

	/* Free null padding buffer. */
	if (sc->nullpad)
		free(sc->nullpad, M_DEVBUF);

	/* Free DMA tag */
	if (sc->mbuf_dtag) {
		error = bus_dma_tag_destroy(sc->mbuf_dtag);
		KASSERT(error == 0, ("Unable to destroy DMA tag"));
	}

	/* Free IO memory handler */
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
	bus_release_resources(dev, irq_res_spec, sc->irq_res);

	/* Destroy mutexes */
	mtx_destroy(&sc->rx.lock);
	mtx_destroy(&sc->tx.lock);

	/* Detach the switch device, if present. */
	error = bus_generic_detach(dev);
	if (error != 0)
		return (error);
        
	return (device_delete_children(dev));
}

static phandle_t
cpsw_get_node(device_t bus, device_t dev)
{

	/* Share controller node with port device. */
	return (ofw_bus_get_node(bus));
}

static int
cpswp_probe(device_t dev)
{

	if (device_get_unit(dev) > 1) {
		device_printf(dev, "Only two ports are supported.\n");
		return (ENXIO);
	}
	device_set_desc(dev, "Ethernet Switch Port");

	return (BUS_PROBE_DEFAULT);
}

static int
cpswp_attach(device_t dev)
{
	int error;
	struct ifnet *ifp;
	struct cpswp_softc *sc;
	uint32_t reg;
	uint8_t mac_addr[ETHER_ADDR_LEN];

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->pdev = device_get_parent(dev);
	sc->swsc = device_get_softc(sc->pdev);
	sc->unit = device_get_unit(dev);
	sc->phy = sc->swsc->port[sc->unit].phy;
	sc->vlan = sc->swsc->port[sc->unit].vlan;
	if (sc->swsc->dualemac && sc->vlan == -1)
		sc->vlan = sc->unit + 1;

	if (sc->unit == 0) {
		sc->physel = MDIOUSERPHYSEL0;
		sc->phyaccess = MDIOUSERACCESS0;
	} else {
		sc->physel = MDIOUSERPHYSEL1;
		sc->phyaccess = MDIOUSERACCESS1;
	}

	mtx_init(&sc->lock, device_get_nameunit(dev), "cpsw port lock",
	    MTX_DEF);

	/* Allocate network interface */
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		cpswp_detach(dev);
		return (ENXIO);
	}

	if_initname(ifp, device_get_name(sc->dev), sc->unit);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_SIMPLEX | IFF_MULTICAST | IFF_BROADCAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_HWCSUM; //FIXME VLAN?
	ifp->if_capenable = ifp->if_capabilities;

	ifp->if_init = cpswp_init;
	ifp->if_start = cpswp_start;
	ifp->if_ioctl = cpswp_ioctl;

	ifp->if_snd.ifq_drv_maxlen = sc->swsc->tx.queue_slots;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);

	/* Get high part of MAC address from control module (mac_id[0|1]_hi) */
	ti_scm_reg_read_4(SCM_MAC_ID0_HI + sc->unit * 8, &reg);
	mac_addr[0] = reg & 0xFF;
	mac_addr[1] = (reg >>  8) & 0xFF;
	mac_addr[2] = (reg >> 16) & 0xFF;
	mac_addr[3] = (reg >> 24) & 0xFF;

	/* Get low part of MAC address from control module (mac_id[0|1]_lo) */
	ti_scm_reg_read_4(SCM_MAC_ID0_LO + sc->unit * 8, &reg);
	mac_addr[4] = reg & 0xFF;
	mac_addr[5] = (reg >>  8) & 0xFF;

	error = mii_attach(dev, &sc->miibus, ifp, cpswp_ifmedia_upd,
	    cpswp_ifmedia_sts, BMSR_DEFCAPMASK, sc->phy, MII_OFFSET_ANY, 0);
	if (error) {
		device_printf(dev, "attaching PHYs failed\n");
		cpswp_detach(dev);
		return (error);
	}
	sc->mii = device_get_softc(sc->miibus);

	/* Select PHY and enable interrupts */
	cpsw_write_4(sc->swsc, sc->physel,
	    MDIO_PHYSEL_LINKINTENB | (sc->phy & 0x1F));

	ether_ifattach(sc->ifp, mac_addr);
	callout_init(&sc->mii_callout, 0);

	return (0);
}

static int
cpswp_detach(device_t dev)
{
	struct cpswp_softc *sc;

	sc = device_get_softc(dev);
	CPSW_DEBUGF(sc->swsc, (""));
	if (device_is_attached(dev)) {
		ether_ifdetach(sc->ifp);
		CPSW_PORT_LOCK(sc);
		cpswp_stop_locked(sc);
		CPSW_PORT_UNLOCK(sc);
		callout_drain(&sc->mii_callout);
	}

	bus_generic_detach(dev);

	if_free(sc->ifp);
	mtx_destroy(&sc->lock);

	return (0);
}

/*
 *
 * Init/Shutdown.
 *
 */

static int
cpsw_ports_down(struct cpsw_softc *sc)
{
	struct cpswp_softc *psc;
	struct ifnet *ifp1, *ifp2;

	if (!sc->dualemac)
		return (1);
	psc = device_get_softc(sc->port[0].dev);
	ifp1 = psc->ifp;
	psc = device_get_softc(sc->port[1].dev);
	ifp2 = psc->ifp;
	if ((ifp1->if_flags & IFF_UP) == 0 && (ifp2->if_flags & IFF_UP) == 0)
		return (1);

	return (0);
}

static void
cpswp_init(void *arg)
{
	struct cpswp_softc *sc = arg;

	CPSW_DEBUGF(sc->swsc, (""));
	CPSW_PORT_LOCK(sc);
	cpswp_init_locked(arg);
	CPSW_PORT_UNLOCK(sc);
}

static void
cpswp_init_locked(void *arg)
{
#ifdef CPSW_ETHERSWITCH
	int i;
#endif
	struct cpswp_softc *sc = arg;
	struct ifnet *ifp;
	uint32_t reg;

	CPSW_DEBUGF(sc->swsc, (""));
	CPSW_PORT_LOCK_ASSERT(sc);
	ifp = sc->ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	getbinuptime(&sc->init_uptime);

	if (!sc->swsc->rx.running && !sc->swsc->tx.running) {
		/* Reset the controller. */
		cpsw_reset(sc->swsc);
		cpsw_init(sc->swsc);
	}

	/* Set Slave Mapping. */
	cpsw_write_4(sc->swsc, CPSW_SL_RX_PRI_MAP(sc->unit), 0x76543210);
	cpsw_write_4(sc->swsc, CPSW_PORT_P_TX_PRI_MAP(sc->unit + 1),
	    0x33221100);
	cpsw_write_4(sc->swsc, CPSW_SL_RX_MAXLEN(sc->unit), 0x5f2);
	/* Enable MAC RX/TX modules. */
	/* TODO: Docs claim that IFCTL_B and IFCTL_A do the same thing? */
	/* Huh?  Docs call bit 0 "Loopback" some places, "FullDuplex" others. */
	reg = cpsw_read_4(sc->swsc, CPSW_SL_MACCONTROL(sc->unit));
	reg |= CPSW_SL_MACTL_GMII_ENABLE;
	cpsw_write_4(sc->swsc, CPSW_SL_MACCONTROL(sc->unit), reg);

	/* Initialize ALE: set port to forwarding, initialize addrs */
	cpsw_write_4(sc->swsc, CPSW_ALE_PORTCTL(sc->unit + 1),
	    ALE_PORTCTL_INGRESS | ALE_PORTCTL_FORWARD);
	cpswp_ale_update_addresses(sc, 1);

	if (sc->swsc->dualemac) {
		/* Set Port VID. */
		cpsw_write_4(sc->swsc, CPSW_PORT_P_VLAN(sc->unit + 1),
		    sc->vlan & 0xfff);
		cpsw_ale_update_vlan_table(sc->swsc, sc->vlan,
		    (1 << (sc->unit + 1)) | (1 << 0), /* Member list */
		    (1 << (sc->unit + 1)) | (1 << 0), /* Untagged egress */
		    (1 << (sc->unit + 1)) | (1 << 0), 0); /* mcast reg flood */
#ifdef CPSW_ETHERSWITCH
		for (i = 0; i < CPSW_VLANS; i++) {
			if (cpsw_vgroups[i].vid != -1)
				continue;
			cpsw_vgroups[i].vid = sc->vlan;
			break;
		}
#endif
	}

	mii_mediachg(sc->mii);
	callout_reset(&sc->mii_callout, hz, cpswp_tick, sc);
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static int
cpsw_shutdown(device_t dev)
{
	struct cpsw_softc *sc;
	struct cpswp_softc *psc;
	int i;

 	sc = device_get_softc(dev);
	CPSW_DEBUGF(sc, (""));
	for (i = 0; i < CPSW_PORTS; i++) {
		if (!sc->dualemac && i != sc->active_slave)
			continue;
		psc = device_get_softc(sc->port[i].dev);
		CPSW_PORT_LOCK(psc);
		cpswp_stop_locked(psc);
		CPSW_PORT_UNLOCK(psc);
	}

	return (0);
}

static void
cpsw_rx_teardown(struct cpsw_softc *sc)
{
	int i = 0;

	CPSW_RX_LOCK(sc);
	CPSW_DEBUGF(sc, ("starting RX teardown"));
	sc->rx.teardown = 1;
	cpsw_write_4(sc, CPSW_CPDMA_RX_TEARDOWN, 0);
	CPSW_RX_UNLOCK(sc);
	while (sc->rx.running) {
		if (++i > 10) {
			device_printf(sc->dev,
			    "Unable to cleanly shutdown receiver\n");
			return;
		}
		DELAY(200);
	}
	if (!sc->rx.running)
		CPSW_DEBUGF(sc, ("finished RX teardown (%d retries)", i));
}

static void
cpsw_tx_teardown(struct cpsw_softc *sc)
{
	int i = 0;

	CPSW_TX_LOCK(sc);
	CPSW_DEBUGF(sc, ("starting TX teardown"));
	/* Start the TX queue teardown if queue is not empty. */
	if (STAILQ_FIRST(&sc->tx.active) != NULL)
		cpsw_write_4(sc, CPSW_CPDMA_TX_TEARDOWN, 0);
	else
		sc->tx.teardown = 1;
	cpsw_tx_dequeue(sc);
	while (sc->tx.running && ++i < 10) {
		DELAY(200);
		cpsw_tx_dequeue(sc);
	}
	if (sc->tx.running) {
		device_printf(sc->dev,
		    "Unable to cleanly shutdown transmitter\n");
	}
	CPSW_DEBUGF(sc,
	    ("finished TX teardown (%d retries, %d idle buffers)", i,
	     sc->tx.active_queue_len));
	CPSW_TX_UNLOCK(sc);
}

static void
cpswp_stop_locked(struct cpswp_softc *sc)
{
	struct ifnet *ifp;
	uint32_t reg;

	ifp = sc->ifp;
	CPSW_DEBUGF(sc->swsc, (""));
	CPSW_PORT_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	/* Disable interface */
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	ifp->if_drv_flags |= IFF_DRV_OACTIVE;

	/* Stop ticker */
	callout_stop(&sc->mii_callout);

	/* Tear down the RX/TX queues. */
	if (cpsw_ports_down(sc->swsc)) {
		cpsw_rx_teardown(sc->swsc);
		cpsw_tx_teardown(sc->swsc);
	}

	/* Stop MAC RX/TX modules. */
	reg = cpsw_read_4(sc->swsc, CPSW_SL_MACCONTROL(sc->unit));
	reg &= ~CPSW_SL_MACTL_GMII_ENABLE;
	cpsw_write_4(sc->swsc, CPSW_SL_MACCONTROL(sc->unit), reg);

	if (cpsw_ports_down(sc->swsc)) {
		/* Capture stats before we reset controller. */
		cpsw_stats_collect(sc->swsc);

		cpsw_reset(sc->swsc);
		cpsw_init(sc->swsc);
	}
}

/*
 *  Suspend/Resume.
 */

static int
cpsw_suspend(device_t dev)
{
	struct cpsw_softc *sc;
	struct cpswp_softc *psc;
	int i;

	sc = device_get_softc(dev);
	CPSW_DEBUGF(sc, (""));
	for (i = 0; i < CPSW_PORTS; i++) {
		if (!sc->dualemac && i != sc->active_slave)
			continue;
		psc = device_get_softc(sc->port[i].dev);
		CPSW_PORT_LOCK(psc);
		cpswp_stop_locked(psc);
		CPSW_PORT_UNLOCK(psc);
	}

	return (0);
}

static int
cpsw_resume(device_t dev)
{
	struct cpsw_softc *sc;

	sc  = device_get_softc(dev);
	CPSW_DEBUGF(sc, ("UNIMPLEMENTED"));

	return (0);
}

/*
 *
 *  IOCTL
 *
 */

static void
cpsw_set_promisc(struct cpswp_softc *sc, int set)
{
	uint32_t reg;

	/*
	 * Enabling promiscuous mode requires ALE_BYPASS to be enabled.
	 * That disables the ALE forwarding logic and causes every
	 * packet to be sent only to the host port.  In bypass mode,
	 * the ALE processes host port transmit packets the same as in
	 * normal mode.
	 */
	reg = cpsw_read_4(sc->swsc, CPSW_ALE_CONTROL);
	reg &= ~CPSW_ALE_CTL_BYPASS;
	if (set)
		reg |= CPSW_ALE_CTL_BYPASS;
	cpsw_write_4(sc->swsc, CPSW_ALE_CONTROL, reg);
}

static void
cpsw_set_allmulti(struct cpswp_softc *sc, int set)
{
	if (set) {
		printf("All-multicast mode unimplemented\n");
	}
}

static int
cpswp_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct cpswp_softc *sc;
	struct ifreq *ifr;
	int error;
	uint32_t changed;

	error = 0;
	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;

	switch (command) {
	case SIOCSIFCAP:
		changed = ifp->if_capenable ^ ifr->ifr_reqcap;
		if (changed & IFCAP_HWCSUM) {
			if ((ifr->ifr_reqcap & changed) & IFCAP_HWCSUM)
				ifp->if_capenable |= IFCAP_HWCSUM;
			else
				ifp->if_capenable &= ~IFCAP_HWCSUM;
		}
		error = 0;
		break;
	case SIOCSIFFLAGS:
		CPSW_PORT_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				changed = ifp->if_flags ^ sc->if_flags;
				CPSW_DEBUGF(sc->swsc,
				    ("SIOCSIFFLAGS: UP & RUNNING (changed=0x%x)",
				    changed));
				if (changed & IFF_PROMISC)
					cpsw_set_promisc(sc,
					    ifp->if_flags & IFF_PROMISC);
				if (changed & IFF_ALLMULTI)
					cpsw_set_allmulti(sc,
					    ifp->if_flags & IFF_ALLMULTI);
			} else {
				CPSW_DEBUGF(sc->swsc,
				    ("SIOCSIFFLAGS: starting up"));
				cpswp_init_locked(sc);
			}
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			CPSW_DEBUGF(sc->swsc, ("SIOCSIFFLAGS: shutting down"));
			cpswp_stop_locked(sc);
		}

		sc->if_flags = ifp->if_flags;
		CPSW_PORT_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
		cpswp_ale_update_addresses(sc, 0);
		break;
	case SIOCDELMULTI:
		/* Ugh.  DELMULTI doesn't provide the specific address
		   being removed, so the best we can do is remove
		   everything and rebuild it all. */
		cpswp_ale_update_addresses(sc, 1);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
	}
	return (error);
}

/*
 *
 * MIIBUS
 *
 */
static int
cpswp_miibus_ready(struct cpsw_softc *sc, uint32_t reg)
{
	uint32_t r, retries = CPSW_MIIBUS_RETRIES;

	while (--retries) {
		r = cpsw_read_4(sc, reg);
		if ((r & MDIO_PHYACCESS_GO) == 0)
			return (1);
		DELAY(CPSW_MIIBUS_DELAY);
	}

	return (0);
}

static int
cpswp_miibus_readreg(device_t dev, int phy, int reg)
{
	struct cpswp_softc *sc;
	uint32_t cmd, r;

	sc = device_get_softc(dev);
	if (!cpswp_miibus_ready(sc->swsc, sc->phyaccess)) {
		device_printf(dev, "MDIO not ready to read\n");
		return (0);
	}

	/* Set GO, reg, phy */
	cmd = MDIO_PHYACCESS_GO | (reg & 0x1F) << 21 | (phy & 0x1F) << 16;
	cpsw_write_4(sc->swsc, sc->phyaccess, cmd);

	if (!cpswp_miibus_ready(sc->swsc, sc->phyaccess)) {
		device_printf(dev, "MDIO timed out during read\n");
		return (0);
	}

	r = cpsw_read_4(sc->swsc, sc->phyaccess);
	if ((r & MDIO_PHYACCESS_ACK) == 0) {
		device_printf(dev, "Failed to read from PHY.\n");
		r = 0;
	}
	return (r & 0xFFFF);
}

static int
cpswp_miibus_writereg(device_t dev, int phy, int reg, int value)
{
	struct cpswp_softc *sc;
	uint32_t cmd;

	sc = device_get_softc(dev);
	if (!cpswp_miibus_ready(sc->swsc, sc->phyaccess)) {
		device_printf(dev, "MDIO not ready to write\n");
		return (0);
	}

	/* Set GO, WRITE, reg, phy, and value */
	cmd = MDIO_PHYACCESS_GO | MDIO_PHYACCESS_WRITE |
	    (reg & 0x1F) << 21 | (phy & 0x1F) << 16 | (value & 0xFFFF);
	cpsw_write_4(sc->swsc, sc->phyaccess, cmd);

	if (!cpswp_miibus_ready(sc->swsc, sc->phyaccess)) {
		device_printf(dev, "MDIO timed out during write\n");
		return (0);
	}

	return (0);
}

static void
cpswp_miibus_statchg(device_t dev)
{
	struct cpswp_softc *sc;
	uint32_t mac_control, reg;

	sc = device_get_softc(dev);
	CPSW_DEBUGF(sc->swsc, (""));

	reg = CPSW_SL_MACCONTROL(sc->unit);
	mac_control = cpsw_read_4(sc->swsc, reg);
	mac_control &= ~(CPSW_SL_MACTL_GIG | CPSW_SL_MACTL_IFCTL_A |
	    CPSW_SL_MACTL_IFCTL_B | CPSW_SL_MACTL_FULLDUPLEX);

	switch(IFM_SUBTYPE(sc->mii->mii_media_active)) {
	case IFM_1000_SX:
	case IFM_1000_LX:
	case IFM_1000_CX:
	case IFM_1000_T:
		mac_control |= CPSW_SL_MACTL_GIG;
		break;

	case IFM_100_TX:
		mac_control |= CPSW_SL_MACTL_IFCTL_A;
		break;
	}
	if (sc->mii->mii_media_active & IFM_FDX)
		mac_control |= CPSW_SL_MACTL_FULLDUPLEX;

	cpsw_write_4(sc->swsc, reg, mac_control);
}

/*
 *
 * Transmit/Receive Packets.
 *
 */
static void
cpsw_intr_rx(void *arg)
{
	struct cpsw_softc *sc;
	struct ifnet *ifp;
	struct mbuf *received, *next;

	sc = (struct cpsw_softc *)arg;
	CPSW_RX_LOCK(sc);
	if (sc->rx.teardown) {
		sc->rx.running = 0;
		sc->rx.teardown = 0;
		cpsw_write_cp(sc, &sc->rx, 0xfffffffc);
	}
	received = cpsw_rx_dequeue(sc);
	cpsw_rx_enqueue(sc);
	cpsw_write_4(sc, CPSW_CPDMA_CPDMA_EOI_VECTOR, 1);
	CPSW_RX_UNLOCK(sc);

	while (received != NULL) {
		next = received->m_nextpkt;
		received->m_nextpkt = NULL;
		ifp = received->m_pkthdr.rcvif;
		(*ifp->if_input)(ifp, received);
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		received = next;
	}
}

static struct mbuf *
cpsw_rx_dequeue(struct cpsw_softc *sc)
{
	int nsegs, port, removed;
	struct cpsw_cpdma_bd bd;
	struct cpsw_slot *last, *slot;
	struct cpswp_softc *psc;
	struct mbuf *m, *m0, *mb_head, *mb_tail;
	uint16_t m0_flags;

	nsegs = 0;
	m0 = NULL;
	last = NULL;
	mb_head = NULL;
	mb_tail = NULL;
	removed = 0;

	/* Pull completed packets off hardware RX queue. */
	while ((slot = STAILQ_FIRST(&sc->rx.active)) != NULL) {
		cpsw_cpdma_read_bd(sc, slot, &bd);

		/*
		 * Stop on packets still in use by hardware, but do not stop
		 * on packets with the teardown complete flag, they will be
		 * discarded later.
		 */
		if ((bd.flags & (CPDMA_BD_OWNER | CPDMA_BD_TDOWNCMPLT)) ==
		    CPDMA_BD_OWNER)
			break;

		last = slot;
		++removed;
		STAILQ_REMOVE_HEAD(&sc->rx.active, next);
		STAILQ_INSERT_TAIL(&sc->rx.avail, slot, next);

		bus_dmamap_sync(sc->mbuf_dtag, slot->dmamap, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->mbuf_dtag, slot->dmamap);

		m = slot->mbuf;
		slot->mbuf = NULL;

		if (bd.flags & CPDMA_BD_TDOWNCMPLT) {
			CPSW_DEBUGF(sc, ("RX teardown is complete"));
			m_freem(m);
			sc->rx.running = 0;
			sc->rx.teardown = 0;
			break;
		}

		port = (bd.flags & CPDMA_BD_PORT_MASK) - 1;
		KASSERT(port >= 0 && port <= 1,
		    ("patcket received with invalid port: %d", port));
		psc = device_get_softc(sc->port[port].dev);

		/* Set up mbuf */
		m->m_data += bd.bufoff;
		m->m_len = bd.buflen;
		if (bd.flags & CPDMA_BD_SOP) {
			m->m_pkthdr.len = bd.pktlen;
			m->m_pkthdr.rcvif = psc->ifp;
			m->m_flags |= M_PKTHDR;
			m0_flags = bd.flags;
			m0 = m;
		}
		nsegs++;
		m->m_next = NULL;
		m->m_nextpkt = NULL;
		if (bd.flags & CPDMA_BD_EOP && m0 != NULL) {
			if (m0_flags & CPDMA_BD_PASS_CRC)
				m_adj(m0, -ETHER_CRC_LEN);
			m0_flags = 0;
			m0 = NULL;
			if (nsegs > sc->rx.longest_chain)
				sc->rx.longest_chain = nsegs;
			nsegs = 0;
		}

		if ((psc->ifp->if_capenable & IFCAP_RXCSUM) != 0) {
			/* check for valid CRC by looking into pkt_err[5:4] */
			if ((bd.flags &
			    (CPDMA_BD_SOP | CPDMA_BD_PKT_ERR_MASK)) ==
			    CPDMA_BD_SOP) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		if (STAILQ_FIRST(&sc->rx.active) != NULL &&
		    (bd.flags & (CPDMA_BD_EOP | CPDMA_BD_EOQ)) ==
		    (CPDMA_BD_EOP | CPDMA_BD_EOQ)) {
			cpsw_write_hdp_slot(sc, &sc->rx,
			    STAILQ_FIRST(&sc->rx.active));
			sc->rx.queue_restart++;
		}

		/* Add mbuf to packet list to be returned. */
		if (mb_tail != NULL && (bd.flags & CPDMA_BD_SOP)) {
			mb_tail->m_nextpkt = m;
		} else if (mb_tail != NULL) {
			mb_tail->m_next = m;
		} else if (mb_tail == NULL && (bd.flags & CPDMA_BD_SOP) == 0) {
			if (bootverbose)
				printf(
				    "%s: %s: discanding fragment packet w/o header\n",
				    __func__, psc->ifp->if_xname);
			m_freem(m);
			continue;
		} else {
			mb_head = m;
		}
		mb_tail = m;
	}

	if (removed != 0) {
		cpsw_write_cp_slot(sc, &sc->rx, last);
		sc->rx.queue_removes += removed;
		sc->rx.avail_queue_len += removed;
		sc->rx.active_queue_len -= removed;
		if (sc->rx.avail_queue_len > sc->rx.max_avail_queue_len)
			sc->rx.max_avail_queue_len = sc->rx.avail_queue_len;
		CPSW_DEBUGF(sc, ("Removed %d received packet(s) from RX queue", removed));
	}

	return (mb_head);
}

static void
cpsw_rx_enqueue(struct cpsw_softc *sc)
{
	bus_dma_segment_t seg[1];
	struct cpsw_cpdma_bd bd;
	struct cpsw_slot *first_new_slot, *last_old_slot, *next, *slot;
	int error, nsegs, added = 0;

	/* Register new mbufs with hardware. */
	first_new_slot = NULL;
	last_old_slot = STAILQ_LAST(&sc->rx.active, cpsw_slot, next);
	while ((slot = STAILQ_FIRST(&sc->rx.avail)) != NULL) {
		if (first_new_slot == NULL)
			first_new_slot = slot;
		if (slot->mbuf == NULL) {
			slot->mbuf = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			if (slot->mbuf == NULL) {
				device_printf(sc->dev,
				    "Unable to fill RX queue\n");
				break;
			}
			slot->mbuf->m_len =
			    slot->mbuf->m_pkthdr.len =
			    slot->mbuf->m_ext.ext_size;
		}

		error = bus_dmamap_load_mbuf_sg(sc->mbuf_dtag, slot->dmamap,
		    slot->mbuf, seg, &nsegs, BUS_DMA_NOWAIT);

		KASSERT(nsegs == 1, ("More than one segment (nsegs=%d)", nsegs));
		KASSERT(error == 0, ("DMA error (error=%d)", error));
		if (error != 0 || nsegs != 1) {
			device_printf(sc->dev,
			    "%s: Can't prep RX buf for DMA (nsegs=%d, error=%d)\n",
			    __func__, nsegs, error);
			bus_dmamap_unload(sc->mbuf_dtag, slot->dmamap);
			m_freem(slot->mbuf);
			slot->mbuf = NULL;
			break;
		}

		bus_dmamap_sync(sc->mbuf_dtag, slot->dmamap, BUS_DMASYNC_PREREAD);

		/* Create and submit new rx descriptor. */
		if ((next = STAILQ_NEXT(slot, next)) != NULL)
			bd.next = cpsw_cpdma_bd_paddr(sc, next);
		else
			bd.next = 0;
		bd.bufptr = seg->ds_addr;
		bd.bufoff = 0;
		bd.buflen = MCLBYTES - 1;
		bd.pktlen = bd.buflen;
		bd.flags = CPDMA_BD_OWNER;
		cpsw_cpdma_write_bd(sc, slot, &bd);
		++added;

		STAILQ_REMOVE_HEAD(&sc->rx.avail, next);
		STAILQ_INSERT_TAIL(&sc->rx.active, slot, next);
	}

	if (added == 0 || first_new_slot == NULL)
		return;

	CPSW_DEBUGF(sc, ("Adding %d buffers to RX queue", added));

	/* Link new entries to hardware RX queue. */
	if (last_old_slot == NULL) {
		/* Start a fresh queue. */
		cpsw_write_hdp_slot(sc, &sc->rx, first_new_slot);
	} else {
		/* Add buffers to end of current queue. */
		cpsw_cpdma_write_bd_next(sc, last_old_slot, first_new_slot);
	}
	sc->rx.queue_adds += added;
	sc->rx.avail_queue_len -= added;
	sc->rx.active_queue_len += added;
	cpsw_write_4(sc, CPSW_CPDMA_RX_FREEBUFFER(0), added);
	if (sc->rx.active_queue_len > sc->rx.max_active_queue_len)
		sc->rx.max_active_queue_len = sc->rx.active_queue_len;
}

static void
cpswp_start(struct ifnet *ifp)
{
	struct cpswp_softc *sc;

	sc = ifp->if_softc;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    sc->swsc->tx.running == 0) {
		return;
	}
	CPSW_TX_LOCK(sc->swsc);
	cpswp_tx_enqueue(sc);
	cpsw_tx_dequeue(sc->swsc);
	CPSW_TX_UNLOCK(sc->swsc);
}

static void
cpsw_intr_tx(void *arg)
{
	struct cpsw_softc *sc;

	sc = (struct cpsw_softc *)arg;
	CPSW_TX_LOCK(sc);
	if (cpsw_read_4(sc, CPSW_CPDMA_TX_CP(0)) == 0xfffffffc)
		cpsw_write_cp(sc, &sc->tx, 0xfffffffc);
	cpsw_tx_dequeue(sc);
	cpsw_write_4(sc, CPSW_CPDMA_CPDMA_EOI_VECTOR, 2);
	CPSW_TX_UNLOCK(sc);
}

static void
cpswp_tx_enqueue(struct cpswp_softc *sc)
{
	bus_dma_segment_t segs[CPSW_TXFRAGS];
	struct cpsw_cpdma_bd bd;
	struct cpsw_slot *first_new_slot, *last, *last_old_slot, *next, *slot;
	struct mbuf *m0;
	int error, nsegs, seg, added = 0, padlen;

	/* Pull pending packets from IF queue and prep them for DMA. */
	last = NULL;
	first_new_slot = NULL;
	last_old_slot = STAILQ_LAST(&sc->swsc->tx.active, cpsw_slot, next);
	while ((slot = STAILQ_FIRST(&sc->swsc->tx.avail)) != NULL) {
		IF_DEQUEUE(&sc->ifp->if_snd, m0);
		if (m0 == NULL)
			break;

		slot->mbuf = m0;
		padlen = ETHER_MIN_LEN - ETHER_CRC_LEN - m0->m_pkthdr.len;
		if (padlen < 0)
			padlen = 0;
		else if (padlen > 0)
			m_append(slot->mbuf, padlen, sc->swsc->nullpad);

		/* Create mapping in DMA memory */
		error = bus_dmamap_load_mbuf_sg(sc->swsc->mbuf_dtag,
		    slot->dmamap, slot->mbuf, segs, &nsegs, BUS_DMA_NOWAIT);
		/* If the packet is too fragmented, try to simplify. */
		if (error == EFBIG ||
		    (error == 0 && nsegs > sc->swsc->tx.avail_queue_len)) {
			bus_dmamap_unload(sc->swsc->mbuf_dtag, slot->dmamap);
			m0 = m_defrag(slot->mbuf, M_NOWAIT);
			if (m0 == NULL) {
				device_printf(sc->dev,
				    "Can't defragment packet; dropping\n");
				m_freem(slot->mbuf);
			} else {
				CPSW_DEBUGF(sc->swsc,
				    ("Requeueing defragmented packet"));
				IF_PREPEND(&sc->ifp->if_snd, m0);
			}
			slot->mbuf = NULL;
			continue;
		}
		if (error != 0) {
			device_printf(sc->dev,
			    "%s: Can't setup DMA (error=%d), dropping packet\n",
			    __func__, error);
			bus_dmamap_unload(sc->swsc->mbuf_dtag, slot->dmamap);
			m_freem(slot->mbuf);
			slot->mbuf = NULL;
			break;
		}

		bus_dmamap_sync(sc->swsc->mbuf_dtag, slot->dmamap,
				BUS_DMASYNC_PREWRITE);

		CPSW_DEBUGF(sc->swsc,
		    ("Queueing TX packet: %d segments + %d pad bytes",
		    nsegs, padlen));

		if (first_new_slot == NULL)
			first_new_slot = slot;

		/* Link from the previous descriptor. */
		if (last != NULL)
			cpsw_cpdma_write_bd_next(sc->swsc, last, slot);

		slot->ifp = sc->ifp;

		/* If there is only one segment, the for() loop
		 * gets skipped and the single buffer gets set up
		 * as both SOP and EOP. */
		if (nsegs > 1) {
			next = STAILQ_NEXT(slot, next);
			bd.next = cpsw_cpdma_bd_paddr(sc->swsc, next);
		} else
			bd.next = 0;
		/* Start by setting up the first buffer. */
		bd.bufptr = segs[0].ds_addr;
		bd.bufoff = 0;
		bd.buflen = segs[0].ds_len;
		bd.pktlen = m_length(slot->mbuf, NULL);
		bd.flags =  CPDMA_BD_SOP | CPDMA_BD_OWNER;
		if (sc->swsc->dualemac) {
			bd.flags |= CPDMA_BD_TO_PORT;
			bd.flags |= ((sc->unit + 1) & CPDMA_BD_PORT_MASK);
		}
		for (seg = 1; seg < nsegs; ++seg) {
			/* Save the previous buffer (which isn't EOP) */
			cpsw_cpdma_write_bd(sc->swsc, slot, &bd);
			STAILQ_REMOVE_HEAD(&sc->swsc->tx.avail, next);
			STAILQ_INSERT_TAIL(&sc->swsc->tx.active, slot, next);
			slot = STAILQ_FIRST(&sc->swsc->tx.avail);

			/* Setup next buffer (which isn't SOP) */
			if (nsegs > seg + 1) {
				next = STAILQ_NEXT(slot, next);
				bd.next = cpsw_cpdma_bd_paddr(sc->swsc, next);
			} else
				bd.next = 0;
			bd.bufptr = segs[seg].ds_addr;
			bd.bufoff = 0;
			bd.buflen = segs[seg].ds_len;
			bd.pktlen = 0;
			bd.flags = CPDMA_BD_OWNER;
		}

		/* Save the final buffer. */
		bd.flags |= CPDMA_BD_EOP;
		cpsw_cpdma_write_bd(sc->swsc, slot, &bd);
		STAILQ_REMOVE_HEAD(&sc->swsc->tx.avail, next);
		STAILQ_INSERT_TAIL(&sc->swsc->tx.active, slot, next);

		last = slot;
		added += nsegs;
		if (nsegs > sc->swsc->tx.longest_chain)
			sc->swsc->tx.longest_chain = nsegs;

		BPF_MTAP(sc->ifp, m0);
	}

	if (first_new_slot == NULL)
		return;

	/* Attach the list of new buffers to the hardware TX queue. */
	if (last_old_slot != NULL &&
	    (cpsw_cpdma_read_bd_flags(sc->swsc, last_old_slot) &
	     CPDMA_BD_EOQ) == 0) {
		/* Add buffers to end of current queue. */
		cpsw_cpdma_write_bd_next(sc->swsc, last_old_slot,
		    first_new_slot);
	} else {
		/* Start a fresh queue. */
		cpsw_write_hdp_slot(sc->swsc, &sc->swsc->tx, first_new_slot);
	}
	sc->swsc->tx.queue_adds += added;
	sc->swsc->tx.avail_queue_len -= added;
	sc->swsc->tx.active_queue_len += added;
	if (sc->swsc->tx.active_queue_len > sc->swsc->tx.max_active_queue_len) {
		sc->swsc->tx.max_active_queue_len = sc->swsc->tx.active_queue_len;
	}
	CPSW_DEBUGF(sc->swsc, ("Queued %d TX packet(s)", added));
}

static int
cpsw_tx_dequeue(struct cpsw_softc *sc)
{
	struct cpsw_slot *slot, *last_removed_slot = NULL;
	struct cpsw_cpdma_bd bd;
	uint32_t flags, removed = 0;

	/* Pull completed buffers off the hardware TX queue. */
	slot = STAILQ_FIRST(&sc->tx.active);
	while (slot != NULL) {
		flags = cpsw_cpdma_read_bd_flags(sc, slot);

		/* TearDown complete is only marked on the SOP for the packet. */
		if ((flags & (CPDMA_BD_SOP | CPDMA_BD_TDOWNCMPLT)) ==
		    (CPDMA_BD_SOP | CPDMA_BD_TDOWNCMPLT)) {
			sc->tx.teardown = 1;
		}

		if ((flags & (CPDMA_BD_SOP | CPDMA_BD_OWNER)) ==
		    (CPDMA_BD_SOP | CPDMA_BD_OWNER) && sc->tx.teardown == 0)
			break; /* Hardware is still using this packet. */

		bus_dmamap_sync(sc->mbuf_dtag, slot->dmamap, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->mbuf_dtag, slot->dmamap);
		m_freem(slot->mbuf);
		slot->mbuf = NULL;

		if (slot->ifp) {
			if (sc->tx.teardown == 0)
				if_inc_counter(slot->ifp, IFCOUNTER_OPACKETS, 1);
			else
				if_inc_counter(slot->ifp, IFCOUNTER_OQDROPS, 1);
		}

		/* Dequeue any additional buffers used by this packet. */
		while (slot != NULL && slot->mbuf == NULL) {
			STAILQ_REMOVE_HEAD(&sc->tx.active, next);
			STAILQ_INSERT_TAIL(&sc->tx.avail, slot, next);
			++removed;
			last_removed_slot = slot;
			slot = STAILQ_FIRST(&sc->tx.active);
		}

		cpsw_write_cp_slot(sc, &sc->tx, last_removed_slot);

		/* Restart the TX queue if necessary. */
		cpsw_cpdma_read_bd(sc, last_removed_slot, &bd);
		if (slot != NULL && bd.next != 0 && (bd.flags &
		    (CPDMA_BD_EOP | CPDMA_BD_OWNER | CPDMA_BD_EOQ)) ==
		    (CPDMA_BD_EOP | CPDMA_BD_EOQ)) {
			cpsw_write_hdp_slot(sc, &sc->tx, slot);
			sc->tx.queue_restart++;
			break;
		}
	}

	if (removed != 0) {
		sc->tx.queue_removes += removed;
		sc->tx.active_queue_len -= removed;
		sc->tx.avail_queue_len += removed;
		if (sc->tx.avail_queue_len > sc->tx.max_avail_queue_len)
			sc->tx.max_avail_queue_len = sc->tx.avail_queue_len;
		CPSW_DEBUGF(sc, ("TX removed %d completed packet(s)", removed));
	}

	if (sc->tx.teardown && STAILQ_EMPTY(&sc->tx.active)) {
		CPSW_DEBUGF(sc, ("TX teardown is complete"));
		sc->tx.teardown = 0;
		sc->tx.running = 0;
	}

	return (removed);
}

/*
 *
 * Miscellaneous interrupts.
 *
 */

static void
cpsw_intr_rx_thresh(void *arg)
{
	struct cpsw_softc *sc;
	struct ifnet *ifp;
	struct mbuf *received, *next;

	sc = (struct cpsw_softc *)arg;
	CPSW_RX_LOCK(sc);
	received = cpsw_rx_dequeue(sc);
	cpsw_rx_enqueue(sc);
	cpsw_write_4(sc, CPSW_CPDMA_CPDMA_EOI_VECTOR, 0);
	CPSW_RX_UNLOCK(sc);

	while (received != NULL) {
		next = received->m_nextpkt;
		received->m_nextpkt = NULL;
		ifp = received->m_pkthdr.rcvif;
		(*ifp->if_input)(ifp, received);
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		received = next;
	}
}

static void
cpsw_intr_misc_host_error(struct cpsw_softc *sc)
{
	uint32_t intstat;
	uint32_t dmastat;
	int txerr, rxerr, txchan, rxchan;

	printf("\n\n");
	device_printf(sc->dev,
	    "HOST ERROR:  PROGRAMMING ERROR DETECTED BY HARDWARE\n");
	printf("\n\n");
	intstat = cpsw_read_4(sc, CPSW_CPDMA_DMA_INTSTAT_MASKED);
	device_printf(sc->dev, "CPSW_CPDMA_DMA_INTSTAT_MASKED=0x%x\n", intstat);
	dmastat = cpsw_read_4(sc, CPSW_CPDMA_DMASTATUS);
	device_printf(sc->dev, "CPSW_CPDMA_DMASTATUS=0x%x\n", dmastat);

	txerr = (dmastat >> 20) & 15;
	txchan = (dmastat >> 16) & 7;
	rxerr = (dmastat >> 12) & 15;
	rxchan = (dmastat >> 8) & 7;

	switch (txerr) {
	case 0: break;
	case 1:	printf("SOP error on TX channel %d\n", txchan);
		break;
	case 2:	printf("Ownership bit not set on SOP buffer on TX channel %d\n", txchan);
		break;
	case 3:	printf("Zero Next Buffer but not EOP on TX channel %d\n", txchan);
		break;
	case 4:	printf("Zero Buffer Pointer on TX channel %d\n", txchan);
		break;
	case 5:	printf("Zero Buffer Length on TX channel %d\n", txchan);
		break;
	case 6:	printf("Packet length error on TX channel %d\n", txchan);
		break;
	default: printf("Unknown error on TX channel %d\n", txchan);
		break;
	}

	if (txerr != 0) {
		printf("CPSW_CPDMA_TX%d_HDP=0x%x\n",
		    txchan, cpsw_read_4(sc, CPSW_CPDMA_TX_HDP(txchan)));
		printf("CPSW_CPDMA_TX%d_CP=0x%x\n",
		    txchan, cpsw_read_4(sc, CPSW_CPDMA_TX_CP(txchan)));
		cpsw_dump_queue(sc, &sc->tx.active);
	}

	switch (rxerr) {
	case 0: break;
	case 2:	printf("Ownership bit not set on RX channel %d\n", rxchan);
		break;
	case 4:	printf("Zero Buffer Pointer on RX channel %d\n", rxchan);
		break;
	case 5:	printf("Zero Buffer Length on RX channel %d\n", rxchan);
		break;
	case 6:	printf("Buffer offset too big on RX channel %d\n", rxchan);
		break;
	default: printf("Unknown RX error on RX channel %d\n", rxchan);
		break;
	}

	if (rxerr != 0) {
		printf("CPSW_CPDMA_RX%d_HDP=0x%x\n",
		    rxchan, cpsw_read_4(sc,CPSW_CPDMA_RX_HDP(rxchan)));
		printf("CPSW_CPDMA_RX%d_CP=0x%x\n",
		    rxchan, cpsw_read_4(sc, CPSW_CPDMA_RX_CP(rxchan)));
		cpsw_dump_queue(sc, &sc->rx.active);
	}

	printf("\nALE Table\n");
	cpsw_ale_dump_table(sc);

	// XXX do something useful here??
	panic("CPSW HOST ERROR INTERRUPT");

	// Suppress this interrupt in the future.
	cpsw_write_4(sc, CPSW_CPDMA_DMA_INTMASK_CLEAR, intstat);
	printf("XXX HOST ERROR INTERRUPT SUPPRESSED\n");
	// The watchdog will probably reset the controller
	// in a little while.  It will probably fail again.
}

static void
cpsw_intr_misc(void *arg)
{
	struct cpsw_softc *sc = arg;
	uint32_t stat = cpsw_read_4(sc, CPSW_WR_C_MISC_STAT(0));

	if (stat & CPSW_WR_C_MISC_EVNT_PEND)
		CPSW_DEBUGF(sc, ("Time sync event interrupt unimplemented"));
	if (stat & CPSW_WR_C_MISC_STAT_PEND)
		cpsw_stats_collect(sc);
	if (stat & CPSW_WR_C_MISC_HOST_PEND)
		cpsw_intr_misc_host_error(sc);
	if (stat & CPSW_WR_C_MISC_MDIOLINK) {
		cpsw_write_4(sc, MDIOLINKINTMASKED,
		    cpsw_read_4(sc, MDIOLINKINTMASKED));
	}
	if (stat & CPSW_WR_C_MISC_MDIOUSER) {
		CPSW_DEBUGF(sc,
		    ("MDIO operation completed interrupt unimplemented"));
	}
	cpsw_write_4(sc, CPSW_CPDMA_CPDMA_EOI_VECTOR, 3);
}

/*
 *
 * Periodic Checks and Watchdog.
 *
 */

static void
cpswp_tick(void *msc)
{
	struct cpswp_softc *sc = msc;

	/* Check for media type change */
	mii_tick(sc->mii);
	if (sc->media_status != sc->mii->mii_media.ifm_media) {
		printf("%s: media type changed (ifm_media=%x)\n", __func__, 
			sc->mii->mii_media.ifm_media);
		cpswp_ifmedia_upd(sc->ifp);
	}

	/* Schedule another timeout one second from now */
	callout_reset(&sc->mii_callout, hz, cpswp_tick, sc);
}

static void
cpswp_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct cpswp_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	CPSW_DEBUGF(sc->swsc, (""));
	CPSW_PORT_LOCK(sc);

	mii = sc->mii;
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	CPSW_PORT_UNLOCK(sc);
}

static int
cpswp_ifmedia_upd(struct ifnet *ifp)
{
	struct cpswp_softc *sc;

	sc = ifp->if_softc;
	CPSW_DEBUGF(sc->swsc, (""));
	CPSW_PORT_LOCK(sc);
	mii_mediachg(sc->mii);
	sc->media_status = sc->mii->mii_media.ifm_media;
	CPSW_PORT_UNLOCK(sc);

	return (0);
}

static void
cpsw_tx_watchdog_full_reset(struct cpsw_softc *sc)
{
	struct cpswp_softc *psc;
	int i;

	cpsw_debugf_head("CPSW watchdog");
	device_printf(sc->dev, "watchdog timeout\n");
	printf("CPSW_CPDMA_TX%d_HDP=0x%x\n", 0,
	    cpsw_read_4(sc, CPSW_CPDMA_TX_HDP(0)));
	printf("CPSW_CPDMA_TX%d_CP=0x%x\n", 0,
	    cpsw_read_4(sc, CPSW_CPDMA_TX_CP(0)));
	cpsw_dump_queue(sc, &sc->tx.active);
	for (i = 0; i < CPSW_PORTS; i++) {
		if (!sc->dualemac && i != sc->active_slave)
			continue;
		psc = device_get_softc(sc->port[i].dev);
		CPSW_PORT_LOCK(psc);
		cpswp_stop_locked(psc);
		CPSW_PORT_UNLOCK(psc);
	}
}

static void
cpsw_tx_watchdog(void *msc)
{
	struct cpsw_softc *sc;

	sc = msc;
	CPSW_TX_LOCK(sc);
	if (sc->tx.active_queue_len == 0 || !sc->tx.running) {
		sc->watchdog.timer = 0; /* Nothing to do. */
	} else if (sc->tx.queue_removes > sc->tx.queue_removes_at_last_tick) {
		sc->watchdog.timer = 0;  /* Stuff done while we weren't looking. */
	} else if (cpsw_tx_dequeue(sc) > 0) {
		sc->watchdog.timer = 0;  /* We just did something. */
	} else {
		/* There was something to do but it didn't get done. */
		++sc->watchdog.timer;
		if (sc->watchdog.timer > 5) {
			sc->watchdog.timer = 0;
			++sc->watchdog.resets;
			cpsw_tx_watchdog_full_reset(sc);
		}
	}
	sc->tx.queue_removes_at_last_tick = sc->tx.queue_removes;
	CPSW_TX_UNLOCK(sc);

	/* Schedule another timeout one second from now */
	callout_reset(&sc->watchdog.callout, hz, cpsw_tx_watchdog, sc);
}

/*
 *
 * ALE support routines.
 *
 */

static void
cpsw_ale_read_entry(struct cpsw_softc *sc, uint16_t idx, uint32_t *ale_entry)
{
	cpsw_write_4(sc, CPSW_ALE_TBLCTL, idx & 1023);
	ale_entry[0] = cpsw_read_4(sc, CPSW_ALE_TBLW0);
	ale_entry[1] = cpsw_read_4(sc, CPSW_ALE_TBLW1);
	ale_entry[2] = cpsw_read_4(sc, CPSW_ALE_TBLW2);
}

static void
cpsw_ale_write_entry(struct cpsw_softc *sc, uint16_t idx, uint32_t *ale_entry)
{
	cpsw_write_4(sc, CPSW_ALE_TBLW0, ale_entry[0]);
	cpsw_write_4(sc, CPSW_ALE_TBLW1, ale_entry[1]);
	cpsw_write_4(sc, CPSW_ALE_TBLW2, ale_entry[2]);
	cpsw_write_4(sc, CPSW_ALE_TBLCTL, 1 << 31 | (idx & 1023));
}

static void
cpsw_ale_remove_all_mc_entries(struct cpsw_softc *sc)
{
	int i;
	uint32_t ale_entry[3];

	/* First four entries are link address and broadcast. */
	for (i = 10; i < CPSW_MAX_ALE_ENTRIES; i++) {
		cpsw_ale_read_entry(sc, i, ale_entry);
		if ((ALE_TYPE(ale_entry) == ALE_TYPE_ADDR ||
		    ALE_TYPE(ale_entry) == ALE_TYPE_VLAN_ADDR) &&
		    ALE_MCAST(ale_entry)  == 1) { /* MCast link addr */
			ale_entry[0] = ale_entry[1] = ale_entry[2] = 0;
			cpsw_ale_write_entry(sc, i, ale_entry);
		}
	}
}

static int
cpsw_ale_mc_entry_set(struct cpsw_softc *sc, uint8_t portmap, int vlan,
	uint8_t *mac)
{
	int free_index = -1, matching_index = -1, i;
	uint32_t ale_entry[3], ale_type;

	/* Find a matching entry or a free entry. */
	for (i = 10; i < CPSW_MAX_ALE_ENTRIES; i++) {
		cpsw_ale_read_entry(sc, i, ale_entry);

		/* Entry Type[61:60] is 0 for free entry */ 
		if (free_index < 0 && ALE_TYPE(ale_entry) == 0)
			free_index = i;

		if ((((ale_entry[1] >> 8) & 0xFF) == mac[0]) &&
		    (((ale_entry[1] >> 0) & 0xFF) == mac[1]) &&
		    (((ale_entry[0] >>24) & 0xFF) == mac[2]) &&
		    (((ale_entry[0] >>16) & 0xFF) == mac[3]) &&
		    (((ale_entry[0] >> 8) & 0xFF) == mac[4]) &&
		    (((ale_entry[0] >> 0) & 0xFF) == mac[5])) {
			matching_index = i;
			break;
		}
	}

	if (matching_index < 0) {
		if (free_index < 0)
			return (ENOMEM);
		i = free_index;
	}

	if (vlan != -1)
		ale_type = ALE_TYPE_VLAN_ADDR << 28 | vlan << 16;
	else
		ale_type = ALE_TYPE_ADDR << 28;

	/* Set MAC address */
	ale_entry[0] = mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5];
	ale_entry[1] = mac[0] << 8 | mac[1];

	/* Entry type[61:60] and Mcast fwd state[63:62] is fw(3). */
	ale_entry[1] |= ALE_MCAST_FWD | ale_type;

	/* Set portmask [68:66] */
	ale_entry[2] = (portmap & 7) << 2;

	cpsw_ale_write_entry(sc, i, ale_entry);

	return 0;
}

static void
cpsw_ale_dump_table(struct cpsw_softc *sc) {
	int i;
	uint32_t ale_entry[3];
	for (i = 0; i < CPSW_MAX_ALE_ENTRIES; i++) {
		cpsw_ale_read_entry(sc, i, ale_entry);
		switch (ALE_TYPE(ale_entry)) {
		case ALE_TYPE_VLAN:
			printf("ALE[%4u] %08x %08x %08x ", i, ale_entry[2],
				ale_entry[1], ale_entry[0]);
			printf("type: %u ", ALE_TYPE(ale_entry));
			printf("vlan: %u ", ALE_VLAN(ale_entry));
			printf("untag: %u ", ALE_VLAN_UNTAG(ale_entry));
			printf("reg flood: %u ", ALE_VLAN_REGFLOOD(ale_entry));
			printf("unreg flood: %u ", ALE_VLAN_UNREGFLOOD(ale_entry));
			printf("members: %u ", ALE_VLAN_MEMBERS(ale_entry));
			printf("\n");
			break;
		case ALE_TYPE_ADDR:
		case ALE_TYPE_VLAN_ADDR:
			printf("ALE[%4u] %08x %08x %08x ", i, ale_entry[2],
				ale_entry[1], ale_entry[0]);
			printf("type: %u ", ALE_TYPE(ale_entry));
			printf("mac: %02x:%02x:%02x:%02x:%02x:%02x ",
				(ale_entry[1] >> 8) & 0xFF,
				(ale_entry[1] >> 0) & 0xFF,
				(ale_entry[0] >>24) & 0xFF,
				(ale_entry[0] >>16) & 0xFF,
				(ale_entry[0] >> 8) & 0xFF,
				(ale_entry[0] >> 0) & 0xFF);
			printf(ALE_MCAST(ale_entry) ? "mcast " : "ucast ");
			if (ALE_TYPE(ale_entry) == ALE_TYPE_VLAN_ADDR)
				printf("vlan: %u ", ALE_VLAN(ale_entry));
			printf("port: %u ", ALE_PORTS(ale_entry));
			printf("\n");
			break;
		}
	}
	printf("\n");
}

static int
cpswp_ale_update_addresses(struct cpswp_softc *sc, int purge)
{
	uint8_t *mac;
	uint32_t ale_entry[3], ale_type, portmask;
	struct ifmultiaddr *ifma;

	if (sc->swsc->dualemac) {
		ale_type = ALE_TYPE_VLAN_ADDR << 28 | sc->vlan << 16;
		portmask = 1 << (sc->unit + 1) | 1 << 0;
	} else {
		ale_type = ALE_TYPE_ADDR << 28;
		portmask = 7;
	}

	/*
	 * Route incoming packets for our MAC address to Port 0 (host).
	 * For simplicity, keep this entry at table index 0 for port 1 and
	 * at index 2 for port 2 in the ALE.
	 */
        if_addr_rlock(sc->ifp);
	mac = LLADDR((struct sockaddr_dl *)sc->ifp->if_addr->ifa_addr);
	ale_entry[0] = mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5];
	ale_entry[1] = ale_type | mac[0] << 8 | mac[1]; /* addr entry + mac */
	ale_entry[2] = 0; /* port = 0 */
	cpsw_ale_write_entry(sc->swsc, 0 + 2 * sc->unit, ale_entry);

	/* Set outgoing MAC Address for slave port. */
	cpsw_write_4(sc->swsc, CPSW_PORT_P_SA_HI(sc->unit + 1),
	    mac[3] << 24 | mac[2] << 16 | mac[1] << 8 | mac[0]);
	cpsw_write_4(sc->swsc, CPSW_PORT_P_SA_LO(sc->unit + 1),
	    mac[5] << 8 | mac[4]);
        if_addr_runlock(sc->ifp);

	/* Keep the broadcast address at table entry 1 (or 3). */
	ale_entry[0] = 0xffffffff; /* Lower 32 bits of MAC */
	/* ALE_MCAST_FWD, Addr type, upper 16 bits of Mac */ 
	ale_entry[1] = ALE_MCAST_FWD | ale_type | 0xffff;
	ale_entry[2] = portmask << 2;
	cpsw_ale_write_entry(sc->swsc, 1 + 2 * sc->unit, ale_entry);

	/* SIOCDELMULTI doesn't specify the particular address
	   being removed, so we have to remove all and rebuild. */
	if (purge)
		cpsw_ale_remove_all_mc_entries(sc->swsc);

        /* Set other multicast addrs desired. */
        if_maddr_rlock(sc->ifp);
        CK_STAILQ_FOREACH(ifma, &sc->ifp->if_multiaddrs, ifma_link) {
                if (ifma->ifma_addr->sa_family != AF_LINK)
                        continue;
		cpsw_ale_mc_entry_set(sc->swsc, portmask, sc->vlan,
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
        }
        if_maddr_runlock(sc->ifp);

	return (0);
}

static int
cpsw_ale_update_vlan_table(struct cpsw_softc *sc, int vlan, int ports,
	int untag, int mcregflood, int mcunregflood)
{
	int free_index, i, matching_index;
	uint32_t ale_entry[3];

	free_index = matching_index = -1;
	/* Find a matching entry or a free entry. */
	for (i = 5; i < CPSW_MAX_ALE_ENTRIES; i++) {
		cpsw_ale_read_entry(sc, i, ale_entry);

		/* Entry Type[61:60] is 0 for free entry */ 
		if (free_index < 0 && ALE_TYPE(ale_entry) == 0)
			free_index = i;

		if (ALE_VLAN(ale_entry) == vlan) {
			matching_index = i;
			break;
		}
	}

	if (matching_index < 0) {
		if (free_index < 0)
			return (-1);
		i = free_index;
	}

	ale_entry[0] = (untag & 7) << 24 | (mcregflood & 7) << 16 |
	    (mcunregflood & 7) << 8 | (ports & 7);
	ale_entry[1] = ALE_TYPE_VLAN << 28 | vlan << 16;
	ale_entry[2] = 0;
	cpsw_ale_write_entry(sc, i, ale_entry);

	return (0);
}

/*
 *
 * Statistics and Sysctls.
 *
 */

#if 0
static void
cpsw_stats_dump(struct cpsw_softc *sc)
{
	int i;
	uint32_t r;

	for (i = 0; i < CPSW_SYSCTL_COUNT; ++i) {
		r = cpsw_read_4(sc, CPSW_STATS_OFFSET +
		    cpsw_stat_sysctls[i].reg);
		CPSW_DEBUGF(sc, ("%s: %ju + %u = %ju", cpsw_stat_sysctls[i].oid,
		    (intmax_t)sc->shadow_stats[i], r,
		    (intmax_t)sc->shadow_stats[i] + r));
	}
}
#endif

static void
cpsw_stats_collect(struct cpsw_softc *sc)
{
	int i;
	uint32_t r;

	CPSW_DEBUGF(sc, ("Controller shadow statistics updated."));

	for (i = 0; i < CPSW_SYSCTL_COUNT; ++i) {
		r = cpsw_read_4(sc, CPSW_STATS_OFFSET +
		    cpsw_stat_sysctls[i].reg);
		sc->shadow_stats[i] += r;
		cpsw_write_4(sc, CPSW_STATS_OFFSET + cpsw_stat_sysctls[i].reg,
		    r);
	}
}

static int
cpsw_stats_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct cpsw_softc *sc;
	struct cpsw_stat *stat;
	uint64_t result;

	sc = (struct cpsw_softc *)arg1;
	stat = &cpsw_stat_sysctls[oidp->oid_number];
	result = sc->shadow_stats[oidp->oid_number];
	result += cpsw_read_4(sc, CPSW_STATS_OFFSET + stat->reg);
	return (sysctl_handle_64(oidp, &result, 0, req));
}

static int
cpsw_stat_attached(SYSCTL_HANDLER_ARGS)
{
	struct cpsw_softc *sc;
	struct bintime t;
	unsigned result;

	sc = (struct cpsw_softc *)arg1;
	getbinuptime(&t);
	bintime_sub(&t, &sc->attach_uptime);
	result = t.sec;
	return (sysctl_handle_int(oidp, &result, 0, req));
}

static int
cpsw_intr_coalesce(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct cpsw_softc *sc;
	uint32_t ctrl, intr_per_ms;

	sc = (struct cpsw_softc *)arg1;
	error = sysctl_handle_int(oidp, &sc->coal_us, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	ctrl = cpsw_read_4(sc, CPSW_WR_INT_CONTROL);
	ctrl &= ~(CPSW_WR_INT_PACE_EN | CPSW_WR_INT_PRESCALE_MASK);
	if (sc->coal_us == 0) {
		/* Disable the interrupt pace hardware. */
		cpsw_write_4(sc, CPSW_WR_INT_CONTROL, ctrl);
		cpsw_write_4(sc, CPSW_WR_C_RX_IMAX(0), 0);
		cpsw_write_4(sc, CPSW_WR_C_TX_IMAX(0), 0);
		return (0);
	}

	if (sc->coal_us > CPSW_WR_C_IMAX_US_MAX)
		sc->coal_us = CPSW_WR_C_IMAX_US_MAX;
	if (sc->coal_us < CPSW_WR_C_IMAX_US_MIN)
		sc->coal_us = CPSW_WR_C_IMAX_US_MIN;
	intr_per_ms = 1000 / sc->coal_us;
	/* Just to make sure... */
	if (intr_per_ms > CPSW_WR_C_IMAX_MAX)
		intr_per_ms = CPSW_WR_C_IMAX_MAX;
	if (intr_per_ms < CPSW_WR_C_IMAX_MIN)
		intr_per_ms = CPSW_WR_C_IMAX_MIN;

	/* Set the prescale to produce 4us pulses from the 125 Mhz clock. */
	ctrl |= (125 * 4) & CPSW_WR_INT_PRESCALE_MASK;

	/* Enable the interrupt pace hardware. */
	cpsw_write_4(sc, CPSW_WR_C_RX_IMAX(0), intr_per_ms);
	cpsw_write_4(sc, CPSW_WR_C_TX_IMAX(0), intr_per_ms);
	ctrl |= CPSW_WR_INT_C0_RX_PULSE | CPSW_WR_INT_C0_TX_PULSE;
	cpsw_write_4(sc, CPSW_WR_INT_CONTROL, ctrl);

	return (0);
}

static int
cpsw_stat_uptime(SYSCTL_HANDLER_ARGS)
{
	struct cpsw_softc *swsc;
	struct cpswp_softc *sc;
	struct bintime t;
	unsigned result;

	swsc = arg1;
	sc = device_get_softc(swsc->port[arg2].dev);
	if (sc->ifp->if_drv_flags & IFF_DRV_RUNNING) {
		getbinuptime(&t);
		bintime_sub(&t, &sc->init_uptime);
		result = t.sec;
	} else
		result = 0;
	return (sysctl_handle_int(oidp, &result, 0, req));
}

static void
cpsw_add_queue_sysctls(struct sysctl_ctx_list *ctx, struct sysctl_oid *node,
	struct cpsw_queue *queue)
{
	struct sysctl_oid_list *parent;

	parent = SYSCTL_CHILDREN(node);
	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "totalBuffers",
	    CTLFLAG_RD, &queue->queue_slots, 0,
	    "Total buffers currently assigned to this queue");
	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "activeBuffers",
	    CTLFLAG_RD, &queue->active_queue_len, 0,
	    "Buffers currently registered with hardware controller");
	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "maxActiveBuffers",
	    CTLFLAG_RD, &queue->max_active_queue_len, 0,
	    "Max value of activeBuffers since last driver reset");
	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "availBuffers",
	    CTLFLAG_RD, &queue->avail_queue_len, 0,
	    "Buffers allocated to this queue but not currently "
	    "registered with hardware controller");
	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "maxAvailBuffers",
	    CTLFLAG_RD, &queue->max_avail_queue_len, 0,
	    "Max value of availBuffers since last driver reset");
	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "totalEnqueued",
	    CTLFLAG_RD, &queue->queue_adds, 0,
	    "Total buffers added to queue");
	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "totalDequeued",
	    CTLFLAG_RD, &queue->queue_removes, 0,
	    "Total buffers removed from queue");
	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "queueRestart",
	    CTLFLAG_RD, &queue->queue_restart, 0,
	    "Total times the queue has been restarted");
	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "longestChain",
	    CTLFLAG_RD, &queue->longest_chain, 0,
	    "Max buffers used for a single packet");
}

static void
cpsw_add_watchdog_sysctls(struct sysctl_ctx_list *ctx, struct sysctl_oid *node,
	struct cpsw_softc *sc)
{
	struct sysctl_oid_list *parent;

	parent = SYSCTL_CHILDREN(node);
	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "resets",
	    CTLFLAG_RD, &sc->watchdog.resets, 0,
	    "Total number of watchdog resets");
}

static void
cpsw_add_sysctls(struct cpsw_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *stats_node, *queue_node, *node;
	struct sysctl_oid_list *parent, *stats_parent, *queue_parent;
	struct sysctl_oid_list *ports_parent, *port_parent;
	char port[16];
	int i;

	ctx = device_get_sysctl_ctx(sc->dev);
	parent = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));

	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "debug",
	    CTLFLAG_RW, &sc->debug, 0, "Enable switch debug messages");

	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, "attachedSecs",
	    CTLTYPE_UINT | CTLFLAG_RD, sc, 0, cpsw_stat_attached, "IU",
	    "Time since driver attach");

	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, "intr_coalesce_us",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, 0, cpsw_intr_coalesce, "IU",
	    "minimum time between interrupts");

	node = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "ports",
	    CTLFLAG_RD, NULL, "CPSW Ports Statistics");
	ports_parent = SYSCTL_CHILDREN(node);
	for (i = 0; i < CPSW_PORTS; i++) {
		if (!sc->dualemac && i != sc->active_slave)
			continue;
		port[0] = '0' + i;
		port[1] = '\0';
		node = SYSCTL_ADD_NODE(ctx, ports_parent, OID_AUTO,
		    port, CTLFLAG_RD, NULL, "CPSW Port Statistics");
		port_parent = SYSCTL_CHILDREN(node);
		SYSCTL_ADD_PROC(ctx, port_parent, OID_AUTO, "uptime",
		    CTLTYPE_UINT | CTLFLAG_RD, sc, i,
		    cpsw_stat_uptime, "IU", "Seconds since driver init");
	}

	stats_node = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "stats",
				     CTLFLAG_RD, NULL, "CPSW Statistics");
	stats_parent = SYSCTL_CHILDREN(stats_node);
	for (i = 0; i < CPSW_SYSCTL_COUNT; ++i) {
		SYSCTL_ADD_PROC(ctx, stats_parent, i,
				cpsw_stat_sysctls[i].oid,
				CTLTYPE_U64 | CTLFLAG_RD, sc, 0,
				cpsw_stats_sysctl, "IU",
				cpsw_stat_sysctls[i].oid);
	}

	queue_node = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "queue",
	    CTLFLAG_RD, NULL, "CPSW Queue Statistics");
	queue_parent = SYSCTL_CHILDREN(queue_node);

	node = SYSCTL_ADD_NODE(ctx, queue_parent, OID_AUTO, "tx",
	    CTLFLAG_RD, NULL, "TX Queue Statistics");
	cpsw_add_queue_sysctls(ctx, node, &sc->tx);

	node = SYSCTL_ADD_NODE(ctx, queue_parent, OID_AUTO, "rx",
	    CTLFLAG_RD, NULL, "RX Queue Statistics");
	cpsw_add_queue_sysctls(ctx, node, &sc->rx);

	node = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "watchdog",
	    CTLFLAG_RD, NULL, "Watchdog Statistics");
	cpsw_add_watchdog_sysctls(ctx, node, sc);
}

#ifdef CPSW_ETHERSWITCH
static etherswitch_info_t etherswitch_info = {
	.es_nports =		CPSW_PORTS + 1,
	.es_nvlangroups =	CPSW_VLANS,
	.es_name =		"TI Common Platform Ethernet Switch (CPSW)",
	.es_vlan_caps =		ETHERSWITCH_VLAN_DOT1Q,
};

static etherswitch_info_t *
cpsw_getinfo(device_t dev)
{
	return (&etherswitch_info);
}

static int
cpsw_getport(device_t dev, etherswitch_port_t *p)
{
	int err;
	struct cpsw_softc *sc;
	struct cpswp_softc *psc;
	struct ifmediareq *ifmr;
	uint32_t reg;

	if (p->es_port < 0 || p->es_port > CPSW_PORTS)
		return (ENXIO);

	err = 0;
	sc = device_get_softc(dev);
	if (p->es_port == CPSW_CPU_PORT) {
		p->es_flags |= ETHERSWITCH_PORT_CPU;
 		ifmr = &p->es_ifmr;
		ifmr->ifm_current = ifmr->ifm_active =
		    IFM_ETHER | IFM_1000_T | IFM_FDX;
		ifmr->ifm_mask = 0;
		ifmr->ifm_status = IFM_ACTIVE | IFM_AVALID;
		ifmr->ifm_count = 0;
	} else {
		psc = device_get_softc(sc->port[p->es_port - 1].dev);
		err = ifmedia_ioctl(psc->ifp, &p->es_ifr,
		    &psc->mii->mii_media, SIOCGIFMEDIA);
	}
	reg = cpsw_read_4(sc, CPSW_PORT_P_VLAN(p->es_port));
	p->es_pvid = reg & ETHERSWITCH_VID_MASK;

	reg = cpsw_read_4(sc, CPSW_ALE_PORTCTL(p->es_port));
	if (reg & ALE_PORTCTL_DROP_UNTAGGED)
		p->es_flags |= ETHERSWITCH_PORT_DROPUNTAGGED;
	if (reg & ALE_PORTCTL_INGRESS)
		p->es_flags |= ETHERSWITCH_PORT_INGRESS;

	return (err);
}

static int
cpsw_setport(device_t dev, etherswitch_port_t *p)
{
	struct cpsw_softc *sc;
	struct cpswp_softc *psc;
	struct ifmedia *ifm;
	uint32_t reg;

	if (p->es_port < 0 || p->es_port > CPSW_PORTS)
		return (ENXIO);

	sc = device_get_softc(dev);
	if (p->es_pvid != 0) {
		cpsw_write_4(sc, CPSW_PORT_P_VLAN(p->es_port),
		    p->es_pvid & ETHERSWITCH_VID_MASK);
	}

	reg = cpsw_read_4(sc, CPSW_ALE_PORTCTL(p->es_port));
	if (p->es_flags & ETHERSWITCH_PORT_DROPUNTAGGED)
		reg |= ALE_PORTCTL_DROP_UNTAGGED;
	else
		reg &= ~ALE_PORTCTL_DROP_UNTAGGED;
	if (p->es_flags & ETHERSWITCH_PORT_INGRESS)
		reg |= ALE_PORTCTL_INGRESS;
	else
		reg &= ~ALE_PORTCTL_INGRESS;
	cpsw_write_4(sc, CPSW_ALE_PORTCTL(p->es_port), reg);

	/* CPU port does not allow media settings. */
	if (p->es_port == CPSW_CPU_PORT)
		return (0);

	psc = device_get_softc(sc->port[p->es_port - 1].dev);
	ifm = &psc->mii->mii_media;

	return (ifmedia_ioctl(psc->ifp, &p->es_ifr, ifm, SIOCSIFMEDIA));
}

static int
cpsw_getconf(device_t dev, etherswitch_conf_t *conf)
{

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = ETHERSWITCH_VLAN_DOT1Q;

	return (0);
}

static int
cpsw_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	int i, vid;
	uint32_t ale_entry[3];
	struct cpsw_softc *sc;

	sc = device_get_softc(dev);

	if (vg->es_vlangroup >= CPSW_VLANS)
		return (EINVAL);

	vg->es_vid = 0;
	vid = cpsw_vgroups[vg->es_vlangroup].vid;
	if (vid == -1)
		return (0);

	for (i = 0; i < CPSW_MAX_ALE_ENTRIES; i++) {
		cpsw_ale_read_entry(sc, i, ale_entry);
		if (ALE_TYPE(ale_entry) != ALE_TYPE_VLAN)
			continue;
		if (vid != ALE_VLAN(ale_entry))
			continue;

		vg->es_fid = 0;
		vg->es_vid = ALE_VLAN(ale_entry) | ETHERSWITCH_VID_VALID;
		vg->es_member_ports = ALE_VLAN_MEMBERS(ale_entry);
		vg->es_untagged_ports = ALE_VLAN_UNTAG(ale_entry);
	}

	return (0);
}

static void
cpsw_remove_vlan(struct cpsw_softc *sc, int vlan)
{
	int i;
	uint32_t ale_entry[3];

	for (i = 0; i < CPSW_MAX_ALE_ENTRIES; i++) {
		cpsw_ale_read_entry(sc, i, ale_entry);
		if (ALE_TYPE(ale_entry) != ALE_TYPE_VLAN)
			continue;
		if (vlan != ALE_VLAN(ale_entry))
			continue;
		ale_entry[0] = ale_entry[1] = ale_entry[2] = 0;
		cpsw_ale_write_entry(sc, i, ale_entry);
		break;
	}
}

static int
cpsw_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	int i;
	struct cpsw_softc *sc;

	sc = device_get_softc(dev);

	for (i = 0; i < CPSW_VLANS; i++) {
		/* Is this Vlan ID in use by another vlangroup ? */
		if (vg->es_vlangroup != i && cpsw_vgroups[i].vid == vg->es_vid)
			return (EINVAL);
	}

	if (vg->es_vid == 0) {
		if (cpsw_vgroups[vg->es_vlangroup].vid == -1)
			return (0);
		cpsw_remove_vlan(sc, cpsw_vgroups[vg->es_vlangroup].vid);
		cpsw_vgroups[vg->es_vlangroup].vid = -1;
		vg->es_untagged_ports = 0;
		vg->es_member_ports = 0;
		vg->es_vid = 0;
		return (0);
	}

	vg->es_vid &= ETHERSWITCH_VID_MASK;
	vg->es_member_ports &= CPSW_PORTS_MASK;
	vg->es_untagged_ports &= CPSW_PORTS_MASK;

	if (cpsw_vgroups[vg->es_vlangroup].vid != -1 &&
	    cpsw_vgroups[vg->es_vlangroup].vid != vg->es_vid)
		return (EINVAL);

	cpsw_vgroups[vg->es_vlangroup].vid = vg->es_vid;
	cpsw_ale_update_vlan_table(sc, vg->es_vid, vg->es_member_ports,
	    vg->es_untagged_ports, vg->es_member_ports, 0);

	return (0);
}

static int
cpsw_readreg(device_t dev, int addr)
{

	/* Not supported. */
	return (0);
}

static int
cpsw_writereg(device_t dev, int addr, int value)
{

	/* Not supported. */
	return (0);
}

static int
cpsw_readphy(device_t dev, int phy, int reg)
{

	/* Not supported. */
	return (0);
}

static int
cpsw_writephy(device_t dev, int phy, int reg, int data)
{

	/* Not supported. */
	return (0);
}
#endif
