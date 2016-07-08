/*
 * NFC Digital Protocol stack
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#define pr_fmt(fmt) "digital: %s: " fmt, __func__

#include "digital.h"

#define DIGITAL_NFC_DEP_N_RETRY_NACK	2
#define DIGITAL_NFC_DEP_N_RETRY_ATN	2

#define DIGITAL_NFC_DEP_FRAME_DIR_OUT 0xD4
#define DIGITAL_NFC_DEP_FRAME_DIR_IN  0xD5

#define DIGITAL_NFC_DEP_NFCA_SOD_SB   0xF0

#define DIGITAL_CMD_ATR_REQ 0x00
#define DIGITAL_CMD_ATR_RES 0x01
#define DIGITAL_CMD_PSL_REQ 0x04
#define DIGITAL_CMD_PSL_RES 0x05
#define DIGITAL_CMD_DEP_REQ 0x06
#define DIGITAL_CMD_DEP_RES 0x07

#define DIGITAL_ATR_REQ_MIN_SIZE 16
#define DIGITAL_ATR_REQ_MAX_SIZE 64

#define DIGITAL_DID_MAX	14

#define DIGITAL_PAYLOAD_SIZE_MAX	254
#define DIGITAL_PAYLOAD_BITS_TO_PP(s)	(((s) & 0x3) << 4)
#define DIGITAL_PAYLOAD_PP_TO_BITS(s)	(((s) >> 4) & 0x3)
#define DIGITAL_PAYLOAD_BITS_TO_FSL(s)	((s) & 0x3)
#define DIGITAL_PAYLOAD_FSL_TO_BITS(s)	((s) & 0x3)

#define DIGITAL_GB_BIT	0x02

#define DIGITAL_NFC_DEP_REQ_RES_HEADROOM	2 /* SoD: [SB (NFC-A)] + LEN */
#define DIGITAL_NFC_DEP_REQ_RES_TAILROOM	2 /* EoD: 2-byte CRC */

#define DIGITAL_NFC_DEP_PFB_TYPE(pfb) ((pfb) & 0xE0)

#define DIGITAL_NFC_DEP_PFB_TIMEOUT_BIT 0x10
#define DIGITAL_NFC_DEP_PFB_MI_BIT	0x10
#define DIGITAL_NFC_DEP_PFB_NACK_BIT	0x10
#define DIGITAL_NFC_DEP_PFB_DID_BIT	0x04

#define DIGITAL_NFC_DEP_PFB_IS_TIMEOUT(pfb) \
				((pfb) & DIGITAL_NFC_DEP_PFB_TIMEOUT_BIT)
#define DIGITAL_NFC_DEP_MI_BIT_SET(pfb)  ((pfb) & DIGITAL_NFC_DEP_PFB_MI_BIT)
#define DIGITAL_NFC_DEP_NACK_BIT_SET(pfb) ((pfb) & DIGITAL_NFC_DEP_PFB_NACK_BIT)
#define DIGITAL_NFC_DEP_NAD_BIT_SET(pfb) ((pfb) & 0x08)
#define DIGITAL_NFC_DEP_DID_BIT_SET(pfb) ((pfb) & DIGITAL_NFC_DEP_PFB_DID_BIT)
#define DIGITAL_NFC_DEP_PFB_PNI(pfb)     ((pfb) & 0x03)

#define DIGITAL_NFC_DEP_PFB_I_PDU          0x00
#define DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU   0x40
#define DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU 0x80

struct digital_atr_req {
	u8 dir;
	u8 cmd;
	u8 nfcid3[10];
	u8 did;
	u8 bs;
	u8 br;
	u8 pp;
	u8 gb[0];
} __packed;

struct digital_atr_res {
	u8 dir;
	u8 cmd;
	u8 nfcid3[10];
	u8 did;
	u8 bs;
	u8 br;
	u8 to;
	u8 pp;
	u8 gb[0];
} __packed;

struct digital_psl_req {
	u8 dir;
	u8 cmd;
	u8 did;
	u8 brs;
	u8 fsl;
} __packed;

struct digital_psl_res {
	u8 dir;
	u8 cmd;
	u8 did;
} __packed;

struct digital_dep_req_res {
	u8 dir;
	u8 cmd;
	u8 pfb;
} __packed;

static void digital_in_recv_dep_res(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp);
static void digital_tg_recv_dep_req(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp);

static const u8 digital_payload_bits_map[4] = {
	[0] = 64,
	[1] = 128,
	[2] = 192,
	[3] = 254
};

static u8 digital_payload_bits_to_size(u8 payload_bits)
{
	if (payload_bits >= ARRAY_SIZE(digital_payload_bits_map))
		return 0;

	return digital_payload_bits_map[payload_bits];
}

static u8 digital_payload_size_to_bits(u8 payload_size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(digital_payload_bits_map); i++)
		if (digital_payload_bits_map[i] == payload_size)
			return i;

	return 0xff;
}

static void digital_skb_push_dep_sod(struct nfc_digital_dev *ddev,
				     struct sk_buff *skb)
{
	skb_push(skb, sizeof(u8));

	skb->data[0] = skb->len;

	if (ddev->curr_rf_tech == NFC_DIGITAL_RF_TECH_106A)
		*skb_push(skb, sizeof(u8)) = DIGITAL_NFC_DEP_NFCA_SOD_SB;
}

