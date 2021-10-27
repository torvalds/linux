/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2021 Intel Corporation
 */

typedef int (*hci_cmd_sync_work_func_t)(struct hci_dev *hdev, void *data);
typedef void (*hci_cmd_sync_work_destroy_t)(struct hci_dev *hdev, void *data,
					    int err);

struct hci_cmd_sync_work_entry {
	struct list_head list;
	hci_cmd_sync_work_func_t func;
	void *data;
	hci_cmd_sync_work_destroy_t destroy;
};

/* Function with sync suffix shall not be called with hdev->lock held as they
 * wait the command to complete and in the meantime an event could be received
 * which could attempt to acquire hdev->lock causing a deadlock.
 */
struct sk_buff *__hci_cmd_sync(struct hci_dev *hdev, u16 opcode, u32 plen,
			       const void *param, u32 timeout);
struct sk_buff *hci_cmd_sync(struct hci_dev *hdev, u16 opcode, u32 plen,
			     const void *param, u32 timeout);
struct sk_buff *__hci_cmd_sync_ev(struct hci_dev *hdev, u16 opcode, u32 plen,
				  const void *param, u8 event, u32 timeout);
struct sk_buff *__hci_cmd_sync_sk(struct hci_dev *hdev, u16 opcode, u32 plen,
				  const void *param, u8 event, u32 timeout,
				  struct sock *sk);
int __hci_cmd_sync_status(struct hci_dev *hdev, u16 opcode, u32 plen,
			  const void *param, u32 timeout);
int __hci_cmd_sync_status_sk(struct hci_dev *hdev, u16 opcode, u32 plen,
			     const void *param, u8 event, u32 timeout,
			     struct sock *sk);

void hci_cmd_sync_init(struct hci_dev *hdev);
void hci_cmd_sync_clear(struct hci_dev *hdev);

int hci_cmd_sync_queue(struct hci_dev *hdev, hci_cmd_sync_work_func_t func,
		       void *data, hci_cmd_sync_work_destroy_t destroy);

int hci_update_eir_sync(struct hci_dev *hdev);
int hci_update_class_sync(struct hci_dev *hdev);

int hci_update_eir_sync(struct hci_dev *hdev);
int hci_update_class_sync(struct hci_dev *hdev);

int hci_update_random_address_sync(struct hci_dev *hdev, bool require_privacy,
				   bool rpa, u8 *own_addr_type);

int hci_update_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance);
int hci_update_adv_data_sync(struct hci_dev *hdev, u8 instance);
int hci_schedule_adv_instance_sync(struct hci_dev *hdev, u8 instance,
				   bool force);

int hci_setup_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance);
int hci_start_ext_adv_sync(struct hci_dev *hdev, u8 instance);
int hci_enable_ext_advertising_sync(struct hci_dev *hdev, u8 instance);
int hci_enable_advertising_sync(struct hci_dev *hdev);
int hci_enable_advertising(struct hci_dev *hdev);

int hci_remove_advertising_sync(struct hci_dev *hdev, struct sock *sk,
				u8 instance, bool force);
int hci_disable_advertising_sync(struct hci_dev *hdev);

int hci_update_passive_scan_sync(struct hci_dev *hdev);
int hci_update_passive_scan(struct hci_dev *hdev);

int hci_write_fast_connectable_sync(struct hci_dev *hdev, bool enable);
int hci_update_scan_sync(struct hci_dev *hdev);

int hci_dev_open_sync(struct hci_dev *hdev);
int hci_dev_close_sync(struct hci_dev *hdev);

int hci_powered_update_sync(struct hci_dev *hdev);
int hci_set_powered_sync(struct hci_dev *hdev, u8 val);

int hci_start_discovery_sync(struct hci_dev *hdev);
int hci_stop_discovery_sync(struct hci_dev *hdev);
