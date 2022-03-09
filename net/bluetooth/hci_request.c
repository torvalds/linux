/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2014 Intel Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#include <linux/sched/signal.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "smp.h"
#include "hci_request.h"
#include "msft.h"
#include "eir.h"

void hci_req_init(struct hci_request *req, struct hci_dev *hdev)
{
	skb_queue_head_init(&req->cmd_q);
	req->hdev = hdev;
	req->err = 0;
}

void hci_req_purge(struct hci_request *req)
{
	skb_queue_purge(&req->cmd_q);
}

bool hci_req_status_pend(struct hci_dev *hdev)
{
	return hdev->req_status == HCI_REQ_PEND;
}

static int req_run(struct hci_request *req, hci_req_complete_t complete,
		   hci_req_complete_skb_t complete_skb)
{
	struct hci_dev *hdev = req->hdev;
	struct sk_buff *skb;
	unsigned long flags;

	bt_dev_dbg(hdev, "length %u", skb_queue_len(&req->cmd_q));

	/* If an error occurred during request building, remove all HCI
	 * commands queued on the HCI request queue.
	 */
	if (req->err) {
		skb_queue_purge(&req->cmd_q);
		return req->err;
	}

	/* Do not allow empty requests */
	if (skb_queue_empty(&req->cmd_q))
		return -ENODATA;

	skb = skb_peek_tail(&req->cmd_q);
	if (complete) {
		bt_cb(skb)->hci.req_complete = complete;
	} else if (complete_skb) {
		bt_cb(skb)->hci.req_complete_skb = complete_skb;
		bt_cb(skb)->hci.req_flags |= HCI_REQ_SKB;
	}

	spin_lock_irqsave(&hdev->cmd_q.lock, flags);
	skb_queue_splice_tail(&req->cmd_q, &hdev->cmd_q);
	spin_unlock_irqrestore(&hdev->cmd_q.lock, flags);

	queue_work(hdev->workqueue, &hdev->cmd_work);

	return 0;
}

int hci_req_run(struct hci_request *req, hci_req_complete_t complete)
{
	return req_run(req, complete, NULL);
}

int hci_req_run_skb(struct hci_request *req, hci_req_complete_skb_t complete)
{
	return req_run(req, NULL, complete);
}

void hci_req_sync_complete(struct hci_dev *hdev, u8 result, u16 opcode,
			   struct sk_buff *skb)
{
	bt_dev_dbg(hdev, "result 0x%2.2x", result);

	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = result;
		hdev->req_status = HCI_REQ_DONE;
		if (skb)
			hdev->req_skb = skb_get(skb);
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

/* Execute request and wait for completion. */
int __hci_req_sync(struct hci_dev *hdev, int (*func)(struct hci_request *req,
						     unsigned long opt),
		   unsigned long opt, u32 timeout, u8 *hci_status)
{
	struct hci_request req;
	int err = 0;

	bt_dev_dbg(hdev, "start");

	hci_req_init(&req, hdev);

	hdev->req_status = HCI_REQ_PEND;

	err = func(&req, opt);
	if (err) {
		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;
		return err;
	}

	err = hci_req_run_skb(&req, hci_req_sync_complete);
	if (err < 0) {
		hdev->req_status = 0;

		/* ENODATA means the HCI request command queue is empty.
		 * This can happen when a request with conditionals doesn't
		 * trigger any commands to be sent. This is normal behavior
		 * and should not trigger an error return.
		 */
		if (err == -ENODATA) {
			if (hci_status)
				*hci_status = 0;
			return 0;
		}

		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;

		return err;
	}

	err = wait_event_interruptible_timeout(hdev->req_wait_q,
			hdev->req_status != HCI_REQ_PEND, timeout);

	if (err == -ERESTARTSYS)
		return -EINTR;

	switch (hdev->req_status) {
	case HCI_REQ_DONE:
		err = -bt_to_errno(hdev->req_result);
		if (hci_status)
			*hci_status = hdev->req_result;
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;
		break;

	default:
		err = -ETIMEDOUT;
		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;
		break;
	}

	kfree_skb(hdev->req_skb);
	hdev->req_skb = NULL;
	hdev->req_status = hdev->req_result = 0;

	bt_dev_dbg(hdev, "end: err %d", err);

	return err;
}

int hci_req_sync(struct hci_dev *hdev, int (*req)(struct hci_request *req,
						  unsigned long opt),
		 unsigned long opt, u32 timeout, u8 *hci_status)
{
	int ret;

	/* Serialize all requests */
	hci_req_sync_lock(hdev);
	/* check the state after obtaing the lock to protect the HCI_UP
	 * against any races from hci_dev_do_close when the controller
	 * gets removed.
	 */
	if (test_bit(HCI_UP, &hdev->flags))
		ret = __hci_req_sync(hdev, req, opt, timeout, hci_status);
	else
		ret = -ENETDOWN;
	hci_req_sync_unlock(hdev);

	return ret;
}

struct sk_buff *hci_prepare_cmd(struct hci_dev *hdev, u16 opcode, u32 plen,
				const void *param)
{
	int len = HCI_COMMAND_HDR_SIZE + plen;
	struct hci_command_hdr *hdr;
	struct sk_buff *skb;

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb)
		return NULL;

	hdr = skb_put(skb, HCI_COMMAND_HDR_SIZE);
	hdr->opcode = cpu_to_le16(opcode);
	hdr->plen   = plen;

	if (plen)
		skb_put_data(skb, param, plen);

	bt_dev_dbg(hdev, "skb len %d", skb->len);

	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
	hci_skb_opcode(skb) = opcode;

	return skb;
}

/* Queue a command to an asynchronous HCI request */
void hci_req_add_ev(struct hci_request *req, u16 opcode, u32 plen,
		    const void *param, u8 event)
{
	struct hci_dev *hdev = req->hdev;
	struct sk_buff *skb;

	bt_dev_dbg(hdev, "opcode 0x%4.4x plen %d", opcode, plen);

	/* If an error occurred during request building, there is no point in
	 * queueing the HCI command. We can simply return.
	 */
	if (req->err)
		return;

	skb = hci_prepare_cmd(hdev, opcode, plen, param);
	if (!skb) {
		bt_dev_err(hdev, "no memory for command (opcode 0x%4.4x)",
			   opcode);
		req->err = -ENOMEM;
		return;
	}

	if (skb_queue_empty(&req->cmd_q))
		bt_cb(skb)->hci.req_flags |= HCI_REQ_START;

	hci_skb_event(skb) = event;

	skb_queue_tail(&req->cmd_q, skb);
}

void hci_req_add(struct hci_request *req, u16 opcode, u32 plen,
		 const void *param)
{
	hci_req_add_ev(req, opcode, plen, param, 0);
}

void __hci_req_write_fast_connectable(struct hci_request *req, bool enable)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_write_page_scan_activity acp;
	u8 type;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return;

	if (hdev->hci_ver < BLUETOOTH_VER_1_2)
		return;

	if (enable) {
		type = PAGE_SCAN_TYPE_INTERLACED;

		/* 160 msec page scan interval */
		acp.interval = cpu_to_le16(0x0100);
	} else {
		type = hdev->def_page_scan_type;
		acp.interval = cpu_to_le16(hdev->def_page_scan_int);
	}

	acp.window = cpu_to_le16(hdev->def_page_scan_window);

	if (__cpu_to_le16(hdev->page_scan_interval) != acp.interval ||
	    __cpu_to_le16(hdev->page_scan_window) != acp.window)
		hci_req_add(req, HCI_OP_WRITE_PAGE_SCAN_ACTIVITY,
			    sizeof(acp), &acp);

	if (hdev->page_scan_type != type)
		hci_req_add(req, HCI_OP_WRITE_PAGE_SCAN_TYPE, 1, &type);
}

static void start_interleave_scan(struct hci_dev *hdev)
{
	hdev->interleave_scan_state = INTERLEAVE_SCAN_NO_FILTER;
	queue_delayed_work(hdev->req_workqueue,
			   &hdev->interleave_scan, 0);
}

static bool is_interleave_scanning(struct hci_dev *hdev)
{
	return hdev->interleave_scan_state != INTERLEAVE_SCAN_NONE;
}

static void cancel_interleave_scan(struct hci_dev *hdev)
{
	bt_dev_dbg(hdev, "cancelling interleave scan");

	cancel_delayed_work_sync(&hdev->interleave_scan);

	hdev->interleave_scan_state = INTERLEAVE_SCAN_NONE;
}

/* Return true if interleave_scan wasn't started until exiting this function,
 * otherwise, return false
 */
