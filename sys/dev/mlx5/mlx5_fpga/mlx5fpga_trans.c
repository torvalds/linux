/*-
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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
 *
 * $FreeBSD$
 */

#include <dev/mlx5/mlx5_fpga/trans.h>
#include <dev/mlx5/mlx5_fpga/conn.h>

enum mlx5_fpga_transaction_state {
	TRANS_STATE_NONE,
	TRANS_STATE_SEND,
	TRANS_STATE_WAIT,
	TRANS_STATE_COMPLETE,
};

struct mlx5_fpga_trans_priv {
	const struct mlx5_fpga_transaction *user_trans;
	u8 tid;
	enum mlx5_fpga_transaction_state state;
	u8 status;
	u32 header[MLX5_ST_SZ_DW(fpga_shell_qp_packet)];
	struct mlx5_fpga_dma_buf buf;
	struct list_head list_item;
};

struct mlx5_fpga_trans_device_state {
	spinlock_t lock; /* Protects all members of this struct */
	struct list_head free_queue;
	struct mlx5_fpga_trans_priv transactions[MLX5_FPGA_TID_COUNT];
};

static struct mlx5_fpga_trans_priv *find_tid(struct mlx5_fpga_device *fdev,
					     u8 tid)
{
	if (tid >= MLX5_FPGA_TID_COUNT) {
		mlx5_fpga_warn(fdev, "Unexpected transaction ID %u\n", tid);
		return NULL;
	}
	return &fdev->trans->transactions[tid];
}

static struct mlx5_fpga_trans_priv *alloc_tid(struct mlx5_fpga_device *fdev)
{
	struct mlx5_fpga_trans_priv *ret;
	unsigned long flags;

	spin_lock_irqsave(&fdev->trans->lock, flags);

	if (list_empty(&fdev->trans->free_queue)) {
		mlx5_fpga_dbg(fdev, "No free transaction ID available\n");
		ret = NULL;
		goto out;
	}

	ret = list_first_entry(&fdev->trans->free_queue,
			       struct mlx5_fpga_trans_priv, list_item);
	list_del(&ret->list_item);

	ret->state = TRANS_STATE_NONE;
out:
	spin_unlock_irqrestore(&fdev->trans->lock, flags);
	return ret;
}

static void free_tid(struct mlx5_fpga_device *fdev,
		     struct mlx5_fpga_trans_priv *trans_priv)
{
	unsigned long flags;

	spin_lock_irqsave(&fdev->trans->lock, flags);
	list_add_tail(&trans_priv->list_item, &fdev->trans->free_queue);
	spin_unlock_irqrestore(&fdev->trans->lock, flags);
}

static void trans_complete(struct mlx5_fpga_device *fdev,
			   struct mlx5_fpga_trans_priv *trans_priv, u8 status)
{
	const struct mlx5_fpga_transaction *user_trans;
	unsigned long flags;

	mlx5_fpga_dbg(fdev, "Transaction %u is complete with status %u\n",
		      trans_priv->tid, status);

	spin_lock_irqsave(&fdev->trans->lock, flags);
	trans_priv->state = TRANS_STATE_COMPLETE;
	trans_priv->status = status;
	spin_unlock_irqrestore(&fdev->trans->lock, flags);

	user_trans = trans_priv->user_trans;
	free_tid(fdev, trans_priv);

	if (user_trans->complete1)
		user_trans->complete1(user_trans, status);
}

static void trans_send_complete(struct mlx5_fpga_conn *conn,
				struct mlx5_fpga_device *fdev,
				struct mlx5_fpga_dma_buf *buf, u8 status)
{
	unsigned long flags;
	struct mlx5_fpga_trans_priv *trans_priv;

	trans_priv = container_of(buf, struct mlx5_fpga_trans_priv, buf);
	mlx5_fpga_dbg(fdev, "send complete tid %u. Status: %u\n",
		      trans_priv->tid, status);
	if (status) {
		trans_complete(fdev, trans_priv, status);
		return;
	}

