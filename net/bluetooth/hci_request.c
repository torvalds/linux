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

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "smp.h"
#include "hci_request.h"

void hci_req_init(struct hci_request *req, struct hci_dev *hdev)
{
	skb_queue_head_init(&req->cmd_q);
	req->hdev = hdev;
	req->err = 0;
}

int hci_req_run(struct hci_request *req, hci_req_complete_t complete)
{
	struct hci_dev *hdev = req->hdev;
	struct sk_buff *skb;
	unsigned long flags;

	BT_DBG("length %u", skb_queue_len(&req->cmd_q));

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
	bt_cb(skb)->req_complete = complete;

	spin_lock_irqsave(&hdev->cmd_q.lock, flags);
	skb_queue_splice_tail(&req->cmd_q, &hdev->cmd_q);
	spin_unlock_irqrestore(&hdev->cmd_q.lock, flags);

	queue_work(hdev->workqueue, &hdev->cmd_work);

	return 0;
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

	hdr = (struct hci_command_hdr *) skb_put(skb, HCI_COMMAND_HDR_SIZE);
	hdr->opcode = cpu_to_le16(opcode);
	hdr->plen   = plen;

	if (plen)
		memcpy(skb_put(skb, plen), param, plen);

	BT_DBG("skb len %d", skb->len);

	bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;
	bt_cb(skb)->opcode = opcode;

	return skb;
}

/* Queue a command to an asynchronous HCI request */
void hci_req_add_ev(struct hci_request *req, u16 opcode, u32 plen,
		    const void *param, u8 event)
{
	struct hci_dev *hdev = req->hdev;
	struct sk_buff *skb;

	BT_DBG("%s opcode 0x%4.4x plen %d", hdev->name, opcode, plen);

	/* If an error occurred during request building, there is no point in
	 * queueing the HCI command. We can simply return.
	 */
	if (req->err)
		return;

	skb = hci_prepare_cmd(hdev, opcode, plen, param);
	if (!skb) {
		BT_ERR("%s no memory for command (opcode 0x%4.4x)",
		       hdev->name, opcode);
		req->err = -ENOMEM;
		return;
	}

	if (skb_queue_empty(&req->cmd_q))
		bt_cb(skb)->req_start = 1;

	bt_cb(skb)->req_event = event;

	skb_queue_tail(&req->cmd_q, skb);
}

void hci_req_add(struct hci_request *req, u16 opcode, u32 plen,
		 const void *param)
{
	hci_req_add_ev(req, opcode, plen, param, 0);
}

void hci_req_add_le_scan_disable(struct hci_request *req)
{
	struct hci_cp_le_set_scan_enable cp;

	memset(&cp, 0, sizeof(cp));
	cp.enable = LE_SCAN_DISABLE;
	hci_req_add(req, HCI_OP_LE_SET_SCAN_ENABLE, sizeof(cp), &cp);
}

static void add_to_white_list(struct hci_request *req,
			      struct hci_conn_params *params)
{
	struct hci_cp_le_add_to_white_list cp;

	cp.bdaddr_type = params->addr_type;
	bacpy(&cp.bdaddr, &params->addr);

	hci_req_add(req, HCI_OP_LE_ADD_TO_WHITE_LIST, sizeof(cp), &cp);
}

static u8 update_white_list(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_conn_params *params;
	struct bdaddr_list *b;
	uint8_t white_list_entries = 0;

	/* Go through the current white list programmed into the
	 * controller one by one and check if that address is still
	 * in the list of pending connections or list of devices to
	 * report. If not present in either list, then queue the
	 * command to remove it from the controller.
	 */
	list_for_each_entry(b, &hdev->le_white_list, list) {
		struct hci_cp_le_del_from_white_list cp;

		if (hci_pend_le_action_lookup(&hdev->pend_le_conns,
					      &b->bdaddr, b->bdaddr_type) ||
		    hci_pend_le_action_lookup(&hdev->pend_le_reports,
					      &b->bdaddr, b->bdaddr_type)) {
			white_list_entries++;
			continue;
		}

		cp.bdaddr_type = b->bdaddr_type;
		bacpy(&cp.bdaddr, &b->bdaddr);

		hci_req_add(req, HCI_OP_LE_DEL_FROM_WHITE_LIST,
			    sizeof(cp), &cp);
	}

	/* Since all no longer valid white list entries have been
	 * removed, walk through the list of pending connections
	 * and ensure that any new device gets programmed into
	 * the controller.
	 *
	 * If the list of the devices is larger than the list of
	 * available white list entries in the controller, then
	 * just abort and return filer policy value to not use the
	 * white list.
	 */
	list_for_each_entry(params, &hdev->pend_le_conns, action) {
		if (hci_bdaddr_list_lookup(&hdev->le_white_list,
					   &params->addr, params->addr_type))
			continue;

		if (white_list_entries >= hdev->le_white_list_size) {
			/* Select filter policy to accept all advertising */
			return 0x00;
		}

		if (hci_find_irk_by_addr(hdev, &params->addr,
					 params->addr_type)) {
			/* White list can not be used with RPAs */
			return 0x00;
		}

		white_list_entries++;
		add_to_white_list(req, params);
	}

	/* After adding all new pending connections, walk through
	 * the list of pending reports and also add these to the
	 * white list if there is still space.
	 */
	list_for_each_entry(params, &hdev->pend_le_reports, action) {
		if (hci_bdaddr_list_lookup(&hdev->le_white_list,
					   &params->addr, params->addr_type))
			continue;

		if (white_list_entries >= hdev->le_white_list_size) {
			/* Select filter policy to accept all advertising */
			return 0x00;
		}

		if (hci_find_irk_by_addr(hdev, &params->addr,
					 params->addr_type)) {
			/* White list can not be used with RPAs */
			return 0x00;
		}

		white_list_entries++;
		add_to_white_list(req, params);
	}

	/* Select filter policy to use white list */
	return 0x01;
}

