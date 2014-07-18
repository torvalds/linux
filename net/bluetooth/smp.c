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

#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/b128ops.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/mgmt.h>

#include "smp.h"

#define SMP_TIMEOUT	msecs_to_jiffies(30000)

#define AUTH_REQ_MASK   0x07

enum {
	SMP_FLAG_TK_VALID,
	SMP_FLAG_CFM_PENDING,
	SMP_FLAG_MITM_AUTH,
	SMP_FLAG_COMPLETE,
	SMP_FLAG_INITIATOR,
};

struct smp_chan {
	struct l2cap_conn *conn;
	u8		preq[7]; /* SMP Pairing Request */
	u8		prsp[7]; /* SMP Pairing Response */
	u8		prnd[16]; /* SMP Pairing Random (local) */
	u8		rrnd[16]; /* SMP Pairing Random (remote) */
	u8		pcnf[16]; /* SMP Pairing Confirm */
	u8		tk[16]; /* SMP Temporary Key */
	u8		enc_key_size;
	u8		remote_key_dist;
	bdaddr_t	id_addr;
	u8		id_addr_type;
	u8		irk[16];
	struct smp_csrk	*csrk;
	struct smp_csrk	*slave_csrk;
	struct smp_ltk	*ltk;
	struct smp_ltk	*slave_ltk;
	struct smp_irk	*remote_irk;
	unsigned long	flags;

	struct crypto_blkcipher	*tfm_aes;
};

static inline void swap_buf(const u8 *src, u8 *dst, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		dst[len - 1 - i] = src[i];
}

static int smp_e(struct crypto_blkcipher *tfm, const u8 *k, u8 *r)
{
	struct blkcipher_desc desc;
	struct scatterlist sg;
	uint8_t tmp[16], data[16];
	int err;

	if (tfm == NULL) {
		BT_ERR("tfm %p", tfm);
		return -EINVAL;
	}

	desc.tfm = tfm;
	desc.flags = 0;

	/* The most significant octet of key corresponds to k[0] */
	swap_buf(k, tmp, 16);

	err = crypto_blkcipher_setkey(tfm, tmp, 16);
	if (err) {
		BT_ERR("cipher setkey failed: %d", err);
		return err;
	}

	/* Most significant octet of plaintextData corresponds to data[0] */
	swap_buf(r, data, 16);

	sg_init_one(&sg, data, 16);

	err = crypto_blkcipher_encrypt(&desc, &sg, &sg, 16);
	if (err)
		BT_ERR("Encrypt data error %d", err);

	/* Most significant octet of encryptedData corresponds to data[0] */
	swap_buf(data, r, 16);

	return err;
}

static int smp_ah(struct crypto_blkcipher *tfm, u8 irk[16], u8 r[3], u8 res[3])
{
	u8 _res[16];
	int err;

	/* r' = padding || r */
	memcpy(_res, r, 3);
	memset(_res + 3, 0, 13);

	err = smp_e(tfm, irk, _res);
	if (err) {
		BT_ERR("Encrypt error");
		return err;
	}

	/* The output of the random address function ah is:
	 *	ah(h, r) = e(k, r') mod 2^24
	 * The output of the security function e is then truncated to 24 bits
	 * by taking the least significant 24 bits of the output of e as the
	 * result of ah.
	 */
	memcpy(res, _res, 3);

	return 0;
}

bool smp_irk_matches(struct crypto_blkcipher *tfm, u8 irk[16],
		     bdaddr_t *bdaddr)
{
	u8 hash[3];
	int err;

	BT_DBG("RPA %pMR IRK %*phN", bdaddr, 16, irk);

	err = smp_ah(tfm, irk, &bdaddr->b[3], hash);
	if (err)
		return false;

	return !memcmp(bdaddr->b, hash, 3);
}

int smp_generate_rpa(struct crypto_blkcipher *tfm, u8 irk[16], bdaddr_t *rpa)
{
	int err;

	get_random_bytes(&rpa->b[3], 3);

	rpa->b[5] &= 0x3f;	/* Clear two most significant bits */
	rpa->b[5] |= 0x40;	/* Set second most significant bit */

	err = smp_ah(tfm, irk, &rpa->b[3], rpa->b);
	if (err < 0)
		return err;

	BT_DBG("RPA %pMR", rpa);

	return 0;
}

static int smp_c1(struct smp_chan *smp, u8 k[16], u8 r[16], u8 preq[7],
		  u8 pres[7], u8 _iat, bdaddr_t *ia, u8 _rat, bdaddr_t *ra,
		  u8 res[16])
{
	struct hci_dev *hdev = smp->conn->hcon->hdev;
	u8 p1[16], p2[16];
	int err;

	BT_DBG("%s", hdev->name);

	memset(p1, 0, 16);

	/* p1 = pres || preq || _rat || _iat */
	p1[0] = _iat;
	p1[1] = _rat;
	memcpy(p1 + 2, preq, 7);
	memcpy(p1 + 9, pres, 7);

	/* p2 = padding || ia || ra */
	memcpy(p2, ra, 6);
	memcpy(p2 + 6, ia, 6);
	memset(p2 + 12, 0, 4);

	/* res = r XOR p1 */
	u128_xor((u128 *) res, (u128 *) r, (u128 *) p1);

	/* res = e(k, res) */
	err = smp_e(smp->tfm_aes, k, res);
	if (err) {
		BT_ERR("Encrypt data error");
		return err;
	}

	/* res = res XOR p2 */
	u128_xor((u128 *) res, (u128 *) res, (u128 *) p2);

	/* res = e(k, res) */
	err = smp_e(smp->tfm_aes, k, res);
	if (err)
		BT_ERR("Encrypt data error");

	return err;
}

