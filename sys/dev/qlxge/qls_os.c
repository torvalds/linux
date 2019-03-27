/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Qlogic Corporation
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
 *  and ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
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
 * File: qls_os.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include "qls_os.h"
#include "qls_hw.h"
#include "qls_def.h"
#include "qls_inline.h"
#include "qls_ver.h"
#include "qls_glbl.h"
#include "qls_dbg.h"
#include <sys/smp.h>

/*
 * Some PCI Configuration Space Related Defines
 */

#ifndef PCI_VENDOR_QLOGIC
#define PCI_VENDOR_QLOGIC	0x1077
#endif

#ifndef PCI_DEVICE_QLOGIC_8000
#define PCI_DEVICE_QLOGIC_8000	0x8000
#endif

#define PCI_QLOGIC_DEV8000 \
	((PCI_DEVICE_QLOGIC_8000 << 16) | PCI_VENDOR_QLOGIC)

/*
 * static functions
 */
static int qls_alloc_parent_dma_tag(qla_host_t *ha);
static void qls_free_parent_dma_tag(qla_host_t *ha);

static void qls_flush_xmt_bufs(qla_host_t *ha);

static int qls_alloc_rcv_bufs(qla_host_t *ha);
static void qls_free_rcv_bufs(qla_host_t *ha);

static void qls_init_ifnet(device_t dev, qla_host_t *ha);
static void qls_release(qla_host_t *ha);
static void qls_dmamap_callback(void *arg, bus_dma_segment_t *segs, int nsegs,
		int error);
static void qls_stop(qla_host_t *ha);
static int qls_send(qla_host_t *ha, struct mbuf **m_headp);
static void qls_tx_done(void *context, int pending);

static int qls_config_lro(qla_host_t *ha);
static void qls_free_lro(qla_host_t *ha);

static void qls_error_recovery(void *context, int pending);

/*
 * Hooks to the Operating Systems
 */
static int qls_pci_probe (device_t);
static int qls_pci_attach (device_t);
static int qls_pci_detach (device_t);

static void qls_start(struct ifnet *ifp);
static void qls_init(void *arg);
static int qls_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static int qls_media_change(struct ifnet *ifp);
static void qls_media_status(struct ifnet *ifp, struct ifmediareq *ifmr);

static device_method_t qla_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, qls_pci_probe),
	DEVMETHOD(device_attach, qls_pci_attach),
	DEVMETHOD(device_detach, qls_pci_detach),
	{ 0, 0 }
};

static driver_t qla_pci_driver = {
	"ql", qla_pci_methods, sizeof (qla_host_t),
};

static devclass_t qla8000_devclass;

DRIVER_MODULE(qla8000, pci, qla_pci_driver, qla8000_devclass, 0, 0);

MODULE_DEPEND(qla8000, pci, 1, 1, 1);
MODULE_DEPEND(qla8000, ether, 1, 1, 1);

MALLOC_DEFINE(M_QLA8000BUF, "qla8000buf", "Buffers for qla8000 driver");

static char dev_str[64];
static char ver_str[64];

/*
 * Name:	qls_pci_probe
 * Function:	Validate the PCI device to be a QLA80XX device
 */
