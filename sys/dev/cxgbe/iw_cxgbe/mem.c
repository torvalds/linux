/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#ifdef TCP_OFFLOAD
#include <linux/types.h>
#include <linux/kref.h>
#include <rdma/ib_umem.h>
#include <asm/atomic.h>

#include <common/t4_msg.h>
#include "iw_cxgbe.h"

#define T4_ULPTX_MIN_IO 32
#define C4IW_MAX_INLINE_SIZE 96
#define T4_ULPTX_MAX_DMA 1024

static int
mr_exceeds_hw_limits(struct c4iw_dev *dev, u64 length)
{

	return (is_t5(dev->rdev.adap) && length >= 8*1024*1024*1024ULL);
}

static int
_c4iw_write_mem_dma_aligned(struct c4iw_rdev *rdev, u32 addr, u32 len,
				void *data, int wait)
{
	struct adapter *sc = rdev->adap;
	struct ulp_mem_io *ulpmc;
	struct ulptx_sgl *sgl;
	u8 wr_len;
	int ret = 0;
	struct c4iw_wr_wait wr_wait;
	struct wrqe *wr;

	addr &= 0x7FFFFFF;

	if (wait)
		c4iw_init_wr_wait(&wr_wait);
	wr_len = roundup(sizeof *ulpmc + sizeof *sgl, 16);

	wr = alloc_wrqe(wr_len, &sc->sge.ctrlq[0]);
	if (wr == NULL)
		return -ENOMEM;
	ulpmc = wrtod(wr);

	memset(ulpmc, 0, wr_len);
	INIT_ULPTX_WR(ulpmc, wr_len, 0, 0);
	ulpmc->wr.wr_hi = cpu_to_be32(V_FW_WR_OP(FW_ULPTX_WR) |
				    (wait ? F_FW_WR_COMPL : 0));
	ulpmc->wr.wr_lo = wait ? (u64)(unsigned long)&wr_wait : 0;
	ulpmc->wr.wr_mid = cpu_to_be32(V_FW_WR_LEN16(DIV_ROUND_UP(wr_len, 16)));
	ulpmc->cmd = cpu_to_be32(V_ULPTX_CMD(ULP_TX_MEM_WRITE) |
			       V_T5_ULP_MEMIO_ORDER(1) |
			V_T5_ULP_MEMIO_FID(sc->sge.ofld_rxq[0].iq.abs_id));
	ulpmc->dlen = cpu_to_be32(V_ULP_MEMIO_DATA_LEN(len>>5));
	ulpmc->len16 = cpu_to_be32(DIV_ROUND_UP(wr_len-sizeof(ulpmc->wr), 16));
	ulpmc->lock_addr = cpu_to_be32(V_ULP_MEMIO_ADDR(addr));

	sgl = (struct ulptx_sgl *)(ulpmc + 1);
	sgl->cmd_nsge = cpu_to_be32(V_ULPTX_CMD(ULP_TX_SC_DSGL) |
				    V_ULPTX_NSGE(1));
	sgl->len0 = cpu_to_be32(len);
	sgl->addr0 = cpu_to_be64((u64)data);

	t4_wrq_tx(sc, wr);

	if (wait)
		ret = c4iw_wait_for_reply(rdev, &wr_wait, 0, 0, NULL, __func__);
	return ret;
}


