/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

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

#ifndef __HCI_H
#define __HCI_H

#define HCI_MAX_ACL_SIZE	1024
#define HCI_MAX_SCO_SIZE	255
#define HCI_MAX_EVENT_SIZE	260
#define HCI_MAX_FRAME_SIZE	(HCI_MAX_ACL_SIZE + 4)

#define HCI_LINK_KEY_SIZE	16
#define HCI_AMP_LINK_KEY_SIZE	(2 * HCI_LINK_KEY_SIZE)

#define HCI_MAX_AMP_ASSOC_SIZE	672

/* HCI dev events */
#define HCI_DEV_REG			1
#define HCI_DEV_UNREG			2
#define HCI_DEV_UP			3
#define HCI_DEV_DOWN			4
#define HCI_DEV_SUSPEND			5
#define HCI_DEV_RESUME			6

/* HCI notify events */
#define HCI_NOTIFY_CONN_ADD		1
#define HCI_NOTIFY_CONN_DEL		2
#define HCI_NOTIFY_VOICE_SETTING	3

/* HCI bus types */
#define HCI_VIRTUAL	0
#define HCI_USB		1
#define HCI_PCCARD	2
#define HCI_UART	3
#define HCI_RS232	4
#define HCI_PCI		5
#define HCI_SDIO	6

/* HCI controller types */
#define HCI_BREDR	0x00
#define HCI_AMP		0x01

/* First BR/EDR Controller shall have ID = 0 */
#define HCI_BREDR_ID	0

/* AMP controller status */
#define AMP_CTRL_POWERED_DOWN			0x00
#define AMP_CTRL_BLUETOOTH_ONLY			0x01
#define AMP_CTRL_NO_CAPACITY			0x02
#define AMP_CTRL_LOW_CAPACITY			0x03
#define AMP_CTRL_MEDIUM_CAPACITY		0x04
#define AMP_CTRL_HIGH_CAPACITY			0x05
#define AMP_CTRL_FULL_CAPACITY			0x06

/* HCI device quirks */
enum {
	HCI_QUIRK_RESET_ON_CLOSE,
	HCI_QUIRK_RAW_DEVICE,
	HCI_QUIRK_FIXUP_BUFFER_SIZE
};

/* HCI device flags */
enum {
	HCI_UP,
	HCI_INIT,
	HCI_RUNNING,

	HCI_PSCAN,
	HCI_ISCAN,
	HCI_AUTH,
	HCI_ENCRYPT,
	HCI_INQUIRY,

	HCI_RAW,

	HCI_RESET,
};

/*
 * BR/EDR and/or LE controller flags: the flags defined here should represent
 * states from the controller.
 */
enum {
	HCI_SETUP,
	HCI_AUTO_OFF,
	HCI_MGMT,
	HCI_PAIRABLE,
	HCI_SERVICE_CACHE,
	HCI_LINK_KEYS,
	HCI_DEBUG_KEYS,
	HCI_UNREGISTER,

	HCI_LE_SCAN,
	HCI_SSP_ENABLED,
	HCI_HS_ENABLED,
	HCI_LE_ENABLED,
	HCI_LE_PERIPHERAL,
	HCI_CONNECTABLE,
	HCI_DISCOVERABLE,
	HCI_LINK_SECURITY,
	HCI_PENDING_CLASS,
	HCI_PERIODIC_INQ,
};

/* HCI ioctl defines */
#define HCIDEVUP	_IOW('H', 201, int)
#define HCIDEVDOWN	_IOW('H', 202, int)
#define HCIDEVRESET	_IOW('H', 203, int)
#define HCIDEVRESTAT	_IOW('H', 204, int)

#define HCIGETDEVLIST	_IOR('H', 210, int)
#define HCIGETDEVINFO	_IOR('H', 211, int)
#define HCIGETCONNLIST	_IOR('H', 212, int)
#define HCIGETCONNINFO	_IOR('H', 213, int)
#define HCIGETAUTHINFO	_IOR('H', 215, int)

#define HCISETRAW	_IOW('H', 220, int)
#define HCISETSCAN	_IOW('H', 221, int)
#define HCISETAUTH	_IOW('H', 222, int)
#define HCISETENCRYPT	_IOW('H', 223, int)
#define HCISETPTYPE	_IOW('H', 224, int)
#define HCISETLINKPOL	_IOW('H', 225, int)
#define HCISETLINKMODE	_IOW('H', 226, int)
#define HCISETACLMTU	_IOW('H', 227, int)
#define HCISETSCOMTU	_IOW('H', 228, int)

#define HCIBLOCKADDR	_IOW('H', 230, int)
#define HCIUNBLOCKADDR	_IOW('H', 231, int)

#define HCIINQUIRY	_IOR('H', 240, int)

/* HCI timeouts */
#define HCI_DISCONN_TIMEOUT	msecs_to_jiffies(2000)	/* 2 seconds */
#define HCI_PAIRING_TIMEOUT	msecs_to_jiffies(60000)	/* 60 seconds */
#define HCI_INIT_TIMEOUT	msecs_to_jiffies(10000)	/* 10 seconds */
#define HCI_CMD_TIMEOUT		msecs_to_jiffies(1000)	/* 1 second */
#define HCI_ACL_TX_TIMEOUT	msecs_to_jiffies(45000)	/* 45 seconds */
#define HCI_AUTO_OFF_TIMEOUT	msecs_to_jiffies(2000)	/* 2 seconds */

/* HCI data types */
#define HCI_COMMAND_PKT		0x01
#define HCI_ACLDATA_PKT		0x02
#define HCI_SCODATA_PKT		0x03
#define HCI_EVENT_PKT		0x04
#define HCI_VENDOR_PKT		0xff

/* HCI packet types */
#define HCI_DM1		0x0008
#define HCI_DM3		0x0400
#define HCI_DM5		0x4000
#define HCI_DH1		0x0010
#define HCI_DH3		0x0800
#define HCI_DH5		0x8000

#define HCI_HV1		0x0020
#define HCI_HV2		0x0040
#define HCI_HV3		0x0080

#define SCO_PTYPE_MASK	(HCI_HV1 | HCI_HV2 | HCI_HV3)
#define ACL_PTYPE_MASK	(~SCO_PTYPE_MASK)

