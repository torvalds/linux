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
#include <net/bluetooth/smp.h>

#define SMP_TIMEOUT	msecs_to_jiffies(30000)

static inline void swap128(u8 src[16], u8 dst[16])
{
	int i;
	for (i = 0; i < 16; i++)
		dst[15 - i] = src[i];
}

static inline void swap56(u8 src[7], u8 dst[7])
{
	int i;
	for (i = 0; i < 7; i++)
		dst[6 - i] = src[i];
}

static int smp_e(struct crypto_blkcipher *tfm, const u8 *k, u8 *r)
{
	struct blkcipher_desc desc;
	struct scatterlist sg;
	int err, iv_len;
	unsigned char iv[128];

	if (tfm == NULL) {
		BT_ERR("tfm %p", tfm);
		return -EINVAL;
	}

	desc.tfm = tfm;
	desc.flags = 0;

	err = crypto_blkcipher_setkey(tfm, k, 16);
	if (err) {
		BT_ERR("cipher setkey failed: %d", err);
		return err;
	}

	sg_init_one(&sg, r, 16);

	iv_len = crypto_blkcipher_ivsize(tfm);
	if (iv_len) {
		memset(&iv, 0xff, iv_len);
		crypto_blkcipher_set_iv(tfm, iv, iv_len);
	}

	err = crypto_blkcipher_encrypt(&desc, &sg, &sg, 16);
	if (err)
		BT_ERR("Encrypt data error %d", err);

	return err;
}

static int smp_c1(struct crypto_blkcipher *tfm, u8 k[16], u8 r[16],
		u8 preq[7], u8 pres[7], u8 _iat, bdaddr_t *ia,
		u8 _rat, bdaddr_t *ra, u8 res[16])
{
	u8 p1[16], p2[16];
	int err;

	memset(p1, 0, 16);

	/* p1 = pres || preq || _rat || _iat */
	swap56(pres, p1);
	swap56(preq, p1 + 7);
	p1[14] = _rat;
	p1[15] = _iat;

	memset(p2, 0, 16);

	/* p2 = padding || ia || ra */
	baswap((bdaddr_t *) (p2 + 4), ia);
	baswap((bdaddr_t *) (p2 + 10), ra);

	/* res = r XOR p1 */
	u128_xor((u128 *) res, (u128 *) r, (u128 *) p1);

	/* res = e(k, res) */
	err = smp_e(tfm, k, res);
	if (err) {
		BT_ERR("Encrypt data error");
		return err;
	}

	/* res = res XOR p2 */
	u128_xor((u128 *) res, (u128 *) res, (u128 *) p2);

	/* res = e(k, res) */
	err = smp_e(tfm, k, res);
	if (err)
		BT_ERR("Encrypt data error");

	return err;
}

static int smp_s1(struct crypto_blkcipher *tfm, u8 k[16],
			u8 r1[16], u8 r2[16], u8 _r[16])
{
	int err;

	/* Just least significant octets from r1 and r2 are considered */
	memcpy(_r, r1 + 8, 8);
	memcpy(_r + 8, r2 + 8, 8);

	err = smp_e(tfm, k, _r);
	if (err)
		BT_ERR("Encrypt data error");

	return err;
}

