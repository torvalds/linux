/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *  Copyright (C) 2014 Marvell International Ltd.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci_core.c, which was written
 *  by Maxim Krasnyansky.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>

#include "../nfc.h"
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>
#include <linux/nfc.h>

struct core_conn_create_data {
	int length;
	struct nci_core_conn_create_cmd *cmd;
};

static void nci_cmd_work(struct work_struct *work);
static void nci_rx_work(struct work_struct *work);
static void nci_tx_work(struct work_struct *work);

struct nci_conn_info *nci_get_conn_info_by_conn_id(struct nci_dev *ndev,
						   int conn_id)
{
	struct nci_conn_info *conn_info;

	list_for_each_entry(conn_info, &ndev->conn_info_list, list) {
		if (conn_info->conn_id == conn_id)
			return conn_info;
	}

	return NULL;
}

int nci_get_conn_info_by_id(struct nci_dev *ndev, u8 id)
{
	struct nci_conn_info *conn_info;

	list_for_each_entry(conn_info, &ndev->conn_info_list, list) {
		if (conn_info->id == id)
			return conn_info->conn_id;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(nci_get_conn_info_by_id);

/* ---- NCI requests ---- */

void nci_req_complete(struct nci_dev *ndev, int result)
{
	if (ndev->req_status == NCI_REQ_PEND) {
		ndev->req_result = result;
		ndev->req_status = NCI_REQ_DONE;
		complete(&ndev->req_completion);
	}
}
EXPORT_SYMBOL(nci_req_complete);

static void nci_req_cancel(struct nci_dev *ndev, int err)
{
	if (ndev->req_status == NCI_REQ_PEND) {
		ndev->req_result = err;
		ndev->req_status = NCI_REQ_CANCELED;
		complete(&ndev->req_completion);
	}
}

/* Execute request and wait for completion. */
static int __nci_request(struct nci_dev *ndev,
			 void (*req)(struct nci_dev *ndev, unsigned long opt),
			 unsigned long opt, __u32 timeout)
{
	int rc = 0;
	long completion_rc;

	ndev->req_status = NCI_REQ_PEND;

	reinit_completion(&ndev->req_completion);
	req(ndev, opt);
	completion_rc =
		wait_for_completion_interruptible_timeout(&ndev->req_completion,
							  timeout);

	pr_debug("wait_for_completion return %ld\n", completion_rc);

	if (completion_rc > 0) {
		switch (ndev->req_status) {
		case NCI_REQ_DONE:
			rc = nci_to_errno(ndev->req_result);
			break;

		case NCI_REQ_CANCELED:
			rc = -ndev->req_result;
			break;

		default:
			rc = -ETIMEDOUT;
			break;
		}
	} else {
		pr_err("wait_for_completion_interruptible_timeout failed %ld\n",
		       completion_rc);

		rc = ((completion_rc == 0) ? (-ETIMEDOUT) : (completion_rc));
	}

	ndev->req_status = ndev->req_result = 0;

	return rc;
}

inline int nci_request(struct nci_dev *ndev,
		       void (*req)(struct nci_dev *ndev,
				   unsigned long opt),
		       unsigned long opt, __u32 timeout)
{
	int rc;

	if (!test_bit(NCI_UP, &ndev->flags))
		return -ENETDOWN;

	/* Serialize all requests */
	mutex_lock(&ndev->req_lock);
	rc = __nci_request(ndev, req, opt, timeout);
	mutex_unlock(&ndev->req_lock);

	return rc;
}

static void nci_reset_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_core_reset_cmd cmd;

	cmd.reset_type = NCI_RESET_TYPE_RESET_CONFIG;
	nci_send_cmd(ndev, NCI_OP_CORE_RESET_CMD, 1, &cmd);
}

static void nci_init_req(struct nci_dev *ndev, unsigned long opt)
{
	nci_send_cmd(ndev, NCI_OP_CORE_INIT_CMD, 0, NULL);
}

static void nci_init_complete_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_rf_disc_map_cmd cmd;
	struct disc_map_config *cfg = cmd.mapping_configs;
	__u8 *num = &cmd.num_mapping_configs;
	int i;

	/* set rf mapping configurations */
	*num = 0;

	/* by default mapping is set to NCI_RF_INTERFACE_FRAME */
	for (i = 0; i < ndev->num_supported_rf_interfaces; i++) {
		if (ndev->supported_rf_interfaces[i] ==
		    NCI_RF_INTERFACE_ISO_DEP) {
			cfg[*num].rf_protocol = NCI_RF_PROTOCOL_ISO_DEP;
			cfg[*num].mode = NCI_DISC_MAP_MODE_POLL |
				NCI_DISC_MAP_MODE_LISTEN;
			cfg[*num].rf_interface = NCI_RF_INTERFACE_ISO_DEP;
			(*num)++;
		} else if (ndev->supported_rf_interfaces[i] ==
			   NCI_RF_INTERFACE_NFC_DEP) {
			cfg[*num].rf_protocol = NCI_RF_PROTOCOL_NFC_DEP;
			cfg[*num].mode = NCI_DISC_MAP_MODE_POLL |
				NCI_DISC_MAP_MODE_LISTEN;
			cfg[*num].rf_interface = NCI_RF_INTERFACE_NFC_DEP;
			(*num)++;
		}

		if (*num == NCI_MAX_NUM_MAPPING_CONFIGS)
			break;
	}

	nci_send_cmd(ndev, NCI_OP_RF_DISCOVER_MAP_CMD,
		     (1 + ((*num) * sizeof(struct disc_map_config))), &cmd);
}

