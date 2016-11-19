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

#define hci_req_sync_lock(hdev)   mutex_lock(&hdev->req_lock)
#define hci_req_sync_unlock(hdev) mutex_unlock(&hdev->req_lock)

struct hci_request {
	struct hci_dev		*hdev;
	struct sk_buff_head	cmd_q;

	/* If something goes wrong when building the HCI request, the error
	 * value is stored in this field.
	 */
	int			err;
};

void hci_req_init(struct hci_request *req, struct hci_dev *hdev);
int hci_req_run(struct hci_request *req, hci_req_complete_t complete);
int hci_req_run_skb(struct hci_request *req, hci_req_complete_skb_t complete);
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
void hci_req_sync_cancel(struct hci_dev *hdev, int err);

struct sk_buff *hci_prepare_cmd(struct hci_dev *hdev, u16 opcode, u32 plen,
				const void *param);

int __hci_req_hci_power_on(struct hci_dev *hdev);

void __hci_req_write_fast_connectable(struct hci_request *req, bool enable);
void __hci_req_update_name(struct hci_request *req);
void __hci_req_update_eir(struct hci_request *req);

void hci_req_add_le_scan_disable(struct hci_request *req);
void hci_req_add_le_passive_scan(struct hci_request *req);

void hci_req_reenable_advertising(struct hci_dev *hdev);
void __hci_req_enable_advertising(struct hci_request *req);
void __hci_req_disable_advertising(struct hci_request *req);
void __hci_req_update_adv_data(struct hci_request *req, u8 instance);
int hci_req_update_adv_data(struct hci_dev *hdev, u8 instance);
void __hci_req_update_scan_rsp_data(struct hci_request *req, u8 instance);

int __hci_req_schedule_adv_instance(struct hci_request *req, u8 instance,
				    bool force);
void hci_req_clear_adv_instance(struct hci_dev *hdev, struct sock *sk,
				struct hci_request *req, u8 instance,
				bool force);

void __hci_req_update_class(struct hci_request *req);

/* Returns true if HCI commands were queued */
bool hci_req_stop_discovery(struct hci_request *req);

static inline void hci_req_update_scan(struct hci_dev *hdev)
{
	queue_work(hdev->req_workqueue, &hdev->scan_update);
}

void __hci_req_update_scan(struct hci_request *req);

int hci_update_random_address(struct hci_request *req, bool require_privacy,
			      bool use_rpa, u8 *own_addr_type);

int hci_abort_conn(struct hci_conn *conn, u8 reason);
void __hci_abort_conn(struct hci_request *req, struct hci_conn *conn,
		      u8 reason);

static inline void hci_update_background_scan(struct hci_dev *hdev)
{
	queue_work(hdev->req_workqueue, &hdev->bg_scan_update);
}

void hci_request_setup(struct hci_dev *hdev);
void hci_request_cancel_all(struct hci_dev *hdev);

u8 append_local_name(struct hci_dev *hdev, u8 *ptr, u8 ad_len);

static inline u16 eir_append_data(u8 *eir, u16 eir_len, u8 type,
				  u8 *data, u8 data_len)
{
	eir[eir_len++] = sizeof(type) + data_len;
	eir[eir_len++] = type;
	memcpy(&eir[eir_len], data, data_len);
	eir_len += data_len;

	return eir_len;
}

static inline u16 eir_append_le16(u8 *eir, u16 eir_len, u8 type, u16 data)
{
	eir[eir_len++] = sizeof(type) + sizeof(data);
	eir[eir_len++] = type;
	put_unaligned_le16(data, &eir[eir_len]);
	eir_len += sizeof(data);

	return eir_len;
}
