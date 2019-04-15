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

#include <linux/debugfs.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/b128ops.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/mgmt.h>

#include "ecdh_helper.h"
#include "smp.h"

#define SMP_DEV(hdev) \
	((struct smp_dev *)((struct l2cap_chan *)((hdev)->smp_data))->data)

/* Low-level debug macros to be used for stuff that we don't want
 * accidentially in dmesg, i.e. the values of the various crypto keys
 * and the inputs & outputs of crypto functions.
 */
#ifdef DEBUG
#define SMP_DBG(fmt, ...) printk(KERN_DEBUG "%s: " fmt, __func__, \
				 ##__VA_ARGS__)
#else
#define SMP_DBG(fmt, ...) no_printk(KERN_DEBUG "%s: " fmt, __func__, \
				    ##__VA_ARGS__)
#endif

#define SMP_ALLOW_CMD(smp, code)	set_bit(code, &smp->allow_cmd)

/* Keys which are not distributed with Secure Connections */
#define SMP_SC_NO_DIST (SMP_DIST_ENC_KEY | SMP_DIST_LINK_KEY);

#define SMP_TIMEOUT	msecs_to_jiffies(30000)

#define AUTH_REQ_MASK(dev)	(hci_dev_test_flag(dev, HCI_SC_ENABLED) ? \
				 0x3f : 0x07)
#define KEY_DIST_MASK		0x07

/* Maximum message length that can be passed to aes_cmac */
#define CMAC_MSG_MAX	80

enum {
	SMP_FLAG_TK_VALID,
	SMP_FLAG_CFM_PENDING,
	SMP_FLAG_MITM_AUTH,
	SMP_FLAG_COMPLETE,
	SMP_FLAG_INITIATOR,
	SMP_FLAG_SC,
	SMP_FLAG_REMOTE_PK,
	SMP_FLAG_DEBUG_KEY,
	SMP_FLAG_WAIT_USER,
	SMP_FLAG_DHKEY_PENDING,
	SMP_FLAG_REMOTE_OOB,
	SMP_FLAG_LOCAL_OOB,
	SMP_FLAG_CT2,
};

struct smp_dev {
	/* Secure Connections OOB data */
	bool			local_oob;
	u8			local_pk[64];
	u8			local_rand[16];
	bool			debug_key;

	struct crypto_cipher	*tfm_aes;
	struct crypto_shash	*tfm_cmac;
	struct crypto_kpp	*tfm_ecdh;
};

struct smp_chan {
	struct l2cap_conn	*conn;
	struct delayed_work	security_timer;
	unsigned long           allow_cmd; /* Bitmask of allowed commands */

	u8		preq[7]; /* SMP Pairing Request */
	u8		prsp[7]; /* SMP Pairing Response */
	u8		prnd[16]; /* SMP Pairing Random (local) */
	u8		rrnd[16]; /* SMP Pairing Random (remote) */
	u8		pcnf[16]; /* SMP Pairing Confirm */
	u8		tk[16]; /* SMP Temporary Key */
	u8		rr[16]; /* Remote OOB ra/rb value */
	u8		lr[16]; /* Local OOB ra/rb value */
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
	u8		*link_key;
	unsigned long	flags;
	u8		method;
	u8		passkey_round;

	/* Secure Connections variables */
	u8			local_pk[64];
	u8			remote_pk[64];
	u8			dhkey[32];
	u8			mackey[16];

	struct crypto_cipher	*tfm_aes;
	struct crypto_shash	*tfm_cmac;
	struct crypto_kpp	*tfm_ecdh;
};

/* These debug key values are defined in the SMP section of the core
 * specification. debug_pk is the public debug key and debug_sk the
 * private debug key.
 */
static const u8 debug_pk[64] = {
		0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
		0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
		0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
		0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20,

		0x8b, 0xd2, 0x89, 0x15, 0xd0, 0x8e, 0x1c, 0x74,
		0x24, 0x30, 0xed, 0x8f, 0xc2, 0x45, 0x63, 0x76,
		0x5c, 0x15, 0x52, 0x5a, 0xbf, 0x9a, 0x32, 0x63,
		0x6d, 0xeb, 0x2a, 0x65, 0x49, 0x9c, 0x80, 0xdc,
};

static const u8 debug_sk[32] = {
		0xbd, 0x1a, 0x3c, 0xcd, 0xa6, 0xb8, 0x99, 0x58,
		0x99, 0xb7, 0x40, 0xeb, 0x7b, 0x60, 0xff, 0x4a,
		0x50, 0x3f, 0x10, 0xd2, 0xe3, 0xb3, 0xc9, 0x74,
		0x38, 0x5f, 0xc5, 0xa3, 0xd4, 0xf6, 0x49, 0x3f,
};

static inline void swap_buf(const u8 *src, u8 *dst, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		dst[len - 1 - i] = src[i];
}

/* The following functions map to the LE SC SMP crypto functions
 * AES-CMAC, f4, f5, f6, g2 and h6.
 */

static int aes_cmac(struct crypto_shash *tfm, const u8 k[16], const u8 *m,
		    size_t len, u8 mac[16])
{
	uint8_t tmp[16], mac_msb[16], msg_msb[CMAC_MSG_MAX];
	SHASH_DESC_ON_STACK(desc, tfm);
	int err;

	if (len > CMAC_MSG_MAX)
		return -EFBIG;

	if (!tfm) {
		BT_ERR("tfm %p", tfm);
		return -EINVAL;
	}

	desc->tfm = tfm;

	/* Swap key and message from LSB to MSB */
	swap_buf(k, tmp, 16);
	swap_buf(m, msg_msb, len);

	SMP_DBG("msg (len %zu) %*phN", len, (int) len, m);
	SMP_DBG("key %16phN", k);

	err = crypto_shash_setkey(tfm, tmp, 16);
	if (err) {
		BT_ERR("cipher setkey failed: %d", err);
		return err;
	}

	err = crypto_shash_digest(desc, msg_msb, len, mac_msb);
	shash_desc_zero(desc);
	if (err) {
		BT_ERR("Hash computation error %d", err);
		return err;
	}

	swap_buf(mac_msb, mac, 16);

	SMP_DBG("mac %16phN", mac);

	return 0;
}

static int smp_f4(struct crypto_shash *tfm_cmac, const u8 u[32],
		  const u8 v[32], const u8 x[16], u8 z, u8 res[16])
{
	u8 m[65];
	int err;

	SMP_DBG("u %32phN", u);
	SMP_DBG("v %32phN", v);
	SMP_DBG("x %16phN z %02x", x, z);

	m[0] = z;
	memcpy(m + 1, v, 32);
	memcpy(m + 33, u, 32);

	err = aes_cmac(tfm_cmac, x, m, sizeof(m), res);
	if (err)
		return err;

	SMP_DBG("res %16phN", res);

	return err;
}

static int smp_f5(struct crypto_shash *tfm_cmac, const u8 w[32],
		  const u8 n1[16], const u8 n2[16], const u8 a1[7],
		  const u8 a2[7], u8 mackey[16], u8 ltk[16])
{
	/* The btle, salt and length "magic" values are as defined in
	 * the SMP section of the Bluetooth core specification. In ASCII
	 * the btle value ends up being 'btle'. The salt is just a
	 * random number whereas length is the value 256 in little
	 * endian format.
	 */
	const u8 btle[4] = { 0x65, 0x6c, 0x74, 0x62 };
	const u8 salt[16] = { 0xbe, 0x83, 0x60, 0x5a, 0xdb, 0x0b, 0x37, 0x60,
			      0x38, 0xa5, 0xf5, 0xaa, 0x91, 0x83, 0x88, 0x6c };
	const u8 length[2] = { 0x00, 0x01 };
	u8 m[53], t[16];
	int err;

	SMP_DBG("w %32phN", w);
	SMP_DBG("n1 %16phN n2 %16phN", n1, n2);
	SMP_DBG("a1 %7phN a2 %7phN", a1, a2);

	err = aes_cmac(tfm_cmac, salt, w, 32, t);
	if (err)
		return err;

	SMP_DBG("t %16phN", t);

	memcpy(m, length, 2);
	memcpy(m + 2, a2, 7);
	memcpy(m + 9, a1, 7);
	memcpy(m + 16, n2, 16);
	memcpy(m + 32, n1, 16);
	memcpy(m + 48, btle, 4);

	m[52] = 0; /* Counter */

	err = aes_cmac(tfm_cmac, t, m, sizeof(m), mackey);
	if (err)
		return err;

	SMP_DBG("mackey %16phN", mackey);

	m[52] = 1; /* Counter */

	err = aes_cmac(tfm_cmac, t, m, sizeof(m), ltk);
	if (err)
		return err;

	SMP_DBG("ltk %16phN", ltk);

	return 0;
}

static int smp_f6(struct crypto_shash *tfm_cmac, const u8 w[16],
		  const u8 n1[16], const u8 n2[16], const u8 r[16],
		  const u8 io_cap[3], const u8 a1[7], const u8 a2[7],
		  u8 res[16])
{
	u8 m[65];
	int err;

	SMP_DBG("w %16phN", w);
	SMP_DBG("n1 %16phN n2 %16phN", n1, n2);
	SMP_DBG("r %16phN io_cap %3phN a1 %7phN a2 %7phN", r, io_cap, a1, a2);

	memcpy(m, a2, 7);
	memcpy(m + 7, a1, 7);
	memcpy(m + 14, io_cap, 3);
	memcpy(m + 17, r, 16);
	memcpy(m + 33, n2, 16);
	memcpy(m + 49, n1, 16);

	err = aes_cmac(tfm_cmac, w, m, sizeof(m), res);
	if (err)
		return err;

	SMP_DBG("res %16phN", res);

	return err;
}

static int smp_g2(struct crypto_shash *tfm_cmac, const u8 u[32], const u8 v[32],
		  const u8 x[16], const u8 y[16], u32 *val)
{
	u8 m[80], tmp[16];
	int err;

	SMP_DBG("u %32phN", u);
	SMP_DBG("v %32phN", v);
	SMP_DBG("x %16phN y %16phN", x, y);

	memcpy(m, y, 16);
	memcpy(m + 16, v, 32);
	memcpy(m + 48, u, 32);

	err = aes_cmac(tfm_cmac, x, m, sizeof(m), tmp);
	if (err)
		return err;

	*val = get_unaligned_le32(tmp);
	*val %= 1000000;

	SMP_DBG("val %06u", *val);

	return 0;
}

static int smp_h6(struct crypto_shash *tfm_cmac, const u8 w[16],
		  const u8 key_id[4], u8 res[16])
{
	int err;

	SMP_DBG("w %16phN key_id %4phN", w, key_id);

	err = aes_cmac(tfm_cmac, w, key_id, 4, res);
	if (err)
		return err;

	SMP_DBG("res %16phN", res);

	return err;
}

static int smp_h7(struct crypto_shash *tfm_cmac, const u8 w[16],
		  const u8 salt[16], u8 res[16])
{
	int err;

	SMP_DBG("w %16phN salt %16phN", w, salt);

	err = aes_cmac(tfm_cmac, salt, w, 16, res);
	if (err)
		return err;

	SMP_DBG("res %16phN", res);

	return err;
}

/* The following functions map to the legacy SMP crypto functions e, c1,
 * s1 and ah.
 */

static int smp_e(struct crypto_cipher *tfm, const u8 *k, u8 *r)
{
	uint8_t tmp[16], data[16];
	int err;

	SMP_DBG("k %16phN r %16phN", k, r);

	if (!tfm) {
		BT_ERR("tfm %p", tfm);
		return -EINVAL;
	}

	/* The most significant octet of key corresponds to k[0] */
	swap_buf(k, tmp, 16);

	err = crypto_cipher_setkey(tfm, tmp, 16);
	if (err) {
		BT_ERR("cipher setkey failed: %d", err);
		return err;
	}

	/* Most significant octet of plaintextData corresponds to data[0] */
	swap_buf(r, data, 16);

	crypto_cipher_encrypt_one(tfm, data, data);

	/* Most significant octet of encryptedData corresponds to data[0] */
	swap_buf(data, r, 16);

	SMP_DBG("r %16phN", r);

	return err;
}

static int smp_c1(struct crypto_cipher *tfm_aes, const u8 k[16],
		  const u8 r[16], const u8 preq[7], const u8 pres[7], u8 _iat,
		  const bdaddr_t *ia, u8 _rat, const bdaddr_t *ra, u8 res[16])
{
	u8 p1[16], p2[16];
	int err;

	SMP_DBG("k %16phN r %16phN", k, r);
	SMP_DBG("iat %u ia %6phN rat %u ra %6phN", _iat, ia, _rat, ra);
	SMP_DBG("preq %7phN pres %7phN", preq, pres);

	memset(p1, 0, 16);

	/* p1 = pres || preq || _rat || _iat */
	p1[0] = _iat;
	p1[1] = _rat;
	memcpy(p1 + 2, preq, 7);
	memcpy(p1 + 9, pres, 7);

	SMP_DBG("p1 %16phN", p1);

	/* res = r XOR p1 */
	u128_xor((u128 *) res, (u128 *) r, (u128 *) p1);

	/* res = e(k, res) */
	err = smp_e(tfm_aes, k, res);
	if (err) {
		BT_ERR("Encrypt data error");
		return err;
	}

	/* p2 = padding || ia || ra */
	memcpy(p2, ra, 6);
	memcpy(p2 + 6, ia, 6);
	memset(p2 + 12, 0, 4);

	SMP_DBG("p2 %16phN", p2);

	/* res = res XOR p2 */
	u128_xor((u128 *) res, (u128 *) res, (u128 *) p2);

	/* res = e(k, res) */
	err = smp_e(tfm_aes, k, res);
	if (err)
		BT_ERR("Encrypt data error");

	return err;
}

static int smp_s1(struct crypto_cipher *tfm_aes, const u8 k[16],
		  const u8 r1[16], const u8 r2[16], u8 _r[16])
{
	int err;

	/* Just least significant octets from r1 and r2 are considered */
	memcpy(_r, r2, 8);
	memcpy(_r + 8, r1, 8);

	err = smp_e(tfm_aes, k, _r);
	if (err)
		BT_ERR("Encrypt data error");

	return err;
}

static int smp_ah(struct crypto_cipher *tfm, const u8 irk[16],
		  const u8 r[3], u8 res[3])
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
	 *	ah(k, r) = e(k, r') mod 2^24
	 * The output of the security function e is then truncated to 24 bits
	 * by taking the least significant 24 bits of the output of e as the
	 * result of ah.
	 */
	memcpy(res, _res, 3);

	return 0;
}