static int digital_skb_pull_dep_sod(struct nfc_digital_dev *ddev,
				    struct sk_buff *skb)
{
	u8 size;

	if (skb->len < 2)
		return -EIO;

	if (ddev->curr_rf_tech == NFC_DIGITAL_RF_TECH_106A)
		skb_pull(skb, sizeof(u8));

	size = skb->data[0];
	if (size != skb->len)
		return -EIO;

	skb_pull(skb, sizeof(u8));

	return 0;
}

static struct sk_buff *
digital_send_dep_data_prep(struct nfc_digital_dev *ddev, struct sk_buff *skb,
			   struct digital_dep_req_res *dep_req_res,
			   struct digital_data_exch *data_exch)
{
	struct sk_buff *new_skb;

	if (skb->len > ddev->remote_payload_max) {
		dep_req_res->pfb |= DIGITAL_NFC_DEP_PFB_MI_BIT;

		new_skb = digital_skb_alloc(ddev, ddev->remote_payload_max);
		if (!new_skb) {
			kfree_skb(ddev->chaining_skb);
			ddev->chaining_skb = NULL;

			return ERR_PTR(-ENOMEM);
		}

		memcpy(skb_put(new_skb, ddev->remote_payload_max), skb->data,
		       ddev->remote_payload_max);
		skb_pull(skb, ddev->remote_payload_max);

		ddev->chaining_skb = skb;
		ddev->data_exch = data_exch;
	} else {
		ddev->chaining_skb = NULL;
		new_skb = skb;
	}

	return new_skb;
}

static struct sk_buff *
digital_recv_dep_data_gather(struct nfc_digital_dev *ddev, u8 pfb,
			     struct sk_buff *resp,
			     int (*send_ack)(struct nfc_digital_dev *ddev,
					     struct digital_data_exch
							     *data_exch),
			     struct digital_data_exch *data_exch)
{
	struct sk_buff *new_skb;
	int rc;

	if (DIGITAL_NFC_DEP_MI_BIT_SET(pfb) && (!ddev->chaining_skb)) {
		ddev->chaining_skb =
			nfc_alloc_recv_skb(8 * ddev->local_payload_max,
					   GFP_KERNEL);
		if (!ddev->chaining_skb) {
			rc = -ENOMEM;
			goto error;
		}
	}

	if (ddev->chaining_skb) {
		if (resp->len > skb_tailroom(ddev->chaining_skb)) {
			new_skb = skb_copy_expand(ddev->chaining_skb,
						  skb_headroom(
							  ddev->chaining_skb),
						  8 * ddev->local_payload_max,
						  GFP_KERNEL);
			if (!new_skb) {
				rc = -ENOMEM;
				goto error;
			}

			kfree_skb(ddev->chaining_skb);
			ddev->chaining_skb = new_skb;
		}

		memcpy(skb_put(ddev->chaining_skb, resp->len), resp->data,
		       resp->len);

		kfree_skb(resp);
		resp = NULL;

		if (DIGITAL_NFC_DEP_MI_BIT_SET(pfb)) {
			rc = send_ack(ddev, data_exch);
			if (rc)
				goto error;

			return NULL;
		}

		resp = ddev->chaining_skb;
		ddev->chaining_skb = NULL;
	}

	return resp;

error:
	kfree_skb(resp);

	kfree_skb(ddev->chaining_skb);
	ddev->chaining_skb = NULL;

	return ERR_PTR(rc);
}

static void digital_in_recv_psl_res(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	struct nfc_target *target = arg;
	struct digital_psl_res *psl_res;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	rc = ddev->skb_check_crc(resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.6");
		goto exit;
	}

	rc = digital_skb_pull_dep_sod(ddev, resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.2");
		goto exit;
	}

	psl_res = (struct digital_psl_res *)resp->data;

	if ((resp->len != sizeof(*psl_res)) ||
	    (psl_res->dir != DIGITAL_NFC_DEP_FRAME_DIR_IN) ||
	    (psl_res->cmd != DIGITAL_CMD_PSL_RES)) {
		rc = -EIO;
		goto exit;
	}

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH,
				     NFC_DIGITAL_RF_TECH_424F);
	if (rc)
		goto exit;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCF_NFC_DEP);
	if (rc)
		goto exit;

	if (!DIGITAL_DRV_CAPS_IN_CRC(ddev) &&
	    (ddev->curr_rf_tech == NFC_DIGITAL_RF_TECH_106A)) {
		ddev->skb_add_crc = digital_skb_add_crc_f;
		ddev->skb_check_crc = digital_skb_check_crc_f;
	}

	ddev->curr_rf_tech = NFC_DIGITAL_RF_TECH_424F;

	nfc_dep_link_is_up(ddev->nfc_dev, target->idx, NFC_COMM_ACTIVE,
			   NFC_RF_INITIATOR);

	ddev->curr_nfc_dep_pni = 0;

exit:
	dev_kfree_skb(resp);

	if (rc)
		ddev->curr_protocol = 0;
}

static int digital_in_send_psl_req(struct nfc_digital_dev *ddev,
				   struct nfc_target *target)
{
	struct sk_buff *skb;
	struct digital_psl_req *psl_req;
	int rc;
	u8 payload_size, payload_bits;