static bool __hci_update_interleaved_scan(struct hci_dev *hdev)
{
	/* Do interleaved scan only if all of the following are true:
	 * - There is at least one ADV monitor
	 * - At least one pending LE connection or one device to be scanned for
	 * - Monitor offloading is not supported
	 * If so, we should alternate between allowlist scan and one without
	 * any filters to save power.
	 */
	bool use_interleaving = hci_is_adv_monitoring(hdev) &&
				!(list_empty(&hdev->pend_le_conns) &&
				  list_empty(&hdev->pend_le_reports)) &&
				hci_get_adv_monitor_offload_ext(hdev) ==
				    HCI_ADV_MONITOR_EXT_NONE;
	bool is_interleaving = is_interleave_scanning(hdev);

	if (use_interleaving && !is_interleaving) {
		start_interleave_scan(hdev);
		bt_dev_dbg(hdev, "starting interleave scan");
		return true;
	}

	if (!use_interleaving && is_interleaving)
		cancel_interleave_scan(hdev);

	return false;
}

void __hci_req_update_name(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_write_local_name cp;

	memcpy(cp.name, hdev->dev_name, sizeof(cp.name));

	hci_req_add(req, HCI_OP_WRITE_LOCAL_NAME, sizeof(cp), &cp);
}

void __hci_req_update_eir(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_write_eir cp;

	if (!hdev_is_powered(hdev))
		return;

	if (!lmp_ext_inq_capable(hdev))
		return;

	if (!hci_dev_test_flag(hdev, HCI_SSP_ENABLED))
		return;

	if (hci_dev_test_flag(hdev, HCI_SERVICE_CACHE))
		return;

	memset(&cp, 0, sizeof(cp));

	eir_create(hdev, cp.data);

	if (memcmp(cp.data, hdev->eir, sizeof(cp.data)) == 0)
		return;

	memcpy(hdev->eir, cp.data, sizeof(cp.data));

	hci_req_add(req, HCI_OP_WRITE_EIR, sizeof(cp), &cp);
}

void hci_req_add_le_scan_disable(struct hci_request *req, bool rpa_le_conn)
{
	struct hci_dev *hdev = req->hdev;

	if (hdev->scanning_paused) {
		bt_dev_dbg(hdev, "Scanning is paused for suspend");
		return;
	}

	if (use_ext_scan(hdev)) {
		struct hci_cp_le_set_ext_scan_enable cp;

		memset(&cp, 0, sizeof(cp));
		cp.enable = LE_SCAN_DISABLE;
		hci_req_add(req, HCI_OP_LE_SET_EXT_SCAN_ENABLE, sizeof(cp),
			    &cp);
	} else {
		struct hci_cp_le_set_scan_enable cp;

		memset(&cp, 0, sizeof(cp));
		cp.enable = LE_SCAN_DISABLE;
		hci_req_add(req, HCI_OP_LE_SET_SCAN_ENABLE, sizeof(cp), &cp);
	}

	/* Disable address resolution */
	if (hci_dev_test_flag(hdev, HCI_LL_RPA_RESOLUTION) && !rpa_le_conn) {
		__u8 enable = 0x00;

		hci_req_add(req, HCI_OP_LE_SET_ADDR_RESOLV_ENABLE, 1, &enable);
	}
}

static void del_from_accept_list(struct hci_request *req, bdaddr_t *bdaddr,
				 u8 bdaddr_type)
{
	struct hci_cp_le_del_from_accept_list cp;

	cp.bdaddr_type = bdaddr_type;
	bacpy(&cp.bdaddr, bdaddr);

	bt_dev_dbg(req->hdev, "Remove %pMR (0x%x) from accept list", &cp.bdaddr,
		   cp.bdaddr_type);
	hci_req_add(req, HCI_OP_LE_DEL_FROM_ACCEPT_LIST, sizeof(cp), &cp);

	if (use_ll_privacy(req->hdev)) {
		struct smp_irk *irk;

		irk = hci_find_irk_by_addr(req->hdev, bdaddr, bdaddr_type);
		if (irk) {
			struct hci_cp_le_del_from_resolv_list cp;

			cp.bdaddr_type = bdaddr_type;
			bacpy(&cp.bdaddr, bdaddr);

			hci_req_add(req, HCI_OP_LE_DEL_FROM_RESOLV_LIST,
				    sizeof(cp), &cp);
		}
	}
}

/* Adds connection to accept list if needed. On error, returns -1. */
static int add_to_accept_list(struct hci_request *req,
			      struct hci_conn_params *params, u8 *num_entries,
			      bool allow_rpa)
{
	struct hci_cp_le_add_to_accept_list cp;
	struct hci_dev *hdev = req->hdev;

	/* Already in accept list */
	if (hci_bdaddr_list_lookup(&hdev->le_accept_list, &params->addr,
				   params->addr_type))
		return 0;

	/* Select filter policy to accept all advertising */
	if (*num_entries >= hdev->le_accept_list_size)
		return -1;

	/* Accept list can not be used with RPAs */
	if (!allow_rpa &&
	    !hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY) &&
	    hci_find_irk_by_addr(hdev, &params->addr, params->addr_type)) {
		return -1;
	}

	/* During suspend, only wakeable devices can be in accept list */
	if (hdev->suspended &&
	    !(params->flags & HCI_CONN_FLAG_REMOTE_WAKEUP))
		return 0;

	*num_entries += 1;
	cp.bdaddr_type = params->addr_type;
	bacpy(&cp.bdaddr, &params->addr);

	bt_dev_dbg(hdev, "Add %pMR (0x%x) to accept list", &cp.bdaddr,
		   cp.bdaddr_type);
	hci_req_add(req, HCI_OP_LE_ADD_TO_ACCEPT_LIST, sizeof(cp), &cp);

	if (use_ll_privacy(hdev)) {
		struct smp_irk *irk;

		irk = hci_find_irk_by_addr(hdev, &params->addr,
					   params->addr_type);
		if (irk) {
			struct hci_cp_le_add_to_resolv_list cp;

			cp.bdaddr_type = params->addr_type;
			bacpy(&cp.bdaddr, &params->addr);
			memcpy(cp.peer_irk, irk->val, 16);

			if (hci_dev_test_flag(hdev, HCI_PRIVACY))
				memcpy(cp.local_irk, hdev->irk, 16);
			else
				memset(cp.local_irk, 0, 16);

			hci_req_add(req, HCI_OP_LE_ADD_TO_RESOLV_LIST,
				    sizeof(cp), &cp);
		}
	}

	return 0;
}

static u8 update_accept_list(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_conn_params *params;
	struct bdaddr_list *b;
	u8 num_entries = 0;
	bool pend_conn, pend_report;
	/* We allow usage of accept list even with RPAs in suspend. In the worst
	 * case, we won't be able to wake from devices that use the privacy1.2
	 * features. Additionally, once we support privacy1.2 and IRK
	 * offloading, we can update this to also check for those conditions.
	 */
	bool allow_rpa = hdev->suspended;

	if (use_ll_privacy(hdev))
		allow_rpa = true;

	/* Go through the current accept list programmed into the
	 * controller one by one and check if that address is still
	 * in the list of pending connections or list of devices to
	 * report. If not present in either list, then queue the
	 * command to remove it from the controller.
	 */
	list_for_each_entry(b, &hdev->le_accept_list, list) {
		pend_conn = hci_pend_le_action_lookup(&hdev->pend_le_conns,
						      &b->bdaddr,
						      b->bdaddr_type);
		pend_report = hci_pend_le_action_lookup(&hdev->pend_le_reports,
							&b->bdaddr,
							b->bdaddr_type);

		/* If the device is not likely to connect or report,
		 * remove it from the accept list.
		 */
		if (!pend_conn && !pend_report) {
			del_from_accept_list(req, &b->bdaddr, b->bdaddr_type);
			continue;
		}

		/* Accept list can not be used with RPAs */
		if (!allow_rpa &&
		    !hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY) &&
		    hci_find_irk_by_addr(hdev, &b->bdaddr, b->bdaddr_type)) {
			return 0x00;
		}

		num_entries++;
	}

	/* Since all no longer valid accept list entries have been
	 * removed, walk through the list of pending connections
	 * and ensure that any new device gets programmed into
	 * the controller.
	 *
	 * If the list of the devices is larger than the list of
	 * available accept list entries in the controller, then
	 * just abort and return filer policy value to not use the
	 * accept list.
	 */
	list_for_each_entry(params, &hdev->pend_le_conns, action) {
		if (add_to_accept_list(req, params, &num_entries, allow_rpa))
			return 0x00;
	}

	/* After adding all new pending connections, walk through
	 * the list of pending reports and also add these to the
	 * accept list if there is still space. Abort if space runs out.
	 */
	list_for_each_entry(params, &hdev->pend_le_reports, action) {
		if (add_to_accept_list(req, params, &num_entries, allow_rpa))
			return 0x00;
	}

	/* Use the allowlist unless the following conditions are all true:
	 * - We are not currently suspending
	 * - There are 1 or more ADV monitors registered and it's not offloaded
	 * - Interleaved scanning is not currently using the allowlist
	 */
	if (!idr_is_empty(&hdev->adv_monitors_idr) && !hdev->suspended &&
	    hci_get_adv_monitor_offload_ext(hdev) == HCI_ADV_MONITOR_EXT_NONE &&
	    hdev->interleave_scan_state != INTERLEAVE_SCAN_ALLOWLIST)
		return 0x00;

	/* Select filter policy to use accept list */
	return 0x01;
}