static int
qls_pci_probe(device_t dev)
{
        switch ((pci_get_device(dev) << 16) | (pci_get_vendor(dev))) {
        case PCI_QLOGIC_DEV8000:
		snprintf(dev_str, sizeof(dev_str), "%s v%d.%d.%d",
			"Qlogic ISP 8000 PCI CNA Adapter-Ethernet Function",
			QLA_VERSION_MAJOR, QLA_VERSION_MINOR,
			QLA_VERSION_BUILD);
		snprintf(ver_str, sizeof(ver_str), "v%d.%d.%d",
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

static int
qls_sysctl_get_drvr_stats(SYSCTL_HANDLER_ARGS)
{
        int err = 0, ret;
        qla_host_t *ha;
        uint32_t i;

        err = sysctl_handle_int(oidp, &ret, 0, req);

        if (err || !req->newptr)
                return (err);

        if (ret == 1) {

                ha = (qla_host_t *)arg1;

                for (i = 0; i < ha->num_tx_rings; i++) {

                        device_printf(ha->pci_dev,
                                "%s: tx_ring[%d].tx_frames= %p\n",
				__func__, i,
                                (void *)ha->tx_ring[i].tx_frames);

                        device_printf(ha->pci_dev,
                                "%s: tx_ring[%d].tx_tso_frames= %p\n",
				__func__, i,
                                (void *)ha->tx_ring[i].tx_tso_frames);

                        device_printf(ha->pci_dev,
                                "%s: tx_ring[%d].tx_vlan_frames= %p\n",
				__func__, i,
                                (void *)ha->tx_ring[i].tx_vlan_frames);

                        device_printf(ha->pci_dev,
                                "%s: tx_ring[%d].txr_free= 0x%08x\n",
				__func__, i,
                                ha->tx_ring[i].txr_free);

                        device_printf(ha->pci_dev,
                                "%s: tx_ring[%d].txr_next= 0x%08x\n",
				__func__, i,
                                ha->tx_ring[i].txr_next);

                        device_printf(ha->pci_dev,
                                "%s: tx_ring[%d].txr_done= 0x%08x\n",
				__func__, i,
                                ha->tx_ring[i].txr_done);

                        device_printf(ha->pci_dev,
                                "%s: tx_ring[%d].txr_cons_idx= 0x%08x\n",
				__func__, i,
                                *(ha->tx_ring[i].txr_cons_vaddr));
		}

                for (i = 0; i < ha->num_rx_rings; i++) {

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].rx_int= %p\n",
				__func__, i,
                                (void *)ha->rx_ring[i].rx_int);

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].rss_int= %p\n",
				__func__, i,
                                (void *)ha->rx_ring[i].rss_int);

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].lbq_next= 0x%08x\n",
				__func__, i,
                                ha->rx_ring[i].lbq_next);

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].lbq_free= 0x%08x\n",
				__func__, i,
                                ha->rx_ring[i].lbq_free);

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].lbq_in= 0x%08x\n",
				__func__, i,
                                ha->rx_ring[i].lbq_in);

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].sbq_next= 0x%08x\n",
				__func__, i,
                                ha->rx_ring[i].sbq_next);

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].sbq_free= 0x%08x\n",
				__func__, i,
                                ha->rx_ring[i].sbq_free);

                        device_printf(ha->pci_dev,
                                "%s: rx_ring[%d].sbq_in= 0x%08x\n",
				__func__, i,
                                ha->rx_ring[i].sbq_in);
		}

		device_printf(ha->pci_dev, "%s: err_m_getcl = 0x%08x\n",
				__func__, ha->err_m_getcl);
		device_printf(ha->pci_dev, "%s: err_m_getjcl = 0x%08x\n",
				__func__, ha->err_m_getjcl);
		device_printf(ha->pci_dev,
				"%s: err_tx_dmamap_create = 0x%08x\n",
				__func__, ha->err_tx_dmamap_create);
		device_printf(ha->pci_dev,
				"%s: err_tx_dmamap_load = 0x%08x\n",
				__func__, ha->err_tx_dmamap_load);
		device_printf(ha->pci_dev,
				"%s: err_tx_defrag = 0x%08x\n",
				__func__, ha->err_tx_defrag);
        }
        return (err);
}

static void
qls_add_sysctls(qla_host_t *ha)
{
        device_t dev = ha->pci_dev;

	SYSCTL_ADD_STRING(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "version", CTLFLAG_RD,
		ver_str, 0, "Driver Version");

	qls_dbg_level = 0;
        SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "debug", CTLFLAG_RW,
                &qls_dbg_level, qls_dbg_level, "Debug Level");

        SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
                SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                OID_AUTO, "drvr_stats", CTLTYPE_INT | CTLFLAG_RW,
                (void *)ha, 0,
                qls_sysctl_get_drvr_stats, "I", "Driver Maintained Statistics");

        return;
}

static void
qls_watchdog(void *arg)
{
	qla_host_t *ha = arg;
	struct ifnet *ifp;

	ifp = ha->ifp;

        if (ha->flags.qla_watchdog_exit) {
		ha->qla_watchdog_exited = 1;
		return;
	}
	ha->qla_watchdog_exited = 0;

	if (!ha->flags.qla_watchdog_pause) {

		if (ha->qla_initiate_recovery) {

			ha->qla_watchdog_paused = 1;
			ha->qla_initiate_recovery = 0;
			ha->err_inject = 0;
			taskqueue_enqueue(ha->err_tq, &ha->err_task);

		} else if ((ifp->if_snd.ifq_head != NULL) && QL_RUNNING(ifp)) {

			taskqueue_enqueue(ha->tx_tq, &ha->tx_task);
		}

		ha->qla_watchdog_paused = 0;
	} else {
		ha->qla_watchdog_paused = 1;
	}

	ha->watchdog_ticks = ha->watchdog_ticks++ % 1000;
	callout_reset(&ha->tx_callout, QLA_WATCHDOG_CALLOUT_TICKS,
		qls_watchdog, ha);

	return;
}

/*
 * Name:	qls_pci_attach
 * Function:	attaches the device to the operating system
 */