static int smp_s1(struct smp_chan *smp, u8 k[16], u8 r1[16], u8 r2[16],
		  u8 _r[16])
{
	struct hci_dev *hdev = smp->conn->hcon->hdev;
	int err;

	BT_DBG("%s", hdev->name);

	/* Just least significant octets from r1 and r2 are considered */
	memcpy(_r, r2, 8);
	memcpy(_r + 8, r1, 8);

	err = smp_e(smp->tfm_aes, k, _r);
	if (err)
		BT_ERR("Encrypt data error");

	return err;
}

static struct sk_buff *smp_build_cmd(struct l2cap_conn *conn, u8 code,
				     u16 dlen, void *data)
{
	struct sk_buff *skb;
	struct l2cap_hdr *lh;
	int len;

	len = L2CAP_HDR_SIZE + sizeof(code) + dlen;

	if (len > conn->mtu)
		return NULL;

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb)
		return NULL;

	lh = (struct l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = cpu_to_le16(sizeof(code) + dlen);
	lh->cid = cpu_to_le16(L2CAP_CID_SMP);

	memcpy(skb_put(skb, sizeof(code)), &code, sizeof(code));

	memcpy(skb_put(skb, dlen), data, dlen);

	return skb;
}

static void smp_send_cmd(struct l2cap_conn *conn, u8 code, u16 len, void *data)
{
	struct sk_buff *skb = smp_build_cmd(conn, code, len, data);

	BT_DBG("code 0x%2.2x", code);

	if (!skb)
		return;

	skb->priority = HCI_PRIO_MAX;
	hci_send_acl(conn->hchan, skb, 0);

	cancel_delayed_work_sync(&conn->security_timer);
	schedule_delayed_work(&conn->security_timer, SMP_TIMEOUT);
}

static __u8 authreq_to_seclevel(__u8 authreq)
{
	if (authreq & SMP_AUTH_MITM)
		return BT_SECURITY_HIGH;
	else
		return BT_SECURITY_MEDIUM;
}

static __u8 seclevel_to_authreq(__u8 sec_level)
{
	switch (sec_level) {
	case BT_SECURITY_HIGH:
		return SMP_AUTH_MITM | SMP_AUTH_BONDING;
	case BT_SECURITY_MEDIUM:
		return SMP_AUTH_BONDING;
	default:
		return SMP_AUTH_NONE;
	}
}

static void build_pairing_cmd(struct l2cap_conn *conn,
			      struct smp_cmd_pairing *req,
			      struct smp_cmd_pairing *rsp, __u8 authreq)
{
	struct smp_chan *smp = conn->smp_chan;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	u8 local_dist = 0, remote_dist = 0;

	if (test_bit(HCI_PAIRABLE, &conn->hcon->hdev->dev_flags)) {
		local_dist = SMP_DIST_ENC_KEY | SMP_DIST_SIGN;
		remote_dist = SMP_DIST_ENC_KEY | SMP_DIST_SIGN;
		authreq |= SMP_AUTH_BONDING;
	} else {
		authreq &= ~SMP_AUTH_BONDING;
	}

	if (test_bit(HCI_RPA_RESOLVING, &hdev->dev_flags))
		remote_dist |= SMP_DIST_ID_KEY;

	if (test_bit(HCI_PRIVACY, &hdev->dev_flags))
		local_dist |= SMP_DIST_ID_KEY;

	if (rsp == NULL) {
		req->io_capability = conn->hcon->io_capability;
		req->oob_flag = SMP_OOB_NOT_PRESENT;
		req->max_key_size = SMP_MAX_ENC_KEY_SIZE;
		req->init_key_dist = local_dist;
		req->resp_key_dist = remote_dist;
		req->auth_req = (authreq & AUTH_REQ_MASK);

		smp->remote_key_dist = remote_dist;
		return;
	}

	rsp->io_capability = conn->hcon->io_capability;
	rsp->oob_flag = SMP_OOB_NOT_PRESENT;
	rsp->max_key_size = SMP_MAX_ENC_KEY_SIZE;
	rsp->init_key_dist = req->init_key_dist & remote_dist;
	rsp->resp_key_dist = req->resp_key_dist & local_dist;
	rsp->auth_req = (authreq & AUTH_REQ_MASK);

	smp->remote_key_dist = rsp->init_key_dist;
}

static u8 check_enc_key_size(struct l2cap_conn *conn, __u8 max_key_size)
{
	struct smp_chan *smp = conn->smp_chan;

	if ((max_key_size > SMP_MAX_ENC_KEY_SIZE) ||
	    (max_key_size < SMP_MIN_ENC_KEY_SIZE))
		return SMP_ENC_KEY_SIZE;

	smp->enc_key_size = max_key_size;

	return 0;
}

static void smp_failure(struct l2cap_conn *conn, u8 reason)
{
	struct hci_conn *hcon = conn->hcon;

	if (reason)
		smp_send_cmd(conn, SMP_CMD_PAIRING_FAIL, sizeof(reason),
			     &reason);

	clear_bit(HCI_CONN_ENCRYPT_PEND, &hcon->flags);
	mgmt_auth_failed(hcon->hdev, &hcon->dst, hcon->type, hcon->dst_type,
			 HCI_ERROR_AUTH_FAILURE);

	cancel_delayed_work_sync(&conn->security_timer);

	if (test_and_clear_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags))
		smp_chan_destroy(conn);
}

