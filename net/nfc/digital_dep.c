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

#define DIGITAL_LR_BITS_PAYLOAD_SIZE_254B 0x30
#define DIGITAL_GB_BIT	0x02

#define DIGITAL_NFC_DEP_PFB_TYPE(pfb) ((pfb) & 0xE0)

#define DIGITAL_NFC_DEP_PFB_TIMEOUT_BIT 0x10

#define DIGITAL_NFC_DEP_PFB_IS_TIMEOUT(pfb) \
				((pfb) & DIGITAL_NFC_DEP_PFB_TIMEOUT_BIT)
#define DIGITAL_NFC_DEP_MI_BIT_SET(pfb)  ((pfb) & 0x10)
#define DIGITAL_NFC_DEP_NAD_BIT_SET(pfb) ((pfb) & 0x08)
#define DIGITAL_NFC_DEP_DID_BIT_SET(pfb) ((pfb) & 0x04)
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

static void digital_in_recv_atr_res(struct nfc_digital_dev *ddev, void *arg,
				 struct sk_buff *resp)
{
	struct nfc_target *target = arg;
	struct digital_atr_res *atr_res;
	u8 gb_len;
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

	rc = nfc_set_remote_general_bytes(ddev->nfc_dev, atr_res->gb, gb_len);
	if (rc)
		goto exit;

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

	atr_req->pp = DIGITAL_LR_BITS_PAYLOAD_SIZE_254B;

	if (gb_len) {
		atr_req->pp |= DIGITAL_GB_BIT;
		memcpy(skb_put(skb, gb_len), gb, gb_len);
	}

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	return digital_in_send_cmd(ddev, skb, 500, digital_in_recv_atr_res,
				   target);
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
		goto exit;
	}

	rc = ddev->skb_check_crc(resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.6");
		goto error;
	}

	rc = digital_skb_pull_dep_sod(ddev, resp);
	if (rc) {
		PROTOCOL_ERR("14.4.1.2");
		goto exit;
	}

	dep_res = (struct digital_dep_req_res *)resp->data;

	if (resp->len < sizeof(struct digital_dep_req_res) ||
	    dep_res->dir != DIGITAL_NFC_DEP_FRAME_DIR_IN ||
	    dep_res->cmd != DIGITAL_CMD_DEP_RES) {
		rc = -EIO;
		goto error;
	}

	pfb = dep_res->pfb;

	switch (DIGITAL_NFC_DEP_PFB_TYPE(pfb)) {
	case DIGITAL_NFC_DEP_PFB_I_PDU:
		if (DIGITAL_NFC_DEP_PFB_PNI(pfb) != ddev->curr_nfc_dep_pni) {
			PROTOCOL_ERR("14.12.3.3");
			rc = -EIO;
			goto error;
		}

		ddev->curr_nfc_dep_pni =
			DIGITAL_NFC_DEP_PFB_PNI(ddev->curr_nfc_dep_pni + 1);
		rc = 0;
		break;

	case DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU:
		pr_err("Received a ACK/NACK PDU\n");
		rc = -EIO;
		goto error;

	case DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU:
		if (!DIGITAL_NFC_DEP_PFB_IS_TIMEOUT(pfb)) {
			rc = -EINVAL;
			goto error;
		}

		rc = digital_in_send_rtox(ddev, data_exch, resp->data[3]);
		if (rc)
			goto error;

		kfree_skb(resp);
		return;
	}

	if (DIGITAL_NFC_DEP_MI_BIT_SET(pfb)) {
		pr_err("MI bit set. Chained PDU not supported\n");
		rc = -EIO;
		goto error;
	}

	size = sizeof(struct digital_dep_req_res);

	if (DIGITAL_NFC_DEP_DID_BIT_SET(pfb))
		size++;

	if (size > resp->len) {
		rc = -EIO;
		goto error;
	}

	skb_pull(resp, size);

exit:
	data_exch->cb(data_exch->cb_context, resp, rc);

error:
	kfree(data_exch);

	if (rc)
		kfree_skb(resp);
}

int digital_in_send_dep_req(struct nfc_digital_dev *ddev,
			    struct nfc_target *target, struct sk_buff *skb,
			    struct digital_data_exch *data_exch)
{
	struct digital_dep_req_res *dep_req;

	skb_push(skb, sizeof(struct digital_dep_req_res));