static int
qls_pci_attach(device_t dev)
{
	qla_host_t *ha = NULL;
	int i;

	QL_DPRINT2((dev, "%s: enter\n", __func__));

        if ((ha = device_get_softc(dev)) == NULL) {
                device_printf(dev, "cannot get softc\n");
                return (ENOMEM);
        }

        memset(ha, 0, sizeof (qla_host_t));

        if (pci_get_device(dev) != PCI_DEVICE_QLOGIC_8000) {
                device_printf(dev, "device is not QLE8000\n");
                return (ENXIO);
	}

        ha->pci_func = pci_get_function(dev);

        ha->pci_dev = dev;

	pci_enable_busmaster(dev);

	ha->reg_rid = PCIR_BAR(1);
	ha->pci_reg = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &ha->reg_rid,
				RF_ACTIVE);

        if (ha->pci_reg == NULL) {
                device_printf(dev, "unable to map any ports\n");
                goto qls_pci_attach_err;
        }

	ha->reg_rid1 = PCIR_BAR(3);
	ha->pci_reg1 = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
			&ha->reg_rid1, RF_ACTIVE);

        if (ha->pci_reg1 == NULL) {
                device_printf(dev, "unable to map any ports\n");
                goto qls_pci_attach_err;
        }

	mtx_init(&ha->hw_lock, "qla80xx_hw_lock", MTX_NETWORK_LOCK, MTX_DEF);
	mtx_init(&ha->tx_lock, "qla80xx_tx_lock", MTX_NETWORK_LOCK, MTX_DEF);

	qls_add_sysctls(ha);
	qls_hw_add_sysctls(ha);

	ha->flags.lock_init = 1;

	ha->msix_count = pci_msix_count(dev);

	if (ha->msix_count < qls_get_msix_count(ha)) {
		device_printf(dev, "%s: msix_count[%d] not enough\n", __func__,
			ha->msix_count);
		goto qls_pci_attach_err;
	}

	ha->msix_count = qls_get_msix_count(ha);

	device_printf(dev, "\n%s: ha %p pci_func 0x%x  msix_count 0x%x"
		" pci_reg %p pci_reg1 %p\n", __func__, ha,
		ha->pci_func, ha->msix_count, ha->pci_reg, ha->pci_reg1);

	if (pci_alloc_msix(dev, &ha->msix_count)) {
		device_printf(dev, "%s: pci_alloc_msi[%d] failed\n", __func__,
			ha->msix_count);
		ha->msix_count = 0;
		goto qls_pci_attach_err;
	}

        for (i = 0; i < ha->num_rx_rings; i++) {
                ha->irq_vec[i].cq_idx = i;
                ha->irq_vec[i].ha = ha;
                ha->irq_vec[i].irq_rid = 1 + i;

                ha->irq_vec[i].irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
                                &ha->irq_vec[i].irq_rid,
                                (RF_ACTIVE | RF_SHAREABLE));

                if (ha->irq_vec[i].irq == NULL) {
                        device_printf(dev, "could not allocate interrupt\n");
                        goto qls_pci_attach_err;
                }

		if (bus_setup_intr(dev, ha->irq_vec[i].irq,
			(INTR_TYPE_NET | INTR_MPSAFE), NULL, qls_isr,
			&ha->irq_vec[i], &ha->irq_vec[i].handle)) {
				device_printf(dev,
					"could not setup interrupt\n");
			goto qls_pci_attach_err;
		}
        }

	qls_rd_nic_params(ha);

	/* allocate parent dma tag */
	if (qls_alloc_parent_dma_tag(ha)) {
		device_printf(dev, "%s: qls_alloc_parent_dma_tag failed\n",
			__func__);
		goto qls_pci_attach_err;
	}

	/* alloc all dma buffers */
	if (qls_alloc_dma(ha)) {
		device_printf(dev, "%s: qls_alloc_dma failed\n", __func__);
		goto qls_pci_attach_err;
	}

	/* create the o.s ethernet interface */
	qls_init_ifnet(dev, ha);

	ha->flags.qla_watchdog_active = 1;
	ha->flags.qla_watchdog_pause = 1;

	TASK_INIT(&ha->tx_task, 0, qls_tx_done, ha);
	ha->tx_tq = taskqueue_create_fast("qla_txq", M_NOWAIT,
			taskqueue_thread_enqueue, &ha->tx_tq);
	taskqueue_start_threads(&ha->tx_tq, 1, PI_NET, "%s txq",
		device_get_nameunit(ha->pci_dev));
	
	callout_init(&ha->tx_callout, 1);
	ha->flags.qla_callout_init = 1;

        /* create ioctl device interface */
        if (qls_make_cdev(ha)) {
                device_printf(dev, "%s: qls_make_cdev failed\n", __func__);
                goto qls_pci_attach_err;
        }

	callout_reset(&ha->tx_callout, QLA_WATCHDOG_CALLOUT_TICKS,
		qls_watchdog, ha);

        TASK_INIT(&ha->err_task, 0, qls_error_recovery, ha);
        ha->err_tq = taskqueue_create_fast("qla_errq", M_NOWAIT,
                        taskqueue_thread_enqueue, &ha->err_tq);
        taskqueue_start_threads(&ha->err_tq, 1, PI_NET, "%s errq",
                device_get_nameunit(ha->pci_dev));

	QL_DPRINT2((dev, "%s: exit 0\n", __func__));
        return (0);

qls_pci_attach_err:

	qls_release(ha);

	QL_DPRINT2((dev, "%s: exit ENXIO\n", __func__));
        return (ENXIO);
}