struct nci_set_config_param {
	__u8	id;
	size_t	len;
	__u8	*val;
};

static void nci_set_config_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_set_config_param *param = (struct nci_set_config_param *)opt;
	struct nci_core_set_config_cmd cmd;

	BUG_ON(param->len > NCI_MAX_PARAM_LEN);

	cmd.num_params = 1;
	cmd.param.id = param->id;
	cmd.param.len = param->len;
	memcpy(cmd.param.val, param->val, param->len);

	nci_send_cmd(ndev, NCI_OP_CORE_SET_CONFIG_CMD, (3 + param->len), &cmd);
}

struct nci_rf_discover_param {
	__u32	im_protocols;
	__u32	tm_protocols;
};

static void nci_rf_discover_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_rf_discover_param *param =
		(struct nci_rf_discover_param *)opt;
	struct nci_rf_disc_cmd cmd;

	cmd.num_disc_configs = 0;

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS) &&
	    (param->im_protocols & NFC_PROTO_JEWEL_MASK ||
	     param->im_protocols & NFC_PROTO_MIFARE_MASK ||
	     param->im_protocols & NFC_PROTO_ISO14443_MASK ||
	     param->im_protocols & NFC_PROTO_NFC_DEP_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].rf_tech_and_mode =
			NCI_NFC_A_PASSIVE_POLL_MODE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS) &&
	    (param->im_protocols & NFC_PROTO_ISO14443_B_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].rf_tech_and_mode =
			NCI_NFC_B_PASSIVE_POLL_MODE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS) &&
	    (param->im_protocols & NFC_PROTO_FELICA_MASK ||
	     param->im_protocols & NFC_PROTO_NFC_DEP_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].rf_tech_and_mode =
			NCI_NFC_F_PASSIVE_POLL_MODE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS) &&
	    (param->im_protocols & NFC_PROTO_ISO15693_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].rf_tech_and_mode =
			NCI_NFC_V_PASSIVE_POLL_MODE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS - 1) &&
	    (param->tm_protocols & NFC_PROTO_NFC_DEP_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].rf_tech_and_mode =
			NCI_NFC_A_PASSIVE_LISTEN_MODE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
		cmd.disc_configs[cmd.num_disc_configs].rf_tech_and_mode =
			NCI_NFC_F_PASSIVE_LISTEN_MODE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	nci_send_cmd(ndev, NCI_OP_RF_DISCOVER_CMD,
		     (1 + (cmd.num_disc_configs * sizeof(struct disc_config))),
		     &cmd);
}

struct nci_rf_discover_select_param {
	__u8	rf_discovery_id;
	__u8	rf_protocol;
};

static void nci_rf_discover_select_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_rf_discover_select_param *param =
		(struct nci_rf_discover_select_param *)opt;
	struct nci_rf_discover_select_cmd cmd;

	cmd.rf_discovery_id = param->rf_discovery_id;
	cmd.rf_protocol = param->rf_protocol;

	switch (cmd.rf_protocol) {
	case NCI_RF_PROTOCOL_ISO_DEP:
		cmd.rf_interface = NCI_RF_INTERFACE_ISO_DEP;
		break;

	case NCI_RF_PROTOCOL_NFC_DEP:
		cmd.rf_interface = NCI_RF_INTERFACE_NFC_DEP;
		break;

	default:
		cmd.rf_interface = NCI_RF_INTERFACE_FRAME;
		break;
	}

	nci_send_cmd(ndev, NCI_OP_RF_DISCOVER_SELECT_CMD,
		     sizeof(struct nci_rf_discover_select_cmd), &cmd);
}

static void nci_rf_deactivate_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_rf_deactivate_cmd cmd;

	cmd.type = opt;

	nci_send_cmd(ndev, NCI_OP_RF_DEACTIVATE_CMD,
		     sizeof(struct nci_rf_deactivate_cmd), &cmd);
}

struct nci_cmd_param {
	__u16 opcode;
	size_t len;
	__u8 *payload;
};

static void nci_generic_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_cmd_param *param =
		(struct nci_cmd_param *)opt;

	nci_send_cmd(ndev, param->opcode, param->len, param->payload);
}

int nci_prop_cmd(struct nci_dev *ndev, __u8 oid, size_t len, __u8 *payload)
{
	struct nci_cmd_param param;

	param.opcode = nci_opcode_pack(NCI_GID_PROPRIETARY, oid);
	param.len = len;
	param.payload = payload;

	return __nci_request(ndev, nci_generic_req, (unsigned long)&param,
			     msecs_to_jiffies(NCI_CMD_TIMEOUT));
}
EXPORT_SYMBOL(nci_prop_cmd);

int nci_core_cmd(struct nci_dev *ndev, __u16 opcode, size_t len, __u8 *payload)
{
	struct nci_cmd_param param;

	param.opcode = opcode;
	param.len = len;
	param.payload = payload;

	return __nci_request(ndev, nci_generic_req, (unsigned long)&param,
			     msecs_to_jiffies(NCI_CMD_TIMEOUT));
}
EXPORT_SYMBOL(nci_core_cmd);

int nci_core_reset(struct nci_dev *ndev)
{
	return __nci_request(ndev, nci_reset_req, 0,
			     msecs_to_jiffies(NCI_RESET_TIMEOUT));
}
EXPORT_SYMBOL(nci_core_reset);

int nci_core_init(struct nci_dev *ndev)
{
	return __nci_request(ndev, nci_init_req, 0,
			     msecs_to_jiffies(NCI_INIT_TIMEOUT));
}
EXPORT_SYMBOL(nci_core_init);

