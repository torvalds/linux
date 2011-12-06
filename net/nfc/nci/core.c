/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

static void nci_cmd_work(struct work_struct *work);
static void nci_rx_work(struct work_struct *work);
static void nci_tx_work(struct work_struct *work);

/* ---- NCI requests ---- */

void nci_req_complete(struct nci_dev *ndev, int result)
{
	if (ndev->req_status == NCI_REQ_PEND) {
		ndev->req_result = result;
		ndev->req_status = NCI_REQ_DONE;
		complete(&ndev->req_completion);
	}
}

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
	unsigned long opt,
	__u32 timeout)
{
	int rc = 0;
	unsigned long completion_rc;

	ndev->req_status = NCI_REQ_PEND;

	init_completion(&ndev->req_completion);
	req(ndev, opt);
	completion_rc = wait_for_completion_interruptible_timeout(
							&ndev->req_completion,
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

static inline int nci_request(struct nci_dev *ndev,
		void (*req)(struct nci_dev *ndev, unsigned long opt),
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
			cfg[*num].mode = NCI_DISC_MAP_MODE_BOTH;
			cfg[*num].rf_interface_type = NCI_RF_INTERFACE_ISO_DEP;
			(*num)++;
		} else if (ndev->supported_rf_interfaces[i] ==
			NCI_RF_INTERFACE_NFC_DEP) {
			cfg[*num].rf_protocol = NCI_RF_PROTOCOL_NFC_DEP;
			cfg[*num].mode = NCI_DISC_MAP_MODE_BOTH;
			cfg[*num].rf_interface_type = NCI_RF_INTERFACE_NFC_DEP;
			(*num)++;
		}

		if (*num == NCI_MAX_NUM_MAPPING_CONFIGS)
			break;
	}

	nci_send_cmd(ndev, NCI_OP_RF_DISCOVER_MAP_CMD,
		(1 + ((*num)*sizeof(struct disc_map_config))),
		&cmd);
}

static void nci_rf_discover_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_rf_disc_cmd cmd;
	__u32 protocols = opt;

	cmd.num_disc_configs = 0;

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS) &&
		(protocols & NFC_PROTO_JEWEL_MASK
		|| protocols & NFC_PROTO_MIFARE_MASK
		|| protocols & NFC_PROTO_ISO14443_MASK
		|| protocols & NFC_PROTO_NFC_DEP_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].type =
		NCI_DISCOVERY_TYPE_POLL_A_PASSIVE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS) &&
		(protocols & NFC_PROTO_ISO14443_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].type =
		NCI_DISCOVERY_TYPE_POLL_B_PASSIVE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	if ((cmd.num_disc_configs < NCI_MAX_NUM_RF_CONFIGS) &&
		(protocols & NFC_PROTO_FELICA_MASK
		|| protocols & NFC_PROTO_NFC_DEP_MASK)) {
		cmd.disc_configs[cmd.num_disc_configs].type =
		NCI_DISCOVERY_TYPE_POLL_F_PASSIVE;
		cmd.disc_configs[cmd.num_disc_configs].frequency = 1;
		cmd.num_disc_configs++;
	}

	nci_send_cmd(ndev, NCI_OP_RF_DISCOVER_CMD,
		(1 + (cmd.num_disc_configs*sizeof(struct disc_config))),
		&cmd);
}

