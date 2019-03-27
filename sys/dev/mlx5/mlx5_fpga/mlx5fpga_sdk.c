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

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <dev/mlx5/device.h>
#include <dev/mlx5/mlx5_fpga/core.h>
#include <dev/mlx5/mlx5_fpga/conn.h>
#include <dev/mlx5/mlx5_fpga/sdk.h>
#include <dev/mlx5/mlx5_fpga/xfer.h>
#include <dev/mlx5/mlx5_core/mlx5_core.h>
/* #include "accel/ipsec.h" */

#define MLX5_FPGA_LOAD_TIMEOUT 25000 /* msec */

struct mem_transfer {
	struct mlx5_fpga_transaction t;
	struct completion comp;
	u8 status;
};

struct mlx5_fpga_conn *
mlx5_fpga_sbu_conn_create(struct mlx5_fpga_device *fdev,
			  struct mlx5_fpga_conn_attr *attr)
{
#ifdef NOT_YET
	/* XXXKIB */
	return mlx5_fpga_conn_create(fdev, attr, MLX5_FPGA_QPC_QP_TYPE_SANDBOX_QP);
#else
	return (NULL);
#endif
}
EXPORT_SYMBOL(mlx5_fpga_sbu_conn_create);

void mlx5_fpga_sbu_conn_destroy(struct mlx5_fpga_conn *conn)
{
#ifdef NOT_YET
	/* XXXKIB */
	mlx5_fpga_conn_destroy(conn);
#endif
}
EXPORT_SYMBOL(mlx5_fpga_sbu_conn_destroy);

int mlx5_fpga_sbu_conn_sendmsg(struct mlx5_fpga_conn *conn,
			       struct mlx5_fpga_dma_buf *buf)
{
#ifdef NOT_YET
	/* XXXKIB */
	return mlx5_fpga_conn_send(conn, buf);
#else
	return (0);
#endif
}
EXPORT_SYMBOL(mlx5_fpga_sbu_conn_sendmsg);

static void mem_complete(const struct mlx5_fpga_transaction *complete,
			 u8 status)
{
	struct mem_transfer *xfer;

	mlx5_fpga_dbg(complete->conn->fdev,
		      "transaction %p complete status %u", complete, status);

	xfer = container_of(complete, struct mem_transfer, t);
	xfer->status = status;
	complete_all(&xfer->comp);
}

static int mem_transaction(struct mlx5_fpga_device *fdev, size_t size, u64 addr,
			   void *buf, enum mlx5_fpga_direction direction)
{
	int ret;
	struct mem_transfer xfer;

	if (!fdev->shell_conn) {
		ret = -ENOTCONN;
		goto out;
	}

	xfer.t.data = buf;
	xfer.t.size = size;
	xfer.t.addr = addr;
	xfer.t.conn = fdev->shell_conn;
	xfer.t.direction = direction;
	xfer.t.complete1 = mem_complete;
	init_completion(&xfer.comp);
	ret = mlx5_fpga_xfer_exec(&xfer.t);
	if (ret) {
		mlx5_fpga_dbg(fdev, "Transfer execution failed: %d\n", ret);
		goto out;
	}
	wait_for_completion(&xfer.comp);
	if (xfer.status != 0)
		ret = -EIO;

out:
	return ret;
}

static int mlx5_fpga_mem_read_i2c(struct mlx5_fpga_device *fdev, size_t size,
				  u64 addr, u8 *buf)
{
	size_t max_size = MLX5_FPGA_ACCESS_REG_SIZE_MAX;
	size_t bytes_done = 0;
	u8 actual_size;
	int err = 0;

	if (!size)
		return -EINVAL;

	if (!fdev->mdev)
		return -ENOTCONN;

	while (bytes_done < size) {
		actual_size = min(max_size, (size - bytes_done));

		err = mlx5_fpga_access_reg(fdev->mdev, actual_size,
					   addr + bytes_done,
					   buf + bytes_done, false);
		if (err) {
			mlx5_fpga_err(fdev, "Failed to read over I2C: %d\n",
				      err);
			break;
		}

		bytes_done += actual_size;
	}

	return err;
}