static int
_c4iw_write_mem_inline(struct c4iw_rdev *rdev, u32 addr, u32 len, void *data)
{
	struct adapter *sc = rdev->adap;
	struct ulp_mem_io *ulpmc;
	struct ulptx_idata *ulpsc;
	u8 wr_len, *to_dp, *from_dp;
	int copy_len, num_wqe, i, ret = 0;
	struct c4iw_wr_wait wr_wait;
	struct wrqe *wr;
	u32 cmd;

	cmd = cpu_to_be32(V_ULPTX_CMD(ULP_TX_MEM_WRITE));

	cmd |= cpu_to_be32(F_T5_ULP_MEMIO_IMM);

	addr &= 0x7FFFFFF;
	CTR3(KTR_IW_CXGBE, "%s addr 0x%x len %u", __func__, addr, len);
	num_wqe = DIV_ROUND_UP(len, C4IW_MAX_INLINE_SIZE);
	c4iw_init_wr_wait(&wr_wait);
	for (i = 0; i < num_wqe; i++) {

		copy_len = min(len, C4IW_MAX_INLINE_SIZE);
		wr_len = roundup(sizeof *ulpmc + sizeof *ulpsc +
				 roundup(copy_len, T4_ULPTX_MIN_IO), 16);

		wr = alloc_wrqe(wr_len, &sc->sge.ctrlq[0]);
		if (wr == NULL)
			return -ENOMEM;
		ulpmc = wrtod(wr);

		memset(ulpmc, 0, wr_len);
		INIT_ULPTX_WR(ulpmc, wr_len, 0, 0);

		if (i == (num_wqe-1)) {
			ulpmc->wr.wr_hi = cpu_to_be32(V_FW_WR_OP(FW_ULPTX_WR) |
						    F_FW_WR_COMPL);
			ulpmc->wr.wr_lo =
				       (__force __be64)(unsigned long) &wr_wait;
		} else
			ulpmc->wr.wr_hi = cpu_to_be32(V_FW_WR_OP(FW_ULPTX_WR));
		ulpmc->wr.wr_mid = cpu_to_be32(
				       V_FW_WR_LEN16(DIV_ROUND_UP(wr_len, 16)));

		ulpmc->cmd = cmd;
		ulpmc->dlen = cpu_to_be32(V_ULP_MEMIO_DATA_LEN(
		    DIV_ROUND_UP(copy_len, T4_ULPTX_MIN_IO)));
		ulpmc->len16 = cpu_to_be32(DIV_ROUND_UP(wr_len-sizeof(ulpmc->wr),
						      16));
		ulpmc->lock_addr = cpu_to_be32(V_ULP_MEMIO_ADDR(addr + i * 3));

		ulpsc = (struct ulptx_idata *)(ulpmc + 1);
		ulpsc->cmd_more = cpu_to_be32(V_ULPTX_CMD(ULP_TX_SC_IMM));
		ulpsc->len = cpu_to_be32(roundup(copy_len, T4_ULPTX_MIN_IO));

		to_dp = (u8 *)(ulpsc + 1);
		from_dp = (u8 *)data + i * C4IW_MAX_INLINE_SIZE;
		if (data)
			memcpy(to_dp, from_dp, copy_len);
		else
			memset(to_dp, 0, copy_len);
		if (copy_len % T4_ULPTX_MIN_IO)
			memset(to_dp + copy_len, 0, T4_ULPTX_MIN_IO -
			       (copy_len % T4_ULPTX_MIN_IO));
		t4_wrq_tx(sc, wr);
		len -= C4IW_MAX_INLINE_SIZE;
	}

	ret = c4iw_wait_for_reply(rdev, &wr_wait, 0, 0, NULL, __func__);
	return ret;
}

static int
_c4iw_write_mem_dma(struct c4iw_rdev *rdev, u32 addr, u32 len, void *data)
{
	struct c4iw_dev *rhp = rdev_to_c4iw_dev(rdev);
	u32 remain = len;
	u32 dmalen;
	int ret = 0;
	dma_addr_t daddr;
	dma_addr_t save;

	daddr = dma_map_single(rhp->ibdev.dma_device, data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(rhp->ibdev.dma_device, daddr))
		return -1;
	save = daddr;

	while (remain > inline_threshold) {
		if (remain < T4_ULPTX_MAX_DMA) {
			if (remain & ~T4_ULPTX_MIN_IO)
				dmalen = remain & ~(T4_ULPTX_MIN_IO-1);
			else
				dmalen = remain;
		} else
			dmalen = T4_ULPTX_MAX_DMA;
		remain -= dmalen;
		ret = _c4iw_write_mem_dma_aligned(rdev, addr, dmalen,
				(void *)daddr, !remain);
		if (ret)
			goto out;
		addr += dmalen >> 5;
		data = (u8 *)data + dmalen;
		daddr = daddr + dmalen;
	}
	if (remain)
		ret = _c4iw_write_mem_inline(rdev, addr, remain, data);
out:
	dma_unmap_single(rhp->ibdev.dma_device, save, len, DMA_TO_DEVICE);
	return ret;
}

/*
 * write len bytes of data into addr (32B aligned address)
 * If data is NULL, clear len byte of memory to zero.
 */
