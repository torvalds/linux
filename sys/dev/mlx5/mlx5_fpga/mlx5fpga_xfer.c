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

#include <dev/mlx5/mlx5_fpga/xfer.h>
#include <dev/mlx5/mlx5_fpga/conn.h>

struct xfer_state {
	const struct mlx5_fpga_transaction *xfer;
	/* Total transactions */
	unsigned int start_count;
	unsigned int done_count;
	unsigned int error_count;
	u8 status;
	/* Inflight transactions */
	unsigned int budget;
	unsigned int inflight_count;
	/* Chunking state */
	size_t pos;
	spinlock_t lock; /* Protects all members of this struct */
};

struct xfer_transaction {
	struct xfer_state *xfer_state;
	struct mlx5_fpga_transaction transaction;
};

static void trans_complete(const struct mlx5_fpga_transaction *complete,
			   u8 status);

static void xfer_complete(struct xfer_state *xfer_state)
{
	const struct mlx5_fpga_transaction *xfer = xfer_state->xfer;
	u8 status = xfer_state->status;

	kfree(xfer_state);
	xfer->complete1(xfer, status);
}

/* Xfer state spin lock must be locked */
static int exec_more(struct xfer_state *xfer_state)
{
	struct xfer_transaction *xfer_trans;
	size_t left, cur_size, page_size;
	u64 pos_addr, ddr_base;
	u8 *pos_data;
	int ret = 0;

	ddr_base = mlx5_fpga_ddr_base_get(xfer_state->xfer->conn->fdev);
	page_size = (xfer_state->xfer->addr + xfer_state->pos < ddr_base) ?
		    sizeof(u32) : (1 << MLX5_FPGA_TRANSACTION_SEND_PAGE_BITS);

	do {
		if (xfer_state->status != IB_WC_SUCCESS) {
			ret = -EIO;
			break;
		}

		left = xfer_state->xfer->size - xfer_state->pos;
		if (!left)
			break;

		xfer_trans = kzalloc(sizeof(*xfer_trans), GFP_ATOMIC);
		if (!xfer_trans) {
			ret = -ENOMEM;
			break;
		}

		pos_addr = xfer_state->xfer->addr + xfer_state->pos;
		pos_data = xfer_state->xfer->data + xfer_state->pos;

		/* Determine largest possible transaction at this point */
		cur_size = page_size - (pos_addr & (page_size - 1));
		if (cur_size > MLX5_FPGA_TRANSACTION_MAX_SIZE)
			cur_size = MLX5_FPGA_TRANSACTION_MAX_SIZE;
		if (cur_size > left)
			cur_size = left;

		xfer_trans->xfer_state = xfer_state;
		xfer_trans->transaction.addr = pos_addr;
		xfer_trans->transaction.complete1 = trans_complete;
		xfer_trans->transaction.conn = xfer_state->xfer->conn;
		xfer_trans->transaction.data = pos_data;
		xfer_trans->transaction.direction = xfer_state->xfer->direction;
		xfer_trans->transaction.size = cur_size;

		xfer_state->start_count++;
		xfer_state->inflight_count++;
		mlx5_fpga_dbg(xfer_state->xfer->conn->fdev, "Starting %zu bytes at %p done; %u started %u inflight %u done %u error\n",
			      xfer_trans->transaction.size,
			      xfer_trans->transaction.data,
			      xfer_state->start_count,
			      xfer_state->inflight_count,
			      xfer_state->done_count,
			      xfer_state->error_count);
		ret = mlx5_fpga_trans_exec(&xfer_trans->transaction);
		if (ret) {
			xfer_state->start_count--;
			xfer_state->inflight_count--;
			if (ret == -EBUSY)
				ret = 0;

			if (ret) {
				mlx5_fpga_warn(xfer_state->xfer->conn->fdev, "Transfer failed to start transaction: %d. %u started %u done %u error\n",
					       ret, xfer_state->start_count,
					       xfer_state->done_count,
					       xfer_state->error_count);
				xfer_state->status = IB_WC_GENERAL_ERR;
			}
			kfree(xfer_trans);
			break;
		}
		xfer_state->pos += cur_size;
		if (xfer_state->inflight_count >= xfer_state->budget)
			break;
	} while (cur_size != left);

	return ret;
}

