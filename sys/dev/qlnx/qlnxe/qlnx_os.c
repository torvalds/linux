/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
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
 * File: qlnx_os.c
 * Author : David C Somayajulu, Cavium, Inc., San Jose, CA 95131.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qlnx_os.h"
#include "bcm_osal.h"
#include "reg_addr.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore.h"
#include "ecore_chain.h"
#include "ecore_status.h"
#include "ecore_hw.h"
#include "ecore_rt_defs.h"
#include "ecore_init_ops.h"
#include "ecore_int.h"
#include "ecore_cxt.h"
#include "ecore_spq.h"
#include "ecore_init_fw_funcs.h"
#include "ecore_sp_commands.h"
#include "ecore_dev_api.h"
#include "ecore_l2_api.h"
#include "ecore_mcp.h"
#include "ecore_hw_defs.h"
#include "mcp_public.h"
#include "ecore_iro.h"
#include "nvm_cfg.h"
#include "ecore_dev_api.h"
#include "ecore_dbg_fw_funcs.h"
#include "ecore_iov_api.h"
#include "ecore_vf_api.h"

#include "qlnx_ioctl.h"
#include "qlnx_def.h"
#include "qlnx_ver.h"

#ifdef QLNX_ENABLE_IWARP
#include "qlnx_rdma.h"
#endif /* #ifdef QLNX_ENABLE_IWARP */

#include <sys/smp.h>


/*
 * static functions
 */
/*
 * ioctl related functions
 */
static void qlnx_add_sysctls(qlnx_host_t *ha);

/*
 * main driver
 */
static void qlnx_release(qlnx_host_t *ha);
static void qlnx_fp_isr(void *arg);
static void qlnx_init_ifnet(device_t dev, qlnx_host_t *ha);
static void qlnx_init(void *arg);
static void qlnx_init_locked(qlnx_host_t *ha);
static int qlnx_set_multi(qlnx_host_t *ha, uint32_t add_multi);
static int qlnx_set_promisc(qlnx_host_t *ha);
static int qlnx_set_allmulti(qlnx_host_t *ha);
static int qlnx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static int qlnx_media_change(struct ifnet *ifp);
static void qlnx_media_status(struct ifnet *ifp, struct ifmediareq *ifmr);
static void qlnx_stop(qlnx_host_t *ha);
static int qlnx_send(qlnx_host_t *ha, struct qlnx_fastpath *fp,
		struct mbuf **m_headp);
static int qlnx_get_ifq_snd_maxlen(qlnx_host_t *ha);
static uint32_t qlnx_get_optics(qlnx_host_t *ha,
			struct qlnx_link_output *if_link);
static int qlnx_transmit(struct ifnet *ifp, struct mbuf  *mp);
static int qlnx_transmit_locked(struct ifnet *ifp, struct qlnx_fastpath *fp,
		struct mbuf *mp);
static void qlnx_qflush(struct ifnet *ifp);

static int qlnx_alloc_parent_dma_tag(qlnx_host_t *ha);
static void qlnx_free_parent_dma_tag(qlnx_host_t *ha);
static int qlnx_alloc_tx_dma_tag(qlnx_host_t *ha);
static void qlnx_free_tx_dma_tag(qlnx_host_t *ha);
static int qlnx_alloc_rx_dma_tag(qlnx_host_t *ha);
static void qlnx_free_rx_dma_tag(qlnx_host_t *ha);

static int qlnx_get_mfw_version(qlnx_host_t *ha, uint32_t *mfw_ver);
static int qlnx_get_flash_size(qlnx_host_t *ha, uint32_t *flash_size);

static int qlnx_nic_setup(struct ecore_dev *cdev,
		struct ecore_pf_params *func_params);
static int qlnx_nic_start(struct ecore_dev *cdev);
static int qlnx_slowpath_start(qlnx_host_t *ha);
static int qlnx_slowpath_stop(qlnx_host_t *ha);
static int qlnx_init_hw(qlnx_host_t *ha);
static void qlnx_set_id(struct ecore_dev *cdev, char name[NAME_SIZE],
		char ver_str[VER_SIZE]);
static void qlnx_unload(qlnx_host_t *ha);
static int qlnx_load(qlnx_host_t *ha);
static void qlnx_hw_set_multi(qlnx_host_t *ha, uint8_t *mta, uint32_t mcnt,
		uint32_t add_mac);
static void qlnx_dump_buf8(qlnx_host_t *ha, const char *msg, void *dbuf,
		uint32_t len);
static int qlnx_alloc_rx_buffer(qlnx_host_t *ha, struct qlnx_rx_queue *rxq);
static void qlnx_reuse_rx_data(struct qlnx_rx_queue *rxq);
static void qlnx_update_rx_prod(struct ecore_hwfn *p_hwfn,
		struct qlnx_rx_queue *rxq);
static int qlnx_set_rx_accept_filter(qlnx_host_t *ha, uint8_t filter);
static int qlnx_grc_dumpsize(qlnx_host_t *ha, uint32_t *num_dwords,
		int hwfn_index);
static int qlnx_idle_chk_size(qlnx_host_t *ha, uint32_t *num_dwords,
		int hwfn_index);
static void qlnx_timer(void *arg);
static int qlnx_alloc_tx_br(qlnx_host_t *ha, struct qlnx_fastpath *fp);
static void qlnx_free_tx_br(qlnx_host_t *ha, struct qlnx_fastpath *fp);
static void qlnx_trigger_dump(qlnx_host_t *ha);
static uint16_t qlnx_num_tx_compl(qlnx_host_t *ha, struct qlnx_fastpath *fp,
			struct qlnx_tx_queue *txq);
static void qlnx_tx_int(qlnx_host_t *ha, struct qlnx_fastpath *fp,
		struct qlnx_tx_queue *txq);
static int qlnx_rx_int(qlnx_host_t *ha, struct qlnx_fastpath *fp, int budget,
		int lro_enable);
static void qlnx_fp_taskqueue(void *context, int pending);
static void qlnx_sample_storm_stats(qlnx_host_t *ha);
static int qlnx_alloc_tpa_mbuf(qlnx_host_t *ha, uint16_t rx_buf_size,
		struct qlnx_agg_info *tpa);
static void qlnx_free_tpa_mbuf(qlnx_host_t *ha, struct qlnx_agg_info *tpa);

#if __FreeBSD_version >= 1100000
static uint64_t qlnx_get_counter(if_t ifp, ift_counter cnt);
#endif


/*
 * Hooks to the Operating Systems
 */
static int qlnx_pci_probe (device_t);
static int qlnx_pci_attach (device_t);
static int qlnx_pci_detach (device_t);

#ifndef QLNX_VF

#ifdef CONFIG_ECORE_SRIOV

static int qlnx_iov_init(device_t dev, uint16_t num_vfs, const nvlist_t *params);
static void qlnx_iov_uninit(device_t dev);
static int qlnx_iov_add_vf(device_t dev, uint16_t vfnum, const nvlist_t *params);
static void qlnx_initialize_sriov(qlnx_host_t *ha);
static void qlnx_pf_taskqueue(void *context, int pending);
static int qlnx_create_pf_taskqueues(qlnx_host_t *ha);
static void qlnx_destroy_pf_taskqueues(qlnx_host_t *ha);
static void qlnx_inform_vf_link_state(struct ecore_hwfn *p_hwfn, qlnx_host_t *ha);

#endif /* #ifdef CONFIG_ECORE_SRIOV */

static device_method_t qlnx_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, qlnx_pci_probe),
	DEVMETHOD(device_attach, qlnx_pci_attach),
	DEVMETHOD(device_detach, qlnx_pci_detach),

#ifdef CONFIG_ECORE_SRIOV
	DEVMETHOD(pci_iov_init, qlnx_iov_init),
	DEVMETHOD(pci_iov_uninit, qlnx_iov_uninit),
	DEVMETHOD(pci_iov_add_vf, qlnx_iov_add_vf),
#endif /* #ifdef CONFIG_ECORE_SRIOV */
	{ 0, 0 }
};

static driver_t qlnx_pci_driver = {
	"ql", qlnx_pci_methods, sizeof (qlnx_host_t),
};

static devclass_t qlnx_devclass;

MODULE_VERSION(if_qlnxe,1);
DRIVER_MODULE(if_qlnxe, pci, qlnx_pci_driver, qlnx_devclass, 0, 0);

MODULE_DEPEND(if_qlnxe, pci, 1, 1, 1);
MODULE_DEPEND(if_qlnxe, ether, 1, 1, 1);

#else

static device_method_t qlnxv_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, qlnx_pci_probe),
	DEVMETHOD(device_attach, qlnx_pci_attach),
	DEVMETHOD(device_detach, qlnx_pci_detach),
	{ 0, 0 }
};

static driver_t qlnxv_pci_driver = {
	"ql", qlnxv_pci_methods, sizeof (qlnx_host_t),
};

static devclass_t qlnxv_devclass;
MODULE_VERSION(if_qlnxev,1);
DRIVER_MODULE(if_qlnxev, pci, qlnxv_pci_driver, qlnxv_devclass, 0, 0);

MODULE_DEPEND(if_qlnxev, pci, 1, 1, 1);
MODULE_DEPEND(if_qlnxev, ether, 1, 1, 1);

#endif /* #ifdef QLNX_VF */

MALLOC_DEFINE(M_QLNXBUF, "qlnxbuf", "Buffers for qlnx driver");


static char qlnx_dev_str[128];
static char qlnx_ver_str[VER_SIZE];
static char qlnx_name_str[NAME_SIZE];

/*
 * Some PCI Configuration Space Related Defines
 */

#ifndef PCI_VENDOR_QLOGIC
#define PCI_VENDOR_QLOGIC		0x1077
#endif

/* 40G Adapter QLE45xxx*/
#ifndef QLOGIC_PCI_DEVICE_ID_1634
#define QLOGIC_PCI_DEVICE_ID_1634	0x1634
#endif

/* 100G Adapter QLE45xxx*/
#ifndef QLOGIC_PCI_DEVICE_ID_1644
#define QLOGIC_PCI_DEVICE_ID_1644	0x1644
#endif

/* 25G Adapter QLE45xxx*/
#ifndef QLOGIC_PCI_DEVICE_ID_1656
#define QLOGIC_PCI_DEVICE_ID_1656	0x1656
#endif

/* 50G Adapter QLE45xxx*/
#ifndef QLOGIC_PCI_DEVICE_ID_1654
#define QLOGIC_PCI_DEVICE_ID_1654	0x1654
#endif

/* 10G/25G/40G Adapter QLE41xxx*/
#ifndef QLOGIC_PCI_DEVICE_ID_8070
#define QLOGIC_PCI_DEVICE_ID_8070	0x8070
#endif

/* SRIOV Device (All Speeds) Adapter QLE41xxx*/
#ifndef QLOGIC_PCI_DEVICE_ID_8090
#define QLOGIC_PCI_DEVICE_ID_8090	0x8090
#endif



SYSCTL_NODE(_hw, OID_AUTO, qlnxe, CTLFLAG_RD, 0, "qlnxe driver parameters");

/* Number of Queues: 0 (Auto) or 1 to 32 (fixed queue number) */
static int qlnxe_queue_count = QLNX_DEFAULT_RSS;

#if __FreeBSD_version < 1100000

TUNABLE_INT("hw.qlnxe.queue_count", &qlnxe_queue_count);

#endif

SYSCTL_INT(_hw_qlnxe, OID_AUTO, queue_count, CTLFLAG_RDTUN,
		&qlnxe_queue_count, 0, "Multi-Queue queue count");


/*
 * Note on RDMA personality setting
 * 
 * Read the personality configured in NVRAM
 * If the personality is ETH_ONLY, ETH_IWARP or ETH_ROCE and 
 * the configured personality in sysctl is QLNX_PERSONALITY_DEFAULT 
 * use the personality in NVRAM.

 * Otherwise use t the personality configured in sysctl.
 *
 */
#define QLNX_PERSONALITY_DEFAULT	0x0  /* use personality in NVRAM */
#define QLNX_PERSONALITY_ETH_ONLY	0x1  /* Override with ETH_ONLY */
#define QLNX_PERSONALITY_ETH_IWARP	0x2  /* Override with ETH_IWARP */
#define QLNX_PERSONALITY_ETH_ROCE	0x3  /* Override with ETH_ROCE */
#define QLNX_PERSONALITY_BITS_PER_FUNC	4
#define QLNX_PERSONALIY_MASK		0xF

/* RDMA configuration; 64bit field allows setting for 16 physical functions*/
static uint64_t qlnxe_rdma_configuration = 0x22222222; 

#if __FreeBSD_version < 1100000

TUNABLE_QUAD("hw.qlnxe.rdma_configuration", &qlnxe_rdma_configuration);

SYSCTL_UQUAD(_hw_qlnxe, OID_AUTO, rdma_configuration, CTLFLAG_RDTUN,
               &qlnxe_rdma_configuration, 0, "RDMA Configuration");

#else

SYSCTL_U64(_hw_qlnxe, OID_AUTO, rdma_configuration, CTLFLAG_RDTUN,
                &qlnxe_rdma_configuration, 0, "RDMA Configuration");

#endif /* #if __FreeBSD_version < 1100000 */

int
qlnx_vf_device(qlnx_host_t *ha)
{
        uint16_t	device_id;

        device_id = ha->device_id;

        if (device_id == QLOGIC_PCI_DEVICE_ID_8090)
                return 0;

        return -1;
}

static int
qlnx_valid_device(qlnx_host_t *ha)
{
        uint16_t device_id;

        device_id = ha->device_id;

#ifndef QLNX_VF
        if ((device_id == QLOGIC_PCI_DEVICE_ID_1634) ||
                (device_id == QLOGIC_PCI_DEVICE_ID_1644) ||
                (device_id == QLOGIC_PCI_DEVICE_ID_1656) ||
                (device_id == QLOGIC_PCI_DEVICE_ID_1654) ||
                (device_id == QLOGIC_PCI_DEVICE_ID_8070))
                return 0;
#else
        if (device_id == QLOGIC_PCI_DEVICE_ID_8090)
		return 0;

#endif /* #ifndef QLNX_VF */
        return -1;
}

#ifdef QLNX_ENABLE_IWARP
static int
qlnx_rdma_supported(struct qlnx_host *ha)
{
	uint16_t device_id;

	device_id = pci_get_device(ha->pci_dev);

	if ((device_id == QLOGIC_PCI_DEVICE_ID_1634) ||
		(device_id == QLOGIC_PCI_DEVICE_ID_1656) ||
		(device_id == QLOGIC_PCI_DEVICE_ID_1654) ||
		(device_id == QLOGIC_PCI_DEVICE_ID_8070))
		return (0);

	return (-1);
}
#endif /* #ifdef QLNX_ENABLE_IWARP */

/*
 * Name:	qlnx_pci_probe
 * Function:	Validate the PCI device to be a QLA80XX device
 */
static int
qlnx_pci_probe(device_t dev)
{
	snprintf(qlnx_ver_str, sizeof(qlnx_ver_str), "v%d.%d.%d",
		QLNX_VERSION_MAJOR, QLNX_VERSION_MINOR, QLNX_VERSION_BUILD);
	snprintf(qlnx_name_str, sizeof(qlnx_name_str), "qlnx");

	if (pci_get_vendor(dev) != PCI_VENDOR_QLOGIC) {
                return (ENXIO);
	}

        switch (pci_get_device(dev)) {

#ifndef QLNX_VF

        case QLOGIC_PCI_DEVICE_ID_1644:
		snprintf(qlnx_dev_str, sizeof(qlnx_dev_str), "%s v%d.%d.%d",
			"Qlogic 100GbE PCI CNA Adapter-Ethernet Function",
			QLNX_VERSION_MAJOR, QLNX_VERSION_MINOR,
			QLNX_VERSION_BUILD);
                device_set_desc_copy(dev, qlnx_dev_str);

                break;

        case QLOGIC_PCI_DEVICE_ID_1634:
		snprintf(qlnx_dev_str, sizeof(qlnx_dev_str), "%s v%d.%d.%d",
			"Qlogic 40GbE PCI CNA Adapter-Ethernet Function",
			QLNX_VERSION_MAJOR, QLNX_VERSION_MINOR,
			QLNX_VERSION_BUILD);
                device_set_desc_copy(dev, qlnx_dev_str);

                break;

        case QLOGIC_PCI_DEVICE_ID_1656:
		snprintf(qlnx_dev_str, sizeof(qlnx_dev_str), "%s v%d.%d.%d",
			"Qlogic 25GbE PCI CNA Adapter-Ethernet Function",
			QLNX_VERSION_MAJOR, QLNX_VERSION_MINOR,
			QLNX_VERSION_BUILD);
                device_set_desc_copy(dev, qlnx_dev_str);

                break;

        case QLOGIC_PCI_DEVICE_ID_1654:
		snprintf(qlnx_dev_str, sizeof(qlnx_dev_str), "%s v%d.%d.%d",
			"Qlogic 50GbE PCI CNA Adapter-Ethernet Function",
			QLNX_VERSION_MAJOR, QLNX_VERSION_MINOR,
			QLNX_VERSION_BUILD);
                device_set_desc_copy(dev, qlnx_dev_str);

                break;

	case QLOGIC_PCI_DEVICE_ID_8070:
		snprintf(qlnx_dev_str, sizeof(qlnx_dev_str), "%s v%d.%d.%d",
			"Qlogic 10GbE/25GbE/40GbE PCI CNA (AH)"
			" Adapter-Ethernet Function",
			QLNX_VERSION_MAJOR, QLNX_VERSION_MINOR,
			QLNX_VERSION_BUILD);
		device_set_desc_copy(dev, qlnx_dev_str);

		break;

#else
	case QLOGIC_PCI_DEVICE_ID_8090:
		snprintf(qlnx_dev_str, sizeof(qlnx_dev_str), "%s v%d.%d.%d",
			"Qlogic SRIOV PCI CNA (AH) "
			"Adapter-Ethernet Function",
			QLNX_VERSION_MAJOR, QLNX_VERSION_MINOR,
			QLNX_VERSION_BUILD);
		device_set_desc_copy(dev, qlnx_dev_str);

		break;

#endif /* #ifndef QLNX_VF */

        default:
                return (ENXIO);
        }

#ifdef QLNX_ENABLE_IWARP
	qlnx_rdma_init();
#endif /* #ifdef QLNX_ENABLE_IWARP */

        return (BUS_PROBE_DEFAULT);
}

static uint16_t
qlnx_num_tx_compl(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct qlnx_tx_queue *txq)
{
	u16 hw_bd_cons;
	u16 ecore_cons_idx;
	uint16_t diff;

	hw_bd_cons = le16toh(*txq->hw_cons_ptr);

	ecore_cons_idx = ecore_chain_get_cons_idx(&txq->tx_pbl);
	if (hw_bd_cons < ecore_cons_idx) {
		diff = (1 << 16) - (ecore_cons_idx - hw_bd_cons);
	} else {
		diff = hw_bd_cons - ecore_cons_idx;
	}
	return diff;
}


static void
qlnx_sp_intr(void *arg)
{
	struct ecore_hwfn	*p_hwfn;
	qlnx_host_t		*ha;
	int			i;
	
	p_hwfn = arg;

	if (p_hwfn == NULL) {
		printf("%s: spurious slowpath intr\n", __func__);
		return;
	}

	ha = (qlnx_host_t *)p_hwfn->p_dev;

	QL_DPRINT2(ha, "enter\n");

	for (i = 0; i < ha->cdev.num_hwfns; i++) {
		if (&ha->cdev.hwfns[i] == p_hwfn) {
			taskqueue_enqueue(ha->sp_taskqueue[i], &ha->sp_task[i]);
			break;
		}
	}
	QL_DPRINT2(ha, "exit\n");
	
	return;
}

static void
qlnx_sp_taskqueue(void *context, int pending)
{
	struct ecore_hwfn	*p_hwfn;

	p_hwfn = context;

	if (p_hwfn != NULL) {
		qlnx_sp_isr(p_hwfn);
	}
	return;
}

static int
qlnx_create_sp_taskqueues(qlnx_host_t *ha)
{
	int	i;
	uint8_t	tq_name[32];

	for (i = 0; i < ha->cdev.num_hwfns; i++) {

                struct ecore_hwfn *p_hwfn = &ha->cdev.hwfns[i];

		bzero(tq_name, sizeof (tq_name));
		snprintf(tq_name, sizeof (tq_name), "ql_sp_tq_%d", i);

		TASK_INIT(&ha->sp_task[i], 0, qlnx_sp_taskqueue, p_hwfn);

		ha->sp_taskqueue[i] = taskqueue_create(tq_name, M_NOWAIT,
			 taskqueue_thread_enqueue, &ha->sp_taskqueue[i]);

		if (ha->sp_taskqueue[i] == NULL) 
			return (-1);

		taskqueue_start_threads(&ha->sp_taskqueue[i], 1, PI_NET, "%s",
			tq_name);

		QL_DPRINT1(ha, "%p\n", ha->sp_taskqueue[i]);
	}

	return (0);
}

static void
qlnx_destroy_sp_taskqueues(qlnx_host_t *ha)
{
	int	i;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {
		if (ha->sp_taskqueue[i] != NULL) {
			taskqueue_drain(ha->sp_taskqueue[i], &ha->sp_task[i]);
			taskqueue_free(ha->sp_taskqueue[i]);
		}
	}
	return;
}

static void
qlnx_fp_taskqueue(void *context, int pending)
{
        struct qlnx_fastpath	*fp;
        qlnx_host_t		*ha;
        struct ifnet		*ifp;

        fp = context;

        if (fp == NULL)
                return;

	ha = (qlnx_host_t *)fp->edev;

	ifp = ha->ifp;

        if(ifp->if_drv_flags & IFF_DRV_RUNNING) {

                if (!drbr_empty(ifp, fp->tx_br)) {

                        if(mtx_trylock(&fp->tx_mtx)) {

#ifdef QLNX_TRACE_PERF_DATA
                                tx_pkts = fp->tx_pkts_transmitted;
                                tx_compl = fp->tx_pkts_completed;
#endif

                                qlnx_transmit_locked(ifp, fp, NULL);

#ifdef QLNX_TRACE_PERF_DATA
                                fp->tx_pkts_trans_fp +=
					(fp->tx_pkts_transmitted - tx_pkts);
                                fp->tx_pkts_compl_fp +=
					(fp->tx_pkts_completed - tx_compl);
#endif
                                mtx_unlock(&fp->tx_mtx);
                        }
                }
        }

        QL_DPRINT2(ha, "exit \n");
        return;
}

static int
qlnx_create_fp_taskqueues(qlnx_host_t *ha)
{
	int	i;
	uint8_t	tq_name[32];
	struct qlnx_fastpath *fp;

	for (i = 0; i < ha->num_rss; i++) {

                fp = &ha->fp_array[i];

		bzero(tq_name, sizeof (tq_name));
		snprintf(tq_name, sizeof (tq_name), "ql_fp_tq_%d", i);

		TASK_INIT(&fp->fp_task, 0, qlnx_fp_taskqueue, fp);

		fp->fp_taskqueue = taskqueue_create(tq_name, M_NOWAIT,
					taskqueue_thread_enqueue,
					&fp->fp_taskqueue);

		if (fp->fp_taskqueue == NULL) 
			return (-1);

		taskqueue_start_threads(&fp->fp_taskqueue, 1, PI_NET, "%s",
			tq_name);

		QL_DPRINT1(ha, "%p\n",fp->fp_taskqueue);
	}

	return (0);
}

static void
qlnx_destroy_fp_taskqueues(qlnx_host_t *ha)
{
	int			i;
	struct qlnx_fastpath	*fp;

	for (i = 0; i < ha->num_rss; i++) {

                fp = &ha->fp_array[i];

		if (fp->fp_taskqueue != NULL) {

			taskqueue_drain(fp->fp_taskqueue, &fp->fp_task);
			taskqueue_free(fp->fp_taskqueue);
			fp->fp_taskqueue = NULL;
		}
	}
	return;
}

static void
qlnx_drain_fp_taskqueues(qlnx_host_t *ha)
{
	int			i;
	struct qlnx_fastpath	*fp;

	for (i = 0; i < ha->num_rss; i++) {
                fp = &ha->fp_array[i];

		if (fp->fp_taskqueue != NULL) {
			QLNX_UNLOCK(ha);
			taskqueue_drain(fp->fp_taskqueue, &fp->fp_task);
			QLNX_LOCK(ha);
		}
	}
	return;
}

static void
qlnx_get_params(qlnx_host_t *ha)
{
	if ((qlnxe_queue_count < 0) || (qlnxe_queue_count > QLNX_MAX_RSS)) {
		device_printf(ha->pci_dev, "invalid queue_count value (%d)\n",
			qlnxe_queue_count);
		qlnxe_queue_count = 0;
	}
	return;
}

static void
qlnx_error_recovery_taskqueue(void *context, int pending)
{
        qlnx_host_t *ha;

        ha = context;

        QL_DPRINT2(ha, "enter\n");

        QLNX_LOCK(ha);
        qlnx_stop(ha);
        QLNX_UNLOCK(ha);

#ifdef QLNX_ENABLE_IWARP
	qlnx_rdma_dev_remove(ha);
#endif /* #ifdef QLNX_ENABLE_IWARP */

        qlnx_slowpath_stop(ha);
        qlnx_slowpath_start(ha);

#ifdef QLNX_ENABLE_IWARP
	qlnx_rdma_dev_add(ha);
#endif /* #ifdef QLNX_ENABLE_IWARP */

        qlnx_init(ha);

        callout_reset(&ha->qlnx_callout, hz, qlnx_timer, ha);

        QL_DPRINT2(ha, "exit\n");

        return;
}

static int
qlnx_create_error_recovery_taskqueue(qlnx_host_t *ha)
{
        uint8_t tq_name[32];

        bzero(tq_name, sizeof (tq_name));
        snprintf(tq_name, sizeof (tq_name), "ql_err_tq");

        TASK_INIT(&ha->err_task, 0, qlnx_error_recovery_taskqueue, ha);

        ha->err_taskqueue = taskqueue_create(tq_name, M_NOWAIT,
                                taskqueue_thread_enqueue, &ha->err_taskqueue);


        if (ha->err_taskqueue == NULL)
                return (-1);

        taskqueue_start_threads(&ha->err_taskqueue, 1, PI_NET, "%s", tq_name);

        QL_DPRINT1(ha, "%p\n",ha->err_taskqueue);

        return (0);
}

static void
qlnx_destroy_error_recovery_taskqueue(qlnx_host_t *ha)
{
        if (ha->err_taskqueue != NULL) {
                taskqueue_drain(ha->err_taskqueue, &ha->err_task);
                taskqueue_free(ha->err_taskqueue);
        }

        ha->err_taskqueue = NULL;

        return;
}

/*
 * Name:	qlnx_pci_attach
 * Function:	attaches the device to the operating system
 */
static int
qlnx_pci_attach(device_t dev)
{
	qlnx_host_t	*ha = NULL;
	uint32_t	rsrc_len_reg = 0;
	uint32_t	rsrc_len_dbells = 0;
	uint32_t	rsrc_len_msix = 0;
	int		i;
	uint32_t	mfw_ver;
	uint32_t	num_sp_msix = 0;
	uint32_t	num_rdma_irqs = 0;

        if ((ha = device_get_softc(dev)) == NULL) {
                device_printf(dev, "cannot get softc\n");
                return (ENOMEM);
        }

        memset(ha, 0, sizeof (qlnx_host_t));

        ha->device_id = pci_get_device(dev);

        if (qlnx_valid_device(ha) != 0) {
                device_printf(dev, "device is not valid device\n");
                return (ENXIO);
	}
        ha->pci_func = pci_get_function(dev);

        ha->pci_dev = dev;

	mtx_init(&ha->hw_lock, "qlnx_hw_lock", MTX_NETWORK_LOCK, MTX_DEF);

        ha->flags.lock_init = 1;

        pci_enable_busmaster(dev);

	/*
	 * map the PCI BARs
	 */

        ha->reg_rid = PCIR_BAR(0);
        ha->pci_reg = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &ha->reg_rid,
                                RF_ACTIVE);

        if (ha->pci_reg == NULL) {
                device_printf(dev, "unable to map BAR0\n");
                goto qlnx_pci_attach_err;
        }

        rsrc_len_reg = (uint32_t) bus_get_resource_count(dev, SYS_RES_MEMORY,
                                        ha->reg_rid);

	ha->dbells_rid = PCIR_BAR(2);
	rsrc_len_dbells = (uint32_t) bus_get_resource_count(dev,
					SYS_RES_MEMORY,
					ha->dbells_rid);
	if (rsrc_len_dbells) {

		ha->pci_dbells = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					&ha->dbells_rid, RF_ACTIVE);

		if (ha->pci_dbells == NULL) {
			device_printf(dev, "unable to map BAR1\n");
			goto qlnx_pci_attach_err;
		}
		ha->dbells_phys_addr = (uint64_t)
			bus_get_resource_start(dev, SYS_RES_MEMORY, ha->dbells_rid);

		ha->dbells_size = rsrc_len_dbells;
	} else {
		if (qlnx_vf_device(ha) != 0) {
			device_printf(dev, " BAR1 size is zero\n");
			goto qlnx_pci_attach_err;
		}
	}

        ha->msix_rid = PCIR_BAR(4);
        ha->msix_bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
                        &ha->msix_rid, RF_ACTIVE);

        if (ha->msix_bar == NULL) {
                device_printf(dev, "unable to map BAR2\n");
                goto qlnx_pci_attach_err;
	}

        rsrc_len_msix = (uint32_t) bus_get_resource_count(dev, SYS_RES_MEMORY,
                                        ha->msix_rid);

	ha->dbg_level = 0x0000;

	QL_DPRINT1(ha, "\n\t\t\t"
		"pci_dev = %p pci_reg = %p, reg_len = 0x%08x reg_rid = 0x%08x"
		"\n\t\t\tdbells = %p, dbells_len = 0x%08x dbells_rid = 0x%08x"
		"\n\t\t\tmsix = %p, msix_len = 0x%08x msix_rid = 0x%08x"
		" msix_avail = 0x%x "
		"\n\t\t\t[ncpus = %d]\n",
		ha->pci_dev, ha->pci_reg, rsrc_len_reg,
		ha->reg_rid, ha->pci_dbells, rsrc_len_dbells, ha->dbells_rid,
		ha->msix_bar, rsrc_len_msix, ha->msix_rid, pci_msix_count(dev),
		mp_ncpus);
	/*
	 * allocate dma tags
	 */

	if (qlnx_alloc_parent_dma_tag(ha))
                goto qlnx_pci_attach_err;

	if (qlnx_alloc_tx_dma_tag(ha))
                goto qlnx_pci_attach_err;

	if (qlnx_alloc_rx_dma_tag(ha))
                goto qlnx_pci_attach_err;
		

	if (qlnx_init_hw(ha) != 0)
		goto qlnx_pci_attach_err;
		
        ha->flags.hw_init = 1;

	qlnx_get_params(ha);

	if((pci_get_device(dev) == QLOGIC_PCI_DEVICE_ID_1644) &&
		(qlnxe_queue_count == QLNX_DEFAULT_RSS)) {
		qlnxe_queue_count = QLNX_MAX_RSS;
	}

	/*
	 * Allocate MSI-x vectors
	 */
	if (qlnx_vf_device(ha) != 0) {

		if (qlnxe_queue_count == 0)
			ha->num_rss = QLNX_DEFAULT_RSS;
		else
			ha->num_rss = qlnxe_queue_count;

		num_sp_msix = ha->cdev.num_hwfns;
	} else {
		uint8_t max_rxq;
		uint8_t max_txq;
		
		ecore_vf_get_num_rxqs(&ha->cdev.hwfns[0], &max_rxq);
		ecore_vf_get_num_rxqs(&ha->cdev.hwfns[0], &max_txq);

		if (max_rxq < max_txq)
			ha->num_rss = max_rxq;
		else
			ha->num_rss = max_txq;

		if (ha->num_rss > QLNX_MAX_VF_RSS)
			ha->num_rss = QLNX_MAX_VF_RSS;

		num_sp_msix = 0;
	}

	if (ha->num_rss > mp_ncpus)
		ha->num_rss = mp_ncpus;

	ha->num_tc = QLNX_MAX_TC;

        ha->msix_count = pci_msix_count(dev);

#ifdef QLNX_ENABLE_IWARP

	num_rdma_irqs = qlnx_rdma_get_num_irqs(ha);