static int
write_adapter_mem(struct c4iw_rdev *rdev, u32 addr, u32 len,
			     void *data)
{
	if (rdev->adap->params.ulptx_memwrite_dsgl && use_dsgl) {
		if (len > inline_threshold) {
			if (_c4iw_write_mem_dma(rdev, addr, len, data)) {
				log(LOG_ERR, "%s: dma map "
				       "failure (non fatal)\n", __func__);
				return _c4iw_write_mem_inline(rdev, addr, len,
							      data);
			} else
				return 0;
		} else
			return _c4iw_write_mem_inline(rdev, addr, len, data);
	} else
		return _c4iw_write_mem_inline(rdev, addr, len, data);
}


/*
 * Build and write a TPT entry.
 * IN: stag key, pdid, perm, bind_enabled, zbva, to, len, page_size,
 *     pbl_size and pbl_addr
 * OUT: stag index
 */
static int write_tpt_entry(struct c4iw_rdev *rdev, u32 reset_tpt_entry,
			   u32 *stag, u8 stag_state, u32 pdid,
			   enum fw_ri_stag_type type, enum fw_ri_mem_perms perm,
			   int bind_enabled, u32 zbva, u64 to,
			   u64 len, u8 page_size, u32 pbl_size, u32 pbl_addr)
{
	int err;
	struct fw_ri_tpte tpt;
	u32 stag_idx;
	static atomic_t key;

	if (c4iw_fatal_error(rdev))
		return -EIO;

	stag_state = stag_state > 0;
	stag_idx = (*stag) >> 8;

	if ((!reset_tpt_entry) && (*stag == T4_STAG_UNSET)) {
		stag_idx = c4iw_get_resource(&rdev->resource.tpt_table);
		if (!stag_idx) {
			mutex_lock(&rdev->stats.lock);
			rdev->stats.stag.fail++;
			mutex_unlock(&rdev->stats.lock);
			return -ENOMEM;
		}
		mutex_lock(&rdev->stats.lock);
		rdev->stats.stag.cur += 32;
		if (rdev->stats.stag.cur > rdev->stats.stag.max)
			rdev->stats.stag.max = rdev->stats.stag.cur;
		mutex_unlock(&rdev->stats.lock);
		*stag = (stag_idx << 8) | (atomic_inc_return(&key) & 0xff);
	}
	CTR5(KTR_IW_CXGBE,
	    "%s stag_state 0x%0x type 0x%0x pdid 0x%0x, stag_idx 0x%x",
	    __func__, stag_state, type, pdid, stag_idx);

	/* write TPT entry */
	if (reset_tpt_entry)
		memset(&tpt, 0, sizeof(tpt));
	else {
		tpt.valid_to_pdid = cpu_to_be32(F_FW_RI_TPTE_VALID |
			V_FW_RI_TPTE_STAGKEY((*stag & M_FW_RI_TPTE_STAGKEY)) |
			V_FW_RI_TPTE_STAGSTATE(stag_state) |
			V_FW_RI_TPTE_STAGTYPE(type) | V_FW_RI_TPTE_PDID(pdid));
		tpt.locread_to_qpid = cpu_to_be32(V_FW_RI_TPTE_PERM(perm) |
			(bind_enabled ? F_FW_RI_TPTE_MWBINDEN : 0) |
			V_FW_RI_TPTE_ADDRTYPE((zbva ? FW_RI_ZERO_BASED_TO :
						      FW_RI_VA_BASED_TO))|
			V_FW_RI_TPTE_PS(page_size));
		tpt.nosnoop_pbladdr = !pbl_size ? 0 : cpu_to_be32(
			V_FW_RI_TPTE_PBLADDR(PBL_OFF(rdev, pbl_addr)>>3));
		tpt.len_lo = cpu_to_be32((u32)(len & 0xffffffffUL));
		tpt.va_hi = cpu_to_be32((u32)(to >> 32));
		tpt.va_lo_fbo = cpu_to_be32((u32)(to & 0xffffffffUL));
		tpt.dca_mwbcnt_pstag = cpu_to_be32(0);
		tpt.len_hi = cpu_to_be32((u32)(len >> 32));
	}
	err = write_adapter_mem(rdev, stag_idx +
				(rdev->adap->vres.stag.start >> 5),
				sizeof(tpt), &tpt);

	if (reset_tpt_entry) {
		c4iw_put_resource(&rdev->resource.tpt_table, stag_idx);
		mutex_lock(&rdev->stats.lock);
		rdev->stats.stag.cur -= 32;
		mutex_unlock(&rdev->stats.lock);
	}
	return err;
}

