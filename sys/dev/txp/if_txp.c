/*	$OpenBSD: if_txp.c,v 1.48 2001/06/27 06:34:50 kjc Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001
 *	Jason L. Wright <jason@thought.net>, Theo de Raadt, and
 *	Aaron Campbell <aaron@monkey.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright,
 *	Theo de Raadt and Aaron Campbell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for 3c990 (Typhoon) Ethernet ASIC
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <dev/mii/mii.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/in_cksum.h>

#include <dev/txp/if_txpreg.h>
#include <dev/txp/3c990img.h>

MODULE_DEPEND(txp, pci, 1, 1, 1);
MODULE_DEPEND(txp, ether, 1, 1, 1);

/*
 * XXX Known Typhoon firmware issues.
 *
 * 1. It seems that firmware has Tx TCP/UDP checksum offloading bug.
 *    The firmware hangs when it's told to compute TCP/UDP checksum.
 *    I'm not sure whether the firmware requires special alignment to
 *    do checksum offloading but datasheet says nothing about that.
 * 2. Datasheet says nothing for maximum number of fragmented
 *    descriptors supported. Experimentation shows up to 16 fragment
 *    descriptors are supported in the firmware. For TSO case, upper
 *    stack can send 64KB sized IP datagram plus link header size(
 *    ethernet header + VLAN tag)  frame but controller can handle up
 *    to 64KB frame given that PAGE_SIZE is 4KB(i.e. 16 * PAGE_SIZE).
 *    Because frames that need TSO operation of hardware can be
 *    larger than 64KB I disabled TSO capability. TSO operation for
 *    less than or equal to 16 fragment descriptors works without
 *    problems, though.
 * 3. VLAN hardware tag stripping is always enabled in the firmware
 *    even if it's explicitly told to not strip the tag. It's
 *    possible to add the tag back in Rx handler if VLAN hardware
 *    tag is not active but I didn't try that as it would be
 *    layering violation.
 * 4. TXP_CMD_RECV_BUFFER_CONTROL does not work as expected in
 *    datasheet such that driver should handle the alignment
 *    restriction by copying received frame to align the frame on
 *    32bit boundary on strict-alignment architectures. This adds a
 *    lot of CPU burden and it effectively reduce Rx performance on
 *    strict-alignment architectures(e.g. sparc64, arm and mips).
 *
 * Unfortunately it seems that 3Com have no longer interests in
 * releasing fixed firmware so we may have to live with these bugs.
 */

#define	TXP_CSUM_FEATURES	(CSUM_IP)

/*
 * Various supported device vendors/types and their names.
 */
static struct txp_type txp_devs[] = {
	{ TXP_VENDORID_3COM, TXP_DEVICEID_3CR990_TX_95,
	    "3Com 3cR990-TX-95 Etherlink with 3XP Processor" },
	{ TXP_VENDORID_3COM, TXP_DEVICEID_3CR990_TX_97,
	    "3Com 3cR990-TX-97 Etherlink with 3XP Processor" },
	{ TXP_VENDORID_3COM, TXP_DEVICEID_3CR990B_TXM,
	    "3Com 3cR990B-TXM Etherlink with 3XP Processor" },
	{ TXP_VENDORID_3COM, TXP_DEVICEID_3CR990_SRV_95,
	    "3Com 3cR990-SRV-95 Etherlink Server with 3XP Processor" },
	{ TXP_VENDORID_3COM, TXP_DEVICEID_3CR990_SRV_97,
	    "3Com 3cR990-SRV-97 Etherlink Server with 3XP Processor" },
	{ TXP_VENDORID_3COM, TXP_DEVICEID_3CR990B_SRV,
	    "3Com 3cR990B-SRV Etherlink Server with 3XP Processor" },
	{ 0, 0, NULL }
};

static int txp_probe(device_t);
static int txp_attach(device_t);
static int txp_detach(device_t);
static int txp_shutdown(device_t);
static int txp_suspend(device_t);
static int txp_resume(device_t);
static int txp_intr(void *);
static void txp_int_task(void *, int);
static void txp_tick(void *);
static int txp_ioctl(struct ifnet *, u_long, caddr_t);
static uint64_t txp_get_counter(struct ifnet *, ift_counter);
static void txp_start(struct ifnet *);
static void txp_start_locked(struct ifnet *);
static int txp_encap(struct txp_softc *, struct txp_tx_ring *, struct mbuf **);
static void txp_stop(struct txp_softc *);
static void txp_init(void *);
static void txp_init_locked(struct txp_softc *);
static void txp_watchdog(struct txp_softc *);

static int txp_reset(struct txp_softc *);
static int txp_boot(struct txp_softc *, uint32_t);
static int txp_sleep(struct txp_softc *, int);
static int txp_wait(struct txp_softc *, uint32_t);
static int txp_download_fw(struct txp_softc *);
static int txp_download_fw_wait(struct txp_softc *);
static int txp_download_fw_section(struct txp_softc *,
    struct txp_fw_section_header *, int);
static int txp_alloc_rings(struct txp_softc *);
static void txp_init_rings(struct txp_softc *);
static int txp_dma_alloc(struct txp_softc *, char *, bus_dma_tag_t *,
    bus_size_t, bus_size_t, bus_dmamap_t *, void **, bus_size_t, bus_addr_t *);
static void txp_dma_free(struct txp_softc *, bus_dma_tag_t *, bus_dmamap_t,
    void **, bus_addr_t *);
static void txp_free_rings(struct txp_softc *);
static int txp_rxring_fill(struct txp_softc *);
static void txp_rxring_empty(struct txp_softc *);
static void txp_set_filter(struct txp_softc *);

static int txp_cmd_desc_numfree(struct txp_softc *);
static int txp_command(struct txp_softc *, uint16_t, uint16_t, uint32_t,
    uint32_t, uint16_t *, uint32_t *, uint32_t *, int);
static int txp_ext_command(struct txp_softc *, uint16_t, uint16_t,
    uint32_t, uint32_t, struct txp_ext_desc *, uint8_t,
    struct txp_rsp_desc **, int);
static int txp_response(struct txp_softc *, uint16_t, uint16_t,
    struct txp_rsp_desc **);
static void txp_rsp_fixup(struct txp_softc *, struct txp_rsp_desc *,
    struct txp_rsp_desc *);
static int txp_set_capabilities(struct txp_softc *);

static void txp_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int txp_ifmedia_upd(struct ifnet *);
#ifdef TXP_DEBUG
static void txp_show_descriptor(void *);
#endif
static void txp_tx_reclaim(struct txp_softc *, struct txp_tx_ring *);
static void txp_rxbuf_reclaim(struct txp_softc *);
#ifndef __NO_STRICT_ALIGNMENT
static __inline void txp_fixup_rx(struct mbuf *);
#endif
static int txp_rx_reclaim(struct txp_softc *, struct txp_rx_ring *, int);
static void txp_stats_save(struct txp_softc *);
static void txp_stats_update(struct txp_softc *, struct txp_rsp_desc *);
static void txp_sysctl_node(struct txp_softc *);
static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int sysctl_hw_txp_proc_limit(SYSCTL_HANDLER_ARGS);

static int prefer_iomap = 0;
TUNABLE_INT("hw.txp.prefer_iomap", &prefer_iomap);

static device_method_t txp_methods[] = {
        /* Device interface */
	DEVMETHOD(device_probe,		txp_probe),
	DEVMETHOD(device_attach,	txp_attach),
	DEVMETHOD(device_detach,	txp_detach),
	DEVMETHOD(device_shutdown,	txp_shutdown),
	DEVMETHOD(device_suspend,	txp_suspend),
	DEVMETHOD(device_resume,	txp_resume),

	{ NULL, NULL }
};

static driver_t txp_driver = {
	"txp",
	txp_methods,
	sizeof(struct txp_softc)
};

static devclass_t txp_devclass;

DRIVER_MODULE(txp, pci, txp_driver, txp_devclass, 0, 0);

