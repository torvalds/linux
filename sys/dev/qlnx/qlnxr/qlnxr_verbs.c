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
 * File: qlnxr_verbs.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qlnxr_def.h"
#include "rdma_common.h"
#include "qlnxr_roce.h"
#include "qlnxr_cm.h"

#define upper_32_bits(x) (uint32_t)(x >> 32)
#define lower_32_bits(x) (uint32_t)(x)
#define HILO_U64(hi, lo)		((((u64)(hi)) << 32) + (lo))

#define TYPEPTR_ADDR_SET(type_ptr, field, vaddr)			\
	do {								\
		(type_ptr)->field.hi = cpu_to_le32(upper_32_bits(vaddr));\
		(type_ptr)->field.lo = cpu_to_le32(lower_32_bits(vaddr));\
	} while (0)


#define RQ_SGE_SET(sge, vaddr, vlength, vflags)			\
	do {							\
		TYPEPTR_ADDR_SET(sge, addr, vaddr);		\
		(sge)->length = cpu_to_le32(vlength);		\
		(sge)->flags = cpu_to_le32(vflags);		\
	} while (0)

#define SRQ_HDR_SET(hdr, vwr_id, num_sge)			\
	do {							\
		TYPEPTR_ADDR_SET(hdr, wr_id, vwr_id);		\
		(hdr)->num_sges = num_sge;			\
	} while (0)

#define SRQ_SGE_SET(sge, vaddr, vlength, vlkey)			\
	do {							\
		TYPEPTR_ADDR_SET(sge, addr, vaddr);		\
		(sge)->length = cpu_to_le32(vlength);		\
		(sge)->l_key = cpu_to_le32(vlkey);		\
	} while (0)

#define NIPQUAD(addr) \
	((unsigned char *)&addr)[0], \
	((unsigned char *)&addr)[1], \
	((unsigned char *)&addr)[2], \
	((unsigned char *)&addr)[3]

static int
qlnxr_check_srq_params(struct ib_pd *ibpd,
	struct qlnxr_dev *dev,
	struct ib_srq_init_attr *attrs);

static int
qlnxr_init_srq_user_params(struct ib_ucontext *ib_ctx,
	struct qlnxr_srq *srq,
	struct qlnxr_create_srq_ureq *ureq,
	int access, int dmasync);

static int
qlnxr_alloc_srq_kernel_params(struct qlnxr_srq *srq,
	struct qlnxr_dev *dev,
	struct ib_srq_init_attr *init_attr);


static int
qlnxr_copy_srq_uresp(struct qlnxr_dev *dev,
	struct qlnxr_srq *srq,
	struct ib_udata *udata);

static void
qlnxr_free_srq_user_params(struct qlnxr_srq *srq);

static void
qlnxr_free_srq_kernel_params(struct qlnxr_srq *srq);


static u32
qlnxr_srq_elem_left(struct qlnxr_srq_hwq_info *hw_srq);

int
qlnxr_iw_query_gid(struct ib_device *ibdev, u8 port, int index,
	union ib_gid *sgid)
{
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	memset(sgid->raw, 0, sizeof(sgid->raw));

	memcpy(sgid->raw, dev->ha->primary_mac, sizeof (dev->ha->primary_mac));

	QL_DPRINT12(ha, "exit\n");

	return 0;
}

int
qlnxr_query_gid(struct ib_device *ibdev, u8 port, int index,
	union ib_gid *sgid)
{
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;
	QL_DPRINT12(ha, "enter index: %d\n", index);
#if 0
	int ret = 0;
	/* @@@: if DEFINE_ROCE_GID_TABLE to be used here */
	//if (!rdma_cap_roce_gid_table(ibdev, port)) {
	if (!(rdma_protocol_roce(ibdev, port) &&
		ibdev->add_gid && ibdev->del_gid)) {
		QL_DPRINT11(ha, "acquire gid failed\n");
		return -ENODEV;
	}

	ret = ib_get_cached_gid(ibdev, port, index, sgid, NULL);
	if (ret == -EAGAIN) {
		memcpy(sgid, &zgid, sizeof(*sgid));
		return 0;
	}
#endif
	if ((index >= QLNXR_MAX_SGID) || (index < 0)) {
		QL_DPRINT12(ha, "invalid gid index %d\n", index);
		memset(sgid, 0, sizeof(*sgid));
		return -EINVAL;
	}
	memcpy(sgid, &dev->sgid_tbl[index], sizeof(*sgid));

	QL_DPRINT12(ha, "exit : %p\n", sgid);

	return 0;
}

struct ib_srq *
qlnxr_create_srq(struct ib_pd *ibpd, struct ib_srq_init_attr *init_attr,
	struct ib_udata *udata)
{
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;
	struct ecore_rdma_destroy_srq_in_params destroy_in_params;
	struct ecore_rdma_create_srq_out_params out_params;
	struct ecore_rdma_create_srq_in_params in_params;
	u64 pbl_base_addr, phy_prod_pair_addr;
	struct qlnxr_pd *pd = get_qlnxr_pd(ibpd);
	struct ib_ucontext *ib_ctx = NULL;
	struct qlnxr_srq_hwq_info *hw_srq;
	struct qlnxr_ucontext *ctx = NULL;
	struct qlnxr_create_srq_ureq ureq;
	u32 page_cnt, page_size;
	struct qlnxr_srq *srq;
	int ret = 0;

	dev = get_qlnxr_dev((ibpd->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	ret = qlnxr_check_srq_params(ibpd, dev, init_attr);

	srq = kzalloc(sizeof(*srq), GFP_KERNEL);
	if (!srq) {
		QL_DPRINT11(ha, "cannot allocate memory for srq\n");
		return NULL; //@@@ : TODO what to return here?
	}

	srq->dev = dev;
	hw_srq = &srq->hw_srq;
	spin_lock_init(&srq->lock);
	memset(&in_params, 0, sizeof(in_params));

	if (udata && ibpd->uobject && ibpd->uobject->context) {
		ib_ctx = ibpd->uobject->context;
		ctx = get_qlnxr_ucontext(ib_ctx);

		memset(&ureq, 0, sizeof(ureq));
		if (ib_copy_from_udata(&ureq, udata, min(sizeof(ureq),
			udata->inlen))) {
			QL_DPRINT11(ha, "problem"
				" copying data from user space\n");
			goto err0;
		}

		ret = qlnxr_init_srq_user_params(ib_ctx, srq, &ureq, 0, 0);
		if (ret)
			goto err0;

		page_cnt = srq->usrq.pbl_info.num_pbes;
		pbl_base_addr = srq->usrq.pbl_tbl->pa;
		phy_prod_pair_addr = hw_srq->phy_prod_pair_addr;
		// @@@ : if DEFINE_IB_UMEM_PAGE_SHIFT
		// page_size = BIT(srq->usrq.umem->page_shift);
		// else
		page_size = srq->usrq.umem->page_size;
	} else {
		struct ecore_chain *pbl;
		ret = qlnxr_alloc_srq_kernel_params(srq, dev, init_attr);
		if (ret)
			goto err0;
		pbl = &hw_srq->pbl;

		page_cnt = ecore_chain_get_page_cnt(pbl);
		pbl_base_addr = ecore_chain_get_pbl_phys(pbl);
		phy_prod_pair_addr = hw_srq->phy_prod_pair_addr;
		page_size = pbl->elem_per_page << 4;
	}

	in_params.pd_id = pd->pd_id;
	in_params.pbl_base_addr = pbl_base_addr;
	in_params.prod_pair_addr = phy_prod_pair_addr;
	in_params.num_pages = page_cnt;
	in_params.page_size = page_size;

	ret = ecore_rdma_create_srq(dev->rdma_ctx, &in_params, &out_params);
	if (ret)
		goto err1;

	srq->srq_id = out_params.srq_id;

	if (udata) {
		ret = qlnxr_copy_srq_uresp(dev, srq, udata);
		if (ret)
			goto err2;
	}

	QL_DPRINT12(ha, "created srq with srq_id = 0x%0x\n", srq->srq_id);
	return &srq->ibsrq;
err2:
	memset(&in_params, 0, sizeof(in_params));
	destroy_in_params.srq_id = srq->srq_id;
	ecore_rdma_destroy_srq(dev->rdma_ctx, &destroy_in_params);

err1:
	if (udata)
		qlnxr_free_srq_user_params(srq);
	else
		qlnxr_free_srq_kernel_params(srq);

err0:
	kfree(srq);	
	return ERR_PTR(-EFAULT);
}

int
qlnxr_destroy_srq(struct ib_srq *ibsrq)
{
	struct qlnxr_dev	*dev;
	struct qlnxr_srq	*srq;
	qlnx_host_t		*ha;
	struct ecore_rdma_destroy_srq_in_params in_params;

	srq = get_qlnxr_srq(ibsrq);
	dev = srq->dev;
	ha = dev->ha;

	memset(&in_params, 0, sizeof(in_params));
	in_params.srq_id = srq->srq_id;

	ecore_rdma_destroy_srq(dev->rdma_ctx, &in_params);

	if (ibsrq->pd->uobject && ibsrq->pd->uobject->context)
		qlnxr_free_srq_user_params(srq);
	else
		qlnxr_free_srq_kernel_params(srq);

	QL_DPRINT12(ha, "destroyed srq_id=0x%0x\n", srq->srq_id);
	kfree(srq);
	return 0;
}

int
qlnxr_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
	enum ib_srq_attr_mask attr_mask, struct ib_udata *udata)
{
	struct qlnxr_dev	*dev;
	struct qlnxr_srq	*srq;
	qlnx_host_t		*ha;
	struct ecore_rdma_modify_srq_in_params in_params;
	int ret = 0;

	srq = get_qlnxr_srq(ibsrq);
	dev = srq->dev;
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
	if (attr_mask & IB_SRQ_MAX_WR) {
		QL_DPRINT12(ha, "invalid attribute mask=0x%x"
			" specified for %p\n", attr_mask, srq);
		return -EINVAL;
	}

	if (attr_mask & IB_SRQ_LIMIT) {
		if (attr->srq_limit >= srq->hw_srq.max_wr) {
			QL_DPRINT12(ha, "invalid srq_limit=0x%x"
				" (max_srq_limit = 0x%x)\n",
			       attr->srq_limit, srq->hw_srq.max_wr);
			return -EINVAL;	
		}
		memset(&in_params, 0, sizeof(in_params));
		in_params.srq_id = srq->srq_id;
		in_params.wqe_limit = attr->srq_limit;
		ret = ecore_rdma_modify_srq(dev->rdma_ctx, &in_params);
		if (ret)
			return ret;
	}

	QL_DPRINT12(ha, "modified srq with srq_id = 0x%0x\n", srq->srq_id);
	return 0;
}

int
qlnxr_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr)
{
	struct qlnxr_dev	*dev;
	struct qlnxr_srq	*srq;
	qlnx_host_t		*ha;
	struct ecore_rdma_device *qattr;
	srq = get_qlnxr_srq(ibsrq);
	dev = srq->dev;
	ha = dev->ha;
	//qattr = &dev->attr;
	qattr = ecore_rdma_query_device(dev->rdma_ctx);
	QL_DPRINT12(ha, "enter\n");

	if (!dev->rdma_ctx) {
		QL_DPRINT12(ha, "called with invalid params"
			" rdma_ctx is NULL\n");
		return -EINVAL;
	}

	srq_attr->srq_limit = qattr->max_srq;
	srq_attr->max_wr = qattr->max_srq_wr;
	srq_attr->max_sge = qattr->max_sge;

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

/* Increment srq wr producer by one */
static
void qlnxr_inc_srq_wr_prod (struct qlnxr_srq_hwq_info *info)
{
	info->wr_prod_cnt++;
}

/* Increment srq wr consumer by one */
static 
void qlnxr_inc_srq_wr_cons(struct qlnxr_srq_hwq_info *info)
{
        info->wr_cons_cnt++;
}

/* get_port_immutable verb is not available in FreeBSD */
#if 0
int
qlnxr_roce_port_immutable(struct ib_device *ibdev, u8 port_num,
	struct ib_port_immutable *immutable)
{
	struct qlnxr_dev                *dev;
	qlnx_host_t                     *ha;
	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "entered but not implemented!!!\n");
}
#endif

int
qlnxr_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
	struct ib_recv_wr **bad_wr)
{
	struct qlnxr_dev	*dev;
	struct qlnxr_srq	*srq;
	qlnx_host_t		*ha;
	struct qlnxr_srq_hwq_info *hw_srq;
	struct ecore_chain *pbl;
	unsigned long flags;
	int status = 0;
	u32 num_sge, offset;

	srq = get_qlnxr_srq(ibsrq);
	dev = srq->dev;
	ha = dev->ha;
	hw_srq = &srq->hw_srq;

	QL_DPRINT12(ha, "enter\n");
	spin_lock_irqsave(&srq->lock, flags);

	pbl = &srq->hw_srq.pbl;
	while (wr) {
		struct rdma_srq_wqe_header *hdr;
		int i;

		if (!qlnxr_srq_elem_left(hw_srq) ||
		    wr->num_sge > srq->hw_srq.max_sges) {
			QL_DPRINT11(ha, "WR cannot be posted"
			    " (%d, %d) || (%d > %d)\n",
			    hw_srq->wr_prod_cnt, hw_srq->wr_cons_cnt,
			    wr->num_sge, srq->hw_srq.max_sges);
			status = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		hdr = ecore_chain_produce(pbl);
		num_sge = wr->num_sge;
		/* Set number of sge and WR id in header */
		SRQ_HDR_SET(hdr, wr->wr_id, num_sge);

                /* PBL is maintained in case of WR granularity.
                 * So increment WR producer in case we post a WR.
                 */
		qlnxr_inc_srq_wr_prod(hw_srq);
		hw_srq->wqe_prod++;
		hw_srq->sge_prod++;

		QL_DPRINT12(ha, "SRQ WR : SGEs: %d with wr_id[%d] = %llx\n",
			wr->num_sge, hw_srq->wqe_prod, wr->wr_id);

		for (i = 0; i < wr->num_sge; i++) {
			struct rdma_srq_sge *srq_sge = 
			    ecore_chain_produce(pbl);
			/* Set SGE length, lkey and address */
			SRQ_SGE_SET(srq_sge, wr->sg_list[i].addr,
				wr->sg_list[i].length, wr->sg_list[i].lkey);

			QL_DPRINT12(ha, "[%d]: len %d, key %x, addr %x:%x\n",
				i, srq_sge->length, srq_sge->l_key,
				srq_sge->addr.hi, srq_sge->addr.lo);
			hw_srq->sge_prod++;
		}
		wmb();
		/*
		 * SRQ prod is 8 bytes. Need to update SGE prod in index
		 * in first 4 bytes and need to update WQE prod in next
		 * 4 bytes.
		 */
		*(srq->hw_srq.virt_prod_pair_addr) = hw_srq->sge_prod;
		offset = offsetof(struct rdma_srq_producers, wqe_prod);
		*((u8 *)srq->hw_srq.virt_prod_pair_addr + offset) =
			hw_srq->wqe_prod;
		/* Flush prod after updating it */
		wmb();
		wr = wr->next;
	}	

	QL_DPRINT12(ha, "Elements in SRQ: %d\n",
		ecore_chain_get_elem_left(pbl));

	spin_unlock_irqrestore(&srq->lock, flags);	
	QL_DPRINT12(ha, "exit\n");
	return status;
}

int
#if __FreeBSD_version < 1102000
qlnxr_query_device(struct ib_device *ibdev, struct ib_device_attr *attr)
#else
qlnxr_query_device(struct ib_device *ibdev, struct ib_device_attr *attr,
	struct ib_udata *udata)
#endif /* #if __FreeBSD_version < 1102000 */

{
	struct qlnxr_dev		*dev;
	struct ecore_rdma_device	*qattr;
	qlnx_host_t			*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

#if __FreeBSD_version > 1102000
	if (udata->inlen || udata->outlen)
		return -EINVAL;
#endif /* #if __FreeBSD_version > 1102000 */

	if (dev->rdma_ctx == NULL) {
		return -EINVAL;
	}

	qattr = ecore_rdma_query_device(dev->rdma_ctx);

	memset(attr, 0, sizeof *attr);

	attr->fw_ver = qattr->fw_ver;
	attr->sys_image_guid = qattr->sys_image_guid;
	attr->max_mr_size = qattr->max_mr_size;
	attr->page_size_cap = qattr->page_size_caps;
	attr->vendor_id = qattr->vendor_id;
	attr->vendor_part_id = qattr->vendor_part_id;
	attr->hw_ver = qattr->hw_ver;
	attr->max_qp = qattr->max_qp;
	attr->device_cap_flags = IB_DEVICE_CURR_QP_STATE_MOD |
					IB_DEVICE_RC_RNR_NAK_GEN |
					IB_DEVICE_LOCAL_DMA_LKEY |
					IB_DEVICE_MEM_MGT_EXTENSIONS;

	attr->max_sge = qattr->max_sge;
	attr->max_sge_rd = qattr->max_sge;
	attr->max_cq = qattr->max_cq;
	attr->max_cqe = qattr->max_cqe;
	attr->max_mr = qattr->max_mr;
	attr->max_mw = qattr->max_mw;
	attr->max_pd = qattr->max_pd;
	attr->atomic_cap = dev->atomic_cap;
	attr->max_fmr = qattr->max_fmr;
	attr->max_map_per_fmr = 16; /* TBD: FMR */

	/* There is an implicit assumption in some of the ib_xxx apps that the
	 * qp_rd_atom is smaller than the qp_init_rd_atom. Specifically, in
	 * communication the qp_rd_atom is passed to the other side and used as
	 * init_rd_atom without check device capabilities for init_rd_atom.
	 * for this reason, we set the qp_rd_atom to be the minimum between the
	 * two...There is an additional assumption in mlx4 driver that the
	 * values are power of two, fls is performed on the value - 1, which
	 * in fact gives a larger power of two for values which are not a power
	 * of two. This should be fixed in mlx4 driver, but until then ->
	 * we provide a value that is a power of two in our code.
	 */
	attr->max_qp_init_rd_atom =
		1 << (fls(qattr->max_qp_req_rd_atomic_resc) - 1);
	attr->max_qp_rd_atom =
		min(1 << (fls(qattr->max_qp_resp_rd_atomic_resc) - 1),
		    attr->max_qp_init_rd_atom);

	attr->max_srq = qattr->max_srq;
	attr->max_srq_sge = qattr->max_srq_sge;
	attr->max_srq_wr = qattr->max_srq_wr;

	/* TODO: R&D to more properly configure the following */
	attr->local_ca_ack_delay = qattr->dev_ack_delay;
	attr->max_fast_reg_page_list_len = qattr->max_mr/8;
	attr->max_pkeys = QLNXR_ROCE_PKEY_MAX;
	attr->max_ah = qattr->max_ah;

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

static inline void
get_link_speed_and_width(int speed, uint8_t *ib_speed, uint8_t *ib_width)
{
	switch (speed) {
	case 1000:
		*ib_speed = IB_SPEED_SDR;
		*ib_width = IB_WIDTH_1X;
		break;
	case 10000:
		*ib_speed = IB_SPEED_QDR;
		*ib_width = IB_WIDTH_1X;
		break;

	case 20000:
		*ib_speed = IB_SPEED_DDR;
		*ib_width = IB_WIDTH_4X;
		break;

	case 25000:
		*ib_speed = IB_SPEED_EDR;
		*ib_width = IB_WIDTH_1X;
		break;

	case 40000:
		*ib_speed = IB_SPEED_QDR;
		*ib_width = IB_WIDTH_4X;
		break;

	case 50000:
		*ib_speed = IB_SPEED_QDR;
		*ib_width = IB_WIDTH_4X; // TODO doesn't add up to 50...
		break;

	case 100000:
		*ib_speed = IB_SPEED_EDR;
		*ib_width = IB_WIDTH_4X;
		break;

	default:
		/* Unsupported */
		*ib_speed = IB_SPEED_SDR;
		*ib_width = IB_WIDTH_1X;
	}
	return;
}

int
qlnxr_query_port(struct ib_device *ibdev, uint8_t port,
	struct ib_port_attr *attr)
{
	struct qlnxr_dev	*dev;
	struct ecore_rdma_port	*rdma_port;
	qlnx_host_t		*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (port > 1) {
		QL_DPRINT12(ha, "port [%d] > 1 \n", port);
		return -EINVAL;
	}

	if (dev->rdma_ctx == NULL) {
		QL_DPRINT12(ha, "rdma_ctx == NULL\n");
		return -EINVAL;
	}

	rdma_port = ecore_rdma_query_port(dev->rdma_ctx);
	memset(attr, 0, sizeof *attr);

	if (rdma_port->port_state == ECORE_RDMA_PORT_UP) {
		attr->state = IB_PORT_ACTIVE;
		attr->phys_state = 5;
	} else {
		attr->state = IB_PORT_DOWN;
		attr->phys_state = 3;
	}

	attr->max_mtu = IB_MTU_4096;
	attr->active_mtu = iboe_get_mtu(dev->ha->ifp->if_mtu);
	attr->lid = 0;
	attr->lmc = 0;
	attr->sm_lid = 0;
	attr->sm_sl = 0;
	attr->port_cap_flags = 0;

	if (QLNX_IS_IWARP(dev)) {
		attr->gid_tbl_len = 1;
		attr->pkey_tbl_len = 1;
	} else {
		attr->gid_tbl_len = QLNXR_MAX_SGID;
		attr->pkey_tbl_len = QLNXR_ROCE_PKEY_TABLE_LEN;
	}

	attr->bad_pkey_cntr = rdma_port->pkey_bad_counter;
	attr->qkey_viol_cntr = 0;

	get_link_speed_and_width(rdma_port->link_speed,
				 &attr->active_speed, &attr->active_width);

	attr->max_msg_sz = rdma_port->max_msg_size;
	attr->max_vl_num = 4; /* TODO -> figure this one out... */

	QL_DPRINT12(ha, "state = %d phys_state = %d "
		" link_speed = %d active_speed = %d active_width = %d"
		" attr->gid_tbl_len = %d attr->pkey_tbl_len = %d"
		" max_msg_sz = 0x%x max_vl_num = 0x%x \n",
		attr->state, attr->phys_state,
		rdma_port->link_speed, attr->active_speed,
		attr->active_width, attr->gid_tbl_len, attr->pkey_tbl_len,
		attr->max_msg_sz, attr->max_vl_num);

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

int
qlnxr_modify_port(struct ib_device *ibdev, uint8_t port, int mask,
	struct ib_port_modify *props)
{
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (port > 1) {
		QL_DPRINT12(ha, "port (%d) > 1\n", port);
		return -EINVAL;
	}

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

enum rdma_link_layer
qlnxr_link_layer(struct ib_device *ibdev, uint8_t port_num)
{
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "ibdev = %p port_num = 0x%x\n", ibdev, port_num);

        return IB_LINK_LAYER_ETHERNET;
}

struct ib_pd *
qlnxr_alloc_pd(struct ib_device *ibdev, struct ib_ucontext *context,
	struct ib_udata *udata)
{
	struct qlnxr_pd		*pd = NULL;
	u16			pd_id;
	int			rc;
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "ibdev = %p context = %p"
		" udata = %p enter\n", ibdev, context, udata);

	if (dev->rdma_ctx == NULL) {
		QL_DPRINT11(ha, "dev->rdma_ctx = NULL\n");
		rc = -1;
		goto err;
	}

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		rc = -ENOMEM;
		QL_DPRINT11(ha, "kzalloc(pd) = NULL\n");
		goto err;
	}

	rc = ecore_rdma_alloc_pd(dev->rdma_ctx, &pd_id);
	if (rc)	{
		QL_DPRINT11(ha, "ecore_rdma_alloc_pd failed\n");
		goto err;
	}

	pd->pd_id = pd_id;

	if (udata && context) {

		rc = ib_copy_to_udata(udata, &pd->pd_id, sizeof(pd->pd_id));
		if (rc) {
			QL_DPRINT11(ha, "ib_copy_to_udata failed\n");
			ecore_rdma_free_pd(dev->rdma_ctx, pd_id);
			goto err;
		}

		pd->uctx = get_qlnxr_ucontext(context);
		pd->uctx->pd = pd;
	}

	atomic_add_rel_32(&dev->pd_count, 1);
	QL_DPRINT12(ha, "exit [pd, pd_id, pd_count] = [%p, 0x%x, %d]\n",
		pd, pd_id, dev->pd_count);

	return &pd->ibpd;

err:
	kfree(pd);
	QL_DPRINT12(ha, "exit -1\n");
	return ERR_PTR(rc);
}

int
qlnxr_dealloc_pd(struct ib_pd *ibpd)
{
	struct qlnxr_pd		*pd;
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;

	pd = get_qlnxr_pd(ibpd);
	dev = get_qlnxr_dev((ibpd->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (pd == NULL) {
		QL_DPRINT11(ha, "pd = NULL\n");
	} else {
		ecore_rdma_free_pd(dev->rdma_ctx, pd->pd_id);
		kfree(pd);
		atomic_subtract_rel_32(&dev->pd_count, 1);
		QL_DPRINT12(ha, "exit [pd, pd_id, pd_count] = [%p, 0x%x, %d]\n",
			pd, pd->pd_id, dev->pd_count);
	}

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

#define ROCE_WQE_ELEM_SIZE	sizeof(struct rdma_sq_sge)
#define	RDMA_MAX_SGE_PER_SRQ	(4) /* Should be part of HSI */
/* Should be part of HSI */
#define RDMA_MAX_SRQ_WQE_SIZE	(RDMA_MAX_SGE_PER_SRQ + 1) /* +1 for header */
#define DB_ADDR_SHIFT(addr)		((addr) << DB_PWM_ADDR_OFFSET_SHIFT)

static void qlnxr_cleanup_user(struct qlnxr_dev *, struct qlnxr_qp *);
static void qlnxr_cleanup_kernel(struct qlnxr_dev *, struct qlnxr_qp *);

int
qlnxr_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	struct qlnxr_dev	*dev;
	qlnx_host_t		*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter index = 0x%x\n", index);

	if (index > QLNXR_ROCE_PKEY_TABLE_LEN) 
		return -EINVAL;

	*pkey = QLNXR_ROCE_PKEY_DEFAULT;

	QL_DPRINT12(ha, "exit\n");
	return 0;
}


static inline bool
qlnxr_get_vlan_id_qp(qlnx_host_t *ha, struct ib_qp_attr *attr, int attr_mask,
       u16 *vlan_id)
{
	bool ret = false;

	QL_DPRINT12(ha, "enter \n");

	*vlan_id = 0;

#if __FreeBSD_version >= 1100000
	u16 tmp_vlan_id;

#if __FreeBSD_version >= 1102000
	union ib_gid *dgid;

	dgid = &attr->ah_attr.grh.dgid;
	tmp_vlan_id = (dgid->raw[11] << 8) | dgid->raw[12];

	if (!(tmp_vlan_id & ~EVL_VLID_MASK)) {
		*vlan_id = tmp_vlan_id;
		ret = true;
	}
#else
	tmp_vlan_id = attr->vlan_id;

	if ((attr_mask & IB_QP_VID) && (!(tmp_vlan_id & ~EVL_VLID_MASK))) {
		*vlan_id = tmp_vlan_id;
		ret = true;
	}

#endif /* #if __FreeBSD_version > 1102000 */

#else
	ret = true;

#endif /* #if __FreeBSD_version >= 1100000 */

	QL_DPRINT12(ha, "exit vlan_id = 0x%x ret = %d \n", *vlan_id, ret);

	return (ret);
}

static inline void
get_gid_info(struct ib_qp *ibqp, struct ib_qp_attr *attr,
	int attr_mask,
	struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct ecore_rdma_modify_qp_in_params *qp_params)
{
	int		i;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	memcpy(&qp_params->sgid.bytes[0],
	       &dev->sgid_tbl[qp->sgid_idx].raw[0],
	       sizeof(qp_params->sgid.bytes));
	memcpy(&qp_params->dgid.bytes[0],
	       &attr->ah_attr.grh.dgid.raw[0],
	       sizeof(qp_params->dgid));

	qlnxr_get_vlan_id_qp(ha, attr, attr_mask, &qp_params->vlan_id);

	for (i = 0; i < (sizeof(qp_params->sgid.dwords)/sizeof(uint32_t)); i++) {
		qp_params->sgid.dwords[i] = ntohl(qp_params->sgid.dwords[i]);
		qp_params->dgid.dwords[i] = ntohl(qp_params->dgid.dwords[i]);
	}

	QL_DPRINT12(ha, "exit\n");
	return;
}



static int
qlnxr_add_mmap(struct qlnxr_ucontext *uctx, u64 phy_addr, unsigned long len)
{
	struct qlnxr_mm	*mm;
	qlnx_host_t	*ha;

	ha = uctx->dev->ha;

	QL_DPRINT12(ha, "enter\n");

	mm = kzalloc(sizeof(*mm), GFP_KERNEL);
	if (mm == NULL) {
		QL_DPRINT11(ha, "mm = NULL\n");
		return -ENOMEM;
	}

	mm->key.phy_addr = phy_addr;

	/* This function might be called with a length which is not a multiple
	 * of PAGE_SIZE, while the mapping is PAGE_SIZE grained and the kernel
	 * forces this granularity by increasing the requested size if needed.
	 * When qedr_mmap is called, it will search the list with the updated
	 * length as a key. To prevent search failures, the length is rounded up
	 * in advance to PAGE_SIZE.
	 */
	mm->key.len = roundup(len, PAGE_SIZE);
	INIT_LIST_HEAD(&mm->entry);

	mutex_lock(&uctx->mm_list_lock);
	list_add(&mm->entry, &uctx->mm_head);
	mutex_unlock(&uctx->mm_list_lock);

	QL_DPRINT12(ha, "added (addr=0x%llx,len=0x%lx) for ctx=%p\n",
		(unsigned long long)mm->key.phy_addr,
		(unsigned long)mm->key.len, uctx);

	return 0;
}

static bool
qlnxr_search_mmap(struct qlnxr_ucontext *uctx, u64 phy_addr, unsigned long len)
{
	bool		found = false;
	struct qlnxr_mm	*mm;
	qlnx_host_t	*ha;

	ha = uctx->dev->ha;

	QL_DPRINT12(ha, "enter\n");

	mutex_lock(&uctx->mm_list_lock);
	list_for_each_entry(mm, &uctx->mm_head, entry) {
		if (len != mm->key.len || phy_addr != mm->key.phy_addr)
			continue;

		found = true;
		break;
	}
	mutex_unlock(&uctx->mm_list_lock);

	QL_DPRINT12(ha,
		"searched for (addr=0x%llx,len=0x%lx) for ctx=%p, found=%d\n",
		mm->key.phy_addr, mm->key.len, uctx, found);

	return found;
}

struct
ib_ucontext *qlnxr_alloc_ucontext(struct ib_device *ibdev,
                struct ib_udata *udata)
{
        int rc;
        struct qlnxr_ucontext *ctx;
        struct qlnxr_alloc_ucontext_resp uresp;
        struct qlnxr_dev *dev = get_qlnxr_dev(ibdev);
        qlnx_host_t *ha = dev->ha;
        struct ecore_rdma_add_user_out_params oparams;

        if (!udata) {
                return ERR_PTR(-EFAULT);
        }

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	rc = ecore_rdma_add_user(dev->rdma_ctx, &oparams);
	if (rc) {
		QL_DPRINT12(ha,
			"Failed to allocate a DPI for a new RoCE application "
			",rc = %d. To overcome this, consider to increase "
			"the number of DPIs, increase the doorbell BAR size "
			"or just close unnecessary RoCE applications. In "
			"order to increase the number of DPIs consult the "
			"README\n", rc);
		goto err;
	}

	ctx->dpi = oparams.dpi;
	ctx->dpi_addr = oparams.dpi_addr;
	ctx->dpi_phys_addr = oparams.dpi_phys_addr;
	ctx->dpi_size = oparams.dpi_size;
	INIT_LIST_HEAD(&ctx->mm_head);
	mutex_init(&ctx->mm_list_lock);

	memset(&uresp, 0, sizeof(uresp));
	uresp.dpm_enabled = offsetof(struct qlnxr_alloc_ucontext_resp, dpm_enabled)
				< udata->outlen ? dev->user_dpm_enabled : 0; //TODO: figure this out
	uresp.wids_enabled = offsetof(struct qlnxr_alloc_ucontext_resp, wids_enabled)
				< udata->outlen ? 1 : 0; //TODO: figure this out
	uresp.wid_count = offsetof(struct qlnxr_alloc_ucontext_resp, wid_count)
				< udata->outlen ? oparams.wid_count : 0; //TODO: figure this out 
        uresp.db_pa = ctx->dpi_phys_addr;
        uresp.db_size = ctx->dpi_size;
        uresp.max_send_wr = dev->attr.max_sqe;
        uresp.max_recv_wr = dev->attr.max_rqe;
        uresp.max_srq_wr = dev->attr.max_srq_wr;
        uresp.sges_per_send_wr = QLNXR_MAX_SQE_ELEMENTS_PER_SQE;
        uresp.sges_per_recv_wr = QLNXR_MAX_RQE_ELEMENTS_PER_RQE;
        uresp.sges_per_srq_wr = dev->attr.max_srq_sge;
        uresp.max_cqes = QLNXR_MAX_CQES;
	
	rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (rc)
		goto err;

	ctx->dev = dev;

	rc = qlnxr_add_mmap(ctx, ctx->dpi_phys_addr, ctx->dpi_size);
	if (rc)
		goto err;
	QL_DPRINT12(ha, "Allocated user context %p\n",
		&ctx->ibucontext);
	
	return &ctx->ibucontext;
err:
	kfree(ctx);
	return ERR_PTR(rc);
}

int
qlnxr_dealloc_ucontext(struct ib_ucontext *ibctx)
{
        struct qlnxr_ucontext *uctx = get_qlnxr_ucontext(ibctx);
        struct qlnxr_dev *dev = uctx->dev;
        qlnx_host_t *ha = dev->ha;
        struct qlnxr_mm *mm, *tmp;
        int status = 0;

        QL_DPRINT12(ha, "Deallocating user context %p\n",
                        uctx);

        if (dev) {
                ecore_rdma_remove_user(uctx->dev->rdma_ctx, uctx->dpi);
        }

        list_for_each_entry_safe(mm, tmp, &uctx->mm_head, entry) {
                QL_DPRINT12(ha, "deleted addr= 0x%llx, len = 0x%lx for"
                                " ctx=%p\n",
                                mm->key.phy_addr, mm->key.len, uctx);
                list_del(&mm->entry);
                kfree(mm);
        }
        kfree(uctx);
        return status;
}

int
qlnxr_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct qlnxr_ucontext	*ucontext = get_qlnxr_ucontext(context);
	struct qlnxr_dev	*dev = get_qlnxr_dev((context->device));
	unsigned long		vm_page = vma->vm_pgoff << PAGE_SHIFT;
	u64 			unmapped_db;
	unsigned long 		len = (vma->vm_end - vma->vm_start);
	int 			rc = 0;
	bool 			found;
	qlnx_host_t		*ha;

	ha = dev->ha;

#if __FreeBSD_version > 1102000
	unmapped_db = dev->db_phys_addr + (ucontext->dpi * ucontext->dpi_size);
#else
	unmapped_db = dev->db_phys_addr;
#endif /* #if __FreeBSD_version > 1102000 */

	QL_DPRINT12(ha, "qedr_mmap enter vm_page=0x%lx"
		" vm_pgoff=0x%lx unmapped_db=0x%llx db_size=%x, len=%lx\n",
		vm_page, vma->vm_pgoff, unmapped_db,
		dev->db_size, len);

	if ((vma->vm_start & (PAGE_SIZE - 1)) || (len & (PAGE_SIZE - 1))) {
		QL_DPRINT11(ha, "Vma_start not page aligned "
			"vm_start = %ld vma_end = %ld\n", vma->vm_start,
			vma->vm_end);
		return -EINVAL;
	}

	found = qlnxr_search_mmap(ucontext, vm_page, len);
	if (!found) {
		QL_DPRINT11(ha, "Vma_pgoff not found in mapped array = %ld\n",
			vma->vm_pgoff);
		return -EINVAL;
	}

	QL_DPRINT12(ha, "Mapping doorbell bar\n");

#if __FreeBSD_version > 1102000

	if ((vm_page < unmapped_db) ||
		((vm_page + len) > (unmapped_db + ucontext->dpi_size))) {
		QL_DPRINT11(ha, "failed pages are outside of dpi;"
			"page address=0x%lx, unmapped_db=0x%lx, dpi_size=0x%x\n",
			vm_page, unmapped_db, ucontext->dpi_size);
		return -EINVAL;
	}

	if (vma->vm_flags & VM_READ) {
		QL_DPRINT11(ha, "failed mmap, cannot map doorbell bar for read\n");
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	rc = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, len,
			vma->vm_page_prot);

#else

	if ((vm_page >= unmapped_db) && (vm_page <= (unmapped_db +
		dev->db_size))) {

		QL_DPRINT12(ha, "Mapping doorbell bar\n");

		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

		rc = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					    PAGE_SIZE, vma->vm_page_prot);
	} else {
		QL_DPRINT12(ha, "Mapping chains\n");
		rc = io_remap_pfn_range(vma, vma->vm_start,
					 vma->vm_pgoff, len, vma->vm_page_prot);
	}

#endif /* #if __FreeBSD_version > 1102000 */

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

struct ib_mr *
qlnxr_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	struct qlnxr_mr		*mr;
	struct qlnxr_dev	*dev = get_qlnxr_dev((ibpd->device));
	struct qlnxr_pd		*pd = get_qlnxr_pd(ibpd);
	int			rc;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (acc & IB_ACCESS_MW_BIND) {
		QL_DPRINT12(ha, "Unsupported access flags received for dma mr\n");
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		rc = -ENOMEM;
		QL_DPRINT12(ha, "kzalloc(mr) failed %d\n", rc);
		goto err0;
	}

	mr->type = QLNXR_MR_DMA;

	rc = ecore_rdma_alloc_tid(dev->rdma_ctx, &mr->hw_mr.itid);
	if (rc) {
		QL_DPRINT12(ha, "ecore_rdma_alloc_tid failed %d\n", rc);
		goto err1;
	}

	/* index only, 18 bit long, lkey = itid << 8 | key */
	mr->hw_mr.tid_type = ECORE_RDMA_TID_REGISTERED_MR;
	mr->hw_mr.pd = pd->pd_id;
	mr->hw_mr.local_read = 1;
	mr->hw_mr.local_write = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hw_mr.remote_read = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hw_mr.remote_write = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hw_mr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	mr->hw_mr.dma_mr = true;

	rc = ecore_rdma_register_tid(dev->rdma_ctx, &mr->hw_mr);
	if (rc) {
		QL_DPRINT12(ha, "ecore_rdma_register_tid failed %d\n", rc);
		goto err2;
	}

	mr->ibmr.lkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;

	if (mr->hw_mr.remote_write || mr->hw_mr.remote_read ||
		mr->hw_mr.remote_atomic) {
		mr->ibmr.rkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;
	}

	QL_DPRINT12(ha, "lkey = %x\n", mr->ibmr.lkey);

	return &mr->ibmr;

err2:
	ecore_rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);
err1:
	kfree(mr);
err0:
	QL_DPRINT12(ha, "exit [%d]\n", rc);