static bool scan_use_rpa(struct hci_dev *hdev)
{
	return hci_dev_test_flag(hdev, HCI_PRIVACY);
}

static void hci_req_start_scan(struct hci_request *req, u8 type, u16 interval,
			       u16 window, u8 own_addr_type, u8 filter_policy,
			       bool filter_dup, bool addr_resolv)
{
	struct hci_dev *hdev = req->hdev;

	if (hdev->scanning_paused) {
		bt_dev_dbg(hdev, "Scanning is paused for suspend");
		return;
	}

	if (use_ll_privacy(hdev) && addr_resolv) {
		u8 enable = 0x01;

		hci_req_add(req, HCI_OP_LE_SET_ADDR_RESOLV_ENABLE, 1, &enable);
	}

	/* Use ext scanning if set ext scan param and ext scan enable is
	 * supported
	 */
	if (use_ext_scan(hdev)) {
		struct hci_cp_le_set_ext_scan_params *ext_param_cp;
		struct hci_cp_le_set_ext_scan_enable ext_enable_cp;
		struct hci_cp_le_scan_phy_params *phy_params;
		u8 data[sizeof(*ext_param_cp) + sizeof(*phy_params) * 2];
		u32 plen;

		ext_param_cp = (void *)data;
		phy_params = (void *)ext_param_cp->data;

		memset(ext_param_cp, 0, sizeof(*ext_param_cp));
		ext_param_cp->own_addr_type = own_addr_type;
		ext_param_cp->filter_policy = filter_policy;

		plen = sizeof(*ext_param_cp);

		if (scan_1m(hdev) || scan_2m(hdev)) {
			ext_param_cp->scanning_phys |= LE_SCAN_PHY_1M;

			memset(phy_params, 0, sizeof(*phy_params));
			phy_params->type = type;
			phy_params->interval = cpu_to_le16(interval);
			phy_params->window = cpu_to_le16(window);

			plen += sizeof(*phy_params);
			phy_params++;
		}

		if (scan_coded(hdev)) {
			ext_param_cp->scanning_phys |= LE_SCAN_PHY_CODED;

			memset(phy_params, 0, sizeof(*phy_params));
			phy_params->type = type;
			phy_params->interval = cpu_to_le16(interval);
			phy_params->window = cpu_to_le16(window);

			plen += sizeof(*phy_params);
			phy_params++;
		}

		hci_req_add(req, HCI_OP_LE_SET_EXT_SCAN_PARAMS,
			    plen, ext_param_cp);

		memset(&ext_enable_cp, 0, sizeof(ext_enable_cp));
		ext_enable_cp.enable = LE_SCAN_ENABLE;
		ext_enable_cp.filter_dup = filter_dup;

		hci_req_add(req, HCI_OP_LE_SET_EXT_SCAN_ENABLE,
			    sizeof(ext_enable_cp), &ext_enable_cp);
	} else {
		struct hci_cp_le_set_scan_param param_cp;
		struct hci_cp_le_set_scan_enable enable_cp;

		memset(&param_cp, 0, sizeof(param_cp));
		param_cp.type = type;
		param_cp.interval = cpu_to_le16(interval);
		param_cp.window = cpu_to_le16(window);
		param_cp.own_address_type = own_addr_type;
		param_cp.filter_policy = filter_policy;
		hci_req_add(req, HCI_OP_LE_SET_SCAN_PARAM, sizeof(param_cp),
			    &param_cp);

		memset(&enable_cp, 0, sizeof(enable_cp));
		enable_cp.enable = LE_SCAN_ENABLE;
		enable_cp.filter_dup = filter_dup;
		hci_req_add(req, HCI_OP_LE_SET_SCAN_ENABLE, sizeof(enable_cp),
			    &enable_cp);
	}
}

/* Returns true if an le connection is in the scanning state */
static inline bool hci_is_le_conn_scanning(struct hci_dev *hdev)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn  *c;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &h->list, list) {
		if (c->type == LE_LINK && c->state == BT_CONNECT &&
		    test_bit(HCI_CONN_SCANNING, &c->flags)) {
			rcu_read_unlock();
			return true;
		}
	}

	rcu_read_unlock();

	return false;
}

/* Ensure to call hci_req_add_le_scan_disable() first to disable the
 * controller based address resolution to be able to reconfigure
 * resolving list.
 */
void hci_req_add_le_passive_scan(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	u8 own_addr_type;
	u8 filter_policy;
	u16 window, interval;
	/* Default is to enable duplicates filter */
	u8 filter_dup = LE_SCAN_FILTER_DUP_ENABLE;
	/* Background scanning should run with address resolution */
	bool addr_resolv = true;

	if (hdev->scanning_paused) {
		bt_dev_dbg(hdev, "Scanning is paused for suspend");
		return;
	}

	/* Set require_privacy to false since no SCAN_REQ are send
	 * during passive scanning. Not using an non-resolvable address
	 * here is important so that peer devices using direct
	 * advertising with our address will be correctly reported
	 * by the controller.
	 */
	if (hci_update_random_address(req, false, scan_use_rpa(hdev),
				      &own_addr_type))
		return;

	if (hdev->enable_advmon_interleave_scan &&
	    __hci_update_interleaved_scan(hdev))
		return;

	bt_dev_dbg(hdev, "interleave state %d", hdev->interleave_scan_state);
	/* Adding or removing entries from the accept list must
	 * happen before enabling scanning. The controller does
	 * not allow accept list modification while scanning.
	 */
	filter_policy = update_accept_list(req);

	/* When the controller is using random resolvable addresses and
	 * with that having LE privacy enabled, then controllers with
	 * Extended Scanner Filter Policies support can now enable support
	 * for handling directed advertising.
	 *
	 * So instead of using filter polices 0x00 (no accept list)
	 * and 0x01 (accept list enabled) use the new filter policies
	 * 0x02 (no accept list) and 0x03 (accept list enabled).
	 */
	if (hci_dev_test_flag(hdev, HCI_PRIVACY) &&
	    (hdev->le_features[0] & HCI_LE_EXT_SCAN_POLICY))
		filter_policy |= 0x02;

	if (hdev->suspended) {
		window = hdev->le_scan_window_suspend;
		interval = hdev->le_scan_int_suspend;
	} else if (hci_is_le_conn_scanning(hdev)) {
		window = hdev->le_scan_window_connect;
		interval = hdev->le_scan_int_connect;
	} else if (hci_is_adv_monitoring(hdev)) {
		window = hdev->le_scan_window_adv_monitor;
		interval = hdev->le_scan_int_adv_monitor;

		/* Disable duplicates filter when scanning for advertisement
		 * monitor for the following reasons.
		 *
		 * For HW pattern filtering (ex. MSFT), Realtek and Qualcomm
		 * controllers ignore RSSI_Sampling_Period when the duplicates
		 * filter is enabled.
		 *
		 * For SW pattern filtering, when we're not doing interleaved
		 * scanning, it is necessary to disable duplicates filter,
		 * otherwise hosts can only receive one advertisement and it's
		 * impossible to know if a peer is still in range.
		 */
		filter_dup = LE_SCAN_FILTER_DUP_DISABLE;
	} else {
		window = hdev->le_scan_window;
		interval = hdev->le_scan_interval;
	}

	bt_dev_dbg(hdev, "LE passive scan with accept list = %d",
		   filter_policy);
	hci_req_start_scan(req, LE_SCAN_PASSIVE, interval, window,
			   own_addr_type, filter_policy, filter_dup,
			   addr_resolv);
}

static void cancel_adv_timeout(struct hci_dev *hdev)
{
	if (hdev->adv_instance_timeout) {
		hdev->adv_instance_timeout = 0;
		cancel_delayed_work(&hdev->adv_instance_expire);
	}
}

static bool adv_cur_instance_is_scannable(struct hci_dev *hdev)
{
	return hci_adv_instance_is_scannable(hdev, hdev->cur_adv_instance);
}

void __hci_req_disable_advertising(struct hci_request *req)
{
	if (ext_adv_capable(req->hdev)) {
		__hci_req_disable_ext_adv_instance(req, 0x00);
	} else {
		u8 enable = 0x00;

		hci_req_add(req, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable), &enable);
	}
}

static bool adv_use_rpa(struct hci_dev *hdev, uint32_t flags)
{
	/* If privacy is not enabled don't use RPA */
	if (!hci_dev_test_flag(hdev, HCI_PRIVACY))
		return false;

	/* If basic privacy mode is enabled use RPA */
	if (!hci_dev_test_flag(hdev, HCI_LIMITED_PRIVACY))
		return true;

	/* If limited privacy mode is enabled don't use RPA if we're
	 * both discoverable and bondable.
	 */
	if ((flags & MGMT_ADV_FLAG_DISCOV) &&
	    hci_dev_test_flag(hdev, HCI_BONDABLE))
		return false;

	/* We're neither bondable nor discoverable in the limited
	 * privacy mode, therefore use RPA.
	 */
	return true;
}

