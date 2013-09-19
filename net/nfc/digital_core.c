/*
 * NFC Digital Protocol stack
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>

#include "digital.h"

#define DIGITAL_PROTO_NFCA_RF_TECH \
	(NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK | NFC_PROTO_NFC_DEP_MASK)

#define DIGITAL_PROTO_NFCF_RF_TECH \
	(NFC_PROTO_FELICA_MASK | NFC_PROTO_NFC_DEP_MASK)

struct digital_cmd {
	struct list_head queue;

	u8 type;
	u8 pending;

	u16 timeout;
	struct sk_buff *req;
	struct sk_buff *resp;
	struct digital_tg_mdaa_params *mdaa_params;

	nfc_digital_cmd_complete_t cmd_cb;
	void *cb_context;
};

struct sk_buff *digital_skb_alloc(struct nfc_digital_dev *ddev,
				  unsigned int len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len + ddev->tx_headroom + ddev->tx_tailroom,
			GFP_KERNEL);
	if (skb)
		skb_reserve(skb, ddev->tx_headroom);

	return skb;
}

void digital_skb_add_crc(struct sk_buff *skb, crc_func_t crc_func, u16 init,
			 u8 bitwise_inv, u8 msb_first)
{
	u16 crc;

	crc = crc_func(init, skb->data, skb->len);

	if (bitwise_inv)
		crc = ~crc;

	if (msb_first)
		crc = __fswab16(crc);

	*skb_put(skb, 1) = crc & 0xFF;
	*skb_put(skb, 1) = (crc >> 8) & 0xFF;
}

int digital_skb_check_crc(struct sk_buff *skb, crc_func_t crc_func,
			  u16 crc_init, u8 bitwise_inv, u8 msb_first)
{
	int rc;
	u16 crc;

	if (skb->len <= 2)
		return -EIO;

	crc = crc_func(crc_init, skb->data, skb->len - 2);

	if (bitwise_inv)
		crc = ~crc;

	if (msb_first)
		crc = __swab16(crc);

	rc = (skb->data[skb->len - 2] - (crc & 0xFF)) +
	     (skb->data[skb->len - 1] - ((crc >> 8) & 0xFF));

	if (rc)
		return -EIO;

	skb_trim(skb, skb->len - 2);

	return 0;
}

static inline void digital_switch_rf(struct nfc_digital_dev *ddev, bool on)
{
	ddev->ops->switch_rf(ddev, on);
}

static inline void digital_abort_cmd(struct nfc_digital_dev *ddev)
{
	ddev->ops->abort_cmd(ddev);
}

static void digital_wq_cmd_complete(struct work_struct *work)
{
	struct digital_cmd *cmd;
	struct nfc_digital_dev *ddev = container_of(work,
						    struct nfc_digital_dev,
						    cmd_complete_work);

	mutex_lock(&ddev->cmd_lock);

	cmd = list_first_entry_or_null(&ddev->cmd_queue, struct digital_cmd,
				       queue);
	if (!cmd) {
		mutex_unlock(&ddev->cmd_lock);
		return;
	}

	list_del(&cmd->queue);

	mutex_unlock(&ddev->cmd_lock);

	if (!IS_ERR(cmd->resp))
		print_hex_dump_debug("DIGITAL RX: ", DUMP_PREFIX_NONE, 16, 1,
				     cmd->resp->data, cmd->resp->len, false);

	cmd->cmd_cb(ddev, cmd->cb_context, cmd->resp);

	kfree(cmd->mdaa_params);
	kfree(cmd);

	schedule_work(&ddev->cmd_work);
}

static void digital_send_cmd_complete(struct nfc_digital_dev *ddev,
				      void *arg, struct sk_buff *resp)
{
	struct digital_cmd *cmd = arg;

	cmd->resp = resp;

	schedule_work(&ddev->cmd_complete_work);
}

static void digital_wq_cmd(struct work_struct *work)
{
	int rc;
	struct digital_cmd *cmd;
	struct digital_tg_mdaa_params *params;
	struct nfc_digital_dev *ddev = container_of(work,
						    struct nfc_digital_dev,
						    cmd_work);

	mutex_lock(&ddev->cmd_lock);

	cmd = list_first_entry_or_null(&ddev->cmd_queue, struct digital_cmd,
				       queue);
	if (!cmd || cmd->pending) {
		mutex_unlock(&ddev->cmd_lock);
		return;
	}

	mutex_unlock(&ddev->cmd_lock);

	if (cmd->req)
		print_hex_dump_debug("DIGITAL TX: ", DUMP_PREFIX_NONE, 16, 1,
				     cmd->req->data, cmd->req->len, false);

	switch (cmd->type) {
	case DIGITAL_CMD_IN_SEND:
		rc = ddev->ops->in_send_cmd(ddev, cmd->req, cmd->timeout,
					    digital_send_cmd_complete, cmd);
		break;

	case DIGITAL_CMD_TG_SEND:
		rc = ddev->ops->tg_send_cmd(ddev, cmd->req, cmd->timeout,
					    digital_send_cmd_complete, cmd);
		break;

	case DIGITAL_CMD_TG_LISTEN:
		rc = ddev->ops->tg_listen(ddev, cmd->timeout,
					  digital_send_cmd_complete, cmd);
		break;

	case DIGITAL_CMD_TG_LISTEN_MDAA:
		params = cmd->mdaa_params;

		rc = ddev->ops->tg_listen_mdaa(ddev, params, cmd->timeout,
					       digital_send_cmd_complete, cmd);
		break;

	default:
		PR_ERR("Unknown cmd type %d", cmd->type);
		return;
	}

	if (!rc)
		return;

	PR_ERR("in_send_command returned err %d", rc);

	mutex_lock(&ddev->cmd_lock);
	list_del(&cmd->queue);
	mutex_unlock(&ddev->cmd_lock);

	kfree_skb(cmd->req);
	kfree(cmd->mdaa_params);
	kfree(cmd);

	schedule_work(&ddev->cmd_work);
}

int digital_send_cmd(struct nfc_digital_dev *ddev, u8 cmd_type,
		     struct sk_buff *skb, struct digital_tg_mdaa_params *params,
		     u16 timeout, nfc_digital_cmd_complete_t cmd_cb,
		     void *cb_context)
{
	struct digital_cmd *cmd;

	cmd = kzalloc(sizeof(struct digital_cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->type = cmd_type;
	cmd->timeout = timeout;
	cmd->req = skb;
	cmd->mdaa_params = params;
	cmd->cmd_cb = cmd_cb;
	cmd->cb_context = cb_context;
	INIT_LIST_HEAD(&cmd->queue);

	mutex_lock(&ddev->cmd_lock);
	list_add_tail(&cmd->queue, &ddev->cmd_queue);
	mutex_unlock(&ddev->cmd_lock);

	schedule_work(&ddev->cmd_work);

	return 0;
}

int digital_in_configure_hw(struct nfc_digital_dev *ddev, int type, int param)
{
	int rc;

	rc = ddev->ops->in_configure_hw(ddev, type, param);
	if (rc)
		PR_ERR("in_configure_hw failed: %d", rc);

	return rc;
}

int digital_tg_configure_hw(struct nfc_digital_dev *ddev, int type, int param)
{
	int rc;

	rc = ddev->ops->tg_configure_hw(ddev, type, param);
	if (rc)
		PR_ERR("tg_configure_hw failed: %d", rc);

	return rc;
}

static int digital_tg_listen_mdaa(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	struct digital_tg_mdaa_params *params;

	params = kzalloc(sizeof(struct digital_tg_mdaa_params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->sens_res = DIGITAL_SENS_RES_NFC_DEP;
	get_random_bytes(params->nfcid1, sizeof(params->nfcid1));
	params->sel_res = DIGITAL_SEL_RES_NFC_DEP;

	params->nfcid2[0] = DIGITAL_SENSF_NFCID2_NFC_DEP_B1;
	params->nfcid2[1] = DIGITAL_SENSF_NFCID2_NFC_DEP_B2;
	get_random_bytes(params->nfcid2 + 2, NFC_NFCID2_MAXSIZE - 2);
	params->sc = DIGITAL_SENSF_FELICA_SC;

	return digital_send_cmd(ddev, DIGITAL_CMD_TG_LISTEN_MDAA, NULL, params,
				500, digital_tg_recv_atr_req, NULL);
}

int digital_target_found(struct nfc_digital_dev *ddev,
			 struct nfc_target *target, u8 protocol)
{
	int rc;
	u8 framing;
	u8 rf_tech;
	int (*check_crc)(struct sk_buff *skb);
	void (*add_crc)(struct sk_buff *skb);

	rf_tech = ddev->poll_techs[ddev->poll_tech_index].rf_tech;

	switch (protocol) {
	case NFC_PROTO_JEWEL:
		framing = NFC_DIGITAL_FRAMING_NFCA_T1T;
		check_crc = digital_skb_check_crc_b;
		add_crc = digital_skb_add_crc_b;
		break;

	case NFC_PROTO_MIFARE:
		framing = NFC_DIGITAL_FRAMING_NFCA_T2T;
		check_crc = digital_skb_check_crc_a;
		add_crc = digital_skb_add_crc_a;
		break;

	case NFC_PROTO_FELICA:
		framing = NFC_DIGITAL_FRAMING_NFCF_T3T;
		check_crc = digital_skb_check_crc_f;
		add_crc = digital_skb_add_crc_f;
		break;

	case NFC_PROTO_NFC_DEP:
		if (rf_tech == NFC_DIGITAL_RF_TECH_106A) {
			framing = NFC_DIGITAL_FRAMING_NFCA_NFC_DEP;
			check_crc = digital_skb_check_crc_a;
			add_crc = digital_skb_add_crc_a;
		} else {
			framing = NFC_DIGITAL_FRAMING_NFCF_NFC_DEP;
			check_crc = digital_skb_check_crc_f;
			add_crc = digital_skb_add_crc_f;
		}
		break;

	default:
		PR_ERR("Invalid protocol %d", protocol);
		return -EINVAL;
	}

	PR_DBG("rf_tech=%d, protocol=%d", rf_tech, protocol);

	ddev->curr_rf_tech = rf_tech;
	ddev->curr_protocol = protocol;

	if (DIGITAL_DRV_CAPS_IN_CRC(ddev)) {
		ddev->skb_add_crc = digital_skb_add_crc_none;
		ddev->skb_check_crc = digital_skb_check_crc_none;
	} else {
		ddev->skb_add_crc = add_crc;
		ddev->skb_check_crc = check_crc;
	}

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING, framing);
	if (rc)
		return rc;

	target->supported_protocols = (1 << protocol);
	rc = nfc_targets_found(ddev->nfc_dev, target, 1);
	if (rc)
		return rc;

	ddev->poll_tech_count = 0;

	return 0;
}

void digital_poll_next_tech(struct nfc_digital_dev *ddev)
{
	digital_switch_rf(ddev, 0);

	mutex_lock(&ddev->poll_lock);

	if (!ddev->poll_tech_count) {
		mutex_unlock(&ddev->poll_lock);
		return;
	}

	ddev->poll_tech_index = (ddev->poll_tech_index + 1) %
				ddev->poll_tech_count;

	mutex_unlock(&ddev->poll_lock);

	schedule_work(&ddev->poll_work);
}

static void digital_wq_poll(struct work_struct *work)
{
	int rc;
	struct digital_poll_tech *poll_tech;
	struct nfc_digital_dev *ddev = container_of(work,
						    struct nfc_digital_dev,
						    poll_work);
	mutex_lock(&ddev->poll_lock);

	if (!ddev->poll_tech_count) {
		mutex_unlock(&ddev->poll_lock);
		return;
	}

	poll_tech = &ddev->poll_techs[ddev->poll_tech_index];

	mutex_unlock(&ddev->poll_lock);

	rc = poll_tech->poll_func(ddev, poll_tech->rf_tech);
	if (rc)
		digital_poll_next_tech(ddev);
}

static void digital_add_poll_tech(struct nfc_digital_dev *ddev, u8 rf_tech,
				  digital_poll_t poll_func)
{
	struct digital_poll_tech *poll_tech;

	if (ddev->poll_tech_count >= NFC_DIGITAL_POLL_MODE_COUNT_MAX)
		return;

	poll_tech = &ddev->poll_techs[ddev->poll_tech_count++];

	poll_tech->rf_tech = rf_tech;
	poll_tech->poll_func = poll_func;
}

/**
 * start_poll operation
 *
 * For every supported protocol, the corresponding polling function is added
 * to the table of polling technologies (ddev->poll_techs[]) using
 * digital_add_poll_tech().
 * When a polling function fails (by timeout or protocol error) the next one is
 * schedule by digital_poll_next_tech() on the poll workqueue (ddev->poll_work).
 */
