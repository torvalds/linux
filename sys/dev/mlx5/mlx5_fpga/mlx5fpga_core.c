/*-
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#include <linux/module.h>
#include <linux/etherdevice.h>
#include <dev/mlx5/driver.h>
#include <dev/mlx5/mlx5_core/mlx5_core.h>
#include <dev/mlx5/mlx5_lib/mlx5.h>
#include <dev/mlx5/mlx5_fpga/core.h>
#include <dev/mlx5/mlx5_fpga/conn.h>
#include <dev/mlx5/mlx5_fpga/trans.h>

static LIST_HEAD(mlx5_fpga_devices);
static LIST_HEAD(mlx5_fpga_clients);
/* protects access between client un/registration and device add/remove calls */
static DEFINE_MUTEX(mlx5_fpga_mutex);

static const char *const mlx5_fpga_error_strings[] = {
	"Null Syndrome",
	"Corrupted DDR",
	"Flash Timeout",
	"Internal Link Error",
	"Watchdog HW Failure",
	"I2C Failure",
	"Image Changed",
	"Temperature Critical",
};

static const char * const mlx5_fpga_qp_error_strings[] = {
	"Null Syndrome",
	"Retry Counter Expired",
	"RNR Expired",
};

static void client_context_destroy(struct mlx5_fpga_device *fdev,
				   struct mlx5_fpga_client_data *context)
{
	mlx5_fpga_dbg(fdev, "Deleting client context %p of client %p\n",
		      context, context->client);
	if (context->client->destroy)
		context->client->destroy(fdev);
	list_del(&context->list);
	kfree(context);
}

static int client_context_create(struct mlx5_fpga_device *fdev,
				 struct mlx5_fpga_client *client,
				 struct mlx5_fpga_client_data **pctx)
{
	struct mlx5_fpga_client_data *context;

	context = kmalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	context->client = client;
	context->data = NULL;
	context->added  = false;
	list_add(&context->list, &fdev->client_data_list);

	mlx5_fpga_dbg(fdev, "Adding client context %p client %p\n",
		      context, client);

	if (client->create)
		client->create(fdev);

	if (pctx)
		*pctx = context;
	return 0;
}

static struct mlx5_fpga_device *mlx5_fpga_device_alloc(void)
{
	struct mlx5_fpga_device *fdev = NULL;

	fdev = kzalloc(sizeof(*fdev), GFP_KERNEL);
	if (!fdev)
		return NULL;

	spin_lock_init(&fdev->state_lock);
	init_completion(&fdev->load_event);
	fdev->fdev_state = MLX5_FDEV_STATE_NONE;
	INIT_LIST_HEAD(&fdev->client_data_list);
	return fdev;
}

static const char *mlx5_fpga_image_name(enum mlx5_fpga_image image)
{
	switch (image) {
	case MLX5_FPGA_IMAGE_USER:
		return "user";
	case MLX5_FPGA_IMAGE_FACTORY:
		return "factory";
	default:
		return "unknown";
	}
}

static const char *mlx5_fpga_name(u32 fpga_id)
{
	static char ret[32];

	switch (fpga_id) {
	case MLX5_FPGA_NEWTON:
		return "Newton";
	case MLX5_FPGA_EDISON:
		return "Edison";
	case MLX5_FPGA_MORSE:
		return "Morse";
	case MLX5_FPGA_MORSEQ:
		return "MorseQ";
	}

	snprintf(ret, sizeof(ret), "Unknown %d", fpga_id);
	return ret;
}

static int mlx5_fpga_device_load_check(struct mlx5_fpga_device *fdev)
{
	struct mlx5_fpga_query query;
	int err;
	u32 fpga_id;

	err = mlx5_fpga_query(fdev->mdev, &query);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to query status: %d\n", err);
		return err;
	}

	fdev->last_admin_image = query.admin_image;
	fdev->last_oper_image = query.oper_image;
	fdev->image_status = query.image_status;

	mlx5_fpga_info(fdev, "Status %u; Admin image %u; Oper image %u\n",
		      query.image_status, query.admin_image, query.oper_image);

	/* For Morse projects FPGA has no influence to network functionality */
	fpga_id = MLX5_CAP_FPGA(fdev->mdev, fpga_id);
	if (fpga_id == MLX5_FPGA_MORSE || fpga_id == MLX5_FPGA_MORSEQ)
		return 0;

	if (query.image_status != MLX5_FPGA_STATUS_SUCCESS) {
		mlx5_fpga_err(fdev, "%s image failed to load; status %u\n",
			      mlx5_fpga_image_name(fdev->last_oper_image),
			      query.image_status);
		return -EIO;
	}

	return 0;
}