static bool is_advertising_allowed(struct hci_dev *hdev, bool connectable)
{
	/* If there is no connection we are OK to advertise. */
	if (hci_conn_num(hdev, LE_LINK) == 0)
		return true;

	/* Check le_states if there is any connection in peripheral role. */
	if (hdev->conn_hash.le_num_peripheral > 0) {
		/* Peripheral connection state and non connectable mode bit 20.
		 */
		if (!connectable && !(hdev->le_states[2] & 0x10))
			return false;

		/* Peripheral connection state and connectable mode bit 38
		 * and scannable bit 21.
		 */
		if (connectable && (!(hdev->le_states[4] & 0x40) ||
				    !(hdev->le_states[2] & 0x20)))
			return false;
	}

	/* Check le_states if there is any connection in central role. */
	if (hci_conn_num(hdev, LE_LINK) != hdev->conn_hash.le_num_peripheral) {
		/* Central connection state and non connectable mode bit 18. */
		if (!connectable && !(hdev->le_states[2] & 0x02))
			return false;

		/* Central connection state and connectable mode bit 35 and
		 * scannable 19.
		 */
		if (connectable && (!(hdev->le_states[4] & 0x08) ||
				    !(hdev->le_states[2] & 0x08)))
			return false;
	}

	return true;
}

void __hci_req_enable_advertising(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct adv_info *adv;
	struct hci_cp_le_set_adv_param cp;
	u8 own_addr_type, enable = 0x01;
	bool connectable;
	u16 adv_min_interval, adv_max_interval;
	u32 flags;

	flags = hci_adv_instance_flags(hdev, hdev->cur_adv_instance);
	adv = hci_find_adv_instance(hdev, hdev->cur_adv_instance);

	/* If the "connectable" instance flag was not set, then choose between
	 * ADV_IND and ADV_NONCONN_IND based on the global connectable setting.
	 */
	connectable = (flags & MGMT_ADV_FLAG_CONNECTABLE) ||
		      mgmt_get_connectable(hdev);

	if (!is_advertising_allowed(hdev, connectable))
		return;

	if (hci_dev_test_flag(hdev, HCI_LE_ADV))
		__hci_req_disable_advertising(req);

	/* Clear the HCI_LE_ADV bit temporarily so that the
	 * hci_update_random_address knows that it's safe to go ahead
	 * and write a new random address. The flag will be set back on
	 * as soon as the SET_ADV_ENABLE HCI command completes.
	 */
	hci_dev_clear_flag(hdev, HCI_LE_ADV);

	/* Set require_privacy to true only when non-connectable
	 * advertising is used. In that case it is fine to use a
	 * non-resolvable private address.
	 */
	if (hci_update_random_address(req, !connectable,
				      adv_use_rpa(hdev, flags),
				      &own_addr_type) < 0)
		return;

	memset(&cp, 0, sizeof(cp));

	if (adv) {
		adv_min_interval = adv->min_interval;
		adv_max_interval = adv->max_interval;
	} else {
		adv_min_interval = hdev->le_adv_min_interval;
		adv_max_interval = hdev->le_adv_max_interval;
	}

	if (connectable) {
		cp.type = LE_ADV_IND;
	} else {
		if (adv_cur_instance_is_scannable(hdev))
			cp.type = LE_ADV_SCAN_IND;
		else
			cp.type = LE_ADV_NONCONN_IND;

		if (!hci_dev_test_flag(hdev, HCI_DISCOVERABLE) ||
		    hci_dev_test_flag(hdev, HCI_LIMITED_DISCOVERABLE)) {
			adv_min_interval = DISCOV_LE_FAST_ADV_INT_MIN;
			adv_max_interval = DISCOV_LE_FAST_ADV_INT_MAX;
		}
	}

	cp.min_interval = cpu_to_le16(adv_min_interval);
	cp.max_interval = cpu_to_le16(adv_max_interval);
	cp.own_address_type = own_addr_type;
	cp.channel_map = hdev->le_adv_channel_map;

	hci_req_add(req, HCI_OP_LE_SET_ADV_PARAM, sizeof(cp), &cp);

	hci_req_add(req, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable), &enable);
}

void __hci_req_update_scan_rsp_data(struct hci_request *req, u8 instance)
{
	struct hci_dev *hdev = req->hdev;
	u8 len;

	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return;

	if (ext_adv_capable(hdev)) {
		struct {
			struct hci_cp_le_set_ext_scan_rsp_data cp;
			u8 data[HCI_MAX_EXT_AD_LENGTH];
		} pdu;

		memset(&pdu, 0, sizeof(pdu));

		len = eir_create_scan_rsp(hdev, instance, pdu.data);

		if (hdev->scan_rsp_data_len == len &&
		    !memcmp(pdu.data, hdev->scan_rsp_data, len))
			return;

		memcpy(hdev->scan_rsp_data, pdu.data, len);
		hdev->scan_rsp_data_len = len;

		pdu.cp.handle = instance;
		pdu.cp.length = len;
		pdu.cp.operation = LE_SET_ADV_DATA_OP_COMPLETE;
		pdu.cp.frag_pref = LE_SET_ADV_DATA_NO_FRAG;

		hci_req_add(req, HCI_OP_LE_SET_EXT_SCAN_RSP_DATA,
			    sizeof(pdu.cp) + len, &pdu.cp);
	} else {
		struct hci_cp_le_set_scan_rsp_data cp;

		memset(&cp, 0, sizeof(cp));

		len = eir_create_scan_rsp(hdev, instance, cp.data);

		if (hdev->scan_rsp_data_len == len &&
		    !memcmp(cp.data, hdev->scan_rsp_data, len))
			return;

		memcpy(hdev->scan_rsp_data, cp.data, sizeof(cp.data));
		hdev->scan_rsp_data_len = len;

		cp.length = len;

		hci_req_add(req, HCI_OP_LE_SET_SCAN_RSP_DATA, sizeof(cp), &cp);
	}
}

void __hci_req_update_adv_data(struct hci_request *req, u8 instance)
{
	struct hci_dev *hdev = req->hdev;
	u8 len;

	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return;

	if (ext_adv_capable(hdev)) {
		struct {
			struct hci_cp_le_set_ext_adv_data cp;
			u8 data[HCI_MAX_EXT_AD_LENGTH];
		} pdu;

		memset(&pdu, 0, sizeof(pdu));

		len = eir_create_adv_data(hdev, instance, pdu.data);

		/* There's nothing to do if the data hasn't changed */
		if (hdev->adv_data_len == len &&
		    memcmp(pdu.data, hdev->adv_data, len) == 0)
			return;

		memcpy(hdev->adv_data, pdu.data, len);
		hdev->adv_data_len = len;

		pdu.cp.length = len;
		pdu.cp.handle = instance;
		pdu.cp.operation = LE_SET_ADV_DATA_OP_COMPLETE;
		pdu.cp.frag_pref = LE_SET_ADV_DATA_NO_FRAG;

		hci_req_add(req, HCI_OP_LE_SET_EXT_ADV_DATA,
			    sizeof(pdu.cp) + len, &pdu.cp);
	} else {
		struct hci_cp_le_set_adv_data cp;

		memset(&cp, 0, sizeof(cp));

		len = eir_create_adv_data(hdev, instance, cp.data);

		/* There's nothing to do if the data hasn't changed */
		if (hdev->adv_data_len == len &&
		    memcmp(cp.data, hdev->adv_data, len) == 0)
			return;

		memcpy(hdev->adv_data, cp.data, sizeof(cp.data));
		hdev->adv_data_len = len;

		cp.length = len;

		hci_req_add(req, HCI_OP_LE_SET_ADV_DATA, sizeof(cp), &cp);
	}
}

int hci_req_update_adv_data(struct hci_dev *hdev, u8 instance)
{
	struct hci_request req;

	hci_req_init(&req, hdev);
	__hci_req_update_adv_data(&req, instance);

	return hci_req_run(&req, NULL);
}

static void enable_addr_resolution_complete(struct hci_dev *hdev, u8 status,
					    u16 opcode)
{
	BT_DBG("%s status %u", hdev->name, status);
}

void hci_req_disable_address_resolution(struct hci_dev *hdev)
{
	struct hci_request req;
	__u8 enable = 0x00;

	if (!hci_dev_test_flag(hdev, HCI_LL_RPA_RESOLUTION))
		return;

	hci_req_init(&req, hdev);

	hci_req_add(&req, HCI_OP_LE_SET_ADDR_RESOLV_ENABLE, 1, &enable);

	hci_req_run(&req, enable_addr_resolution_complete);
}

static void adv_enable_complete(struct hci_dev *hdev, u8 status, u16 opcode)
{
	bt_dev_dbg(hdev, "status %u", status);
}

void hci_req_reenable_advertising(struct hci_dev *hdev)
{
	struct hci_request req;

	if (!hci_dev_test_flag(hdev, HCI_ADVERTISING) &&
	    list_empty(&hdev->adv_instances))
		return;

	hci_req_init(&req, hdev);

	if (hdev->cur_adv_instance) {
		__hci_req_schedule_adv_instance(&req, hdev->cur_adv_instance,
						true);
	} else {
		if (ext_adv_capable(hdev)) {
			__hci_req_start_ext_adv(&req, 0x00);
		} else {
			__hci_req_update_adv_data(&req, 0x00);
			__hci_req_update_scan_rsp_data(&req, 0x00);
			__hci_req_enable_advertising(&req);
		}
	}

	hci_req_run(&req, adv_enable_complete);
}