#define JUST_WORKS	0x00
#define JUST_CFM	0x01
#define REQ_PASSKEY	0x02
#define CFM_PASSKEY	0x03
#define REQ_OOB		0x04
#define OVERLAP		0xFF

static const u8 gen_method[5][5] = {
	{ JUST_WORKS,  JUST_CFM,    REQ_PASSKEY, JUST_WORKS, REQ_PASSKEY },
	{ JUST_WORKS,  JUST_CFM,    REQ_PASSKEY, JUST_WORKS, REQ_PASSKEY },
	{ CFM_PASSKEY, CFM_PASSKEY, REQ_PASSKEY, JUST_WORKS, CFM_PASSKEY },
	{ JUST_WORKS,  JUST_CFM,    JUST_WORKS,  JUST_WORKS, JUST_CFM    },
	{ CFM_PASSKEY, CFM_PASSKEY, REQ_PASSKEY, JUST_WORKS, OVERLAP     },
};

static u8 get_auth_method(struct smp_chan *smp, u8 local_io, u8 remote_io)
{
	/* If either side has unknown io_caps, use JUST_CFM (which gets
	 * converted later to JUST_WORKS if we're initiators.
	 */
	if (local_io > SMP_IO_KEYBOARD_DISPLAY ||
	    remote_io > SMP_IO_KEYBOARD_DISPLAY)
		return JUST_CFM;

	return gen_method[remote_io][local_io];
}

static int tk_request(struct l2cap_conn *conn, u8 remote_oob, u8 auth,
						u8 local_io, u8 remote_io)
{
	struct hci_conn *hcon = conn->hcon;
	struct smp_chan *smp = conn->smp_chan;
	u8 method;
	u32 passkey = 0;
	int ret = 0;

	/* Initialize key for JUST WORKS */
	memset(smp->tk, 0, sizeof(smp->tk));
	clear_bit(SMP_FLAG_TK_VALID, &smp->flags);

	BT_DBG("tk_request: auth:%d lcl:%d rem:%d", auth, local_io, remote_io);

	/* If neither side wants MITM, either "just" confirm an incoming
	 * request or use just-works for outgoing ones. The JUST_CFM
	 * will be converted to JUST_WORKS if necessary later in this
	 * function. If either side has MITM look up the method from the
	 * table.
	 */
	if (!(auth & SMP_AUTH_MITM))
		method = JUST_CFM;
	else
		method = get_auth_method(smp, local_io, remote_io);

	/* Don't confirm locally initiated pairing attempts */
	if (method == JUST_CFM && test_bit(SMP_FLAG_INITIATOR, &smp->flags))
		method = JUST_WORKS;

	/* If Just Works, Continue with Zero TK */
	if (method == JUST_WORKS) {
		set_bit(SMP_FLAG_TK_VALID, &smp->flags);
		return 0;
	}

	/* Not Just Works/Confirm results in MITM Authentication */
	if (method != JUST_CFM)
		set_bit(SMP_FLAG_MITM_AUTH, &smp->flags);

	/* If both devices have Keyoard-Display I/O, the master
	 * Confirms and the slave Enters the passkey.
	 */
	if (method == OVERLAP) {
		if (test_bit(HCI_CONN_MASTER, &hcon->flags))
			method = CFM_PASSKEY;
		else
			method = REQ_PASSKEY;
	}

	/* Generate random passkey. */
	if (method == CFM_PASSKEY) {
		memset(smp->tk, 0, sizeof(smp->tk));
		get_random_bytes(&passkey, sizeof(passkey));
		passkey %= 1000000;
		put_unaligned_le32(passkey, smp->tk);
		BT_DBG("PassKey: %d", passkey);
		set_bit(SMP_FLAG_TK_VALID, &smp->flags);
	}

	hci_dev_lock(hcon->hdev);

	if (method == REQ_PASSKEY)
		ret = mgmt_user_passkey_request(hcon->hdev, &hcon->dst,
						hcon->type, hcon->dst_type);
	else if (method == JUST_CFM)
		ret = mgmt_user_confirm_request(hcon->hdev, &hcon->dst,
						hcon->type, hcon->dst_type,
						passkey, 1);
	else
		ret = mgmt_user_passkey_notify(hcon->hdev, &hcon->dst,
						hcon->type, hcon->dst_type,
						passkey, 0);

	hci_dev_unlock(hcon->hdev);

	return ret;
}

static u8 smp_confirm(struct smp_chan *smp)
{
	struct l2cap_conn *conn = smp->conn;
	struct smp_cmd_pairing_confirm cp;
	int ret;

	BT_DBG("conn %p", conn);

	ret = smp_c1(smp, smp->tk, smp->prnd, smp->preq, smp->prsp,
		     conn->hcon->init_addr_type, &conn->hcon->init_addr,
		     conn->hcon->resp_addr_type, &conn->hcon->resp_addr,
		     cp.confirm_val);
	if (ret)
		return SMP_UNSPECIFIED;

	clear_bit(SMP_FLAG_CFM_PENDING, &smp->flags);

	smp_send_cmd(smp->conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cp), &cp);

	return 0;
}

