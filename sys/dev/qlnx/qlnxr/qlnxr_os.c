/*
 * Copyright (c) 2018-2019 Cavium, Inc.
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
 * File: qlnxr_os.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qlnxr_def.h"

SYSCTL_NODE(_dev, OID_AUTO, qnxr, CTLFLAG_RW, 0, "Qlogic RDMA module");

uint32_t delayed_ack = 0;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, delayed_ack, CTLFLAG_RW, &delayed_ack, 1,
	"iWARP: Delayed Ack: 0 - Disabled 1 - Enabled. Default: Disabled");

uint32_t timestamp = 1;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, timestamp, CTLFLAG_RW, &timestamp, 1,
	"iWARP: Timestamp: 0 - Disabled 1 - Enabled. Default:Enabled");

uint32_t rcv_wnd_size = 0;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, rcv_wnd_size, CTLFLAG_RW, &rcv_wnd_size, 1,
	"iWARP: Receive Window Size in K. Default 1M");

uint32_t crc_needed = 1;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, crc_needed, CTLFLAG_RW, &crc_needed, 1,
	"iWARP: CRC needed 0 - Disabled 1 - Enabled. Default:Enabled");

uint32_t peer2peer = 1;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, peer2peer, CTLFLAG_RW, &peer2peer, 1,
	"iWARP: Support peer2peer ULPs 0 - Disabled 1 - Enabled. Default:Enabled");

uint32_t mpa_enhanced = 1;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, mpa_enhanced, CTLFLAG_RW, &mpa_enhanced, 1,
	"iWARP: MPA Enhanced mode. Default:1");

uint32_t rtr_type = 7;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, rtr_type, CTLFLAG_RW, &rtr_type, 1,
	"iWARP: RDMAP opcode to use for the RTR message: BITMAP 1: RDMA_SEND 2: RDMA_WRITE 4: RDMA_READ. Default: 7");


#define QNXR_WQ_MULTIPLIER_MIN  (1)
#define QNXR_WQ_MULTIPLIER_MAX  (7)
#define QNXR_WQ_MULTIPLIER_DFT  (3)

uint32_t wq_multiplier= QNXR_WQ_MULTIPLIER_DFT;
SYSCTL_UINT(_dev_qnxr, OID_AUTO, wq_multiplier, CTLFLAG_RW, &wq_multiplier, 1,
	" When creating a WQ the actual number of WQE created will"
	" be multiplied by this number (default is 3).");
static ssize_t
show_rev(struct device *device, struct device_attribute *attr,
	char *buf)
{
        struct qlnxr_dev *dev = dev_get_drvdata(device);

        return sprintf(buf, "0x%x\n", dev->cdev->vendor_id);
}

static ssize_t
show_hca_type(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct qlnxr_dev *dev = dev_get_drvdata(device);
        return sprintf(buf, "QLogic0x%x\n", dev->cdev->device_id);
}

static ssize_t
show_fw_ver(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct qlnxr_dev *dev = dev_get_drvdata(device);
	uint32_t fw_ver = (uint32_t) dev->attr.fw_ver;

	return sprintf(buf, "%d.%d.%d\n",
		       (fw_ver >> 24) & 0xff, (fw_ver >> 16) & 0xff,
		       (fw_ver >> 8) & 0xff);
}
static ssize_t
show_board(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct qlnxr_dev *dev = dev_get_drvdata(device);
	return sprintf(buf, "%x\n", dev->cdev->device_id);
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca_type, NULL);
static DEVICE_ATTR(fw_ver, S_IRUGO, show_fw_ver, NULL);
static DEVICE_ATTR(board_id, S_IRUGO, show_board, NULL);

static struct device_attribute *qlnxr_class_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_hca_type,
	&dev_attr_fw_ver,
	&dev_attr_board_id
};

static void
qlnxr_ib_dispatch_event(qlnxr_dev_t *dev, uint8_t port_num,
	enum ib_event_type type)
{
        struct ib_event ibev;

	QL_DPRINT12(dev->ha, "enter\n");

        ibev.device = &dev->ibdev;
        ibev.element.port_num = port_num;
        ibev.event = type;

        ib_dispatch_event(&ibev);

	QL_DPRINT12(dev->ha, "exit\n");
}

static int
__qlnxr_iw_destroy_listen(struct iw_cm_id *cm_id)
{
	qlnxr_iw_destroy_listen(cm_id);

	return (0);
}

static int
qlnxr_register_device(qlnxr_dev_t *dev)
{
	struct ib_device *ibdev;
	struct iw_cm_verbs *iwcm;
	int ret;

	QL_DPRINT12(dev->ha, "enter\n");

	ibdev = &dev->ibdev;

	strlcpy(ibdev->name, "qlnxr%d", IB_DEVICE_NAME_MAX);

	memset(&ibdev->node_guid, 0, sizeof(ibdev->node_guid));
	memcpy(&ibdev->node_guid, dev->ha->primary_mac, ETHER_ADDR_LEN);

	memcpy(ibdev->node_desc, QLNXR_NODE_DESC, sizeof(QLNXR_NODE_DESC));

	ibdev->owner = THIS_MODULE;
	ibdev->uverbs_abi_ver = 7;
	ibdev->local_dma_lkey = 0;

	ibdev->uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT) |
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE) |
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT) |
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD) |
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD) |
		(1ull << IB_USER_VERBS_CMD_REG_MR) |
		(1ull << IB_USER_VERBS_CMD_DEREG_MR) |
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ) |
		(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ) |
		(1ull << IB_USER_VERBS_CMD_CREATE_QP) |
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP) |
		(1ull << IB_USER_VERBS_CMD_QUERY_QP) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP) |
		(1ull << IB_USER_VERBS_CMD_POLL_CQ) |
		(1ull << IB_USER_VERBS_CMD_POST_SEND) |
		(1ull << IB_USER_VERBS_CMD_POST_RECV);

        if (QLNX_IS_IWARP(dev)) {
                ibdev->node_type = RDMA_NODE_RNIC;
                ibdev->query_gid = qlnxr_iw_query_gid;
        } else {
                ibdev->node_type = RDMA_NODE_IB_CA;
                ibdev->query_gid = qlnxr_query_gid;
                ibdev->uverbs_cmd_mask |=
			(1ull << IB_USER_VERBS_CMD_CREATE_SRQ) |
			(1ull << IB_USER_VERBS_CMD_DESTROY_SRQ) |
			(1ull << IB_USER_VERBS_CMD_QUERY_SRQ) |
			(1ull << IB_USER_VERBS_CMD_MODIFY_SRQ) |
			(1ull << IB_USER_VERBS_CMD_POST_SRQ_RECV);
                ibdev->create_srq = qlnxr_create_srq;
                ibdev->destroy_srq = qlnxr_destroy_srq;
                ibdev->modify_srq = qlnxr_modify_srq;
                ibdev->query_srq = qlnxr_query_srq;
                ibdev->post_srq_recv = qlnxr_post_srq_recv;
        }

	ibdev->phys_port_cnt = 1;
	ibdev->num_comp_vectors = dev->num_cnq;

        /* mandatory verbs. */
        ibdev->query_device = qlnxr_query_device;
        ibdev->query_port = qlnxr_query_port;
        ibdev->modify_port = qlnxr_modify_port;
        
	ibdev->alloc_ucontext = qlnxr_alloc_ucontext;
	ibdev->dealloc_ucontext = qlnxr_dealloc_ucontext;
        /* mandatory to support user space verbs consumer. */
        ibdev->mmap = qlnxr_mmap;

        ibdev->alloc_pd = qlnxr_alloc_pd;
        ibdev->dealloc_pd = qlnxr_dealloc_pd;

        ibdev->create_cq = qlnxr_create_cq;
        ibdev->destroy_cq = qlnxr_destroy_cq;
        ibdev->resize_cq = qlnxr_resize_cq;
        ibdev->req_notify_cq = qlnxr_arm_cq;

        ibdev->create_qp = qlnxr_create_qp;
        ibdev->modify_qp = qlnxr_modify_qp;
        ibdev->query_qp = qlnxr_query_qp;
        ibdev->destroy_qp = qlnxr_destroy_qp;

        ibdev->query_pkey = qlnxr_query_pkey;
        ibdev->create_ah = qlnxr_create_ah;
        ibdev->destroy_ah = qlnxr_destroy_ah;
        ibdev->query_ah = qlnxr_query_ah;
        ibdev->modify_ah = qlnxr_modify_ah;
        ibdev->get_dma_mr = qlnxr_get_dma_mr;
        ibdev->dereg_mr = qlnxr_dereg_mr;
        ibdev->reg_user_mr = qlnxr_reg_user_mr;
        
