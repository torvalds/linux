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
#include <net/bluetooth/a2mp.h>
#include <net/bluetooth/amp.h>
#include <crypto/hash.h>

/* Remote AMP Controllers interface */
static void amp_ctrl_get(struct amp_ctrl *ctrl)
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

struct amp_ctrl *amp_ctrl_add(struct amp_mgr *mgr)
{
	struct amp_ctrl *ctrl;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return NULL;

	mutex_lock(&mgr->amp_ctrls_lock);
	list_add(&ctrl->list, &mgr->amp_ctrls);
	mutex_unlock(&mgr->amp_ctrls_lock);

	kref_init(&ctrl->kref);

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
			     u8 remote_id)
{
	bdaddr_t *dst = mgr->l2cap_conn->dst;
	struct hci_conn *hcon;

	hcon = hci_conn_add(hdev, AMP_LINK, dst);
	if (!hcon)
		return NULL;

	hcon->state = BT_CONNECT;
	hcon->out = true;
	hcon->attempt++;
	hcon->handle = __next_handle(mgr);
	hcon->remote_id = remote_id;
	hcon->amp_mgr = mgr;

	return hcon;
}

/* AMP crypto key generation interface */
static int hmac_sha256(u8 *key, u8 ksize, char *plaintext, u8 psize, u8 *output)
{
	int ret = 0;
	struct crypto_shash *tfm;

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
		struct {
			struct shash_desc shash;
			char ctx[crypto_shash_descsize(tfm)];
		} desc;

		desc.shash.tfm = tfm;
		desc.shash.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

		ret = crypto_shash_digest(&desc.shash, plaintext, psize,
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

	mgr->state = READ_LOC_AMP_ASSOC;
	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_AMP_ASSOC, sizeof(cp), &cp);
}
