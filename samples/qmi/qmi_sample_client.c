// SPDX-License-Identifier: GPL-2.0
/*
 * Sample in-kernel QMI client driver
 *
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/qrtr.h>
#include <linux/net.h>
#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/string.h>
#include <net/sock.h>
#include <linux/soc/qcom/qmi.h>

#define PING_REQ1_TLV_TYPE		0x1
#define PING_RESP1_TLV_TYPE		0x2
#define PING_OPT1_TLV_TYPE		0x10
#define PING_OPT2_TLV_TYPE		0x11

#define DATA_REQ1_TLV_TYPE		0x1
#define DATA_RESP1_TLV_TYPE		0x2
#define DATA_OPT1_TLV_TYPE		0x10
#define DATA_OPT2_TLV_TYPE		0x11

#define TEST_MED_DATA_SIZE_V01		8192
#define TEST_MAX_NAME_SIZE_V01		255

#define TEST_PING_REQ_MSG_ID_V01	0x20
#define TEST_DATA_REQ_MSG_ID_V01	0x21

#define TEST_PING_REQ_MAX_MSG_LEN_V01	266
#define TEST_DATA_REQ_MAX_MSG_LEN_V01	8456

struct test_name_type_v01 {
	u32 name_len;
	char name[TEST_MAX_NAME_SIZE_V01];
};

static struct qmi_elem_info test_name_type_v01_ei[] = {
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
		.offset		= offsetof(struct test_name_type_v01,
					   name_len),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= TEST_MAX_NAME_SIZE_V01,
		.elem_size	= sizeof(char),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
		.offset		= offsetof(struct test_name_type_v01,
					   name),
	},
	{}
};

struct test_ping_req_msg_v01 {
	char ping[4];

	u8 client_name_valid;
	struct test_name_type_v01 client_name;
};

static struct qmi_elem_info test_ping_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 4,
		.elem_size	= sizeof(char),
		.array_type	= STATIC_ARRAY,
		.tlv_type	= PING_REQ1_TLV_TYPE,
		.offset		= offsetof(struct test_ping_req_msg_v01,
					   ping),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= PING_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_ping_req_msg_v01,
					   client_name_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct test_name_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= PING_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_ping_req_msg_v01,
					   client_name),
		.ei_array	= test_name_type_v01_ei,
	},
	{}
};

struct test_ping_resp_msg_v01 {
	struct qmi_response_type_v01 resp;

	u8 pong_valid;
	char pong[4];

	u8 service_name_valid;
	struct test_name_type_v01 service_name;
};

static struct qmi_elem_info test_ping_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= PING_RESP1_TLV_TYPE,
		.offset		= offsetof(struct test_ping_resp_msg_v01,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= PING_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_ping_resp_msg_v01,
					   pong_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 4,
		.elem_size	= sizeof(char),
		.array_type	= STATIC_ARRAY,
		.tlv_type	= PING_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_ping_resp_msg_v01,
					   pong),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= PING_OPT2_TLV_TYPE,
		.offset		= offsetof(struct test_ping_resp_msg_v01,
					   service_name_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct test_name_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= PING_OPT2_TLV_TYPE,
		.offset		= offsetof(struct test_ping_resp_msg_v01,
					   service_name),
		.ei_array	= test_name_type_v01_ei,
	},
	{}
};

struct test_data_req_msg_v01 {
	u32 data_len;
	u8 data[TEST_MED_DATA_SIZE_V01];

	u8 client_name_valid;
	struct test_name_type_v01 client_name;
};

static struct qmi_elem_info test_data_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_REQ1_TLV_TYPE,
		.offset		= offsetof(struct test_data_req_msg_v01,
					   data_len),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= TEST_MED_DATA_SIZE_V01,
		.elem_size	= sizeof(u8),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= DATA_REQ1_TLV_TYPE,
		.offset		= offsetof(struct test_data_req_msg_v01,
					   data),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_data_req_msg_v01,
					   client_name_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct test_name_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_data_req_msg_v01,
					   client_name),
		.ei_array	= test_name_type_v01_ei,
	},
	{}
};

struct test_data_resp_msg_v01 {
	struct qmi_response_type_v01 resp;

	u8 data_valid;
	u32 data_len;
	u8 data[TEST_MED_DATA_SIZE_V01];

	u8 service_name_valid;
	struct test_name_type_v01 service_name;
};

static struct qmi_elem_info test_data_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_RESP1_TLV_TYPE,
		.offset		= offsetof(struct test_data_resp_msg_v01,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_data_resp_msg_v01,
					   data_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_data_resp_msg_v01,
					   data_len),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= TEST_MED_DATA_SIZE_V01,
		.elem_size	= sizeof(u8),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= DATA_OPT1_TLV_TYPE,
		.offset		= offsetof(struct test_data_resp_msg_v01,
					   data),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_OPT2_TLV_TYPE,
		.offset		= offsetof(struct test_data_resp_msg_v01,
					   service_name_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct test_name_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= DATA_OPT2_TLV_TYPE,
		.offset		= offsetof(struct test_data_resp_msg_v01,
					   service_name),
		.ei_array	= test_name_type_v01_ei,
	},
	{}
};

/*
 * ping_write() - ping_pong debugfs file write handler
 * @file:	debugfs file context
 * @user_buf:	reference to the user data (ignored)
 * @count:	number of bytes in @user_buf
 * @ppos:	offset in @file to write
 *
 * This function allows user space to send out a ping_pong QMI encoded message
 * to the associated remote test service and will return with the result of the
 * transaction. It serves as an example of how to provide a custom response
 * handler.
 *
 * Return: @count, or negative errno on failure.
 */