static int
txp_probe(device_t dev)
{
	struct txp_type *t;

	t = txp_devs;

	while (t->txp_name != NULL) {
		if ((pci_get_vendor(dev) == t->txp_vid) &&
		    (pci_get_device(dev) == t->txp_did)) {
			device_set_desc(dev, t->txp_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

static int
txp_attach(device_t dev)
{
	struct txp_softc *sc;
	struct ifnet *ifp;
	struct txp_rsp_desc *rsp;
	uint16_t p1;
	uint32_t p2, reg;
	int error = 0, pmc, rid;
	uint8_t eaddr[ETHER_ADDR_LEN], *ver;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->sc_tick, &sc->sc_mtx, 0);
	TASK_INIT(&sc->sc_int_task, 0, txp_int_task, sc);
	TAILQ_INIT(&sc->sc_busy_list);
	TAILQ_INIT(&sc->sc_free_list);

	ifmedia_init(&sc->sc_ifmedia, 0, txp_ifmedia_upd, txp_ifmedia_sts);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_10_T, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_10_T | IFM_HDX, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_10_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_100_TX, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_100_TX | IFM_HDX, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_100_TX | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);

	pci_enable_busmaster(dev);
	/* Prefer memory space register mapping over IO space. */
	if (prefer_iomap == 0) {
		sc->sc_res_id = PCIR_BAR(1);
		sc->sc_res_type = SYS_RES_MEMORY;
	} else {
		sc->sc_res_id = PCIR_BAR(0);
		sc->sc_res_type = SYS_RES_IOPORT;
	}
	sc->sc_res = bus_alloc_resource_any(dev, sc->sc_res_type,
	    &sc->sc_res_id, RF_ACTIVE);
	if (sc->sc_res == NULL && prefer_iomap == 0) {
		sc->sc_res_id = PCIR_BAR(0);
		sc->sc_res_type = SYS_RES_IOPORT;
		sc->sc_res = bus_alloc_resource_any(dev, sc->sc_res_type,
		    &sc->sc_res_id, RF_ACTIVE);
	}
	if (sc->sc_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		ifmedia_removeall(&sc->sc_ifmedia);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	/* Enable MWI. */
	reg = pci_read_config(dev, PCIR_COMMAND, 2);
	reg |= PCIM_CMD_MWRICEN;
	pci_write_config(dev, PCIR_COMMAND, reg, 2);
	/* Check cache line size. */
	reg = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	reg <<= 4;
	if (reg == 0 || (reg % 16) != 0)
		device_printf(sc->sc_dev,
		    "invalid cache line size : %u\n", reg);

	/* Allocate interrupt */
	rid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->sc_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	if ((error = txp_alloc_rings(sc)) != 0)
		goto fail;
	txp_init_rings(sc);
	txp_sysctl_node(sc);
	/* Reset controller and make it reload sleep image. */
	if (txp_reset(sc) != 0) {
		error = ENXIO;
		goto fail;
	}

	/* Let controller boot from sleep image. */
	if (txp_boot(sc, STAT_WAITING_FOR_HOST_REQUEST) != 0) {
		device_printf(sc->sc_dev, "could not boot sleep image\n");
		error = ENXIO;
		goto fail;
	}

	/* Get station address. */
	if (txp_command(sc, TXP_CMD_STATION_ADDRESS_READ, 0, 0, 0,
	    &p1, &p2, NULL, TXP_CMD_WAIT)) {
		error = ENXIO;
		goto fail;
	}

	p1 = le16toh(p1);
	eaddr[0] = ((uint8_t *)&p1)[1];
	eaddr[1] = ((uint8_t *)&p1)[0];
	p2 = le32toh(p2);
	eaddr[2] = ((uint8_t *)&p2)[3];
	eaddr[3] = ((uint8_t *)&p2)[2];
	eaddr[4] = ((uint8_t *)&p2)[1];
	eaddr[5] = ((uint8_t *)&p2)[0];

	ifp = sc->sc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}

	/*
	 * Show sleep image version information which may help to
	 * diagnose sleep image specific issues.
	 */
	rsp = NULL;
	if (txp_ext_command(sc, TXP_CMD_VERSIONS_READ, 0, 0, 0, NULL, 0,
	    &rsp, TXP_CMD_WAIT)) {
		device_printf(dev, "can not read sleep image version\n");
		error = ENXIO;
		goto fail;
	}
	if (rsp->rsp_numdesc == 0) {
		p2 = le32toh(rsp->rsp_par2) & 0xFFFF;
		device_printf(dev, "Typhoon 1.0 sleep image (2000/%02u/%02u)\n",
		    p2 >> 8, p2 & 0xFF);
	} else if (rsp->rsp_numdesc == 2) {
		p2 = le32toh(rsp->rsp_par2);
		ver = (uint8_t *)(rsp + 1);
		/*
		 * Even if datasheet says the command returns a NULL
		 * terminated version string, explicitly terminate
		 * the string. Given that several bugs of firmware
		 * I can't trust this simple one.
		 */
		ver[25] = '\0';
		device_printf(dev,
		    "Typhoon 1.1+ sleep image %02u.%03u.%03u %s\n",
		    p2 >> 24, (p2 >> 12) & 0xFFF, p2 & 0xFFF, ver);
	} else {
		p2 = le32toh(rsp->rsp_par2);
		device_printf(dev,
		    "Unknown Typhoon sleep image version: %u:0x%08x\n",
		    rsp->rsp_numdesc, p2);
	}
	free(rsp, M_DEVBUF);

	sc->sc_xcvr = TXP_XCVR_AUTO;
	txp_command(sc, TXP_CMD_XCVR_SELECT, TXP_XCVR_AUTO, 0, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT);
	ifmedia_set(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO);

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = txp_ioctl;
	ifp->if_start = txp_start;
	ifp->if_init = txp_init;
	ifp->if_get_counter = txp_get_counter;
	ifp->if_snd.ifq_drv_maxlen = TX_ENTRIES - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	/*
	 * It's possible to read firmware's offload capability but
	 * we have not downloaded the firmware yet so announce
	 * working capability here. We're not interested in IPSec
	 * capability and due to the lots of firmware bug we can't
	 * advertise the whole capability anyway.
	 */
	ifp->if_capabilities = IFCAP_RXCSUM | IFCAP_TXCSUM;
	if (pci_find_cap(dev, PCIY_PMG, &pmc) == 0)
		ifp->if_capabilities |= IFCAP_WOL_MAGIC;
	/* Enable all capabilities. */
	ifp->if_capenable = ifp->if_capabilities;

	ether_ifattach(ifp, eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWCSUM;
	ifp->if_capenable = ifp->if_capabilities;
	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	WRITE_REG(sc, TXP_IER, TXP_INTR_NONE);
	WRITE_REG(sc, TXP_IMR, TXP_INTR_ALL);

	/* Create local taskq. */
	sc->sc_tq = taskqueue_create_fast("txp_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->sc_tq);
	if (sc->sc_tq == NULL) {
		device_printf(dev, "could not create taskqueue.\n");
		ether_ifdetach(ifp);
		error = ENXIO;
		goto fail;
	}
	taskqueue_start_threads(&sc->sc_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->sc_dev));

	/* Put controller into sleep. */
	if (txp_sleep(sc, 0) != 0) {
		ether_ifdetach(ifp);
		error = ENXIO;
		goto fail;
	}

	error = bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    txp_intr, NULL, sc, &sc->sc_intrhand);

	if (error != 0) {
		ether_ifdetach(ifp);
		device_printf(dev, "couldn't set up interrupt handler.\n");
		goto fail;
	}

	gone_by_fcp101_dev(dev);

	return (0);

fail:
	if (error != 0)
		txp_detach(dev);
	return (error);
}

static int
txp_detach(device_t dev)
{
	struct txp_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	ifp = sc->sc_ifp;
	if (device_is_attached(dev)) {
		TXP_LOCK(sc);
		sc->sc_flags |= TXP_FLAG_DETACH;
		txp_stop(sc);
		TXP_UNLOCK(sc);
		callout_drain(&sc->sc_tick);
		taskqueue_drain(sc->sc_tq, &sc->sc_int_task);
		ether_ifdetach(ifp);
	}
	WRITE_REG(sc, TXP_IMR, TXP_INTR_ALL);

	ifmedia_removeall(&sc->sc_ifmedia);
	if (sc->sc_intrhand != NULL)
		bus_teardown_intr(dev, sc->sc_irq, sc->sc_intrhand);
	if (sc->sc_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq);
	if (sc->sc_res != NULL)
		bus_release_resource(dev, sc->sc_res_type, sc->sc_res_id,
		    sc->sc_res);
	if (sc->sc_ifp != NULL) {
		if_free(sc->sc_ifp);
		sc->sc_ifp = NULL;
	}
	txp_free_rings(sc);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
txp_reset(struct txp_softc *sc)
{
	uint32_t r;
	int i;

	/* Disable interrupts. */
	WRITE_REG(sc, TXP_IER, TXP_INTR_NONE);
	WRITE_REG(sc, TXP_IMR, TXP_INTR_ALL);
	/* Ack all pending interrupts. */
	WRITE_REG(sc, TXP_ISR, TXP_INTR_ALL);

	r = 0;
	WRITE_REG(sc, TXP_SRR, TXP_SRR_ALL);
	DELAY(1000);
	WRITE_REG(sc, TXP_SRR, 0);

	/* Should wait max 6 seconds. */
	for (i = 0; i < 6000; i++) {
		r = READ_REG(sc, TXP_A2H_0);
		if (r == STAT_WAITING_FOR_HOST_REQUEST)
			break;
		DELAY(1000);
	}

	if (r != STAT_WAITING_FOR_HOST_REQUEST)
		device_printf(sc->sc_dev, "reset hung\n");

	WRITE_REG(sc, TXP_IER, TXP_INTR_NONE);
	WRITE_REG(sc, TXP_IMR, TXP_INTR_ALL);
	WRITE_REG(sc, TXP_ISR, TXP_INTR_ALL);

	/*
	 * Give more time to complete loading sleep image before
	 * trying to boot from sleep image.
	 */
	DELAY(5000);

	return (0);
}

static int
txp_boot(struct txp_softc *sc, uint32_t state)
{

	/* See if it's waiting for boot, and try to boot it. */
	if (txp_wait(sc, state) != 0) {
		device_printf(sc->sc_dev, "not waiting for boot\n");
		return (ENXIO);
	}

	WRITE_REG(sc, TXP_H2A_2, TXP_ADDR_HI(sc->sc_ldata.txp_boot_paddr));
	TXP_BARRIER(sc, TXP_H2A_2, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_1, TXP_ADDR_LO(sc->sc_ldata.txp_boot_paddr));
	TXP_BARRIER(sc, TXP_H2A_1, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_0, TXP_BOOTCMD_REGISTER_BOOT_RECORD);
	TXP_BARRIER(sc, TXP_H2A_0, 4, BUS_SPACE_BARRIER_WRITE);

	/* See if it booted. */
	if (txp_wait(sc, STAT_RUNNING) != 0) {
		device_printf(sc->sc_dev, "firmware not running\n");
		return (ENXIO);
	}

	/* Clear TX and CMD ring write registers. */
	WRITE_REG(sc, TXP_H2A_1, TXP_BOOTCMD_NULL);
	TXP_BARRIER(sc, TXP_H2A_1, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_2, TXP_BOOTCMD_NULL);
	TXP_BARRIER(sc, TXP_H2A_2, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_3, TXP_BOOTCMD_NULL);
	TXP_BARRIER(sc, TXP_H2A_3, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_0, TXP_BOOTCMD_NULL);
	TXP_BARRIER(sc, TXP_H2A_0, 4, BUS_SPACE_BARRIER_WRITE);

	return (0);
}

static int
txp_download_fw(struct txp_softc *sc)
{
	struct txp_fw_file_header *fileheader;
	struct txp_fw_section_header *secthead;
	int sect;
	uint32_t error, ier, imr;

	TXP_LOCK_ASSERT(sc);

	error = 0;
	ier = READ_REG(sc, TXP_IER);
	WRITE_REG(sc, TXP_IER, ier | TXP_INT_A2H_0);

	imr = READ_REG(sc, TXP_IMR);
	WRITE_REG(sc, TXP_IMR, imr | TXP_INT_A2H_0);

	if (txp_wait(sc, STAT_WAITING_FOR_HOST_REQUEST) != 0) {
		device_printf(sc->sc_dev, "not waiting for host request\n");
		error = ETIMEDOUT;
		goto fail;
	}

	/* Ack the status. */
	WRITE_REG(sc, TXP_ISR, TXP_INT_A2H_0);

	fileheader = (struct txp_fw_file_header *)tc990image;
	if (bcmp("TYPHOON", fileheader->magicid, sizeof(fileheader->magicid))) {
		device_printf(sc->sc_dev, "firmware invalid magic\n");
		goto fail;
	}

	/* Tell boot firmware to get ready for image. */
	WRITE_REG(sc, TXP_H2A_1, le32toh(fileheader->addr));
	TXP_BARRIER(sc, TXP_H2A_1, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_2, le32toh(fileheader->hmac[0]));
	TXP_BARRIER(sc, TXP_H2A_2, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_3, le32toh(fileheader->hmac[1]));
	TXP_BARRIER(sc, TXP_H2A_3, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_4, le32toh(fileheader->hmac[2]));
	TXP_BARRIER(sc, TXP_H2A_4, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_5, le32toh(fileheader->hmac[3]));
	TXP_BARRIER(sc, TXP_H2A_5, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_6, le32toh(fileheader->hmac[4]));
	TXP_BARRIER(sc, TXP_H2A_6, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_0, TXP_BOOTCMD_RUNTIME_IMAGE);
	TXP_BARRIER(sc, TXP_H2A_0, 4, BUS_SPACE_BARRIER_WRITE);

	if (txp_download_fw_wait(sc)) {
		device_printf(sc->sc_dev, "firmware wait failed, initial\n");
		error = ETIMEDOUT;
		goto fail;
	}

	secthead = (struct txp_fw_section_header *)(((uint8_t *)tc990image) +
	    sizeof(struct txp_fw_file_header));

	for (sect = 0; sect < le32toh(fileheader->nsections); sect++) {
		if ((error = txp_download_fw_section(sc, secthead, sect)) != 0)
			goto fail;
		secthead = (struct txp_fw_section_header *)
		    (((uint8_t *)secthead) + le32toh(secthead->nbytes) +
		    sizeof(*secthead));
	}

	WRITE_REG(sc, TXP_H2A_0, TXP_BOOTCMD_DOWNLOAD_COMPLETE);
	TXP_BARRIER(sc, TXP_H2A_0, 4, BUS_SPACE_BARRIER_WRITE);

	if (txp_wait(sc, STAT_WAITING_FOR_BOOT) != 0) {
		device_printf(sc->sc_dev, "not waiting for boot\n");
		error = ETIMEDOUT;
		goto fail;
	}

fail:
	WRITE_REG(sc, TXP_IER, ier);
	WRITE_REG(sc, TXP_IMR, imr);

	return (error);
}

static int
txp_download_fw_wait(struct txp_softc *sc)
{
	uint32_t i;

	TXP_LOCK_ASSERT(sc);

	for (i = 0; i < TXP_TIMEOUT; i++) {
		if ((READ_REG(sc, TXP_ISR) & TXP_INT_A2H_0) != 0)
			break;
		DELAY(50);
	}

	if (i == TXP_TIMEOUT) {
		device_printf(sc->sc_dev, "firmware wait failed comm0\n");
		return (ETIMEDOUT);
	}

	WRITE_REG(sc, TXP_ISR, TXP_INT_A2H_0);

	if (READ_REG(sc, TXP_A2H_0) != STAT_WAITING_FOR_SEGMENT) {
		device_printf(sc->sc_dev, "firmware not waiting for segment\n");
		return (ETIMEDOUT);
	}
	return (0);
}

static int
txp_download_fw_section(struct txp_softc *sc,
    struct txp_fw_section_header *sect, int sectnum)
{
	bus_dma_tag_t sec_tag;
	bus_dmamap_t sec_map;
	bus_addr_t sec_paddr;
	uint8_t *sec_buf;
	int rseg, err = 0;
	struct mbuf m;
	uint16_t csum;

	TXP_LOCK_ASSERT(sc);

	/* Skip zero length sections. */
	if (le32toh(sect->nbytes) == 0)
		return (0);

	/* Make sure we aren't past the end of the image. */
	rseg = ((uint8_t *)sect) - ((uint8_t *)tc990image);
	if (rseg >= sizeof(tc990image)) {
		device_printf(sc->sc_dev,
		    "firmware invalid section address, section %d\n", sectnum);
		return (EIO);
	}

	/* Make sure this section doesn't go past the end. */
	rseg += le32toh(sect->nbytes);
	if (rseg >= sizeof(tc990image)) {
		device_printf(sc->sc_dev, "firmware truncated section %d\n",
		    sectnum);
		return (EIO);
	}

	sec_tag = NULL;
	sec_map = NULL;
	sec_buf = NULL;
	/* XXX */
	TXP_UNLOCK(sc);
	err = txp_dma_alloc(sc, "firmware sections", &sec_tag, sizeof(uint32_t),
	    0, &sec_map, (void **)&sec_buf, le32toh(sect->nbytes), &sec_paddr);
	TXP_LOCK(sc);
	if (err != 0)
		goto bail;
	bcopy(((uint8_t *)sect) + sizeof(*sect), sec_buf,
	    le32toh(sect->nbytes));

	/*
	 * dummy up mbuf and verify section checksum
	 */
	m.m_type = MT_DATA;
	m.m_next = m.m_nextpkt = NULL;
	m.m_len = le32toh(sect->nbytes);
	m.m_data = sec_buf;
	m.m_flags = 0;
	csum = in_cksum(&m, le32toh(sect->nbytes));
	if (csum != sect->cksum) {
		device_printf(sc->sc_dev,
		    "firmware section %d, bad cksum (expected 0x%x got 0x%x)\n",
		    sectnum, le16toh(sect->cksum), csum);
		err = EIO;
		goto bail;
	}

	bus_dmamap_sync(sec_tag, sec_map, BUS_DMASYNC_PREWRITE);

	WRITE_REG(sc, TXP_H2A_1, le32toh(sect->nbytes));
	TXP_BARRIER(sc, TXP_H2A_1, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_2, le16toh(sect->cksum));
	TXP_BARRIER(sc, TXP_H2A_2, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_3, le32toh(sect->addr));
	TXP_BARRIER(sc, TXP_H2A_3, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_4, TXP_ADDR_HI(sec_paddr));
	TXP_BARRIER(sc, TXP_H2A_4, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_5, TXP_ADDR_LO(sec_paddr));
	TXP_BARRIER(sc, TXP_H2A_5, 4, BUS_SPACE_BARRIER_WRITE);
	WRITE_REG(sc, TXP_H2A_0, TXP_BOOTCMD_SEGMENT_AVAILABLE);
	TXP_BARRIER(sc, TXP_H2A_0, 4, BUS_SPACE_BARRIER_WRITE);

	if (txp_download_fw_wait(sc)) {
		device_printf(sc->sc_dev,
		    "firmware wait failed, section %d\n", sectnum);
		err = ETIMEDOUT;
	}

	bus_dmamap_sync(sec_tag, sec_map, BUS_DMASYNC_POSTWRITE);
bail:
	txp_dma_free(sc, &sec_tag, sec_map, (void **)&sec_buf, &sec_paddr);
	return (err);
}

static int
txp_intr(void *vsc)
{
	struct txp_softc *sc;
	uint32_t status;

	sc = vsc;
	status = READ_REG(sc, TXP_ISR);
	if ((status & TXP_INT_LATCH) == 0)
		return (FILTER_STRAY);
	WRITE_REG(sc, TXP_ISR, status);
	WRITE_REG(sc, TXP_IMR, TXP_INTR_ALL);
	taskqueue_enqueue(sc->sc_tq, &sc->sc_int_task);

	return (FILTER_HANDLED);
}

static void
txp_int_task(void *arg, int pending)
{
	struct txp_softc *sc;
	struct ifnet *ifp;
	struct txp_hostvar *hv;
	uint32_t isr;
	int more;

	sc = (struct txp_softc *)arg;

	TXP_LOCK(sc);
	ifp = sc->sc_ifp;
	hv = sc->sc_hostvar;
	isr = READ_REG(sc, TXP_ISR);
	if ((isr & TXP_INT_LATCH) != 0)
		WRITE_REG(sc, TXP_ISR, isr);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
		    sc->sc_cdata.txp_hostvar_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		more = 0;
		if ((*sc->sc_rxhir.r_roff) != (*sc->sc_rxhir.r_woff))
			more += txp_rx_reclaim(sc, &sc->sc_rxhir,
			    sc->sc_process_limit);
		if ((*sc->sc_rxlor.r_roff) != (*sc->sc_rxlor.r_woff))
			more += txp_rx_reclaim(sc, &sc->sc_rxlor,
			    sc->sc_process_limit);
		/*
		 * XXX
		 * It seems controller is not smart enough to handle
		 * FIFO overflow conditions under heavy network load.
		 * No matter how often new Rx buffers are passed to
		 * controller the situation didn't change. Maybe
		 * flow-control would be the only way to mitigate the
		 * issue but firmware does not have commands that
		 * control the threshold of emitting pause frames.
		 */
		if (hv->hv_rx_buf_write_idx == hv->hv_rx_buf_read_idx)
			txp_rxbuf_reclaim(sc);
		if (sc->sc_txhir.r_cnt && (sc->sc_txhir.r_cons !=
		    TXP_OFFSET2IDX(le32toh(*(sc->sc_txhir.r_off)))))
			txp_tx_reclaim(sc, &sc->sc_txhir);
		if (sc->sc_txlor.r_cnt && (sc->sc_txlor.r_cons !=
		    TXP_OFFSET2IDX(le32toh(*(sc->sc_txlor.r_off)))))
			txp_tx_reclaim(sc, &sc->sc_txlor);
		bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
		    sc->sc_cdata.txp_hostvar_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			txp_start_locked(sc->sc_ifp);
		if (more != 0 || READ_REG(sc, TXP_ISR & TXP_INT_LATCH) != 0) {
			taskqueue_enqueue(sc->sc_tq, &sc->sc_int_task);
			TXP_UNLOCK(sc);
			return;
		}
	}

	/* Re-enable interrupts. */
	WRITE_REG(sc, TXP_IMR, TXP_INTR_NONE);
	TXP_UNLOCK(sc);
}

#ifndef __NO_STRICT_ALIGNMENT
static __inline void
txp_fixup_rx(struct mbuf *m)
{
	int i;
	uint16_t *src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - (TXP_RXBUF_ALIGN - ETHER_ALIGN) / sizeof *src;

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;

	m->m_data -= TXP_RXBUF_ALIGN - ETHER_ALIGN;
}
#endif

static int
txp_rx_reclaim(struct txp_softc *sc, struct txp_rx_ring *r, int count)
{
	struct ifnet *ifp;
	struct txp_rx_desc *rxd;
	struct mbuf *m;
	struct txp_rx_swdesc *sd;
	uint32_t roff, woff, rx_stat, prog;

	TXP_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;

	bus_dmamap_sync(r->r_tag, r->r_map, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	roff = le32toh(*r->r_roff);
	woff = le32toh(*r->r_woff);
	rxd = r->r_desc + roff / sizeof(struct txp_rx_desc);
	for (prog = 0; roff != woff; prog++, count--) {
		if (count <= 0)
			break;
		bcopy((u_long *)&rxd->rx_vaddrlo, &sd, sizeof(sd));
		KASSERT(sd != NULL, ("%s: Rx desc ring corrupted", __func__));
		bus_dmamap_sync(sc->sc_cdata.txp_rx_tag, sd->sd_map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_cdata.txp_rx_tag, sd->sd_map);
		m = sd->sd_mbuf;
		KASSERT(m != NULL, ("%s: Rx buffer ring corrupted", __func__));
		sd->sd_mbuf = NULL;
		TAILQ_REMOVE(&sc->sc_busy_list, sd, sd_next);
		TAILQ_INSERT_TAIL(&sc->sc_free_list, sd, sd_next);
		if ((rxd->rx_flags & RX_FLAGS_ERROR) != 0) {
			if (bootverbose)
				device_printf(sc->sc_dev, "Rx error %u\n",
				    le32toh(rxd->rx_stat) & RX_ERROR_MASK);
			m_freem(m);
			goto next;
		}

		m->m_pkthdr.len = m->m_len = le16toh(rxd->rx_len);
		m->m_pkthdr.rcvif = ifp;
#ifndef __NO_STRICT_ALIGNMENT
		txp_fixup_rx(m);
#endif
		rx_stat = le32toh(rxd->rx_stat);
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {
			if ((rx_stat & RX_STAT_IPCKSUMBAD) != 0)
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			else if ((rx_stat & RX_STAT_IPCKSUMGOOD) != 0)
				m->m_pkthdr.csum_flags |=
				    CSUM_IP_CHECKED|CSUM_IP_VALID;

			if ((rx_stat & RX_STAT_TCPCKSUMGOOD) != 0 ||
			    (rx_stat & RX_STAT_UDPCKSUMGOOD) != 0) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		/*
		 * XXX
		 * Typhoon has a firmware bug that VLAN tag is always
		 * stripped out even if it is told to not remove the tag.
		 * Therefore don't check if_capenable here.
		 */
		if (/* (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0 && */
		    (rx_stat & RX_STAT_VLAN) != 0) {
			m->m_pkthdr.ether_vtag =
			    bswap16((le32toh(rxd->rx_vlan) >> 16));
			m->m_flags |= M_VLANTAG;
		}

		TXP_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		TXP_LOCK(sc);

next:
		roff += sizeof(struct txp_rx_desc);
		if (roff == (RX_ENTRIES * sizeof(struct txp_rx_desc))) {
			roff = 0;
			rxd = r->r_desc;
		} else
			rxd++;
		prog++;
	}

	if (prog == 0)
		return (0);

	bus_dmamap_sync(r->r_tag, r->r_map, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	*r->r_roff = le32toh(roff);

	return (count > 0 ? 0 : EAGAIN);
}

static void
txp_rxbuf_reclaim(struct txp_softc *sc)
{
	struct txp_hostvar *hv;
	struct txp_rxbuf_desc *rbd;
	struct txp_rx_swdesc *sd;
	bus_dma_segment_t segs[1];
	int nsegs, prod, prog;
	uint32_t cons;

	TXP_LOCK_ASSERT(sc);

	hv = sc->sc_hostvar;
	cons = TXP_OFFSET2IDX(le32toh(hv->hv_rx_buf_read_idx));
	prod = sc->sc_rxbufprod;
	TXP_DESC_INC(prod, RXBUF_ENTRIES);
	if (prod == cons)
		return;

	bus_dmamap_sync(sc->sc_cdata.txp_rxbufs_tag,
	    sc->sc_cdata.txp_rxbufs_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; prod != cons; prog++) {
		sd = TAILQ_FIRST(&sc->sc_free_list);
		if (sd == NULL)
			break;
		rbd = sc->sc_rxbufs + prod;
		bcopy((u_long *)&rbd->rb_vaddrlo, &sd, sizeof(sd));
		sd->sd_mbuf = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (sd->sd_mbuf == NULL)
			break;
		sd->sd_mbuf->m_pkthdr.len = sd->sd_mbuf->m_len = MCLBYTES;
#ifndef __NO_STRICT_ALIGNMENT
		m_adj(sd->sd_mbuf, TXP_RXBUF_ALIGN);
#endif
		if (bus_dmamap_load_mbuf_sg(sc->sc_cdata.txp_rx_tag,
		    sd->sd_map, sd->sd_mbuf, segs, &nsegs, 0) != 0) {
			m_freem(sd->sd_mbuf);
			sd->sd_mbuf = NULL;
			break;
		}
		KASSERT(nsegs == 1, ("%s : %d segments returned!", __func__,
		    nsegs));
		TAILQ_REMOVE(&sc->sc_free_list, sd, sd_next);
		TAILQ_INSERT_TAIL(&sc->sc_busy_list, sd, sd_next);
		bus_dmamap_sync(sc->sc_cdata.txp_rx_tag, sd->sd_map,
		    BUS_DMASYNC_PREREAD);
		rbd->rb_paddrlo = htole32(TXP_ADDR_LO(segs[0].ds_addr));
		rbd->rb_paddrhi = htole32(TXP_ADDR_HI(segs[0].ds_addr));
		TXP_DESC_INC(prod, RXBUF_ENTRIES);
	}

	if (prog == 0)
		return;
	bus_dmamap_sync(sc->sc_cdata.txp_rxbufs_tag,
	    sc->sc_cdata.txp_rxbufs_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	prod = (prod + RXBUF_ENTRIES - 1) % RXBUF_ENTRIES;
	sc->sc_rxbufprod = prod;
	hv->hv_rx_buf_write_idx = htole32(TXP_IDX2OFFSET(prod));
}

/*
 * Reclaim mbufs and entries from a transmit ring.
 */
static void
txp_tx_reclaim(struct txp_softc *sc, struct txp_tx_ring *r)
{
	struct ifnet *ifp;
	uint32_t idx;
	uint32_t cons, cnt;
	struct txp_tx_desc *txd;
	struct txp_swdesc *sd;

	TXP_LOCK_ASSERT(sc);

	bus_dmamap_sync(r->r_tag, r->r_map, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);
	ifp = sc->sc_ifp;
	idx = TXP_OFFSET2IDX(le32toh(*(r->r_off)));
	cons = r->r_cons;
	cnt = r->r_cnt;
	txd = r->r_desc + cons;
	sd = sc->sc_txd + cons;

	for (cnt = r->r_cnt; cons != idx && cnt > 0; cnt--) {
		if ((txd->tx_flags & TX_FLAGS_TYPE_M) == TX_FLAGS_TYPE_DATA) {
			if (sd->sd_mbuf != NULL) {
				bus_dmamap_sync(sc->sc_cdata.txp_tx_tag,
				    sd->sd_map, BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->sc_cdata.txp_tx_tag,
				    sd->sd_map);
				m_freem(sd->sd_mbuf);
				sd->sd_mbuf = NULL;
				txd->tx_addrlo = 0;
				txd->tx_addrhi = 0;
				txd->tx_flags = 0;
			}
		}
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		if (++cons == TX_ENTRIES) {
			txd = r->r_desc;
			cons = 0;
			sd = sc->sc_txd;
		} else {
			txd++;
			sd++;
		}
	}

	bus_dmamap_sync(r->r_tag, r->r_map, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	r->r_cons = cons;
	r->r_cnt = cnt;
	if (cnt == 0)
		sc->sc_watchdog_timer = 0;
}

static int
txp_shutdown(device_t dev)
{

	return (txp_suspend(dev));
}

static int
txp_suspend(device_t dev)
{
	struct txp_softc *sc;
	struct ifnet *ifp;
	uint8_t *eaddr;
	uint16_t p1;
	uint32_t p2;
	int pmc;
	uint16_t pmstat;

	sc = device_get_softc(dev);

	TXP_LOCK(sc);
	ifp = sc->sc_ifp;
	txp_stop(sc);
	txp_init_rings(sc);
	/* Reset controller and make it reload sleep image. */
	txp_reset(sc);
	/* Let controller boot from sleep image. */
	if (txp_boot(sc, STAT_WAITING_FOR_HOST_REQUEST) != 0)
		device_printf(sc->sc_dev, "couldn't boot sleep image\n");

	/* Set station address. */
	eaddr = IF_LLADDR(sc->sc_ifp);
	p1 = 0;
	((uint8_t *)&p1)[1] = eaddr[0];
	((uint8_t *)&p1)[0] = eaddr[1];
	p1 = le16toh(p1);
	((uint8_t *)&p2)[3] = eaddr[2];
	((uint8_t *)&p2)[2] = eaddr[3];
	((uint8_t *)&p2)[1] = eaddr[4];
	((uint8_t *)&p2)[0] = eaddr[5];
	p2 = le32toh(p2);
	txp_command(sc, TXP_CMD_STATION_ADDRESS_WRITE, p1, p2, 0, NULL, NULL,
	    NULL, TXP_CMD_WAIT);
	txp_set_filter(sc);
	WRITE_REG(sc, TXP_IER, TXP_INTR_NONE);
	WRITE_REG(sc, TXP_IMR, TXP_INTR_ALL);
	txp_sleep(sc, sc->sc_ifp->if_capenable);
	if (pci_find_cap(sc->sc_dev, PCIY_PMG, &pmc) == 0) {
		/* Request PME. */
		pmstat = pci_read_config(sc->sc_dev,
		    pmc + PCIR_POWER_STATUS, 2);
		pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
		if ((ifp->if_capenable & IFCAP_WOL) != 0)
			pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
		pci_write_config(sc->sc_dev,
		    pmc + PCIR_POWER_STATUS, pmstat, 2);
	}
	TXP_UNLOCK(sc);

	return (0);
}

static int
txp_resume(device_t dev)
{
	struct txp_softc *sc;
	int pmc;
	uint16_t pmstat;

	sc = device_get_softc(dev);

	TXP_LOCK(sc);
	if (pci_find_cap(sc->sc_dev, PCIY_PMG, &pmc) == 0) {
		/* Disable PME and clear PME status. */
		pmstat = pci_read_config(sc->sc_dev,
		    pmc + PCIR_POWER_STATUS, 2);
		if ((pmstat & PCIM_PSTAT_PMEENABLE) != 0) {
			pmstat &= ~PCIM_PSTAT_PMEENABLE;
			pci_write_config(sc->sc_dev,
			    pmc + PCIR_POWER_STATUS, pmstat, 2);
		}
	}
	if ((sc->sc_ifp->if_flags & IFF_UP) != 0)
		txp_init_locked(sc);
	TXP_UNLOCK(sc);

	return (0);
}

struct txp_dmamap_arg {
	bus_addr_t	txp_busaddr;
};

static void
txp_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct txp_dmamap_arg *ctx;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	ctx = (struct txp_dmamap_arg *)arg;
	ctx->txp_busaddr = segs[0].ds_addr;
}

static int
txp_dma_alloc(struct txp_softc *sc, char *type, bus_dma_tag_t *tag,
    bus_size_t alignment, bus_size_t boundary, bus_dmamap_t *map, void **buf,
    bus_size_t size, bus_addr_t *paddr)
{
	struct txp_dmamap_arg ctx;
	int error;

	/* Create DMA block tag. */
	error = bus_dma_tag_create(
	    sc->sc_cdata.txp_parent_tag,	/* parent */
	    alignment, boundary,	/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    size,			/* maxsize */
	    1,				/* nsegments */
	    size,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    tag);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not create DMA tag for %s.\n", type);
		return (error);
	}

	*paddr = 0;
	/* Allocate DMA'able memory and load the DMA map. */
	error = bus_dmamem_alloc(*tag, buf, BUS_DMA_WAITOK | BUS_DMA_ZERO |
	    BUS_DMA_COHERENT, map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate DMA'able memory for %s.\n", type);
		return (error);
	}

	ctx.txp_busaddr = 0;
	error = bus_dmamap_load(*tag, *map, *(uint8_t **)buf,
	    size, txp_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0 || ctx.txp_busaddr == 0) {
		device_printf(sc->sc_dev,
		    "could not load DMA'able memory for %s.\n", type);
		return (error);
	}
	*paddr = ctx.txp_busaddr;

	return (0);
}

static void
txp_dma_free(struct txp_softc *sc, bus_dma_tag_t *tag, bus_dmamap_t map,
    void **buf, bus_addr_t *paddr)
{

	if (*tag != NULL) {
		if (*paddr != 0)
			bus_dmamap_unload(*tag, map);
		if (buf != NULL)
			bus_dmamem_free(*tag, *(uint8_t **)buf, map);
		*(uint8_t **)buf = NULL;
		*paddr = 0;
		bus_dma_tag_destroy(*tag);
		*tag = NULL;
	}
}

static int
txp_alloc_rings(struct txp_softc *sc)
{
	struct txp_boot_record *boot;
	struct txp_ldata *ld;
	struct txp_swdesc *txd;
	struct txp_rxbuf_desc *rbd;
	struct txp_rx_swdesc *sd;
	int error, i;

	ld = &sc->sc_ldata;
	boot = ld->txp_boot;

	/* boot record */
	sc->sc_boot = boot;

	/*
	 * Create parent ring/DMA block tag.
	 * Datasheet says that all ring addresses and descriptors
	 * support 64bits addressing. However the controller is
	 * known to have no support DAC so limit DMA address space
	 * to 32bits.
	 */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->sc_dev), /* parent */
	    1, 0,			/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sc_cdata.txp_parent_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create parent DMA tag.\n");
		return (error);
	}

	/* Boot record. */
	error = txp_dma_alloc(sc, "boot record",
	    &sc->sc_cdata.txp_boot_tag, sizeof(uint32_t), 0,
	    &sc->sc_cdata.txp_boot_map, (void **)&sc->sc_ldata.txp_boot,
	    sizeof(struct txp_boot_record),
	    &sc->sc_ldata.txp_boot_paddr);
	if (error != 0)
		return (error);
	boot = sc->sc_ldata.txp_boot;
	sc->sc_boot = boot;

	/* Host variables. */
	error = txp_dma_alloc(sc, "host variables",
	    &sc->sc_cdata.txp_hostvar_tag, sizeof(uint32_t), 0,
	    &sc->sc_cdata.txp_hostvar_map, (void **)&sc->sc_ldata.txp_hostvar,
	    sizeof(struct txp_hostvar),
	    &sc->sc_ldata.txp_hostvar_paddr);
	if (error != 0)
		return (error);
	boot->br_hostvar_lo =
	    htole32(TXP_ADDR_LO(sc->sc_ldata.txp_hostvar_paddr));
	boot->br_hostvar_hi =
	    htole32(TXP_ADDR_HI(sc->sc_ldata.txp_hostvar_paddr));
	sc->sc_hostvar = sc->sc_ldata.txp_hostvar;

	/* Hi priority tx ring. */
	error = txp_dma_alloc(sc, "hi priority tx ring",
	    &sc->sc_cdata.txp_txhiring_tag, sizeof(struct txp_tx_desc), 0,
	    &sc->sc_cdata.txp_txhiring_map, (void **)&sc->sc_ldata.txp_txhiring,
	    sizeof(struct txp_tx_desc) * TX_ENTRIES,
	    &sc->sc_ldata.txp_txhiring_paddr);
	if (error != 0)
		return (error);
	boot->br_txhipri_lo =
	    htole32(TXP_ADDR_LO(sc->sc_ldata.txp_txhiring_paddr));
	boot->br_txhipri_hi =
	    htole32(TXP_ADDR_HI(sc->sc_ldata.txp_txhiring_paddr));
	boot->br_txhipri_siz =
	    htole32(TX_ENTRIES * sizeof(struct txp_tx_desc));
	sc->sc_txhir.r_tag = sc->sc_cdata.txp_txhiring_tag;
	sc->sc_txhir.r_map = sc->sc_cdata.txp_txhiring_map;
	sc->sc_txhir.r_reg = TXP_H2A_1;
	sc->sc_txhir.r_desc = sc->sc_ldata.txp_txhiring;
	sc->sc_txhir.r_cons = sc->sc_txhir.r_prod = sc->sc_txhir.r_cnt = 0;
	sc->sc_txhir.r_off = &sc->sc_hostvar->hv_tx_hi_desc_read_idx;

	/* Low priority tx ring. */
	error = txp_dma_alloc(sc, "low priority tx ring",
	    &sc->sc_cdata.txp_txloring_tag, sizeof(struct txp_tx_desc), 0,
	    &sc->sc_cdata.txp_txloring_map, (void **)&sc->sc_ldata.txp_txloring,
	    sizeof(struct txp_tx_desc) * TX_ENTRIES,
	    &sc->sc_ldata.txp_txloring_paddr);
	if (error != 0)
		return (error);
	boot->br_txlopri_lo =
	    htole32(TXP_ADDR_LO(sc->sc_ldata.txp_txloring_paddr));
	boot->br_txlopri_hi =
	    htole32(TXP_ADDR_HI(sc->sc_ldata.txp_txloring_paddr));
	boot->br_txlopri_siz =
	    htole32(TX_ENTRIES * sizeof(struct txp_tx_desc));
	sc->sc_txlor.r_tag = sc->sc_cdata.txp_txloring_tag;
	sc->sc_txlor.r_map = sc->sc_cdata.txp_txloring_map;
	sc->sc_txlor.r_reg = TXP_H2A_3;
	sc->sc_txlor.r_desc = sc->sc_ldata.txp_txloring;
	sc->sc_txlor.r_cons = sc->sc_txlor.r_prod = sc->sc_txlor.r_cnt = 0;
	sc->sc_txlor.r_off = &sc->sc_hostvar->hv_tx_lo_desc_read_idx;

	/* High priority rx ring. */
	error = txp_dma_alloc(sc, "hi priority rx ring",
	    &sc->sc_cdata.txp_rxhiring_tag,
	    roundup(sizeof(struct txp_rx_desc), 16), 0,
	    &sc->sc_cdata.txp_rxhiring_map, (void **)&sc->sc_ldata.txp_rxhiring,
	    sizeof(struct txp_rx_desc) * RX_ENTRIES,
	    &sc->sc_ldata.txp_rxhiring_paddr);
	if (error != 0)
		return (error);
	boot->br_rxhipri_lo =
	    htole32(TXP_ADDR_LO(sc->sc_ldata.txp_rxhiring_paddr));
	boot->br_rxhipri_hi =
	    htole32(TXP_ADDR_HI(sc->sc_ldata.txp_rxhiring_paddr));
	boot->br_rxhipri_siz =
	    htole32(RX_ENTRIES * sizeof(struct txp_rx_desc));
	sc->sc_rxhir.r_tag = sc->sc_cdata.txp_rxhiring_tag;
	sc->sc_rxhir.r_map = sc->sc_cdata.txp_rxhiring_map;
	sc->sc_rxhir.r_desc = sc->sc_ldata.txp_rxhiring;
	sc->sc_rxhir.r_roff = &sc->sc_hostvar->hv_rx_hi_read_idx;
	sc->sc_rxhir.r_woff = &sc->sc_hostvar->hv_rx_hi_write_idx;

	/* Low priority rx ring. */
	error = txp_dma_alloc(sc, "low priority rx ring",
	    &sc->sc_cdata.txp_rxloring_tag,
	    roundup(sizeof(struct txp_rx_desc), 16), 0,
	    &sc->sc_cdata.txp_rxloring_map, (void **)&sc->sc_ldata.txp_rxloring,
	    sizeof(struct txp_rx_desc) * RX_ENTRIES,
	    &sc->sc_ldata.txp_rxloring_paddr);
	if (error != 0)
		return (error);
	boot->br_rxlopri_lo =
	    htole32(TXP_ADDR_LO(sc->sc_ldata.txp_rxloring_paddr));
	boot->br_rxlopri_hi =
	    htole32(TXP_ADDR_HI(sc->sc_ldata.txp_rxloring_paddr));
	boot->br_rxlopri_siz =
	    htole32(RX_ENTRIES * sizeof(struct txp_rx_desc));
	sc->sc_rxlor.r_tag = sc->sc_cdata.txp_rxloring_tag;
	sc->sc_rxlor.r_map = sc->sc_cdata.txp_rxloring_map;
	sc->sc_rxlor.r_desc = sc->sc_ldata.txp_rxloring;
	sc->sc_rxlor.r_roff = &sc->sc_hostvar->hv_rx_lo_read_idx;
	sc->sc_rxlor.r_woff = &sc->sc_hostvar->hv_rx_lo_write_idx;

	/* Command ring. */
	error = txp_dma_alloc(sc, "command ring",
	    &sc->sc_cdata.txp_cmdring_tag, sizeof(struct txp_cmd_desc), 0,
	    &sc->sc_cdata.txp_cmdring_map, (void **)&sc->sc_ldata.txp_cmdring,
	    sizeof(struct txp_cmd_desc) * CMD_ENTRIES,
	    &sc->sc_ldata.txp_cmdring_paddr);
	if (error != 0)
		return (error);
	boot->br_cmd_lo = htole32(TXP_ADDR_LO(sc->sc_ldata.txp_cmdring_paddr));
	boot->br_cmd_hi = htole32(TXP_ADDR_HI(sc->sc_ldata.txp_cmdring_paddr));
	boot->br_cmd_siz = htole32(CMD_ENTRIES * sizeof(struct txp_cmd_desc));
	sc->sc_cmdring.base = sc->sc_ldata.txp_cmdring;
	sc->sc_cmdring.size = CMD_ENTRIES * sizeof(struct txp_cmd_desc);
	sc->sc_cmdring.lastwrite = 0;

	/* Response ring. */
	error = txp_dma_alloc(sc, "response ring",
	    &sc->sc_cdata.txp_rspring_tag, sizeof(struct txp_rsp_desc), 0,
	    &sc->sc_cdata.txp_rspring_map, (void **)&sc->sc_ldata.txp_rspring,
	    sizeof(struct txp_rsp_desc) * RSP_ENTRIES,
	    &sc->sc_ldata.txp_rspring_paddr);
	if (error != 0)
		return (error);
	boot->br_resp_lo = htole32(TXP_ADDR_LO(sc->sc_ldata.txp_rspring_paddr));
	boot->br_resp_hi = htole32(TXP_ADDR_HI(sc->sc_ldata.txp_rspring_paddr));
	boot->br_resp_siz = htole32(RSP_ENTRIES * sizeof(struct txp_rsp_desc));
	sc->sc_rspring.base = sc->sc_ldata.txp_rspring;
	sc->sc_rspring.size = RSP_ENTRIES * sizeof(struct txp_rsp_desc);
	sc->sc_rspring.lastwrite = 0;

	/* Receive buffer ring. */
	error = txp_dma_alloc(sc, "receive buffer ring",
	    &sc->sc_cdata.txp_rxbufs_tag, sizeof(struct txp_rxbuf_desc), 0,
	    &sc->sc_cdata.txp_rxbufs_map, (void **)&sc->sc_ldata.txp_rxbufs,
	    sizeof(struct txp_rxbuf_desc) * RXBUF_ENTRIES,
	    &sc->sc_ldata.txp_rxbufs_paddr);
	if (error != 0)
		return (error);
	boot->br_rxbuf_lo =
	    htole32(TXP_ADDR_LO(sc->sc_ldata.txp_rxbufs_paddr));
	boot->br_rxbuf_hi =
	    htole32(TXP_ADDR_HI(sc->sc_ldata.txp_rxbufs_paddr));
	boot->br_rxbuf_siz =
	    htole32(RXBUF_ENTRIES * sizeof(struct txp_rxbuf_desc));
	sc->sc_rxbufs = sc->sc_ldata.txp_rxbufs;

	/* Zero ring. */
	error = txp_dma_alloc(sc, "zero buffer",
	    &sc->sc_cdata.txp_zero_tag, sizeof(uint32_t), 0,
	    &sc->sc_cdata.txp_zero_map, (void **)&sc->sc_ldata.txp_zero,
	    sizeof(uint32_t), &sc->sc_ldata.txp_zero_paddr);
	if (error != 0)
		return (error);
	boot->br_zero_lo = htole32(TXP_ADDR_LO(sc->sc_ldata.txp_zero_paddr));
	boot->br_zero_hi = htole32(TXP_ADDR_HI(sc->sc_ldata.txp_zero_paddr));

	bus_dmamap_sync(sc->sc_cdata.txp_boot_tag, sc->sc_cdata.txp_boot_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Create Tx buffers. */
	error = bus_dma_tag_create(
	    sc->sc_cdata.txp_parent_tag,	/* parent */
	    1, 0,			/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * TXP_MAXTXSEGS,	/* maxsize */
	    TXP_MAXTXSEGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sc_cdata.txp_tx_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create Tx DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(
	    sc->sc_cdata.txp_parent_tag,	/* parent */
	    TXP_RXBUF_ALIGN, 0,		/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sc_cdata.txp_rx_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create Rx DMA tag.\n");
		goto fail;
	}

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < TX_ENTRIES; i++) {
		txd = &sc->sc_txd[i];
		txd->sd_mbuf = NULL;
		txd->sd_map = NULL;
		error = bus_dmamap_create(sc->sc_cdata.txp_tx_tag, 0,
		    &txd->sd_map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create Tx dmamap.\n");
			goto fail;
		}
	}

	/* Create DMA maps for Rx buffers. */
	for (i = 0; i < RXBUF_ENTRIES; i++) {
		sd = malloc(sizeof(struct txp_rx_swdesc), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (sd == NULL) {
			error = ENOMEM;
			goto fail;
		}
		/*
		 * The virtual address part of descriptor is not used
		 * by hardware so use that to save an ring entry. We
		 * need bcopy here otherwise the address wouldn't be
		 * valid on big-endian architectures.
		 */
		rbd = sc->sc_rxbufs + i;
		bcopy(&sd, (u_long *)&rbd->rb_vaddrlo, sizeof(sd));
		sd->sd_mbuf = NULL;
		sd->sd_map = NULL;
		error = bus_dmamap_create(sc->sc_cdata.txp_rx_tag, 0,
		    &sd->sd_map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create Rx dmamap.\n");
			goto fail;
		}
		TAILQ_INSERT_TAIL(&sc->sc_free_list, sd, sd_next);
	}

fail:
	return (error);
}

static void
txp_init_rings(struct txp_softc *sc)
{

	bzero(sc->sc_ldata.txp_hostvar, sizeof(struct txp_hostvar));
	bzero(sc->sc_ldata.txp_zero, sizeof(uint32_t));
	sc->sc_txhir.r_cons = 0;
	sc->sc_txhir.r_prod = 0;
	sc->sc_txhir.r_cnt = 0;
	sc->sc_txlor.r_cons = 0;
	sc->sc_txlor.r_prod = 0;
	sc->sc_txlor.r_cnt = 0;
	sc->sc_cmdring.lastwrite = 0;
	sc->sc_rspring.lastwrite = 0;
	sc->sc_rxbufprod = 0;
	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
txp_wait(struct txp_softc *sc, uint32_t state)
{
	uint32_t reg;
	int i;

	for (i = 0; i < TXP_TIMEOUT; i++) {
		reg = READ_REG(sc, TXP_A2H_0);
		if (reg == state)
			break;
		DELAY(50);
	}

	return (i == TXP_TIMEOUT ? ETIMEDOUT : 0);
}

static void
txp_free_rings(struct txp_softc *sc)
{
	struct txp_swdesc *txd;
	struct txp_rx_swdesc *sd;
	int i;

	/* Tx buffers. */
	if (sc->sc_cdata.txp_tx_tag != NULL) {
		for (i = 0; i < TX_ENTRIES; i++) {
			txd = &sc->sc_txd[i];
			if (txd->sd_map != NULL) {
				bus_dmamap_destroy(sc->sc_cdata.txp_tx_tag,
				    txd->sd_map);
				txd->sd_map = NULL;
			}
		}
		bus_dma_tag_destroy(sc->sc_cdata.txp_tx_tag);
		sc->sc_cdata.txp_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc->sc_cdata.txp_rx_tag != NULL) {
		if (sc->sc_rxbufs != NULL) {
			KASSERT(TAILQ_FIRST(&sc->sc_busy_list) == NULL,
			    ("%s : still have busy Rx buffers", __func__));
			while ((sd = TAILQ_FIRST(&sc->sc_free_list)) != NULL) {
				TAILQ_REMOVE(&sc->sc_free_list, sd, sd_next);
				if (sd->sd_map != NULL) {
					bus_dmamap_destroy(
					    sc->sc_cdata.txp_rx_tag,
					    sd->sd_map);
					sd->sd_map = NULL;
				}
				free(sd, M_DEVBUF);
			}
		}
		bus_dma_tag_destroy(sc->sc_cdata.txp_rx_tag);
		sc->sc_cdata.txp_rx_tag = NULL;
	}

	/* Hi priority Tx ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_txhiring_tag,
	    sc->sc_cdata.txp_txhiring_map,
	    (void **)&sc->sc_ldata.txp_txhiring,
	    &sc->sc_ldata.txp_txhiring_paddr);
	/* Low priority Tx ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_txloring_tag,
	    sc->sc_cdata.txp_txloring_map,
	    (void **)&sc->sc_ldata.txp_txloring,
	    &sc->sc_ldata.txp_txloring_paddr);
	/* Hi priority Rx ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_rxhiring_tag,
	    sc->sc_cdata.txp_rxhiring_map,
	    (void **)&sc->sc_ldata.txp_rxhiring,
	    &sc->sc_ldata.txp_rxhiring_paddr);
	/* Low priority Rx ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_rxloring_tag,
	    sc->sc_cdata.txp_rxloring_map,
	    (void **)&sc->sc_ldata.txp_rxloring,
	    &sc->sc_ldata.txp_rxloring_paddr);
	/* Receive buffer ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_rxbufs_tag,
	    sc->sc_cdata.txp_rxbufs_map, (void **)&sc->sc_ldata.txp_rxbufs,
	    &sc->sc_ldata.txp_rxbufs_paddr);
	/* Command ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_cmdring_tag,
	    sc->sc_cdata.txp_cmdring_map, (void **)&sc->sc_ldata.txp_cmdring,
	    &sc->sc_ldata.txp_cmdring_paddr);
	/* Response ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_rspring_tag,
	    sc->sc_cdata.txp_rspring_map, (void **)&sc->sc_ldata.txp_rspring,
	    &sc->sc_ldata.txp_rspring_paddr);
	/* Zero ring. */
	txp_dma_free(sc, &sc->sc_cdata.txp_zero_tag,
	    sc->sc_cdata.txp_zero_map, (void **)&sc->sc_ldata.txp_zero,
	    &sc->sc_ldata.txp_zero_paddr);
	/* Host variables. */
	txp_dma_free(sc, &sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map, (void **)&sc->sc_ldata.txp_hostvar,
	    &sc->sc_ldata.txp_hostvar_paddr);
	/* Boot record. */
	txp_dma_free(sc, &sc->sc_cdata.txp_boot_tag,
	    sc->sc_cdata.txp_boot_map, (void **)&sc->sc_ldata.txp_boot,
	    &sc->sc_ldata.txp_boot_paddr);

	if (sc->sc_cdata.txp_parent_tag != NULL) {
		bus_dma_tag_destroy(sc->sc_cdata.txp_parent_tag);
		sc->sc_cdata.txp_parent_tag = NULL;
	}

}

static int
txp_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct txp_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int capenable, error = 0, mask;

	switch(command) {
	case SIOCSIFFLAGS:
		TXP_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if (((ifp->if_flags ^ sc->sc_if_flags)
				    & (IFF_PROMISC | IFF_ALLMULTI)) != 0)
					txp_set_filter(sc);
			} else {
				if ((sc->sc_flags & TXP_FLAG_DETACH) == 0)
					txp_init_locked(sc);
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				txp_stop(sc);
		}
		sc->sc_if_flags = ifp->if_flags;
		TXP_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * Multicast list has changed; set the hardware
		 * filter accordingly.
		 */
		TXP_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			txp_set_filter(sc);
		TXP_UNLOCK(sc);
		break;
	case SIOCSIFCAP:
		TXP_LOCK(sc);
		capenable = ifp->if_capenable;
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_TXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= TXP_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~TXP_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_RXCSUM) != 0)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_MAGIC) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWCSUM) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((ifp->if_capenable & IFCAP_TXCSUM) == 0)
			ifp->if_capenable &= ~IFCAP_VLAN_HWCSUM;
		if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) == 0)
			ifp->if_capenable &= ~IFCAP_VLAN_HWCSUM;
		if (capenable != ifp->if_capenable)
			txp_set_capabilities(sc);
		TXP_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_ifmedia, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static int