static void adv_timeout_expire(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    adv_instance_expire.work);

	struct hci_request req;
	u8 instance;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	hdev->adv_instance_timeout = 0;

	instance = hdev->cur_adv_instance;
	if (instance == 0x00)
		goto unlock;

	hci_req_init(&req, hdev);

	hci_req_clear_adv_instance(hdev, NULL, &req, instance, false);

	if (list_empty(&hdev->adv_instances))
		__hci_req_disable_advertising(&req);

	hci_req_run(&req, NULL);

unlock:
	hci_dev_unlock(hdev);
}

static int hci_req_add_le_interleaved_scan(struct hci_request *req,
					   unsigned long opt)
{
	struct hci_dev *hdev = req->hdev;
	int ret = 0;

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_LE_SCAN))
		hci_req_add_le_scan_disable(req, false);
	hci_req_add_le_passive_scan(req);

	switch (hdev->interleave_scan_state) {
	case INTERLEAVE_SCAN_ALLOWLIST:
		bt_dev_dbg(hdev, "next state: allowlist");
		hdev->interleave_scan_state = INTERLEAVE_SCAN_NO_FILTER;
		break;
	case INTERLEAVE_SCAN_NO_FILTER:
		bt_dev_dbg(hdev, "next state: no filter");
		hdev->interleave_scan_state = INTERLEAVE_SCAN_ALLOWLIST;
		break;
	case INTERLEAVE_SCAN_NONE:
		BT_ERR("unexpected error");
		ret = -1;
	}

	hci_dev_unlock(hdev);

	return ret;
}

static void interleave_scan_work(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    interleave_scan.work);
	u8 status;
	unsigned long timeout;

	if (hdev->interleave_scan_state == INTERLEAVE_SCAN_ALLOWLIST) {
		timeout = msecs_to_jiffies(hdev->advmon_allowlist_duration);
	} else if (hdev->interleave_scan_state == INTERLEAVE_SCAN_NO_FILTER) {
		timeout = msecs_to_jiffies(hdev->advmon_no_filter_duration);
	} else {
		bt_dev_err(hdev, "unexpected error");
		return;
	}

	hci_req_sync(hdev, hci_req_add_le_interleaved_scan, 0,
		     HCI_CMD_TIMEOUT, &status);

	/* Don't continue interleaving if it was canceled */
	if (is_interleave_scanning(hdev))
		queue_delayed_work(hdev->req_workqueue,
				   &hdev->interleave_scan, timeout);
}

int hci_get_random_address(struct hci_dev *hdev, bool require_privacy,
			   bool use_rpa, struct adv_info *adv_instance,
			   u8 *own_addr_type, bdaddr_t *rand_addr)
{
	int err;

	bacpy(rand_addr, BDADDR_ANY);

	/* If privacy is enabled use a resolvable private address. If
	 * current RPA has expired then generate a new one.
	 */
	if (use_rpa) {
		/* If Controller supports LL Privacy use own address type is
		 * 0x03
		 */
		if (use_ll_privacy(hdev))
			*own_addr_type = ADDR_LE_DEV_RANDOM_RESOLVED;
		else
			*own_addr_type = ADDR_LE_DEV_RANDOM;

		if (adv_instance) {
			if (adv_rpa_valid(adv_instance))
				return 0;
		} else {
			if (rpa_valid(hdev))
				return 0;
		}

		err = smp_generate_rpa(hdev, hdev->irk, &hdev->rpa);
		if (err < 0) {
			bt_dev_err(hdev, "failed to generate new RPA");
			return err;
		}

		bacpy(rand_addr, &hdev->rpa);

		return 0;
	}

	/* In case of required privacy without resolvable private address,
	 * use an non-resolvable private address. This is useful for
	 * non-connectable advertising.
	 */
	if (require_privacy) {
		bdaddr_t nrpa;

		while (true) {
			/* The non-resolvable private address is generated
			 * from random six bytes with the two most significant
			 * bits cleared.
			 */
			get_random_bytes(&nrpa, 6);
			nrpa.b[5] &= 0x3f;

			/* The non-resolvable private address shall not be
			 * equal to the public address.
			 */
			if (bacmp(&hdev->bdaddr, &nrpa))
				break;
		}

		*own_addr_type = ADDR_LE_DEV_RANDOM;
		bacpy(rand_addr, &nrpa);

		return 0;
	}

	/* No privacy so use a public address. */
	*own_addr_type = ADDR_LE_DEV_PUBLIC;

	return 0;
}

void __hci_req_clear_ext_adv_sets(struct hci_request *req)
{
	hci_req_add(req, HCI_OP_LE_CLEAR_ADV_SETS, 0, NULL);
}

static void set_random_addr(struct hci_request *req, bdaddr_t *rpa)
{
	struct hci_dev *hdev = req->hdev;

	/* If we're advertising or initiating an LE connection we can't
	 * go ahead and change the random address at this time. This is
	 * because the eventual initiator address used for the
	 * subsequently created connection will be undefined (some
	 * controllers use the new address and others the one we had
	 * when the operation started).
	 *
	 * In this kind of scenario skip the update and let the random
	 * address be updated at the next cycle.
	 */
	if (hci_dev_test_flag(hdev, HCI_LE_ADV) ||
	    hci_lookup_le_connect(hdev)) {
		bt_dev_dbg(hdev, "Deferring random address update");
		hci_dev_set_flag(hdev, HCI_RPA_EXPIRED);
		return;
	}

	hci_req_add(req, HCI_OP_LE_SET_RANDOM_ADDR, 6, rpa);
}

int __hci_req_setup_ext_adv_instance(struct hci_request *req, u8 instance)
{
	struct hci_cp_le_set_ext_adv_params cp;
	struct hci_dev *hdev = req->hdev;
	bool connectable;
	u32 flags;
	bdaddr_t random_addr;
	u8 own_addr_type;
	int err;
	struct adv_info *adv;
	bool secondary_adv, require_privacy;

	if (instance > 0) {
		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return -EINVAL;
	} else {
		adv = NULL;
	}

	flags = hci_adv_instance_flags(hdev, instance);

	/* If the "connectable" instance flag was not set, then choose between
	 * ADV_IND and ADV_NONCONN_IND based on the global connectable setting.
	 */
	connectable = (flags & MGMT_ADV_FLAG_CONNECTABLE) ||
		      mgmt_get_connectable(hdev);

	if (!is_advertising_allowed(hdev, connectable))
		return -EPERM;

	/* Set require_privacy to true only when non-connectable
	 * advertising is used. In that case it is fine to use a
	 * non-resolvable private address.
	 */
	require_privacy = !connectable;

	/* Don't require privacy for periodic adv? */
	if (adv && adv->periodic)
		require_privacy = false;

	err = hci_get_random_address(hdev, require_privacy,
				     adv_use_rpa(hdev, flags), adv,
				     &own_addr_type, &random_addr);
	if (err < 0)
		return err;

	memset(&cp, 0, sizeof(cp));

	if (adv) {
		hci_cpu_to_le24(adv->min_interval, cp.min_interval);
		hci_cpu_to_le24(adv->max_interval, cp.max_interval);
		cp.tx_power = adv->tx_power;
	} else {
		hci_cpu_to_le24(hdev->le_adv_min_interval, cp.min_interval);
		hci_cpu_to_le24(hdev->le_adv_max_interval, cp.max_interval);
		cp.tx_power = HCI_ADV_TX_POWER_NO_PREFERENCE;
	}

	secondary_adv = (flags & MGMT_ADV_FLAG_SEC_MASK);

	if (connectable) {
		if (secondary_adv)
			cp.evt_properties = cpu_to_le16(LE_EXT_ADV_CONN_IND);
		else
			cp.evt_properties = cpu_to_le16(LE_LEGACY_ADV_IND);
	} else if (hci_adv_instance_is_scannable(hdev, instance) ||
		   (flags & MGMT_ADV_PARAM_SCAN_RSP)) {
		if (secondary_adv)
			cp.evt_properties = cpu_to_le16(LE_EXT_ADV_SCAN_IND);
		else
			cp.evt_properties = cpu_to_le16(LE_LEGACY_ADV_SCAN_IND);
	} else {
		/* Secondary and periodic cannot use legacy PDUs */
		if (secondary_adv || (adv && adv->periodic))
			cp.evt_properties = cpu_to_le16(LE_EXT_ADV_NON_CONN_IND);
		else
			cp.evt_properties = cpu_to_le16(LE_LEGACY_NONCONN_IND);
	}

	cp.own_addr_type = own_addr_type;
	cp.channel_map = hdev->le_adv_channel_map;
	cp.handle = instance;

	if (flags & MGMT_ADV_FLAG_SEC_2M) {
		cp.primary_phy = HCI_ADV_PHY_1M;
		cp.secondary_phy = HCI_ADV_PHY_2M;
	} else if (flags & MGMT_ADV_FLAG_SEC_CODED) {
		cp.primary_phy = HCI_ADV_PHY_CODED;
		cp.secondary_phy = HCI_ADV_PHY_CODED;
	} else {
		/* In all other cases use 1M */
		cp.primary_phy = HCI_ADV_PHY_1M;
		cp.secondary_phy = HCI_ADV_PHY_1M;
	}

	hci_req_add(req, HCI_OP_LE_SET_EXT_ADV_PARAMS, sizeof(cp), &cp);

	if ((own_addr_type == ADDR_LE_DEV_RANDOM ||
	     own_addr_type == ADDR_LE_DEV_RANDOM_RESOLVED) &&
	    bacmp(&random_addr, BDADDR_ANY)) {
		struct hci_cp_le_set_adv_set_rand_addr cp;

		/* Check if random address need to be updated */
		if (adv) {
			if (!bacmp(&random_addr, &adv->random_addr))
				return 0;
		} else {
			if (!bacmp(&random_addr, &hdev->random_addr))
				return 0;
			/* Instance 0x00 doesn't have an adv_info, instead it
			 * uses hdev->random_addr to track its address so
			 * whenever it needs to be updated this also set the
			 * random address since hdev->random_addr is shared with
			 * scan state machine.
			 */
			set_random_addr(req, &random_addr);
		}

		memset(&cp, 0, sizeof(cp));

		cp.handle = instance;
		bacpy(&cp.bdaddr, &random_addr);

		hci_req_add(req,
			    HCI_OP_LE_SET_ADV_SET_RAND_ADDR,
			    sizeof(cp), &cp);
	}

	return 0;
}