#endif /* #ifdef QLNX_ENABLE_IWARP */

        if (!ha->msix_count ||
		(ha->msix_count < (num_sp_msix + 1 + num_rdma_irqs))) {
                device_printf(dev, "%s: msix_count[%d] not enough\n", __func__,
                        ha->msix_count);
                goto qlnx_pci_attach_err;
        }

	if (ha->msix_count > (ha->num_rss + num_sp_msix + num_rdma_irqs))
		ha->msix_count = ha->num_rss + num_sp_msix + num_rdma_irqs;
	else
		ha->num_rss = ha->msix_count - (num_sp_msix + num_rdma_irqs);

	QL_DPRINT1(ha, "\n\t\t\t"
		"pci_reg = %p, reg_len = 0x%08x reg_rid = 0x%08x"
		"\n\t\t\tdbells = %p, dbells_len = 0x%08x dbells_rid = 0x%08x"
		"\n\t\t\tmsix = %p, msix_len = 0x%08x msix_rid = 0x%08x"
		" msix_avail = 0x%x msix_alloc = 0x%x"
		"\n\t\t\t[ncpus = %d][num_rss = 0x%x] [num_tc = 0x%x]\n",
		 ha->pci_reg, rsrc_len_reg,
		ha->reg_rid, ha->pci_dbells, rsrc_len_dbells, ha->dbells_rid,
		ha->msix_bar, rsrc_len_msix, ha->msix_rid, pci_msix_count(dev),
		ha->msix_count, mp_ncpus, ha->num_rss, ha->num_tc);

        if (pci_alloc_msix(dev, &ha->msix_count)) {
                device_printf(dev, "%s: pci_alloc_msix[%d] failed\n", __func__,
                        ha->msix_count);
                ha->msix_count = 0;
                goto qlnx_pci_attach_err;
        }

	/*
	 * Initialize slow path interrupt and task queue
	 */

	if (num_sp_msix) {

		if (qlnx_create_sp_taskqueues(ha) != 0)
			goto qlnx_pci_attach_err;

		for (i = 0; i < ha->cdev.num_hwfns; i++) {

			struct ecore_hwfn *p_hwfn = &ha->cdev.hwfns[i];

			ha->sp_irq_rid[i] = i + 1;
			ha->sp_irq[i] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						&ha->sp_irq_rid[i],
						(RF_ACTIVE | RF_SHAREABLE));
			if (ha->sp_irq[i] == NULL) {
                		device_printf(dev,
					"could not allocate mbx interrupt\n");
				goto qlnx_pci_attach_err;
			}

			if (bus_setup_intr(dev, ha->sp_irq[i],
				(INTR_TYPE_NET | INTR_MPSAFE), NULL,
				qlnx_sp_intr, p_hwfn, &ha->sp_handle[i])) {
				device_printf(dev,
					"could not setup slow path interrupt\n");
				goto qlnx_pci_attach_err;
			}

			QL_DPRINT1(ha, "p_hwfn [%p] sp_irq_rid %d"
				" sp_irq %p sp_handle %p\n", p_hwfn,
				ha->sp_irq_rid[i], ha->sp_irq[i], ha->sp_handle[i]);
		}
	}

	/*
	 * initialize fast path interrupt
	 */
	if (qlnx_create_fp_taskqueues(ha) != 0)
		goto qlnx_pci_attach_err;

        for (i = 0; i < ha->num_rss; i++) {
                ha->irq_vec[i].rss_idx = i;
                ha->irq_vec[i].ha = ha;
                ha->irq_vec[i].irq_rid = (1 + num_sp_msix) + i;

                ha->irq_vec[i].irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
                                &ha->irq_vec[i].irq_rid,
                                (RF_ACTIVE | RF_SHAREABLE));

                if (ha->irq_vec[i].irq == NULL) {
                        device_printf(dev,
				"could not allocate interrupt[%d] irq_rid = %d\n",
				i, ha->irq_vec[i].irq_rid);
                        goto qlnx_pci_attach_err;
                }
		
		if (qlnx_alloc_tx_br(ha, &ha->fp_array[i])) {
                        device_printf(dev, "could not allocate tx_br[%d]\n", i);
                        goto qlnx_pci_attach_err;

		}
	}


	if (qlnx_vf_device(ha) != 0) {

		callout_init(&ha->qlnx_callout, 1);
		ha->flags.callout_init = 1;

		for (i = 0; i < ha->cdev.num_hwfns; i++) {

			if (qlnx_grc_dumpsize(ha, &ha->grcdump_size[i], i) != 0)
				goto qlnx_pci_attach_err;
			if (ha->grcdump_size[i] == 0)
				goto qlnx_pci_attach_err;

			ha->grcdump_size[i] = ha->grcdump_size[i] << 2;
			QL_DPRINT1(ha, "grcdump_size[%d] = 0x%08x\n",
				i, ha->grcdump_size[i]);

			ha->grcdump[i] = qlnx_zalloc(ha->grcdump_size[i]);
			if (ha->grcdump[i] == NULL) {
				device_printf(dev, "grcdump alloc[%d] failed\n", i);
				goto qlnx_pci_attach_err;
			}

			if (qlnx_idle_chk_size(ha, &ha->idle_chk_size[i], i) != 0)
				goto qlnx_pci_attach_err;
			if (ha->idle_chk_size[i] == 0)
				goto qlnx_pci_attach_err;

			ha->idle_chk_size[i] = ha->idle_chk_size[i] << 2;
			QL_DPRINT1(ha, "idle_chk_size[%d] = 0x%08x\n",
				i, ha->idle_chk_size[i]);

			ha->idle_chk[i] = qlnx_zalloc(ha->idle_chk_size[i]);

			if (ha->idle_chk[i] == NULL) {
				device_printf(dev, "idle_chk alloc failed\n");
				goto qlnx_pci_attach_err;
			}
		}

		if (qlnx_create_error_recovery_taskqueue(ha) != 0)
			goto qlnx_pci_attach_err;
	}

	if (qlnx_slowpath_start(ha) != 0)
		goto qlnx_pci_attach_err;
	else
		ha->flags.slowpath_start = 1;

	if (qlnx_vf_device(ha) != 0) {
		if (qlnx_get_flash_size(ha, &ha->flash_size) != 0) {
			qlnx_mdelay(__func__, 1000);
			qlnx_trigger_dump(ha);

			goto qlnx_pci_attach_err0;
		}

		if (qlnx_get_mfw_version(ha, &mfw_ver) != 0) {
			qlnx_mdelay(__func__, 1000);
			qlnx_trigger_dump(ha);

			goto qlnx_pci_attach_err0;
		}
	} else {
		struct ecore_hwfn *p_hwfn = &ha->cdev.hwfns[0];
		ecore_mcp_get_mfw_ver(p_hwfn, NULL, &mfw_ver, NULL);
	}

	snprintf(ha->mfw_ver, sizeof(ha->mfw_ver), "%d.%d.%d.%d",
		((mfw_ver >> 24) & 0xFF), ((mfw_ver >> 16) & 0xFF),
		((mfw_ver >> 8) & 0xFF), (mfw_ver & 0xFF));
	snprintf(ha->stormfw_ver, sizeof(ha->stormfw_ver), "%d.%d.%d.%d",
		FW_MAJOR_VERSION, FW_MINOR_VERSION, FW_REVISION_VERSION,
		FW_ENGINEERING_VERSION);

	QL_DPRINT1(ha, "STORM_FW version %s MFW version %s\n",
		 ha->stormfw_ver, ha->mfw_ver);

	qlnx_init_ifnet(dev, ha);

	/*
	 * add sysctls
	 */ 
	qlnx_add_sysctls(ha);

qlnx_pci_attach_err0:
        /*
	 * create ioctl device interface
	 */
	if (qlnx_vf_device(ha) != 0) {

		if (qlnx_make_cdev(ha)) {
			device_printf(dev, "%s: ql_make_cdev failed\n", __func__);
			goto qlnx_pci_attach_err;
		}

#ifdef QLNX_ENABLE_IWARP
		qlnx_rdma_dev_add(ha);
#endif /* #ifdef QLNX_ENABLE_IWARP */
	}

#ifndef QLNX_VF
#ifdef CONFIG_ECORE_SRIOV

	if (qlnx_vf_device(ha) != 0)
		qlnx_initialize_sriov(ha);

#endif /* #ifdef CONFIG_ECORE_SRIOV */
#endif /* #ifdef QLNX_VF */

	QL_DPRINT2(ha, "success\n");

        return (0);

qlnx_pci_attach_err:

	qlnx_release(ha);

	return (ENXIO);
}

/*
 * Name:	qlnx_pci_detach
 * Function:	Unhooks the device from the operating system
 */
static int
qlnx_pci_detach(device_t dev)
{
	qlnx_host_t	*ha = NULL;

        if ((ha = device_get_softc(dev)) == NULL) {
                device_printf(dev, "%s: cannot get softc\n", __func__);
                return (ENOMEM);
        }

	if (qlnx_vf_device(ha) != 0) {
#ifdef CONFIG_ECORE_SRIOV
		int ret;

		ret = pci_iov_detach(dev);
		if (ret) {
                	device_printf(dev, "%s: SRIOV in use\n", __func__);
			return (ret);
		}

#endif /* #ifdef CONFIG_ECORE_SRIOV */

#ifdef QLNX_ENABLE_IWARP
		if (qlnx_rdma_dev_remove(ha) != 0)
			return (EBUSY);
#endif /* #ifdef QLNX_ENABLE_IWARP */
	}

	QLNX_LOCK(ha);
	qlnx_stop(ha);
	QLNX_UNLOCK(ha);

	qlnx_release(ha);

        return (0);
}

#ifdef QLNX_ENABLE_IWARP

static uint8_t
qlnx_get_personality(uint8_t pci_func)
{
	uint8_t personality;

	personality = (qlnxe_rdma_configuration >>
				(pci_func * QLNX_PERSONALITY_BITS_PER_FUNC)) &
				QLNX_PERSONALIY_MASK;
	return (personality);
}

static void
qlnx_set_personality(qlnx_host_t *ha)
{
	struct ecore_hwfn *p_hwfn;
	uint8_t personality;

	p_hwfn = &ha->cdev.hwfns[0];

	personality = qlnx_get_personality(ha->pci_func);

	switch (personality) {

	case QLNX_PERSONALITY_DEFAULT:
               	device_printf(ha->pci_dev, "%s: DEFAULT\n",
			__func__);
		ha->personality = ECORE_PCI_DEFAULT;
		break;

	case QLNX_PERSONALITY_ETH_ONLY:
               	device_printf(ha->pci_dev, "%s: ETH_ONLY\n",
			__func__);
		ha->personality = ECORE_PCI_ETH;
		break;

	case QLNX_PERSONALITY_ETH_IWARP:
               	device_printf(ha->pci_dev, "%s: ETH_IWARP\n",
			__func__);
		ha->personality = ECORE_PCI_ETH_IWARP;
		break;

	case QLNX_PERSONALITY_ETH_ROCE:
               	device_printf(ha->pci_dev, "%s: ETH_ROCE\n",
			__func__);
		ha->personality = ECORE_PCI_ETH_ROCE;
		break;
	}
 
	return;
}

#endif /* #ifdef QLNX_ENABLE_IWARP */

static int
qlnx_init_hw(qlnx_host_t *ha)
{
	int				rval = 0;
	struct ecore_hw_prepare_params	params;

	ecore_init_struct(&ha->cdev);

	/* ha->dp_module = ECORE_MSG_PROBE |
				ECORE_MSG_INTR |
				ECORE_MSG_SP |
				ECORE_MSG_LINK |
				ECORE_MSG_SPQ |
				ECORE_MSG_RDMA;
	ha->dp_level = ECORE_LEVEL_VERBOSE;*/
	//ha->dp_module = ECORE_MSG_RDMA | ECORE_MSG_INTR | ECORE_MSG_LL2;
	ha->dp_level = ECORE_LEVEL_NOTICE;
	//ha->dp_level = ECORE_LEVEL_VERBOSE;

	ecore_init_dp(&ha->cdev, ha->dp_module, ha->dp_level, ha->pci_dev);

	ha->cdev.regview = ha->pci_reg;

	ha->personality = ECORE_PCI_DEFAULT;

	if (qlnx_vf_device(ha) == 0) {
		ha->cdev.b_is_vf = true;

		if (ha->pci_dbells != NULL) {
			ha->cdev.doorbells = ha->pci_dbells;
			ha->cdev.db_phys_addr = ha->dbells_phys_addr;
			ha->cdev.db_size = ha->dbells_size;
		} else {
			ha->pci_dbells = ha->pci_reg;
		}
	} else {
		ha->cdev.doorbells = ha->pci_dbells;
		ha->cdev.db_phys_addr = ha->dbells_phys_addr;
		ha->cdev.db_size = ha->dbells_size;

#ifdef QLNX_ENABLE_IWARP

		if (qlnx_rdma_supported(ha) == 0)
			qlnx_set_personality(ha);
		
#endif /* #ifdef QLNX_ENABLE_IWARP */

	}
	QL_DPRINT2(ha, "%s: %s\n", __func__,
		(ha->personality == ECORE_PCI_ETH_IWARP ? "iwarp": "ethernet"));

	bzero(&params, sizeof (struct ecore_hw_prepare_params));

	params.personality = ha->personality;

	params.drv_resc_alloc = false;
	params.chk_reg_fifo = false;
	params.initiate_pf_flr = true;
	params.epoch = 0;

	ecore_hw_prepare(&ha->cdev, &params);

	qlnx_set_id(&ha->cdev, qlnx_name_str, qlnx_ver_str);

	QL_DPRINT1(ha, "ha = %p cdev = %p p_hwfn = %p\n",
		ha, &ha->cdev, &ha->cdev.hwfns[0]);

	return (rval);
}

static void
qlnx_release(qlnx_host_t *ha)
{
        device_t	dev;
        int		i;

        dev = ha->pci_dev;

	QL_DPRINT2(ha, "enter\n");

	for (i = 0; i < QLNX_MAX_HW_FUNCS; i++) {
		if (ha->idle_chk[i] != NULL) {
			free(ha->idle_chk[i], M_QLNXBUF);
			ha->idle_chk[i] = NULL;
		}

		if (ha->grcdump[i] != NULL) {
			free(ha->grcdump[i], M_QLNXBUF);
			ha->grcdump[i] = NULL;
		}
	}

        if (ha->flags.callout_init)
                callout_drain(&ha->qlnx_callout);

	if (ha->flags.slowpath_start) {
		qlnx_slowpath_stop(ha);
	}

        if (ha->flags.hw_init)
		ecore_hw_remove(&ha->cdev);

        qlnx_del_cdev(ha);

        if (ha->ifp != NULL)
                ether_ifdetach(ha->ifp);

	qlnx_free_tx_dma_tag(ha);

	qlnx_free_rx_dma_tag(ha);

	qlnx_free_parent_dma_tag(ha);

	if (qlnx_vf_device(ha) != 0) {
		qlnx_destroy_error_recovery_taskqueue(ha);
	}

        for (i = 0; i < ha->num_rss; i++) {
		struct qlnx_fastpath *fp = &ha->fp_array[i];

                if (ha->irq_vec[i].handle) {
                        (void)bus_teardown_intr(dev, ha->irq_vec[i].irq,
                                        ha->irq_vec[i].handle);
                }

                if (ha->irq_vec[i].irq) {
                        (void)bus_release_resource(dev, SYS_RES_IRQ,
                                ha->irq_vec[i].irq_rid,
                                ha->irq_vec[i].irq);
                }

		qlnx_free_tx_br(ha, fp);
        }
	qlnx_destroy_fp_taskqueues(ha);

 	for (i = 0; i < ha->cdev.num_hwfns; i++) {
        	if (ha->sp_handle[i])
                	(void)bus_teardown_intr(dev, ha->sp_irq[i],
				ha->sp_handle[i]);

        	if (ha->sp_irq[i])
			(void) bus_release_resource(dev, SYS_RES_IRQ,
				ha->sp_irq_rid[i], ha->sp_irq[i]);
	}

	qlnx_destroy_sp_taskqueues(ha);

        if (ha->msix_count)
                pci_release_msi(dev);

        if (ha->flags.lock_init) {
                mtx_destroy(&ha->hw_lock);
        }

        if (ha->pci_reg)
                (void) bus_release_resource(dev, SYS_RES_MEMORY, ha->reg_rid,
                                ha->pci_reg);

        if (ha->dbells_size && ha->pci_dbells)
                (void) bus_release_resource(dev, SYS_RES_MEMORY, ha->dbells_rid,
                                ha->pci_dbells);

        if (ha->msix_bar)
                (void) bus_release_resource(dev, SYS_RES_MEMORY, ha->msix_rid,
                                ha->msix_bar);

	QL_DPRINT2(ha, "exit\n");
	return;
}

static void
qlnx_trigger_dump(qlnx_host_t *ha)
{
	int	i;

	if (ha->ifp != NULL)
		ha->ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE | IFF_DRV_RUNNING);

	QL_DPRINT2(ha, "enter\n");

	if (qlnx_vf_device(ha) == 0)
		return;

	ha->error_recovery = 1;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {
		qlnx_grc_dump(ha, &ha->grcdump_dwords[i], i);
		qlnx_idle_chk(ha, &ha->idle_chk_dwords[i], i);
	}

	QL_DPRINT2(ha, "exit\n");

	return;
}

static int
qlnx_trigger_dump_sysctl(SYSCTL_HANDLER_ARGS)
{
        int		err, ret = 0;
        qlnx_host_t	*ha;

        err = sysctl_handle_int(oidp, &ret, 0, req);

        if (err || !req->newptr)
                return (err);

        if (ret == 1) {
                ha = (qlnx_host_t *)arg1;
                qlnx_trigger_dump(ha);
        }
        return (err);
}

static int
qlnx_set_tx_coalesce(SYSCTL_HANDLER_ARGS)
{
        int			err, i, ret = 0, usecs = 0;
        qlnx_host_t		*ha;
	struct ecore_hwfn	*p_hwfn;
	struct qlnx_fastpath	*fp;

        err = sysctl_handle_int(oidp, &usecs, 0, req);

        if (err || !req->newptr || !usecs || (usecs > 255))
                return (err);

        ha = (qlnx_host_t *)arg1;

	if (qlnx_vf_device(ha) == 0)
		return (-1);

	for (i = 0; i < ha->num_rss; i++) {

		p_hwfn = &ha->cdev.hwfns[(i % ha->cdev.num_hwfns)];

        	fp = &ha->fp_array[i];

		if (fp->txq[0]->handle != NULL) {
			ret = ecore_set_queue_coalesce(p_hwfn, 0,
					(uint16_t)usecs, fp->txq[0]->handle);
		}
        }

	if (!ret)
		ha->tx_coalesce_usecs = (uint8_t)usecs;

        return (err);
}

static int
qlnx_set_rx_coalesce(SYSCTL_HANDLER_ARGS)
{
        int			err, i, ret = 0, usecs = 0;
        qlnx_host_t		*ha;
	struct ecore_hwfn	*p_hwfn;
	struct qlnx_fastpath	*fp;

        err = sysctl_handle_int(oidp, &usecs, 0, req);

        if (err || !req->newptr || !usecs || (usecs > 255))
                return (err);

        ha = (qlnx_host_t *)arg1;

	if (qlnx_vf_device(ha) == 0)
		return (-1);

	for (i = 0; i < ha->num_rss; i++) {

		p_hwfn = &ha->cdev.hwfns[(i % ha->cdev.num_hwfns)];

        	fp = &ha->fp_array[i];

		if (fp->rxq->handle != NULL) {
			ret = ecore_set_queue_coalesce(p_hwfn, (uint16_t)usecs,
					 0, fp->rxq->handle);
		}
	}

	if (!ret)
		ha->rx_coalesce_usecs = (uint8_t)usecs;

        return (err);
}

static void
qlnx_add_sp_stats_sysctls(qlnx_host_t *ha)
{
        struct sysctl_ctx_list	*ctx;
        struct sysctl_oid_list	*children;
	struct sysctl_oid	*ctx_oid;

        ctx = device_get_sysctl_ctx(ha->pci_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

	ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "spstat",
			CTLFLAG_RD, NULL, "spstat");
        children = SYSCTL_CHILDREN(ctx_oid);

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "sp_interrupts",
                CTLFLAG_RD, &ha->sp_interrupts,
                "No. of slowpath interrupts");

	return;
}

static void
qlnx_add_fp_stats_sysctls(qlnx_host_t *ha)
{
        struct sysctl_ctx_list	*ctx;
        struct sysctl_oid_list	*children;
        struct sysctl_oid_list	*node_children;
	struct sysctl_oid	*ctx_oid;
	int			i, j;
	uint8_t			name_str[16];

        ctx = device_get_sysctl_ctx(ha->pci_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

	ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "fpstat",
			CTLFLAG_RD, NULL, "fpstat");
	children = SYSCTL_CHILDREN(ctx_oid);

	for (i = 0; i < ha->num_rss; i++) {

		bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
		snprintf(name_str, sizeof(name_str), "%d", i);

		ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, name_str,
			CTLFLAG_RD, NULL, name_str);
		node_children = SYSCTL_CHILDREN(ctx_oid);

		/* Tx Related */

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tx_pkts_processed",
			CTLFLAG_RD, &ha->fp_array[i].tx_pkts_processed,
			"No. of packets processed for transmission");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tx_pkts_freed",
			CTLFLAG_RD, &ha->fp_array[i].tx_pkts_freed,
			"No. of freed packets");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tx_pkts_transmitted",
			CTLFLAG_RD, &ha->fp_array[i].tx_pkts_transmitted,
			"No. of transmitted packets");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tx_pkts_completed",
			CTLFLAG_RD, &ha->fp_array[i].tx_pkts_completed,
			"No. of transmit completions");

                SYSCTL_ADD_QUAD(ctx, node_children,
                        OID_AUTO, "tx_non_tso_pkts",
                        CTLFLAG_RD, &ha->fp_array[i].tx_non_tso_pkts,
                        "No. of non LSO transmited packets");

#ifdef QLNX_TRACE_PERF_DATA

                SYSCTL_ADD_QUAD(ctx, node_children,
                        OID_AUTO, "tx_pkts_trans_ctx",
                        CTLFLAG_RD, &ha->fp_array[i].tx_pkts_trans_ctx,
                        "No. of transmitted packets in transmit context");

                SYSCTL_ADD_QUAD(ctx, node_children,
                        OID_AUTO, "tx_pkts_compl_ctx",
                        CTLFLAG_RD, &ha->fp_array[i].tx_pkts_compl_ctx,
                        "No. of transmit completions in transmit context");

                SYSCTL_ADD_QUAD(ctx, node_children,
                        OID_AUTO, "tx_pkts_trans_fp",
                        CTLFLAG_RD, &ha->fp_array[i].tx_pkts_trans_fp,
                        "No. of transmitted packets in taskqueue");

                SYSCTL_ADD_QUAD(ctx, node_children,
                        OID_AUTO, "tx_pkts_compl_fp",
                        CTLFLAG_RD, &ha->fp_array[i].tx_pkts_compl_fp,
                        "No. of transmit completions in taskqueue");

                SYSCTL_ADD_QUAD(ctx, node_children,
                        OID_AUTO, "tx_pkts_compl_intr",
                        CTLFLAG_RD, &ha->fp_array[i].tx_pkts_compl_intr,
                        "No. of transmit completions in interrupt ctx");
#endif

                SYSCTL_ADD_QUAD(ctx, node_children,
                        OID_AUTO, "tx_tso_pkts",
                        CTLFLAG_RD, &ha->fp_array[i].tx_tso_pkts,
                        "No. of LSO transmited packets");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tx_lso_wnd_min_len",
			CTLFLAG_RD, &ha->fp_array[i].tx_lso_wnd_min_len,
			"tx_lso_wnd_min_len");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tx_defrag",
			CTLFLAG_RD, &ha->fp_array[i].tx_defrag,
			"tx_defrag");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tx_nsegs_gt_elem_left",
			CTLFLAG_RD, &ha->fp_array[i].tx_nsegs_gt_elem_left,
			"tx_nsegs_gt_elem_left");

		SYSCTL_ADD_UINT(ctx, node_children,
			OID_AUTO, "tx_tso_max_nsegs",
			CTLFLAG_RD, &ha->fp_array[i].tx_tso_max_nsegs,
			ha->fp_array[i].tx_tso_max_nsegs, "tx_tso_max_nsegs");

		SYSCTL_ADD_UINT(ctx, node_children,
			OID_AUTO, "tx_tso_min_nsegs",
			CTLFLAG_RD, &ha->fp_array[i].tx_tso_min_nsegs,
			ha->fp_array[i].tx_tso_min_nsegs, "tx_tso_min_nsegs");

		SYSCTL_ADD_UINT(ctx, node_children,
			OID_AUTO, "tx_tso_max_pkt_len",
			CTLFLAG_RD, &ha->fp_array[i].tx_tso_max_pkt_len,
			ha->fp_array[i].tx_tso_max_pkt_len,
			"tx_tso_max_pkt_len");

		SYSCTL_ADD_UINT(ctx, node_children,
			OID_AUTO, "tx_tso_min_pkt_len",
			CTLFLAG_RD, &ha->fp_array[i].tx_tso_min_pkt_len,
			ha->fp_array[i].tx_tso_min_pkt_len,
			"tx_tso_min_pkt_len");

		for (j = 0; j < QLNX_FP_MAX_SEGS; j++) {

			bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
			snprintf(name_str, sizeof(name_str),
				"tx_pkts_nseg_%02d", (j+1));

			SYSCTL_ADD_QUAD(ctx, node_children,
				OID_AUTO, name_str, CTLFLAG_RD,
				&ha->fp_array[i].tx_pkts[j], name_str);
		}

#ifdef QLNX_TRACE_PERF_DATA
                for (j = 0; j < 18; j++) {

                        bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
                        snprintf(name_str, sizeof(name_str),
                                "tx_pkts_hist_%02d", (j+1));

                        SYSCTL_ADD_QUAD(ctx, node_children,
                                OID_AUTO, name_str, CTLFLAG_RD,
                                &ha->fp_array[i].tx_pkts_hist[j], name_str);
                }
                for (j = 0; j < 5; j++) {

                        bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
                        snprintf(name_str, sizeof(name_str),
                                "tx_comInt_%02d", (j+1));

                        SYSCTL_ADD_QUAD(ctx, node_children,
                                OID_AUTO, name_str, CTLFLAG_RD,
                                &ha->fp_array[i].tx_comInt[j], name_str);
                }
                for (j = 0; j < 18; j++) {

                        bzero(name_str, (sizeof(uint8_t) * sizeof(name_str)));
                        snprintf(name_str, sizeof(name_str),
                                "tx_pkts_q_%02d", (j+1));

                        SYSCTL_ADD_QUAD(ctx, node_children,
                                OID_AUTO, name_str, CTLFLAG_RD,
                                &ha->fp_array[i].tx_pkts_q[j], name_str);
                }
#endif

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_nsegs_gt_elem_left",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_nsegs_gt_elem_left,
			"err_tx_nsegs_gt_elem_left");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_dmamap_create",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_dmamap_create,
			"err_tx_dmamap_create");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_defrag_dmamap_load",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_defrag_dmamap_load,
			"err_tx_defrag_dmamap_load");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_non_tso_max_seg",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_non_tso_max_seg,
			"err_tx_non_tso_max_seg");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_dmamap_load",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_dmamap_load,
			"err_tx_dmamap_load");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_defrag",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_defrag,
			"err_tx_defrag");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_free_pkt_null",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_free_pkt_null,
			"err_tx_free_pkt_null");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_tx_cons_idx_conflict",
			CTLFLAG_RD, &ha->fp_array[i].err_tx_cons_idx_conflict,
			"err_tx_cons_idx_conflict");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "lro_cnt_64",
			CTLFLAG_RD, &ha->fp_array[i].lro_cnt_64,
			"lro_cnt_64");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "lro_cnt_128",
			CTLFLAG_RD, &ha->fp_array[i].lro_cnt_128,
			"lro_cnt_128");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "lro_cnt_256",
			CTLFLAG_RD, &ha->fp_array[i].lro_cnt_256,
			"lro_cnt_256");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "lro_cnt_512",
			CTLFLAG_RD, &ha->fp_array[i].lro_cnt_512,
			"lro_cnt_512");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "lro_cnt_1024",
			CTLFLAG_RD, &ha->fp_array[i].lro_cnt_1024,
			"lro_cnt_1024");

		/* Rx Related */

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "rx_pkts",
			CTLFLAG_RD, &ha->fp_array[i].rx_pkts,
			"No. of received packets");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tpa_start",
			CTLFLAG_RD, &ha->fp_array[i].tpa_start,
			"No. of tpa_start packets");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tpa_cont",
			CTLFLAG_RD, &ha->fp_array[i].tpa_cont,
			"No. of tpa_cont packets");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "tpa_end",
			CTLFLAG_RD, &ha->fp_array[i].tpa_end,
			"No. of tpa_end packets");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_m_getcl",
			CTLFLAG_RD, &ha->fp_array[i].err_m_getcl,
			"err_m_getcl");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_m_getjcl",
			CTLFLAG_RD, &ha->fp_array[i].err_m_getjcl,
			"err_m_getjcl");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_rx_hw_errors",
			CTLFLAG_RD, &ha->fp_array[i].err_rx_hw_errors,
			"err_rx_hw_errors");

		SYSCTL_ADD_QUAD(ctx, node_children,
			OID_AUTO, "err_rx_alloc_errors",
			CTLFLAG_RD, &ha->fp_array[i].err_rx_alloc_errors,
			"err_rx_alloc_errors");
	}

	return;
}

static void
qlnx_add_hw_stats_sysctls(qlnx_host_t *ha)
{
        struct sysctl_ctx_list	*ctx;
        struct sysctl_oid_list	*children;
	struct sysctl_oid	*ctx_oid;

        ctx = device_get_sysctl_ctx(ha->pci_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(ha->pci_dev));

	ctx_oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "hwstat",
			CTLFLAG_RD, NULL, "hwstat");
        children = SYSCTL_CHILDREN(ctx_oid);

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "no_buff_discards",
                CTLFLAG_RD, &ha->hw_stats.common.no_buff_discards,
                "No. of packets discarded due to lack of buffer");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "packet_too_big_discard",
                CTLFLAG_RD, &ha->hw_stats.common.packet_too_big_discard,
                "No. of packets discarded because packet was too big");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "ttl0_discard",
                CTLFLAG_RD, &ha->hw_stats.common.ttl0_discard,
                "ttl0_discard");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_ucast_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.rx_ucast_bytes,
                "rx_ucast_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mcast_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mcast_bytes,
                "rx_mcast_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_bcast_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.rx_bcast_bytes,
                "rx_bcast_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_ucast_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.rx_ucast_pkts,
                "rx_ucast_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mcast_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mcast_pkts,
                "rx_mcast_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_bcast_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.rx_bcast_pkts,
                "rx_bcast_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "mftag_filter_discards",
                CTLFLAG_RD, &ha->hw_stats.common.mftag_filter_discards,
                "mftag_filter_discards");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "mac_filter_discards",
                CTLFLAG_RD, &ha->hw_stats.common.mac_filter_discards,
                "mac_filter_discards");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_ucast_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.tx_ucast_bytes,
                "tx_ucast_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_mcast_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.tx_mcast_bytes,
                "tx_mcast_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_bcast_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.tx_bcast_bytes,
                "tx_bcast_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_ucast_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.tx_ucast_pkts,
                "tx_ucast_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_mcast_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.tx_mcast_pkts,
                "tx_mcast_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_bcast_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.tx_bcast_pkts,
                "tx_bcast_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_err_drop_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.tx_err_drop_pkts,
                "tx_err_drop_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tpa_coalesced_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.tpa_coalesced_pkts,
                "tpa_coalesced_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tpa_coalesced_events",
                CTLFLAG_RD, &ha->hw_stats.common.tpa_coalesced_events,
                "tpa_coalesced_events");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tpa_aborts_num",
                CTLFLAG_RD, &ha->hw_stats.common.tpa_aborts_num,
                "tpa_aborts_num");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tpa_not_coalesced_pkts",
                CTLFLAG_RD, &ha->hw_stats.common.tpa_not_coalesced_pkts,
                "tpa_not_coalesced_pkts");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tpa_coalesced_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.tpa_coalesced_bytes,
                "tpa_coalesced_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_64_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_64_byte_packets,
                "rx_64_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_65_to_127_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_65_to_127_byte_packets,
                "rx_65_to_127_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_128_to_255_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_128_to_255_byte_packets,
                "rx_128_to_255_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_256_to_511_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_256_to_511_byte_packets,
                "rx_256_to_511_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_512_to_1023_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_512_to_1023_byte_packets,
                "rx_512_to_1023_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_1024_to_1518_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_1024_to_1518_byte_packets,
                "rx_1024_to_1518_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_1519_to_1522_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.rx_1519_to_1522_byte_packets,
                "rx_1519_to_1522_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_1523_to_2047_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.rx_1519_to_2047_byte_packets,
                "rx_1523_to_2047_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_2048_to_4095_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.rx_2048_to_4095_byte_packets,
                "rx_2048_to_4095_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_4096_to_9216_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.rx_4096_to_9216_byte_packets,
                "rx_4096_to_9216_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_9217_to_16383_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.rx_9217_to_16383_byte_packets,
                "rx_9217_to_16383_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_crc_errors",
                CTLFLAG_RD, &ha->hw_stats.common.rx_crc_errors,
                "rx_crc_errors");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mac_crtl_frames",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mac_crtl_frames,
                "rx_mac_crtl_frames");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_pause_frames",
                CTLFLAG_RD, &ha->hw_stats.common.rx_pause_frames,
                "rx_pause_frames");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_pfc_frames",
                CTLFLAG_RD, &ha->hw_stats.common.rx_pfc_frames,
                "rx_pfc_frames");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_align_errors",
                CTLFLAG_RD, &ha->hw_stats.common.rx_align_errors,
                "rx_align_errors");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_carrier_errors",
                CTLFLAG_RD, &ha->hw_stats.common.rx_carrier_errors,
                "rx_carrier_errors");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_oversize_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_oversize_packets,
                "rx_oversize_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_jabbers",
                CTLFLAG_RD, &ha->hw_stats.common.rx_jabbers,
                "rx_jabbers");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_undersize_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_undersize_packets,
                "rx_undersize_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_fragments",
                CTLFLAG_RD, &ha->hw_stats.common.rx_fragments,
                "rx_fragments");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_64_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_64_byte_packets,
                "tx_64_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_65_to_127_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_65_to_127_byte_packets,
                "tx_65_to_127_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_128_to_255_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_128_to_255_byte_packets,
                "tx_128_to_255_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_256_to_511_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_256_to_511_byte_packets,
                "tx_256_to_511_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_512_to_1023_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_512_to_1023_byte_packets,
                "tx_512_to_1023_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_1024_to_1518_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_1024_to_1518_byte_packets,
                "tx_1024_to_1518_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_1519_to_2047_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.tx_1519_to_2047_byte_packets,
                "tx_1519_to_2047_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_2048_to_4095_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.tx_2048_to_4095_byte_packets,
                "tx_2048_to_4095_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_4096_to_9216_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.tx_4096_to_9216_byte_packets,
                "tx_4096_to_9216_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_9217_to_16383_byte_packets",
                CTLFLAG_RD, &ha->hw_stats.bb.tx_9217_to_16383_byte_packets,
                "tx_9217_to_16383_byte_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_pause_frames",
                CTLFLAG_RD, &ha->hw_stats.common.tx_pause_frames,
                "tx_pause_frames");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_pfc_frames",
                CTLFLAG_RD, &ha->hw_stats.common.tx_pfc_frames,
                "tx_pfc_frames");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_lpi_entry_count",
                CTLFLAG_RD, &ha->hw_stats.bb.tx_lpi_entry_count,
                "tx_lpi_entry_count");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_total_collisions",
                CTLFLAG_RD, &ha->hw_stats.bb.tx_total_collisions,
                "tx_total_collisions");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "brb_truncates",
                CTLFLAG_RD, &ha->hw_stats.common.brb_truncates,
                "brb_truncates");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "brb_discards",
                CTLFLAG_RD, &ha->hw_stats.common.brb_discards,
                "brb_discards");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mac_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mac_bytes,
                "rx_mac_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mac_uc_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mac_uc_packets,
                "rx_mac_uc_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mac_mc_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mac_mc_packets,
                "rx_mac_mc_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mac_bc_packets",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mac_bc_packets,
                "rx_mac_bc_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "rx_mac_frames_ok",
                CTLFLAG_RD, &ha->hw_stats.common.rx_mac_frames_ok,
                "rx_mac_frames_ok");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_mac_bytes",
                CTLFLAG_RD, &ha->hw_stats.common.tx_mac_bytes,
                "tx_mac_bytes");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_mac_uc_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_mac_uc_packets,
                "tx_mac_uc_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_mac_mc_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_mac_mc_packets,
                "tx_mac_mc_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_mac_bc_packets",
                CTLFLAG_RD, &ha->hw_stats.common.tx_mac_bc_packets,
                "tx_mac_bc_packets");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "tx_mac_ctrl_frames",
                CTLFLAG_RD, &ha->hw_stats.common.tx_mac_ctrl_frames,
                "tx_mac_ctrl_frames");
	return;
}

