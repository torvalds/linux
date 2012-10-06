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

#define SMP_AUTH_NONE		0x00
#define SMP_AUTH_BONDING	0x01
#define SMP_AUTH_MITM		0x04

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
	__u16	ediv;
	__u8	rand[8];
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

#define SMP_PASSKEY_ENTRY_FAILED	0x01
#define SMP_OOB_NOT_AVAIL		0x02
#define SMP_AUTH_REQUIREMENTS		0x03
#define SMP_CONFIRM_FAILED		0x04
#define SMP_PAIRING_NOTSUPP		0x05
#define SMP_ENC_KEY_SIZE		0x06
#define SMP_CMD_NOTSUPP		0x07
#define SMP_UNSPECIFIED		0x08
#define SMP_REPEATED_ATTEMPTS		0x09

#define SMP_MIN_ENC_KEY_SIZE		7
#define SMP_MAX_ENC_KEY_SIZE		16

#define SMP_FLAG_TK_VALID	1
#define SMP_FLAG_CFM_PENDING	2
#define SMP_FLAG_MITM_AUTH	3

struct smp_chan {
	struct l2cap_conn *conn;
	u8		preq[7]; /* SMP Pairing Request */
	u8		prsp[7]; /* SMP Pairing Response */
	u8              prnd[16]; /* SMP Pairing Random (local) */
	u8              rrnd[16]; /* SMP Pairing Random (remote) */
	u8		pcnf[16]; /* SMP Pairing Confirm */
	u8		tk[16]; /* SMP Temporary Key */
	u8		enc_key_size;
	unsigned long	smp_flags;
	struct crypto_blkcipher	*tfm;
	struct work_struct confirm;
	struct work_struct random;

};

/* SMP Commands */
int smp_conn_security(struct hci_conn *hcon, __u8 sec_level);
int smp_sig_channel(struct l2cap_conn *conn, struct sk_buff *skb);
int smp_distribute_keys(struct l2cap_conn *conn, __u8 force);
int smp_user_confirm_reply(struct hci_conn *conn, u16 mgmt_op, __le32 passkey);

void smp_chan_destroy(struct l2cap_conn *conn);

#endif /* __SMP_H */