static int mlx5_fpga_mem_write_i2c(struct mlx5_fpga_device *fdev, size_t size,
				   u64 addr, u8 *buf)
{
	size_t max_size = MLX5_FPGA_ACCESS_REG_SIZE_MAX;
	size_t bytes_done = 0;
	u8 actual_size;
	int err = 0;

	if (!size)
		return -EINVAL;

	if (!fdev->mdev)
		return -ENOTCONN;

	while (bytes_done < size) {
		actual_size = min(max_size, (size - bytes_done));

		err = mlx5_fpga_access_reg(fdev->mdev, actual_size,
					   addr + bytes_done,
					   buf + bytes_done, true);
		if (err) {
			mlx5_fpga_err(fdev, "Failed to write FPGA crspace\n");
			break;
		}

		bytes_done += actual_size;
	}

	return err;
}

int mlx5_fpga_mem_read(struct mlx5_fpga_device *fdev, size_t size, u64 addr,
		       void *buf, enum mlx5_fpga_access_type access_type)
{
	int ret;

	if (access_type == MLX5_FPGA_ACCESS_TYPE_DONTCARE)
		access_type = fdev->shell_conn ? MLX5_FPGA_ACCESS_TYPE_RDMA :
						 MLX5_FPGA_ACCESS_TYPE_I2C;

	mlx5_fpga_dbg(fdev, "Reading %zu bytes at 0x%jx over %s",
		      size, (uintmax_t)addr, access_type ? "RDMA" : "I2C");

	switch (access_type) {
	case MLX5_FPGA_ACCESS_TYPE_RDMA:
		ret = mem_transaction(fdev, size, addr, buf, MLX5_FPGA_READ);
		if (ret)
			return ret;
		break;
	case MLX5_FPGA_ACCESS_TYPE_I2C:
		ret = mlx5_fpga_mem_read_i2c(fdev, size, addr, buf);
		if (ret)
			return ret;
		break;
	default:
		mlx5_fpga_warn(fdev, "Unexpected read access_type %u\n",
			       access_type);
		return -EACCES;
	}

	return size;
}
EXPORT_SYMBOL(mlx5_fpga_mem_read);

int mlx5_fpga_mem_write(struct mlx5_fpga_device *fdev, size_t size, u64 addr,
			void *buf, enum mlx5_fpga_access_type access_type)
{
	int ret;

	if (access_type == MLX5_FPGA_ACCESS_TYPE_DONTCARE)
		access_type = fdev->shell_conn ? MLX5_FPGA_ACCESS_TYPE_RDMA :
						 MLX5_FPGA_ACCESS_TYPE_I2C;

	mlx5_fpga_dbg(fdev, "Writing %zu bytes at 0x%jx over %s",
		      size, (uintmax_t)addr, access_type ? "RDMA" : "I2C");

	switch (access_type) {
	case MLX5_FPGA_ACCESS_TYPE_RDMA:
		ret = mem_transaction(fdev, size, addr, buf, MLX5_FPGA_WRITE);
		if (ret)
			return ret;
		break;
	case MLX5_FPGA_ACCESS_TYPE_I2C:
		ret = mlx5_fpga_mem_write_i2c(fdev, size, addr, buf);
		if (ret)
			return ret;
		break;
	default:
		mlx5_fpga_warn(fdev, "Unexpected write access_type %u\n",
			       access_type);
		return -EACCES;
	}

	return size;
}
EXPORT_SYMBOL(mlx5_fpga_mem_write);

int mlx5_fpga_get_sbu_caps(struct mlx5_fpga_device *fdev, int size, void *buf)
{
	return mlx5_fpga_sbu_caps(fdev->mdev, buf, size);
}
EXPORT_SYMBOL(mlx5_fpga_get_sbu_caps);

u64 mlx5_fpga_ddr_size_get(struct mlx5_fpga_device *fdev)
{
	return (u64)MLX5_CAP_FPGA(fdev->mdev, fpga_ddr_size) << 10;
}
EXPORT_SYMBOL(mlx5_fpga_ddr_size_get);

