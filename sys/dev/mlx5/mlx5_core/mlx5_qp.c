/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


#include <linux/gfp.h>
#include <dev/mlx5/qp.h>
#include <dev/mlx5/driver.h>

#include "mlx5_core.h"

#include "transobj.h"

static struct mlx5_core_rsc_common *mlx5_get_rsc(struct mlx5_core_dev *dev,
						 u32 rsn)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	struct mlx5_core_rsc_common *common;

	spin_lock(&table->lock);

	common = radix_tree_lookup(&table->tree, rsn);
	if (common)
		atomic_inc(&common->refcount);

	spin_unlock(&table->lock);

	if (!common) {
		mlx5_core_warn(dev, "Async event for bogus resource 0x%x\n",
			       rsn);
		return NULL;
	}
	return common;
}

void mlx5_core_put_rsc(struct mlx5_core_rsc_common *common)
{
	if (atomic_dec_and_test(&common->refcount))
		complete(&common->free);
}

void mlx5_rsc_event(struct mlx5_core_dev *dev, u32 rsn, int event_type)
{
	struct mlx5_core_rsc_common *common = mlx5_get_rsc(dev, rsn);
	struct mlx5_core_qp *qp;

	if (!common)
		return;

	switch (common->res) {
	case MLX5_RES_QP:
		qp = (struct mlx5_core_qp *)common;
		qp->event(qp, event_type);
		break;

	default:
		mlx5_core_warn(dev, "invalid resource type for 0x%x\n", rsn);
	}

	mlx5_core_put_rsc(common);
}

static int create_qprqsq_common(struct mlx5_core_dev *dev,
				struct mlx5_core_qp *qp, int rsc_type)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	int err;

	qp->common.res = rsc_type;

	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, qp->qpn | (rsc_type << 24), qp);
	spin_unlock_irq(&table->lock);
	if (err)
		return err;

	atomic_set(&qp->common.refcount, 1);
	init_completion(&qp->common.free);
	qp->pid = curthread->td_proc->p_pid;

	return 0;
}

static void destroy_qprqsq_common(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *qp, int rsc_type)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	unsigned long flags;

	spin_lock_irqsave(&table->lock, flags);
	radix_tree_delete(&table->tree, qp->qpn | (rsc_type << 24));
	spin_unlock_irqrestore(&table->lock, flags);

	mlx5_core_put_rsc((struct mlx5_core_rsc_common *)qp);
	wait_for_completion(&qp->common.free);
}

int mlx5_core_create_qp(struct mlx5_core_dev *dev,
			struct mlx5_core_qp *qp,
			u32 *in, int inlen)
{
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {0};
	u32 dout[MLX5_ST_SZ_DW(destroy_qp_out)] = {0};
	u32 din[MLX5_ST_SZ_DW(destroy_qp_in)] = {0};
	int err;

	MLX5_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err)
		return err;

	qp->qpn = MLX5_GET(create_qp_out, out, qpn);
	mlx5_core_dbg(dev, "qpn = 0x%x\n", qp->qpn);

	err = create_qprqsq_common(dev, qp, MLX5_RES_QP);
	if (err)
		goto err_cmd;

	atomic_inc(&dev->num_qps);

	return 0;

err_cmd:
	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, qp->qpn);
	mlx5_cmd_exec(dev, din, sizeof(din), dout, sizeof(dout));
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_create_qp);

int mlx5_core_destroy_qp(struct mlx5_core_dev *dev,
			 struct mlx5_core_qp *qp)
{
	u32 out[MLX5_ST_SZ_DW(destroy_qp_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_qp_in)]   = {0};
	int err;


	destroy_qprqsq_common(dev, qp, MLX5_RES_QP);

	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, qp->qpn);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	atomic_dec(&dev->num_qps);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_core_destroy_qp);

struct mbox_info {
	u32 *in;
	u32 *out;
	int inlen;
	int outlen;
};