	return ERR_PTR(rc);
}

static void
qlnxr_free_pbl(struct qlnxr_dev *dev, struct qlnxr_pbl_info *pbl_info,
	struct qlnxr_pbl *pbl)
{
	int		i;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	for (i = 0; i < pbl_info->num_pbls; i++) {
		if (!pbl[i].va)
			continue;
		qlnx_dma_free_coherent(&dev->ha->cdev, pbl[i].va, pbl[i].pa,
			pbl_info->pbl_size);
	}
	kfree(pbl);

	QL_DPRINT12(ha, "exit\n");
	return;
}

#define MIN_FW_PBL_PAGE_SIZE (4*1024)
#define MAX_FW_PBL_PAGE_SIZE (64*1024)

#define NUM_PBES_ON_PAGE(_page_size) (_page_size / sizeof(u64))
#define MAX_PBES_ON_PAGE NUM_PBES_ON_PAGE(MAX_FW_PBL_PAGE_SIZE)
#define MAX_PBES_TWO_LAYER (MAX_PBES_ON_PAGE*MAX_PBES_ON_PAGE)

static struct qlnxr_pbl *
qlnxr_alloc_pbl_tbl(struct qlnxr_dev *dev,
	struct qlnxr_pbl_info *pbl_info, gfp_t flags)
{
	void			*va;
	dma_addr_t		pa;
	dma_addr_t		*pbl_main_tbl;
	struct qlnxr_pbl	*pbl_table;
	int			i, rc = 0;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	pbl_table = kzalloc(sizeof(*pbl_table) * pbl_info->num_pbls, flags);

	if (!pbl_table) {
		QL_DPRINT12(ha, "pbl_table = NULL\n");
		return NULL;
	}

	for (i = 0; i < pbl_info->num_pbls; i++) {
		va = qlnx_dma_alloc_coherent(&dev->ha->cdev, &pa, pbl_info->pbl_size);
		if (!va) {
			QL_DPRINT11(ha, "Failed to allocate pbl#%d\n", i);
			rc = -ENOMEM;
			goto err;
		}
		memset(va, 0, pbl_info->pbl_size);
		pbl_table[i].va = va;
		pbl_table[i].pa = pa;
	}

	/* Two-Layer PBLs, if we have more than one pbl we need to initialize
	 * the first one with physical pointers to all of the rest
	 */
	pbl_main_tbl = (dma_addr_t *)pbl_table[0].va;
	for (i = 0; i < pbl_info->num_pbls - 1; i++)
		pbl_main_tbl[i] = pbl_table[i + 1].pa;

	QL_DPRINT12(ha, "exit\n");
	return pbl_table;

err:
	qlnxr_free_pbl(dev, pbl_info, pbl_table);

	QL_DPRINT12(ha, "exit with error\n");
	return NULL;
}

static int
qlnxr_prepare_pbl_tbl(struct qlnxr_dev *dev,
	struct qlnxr_pbl_info *pbl_info,
	u32 num_pbes,
	int two_layer_capable)
{
	u32		pbl_capacity;
	u32		pbl_size;
	u32		num_pbls;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if ((num_pbes > MAX_PBES_ON_PAGE) && two_layer_capable) {
		if (num_pbes > MAX_PBES_TWO_LAYER) {
			QL_DPRINT11(ha, "prepare pbl table: too many pages %d\n",
				num_pbes);
			return -EINVAL;
		}

		/* calculate required pbl page size */
		pbl_size = MIN_FW_PBL_PAGE_SIZE;
		pbl_capacity = NUM_PBES_ON_PAGE(pbl_size) *
			NUM_PBES_ON_PAGE(pbl_size);

		while (pbl_capacity < num_pbes) {
			pbl_size *= 2;
			pbl_capacity = pbl_size / sizeof(u64);
			pbl_capacity = pbl_capacity * pbl_capacity;
		}

		num_pbls = DIV_ROUND_UP(num_pbes, NUM_PBES_ON_PAGE(pbl_size));
		num_pbls++; /* One for the layer0 ( points to the pbls) */
		pbl_info->two_layered = true;
	} else {
		/* One layered PBL */
		num_pbls = 1;
		pbl_size = max_t(u32, MIN_FW_PBL_PAGE_SIZE, \
				roundup_pow_of_two((num_pbes * sizeof(u64))));
		pbl_info->two_layered = false;
	}

	pbl_info->num_pbls = num_pbls;
	pbl_info->pbl_size = pbl_size;
	pbl_info->num_pbes = num_pbes;

	QL_DPRINT12(ha, "prepare pbl table: num_pbes=%d, num_pbls=%d pbl_size=%d\n",
		pbl_info->num_pbes, pbl_info->num_pbls, pbl_info->pbl_size);

	return 0;
}

#define upper_32_bits(x) (uint32_t)(x >> 32)
#define lower_32_bits(x) (uint32_t)(x)

static void
qlnxr_populate_pbls(struct qlnxr_dev *dev, struct ib_umem *umem,
	struct qlnxr_pbl *pbl, struct qlnxr_pbl_info *pbl_info)
{
	struct regpair		*pbe;
	struct qlnxr_pbl	*pbl_tbl;
	struct scatterlist	*sg;
	int			shift, pg_cnt, pages, pbe_cnt, total_num_pbes = 0;
	qlnx_host_t		*ha;

#ifdef DEFINE_IB_UMEM_WITH_CHUNK
        int                     i;
        struct                  ib_umem_chunk *chunk = NULL;
#else
        int                     entry;
#endif


	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (!pbl_info) {
		QL_DPRINT11(ha, "PBL_INFO not initialized\n");
		return;
	}

	if (!pbl_info->num_pbes) {
		QL_DPRINT11(ha, "pbl_info->num_pbes == 0\n");
		return;
	}

	/* If we have a two layered pbl, the first pbl points to the rest
	 * of the pbls and the first entry lays on the second pbl in the table
	 */
	if (pbl_info->two_layered)
		pbl_tbl = &pbl[1];
	else
		pbl_tbl = pbl;

	pbe = (struct regpair *)pbl_tbl->va;
	if (!pbe) {
		QL_DPRINT12(ha, "pbe is NULL\n");
		return;
	}

	pbe_cnt = 0;

	shift = ilog2(umem->page_size);

#ifndef DEFINE_IB_UMEM_WITH_CHUNK

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {

#else
	list_for_each_entry(chunk, &umem->chunk_list, list) {
		/* get all the dma regions from the chunk. */
		for (i = 0; i < chunk->nmap; i++) {
			sg = &chunk->page_list[i];
#endif
			pages = sg_dma_len(sg) >> shift;
			for (pg_cnt = 0; pg_cnt < pages; pg_cnt++) {
				/* store the page address in pbe */
				pbe->lo =
				    cpu_to_le32(sg_dma_address(sg) +
						(umem->page_size * pg_cnt));
				pbe->hi =
				    cpu_to_le32(upper_32_bits
						((sg_dma_address(sg) +
						  umem->page_size * pg_cnt)));

				QL_DPRINT12(ha,
					"Populate pbl table:"
					" pbe->addr=0x%x:0x%x "
					" pbe_cnt = %d total_num_pbes=%d"
					" pbe=%p\n", pbe->lo, pbe->hi, pbe_cnt,
					total_num_pbes, pbe);

				pbe_cnt ++;
				total_num_pbes ++;
				pbe++;

				if (total_num_pbes == pbl_info->num_pbes)
					return;

				/* if the given pbl is full storing the pbes,
				 * move to next pbl.
				 */
				if (pbe_cnt ==
					(pbl_info->pbl_size / sizeof(u64))) {
					pbl_tbl++;
					pbe = (struct regpair *)pbl_tbl->va;
					pbe_cnt = 0;
				}
			}
#ifdef DEFINE_IB_UMEM_WITH_CHUNK
		}
#endif
	}
	QL_DPRINT12(ha, "exit\n");
	return;
}

static void
free_mr_info(struct qlnxr_dev *dev, struct mr_info *info)
{
	struct qlnxr_pbl *pbl, *tmp;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (info->pbl_table)
		list_add_tail(&info->pbl_table->list_entry,
			      &info->free_pbl_list);

	if (!list_empty(&info->inuse_pbl_list))
		list_splice(&info->inuse_pbl_list, &info->free_pbl_list);

	list_for_each_entry_safe(pbl, tmp, &info->free_pbl_list, list_entry) {
		list_del(&pbl->list_entry);
		qlnxr_free_pbl(dev, &info->pbl_info, pbl);
	}
	QL_DPRINT12(ha, "exit\n");

	return;
}

static int
qlnxr_init_mr_info(struct qlnxr_dev *dev, struct mr_info *info,
	size_t page_list_len, bool two_layered)
{
	int			rc;
	struct qlnxr_pbl	*tmp;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	INIT_LIST_HEAD(&info->free_pbl_list);
	INIT_LIST_HEAD(&info->inuse_pbl_list);

	rc = qlnxr_prepare_pbl_tbl(dev, &info->pbl_info,
				  page_list_len, two_layered);
	if (rc) {
		QL_DPRINT11(ha, "qlnxr_prepare_pbl_tbl [%d]\n", rc);
		goto done;
	}

	info->pbl_table = qlnxr_alloc_pbl_tbl(dev, &info->pbl_info, GFP_KERNEL);

	if (!info->pbl_table) {
		rc = -ENOMEM;
		QL_DPRINT11(ha, "qlnxr_alloc_pbl_tbl returned NULL\n");
		goto done;
	}

	QL_DPRINT12(ha, "pbl_table_pa = %pa\n", &info->pbl_table->pa);

	/* in usual case we use 2 PBLs, so we add one to free
	 * list and allocating another one
	 */
	tmp = qlnxr_alloc_pbl_tbl(dev, &info->pbl_info, GFP_KERNEL);

	if (!tmp) {
		QL_DPRINT11(ha, "Extra PBL is not allocated\n");
		goto done; /* it's OK if second allocation fails, so rc = 0*/
	}

	list_add_tail(&tmp->list_entry, &info->free_pbl_list);

	QL_DPRINT12(ha, "extra pbl_table_pa = %pa\n", &tmp->pa);

done:
	if (rc)
		free_mr_info(dev, info);

	QL_DPRINT12(ha, "exit [%d]\n", rc);

	return rc;
}


struct ib_mr *
#if __FreeBSD_version >= 1102000
qlnxr_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
	u64 usr_addr, int acc, struct ib_udata *udata)
#else
qlnxr_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
	u64 usr_addr, int acc, struct ib_udata *udata, int mr_id)
#endif /* #if __FreeBSD_version >= 1102000 */
{
	int		rc = -ENOMEM;
	struct qlnxr_dev *dev = get_qlnxr_dev((ibpd->device));
	struct qlnxr_mr *mr;
	struct qlnxr_pd *pd;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	pd = get_qlnxr_pd(ibpd);

	QL_DPRINT12(ha, "qedr_register user mr pd = %d"
		" start = %lld, len = %lld, usr_addr = %lld, acc = %d\n",
		pd->pd_id, start, len, usr_addr, acc);

	if (acc & IB_ACCESS_REMOTE_WRITE && !(acc & IB_ACCESS_LOCAL_WRITE)) {
		QL_DPRINT11(ha,
			"(acc & IB_ACCESS_REMOTE_WRITE &&"
			" !(acc & IB_ACCESS_LOCAL_WRITE))\n");
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		QL_DPRINT11(ha, "kzalloc(mr) failed\n");
		return ERR_PTR(rc);
	}

	mr->type = QLNXR_MR_USER;

	mr->umem = ib_umem_get(ibpd->uobject->context, start, len, acc, 0);
	if (IS_ERR(mr->umem)) {
		rc = -EFAULT;
		QL_DPRINT11(ha, "ib_umem_get failed [%p]\n", mr->umem);
		goto err0;
	}

	rc = qlnxr_init_mr_info(dev, &mr->info, ib_umem_page_count(mr->umem), 1);
	if (rc) {
		QL_DPRINT11(ha,
			"qlnxr_init_mr_info failed [%d]\n", rc);
		goto err1;
	}

	qlnxr_populate_pbls(dev, mr->umem, mr->info.pbl_table,
			   &mr->info.pbl_info);

	rc = ecore_rdma_alloc_tid(dev->rdma_ctx, &mr->hw_mr.itid);

	if (rc) {
		QL_DPRINT11(ha, "roce alloc tid returned an error %d\n", rc);
		goto err1;
	}

	/* index only, 18 bit long, lkey = itid << 8 | key */
	mr->hw_mr.tid_type = ECORE_RDMA_TID_REGISTERED_MR;
	mr->hw_mr.key = 0;
	mr->hw_mr.pd = pd->pd_id;
	mr->hw_mr.local_read = 1;
	mr->hw_mr.local_write = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hw_mr.remote_read = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hw_mr.remote_write = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hw_mr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	mr->hw_mr.mw_bind = false; /* TBD MW BIND */
	mr->hw_mr.pbl_ptr = mr->info.pbl_table[0].pa;
	mr->hw_mr.pbl_two_level = mr->info.pbl_info.two_layered;
	mr->hw_mr.pbl_page_size_log = ilog2(mr->info.pbl_info.pbl_size);
	mr->hw_mr.page_size_log = ilog2(mr->umem->page_size); /* for the MR pages */

#if __FreeBSD_version >= 1102000
	mr->hw_mr.fbo = ib_umem_offset(mr->umem);
#else
	mr->hw_mr.fbo = mr->umem->offset;
#endif
	mr->hw_mr.length = len;
	mr->hw_mr.vaddr = usr_addr;
	mr->hw_mr.zbva = false; /* TBD figure when this should be true */
	mr->hw_mr.phy_mr = false; /* Fast MR - True, Regular Register False */
	mr->hw_mr.dma_mr = false;

	rc = ecore_rdma_register_tid(dev->rdma_ctx, &mr->hw_mr);
	if (rc) {
		QL_DPRINT11(ha, "roce register tid returned an error %d\n", rc);
		goto err2;
	}

	mr->ibmr.lkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;
	if (mr->hw_mr.remote_write || mr->hw_mr.remote_read ||
		mr->hw_mr.remote_atomic)
		mr->ibmr.rkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;

	QL_DPRINT12(ha, "register user mr lkey: %x\n", mr->ibmr.lkey);

	return (&mr->ibmr);

err2:
	ecore_rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);
err1:
	qlnxr_free_pbl(dev, &mr->info.pbl_info, mr->info.pbl_table);
err0:
	kfree(mr);

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return (ERR_PTR(rc));
}

int
qlnxr_dereg_mr(struct ib_mr *ib_mr)
{
	struct qlnxr_mr	*mr = get_qlnxr_mr(ib_mr);
	struct qlnxr_dev *dev = get_qlnxr_dev((ib_mr->device));
	int		rc = 0;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if ((mr->type != QLNXR_MR_DMA) && (mr->type != QLNXR_MR_FRMR))
		qlnxr_free_pbl(dev, &mr->info.pbl_info, mr->info.pbl_table);

	/* it could be user registered memory. */
	if (mr->umem)
		ib_umem_release(mr->umem);

	kfree(mr->pages);

	kfree(mr);

	QL_DPRINT12(ha, "exit\n");
	return rc;
}

static int
qlnxr_copy_cq_uresp(struct qlnxr_dev *dev,
	struct qlnxr_cq *cq, struct ib_udata *udata)
{
	struct qlnxr_create_cq_uresp	uresp;
	int				rc;
	qlnx_host_t			*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	memset(&uresp, 0, sizeof(uresp));

	uresp.db_offset = DB_ADDR_SHIFT(DQ_PWM_OFFSET_UCM_RDMA_CQ_CONS_32BIT);
	uresp.icid = cq->icid;

	rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));

	if (rc) {
		QL_DPRINT12(ha, "ib_copy_to_udata error cqid=0x%x[%d]\n",
			cq->icid, rc);
	}

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

static void
consume_cqe(struct qlnxr_cq *cq)
{

	if (cq->latest_cqe == cq->toggle_cqe)
		cq->pbl_toggle ^= RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT_MASK;

	cq->latest_cqe = ecore_chain_consume(&cq->pbl);
}

static inline int
qlnxr_align_cq_entries(int entries)
{
	u64 size, aligned_size;

	/* We allocate an extra entry that we don't report to the FW.
	 * Why?
	 * The CQE size is 32 bytes but the FW writes in chunks of 64 bytes
	 * (for performance purposes). Allocating an extra entry and telling
	 * the FW we have less prevents overwriting the first entry in case of
	 * a wrap i.e. when the FW writes the last entry and the application
	 * hasn't read the first one.
	 */
	size = (entries + 1) * QLNXR_CQE_SIZE;

	/* We align to PAGE_SIZE.
	 * Why?
	 * Since the CQ is going to be mapped and the mapping is anyhow in whole
	 * kernel pages we benefit from the possibly extra CQEs.
	 */
	aligned_size = ALIGN(size, PAGE_SIZE);

	/* note: for CQs created in user space the result of this function
	 * should match the size mapped in user space
	 */
	return (aligned_size / QLNXR_CQE_SIZE);
}