static int mlx5_fpga_device_brb(struct mlx5_fpga_device *fdev)
{
	int err;
	struct mlx5_core_dev *mdev = fdev->mdev;

	err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_ON);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to set bypass on: %d\n", err);
		return err;
	}
	err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_RESET_SANDBOX);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to reset SBU: %d\n", err);
		return err;
	}
	err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_OFF);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to set bypass off: %d\n", err);
		return err;
	}
	return 0;
}

int mlx5_fpga_device_start(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_client_data *client_context;
	struct mlx5_fpga_device *fdev = mdev->fpga;
	struct mlx5_fpga_conn_attr conn_attr = {0};
	struct mlx5_fpga_conn *conn;
	unsigned int max_num_qps;
	unsigned long flags;
	u32 fpga_id;
	u32 vid;
	u16 pid;
	int err;

	if (!fdev)
		return 0;

	err = mlx5_fpga_caps(fdev->mdev);
	if (err)
		goto out;

	err = mlx5_fpga_device_load_check(fdev);
	if (err)
		goto out;

	fpga_id = MLX5_CAP_FPGA(fdev->mdev, fpga_id);
	mlx5_fpga_info(fdev, "FPGA card %s\n", mlx5_fpga_name(fpga_id));

	if (fpga_id == MLX5_FPGA_MORSE || fpga_id == MLX5_FPGA_MORSEQ)
		goto out;

	mlx5_fpga_info(fdev, "%s(%d) image, version %u; SBU %06x:%04x version %d\n",
		       mlx5_fpga_image_name(fdev->last_oper_image),
		       fdev->last_oper_image,
		       MLX5_CAP_FPGA(fdev->mdev, image_version),
		       MLX5_CAP_FPGA(fdev->mdev, ieee_vendor_id),
		       MLX5_CAP_FPGA(fdev->mdev, sandbox_product_id),
		       MLX5_CAP_FPGA(fdev->mdev, sandbox_product_version));

	max_num_qps = MLX5_CAP_FPGA(mdev, shell_caps.max_num_qps);
	err = mlx5_core_reserve_gids(mdev, max_num_qps);
	if (err)
		goto out;

#ifdef NOT_YET
	/* XXXKIB */
	err = mlx5_fpga_conn_device_init(fdev);
#else
	err = 0;
#endif
	if (err)
		goto err_rsvd_gid;

	err = mlx5_fpga_trans_device_init(fdev);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to init transaction: %d\n",
			      err);
		goto err_conn_init;
	}

	conn_attr.tx_size = MLX5_FPGA_TID_COUNT;
	conn_attr.rx_size = MLX5_FPGA_TID_COUNT;
	conn_attr.recv_cb = mlx5_fpga_trans_recv;
	conn_attr.cb_arg = fdev;
#ifdef NOT_YET
	/* XXXKIB */
	conn = mlx5_fpga_conn_create(fdev, &conn_attr,
				     MLX5_FPGA_QPC_QP_TYPE_SHELL_QP);
	if (IS_ERR(conn)) {
		err = PTR_ERR(conn);
		mlx5_fpga_err(fdev, "Failed to create shell conn: %d\n", err);
		goto err_trans;
	}
#else
	conn = NULL;
#endif
	fdev->shell_conn = conn;

	if (fdev->last_oper_image == MLX5_FPGA_IMAGE_USER) {
		err = mlx5_fpga_device_brb(fdev);
		if (err)
			goto err_shell_conn;

		vid = MLX5_CAP_FPGA(fdev->mdev, ieee_vendor_id);
		pid = MLX5_CAP_FPGA(fdev->mdev, sandbox_product_id);
		mutex_lock(&mlx5_fpga_mutex);
		list_for_each_entry(client_context, &fdev->client_data_list,
				    list) {
			if (client_context->client->add(fdev, vid, pid))
				continue;
			client_context->added = true;
		}
		mutex_unlock(&mlx5_fpga_mutex);
	}

	goto out;

err_shell_conn:
	if (fdev->shell_conn) {
#ifdef NOT_YET
		/* XXXKIB */
		mlx5_fpga_conn_destroy(fdev->shell_conn);
#endif
		fdev->shell_conn = NULL;
	}

#ifdef NOT_YET
		/* XXXKIB */
err_trans:
#endif
	mlx5_fpga_trans_device_cleanup(fdev);