static int mbox_alloc(struct mbox_info *mbox, int inlen, int outlen)
{
	mbox->inlen  = inlen;
	mbox->outlen = outlen;
	mbox->in = kzalloc(mbox->inlen, GFP_KERNEL);
	mbox->out = kzalloc(mbox->outlen, GFP_KERNEL);
	if (!mbox->in || !mbox->out) {
		kfree(mbox->in);
		kfree(mbox->out);
		return -ENOMEM;
	}

	return 0;
}

static void mbox_free(struct mbox_info *mbox)
{
	kfree(mbox->in);
	kfree(mbox->out);
}

static int modify_qp_mbox_alloc(struct mlx5_core_dev *dev, u16 opcode, int qpn,
				u32 opt_param_mask, void *qpc,
				struct mbox_info *mbox)
{
	mbox->out = NULL;
	mbox->in = NULL;

	#define MBOX_ALLOC(mbox, typ)  \
		mbox_alloc(mbox, MLX5_ST_SZ_BYTES(typ##_in), MLX5_ST_SZ_BYTES(typ##_out))

	#define MOD_QP_IN_SET(typ, in, _opcode, _qpn) \
		MLX5_SET(typ##_in, in, opcode, _opcode); \
		MLX5_SET(typ##_in, in, qpn, _qpn)
	#define MOD_QP_IN_SET_QPC(typ, in, _opcode, _qpn, _opt_p, _qpc) \
		MOD_QP_IN_SET(typ, in, _opcode, _qpn); \
		MLX5_SET(typ##_in, in, opt_param_mask, _opt_p); \
		memcpy(MLX5_ADDR_OF(typ##_in, in, qpc), _qpc, MLX5_ST_SZ_BYTES(qpc))

	switch (opcode) {
	/* 2RST & 2ERR */
	case MLX5_CMD_OP_2RST_QP:
		if (MBOX_ALLOC(mbox, qp_2rst))
			return -ENOMEM;
		MOD_QP_IN_SET(qp_2rst, mbox->in, opcode, qpn);
		break;
	case MLX5_CMD_OP_2ERR_QP:
		if (MBOX_ALLOC(mbox, qp_2err))
			return -ENOMEM;
		MOD_QP_IN_SET(qp_2err, mbox->in, opcode, qpn);
		break;

	/* MODIFY with QPC */
	case MLX5_CMD_OP_RST2INIT_QP:
		if (MBOX_ALLOC(mbox, rst2init_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(rst2init_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	case MLX5_CMD_OP_INIT2RTR_QP:
		if (MBOX_ALLOC(mbox, init2rtr_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(init2rtr_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	case MLX5_CMD_OP_RTR2RTS_QP:
		if (MBOX_ALLOC(mbox, rtr2rts_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(rtr2rts_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	case MLX5_CMD_OP_RTS2RTS_QP:
		if (MBOX_ALLOC(mbox, rts2rts_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(rts2rts_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	case MLX5_CMD_OP_SQERR2RTS_QP:
		if (MBOX_ALLOC(mbox, sqerr2rts_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(sqerr2rts_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	case MLX5_CMD_OP_INIT2INIT_QP:
		if (MBOX_ALLOC(mbox, init2init_qp))
			return -ENOMEM;
		MOD_QP_IN_SET_QPC(init2init_qp, mbox->in, opcode, qpn,
				  opt_param_mask, qpc);
		break;
	default:
		mlx5_core_err(dev, "Unknown transition for modify QP: OP(0x%x) QPN(0x%x)\n",
			opcode, qpn);
		return -EINVAL;
	}

	return 0;
}


int mlx5_core_qp_modify(struct mlx5_core_dev *dev, u16 opcode,
			u32 opt_param_mask, void *qpc,
			struct mlx5_core_qp *qp)
{
	struct mbox_info mbox;
	int err;

	err = modify_qp_mbox_alloc(dev, opcode, qp->qpn,
				   opt_param_mask, qpc, &mbox);
	if (err)
		return err;

	err = mlx5_cmd_exec(dev, mbox.in, mbox.inlen, mbox.out, mbox.outlen);
	mbox_free(&mbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_qp_modify);

void mlx5_init_qp_table(struct mlx5_core_dev *dev)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;

	memset(table, 0, sizeof(*table));
	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
}

void mlx5_cleanup_qp_table(struct mlx5_core_dev *dev)
{
}

int mlx5_core_qp_query(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp,
		       u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_qp_in)] = {0};

	MLX5_SET(query_qp_in, in, opcode, MLX5_CMD_OP_QUERY_QP);
	MLX5_SET(query_qp_in, in, qpn, qp->qpn);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}
EXPORT_SYMBOL_GPL(mlx5_core_qp_query);

int mlx5_core_xrcd_alloc(struct mlx5_core_dev *dev, u32 *xrcdn)
{
	u32 in[MLX5_ST_SZ_DW(alloc_xrcd_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(alloc_xrcd_out)] = {0};
	int err;

	MLX5_SET(alloc_xrcd_in, in, opcode, MLX5_CMD_OP_ALLOC_XRCD);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*xrcdn = MLX5_GET(alloc_xrcd_out, out, xrcd);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_xrcd_alloc);

int mlx5_core_xrcd_dealloc(struct mlx5_core_dev *dev, u32 xrcdn)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_xrcd_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(dealloc_xrcd_out)] = {0};

	MLX5_SET(dealloc_xrcd_in, in, opcode, MLX5_CMD_OP_DEALLOC_XRCD);
	MLX5_SET(dealloc_xrcd_in, in, xrcd, xrcdn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL_GPL(mlx5_core_xrcd_dealloc);

int mlx5_core_create_dct(struct mlx5_core_dev *dev,
			 struct mlx5_core_dct *dct,
			 u32 *in)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	u32 out[MLX5_ST_SZ_DW(create_dct_out)]	 = {0};
	u32 dout[MLX5_ST_SZ_DW(destroy_dct_out)] = {0};
	u32 din[MLX5_ST_SZ_DW(destroy_dct_in)]	 = {0};
	int inlen = MLX5_ST_SZ_BYTES(create_dct_in);
	int err;

	init_completion(&dct->drained);
	MLX5_SET(create_dct_in, in, opcode, MLX5_CMD_OP_CREATE_DCT);

	err = mlx5_cmd_exec(dev, in, inlen, &out, sizeof(out));
	if (err) {
		mlx5_core_warn(dev, "create DCT failed, ret %d", err);
		return err;
	}

	dct->dctn = MLX5_GET(create_dct_out, out, dctn);

	dct->common.res = MLX5_RES_DCT;
	spin_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, dct->dctn, dct);
	spin_unlock_irq(&table->lock);
	if (err) {
		mlx5_core_warn(dev, "err %d", err);
		goto err_cmd;
	}

	dct->pid = curthread->td_proc->p_pid;
	atomic_set(&dct->common.refcount, 1);
	init_completion(&dct->common.free);

	return 0;

err_cmd:
	MLX5_SET(destroy_dct_in, din, opcode, MLX5_CMD_OP_DESTROY_DCT);
	MLX5_SET(destroy_dct_in, din, dctn, dct->dctn);
	mlx5_cmd_exec(dev, &din, sizeof(din), &out, sizeof(dout));

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_create_dct);

static int mlx5_core_drain_dct(struct mlx5_core_dev *dev,
			       struct mlx5_core_dct *dct)
{
	u32 out[MLX5_ST_SZ_DW(drain_dct_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(drain_dct_in)]   = {0};

	MLX5_SET(drain_dct_in, in, opcode, MLX5_CMD_OP_DRAIN_DCT);
	MLX5_SET(drain_dct_in, in, dctn, dct->dctn);
	return mlx5_cmd_exec(dev, (void *)&in, sizeof(in),
			     (void *)&out, sizeof(out));
}

int mlx5_core_destroy_dct(struct mlx5_core_dev *dev,
			  struct mlx5_core_dct *dct)
{
	struct mlx5_qp_table *table = &dev->priv.qp_table;
	u32 out[MLX5_ST_SZ_DW(destroy_dct_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_dct_in)]	= {0};
	unsigned long flags;
	int err;

	err = mlx5_core_drain_dct(dev, dct);
	if (err) {
		if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
			goto free_dct;
		} else {
			mlx5_core_warn(dev, "failed drain DCT 0x%x\n", dct->dctn);
			return err;
		}
	}

	wait_for_completion(&dct->drained);

free_dct:
	spin_lock_irqsave(&table->lock, flags);
	if (radix_tree_delete(&table->tree, dct->dctn) != dct)
		mlx5_core_warn(dev, "dct delete differs\n");
	spin_unlock_irqrestore(&table->lock, flags);

	if (atomic_dec_and_test(&dct->common.refcount))
		complete(&dct->common.free);
	wait_for_completion(&dct->common.free);

	MLX5_SET(destroy_dct_in, in, opcode, MLX5_CMD_OP_DESTROY_DCT);
	MLX5_SET(destroy_dct_in, in, dctn, dct->dctn);

	return mlx5_cmd_exec(dev, (void *)&in, sizeof(in),
			     (void *)&out, sizeof(out));
}
EXPORT_SYMBOL_GPL(mlx5_core_destroy_dct);

int mlx5_core_dct_query(struct mlx5_core_dev *dev, struct mlx5_core_dct *dct,
			u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_dct_in)] = {0};

	MLX5_SET(query_dct_in, in, opcode, MLX5_CMD_OP_QUERY_DCT);
	MLX5_SET(query_dct_in, in, dctn, dct->dctn);

	return mlx5_cmd_exec(dev, (void *)&in, sizeof(in),
			     (void *)out, outlen);
}
EXPORT_SYMBOL_GPL(mlx5_core_dct_query);

int mlx5_core_arm_dct(struct mlx5_core_dev *dev, struct mlx5_core_dct *dct)
{
	u32 out[MLX5_ST_SZ_DW(arm_dct_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(arm_dct_in)]   = {0};

	MLX5_SET(arm_dct_in, in, opcode, MLX5_CMD_OP_ARM_DCT_FOR_KEY_VIOLATION);
	MLX5_SET(arm_dct_in, in, dctn, dct->dctn);

	return mlx5_cmd_exec(dev, (void *)&in, sizeof(in),
			     (void *)&out, sizeof(out));
}
EXPORT_SYMBOL_GPL(mlx5_core_arm_dct);

int mlx5_core_create_rq_tracked(struct mlx5_core_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *rq)
{
	int err;

	err = mlx5_core_create_rq(dev, in, inlen, &rq->qpn);
	if (err)
		return err;

	err = create_qprqsq_common(dev, rq, MLX5_RES_RQ);
	if (err)
		mlx5_core_destroy_rq(dev, rq->qpn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_rq_tracked);

void mlx5_core_destroy_rq_tracked(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *rq)
{
	destroy_qprqsq_common(dev, rq, MLX5_RES_RQ);
	mlx5_core_destroy_rq(dev, rq->qpn);
}
EXPORT_SYMBOL(mlx5_core_destroy_rq_tracked);

int mlx5_core_create_sq_tracked(struct mlx5_core_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *sq)
{
	int err;

	err = mlx5_core_create_sq(dev, in, inlen, &sq->qpn);
	if (err)
		return err;

	err = create_qprqsq_common(dev, sq, MLX5_RES_SQ);
	if (err)
		mlx5_core_destroy_sq(dev, sq->qpn);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_sq_tracked);

void mlx5_core_destroy_sq_tracked(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *sq)
{
	destroy_qprqsq_common(dev, sq, MLX5_RES_SQ);
	mlx5_core_destroy_sq(dev, sq->qpn);
}
EXPORT_SYMBOL(mlx5_core_destroy_sq_tracked);