static int nci_open_device(struct nci_dev *ndev)
{
	int rc = 0;

	mutex_lock(&ndev->req_lock);

	if (test_bit(NCI_UP, &ndev->flags)) {
		rc = -EALREADY;
		goto done;
	}

	if (ndev->ops->open(ndev)) {
		rc = -EIO;
		goto done;
	}

	atomic_set(&ndev->cmd_cnt, 1);

	set_bit(NCI_INIT, &ndev->flags);

	if (ndev->ops->init)
		rc = ndev->ops->init(ndev);

	if (!rc) {
		rc = __nci_request(ndev, nci_reset_req, 0,
				   msecs_to_jiffies(NCI_RESET_TIMEOUT));
	}

	if (!rc && ndev->ops->setup) {
		rc = ndev->ops->setup(ndev);
	}

	if (!rc) {
		rc = __nci_request(ndev, nci_init_req, 0,
				   msecs_to_jiffies(NCI_INIT_TIMEOUT));
	}

	if (!rc && ndev->ops->post_setup)
		rc = ndev->ops->post_setup(ndev);

	if (!rc) {
		rc = __nci_request(ndev, nci_init_complete_req, 0,
				   msecs_to_jiffies(NCI_INIT_TIMEOUT));
	}

	clear_bit(NCI_INIT, &ndev->flags);

	if (!rc) {
		set_bit(NCI_UP, &ndev->flags);
		nci_clear_target_list(ndev);
		atomic_set(&ndev->state, NCI_IDLE);
	} else {
		/* Init failed, cleanup */
		skb_queue_purge(&ndev->cmd_q);
		skb_queue_purge(&ndev->rx_q);
		skb_queue_purge(&ndev->tx_q);

		ndev->ops->close(ndev);
		ndev->flags = 0;
	}

done:
	mutex_unlock(&ndev->req_lock);
	return rc;
}

static int nci_close_device(struct nci_dev *ndev)
{
	nci_req_cancel(ndev, ENODEV);
	mutex_lock(&ndev->req_lock);

	if (!test_and_clear_bit(NCI_UP, &ndev->flags)) {
		del_timer_sync(&ndev->cmd_timer);
		del_timer_sync(&ndev->data_timer);
		mutex_unlock(&ndev->req_lock);
		return 0;
	}

	/* Drop RX and TX queues */
	skb_queue_purge(&ndev->rx_q);
	skb_queue_purge(&ndev->tx_q);

	/* Flush RX and TX wq */
	flush_workqueue(ndev->rx_wq);
	flush_workqueue(ndev->tx_wq);

	/* Reset device */
	skb_queue_purge(&ndev->cmd_q);
	atomic_set(&ndev->cmd_cnt, 1);

	set_bit(NCI_INIT, &ndev->flags);
	__nci_request(ndev, nci_reset_req, 0,
		      msecs_to_jiffies(NCI_RESET_TIMEOUT));

	/* After this point our queues are empty
	 * and no works are scheduled.
	 */
	ndev->ops->close(ndev);

	clear_bit(NCI_INIT, &ndev->flags);

	del_timer_sync(&ndev->cmd_timer);

	/* Flush cmd wq */
	flush_workqueue(ndev->cmd_wq);

	/* Clear flags */
	ndev->flags = 0;

	mutex_unlock(&ndev->req_lock);

	return 0;
}

/* NCI command timer function */
static void nci_cmd_timer(unsigned long arg)
{
	struct nci_dev *ndev = (void *) arg;

	atomic_set(&ndev->cmd_cnt, 1);
	queue_work(ndev->cmd_wq, &ndev->cmd_work);
}

/* NCI data exchange timer function */
static void nci_data_timer(unsigned long arg)
{
	struct nci_dev *ndev = (void *) arg;

	set_bit(NCI_DATA_EXCHANGE_TO, &ndev->flags);
	queue_work(ndev->rx_wq, &ndev->rx_work);
}

static int nci_dev_up(struct nfc_dev *nfc_dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	return nci_open_device(ndev);
}

static int nci_dev_down(struct nfc_dev *nfc_dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	return nci_close_device(ndev);
}

int nci_set_config(struct nci_dev *ndev, __u8 id, size_t len, __u8 *val)
{
	struct nci_set_config_param param;

	if (!val || !len)
		return 0;

	param.id = id;
	param.len = len;
	param.val = val;

	return __nci_request(ndev, nci_set_config_req, (unsigned long)&param,
			     msecs_to_jiffies(NCI_SET_CONFIG_TIMEOUT));
}
EXPORT_SYMBOL(nci_set_config);

static void nci_nfcee_discover_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_nfcee_discover_cmd cmd;
	__u8 action = opt;

	cmd.discovery_action = action;

	nci_send_cmd(ndev, NCI_OP_NFCEE_DISCOVER_CMD, 1, &cmd);
}

int nci_nfcee_discover(struct nci_dev *ndev, u8 action)
{
	return __nci_request(ndev, nci_nfcee_discover_req, action,
				msecs_to_jiffies(NCI_CMD_TIMEOUT));
}
EXPORT_SYMBOL(nci_nfcee_discover);

static void nci_nfcee_mode_set_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_nfcee_mode_set_cmd *cmd =
					(struct nci_nfcee_mode_set_cmd *)opt;

	nci_send_cmd(ndev, NCI_OP_NFCEE_MODE_SET_CMD,
		     sizeof(struct nci_nfcee_mode_set_cmd), cmd);
}