#if __FreeBSD_version >= 1102000
	ibdev->alloc_mr = qlnxr_alloc_mr;
	ibdev->map_mr_sg = qlnxr_map_mr_sg;
	ibdev->get_port_immutable = qlnxr_get_port_immutable;
#else
        ibdev->reg_phys_mr = qlnxr_reg_kernel_mr;
        ibdev->alloc_fast_reg_mr = qlnxr_alloc_frmr;
        ibdev->alloc_fast_reg_page_list = qlnxr_alloc_frmr_page_list;
        ibdev->free_fast_reg_page_list = qlnxr_free_frmr_page_list;
#endif /* #if __FreeBSD_version >= 1102000 */
	
        ibdev->poll_cq = qlnxr_poll_cq;
        ibdev->post_send = qlnxr_post_send;
        ibdev->post_recv = qlnxr_post_recv;
	ibdev->process_mad = qlnxr_process_mad;



        ibdev->dma_device = &dev->pdev->dev;

	ibdev->get_link_layer = qlnxr_link_layer;
        
	if (QLNX_IS_IWARP(dev)) {
                iwcm = kmalloc(sizeof(*iwcm), GFP_KERNEL);
	
		device_printf(dev->ha->pci_dev, "device is IWARP\n");
		if (iwcm == NULL)
			return (-ENOMEM);

                ibdev->iwcm = iwcm;

                iwcm->connect = qlnxr_iw_connect;
                iwcm->accept = qlnxr_iw_accept;
                iwcm->reject = qlnxr_iw_reject;

#if (__FreeBSD_version >= 1004000) && (__FreeBSD_version < 1102000)

                iwcm->create_listen_ep = qlnxr_iw_create_listen;
                iwcm->destroy_listen_ep = qlnxr_iw_destroy_listen;
#else
                iwcm->create_listen = qlnxr_iw_create_listen;
                iwcm->destroy_listen = __qlnxr_iw_destroy_listen;
#endif
                iwcm->add_ref = qlnxr_iw_qp_add_ref;
                iwcm->rem_ref = qlnxr_iw_qp_rem_ref;
                iwcm->get_qp = qlnxr_iw_get_qp;
        }

        ret = ib_register_device(ibdev, NULL);
	if (ret) {
		kfree(iwcm);
	}

	QL_DPRINT12(dev->ha, "exit\n");
        return ret;
}

#define HILO_U64(hi, lo)                ((((u64)(hi)) << 32) + (lo))