static void nci_rf_deactivate_req(struct nci_dev *ndev, unsigned long opt)
{
	struct nci_rf_deactivate_cmd cmd;

	cmd.type = NCI_DEACTIVATE_TYPE_IDLE_MODE;

	nci_send_cmd(ndev, NCI_OP_RF_DEACTIVATE_CMD,
			sizeof(struct nci_rf_deactivate_cmd),
			&cmd);
}

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

	rc = __nci_request(ndev, nci_reset_req, 0,
				msecs_to_jiffies(NCI_RESET_TIMEOUT));

	if (!rc) {
		rc = __nci_request(ndev, nci_init_req, 0,
				msecs_to_jiffies(NCI_INIT_TIMEOUT));
	}

	if (!rc) {
		rc = __nci_request(ndev, nci_init_complete_req, 0,
				msecs_to_jiffies(NCI_INIT_TIMEOUT));
	}

	clear_bit(NCI_INIT, &ndev->flags);

	if (!rc) {
		set_bit(NCI_UP, &ndev->flags);
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
	clear_bit(NCI_INIT, &ndev->flags);

	/* Flush cmd wq */
	flush_workqueue(ndev->cmd_wq);

	/* After this point our queues are empty
	 * and no works are scheduled. */
	ndev->ops->close(ndev);

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

static int nci_start_poll(struct nfc_dev *nfc_dev, __u32 protocols)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	int rc;

	if (test_bit(NCI_DISCOVERY, &ndev->flags)) {
		pr_err("unable to start poll, since poll is already active\n");
		return -EBUSY;
	}

	if (ndev->target_active_prot) {
		pr_err("there is an active target\n");
		return -EBUSY;
	}

	if (test_bit(NCI_POLL_ACTIVE, &ndev->flags)) {
		pr_debug("target is active, implicitly deactivate...\n");

		rc = nci_request(ndev, nci_rf_deactivate_req, 0,
			msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
		if (rc)
			return -EBUSY;
	}

	rc = nci_request(ndev, nci_rf_discover_req, protocols,
		msecs_to_jiffies(NCI_RF_DISC_TIMEOUT));

	if (!rc)
		ndev->poll_prots = protocols;

	return rc;
}

static void nci_stop_poll(struct nfc_dev *nfc_dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	if (!test_bit(NCI_DISCOVERY, &ndev->flags)) {
		pr_err("unable to stop poll, since poll is not active\n");
		return;
	}

	nci_request(ndev, nci_rf_deactivate_req, 0,
		msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
}

static int nci_activate_target(struct nfc_dev *nfc_dev, __u32 target_idx,
				__u32 protocol)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	pr_debug("target_idx %d, protocol 0x%x\n", target_idx, protocol);

	if (!test_bit(NCI_POLL_ACTIVE, &ndev->flags)) {
		pr_err("there is no available target to activate\n");
		return -EINVAL;
	}

	if (ndev->target_active_prot) {
		pr_err("there is already an active target\n");
		return -EBUSY;
	}

	if (!(ndev->target_available_prots & (1 << protocol))) {
		pr_err("target does not support the requested protocol 0x%x\n",
		       protocol);
		return -EINVAL;
	}

	ndev->target_active_prot = protocol;
	ndev->target_available_prots = 0;

	return 0;
}

static void nci_deactivate_target(struct nfc_dev *nfc_dev, __u32 target_idx)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);

	pr_debug("target_idx %d\n", target_idx);

	if (!ndev->target_active_prot) {
		pr_err("unable to deactivate target, no active target\n");
		return;
	}

	ndev->target_active_prot = 0;

	if (test_bit(NCI_POLL_ACTIVE, &ndev->flags)) {
		nci_request(ndev, nci_rf_deactivate_req, 0,
			msecs_to_jiffies(NCI_RF_DEACTIVATE_TIMEOUT));
	}
}

static int nci_data_exchange(struct nfc_dev *nfc_dev, __u32 target_idx,
						struct sk_buff *skb,
						data_exchange_cb_t cb,
						void *cb_context)
{
	struct nci_dev *ndev = nfc_get_drvdata(nfc_dev);
	int rc;

	pr_debug("target_idx %d, len %d\n", target_idx, skb->len);

	if (!ndev->target_active_prot) {
		pr_err("unable to exchange data, no active target\n");
		return -EINVAL;
	}

	if (test_and_set_bit(NCI_DATA_EXCHANGE, &ndev->flags))
		return -EBUSY;

	/* store cb and context to be used on receiving data */
	ndev->data_exchange_cb = cb;
	ndev->data_exchange_cb_context = cb_context;

	rc = nci_send_data(ndev, NCI_STATIC_RF_CONN_ID, skb);
	if (rc)
		clear_bit(NCI_DATA_EXCHANGE, &ndev->flags);

	return rc;
}

static struct nfc_ops nci_nfc_ops = {
	.dev_up = nci_dev_up,
	.dev_down = nci_dev_down,
	.start_poll = nci_start_poll,
	.stop_poll = nci_stop_poll,
	.activate_target = nci_activate_target,
	.deactivate_target = nci_deactivate_target,
	.data_exchange = nci_data_exchange,
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
					int tx_headroom,
					int tx_tailroom)
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
	ndev->tx_headroom = tx_headroom;
	ndev->tx_tailroom = tx_tailroom;

	ndev->nfc_dev = nfc_allocate_device(&nci_nfc_ops,
						supported_protocols,
						tx_headroom + NCI_DATA_HDR_SIZE,
						tx_tailroom);
	if (!ndev->nfc_dev)
		goto free_exit;

	nfc_set_drvdata(ndev->nfc_dev, ndev);

	return ndev;