	skb = digital_skb_alloc(ddev, sizeof(*psl_req));
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(*psl_req));

	psl_req = (struct digital_psl_req *)skb->data;

	psl_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	psl_req->cmd = DIGITAL_CMD_PSL_REQ;
	psl_req->did = 0;
	psl_req->brs = (0x2 << 3) | 0x2; /* 424F both directions */

	payload_size = min(ddev->local_payload_max, ddev->remote_payload_max);
	payload_bits = digital_payload_size_to_bits(payload_size);
	psl_req->fsl = DIGITAL_PAYLOAD_BITS_TO_FSL(payload_bits);

	ddev->local_payload_max = payload_size;
	ddev->remote_payload_max = payload_size;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	rc = digital_in_send_cmd(ddev, skb, 500, digital_in_recv_psl_res,
				 target);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_in_recv_atr_res(struct nfc_digital_dev *ddev, void *arg,
				 struct sk_buff *resp)
{
	struct nfc_target *target = arg;
	struct digital_atr_res *atr_res;
	u8 gb_len, payload_bits;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	rc = ddev->skb_check_crc(resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.6");
		goto exit;
	}

	rc = digital_skb_pull_dep_sod(ddev, resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.2");
		goto exit;
	}

	if (resp->len < sizeof(struct digital_atr_res)) {
		rc = -EIO;
		goto exit;
	}

	gb_len = resp->len - sizeof(struct digital_atr_res);

	atr_res = (struct digital_atr_res *)resp->data;

	payload_bits = DIGITAL_PAYLOAD_PP_TO_BITS(atr_res->pp);
	ddev->remote_payload_max = digital_payload_bits_to_size(payload_bits);

	if (!ddev->remote_payload_max) {
		rc = -EINVAL;
		goto exit;
	}

	rc = nfc_set_remote_general_bytes(ddev->nfc_dev, atr_res->gb, gb_len);
	if (rc)
		goto exit;

	if ((ddev->protocols & NFC_PROTO_FELICA_MASK) &&
	    (ddev->curr_rf_tech != NFC_DIGITAL_RF_TECH_424F)) {
		rc = digital_in_send_psl_req(ddev, target);
		if (!rc)
			goto exit;
	}

	rc = nfc_dep_link_is_up(ddev->nfc_dev, target->idx, NFC_COMM_ACTIVE,
				NFC_RF_INITIATOR);

	ddev->curr_nfc_dep_pni = 0;

exit:
	dev_kfree_skb(resp);

	if (rc)
		ddev->curr_protocol = 0;
}

int digital_in_send_atr_req(struct nfc_digital_dev *ddev,
			    struct nfc_target *target, __u8 comm_mode, __u8 *gb,
			    size_t gb_len)
{
	struct sk_buff *skb;
	struct digital_atr_req *atr_req;
	uint size;
	int rc;
	u8 payload_bits;

	size = DIGITAL_ATR_REQ_MIN_SIZE + gb_len;

	if (size > DIGITAL_ATR_REQ_MAX_SIZE) {
		PROTOCOL_ERR("14.6.1.1");
		return -EINVAL;
	}

	skb = digital_skb_alloc(ddev, size);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(struct digital_atr_req));

	atr_req = (struct digital_atr_req *)skb->data;
	memset(atr_req, 0, sizeof(struct digital_atr_req));

	atr_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	atr_req->cmd = DIGITAL_CMD_ATR_REQ;
	if (target->nfcid2_len)
		memcpy(atr_req->nfcid3, target->nfcid2, NFC_NFCID2_MAXSIZE);
	else
		get_random_bytes(atr_req->nfcid3, NFC_NFCID3_MAXSIZE);

	atr_req->did = 0;
	atr_req->bs = 0;
	atr_req->br = 0;

	ddev->local_payload_max = DIGITAL_PAYLOAD_SIZE_MAX;
	payload_bits = digital_payload_size_to_bits(ddev->local_payload_max);
	atr_req->pp = DIGITAL_PAYLOAD_BITS_TO_PP(payload_bits);

	if (gb_len) {
		atr_req->pp |= DIGITAL_GB_BIT;
		memcpy(skb_put(skb, gb_len), gb, gb_len);
	}

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	rc = digital_in_send_cmd(ddev, skb, 500, digital_in_recv_atr_res,
				 target);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static int digital_in_send_ack(struct nfc_digital_dev *ddev,
			       struct digital_data_exch *data_exch)
{
	struct digital_dep_req_res *dep_req;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_req = (struct digital_dep_req_res *)skb->data;

	dep_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	dep_req->cmd = DIGITAL_CMD_DEP_REQ;
	dep_req->pfb = DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU |
		       ddev->curr_nfc_dep_pni;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	ddev->saved_skb = pskb_copy(skb, GFP_KERNEL);

	rc = digital_in_send_cmd(ddev, skb, 1500, digital_in_recv_dep_res,
				 data_exch);
	if (rc) {
		kfree_skb(skb);
		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;
	}

	return rc;
}

static int digital_in_send_nack(struct nfc_digital_dev *ddev,
				struct digital_data_exch *data_exch)
{
	struct digital_dep_req_res *dep_req;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_req = (struct digital_dep_req_res *)skb->data;

	dep_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	dep_req->cmd = DIGITAL_CMD_DEP_REQ;
	dep_req->pfb = DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU |
		       DIGITAL_NFC_DEP_PFB_NACK_BIT | ddev->curr_nfc_dep_pni;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	rc = digital_in_send_cmd(ddev, skb, 1500, digital_in_recv_dep_res,
				 data_exch);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static int digital_in_send_atn(struct nfc_digital_dev *ddev,
			       struct digital_data_exch *data_exch)
{
	struct digital_dep_req_res *dep_req;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_req = (struct digital_dep_req_res *)skb->data;

