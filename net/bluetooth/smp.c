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

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/smp.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/b128ops.h>

#define SMP_TIMEOUT 30000 /* 30 seconds */

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

	hci_send_acl(conn->hcon, skb, 0);
}

static __u8 seclevel_to_authreq(__u8 level)
{
	switch (level) {
	case BT_SECURITY_HIGH:
		/* Right now we don't support bonding */
		return SMP_AUTH_MITM;

	default:
		return SMP_AUTH_NONE;
	}
}

static void build_pairing_cmd(struct l2cap_conn *conn,
				struct smp_cmd_pairing *req,
				struct smp_cmd_pairing *rsp,
				__u8 authreq)
{
	u8 dist_keys;

	dist_keys = 0;
	if (test_bit(HCI_PAIRABLE, &conn->hcon->hdev->flags)) {
		dist_keys = SMP_DIST_ENC_KEY | SMP_DIST_ID_KEY | SMP_DIST_SIGN;
		authreq |= SMP_AUTH_BONDING;
	}

	if (rsp == NULL) {
		req->io_capability = conn->hcon->io_capability;
		req->oob_flag = SMP_OOB_NOT_PRESENT;
		req->max_key_size = SMP_MAX_ENC_KEY_SIZE;
		req->init_key_dist = dist_keys;
		req->resp_key_dist = dist_keys;
		req->auth_req = authreq;
		return;
	}

	rsp->io_capability = conn->hcon->io_capability;
	rsp->oob_flag = SMP_OOB_NOT_PRESENT;
	rsp->max_key_size = SMP_MAX_ENC_KEY_SIZE;
	rsp->init_key_dist = req->init_key_dist & dist_keys;
	rsp->resp_key_dist = req->resp_key_dist & dist_keys;
	rsp->auth_req = authreq;
}

static u8 check_enc_key_size(struct l2cap_conn *conn, __u8 max_key_size)
{
	if ((max_key_size > SMP_MAX_ENC_KEY_SIZE) ||
			(max_key_size < SMP_MIN_ENC_KEY_SIZE))
		return SMP_ENC_KEY_SIZE;

	conn->smp_key_size = max_key_size;

	return 0;
}

static u8 smp_cmd_pairing_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing rsp, *req = (void *) skb->data;
	u8 key_size;

	BT_DBG("conn %p", conn);

	conn->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&conn->preq[1], req, sizeof(*req));
	skb_pull(skb, sizeof(*req));

	if (req->oob_flag)
		return SMP_OOB_NOT_AVAIL;

	/* We didn't start the pairing, so no requirements */
	build_pairing_cmd(conn, req, &rsp, SMP_AUTH_NONE);

	key_size = min(req->max_key_size, rsp.max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	/* Just works */
	memset(conn->tk, 0, sizeof(conn->tk));

	conn->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&conn->prsp[1], &rsp, sizeof(rsp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_RSP, sizeof(rsp), &rsp);

	mod_timer(&conn->security_timer, jiffies +
					msecs_to_jiffies(SMP_TIMEOUT));

	return 0;
}

static u8 smp_cmd_pairing_rsp(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing *req, *rsp = (void *) skb->data;
	struct smp_cmd_pairing_confirm cp;
	struct crypto_blkcipher *tfm = conn->hcon->hdev->tfm;
	int ret;
	u8 res[16], key_size;

	BT_DBG("conn %p", conn);

	skb_pull(skb, sizeof(*rsp));

	req = (void *) &conn->preq[1];

	key_size = min(req->max_key_size, rsp->max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	if (rsp->oob_flag)
		return SMP_OOB_NOT_AVAIL;

	/* Just works */
	memset(conn->tk, 0, sizeof(conn->tk));

	conn->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&conn->prsp[1], rsp, sizeof(*rsp));

	ret = smp_rand(conn->prnd);
	if (ret)
		return SMP_UNSPECIFIED;

	ret = smp_c1(tfm, conn->tk, conn->prnd, conn->preq, conn->prsp, 0,
			conn->src, conn->hcon->dst_type, conn->dst, res);
	if (ret)
		return SMP_UNSPECIFIED;

	swap128(res, cp.confirm_val);

	smp_send_cmd(conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cp), &cp);

	return 0;
}

static u8 smp_cmd_pairing_confirm(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct crypto_blkcipher *tfm = conn->hcon->hdev->tfm;

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	memcpy(conn->pcnf, skb->data, sizeof(conn->pcnf));
	skb_pull(skb, sizeof(conn->pcnf));

	if (conn->hcon->out) {
		u8 random[16];

		swap128(conn->prnd, random);
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(random),
								random);
	} else {
		struct smp_cmd_pairing_confirm cp;
		int ret;
		u8 res[16];

		ret = smp_rand(conn->prnd);
		if (ret)
			return SMP_UNSPECIFIED;

		ret = smp_c1(tfm, conn->tk, conn->prnd, conn->preq, conn->prsp,
						conn->hcon->dst_type, conn->dst,
						0, conn->src, res);
		if (ret)
			return SMP_CONFIRM_FAILED;

		swap128(res, cp.confirm_val);

		smp_send_cmd(conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cp), &cp);
	}

	mod_timer(&conn->security_timer, jiffies +
					msecs_to_jiffies(SMP_TIMEOUT));

	return 0;
}