static int digital_start_poll(struct nfc_dev *nfc_dev, __u32 im_protocols,
			      __u32 tm_protocols)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);
	u32 matching_im_protocols, matching_tm_protocols;

	PR_DBG("protocols: im 0x%x, tm 0x%x, supported 0x%x", im_protocols,
	       tm_protocols, ddev->protocols);

	matching_im_protocols = ddev->protocols & im_protocols;
	matching_tm_protocols = ddev->protocols & tm_protocols;

	if (!matching_im_protocols && !matching_tm_protocols) {
		PR_ERR("No known protocol");
		return -EINVAL;
	}

	if (ddev->poll_tech_count) {
		PR_ERR("Already polling");
		return -EBUSY;
	}

	if (ddev->curr_protocol) {
		PR_ERR("A target is already active");
		return -EBUSY;
	}

	ddev->poll_tech_count = 0;
	ddev->poll_tech_index = 0;

	if (matching_im_protocols & DIGITAL_PROTO_NFCA_RF_TECH)
		digital_add_poll_tech(ddev, NFC_DIGITAL_RF_TECH_106A,
				      digital_in_send_sens_req);

	if (im_protocols & DIGITAL_PROTO_NFCF_RF_TECH) {
		digital_add_poll_tech(ddev, NFC_DIGITAL_RF_TECH_212F,
				      digital_in_send_sensf_req);

		digital_add_poll_tech(ddev, NFC_DIGITAL_RF_TECH_424F,
				      digital_in_send_sensf_req);
	}

	if (tm_protocols & NFC_PROTO_NFC_DEP_MASK) {
		if (ddev->ops->tg_listen_mdaa) {
			digital_add_poll_tech(ddev, 0,
					      digital_tg_listen_mdaa);
		} else {
			digital_add_poll_tech(ddev, NFC_DIGITAL_RF_TECH_106A,
					      digital_tg_listen_nfca);

			digital_add_poll_tech(ddev, NFC_DIGITAL_RF_TECH_212F,
					      digital_tg_listen_nfcf);

			digital_add_poll_tech(ddev, NFC_DIGITAL_RF_TECH_424F,
					      digital_tg_listen_nfcf);
		}
	}

	if (!ddev->poll_tech_count) {
		PR_ERR("Unsupported protocols: im=0x%x, tm=0x%x",
		       matching_im_protocols, matching_tm_protocols);
		return -EINVAL;
	}

	schedule_work(&ddev->poll_work);

	return 0;
}