static ssize_t ping_write(struct file *file, const char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	struct qmi_handle *qmi = file->private_data;
	struct test_ping_req_msg_v01 req = {};
	struct qmi_txn txn;
	int ret;

	memcpy(req.ping, "ping", sizeof(req.ping));

	ret = qmi_txn_init(qmi, &txn, NULL, NULL);
	if (ret < 0)
		return ret;

	ret = qmi_send_request(qmi, NULL, &txn,
			       TEST_PING_REQ_MSG_ID_V01,
			       TEST_PING_REQ_MAX_MSG_LEN_V01,
			       test_ping_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0)
		count = ret;

	return count;
}

static const struct file_operations ping_fops = {
	.open = simple_open,
	.write = ping_write,
};

static void ping_pong_cb(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			 struct qmi_txn *txn, const void *data)
{
	const struct test_ping_resp_msg_v01 *resp = data;

	if (!txn) {
		pr_err("spurious ping response\n");
		return;
	}

	if (resp->resp.result == QMI_RESULT_FAILURE_V01)
		txn->result = -ENXIO;
	else if (!resp->pong_valid || memcmp(resp->pong, "pong", 4))
		txn->result = -EINVAL;

	complete(&txn->completion);
}

/*
 * data_write() - data debugfs file write handler
 * @file:	debugfs file context
 * @user_buf:	reference to the user data
 * @count:	number of bytes in @user_buf
 * @ppos:	offset in @file to write
 *
 * This function allows user space to send out a data QMI encoded message to
 * the associated remote test service and will return with the result of the
 * transaction. It serves as an example of how to have the QMI helpers decode a
 * transaction response into a provided object automatically.
 *
 * Return: @count, or negative errno on failure.
 */
static ssize_t data_write(struct file *file, const char __user *user_buf,
			  size_t count, loff_t *ppos)

{
	struct qmi_handle *qmi = file->private_data;
	struct test_data_resp_msg_v01 *resp;
	struct test_data_req_msg_v01 *req;
	struct qmi_txn txn;
	int ret;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp) {
		kfree(req);
		return -ENOMEM;
	}

	req->data_len = min_t(size_t, sizeof(req->data), count);
	if (copy_from_user(req->data, user_buf, req->data_len)) {
		ret = -EFAULT;
		goto out;
	}

	ret = qmi_txn_init(qmi, &txn, test_data_resp_msg_v01_ei, resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(qmi, NULL, &txn,
			       TEST_DATA_REQ_MSG_ID_V01,
			       TEST_DATA_REQ_MAX_MSG_LEN_V01,
			       test_data_req_msg_v01_ei, req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0) {
		goto out;
	} else if (!resp->data_valid ||
		   resp->data_len != req->data_len ||
		   memcmp(resp->data, req->data, req->data_len)) {
		pr_err("response data doesn't match expectation\n");
		ret = -EINVAL;
		goto out;
	}

	ret = count;

out:
	kfree(resp);
	kfree(req);

	return ret;
}

static const struct file_operations data_fops = {
	.open = simple_open,
	.write = data_write,
};

static struct qmi_msg_handler qmi_sample_handlers[] = {
	{
		.type = QMI_RESPONSE,
		.msg_id = TEST_PING_REQ_MSG_ID_V01,
		.ei = test_ping_resp_msg_v01_ei,
		.decoded_size = sizeof(struct test_ping_req_msg_v01),
		.fn = ping_pong_cb
	},
	{}
};

struct qmi_sample {
	struct qmi_handle qmi;

	struct dentry *de_dir;
	struct dentry *de_data;
	struct dentry *de_ping;
};

static struct dentry *qmi_debug_dir;