int __hci_req_enable_ext_advertising(struct hci_request *req, u8 instance)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_le_set_ext_adv_enable *cp;
	struct hci_cp_ext_adv_set *adv_set;
	u8 data[sizeof(*cp) + sizeof(*adv_set) * 1];
	struct adv_info *adv_instance;

	if (instance > 0) {
		adv_instance = hci_find_adv_instance(hdev, instance);
		if (!adv_instance)
			return -EINVAL;
	} else {
		adv_instance = NULL;
	}

	cp = (void *) data;
	adv_set = (void *) cp->data;

	memset(cp, 0, sizeof(*cp));

	cp->enable = 0x01;
	cp->num_of_sets = 0x01;

	memset(adv_set, 0, sizeof(*adv_set));

	adv_set->handle = instance;

	/* Set duration per instance since controller is responsible for
	 * scheduling it.
	 */
	if (adv_instance && adv_instance->duration) {
		u16 duration = adv_instance->timeout * MSEC_PER_SEC;

		/* Time = N * 10 ms */
		adv_set->duration = cpu_to_le16(duration / 10);
	}

	hci_req_add(req, HCI_OP_LE_SET_EXT_ADV_ENABLE,
		    sizeof(*cp) + sizeof(*adv_set) * cp->num_of_sets,
		    data);

	return 0;
}

int __hci_req_disable_ext_adv_instance(struct hci_request *req, u8 instance)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_le_set_ext_adv_enable *cp;
	struct hci_cp_ext_adv_set *adv_set;
	u8 data[sizeof(*cp) + sizeof(*adv_set) * 1];
	u8 req_size;

	/* If request specifies an instance that doesn't exist, fail */
	if (instance > 0 && !hci_find_adv_instance(hdev, instance))
		return -EINVAL;

	memset(data, 0, sizeof(data));

	cp = (void *)data;
	adv_set = (void *)cp->data;

	/* Instance 0x00 indicates all advertising instances will be disabled */
	cp->num_of_sets = !!instance;
	cp->enable = 0x00;

	adv_set->handle = instance;

	req_size = sizeof(*cp) + sizeof(*adv_set) * cp->num_of_sets;
	hci_req_add(req, HCI_OP_LE_SET_EXT_ADV_ENABLE, req_size, data);

	return 0;
}

int __hci_req_remove_ext_adv_instance(struct hci_request *req, u8 instance)
{
	struct hci_dev *hdev = req->hdev;

	/* If request specifies an instance that doesn't exist, fail */
	if (instance > 0 && !hci_find_adv_instance(hdev, instance))
		return -EINVAL;

	hci_req_add(req, HCI_OP_LE_REMOVE_ADV_SET, sizeof(instance), &instance);

	return 0;
}

int __hci_req_start_ext_adv(struct hci_request *req, u8 instance)
{
	struct hci_dev *hdev = req->hdev;
	struct adv_info *adv_instance = hci_find_adv_instance(hdev, instance);
	int err;

	/* If instance isn't pending, the chip knows about it, and it's safe to
	 * disable
	 */
	if (adv_instance && !adv_instance->pending)
		__hci_req_disable_ext_adv_instance(req, instance);

	err = __hci_req_setup_ext_adv_instance(req, instance);
	if (err < 0)
		return err;

	__hci_req_update_scan_rsp_data(req, instance);
	__hci_req_enable_ext_advertising(req, instance);

	return 0;
}

int __hci_req_schedule_adv_instance(struct hci_request *req, u8 instance,
				    bool force)
{
	struct hci_dev *hdev = req->hdev;
	struct adv_info *adv_instance = NULL;
	u16 timeout;

	if (hci_dev_test_flag(hdev, HCI_ADVERTISING) ||
	    list_empty(&hdev->adv_instances))
		return -EPERM;

	if (hdev->adv_instance_timeout)
		return -EBUSY;

	adv_instance = hci_find_adv_instance(hdev, instance);
	if (!adv_instance)
		return -ENOENT;

	/* A zero timeout means unlimited advertising. As long as there is
	 * only one instance, duration should be ignored. We still set a timeout
	 * in case further instances are being added later on.
	 *
	 * If the remaining lifetime of the instance is more than the duration
	 * then the timeout corresponds to the duration, otherwise it will be
	 * reduced to the remaining instance lifetime.
	 */
	if (adv_instance->timeout == 0 ||
	    adv_instance->duration <= adv_instance->remaining_time)
		timeout = adv_instance->duration;
	else
		timeout = adv_instance->remaining_time;

	/* The remaining time is being reduced unless the instance is being
	 * advertised without time limit.
	 */
	if (adv_instance->timeout)
		adv_instance->remaining_time =
				adv_instance->remaining_time - timeout;

	/* Only use work for scheduling instances with legacy advertising */
	if (!ext_adv_capable(hdev)) {
		hdev->adv_instance_timeout = timeout;
		queue_delayed_work(hdev->req_workqueue,
			   &hdev->adv_instance_expire,
			   msecs_to_jiffies(timeout * 1000));
	}

	/* If we're just re-scheduling the same instance again then do not
	 * execute any HCI commands. This happens when a single instance is
	 * being advertised.
	 */
	if (!force && hdev->cur_adv_instance == instance &&
	    hci_dev_test_flag(hdev, HCI_LE_ADV))
		return 0;

	hdev->cur_adv_instance = instance;
	if (ext_adv_capable(hdev)) {
		__hci_req_start_ext_adv(req, instance);
	} else {
		__hci_req_update_adv_data(req, instance);
		__hci_req_update_scan_rsp_data(req, instance);
		__hci_req_enable_advertising(req);
	}

	return 0;
}

/* For a single instance:
 * - force == true: The instance will be removed even when its remaining
 *   lifetime is not zero.
 * - force == false: the instance will be deactivated but kept stored unless
 *   the remaining lifetime is zero.
 *
 * For instance == 0x00:
 * - force == true: All instances will be removed regardless of their timeout
 *   setting.
 * - force == false: Only instances that have a timeout will be removed.
 */
void hci_req_clear_adv_instance(struct hci_dev *hdev, struct sock *sk,
				struct hci_request *req, u8 instance,
				bool force)
{
	struct adv_info *adv_instance, *n, *next_instance = NULL;
	int err;
	u8 rem_inst;

	/* Cancel any timeout concerning the removed instance(s). */
	if (!instance || hdev->cur_adv_instance == instance)
		cancel_adv_timeout(hdev);

	/* Get the next instance to advertise BEFORE we remove
	 * the current one. This can be the same instance again
	 * if there is only one instance.
	 */
	if (instance && hdev->cur_adv_instance == instance)
		next_instance = hci_get_next_instance(hdev, instance);

	if (instance == 0x00) {
		list_for_each_entry_safe(adv_instance, n, &hdev->adv_instances,
					 list) {
			if (!(force || adv_instance->timeout))
				continue;

			rem_inst = adv_instance->instance;
			err = hci_remove_adv_instance(hdev, rem_inst);
			if (!err)
				mgmt_advertising_removed(sk, hdev, rem_inst);
		}
	} else {
		adv_instance = hci_find_adv_instance(hdev, instance);

		if (force || (adv_instance && adv_instance->timeout &&
			      !adv_instance->remaining_time)) {
			/* Don't advertise a removed instance. */
			if (next_instance &&
			    next_instance->instance == instance)
				next_instance = NULL;

			err = hci_remove_adv_instance(hdev, instance);
			if (!err)
				mgmt_advertising_removed(sk, hdev, instance);
		}
	}

	if (!req || !hdev_is_powered(hdev) ||
	    hci_dev_test_flag(hdev, HCI_ADVERTISING))
		return;

	if (next_instance && !ext_adv_capable(hdev))
		__hci_req_schedule_adv_instance(req, next_instance->instance,
						false);
}