static void digital_stop_poll(struct nfc_dev *nfc_dev)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);

	mutex_lock(&ddev->poll_lock);

	if (!ddev->poll_tech_count) {
		PR_ERR("Polling operation was not running");
		mutex_unlock(&ddev->poll_lock);
		return;
	}

	ddev->poll_tech_count = 0;

	mutex_unlock(&ddev->poll_lock);

	cancel_work_sync(&ddev->poll_work);

	digital_abort_cmd(ddev);
}

static int digital_dev_up(struct nfc_dev *nfc_dev)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);

	digital_switch_rf(ddev, 1);

	return 0;
}

static int digital_dev_down(struct nfc_dev *nfc_dev)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);

	digital_switch_rf(ddev, 0);

	return 0;
}

static int digital_dep_link_up(struct nfc_dev *nfc_dev,
			       struct nfc_target *target,
			       __u8 comm_mode, __u8 *gb, size_t gb_len)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);

	return digital_in_send_atr_req(ddev, target, comm_mode, gb, gb_len);
}

static int digital_dep_link_down(struct nfc_dev *nfc_dev)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);

	ddev->curr_protocol = 0;

	return 0;
}

static int digital_activate_target(struct nfc_dev *nfc_dev,
				   struct nfc_target *target, __u32 protocol)
{
	return 0;
}