static int smp_rand(u8 *buf)
{
	get_random_bytes(buf, 16);

	return 0;
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
				struct smp_cmd_pairing *rsp,
				__u8 authreq)
{
	u8 dist_keys = 0;

	if (test_bit(HCI_PAIRABLE, &conn->hcon->hdev->dev_flags)) {
		dist_keys = SMP_DIST_ENC_KEY;
		authreq |= SMP_AUTH_BONDING;
	} else {
		authreq &= ~SMP_AUTH_BONDING;
	}

	if (rsp == NULL) {
		req->io_capability = conn->hcon->io_capability;
		req->oob_flag = SMP_OOB_NOT_PRESENT;
		req->max_key_size = SMP_MAX_ENC_KEY_SIZE;
		req->init_key_dist = 0;
		req->resp_key_dist = dist_keys;
		req->auth_req = authreq;
		return;
	}

	rsp->io_capability = conn->hcon->io_capability;
	rsp->oob_flag = SMP_OOB_NOT_PRESENT;
	rsp->max_key_size = SMP_MAX_ENC_KEY_SIZE;
	rsp->init_key_dist = 0;
	rsp->resp_key_dist = req->resp_key_dist & dist_keys;
	rsp->auth_req = authreq;
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

static void smp_failure(struct l2cap_conn *conn, u8 reason, u8 send)
{
	struct hci_conn *hcon = conn->hcon;

	if (send)
		smp_send_cmd(conn, SMP_CMD_PAIRING_FAIL, sizeof(reason),
								&reason);

	clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->hcon->flags);
	mgmt_auth_failed(conn->hcon->hdev, conn->dst, hcon->type,
			 hcon->dst_type, reason);

	if (test_and_clear_bit(HCI_CONN_LE_SMP_PEND, &conn->hcon->flags)) {
		cancel_delayed_work_sync(&conn->security_timer);
		smp_chan_destroy(conn);
	}
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
	clear_bit(SMP_FLAG_TK_VALID, &smp->smp_flags);

	BT_DBG("tk_request: auth:%d lcl:%d rem:%d", auth, local_io, remote_io);

	/* If neither side wants MITM, use JUST WORKS */
	/* If either side has unknown io_caps, use JUST WORKS */
	/* Otherwise, look up method from the table */
	if (!(auth & SMP_AUTH_MITM) ||
			local_io > SMP_IO_KEYBOARD_DISPLAY ||
			remote_io > SMP_IO_KEYBOARD_DISPLAY)
		method = JUST_WORKS;
	else
		method = gen_method[remote_io][local_io];

	/* If not bonding, don't ask user to confirm a Zero TK */
	if (!(auth & SMP_AUTH_BONDING) && method == JUST_CFM)
		method = JUST_WORKS;

	/* If Just Works, Continue with Zero TK */
	if (method == JUST_WORKS) {
		set_bit(SMP_FLAG_TK_VALID, &smp->smp_flags);
		return 0;
	}

	/* Not Just Works/Confirm results in MITM Authentication */
	if (method != JUST_CFM)
		set_bit(SMP_FLAG_MITM_AUTH, &smp->smp_flags);

	/* If both devices have Keyoard-Display I/O, the master
	 * Confirms and the slave Enters the passkey.
	 */
	if (method == OVERLAP) {
		if (hcon->link_mode & HCI_LM_MASTER)
			method = CFM_PASSKEY;
		else
			method = REQ_PASSKEY;
	}

	/* Generate random passkey. Not valid until confirmed. */
	if (method == CFM_PASSKEY) {
		u8 key[16];

		memset(key, 0, sizeof(key));
		get_random_bytes(&passkey, sizeof(passkey));
		passkey %= 1000000;
		put_unaligned_le32(passkey, key);
		swap128(key, smp->tk);
		BT_DBG("PassKey: %d", passkey);
	}

	hci_dev_lock(hcon->hdev);

	if (method == REQ_PASSKEY)
		ret = mgmt_user_passkey_request(hcon->hdev, conn->dst,
						hcon->type, hcon->dst_type);
	else
		ret = mgmt_user_confirm_request(hcon->hdev, conn->dst,
						hcon->type, hcon->dst_type,
						cpu_to_le32(passkey), 0);

	hci_dev_unlock(hcon->hdev);

	return ret;
}

static void confirm_work(struct work_struct *work)
{
	struct smp_chan *smp = container_of(work, struct smp_chan, confirm);
	struct l2cap_conn *conn = smp->conn;
	struct crypto_blkcipher *tfm;
	struct smp_cmd_pairing_confirm cp;
	int ret;
	u8 res[16], reason;

	BT_DBG("conn %p", conn);

	tfm = crypto_alloc_blkcipher("ecb(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		reason = SMP_UNSPECIFIED;
		goto error;
	}

	smp->tfm = tfm;

	if (conn->hcon->out)
		ret = smp_c1(tfm, smp->tk, smp->prnd, smp->preq, smp->prsp, 0,
			     conn->src, conn->hcon->dst_type, conn->dst, res);
	else
		ret = smp_c1(tfm, smp->tk, smp->prnd, smp->preq, smp->prsp,
			     conn->hcon->dst_type, conn->dst, 0, conn->src,
			     res);
	if (ret) {
		reason = SMP_UNSPECIFIED;
		goto error;
	}

	clear_bit(SMP_FLAG_CFM_PENDING, &smp->smp_flags);

	swap128(res, cp.confirm_val);
	smp_send_cmd(smp->conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cp), &cp);

	return;