int hci_update_random_address(struct hci_request *req, bool require_privacy,
			      bool use_rpa, u8 *own_addr_type)
{
	struct hci_dev *hdev = req->hdev;
	int err;

	/* If privacy is enabled use a resolvable private address. If
	 * current RPA has expired or there is something else than
	 * the current RPA in use, then generate a new one.
	 */
	if (use_rpa) {
		/* If Controller supports LL Privacy use own address type is
		 * 0x03
		 */
		if (use_ll_privacy(hdev))
			*own_addr_type = ADDR_LE_DEV_RANDOM_RESOLVED;
		else
			*own_addr_type = ADDR_LE_DEV_RANDOM;

		if (rpa_valid(hdev))
			return 0;

		err = smp_generate_rpa(hdev, hdev->irk, &hdev->rpa);
		if (err < 0) {
			bt_dev_err(hdev, "failed to generate new RPA");
			return err;
		}

		set_random_addr(req, &hdev->rpa);

		return 0;
	}

	/* In case of required privacy without resolvable private address,
	 * use an non-resolvable private address. This is useful for active
	 * scanning and non-connectable advertising.
	 */
	if (require_privacy) {
		bdaddr_t nrpa;

		while (true) {
			/* The non-resolvable private address is generated
			 * from random six bytes with the two most significant
			 * bits cleared.
			 */
			get_random_bytes(&nrpa, 6);
			nrpa.b[5] &= 0x3f;

			/* The non-resolvable private address shall not be
			 * equal to the public address.
			 */
			if (bacmp(&hdev->bdaddr, &nrpa))
				break;
		}

		*own_addr_type = ADDR_LE_DEV_RANDOM;
		set_random_addr(req, &nrpa);
		return 0;
	}

	/* If forcing static address is in use or there is no public
	 * address use the static address as random address (but skip
	 * the HCI command if the current random address is already the
	 * static one.
	 *
	 * In case BR/EDR has been disabled on a dual-mode controller
	 * and a static address has been configured, then use that
	 * address instead of the public BR/EDR address.
	 */
	if (hci_dev_test_flag(hdev, HCI_FORCE_STATIC_ADDR) ||
	    !bacmp(&hdev->bdaddr, BDADDR_ANY) ||
	    (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) &&
	     bacmp(&hdev->static_addr, BDADDR_ANY))) {
		*own_addr_type = ADDR_LE_DEV_RANDOM;
		if (bacmp(&hdev->static_addr, &hdev->random_addr))
			hci_req_add(req, HCI_OP_LE_SET_RANDOM_ADDR, 6,
				    &hdev->static_addr);
		return 0;
	}

	/* Neither privacy nor static address is being used so use a
	 * public address.
	 */
	*own_addr_type = ADDR_LE_DEV_PUBLIC;

	return 0;
}

static bool disconnected_accept_list_entries(struct hci_dev *hdev)
{
	struct bdaddr_list *b;

	list_for_each_entry(b, &hdev->accept_list, list) {
		struct hci_conn *conn;

		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &b->bdaddr);
		if (!conn)
			return true;

		if (conn->state != BT_CONNECTED && conn->state != BT_CONFIG)
			return true;
	}

	return false;
}

void __hci_req_update_scan(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	u8 scan;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return;

	if (!hdev_is_powered(hdev))
		return;

	if (mgmt_powering_down(hdev))
		return;

	if (hdev->scanning_paused)
		return;

	if (hci_dev_test_flag(hdev, HCI_CONNECTABLE) ||
	    disconnected_accept_list_entries(hdev))
		scan = SCAN_PAGE;
	else
		scan = SCAN_DISABLED;

	if (hci_dev_test_flag(hdev, HCI_DISCOVERABLE))
		scan |= SCAN_INQUIRY;

	if (test_bit(HCI_PSCAN, &hdev->flags) == !!(scan & SCAN_PAGE) &&
	    test_bit(HCI_ISCAN, &hdev->flags) == !!(scan & SCAN_INQUIRY))
		return;

	hci_req_add(req, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
}

static u8 get_service_classes(struct hci_dev *hdev)
{
	struct bt_uuid *uuid;
	u8 val = 0;

	list_for_each_entry(uuid, &hdev->uuids, list)
		val |= uuid->svc_hint;

	return val;
}

void __hci_req_update_class(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	u8 cod[3];

	bt_dev_dbg(hdev, "");

	if (!hdev_is_powered(hdev))
		return;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return;

	if (hci_dev_test_flag(hdev, HCI_SERVICE_CACHE))
		return;

	cod[0] = hdev->minor_class;
	cod[1] = hdev->major_class;
	cod[2] = get_service_classes(hdev);

	if (hci_dev_test_flag(hdev, HCI_LIMITED_DISCOVERABLE))
		cod[1] |= 0x20;

	if (memcmp(cod, hdev->dev_class, 3) == 0)
		return;

	hci_req_add(req, HCI_OP_WRITE_CLASS_OF_DEV, sizeof(cod), cod);
}

void __hci_abort_conn(struct hci_request *req, struct hci_conn *conn,
		      u8 reason)
{
	switch (conn->state) {
	case BT_CONNECTED:
	case BT_CONFIG:
		if (conn->type == AMP_LINK) {
			struct hci_cp_disconn_phy_link cp;

			cp.phy_handle = HCI_PHY_HANDLE(conn->handle);
			cp.reason = reason;
			hci_req_add(req, HCI_OP_DISCONN_PHY_LINK, sizeof(cp),
				    &cp);
		} else {
			struct hci_cp_disconnect dc;

			dc.handle = cpu_to_le16(conn->handle);
			dc.reason = reason;
			hci_req_add(req, HCI_OP_DISCONNECT, sizeof(dc), &dc);
		}

		conn->state = BT_DISCONN;

		break;
	case BT_CONNECT:
		if (conn->type == LE_LINK) {
			if (test_bit(HCI_CONN_SCANNING, &conn->flags))
				break;
			hci_req_add(req, HCI_OP_LE_CREATE_CONN_CANCEL,
				    0, NULL);
		} else if (conn->type == ACL_LINK) {
			if (req->hdev->hci_ver < BLUETOOTH_VER_1_2)
				break;
			hci_req_add(req, HCI_OP_CREATE_CONN_CANCEL,
				    6, &conn->dst);
		}
		break;
	case BT_CONNECT2:
		if (conn->type == ACL_LINK) {
			struct hci_cp_reject_conn_req rej;

			bacpy(&rej.bdaddr, &conn->dst);
			rej.reason = reason;

			hci_req_add(req, HCI_OP_REJECT_CONN_REQ,
				    sizeof(rej), &rej);
		} else if (conn->type == SCO_LINK || conn->type == ESCO_LINK) {
			struct hci_cp_reject_sync_conn_req rej;

			bacpy(&rej.bdaddr, &conn->dst);

			/* SCO rejection has its own limited set of
			 * allowed error values (0x0D-0x0F) which isn't
			 * compatible with most values passed to this
			 * function. To be safe hard-code one of the
			 * values that's suitable for SCO.
			 */
			rej.reason = HCI_ERROR_REJ_LIMITED_RESOURCES;

			hci_req_add(req, HCI_OP_REJECT_SYNC_CONN_REQ,
				    sizeof(rej), &rej);
		}
		break;
	default:
		conn->state = BT_CLOSED;
		break;
	}
}

static void abort_conn_complete(struct hci_dev *hdev, u8 status, u16 opcode)
{
	if (status)
		bt_dev_dbg(hdev, "Failed to abort connection: status 0x%2.2x", status);
}

int hci_abort_conn(struct hci_conn *conn, u8 reason)
{
	struct hci_request req;
	int err;

	hci_req_init(&req, conn->hdev);

	__hci_abort_conn(&req, conn, reason);

	err = hci_req_run(&req, abort_conn_complete);
	if (err && err != -ENODATA) {
		bt_dev_err(conn->hdev, "failed to run HCI request: err %d", err);
		return err;
	}

	return 0;
}

static int le_scan_disable(struct hci_request *req, unsigned long opt)
{
	hci_req_add_le_scan_disable(req, false);
	return 0;
}

static int bredr_inquiry(struct hci_request *req, unsigned long opt)
{
	u8 length = opt;
	const u8 giac[3] = { 0x33, 0x8b, 0x9e };
	const u8 liac[3] = { 0x00, 0x8b, 0x9e };
	struct hci_cp_inquiry cp;

	if (test_bit(HCI_INQUIRY, &req->hdev->flags))
		return 0;

	bt_dev_dbg(req->hdev, "");

	hci_dev_lock(req->hdev);
	hci_inquiry_cache_flush(req->hdev);
	hci_dev_unlock(req->hdev);

	memset(&cp, 0, sizeof(cp));

	if (req->hdev->discovery.limited)
		memcpy(&cp.lap, liac, sizeof(cp.lap));
	else
		memcpy(&cp.lap, giac, sizeof(cp.lap));

	cp.length = length;

	hci_req_add(req, HCI_OP_INQUIRY, sizeof(cp), &cp);

	return 0;
}

static void le_scan_disable_work(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    le_scan_disable.work);
	u8 status;

	bt_dev_dbg(hdev, "");

	if (!hci_dev_test_flag(hdev, HCI_LE_SCAN))
		return;

	cancel_delayed_work(&hdev->le_scan_restart);

	hci_req_sync(hdev, le_scan_disable, 0, HCI_CMD_TIMEOUT, &status);
	if (status) {
		bt_dev_err(hdev, "failed to disable LE scan: status 0x%02x",
			   status);
		return;
	}

	hdev->discovery.scan_start = 0;

	/* If we were running LE only scan, change discovery state. If
	 * we were running both LE and BR/EDR inquiry simultaneously,
	 * and BR/EDR inquiry is already finished, stop discovery,
	 * otherwise BR/EDR inquiry will stop discovery when finished.
	 * If we will resolve remote device name, do not change
	 * discovery state.
	 */

	if (hdev->discovery.type == DISCOV_TYPE_LE)
		goto discov_stopped;

	if (hdev->discovery.type != DISCOV_TYPE_INTERLEAVED)
		return;

	if (test_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks)) {
		if (!test_bit(HCI_INQUIRY, &hdev->flags) &&
		    hdev->discovery.state != DISCOVERY_RESOLVING)
			goto discov_stopped;

		return;
	}

	hci_req_sync(hdev, bredr_inquiry, DISCOV_INTERLEAVED_INQUIRY_LEN,
		     HCI_CMD_TIMEOUT, &status);
	if (status) {
		bt_dev_err(hdev, "inquiry failed: status 0x%02x", status);
		goto discov_stopped;
	}

	return;