static int write_pbl(struct c4iw_rdev *rdev, __be64 *pbl,
		     u32 pbl_addr, u32 pbl_size)
{
	int err;

	CTR4(KTR_IW_CXGBE, "%s *pdb_addr 0x%x, pbl_base 0x%x, pbl_size %d",
	     __func__, pbl_addr, rdev->adap->vres.pbl.start, pbl_size);

	err = write_adapter_mem(rdev, pbl_addr >> 5, pbl_size << 3, pbl);
	return err;
}

static int dereg_mem(struct c4iw_rdev *rdev, u32 stag, u32 pbl_size,
		     u32 pbl_addr)
{
	return write_tpt_entry(rdev, 1, &stag, 0, 0, 0, 0, 0, 0, 0UL, 0, 0,
			       pbl_size, pbl_addr);
}

static int allocate_window(struct c4iw_rdev *rdev, u32 * stag, u32 pdid)
{
	*stag = T4_STAG_UNSET;
	return write_tpt_entry(rdev, 0, stag, 0, pdid, FW_RI_STAG_MW, 0, 0, 0,
			       0UL, 0, 0, 0, 0);
}

static int deallocate_window(struct c4iw_rdev *rdev, u32 stag)
{
	return write_tpt_entry(rdev, 1, &stag, 0, 0, 0, 0, 0, 0, 0UL, 0, 0, 0,
			       0);
}

static int allocate_stag(struct c4iw_rdev *rdev, u32 *stag, u32 pdid,
			 u32 pbl_size, u32 pbl_addr)
{
	*stag = T4_STAG_UNSET;
	return write_tpt_entry(rdev, 0, stag, 0, pdid, FW_RI_STAG_NSMR, 0, 0, 0,
			       0UL, 0, 0, pbl_size, pbl_addr);
}

static int finish_mem_reg(struct c4iw_mr *mhp, u32 stag)
{
	u32 mmid;

	mhp->attr.state = 1;
	mhp->attr.stag = stag;
	mmid = stag >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	CTR3(KTR_IW_CXGBE, "%s mmid 0x%x mhp %p", __func__, mmid, mhp);
	return insert_handle(mhp->rhp, &mhp->rhp->mmidr, mhp, mmid);
}

static int register_mem(struct c4iw_dev *rhp, struct c4iw_pd *php,
		      struct c4iw_mr *mhp, int shift)
{
	u32 stag = T4_STAG_UNSET;
	int ret;

	ret = write_tpt_entry(&rhp->rdev, 0, &stag, 1, mhp->attr.pdid,
			      FW_RI_STAG_NSMR, mhp->attr.len ? mhp->attr.perms : 0,
			      mhp->attr.mw_bind_enable, mhp->attr.zbva,
			      mhp->attr.va_fbo, mhp->attr.len ? mhp->attr.len : -1, shift - 12,
			      mhp->attr.pbl_size, mhp->attr.pbl_addr);
	if (ret)
		return ret;

	ret = finish_mem_reg(mhp, stag);
	if (ret)
		dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
	return ret;
}

static int alloc_pbl(struct c4iw_mr *mhp, int npages)
{
	mhp->attr.pbl_addr = c4iw_pblpool_alloc(&mhp->rhp->rdev,
						    npages << 3);

	if (!mhp->attr.pbl_addr)
		return -ENOMEM;

	mhp->attr.pbl_size = npages;

	return 0;
}

struct ib_mr *c4iw_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;
	int ret;
	u32 stag = T4_STAG_UNSET;

	CTR2(KTR_IW_CXGBE, "%s ib_pd %p", __func__, pd);
	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);

	mhp->rhp = rhp;
	mhp->attr.pdid = php->pdid;
	mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	mhp->attr.mw_bind_enable = (acc&IB_ACCESS_MW_BIND) == IB_ACCESS_MW_BIND;
	mhp->attr.zbva = 0;
	mhp->attr.va_fbo = 0;
	mhp->attr.page_size = 0;
	mhp->attr.len = ~0ULL;
	mhp->attr.pbl_size = 0;

	ret = write_tpt_entry(&rhp->rdev, 0, &stag, 1, php->pdid,
			      FW_RI_STAG_NSMR, mhp->attr.perms,
			      mhp->attr.mw_bind_enable, 0, 0, ~0ULL, 0, 0, 0);
	if (ret)
		goto err1;

	ret = finish_mem_reg(mhp, stag);
	if (ret)
		goto err2;
	return &mhp->ibmr;