bool smp_irk_matches(struct hci_dev *hdev, const u8 irk[16],
		     const bdaddr_t *bdaddr)
{
	struct l2cap_chan *chan = hdev->smp_data;
	struct smp_dev *smp;
	u8 hash[3];
	int err;

	if (!chan || !chan->data)
		return false;

	smp = chan->data;

	BT_DBG("RPA %pMR IRK %*phN", bdaddr, 16, irk);

	err = smp_ah(smp->tfm_aes, irk, &bdaddr->b[3], hash);
	if (err)
		return false;

	return !crypto_memneq(bdaddr->b, hash, 3);
}

int smp_generate_rpa(struct hci_dev *hdev, const u8 irk[16], bdaddr_t *rpa)
{
	struct l2cap_chan *chan = hdev->smp_data;
	struct smp_dev *smp;
	int err;

	if (!chan || !chan->data)
		return -EOPNOTSUPP;

	smp = chan->data;

	get_random_bytes(&rpa->b[3], 3);

	rpa->b[5] &= 0x3f;	/* Clear two most significant bits */
	rpa->b[5] |= 0x40;	/* Set second most significant bit */

	err = smp_ah(smp->tfm_aes, irk, &rpa->b[3], rpa->b);
	if (err < 0)
		return err;

	BT_DBG("RPA %pMR", rpa);

	return 0;
}

int smp_generate_oob(struct hci_dev *hdev, u8 hash[16], u8 rand[16])
{
	struct l2cap_chan *chan = hdev->smp_data;
	struct smp_dev *smp;
	int err;

	if (!chan || !chan->data)
		return -EOPNOTSUPP;

	smp = chan->data;

	if (hci_dev_test_flag(hdev, HCI_USE_DEBUG_KEYS)) {
		BT_DBG("Using debug keys");
		err = set_ecdh_privkey(smp->tfm_ecdh, debug_sk);
		if (err)
			return err;
		memcpy(smp->local_pk, debug_pk, 64);
		smp->debug_key = true;
	} else {
		while (true) {
			/* Generate key pair for Secure Connections */
			err = generate_ecdh_keys(smp->tfm_ecdh, smp->local_pk);
			if (err)
				return err;

			/* This is unlikely, but we need to check that
			 * we didn't accidentially generate a debug key.
			 */
			if (crypto_memneq(smp->local_pk, debug_pk, 64))
				break;
		}
		smp->debug_key = false;
	}

	SMP_DBG("OOB Public Key X: %32phN", smp->local_pk);
	SMP_DBG("OOB Public Key Y: %32phN", smp->local_pk + 32);

	get_random_bytes(smp->local_rand, 16);

	err = smp_f4(smp->tfm_cmac, smp->local_pk, smp->local_pk,
		     smp->local_rand, 0, hash);
	if (err < 0)
		return err;

	memcpy(rand, smp->local_rand, 16);

	smp->local_oob = true;

	return 0;
}

static void smp_send_cmd(struct l2cap_conn *conn, u8 code, u16 len, void *data)
{
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp;
	struct kvec iv[2];
	struct msghdr msg;

	if (!chan)
		return;

	BT_DBG("code 0x%2.2x", code);

	iv[0].iov_base = &code;
	iv[0].iov_len = 1;

	iv[1].iov_base = data;
	iv[1].iov_len = len;

	memset(&msg, 0, sizeof(msg));

	iov_iter_kvec(&msg.msg_iter, WRITE, iv, 2, 1 + len);

	l2cap_chan_send(chan, &msg, 1 + len);

	if (!chan->data)
		return;

	smp = chan->data;

	cancel_delayed_work_sync(&smp->security_timer);
	schedule_delayed_work(&smp->security_timer, SMP_TIMEOUT);
}

static u8 authreq_to_seclevel(u8 authreq)
{
	if (authreq & SMP_AUTH_MITM) {
		if (authreq & SMP_AUTH_SC)
			return BT_SECURITY_FIPS;
		else
			return BT_SECURITY_HIGH;
	} else {
		return BT_SECURITY_MEDIUM;
	}
}

static __u8 seclevel_to_authreq(__u8 sec_level)
{
	switch (sec_level) {
	case BT_SECURITY_FIPS:
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
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	u8 local_dist = 0, remote_dist = 0, oob_flag = SMP_OOB_NOT_PRESENT;

	if (hci_dev_test_flag(hdev, HCI_BONDABLE)) {
		local_dist = SMP_DIST_ENC_KEY | SMP_DIST_SIGN;
		remote_dist = SMP_DIST_ENC_KEY | SMP_DIST_SIGN;
		authreq |= SMP_AUTH_BONDING;
	} else {
		authreq &= ~SMP_AUTH_BONDING;
	}

	if (hci_dev_test_flag(hdev, HCI_RPA_RESOLVING))
		remote_dist |= SMP_DIST_ID_KEY;

	if (hci_dev_test_flag(hdev, HCI_PRIVACY))
		local_dist |= SMP_DIST_ID_KEY;

	if (hci_dev_test_flag(hdev, HCI_SC_ENABLED) &&
	    (authreq & SMP_AUTH_SC)) {
		struct oob_data *oob_data;
		u8 bdaddr_type;

		if (hci_dev_test_flag(hdev, HCI_SSP_ENABLED)) {
			local_dist |= SMP_DIST_LINK_KEY;
			remote_dist |= SMP_DIST_LINK_KEY;
		}

		if (hcon->dst_type == ADDR_LE_DEV_PUBLIC)
			bdaddr_type = BDADDR_LE_PUBLIC;
		else
			bdaddr_type = BDADDR_LE_RANDOM;

		oob_data = hci_find_remote_oob_data(hdev, &hcon->dst,
						    bdaddr_type);
		if (oob_data && oob_data->present) {
			set_bit(SMP_FLAG_REMOTE_OOB, &smp->flags);
			oob_flag = SMP_OOB_PRESENT;
			memcpy(smp->rr, oob_data->rand256, 16);
			memcpy(smp->pcnf, oob_data->hash256, 16);
			SMP_DBG("OOB Remote Confirmation: %16phN", smp->pcnf);
			SMP_DBG("OOB Remote Random: %16phN", smp->rr);
		}

	} else {
		authreq &= ~SMP_AUTH_SC;
	}

	if (rsp == NULL) {
		req->io_capability = conn->hcon->io_capability;
		req->oob_flag = oob_flag;
		req->max_key_size = hdev->le_max_key_size;
		req->init_key_dist = local_dist;
		req->resp_key_dist = remote_dist;
		req->auth_req = (authreq & AUTH_REQ_MASK(hdev));

		smp->remote_key_dist = remote_dist;
		return;
	}

	rsp->io_capability = conn->hcon->io_capability;
	rsp->oob_flag = oob_flag;
	rsp->max_key_size = hdev->le_max_key_size;
	rsp->init_key_dist = req->init_key_dist & remote_dist;
	rsp->resp_key_dist = req->resp_key_dist & local_dist;
	rsp->auth_req = (authreq & AUTH_REQ_MASK(hdev));

	smp->remote_key_dist = rsp->init_key_dist;
}

static u8 check_enc_key_size(struct l2cap_conn *conn, __u8 max_key_size)
{
	struct l2cap_chan *chan = conn->smp;
	struct hci_dev *hdev = conn->hcon->hdev;
	struct smp_chan *smp = chan->data;

	if (max_key_size > hdev->le_max_key_size ||
	    max_key_size < SMP_MIN_ENC_KEY_SIZE)
		return SMP_ENC_KEY_SIZE;

	smp->enc_key_size = max_key_size;

	return 0;
}

static void smp_chan_destroy(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_conn *hcon = conn->hcon;
	bool complete;

	BUG_ON(!smp);

	cancel_delayed_work_sync(&smp->security_timer);

	complete = test_bit(SMP_FLAG_COMPLETE, &smp->flags);
	mgmt_smp_complete(hcon, complete);

	kzfree(smp->csrk);
	kzfree(smp->slave_csrk);
	kzfree(smp->link_key);

	crypto_free_cipher(smp->tfm_aes);
	crypto_free_shash(smp->tfm_cmac);
	crypto_free_kpp(smp->tfm_ecdh);

	/* Ensure that we don't leave any debug key around if debug key
	 * support hasn't been explicitly enabled.
	 */
	if (smp->ltk && smp->ltk->type == SMP_LTK_P256_DEBUG &&
	    !hci_dev_test_flag(hcon->hdev, HCI_KEEP_DEBUG_KEYS)) {
		list_del_rcu(&smp->ltk->list);
		kfree_rcu(smp->ltk, rcu);
		smp->ltk = NULL;
	}

	/* If pairing failed clean up any keys we might have */
	if (!complete) {
		if (smp->ltk) {
			list_del_rcu(&smp->ltk->list);
			kfree_rcu(smp->ltk, rcu);
		}

		if (smp->slave_ltk) {
			list_del_rcu(&smp->slave_ltk->list);
			kfree_rcu(smp->slave_ltk, rcu);
		}

		if (smp->remote_irk) {
			list_del_rcu(&smp->remote_irk->list);
			kfree_rcu(smp->remote_irk, rcu);
		}
	}

	chan->data = NULL;
	kzfree(smp);
	hci_conn_drop(hcon);
}

static void smp_failure(struct l2cap_conn *conn, u8 reason)
{
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_chan *chan = conn->smp;

	if (reason)
		smp_send_cmd(conn, SMP_CMD_PAIRING_FAIL, sizeof(reason),
			     &reason);

	mgmt_auth_failed(hcon, HCI_ERROR_AUTH_FAILURE);

	if (chan->data)
		smp_chan_destroy(conn);
}

#define JUST_WORKS	0x00
#define JUST_CFM	0x01
#define REQ_PASSKEY	0x02
#define CFM_PASSKEY	0x03
#define REQ_OOB		0x04
#define DSP_PASSKEY	0x05
#define OVERLAP		0xFF

static const u8 gen_method[5][5] = {
	{ JUST_WORKS,  JUST_CFM,    REQ_PASSKEY, JUST_WORKS, REQ_PASSKEY },
	{ JUST_WORKS,  JUST_CFM,    REQ_PASSKEY, JUST_WORKS, REQ_PASSKEY },
	{ CFM_PASSKEY, CFM_PASSKEY, REQ_PASSKEY, JUST_WORKS, CFM_PASSKEY },
	{ JUST_WORKS,  JUST_CFM,    JUST_WORKS,  JUST_WORKS, JUST_CFM    },
	{ CFM_PASSKEY, CFM_PASSKEY, REQ_PASSKEY, JUST_WORKS, OVERLAP     },
};

static const u8 sc_method[5][5] = {
	{ JUST_WORKS,  JUST_CFM,    REQ_PASSKEY, JUST_WORKS, REQ_PASSKEY },
	{ JUST_WORKS,  CFM_PASSKEY, REQ_PASSKEY, JUST_WORKS, CFM_PASSKEY },
	{ DSP_PASSKEY, DSP_PASSKEY, REQ_PASSKEY, JUST_WORKS, DSP_PASSKEY },
	{ JUST_WORKS,  JUST_CFM,    JUST_WORKS,  JUST_WORKS, JUST_CFM    },
	{ DSP_PASSKEY, CFM_PASSKEY, REQ_PASSKEY, JUST_WORKS, CFM_PASSKEY },
};

static u8 get_auth_method(struct smp_chan *smp, u8 local_io, u8 remote_io)
{
	/* If either side has unknown io_caps, use JUST_CFM (which gets
	 * converted later to JUST_WORKS if we're initiators.
	 */
	if (local_io > SMP_IO_KEYBOARD_DISPLAY ||
	    remote_io > SMP_IO_KEYBOARD_DISPLAY)
		return JUST_CFM;

	if (test_bit(SMP_FLAG_SC, &smp->flags))
		return sc_method[remote_io][local_io];

	return gen_method[remote_io][local_io];
}

static int tk_request(struct l2cap_conn *conn, u8 remote_oob, u8 auth,
						u8 local_io, u8 remote_io)
{
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
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
		smp->method = JUST_CFM;
	else
		smp->method = get_auth_method(smp, local_io, remote_io);

	/* Don't confirm locally initiated pairing attempts */
	if (smp->method == JUST_CFM && test_bit(SMP_FLAG_INITIATOR,
						&smp->flags))
		smp->method = JUST_WORKS;

	/* Don't bother user space with no IO capabilities */
	if (smp->method == JUST_CFM &&
	    hcon->io_capability == HCI_IO_NO_INPUT_OUTPUT)
		smp->method = JUST_WORKS;

	/* If Just Works, Continue with Zero TK */
	if (smp->method == JUST_WORKS) {
		set_bit(SMP_FLAG_TK_VALID, &smp->flags);
		return 0;
	}

	/* If this function is used for SC -> legacy fallback we
	 * can only recover the just-works case.
	 */
	if (test_bit(SMP_FLAG_SC, &smp->flags))
		return -EINVAL;

	/* Not Just Works/Confirm results in MITM Authentication */
	if (smp->method != JUST_CFM) {
		set_bit(SMP_FLAG_MITM_AUTH, &smp->flags);
		if (hcon->pending_sec_level < BT_SECURITY_HIGH)
			hcon->pending_sec_level = BT_SECURITY_HIGH;
	}

	/* If both devices have Keyoard-Display I/O, the master
	 * Confirms and the slave Enters the passkey.
	 */
	if (smp->method == OVERLAP) {
		if (hcon->role == HCI_ROLE_MASTER)
			smp->method = CFM_PASSKEY;
		else
			smp->method = REQ_PASSKEY;
	}

	/* Generate random passkey. */
	if (smp->method == CFM_PASSKEY) {
		memset(smp->tk, 0, sizeof(smp->tk));
		get_random_bytes(&passkey, sizeof(passkey));
		passkey %= 1000000;
		put_unaligned_le32(passkey, smp->tk);
		BT_DBG("PassKey: %d", passkey);
		set_bit(SMP_FLAG_TK_VALID, &smp->flags);
	}

	if (smp->method == REQ_PASSKEY)
		ret = mgmt_user_passkey_request(hcon->hdev, &hcon->dst,
						hcon->type, hcon->dst_type);
	else if (smp->method == JUST_CFM)
		ret = mgmt_user_confirm_request(hcon->hdev, &hcon->dst,
						hcon->type, hcon->dst_type,
						passkey, 1);
	else
		ret = mgmt_user_passkey_notify(hcon->hdev, &hcon->dst,
						hcon->type, hcon->dst_type,
						passkey, 0);

	return ret;
}

static u8 smp_confirm(struct smp_chan *smp)
{
	struct l2cap_conn *conn = smp->conn;
	struct smp_cmd_pairing_confirm cp;
	int ret;

	BT_DBG("conn %p", conn);

	ret = smp_c1(smp->tfm_aes, smp->tk, smp->prnd, smp->preq, smp->prsp,
		     conn->hcon->init_addr_type, &conn->hcon->init_addr,
		     conn->hcon->resp_addr_type, &conn->hcon->resp_addr,
		     cp.confirm_val);
	if (ret)
		return SMP_UNSPECIFIED;

	clear_bit(SMP_FLAG_CFM_PENDING, &smp->flags);

	smp_send_cmd(smp->conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cp), &cp);

	if (conn->hcon->out)
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_CONFIRM);
	else
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RANDOM);

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

	ret = smp_c1(smp->tfm_aes, smp->tk, smp->rrnd, smp->preq, smp->prsp,
		     hcon->init_addr_type, &hcon->init_addr,
		     hcon->resp_addr_type, &hcon->resp_addr, confirm);
	if (ret)
		return SMP_UNSPECIFIED;

	if (crypto_memneq(smp->pcnf, confirm, sizeof(smp->pcnf))) {
		bt_dev_err(hcon->hdev, "pairing failed "
			   "(confirmation values mismatch)");
		return SMP_CONFIRM_FAILED;
	}

	if (hcon->out) {
		u8 stk[16];
		__le64 rand = 0;
		__le16 ediv = 0;

		smp_s1(smp->tfm_aes, smp->tk, smp->rrnd, smp->prnd, stk);

		if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->flags))
			return SMP_UNSPECIFIED;

		hci_le_start_enc(hcon, ediv, rand, stk, smp->enc_key_size);
		hcon->enc_key_size = smp->enc_key_size;
		set_bit(HCI_CONN_STK_ENCRYPT, &hcon->flags);
	} else {
		u8 stk[16], auth;
		__le64 rand = 0;
		__le16 ediv = 0;

		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(smp->prnd),
			     smp->prnd);

		smp_s1(smp->tfm_aes, smp->tk, smp->prnd, smp->rrnd, stk);

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

