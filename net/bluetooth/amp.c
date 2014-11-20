/*
   Copyright (c) 2011,2012 Intel Corp.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 and
   only version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/hci_core.h>
#include <crypto/hash.h>

#include "a2mp.h"
#include "amp.h"

/* Remote AMP Controllers interface */
void amp_ctrl_get(struct amp_ctrl *ctrl)
{
	BT_DBG("ctrl %p orig refcnt %d", ctrl,
	       atomic_read(&ctrl->kref.refcount));

	kref_get(&ctrl->kref);
}

static void amp_ctrl_destroy(struct kref *kref)
{
	struct amp_ctrl *ctrl = container_of(kref, struct amp_ctrl, kref);

	BT_DBG("ctrl %p", ctrl);

	kfree(ctrl->assoc);
	kfree(ctrl);
}

int amp_ctrl_put(struct amp_ctrl *ctrl)
{
	BT_DBG("ctrl %p orig refcnt %d", ctrl,
	       atomic_read(&ctrl->kref.refcount));

	return kref_put(&ctrl->kref, &amp_ctrl_destroy);
}

struct amp_ctrl *amp_ctrl_add(struct amp_mgr *mgr, u8 id)
{
	struct amp_ctrl *ctrl;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return NULL;

	kref_init(&ctrl->kref);
	ctrl->id = id;

	mutex_lock(&mgr->amp_ctrls_lock);
	list_add(&ctrl->list, &mgr->amp_ctrls);
	mutex_unlock(&mgr->amp_ctrls_lock);

	BT_DBG("mgr %p ctrl %p", mgr, ctrl);

	return ctrl;
}

void amp_ctrl_list_flush(struct amp_mgr *mgr)
{
	struct amp_ctrl *ctrl, *n;

	BT_DBG("mgr %p", mgr);

	mutex_lock(&mgr->amp_ctrls_lock);
	list_for_each_entry_safe(ctrl, n, &mgr->amp_ctrls, list) {
		list_del(&ctrl->list);
		amp_ctrl_put(ctrl);
	}
	mutex_unlock(&mgr->amp_ctrls_lock);
}

struct amp_ctrl *amp_ctrl_lookup(struct amp_mgr *mgr, u8 id)
{
	struct amp_ctrl *ctrl;

	BT_DBG("mgr %p id %d", mgr, id);

	mutex_lock(&mgr->amp_ctrls_lock);
	list_for_each_entry(ctrl, &mgr->amp_ctrls, list) {
		if (ctrl->id == id) {
			amp_ctrl_get(ctrl);
			mutex_unlock(&mgr->amp_ctrls_lock);
			return ctrl;
		}
	}
	mutex_unlock(&mgr->amp_ctrls_lock);

	return NULL;
}

/* Physical Link interface */
static u8 __next_handle(struct amp_mgr *mgr)
{
	if (++mgr->handle == 0)
		mgr->handle = 1;

	return mgr->handle;
}

struct hci_conn *phylink_add(struct hci_dev *hdev, struct amp_mgr *mgr,
			     u8 remote_id, bool out)
{
	bdaddr_t *dst = &mgr->l2cap_conn->hcon->dst;
	struct hci_conn *hcon;
	u8 role = out ? HCI_ROLE_MASTER : HCI_ROLE_SLAVE;

	hcon = hci_conn_add(hdev, AMP_LINK, dst, role);
	if (!hcon)
		return NULL;

	BT_DBG("hcon %p dst %pMR", hcon, dst);

	hcon->state = BT_CONNECT;
	hcon->attempt++;
	hcon->handle = __next_handle(mgr);
	hcon->remote_id = remote_id;
	hcon->amp_mgr = amp_mgr_get(mgr);

	return hcon;
}