	dep_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	dep_req->cmd = DIGITAL_CMD_DEP_REQ;
	dep_req->pfb = DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	rc = digital_in_send_cmd(ddev, skb, 1500, digital_in_recv_dep_res,
				 data_exch);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static int digital_in_send_rtox(struct nfc_digital_dev *ddev,
				struct digital_data_exch *data_exch, u8 rtox)
{
	struct digital_dep_req_res *dep_req;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, 1) = rtox;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_req = (struct digital_dep_req_res *)skb->data;

	dep_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	dep_req->cmd = DIGITAL_CMD_DEP_REQ;
	dep_req->pfb = DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU |
		       DIGITAL_NFC_DEP_PFB_TIMEOUT_BIT;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	rc = digital_in_send_cmd(ddev, skb, 1500, digital_in_recv_dep_res,
				 data_exch);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static int digital_in_send_saved_skb(struct nfc_digital_dev *ddev,
				     struct digital_data_exch *data_exch)
{
	int rc;

	if (!ddev->saved_skb)
		return -EINVAL;

	skb_get(ddev->saved_skb);

	rc = digital_in_send_cmd(ddev, ddev->saved_skb, 1500,
				 digital_in_recv_dep_res, data_exch);
	if (rc)
		kfree_skb(ddev->saved_skb);

	return rc;
}

static void digital_in_recv_dep_res(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	struct digital_data_exch *data_exch = arg;
	struct digital_dep_req_res *dep_res;
	u8 pfb;
	uint size;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;

		if ((rc == -EIO || (rc == -ETIMEDOUT && ddev->nack_count)) &&
		    (ddev->nack_count++ < DIGITAL_NFC_DEP_N_RETRY_NACK)) {
			ddev->atn_count = 0;

			rc = digital_in_send_nack(ddev, data_exch);
			if (rc)
				goto error;

			return;
		} else if ((rc == -ETIMEDOUT) &&
			   (ddev->atn_count++ < DIGITAL_NFC_DEP_N_RETRY_ATN)) {
			ddev->nack_count = 0;

			rc = digital_in_send_atn(ddev, data_exch);
			if (rc)
				goto error;

			return;
		}

		goto exit;
	}

	rc = digital_skb_pull_dep_sod(ddev, resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.2");
		goto exit;
	}

	rc = ddev->skb_check_crc(resp);
	if (rc) {
		if ((resp->len >= 4) &&
		    (ddev->nack_count++ < DIGITAL_NFC_DEP_N_RETRY_NACK)) {
			ddev->atn_count = 0;

			rc = digital_in_send_nack(ddev, data_exch);
			if (rc)
				goto error;

			kfree_skb(resp);

			return;
		}

		PROTOCOL_ERR("14.4.1.6");
		goto error;
	}

	ddev->atn_count = 0;
	ddev->nack_count = 0;

	if (resp->len > ddev->local_payload_max) {
		rc = -EMSGSIZE;
		goto exit;
	}

	size = sizeof(struct digital_dep_req_res);
	dep_res = (struct digital_dep_req_res *)resp->data;

	if (resp->len < size || dep_res->dir != DIGITAL_NFC_DEP_FRAME_DIR_IN ||
	    dep_res->cmd != DIGITAL_CMD_DEP_RES) {
		rc = -EIO;
		goto error;
	}

	pfb = dep_res->pfb;

	if (DIGITAL_NFC_DEP_DID_BIT_SET(pfb)) {
		PROTOCOL_ERR("14.8.2.1");
		rc = -EIO;
		goto error;
	}

	if (DIGITAL_NFC_DEP_NAD_BIT_SET(pfb)) {
		rc = -EIO;
		goto exit;
	}

	if (size > resp->len) {
		rc = -EIO;
		goto error;
	}

	skb_pull(resp, size);

	switch (DIGITAL_NFC_DEP_PFB_TYPE(pfb)) {
	case DIGITAL_NFC_DEP_PFB_I_PDU:
		if (DIGITAL_NFC_DEP_PFB_PNI(pfb) != ddev->curr_nfc_dep_pni) {
			PROTOCOL_ERR("14.12.3.3");
			rc = -EIO;
			goto error;
		}

		ddev->curr_nfc_dep_pni =
			DIGITAL_NFC_DEP_PFB_PNI(ddev->curr_nfc_dep_pni + 1);

		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;

		resp = digital_recv_dep_data_gather(ddev, pfb, resp,
						    digital_in_send_ack,
						    data_exch);
		if (IS_ERR(resp)) {
			rc = PTR_ERR(resp);
			resp = NULL;
			goto error;
		}

		/* If resp is NULL then we're still chaining so return and
		 * wait for the next part of the PDU.  Else, the PDU is
		 * complete so pass it up.
		 */
		if (!resp)
			return;

		rc = 0;
		break;

	case DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU:
		if (DIGITAL_NFC_DEP_NACK_BIT_SET(pfb)) {
			PROTOCOL_ERR("14.12.4.5");
			rc = -EIO;
			goto exit;
		}

		if (DIGITAL_NFC_DEP_PFB_PNI(pfb) != ddev->curr_nfc_dep_pni) {
			PROTOCOL_ERR("14.12.3.3");
			rc = -EIO;
			goto exit;
		}

		ddev->curr_nfc_dep_pni =
			DIGITAL_NFC_DEP_PFB_PNI(ddev->curr_nfc_dep_pni + 1);

		if (!ddev->chaining_skb) {
			PROTOCOL_ERR("14.12.4.3");
			rc = -EIO;
			goto exit;
		}

		/* The initiator has received a valid ACK. Free the last sent
		 * PDU and keep on sending chained skb.
		 */
		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;

		rc = digital_in_send_dep_req(ddev, NULL,
					     ddev->chaining_skb,
					     ddev->data_exch);
		if (rc)
			goto error;

		goto free_resp;

	case DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU:
		if (!DIGITAL_NFC_DEP_PFB_IS_TIMEOUT(pfb)) { /* ATN */
			rc = digital_in_send_saved_skb(ddev, data_exch);
			if (rc)
				goto error;

			goto free_resp;
		}

		rc = digital_in_send_rtox(ddev, data_exch, resp->data[0]);
		if (rc)
			goto error;

		goto free_resp;
	}

exit:
	data_exch->cb(data_exch->cb_context, resp, rc);

error:
	kfree(data_exch);