/* eSCO packet types */
#define ESCO_HV1	0x0001
#define ESCO_HV2	0x0002
#define ESCO_HV3	0x0004
#define ESCO_EV3	0x0008
#define ESCO_EV4	0x0010
#define ESCO_EV5	0x0020
#define ESCO_2EV3	0x0040
#define ESCO_3EV3	0x0080
#define ESCO_2EV5	0x0100
#define ESCO_3EV5	0x0200

#define SCO_ESCO_MASK  (ESCO_HV1 | ESCO_HV2 | ESCO_HV3)
#define EDR_ESCO_MASK  (ESCO_2EV3 | ESCO_3EV3 | ESCO_2EV5 | ESCO_3EV5)

/* ACL flags */
#define ACL_START_NO_FLUSH	0x00
#define ACL_CONT		0x01
#define ACL_START		0x02
#define ACL_COMPLETE		0x03
#define ACL_ACTIVE_BCAST	0x04
#define ACL_PICO_BCAST		0x08

/* Baseband links */
#define SCO_LINK	0x00
#define ACL_LINK	0x01
#define ESCO_LINK	0x02
/* Low Energy links do not have defined link type. Use invented one */
#define LE_LINK		0x80
#define AMP_LINK	0x81

/* LMP features */
#define LMP_3SLOT	0x01
#define LMP_5SLOT	0x02
#define LMP_ENCRYPT	0x04
#define LMP_SOFFSET	0x08
#define LMP_TACCURACY	0x10
#define LMP_RSWITCH	0x20
#define LMP_HOLD	0x40
#define LMP_SNIFF	0x80

#define LMP_PARK	0x01
#define LMP_RSSI	0x02
#define LMP_QUALITY	0x04
#define LMP_SCO		0x08
#define LMP_HV2		0x10
#define LMP_HV3		0x20
#define LMP_ULAW	0x40
#define LMP_ALAW	0x80

#define LMP_CVSD	0x01
#define LMP_PSCHEME	0x02
#define LMP_PCONTROL	0x04

#define LMP_RSSI_INQ	0x40
#define LMP_ESCO	0x80

#define LMP_EV4		0x01
#define LMP_EV5		0x02
#define LMP_NO_BREDR	0x20
#define LMP_LE		0x40

#define LMP_SNIFF_SUBR	0x02
#define LMP_PAUSE_ENC	0x04
#define LMP_EDR_ESCO_2M	0x20
#define LMP_EDR_ESCO_3M	0x40
#define LMP_EDR_3S_ESCO	0x80

#define LMP_EXT_INQ	0x01
#define LMP_SIMUL_LE_BR	0x02
#define LMP_SIMPLE_PAIR	0x08
#define LMP_NO_FLUSH	0x40

#define LMP_LSTO	0x01
#define LMP_INQ_TX_PWR	0x02
#define LMP_EXTFEATURES	0x80

/* Extended LMP features */
#define LMP_HOST_SSP		0x01
#define LMP_HOST_LE		0x02
#define LMP_HOST_LE_BREDR	0x04

/* Connection modes */
#define HCI_CM_ACTIVE	0x0000
#define HCI_CM_HOLD	0x0001
#define HCI_CM_SNIFF	0x0002
#define HCI_CM_PARK	0x0003

/* Link policies */
#define HCI_LP_RSWITCH	0x0001
#define HCI_LP_HOLD	0x0002
#define HCI_LP_SNIFF	0x0004
#define HCI_LP_PARK	0x0008

/* Link modes */
#define HCI_LM_ACCEPT	0x8000
#define HCI_LM_MASTER	0x0001
#define HCI_LM_AUTH	0x0002
#define HCI_LM_ENCRYPT	0x0004
#define HCI_LM_TRUSTED	0x0008
#define HCI_LM_RELIABLE	0x0010
#define HCI_LM_SECURE	0x0020

/* Authentication types */
#define HCI_AT_NO_BONDING		0x00
#define HCI_AT_NO_BONDING_MITM		0x01
#define HCI_AT_DEDICATED_BONDING	0x02
#define HCI_AT_DEDICATED_BONDING_MITM	0x03
#define HCI_AT_GENERAL_BONDING		0x04
#define HCI_AT_GENERAL_BONDING_MITM	0x05

/* Link Key types */
#define HCI_LK_COMBINATION		0x00
#define HCI_LK_LOCAL_UNIT		0x01
#define HCI_LK_REMOTE_UNIT		0x02
#define HCI_LK_DEBUG_COMBINATION	0x03
#define HCI_LK_UNAUTH_COMBINATION	0x04
#define HCI_LK_AUTH_COMBINATION		0x05
#define HCI_LK_CHANGED_COMBINATION	0x06
/* The spec doesn't define types for SMP keys, the _MASTER suffix is implied */
#define HCI_SMP_STK			0x80
#define HCI_SMP_STK_SLAVE		0x81
#define HCI_SMP_LTK			0x82
#define HCI_SMP_LTK_SLAVE		0x83

/* ---- HCI Error Codes ---- */
#define HCI_ERROR_AUTH_FAILURE		0x05
#define HCI_ERROR_CONNECTION_TIMEOUT	0x08
#define HCI_ERROR_REJ_BAD_ADDR		0x0f
#define HCI_ERROR_REMOTE_USER_TERM	0x13
#define HCI_ERROR_REMOTE_LOW_RESOURCES	0x14
#define HCI_ERROR_REMOTE_POWER_OFF	0x15
#define HCI_ERROR_LOCAL_HOST_TERM	0x16
#define HCI_ERROR_PAIRING_NOT_ALLOWED	0x18

/* Flow control modes */
#define HCI_FLOW_CTL_MODE_PACKET_BASED	0x00
#define HCI_FLOW_CTL_MODE_BLOCK_BASED	0x01

/* The core spec defines 127 as the "not available" value */
#define HCI_TX_POWER_INVALID	127

/* Extended Inquiry Response field types */
#define EIR_FLAGS		0x01 /* flags */
#define EIR_UUID16_SOME		0x02 /* 16-bit UUID, more available */
#define EIR_UUID16_ALL		0x03 /* 16-bit UUID, all listed */
#define EIR_UUID32_SOME		0x04 /* 32-bit UUID, more available */
#define EIR_UUID32_ALL		0x05 /* 32-bit UUID, all listed */
#define EIR_UUID128_SOME	0x06 /* 128-bit UUID, more available */
#define EIR_UUID128_ALL		0x07 /* 128-bit UUID, all listed */
#define EIR_NAME_SHORT		0x08 /* shortened local name */
#define EIR_NAME_COMPLETE	0x09 /* complete local name */
#define EIR_TX_POWER		0x0A /* transmit power level */
#define EIR_CLASS_OF_DEV	0x0D /* Class of Device */
#define EIR_SSP_HASH_C		0x0E /* Simple Pairing Hash C */
#define EIR_SSP_RAND_R		0x0F /* Simple Pairing Randomizer R */
#define EIR_DEVICE_ID		0x10 /* device ID */