static void
qlnx_add_sysctls(qlnx_host_t *ha)
{
        device_t		dev = ha->pci_dev;
	struct sysctl_ctx_list	*ctx;
	struct sysctl_oid_list	*children;

	ctx = device_get_sysctl_ctx(dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));

	qlnx_add_fp_stats_sysctls(ha);
	qlnx_add_sp_stats_sysctls(ha);

	if (qlnx_vf_device(ha) != 0)
		qlnx_add_hw_stats_sysctls(ha);

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "Driver_Version",
		CTLFLAG_RD, qlnx_ver_str, 0,
		"Driver Version");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "STORMFW_Version",
		CTLFLAG_RD, ha->stormfw_ver, 0,
		"STORM Firmware Version");

	SYSCTL_ADD_STRING(ctx, children, OID_AUTO, "MFW_Version",
		CTLFLAG_RD, ha->mfw_ver, 0,
		"Management Firmware Version");

        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "personality", CTLFLAG_RD,
                &ha->personality, ha->personality,
		"\tpersonality = 0 => Ethernet Only\n"
		"\tpersonality = 3 => Ethernet and RoCE\n"
		"\tpersonality = 4 => Ethernet and iWARP\n"
		"\tpersonality = 6 => Default in Shared Memory\n");

        ha->dbg_level = 0;
        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "debug", CTLFLAG_RW,
                &ha->dbg_level, ha->dbg_level, "Debug Level");

        ha->dp_level = 0x01;
        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "dp_level", CTLFLAG_RW,
                &ha->dp_level, ha->dp_level, "DP Level");

        ha->dbg_trace_lro_cnt = 0;
        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "dbg_trace_lro_cnt", CTLFLAG_RW,
                &ha->dbg_trace_lro_cnt, ha->dbg_trace_lro_cnt,
		"Trace LRO Counts");

        ha->dbg_trace_tso_pkt_len = 0;
        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "dbg_trace_tso_pkt_len", CTLFLAG_RW,
                &ha->dbg_trace_tso_pkt_len, ha->dbg_trace_tso_pkt_len,
		"Trace TSO packet lengths");

        ha->dp_module = 0;
        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "dp_module", CTLFLAG_RW,
                &ha->dp_module, ha->dp_module, "DP Module");

        ha->err_inject = 0;

        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "err_inject", CTLFLAG_RW,
                &ha->err_inject, ha->err_inject, "Error Inject");

	ha->storm_stats_enable = 0;

	SYSCTL_ADD_UINT(ctx, children,
		OID_AUTO, "storm_stats_enable", CTLFLAG_RW,
		&ha->storm_stats_enable, ha->storm_stats_enable,
		"Enable Storm Statistics Gathering");

	ha->storm_stats_index = 0;

	SYSCTL_ADD_UINT(ctx, children,
		OID_AUTO, "storm_stats_index", CTLFLAG_RD,
		&ha->storm_stats_index, ha->storm_stats_index,
		"Enable Storm Statistics Gathering Current Index");

	ha->grcdump_taken = 0;
	SYSCTL_ADD_UINT(ctx, children,
		OID_AUTO, "grcdump_taken", CTLFLAG_RD,
		&ha->grcdump_taken, ha->grcdump_taken,
		"grcdump_taken");

	ha->idle_chk_taken = 0;
	SYSCTL_ADD_UINT(ctx, children,
		OID_AUTO, "idle_chk_taken", CTLFLAG_RD,
		&ha->idle_chk_taken, ha->idle_chk_taken,
		"idle_chk_taken");

	SYSCTL_ADD_UINT(ctx, children,
		OID_AUTO, "rx_coalesce_usecs", CTLFLAG_RD,
		&ha->rx_coalesce_usecs, ha->rx_coalesce_usecs,
		"rx_coalesce_usecs");

	SYSCTL_ADD_UINT(ctx, children,
		OID_AUTO, "tx_coalesce_usecs", CTLFLAG_RD,
		&ha->tx_coalesce_usecs, ha->tx_coalesce_usecs,
		"tx_coalesce_usecs");

	SYSCTL_ADD_PROC(ctx, children,
		OID_AUTO, "trigger_dump", (CTLTYPE_INT | CTLFLAG_RW),
		(void *)ha, 0,
		qlnx_trigger_dump_sysctl, "I", "trigger_dump");

	SYSCTL_ADD_PROC(ctx, children,
		OID_AUTO, "set_rx_coalesce_usecs",
		(CTLTYPE_INT | CTLFLAG_RW),
		(void *)ha, 0,
		qlnx_set_rx_coalesce, "I",
		"rx interrupt coalesce period microseconds");

	SYSCTL_ADD_PROC(ctx, children,
		OID_AUTO, "set_tx_coalesce_usecs",
		(CTLTYPE_INT | CTLFLAG_RW),
		(void *)ha, 0,
		qlnx_set_tx_coalesce, "I",
		"tx interrupt coalesce period microseconds");

	ha->rx_pkt_threshold = 128;
        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "rx_pkt_threshold", CTLFLAG_RW,
                &ha->rx_pkt_threshold, ha->rx_pkt_threshold,
		"No. of Rx Pkts to process at a time");

	ha->rx_jumbo_buf_eq_mtu = 0;
        SYSCTL_ADD_UINT(ctx, children,
                OID_AUTO, "rx_jumbo_buf_eq_mtu", CTLFLAG_RW,
                &ha->rx_jumbo_buf_eq_mtu, ha->rx_jumbo_buf_eq_mtu,
		"== 0 => Rx Jumbo buffers are capped to 4Kbytes\n"
		"otherwise Rx Jumbo buffers are set to >= MTU size\n");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "err_illegal_intr", CTLFLAG_RD,
		&ha->err_illegal_intr, "err_illegal_intr");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "err_fp_null", CTLFLAG_RD,
		&ha->err_fp_null, "err_fp_null");

	SYSCTL_ADD_QUAD(ctx, children,
                OID_AUTO, "err_get_proto_invalid_type", CTLFLAG_RD,
		&ha->err_get_proto_invalid_type, "err_get_proto_invalid_type");
	return;
}



/*****************************************************************************
 * Operating System Network Interface Functions
 *****************************************************************************/

static void
qlnx_init_ifnet(device_t dev, qlnx_host_t *ha)
{
	uint16_t	device_id;
        struct ifnet	*ifp;

        ifp = ha->ifp = if_alloc(IFT_ETHER);

        if (ifp == NULL)
                panic("%s: cannot if_alloc()\n", device_get_nameunit(dev));

        if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	device_id = pci_get_device(ha->pci_dev);

#if __FreeBSD_version >= 1000000

        if (device_id == QLOGIC_PCI_DEVICE_ID_1634) 
		ifp->if_baudrate = IF_Gbps(40);
        else if ((device_id == QLOGIC_PCI_DEVICE_ID_1656) ||
			(device_id == QLOGIC_PCI_DEVICE_ID_8070))
		ifp->if_baudrate = IF_Gbps(25);
        else if (device_id == QLOGIC_PCI_DEVICE_ID_1654)
		ifp->if_baudrate = IF_Gbps(50);
        else if (device_id == QLOGIC_PCI_DEVICE_ID_1644)
		ifp->if_baudrate = IF_Gbps(100);

        ifp->if_capabilities = IFCAP_LINKSTATE;
#else
        ifp->if_mtu = ETHERMTU;
	ifp->if_baudrate = (1 * 1000 * 1000 *1000);

#endif /* #if __FreeBSD_version >= 1000000 */

        ifp->if_init = qlnx_init;
        ifp->if_softc = ha;
        ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
        ifp->if_ioctl = qlnx_ioctl;
        ifp->if_transmit = qlnx_transmit;
        ifp->if_qflush = qlnx_qflush;

        IFQ_SET_MAXLEN(&ifp->if_snd, qlnx_get_ifq_snd_maxlen(ha));
        ifp->if_snd.ifq_drv_maxlen = qlnx_get_ifq_snd_maxlen(ha);
        IFQ_SET_READY(&ifp->if_snd);

#if __FreeBSD_version >= 1100036
	if_setgetcounterfn(ifp, qlnx_get_counter);
#endif

        ha->max_frame_size = ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;

        memcpy(ha->primary_mac, qlnx_get_mac_addr(ha), ETH_ALEN);

	if (!ha->primary_mac[0] && !ha->primary_mac[1] &&
		!ha->primary_mac[2] && !ha->primary_mac[3] &&
		!ha->primary_mac[4] && !ha->primary_mac[5]) {
		uint32_t rnd;

		rnd = arc4random();

		ha->primary_mac[0] = 0x00;
		ha->primary_mac[1] = 0x0e;
		ha->primary_mac[2] = 0x1e;
		ha->primary_mac[3] = rnd & 0xFF;
		ha->primary_mac[4] = (rnd >> 8) & 0xFF;
		ha->primary_mac[5] = (rnd >> 16) & 0xFF;
	}

	ether_ifattach(ifp, ha->primary_mac);
	bcopy(IF_LLADDR(ha->ifp), ha->primary_mac, ETHER_ADDR_LEN);

	ifp->if_capabilities = IFCAP_HWCSUM;
	ifp->if_capabilities |= IFCAP_JUMBO_MTU;

	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
	ifp->if_capabilities |= IFCAP_VLAN_HWFILTER;
	ifp->if_capabilities |= IFCAP_VLAN_HWCSUM;
	ifp->if_capabilities |= IFCAP_VLAN_HWTSO;
	ifp->if_capabilities |= IFCAP_TSO4;
	ifp->if_capabilities |= IFCAP_TSO6;
	ifp->if_capabilities |= IFCAP_LRO;

	ifp->if_hw_tsomax =  QLNX_MAX_TSO_FRAME_SIZE -
				(ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN);
	ifp->if_hw_tsomaxsegcount = QLNX_MAX_SEGMENTS - 1 /* hdr */;
	ifp->if_hw_tsomaxsegsize = QLNX_MAX_TX_MBUF_SIZE;


        ifp->if_capenable = ifp->if_capabilities;

	ifp->if_hwassist = CSUM_IP;
	ifp->if_hwassist |= CSUM_TCP | CSUM_UDP;
	ifp->if_hwassist |= CSUM_TCP_IPV6 | CSUM_UDP_IPV6;
	ifp->if_hwassist |= CSUM_TSO;

	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

        ifmedia_init(&ha->media, IFM_IMASK, qlnx_media_change,\
		qlnx_media_status);

        if (device_id == QLOGIC_PCI_DEVICE_ID_1634) {
		ifmedia_add(&ha->media, (IFM_ETHER | IFM_40G_LR4), 0, NULL);
		ifmedia_add(&ha->media, (IFM_ETHER | IFM_40G_SR4), 0, NULL);
		ifmedia_add(&ha->media, (IFM_ETHER | IFM_40G_CR4), 0, NULL);
        } else if ((device_id == QLOGIC_PCI_DEVICE_ID_1656) ||
			(device_id == QLOGIC_PCI_DEVICE_ID_8070)) {
		ifmedia_add(&ha->media, (IFM_ETHER | QLNX_IFM_25G_SR), 0, NULL);
		ifmedia_add(&ha->media, (IFM_ETHER | QLNX_IFM_25G_CR), 0, NULL);
        } else if (device_id == QLOGIC_PCI_DEVICE_ID_1654) {
		ifmedia_add(&ha->media, (IFM_ETHER | IFM_50G_KR2), 0, NULL);
		ifmedia_add(&ha->media, (IFM_ETHER | IFM_50G_CR2), 0, NULL);
        } else if (device_id == QLOGIC_PCI_DEVICE_ID_1644) {
		ifmedia_add(&ha->media,
			(IFM_ETHER | QLNX_IFM_100G_LR4), 0, NULL);
		ifmedia_add(&ha->media,
			(IFM_ETHER | QLNX_IFM_100G_SR4), 0, NULL);
		ifmedia_add(&ha->media,
			(IFM_ETHER | QLNX_IFM_100G_CR4), 0, NULL);
	}

        ifmedia_add(&ha->media, (IFM_ETHER | IFM_FDX), 0, NULL);
        ifmedia_add(&ha->media, (IFM_ETHER | IFM_AUTO), 0, NULL);


        ifmedia_set(&ha->media, (IFM_ETHER | IFM_AUTO));

        QL_DPRINT2(ha, "exit\n");

        return;
}

static void
qlnx_init_locked(qlnx_host_t *ha)
{
	struct ifnet	*ifp = ha->ifp;

	QL_DPRINT1(ha, "Driver Initialization start \n");

	qlnx_stop(ha);

	if (qlnx_load(ha) == 0) {

		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

#ifdef QLNX_ENABLE_IWARP
		if (qlnx_vf_device(ha) != 0) {
			qlnx_rdma_dev_open(ha);
		}
#endif /* #ifdef QLNX_ENABLE_IWARP */
	}

	return;
}

static void
qlnx_init(void *arg)
{
	qlnx_host_t	*ha;

	ha = (qlnx_host_t *)arg;

	QL_DPRINT2(ha, "enter\n");

	QLNX_LOCK(ha);
	qlnx_init_locked(ha);
	QLNX_UNLOCK(ha);

	QL_DPRINT2(ha, "exit\n");

	return;
}

static int
qlnx_config_mcast_mac_addr(qlnx_host_t *ha, uint8_t *mac_addr, uint32_t add_mac)
{
	struct ecore_filter_mcast	*mcast;
	struct ecore_dev		*cdev;
	int				rc;

	cdev = &ha->cdev;

	mcast = &ha->ecore_mcast;
	bzero(mcast, sizeof(struct ecore_filter_mcast));

	if (add_mac)
		mcast->opcode = ECORE_FILTER_ADD;
	else
		mcast->opcode = ECORE_FILTER_REMOVE;

	mcast->num_mc_addrs = 1;
	memcpy(mcast->mac, mac_addr, ETH_ALEN);

	rc = ecore_filter_mcast_cmd(cdev, mcast, ECORE_SPQ_MODE_CB, NULL);

	return (rc);
}

static int
qlnx_hw_add_mcast(qlnx_host_t *ha, uint8_t *mta)
{
        int	i;

        for (i = 0; i < QLNX_MAX_NUM_MULTICAST_ADDRS; i++) {

                if (QL_MAC_CMP(ha->mcast[i].addr, mta) == 0)
                        return 0; /* its been already added */
        }

        for (i = 0; i < QLNX_MAX_NUM_MULTICAST_ADDRS; i++) {

                if ((ha->mcast[i].addr[0] == 0) &&
                        (ha->mcast[i].addr[1] == 0) &&
                        (ha->mcast[i].addr[2] == 0) &&
                        (ha->mcast[i].addr[3] == 0) &&
                        (ha->mcast[i].addr[4] == 0) &&
                        (ha->mcast[i].addr[5] == 0)) {

                        if (qlnx_config_mcast_mac_addr(ha, mta, 1))
                                return (-1);

                        bcopy(mta, ha->mcast[i].addr, ETH_ALEN);
                        ha->nmcast++;

                        return 0;
                }
        }
        return 0;
}

static int
qlnx_hw_del_mcast(qlnx_host_t *ha, uint8_t *mta)
{
        int	i;

        for (i = 0; i < QLNX_MAX_NUM_MULTICAST_ADDRS; i++) {
                if (QL_MAC_CMP(ha->mcast[i].addr, mta) == 0) {

                        if (qlnx_config_mcast_mac_addr(ha, mta, 0))
                                return (-1);

                        ha->mcast[i].addr[0] = 0;
                        ha->mcast[i].addr[1] = 0;
                        ha->mcast[i].addr[2] = 0;
                        ha->mcast[i].addr[3] = 0;
                        ha->mcast[i].addr[4] = 0;
                        ha->mcast[i].addr[5] = 0;

                        ha->nmcast--;

                        return 0;
                }
        }
        return 0;
}

/*
 * Name: qls_hw_set_multi
 * Function: Sets the Multicast Addresses provided the host O.S into the
 *      hardware (for the given interface)
 */
static void
qlnx_hw_set_multi(qlnx_host_t *ha, uint8_t *mta, uint32_t mcnt,
	uint32_t add_mac)
{
        int	i;

        for (i = 0; i < mcnt; i++) {
                if (add_mac) {
                        if (qlnx_hw_add_mcast(ha, mta))
                                break;
                } else {
                        if (qlnx_hw_del_mcast(ha, mta))
                                break;
                }

                mta += ETHER_HDR_LEN;
        }
        return;
}


#define QLNX_MCAST_ADDRS_SIZE (QLNX_MAX_NUM_MULTICAST_ADDRS * ETHER_HDR_LEN)
static int
qlnx_set_multi(qlnx_host_t *ha, uint32_t add_multi)
{
	uint8_t			mta[QLNX_MCAST_ADDRS_SIZE];
	struct ifmultiaddr	*ifma;
	int			mcnt = 0;
	struct ifnet		*ifp = ha->ifp;
	int			ret = 0;

	if (qlnx_vf_device(ha) == 0)
		return (0);

	if_maddr_rlock(ifp);

	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (mcnt == QLNX_MAX_NUM_MULTICAST_ADDRS)
			break;

		bcopy(LLADDR((struct sockaddr_dl *) ifma->ifma_addr),
			&mta[mcnt * ETHER_HDR_LEN], ETHER_HDR_LEN);

		mcnt++;
	}

	if_maddr_runlock(ifp);

	QLNX_LOCK(ha);
	qlnx_hw_set_multi(ha, mta, mcnt, add_multi);
	QLNX_UNLOCK(ha);

	return (ret);
}

static int
qlnx_set_promisc(qlnx_host_t *ha)
{
	int	rc = 0;
	uint8_t	filter;

	if (qlnx_vf_device(ha) == 0)
		return (0);

	filter = ha->filter;
	filter |= ECORE_ACCEPT_MCAST_UNMATCHED;
	filter |= ECORE_ACCEPT_UCAST_UNMATCHED;

	rc = qlnx_set_rx_accept_filter(ha, filter);
	return (rc);
}

static int
qlnx_set_allmulti(qlnx_host_t *ha)
{
	int	rc = 0;
	uint8_t	filter;

	if (qlnx_vf_device(ha) == 0)
		return (0);

	filter = ha->filter;
	filter |= ECORE_ACCEPT_MCAST_UNMATCHED;
	rc = qlnx_set_rx_accept_filter(ha, filter);

	return (rc);
}


static int
qlnx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	int		ret = 0, mask;
	struct ifreq	*ifr = (struct ifreq *)data;
	struct ifaddr	*ifa = (struct ifaddr *)data;
	qlnx_host_t	*ha;

	ha = (qlnx_host_t *)ifp->if_softc;

	switch (cmd) {
	case SIOCSIFADDR:
		QL_DPRINT4(ha, "SIOCSIFADDR (0x%lx)\n", cmd);

		if (ifa->ifa_addr->sa_family == AF_INET) {
			ifp->if_flags |= IFF_UP;
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				QLNX_LOCK(ha);
				qlnx_init_locked(ha);
				QLNX_UNLOCK(ha);
			}
			QL_DPRINT4(ha, "SIOCSIFADDR (0x%lx) ipv4 [0x%08x]\n",
				   cmd, ntohl(IA_SIN(ifa)->sin_addr.s_addr));

			arp_ifinit(ifp, ifa);
		} else {
			ether_ioctl(ifp, cmd, data);
		}
		break;

	case SIOCSIFMTU:
		QL_DPRINT4(ha, "SIOCSIFMTU (0x%lx)\n", cmd);

		if (ifr->ifr_mtu > QLNX_MAX_MTU) {
			ret = EINVAL;
		} else {
			QLNX_LOCK(ha);
			ifp->if_mtu = ifr->ifr_mtu;
			ha->max_frame_size =
				ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				qlnx_init_locked(ha);
			}

			QLNX_UNLOCK(ha);
		}

		break;

	case SIOCSIFFLAGS:
		QL_DPRINT4(ha, "SIOCSIFFLAGS (0x%lx)\n", cmd);

		QLNX_LOCK(ha);

		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ ha->if_flags) &
					IFF_PROMISC) {
					ret = qlnx_set_promisc(ha);
				} else if ((ifp->if_flags ^ ha->if_flags) &
					IFF_ALLMULTI) {
					ret = qlnx_set_allmulti(ha);
				}
			} else {
				ha->max_frame_size = ifp->if_mtu +
					ETHER_HDR_LEN + ETHER_CRC_LEN;
				qlnx_init_locked(ha);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				qlnx_stop(ha);
			ha->if_flags = ifp->if_flags;
		}

		QLNX_UNLOCK(ha);
		break;

	case SIOCADDMULTI:
		QL_DPRINT4(ha, "%s (0x%lx)\n", "SIOCADDMULTI", cmd);

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			if (qlnx_set_multi(ha, 1))
				ret = EINVAL;
		}
		break;

	case SIOCDELMULTI:
		QL_DPRINT4(ha, "%s (0x%lx)\n", "SIOCDELMULTI", cmd);

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			if (qlnx_set_multi(ha, 0))
				ret = EINVAL;
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		QL_DPRINT4(ha, "SIOCSIFMEDIA/SIOCGIFMEDIA (0x%lx)\n", cmd);

		ret = ifmedia_ioctl(ifp, ifr, &ha->media, cmd);
		break;

	case SIOCSIFCAP:
		
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;

		QL_DPRINT4(ha, "SIOCSIFCAP (0x%lx)\n", cmd);

		if (mask & IFCAP_HWCSUM)
			ifp->if_capenable ^= IFCAP_HWCSUM;
		if (mask & IFCAP_TSO4)
			ifp->if_capenable ^= IFCAP_TSO4;
		if (mask & IFCAP_TSO6)
			ifp->if_capenable ^= IFCAP_TSO6;
		if (mask & IFCAP_VLAN_HWTAGGING)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
		if (mask & IFCAP_VLAN_HWTSO)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if (mask & IFCAP_LRO)
			ifp->if_capenable ^= IFCAP_LRO;

		QLNX_LOCK(ha);

		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			qlnx_init_locked(ha);

		QLNX_UNLOCK(ha);

		VLAN_CAPABILITIES(ifp);
		break;

#if (__FreeBSD_version >= 1100101)

	case SIOCGI2C:
	{
		struct ifi2creq i2c;
		struct ecore_hwfn *p_hwfn = &ha->cdev.hwfns[0];
		struct ecore_ptt *p_ptt;

		ret = copyin(ifr_data_get_ptr(ifr), &i2c, sizeof(i2c));

		if (ret)
			break;

		if ((i2c.len > sizeof (i2c.data)) ||
			(i2c.dev_addr != 0xA0 && i2c.dev_addr != 0xA2)) {
			ret = EINVAL;
			break;
		}

		p_ptt = ecore_ptt_acquire(p_hwfn);

		if (!p_ptt) {
			QL_DPRINT1(ha, "ecore_ptt_acquire failed\n");
			ret = -1;
			break;
		}

		ret = ecore_mcp_phy_sfp_read(p_hwfn, p_ptt,
			(ha->pci_func & 0x1), i2c.dev_addr, i2c.offset,
			i2c.len, &i2c.data[0]);

		ecore_ptt_release(p_hwfn, p_ptt);

		if (ret) {
			ret = -1;
			break;
		}

		ret = copyout(&i2c, ifr_data_get_ptr(ifr), sizeof(i2c));

		QL_DPRINT8(ha, "SIOCGI2C copyout ret = %d \
			 len = %d addr = 0x%02x offset = 0x%04x \
			 data[0..7]=0x%02x 0x%02x 0x%02x 0x%02x 0x%02x \
			 0x%02x 0x%02x 0x%02x\n",
			ret, i2c.len, i2c.dev_addr, i2c.offset,
			i2c.data[0], i2c.data[1], i2c.data[2], i2c.data[3],
			i2c.data[4], i2c.data[5], i2c.data[6], i2c.data[7]);
		break;
	}
#endif /* #if (__FreeBSD_version >= 1100101) */

	default:
		QL_DPRINT4(ha, "default (0x%lx)\n", cmd);
		ret = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (ret);
}

static int
qlnx_media_change(struct ifnet *ifp)
{
	qlnx_host_t	*ha;
	struct ifmedia	*ifm;
	int		ret = 0;

	ha = (qlnx_host_t *)ifp->if_softc;

	QL_DPRINT2(ha, "enter\n");

	ifm = &ha->media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		ret = EINVAL;

	QL_DPRINT2(ha, "exit\n");

	return (ret);
}

static void
qlnx_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	qlnx_host_t		*ha;

	ha = (qlnx_host_t *)ifp->if_softc;

	QL_DPRINT2(ha, "enter\n");

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (ha->link_up) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active |=
			(IFM_FDX | qlnx_get_optics(ha, &ha->if_link));

		if (ha->if_link.link_partner_caps &
			(QLNX_LINK_CAP_Pause | QLNX_LINK_CAP_Asym_Pause))
			ifmr->ifm_active |=
				(IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE);
	}

	QL_DPRINT2(ha, "exit (%s)\n", (ha->link_up ? "link_up" : "link_down"));

	return;
}


static void
qlnx_free_tx_pkt(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct qlnx_tx_queue *txq)
{
	u16			idx;
	struct mbuf		*mp;
	bus_dmamap_t		map;
	int			i;
	struct eth_tx_bd	*tx_data_bd;
	struct eth_tx_1st_bd	*first_bd;
	int			nbds = 0;

	idx = txq->sw_tx_cons;
	mp = txq->sw_tx_ring[idx].mp;
	map = txq->sw_tx_ring[idx].map;

	if ((mp == NULL) || QL_ERR_INJECT(ha, QL_ERR_INJCT_TX_INT_MBUF_NULL)){

		QL_RESET_ERR_INJECT(ha, QL_ERR_INJCT_TX_INT_MBUF_NULL);

		QL_DPRINT1(ha, "(mp == NULL) "
			" tx_idx = 0x%x"
			" ecore_prod_idx = 0x%x"
			" ecore_cons_idx = 0x%x"
			" hw_bd_cons = 0x%x"
			" txq_db_last = 0x%x"
			" elem_left = 0x%x\n",
			fp->rss_id,
			ecore_chain_get_prod_idx(&txq->tx_pbl),
			ecore_chain_get_cons_idx(&txq->tx_pbl),
			le16toh(*txq->hw_cons_ptr),
			txq->tx_db.raw,
			ecore_chain_get_elem_left(&txq->tx_pbl));

		fp->err_tx_free_pkt_null++;

		//DEBUG
		qlnx_trigger_dump(ha);

		return;
	} else {

		QLNX_INC_OPACKETS((ha->ifp));
		QLNX_INC_OBYTES((ha->ifp), (mp->m_pkthdr.len));

		bus_dmamap_sync(ha->tx_tag, map, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ha->tx_tag, map);

		fp->tx_pkts_freed++;
		fp->tx_pkts_completed++;

		m_freem(mp);
	}

	first_bd = (struct eth_tx_1st_bd *)ecore_chain_consume(&txq->tx_pbl);
	nbds = first_bd->data.nbds;

//	BD_SET_UNMAP_ADDR_LEN(first_bd, 0, 0);

	for (i = 1; i < nbds; i++) {
		tx_data_bd = ecore_chain_consume(&txq->tx_pbl);
//		BD_SET_UNMAP_ADDR_LEN(tx_data_bd, 0, 0);
	}
	txq->sw_tx_ring[idx].flags = 0;
	txq->sw_tx_ring[idx].mp = NULL;
	txq->sw_tx_ring[idx].map = (bus_dmamap_t)0;

	return;
}

static void
qlnx_tx_int(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct qlnx_tx_queue *txq)
{
	u16 hw_bd_cons;
	u16 ecore_cons_idx;
	uint16_t diff;
	uint16_t idx, idx2;

	hw_bd_cons = le16toh(*txq->hw_cons_ptr);

	while (hw_bd_cons !=
		(ecore_cons_idx = ecore_chain_get_cons_idx(&txq->tx_pbl))) {

		if (hw_bd_cons < ecore_cons_idx) {
			diff = (1 << 16) - (ecore_cons_idx - hw_bd_cons);
		} else {
			diff = hw_bd_cons - ecore_cons_idx;
		}
		if ((diff > TX_RING_SIZE) ||
			QL_ERR_INJECT(ha, QL_ERR_INJCT_TX_INT_DIFF)){

			QL_RESET_ERR_INJECT(ha, QL_ERR_INJCT_TX_INT_DIFF);

			QL_DPRINT1(ha, "(diff = 0x%x) "
				" tx_idx = 0x%x"
				" ecore_prod_idx = 0x%x"
				" ecore_cons_idx = 0x%x"
				" hw_bd_cons = 0x%x"
				" txq_db_last = 0x%x"
				" elem_left = 0x%x\n",
				diff,
				fp->rss_id,
				ecore_chain_get_prod_idx(&txq->tx_pbl),
				ecore_chain_get_cons_idx(&txq->tx_pbl),
				le16toh(*txq->hw_cons_ptr),
				txq->tx_db.raw,
				ecore_chain_get_elem_left(&txq->tx_pbl));

			fp->err_tx_cons_idx_conflict++;

			//DEBUG
			qlnx_trigger_dump(ha);
		}

		idx = (txq->sw_tx_cons + 1) & (TX_RING_SIZE - 1);
		idx2 = (txq->sw_tx_cons + 2) & (TX_RING_SIZE - 1);
		prefetch(txq->sw_tx_ring[idx].mp);
		prefetch(txq->sw_tx_ring[idx2].mp);

		qlnx_free_tx_pkt(ha, fp, txq);

		txq->sw_tx_cons = (txq->sw_tx_cons + 1) & (TX_RING_SIZE - 1);
	}
	return;
}

static int
qlnx_transmit_locked(struct ifnet *ifp,struct qlnx_fastpath  *fp, struct mbuf  *mp)
{
        int                     ret = 0;
        struct qlnx_tx_queue    *txq;
        qlnx_host_t *           ha;
        uint16_t elem_left;

        txq = fp->txq[0];
        ha = (qlnx_host_t *)fp->edev;


        if ((!(ifp->if_drv_flags & IFF_DRV_RUNNING)) || (!ha->link_up)) {
                if(mp != NULL)
                        ret = drbr_enqueue(ifp, fp->tx_br, mp);
                return (ret);
        }

        if(mp != NULL)
                ret  = drbr_enqueue(ifp, fp->tx_br, mp);

        mp = drbr_peek(ifp, fp->tx_br);

        while (mp != NULL) {

                if (qlnx_send(ha, fp, &mp)) {

                        if (mp != NULL) {
                                drbr_putback(ifp, fp->tx_br, mp);
                        } else {
                                fp->tx_pkts_processed++;
                                drbr_advance(ifp, fp->tx_br);
                        }
                        goto qlnx_transmit_locked_exit;

                } else {
                        drbr_advance(ifp, fp->tx_br);
                        fp->tx_pkts_transmitted++;
                        fp->tx_pkts_processed++;
                }

                mp = drbr_peek(ifp, fp->tx_br);
        }

qlnx_transmit_locked_exit:
        if((qlnx_num_tx_compl(ha,fp, fp->txq[0]) > QLNX_TX_COMPL_THRESH) ||
                ((int)(elem_left = ecore_chain_get_elem_left(&txq->tx_pbl))
                                        < QLNX_TX_ELEM_MAX_THRESH))
                (void)qlnx_tx_int(ha, fp, fp->txq[0]);

        QL_DPRINT2(ha, "%s: exit ret = %d\n", __func__, ret);
        return ret;
}


static int
qlnx_transmit(struct ifnet *ifp, struct mbuf  *mp)
{
        qlnx_host_t		*ha = (qlnx_host_t *)ifp->if_softc;
        struct qlnx_fastpath	*fp;
        int			rss_id = 0, ret = 0;

#ifdef QLNX_TRACEPERF_DATA
        uint64_t tx_pkts = 0, tx_compl = 0;
#endif

        QL_DPRINT2(ha, "enter\n");

#if __FreeBSD_version >= 1100000
        if (M_HASHTYPE_GET(mp) != M_HASHTYPE_NONE)
#else
        if (mp->m_flags & M_FLOWID)
#endif
                rss_id = (mp->m_pkthdr.flowid % ECORE_RSS_IND_TABLE_SIZE) %
					ha->num_rss;

        fp = &ha->fp_array[rss_id];

        if (fp->tx_br == NULL) {
                ret = EINVAL;
                goto qlnx_transmit_exit;
        }

        if (mtx_trylock(&fp->tx_mtx)) {

#ifdef QLNX_TRACEPERF_DATA
                        tx_pkts = fp->tx_pkts_transmitted;
                        tx_compl = fp->tx_pkts_completed;
#endif

                        ret = qlnx_transmit_locked(ifp, fp, mp);

#ifdef QLNX_TRACEPERF_DATA
                        fp->tx_pkts_trans_ctx += (fp->tx_pkts_transmitted - tx_pkts);
                        fp->tx_pkts_compl_ctx += (fp->tx_pkts_completed - tx_compl);
#endif
                        mtx_unlock(&fp->tx_mtx);
        } else {
                if (mp != NULL && (fp->fp_taskqueue != NULL)) {
                        ret = drbr_enqueue(ifp, fp->tx_br, mp);
                        taskqueue_enqueue(fp->fp_taskqueue, &fp->fp_task);
                }
        }

qlnx_transmit_exit:

        QL_DPRINT2(ha, "exit ret = %d\n", ret);
        return ret;
}

static void
qlnx_qflush(struct ifnet *ifp)
{
	int			rss_id;
	struct qlnx_fastpath	*fp;
	struct mbuf		*mp;
	qlnx_host_t		*ha;

	ha = (qlnx_host_t *)ifp->if_softc;

	QL_DPRINT2(ha, "enter\n");

	for (rss_id = 0; rss_id < ha->num_rss; rss_id++) {

		fp = &ha->fp_array[rss_id];

		if (fp == NULL)
			continue;

		if (fp->tx_br) {
			mtx_lock(&fp->tx_mtx);

			while ((mp = drbr_dequeue(ifp, fp->tx_br)) != NULL) { 
				fp->tx_pkts_freed++;
				m_freem(mp);			
			}
			mtx_unlock(&fp->tx_mtx);
		}
	}
	QL_DPRINT2(ha, "exit\n");

	return;
}

static void
qlnx_txq_doorbell_wr32(qlnx_host_t *ha, void *reg_addr, uint32_t value)
{
	struct ecore_dev	*cdev;
	uint32_t		offset;

	cdev = &ha->cdev;
		
	offset = (uint32_t)((uint8_t *)reg_addr - (uint8_t *)ha->pci_dbells);

	bus_write_4(ha->pci_dbells, offset, value);
	bus_barrier(ha->pci_reg,  0, 0, BUS_SPACE_BARRIER_READ);
	bus_barrier(ha->pci_dbells,  0, 0, BUS_SPACE_BARRIER_READ);

	return;
}

static uint32_t
qlnx_tcp_offset(qlnx_host_t *ha, struct mbuf *mp)
{
        struct ether_vlan_header	*eh = NULL;
        struct ip			*ip = NULL;
        struct ip6_hdr			*ip6 = NULL;
        struct tcphdr			*th = NULL;
        uint32_t			ehdrlen = 0, ip_hlen = 0, offset = 0;
        uint16_t			etype = 0;
        device_t			dev;
        uint8_t				buf[sizeof(struct ip6_hdr)];

        dev = ha->pci_dev;

        eh = mtod(mp, struct ether_vlan_header *);

        if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
                ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
                etype = ntohs(eh->evl_proto);
        } else {
                ehdrlen = ETHER_HDR_LEN;
                etype = ntohs(eh->evl_encap_proto);
        }

        switch (etype) {

                case ETHERTYPE_IP:
                        ip = (struct ip *)(mp->m_data + ehdrlen);

                        ip_hlen = sizeof (struct ip);

                        if (mp->m_len < (ehdrlen + ip_hlen)) {
                                m_copydata(mp, ehdrlen, sizeof(struct ip), buf);
                                ip = (struct ip *)buf;
                        }

                        th = (struct tcphdr *)(ip + 1);
			offset = ip_hlen + ehdrlen + (th->th_off << 2);
                break;

                case ETHERTYPE_IPV6:
                        ip6 = (struct ip6_hdr *)(mp->m_data + ehdrlen);

                        ip_hlen = sizeof(struct ip6_hdr);

                        if (mp->m_len < (ehdrlen + ip_hlen)) {
                                m_copydata(mp, ehdrlen, sizeof (struct ip6_hdr),
                                        buf);
                                ip6 = (struct ip6_hdr *)buf;
                        }
                        th = (struct tcphdr *)(ip6 + 1);
			offset = ip_hlen + ehdrlen + (th->th_off << 2);
                break;

                default:
                break;
        }

        return (offset);
}

