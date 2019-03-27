/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * File: qla_os.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qla_os.h"
#include "qla_reg.h"
#include "qla_hw.h"
#include "qla_def.h"
#include "qla_inline.h"
#include "qla_ver.h"
#include "qla_glbl.h"
#include "qla_dbg.h"

/*
 * Some PCI Configuration Space Related Defines
 */

#ifndef PCI_VENDOR_QLOGIC
#define PCI_VENDOR_QLOGIC	0x1077
#endif

#ifndef PCI_PRODUCT_QLOGIC_ISP8020
#define PCI_PRODUCT_QLOGIC_ISP8020	0x8020
#endif

#define PCI_QLOGIC_ISP8020 \
	((PCI_PRODUCT_QLOGIC_ISP8020 << 16) | PCI_VENDOR_QLOGIC)

/*
 * static functions
 */
static int qla_alloc_parent_dma_tag(qla_host_t *ha);
static void qla_free_parent_dma_tag(qla_host_t *ha);
static int qla_alloc_xmt_bufs(qla_host_t *ha);
static void qla_free_xmt_bufs(qla_host_t *ha);
static int qla_alloc_rcv_bufs(qla_host_t *ha);
static void qla_free_rcv_bufs(qla_host_t *ha);

static void qla_init_ifnet(device_t dev, qla_host_t *ha);
static int qla_sysctl_get_stats(SYSCTL_HANDLER_ARGS);
static void qla_release(qla_host_t *ha);
static void qla_dmamap_callback(void *arg, bus_dma_segment_t *segs, int nsegs,
		int error);
static void qla_stop(qla_host_t *ha);
static int qla_send(qla_host_t *ha, struct mbuf **m_headp);
static void qla_tx_done(void *context, int pending);

/*
 * Hooks to the Operating Systems
 */
static int qla_pci_probe (device_t);
static int qla_pci_attach (device_t);
static int qla_pci_detach (device_t);

static void qla_init(void *arg);
static int qla_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static int qla_media_change(struct ifnet *ifp);
static void qla_media_status(struct ifnet *ifp, struct ifmediareq *ifmr);

static device_method_t qla_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, qla_pci_probe),
	DEVMETHOD(device_attach, qla_pci_attach),
	DEVMETHOD(device_detach, qla_pci_detach),
	{ 0, 0 }
};

static driver_t qla_pci_driver = {
	"ql", qla_pci_methods, sizeof (qla_host_t),
};

static devclass_t qla80xx_devclass;

DRIVER_MODULE(qla80xx, pci, qla_pci_driver, qla80xx_devclass, 0, 0);

MODULE_DEPEND(qla80xx, pci, 1, 1, 1);
MODULE_DEPEND(qla80xx, ether, 1, 1, 1);

MALLOC_DEFINE(M_QLA8XXXBUF, "qla80xxbuf", "Buffers for qla80xx driver");

uint32_t std_replenish = 8;
uint32_t jumbo_replenish = 2;
uint32_t rcv_pkt_thres = 128;
uint32_t rcv_pkt_thres_d = 32;
uint32_t snd_pkt_thres = 16;
uint32_t free_pkt_thres = (NUM_TX_DESCRIPTORS / 2);

static char dev_str[64];

/*
 * Name:	qla_pci_probe
 * Function:	Validate the PCI device to be a QLA80XX device
 */
static int
qla_pci_probe(device_t dev)
{
        switch ((pci_get_device(dev) << 16) | (pci_get_vendor(dev))) {
        case PCI_QLOGIC_ISP8020:
		snprintf(dev_str, sizeof(dev_str), "%s v%d.%d.%d",
			"Qlogic ISP 80xx PCI CNA Adapter-Ethernet Function",
			QLA_VERSION_MAJOR, QLA_VERSION_MINOR,
			QLA_VERSION_BUILD);
                device_set_desc(dev, dev_str);
                break;
        default:
                return (ENXIO);
        }

        if (bootverbose)
                printf("%s: %s\n ", __func__, dev_str);

        return (BUS_PROBE_DEFAULT);
}

static void
qla_add_sysctls(qla_host_t *ha)
{
        device_t dev = ha->pci_dev;

        SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "stats", CTLTYPE_INT | CTLFLAG_RD,
                (void *)ha, 0,
                qla_sysctl_get_stats, "I", "Statistics");

	SYSCTL_ADD_STRING(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "fw_version", CTLFLAG_RD,
		ha->fw_ver_str, 0, "firmware version");

	dbg_level = 0;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "debug", CTLFLAG_RW,
                &dbg_level, dbg_level, "Debug Level");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "std_replenish", CTLFLAG_RW,
                &std_replenish, std_replenish,
                "Threshold for Replenishing Standard Frames");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "jumbo_replenish", CTLFLAG_RW,
                &jumbo_replenish, jumbo_replenish,
                "Threshold for Replenishing Jumbo Frames");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "rcv_pkt_thres",  CTLFLAG_RW,
                &rcv_pkt_thres, rcv_pkt_thres,
                "Threshold for # of rcv pkts to trigger indication isr");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "rcv_pkt_thres_d",  CTLFLAG_RW,
                &rcv_pkt_thres_d, rcv_pkt_thres_d,
                "Threshold for # of rcv pkts to trigger indication defered");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "snd_pkt_thres",  CTLFLAG_RW,
                &snd_pkt_thres, snd_pkt_thres,
                "Threshold for # of snd packets");

        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "free_pkt_thres",  CTLFLAG_RW,
                &free_pkt_thres, free_pkt_thres,
                "Threshold for # of packets to free at a time");

        return;
}