static void smp_notify_keys(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	struct smp_cmd_pairing *req = (void *) &smp->preq[1];
	struct smp_cmd_pairing *rsp = (void *) &smp->prsp[1];
	bool persistent;

	if (hcon->type == ACL_LINK) {
		if (hcon->key_type == HCI_LK_DEBUG_COMBINATION)
			persistent = false;
		else
			persistent = !test_bit(HCI_CONN_FLUSH_KEY,
					       &hcon->flags);
	} else {
		/* The LTKs, IRKs and CSRKs should be persistent only if
		 * both sides had the bonding bit set in their
		 * authentication requests.
		 */
		persistent = !!((req->auth_req & rsp->auth_req) &
				SMP_AUTH_BONDING);
	}

	if (smp->remote_irk) {
		mgmt_new_irk(hdev, smp->remote_irk, persistent);

		/* Now that user space can be considered to know the
		 * identity address track the connection based on it
		 * from now on (assuming this is an LE link).
		 */
		if (hcon->type == LE_LINK) {
			bacpy(&hcon->dst, &smp->remote_irk->bdaddr);
			hcon->dst_type = smp->remote_irk->addr_type;
			queue_work(hdev->workqueue, &conn->id_addr_update_work);
		}
	}

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

	if (smp->link_key) {
		struct link_key *key;
		u8 type;

		if (test_bit(SMP_FLAG_DEBUG_KEY, &smp->flags))
			type = HCI_LK_DEBUG_COMBINATION;
		else if (hcon->sec_level == BT_SECURITY_FIPS)
			type = HCI_LK_AUTH_COMBINATION_P256;
		else
			type = HCI_LK_UNAUTH_COMBINATION_P256;

		key = hci_add_link_key(hdev, smp->conn->hcon, &hcon->dst,
				       smp->link_key, type, 0, &persistent);
		if (key) {
			mgmt_new_link_key(hdev, key, persistent);

			/* Don't keep debug keys around if the relevant
			 * flag is not set.
			 */
			if (!hci_dev_test_flag(hdev, HCI_KEEP_DEBUG_KEYS) &&
			    key->type == HCI_LK_DEBUG_COMBINATION) {
				list_del_rcu(&key->list);
				kfree_rcu(key, rcu);
			}
		}
	}
}

static void sc_add_ltk(struct smp_chan *smp)
{
	struct hci_conn *hcon = smp->conn->hcon;
	u8 key_type, auth;

	if (test_bit(SMP_FLAG_DEBUG_KEY, &smp->flags))
		key_type = SMP_LTK_P256_DEBUG;
	else
		key_type = SMP_LTK_P256;

	if (hcon->pending_sec_level == BT_SECURITY_FIPS)
		auth = 1;
	else
		auth = 0;

	smp->ltk = hci_add_ltk(hcon->hdev, &hcon->dst, hcon->dst_type,
			       key_type, auth, smp->tk, smp->enc_key_size,
			       0, 0);
}

static void sc_generate_link_key(struct smp_chan *smp)
{
	/* From core spec. Spells out in ASCII as 'lebr'. */
	const u8 lebr[4] = { 0x72, 0x62, 0x65, 0x6c };

	smp->link_key = kzalloc(16, GFP_KERNEL);
	if (!smp->link_key)
		return;

	if (test_bit(SMP_FLAG_CT2, &smp->flags)) {
		/* SALT = 0x00000000000000000000000000000000746D7031 */
		const u8 salt[16] = { 0x31, 0x70, 0x6d, 0x74 };

		if (smp_h7(smp->tfm_cmac, smp->tk, salt, smp->link_key)) {
			kzfree(smp->link_key);
			smp->link_key = NULL;
			return;
		}
	} else {
		/* From core spec. Spells out in ASCII as 'tmp1'. */
		const u8 tmp1[4] = { 0x31, 0x70, 0x6d, 0x74 };

		if (smp_h6(smp->tfm_cmac, smp->tk, tmp1, smp->link_key)) {
			kzfree(smp->link_key);
			smp->link_key = NULL;
			return;
		}
	}

	if (smp_h6(smp->tfm_cmac, smp->link_key, lebr, smp->link_key)) {
		kzfree(smp->link_key);
		smp->link_key = NULL;
		return;
	}
}

static void smp_allow_key_dist(struct smp_chan *smp)
{
	/* Allow the first expected phase 3 PDU. The rest of the PDUs
	 * will be allowed in each PDU handler to ensure we receive
	 * them in the correct order.
	 */
	if (smp->remote_key_dist & SMP_DIST_ENC_KEY)
		SMP_ALLOW_CMD(smp, SMP_CMD_ENCRYPT_INFO);
	else if (smp->remote_key_dist & SMP_DIST_ID_KEY)
		SMP_ALLOW_CMD(smp, SMP_CMD_IDENT_INFO);
	else if (smp->remote_key_dist & SMP_DIST_SIGN)
		SMP_ALLOW_CMD(smp, SMP_CMD_SIGN_INFO);
}

static void sc_generate_ltk(struct smp_chan *smp)
{
	/* From core spec. Spells out in ASCII as 'brle'. */
	const u8 brle[4] = { 0x65, 0x6c, 0x72, 0x62 };
	struct hci_conn *hcon = smp->conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	struct link_key *key;

	key = hci_find_link_key(hdev, &hcon->dst);
	if (!key) {
		bt_dev_err(hdev, "no Link Key found to generate LTK");
		return;
	}

	if (key->type == HCI_LK_DEBUG_COMBINATION)
		set_bit(SMP_FLAG_DEBUG_KEY, &smp->flags);

	if (test_bit(SMP_FLAG_CT2, &smp->flags)) {
		/* SALT = 0x00000000000000000000000000000000746D7032 */
		const u8 salt[16] = { 0x32, 0x70, 0x6d, 0x74 };

		if (smp_h7(smp->tfm_cmac, key->val, salt, smp->tk))
			return;
	} else {
		/* From core spec. Spells out in ASCII as 'tmp2'. */
		const u8 tmp2[4] = { 0x32, 0x70, 0x6d, 0x74 };

		if (smp_h6(smp->tfm_cmac, key->val, tmp2, smp->tk))
			return;
	}

	if (smp_h6(smp->tfm_cmac, smp->tk, brle, smp->tk))
		return;

	sc_add_ltk(smp);
}

static void smp_distribute_keys(struct smp_chan *smp)
{
	struct smp_cmd_pairing *req, *rsp;
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	__u8 *keydist;

	BT_DBG("conn %p", conn);

	rsp = (void *) &smp->prsp[1];

	/* The responder sends its keys first */
	if (hcon->out && (smp->remote_key_dist & KEY_DIST_MASK)) {
		smp_allow_key_dist(smp);
		return;
	}

	req = (void *) &smp->preq[1];

	if (hcon->out) {
		keydist = &rsp->init_key_dist;
		*keydist &= req->init_key_dist;
	} else {
		keydist = &rsp->resp_key_dist;
		*keydist &= req->resp_key_dist;
	}

	if (test_bit(SMP_FLAG_SC, &smp->flags)) {
		if (hcon->type == LE_LINK && (*keydist & SMP_DIST_LINK_KEY))
			sc_generate_link_key(smp);
		if (hcon->type == ACL_LINK && (*keydist & SMP_DIST_ENC_KEY))
			sc_generate_ltk(smp);

		/* Clear the keys which are generated but not distributed */
		*keydist &= ~SMP_SC_NO_DIST;
	}

	BT_DBG("keydist 0x%x", *keydist);

	if (*keydist & SMP_DIST_ENC_KEY) {
		struct smp_cmd_encrypt_info enc;
		struct smp_cmd_master_ident ident;
		struct smp_ltk *ltk;
		u8 authenticated;
		__le16 ediv;
		__le64 rand;

		/* Make sure we generate only the significant amount of
		 * bytes based on the encryption key size, and set the rest
		 * of the value to zeroes.
		 */
		get_random_bytes(enc.ltk, smp->enc_key_size);
		memset(enc.ltk + smp->enc_key_size, 0,
		       sizeof(enc.ltk) - smp->enc_key_size);

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
			if (hcon->sec_level > BT_SECURITY_MEDIUM)
				csrk->type = MGMT_CSRK_LOCAL_AUTHENTICATED;
			else
				csrk->type = MGMT_CSRK_LOCAL_UNAUTHENTICATED;
			memcpy(csrk->val, sign.csrk, sizeof(csrk->val));
		}
		smp->slave_csrk = csrk;

		smp_send_cmd(conn, SMP_CMD_SIGN_INFO, sizeof(sign), &sign);

		*keydist &= ~SMP_DIST_SIGN;
	}

	/* If there are still keys to be received wait for them */
	if (smp->remote_key_dist & KEY_DIST_MASK) {
		smp_allow_key_dist(smp);
		return;
	}

	set_bit(SMP_FLAG_COMPLETE, &smp->flags);
	smp_notify_keys(conn);

	smp_chan_destroy(conn);
}

static void smp_timeout(struct work_struct *work)
{
	struct smp_chan *smp = container_of(work, struct smp_chan,
					    security_timer.work);
	struct l2cap_conn *conn = smp->conn;

	BT_DBG("conn %p", conn);

	hci_disconnect(conn->hcon, HCI_ERROR_REMOTE_USER_TERM);
}

static struct smp_chan *smp_chan_create(struct l2cap_conn *conn)
{
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp;

	smp = kzalloc(sizeof(*smp), GFP_ATOMIC);
	if (!smp)
		return NULL;

	smp->tfm_aes = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(smp->tfm_aes)) {
		BT_ERR("Unable to create AES crypto context");
		goto zfree_smp;
	}

	smp->tfm_cmac = crypto_alloc_shash("cmac(aes)", 0, 0);
	if (IS_ERR(smp->tfm_cmac)) {
		BT_ERR("Unable to create CMAC crypto context");
		goto free_cipher;
	}

	smp->tfm_ecdh = crypto_alloc_kpp("ecdh", CRYPTO_ALG_INTERNAL, 0);
	if (IS_ERR(smp->tfm_ecdh)) {
		BT_ERR("Unable to create ECDH crypto context");
		goto free_shash;
	}

	smp->conn = conn;
	chan->data = smp;

	SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_FAIL);

	INIT_DELAYED_WORK(&smp->security_timer, smp_timeout);

	hci_conn_hold(conn->hcon);

	return smp;

free_shash:
	crypto_free_shash(smp->tfm_cmac);
free_cipher:
	crypto_free_cipher(smp->tfm_aes);
zfree_smp:
	kzfree(smp);
	return NULL;
}

static int sc_mackey_and_ltk(struct smp_chan *smp, u8 mackey[16], u8 ltk[16])
{
	struct hci_conn *hcon = smp->conn->hcon;
	u8 *na, *nb, a[7], b[7];

	if (hcon->out) {
		na   = smp->prnd;
		nb   = smp->rrnd;
	} else {
		na   = smp->rrnd;
		nb   = smp->prnd;
	}

	memcpy(a, &hcon->init_addr, 6);
	memcpy(b, &hcon->resp_addr, 6);
	a[6] = hcon->init_addr_type;
	b[6] = hcon->resp_addr_type;

	return smp_f5(smp->tfm_cmac, smp->dhkey, na, nb, a, b, mackey, ltk);
}

static void sc_dhkey_check(struct smp_chan *smp)
{
	struct hci_conn *hcon = smp->conn->hcon;
	struct smp_cmd_dhkey_check check;
	u8 a[7], b[7], *local_addr, *remote_addr;
	u8 io_cap[3], r[16];

	memcpy(a, &hcon->init_addr, 6);
	memcpy(b, &hcon->resp_addr, 6);
	a[6] = hcon->init_addr_type;
	b[6] = hcon->resp_addr_type;

	if (hcon->out) {
		local_addr = a;
		remote_addr = b;
		memcpy(io_cap, &smp->preq[1], 3);
	} else {
		local_addr = b;
		remote_addr = a;
		memcpy(io_cap, &smp->prsp[1], 3);
	}

	memset(r, 0, sizeof(r));

	if (smp->method == REQ_PASSKEY || smp->method == DSP_PASSKEY)
		put_unaligned_le32(hcon->passkey_notify, r);

	if (smp->method == REQ_OOB)
		memcpy(r, smp->rr, 16);

	smp_f6(smp->tfm_cmac, smp->mackey, smp->prnd, smp->rrnd, r, io_cap,
	       local_addr, remote_addr, check.e);

	smp_send_cmd(smp->conn, SMP_CMD_DHKEY_CHECK, sizeof(check), &check);
}

static u8 sc_passkey_send_confirm(struct smp_chan *smp)
{
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	struct smp_cmd_pairing_confirm cfm;
	u8 r;

	r = ((hcon->passkey_notify >> smp->passkey_round) & 0x01);
	r |= 0x80;

	get_random_bytes(smp->prnd, sizeof(smp->prnd));

	if (smp_f4(smp->tfm_cmac, smp->local_pk, smp->remote_pk, smp->prnd, r,
		   cfm.confirm_val))
		return SMP_UNSPECIFIED;

	smp_send_cmd(conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cfm), &cfm);

	return 0;
}