/*
 * Name:	qls_pci_detach
 * Function:	Unhooks the device from the operating system
 */
static int
qls_pci_detach(device_t dev)
{
	qla_host_t *ha = NULL;
	struct ifnet *ifp;

	QL_DPRINT2((dev, "%s: enter\n", __func__));

        if ((ha = device_get_softc(dev)) == NULL) {
                device_printf(dev, "cannot get softc\n");
                return (ENOMEM);
        }

	ifp = ha->ifp;

	(void)QLA_LOCK(ha, __func__, 0);
	qls_stop(ha);
	QLA_UNLOCK(ha, __func__);

	qls_release(ha);

	QL_DPRINT2((dev, "%s: exit\n", __func__));

        return (0);
}

/*
 * Name:	qls_release
 * Function:	Releases the resources allocated for the device
 */
static void
qls_release(qla_host_t *ha)
{
	device_t dev;
	int i;

	dev = ha->pci_dev;

	if (ha->err_tq) {
		taskqueue_drain(ha->err_tq, &ha->err_task);
		taskqueue_free(ha->err_tq);
	}

	if (ha->tx_tq) {
		taskqueue_drain(ha->tx_tq, &ha->tx_task);
		taskqueue_free(ha->tx_tq);
	}

	qls_del_cdev(ha);

	if (ha->flags.qla_watchdog_active) {
		ha->flags.qla_watchdog_exit = 1;

		while (ha->qla_watchdog_exited == 0)
			qls_mdelay(__func__, 1);
	}

	if (ha->flags.qla_callout_init)
		callout_stop(&ha->tx_callout);

	if (ha->ifp != NULL)
		ether_ifdetach(ha->ifp);

	qls_free_dma(ha); 
	qls_free_parent_dma_tag(ha);

        for (i = 0; i < ha->num_rx_rings; i++) {

                if (ha->irq_vec[i].handle) {
                        (void)bus_teardown_intr(dev, ha->irq_vec[i].irq,
                                        ha->irq_vec[i].handle);
                }

                if (ha->irq_vec[i].irq) {
                        (void)bus_release_resource(dev, SYS_RES_IRQ,
                                ha->irq_vec[i].irq_rid,
                                ha->irq_vec[i].irq);
                }
        }

	if (ha->msix_count)
		pci_release_msi(dev);

	if (ha->flags.lock_init) {
		mtx_destroy(&ha->tx_lock);
		mtx_destroy(&ha->hw_lock);
	}

        if (ha->pci_reg)
                (void) bus_release_resource(dev, SYS_RES_MEMORY, ha->reg_rid,
				ha->pci_reg);

        if (ha->pci_reg1)
                (void) bus_release_resource(dev, SYS_RES_MEMORY, ha->reg_rid1,
				ha->pci_reg1);
}

/*
 * DMA Related Functions
 */

static void
qls_dmamap_callback(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
        *((bus_addr_t *)arg) = 0;

        if (error) {
                printf("%s: bus_dmamap_load failed (%d)\n", __func__, error);
                return;
	}

        *((bus_addr_t *)arg) = segs[0].ds_addr;

	return;
}

int
qls_alloc_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf)
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
                goto qls_alloc_dmabuf_exit;
        }
        ret = bus_dmamem_alloc(dma_buf->dma_tag,
                        (void **)&dma_buf->dma_b,
                        (BUS_DMA_ZERO | BUS_DMA_COHERENT | BUS_DMA_NOWAIT),
                        &dma_buf->dma_map);
        if (ret) {
                bus_dma_tag_destroy(dma_buf->dma_tag);
                device_printf(dev, "%s: bus_dmamem_alloc failed\n", __func__);
                goto qls_alloc_dmabuf_exit;
        }

        ret = bus_dmamap_load(dma_buf->dma_tag,
                        dma_buf->dma_map,
                        dma_buf->dma_b,
                        dma_buf->size,
                        qls_dmamap_callback,
                        &b_addr, BUS_DMA_NOWAIT);

        if (ret || !b_addr) {
                bus_dma_tag_destroy(dma_buf->dma_tag);
                bus_dmamem_free(dma_buf->dma_tag, dma_buf->dma_b,
                        dma_buf->dma_map);
                ret = -1;
                goto qls_alloc_dmabuf_exit;
        }

        dma_buf->dma_addr = b_addr;

qls_alloc_dmabuf_exit:
        QL_DPRINT2((dev, "%s: exit ret 0x%08x tag %p map %p b %p sz 0x%x\n",
                __func__, ret, (void *)dma_buf->dma_tag,
                (void *)dma_buf->dma_map, (void *)dma_buf->dma_b,
		dma_buf->size));

        return ret;
}

void
qls_free_dmabuf(qla_host_t *ha, qla_dma_t *dma_buf)
{
        bus_dmamap_unload(dma_buf->dma_tag, dma_buf->dma_map);
        bus_dmamem_free(dma_buf->dma_tag, dma_buf->dma_b, dma_buf->dma_map);
        bus_dma_tag_destroy(dma_buf->dma_tag);
}