static void
qla_watchdog(void *arg)
{
	qla_host_t *ha = arg;
	qla_hw_t *hw;
	struct ifnet *ifp;

	hw = &ha->hw;
	ifp = ha->ifp;

        if (ha->flags.qla_watchdog_exit)
		return;

	if (!ha->flags.qla_watchdog_pause) {
		if (qla_le32_to_host(*(hw->tx_cons)) != hw->txr_comp) {
			taskqueue_enqueue(ha->tx_tq, &ha->tx_task);
		} else if ((ifp->if_snd.ifq_head != NULL) && QL_RUNNING(ifp)) {
			taskqueue_enqueue(ha->tx_tq, &ha->tx_task);
		}
	}
	ha->watchdog_ticks = ha->watchdog_ticks++ % 1000;
	callout_reset(&ha->tx_callout, QLA_WATCHDOG_CALLOUT_TICKS,
		qla_watchdog, ha);
}

/*
 * Name:	qla_pci_attach
 * Function:	attaches the device to the operating system
 */
static int
qla_pci_attach(device_t dev)
{
	qla_host_t *ha = NULL;
	uint32_t rsrc_len, i;

	QL_DPRINT2((dev, "%s: enter\n", __func__));

        if ((ha = device_get_softc(dev)) == NULL) {
                device_printf(dev, "cannot get softc\n");
                return (ENOMEM);
        }

        memset(ha, 0, sizeof (qla_host_t));

        if (pci_get_device(dev) != PCI_PRODUCT_QLOGIC_ISP8020) {
                device_printf(dev, "device is not ISP8020\n");
                return (ENXIO);
	}

        ha->pci_func = pci_get_function(dev);

        ha->pci_dev = dev;

	pci_enable_busmaster(dev);

	ha->reg_rid = PCIR_BAR(0);
	ha->pci_reg = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &ha->reg_rid,
				RF_ACTIVE);

        if (ha->pci_reg == NULL) {
                device_printf(dev, "unable to map any ports\n");
                goto qla_pci_attach_err;
        }

	rsrc_len = (uint32_t) bus_get_resource_count(dev, SYS_RES_MEMORY,
					ha->reg_rid);

	mtx_init(&ha->hw_lock, "qla80xx_hw_lock", MTX_NETWORK_LOCK, MTX_DEF);
	mtx_init(&ha->tx_lock, "qla80xx_tx_lock", MTX_NETWORK_LOCK, MTX_DEF);
	mtx_init(&ha->rx_lock, "qla80xx_rx_lock", MTX_NETWORK_LOCK, MTX_DEF);
	mtx_init(&ha->rxj_lock, "qla80xx_rxj_lock", MTX_NETWORK_LOCK, MTX_DEF);
	ha->flags.lock_init = 1;

	ha->msix_count = pci_msix_count(dev);

	if (ha->msix_count < qla_get_msix_count(ha)) {
		device_printf(dev, "%s: msix_count[%d] not enough\n", __func__,
			ha->msix_count);
		goto qla_pci_attach_err;
	}

	QL_DPRINT2((dev, "%s: ha %p irq %p pci_func 0x%x rsrc_count 0x%08x"
		" msix_count 0x%x pci_reg %p\n", __func__, ha,
		ha->irq, ha->pci_func, rsrc_len, ha->msix_count, ha->pci_reg));

	ha->msix_count = qla_get_msix_count(ha);

	if (pci_alloc_msix(dev, &ha->msix_count)) {
		device_printf(dev, "%s: pci_alloc_msi[%d] failed\n", __func__,
			ha->msix_count);
		ha->msix_count = 0;
		goto qla_pci_attach_err;
	}

	TASK_INIT(&ha->tx_task, 0, qla_tx_done, ha);
	ha->tx_tq = taskqueue_create_fast("qla_txq", M_NOWAIT,
			taskqueue_thread_enqueue, &ha->tx_tq);
	taskqueue_start_threads(&ha->tx_tq, 1, PI_NET, "%s txq",
		device_get_nameunit(ha->pci_dev));

        for (i = 0; i < ha->msix_count; i++) {
                ha->irq_vec[i].irq_rid = i+1;
                ha->irq_vec[i].ha = ha;

                ha->irq_vec[i].irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
                                        &ha->irq_vec[i].irq_rid,
                                        (RF_ACTIVE | RF_SHAREABLE));

                if (ha->irq_vec[i].irq == NULL) {
                        device_printf(dev, "could not allocate interrupt\n");
                        goto qla_pci_attach_err;
                }

                if (bus_setup_intr(dev, ha->irq_vec[i].irq,
                        (INTR_TYPE_NET | INTR_MPSAFE),
                        NULL, qla_isr, &ha->irq_vec[i],
                        &ha->irq_vec[i].handle)) {
                        device_printf(dev, "could not setup interrupt\n");
                        goto qla_pci_attach_err;
                }

		TASK_INIT(&ha->irq_vec[i].rcv_task, 0, qla_rcv,\
			&ha->irq_vec[i]);

		ha->irq_vec[i].rcv_tq = taskqueue_create_fast("qla_rcvq",
			M_NOWAIT, taskqueue_thread_enqueue,
			&ha->irq_vec[i].rcv_tq);

		taskqueue_start_threads(&ha->irq_vec[i].rcv_tq, 1, PI_NET,
			"%s rcvq",
			device_get_nameunit(ha->pci_dev));
        }

	qla_add_sysctls(ha);

	/* add hardware specific sysctls */
	qla_hw_add_sysctls(ha);

	/* initialize hardware */
	if (qla_init_hw(ha)) {
		device_printf(dev, "%s: qla_init_hw failed\n", __func__);
		goto qla_pci_attach_err;
	}

	device_printf(dev, "%s: firmware[%d.%d.%d.%d]\n", __func__,
		ha->fw_ver_major, ha->fw_ver_minor, ha->fw_ver_sub,
		ha->fw_ver_build);

	snprintf(ha->fw_ver_str, sizeof(ha->fw_ver_str), "%d.%d.%d.%d",
			ha->fw_ver_major, ha->fw_ver_minor, ha->fw_ver_sub,
			ha->fw_ver_build);

	//qla_get_hw_caps(ha);
	qla_read_mac_addr(ha);

	/* allocate parent dma tag */
	if (qla_alloc_parent_dma_tag(ha)) {
		device_printf(dev, "%s: qla_alloc_parent_dma_tag failed\n",
			__func__);
		goto qla_pci_attach_err;
	}

	/* alloc all dma buffers */
	if (qla_alloc_dma(ha)) {
		device_printf(dev, "%s: qla_alloc_dma failed\n", __func__);
		goto qla_pci_attach_err;
	}

	/* create the o.s ethernet interface */
	qla_init_ifnet(dev, ha);

	ha->flags.qla_watchdog_active = 1;
	ha->flags.qla_watchdog_pause = 1;
	
	callout_init(&ha->tx_callout, 1);

	/* create ioctl device interface */
	if (qla_make_cdev(ha)) {
		device_printf(dev, "%s: qla_make_cdev failed\n", __func__);
		goto qla_pci_attach_err;
	}

	callout_reset(&ha->tx_callout, QLA_WATCHDOG_CALLOUT_TICKS,
		qla_watchdog, ha);

	QL_DPRINT2((dev, "%s: exit 0\n", __func__));
        return (0);

