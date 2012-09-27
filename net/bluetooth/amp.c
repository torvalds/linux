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
