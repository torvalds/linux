// SPDX-License-Identifier: GPL-2.0
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "smp.h"
#include "eir.h"

static void hci_cmd_sync_complete(struct hci_dev *hdev, u8 result, u16 opcode,
				  struct sk_buff *skb)
{
	bt_dev_dbg(hdev, "result 0x%2.2x", result);

	if (hdev->req_status != HCI_REQ_PEND)
		return;

	hdev->req_result = result;
	hdev->req_status = HCI_REQ_DONE;

	if (skb) {
		struct sock *sk = hci_skb_sk(skb);

		/* Drop sk reference if set */
		if (sk)
			sock_put(sk);

		hdev->req_skb = skb_get(skb);
	}

	wake_up_interruptible(&hdev->req_wait_q);
}

static struct sk_buff *hci_cmd_sync_alloc(struct hci_dev *hdev, u16 opcode,
					  u32 plen, const void *param,
					  struct sock *sk)
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

	/* Grab a reference if command needs to be associated with a sock (e.g.
	 * likely mgmt socket that initiated the command).
	 */
	if (sk) {
		hci_skb_sk(skb) = sk;
		sock_hold(sk);
	}

	return skb;
}

static void hci_cmd_sync_add(struct hci_request *req, u16 opcode, u32 plen,
			     const void *param, u8 event, struct sock *sk)
{
	struct hci_dev *hdev = req->hdev;
	struct sk_buff *skb;

	bt_dev_dbg(hdev, "opcode 0x%4.4x plen %d", opcode, plen);

	/* If an error occurred during request building, there is no point in
	 * queueing the HCI command. We can simply return.
	 */
	if (req->err)
		return;

	skb = hci_cmd_sync_alloc(hdev, opcode, plen, param, sk);
	if (!skb) {
		bt_dev_err(hdev, "no memory for command (opcode 0x%4.4x)",
			   opcode);
		req->err = -ENOMEM;
		return;
	}

	if (skb_queue_empty(&req->cmd_q))
		bt_cb(skb)->hci.req_flags |= HCI_REQ_START;

	bt_cb(skb)->hci.req_event = event;

	skb_queue_tail(&req->cmd_q, skb);
}

static int hci_cmd_sync_run(struct hci_request *req)
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
	bt_cb(skb)->hci.req_complete_skb = hci_cmd_sync_complete;
	bt_cb(skb)->hci.req_flags |= HCI_REQ_SKB;

	spin_lock_irqsave(&hdev->cmd_q.lock, flags);
	skb_queue_splice_tail(&req->cmd_q, &hdev->cmd_q);
	spin_unlock_irqrestore(&hdev->cmd_q.lock, flags);

	queue_work(hdev->workqueue, &hdev->cmd_work);

	return 0;
}

/* This function requires the caller holds hdev->req_lock. */
struct sk_buff *__hci_cmd_sync_sk(struct hci_dev *hdev, u16 opcode, u32 plen,
				  const void *param, u8 event, u32 timeout,
				  struct sock *sk)
{
	struct hci_request req;
	struct sk_buff *skb;
	int err = 0;

	bt_dev_dbg(hdev, "");

	hci_req_init(&req, hdev);

	hci_cmd_sync_add(&req, opcode, plen, param, event, sk);

	hdev->req_status = HCI_REQ_PEND;

	err = hci_cmd_sync_run(&req);
	if (err < 0)
		return ERR_PTR(err);

	err = wait_event_interruptible_timeout(hdev->req_wait_q,
					       hdev->req_status != HCI_REQ_PEND,
					       timeout);

	if (err == -ERESTARTSYS)
		return ERR_PTR(-EINTR);

	switch (hdev->req_status) {
	case HCI_REQ_DONE:
		err = -bt_to_errno(hdev->req_result);
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		break;

	default:
		err = -ETIMEDOUT;
		break;
	}

	hdev->req_status = 0;
	hdev->req_result = 0;
	skb = hdev->req_skb;
	hdev->req_skb = NULL;

	bt_dev_dbg(hdev, "end: err %d", err);

	if (err < 0) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	return skb;
}
EXPORT_SYMBOL(__hci_cmd_sync_sk);

/* This function requires the caller holds hdev->req_lock. */
struct sk_buff *__hci_cmd_sync(struct hci_dev *hdev, u16 opcode, u32 plen,
			       const void *param, u32 timeout)
{
	return __hci_cmd_sync_sk(hdev, opcode, plen, param, 0, timeout, NULL);
}
EXPORT_SYMBOL(__hci_cmd_sync);

/* Send HCI command and wait for command complete event */
struct sk_buff *hci_cmd_sync(struct hci_dev *hdev, u16 opcode, u32 plen,
			     const void *param, u32 timeout)
{
	struct sk_buff *skb;

	if (!test_bit(HCI_UP, &hdev->flags))
		return ERR_PTR(-ENETDOWN);

	bt_dev_dbg(hdev, "opcode 0x%4.4x plen %d", opcode, plen);

	hci_req_sync_lock(hdev);
	skb = __hci_cmd_sync(hdev, opcode, plen, param, timeout);
	hci_req_sync_unlock(hdev);

	return skb;
}
EXPORT_SYMBOL(hci_cmd_sync);

/* This function requires the caller holds hdev->req_lock. */
struct sk_buff *__hci_cmd_sync_ev(struct hci_dev *hdev, u16 opcode, u32 plen,
				  const void *param, u8 event, u32 timeout)
{
	return __hci_cmd_sync_sk(hdev, opcode, plen, param, event, timeout,
				 NULL);
}
EXPORT_SYMBOL(__hci_cmd_sync_ev);

/* This function requires the caller holds hdev->req_lock. */
int __hci_cmd_sync_status_sk(struct hci_dev *hdev, u16 opcode, u32 plen,
			     const void *param, u8 event, u32 timeout,
			     struct sock *sk)
{
	struct sk_buff *skb;
	u8 status;

	skb = __hci_cmd_sync_sk(hdev, opcode, plen, param, event, timeout, sk);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Opcode 0x%4x failed: %ld", opcode,
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	/* If command return a status event skb will be set to NULL as there are
	 * no parameters, in case of failure IS_ERR(skb) would have be set to
	 * the actual error would be found with PTR_ERR(skb).
	 */
	if (!skb)
		return 0;

	status = skb->data[0];

	kfree_skb(skb);

	return status;
}
EXPORT_SYMBOL(__hci_cmd_sync_status_sk);

int __hci_cmd_sync_status(struct hci_dev *hdev, u16 opcode, u32 plen,
			  const void *param, u32 timeout)
{
	return __hci_cmd_sync_status_sk(hdev, opcode, plen, param, 0, timeout,
					NULL);
}
EXPORT_SYMBOL(__hci_cmd_sync_status);

static void hci_cmd_sync_work(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, cmd_sync_work);
	struct hci_cmd_sync_work_entry *entry;
	hci_cmd_sync_work_func_t func;
	hci_cmd_sync_work_destroy_t destroy;
	void *data;

	bt_dev_dbg(hdev, "");

	mutex_lock(&hdev->cmd_sync_work_lock);
	entry = list_first_entry(&hdev->cmd_sync_work_list,
				 struct hci_cmd_sync_work_entry, list);
	if (entry) {
		list_del(&entry->list);
		func = entry->func;
		data = entry->data;
		destroy = entry->destroy;
		kfree(entry);
	} else {
		func = NULL;
		data = NULL;
		destroy = NULL;
	}
	mutex_unlock(&hdev->cmd_sync_work_lock);

	if (func) {
		int err;

		hci_req_sync_lock(hdev);

		err = func(hdev, data);

		if (destroy)
			destroy(hdev, data, err);

		hci_req_sync_unlock(hdev);
	}
}

void hci_cmd_sync_init(struct hci_dev *hdev)
{
	INIT_WORK(&hdev->cmd_sync_work, hci_cmd_sync_work);
	INIT_LIST_HEAD(&hdev->cmd_sync_work_list);
	mutex_init(&hdev->cmd_sync_work_lock);
}

void hci_cmd_sync_clear(struct hci_dev *hdev)
{
	struct hci_cmd_sync_work_entry *entry, *tmp;

	cancel_work_sync(&hdev->cmd_sync_work);

	list_for_each_entry_safe(entry, tmp, &hdev->cmd_sync_work_list, list) {
		if (entry->destroy)
			entry->destroy(hdev, entry->data, -ECANCELED);

		list_del(&entry->list);
		kfree(entry);
	}
}

int hci_cmd_sync_queue(struct hci_dev *hdev, hci_cmd_sync_work_func_t func,
		       void *data, hci_cmd_sync_work_destroy_t destroy)
{
	struct hci_cmd_sync_work_entry *entry;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->func = func;
	entry->data = data;
	entry->destroy = destroy;

	mutex_lock(&hdev->cmd_sync_work_lock);
	list_add_tail(&entry->list, &hdev->cmd_sync_work_list);
	mutex_unlock(&hdev->cmd_sync_work_lock);

	queue_work(hdev->req_workqueue, &hdev->cmd_sync_work);

	return 0;
}
EXPORT_SYMBOL(hci_cmd_sync_queue);

int hci_update_eir_sync(struct hci_dev *hdev)
{
	struct hci_cp_write_eir cp;

	bt_dev_dbg(hdev, "");

	if (!hdev_is_powered(hdev))
		return 0;

	if (!lmp_ext_inq_capable(hdev))
		return 0;

	if (!hci_dev_test_flag(hdev, HCI_SSP_ENABLED))
		return 0;

	if (hci_dev_test_flag(hdev, HCI_SERVICE_CACHE))
		return 0;

	memset(&cp, 0, sizeof(cp));

	eir_create(hdev, cp.data);

	if (memcmp(cp.data, hdev->eir, sizeof(cp.data)) == 0)
		return 0;

	memcpy(hdev->eir, cp.data, sizeof(cp.data));

	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_EIR, sizeof(cp), &cp,
				     HCI_CMD_TIMEOUT);
}