	kfree_skb(ddev->chaining_skb);
	ddev->chaining_skb = NULL;

	kfree_skb(ddev->saved_skb);
	ddev->saved_skb = NULL;

	if (rc)
		kfree_skb(resp);

	return;

free_resp:
	dev_kfree_skb(resp);
}

int digital_in_send_dep_req(struct nfc_digital_dev *ddev,
			    struct nfc_target *target, struct sk_buff *skb,
			    struct digital_data_exch *data_exch)
{
	struct digital_dep_req_res *dep_req;
	struct sk_buff *chaining_skb, *tmp_skb;
	int rc;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_req = (struct digital_dep_req_res *)skb->data;

	dep_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	dep_req->cmd = DIGITAL_CMD_DEP_REQ;
	dep_req->pfb = ddev->curr_nfc_dep_pni;

	ddev->atn_count = 0;
	ddev->nack_count = 0;

	chaining_skb = ddev->chaining_skb;

	tmp_skb = digital_send_dep_data_prep(ddev, skb, dep_req, data_exch);
	if (IS_ERR(tmp_skb))
		return PTR_ERR(tmp_skb);

	digital_skb_push_dep_sod(ddev, tmp_skb);

	ddev->skb_add_crc(tmp_skb);

	ddev->saved_skb = pskb_copy(tmp_skb, GFP_KERNEL);

	rc = digital_in_send_cmd(ddev, tmp_skb, 1500, digital_in_recv_dep_res,
				 data_exch);
	if (rc) {
		if (tmp_skb != skb)
			kfree_skb(tmp_skb);

		kfree_skb(chaining_skb);
		ddev->chaining_skb = NULL;

		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;
	}

	return rc;
}

static void digital_tg_set_rf_tech(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	ddev->curr_rf_tech = rf_tech;

	ddev->skb_add_crc = digital_skb_add_crc_none;
	ddev->skb_check_crc = digital_skb_check_crc_none;

	if (DIGITAL_DRV_CAPS_TG_CRC(ddev))
		return;

	switch (ddev->curr_rf_tech) {
	case NFC_DIGITAL_RF_TECH_106A:
		ddev->skb_add_crc = digital_skb_add_crc_a;
		ddev->skb_check_crc = digital_skb_check_crc_a;
		break;

	case NFC_DIGITAL_RF_TECH_212F:
	case NFC_DIGITAL_RF_TECH_424F:
		ddev->skb_add_crc = digital_skb_add_crc_f;
		ddev->skb_check_crc = digital_skb_check_crc_f;
		break;

	default:
		break;
	}
}

static int digital_tg_send_ack(struct nfc_digital_dev *ddev,
			       struct digital_data_exch *data_exch)
{
	struct digital_dep_req_res *dep_res;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_res = (struct digital_dep_req_res *)skb->data;

	dep_res->dir = DIGITAL_NFC_DEP_FRAME_DIR_IN;
	dep_res->cmd = DIGITAL_CMD_DEP_RES;
	dep_res->pfb = DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU |
		       ddev->curr_nfc_dep_pni;

	if (ddev->did) {
		dep_res->pfb |= DIGITAL_NFC_DEP_PFB_DID_BIT;

		memcpy(skb_put(skb, sizeof(ddev->did)), &ddev->did,
		       sizeof(ddev->did));
	}

	ddev->curr_nfc_dep_pni =
		DIGITAL_NFC_DEP_PFB_PNI(ddev->curr_nfc_dep_pni + 1);

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	ddev->saved_skb = pskb_copy(skb, GFP_KERNEL);

	rc = digital_tg_send_cmd(ddev, skb, 1500, digital_tg_recv_dep_req,
				 data_exch);
	if (rc) {
		kfree_skb(skb);
		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;
	}