static int
qls_alloc_parent_dma_tag(qla_host_t *ha)
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
qls_free_parent_dma_tag(qla_host_t *ha)
{
        if (ha->flags.parent_tag) {
                bus_dma_tag_destroy(ha->parent_tag);
                ha->flags.parent_tag = 0;
        }
}

/*
 * Name: qls_init_ifnet
 * Function: Creates the Network Device Interface and Registers it with the O.S
 */

static void
qls_init_ifnet(device_t dev, qla_host_t *ha)
{
	struct ifnet *ifp;

	QL_DPRINT2((dev, "%s: enter\n", __func__));

	ifp = ha->ifp = if_alloc(IFT_ETHER);

	if (ifp == NULL)
		panic("%s: cannot if_alloc()\n", device_get_nameunit(dev));

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_baudrate = IF_Gbps(10);
	ifp->if_init = qls_init;
	ifp->if_softc = ha;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = qls_ioctl;
	ifp->if_start = qls_start;

	IFQ_SET_MAXLEN(&ifp->if_snd, qls_get_ifq_snd_maxlen(ha));
	ifp->if_snd.ifq_drv_maxlen = qls_get_ifq_snd_maxlen(ha);
	IFQ_SET_READY(&ifp->if_snd);

	ha->max_frame_size = ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;
	if (ha->max_frame_size <= MCLBYTES) {
		ha->msize = MCLBYTES;
	} else if (ha->max_frame_size <= MJUMPAGESIZE) {
		ha->msize = MJUMPAGESIZE;
	} else
		ha->msize = MJUM9BYTES;

	ether_ifattach(ifp, qls_get_mac_addr(ha));

	ifp->if_capabilities = IFCAP_JUMBO_MTU;

	ifp->if_capabilities |= IFCAP_HWCSUM;
	ifp->if_capabilities |= IFCAP_VLAN_MTU;

	ifp->if_capabilities |= IFCAP_TSO4;
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
	ifp->if_capabilities |= IFCAP_VLAN_HWTSO;
	ifp->if_capabilities |= IFCAP_LINKSTATE;

	ifp->if_capenable = ifp->if_capabilities;

	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	ifmedia_init(&ha->media, IFM_IMASK, qls_media_change, qls_media_status);

	ifmedia_add(&ha->media, (IFM_ETHER | qls_get_optics(ha) | IFM_FDX), 0,
		NULL);
	ifmedia_add(&ha->media, (IFM_ETHER | IFM_AUTO), 0, NULL);

	ifmedia_set(&ha->media, (IFM_ETHER | IFM_AUTO));

	QL_DPRINT2((dev, "%s: exit\n", __func__));

	return;
}

static void
qls_init_locked(qla_host_t *ha)
{
	struct ifnet *ifp = ha->ifp;

	qls_stop(ha);

	qls_flush_xmt_bufs(ha);

	if (qls_alloc_rcv_bufs(ha) != 0)
		return;

	if (qls_config_lro(ha))
		return;

	bcopy(IF_LLADDR(ha->ifp), ha->mac_addr, ETHER_ADDR_LEN);

	ifp->if_hwassist = CSUM_IP;
	ifp->if_hwassist |= CSUM_TCP;
	ifp->if_hwassist |= CSUM_UDP;
	ifp->if_hwassist |= CSUM_TSO;

 	if (qls_init_hw_if(ha) == 0) {
		ifp = ha->ifp;
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		ha->flags.qla_watchdog_pause = 0;
	}

	return;
}

static void
qls_init(void *arg)
{
	qla_host_t *ha;

	ha = (qla_host_t *)arg;

	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	(void)QLA_LOCK(ha, __func__, 0);
	qls_init_locked(ha);
	QLA_UNLOCK(ha, __func__);

	QL_DPRINT2((ha->pci_dev, "%s: exit\n", __func__));
}

static void
qls_set_multi(qla_host_t *ha, uint32_t add_multi)
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

	if (QLA_LOCK(ha, __func__, 1) == 0) {
		qls_hw_set_multi(ha, mta, mcnt, add_multi);
		QLA_UNLOCK(ha, __func__);
	}

	return;
}