txp_rxring_fill(struct txp_softc *sc)
{
	struct txp_rxbuf_desc *rbd;
	struct txp_rx_swdesc *sd;
	bus_dma_segment_t segs[1];
	int error, i, nsegs;

	TXP_LOCK_ASSERT(sc);

	bus_dmamap_sync(sc->sc_cdata.txp_rxbufs_tag,
	    sc->sc_cdata.txp_rxbufs_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (i = 0; i < RXBUF_ENTRIES; i++) {
		sd = TAILQ_FIRST(&sc->sc_free_list);
		if (sd == NULL)
			return (ENOMEM);
		rbd = sc->sc_rxbufs + i;
		bcopy(&sd, (u_long *)&rbd->rb_vaddrlo, sizeof(sd));
		KASSERT(sd->sd_mbuf == NULL,
		    ("%s : Rx buffer ring corrupted", __func__));
		sd->sd_mbuf = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (sd->sd_mbuf == NULL)
			return (ENOMEM);
		sd->sd_mbuf->m_pkthdr.len = sd->sd_mbuf->m_len = MCLBYTES;
#ifndef __NO_STRICT_ALIGNMENT
		m_adj(sd->sd_mbuf, TXP_RXBUF_ALIGN);
#endif
		if ((error = bus_dmamap_load_mbuf_sg(sc->sc_cdata.txp_rx_tag,
		    sd->sd_map, sd->sd_mbuf, segs, &nsegs, 0)) != 0) {
			m_freem(sd->sd_mbuf);
			sd->sd_mbuf = NULL;
			return (error);
		}
		KASSERT(nsegs == 1, ("%s : %d segments returned!", __func__,
		    nsegs));
		TAILQ_REMOVE(&sc->sc_free_list, sd, sd_next);
		TAILQ_INSERT_TAIL(&sc->sc_busy_list, sd, sd_next);
		bus_dmamap_sync(sc->sc_cdata.txp_rx_tag, sd->sd_map,
		    BUS_DMASYNC_PREREAD);
		rbd->rb_paddrlo = htole32(TXP_ADDR_LO(segs[0].ds_addr));
		rbd->rb_paddrhi = htole32(TXP_ADDR_HI(segs[0].ds_addr));
	}

	bus_dmamap_sync(sc->sc_cdata.txp_rxbufs_tag,
	    sc->sc_cdata.txp_rxbufs_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	sc->sc_rxbufprod = RXBUF_ENTRIES - 1;
	sc->sc_hostvar->hv_rx_buf_write_idx =
	    htole32(TXP_IDX2OFFSET(RXBUF_ENTRIES - 1));

	return (0);
}

static void
txp_rxring_empty(struct txp_softc *sc)
{
	struct txp_rx_swdesc *sd;
	int cnt;

	TXP_LOCK_ASSERT(sc);

	if (sc->sc_rxbufs == NULL)
		return;
	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/* Release allocated Rx buffers. */
	cnt = 0;
	while ((sd = TAILQ_FIRST(&sc->sc_busy_list)) != NULL) {
		TAILQ_REMOVE(&sc->sc_busy_list, sd, sd_next);
		KASSERT(sd->sd_mbuf != NULL,
		    ("%s : Rx buffer ring corrupted", __func__));
		bus_dmamap_sync(sc->sc_cdata.txp_rx_tag, sd->sd_map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_cdata.txp_rx_tag, sd->sd_map);
		m_freem(sd->sd_mbuf);
		sd->sd_mbuf = NULL;
		TAILQ_INSERT_TAIL(&sc->sc_free_list, sd, sd_next);
		cnt++;
	}
}

static void
txp_init(void *xsc)
{
	struct txp_softc *sc;

	sc = xsc;
	TXP_LOCK(sc);
	txp_init_locked(sc);
	TXP_UNLOCK(sc);
}

static void
txp_init_locked(struct txp_softc *sc)
{
	struct ifnet *ifp;
	uint8_t *eaddr;
	uint16_t p1;
	uint32_t p2;
	int error;

	TXP_LOCK_ASSERT(sc);
	ifp = sc->sc_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/* Initialize ring structure. */
	txp_init_rings(sc);
	/* Wakeup controller. */
	WRITE_REG(sc, TXP_H2A_0, TXP_BOOTCMD_WAKEUP);
	TXP_BARRIER(sc, TXP_H2A_0, 4, BUS_SPACE_BARRIER_WRITE);
	/*
	 * It seems that earlier NV image can go back to online from
	 * wakeup command but newer ones require controller reset.
	 * So jut reset controller again.
	 */
	if (txp_reset(sc) != 0)
		goto init_fail;
	/* Download firmware. */
	error = txp_download_fw(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not download firmware.\n");
		goto init_fail;
	}
	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	if ((error = txp_rxring_fill(sc)) != 0) {
		device_printf(sc->sc_dev, "no memory for Rx buffers.\n");
		goto init_fail;
	}
	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	if (txp_boot(sc, STAT_WAITING_FOR_BOOT) != 0) {
		device_printf(sc->sc_dev, "could not boot firmware.\n");
		goto init_fail;
	}

	/*
	 * Quite contrary to Typhoon T2 software functional specification,
	 * it seems that TXP_CMD_RECV_BUFFER_CONTROL command is not
	 * implemented in the firmware. This means driver should have to
	 * handle misaligned frames on alignment architectures. AFAIK this
	 * is the only controller manufactured by 3Com that has this stupid
	 * bug. 3Com should fix this.
	 */
	if (txp_command(sc, TXP_CMD_MAX_PKT_SIZE_WRITE, TXP_MAX_PKTLEN, 0, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT) != 0)
		goto init_fail;
	/* Undocumented command(interrupt coalescing disable?) - From Linux. */
	if (txp_command(sc, TXP_CMD_FILTER_DEFINE, 0, 0, 0, NULL, NULL, NULL,
	    TXP_CMD_NOWAIT) != 0)
		goto init_fail;

	/* Set station address. */
	eaddr = IF_LLADDR(sc->sc_ifp);
	p1 = 0;
	((uint8_t *)&p1)[1] = eaddr[0];
	((uint8_t *)&p1)[0] = eaddr[1];
	p1 = le16toh(p1);
	((uint8_t *)&p2)[3] = eaddr[2];
	((uint8_t *)&p2)[2] = eaddr[3];
	((uint8_t *)&p2)[1] = eaddr[4];
	((uint8_t *)&p2)[0] = eaddr[5];
	p2 = le32toh(p2);
	if (txp_command(sc, TXP_CMD_STATION_ADDRESS_WRITE, p1, p2, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT) != 0)
		goto init_fail;

	txp_set_filter(sc);
	txp_set_capabilities(sc);

	if (txp_command(sc, TXP_CMD_CLEAR_STATISTICS, 0, 0, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT))
		goto init_fail;
	if (txp_command(sc, TXP_CMD_XCVR_SELECT, sc->sc_xcvr, 0, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT) != 0)
		goto init_fail;
	if (txp_command(sc, TXP_CMD_TX_ENABLE, 0, 0, 0, NULL, NULL, NULL,
	    TXP_CMD_NOWAIT) != 0)
		goto init_fail;
	if (txp_command(sc, TXP_CMD_RX_ENABLE, 0, 0, 0, NULL, NULL, NULL,
	    TXP_CMD_NOWAIT) != 0)
		goto init_fail;

	/* Ack all pending interrupts and enable interrupts. */
	WRITE_REG(sc, TXP_ISR, TXP_INTR_ALL);
	WRITE_REG(sc, TXP_IER, TXP_INTRS);
	WRITE_REG(sc, TXP_IMR, TXP_INTR_NONE);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->sc_tick, hz, txp_tick, sc);
	return;

init_fail:
	txp_rxring_empty(sc);
	txp_init_rings(sc);
	txp_reset(sc);
	WRITE_REG(sc, TXP_IMR, TXP_INTR_ALL);
}

static void
txp_tick(void *vsc)
{
	struct txp_softc *sc;
	struct ifnet *ifp;
	struct txp_rsp_desc *rsp;
	struct txp_ext_desc *ext;
	int link;

	sc = vsc;
	TXP_LOCK_ASSERT(sc);
	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	txp_rxbuf_reclaim(sc);
	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	ifp = sc->sc_ifp;
	rsp = NULL;

	link = sc->sc_flags & TXP_FLAG_LINK;
	if (txp_ext_command(sc, TXP_CMD_READ_STATISTICS, 0, 0, 0, NULL, 0,
	    &rsp, TXP_CMD_WAIT))
		goto out;
	if (rsp->rsp_numdesc != 6)
		goto out;
	txp_stats_update(sc, rsp);
	if (link == 0 && (sc->sc_flags & TXP_FLAG_LINK) != 0) {
		ext = (struct txp_ext_desc *)(rsp + 1);
		/* Update baudrate with resolved speed. */
		if ((ext[5].ext_2 & 0x02) != 0)
			ifp->if_baudrate = IF_Mbps(100);
		else
			ifp->if_baudrate = IF_Mbps(10);
	}

out:
	if (rsp != NULL)
		free(rsp, M_DEVBUF);
	txp_watchdog(sc);
	callout_reset(&sc->sc_tick, hz, txp_tick, sc);
}

static void
txp_start(struct ifnet *ifp)
{
	struct txp_softc *sc;

	sc = ifp->if_softc;
	TXP_LOCK(sc);
	txp_start_locked(ifp);
	TXP_UNLOCK(sc);
}

static void
txp_start_locked(struct ifnet *ifp)
{
	struct txp_softc *sc;
	struct mbuf *m_head;
	int enq;

	sc = ifp->if_softc;
	TXP_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	   IFF_DRV_RUNNING || (sc->sc_flags & TXP_FLAG_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 * ATM only Hi-ring is used.
		 */
		if (txp_encap(sc, &sc->sc_txhir, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);

		/* Send queued frame. */
		WRITE_REG(sc, sc->sc_txhir.r_reg,
		    TXP_IDX2OFFSET(sc->sc_txhir.r_prod));
	}

	if (enq > 0) {
		/* Set a timeout in case the chip goes out to lunch. */
		sc->sc_watchdog_timer = TXP_TX_TIMEOUT;
	}
}

static int
txp_encap(struct txp_softc *sc, struct txp_tx_ring *r, struct mbuf **m_head)
{
	struct txp_tx_desc *first_txd;
	struct txp_frag_desc *fxd;
	struct txp_swdesc *sd;
	struct mbuf *m;
	bus_dma_segment_t txsegs[TXP_MAXTXSEGS];
	int error, i, nsegs;

	TXP_LOCK_ASSERT(sc);

	M_ASSERTPKTHDR((*m_head));

	m = *m_head;
	first_txd = r->r_desc + r->r_prod;
	sd = sc->sc_txd + r->r_prod;

	error = bus_dmamap_load_mbuf_sg(sc->sc_cdata.txp_tx_tag, sd->sd_map,
	    *m_head, txsegs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, TXP_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->sc_cdata.txp_tx_tag,
		    sd->sd_map, *m_head, txsegs, &nsegs, 0);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/* Check descriptor overrun. */
	if (r->r_cnt + nsegs >= TX_ENTRIES - TXP_TXD_RESERVED) {
		bus_dmamap_unload(sc->sc_cdata.txp_tx_tag, sd->sd_map);
		return (ENOBUFS);
	}
	bus_dmamap_sync(sc->sc_cdata.txp_tx_tag, sd->sd_map,
	    BUS_DMASYNC_PREWRITE);
	sd->sd_mbuf = m;

	first_txd->tx_flags = TX_FLAGS_TYPE_DATA;
	first_txd->tx_numdesc = 0;
	first_txd->tx_addrlo = 0;
	first_txd->tx_addrhi = 0;
	first_txd->tx_totlen = 0;
	first_txd->tx_pflags = 0;
	r->r_cnt++;
	TXP_DESC_INC(r->r_prod, TX_ENTRIES);

	/* Configure Tx IP/TCP/UDP checksum offload. */
	if ((m->m_pkthdr.csum_flags & CSUM_IP) != 0)
		first_txd->tx_pflags |= htole32(TX_PFLAGS_IPCKSUM);
#ifdef notyet
	/* XXX firmware bug. */
	if ((m->m_pkthdr.csum_flags & CSUM_TCP) != 0)
		first_txd->tx_pflags |= htole32(TX_PFLAGS_TCPCKSUM);
	if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
		first_txd->tx_pflags |= htole32(TX_PFLAGS_UDPCKSUM);
#endif

	/* Configure VLAN hardware tag insertion. */
	if ((m->m_flags & M_VLANTAG) != 0)
		first_txd->tx_pflags |=
		    htole32(TX_PFLAGS_VLAN | TX_PFLAGS_PRIO |
		    (bswap16(m->m_pkthdr.ether_vtag) << TX_PFLAGS_VLANTAG_S));

	for (i = 0; i < nsegs; i++) {
		fxd = (struct txp_frag_desc *)(r->r_desc + r->r_prod);
		fxd->frag_flags = FRAG_FLAGS_TYPE_FRAG | TX_FLAGS_VALID;
		fxd->frag_rsvd1 = 0;
		fxd->frag_len = htole16(txsegs[i].ds_len);
		fxd->frag_addrhi = htole32(TXP_ADDR_HI(txsegs[i].ds_addr));
		fxd->frag_addrlo = htole32(TXP_ADDR_LO(txsegs[i].ds_addr));
		fxd->frag_rsvd2 = 0;
		first_txd->tx_numdesc++;
		r->r_cnt++;
		TXP_DESC_INC(r->r_prod, TX_ENTRIES);
	}

	/* Lastly set valid flag. */
	first_txd->tx_flags |= TX_FLAGS_VALID;

	/* Sync descriptors. */
	bus_dmamap_sync(r->r_tag, r->r_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Handle simple commands sent to the typhoon
 */
static int
txp_command(struct txp_softc *sc, uint16_t id, uint16_t in1, uint32_t in2,
    uint32_t in3, uint16_t *out1, uint32_t *out2, uint32_t *out3, int wait)
{
	struct txp_rsp_desc *rsp;

	rsp = NULL;
	if (txp_ext_command(sc, id, in1, in2, in3, NULL, 0, &rsp, wait) != 0) {
		device_printf(sc->sc_dev, "command 0x%02x failed\n", id);
		return (-1);
	}

	if (wait == TXP_CMD_NOWAIT)
		return (0);

	KASSERT(rsp != NULL, ("rsp is NULL!\n"));
	if (out1 != NULL)
		*out1 = le16toh(rsp->rsp_par1);
	if (out2 != NULL)
		*out2 = le32toh(rsp->rsp_par2);
	if (out3 != NULL)
		*out3 = le32toh(rsp->rsp_par3);
	free(rsp, M_DEVBUF);
	return (0);
}

static int
txp_ext_command(struct txp_softc *sc, uint16_t id, uint16_t in1, uint32_t in2,
    uint32_t in3, struct txp_ext_desc *in_extp, uint8_t in_extn,
    struct txp_rsp_desc **rspp, int wait)
{
	struct txp_hostvar *hv;
	struct txp_cmd_desc *cmd;
	struct txp_ext_desc *ext;
	uint32_t idx, i;
	uint16_t seq;
	int error;

	error = 0;
	hv = sc->sc_hostvar;
	if (txp_cmd_desc_numfree(sc) < (in_extn + 1)) {
		device_printf(sc->sc_dev,
		    "%s : out of free cmd descriptors for command 0x%02x\n",
		    __func__, id);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sc_cdata.txp_cmdring_tag,
	    sc->sc_cdata.txp_cmdring_map, BUS_DMASYNC_POSTWRITE);
	idx = sc->sc_cmdring.lastwrite;
	cmd = (struct txp_cmd_desc *)(((uint8_t *)sc->sc_cmdring.base) + idx);
	bzero(cmd, sizeof(*cmd));

	cmd->cmd_numdesc = in_extn;
	seq = sc->sc_seq++;
	cmd->cmd_seq = htole16(seq);
	cmd->cmd_id = htole16(id);
	cmd->cmd_par1 = htole16(in1);
	cmd->cmd_par2 = htole32(in2);
	cmd->cmd_par3 = htole32(in3);
	cmd->cmd_flags = CMD_FLAGS_TYPE_CMD |
	    (wait == TXP_CMD_WAIT ? CMD_FLAGS_RESP : 0) | CMD_FLAGS_VALID;

	idx += sizeof(struct txp_cmd_desc);
	if (idx == sc->sc_cmdring.size)
		idx = 0;

	for (i = 0; i < in_extn; i++) {
		ext = (struct txp_ext_desc *)(((uint8_t *)sc->sc_cmdring.base) + idx);
		bcopy(in_extp, ext, sizeof(struct txp_ext_desc));
		in_extp++;
		idx += sizeof(struct txp_cmd_desc);
		if (idx == sc->sc_cmdring.size)
			idx = 0;
	}

	sc->sc_cmdring.lastwrite = idx;
	bus_dmamap_sync(sc->sc_cdata.txp_cmdring_tag,
	    sc->sc_cdata.txp_cmdring_map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	WRITE_REG(sc, TXP_H2A_2, sc->sc_cmdring.lastwrite);
	TXP_BARRIER(sc, TXP_H2A_2, 4, BUS_SPACE_BARRIER_WRITE);

	if (wait == TXP_CMD_NOWAIT)
		return (0);

	for (i = 0; i < TXP_TIMEOUT; i++) {
		bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
		    sc->sc_cdata.txp_hostvar_map, BUS_DMASYNC_POSTREAD |
		    BUS_DMASYNC_POSTWRITE);
		if (le32toh(hv->hv_resp_read_idx) !=
		    le32toh(hv->hv_resp_write_idx)) {
			error = txp_response(sc, id, seq, rspp);
			bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
			    sc->sc_cdata.txp_hostvar_map,
			    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
			if (error != 0)
				return (error);
 			if (*rspp != NULL)
				break;
		}
		DELAY(50);
	}
	if (i == TXP_TIMEOUT) {
		device_printf(sc->sc_dev, "command 0x%02x timedout\n", id);
		error = ETIMEDOUT;
	}

	return (error);
}

static int
txp_response(struct txp_softc *sc, uint16_t id, uint16_t seq,
    struct txp_rsp_desc **rspp)
{
	struct txp_hostvar *hv;
	struct txp_rsp_desc *rsp;
	uint32_t ridx;

	bus_dmamap_sync(sc->sc_cdata.txp_rspring_tag,
	    sc->sc_cdata.txp_rspring_map, BUS_DMASYNC_POSTREAD);
	hv = sc->sc_hostvar;
	ridx = le32toh(hv->hv_resp_read_idx);
	while (ridx != le32toh(hv->hv_resp_write_idx)) {
		rsp = (struct txp_rsp_desc *)(((uint8_t *)sc->sc_rspring.base) + ridx);

		if (id == le16toh(rsp->rsp_id) &&
		    le16toh(rsp->rsp_seq) == seq) {
			*rspp = (struct txp_rsp_desc *)malloc(
			    sizeof(struct txp_rsp_desc) * (rsp->rsp_numdesc + 1),
			    M_DEVBUF, M_NOWAIT);
			if (*rspp == NULL) {
				device_printf(sc->sc_dev,"%s : command 0x%02x "
				    "memory allocation failure\n",
				    __func__, id);
				return (ENOMEM);
			}
			txp_rsp_fixup(sc, rsp, *rspp);
			return (0);
		}

		if ((rsp->rsp_flags & RSP_FLAGS_ERROR) != 0) {
			device_printf(sc->sc_dev,
			    "%s : command 0x%02x response error!\n", __func__,
			    le16toh(rsp->rsp_id));
			txp_rsp_fixup(sc, rsp, NULL);
			ridx = le32toh(hv->hv_resp_read_idx);
			continue;
		}

		/*
		 * The following unsolicited responses are handled during
		 * processing of TXP_CMD_READ_STATISTICS which requires
		 * response. Driver abuses the command to detect media
		 * status change.
		 * TXP_CMD_FILTER_DEFINE is not an unsolicited response
		 * but we don't process response ring in interrupt handler
		 * so we have to ignore this command here, otherwise
		 * unknown command message would be printed.
		 */
		switch (le16toh(rsp->rsp_id)) {
		case TXP_CMD_CYCLE_STATISTICS:
		case TXP_CMD_FILTER_DEFINE:
			break;
		case TXP_CMD_MEDIA_STATUS_READ:
			if ((le16toh(rsp->rsp_par1) & 0x0800) == 0) {
				sc->sc_flags |= TXP_FLAG_LINK;
				if_link_state_change(sc->sc_ifp,
				    LINK_STATE_UP);
			} else {
				sc->sc_flags &= ~TXP_FLAG_LINK;
				if_link_state_change(sc->sc_ifp,
				    LINK_STATE_DOWN);
			}
			break;
		case TXP_CMD_HELLO_RESPONSE:
			/*
			 * Driver should repsond to hello message but
			 * TXP_CMD_READ_STATISTICS is issued for every
			 * hz, therefore there is no need to send an
			 * explicit command here.
			 */
			device_printf(sc->sc_dev, "%s : hello\n", __func__);
			break;
		default:
			device_printf(sc->sc_dev,
			    "%s : unknown command 0x%02x\n", __func__,
			    le16toh(rsp->rsp_id));
		}
		txp_rsp_fixup(sc, rsp, NULL);
		ridx = le32toh(hv->hv_resp_read_idx);
	}

	return (0);
}

static void
txp_rsp_fixup(struct txp_softc *sc, struct txp_rsp_desc *rsp,
    struct txp_rsp_desc *dst)
{
	struct txp_rsp_desc *src;
	struct txp_hostvar *hv;
	uint32_t i, ridx;

	src = rsp;
	hv = sc->sc_hostvar;
	ridx = le32toh(hv->hv_resp_read_idx);

	for (i = 0; i < rsp->rsp_numdesc + 1; i++) {
		if (dst != NULL)
			bcopy(src, dst++, sizeof(struct txp_rsp_desc));
		ridx += sizeof(struct txp_rsp_desc);
		if (ridx == sc->sc_rspring.size) {
			src = sc->sc_rspring.base;
			ridx = 0;
		} else
			src++;
		sc->sc_rspring.lastwrite = ridx;
	}

	hv->hv_resp_read_idx = htole32(ridx);
}

static int
txp_cmd_desc_numfree(struct txp_softc *sc)
{
	struct txp_hostvar *hv;
	struct txp_boot_record *br;
	uint32_t widx, ridx, nfree;

	bus_dmamap_sync(sc->sc_cdata.txp_hostvar_tag,
	    sc->sc_cdata.txp_hostvar_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	hv = sc->sc_hostvar;
	br = sc->sc_boot;
	widx = sc->sc_cmdring.lastwrite;
	ridx = le32toh(hv->hv_cmd_read_idx);

	if (widx == ridx) {
		/* Ring is completely free */
		nfree = le32toh(br->br_cmd_siz) - sizeof(struct txp_cmd_desc);
	} else {
		if (widx > ridx)
			nfree = le32toh(br->br_cmd_siz) -
			    (widx - ridx + sizeof(struct txp_cmd_desc));
		else
			nfree = ridx - widx - sizeof(struct txp_cmd_desc);
	}

	return (nfree / sizeof(struct txp_cmd_desc));
}

static int
txp_sleep(struct txp_softc *sc, int capenable)
{
	uint16_t events;
	int error;

	events = 0;
	if ((capenable & IFCAP_WOL_MAGIC) != 0)
		events |= 0x01;
	error = txp_command(sc, TXP_CMD_ENABLE_WAKEUP_EVENTS, events, 0, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT);
	if (error == 0) {
		/* Goto sleep. */
		error = txp_command(sc, TXP_CMD_GOTO_SLEEP, 0, 0, 0, NULL,
		    NULL, NULL, TXP_CMD_NOWAIT);
		if (error == 0) {
			error = txp_wait(sc, STAT_SLEEPING);
			if (error != 0)
				device_printf(sc->sc_dev,
				    "unable to enter into sleep\n");
		}
	}

	return (error);
}

static void
txp_stop(struct txp_softc *sc)
{
	struct ifnet *ifp;

	TXP_LOCK_ASSERT(sc);
	ifp = sc->sc_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	WRITE_REG(sc, TXP_IER, TXP_INTR_NONE);
	WRITE_REG(sc, TXP_ISR, TXP_INTR_ALL);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_flags &= ~TXP_FLAG_LINK;

	callout_stop(&sc->sc_tick);

	txp_command(sc, TXP_CMD_TX_DISABLE, 0, 0, 0, NULL, NULL, NULL,
	    TXP_CMD_NOWAIT);
	txp_command(sc, TXP_CMD_RX_DISABLE, 0, 0, 0, NULL, NULL, NULL,
	    TXP_CMD_NOWAIT);
	/* Save statistics for later use. */
	txp_stats_save(sc);
	/* Halt controller. */
	txp_command(sc, TXP_CMD_HALT, 0, 0, 0, NULL, NULL, NULL,
	    TXP_CMD_NOWAIT);

	if (txp_wait(sc, STAT_HALTED) != 0)
		device_printf(sc->sc_dev, "controller halt timedout!\n");
	/* Reclaim Tx/Rx buffers. */
	if (sc->sc_txhir.r_cnt && (sc->sc_txhir.r_cons !=
	    TXP_OFFSET2IDX(le32toh(*(sc->sc_txhir.r_off)))))
		txp_tx_reclaim(sc, &sc->sc_txhir);
	if (sc->sc_txlor.r_cnt && (sc->sc_txlor.r_cons !=
	    TXP_OFFSET2IDX(le32toh(*(sc->sc_txlor.r_off)))))
		txp_tx_reclaim(sc, &sc->sc_txlor);
	txp_rxring_empty(sc);

	txp_init_rings(sc);
	/* Reset controller and make it reload sleep image. */
	txp_reset(sc);
	/* Let controller boot from sleep image. */
	if (txp_boot(sc, STAT_WAITING_FOR_HOST_REQUEST) != 0)
		device_printf(sc->sc_dev, "could not boot sleep image\n");
	txp_sleep(sc, 0);
}

static void
txp_watchdog(struct txp_softc *sc)
{
	struct ifnet *ifp;

	TXP_LOCK_ASSERT(sc);

	if (sc->sc_watchdog_timer == 0 || --sc->sc_watchdog_timer)
		return;

	ifp = sc->sc_ifp;
	if_printf(ifp, "watchdog timeout -- resetting\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	txp_stop(sc);
	txp_init_locked(sc);
}

static int
txp_ifmedia_upd(struct ifnet *ifp)
{
	struct txp_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->sc_ifmedia;
	uint16_t new_xcvr;

	TXP_LOCK(sc);
	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER) {
		TXP_UNLOCK(sc);
		return (EINVAL);
	}

	if (IFM_SUBTYPE(ifm->ifm_media) == IFM_10_T) {
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			new_xcvr = TXP_XCVR_10_FDX;
		else
			new_xcvr = TXP_XCVR_10_HDX;
	} else if (IFM_SUBTYPE(ifm->ifm_media) == IFM_100_TX) {
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX)
			new_xcvr = TXP_XCVR_100_FDX;
		else
			new_xcvr = TXP_XCVR_100_HDX;
	} else if (IFM_SUBTYPE(ifm->ifm_media) == IFM_AUTO) {
		new_xcvr = TXP_XCVR_AUTO;
	} else {
		TXP_UNLOCK(sc);
		return (EINVAL);
	}

	/* nothing to do */
	if (sc->sc_xcvr == new_xcvr) {
		TXP_UNLOCK(sc);
		return (0);
	}

	txp_command(sc, TXP_CMD_XCVR_SELECT, new_xcvr, 0, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT);
	sc->sc_xcvr = new_xcvr;
	TXP_UNLOCK(sc);

	return (0);
}

static void
txp_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct txp_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->sc_ifmedia;
	uint16_t bmsr, bmcr, anar, anlpar;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	TXP_LOCK(sc);
	/* Check whether firmware is running. */
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto bail;
	if (txp_command(sc, TXP_CMD_PHY_MGMT_READ, 0, MII_BMSR, 0,
	    &bmsr, NULL, NULL, TXP_CMD_WAIT))
		goto bail;
	if (txp_command(sc, TXP_CMD_PHY_MGMT_READ, 0, MII_BMSR, 0,
	    &bmsr, NULL, NULL, TXP_CMD_WAIT))
		goto bail;

	if (txp_command(sc, TXP_CMD_PHY_MGMT_READ, 0, MII_BMCR, 0,
	    &bmcr, NULL, NULL, TXP_CMD_WAIT))
		goto bail;

	if (txp_command(sc, TXP_CMD_PHY_MGMT_READ, 0, MII_ANLPAR, 0,
	    &anlpar, NULL, NULL, TXP_CMD_WAIT))
		goto bail;

	if (txp_command(sc, TXP_CMD_PHY_MGMT_READ, 0, MII_ANAR, 0,
	    &anar, NULL, NULL, TXP_CMD_WAIT))
		goto bail;
	TXP_UNLOCK(sc);

	if (bmsr & BMSR_LINK)
		ifmr->ifm_status |= IFM_ACTIVE;

	if (bmcr & BMCR_ISO) {
		ifmr->ifm_active |= IFM_NONE;
		ifmr->ifm_status = 0;
		return;
	}

	if (bmcr & BMCR_LOOP)
		ifmr->ifm_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		if ((bmsr & BMSR_ACOMP) == 0) {
			ifmr->ifm_active |= IFM_NONE;
			return;
		}

		anlpar &= anar;
		if (anlpar & ANLPAR_TX_FD)
			ifmr->ifm_active |= IFM_100_TX|IFM_FDX;
		else if (anlpar & ANLPAR_T4)
			ifmr->ifm_active |= IFM_100_T4;
		else if (anlpar & ANLPAR_TX)
			ifmr->ifm_active |= IFM_100_TX;
		else if (anlpar & ANLPAR_10_FD)
			ifmr->ifm_active |= IFM_10_T|IFM_FDX;
		else if (anlpar & ANLPAR_10)
			ifmr->ifm_active |= IFM_10_T;
		else
			ifmr->ifm_active |= IFM_NONE;
	} else
		ifmr->ifm_active = ifm->ifm_cur->ifm_media;
	return;

bail:
	TXP_UNLOCK(sc);
	ifmr->ifm_active |= IFM_NONE;
	ifmr->ifm_status &= ~IFM_AVALID;
}

#ifdef TXP_DEBUG
static void
txp_show_descriptor(void *d)
{
	struct txp_cmd_desc *cmd = d;
	struct txp_rsp_desc *rsp = d;
	struct txp_tx_desc *txd = d;
	struct txp_frag_desc *frgd = d;

	switch (cmd->cmd_flags & CMD_FLAGS_TYPE_M) {
	case CMD_FLAGS_TYPE_CMD:
		/* command descriptor */
		printf("[cmd flags 0x%x num %d id %d seq %d par1 0x%x par2 0x%x par3 0x%x]\n",
		    cmd->cmd_flags, cmd->cmd_numdesc, le16toh(cmd->cmd_id),
		    le16toh(cmd->cmd_seq), le16toh(cmd->cmd_par1),
		    le32toh(cmd->cmd_par2), le32toh(cmd->cmd_par3));
		break;
	case CMD_FLAGS_TYPE_RESP:
		/* response descriptor */
		printf("[rsp flags 0x%x num %d id %d seq %d par1 0x%x par2 0x%x par3 0x%x]\n",
		    rsp->rsp_flags, rsp->rsp_numdesc, le16toh(rsp->rsp_id),
		    le16toh(rsp->rsp_seq), le16toh(rsp->rsp_par1),
		    le32toh(rsp->rsp_par2), le32toh(rsp->rsp_par3));
		break;
	case CMD_FLAGS_TYPE_DATA:
		/* data header (assuming tx for now) */
		printf("[data flags 0x%x num %d totlen %d addr 0x%x/0x%x pflags 0x%x]",
		    txd->tx_flags, txd->tx_numdesc, le16toh(txd->tx_totlen),
		    le32toh(txd->tx_addrlo), le32toh(txd->tx_addrhi),
		    le32toh(txd->tx_pflags));
		break;
	case CMD_FLAGS_TYPE_FRAG:
		/* fragment descriptor */
		printf("[frag flags 0x%x rsvd1 0x%x len %d addr 0x%x/0x%x rsvd2 0x%x]",
		    frgd->frag_flags, frgd->frag_rsvd1, le16toh(frgd->frag_len),
		    le32toh(frgd->frag_addrlo), le32toh(frgd->frag_addrhi),
		    le32toh(frgd->frag_rsvd2));
		break;
	default:
		printf("[unknown(%x) flags 0x%x num %d id %d seq %d par1 0x%x par2 0x%x par3 0x%x]\n",
		    cmd->cmd_flags & CMD_FLAGS_TYPE_M,
		    cmd->cmd_flags, cmd->cmd_numdesc, le16toh(cmd->cmd_id),
		    le16toh(cmd->cmd_seq), le16toh(cmd->cmd_par1),
		    le32toh(cmd->cmd_par2), le32toh(cmd->cmd_par3));
		break;
	}
}
#endif

static void
txp_set_filter(struct txp_softc *sc)
{
	struct ifnet *ifp;
	uint32_t crc, mchash[2];
	uint16_t filter;
	struct ifmultiaddr *ifma;
	int mcnt;

	TXP_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;
	filter = TXP_RXFILT_DIRECT;
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		filter |= TXP_RXFILT_BROADCAST;
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			filter |= TXP_RXFILT_ALLMULTI;
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			filter = TXP_RXFILT_PROMISC;
		goto setit;
	}

	mchash[0] = mchash[1] = 0;
	mcnt = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);
		crc &= 0x3f;
		mchash[crc >> 5] |= 1 << (crc & 0x1f);
		mcnt++;
	}
	if_maddr_runlock(ifp);

	if (mcnt > 0) {
		filter |= TXP_RXFILT_HASHMULTI;
		txp_command(sc, TXP_CMD_MCAST_HASH_MASK_WRITE, 2, mchash[0],
		    mchash[1], NULL, NULL, NULL, TXP_CMD_NOWAIT);
	}