static u8 sc_passkey_round(struct smp_chan *smp, u8 smp_op)
{
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	u8 cfm[16], r;

	/* Ignore the PDU if we've already done 20 rounds (0 - 19) */
	if (smp->passkey_round >= 20)
		return 0;

	switch (smp_op) {
	case SMP_CMD_PAIRING_RANDOM:
		r = ((hcon->passkey_notify >> smp->passkey_round) & 0x01);
		r |= 0x80;

		if (smp_f4(smp->tfm_cmac, smp->remote_pk, smp->local_pk,
			   smp->rrnd, r, cfm))
			return SMP_UNSPECIFIED;

		if (crypto_memneq(smp->pcnf, cfm, 16))
			return SMP_CONFIRM_FAILED;

		smp->passkey_round++;

		if (smp->passkey_round == 20) {
			/* Generate MacKey and LTK */
			if (sc_mackey_and_ltk(smp, smp->mackey, smp->tk))
				return SMP_UNSPECIFIED;
		}

		/* The round is only complete when the initiator
		 * receives pairing random.
		 */
		if (!hcon->out) {
			smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM,
				     sizeof(smp->prnd), smp->prnd);
			if (smp->passkey_round == 20)
				SMP_ALLOW_CMD(smp, SMP_CMD_DHKEY_CHECK);
			else
				SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_CONFIRM);
			return 0;
		}

		/* Start the next round */
		if (smp->passkey_round != 20)
			return sc_passkey_round(smp, 0);

		/* Passkey rounds are complete - start DHKey Check */
		sc_dhkey_check(smp);
		SMP_ALLOW_CMD(smp, SMP_CMD_DHKEY_CHECK);

		break;

	case SMP_CMD_PAIRING_CONFIRM:
		if (test_bit(SMP_FLAG_WAIT_USER, &smp->flags)) {
			set_bit(SMP_FLAG_CFM_PENDING, &smp->flags);
			return 0;
		}

		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RANDOM);

		if (hcon->out) {
			smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM,
				     sizeof(smp->prnd), smp->prnd);
			return 0;
		}

		return sc_passkey_send_confirm(smp);

	case SMP_CMD_PUBLIC_KEY:
	default:
		/* Initiating device starts the round */
		if (!hcon->out)
			return 0;

		BT_DBG("%s Starting passkey round %u", hdev->name,
		       smp->passkey_round + 1);

		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_CONFIRM);

		return sc_passkey_send_confirm(smp);
	}

	return 0;
}

static int sc_user_reply(struct smp_chan *smp, u16 mgmt_op, __le32 passkey)
{
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	u8 smp_op;

	clear_bit(SMP_FLAG_WAIT_USER, &smp->flags);

	switch (mgmt_op) {
	case MGMT_OP_USER_PASSKEY_NEG_REPLY:
		smp_failure(smp->conn, SMP_PASSKEY_ENTRY_FAILED);
		return 0;
	case MGMT_OP_USER_CONFIRM_NEG_REPLY:
		smp_failure(smp->conn, SMP_NUMERIC_COMP_FAILED);
		return 0;
	case MGMT_OP_USER_PASSKEY_REPLY:
		hcon->passkey_notify = le32_to_cpu(passkey);
		smp->passkey_round = 0;

		if (test_and_clear_bit(SMP_FLAG_CFM_PENDING, &smp->flags))
			smp_op = SMP_CMD_PAIRING_CONFIRM;
		else
			smp_op = 0;

		if (sc_passkey_round(smp, smp_op))
			return -EIO;

		return 0;
	}

	/* Initiator sends DHKey check first */
	if (hcon->out) {
		sc_dhkey_check(smp);
		SMP_ALLOW_CMD(smp, SMP_CMD_DHKEY_CHECK);
	} else if (test_and_clear_bit(SMP_FLAG_DHKEY_PENDING, &smp->flags)) {
		sc_dhkey_check(smp);
		sc_add_ltk(smp);
	}

	return 0;
}

int smp_user_confirm_reply(struct hci_conn *hcon, u16 mgmt_op, __le32 passkey)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct l2cap_chan *chan;
	struct smp_chan *smp;
	u32 value;
	int err;

	BT_DBG("");

	if (!conn)
		return -ENOTCONN;

	chan = conn->smp;
	if (!chan)
		return -ENOTCONN;

	l2cap_chan_lock(chan);
	if (!chan->data) {
		err = -ENOTCONN;
		goto unlock;
	}

	smp = chan->data;

	if (test_bit(SMP_FLAG_SC, &smp->flags)) {
		err = sc_user_reply(smp, mgmt_op, passkey);
		goto unlock;
	}

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
		err = 0;
		goto unlock;
	default:
		smp_failure(conn, SMP_PASSKEY_ENTRY_FAILED);
		err = -EOPNOTSUPP;
		goto unlock;
	}

	err = 0;

	/* If it is our turn to send Pairing Confirm, do so now */
	if (test_bit(SMP_FLAG_CFM_PENDING, &smp->flags)) {
		u8 rsp = smp_confirm(smp);
		if (rsp)
			smp_failure(conn, rsp);
	}

unlock:
	l2cap_chan_unlock(chan);
	return err;
}

static void build_bredr_pairing_cmd(struct smp_chan *smp,
				    struct smp_cmd_pairing *req,
				    struct smp_cmd_pairing *rsp)
{
	struct l2cap_conn *conn = smp->conn;
	struct hci_dev *hdev = conn->hcon->hdev;
	u8 local_dist = 0, remote_dist = 0;

	if (hci_dev_test_flag(hdev, HCI_BONDABLE)) {
		local_dist = SMP_DIST_ENC_KEY | SMP_DIST_SIGN;
		remote_dist = SMP_DIST_ENC_KEY | SMP_DIST_SIGN;
	}

	if (hci_dev_test_flag(hdev, HCI_RPA_RESOLVING))
		remote_dist |= SMP_DIST_ID_KEY;

	if (hci_dev_test_flag(hdev, HCI_PRIVACY))
		local_dist |= SMP_DIST_ID_KEY;

	if (!rsp) {
		memset(req, 0, sizeof(*req));

		req->auth_req        = SMP_AUTH_CT2;
		req->init_key_dist   = local_dist;
		req->resp_key_dist   = remote_dist;
		req->max_key_size    = conn->hcon->enc_key_size;

		smp->remote_key_dist = remote_dist;

		return;
	}

	memset(rsp, 0, sizeof(*rsp));

	rsp->auth_req        = SMP_AUTH_CT2;
	rsp->max_key_size    = conn->hcon->enc_key_size;
	rsp->init_key_dist   = req->init_key_dist & remote_dist;
	rsp->resp_key_dist   = req->resp_key_dist & local_dist;

	smp->remote_key_dist = rsp->init_key_dist;
}

static u8 smp_cmd_pairing_req(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing rsp, *req = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct hci_dev *hdev = conn->hcon->hdev;
	struct smp_chan *smp;
	u8 key_size, auth, sec_level;
	int ret;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*req))
		return SMP_INVALID_PARAMS;

	if (conn->hcon->role != HCI_ROLE_SLAVE)
		return SMP_CMD_NOTSUPP;

	if (!chan->data)
		smp = smp_chan_create(conn);
	else
		smp = chan->data;

	if (!smp)
		return SMP_UNSPECIFIED;

	/* We didn't start the pairing, so match remote */
	auth = req->auth_req & AUTH_REQ_MASK(hdev);

	if (!hci_dev_test_flag(hdev, HCI_BONDABLE) &&
	    (auth & SMP_AUTH_BONDING))
		return SMP_PAIRING_NOTSUPP;

	if (hci_dev_test_flag(hdev, HCI_SC_ONLY) && !(auth & SMP_AUTH_SC))
		return SMP_AUTH_REQUIREMENTS;

	smp->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&smp->preq[1], req, sizeof(*req));
	skb_pull(skb, sizeof(*req));

	/* If the remote side's OOB flag is set it means it has
	 * successfully received our local OOB data - therefore set the
	 * flag to indicate that local OOB is in use.
	 */
	if (req->oob_flag == SMP_OOB_PRESENT && SMP_DEV(hdev)->local_oob)
		set_bit(SMP_FLAG_LOCAL_OOB, &smp->flags);

	/* SMP over BR/EDR requires special treatment */
	if (conn->hcon->type == ACL_LINK) {
		/* We must have a BR/EDR SC link */
		if (!test_bit(HCI_CONN_AES_CCM, &conn->hcon->flags) &&
		    !hci_dev_test_flag(hdev, HCI_FORCE_BREDR_SMP))
			return SMP_CROSS_TRANSP_NOT_ALLOWED;

		set_bit(SMP_FLAG_SC, &smp->flags);

		build_bredr_pairing_cmd(smp, req, &rsp);

		if (req->auth_req & SMP_AUTH_CT2)
			set_bit(SMP_FLAG_CT2, &smp->flags);

		key_size = min(req->max_key_size, rsp.max_key_size);
		if (check_enc_key_size(conn, key_size))
			return SMP_ENC_KEY_SIZE;

		/* Clear bits which are generated but not distributed */
		smp->remote_key_dist &= ~SMP_SC_NO_DIST;

		smp->prsp[0] = SMP_CMD_PAIRING_RSP;
		memcpy(&smp->prsp[1], &rsp, sizeof(rsp));
		smp_send_cmd(conn, SMP_CMD_PAIRING_RSP, sizeof(rsp), &rsp);

		smp_distribute_keys(smp);
		return 0;
	}

	build_pairing_cmd(conn, req, &rsp, auth);

	if (rsp.auth_req & SMP_AUTH_SC) {
		set_bit(SMP_FLAG_SC, &smp->flags);

		if (rsp.auth_req & SMP_AUTH_CT2)
			set_bit(SMP_FLAG_CT2, &smp->flags);
	}

	if (conn->hcon->io_capability == HCI_IO_NO_INPUT_OUTPUT)
		sec_level = BT_SECURITY_MEDIUM;
	else
		sec_level = authreq_to_seclevel(auth);

	if (sec_level > conn->hcon->pending_sec_level)
		conn->hcon->pending_sec_level = sec_level;

	/* If we need MITM check that it can be achieved */
	if (conn->hcon->pending_sec_level >= BT_SECURITY_HIGH) {
		u8 method;

		method = get_auth_method(smp, conn->hcon->io_capability,
					 req->io_capability);
		if (method == JUST_WORKS || method == JUST_CFM)
			return SMP_AUTH_REQUIREMENTS;
	}

	key_size = min(req->max_key_size, rsp.max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	get_random_bytes(smp->prnd, sizeof(smp->prnd));

	smp->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&smp->prsp[1], &rsp, sizeof(rsp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_RSP, sizeof(rsp), &rsp);

	clear_bit(SMP_FLAG_INITIATOR, &smp->flags);

	/* Strictly speaking we shouldn't allow Pairing Confirm for the
	 * SC case, however some implementations incorrectly copy RFU auth
	 * req bits from our security request, which may create a false
	 * positive SC enablement.
	 */
	SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_CONFIRM);

	if (test_bit(SMP_FLAG_SC, &smp->flags)) {
		SMP_ALLOW_CMD(smp, SMP_CMD_PUBLIC_KEY);
		/* Clear bits which are generated but not distributed */
		smp->remote_key_dist &= ~SMP_SC_NO_DIST;
		/* Wait for Public Key from Initiating Device */
		return 0;
	}

	/* Request setup of TK */
	ret = tk_request(conn, 0, auth, rsp.io_capability, req->io_capability);
	if (ret)
		return SMP_UNSPECIFIED;

	return 0;
}

static u8 sc_send_public_key(struct smp_chan *smp)
{
	struct hci_dev *hdev = smp->conn->hcon->hdev;

	BT_DBG("");

	if (test_bit(SMP_FLAG_LOCAL_OOB, &smp->flags)) {
		struct l2cap_chan *chan = hdev->smp_data;
		struct smp_dev *smp_dev;

		if (!chan || !chan->data)
			return SMP_UNSPECIFIED;

		smp_dev = chan->data;

		memcpy(smp->local_pk, smp_dev->local_pk, 64);
		memcpy(smp->lr, smp_dev->local_rand, 16);

		if (smp_dev->debug_key)
			set_bit(SMP_FLAG_DEBUG_KEY, &smp->flags);

		goto done;
	}

	if (hci_dev_test_flag(hdev, HCI_USE_DEBUG_KEYS)) {
		BT_DBG("Using debug keys");
		if (set_ecdh_privkey(smp->tfm_ecdh, debug_sk))
			return SMP_UNSPECIFIED;
		memcpy(smp->local_pk, debug_pk, 64);
		set_bit(SMP_FLAG_DEBUG_KEY, &smp->flags);
	} else {
		while (true) {
			/* Generate key pair for Secure Connections */
			if (generate_ecdh_keys(smp->tfm_ecdh, smp->local_pk))
				return SMP_UNSPECIFIED;

			/* This is unlikely, but we need to check that
			 * we didn't accidentially generate a debug key.
			 */
			if (crypto_memneq(smp->local_pk, debug_pk, 64))
				break;
		}
	}

done:
	SMP_DBG("Local Public Key X: %32phN", smp->local_pk);
	SMP_DBG("Local Public Key Y: %32phN", smp->local_pk + 32);

	smp_send_cmd(smp->conn, SMP_CMD_PUBLIC_KEY, 64, smp->local_pk);

	return 0;
}