static inline int
qlnxr_init_user_queue(struct ib_ucontext *ib_ctx, struct qlnxr_dev *dev,
	struct qlnxr_userq *q, u64 buf_addr, size_t buf_len,
	int access, int dmasync, int alloc_and_init)
{
	int		page_cnt;
	int		rc;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	q->buf_addr = buf_addr;
	q->buf_len = buf_len;

	QL_DPRINT12(ha, "buf_addr : %llx, buf_len : %x, access : %x"
	      " dmasync : %x\n", q->buf_addr, q->buf_len,
		access, dmasync);	

	q->umem = ib_umem_get(ib_ctx, q->buf_addr, q->buf_len, access, dmasync);

	if (IS_ERR(q->umem)) {
		QL_DPRINT11(ha, "ib_umem_get failed [%lx]\n", PTR_ERR(q->umem));
		return PTR_ERR(q->umem);
	}

	page_cnt = ib_umem_page_count(q->umem);
	rc = qlnxr_prepare_pbl_tbl(dev, &q->pbl_info, page_cnt,
				  0 /* SQ and RQ don't support dual layer pbl.
				     * CQ may, but this is yet uncoded.
				     */);
	if (rc) {
		QL_DPRINT11(ha, "qlnxr_prepare_pbl_tbl failed [%d]\n", rc);
		goto err;
	}

	if (alloc_and_init) {
		q->pbl_tbl = qlnxr_alloc_pbl_tbl(dev, &q->pbl_info, GFP_KERNEL);

		if (!q->pbl_tbl) {
			QL_DPRINT11(ha, "qlnxr_alloc_pbl_tbl failed\n");
			rc = -ENOMEM;
			goto err;
		}

		qlnxr_populate_pbls(dev, q->umem, q->pbl_tbl, &q->pbl_info);
	} else {
		q->pbl_tbl = kzalloc(sizeof(*q->pbl_tbl), GFP_KERNEL);

		if (!q->pbl_tbl) {
			QL_DPRINT11(ha, "qlnxr_alloc_pbl_tbl failed\n");
			rc = -ENOMEM;
			goto err;
		}
	}

	QL_DPRINT12(ha, "exit\n");
	return 0;

err:
	ib_umem_release(q->umem);
	q->umem = NULL;

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

#if __FreeBSD_version >= 1102000

struct ib_cq *
qlnxr_create_cq(struct ib_device *ibdev,
	const struct ib_cq_init_attr *attr,
	struct ib_ucontext *ib_ctx,
	struct ib_udata *udata)

#else 

#if __FreeBSD_version >= 1100000

struct ib_cq *
qlnxr_create_cq(struct ib_device *ibdev,
	struct ib_cq_init_attr *attr,
	struct ib_ucontext *ib_ctx,
	struct ib_udata *udata)

#else

struct ib_cq *
qlnxr_create_cq(struct ib_device *ibdev,
	int entries,
	int vector,
	struct ib_ucontext *ib_ctx,
	struct ib_udata *udata)
#endif /* #if __FreeBSD_version >= 1100000 */

#endif /* #if __FreeBSD_version >= 1102000 */
{
	struct qlnxr_ucontext			*ctx;
	struct ecore_rdma_destroy_cq_out_params destroy_oparams;
	struct ecore_rdma_destroy_cq_in_params	destroy_iparams;
	struct qlnxr_dev			*dev;
	struct ecore_rdma_create_cq_in_params	params;
	struct qlnxr_create_cq_ureq		ureq;

#if __FreeBSD_version >= 1100000
	int					vector = attr->comp_vector;
	int					entries = attr->cqe;
#endif
	struct qlnxr_cq				*cq;
	int					chain_entries, rc, page_cnt;
	u64					pbl_ptr;
	u16					icid;
	qlnx_host_t				*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "called from %s. entries = %d, "
		"vector = %d\n",
		(udata ? "User Lib" : "Kernel"), entries, vector);

        memset(&params, 0, sizeof(struct ecore_rdma_create_cq_in_params));
        memset(&destroy_iparams, 0, sizeof(struct ecore_rdma_destroy_cq_in_params));
        memset(&destroy_oparams, 0, sizeof(struct ecore_rdma_destroy_cq_out_params));

	if (entries > QLNXR_MAX_CQES) {
		QL_DPRINT11(ha,
			"the number of entries %d is too high. "
			"Must be equal or below %d.\n",
			entries, QLNXR_MAX_CQES);
		return ERR_PTR(-EINVAL);
	}
	chain_entries = qlnxr_align_cq_entries(entries);
	chain_entries = min_t(int, chain_entries, QLNXR_MAX_CQES);

	cq = qlnx_zalloc((sizeof(struct qlnxr_cq)));

	if (!cq)
		return ERR_PTR(-ENOMEM);

	if (udata) {
		memset(&ureq, 0, sizeof(ureq));

		if (ib_copy_from_udata(&ureq, udata,
			min(sizeof(ureq), udata->inlen))) {
			QL_DPRINT11(ha, "ib_copy_from_udata failed\n");
			goto err0;
		}

		if (!ureq.len) {
			QL_DPRINT11(ha, "ureq.len == 0\n");
			goto err0;
		}

		cq->cq_type = QLNXR_CQ_TYPE_USER;

		qlnxr_init_user_queue(ib_ctx, dev, &cq->q, ureq.addr, ureq.len,
				     IB_ACCESS_LOCAL_WRITE, 1, 1);

		pbl_ptr = cq->q.pbl_tbl->pa;
		page_cnt = cq->q.pbl_info.num_pbes;
		cq->ibcq.cqe = chain_entries;
	} else {
		cq->cq_type = QLNXR_CQ_TYPE_KERNEL;

                rc = ecore_chain_alloc(&dev->ha->cdev,
                           ECORE_CHAIN_USE_TO_CONSUME,
                           ECORE_CHAIN_MODE_PBL,
                           ECORE_CHAIN_CNT_TYPE_U32,
                           chain_entries,
                           sizeof(union roce_cqe),
                           &cq->pbl, NULL);

		if (rc)
			goto err1;

		page_cnt = ecore_chain_get_page_cnt(&cq->pbl);
		pbl_ptr = ecore_chain_get_pbl_phys(&cq->pbl);
		cq->ibcq.cqe = cq->pbl.capacity;
	}

        params.cq_handle_hi = upper_32_bits((uintptr_t)cq);
        params.cq_handle_lo = lower_32_bits((uintptr_t)cq);
        params.cnq_id = vector;
        params.cq_size = chain_entries - 1;
        params.pbl_num_pages = page_cnt;
        params.pbl_ptr = pbl_ptr;
        params.pbl_two_level = 0;

	if (ib_ctx != NULL) {
		ctx = get_qlnxr_ucontext(ib_ctx);
        	params.dpi = ctx->dpi;
	} else {
        	params.dpi = dev->dpi;
	}

	rc = ecore_rdma_create_cq(dev->rdma_ctx, &params, &icid);
	if (rc)
		goto err2;

	cq->icid = icid;
	cq->sig = QLNXR_CQ_MAGIC_NUMBER;
	spin_lock_init(&cq->cq_lock);

	if (ib_ctx) {
		rc = qlnxr_copy_cq_uresp(dev, cq, udata);
		if (rc)
			goto err3;
	} else {
		/* Generate doorbell address.
		 * Configure bits 3-9 with DQ_PWM_OFFSET_UCM_RDMA_CQ_CONS_32BIT.
		 * TODO: consider moving to device scope as it is a function of
		 *       the device.
		 * TODO: add ifdef if plan to support 16 bit.
		 */
		cq->db_addr = dev->db_addr +
			DB_ADDR_SHIFT(DQ_PWM_OFFSET_UCM_RDMA_CQ_CONS_32BIT);
		cq->db.data.icid = cq->icid;
		cq->db.data.params = DB_AGG_CMD_SET <<
				     RDMA_PWM_VAL32_DATA_AGG_CMD_SHIFT;

		/* point to the very last element, passing it we will toggle */
		cq->toggle_cqe = ecore_chain_get_last_elem(&cq->pbl);
		cq->pbl_toggle = RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT_MASK;

		/* must be different from pbl_toggle */
		cq->latest_cqe = NULL;
		consume_cqe(cq);
		cq->cq_cons = ecore_chain_get_cons_idx_u32(&cq->pbl);
	}

	QL_DPRINT12(ha, "exit icid = 0x%0x, addr = %p,"
		" number of entries = 0x%x\n",
		cq->icid, cq, params.cq_size);
	QL_DPRINT12(ha,"cq_addr = %p\n", cq);
	return &cq->ibcq;

err3:
	destroy_iparams.icid = cq->icid;
	ecore_rdma_destroy_cq(dev->rdma_ctx, &destroy_iparams, &destroy_oparams);
err2:
	if (udata)
		qlnxr_free_pbl(dev, &cq->q.pbl_info, cq->q.pbl_tbl);
	else
		ecore_chain_free(&dev->ha->cdev, &cq->pbl);
err1:
	if (udata)
		ib_umem_release(cq->q.umem);
err0:
	kfree(cq);

	QL_DPRINT12(ha, "exit error\n");

	return ERR_PTR(-EINVAL);
}

int qlnxr_resize_cq(struct ib_cq *ibcq, int new_cnt, struct ib_udata *udata)
{
	int			status = 0;
	struct qlnxr_dev	*dev = get_qlnxr_dev((ibcq->device));
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter/exit\n");

	return status;
}

int
qlnxr_destroy_cq(struct ib_cq *ibcq)
{
	struct qlnxr_dev			*dev = get_qlnxr_dev((ibcq->device));
	struct ecore_rdma_destroy_cq_out_params oparams;
	struct ecore_rdma_destroy_cq_in_params	iparams;
	struct qlnxr_cq				*cq = get_qlnxr_cq(ibcq);
	int					rc = 0;
	qlnx_host_t				*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter cq_id = %d\n", cq->icid);

	cq->destroyed = 1;

	/* TODO: Syncronize irq of the CNQ the CQ belongs to for validation
	 * that all completions with notification are dealt with. The rest
	 * of the completions are not interesting
	 */

	/* GSIs CQs are handled by driver, so they don't exist in the FW */

	if (cq->cq_type != QLNXR_CQ_TYPE_GSI) {

		iparams.icid = cq->icid;

		rc = ecore_rdma_destroy_cq(dev->rdma_ctx, &iparams, &oparams);

		if (rc) {
			QL_DPRINT12(ha, "ecore_rdma_destroy_cq failed cq_id = %d\n",
				cq->icid);
			return rc;
		}

		QL_DPRINT12(ha, "free cq->pbl cq_id = %d\n", cq->icid);
		ecore_chain_free(&dev->ha->cdev, &cq->pbl);
	}

	if (ibcq->uobject && ibcq->uobject->context) {
		qlnxr_free_pbl(dev, &cq->q.pbl_info, cq->q.pbl_tbl);
		ib_umem_release(cq->q.umem);
	}

	cq->sig = ~cq->sig;

	kfree(cq);

	QL_DPRINT12(ha, "exit cq_id = %d\n", cq->icid);

	return rc;
}

static int
qlnxr_check_qp_attrs(struct ib_pd *ibpd,
	struct qlnxr_dev *dev,
	struct ib_qp_init_attr *attrs,
	struct ib_udata *udata)
{
	struct ecore_rdma_device	*qattr;
	qlnx_host_t			*ha;

	qattr = ecore_rdma_query_device(dev->rdma_ctx);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	QL_DPRINT12(ha, "attrs->sq_sig_type = %d\n", attrs->sq_sig_type);
	QL_DPRINT12(ha, "attrs->qp_type = %d\n", attrs->qp_type);
	QL_DPRINT12(ha, "attrs->create_flags = %d\n", attrs->create_flags);

#if __FreeBSD_version < 1102000
	QL_DPRINT12(ha, "attrs->qpg_type = %d\n", attrs->qpg_type);
#endif

	QL_DPRINT12(ha, "attrs->port_num = %d\n", attrs->port_num);
	QL_DPRINT12(ha, "attrs->cap.max_send_wr = 0x%x\n", attrs->cap.max_send_wr);
	QL_DPRINT12(ha, "attrs->cap.max_recv_wr = 0x%x\n", attrs->cap.max_recv_wr);
	QL_DPRINT12(ha, "attrs->cap.max_send_sge = 0x%x\n", attrs->cap.max_send_sge);
	QL_DPRINT12(ha, "attrs->cap.max_recv_sge = 0x%x\n", attrs->cap.max_recv_sge);
	QL_DPRINT12(ha, "attrs->cap.max_inline_data = 0x%x\n",
		attrs->cap.max_inline_data);

#if __FreeBSD_version < 1102000
	QL_DPRINT12(ha, "attrs->cap.qpg_tss_mask_sz = 0x%x\n",
		attrs->cap.qpg_tss_mask_sz);
#endif

	QL_DPRINT12(ha, "\n\nqattr->vendor_id = 0x%x\n", qattr->vendor_id);
	QL_DPRINT12(ha, "qattr->vendor_part_id = 0x%x\n", qattr->vendor_part_id);
	QL_DPRINT12(ha, "qattr->hw_ver = 0x%x\n", qattr->hw_ver);
	QL_DPRINT12(ha, "qattr->fw_ver = %p\n", (void *)qattr->fw_ver);
	QL_DPRINT12(ha, "qattr->node_guid = %p\n", (void *)qattr->node_guid);
	QL_DPRINT12(ha, "qattr->sys_image_guid = %p\n",
		(void *)qattr->sys_image_guid);
	QL_DPRINT12(ha, "qattr->max_cnq = 0x%x\n", qattr->max_cnq);
	QL_DPRINT12(ha, "qattr->max_sge = 0x%x\n", qattr->max_sge);
	QL_DPRINT12(ha, "qattr->max_srq_sge = 0x%x\n", qattr->max_srq_sge);
	QL_DPRINT12(ha, "qattr->max_inline = 0x%x\n", qattr->max_inline);
	QL_DPRINT12(ha, "qattr->max_wqe = 0x%x\n", qattr->max_wqe);
	QL_DPRINT12(ha, "qattr->max_srq_wqe = 0x%x\n", qattr->max_srq_wqe);
	QL_DPRINT12(ha, "qattr->max_qp_resp_rd_atomic_resc = 0x%x\n",
		qattr->max_qp_resp_rd_atomic_resc);
	QL_DPRINT12(ha, "qattr->max_qp_req_rd_atomic_resc = 0x%x\n",
		qattr->max_qp_req_rd_atomic_resc);
	QL_DPRINT12(ha, "qattr->max_dev_resp_rd_atomic_resc = 0x%x\n",
		qattr->max_dev_resp_rd_atomic_resc);
	QL_DPRINT12(ha, "qattr->max_cq = 0x%x\n", qattr->max_cq);
	QL_DPRINT12(ha, "qattr->max_qp = 0x%x\n", qattr->max_qp);
	QL_DPRINT12(ha, "qattr->max_srq = 0x%x\n", qattr->max_srq);
	QL_DPRINT12(ha, "qattr->max_mr = 0x%x\n", qattr->max_mr);
	QL_DPRINT12(ha, "qattr->max_mr_size = %p\n", (void *)qattr->max_mr_size);
	QL_DPRINT12(ha, "qattr->max_cqe = 0x%x\n", qattr->max_cqe);
	QL_DPRINT12(ha, "qattr->max_mw = 0x%x\n", qattr->max_mw);
	QL_DPRINT12(ha, "qattr->max_fmr = 0x%x\n", qattr->max_fmr);
	QL_DPRINT12(ha, "qattr->max_mr_mw_fmr_pbl = 0x%x\n",
		qattr->max_mr_mw_fmr_pbl);
	QL_DPRINT12(ha, "qattr->max_mr_mw_fmr_size = %p\n",
		(void *)qattr->max_mr_mw_fmr_size);
	QL_DPRINT12(ha, "qattr->max_pd = 0x%x\n", qattr->max_pd);
	QL_DPRINT12(ha, "qattr->max_ah = 0x%x\n", qattr->max_ah);
	QL_DPRINT12(ha, "qattr->max_pkey = 0x%x\n", qattr->max_pkey);
	QL_DPRINT12(ha, "qattr->max_srq_wr = 0x%x\n", qattr->max_srq_wr);
	QL_DPRINT12(ha, "qattr->max_stats_queues = 0x%x\n",
		qattr->max_stats_queues);
	//QL_DPRINT12(ha, "qattr->dev_caps = 0x%x\n", qattr->dev_caps);
	QL_DPRINT12(ha, "qattr->page_size_caps = %p\n",
		(void *)qattr->page_size_caps);
	QL_DPRINT12(ha, "qattr->dev_ack_delay = 0x%x\n", qattr->dev_ack_delay);
	QL_DPRINT12(ha, "qattr->reserved_lkey = 0x%x\n", qattr->reserved_lkey);
	QL_DPRINT12(ha, "qattr->bad_pkey_counter = 0x%x\n",
		qattr->bad_pkey_counter);

	if ((attrs->qp_type == IB_QPT_GSI) && udata) {
		QL_DPRINT12(ha, "unexpected udata when creating GSI QP\n");
		return -EINVAL;
	}

	if (udata && !(ibpd->uobject && ibpd->uobject->context)) {
		QL_DPRINT12(ha, "called from user without context\n");
		return -EINVAL;
	}

	/* QP0... attrs->qp_type == IB_QPT_GSI */
	if (attrs->qp_type != IB_QPT_RC && attrs->qp_type != IB_QPT_GSI) {
		QL_DPRINT12(ha, "unsupported qp type=0x%x requested\n", 
			   attrs->qp_type);
		return -EINVAL;
	}
	if (attrs->qp_type == IB_QPT_GSI && attrs->srq) {
		QL_DPRINT12(ha, "cannot create GSI qp with SRQ\n");
		return -EINVAL;
	}
	/* Skip the check for QP1 to support CM size of 128 */
	if (attrs->cap.max_send_wr > qattr->max_wqe) {
		QL_DPRINT12(ha, "cannot create a SQ with %d elements "
			" (max_send_wr=0x%x)\n",
			attrs->cap.max_send_wr, qattr->max_wqe);
		return -EINVAL;
	}
	if (!attrs->srq && (attrs->cap.max_recv_wr > qattr->max_wqe)) {
		QL_DPRINT12(ha, "cannot create a RQ with %d elements"
			" (max_recv_wr=0x%x)\n",
			attrs->cap.max_recv_wr, qattr->max_wqe);
		return -EINVAL;
	}
	if (attrs->cap.max_inline_data > qattr->max_inline) {
		QL_DPRINT12(ha,
			"unsupported inline data size=0x%x "
			"requested (max_inline=0x%x)\n",
			attrs->cap.max_inline_data, qattr->max_inline);
		return -EINVAL;
	}
	if (attrs->cap.max_send_sge > qattr->max_sge) {
		QL_DPRINT12(ha,
			"unsupported send_sge=0x%x "
			"requested (max_send_sge=0x%x)\n",
			attrs->cap.max_send_sge, qattr->max_sge);
		return -EINVAL;
	}
	if (attrs->cap.max_recv_sge > qattr->max_sge) {
		QL_DPRINT12(ha,
			"unsupported recv_sge=0x%x requested "
			" (max_recv_sge=0x%x)\n",
			attrs->cap.max_recv_sge, qattr->max_sge);
		return -EINVAL;
	}
	/* unprivileged user space cannot create special QP */
	if (ibpd->uobject && attrs->qp_type == IB_QPT_GSI) {
		QL_DPRINT12(ha,
			"userspace can't create special QPs of type=0x%x\n",
			attrs->qp_type);
		return -EINVAL;
	}
	/* allow creating only one GSI type of QP */
	if (attrs->qp_type == IB_QPT_GSI && dev->gsi_qp_created) {
		QL_DPRINT12(ha,
			"create qp: GSI special QPs already created.\n");
		return -EINVAL;
	}

	/* verify consumer QPs are not trying to use GSI QP's CQ */
	if ((attrs->qp_type != IB_QPT_GSI) && (dev->gsi_qp_created)) {
		struct qlnxr_cq *send_cq = get_qlnxr_cq(attrs->send_cq);
		struct qlnxr_cq *recv_cq = get_qlnxr_cq(attrs->recv_cq);

		if ((send_cq->cq_type == QLNXR_CQ_TYPE_GSI) ||
		    (recv_cq->cq_type == QLNXR_CQ_TYPE_GSI)) {
			QL_DPRINT11(ha, "consumer QP cannot use GSI CQs.\n");
			return -EINVAL;
		}
	}
	QL_DPRINT12(ha, "exit\n");
	return 0;
}

static int
qlnxr_copy_srq_uresp(struct qlnxr_dev *dev,
	struct qlnxr_srq *srq,
	struct ib_udata *udata)
{
	struct qlnxr_create_srq_uresp	uresp;
	qlnx_host_t			*ha;
	int				rc;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	memset(&uresp, 0, sizeof(uresp));

	uresp.srq_id = srq->srq_id;

	rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

static void
qlnxr_copy_rq_uresp(struct qlnxr_dev *dev,
	struct qlnxr_create_qp_uresp *uresp,
	struct qlnxr_qp *qp)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	/* Return if QP is associated with SRQ instead of RQ */
	QL_DPRINT12(ha, "enter qp->srq = %p\n", qp->srq);

	if (qp->srq)
		return;

	/* iWARP requires two doorbells per RQ. */
	if (QLNX_IS_IWARP(dev)) {

		uresp->rq_db_offset =
			DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_IWARP_RQ_PROD);
		uresp->rq_db2_offset =
			DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_FLAGS);

		QL_DPRINT12(ha, "uresp->rq_db_offset = 0x%x "
			"uresp->rq_db2_offset = 0x%x\n",
			uresp->rq_db_offset, uresp->rq_db2_offset);
	} else {
		uresp->rq_db_offset =
			DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_ROCE_RQ_PROD);
	}
	uresp->rq_icid = qp->icid;

	QL_DPRINT12(ha, "exit\n");
	return;
}

static void
qlnxr_copy_sq_uresp(struct qlnxr_dev *dev,
	struct qlnxr_create_qp_uresp *uresp,
	struct qlnxr_qp *qp)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	uresp->sq_db_offset = DB_ADDR_SHIFT(DQ_PWM_OFFSET_XCM_RDMA_SQ_PROD);

	/* iWARP uses the same cid for rq and sq*/
	if (QLNX_IS_IWARP(dev)) {
		uresp->sq_icid = qp->icid;
		QL_DPRINT12(ha, "uresp->sq_icid = 0x%x\n", uresp->sq_icid);
	} else
		uresp->sq_icid = qp->icid + 1;

	QL_DPRINT12(ha, "exit\n");
	return;
}

static int
qlnxr_copy_qp_uresp(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct ib_udata *udata)
{
	int				rc;
	struct qlnxr_create_qp_uresp	uresp;
	qlnx_host_t			*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter qp->icid =0x%x\n", qp->icid);

	memset(&uresp, 0, sizeof(uresp));
	qlnxr_copy_sq_uresp(dev, &uresp, qp);
	qlnxr_copy_rq_uresp(dev, &uresp, qp);

	uresp.atomic_supported = dev->atomic_cap != IB_ATOMIC_NONE;
	uresp.qp_id = qp->qp_id;

	rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}


static void
qlnxr_set_common_qp_params(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_pd *pd,
	struct ib_qp_init_attr *attrs)
{
	qlnx_host_t			*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	spin_lock_init(&qp->q_lock);

	atomic_set(&qp->refcnt, 1);
	qp->pd = pd;
	qp->sig = QLNXR_QP_MAGIC_NUMBER;
	qp->qp_type = attrs->qp_type;
	qp->max_inline_data = ROCE_REQ_MAX_INLINE_DATA_SIZE;
	qp->sq.max_sges = attrs->cap.max_send_sge;
	qp->state = ECORE_ROCE_QP_STATE_RESET;
	qp->signaled = (attrs->sq_sig_type == IB_SIGNAL_ALL_WR) ? true : false;
	qp->sq_cq = get_qlnxr_cq(attrs->send_cq);
	qp->rq_cq = get_qlnxr_cq(attrs->recv_cq);
	qp->dev = dev;

	if (!attrs->srq) {
		/* QP is associated with RQ instead of SRQ */
		qp->rq.max_sges = attrs->cap.max_recv_sge;
		QL_DPRINT12(ha, "RQ params:\trq_max_sges = %d, rq_cq_id = %d\n",
			qp->rq.max_sges, qp->rq_cq->icid);
	} else {
		qp->srq = get_qlnxr_srq(attrs->srq);
	}

	QL_DPRINT12(ha,
		"QP params:\tpd = %d, qp_type = %d, max_inline_data = %d,"
		" state = %d, signaled = %d, use_srq=%d\n",
		pd->pd_id, qp->qp_type, qp->max_inline_data,
		qp->state, qp->signaled, ((attrs->srq) ? 1 : 0));
	QL_DPRINT12(ha, "SQ params:\tsq_max_sges = %d, sq_cq_id = %d\n",
		qp->sq.max_sges, qp->sq_cq->icid);
	return;
}

static int
qlnxr_check_srq_params(struct ib_pd *ibpd,
	struct qlnxr_dev *dev,
	struct ib_srq_init_attr *attrs)
{
	struct ecore_rdma_device *qattr;
	qlnx_host_t		*ha;

	ha = dev->ha;
	qattr = ecore_rdma_query_device(dev->rdma_ctx);

	QL_DPRINT12(ha, "enter\n");

	if (attrs->attr.max_wr > qattr->max_srq_wqe) {
		QL_DPRINT12(ha, "unsupported srq_wr=0x%x"
			" requested (max_srq_wr=0x%x)\n",
			attrs->attr.max_wr, qattr->max_srq_wr);
		return -EINVAL;
	}

	if (attrs->attr.max_sge > qattr->max_sge) {
		QL_DPRINT12(ha,
			"unsupported sge=0x%x requested (max_srq_sge=0x%x)\n",
			attrs->attr.max_sge, qattr->max_sge);
		return -EINVAL;
	}

	if (attrs->attr.srq_limit > attrs->attr.max_wr) {
		QL_DPRINT12(ha,
		       "unsupported srq_limit=0x%x requested"
			" (max_srq_limit=0x%x)\n",
			attrs->attr.srq_limit, attrs->attr.srq_limit);
		return -EINVAL;
	}

	QL_DPRINT12(ha, "exit\n");
	return 0;
}


static void
qlnxr_free_srq_user_params(struct qlnxr_srq *srq)
{
	struct qlnxr_dev	*dev = srq->dev;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	qlnxr_free_pbl(srq->dev, &srq->usrq.pbl_info, srq->usrq.pbl_tbl);
	ib_umem_release(srq->usrq.umem);
	ib_umem_release(srq->prod_umem);

	QL_DPRINT12(ha, "exit\n");
	return;
}

static void
qlnxr_free_srq_kernel_params(struct qlnxr_srq *srq)
{
	struct qlnxr_srq_hwq_info *hw_srq  = &srq->hw_srq;
	struct qlnxr_dev	*dev = srq->dev;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	ecore_chain_free(dev->cdev, &hw_srq->pbl);

	qlnx_dma_free_coherent(&dev->cdev,
		hw_srq->virt_prod_pair_addr,
		hw_srq->phy_prod_pair_addr,
		sizeof(struct rdma_srq_producers));

	QL_DPRINT12(ha, "exit\n");

	return;
}

static int
qlnxr_init_srq_user_params(struct ib_ucontext *ib_ctx,
	struct qlnxr_srq *srq,
	struct qlnxr_create_srq_ureq *ureq,
	int access, int dmasync)
{
#ifdef DEFINE_IB_UMEM_WITH_CHUNK
	struct ib_umem_chunk	*chunk;
#endif
	struct scatterlist	*sg;
	int			rc;
	struct qlnxr_dev	*dev = srq->dev;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	rc = qlnxr_init_user_queue(ib_ctx, srq->dev, &srq->usrq, ureq->srq_addr,
				  ureq->srq_len, access, dmasync, 1);
	if (rc)
		return rc;

	srq->prod_umem = ib_umem_get(ib_ctx, ureq->prod_pair_addr,
				     sizeof(struct rdma_srq_producers),
				     access, dmasync);
	if (IS_ERR(srq->prod_umem)) {

		qlnxr_free_pbl(srq->dev, &srq->usrq.pbl_info, srq->usrq.pbl_tbl);
		ib_umem_release(srq->usrq.umem);

		QL_DPRINT12(ha, "ib_umem_get failed for producer [%p]\n",
			PTR_ERR(srq->prod_umem));

		return PTR_ERR(srq->prod_umem);
	}

#ifdef DEFINE_IB_UMEM_WITH_CHUNK
	chunk = container_of((&srq->prod_umem->chunk_list)->next,
			     typeof(*chunk), list);
	sg = &chunk->page_list[0];
#else
	sg = srq->prod_umem->sg_head.sgl;
#endif
	srq->hw_srq.phy_prod_pair_addr = sg_dma_address(sg);

	QL_DPRINT12(ha, "exit\n");
	return 0;
}


static int
qlnxr_alloc_srq_kernel_params(struct qlnxr_srq *srq,
	struct qlnxr_dev *dev,
	struct ib_srq_init_attr *init_attr)
{
	struct qlnxr_srq_hwq_info	*hw_srq  = &srq->hw_srq;
	dma_addr_t			phy_prod_pair_addr;
	u32				num_elems, max_wr;
	void				*va;
	int				rc;
	qlnx_host_t			*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	va = qlnx_dma_alloc_coherent(&dev->cdev,
			&phy_prod_pair_addr,
			sizeof(struct rdma_srq_producers));
	if (!va) {
		QL_DPRINT11(ha, "qlnx_dma_alloc_coherent failed for produceer\n");
		return -ENOMEM;
	}

	hw_srq->phy_prod_pair_addr = phy_prod_pair_addr;
	hw_srq->virt_prod_pair_addr = va;

	max_wr = init_attr->attr.max_wr;

	num_elems = max_wr * RDMA_MAX_SRQ_WQE_SIZE;

        rc = ecore_chain_alloc(dev->cdev,
                   ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
                   ECORE_CHAIN_MODE_PBL,
                   ECORE_CHAIN_CNT_TYPE_U32,
                   num_elems,
                   ECORE_RDMA_SRQ_WQE_ELEM_SIZE,
                   &hw_srq->pbl, NULL);

	if (rc) {
		QL_DPRINT11(ha, "ecore_chain_alloc failed [%d]\n", rc);
		goto err0;
	}

	hw_srq->max_wr = max_wr;
	hw_srq->num_elems = num_elems;
	hw_srq->max_sges = RDMA_MAX_SGE_PER_SRQ;

	QL_DPRINT12(ha, "exit\n");
	return 0;

err0:
	qlnx_dma_free_coherent(&dev->cdev, va, phy_prod_pair_addr,
		sizeof(struct rdma_srq_producers));

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

static inline void
qlnxr_init_common_qp_in_params(struct qlnxr_dev *dev,
	struct qlnxr_pd *pd,
	struct qlnxr_qp *qp,
	struct ib_qp_init_attr *attrs,
	bool fmr_and_reserved_lkey,
	struct ecore_rdma_create_qp_in_params *params)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	/* QP handle to be written in an async event */
	params->qp_handle_async_lo = lower_32_bits((uintptr_t)qp);
	params->qp_handle_async_hi = upper_32_bits((uintptr_t)qp);

	params->signal_all = (attrs->sq_sig_type == IB_SIGNAL_ALL_WR);
	params->fmr_and_reserved_lkey = fmr_and_reserved_lkey;
	params->pd = pd->pd_id;
	params->dpi = pd->uctx ? pd->uctx->dpi : dev->dpi;
	params->sq_cq_id = get_qlnxr_cq(attrs->send_cq)->icid;
	params->stats_queue = 0;

	params->rq_cq_id = get_qlnxr_cq(attrs->recv_cq)->icid;

	if (qp->srq) {
		/* QP is associated with SRQ instead of RQ */
		params->srq_id = qp->srq->srq_id;
		params->use_srq = true;
		QL_DPRINT11(ha, "exit srq_id = 0x%x use_srq = 0x%x\n",
			params->srq_id, params->use_srq);
		return;
	}

	params->srq_id = 0;
	params->use_srq = false;

	QL_DPRINT12(ha, "exit\n");
	return;
}


static inline void
qlnxr_qp_user_print( struct qlnxr_dev *dev,
	struct qlnxr_qp *qp)
{
	QL_DPRINT12((dev->ha), "qp=%p. sq_addr=0x%llx, sq_len=%zd, "
		"rq_addr=0x%llx, rq_len=%zd\n",
		qp, qp->usq.buf_addr, qp->usq.buf_len, qp->urq.buf_addr,
		qp->urq.buf_len);
	return;
}

static int
qlnxr_idr_add(struct qlnxr_dev *dev, void *ptr, u32 id)
{
	u32		newid;
	int		rc;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (!QLNX_IS_IWARP(dev))
		return 0;

	do {
		if (!idr_pre_get(&dev->qpidr, GFP_KERNEL)) {
			QL_DPRINT11(ha, "idr_pre_get failed\n");
			return -ENOMEM;
		}

		mtx_lock(&dev->idr_lock);

		rc = idr_get_new_above(&dev->qpidr, ptr, id, &newid);

		mtx_unlock(&dev->idr_lock);

	} while (rc == -EAGAIN);

	QL_DPRINT12(ha, "exit [%d]\n", rc);

	return rc;
}

static void
qlnxr_idr_remove(struct qlnxr_dev *dev, u32 id)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (!QLNX_IS_IWARP(dev))
		return;

	mtx_lock(&dev->idr_lock);
	idr_remove(&dev->qpidr, id);
	mtx_unlock(&dev->idr_lock);

	QL_DPRINT12(ha, "exit \n");

	return;
}

static inline void
qlnxr_iwarp_populate_user_qp(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct ecore_rdma_create_qp_out_params *out_params)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	qp->usq.pbl_tbl->va = out_params->sq_pbl_virt;
	qp->usq.pbl_tbl->pa = out_params->sq_pbl_phys;

	qlnxr_populate_pbls(dev, qp->usq.umem, qp->usq.pbl_tbl,
			   &qp->usq.pbl_info);

	if (qp->srq) {
		QL_DPRINT11(ha, "qp->srq = %p\n", qp->srq);
		return;
	}

	qp->urq.pbl_tbl->va = out_params->rq_pbl_virt;
	qp->urq.pbl_tbl->pa = out_params->rq_pbl_phys;

	qlnxr_populate_pbls(dev, qp->urq.umem, qp->urq.pbl_tbl,
			   &qp->urq.pbl_info);

	QL_DPRINT12(ha, "exit\n");
	return;
}