free_exit:
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

	rc = nfc_register_device(ndev->nfc_dev);
	if (rc)
		goto exit;

	ndev->flags = 0;

	INIT_WORK(&ndev->cmd_work, nci_cmd_work);
	snprintf(name, sizeof(name), "%s_nci_cmd_wq", dev_name(dev));
	ndev->cmd_wq = create_singlethread_workqueue(name);
	if (!ndev->cmd_wq) {
		rc = -ENOMEM;
		goto unreg_exit;
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

	mutex_init(&ndev->req_lock);

	goto exit;

destroy_rx_wq_exit:
	destroy_workqueue(ndev->rx_wq);

destroy_cmd_wq_exit:
	destroy_workqueue(ndev->cmd_wq);

unreg_exit:
	nfc_unregister_device(ndev->nfc_dev);

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
	nci_close_device(ndev);

	destroy_workqueue(ndev->cmd_wq);
	destroy_workqueue(ndev->rx_wq);
	destroy_workqueue(ndev->tx_wq);

	nfc_unregister_device(ndev->nfc_dev);
}
EXPORT_SYMBOL(nci_unregister_device);

/**
 * nci_recv_frame - receive frame from NCI drivers
 *
 * @skb: The sk_buff to receive
 */
int nci_recv_frame(struct sk_buff *skb)
{
	struct nci_dev *ndev = (struct nci_dev *) skb->dev;

	pr_debug("len %d\n", skb->len);

	if (!ndev || (!test_bit(NCI_UP, &ndev->flags)
		&& !test_bit(NCI_INIT, &ndev->flags))) {
		kfree_skb(skb);
		return -ENXIO;
	}

	/* Queue frame for rx worker thread */
	skb_queue_tail(&ndev->rx_q, skb);
	queue_work(ndev->rx_wq, &ndev->rx_work);

	return 0;
}
EXPORT_SYMBOL(nci_recv_frame);

static int nci_send_frame(struct sk_buff *skb)
{
	struct nci_dev *ndev = (struct nci_dev *) skb->dev;

	pr_debug("len %d\n", skb->len);

	if (!ndev) {
		kfree_skb(skb);
		return -ENODEV;
	}

	/* Get rid of skb owner, prior to sending to the driver. */
	skb_orphan(skb);

	return ndev->ops->send(skb);
}

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

	skb->dev = (void *) ndev;

	skb_queue_tail(&ndev->cmd_q, skb);
	queue_work(ndev->cmd_wq, &ndev->cmd_work);

	return 0;
}

/* ---- NCI TX Data worker thread ---- */

static void nci_tx_work(struct work_struct *work)
{
	struct nci_dev *ndev = container_of(work, struct nci_dev, tx_work);
	struct sk_buff *skb;

	pr_debug("credits_cnt %d\n", atomic_read(&ndev->credits_cnt));

	/* Send queued tx data */
	while (atomic_read(&ndev->credits_cnt)) {
		skb = skb_dequeue(&ndev->tx_q);
		if (!skb)
			return;

		/* Check if data flow control is used */
		if (atomic_read(&ndev->credits_cnt) !=
				NCI_DATA_FLOW_CONTROL_NOT_USED)
			atomic_dec(&ndev->credits_cnt);

		pr_debug("NCI TX: MT=data, PBF=%d, conn_id=%d, plen=%d\n",
			 nci_pbf(skb->data),
			 nci_conn_id(skb->data),
			 nci_plen(skb->data));

		nci_send_frame(skb);
	}
}

/* ----- NCI RX worker thread (data & control) ----- */

static void nci_rx_work(struct work_struct *work)
{
	struct nci_dev *ndev = container_of(work, struct nci_dev, rx_work);
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ndev->rx_q))) {
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

		nci_send_frame(skb);

		mod_timer(&ndev->cmd_timer,
			jiffies + msecs_to_jiffies(NCI_CMD_TIMEOUT));
	}
}