err2:
	dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		  mhp->attr.pbl_addr);
err1:
	kfree(mhp);
	return ERR_PTR(ret);
}

struct ib_mr *c4iw_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
		u64 virt, int acc, struct ib_udata *udata)
{
	__be64 *pages;
	int shift, n, len;
	int i, k, entry;
	int err = 0;
	struct scatterlist *sg;
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;

	CTR2(KTR_IW_CXGBE, "%s ib_pd %p", __func__, pd);

	if (length == ~0ULL)
		return ERR_PTR(-EINVAL);

	if ((length + start) < start)
		return ERR_PTR(-EINVAL);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	if (mr_exceeds_hw_limits(rhp, length))
		return ERR_PTR(-EINVAL);

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);

	mhp->rhp = rhp;

	mhp->umem = ib_umem_get(pd->uobject->context, start, length, acc, 0);
	if (IS_ERR(mhp->umem)) {
		err = PTR_ERR(mhp->umem);
		kfree(mhp);
		return ERR_PTR(err);
	}

	shift = ffs(mhp->umem->page_size) - 1;
	
	n = mhp->umem->nmap;
	err = alloc_pbl(mhp, n);
	if (err)
		goto err;

	pages = (__be64 *) __get_free_page(GFP_KERNEL);
	if (!pages) {
		err = -ENOMEM;
		goto err_pbl;
	}

	i = n = 0;
	for_each_sg(mhp->umem->sg_head.sgl, sg, mhp->umem->nmap, entry) {
		len = sg_dma_len(sg) >> shift;
		for (k = 0; k < len; ++k) {
			pages[i++] = cpu_to_be64(sg_dma_address(sg) +
					mhp->umem->page_size * k);
			if (i == PAGE_SIZE / sizeof *pages) {
				err = write_pbl(&mhp->rhp->rdev,
						pages,
						mhp->attr.pbl_addr + (n << 3), i);
				if (err)
					goto pbl_done;
				n += i;
				i = 0;

			}
		}
	}

	if (i)
		err = write_pbl(&mhp->rhp->rdev, pages,
				     mhp->attr.pbl_addr + (n << 3), i);

pbl_done:
	free_page((unsigned long) pages);
	if (err)
		goto err_pbl;

	mhp->attr.pdid = php->pdid;
	mhp->attr.zbva = 0;
	mhp->attr.perms = c4iw_ib_to_tpt_access(acc);
	mhp->attr.va_fbo = virt;
	mhp->attr.page_size = shift - 12;
	mhp->attr.len = length;

	err = register_mem(rhp, php, mhp, shift);
	if (err)
		goto err_pbl;

	return &mhp->ibmr;

err_pbl:
	c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);

err:
	ib_umem_release(mhp->umem);
	kfree(mhp);
	return ERR_PTR(err);
}

struct ib_mw *c4iw_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
	struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mw *mhp;
	u32 mmid;
	u32 stag = 0;
	int ret;

	if (type != IB_MW_TYPE_1)
		return ERR_PTR(-EINVAL);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;
	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);
	ret = allocate_window(&rhp->rdev, &stag, php->pdid);
	if (ret) {
		kfree(mhp);
		return ERR_PTR(ret);
	}
	mhp->rhp = rhp;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = FW_RI_STAG_MW;
	mhp->attr.stag = stag;
	mmid = (stag) >> 8;
	mhp->ibmw.rkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid)) {
		deallocate_window(&rhp->rdev, mhp->attr.stag);
		kfree(mhp);
		return ERR_PTR(-ENOMEM);
	}
	CTR4(KTR_IW_CXGBE, "%s mmid 0x%x mhp %p stag 0x%x", __func__, mmid, mhp,
	    stag);
	return &(mhp->ibmw);
}