static int
qlnxr_create_user_qp(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct ib_pd *ibpd,
	struct ib_udata *udata,
	struct ib_qp_init_attr *attrs)
{
	struct ecore_rdma_destroy_qp_out_params d_out_params;
	struct ecore_rdma_create_qp_in_params in_params;
	struct ecore_rdma_create_qp_out_params out_params;
	struct qlnxr_pd *pd = get_qlnxr_pd(ibpd);
	struct ib_ucontext *ib_ctx = NULL;
	struct qlnxr_ucontext *ctx = NULL;
	struct qlnxr_create_qp_ureq ureq;
	int alloc_and_init = QLNX_IS_ROCE(dev);
	int rc = -EINVAL;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	ib_ctx = ibpd->uobject->context;
	ctx = get_qlnxr_ucontext(ib_ctx);

	memset(&ureq, 0, sizeof(ureq));
	rc = ib_copy_from_udata(&ureq, udata, sizeof(ureq));

	if (rc) {
		QL_DPRINT11(ha, "ib_copy_from_udata failed [%d]\n", rc);
		return rc;
	}

	/* SQ - read access only (0), dma sync not required (0) */
	rc = qlnxr_init_user_queue(ib_ctx, dev, &qp->usq, ureq.sq_addr,
				  ureq.sq_len, 0, 0,
				  alloc_and_init);
	if (rc) {
		QL_DPRINT11(ha, "qlnxr_init_user_queue failed [%d]\n", rc);
		return rc;
	}

	if (!qp->srq) {
		/* RQ - read access only (0), dma sync not required (0) */
		rc = qlnxr_init_user_queue(ib_ctx, dev, &qp->urq, ureq.rq_addr,
					  ureq.rq_len, 0, 0,
					  alloc_and_init);

		if (rc) {
			QL_DPRINT11(ha, "qlnxr_init_user_queue failed [%d]\n", rc);
			return rc;
		}
	}

	memset(&in_params, 0, sizeof(in_params));
	qlnxr_init_common_qp_in_params(dev, pd, qp, attrs, false, &in_params);
	in_params.qp_handle_lo = ureq.qp_handle_lo;
	in_params.qp_handle_hi = ureq.qp_handle_hi;
	in_params.sq_num_pages = qp->usq.pbl_info.num_pbes;
	in_params.sq_pbl_ptr = qp->usq.pbl_tbl->pa;

	if (!qp->srq) {
		in_params.rq_num_pages = qp->urq.pbl_info.num_pbes;
		in_params.rq_pbl_ptr = qp->urq.pbl_tbl->pa;
	}

	qp->ecore_qp = ecore_rdma_create_qp(dev->rdma_ctx, &in_params, &out_params);

	if (!qp->ecore_qp) {
		rc = -ENOMEM;
		QL_DPRINT11(ha, "ecore_rdma_create_qp failed\n");
		goto err1;
	}

	if (QLNX_IS_IWARP(dev))
		qlnxr_iwarp_populate_user_qp(dev, qp, &out_params);

	qp->qp_id = out_params.qp_id;
	qp->icid = out_params.icid;

	rc = qlnxr_copy_qp_uresp(dev, qp, udata);

	if (rc) {
		QL_DPRINT11(ha, "qlnxr_copy_qp_uresp failed\n");
		goto err;
	}

	qlnxr_qp_user_print(dev, qp);

	QL_DPRINT12(ha, "exit\n");
	return 0;
err:
	rc = ecore_rdma_destroy_qp(dev->rdma_ctx, qp->ecore_qp, &d_out_params);

	if (rc)
		QL_DPRINT12(ha, "fatal fault\n");

err1:
	qlnxr_cleanup_user(dev, qp);

	QL_DPRINT12(ha, "exit[%d]\n", rc);
	return rc;
}

static void
qlnxr_set_roce_db_info(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter qp = %p qp->srq %p\n", qp, qp->srq);

	qp->sq.db = dev->db_addr +
		DB_ADDR_SHIFT(DQ_PWM_OFFSET_XCM_RDMA_SQ_PROD);
	qp->sq.db_data.data.icid = qp->icid + 1;

	if (!qp->srq) {
		qp->rq.db = dev->db_addr +
			DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_ROCE_RQ_PROD);
		qp->rq.db_data.data.icid = qp->icid;
	}

	QL_DPRINT12(ha, "exit\n");
	return;
}

static void
qlnxr_set_iwarp_db_info(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp)

{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter qp = %p qp->srq %p\n", qp, qp->srq);

	qp->sq.db = dev->db_addr +
		DB_ADDR_SHIFT(DQ_PWM_OFFSET_XCM_RDMA_SQ_PROD);
	qp->sq.db_data.data.icid = qp->icid;

	if (!qp->srq) {
		qp->rq.db = dev->db_addr +
			DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_IWARP_RQ_PROD);
		qp->rq.db_data.data.icid = qp->icid;

		qp->rq.iwarp_db2 = dev->db_addr +
			DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_FLAGS);
		qp->rq.iwarp_db2_data.data.icid = qp->icid;
		qp->rq.iwarp_db2_data.data.value = DQ_TCM_IWARP_POST_RQ_CF_CMD;
	}

	QL_DPRINT12(ha,
		"qp->sq.db = %p qp->sq.db_data.data.icid =0x%x\n"
		"\t\t\tqp->rq.db = %p qp->rq.db_data.data.icid =0x%x\n"
		"\t\t\tqp->rq.iwarp_db2 = %p qp->rq.iwarp_db2.data.icid =0x%x"
		" qp->rq.iwarp_db2.data.prod_val =0x%x\n",
		qp->sq.db, qp->sq.db_data.data.icid,
		qp->rq.db, qp->rq.db_data.data.icid,
		qp->rq.iwarp_db2, qp->rq.iwarp_db2_data.data.icid,
		qp->rq.iwarp_db2_data.data.value);

	QL_DPRINT12(ha, "exit\n");
	return;
}

static int
qlnxr_roce_create_kernel_qp(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct ecore_rdma_create_qp_in_params *in_params,
	u32 n_sq_elems,
	u32 n_rq_elems)
{
	struct ecore_rdma_create_qp_out_params out_params;
	int		rc;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

        rc = ecore_chain_alloc(
                dev->cdev,
                ECORE_CHAIN_USE_TO_PRODUCE,
                ECORE_CHAIN_MODE_PBL,
                ECORE_CHAIN_CNT_TYPE_U32,
                n_sq_elems,
                QLNXR_SQE_ELEMENT_SIZE,
                &qp->sq.pbl,
                NULL);

	if (rc) {
		QL_DPRINT11(ha, "ecore_chain_alloc qp->sq.pbl failed[%d]\n", rc);
		return rc;
	}

	in_params->sq_num_pages = ecore_chain_get_page_cnt(&qp->sq.pbl);
	in_params->sq_pbl_ptr = ecore_chain_get_pbl_phys(&qp->sq.pbl);

	if (!qp->srq) {

                rc = ecore_chain_alloc(
                        dev->cdev,
                        ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
                        ECORE_CHAIN_MODE_PBL,
                        ECORE_CHAIN_CNT_TYPE_U32,
                        n_rq_elems,
                        QLNXR_RQE_ELEMENT_SIZE,
                        &qp->rq.pbl,
                        NULL);

		if (rc) {
			QL_DPRINT11(ha,
				"ecore_chain_alloc qp->rq.pbl failed[%d]\n", rc);
			return rc;
		}

		in_params->rq_num_pages = ecore_chain_get_page_cnt(&qp->rq.pbl);
		in_params->rq_pbl_ptr = ecore_chain_get_pbl_phys(&qp->rq.pbl);
	}

	qp->ecore_qp = ecore_rdma_create_qp(dev->rdma_ctx, in_params, &out_params);

	if (!qp->ecore_qp) {
		QL_DPRINT11(ha, "qp->ecore_qp == NULL\n");
		return -EINVAL;
	}

	qp->qp_id = out_params.qp_id;
	qp->icid = out_params.icid;

	qlnxr_set_roce_db_info(dev, qp);

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

static int
qlnxr_iwarp_create_kernel_qp(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct ecore_rdma_create_qp_in_params *in_params,
	u32 n_sq_elems,
	u32 n_rq_elems)
{
	struct ecore_rdma_destroy_qp_out_params d_out_params;
	struct ecore_rdma_create_qp_out_params out_params;
	struct ecore_chain_ext_pbl ext_pbl;
	int rc;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	in_params->sq_num_pages = ECORE_CHAIN_PAGE_CNT(n_sq_elems,
						     QLNXR_SQE_ELEMENT_SIZE,
						     ECORE_CHAIN_MODE_PBL);
	in_params->rq_num_pages = ECORE_CHAIN_PAGE_CNT(n_rq_elems,
						     QLNXR_RQE_ELEMENT_SIZE,
						     ECORE_CHAIN_MODE_PBL);

	QL_DPRINT12(ha, "n_sq_elems = 0x%x"
		" n_rq_elems = 0x%x in_params\n"
		"\t\t\tqp_handle_lo\t\t= 0x%08x\n"
		"\t\t\tqp_handle_hi\t\t= 0x%08x\n"
		"\t\t\tqp_handle_async_lo\t\t= 0x%08x\n"
		"\t\t\tqp_handle_async_hi\t\t= 0x%08x\n"
		"\t\t\tuse_srq\t\t\t= 0x%x\n"
		"\t\t\tsignal_all\t\t= 0x%x\n"
		"\t\t\tfmr_and_reserved_lkey\t= 0x%x\n"
		"\t\t\tpd\t\t\t= 0x%x\n"
		"\t\t\tdpi\t\t\t= 0x%x\n"
		"\t\t\tsq_cq_id\t\t\t= 0x%x\n"
		"\t\t\tsq_num_pages\t\t= 0x%x\n"
		"\t\t\tsq_pbl_ptr\t\t= %p\n"
		"\t\t\tmax_sq_sges\t\t= 0x%x\n"
		"\t\t\trq_cq_id\t\t\t= 0x%x\n"
		"\t\t\trq_num_pages\t\t= 0x%x\n"
		"\t\t\trq_pbl_ptr\t\t= %p\n"
		"\t\t\tsrq_id\t\t\t= 0x%x\n"
		"\t\t\tstats_queue\t\t= 0x%x\n",
		n_sq_elems, n_rq_elems,
		in_params->qp_handle_lo,
		in_params->qp_handle_hi,
		in_params->qp_handle_async_lo,
		in_params->qp_handle_async_hi,
		in_params->use_srq,
		in_params->signal_all,
		in_params->fmr_and_reserved_lkey,
		in_params->pd,
		in_params->dpi,
		in_params->sq_cq_id,
		in_params->sq_num_pages,
		(void *)in_params->sq_pbl_ptr,
		in_params->max_sq_sges,
		in_params->rq_cq_id,
		in_params->rq_num_pages,
		(void *)in_params->rq_pbl_ptr,
		in_params->srq_id,
		in_params->stats_queue );

	memset(&out_params, 0, sizeof (struct ecore_rdma_create_qp_out_params));
	memset(&ext_pbl, 0, sizeof (struct ecore_chain_ext_pbl));

	qp->ecore_qp = ecore_rdma_create_qp(dev->rdma_ctx, in_params, &out_params);

	if (!qp->ecore_qp) {
		QL_DPRINT11(ha, "ecore_rdma_create_qp failed\n");
		return -EINVAL;
	}

	/* Now we allocate the chain */
	ext_pbl.p_pbl_virt = out_params.sq_pbl_virt;
	ext_pbl.p_pbl_phys = out_params.sq_pbl_phys;

	QL_DPRINT12(ha, "ext_pbl.p_pbl_virt = %p "
		"ext_pbl.p_pbl_phys = %p\n",
		ext_pbl.p_pbl_virt, ext_pbl.p_pbl_phys);
		
        rc = ecore_chain_alloc(
                dev->cdev,
                ECORE_CHAIN_USE_TO_PRODUCE,
                ECORE_CHAIN_MODE_PBL,
                ECORE_CHAIN_CNT_TYPE_U32,
                n_sq_elems,
                QLNXR_SQE_ELEMENT_SIZE,
                &qp->sq.pbl,
                &ext_pbl);

	if (rc) {
		QL_DPRINT11(ha,
			"ecore_chain_alloc qp->sq.pbl failed rc = %d\n", rc);
		goto err;
	}

	ext_pbl.p_pbl_virt = out_params.rq_pbl_virt;
	ext_pbl.p_pbl_phys = out_params.rq_pbl_phys;

	QL_DPRINT12(ha, "ext_pbl.p_pbl_virt = %p "
		"ext_pbl.p_pbl_phys = %p\n",
		ext_pbl.p_pbl_virt, ext_pbl.p_pbl_phys);

	if (!qp->srq) {

                rc = ecore_chain_alloc(
                        dev->cdev,
                        ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
                        ECORE_CHAIN_MODE_PBL,
                        ECORE_CHAIN_CNT_TYPE_U32,
                        n_rq_elems,
                        QLNXR_RQE_ELEMENT_SIZE,
                        &qp->rq.pbl,
                        &ext_pbl);

		if (rc) {
			QL_DPRINT11(ha,, "ecore_chain_alloc qp->rq.pbl"
				" failed rc = %d\n", rc);
			goto err;
		}
	}

	QL_DPRINT12(ha, "qp_id = 0x%x icid =0x%x\n",
		out_params.qp_id, out_params.icid);

	qp->qp_id = out_params.qp_id;
	qp->icid = out_params.icid;

	qlnxr_set_iwarp_db_info(dev, qp);

	QL_DPRINT12(ha, "exit\n");
	return 0;

err:
	ecore_rdma_destroy_qp(dev->rdma_ctx, qp->ecore_qp, &d_out_params);

	QL_DPRINT12(ha, "exit rc = %d\n", rc);
	return rc;
}

static int
qlnxr_create_kernel_qp(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct ib_pd *ibpd,
	struct ib_qp_init_attr *attrs)
{
	struct ecore_rdma_create_qp_in_params in_params;
	struct qlnxr_pd *pd = get_qlnxr_pd(ibpd);
	int rc = -EINVAL;
	u32 n_rq_elems;
	u32 n_sq_elems;
	u32 n_sq_entries;
	struct ecore_rdma_device *qattr = ecore_rdma_query_device(dev->rdma_ctx);
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	memset(&in_params, 0, sizeof(in_params));

	/* A single work request may take up to MAX_SQ_WQE_SIZE elements in
	 * the ring. The ring should allow at least a single WR, even if the
	 * user requested none, due to allocation issues.
	 * We should add an extra WR since the prod and cons indices of
	 * wqe_wr_id are managed in such a way that the WQ is considered full
	 * when (prod+1)%max_wr==cons. We currently don't do that because we
	 * double the number of entries due an iSER issue that pushes far more
	 * WRs than indicated. If we decline its ib_post_send() then we get
	 * error prints in the dmesg we'd like to avoid.
	 */
	qp->sq.max_wr = min_t(u32, attrs->cap.max_send_wr * dev->wq_multiplier,
			      qattr->max_wqe);

	qp->wqe_wr_id = kzalloc(qp->sq.max_wr * sizeof(*qp->wqe_wr_id),
			GFP_KERNEL);
	if (!qp->wqe_wr_id) {
		QL_DPRINT11(ha, "failed SQ shadow memory allocation\n");
		return -ENOMEM;
	}

	/* QP handle to be written in CQE */
	in_params.qp_handle_lo = lower_32_bits((uintptr_t)qp);
	in_params.qp_handle_hi = upper_32_bits((uintptr_t)qp);

	/* A single work request may take up to MAX_RQ_WQE_SIZE elements in
	 * the ring. There ring should allow at least a single WR, even if the
	 * user requested none, due to allocation issues.
	 */
	qp->rq.max_wr = (u16)max_t(u32, attrs->cap.max_recv_wr, 1);

	/* Allocate driver internal RQ array */
	if (!qp->srq) {
		qp->rqe_wr_id = kzalloc(qp->rq.max_wr * sizeof(*qp->rqe_wr_id),
					GFP_KERNEL);
		if (!qp->rqe_wr_id) {
			QL_DPRINT11(ha, "failed RQ shadow memory allocation\n");
			kfree(qp->wqe_wr_id);
			return -ENOMEM;
		}
	}

	//qlnxr_init_common_qp_in_params(dev, pd, qp, attrs, true, &in_params);

        in_params.qp_handle_async_lo = lower_32_bits((uintptr_t)qp);
        in_params.qp_handle_async_hi = upper_32_bits((uintptr_t)qp);

        in_params.signal_all = (attrs->sq_sig_type == IB_SIGNAL_ALL_WR);
        in_params.fmr_and_reserved_lkey = true;
        in_params.pd = pd->pd_id;
        in_params.dpi = pd->uctx ? pd->uctx->dpi : dev->dpi;
        in_params.sq_cq_id = get_qlnxr_cq(attrs->send_cq)->icid;
        in_params.stats_queue = 0;

        in_params.rq_cq_id = get_qlnxr_cq(attrs->recv_cq)->icid;

        if (qp->srq) {
                /* QP is associated with SRQ instead of RQ */
                in_params.srq_id = qp->srq->srq_id;
                in_params.use_srq = true;
                QL_DPRINT11(ha, "exit srq_id = 0x%x use_srq = 0x%x\n",
                        in_params.srq_id, in_params.use_srq);
        } else {
        	in_params.srq_id = 0;
		in_params.use_srq = false;
	}

	n_sq_entries = attrs->cap.max_send_wr;
	n_sq_entries = min_t(u32, n_sq_entries, qattr->max_wqe);
	n_sq_entries = max_t(u32, n_sq_entries, 1);
	n_sq_elems = n_sq_entries * QLNXR_MAX_SQE_ELEMENTS_PER_SQE;

	n_rq_elems = qp->rq.max_wr * QLNXR_MAX_RQE_ELEMENTS_PER_RQE;

	if (QLNX_IS_ROCE(dev)) {
		rc = qlnxr_roce_create_kernel_qp(dev, qp, &in_params,
						n_sq_elems, n_rq_elems);
	} else {
		rc = qlnxr_iwarp_create_kernel_qp(dev, qp, &in_params,
						 n_sq_elems, n_rq_elems);
	}

	if (rc)
		qlnxr_cleanup_kernel(dev, qp);

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

struct ib_qp *
qlnxr_create_qp(struct ib_pd *ibpd,
		struct ib_qp_init_attr *attrs,
		struct ib_udata *udata)
{
	struct qlnxr_dev *dev = get_qlnxr_dev(ibpd->device);
	struct qlnxr_pd *pd = get_qlnxr_pd(ibpd);
	struct qlnxr_qp *qp;
	int rc = 0;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	rc = qlnxr_check_qp_attrs(ibpd, dev, attrs, udata);
	if (rc) {
		QL_DPRINT11(ha, "qlnxr_check_qp_attrs failed [%d]\n", rc);
		return ERR_PTR(rc);
	}

	QL_DPRINT12(ha, "called from %s, event_handle=%p,"
		" eepd=%p sq_cq=%p, sq_icid=%d, rq_cq=%p, rq_icid=%d\n",
		(udata ? "user library" : "kernel"),
		attrs->event_handler, pd,
		get_qlnxr_cq(attrs->send_cq),
		get_qlnxr_cq(attrs->send_cq)->icid,
		get_qlnxr_cq(attrs->recv_cq),
		get_qlnxr_cq(attrs->recv_cq)->icid);

	qp = qlnx_zalloc(sizeof(struct qlnxr_qp));

	if (!qp) {
		QL_DPRINT11(ha, "kzalloc(qp) failed\n");
		return ERR_PTR(-ENOMEM);
	}

	qlnxr_set_common_qp_params(dev, qp, pd, attrs);

	if (attrs->qp_type == IB_QPT_GSI) {
		QL_DPRINT11(ha, "calling qlnxr_create_gsi_qp\n");
		return qlnxr_create_gsi_qp(dev, attrs, qp);
	}

	if (udata) {
		rc = qlnxr_create_user_qp(dev, qp, ibpd, udata, attrs);

		if (rc) {
			QL_DPRINT11(ha, "qlnxr_create_user_qp failed\n");
			goto err;
		}
	} else {
		rc = qlnxr_create_kernel_qp(dev, qp, ibpd, attrs);

		if (rc) {
			QL_DPRINT11(ha, "qlnxr_create_kernel_qp failed\n");
			goto err;
		}
	}

	qp->ibqp.qp_num = qp->qp_id;

	rc = qlnxr_idr_add(dev, qp, qp->qp_id);

	if (rc) {
		QL_DPRINT11(ha, "qlnxr_idr_add failed\n");
		goto err;
	}

	QL_DPRINT12(ha, "exit [%p]\n", &qp->ibqp);

	return &qp->ibqp;
err:
	kfree(qp);

	QL_DPRINT12(ha, "failed exit\n");
	return ERR_PTR(-EFAULT);
}


static enum ib_qp_state
qlnxr_get_ibqp_state(enum ecore_roce_qp_state qp_state)
{
	enum ib_qp_state state = IB_QPS_ERR;

	switch (qp_state) {
	case ECORE_ROCE_QP_STATE_RESET:
		state = IB_QPS_RESET;
		break;

	case ECORE_ROCE_QP_STATE_INIT:
		state = IB_QPS_INIT;
		break;

	case ECORE_ROCE_QP_STATE_RTR:
		state = IB_QPS_RTR;
		break;

	case ECORE_ROCE_QP_STATE_RTS:
		state = IB_QPS_RTS;
		break;

	case ECORE_ROCE_QP_STATE_SQD:
		state = IB_QPS_SQD;
		break;

	case ECORE_ROCE_QP_STATE_ERR:
		state = IB_QPS_ERR;
		break;

	case ECORE_ROCE_QP_STATE_SQE:
		state = IB_QPS_SQE;
		break;
	}
	return state;
}

static enum ecore_roce_qp_state
qlnxr_get_state_from_ibqp( enum ib_qp_state qp_state)
{
	enum ecore_roce_qp_state ecore_qp_state;

	ecore_qp_state = ECORE_ROCE_QP_STATE_ERR;

	switch (qp_state) {
	case IB_QPS_RESET:
		ecore_qp_state =  ECORE_ROCE_QP_STATE_RESET;
		break;

	case IB_QPS_INIT:
		ecore_qp_state =  ECORE_ROCE_QP_STATE_INIT;
		break;

	case IB_QPS_RTR:
		ecore_qp_state =  ECORE_ROCE_QP_STATE_RTR;
		break;

	case IB_QPS_RTS:
		ecore_qp_state =  ECORE_ROCE_QP_STATE_RTS;
		break;

	case IB_QPS_SQD:
		ecore_qp_state =  ECORE_ROCE_QP_STATE_SQD;
		break;

	case IB_QPS_ERR:
		ecore_qp_state =  ECORE_ROCE_QP_STATE_ERR;
		break;

	default:
		ecore_qp_state =  ECORE_ROCE_QP_STATE_ERR;
		break;
	}

	return (ecore_qp_state);
}

static void
qlnxr_reset_qp_hwq_info(struct qlnxr_qp_hwq_info *qph)
{
	ecore_chain_reset(&qph->pbl);
	qph->prod = qph->cons = 0;
	qph->wqe_cons = 0;
	qph->db_data.data.value = cpu_to_le16(0);

	return;
}

static int
qlnxr_update_qp_state(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	enum ecore_roce_qp_state new_state)
{
	int		status = 0;
	uint32_t	reg_addr;
	struct ecore_dev *cdev;
	qlnx_host_t	*ha;

	ha = dev->ha;
	cdev = &ha->cdev;

	QL_DPRINT12(ha, "enter qp = %p new_state = 0x%x qp->state = 0x%x\n",
		qp, new_state, qp->state);

	if (new_state == qp->state) {
		return 0;
	}

	switch (qp->state) {
	case ECORE_ROCE_QP_STATE_RESET:
		switch (new_state) {
		case ECORE_ROCE_QP_STATE_INIT:
			qp->prev_wqe_size = 0;
			qlnxr_reset_qp_hwq_info(&qp->sq);
			if (!(qp->srq))
				qlnxr_reset_qp_hwq_info(&qp->rq);
			break;
		default:
			status = -EINVAL;
			break;
		};
		break;
	case ECORE_ROCE_QP_STATE_INIT:
		/* INIT->XXX */
		switch (new_state) {
		case ECORE_ROCE_QP_STATE_RTR:
		/* Update doorbell (in case post_recv was done before move to RTR) */
			if (qp->srq)
				break;
			wmb();
			//writel(qp->rq.db_data.raw, qp->rq.db);
			//if (QLNX_IS_IWARP(dev))
			//	writel(qp->rq.iwarp_db2_data.raw,
			//	       qp->rq.iwarp_db2);

			reg_addr = (uint32_t)((uint8_t *)qp->rq.db -
					(uint8_t *)cdev->doorbells);

			bus_write_4(ha->pci_dbells, reg_addr, qp->rq.db_data.raw);
			bus_barrier(ha->pci_dbells,  0, 0, BUS_SPACE_BARRIER_READ);

			if (QLNX_IS_IWARP(dev)) {
				reg_addr = (uint32_t)((uint8_t *)qp->rq.iwarp_db2 -
					(uint8_t *)cdev->doorbells);
				bus_write_4(ha->pci_dbells, reg_addr,\
					qp->rq.iwarp_db2_data.raw);
				bus_barrier(ha->pci_dbells,  0, 0,\
					BUS_SPACE_BARRIER_READ);
			}

			
			mmiowb();
			break;
		case ECORE_ROCE_QP_STATE_ERR:
			/* TBD:flush qps... */
			break;
		default:
			/* invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case ECORE_ROCE_QP_STATE_RTR:
		/* RTR->XXX */
		switch (new_state) {
		case ECORE_ROCE_QP_STATE_RTS:
			break;
		case ECORE_ROCE_QP_STATE_ERR:
			break;
		default:
			/* invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case ECORE_ROCE_QP_STATE_RTS:
		/* RTS->XXX */
		switch (new_state) {
		case ECORE_ROCE_QP_STATE_SQD:
			break;
		case ECORE_ROCE_QP_STATE_ERR:
			break;
		default:
			/* invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case ECORE_ROCE_QP_STATE_SQD:
		/* SQD->XXX */
		switch (new_state) {
		case ECORE_ROCE_QP_STATE_RTS:
		case ECORE_ROCE_QP_STATE_ERR:
			break;
		default:
			/* invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case ECORE_ROCE_QP_STATE_ERR:
		/* ERR->XXX */
		switch (new_state) {
		case ECORE_ROCE_QP_STATE_RESET:
			if ((qp->rq.prod != qp->rq.cons) ||
			    (qp->sq.prod != qp->sq.cons)) {
				QL_DPRINT11(ha,
					"Error->Reset with rq/sq "
					"not empty rq.prod=0x%x rq.cons=0x%x"
					" sq.prod=0x%x sq.cons=0x%x\n",
					qp->rq.prod, qp->rq.cons,
					qp->sq.prod, qp->sq.cons);
				status = -EINVAL;
			}
			break;
		default:
			status = -EINVAL;
			break;
		};
		break;
	default:
		status = -EINVAL;
		break;
	};

	QL_DPRINT12(ha, "exit\n");
	return status;
}

int
qlnxr_modify_qp(struct ib_qp	*ibqp,
	struct ib_qp_attr	*attr,
	int			attr_mask,
	struct ib_udata		*udata)
{
	int rc = 0;
	struct qlnxr_qp *qp = get_qlnxr_qp(ibqp);
	struct qlnxr_dev *dev = get_qlnxr_dev(&qp->dev->ibdev);
	struct ecore_rdma_modify_qp_in_params qp_params = { 0 };
	enum ib_qp_state old_qp_state, new_qp_state;
	struct ecore_rdma_device *qattr = ecore_rdma_query_device(dev->rdma_ctx);
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha,
		"enter qp = %p attr_mask = 0x%x, state = %d udata = %p\n",
		qp, attr_mask, attr->qp_state, udata);

	old_qp_state = qlnxr_get_ibqp_state(qp->state);
	if (attr_mask & IB_QP_STATE)
		new_qp_state = attr->qp_state;
	else
		new_qp_state = old_qp_state;

	if (QLNX_IS_ROCE(dev)) {
#if __FreeBSD_version >= 1100000
		if (!ib_modify_qp_is_ok(old_qp_state,
					new_qp_state,
					ibqp->qp_type,
					attr_mask,
					IB_LINK_LAYER_ETHERNET)) {
			QL_DPRINT12(ha,
				"invalid attribute mask=0x%x"
				" specified for qpn=0x%x of type=0x%x \n"
				" old_qp_state=0x%x, new_qp_state=0x%x\n",
				attr_mask, qp->qp_id, ibqp->qp_type,
				old_qp_state, new_qp_state);
			rc = -EINVAL;
			goto err;
		}
#else
		if (!ib_modify_qp_is_ok(old_qp_state,
					new_qp_state,
					ibqp->qp_type,
					attr_mask )) {
			QL_DPRINT12(ha,
				"invalid attribute mask=0x%x"
				" specified for qpn=0x%x of type=0x%x \n"
				" old_qp_state=0x%x, new_qp_state=0x%x\n",
				attr_mask, qp->qp_id, ibqp->qp_type,
				old_qp_state, new_qp_state);
			rc = -EINVAL;
			goto err;
		}

#endif /* #if __FreeBSD_version >= 1100000 */
	}
	/* translate the masks... */
	if (attr_mask & IB_QP_STATE) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_RDMA_MODIFY_QP_VALID_NEW_STATE, 1);
		qp_params.new_state = qlnxr_get_state_from_ibqp(attr->qp_state);
	}

	// TBD consider changing ecore to be a flag as well...
	if (attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY)
		qp_params.sqd_async = true;

	if (attr_mask & IB_QP_PKEY_INDEX) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_ROCE_MODIFY_QP_VALID_PKEY,
			  1);
		if (attr->pkey_index >= QLNXR_ROCE_PKEY_TABLE_LEN) {
			rc = -EINVAL;
			goto err;
		}

		qp_params.pkey = QLNXR_ROCE_PKEY_DEFAULT;
	}

	if (attr_mask & IB_QP_QKEY) {
		qp->qkey = attr->qkey;
	}

	/* tbd consider splitting in ecore.. */
	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN, 1);
		qp_params.incoming_rdma_read_en =
			attr->qp_access_flags & IB_ACCESS_REMOTE_READ;
		qp_params.incoming_rdma_write_en =
			attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE;
		qp_params.incoming_atomic_en =
			attr->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC;
	}

	if (attr_mask & (IB_QP_AV | IB_QP_PATH_MTU)) {
		if (attr_mask & IB_QP_PATH_MTU) {
			if (attr->path_mtu < IB_MTU_256 ||
			    attr->path_mtu > IB_MTU_4096) {

				QL_DPRINT12(ha,
					"Only MTU sizes of 256, 512, 1024,"
					" 2048 and 4096 are supported "
					" attr->path_mtu = [%d]\n",
					attr->path_mtu);

				rc = -EINVAL;
				goto err;
			}
			qp->mtu = min(ib_mtu_enum_to_int(attr->path_mtu),
				      ib_mtu_enum_to_int(
						iboe_get_mtu(dev->ha->ifp->if_mtu)));
		}

		if (qp->mtu == 0) {
			qp->mtu = ib_mtu_enum_to_int(
					iboe_get_mtu(dev->ha->ifp->if_mtu));
			QL_DPRINT12(ha, "fixing zetoed MTU to qp->mtu = %d\n",
				qp->mtu);
		}

		SET_FIELD(qp_params.modify_flags,
			  ECORE_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR,
			  1);

		qp_params.traffic_class_tos = attr->ah_attr.grh.traffic_class;
		qp_params.flow_label = attr->ah_attr.grh.flow_label;
		qp_params.hop_limit_ttl = attr->ah_attr.grh.hop_limit;

		qp->sgid_idx = attr->ah_attr.grh.sgid_index;

		get_gid_info(ibqp, attr, attr_mask, dev, qp, &qp_params);

		rc = qlnxr_get_dmac(dev, &attr->ah_attr, qp_params.remote_mac_addr);
		if (rc)
			return rc;

		qp_params.use_local_mac = true;
		memcpy(qp_params.local_mac_addr, dev->ha->primary_mac, ETH_ALEN);

		QL_DPRINT12(ha, "dgid=0x%x:0x%x:0x%x:0x%x\n",
		       qp_params.dgid.dwords[0], qp_params.dgid.dwords[1],
		       qp_params.dgid.dwords[2], qp_params.dgid.dwords[3]);
		QL_DPRINT12(ha, "sgid=0x%x:0x%x:0x%x:0x%x\n",
		       qp_params.sgid.dwords[0], qp_params.sgid.dwords[1],
		       qp_params.sgid.dwords[2], qp_params.sgid.dwords[3]);
		QL_DPRINT12(ha,
			"remote_mac=[0x%x:0x%x:0x%x:0x%x:0x%x:0x%x]\n",
			qp_params.remote_mac_addr[0],
			qp_params.remote_mac_addr[1],
			qp_params.remote_mac_addr[2],
			qp_params.remote_mac_addr[3],
			qp_params.remote_mac_addr[4],
			qp_params.remote_mac_addr[5]);

		qp_params.mtu = qp->mtu;
	}

	if (qp_params.mtu == 0) {
		/* stay with current MTU */
		if (qp->mtu) {
			qp_params.mtu = qp->mtu;
		} else {
			qp_params.mtu = ib_mtu_enum_to_int(
						iboe_get_mtu(dev->ha->ifp->if_mtu));
		}
	}

	if (attr_mask & IB_QP_TIMEOUT) {
		SET_FIELD(qp_params.modify_flags, \
			ECORE_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT, 1);

		qp_params.ack_timeout = attr->timeout;
		if (attr->timeout) {
			u32 temp;

			/* 12.7.34 LOCAL ACK TIMEOUT
			 * Value representing the transport (ACK) timeout for
			 * use by the remote, expressed as (4.096 μS*2Local ACK
			 * Timeout)
			 */
			/* We use 1UL since the temporal value may be  overflow
			 * 32 bits
			 */
			temp = 4096 * (1UL << attr->timeout) / 1000 / 1000;
			qp_params.ack_timeout = temp; /* FW requires [msec] */
		}
		else
			qp_params.ack_timeout = 0; /* infinite */
	}
	if (attr_mask & IB_QP_RETRY_CNT) {
		SET_FIELD(qp_params.modify_flags,\
			 ECORE_ROCE_MODIFY_QP_VALID_RETRY_CNT, 1);
		qp_params.retry_cnt = attr->retry_cnt;
	}

	if (attr_mask & IB_QP_RNR_RETRY) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT,
			  1);
		qp_params.rnr_retry_cnt = attr->rnr_retry;
	}

	if (attr_mask & IB_QP_RQ_PSN) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_ROCE_MODIFY_QP_VALID_RQ_PSN,
			  1);
		qp_params.rq_psn = attr->rq_psn;
		qp->rq_psn = attr->rq_psn;
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		if (attr->max_rd_atomic > qattr->max_qp_req_rd_atomic_resc) {
			rc = -EINVAL;
			QL_DPRINT12(ha,
				"unsupported  max_rd_atomic=%d, supported=%d\n",
				attr->max_rd_atomic,
				qattr->max_qp_req_rd_atomic_resc);
			goto err;
		}

		SET_FIELD(qp_params.modify_flags,
			  ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ,
			  1);
		qp_params.max_rd_atomic_req = attr->max_rd_atomic;
	}

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER,
			  1);
		qp_params.min_rnr_nak_timer = attr->min_rnr_timer;
	}

	if (attr_mask & IB_QP_SQ_PSN) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_ROCE_MODIFY_QP_VALID_SQ_PSN,
			  1);
		qp_params.sq_psn = attr->sq_psn;
		qp->sq_psn = attr->sq_psn;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		if (attr->max_dest_rd_atomic >
		    qattr->max_qp_resp_rd_atomic_resc) {
			QL_DPRINT12(ha,
				"unsupported max_dest_rd_atomic=%d, "
				"supported=%d\n",
				attr->max_dest_rd_atomic,
				qattr->max_qp_resp_rd_atomic_resc);

			rc = -EINVAL;
			goto err;
		}

		SET_FIELD(qp_params.modify_flags,
			  ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP,
			  1);
		qp_params.max_rd_atomic_resp = attr->max_dest_rd_atomic;
	}

 	if (attr_mask & IB_QP_DEST_QPN) {
		SET_FIELD(qp_params.modify_flags,
			  ECORE_ROCE_MODIFY_QP_VALID_DEST_QP,
			  1);

		qp_params.dest_qp = attr->dest_qp_num;
		qp->dest_qp_num = attr->dest_qp_num;
	}

	/*
	 * Update the QP state before the actual ramrod to prevent a race with
	 * fast path. Modifying the QP state to error will cause the device to
	 * flush the CQEs and while polling the flushed CQEs will considered as
	 * a potential issue if the QP isn't in error state.
	 */
	if ((attr_mask & IB_QP_STATE) && (qp->qp_type != IB_QPT_GSI) &&
		(!udata) && (qp_params.new_state == ECORE_ROCE_QP_STATE_ERR))
		qp->state = ECORE_ROCE_QP_STATE_ERR;

	if (qp->qp_type != IB_QPT_GSI)
		rc = ecore_rdma_modify_qp(dev->rdma_ctx, qp->ecore_qp, &qp_params);

	if (attr_mask & IB_QP_STATE) {
		if ((qp->qp_type != IB_QPT_GSI) && (!udata))
			rc = qlnxr_update_qp_state(dev, qp, qp_params.new_state);
		qp->state = qp_params.new_state;
	}