error:
	smp_failure(conn, reason, 1);
}

static void random_work(struct work_struct *work)
{
	struct smp_chan *smp = container_of(work, struct smp_chan, random);
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	struct crypto_blkcipher *tfm = smp->tfm;
	u8 reason, confirm[16], res[16], key[16];
	int ret;

	if (IS_ERR_OR_NULL(tfm)) {
		reason = SMP_UNSPECIFIED;
		goto error;
	}

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	if (hcon->out)
		ret = smp_c1(tfm, smp->tk, smp->rrnd, smp->preq, smp->prsp, 0,
			     conn->src, hcon->dst_type, conn->dst, res);
	else
		ret = smp_c1(tfm, smp->tk, smp->rrnd, smp->preq, smp->prsp,
			     hcon->dst_type, conn->dst, 0, conn->src, res);
	if (ret) {
		reason = SMP_UNSPECIFIED;
		goto error;
	}

	swap128(res, confirm);

	if (memcmp(smp->pcnf, confirm, sizeof(smp->pcnf)) != 0) {
		BT_ERR("Pairing failed (confirmation values mismatch)");
		reason = SMP_CONFIRM_FAILED;
		goto error;
	}

	if (hcon->out) {
		u8 stk[16], rand[8];
		__le16 ediv;

		memset(rand, 0, sizeof(rand));
		ediv = 0;

		smp_s1(tfm, smp->tk, smp->rrnd, smp->prnd, key);
		swap128(key, stk);

		memset(stk + smp->enc_key_size, 0,
		       SMP_MAX_ENC_KEY_SIZE - smp->enc_key_size);

		if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->flags)) {
			reason = SMP_UNSPECIFIED;
			goto error;
		}

		hci_le_start_enc(hcon, ediv, rand, stk);
		hcon->enc_key_size = smp->enc_key_size;
	} else {
		u8 stk[16], r[16], rand[8];
		__le16 ediv;

		memset(rand, 0, sizeof(rand));
		ediv = 0;

		swap128(smp->prnd, r);
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(r), r);

		smp_s1(tfm, smp->tk, smp->prnd, smp->rrnd, key);
		swap128(key, stk);

		memset(stk + smp->enc_key_size, 0,
				SMP_MAX_ENC_KEY_SIZE - smp->enc_key_size);

		hci_add_ltk(hcon->hdev, conn->dst, hcon->dst_type,
			    HCI_SMP_STK_SLAVE, 0, 0, stk, smp->enc_key_size,
			    ediv, rand);
	}

	return;

error:
	smp_failure(conn, reason, 1);
}

static struct smp_chan *smp_chan_create(struct l2cap_conn *conn)
{
	struct smp_chan *smp;

	smp = kzalloc(sizeof(struct smp_chan), GFP_ATOMIC);
	if (!smp)
		return NULL;

	INIT_WORK(&smp->confirm, confirm_work);
	INIT_WORK(&smp->random, random_work);

	smp->conn = conn;
	conn->smp_chan = smp;
	conn->hcon->smp_conn = conn;

	hci_conn_hold(conn->hcon);

	return smp;
}

void smp_chan_destroy(struct l2cap_conn *conn)
{
	struct smp_chan *smp = conn->smp_chan;

	BUG_ON(!smp);

	if (smp->tfm)
		crypto_free_blkcipher(smp->tfm);

	kfree(smp);
	conn->smp_chan = NULL;
	conn->hcon->smp_conn = NULL;
	hci_conn_put(conn->hcon);
}