static void
qlnxr_intr(void *handle)
{
        struct qlnxr_cnq *cnq = handle;
        struct qlnxr_cq *cq;
        struct regpair *cq_handle;
        u16 hw_comp_cons, sw_comp_cons;
	qlnx_host_t *ha;

	ha = cnq->dev->ha;

	QL_DPRINT12(ha, "enter cnq = %p\n", handle);

        ecore_sb_ack(cnq->sb, IGU_INT_DISABLE, 0 /*do not update*/);

        ecore_sb_update_sb_idx(cnq->sb);

        hw_comp_cons = le16_to_cpu(*cnq->hw_cons_ptr);
        sw_comp_cons = ecore_chain_get_cons_idx(&cnq->pbl);

        rmb();

	QL_DPRINT12(ha, "enter cnq = %p hw_comp_cons = 0x%x sw_comp_cons = 0x%x\n",
		handle, hw_comp_cons, sw_comp_cons);

        while (sw_comp_cons != hw_comp_cons) {
                cq_handle = (struct regpair *)ecore_chain_consume(&cnq->pbl);
                cq = (struct qlnxr_cq *)(uintptr_t)HILO_U64(cq_handle->hi,
                                cq_handle->lo);

                if (cq == NULL) {
			QL_DPRINT11(ha, "cq == NULL\n");
                        break;
                }

                if (cq->sig != QLNXR_CQ_MAGIC_NUMBER) {
			QL_DPRINT11(ha,
				"cq->sig = 0x%x QLNXR_CQ_MAGIC_NUMBER = 0x%x\n",
				cq->sig, QLNXR_CQ_MAGIC_NUMBER);
                        break;
                }
                cq->arm_flags = 0;

                if (!cq->destroyed && cq->ibcq.comp_handler) {
			QL_DPRINT11(ha, "calling comp_handler = %p "
				"ibcq = %p cq_context = 0x%x\n",
				&cq->ibcq, cq->ibcq.cq_context);

                        (*cq->ibcq.comp_handler) (&cq->ibcq, cq->ibcq.cq_context);
                }
		cq->cnq_notif++;

                sw_comp_cons = ecore_chain_get_cons_idx(&cnq->pbl);

                cnq->n_comp++;
        }

        ecore_rdma_cnq_prod_update(cnq->dev->rdma_ctx, cnq->index, sw_comp_cons);

        ecore_sb_ack(cnq->sb, IGU_INT_ENABLE, 1 /*update*/);

	QL_DPRINT12(ha, "exit cnq = %p\n", handle);
        return;
}

static void
qlnxr_release_irqs(struct qlnxr_dev *dev)
{
	int i;
	qlnx_host_t *ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

        for (i = 0; i < dev->num_cnq; i++) {
                if (dev->cnq_array[i].irq_handle)
                        (void)bus_teardown_intr(dev->ha->pci_dev,
				dev->cnq_array[i].irq,
                                dev->cnq_array[i].irq_handle);

                if (dev->cnq_array[i].irq)
                        (void) bus_release_resource(dev->ha->pci_dev,
				SYS_RES_IRQ,
                                dev->cnq_array[i].irq_rid,
				dev->cnq_array[i].irq);
	}
	QL_DPRINT12(ha, "exit\n");
	return;
}

static int
qlnxr_setup_irqs(struct qlnxr_dev *dev)
{
	int start_irq_rid;
	int i;
	qlnx_host_t *ha;

	ha = dev->ha;

	start_irq_rid = dev->sb_start + 2;

	QL_DPRINT12(ha, "enter start_irq_rid = %d num_rss = %d\n",
		start_irq_rid, dev->ha->num_rss);


        for (i = 0; i < dev->num_cnq; i++) {

		dev->cnq_array[i].irq_rid = start_irq_rid + i;
	
		dev->cnq_array[i].irq = bus_alloc_resource_any(dev->ha->pci_dev,
						SYS_RES_IRQ,
						&dev->cnq_array[i].irq_rid,
						(RF_ACTIVE | RF_SHAREABLE));

		if (dev->cnq_array[i].irq == NULL) {

			QL_DPRINT11(ha,
				"bus_alloc_resource_any failed irq_rid = %d\n",
				dev->cnq_array[i].irq_rid);

			goto qlnxr_setup_irqs_err;
		}
			
                if (bus_setup_intr(dev->ha->pci_dev,
                                dev->cnq_array[i].irq,
                                (INTR_TYPE_NET | INTR_MPSAFE),
                                NULL, qlnxr_intr, &dev->cnq_array[i],
				&dev->cnq_array[i].irq_handle)) {

			QL_DPRINT11(ha, "bus_setup_intr failed\n");
			goto qlnxr_setup_irqs_err;
                }
		QL_DPRINT12(ha, "irq_rid = %d irq = %p irq_handle = %p\n",
			dev->cnq_array[i].irq_rid, dev->cnq_array[i].irq,
			dev->cnq_array[i].irq_handle);
	}

	QL_DPRINT12(ha, "exit\n");
	return (0);

qlnxr_setup_irqs_err:
	qlnxr_release_irqs(dev);

	QL_DPRINT12(ha, "exit -1\n");
	return (-1);
}

static void
qlnxr_free_resources(struct qlnxr_dev *dev)
{
        int i;
	qlnx_host_t *ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter dev->num_cnq = %d\n", dev->num_cnq);

	if (QLNX_IS_IWARP(dev)) {
		if (dev->iwarp_wq != NULL)
			destroy_workqueue(dev->iwarp_wq);
	}

        for (i = 0; i < dev->num_cnq; i++) {
                qlnx_free_mem_sb(dev->ha, &dev->sb_array[i]);
                ecore_chain_free(&dev->ha->cdev, &dev->cnq_array[i].pbl);
        }

	bzero(dev->cnq_array, (sizeof(struct qlnxr_cnq) * QLNXR_MAX_MSIX));
	bzero(dev->sb_array, (sizeof(struct ecore_sb_info) * QLNXR_MAX_MSIX));
	bzero(dev->sgid_tbl, (sizeof(union ib_gid) * QLNXR_MAX_SGID));

	if (mtx_initialized(&dev->idr_lock))
		mtx_destroy(&dev->idr_lock);

	if (mtx_initialized(&dev->sgid_lock))
		mtx_destroy(&dev->sgid_lock);

	QL_DPRINT12(ha, "exit\n");
	return;
}