qla_pci_attach_err:

	qla_release(ha);

	QL_DPRINT2((dev, "%s: exit ENXIO\n", __func__));
        return (ENXIO);
}

/*
 * Name:	qla_pci_detach
 * Function:	Unhooks the device from the operating system
 */
static int
qla_pci_detach(device_t dev)
{
	qla_host_t *ha = NULL;
	struct ifnet *ifp;
	int i;

	QL_DPRINT2((dev, "%s: enter\n", __func__));

        if ((ha = device_get_softc(dev)) == NULL) {
                device_printf(dev, "cannot get softc\n");
                return (ENOMEM);
        }

	ifp = ha->ifp;

	QLA_LOCK(ha, __func__);
	qla_stop(ha);
	QLA_UNLOCK(ha, __func__);

	if (ha->tx_tq) {
		taskqueue_drain(ha->tx_tq, &ha->tx_task);
		taskqueue_free(ha->tx_tq);
	}

        for (i = 0; i < ha->msix_count; i++) {
		taskqueue_drain(ha->irq_vec[i].rcv_tq,
			&ha->irq_vec[i].rcv_task);
		taskqueue_free(ha->irq_vec[i].rcv_tq);
	}

	qla_release(ha);

	QL_DPRINT2((dev, "%s: exit\n", __func__));

        return (0);
}

/*
 * SYSCTL Related Callbacks
 */
static int
qla_sysctl_get_stats(SYSCTL_HANDLER_ARGS)
{
	int err, ret = 0;
	qla_host_t *ha;

	err = sysctl_handle_int(oidp, &ret, 0, req);

	if (err)
		return (err);

	ha = (qla_host_t *)arg1;
	//qla_get_stats(ha);
	QL_DPRINT2((ha->pci_dev, "%s: called ret %d\n", __func__, ret));
	return (err);
}


/*
 * Name:	qla_release
 * Function:	Releases the resources allocated for the device
 */
static void
qla_release(qla_host_t *ha)
{
	device_t dev;
	int i;

	dev = ha->pci_dev;

	qla_del_cdev(ha);

	if (ha->flags.qla_watchdog_active)
		ha->flags.qla_watchdog_exit = 1;

	callout_stop(&ha->tx_callout);
	qla_mdelay(__func__, 100);

	if (ha->ifp != NULL)
		ether_ifdetach(ha->ifp);

	qla_free_dma(ha); 
	qla_free_parent_dma_tag(ha);

	for (i = 0; i < ha->msix_count; i++) {
		if (ha->irq_vec[i].handle)
			(void)bus_teardown_intr(dev, ha->irq_vec[i].irq,
				ha->irq_vec[i].handle);
		if (ha->irq_vec[i].irq)
			(void) bus_release_resource(dev, SYS_RES_IRQ,
				ha->irq_vec[i].irq_rid,
				ha->irq_vec[i].irq);
	}
	if (ha->msix_count)
		pci_release_msi(dev);

	if (ha->flags.lock_init) {
		mtx_destroy(&ha->tx_lock);
		mtx_destroy(&ha->rx_lock);
		mtx_destroy(&ha->rxj_lock);
		mtx_destroy(&ha->hw_lock);
	}

        if (ha->pci_reg)
                (void) bus_release_resource(dev, SYS_RES_MEMORY, ha->reg_rid,
				ha->pci_reg);
}

/*
 * DMA Related Functions
 */

static void
qla_dmamap_callback(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
        *((bus_addr_t *)arg) = 0;

        if (error) {
                printf("%s: bus_dmamap_load failed (%d)\n", __func__, error);
                return;
	}

        QL_ASSERT((nsegs == 1), ("%s: %d segments returned!", __func__, nsegs));

        *((bus_addr_t *)arg) = segs[0].ds_addr;

	return;
}