err:
	QL_DPRINT12(ha, "exit\n");
	return rc;
}

static int
qlnxr_to_ib_qp_acc_flags(struct ecore_rdma_query_qp_out_params *params)
{
	int ib_qp_acc_flags = 0;

	if (params->incoming_rdma_write_en)
		ib_qp_acc_flags |= IB_ACCESS_REMOTE_WRITE;
	if (params->incoming_rdma_read_en)
		ib_qp_acc_flags |= IB_ACCESS_REMOTE_READ;
	if (params->incoming_atomic_en)
		ib_qp_acc_flags |= IB_ACCESS_REMOTE_ATOMIC;
	if (true) /* FIXME -> local write ?? */
		ib_qp_acc_flags |= IB_ACCESS_LOCAL_WRITE;

	return ib_qp_acc_flags;
}

static enum ib_mtu
qlnxr_mtu_int_to_enum(u16 mtu)
{
	enum ib_mtu ib_mtu_size;

	switch (mtu) {
	case 256:
		ib_mtu_size = IB_MTU_256;
		break;

	case 512:
		ib_mtu_size = IB_MTU_512;
		break;

	case 1024:
		ib_mtu_size = IB_MTU_1024;
		break;

	case 2048:
		ib_mtu_size = IB_MTU_2048;
		break;

	case 4096:
		ib_mtu_size = IB_MTU_4096;
		break;

	default:
		ib_mtu_size = IB_MTU_1024;
		break;
	}
	return (ib_mtu_size);
}

int
qlnxr_query_qp(struct ib_qp *ibqp,
	struct ib_qp_attr *qp_attr,
	int attr_mask,
	struct ib_qp_init_attr *qp_init_attr)
{
	int rc = 0;
	struct ecore_rdma_query_qp_out_params params;
	struct qlnxr_qp *qp = get_qlnxr_qp(ibqp);
	struct qlnxr_dev *dev = qp->dev;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	memset(&params, 0, sizeof(params));

	rc = ecore_rdma_query_qp(dev->rdma_ctx, qp->ecore_qp, &params);
	if (rc)
		goto err;

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	qp_attr->qp_state = qlnxr_get_ibqp_state(params.state);
	qp_attr->cur_qp_state = qlnxr_get_ibqp_state(params.state);

	/* In some cases in iWARP qelr will ask for the state only */
	if (QLNX_IS_IWARP(dev) && (attr_mask == IB_QP_STATE)) {
		QL_DPRINT11(ha, "only state requested\n");
		return 0;
	}

	qp_attr->path_mtu = qlnxr_mtu_int_to_enum(params.mtu);
	qp_attr->path_mig_state = IB_MIG_MIGRATED;
	qp_attr->rq_psn = params.rq_psn;
	qp_attr->sq_psn = params.sq_psn;
	qp_attr->dest_qp_num = params.dest_qp;

	qp_attr->qp_access_flags = qlnxr_to_ib_qp_acc_flags(&params);

	QL_DPRINT12(ha, "qp_state = 0x%x cur_qp_state = 0x%x "
		"path_mtu = %d qp_access_flags = 0x%x\n",
		qp_attr->qp_state, qp_attr->cur_qp_state, qp_attr->path_mtu,
		qp_attr->qp_access_flags);

	qp_attr->cap.max_send_wr = qp->sq.max_wr;
	qp_attr->cap.max_recv_wr = qp->rq.max_wr;
	qp_attr->cap.max_send_sge = qp->sq.max_sges;
	qp_attr->cap.max_recv_sge = qp->rq.max_sges;
	qp_attr->cap.max_inline_data = qp->max_inline_data;
	qp_init_attr->cap = qp_attr->cap;

	memcpy(&qp_attr->ah_attr.grh.dgid.raw[0], &params.dgid.bytes[0],
	       sizeof(qp_attr->ah_attr.grh.dgid.raw));

	qp_attr->ah_attr.grh.flow_label = params.flow_label;
	qp_attr->ah_attr.grh.sgid_index = qp->sgid_idx;
	qp_attr->ah_attr.grh.hop_limit = params.hop_limit_ttl;
	qp_attr->ah_attr.grh.traffic_class = params.traffic_class_tos;

	qp_attr->ah_attr.ah_flags = IB_AH_GRH;
	qp_attr->ah_attr.port_num = 1; /* FIXME -> check this */
	qp_attr->ah_attr.sl = 0;/* FIXME -> check this */
	qp_attr->timeout = params.timeout;
	qp_attr->rnr_retry = params.rnr_retry;
	qp_attr->retry_cnt = params.retry_cnt;
	qp_attr->min_rnr_timer = params.min_rnr_nak_timer;
	qp_attr->pkey_index = params.pkey_index;
	qp_attr->port_num = 1; /* FIXME -> check this */
	qp_attr->ah_attr.src_path_bits = 0;
	qp_attr->ah_attr.static_rate = 0;
	qp_attr->alt_pkey_index = 0;
	qp_attr->alt_port_num = 0;
	qp_attr->alt_timeout = 0;
	memset(&qp_attr->alt_ah_attr, 0, sizeof(qp_attr->alt_ah_attr));

	qp_attr->sq_draining = (params.state == ECORE_ROCE_QP_STATE_SQD) ? 1 : 0;
	qp_attr->max_dest_rd_atomic = params.max_dest_rd_atomic;
	qp_attr->max_rd_atomic = params.max_rd_atomic;
	qp_attr->en_sqd_async_notify = (params.sqd_async)? 1 : 0;

	QL_DPRINT12(ha, "max_inline_data=%d\n",
		qp_attr->cap.max_inline_data);

err:
	QL_DPRINT12(ha, "exit\n");
	return rc;
}


static void
qlnxr_cleanup_user(struct qlnxr_dev *dev, struct qlnxr_qp *qp)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	if (qp->usq.umem)
		ib_umem_release(qp->usq.umem);

	qp->usq.umem = NULL;

	if (qp->urq.umem)
		ib_umem_release(qp->urq.umem);

	qp->urq.umem = NULL;

	QL_DPRINT12(ha, "exit\n");
	return;
}

static void
qlnxr_cleanup_kernel(struct qlnxr_dev *dev, struct qlnxr_qp *qp)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	if (qlnxr_qp_has_sq(qp)) {
		QL_DPRINT12(ha, "freeing SQ\n");
		ha->qlnxr_debug = 1;
//		ecore_chain_free(dev->cdev, &qp->sq.pbl);
		ha->qlnxr_debug = 0;
		kfree(qp->wqe_wr_id);
	}

	if (qlnxr_qp_has_rq(qp)) {
		QL_DPRINT12(ha, "freeing RQ\n");
		ha->qlnxr_debug = 1;
	//	ecore_chain_free(dev->cdev, &qp->rq.pbl);
		ha->qlnxr_debug = 0;
		kfree(qp->rqe_wr_id);
	}

	QL_DPRINT12(ha, "exit\n");
	return;
}

int
qlnxr_free_qp_resources(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp)
{
	int		rc = 0;
	qlnx_host_t	*ha;
	struct ecore_rdma_destroy_qp_out_params d_out_params;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
#if 0
	if (qp->qp_type != IB_QPT_GSI) {
		rc = ecore_rdma_destroy_qp(dev->rdma_ctx, qp->ecore_qp,
				&d_out_params);
		if (rc)
			return rc;
	}

	if (qp->ibqp.uobject && qp->ibqp.uobject->context)
		qlnxr_cleanup_user(dev, qp);
	else
		qlnxr_cleanup_kernel(dev, qp);
#endif

	if (qp->ibqp.uobject && qp->ibqp.uobject->context)
		qlnxr_cleanup_user(dev, qp);
	else
		qlnxr_cleanup_kernel(dev, qp);

	if (qp->qp_type != IB_QPT_GSI) {
		rc = ecore_rdma_destroy_qp(dev->rdma_ctx, qp->ecore_qp,
				&d_out_params);
		if (rc)
			return rc;
	}

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

int
qlnxr_destroy_qp(struct ib_qp *ibqp)
{
	struct qlnxr_qp *qp = get_qlnxr_qp(ibqp);
	struct qlnxr_dev *dev = qp->dev;
	int rc = 0;
	struct ib_qp_attr attr;
	int attr_mask = 0;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter qp = %p, qp_type=%d\n", qp, qp->qp_type);

	qp->destroyed = 1;

	if (QLNX_IS_ROCE(dev) && (qp->state != (ECORE_ROCE_QP_STATE_RESET |
				  ECORE_ROCE_QP_STATE_ERR |
				  ECORE_ROCE_QP_STATE_INIT))) {

		attr.qp_state = IB_QPS_ERR;
		attr_mask |= IB_QP_STATE;

		/* change the QP state to ERROR */
		qlnxr_modify_qp(ibqp, &attr, attr_mask, NULL);
	}

	if (qp->qp_type == IB_QPT_GSI)
		qlnxr_destroy_gsi_qp(dev);

	qp->sig = ~qp->sig;

	qlnxr_free_qp_resources(dev, qp);

	if (atomic_dec_and_test(&qp->refcnt)) {
		/* TODO: only for iWARP? */
		qlnxr_idr_remove(dev, qp->qp_id);
		kfree(qp);
	}

	QL_DPRINT12(ha, "exit\n");
	return rc;
}

static inline int
qlnxr_wq_is_full(struct qlnxr_qp_hwq_info *wq)
{
	return (((wq->prod + 1) % wq->max_wr) == wq->cons);
}

static int
sge_data_len(struct ib_sge *sg_list, int num_sge)
{
	int i, len = 0;
	for (i = 0; i < num_sge; i++)
		len += sg_list[i].length;
	return len;
}

static void
swap_wqe_data64(u64 *p)
{
	int i;

	for (i = 0; i < QLNXR_SQE_ELEMENT_SIZE / sizeof(u64); i++, p++)
		*p = cpu_to_be64(cpu_to_le64(*p));
}


static u32
qlnxr_prepare_sq_inline_data(struct qlnxr_dev *dev,
	struct qlnxr_qp		*qp,
	u8			*wqe_size,
	struct ib_send_wr	*wr,
	struct ib_send_wr	**bad_wr,
	u8			*bits,
	u8			bit)
{
	int i, seg_siz;
	char *seg_prt, *wqe;
	u32 data_size = sge_data_len(wr->sg_list, wr->num_sge);
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter[%d]\n", data_size);

	if (data_size > ROCE_REQ_MAX_INLINE_DATA_SIZE) {
		QL_DPRINT12(ha,
			"Too much inline data in WR:[%d, %d]\n",
			data_size, ROCE_REQ_MAX_INLINE_DATA_SIZE);
		*bad_wr = wr;
		return 0;
	}

	if (!data_size)
		return data_size;

	/* set the bit */
	*bits |= bit;

	seg_prt = wqe = NULL;
	seg_siz = 0;

	/* copy data inline */
	for (i = 0; i < wr->num_sge; i++) {
		u32 len = wr->sg_list[i].length;
		void *src = (void *)(uintptr_t)wr->sg_list[i].addr;

		while (len > 0) {
			u32 cur;

			/* new segment required */
			if (!seg_siz) {
				wqe = (char *)ecore_chain_produce(&qp->sq.pbl);
				seg_prt = wqe;
				seg_siz = sizeof(struct rdma_sq_common_wqe);
				(*wqe_size)++;
			}

			/* calculate currently allowed length */
			cur = MIN(len, seg_siz);

			memcpy(seg_prt, src, cur);

			/* update segment variables */
			seg_prt += cur;
			seg_siz -= cur;
			/* update sge variables */
			src += cur;
			len -= cur;

			/* swap fully-completed segments */
			if (!seg_siz)
				swap_wqe_data64((u64 *)wqe);
		}
	}

	/* swap last not completed segment */
	if (seg_siz)
		swap_wqe_data64((u64 *)wqe);

	QL_DPRINT12(ha, "exit\n");
	return data_size;
}

static u32
qlnxr_prepare_sq_sges(struct qlnxr_dev *dev, struct qlnxr_qp *qp,
	u8 *wqe_size, struct ib_send_wr *wr)
{
	int i;
	u32 data_size = 0;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter wr->num_sge = %d \n", wr->num_sge);
 
	for (i = 0; i < wr->num_sge; i++) {
		struct rdma_sq_sge *sge = ecore_chain_produce(&qp->sq.pbl);

		TYPEPTR_ADDR_SET(sge, addr, wr->sg_list[i].addr);
		sge->l_key = cpu_to_le32(wr->sg_list[i].lkey);
		sge->length = cpu_to_le32(wr->sg_list[i].length);
		data_size += wr->sg_list[i].length;
	}

	if (wqe_size)
		*wqe_size += wr->num_sge;

	QL_DPRINT12(ha, "exit data_size = %d\n", data_size);
	return data_size;
}

static u32
qlnxr_prepare_sq_rdma_data(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct rdma_sq_rdma_wqe_1st *rwqe,
	struct rdma_sq_rdma_wqe_2nd *rwqe2,
	struct ib_send_wr *wr,
	struct ib_send_wr **bad_wr)
{
	qlnx_host_t	*ha;
	u32             ret = 0;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	rwqe2->r_key = cpu_to_le32(rdma_wr(wr)->rkey);
	TYPEPTR_ADDR_SET(rwqe2, remote_va, rdma_wr(wr)->remote_addr);

	if (wr->send_flags & IB_SEND_INLINE) {
		u8 flags = 0;
		SET_FIELD2(flags, RDMA_SQ_RDMA_WQE_1ST_INLINE_FLG, 1);
		return qlnxr_prepare_sq_inline_data(dev, qp, &rwqe->wqe_size,
				wr, bad_wr, &rwqe->flags, flags);
	}

	ret = qlnxr_prepare_sq_sges(dev, qp, &rwqe->wqe_size, wr);

	QL_DPRINT12(ha, "exit ret = 0x%x\n", ret);

	return (ret);
}

static u32
qlnxr_prepare_sq_send_data(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct rdma_sq_send_wqe *swqe,
	struct rdma_sq_send_wqe *swqe2,
	struct ib_send_wr *wr,
	struct ib_send_wr **bad_wr)
{
	qlnx_host_t	*ha;
	u32             ret = 0;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	memset(swqe2, 0, sizeof(*swqe2));

	if (wr->send_flags & IB_SEND_INLINE) {
		u8 flags = 0;
		SET_FIELD2(flags, RDMA_SQ_SEND_WQE_INLINE_FLG, 1);
		return qlnxr_prepare_sq_inline_data(dev, qp, &swqe->wqe_size,
				wr, bad_wr, &swqe->flags, flags);
	}

	ret = qlnxr_prepare_sq_sges(dev, qp, &swqe->wqe_size, wr);

	QL_DPRINT12(ha, "exit ret = 0x%x\n", ret);

	return (ret);
}

static void
qlnx_handle_completed_mrs(struct qlnxr_dev *dev, struct mr_info *info)
{
	qlnx_host_t	*ha;

	ha = dev->ha;

	int work = info->completed - info->completed_handled - 1;

	QL_DPRINT12(ha, "enter [%d]\n", work);
 
	while (work-- > 0 && !list_empty(&info->inuse_pbl_list)) {
		struct qlnxr_pbl *pbl;

		/* Free all the page list that are possible to be freed
		 * (all the ones that were invalidated), under the assumption
		 * that if an FMR was completed successfully that means that
		 * if there was an invalidate operation before it also ended
		 */
		pbl = list_first_entry(&info->inuse_pbl_list,
				       struct qlnxr_pbl,
				       list_entry);
		list_del(&pbl->list_entry);
		list_add_tail(&pbl->list_entry, &info->free_pbl_list);
		info->completed_handled++;
	}

	QL_DPRINT12(ha, "exit\n");
	return;
}

#if __FreeBSD_version >= 1102000

static int qlnxr_prepare_reg(struct qlnxr_qp *qp,
		struct rdma_sq_fmr_wqe_1st *fwqe1,
		struct ib_reg_wr *wr)
{
	struct qlnxr_mr *mr = get_qlnxr_mr(wr->mr);
	struct rdma_sq_fmr_wqe_2nd *fwqe2;

	fwqe2 = (struct rdma_sq_fmr_wqe_2nd *)ecore_chain_produce(&qp->sq.pbl);
	fwqe1->addr.hi = upper_32_bits(mr->ibmr.iova);
	fwqe1->addr.lo = lower_32_bits(mr->ibmr.iova);
	fwqe1->l_key = wr->key;

	fwqe2->access_ctrl = 0;

	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_REMOTE_READ,
		!!(wr->access & IB_ACCESS_REMOTE_READ));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_REMOTE_WRITE,
		!!(wr->access & IB_ACCESS_REMOTE_WRITE));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_ENABLE_ATOMIC,
		!!(wr->access & IB_ACCESS_REMOTE_ATOMIC));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_LOCAL_READ, 1);
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_LOCAL_WRITE,
		!!(wr->access & IB_ACCESS_LOCAL_WRITE));
	fwqe2->fmr_ctrl = 0;

	SET_FIELD2(fwqe2->fmr_ctrl, RDMA_SQ_FMR_WQE_2ND_PAGE_SIZE_LOG,
		ilog2(mr->ibmr.page_size) - 12);

	fwqe2->length_hi = 0; /* TODO - figure out why length is only 32bit.. */
	fwqe2->length_lo = mr->ibmr.length;
	fwqe2->pbl_addr.hi = upper_32_bits(mr->info.pbl_table->pa);
	fwqe2->pbl_addr.lo = lower_32_bits(mr->info.pbl_table->pa);

	qp->wqe_wr_id[qp->sq.prod].mr = mr;

	return 0;
}

#else

static void
build_frmr_pbes(struct qlnxr_dev *dev, struct ib_send_wr *wr,
	struct mr_info *info)
{
	int i;
	u64 buf_addr = 0;
	int num_pbes, total_num_pbes = 0;
	struct regpair *pbe;
	struct qlnxr_pbl *pbl_tbl = info->pbl_table;
	struct qlnxr_pbl_info *pbl_info = &info->pbl_info;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	pbe = (struct regpair *)pbl_tbl->va;
	num_pbes = 0;

	for (i = 0; i < wr->wr.fast_reg.page_list_len; i++) {
		buf_addr = wr->wr.fast_reg.page_list->page_list[i];
		pbe->lo = cpu_to_le32((u32)buf_addr);
		pbe->hi = cpu_to_le32((u32)upper_32_bits(buf_addr));

		num_pbes += 1;
		pbe++;
		total_num_pbes++;

		if (total_num_pbes == pbl_info->num_pbes)
			return;

		/* if the given pbl is full storing the pbes,
		 * move to next pbl.
		 */
		if (num_pbes ==
		    (pbl_info->pbl_size / sizeof(u64))) {
			pbl_tbl++;
			pbe = (struct regpair *)pbl_tbl->va;
			num_pbes = 0;
		}
	}
	QL_DPRINT12(ha, "exit\n");

	return;
}

static int
qlnxr_prepare_safe_pbl(struct qlnxr_dev *dev, struct mr_info *info)
{
	int rc = 0;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	if (info->completed == 0) {
		//DP_VERBOSE(dev, QLNXR_MSG_MR, "First FMR\n");
		/* first fmr */
		return 0;
	}

	qlnx_handle_completed_mrs(dev, info);

	list_add_tail(&info->pbl_table->list_entry, &info->inuse_pbl_list);

	if (list_empty(&info->free_pbl_list)) {
		info->pbl_table = qlnxr_alloc_pbl_tbl(dev, &info->pbl_info,
							  GFP_ATOMIC);
	} else {
		info->pbl_table = list_first_entry(&info->free_pbl_list,
					struct qlnxr_pbl,
					list_entry);
		list_del(&info->pbl_table->list_entry);
	}

	if (!info->pbl_table)
		rc = -ENOMEM;

	QL_DPRINT12(ha, "exit\n");
	return rc;
}

static inline int
qlnxr_prepare_fmr(struct qlnxr_qp *qp,
	struct rdma_sq_fmr_wqe_1st *fwqe1,
	struct ib_send_wr *wr)
{
	struct qlnxr_dev *dev = qp->dev;
	u64 fbo;
	struct qlnxr_fast_reg_page_list *frmr_list =
		get_qlnxr_frmr_list(wr->wr.fast_reg.page_list);
	struct rdma_sq_fmr_wqe *fwqe2 =
		(struct rdma_sq_fmr_wqe *)ecore_chain_produce(&qp->sq.pbl);
	int rc = 0;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	if (wr->wr.fast_reg.page_list_len == 0)
		BUG();

	rc = qlnxr_prepare_safe_pbl(dev, &frmr_list->info);
	if (rc)
		return rc;

	fwqe1->addr.hi = upper_32_bits(wr->wr.fast_reg.iova_start);
	fwqe1->addr.lo = lower_32_bits(wr->wr.fast_reg.iova_start);
	fwqe1->l_key = wr->wr.fast_reg.rkey;

	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_REMOTE_READ,
		   !!(wr->wr.fast_reg.access_flags & IB_ACCESS_REMOTE_READ));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_REMOTE_WRITE,
		   !!(wr->wr.fast_reg.access_flags & IB_ACCESS_REMOTE_WRITE));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_ENABLE_ATOMIC,
		   !!(wr->wr.fast_reg.access_flags & IB_ACCESS_REMOTE_ATOMIC));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_LOCAL_READ, 1);
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_LOCAL_WRITE,
		   !!(wr->wr.fast_reg.access_flags & IB_ACCESS_LOCAL_WRITE));

	fwqe2->fmr_ctrl = 0;

	SET_FIELD2(fwqe2->fmr_ctrl, RDMA_SQ_FMR_WQE_2ND_PAGE_SIZE_LOG,
		   ilog2(1 << wr->wr.fast_reg.page_shift) - 12);
	SET_FIELD2(fwqe2->fmr_ctrl, RDMA_SQ_FMR_WQE_2ND_ZERO_BASED, 0);

	fwqe2->length_hi = 0; /* Todo - figure this out... why length is only 32bit.. */
	fwqe2->length_lo = wr->wr.fast_reg.length;
	fwqe2->pbl_addr.hi = upper_32_bits(frmr_list->info.pbl_table->pa);
	fwqe2->pbl_addr.lo = lower_32_bits(frmr_list->info.pbl_table->pa);

	/* produce another wqe for fwqe3 */
	ecore_chain_produce(&qp->sq.pbl);

	fbo = wr->wr.fast_reg.iova_start -
	    (wr->wr.fast_reg.page_list->page_list[0] & PAGE_MASK);

	QL_DPRINT12(ha, "wr.fast_reg.iova_start = %p rkey=%x addr=%x:%x"
		" length = %x pbl_addr %x:%x\n",
		wr->wr.fast_reg.iova_start, wr->wr.fast_reg.rkey,
		fwqe1->addr.hi, fwqe1->addr.lo, fwqe2->length_lo,
		fwqe2->pbl_addr.hi, fwqe2->pbl_addr.lo);

	build_frmr_pbes(dev, wr, &frmr_list->info);

	qp->wqe_wr_id[qp->sq.prod].frmr = frmr_list;

	QL_DPRINT12(ha, "exit\n");
	return 0;
}