int nci_nfcee_mode_set(struct nci_dev *ndev, u8 nfcee_id, u8 nfcee_mode)
{
	struct nci_nfcee_mode_set_cmd cmd;

	cmd.nfcee_id = nfcee_id;
	cmd.nfcee_mode = nfcee_mode;

	return __nci_request(ndev, nci_nfcee_mode_set_req,
			     (unsigned long)&cmd,
			     msecs_to_jiffies(NCI_CMD_TIMEOUT));
}
EXPORT_SYMBOL(nci_nfcee_mode_set);

static void nci_core_conn_create_req(struct nci_dev *ndev, unsigned long opt)
{
	struct core_conn_create_data *data =
					(struct core_conn_create_data *)opt;

	nci_send_cmd(ndev, NCI_OP_CORE_CONN_CREATE_CMD, data->length, data->cmd);
}

int nci_core_conn_create(struct nci_dev *ndev, u8 destination_type,
			 u8 number_destination_params,
			 size_t params_len,
			 struct core_conn_create_dest_spec_params *params)
{
	int r;
	struct nci_core_conn_create_cmd *cmd;
	struct core_conn_create_data data;

	data.length = params_len + sizeof(struct nci_core_conn_create_cmd);
	cmd = kzalloc(data.length, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->destination_type = destination_type;
	cmd->number_destination_params = number_destination_params;

	data.cmd = cmd;

	if (params) {
		memcpy(cmd->params, params, params_len);
		if (params->length > 0)
			ndev->cur_id = params->value[DEST_SPEC_PARAMS_ID_INDEX];
		else
			ndev->cur_id = 0;
	} else {
		ndev->cur_id = 0;
	}

	r = __nci_request(ndev, nci_core_conn_create_req, (unsigned long)&data,
			  msecs_to_jiffies(NCI_CMD_TIMEOUT));
	kfree(cmd);
	return r;
}
EXPORT_SYMBOL(nci_core_conn_create);

static void nci_core_conn_close_req(struct nci_dev *ndev, unsigned long opt)
{
	__u8 conn_id = opt;

	nci_send_cmd(ndev, NCI_OP_CORE_CONN_CLOSE_CMD, 1, &conn_id);
}

int nci_core_conn_close(struct nci_dev *ndev, u8 conn_id)
{
	return __nci_request(ndev, nci_core_conn_close_req, conn_id,
			     msecs_to_jiffies(NCI_CMD_TIMEOUT));
}
EXPORT_SYMBOL(nci_core_conn_close);

static int nci_set_local_general_bytes(struct nfc_dev *nfc_dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	struct nci_set_config_param param;
	int rc;

	param.val = nfc_get_local_general_bytes(nfc_dev, &param.len);
	if ((param.val == NULL) || (param.len == 0))
		return 0;

	if (param.len > NFC_MAX_GT_LEN)
		return -EINVAL;

	param.id = NCI_PN_ATR_REQ_GEN_BYTES;

	rc = nci_request(ndev, nci_set_config_req, (unsigned long)&param,
			 msecs_to_jiffies(NCI_SET_CONFIG_TIMEOUT));
	if (rc)
		return rc;

	param.id = NCI_LN_ATR_RES_GEN_BYTES;

	return nci_request(ndev, nci_set_config_req, (unsigned long)&param,
			   msecs_to_jiffies(NCI_SET_CONFIG_TIMEOUT));
}

static int nci_set_listen_parameters(struct nfc_dev *nfc_dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	int rc;
	__u8 val;

	val = NCI_LA_SEL_INFO_NFC_DEP_MASK;

	rc = nci_set_config(ndev, NCI_LA_SEL_INFO, 1, &val);
	if (rc)
		return rc;

	val = NCI_LF_PROTOCOL_TYPE_NFC_DEP_MASK;

	rc = nci_set_config(ndev, NCI_LF_PROTOCOL_TYPE, 1, &val);
	if (rc)
		return rc;

	val = NCI_LF_CON_BITR_F_212 | NCI_LF_CON_BITR_F_424;

	return nci_set_config(ndev, NCI_LF_CON_BITR_F, 1, &val);
}

static int nci_start_poll(struct nfc_dev *nfc_dev,
			  __u32 im_protocols, __u32 tm_protocols)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	struct nci_rf_discover_param param;
	int rc;

	if ((atomic_read(&ndev->state) == NCI_DISCOVERY) ||
	    (atomic_read(&ndev->state) == NCI_W4_ALL_DISCOVERIES)) {
		pr_err("unable to start poll, since poll is already active\n");
		return -EBUSY;
	}

	if (ndev->target_active_prot) {
		pr_err("there is an active target\n");
		return -EBUSY;
	}