int
qla_alloc_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf)
{
        int             ret = 0;
        device_t        dev;
        bus_addr_t      b_addr;

        dev = ha->pci_dev;

        QL_DPRINT2((dev, "%s: enter\n", __func__));

        ret = bus_dma_tag_create(
                        ha->parent_tag,/* parent */
                        dma_buf->alignment,
                        ((bus_size_t)(1ULL << 32)),/* boundary */
                        BUS_SPACE_MAXADDR,      /* lowaddr */
                        BUS_SPACE_MAXADDR,      /* highaddr */
                        NULL, NULL,             /* filter, filterarg */
                        dma_buf->size,          /* maxsize */
                        1,                      /* nsegments */
                        dma_buf->size,          /* maxsegsize */
                        0,                      /* flags */
                        NULL, NULL,             /* lockfunc, lockarg */
                        &dma_buf->dma_tag);

        if (ret) {
                device_printf(dev, "%s: could not create dma tag\n", __func__);
                goto qla_alloc_dmabuf_exit;
        }
        ret = bus_dmamem_alloc(dma_buf->dma_tag,
                        (void **)&dma_buf->dma_b,
                        (BUS_DMA_ZERO | BUS_DMA_COHERENT | BUS_DMA_NOWAIT),
                        &dma_buf->dma_map);
        if (ret) {
                bus_dma_tag_destroy(dma_buf->dma_tag);
                device_printf(dev, "%s: bus_dmamem_alloc failed\n", __func__);
                goto qla_alloc_dmabuf_exit;
        }

        ret = bus_dmamap_load(dma_buf->dma_tag,
                        dma_buf->dma_map,
                        dma_buf->dma_b,
                        dma_buf->size,
                        qla_dmamap_callback,
                        &b_addr, BUS_DMA_NOWAIT);

        if (ret || !b_addr) {
                bus_dma_tag_destroy(dma_buf->dma_tag);
                bus_dmamem_free(dma_buf->dma_tag, dma_buf->dma_b,
                        dma_buf->dma_map);
                ret = -1;
                goto qla_alloc_dmabuf_exit;
        }

        dma_buf->dma_addr = b_addr;

qla_alloc_dmabuf_exit:
        QL_DPRINT2((dev, "%s: exit ret 0x%08x tag %p map %p b %p sz 0x%x\n",
                __func__, ret, (void *)dma_buf->dma_tag,
                (void *)dma_buf->dma_map, (void *)dma_buf->dma_b,
		dma_buf->size));

        return ret;
}

void
qla_free_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf)
{
        bus_dmamap_unload(dma_buf->dma_tag, dma_buf->dma_map);
        bus_dmamem_free(dma_buf->dma_tag, dma_buf->dma_b, dma_buf->dma_map);
        bus_dma_tag_destroy(dma_buf->dma_tag);
}

static int
qla_alloc_parent_dma_tag(qla_host_t *ha)
{
	int		ret;
	device_t	dev;

	dev = ha->pci_dev;

        /*
         * Allocate parent DMA Tag
         */
        ret = bus_dma_tag_create(
                        bus_get_dma_tag(dev),   /* parent */
                        1,((bus_size_t)(1ULL << 32)),/* alignment, boundary */
                        BUS_SPACE_MAXADDR,      /* lowaddr */
                        BUS_SPACE_MAXADDR,      /* highaddr */
                        NULL, NULL,             /* filter, filterarg */
                        BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
                        0,                      /* nsegments */
                        BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
                        0,                      /* flags */
                        NULL, NULL,             /* lockfunc, lockarg */
                        &ha->parent_tag);

        if (ret) {
                device_printf(dev, "%s: could not create parent dma tag\n",
                        __func__);
		return (-1);
        }

        ha->flags.parent_tag = 1;
	
	return (0);
}

static void
qla_free_parent_dma_tag(qla_host_t *ha)
{
        if (ha->flags.parent_tag) {
                bus_dma_tag_destroy(ha->parent_tag);
                ha->flags.parent_tag = 0;
        }
}

/*
 * Name: qla_init_ifnet
 * Function: Creates the Network Device Interface and Registers it with the O.S
 */

static void
qla_init_ifnet(device_t dev, qla_host_t *ha)
{
	struct ifnet *ifp;

	QL_DPRINT2((dev, "%s: enter\n", __func__));

	ifp = ha->ifp = if_alloc(IFT_ETHER);

	if (ifp == NULL)
		panic("%s: cannot if_alloc()\n", device_get_nameunit(dev));

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	ifp->if_baudrate = IF_Gbps(10);
	ifp->if_init = qla_init;
	ifp->if_softc = ha;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = qla_ioctl;
	ifp->if_start = qla_start;

	IFQ_SET_MAXLEN(&ifp->if_snd, qla_get_ifq_snd_maxlen(ha));
	ifp->if_snd.ifq_drv_maxlen = qla_get_ifq_snd_maxlen(ha);
	IFQ_SET_READY(&ifp->if_snd);

	ha->max_frame_size = ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;

	ether_ifattach(ifp, qla_get_mac_addr(ha));

	ifp->if_capabilities = IFCAP_HWCSUM |
				IFCAP_TSO4 |
				IFCAP_JUMBO_MTU;

	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;
	ifp->if_capabilities |= IFCAP_LINKSTATE;

#if defined(__FreeBSD_version) && (__FreeBSD_version < 900002)
	ifp->if_timer = 0;
	ifp->if_watchdog = NULL;
#endif /* #if defined(__FreeBSD_version) && (__FreeBSD_version < 900002) */

	ifp->if_capenable = ifp->if_capabilities;

	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	ifmedia_init(&ha->media, IFM_IMASK, qla_media_change, qla_media_status);

	ifmedia_add(&ha->media, (IFM_ETHER | qla_get_optics(ha) | IFM_FDX), 0,
		NULL);
	ifmedia_add(&ha->media, (IFM_ETHER | IFM_AUTO), 0, NULL);

	ifmedia_set(&ha->media, (IFM_ETHER | IFM_AUTO));

	QL_DPRINT2((dev, "%s: exit\n", __func__));

	return;
}

static void
qla_init_locked(qla_host_t *ha)
{
	struct ifnet *ifp = ha->ifp;

	qla_stop(ha);

	if (qla_alloc_xmt_bufs(ha) != 0) 
		return;

	if (qla_alloc_rcv_bufs(ha) != 0)
		return;

	if (qla_config_lro(ha))
		return;

	bcopy(IF_LLADDR(ha->ifp), ha->hw.mac_addr, ETHER_ADDR_LEN);

	ifp->if_hwassist = CSUM_TCP | CSUM_UDP | CSUM_TSO;

	ha->flags.stop_rcv = 0;
	if (qla_init_hw_if(ha) == 0) {
		ifp = ha->ifp;
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		ha->flags.qla_watchdog_pause = 0;
	}

	return;
}