#endif /* #if __FreeBSD_version >= 1102000 */

static enum ib_wc_opcode
qlnxr_ib_to_wc_opcode(enum ib_wr_opcode opcode)
{
	switch (opcode) {
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		return IB_WC_RDMA_WRITE;
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_INV:
		return IB_WC_SEND;
	case IB_WR_RDMA_READ:
		return IB_WC_RDMA_READ;
	case IB_WR_ATOMIC_CMP_AND_SWP:
		return IB_WC_COMP_SWAP;
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		return IB_WC_FETCH_ADD;

#if __FreeBSD_version >= 1102000
	case IB_WR_REG_MR:
		return IB_WC_REG_MR;
#else
	case IB_WR_FAST_REG_MR:
		return IB_WC_FAST_REG_MR;
#endif /* #if __FreeBSD_version >= 1102000 */

	case IB_WR_LOCAL_INV:
		return IB_WC_LOCAL_INV;
	default:
		return IB_WC_SEND;
	}
}
static inline bool
qlnxr_can_post_send(struct qlnxr_qp *qp, struct ib_send_wr *wr)
{
	int wq_is_full, err_wr, pbl_is_full;
	struct qlnxr_dev *dev = qp->dev;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter[qp, wr] = [%p,%p]\n", qp, wr);
 
	/* prevent SQ overflow and/or processing of a bad WR */
	err_wr = wr->num_sge > qp->sq.max_sges;
	wq_is_full = qlnxr_wq_is_full(&qp->sq);
	pbl_is_full = ecore_chain_get_elem_left_u32(&qp->sq.pbl) <
		      QLNXR_MAX_SQE_ELEMENTS_PER_SQE;
	if (wq_is_full || err_wr || pbl_is_full) {
		if (wq_is_full &&
		    !(qp->err_bitmap & QLNXR_QP_ERR_SQ_FULL)) {

			qp->err_bitmap |= QLNXR_QP_ERR_SQ_FULL;

			QL_DPRINT12(ha,
				"error: WQ is full. Post send on QP failed"
				" (this error appears only once) "
				"[qp, wr, qp->err_bitmap]=[%p, %p, 0x%x]\n",
				qp, wr, qp->err_bitmap);
		}

		if (err_wr &&
		    !(qp->err_bitmap & QLNXR_QP_ERR_BAD_SR)) {

			qp->err_bitmap |= QLNXR_QP_ERR_BAD_SR;

			QL_DPRINT12(ha,
				"error: WQ is bad. Post send on QP failed"
				" (this error appears only once) "
				"[qp, wr, qp->err_bitmap]=[%p, %p, 0x%x]\n",
				qp, wr, qp->err_bitmap);
		}

		if (pbl_is_full &&
		    !(qp->err_bitmap & QLNXR_QP_ERR_SQ_PBL_FULL)) {

			qp->err_bitmap |= QLNXR_QP_ERR_SQ_PBL_FULL;

			QL_DPRINT12(ha,
				"error: WQ PBL is full. Post send on QP failed"
				" (this error appears only once) "
				"[qp, wr, qp->err_bitmap]=[%p, %p, 0x%x]\n",
				qp, wr, qp->err_bitmap);
		}
		return false;
	}
	QL_DPRINT12(ha, "exit[qp, wr] = [%p,%p]\n", qp, wr);
	return true;
}

int
qlnxr_post_send(struct ib_qp *ibqp,
	struct ib_send_wr *wr,
	struct ib_send_wr **bad_wr)
{
	struct qlnxr_dev	*dev = get_qlnxr_dev(ibqp->device);
	struct qlnxr_qp		*qp = get_qlnxr_qp(ibqp);
	unsigned long 		flags;
	int 			status = 0, rc = 0;
	bool			comp;
	qlnx_host_t		*ha;
	uint32_t		reg_addr;
 
	*bad_wr = NULL;
	ha = dev->ha;

	QL_DPRINT12(ha, "exit[ibqp, wr, bad_wr] = [%p, %p, %p]\n",
		ibqp, wr, bad_wr);

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;

	if (qp->qp_type == IB_QPT_GSI)
		return qlnxr_gsi_post_send(ibqp, wr, bad_wr);

	spin_lock_irqsave(&qp->q_lock, flags);

	if (QLNX_IS_ROCE(dev) && (qp->state != ECORE_ROCE_QP_STATE_RTS) &&
	    (qp->state != ECORE_ROCE_QP_STATE_ERR) &&
	    (qp->state != ECORE_ROCE_QP_STATE_SQD)) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		*bad_wr = wr;
		QL_DPRINT11(ha, "QP in wrong state! QP icid=0x%x state %d\n",
			qp->icid, qp->state);
		return -EINVAL;
	}

	if (!wr) {
		QL_DPRINT11(ha, "Got an empty post send???\n");
	}

	while (wr) {
		struct rdma_sq_common_wqe	*wqe;
		struct rdma_sq_send_wqe		*swqe;
		struct rdma_sq_send_wqe		*swqe2;
		struct rdma_sq_rdma_wqe_1st	*rwqe;
		struct rdma_sq_rdma_wqe_2nd	*rwqe2;
		struct rdma_sq_local_inv_wqe	*iwqe;
		struct rdma_sq_atomic_wqe	*awqe1;
		struct rdma_sq_atomic_wqe	*awqe2;
		struct rdma_sq_atomic_wqe	*awqe3;
		struct rdma_sq_fmr_wqe_1st	*fwqe1;

		if (!qlnxr_can_post_send(qp, wr)) {
			status = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		wqe = ecore_chain_produce(&qp->sq.pbl);

		qp->wqe_wr_id[qp->sq.prod].signaled =
			!!(wr->send_flags & IB_SEND_SIGNALED) || qp->signaled;

		/* common fields */
		wqe->flags = 0;
		wqe->flags |= (RDMA_SQ_SEND_WQE_COMP_FLG_MASK <<
				RDMA_SQ_SEND_WQE_COMP_FLG_SHIFT);

		SET_FIELD2(wqe->flags, RDMA_SQ_SEND_WQE_SE_FLG, \
			!!(wr->send_flags & IB_SEND_SOLICITED));

		comp = (!!(wr->send_flags & IB_SEND_SIGNALED)) ||
				(qp->signaled);

		SET_FIELD2(wqe->flags, RDMA_SQ_SEND_WQE_COMP_FLG, comp);
		SET_FIELD2(wqe->flags, RDMA_SQ_SEND_WQE_RD_FENCE_FLG,  \
			!!(wr->send_flags & IB_SEND_FENCE));

		wqe->prev_wqe_size = qp->prev_wqe_size;

		qp->wqe_wr_id[qp->sq.prod].opcode = qlnxr_ib_to_wc_opcode(wr->opcode);


		switch (wr->opcode) {

		case IB_WR_SEND_WITH_IMM:

			wqe->req_type = RDMA_SQ_REQ_TYPE_SEND_WITH_IMM;
			swqe = (struct rdma_sq_send_wqe *)wqe;
			swqe->wqe_size = 2;
			swqe2 = (struct rdma_sq_send_wqe *)
					ecore_chain_produce(&qp->sq.pbl);
			swqe->inv_key_or_imm_data =
				cpu_to_le32(wr->ex.imm_data);
			swqe->length = cpu_to_le32(
						qlnxr_prepare_sq_send_data(dev,
							qp, swqe, swqe2, wr,
							bad_wr));

			qp->wqe_wr_id[qp->sq.prod].wqe_size = swqe->wqe_size;
			qp->prev_wqe_size = swqe->wqe_size;
			qp->wqe_wr_id[qp->sq.prod].bytes_len = swqe->length;

			QL_DPRINT12(ha, "SEND w/ IMM length = %d imm data=%x\n",
				swqe->length, wr->ex.imm_data);

			break;

		case IB_WR_SEND:

			wqe->req_type = RDMA_SQ_REQ_TYPE_SEND;
			swqe = (struct rdma_sq_send_wqe *)wqe;

			swqe->wqe_size = 2;
			swqe2 = (struct rdma_sq_send_wqe *)
					ecore_chain_produce(&qp->sq.pbl);
			swqe->length = cpu_to_le32(
						qlnxr_prepare_sq_send_data(dev,
							qp, swqe, swqe2, wr,
							bad_wr));
			qp->wqe_wr_id[qp->sq.prod].wqe_size = swqe->wqe_size;
			qp->prev_wqe_size = swqe->wqe_size;
			qp->wqe_wr_id[qp->sq.prod].bytes_len = swqe->length;

			QL_DPRINT12(ha, "SEND w/o IMM length = %d\n",
				swqe->length);

			break;

		case IB_WR_SEND_WITH_INV:

			wqe->req_type = RDMA_SQ_REQ_TYPE_SEND_WITH_INVALIDATE;
			swqe = (struct rdma_sq_send_wqe *)wqe;
			swqe2 = (struct rdma_sq_send_wqe *)
					ecore_chain_produce(&qp->sq.pbl);
			swqe->wqe_size = 2;
			swqe->inv_key_or_imm_data =
				cpu_to_le32(wr->ex.invalidate_rkey);
			swqe->length = cpu_to_le32(qlnxr_prepare_sq_send_data(dev,
						qp, swqe, swqe2, wr, bad_wr));
			qp->wqe_wr_id[qp->sq.prod].wqe_size = swqe->wqe_size;
			qp->prev_wqe_size = swqe->wqe_size;
			qp->wqe_wr_id[qp->sq.prod].bytes_len = swqe->length;

			QL_DPRINT12(ha, "SEND w INVALIDATE length = %d\n",
				swqe->length);
			break;

		case IB_WR_RDMA_WRITE_WITH_IMM:

			wqe->req_type = RDMA_SQ_REQ_TYPE_RDMA_WR_WITH_IMM;
			rwqe = (struct rdma_sq_rdma_wqe_1st *)wqe;

			rwqe->wqe_size = 2;
			rwqe->imm_data = htonl(cpu_to_le32(wr->ex.imm_data));
			rwqe2 = (struct rdma_sq_rdma_wqe_2nd *)
					ecore_chain_produce(&qp->sq.pbl);
			rwqe->length = cpu_to_le32(qlnxr_prepare_sq_rdma_data(dev,
						qp, rwqe, rwqe2, wr, bad_wr));
			qp->wqe_wr_id[qp->sq.prod].wqe_size = rwqe->wqe_size;
			qp->prev_wqe_size = rwqe->wqe_size;
			qp->wqe_wr_id[qp->sq.prod].bytes_len = rwqe->length;

			QL_DPRINT12(ha,
				"RDMA WRITE w/ IMM length = %d imm data=%x\n",
				rwqe->length, rwqe->imm_data);

			break;

		case IB_WR_RDMA_WRITE:

			wqe->req_type = RDMA_SQ_REQ_TYPE_RDMA_WR;
			rwqe = (struct rdma_sq_rdma_wqe_1st *)wqe;

			rwqe->wqe_size = 2;
			rwqe2 = (struct rdma_sq_rdma_wqe_2nd *)
					ecore_chain_produce(&qp->sq.pbl);
			rwqe->length = cpu_to_le32(qlnxr_prepare_sq_rdma_data(dev,
						qp, rwqe, rwqe2, wr, bad_wr));
			qp->wqe_wr_id[qp->sq.prod].wqe_size = rwqe->wqe_size;
			qp->prev_wqe_size = rwqe->wqe_size;
			qp->wqe_wr_id[qp->sq.prod].bytes_len = rwqe->length;

			QL_DPRINT12(ha,
				"RDMA WRITE w/o IMM length = %d\n",
				rwqe->length);

			break;

		case IB_WR_RDMA_READ_WITH_INV:

			QL_DPRINT12(ha,
				"RDMA READ WITH INVALIDATE not supported\n");

			*bad_wr = wr;
			rc = -EINVAL;

			break;

		case IB_WR_RDMA_READ:

			wqe->req_type = RDMA_SQ_REQ_TYPE_RDMA_RD;
			rwqe = (struct rdma_sq_rdma_wqe_1st *)wqe;

			rwqe->wqe_size = 2;
			rwqe2 = (struct rdma_sq_rdma_wqe_2nd *)
					ecore_chain_produce(&qp->sq.pbl);
			rwqe->length = cpu_to_le32(qlnxr_prepare_sq_rdma_data(dev,
						qp, rwqe, rwqe2, wr, bad_wr));

			qp->wqe_wr_id[qp->sq.prod].wqe_size = rwqe->wqe_size;
			qp->prev_wqe_size = rwqe->wqe_size;
			qp->wqe_wr_id[qp->sq.prod].bytes_len = rwqe->length;

			QL_DPRINT12(ha, "RDMA READ length = %d\n",
				rwqe->length);

			break;

		case IB_WR_ATOMIC_CMP_AND_SWP:
		case IB_WR_ATOMIC_FETCH_AND_ADD:

			QL_DPRINT12(ha,
				"ATOMIC operation = %s\n",
				((wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP) ?
					"IB_WR_ATOMIC_CMP_AND_SWP" : 
					"IB_WR_ATOMIC_FETCH_AND_ADD"));

			awqe1 = (struct rdma_sq_atomic_wqe *)wqe;
			awqe1->prev_wqe_size = 4;

			awqe2 = (struct rdma_sq_atomic_wqe *)
					ecore_chain_produce(&qp->sq.pbl);

			TYPEPTR_ADDR_SET(awqe2, remote_va, \
				atomic_wr(wr)->remote_addr);

			awqe2->r_key = cpu_to_le32(atomic_wr(wr)->rkey);

			awqe3 = (struct rdma_sq_atomic_wqe *)
					ecore_chain_produce(&qp->sq.pbl);

			if (wr->opcode == IB_WR_ATOMIC_FETCH_AND_ADD) {
				wqe->req_type = RDMA_SQ_REQ_TYPE_ATOMIC_ADD;
				TYPEPTR_ADDR_SET(awqe3, swap_data,
						 atomic_wr(wr)->compare_add);
			} else {
				wqe->req_type = RDMA_SQ_REQ_TYPE_ATOMIC_CMP_AND_SWAP;
				TYPEPTR_ADDR_SET(awqe3, swap_data,
						 atomic_wr(wr)->swap);
				TYPEPTR_ADDR_SET(awqe3, cmp_data,
						 atomic_wr(wr)->compare_add);
			}

			qlnxr_prepare_sq_sges(dev, qp, NULL, wr);

			qp->wqe_wr_id[qp->sq.prod].wqe_size = awqe1->prev_wqe_size;
			qp->prev_wqe_size = awqe1->prev_wqe_size;

			break;

		case IB_WR_LOCAL_INV:

			QL_DPRINT12(ha,
				"INVALIDATE length (IB_WR_LOCAL_INV)\n");

			iwqe = (struct rdma_sq_local_inv_wqe *)wqe;
			iwqe->prev_wqe_size = 1;

			iwqe->req_type = RDMA_SQ_REQ_TYPE_LOCAL_INVALIDATE;
			iwqe->inv_l_key = wr->ex.invalidate_rkey;
			qp->wqe_wr_id[qp->sq.prod].wqe_size = iwqe->prev_wqe_size;
			qp->prev_wqe_size = iwqe->prev_wqe_size;

			break;

#if __FreeBSD_version >= 1102000

		case IB_WR_REG_MR:

			QL_DPRINT12(ha, "IB_WR_REG_MR\n");

			wqe->req_type = RDMA_SQ_REQ_TYPE_FAST_MR;
			fwqe1 = (struct rdma_sq_fmr_wqe_1st *)wqe;
			fwqe1->wqe_size = 2;

			rc = qlnxr_prepare_reg(qp, fwqe1, reg_wr(wr));
			if (rc) {
				QL_DPRINT11(ha, "IB_WR_REG_MR failed rc=%d\n", rc);
				*bad_wr = wr;
				break;
			}

			qp->wqe_wr_id[qp->sq.prod].wqe_size = fwqe1->wqe_size;
			qp->prev_wqe_size = fwqe1->wqe_size;

			break;
#else
		case IB_WR_FAST_REG_MR:

			QL_DPRINT12(ha, "FAST_MR (IB_WR_FAST_REG_MR)\n");

			wqe->req_type = RDMA_SQ_REQ_TYPE_FAST_MR;
			fwqe1 = (struct rdma_sq_fmr_wqe_1st *)wqe;
			fwqe1->prev_wqe_size = 3;

			rc = qlnxr_prepare_fmr(qp, fwqe1, wr);

			if (rc) {
				QL_DPRINT12(ha,
					"FAST_MR (IB_WR_FAST_REG_MR) failed"
					" rc = %d\n", rc);
				*bad_wr = wr;
				break;
			}

			qp->wqe_wr_id[qp->sq.prod].wqe_size = fwqe1->prev_wqe_size;
			qp->prev_wqe_size = fwqe1->prev_wqe_size;

			break;
#endif /* #if __FreeBSD_version >= 1102000 */

		default:

			QL_DPRINT12(ha, "Invalid Opcode 0x%x!\n", wr->opcode);

			rc = -EINVAL;
			*bad_wr = wr;
			break;
		}

		if (*bad_wr) {
			/*
			 * restore prod to its position before this WR was processed
			 */
			ecore_chain_set_prod(&qp->sq.pbl,
			     le16_to_cpu(qp->sq.db_data.data.value),
			     wqe);
			/* restore prev_wqe_size */
			qp->prev_wqe_size = wqe->prev_wqe_size;
			status = rc;

			QL_DPRINT12(ha, "failed *bad_wr = %p\n", *bad_wr);
			break; /* out of the loop */
		}

		qp->wqe_wr_id[qp->sq.prod].wr_id = wr->wr_id;

		qlnxr_inc_sw_prod(&qp->sq);

		qp->sq.db_data.data.value++;

		wr = wr->next;
	}

	/* Trigger doorbell
	 * If there was a failure in the first WR then it will be triggered in
	 * vane. However this is not harmful (as long as the producer value is
	 * unchanged). For performance reasons we avoid checking for this
	 * redundant doorbell.
	 */
	wmb();
	//writel(qp->sq.db_data.raw, qp->sq.db);

	reg_addr = (uint32_t)((uint8_t *)qp->sq.db - (uint8_t *)ha->cdev.doorbells);
        bus_write_4(ha->pci_dbells, reg_addr, qp->sq.db_data.raw);
        bus_barrier(ha->pci_dbells,  0, 0, BUS_SPACE_BARRIER_READ);

	mmiowb();

	spin_unlock_irqrestore(&qp->q_lock, flags);

	QL_DPRINT12(ha, "exit[ibqp, wr, bad_wr] = [%p, %p, %p]\n",
		ibqp, wr, bad_wr);

	return status;
}

static u32
qlnxr_srq_elem_left(struct qlnxr_srq_hwq_info *hw_srq)
{
	u32 used;

	/* Calculate number of elements used based on producer
	 * count and consumer count and subtract it from max
	 * work request supported so that we get elements left.
	 */
	used = hw_srq->wr_prod_cnt - hw_srq->wr_cons_cnt;

	return hw_srq->max_wr - used;
}


int
qlnxr_post_recv(struct ib_qp *ibqp,
	struct ib_recv_wr *wr,
	struct ib_recv_wr **bad_wr)
{
 	struct qlnxr_qp		*qp = get_qlnxr_qp(ibqp);
	struct qlnxr_dev	*dev = qp->dev;
	unsigned long		flags;
	int			status = 0;
	qlnx_host_t		*ha;
	uint32_t		reg_addr;

	ha = dev->ha;

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;

	QL_DPRINT12(ha, "enter\n");
 
	if (qp->qp_type == IB_QPT_GSI) {
		QL_DPRINT12(ha, "(qp->qp_type = IB_QPT_GSI)\n");
		return qlnxr_gsi_post_recv(ibqp, wr, bad_wr);
	}

	if (qp->srq) {
		QL_DPRINT11(ha, "qp->srq [%p]"
			" QP is associated with SRQ, cannot post RQ buffers\n",
			qp->srq);
		return -EINVAL;
	}

	spin_lock_irqsave(&qp->q_lock, flags);

	if (qp->state == ECORE_ROCE_QP_STATE_RESET) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		*bad_wr = wr;

		QL_DPRINT11(ha, "qp->qp_type = ECORE_ROCE_QP_STATE_RESET\n");

		return -EINVAL;
	}

	while (wr) {
		int i;

		if ((ecore_chain_get_elem_left_u32(&qp->rq.pbl) <
			QLNXR_MAX_RQE_ELEMENTS_PER_RQE) ||
			(wr->num_sge > qp->rq.max_sges)) {
			status = -ENOMEM;
			*bad_wr = wr;
			break;
		}
		for (i = 0; i < wr->num_sge; i++) {
			u32 flags = 0;
			struct rdma_rq_sge *rqe = ecore_chain_produce(&qp->rq.pbl);

			/* first one must include the number of SGE in the list */
			if (!i)
				SET_FIELD(flags, RDMA_RQ_SGE_NUM_SGES, wr->num_sge);

			SET_FIELD(flags, RDMA_RQ_SGE_L_KEY, wr->sg_list[i].lkey);

			RQ_SGE_SET(rqe, wr->sg_list[i].addr, \
				wr->sg_list[i].length, flags);
		}
		/* Special case of no sges. FW requires between 1-4 sges...
		 * in this case we need to post 1 sge with length zero. this is
		 * because rdma write with immediate consumes an RQ. */
		if (!wr->num_sge) {
			u32 flags = 0;
			struct rdma_rq_sge *rqe = ecore_chain_produce(&qp->rq.pbl);

			/* first one must include the number of SGE in the list */
			SET_FIELD(flags, RDMA_RQ_SGE_L_KEY, 0);
			SET_FIELD(flags, RDMA_RQ_SGE_NUM_SGES, 1);

			//RQ_SGE_SET(rqe, 0, 0, flags);
			rqe->addr.hi = 0;
			rqe->addr.lo = 0;

			rqe->length = 0;
			rqe->flags = cpu_to_le32(flags);

			i = 1;
		}

		qp->rqe_wr_id[qp->rq.prod].wr_id = wr->wr_id;
		qp->rqe_wr_id[qp->rq.prod].wqe_size = i;

		qlnxr_inc_sw_prod(&qp->rq);

		wmb();

		qp->rq.db_data.data.value++;

	//	writel(qp->rq.db_data.raw, qp->rq.db);
		mmiowb();
	//	if (QLNX_IS_IWARP(dev)) {
	//		writel(qp->rq.iwarp_db2_data.raw, qp->rq.iwarp_db2);
	//		mmiowb(); /* for second doorbell */
	//	}

		reg_addr = (uint32_t)((uint8_t *)qp->rq.db -
				(uint8_t *)ha->cdev.doorbells);

		bus_write_4(ha->pci_dbells, reg_addr, qp->rq.db_data.raw);
		bus_barrier(ha->pci_dbells,  0, 0, BUS_SPACE_BARRIER_READ);

		if (QLNX_IS_IWARP(dev)) {
			reg_addr = (uint32_t)((uint8_t *)qp->rq.iwarp_db2 -
						(uint8_t *)ha->cdev.doorbells);
			bus_write_4(ha->pci_dbells, reg_addr, \
				qp->rq.iwarp_db2_data.raw);
			bus_barrier(ha->pci_dbells,  0, 0, \
				BUS_SPACE_BARRIER_READ);
		}

		wr = wr->next;
	}

	spin_unlock_irqrestore(&qp->q_lock, flags);

	QL_DPRINT12(ha, "exit status = 0x%x\n", status);

	return status;
}

/* In fmr we need to increase the number of fmr completed counter for the fmr
 * algorithm determining whether we can free a pbl or not.
 * we need to perform this whether the work request was signaled or not. for
 * this purpose we call this function from the condition that checks if a wr
 * should be skipped, to make sure we don't miss it ( possibly this fmr
 * operation was not signalted)
 */
static inline void
qlnxr_chk_if_fmr(struct qlnxr_qp *qp)
{
#if __FreeBSD_version >= 1102000

	if (qp->wqe_wr_id[qp->sq.cons].opcode == IB_WC_REG_MR)
		qp->wqe_wr_id[qp->sq.cons].mr->info.completed++;
#else
	if (qp->wqe_wr_id[qp->sq.cons].opcode == IB_WC_FAST_REG_MR)
		qp->wqe_wr_id[qp->sq.cons].frmr->info.completed++;

#endif /* #if __FreeBSD_version >= 1102000 */
}

static int
process_req(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_cq *cq,
	int num_entries,
	struct ib_wc *wc,
	u16 hw_cons,
	enum ib_wc_status status,
	int force)
{
	u16		cnt = 0;
	qlnx_host_t	*ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	while (num_entries && qp->sq.wqe_cons != hw_cons) {
		if (!qp->wqe_wr_id[qp->sq.cons].signaled && !force) {
			qlnxr_chk_if_fmr(qp);
			/* skip WC */
			goto next_cqe;
		}

		/* fill WC */
		wc->status = status;
		wc->vendor_err = 0;
		wc->wc_flags = 0;
		wc->src_qp = qp->id;
		wc->qp = &qp->ibqp;

		// common section
		wc->wr_id = qp->wqe_wr_id[qp->sq.cons].wr_id;
		wc->opcode = qp->wqe_wr_id[qp->sq.cons].opcode;

		switch (wc->opcode) {

		case IB_WC_RDMA_WRITE:

			wc->byte_len = qp->wqe_wr_id[qp->sq.cons].bytes_len;

			QL_DPRINT12(ha,
				"opcode = IB_WC_RDMA_WRITE bytes = %d\n",
				qp->wqe_wr_id[qp->sq.cons].bytes_len);
			break;

		case IB_WC_COMP_SWAP:
		case IB_WC_FETCH_ADD:
			wc->byte_len = 8;
			break;

#if __FreeBSD_version >= 1102000
		case IB_WC_REG_MR:
			qp->wqe_wr_id[qp->sq.cons].mr->info.completed++;
			break;
#else
		case IB_WC_FAST_REG_MR:
			qp->wqe_wr_id[qp->sq.cons].frmr->info.completed++;
			break;
#endif /* #if __FreeBSD_version >= 1102000 */

		case IB_WC_RDMA_READ:
		case IB_WC_SEND:

			QL_DPRINT12(ha, "opcode = 0x%x \n", wc->opcode);
			break;
		default:
			;//DP_ERR("TBD ERROR");
		}

		num_entries--;
		wc++;
		cnt++;
next_cqe:
		while (qp->wqe_wr_id[qp->sq.cons].wqe_size--)
			ecore_chain_consume(&qp->sq.pbl);
		qlnxr_inc_sw_cons(&qp->sq);
	}

	QL_DPRINT12(ha, "exit cnt = 0x%x\n", cnt);
	return cnt;
}

static int
qlnxr_poll_cq_req(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_cq *cq,
	int num_entries,
	struct ib_wc *wc,
	struct rdma_cqe_requester *req)
{
	int		cnt = 0;
	qlnx_host_t	*ha = dev->ha;

	QL_DPRINT12(ha, "enter req->status = 0x%x\n", req->status);
 
	switch (req->status) {

	case RDMA_CQE_REQ_STS_OK:

		cnt = process_req(dev, qp, cq, num_entries, wc, req->sq_cons,
			IB_WC_SUCCESS, 0);
		break;

	case RDMA_CQE_REQ_STS_WORK_REQUEST_FLUSHED_ERR:

		if (qp->state != ECORE_ROCE_QP_STATE_ERR)
		cnt = process_req(dev, qp, cq, num_entries, wc, req->sq_cons,
				  IB_WC_WR_FLUSH_ERR, 1);
		break;

	default: /* other errors case */

		/* process all WQE before the cosumer */
		qp->state = ECORE_ROCE_QP_STATE_ERR;
		cnt = process_req(dev, qp, cq, num_entries, wc,
				req->sq_cons - 1, IB_WC_SUCCESS, 0);
		wc += cnt;
		/* if we have extra WC fill it with actual error info */

		if (cnt < num_entries) {
			enum ib_wc_status wc_status;

			switch (req->status) {
			case 	RDMA_CQE_REQ_STS_BAD_RESPONSE_ERR:
				wc_status = IB_WC_BAD_RESP_ERR;
				break;
			case 	RDMA_CQE_REQ_STS_LOCAL_LENGTH_ERR:
				wc_status = IB_WC_LOC_LEN_ERR;
				break;
			case    RDMA_CQE_REQ_STS_LOCAL_QP_OPERATION_ERR:
				wc_status = IB_WC_LOC_QP_OP_ERR;
				break;
			case    RDMA_CQE_REQ_STS_LOCAL_PROTECTION_ERR:
				wc_status = IB_WC_LOC_PROT_ERR;
				break;
			case    RDMA_CQE_REQ_STS_MEMORY_MGT_OPERATION_ERR:
				wc_status = IB_WC_MW_BIND_ERR;
				break;
			case    RDMA_CQE_REQ_STS_REMOTE_INVALID_REQUEST_ERR:
				wc_status = IB_WC_REM_INV_REQ_ERR;
				break;
			case    RDMA_CQE_REQ_STS_REMOTE_ACCESS_ERR:
				wc_status = IB_WC_REM_ACCESS_ERR;
				break;
			case    RDMA_CQE_REQ_STS_REMOTE_OPERATION_ERR:
				wc_status = IB_WC_REM_OP_ERR;
				break;
			case    RDMA_CQE_REQ_STS_RNR_NAK_RETRY_CNT_ERR:
				wc_status = IB_WC_RNR_RETRY_EXC_ERR;
				break;
			case    RDMA_CQE_REQ_STS_TRANSPORT_RETRY_CNT_ERR:
				wc_status = IB_WC_RETRY_EXC_ERR;
				break;
			default:
				wc_status = IB_WC_GENERAL_ERR;
			}

			cnt += process_req(dev, qp, cq, 1, wc, req->sq_cons,
					wc_status, 1 /* force use of WC */);
		}
	}