static __inline int
qlnx_tso_check(struct qlnx_fastpath *fp, bus_dma_segment_t *segs, int nsegs,
	uint32_t offset)
{
	int			i;
	uint32_t		sum, nbds_in_hdr = 1;
        uint32_t		window;
        bus_dma_segment_t	*s_seg;

        /* If the header spans mulitple segments, skip those segments */

        if (nsegs < ETH_TX_LSO_WINDOW_BDS_NUM)
                return (0);

        i = 0;

        while ((i < nsegs) && (offset >= segs->ds_len)) {
                offset = offset - segs->ds_len;
                segs++;
                i++;
                nbds_in_hdr++;
        }

        window = ETH_TX_LSO_WINDOW_BDS_NUM - nbds_in_hdr;

        nsegs = nsegs - i;

        while (nsegs >= window) {

                sum = 0;
                s_seg = segs;

                for (i = 0; i < window; i++){
                        sum += s_seg->ds_len;
                        s_seg++;
                }

                if (sum < ETH_TX_LSO_WINDOW_MIN_LEN) {
                        fp->tx_lso_wnd_min_len++;
                        return (-1);
                }

                nsegs = nsegs - 1;
                segs++;
        }

	return (0);
}

static int
qlnx_send(qlnx_host_t *ha, struct qlnx_fastpath *fp, struct mbuf **m_headp)
{
	bus_dma_segment_t	*segs;
	bus_dmamap_t		map = 0;
	uint32_t		nsegs = 0;
	int			ret = -1;
	struct mbuf		*m_head = *m_headp;
	uint16_t		idx = 0;
	uint16_t		elem_left;

	uint8_t			nbd = 0;
	struct qlnx_tx_queue    *txq;

	struct eth_tx_1st_bd    *first_bd;
	struct eth_tx_2nd_bd    *second_bd;
	struct eth_tx_3rd_bd    *third_bd;
	struct eth_tx_bd        *tx_data_bd;

	int			seg_idx = 0;
	uint32_t		nbds_in_hdr = 0;
	uint32_t		offset = 0;

#ifdef QLNX_TRACE_PERF_DATA
        uint16_t                bd_used;
#endif

	QL_DPRINT8(ha, "enter[%d]\n", fp->rss_id);

	if (!ha->link_up)
		return (-1);

	first_bd	= NULL;
	second_bd	= NULL;
	third_bd	= NULL;
	tx_data_bd	= NULL;

	txq = fp->txq[0];

        if ((int)(elem_left = ecore_chain_get_elem_left(&txq->tx_pbl)) <
		QLNX_TX_ELEM_MIN_THRESH) {

                fp->tx_nsegs_gt_elem_left++;
                fp->err_tx_nsegs_gt_elem_left++;

                return (ENOBUFS);
        }

	idx = txq->sw_tx_prod;

	map = txq->sw_tx_ring[idx].map;
	segs = txq->segs;

	ret = bus_dmamap_load_mbuf_sg(ha->tx_tag, map, m_head, segs, &nsegs,
			BUS_DMA_NOWAIT);

	if (ha->dbg_trace_tso_pkt_len) {
		if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
			if (!fp->tx_tso_min_pkt_len) {
				fp->tx_tso_min_pkt_len = m_head->m_pkthdr.len;
				fp->tx_tso_min_pkt_len = m_head->m_pkthdr.len;
			} else {
				if (fp->tx_tso_min_pkt_len > m_head->m_pkthdr.len)
					fp->tx_tso_min_pkt_len =
						m_head->m_pkthdr.len;
				if (fp->tx_tso_max_pkt_len < m_head->m_pkthdr.len)
					fp->tx_tso_max_pkt_len =
						m_head->m_pkthdr.len;
			}
		}
	}

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO)
		offset = qlnx_tcp_offset(ha, m_head);

	if ((ret == EFBIG) ||
		((nsegs > QLNX_MAX_SEGMENTS_NON_TSO) && (
			(!(m_head->m_pkthdr.csum_flags & CSUM_TSO)) ||
		((m_head->m_pkthdr.csum_flags & CSUM_TSO) &&
			qlnx_tso_check(fp, segs, nsegs, offset))))) {

		struct mbuf *m;

		QL_DPRINT8(ha, "EFBIG [%d]\n", m_head->m_pkthdr.len);

		fp->tx_defrag++;

		m = m_defrag(m_head, M_NOWAIT);
		if (m == NULL) {
			fp->err_tx_defrag++;
			fp->tx_pkts_freed++;
			m_freem(m_head);
			*m_headp = NULL;
			QL_DPRINT1(ha, "m_defrag() = NULL [%d]\n", ret);
			return (ENOBUFS);
		}

		m_head = m;
		*m_headp = m_head;

		if ((ret = bus_dmamap_load_mbuf_sg(ha->tx_tag, map, m_head,
				segs, &nsegs, BUS_DMA_NOWAIT))) {

			fp->err_tx_defrag_dmamap_load++;

			QL_DPRINT1(ha,
				"bus_dmamap_load_mbuf_sg failed0 [%d, %d]\n",
				ret, m_head->m_pkthdr.len);

			fp->tx_pkts_freed++;
			m_freem(m_head);
			*m_headp = NULL;

			return (ret);
		}

		if ((nsegs > QLNX_MAX_SEGMENTS_NON_TSO) &&
			!(m_head->m_pkthdr.csum_flags & CSUM_TSO)) {

			fp->err_tx_non_tso_max_seg++;

			QL_DPRINT1(ha,
				"(%d) nsegs too many for non-TSO [%d, %d]\n",
				ret, nsegs, m_head->m_pkthdr.len);

			fp->tx_pkts_freed++;
			m_freem(m_head);
			*m_headp = NULL;

			return (ret);
		}
		if (m_head->m_pkthdr.csum_flags & CSUM_TSO)
			offset = qlnx_tcp_offset(ha, m_head);

	} else if (ret) {

		fp->err_tx_dmamap_load++;

		QL_DPRINT1(ha, "bus_dmamap_load_mbuf_sg failed1 [%d, %d]\n",
			   ret, m_head->m_pkthdr.len);
		fp->tx_pkts_freed++;
		m_freem(m_head);
		*m_headp = NULL;
		return (ret);
	}

	QL_ASSERT(ha, (nsegs != 0), ("qlnx_send: empty packet"));

	if (ha->dbg_trace_tso_pkt_len) {
		if (nsegs < QLNX_FP_MAX_SEGS)
			fp->tx_pkts[(nsegs - 1)]++;
		else
			fp->tx_pkts[(QLNX_FP_MAX_SEGS - 1)]++; 
	}

#ifdef QLNX_TRACE_PERF_DATA
        if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
                if(m_head->m_pkthdr.len <= 2048)
                        fp->tx_pkts_hist[0]++;
                else if((m_head->m_pkthdr.len > 2048) &&
				(m_head->m_pkthdr.len <= 4096))
                        fp->tx_pkts_hist[1]++;
                else if((m_head->m_pkthdr.len > 4096) &&
				(m_head->m_pkthdr.len <= 8192))
                        fp->tx_pkts_hist[2]++;
                else if((m_head->m_pkthdr.len > 8192) &&
				(m_head->m_pkthdr.len <= 12288 ))
                        fp->tx_pkts_hist[3]++;
                else if((m_head->m_pkthdr.len > 11288) &&
				(m_head->m_pkthdr.len <= 16394))
                        fp->tx_pkts_hist[4]++;
                else if((m_head->m_pkthdr.len > 16384) &&
				(m_head->m_pkthdr.len <= 20480))
                        fp->tx_pkts_hist[5]++;
                else if((m_head->m_pkthdr.len > 20480) &&
				(m_head->m_pkthdr.len <= 24576))
                        fp->tx_pkts_hist[6]++;
                else if((m_head->m_pkthdr.len > 24576) &&
				(m_head->m_pkthdr.len <= 28672))
                        fp->tx_pkts_hist[7]++;
                else if((m_head->m_pkthdr.len > 28762) &&
				(m_head->m_pkthdr.len <= 32768))
                        fp->tx_pkts_hist[8]++;
                else if((m_head->m_pkthdr.len > 32768) &&
				(m_head->m_pkthdr.len <= 36864))
                        fp->tx_pkts_hist[9]++;
                else if((m_head->m_pkthdr.len > 36864) &&
				(m_head->m_pkthdr.len <= 40960))
                        fp->tx_pkts_hist[10]++;
                else if((m_head->m_pkthdr.len > 40960) &&
				(m_head->m_pkthdr.len <= 45056))
                        fp->tx_pkts_hist[11]++;
                else if((m_head->m_pkthdr.len > 45056) &&
				(m_head->m_pkthdr.len <= 49152))
                        fp->tx_pkts_hist[12]++;
                else if((m_head->m_pkthdr.len > 49512) && 
				m_head->m_pkthdr.len <= 53248))
                        fp->tx_pkts_hist[13]++;
                else if((m_head->m_pkthdr.len > 53248) &&
				(m_head->m_pkthdr.len <= 57344))
                        fp->tx_pkts_hist[14]++;
                else if((m_head->m_pkthdr.len > 53248) &&
				(m_head->m_pkthdr.len <= 57344))
                        fp->tx_pkts_hist[15]++;
                else if((m_head->m_pkthdr.len > 57344) &&
				(m_head->m_pkthdr.len <= 61440))
                        fp->tx_pkts_hist[16]++;
                else
                        fp->tx_pkts_hist[17]++;
        }

        if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {

                elem_left =  ecore_chain_get_elem_left(&txq->tx_pbl);
                bd_used = TX_RING_SIZE - elem_left;

                if(bd_used <= 100)
                        fp->tx_pkts_q[0]++;
                else if((bd_used > 100) && (bd_used <= 500))
                        fp->tx_pkts_q[1]++;
                else if((bd_used > 500) && (bd_used <= 1000))
                        fp->tx_pkts_q[2]++;
                else if((bd_used > 1000) && (bd_used <= 2000))
                        fp->tx_pkts_q[3]++;
                else if((bd_used > 3000) && (bd_used <= 4000))
                        fp->tx_pkts_q[4]++;
                else if((bd_used > 4000) && (bd_used <= 5000))
                        fp->tx_pkts_q[5]++;
                else if((bd_used > 6000) && (bd_used <= 7000))
                        fp->tx_pkts_q[6]++;
                else if((bd_used > 7000) && (bd_used <= 8000))
                        fp->tx_pkts_q[7]++;
                else if((bd_used > 8000) && (bd_used <= 9000))
                        fp->tx_pkts_q[8]++;
                else if((bd_used > 9000) && (bd_used <= 10000))
                        fp->tx_pkts_q[9]++;
                else if((bd_used > 10000) && (bd_used <= 11000))
                        fp->tx_pkts_q[10]++;
                else if((bd_used > 11000) && (bd_used <= 12000))
                        fp->tx_pkts_q[11]++;
                else if((bd_used > 12000) && (bd_used <= 13000))
                        fp->tx_pkts_q[12]++;
                else if((bd_used > 13000) && (bd_used <= 14000))
                        fp->tx_pkts_q[13]++;
                else if((bd_used > 14000) && (bd_used <= 15000))
                        fp->tx_pkts_q[14]++;
               else if((bd_used > 15000) && (bd_used <= 16000))
                        fp->tx_pkts_q[15]++;
                else
                        fp->tx_pkts_q[16]++;
        }

#endif /* end of QLNX_TRACE_PERF_DATA */

	if ((nsegs + QLNX_TX_ELEM_RESERVE) >
		(int)(elem_left = ecore_chain_get_elem_left(&txq->tx_pbl))) {

		QL_DPRINT1(ha, "(%d, 0x%x) insuffient BDs"
			" in chain[%d] trying to free packets\n",
			nsegs, elem_left, fp->rss_id);

		fp->tx_nsegs_gt_elem_left++;

		(void)qlnx_tx_int(ha, fp, txq);

		if ((nsegs + QLNX_TX_ELEM_RESERVE) > (int)(elem_left =
			ecore_chain_get_elem_left(&txq->tx_pbl))) {

			QL_DPRINT1(ha,
				"(%d, 0x%x) insuffient BDs in chain[%d]\n",
				nsegs, elem_left, fp->rss_id);

			fp->err_tx_nsegs_gt_elem_left++;
			fp->tx_ring_full = 1;
			if (ha->storm_stats_enable)
				ha->storm_stats_gather = 1;
			return (ENOBUFS);
		}
	}

	bus_dmamap_sync(ha->tx_tag, map, BUS_DMASYNC_PREWRITE);

	txq->sw_tx_ring[idx].mp = m_head;

	first_bd = (struct eth_tx_1st_bd *)ecore_chain_produce(&txq->tx_pbl);

	memset(first_bd, 0, sizeof(*first_bd));

	first_bd->data.bd_flags.bitfields =
		1 << ETH_TX_1ST_BD_FLAGS_START_BD_SHIFT;

	BD_SET_UNMAP_ADDR_LEN(first_bd, segs->ds_addr, segs->ds_len);

	nbd++;

	if (m_head->m_pkthdr.csum_flags & CSUM_IP) {
		first_bd->data.bd_flags.bitfields |=
			(1 << ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT);
	}

	if (m_head->m_pkthdr.csum_flags &
		(CSUM_UDP | CSUM_TCP | CSUM_TCP_IPV6 | CSUM_UDP_IPV6)) {
		first_bd->data.bd_flags.bitfields |=
			(1 << ETH_TX_1ST_BD_FLAGS_L4_CSUM_SHIFT);
	}

        if (m_head->m_flags & M_VLANTAG) {
                first_bd->data.vlan = m_head->m_pkthdr.ether_vtag;
		first_bd->data.bd_flags.bitfields |=
			(1 << ETH_TX_1ST_BD_FLAGS_VLAN_INSERTION_SHIFT);
        }

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {

                first_bd->data.bd_flags.bitfields |=
			(1 << ETH_TX_1ST_BD_FLAGS_LSO_SHIFT);
		first_bd->data.bd_flags.bitfields |=
			(1 << ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT);

		nbds_in_hdr = 1;

		if (offset == segs->ds_len) {
			BD_SET_UNMAP_ADDR_LEN(first_bd, segs->ds_addr, offset);
			segs++;
			seg_idx++;

			second_bd = (struct eth_tx_2nd_bd *)
					ecore_chain_produce(&txq->tx_pbl);
			memset(second_bd, 0, sizeof(*second_bd));
			nbd++;

			if (seg_idx < nsegs) {
				BD_SET_UNMAP_ADDR_LEN(second_bd, \
					(segs->ds_addr), (segs->ds_len));
				segs++;
				seg_idx++;
			}

			third_bd = (struct eth_tx_3rd_bd *)
					ecore_chain_produce(&txq->tx_pbl);
			memset(third_bd, 0, sizeof(*third_bd));
			third_bd->data.lso_mss = m_head->m_pkthdr.tso_segsz;
			third_bd->data.bitfields |=
				(nbds_in_hdr<<ETH_TX_DATA_3RD_BD_HDR_NBD_SHIFT);
			nbd++;

			if (seg_idx < nsegs) {
				BD_SET_UNMAP_ADDR_LEN(third_bd, \
					(segs->ds_addr), (segs->ds_len));
				segs++;
				seg_idx++;
			}

			for (; seg_idx < nsegs; seg_idx++) {
				tx_data_bd = (struct eth_tx_bd *)
					ecore_chain_produce(&txq->tx_pbl);
				memset(tx_data_bd, 0, sizeof(*tx_data_bd));
				BD_SET_UNMAP_ADDR_LEN(tx_data_bd, \
					segs->ds_addr,\
					segs->ds_len);
				segs++;
				nbd++;
			}

		} else if (offset < segs->ds_len) {
			BD_SET_UNMAP_ADDR_LEN(first_bd, segs->ds_addr, offset);

			second_bd = (struct eth_tx_2nd_bd *)
					ecore_chain_produce(&txq->tx_pbl);
			memset(second_bd, 0, sizeof(*second_bd));
			BD_SET_UNMAP_ADDR_LEN(second_bd, \
				(segs->ds_addr + offset),\
				(segs->ds_len - offset));
			nbd++;
			segs++;

			third_bd = (struct eth_tx_3rd_bd *)
					ecore_chain_produce(&txq->tx_pbl);
			memset(third_bd, 0, sizeof(*third_bd));

			BD_SET_UNMAP_ADDR_LEN(third_bd, \
					segs->ds_addr,\
					segs->ds_len);
			third_bd->data.lso_mss = m_head->m_pkthdr.tso_segsz;
			third_bd->data.bitfields |=
				(nbds_in_hdr<<ETH_TX_DATA_3RD_BD_HDR_NBD_SHIFT);
			segs++;
			nbd++;

			for (seg_idx = 2; seg_idx < nsegs; seg_idx++) {
				tx_data_bd = (struct eth_tx_bd *)
					ecore_chain_produce(&txq->tx_pbl);
				memset(tx_data_bd, 0, sizeof(*tx_data_bd));
				BD_SET_UNMAP_ADDR_LEN(tx_data_bd, \
					segs->ds_addr,\
					segs->ds_len);
				segs++;
				nbd++;
			}

		} else {
			offset = offset - segs->ds_len;
			segs++;

			for (seg_idx = 1; seg_idx < nsegs; seg_idx++) {

				if (offset)
					nbds_in_hdr++;

				tx_data_bd = (struct eth_tx_bd *)
					ecore_chain_produce(&txq->tx_pbl);
				memset(tx_data_bd, 0, sizeof(*tx_data_bd));

				if (second_bd == NULL) {
					second_bd = (struct eth_tx_2nd_bd *)
								tx_data_bd;
				} else if (third_bd == NULL) {
					third_bd = (struct eth_tx_3rd_bd *)
								tx_data_bd;
				}
				
				if (offset && (offset < segs->ds_len)) {
					BD_SET_UNMAP_ADDR_LEN(tx_data_bd,\
						segs->ds_addr, offset);

					tx_data_bd = (struct eth_tx_bd *)
					ecore_chain_produce(&txq->tx_pbl);

					memset(tx_data_bd, 0,
						sizeof(*tx_data_bd));

					if (second_bd == NULL) {
						second_bd =
					(struct eth_tx_2nd_bd *)tx_data_bd;
					} else if (third_bd == NULL) {
						third_bd =
					(struct eth_tx_3rd_bd *)tx_data_bd;
					}
					BD_SET_UNMAP_ADDR_LEN(tx_data_bd,\
						(segs->ds_addr + offset), \
						(segs->ds_len - offset));
					nbd++;
					offset = 0;
				} else {
					if (offset)
						offset = offset - segs->ds_len;
					BD_SET_UNMAP_ADDR_LEN(tx_data_bd,\
						segs->ds_addr, segs->ds_len);
				}
				segs++;
				nbd++;
			}

			if (third_bd == NULL) {
				third_bd = (struct eth_tx_3rd_bd *)
					ecore_chain_produce(&txq->tx_pbl);
				memset(third_bd, 0, sizeof(*third_bd));
			}

			third_bd->data.lso_mss = m_head->m_pkthdr.tso_segsz;
			third_bd->data.bitfields |=
				(nbds_in_hdr<<ETH_TX_DATA_3RD_BD_HDR_NBD_SHIFT);
		}
		fp->tx_tso_pkts++;
	} else {
		segs++;
		for (seg_idx = 1; seg_idx < nsegs; seg_idx++) {
			tx_data_bd = (struct eth_tx_bd *)
					ecore_chain_produce(&txq->tx_pbl);
			memset(tx_data_bd, 0, sizeof(*tx_data_bd));
			BD_SET_UNMAP_ADDR_LEN(tx_data_bd, segs->ds_addr,\
				segs->ds_len);
			segs++;
			nbd++;
		}
		first_bd->data.bitfields =
			(m_head->m_pkthdr.len & ETH_TX_DATA_1ST_BD_PKT_LEN_MASK)
				 << ETH_TX_DATA_1ST_BD_PKT_LEN_SHIFT;
		first_bd->data.bitfields =
			htole16(first_bd->data.bitfields);
		fp->tx_non_tso_pkts++;
	}


	first_bd->data.nbds = nbd;

	if (ha->dbg_trace_tso_pkt_len) {
		if (fp->tx_tso_max_nsegs < nsegs)
			fp->tx_tso_max_nsegs = nsegs;

		if ((nsegs < fp->tx_tso_min_nsegs) || (!fp->tx_tso_min_nsegs))
			fp->tx_tso_min_nsegs = nsegs;
	}

	txq->sw_tx_ring[idx].nsegs = nsegs;
	txq->sw_tx_prod = (txq->sw_tx_prod + 1) & (TX_RING_SIZE - 1);

	txq->tx_db.data.bd_prod =
		htole16(ecore_chain_get_prod_idx(&txq->tx_pbl));

	qlnx_txq_doorbell_wr32(ha, txq->doorbell_addr, txq->tx_db.raw);
   
	QL_DPRINT8(ha, "exit[%d]\n", fp->rss_id);
	return (0);
}

static void
qlnx_stop(qlnx_host_t *ha)
{
	struct ifnet	*ifp = ha->ifp;
	device_t	dev;
	int		i;

	dev = ha->pci_dev;

	ifp->if_drv_flags &= ~(IFF_DRV_OACTIVE | IFF_DRV_RUNNING);

	/*
	 * We simply lock and unlock each fp->tx_mtx to
	 * propagate the if_drv_flags
	 * state to each tx thread
	 */
        QL_DPRINT1(ha, "QLNX STATE = %d\n",ha->state);

	if (ha->state == QLNX_STATE_OPEN) {
        	for (i = 0; i < ha->num_rss; i++) {
			struct qlnx_fastpath *fp = &ha->fp_array[i];

			mtx_lock(&fp->tx_mtx);
			mtx_unlock(&fp->tx_mtx);

			if (fp->fp_taskqueue != NULL)
				taskqueue_enqueue(fp->fp_taskqueue,
					&fp->fp_task);
		}
	}
#ifdef QLNX_ENABLE_IWARP
	if (qlnx_vf_device(ha) != 0) {
		qlnx_rdma_dev_close(ha);
	}
#endif /* #ifdef QLNX_ENABLE_IWARP */

	qlnx_unload(ha);

	return;
}

static int
qlnx_get_ifq_snd_maxlen(qlnx_host_t *ha)
{
        return(TX_RING_SIZE - 1);
}

uint8_t *
qlnx_get_mac_addr(qlnx_host_t *ha)
{
	struct ecore_hwfn	*p_hwfn;
	unsigned char mac[ETHER_ADDR_LEN];
	uint8_t			p_is_forced;

	p_hwfn = &ha->cdev.hwfns[0];

	if (qlnx_vf_device(ha) != 0) 
		return (p_hwfn->hw_info.hw_mac_addr);

	ecore_vf_read_bulletin(p_hwfn, &p_is_forced);
	if (ecore_vf_bulletin_get_forced_mac(p_hwfn, mac, &p_is_forced) ==
		true) {
		device_printf(ha->pci_dev, "%s: p_is_forced = %d"
			" mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
			p_is_forced, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        	memcpy(ha->primary_mac, mac, ETH_ALEN);
	}

	return (ha->primary_mac);
}

static uint32_t
qlnx_get_optics(qlnx_host_t *ha, struct qlnx_link_output *if_link)
{
	uint32_t	ifm_type = 0;

	switch (if_link->media_type) {

	case MEDIA_MODULE_FIBER:
	case MEDIA_UNSPECIFIED:
		if (if_link->speed == (100 * 1000))
			ifm_type = QLNX_IFM_100G_SR4;
		else if (if_link->speed == (40 * 1000))
			ifm_type = IFM_40G_SR4;
		else if (if_link->speed == (25 * 1000))
			ifm_type = QLNX_IFM_25G_SR;
		else if (if_link->speed == (10 * 1000))
			ifm_type = (IFM_10G_LR | IFM_10G_SR);
		else if (if_link->speed == (1 * 1000))
			ifm_type = (IFM_1000_SX | IFM_1000_LX);

		break;

	case MEDIA_DA_TWINAX:
		if (if_link->speed == (100 * 1000))
			ifm_type = QLNX_IFM_100G_CR4;
		else if (if_link->speed == (40 * 1000))
			ifm_type = IFM_40G_CR4;
		else if (if_link->speed == (25 * 1000))
			ifm_type = QLNX_IFM_25G_CR;
		else if (if_link->speed == (10 * 1000))
			ifm_type = IFM_10G_TWINAX;

		break;

	default :
		ifm_type = IFM_UNKNOWN;
		break;
	}
	return (ifm_type);
}



/*****************************************************************************
 * Interrupt Service Functions
 *****************************************************************************/

static int
qlnx_rx_jumbo_chain(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct mbuf *mp_head, uint16_t len)
{
	struct mbuf		*mp, *mpf, *mpl;
	struct sw_rx_data	*sw_rx_data;
	struct qlnx_rx_queue	*rxq;
	uint16_t 		len_in_buffer;

	rxq = fp->rxq;
	mpf = mpl = mp = NULL;

	while (len) {

        	rxq->sw_rx_cons  = (rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

                sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_cons];
                mp = sw_rx_data->data;

		if (mp == NULL) {
                	QL_DPRINT1(ha, "mp = NULL\n");
			fp->err_rx_mp_null++;
        		rxq->sw_rx_cons  =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

			if (mpf != NULL)
				m_freem(mpf);

			return (-1);
		}
		bus_dmamap_sync(ha->rx_tag, sw_rx_data->map,
			BUS_DMASYNC_POSTREAD);

                if (qlnx_alloc_rx_buffer(ha, rxq) != 0) {

                        QL_DPRINT1(ha, "New buffer allocation failed, dropping"
				" incoming packet and reusing its buffer\n");

                        qlnx_reuse_rx_data(rxq);
                        fp->err_rx_alloc_errors++;

			if (mpf != NULL)
				m_freem(mpf);

			return (-1);
		}
                ecore_chain_consume(&rxq->rx_bd_ring);

		if (len > rxq->rx_buf_size)
			len_in_buffer = rxq->rx_buf_size;
		else
			len_in_buffer = len;

		len = len - len_in_buffer;

		mp->m_flags &= ~M_PKTHDR;
		mp->m_next = NULL;
		mp->m_len = len_in_buffer;

		if (mpf == NULL)
			mpf = mpl = mp;
		else {
			mpl->m_next = mp;
			mpl = mp;
		}
	}

	if (mpf != NULL)
		mp_head->m_next = mpf;

	return (0);
}

static void
qlnx_tpa_start(qlnx_host_t *ha,
	struct qlnx_fastpath *fp,
	struct qlnx_rx_queue *rxq,
	struct eth_fast_path_rx_tpa_start_cqe *cqe)
{
	uint32_t		agg_index;
        struct ifnet		*ifp = ha->ifp;
	struct mbuf		*mp;
	struct mbuf		*mpf = NULL, *mpl = NULL, *mpc = NULL;
	struct sw_rx_data	*sw_rx_data;
	dma_addr_t		addr;
	bus_dmamap_t		map;
	struct eth_rx_bd	*rx_bd;
	int			i;
	device_t		dev;
#if __FreeBSD_version >= 1100000
	uint8_t			hash_type;
#endif /* #if __FreeBSD_version >= 1100000 */

	dev = ha->pci_dev;
	agg_index = cqe->tpa_agg_index;

        QL_DPRINT7(ha, "[rss_id = %d]: enter\n \
                \t type = 0x%x\n \
                \t bitfields = 0x%x\n \
                \t seg_len = 0x%x\n \
                \t pars_flags = 0x%x\n \
                \t vlan_tag = 0x%x\n \
                \t rss_hash = 0x%x\n \
                \t len_on_first_bd = 0x%x\n \
                \t placement_offset = 0x%x\n \
                \t tpa_agg_index = 0x%x\n \
                \t header_len = 0x%x\n \
                \t ext_bd_len_list[0] = 0x%x\n \
                \t ext_bd_len_list[1] = 0x%x\n \
                \t ext_bd_len_list[2] = 0x%x\n \
                \t ext_bd_len_list[3] = 0x%x\n \
                \t ext_bd_len_list[4] = 0x%x\n",
                fp->rss_id, cqe->type, cqe->bitfields, cqe->seg_len,
                cqe->pars_flags.flags, cqe->vlan_tag,
                cqe->rss_hash, cqe->len_on_first_bd, cqe->placement_offset,
                cqe->tpa_agg_index, cqe->header_len,
                cqe->ext_bd_len_list[0], cqe->ext_bd_len_list[1],
                cqe->ext_bd_len_list[2], cqe->ext_bd_len_list[3],
                cqe->ext_bd_len_list[4]);

	if (agg_index >= ETH_TPA_MAX_AGGS_NUM) {
		fp->err_rx_tpa_invalid_agg_num++;
		return;
	}

	sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_cons];
	bus_dmamap_sync(ha->rx_tag, sw_rx_data->map, BUS_DMASYNC_POSTREAD);
	mp = sw_rx_data->data;

	QL_DPRINT7(ha, "[rss_id = %d]: mp = %p \n ", fp->rss_id, mp);

	if (mp == NULL) {
               	QL_DPRINT7(ha, "[%d]: mp = NULL\n", fp->rss_id);
		fp->err_rx_mp_null++;
       		rxq->sw_rx_cons = (rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

		return;
	}

	if ((le16toh(cqe->pars_flags.flags)) & CQE_FLAGS_ERR) {

		QL_DPRINT7(ha, "[%d]: CQE in CONS = %u has error,"
			" flags = %x, dropping incoming packet\n", fp->rss_id,
			rxq->sw_rx_cons, le16toh(cqe->pars_flags.flags));

		fp->err_rx_hw_errors++;

		qlnx_reuse_rx_data(rxq);

		QLNX_INC_IERRORS(ifp);

		return;
	}

	if (qlnx_alloc_rx_buffer(ha, rxq) != 0) {

		QL_DPRINT7(ha, "[%d]: New buffer allocation failed,"
			" dropping incoming packet and reusing its buffer\n",
			fp->rss_id);

		fp->err_rx_alloc_errors++;
		QLNX_INC_IQDROPS(ifp);

		/*
		 * Load the tpa mbuf into the rx ring and save the 
		 * posted mbuf
		 */

		map = sw_rx_data->map;
		addr = sw_rx_data->dma_addr;

		sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_prod];

		sw_rx_data->data = rxq->tpa_info[agg_index].rx_buf.data;
		sw_rx_data->dma_addr = rxq->tpa_info[agg_index].rx_buf.dma_addr;
		sw_rx_data->map = rxq->tpa_info[agg_index].rx_buf.map;

		rxq->tpa_info[agg_index].rx_buf.data = mp;
		rxq->tpa_info[agg_index].rx_buf.dma_addr = addr;
		rxq->tpa_info[agg_index].rx_buf.map = map;

		rx_bd = (struct eth_rx_bd *)
				ecore_chain_produce(&rxq->rx_bd_ring);

		rx_bd->addr.hi = htole32(U64_HI(sw_rx_data->dma_addr));
		rx_bd->addr.lo = htole32(U64_LO(sw_rx_data->dma_addr));

		bus_dmamap_sync(ha->rx_tag, sw_rx_data->map,
			BUS_DMASYNC_PREREAD);

		rxq->sw_rx_prod = (rxq->sw_rx_prod + 1) & (RX_RING_SIZE - 1);
		rxq->sw_rx_cons = (rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

		ecore_chain_consume(&rxq->rx_bd_ring);

		/* Now reuse any buffers posted in ext_bd_len_list */
		for (i = 0; i < ETH_TPA_CQE_START_LEN_LIST_SIZE; i++) {

			if (cqe->ext_bd_len_list[i] == 0)
				break;

			qlnx_reuse_rx_data(rxq);
		}

		rxq->tpa_info[agg_index].agg_state = QLNX_AGG_STATE_ERROR;
		return;
	}

	if (rxq->tpa_info[agg_index].agg_state != QLNX_AGG_STATE_NONE) {

		QL_DPRINT7(ha, "[%d]: invalid aggregation state,"
			" dropping incoming packet and reusing its buffer\n",
			fp->rss_id);

		QLNX_INC_IQDROPS(ifp);

		/* if we already have mbuf head in aggregation free it */
		if (rxq->tpa_info[agg_index].mpf) {
			m_freem(rxq->tpa_info[agg_index].mpf);
			rxq->tpa_info[agg_index].mpl = NULL;
		}
		rxq->tpa_info[agg_index].mpf = mp;
		rxq->tpa_info[agg_index].mpl = NULL;

		rxq->sw_rx_cons = (rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
		ecore_chain_consume(&rxq->rx_bd_ring);

		/* Now reuse any buffers posted in ext_bd_len_list */
		for (i = 0; i < ETH_TPA_CQE_START_LEN_LIST_SIZE; i++) {

			if (cqe->ext_bd_len_list[i] == 0)
				break;

			qlnx_reuse_rx_data(rxq);
		}
		rxq->tpa_info[agg_index].agg_state = QLNX_AGG_STATE_ERROR;

		return;
	}

	/*
	 * first process the ext_bd_len_list 
	 * if this fails then we simply drop the packet
	 */
	ecore_chain_consume(&rxq->rx_bd_ring);
	rxq->sw_rx_cons  = (rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

	for (i = 0; i < ETH_TPA_CQE_START_LEN_LIST_SIZE; i++) {

		QL_DPRINT7(ha, "[%d]: 4\n ", fp->rss_id);

		if (cqe->ext_bd_len_list[i] == 0)
			break;

		sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_cons];
		bus_dmamap_sync(ha->rx_tag, sw_rx_data->map,
			BUS_DMASYNC_POSTREAD);

		mpc = sw_rx_data->data;

		if (mpc == NULL) {
			QL_DPRINT7(ha, "[%d]: mpc = NULL\n", fp->rss_id);
			fp->err_rx_mp_null++;
			if (mpf != NULL)
				m_freem(mpf);
			mpf = mpl = NULL;
			rxq->tpa_info[agg_index].agg_state =
						QLNX_AGG_STATE_ERROR;
			ecore_chain_consume(&rxq->rx_bd_ring);
			rxq->sw_rx_cons =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
			continue;
		}

		if (qlnx_alloc_rx_buffer(ha, rxq) != 0) {
			QL_DPRINT7(ha, "[%d]: New buffer allocation failed,"
				" dropping incoming packet and reusing its"
				" buffer\n", fp->rss_id);

			qlnx_reuse_rx_data(rxq);

			if (mpf != NULL)
				m_freem(mpf);
			mpf = mpl = NULL;

			rxq->tpa_info[agg_index].agg_state =
						QLNX_AGG_STATE_ERROR;

			ecore_chain_consume(&rxq->rx_bd_ring);
			rxq->sw_rx_cons =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

			continue;
		}

		mpc->m_flags &= ~M_PKTHDR;
		mpc->m_next = NULL;
		mpc->m_len = cqe->ext_bd_len_list[i];


		if (mpf == NULL) {
			mpf = mpl = mpc;
		} else {
			mpl->m_len = ha->rx_buf_size;
			mpl->m_next = mpc;
			mpl = mpc;
		}

		ecore_chain_consume(&rxq->rx_bd_ring);
		rxq->sw_rx_cons =
			(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
	}

	if (rxq->tpa_info[agg_index].agg_state != QLNX_AGG_STATE_NONE) {

		QL_DPRINT7(ha, "[%d]: invalid aggregation state, dropping"
			" incoming packet and reusing its buffer\n",
			fp->rss_id);

		QLNX_INC_IQDROPS(ifp);

		rxq->tpa_info[agg_index].mpf = mp;
		rxq->tpa_info[agg_index].mpl = NULL;

		return;
	}
	   
        rxq->tpa_info[agg_index].placement_offset = cqe->placement_offset;

        if (mpf != NULL) {
                mp->m_len = ha->rx_buf_size;
                mp->m_next = mpf;
                rxq->tpa_info[agg_index].mpf = mp;
                rxq->tpa_info[agg_index].mpl = mpl;
        } else {
                mp->m_len = cqe->len_on_first_bd + cqe->placement_offset;
                rxq->tpa_info[agg_index].mpf = mp;
                rxq->tpa_info[agg_index].mpl = mp;
                mp->m_next = NULL;
        }

	mp->m_flags |= M_PKTHDR;

	/* assign packet to this interface interface */
	mp->m_pkthdr.rcvif = ifp;

	/* assume no hardware checksum has complated */
	mp->m_pkthdr.csum_flags = 0;

	//mp->m_pkthdr.flowid = fp->rss_id;
	mp->m_pkthdr.flowid = cqe->rss_hash;

#if __FreeBSD_version >= 1100000

	hash_type = cqe->bitfields &
			(ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE_MASK <<
			ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE_SHIFT);

	switch (hash_type) {

	case RSS_HASH_TYPE_IPV4:
		M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_IPV4);
		break;

	case RSS_HASH_TYPE_TCP_IPV4:
		M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_TCP_IPV4);
		break;

	case RSS_HASH_TYPE_IPV6:
		M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_IPV6);
		break;

	case RSS_HASH_TYPE_TCP_IPV6:
		M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_TCP_IPV6);
		break;

	default:
		M_HASHTYPE_SET(mp, M_HASHTYPE_OPAQUE);
		break;
	}