static void
qla_init(void *arg)
{
	qla_host_t *ha;

	ha = (qla_host_t *)arg;

	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	QLA_LOCK(ha, __func__);
	qla_init_locked(ha);
	QLA_UNLOCK(ha, __func__);

	QL_DPRINT2((ha->pci_dev, "%s: exit\n", __func__));
}

static void
qla_set_multi(qla_host_t *ha, uint32_t add_multi)
{
	uint8_t mta[Q8_MAX_NUM_MULTICAST_ADDRS * Q8_MAC_ADDR_LEN];
	struct ifmultiaddr *ifma;
	int mcnt = 0;
	struct ifnet *ifp = ha->ifp;

	if_maddr_rlock(ifp);

	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (mcnt == Q8_MAX_NUM_MULTICAST_ADDRS)
			break;

		bcopy(LLADDR((struct sockaddr_dl *) ifma->ifma_addr),
			&mta[mcnt * Q8_MAC_ADDR_LEN], Q8_MAC_ADDR_LEN);

		mcnt++;
	}

	if_maddr_runlock(ifp);

	qla_hw_set_multi(ha, mta, mcnt, add_multi);

	return;
}

static int
qla_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	int ret = 0;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifaddr *ifa = (struct ifaddr *)data;
	qla_host_t *ha;

	ha = (qla_host_t *)ifp->if_softc;

	switch (cmd) {
	case SIOCSIFADDR:
		QL_DPRINT4((ha->pci_dev, "%s: SIOCSIFADDR (0x%lx)\n",
			__func__, cmd));

		if (ifa->ifa_addr->sa_family == AF_INET) {
			ifp->if_flags |= IFF_UP;
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				QLA_LOCK(ha, __func__);
				qla_init_locked(ha);
				QLA_UNLOCK(ha, __func__);
			}
		QL_DPRINT4((ha->pci_dev,
			"%s: SIOCSIFADDR (0x%lx) ipv4 [0x%08x]\n",
			__func__, cmd, ntohl(IA_SIN(ifa)->sin_addr.s_addr)));

			arp_ifinit(ifp, ifa);
			if (ntohl(IA_SIN(ifa)->sin_addr.s_addr) != INADDR_ANY) {
				qla_config_ipv4_addr(ha,
					(IA_SIN(ifa)->sin_addr.s_addr));
			}
		} else {
			ether_ioctl(ifp, cmd, data);
		}
		break;

	case SIOCSIFMTU:
		QL_DPRINT4((ha->pci_dev, "%s: SIOCSIFMTU (0x%lx)\n",
			__func__, cmd));

		if (ifr->ifr_mtu > QLA_MAX_FRAME_SIZE - ETHER_HDR_LEN) {
			ret = EINVAL;
		} else {
			QLA_LOCK(ha, __func__);
			ifp->if_mtu = ifr->ifr_mtu;
			ha->max_frame_size =
				ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				ret = qla_set_max_mtu(ha, ha->max_frame_size,
					(ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id);
			}
			QLA_UNLOCK(ha, __func__);

			if (ret)
				ret = EINVAL;
		}

		break;

	case SIOCSIFFLAGS:
		QL_DPRINT4((ha->pci_dev, "%s: SIOCSIFFLAGS (0x%lx)\n",
			__func__, cmd));

		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				if ((ifp->if_flags ^ ha->if_flags) &
					IFF_PROMISC) {
					qla_set_promisc(ha);
				} else if ((ifp->if_flags ^ ha->if_flags) &
					IFF_ALLMULTI) {
					qla_set_allmulti(ha);
				}
			} else {
				QLA_LOCK(ha, __func__);
				qla_init_locked(ha);
				ha->max_frame_size = ifp->if_mtu +
					ETHER_HDR_LEN + ETHER_CRC_LEN;
				ret = qla_set_max_mtu(ha, ha->max_frame_size,
					(ha->hw.rx_cntxt_rsp)->rx_rsp.cntxt_id);
				QLA_UNLOCK(ha, __func__);
			}
		} else {
			QLA_LOCK(ha, __func__);
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				qla_stop(ha);
			ha->if_flags = ifp->if_flags;
			QLA_UNLOCK(ha, __func__);
		}
		break;

	case SIOCADDMULTI:
		QL_DPRINT4((ha->pci_dev,
			"%s: %s (0x%lx)\n", __func__, "SIOCADDMULTI", cmd));

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			qla_set_multi(ha, 1);
		}
		break;

	case SIOCDELMULTI:
		QL_DPRINT4((ha->pci_dev,
			"%s: %s (0x%lx)\n", __func__, "SIOCDELMULTI", cmd));

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			qla_set_multi(ha, 0);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		QL_DPRINT4((ha->pci_dev,
			"%s: SIOCSIFMEDIA/SIOCGIFMEDIA (0x%lx)\n",
			__func__, cmd));
		ret = ifmedia_ioctl(ifp, ifr, &ha->media, cmd);
		break;

	case SIOCSIFCAP:
	{
		int mask = ifr->ifr_reqcap ^ ifp->if_capenable;

		QL_DPRINT4((ha->pci_dev, "%s: SIOCSIFCAP (0x%lx)\n",
			__func__, cmd));

		if (mask & IFCAP_HWCSUM)
			ifp->if_capenable ^= IFCAP_HWCSUM;
		if (mask & IFCAP_TSO4)
			ifp->if_capenable ^= IFCAP_TSO4;
		if (mask & IFCAP_TSO6)
			ifp->if_capenable ^= IFCAP_TSO6;
		if (mask & IFCAP_VLAN_HWTAGGING)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;

		if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
			qla_init(ha);

		VLAN_CAPABILITIES(ifp);
		break;
	}

	default:
		QL_DPRINT4((ha->pci_dev, "%s: default (0x%lx)\n",
			__func__, cmd));
		ret = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (ret);
}