static int qmi_sample_probe(struct platform_device *pdev)
{
	struct sockaddr_qrtr *sq;
	struct qmi_sample *sample;
	char path[20];
	int ret;

	sample = devm_kzalloc(&pdev->dev, sizeof(*sample), GFP_KERNEL);
	if (!sample)
		return -ENOMEM;

	ret = qmi_handle_init(&sample->qmi, TEST_DATA_REQ_MAX_MSG_LEN_V01,
			      NULL,
			      qmi_sample_handlers);
	if (ret < 0)
		return ret;

	sq = dev_get_platdata(&pdev->dev);
	ret = kernel_connect(sample->qmi.sock, (struct sockaddr *)sq,
			     sizeof(*sq), 0);
	if (ret < 0) {
		pr_err("failed to connect to remote service port\n");
		goto err_release_qmi_handle;
	}

	snprintf(path, sizeof(path), "%d:%d", sq->sq_node, sq->sq_port);

	sample->de_dir = debugfs_create_dir(path, qmi_debug_dir);
	if (IS_ERR(sample->de_dir)) {
		ret = PTR_ERR(sample->de_dir);
		goto err_release_qmi_handle;
	}

	sample->de_data = debugfs_create_file("data", 0600, sample->de_dir,
					      sample, &data_fops);
	if (IS_ERR(sample->de_data)) {
		ret = PTR_ERR(sample->de_data);
		goto err_remove_de_dir;
	}

	sample->de_ping = debugfs_create_file("ping", 0600, sample->de_dir,
					      sample, &ping_fops);
	if (IS_ERR(sample->de_ping)) {
		ret = PTR_ERR(sample->de_ping);
		goto err_remove_de_data;
	}

	platform_set_drvdata(pdev, sample);

	return 0;

err_remove_de_data:
	debugfs_remove(sample->de_data);
err_remove_de_dir:
	debugfs_remove(sample->de_dir);
err_release_qmi_handle:
	qmi_handle_release(&sample->qmi);

	return ret;
}

static int qmi_sample_remove(struct platform_device *pdev)
{
	struct qmi_sample *sample = platform_get_drvdata(pdev);

	debugfs_remove(sample->de_ping);
	debugfs_remove(sample->de_data);
	debugfs_remove(sample->de_dir);

	qmi_handle_release(&sample->qmi);

	return 0;
}

static struct platform_driver qmi_sample_driver = {
	.probe = qmi_sample_probe,
	.remove = qmi_sample_remove,
	.driver = {
		.name = "qmi_sample_client",
	},
};

static int qmi_sample_new_server(struct qmi_handle *qmi,
				 struct qmi_service *service)
{
	struct platform_device *pdev;
	struct sockaddr_qrtr sq = { AF_QIPCRTR, service->node, service->port };
	int ret;

	pdev = platform_device_alloc("qmi_sample_client", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	ret = platform_device_add_data(pdev, &sq, sizeof(sq));
	if (ret)
		goto err_put_device;

	ret = platform_device_add(pdev);
	if (ret)
		goto err_put_device;

	service->priv = pdev;

	return 0;

err_put_device:
	platform_device_put(pdev);

	return ret;
}

static void qmi_sample_del_server(struct qmi_handle *qmi,
				  struct qmi_service *service)
{
	struct platform_device *pdev = service->priv;

	platform_device_unregister(pdev);
}

static struct qmi_handle lookup_client;

static struct qmi_ops lookup_ops = {
	.new_server = qmi_sample_new_server,
	.del_server = qmi_sample_del_server,
};

static int qmi_sample_init(void)
{
	int ret;

	qmi_debug_dir = debugfs_create_dir("qmi_sample", NULL);
	if (IS_ERR(qmi_debug_dir)) {
		pr_err("failed to create qmi_sample dir\n");
		return PTR_ERR(qmi_debug_dir);
	}

	ret = platform_driver_register(&qmi_sample_driver);
	if (ret)
		goto err_remove_debug_dir;

	ret = qmi_handle_init(&lookup_client, 0, &lookup_ops, NULL);
	if (ret < 0)
		goto err_unregister_driver;

	qmi_add_lookup(&lookup_client, 15, 0, 0);

	return 0;

err_unregister_driver:
	platform_driver_unregister(&qmi_sample_driver);
err_remove_debug_dir:
	debugfs_remove(qmi_debug_dir);

	return ret;
}

static void qmi_sample_exit(void)
{
	qmi_handle_release(&lookup_client);

	platform_driver_unregister(&qmi_sample_driver);

	debugfs_remove(qmi_debug_dir);
}

module_init(qmi_sample_init);
module_exit(qmi_sample_exit);

MODULE_DESCRIPTION("Sample QMI client driver");
MODULE_LICENSE("GPL v2");