void hci_req_add_le_passive_scan(struct hci_request *req)
{
	struct hci_cp_le_set_scan_param param_cp;
	struct hci_cp_le_set_scan_enable enable_cp;
	struct hci_dev *hdev = req->hdev;
	u8 own_addr_type;
	u8 filter_policy;

	/* Set require_privacy to false since no SCAN_REQ are send
	 * during passive scanning. Not using an non-resolvable address
	 * here is important so that peer devices using direct
	 * advertising with our address will be correctly reported
	 * by the controller.
	 */
	if (hci_update_random_address(req, false, &own_addr_type))
		return;

	/* Adding or removing entries from the white list must
	 * happen before enabling scanning. The controller does
	 * not allow white list modification while scanning.
	 */
	filter_policy = update_white_list(req);

	/* When the controller is using random resolvable addresses and
	 * with that having LE privacy enabled, then controllers with
	 * Extended Scanner Filter Policies support can now enable support
	 * for handling directed advertising.
	 *
	 * So instead of using filter polices 0x00 (no whitelist)
	 * and 0x01 (whitelist enabled) use the new filter policies
	 * 0x02 (no whitelist) and 0x03 (whitelist enabled).
	 */
	if (hci_dev_test_flag(hdev, HCI_PRIVACY) &&
	    (hdev->le_features[0] & HCI_LE_EXT_SCAN_POLICY))
		filter_policy |= 0x02;

	memset(&param_cp, 0, sizeof(param_cp));
	param_cp.type = LE_SCAN_PASSIVE;
	param_cp.interval = cpu_to_le16(hdev->le_scan_interval);
	param_cp.window = cpu_to_le16(hdev->le_scan_window);
	param_cp.own_address_type = own_addr_type;
	param_cp.filter_policy = filter_policy;
	hci_req_add(req, HCI_OP_LE_SET_SCAN_PARAM, sizeof(param_cp),
		    &param_cp);

	memset(&enable_cp, 0, sizeof(enable_cp));
	enable_cp.enable = LE_SCAN_ENABLE;
	enable_cp.filter_dup = LE_SCAN_FILTER_DUP_ENABLE;
	hci_req_add(req, HCI_OP_LE_SET_SCAN_ENABLE, sizeof(enable_cp),
		    &enable_cp);
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
	    hci_conn_hash_lookup_state(hdev, LE_LINK, BT_CONNECT)) {
		BT_DBG("Deferring random address update");
		hci_dev_set_flag(hdev, HCI_RPA_EXPIRED);
		return;
	}

	hci_req_add(req, HCI_OP_LE_SET_RANDOM_ADDR, 6, rpa);
}