static void trans_complete(const struct mlx5_fpga_transaction *complete,
			   u8 status)
{
	struct xfer_transaction *xfer_trans;
	struct xfer_state *xfer_state;
	unsigned long flags;
	bool done = false;
	int ret;

	xfer_trans = container_of(complete, struct xfer_transaction,
				  transaction);
	xfer_state = xfer_trans->xfer_state;
	mlx5_fpga_dbg(complete->conn->fdev, "Transaction %zu bytes at %p done, status %u; %u started %u inflight %u done %u error\n",
		      xfer_trans->transaction.size,
		      xfer_trans->transaction.data, status,
		      xfer_state->start_count, xfer_state->inflight_count,
		      xfer_state->done_count, xfer_state->error_count);
	kfree(xfer_trans);

	spin_lock_irqsave(&xfer_state->lock, flags);

	if (status != IB_WC_SUCCESS) {
		xfer_state->error_count++;
		mlx5_fpga_warn(complete->conn->fdev, "Transaction failed during transfer. %u started %u inflight %u done %u error\n",
			       xfer_state->start_count,
			       xfer_state->inflight_count,
			       xfer_state->done_count, xfer_state->error_count);
		if (xfer_state->status == IB_WC_SUCCESS)
			xfer_state->status = status;
	} else {
		xfer_state->done_count++;
	}
	ret = exec_more(xfer_state);

	xfer_state->inflight_count--;
	if (!xfer_state->inflight_count)
		done = true;

	spin_unlock_irqrestore(&xfer_state->lock, flags);

	if (done)
		xfer_complete(xfer_state);
}

int mlx5_fpga_xfer_exec(const struct mlx5_fpga_transaction *xfer)
{
	u64 base = mlx5_fpga_ddr_base_get(xfer->conn->fdev);
	u64 size = mlx5_fpga_ddr_size_get(xfer->conn->fdev);
	struct xfer_state *xfer_state;
	unsigned long flags;
	bool done = false;
	int ret = 0;

	if (xfer->addr + xfer->size > base + size) {
		mlx5_fpga_warn(xfer->conn->fdev, "Transfer ends at %jx outside of DDR range %jx\n",
			       (uintmax_t)(xfer->addr + xfer->size), (uintmax_t)(base + size));
		return -EINVAL;
	}

	if (xfer->addr & MLX5_FPGA_TRANSACTION_SEND_ALIGN_BITS) {
		mlx5_fpga_warn(xfer->conn->fdev, "Transfer address %jx not aligned\n",
			       (uintmax_t)xfer->addr);
		return -EINVAL;
	}

	if (xfer->size & MLX5_FPGA_TRANSACTION_SEND_ALIGN_BITS) {
		mlx5_fpga_warn(xfer->conn->fdev, "Transfer size %zu not aligned\n",
			       xfer->size);
		return -EINVAL;
	}

	if (xfer->size < 1) {
		mlx5_fpga_warn(xfer->conn->fdev, "Empty transfer size %zu not allowed\n",
			       xfer->size);
		return -EINVAL;
	}

	xfer_state = kzalloc(sizeof(*xfer_state), GFP_KERNEL);
	xfer_state->xfer = xfer;
	xfer_state->status = IB_WC_SUCCESS;
	xfer_state->budget = 7;
	spin_lock_init(&xfer_state->lock);
	spin_lock_irqsave(&xfer_state->lock, flags);

	ret = exec_more(xfer_state);
	if (ret && (xfer_state->start_count == 0))
		done = true;

	spin_unlock_irqrestore(&xfer_state->lock, flags);

	if (done)
		xfer_complete(xfer_state);
	return ret;
}