static u8 get_service_classes(struct hci_dev *hdev)
{
	struct bt_uuid *uuid;
	u8 val = 0;

	list_for_each_entry(uuid, &hdev->uuids, list)
		val |= uuid->svc_hint;

	return val;
}

int hci_update_class_sync(struct hci_dev *hdev)
{
	u8 cod[3];

	bt_dev_dbg(hdev, "");

	if (!hdev_is_powered(hdev))
		return 0;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return 0;

	if (hci_dev_test_flag(hdev, HCI_SERVICE_CACHE))
		return 0;

	cod[0] = hdev->minor_class;
	cod[1] = hdev->major_class;
	cod[2] = get_service_classes(hdev);

	if (hci_dev_test_flag(hdev, HCI_LIMITED_DISCOVERABLE))
		cod[1] |= 0x20;

	if (memcmp(cod, hdev->dev_class, 3) == 0)
		return 0;

	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_CLASS_OF_DEV,
				     sizeof(cod), cod, HCI_CMD_TIMEOUT);
}

static bool is_advertising_allowed(struct hci_dev *hdev, bool connectable)
{
	/* If there is no connection we are OK to advertise. */
	if (hci_conn_num(hdev, LE_LINK) == 0)
		return true;

	/* Check le_states if there is any connection in peripheral role. */
	if (hdev->conn_hash.le_num_peripheral > 0) {
		/* Peripheral connection state and non connectable mode
		 * bit 20.
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

static int hci_set_random_addr_sync(struct hci_dev *hdev, bdaddr_t *rpa)
{
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
		return 0;
	}

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_RANDOM_ADDR,
				     6, rpa, HCI_CMD_TIMEOUT);
}

int hci_update_random_address_sync(struct hci_dev *hdev, bool require_privacy,
				   bool rpa, u8 *own_addr_type)
{
	int err;

	/* If privacy is enabled use a resolvable private address. If
	 * current RPA has expired or there is something else than
	 * the current RPA in use, then generate a new one.
	 */
	if (rpa) {
		/* If Controller supports LL Privacy use own address type is
		 * 0x03
		 */
		if (use_ll_privacy(hdev))
			*own_addr_type = ADDR_LE_DEV_RANDOM_RESOLVED;
		else
			*own_addr_type = ADDR_LE_DEV_RANDOM;

		/* Check if RPA is valid */
		if (rpa_valid(hdev))
			return 0;

		err = smp_generate_rpa(hdev, hdev->irk, &hdev->rpa);
		if (err < 0) {
			bt_dev_err(hdev, "failed to generate new RPA");
			return err;
		}

		err = hci_set_random_addr_sync(hdev, &hdev->rpa);
		if (err)
			return err;

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

		return hci_set_random_addr_sync(hdev, &nrpa);
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
			return hci_set_random_addr_sync(hdev,
							&hdev->static_addr);
		return 0;
	}

	/* Neither privacy nor static address is being used so use a
	 * public address.
	 */
	*own_addr_type = ADDR_LE_DEV_PUBLIC;

	return 0;
}

static int hci_disable_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_adv_enable *cp;
	struct hci_cp_ext_adv_set *set;
	u8 data[sizeof(*cp) + sizeof(*set) * 1];
	u8 size;

	/* If request specifies an instance that doesn't exist, fail */
	if (instance > 0) {
		struct adv_info *adv;

		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return -EINVAL;

		/* If not enabled there is nothing to do */
		if (!adv->enabled)
			return 0;
	}

	memset(data, 0, sizeof(data));

	cp = (void *)data;
	set = (void *)cp->data;

	/* Instance 0x00 indicates all advertising instances will be disabled */
	cp->num_of_sets = !!instance;
	cp->enable = 0x00;

	set->handle = instance;

	size = sizeof(*cp) + sizeof(*set) * cp->num_of_sets;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_ENABLE,
				     size, data, HCI_CMD_TIMEOUT);
}

static int hci_set_adv_set_random_addr_sync(struct hci_dev *hdev, u8 instance,
					    bdaddr_t *random_addr)
{
	struct hci_cp_le_set_adv_set_rand_addr cp;
	int err;

	if (!instance) {
		/* Instance 0x00 doesn't have an adv_info, instead it uses
		 * hdev->random_addr to track its address so whenever it needs
		 * to be updated this also set the random address since
		 * hdev->random_addr is shared with scan state machine.
		 */
		err = hci_set_random_addr_sync(hdev, random_addr);
		if (err)
			return err;
	}

	memset(&cp, 0, sizeof(cp));

	cp.handle = instance;
	bacpy(&cp.bdaddr, random_addr);

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_SET_RAND_ADDR,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_setup_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_adv_params cp;
	bool connectable;
	u32 flags;
	bdaddr_t random_addr;
	u8 own_addr_type;
	int err;
	struct adv_info *adv;
	bool secondary_adv;

	if (instance > 0) {
		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return -EINVAL;
	} else {
		adv = NULL;
	}

	/* Updating parameters of an active instance will return a
	 * Command Disallowed error, so we must first disable the
	 * instance if it is active.
	 */
	if (adv && !adv->pending) {
		err = hci_disable_ext_adv_instance_sync(hdev, instance);
		if (err)
			return err;
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
	err = hci_get_random_address(hdev, !connectable,
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
		if (secondary_adv)
			cp.evt_properties = cpu_to_le16(LE_EXT_ADV_NON_CONN_IND);
		else
			cp.evt_properties = cpu_to_le16(LE_LEGACY_NONCONN_IND);
	}

	/* If Own_Address_Type equals 0x02 or 0x03, the Peer_Address parameter
	 * contains the peer’s Identity Address and the Peer_Address_Type
	 * parameter contains the peer’s Identity Type (i.e., 0x00 or 0x01).
	 * These parameters are used to locate the corresponding local IRK in
	 * the resolving list; this IRK is used to generate their own address
	 * used in the advertisement.
	 */
	if (own_addr_type == ADDR_LE_DEV_RANDOM_RESOLVED)
		hci_copy_identity_address(hdev, &cp.peer_addr,
					  &cp.peer_addr_type);

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

	err = __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_PARAMS,
				    sizeof(cp), &cp, HCI_CMD_TIMEOUT);
	if (err)
		return err;

	if ((own_addr_type == ADDR_LE_DEV_RANDOM ||
	     own_addr_type == ADDR_LE_DEV_RANDOM_RESOLVED) &&
	    bacmp(&random_addr, BDADDR_ANY)) {
		/* Check if random address need to be updated */
		if (adv) {
			if (!bacmp(&random_addr, &adv->random_addr))
				return 0;
		} else {
			if (!bacmp(&random_addr, &hdev->random_addr))
				return 0;
		}

		return hci_set_adv_set_random_addr_sync(hdev, instance,
							&random_addr);
	}

	return 0;
}

static int hci_set_ext_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct {
		struct hci_cp_le_set_ext_scan_rsp_data cp;
		u8 data[HCI_MAX_EXT_AD_LENGTH];
	} pdu;
	u8 len;

	memset(&pdu, 0, sizeof(pdu));

	len = eir_create_scan_rsp(hdev, instance, pdu.data);

	if (hdev->scan_rsp_data_len == len &&
	    !memcmp(pdu.data, hdev->scan_rsp_data, len))
		return 0;

	memcpy(hdev->scan_rsp_data, pdu.data, len);
	hdev->scan_rsp_data_len = len;

	pdu.cp.handle = instance;
	pdu.cp.length = len;
	pdu.cp.operation = LE_SET_ADV_DATA_OP_COMPLETE;
	pdu.cp.frag_pref = LE_SET_ADV_DATA_NO_FRAG;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_SCAN_RSP_DATA,
				     sizeof(pdu.cp) + len, &pdu.cp,
				     HCI_CMD_TIMEOUT);
}

static int __hci_set_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_scan_rsp_data cp;
	u8 len;

	memset(&cp, 0, sizeof(cp));

	len = eir_create_scan_rsp(hdev, instance, cp.data);

	if (hdev->scan_rsp_data_len == len &&
	    !memcmp(cp.data, hdev->scan_rsp_data, len))
		return 0;

	memcpy(hdev->scan_rsp_data, cp.data, sizeof(cp.data));
	hdev->scan_rsp_data_len = len;

	cp.length = len;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_SCAN_RSP_DATA,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_update_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance)
{
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return 0;

	if (ext_adv_capable(hdev))
		return hci_set_ext_scan_rsp_data_sync(hdev, instance);

	return __hci_set_scan_rsp_data_sync(hdev, instance);
}

int hci_enable_ext_advertising_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_adv_enable *cp;
	struct hci_cp_ext_adv_set *set;
	u8 data[sizeof(*cp) + sizeof(*set) * 1];
	struct adv_info *adv;

	if (instance > 0) {
		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return -EINVAL;
		/* If already enabled there is nothing to do */
		if (adv->enabled)
			return 0;
	} else {
		adv = NULL;
	}

	cp = (void *)data;
	set = (void *)cp->data;

	memset(cp, 0, sizeof(*cp));

	cp->enable = 0x01;
	cp->num_of_sets = 0x01;

	memset(set, 0, sizeof(*set));

	set->handle = instance;

	/* Set duration per instance since controller is responsible for
	 * scheduling it.
	 */
	if (adv && adv->duration) {
		u16 duration = adv->timeout * MSEC_PER_SEC;

		/* Time = N * 10 ms */
		set->duration = cpu_to_le16(duration / 10);
	}

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_ENABLE,
				     sizeof(*cp) +
				     sizeof(*set) * cp->num_of_sets,
				     data, HCI_CMD_TIMEOUT);
}

