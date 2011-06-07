/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2010  Nokia Corporation

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

#define MGMT_INDEX_NONE			0xFFFF

struct mgmt_hdr {
	__le16 opcode;
	__le16 index;
	__le16 len;
} __packed;

#define MGMT_OP_READ_VERSION		0x0001
struct mgmt_rp_read_version {
	__u8 version;
	__le16 revision;
} __packed;

#define MGMT_OP_READ_INDEX_LIST		0x0003
struct mgmt_rp_read_index_list {
	__le16 num_controllers;
	__le16 index[0];
} __packed;

/* Reserve one extra byte for names in management messages so that they
 * are always guaranteed to be nul-terminated */
#define MGMT_MAX_NAME_LENGTH		(HCI_MAX_NAME_LENGTH + 1)

#define MGMT_OP_READ_INFO		0x0004
struct mgmt_rp_read_info {
	__u8 type;
	__u8 powered;
	__u8 connectable;
	__u8 discoverable;
	__u8 pairable;
	__u8 sec_mode;
	bdaddr_t bdaddr;
	__u8 dev_class[3];
	__u8 features[8];
	__u16 manufacturer;
	__u8 hci_ver;
	__u16 hci_rev;
	__u8 name[MGMT_MAX_NAME_LENGTH];
} __packed;

struct mgmt_mode {
	__u8 val;
} __packed;

#define MGMT_OP_SET_POWERED		0x0005

#define MGMT_OP_SET_DISCOVERABLE	0x0006

#define MGMT_OP_SET_CONNECTABLE		0x0007

#define MGMT_OP_SET_PAIRABLE		0x0008

#define MGMT_OP_ADD_UUID		0x0009
struct mgmt_cp_add_uuid {
	__u8 uuid[16];
	__u8 svc_hint;
} __packed;

#define MGMT_OP_REMOVE_UUID		0x000A
struct mgmt_cp_remove_uuid {
	__u8 uuid[16];
} __packed;

#define MGMT_OP_SET_DEV_CLASS		0x000B
struct mgmt_cp_set_dev_class {
	__u8 major;
	__u8 minor;
} __packed;

#define MGMT_OP_SET_SERVICE_CACHE	0x000C
struct mgmt_cp_set_service_cache {
	__u8 enable;
} __packed;

struct mgmt_key_info {
	bdaddr_t bdaddr;
	u8 type;
	u8 val[16];
	u8 pin_len;
} __packed;

#define MGMT_OP_LOAD_KEYS		0x000D
struct mgmt_cp_load_keys {
	__u8 debug_keys;
	__le16 key_count;
	struct mgmt_key_info keys[0];
} __packed;

#define MGMT_OP_REMOVE_KEY		0x000E
struct mgmt_cp_remove_key {
	bdaddr_t bdaddr;
	__u8 disconnect;
} __packed;

#define MGMT_OP_DISCONNECT		0x000F
struct mgmt_cp_disconnect {
	bdaddr_t bdaddr;
} __packed;
struct mgmt_rp_disconnect {
	bdaddr_t bdaddr;
} __packed;

#define MGMT_OP_GET_CONNECTIONS		0x0010
struct mgmt_rp_get_connections {
	__le16 conn_count;
	bdaddr_t conn[0];
} __packed;

#define MGMT_OP_PIN_CODE_REPLY		0x0011
struct mgmt_cp_pin_code_reply {
	bdaddr_t bdaddr;
	__u8 pin_len;
	__u8 pin_code[16];
} __packed;
struct mgmt_rp_pin_code_reply {
	bdaddr_t bdaddr;
	uint8_t status;
} __packed;

#define MGMT_OP_PIN_CODE_NEG_REPLY	0x0012
struct mgmt_cp_pin_code_neg_reply {
	bdaddr_t bdaddr;
} __packed;

#define MGMT_OP_SET_IO_CAPABILITY	0x0013
struct mgmt_cp_set_io_capability {
	__u8 io_capability;
} __packed;

#define MGMT_OP_PAIR_DEVICE		0x0014
struct mgmt_cp_pair_device {
	bdaddr_t bdaddr;
	__u8 io_cap;
} __packed;
struct mgmt_rp_pair_device {
	bdaddr_t bdaddr;
	__u8 status;
} __packed;