	QL_DPRINT12(ha, "exit cnt = %d\n", cnt);
	return cnt;
}

static void
__process_resp_one(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_cq *cq,
	struct ib_wc *wc,
	struct rdma_cqe_responder *resp,
	u64 wr_id)
{
	enum ib_wc_status	wc_status = IB_WC_SUCCESS;
#if __FreeBSD_version < 1102000
	u8			flags;
#endif
	qlnx_host_t		*ha = dev->ha;

	QL_DPRINT12(ha, "enter qp = %p resp->status = 0x%x\n",
		qp, resp->status);
 
	wc->opcode = IB_WC_RECV;
	wc->wc_flags = 0;

	switch (resp->status) {

	case RDMA_CQE_RESP_STS_LOCAL_ACCESS_ERR:
		wc_status = IB_WC_LOC_ACCESS_ERR;
		break;

	case RDMA_CQE_RESP_STS_LOCAL_LENGTH_ERR:
		wc_status = IB_WC_LOC_LEN_ERR;
		break;

	case RDMA_CQE_RESP_STS_LOCAL_QP_OPERATION_ERR:
		wc_status = IB_WC_LOC_QP_OP_ERR;
		break;

	case RDMA_CQE_RESP_STS_LOCAL_PROTECTION_ERR:
		wc_status = IB_WC_LOC_PROT_ERR;
		break;

	case RDMA_CQE_RESP_STS_MEMORY_MGT_OPERATION_ERR:
		wc_status = IB_WC_MW_BIND_ERR;
		break;

	case RDMA_CQE_RESP_STS_REMOTE_INVALID_REQUEST_ERR:
		wc_status = IB_WC_REM_INV_RD_REQ_ERR;
		break;

	case RDMA_CQE_RESP_STS_OK:

#if __FreeBSD_version >= 1102000
		if (resp->flags & QLNXR_RESP_IMM) {
			wc->ex.imm_data =
				le32_to_cpu(resp->imm_data_or_inv_r_Key);
			wc->wc_flags |= IB_WC_WITH_IMM;

			if (resp->flags & QLNXR_RESP_RDMA)
				wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;

			if (resp->flags & QLNXR_RESP_INV) {
				QL_DPRINT11(ha,
					"Invalid flags QLNXR_RESP_INV [0x%x]"
					"qp = %p qp->id = 0x%x cq = %p"
					" cq->icid = 0x%x\n",
					resp->flags, qp, qp->id, cq, cq->icid );
			}
		} else if (resp->flags & QLNXR_RESP_INV) {
			wc->ex.imm_data =
				le32_to_cpu(resp->imm_data_or_inv_r_Key);
			wc->wc_flags |= IB_WC_WITH_INVALIDATE;

			if (resp->flags & QLNXR_RESP_RDMA) {
				QL_DPRINT11(ha,
					"Invalid flags QLNXR_RESP_RDMA [0x%x]"
					"qp = %p qp->id = 0x%x cq = %p"
					" cq->icid = 0x%x\n",
					resp->flags, qp, qp->id, cq, cq->icid );
			}
		} else if (resp->flags & QLNXR_RESP_RDMA) {
			QL_DPRINT11(ha, "Invalid flags QLNXR_RESP_RDMA [0x%x]"
				"qp = %p qp->id = 0x%x cq = %p cq->icid = 0x%x\n",
				resp->flags, qp, qp->id, cq, cq->icid );
		}
#else
		wc_status = IB_WC_SUCCESS;
		wc->byte_len = le32_to_cpu(resp->length);

		flags = resp->flags & QLNXR_RESP_RDMA_IMM;

		switch (flags) {

		case QLNXR_RESP_RDMA_IMM:
			/* update opcode */
			wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;
			/* fall to set imm data */
		case QLNXR_RESP_IMM:
			wc->ex.imm_data =
				le32_to_cpu(resp->imm_data_or_inv_r_Key);
			wc->wc_flags |= IB_WC_WITH_IMM;
			break;
		case QLNXR_RESP_RDMA:
			QL_DPRINT11(ha, "Invalid flags QLNXR_RESP_RDMA [0x%x]"
				"qp = %p qp->id = 0x%x cq = %p cq->icid = 0x%x\n",
				resp->flags, qp, qp->id, cq, cq->icid );
			break;
		default:
			/* valid configuration, but nothing todo here */
			;
		}
#endif /* #if __FreeBSD_version >= 1102000 */

		break;
	default:
		wc_status = IB_WC_GENERAL_ERR;
	}

	/* fill WC */
	wc->status = wc_status;
	wc->vendor_err = 0;
	wc->src_qp = qp->id;
	wc->qp = &qp->ibqp;
	wc->wr_id = wr_id;

	QL_DPRINT12(ha, "exit status = 0x%x\n", wc_status);

	return;
}

static int
process_resp_one_srq(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_cq *cq,
	struct ib_wc *wc,
	struct rdma_cqe_responder *resp)
{
	struct qlnxr_srq	*srq = qp->srq;
	u64			wr_id;
	qlnx_host_t		*ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	wr_id = HILO_U64(resp->srq_wr_id.hi, resp->srq_wr_id.lo);

	if (resp->status == RDMA_CQE_RESP_STS_WORK_REQUEST_FLUSHED_ERR) {
		wc->status = IB_WC_WR_FLUSH_ERR;
		wc->vendor_err = 0;
		wc->wr_id = wr_id;
		wc->byte_len = 0;
		wc->src_qp = qp->id;
		wc->qp = &qp->ibqp;
		wc->wr_id = wr_id;
	} else {
		__process_resp_one(dev, qp, cq, wc, resp, wr_id);
	}

	/* PBL is maintained in case of WR granularity.
	 * So increment WR consumer after consuming WR
	 */
	srq->hw_srq.wr_cons_cnt++;

	QL_DPRINT12(ha, "exit\n");
	return 1;
}

static int
process_resp_one(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_cq *cq,
	struct ib_wc *wc,
	struct rdma_cqe_responder *resp)
{
	qlnx_host_t	*ha = dev->ha;
	u64		wr_id = qp->rqe_wr_id[qp->rq.cons].wr_id;

	QL_DPRINT12(ha, "enter\n");
 
	__process_resp_one(dev, qp, cq, wc, resp, wr_id);

	while (qp->rqe_wr_id[qp->rq.cons].wqe_size--)
		ecore_chain_consume(&qp->rq.pbl);
	qlnxr_inc_sw_cons(&qp->rq);

	QL_DPRINT12(ha, "exit\n");
	return 1;
}

static int
process_resp_flush(struct qlnxr_qp *qp,
	int num_entries,
	struct ib_wc *wc,
	u16 hw_cons)
{
	u16		cnt = 0;
	qlnx_host_t	*ha = qp->dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	while (num_entries && qp->rq.wqe_cons != hw_cons) {
		/* fill WC */
		wc->status = IB_WC_WR_FLUSH_ERR;
		wc->vendor_err = 0;
		wc->wc_flags = 0;
		wc->src_qp = qp->id;
		wc->byte_len = 0;
		wc->wr_id = qp->rqe_wr_id[qp->rq.cons].wr_id;
		wc->qp = &qp->ibqp;
		num_entries--;
		wc++;
		cnt++;
		while (qp->rqe_wr_id[qp->rq.cons].wqe_size--)
			ecore_chain_consume(&qp->rq.pbl);
		qlnxr_inc_sw_cons(&qp->rq);
	}

	QL_DPRINT12(ha, "exit cnt = 0x%x\n", cnt);
	return cnt;
}

static void
try_consume_resp_cqe(struct qlnxr_cq *cq,
	struct qlnxr_qp *qp,
	struct rdma_cqe_responder *resp,
	int *update)
{
	if (le16_to_cpu(resp->rq_cons) == qp->rq.wqe_cons) {
		consume_cqe(cq);
		*update |= 1;
	}
}

static int
qlnxr_poll_cq_resp_srq(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_cq *cq,
	int num_entries,
	struct ib_wc *wc,
	struct rdma_cqe_responder *resp,
	int *update)
{
	int		cnt;
	qlnx_host_t	*ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	cnt = process_resp_one_srq(dev, qp, cq, wc, resp);
	consume_cqe(cq);
	*update |= 1;

	QL_DPRINT12(ha, "exit cnt = 0x%x\n", cnt);
	return cnt;
}

static int
qlnxr_poll_cq_resp(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp,
	struct qlnxr_cq *cq,
	int num_entries,
	struct ib_wc *wc,
	struct rdma_cqe_responder *resp,
	int *update)
{
	int		cnt;
	qlnx_host_t	*ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	if (resp->status == RDMA_CQE_RESP_STS_WORK_REQUEST_FLUSHED_ERR) {
		cnt = process_resp_flush(qp, num_entries, wc,
				resp->rq_cons);
		try_consume_resp_cqe(cq, qp, resp, update);
	} else {
		cnt = process_resp_one(dev, qp, cq, wc, resp);
		consume_cqe(cq);
		*update |= 1;
	}

	QL_DPRINT12(ha, "exit cnt = 0x%x\n", cnt);
	return cnt;
}

static void
try_consume_req_cqe(struct qlnxr_cq *cq, struct qlnxr_qp *qp,
	struct rdma_cqe_requester *req, int *update)
{
	if (le16_to_cpu(req->sq_cons) == qp->sq.wqe_cons) {
		consume_cqe(cq);
		*update |= 1;
	}
}

static void
doorbell_cq(struct qlnxr_dev *dev, struct qlnxr_cq *cq, u32 cons, u8 flags)
{
	uint64_t	reg_addr;
	qlnx_host_t	*ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	wmb();
	cq->db.data.agg_flags = flags;
	cq->db.data.value = cpu_to_le32(cons);

	reg_addr = (uint64_t)((uint8_t *)cq->db_addr -
				(uint8_t *)(ha->cdev.doorbells));

	bus_write_8(ha->pci_dbells, reg_addr, cq->db.raw);
	bus_barrier(ha->pci_dbells,  0, 0, BUS_SPACE_BARRIER_READ);

	QL_DPRINT12(ha, "exit\n");
	return;

//#ifdef __LP64__
//	writeq(cq->db.raw, cq->db_addr);
//#else
	/* Note that since the FW allows 64 bit write only, in 32bit systems
	 * the value of db_addr must be low enough. This is currently not
	 * enforced.
	 */
//	writel(cq->db.raw & 0xffffffff, cq->db_addr);
//	mmiowb();
//#endif
}


static int
is_valid_cqe(struct qlnxr_cq *cq, union rdma_cqe *cqe)
{
	struct rdma_cqe_requester *resp_cqe = &cqe->req;
	return (resp_cqe->flags & RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT_MASK) ==
			cq->pbl_toggle;
}

int
qlnxr_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct qlnxr_cq	*cq = get_qlnxr_cq(ibcq);
	struct qlnxr_dev *dev = get_qlnxr_dev((ibcq->device));
	int		done = 0;
	union rdma_cqe	*cqe = cq->latest_cqe;
	int 		update = 0;
	u32		old_cons, new_cons;
	unsigned long	flags;
	qlnx_host_t	*ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;
 
	if (cq->destroyed) {
		QL_DPRINT11(ha, "called after destroy for cq %p (icid=%d)\n",
			cq, cq->icid);
		return 0;
	}

	if (cq->cq_type == QLNXR_CQ_TYPE_GSI)
		return qlnxr_gsi_poll_cq(ibcq, num_entries, wc);

	spin_lock_irqsave(&cq->cq_lock, flags);

	old_cons = ecore_chain_get_cons_idx_u32(&cq->pbl);

	while (num_entries && is_valid_cqe(cq, cqe)) {
		int cnt = 0;
		struct qlnxr_qp *qp;
		struct rdma_cqe_requester *resp_cqe;
		enum rdma_cqe_type cqe_type;

		/* prevent speculative reads of any field of CQE */
		rmb();

		resp_cqe = &cqe->req;
		qp = (struct qlnxr_qp *)(uintptr_t)HILO_U64(resp_cqe->qp_handle.hi,
						resp_cqe->qp_handle.lo);

		if (!qp) {
			QL_DPRINT11(ha, "qp = NULL\n");
			break;
		}

		wc->qp = &qp->ibqp;

		cqe_type = GET_FIELD(resp_cqe->flags, RDMA_CQE_REQUESTER_TYPE);

		switch (cqe_type) {
		case RDMA_CQE_TYPE_REQUESTER:
			cnt = qlnxr_poll_cq_req(dev, qp, cq, num_entries,
					wc, &cqe->req);
			try_consume_req_cqe(cq, qp, &cqe->req, &update);
			break;
		case RDMA_CQE_TYPE_RESPONDER_RQ:
			cnt = qlnxr_poll_cq_resp(dev, qp, cq, num_entries,
					wc, &cqe->resp, &update);
			break;
		case RDMA_CQE_TYPE_RESPONDER_SRQ:
			cnt = qlnxr_poll_cq_resp_srq(dev, qp, cq, num_entries,
					wc, &cqe->resp, &update);
			break;
		case RDMA_CQE_TYPE_INVALID:
		default:
			QL_DPRINT11(ha, "cqe type [0x%x] invalid\n", cqe_type);
			break;
		}
		num_entries -= cnt;
		wc += cnt;
		done += cnt;

		cqe = cq->latest_cqe;
	}
	new_cons = ecore_chain_get_cons_idx_u32(&cq->pbl);

	cq->cq_cons += new_cons - old_cons;

	if (update) {
		/* doorbell notifies abount latest VALID entry,
		 * but chain already point to the next INVALID one
		 */
		doorbell_cq(dev, cq, cq->cq_cons - 1, cq->arm_flags);
		QL_DPRINT12(ha, "cq = %p cons = 0x%x "
			"arm_flags = 0x%x db.icid = 0x%x\n", cq,
			(cq->cq_cons - 1), cq->arm_flags, cq->db.data.icid);
	}

	spin_unlock_irqrestore(&cq->cq_lock, flags);

	QL_DPRINT12(ha, "exit\n");
 
	return done;
}


int
qlnxr_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
        struct qlnxr_cq *cq = get_qlnxr_cq(ibcq);
        unsigned long sflags;
        struct qlnxr_dev *dev;
	qlnx_host_t	*ha;

	dev = get_qlnxr_dev((ibcq->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "enter ibcq = %p flags = 0x%x "
		"cp = %p cons = 0x%x cq_type = 0x%x\n", ibcq,
		flags, cq, cq->cq_cons, cq->cq_type);

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;

	if (cq->destroyed) {
		QL_DPRINT11(ha, "cq was already destroyed cq = %p icid=%d\n",
			cq, cq->icid);
		return -EINVAL;
	}

        if (cq->cq_type == QLNXR_CQ_TYPE_GSI) {
                return 0;
        }

        spin_lock_irqsave(&cq->cq_lock, sflags);

        cq->arm_flags = 0;

        if (flags & IB_CQ_SOLICITED) {
                cq->arm_flags |= DQ_UCM_ROCE_CQ_ARM_SE_CF_CMD;
        }
        if (flags & IB_CQ_NEXT_COMP) {
                cq->arm_flags |= DQ_UCM_ROCE_CQ_ARM_CF_CMD;
        }

        doorbell_cq(dev, cq, (cq->cq_cons - 1), cq->arm_flags);

        spin_unlock_irqrestore(&cq->cq_lock, sflags);

	QL_DPRINT12(ha, "exit ibcq = %p flags = 0x%x\n", ibcq, flags);
        return 0;
}


static struct qlnxr_mr *
__qlnxr_alloc_mr(struct ib_pd *ibpd, int max_page_list_len)
{
	struct qlnxr_pd *pd = get_qlnxr_pd(ibpd);
	struct qlnxr_dev *dev = get_qlnxr_dev((ibpd->device));
	struct qlnxr_mr *mr;
	int		rc = -ENOMEM;
	qlnx_host_t	*ha;

	ha = dev->ha;
 
	QL_DPRINT12(ha, "enter ibpd = %p pd = %p "
		" pd_id = %d max_page_list_len = %d\n",
		ibpd, pd, pd->pd_id, max_page_list_len);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		QL_DPRINT11(ha, "kzalloc(mr) failed\n");
		return ERR_PTR(rc);
	}

	mr->dev = dev;
	mr->type = QLNXR_MR_FRMR;

	rc = qlnxr_init_mr_info(dev, &mr->info, max_page_list_len,
				  1 /* allow dual layer pbl */);
	if (rc) {
		QL_DPRINT11(ha, "qlnxr_init_mr_info failed\n");
		goto err0;
	}

	rc = ecore_rdma_alloc_tid(dev->rdma_ctx, &mr->hw_mr.itid);
	if (rc) {
		QL_DPRINT11(ha, "ecore_rdma_alloc_tid failed\n");
		goto err0;
	}

	/* index only, 18 bit long, lkey = itid << 8 | key */
	mr->hw_mr.tid_type = ECORE_RDMA_TID_FMR;
	mr->hw_mr.key = 0;
	mr->hw_mr.pd = pd->pd_id;
	mr->hw_mr.local_read = 1;
	mr->hw_mr.local_write = 0;
	mr->hw_mr.remote_read = 0;
	mr->hw_mr.remote_write = 0;
	mr->hw_mr.remote_atomic = 0;
	mr->hw_mr.mw_bind = false; /* TBD MW BIND */
	mr->hw_mr.pbl_ptr = 0; /* Will be supplied during post */
	mr->hw_mr.pbl_two_level = mr->info.pbl_info.two_layered;
	mr->hw_mr.pbl_page_size_log = ilog2(mr->info.pbl_info.pbl_size);
	mr->hw_mr.fbo = 0;
	mr->hw_mr.length = 0;
	mr->hw_mr.vaddr = 0;
	mr->hw_mr.zbva = false; /* TBD figure when this should be true */
	mr->hw_mr.phy_mr = true; /* Fast MR - True, Regular Register False */
	mr->hw_mr.dma_mr = false;

	rc = ecore_rdma_register_tid(dev->rdma_ctx, &mr->hw_mr);
	if (rc) {
		QL_DPRINT11(ha, "ecore_rdma_register_tid failed\n");
		goto err1;
	}

	mr->ibmr.lkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;
	mr->ibmr.rkey = mr->ibmr.lkey;

	QL_DPRINT12(ha, "exit mr = %p mr->ibmr.lkey = 0x%x\n",
		mr, mr->ibmr.lkey);

	return mr;

err1:
	ecore_rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);
err0:
	kfree(mr);

	QL_DPRINT12(ha, "exit\n");

	return ERR_PTR(rc);
}

#if __FreeBSD_version >= 1102000

struct ib_mr *
qlnxr_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type, u32 max_num_sg)
{
	struct qlnxr_dev *dev;
	struct qlnxr_mr *mr;
	qlnx_host_t     *ha;

	dev = get_qlnxr_dev(ibpd->device);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	mr = __qlnxr_alloc_mr(ibpd, max_num_sg);

	if (IS_ERR(mr))
		return ERR_PTR(-EINVAL);

	QL_DPRINT12(ha, "exit mr = %p &mr->ibmr = %p\n", mr, &mr->ibmr);

	return &mr->ibmr;
}

static int
qlnxr_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct qlnxr_mr *mr = get_qlnxr_mr(ibmr);
	struct qlnxr_pbl *pbl_table;
	struct regpair *pbe;
	struct qlnxr_dev *dev;
	qlnx_host_t     *ha;
	u32 pbes_in_page;

	dev = mr->dev;
	ha = dev->ha;

	if (unlikely(mr->npages == mr->info.pbl_info.num_pbes)) {
		QL_DPRINT12(ha, "fails mr->npages %d\n", mr->npages);
		return -ENOMEM;
	}

	QL_DPRINT12(ha, "mr->npages %d addr = %p enter\n", mr->npages,
		((void *)addr));

	pbes_in_page = mr->info.pbl_info.pbl_size / sizeof(u64);
	pbl_table = mr->info.pbl_table + (mr->npages / pbes_in_page);
	pbe = (struct regpair *)pbl_table->va;
	pbe +=  mr->npages % pbes_in_page;
	pbe->lo = cpu_to_le32((u32)addr);
	pbe->hi = cpu_to_le32((u32)upper_32_bits(addr));

	mr->npages++;

	QL_DPRINT12(ha, "mr->npages %d addr = %p exit \n", mr->npages,
		((void *)addr));
	return 0;
}

int
qlnxr_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
	int sg_nents, unsigned int *sg_offset)
{
	int             ret;
	struct qlnxr_mr *mr = get_qlnxr_mr(ibmr);
	qlnx_host_t     *ha;

	if (mr == NULL)
		return (-1);

	if (mr->dev == NULL)
		return (-1);

	ha = mr->dev->ha;

	QL_DPRINT12(ha, "enter\n");

	mr->npages = 0;
	qlnx_handle_completed_mrs(mr->dev, &mr->info);

	ret = ib_sg_to_pages(ibmr, sg, sg_nents, NULL, qlnxr_set_page);

	QL_DPRINT12(ha, "exit ret = %d\n", ret);

	return (ret);
}

#else

struct ib_mr *
qlnxr_alloc_frmr(struct ib_pd *ibpd, int max_page_list_len)
{
	struct qlnxr_dev *dev;
	struct qlnxr_mr *mr;
	qlnx_host_t	*ha;
	struct ib_mr *ibmr = NULL;

	dev = get_qlnxr_dev((ibpd->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	mr = __qlnxr_alloc_mr(ibpd, max_page_list_len);

	if (IS_ERR(mr)) {
		ibmr = ERR_PTR(-EINVAL);
	} else {
		ibmr = &mr->ibmr;
	}

	QL_DPRINT12(ha, "exit %p\n", ibmr);
	return (ibmr);
}

void
qlnxr_free_frmr_page_list(struct ib_fast_reg_page_list *page_list)
{
	struct qlnxr_fast_reg_page_list *frmr_list;

	frmr_list = get_qlnxr_frmr_list(page_list);
 
	free_mr_info(frmr_list->dev, &frmr_list->info);

	kfree(frmr_list->ibfrpl.page_list);
	kfree(frmr_list);

	return;
}

struct ib_fast_reg_page_list *
qlnxr_alloc_frmr_page_list(struct ib_device *ibdev, int page_list_len)
{
	struct qlnxr_fast_reg_page_list *frmr_list = NULL;
	struct qlnxr_dev		*dev;
	int				size = page_list_len * sizeof(u64);
	int				rc = -ENOMEM;
	qlnx_host_t			*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	frmr_list = kzalloc(sizeof(*frmr_list), GFP_KERNEL);
	if (!frmr_list) {
		QL_DPRINT11(ha, "kzalloc(frmr_list) failed\n");
		goto err;
	}

	frmr_list->dev = dev;
	frmr_list->ibfrpl.page_list = kzalloc(size, GFP_KERNEL);
	if (!frmr_list->ibfrpl.page_list) {
		QL_DPRINT11(ha, "frmr_list->ibfrpl.page_list = NULL failed\n");
		goto err0;
	}

	rc = qlnxr_init_mr_info(dev, &frmr_list->info, page_list_len,
			  1 /* allow dual layer pbl */);
	if (rc)
		goto err1;

	QL_DPRINT12(ha, "exit %p\n", &frmr_list->ibfrpl);

	return &frmr_list->ibfrpl;

err1:
	kfree(frmr_list->ibfrpl.page_list);
err0:
	kfree(frmr_list);
err:
	QL_DPRINT12(ha, "exit with error\n");

	return ERR_PTR(rc);
}

static int
qlnxr_validate_phys_buf_list(qlnx_host_t *ha, struct ib_phys_buf *buf_list,
	int buf_cnt, uint64_t *total_size)
{
	u64 size = 0;

	*total_size = 0;

	if (!buf_cnt || buf_list == NULL) {
		QL_DPRINT11(ha,
			"failed buf_list = %p buf_cnt = %d\n", buf_list, buf_cnt);
		return (-1);
	}

	size = buf_list->size;

	if (!size) {
		QL_DPRINT11(ha,
			"failed buf_list = %p buf_cnt = %d"
			" buf_list->size = 0\n", buf_list, buf_cnt);
		return (-1);
	}

	while (buf_cnt) {

		*total_size += buf_list->size;

		if (buf_list->size != size) {
			QL_DPRINT11(ha,
				"failed buf_list = %p buf_cnt = %d"
				" all buffers should have same size\n",
				buf_list, buf_cnt);
			return (-1);
		}

		buf_list++;
		buf_cnt--;
	}
	return (0);
}

static size_t
qlnxr_get_num_pages(qlnx_host_t *ha, struct ib_phys_buf *buf_list,
	int buf_cnt)
{
	int	i;
	size_t	num_pages = 0;
	u64	size;

	for (i = 0; i < buf_cnt; i++) {

		size = 0;
		while (size < buf_list->size) {
			size += PAGE_SIZE;
			num_pages++;
		}
		buf_list++;
	}
	return (num_pages);
}

static void
qlnxr_populate_phys_mem_pbls(struct qlnxr_dev *dev,
	struct ib_phys_buf *buf_list, int buf_cnt,
	struct qlnxr_pbl *pbl, struct qlnxr_pbl_info *pbl_info)
{
	struct regpair		*pbe;
	struct qlnxr_pbl	*pbl_tbl;
	int			pg_cnt, pages, pbe_cnt, total_num_pbes = 0;
	qlnx_host_t		*ha;
        int                     i;
	u64			pbe_addr;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (!pbl_info) {
		QL_DPRINT11(ha, "PBL_INFO not initialized\n");
		return;
	}

	if (!pbl_info->num_pbes) {
		QL_DPRINT11(ha, "pbl_info->num_pbes == 0\n");
		return;
	}

	/* If we have a two layered pbl, the first pbl points to the rest
	 * of the pbls and the first entry lays on the second pbl in the table
	 */
	if (pbl_info->two_layered)
		pbl_tbl = &pbl[1];
	else
		pbl_tbl = pbl;

	pbe = (struct regpair *)pbl_tbl->va;
	if (!pbe) {
		QL_DPRINT12(ha, "pbe is NULL\n");
		return;
	}

	pbe_cnt = 0;

	for (i = 0; i < buf_cnt; i++) {

		pages = buf_list->size >> PAGE_SHIFT;

		for (pg_cnt = 0; pg_cnt < pages; pg_cnt++) {
			/* store the page address in pbe */

			pbe_addr = buf_list->addr + (PAGE_SIZE * pg_cnt);

			pbe->lo = cpu_to_le32((u32)pbe_addr);
			pbe->hi = cpu_to_le32(((u32)(pbe_addr >> 32)));

			QL_DPRINT12(ha, "Populate pbl table:"
				" pbe->addr=0x%x:0x%x "
				" pbe_cnt = %d total_num_pbes=%d"
				" pbe=%p\n", pbe->lo, pbe->hi, pbe_cnt,
				total_num_pbes, pbe);

			pbe_cnt ++;
			total_num_pbes ++;
			pbe++;

			if (total_num_pbes == pbl_info->num_pbes)
				return;

			/* if the given pbl is full storing the pbes,
			 * move to next pbl.  */

			if (pbe_cnt == (pbl_info->pbl_size / sizeof(u64))) {
				pbl_tbl++;
				pbe = (struct regpair *)pbl_tbl->va;
				pbe_cnt = 0;
			}
		}
		buf_list++;
	}
	QL_DPRINT12(ha, "exit\n");
	return;
}

struct ib_mr *
qlnxr_reg_kernel_mr(struct ib_pd *ibpd,
	struct ib_phys_buf *buf_list,
	int buf_cnt, int acc, u64 *iova_start)
{
	int		rc = -ENOMEM;
	struct qlnxr_dev *dev = get_qlnxr_dev((ibpd->device));
	struct qlnxr_mr *mr;
	struct qlnxr_pd *pd;
	qlnx_host_t	*ha;
	size_t		num_pages = 0;
	uint64_t	length;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	pd = get_qlnxr_pd(ibpd);

	QL_DPRINT12(ha, "pd = %d buf_list = %p, buf_cnt = %d,"
		" iova_start = %p, acc = %d\n",
		pd->pd_id, buf_list, buf_cnt, iova_start, acc);

	//if (acc & IB_ACCESS_REMOTE_WRITE && !(acc & IB_ACCESS_LOCAL_WRITE)) {
	//	QL_DPRINT11(ha, "(acc & IB_ACCESS_REMOTE_WRITE &&"
	//		" !(acc & IB_ACCESS_LOCAL_WRITE))\n");
	//	return ERR_PTR(-EINVAL);
	//}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		QL_DPRINT11(ha, "kzalloc(mr) failed\n");
		return ERR_PTR(rc);
	}

	mr->type = QLNXR_MR_KERNEL;
	mr->iova_start = iova_start;

	rc = qlnxr_validate_phys_buf_list(ha, buf_list, buf_cnt, &length);
	if (rc)
		goto err0;

	num_pages = qlnxr_get_num_pages(ha, buf_list, buf_cnt);
	if (!num_pages)
		goto err0;

	rc = qlnxr_init_mr_info(dev, &mr->info, num_pages, 1);
	if (rc) {
		QL_DPRINT11(ha,
			"qlnxr_init_mr_info failed [%d]\n", rc);
		goto err1;
	}

	qlnxr_populate_phys_mem_pbls(dev, buf_list, buf_cnt, mr->info.pbl_table,
		   &mr->info.pbl_info);

	rc = ecore_rdma_alloc_tid(dev->rdma_ctx, &mr->hw_mr.itid);

	if (rc) {
		QL_DPRINT11(ha, "roce alloc tid returned an error %d\n", rc);
		goto err1;
	}

	/* index only, 18 bit long, lkey = itid << 8 | key */
	mr->hw_mr.tid_type = ECORE_RDMA_TID_REGISTERED_MR;
	mr->hw_mr.key = 0;
	mr->hw_mr.pd = pd->pd_id;
	mr->hw_mr.local_read = 1;
	mr->hw_mr.local_write = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hw_mr.remote_read = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hw_mr.remote_write = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hw_mr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	mr->hw_mr.mw_bind = false; /* TBD MW BIND */
	mr->hw_mr.pbl_ptr = mr->info.pbl_table[0].pa;
	mr->hw_mr.pbl_two_level = mr->info.pbl_info.two_layered;
	mr->hw_mr.pbl_page_size_log = ilog2(mr->info.pbl_info.pbl_size);
	mr->hw_mr.page_size_log = ilog2(PAGE_SIZE); /* for the MR pages */

	mr->hw_mr.fbo = 0;

	mr->hw_mr.length = length;
	mr->hw_mr.vaddr = (uint64_t)iova_start;
	mr->hw_mr.zbva = false; /* TBD figure when this should be true */
	mr->hw_mr.phy_mr = false; /* Fast MR - True, Regular Register False */
	mr->hw_mr.dma_mr = false;

	rc = ecore_rdma_register_tid(dev->rdma_ctx, &mr->hw_mr);
	if (rc) {
		QL_DPRINT11(ha, "roce register tid returned an error %d\n", rc);
		goto err2;
	}

	mr->ibmr.lkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;
	if (mr->hw_mr.remote_write || mr->hw_mr.remote_read ||
		mr->hw_mr.remote_atomic)
		mr->ibmr.rkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;

	QL_DPRINT12(ha, "lkey: %x\n", mr->ibmr.lkey);

	return (&mr->ibmr);

err2:
	ecore_rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);
err1:
	qlnxr_free_pbl(dev, &mr->info.pbl_info, mr->info.pbl_table);
err0:
	kfree(mr);

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return (ERR_PTR(rc));
}