int hci_start_ext_adv_sync(struct hci_dev *hdev, u8 instance)
{
	int err;

	err = hci_setup_ext_adv_instance_sync(hdev, instance);
	if (err)
		return err;

	err = hci_set_ext_scan_rsp_data_sync(hdev, instance);
	if (err)
		return err;

	return hci_enable_ext_advertising_sync(hdev, instance);
}

static int hci_start_adv_sync(struct hci_dev *hdev, u8 instance)
{
	int err;

	if (ext_adv_capable(hdev))
		return hci_start_ext_adv_sync(hdev, instance);

	err = hci_update_adv_data_sync(hdev, instance);
	if (err)
		return err;

	err = hci_update_scan_rsp_data_sync(hdev, instance);
	if (err)
		return err;

	return hci_enable_advertising_sync(hdev);
}

int hci_enable_advertising_sync(struct hci_dev *hdev)
{
	struct adv_info *adv_instance;
	struct hci_cp_le_set_adv_param cp;
	u8 own_addr_type, enable = 0x01;
	bool connectable;
	u16 adv_min_interval, adv_max_interval;
	u32 flags;
	u8 status;

	if (ext_adv_capable(hdev))
		return hci_enable_ext_advertising_sync(hdev,
						       hdev->cur_adv_instance);

	flags = hci_adv_instance_flags(hdev, hdev->cur_adv_instance);
	adv_instance = hci_find_adv_instance(hdev, hdev->cur_adv_instance);

	/* If the "connectable" instance flag was not set, then choose between
	 * ADV_IND and ADV_NONCONN_IND based on the global connectable setting.
	 */
	connectable = (flags & MGMT_ADV_FLAG_CONNECTABLE) ||
		      mgmt_get_connectable(hdev);

	if (!is_advertising_allowed(hdev, connectable))
		return -EINVAL;

	status = hci_disable_advertising_sync(hdev);
	if (status)
		return status;

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
	status = hci_update_random_address_sync(hdev, !connectable,
						adv_use_rpa(hdev, flags),
						&own_addr_type);
	if (status)
		return status;

	memset(&cp, 0, sizeof(cp));

	if (adv_instance) {
		adv_min_interval = adv_instance->min_interval;
		adv_max_interval = adv_instance->max_interval;
	} else {
		adv_min_interval = hdev->le_adv_min_interval;
		adv_max_interval = hdev->le_adv_max_interval;
	}

	if (connectable) {
		cp.type = LE_ADV_IND;
	} else {
		if (hci_adv_instance_is_scannable(hdev, hdev->cur_adv_instance))
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

	status = __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_PARAM,
				       sizeof(cp), &cp, HCI_CMD_TIMEOUT);
	if (status)
		return status;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_ENABLE,
				     sizeof(enable), &enable, HCI_CMD_TIMEOUT);
}

static int enable_advertising_sync(struct hci_dev *hdev, void *data)
{
	return hci_enable_advertising_sync(hdev);
}

int hci_enable_advertising(struct hci_dev *hdev)
{
	if (!hci_dev_test_flag(hdev, HCI_ADVERTISING) &&
	    list_empty(&hdev->adv_instances))
		return 0;

	return hci_cmd_sync_queue(hdev, enable_advertising_sync, NULL, NULL);
}

int hci_remove_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance,
				     struct sock *sk)
{
	int err;

	if (!ext_adv_capable(hdev))
		return 0;

	err = hci_disable_ext_adv_instance_sync(hdev, instance);
	if (err)
		return err;

	/* If request specifies an instance that doesn't exist, fail */
	if (instance > 0 && !hci_find_adv_instance(hdev, instance))
		return -EINVAL;

	return __hci_cmd_sync_status_sk(hdev, HCI_OP_LE_REMOVE_ADV_SET,
					sizeof(instance), &instance, 0,
					HCI_CMD_TIMEOUT, sk);
}

static void cancel_adv_timeout(struct hci_dev *hdev)
{
	if (hdev->adv_instance_timeout) {
		hdev->adv_instance_timeout = 0;
		cancel_delayed_work(&hdev->adv_instance_expire);
	}
}

static int hci_set_ext_adv_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct {
		struct hci_cp_le_set_ext_adv_data cp;
		u8 data[HCI_MAX_EXT_AD_LENGTH];
	} pdu;
	u8 len;

	memset(&pdu, 0, sizeof(pdu));

	len = eir_create_adv_data(hdev, instance, pdu.data);

	/* There's nothing to do if the data hasn't changed */
	if (hdev->adv_data_len == len &&
	    memcmp(pdu.data, hdev->adv_data, len) == 0)
		return 0;

	memcpy(hdev->adv_data, pdu.data, len);
	hdev->adv_data_len = len;

	pdu.cp.length = len;
	pdu.cp.handle = instance;
	pdu.cp.operation = LE_SET_ADV_DATA_OP_COMPLETE;
	pdu.cp.frag_pref = LE_SET_ADV_DATA_NO_FRAG;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_DATA,
				     sizeof(pdu.cp) + len, &pdu.cp,
				     HCI_CMD_TIMEOUT);
}

static int hci_set_adv_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_adv_data cp;
	u8 len;

	memset(&cp, 0, sizeof(cp));

	len = eir_create_adv_data(hdev, instance, cp.data);

	/* There's nothing to do if the data hasn't changed */
	if (hdev->adv_data_len == len &&
	    memcmp(cp.data, hdev->adv_data, len) == 0)
		return 0;

	memcpy(hdev->adv_data, cp.data, sizeof(cp.data));
	hdev->adv_data_len = len;

	cp.length = len;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_DATA,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_update_adv_data_sync(struct hci_dev *hdev, u8 instance)
{
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return 0;

	if (ext_adv_capable(hdev))
		return hci_set_ext_adv_data_sync(hdev, instance);

	return hci_set_adv_data_sync(hdev, instance);
}

int hci_schedule_adv_instance_sync(struct hci_dev *hdev, u8 instance,
				   bool force)
{
	struct adv_info *adv = NULL;
	u16 timeout;

	if (hci_dev_test_flag(hdev, HCI_ADVERTISING) && !ext_adv_capable(hdev))
		return -EPERM;

	if (hdev->adv_instance_timeout)
		return -EBUSY;

	adv = hci_find_adv_instance(hdev, instance);
	if (!adv)
		return -ENOENT;

	/* A zero timeout means unlimited advertising. As long as there is
	 * only one instance, duration should be ignored. We still set a timeout
	 * in case further instances are being added later on.
	 *
	 * If the remaining lifetime of the instance is more than the duration
	 * then the timeout corresponds to the duration, otherwise it will be
	 * reduced to the remaining instance lifetime.
	 */
	if (adv->timeout == 0 || adv->duration <= adv->remaining_time)
		timeout = adv->duration;
	else
		timeout = adv->remaining_time;

	/* The remaining time is being reduced unless the instance is being
	 * advertised without time limit.
	 */
	if (adv->timeout)
		adv->remaining_time = adv->remaining_time - timeout;

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

	return hci_start_adv_sync(hdev, instance);
}

static int hci_clear_adv_sets_sync(struct hci_dev *hdev, struct sock *sk)
{
	int err;

	if (!ext_adv_capable(hdev))
		return 0;

	/* Disable instance 0x00 to disable all instances */
	err = hci_disable_ext_adv_instance_sync(hdev, 0x00);
	if (err)
		return err;

	return __hci_cmd_sync_status_sk(hdev, HCI_OP_LE_CLEAR_ADV_SETS,
					0, NULL, 0, HCI_CMD_TIMEOUT, sk);
}

static int hci_clear_adv_sync(struct hci_dev *hdev, struct sock *sk, bool force)
{
	struct adv_info *adv, *n;

	if (ext_adv_capable(hdev))
		/* Remove all existing sets */
		return hci_clear_adv_sets_sync(hdev, sk);

	/* This is safe as long as there is no command send while the lock is
	 * held.
	 */
	hci_dev_lock(hdev);

	/* Cleanup non-ext instances */
	list_for_each_entry_safe(adv, n, &hdev->adv_instances, list) {
		u8 instance = adv->instance;
		int err;

		if (!(force || adv->timeout))
			continue;

		err = hci_remove_adv_instance(hdev, instance);
		if (!err)
			mgmt_advertising_removed(sk, hdev, instance);
	}

	hci_dev_unlock(hdev);

	return 0;
}

static int hci_remove_adv_sync(struct hci_dev *hdev, u8 instance,
			       struct sock *sk)
{
	int err;

	/* If we use extended advertising, instance has to be removed first. */
	if (ext_adv_capable(hdev))
		return hci_remove_ext_adv_instance_sync(hdev, instance, sk);

	/* This is safe as long as there is no command send while the lock is
	 * held.
	 */
	hci_dev_lock(hdev);

	err = hci_remove_adv_instance(hdev, instance);
	if (!err)
		mgmt_advertising_removed(sk, hdev, instance);

	hci_dev_unlock(hdev);