/* Low Energy Advertising Flags */
#define LE_AD_LIMITED		0x01 /* Limited Discoverable */
#define LE_AD_GENERAL		0x02 /* General Discoverable */
#define LE_AD_NO_BREDR		0x04 /* BR/EDR not supported */
#define LE_AD_SIM_LE_BREDR_CTRL	0x08 /* Simultaneous LE & BR/EDR Controller */
#define LE_AD_SIM_LE_BREDR_HOST	0x10 /* Simultaneous LE & BR/EDR Host */

/* -----  HCI Commands ---- */
#define HCI_OP_NOP			0x0000

#define HCI_OP_INQUIRY			0x0401
struct hci_cp_inquiry {
	__u8     lap[3];
	__u8     length;
	__u8     num_rsp;
} __packed;

#define HCI_OP_INQUIRY_CANCEL		0x0402

#define HCI_OP_PERIODIC_INQ		0x0403

#define HCI_OP_EXIT_PERIODIC_INQ	0x0404

#define HCI_OP_CREATE_CONN		0x0405
struct hci_cp_create_conn {
	bdaddr_t bdaddr;
	__le16   pkt_type;
	__u8     pscan_rep_mode;
	__u8     pscan_mode;
	__le16   clock_offset;
	__u8     role_switch;
} __packed;

#define HCI_OP_DISCONNECT		0x0406
struct hci_cp_disconnect {
	__le16   handle;
	__u8     reason;
} __packed;

#define HCI_OP_ADD_SCO			0x0407
struct hci_cp_add_sco {
	__le16   handle;
	__le16   pkt_type;
} __packed;

