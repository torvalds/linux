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

#ifndef __AMP_H
#define __AMP_H

struct amp_ctrl {
	struct list_head	list;
	struct kref		kref;
	__u8			id;
	__u16			assoc_len_so_far;
	__u16			assoc_rem_len;
	__u16			assoc_len;
	__u8			*assoc;
};

int amp_ctrl_put(struct amp_ctrl *ctrl);
void amp_ctrl_get(struct amp_ctrl *ctrl);
struct amp_ctrl *amp_ctrl_add(struct amp_mgr *mgr, u8 id);
struct amp_ctrl *amp_ctrl_lookup(struct amp_mgr *mgr, u8 id);
void amp_ctrl_list_flush(struct amp_mgr *mgr);

struct hci_conn *phylink_add(struct hci_dev *hdev, struct amp_mgr *mgr,
			     u8 remote_id, bool out);

int phylink_gen_key(struct hci_conn *hcon, u8 *data, u8 *len, u8 *type);

void amp_read_loc_info(struct hci_dev *hdev, struct amp_mgr *mgr);
void amp_read_loc_assoc_frag(struct hci_dev *hdev, u8 phy_handle);
void amp_read_loc_assoc(struct hci_dev *hdev, struct amp_mgr *mgr);
void amp_read_loc_assoc_final_data(struct hci_dev *hdev,
				   struct hci_conn *hcon);
void amp_create_phylink(struct hci_dev *hdev, struct amp_mgr *mgr,
			struct hci_conn *hcon);
void amp_accept_phylink(struct hci_dev *hdev, struct amp_mgr *mgr,
			struct hci_conn *hcon);
void amp_write_remote_assoc(struct hci_dev *hdev, u8 handle);
void amp_write_rem_assoc_continue(struct hci_dev *hdev, u8 handle);
void amp_physical_cfm(struct hci_conn *bredr_hcon, struct hci_conn *hs_hcon);
void amp_create_logical_link(struct l2cap_chan *chan);
void amp_disconnect_logical_link(struct hci_chan *hchan);
void amp_destroy_logical_link(struct hci_chan *hchan, u8 reason);

#endif /* __AMP_H */