	dep_req = (struct digital_dep_req_res *)skb->data;
	dep_req->dir = DIGITAL_NFC_DEP_FRAME_DIR_OUT;
	dep_req->cmd = DIGITAL_CMD_DEP_REQ;
	dep_req->pfb = ddev->curr_nfc_dep_pni;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	return digital_in_send_cmd(ddev, skb, 1500, digital_in_recv_dep_res,
				   data_exch);
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

static void digital_tg_recv_dep_req(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	int rc;
	struct digital_dep_req_res *dep_req;
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

	size = sizeof(struct digital_dep_req_res);
	dep_req = (struct digital_dep_req_res *)resp->data;

	if (resp->len < size || dep_req->dir != DIGITAL_NFC_DEP_FRAME_DIR_OUT ||
	    dep_req->cmd != DIGITAL_CMD_DEP_REQ) {
		rc = -EIO;
		goto exit;
	}

	if (DIGITAL_NFC_DEP_DID_BIT_SET(dep_req->pfb))
		size++;

	if (resp->len < size) {
		rc = -EIO;
		goto exit;
	}

	switch (DIGITAL_NFC_DEP_PFB_TYPE(dep_req->pfb)) {
	case DIGITAL_NFC_DEP_PFB_I_PDU:
		pr_debug("DIGITAL_NFC_DEP_PFB_I_PDU\n");
		ddev->curr_nfc_dep_pni = DIGITAL_NFC_DEP_PFB_PNI(dep_req->pfb);
		break;
	case DIGITAL_NFC_DEP_PFB_ACK_NACK_PDU:
		pr_err("Received a ACK/NACK PDU\n");
		rc = -EINVAL;
		goto exit;
	case DIGITAL_NFC_DEP_PFB_SUPERVISOR_PDU:
		pr_err("Received a SUPERVISOR PDU\n");
		rc = -EINVAL;
		goto exit;
	}

	skb_pull(resp, size);

	rc = nfc_tm_data_received(ddev->nfc_dev, resp);

exit:
	if (rc)
		kfree_skb(resp);
}

int digital_tg_send_dep_res(struct nfc_digital_dev *ddev, struct sk_buff *skb)
{
	struct digital_dep_req_res *dep_res;

	skb_push(skb, sizeof(struct digital_dep_req_res));
	dep_res = (struct digital_dep_req_res *)skb->data;

	dep_res->dir = DIGITAL_NFC_DEP_FRAME_DIR_IN;
	dep_res->cmd = DIGITAL_CMD_DEP_RES;
	dep_res->pfb = ddev->curr_nfc_dep_pni;

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	return digital_tg_send_cmd(ddev, skb, 1500, digital_tg_recv_dep_req,
				   NULL);
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
	u8 dsi;

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
	u8 *gb;
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
	atr_res->pp = DIGITAL_LR_BITS_PAYLOAD_SIZE_254B;
	if (gb_len) {
		skb_put(skb, gb_len);

		atr_res->pp |= DIGITAL_GB_BIT;
		memcpy(atr_res->gb, gb, gb_len);
	}

	digital_skb_push_dep_sod(ddev, skb);

	ddev->skb_add_crc(skb);

	rc = digital_tg_send_cmd(ddev, skb, 999,
				 digital_tg_send_atr_res_complete, NULL);
	if (rc) {
		kfree_skb(skb);
		return rc;
	}

	return rc;
}

void digital_tg_recv_atr_req(struct nfc_digital_dev *ddev, void *arg,
			     struct sk_buff *resp)
{
	int rc;
	struct digital_atr_req *atr_req;
	size_t gb_len, min_size;

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
	    atr_req->cmd != DIGITAL_CMD_ATR_REQ) {
		rc = -EINVAL;
		goto exit;
	}

	rc = digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFC_DEP_ACTIVATED);
	if (rc)
		goto exit;

	rc = digital_tg_send_atr_res(ddev, atr_req);
	if (rc)
		goto exit;

	gb_len = resp->len - sizeof(struct digital_atr_req);
	rc = nfc_tm_activated(ddev->nfc_dev, NFC_PROTO_NFC_DEP_MASK,
			      NFC_COMM_PASSIVE, atr_req->gb, gb_len);
	if (rc)
		goto exit;

	ddev->poll_tech_count = 0;

	rc = 0;
exit:
	if (rc)
		digital_poll_next_tech(ddev);

	dev_kfree_skb(resp);
}