static u8 smp_random(struct smp_chan *smp)
{
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	u8 confirm[16];
	int ret;

	if (IS_ERR_OR_NULL(smp->tfm_aes))
		return SMP_UNSPECIFIED;

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	ret = smp_c1(smp, smp->tk, smp->rrnd, smp->preq, smp->prsp,
		     hcon->init_addr_type, &hcon->init_addr,
		     hcon->resp_addr_type, &hcon->resp_addr, confirm);
	if (ret)
		return SMP_UNSPECIFIED;

	if (memcmp(smp->pcnf, confirm, sizeof(smp->pcnf)) != 0) {
		BT_ERR("Pairing failed (confirmation values mismatch)");
		return SMP_CONFIRM_FAILED;
	}

	if (hcon->out) {
		u8 stk[16];
		__le64 rand = 0;
		__le16 ediv = 0;

		smp_s1(smp, smp->tk, smp->rrnd, smp->prnd, stk);

		memset(stk + smp->enc_key_size, 0,
		       SMP_MAX_ENC_KEY_SIZE - smp->enc_key_size);

		if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->flags))
			return SMP_UNSPECIFIED;

		hci_le_start_enc(hcon, ediv, rand, stk);
		hcon->enc_key_size = smp->enc_key_size;
		set_bit(HCI_CONN_STK_ENCRYPT, &hcon->flags);
	} else {
		u8 stk[16], auth;
		__le64 rand = 0;
		__le16 ediv = 0;

		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(smp->prnd),
			     smp->prnd);

		smp_s1(smp, smp->tk, smp->prnd, smp->rrnd, stk);

		memset(stk + smp->enc_key_size, 0,
		       SMP_MAX_ENC_KEY_SIZE - smp->enc_key_size);

		if (hcon->pending_sec_level == BT_SECURITY_HIGH)
			auth = 1;
		else
			auth = 0;

		/* Even though there's no _SLAVE suffix this is the
		 * slave STK we're adding for later lookup (the master
		 * STK never needs to be stored).
		 */
		hci_add_ltk(hcon->hdev, &hcon->dst, hcon->dst_type,
			    SMP_STK, auth, stk, smp->enc_key_size, ediv, rand);
	}

	return 0;
}

static struct smp_chan *smp_chan_create(struct l2cap_conn *conn)
{
	struct smp_chan *smp;

	smp = kzalloc(sizeof(*smp), GFP_ATOMIC);
	if (!smp)
		return NULL;

	smp->tfm_aes = crypto_alloc_blkcipher("ecb(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(smp->tfm_aes)) {
		BT_ERR("Unable to create ECB crypto context");
		kfree(smp);
		return NULL;
	}

	smp->conn = conn;
	conn->smp_chan = smp;

	hci_conn_hold(conn->hcon);

	return smp;
}

void smp_chan_destroy(struct l2cap_conn *conn)
{
	struct smp_chan *smp = conn->smp_chan;
	bool complete;

	BUG_ON(!smp);

	complete = test_bit(SMP_FLAG_COMPLETE, &smp->flags);
	mgmt_smp_complete(conn->hcon, complete);

	kfree(smp->csrk);
	kfree(smp->slave_csrk);

	crypto_free_blkcipher(smp->tfm_aes);

	/* If pairing failed clean up any keys we might have */
	if (!complete) {
		if (smp->ltk) {
			list_del(&smp->ltk->list);
			kfree(smp->ltk);
		}

		if (smp->slave_ltk) {
			list_del(&smp->slave_ltk->list);
			kfree(smp->slave_ltk);
		}

		if (smp->remote_irk) {
			list_del(&smp->remote_irk->list);
			kfree(smp->remote_irk);
		}
	}

	kfree(smp);
	conn->smp_chan = NULL;
	hci_conn_drop(conn->hcon);
}

int smp_user_confirm_reply(struct hci_conn *hcon, u16 mgmt_op, __le32 passkey)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct smp_chan *smp;
	u32 value;

	BT_DBG("");

	if (!conn || !test_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags))
		return -ENOTCONN;

	smp = conn->smp_chan;

	switch (mgmt_op) {
	case MGMT_OP_USER_PASSKEY_REPLY:
		value = le32_to_cpu(passkey);
		memset(smp->tk, 0, sizeof(smp->tk));
		BT_DBG("PassKey: %d", value);
		put_unaligned_le32(value, smp->tk);
		/* Fall Through */
	case MGMT_OP_USER_CONFIRM_REPLY:
		set_bit(SMP_FLAG_TK_VALID, &smp->flags);
		break;
	case MGMT_OP_USER_PASSKEY_NEG_REPLY:
	case MGMT_OP_USER_CONFIRM_NEG_REPLY:
		smp_failure(conn, SMP_PASSKEY_ENTRY_FAILED);
		return 0;
	default:
		smp_failure(conn, SMP_PASSKEY_ENTRY_FAILED);
		return -EOPNOTSUPP;
	}

	/* If it is our turn to send Pairing Confirm, do so now */
	if (test_bit(SMP_FLAG_CFM_PENDING, &smp->flags)) {
		u8 rsp = smp_confirm(smp);
		if (rsp)
			smp_failure(conn, rsp);
	}

	return 0;
}