	return rc;
}

static int digital_tg_send_atn(struct nfc_digital_dev *ddev)
{
	struct digital_dep_req_res *dep_res;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_res = (struct digital_dep_req_res *)skb->data;

	dep_res->dir = DIGITAL_NFC_DEP_FRAME_DIR_IN;
	dep_res->cmd = DIGITAL_CMD_DEP_RES;
	dep_res->pfb = DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU;

	if (ddev->did) {
		dep_res->pfb |= DIGITAL_NFC_DEP_PFB_DID_BIT;

		memcpy(skb_put(skb, sizeof(ddev->did)), &ddev->did,
		       sizeof(ddev->did));
	}

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	rc = digital_tg_send_cmd(ddev, skb, 1500, digital_tg_recv_dep_req,
				 NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static int digital_tg_send_saved_skb(struct nfc_digital_dev *ddev)
{
	int rc;

	if (!ddev->saved_skb)
		return -EINVAL;

	skb_get(ddev->saved_skb);

	rc = digital_tg_send_cmd(ddev, ddev->saved_skb, 1500,
				 digital_tg_recv_dep_req, NULL);
	if (rc)
		kfree_skb(ddev->saved_skb);

	return rc;
}

static void digital_tg_recv_dep_req(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	int rc;
	struct digital_dep_req_res *dep_req;
	u8 pfb;
	size_t size;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	rc = ddev->skb_check_crc(resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.6");
		goto exit;
	}

	rc = digital_skb_pull_dep_sod(ddev, resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.2");
		goto exit;
	}

	if (resp->len > ddev->local_payload_max) {
		rc = -EMSGSIZE;
		goto exit;
	}

	size = sizeof(struct digital_dep_req_res);
	dep_req = (struct digital_dep_req_res *)resp->data;

	if (resp->len < size || dep_req->dir != DIGITAL_NFC_DEP_FRAME_DIR_OUT ||
	    dep_req->cmd != DIGITAL_CMD_DEP_REQ) {
		rc = -EIO;
		goto exit;
	}

	pfb = dep_req->pfb;

	if (DIGITAL_NFC_DEP_DID_BIT_SET(pfb)) {
		if (ddev->did && (ddev->did == resp->data[3])) {
			size++;
		} else {
			rc = -EIO;
			goto exit;
		}
	} else if (ddev->did) {
		rc = -EIO;
		goto exit;
	}

	if (DIGITAL_NFC_DEP_NAD_BIT_SET(pfb)) {
		rc = -EIO;
		goto exit;
	}

	if (size > resp->len) {
		rc = -EIO;
		goto exit;
	}

	skb_pull(resp, size);

	switch (DIGITAL_NFC_DEP_PFB_TYPE(pfb)) {
	case DIGITAL_NFC_DEP_PFB_I_PDU:
		pr_debug("DIGITAL_NFC_DEP_PFB_I_PDU\n");

		if (ddev->atn_count) {
			/* The target has received (and replied to) at least one
			 * ATN DEP_REQ.
			 */
			ddev->atn_count = 0;

			/* pni of resp PDU equal to the target current pni - 1
			 * means resp is the previous DEP_REQ PDU received from
			 * the initiator so the target replies with saved_skb
			 * which is the previous DEP_RES saved in
			 * digital_tg_send_dep_res().
			 */
			if (DIGITAL_NFC_DEP_PFB_PNI(pfb) ==
			  DIGITAL_NFC_DEP_PFB_PNI(ddev->curr_nfc_dep_pni - 1)) {
				rc = digital_tg_send_saved_skb(ddev);
				if (rc)
					goto exit;

				goto free_resp;
			}

			/* atn_count > 0 and PDU pni != curr_nfc_dep_pni - 1
			 * means the target probably did not received the last
			 * DEP_REQ PDU sent by the initiator. The target
			 * fallbacks to normal processing then.
			 */
		}

		if (DIGITAL_NFC_DEP_PFB_PNI(pfb) != ddev->curr_nfc_dep_pni) {
			PROTOCOL_ERR("14.12.3.4");
			rc = -EIO;
			goto exit;
		}

		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;

		resp = digital_recv_dep_data_gather(ddev, pfb, resp,
						    digital_tg_send_ack, NULL);
		if (IS_ERR(resp)) {
			rc = PTR_ERR(resp);
			resp = NULL;
			goto exit;
		}

		/* If resp is NULL then we're still chaining so return and
		 * wait for the next part of the PDU.  Else, the PDU is
		 * complete so pass it up.
		 */
		if (!resp)
			return;

		rc = 0;
		break;
	case DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU:
		if (DIGITAL_NFC_DEP_NACK_BIT_SET(pfb)) { /* NACK */
			if (DIGITAL_NFC_DEP_PFB_PNI(pfb + 1) !=
						ddev->curr_nfc_dep_pni) {
				rc = -EIO;
				goto exit;
			}

			ddev->atn_count = 0;

			rc = digital_tg_send_saved_skb(ddev);
			if (rc)
				goto exit;

			goto free_resp;
		}

		/* ACK */
		if (ddev->atn_count) {
			/* The target has previously recevied one or more ATN
			 * PDUs.
			 */
			ddev->atn_count = 0;

			/* If the ACK PNI is equal to the target PNI - 1 means
			 * that the initiator did not receive the previous PDU
			 * sent by the target so re-send it.
			 */
			if (DIGITAL_NFC_DEP_PFB_PNI(pfb + 1) ==
						ddev->curr_nfc_dep_pni) {
				rc = digital_tg_send_saved_skb(ddev);
				if (rc)
					goto exit;

				goto free_resp;
			}

			/* Otherwise, the target did not receive the previous
			 * ACK PDU from the initiator. Fallback to normal
			 * processing of chained PDU then.
			 */
		}

		/* Keep on sending chained PDU */
		if (!ddev->chaining_skb ||
		    DIGITAL_NFC_DEP_PFB_PNI(pfb) !=
					ddev->curr_nfc_dep_pni) {
			rc = -EIO;
			goto exit;
		}

		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;

		rc = digital_tg_send_dep_res(ddev, ddev->chaining_skb);
		if (rc)
			goto exit;

		goto free_resp;
	case DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU:
		if (DIGITAL_NFC_DEP_PFB_IS_TIMEOUT(pfb)) {
			rc = -EINVAL;
			goto exit;
		}

		rc = digital_tg_send_atn(ddev);
		if (rc)
			goto exit;

		ddev->atn_count++;

		goto free_resp;
	}

	rc = nfc_tm_data_received(ddev->nfc_dev, resp);

exit:
	kfree_skb(ddev->chaining_skb);
	ddev->chaining_skb = NULL;

	ddev->atn_count = 0;

	kfree_skb(ddev->saved_skb);
	ddev->saved_skb = NULL;

	if (rc)
		kfree_skb(resp);

	return;

free_resp:
	dev_kfree_skb(resp);
}

int digital_tg_send_dep_res(struct nfc_digital_dev *ddev, struct sk_buff *skb)
{
	struct digital_dep_req_res *dep_res;
	struct sk_buff *chaining_skb, *tmp_skb;
	int rc;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_res = (struct digital_dep_req_res *)skb->data;

	dep_res->dir = DIGITAL_NFC_DEP_FRAME_DIR_IN;
	dep_res->cmd = DIGITAL_CMD_DEP_RES;
	dep_res->pfb = ddev->curr_nfc_dep_pni;

	if (ddev->did) {
		dep_res->pfb |= DIGITAL_NFC_DEP_PFB_DID_BIT;

		memcpy(skb_put(skb, sizeof(ddev->did)), &ddev->did,
		       sizeof(ddev->did));
	}

	ddev->curr_nfc_dep_pni =
		DIGITAL_NFC_DEP_PFB_PNI(ddev->curr_nfc_dep_pni + 1);

	chaining_skb = ddev->chaining_skb;

	tmp_skb = digital_send_dep_data_prep(ddev, skb, dep_res, NULL);
	if (IS_ERR(tmp_skb))
		return PTR_ERR(tmp_skb);

	digital_skb_push_dep_sod(ddev, tmp_skb);

	ddev->skb_add_crc(tmp_skb);

	ddev->saved_skb = pskb_copy(tmp_skb, GFP_KERNEL);

	rc = digital_tg_send_cmd(ddev, tmp_skb, 1500, digital_tg_recv_dep_req,
				 NULL);
	if (rc) {
		if (tmp_skb != skb)
			kfree_skb(tmp_skb);

		kfree_skb(chaining_skb);
		ddev->chaining_skb = NULL;

		kfree_skb(ddev->saved_skb);
		ddev->saved_skb = NULL;
	}

	return rc;
}

static void digital_tg_send_psl_res_complete(struct nfc_digital_dev *ddev,
					     void *arg, struct sk_buff *resp)
{
	u8 rf_tech = (unsigned long)arg;

	if (IS_ERR(resp))
		return;

	digital_tg_set_rf_tech(ddev, rf_tech);

	digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH, rf_tech);

	digital_tg_listen(ddev, 1500, digital_tg_recv_dep_req, NULL);

	dev_kfree_skb(resp);
}

static int digital_tg_send_psl_res(struct nfc_digital_dev *ddev, u8 did,
				   u8 rf_tech)
{
	struct digital_psl_res *psl_res;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, sizeof(struct digital_psl_res));
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(struct digital_psl_res));

