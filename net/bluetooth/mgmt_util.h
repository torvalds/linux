/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2015  Intel Coropration

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

struct mgmt_pending_cmd {
	struct list_head list;
	u16 opcode;
	int index;
	void *param;
	size_t param_len;
	struct sock *sk;
	void *user_data;
	int (*cmd_complete)(struct mgmt_pending_cmd *cmd, u8 status);
};

int mgmt_send_event(u16 event, struct hci_dev *hdev, unsigned short channel,
		    void *data, u16 data_len, int flag, struct sock *skip_sk);
int mgmt_cmd_status(struct sock *sk, u16 index, u16 cmd, u8 status);
int mgmt_cmd_complete(struct sock *sk, u16 index, u16 cmd, u8 status,
		      void *rp, size_t rp_len);

struct mgmt_pending_cmd *mgmt_pending_find(unsigned short channel, u16 opcode,
					   struct hci_dev *hdev);
struct mgmt_pending_cmd *mgmt_pending_find_data(unsigned short channel,
						u16 opcode,
						struct hci_dev *hdev,
						const void *data);
void mgmt_pending_foreach(u16 opcode, struct hci_dev *hdev,
			  void (*cb)(struct mgmt_pending_cmd *cmd, void *data),
			  void *data);
struct mgmt_pending_cmd *mgmt_pending_add(struct sock *sk, u16 opcode,
					  struct hci_dev *hdev,
					  void *data, u16 len);
void mgmt_pending_free(struct mgmt_pending_cmd *cmd);
void mgmt_pending_remove(struct mgmt_pending_cmd *cmd);