static u8 smp_cmd_pairing_rsp(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_pairing *req, *rsp = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_dev *hdev = conn->hcon->hdev;
	u8 key_size, auth;
	int ret;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rsp))
		return SMP_INVALID_PARAMS;

	if (conn->hcon->role != HCI_ROLE_MASTER)
		return SMP_CMD_NOTSUPP;

	skb_pull(skb, sizeof(*rsp));

	req = (void *) &smp->preq[1];

	key_size = min(req->max_key_size, rsp->max_key_size);
	if (check_enc_key_size(conn, key_size))
		return SMP_ENC_KEY_SIZE;

	auth = rsp->auth_req & AUTH_REQ_MASK(hdev);

	if (hci_dev_test_flag(hdev, HCI_SC_ONLY) && !(auth & SMP_AUTH_SC))
		return SMP_AUTH_REQUIREMENTS;

	/* If the remote side's OOB flag is set it means it has
	 * successfully received our local OOB data - therefore set the
	 * flag to indicate that local OOB is in use.
	 */
	if (rsp->oob_flag == SMP_OOB_PRESENT && SMP_DEV(hdev)->local_oob)
		set_bit(SMP_FLAG_LOCAL_OOB, &smp->flags);

	smp->prsp[0] = SMP_CMD_PAIRING_RSP;
	memcpy(&smp->prsp[1], rsp, sizeof(*rsp));

	/* Update remote key distribution in case the remote cleared
	 * some bits that we had enabled in our request.
	 */
	smp->remote_key_dist &= rsp->resp_key_dist;

	if ((req->auth_req & SMP_AUTH_CT2) && (auth & SMP_AUTH_CT2))
		set_bit(SMP_FLAG_CT2, &smp->flags);

	/* For BR/EDR this means we're done and can start phase 3 */
	if (conn->hcon->type == ACL_LINK) {
		/* Clear bits which are generated but not distributed */
		smp->remote_key_dist &= ~SMP_SC_NO_DIST;
		smp_distribute_keys(smp);
		return 0;
	}

	if ((req->auth_req & SMP_AUTH_SC) && (auth & SMP_AUTH_SC))
		set_bit(SMP_FLAG_SC, &smp->flags);
	else if (conn->hcon->pending_sec_level > BT_SECURITY_HIGH)
		conn->hcon->pending_sec_level = BT_SECURITY_HIGH;

	/* If we need MITM check that it can be achieved */
	if (conn->hcon->pending_sec_level >= BT_SECURITY_HIGH) {
		u8 method;

		method = get_auth_method(smp, req->io_capability,
					 rsp->io_capability);
		if (method == JUST_WORKS || method == JUST_CFM)
			return SMP_AUTH_REQUIREMENTS;
	}

	get_random_bytes(smp->prnd, sizeof(smp->prnd));

	/* Update remote key distribution in case the remote cleared
	 * some bits that we had enabled in our request.
	 */
	smp->remote_key_dist &= rsp->resp_key_dist;

	if (test_bit(SMP_FLAG_SC, &smp->flags)) {
		/* Clear bits which are generated but not distributed */
		smp->remote_key_dist &= ~SMP_SC_NO_DIST;
		SMP_ALLOW_CMD(smp, SMP_CMD_PUBLIC_KEY);
		return sc_send_public_key(smp);
	}

	auth |= req->auth_req;

	ret = tk_request(conn, 0, auth, req->io_capability, rsp->io_capability);
	if (ret)
		return SMP_UNSPECIFIED;

	set_bit(SMP_FLAG_CFM_PENDING, &smp->flags);

	/* Can't compose response until we have been confirmed */
	if (test_bit(SMP_FLAG_TK_VALID, &smp->flags))
		return smp_confirm(smp);

	return 0;
}

static u8 sc_check_confirm(struct smp_chan *smp)
{
	struct l2cap_conn *conn = smp->conn;

	BT_DBG("");

	if (smp->method == REQ_PASSKEY || smp->method == DSP_PASSKEY)
		return sc_passkey_round(smp, SMP_CMD_PAIRING_CONFIRM);

	if (conn->hcon->out) {
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(smp->prnd),
			     smp->prnd);
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RANDOM);
	}

	return 0;
}

/* Work-around for some implementations that incorrectly copy RFU bits
 * from our security request and thereby create the impression that
 * we're doing SC when in fact the remote doesn't support it.
 */
static int fixup_sc_false_positive(struct smp_chan *smp)
{
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	struct smp_cmd_pairing *req, *rsp;
	u8 auth;

	/* The issue is only observed when we're in slave role */
	if (hcon->out)
		return SMP_UNSPECIFIED;

	if (hci_dev_test_flag(hdev, HCI_SC_ONLY)) {
		bt_dev_err(hdev, "refusing legacy fallback in SC-only mode");
		return SMP_UNSPECIFIED;
	}

	bt_dev_err(hdev, "trying to fall back to legacy SMP");

	req = (void *) &smp->preq[1];
	rsp = (void *) &smp->prsp[1];

	/* Rebuild key dist flags which may have been cleared for SC */
	smp->remote_key_dist = (req->init_key_dist & rsp->resp_key_dist);

	auth = req->auth_req & AUTH_REQ_MASK(hdev);

	if (tk_request(conn, 0, auth, rsp->io_capability, req->io_capability)) {
		bt_dev_err(hdev, "failed to fall back to legacy SMP");
		return SMP_UNSPECIFIED;
	}

	clear_bit(SMP_FLAG_SC, &smp->flags);

	return 0;
}

static u8 smp_cmd_pairing_confirm(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;

	BT_DBG("conn %p %s", conn, conn->hcon->out ? "master" : "slave");

	if (skb->len < sizeof(smp->pcnf))
		return SMP_INVALID_PARAMS;

	memcpy(smp->pcnf, skb->data, sizeof(smp->pcnf));
	skb_pull(skb, sizeof(smp->pcnf));

	if (test_bit(SMP_FLAG_SC, &smp->flags)) {
		int ret;

		/* Public Key exchange must happen before any other steps */
		if (test_bit(SMP_FLAG_REMOTE_PK, &smp->flags))
			return sc_check_confirm(smp);

		BT_ERR("Unexpected SMP Pairing Confirm");

		ret = fixup_sc_false_positive(smp);
		if (ret)
			return ret;
	}

	if (conn->hcon->out) {
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(smp->prnd),
			     smp->prnd);
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RANDOM);
		return 0;
	}

	if (test_bit(SMP_FLAG_TK_VALID, &smp->flags))
		return smp_confirm(smp);

	set_bit(SMP_FLAG_CFM_PENDING, &smp->flags);

	return 0;
}

static u8 smp_cmd_pairing_random(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_conn *hcon = conn->hcon;
	u8 *pkax, *pkbx, *na, *nb;
	u32 passkey;
	int err;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(smp->rrnd))
		return SMP_INVALID_PARAMS;

	memcpy(smp->rrnd, skb->data, sizeof(smp->rrnd));
	skb_pull(skb, sizeof(smp->rrnd));

	if (!test_bit(SMP_FLAG_SC, &smp->flags))
		return smp_random(smp);

	if (hcon->out) {
		pkax = smp->local_pk;
		pkbx = smp->remote_pk;
		na   = smp->prnd;
		nb   = smp->rrnd;
	} else {
		pkax = smp->remote_pk;
		pkbx = smp->local_pk;
		na   = smp->rrnd;
		nb   = smp->prnd;
	}

	if (smp->method == REQ_OOB) {
		if (!hcon->out)
			smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM,
				     sizeof(smp->prnd), smp->prnd);
		SMP_ALLOW_CMD(smp, SMP_CMD_DHKEY_CHECK);
		goto mackey_and_ltk;
	}

	/* Passkey entry has special treatment */
	if (smp->method == REQ_PASSKEY || smp->method == DSP_PASSKEY)
		return sc_passkey_round(smp, SMP_CMD_PAIRING_RANDOM);

	if (hcon->out) {
		u8 cfm[16];

		err = smp_f4(smp->tfm_cmac, smp->remote_pk, smp->local_pk,
			     smp->rrnd, 0, cfm);
		if (err)
			return SMP_UNSPECIFIED;

		if (crypto_memneq(smp->pcnf, cfm, 16))
			return SMP_CONFIRM_FAILED;
	} else {
		smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM, sizeof(smp->prnd),
			     smp->prnd);
		SMP_ALLOW_CMD(smp, SMP_CMD_DHKEY_CHECK);
	}

mackey_and_ltk:
	/* Generate MacKey and LTK */
	err = sc_mackey_and_ltk(smp, smp->mackey, smp->tk);
	if (err)
		return SMP_UNSPECIFIED;

	if (smp->method == JUST_WORKS || smp->method == REQ_OOB) {
		if (hcon->out) {
			sc_dhkey_check(smp);
			SMP_ALLOW_CMD(smp, SMP_CMD_DHKEY_CHECK);
		}
		return 0;
	}

	err = smp_g2(smp->tfm_cmac, pkax, pkbx, na, nb, &passkey);
	if (err)
		return SMP_UNSPECIFIED;

	err = mgmt_user_confirm_request(hcon->hdev, &hcon->dst, hcon->type,
					hcon->dst_type, passkey, 0);
	if (err)
		return SMP_UNSPECIFIED;

	set_bit(SMP_FLAG_WAIT_USER, &smp->flags);

	return 0;
}

static bool smp_ltk_encrypt(struct l2cap_conn *conn, u8 sec_level)
{
	struct smp_ltk *key;
	struct hci_conn *hcon = conn->hcon;

	key = hci_find_ltk(hcon->hdev, &hcon->dst, hcon->dst_type, hcon->role);
	if (!key)
		return false;

	if (smp_ltk_sec_level(key) < sec_level)
		return false;

	if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &hcon->flags))
		return true;

	hci_le_start_enc(hcon, key->ediv, key->rand, key->val, key->enc_size);
	hcon->enc_key_size = key->enc_size;

	/* We never store STKs for master role, so clear this flag */
	clear_bit(HCI_CONN_STK_ENCRYPT, &hcon->flags);

	return true;
}

bool smp_sufficient_security(struct hci_conn *hcon, u8 sec_level,
			     enum smp_key_pref key_pref)
{
	if (sec_level == BT_SECURITY_LOW)
		return true;

	/* If we're encrypted with an STK but the caller prefers using
	 * LTK claim insufficient security. This way we allow the
	 * connection to be re-encrypted with an LTK, even if the LTK
	 * provides the same level of security. Only exception is if we
	 * don't have an LTK (e.g. because of key distribution bits).
	 */
	if (key_pref == SMP_USE_LTK &&
	    test_bit(HCI_CONN_STK_ENCRYPT, &hcon->flags) &&
	    hci_find_ltk(hcon->hdev, &hcon->dst, hcon->dst_type, hcon->role))
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
	struct hci_dev *hdev = hcon->hdev;
	struct smp_chan *smp;
	u8 sec_level, auth;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	if (hcon->role != HCI_ROLE_MASTER)
		return SMP_CMD_NOTSUPP;

	auth = rp->auth_req & AUTH_REQ_MASK(hdev);

	if (hci_dev_test_flag(hdev, HCI_SC_ONLY) && !(auth & SMP_AUTH_SC))
		return SMP_AUTH_REQUIREMENTS;

	if (hcon->io_capability == HCI_IO_NO_INPUT_OUTPUT)
		sec_level = BT_SECURITY_MEDIUM;
	else
		sec_level = authreq_to_seclevel(auth);

	if (smp_sufficient_security(hcon, sec_level, SMP_USE_LTK)) {
		/* If link is already encrypted with sufficient security we
		 * still need refresh encryption as per Core Spec 5.0 Vol 3,
		 * Part H 2.4.6
		 */
		smp_ltk_encrypt(conn, hcon->sec_level);
		return 0;
	}

	if (sec_level > hcon->pending_sec_level)
		hcon->pending_sec_level = sec_level;

	if (smp_ltk_encrypt(conn, hcon->pending_sec_level))
		return 0;

	smp = smp_chan_create(conn);
	if (!smp)
		return SMP_UNSPECIFIED;

	if (!hci_dev_test_flag(hdev, HCI_BONDABLE) &&
	    (auth & SMP_AUTH_BONDING))
		return SMP_PAIRING_NOTSUPP;

	skb_pull(skb, sizeof(*rp));

	memset(&cp, 0, sizeof(cp));
	build_pairing_cmd(conn, &cp, NULL, auth);

	smp->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&smp->preq[1], &cp, sizeof(cp));

	smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);
	SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RSP);

	return 0;
}

int smp_conn_security(struct hci_conn *hcon, __u8 sec_level)
{
	struct l2cap_conn *conn = hcon->l2cap_data;
	struct l2cap_chan *chan;
	struct smp_chan *smp;
	__u8 authreq;
	int ret;

	BT_DBG("conn %p hcon %p level 0x%2.2x", conn, hcon, sec_level);

	/* This may be NULL if there's an unexpected disconnection */
	if (!conn)
		return 1;

	if (!hci_dev_test_flag(hcon->hdev, HCI_LE_ENABLED))
		return 1;

	if (smp_sufficient_security(hcon, sec_level, SMP_USE_LTK))
		return 1;

	if (sec_level > hcon->pending_sec_level)
		hcon->pending_sec_level = sec_level;

	if (hcon->role == HCI_ROLE_MASTER)
		if (smp_ltk_encrypt(conn, hcon->pending_sec_level))
			return 0;

	chan = conn->smp;
	if (!chan) {
		bt_dev_err(hcon->hdev, "security requested but not available");
		return 1;
	}

	l2cap_chan_lock(chan);

	/* If SMP is already in progress ignore this request */
	if (chan->data) {
		ret = 0;
		goto unlock;
	}

	smp = smp_chan_create(conn);
	if (!smp) {
		ret = 1;
		goto unlock;
	}

	authreq = seclevel_to_authreq(sec_level);

	if (hci_dev_test_flag(hcon->hdev, HCI_SC_ENABLED)) {
		authreq |= SMP_AUTH_SC;
		if (hci_dev_test_flag(hcon->hdev, HCI_SSP_ENABLED))
			authreq |= SMP_AUTH_CT2;
	}

	/* Require MITM if IO Capability allows or the security level
	 * requires it.
	 */
	if (hcon->io_capability != HCI_IO_NO_INPUT_OUTPUT ||
	    hcon->pending_sec_level > BT_SECURITY_MEDIUM)
		authreq |= SMP_AUTH_MITM;

	if (hcon->role == HCI_ROLE_MASTER) {
		struct smp_cmd_pairing cp;

		build_pairing_cmd(conn, &cp, NULL, authreq);
		smp->preq[0] = SMP_CMD_PAIRING_REQ;
		memcpy(&smp->preq[1], &cp, sizeof(cp));

		smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(cp), &cp);
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RSP);
	} else {
		struct smp_cmd_security_req cp;
		cp.auth_req = authreq;
		smp_send_cmd(conn, SMP_CMD_SECURITY_REQ, sizeof(cp), &cp);
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_REQ);
	}

	set_bit(SMP_FLAG_INITIATOR, &smp->flags);
	ret = 0;

unlock:
	l2cap_chan_unlock(chan);
	return ret;
}

int smp_cancel_and_remove_pairing(struct hci_dev *hdev, bdaddr_t *bdaddr,
				  u8 addr_type)
{
	struct hci_conn *hcon;
	struct l2cap_conn *conn;
	struct l2cap_chan *chan;
	struct smp_chan *smp;
	int err;

	err = hci_remove_ltk(hdev, bdaddr, addr_type);
	hci_remove_irk(hdev, bdaddr, addr_type);

	hcon = hci_conn_hash_lookup_le(hdev, bdaddr, addr_type);
	if (!hcon)
		goto done;

	conn = hcon->l2cap_data;
	if (!conn)
		goto done;

	chan = conn->smp;
	if (!chan)
		goto done;

	l2cap_chan_lock(chan);

	smp = chan->data;
	if (smp) {
		/* Set keys to NULL to make sure smp_failure() does not try to
		 * remove and free already invalidated rcu list entries. */
		smp->ltk = NULL;
		smp->slave_ltk = NULL;
		smp->remote_irk = NULL;

		if (test_bit(SMP_FLAG_COMPLETE, &smp->flags))
			smp_failure(conn, 0);
		else
			smp_failure(conn, SMP_UNSPECIFIED);
		err = 0;
	}

	l2cap_chan_unlock(chan);

done:
	return err;
}