setit:
	txp_command(sc, TXP_CMD_RX_FILTER_WRITE, filter, 0, 0,
	    NULL, NULL, NULL, TXP_CMD_NOWAIT);
}

static int
txp_set_capabilities(struct txp_softc *sc)
{
	struct ifnet *ifp;
	uint32_t rxcap, txcap;

	TXP_LOCK_ASSERT(sc);

	rxcap = txcap = 0;
	ifp = sc->sc_ifp;
	if ((ifp->if_capenable & IFCAP_TXCSUM) != 0) {
		if ((ifp->if_hwassist & CSUM_IP) != 0)
			txcap |= OFFLOAD_IPCKSUM;
		if ((ifp->if_hwassist & CSUM_TCP) != 0)
			txcap |= OFFLOAD_TCPCKSUM;
		if ((ifp->if_hwassist & CSUM_UDP) != 0)
			txcap |= OFFLOAD_UDPCKSUM;
		rxcap = txcap;
	}
	if ((ifp->if_capenable & IFCAP_RXCSUM) == 0)
		rxcap &= ~(OFFLOAD_IPCKSUM | OFFLOAD_TCPCKSUM |
		    OFFLOAD_UDPCKSUM);
	if ((ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0) {
		rxcap |= OFFLOAD_VLAN;
		txcap |= OFFLOAD_VLAN;
	}

	/* Tell firmware new offload configuration. */
	return (txp_command(sc, TXP_CMD_OFFLOAD_WRITE, 0, txcap, rxcap, NULL,
	    NULL, NULL, TXP_CMD_NOWAIT));
}

static void
txp_stats_save(struct txp_softc *sc)
{
	struct txp_rsp_desc *rsp;

	TXP_LOCK_ASSERT(sc);

	rsp = NULL;
	if (txp_ext_command(sc, TXP_CMD_READ_STATISTICS, 0, 0, 0, NULL, 0,
	    &rsp, TXP_CMD_WAIT))
		goto out;
	if (rsp->rsp_numdesc != 6)
		goto out;
	txp_stats_update(sc, rsp);
out:
	if (rsp != NULL)
		free(rsp, M_DEVBUF);
	bcopy(&sc->sc_stats, &sc->sc_ostats, sizeof(struct txp_hw_stats));
}

static void
txp_stats_update(struct txp_softc *sc, struct txp_rsp_desc *rsp)
{
	struct txp_hw_stats *ostats, *stats;
	struct txp_ext_desc *ext;

	TXP_LOCK_ASSERT(sc);

	ext = (struct txp_ext_desc *)(rsp + 1);
	ostats = &sc->sc_ostats;
	stats = &sc->sc_stats;
	stats->tx_frames = ostats->tx_frames + le32toh(rsp->rsp_par2);
	stats->tx_bytes = ostats->tx_bytes + (uint64_t)le32toh(rsp->rsp_par3) +
	    ((uint64_t)le32toh(ext[0].ext_1) << 32);
	stats->tx_deferred = ostats->tx_deferred + le32toh(ext[0].ext_2);
	stats->tx_late_colls = ostats->tx_late_colls + le32toh(ext[0].ext_3);
	stats->tx_colls = ostats->tx_colls + le32toh(ext[0].ext_4);
	stats->tx_carrier_lost = ostats->tx_carrier_lost +
	    le32toh(ext[1].ext_1);
	stats->tx_multi_colls = ostats->tx_multi_colls +
	    le32toh(ext[1].ext_2);
	stats->tx_excess_colls = ostats->tx_excess_colls +
	    le32toh(ext[1].ext_3);
	stats->tx_fifo_underruns = ostats->tx_fifo_underruns +
	    le32toh(ext[1].ext_4);
	stats->tx_mcast_oflows = ostats->tx_mcast_oflows +
	    le32toh(ext[2].ext_1);
	stats->tx_filtered = ostats->tx_filtered + le32toh(ext[2].ext_2);
	stats->rx_frames = ostats->rx_frames + le32toh(ext[2].ext_3);
	stats->rx_bytes = ostats->rx_bytes + (uint64_t)le32toh(ext[2].ext_4) +
	    ((uint64_t)le32toh(ext[3].ext_1) << 32);
	stats->rx_fifo_oflows = ostats->rx_fifo_oflows + le32toh(ext[3].ext_2);
	stats->rx_badssd = ostats->rx_badssd + le32toh(ext[3].ext_3);
	stats->rx_crcerrs = ostats->rx_crcerrs + le32toh(ext[3].ext_4);
	stats->rx_lenerrs = ostats->rx_lenerrs + le32toh(ext[4].ext_1);
	stats->rx_bcast_frames = ostats->rx_bcast_frames +
	    le32toh(ext[4].ext_2);
	stats->rx_mcast_frames = ostats->rx_mcast_frames +
	    le32toh(ext[4].ext_3);
	stats->rx_oflows = ostats->rx_oflows + le32toh(ext[4].ext_4);
	stats->rx_filtered = ostats->rx_filtered + le32toh(ext[5].ext_1);
}

static uint64_t
txp_get_counter(struct ifnet *ifp, ift_counter cnt)
{
	struct txp_softc *sc;
	struct txp_hw_stats *stats;

	sc = if_getsoftc(ifp);
	stats = &sc->sc_stats;

	switch (cnt) {
	case IFCOUNTER_IERRORS:
		return (stats->rx_fifo_oflows + stats->rx_badssd +
		    stats->rx_crcerrs + stats->rx_lenerrs + stats->rx_oflows);
	case IFCOUNTER_OERRORS:
		return (stats->tx_deferred + stats->tx_carrier_lost +
		    stats->tx_fifo_underruns + stats->tx_mcast_oflows);
	case IFCOUNTER_COLLISIONS:
		return (stats->tx_late_colls + stats->tx_multi_colls +
		    stats->tx_excess_colls);
	case IFCOUNTER_OPACKETS:
		return (stats->tx_frames);
	case IFCOUNTER_IPACKETS:
		return (stats->rx_frames);
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

#define	TXP_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)

#if __FreeBSD_version >= 900030
#define	TXP_SYSCTL_STAT_ADD64(c, h, n, p, d)	\
	    SYSCTL_ADD_UQUAD(c, h, OID_AUTO, n, CTLFLAG_RD, p, d)
#elif __FreeBSD_version > 800000
#define	TXP_SYSCTL_STAT_ADD64(c, h, n, p, d)	\
	    SYSCTL_ADD_QUAD(c, h, OID_AUTO, n, CTLFLAG_RD, p, d)
#else
#define	TXP_SYSCTL_STAT_ADD64(c, h, n, p, d)	\
	    SYSCTL_ADD_ULONG(c, h, OID_AUTO, n, CTLFLAG_RD, p, d)
#endif

static void
txp_sysctl_node(struct txp_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct txp_hw_stats *stats;
	int error;

	stats = &sc->sc_stats;
	ctx = device_get_sysctl_ctx(sc->sc_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev));
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "process_limit",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->sc_process_limit, 0,
	    sysctl_hw_txp_proc_limit, "I",
	    "max number of Rx events to process");
	/* Pull in device tunables. */
	sc->sc_process_limit = TXP_PROC_DEFAULT;
	error = resource_int_value(device_get_name(sc->sc_dev),
	    device_get_unit(sc->sc_dev), "process_limit",
	    &sc->sc_process_limit);
	if (error == 0) {
		if (sc->sc_process_limit < TXP_PROC_MIN ||
		    sc->sc_process_limit > TXP_PROC_MAX) {
			device_printf(sc->sc_dev,
			    "process_limit value out of range; "
			    "using default: %d\n", TXP_PROC_DEFAULT);
			sc->sc_process_limit = TXP_PROC_DEFAULT;
		}
	}
	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "TXP statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* Tx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "Tx MAC statistics");
	child = SYSCTL_CHILDREN(tree);

	TXP_SYSCTL_STAT_ADD32(ctx, child, "frames",
	    &stats->tx_frames, "Frames");
	TXP_SYSCTL_STAT_ADD64(ctx, child, "octets",
	    &stats->tx_bytes, "Octets");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "deferred",
	    &stats->tx_deferred, "Deferred frames");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "late_colls",
	    &stats->tx_late_colls, "Late collisions");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "colls",
	    &stats->tx_colls, "Collisions");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "carrier_lost",
	    &stats->tx_carrier_lost, "Carrier lost");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "multi_colls",
	    &stats->tx_multi_colls, "Multiple collisions");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "excess_colls",
	    &stats->tx_excess_colls, "Excessive collisions");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "fifo_underruns",
	    &stats->tx_fifo_underruns, "FIFO underruns");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "mcast_oflows",
	    &stats->tx_mcast_oflows, "Multicast overflows");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "filtered",
	    &stats->tx_filtered, "Filtered frames");

	/* Rx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "Rx MAC statistics");
	child = SYSCTL_CHILDREN(tree);

	TXP_SYSCTL_STAT_ADD32(ctx, child, "frames",
	    &stats->rx_frames, "Frames");
	TXP_SYSCTL_STAT_ADD64(ctx, child, "octets",
	    &stats->rx_bytes, "Octets");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "fifo_oflows",
	    &stats->rx_fifo_oflows, "FIFO overflows");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "badssd",
	    &stats->rx_badssd, "Bad SSD");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "crcerrs",
	    &stats->rx_crcerrs, "CRC errors");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "lenerrs",
	    &stats->rx_lenerrs, "Length errors");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "bcast_frames",
	    &stats->rx_bcast_frames, "Broadcast frames");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "mcast_frames",
	    &stats->rx_mcast_frames, "Multicast frames");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "oflows",
	    &stats->rx_oflows, "Overflows");
	TXP_SYSCTL_STAT_ADD32(ctx, child, "filtered",
	    &stats->rx_filtered, "Filtered frames");
}

#undef TXP_SYSCTL_STAT_ADD32
#undef TXP_SYSCTL_STAT_ADD64

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (arg1 == NULL)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
        *(int *)arg1 = value;

        return (0);
}

static int
sysctl_hw_txp_proc_limit(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    TXP_PROC_MIN, TXP_PROC_MAX));
}