u64 mlx5_fpga_ddr_base_get(struct mlx5_fpga_device *fdev)
{
	return MLX5_CAP64_FPGA(fdev->mdev, fpga_ddr_start_addr);
}
EXPORT_SYMBOL(mlx5_fpga_ddr_base_get);

void mlx5_fpga_client_data_set(struct mlx5_fpga_device *fdev,
			       struct mlx5_fpga_client *client, void *data)
{
	struct mlx5_fpga_client_data *context;

	list_for_each_entry(context, &fdev->client_data_list, list) {
		if (context->client != client)
			continue;
		context->data = data;
		return;
	}

	mlx5_fpga_warn(fdev, "No client context found for %s\n", client->name);
}
EXPORT_SYMBOL(mlx5_fpga_client_data_set);

void *mlx5_fpga_client_data_get(struct mlx5_fpga_device *fdev,
				struct mlx5_fpga_client *client)
{
	struct mlx5_fpga_client_data *context;
	void *ret = NULL;

	list_for_each_entry(context, &fdev->client_data_list, list) {
		if (context->client != client)
			continue;
		ret = context->data;
		goto out;
	}
	mlx5_fpga_warn(fdev, "No client context found for %s\n", client->name);

out:
	return ret;
}
EXPORT_SYMBOL(mlx5_fpga_client_data_get);

void mlx5_fpga_device_query(struct mlx5_fpga_device *fdev,
			    struct mlx5_fpga_query *query)
{
	unsigned long flags;

	spin_lock_irqsave(&fdev->state_lock, flags);
	query->image_status = fdev->image_status;
	query->admin_image = fdev->last_admin_image;
	query->oper_image = fdev->last_oper_image;
	spin_unlock_irqrestore(&fdev->state_lock, flags);
}
EXPORT_SYMBOL(mlx5_fpga_device_query);

int mlx5_fpga_device_reload(struct mlx5_fpga_device *fdev,
			    enum mlx5_fpga_image image)
{
	struct mlx5_core_dev *mdev = fdev->mdev;
	unsigned long timeout;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&fdev->state_lock, flags);
	switch (fdev->fdev_state) {
	case MLX5_FDEV_STATE_NONE:
		err = -ENODEV;
		break;
	case MLX5_FDEV_STATE_IN_PROGRESS:
		err = -EBUSY;
		break;
	case MLX5_FDEV_STATE_SUCCESS:
	case MLX5_FDEV_STATE_FAILURE:
	case MLX5_FDEV_STATE_DISCONNECTED:
		break;
	}
	spin_unlock_irqrestore(&fdev->state_lock, flags);
	if (err)
		return err;

	mutex_lock(&mdev->intf_state_mutex);
	clear_bit(MLX5_INTERFACE_STATE_UP, &mdev->intf_state);

	mlx5_unregister_device(mdev);
	/* XXXKIB	mlx5_accel_ipsec_cleanup(mdev); */
	mlx5_fpga_device_stop(mdev);

	fdev->fdev_state = MLX5_FDEV_STATE_IN_PROGRESS;
	reinit_completion(&fdev->load_event);

	if (image <= MLX5_FPGA_IMAGE_MAX) {
		mlx5_fpga_info(fdev, "Loading from flash\n");
		err = mlx5_fpga_load(mdev, image);
		if (err) {
			mlx5_fpga_err(fdev, "Failed to request load: %d\n",
				      err);
			goto out;
		}
	} else {
		mlx5_fpga_info(fdev, "Resetting\n");
		err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_RESET);
		if (err) {
			mlx5_fpga_err(fdev, "Failed to request reset: %d\n",
				      err);
			goto out;
		}
	}

	timeout = jiffies + msecs_to_jiffies(MLX5_FPGA_LOAD_TIMEOUT);
	err = wait_for_completion_timeout(&fdev->load_event, timeout - jiffies);
	if (err < 0) {
		mlx5_fpga_err(fdev, "Failed waiting for FPGA load: %d\n", err);
		fdev->fdev_state = MLX5_FDEV_STATE_FAILURE;
		goto out;
	}

	err = mlx5_fpga_device_start(mdev);
	if (err) {
		mlx5_core_err(mdev, "fpga device start failed %d\n", err);
		goto out;
	}
	/* XXXKIB err = mlx5_accel_ipsec_init(mdev); */
	if (err) {
		mlx5_core_err(mdev, "IPSec device start failed %d\n", err);
		goto err_fpga;
	}

	err = mlx5_register_device(mdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5_register_device failed %d\n", err);
		fdev->fdev_state = MLX5_FDEV_STATE_FAILURE;
		goto err_ipsec;
	}

	set_bit(MLX5_INTERFACE_STATE_UP, &mdev->intf_state);
	goto out;