static int
qls_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
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
				(void)QLA_LOCK(ha, __func__, 0);
				qls_init_locked(ha);
				QLA_UNLOCK(ha, __func__);
			}
			QL_DPRINT4((ha->pci_dev,
				"%s: SIOCSIFADDR (0x%lx) ipv4 [0x%08x]\n",
				__func__, cmd,
				ntohl(IA_SIN(ifa)->sin_addr.s_addr)));

			arp_ifinit(ifp, ifa);
		} else {
			ether_ioctl(ifp, cmd, data);
		}
		break;

	case SIOCSIFMTU:
		QL_DPRINT4((ha->pci_dev, "%s: SIOCSIFMTU (0x%lx)\n",
			__func__, cmd));

		if (ifr->ifr_mtu > QLA_MAX_MTU) {
			ret = EINVAL;
		} else {
			(void) QLA_LOCK(ha, __func__, 0);

			ifp->if_mtu = ifr->ifr_mtu;
			ha->max_frame_size =
				ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;

			QLA_UNLOCK(ha, __func__);

			if (ret)
				ret = EINVAL;
		}

		break;

	case SIOCSIFFLAGS:
		QL_DPRINT4((ha->pci_dev, "%s: SIOCSIFFLAGS (0x%lx)\n",
			__func__, cmd));

		(void)QLA_LOCK(ha, __func__, 0);

		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				if ((ifp->if_flags ^ ha->if_flags) &
					IFF_PROMISC) {
					ret = qls_set_promisc(ha);
				} else if ((ifp->if_flags ^ ha->if_flags) &
					IFF_ALLMULTI) {
					ret = qls_set_allmulti(ha);
				}
			} else {
				ha->max_frame_size = ifp->if_mtu +
					ETHER_HDR_LEN + ETHER_CRC_LEN;
				qls_init_locked(ha);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				qls_stop(ha);
			ha->if_flags = ifp->if_flags;
		}

		QLA_UNLOCK(ha, __func__);
		break;

	case SIOCADDMULTI:
		QL_DPRINT4((ha->pci_dev,
			"%s: %s (0x%lx)\n", __func__, "SIOCADDMULTI", cmd));

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			qls_set_multi(ha, 1);
		}
		break;

	case SIOCDELMULTI:
		QL_DPRINT4((ha->pci_dev,
			"%s: %s (0x%lx)\n", __func__, "SIOCDELMULTI", cmd));

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			qls_set_multi(ha, 0);
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
		if (mask & IFCAP_VLAN_HWTAGGING)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
		if (mask & IFCAP_VLAN_HWTSO)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;

		if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
			qls_init(ha);

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
qls_media_change(struct ifnet *ifp)
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
qls_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	qla_host_t *ha;

	ha = (qla_host_t *)ifp->if_softc;

	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;
	
	qls_update_link_state(ha);
	if (ha->link_up) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active |= (IFM_FDX | qls_get_optics(ha));
	}

	QL_DPRINT2((ha->pci_dev, "%s: exit (%s)\n", __func__,\
		(ha->link_up ? "link_up" : "link_down")));

	return;
}