	psl_res = (struct digital_psl_res *)skb->data;

	psl_res->dir = DIGITAL_NFC_DEP_FRAME_DIR_IN;
	psl_res->cmd = DIGITAL_CMD_PSL_RES;
	psl_res->did = did;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	ddev->curr_nfc_dep_pni = 0;

	rc = digital_tg_send_cmd(ddev, skb, 0, digital_tg_send_psl_res_complete,
				 (void *)(unsigned long)rf_tech);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_tg_recv_psl_req(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	int rc;
	struct digital_psl_req *psl_req;
	u8 rf_tech;
	u8 dsi, payload_size, payload_bits;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	rc = ddev->skb_check_crc(resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.6");
		goto exit;
	}

	rc = digital_skb_pull_dep_sod(ddev, resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.2");
		goto exit;
	}

	psl_req = (struct digital_psl_req *)resp->data;

	if (resp->len != sizeof(struct digital_psl_req) ||
	    psl_req->dir != DIGITAL_NFC_DEP_FRAME_DIR_OUT ||
	    psl_req->cmd != DIGITAL_CMD_PSL_REQ) {
		rc = -EIO;
		goto exit;
	}

	dsi = (psl_req->brs >> 3) & 0x07;
	switch (dsi) {
	case 0:
		rf_tech = NFC_DIGITAL_RF_TECH_106A;
		break;
	case 1:
		rf_tech = NFC_DIGITAL_RF_TECH_212F;
		break;
	case 2:
		rf_tech = NFC_DIGITAL_RF_TECH_424F;
		break;
	default:
		pr_err("Unsupported dsi value %d\n", dsi);
		goto exit;
	}

	payload_bits = DIGITAL_PAYLOAD_FSL_TO_BITS(psl_req->fsl);
	payload_size = digital_payload_bits_to_size(payload_bits);

	if (!payload_size || (payload_size > min(ddev->local_payload_max,
						 ddev->remote_payload_max))) {
		rc = -EINVAL;
		goto exit;
	}

	ddev->local_payload_max = payload_size;
	ddev->remote_payload_max = payload_size;

	rc = digital_tg_send_psl_res(ddev, psl_req->did, rf_tech);

exit:
	kfree_skb(resp);
}

static void digital_tg_send_atr_res_complete(struct nfc_digital_dev *ddev,
					     void *arg, struct sk_buff *resp)
{
	int offset;

	if (IS_ERR(resp)) {
		digital_poll_next_tech(ddev);
		return;
	}

	offset = 2;
	if (resp->data[0] == DIGITAL_NFC_DEP_NFCA_SOD_SB)
		offset++;

	ddev->atn_count = 0;

	if (resp->data[offset] == DIGITAL_CMD_PSL_REQ)
		digital_tg_recv_psl_req(ddev, arg, resp);
	else
		digital_tg_recv_dep_req(ddev, arg, resp);
}

static int digital_tg_send_atr_res(struct nfc_digital_dev *ddev,
				   struct digital_atr_req *atr_req)
{
	struct digital_atr_res *atr_res;
	struct sk_buff *skb;
	u8 *gb, payload_bits;
	size_t gb_len;
	int rc;