static int
qla_media_change(struct ifnet *ifp)
{
	qla_host_t *ha;
	struct ifmedia *ifm;
	int ret = 0;

	ha = (qla_host_t *)ifp->if_softc;

	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	ifm = &ha->media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		ret = EINVAL;

	QL_DPRINT2((ha->pci_dev, "%s: exit\n", __func__));

	return (ret);
}

static void
qla_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	qla_host_t *ha;

	ha = (qla_host_t *)ifp->if_softc;

	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;
	
	qla_update_link_state(ha);
	if (ha->hw.flags.link_up) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active |= (IFM_FDX | qla_get_optics(ha));
	}

	QL_DPRINT2((ha->pci_dev, "%s: exit (%s)\n", __func__,\
		(ha->hw.flags.link_up ? "link_up" : "link_down")));

	return;
}

void
qla_start(struct ifnet *ifp)
{
	struct mbuf    *m_head;
	qla_host_t *ha = (qla_host_t *)ifp->if_softc;

	QL_DPRINT8((ha->pci_dev, "%s: enter\n", __func__));

	if (!mtx_trylock(&ha->tx_lock)) {
		QL_DPRINT8((ha->pci_dev,
			"%s: mtx_trylock(&ha->tx_lock) failed\n", __func__));
		return;
	}

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) != 
		IFF_DRV_RUNNING) {
		QL_DPRINT8((ha->pci_dev, "%s: !IFF_DRV_RUNNING\n", __func__));
		QLA_TX_UNLOCK(ha);
		return;
	}

	if (!ha->watchdog_ticks)
		qla_update_link_state(ha);

	if (!ha->hw.flags.link_up) {
		QL_DPRINT8((ha->pci_dev, "%s: link down\n", __func__));
		QLA_TX_UNLOCK(ha);
		return;
	}

	while (ifp->if_snd.ifq_head != NULL) {
		IF_DEQUEUE(&ifp->if_snd, m_head);

		if (m_head == NULL) {
			QL_DPRINT8((ha->pci_dev, "%s: m_head == NULL\n",
				__func__));
			break;
		}

		if (qla_send(ha, &m_head)) {
			if (m_head == NULL)
				break;
			QL_DPRINT8((ha->pci_dev, "%s: PREPEND\n", __func__));
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			IF_PREPEND(&ifp->if_snd, m_head);
			break;
		}
		/* Send a copy of the frame to the BPF listener */
		ETHER_BPF_MTAP(ifp, m_head);
	}
	QLA_TX_UNLOCK(ha);
	QL_DPRINT8((ha->pci_dev, "%s: exit\n", __func__));
	return;
}

static int
qla_send(qla_host_t *ha, struct mbuf **m_headp)
{
	bus_dma_segment_t	segs[QLA_MAX_SEGMENTS];
	bus_dmamap_t		map;
	int			nsegs;
	int			ret = -1;
	uint32_t		tx_idx;
	struct mbuf *m_head = *m_headp;

	QL_DPRINT8((ha->pci_dev, "%s: enter\n", __func__));

	if ((ret = bus_dmamap_create(ha->tx_tag, BUS_DMA_NOWAIT, &map))) {
		ha->err_tx_dmamap_create++;
		device_printf(ha->pci_dev,
			"%s: bus_dmamap_create failed[%d, %d]\n",
			__func__, ret, m_head->m_pkthdr.len);
		return (ret);
	}

	ret = bus_dmamap_load_mbuf_sg(ha->tx_tag, map, m_head, segs, &nsegs,
			BUS_DMA_NOWAIT);

	if (ret == EFBIG) {

		struct mbuf *m;

		QL_DPRINT8((ha->pci_dev, "%s: EFBIG [%d]\n", __func__,
			m_head->m_pkthdr.len));

		m = m_defrag(m_head, M_NOWAIT);
		if (m == NULL) {
			ha->err_tx_defrag++;
			m_freem(m_head);
			*m_headp = NULL;
			device_printf(ha->pci_dev,
				"%s: m_defrag() = NULL [%d]\n",
				__func__, ret);
			return (ENOBUFS);
		}
		m_head = m;

		if ((ret = bus_dmamap_load_mbuf_sg(ha->tx_tag, map, m_head,
					segs, &nsegs, BUS_DMA_NOWAIT))) {

			ha->err_tx_dmamap_load++;

			device_printf(ha->pci_dev,
				"%s: bus_dmamap_load_mbuf_sg failed0[%d, %d]\n",
				__func__, ret, m_head->m_pkthdr.len);

			bus_dmamap_destroy(ha->tx_tag, map);
			if (ret != ENOMEM) {
				m_freem(m_head);
				*m_headp = NULL;
			}
			return (ret);
		}
	} else if (ret) {
		ha->err_tx_dmamap_load++;

		device_printf(ha->pci_dev,
			"%s: bus_dmamap_load_mbuf_sg failed1[%d, %d]\n",
			__func__, ret, m_head->m_pkthdr.len);

		bus_dmamap_destroy(ha->tx_tag, map);

		if (ret != ENOMEM) {
			m_freem(m_head);
			*m_headp = NULL;
		}
		return (ret);
	}

	QL_ASSERT((nsegs != 0), ("qla_send: empty packet"));

	bus_dmamap_sync(ha->tx_tag, map, BUS_DMASYNC_PREWRITE);

	if (!(ret = qla_hw_send(ha, segs, nsegs, &tx_idx, m_head))) {
		ha->tx_buf[tx_idx].m_head = m_head;
		ha->tx_buf[tx_idx].map = map;
	} else {
		if (ret == EINVAL) {
			m_freem(m_head);
			*m_headp = NULL;
		}
	}

	QL_DPRINT8((ha->pci_dev, "%s: exit\n", __func__));
	return (ret);
}

