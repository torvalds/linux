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

#include <asm/unaligned.h>

#define HCI_REQ_DONE	  0
#define HCI_REQ_PEND	  1
#define HCI_REQ_CANCELED  2

#define hci_req_sync_lock(hdev)   mutex_lock(&hdev->req_lock)
#define hci_req_sync_unlock(hdev) mutex_unlock(&hdev->req_lock)

#define HCI_REQ_DONE	  0
#define HCI_REQ_PEND	  1
#define HCI_REQ_CANCELED  2

struct hci_request {
	struct hci_dev		*hdev;
	struct sk_buff_head	cmd_q;

	/* If something goes wrong when building the HCI request, the error
	 * value is stored in this field.
	 */
	int			err;
};

void hci_req_init(struct hci_request *req, struct hci_dev *hdev);
void hci_req_purge(struct hci_request *req);
bool hci_req_status_pend(struct hci_dev *hdev);
int hci_req_run(struct hci_request *req, hci_req_complete_t complete);
int hci_req_run_skb(struct hci_request *req, hci_req_complete_skb_t complete);
void hci_req_sync_complete(struct hci_dev *hdev, u8 result, u16 opcode,
			   struct sk_buff *skb);
void hci_req_add(struct hci_request *req, u16 opcode, u32 plen,
		 const void *param);
void hci_req_add_ev(struct hci_request *req, u16 opcode, u32 plen,
		    const void *param, u8 event);
void hci_req_cmd_complete(struct hci_dev *hdev, u16 opcode, u8 status,
			  hci_req_complete_t *req_complete,
			  hci_req_complete_skb_t *req_complete_skb);

int hci_req_sync(struct hci_dev *hdev, int (*req)(struct hci_request *req,
						  unsigned long opt),
		 unsigned long opt, u32 timeout, u8 *hci_status);
int __hci_req_sync(struct hci_dev *hdev, int (*func)(struct hci_request *req,
						     unsigned long opt),
		   unsigned long opt, u32 timeout, u8 *hci_status);

struct sk_buff *hci_prepare_cmd(struct hci_dev *hdev, u16 opcode, u32 plen,
				const void *param);

void __hci_req_write_fast_connectable(struct hci_request *req, bool enable);
void __hci_req_update_name(struct hci_request *req);
void __hci_req_update_eir(struct hci_request *req);

void hci_req_add_le_scan_disable(struct hci_request *req, bool rpa_le_conn);
void hci_req_add_le_passive_scan(struct hci_request *req);

void hci_req_prepare_suspend(struct hci_dev *hdev, enum suspended_state next);

void hci_req_disable_address_resolution(struct hci_dev *hdev);
void hci_req_reenable_advertising(struct hci_dev *hdev);
void __hci_req_enable_advertising(struct hci_request *req);
void __hci_req_disable_advertising(struct hci_request *req);
void __hci_req_update_adv_data(struct hci_request *req, u8 instance);
int hci_req_update_adv_data(struct hci_dev *hdev, u8 instance);
int hci_req_start_per_adv(struct hci_dev *hdev, u8 instance, u32 flags,
			  u16 min_interval, u16 max_interval,
			  u16 sync_interval);
void __hci_req_update_scan_rsp_data(struct hci_request *req, u8 instance);

int __hci_req_schedule_adv_instance(struct hci_request *req, u8 instance,
				    bool force);
void hci_req_clear_adv_instance(struct hci_dev *hdev, struct sock *sk,
				struct hci_request *req, u8 instance,
				bool force);

int __hci_req_setup_ext_adv_instance(struct hci_request *req, u8 instance);
int __hci_req_setup_per_adv_instance(struct hci_request *req, u8 instance,
				     u16 min_interval, u16 max_interval);
int __hci_req_start_ext_adv(struct hci_request *req, u8 instance);
int __hci_req_start_per_adv(struct hci_request *req, u8 instance, u32 flags,
			    u16 min_interval, u16 max_interval,
			    u16 sync_interval);
int __hci_req_enable_ext_advertising(struct hci_request *req, u8 instance);
int __hci_req_enable_per_advertising(struct hci_request *req, u8 instance);
int __hci_req_disable_ext_adv_instance(struct hci_request *req, u8 instance);
int __hci_req_remove_ext_adv_instance(struct hci_request *req, u8 instance);
void __hci_req_clear_ext_adv_sets(struct hci_request *req);
int hci_get_random_address(struct hci_dev *hdev, bool require_privacy,
			   bool use_rpa, struct adv_info *adv_instance,
			   u8 *own_addr_type, bdaddr_t *rand_addr);

void __hci_req_update_class(struct hci_request *req);

/* Returns true if HCI commands were queued */
bool hci_req_stop_discovery(struct hci_request *req);

int hci_req_configure_datapath(struct hci_dev *hdev, struct bt_codec *codec);

void __hci_req_update_scan(struct hci_request *req);

int hci_update_random_address(struct hci_request *req, bool require_privacy,
			      bool use_rpa, u8 *own_addr_type);

int hci_abort_conn(struct hci_conn *conn, u8 reason);
void __hci_abort_conn(struct hci_request *req, struct hci_conn *conn,
		      u8 reason);

void hci_request_setup(struct hci_dev *hdev);
void hci_request_cancel_all(struct hci_dev *hdev);