static u8 smp_cmd_pairing_random(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct hci_conn *hcon = conn->hcon;
	struct crypto_blkcipher *tfm = hcon->hdev->tfm;
	int ret;
	u8 key[16], res[16], random[16], confirm[16];

	swap128(skb->data, random);
	skb_pull(skb, sizeof(random));

	memset(hcon->ltk, 0, sizeof(hcon->ltk));

	if (conn->hcon->out)
		ret = smp_c1(tfm, conn->tk, random, conn->preq, conn->prsp, 0,
				conn->src, conn->hcon->dst_type, conn->dst,
				res);
	else
		ret = smp_c1(tfm, conn->tk, random, conn->preq, conn->prsp,
				conn->hcon->dst_type, conn->dst, 0, conn->src,
				res);
	if (ret)
		return SMP_UNSPECIFIED;

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	swap128(res, confirm);

	if (memcmp(conn->pcnf, confirm, sizeof(conn->pcnf)) != 0) {
		BT_ERR("Pairing failed (confirmation values mismatch)");
		return SMP_CONFIRM_FAILED;
	}

	if (conn->hcon->out) {
		__le16 ediv;
		u8 rand[8];

		smp_s1(tfm, conn->tk, random, conn->prnd, key);
		swap128(key, hcon->ltk);

		memset(hcon->ltk + conn->smp_key_size, 0,
				SMP_MAX_ENC_KEY_SIZE - conn->smp_key_size);

		memset(rand, 0, sizeof(rand));
		ediv = 0;
		hci_le_start_enc(hcon, ediv, rand, hcon->ltk);
	} else {
		u8 r[16];

		swap128(conn->prnd, r);
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(r), r);

		smp_s1(tfm, conn->tk, conn->prnd, random, key);
		swap128(key, hcon->ltk);

		memset(hcon->ltk + conn->smp_key_size, 0,
				SMP_MAX_ENC_KEY_SIZE - conn->smp_key_size);
	}

	return 0;
}

static u8 smp_cmd_security_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_security_req *rp = (void *) skb->data;
	struct smp_cmd_pairing cp;
	struct hci_conn *hcon = conn->hcon;

	BT_DBG("conn %p", conn);

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &hcon->pend))
		return 0;

	skb_pull(skb, sizeof(*rp));

	memset(&cp, 0, sizeof(cp));
	build_pairing_cmd(conn, &cp, NULL, rp->auth_req);

	conn->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&conn->preq[1], &cp, sizeof(cp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);

	mod_timer(&conn->security_timer, jiffies +
					msecs_to_jiffies(SMP_TIMEOUT));

	set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->pend);

	return 0;
}

int smp_conn_security(struct l2cap_conn *conn, __u8 sec_level)
{
	struct hci_conn *hcon = conn->hcon;
	__u8 authreq;

	BT_DBG("conn %p hcon %p level 0x%2.2x", conn, hcon, sec_level);

	if (!lmp_host_le_capable(hcon->hdev))
		return 1;

	if (IS_ERR(hcon->hdev->tfm))
		return 1;

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &hcon->pend))
		return 0;

	if (sec_level == BT_SECURITY_LOW)
		return 1;

	if (hcon->sec_level >= sec_level)
		return 1;

	authreq = seclevel_to_authreq(sec_level);

	if (hcon->link_mode & HCI_LM_MASTER) {
		struct smp_cmd_pairing cp;

		build_pairing_cmd(conn, &cp, NULL, authreq);
		conn->preq[0] = SMP_CMD_PAIRING_REQ;
		memcpy(&conn->preq[1], &cp, sizeof(cp));

		mod_timer(&conn->security_timer, jiffies +
					msecs_to_jiffies(SMP_TIMEOUT));

		smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);
	} else {
		struct smp_cmd_security_req cp;
		cp.auth_req = authreq;
		smp_send_cmd(conn, SMP_CMD_SECURITY_REQ, sizeof(cp), &cp);
	}

	hcon->pending_sec_level = sec_level;
	set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->pend);

	return 0;
}

static int smp_cmd_encrypt_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	BT_DBG("conn %p", conn);
	/* FIXME: store the ltk */
	return 0;
}

static int smp_cmd_master_ident(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing *paircmd = (void *) &conn->prsp[1];
	u8 keydist = paircmd->init_key_dist;

	BT_DBG("keydist 0x%x", keydist);
	/* FIXME: store ediv and rand */

	smp_distribute_keys(conn, 1);

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

	if (IS_ERR(conn->hcon->hdev->tfm)) {
		err = PTR_ERR(conn->hcon->hdev->tfm);
		reason = SMP_PAIRING_NOTSUPP;
		goto done;
	}

	skb_pull(skb, sizeof(code));

	switch (code) {
	case SMP_CMD_PAIRING_REQ:
		reason = smp_cmd_pairing_req(conn, skb);
		break;

	case SMP_CMD_PAIRING_FAIL:
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
		smp_send_cmd(conn, SMP_CMD_PAIRING_FAIL, sizeof(reason),
								&reason);

	kfree_skb(skb);
	return err;
}

int smp_distribute_keys(struct l2cap_conn *conn, __u8 force)
{
	struct smp_cmd_pairing *req, *rsp;
	__u8 *keydist;

	BT_DBG("conn %p force %d", conn, force);

	if (IS_ERR(conn->hcon->hdev->tfm))
		return PTR_ERR(conn->hcon->hdev->tfm);

	rsp = (void *) &conn->prsp[1];

	/* The responder sends its keys first */
	if (!force && conn->hcon->out && (rsp->resp_key_dist & 0x07))
		return 0;

	req = (void *) &conn->preq[1];

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
		__le16 ediv;

		get_random_bytes(enc.ltk, sizeof(enc.ltk));
		get_random_bytes(&ediv, sizeof(ediv));
		get_random_bytes(ident.rand, sizeof(ident.rand));

		smp_send_cmd(conn, SMP_CMD_ENCRYPT_INFO, sizeof(enc), &enc);

		ident.ediv = cpu_to_le16(ediv);

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

	return 0;
}