static u8 smp_cmd_pairing_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing rsp, *req = (void *) skb->data;
	struct hci_dev *hdev = conn->hcon->hdev;
	struct smp_chan *smp;
	u8 key_size, auth, sec_level;
	int ret;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*req))
		return SMP_INVALID_PARAMS;

	if (test_bit(HCI_CONN_MASTER, &conn->hcon->flags))
		return SMP_CMD_NOTSUPP;

	if (!test_and_set_bit(HCI_CONN_LE_SMP_PEND, &conn->hcon->flags))
		smp = smp_chan_create(conn);
	else
		smp = conn->smp_chan;

	if (!smp)
		return SMP_UNSPECIFIED;

	if (!test_bit(HCI_PAIRABLE, &hdev->dev_flags) &&
	    (req->auth_req & SMP_AUTH_BONDING))
		return SMP_PAIRING_NOTSUPP;

	smp->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&smp->preq[1], req, sizeof(*req));
	skb_pull(skb, sizeof(*req));

	/* We didn't start the pairing, so match remote */
	auth = req->auth_req;

	sec_level = authreq_to_seclevel(auth);
	if (sec_level > conn->hcon->pending_sec_level)
		conn->hcon->pending_sec_level = sec_level;

	/* If we need MITM check that it can be acheived */
	if (conn->hcon->pending_sec_level >= BT_SECURITY_HIGH) {
		u8 method;

		method = get_auth_method(smp, conn->hcon->io_capability,
					 req->io_capability);
		if (method == JUST_WORKS || method == JUST_CFM)
			return SMP_AUTH_REQUIREMENTS;
	}

	build_pairing_cmd(conn, req, &rsp, auth);

	key_size = min(req->max_key_size, rsp.max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	get_random_bytes(smp->prnd, sizeof(smp->prnd));

	smp->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&smp->prsp[1], &rsp, sizeof(rsp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_RSP, sizeof(rsp), &rsp);

	/* Request setup of TK */
	ret = tk_request(conn, 0, auth, rsp.io_capability, req->io_capability);
	if (ret)
		return SMP_UNSPECIFIED;

	return 0;
}

static u8 smp_cmd_pairing_rsp(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing *req, *rsp = (void *) skb->data;
	struct smp_chan *smp = conn->smp_chan;
	u8 key_size, auth = SMP_AUTH_NONE;
	int ret;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rsp))
		return SMP_INVALID_PARAMS;

	if (!test_bit(HCI_CONN_MASTER, &conn->hcon->flags))
		return SMP_CMD_NOTSUPP;

	skb_pull(skb, sizeof(*rsp));

	req = (void *) &smp->preq[1];

	key_size = min(req->max_key_size, rsp->max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	/* If we need MITM check that it can be acheived */
	if (conn->hcon->pending_sec_level >= BT_SECURITY_HIGH) {
		u8 method;

		method = get_auth_method(smp, req->io_capability,
					 rsp->io_capability);
		if (method == JUST_WORKS || method == JUST_CFM)
			return SMP_AUTH_REQUIREMENTS;
	}

	get_random_bytes(smp->prnd, sizeof(smp->prnd));

	smp->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&smp->prsp[1], rsp, sizeof(*rsp));

	/* Update remote key distribution in case the remote cleared
	 * some bits that we had enabled in our request.
	 */
	smp->remote_key_dist &= rsp->resp_key_dist;

	if ((req->auth_req & SMP_AUTH_BONDING) &&
	    (rsp->auth_req & SMP_AUTH_BONDING))
		auth = SMP_AUTH_BONDING;

	auth |= (req->auth_req | rsp->auth_req) & SMP_AUTH_MITM;

	ret = tk_request(conn, 0, auth, req->io_capability, rsp->io_capability);
	if (ret)
		return SMP_UNSPECIFIED;

	set_bit(SMP_FLAG_CFM_PENDING, &smp->flags);

	/* Can't compose response until we have been confirmed */
	if (test_bit(SMP_FLAG_TK_VALID, &smp->flags))
		return smp_confirm(smp);

	return 0;
}

static u8 smp_cmd_pairing_confirm(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_chan *smp = conn->smp_chan;

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	if (skb->len < sizeof(smp->pcnf))
		return SMP_INVALID_PARAMS;

	memcpy(smp->pcnf, skb->data, sizeof(smp->pcnf));
	skb_pull(skb, sizeof(smp->pcnf));

	if (conn->hcon->out)
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(smp->prnd),
			     smp->prnd);
	else if (test_bit(SMP_FLAG_TK_VALID, &smp->flags))
		return smp_confirm(smp);
	else
		set_bit(SMP_FLAG_CFM_PENDING, &smp->flags);

	return 0;
}

static u8 smp_cmd_pairing_random(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_chan *smp = conn->smp_chan;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(smp->rrnd))
		return SMP_INVALID_PARAMS;

	memcpy(smp->rrnd, skb->data, sizeof(smp->rrnd));
	skb_pull(skb, sizeof(smp->rrnd));

	return smp_random(smp);
}

static bool smp_ltk_encrypt(struct l2cap_conn *conn, u8 sec_level)
{
	struct smp_ltk *key;
	struct hci_conn *hcon = conn->hcon;

	key = hci_find_ltk_by_addr(hcon->hdev, &hcon->dst, hcon->dst_type,
				   hcon->out);
	if (!key)
		return false;

	if (sec_level > BT_SECURITY_MEDIUM && !key->authenticated)
		return false;

	if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->flags))
		return true;

	hci_le_start_enc(hcon, key->ediv, key->rand, key->val);
	hcon->enc_key_size = key->enc_size;

	/* We never store STKs for master role, so clear this flag */
	clear_bit(HCI_CONN_STK_ENCRYPT, &hcon->flags);

	return true;
}

bool smp_sufficient_security(struct hci_conn *hcon, u8 sec_level)
{
	if (sec_level == BT_SECURITY_LOW)
		return true;

	/* If we're encrypted with an STK always claim insufficient
	 * security. This way we allow the connection to be re-encrypted
	 * with an LTK, even if the LTK provides the same level of
	 * security. Only exception is if we don't have an LTK (e.g.
	 * because of key distribution bits).
	 */
	if (test_bit(HCI_CONN_STK_ENCRYPT, &hcon->flags) &&
	    hci_find_ltk_by_addr(hcon->hdev, &hcon->dst, hcon->dst_type,
				 hcon->out))
		return false;

	if (hcon->sec_level >= sec_level)
		return true;

	return false;
}