static int
qlnxr_alloc_resources(struct qlnxr_dev *dev)
{
	uint16_t n_entries;
	int i, rc;
	qlnx_host_t *ha;

	ha = dev->ha;

        QL_DPRINT12(ha, "enter\n");

        bzero(dev->sgid_tbl, (sizeof (union ib_gid) * QLNXR_MAX_SGID));

        mtx_init(&dev->idr_lock, "idr_lock", NULL, MTX_DEF);
        mtx_init(&dev->sgid_lock, "sgid_lock", NULL, MTX_DEF);

        idr_init(&dev->qpidr);

        bzero(dev->sb_array, (sizeof (struct ecore_sb_info) * QLNXR_MAX_MSIX));
        bzero(dev->cnq_array, (sizeof (struct qlnxr_cnq) * QLNXR_MAX_MSIX));

        dev->sb_start = ecore_rdma_get_sb_id(dev->rdma_ctx, 0);

        QL_DPRINT12(ha, "dev->sb_start = 0x%x\n", dev->sb_start);

        /* Allocate CNQ PBLs */

        n_entries = min_t(u32, ECORE_RDMA_MAX_CNQ_SIZE, QLNXR_ROCE_MAX_CNQ_SIZE);

        for (i = 0; i < dev->num_cnq; i++) {
                rc = qlnx_alloc_mem_sb(dev->ha, &dev->sb_array[i],
                                       dev->sb_start + i);
                if (rc)
                        goto qlnxr_alloc_resources_exit;

                rc = ecore_chain_alloc(&dev->ha->cdev,
                                ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
                                ECORE_CHAIN_MODE_PBL,
                                ECORE_CHAIN_CNT_TYPE_U16,
                                n_entries,
                                sizeof(struct regpair *),
                                &dev->cnq_array[i].pbl,
                                NULL);

                /* configure cnq, except name since ibdev.name is still NULL */
                dev->cnq_array[i].dev = dev;
                dev->cnq_array[i].sb = &dev->sb_array[i];
                dev->cnq_array[i].hw_cons_ptr =
                        &(dev->sb_array[i].sb_virt->pi_array[ECORE_ROCE_PROTOCOL_INDEX]);
                dev->cnq_array[i].index = i;
                sprintf(dev->cnq_array[i].name, "qlnxr%d@pci:%d",
                        i, (dev->ha->pci_func));

        }

	QL_DPRINT12(ha, "exit\n");
        return 0;

qlnxr_alloc_resources_exit:

	qlnxr_free_resources(dev);

	QL_DPRINT12(ha, "exit -ENOMEM\n");
        return -ENOMEM;
}

void
qlnxr_affiliated_event(void *context, u8 e_code, void *fw_handle)
{
#define EVENT_TYPE_NOT_DEFINED  0
#define EVENT_TYPE_CQ           1
#define EVENT_TYPE_QP           2
#define EVENT_TYPE_GENERAL      3

        struct qlnxr_dev *dev = (struct qlnxr_dev *)context;
        struct regpair *async_handle = (struct regpair *)fw_handle;
        u64 roceHandle64 = ((u64)async_handle->hi << 32) + async_handle->lo;
        struct qlnxr_cq *cq =  (struct qlnxr_cq *)(uintptr_t)roceHandle64;
        struct qlnxr_qp *qp =  (struct qlnxr_qp *)(uintptr_t)roceHandle64;
        u8 event_type = EVENT_TYPE_NOT_DEFINED;
        struct ib_event event;
	qlnx_host_t *ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter context = %p e_code = 0x%x fw_handle = %p\n",
		context, e_code, fw_handle);

        if (QLNX_IS_IWARP(dev)) {
		switch (e_code) {

		case ECORE_IWARP_EVENT_CQ_OVERFLOW:
			event.event = IB_EVENT_CQ_ERR;
			event_type = EVENT_TYPE_CQ;
			break;

		default:
			QL_DPRINT12(ha,
				"unsupported event %d on handle=%llx\n",
				e_code, roceHandle64);
			break;
		}
        } else {
		switch (e_code) {

		case ROCE_ASYNC_EVENT_CQ_OVERFLOW_ERR:
			event.event = IB_EVENT_CQ_ERR;
			event_type = EVENT_TYPE_CQ;
			break;

		case ROCE_ASYNC_EVENT_SQ_DRAINED:
			event.event = IB_EVENT_SQ_DRAINED;
			event_type = EVENT_TYPE_QP;
			break;

		case ROCE_ASYNC_EVENT_QP_CATASTROPHIC_ERR:
			event.event = IB_EVENT_QP_FATAL;
			event_type = EVENT_TYPE_QP;
			break;

		case ROCE_ASYNC_EVENT_LOCAL_INVALID_REQUEST_ERR:
			event.event = IB_EVENT_QP_REQ_ERR;
			event_type = EVENT_TYPE_QP;
			break;

		case ROCE_ASYNC_EVENT_LOCAL_ACCESS_ERR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			event_type = EVENT_TYPE_QP;
			break;

		/* NOTE the following are not implemented in FW
		 *      ROCE_ASYNC_EVENT_CQ_ERR
		 *      ROCE_ASYNC_EVENT_COMM_EST
		 */
		/* TODO associate the following events -
		 *      ROCE_ASYNC_EVENT_SRQ_LIMIT
		 *      ROCE_ASYNC_EVENT_LAST_WQE_REACHED
		 *      ROCE_ASYNC_EVENT_LOCAL_CATASTROPHIC_ERR (un-affiliated)
		 */
		default:
			QL_DPRINT12(ha,
				"unsupported event 0x%x on fw_handle = %p\n",
				e_code, fw_handle);
			break;
		}
	}

        switch (event_type) {

        case EVENT_TYPE_CQ:
                if (cq && cq->sig == QLNXR_CQ_MAGIC_NUMBER) {
                        struct ib_cq *ibcq = &cq->ibcq;

                        if (ibcq->event_handler) {
                                event.device     = ibcq->device;
                                event.element.cq = ibcq;
                                ibcq->event_handler(&event, ibcq->cq_context);
                        }
                } else {
			QL_DPRINT11(ha,
				"CQ event with invalid CQ pointer"
				" Handle = %llx\n", roceHandle64);
                }
		QL_DPRINT12(ha,
			"CQ event 0x%x on handle = %p\n", e_code, cq);
                break;

        case EVENT_TYPE_QP:
                if (qp && qp->sig == QLNXR_QP_MAGIC_NUMBER) {
                        struct ib_qp *ibqp = &qp->ibqp;

                        if (ibqp->event_handler) {
                                event.device     = ibqp->device;
                                event.element.qp = ibqp;
                                ibqp->event_handler(&event, ibqp->qp_context);
                        }
                } else {
			QL_DPRINT11(ha,
				"QP event 0x%x with invalid QP pointer"
				" qp handle = %p\n",
				e_code, roceHandle64);
                }
		QL_DPRINT12(ha, "QP event 0x%x on qp handle = %p\n",
			e_code, qp);
                break;

        case EVENT_TYPE_GENERAL:
                break;

        default:
                break;

	}

	QL_DPRINT12(ha, "exit\n");

	return;
}