	spin_lock_irqsave(&fdev->trans->lock, flags);
	if (trans_priv->state == TRANS_STATE_SEND)
		trans_priv->state = TRANS_STATE_WAIT;
	spin_unlock_irqrestore(&fdev->trans->lock, flags);
}

static int trans_validate(struct mlx5_fpga_device *fdev, u64 addr, size_t size)
{
	if (size > MLX5_FPGA_TRANSACTION_MAX_SIZE) {
		mlx5_fpga_warn(fdev, "Cannot access %zu bytes at once. Max is %u\n",
			       size, MLX5_FPGA_TRANSACTION_MAX_SIZE);
		return -EINVAL;
	}
	if (size & MLX5_FPGA_TRANSACTION_SEND_ALIGN_BITS) {
		mlx5_fpga_warn(fdev, "Cannot access %zu bytes. Must be full dwords\n",
			       size);
		return -EINVAL;
	}
	if (size < 1) {
		mlx5_fpga_warn(fdev, "Cannot access %zu bytes. Empty transaction not allowed\n",
			       size);
		return -EINVAL;
	}
	if (addr & MLX5_FPGA_TRANSACTION_SEND_ALIGN_BITS) {
		mlx5_fpga_warn(fdev, "Cannot access %zu bytes at unaligned address %jx\n",
			       size, (uintmax_t)addr);
		return -EINVAL;
	}
	if ((addr >> MLX5_FPGA_TRANSACTION_SEND_PAGE_BITS) !=
	    ((addr + size - 1) >> MLX5_FPGA_TRANSACTION_SEND_PAGE_BITS)) {
		mlx5_fpga_warn(fdev, "Cannot access %zu bytes at address %jx. Crosses page boundary\n",
			       size, (uintmax_t)addr);
		return -EINVAL;
	}
	if (addr < mlx5_fpga_ddr_base_get(fdev)) {
		if (size != sizeof(u32)) {
			mlx5_fpga_warn(fdev, "Cannot access %zu bytes at cr-space address %jx. Must access a single dword\n",
				       size, (uintmax_t)addr);
			return -EINVAL;
		}
	}
	return 0;
}

int mlx5_fpga_trans_exec(const struct mlx5_fpga_transaction *trans)
{
	struct mlx5_fpga_conn *conn = trans->conn;
	struct mlx5_fpga_trans_priv *trans_priv;
	u32 *header;
	int err;

	if (!trans->complete1) {
		mlx5_fpga_warn(conn->fdev, "Transaction must have a completion callback\n");
		err = -EINVAL;
		goto out;
	}

	err = trans_validate(conn->fdev, trans->addr, trans->size);
	if (err)
		goto out;

	trans_priv = alloc_tid(conn->fdev);
	if (!trans_priv) {
		err = -EBUSY;
		goto out;
	}
	trans_priv->user_trans = trans;
	header = trans_priv->header;

	memset(header, 0, sizeof(trans_priv->header));
	memset(&trans_priv->buf, 0, sizeof(trans_priv->buf));
	MLX5_SET(fpga_shell_qp_packet, header, type,
		 (trans->direction == MLX5_FPGA_WRITE) ?
		 MLX5_FPGA_SHELL_QP_PACKET_TYPE_DDR_WRITE :
		 MLX5_FPGA_SHELL_QP_PACKET_TYPE_DDR_READ);
	MLX5_SET(fpga_shell_qp_packet, header, tid, trans_priv->tid);
	MLX5_SET(fpga_shell_qp_packet, header, len, trans->size);
	MLX5_SET64(fpga_shell_qp_packet, header, address, trans->addr);

	trans_priv->buf.sg[0].data = header;
	trans_priv->buf.sg[0].size = sizeof(trans_priv->header);
	if (trans->direction == MLX5_FPGA_WRITE) {
		trans_priv->buf.sg[1].data = trans->data;
		trans_priv->buf.sg[1].size = trans->size;
	}

	trans_priv->buf.complete = trans_send_complete;
	trans_priv->state = TRANS_STATE_SEND;

#ifdef NOT_YET
	/* XXXKIB */
	err = mlx5_fpga_conn_send(conn->fdev->shell_conn, &trans_priv->buf);
#else
	err = 0;
#endif
	if (err)
		goto out_buf_tid;
	goto out;

out_buf_tid:
	free_tid(conn->fdev, trans_priv);
out:
	return err;
}