discov_stopped:
	hci_dev_lock(hdev);
	hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
	hci_dev_unlock(hdev);
}

static int le_scan_restart(struct hci_request *req, unsigned long opt)
{
	struct hci_dev *hdev = req->hdev;

	/* If controller is not scanning we are done. */
	if (!hci_dev_test_flag(hdev, HCI_LE_SCAN))
		return 0;

	if (hdev->scanning_paused) {
		bt_dev_dbg(hdev, "Scanning is paused for suspend");
		return 0;
	}

	hci_req_add_le_scan_disable(req, false);

	if (use_ext_scan(hdev)) {
		struct hci_cp_le_set_ext_scan_enable ext_enable_cp;

		memset(&ext_enable_cp, 0, sizeof(ext_enable_cp));
		ext_enable_cp.enable = LE_SCAN_ENABLE;
		ext_enable_cp.filter_dup = LE_SCAN_FILTER_DUP_ENABLE;

		hci_req_add(req, HCI_OP_LE_SET_EXT_SCAN_ENABLE,
			    sizeof(ext_enable_cp), &ext_enable_cp);
	} else {
		struct hci_cp_le_set_scan_enable cp;

		memset(&cp, 0, sizeof(cp));
		cp.enable = LE_SCAN_ENABLE;
		cp.filter_dup = LE_SCAN_FILTER_DUP_ENABLE;
		hci_req_add(req, HCI_OP_LE_SET_SCAN_ENABLE, sizeof(cp), &cp);
	}

	return 0;
}

static void le_scan_restart_work(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    le_scan_restart.work);
	unsigned long timeout, duration, scan_start, now;
	u8 status;

	bt_dev_dbg(hdev, "");

	hci_req_sync(hdev, le_scan_restart, 0, HCI_CMD_TIMEOUT, &status);
	if (status) {
		bt_dev_err(hdev, "failed to restart LE scan: status %d",
			   status);
		return;
	}

	hci_dev_lock(hdev);

	if (!test_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks) ||
	    !hdev->discovery.scan_start)
		goto unlock;

	/* When the scan was started, hdev->le_scan_disable has been queued
	 * after duration from scan_start. During scan restart this job
	 * has been canceled, and we need to queue it again after proper
	 * timeout, to make sure that scan does not run indefinitely.
	 */
	duration = hdev->discovery.scan_duration;
	scan_start = hdev->discovery.scan_start;
	now = jiffies;
	if (now - scan_start <= duration) {
		int elapsed;

		if (now >= scan_start)
			elapsed = now - scan_start;
		else
			elapsed = ULONG_MAX - scan_start + now;

		timeout = duration - elapsed;
	} else {
		timeout = 0;
	}

	queue_delayed_work(hdev->req_workqueue,
			   &hdev->le_scan_disable, timeout);

unlock:
	hci_dev_unlock(hdev);
}

bool hci_req_stop_discovery(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct discovery_state *d = &hdev->discovery;
	struct hci_cp_remote_name_req_cancel cp;
	struct inquiry_entry *e;
	bool ret = false;

	bt_dev_dbg(hdev, "state %u", hdev->discovery.state);

	if (d->state == DISCOVERY_FINDING || d->state == DISCOVERY_STOPPING) {
		if (test_bit(HCI_INQUIRY, &hdev->flags))
			hci_req_add(req, HCI_OP_INQUIRY_CANCEL, 0, NULL);

		if (hci_dev_test_flag(hdev, HCI_LE_SCAN)) {
			cancel_delayed_work(&hdev->le_scan_disable);
			cancel_delayed_work(&hdev->le_scan_restart);
			hci_req_add_le_scan_disable(req, false);
		}

		ret = true;
	} else {
		/* Passive scanning */
		if (hci_dev_test_flag(hdev, HCI_LE_SCAN)) {
			hci_req_add_le_scan_disable(req, false);
			ret = true;
		}
	}

	/* No further actions needed for LE-only discovery */
	if (d->type == DISCOV_TYPE_LE)
		return ret;

	if (d->state == DISCOVERY_RESOLVING || d->state == DISCOVERY_STOPPING) {
		e = hci_inquiry_cache_lookup_resolve(hdev, BDADDR_ANY,
						     NAME_PENDING);
		if (!e)
			return ret;

		bacpy(&cp.bdaddr, &e->data.bdaddr);
		hci_req_add(req, HCI_OP_REMOTE_NAME_REQ_CANCEL, sizeof(cp),
			    &cp);
		ret = true;
	}

	return ret;
}

static void config_data_path_complete(struct hci_dev *hdev, u8 status,
				      u16 opcode)
{
	bt_dev_dbg(hdev, "status %u", status);
}

int hci_req_configure_datapath(struct hci_dev *hdev, struct bt_codec *codec)
{
	struct hci_request req;
	int err;
	__u8 vnd_len, *vnd_data = NULL;
	struct hci_op_configure_data_path *cmd = NULL;

	hci_req_init(&req, hdev);

	err = hdev->get_codec_config_data(hdev, ESCO_LINK, codec, &vnd_len,
					  &vnd_data);
	if (err < 0)
		goto error;

	cmd = kzalloc(sizeof(*cmd) + vnd_len, GFP_KERNEL);
	if (!cmd) {
		err = -ENOMEM;
		goto error;
	}

	err = hdev->get_data_path_id(hdev, &cmd->data_path_id);
	if (err < 0)
		goto error;

	cmd->vnd_len = vnd_len;
	memcpy(cmd->vnd_data, vnd_data, vnd_len);

	cmd->direction = 0x00;
	hci_req_add(&req, HCI_CONFIGURE_DATA_PATH, sizeof(*cmd) + vnd_len, cmd);

	cmd->direction = 0x01;
	hci_req_add(&req, HCI_CONFIGURE_DATA_PATH, sizeof(*cmd) + vnd_len, cmd);

	err = hci_req_run(&req, config_data_path_complete);
error:

	kfree(cmd);
	kfree(vnd_data);
	return err;
}

void hci_request_setup(struct hci_dev *hdev)
{
	INIT_DELAYED_WORK(&hdev->le_scan_disable, le_scan_disable_work);
	INIT_DELAYED_WORK(&hdev->le_scan_restart, le_scan_restart_work);
	INIT_DELAYED_WORK(&hdev->adv_instance_expire, adv_timeout_expire);
	INIT_DELAYED_WORK(&hdev->interleave_scan, interleave_scan_work);
}

void hci_request_cancel_all(struct hci_dev *hdev)
{
	__hci_cmd_sync_cancel(hdev, ENODEV);

	cancel_delayed_work_sync(&hdev->le_scan_disable);
	cancel_delayed_work_sync(&hdev->le_scan_restart);

	if (hdev->adv_instance_timeout) {
		cancel_delayed_work_sync(&hdev->adv_instance_expire);
		hdev->adv_instance_timeout = 0;
	}

	cancel_interleave_scan(hdev);
}