void
qlnxr_unaffiliated_event(void *context, u8 e_code)
{
        struct qlnxr_dev *dev = (struct qlnxr_dev *)context;
	qlnx_host_t *ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter/exit \n");
	return;
}


static int
qlnxr_set_device_attr(struct qlnxr_dev *dev)
{
	struct ecore_rdma_device *ecore_attr;
	struct qlnxr_device_attr *attr;
	u32 page_size;

	ecore_attr = ecore_rdma_query_device(dev->rdma_ctx);

	page_size = ~dev->attr.page_size_caps + 1;
	if(page_size > PAGE_SIZE) {
		QL_DPRINT12(dev->ha, "Kernel page size : %ld is smaller than"
		    " minimum page size : %ld required by qlnxr\n",
		    PAGE_SIZE, page_size);
		return -ENODEV;
	}
	attr = &dev->attr;
        attr->vendor_id = ecore_attr->vendor_id;
        attr->vendor_part_id = ecore_attr->vendor_part_id;

        QL_DPRINT12(dev->ha, "in qlnxr_set_device_attr, vendor : %x device : %x\n",
		attr->vendor_id, attr->vendor_part_id);

	attr->hw_ver = ecore_attr->hw_ver;
        attr->fw_ver = ecore_attr->fw_ver;
        attr->node_guid = ecore_attr->node_guid;
        attr->sys_image_guid = ecore_attr->sys_image_guid;
        attr->max_cnq = ecore_attr->max_cnq;
        attr->max_sge = ecore_attr->max_sge;
        attr->max_inline = ecore_attr->max_inline;
        attr->max_sqe = min_t(u32, ecore_attr->max_wqe, QLNXR_MAX_SQE);
        attr->max_rqe = min_t(u32, ecore_attr->max_wqe, QLNXR_MAX_RQE);
        attr->max_qp_resp_rd_atomic_resc = ecore_attr->max_qp_resp_rd_atomic_resc;
        attr->max_qp_req_rd_atomic_resc = ecore_attr->max_qp_req_rd_atomic_resc;
        attr->max_dev_resp_rd_atomic_resc =
            ecore_attr->max_dev_resp_rd_atomic_resc;
        attr->max_cq = ecore_attr->max_cq;
        attr->max_qp = ecore_attr->max_qp;
        attr->max_mr = ecore_attr->max_mr;
	attr->max_mr_size = ecore_attr->max_mr_size;
        attr->max_cqe = min_t(u64, ecore_attr->max_cqe, QLNXR_MAX_CQES);
        attr->max_mw = ecore_attr->max_mw;
        attr->max_fmr = ecore_attr->max_fmr;
        attr->max_mr_mw_fmr_pbl = ecore_attr->max_mr_mw_fmr_pbl;
        attr->max_mr_mw_fmr_size = ecore_attr->max_mr_mw_fmr_size;
        attr->max_pd = ecore_attr->max_pd;
        attr->max_ah = ecore_attr->max_ah;
        attr->max_pkey = ecore_attr->max_pkey;
        attr->max_srq = ecore_attr->max_srq;
        attr->max_srq_wr = ecore_attr->max_srq_wr;
        //attr->dev_caps = ecore_attr->dev_caps;
        attr->page_size_caps = ecore_attr->page_size_caps;
        attr->dev_ack_delay = ecore_attr->dev_ack_delay;
        attr->reserved_lkey = ecore_attr->reserved_lkey;
        attr->bad_pkey_counter = ecore_attr->bad_pkey_counter;
        attr->max_stats_queues = ecore_attr->max_stats_queues;

        return 0;
}


static int
qlnxr_init_hw(struct qlnxr_dev *dev)
{
        struct ecore_rdma_events events;
        struct ecore_rdma_add_user_out_params out_params;
        struct ecore_rdma_cnq_params *cur_pbl;
        struct ecore_rdma_start_in_params *in_params;
        dma_addr_t p_phys_table;
        u32 page_cnt;
        int rc = 0;
        int i;
	qlnx_host_t *ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

        in_params = kzalloc(sizeof(*in_params), GFP_KERNEL);
        if (!in_params) {
                rc = -ENOMEM;
                goto out;
        }

	bzero(&out_params, sizeof(struct ecore_rdma_add_user_out_params));
	bzero(&events, sizeof(struct ecore_rdma_events));

        in_params->desired_cnq = dev->num_cnq;

        for (i = 0; i < dev->num_cnq; i++) {
                cur_pbl = &in_params->cnq_pbl_list[i];

                page_cnt = ecore_chain_get_page_cnt(&dev->cnq_array[i].pbl);
                cur_pbl->num_pbl_pages = page_cnt;

                p_phys_table = ecore_chain_get_pbl_phys(&dev->cnq_array[i].pbl);
                cur_pbl->pbl_ptr = (u64)p_phys_table;
        }

        events.affiliated_event = qlnxr_affiliated_event;
        events.unaffiliated_event = qlnxr_unaffiliated_event;
        events.context = dev;

        in_params->events = &events;
        in_params->roce.cq_mode = ECORE_RDMA_CQ_MODE_32_BITS;
        in_params->max_mtu = dev->ha->max_frame_size;


	if (QLNX_IS_IWARP(dev)) {
	        if (delayed_ack)
        	        in_params->iwarp.flags |= ECORE_IWARP_DA_EN;

	        if (timestamp)
        	        in_params->iwarp.flags |= ECORE_IWARP_TS_EN;
 
	        in_params->iwarp.rcv_wnd_size = rcv_wnd_size*1024;
	        in_params->iwarp.crc_needed = crc_needed;
	        in_params->iwarp.ooo_num_rx_bufs =
        	        (MAX_RXMIT_CONNS * in_params->iwarp.rcv_wnd_size) /
	                in_params->max_mtu;

	        in_params->iwarp.mpa_peer2peer = peer2peer;
	        in_params->iwarp.mpa_rev =
			mpa_enhanced ? ECORE_MPA_REV2 : ECORE_MPA_REV1;
	        in_params->iwarp.mpa_rtr = rtr_type;
	}

        memcpy(&in_params->mac_addr[0], dev->ha->primary_mac, ETH_ALEN);

        rc = ecore_rdma_start(dev->rdma_ctx, in_params);
        if (rc)
                goto out;

        rc = ecore_rdma_add_user(dev->rdma_ctx, &out_params);
        if (rc)
                goto out;

        dev->db_addr = (void *)(uintptr_t)out_params.dpi_addr;
        dev->db_phys_addr = out_params.dpi_phys_addr;
        dev->db_size = out_params.dpi_size;
        dev->dpi = out_params.dpi;

	qlnxr_set_device_attr(dev);

	QL_DPRINT12(ha,
		"cdev->doorbells = %p, db_phys_addr = %p db_size = 0x%x\n",
		(void *)ha->cdev.doorbells,
		(void *)ha->cdev.db_phys_addr, ha->cdev.db_size);

	QL_DPRINT12(ha,
		"db_addr = %p db_phys_addr = %p db_size = 0x%x dpi = 0x%x\n",
		(void *)dev->db_addr, (void *)dev->db_phys_addr,
		dev->db_size, dev->dpi);
out:
        kfree(in_params);

	QL_DPRINT12(ha, "exit\n");
        return rc;
}