int smp_user_confirm_reply(struct hci_conn *hcon, u16 mgmt_op, __le32 passkey)
{
	struct l2cap_conn *conn = hcon->smp_conn;
	struct smp_chan *smp;
	u32 value;
	u8 key[16];

	BT_DBG("");

	if (!conn)
		return -ENOTCONN;

	smp = conn->smp_chan;

	switch (mgmt_op) {
	case MGMT_OP_USER_PASSKEY_REPLY:
		value = le32_to_cpu(passkey);
		memset(key, 0, sizeof(key));
		BT_DBG("PassKey: %d", value);
		put_unaligned_le32(value, key);
		swap128(key, smp->tk);
		/* Fall Through */
	case MGMT_OP_USER_CONFIRM_REPLY:
		set_bit(SMP_FLAG_TK_VALID, &smp->smp_flags);
		break;
	case MGMT_OP_USER_PASSKEY_NEG_REPLY:
	case MGMT_OP_USER_CONFIRM_NEG_REPLY:
		smp_failure(conn, SMP_PASSKEY_ENTRY_FAILED, 1);
		return 0;
	default:
		smp_failure(conn, SMP_PASSKEY_ENTRY_FAILED, 1);
		return -EOPNOTSUPP;
	}

	/* If it is our turn to send Pairing Confirm, do so now */
	if (test_bit(SMP_FLAG_CFM_PENDING, &smp->smp_flags))
		queue_work(hcon->hdev->workqueue, &smp->confirm);

	return 0;
}

static u8 smp_cmd_pairing_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing rsp, *req = (void *) skb->data;
	struct smp_chan *smp;
	u8 key_size;
	u8 auth = SMP_AUTH_NONE;
	int ret;

	BT_DBG("conn %p", conn);

	if (conn->hcon->link_mode & HCI_LM_MASTER)
		return SMP_CMD_NOTSUPP;

	if (!test_and_set_bit(HCI_CONN_LE_SMP_PEND, &conn->hcon->flags))
		smp = smp_chan_create(conn);

	smp = conn->smp_chan;

	smp->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&smp->preq[1], req, sizeof(*req));
	skb_pull(skb, sizeof(*req));

	/* We didn't start the pairing, so match remote */
	if (req->auth_req & SMP_AUTH_BONDING)
		auth = req->auth_req;

	conn->hcon->pending_sec_level = authreq_to_seclevel(auth);

	build_pairing_cmd(conn, req, &rsp, auth);

	key_size = min(req->max_key_size, rsp.max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	ret = smp_rand(smp->prnd);
	if (ret)
		return SMP_UNSPECIFIED;

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
	struct hci_dev *hdev = conn->hcon->hdev;
	u8 key_size, auth = SMP_AUTH_NONE;
	int ret;

	BT_DBG("conn %p", conn);

	if (!(conn->hcon->link_mode & HCI_LM_MASTER))
		return SMP_CMD_NOTSUPP;

	skb_pull(skb, sizeof(*rsp));

	req = (void *) &smp->preq[1];

	key_size = min(req->max_key_size, rsp->max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	ret = smp_rand(smp->prnd);
	if (ret)
		return SMP_UNSPECIFIED;

	smp->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&smp->prsp[1], rsp, sizeof(*rsp));

	if ((req->auth_req & SMP_AUTH_BONDING) &&
			(rsp->auth_req & SMP_AUTH_BONDING))
		auth = SMP_AUTH_BONDING;

	auth |= (req->auth_req | rsp->auth_req) & SMP_AUTH_MITM;

	ret = tk_request(conn, 0, auth, rsp->io_capability, req->io_capability);
	if (ret)
		return SMP_UNSPECIFIED;

	set_bit(SMP_FLAG_CFM_PENDING, &smp->smp_flags);

	/* Can't compose response until we have been confirmed */
	if (!test_bit(SMP_FLAG_TK_VALID, &smp->smp_flags))
		return 0;

	queue_work(hdev->workqueue, &smp->confirm);

	return 0;
}