int hci_update_random_address(struct hci_request *req, bool require_privacy,
			      u8 *own_addr_type)
{
	struct hci_dev *hdev = req->hdev;
	int err;

	/* If privacy is enabled use a resolvable private address. If
	 * current RPA has expired or there is something else than
	 * the current RPA in use, then generate a new one.
	 */
	if (hci_dev_test_flag(hdev, HCI_PRIVACY)) {
		int to;

		*own_addr_type = ADDR_LE_DEV_RANDOM;

		if (!hci_dev_test_and_clear_flag(hdev, HCI_RPA_EXPIRED) &&
		    !bacmp(&hdev->random_addr, &hdev->rpa))
			return 0;

		err = smp_generate_rpa(hdev, hdev->irk, &hdev->rpa);
		if (err < 0) {
			BT_ERR("%s failed to generate new RPA", hdev->name);
			return err;
		}

		set_random_addr(req, &hdev->rpa);

		to = msecs_to_jiffies(hdev->rpa_timeout * 1000);
		queue_delayed_work(hdev->workqueue, &hdev->rpa_expired, to);

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

static bool disconnected_whitelist_entries(struct hci_dev *hdev)
{
	struct bdaddr_list *b;

	list_for_each_entry(b, &hdev->whitelist, list) {
		struct hci_conn *conn;

		conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &b->bdaddr);
		if (!conn)
			return true;

		if (conn->state != BT_CONNECTED && conn->state != BT_CONFIG)
			return true;
	}

	return false;
}

void __hci_update_page_scan(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	u8 scan;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return;

	if (!hdev_is_powered(hdev))
		return;

	if (mgmt_powering_down(hdev))
		return;

	if (hci_dev_test_flag(hdev, HCI_CONNECTABLE) ||
	    disconnected_whitelist_entries(hdev))
		scan = SCAN_PAGE;
	else
		scan = SCAN_DISABLED;

	if (test_bit(HCI_PSCAN, &hdev->flags) == !!(scan & SCAN_PAGE))
		return;

	if (hci_dev_test_flag(hdev, HCI_DISCOVERABLE))
		scan |= SCAN_INQUIRY;

	hci_req_add(req, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
}

void hci_update_page_scan(struct hci_dev *hdev)
{
	struct hci_request req;

	hci_req_init(&req, hdev);
	__hci_update_page_scan(&req);
	hci_req_run(&req, NULL);
}

/* This function controls the background scanning based on hdev->pend_le_conns
 * list. If there are pending LE connection we start the background scanning,
 * otherwise we stop it.
 *
 * This function requires the caller holds hdev->lock.
 */
void __hci_update_background_scan(struct hci_request *req)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_conn *conn;

	if (!test_bit(HCI_UP, &hdev->flags) ||
	    test_bit(HCI_INIT, &hdev->flags) ||
	    hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG) ||
	    hci_dev_test_flag(hdev, HCI_AUTO_OFF) ||
	    hci_dev_test_flag(hdev, HCI_UNREGISTER))
		return;

	/* No point in doing scanning if LE support hasn't been enabled */
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return;

	/* If discovery is active don't interfere with it */
	if (hdev->discovery.state != DISCOVERY_STOPPED)
		return;

	/* Reset RSSI and UUID filters when starting background scanning
	 * since these filters are meant for service discovery only.
	 *
	 * The Start Discovery and Start Service Discovery operations
	 * ensure to set proper values for RSSI threshold and UUID
	 * filter list. So it is safe to just reset them here.
	 */
	hci_discovery_filter_clear(hdev);

	if (list_empty(&hdev->pend_le_conns) &&
	    list_empty(&hdev->pend_le_reports)) {
		/* If there is no pending LE connections or devices
		 * to be scanned for, we should stop the background
		 * scanning.
		 */

		/* If controller is not scanning we are done. */
		if (!hci_dev_test_flag(hdev, HCI_LE_SCAN))
			return;

		hci_req_add_le_scan_disable(req);

		BT_DBG("%s stopping background scanning", hdev->name);
	} else {
		/* If there is at least one pending LE connection, we should
		 * keep the background scan running.
		 */

		/* If controller is connecting, we should not start scanning
		 * since some controllers are not able to scan and connect at
		 * the same time.
		 */
		conn = hci_conn_hash_lookup_state(hdev, LE_LINK, BT_CONNECT);
		if (conn)
			return;

		/* If controller is currently scanning, we stop it to ensure we
		 * don't miss any advertising (due to duplicates filter).
		 */
		if (hci_dev_test_flag(hdev, HCI_LE_SCAN))
			hci_req_add_le_scan_disable(req);

		hci_req_add_le_passive_scan(req);

		BT_DBG("%s starting background scanning", hdev->name);
	}
}

static void update_background_scan_complete(struct hci_dev *hdev, u8 status,
					    u16 opcode)
{
	if (status)
		BT_DBG("HCI request failed to update background scanning: "
		       "status 0x%2.2x", status);
}

void hci_update_background_scan(struct hci_dev *hdev)
{
	int err;
	struct hci_request req;

	hci_req_init(&req, hdev);

	__hci_update_background_scan(&req);

	err = hci_req_run(&req, update_background_scan_complete);
	if (err && err != -ENODATA)
		BT_ERR("Failed to run HCI request: err %d", err);
}