	return err;
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
int hci_remove_advertising_sync(struct hci_dev *hdev, struct sock *sk,
				u8 instance, bool force)
{
	struct adv_info *next = NULL;
	int err;

	/* Cancel any timeout concerning the removed instance(s). */
	if (!instance || hdev->cur_adv_instance == instance)
		cancel_adv_timeout(hdev);

	/* Get the next instance to advertise BEFORE we remove
	 * the current one. This can be the same instance again
	 * if there is only one instance.
	 */
	if (hdev->cur_adv_instance == instance)
		next = hci_get_next_instance(hdev, instance);

	if (!instance) {
		err = hci_clear_adv_sync(hdev, sk, force);
		if (err)
			return err;
	} else {
		struct adv_info *adv = hci_find_adv_instance(hdev, instance);

		if (force || (adv && adv->timeout && !adv->remaining_time)) {
			/* Don't advertise a removed instance. */
			if (next && next->instance == instance)
				next = NULL;

			err = hci_remove_adv_sync(hdev, instance, sk);
			if (err)
				return err;
		}
	}

	if (!hdev_is_powered(hdev) || hci_dev_test_flag(hdev, HCI_ADVERTISING))
		return 0;

	if (next && !ext_adv_capable(hdev))
		hci_schedule_adv_instance_sync(hdev, next->instance, false);

	return 0;
}

int hci_read_rssi_sync(struct hci_dev *hdev, __le16 handle)
{
	struct hci_cp_read_rssi cp;

	cp.handle = handle;
	return __hci_cmd_sync_status(hdev, HCI_OP_READ_RSSI,
					sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_read_clock_sync(struct hci_dev *hdev, struct hci_cp_read_clock *cp)
{
	return __hci_cmd_sync_status(hdev, HCI_OP_READ_CLOCK,
					sizeof(*cp), cp, HCI_CMD_TIMEOUT);
}

int hci_read_tx_power_sync(struct hci_dev *hdev, __le16 handle, u8 type)
{
	struct hci_cp_read_tx_power cp;

	cp.handle = handle;
	cp.type = type;
	return __hci_cmd_sync_status(hdev, HCI_OP_READ_TX_POWER,
					sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_disable_advertising_sync(struct hci_dev *hdev)
{
	u8 enable = 0x00;

	/* If controller is not advertising we are done. */
	if (!hci_dev_test_flag(hdev, HCI_LE_ADV))
		return 0;

	if (ext_adv_capable(hdev))
		return hci_disable_ext_adv_instance_sync(hdev, 0x00);

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_ENABLE,
				     sizeof(enable), &enable, HCI_CMD_TIMEOUT);
}

static int hci_le_set_ext_scan_enable_sync(struct hci_dev *hdev, u8 val,
					   u8 filter_dup)
{
	struct hci_cp_le_set_ext_scan_enable cp;

	memset(&cp, 0, sizeof(cp));
	cp.enable = val;
	cp.filter_dup = filter_dup;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_SCAN_ENABLE,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_le_set_scan_enable_sync(struct hci_dev *hdev, u8 val,
				       u8 filter_dup)
{
	struct hci_cp_le_set_scan_enable cp;

	if (use_ext_scan(hdev))
		return hci_le_set_ext_scan_enable_sync(hdev, val, filter_dup);

	memset(&cp, 0, sizeof(cp));
	cp.enable = val;
	cp.filter_dup = filter_dup;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_SCAN_ENABLE,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_le_set_addr_resolution_enable_sync(struct hci_dev *hdev, u8 val)
{
	if (!use_ll_privacy(hdev))
		return 0;

	/* If controller is not/already resolving we are done. */
	if (val == hci_dev_test_flag(hdev, HCI_LL_RPA_RESOLUTION))
		return 0;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADDR_RESOLV_ENABLE,
				     sizeof(val), &val, HCI_CMD_TIMEOUT);
}

int hci_scan_disable_sync(struct hci_dev *hdev)
{
	int err;

	/* If controller is not scanning we are done. */
	if (!hci_dev_test_flag(hdev, HCI_LE_SCAN))
		return 0;

	if (hdev->scanning_paused) {
		bt_dev_dbg(hdev, "Scanning is paused for suspend");
		return 0;
	}

	if (hdev->suspended)
		set_bit(SUSPEND_SCAN_DISABLE, hdev->suspend_tasks);

	err = hci_le_set_scan_enable_sync(hdev, LE_SCAN_DISABLE, 0x00);
	if (err) {
		bt_dev_err(hdev, "Unable to disable scanning: %d", err);
		return err;
	}

	return err;
}

static bool scan_use_rpa(struct hci_dev *hdev)
{
	return hci_dev_test_flag(hdev, HCI_PRIVACY);
}

static void hci_start_interleave_scan(struct hci_dev *hdev)
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
static bool hci_update_interleaved_scan_sync(struct hci_dev *hdev)
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
		hci_start_interleave_scan(hdev);
		bt_dev_dbg(hdev, "starting interleave scan");
		return true;
	}

	if (!use_interleaving && is_interleaving)
		cancel_interleave_scan(hdev);

	return false;
}

/* Removes connection to resolve list if needed.*/
static int hci_le_del_resolve_list_sync(struct hci_dev *hdev,
					bdaddr_t *bdaddr, u8 bdaddr_type)
{
	struct hci_cp_le_del_from_resolv_list cp;
	struct bdaddr_list_with_irk *entry;

	if (!use_ll_privacy(hdev))
		return 0;

	/* Check if the IRK has been programmed */
	entry = hci_bdaddr_list_lookup_with_irk(&hdev->le_resolv_list, bdaddr,
						bdaddr_type);
	if (!entry)
		return 0;

	cp.bdaddr_type = bdaddr_type;
	bacpy(&cp.bdaddr, bdaddr);

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_DEL_FROM_RESOLV_LIST,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_le_del_accept_list_sync(struct hci_dev *hdev,
				       bdaddr_t *bdaddr, u8 bdaddr_type)
{
	struct hci_cp_le_del_from_accept_list cp;
	int err;

	/* Check if device is on accept list before removing it */
	if (!hci_bdaddr_list_lookup(&hdev->le_accept_list, bdaddr, bdaddr_type))
		return 0;

	cp.bdaddr_type = bdaddr_type;
	bacpy(&cp.bdaddr, bdaddr);

	/* Ignore errors when removing from resolving list as that is likely
	 * that the device was never added.
	 */
	hci_le_del_resolve_list_sync(hdev, &cp.bdaddr, cp.bdaddr_type);

	err = __hci_cmd_sync_status(hdev, HCI_OP_LE_DEL_FROM_ACCEPT_LIST,
				    sizeof(cp), &cp, HCI_CMD_TIMEOUT);
	if (err) {
		bt_dev_err(hdev, "Unable to remove from allow list: %d", err);
		return err;
	}

	bt_dev_dbg(hdev, "Remove %pMR (0x%x) from allow list", &cp.bdaddr,
		   cp.bdaddr_type);

	return 0;
}

/* Adds connection to resolve list if needed.
 * Setting params to NULL programs local hdev->irk
 */
static int hci_le_add_resolve_list_sync(struct hci_dev *hdev,
					struct hci_conn_params *params)
{
	struct hci_cp_le_add_to_resolv_list cp;
	struct smp_irk *irk;
	struct bdaddr_list_with_irk *entry;

	if (!use_ll_privacy(hdev))
		return 0;

	/* Attempt to program local identity address, type and irk if params is
	 * NULL.
	 */
	if (!params) {
		if (!hci_dev_test_flag(hdev, HCI_PRIVACY))
			return 0;

		hci_copy_identity_address(hdev, &cp.bdaddr, &cp.bdaddr_type);
		memcpy(cp.peer_irk, hdev->irk, 16);
		goto done;
	}

	irk = hci_find_irk_by_addr(hdev, &params->addr, params->addr_type);
	if (!irk)
		return 0;

	/* Check if the IK has _not_ been programmed yet. */
	entry = hci_bdaddr_list_lookup_with_irk(&hdev->le_resolv_list,
						&params->addr,
						params->addr_type);
	if (entry)
		return 0;

	cp.bdaddr_type = params->addr_type;
	bacpy(&cp.bdaddr, &params->addr);
	memcpy(cp.peer_irk, irk->val, 16);

done:
	if (hci_dev_test_flag(hdev, HCI_PRIVACY))
		memcpy(cp.local_irk, hdev->irk, 16);
	else
		memset(cp.local_irk, 0, 16);

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_ADD_TO_RESOLV_LIST,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

/* Adds connection to allow list if needed, if the device uses RPA (has IRK)
 * this attempts to program the device in the resolving list as well.
 */
static int hci_le_add_accept_list_sync(struct hci_dev *hdev,
				       struct hci_conn_params *params,
				       u8 *num_entries)
{
	struct hci_cp_le_add_to_accept_list cp;
	int err;

	/* Already in accept list */
	if (hci_bdaddr_list_lookup(&hdev->le_accept_list, &params->addr,
				   params->addr_type))
		return 0;

	/* Select filter policy to accept all advertising */
	if (*num_entries >= hdev->le_accept_list_size)
		return -ENOSPC;

	/* Accept list can not be used with RPAs */
	if (!use_ll_privacy(hdev) &&
	    hci_find_irk_by_addr(hdev, &params->addr, params->addr_type)) {
		return -EINVAL;
	}

	/* During suspend, only wakeable devices can be in acceptlist */
	if (hdev->suspended && !hci_conn_test_flag(HCI_CONN_FLAG_REMOTE_WAKEUP,
						   params->current_flags))
		return 0;

	/* Attempt to program the device in the resolving list first to avoid
	 * having to rollback in case it fails since the resolving list is
	 * dynamic it can probably be smaller than the accept list.
	 */
	err = hci_le_add_resolve_list_sync(hdev, params);
	if (err) {
		bt_dev_err(hdev, "Unable to add to resolve list: %d", err);
		return err;
	}

	*num_entries += 1;
	cp.bdaddr_type = params->addr_type;
	bacpy(&cp.bdaddr, &params->addr);

	err = __hci_cmd_sync_status(hdev, HCI_OP_LE_ADD_TO_ACCEPT_LIST,
				    sizeof(cp), &cp, HCI_CMD_TIMEOUT);
	if (err) {
		bt_dev_err(hdev, "Unable to add to allow list: %d", err);
		/* Rollback the device from the resolving list */
		hci_le_del_resolve_list_sync(hdev, &cp.bdaddr, cp.bdaddr_type);
		return err;
	}

	bt_dev_dbg(hdev, "Add %pMR (0x%x) to allow list", &cp.bdaddr,
		   cp.bdaddr_type);

	return 0;
}

/* This function disables all advertising instances (including 0x00) */
static int hci_pause_advertising_sync(struct hci_dev *hdev)
{
	int err;

	/* If there are no instances or advertising has already been paused
	 * there is nothing to do.
	 */
	if (!hdev->adv_instance_cnt || hdev->advertising_paused)
		return 0;

	bt_dev_dbg(hdev, "Pausing advertising instances");

	/* Call to disable any advertisements active on the controller.
	 * This will succeed even if no advertisements are configured.
	 */
	err = hci_disable_advertising_sync(hdev);
	if (err)
		return err;

	/* If we are using software rotation, pause the loop */
	if (!ext_adv_capable(hdev))
		cancel_adv_timeout(hdev);

	hdev->advertising_paused = true;

	return 0;
}

/* This function enables all user advertising instances (excluding 0x00) */
static int hci_resume_advertising_sync(struct hci_dev *hdev)
{
	struct adv_info *adv, *tmp;
	int err;

	/* If advertising has not been paused there is nothing  to do. */
	if (!hdev->advertising_paused)
		return 0;

	bt_dev_dbg(hdev, "Resuming advertising instances");

	if (ext_adv_capable(hdev)) {
		/* Call for each tracked instance to be re-enabled */
		list_for_each_entry_safe(adv, tmp, &hdev->adv_instances, list) {
			err = hci_enable_ext_advertising_sync(hdev,
							      adv->instance);
			if (!err)
				continue;

			/* If the instance cannot be resumed remove it */
			hci_remove_ext_adv_instance_sync(hdev, adv->instance,
							 NULL);
		}
	} else {
		/* Schedule for most recent instance to be restarted and begin
		 * the software rotation loop
		 */
		err = hci_schedule_adv_instance_sync(hdev,
						     hdev->cur_adv_instance,
						     true);
	}

	hdev->advertising_paused = false;

	return err;
}

struct sk_buff *hci_read_local_oob_data_sync(struct hci_dev *hdev,
					     bool extended, struct sock *sk)
{
	u16 opcode = extended ? HCI_OP_READ_LOCAL_OOB_EXT_DATA :
					HCI_OP_READ_LOCAL_OOB_DATA;

	return __hci_cmd_sync_sk(hdev, opcode, 0, NULL, 0, HCI_CMD_TIMEOUT, sk);
}

/* Device must not be scanning when updating the accept list.
 *
 * Update is done using the following sequence:
 *
 * use_ll_privacy((Disable Advertising) -> Disable Resolving List) ->
 * Remove Devices From Accept List ->
 * (has IRK && use_ll_privacy(Remove Devices From Resolving List))->
 * Add Devices to Accept List ->
 * (has IRK && use_ll_privacy(Remove Devices From Resolving List)) ->
 * use_ll_privacy(Enable Resolving List -> (Enable Advertising)) ->
 * Enable Scanning
 *
 * In case of failure advertising shall be restored to its original state and
 * return would disable accept list since either accept or resolving list could
 * not be programmed.
 *
 */
static u8 hci_update_accept_list_sync(struct hci_dev *hdev)
{
	struct hci_conn_params *params;
	struct bdaddr_list *b, *t;
	u8 num_entries = 0;
	bool pend_conn, pend_report;
	int err;

	/* Pause advertising if resolving list can be used as controllers are
	 * cannot accept resolving list modifications while advertising.
	 */
	if (use_ll_privacy(hdev)) {
		err = hci_pause_advertising_sync(hdev);
		if (err) {
			bt_dev_err(hdev, "pause advertising failed: %d", err);
			return 0x00;
		}
	}

	/* Disable address resolution while reprogramming accept list since
	 * devices that do have an IRK will be programmed in the resolving list
	 * when LL Privacy is enabled.
	 */
	err = hci_le_set_addr_resolution_enable_sync(hdev, 0x00);
	if (err) {
		bt_dev_err(hdev, "Unable to disable LL privacy: %d", err);
		goto done;
	}

	/* Go through the current accept list programmed into the
	 * controller one by one and check if that address is still
	 * in the list of pending connections or list of devices to
	 * report. If not present in either list, then remove it from
	 * the controller.
	 */
	list_for_each_entry_safe(b, t, &hdev->le_accept_list, list) {
		pend_conn = hci_pend_le_action_lookup(&hdev->pend_le_conns,
						      &b->bdaddr,
						      b->bdaddr_type);
		pend_report = hci_pend_le_action_lookup(&hdev->pend_le_reports,
							&b->bdaddr,
							b->bdaddr_type);

		/* If the device is not likely to connect or report,
		 * remove it from the acceptlist.
		 */
		if (!pend_conn && !pend_report) {
			hci_le_del_accept_list_sync(hdev, &b->bdaddr,
						    b->bdaddr_type);
			continue;
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
		err = hci_le_add_accept_list_sync(hdev, params, &num_entries);
		if (err)
			goto done;
	}

	/* After adding all new pending connections, walk through
	 * the list of pending reports and also add these to the
	 * accept list if there is still space. Abort if space runs out.
	 */
	list_for_each_entry(params, &hdev->pend_le_reports, action) {
		err = hci_le_add_accept_list_sync(hdev, params, &num_entries);
		if (err)
			goto done;
	}

	/* Use the allowlist unless the following conditions are all true:
	 * - We are not currently suspending
	 * - There are 1 or more ADV monitors registered and it's not offloaded
	 * - Interleaved scanning is not currently using the allowlist
	 */
	if (!idr_is_empty(&hdev->adv_monitors_idr) && !hdev->suspended &&
	    hci_get_adv_monitor_offload_ext(hdev) == HCI_ADV_MONITOR_EXT_NONE &&
	    hdev->interleave_scan_state != INTERLEAVE_SCAN_ALLOWLIST)
		err = -EINVAL;

done:
	/* Enable address resolution when LL Privacy is enabled. */
	err = hci_le_set_addr_resolution_enable_sync(hdev, 0x01);
	if (err)
		bt_dev_err(hdev, "Unable to enable LL privacy: %d", err);

	/* Resume advertising if it was paused */
	if (use_ll_privacy(hdev))
		hci_resume_advertising_sync(hdev);

	/* Select filter policy to use accept list */
	return err ? 0x00 : 0x01;
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

static int hci_le_set_ext_scan_param_sync(struct hci_dev *hdev, u8 type,
					  u16 interval, u16 window,
					  u8 own_addr_type, u8 filter_policy)
{
	struct hci_cp_le_set_ext_scan_params *cp;
	struct hci_cp_le_scan_phy_params *phy;
	u8 data[sizeof(*cp) + sizeof(*phy) * 2];
	u8 num_phy = 0;

	cp = (void *)data;
	phy = (void *)cp->data;

	memset(data, 0, sizeof(data));

	cp->own_addr_type = own_addr_type;
	cp->filter_policy = filter_policy;

	if (scan_1m(hdev) || scan_2m(hdev)) {
		cp->scanning_phys |= LE_SCAN_PHY_1M;

		phy->type = type;
		phy->interval = cpu_to_le16(interval);
		phy->window = cpu_to_le16(window);

		num_phy++;
		phy++;
	}

	if (scan_coded(hdev)) {
		cp->scanning_phys |= LE_SCAN_PHY_CODED;

		phy->type = type;
		phy->interval = cpu_to_le16(interval);
		phy->window = cpu_to_le16(window);

		num_phy++;
		phy++;
	}

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_SCAN_PARAMS,
				     sizeof(*cp) + sizeof(*phy) * num_phy,
				     data, HCI_CMD_TIMEOUT);
}

static int hci_le_set_scan_param_sync(struct hci_dev *hdev, u8 type,
				      u16 interval, u16 window,
				      u8 own_addr_type, u8 filter_policy)
{
	struct hci_cp_le_set_scan_param cp;

	if (use_ext_scan(hdev))
		return hci_le_set_ext_scan_param_sync(hdev, type, interval,
						      window, own_addr_type,
						      filter_policy);

	memset(&cp, 0, sizeof(cp));
	cp.type = type;
	cp.interval = cpu_to_le16(interval);
	cp.window = cpu_to_le16(window);
	cp.own_address_type = own_addr_type;
	cp.filter_policy = filter_policy;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_SCAN_PARAM,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_start_scan_sync(struct hci_dev *hdev, u8 type, u16 interval,
			       u16 window, u8 own_addr_type, u8 filter_policy,
			       u8 filter_dup)
{
	int err;

	if (hdev->scanning_paused) {
		bt_dev_dbg(hdev, "Scanning is paused for suspend");
		return 0;
	}

	err = hci_le_set_scan_param_sync(hdev, type, interval, window,
					 own_addr_type, filter_policy);
	if (err)
		return err;

	return hci_le_set_scan_enable_sync(hdev, LE_SCAN_ENABLE, filter_dup);
}

int hci_passive_scan_sync(struct hci_dev *hdev)
{
	u8 own_addr_type;
	u8 filter_policy;
	u16 window, interval;
	int err;

	if (hdev->scanning_paused) {
		bt_dev_dbg(hdev, "Scanning is paused for suspend");
		return 0;
	}

	err = hci_scan_disable_sync(hdev);
	if (err) {
		bt_dev_err(hdev, "disable scanning failed: %d", err);
		return err;
	}

	/* Set require_privacy to false since no SCAN_REQ are send
	 * during passive scanning. Not using an non-resolvable address
	 * here is important so that peer devices using direct
	 * advertising with our address will be correctly reported
	 * by the controller.
	 */
	if (hci_update_random_address_sync(hdev, false, scan_use_rpa(hdev),
					   &own_addr_type))
		return 0;

	if (hdev->enable_advmon_interleave_scan &&
	    hci_update_interleaved_scan_sync(hdev))
		return 0;

	bt_dev_dbg(hdev, "interleave state %d", hdev->interleave_scan_state);

	/* Adding or removing entries from the accept list must
	 * happen before enabling scanning. The controller does
	 * not allow accept list modification while scanning.
	 */
	filter_policy = hci_update_accept_list_sync(hdev);

	/* When the controller is using random resolvable addresses and
	 * with that having LE privacy enabled, then controllers with
	 * Extended Scanner Filter Policies support can now enable support
	 * for handling directed advertising.
	 *
	 * So instead of using filter polices 0x00 (no acceptlist)
	 * and 0x01 (acceptlist enabled) use the new filter policies
	 * 0x02 (no acceptlist) and 0x03 (acceptlist enabled).
	 */
	if (hci_dev_test_flag(hdev, HCI_PRIVACY) &&
	    (hdev->le_features[0] & HCI_LE_EXT_SCAN_POLICY))
		filter_policy |= 0x02;

	if (hdev->suspended) {
		window = hdev->le_scan_window_suspend;
		interval = hdev->le_scan_int_suspend;

		set_bit(SUSPEND_SCAN_ENABLE, hdev->suspend_tasks);
	} else if (hci_is_le_conn_scanning(hdev)) {
		window = hdev->le_scan_window_connect;
		interval = hdev->le_scan_int_connect;
	} else if (hci_is_adv_monitoring(hdev)) {
		window = hdev->le_scan_window_adv_monitor;
		interval = hdev->le_scan_int_adv_monitor;
	} else {
		window = hdev->le_scan_window;
		interval = hdev->le_scan_interval;
	}

	bt_dev_dbg(hdev, "LE passive scan with acceptlist = %d", filter_policy);

	return hci_start_scan_sync(hdev, LE_SCAN_PASSIVE, interval, window,
				   own_addr_type, filter_policy,
				   LE_SCAN_FILTER_DUP_ENABLE);
}

/* This function controls the passive scanning based on hdev->pend_le_conns
 * list. If there are pending LE connection we start the background scanning,
 * otherwise we stop it in the following sequence:
 *
 * If there are devices to scan:
 *
 * Disable Scanning -> Update Accept List ->
 * use_ll_privacy((Disable Advertising) -> Disable Resolving List ->
 * Update Resolving List -> Enable Resolving List -> (Enable Advertising)) ->
 * Enable Scanning
 *
 * Otherwise:
 *
 * Disable Scanning
 */
int hci_update_passive_scan_sync(struct hci_dev *hdev)
{
	int err;

	if (!test_bit(HCI_UP, &hdev->flags) ||
	    test_bit(HCI_INIT, &hdev->flags) ||
	    hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG) ||
	    hci_dev_test_flag(hdev, HCI_AUTO_OFF) ||
	    hci_dev_test_flag(hdev, HCI_UNREGISTER))
		return 0;

	/* No point in doing scanning if LE support hasn't been enabled */
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return 0;

	/* If discovery is active don't interfere with it */
	if (hdev->discovery.state != DISCOVERY_STOPPED)
		return 0;

	/* Reset RSSI and UUID filters when starting background scanning
	 * since these filters are meant for service discovery only.
	 *
	 * The Start Discovery and Start Service Discovery operations
	 * ensure to set proper values for RSSI threshold and UUID
	 * filter list. So it is safe to just reset them here.
	 */
	hci_discovery_filter_clear(hdev);

	bt_dev_dbg(hdev, "ADV monitoring is %s",
		   hci_is_adv_monitoring(hdev) ? "on" : "off");

	if (list_empty(&hdev->pend_le_conns) &&
	    list_empty(&hdev->pend_le_reports) &&
	    !hci_is_adv_monitoring(hdev)) {
		/* If there is no pending LE connections or devices
		 * to be scanned for or no ADV monitors, we should stop the
		 * background scanning.
		 */

		bt_dev_dbg(hdev, "stopping background scanning");

		err = hci_scan_disable_sync(hdev);
		if (err)
			bt_dev_err(hdev, "stop background scanning failed: %d",
				   err);
	} else {
		/* If there is at least one pending LE connection, we should
		 * keep the background scan running.
		 */

		/* If controller is connecting, we should not start scanning
		 * since some controllers are not able to scan and connect at
		 * the same time.
		 */
		if (hci_lookup_le_connect(hdev))
			return 0;

		bt_dev_dbg(hdev, "start background scanning");

		err = hci_passive_scan_sync(hdev);
		if (err)
			bt_dev_err(hdev, "start background scanning failed: %d",
				   err);
	}

	return err;
}

static int update_passive_scan_sync(struct hci_dev *hdev, void *data)
{
	return hci_update_passive_scan_sync(hdev);
}

int hci_update_passive_scan(struct hci_dev *hdev)
{
	/* Only queue if it would have any effect */
	if (!test_bit(HCI_UP, &hdev->flags) ||
	    test_bit(HCI_INIT, &hdev->flags) ||
	    hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG) ||
	    hci_dev_test_flag(hdev, HCI_AUTO_OFF) ||
	    hci_dev_test_flag(hdev, HCI_UNREGISTER))
		return 0;

	return hci_cmd_sync_queue(hdev, update_passive_scan_sync, NULL, NULL);
}

int hci_write_sc_support_sync(struct hci_dev *hdev, u8 val)
{
	int err;

	if (!bredr_sc_enabled(hdev) || lmp_host_sc_capable(hdev))
		return 0;

	err = __hci_cmd_sync_status(hdev, HCI_OP_WRITE_SC_SUPPORT,
				    sizeof(val), &val, HCI_CMD_TIMEOUT);

	if (!err) {
		if (val) {
			hdev->features[1][0] |= LMP_HOST_SC;
			hci_dev_set_flag(hdev, HCI_SC_ENABLED);
		} else {
			hdev->features[1][0] &= ~LMP_HOST_SC;
			hci_dev_clear_flag(hdev, HCI_SC_ENABLED);
		}
	}

	return err;
}

static int hci_write_ssp_mode_sync(struct hci_dev *hdev, u8 mode)
{
	int err;

	if (!hci_dev_test_flag(hdev, HCI_SSP_ENABLED) ||
	    lmp_host_ssp_capable(hdev))
		return 0;

	err = __hci_cmd_sync_status(hdev, HCI_OP_WRITE_SSP_MODE,
				    sizeof(mode), &mode, HCI_CMD_TIMEOUT);
	if (err)
		return err;

	return hci_write_sc_support_sync(hdev, 0x01);
}

int hci_write_le_host_supported_sync(struct hci_dev *hdev, u8 le, u8 simul)
{
	struct hci_cp_write_le_host_supported cp;

	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED) ||
	    !lmp_bredr_capable(hdev))
		return 0;

	/* Check first if we already have the right host state
	 * (host features set)
	 */
	if (le == lmp_host_le_capable(hdev) &&
	    simul == lmp_host_le_br_capable(hdev))
		return 0;

	memset(&cp, 0, sizeof(cp));

	cp.le = le;
	cp.simul = simul;

	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_LE_HOST_SUPPORTED,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_powered_update_adv_sync(struct hci_dev *hdev)
{
	struct adv_info *adv, *tmp;
	int err;

	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return 0;

	/* If RPA Resolution has not been enable yet it means the
	 * resolving list is empty and we should attempt to program the
	 * local IRK in order to support using own_addr_type
	 * ADDR_LE_DEV_RANDOM_RESOLVED (0x03).
	 */
	if (!hci_dev_test_flag(hdev, HCI_LL_RPA_RESOLUTION)) {
		hci_le_add_resolve_list_sync(hdev, NULL);
		hci_le_set_addr_resolution_enable_sync(hdev, 0x01);
	}

	/* Make sure the controller has a good default for
	 * advertising data. This also applies to the case
	 * where BR/EDR was toggled during the AUTO_OFF phase.
	 */
	if (hci_dev_test_flag(hdev, HCI_ADVERTISING) ||
	    list_empty(&hdev->adv_instances)) {
		if (ext_adv_capable(hdev)) {
			err = hci_setup_ext_adv_instance_sync(hdev, 0x00);
			if (!err)
				hci_update_scan_rsp_data_sync(hdev, 0x00);
		} else {
			err = hci_update_adv_data_sync(hdev, 0x00);
			if (!err)
				hci_update_scan_rsp_data_sync(hdev, 0x00);
		}

		if (hci_dev_test_flag(hdev, HCI_ADVERTISING))
			hci_enable_advertising_sync(hdev);
	}

	/* Call for each tracked instance to be scheduled */
	list_for_each_entry_safe(adv, tmp, &hdev->adv_instances, list)
		hci_schedule_adv_instance_sync(hdev, adv->instance, true);

	return 0;
}

static int hci_write_auth_enable_sync(struct hci_dev *hdev)
{
	u8 link_sec;

	link_sec = hci_dev_test_flag(hdev, HCI_LINK_SECURITY);
	if (link_sec == test_bit(HCI_AUTH, &hdev->flags))
		return 0;

	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_AUTH_ENABLE,
				     sizeof(link_sec), &link_sec,
				     HCI_CMD_TIMEOUT);
}

int hci_write_fast_connectable_sync(struct hci_dev *hdev, bool enable)
{
	struct hci_cp_write_page_scan_activity cp;
	u8 type;
	int err = 0;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return 0;

	if (hdev->hci_ver < BLUETOOTH_VER_1_2)
		return 0;

	memset(&cp, 0, sizeof(cp));

	if (enable) {
		type = PAGE_SCAN_TYPE_INTERLACED;

		/* 160 msec page scan interval */
		cp.interval = cpu_to_le16(0x0100);
	} else {
		type = hdev->def_page_scan_type;
		cp.interval = cpu_to_le16(hdev->def_page_scan_int);
	}

	cp.window = cpu_to_le16(hdev->def_page_scan_window);

	if (__cpu_to_le16(hdev->page_scan_interval) != cp.interval ||
	    __cpu_to_le16(hdev->page_scan_window) != cp.window) {
		err = __hci_cmd_sync_status(hdev,
					    HCI_OP_WRITE_PAGE_SCAN_ACTIVITY,
					    sizeof(cp), &cp, HCI_CMD_TIMEOUT);
		if (err)
			return err;
	}

	if (hdev->page_scan_type != type)
		err = __hci_cmd_sync_status(hdev,
					    HCI_OP_WRITE_PAGE_SCAN_TYPE,
					    sizeof(type), &type,
					    HCI_CMD_TIMEOUT);

	return err;
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

static int hci_write_scan_enable_sync(struct hci_dev *hdev, u8 val)
{
	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_SCAN_ENABLE,
					    sizeof(val), &val,
					    HCI_CMD_TIMEOUT);
}

int hci_update_scan_sync(struct hci_dev *hdev)
{
	u8 scan;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return 0;

	if (!hdev_is_powered(hdev))
		return 0;

	if (mgmt_powering_down(hdev))
		return 0;

	if (hdev->scanning_paused)
		return 0;

	if (hci_dev_test_flag(hdev, HCI_CONNECTABLE) ||
	    disconnected_accept_list_entries(hdev))
		scan = SCAN_PAGE;
	else
		scan = SCAN_DISABLED;

	if (hci_dev_test_flag(hdev, HCI_DISCOVERABLE))
		scan |= SCAN_INQUIRY;

	if (test_bit(HCI_PSCAN, &hdev->flags) == !!(scan & SCAN_PAGE) &&
	    test_bit(HCI_ISCAN, &hdev->flags) == !!(scan & SCAN_INQUIRY))
		return 0;

	return hci_write_scan_enable_sync(hdev, scan);
}

int hci_update_name_sync(struct hci_dev *hdev)
{
	struct hci_cp_write_local_name cp;

	memset(&cp, 0, sizeof(cp));

	memcpy(cp.name, hdev->dev_name, sizeof(cp.name));

	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_LOCAL_NAME,
					    sizeof(cp), &cp,
					    HCI_CMD_TIMEOUT);
}

/* This function perform powered update HCI command sequence after the HCI init
 * sequence which end up resetting all states, the sequence is as follows:
 *
 * HCI_SSP_ENABLED(Enable SSP)
 * HCI_LE_ENABLED(Enable LE)
 * HCI_LE_ENABLED(use_ll_privacy(Add local IRK to Resolving List) ->
 * Update adv data)
 * Enable Authentication
 * lmp_bredr_capable(Set Fast Connectable -> Set Scan Type -> Set Class ->
 * Set Name -> Set EIR)
 */
int hci_powered_update_sync(struct hci_dev *hdev)
{
	int err;

	/* Register the available SMP channels (BR/EDR and LE) only when
	 * successfully powering on the controller. This late
	 * registration is required so that LE SMP can clearly decide if
	 * the public address or static address is used.
	 */
	smp_register(hdev);

	err = hci_write_ssp_mode_sync(hdev, 0x01);
	if (err)
		return err;

	err = hci_write_le_host_supported_sync(hdev, 0x01, 0x00);
	if (err)
		return err;

	err = hci_powered_update_adv_sync(hdev);
	if (err)
		return err;

	err = hci_write_auth_enable_sync(hdev);
	if (err)
		return err;

	if (lmp_bredr_capable(hdev)) {
		if (hci_dev_test_flag(hdev, HCI_FAST_CONNECTABLE))
			hci_write_fast_connectable_sync(hdev, true);
		else
			hci_write_fast_connectable_sync(hdev, false);
		hci_update_scan_sync(hdev);
		hci_update_class_sync(hdev);
		hci_update_name_sync(hdev);
		hci_update_eir_sync(hdev);
	}

	return 0;
}

/* This function perform power on HCI command sequence as follows:
 *
 * If controller is already up (HCI_UP) performs hci_powered_update_sync
 * sequence otherwise run hci_dev_open_sync which will follow with
 * hci_powered_update_sync after the init sequence is completed.
 */
static int hci_power_on_sync(struct hci_dev *hdev)
{
	int err;

	if (test_bit(HCI_UP, &hdev->flags) &&
	    hci_dev_test_flag(hdev, HCI_MGMT) &&
	    hci_dev_test_and_clear_flag(hdev, HCI_AUTO_OFF)) {
		cancel_delayed_work(&hdev->power_off);
		return hci_powered_update_sync(hdev);
	}

	err = hci_dev_open_sync(hdev);
	if (err < 0)
		return err;

	/* During the HCI setup phase, a few error conditions are
	 * ignored and they need to be checked now. If they are still
	 * valid, it is important to return the device back off.
	 */
	if (hci_dev_test_flag(hdev, HCI_RFKILLED) ||
	    hci_dev_test_flag(hdev, HCI_UNCONFIGURED) ||
	    (hdev->dev_type == HCI_PRIMARY &&
	     !bacmp(&hdev->bdaddr, BDADDR_ANY) &&
	     !bacmp(&hdev->static_addr, BDADDR_ANY))) {
		hci_dev_clear_flag(hdev, HCI_AUTO_OFF);
		hci_dev_close_sync(hdev);
	} else if (hci_dev_test_flag(hdev, HCI_AUTO_OFF)) {
		queue_delayed_work(hdev->req_workqueue, &hdev->power_off,
				   HCI_AUTO_OFF_TIMEOUT);
	}

	if (hci_dev_test_and_clear_flag(hdev, HCI_SETUP)) {
		/* For unconfigured devices, set the HCI_RAW flag
		 * so that userspace can easily identify them.
		 */
		if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED))
			set_bit(HCI_RAW, &hdev->flags);

		/* For fully configured devices, this will send
		 * the Index Added event. For unconfigured devices,
		 * it will send Unconfigued Index Added event.
		 *
		 * Devices with HCI_QUIRK_RAW_DEVICE are ignored
		 * and no event will be send.
		 */
		mgmt_index_added(hdev);
	} else if (hci_dev_test_and_clear_flag(hdev, HCI_CONFIG)) {
		/* When the controller is now configured, then it
		 * is important to clear the HCI_RAW flag.
		 */
		if (!hci_dev_test_flag(hdev, HCI_UNCONFIGURED))
			clear_bit(HCI_RAW, &hdev->flags);

		/* Powering on the controller with HCI_CONFIG set only
		 * happens with the transition from unconfigured to
		 * configured. This will send the Index Added event.
		 */
		mgmt_index_added(hdev);
	}