static u8 smp_cmd_pairing_confirm(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_chan *smp = conn->smp_chan;
	struct hci_dev *hdev = conn->hcon->hdev;

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	memcpy(smp->pcnf, skb->data, sizeof(smp->pcnf));
	skb_pull(skb, sizeof(smp->pcnf));

	if (conn->hcon->out) {
		u8 random[16];

		swap128(smp->prnd, random);
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(random),
								random);
	} else if (test_bit(SMP_FLAG_TK_VALID, &smp->smp_flags)) {
		queue_work(hdev->workqueue, &smp->confirm);
	} else {
		set_bit(SMP_FLAG_CFM_PENDING, &smp->smp_flags);
	}

	return 0;
}

static u8 smp_cmd_pairing_random(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_chan *smp = conn->smp_chan;
	struct hci_dev *hdev = conn->hcon->hdev;

	BT_DBG("conn %p", conn);

	swap128(skb->data, smp->rrnd);
	skb_pull(skb, sizeof(smp->rrnd));

	queue_work(hdev->workqueue, &smp->random);

	return 0;
}

static u8 smp_ltk_encrypt(struct l2cap_conn *conn)
{
	struct smp_ltk *key;
	struct hci_conn *hcon = conn->hcon;

	key = hci_find_ltk_by_addr(hcon->hdev, conn->dst, hcon->dst_type);
	if (!key)
		return 0;

	if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->flags))
		return 1;

	hci_le_start_enc(hcon, key->ediv, key->rand, key->val);
	hcon->enc_key_size = key->enc_size;

	return 1;

}
static u8 smp_cmd_security_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_security_req *rp = (void *) skb->data;
	struct smp_cmd_pairing cp;
	struct hci_conn *hcon = conn->hcon;
	struct smp_chan *smp;

	BT_DBG("conn %p", conn);

	hcon->pending_sec_level = authreq_to_seclevel(rp->auth_req);

	if (smp_ltk_encrypt(conn))
		return 0;

	if (test_and_set_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags))
		return 0;

	smp = smp_chan_create(conn);

	skb_pull(skb, sizeof(*rp));

	memset(&cp, 0, sizeof(cp));
	build_pairing_cmd(conn, &cp, NULL, rp->auth_req);

	smp->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&smp->preq[1], &cp, sizeof(cp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);

	return 0;
}

int smp_conn_security(struct l2cap_conn *conn, __u8 sec_level)
{
	struct hci_conn *hcon = conn->hcon;
	struct smp_chan *smp = conn->smp_chan;
	__u8 authreq;

	BT_DBG("conn %p hcon %p level 0x%2.2x", conn, hcon, sec_level);

	if (!lmp_host_le_capable(hcon->hdev))
		return 1;

	if (sec_level == BT_SECURITY_LOW)
		return 1;

	if (hcon->sec_level >= sec_level)
		return 1;

	if (hcon->link_mode & HCI_LM_MASTER)
		if (smp_ltk_encrypt(conn))
			goto done;

	if (test_and_set_bit(HCI_CONN_LE_SMP_PEND, &hcon->flags))
		return 0;

	smp = smp_chan_create(conn);
	if (!smp)
		return 1;

	authreq = seclevel_to_authreq(sec_level);

	if (hcon->link_mode & HCI_LM_MASTER) {
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

done:
	hcon->pending_sec_level = sec_level;

	return 0;
}

static int smp_cmd_encrypt_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_encrypt_info *rp = (void *) skb->data;
	struct smp_chan *smp = conn->smp_chan;

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
	u8 authenticated;

	skb_pull(skb, sizeof(*rp));

	hci_dev_lock(hdev);
	authenticated = (conn->hcon->sec_level == BT_SECURITY_HIGH);
	hci_add_ltk(conn->hcon->hdev, conn->dst, hcon->dst_type,
		    HCI_SMP_LTK, 1, authenticated, smp->tk, smp->enc_key_size,
		    rp->ediv, rp->rand);
	smp_distribute_keys(conn, 1);
	hci_dev_unlock(hdev);

	return 0;
}