/* AMP crypto key generation interface */
static int hmac_sha256(u8 *key, u8 ksize, char *plaintext, u8 psize, u8 *output)
{
	struct crypto_shash *tfm;
	int ret;

	if (!ksize)
		return -EINVAL;

	tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(tfm)) {
		BT_DBG("crypto_alloc_ahash failed: err %ld", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	ret = crypto_shash_setkey(tfm, key, ksize);
	if (ret) {
		BT_DBG("crypto_ahash_setkey failed: err %d", ret);
	} else {
		char desc[sizeof(struct shash_desc) +
			crypto_shash_descsize(tfm)] CRYPTO_MINALIGN_ATTR;
		struct shash_desc *shash = (struct shash_desc *)desc;

		shash->tfm = tfm;
		shash->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

		ret = crypto_shash_digest(shash, plaintext, psize,
					  output);
	}

	crypto_free_shash(tfm);
	return ret;
}

int phylink_gen_key(struct hci_conn *conn, u8 *data, u8 *len, u8 *type)
{
	struct hci_dev *hdev = conn->hdev;
	struct link_key *key;
	u8 keybuf[HCI_AMP_LINK_KEY_SIZE];
	u8 gamp_key[HCI_AMP_LINK_KEY_SIZE];
	int err;

	if (!hci_conn_check_link_mode(conn))
		return -EACCES;

	BT_DBG("conn %p key_type %d", conn, conn->key_type);

	/* Legacy key */
	if (conn->key_type < 3) {
		BT_ERR("Legacy key type %d", conn->key_type);
		return -EACCES;
	}

	*type = conn->key_type;
	*len = HCI_AMP_LINK_KEY_SIZE;

	key = hci_find_link_key(hdev, &conn->dst);
	if (!key) {
		BT_DBG("No Link key for conn %p dst %pMR", conn, &conn->dst);
		return -EACCES;
	}

	/* BR/EDR Link Key concatenated together with itself */
	memcpy(&keybuf[0], key->val, HCI_LINK_KEY_SIZE);
	memcpy(&keybuf[HCI_LINK_KEY_SIZE], key->val, HCI_LINK_KEY_SIZE);

	/* Derive Generic AMP Link Key (gamp) */
	err = hmac_sha256(keybuf, HCI_AMP_LINK_KEY_SIZE, "gamp", 4, gamp_key);
	if (err) {
		BT_ERR("Could not derive Generic AMP Key: err %d", err);
		return err;
	}

	if (conn->key_type == HCI_LK_DEBUG_COMBINATION) {
		BT_DBG("Use Generic AMP Key (gamp)");
		memcpy(data, gamp_key, HCI_AMP_LINK_KEY_SIZE);
		return err;
	}

	/* Derive Dedicated AMP Link Key: "802b" is 802.11 PAL keyID */
	return hmac_sha256(gamp_key, HCI_AMP_LINK_KEY_SIZE, "802b", 4, data);
}

void amp_read_loc_assoc_frag(struct hci_dev *hdev, u8 phy_handle)
{
	struct hci_cp_read_local_amp_assoc cp;
	struct amp_assoc *loc_assoc = &hdev->loc_assoc;

	BT_DBG("%s handle %d", hdev->name, phy_handle);

	cp.phy_handle = phy_handle;
	cp.max_len = cpu_to_le16(hdev->amp_assoc_size);
	cp.len_so_far = cpu_to_le16(loc_assoc->offset);

	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_AMP_ASSOC, sizeof(cp), &cp);
}

void amp_read_loc_assoc(struct hci_dev *hdev, struct amp_mgr *mgr)
{
	struct hci_cp_read_local_amp_assoc cp;

	memset(&hdev->loc_assoc, 0, sizeof(struct amp_assoc));
	memset(&cp, 0, sizeof(cp));

	cp.max_len = cpu_to_le16(hdev->amp_assoc_size);

	set_bit(READ_LOC_AMP_ASSOC, &mgr->state);
	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_AMP_ASSOC, sizeof(cp), &cp);
}

void amp_read_loc_assoc_final_data(struct hci_dev *hdev,
				   struct hci_conn *hcon)
{
	struct hci_cp_read_local_amp_assoc cp;
	struct amp_mgr *mgr = hcon->amp_mgr;

	cp.phy_handle = hcon->handle;
	cp.len_so_far = cpu_to_le16(0);
	cp.max_len = cpu_to_le16(hdev->amp_assoc_size);