	return 0;
}

static int hci_remote_name_cancel_sync(struct hci_dev *hdev, bdaddr_t *addr)
{
	struct hci_cp_remote_name_req_cancel cp;

	memset(&cp, 0, sizeof(cp));
	bacpy(&cp.bdaddr, addr);

	return __hci_cmd_sync_status(hdev, HCI_OP_REMOTE_NAME_REQ_CANCEL,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_stop_discovery_sync(struct hci_dev *hdev)
{
	struct discovery_state *d = &hdev->discovery;
	struct inquiry_entry *e;
	int err;

	bt_dev_dbg(hdev, "state %u", hdev->discovery.state);

	if (d->state == DISCOVERY_FINDING || d->state == DISCOVERY_STOPPING) {
		if (test_bit(HCI_INQUIRY, &hdev->flags)) {
			err = __hci_cmd_sync_status(hdev, HCI_OP_INQUIRY_CANCEL,
						    0, NULL, HCI_CMD_TIMEOUT);
			if (err)
				return err;
		}

		if (hci_dev_test_flag(hdev, HCI_LE_SCAN)) {
			cancel_delayed_work(&hdev->le_scan_disable);
			cancel_delayed_work(&hdev->le_scan_restart);

			err = hci_scan_disable_sync(hdev);
			if (err)
				return err;
		}

	} else {
		err = hci_scan_disable_sync(hdev);
		if (err)
			return err;
	}

	/* Resume advertising if it was paused */
	if (use_ll_privacy(hdev))
		hci_resume_advertising_sync(hdev);

	/* No further actions needed for LE-only discovery */
	if (d->type == DISCOV_TYPE_LE)
		return 0;

	if (d->state == DISCOVERY_RESOLVING || d->state == DISCOVERY_STOPPING) {
		e = hci_inquiry_cache_lookup_resolve(hdev, BDADDR_ANY,
						     NAME_PENDING);
		if (!e)
			return 0;

		return hci_remote_name_cancel_sync(hdev, &e->data.bdaddr);
	}

	return 0;
}

static int hci_disconnect_phy_link_sync(struct hci_dev *hdev, u16 handle,
					u8 reason)
{
	struct hci_cp_disconn_phy_link cp;

	memset(&cp, 0, sizeof(cp));
	cp.phy_handle = HCI_PHY_HANDLE(handle);
	cp.reason = reason;

	return __hci_cmd_sync_status(hdev, HCI_OP_DISCONN_PHY_LINK,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_disconnect_sync(struct hci_dev *hdev, struct hci_conn *conn,
			       u8 reason)
{
	struct hci_cp_disconnect cp;

	if (conn->type == AMP_LINK)
		return hci_disconnect_phy_link_sync(hdev, conn->handle, reason);

	memset(&cp, 0, sizeof(cp));
	cp.handle = cpu_to_le16(conn->handle);
	cp.reason = reason;

	return __hci_cmd_sync_status(hdev, HCI_OP_DISCONNECT,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_le_connect_cancel_sync(struct hci_dev *hdev,
				      struct hci_conn *conn)
{
	if (test_bit(HCI_CONN_SCANNING, &conn->flags))
		return 0;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_CREATE_CONN_CANCEL,
				     6, &conn->dst, HCI_CMD_TIMEOUT);
}

static int hci_connect_cancel_sync(struct hci_dev *hdev, struct hci_conn *conn)
{
	if (conn->type == LE_LINK)
		return hci_le_connect_cancel_sync(hdev, conn);

	if (hdev->hci_ver < BLUETOOTH_VER_1_2)
		return 0;

	return __hci_cmd_sync_status(hdev, HCI_OP_CREATE_CONN_CANCEL,
				     6, &conn->dst, HCI_CMD_TIMEOUT);
}

static int hci_reject_sco_sync(struct hci_dev *hdev, struct hci_conn *conn,
			       u8 reason)
{
	struct hci_cp_reject_sync_conn_req cp;

	memset(&cp, 0, sizeof(cp));
	bacpy(&cp.bdaddr, &conn->dst);
	cp.reason = reason;

	/* SCO rejection has its own limited set of
	 * allowed error values (0x0D-0x0F).
	 */
	if (reason < 0x0d || reason > 0x0f)
		cp.reason = HCI_ERROR_REJ_LIMITED_RESOURCES;

	return __hci_cmd_sync_status(hdev, HCI_OP_REJECT_SYNC_CONN_REQ,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_reject_conn_sync(struct hci_dev *hdev, struct hci_conn *conn,
				u8 reason)
{
	struct hci_cp_reject_conn_req cp;

	if (conn->type == SCO_LINK || conn->type == ESCO_LINK)
		return hci_reject_sco_sync(hdev, conn, reason);

	memset(&cp, 0, sizeof(cp));
	bacpy(&cp.bdaddr, &conn->dst);
	cp.reason = reason;

	return __hci_cmd_sync_status(hdev, HCI_OP_REJECT_CONN_REQ,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_abort_conn_sync(struct hci_dev *hdev, struct hci_conn *conn,
			       u8 reason)
{
	switch (conn->state) {
	case BT_CONNECTED:
	case BT_CONFIG:
		return hci_disconnect_sync(hdev, conn, reason);
	case BT_CONNECT:
		return hci_connect_cancel_sync(hdev, conn);
	case BT_CONNECT2:
		return hci_reject_conn_sync(hdev, conn, reason);
	default:
		conn->state = BT_CLOSED;
		break;
	}

	return 0;
}

/* This function perform power off HCI command sequence as follows:
 *
 * Clear Advertising
 * Stop Discovery
 * Disconnect all connections
 * hci_dev_close_sync
 */
static int hci_power_off_sync(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	int err;

	/* If controller is already down there is nothing to do */
	if (!test_bit(HCI_UP, &hdev->flags))
		return 0;

	if (test_bit(HCI_ISCAN, &hdev->flags) ||
	    test_bit(HCI_PSCAN, &hdev->flags)) {
		err = hci_write_scan_enable_sync(hdev, 0x00);
		if (err)
			return err;
	}

	err = hci_clear_adv_sync(hdev, NULL, false);
	if (err)
		return err;

	err = hci_stop_discovery_sync(hdev);
	if (err)
		return err;

	list_for_each_entry(conn, &hdev->conn_hash.list, list) {
		/* 0x15 == Terminated due to Power Off */
		hci_abort_conn_sync(hdev, conn, 0x15);
	}

	return hci_dev_close_sync(hdev);
}

int hci_set_powered_sync(struct hci_dev *hdev, u8 val)
{
	if (val)
		return hci_power_on_sync(hdev);

	return hci_power_off_sync(hdev);
}

static int hci_inquiry_sync(struct hci_dev *hdev, u8 length)
{
	const u8 giac[3] = { 0x33, 0x8b, 0x9e };
	const u8 liac[3] = { 0x00, 0x8b, 0x9e };
	struct hci_cp_inquiry cp;

	bt_dev_dbg(hdev, "");

	if (hci_dev_test_flag(hdev, HCI_INQUIRY))
		return 0;

	hci_dev_lock(hdev);
	hci_inquiry_cache_flush(hdev);
	hci_dev_unlock(hdev);

	memset(&cp, 0, sizeof(cp));

	if (hdev->discovery.limited)
		memcpy(&cp.lap, liac, sizeof(cp.lap));
	else
		memcpy(&cp.lap, giac, sizeof(cp.lap));

	cp.length = length;

	return __hci_cmd_sync_status(hdev, HCI_OP_INQUIRY,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_active_scan_sync(struct hci_dev *hdev, uint16_t interval)
{
	u8 own_addr_type;
	/* Accept list is not used for discovery */
	u8 filter_policy = 0x00;
	/* Default is to enable duplicates filter */
	u8 filter_dup = LE_SCAN_FILTER_DUP_ENABLE;
	int err;

	bt_dev_dbg(hdev, "");

	/* If controller is scanning, it means the passive scanning is
	 * running. Thus, we should temporarily stop it in order to set the
	 * discovery scanning parameters.
	 */
	err = hci_scan_disable_sync(hdev);
	if (err) {
		bt_dev_err(hdev, "Unable to disable scanning: %d", err);
		return err;
	}

	cancel_interleave_scan(hdev);

	/* Pause advertising since active scanning disables address resolution
	 * which advertising depend on in order to generate its RPAs.
	 */
	if (use_ll_privacy(hdev)) {
		err = hci_pause_advertising_sync(hdev);
		if (err) {
			bt_dev_err(hdev, "pause advertising failed: %d", err);
			goto failed;
		}
	}

	/* Disable address resolution while doing active scanning since the
	 * accept list shall not be used and all reports shall reach the host
	 * anyway.
	 */
	err = hci_le_set_addr_resolution_enable_sync(hdev, 0x00);
	if (err) {
		bt_dev_err(hdev, "Unable to disable Address Resolution: %d",
			   err);
		goto failed;
	}

	/* All active scans will be done with either a resolvable private
	 * address (when privacy feature has been enabled) or non-resolvable
	 * private address.
	 */
	err = hci_update_random_address_sync(hdev, true, scan_use_rpa(hdev),
					     &own_addr_type);
	if (err < 0)
		own_addr_type = ADDR_LE_DEV_PUBLIC;

	if (hci_is_adv_monitoring(hdev)) {
		/* Duplicate filter should be disabled when some advertisement
		 * monitor is activated, otherwise AdvMon can only receive one
		 * advertisement for one peer(*) during active scanning, and
		 * might report loss to these peers.
		 *
		 * Note that different controllers have different meanings of
		 * |duplicate|. Some of them consider packets with the same
		 * address as duplicate, and others consider packets with the
		 * same address and the same RSSI as duplicate. Although in the
		 * latter case we don't need to disable duplicate filter, but
		 * it is common to have active scanning for a short period of
		 * time, the power impact should be neglectable.
		 */
		filter_dup = LE_SCAN_FILTER_DUP_DISABLE;
	}

	err = hci_start_scan_sync(hdev, LE_SCAN_ACTIVE, interval,
				  hdev->le_scan_window_discovery,
				  own_addr_type, filter_policy, filter_dup);
	if (!err)
		return err;

failed:
	/* Resume advertising if it was paused */
	if (use_ll_privacy(hdev))
		hci_resume_advertising_sync(hdev);

	/* Resume passive scanning */
	hci_update_passive_scan_sync(hdev);
	return err;
}

static int hci_start_interleaved_discovery_sync(struct hci_dev *hdev)
{
	int err;

	bt_dev_dbg(hdev, "");

	err = hci_active_scan_sync(hdev, hdev->le_scan_int_discovery * 2);
	if (err)
		return err;

	return hci_inquiry_sync(hdev, DISCOV_BREDR_INQUIRY_LEN);
}

int hci_start_discovery_sync(struct hci_dev *hdev)
{
	unsigned long timeout;
	int err;

	bt_dev_dbg(hdev, "type %u", hdev->discovery.type);

	switch (hdev->discovery.type) {
	case DISCOV_TYPE_BREDR:
		return hci_inquiry_sync(hdev, DISCOV_BREDR_INQUIRY_LEN);
	case DISCOV_TYPE_INTERLEAVED:
		/* When running simultaneous discovery, the LE scanning time
		 * should occupy the whole discovery time sine BR/EDR inquiry
		 * and LE scanning are scheduled by the controller.
		 *
		 * For interleaving discovery in comparison, BR/EDR inquiry
		 * and LE scanning are done sequentially with separate
		 * timeouts.
		 */
		if (test_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY,
			     &hdev->quirks)) {
			timeout = msecs_to_jiffies(DISCOV_LE_TIMEOUT);
			/* During simultaneous discovery, we double LE scan
			 * interval. We must leave some time for the controller
			 * to do BR/EDR inquiry.
			 */
			err = hci_start_interleaved_discovery_sync(hdev);
			break;
		}

		timeout = msecs_to_jiffies(hdev->discov_interleaved_timeout);
		err = hci_active_scan_sync(hdev, hdev->le_scan_int_discovery);
		break;
	case DISCOV_TYPE_LE:
		timeout = msecs_to_jiffies(DISCOV_LE_TIMEOUT);
		err = hci_active_scan_sync(hdev, hdev->le_scan_int_discovery);
		break;
	default:
		return -EINVAL;
	}

	if (err)
		return err;

	bt_dev_dbg(hdev, "timeout %u ms", jiffies_to_msecs(timeout));

	/* When service discovery is used and the controller has a
	 * strict duplicate filter, it is important to remember the
	 * start and duration of the scan. This is required for
	 * restarting scanning during the discovery phase.
	 */
	if (test_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks) &&
	    hdev->discovery.result_filtering) {
		hdev->discovery.scan_start = jiffies;
		hdev->discovery.scan_duration = timeout;
	}

	queue_delayed_work(hdev->req_workqueue, &hdev->le_scan_disable,
			   timeout);

	return 0;
}