#endif /* #if __FreeBSD_version >= 1102000 */

struct ib_ah *
#if __FreeBSD_version >= 1102000
qlnxr_create_ah(struct ib_pd *ibpd, struct ib_ah_attr *attr,
	struct ib_udata *udata)
#else
qlnxr_create_ah(struct ib_pd *ibpd, struct ib_ah_attr *attr)
#endif /* #if __FreeBSD_version >= 1102000 */
{
	struct qlnxr_dev *dev;
	qlnx_host_t	*ha;
	struct qlnxr_ah *ah;

	dev = get_qlnxr_dev((ibpd->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "in create_ah\n");

	ah = kzalloc(sizeof(*ah), GFP_ATOMIC);
	if (!ah) {
		QL_DPRINT12(ha, "no address handle can be allocated\n");
		return ERR_PTR(-ENOMEM);
	}
	
	ah->attr = *attr;	
 
	return &ah->ibah;
}

int
qlnxr_destroy_ah(struct ib_ah *ibah)
{
	struct qlnxr_dev *dev;
	qlnx_host_t     *ha;
	struct qlnxr_ah *ah = get_qlnxr_ah(ibah);
	
	dev = get_qlnxr_dev((ibah->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "in destroy_ah\n");

	kfree(ah);
	return 0;
}

int
qlnxr_query_ah(struct ib_ah *ibah, struct ib_ah_attr *attr)
{
	struct qlnxr_dev *dev;
	qlnx_host_t     *ha;

	dev = get_qlnxr_dev((ibah->device));
	ha = dev->ha;
	QL_DPRINT12(ha, "Query AH not supported\n");
	return -EINVAL;
}

int
qlnxr_modify_ah(struct ib_ah *ibah, struct ib_ah_attr *attr)
{
	struct qlnxr_dev *dev;
	qlnx_host_t     *ha;

	dev = get_qlnxr_dev((ibah->device));
	ha = dev->ha;
	QL_DPRINT12(ha, "Modify AH not supported\n");
	return -ENOSYS;
}

#if __FreeBSD_version >= 1102000
int
qlnxr_process_mad(struct ib_device *ibdev,
		int process_mad_flags,
		u8 port_num,
		const struct ib_wc *in_wc,
		const struct ib_grh *in_grh,
		const struct ib_mad_hdr *mad_hdr,
		size_t in_mad_size,
		struct ib_mad_hdr *out_mad,
		size_t *out_mad_size,
		u16 *out_mad_pkey_index)

#else

int
qlnxr_process_mad(struct ib_device *ibdev,
                        int process_mad_flags,
                        u8 port_num,
                        struct ib_wc *in_wc,
                        struct ib_grh *in_grh,
                        struct ib_mad *in_mad,
                        struct ib_mad *out_mad)

#endif /* #if __FreeBSD_version >= 1102000 */
{
	struct qlnxr_dev *dev;
	qlnx_host_t	*ha;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;
	QL_DPRINT12(ha, "process mad not supported\n");

	return -ENOSYS;
//	QL_DPRINT12(ha, "qlnxr_process_mad in_mad %x %x %x %x %x %x %x %x\n",
//               in_mad->mad_hdr.attr_id, in_mad->mad_hdr.base_version,
//               in_mad->mad_hdr.attr_mod, in_mad->mad_hdr.class_specific,
//               in_mad->mad_hdr.class_version, in_mad->mad_hdr.method,
//               in_mad->mad_hdr.mgmt_class, in_mad->mad_hdr.status);

//	return IB_MAD_RESULT_SUCCESS;	
}


#if __FreeBSD_version >= 1102000
int
qlnxr_get_port_immutable(struct ib_device *ibdev, u8 port_num,
	struct ib_port_immutable *immutable)
{
	struct qlnxr_dev        *dev;
	qlnx_host_t             *ha;
	struct ib_port_attr     attr;
	int                     err;

	dev = get_qlnxr_dev(ibdev);
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	err = qlnxr_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	if (QLNX_IS_IWARP(dev)) {
		immutable->pkey_tbl_len = 1;
		immutable->gid_tbl_len = 1;
		immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;
		immutable->max_mad_size = 0;
	} else {
		immutable->pkey_tbl_len = attr.pkey_tbl_len;
		immutable->gid_tbl_len = attr.gid_tbl_len;
		immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE;
		immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	}

	QL_DPRINT12(ha, "exit\n");
	return 0;
}
#endif /* #if __FreeBSD_version > 1102000 */


/***** iWARP related functions *************/


static void
qlnxr_iw_mpa_request(void *context,
	struct ecore_iwarp_cm_event_params *params)
{
	struct qlnxr_iw_listener *listener = (struct qlnxr_iw_listener *)context;
	struct qlnxr_dev *dev = listener->dev;
	struct qlnxr_iw_ep *ep;
	struct iw_cm_event event;
	struct sockaddr_in *laddr;
	struct sockaddr_in *raddr;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (params->cm_info->ip_version != ECORE_TCP_IPV4) {
		QL_DPRINT11(ha, "only IPv4 supported [0x%x]\n",
			params->cm_info->ip_version);
		return;
	}
 
	ep = kzalloc(sizeof(*ep), GFP_ATOMIC);

	if (!ep) {
		QL_DPRINT11(ha, "kzalloc{ep) failed\n");
		return;
	}

	ep->dev = dev;
	ep->ecore_context = params->ep_context;

	memset(&event, 0, sizeof(event));

	event.event = IW_CM_EVENT_CONNECT_REQUEST;
	event.status = params->status;

	laddr = (struct sockaddr_in *)&event.local_addr;
	raddr = (struct sockaddr_in *)&event.remote_addr;

	laddr->sin_family = AF_INET;
	raddr->sin_family = AF_INET;

	laddr->sin_port = htons(params->cm_info->local_port);
	raddr->sin_port = htons(params->cm_info->remote_port);

	laddr->sin_addr.s_addr = htonl(params->cm_info->local_ip[0]);
	raddr->sin_addr.s_addr = htonl(params->cm_info->remote_ip[0]);

	event.provider_data = (void *)ep;
	event.private_data = (void *)params->cm_info->private_data;
	event.private_data_len = (u8)params->cm_info->private_data_len;

#if __FreeBSD_version >= 1100000
	event.ord = params->cm_info->ord;
	event.ird = params->cm_info->ird;
#endif /* #if __FreeBSD_version >= 1100000 */

	listener->cm_id->event_handler(listener->cm_id, &event);

	QL_DPRINT12(ha, "exit\n");

	return;
}

static void
qlnxr_iw_issue_event(void *context,
	 struct ecore_iwarp_cm_event_params *params,
	 enum iw_cm_event_type event_type,
	 char *str)
{
	struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)context;
	struct qlnxr_dev *dev = ep->dev;
	struct iw_cm_event event;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	memset(&event, 0, sizeof(event));
	event.status = params->status;
	event.event = event_type;

	if (params->cm_info != NULL) {
#if __FreeBSD_version >= 1100000
		event.ird = params->cm_info->ird;
		event.ord = params->cm_info->ord;
		QL_DPRINT12(ha, "ord=[%d] \n", event.ord);
		QL_DPRINT12(ha, "ird=[%d] \n", event.ird);
#endif /* #if __FreeBSD_version >= 1100000 */

		event.private_data_len = params->cm_info->private_data_len;
		event.private_data = (void *)params->cm_info->private_data;
		QL_DPRINT12(ha, "private_data_len=[%d] \n",
			event.private_data_len);
	}

	QL_DPRINT12(ha, "event=[%d] %s\n", event.event, str);
	QL_DPRINT12(ha, "status=[%d] \n", event.status);
	
	if (ep) {
		if (ep->cm_id)
			ep->cm_id->event_handler(ep->cm_id, &event);
		else
			QL_DPRINT11(ha, "ep->cm_id == NULL \n");
	} else {
		QL_DPRINT11(ha, "ep == NULL \n");
	}

	QL_DPRINT12(ha, "exit\n");

	return;
}

static void
qlnxr_iw_close_event(void *context,
	 struct ecore_iwarp_cm_event_params *params)
{
	struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)context;
	struct qlnxr_dev *dev = ep->dev;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");
 
	if (ep->cm_id) {
		qlnxr_iw_issue_event(context,
				    params,
				    IW_CM_EVENT_CLOSE,
				    "IW_CM_EVENT_EVENT_CLOSE");
		ep->cm_id->rem_ref(ep->cm_id);
		ep->cm_id = NULL;
	}

	QL_DPRINT12(ha, "exit\n");

	return;
}

#if __FreeBSD_version >= 1102000

static void
qlnxr_iw_passive_complete(void *context,
        struct ecore_iwarp_cm_event_params *params)
{
        struct qlnxr_iw_ep      *ep = (struct qlnxr_iw_ep *)context;
        struct qlnxr_dev        *dev = ep->dev;
        qlnx_host_t             *ha;

        ha = dev->ha;

        /* We will only reach the following state if MPA_REJECT was called on
         * passive. In this case there will be no associated QP.
         */
        if ((params->status == -ECONNREFUSED) && (ep->qp == NULL)) {
                QL_DPRINT11(ha, "PASSIVE connection refused releasing ep...\n");
                kfree(ep);
                return;
        }

        /* We always issue an established event, however, ofed does not look
         * at event code for established. So if there was a failure, we follow
         * with close...
         */
        qlnxr_iw_issue_event(context,
                params,
                IW_CM_EVENT_ESTABLISHED,
                "IW_CM_EVENT_ESTABLISHED");

        if (params->status < 0) {
                qlnxr_iw_close_event(context, params);
        }

        return;
}

struct qlnxr_discon_work {
        struct work_struct work;
        struct qlnxr_iw_ep *ep;
        enum ecore_iwarp_event_type event;
        int status;
};

static void
qlnxr_iw_disconnect_worker(struct work_struct *work)
{
        struct qlnxr_discon_work *dwork =
                container_of(work, struct qlnxr_discon_work, work);
        struct ecore_rdma_modify_qp_in_params qp_params = { 0 };
        struct qlnxr_iw_ep *ep = dwork->ep;
        struct qlnxr_dev *dev = ep->dev;
        struct qlnxr_qp *qp = ep->qp;
        struct iw_cm_event event;

        if (qp->destroyed) {
                kfree(dwork);
                qlnxr_iw_qp_rem_ref(&qp->ibqp);
                return;
        }

        memset(&event, 0, sizeof(event));
        event.status = dwork->status;
        event.event = IW_CM_EVENT_DISCONNECT;

        /* Success means graceful disconnect was requested. modifying
         * to SQD is translated to graceful disconnect. O/w reset is sent
         */
        if (dwork->status)
                qp_params.new_state = ECORE_ROCE_QP_STATE_ERR;
        else
                qp_params.new_state = ECORE_ROCE_QP_STATE_SQD;

        kfree(dwork);

        if (ep->cm_id)
                ep->cm_id->event_handler(ep->cm_id, &event);

        SET_FIELD(qp_params.modify_flags,
                  ECORE_RDMA_MODIFY_QP_VALID_NEW_STATE, 1);

        ecore_rdma_modify_qp(dev->rdma_ctx, qp->ecore_qp, &qp_params);

        qlnxr_iw_qp_rem_ref(&qp->ibqp);

        return;
}

void
qlnxr_iw_disconnect_event(void *context,
        struct ecore_iwarp_cm_event_params *params)
{
        struct qlnxr_discon_work *work;
        struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)context;
        struct qlnxr_dev *dev = ep->dev;
        struct qlnxr_qp *qp = ep->qp;

        work = kzalloc(sizeof(*work), GFP_ATOMIC);
        if (!work)
                return;

        qlnxr_iw_qp_add_ref(&qp->ibqp);
        work->ep = ep;
        work->event = params->event;
        work->status = params->status;

        INIT_WORK(&work->work, qlnxr_iw_disconnect_worker);
        queue_work(dev->iwarp_wq, &work->work);

        return;
}

#endif /* #if __FreeBSD_version >= 1102000 */

static int
qlnxr_iw_mpa_reply(void *context,
	struct ecore_iwarp_cm_event_params *params)
{
        struct qlnxr_iw_ep	*ep = (struct qlnxr_iw_ep *)context;
        struct qlnxr_dev	*dev = ep->dev;
        struct ecore_iwarp_send_rtr_in rtr_in;
        int			rc;
	qlnx_host_t		*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;

	bzero(&rtr_in, sizeof(struct ecore_iwarp_send_rtr_in));
        rtr_in.ep_context = params->ep_context;

        rc = ecore_iwarp_send_rtr(dev->rdma_ctx, &rtr_in);

	QL_DPRINT12(ha, "exit rc = %d\n", rc);
        return rc;
}


void
qlnxr_iw_qp_event(void *context,
	struct ecore_iwarp_cm_event_params *params,
	enum ib_event_type ib_event,
	char *str)
{
        struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)context;
        struct qlnxr_dev *dev = ep->dev;
        struct ib_qp *ibqp = &(ep->qp->ibqp);
        struct ib_event event;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha,
		"[context, event, event_handler] = [%p, 0x%x, %s, %p] enter\n",
		context, params->event, str, ibqp->event_handler);

        if (ibqp->event_handler) {
                event.event = ib_event;
                event.device = ibqp->device;
                event.element.qp = ibqp;
                ibqp->event_handler(&event, ibqp->qp_context);
        }

	return;
}

int
qlnxr_iw_event_handler(void *context,
	struct ecore_iwarp_cm_event_params *params)
{
	struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)context;
	struct qlnxr_dev *dev = ep->dev;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "[context, event] = [%p, 0x%x] "
		"enter\n", context, params->event);
 
	switch (params->event) {

	/* Passive side request received */
	case ECORE_IWARP_EVENT_MPA_REQUEST:
		qlnxr_iw_mpa_request(context, params);
		break;

        case ECORE_IWARP_EVENT_ACTIVE_MPA_REPLY:
                qlnxr_iw_mpa_reply(context, params);
                break;

	/* Passive side established ( ack on mpa response ) */
	case ECORE_IWARP_EVENT_PASSIVE_COMPLETE:

#if __FreeBSD_version >= 1102000

		ep->during_connect = 0;
		qlnxr_iw_passive_complete(context, params);

#else
		qlnxr_iw_issue_event(context,
				    params,
				    IW_CM_EVENT_ESTABLISHED,
				    "IW_CM_EVENT_ESTABLISHED");
#endif /* #if __FreeBSD_version >= 1102000 */
		break;

	/* Active side reply received */
	case ECORE_IWARP_EVENT_ACTIVE_COMPLETE:
		ep->during_connect = 0;
		qlnxr_iw_issue_event(context,
				    params,
				    IW_CM_EVENT_CONNECT_REPLY,
				    "IW_CM_EVENT_CONNECT_REPLY");
		if (params->status < 0) {
			struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)context;

			ep->cm_id->rem_ref(ep->cm_id);
			ep->cm_id = NULL;
		}
		break;

	case ECORE_IWARP_EVENT_DISCONNECT:

#if __FreeBSD_version >= 1102000
		qlnxr_iw_disconnect_event(context, params);
#else
		qlnxr_iw_issue_event(context,
				    params,
				    IW_CM_EVENT_DISCONNECT,
				    "IW_CM_EVENT_DISCONNECT");
		qlnxr_iw_close_event(context, params);
#endif /* #if __FreeBSD_version >= 1102000 */
		break;

	case ECORE_IWARP_EVENT_CLOSE:
		ep->during_connect = 0;
		qlnxr_iw_close_event(context, params);
		break;

        case ECORE_IWARP_EVENT_RQ_EMPTY:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_FATAL,
                                 "IWARP_EVENT_RQ_EMPTY");
                break;

        case ECORE_IWARP_EVENT_IRQ_FULL:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_FATAL,
                                 "IWARP_EVENT_IRQ_FULL");
                break;

        case ECORE_IWARP_EVENT_LLP_TIMEOUT:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_FATAL,
                                 "IWARP_EVENT_LLP_TIMEOUT");
                break;

        case ECORE_IWARP_EVENT_REMOTE_PROTECTION_ERROR:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_ACCESS_ERR,
                                 "IWARP_EVENT_REMOTE_PROTECTION_ERROR");
                break;

        case ECORE_IWARP_EVENT_CQ_OVERFLOW:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_FATAL,
                                 "QED_IWARP_EVENT_CQ_OVERFLOW");
                break;

        case ECORE_IWARP_EVENT_QP_CATASTROPHIC:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_FATAL,
                                 "QED_IWARP_EVENT_QP_CATASTROPHIC");
                break;

        case ECORE_IWARP_EVENT_LOCAL_ACCESS_ERROR:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_ACCESS_ERR,
                                 "IWARP_EVENT_LOCAL_ACCESS_ERROR");
                break;

        case ECORE_IWARP_EVENT_REMOTE_OPERATION_ERROR:
                qlnxr_iw_qp_event(context, params, IB_EVENT_QP_FATAL,
                                 "IWARP_EVENT_REMOTE_OPERATION_ERROR");
                break;

        case ECORE_IWARP_EVENT_TERMINATE_RECEIVED:
		QL_DPRINT12(ha, "Got terminate message"
			" ECORE_IWARP_EVENT_TERMINATE_RECEIVED\n");
                break;

	default:
		QL_DPRINT12(ha,
			"Unknown event [0x%x] received \n", params->event);
		break;
	};

	QL_DPRINT12(ha, "[context, event] = [%p, 0x%x] "
		"exit\n", context, params->event);
	return 0;
}

static int
qlnxr_addr4_resolve(struct qlnxr_dev *dev,
			      struct sockaddr_in *src_in,
			      struct sockaddr_in *dst_in,
			      u8 *dst_mac)
{
	int rc;

#if __FreeBSD_version >= 1100000
	rc = arpresolve(dev->ha->ifp, 0, NULL, (struct sockaddr *)dst_in,
			dst_mac, NULL, NULL);
#else
	struct llentry *lle;

	rc = arpresolve(dev->ha->ifp, NULL, NULL, (struct sockaddr *)dst_in,
			dst_mac, &lle);
#endif

	QL_DPRINT12(dev->ha, "rc = %d "
		"sa_len = 0x%x sa_family = 0x%x IP Address = %d.%d.%d.%d "
		"Dest MAC %02x:%02x:%02x:%02x:%02x:%02x\n", rc,
		dst_in->sin_len, dst_in->sin_family,
		NIPQUAD((dst_in->sin_addr.s_addr)),
		dst_mac[0], dst_mac[1], dst_mac[2],
		dst_mac[3], dst_mac[4], dst_mac[5]);

	return rc;
}

int
qlnxr_iw_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct qlnxr_dev *dev;
	struct ecore_iwarp_connect_out out_params;
	struct ecore_iwarp_connect_in in_params;
	struct qlnxr_iw_ep *ep;
	struct qlnxr_qp *qp;
	struct sockaddr_in *laddr;
	struct sockaddr_in *raddr;
	int rc = 0;
	qlnx_host_t	*ha;

	dev = get_qlnxr_dev((cm_id->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "[cm_id, conn_param] = [%p, %p] "
		"enter \n", cm_id, conn_param);

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;

	qp = idr_find(&dev->qpidr, conn_param->qpn);

	laddr = (struct sockaddr_in *)&cm_id->local_addr;
	raddr = (struct sockaddr_in *)&cm_id->remote_addr;

	QL_DPRINT12(ha,
		"local = [%d.%d.%d.%d, %d] remote = [%d.%d.%d.%d, %d]\n",
		NIPQUAD((laddr->sin_addr.s_addr)), laddr->sin_port,
		NIPQUAD((raddr->sin_addr.s_addr)), raddr->sin_port);

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		QL_DPRINT11(ha, "struct qlnxr_iw_ep "
			"alloc memory failed\n");
		return -ENOMEM;
	}

	ep->dev = dev;
	ep->qp = qp;
	cm_id->add_ref(cm_id);
	ep->cm_id = cm_id;

	memset(&in_params, 0, sizeof (struct ecore_iwarp_connect_in));
	memset(&out_params, 0, sizeof (struct ecore_iwarp_connect_out));

	in_params.event_cb = qlnxr_iw_event_handler;
	in_params.cb_context = ep;

	in_params.cm_info.ip_version = ECORE_TCP_IPV4;

	in_params.cm_info.remote_ip[0] = ntohl(raddr->sin_addr.s_addr);
	in_params.cm_info.local_ip[0] = ntohl(laddr->sin_addr.s_addr);
	in_params.cm_info.remote_port = ntohs(raddr->sin_port);
	in_params.cm_info.local_port = ntohs(laddr->sin_port);
	in_params.cm_info.vlan = 0;
	in_params.mss = dev->ha->ifp->if_mtu - 40;

	QL_DPRINT12(ha, "remote_ip = [%d.%d.%d.%d] "
		"local_ip = [%d.%d.%d.%d] remote_port = %d local_port = %d "
		"vlan = %d\n",
		NIPQUAD((in_params.cm_info.remote_ip[0])),
		NIPQUAD((in_params.cm_info.local_ip[0])),
		in_params.cm_info.remote_port, in_params.cm_info.local_port,
		in_params.cm_info.vlan);

	rc = qlnxr_addr4_resolve(dev, laddr, raddr, (u8 *)in_params.remote_mac_addr);

	if (rc) {
		QL_DPRINT11(ha, "qlnxr_addr4_resolve failed\n");
		goto err;
	}

	QL_DPRINT12(ha, "ord = %d ird=%d private_data=%p"
		" private_data_len=%d rq_psn=%d\n",
		conn_param->ord, conn_param->ird, conn_param->private_data,
		conn_param->private_data_len, qp->rq_psn);

	in_params.cm_info.ord = conn_param->ord;
	in_params.cm_info.ird = conn_param->ird;
	in_params.cm_info.private_data = conn_param->private_data;
	in_params.cm_info.private_data_len = conn_param->private_data_len;
	in_params.qp = qp->ecore_qp;

	memcpy(in_params.local_mac_addr, dev->ha->primary_mac, ETH_ALEN);

	rc = ecore_iwarp_connect(dev->rdma_ctx, &in_params, &out_params);

	if (rc) {
		QL_DPRINT12(ha, "ecore_iwarp_connect failed\n");
		goto err;
	}

	QL_DPRINT12(ha, "exit\n");

	return rc;

err:
	cm_id->rem_ref(cm_id);
	kfree(ep);

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

int
qlnxr_iw_create_listen(struct iw_cm_id *cm_id, int backlog)
{
	struct qlnxr_dev *dev;
	struct qlnxr_iw_listener *listener;
	struct ecore_iwarp_listen_in iparams;
	struct ecore_iwarp_listen_out oparams;
	struct sockaddr_in *laddr;
	qlnx_host_t	*ha;
	int rc;

	dev = get_qlnxr_dev((cm_id->device));
	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;

	laddr = (struct sockaddr_in *)&cm_id->local_addr;

	listener = kzalloc(sizeof(*listener), GFP_KERNEL);

	if (listener == NULL) {
		QL_DPRINT11(ha, "listener memory alloc failed\n");
		return -ENOMEM;
	}

	listener->dev = dev;
	cm_id->add_ref(cm_id);
	listener->cm_id = cm_id;
	listener->backlog = backlog;

	memset(&iparams, 0, sizeof (struct ecore_iwarp_listen_in));
	memset(&oparams, 0, sizeof (struct ecore_iwarp_listen_out));

	iparams.cb_context = listener;
	iparams.event_cb = qlnxr_iw_event_handler;
	iparams.max_backlog = backlog;

	iparams.ip_version = ECORE_TCP_IPV4;

	iparams.ip_addr[0] = ntohl(laddr->sin_addr.s_addr);
	iparams.port = ntohs(laddr->sin_port);
	iparams.vlan = 0;

	QL_DPRINT12(ha, "[%d.%d.%d.%d, %d] iparamsport=%d\n",
		NIPQUAD((laddr->sin_addr.s_addr)),
		laddr->sin_port, iparams.port);

	rc = ecore_iwarp_create_listen(dev->rdma_ctx, &iparams, &oparams);
	if (rc) {
		QL_DPRINT11(ha,
			"ecore_iwarp_create_listen failed rc = %d\n", rc);
		goto err;
	}

	listener->ecore_handle = oparams.handle;
	cm_id->provider_data = listener;

	QL_DPRINT12(ha, "exit\n");
	return rc;

err:
	cm_id->rem_ref(cm_id);
	kfree(listener);

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return rc;
}

void
qlnxr_iw_destroy_listen(struct iw_cm_id *cm_id)
{
	struct qlnxr_iw_listener *listener = cm_id->provider_data;
	struct qlnxr_dev *dev = get_qlnxr_dev((cm_id->device));
	int rc = 0;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter\n");

	if (listener->ecore_handle)
		rc = ecore_iwarp_destroy_listen(dev->rdma_ctx,
				listener->ecore_handle);

	cm_id->rem_ref(cm_id);

	QL_DPRINT12(ha, "exit [%d]\n", rc);
	return;
}

int
qlnxr_iw_accept(struct iw_cm_id *cm_id,
	struct iw_cm_conn_param *conn_param)
{
	struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)cm_id->provider_data;
	struct qlnxr_dev *dev = ep->dev;
	struct qlnxr_qp *qp;
	struct ecore_iwarp_accept_in params;
	int rc;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter  qpid=%d\n", conn_param->qpn);

	if (!(ha->ifp->if_drv_flags & IFF_DRV_RUNNING))
		return -EINVAL;
 
	qp = idr_find(&dev->qpidr, conn_param->qpn);
	if (!qp) {
		QL_DPRINT11(ha, "idr_find failed invalid qpn = %d\n",
			conn_param->qpn);
		return -EINVAL;
	}
	ep->qp = qp;
	qp->ep = ep;
	cm_id->add_ref(cm_id);
	ep->cm_id = cm_id;

	params.ep_context = ep->ecore_context;
	params.cb_context = ep;
	params.qp = ep->qp->ecore_qp;
	params.private_data = conn_param->private_data;
	params.private_data_len = conn_param->private_data_len;
	params.ird = conn_param->ird;
	params.ord = conn_param->ord;

	rc = ecore_iwarp_accept(dev->rdma_ctx, &params);
	if (rc) {
		QL_DPRINT11(ha, "ecore_iwarp_accept failed %d\n", rc);
		goto err;
	}

	QL_DPRINT12(ha, "exit\n");
	return 0;
err:
	cm_id->rem_ref(cm_id);
	QL_DPRINT12(ha, "exit rc = %d\n", rc);
	return rc;
}

int
qlnxr_iw_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
#if __FreeBSD_version >= 1102000

        struct qlnxr_iw_ep *ep = (struct qlnxr_iw_ep *)cm_id->provider_data;
        struct qlnxr_dev *dev = ep->dev;
        struct ecore_iwarp_reject_in params;
        int rc;

        params.ep_context = ep->ecore_context;
        params.cb_context = ep;
        params.private_data = pdata;
        params.private_data_len = pdata_len;
        ep->qp = NULL;

        rc = ecore_iwarp_reject(dev->rdma_ctx, &params);

        return rc;

#else

	printf("iWARP reject_cr not implemented\n");
	return -EINVAL;

#endif /* #if __FreeBSD_version >= 1102000 */
}

void
qlnxr_iw_qp_add_ref(struct ib_qp *ibqp)
{
	struct qlnxr_qp *qp = get_qlnxr_qp(ibqp);
	qlnx_host_t	*ha;

	ha = qp->dev->ha;

	QL_DPRINT12(ha, "enter ibqp = %p\n", ibqp);
 
	atomic_inc(&qp->refcnt);

	QL_DPRINT12(ha, "exit \n");
	return;
}

void
qlnxr_iw_qp_rem_ref(struct ib_qp *ibqp)
{
	struct qlnxr_qp *qp = get_qlnxr_qp(ibqp);
	qlnx_host_t	*ha;

	ha = qp->dev->ha;

	QL_DPRINT12(ha, "enter ibqp = %p qp = %p\n", ibqp, qp);

	if (atomic_dec_and_test(&qp->refcnt)) {
		qlnxr_idr_remove(qp->dev, qp->qp_id);
		kfree(qp);
	}

	QL_DPRINT12(ha, "exit \n");
	return;
}

struct ib_qp *
qlnxr_iw_get_qp(struct ib_device *ibdev, int qpn)
{
	struct qlnxr_dev *dev = get_qlnxr_dev(ibdev);
	struct ib_qp *qp;
	qlnx_host_t	*ha;

	ha = dev->ha;

	QL_DPRINT12(ha, "enter dev = %p ibdev = %p qpn = %d\n", dev, ibdev, qpn);

	qp = idr_find(&dev->qpidr, qpn);

	QL_DPRINT12(ha, "exit qp = %p\n", qp);

	return (qp);
}