	set_bit(READ_LOC_AMP_ASSOC_FINAL, &mgr->state);

	/* Read Local AMP Assoc final link information data */
	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_AMP_ASSOC, sizeof(cp), &cp);
}

/* Write AMP Assoc data fragments, returns true with last fragment written*/
static bool amp_write_rem_assoc_frag(struct hci_dev *hdev,
				     struct hci_conn *hcon)
{
	struct hci_cp_write_remote_amp_assoc *cp;
	struct amp_mgr *mgr = hcon->amp_mgr;
	struct amp_ctrl *ctrl;
	u16 frag_len, len;

	ctrl = amp_ctrl_lookup(mgr, hcon->remote_id);
	if (!ctrl)
		return false;

	if (!ctrl->assoc_rem_len) {
		BT_DBG("all fragments are written");
		ctrl->assoc_rem_len = ctrl->assoc_len;
		ctrl->assoc_len_so_far = 0;

		amp_ctrl_put(ctrl);
		return true;
	}

	frag_len = min_t(u16, 248, ctrl->assoc_rem_len);
	len = frag_len + sizeof(*cp);

	cp = kzalloc(len, GFP_KERNEL);
	if (!cp) {
		amp_ctrl_put(ctrl);
		return false;
	}

	BT_DBG("hcon %p ctrl %p frag_len %u assoc_len %u rem_len %u",
	       hcon, ctrl, frag_len, ctrl->assoc_len, ctrl->assoc_rem_len);

	cp->phy_handle = hcon->handle;
	cp->len_so_far = cpu_to_le16(ctrl->assoc_len_so_far);
	cp->rem_len = cpu_to_le16(ctrl->assoc_rem_len);
	memcpy(cp->frag, ctrl->assoc, frag_len);

	ctrl->assoc_len_so_far += frag_len;
	ctrl->assoc_rem_len -= frag_len;

	amp_ctrl_put(ctrl);

	hci_send_cmd(hdev, HCI_OP_WRITE_REMOTE_AMP_ASSOC, len, cp);

	kfree(cp);

	return false;
}

void amp_write_rem_assoc_continue(struct hci_dev *hdev, u8 handle)
{
	struct hci_conn *hcon;

	BT_DBG("%s phy handle 0x%2.2x", hdev->name, handle);

	hcon = hci_conn_hash_lookup_handle(hdev, handle);
	if (!hcon)
		return;

	/* Send A2MP create phylink rsp when all fragments are written */
	if (amp_write_rem_assoc_frag(hdev, hcon))
		a2mp_send_create_phy_link_rsp(hdev, 0);
}

void amp_write_remote_assoc(struct hci_dev *hdev, u8 handle)
{
	struct hci_conn *hcon;

	BT_DBG("%s phy handle 0x%2.2x", hdev->name, handle);

	hcon = hci_conn_hash_lookup_handle(hdev, handle);
	if (!hcon)
		return;

	BT_DBG("%s phy handle 0x%2.2x hcon %p", hdev->name, handle, hcon);

	amp_write_rem_assoc_frag(hdev, hcon);
}

void amp_create_phylink(struct hci_dev *hdev, struct amp_mgr *mgr,
			struct hci_conn *hcon)
{
	struct hci_cp_create_phy_link cp;

	cp.phy_handle = hcon->handle;

	BT_DBG("%s hcon %p phy handle 0x%2.2x", hdev->name, hcon,
	       hcon->handle);

	if (phylink_gen_key(mgr->l2cap_conn->hcon, cp.key, &cp.key_len,
			    &cp.key_type)) {
		BT_DBG("Cannot create link key");
		return;
	}

	hci_send_cmd(hdev, HCI_OP_CREATE_PHY_LINK, sizeof(cp), &cp);
}

void amp_accept_phylink(struct hci_dev *hdev, struct amp_mgr *mgr,
			struct hci_conn *hcon)
{
	struct hci_cp_accept_phy_link cp;

	cp.phy_handle = hcon->handle;

	BT_DBG("%s hcon %p phy handle 0x%2.2x", hdev->name, hcon,
	       hcon->handle);