static void
qla_stop(qla_host_t *ha)
{
	struct ifnet *ifp = ha->ifp;
	device_t	dev;

	dev = ha->pci_dev;

	ha->flags.qla_watchdog_pause = 1;
	qla_mdelay(__func__, 100);

	ha->flags.stop_rcv = 1;
	qla_hw_stop_rcv(ha);

	qla_del_hw_if(ha);

	qla_free_lro(ha);

	qla_free_xmt_bufs(ha);
	qla_free_rcv_bufs(ha);

	ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE | IFF_DRV_RUNNING);

	return;
}

/*
 * Buffer Management Functions for Transmit and Receive Rings
 */
static int
qla_alloc_xmt_bufs(qla_host_t *ha)
{
	if (bus_dma_tag_create(NULL,    /* parent */
		1, 0,    /* alignment, bounds */
		BUS_SPACE_MAXADDR,       /* lowaddr */
		BUS_SPACE_MAXADDR,       /* highaddr */
		NULL, NULL,      /* filter, filterarg */
		QLA_MAX_TSO_FRAME_SIZE,     /* maxsize */
		QLA_MAX_SEGMENTS,        /* nsegments */
		PAGE_SIZE,        /* maxsegsize */
		BUS_DMA_ALLOCNOW,        /* flags */
		NULL,    /* lockfunc */
		NULL,    /* lockfuncarg */
		&ha->tx_tag)) {
		device_printf(ha->pci_dev, "%s: tx_tag alloc failed\n",
			__func__);
		return (ENOMEM);
	}
	bzero((void *)ha->tx_buf, (sizeof(qla_tx_buf_t) * NUM_TX_DESCRIPTORS));

	return 0;
}

/*
 * Release mbuf after it sent on the wire
 */
static void
qla_clear_tx_buf(qla_host_t *ha, qla_tx_buf_t *txb)
{
	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	if (txb->m_head) {

		bus_dmamap_unload(ha->tx_tag, txb->map);
		bus_dmamap_destroy(ha->tx_tag, txb->map);

		m_freem(txb->m_head);
		txb->m_head = NULL;
	}

	QL_DPRINT2((ha->pci_dev, "%s: exit\n", __func__));
}

static void
qla_free_xmt_bufs(qla_host_t *ha)
{
	int		i;

	for (i = 0; i < NUM_TX_DESCRIPTORS; i++)
		qla_clear_tx_buf(ha, &ha->tx_buf[i]);

	if (ha->tx_tag != NULL) {
		bus_dma_tag_destroy(ha->tx_tag);
		ha->tx_tag = NULL;
	}
	bzero((void *)ha->tx_buf, (sizeof(qla_tx_buf_t) * NUM_TX_DESCRIPTORS));

	return;
}


static int
qla_alloc_rcv_bufs(qla_host_t *ha)
{
	int		i, j, ret = 0;
	qla_rx_buf_t	*rxb;

	if (bus_dma_tag_create(NULL,    /* parent */
			1, 0,    /* alignment, bounds */
			BUS_SPACE_MAXADDR,       /* lowaddr */
			BUS_SPACE_MAXADDR,       /* highaddr */
			NULL, NULL,      /* filter, filterarg */
			MJUM9BYTES,     /* maxsize */
			1,        /* nsegments */
			MJUM9BYTES,        /* maxsegsize */
			BUS_DMA_ALLOCNOW,        /* flags */
			NULL,    /* lockfunc */
			NULL,    /* lockfuncarg */
			&ha->rx_tag)) {

		device_printf(ha->pci_dev, "%s: rx_tag alloc failed\n",
			__func__);

		return (ENOMEM);
	}

	bzero((void *)ha->rx_buf, (sizeof(qla_rx_buf_t) * NUM_RX_DESCRIPTORS));
	bzero((void *)ha->rx_jbuf,
		(sizeof(qla_rx_buf_t) * NUM_RX_JUMBO_DESCRIPTORS));

	for (i = 0; i < MAX_SDS_RINGS; i++) {
		ha->hw.sds[i].sdsr_next = 0;
		ha->hw.sds[i].rxb_free = NULL;
		ha->hw.sds[i].rx_free = 0;
		ha->hw.sds[i].rxjb_free = NULL;
		ha->hw.sds[i].rxj_free = 0;
	}

	for (i = 0; i < NUM_RX_DESCRIPTORS; i++) {

		rxb = &ha->rx_buf[i];

		ret = bus_dmamap_create(ha->rx_tag, BUS_DMA_NOWAIT, &rxb->map);

		if (ret) {
			device_printf(ha->pci_dev,
				"%s: dmamap[%d] failed\n", __func__, i);

			for (j = 0; j < i; j++) {
				bus_dmamap_destroy(ha->rx_tag,
					ha->rx_buf[j].map);
			}
			goto qla_alloc_rcv_bufs_failed;
		}
	}

	qla_init_hw_rcv_descriptors(ha, RDS_RING_INDEX_NORMAL);

	for (i = 0; i < NUM_RX_DESCRIPTORS; i++) {
		rxb = &ha->rx_buf[i];
		rxb->handle = i;
		if (!(ret = qla_get_mbuf(ha, rxb, NULL, 0))) {
			/*
		 	 * set the physical address in the corresponding
			 * descriptor entry in the receive ring/queue for the
			 * hba 
			 */
			qla_set_hw_rcv_desc(ha, RDS_RING_INDEX_NORMAL, i,
				rxb->handle, rxb->paddr,
				(rxb->m_head)->m_pkthdr.len);
		} else {
			device_printf(ha->pci_dev,
				"%s: qla_get_mbuf [standard(%d)] failed\n",
				__func__, i);
			bus_dmamap_destroy(ha->rx_tag, rxb->map);
			goto qla_alloc_rcv_bufs_failed;
		}
	}


	for (i = 0; i < NUM_RX_JUMBO_DESCRIPTORS; i++) {

		rxb = &ha->rx_jbuf[i];

		ret = bus_dmamap_create(ha->rx_tag, BUS_DMA_NOWAIT, &rxb->map);

		if (ret) {
			device_printf(ha->pci_dev,
				"%s: dmamap[%d] failed\n", __func__, i);

			for (j = 0; j < i; j++) {
				bus_dmamap_destroy(ha->rx_tag,
					ha->rx_jbuf[j].map);
			}
			goto qla_alloc_rcv_bufs_failed;
		}
	}

	qla_init_hw_rcv_descriptors(ha, RDS_RING_INDEX_JUMBO);

	for (i = 0; i < NUM_RX_JUMBO_DESCRIPTORS; i++) {
		rxb = &ha->rx_jbuf[i];
		rxb->handle = i;
		if (!(ret = qla_get_mbuf(ha, rxb, NULL, 1))) {
			/*
		 	 * set the physical address in the corresponding
			 * descriptor entry in the receive ring/queue for the
			 * hba 
			 */
			qla_set_hw_rcv_desc(ha, RDS_RING_INDEX_JUMBO, i,
				rxb->handle, rxb->paddr,
				(rxb->m_head)->m_pkthdr.len);
		} else {
			device_printf(ha->pci_dev,
				"%s: qla_get_mbuf [jumbo(%d)] failed\n",
				__func__, i);
			bus_dmamap_destroy(ha->rx_tag, rxb->map);
			goto qla_alloc_rcv_bufs_failed;
		}
	}

	return (0);

qla_alloc_rcv_bufs_failed:
	qla_free_rcv_bufs(ha);
	return (ret);
}