#else
	mp->m_flags |= M_FLOWID;
#endif

	mp->m_pkthdr.csum_flags |= (CSUM_IP_CHECKED | CSUM_IP_VALID |
					CSUM_DATA_VALID | CSUM_PSEUDO_HDR);

	mp->m_pkthdr.csum_data = 0xFFFF;

	if (CQE_HAS_VLAN(cqe->pars_flags.flags)) {
		mp->m_pkthdr.ether_vtag = le16toh(cqe->vlan_tag);
		mp->m_flags |= M_VLANTAG;
	}

	rxq->tpa_info[agg_index].agg_state = QLNX_AGG_STATE_START;

        QL_DPRINT7(ha, "[%d]: 5\n\tagg_state = %d\n\t mpf = %p mpl = %p\n",
		fp->rss_id, rxq->tpa_info[agg_index].agg_state,
                rxq->tpa_info[agg_index].mpf, rxq->tpa_info[agg_index].mpl);

	return;
}

static void
qlnx_tpa_cont(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct qlnx_rx_queue *rxq,
	struct eth_fast_path_rx_tpa_cont_cqe *cqe)
{
	struct sw_rx_data	*sw_rx_data;
	int			i;
	struct mbuf		*mpf = NULL, *mpl = NULL, *mpc = NULL;
	struct mbuf		*mp;
	uint32_t		agg_index;
	device_t		dev;

	dev = ha->pci_dev;

        QL_DPRINT7(ha, "[%d]: enter\n \
                \t type = 0x%x\n \
                \t tpa_agg_index = 0x%x\n \
                \t len_list[0] = 0x%x\n \
                \t len_list[1] = 0x%x\n \
                \t len_list[2] = 0x%x\n \
                \t len_list[3] = 0x%x\n \
                \t len_list[4] = 0x%x\n \
                \t len_list[5] = 0x%x\n",
                fp->rss_id, cqe->type, cqe->tpa_agg_index,
                cqe->len_list[0], cqe->len_list[1], cqe->len_list[2],
                cqe->len_list[3], cqe->len_list[4], cqe->len_list[5]);

	agg_index = cqe->tpa_agg_index;

	if (agg_index >= ETH_TPA_MAX_AGGS_NUM) {
		QL_DPRINT7(ha, "[%d]: 0\n ", fp->rss_id);
		fp->err_rx_tpa_invalid_agg_num++;
		return;
	}


	for (i = 0; i < ETH_TPA_CQE_CONT_LEN_LIST_SIZE; i++) {

		QL_DPRINT7(ha, "[%d]: 1\n ", fp->rss_id);

		if (cqe->len_list[i] == 0)
			break;

		if (rxq->tpa_info[agg_index].agg_state != 
			QLNX_AGG_STATE_START) {
			qlnx_reuse_rx_data(rxq);
			continue;
		}

		sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_cons];
		bus_dmamap_sync(ha->rx_tag, sw_rx_data->map,
			BUS_DMASYNC_POSTREAD);

		mpc = sw_rx_data->data;

		if (mpc == NULL) {

			QL_DPRINT7(ha, "[%d]: mpc = NULL\n", fp->rss_id);

			fp->err_rx_mp_null++;
			if (mpf != NULL)
				m_freem(mpf);
			mpf = mpl = NULL;
			rxq->tpa_info[agg_index].agg_state =
						QLNX_AGG_STATE_ERROR;
			ecore_chain_consume(&rxq->rx_bd_ring);
			rxq->sw_rx_cons =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
			continue;
		}

		if (qlnx_alloc_rx_buffer(ha, rxq) != 0) {

			QL_DPRINT7(ha, "[%d]: New buffer allocation failed,"
				" dropping incoming packet and reusing its"
				" buffer\n", fp->rss_id);

			qlnx_reuse_rx_data(rxq);

			if (mpf != NULL)
				m_freem(mpf);
			mpf = mpl = NULL;

			rxq->tpa_info[agg_index].agg_state =
						QLNX_AGG_STATE_ERROR;

			ecore_chain_consume(&rxq->rx_bd_ring);
			rxq->sw_rx_cons =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

			continue;
		}

		mpc->m_flags &= ~M_PKTHDR;
		mpc->m_next = NULL;
		mpc->m_len = cqe->len_list[i];


		if (mpf == NULL) {
			mpf = mpl = mpc;
		} else {
			mpl->m_len = ha->rx_buf_size;
			mpl->m_next = mpc;
			mpl = mpc;
		}

		ecore_chain_consume(&rxq->rx_bd_ring);
		rxq->sw_rx_cons =
			(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
	}

        QL_DPRINT7(ha, "[%d]: 2\n" "\tmpf = %p mpl = %p\n",
                  fp->rss_id, mpf, mpl);

	if (mpf != NULL) {
		mp = rxq->tpa_info[agg_index].mpl;
		mp->m_len = ha->rx_buf_size;
		mp->m_next = mpf;
		rxq->tpa_info[agg_index].mpl = mpl;
	}

	return;
}

static int
qlnx_tpa_end(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct qlnx_rx_queue *rxq,
	struct eth_fast_path_rx_tpa_end_cqe *cqe)
{
	struct sw_rx_data	*sw_rx_data;
	int			i;
	struct mbuf		*mpf = NULL, *mpl = NULL, *mpc = NULL;
	struct mbuf		*mp;
	uint32_t		agg_index;
	uint32_t		len = 0;
        struct ifnet		*ifp = ha->ifp;
	device_t		dev;

	dev = ha->pci_dev;

        QL_DPRINT7(ha, "[%d]: enter\n \
                \t type = 0x%x\n \
                \t tpa_agg_index = 0x%x\n \
                \t total_packet_len = 0x%x\n \
                \t num_of_bds = 0x%x\n \
                \t end_reason = 0x%x\n \
                \t num_of_coalesced_segs = 0x%x\n \
                \t ts_delta = 0x%x\n \
                \t len_list[0] = 0x%x\n \
                \t len_list[1] = 0x%x\n \
                \t len_list[2] = 0x%x\n \
                \t len_list[3] = 0x%x\n",
                 fp->rss_id, cqe->type, cqe->tpa_agg_index,
                cqe->total_packet_len, cqe->num_of_bds,
                cqe->end_reason, cqe->num_of_coalesced_segs, cqe->ts_delta,
                cqe->len_list[0], cqe->len_list[1], cqe->len_list[2],
                cqe->len_list[3]);

	agg_index = cqe->tpa_agg_index;

	if (agg_index >= ETH_TPA_MAX_AGGS_NUM) {

		QL_DPRINT7(ha, "[%d]: 0\n ", fp->rss_id);

		fp->err_rx_tpa_invalid_agg_num++;
		return (0);
	}


	for (i = 0; i < ETH_TPA_CQE_END_LEN_LIST_SIZE; i++) {

		QL_DPRINT7(ha, "[%d]: 1\n ", fp->rss_id);

		if (cqe->len_list[i] == 0)
			break;

		if (rxq->tpa_info[agg_index].agg_state != 
			QLNX_AGG_STATE_START) {

			QL_DPRINT7(ha, "[%d]: 2\n ", fp->rss_id);
	
			qlnx_reuse_rx_data(rxq);
			continue;
		}

		sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_cons];
		bus_dmamap_sync(ha->rx_tag, sw_rx_data->map,
			BUS_DMASYNC_POSTREAD);

		mpc = sw_rx_data->data;

		if (mpc == NULL) {

			QL_DPRINT7(ha, "[%d]: mpc = NULL\n", fp->rss_id);

			fp->err_rx_mp_null++;
			if (mpf != NULL)
				m_freem(mpf);
			mpf = mpl = NULL;
			rxq->tpa_info[agg_index].agg_state =
						QLNX_AGG_STATE_ERROR;
			ecore_chain_consume(&rxq->rx_bd_ring);
			rxq->sw_rx_cons =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
			continue;
		}

		if (qlnx_alloc_rx_buffer(ha, rxq) != 0) {
			QL_DPRINT7(ha, "[%d]: New buffer allocation failed,"
				" dropping incoming packet and reusing its"
				" buffer\n", fp->rss_id);

			qlnx_reuse_rx_data(rxq);

			if (mpf != NULL)
				m_freem(mpf);
			mpf = mpl = NULL;

			rxq->tpa_info[agg_index].agg_state =
						QLNX_AGG_STATE_ERROR;

			ecore_chain_consume(&rxq->rx_bd_ring);
			rxq->sw_rx_cons =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

			continue;
		}

		mpc->m_flags &= ~M_PKTHDR;
		mpc->m_next = NULL;
		mpc->m_len = cqe->len_list[i];


		if (mpf == NULL) {
			mpf = mpl = mpc;
		} else {
			mpl->m_len = ha->rx_buf_size;
			mpl->m_next = mpc;
			mpl = mpc;
		}

		ecore_chain_consume(&rxq->rx_bd_ring);
		rxq->sw_rx_cons =
			(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
	}

	QL_DPRINT7(ha, "[%d]: 5\n ", fp->rss_id);

	if (mpf != NULL) {

		QL_DPRINT7(ha, "[%d]: 6\n ", fp->rss_id);

		mp = rxq->tpa_info[agg_index].mpl;
		mp->m_len = ha->rx_buf_size;
		mp->m_next = mpf;
	}

	if (rxq->tpa_info[agg_index].agg_state != QLNX_AGG_STATE_START) {

		QL_DPRINT7(ha, "[%d]: 7\n ", fp->rss_id);

		if (rxq->tpa_info[agg_index].mpf != NULL)
			m_freem(rxq->tpa_info[agg_index].mpf);
		rxq->tpa_info[agg_index].mpf = NULL;
		rxq->tpa_info[agg_index].mpl = NULL;
		rxq->tpa_info[agg_index].agg_state = QLNX_AGG_STATE_NONE;
		return (0);
	}

	mp = rxq->tpa_info[agg_index].mpf;
	m_adj(mp, rxq->tpa_info[agg_index].placement_offset);
	mp->m_pkthdr.len = cqe->total_packet_len;

	if (mp->m_next  == NULL)
		mp->m_len = mp->m_pkthdr.len;
	else {
		/* compute the total packet length */
		mpf = mp;
		while (mpf != NULL) {
			len += mpf->m_len;
			mpf = mpf->m_next;
		}

		if (cqe->total_packet_len > len) {
			mpl = rxq->tpa_info[agg_index].mpl;
			mpl->m_len += (cqe->total_packet_len - len);
		}
	}

	QLNX_INC_IPACKETS(ifp);
	QLNX_INC_IBYTES(ifp, (cqe->total_packet_len));

        QL_DPRINT7(ha, "[%d]: 8 csum_data = 0x%x csum_flags = 0x%" PRIu64 "\n \
		m_len = 0x%x m_pkthdr_len = 0x%x\n",
                fp->rss_id, mp->m_pkthdr.csum_data,
                (uint64_t)mp->m_pkthdr.csum_flags, mp->m_len, mp->m_pkthdr.len);

	(*ifp->if_input)(ifp, mp);

	rxq->tpa_info[agg_index].mpf = NULL;
	rxq->tpa_info[agg_index].mpl = NULL;
	rxq->tpa_info[agg_index].agg_state = QLNX_AGG_STATE_NONE;

	return (cqe->num_of_coalesced_segs);
}

static int
qlnx_rx_int(qlnx_host_t *ha, struct qlnx_fastpath *fp, int budget,
	int lro_enable)
{
        uint16_t		hw_comp_cons, sw_comp_cons;
        int			rx_pkt = 0;
        struct qlnx_rx_queue	*rxq = fp->rxq;
        struct ifnet		*ifp = ha->ifp;
	struct ecore_dev	*cdev = &ha->cdev;
	struct ecore_hwfn       *p_hwfn;

#ifdef QLNX_SOFT_LRO
	struct lro_ctrl		*lro;

	lro = &rxq->lro;
#endif /* #ifdef QLNX_SOFT_LRO */

        hw_comp_cons = le16toh(*rxq->hw_cons_ptr);
        sw_comp_cons = ecore_chain_get_cons_idx(&rxq->rx_comp_ring);

	p_hwfn = &ha->cdev.hwfns[(fp->rss_id % cdev->num_hwfns)];

        /* Memory barrier to prevent the CPU from doing speculative reads of CQE
         * / BD in the while-loop before reading hw_comp_cons. If the CQE is
         * read before it is written by FW, then FW writes CQE and SB, and then
         * the CPU reads the hw_comp_cons, it will use an old CQE.
         */

        /* Loop to complete all indicated BDs */
        while (sw_comp_cons != hw_comp_cons) {
                union eth_rx_cqe		*cqe;
                struct eth_fast_path_rx_reg_cqe	*fp_cqe;
                struct sw_rx_data		*sw_rx_data;
		register struct mbuf		*mp;
                enum eth_rx_cqe_type		cqe_type;
                uint16_t			len, pad, len_on_first_bd;
                uint8_t				*data;
#if __FreeBSD_version >= 1100000
		uint8_t				hash_type;
#endif /* #if __FreeBSD_version >= 1100000 */

                /* Get the CQE from the completion ring */
                cqe = (union eth_rx_cqe *)
                        ecore_chain_consume(&rxq->rx_comp_ring);
                cqe_type = cqe->fast_path_regular.type;

                if (cqe_type == ETH_RX_CQE_TYPE_SLOW_PATH) {
                        QL_DPRINT3(ha, "Got a slowath CQE\n");

                        ecore_eth_cqe_completion(p_hwfn,
                                        (struct eth_slow_path_rx_cqe *)cqe);
                        goto next_cqe;
                }

		if (cqe_type != ETH_RX_CQE_TYPE_REGULAR) {

			switch (cqe_type) {

			case ETH_RX_CQE_TYPE_TPA_START:
				qlnx_tpa_start(ha, fp, rxq,
					&cqe->fast_path_tpa_start);
				fp->tpa_start++;
				break;

			case ETH_RX_CQE_TYPE_TPA_CONT:
				qlnx_tpa_cont(ha, fp, rxq,
					&cqe->fast_path_tpa_cont);
				fp->tpa_cont++;
				break;

			case ETH_RX_CQE_TYPE_TPA_END:
				rx_pkt += qlnx_tpa_end(ha, fp, rxq,
						&cqe->fast_path_tpa_end);
				fp->tpa_end++;
				break;

			default:
				break;
			}

                        goto next_cqe;
		}

                /* Get the data from the SW ring */
                sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_cons];
                mp = sw_rx_data->data;

		if (mp == NULL) {
                	QL_DPRINT1(ha, "mp = NULL\n");
			fp->err_rx_mp_null++;
        		rxq->sw_rx_cons  =
				(rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
			goto next_cqe;
		}
		bus_dmamap_sync(ha->rx_tag, sw_rx_data->map,
			BUS_DMASYNC_POSTREAD);

                /* non GRO */
                fp_cqe = &cqe->fast_path_regular;/* MK CR TPA check assembly */
                len =  le16toh(fp_cqe->pkt_len);
                pad = fp_cqe->placement_offset;
#if 0
		QL_DPRINT3(ha, "CQE type = %x, flags = %x, vlan = %x,"
			" len %u, parsing flags = %d pad  = %d\n",
			cqe_type, fp_cqe->bitfields,
			le16toh(fp_cqe->vlan_tag),
			len, le16toh(fp_cqe->pars_flags.flags), pad);
#endif
		data = mtod(mp, uint8_t *);
		data = data + pad;

		if (0)
			qlnx_dump_buf8(ha, __func__, data, len);

                /* For every Rx BD consumed, we allocate a new BD so the BD ring
                 * is always with a fixed size. If allocation fails, we take the
                 * consumed BD and return it to the ring in the PROD position.
                 * The packet that was received on that BD will be dropped (and
                 * not passed to the upper stack).
                 */
		/* If this is an error packet then drop it */
		if ((le16toh(cqe->fast_path_regular.pars_flags.flags)) &
			CQE_FLAGS_ERR) {

			QL_DPRINT1(ha, "CQE in CONS = %u has error, flags = %x,"
				" dropping incoming packet\n", sw_comp_cons,
			le16toh(cqe->fast_path_regular.pars_flags.flags));
			fp->err_rx_hw_errors++;

                        qlnx_reuse_rx_data(rxq);

			QLNX_INC_IERRORS(ifp);

			goto next_cqe;
		}

                if (qlnx_alloc_rx_buffer(ha, rxq) != 0) {

                        QL_DPRINT1(ha, "New buffer allocation failed, dropping"
				" incoming packet and reusing its buffer\n");
                        qlnx_reuse_rx_data(rxq);

                        fp->err_rx_alloc_errors++;

			QLNX_INC_IQDROPS(ifp);

                        goto next_cqe;
                }

                ecore_chain_consume(&rxq->rx_bd_ring);

		len_on_first_bd = fp_cqe->len_on_first_bd;
		m_adj(mp, pad);
		mp->m_pkthdr.len = len;

		if ((len > 60 ) && (len > len_on_first_bd)) {

			mp->m_len = len_on_first_bd;

			if (qlnx_rx_jumbo_chain(ha, fp, mp,
				(len - len_on_first_bd)) != 0) {

				m_freem(mp);

				QLNX_INC_IQDROPS(ifp);

                        	goto next_cqe;
			}

		} else if (len_on_first_bd < len) {
			fp->err_rx_jumbo_chain_pkts++;
		} else {
			mp->m_len = len;
		}

		mp->m_flags |= M_PKTHDR;

		/* assign packet to this interface interface */
		mp->m_pkthdr.rcvif = ifp;

		/* assume no hardware checksum has complated */
		mp->m_pkthdr.csum_flags = 0;

		mp->m_pkthdr.flowid = fp_cqe->rss_hash;

#if __FreeBSD_version >= 1100000

		hash_type = fp_cqe->bitfields &
				(ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE_MASK <<
				ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE_SHIFT);

		switch (hash_type) {

		case RSS_HASH_TYPE_IPV4:
			M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_IPV4);
			break;

		case RSS_HASH_TYPE_TCP_IPV4:
			M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_TCP_IPV4);
			break;

		case RSS_HASH_TYPE_IPV6:
			M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_IPV6);
			break;

		case RSS_HASH_TYPE_TCP_IPV6:
			M_HASHTYPE_SET(mp, M_HASHTYPE_RSS_TCP_IPV6);
			break;

		default:
			M_HASHTYPE_SET(mp, M_HASHTYPE_OPAQUE);
			break;
		}

#else
		mp->m_flags |= M_FLOWID;
#endif

		if (CQE_L3_PACKET(fp_cqe->pars_flags.flags)) {
			mp->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
		}

		if (!(CQE_IP_HDR_ERR(fp_cqe->pars_flags.flags))) {
			mp->m_pkthdr.csum_flags |= CSUM_IP_VALID;
		}

		if (CQE_L4_HAS_CSUM(fp_cqe->pars_flags.flags)) {
			mp->m_pkthdr.csum_data = 0xFFFF;
			mp->m_pkthdr.csum_flags |=
				(CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
		}

		if (CQE_HAS_VLAN(fp_cqe->pars_flags.flags)) {
			mp->m_pkthdr.ether_vtag = le16toh(fp_cqe->vlan_tag);
			mp->m_flags |= M_VLANTAG;
		}

		QLNX_INC_IPACKETS(ifp);
		QLNX_INC_IBYTES(ifp, len);

#ifdef QLNX_SOFT_LRO

		if (lro_enable) {

#if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO)

			tcp_lro_queue_mbuf(lro, mp);

#else

			if (tcp_lro_rx(lro, mp, 0))
				(*ifp->if_input)(ifp, mp);

#endif /* #if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO) */

		} else {
			(*ifp->if_input)(ifp, mp);
		}
#else

		(*ifp->if_input)(ifp, mp);

#endif /* #ifdef QLNX_SOFT_LRO */

                rx_pkt++;

        	rxq->sw_rx_cons  = (rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);

next_cqe:	/* don't consume bd rx buffer */
                ecore_chain_recycle_consumed(&rxq->rx_comp_ring);
                sw_comp_cons = ecore_chain_get_cons_idx(&rxq->rx_comp_ring);

		/* CR TPA - revisit how to handle budget in TPA perhaps
		   increase on "end" */
                if (rx_pkt == budget)
                        break;
        } /* repeat while sw_comp_cons != hw_comp_cons... */

        /* Update producers */
        qlnx_update_rx_prod(p_hwfn, rxq);

        return rx_pkt;
}


/*
 * fast path interrupt
 */

static void
qlnx_fp_isr(void *arg)
{
        qlnx_ivec_t		*ivec = arg;
        qlnx_host_t		*ha;
        struct qlnx_fastpath	*fp = NULL;
        int			idx;

        ha = ivec->ha;

        if (ha->state != QLNX_STATE_OPEN) {
                return;
        }

        idx = ivec->rss_idx;

        if ((idx = ivec->rss_idx) >= ha->num_rss) {
                QL_DPRINT1(ha, "illegal interrupt[%d]\n", idx);
                ha->err_illegal_intr++;
                return;
        }
        fp = &ha->fp_array[idx];

        if (fp == NULL) {
                ha->err_fp_null++;
        } else {
		int			rx_int = 0, total_rx_count = 0;
		int 			lro_enable, tc;
		struct qlnx_tx_queue	*txq;
		uint16_t		elem_left;

		lro_enable = ha->ifp->if_capenable & IFCAP_LRO;

                ecore_sb_ack(fp->sb_info, IGU_INT_DISABLE, 0);

                do {
                        for (tc = 0; tc < ha->num_tc; tc++) {

				txq = fp->txq[tc];

				if((int)(elem_left =
					ecore_chain_get_elem_left(&txq->tx_pbl)) <
						QLNX_TX_ELEM_THRESH)  {

                                	if (mtx_trylock(&fp->tx_mtx)) {
#ifdef QLNX_TRACE_PERF_DATA
						tx_compl = fp->tx_pkts_completed;
#endif

						qlnx_tx_int(ha, fp, fp->txq[tc]);
#ifdef QLNX_TRACE_PERF_DATA
						fp->tx_pkts_compl_intr +=
							(fp->tx_pkts_completed - tx_compl);
						if ((fp->tx_pkts_completed - tx_compl) <= 32)
							fp->tx_comInt[0]++;
						else if (((fp->tx_pkts_completed - tx_compl) > 32) &&
							((fp->tx_pkts_completed - tx_compl) <= 64))
							fp->tx_comInt[1]++;
						else if(((fp->tx_pkts_completed - tx_compl) > 64) &&
							((fp->tx_pkts_completed - tx_compl) <= 128))
							fp->tx_comInt[2]++;
						else if(((fp->tx_pkts_completed - tx_compl) > 128))
							fp->tx_comInt[3]++;
#endif
						mtx_unlock(&fp->tx_mtx);
					}
				}
                        }

                        rx_int = qlnx_rx_int(ha, fp, ha->rx_pkt_threshold,
                                        lro_enable);

                        if (rx_int) {
                                fp->rx_pkts += rx_int;
                                total_rx_count += rx_int;
                        }

                } while (rx_int);

#ifdef QLNX_SOFT_LRO
                {
                        struct lro_ctrl *lro;

                        lro = &fp->rxq->lro;

                        if (lro_enable && total_rx_count) {

#if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO)

#ifdef QLNX_TRACE_LRO_CNT
                                if (lro->lro_mbuf_count & ~1023)
                                        fp->lro_cnt_1024++;
                                else if (lro->lro_mbuf_count & ~511)
                                        fp->lro_cnt_512++;
                                else if (lro->lro_mbuf_count & ~255)
                                        fp->lro_cnt_256++;
                                else if (lro->lro_mbuf_count & ~127)
                                        fp->lro_cnt_128++;
                                else if (lro->lro_mbuf_count & ~63)
                                        fp->lro_cnt_64++;
#endif /* #ifdef QLNX_TRACE_LRO_CNT */

                                tcp_lro_flush_all(lro);

#else
                                struct lro_entry *queued;

                                while ((!SLIST_EMPTY(&lro->lro_active))) {
                                        queued = SLIST_FIRST(&lro->lro_active);
                                        SLIST_REMOVE_HEAD(&lro->lro_active, \
                                                next);
                                        tcp_lro_flush(lro, queued);
                                }
#endif /* #if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO) */
                        }
                }
#endif /* #ifdef QLNX_SOFT_LRO */

                ecore_sb_update_sb_idx(fp->sb_info);
                rmb();
                ecore_sb_ack(fp->sb_info, IGU_INT_ENABLE, 1);
        }

        return;
}


/*
 * slow path interrupt processing function
 * can be invoked in polled mode or in interrupt mode via taskqueue.
 */
void
qlnx_sp_isr(void *arg)
{
	struct ecore_hwfn	*p_hwfn;
	qlnx_host_t		*ha;
	
	p_hwfn = arg;

	ha = (qlnx_host_t *)p_hwfn->p_dev;

	ha->sp_interrupts++;

	QL_DPRINT2(ha, "enter\n");

	ecore_int_sp_dpc(p_hwfn);

	QL_DPRINT2(ha, "exit\n");
	
	return;
}

/*****************************************************************************
 * Support Functions for DMA'able Memory
 *****************************************************************************/

static void
qlnx_dmamap_callback(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
        *((bus_addr_t *)arg) = 0;

        if (error) {
                printf("%s: bus_dmamap_load failed (%d)\n", __func__, error);
                return;
        }

        *((bus_addr_t *)arg) = segs[0].ds_addr;

        return;
}

static int
qlnx_alloc_dmabuf(qlnx_host_t *ha, qlnx_dma_t *dma_buf)
{
        int             ret = 0;
        device_t        dev;
        bus_addr_t      b_addr;

        dev = ha->pci_dev;

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
                QL_DPRINT1(ha, "could not create dma tag\n");
                goto qlnx_alloc_dmabuf_exit;
        }
        ret = bus_dmamem_alloc(dma_buf->dma_tag,
                        (void **)&dma_buf->dma_b,
                        (BUS_DMA_ZERO | BUS_DMA_COHERENT | BUS_DMA_NOWAIT),
                        &dma_buf->dma_map);
        if (ret) {
                bus_dma_tag_destroy(dma_buf->dma_tag);
                QL_DPRINT1(ha, "bus_dmamem_alloc failed\n");
                goto qlnx_alloc_dmabuf_exit;
        }

        ret = bus_dmamap_load(dma_buf->dma_tag,
                        dma_buf->dma_map,
                        dma_buf->dma_b,
                        dma_buf->size,
                        qlnx_dmamap_callback,
                        &b_addr, BUS_DMA_NOWAIT);

        if (ret || !b_addr) {
                bus_dma_tag_destroy(dma_buf->dma_tag);
                bus_dmamem_free(dma_buf->dma_tag, dma_buf->dma_b,
                        dma_buf->dma_map);
                ret = -1;
                goto qlnx_alloc_dmabuf_exit;
        }

        dma_buf->dma_addr = b_addr;

qlnx_alloc_dmabuf_exit:

        return ret;
}

static void
qlnx_free_dmabuf(qlnx_host_t *ha, qlnx_dma_t *dma_buf)
{
	bus_dmamap_unload(dma_buf->dma_tag, dma_buf->dma_map);
        bus_dmamem_free(dma_buf->dma_tag, dma_buf->dma_b, dma_buf->dma_map);
        bus_dma_tag_destroy(dma_buf->dma_tag);
	return;
}

void *
qlnx_dma_alloc_coherent(void *ecore_dev, bus_addr_t *phys, uint32_t size)
{
	qlnx_dma_t	dma_buf;
	qlnx_dma_t	*dma_p;
	qlnx_host_t	*ha;
	device_t        dev;

	ha = (qlnx_host_t *)ecore_dev;
	dev = ha->pci_dev;

	size = (size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);

	memset(&dma_buf, 0, sizeof (qlnx_dma_t));

	dma_buf.size = size + PAGE_SIZE;
	dma_buf.alignment = 8;

	if (qlnx_alloc_dmabuf((qlnx_host_t *)ecore_dev, &dma_buf) != 0)
		return (NULL);
	bzero((uint8_t *)dma_buf.dma_b, dma_buf.size);

	*phys = dma_buf.dma_addr;

	dma_p = (qlnx_dma_t *)((uint8_t *)dma_buf.dma_b + size);

	memcpy(dma_p, &dma_buf, sizeof(qlnx_dma_t));

	QL_DPRINT5(ha, "[%p %p %p %p 0x%08x ]\n",
		(void *)dma_buf.dma_map, (void *)dma_buf.dma_tag,
		dma_buf.dma_b, (void *)dma_buf.dma_addr, size);

	return (dma_buf.dma_b);
}

void
qlnx_dma_free_coherent(void *ecore_dev, void *v_addr, bus_addr_t phys,
	uint32_t size)
{
	qlnx_dma_t dma_buf, *dma_p;
	qlnx_host_t	*ha;
	device_t        dev;

	ha = (qlnx_host_t *)ecore_dev;
	dev = ha->pci_dev;

	if (v_addr == NULL)
		return;

	size = (size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);

	dma_p = (qlnx_dma_t *)((uint8_t *)v_addr + size);

	QL_DPRINT5(ha, "[%p %p %p %p 0x%08x ]\n",
		(void *)dma_p->dma_map, (void *)dma_p->dma_tag,
		dma_p->dma_b, (void *)dma_p->dma_addr, size);

	dma_buf = *dma_p;

	if (!ha->qlnxr_debug)
	qlnx_free_dmabuf((qlnx_host_t *)ecore_dev, &dma_buf);
	return;
}

static int
qlnx_alloc_parent_dma_tag(qlnx_host_t *ha)
{
        int             ret;
        device_t        dev;

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
                QL_DPRINT1(ha, "could not create parent dma tag\n");
                return (-1);
        }

        ha->flags.parent_tag = 1;

        return (0);
}

static void
qlnx_free_parent_dma_tag(qlnx_host_t *ha)
{
        if (ha->parent_tag != NULL) {
                bus_dma_tag_destroy(ha->parent_tag);
		ha->parent_tag = NULL;
        }
	return;
}

static int
qlnx_alloc_tx_dma_tag(qlnx_host_t *ha)
{
        if (bus_dma_tag_create(NULL,    /* parent */
                1, 0,    /* alignment, bounds */
                BUS_SPACE_MAXADDR,       /* lowaddr */
                BUS_SPACE_MAXADDR,       /* highaddr */
                NULL, NULL,      /* filter, filterarg */
                QLNX_MAX_TSO_FRAME_SIZE,     /* maxsize */
                QLNX_MAX_SEGMENTS,        /* nsegments */
                QLNX_MAX_TX_MBUF_SIZE,	  /* maxsegsize */
                0,        /* flags */
                NULL,    /* lockfunc */
                NULL,    /* lockfuncarg */
                &ha->tx_tag)) {

                QL_DPRINT1(ha, "tx_tag alloc failed\n");
                return (-1);
        }

	return (0);
}

static void
qlnx_free_tx_dma_tag(qlnx_host_t *ha)
{
        if (ha->tx_tag != NULL) {
                bus_dma_tag_destroy(ha->tx_tag);
		ha->tx_tag = NULL;
        }
	return;
}

static int
qlnx_alloc_rx_dma_tag(qlnx_host_t *ha)
{
        if (bus_dma_tag_create(NULL,    /* parent */
                        1, 0,    /* alignment, bounds */
                        BUS_SPACE_MAXADDR,       /* lowaddr */
                        BUS_SPACE_MAXADDR,       /* highaddr */
                        NULL, NULL,      /* filter, filterarg */
                        MJUM9BYTES,     /* maxsize */
                        1,        /* nsegments */
                        MJUM9BYTES,        /* maxsegsize */
                        0,        /* flags */
                        NULL,    /* lockfunc */
                        NULL,    /* lockfuncarg */
                        &ha->rx_tag)) {

                QL_DPRINT1(ha, " rx_tag alloc failed\n");

                return (-1);
        }
	return (0);
}

static void
qlnx_free_rx_dma_tag(qlnx_host_t *ha)
{
        if (ha->rx_tag != NULL) {
                bus_dma_tag_destroy(ha->rx_tag);
		ha->rx_tag = NULL;
        }
	return;
}

/*********************************
 * Exported functions
 *********************************/
uint32_t
qlnx_pci_bus_get_bar_size(void *ecore_dev, uint8_t bar_id)
{
	uint32_t bar_size;

	bar_id = bar_id * 2;

	bar_size = bus_get_resource_count(((qlnx_host_t *)ecore_dev)->pci_dev,
				SYS_RES_MEMORY,
				PCIR_BAR(bar_id));

	return (bar_size);
}

uint32_t
qlnx_pci_read_config_byte(void *ecore_dev, uint32_t pci_reg, uint8_t *reg_value)
{
	*reg_value = pci_read_config(((qlnx_host_t *)ecore_dev)->pci_dev,
				pci_reg, 1);
	return 0;
}