static u8 smp_cmd_security_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_security_req *rp = (void *) skb->data;
	struct smp_cmd_pairing cp;
	struct hci_conn *hcon = conn->hcon;
	struct smp_chan *smp;
	u8 sec_level;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	if (!test_bit(HCI_CONN_MASTER, &conn->hcon->flags))
		return SMP_CMD_NOTSUPP;

	sec_level = authreq_to_seclevel(rp->auth_req);
	if (smp_sufficient_security(hcon, sec_level))
		return 0;

	if (sec_level > hcon->pending_sec_level)
		hcon->pending_sec_level = sec_level;

	if (smp_ltk_encrypt(conn, hcon->pending_sec_level))
		return 0;

	if (test_and_set_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags))
		return 0;

	if (!test_bit(HCI_PAIRABLE, &hcon->hdev->dev_flags) &&
	    (rp->auth_req & SMP_AUTH_BONDING))
		return SMP_PAIRING_NOTSUPP;

	smp = smp_chan_create(conn);
	if (!smp)
		return SMP_UNSPECIFIED;

	skb_pull(skb, sizeof(*rp));

	memset(&cp, 0, sizeof(cp));
	build_pairing_cmd(conn, &cp, NULL, rp->auth_req);

	smp->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&smp->preq[1], &cp, sizeof(cp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);

	return 0;
}

int smp_conn_security(struct hci_conn *hcon, __u8 sec_level)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct smp_chan *smp;
	__u8 authreq;

	BT_DBG("conn %p hcon %p level 0x%2.2x", conn, hcon, sec_level);

	/* This may be NULL if there's an unexpected disconnection */
	if (!conn)
		return 1;

	if (!test_bit(HCI_LE_ENABLED, &hcon->hdev->dev_flags))
		return 1;

	if (smp_sufficient_security(hcon, sec_level))
		return 1;

	if (sec_level > hcon->pending_sec_level)
		hcon->pending_sec_level = sec_level;

	if (test_bit(HCI_CONN_MASTER, &hcon->flags))
		if (smp_ltk_encrypt(conn, hcon->pending_sec_level))
			return 0;

	if (test_and_set_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags))
		return 0;

	smp = smp_chan_create(conn);
	if (!smp)
		return 1;

	authreq = seclevel_to_authreq(sec_level);

	/* Require MITM if IO Capability allows or the security level
	 * requires it.
	 */
	if (hcon->io_capability != HCI_IO_NO_INPUT_OUTPUT ||
	    hcon->pending_sec_level > BT_SECURITY_MEDIUM)
		authreq |= SMP_AUTH_MITM;

	if (test_bit(HCI_CONN_MASTER, &hcon->flags)) {
		struct smp_cmd_pairing cp;

		build_pairing_cmd(conn, &cp, NULL, authreq);
		smp->preq[0] = SMP_CMD_PAIRING_REQ;
		memcpy(&smp->preq[1], &cp, sizeof(cp));

		smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);
	} else {
		struct smp_cmd_security_req cp;
		cp.auth_req = authreq;
		smp_send_cmd(conn, SMP_CMD_SECURITY_REQ, sizeof(cp), &cp);
	}

	set_bit(SMP_FLAG_INITIATOR, &smp->flags);

	return 0;
}

static int smp_cmd_encrypt_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_encrypt_info *rp = (void *) skb->data;
	struct smp_chan *smp = conn->smp_chan;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	/* Ignore this PDU if it wasn't requested */
	if (!(smp->remote_key_dist & SMP_DIST_ENC_KEY))
		return 0;

	skb_pull(skb, sizeof(*rp));

	memcpy(smp->tk, rp->ltk, sizeof(smp->tk));

	return 0;
}

static int smp_cmd_master_ident(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_master_ident *rp = (void *) skb->data;
	struct smp_chan *smp = conn->smp_chan;
	struct hci_dev *hdev = conn->hcon->hdev;
	struct hci_conn *hcon = conn->hcon;
	struct smp_ltk *ltk;
	u8 authenticated;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	/* Ignore this PDU if it wasn't requested */
	if (!(smp->remote_key_dist & SMP_DIST_ENC_KEY))
		return 0;

	/* Mark the information as received */
	smp->remote_key_dist &= ~SMP_DIST_ENC_KEY;

	skb_pull(skb, sizeof(*rp));

	hci_dev_lock(hdev);
	authenticated = (hcon->sec_level == BT_SECURITY_HIGH);
	ltk = hci_add_ltk(hdev, &hcon->dst, hcon->dst_type, SMP_LTK,
			  authenticated, smp->tk, smp->enc_key_size,
			  rp->ediv, rp->rand);
	smp->ltk = ltk;
	if (!(smp->remote_key_dist & SMP_DIST_ID_KEY))
		smp_distribute_keys(conn);
	hci_dev_unlock(hdev);

	return 0;
}

static int smp_cmd_ident_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_ident_info *info = (void *) skb->data;
	struct smp_chan *smp = conn->smp_chan;

	BT_DBG("");

	if (skb->len < sizeof(*info))
		return SMP_INVALID_PARAMS;

	/* Ignore this PDU if it wasn't requested */
	if (!(smp->remote_key_dist & SMP_DIST_ID_KEY))
		return 0;

	skb_pull(skb, sizeof(*info));

	memcpy(smp->irk, info->irk, 16);

	return 0;
}

static int smp_cmd_ident_addr_info(struct l2cap_conn *conn,
				   struct sk_buff *skb)
{
	struct smp_cmd_ident_addr_info *info = (void *) skb->data;
	struct smp_chan *smp = conn->smp_chan;
	struct hci_conn *hcon = conn->hcon;
	bdaddr_t rpa;