	if ((atomic_read(&ndev->state) == NCI_W4_HOST_SELECT) ||
	    (atomic_read(&ndev->state) == NCI_POLL_ACTIVE)) {
		pr_debug("target active or w4 select, implicitly deactivate\n");

		rc = nci_request(ndev, nci_rf_deactivate_req,
				 NCI_DEACTIVATE_TYPE_IDLE_MODE,
				 msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
		if (rc)
			return -EBUSY;
	}

	if ((im_protocols | tm_protocols) & NFC_PROTO_NFC_DEP_MASK) {
		rc = nci_set_local_general_bytes(nfc_dev);
		if (rc) {
			pr_err("failed to set local general bytes\n");
			return rc;
		}
	}

	if (tm_protocols & NFC_PROTO_NFC_DEP_MASK) {
		rc = nci_set_listen_parameters(nfc_dev);
		if (rc)
			pr_err("failed to set listen parameters\n");
	}

	param.im_protocols = im_protocols;
	param.tm_protocols = tm_protocols;
	rc = nci_request(ndev, nci_rf_discover_req, (unsigned long)&param,
			 msecs_to_jiffies(NCI_RF_DISC_TIMEOUT));

	if (!rc)
		ndev->poll_prots = im_protocols;

	return rc;
}

static void nci_stop_poll(struct nfc_dev *nfc_dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	if ((atomic_read(&ndev->state) != NCI_DISCOVERY) &&
	    (atomic_read(&ndev->state) != NCI_W4_ALL_DISCOVERIES)) {
		pr_err("unable to stop poll, since poll is not active\n");
		return;
	}

	nci_request(ndev, nci_rf_deactivate_req, NCI_DEACTIVATE_TYPE_IDLE_MODE,
		    msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
}

static int nci_activate_target(struct nfc_dev *nfc_dev,
			       struct nfc_target *target, __u32 protocol)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	struct nci_rf_discover_select_param param;
	struct nfc_target *nci_target = NULL;
	int i;
	int rc = 0;

	pr_debug("target_idx %d, protocol 0x%x\n", target->idx, protocol);

	if ((atomic_read(&ndev->state) != NCI_W4_HOST_SELECT) &&
	    (atomic_read(&ndev->state) != NCI_POLL_ACTIVE)) {
		pr_err("there is no available target to activate\n");
		return -EINVAL;
	}

	if (ndev->target_active_prot) {
		pr_err("there is already an active target\n");
		return -EBUSY;
	}

	for (i = 0; i < ndev->n_targets; i++) {
		if (ndev->targets[i].idx == target->idx) {
			nci_target = &ndev->targets[i];
			break;
		}
	}

	if (!nci_target) {
		pr_err("unable to find the selected target\n");
		return -EINVAL;
	}

	if (!(nci_target->supported_protocols & (1 << protocol))) {
		pr_err("target does not support the requested protocol 0x%x\n",
		       protocol);
		return -EINVAL;
	}

	if (atomic_read(&ndev->state) == NCI_W4_HOST_SELECT) {
		param.rf_discovery_id = nci_target->logical_idx;

		if (protocol == NFC_PROTO_JEWEL)
			param.rf_protocol = NCI_RF_PROTOCOL_T1T;
		else if (protocol == NFC_PROTO_MIFARE)
			param.rf_protocol = NCI_RF_PROTOCOL_T2T;
		else if (protocol == NFC_PROTO_FELICA)
			param.rf_protocol = NCI_RF_PROTOCOL_T3T;
		else if (protocol == NFC_PROTO_ISO14443 ||
			 protocol == NFC_PROTO_ISO14443_B)
			param.rf_protocol = NCI_RF_PROTOCOL_ISO_DEP;
		else
			param.rf_protocol = NCI_RF_PROTOCOL_NFC_DEP;

		rc = nci_request(ndev, nci_rf_discover_select_req,
				 (unsigned long)&param,
				 msecs_to_jiffies(NCI_RF_DISC_SELECT_TIMEOUT));
	}

	if (!rc)
		ndev->target_active_prot = protocol;

	return rc;
}

static void nci_deactivate_target(struct nfc_dev *nfc_dev,
				  struct nfc_target *target,
				  __u8 mode)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	u8 nci_mode = NCI_DEACTIVATE_TYPE_IDLE_MODE;

	pr_debug("entry\n");

	if (!ndev->target_active_prot) {
		pr_err("unable to deactivate target, no active target\n");
		return;
	}

	ndev->target_active_prot = 0;

	switch (mode) {
	case NFC_TARGET_MODE_SLEEP:
		nci_mode = NCI_DEACTIVATE_TYPE_SLEEP_MODE;
		break;
	}

	if (atomic_read(&ndev->state) == NCI_POLL_ACTIVE) {
		nci_request(ndev, nci_rf_deactivate_req, nci_mode,
			    msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
	}
}

static int nci_dep_link_up(struct nfc_dev *nfc_dev, struct nfc_target *target,
			   __u8 comm_mode, __u8 *gb, size_t gb_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	int rc;

	pr_debug("target_idx %d, comm_mode %d\n", target->idx, comm_mode);

	rc = nci_activate_target(nfc_dev, target, NFC_PROTO_NFC_DEP);
	if (rc)
		return rc;

	rc = nfc_set_remote_general_bytes(nfc_dev, ndev->remote_gb,
					  ndev->remote_gb_len);
	if (!rc)
		rc = nfc_dep_link_is_up(nfc_dev, target->idx, NFC_COMM_PASSIVE,
					NFC_RF_INITIATOR);

	return rc;
}

static int nci_dep_link_down(struct nfc_dev *nfc_dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	int rc;

	pr_debug("entry\n");

	if (nfc_dev->rf_mode == NFC_RF_INITIATOR) {
		nci_deactivate_target(nfc_dev, NULL, NCI_DEACTIVATE_TYPE_IDLE_MODE);
	} else {
		if (atomic_read(&ndev->state) == NCI_LISTEN_ACTIVE ||
		    atomic_read(&ndev->state) == NCI_DISCOVERY) {
			nci_request(ndev, nci_rf_deactivate_req, 0,
				msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
		}

		rc = nfc_tm_deactivated(nfc_dev);
		if (rc)
			pr_err("error when signaling tm deactivation\n");
	}

	return 0;
}


static int nci_transceive(struct nfc_dev *nfc_dev, struct nfc_target *target,
			  struct sk_buff *skb,
			  data_exchange_cb_t cb, void *cb_context)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	int rc;
	struct nci_conn_info    *conn_info;

	conn_info = ndev->rf_conn_info;
	if (!conn_info)
		return -EPROTO;

	pr_debug("target_idx %d, len %d\n", target->idx, skb->len);

	if (!ndev->target_active_prot) {
		pr_err("unable to exchange data, no active target\n");
		return -EINVAL;
	}

	if (test_and_set_bit(NCI_DATA_EXCHANGE, &ndev->flags))
		return -EBUSY;

	/* store cb and context to be used on receiving data */
	conn_info->data_exchange_cb = cb;
	conn_info->data_exchange_cb_context = cb_context;

	rc = nci_send_data(ndev, NCI_STATIC_RF_CONN_ID, skb);
	if (rc)
		clear_bit(NCI_DATA_EXCHANGE, &ndev->flags);

	return rc;
}

static int nci_tm_send(struct nfc_dev *nfc_dev, struct sk_buff *skb)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	int rc;

