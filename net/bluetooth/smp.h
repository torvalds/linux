/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).

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

#ifndef __SMP_H
#define __SMP_H

struct smp_command_hdr {
	__u8	code;
} __packed;

#define SMP_CMD_PAIRING_REQ	0x01
#define SMP_CMD_PAIRING_RSP	0x02
struct smp_cmd_pairing {
	__u8	io_capability;
	__u8	oob_flag;
	__u8	auth_req;
	__u8	max_key_size;
	__u8	init_key_dist;
	__u8	resp_key_dist;
} __packed;

#define SMP_IO_DISPLAY_ONLY	0x00
#define SMP_IO_DISPLAY_YESNO	0x01
#define SMP_IO_KEYBOARD_ONLY	0x02
#define SMP_IO_NO_INPUT_OUTPUT	0x03
#define SMP_IO_KEYBOARD_DISPLAY	0x04

#define SMP_OOB_NOT_PRESENT	0x00
#define SMP_OOB_PRESENT		0x01

#define SMP_DIST_ENC_KEY	0x01
#define SMP_DIST_ID_KEY		0x02
#define SMP_DIST_SIGN		0x04
#define SMP_DIST_LINK_KEY	0x08

#define SMP_AUTH_NONE		0x00
#define SMP_AUTH_BONDING	0x01
#define SMP_AUTH_MITM		0x04
#define SMP_AUTH_SC		0x08
#define SMP_AUTH_KEYPRESS	0x10
#define SMP_AUTH_CT2		0x20

#define SMP_CMD_PAIRING_CONFIRM	0x03
struct smp_cmd_pairing_confirm {
	__u8	confirm_val[16];
} __packed;

#define SMP_CMD_PAIRING_RANDOM	0x04
struct smp_cmd_pairing_random {
	__u8	rand_val[16];
} __packed;

#define SMP_CMD_PAIRING_FAIL	0x05
struct smp_cmd_pairing_fail {
	__u8	reason;
} __packed;

#define SMP_CMD_ENCRYPT_INFO	0x06
struct smp_cmd_encrypt_info {
	__u8	ltk[16];
} __packed;

#define SMP_CMD_MASTER_IDENT	0x07
struct smp_cmd_master_ident {
	__le16	ediv;
	__le64	rand;
} __packed;

#define SMP_CMD_IDENT_INFO	0x08
struct smp_cmd_ident_info {
	__u8	irk[16];
} __packed;

#define SMP_CMD_IDENT_ADDR_INFO	0x09
struct smp_cmd_ident_addr_info {
	__u8	addr_type;
	bdaddr_t bdaddr;
} __packed;

#define SMP_CMD_SIGN_INFO	0x0a
struct smp_cmd_sign_info {
	__u8	csrk[16];
} __packed;

#define SMP_CMD_SECURITY_REQ	0x0b
struct smp_cmd_security_req {
	__u8	auth_req;
} __packed;

#define SMP_CMD_PUBLIC_KEY	0x0c
struct smp_cmd_public_key {
	__u8	x[32];
	__u8	y[32];
} __packed;

#define SMP_CMD_DHKEY_CHECK	0x0d
struct smp_cmd_dhkey_check {
	__u8	e[16];
} __packed;

#define SMP_CMD_KEYPRESS_NOTIFY	0x0e
struct smp_cmd_keypress_notify {
	__u8	value;
} __packed;

#define SMP_CMD_MAX		0x0e

#define SMP_PASSKEY_ENTRY_FAILED	0x01
#define SMP_OOB_NOT_AVAIL		0x02
#define SMP_AUTH_REQUIREMENTS		0x03
#define SMP_CONFIRM_FAILED		0x04
#define SMP_PAIRING_NOTSUPP		0x05
#define SMP_ENC_KEY_SIZE		0x06
#define SMP_CMD_NOTSUPP			0x07
#define SMP_UNSPECIFIED			0x08
#define SMP_REPEATED_ATTEMPTS		0x09
#define SMP_INVALID_PARAMS		0x0a
#define SMP_DHKEY_CHECK_FAILED		0x0b
#define SMP_NUMERIC_COMP_FAILED		0x0c
#define SMP_BREDR_PAIRING_IN_PROGRESS	0x0d
#define SMP_CROSS_TRANSP_NOT_ALLOWED	0x0e

#define SMP_MIN_ENC_KEY_SIZE		7
#define SMP_MAX_ENC_KEY_SIZE		16

/* LTK types used in internal storage (struct smp_ltk) */
enum {
	SMP_STK,
	SMP_LTK,
	SMP_LTK_SLAVE,
	SMP_LTK_P256,
	SMP_LTK_P256_DEBUG,
};

static inline bool smp_ltk_is_sc(struct smp_ltk *key)
{
	switch (key->type) {
	case SMP_LTK_P256:
	case SMP_LTK_P256_DEBUG:
		return true;
	}

	return false;
}

static inline u8 smp_ltk_sec_level(struct smp_ltk *key)
{
	if (key->authenticated) {
		if (smp_ltk_is_sc(key))
			return BT_SECURITY_FIPS;
		else
			return BT_SECURITY_HIGH;
	}

	return BT_SECURITY_MEDIUM;
}

/* Key preferences for smp_sufficient security */
enum smp_key_pref {
	SMP_ALLOW_STK,
	SMP_USE_LTK,
};

/* SMP Commands */
int smp_cancel_and_remove_pairing(struct hci_dev *hdev, bdaddr_t *bdaddr,
				  u8 addr_type);
bool smp_sufficient_security(struct hci_conn *hcon, u8 sec_level,
			     enum smp_key_pref key_pref);
int smp_conn_security(struct hci_conn *hcon, __u8 sec_level);
int smp_user_confirm_reply(struct hci_conn *conn, u16 mgmt_op, __le32 passkey);

bool smp_irk_matches(struct hci_dev *hdev, const u8 irk[16],
		     const bdaddr_t *bdaddr);
int smp_generate_rpa(struct hci_dev *hdev, const u8 irk[16], bdaddr_t *rpa);
int smp_generate_oob(struct hci_dev *hdev, u8 hash[16], u8 rand[16]);

int smp_force_bredr(struct hci_dev *hdev, bool enable);

int smp_register(struct hci_dev *hdev);
void smp_unregister(struct hci_dev *hdev);

#if IS_ENABLED(CONFIG_BT_SELFTEST_SMP)

int bt_selftest_smp(void);

#else

static inline int bt_selftest_smp(void)
{
	return 0;
}

#endif

#endif /* __SMP_H */