static int smp_cmd_encrypt_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_encrypt_info *rp = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	SMP_ALLOW_CMD(smp, SMP_CMD_MASTER_IDENT);

	skb_pull(skb, sizeof(*rp));

	memcpy(smp->tk, rp->ltk, sizeof(smp->tk));

	return 0;
}

static int smp_cmd_master_ident(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_master_ident *rp = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_dev *hdev = conn->hcon->hdev;
	struct hci_conn *hcon = conn->hcon;
	struct smp_ltk *ltk;
	u8 authenticated;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	/* Mark the information as received */
	smp->remote_key_dist &= ~SMP_DIST_ENC_KEY;

	if (smp->remote_key_dist & SMP_DIST_ID_KEY)
		SMP_ALLOW_CMD(smp, SMP_CMD_IDENT_INFO);
	else if (smp->remote_key_dist & SMP_DIST_SIGN)
		SMP_ALLOW_CMD(smp, SMP_CMD_SIGN_INFO);

	skb_pull(skb, sizeof(*rp));

	authenticated = (hcon->sec_level == BT_SECURITY_HIGH);
	ltk = hci_add_ltk(hdev, &hcon->dst, hcon->dst_type, SMP_LTK,
			  authenticated, smp->tk, smp->enc_key_size,
			  rp->ediv, rp->rand);
	smp->ltk = ltk;
	if (!(smp->remote_key_dist & KEY_DIST_MASK))
		smp_distribute_keys(smp);

	return 0;
}

static int smp_cmd_ident_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_ident_info *info = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;

	BT_DBG("");

	if (skb->len < sizeof(*info))
		return SMP_INVALID_PARAMS;

	SMP_ALLOW_CMD(smp, SMP_CMD_IDENT_ADDR_INFO);

	skb_pull(skb, sizeof(*info));

	memcpy(smp->irk, info->irk, 16);

	return 0;
}

static int smp_cmd_ident_addr_info(struct l2cap_conn *conn,
				   struct sk_buff *skb)
{
	struct smp_cmd_ident_addr_info *info = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_conn *hcon = conn->hcon;
	bdaddr_t rpa;

	BT_DBG("");

	if (skb->len < sizeof(*info))
		return SMP_INVALID_PARAMS;

	/* Mark the information as received */
	smp->remote_key_dist &= ~SMP_DIST_ID_KEY;

	if (smp->remote_key_dist & SMP_DIST_SIGN)
		SMP_ALLOW_CMD(smp, SMP_CMD_SIGN_INFO);

	skb_pull(skb, sizeof(*info));

	/* Strictly speaking the Core Specification (4.1) allows sending
	 * an empty address which would force us to rely on just the IRK
	 * as "identity information". However, since such
	 * implementations are not known of and in order to not over
	 * complicate our implementation, simply pretend that we never
	 * received an IRK for such a device.
	 *
	 * The Identity Address must also be a Static Random or Public
	 * Address, which hci_is_identity_address() checks for.
	 */
	if (!bacmp(&info->bdaddr, BDADDR_ANY) ||
	    !hci_is_identity_address(&info->bdaddr, info->addr_type)) {
		bt_dev_err(hcon->hdev, "ignoring IRK with no identity address");
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
	if (!(smp->remote_key_dist & KEY_DIST_MASK))
		smp_distribute_keys(smp);

	return 0;
}

static int smp_cmd_sign_info(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_sign_info *rp = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct smp_csrk *csrk;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*rp))
		return SMP_INVALID_PARAMS;

	/* Mark the information as received */
	smp->remote_key_dist &= ~SMP_DIST_SIGN;

	skb_pull(skb, sizeof(*rp));

	csrk = kzalloc(sizeof(*csrk), GFP_KERNEL);
	if (csrk) {
		if (conn->hcon->sec_level > BT_SECURITY_MEDIUM)
			csrk->type = MGMT_CSRK_REMOTE_AUTHENTICATED;
		else
			csrk->type = MGMT_CSRK_REMOTE_UNAUTHENTICATED;
		memcpy(csrk->val, rp->csrk, sizeof(csrk->val));
	}
	smp->csrk = csrk;
	smp_distribute_keys(smp);

	return 0;
}

static u8 sc_select_method(struct smp_chan *smp)
{
	struct l2cap_conn *conn = smp->conn;
	struct hci_conn *hcon = conn->hcon;
	struct smp_cmd_pairing *local, *remote;
	u8 local_mitm, remote_mitm, local_io, remote_io, method;

	if (test_bit(SMP_FLAG_REMOTE_OOB, &smp->flags) ||
	    test_bit(SMP_FLAG_LOCAL_OOB, &smp->flags))
		return REQ_OOB;

	/* The preq/prsp contain the raw Pairing Request/Response PDUs
	 * which are needed as inputs to some crypto functions. To get
	 * the "struct smp_cmd_pairing" from them we need to skip the
	 * first byte which contains the opcode.
	 */
	if (hcon->out) {
		local = (void *) &smp->preq[1];
		remote = (void *) &smp->prsp[1];
	} else {
		local = (void *) &smp->prsp[1];
		remote = (void *) &smp->preq[1];
	}

	local_io = local->io_capability;
	remote_io = remote->io_capability;

	local_mitm = (local->auth_req & SMP_AUTH_MITM);
	remote_mitm = (remote->auth_req & SMP_AUTH_MITM);

	/* If either side wants MITM, look up the method from the table,
	 * otherwise use JUST WORKS.
	 */
	if (local_mitm || remote_mitm)
		method = get_auth_method(smp, local_io, remote_io);
	else
		method = JUST_WORKS;

	/* Don't confirm locally initiated pairing attempts */
	if (method == JUST_CFM && test_bit(SMP_FLAG_INITIATOR, &smp->flags))
		method = JUST_WORKS;

	return method;
}

static int smp_cmd_public_key(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_public_key *key = (void *) skb->data;
	struct hci_conn *hcon = conn->hcon;
	struct l2cap_chan *chan = conn->smp;
	struct smp_chan *smp = chan->data;
	struct hci_dev *hdev = hcon->hdev;
	struct crypto_kpp *tfm_ecdh;
	struct smp_cmd_pairing_confirm cfm;
	int err;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*key))
		return SMP_INVALID_PARAMS;

	memcpy(smp->remote_pk, key, 64);

	if (test_bit(SMP_FLAG_REMOTE_OOB, &smp->flags)) {
		err = smp_f4(smp->tfm_cmac, smp->remote_pk, smp->remote_pk,
			     smp->rr, 0, cfm.confirm_val);
		if (err)
			return SMP_UNSPECIFIED;

		if (crypto_memneq(cfm.confirm_val, smp->pcnf, 16))
			return SMP_CONFIRM_FAILED;
	}

	/* Non-initiating device sends its public key after receiving
	 * the key from the initiating device.
	 */
	if (!hcon->out) {
		err = sc_send_public_key(smp);
		if (err)
			return err;
	}

	SMP_DBG("Remote Public Key X: %32phN", smp->remote_pk);
	SMP_DBG("Remote Public Key Y: %32phN", smp->remote_pk + 32);

	/* Compute the shared secret on the same crypto tfm on which the private
	 * key was set/generated.
	 */
	if (test_bit(SMP_FLAG_LOCAL_OOB, &smp->flags)) {
		struct l2cap_chan *hchan = hdev->smp_data;
		struct smp_dev *smp_dev;

		if (!hchan || !hchan->data)
			return SMP_UNSPECIFIED;

		smp_dev = hchan->data;

		tfm_ecdh = smp_dev->tfm_ecdh;
	} else {
		tfm_ecdh = smp->tfm_ecdh;
	}

	if (compute_ecdh_secret(tfm_ecdh, smp->remote_pk, smp->dhkey))
		return SMP_UNSPECIFIED;

	SMP_DBG("DHKey %32phN", smp->dhkey);

	set_bit(SMP_FLAG_REMOTE_PK, &smp->flags);

	smp->method = sc_select_method(smp);

	BT_DBG("%s selected method 0x%02x", hdev->name, smp->method);

	/* JUST_WORKS and JUST_CFM result in an unauthenticated key */
	if (smp->method == JUST_WORKS || smp->method == JUST_CFM)
		hcon->pending_sec_level = BT_SECURITY_MEDIUM;
	else
		hcon->pending_sec_level = BT_SECURITY_FIPS;

	if (!crypto_memneq(debug_pk, smp->remote_pk, 64))
		set_bit(SMP_FLAG_DEBUG_KEY, &smp->flags);

	if (smp->method == DSP_PASSKEY) {
		get_random_bytes(&hcon->passkey_notify,
				 sizeof(hcon->passkey_notify));
		hcon->passkey_notify %= 1000000;
		hcon->passkey_entered = 0;
		smp->passkey_round = 0;
		if (mgmt_user_passkey_notify(hdev, &hcon->dst, hcon->type,
					     hcon->dst_type,
					     hcon->passkey_notify,
					     hcon->passkey_entered))
			return SMP_UNSPECIFIED;
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_CONFIRM);
		return sc_passkey_round(smp, SMP_CMD_PUBLIC_KEY);
	}

	if (smp->method == REQ_OOB) {
		if (hcon->out)
			smp_send_cmd(conn, SMP_CMD_PAIRING_RANDOM,
				     sizeof(smp->prnd), smp->prnd);

		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RANDOM);

		return 0;
	}

	if (hcon->out)
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_CONFIRM);

	if (smp->method == REQ_PASSKEY) {
		if (mgmt_user_passkey_request(hdev, &hcon->dst, hcon->type,
					      hcon->dst_type))
			return SMP_UNSPECIFIED;
		SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_CONFIRM);
		set_bit(SMP_FLAG_WAIT_USER, &smp->flags);
		return 0;
	}

	/* The Initiating device waits for the non-initiating device to
	 * send the confirm value.
	 */
	if (conn->hcon->out)
		return 0;

	err = smp_f4(smp->tfm_cmac, smp->local_pk, smp->remote_pk, smp->prnd,
		     0, cfm.confirm_val);
	if (err)
		return SMP_UNSPECIFIED;

	smp_send_cmd(conn, SMP_CMD_PAIRING_CONFIRM, sizeof(cfm), &cfm);
	SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RANDOM);

	return 0;
}

static int smp_cmd_dhkey_check(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct smp_cmd_dhkey_check *check = (void *) skb->data;
	struct l2cap_chan *chan = conn->smp;
	struct hci_conn *hcon = conn->hcon;
	struct smp_chan *smp = chan->data;
	u8 a[7], b[7], *local_addr, *remote_addr;
	u8 io_cap[3], r[16], e[16];
	int err;

	BT_DBG("conn %p", conn);

	if (skb->len < sizeof(*check))
		return SMP_INVALID_PARAMS;

	memcpy(a, &hcon->init_addr, 6);
	memcpy(b, &hcon->resp_addr, 6);
	a[6] = hcon->init_addr_type;
	b[6] = hcon->resp_addr_type;

	if (hcon->out) {
		local_addr = a;
		remote_addr = b;
		memcpy(io_cap, &smp->prsp[1], 3);
	} else {
		local_addr = b;
		remote_addr = a;
		memcpy(io_cap, &smp->preq[1], 3);
	}

	memset(r, 0, sizeof(r));

	if (smp->method == REQ_PASSKEY || smp->method == DSP_PASSKEY)
		put_unaligned_le32(hcon->passkey_notify, r);
	else if (smp->method == REQ_OOB)
		memcpy(r, smp->lr, 16);

	err = smp_f6(smp->tfm_cmac, smp->mackey, smp->rrnd, smp->prnd, r,
		     io_cap, remote_addr, local_addr, e);
	if (err)
		return SMP_UNSPECIFIED;

	if (crypto_memneq(check->e, e, 16))
		return SMP_DHKEY_CHECK_FAILED;

	if (!hcon->out) {
		if (test_bit(SMP_FLAG_WAIT_USER, &smp->flags)) {
			set_bit(SMP_FLAG_DHKEY_PENDING, &smp->flags);
			return 0;
		}

		/* Slave sends DHKey check as response to master */
		sc_dhkey_check(smp);
	}

	sc_add_ltk(smp);

	if (hcon->out) {
		hci_le_start_enc(hcon, 0, 0, smp->tk, smp->enc_key_size);
		hcon->enc_key_size = smp->enc_key_size;
	}

	return 0;
}

static int smp_cmd_keypress_notify(struct l2cap_conn *conn,
				   struct sk_buff *skb)
{
	struct smp_cmd_keypress_notify *kp = (void *) skb->data;

	BT_DBG("value 0x%02x", kp->value);

	return 0;
}

static int smp_sig_channel(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct l2cap_conn *conn = chan->conn;
	struct hci_conn *hcon = conn->hcon;
	struct smp_chan *smp;
	__u8 code, reason;
	int err = 0;

	if (skb->len < 1)
		return -EILSEQ;

	if (!hci_dev_test_flag(hcon->hdev, HCI_LE_ENABLED)) {
		reason = SMP_PAIRING_NOTSUPP;
		goto done;
	}

	code = skb->data[0];
	skb_pull(skb, sizeof(code));

	smp = chan->data;

	if (code > SMP_CMD_MAX)
		goto drop;

	if (smp && !test_and_clear_bit(code, &smp->allow_cmd))
		goto drop;

	/* If we don't have a context the only allowed commands are
	 * pairing request and security request.
	 */
	if (!smp && code != SMP_CMD_PAIRING_REQ && code != SMP_CMD_SECURITY_REQ)
		goto drop;

	switch (code) {
	case SMP_CMD_PAIRING_REQ:
		reason = smp_cmd_pairing_req(conn, skb);
		break;

	case SMP_CMD_PAIRING_FAIL:
		smp_failure(conn, 0);
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

	case SMP_CMD_PUBLIC_KEY:
		reason = smp_cmd_public_key(conn, skb);
		break;

	case SMP_CMD_DHKEY_CHECK:
		reason = smp_cmd_dhkey_check(conn, skb);
		break;

	case SMP_CMD_KEYPRESS_NOTIFY:
		reason = smp_cmd_keypress_notify(conn, skb);
		break;

	default:
		BT_DBG("Unknown command code 0x%2.2x", code);
		reason = SMP_CMD_NOTSUPP;
		goto done;
	}

done:
	if (!err) {
		if (reason)
			smp_failure(conn, reason);
		kfree_skb(skb);
	}

	return err;

drop:
	bt_dev_err(hcon->hdev, "unexpected SMP command 0x%02x from %pMR",
		   code, &hcon->dst);
	kfree_skb(skb);
	return 0;
}