err_conn_init:
#ifdef NOT_YET
	/* XXXKIB */
	mlx5_fpga_conn_device_cleanup(fdev);
#endif

err_rsvd_gid:
	mlx5_core_unreserve_gids(mdev, max_num_qps);
out:
	spin_lock_irqsave(&fdev->state_lock, flags);
	fdev->fdev_state = err ? MLX5_FDEV_STATE_FAILURE : MLX5_FDEV_STATE_SUCCESS;
	spin_unlock_irqrestore(&fdev->state_lock, flags);
	return err;
}

int mlx5_fpga_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = NULL;
	struct mlx5_fpga_client *client;

	if (!MLX5_CAP_GEN(mdev, fpga)) {
		mlx5_core_dbg(mdev, "FPGA capability not present\n");
		return 0;
	}

	mlx5_core_dbg(mdev, "Initializing FPGA\n");

	fdev = mlx5_fpga_device_alloc();
	if (!fdev)
		return -ENOMEM;

	fdev->mdev = mdev;
	mdev->fpga = fdev;

	mutex_lock(&mlx5_fpga_mutex);

	list_add_tail(&fdev->list, &mlx5_fpga_devices);
	list_for_each_entry(client, &mlx5_fpga_clients, list)
		client_context_create(fdev, client, NULL);

	mutex_unlock(&mlx5_fpga_mutex);
	return 0;
}

void mlx5_fpga_device_stop(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_client_data *client_context;
	struct mlx5_fpga_device *fdev = mdev->fpga;
	unsigned int max_num_qps;
	unsigned long flags;
	int err;
	u32 fpga_id;

	if (!fdev)
		return;

	fpga_id = MLX5_CAP_FPGA(mdev, fpga_id);
	if (fpga_id == MLX5_FPGA_MORSE || fpga_id == MLX5_FPGA_MORSEQ)
		return;

	spin_lock_irqsave(&fdev->state_lock, flags);

	if (fdev->fdev_state != MLX5_FDEV_STATE_SUCCESS) {
		spin_unlock_irqrestore(&fdev->state_lock, flags);
		return;
	}
	fdev->fdev_state = MLX5_FDEV_STATE_NONE;
	spin_unlock_irqrestore(&fdev->state_lock, flags);

	if (fdev->last_oper_image == MLX5_FPGA_IMAGE_USER) {
		err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_ON);
		if (err)
			mlx5_fpga_err(fdev, "Failed to re-set SBU bypass on: %d\n",
				      err);
	}

	mutex_lock(&mlx5_fpga_mutex);
	list_for_each_entry(client_context, &fdev->client_data_list, list) {
		if (!client_context->added)
			continue;
		client_context->client->remove(fdev);
		client_context->added = false;
	}
	mutex_unlock(&mlx5_fpga_mutex);

	if (fdev->shell_conn) {
#ifdef NOT_YET
		/* XXXKIB */
		mlx5_fpga_conn_destroy(fdev->shell_conn);
#endif
		fdev->shell_conn = NULL;
		mlx5_fpga_trans_device_cleanup(fdev);
	}
#ifdef NOT_YET
	/* XXXKIB */
	mlx5_fpga_conn_device_cleanup(fdev);
#endif
	max_num_qps = MLX5_CAP_FPGA(mdev, shell_caps.max_num_qps);
	mlx5_core_unreserve_gids(mdev, max_num_qps);
}

void mlx5_fpga_cleanup(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_client_data *context, *tmp;
	struct mlx5_fpga_device *fdev = mdev->fpga;

	if (!fdev)
		return;

	mutex_lock(&mlx5_fpga_mutex);

	mlx5_fpga_device_stop(mdev);

	list_for_each_entry_safe(context, tmp, &fdev->client_data_list, list)
		client_context_destroy(fdev, context);

	list_del(&fdev->list);
	kfree(fdev);
	mdev->fpga = NULL;

	mutex_unlock(&mlx5_fpga_mutex);
}

static const char *mlx5_fpga_syndrome_to_string(u8 syndrome)
{
	if (syndrome < ARRAY_SIZE(mlx5_fpga_error_strings))
		return mlx5_fpga_error_strings[syndrome];
	return "Unknown";
}

static const char *mlx5_fpga_qp_syndrome_to_string(u8 syndrome)
{
	if (syndrome < ARRAY_SIZE(mlx5_fpga_qp_error_strings))
		return mlx5_fpga_qp_error_strings[syndrome];
	return "Unknown";
}