static void
qlnxr_build_sgid_mac(union ib_gid *sgid, unsigned char *mac_addr,
	bool is_vlan, u16 vlan_id)
{
	sgid->global.subnet_prefix = OSAL_CPU_TO_BE64(0xfe80000000000000LL);
	sgid->raw[8] = mac_addr[0] ^ 2;
	sgid->raw[9] = mac_addr[1];
	sgid->raw[10] = mac_addr[2];
	if (is_vlan) {
		sgid->raw[11] = vlan_id >> 8;
		sgid->raw[12] = vlan_id & 0xff;
	} else {
		sgid->raw[11] = 0xff;
		sgid->raw[12] = 0xfe;
	}
	sgid->raw[13] = mac_addr[3];
	sgid->raw[14] = mac_addr[4];
	sgid->raw[15] = mac_addr[5];
}
static bool
qlnxr_add_sgid(struct qlnxr_dev *dev, union ib_gid *new_sgid);

static void
qlnxr_add_ip_based_gid(struct qlnxr_dev *dev, struct ifnet *ifp)
{
	struct ifaddr *ifa;
	union ib_gid gid;

	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {

			QL_DPRINT12(dev->ha, "IP address : %x\n", ((struct sockaddr_in *) ifa->ifa_addr)->sin_addr.s_addr);
			ipv6_addr_set_v4mapped(
				((struct sockaddr_in *) ifa->ifa_addr)->sin_addr.s_addr,
				(struct in6_addr *)&gid);
			QL_DPRINT12(dev->ha, "gid generated : %llx\n", gid);

			qlnxr_add_sgid(dev, &gid);
		}
	}
	for (int i = 0; i < 16; i++) {
		QL_DPRINT12(dev->ha, "gid generated : %x\n", gid.raw[i]);
	}
}

static bool
qlnxr_add_sgid(struct qlnxr_dev *dev, union ib_gid *new_sgid)
{
	union ib_gid zero_sgid = { { 0 } };
	int i;
	//unsigned long flags;
	mtx_lock(&dev->sgid_lock);
	for (i = 0; i < QLNXR_MAX_SGID; i++) {
		if (!memcmp(&dev->sgid_tbl[i], &zero_sgid,
				sizeof(union ib_gid))) {
			/* found free entry */
			memcpy(&dev->sgid_tbl[i], new_sgid,
				sizeof(union ib_gid));
			QL_DPRINT12(dev->ha, "copying sgid : %llx\n",
					*new_sgid);
			mtx_unlock(&dev->sgid_lock);
			//TODO ib_dispatch event here?
			return true;
		} else if (!memcmp(&dev->sgid_tbl[i], new_sgid,
				sizeof(union ib_gid))) {
			/* entry already present, no addition required */
			mtx_unlock(&dev->sgid_lock);
			QL_DPRINT12(dev->ha, "sgid present : %llx\n",
					*new_sgid);
			return false;
		}
	}	
	if (i == QLNXR_MAX_SGID) {
		QL_DPRINT12(dev->ha, "didn't find an empty entry in sgid_tbl\n");
	}
	mtx_unlock(&dev->sgid_lock);
	return false;
}

static bool qlnxr_del_sgid(struct qlnxr_dev *dev, union ib_gid *gid)
{
	int found = false;
	int i;
	//unsigned long flags;

	QL_DPRINT12(dev->ha, "removing gid %llx %llx\n",
			gid->global.interface_id,
			gid->global.subnet_prefix);
	mtx_lock(&dev->sgid_lock);
	/* first is the default sgid which cannot be deleted */
	for (i = 1; i < QLNXR_MAX_SGID; i++) {
		if (!memcmp(&dev->sgid_tbl[i], gid, sizeof(union ib_gid))) {
			/* found matching entry */
			memset(&dev->sgid_tbl[i], 0, sizeof(union ib_gid));
			found = true;
			break;
		}
	}
	mtx_unlock(&dev->sgid_lock);

	return found;
}

#if __FreeBSD_version < 1100000

static inline int
is_vlan_dev(struct ifnet *ifp)
{
	return (ifp->if_type == IFT_L2VLAN);
}
 
static inline uint16_t
vlan_dev_vlan_id(struct ifnet *ifp)
{
	uint16_t vtag;

	if (VLAN_TAG(ifp, &vtag) == 0)
		return (vtag);

	return (0);
}

#endif /* #if __FreeBSD_version < 1100000 */

static void
qlnxr_add_sgids(struct qlnxr_dev *dev)
{
	qlnx_host_t *ha = dev->ha;
	u16 vlan_id;
	bool is_vlan;
	union ib_gid vgid;

	qlnxr_add_ip_based_gid(dev, ha->ifp);
	/* MAC/VLAN base GIDs */
	is_vlan = is_vlan_dev(ha->ifp);
       	vlan_id = (is_vlan) ? vlan_dev_vlan_id(ha->ifp) : 0;
	qlnxr_build_sgid_mac(&vgid, ha->primary_mac, is_vlan, vlan_id);
	qlnxr_add_sgid(dev, &vgid);
}