static void
qls_start(struct ifnet *ifp)
{
	int		i, ret = 0;
	struct mbuf	*m_head;
	qla_host_t	*ha = (qla_host_t *)ifp->if_softc;

	QL_DPRINT8((ha->pci_dev, "%s: enter\n", __func__));

	if (!mtx_trylock(&ha->tx_lock)) {
		QL_DPRINT8((ha->pci_dev,
			"%s: mtx_trylock(&ha->tx_lock) failed\n", __func__));
		return;
	}

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) == 
		IFF_DRV_RUNNING) {

		for (i = 0; i < ha->num_tx_rings; i++) {
			ret |= qls_hw_tx_done(ha, i);
		}

		if (ret == 0)
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	}

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) != 
		IFF_DRV_RUNNING) {
		QL_DPRINT8((ha->pci_dev, "%s: !IFF_DRV_RUNNING\n", __func__));
		QLA_TX_UNLOCK(ha);
		return;
	}

	if (!ha->link_up) {
		qls_update_link_state(ha);
		if (!ha->link_up) {
			QL_DPRINT8((ha->pci_dev, "%s: link down\n", __func__));
			QLA_TX_UNLOCK(ha);
			return;
		}
	}

	while (ifp->if_snd.ifq_head != NULL) {

		IF_DEQUEUE(&ifp->if_snd, m_head);

		if (m_head == NULL) {
			QL_DPRINT8((ha->pci_dev, "%s: m_head == NULL\n",
				__func__));
			break;
		}

		if (qls_send(ha, &m_head)) {
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
qls_send(qla_host_t *ha, struct mbuf **m_headp)
{
	bus_dma_segment_t	segs[QLA_MAX_SEGMENTS];
	bus_dmamap_t		map;
	int			nsegs;
	int			ret = -1;
	uint32_t		tx_idx;
	struct mbuf		*m_head = *m_headp;
	uint32_t		txr_idx = 0;

	QL_DPRINT8((ha->pci_dev, "%s: enter\n", __func__));

	/* check if flowid is set */
	if (M_HASHTYPE_GET(m_head) != M_HASHTYPE_NONE)
		txr_idx = m_head->m_pkthdr.flowid & (ha->num_tx_rings - 1);

	tx_idx = ha->tx_ring[txr_idx].txr_next;

	map = ha->tx_ring[txr_idx].tx_buf[tx_idx].map;

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
		*m_headp = m_head;

		if ((ret = bus_dmamap_load_mbuf_sg(ha->tx_tag, map, m_head,
					segs, &nsegs, BUS_DMA_NOWAIT))) {

			ha->err_tx_dmamap_load++;

			device_printf(ha->pci_dev,
				"%s: bus_dmamap_load_mbuf_sg failed0[%d, %d]\n",
				__func__, ret, m_head->m_pkthdr.len);

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

		if (ret != ENOMEM) {
			m_freem(m_head);
			*m_headp = NULL;
		}
		return (ret);
	}

	QL_ASSERT(ha, (nsegs != 0), ("qls_send: empty packet"));

	bus_dmamap_sync(ha->tx_tag, map, BUS_DMASYNC_PREWRITE);

        if (!(ret = qls_hw_send(ha, segs, nsegs, tx_idx, m_head, txr_idx))) {

		ha->tx_ring[txr_idx].count++;
		ha->tx_ring[txr_idx].tx_buf[tx_idx].m_head = m_head;
		ha->tx_ring[txr_idx].tx_buf[tx_idx].map = map;
	} else {
		if (ret == EINVAL) {
			if (m_head)
				m_freem(m_head);
			*m_headp = NULL;
		}
	}

	QL_DPRINT8((ha->pci_dev, "%s: exit\n", __func__));
	return (ret);
}

static void
qls_stop(qla_host_t *ha)
{
	struct ifnet *ifp = ha->ifp;
	device_t	dev;

	dev = ha->pci_dev;

	ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE | IFF_DRV_RUNNING);

	ha->flags.qla_watchdog_pause = 1;

	while (!ha->qla_watchdog_paused)
		qls_mdelay(__func__, 1);

	qls_del_hw_if(ha);

	qls_free_lro(ha);

	qls_flush_xmt_bufs(ha);
	qls_free_rcv_bufs(ha);

	return;
}

/*
 * Buffer Management Functions for Transmit and Receive Rings
 */
/*
 * Release mbuf after it sent on the wire
 */
static void
qls_flush_tx_buf(qla_host_t *ha, qla_tx_buf_t *txb)
{
	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	if (txb->m_head) {

		bus_dmamap_unload(ha->tx_tag, txb->map);

		m_freem(txb->m_head);
		txb->m_head = NULL;
	}

	QL_DPRINT2((ha->pci_dev, "%s: exit\n", __func__));
}

static void
qls_flush_xmt_bufs(qla_host_t *ha)
{
	int		i, j;

	for (j = 0; j < ha->num_tx_rings; j++) {
		for (i = 0; i < NUM_TX_DESCRIPTORS; i++)
			qls_flush_tx_buf(ha, &ha->tx_ring[j].tx_buf[i]);
	}

	return;
}


static int
qls_alloc_rcv_mbufs(qla_host_t *ha, int r)
{
	int			i, j, ret = 0;
	qla_rx_buf_t		*rxb;
	qla_rx_ring_t		*rx_ring;
	volatile q81_bq_addr_e_t *sbq_e;


	rx_ring = &ha->rx_ring[r];

	for (i = 0; i < NUM_RX_DESCRIPTORS; i++) {

		rxb = &rx_ring->rx_buf[i];

		ret = bus_dmamap_create(ha->rx_tag, BUS_DMA_NOWAIT, &rxb->map);

		if (ret) {
			device_printf(ha->pci_dev,
				"%s: dmamap[%d, %d] failed\n", __func__, r, i);

			for (j = 0; j < i; j++) {
				rxb = &rx_ring->rx_buf[j];
				bus_dmamap_destroy(ha->rx_tag, rxb->map);
			}
			goto qls_alloc_rcv_mbufs_err;
		}
	}

	rx_ring = &ha->rx_ring[r];

	sbq_e = rx_ring->sbq_vaddr;

	rxb = &rx_ring->rx_buf[0];

	for (i = 0; i < NUM_RX_DESCRIPTORS; i++) {

		if (!(ret = qls_get_mbuf(ha, rxb, NULL))) {

			/*
		 	 * set the physical address in the
			 * corresponding descriptor entry in the
			 * receive ring/queue for the hba 
			 */

			sbq_e->addr_lo = rxb->paddr & 0xFFFFFFFF;
			sbq_e->addr_hi = (rxb->paddr >> 32) & 0xFFFFFFFF;

		} else {
			device_printf(ha->pci_dev,
				"%s: qls_get_mbuf [%d, %d] failed\n",
					__func__, r, i);
			bus_dmamap_destroy(ha->rx_tag, rxb->map);
			goto qls_alloc_rcv_mbufs_err;
		}

		rxb++;
		sbq_e++;
	}
	return 0;

qls_alloc_rcv_mbufs_err:
	return (-1);
}

static void
qls_free_rcv_bufs(qla_host_t *ha)
{
	int		i, r;
	qla_rx_buf_t	*rxb;
	qla_rx_ring_t	*rxr;

	for (r = 0; r < ha->num_rx_rings; r++) {

		rxr = &ha->rx_ring[r];

		for (i = 0; i < NUM_RX_DESCRIPTORS; i++) {

			rxb = &rxr->rx_buf[i];

			if (rxb->m_head != NULL) {
				bus_dmamap_unload(ha->rx_tag, rxb->map);
				bus_dmamap_destroy(ha->rx_tag, rxb->map);
				m_freem(rxb->m_head);
			}
		}
		bzero(rxr->rx_buf, (sizeof(qla_rx_buf_t) * NUM_RX_DESCRIPTORS));
	}
	return;
}

static int
qls_alloc_rcv_bufs(qla_host_t *ha)
{
	int		r, ret = 0;
	qla_rx_ring_t	*rxr;

	for (r = 0; r < ha->num_rx_rings; r++) {
		rxr = &ha->rx_ring[r];
		bzero(rxr->rx_buf, (sizeof(qla_rx_buf_t) * NUM_RX_DESCRIPTORS));
	}

	for (r = 0; r < ha->num_rx_rings; r++) {

		ret = qls_alloc_rcv_mbufs(ha, r);

		if (ret)
			qls_free_rcv_bufs(ha);
	}

	return (ret);
}

int
qls_get_mbuf(qla_host_t *ha, qla_rx_buf_t *rxb, struct mbuf *nmp)
{
	struct mbuf *mp = nmp;
	struct ifnet   		*ifp;
	int            		ret = 0;
	uint32_t		offset;
	bus_dma_segment_t	segs[1];
	int			nsegs;

	QL_DPRINT2((ha->pci_dev, "%s: enter\n", __func__));

	ifp = ha->ifp;

	if (mp == NULL) {

		mp = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, ha->msize);

		if (mp == NULL) {

			if (ha->msize == MCLBYTES)
				ha->err_m_getcl++;
			else
				ha->err_m_getjcl++;

			ret = ENOBUFS;
			device_printf(ha->pci_dev,
					"%s: m_getcl failed\n", __func__);
			goto exit_qls_get_mbuf;
		}
		mp->m_len = mp->m_pkthdr.len = ha->msize;
	} else {
		mp->m_len = mp->m_pkthdr.len = ha->msize;
		mp->m_data = mp->m_ext.ext_buf;
		mp->m_next = NULL;
	}

	/* align the receive buffers to 8 byte boundary */
	offset = (uint32_t)((unsigned long long)mp->m_data & 0x7ULL);
	if (offset) {
		offset = 8 - offset;
		m_adj(mp, offset);
	}

	/*
	 * Using memory from the mbuf cluster pool, invoke the bus_dma
	 * machinery to arrange the memory mapping.
	 */
	ret = bus_dmamap_load_mbuf_sg(ha->rx_tag, rxb->map,
			mp, segs, &nsegs, BUS_DMA_NOWAIT);
	rxb->paddr = segs[0].ds_addr;

	if (ret || !rxb->paddr || (nsegs != 1)) {
		m_freem(mp);
		rxb->m_head = NULL;
		device_printf(ha->pci_dev,
			"%s: bus_dmamap_load failed[%d, 0x%016llx, %d]\n",
			__func__, ret, (long long unsigned int)rxb->paddr,
			nsegs);
                ret = -1;
		goto exit_qls_get_mbuf;
	}
	rxb->m_head = mp;
	bus_dmamap_sync(ha->rx_tag, rxb->map, BUS_DMASYNC_PREREAD);

exit_qls_get_mbuf:
	QL_DPRINT2((ha->pci_dev, "%s: exit ret = 0x%08x\n", __func__, ret));
	return (ret);
}