	rc = nci_send_data(ndev, NCI_STATIC_RF_CONN_ID, skb);
	if (rc)
		pr_err("unable to send data\n");

	return rc;
}

static int nci_enable_se(struct nfc_dev *nfc_dev, u32 se_idx)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	if (ndev->ops->enable_se)
		return ndev->ops->enable_se(ndev, se_idx);

	return 0;
}

static int nci_disable_se(struct nfc_dev *nfc_dev, u32 se_idx)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	if (ndev->ops->disable_se)
		return ndev->ops->disable_se(ndev, se_idx);

	return 0;
}

static int nci_discover_se(struct nfc_dev *nfc_dev)
{
	int r;
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	if (ndev->ops->discover_se) {
		r = nci_nfcee_discover(ndev, NCI_NFCEE_DISCOVERY_ACTION_ENABLE);
		if (r != NCI_STATUS_OK)
			return -EPROTO;

		return ndev->ops->discover_se(ndev);
	}

	return 0;
}

static int nci_se_io(struct nfc_dev *nfc_dev, u32 se_idx,
		     u8 *apdu, size_t apdu_length,
		     se_io_cb_t cb, void *cb_context)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	if (ndev->ops->se_io)
		return ndev->ops->se_io(ndev, se_idx, apdu,
				apdu_length, cb, cb_context);

	return 0;
}

static int nci_fw_download(struct nfc_dev *nfc_dev, const char *firmware_name)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	if (!ndev->ops->fw_download)
		return -ENOTSUPP;

	return ndev->ops->fw_download(ndev, firmware_name);
}

static struct nfc_ops nci_nfc_ops = {
	.dev_up = nci_dev_up,
	.dev_down = nci_dev_down,
	.start_poll = nci_start_poll,
	.stop_poll = nci_stop_poll,
	.dep_link_up = nci_dep_link_up,
	.dep_link_down = nci_dep_link_down,
	.activate_target = nci_activate_target,
	.deactivate_target = nci_deactivate_target,
	.im_transceive = nci_transceive,
	.tm_send = nci_tm_send,
	.enable_se = nci_enable_se,
	.disable_se = nci_disable_se,
	.discover_se = nci_discover_se,
	.se_io = nci_se_io,
	.fw_download = nci_fw_download,
};

/* ---- Interface to NCI drivers ---- */
/**
 * nci_allocate_device - allocate a new nci device
 *
 * @ops: device operations
 * @supported_protocols: NFC protocols supported by the device
 */
struct nci_dev *nci_allocate_device(struct nci_ops *ops,
				    __u32 supported_protocols,
				    int tx_headroom, int tx_tailroom)
{
	struct nci_dev *ndev;

	pr_debug("supported_protocols 0x%x\n", supported_protocols);

	if (!ops->open || !ops->close || !ops->send)
		return NULL;

	if (!supported_protocols)
		return NULL;

	ndev = kzalloc(sizeof(struct nci_dev), GFP_KERNEL);
	if (!ndev)
		return NULL;

	ndev->ops = ops;

	if (ops->n_prop_ops > NCI_MAX_PROPRIETARY_CMD) {
		pr_err("Too many proprietary commands: %zd\n",
		       ops->n_prop_ops);
		ops->prop_ops = NULL;
		ops->n_prop_ops = 0;
	}

	ndev->tx_headroom = tx_headroom;
	ndev->tx_tailroom = tx_tailroom;
	init_completion(&ndev->req_completion);

	ndev->nfc_dev = nfc_allocate_device(&nci_nfc_ops,
					    supported_protocols,
					    tx_headroom + NCI_DATA_HDR_SIZE,
					    tx_tailroom);
	if (!ndev->nfc_dev)
		goto free_nci;

	ndev->hci_dev = nci_hci_allocate(ndev);
	if (!ndev->hci_dev)
		goto free_nfc;

	nfc_set_drvdata(ndev->nfc_dev, ndev);

	return ndev;

free_nfc:
	kfree(ndev->nfc_dev);

free_nci:
	kfree(ndev);
	return NULL;
}
EXPORT_SYMBOL(nci_allocate_device);

/**
 * nci_free_device - deallocate nci device
 *
 * @ndev: The nci device to deallocate
 */
void nci_free_device(struct nci_dev *ndev)
{
	nfc_free_device(ndev->nfc_dev);
	kfree(ndev);
}
EXPORT_SYMBOL(nci_free_device);

/**
 * nci_register_device - register a nci device in the nfc subsystem
 *
 * @dev: The nci device to register
 */