	gb = nfc_get_local_general_bytes(ddev->nfc_dev, &gb_len);
	if (!gb)
		gb_len = 0;

	skb = digital_skb_alloc(ddev, sizeof(struct digital_atr_res) + gb_len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(struct digital_atr_res));
	atr_res = (struct digital_atr_res *)skb->data;

	memset(atr_res, 0, sizeof(struct digital_atr_res));

	atr_res->dir = DIGITAL_NFC_DEP_FRAME_DIR_IN;
	atr_res->cmd = DIGITAL_CMD_ATR_RES;
	memcpy(atr_res->nfcid3, atr_req->nfcid3, sizeof(atr_req->nfcid3));
	atr_res->to = 8;

	ddev->local_payload_max = DIGITAL_PAYLOAD_SIZE_MAX;
	payload_bits = digital_payload_size_to_bits(ddev->local_payload_max);
	atr_res->pp = DIGITAL_PAYLOAD_BITS_TO_PP(payload_bits);

	if (gb_len) {
		skb_put(skb, gb_len);

		atr_res->pp |= DIGITAL_GB_BIT;
		memcpy(atr_res->gb, gb, gb_len);
	}

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	ddev->curr_nfc_dep_pni = 0;

	rc = digital_tg_send_cmd(ddev, skb, 999,
				 digital_tg_send_atr_res_complete, NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

void digital_tg_recv_atr_req(struct nfc_digital_dev *ddev, void *arg,
			     struct sk_buff *resp)
{
	int rc;
	struct digital_atr_req *atr_req;
	size_t gb_len, min_size;
	u8 poll_tech_count, payload_bits;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (!resp->len) {
		rc = -EIO;
		goto exit;
	}

	if (resp->data[0] == DIGITAL_NFC_DEP_NFCA_SOD_SB) {
		min_size = DIGITAL_ATR_REQ_MIN_SIZE + 2;
		digital_tg_set_rf_tech(ddev, NFC_DIGITAL_RF_TECH_106A);
	} else {
		min_size = DIGITAL_ATR_REQ_MIN_SIZE + 1;
		digital_tg_set_rf_tech(ddev, NFC_DIGITAL_RF_TECH_212F);
	}

	if (resp->len < min_size) {
		rc = -EIO;
		goto exit;
	}

	ddev->curr_protocol = NFC_PROTO_NFC_DEP_MASK;

	rc = ddev->skb_check_crc(resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.6");
		goto exit;
	}

	rc = digital_skb_pull_dep_sod(ddev, resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.2");
		goto exit;
	}

	atr_req = (struct digital_atr_req *)resp->data;

	if (atr_req->dir != DIGITAL_NFC_DEP_FRAME_DIR_OUT ||
	    atr_req->cmd != DIGITAL_CMD_ATR_REQ ||
	    atr_req->did > DIGITAL_DID_MAX) {
		rc = -EINVAL;
		goto exit;
	}

	payload_bits = DIGITAL_PAYLOAD_PP_TO_BITS(atr_req->pp);
	ddev->remote_payload_max = digital_payload_bits_to_size(payload_bits);

	if (!ddev->remote_payload_max) {
		rc = -EINVAL;
		goto exit;
	}

	ddev->did = atr_req->did;

	rc = digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFC_DEP_ACTIVATED);
	if (rc)
		goto exit;

	rc = digital_tg_send_atr_res(ddev, atr_req);
	if (rc)
		goto exit;

	gb_len = resp->len - sizeof(struct digital_atr_req);

	poll_tech_count = ddev->poll_tech_count;
	ddev->poll_tech_count = 0;

	rc = nfc_tm_activated(ddev->nfc_dev, NFC_PROTO_NFC_DEP_MASK,
			      NFC_COMM_PASSIVE, atr_req->gb, gb_len);
	if (rc) {
		ddev->poll_tech_count = poll_tech_count;
		goto exit;
	}

	rc = 0;
exit:
	if (rc)
		digital_poll_next_tech(ddev);

	dev_kfree_skb(resp);
}