#define MGMT_OP_USER_CONFIRM_REPLY	0x0015
struct mgmt_cp_user_confirm_reply {
	bdaddr_t bdaddr;
} __packed;
struct mgmt_rp_user_confirm_reply {
	bdaddr_t bdaddr;
	__u8 status;
} __packed;

#define MGMT_OP_USER_CONFIRM_NEG_REPLY	0x0016

#define MGMT_OP_SET_LOCAL_NAME		0x0017
struct mgmt_cp_set_local_name {
	__u8 name[MGMT_MAX_NAME_LENGTH];
} __packed;

#define MGMT_OP_READ_LOCAL_OOB_DATA	0x0018
struct mgmt_rp_read_local_oob_data {
	__u8 hash[16];
	__u8 randomizer[16];
} __packed;

#define MGMT_OP_ADD_REMOTE_OOB_DATA	0x0019
struct mgmt_cp_add_remote_oob_data {
	bdaddr_t bdaddr;
	__u8 hash[16];
	__u8 randomizer[16];
} __packed;

#define MGMT_OP_REMOVE_REMOTE_OOB_DATA	0x001A
struct mgmt_cp_remove_remote_oob_data {
	bdaddr_t bdaddr;
} __packed;

#define MGMT_OP_START_DISCOVERY		0x001B

#define MGMT_OP_STOP_DISCOVERY		0x001C

#define MGMT_EV_CMD_COMPLETE		0x0001
struct mgmt_ev_cmd_complete {
	__le16 opcode;
	__u8 data[0];
} __packed;

#define MGMT_EV_CMD_STATUS		0x0002
struct mgmt_ev_cmd_status {
	__u8 status;
	__le16 opcode;
} __packed;

#define MGMT_EV_CONTROLLER_ERROR	0x0003
struct mgmt_ev_controller_error {
	__u8 error_code;
} __packed;

#define MGMT_EV_INDEX_ADDED		0x0004

#define MGMT_EV_INDEX_REMOVED		0x0005

#define MGMT_EV_POWERED			0x0006

#define MGMT_EV_DISCOVERABLE		0x0007

#define MGMT_EV_CONNECTABLE		0x0008

#define MGMT_EV_PAIRABLE		0x0009

#define MGMT_EV_NEW_KEY			0x000A
struct mgmt_ev_new_key {
	__u8 store_hint;
	struct mgmt_key_info key;
} __packed;

#define MGMT_EV_CONNECTED		0x000B
struct mgmt_ev_connected {
	bdaddr_t bdaddr;
} __packed;

#define MGMT_EV_DISCONNECTED		0x000C
struct mgmt_ev_disconnected {
	bdaddr_t bdaddr;
} __packed;

#define MGMT_EV_CONNECT_FAILED		0x000D
struct mgmt_ev_connect_failed {
	bdaddr_t bdaddr;
	__u8 status;
} __packed;

#define MGMT_EV_PIN_CODE_REQUEST	0x000E
struct mgmt_ev_pin_code_request {
	bdaddr_t bdaddr;
	__u8 secure;
} __packed;

#define MGMT_EV_USER_CONFIRM_REQUEST	0x000F
struct mgmt_ev_user_confirm_request {
	bdaddr_t bdaddr;
	__u8 confirm_hint;
	__le32 value;
} __packed;

#define MGMT_EV_AUTH_FAILED		0x0010
struct mgmt_ev_auth_failed {
	bdaddr_t bdaddr;
	__u8 status;
} __packed;

#define MGMT_EV_LOCAL_NAME_CHANGED	0x0011
struct mgmt_ev_local_name_changed {
	__u8 name[MGMT_MAX_NAME_LENGTH];
} __packed;

#define MGMT_EV_DEVICE_FOUND		0x0012
struct mgmt_ev_device_found {
	bdaddr_t bdaddr;
	__u8 dev_class[3];
	__s8 rssi;
	__u8 eir[HCI_MAX_EIR_LENGTH];
} __packed;

#define MGMT_EV_REMOTE_NAME		0x0013
struct mgmt_ev_remote_name {
	bdaddr_t bdaddr;
	__u8 name[MGMT_MAX_NAME_LENGTH];
} __packed;

#define MGMT_EV_DISCOVERING		0x0014