int nci_register_device(struct nci_dev *ndev)
{
	int rc;
	struct device *dev = &ndev->nfc_dev->dev;
	char name[32];

	ndev->flags = 0;

	INIT_WORK(&ndev->cmd_work, nci_cmd_work);
	snprintf(name, sizeof(name), "%s_nci_cmd_wq", dev_name(dev));
	ndev->cmd_wq = create_singlethread_workqueue(name);
	if (!ndev->cmd_wq) {
		rc = -ENOMEM;
		goto exit;
	}

	INIT_WORK(&ndev->rx_work, nci_rx_work);
	snprintf(name, sizeof(name), "%s_nci_rx_wq", dev_name(dev));
	ndev->rx_wq = create_singlethread_workqueue(name);
	if (!ndev->rx_wq) {
		rc = -ENOMEM;
		goto destroy_cmd_wq_exit;
	}

	INIT_WORK(&ndev->tx_work, nci_tx_work);
	snprintf(name, sizeof(name), "%s_nci_tx_wq", dev_name(dev));
	ndev->tx_wq = create_singlethread_workqueue(name);
	if (!ndev->tx_wq) {
		rc = -ENOMEM;
		goto destroy_rx_wq_exit;
	}

	skb_queue_head_init(&ndev->cmd_q);
	skb_queue_head_init(&ndev->rx_q);
	skb_queue_head_init(&ndev->tx_q);

	setup_timer(&ndev->cmd_timer, nci_cmd_timer,
		    (unsigned long) ndev);
	setup_timer(&ndev->data_timer, nci_data_timer,
		    (unsigned long) ndev);

	mutex_init(&ndev->req_lock);
	INIT_LIST_HEAD(&ndev->conn_info_list);

	rc = nfc_register_device(ndev->nfc_dev);
	if (rc)
		goto destroy_rx_wq_exit;

	goto exit;

destroy_rx_wq_exit:
	destroy_workqueue(ndev->rx_wq);

destroy_cmd_wq_exit:
	destroy_workqueue(ndev->cmd_wq);

exit:
	return rc;
}
EXPORT_SYMBOL(nci_register_device);

/**
 * nci_unregister_device - unregister a nci device in the nfc subsystem
 *
 * @dev: The nci device to unregister
 */
void nci_unregister_device(struct nci_dev *ndev)
{
	struct nci_conn_info    *conn_info, *n;

	nci_close_device(ndev);

	destroy_workqueue(ndev->cmd_wq);
	destroy_workqueue(ndev->rx_wq);
	destroy_workqueue(ndev->tx_wq);

	list_for_each_entry_safe(conn_info, n, &ndev->conn_info_list, list) {
		list_del(&conn_info->list);
		/* conn_info is allocated with devm_kzalloc */
	}

	nfc_unregister_device(ndev->nfc_dev);
}
EXPORT_SYMBOL(nci_unregister_device);

/**
 * nci_recv_frame - receive frame from NCI drivers
 *
 * @ndev: The nci device
 * @skb: The sk_buff to receive
 */
int nci_recv_frame(struct nci_dev *ndev, struct sk_buff *skb)
{
	pr_debug("len %d\n", skb->len);

	if (!ndev || (!test_bit(NCI_UP, &ndev->flags) &&
	    !test_bit(NCI_INIT, &ndev->flags))) {
		kfree_skb(skb);
		return -ENXIO;
	}

	/* Queue frame for rx worker thread */
	skb_queue_tail(&ndev->rx_q, skb);
	queue_work(ndev->rx_wq, &ndev->rx_work);

	return 0;
}
EXPORT_SYMBOL(nci_recv_frame);

int nci_send_frame(struct nci_dev *ndev, struct sk_buff *skb)
{
	pr_debug("len %d\n", skb->len);

	if (!ndev) {
		kfree_skb(skb);
		return -ENODEV;
	}

	/* Get rid of skb owner, prior to sending to the driver. */
	skb_orphan(skb);

	/* Send copy to sniffer */
	nfc_send_to_raw_sock(ndev->nfc_dev, skb,
			     RAW_PAYLOAD_NCI, NFC_DIRECTION_TX);

	return ndev->ops->send(ndev, skb);
}
EXPORT_SYMBOL(nci_send_frame);

/* Send NCI command */
int nci_send_cmd(struct nci_dev *ndev, __u16 opcode, __u8 plen, void *payload)
{
	struct nci_ctrl_hdr *hdr;
	struct sk_buff *skb;

	pr_debug("opcode 0x%x, plen %d\n", opcode, plen);

	skb = nci_skb_alloc(ndev, (NCI_CTRL_HDR_SIZE + plen), GFP_KERNEL);
	if (!skb) {
		pr_err("no memory for command\n");
		return -ENOMEM;
	}

	hdr = (struct nci_ctrl_hdr *) skb_put(skb, NCI_CTRL_HDR_SIZE);
	hdr->gid = nci_opcode_gid(opcode);
	hdr->oid = nci_opcode_oid(opcode);
	hdr->plen = plen;

	nci_mt_set((__u8 *)hdr, NCI_MT_CMD_PKT);
	nci_pbf_set((__u8 *)hdr, NCI_PBF_LAST);

	if (plen)
		memcpy(skb_put(skb, plen), payload, plen);

	skb_queue_tail(&ndev->cmd_q, skb);
	queue_work(ndev->cmd_wq, &ndev->cmd_work);

	return 0;
}
EXPORT_SYMBOL(nci_send_cmd);

/* Proprietary commands API */
static struct nci_driver_ops *ops_cmd_lookup(struct nci_driver_ops *ops,
					     size_t n_ops,
					     __u16 opcode)
{
	size_t i;
	struct nci_driver_ops *op;

	if (!ops || !n_ops)
		return NULL;

	for (i = 0; i < n_ops; i++) {
		op = &ops[i];
		if (op->opcode == opcode)
			return op;
	}

	return NULL;
}