static void smp_teardown_cb(struct l2cap_chan *chan, int err)
{
	struct l2cap_conn *conn = chan->conn;

	BT_DBG("chan %p", chan);

	if (chan->data)
		smp_chan_destroy(conn);

	conn->smp = NULL;
	l2cap_chan_put(chan);
}

static void bredr_pairing(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct hci_conn *hcon = conn->hcon;
	struct hci_dev *hdev = hcon->hdev;
	struct smp_cmd_pairing req;
	struct smp_chan *smp;

	BT_DBG("chan %p", chan);

	/* Only new pairings are interesting */
	if (!test_bit(HCI_CONN_NEW_LINK_KEY, &hcon->flags))
		return;

	/* Don't bother if we're not encrypted */
	if (!test_bit(HCI_CONN_ENCRYPT, &hcon->flags))
		return;

	/* Only master may initiate SMP over BR/EDR */
	if (hcon->role != HCI_ROLE_MASTER)
		return;

	/* Secure Connections support must be enabled */
	if (!hci_dev_test_flag(hdev, HCI_SC_ENABLED))
		return;

	/* BR/EDR must use Secure Connections for SMP */
	if (!test_bit(HCI_CONN_AES_CCM, &hcon->flags) &&
	    !hci_dev_test_flag(hdev, HCI_FORCE_BREDR_SMP))
		return;

	/* If our LE support is not enabled don't do anything */
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return;

	/* Don't bother if remote LE support is not enabled */
	if (!lmp_host_le_capable(hcon))
		return;

	/* Remote must support SMP fixed chan for BR/EDR */
	if (!(conn->remote_fixed_chan & L2CAP_FC_SMP_BREDR))
		return;

	/* Don't bother if SMP is already ongoing */
	if (chan->data)
		return;

	smp = smp_chan_create(conn);
	if (!smp) {
		bt_dev_err(hdev, "unable to create SMP context for BR/EDR");
		return;
	}

	set_bit(SMP_FLAG_SC, &smp->flags);

	BT_DBG("%s starting SMP over BR/EDR", hdev->name);

	/* Prepare and send the BR/EDR SMP Pairing Request */
	build_bredr_pairing_cmd(smp, &req, NULL);

	smp->preq[0] = SMP_CMD_PAIRING_REQ;
	memcpy(&smp->preq[1], &req, sizeof(req));

	smp_send_cmd(conn, SMP_CMD_PAIRING_REQ, sizeof(req), &req);
	SMP_ALLOW_CMD(smp, SMP_CMD_PAIRING_RSP);
}

static void smp_resume_cb(struct l2cap_chan *chan)
{
	struct smp_chan *smp = chan->data;
	struct l2cap_conn *conn = chan->conn;
	struct hci_conn *hcon = conn->hcon;

	BT_DBG("chan %p", chan);

	if (hcon->type == ACL_LINK) {
		bredr_pairing(chan);
		return;
	}

	if (!smp)
		return;

	if (!test_bit(HCI_CONN_ENCRYPT, &hcon->flags))
		return;

	cancel_delayed_work(&smp->security_timer);

	smp_distribute_keys(smp);
}

static void smp_ready_cb(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct hci_conn *hcon = conn->hcon;

	BT_DBG("chan %p", chan);

	/* No need to call l2cap_chan_hold() here since we already own
	 * the reference taken in smp_new_conn_cb(). This is just the
	 * first time that we tie it to a specific pointer. The code in
	 * l2cap_core.c ensures that there's no risk this function wont
	 * get called if smp_new_conn_cb was previously called.
	 */
	conn->smp = chan;

	if (hcon->type == ACL_LINK && test_bit(HCI_CONN_ENCRYPT, &hcon->flags))
		bredr_pairing(chan);
}

static int smp_recv_cb(struct l2cap_chan *chan, struct sk_buff *skb)
{
	int err;

	BT_DBG("chan %p", chan);

	err = smp_sig_channel(chan, skb);
	if (err) {
		struct smp_chan *smp = chan->data;

		if (smp)
			cancel_delayed_work_sync(&smp->security_timer);

		hci_disconnect(chan->conn->hcon, HCI_ERROR_AUTH_FAILURE);
	}

	return err;
}

static struct sk_buff *smp_alloc_skb_cb(struct l2cap_chan *chan,
					unsigned long hdr_len,
					unsigned long len, int nb)
{
	struct sk_buff *skb;

	skb = bt_skb_alloc(hdr_len + len, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb->priority = HCI_PRIO_MAX;
	bt_cb(skb)->l2cap.chan = chan;

	return skb;
}

static const struct l2cap_ops smp_chan_ops = {
	.name			= "Security Manager",
	.ready			= smp_ready_cb,
	.recv			= smp_recv_cb,
	.alloc_skb		= smp_alloc_skb_cb,
	.teardown		= smp_teardown_cb,
	.resume			= smp_resume_cb,

	.new_connection		= l2cap_chan_no_new_connection,
	.state_change		= l2cap_chan_no_state_change,
	.close			= l2cap_chan_no_close,
	.defer			= l2cap_chan_no_defer,
	.suspend		= l2cap_chan_no_suspend,
	.set_shutdown		= l2cap_chan_no_set_shutdown,
	.get_sndtimeo		= l2cap_chan_no_get_sndtimeo,
};

static inline struct l2cap_chan *smp_new_conn_cb(struct l2cap_chan *pchan)
{
	struct l2cap_chan *chan;

	BT_DBG("pchan %p", pchan);

	chan = l2cap_chan_create();
	if (!chan)
		return NULL;

	chan->chan_type	= pchan->chan_type;
	chan->ops	= &smp_chan_ops;
	chan->scid	= pchan->scid;
	chan->dcid	= chan->scid;
	chan->imtu	= pchan->imtu;
	chan->omtu	= pchan->omtu;
	chan->mode	= pchan->mode;

	/* Other L2CAP channels may request SMP routines in order to
	 * change the security level. This means that the SMP channel
	 * lock must be considered in its own category to avoid lockdep
	 * warnings.
	 */
	atomic_set(&chan->nesting, L2CAP_NESTING_SMP);

	BT_DBG("created chan %p", chan);

	return chan;
}

static const struct l2cap_ops smp_root_chan_ops = {
	.name			= "Security Manager Root",
	.new_connection		= smp_new_conn_cb,

	/* None of these are implemented for the root channel */
	.close			= l2cap_chan_no_close,
	.alloc_skb		= l2cap_chan_no_alloc_skb,
	.recv			= l2cap_chan_no_recv,
	.state_change		= l2cap_chan_no_state_change,
	.teardown		= l2cap_chan_no_teardown,
	.ready			= l2cap_chan_no_ready,
	.defer			= l2cap_chan_no_defer,
	.suspend		= l2cap_chan_no_suspend,
	.resume			= l2cap_chan_no_resume,
	.set_shutdown		= l2cap_chan_no_set_shutdown,
	.get_sndtimeo		= l2cap_chan_no_get_sndtimeo,
};

static struct l2cap_chan *smp_add_cid(struct hci_dev *hdev, u16 cid)
{
	struct l2cap_chan *chan;
	struct smp_dev *smp;
	struct crypto_cipher *tfm_aes;
	struct crypto_shash *tfm_cmac;
	struct crypto_kpp *tfm_ecdh;

	if (cid == L2CAP_CID_SMP_BREDR) {
		smp = NULL;
		goto create_chan;
	}

	smp = kzalloc(sizeof(*smp), GFP_KERNEL);
	if (!smp)
		return ERR_PTR(-ENOMEM);

	tfm_aes = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(tfm_aes)) {
		BT_ERR("Unable to create AES crypto context");
		kzfree(smp);
		return ERR_CAST(tfm_aes);
	}

	tfm_cmac = crypto_alloc_shash("cmac(aes)", 0, 0);
	if (IS_ERR(tfm_cmac)) {
		BT_ERR("Unable to create CMAC crypto context");
		crypto_free_cipher(tfm_aes);
		kzfree(smp);
		return ERR_CAST(tfm_cmac);
	}

	tfm_ecdh = crypto_alloc_kpp("ecdh", CRYPTO_ALG_INTERNAL, 0);
	if (IS_ERR(tfm_ecdh)) {
		BT_ERR("Unable to create ECDH crypto context");
		crypto_free_shash(tfm_cmac);
		crypto_free_cipher(tfm_aes);
		kzfree(smp);
		return ERR_CAST(tfm_ecdh);
	}

	smp->local_oob = false;
	smp->tfm_aes = tfm_aes;
	smp->tfm_cmac = tfm_cmac;
	smp->tfm_ecdh = tfm_ecdh;

create_chan:
	chan = l2cap_chan_create();
	if (!chan) {
		if (smp) {
			crypto_free_cipher(smp->tfm_aes);
			crypto_free_shash(smp->tfm_cmac);
			crypto_free_kpp(smp->tfm_ecdh);
			kzfree(smp);
		}
		return ERR_PTR(-ENOMEM);
	}

	chan->data = smp;

	l2cap_add_scid(chan, cid);

	l2cap_chan_set_defaults(chan);

	if (cid == L2CAP_CID_SMP) {
		u8 bdaddr_type;

		hci_copy_identity_address(hdev, &chan->src, &bdaddr_type);

		if (bdaddr_type == ADDR_LE_DEV_PUBLIC)
			chan->src_type = BDADDR_LE_PUBLIC;
		else
			chan->src_type = BDADDR_LE_RANDOM;
	} else {
		bacpy(&chan->src, &hdev->bdaddr);
		chan->src_type = BDADDR_BREDR;
	}

	chan->state = BT_LISTEN;
	chan->mode = L2CAP_MODE_BASIC;
	chan->imtu = L2CAP_DEFAULT_MTU;
	chan->ops = &smp_root_chan_ops;

	/* Set correct nesting level for a parent/listening channel */
	atomic_set(&chan->nesting, L2CAP_NESTING_PARENT);

	return chan;
}

static void smp_del_chan(struct l2cap_chan *chan)
{
	struct smp_dev *smp;

	BT_DBG("chan %p", chan);

	smp = chan->data;
	if (smp) {
		chan->data = NULL;
		crypto_free_cipher(smp->tfm_aes);
		crypto_free_shash(smp->tfm_cmac);
		crypto_free_kpp(smp->tfm_ecdh);
		kzfree(smp);
	}

	l2cap_chan_put(chan);
}

static ssize_t force_bredr_smp_read(struct file *file,
				    char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[3];

	buf[0] = hci_dev_test_flag(hdev, HCI_FORCE_BREDR_SMP) ? 'Y': 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t force_bredr_smp_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	bool enable;
	int err;

	err = kstrtobool_from_user(user_buf, count, &enable);
	if (err)
		return err;

	if (enable == hci_dev_test_flag(hdev, HCI_FORCE_BREDR_SMP))
		return -EALREADY;

	if (enable) {
		struct l2cap_chan *chan;

		chan = smp_add_cid(hdev, L2CAP_CID_SMP_BREDR);
		if (IS_ERR(chan))
			return PTR_ERR(chan);

		hdev->smp_bredr_data = chan;
	} else {
		struct l2cap_chan *chan;

		chan = hdev->smp_bredr_data;
		hdev->smp_bredr_data = NULL;
		smp_del_chan(chan);
	}

	hci_dev_change_flag(hdev, HCI_FORCE_BREDR_SMP);

	return count;
}

static const struct file_operations force_bredr_smp_fops = {
	.open		= simple_open,
	.read		= force_bredr_smp_read,
	.write		= force_bredr_smp_write,
	.llseek		= default_llseek,
};

static ssize_t le_min_key_size_read(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[4];

	snprintf(buf, sizeof(buf), "%2u\n", hdev->le_min_key_size);

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t le_min_key_size_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[32];
	size_t buf_size = min(count, (sizeof(buf) - 1));
	u8 key_size;

	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	sscanf(buf, "%hhu", &key_size);

	if (key_size > hdev->le_max_key_size ||
	    key_size < SMP_MIN_ENC_KEY_SIZE)
		return -EINVAL;

	hdev->le_min_key_size = key_size;

	return count;
}

static const struct file_operations le_min_key_size_fops = {
	.open		= simple_open,
	.read		= le_min_key_size_read,
	.write		= le_min_key_size_write,
	.llseek		= default_llseek,
};

static ssize_t le_max_key_size_read(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[4];

	snprintf(buf, sizeof(buf), "%2u\n", hdev->le_max_key_size);

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t le_max_key_size_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[32];
	size_t buf_size = min(count, (sizeof(buf) - 1));
	u8 key_size;

	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	sscanf(buf, "%hhu", &key_size);

	if (key_size > SMP_MAX_ENC_KEY_SIZE ||
	    key_size < hdev->le_min_key_size)
		return -EINVAL;

	hdev->le_max_key_size = key_size;

	return count;
}

static const struct file_operations le_max_key_size_fops = {
	.open		= simple_open,
	.read		= le_max_key_size_read,
	.write		= le_max_key_size_write,
	.llseek		= default_llseek,
};

int smp_register(struct hci_dev *hdev)
{
	struct l2cap_chan *chan;

	BT_DBG("%s", hdev->name);

	/* If the controller does not support Low Energy operation, then
	 * there is also no need to register any SMP channel.
	 */
	if (!lmp_le_capable(hdev))
		return 0;

	if (WARN_ON(hdev->smp_data)) {
		chan = hdev->smp_data;
		hdev->smp_data = NULL;
		smp_del_chan(chan);
	}

	chan = smp_add_cid(hdev, L2CAP_CID_SMP);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	hdev->smp_data = chan;

	debugfs_create_file("le_min_key_size", 0644, hdev->debugfs, hdev,
			    &le_min_key_size_fops);
	debugfs_create_file("le_max_key_size", 0644, hdev->debugfs, hdev,
			    &le_max_key_size_fops);

	/* If the controller does not support BR/EDR Secure Connections
	 * feature, then the BR/EDR SMP channel shall not be present.
	 *
	 * To test this with Bluetooth 4.0 controllers, create a debugfs
	 * switch that allows forcing BR/EDR SMP support and accepting
	 * cross-transport pairing on non-AES encrypted connections.
	 */
	if (!lmp_sc_capable(hdev)) {
		debugfs_create_file("force_bredr_smp", 0644, hdev->debugfs,
				    hdev, &force_bredr_smp_fops);

		/* Flag can be already set here (due to power toggle) */
		if (!hci_dev_test_flag(hdev, HCI_FORCE_BREDR_SMP))
			return 0;
	}

	if (WARN_ON(hdev->smp_bredr_data)) {
		chan = hdev->smp_bredr_data;
		hdev->smp_bredr_data = NULL;
		smp_del_chan(chan);
	}

	chan = smp_add_cid(hdev, L2CAP_CID_SMP_BREDR);
	if (IS_ERR(chan)) {
		int err = PTR_ERR(chan);
		chan = hdev->smp_data;
		hdev->smp_data = NULL;
		smp_del_chan(chan);
		return err;
	}

	hdev->smp_bredr_data = chan;

	return 0;
}