static void digital_deactivate_target(struct nfc_dev *nfc_dev,
				      struct nfc_target *target)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);

	ddev->curr_protocol = 0;
}

static int digital_tg_send(struct nfc_dev *dev, struct sk_buff *skb)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(dev);

	return digital_tg_send_dep_res(ddev, skb);
}

static void digital_in_send_complete(struct nfc_digital_dev *ddev, void *arg,
				     struct sk_buff *resp)
{
	struct digital_data_exch *data_exch = arg;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		goto done;
	}

	if (ddev->curr_protocol == NFC_PROTO_MIFARE)
		rc = digital_in_recv_mifare_res(resp);
	else
		rc = ddev->skb_check_crc(resp);

	if (rc) {
		kfree_skb(resp);
		resp = NULL;
	}

done:
	data_exch->cb(data_exch->cb_context, resp, rc);

	kfree(data_exch);
}

static int digital_in_send(struct nfc_dev *nfc_dev, struct nfc_target *target,
			   struct sk_buff *skb, data_exchange_cb_t cb,
			   void *cb_context)
{
	struct nfc_digital_dev *ddev = nfc_get_drvdata(nfc_dev);
	struct digital_data_exch *data_exch;

	data_exch = kzalloc(sizeof(struct digital_data_exch), GFP_KERNEL);
	if (!data_exch) {
		PR_ERR("Failed to allocate data_exch struct");
		return -ENOMEM;
	}

	data_exch->cb = cb;
	data_exch->cb_context = cb_context;

	if (ddev->curr_protocol == NFC_PROTO_NFC_DEP)
		return digital_in_send_dep_req(ddev, target, skb, data_exch);

	ddev->skb_add_crc(skb);

	return digital_in_send_cmd(ddev, skb, 500, digital_in_send_complete,
				   data_exch);
}