	BT_DBG("");

	if (skb->len < sizeof(*info))
		return SMP_INVALID_PARAMS;

	/* Ignore this PDU if it wasn't requested */
	if (!(smp->remote_key_dist & SMP_DIST_ID_KEY))
		return 0;

	/* Mark the information as received */
	smp->remote_key_dist &= ~SMP_DIST_ID_KEY;

	skb_pull(skb, sizeof(*info));

	hci_dev_lock(hcon->hdev);

	/* Strictly speaking the Core Specification (4.1) allows sending
	 * an empty address which would force us to rely on just the IRK
	 * as "identity information". However, since such
	 * implementations are not known of and in order to not over
	 * complicate our implementation, simply pretend that we never
	 * received an IRK for such a device.
	 */
	if (!bacmp(&info->bdaddr, BDADDR_ANY)) {
		BT_ERR("Ignoring IRK with no identity address");
		goto distribute;
	}

	bacpy(&smp->id_addr, &info->bdaddr);
	smp->id_addr_type = info->addr_type;

	if (hci_bdaddr_is_rpa(&hcon->dst, hcon->dst_type))
		bacpy(&rpa, &hcon->dst);
	else
		bacpy(&rpa, BDADDR_ANY);

	smp->remote_irk = hci_add_irk(conn->hcon->hdev, &smp->id_addr,
				      smp->id_addr_type, smp->irk, &rpa);

distribute:
	smp_distribute_keys(conn);

	hci_dev_unlock(hcon->hdev);

	return 0;
}

static int smp_cmd_sign_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_sign_info *rp = (void *) skb->data;
	struct smp_chan *smp = conn->smp_chan;
	struct hci_dev *hdev = conn->hcon->hdev;
	struct smp_csrk *csrk;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	/* Ignore this PDU if it wasn't requested */
	if (!(smp->remote_key_dist & SMP_DIST_SIGN))
		return 0;

	/* Mark the information as received */
	smp->remote_key_dist &= ~SMP_DIST_SIGN;

	skb_pull(skb, sizeof(*rp));

	hci_dev_lock(hdev);
	csrk = kzalloc(sizeof(*csrk), GFP_KERNEL);
	if (csrk) {
		csrk->master = 0x01;
		memcpy(csrk->val, rp->csrk, sizeof(csrk->val));
	}
	smp->csrk = csrk;
	if (!(smp->remote_key_dist & SMP_DIST_SIGN))
		smp_distribute_keys(conn);
	hci_dev_unlock(hdev);

	return 0;
}

int smp_sig_channel(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct hci_conn *hcon = conn->hcon;
	__u8 code, reason;
	int err = 0;

	if (hcon->type != LE_LINK) {
		kfree_skb(skb);
		return 0;
	}

	if (skb->len < 1) {
		kfree_skb(skb);
		return -EILSEQ;
	}

	if (!test_bit(HCI_LE_ENABLED, &hcon->hdev->dev_flags)) {
		err = -ENOTSUPP;
		reason = SMP_PAIRING_NOTSUPP;
		goto done;
	}

	code = skb->data[0];
	skb_pull(skb, sizeof(code));

	/*
	 * The SMP context must be initialized for all other PDUs except
	 * pairing and security requests. If we get any other PDU when
	 * not initialized simply disconnect (done if this function
	 * returns an error).
	 */
	if (code != SMP_CMD_PAIRING_REQ && code != SMP_CMD_SECURITY_REQ &&
	    !conn->smp_chan) {
		BT_ERR("Unexpected SMP command 0x%02x. Disconnecting.", code);
		kfree_skb(skb);
		return -ENOTSUPP;
	}

	switch (code) {
	case SMP_CMD_PAIRING_REQ:
		reason = smp_cmd_pairing_req(conn, skb);
		break;

	case SMP_CMD_PAIRING_FAIL:
		smp_failure(conn, 0);
		reason = 0;
		err = -EPERM;
		break;

	case SMP_CMD_PAIRING_RSP:
		reason = smp_cmd_pairing_rsp(conn, skb);
		break;

	case SMP_CMD_SECURITY_REQ:
		reason = smp_cmd_security_req(conn, skb);
		break;

	case SMP_CMD_PAIRING_CONFIRM:
		reason = smp_cmd_pairing_confirm(conn, skb);
		break;

	case SMP_CMD_PAIRING_RANDOM:
		reason = smp_cmd_pairing_random(conn, skb);
		break;

	case SMP_CMD_ENCRYPT_INFO:
		reason = smp_cmd_encrypt_info(conn, skb);
		break;

	case SMP_CMD_MASTER_IDENT:
		reason = smp_cmd_master_ident(conn, skb);
		break;

	case SMP_CMD_IDENT_INFO:
		reason = smp_cmd_ident_info(conn, skb);
		break;

	case SMP_CMD_IDENT_ADDR_INFO:
		reason = smp_cmd_ident_addr_info(conn, skb);
		break;

	case SMP_CMD_SIGN_INFO:
		reason = smp_cmd_sign_info(conn, skb);
		break;

	default:
		BT_DBG("Unknown command code 0x%2.2x", code);

		reason = SMP_CMD_NOTSUPP;
		err = -EOPNOTSUPP;
		goto done;
	}

done:
	if (reason)
		smp_failure(conn, reason);

	kfree_skb(skb);
	return err;
}