void mlx5_fpga_event(struct mlx5_core_dev *mdev, u8 event, void *data)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;
	const char *event_name;
	bool teardown = false;
	unsigned long flags;
	u32 fpga_qpn;
	u8 syndrome;

	switch (event) {
	case MLX5_EVENT_TYPE_FPGA_ERROR:
		syndrome = MLX5_GET(fpga_error_event, data, syndrome);
		event_name = mlx5_fpga_syndrome_to_string(syndrome);
		break;
	case MLX5_EVENT_TYPE_FPGA_QP_ERROR:
		syndrome = MLX5_GET(fpga_qp_error_event, data, syndrome);
		event_name = mlx5_fpga_qp_syndrome_to_string(syndrome);
		fpga_qpn = MLX5_GET(fpga_qp_error_event, data, fpga_qpn);
		mlx5_fpga_err(fdev, "Error %u on QP %u: %s\n",
			      syndrome, fpga_qpn, event_name);
		break;
	default:
		mlx5_fpga_warn_ratelimited(fdev, "Unexpected event %u\n",
					   event);
		return;
	}

	spin_lock_irqsave(&fdev->state_lock, flags);
	switch (fdev->fdev_state) {
	case MLX5_FDEV_STATE_SUCCESS:
		mlx5_fpga_warn(fdev, "Error %u: %s\n", syndrome, event_name);
		teardown = true;
		break;
	case MLX5_FDEV_STATE_IN_PROGRESS:
		if (syndrome != MLX5_FPGA_ERROR_EVENT_SYNDROME_IMAGE_CHANGED)
			mlx5_fpga_warn(fdev, "Error while loading %u: %s\n",
				       syndrome, event_name);
		complete(&fdev->load_event);
		break;
	default:
		mlx5_fpga_warn_ratelimited(fdev, "Unexpected error event %u: %s\n",
					   syndrome, event_name);
	}
	spin_unlock_irqrestore(&fdev->state_lock, flags);
	/* We tear-down the card's interfaces and functionality because
	 * the FPGA bump-on-the-wire is misbehaving and we lose ability
	 * to communicate with the network. User may still be able to
	 * recover by re-programming or debugging the FPGA
	 */
	if (teardown)
		mlx5_trigger_health_work(fdev->mdev);
}

void mlx5_fpga_client_register(struct mlx5_fpga_client *client)
{
	struct mlx5_fpga_client_data *context;
	struct mlx5_fpga_device *fdev;
	bool call_add = false;
	unsigned long flags;
	u32 vid;
	u16 pid;
	int err;

	pr_debug("Client register %s\n", client->name);

	mutex_lock(&mlx5_fpga_mutex);

	list_add_tail(&client->list, &mlx5_fpga_clients);

	list_for_each_entry(fdev, &mlx5_fpga_devices, list) {
		err = client_context_create(fdev, client, &context);
		if (err)
			continue;

		spin_lock_irqsave(&fdev->state_lock, flags);
		call_add = (fdev->fdev_state == MLX5_FDEV_STATE_SUCCESS);
		spin_unlock_irqrestore(&fdev->state_lock, flags);

		if (call_add) {
			vid = MLX5_CAP_FPGA(fdev->mdev, ieee_vendor_id);
			pid = MLX5_CAP_FPGA(fdev->mdev, sandbox_product_id);
			if (!client->add(fdev, vid, pid))
				context->added = true;
		}
	}

	mutex_unlock(&mlx5_fpga_mutex);
}
EXPORT_SYMBOL(mlx5_fpga_client_register);

void mlx5_fpga_client_unregister(struct mlx5_fpga_client *client)
{
	struct mlx5_fpga_client_data *context, *tmp_context;
	struct mlx5_fpga_device *fdev;

	pr_debug("Client unregister %s\n", client->name);

	mutex_lock(&mlx5_fpga_mutex);

	list_for_each_entry(fdev, &mlx5_fpga_devices, list) {
		list_for_each_entry_safe(context, tmp_context,
					 &fdev->client_data_list,
					 list) {
			if (context->client != client)
				continue;
			if (context->added)
				client->remove(fdev);
			client_context_destroy(fdev, context);
			break;
		}
	}

	list_del(&client->list);
	mutex_unlock(&mlx5_fpga_mutex);
}
EXPORT_SYMBOL(mlx5_fpga_client_unregister);

#if (__FreeBSD_version >= 1100000)
MODULE_DEPEND(mlx5fpga, linuxkpi, 1, 1, 1);
#endif
MODULE_DEPEND(mlx5fpga, mlx5, 1, 1, 1);
MODULE_VERSION(mlx5fpga, 1);