static int
qlnxr_add_default_sgid(struct qlnxr_dev *dev)
{
	/* GID Index 0 - Invariant manufacturer-assigned EUI-64 */
	union ib_gid *sgid = &dev->sgid_tbl[0];
	struct ecore_rdma_device        *qattr;
	qlnx_host_t *ha;
	ha = dev->ha;

	qattr =	ecore_rdma_query_device(dev->rdma_ctx);
	if(sgid == NULL)
		QL_DPRINT12(ha, "sgid = NULL?\n");

	sgid->global.subnet_prefix = OSAL_CPU_TO_BE64(0xfe80000000000000LL);
	QL_DPRINT12(ha, "node_guid = %llx", dev->attr.node_guid);
	memcpy(&sgid->raw[8], &qattr->node_guid,
		sizeof(qattr->node_guid));
	//memcpy(&sgid->raw[8], &dev->attr.node_guid,
	//	sizeof(dev->attr.node_guid));
	QL_DPRINT12(ha, "DEFAULT sgid=[%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x]\n",
                   sgid->raw[0], sgid->raw[1], sgid->raw[2], sgid->raw[3], sgid->raw[4], sgid->raw[5],
                   sgid->raw[6], sgid->raw[7], sgid->raw[8], sgid->raw[9], sgid->raw[10], sgid->raw[11],
                   sgid->raw[12], sgid->raw[13], sgid->raw[14], sgid->raw[15]);
	return 0;
}

static int qlnxr_addr_event (struct qlnxr_dev *dev,
				unsigned long event,
				struct ifnet *ifp,
				union ib_gid *gid)
{
	bool is_vlan = false;
	union ib_gid vgid;
	u16 vlan_id = 0xffff;

	QL_DPRINT12(dev->ha, "Link event occured\n");
	is_vlan = is_vlan_dev(dev->ha->ifp);
	vlan_id = (is_vlan) ? vlan_dev_vlan_id(dev->ha->ifp) : 0;

	switch (event) {
	case NETDEV_UP :
		qlnxr_add_sgid(dev, gid);
		if (is_vlan) {
			qlnxr_build_sgid_mac(&vgid, dev->ha->primary_mac, is_vlan, vlan_id);
			qlnxr_add_sgid(dev, &vgid);
		}
		break;
	case NETDEV_DOWN :
		qlnxr_del_sgid(dev, gid);
		if (is_vlan) {
			qlnxr_build_sgid_mac(&vgid, dev->ha->primary_mac, is_vlan, vlan_id);
			qlnxr_del_sgid(dev, &vgid);
		}
		break;
	default :
		break;
	}
	return 1;
}

static int qlnxr_inetaddr_event(struct notifier_block *notifier,
				unsigned long event, void *ptr)
{
	struct ifaddr *ifa = ptr;
	union ib_gid gid;
	struct qlnxr_dev *dev = container_of(notifier, struct qlnxr_dev, nb_inet);
	qlnx_host_t *ha = dev->ha;

	ipv6_addr_set_v4mapped(
			((struct sockaddr_in *) ifa->ifa_addr)->sin_addr.s_addr,
			(struct in6_addr *)&gid);
	return qlnxr_addr_event(dev, event, ha->ifp, &gid);
}

static int
qlnxr_register_inet(struct qlnxr_dev *dev)
{
	int ret;
	dev->nb_inet.notifier_call = qlnxr_inetaddr_event;
	ret = register_inetaddr_notifier(&dev->nb_inet);
	if (ret) {
		QL_DPRINT12(dev->ha, "Failed to register inetaddr\n");
		return ret;
	}
	/* TODO : add for CONFIG_IPV6) */	
	return 0;	
}

static int
qlnxr_build_sgid_tbl(struct qlnxr_dev *dev)
{
	qlnxr_add_default_sgid(dev);
	qlnxr_add_sgids(dev);
	return 0;
}

static struct qlnx_rdma_if qlnxr_drv;

static void *
qlnxr_add(void *eth_dev)
{
	struct qlnxr_dev *dev;
	int ret;
	//device_t pci_dev;
	qlnx_host_t *ha;

	ha = eth_dev;

	QL_DPRINT12(ha, "enter [ha = %p]\n", ha);

	dev = (struct qlnxr_dev *)ib_alloc_device(sizeof(struct qlnxr_dev));

	if (dev == NULL)
		return (NULL);

	dev->ha = eth_dev;
	dev->cdev = &ha->cdev;
	/* Added to extend Application support */
	dev->pdev = kzalloc(sizeof(struct pci_dev), GFP_KERNEL);

        dev->pdev->dev = *(dev->ha->pci_dev);
        dev->pdev->device = pci_get_device(dev->ha->pci_dev);
        dev->pdev->vendor = pci_get_vendor(dev->ha->pci_dev);

	dev->rdma_ctx = &ha->cdev.hwfns[0];
	dev->wq_multiplier = wq_multiplier;
	dev->num_cnq = QLNX_NUM_CNQ;

	QL_DPRINT12(ha,
		"ha = %p dev = %p ha->cdev = %p\n",
		ha, dev, &ha->cdev);
	QL_DPRINT12(ha,
		"dev->cdev = %p dev->rdma_ctx = %p\n",
		dev->cdev, dev->rdma_ctx);

	ret = qlnxr_alloc_resources(dev);

	if (ret)
		goto qlnxr_add_err;

	ret = qlnxr_setup_irqs(dev);

	if (ret) {
		qlnxr_free_resources(dev);
		goto qlnxr_add_err;
	}

	ret = qlnxr_init_hw(dev);

	if (ret) {
		qlnxr_release_irqs(dev);
		qlnxr_free_resources(dev);
		goto qlnxr_add_err;
	}

	qlnxr_register_device(dev);
	for (int i = 0; i < ARRAY_SIZE(qlnxr_class_attributes); ++i) {
		if (device_create_file(&dev->ibdev.dev, qlnxr_class_attributes[i]))
			goto sysfs_err;
	}
	qlnxr_build_sgid_tbl(dev);
	//ret = qlnxr_register_inet(dev);
	QL_DPRINT12(ha, "exit\n");
	if (!test_and_set_bit(QLNXR_ENET_STATE_BIT, &dev->enet_state)) {
		QL_DPRINT12(ha, "dispatching IB_PORT_ACITVE event\n");
		qlnxr_ib_dispatch_event(dev, QLNXR_PORT,
			IB_EVENT_PORT_ACTIVE);
	}

	return (dev);
sysfs_err:
	for (int i = 0; i < ARRAY_SIZE(qlnxr_class_attributes); ++i) {
		device_remove_file(&dev->ibdev.dev, qlnxr_class_attributes[i]);
	}
	ib_unregister_device(&dev->ibdev);

qlnxr_add_err:
	ib_dealloc_device(&dev->ibdev);

	QL_DPRINT12(ha, "exit failed\n");
	return (NULL);
}