uint32_t
qlnx_pci_read_config_word(void *ecore_dev, uint32_t pci_reg,
	uint16_t *reg_value)
{
	*reg_value = pci_read_config(((qlnx_host_t *)ecore_dev)->pci_dev,
				pci_reg, 2);
	return 0;
}

uint32_t
qlnx_pci_read_config_dword(void *ecore_dev, uint32_t pci_reg,
	uint32_t *reg_value)
{
	*reg_value = pci_read_config(((qlnx_host_t *)ecore_dev)->pci_dev,
				pci_reg, 4);
	return 0;
}

void
qlnx_pci_write_config_byte(void *ecore_dev, uint32_t pci_reg, uint8_t reg_value)
{
	pci_write_config(((qlnx_host_t *)ecore_dev)->pci_dev,
		pci_reg, reg_value, 1);
	return;
}

void
qlnx_pci_write_config_word(void *ecore_dev, uint32_t pci_reg,
	uint16_t reg_value)
{
	pci_write_config(((qlnx_host_t *)ecore_dev)->pci_dev,
		pci_reg, reg_value, 2);
	return;
}

void
qlnx_pci_write_config_dword(void *ecore_dev, uint32_t pci_reg,
	uint32_t reg_value)
{
	pci_write_config(((qlnx_host_t *)ecore_dev)->pci_dev,
		pci_reg, reg_value, 4);
	return;
}

int
qlnx_pci_find_capability(void *ecore_dev, int cap)
{
	int		reg;
	qlnx_host_t	*ha;

	ha = ecore_dev;

	if (pci_find_cap(ha->pci_dev, PCIY_EXPRESS, &reg) == 0)
		return reg;
	else {
		QL_DPRINT1(ha, "failed\n");
		return 0;
	}
}

int
qlnx_pci_find_ext_capability(void *ecore_dev, int ext_cap)
{
	int		reg;
	qlnx_host_t	*ha;

	ha = ecore_dev;

	if (pci_find_extcap(ha->pci_dev, ext_cap, &reg) == 0)
		return reg;
	else {
		QL_DPRINT1(ha, "failed\n");
		return 0;
	}
}

uint32_t
qlnx_reg_rd32(void *hwfn, uint32_t reg_addr)
{
	uint32_t		data32;
	struct ecore_hwfn	*p_hwfn;

	p_hwfn = hwfn;

	data32 = bus_read_4(((qlnx_host_t *)p_hwfn->p_dev)->pci_reg, \
			(bus_size_t)(p_hwfn->reg_offset + reg_addr));

	return (data32);
}

void
qlnx_reg_wr32(void *hwfn, uint32_t reg_addr, uint32_t value)
{
	struct ecore_hwfn	*p_hwfn = hwfn;

	bus_write_4(((qlnx_host_t *)p_hwfn->p_dev)->pci_reg, \
		(bus_size_t)(p_hwfn->reg_offset + reg_addr), value);

	return;
}

void
qlnx_reg_wr16(void *hwfn, uint32_t reg_addr, uint16_t value)
{
	struct ecore_hwfn	*p_hwfn = hwfn;
	
	bus_write_2(((qlnx_host_t *)p_hwfn->p_dev)->pci_reg, \
		(bus_size_t)(p_hwfn->reg_offset + reg_addr), value);
	return;
}

void
qlnx_dbell_wr32_db(void *hwfn, void *reg_addr, uint32_t value)
{
	struct ecore_dev	*cdev;
	struct ecore_hwfn	*p_hwfn;
	uint32_t	offset;

	p_hwfn = hwfn;

	cdev = p_hwfn->p_dev;

	offset = (uint32_t)((uint8_t *)reg_addr - (uint8_t *)(p_hwfn->doorbells));
	bus_write_4(((qlnx_host_t *)cdev)->pci_dbells, offset, value);

	return;
}

void
qlnx_dbell_wr32(void *hwfn, uint32_t reg_addr, uint32_t value)
{
	struct ecore_hwfn	*p_hwfn = hwfn;

	bus_write_4(((qlnx_host_t *)p_hwfn->p_dev)->pci_dbells, \
		(bus_size_t)(p_hwfn->db_offset + reg_addr), value);

	return;
}

uint32_t
qlnx_direct_reg_rd32(void *p_hwfn, uint32_t *reg_addr)
{
	uint32_t		data32;
	bus_size_t		offset;
	struct ecore_dev	*cdev;

	cdev = ((struct ecore_hwfn *)p_hwfn)->p_dev;
	offset = (bus_size_t)((uint8_t *)reg_addr - (uint8_t *)(cdev->regview));

	data32 = bus_read_4(((qlnx_host_t *)cdev)->pci_reg, offset);

	return (data32);
}

void
qlnx_direct_reg_wr32(void *p_hwfn, void *reg_addr, uint32_t value)
{
	bus_size_t		offset;
	struct ecore_dev	*cdev;

	cdev = ((struct ecore_hwfn *)p_hwfn)->p_dev;
	offset = (bus_size_t)((uint8_t *)reg_addr - (uint8_t *)(cdev->regview));

	bus_write_4(((qlnx_host_t *)cdev)->pci_reg, offset, value);

	return;
}

void
qlnx_direct_reg_wr64(void *p_hwfn, void *reg_addr, uint64_t value)
{
	bus_size_t		offset;
	struct ecore_dev	*cdev;

	cdev = ((struct ecore_hwfn *)p_hwfn)->p_dev;
	offset = (bus_size_t)((uint8_t *)reg_addr - (uint8_t *)(cdev->regview));

	bus_write_8(((qlnx_host_t *)cdev)->pci_reg, offset, value);
	return;
}

void *
qlnx_zalloc(uint32_t size)
{
	caddr_t	va;

	va = malloc((unsigned long)size, M_QLNXBUF, M_NOWAIT);
	bzero(va, size);
	return ((void *)va);
}

void
qlnx_barrier(void *p_hwfn)
{
	qlnx_host_t	*ha;

	ha = (qlnx_host_t *)((struct ecore_hwfn *)p_hwfn)->p_dev;
	bus_barrier(ha->pci_reg,  0, 0, BUS_SPACE_BARRIER_WRITE);
}

void
qlnx_link_update(void *p_hwfn)
{
	qlnx_host_t	*ha;
	int		prev_link_state;

	ha = (qlnx_host_t *)((struct ecore_hwfn *)p_hwfn)->p_dev;

	qlnx_fill_link(ha, p_hwfn, &ha->if_link);

	prev_link_state = ha->link_up;
	ha->link_up = ha->if_link.link_up;

        if (prev_link_state !=  ha->link_up) {
                if (ha->link_up) {
                        if_link_state_change(ha->ifp, LINK_STATE_UP);
                } else {
                        if_link_state_change(ha->ifp, LINK_STATE_DOWN);
                }
        }
#ifndef QLNX_VF
#ifdef CONFIG_ECORE_SRIOV

	if (qlnx_vf_device(ha) != 0) {
		if (ha->sriov_initialized)
			qlnx_inform_vf_link_state(p_hwfn, ha);
	}

#endif /* #ifdef CONFIG_ECORE_SRIOV */
#endif /* #ifdef QLNX_VF */

        return;
}

static void
__qlnx_osal_vf_fill_acquire_resc_req(struct ecore_hwfn *p_hwfn,
	struct ecore_vf_acquire_sw_info *p_sw_info)
{
	p_sw_info->driver_version = (QLNX_VERSION_MAJOR << 24) |
					(QLNX_VERSION_MINOR << 16) |
					 QLNX_VERSION_BUILD;
	p_sw_info->os_type = VFPF_ACQUIRE_OS_FREEBSD;

	return;
}

void
qlnx_osal_vf_fill_acquire_resc_req(void *p_hwfn, void *p_resc_req,
	void *p_sw_info)
{
	__qlnx_osal_vf_fill_acquire_resc_req(p_hwfn, p_sw_info);

	return;
}

void
qlnx_fill_link(qlnx_host_t *ha, struct ecore_hwfn *hwfn,
	struct qlnx_link_output *if_link)
{
	struct ecore_mcp_link_params    link_params;
	struct ecore_mcp_link_state     link_state;
	uint8_t				p_change;
	struct ecore_ptt *p_ptt = NULL;


	memset(if_link, 0, sizeof(*if_link));
	memset(&link_params, 0, sizeof(struct ecore_mcp_link_params));
	memset(&link_state, 0, sizeof(struct ecore_mcp_link_state));

	ha = (qlnx_host_t *)hwfn->p_dev;

	/* Prepare source inputs */
	/* we only deal with physical functions */
	if (qlnx_vf_device(ha) != 0) {

        	p_ptt = ecore_ptt_acquire(hwfn);

	        if (p_ptt == NULL) {
			QL_DPRINT1(ha, "ecore_ptt_acquire failed\n");
			return;
		}

		ecore_mcp_get_media_type(hwfn, p_ptt, &if_link->media_type);
		ecore_ptt_release(hwfn, p_ptt);

		memcpy(&link_params, ecore_mcp_get_link_params(hwfn),
			sizeof(link_params));
		memcpy(&link_state, ecore_mcp_get_link_state(hwfn),
			sizeof(link_state));
	} else {
		ecore_mcp_get_media_type(hwfn, NULL, &if_link->media_type);
		ecore_vf_read_bulletin(hwfn, &p_change);
		ecore_vf_get_link_params(hwfn, &link_params);
		ecore_vf_get_link_state(hwfn, &link_state);
	}

	/* Set the link parameters to pass to protocol driver */
	if (link_state.link_up) {
		if_link->link_up = true;
		if_link->speed = link_state.speed;
	}

	if_link->supported_caps = QLNX_LINK_CAP_FIBRE;

	if (link_params.speed.autoneg)
		if_link->supported_caps |= QLNX_LINK_CAP_Autoneg;

	if (link_params.pause.autoneg ||
		(link_params.pause.forced_rx && link_params.pause.forced_tx))
		if_link->supported_caps |= QLNX_LINK_CAP_Asym_Pause;

	if (link_params.pause.autoneg || link_params.pause.forced_rx ||
		link_params.pause.forced_tx)
		if_link->supported_caps |= QLNX_LINK_CAP_Pause;

	if (link_params.speed.advertised_speeds &
		NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G)
		if_link->supported_caps |= QLNX_LINK_CAP_1000baseT_Half |
                                           QLNX_LINK_CAP_1000baseT_Full;

	if (link_params.speed.advertised_speeds &
		NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G)
		if_link->supported_caps |= QLNX_LINK_CAP_10000baseKR_Full;

	if (link_params.speed.advertised_speeds &
		NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G)
		if_link->supported_caps |= QLNX_LINK_CAP_25000baseKR_Full;

	if (link_params.speed.advertised_speeds &
		NVM_CFG1_PORT_DRV_LINK_SPEED_40G)
		if_link->supported_caps |= QLNX_LINK_CAP_40000baseLR4_Full;

	if (link_params.speed.advertised_speeds &
		NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G)
		if_link->supported_caps |= QLNX_LINK_CAP_50000baseKR2_Full;

	if (link_params.speed.advertised_speeds &
		NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G)
		if_link->supported_caps |= QLNX_LINK_CAP_100000baseKR4_Full;

	if_link->advertised_caps = if_link->supported_caps;

	if_link->autoneg = link_params.speed.autoneg;
	if_link->duplex = QLNX_LINK_DUPLEX;

	/* Link partner capabilities */

	if (link_state.partner_adv_speed & ECORE_LINK_PARTNER_SPEED_1G_HD)
		if_link->link_partner_caps |= QLNX_LINK_CAP_1000baseT_Half;

	if (link_state.partner_adv_speed & ECORE_LINK_PARTNER_SPEED_1G_FD)
		if_link->link_partner_caps |= QLNX_LINK_CAP_1000baseT_Full;

	if (link_state.partner_adv_speed & ECORE_LINK_PARTNER_SPEED_10G)
		if_link->link_partner_caps |= QLNX_LINK_CAP_10000baseKR_Full;

	if (link_state.partner_adv_speed & ECORE_LINK_PARTNER_SPEED_25G)
		if_link->link_partner_caps |= QLNX_LINK_CAP_25000baseKR_Full;

	if (link_state.partner_adv_speed & ECORE_LINK_PARTNER_SPEED_40G)
		if_link->link_partner_caps |= QLNX_LINK_CAP_40000baseLR4_Full;

	if (link_state.partner_adv_speed & ECORE_LINK_PARTNER_SPEED_50G)
		if_link->link_partner_caps |= QLNX_LINK_CAP_50000baseKR2_Full;

	if (link_state.partner_adv_speed & ECORE_LINK_PARTNER_SPEED_100G)
		if_link->link_partner_caps |= QLNX_LINK_CAP_100000baseKR4_Full;

	if (link_state.an_complete)
		if_link->link_partner_caps |= QLNX_LINK_CAP_Autoneg;

	if (link_state.partner_adv_pause)
		if_link->link_partner_caps |= QLNX_LINK_CAP_Pause;

	if ((link_state.partner_adv_pause ==
		ECORE_LINK_PARTNER_ASYMMETRIC_PAUSE) ||
		(link_state.partner_adv_pause ==
			ECORE_LINK_PARTNER_BOTH_PAUSE))
		if_link->link_partner_caps |= QLNX_LINK_CAP_Asym_Pause;

	return;
}

void
qlnx_schedule_recovery(void *p_hwfn)
{
	qlnx_host_t	*ha;

	ha = (qlnx_host_t *)((struct ecore_hwfn *)p_hwfn)->p_dev;

	if (qlnx_vf_device(ha) != 0) {
		taskqueue_enqueue(ha->err_taskqueue, &ha->err_task);
	}

	return;
}

static int
qlnx_nic_setup(struct ecore_dev *cdev, struct ecore_pf_params *func_params)
{
        int	rc, i;

        for (i = 0; i < cdev->num_hwfns; i++) {
                struct ecore_hwfn *p_hwfn = &cdev->hwfns[i];
                p_hwfn->pf_params = *func_params;

#ifdef QLNX_ENABLE_IWARP
		if (qlnx_vf_device((qlnx_host_t *)cdev) != 0) {
			p_hwfn->using_ll2 = true;
		}
#endif /* #ifdef QLNX_ENABLE_IWARP */

        }

        rc = ecore_resc_alloc(cdev);
        if (rc)
                goto qlnx_nic_setup_exit;

        ecore_resc_setup(cdev);

qlnx_nic_setup_exit:

        return rc;
}

static int
qlnx_nic_start(struct ecore_dev *cdev)
{
        int				rc;
	struct ecore_hw_init_params	params;

	bzero(&params, sizeof (struct ecore_hw_init_params));

	params.p_tunn = NULL;
	params.b_hw_start = true;
	params.int_mode = cdev->int_mode;
	params.allow_npar_tx_switch = true;
	params.bin_fw_data = NULL;

        rc = ecore_hw_init(cdev, &params);
        if (rc) {
                ecore_resc_free(cdev);
                return rc;
        }

        return 0;
}

static int
qlnx_slowpath_start(qlnx_host_t *ha)
{
	struct ecore_dev	*cdev;
	struct ecore_pf_params	pf_params;
	int			rc;

	memset(&pf_params, 0, sizeof(struct ecore_pf_params));
	pf_params.eth_pf_params.num_cons  =
		(ha->num_rss) * (ha->num_tc + 1);

#ifdef QLNX_ENABLE_IWARP
	if (qlnx_vf_device(ha) != 0) {
		if(ha->personality == ECORE_PCI_ETH_IWARP) {
			device_printf(ha->pci_dev, "setting parameters required by iWARP dev\n");	
			pf_params.rdma_pf_params.num_qps = 1024;
			pf_params.rdma_pf_params.num_srqs = 1024;
			pf_params.rdma_pf_params.gl_pi = ECORE_ROCE_PROTOCOL_INDEX;
			pf_params.rdma_pf_params.rdma_protocol = ECORE_RDMA_PROTOCOL_IWARP;
		} else if(ha->personality == ECORE_PCI_ETH_ROCE) {
			device_printf(ha->pci_dev, "setting parameters required by RoCE dev\n");	
			pf_params.rdma_pf_params.num_qps = 8192;
			pf_params.rdma_pf_params.num_srqs = 8192;
			//pf_params.rdma_pf_params.min_dpis = 0;
			pf_params.rdma_pf_params.min_dpis = 8;
			pf_params.rdma_pf_params.roce_edpm_mode = 0;
			pf_params.rdma_pf_params.gl_pi = ECORE_ROCE_PROTOCOL_INDEX;
			pf_params.rdma_pf_params.rdma_protocol = ECORE_RDMA_PROTOCOL_ROCE;
		}
	}
#endif /* #ifdef QLNX_ENABLE_IWARP */

	cdev = &ha->cdev;

	rc = qlnx_nic_setup(cdev, &pf_params);
        if (rc)
                goto qlnx_slowpath_start_exit;

        cdev->int_mode = ECORE_INT_MODE_MSIX;
        cdev->int_coalescing_mode = ECORE_COAL_MODE_ENABLE;

#ifdef QLNX_MAX_COALESCE
	cdev->rx_coalesce_usecs = 255;
	cdev->tx_coalesce_usecs = 255;
#endif

	rc = qlnx_nic_start(cdev);

	ha->rx_coalesce_usecs = cdev->rx_coalesce_usecs;
	ha->tx_coalesce_usecs = cdev->tx_coalesce_usecs;

#ifdef QLNX_USER_LLDP
	(void)qlnx_set_lldp_tlvx(ha, NULL);
#endif /* #ifdef QLNX_USER_LLDP */

qlnx_slowpath_start_exit:

	return (rc);
}

static int
qlnx_slowpath_stop(qlnx_host_t *ha)
{
	struct ecore_dev	*cdev;
	device_t		dev = ha->pci_dev;
	int			i;

	cdev = &ha->cdev;

	ecore_hw_stop(cdev);

 	for (i = 0; i < ha->cdev.num_hwfns; i++) {

        	if (ha->sp_handle[i])
                	(void)bus_teardown_intr(dev, ha->sp_irq[i],
				ha->sp_handle[i]);

		ha->sp_handle[i] = NULL;

        	if (ha->sp_irq[i])
			(void) bus_release_resource(dev, SYS_RES_IRQ,
				ha->sp_irq_rid[i], ha->sp_irq[i]);
		ha->sp_irq[i] = NULL;
	}

        ecore_resc_free(cdev);

        return 0;
}

static void
qlnx_set_id(struct ecore_dev *cdev, char name[NAME_SIZE],
	char ver_str[VER_SIZE])
{
        int	i;

        memcpy(cdev->name, name, NAME_SIZE);

        for_each_hwfn(cdev, i) {
                snprintf(cdev->hwfns[i].name, NAME_SIZE, "%s-%d", name, i);
        }

        cdev->drv_type = DRV_ID_DRV_TYPE_FREEBSD;

	return ;
}

void
qlnx_get_protocol_stats(void *cdev, int proto_type, void *proto_stats)
{
	enum ecore_mcp_protocol_type	type;
	union ecore_mcp_protocol_stats	*stats;
	struct ecore_eth_stats		eth_stats;
	qlnx_host_t			*ha;

	ha = cdev;
	stats = proto_stats;
	type = proto_type;

        switch (type) {

        case ECORE_MCP_LAN_STATS:
                ecore_get_vport_stats((struct ecore_dev *)cdev, &eth_stats);
                stats->lan_stats.ucast_rx_pkts = eth_stats.common.rx_ucast_pkts;
                stats->lan_stats.ucast_tx_pkts = eth_stats.common.tx_ucast_pkts;
                stats->lan_stats.fcs_err = -1;
                break;

	default:
		ha->err_get_proto_invalid_type++;

		QL_DPRINT1(ha, "invalid protocol type 0x%x\n", type);
		break;
	}
	return;
}

static int
qlnx_get_mfw_version(qlnx_host_t *ha, uint32_t *mfw_ver)
{
	struct ecore_hwfn	*p_hwfn;
	struct ecore_ptt	*p_ptt;

	p_hwfn = &ha->cdev.hwfns[0];
	p_ptt = ecore_ptt_acquire(p_hwfn);

	if (p_ptt ==  NULL) {
                QL_DPRINT1(ha, "ecore_ptt_acquire failed\n");
                return (-1);
	}
	ecore_mcp_get_mfw_ver(p_hwfn, p_ptt, mfw_ver, NULL);
	
	ecore_ptt_release(p_hwfn, p_ptt);

	return (0);
}

static int
qlnx_get_flash_size(qlnx_host_t *ha, uint32_t *flash_size)
{
	struct ecore_hwfn	*p_hwfn;
	struct ecore_ptt	*p_ptt;

	p_hwfn = &ha->cdev.hwfns[0];
	p_ptt = ecore_ptt_acquire(p_hwfn);

	if (p_ptt ==  NULL) {
                QL_DPRINT1(ha,"ecore_ptt_acquire failed\n");
                return (-1);
	}
	ecore_mcp_get_flash_size(p_hwfn, p_ptt, flash_size);
	
	ecore_ptt_release(p_hwfn, p_ptt);

	return (0);
}

static int
qlnx_alloc_mem_arrays(qlnx_host_t *ha)
{
	struct ecore_dev	*cdev;

	cdev = &ha->cdev;

	bzero(&ha->txq_array[0], (sizeof(struct qlnx_tx_queue) * QLNX_MAX_RSS));
	bzero(&ha->rxq_array[0], (sizeof(struct qlnx_rx_queue) * QLNX_MAX_RSS));
	bzero(&ha->sb_array[0], (sizeof(struct ecore_sb_info) * QLNX_MAX_RSS));

        return 0;
}

static void
qlnx_init_fp(qlnx_host_t *ha)
{
	int rss_id, txq_array_index, tc;

	for (rss_id = 0; rss_id < ha->num_rss; rss_id++) {

		struct qlnx_fastpath *fp = &ha->fp_array[rss_id];

		fp->rss_id = rss_id;
		fp->edev = ha;
		fp->sb_info = &ha->sb_array[rss_id];
		fp->rxq = &ha->rxq_array[rss_id];
		fp->rxq->rxq_id = rss_id;

		for (tc = 0; tc < ha->num_tc; tc++) {
                        txq_array_index = tc * ha->num_rss + rss_id;
                        fp->txq[tc] = &ha->txq_array[txq_array_index];
                        fp->txq[tc]->index = txq_array_index;
		}

		snprintf(fp->name, sizeof(fp->name), "%s-fp-%d", qlnx_name_str,
			rss_id);

		fp->tx_ring_full = 0;

		/* reset all the statistics counters */

		fp->tx_pkts_processed = 0;
		fp->tx_pkts_freed = 0;
		fp->tx_pkts_transmitted = 0;
		fp->tx_pkts_completed = 0;

#ifdef QLNX_TRACE_PERF_DATA
		fp->tx_pkts_trans_ctx = 0;
		fp->tx_pkts_compl_ctx = 0;
		fp->tx_pkts_trans_fp = 0;
		fp->tx_pkts_compl_fp = 0;
		fp->tx_pkts_compl_intr = 0;
#endif
		fp->tx_lso_wnd_min_len = 0;
		fp->tx_defrag = 0;
		fp->tx_nsegs_gt_elem_left = 0;
		fp->tx_tso_max_nsegs = 0;
		fp->tx_tso_min_nsegs = 0;
		fp->err_tx_nsegs_gt_elem_left = 0;
		fp->err_tx_dmamap_create = 0;
		fp->err_tx_defrag_dmamap_load = 0;
		fp->err_tx_non_tso_max_seg = 0;
		fp->err_tx_dmamap_load = 0;
		fp->err_tx_defrag = 0;
		fp->err_tx_free_pkt_null = 0;
		fp->err_tx_cons_idx_conflict = 0;

		fp->rx_pkts = 0;
		fp->err_m_getcl = 0;
		fp->err_m_getjcl = 0;
        }
	return;
}

void
qlnx_free_mem_sb(qlnx_host_t *ha, struct ecore_sb_info *sb_info)
{
	struct ecore_dev	*cdev;

	cdev = &ha->cdev;

        if (sb_info->sb_virt) {
                OSAL_DMA_FREE_COHERENT(cdev, ((void *)sb_info->sb_virt),
			(sb_info->sb_phys), (sizeof(*sb_info->sb_virt)));
		sb_info->sb_virt = NULL;
	}
}

static int
qlnx_sb_init(struct ecore_dev *cdev, struct ecore_sb_info *sb_info,
	void *sb_virt_addr, bus_addr_t sb_phy_addr, u16 sb_id)
{
        struct ecore_hwfn	*p_hwfn;
        int			hwfn_index, rc;
        u16			rel_sb_id;

        hwfn_index = sb_id % cdev->num_hwfns;
        p_hwfn = &cdev->hwfns[hwfn_index];
        rel_sb_id = sb_id / cdev->num_hwfns;

        QL_DPRINT2(((qlnx_host_t *)cdev), 
                "hwfn_index = %d p_hwfn = %p sb_id = 0x%x rel_sb_id = 0x%x \
                sb_info = %p sb_virt_addr = %p sb_phy_addr = %p\n",
                hwfn_index, p_hwfn, sb_id, rel_sb_id, sb_info,
                sb_virt_addr, (void *)sb_phy_addr);

        rc = ecore_int_sb_init(p_hwfn, p_hwfn->p_main_ptt, sb_info,
                             sb_virt_addr, sb_phy_addr, rel_sb_id);

        return rc;
}

/* This function allocates fast-path status block memory */
int
qlnx_alloc_mem_sb(qlnx_host_t *ha, struct ecore_sb_info *sb_info, u16 sb_id)
{
        struct status_block_e4	*sb_virt;
        bus_addr_t		sb_phys;
        int			rc;
	uint32_t		size;
	struct ecore_dev	*cdev;

	cdev = &ha->cdev;

	size = sizeof(*sb_virt);
	sb_virt = OSAL_DMA_ALLOC_COHERENT(cdev, (&sb_phys), size);

        if (!sb_virt) {
                QL_DPRINT1(ha, "Status block allocation failed\n");
                return -ENOMEM;
        }

        rc = qlnx_sb_init(cdev, sb_info, sb_virt, sb_phys, sb_id);
        if (rc) {
                OSAL_DMA_FREE_COHERENT(cdev, sb_virt, sb_phys, size);
        }

	return rc;
}

static void
qlnx_free_rx_buffers(qlnx_host_t *ha, struct qlnx_rx_queue *rxq)
{
        int			i;
	struct sw_rx_data	*rx_buf;

        for (i = 0; i < rxq->num_rx_buffers; i++) {

                rx_buf = &rxq->sw_rx_ring[i];

		if (rx_buf->data != NULL) {
			if (rx_buf->map != NULL) {
				bus_dmamap_unload(ha->rx_tag, rx_buf->map);
				bus_dmamap_destroy(ha->rx_tag, rx_buf->map);
				rx_buf->map = NULL;
			}
			m_freem(rx_buf->data);
			rx_buf->data = NULL;
		}
        }
	return;
}

static void
qlnx_free_mem_rxq(qlnx_host_t *ha, struct qlnx_rx_queue *rxq)
{
	struct ecore_dev	*cdev;
	int			i;

	cdev = &ha->cdev;

	qlnx_free_rx_buffers(ha, rxq);

	for (i = 0; i < ETH_TPA_MAX_AGGS_NUM; i++) {
		qlnx_free_tpa_mbuf(ha, &rxq->tpa_info[i]);
		if (rxq->tpa_info[i].mpf != NULL)
			m_freem(rxq->tpa_info[i].mpf);
	}

	bzero((void *)&rxq->sw_rx_ring[0],
		(sizeof (struct sw_rx_data) * RX_RING_SIZE));

        /* Free the real RQ ring used by FW */
	if (rxq->rx_bd_ring.p_virt_addr) {
                ecore_chain_free(cdev, &rxq->rx_bd_ring);
                rxq->rx_bd_ring.p_virt_addr = NULL;
        }

        /* Free the real completion ring used by FW */
        if (rxq->rx_comp_ring.p_virt_addr &&
                        rxq->rx_comp_ring.pbl_sp.p_virt_table) {
                ecore_chain_free(cdev, &rxq->rx_comp_ring);
                rxq->rx_comp_ring.p_virt_addr = NULL;
                rxq->rx_comp_ring.pbl_sp.p_virt_table = NULL;
        }

#ifdef QLNX_SOFT_LRO
	{
		struct lro_ctrl *lro;

		lro = &rxq->lro;
		tcp_lro_free(lro);
	}
#endif /* #ifdef QLNX_SOFT_LRO */

	return;
}

static int
qlnx_alloc_rx_buffer(qlnx_host_t *ha, struct qlnx_rx_queue *rxq)
{
        register struct mbuf	*mp;
        uint16_t		rx_buf_size;
        struct sw_rx_data	*sw_rx_data;
        struct eth_rx_bd	*rx_bd;
        dma_addr_t		dma_addr;
	bus_dmamap_t		map;
	bus_dma_segment_t       segs[1];
	int			nsegs;
	int			ret;
	struct ecore_dev	*cdev;

	cdev = &ha->cdev;

        rx_buf_size = rxq->rx_buf_size;

	mp = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, rx_buf_size);

        if (mp == NULL) {
                QL_DPRINT1(ha, "Failed to allocate Rx data\n");
                return -ENOMEM;
        }

	mp->m_len = mp->m_pkthdr.len = rx_buf_size;

	map = (bus_dmamap_t)0;

	ret = bus_dmamap_load_mbuf_sg(ha->rx_tag, map, mp, segs, &nsegs,
			BUS_DMA_NOWAIT);
	dma_addr = segs[0].ds_addr;

	if (ret || !dma_addr || (nsegs != 1)) {
		m_freem(mp);
		QL_DPRINT1(ha, "bus_dmamap_load failed[%d, 0x%016llx, %d]\n",
                           ret, (long long unsigned int)dma_addr, nsegs);
		return -ENOMEM;
	}

        sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_prod];
        sw_rx_data->data = mp;
        sw_rx_data->dma_addr = dma_addr;
        sw_rx_data->map = map;

        /* Advance PROD and get BD pointer */
        rx_bd = (struct eth_rx_bd *)ecore_chain_produce(&rxq->rx_bd_ring);
        rx_bd->addr.hi = htole32(U64_HI(dma_addr));
        rx_bd->addr.lo = htole32(U64_LO(dma_addr));
	bus_dmamap_sync(ha->rx_tag, map, BUS_DMASYNC_PREREAD);

        rxq->sw_rx_prod = (rxq->sw_rx_prod + 1) & (RX_RING_SIZE - 1);

        return 0;
}

static int
qlnx_alloc_tpa_mbuf(qlnx_host_t *ha, uint16_t rx_buf_size,
	struct qlnx_agg_info *tpa)
{
	struct mbuf		*mp;
        dma_addr_t		dma_addr;
	bus_dmamap_t		map;
	bus_dma_segment_t       segs[1];
	int			nsegs;
	int			ret;
        struct sw_rx_data	*rx_buf;

	mp = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, rx_buf_size);

        if (mp == NULL) {
                QL_DPRINT1(ha, "Failed to allocate Rx data\n");
                return -ENOMEM;
        }

	mp->m_len = mp->m_pkthdr.len = rx_buf_size;

	map = (bus_dmamap_t)0;

	ret = bus_dmamap_load_mbuf_sg(ha->rx_tag, map, mp, segs, &nsegs,
			BUS_DMA_NOWAIT);
	dma_addr = segs[0].ds_addr;

	if (ret || !dma_addr || (nsegs != 1)) {
		m_freem(mp);
		QL_DPRINT1(ha, "bus_dmamap_load failed[%d, 0x%016llx, %d]\n",
			ret, (long long unsigned int)dma_addr, nsegs);
		return -ENOMEM;
	}

        rx_buf = &tpa->rx_buf;

	memset(rx_buf, 0, sizeof (struct sw_rx_data));

        rx_buf->data = mp;
        rx_buf->dma_addr = dma_addr;
        rx_buf->map = map;

	bus_dmamap_sync(ha->rx_tag, map, BUS_DMASYNC_PREREAD);

	return (0);
}

static void
qlnx_free_tpa_mbuf(qlnx_host_t *ha, struct qlnx_agg_info *tpa)
{
        struct sw_rx_data	*rx_buf;

	rx_buf = &tpa->rx_buf;

	if (rx_buf->data != NULL) {
		if (rx_buf->map != NULL) {
			bus_dmamap_unload(ha->rx_tag, rx_buf->map);
			bus_dmamap_destroy(ha->rx_tag, rx_buf->map);
			rx_buf->map = NULL;
		}
		m_freem(rx_buf->data);
		rx_buf->data = NULL;
	}
	return;
}

/* This function allocates all memory needed per Rx queue */
static int
qlnx_alloc_mem_rxq(qlnx_host_t *ha, struct qlnx_rx_queue *rxq)
{
        int			i, rc, num_allocated;
	struct ifnet		*ifp;
	struct ecore_dev	 *cdev;

	cdev = &ha->cdev;
	ifp = ha->ifp;

        rxq->num_rx_buffers = RX_RING_SIZE;

	rxq->rx_buf_size = ha->rx_buf_size;

        /* Allocate the parallel driver ring for Rx buffers */
	bzero((void *)&rxq->sw_rx_ring[0],
		(sizeof (struct sw_rx_data) * RX_RING_SIZE));

        /* Allocate FW Rx ring  */

        rc = ecore_chain_alloc(cdev,
			ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
			ECORE_CHAIN_MODE_NEXT_PTR,
			ECORE_CHAIN_CNT_TYPE_U16,
			RX_RING_SIZE,
			sizeof(struct eth_rx_bd),
			&rxq->rx_bd_ring, NULL);

        if (rc)
                goto err;

        /* Allocate FW completion ring */
        rc = ecore_chain_alloc(cdev,
                        ECORE_CHAIN_USE_TO_CONSUME,
                        ECORE_CHAIN_MODE_PBL,
			ECORE_CHAIN_CNT_TYPE_U16,
                        RX_RING_SIZE,
                        sizeof(union eth_rx_cqe),
                        &rxq->rx_comp_ring, NULL);

        if (rc)
                goto err;

        /* Allocate buffers for the Rx ring */

	for (i = 0; i < ETH_TPA_MAX_AGGS_NUM; i++) {
		rc = qlnx_alloc_tpa_mbuf(ha, rxq->rx_buf_size,
			&rxq->tpa_info[i]);
                if (rc)
                        break;

	}

        for (i = 0; i < rxq->num_rx_buffers; i++) {
                rc = qlnx_alloc_rx_buffer(ha, rxq);
                if (rc)
                        break;
        }
        num_allocated = i;
        if (!num_allocated) {
		QL_DPRINT1(ha, "Rx buffers allocation failed\n");
                goto err;
        } else if (num_allocated < rxq->num_rx_buffers) {
		QL_DPRINT1(ha, "Allocated less buffers than"
			" desired (%d allocated)\n", num_allocated);
        }

#ifdef QLNX_SOFT_LRO

	{
		struct lro_ctrl *lro;

		lro = &rxq->lro;

#if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO)
		if (tcp_lro_init_args(lro, ifp, 0, rxq->num_rx_buffers)) {
			QL_DPRINT1(ha, "tcp_lro_init[%d] failed\n",
				   rxq->rxq_id);
			goto err;
		}
#else
		if (tcp_lro_init(lro)) {
			QL_DPRINT1(ha, "tcp_lro_init[%d] failed\n",
				   rxq->rxq_id);
			goto err;
		}
#endif /* #if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO) */

		lro->ifp = ha->ifp;
	}
#endif /* #ifdef QLNX_SOFT_LRO */
        return 0;

err:
        qlnx_free_mem_rxq(ha, rxq);
        return -ENOMEM;
}