int smp_sig_channel(struct l2cap_conn *conn, struct sk_buff *skb)
{
	__u8 code = skb->data[0];
	__u8 reason;
	int err = 0;

	if (!lmp_host_le_capable(conn->hcon->hdev)) {
		err = -ENOTSUPP;
		reason = SMP_PAIRING_NOTSUPP;
		goto done;
	}

	skb_pull(skb, sizeof(code));

	switch (code) {
	case SMP_CMD_PAIRING_REQ:
		reason = smp_cmd_pairing_req(conn, skb);
		break;

	case SMP_CMD_PAIRING_FAIL:
		smp_failure(conn, skb->data[0], 0);
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
	case SMP_CMD_IDENT_ADDR_INFO:
	case SMP_CMD_SIGN_INFO:
		/* Just ignored */
		reason = 0;
		break;

	default:
		BT_DBG("Unknown command code 0x%2.2x", code);

		reason = SMP_CMD_NOTSUPP;
		err = -EOPNOTSUPP;
		goto done;
	}

done:
	if (reason)
		smp_failure(conn, reason, 1);

	kfree_skb(skb);
	return err;
}

int smp_distribute_keys(struct l2cap_conn *conn, __u8 force)
{
	struct smp_cmd_pairing *req, *rsp;
	struct smp_chan *smp = conn->smp_chan;
	__u8 *keydist;

	BT_DBG("conn %p force %d", conn, force);

	if (!test_bit(HCI_CONN_LE_SMP_PEND, &conn->hcon->flags))
		return 0;

	rsp = (void *) &smp->prsp[1];

	/* The responder sends its keys first */
	if (!force && conn->hcon->out && (rsp->resp_key_dist & 0x07))
		return 0;

	req = (void *) &smp->preq[1];

	if (conn->hcon->out) {
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
		struct hci_conn *hcon = conn->hcon;
		u8 authenticated;
		__le16 ediv;

		get_random_bytes(enc.ltk, sizeof(enc.ltk));
		get_random_bytes(&ediv, sizeof(ediv));
		get_random_bytes(ident.rand, sizeof(ident.rand));

		smp_send_cmd(conn, SMP_CMD_ENCRYPT_INFO, sizeof(enc), &enc);

		authenticated = hcon->sec_level == BT_SECURITY_HIGH;
		hci_add_ltk(conn->hcon->hdev, conn->dst, hcon->dst_type,
			    HCI_SMP_LTK_SLAVE, 1, authenticated,
			    enc.ltk, smp->enc_key_size, ediv, ident.rand);

		ident.ediv = ediv;

		smp_send_cmd(conn, SMP_CMD_MASTER_IDENT, sizeof(ident), &ident);

		*keydist &= ~SMP_DIST_ENC_KEY;
	}

	if (*keydist & SMP_DIST_ID_KEY) {
		struct smp_cmd_ident_addr_info addrinfo;
		struct smp_cmd_ident_info idinfo;

		/* Send a dummy key */
		get_random_bytes(idinfo.irk, sizeof(idinfo.irk));

		smp_send_cmd(conn, SMP_CMD_IDENT_INFO, sizeof(idinfo), &idinfo);

		/* Just public address */
		memset(&addrinfo, 0, sizeof(addrinfo));
		bacpy(&addrinfo.bdaddr, conn->src);

		smp_send_cmd(conn, SMP_CMD_IDENT_ADDR_INFO, sizeof(addrinfo),
								&addrinfo);

		*keydist &= ~SMP_DIST_ID_KEY;
	}

	if (*keydist & SMP_DIST_SIGN) {
		struct smp_cmd_sign_info sign;

		/* Send a dummy key */
		get_random_bytes(sign.csrk, sizeof(sign.csrk));

		smp_send_cmd(conn, SMP_CMD_SIGN_INFO, sizeof(sign), &sign);

		*keydist &= ~SMP_DIST_SIGN;
	}

	if (conn->hcon->out || force) {
		clear_bit(HCI_CONN_LE_SMP_PEND, &conn->hcon->flags);
		cancel_delayed_work_sync(&conn->security_timer);
		smp_chan_destroy(conn);
	}

	return 0;
}
