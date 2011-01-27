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
#include <crypto/b128ops.h>

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

static void smp_cmd_pairing_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing *rp = (void *) skb->data;

	BT_DBG("conn %p", conn);

	conn->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&conn->preq[1], rp, sizeof(*rp));
	skb_pull(skb, sizeof(*rp));

	rp->io_capability = 0x00;
	rp->oob_flag = 0x00;
	rp->max_key_size = 16;
	rp->init_key_dist = 0x00;
	rp->resp_key_dist = 0x00;
	rp->auth_req &= (SMP_AUTH_BONDING | SMP_AUTH_MITM);

	/* Just works */
	memset(conn->tk, 0, sizeof(conn->tk));

	conn->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&conn->prsp[1], rp, sizeof(*rp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_RSP, sizeof(*rp), rp);
}

static void smp_cmd_pairing_rsp(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing *rp = (void *) skb->data;
	struct smp_cmd_pairing_confirm cp;
	struct crypto_blkcipher *tfm = conn->hcon->hdev->tfm;
	int ret;
	u8 res[16];

	BT_DBG("conn %p", conn);

	/* Just works */
	memset(conn->tk, 0, sizeof(conn->tk));

	conn->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&conn->prsp[1], rp, sizeof(*rp));
	skb_pull(skb, sizeof(*rp));

	ret = smp_rand(conn->prnd);
	if (ret)
		return;

	ret = smp_c1(tfm, conn->tk, conn->prnd, conn->preq, conn->prsp, 0,
			conn->src, conn->hcon->dst_type, conn->dst, res);
	if (ret)
		return;

	swap128(res, cp.confirm_val);

	smp_send_cmd(conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cp), &cp);
}

static void smp_cmd_pairing_confirm(struct l2cap_conn *conn,
							struct sk_buff *skb)
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
			return;

		ret = smp_c1(tfm, conn->tk, conn->prnd, conn->preq, conn->prsp,
						conn->hcon->dst_type, conn->dst,
						0, conn->src, res);
		if (ret)
			return;

		swap128(res, cp.confirm_val);

		smp_send_cmd(conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cp), &cp);
	}
}

static void smp_cmd_pairing_random(struct l2cap_conn *conn, struct sk_buff *skb)
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
		return;

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	swap128(res, confirm);

	if (memcmp(conn->pcnf, confirm, sizeof(conn->pcnf)) != 0) {
		struct smp_cmd_pairing_fail cp;

		BT_ERR("Pairing failed (confirmation values mismatch)");
		cp.reason = SMP_CONFIRM_FAILED;
		smp_send_cmd(conn, SMP_CMD_PAIRING_FAIL, sizeof(cp), &cp);
		return;
	}

	if (conn->hcon->out) {
		__le16 ediv;
		u8 rand[8];

		smp_s1(tfm, conn->tk, random, conn->prnd, key);
		swap128(key, hcon->ltk);

		memset(rand, 0, sizeof(rand));
		ediv = 0;
		hci_le_start_enc(hcon, ediv, rand, hcon->ltk);
	} else {
		u8 r[16];

		swap128(conn->prnd, r);
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(r), r);

		smp_s1(tfm, conn->tk, conn->prnd, random, key);
		swap128(key, hcon->ltk);
	}
}

static void smp_cmd_security_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_security_req *rp = (void *) skb->data;
	struct smp_cmd_pairing cp;
	struct hci_conn *hcon = conn->hcon;

	BT_DBG("conn %p", conn);

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &hcon->pend))
		return;

	skb_pull(skb, sizeof(*rp));
	memset(&cp, 0, sizeof(cp));

	cp.io_capability = 0x00;
	cp.oob_flag = 0x00;
	cp.max_key_size = 16;
	cp.init_key_dist = 0x00;
	cp.resp_key_dist = 0x00;
	cp.auth_req = rp->auth_req & (SMP_AUTH_BONDING | SMP_AUTH_MITM);

	conn->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&conn->preq[1], &cp, sizeof(cp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);

	set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->pend);
}

static __u8 seclevel_to_authreq(__u8 level)
{
	switch (level) {
	case BT_SECURITY_HIGH:
		/* For now we don't support bonding */
		return SMP_AUTH_MITM;

	default:
		return SMP_AUTH_NONE;
	}
}

int smp_conn_security(struct l2cap_conn *conn, __u8 sec_level)
{
	struct hci_conn *hcon = conn->hcon;
	__u8 authreq;

	BT_DBG("conn %p hcon %p level 0x%2.2x", conn, hcon, sec_level);

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
		cp.io_capability = 0x00;
		cp.oob_flag = 0x00;
		cp.max_key_size = 16;
		cp.init_key_dist = 0x00;
		cp.resp_key_dist = 0x00;
		cp.auth_req = authreq;

		conn->preq[0] = SMP_CMD_PAIRING_REQ;
		memcpy(&conn->preq[1], &cp, sizeof(cp));

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

int smp_sig_channel(struct l2cap_conn *conn, struct sk_buff *skb)
{
	__u8 code = skb->data[0];
	__u8 reason;
	int err = 0;

	if (IS_ERR(conn->hcon->hdev->tfm)) {
		err = PTR_ERR(conn->hcon->hdev->tfm);
		reason = SMP_PAIRING_NOTSUPP;
		goto done;
	}

	skb_pull(skb, sizeof(code));

	switch (code) {
	case SMP_CMD_PAIRING_REQ:
		smp_cmd_pairing_req(conn, skb);
		break;

	case SMP_CMD_PAIRING_FAIL:
		break;

	case SMP_CMD_PAIRING_RSP:
		smp_cmd_pairing_rsp(conn, skb);
		break;

	case SMP_CMD_SECURITY_REQ:
		smp_cmd_security_req(conn, skb);
		break;

	case SMP_CMD_PAIRING_CONFIRM:
		smp_cmd_pairing_confirm(conn, skb);
		break;

	case SMP_CMD_PAIRING_RANDOM:
		smp_cmd_pairing_random(conn, skb);
		break;

	case SMP_CMD_ENCRYPT_INFO:
	case SMP_CMD_MASTER_IDENT:
	case SMP_CMD_IDENT_INFO:
	case SMP_CMD_IDENT_ADDR_INFO:
	case SMP_CMD_SIGN_INFO:
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