static void
qlnx_free_mem_txq(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct qlnx_tx_queue *txq)
{
	struct ecore_dev	*cdev;

	cdev = &ha->cdev;

	bzero((void *)&txq->sw_tx_ring[0],
		(sizeof (struct sw_tx_bd) * TX_RING_SIZE));

        /* Free the real RQ ring used by FW */
        if (txq->tx_pbl.p_virt_addr) {
                ecore_chain_free(cdev, &txq->tx_pbl);
                txq->tx_pbl.p_virt_addr = NULL;
        }
	return;
}

/* This function allocates all memory needed per Tx queue */
static int
qlnx_alloc_mem_txq(qlnx_host_t *ha, struct qlnx_fastpath *fp, 
	struct qlnx_tx_queue *txq)
{
        int			ret = ECORE_SUCCESS;
        union eth_tx_bd_types	*p_virt;
	struct ecore_dev	*cdev;

	cdev = &ha->cdev;

	bzero((void *)&txq->sw_tx_ring[0],
		(sizeof (struct sw_tx_bd) * TX_RING_SIZE));

        /* Allocate the real Tx ring to be used by FW */
        ret = ecore_chain_alloc(cdev,
                        ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
                        ECORE_CHAIN_MODE_PBL,
			ECORE_CHAIN_CNT_TYPE_U16,
                        TX_RING_SIZE,
                        sizeof(*p_virt),
                        &txq->tx_pbl, NULL);

        if (ret != ECORE_SUCCESS) {
                goto err;
        }

	txq->num_tx_buffers = TX_RING_SIZE;

        return 0;

err:
        qlnx_free_mem_txq(ha, fp, txq);
        return -ENOMEM;
}

static void
qlnx_free_tx_br(qlnx_host_t *ha, struct qlnx_fastpath *fp)
{
	struct mbuf	*mp;
	struct ifnet	*ifp = ha->ifp;

	if (mtx_initialized(&fp->tx_mtx)) {

		if (fp->tx_br != NULL) {

			mtx_lock(&fp->tx_mtx);

			while ((mp = drbr_dequeue(ifp, fp->tx_br)) != NULL) {
				fp->tx_pkts_freed++;
				m_freem(mp);
			}

			mtx_unlock(&fp->tx_mtx);

			buf_ring_free(fp->tx_br, M_DEVBUF);
			fp->tx_br = NULL;
		}
		mtx_destroy(&fp->tx_mtx);
	}
	return;
}

static void
qlnx_free_mem_fp(qlnx_host_t *ha, struct qlnx_fastpath *fp)
{
        int	tc;

        qlnx_free_mem_sb(ha, fp->sb_info);

        qlnx_free_mem_rxq(ha, fp->rxq);

        for (tc = 0; tc < ha->num_tc; tc++)
                qlnx_free_mem_txq(ha, fp, fp->txq[tc]);

	return;
}

static int
qlnx_alloc_tx_br(qlnx_host_t *ha, struct qlnx_fastpath *fp)
{
	snprintf(fp->tx_mtx_name, sizeof(fp->tx_mtx_name),
		"qlnx%d_fp%d_tx_mq_lock", ha->dev_unit, fp->rss_id);

	mtx_init(&fp->tx_mtx, fp->tx_mtx_name, NULL, MTX_DEF);

        fp->tx_br = buf_ring_alloc(TX_RING_SIZE, M_DEVBUF,
                                   M_NOWAIT, &fp->tx_mtx);
        if (fp->tx_br == NULL) {
		QL_DPRINT1(ha, "buf_ring_alloc failed for fp[%d, %d]\n",
			ha->dev_unit, fp->rss_id);
		return -ENOMEM;
        }
	return 0;
}

static int
qlnx_alloc_mem_fp(qlnx_host_t *ha, struct qlnx_fastpath *fp)
{
        int	rc, tc;

        rc = qlnx_alloc_mem_sb(ha, fp->sb_info, fp->rss_id);
        if (rc)
                goto err;

	if (ha->rx_jumbo_buf_eq_mtu) {
		if (ha->max_frame_size <= MCLBYTES)
			ha->rx_buf_size = MCLBYTES;
		else if (ha->max_frame_size <= MJUMPAGESIZE)
			ha->rx_buf_size = MJUMPAGESIZE;
		else if (ha->max_frame_size <= MJUM9BYTES)
			ha->rx_buf_size = MJUM9BYTES;
		else if (ha->max_frame_size <= MJUM16BYTES)
			ha->rx_buf_size = MJUM16BYTES;
	} else {
		if (ha->max_frame_size <= MCLBYTES)
			ha->rx_buf_size = MCLBYTES;
		else
			ha->rx_buf_size = MJUMPAGESIZE;
	}

        rc = qlnx_alloc_mem_rxq(ha, fp->rxq);
        if (rc)
                goto err;

        for (tc = 0; tc < ha->num_tc; tc++) {
                rc = qlnx_alloc_mem_txq(ha, fp, fp->txq[tc]);
                if (rc)
                        goto err;
        }

        return 0;

err:
        qlnx_free_mem_fp(ha, fp);
        return -ENOMEM;
}

static void
qlnx_free_mem_load(qlnx_host_t *ha)
{
        int			i;
	struct ecore_dev	*cdev;

	cdev = &ha->cdev;

        for (i = 0; i < ha->num_rss; i++) {
                struct qlnx_fastpath *fp = &ha->fp_array[i];

                qlnx_free_mem_fp(ha, fp);
        }
	return;
}

static int
qlnx_alloc_mem_load(qlnx_host_t *ha)
{
        int	rc = 0, rss_id;

        for (rss_id = 0; rss_id < ha->num_rss; rss_id++) {
                struct qlnx_fastpath *fp = &ha->fp_array[rss_id];

                rc = qlnx_alloc_mem_fp(ha, fp);
                if (rc)
                        break;
        }
	return (rc);
}

static int
qlnx_start_vport(struct ecore_dev *cdev,
                u8 vport_id,
                u16 mtu,
                u8 drop_ttl0_flg,
                u8 inner_vlan_removal_en_flg,
		u8 tx_switching,
		u8 hw_lro_enable)
{
        int					rc, i;
	struct ecore_sp_vport_start_params	vport_start_params = { 0 };
	qlnx_host_t				*ha;

	ha = (qlnx_host_t *)cdev;

	vport_start_params.remove_inner_vlan = inner_vlan_removal_en_flg;
	vport_start_params.tx_switching = 0;
	vport_start_params.handle_ptp_pkts = 0;
	vport_start_params.only_untagged = 0;
	vport_start_params.drop_ttl0 = drop_ttl0_flg;

	vport_start_params.tpa_mode =
		(hw_lro_enable ? ECORE_TPA_MODE_RSC : ECORE_TPA_MODE_NONE);
	vport_start_params.max_buffers_per_cqe = QLNX_TPA_MAX_AGG_BUFFERS;

	vport_start_params.vport_id = vport_id;
	vport_start_params.mtu = mtu;


	QL_DPRINT2(ha, "Setting mtu to %d and VPORT ID = %d\n", mtu, vport_id);

        for_each_hwfn(cdev, i) {
                struct ecore_hwfn *p_hwfn = &cdev->hwfns[i];

		vport_start_params.concrete_fid = p_hwfn->hw_info.concrete_fid;
		vport_start_params.opaque_fid = p_hwfn->hw_info.opaque_fid;

                rc = ecore_sp_vport_start(p_hwfn, &vport_start_params);

                if (rc) {
			QL_DPRINT1(ha, "Failed to start VPORT V-PORT %d"
				" with MTU %d\n" , vport_id, mtu);
                        return -ENOMEM;
                }

                ecore_hw_start_fastpath(p_hwfn);

		QL_DPRINT2(ha, "Started V-PORT %d with MTU %d\n",
			vport_id, mtu);
        }
        return 0;
}


static int
qlnx_update_vport(struct ecore_dev *cdev,
	struct qlnx_update_vport_params *params)
{
        struct ecore_sp_vport_update_params	sp_params;
        int					rc, i, j, fp_index;
	struct ecore_hwfn			*p_hwfn;
        struct ecore_rss_params			*rss;
	qlnx_host_t				*ha = (qlnx_host_t *)cdev;
        struct qlnx_fastpath			*fp;

        memset(&sp_params, 0, sizeof(sp_params));
        /* Translate protocol params into sp params */
        sp_params.vport_id = params->vport_id;

        sp_params.update_vport_active_rx_flg =
		params->update_vport_active_rx_flg;
        sp_params.vport_active_rx_flg = params->vport_active_rx_flg;

        sp_params.update_vport_active_tx_flg =
		params->update_vport_active_tx_flg;
        sp_params.vport_active_tx_flg = params->vport_active_tx_flg;

        sp_params.update_inner_vlan_removal_flg =
                params->update_inner_vlan_removal_flg;
        sp_params.inner_vlan_removal_flg = params->inner_vlan_removal_flg;

	sp_params.sge_tpa_params = params->sge_tpa_params;

        /* RSS - is a bit tricky, since upper-layer isn't familiar with hwfns.
         * We need to re-fix the rss values per engine for CMT.
         */
	if (params->rss_params->update_rss_config)
        sp_params.rss_params = params->rss_params;
	else
		sp_params.rss_params =  NULL;

        for_each_hwfn(cdev, i) {

		p_hwfn = &cdev->hwfns[i];

		if ((cdev->num_hwfns > 1) &&
			params->rss_params->update_rss_config &&
			params->rss_params->rss_enable) {

			rss = params->rss_params;

			for (j = 0; j < ECORE_RSS_IND_TABLE_SIZE; j++) {

				fp_index = ((cdev->num_hwfns * j) + i) %
						ha->num_rss;

                		fp = &ha->fp_array[fp_index];
                        	rss->rss_ind_table[j] = fp->rxq->handle;
			}

			for (j = 0; j < ECORE_RSS_IND_TABLE_SIZE;) {
				QL_DPRINT3(ha, "%p %p %p %p %p %p %p %p \n",
					rss->rss_ind_table[j],
					rss->rss_ind_table[j+1],
					rss->rss_ind_table[j+2],
					rss->rss_ind_table[j+3],
					rss->rss_ind_table[j+4],
					rss->rss_ind_table[j+5],
					rss->rss_ind_table[j+6],
					rss->rss_ind_table[j+7]);
					j += 8;
			}
		}

                sp_params.opaque_fid = p_hwfn->hw_info.opaque_fid;

		QL_DPRINT1(ha, "Update sp vport ID=%d\n", params->vport_id);

                rc = ecore_sp_vport_update(p_hwfn, &sp_params,
                                           ECORE_SPQ_MODE_EBLOCK, NULL);
                if (rc) {
			QL_DPRINT1(ha, "Failed to update VPORT\n");
                        return rc;
                }

                QL_DPRINT2(ha, "Updated V-PORT %d: tx_active_flag %d, \
			rx_active_flag %d [tx_update %d], [rx_update %d]\n",
			params->vport_id, params->vport_active_tx_flg,
			params->vport_active_rx_flg,
			params->update_vport_active_tx_flg,
			params->update_vport_active_rx_flg);
        }

        return 0;
}

static void
qlnx_reuse_rx_data(struct qlnx_rx_queue *rxq)
{
        struct eth_rx_bd	*rx_bd_cons =
					ecore_chain_consume(&rxq->rx_bd_ring);
        struct eth_rx_bd	*rx_bd_prod =
					ecore_chain_produce(&rxq->rx_bd_ring);
        struct sw_rx_data	*sw_rx_data_cons =
					&rxq->sw_rx_ring[rxq->sw_rx_cons];
        struct sw_rx_data	*sw_rx_data_prod =
					&rxq->sw_rx_ring[rxq->sw_rx_prod];

        sw_rx_data_prod->data = sw_rx_data_cons->data;
        memcpy(rx_bd_prod, rx_bd_cons, sizeof(struct eth_rx_bd));

        rxq->sw_rx_cons  = (rxq->sw_rx_cons + 1) & (RX_RING_SIZE - 1);
        rxq->sw_rx_prod  = (rxq->sw_rx_prod + 1) & (RX_RING_SIZE - 1);

	return;
}

static void
qlnx_update_rx_prod(struct ecore_hwfn *p_hwfn, struct qlnx_rx_queue *rxq)
{

        uint16_t	 	bd_prod;
        uint16_t		cqe_prod;
	union {
		struct eth_rx_prod_data rx_prod_data;
		uint32_t		data32;
	} rx_prods;

        bd_prod = ecore_chain_get_prod_idx(&rxq->rx_bd_ring);
        cqe_prod = ecore_chain_get_prod_idx(&rxq->rx_comp_ring);

        /* Update producers */
        rx_prods.rx_prod_data.bd_prod = htole16(bd_prod);
        rx_prods.rx_prod_data.cqe_prod = htole16(cqe_prod);

        /* Make sure that the BD and SGE data is updated before updating the
         * producers since FW might read the BD/SGE right after the producer
         * is updated.
         */
	wmb();

        internal_ram_wr(p_hwfn, rxq->hw_rxq_prod_addr,
		sizeof(rx_prods), &rx_prods.data32);

        /* mmiowb is needed to synchronize doorbell writes from more than one
         * processor. It guarantees that the write arrives to the device before
         * the napi lock is released and another qlnx_poll is called (possibly
         * on another CPU). Without this barrier, the next doorbell can bypass
         * this doorbell. This is applicable to IA64/Altix systems.
         */
        wmb();

	return;
}

static uint32_t qlnx_hash_key[] = {
                ((0x6d << 24)|(0x5a << 16)|(0x56 << 8)|0xda),
                ((0x25 << 24)|(0x5b << 16)|(0x0e << 8)|0xc2),
                ((0x41 << 24)|(0x67 << 16)|(0x25 << 8)|0x3d),
                ((0x43 << 24)|(0xa3 << 16)|(0x8f << 8)|0xb0),
                ((0xd0 << 24)|(0xca << 16)|(0x2b << 8)|0xcb),
                ((0xae << 24)|(0x7b << 16)|(0x30 << 8)|0xb4),
                ((0x77 << 24)|(0xcb << 16)|(0x2d << 8)|0xa3),
                ((0x80 << 24)|(0x30 << 16)|(0xf2 << 8)|0x0c),
                ((0x6a << 24)|(0x42 << 16)|(0xb7 << 8)|0x3b),
                ((0xbe << 24)|(0xac << 16)|(0x01 << 8)|0xfa)};

static int
qlnx_start_queues(qlnx_host_t *ha)
{
        int				rc, tc, i, vport_id = 0,
					drop_ttl0_flg = 1, vlan_removal_en = 1,
					tx_switching = 0, hw_lro_enable = 0;
        struct ecore_dev		*cdev = &ha->cdev;
        struct ecore_rss_params		*rss_params = &ha->rss_params;
        struct qlnx_update_vport_params	vport_update_params;
        struct ifnet			*ifp;
        struct ecore_hwfn		*p_hwfn;
	struct ecore_sge_tpa_params	tpa_params;
	struct ecore_queue_start_common_params qparams;
        struct qlnx_fastpath		*fp;

	ifp = ha->ifp;

	QL_DPRINT1(ha, "Num RSS = %d\n", ha->num_rss);

        if (!ha->num_rss) {
		QL_DPRINT1(ha, "Cannot update V-VPORT as active as there"
			" are no Rx queues\n");
                return -EINVAL;
        }

#ifndef QLNX_SOFT_LRO
        hw_lro_enable = ifp->if_capenable & IFCAP_LRO;
#endif /* #ifndef QLNX_SOFT_LRO */

        rc = qlnx_start_vport(cdev, vport_id, ifp->if_mtu, drop_ttl0_flg,
			vlan_removal_en, tx_switching, hw_lro_enable);

        if (rc) {
                QL_DPRINT1(ha, "Start V-PORT failed %d\n", rc);
                return rc;
        }

	QL_DPRINT2(ha, "Start vport ramrod passed, "
		"vport_id = %d, MTU = %d, vlan_removal_en = %d\n",
		vport_id, (int)(ifp->if_mtu + 0xe), vlan_removal_en);

        for_each_rss(i) {
		struct ecore_rxq_start_ret_params rx_ret_params;
		struct ecore_txq_start_ret_params tx_ret_params;

                fp = &ha->fp_array[i];
        	p_hwfn = &cdev->hwfns[(fp->rss_id % cdev->num_hwfns)];

		bzero(&qparams, sizeof(struct ecore_queue_start_common_params));
		bzero(&rx_ret_params,
			sizeof (struct ecore_rxq_start_ret_params));

		qparams.queue_id = i ;
		qparams.vport_id = vport_id;
		qparams.stats_id = vport_id;
		qparams.p_sb = fp->sb_info;
		qparams.sb_idx = RX_PI;
		

		rc = ecore_eth_rx_queue_start(p_hwfn,
			p_hwfn->hw_info.opaque_fid,
			&qparams,
			fp->rxq->rx_buf_size,	/* bd_max_bytes */
			/* bd_chain_phys_addr */
			fp->rxq->rx_bd_ring.p_phys_addr,
			/* cqe_pbl_addr */
			ecore_chain_get_pbl_phys(&fp->rxq->rx_comp_ring),
			/* cqe_pbl_size */
			ecore_chain_get_page_cnt(&fp->rxq->rx_comp_ring),
			&rx_ret_params);

                if (rc) {
                	QL_DPRINT1(ha, "Start RXQ #%d failed %d\n", i, rc);
                        return rc;
                }

		fp->rxq->hw_rxq_prod_addr	= rx_ret_params.p_prod;
		fp->rxq->handle			= rx_ret_params.p_handle;
                fp->rxq->hw_cons_ptr		=
				&fp->sb_info->sb_virt->pi_array[RX_PI];

                qlnx_update_rx_prod(p_hwfn, fp->rxq);

                for (tc = 0; tc < ha->num_tc; tc++) {
                        struct qlnx_tx_queue *txq = fp->txq[tc];
		
			bzero(&qparams,
				sizeof(struct ecore_queue_start_common_params));
			bzero(&tx_ret_params,
				sizeof (struct ecore_txq_start_ret_params));

			qparams.queue_id = txq->index / cdev->num_hwfns ;
			qparams.vport_id = vport_id;
			qparams.stats_id = vport_id;
			qparams.p_sb = fp->sb_info;
			qparams.sb_idx = TX_PI(tc);

			rc = ecore_eth_tx_queue_start(p_hwfn,
				p_hwfn->hw_info.opaque_fid,
				&qparams, tc,
				/* bd_chain_phys_addr */
				ecore_chain_get_pbl_phys(&txq->tx_pbl),
				ecore_chain_get_page_cnt(&txq->tx_pbl),
				&tx_ret_params);

                        if (rc) {
                		QL_DPRINT1(ha, "Start TXQ #%d failed %d\n",
					   txq->index, rc);
                                return rc;
                        }

			txq->doorbell_addr = tx_ret_params.p_doorbell;
			txq->handle = tx_ret_params.p_handle;

                        txq->hw_cons_ptr =
                                &fp->sb_info->sb_virt->pi_array[TX_PI(tc)];
                        SET_FIELD(txq->tx_db.data.params,
                                  ETH_DB_DATA_DEST, DB_DEST_XCM);
                        SET_FIELD(txq->tx_db.data.params, ETH_DB_DATA_AGG_CMD,
                                  DB_AGG_CMD_SET);
                        SET_FIELD(txq->tx_db.data.params,
                                  ETH_DB_DATA_AGG_VAL_SEL,
                                  DQ_XCM_ETH_TX_BD_PROD_CMD);

                        txq->tx_db.data.agg_flags = DQ_XCM_ETH_DQ_CF_CMD;
                }
        }

        /* Fill struct with RSS params */
        if (ha->num_rss > 1) {

                rss_params->update_rss_config = 1;
                rss_params->rss_enable = 1;
                rss_params->update_rss_capabilities = 1;
                rss_params->update_rss_ind_table = 1;
                rss_params->update_rss_key = 1;
                rss_params->rss_caps = ECORE_RSS_IPV4 | ECORE_RSS_IPV6 |
                                       ECORE_RSS_IPV4_TCP | ECORE_RSS_IPV6_TCP;
                rss_params->rss_table_size_log = 7; /* 2^7 = 128 */

                for (i = 0; i < ECORE_RSS_IND_TABLE_SIZE; i++) {
                	fp = &ha->fp_array[(i % ha->num_rss)];
                        rss_params->rss_ind_table[i] = fp->rxq->handle;
		}

                for (i = 0; i < ECORE_RSS_KEY_SIZE; i++)
			rss_params->rss_key[i] = (__le32)qlnx_hash_key[i];

        } else {
                memset(rss_params, 0, sizeof(*rss_params));
        }


        /* Prepare and send the vport enable */
        memset(&vport_update_params, 0, sizeof(vport_update_params));
        vport_update_params.vport_id = vport_id;
        vport_update_params.update_vport_active_tx_flg = 1;
        vport_update_params.vport_active_tx_flg = 1;
        vport_update_params.update_vport_active_rx_flg = 1;
        vport_update_params.vport_active_rx_flg = 1;
        vport_update_params.rss_params = rss_params;
        vport_update_params.update_inner_vlan_removal_flg = 1;
        vport_update_params.inner_vlan_removal_flg = 1;

	if (hw_lro_enable) {
		memset(&tpa_params, 0, sizeof (struct ecore_sge_tpa_params));

		tpa_params.max_buffers_per_cqe = QLNX_TPA_MAX_AGG_BUFFERS;

		tpa_params.update_tpa_en_flg = 1;
		tpa_params.tpa_ipv4_en_flg = 1;
		tpa_params.tpa_ipv6_en_flg = 1;

		tpa_params.update_tpa_param_flg = 1;
		tpa_params.tpa_pkt_split_flg = 0;
		tpa_params.tpa_hdr_data_split_flg = 0;
		tpa_params.tpa_gro_consistent_flg = 0;
		tpa_params.tpa_max_aggs_num = ETH_TPA_MAX_AGGS_NUM;
		tpa_params.tpa_max_size = (uint16_t)(-1);
		tpa_params.tpa_min_size_to_start = ifp->if_mtu/2;
		tpa_params.tpa_min_size_to_cont = ifp->if_mtu/2;

		vport_update_params.sge_tpa_params = &tpa_params;
	}

        rc = qlnx_update_vport(cdev, &vport_update_params);
        if (rc) {
		QL_DPRINT1(ha, "Update V-PORT failed %d\n", rc);
                return rc;
        }

        return 0;
}

static int
qlnx_drain_txq(qlnx_host_t *ha, struct qlnx_fastpath *fp,
	struct qlnx_tx_queue *txq)
{
	uint16_t	hw_bd_cons;
	uint16_t	ecore_cons_idx;

	QL_DPRINT2(ha, "enter\n");

	hw_bd_cons = le16toh(*txq->hw_cons_ptr);

	while (hw_bd_cons !=
		(ecore_cons_idx = ecore_chain_get_cons_idx(&txq->tx_pbl))) {

		mtx_lock(&fp->tx_mtx);

		(void)qlnx_tx_int(ha, fp, txq);

		mtx_unlock(&fp->tx_mtx);

		qlnx_mdelay(__func__, 2);

		hw_bd_cons = le16toh(*txq->hw_cons_ptr);
	}

	QL_DPRINT2(ha, "[%d, %d]: done\n", fp->rss_id, txq->index);

        return 0;
}

static int
qlnx_stop_queues(qlnx_host_t *ha)
{
        struct qlnx_update_vport_params	vport_update_params;
        struct ecore_dev		*cdev;
        struct qlnx_fastpath		*fp;
        int				rc, tc, i;

        cdev = &ha->cdev;

        /* Disable the vport */

        memset(&vport_update_params, 0, sizeof(vport_update_params));

        vport_update_params.vport_id = 0;
        vport_update_params.update_vport_active_tx_flg = 1;
        vport_update_params.vport_active_tx_flg = 0;
        vport_update_params.update_vport_active_rx_flg = 1;
        vport_update_params.vport_active_rx_flg = 0;
        vport_update_params.rss_params = &ha->rss_params;
        vport_update_params.rss_params->update_rss_config = 0;
        vport_update_params.rss_params->rss_enable = 0;
        vport_update_params.update_inner_vlan_removal_flg = 0;
        vport_update_params.inner_vlan_removal_flg = 0;

	QL_DPRINT1(ha, "Update vport ID= %d\n", vport_update_params.vport_id);

        rc = qlnx_update_vport(cdev, &vport_update_params);
        if (rc) {
		QL_DPRINT1(ha, "Failed to update vport\n");
                return rc;
        }

        /* Flush Tx queues. If needed, request drain from MCP */
        for_each_rss(i) {
                fp = &ha->fp_array[i];

                for (tc = 0; tc < ha->num_tc; tc++) {
                        struct qlnx_tx_queue *txq = fp->txq[tc];

                        rc = qlnx_drain_txq(ha, fp, txq);
                        if (rc)
                                return rc;
                }
        }

        /* Stop all Queues in reverse order*/
        for (i = ha->num_rss - 1; i >= 0; i--) {

		struct ecore_hwfn *p_hwfn = &cdev->hwfns[(i % cdev->num_hwfns)];

                fp = &ha->fp_array[i];

                /* Stop the Tx Queue(s)*/
                for (tc = 0; tc < ha->num_tc; tc++) {
			int tx_queue_id;

			tx_queue_id = tc * ha->num_rss + i;
			rc = ecore_eth_tx_queue_stop(p_hwfn,
					fp->txq[tc]->handle);
					
                        if (rc) {
				QL_DPRINT1(ha, "Failed to stop TXQ #%d\n",
					   tx_queue_id);
                                return rc;
                        }
                }

                /* Stop the Rx Queue*/
		rc = ecore_eth_rx_queue_stop(p_hwfn, fp->rxq->handle, false,
				false);
                if (rc) {
                        QL_DPRINT1(ha, "Failed to stop RXQ #%d\n", i);
                        return rc;
                }
        }

        /* Stop the vport */
	for_each_hwfn(cdev, i) {

		struct ecore_hwfn *p_hwfn = &cdev->hwfns[i];

		rc = ecore_sp_vport_stop(p_hwfn, p_hwfn->hw_info.opaque_fid, 0);

		if (rc) {
                        QL_DPRINT1(ha, "Failed to stop VPORT\n");
			return rc;
		}
	}

        return rc;
}

static int
qlnx_set_ucast_rx_mac(qlnx_host_t *ha,
	enum ecore_filter_opcode opcode,
	unsigned char mac[ETH_ALEN])
{
	struct ecore_filter_ucast	ucast;
	struct ecore_dev		*cdev;
	int				rc;

	cdev = &ha->cdev;

	bzero(&ucast, sizeof(struct ecore_filter_ucast));

        ucast.opcode = opcode;
        ucast.type = ECORE_FILTER_MAC;
        ucast.is_rx_filter = 1;
        ucast.vport_to_add_to = 0;
        memcpy(&ucast.mac[0], mac, ETH_ALEN);

	rc = ecore_filter_ucast_cmd(cdev, &ucast, ECORE_SPQ_MODE_CB, NULL);

        return (rc);
}

static int
qlnx_remove_all_ucast_mac(qlnx_host_t *ha)
{
	struct ecore_filter_ucast	ucast;
	struct ecore_dev		*cdev;
	int				rc;

	bzero(&ucast, sizeof(struct ecore_filter_ucast));

	ucast.opcode = ECORE_FILTER_REPLACE;
	ucast.type = ECORE_FILTER_MAC; 
	ucast.is_rx_filter = 1;

	cdev = &ha->cdev;

	rc = ecore_filter_ucast_cmd(cdev, &ucast, ECORE_SPQ_MODE_CB, NULL);

	return (rc);
}

static int
qlnx_remove_all_mcast_mac(qlnx_host_t *ha)
{
	struct ecore_filter_mcast	*mcast;
	struct ecore_dev		*cdev;
	int				rc, i;

	cdev = &ha->cdev;

	mcast = &ha->ecore_mcast;
	bzero(mcast, sizeof(struct ecore_filter_mcast));

	mcast->opcode = ECORE_FILTER_REMOVE;

	for (i = 0; i < QLNX_MAX_NUM_MULTICAST_ADDRS; i++) {

		if (ha->mcast[i].addr[0] || ha->mcast[i].addr[1] ||
			ha->mcast[i].addr[2] || ha->mcast[i].addr[3] ||
			ha->mcast[i].addr[4] || ha->mcast[i].addr[5]) {

			memcpy(&mcast->mac[i][0], &ha->mcast[i].addr[0], ETH_ALEN);
			mcast->num_mc_addrs++;
		}
	}
	mcast = &ha->ecore_mcast;

	rc = ecore_filter_mcast_cmd(cdev, mcast, ECORE_SPQ_MODE_CB, NULL);

	bzero(ha->mcast, (sizeof(qlnx_mcast_t) * QLNX_MAX_NUM_MULTICAST_ADDRS));
	ha->nmcast = 0;

	return (rc);
}

static int
qlnx_clean_filters(qlnx_host_t *ha)
{
        int	rc = 0;

	/* Remove all unicast macs */
	rc = qlnx_remove_all_ucast_mac(ha);
	if (rc)
		return rc;

	/* Remove all multicast macs */
	rc = qlnx_remove_all_mcast_mac(ha);
	if (rc)
		return rc;

        rc = qlnx_set_ucast_rx_mac(ha, ECORE_FILTER_FLUSH, ha->primary_mac);

        return (rc);
}

static int
qlnx_set_rx_accept_filter(qlnx_host_t *ha, uint8_t filter)
{
	struct ecore_filter_accept_flags	accept;
	int					rc = 0;
	struct ecore_dev			*cdev;

	cdev = &ha->cdev;

	bzero(&accept, sizeof(struct ecore_filter_accept_flags));

	accept.update_rx_mode_config = 1;
	accept.rx_accept_filter = filter;

	accept.update_tx_mode_config = 1;
	accept.tx_accept_filter = ECORE_ACCEPT_UCAST_MATCHED |
		ECORE_ACCEPT_MCAST_MATCHED | ECORE_ACCEPT_BCAST;

	rc = ecore_filter_accept_cmd(cdev, 0, accept, false, false,
			ECORE_SPQ_MODE_CB, NULL);

	return (rc);
}

static int
qlnx_set_rx_mode(qlnx_host_t *ha)
{
	int	rc = 0;
	uint8_t	filter;

	rc = qlnx_set_ucast_rx_mac(ha, ECORE_FILTER_REPLACE, ha->primary_mac);
        if (rc)
                return rc;

	rc = qlnx_remove_all_mcast_mac(ha);
        if (rc)
                return rc;

	filter = ECORE_ACCEPT_UCAST_MATCHED |
			ECORE_ACCEPT_MCAST_MATCHED |
			ECORE_ACCEPT_BCAST;

	if (qlnx_vf_device(ha) == 0) {
		filter |= ECORE_ACCEPT_UCAST_UNMATCHED;
		filter |= ECORE_ACCEPT_MCAST_UNMATCHED;
	}
	ha->filter = filter;

	rc = qlnx_set_rx_accept_filter(ha, filter);

	return (rc);
}

static int
qlnx_set_link(qlnx_host_t *ha, bool link_up)
{
        int			i, rc = 0;
	struct ecore_dev	*cdev;
	struct ecore_hwfn	*hwfn;
	struct ecore_ptt	*ptt;

	if (qlnx_vf_device(ha) == 0)
		return (0);

	cdev = &ha->cdev;

        for_each_hwfn(cdev, i) {

                hwfn = &cdev->hwfns[i];

                ptt = ecore_ptt_acquire(hwfn);
       	        if (!ptt)
                        return -EBUSY;

                rc = ecore_mcp_set_link(hwfn, ptt, link_up);

                ecore_ptt_release(hwfn, ptt);

                if (rc)
                        return rc;
        }
        return (rc);
}

#if __FreeBSD_version >= 1100000
static uint64_t
qlnx_get_counter(if_t ifp, ift_counter cnt)
{
	qlnx_host_t *ha;
	uint64_t count;

        ha = (qlnx_host_t *)if_getsoftc(ifp);

        switch (cnt) {

        case IFCOUNTER_IPACKETS:
		count = ha->hw_stats.common.rx_ucast_pkts +
			ha->hw_stats.common.rx_mcast_pkts +
			ha->hw_stats.common.rx_bcast_pkts;
		break;

        case IFCOUNTER_IERRORS:
		count = ha->hw_stats.common.rx_crc_errors +
			ha->hw_stats.common.rx_align_errors +
			ha->hw_stats.common.rx_oversize_packets +
			ha->hw_stats.common.rx_undersize_packets;
		break;

        case IFCOUNTER_OPACKETS:
		count = ha->hw_stats.common.tx_ucast_pkts +
			ha->hw_stats.common.tx_mcast_pkts +
			ha->hw_stats.common.tx_bcast_pkts;
		break;

        case IFCOUNTER_OERRORS:
                count = ha->hw_stats.common.tx_err_drop_pkts;
		break;

        case IFCOUNTER_COLLISIONS:
                return (0);

        case IFCOUNTER_IBYTES:
		count = ha->hw_stats.common.rx_ucast_bytes +
			ha->hw_stats.common.rx_mcast_bytes +
			ha->hw_stats.common.rx_bcast_bytes;
		break;

        case IFCOUNTER_OBYTES:
		count = ha->hw_stats.common.tx_ucast_bytes +
			ha->hw_stats.common.tx_mcast_bytes +
			ha->hw_stats.common.tx_bcast_bytes;
		break;

        case IFCOUNTER_IMCASTS:
		count = ha->hw_stats.common.rx_mcast_bytes;
		break;

        case IFCOUNTER_OMCASTS:
		count = ha->hw_stats.common.tx_mcast_bytes;
		break;

        case IFCOUNTER_IQDROPS:
        case IFCOUNTER_OQDROPS:
        case IFCOUNTER_NOPROTO:

        default:
                return (if_get_counter_default(ifp, cnt));
        }
	return (count);
}
#endif


static void
qlnx_timer(void *arg)
{
	qlnx_host_t	*ha;

	ha = (qlnx_host_t *)arg;

	if (ha->error_recovery) {
		ha->error_recovery = 0;
		taskqueue_enqueue(ha->err_taskqueue, &ha->err_task);
		return;
	}

       	ecore_get_vport_stats(&ha->cdev, &ha->hw_stats);

	if (ha->storm_stats_gather)
		qlnx_sample_storm_stats(ha);

	callout_reset(&ha->qlnx_callout, hz, qlnx_timer, ha);

	return;
}