#define HCI_OP_CREATE_CONN_CANCEL	0x0408
struct hci_cp_create_conn_cancel {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_ACCEPT_CONN_REQ		0x0409
struct hci_cp_accept_conn_req {
	bdaddr_t bdaddr;
	__u8     role;
} __packed;

#define HCI_OP_REJECT_CONN_REQ		0x040a
struct hci_cp_reject_conn_req {
	bdaddr_t bdaddr;
	__u8     reason;
} __packed;

#define HCI_OP_LINK_KEY_REPLY		0x040b
struct hci_cp_link_key_reply {
	bdaddr_t bdaddr;
	__u8     link_key[HCI_LINK_KEY_SIZE];
} __packed;

#define HCI_OP_LINK_KEY_NEG_REPLY	0x040c
struct hci_cp_link_key_neg_reply {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_PIN_CODE_REPLY		0x040d
struct hci_cp_pin_code_reply {
	bdaddr_t bdaddr;
	__u8     pin_len;
	__u8     pin_code[16];
} __packed;
struct hci_rp_pin_code_reply {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_PIN_CODE_NEG_REPLY	0x040e
struct hci_cp_pin_code_neg_reply {
	bdaddr_t bdaddr;
} __packed;
struct hci_rp_pin_code_neg_reply {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_CHANGE_CONN_PTYPE	0x040f
struct hci_cp_change_conn_ptype {
	__le16   handle;
	__le16   pkt_type;
} __packed;

#define HCI_OP_AUTH_REQUESTED		0x0411
struct hci_cp_auth_requested {
	__le16   handle;
} __packed;

#define HCI_OP_SET_CONN_ENCRYPT		0x0413
struct hci_cp_set_conn_encrypt {
	__le16   handle;
	__u8     encrypt;
} __packed;

#define HCI_OP_CHANGE_CONN_LINK_KEY	0x0415
struct hci_cp_change_conn_link_key {
	__le16   handle;
} __packed;

#define HCI_OP_REMOTE_NAME_REQ		0x0419
struct hci_cp_remote_name_req {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_mode;
	__le16   clock_offset;
} __packed;

#define HCI_OP_REMOTE_NAME_REQ_CANCEL	0x041a
struct hci_cp_remote_name_req_cancel {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_READ_REMOTE_FEATURES	0x041b
struct hci_cp_read_remote_features {
	__le16   handle;
} __packed;

#define HCI_OP_READ_REMOTE_EXT_FEATURES	0x041c
struct hci_cp_read_remote_ext_features {
	__le16   handle;
	__u8     page;
} __packed;

#define HCI_OP_READ_REMOTE_VERSION	0x041d
struct hci_cp_read_remote_version {
	__le16   handle;
} __packed;

#define HCI_OP_SETUP_SYNC_CONN		0x0428
struct hci_cp_setup_sync_conn {
	__le16   handle;
	__le32   tx_bandwidth;
	__le32   rx_bandwidth;
	__le16   max_latency;
	__le16   voice_setting;
	__u8     retrans_effort;
	__le16   pkt_type;
} __packed;

#define HCI_OP_ACCEPT_SYNC_CONN_REQ	0x0429
struct hci_cp_accept_sync_conn_req {
	bdaddr_t bdaddr;
	__le32   tx_bandwidth;
	__le32   rx_bandwidth;
	__le16   max_latency;
	__le16   content_format;
	__u8     retrans_effort;
	__le16   pkt_type;
} __packed;

#define HCI_OP_REJECT_SYNC_CONN_REQ	0x042a
struct hci_cp_reject_sync_conn_req {
	bdaddr_t bdaddr;
	__u8     reason;
} __packed;

#define HCI_OP_IO_CAPABILITY_REPLY	0x042b
struct hci_cp_io_capability_reply {
	bdaddr_t bdaddr;
	__u8     capability;
	__u8     oob_data;
	__u8     authentication;
} __packed;

#define HCI_OP_USER_CONFIRM_REPLY		0x042c
struct hci_cp_user_confirm_reply {
	bdaddr_t bdaddr;
} __packed;
struct hci_rp_user_confirm_reply {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_USER_CONFIRM_NEG_REPLY	0x042d

#define HCI_OP_USER_PASSKEY_REPLY		0x042e
struct hci_cp_user_passkey_reply {
	bdaddr_t bdaddr;
	__le32	passkey;
} __packed;

#define HCI_OP_USER_PASSKEY_NEG_REPLY	0x042f

#define HCI_OP_REMOTE_OOB_DATA_REPLY	0x0430
struct hci_cp_remote_oob_data_reply {
	bdaddr_t bdaddr;
	__u8     hash[16];
	__u8     randomizer[16];
} __packed;

#define HCI_OP_REMOTE_OOB_DATA_NEG_REPLY	0x0433
struct hci_cp_remote_oob_data_neg_reply {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_IO_CAPABILITY_NEG_REPLY	0x0434
struct hci_cp_io_capability_neg_reply {
	bdaddr_t bdaddr;
	__u8     reason;
} __packed;

#define HCI_OP_CREATE_PHY_LINK		0x0435
struct hci_cp_create_phy_link {
	__u8     phy_handle;
	__u8     key_len;
	__u8     key_type;
	__u8     key[HCI_AMP_LINK_KEY_SIZE];
} __packed;

#define HCI_OP_ACCEPT_PHY_LINK		0x0436
struct hci_cp_accept_phy_link {
	__u8     phy_handle;
	__u8     key_len;
	__u8     key_type;
	__u8     key[HCI_AMP_LINK_KEY_SIZE];
} __packed;

#define HCI_OP_DISCONN_PHY_LINK		0x0437
struct hci_cp_disconn_phy_link {
	__u8     phy_handle;
	__u8     reason;
} __packed;

struct ext_flow_spec {
	__u8       id;
	__u8       stype;
	__le16     msdu;
	__le32     sdu_itime;
	__le32     acc_lat;
	__le32     flush_to;
} __packed;

#define HCI_OP_CREATE_LOGICAL_LINK	0x0438
#define HCI_OP_ACCEPT_LOGICAL_LINK	0x0439
struct hci_cp_create_accept_logical_link {
	__u8                  phy_handle;
	struct ext_flow_spec  tx_flow_spec;
	struct ext_flow_spec  rx_flow_spec;
} __packed;

#define HCI_OP_DISCONN_LOGICAL_LINK	0x043a
struct hci_cp_disconn_logical_link {
	__le16   log_handle;
} __packed;

#define HCI_OP_LOGICAL_LINK_CANCEL	0x043b
struct hci_cp_logical_link_cancel {
	__u8     phy_handle;
	__u8     flow_spec_id;
} __packed;

struct hci_rp_logical_link_cancel {
	__u8     status;
	__u8     phy_handle;
	__u8     flow_spec_id;
} __packed;

#define HCI_OP_SNIFF_MODE		0x0803
struct hci_cp_sniff_mode {
	__le16   handle;
	__le16   max_interval;
	__le16   min_interval;
	__le16   attempt;
	__le16   timeout;
} __packed;

#define HCI_OP_EXIT_SNIFF_MODE		0x0804
struct hci_cp_exit_sniff_mode {
	__le16   handle;
} __packed;

#define HCI_OP_ROLE_DISCOVERY		0x0809
struct hci_cp_role_discovery {
	__le16   handle;
} __packed;
struct hci_rp_role_discovery {
	__u8     status;
	__le16   handle;
	__u8     role;
} __packed;

#define HCI_OP_SWITCH_ROLE		0x080b
struct hci_cp_switch_role {
	bdaddr_t bdaddr;
	__u8     role;
} __packed;

#define HCI_OP_READ_LINK_POLICY		0x080c
struct hci_cp_read_link_policy {
	__le16   handle;
} __packed;
struct hci_rp_read_link_policy {
	__u8     status;
	__le16   handle;
	__le16   policy;
} __packed;

#define HCI_OP_WRITE_LINK_POLICY	0x080d
struct hci_cp_write_link_policy {
	__le16   handle;
	__le16   policy;
} __packed;
struct hci_rp_write_link_policy {
	__u8     status;
	__le16   handle;
} __packed;

#define HCI_OP_READ_DEF_LINK_POLICY	0x080e
struct hci_rp_read_def_link_policy {
	__u8     status;
	__le16   policy;
} __packed;

#define HCI_OP_WRITE_DEF_LINK_POLICY	0x080f
struct hci_cp_write_def_link_policy {
	__le16   policy;
} __packed;

#define HCI_OP_SNIFF_SUBRATE		0x0811
struct hci_cp_sniff_subrate {
	__le16   handle;
	__le16   max_latency;
	__le16   min_remote_timeout;
	__le16   min_local_timeout;
} __packed;

#define HCI_OP_SET_EVENT_MASK		0x0c01
struct hci_cp_set_event_mask {
	__u8     mask[8];
} __packed;

#define HCI_OP_RESET			0x0c03

#define HCI_OP_SET_EVENT_FLT		0x0c05
struct hci_cp_set_event_flt {
	__u8     flt_type;
	__u8     cond_type;
	__u8     condition[0];
} __packed;

/* Filter types */
#define HCI_FLT_CLEAR_ALL	0x00
#define HCI_FLT_INQ_RESULT	0x01
#define HCI_FLT_CONN_SETUP	0x02

/* CONN_SETUP Condition types */
#define HCI_CONN_SETUP_ALLOW_ALL	0x00
#define HCI_CONN_SETUP_ALLOW_CLASS	0x01
#define HCI_CONN_SETUP_ALLOW_BDADDR	0x02

/* CONN_SETUP Conditions */
#define HCI_CONN_SETUP_AUTO_OFF	0x01
#define HCI_CONN_SETUP_AUTO_ON	0x02

#define HCI_OP_DELETE_STORED_LINK_KEY	0x0c12
struct hci_cp_delete_stored_link_key {
	bdaddr_t bdaddr;
	__u8     delete_all;
} __packed;

#define HCI_MAX_NAME_LENGTH		248

#define HCI_OP_WRITE_LOCAL_NAME		0x0c13
struct hci_cp_write_local_name {
	__u8     name[HCI_MAX_NAME_LENGTH];
} __packed;

#define HCI_OP_READ_LOCAL_NAME		0x0c14
struct hci_rp_read_local_name {
	__u8     status;
	__u8     name[HCI_MAX_NAME_LENGTH];
} __packed;

#define HCI_OP_WRITE_CA_TIMEOUT		0x0c16

#define HCI_OP_WRITE_PG_TIMEOUT		0x0c18

#define HCI_OP_WRITE_SCAN_ENABLE	0x0c1a
	#define SCAN_DISABLED		0x00
	#define SCAN_INQUIRY		0x01
	#define SCAN_PAGE		0x02

#define HCI_OP_READ_AUTH_ENABLE		0x0c1f

#define HCI_OP_WRITE_AUTH_ENABLE	0x0c20
	#define AUTH_DISABLED		0x00
	#define AUTH_ENABLED		0x01

#define HCI_OP_READ_ENCRYPT_MODE	0x0c21

#define HCI_OP_WRITE_ENCRYPT_MODE	0x0c22
	#define ENCRYPT_DISABLED	0x00
	#define ENCRYPT_P2P		0x01
	#define ENCRYPT_BOTH		0x02

#define HCI_OP_READ_CLASS_OF_DEV	0x0c23
struct hci_rp_read_class_of_dev {
	__u8     status;
	__u8     dev_class[3];
} __packed;

#define HCI_OP_WRITE_CLASS_OF_DEV	0x0c24
struct hci_cp_write_class_of_dev {
	__u8     dev_class[3];
} __packed;

#define HCI_OP_READ_VOICE_SETTING	0x0c25
struct hci_rp_read_voice_setting {
	__u8     status;
	__le16   voice_setting;
} __packed;

#define HCI_OP_WRITE_VOICE_SETTING	0x0c26
struct hci_cp_write_voice_setting {
	__le16   voice_setting;
} __packed;

#define HCI_OP_HOST_BUFFER_SIZE		0x0c33
struct hci_cp_host_buffer_size {
	__le16   acl_mtu;
	__u8     sco_mtu;
	__le16   acl_max_pkt;
	__le16   sco_max_pkt;
} __packed;

#define HCI_OP_WRITE_INQUIRY_MODE	0x0c45

#define HCI_MAX_EIR_LENGTH		240

#define HCI_OP_WRITE_EIR		0x0c52
struct hci_cp_write_eir {
	__u8	fec;
	__u8	data[HCI_MAX_EIR_LENGTH];
} __packed;

#define HCI_OP_READ_SSP_MODE		0x0c55
struct hci_rp_read_ssp_mode {
	__u8     status;
	__u8     mode;
} __packed;

#define HCI_OP_WRITE_SSP_MODE		0x0c56
struct hci_cp_write_ssp_mode {
	__u8     mode;
} __packed;

#define HCI_OP_READ_LOCAL_OOB_DATA		0x0c57
struct hci_rp_read_local_oob_data {
	__u8     status;
	__u8     hash[16];
	__u8     randomizer[16];
} __packed;

#define HCI_OP_READ_INQ_RSP_TX_POWER	0x0c58
struct hci_rp_read_inq_rsp_tx_power {
	__u8     status;
	__s8     tx_power;
} __packed;

#define HCI_OP_READ_FLOW_CONTROL_MODE	0x0c66
struct hci_rp_read_flow_control_mode {
	__u8     status;
	__u8     mode;
} __packed;

#define HCI_OP_WRITE_LE_HOST_SUPPORTED	0x0c6d
struct hci_cp_write_le_host_supported {
	__u8	le;
	__u8	simul;
} __packed;

#define HCI_OP_READ_LOCAL_VERSION	0x1001
struct hci_rp_read_local_version {
	__u8     status;
	__u8     hci_ver;
	__le16   hci_rev;
	__u8     lmp_ver;
	__le16   manufacturer;
	__le16   lmp_subver;
} __packed;

#define HCI_OP_READ_LOCAL_COMMANDS	0x1002
struct hci_rp_read_local_commands {
	__u8     status;
	__u8     commands[64];
} __packed;

#define HCI_OP_READ_LOCAL_FEATURES	0x1003
struct hci_rp_read_local_features {
	__u8     status;
	__u8     features[8];
} __packed;

#define HCI_OP_READ_LOCAL_EXT_FEATURES	0x1004
struct hci_cp_read_local_ext_features {
	__u8     page;
} __packed;
struct hci_rp_read_local_ext_features {
	__u8     status;
	__u8     page;
	__u8     max_page;
	__u8     features[8];
} __packed;

#define HCI_OP_READ_BUFFER_SIZE		0x1005
struct hci_rp_read_buffer_size {
	__u8     status;
	__le16   acl_mtu;
	__u8     sco_mtu;
	__le16   acl_max_pkt;
	__le16   sco_max_pkt;
} __packed;

#define HCI_OP_READ_BD_ADDR		0x1009
struct hci_rp_read_bd_addr {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_READ_DATA_BLOCK_SIZE	0x100a
struct hci_rp_read_data_block_size {
	__u8     status;
	__le16   max_acl_len;
	__le16   block_len;
	__le16   num_blocks;
} __packed;

#define HCI_OP_WRITE_PAGE_SCAN_ACTIVITY	0x0c1c
struct hci_cp_write_page_scan_activity {
	__le16   interval;
	__le16   window;
} __packed;

#define HCI_OP_WRITE_PAGE_SCAN_TYPE	0x0c47
	#define PAGE_SCAN_TYPE_STANDARD		0x00
	#define PAGE_SCAN_TYPE_INTERLACED	0x01

#define HCI_OP_READ_LOCAL_AMP_INFO	0x1409
struct hci_rp_read_local_amp_info {
	__u8     status;
	__u8     amp_status;
	__le32   total_bw;
	__le32   max_bw;
	__le32   min_latency;
	__le32   max_pdu;
	__u8     amp_type;
	__le16   pal_cap;
	__le16   max_assoc_size;
	__le32   max_flush_to;
	__le32   be_flush_to;
} __packed;

#define HCI_OP_READ_LOCAL_AMP_ASSOC	0x140a
struct hci_cp_read_local_amp_assoc {
	__u8     phy_handle;
	__le16   len_so_far;
	__le16   max_len;
} __packed;
struct hci_rp_read_local_amp_assoc {
	__u8     status;
	__u8     phy_handle;
	__le16   rem_len;
	__u8     frag[0];
} __packed;

#define HCI_OP_WRITE_REMOTE_AMP_ASSOC	0x140b
struct hci_cp_write_remote_amp_assoc {
	__u8     phy_handle;
	__le16   len_so_far;
	__le16   rem_len;
	__u8     frag[0];
} __packed;
struct hci_rp_write_remote_amp_assoc {
	__u8     status;
	__u8     phy_handle;
} __packed;

#define HCI_OP_LE_SET_EVENT_MASK	0x2001
struct hci_cp_le_set_event_mask {
	__u8     mask[8];
} __packed;

#define HCI_OP_LE_READ_BUFFER_SIZE	0x2002
struct hci_rp_le_read_buffer_size {
	__u8     status;
	__le16   le_mtu;
	__u8     le_max_pkt;
} __packed;

#define HCI_OP_LE_READ_ADV_TX_POWER	0x2007
struct hci_rp_le_read_adv_tx_power {
	__u8	status;
	__s8	tx_power;
} __packed;

#define HCI_MAX_AD_LENGTH		31

#define HCI_OP_LE_SET_ADV_DATA		0x2008
struct hci_cp_le_set_adv_data {
	__u8	length;
	__u8	data[HCI_MAX_AD_LENGTH];
} __packed;

#define HCI_OP_LE_SET_ADV_ENABLE	0x200a

#define HCI_OP_LE_SET_SCAN_PARAM	0x200b
struct hci_cp_le_set_scan_param {
	__u8    type;
	__le16  interval;
	__le16  window;
	__u8    own_address_type;
	__u8    filter_policy;
} __packed;

#define LE_SCANNING_DISABLED		0x00
#define LE_SCANNING_ENABLED		0x01

#define HCI_OP_LE_SET_SCAN_ENABLE	0x200c
struct hci_cp_le_set_scan_enable {
	__u8     enable;
	__u8     filter_dup;
} __packed;

#define HCI_OP_LE_CREATE_CONN		0x200d
struct hci_cp_le_create_conn {
	__le16   scan_interval;
	__le16   scan_window;
	__u8     filter_policy;
	__u8     peer_addr_type;
	bdaddr_t peer_addr;
	__u8     own_address_type;
	__le16   conn_interval_min;
	__le16   conn_interval_max;
	__le16   conn_latency;
	__le16   supervision_timeout;
	__le16   min_ce_len;
	__le16   max_ce_len;
} __packed;

#define HCI_OP_LE_CREATE_CONN_CANCEL	0x200e

#define HCI_OP_LE_CONN_UPDATE		0x2013
struct hci_cp_le_conn_update {
	__le16   handle;
	__le16   conn_interval_min;
	__le16   conn_interval_max;
	__le16   conn_latency;
	__le16   supervision_timeout;
	__le16   min_ce_len;
	__le16   max_ce_len;
} __packed;

#define HCI_OP_LE_START_ENC		0x2019
struct hci_cp_le_start_enc {
	__le16	handle;
	__u8	rand[8];
	__le16	ediv;
	__u8	ltk[16];
} __packed;

#define HCI_OP_LE_LTK_REPLY		0x201a
struct hci_cp_le_ltk_reply {
	__le16	handle;
	__u8	ltk[16];
} __packed;
struct hci_rp_le_ltk_reply {
	__u8	status;
	__le16	handle;
} __packed;

#define HCI_OP_LE_LTK_NEG_REPLY		0x201b
struct hci_cp_le_ltk_neg_reply {
	__le16	handle;
} __packed;
struct hci_rp_le_ltk_neg_reply {
	__u8	status;
	__le16	handle;
} __packed;

/* ---- HCI Events ---- */
#define HCI_EV_INQUIRY_COMPLETE		0x01

#define HCI_EV_INQUIRY_RESULT		0x02
struct inquiry_info {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     pscan_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
} __packed;

#define HCI_EV_CONN_COMPLETE		0x03
struct hci_ev_conn_complete {
	__u8     status;
	__le16   handle;
	bdaddr_t bdaddr;
	__u8     link_type;
	__u8     encr_mode;
} __packed;

#define HCI_EV_CONN_REQUEST		0x04
struct hci_ev_conn_request {
	bdaddr_t bdaddr;
	__u8     dev_class[3];
	__u8     link_type;
} __packed;

#define HCI_EV_DISCONN_COMPLETE		0x05
struct hci_ev_disconn_complete {
	__u8     status;
	__le16   handle;
	__u8     reason;
} __packed;

#define HCI_EV_AUTH_COMPLETE		0x06
struct hci_ev_auth_complete {
	__u8     status;
	__le16   handle;
} __packed;

#define HCI_EV_REMOTE_NAME		0x07
struct hci_ev_remote_name {
	__u8     status;
	bdaddr_t bdaddr;
	__u8     name[HCI_MAX_NAME_LENGTH];
} __packed;

#define HCI_EV_ENCRYPT_CHANGE		0x08
struct hci_ev_encrypt_change {
	__u8     status;
	__le16   handle;
	__u8     encrypt;
} __packed;

#define HCI_EV_CHANGE_LINK_KEY_COMPLETE	0x09
struct hci_ev_change_link_key_complete {
	__u8     status;
	__le16   handle;
} __packed;

#define HCI_EV_REMOTE_FEATURES		0x0b
struct hci_ev_remote_features {
	__u8     status;
	__le16   handle;
	__u8     features[8];
} __packed;

#define HCI_EV_REMOTE_VERSION		0x0c
struct hci_ev_remote_version {
	__u8     status;
	__le16   handle;
	__u8     lmp_ver;
	__le16   manufacturer;
	__le16   lmp_subver;
} __packed;

#define HCI_EV_QOS_SETUP_COMPLETE	0x0d
struct hci_qos {
	__u8     service_type;
	__u32    token_rate;
	__u32    peak_bandwidth;
	__u32    latency;
	__u32    delay_variation;
} __packed;
struct hci_ev_qos_setup_complete {
	__u8     status;
	__le16   handle;
	struct   hci_qos qos;
} __packed;

#define HCI_EV_CMD_COMPLETE		0x0e
struct hci_ev_cmd_complete {
	__u8     ncmd;
	__le16   opcode;
} __packed;

#define HCI_EV_CMD_STATUS		0x0f
struct hci_ev_cmd_status {
	__u8     status;
	__u8     ncmd;
	__le16   opcode;
} __packed;

#define HCI_EV_ROLE_CHANGE		0x12
struct hci_ev_role_change {
	__u8     status;
	bdaddr_t bdaddr;
	__u8     role;
} __packed;

#define HCI_EV_NUM_COMP_PKTS		0x13
struct hci_comp_pkts_info {
	__le16   handle;
	__le16   count;
} __packed;

struct hci_ev_num_comp_pkts {
	__u8     num_hndl;
	struct hci_comp_pkts_info handles[0];
} __packed;

#define HCI_EV_MODE_CHANGE		0x14
struct hci_ev_mode_change {
	__u8     status;
	__le16   handle;
	__u8     mode;
	__le16   interval;
} __packed;

#define HCI_EV_PIN_CODE_REQ		0x16
struct hci_ev_pin_code_req {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_LINK_KEY_REQ		0x17
struct hci_ev_link_key_req {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_LINK_KEY_NOTIFY		0x18
struct hci_ev_link_key_notify {
	bdaddr_t bdaddr;
	__u8     link_key[HCI_LINK_KEY_SIZE];
	__u8     key_type;
} __packed;

#define HCI_EV_CLOCK_OFFSET		0x1c
struct hci_ev_clock_offset {
	__u8     status;
	__le16   handle;
	__le16   clock_offset;
} __packed;

#define HCI_EV_PKT_TYPE_CHANGE		0x1d
struct hci_ev_pkt_type_change {
	__u8     status;
	__le16   handle;
	__le16   pkt_type;
} __packed;

#define HCI_EV_PSCAN_REP_MODE		0x20
struct hci_ev_pscan_rep_mode {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
} __packed;

#define HCI_EV_INQUIRY_RESULT_WITH_RSSI	0x22
struct inquiry_info_with_rssi {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
	__s8     rssi;
} __packed;
struct inquiry_info_with_rssi_and_pscan_mode {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     pscan_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
	__s8     rssi;
} __packed;

#define HCI_EV_REMOTE_EXT_FEATURES	0x23
struct hci_ev_remote_ext_features {
	__u8     status;
	__le16   handle;
	__u8     page;
	__u8     max_page;
	__u8     features[8];
} __packed;

#define HCI_EV_SYNC_CONN_COMPLETE	0x2c
struct hci_ev_sync_conn_complete {
	__u8     status;
	__le16   handle;
	bdaddr_t bdaddr;
	__u8     link_type;
	__u8     tx_interval;
	__u8     retrans_window;
	__le16   rx_pkt_len;
	__le16   tx_pkt_len;
	__u8     air_mode;
} __packed;

#define HCI_EV_SYNC_CONN_CHANGED	0x2d
struct hci_ev_sync_conn_changed {
	__u8     status;
	__le16   handle;
	__u8     tx_interval;
	__u8     retrans_window;
	__le16   rx_pkt_len;
	__le16   tx_pkt_len;
} __packed;

#define HCI_EV_SNIFF_SUBRATE		0x2e
struct hci_ev_sniff_subrate {
	__u8     status;
	__le16   handle;
	__le16   max_tx_latency;
	__le16   max_rx_latency;
	__le16   max_remote_timeout;
	__le16   max_local_timeout;
} __packed;

#define HCI_EV_EXTENDED_INQUIRY_RESULT	0x2f
struct extended_inquiry_info {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
	__s8     rssi;
	__u8     data[240];
} __packed;

#define HCI_EV_KEY_REFRESH_COMPLETE	0x30
struct hci_ev_key_refresh_complete {
	__u8	status;
	__le16	handle;
} __packed;

#define HCI_EV_IO_CAPA_REQUEST		0x31
struct hci_ev_io_capa_request {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_IO_CAPA_REPLY		0x32
struct hci_ev_io_capa_reply {
	bdaddr_t bdaddr;
	__u8     capability;
	__u8     oob_data;
	__u8     authentication;
} __packed;

#define HCI_EV_USER_CONFIRM_REQUEST	0x33
struct hci_ev_user_confirm_req {
	bdaddr_t	bdaddr;
	__le32		passkey;
} __packed;

#define HCI_EV_USER_PASSKEY_REQUEST	0x34
struct hci_ev_user_passkey_req {
	bdaddr_t	bdaddr;
} __packed;

#define HCI_EV_REMOTE_OOB_DATA_REQUEST	0x35
struct hci_ev_remote_oob_data_request {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_SIMPLE_PAIR_COMPLETE	0x36
struct hci_ev_simple_pair_complete {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_USER_PASSKEY_NOTIFY	0x3b
struct hci_ev_user_passkey_notify {
	bdaddr_t	bdaddr;
	__le32		passkey;
} __packed;

#define HCI_KEYPRESS_STARTED		0
#define HCI_KEYPRESS_ENTERED		1
#define HCI_KEYPRESS_ERASED		2
#define HCI_KEYPRESS_CLEARED		3
#define HCI_KEYPRESS_COMPLETED		4

#define HCI_EV_KEYPRESS_NOTIFY		0x3c
struct hci_ev_keypress_notify {
	bdaddr_t	bdaddr;
	__u8		type;
} __packed;

#define HCI_EV_REMOTE_HOST_FEATURES	0x3d
struct hci_ev_remote_host_features {
	bdaddr_t bdaddr;
	__u8     features[8];
} __packed;

#define HCI_EV_LE_META			0x3e
struct hci_ev_le_meta {
	__u8     subevent;
} __packed;

#define HCI_EV_PHY_LINK_COMPLETE	0x40
struct hci_ev_phy_link_complete {
	__u8     status;
	__u8     phy_handle;
} __packed;

#define HCI_EV_CHANNEL_SELECTED		0x41
struct hci_ev_channel_selected {
	__u8     phy_handle;
} __packed;

#define HCI_EV_DISCONN_PHY_LINK_COMPLETE	0x42
struct hci_ev_disconn_phy_link_complete {
	__u8     status;
	__u8     phy_handle;
	__u8     reason;
} __packed;

#define HCI_EV_LOGICAL_LINK_COMPLETE		0x45
struct hci_ev_logical_link_complete {
	__u8     status;
	__le16   handle;
	__u8     phy_handle;
	__u8     flow_spec_id;
} __packed;

#define HCI_EV_DISCONN_LOGICAL_LINK_COMPLETE	0x46
struct hci_ev_disconn_logical_link_complete {
	__u8     status;
	__le16   handle;
	__u8     reason;
} __packed;

#define HCI_EV_NUM_COMP_BLOCKS		0x48
struct hci_comp_blocks_info {
	__le16   handle;
	__le16   pkts;
	__le16   blocks;
} __packed;

struct hci_ev_num_comp_blocks {
	__le16   num_blocks;
	__u8     num_hndl;
	struct hci_comp_blocks_info handles[0];
} __packed;

/* Low energy meta events */
#define LE_CONN_ROLE_MASTER	0x00

#define HCI_EV_LE_CONN_COMPLETE		0x01
struct hci_ev_le_conn_complete {
	__u8     status;
	__le16   handle;
	__u8     role;
	__u8     bdaddr_type;
	bdaddr_t bdaddr;
	__le16   interval;
	__le16   latency;
	__le16   supervision_timeout;
	__u8     clk_accurancy;
} __packed;

#define HCI_EV_LE_LTK_REQ		0x05
struct hci_ev_le_ltk_req {
	__le16	handle;
	__u8	random[8];
	__le16	ediv;
} __packed;

/* Advertising report event types */
#define ADV_IND		0x00
#define ADV_DIRECT_IND	0x01
#define ADV_SCAN_IND	0x02
#define ADV_NONCONN_IND	0x03
#define ADV_SCAN_RSP	0x04

#define ADDR_LE_DEV_PUBLIC	0x00
#define ADDR_LE_DEV_RANDOM	0x01

#define HCI_EV_LE_ADVERTISING_REPORT	0x02
struct hci_ev_le_advertising_info {
	__u8	 evt_type;
	__u8	 bdaddr_type;
	bdaddr_t bdaddr;
	__u8	 length;
	__u8	 data[0];
} __packed;

/* Internal events generated by Bluetooth stack */
#define HCI_EV_STACK_INTERNAL	0xfd
struct hci_ev_stack_internal {
	__u16    type;
	__u8     data[0];
} __packed;

#define HCI_EV_SI_DEVICE	0x01
struct hci_ev_si_device {
	__u16    event;
	__u16    dev_id;
} __packed;

#define HCI_EV_SI_SECURITY	0x02
struct hci_ev_si_security {
	__u16    event;
	__u16    proto;
	__u16    subproto;
	__u8     incoming;
} __packed;

/* ---- HCI Packet structures ---- */
#define HCI_COMMAND_HDR_SIZE 3
#define HCI_EVENT_HDR_SIZE   2
#define HCI_ACL_HDR_SIZE     4
#define HCI_SCO_HDR_SIZE     3

struct hci_command_hdr {
	__le16	opcode;		/* OCF & OGF */
	__u8	plen;
} __packed;

struct hci_event_hdr {
	__u8	evt;
	__u8	plen;
} __packed;

struct hci_acl_hdr {
	__le16	handle;		/* Handle & Flags(PB, BC) */
	__le16	dlen;
} __packed;

struct hci_sco_hdr {
	__le16	handle;
	__u8	dlen;
} __packed;

static inline struct hci_event_hdr *hci_event_hdr(const struct sk_buff *skb)
{
	return (struct hci_event_hdr *) skb->data;
}

static inline struct hci_acl_hdr *hci_acl_hdr(const struct sk_buff *skb)
{
	return (struct hci_acl_hdr *) skb->data;
}

static inline struct hci_sco_hdr *hci_sco_hdr(const struct sk_buff *skb)
{
	return (struct hci_sco_hdr *) skb->data;
}

/* Command opcode pack/unpack */
#define hci_opcode_pack(ogf, ocf)	((__u16) ((ocf & 0x03ff)|(ogf << 10)))
#define hci_opcode_ogf(op)		(op >> 10)
#define hci_opcode_ocf(op)		(op & 0x03ff)

/* ACL handle and flags pack/unpack */
#define hci_handle_pack(h, f)	((__u16) ((h & 0x0fff)|(f << 12)))
#define hci_handle(h)		(h & 0x0fff)
#define hci_flags(h)		(h >> 12)

/* ---- HCI Sockets ---- */

/* Socket options */
#define HCI_DATA_DIR	1
#define HCI_FILTER	2
#define HCI_TIME_STAMP	3

/* CMSG flags */
#define HCI_CMSG_DIR	0x0001
#define HCI_CMSG_TSTAMP	0x0002

struct sockaddr_hci {
	sa_family_t    hci_family;
	unsigned short hci_dev;
	unsigned short hci_channel;
};
#define HCI_DEV_NONE	0xffff

#define HCI_CHANNEL_RAW		0
#define HCI_CHANNEL_MONITOR	2
#define HCI_CHANNEL_CONTROL	3

struct hci_filter {
	unsigned long type_mask;
	unsigned long event_mask[2];
	__le16 opcode;
};

struct hci_ufilter {
	__u32  type_mask;
	__u32  event_mask[2];
	__le16 opcode;
};

#define HCI_FLT_TYPE_BITS	31
#define HCI_FLT_EVENT_BITS	63
#define HCI_FLT_OGF_BITS	63
#define HCI_FLT_OCF_BITS	127

/* ---- HCI Ioctl requests structures ---- */
struct hci_dev_stats {
	__u32 err_rx;
	__u32 err_tx;
	__u32 cmd_tx;
	__u32 evt_rx;
	__u32 acl_tx;
	__u32 acl_rx;
	__u32 sco_tx;
	__u32 sco_rx;
	__u32 byte_rx;
	__u32 byte_tx;
};

struct hci_dev_info {
	__u16 dev_id;
	char  name[8];