static void
qla_free_rcv_bufs(qla_host_t *ha)
{
	int		i;
	qla_rx_buf_t	*rxb;

	for (i = 0; i < NUM_RX_DESCRIPTORS; i++) {
		rxb = &ha->rx_buf[i];
		if (rxb->m_head != NULL) {
			bus_dmamap_unload(ha->rx_tag, rxb->map);
			bus_dmamap_destroy(ha->rx_tag, rxb->map);
			m_freem(rxb->m_head);
			rxb->m_head = NULL;
		}
	}

	for (i = 0; i < NUM_RX_JUMBO_DESCRIPTORS; i++) {
		rxb = &ha->rx_jbuf[i];
		if (rxb->m_head != NULL) {
			bus_dmamap_unload(ha->rx_tag, rxb->map);
			bus_dmamap_destroy(ha->rx_tag, rxb->map);
			m_freem(rxb->m_head);
			rxb->m_head = NULL;
		}
	}

	if (ha->rx_tag != NULL) {
		bus_dma_tag_destroy(ha->rx_tag);
		ha->rx_tag = NULL;
	}

	bzero((void *)ha->rx_buf, (sizeof(qla_rx_buf_t) * NUM_RX_DESCRIPTORS));
	bzero((void *)ha->rx_jbuf,
		(sizeof(qla_rx_buf_t) * NUM_RX_JUMBO_DESCRIPTORS));

	for (i = 0; i < MAX_SDS_RINGS; i++) {
		ha->hw.sds[i].sdsr_next = 0;
		ha->hw.sds[i].rxb_free = NULL;
		ha->hw.sds[i].rx_free = 0;
		ha->hw.sds[i].rxjb_free = NULL;
		ha->hw.sds[i].rxj_free = 0;
	}

	return;
}

int
qla_get_mbuf(qla_host_t *ha, qla_rx_buf_t *rxb, struct mbuf *nmp,
	uint32_t jumbo)
{
	struct mbuf *mp = nmp;
	struct ifnet   *ifp;
	int             ret = 0;
	uint32_t	offset;

	QL_DPRINT2((ha->pci_dev, "%s: jumbo(0x%x) enter\n", __func__, jumbo));

	ifp = ha->ifp;

	if (mp == NULL) {

		if (!jumbo) {
			mp = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);

			if (mp == NULL) {
				ha->err_m_getcl++;
				ret = ENOBUFS;
				device_printf(ha->pci_dev,
					"%s: m_getcl failed\n", __func__);
				goto exit_qla_get_mbuf;
			}
			mp->m_len = mp->m_pkthdr.len = MCLBYTES;
		} else {
			mp = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
				MJUM9BYTES);
			if (mp == NULL) {
				ha->err_m_getjcl++;
				ret = ENOBUFS;
				device_printf(ha->pci_dev,
					"%s: m_getjcl failed\n", __func__);
				goto exit_qla_get_mbuf;
			}
			mp->m_len = mp->m_pkthdr.len = MJUM9BYTES;
		}
	} else {
		if (!jumbo)
			mp->m_len = mp->m_pkthdr.len = MCLBYTES;
		else
			mp->m_len = mp->m_pkthdr.len = MJUM9BYTES;

		mp->m_data = mp->m_ext.ext_buf;
		mp->m_next = NULL;
	}


	offset = (uint32_t)((unsigned long long)mp->m_data & 0x7ULL);
	if (offset) {
		offset = 8 - offset;
		m_adj(mp, offset);
	}

	/*
	 * Using memory from the mbuf cluster pool, invoke the bus_dma
	 * machinery to arrange the memory mapping.
	 */
	ret = bus_dmamap_load(ha->rx_tag, rxb->map,
				mtod(mp, void *), mp->m_len,
				qla_dmamap_callback, &rxb->paddr,
				BUS_DMA_NOWAIT);
	if (ret || !rxb->paddr) {
		m_free(mp);
		rxb->m_head = NULL;
		device_printf(ha->pci_dev,
			"%s: bus_dmamap_load failed\n", __func__);
                ret = -1;
		goto exit_qla_get_mbuf;
	}
	rxb->m_head = mp;
	bus_dmamap_sync(ha->rx_tag, rxb->map, BUS_DMASYNC_PREREAD);

exit_qla_get_mbuf:
	QL_DPRINT2((ha->pci_dev, "%s: exit ret = 0x%08x\n", __func__, ret));
	return (ret);
}

static void
qla_tx_done(void *context, int pending)
{
	qla_host_t *ha = context;

	qla_hw_tx_done(ha);
	qla_start(ha->ifp);
}