void mlx5_fpga_trans_recv(void *cb_arg, struct mlx5_fpga_dma_buf *buf)
{
	struct mlx5_fpga_device *fdev = cb_arg;
	struct mlx5_fpga_trans_priv *trans_priv;
	size_t payload_len;
	u8 status = 0;
	u8 tid, type;

	mlx5_fpga_dbg(fdev, "Rx QP message on core conn; %u bytes\n",
		      buf->sg[0].size);

	if (buf->sg[0].size < MLX5_ST_SZ_BYTES(fpga_shell_qp_packet)) {
		mlx5_fpga_warn(fdev, "Short message %u bytes from device\n",
			       buf->sg[0].size);
		goto out;
	}
	payload_len = buf->sg[0].size - MLX5_ST_SZ_BYTES(fpga_shell_qp_packet);

	tid = MLX5_GET(fpga_shell_qp_packet, buf->sg[0].data, tid);
	trans_priv = find_tid(fdev, tid);
	if (!trans_priv)
		goto out;

	type = MLX5_GET(fpga_shell_qp_packet, buf->sg[0].data, type);
	switch (type) {
	case MLX5_FPGA_SHELL_QP_PACKET_TYPE_DDR_READ_RESPONSE:
		if (trans_priv->user_trans->direction != MLX5_FPGA_READ) {
			mlx5_fpga_warn(fdev, "Wrong answer type %u to a %u transaction\n",
				       type, trans_priv->user_trans->direction);
			status = -EIO;
			goto complete;
		}
		if (payload_len != trans_priv->user_trans->size) {
			mlx5_fpga_warn(fdev, "Incorrect transaction payload length %zu expected %zu\n",
				       payload_len,
				       trans_priv->user_trans->size);
			goto complete;
		}
		memcpy(trans_priv->user_trans->data,
		       MLX5_ADDR_OF(fpga_shell_qp_packet, buf->sg[0].data,
				    data), payload_len);
		break;
	case MLX5_FPGA_SHELL_QP_PACKET_TYPE_DDR_WRITE_RESPONSE:
		if (trans_priv->user_trans->direction != MLX5_FPGA_WRITE) {
			mlx5_fpga_warn(fdev, "Wrong answer type %u to a %u transaction\n",
				       type, trans_priv->user_trans->direction);
			status = -EIO;
			goto complete;
		}
		break;
	default:
		mlx5_fpga_warn(fdev, "Unexpected message type %u len %u from device\n",
			       type, buf->sg[0].size);
		status = -EIO;
		goto complete;
	}

complete:
	trans_complete(fdev, trans_priv, status);
out:
	return;
}

int mlx5_fpga_trans_device_init(struct mlx5_fpga_device *fdev)
{
	int ret = 0;
	int tid;

	fdev->trans = kzalloc(sizeof(*fdev->trans), GFP_KERNEL);
	if (!fdev->trans) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&fdev->trans->free_queue);
	for (tid = 0; tid < ARRAY_SIZE(fdev->trans->transactions); tid++) {
		fdev->trans->transactions[tid].tid = tid;
		list_add_tail(&fdev->trans->transactions[tid].list_item,
			      &fdev->trans->free_queue);
	}

	spin_lock_init(&fdev->trans->lock);

out:
	return ret;
}

void mlx5_fpga_trans_device_cleanup(struct mlx5_fpga_device *fdev)
{
	kfree(fdev->trans);
}