	bdaddr_t bdaddr;

	__u32 flags;
	__u8  type;

	__u8  features[8];

	__u32 pkt_type;
	__u32 link_policy;
	__u32 link_mode;

	__u16 acl_mtu;
	__u16 acl_pkts;
	__u16 sco_mtu;
	__u16 sco_pkts;

	struct hci_dev_stats stat;
};

struct hci_conn_info {
	__u16    handle;
	bdaddr_t bdaddr;
	__u8     type;
	__u8     out;
	__u16    state;
	__u32    link_mode;
};

struct hci_dev_req {
	__u16  dev_id;
	__u32  dev_opt;
};

struct hci_dev_list_req {
	__u16  dev_num;
	struct hci_dev_req dev_req[0];	/* hci_dev_req structures */
};

struct hci_conn_list_req {
	__u16  dev_id;
	__u16  conn_num;
	struct hci_conn_info conn_info[0];
};

struct hci_conn_info_req {
	bdaddr_t bdaddr;
	__u8     type;
	struct   hci_conn_info conn_info[0];
};

struct hci_auth_info_req {
	bdaddr_t bdaddr;
	__u8     type;
};

struct hci_inquiry_req {
	__u16 dev_id;
	__u16 flags;
	__u8  lap[3];
	__u8  length;
	__u8  num_rsp;
};
#define IREQ_CACHE_FLUSH 0x0001

extern bool enable_hs;

#endif /* __HCI_H */