static void
qls_tx_done(void *context, int pending)
{
	qla_host_t *ha = context;
	struct ifnet   *ifp;

	ifp = ha->ifp;

	if (!ifp) 
		return;

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		QL_DPRINT8((ha->pci_dev, "%s: !IFF_DRV_RUNNING\n", __func__));
		return;
	}

	qls_start(ha->ifp);
	return;
}

static int
qls_config_lro(qla_host_t *ha)
{
        int i;
        struct lro_ctrl *lro;

        for (i = 0; i < ha->num_rx_rings; i++) {
                lro = &ha->rx_ring[i].lro;
                if (tcp_lro_init(lro)) {
                        device_printf(ha->pci_dev, "%s: tcp_lro_init failed\n",
                                __func__);
                        return (-1);
                }
                lro->ifp = ha->ifp;
        }
        ha->flags.lro_init = 1;

        QL_DPRINT2((ha->pci_dev, "%s: LRO initialized\n", __func__));
        return (0);
}

static void
qls_free_lro(qla_host_t *ha)
{
        int i;
        struct lro_ctrl *lro;

        if (!ha->flags.lro_init)
                return;

        for (i = 0; i < ha->num_rx_rings; i++) {
                lro = &ha->rx_ring[i].lro;
                tcp_lro_free(lro);
        }
        ha->flags.lro_init = 0;
}

static void
qls_error_recovery(void *context, int pending)
{
        qla_host_t *ha = context;

	qls_init(ha);

	return;
}