int c4iw_dealloc_mw(struct ib_mw *mw)
{
	struct c4iw_dev *rhp;
	struct c4iw_mw *mhp;
	u32 mmid;

	mhp = to_c4iw_mw(mw);
	rhp = mhp->rhp;
	mmid = (mw->rkey) >> 8;
	remove_handle(rhp, &rhp->mmidr, mmid);
	deallocate_window(&rhp->rdev, mhp->attr.stag);
	kfree(mhp);
	CTR4(KTR_IW_CXGBE, "%s ib_mw %p mmid 0x%x ptr %p", __func__, mw, mmid,
	    mhp);
	return 0;
}

struct ib_mr *c4iw_alloc_mr(struct ib_pd *pd,
			    enum ib_mr_type mr_type,
			    u32 max_num_sg)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;
	struct c4iw_mr *mhp;
	u32 mmid;
	u32 stag = 0;
	int ret = 0;
	int length = roundup(max_num_sg * sizeof(u64), 32);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;

	if (mr_type != IB_MR_TYPE_MEM_REG ||
	    max_num_sg > t4_max_fr_depth(
		    rhp->rdev.adap->params.ulptx_memwrite_dsgl && use_dsgl))
		return ERR_PTR(-EINVAL);

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp) {
		ret = -ENOMEM;
		goto err;
	}

	mhp->mpl = dma_alloc_coherent(rhp->ibdev.dma_device,
				      length, &mhp->mpl_addr, GFP_KERNEL);
	if (!mhp->mpl) {
		ret = -ENOMEM;
		goto err_mpl;
	}
	mhp->max_mpl_len = length;

	mhp->rhp = rhp;
	ret = alloc_pbl(mhp, max_num_sg);
	if (ret)
		goto err1;
	mhp->attr.pbl_size = max_num_sg;
	ret = allocate_stag(&rhp->rdev, &stag, php->pdid,
			    mhp->attr.pbl_size, mhp->attr.pbl_addr);
	if (ret)
		goto err2;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = FW_RI_STAG_NSMR;
	mhp->attr.stag = stag;
	mhp->attr.state = 0;
	mmid = (stag) >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid)) {
		ret = -ENOMEM;
		goto err3;
	}

	PDBG("%s mmid 0x%x mhp %p stag 0x%x\n", __func__, mmid, mhp, stag);
	return &(mhp->ibmr);
err3:
	dereg_mem(&rhp->rdev, stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
err2:
	c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);
err1:
	dma_free_coherent(rhp->ibdev.dma_device,
			  mhp->max_mpl_len, mhp->mpl, mhp->mpl_addr);
err_mpl:
	kfree(mhp);
err:
	return ERR_PTR(ret);
}
static int c4iw_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct c4iw_mr *mhp = to_c4iw_mr(ibmr);

	if (unlikely(mhp->mpl_len == mhp->max_mpl_len))
		return -ENOMEM;

	mhp->mpl[mhp->mpl_len++] = addr;

	return 0;
}

int c4iw_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		   int sg_nents, unsigned int *sg_offset)
{
	struct c4iw_mr *mhp = to_c4iw_mr(ibmr);

	mhp->mpl_len = 0;

	return ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, c4iw_set_page);
}


int c4iw_dereg_mr(struct ib_mr *ib_mr)
{
	struct c4iw_dev *rhp;
	struct c4iw_mr *mhp;
	u32 mmid;

	CTR2(KTR_IW_CXGBE, "%s ib_mr %p", __func__, ib_mr);

	mhp = to_c4iw_mr(ib_mr);
	rhp = mhp->rhp;
	mmid = mhp->attr.stag >> 8;
	remove_handle(rhp, &rhp->mmidr, mmid);
	dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
	if (mhp->attr.pbl_size)
		c4iw_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
				  mhp->attr.pbl_size << 3);
	if (mhp->kva)
		kfree((void *) (unsigned long) mhp->kva);
	if (mhp->umem)
		ib_umem_release(mhp->umem);
	CTR3(KTR_IW_CXGBE, "%s mmid 0x%x ptr %p", __func__, mmid, mhp);
	kfree(mhp);
	return 0;
}

void c4iw_invalidate_mr(struct c4iw_dev *rhp, u32 rkey)
{
	struct c4iw_mr *mhp;
	unsigned long flags;

	spin_lock_irqsave(&rhp->lock, flags);
	mhp = get_mhp(rhp, rkey >> 8);
	if (mhp)
		mhp->attr.state = 0;
	spin_unlock_irqrestore(&rhp->lock, flags);
}
#endif