static struct nfc_ops digital_nfc_ops = {
	.dev_up = digital_dev_up,
	.dev_down = digital_dev_down,
	.start_poll = digital_start_poll,
	.stop_poll = digital_stop_poll,
	.dep_link_up = digital_dep_link_up,
	.dep_link_down = digital_dep_link_down,
	.activate_target = digital_activate_target,
	.deactivate_target = digital_deactivate_target,
	.tm_send = digital_tg_send,
	.im_transceive = digital_in_send,
};

struct nfc_digital_dev *nfc_digital_allocate_device(struct nfc_digital_ops *ops,
					    __u32 supported_protocols,
					    __u32 driver_capabilities,
					    int tx_headroom, int tx_tailroom)
{
	struct nfc_digital_dev *ddev;

	if (!ops->in_configure_hw || !ops->in_send_cmd || !ops->tg_listen ||
	    !ops->tg_configure_hw || !ops->tg_send_cmd || !ops->abort_cmd ||
	    !ops->switch_rf)
		return NULL;

	ddev = kzalloc(sizeof(struct nfc_digital_dev), GFP_KERNEL);
	if (!ddev) {
		PR_ERR("kzalloc failed");
		return NULL;
	}

	ddev->driver_capabilities = driver_capabilities;
	ddev->ops = ops;

	mutex_init(&ddev->cmd_lock);
	INIT_LIST_HEAD(&ddev->cmd_queue);

	INIT_WORK(&ddev->cmd_work, digital_wq_cmd);
	INIT_WORK(&ddev->cmd_complete_work, digital_wq_cmd_complete);

	mutex_init(&ddev->poll_lock);
	INIT_WORK(&ddev->poll_work, digital_wq_poll);

	if (supported_protocols & NFC_PROTO_JEWEL_MASK)
		ddev->protocols |= NFC_PROTO_JEWEL_MASK;
	if (supported_protocols & NFC_PROTO_MIFARE_MASK)
		ddev->protocols |= NFC_PROTO_MIFARE_MASK;
	if (supported_protocols & NFC_PROTO_FELICA_MASK)
		ddev->protocols |= NFC_PROTO_FELICA_MASK;
	if (supported_protocols & NFC_PROTO_NFC_DEP_MASK)
		ddev->protocols |= NFC_PROTO_NFC_DEP_MASK;

	ddev->tx_headroom = tx_headroom + DIGITAL_MAX_HEADER_LEN;
	ddev->tx_tailroom = tx_tailroom + DIGITAL_CRC_LEN;

	ddev->nfc_dev = nfc_allocate_device(&digital_nfc_ops, ddev->protocols,
					    ddev->tx_headroom,
					    ddev->tx_tailroom);
	if (!ddev->nfc_dev) {
		PR_ERR("nfc_allocate_device failed");
		goto free_dev;
	}

	nfc_set_drvdata(ddev->nfc_dev, ddev);

	return ddev;

free_dev:
	kfree(ddev);

	return NULL;
}
EXPORT_SYMBOL(nfc_digital_allocate_device);

void nfc_digital_free_device(struct nfc_digital_dev *ddev)
{
	nfc_free_device(ddev->nfc_dev);
	kfree(ddev);
}
EXPORT_SYMBOL(nfc_digital_free_device);

int nfc_digital_register_device(struct nfc_digital_dev *ddev)
{
	return nfc_register_device(ddev->nfc_dev);
}
EXPORT_SYMBOL(nfc_digital_register_device);

void nfc_digital_unregister_device(struct nfc_digital_dev *ddev)
{
	struct digital_cmd *cmd, *n;

	nfc_unregister_device(ddev->nfc_dev);

	mutex_lock(&ddev->poll_lock);
	ddev->poll_tech_count = 0;
	mutex_unlock(&ddev->poll_lock);

	cancel_work_sync(&ddev->poll_work);
	cancel_work_sync(&ddev->cmd_work);
	cancel_work_sync(&ddev->cmd_complete_work);

	list_for_each_entry_safe(cmd, n, &ddev->cmd_queue, queue) {
		list_del(&cmd->queue);
		kfree(cmd->mdaa_params);
		kfree(cmd);
	}
}
EXPORT_SYMBOL(nfc_digital_unregister_device);

MODULE_LICENSE("GPL");