static void
qlnxr_remove_sysfiles(struct qlnxr_dev *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(qlnxr_class_attributes); ++i)
		device_remove_file(&dev->ibdev.dev, qlnxr_class_attributes[i]);
}

static int
qlnxr_remove(void *eth_dev, void *qlnx_rdma_dev)
{
	struct qlnxr_dev *dev;
	qlnx_host_t *ha;

	dev = qlnx_rdma_dev;
	ha = eth_dev;

	if ((ha == NULL) || (dev == NULL))
		return (0);

	QL_DPRINT12(ha, "enter ha = %p qlnx_rdma_dev = %p pd_count = %d\n",
		ha, qlnx_rdma_dev, dev->pd_count);

	qlnxr_ib_dispatch_event(dev, QLNXR_PORT,
		IB_EVENT_PORT_ERR);

	if (QLNX_IS_IWARP(dev)) {
		if (dev->pd_count)
			return (EBUSY);
	}

	ib_unregister_device(&dev->ibdev);
	
	if (QLNX_IS_ROCE(dev)) {
		if (dev->pd_count)
			return (EBUSY);
	}

	ecore_rdma_remove_user(dev->rdma_ctx, dev->dpi);
	ecore_rdma_stop(dev->rdma_ctx);

	qlnxr_release_irqs(dev);

	qlnxr_free_resources(dev);

	qlnxr_remove_sysfiles(dev);
	ib_dealloc_device(&dev->ibdev);

	QL_DPRINT12(ha, "exit ha = %p qlnx_rdma_dev = %p\n", ha, qlnx_rdma_dev);
	return (0);
}

int
qlnx_rdma_ll2_set_mac_filter(void *rdma_ctx, uint8_t *old_mac_address,
	uint8_t *new_mac_address)
{
        struct ecore_hwfn *p_hwfn = rdma_ctx;
        struct qlnx_host *ha;
        int ret = 0;

        ha = (struct qlnx_host *)(p_hwfn->p_dev);
        QL_DPRINT2(ha, "enter rdma_ctx (%p)\n", rdma_ctx);

        if (old_mac_address)
                ecore_llh_remove_mac_filter(p_hwfn->p_dev, 0, old_mac_address);

        if (new_mac_address)
                ret = ecore_llh_add_mac_filter(p_hwfn->p_dev, 0, new_mac_address);

        QL_DPRINT2(ha, "exit rdma_ctx (%p)\n", rdma_ctx);
        return (ret);
}

static void
qlnxr_mac_address_change(struct qlnxr_dev *dev)
{
	qlnx_host_t *ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter/exit\n");

	return;
}

static void
qlnxr_notify(void *eth_dev, void *qlnx_rdma_dev, enum qlnx_rdma_event event)
{
	struct qlnxr_dev *dev;
	qlnx_host_t *ha;

	dev = qlnx_rdma_dev;

	if (dev == NULL)
		return;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter (%p, %d)\n", qlnx_rdma_dev, event);

        switch (event) {

        case QLNX_ETHDEV_UP:
		if (!test_and_set_bit(QLNXR_ENET_STATE_BIT, &dev->enet_state))
			qlnxr_ib_dispatch_event(dev, QLNXR_PORT,
				IB_EVENT_PORT_ACTIVE);
                break;

        case QLNX_ETHDEV_CHANGE_ADDR:
                qlnxr_mac_address_change(dev);
                break;

        case QLNX_ETHDEV_DOWN:
		if (test_and_set_bit(QLNXR_ENET_STATE_BIT, &dev->enet_state))
			qlnxr_ib_dispatch_event(dev, QLNXR_PORT,
				IB_EVENT_PORT_ERR);
                break;
        }

	QL_DPRINT12(ha, "exit (%p, %d)\n", qlnx_rdma_dev, event);
	return;
}

static int
qlnxr_mod_load(void)
{
	int ret;


	qlnxr_drv.add = qlnxr_add;
	qlnxr_drv.remove = qlnxr_remove;
	qlnxr_drv.notify = qlnxr_notify;

	ret = qlnx_rdma_register_if(&qlnxr_drv);

	return (0);
}

static int
qlnxr_mod_unload(void)
{
	int ret;

	ret = qlnx_rdma_deregister_if(&qlnxr_drv);
	return (ret);
}

static int
qlnxr_event_handler(module_t mod, int event, void *arg)
{

	int ret = 0;

	switch (event) {

	case MOD_LOAD:
		ret = qlnxr_mod_load();
		break;

	case MOD_UNLOAD:
		ret = qlnxr_mod_unload();
		break;

	default:
		break;
	}

        return (ret);
}

static moduledata_t qlnxr_mod_info = {
	.name = "qlnxr",
	.evhand = qlnxr_event_handler,
};

MODULE_VERSION(qlnxr, 1);
MODULE_DEPEND(qlnxr, if_qlnxe, 1, 1, 1);
MODULE_DEPEND(qlnxr, ibcore, 1, 1, 1);

#if __FreeBSD_version >= 1100000
MODULE_DEPEND(qlnxr, linuxkpi, 1, 1, 1);
#endif /* #if __FreeBSD_version >= 1100000 */

DECLARE_MODULE(qlnxr, qlnxr_mod_info, SI_SUB_LAST, SI_ORDER_ANY);