	if (phylink_gen_key(mgr->l2cap_conn->hcon, cp.key, &cp.key_len,
			    &cp.key_type)) {
		BT_DBG("Cannot create link key");
		return;
	}

	hci_send_cmd(hdev, HCI_OP_ACCEPT_PHY_LINK, sizeof(cp), &cp);
}

void amp_physical_cfm(struct hci_conn *bredr_hcon, struct hci_conn *hs_hcon)
{
	struct hci_dev *bredr_hdev = hci_dev_hold(bredr_hcon->hdev);
	struct amp_mgr *mgr = hs_hcon->amp_mgr;
	struct l2cap_chan *bredr_chan;

	BT_DBG("bredr_hcon %p hs_hcon %p mgr %p", bredr_hcon, hs_hcon, mgr);

	if (!bredr_hdev || !mgr || !mgr->bredr_chan)
		return;

	bredr_chan = mgr->bredr_chan;

	l2cap_chan_lock(bredr_chan);

	set_bit(FLAG_EFS_ENABLE, &bredr_chan->flags);
	bredr_chan->remote_amp_id = hs_hcon->remote_id;
	bredr_chan->local_amp_id = hs_hcon->hdev->id;
	bredr_chan->hs_hcon = hs_hcon;
	bredr_chan->conn->mtu = hs_hcon->hdev->block_mtu;

	__l2cap_physical_cfm(bredr_chan, 0);

	l2cap_chan_unlock(bredr_chan);

	hci_dev_put(bredr_hdev);
}

void amp_create_logical_link(struct l2cap_chan *chan)
{
	struct hci_conn *hs_hcon = chan->hs_hcon;
	struct hci_cp_create_accept_logical_link cp;
	struct hci_dev *hdev;

	BT_DBG("chan %p hs_hcon %p dst %pMR", chan, hs_hcon,
	       &chan->conn->hcon->dst);

	if (!hs_hcon)
		return;

	hdev = hci_dev_hold(chan->hs_hcon->hdev);
	if (!hdev)
		return;

	cp.phy_handle = hs_hcon->handle;

	cp.tx_flow_spec.id = chan->local_id;
	cp.tx_flow_spec.stype = chan->local_stype;
	cp.tx_flow_spec.msdu = cpu_to_le16(chan->local_msdu);
	cp.tx_flow_spec.sdu_itime = cpu_to_le32(chan->local_sdu_itime);
	cp.tx_flow_spec.acc_lat = cpu_to_le32(chan->local_acc_lat);
	cp.tx_flow_spec.flush_to = cpu_to_le32(chan->local_flush_to);

	cp.rx_flow_spec.id = chan->remote_id;
	cp.rx_flow_spec.stype = chan->remote_stype;
	cp.rx_flow_spec.msdu = cpu_to_le16(chan->remote_msdu);
	cp.rx_flow_spec.sdu_itime = cpu_to_le32(chan->remote_sdu_itime);
	cp.rx_flow_spec.acc_lat = cpu_to_le32(chan->remote_acc_lat);
	cp.rx_flow_spec.flush_to = cpu_to_le32(chan->remote_flush_to);

	if (hs_hcon->out)
		hci_send_cmd(hdev, HCI_OP_CREATE_LOGICAL_LINK, sizeof(cp),
			     &cp);
	else
		hci_send_cmd(hdev, HCI_OP_ACCEPT_LOGICAL_LINK, sizeof(cp),
			     &cp);

	hci_dev_put(hdev);
}

void amp_disconnect_logical_link(struct hci_chan *hchan)
{
	struct hci_conn *hcon = hchan->conn;
	struct hci_cp_disconn_logical_link cp;

	if (hcon->state != BT_CONNECTED) {
		BT_DBG("hchan %p not connected", hchan);
		return;
	}

	cp.log_handle = cpu_to_le16(hchan->handle);
	hci_send_cmd(hcon->hdev, HCI_OP_DISCONN_LOGICAL_LINK, sizeof(cp), &cp);
}

void amp_destroy_logical_link(struct hci_chan *hchan, u8 reason)
{
	BT_DBG("hchan %p", hchan);

	hci_chan_del(hchan);
}