err_ipsec:
	/* XXXKIB mlx5_accel_ipsec_cleanup(mdev); */
err_fpga:
	mlx5_fpga_device_stop(mdev);
out:
	mutex_unlock(&mdev->intf_state_mutex);
	return err;
}
EXPORT_SYMBOL(mlx5_fpga_device_reload);

int mlx5_fpga_flash_select(struct mlx5_fpga_device *fdev,
			   enum mlx5_fpga_image image)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&fdev->state_lock, flags);
	switch (fdev->fdev_state) {
	case MLX5_FDEV_STATE_NONE:
		spin_unlock_irqrestore(&fdev->state_lock, flags);
		return -ENODEV;
	case MLX5_FDEV_STATE_DISCONNECTED:
	case MLX5_FDEV_STATE_IN_PROGRESS:
	case MLX5_FDEV_STATE_SUCCESS:
	case MLX5_FDEV_STATE_FAILURE:
		break;
	}
	spin_unlock_irqrestore(&fdev->state_lock, flags);

	err = mlx5_fpga_image_select(fdev->mdev, image);
	if (err)
		mlx5_fpga_err(fdev, "Failed to select flash image: %d\n", err);
	else
		fdev->last_admin_image = image;
	return err;
}
EXPORT_SYMBOL(mlx5_fpga_flash_select);

int mlx5_fpga_connectdisconnect(struct mlx5_fpga_device *fdev,
				enum mlx5_fpga_connect *connect)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&fdev->state_lock, flags);
	switch (fdev->fdev_state) {
	case MLX5_FDEV_STATE_NONE:
		spin_unlock_irqrestore(&fdev->state_lock, flags);
		return -ENODEV;
	case MLX5_FDEV_STATE_IN_PROGRESS:
	case MLX5_FDEV_STATE_SUCCESS:
	case MLX5_FDEV_STATE_FAILURE:
	case MLX5_FDEV_STATE_DISCONNECTED:
		break;
	}
	spin_unlock_irqrestore(&fdev->state_lock, flags);

	err = mlx5_fpga_ctrl_connect(fdev->mdev, connect);
	if (err)
		mlx5_fpga_err(fdev, "Failed to connect/disconnect: %d\n", err);
	return err;
}
EXPORT_SYMBOL(mlx5_fpga_connectdisconnect);

int mlx5_fpga_temperature(struct mlx5_fpga_device *fdev,
			  struct mlx5_fpga_temperature *temp)
{
	return mlx5_fpga_query_mtmp(fdev->mdev, temp);
}
EXPORT_SYMBOL(mlx5_fpga_temperature);

struct device *mlx5_fpga_dev(struct mlx5_fpga_device *fdev)
{
	return &fdev->mdev->pdev->dev;
}
EXPORT_SYMBOL(mlx5_fpga_dev);

void mlx5_fpga_get_cap(struct mlx5_fpga_device *fdev, u32 *fpga_caps)
{
	unsigned long flags;

	spin_lock_irqsave(&fdev->state_lock, flags);
	memcpy(fpga_caps, &fdev->mdev->caps.fpga, sizeof(fdev->mdev->caps.fpga));
	spin_unlock_irqrestore(&fdev->state_lock, flags);
}
EXPORT_SYMBOL(mlx5_fpga_get_cap);