void smp_unregister(struct hci_dev *hdev)
{
	struct l2cap_chan *chan;

	if (hdev->smp_bredr_data) {
		chan = hdev->smp_bredr_data;
		hdev->smp_bredr_data = NULL;
		smp_del_chan(chan);
	}

	if (hdev->smp_data) {
		chan = hdev->smp_data;
		hdev->smp_data = NULL;
		smp_del_chan(chan);
	}
}

#if IS_ENABLED(CONFIG_BT_SELFTEST_SMP)

static int __init test_debug_key(struct crypto_kpp *tfm_ecdh)
{
	u8 pk[64];
	int err;

	err = set_ecdh_privkey(tfm_ecdh, debug_sk);
	if (err)
		return err;

	err = generate_ecdh_public_key(tfm_ecdh, pk);
	if (err)
		return err;

	if (crypto_memneq(pk, debug_pk, 64))
		return -EINVAL;

	return 0;
}

static int __init test_ah(struct crypto_cipher *tfm_aes)
{
	const u8 irk[16] = {
			0x9b, 0x7d, 0x39, 0x0a, 0xa6, 0x10, 0x10, 0x34,
			0x05, 0xad, 0xc8, 0x57, 0xa3, 0x34, 0x02, 0xec };
	const u8 r[3] = { 0x94, 0x81, 0x70 };
	const u8 exp[3] = { 0xaa, 0xfb, 0x0d };
	u8 res[3];
	int err;

	err = smp_ah(tfm_aes, irk, r, res);
	if (err)
		return err;

	if (crypto_memneq(res, exp, 3))
		return -EINVAL;

	return 0;
}

static int __init test_c1(struct crypto_cipher *tfm_aes)
{
	const u8 k[16] = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const u8 r[16] = {
			0xe0, 0x2e, 0x70, 0xc6, 0x4e, 0x27, 0x88, 0x63,
			0x0e, 0x6f, 0xad, 0x56, 0x21, 0xd5, 0x83, 0x57 };
	const u8 preq[7] = { 0x01, 0x01, 0x00, 0x00, 0x10, 0x07, 0x07 };
	const u8 pres[7] = { 0x02, 0x03, 0x00, 0x00, 0x08, 0x00, 0x05 };
	const u8 _iat = 0x01;
	const u8 _rat = 0x00;
	const bdaddr_t ra = { { 0xb6, 0xb5, 0xb4, 0xb3, 0xb2, 0xb1 } };
	const bdaddr_t ia = { { 0xa6, 0xa5, 0xa4, 0xa3, 0xa2, 0xa1 } };
	const u8 exp[16] = {
			0x86, 0x3b, 0xf1, 0xbe, 0xc5, 0x4d, 0xa7, 0xd2,
			0xea, 0x88, 0x89, 0x87, 0xef, 0x3f, 0x1e, 0x1e };
	u8 res[16];
	int err;

	err = smp_c1(tfm_aes, k, r, preq, pres, _iat, &ia, _rat, &ra, res);
	if (err)
		return err;

	if (crypto_memneq(res, exp, 16))
		return -EINVAL;

	return 0;
}

static int __init test_s1(struct crypto_cipher *tfm_aes)
{
	const u8 k[16] = {
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const u8 r1[16] = {
			0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
	const u8 r2[16] = {
			0x00, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99 };
	const u8 exp[16] = {
			0x62, 0xa0, 0x6d, 0x79, 0xae, 0x16, 0x42, 0x5b,
			0x9b, 0xf4, 0xb0, 0xe8, 0xf0, 0xe1, 0x1f, 0x9a };
	u8 res[16];
	int err;

	err = smp_s1(tfm_aes, k, r1, r2, res);
	if (err)
		return err;

	if (crypto_memneq(res, exp, 16))
		return -EINVAL;

	return 0;
}

static int __init test_f4(struct crypto_shash *tfm_cmac)
{
	const u8 u[32] = {
			0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
			0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
			0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
			0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20 };
	const u8 v[32] = {
			0xfd, 0xc5, 0x7f, 0xf4, 0x49, 0xdd, 0x4f, 0x6b,
			0xfb, 0x7c, 0x9d, 0xf1, 0xc2, 0x9a, 0xcb, 0x59,
			0x2a, 0xe7, 0xd4, 0xee, 0xfb, 0xfc, 0x0a, 0x90,
			0x9a, 0xbb, 0xf6, 0x32, 0x3d, 0x8b, 0x18, 0x55 };
	const u8 x[16] = {
			0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	const u8 z = 0x00;
	const u8 exp[16] = {
			0x2d, 0x87, 0x74, 0xa9, 0xbe, 0xa1, 0xed, 0xf1,
			0x1c, 0xbd, 0xa9, 0x07, 0xf1, 0x16, 0xc9, 0xf2 };
	u8 res[16];
	int err;

	err = smp_f4(tfm_cmac, u, v, x, z, res);
	if (err)
		return err;

	if (crypto_memneq(res, exp, 16))
		return -EINVAL;

	return 0;
}

static int __init test_f5(struct crypto_shash *tfm_cmac)
{
	const u8 w[32] = {
			0x98, 0xa6, 0xbf, 0x73, 0xf3, 0x34, 0x8d, 0x86,
			0xf1, 0x66, 0xf8, 0xb4, 0x13, 0x6b, 0x79, 0x99,
			0x9b, 0x7d, 0x39, 0x0a, 0xa6, 0x10, 0x10, 0x34,
			0x05, 0xad, 0xc8, 0x57, 0xa3, 0x34, 0x02, 0xec };
	const u8 n1[16] = {
			0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	const u8 n2[16] = {
			0xcf, 0xc4, 0x3d, 0xff, 0xf7, 0x83, 0x65, 0x21,
			0x6e, 0x5f, 0xa7, 0x25, 0xcc, 0xe7, 0xe8, 0xa6 };
	const u8 a1[7] = { 0xce, 0xbf, 0x37, 0x37, 0x12, 0x56, 0x00 };
	const u8 a2[7] = { 0xc1, 0xcf, 0x2d, 0x70, 0x13, 0xa7, 0x00 };
	const u8 exp_ltk[16] = {
			0x38, 0x0a, 0x75, 0x94, 0xb5, 0x22, 0x05, 0x98,
			0x23, 0xcd, 0xd7, 0x69, 0x11, 0x79, 0x86, 0x69 };
	const u8 exp_mackey[16] = {
			0x20, 0x6e, 0x63, 0xce, 0x20, 0x6a, 0x3f, 0xfd,
			0x02, 0x4a, 0x08, 0xa1, 0x76, 0xf1, 0x65, 0x29 };
	u8 mackey[16], ltk[16];
	int err;

	err = smp_f5(tfm_cmac, w, n1, n2, a1, a2, mackey, ltk);
	if (err)
		return err;

	if (crypto_memneq(mackey, exp_mackey, 16))
		return -EINVAL;

	if (crypto_memneq(ltk, exp_ltk, 16))
		return -EINVAL;

	return 0;
}

static int __init test_f6(struct crypto_shash *tfm_cmac)
{
	const u8 w[16] = {
			0x20, 0x6e, 0x63, 0xce, 0x20, 0x6a, 0x3f, 0xfd,
			0x02, 0x4a, 0x08, 0xa1, 0x76, 0xf1, 0x65, 0x29 };
	const u8 n1[16] = {
			0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	const u8 n2[16] = {
			0xcf, 0xc4, 0x3d, 0xff, 0xf7, 0x83, 0x65, 0x21,
			0x6e, 0x5f, 0xa7, 0x25, 0xcc, 0xe7, 0xe8, 0xa6 };
	const u8 r[16] = {
			0xc8, 0x0f, 0x2d, 0x0c, 0xd2, 0x42, 0xda, 0x08,
			0x54, 0xbb, 0x53, 0xb4, 0x3b, 0x34, 0xa3, 0x12 };
	const u8 io_cap[3] = { 0x02, 0x01, 0x01 };
	const u8 a1[7] = { 0xce, 0xbf, 0x37, 0x37, 0x12, 0x56, 0x00 };
	const u8 a2[7] = { 0xc1, 0xcf, 0x2d, 0x70, 0x13, 0xa7, 0x00 };
	const u8 exp[16] = {
			0x61, 0x8f, 0x95, 0xda, 0x09, 0x0b, 0x6c, 0xd2,
			0xc5, 0xe8, 0xd0, 0x9c, 0x98, 0x73, 0xc4, 0xe3 };
	u8 res[16];
	int err;

	err = smp_f6(tfm_cmac, w, n1, n2, r, io_cap, a1, a2, res);
	if (err)
		return err;

	if (crypto_memneq(res, exp, 16))
		return -EINVAL;

	return 0;
}

static int __init test_g2(struct crypto_shash *tfm_cmac)
{
	const u8 u[32] = {
			0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
			0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
			0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
			0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20 };
	const u8 v[32] = {
			0xfd, 0xc5, 0x7f, 0xf4, 0x49, 0xdd, 0x4f, 0x6b,
			0xfb, 0x7c, 0x9d, 0xf1, 0xc2, 0x9a, 0xcb, 0x59,
			0x2a, 0xe7, 0xd4, 0xee, 0xfb, 0xfc, 0x0a, 0x90,
			0x9a, 0xbb, 0xf6, 0x32, 0x3d, 0x8b, 0x18, 0x55 };
	const u8 x[16] = {
			0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	const u8 y[16] = {
			0xcf, 0xc4, 0x3d, 0xff, 0xf7, 0x83, 0x65, 0x21,
			0x6e, 0x5f, 0xa7, 0x25, 0xcc, 0xe7, 0xe8, 0xa6 };
	const u32 exp_val = 0x2f9ed5ba % 1000000;
	u32 val;
	int err;

	err = smp_g2(tfm_cmac, u, v, x, y, &val);
	if (err)
		return err;

	if (val != exp_val)
		return -EINVAL;

	return 0;
}

static int __init test_h6(struct crypto_shash *tfm_cmac)
{
	const u8 w[16] = {
			0x9b, 0x7d, 0x39, 0x0a, 0xa6, 0x10, 0x10, 0x34,
			0x05, 0xad, 0xc8, 0x57, 0xa3, 0x34, 0x02, 0xec };
	const u8 key_id[4] = { 0x72, 0x62, 0x65, 0x6c };
	const u8 exp[16] = {
			0x99, 0x63, 0xb1, 0x80, 0xe2, 0xa9, 0xd3, 0xe8,
			0x1c, 0xc9, 0x6d, 0xe7, 0x02, 0xe1, 0x9a, 0x2d };
	u8 res[16];
	int err;

	err = smp_h6(tfm_cmac, w, key_id, res);
	if (err)
		return err;

	if (crypto_memneq(res, exp, 16))
		return -EINVAL;

	return 0;
}

static char test_smp_buffer[32];

static ssize_t test_smp_read(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(user_buf, count, ppos, test_smp_buffer,
				       strlen(test_smp_buffer));
}

static const struct file_operations test_smp_fops = {
	.open		= simple_open,
	.read		= test_smp_read,
	.llseek		= default_llseek,
};

static int __init run_selftests(struct crypto_cipher *tfm_aes,
				struct crypto_shash *tfm_cmac,
				struct crypto_kpp *tfm_ecdh)
{
	ktime_t calltime, delta, rettime;
	unsigned long long duration;
	int err;

	calltime = ktime_get();

	err = test_debug_key(tfm_ecdh);
	if (err) {
		BT_ERR("debug_key test failed");
		goto done;
	}

	err = test_ah(tfm_aes);
	if (err) {
		BT_ERR("smp_ah test failed");
		goto done;
	}

	err = test_c1(tfm_aes);
	if (err) {
		BT_ERR("smp_c1 test failed");
		goto done;
	}

	err = test_s1(tfm_aes);
	if (err) {
		BT_ERR("smp_s1 test failed");
		goto done;
	}

	err = test_f4(tfm_cmac);
	if (err) {
		BT_ERR("smp_f4 test failed");
		goto done;
	}

	err = test_f5(tfm_cmac);
	if (err) {
		BT_ERR("smp_f5 test failed");
		goto done;
	}

	err = test_f6(tfm_cmac);
	if (err) {
		BT_ERR("smp_f6 test failed");
		goto done;
	}

	err = test_g2(tfm_cmac);
	if (err) {
		BT_ERR("smp_g2 test failed");
		goto done;
	}

	err = test_h6(tfm_cmac);
	if (err) {
		BT_ERR("smp_h6 test failed");
		goto done;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	BT_INFO("SMP test passed in %llu usecs", duration);

done:
	if (!err)
		snprintf(test_smp_buffer, sizeof(test_smp_buffer),
			 "PASS (%llu usecs)\n", duration);
	else
		snprintf(test_smp_buffer, sizeof(test_smp_buffer), "FAIL\n");

	debugfs_create_file("selftest_smp", 0444, bt_debugfs, NULL,
			    &test_smp_fops);

	return err;
}

int __init bt_selftest_smp(void)
{
	struct crypto_cipher *tfm_aes;
	struct crypto_shash *tfm_cmac;
	struct crypto_kpp *tfm_ecdh;
	int err;

	tfm_aes = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(tfm_aes)) {
		BT_ERR("Unable to create AES crypto context");
		return PTR_ERR(tfm_aes);
	}

	tfm_cmac = crypto_alloc_shash("cmac(aes)", 0, 0);
	if (IS_ERR(tfm_cmac)) {
		BT_ERR("Unable to create CMAC crypto context");
		crypto_free_cipher(tfm_aes);
		return PTR_ERR(tfm_cmac);
	}

	tfm_ecdh = crypto_alloc_kpp("ecdh", CRYPTO_ALG_INTERNAL, 0);
	if (IS_ERR(tfm_ecdh)) {
		BT_ERR("Unable to create ECDH crypto context");
		crypto_free_shash(tfm_cmac);
		crypto_free_cipher(tfm_aes);
		return PTR_ERR(tfm_ecdh);
	}

	err = run_selftests(tfm_aes, tfm_cmac, tfm_ecdh);

	crypto_free_shash(tfm_cmac);
	crypto_free_cipher(tfm_aes);
	crypto_free_kpp(tfm_ecdh);

	return err;
}

#endif