static int
qlnx_load(qlnx_host_t *ha)
{
	int			i;
	int			rc = 0;
	struct ecore_dev	*cdev;
        device_t		dev;

	cdev = &ha->cdev;
        dev = ha->pci_dev;

	QL_DPRINT2(ha, "enter\n");

        rc = qlnx_alloc_mem_arrays(ha);
        if (rc)
                goto qlnx_load_exit0;

        qlnx_init_fp(ha);

        rc = qlnx_alloc_mem_load(ha);
        if (rc)
                goto qlnx_load_exit1;

        QL_DPRINT2(ha, "Allocated %d RSS queues on %d TC/s\n",
		   ha->num_rss, ha->num_tc);

	for (i = 0; i < ha->num_rss; i++) {

		if ((rc = bus_setup_intr(dev, ha->irq_vec[i].irq,
                        (INTR_TYPE_NET | INTR_MPSAFE),
                        NULL, qlnx_fp_isr, &ha->irq_vec[i],
                        &ha->irq_vec[i].handle))) {

                        QL_DPRINT1(ha, "could not setup interrupt\n");
                        goto qlnx_load_exit2;
		}

		QL_DPRINT2(ha, "rss_id = %d irq_rid %d \
			 irq %p handle %p\n", i,
			ha->irq_vec[i].irq_rid,
			ha->irq_vec[i].irq, ha->irq_vec[i].handle);

		bus_bind_intr(dev, ha->irq_vec[i].irq, (i % mp_ncpus));
	}

        rc = qlnx_start_queues(ha);
        if (rc)
                goto qlnx_load_exit2;

        QL_DPRINT2(ha, "Start VPORT, RXQ and TXQ succeeded\n");

        /* Add primary mac and set Rx filters */
        rc = qlnx_set_rx_mode(ha);
        if (rc)
                goto qlnx_load_exit2;

        /* Ask for link-up using current configuration */
	qlnx_set_link(ha, true);

	if (qlnx_vf_device(ha) == 0)
		qlnx_link_update(&ha->cdev.hwfns[0]);

        ha->state = QLNX_STATE_OPEN;

	bzero(&ha->hw_stats, sizeof(struct ecore_eth_stats));

	if (ha->flags.callout_init)
        	callout_reset(&ha->qlnx_callout, hz, qlnx_timer, ha);

        goto qlnx_load_exit0;

qlnx_load_exit2:
        qlnx_free_mem_load(ha);

qlnx_load_exit1:
        ha->num_rss = 0;

qlnx_load_exit0:
	QL_DPRINT2(ha, "exit [%d]\n", rc);
        return rc;
}

static void
qlnx_drain_soft_lro(qlnx_host_t *ha)
{
#ifdef QLNX_SOFT_LRO

	struct ifnet	*ifp;
	int		i;

	ifp = ha->ifp;


	if (ifp->if_capenable & IFCAP_LRO) {

	        for (i = 0; i < ha->num_rss; i++) {

			struct qlnx_fastpath *fp = &ha->fp_array[i];
			struct lro_ctrl *lro;

			lro = &fp->rxq->lro;

#if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO)

			tcp_lro_flush_all(lro);

#else
			struct lro_entry *queued;

			while ((!SLIST_EMPTY(&lro->lro_active))){
				queued = SLIST_FIRST(&lro->lro_active);
				SLIST_REMOVE_HEAD(&lro->lro_active, next);
				tcp_lro_flush(lro, queued);
			}

#endif /* #if (__FreeBSD_version >= 1100101) || (defined QLNX_QSORT_LRO) */

                }
	}

#endif /* #ifdef QLNX_SOFT_LRO */

	return;
}

static void
qlnx_unload(qlnx_host_t *ha)
{
	struct ecore_dev	*cdev;
        device_t		dev;
	int			i;

	cdev = &ha->cdev;
        dev = ha->pci_dev;

	QL_DPRINT2(ha, "enter\n");
        QL_DPRINT1(ha, " QLNX STATE = %d\n",ha->state);

	if (ha->state == QLNX_STATE_OPEN) {

		qlnx_set_link(ha, false);
		qlnx_clean_filters(ha);
		qlnx_stop_queues(ha);
		ecore_hw_stop_fastpath(cdev);

		for (i = 0; i < ha->num_rss; i++) {
			if (ha->irq_vec[i].handle) {
				(void)bus_teardown_intr(dev,
					ha->irq_vec[i].irq,
					ha->irq_vec[i].handle);
				ha->irq_vec[i].handle = NULL;
			}
		}

		qlnx_drain_fp_taskqueues(ha);
		qlnx_drain_soft_lro(ha);
        	qlnx_free_mem_load(ha);
	}

	if (ha->flags.callout_init)
		callout_drain(&ha->qlnx_callout);

	qlnx_mdelay(__func__, 1000);

        ha->state = QLNX_STATE_CLOSED;

	QL_DPRINT2(ha, "exit\n");
	return;
}

static int
qlnx_grc_dumpsize(qlnx_host_t *ha, uint32_t *num_dwords, int hwfn_index)
{
	int			rval = -1;
	struct ecore_hwfn	*p_hwfn;
	struct ecore_ptt	*p_ptt;

	ecore_dbg_set_app_ver(ecore_dbg_get_fw_func_ver());

	p_hwfn = &ha->cdev.hwfns[hwfn_index];
	p_ptt = ecore_ptt_acquire(p_hwfn);

        if (!p_ptt) {
		QL_DPRINT1(ha, "ecore_ptt_acquire failed\n");
                return (rval);
        }

        rval = ecore_dbg_grc_get_dump_buf_size(p_hwfn, p_ptt, num_dwords);

	if (rval == DBG_STATUS_OK)
                rval = 0;
        else {
		QL_DPRINT1(ha, "ecore_dbg_grc_get_dump_buf_size failed"
			"[0x%x]\n", rval);
	}

        ecore_ptt_release(p_hwfn, p_ptt);

        return (rval);
}

static int
qlnx_idle_chk_size(qlnx_host_t *ha, uint32_t *num_dwords, int hwfn_index)
{
	int			rval = -1;
	struct ecore_hwfn	*p_hwfn;
	struct ecore_ptt	*p_ptt;

	ecore_dbg_set_app_ver(ecore_dbg_get_fw_func_ver());

	p_hwfn = &ha->cdev.hwfns[hwfn_index];
	p_ptt = ecore_ptt_acquire(p_hwfn);

        if (!p_ptt) {
		QL_DPRINT1(ha, "ecore_ptt_acquire failed\n");
                return (rval);
        }

        rval = ecore_dbg_idle_chk_get_dump_buf_size(p_hwfn, p_ptt, num_dwords);

	if (rval == DBG_STATUS_OK)
                rval = 0;
        else {
		QL_DPRINT1(ha, "ecore_dbg_idle_chk_get_dump_buf_size failed"
			" [0x%x]\n", rval);
	}

        ecore_ptt_release(p_hwfn, p_ptt);

        return (rval);
}


static void
qlnx_sample_storm_stats(qlnx_host_t *ha)
{
        int			i, index;
        struct ecore_dev	*cdev;
	qlnx_storm_stats_t	*s_stats;
	uint32_t		reg;
        struct ecore_ptt	*p_ptt;
        struct ecore_hwfn	*hwfn;

	if (ha->storm_stats_index >= QLNX_STORM_STATS_SAMPLES_PER_HWFN) {
		ha->storm_stats_gather = 0;
		return;
	}

        cdev = &ha->cdev;

        for_each_hwfn(cdev, i) {

                hwfn = &cdev->hwfns[i];

                p_ptt = ecore_ptt_acquire(hwfn);
                if (!p_ptt)
                        return;

		index = ha->storm_stats_index +
				(i * QLNX_STORM_STATS_SAMPLES_PER_HWFN);

		s_stats = &ha->storm_stats[index];

		/* XSTORM */
		reg = XSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_ACTIVE_CYCLES_BB_K2;
		s_stats->xstorm_active_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = XSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_STALL_CYCLES_BB_K2;
		s_stats->xstorm_stall_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = XSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_SLEEPING_CYCLES_BB_K2;
		s_stats->xstorm_sleeping_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = XSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_INACTIVE_CYCLES_BB_K2;
		s_stats->xstorm_inactive_cycles = ecore_rd(hwfn, p_ptt, reg); 

		/* YSTORM */
		reg = YSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_ACTIVE_CYCLES_BB_K2;
		s_stats->ystorm_active_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = YSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_STALL_CYCLES_BB_K2;
		s_stats->ystorm_stall_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = YSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_SLEEPING_CYCLES_BB_K2;
		s_stats->ystorm_sleeping_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = YSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_INACTIVE_CYCLES_BB_K2;
		s_stats->ystorm_inactive_cycles = ecore_rd(hwfn, p_ptt, reg); 

		/* PSTORM */
		reg = PSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_ACTIVE_CYCLES_BB_K2;
		s_stats->pstorm_active_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = PSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_STALL_CYCLES_BB_K2;
		s_stats->pstorm_stall_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = PSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_SLEEPING_CYCLES_BB_K2;
		s_stats->pstorm_sleeping_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = PSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_INACTIVE_CYCLES_BB_K2;
		s_stats->pstorm_inactive_cycles = ecore_rd(hwfn, p_ptt, reg); 

		/* TSTORM */
		reg = TSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_ACTIVE_CYCLES_BB_K2;
		s_stats->tstorm_active_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = TSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_STALL_CYCLES_BB_K2;
		s_stats->tstorm_stall_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = TSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_SLEEPING_CYCLES_BB_K2;
		s_stats->tstorm_sleeping_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = TSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_INACTIVE_CYCLES_BB_K2;
		s_stats->tstorm_inactive_cycles = ecore_rd(hwfn, p_ptt, reg); 

		/* MSTORM */
		reg = MSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_ACTIVE_CYCLES_BB_K2;
		s_stats->mstorm_active_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = MSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_STALL_CYCLES_BB_K2;
		s_stats->mstorm_stall_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = MSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_SLEEPING_CYCLES_BB_K2;
		s_stats->mstorm_sleeping_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = MSEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_INACTIVE_CYCLES_BB_K2;
		s_stats->mstorm_inactive_cycles = ecore_rd(hwfn, p_ptt, reg); 

		/* USTORM */
		reg = USEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_ACTIVE_CYCLES_BB_K2;
		s_stats->ustorm_active_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = USEM_REG_FAST_MEMORY +
				SEM_FAST_REG_STORM_STALL_CYCLES_BB_K2;
		s_stats->ustorm_stall_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = USEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_SLEEPING_CYCLES_BB_K2;
		s_stats->ustorm_sleeping_cycles = ecore_rd(hwfn, p_ptt, reg); 

		reg = USEM_REG_FAST_MEMORY +
				SEM_FAST_REG_IDLE_INACTIVE_CYCLES_BB_K2;
		s_stats->ustorm_inactive_cycles = ecore_rd(hwfn, p_ptt, reg); 

                ecore_ptt_release(hwfn, p_ptt);
        }

	ha->storm_stats_index++;

        return;
}

/*
 * Name: qlnx_dump_buf8
 * Function: dumps a buffer as bytes
 */
static void
qlnx_dump_buf8(qlnx_host_t *ha, const char *msg, void *dbuf, uint32_t len)
{
        device_t	dev;
        uint32_t	i = 0;
        uint8_t		*buf;

        dev = ha->pci_dev;
        buf = dbuf;

        device_printf(dev, "%s: %s 0x%x dump start\n", __func__, msg, len);

        while (len >= 16) {
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x %02x %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3],
                        buf[4], buf[5], buf[6], buf[7],
                        buf[8], buf[9], buf[10], buf[11],
                        buf[12], buf[13], buf[14], buf[15]);
                i += 16;
                len -= 16;
                buf += 16;
        }
        switch (len) {
        case 1:
                device_printf(dev,"0x%08x: %02x\n", i, buf[0]);
                break;
        case 2:
                device_printf(dev,"0x%08x: %02x %02x\n", i, buf[0], buf[1]);
                break;
        case 3:
                device_printf(dev,"0x%08x: %02x %02x %02x\n",
                        i, buf[0], buf[1], buf[2]);
                break;
        case 4:
                device_printf(dev,"0x%08x: %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3]);
                break;
        case 5:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4]);
                break;
        case 6:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
                break;
        case 7:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
                break;
        case 8:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7]);
                break;
        case 9:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7], buf[8]);
                break;
        case 10:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7], buf[8], buf[9]);
                break;
        case 11:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7], buf[8], buf[9], buf[10]);
                break;
        case 12:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7], buf[8], buf[9], buf[10], buf[11]);
                break;
        case 13:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7], buf[8], buf[9], buf[10], buf[11], buf[12]);
                break;
        case 14:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7], buf[8], buf[9], buf[10], buf[11], buf[12],
                        buf[13]);
                break;
        case 15:
                device_printf(dev,"0x%08x:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x %02x %02x %02x %02x %02x\n", i,
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                        buf[7], buf[8], buf[9], buf[10], buf[11], buf[12],
                        buf[13], buf[14]);
                break;
        default:
                break;
        }

        device_printf(dev, "%s: %s dump end\n", __func__, msg);

        return;
}

#ifdef CONFIG_ECORE_SRIOV

static void
__qlnx_osal_iov_vf_cleanup(struct ecore_hwfn *p_hwfn, uint8_t rel_vf_id)
{
        struct ecore_public_vf_info *vf_info;

        vf_info = ecore_iov_get_public_vf_info(p_hwfn, rel_vf_id, false);

        if (!vf_info)
                return;

        /* Clear the VF mac */
        memset(vf_info->forced_mac, 0, ETH_ALEN);

        vf_info->forced_vlan = 0;

	return;
}

void
qlnx_osal_iov_vf_cleanup(void *p_hwfn, uint8_t relative_vf_id)
{
	__qlnx_osal_iov_vf_cleanup(p_hwfn, relative_vf_id);
	return;
}

static int
__qlnx_iov_chk_ucast(struct ecore_hwfn *p_hwfn, int vfid,
	struct ecore_filter_ucast *params)
{
        struct ecore_public_vf_info *vf;

	if (!ecore_iov_vf_has_vport_instance(p_hwfn, vfid)) {
		QL_DPRINT1(((qlnx_host_t *)p_hwfn->p_dev),
			"VF[%d] vport not initialized\n", vfid);
		return ECORE_INVAL;
	}

        vf = ecore_iov_get_public_vf_info(p_hwfn, vfid, true);
        if (!vf)
                return -EINVAL;

        /* No real decision to make; Store the configured MAC */
        if (params->type == ECORE_FILTER_MAC ||
            params->type == ECORE_FILTER_MAC_VLAN)
                memcpy(params->mac, vf->forced_mac, ETH_ALEN);

        return 0;
}

int
qlnx_iov_chk_ucast(void *p_hwfn, int vfid, void *params)
{
	return (__qlnx_iov_chk_ucast(p_hwfn, vfid, params));
}

static int
__qlnx_iov_update_vport(struct ecore_hwfn *hwfn, uint8_t vfid,
        struct ecore_sp_vport_update_params *params, uint16_t * tlvs)
{
        uint8_t mask;
        struct ecore_filter_accept_flags *flags;

	if (!ecore_iov_vf_has_vport_instance(hwfn, vfid)) {
		QL_DPRINT1(((qlnx_host_t *)hwfn->p_dev),
			"VF[%d] vport not initialized\n", vfid);
		return ECORE_INVAL;
	}

        /* Untrusted VFs can't even be trusted to know that fact.
         * Simply indicate everything is configured fine, and trace
         * configuration 'behind their back'.
         */
        mask = ECORE_ACCEPT_UCAST_UNMATCHED | ECORE_ACCEPT_MCAST_UNMATCHED;
        flags = &params->accept_flags;
        if (!(*tlvs & BIT(ECORE_IOV_VP_UPDATE_ACCEPT_PARAM)))
                return 0;

        return 0;

}
int
qlnx_iov_update_vport(void *hwfn, uint8_t vfid, void *params, uint16_t *tlvs)
{
	return(__qlnx_iov_update_vport(hwfn, vfid, params, tlvs));
}

static int
qlnx_find_hwfn_index(struct ecore_hwfn *p_hwfn)
{
	int			i;
	struct ecore_dev	*cdev;

	cdev = p_hwfn->p_dev;

	for (i = 0; i < cdev->num_hwfns; i++) { 
		if (&cdev->hwfns[i] == p_hwfn)
			break;
	}

	if (i >= cdev->num_hwfns)
		return (-1);

	return (i);
}

static int
__qlnx_pf_vf_msg(struct ecore_hwfn *p_hwfn, uint16_t rel_vf_id)
{
	qlnx_host_t *ha = (qlnx_host_t *)p_hwfn->p_dev;
	int i;

	QL_DPRINT2(ha, "ha = %p cdev = %p p_hwfn = %p rel_vf_id = %d\n",
		ha, p_hwfn->p_dev, p_hwfn, rel_vf_id);

	if ((i = qlnx_find_hwfn_index(p_hwfn)) == -1)
		return (-1);

	if (ha->sriov_task[i].pf_taskqueue != NULL) {

		atomic_testandset_32(&ha->sriov_task[i].flags,
			QLNX_SRIOV_TASK_FLAGS_VF_PF_MSG);

		taskqueue_enqueue(ha->sriov_task[i].pf_taskqueue,
			&ha->sriov_task[i].pf_task);

	}

	return (ECORE_SUCCESS);
}


int
qlnx_pf_vf_msg(void *p_hwfn, uint16_t relative_vf_id)
{
	return (__qlnx_pf_vf_msg(p_hwfn, relative_vf_id));
}

static void
__qlnx_vf_flr_update(struct ecore_hwfn *p_hwfn)
{
	qlnx_host_t *ha = (qlnx_host_t *)p_hwfn->p_dev;
	int i;

	if (!ha->sriov_initialized)
		return;

	QL_DPRINT2(ha,  "ha = %p cdev = %p p_hwfn = %p \n",
		ha, p_hwfn->p_dev, p_hwfn);

	if ((i = qlnx_find_hwfn_index(p_hwfn)) == -1)
		return;


	if (ha->sriov_task[i].pf_taskqueue != NULL) {

		atomic_testandset_32(&ha->sriov_task[i].flags,
			QLNX_SRIOV_TASK_FLAGS_VF_FLR_UPDATE);

		taskqueue_enqueue(ha->sriov_task[i].pf_taskqueue,
			&ha->sriov_task[i].pf_task);
	}

	return;
}


void
qlnx_vf_flr_update(void *p_hwfn)
{
	__qlnx_vf_flr_update(p_hwfn);

	return;
}

#ifndef QLNX_VF

static void
qlnx_vf_bulleting_update(struct ecore_hwfn *p_hwfn)
{
	qlnx_host_t *ha = (qlnx_host_t *)p_hwfn->p_dev;
	int i;

	QL_DPRINT2(ha,  "ha = %p cdev = %p p_hwfn = %p \n",
		ha, p_hwfn->p_dev, p_hwfn);

	if ((i = qlnx_find_hwfn_index(p_hwfn)) == -1)
		return;

	QL_DPRINT2(ha,  "ha = %p cdev = %p p_hwfn = %p i = %d\n",
		ha, p_hwfn->p_dev, p_hwfn, i);

	if (ha->sriov_task[i].pf_taskqueue != NULL) {

		atomic_testandset_32(&ha->sriov_task[i].flags,
			QLNX_SRIOV_TASK_FLAGS_BULLETIN_UPDATE);

		taskqueue_enqueue(ha->sriov_task[i].pf_taskqueue,
			&ha->sriov_task[i].pf_task);
	}
}

static void
qlnx_initialize_sriov(qlnx_host_t *ha)
{
	device_t	dev;
	nvlist_t	*pf_schema, *vf_schema;
	int		iov_error;

	dev = ha->pci_dev;

	pf_schema = pci_iov_schema_alloc_node();
	vf_schema = pci_iov_schema_alloc_node();

	pci_iov_schema_add_unicast_mac(vf_schema, "mac-addr", 0, NULL);
	pci_iov_schema_add_bool(vf_schema, "allow-set-mac",
		IOV_SCHEMA_HASDEFAULT, FALSE);
	pci_iov_schema_add_bool(vf_schema, "allow-promisc",
		IOV_SCHEMA_HASDEFAULT, FALSE);
	pci_iov_schema_add_uint16(vf_schema, "num-queues",
		IOV_SCHEMA_HASDEFAULT, 1);

	iov_error = pci_iov_attach(dev, pf_schema, vf_schema);

	if (iov_error != 0) {
		ha->sriov_initialized = 0;
	} else {
		device_printf(dev, "SRIOV initialized\n");
		ha->sriov_initialized = 1;
	}
			
	return;
}

static void
qlnx_sriov_disable(qlnx_host_t *ha)
{
	struct ecore_dev *cdev;
	int i, j;

	cdev = &ha->cdev;

	ecore_iov_set_vfs_to_disable(cdev, true);


	for_each_hwfn(cdev, i) {

		struct ecore_hwfn *hwfn = &cdev->hwfns[i];
		struct ecore_ptt *ptt = ecore_ptt_acquire(hwfn);

		if (!ptt) {
			QL_DPRINT1(ha, "Failed to acquire ptt\n");
			return;
		}
		/* Clean WFQ db and configure equal weight for all vports */
		ecore_clean_wfq_db(hwfn, ptt);

		ecore_for_each_vf(hwfn, j) {
			int k = 0;

			if (!ecore_iov_is_valid_vfid(hwfn, j, true, false))
				continue;

			if (ecore_iov_is_vf_started(hwfn, j)) {
				/* Wait until VF is disabled before releasing */

				for (k = 0; k < 100; k++) {
					if (!ecore_iov_is_vf_stopped(hwfn, j)) {
						qlnx_mdelay(__func__, 10);
					} else
						break;
				}
			}

			if (k < 100)
				ecore_iov_release_hw_for_vf(&cdev->hwfns[i],
                                                          ptt, j);
			else {
				QL_DPRINT1(ha,
					"Timeout waiting for VF's FLR to end\n");
			}
		}
		ecore_ptt_release(hwfn, ptt);
	}

	ecore_iov_set_vfs_to_disable(cdev, false);

	return;
}


static void
qlnx_sriov_enable_qid_config(struct ecore_hwfn *hwfn, u16 vfid,
	struct ecore_iov_vf_init_params *params)
{
        u16 base, i;

        /* Since we have an equal resource distribution per-VF, and we assume
         * PF has acquired the ECORE_PF_L2_QUE first queues, we start setting
         * sequentially from there.
         */
        base = FEAT_NUM(hwfn, ECORE_PF_L2_QUE) + vfid * params->num_queues;

        params->rel_vf_id = vfid;

        for (i = 0; i < params->num_queues; i++) {
                params->req_rx_queue[i] = base + i;
                params->req_tx_queue[i] = base + i;
        }

        /* PF uses indices 0 for itself; Set vport/RSS afterwards */
        params->vport_id = vfid + 1;
        params->rss_eng_id = vfid + 1;

	return;
}

static int
qlnx_iov_init(device_t dev, uint16_t num_vfs, const nvlist_t *nvlist_params)
{
	qlnx_host_t		*ha;
	struct ecore_dev	*cdev;
	struct ecore_iov_vf_init_params params;
	int ret, j, i;
	uint32_t max_vfs;

	if ((ha = device_get_softc(dev)) == NULL) {
		device_printf(dev, "%s: cannot get softc\n", __func__);
		return (-1);
	}

	if (qlnx_create_pf_taskqueues(ha) != 0)
		goto qlnx_iov_init_err0;

	cdev = &ha->cdev;

	max_vfs = RESC_NUM(&cdev->hwfns[0], ECORE_VPORT);

	QL_DPRINT2(ha," dev = %p enter num_vfs = %d max_vfs = %d\n",
		dev, num_vfs, max_vfs);

        if (num_vfs >= max_vfs) {
                QL_DPRINT1(ha, "Can start at most %d VFs\n",
                          (RESC_NUM(&cdev->hwfns[0], ECORE_VPORT) - 1));
		goto qlnx_iov_init_err0;
        }

	ha->vf_attr =  malloc(((sizeof (qlnx_vf_attr_t) * num_vfs)), M_QLNXBUF,
				M_NOWAIT);

	if (ha->vf_attr == NULL)
		goto qlnx_iov_init_err0;


        memset(&params, 0, sizeof(params));

        /* Initialize HW for VF access */
        for_each_hwfn(cdev, j) {
                struct ecore_hwfn *hwfn = &cdev->hwfns[j];
                struct ecore_ptt *ptt = ecore_ptt_acquire(hwfn);

                /* Make sure not to use more than 16 queues per VF */
                params.num_queues = min_t(int,
                                          (FEAT_NUM(hwfn, ECORE_VF_L2_QUE) / num_vfs),
                                          16);

                if (!ptt) {
                        QL_DPRINT1(ha, "Failed to acquire ptt\n");
                        goto qlnx_iov_init_err1;
                }

                for (i = 0; i < num_vfs; i++) {

                        if (!ecore_iov_is_valid_vfid(hwfn, i, false, true))
                                continue;

                        qlnx_sriov_enable_qid_config(hwfn, i, &params);

                        ret = ecore_iov_init_hw_for_vf(hwfn, ptt, &params);

                        if (ret) {
                                QL_DPRINT1(ha, "Failed to enable VF[%d]\n", i);
                                ecore_ptt_release(hwfn, ptt);
                                goto qlnx_iov_init_err1;
                        }
                }

                ecore_ptt_release(hwfn, ptt);
        }

	ha->num_vfs = num_vfs;
	qlnx_inform_vf_link_state(&cdev->hwfns[0], ha);

	QL_DPRINT2(ha," dev = %p exit num_vfs = %d\n", dev, num_vfs);

	return (0);

qlnx_iov_init_err1:
	qlnx_sriov_disable(ha);

qlnx_iov_init_err0:
	qlnx_destroy_pf_taskqueues(ha);
	ha->num_vfs = 0;

	return (-1);
}

static void
qlnx_iov_uninit(device_t dev)
{
	qlnx_host_t	*ha;

	if ((ha = device_get_softc(dev)) == NULL) {
		device_printf(dev, "%s: cannot get softc\n", __func__);
		return;
	}

	QL_DPRINT2(ha," dev = %p enter\n", dev);

	qlnx_sriov_disable(ha);
	qlnx_destroy_pf_taskqueues(ha);

	free(ha->vf_attr, M_QLNXBUF);
	ha->vf_attr = NULL;

	ha->num_vfs = 0;

	QL_DPRINT2(ha," dev = %p exit\n", dev);
	return;
}

static int
qlnx_iov_add_vf(device_t dev, uint16_t vfnum, const nvlist_t *params)
{
	qlnx_host_t	*ha;
	qlnx_vf_attr_t	*vf_attr;
	unsigned const char *mac;
	size_t size;
	struct ecore_hwfn *p_hwfn;

	if ((ha = device_get_softc(dev)) == NULL) {
		device_printf(dev, "%s: cannot get softc\n", __func__);
		return (-1);
	}

	QL_DPRINT2(ha," dev = %p enter vfnum = %d\n", dev, vfnum);

	if (vfnum > (ha->num_vfs - 1)) {
		QL_DPRINT1(ha, " VF[%d] is greater than max allowed [%d]\n",
			vfnum, (ha->num_vfs - 1));
	}
		
	vf_attr = &ha->vf_attr[vfnum];

        if (nvlist_exists_binary(params, "mac-addr")) {
                mac = nvlist_get_binary(params, "mac-addr", &size);
                bcopy(mac, vf_attr->mac_addr, ETHER_ADDR_LEN);
		device_printf(dev,
			"%s: mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\n", 
			__func__, vf_attr->mac_addr[0],
			vf_attr->mac_addr[1], vf_attr->mac_addr[2],
			vf_attr->mac_addr[3], vf_attr->mac_addr[4],
			vf_attr->mac_addr[5]);
		p_hwfn = &ha->cdev.hwfns[0];
		ecore_iov_bulletin_set_mac(p_hwfn, vf_attr->mac_addr,
			vfnum);
	}

	QL_DPRINT2(ha," dev = %p exit vfnum = %d\n", dev, vfnum);
	return (0);
}

static void
qlnx_handle_vf_msg(qlnx_host_t *ha, struct ecore_hwfn *p_hwfn)
{
        uint64_t events[ECORE_VF_ARRAY_LENGTH];
        struct ecore_ptt *ptt;
        int i;

        ptt = ecore_ptt_acquire(p_hwfn);
        if (!ptt) {
                QL_DPRINT1(ha, "Can't acquire PTT; re-scheduling\n");
		__qlnx_pf_vf_msg(p_hwfn, 0);
                return;
        }

        ecore_iov_pf_get_pending_events(p_hwfn, events);

        QL_DPRINT2(ha, "Event mask of VF events:"
		"0x%" PRIu64 "0x%" PRIu64 " 0x%" PRIu64 "\n",
                   events[0], events[1], events[2]);

        ecore_for_each_vf(p_hwfn, i) {

                /* Skip VFs with no pending messages */
                if (!(events[i / 64] & (1ULL << (i % 64))))
                        continue;

		QL_DPRINT2(ha, 
                           "Handling VF message from VF 0x%02x [Abs 0x%02x]\n",
                           i, p_hwfn->p_dev->p_iov_info->first_vf_in_pf + i);

                /* Copy VF's message to PF's request buffer for that VF */
                if (ecore_iov_copy_vf_msg(p_hwfn, ptt, i))
                        continue;

                ecore_iov_process_mbx_req(p_hwfn, ptt, i);
        }

        ecore_ptt_release(p_hwfn, ptt);

	return;
}

static void
qlnx_handle_vf_flr_update(qlnx_host_t *ha, struct ecore_hwfn *p_hwfn)
{
        struct ecore_ptt *ptt;
	int ret;

	ptt = ecore_ptt_acquire(p_hwfn);

	if (!ptt) {
                QL_DPRINT1(ha, "Can't acquire PTT; re-scheduling\n");
		__qlnx_vf_flr_update(p_hwfn);
                return;
	}

	ret = ecore_iov_vf_flr_cleanup(p_hwfn, ptt);

	if (ret) {
                QL_DPRINT1(ha, "ecore_iov_vf_flr_cleanup failed; re-scheduling\n");
	}
		
	ecore_ptt_release(p_hwfn, ptt);

	return;
}

static void
qlnx_handle_bulletin_update(qlnx_host_t *ha, struct ecore_hwfn *p_hwfn)
{
        struct ecore_ptt *ptt;
	int i;

	ptt = ecore_ptt_acquire(p_hwfn);

	if (!ptt) {
                QL_DPRINT1(ha, "Can't acquire PTT; re-scheduling\n");
		qlnx_vf_bulleting_update(p_hwfn);
                return;
	}

	ecore_for_each_vf(p_hwfn, i) {
		QL_DPRINT1(ha, "ecore_iov_post_vf_bulletin[%p, %d]\n",
			p_hwfn, i);
		ecore_iov_post_vf_bulletin(p_hwfn, i, ptt);
	}
		
	ecore_ptt_release(p_hwfn, ptt);

	return;
}

static void
qlnx_pf_taskqueue(void *context, int pending)
{
	struct ecore_hwfn	*p_hwfn;
	qlnx_host_t		*ha;
	int			i;

	p_hwfn = context;

	if (p_hwfn == NULL)
		return;

	ha = (qlnx_host_t *)(p_hwfn->p_dev);

	if ((i = qlnx_find_hwfn_index(p_hwfn)) == -1)
		return;

	if (atomic_testandclear_32(&ha->sriov_task[i].flags,
		QLNX_SRIOV_TASK_FLAGS_VF_PF_MSG))
		qlnx_handle_vf_msg(ha, p_hwfn);

	if (atomic_testandclear_32(&ha->sriov_task[i].flags,
		QLNX_SRIOV_TASK_FLAGS_VF_FLR_UPDATE))
		qlnx_handle_vf_flr_update(ha, p_hwfn);

	if (atomic_testandclear_32(&ha->sriov_task[i].flags,
		QLNX_SRIOV_TASK_FLAGS_BULLETIN_UPDATE))
		qlnx_handle_bulletin_update(ha, p_hwfn);

	return;
}

static int
qlnx_create_pf_taskqueues(qlnx_host_t *ha)
{
	int	i;
	uint8_t	tq_name[32];

	for (i = 0; i < ha->cdev.num_hwfns; i++) {

                struct ecore_hwfn *p_hwfn = &ha->cdev.hwfns[i];

		bzero(tq_name, sizeof (tq_name));
		snprintf(tq_name, sizeof (tq_name), "ql_pf_tq_%d", i);

		TASK_INIT(&ha->sriov_task[i].pf_task, 0, qlnx_pf_taskqueue, p_hwfn);

		ha->sriov_task[i].pf_taskqueue = taskqueue_create(tq_name, M_NOWAIT,
			 taskqueue_thread_enqueue,
			&ha->sriov_task[i].pf_taskqueue);

		if (ha->sriov_task[i].pf_taskqueue == NULL) 
			return (-1);

		taskqueue_start_threads(&ha->sriov_task[i].pf_taskqueue, 1,
			PI_NET, "%s", tq_name);

		QL_DPRINT1(ha, "%p\n", ha->sriov_task[i].pf_taskqueue);
	}

	return (0);
}

static void
qlnx_destroy_pf_taskqueues(qlnx_host_t *ha)
{
	int	i;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {
		if (ha->sriov_task[i].pf_taskqueue != NULL) {
			taskqueue_drain(ha->sriov_task[i].pf_taskqueue,
				&ha->sriov_task[i].pf_task);
			taskqueue_free(ha->sriov_task[i].pf_taskqueue);
			ha->sriov_task[i].pf_taskqueue = NULL;
		}
	}
	return;
}

static void
qlnx_inform_vf_link_state(struct ecore_hwfn *p_hwfn, qlnx_host_t *ha)
{
	struct ecore_mcp_link_capabilities caps;
	struct ecore_mcp_link_params params;
	struct ecore_mcp_link_state link;
	int i;

	if (!p_hwfn->pf_iov_info)
		return;

	memset(&params, 0, sizeof(struct ecore_mcp_link_params));
	memset(&link, 0, sizeof(struct ecore_mcp_link_state));
	memset(&caps, 0, sizeof(struct ecore_mcp_link_capabilities));

	memcpy(&caps, ecore_mcp_get_link_capabilities(p_hwfn), sizeof(caps));
        memcpy(&link, ecore_mcp_get_link_state(p_hwfn), sizeof(link));
        memcpy(&params, ecore_mcp_get_link_params(p_hwfn), sizeof(params));

	QL_DPRINT2(ha, "called\n");

        /* Update bulletin of all future possible VFs with link configuration */
        for (i = 0; i < p_hwfn->p_dev->p_iov_info->total_vfs; i++) {

                /* Modify link according to the VF's configured link state */

                link.link_up = false;

                if (ha->link_up) {
                        link.link_up = true;
                        /* Set speed according to maximum supported by HW.
                         * that is 40G for regular devices and 100G for CMT
                         * mode devices.
                         */
                        link.speed = (p_hwfn->p_dev->num_hwfns > 1) ?
						100000 : link.speed;
		}
		QL_DPRINT2(ha, "link [%d] = %d\n", i, link.link_up);
                ecore_iov_set_link(p_hwfn, i, &params, &link, &caps);
        }

	qlnx_vf_bulleting_update(p_hwfn);

	return;
}
#endif /* #ifndef QLNX_VF */
#endif /* #ifdef CONFIG_ECORE_SRIOV */