static int nci_op_rsp_packet(struct nci_dev *ndev, __u16 rsp_opcode,
			     struct sk_buff *skb, struct nci_driver_ops *ops,
			     size_t n_ops)
{
	struct nci_driver_ops *op;

	op = ops_cmd_lookup(ops, n_ops, rsp_opcode);
	if (!op || !op->rsp)
		return -ENOTSUPP;

	return op->rsp(ndev, skb);
}

static int nci_op_ntf_packet(struct nci_dev *ndev, __u16 ntf_opcode,
			     struct sk_buff *skb, struct nci_driver_ops *ops,
			     size_t n_ops)
{
	struct nci_driver_ops *op;

	op = ops_cmd_lookup(ops, n_ops, ntf_opcode);
	if (!op || !op->ntf)
		return -ENOTSUPP;

	return op->ntf(ndev, skb);
}

int nci_prop_rsp_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb)
{
	return nci_op_rsp_packet(ndev, opcode, skb, ndev->ops->prop_ops,
				 ndev->ops->n_prop_ops);
}

int nci_prop_ntf_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb)
{
	return nci_op_ntf_packet(ndev, opcode, skb, ndev->ops->prop_ops,
				 ndev->ops->n_prop_ops);
}

int nci_core_rsp_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb)
{
	return nci_op_rsp_packet(ndev, opcode, skb, ndev->ops->core_ops,
				  ndev->ops->n_core_ops);
}

int nci_core_ntf_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb)
{
	return nci_op_ntf_packet(ndev, opcode, skb, ndev->ops->core_ops,
				 ndev->ops->n_core_ops);
}

/* ---- NCI TX Data worker thread ---- */

static void nci_tx_work(struct work_struct *work)
{
	struct nci_dev *ndev = container_of(work, struct nci_dev, tx_work);
	struct nci_conn_info    *conn_info;
	struct sk_buff *skb;

	conn_info = nci_get_conn_info_by_conn_id(ndev, ndev->cur_conn_id);
	if (!conn_info)
		return;

	pr_debug("credits_cnt %d\n", atomic_read(&conn_info->credits_cnt));

	/* Send queued tx data */
	while (atomic_read(&conn_info->credits_cnt)) {
		skb = skb_dequeue(&ndev->tx_q);
		if (!skb)
			return;

		/* Check if data flow control is used */
		if (atomic_read(&conn_info->credits_cnt) !=
		    NCI_DATA_FLOW_CONTROL_NOT_USED)
			atomic_dec(&conn_info->credits_cnt);

		pr_debug("NCI TX: MT=data, PBF=%d, conn_id=%d, plen=%d\n",
			 nci_pbf(skb->data),
			 nci_conn_id(skb->data),
			 nci_plen(skb->data));

		nci_send_frame(ndev, skb);

		mod_timer(&ndev->data_timer,
			  jiffies + msecs_to_jiffies(NCI_DATA_TIMEOUT));
	}
}

/* ----- NCI RX worker thread (data & control) ----- */

static void nci_rx_work(struct work_struct *work)
{
	struct nci_dev *ndev = container_of(work, struct nci_dev, rx_work);
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ndev->rx_q))) {

		/* Send copy to sniffer */
		nfc_send_to_raw_sock(ndev->nfc_dev, skb,
				     RAW_PAYLOAD_NCI, NFC_DIRECTION_RX);

		/* Process frame */
		switch (nci_mt(skb->data)) {
		case NCI_MT_RSP_PKT:
			nci_rsp_packet(ndev, skb);
			break;

		case NCI_MT_NTF_PKT:
			nci_ntf_packet(ndev, skb);
			break;

		case NCI_MT_DATA_PKT:
			nci_rx_data_packet(ndev, skb);
			break;

		default:
			pr_err("unknown MT 0x%x\n", nci_mt(skb->data));
			kfree_skb(skb);
			break;
		}
	}

	/* check if a data exchange timout has occurred */
	if (test_bit(NCI_DATA_EXCHANGE_TO, &ndev->flags)) {
		/* complete the data exchange transaction, if exists */
		if (test_bit(NCI_DATA_EXCHANGE, &ndev->flags))
			nci_data_exchange_complete(ndev, NULL,
						   ndev->cur_conn_id,
						   -ETIMEDOUT);

		clear_bit(NCI_DATA_EXCHANGE_TO, &ndev->flags);
	}
}

/* ----- NCI TX CMD worker thread ----- */

static void nci_cmd_work(struct work_struct *work)
{
	struct nci_dev *ndev = container_of(work, struct nci_dev, cmd_work);
	struct sk_buff *skb;

	pr_debug("cmd_cnt %d\n", atomic_read(&ndev->cmd_cnt));

	/* Send queued command */
	if (atomic_read(&ndev->cmd_cnt)) {
		skb = skb_dequeue(&ndev->cmd_q);
		if (!skb)
			return;

		atomic_dec(&ndev->cmd_cnt);

		pr_debug("NCI TX: MT=cmd, PBF=%d, GID=0x%x, OID=0x%x, plen=%d\n",
			 nci_pbf(skb->data),
			 nci_opcode_gid(nci_opcode(skb->data)),
			 nci_opcode_oid(nci_opcode(skb->data)),
			 nci_plen(skb->data));

		nci_send_frame(ndev, skb);

		mod_timer(&ndev->cmd_timer,
			  jiffies + msecs_to_jiffies(NCI_CMD_TIMEOUT));
	}
}

MODULE_LICENSE("GPL");