static void smp_notify_keys(struct l2cap_conn *conn)
{
	struct smp_chan *smp = conn->smp_chan;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	struct smp_cmd_pairing *req = (void *) &smp->preq[1];
	struct smp_cmd_pairing *rsp = (void *) &smp->prsp[1];
	bool persistent;

	if (smp->remote_irk) {
		mgmt_new_irk(hdev, smp->remote_irk);
		/* Now that user space can be considered to know the
		 * identity address track the connection based on it
		 * from now on.
		 */
		bacpy(&hcon->dst, &smp->remote_irk->bdaddr);
		hcon->dst_type = smp->remote_irk->addr_type;
		l2cap_conn_update_id_addr(hcon);
	}

	/* The LTKs and CSRKs should be persistent only if both sides
	 * had the bonding bit set in their authentication requests.
	 */
	persistent = !!((req->auth_req & rsp->auth_req) & SMP_AUTH_BONDING);

	if (smp->csrk) {
		smp->csrk->bdaddr_type = hcon->dst_type;
		bacpy(&smp->csrk->bdaddr, &hcon->dst);
		mgmt_new_csrk(hdev, smp->csrk, persistent);
	}

	if (smp->slave_csrk) {
		smp->slave_csrk->bdaddr_type = hcon->dst_type;
		bacpy(&smp->slave_csrk->bdaddr, &hcon->dst);
		mgmt_new_csrk(hdev, smp->slave_csrk, persistent);
	}

	if (smp->ltk) {
		smp->ltk->bdaddr_type = hcon->dst_type;
		bacpy(&smp->ltk->bdaddr, &hcon->dst);
		mgmt_new_ltk(hdev, smp->ltk, persistent);
	}

	if (smp->slave_ltk) {
		smp->slave_ltk->bdaddr_type = hcon->dst_type;
		bacpy(&smp->slave_ltk->bdaddr, &hcon->dst);
		mgmt_new_ltk(hdev, smp->slave_ltk, persistent);
	}
}

int smp_distribute_keys(struct l2cap_conn *conn)
{
	struct smp_cmd_pairing *req, *rsp;
	struct smp_chan *smp = conn->smp_chan;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	__u8 *keydist;

	BT_DBG("conn %p", conn);

	if (!test_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags))
		return 0;

	rsp = (void *) &smp->prsp[1];

	/* The responder sends its keys first */
	if (hcon->out && (smp->remote_key_dist & 0x07))
		return 0;

	req = (void *) &smp->preq[1];

	if (hcon->out) {
		keydist = &rsp->init_key_dist;
		*keydist &= req->init_key_dist;
	} else {
		keydist = &rsp->resp_key_dist;
		*keydist &= req->resp_key_dist;
	}

	BT_DBG("keydist 0x%x", *keydist);

	if (*keydist & SMP_DIST_ENC_KEY) {
		struct smp_cmd_encrypt_info enc;
		struct smp_cmd_master_ident ident;
		struct smp_ltk *ltk;
		u8 authenticated;
		__le16 ediv;
		__le64 rand;

		get_random_bytes(enc.ltk, sizeof(enc.ltk));
		get_random_bytes(&ediv, sizeof(ediv));
		get_random_bytes(&rand, sizeof(rand));

		smp_send_cmd(conn, SMP_CMD_ENCRYPT_INFO, sizeof(enc), &enc);

		authenticated = hcon->sec_level == BT_SECURITY_HIGH;
		ltk = hci_add_ltk(hdev, &hcon->dst, hcon->dst_type,
				  SMP_LTK_SLAVE, authenticated, enc.ltk,
				  smp->enc_key_size, ediv, rand);
		smp->slave_ltk = ltk;

		ident.ediv = ediv;
		ident.rand = rand;

		smp_send_cmd(conn, SMP_CMD_MASTER_IDENT, sizeof(ident), &ident);

		*keydist &= ~SMP_DIST_ENC_KEY;
	}

	if (*keydist & SMP_DIST_ID_KEY) {
		struct smp_cmd_ident_addr_info addrinfo;
		struct smp_cmd_ident_info idinfo;

		memcpy(idinfo.irk, hdev->irk, sizeof(idinfo.irk));

		smp_send_cmd(conn, SMP_CMD_IDENT_INFO, sizeof(idinfo), &idinfo);

		/* The hci_conn contains the local identity address
		 * after the connection has been established.
		 *
		 * This is true even when the connection has been
		 * established using a resolvable random address.
		 */
		bacpy(&addrinfo.bdaddr, &hcon->src);
		addrinfo.addr_type = hcon->src_type;

		smp_send_cmd(conn, SMP_CMD_IDENT_ADDR_INFO, sizeof(addrinfo),
			     &addrinfo);

		*keydist &= ~SMP_DIST_ID_KEY;
	}

	if (*keydist & SMP_DIST_SIGN) {
		struct smp_cmd_sign_info sign;
		struct smp_csrk *csrk;

		/* Generate a new random key */
		get_random_bytes(sign.csrk, sizeof(sign.csrk));

		csrk = kzalloc(sizeof(*csrk), GFP_KERNEL);
		if (csrk) {
			csrk->master = 0x00;
			memcpy(csrk->val, sign.csrk, sizeof(csrk->val));
		}
		smp->slave_csrk = csrk;

		smp_send_cmd(conn, SMP_CMD_SIGN_INFO, sizeof(sign), &sign);

		*keydist &= ~SMP_DIST_SIGN;
	}

	/* If there are still keys to be received wait for them */
	if ((smp->remote_key_dist & 0x07))
		return 0;

	clear_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags);
	cancel_delayed_work_sync(&conn->security_timer);
	set_bit(SMP_FLAG_COMPLETE, &smp->flags);
	smp_notify_keys(conn);

	smp_chan_destroy(conn);

	return 0;
}
