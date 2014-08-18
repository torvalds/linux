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

#define DIGITAL_CMD_SENS_REQ    0x26
#define DIGITAL_CMD_ALL_REQ     0x52
#define DIGITAL_CMD_SEL_REQ_CL1 0x93
#define DIGITAL_CMD_SEL_REQ_CL2 0x95
#define DIGITAL_CMD_SEL_REQ_CL3 0x97

#define DIGITAL_SDD_REQ_SEL_PAR 0x20

#define DIGITAL_SDD_RES_CT  0x88
#define DIGITAL_SDD_RES_LEN 5

#define DIGITAL_SEL_RES_NFCID1_COMPLETE(sel_res) (!((sel_res) & 0x04))
#define DIGITAL_SEL_RES_IS_T2T(sel_res) (!((sel_res) & 0x60))
#define DIGITAL_SEL_RES_IS_T4T(sel_res) ((sel_res) & 0x20)
#define DIGITAL_SEL_RES_IS_NFC_DEP(sel_res) ((sel_res) & 0x40)

#define DIGITAL_SENS_RES_IS_T1T(sens_res) (((sens_res) & 0x0C00) == 0x0C00)
#define DIGITAL_SENS_RES_IS_VALID(sens_res) \
	((!((sens_res) & 0x001F) && (((sens_res) & 0x0C00) == 0x0C00)) || \
	(((sens_res) & 0x001F) && ((sens_res) & 0x0C00) != 0x0C00))

#define DIGITAL_MIFARE_READ_RES_LEN 16
#define DIGITAL_MIFARE_ACK_RES	0x0A

#define DIGITAL_CMD_SENSB_REQ			0x05
#define DIGITAL_SENSB_ADVANCED			BIT(5)
#define DIGITAL_SENSB_EXTENDED			BIT(4)
#define DIGITAL_SENSB_ALLB_REQ			BIT(3)
#define DIGITAL_SENSB_N(n)			((n) & 0x7)

#define DIGITAL_CMD_SENSB_RES			0x50

#define DIGITAL_CMD_ATTRIB_REQ			0x1D
#define DIGITAL_ATTRIB_P1_TR0_DEFAULT		(0x0 << 6)
#define DIGITAL_ATTRIB_P1_TR1_DEFAULT		(0x0 << 4)
#define DIGITAL_ATTRIB_P1_SUPRESS_EOS		BIT(3)
#define DIGITAL_ATTRIB_P1_SUPRESS_SOS		BIT(2)
#define DIGITAL_ATTRIB_P2_LISTEN_POLL_1		(0x0 << 6)
#define DIGITAL_ATTRIB_P2_POLL_LISTEN_1		(0x0 << 4)
#define DIGITAL_ATTRIB_P2_MAX_FRAME_256		0x8
#define DIGITAL_ATTRIB_P4_DID(n)		((n) & 0xf)

#define DIGITAL_CMD_SENSF_REQ	0x00
#define DIGITAL_CMD_SENSF_RES	0x01

#define DIGITAL_SENSF_RES_MIN_LENGTH 17
#define DIGITAL_SENSF_RES_RD_AP_B1   0x00
#define DIGITAL_SENSF_RES_RD_AP_B2   0x8F

#define DIGITAL_SENSF_REQ_RC_NONE 0
#define DIGITAL_SENSF_REQ_RC_SC   1
#define DIGITAL_SENSF_REQ_RC_AP   2

#define DIGITAL_CMD_ISO15693_INVENTORY_REQ	0x01

#define DIGITAL_ISO15693_REQ_FLAG_DATA_RATE	BIT(1)
#define DIGITAL_ISO15693_REQ_FLAG_INVENTORY	BIT(2)
#define DIGITAL_ISO15693_REQ_FLAG_NB_SLOTS	BIT(5)
#define DIGITAL_ISO15693_RES_FLAG_ERROR		BIT(0)
#define DIGITAL_ISO15693_RES_IS_VALID(flags) \
	(!((flags) & DIGITAL_ISO15693_RES_FLAG_ERROR))

#define DIGITAL_ISO_DEP_I_PCB	 0x02
#define DIGITAL_ISO_DEP_PNI(pni) ((pni) & 0x01)

#define DIGITAL_ISO_DEP_PCB_TYPE(pcb) ((pcb) & 0xC0)

#define DIGITAL_ISO_DEP_I_BLOCK 0x00

#define DIGITAL_ISO_DEP_BLOCK_HAS_DID(pcb) ((pcb) & 0x08)

static const u8 digital_ats_fsc[] = {
	 16,  24,  32,  40,  48,  64,  96, 128,
};

#define DIGITAL_ATS_FSCI(t0) ((t0) & 0x0F)
#define DIGITAL_SENSB_FSCI(pi2) (((pi2) & 0xF0) >> 4)
#define DIGITAL_ATS_MAX_FSC  256

#define DIGITAL_RATS_BYTE1 0xE0
#define DIGITAL_RATS_PARAM 0x80

struct digital_sdd_res {
	u8 nfcid1[4];
	u8 bcc;
} __packed;

struct digital_sel_req {
	u8 sel_cmd;
	u8 b2;
	u8 nfcid1[4];
	u8 bcc;
} __packed;

struct digital_sensb_req {
	u8 cmd;
	u8 afi;
	u8 param;
} __packed;

struct digital_sensb_res {
	u8 cmd;
	u8 nfcid0[4];
	u8 app_data[4];
	u8 proto_info[3];
} __packed;

struct digital_attrib_req {
	u8 cmd;
	u8 nfcid0[4];
	u8 param1;
	u8 param2;
	u8 param3;
	u8 param4;
} __packed;

struct digital_attrib_res {
	u8 mbli_did;
} __packed;

struct digital_sensf_req {
	u8 cmd;
	u8 sc1;
	u8 sc2;
	u8 rc;
	u8 tsn;
} __packed;

struct digital_sensf_res {
	u8 cmd;
	u8 nfcid2[8];
	u8 pad0[2];
	u8 pad1[3];
	u8 mrti_check;
	u8 mrti_update;
	u8 pad2;
	u8 rd[2];
} __packed;

struct digital_iso15693_inv_req {
	u8 flags;
	u8 cmd;
	u8 mask_len;
	u64 mask;
} __packed;

struct digital_iso15693_inv_res {
	u8 flags;
	u8 dsfid;
	u64 uid;
} __packed;

static int digital_in_send_sdd_req(struct nfc_digital_dev *ddev,
				   struct nfc_target *target);

int digital_in_iso_dep_pull_sod(struct nfc_digital_dev *ddev,
				struct sk_buff *skb)
{
	u8 pcb;
	u8 block_type;

	if (skb->len < 1)
		return -EIO;

	pcb = *skb->data;
	block_type = DIGITAL_ISO_DEP_PCB_TYPE(pcb);

	/* No support fo R-block nor S-block */
	if (block_type != DIGITAL_ISO_DEP_I_BLOCK) {
		pr_err("ISO_DEP R-block and S-block not supported\n");
		return -EIO;
	}

	if (DIGITAL_ISO_DEP_BLOCK_HAS_DID(pcb)) {
		pr_err("DID field in ISO_DEP PCB not supported\n");
		return -EIO;
	}

	skb_pull(skb, 1);

	return 0;
}

int digital_in_iso_dep_push_sod(struct nfc_digital_dev *ddev,
				struct sk_buff *skb)
{
	/*
	 * Chaining not supported so skb->len + 1 PCB byte + 2 CRC bytes must
	 * not be greater than remote FSC
	 */
	if (skb->len + 3 > ddev->target_fsc)
		return -EIO;

	skb_push(skb, 1);

	*skb->data = DIGITAL_ISO_DEP_I_PCB | ddev->curr_nfc_dep_pni;

	ddev->curr_nfc_dep_pni =
		DIGITAL_ISO_DEP_PNI(ddev->curr_nfc_dep_pni + 1);

	return 0;
}

static void digital_in_recv_ats(struct nfc_digital_dev *ddev, void *arg,
				struct sk_buff *resp)
{
	struct nfc_target *target = arg;
	u8 fsdi;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (resp->len < 2) {
		rc = -EIO;
		goto exit;
	}

	fsdi = DIGITAL_ATS_FSCI(resp->data[1]);
	if (fsdi >= 8)
		ddev->target_fsc = DIGITAL_ATS_MAX_FSC;
	else
		ddev->target_fsc = digital_ats_fsc[fsdi];

	ddev->curr_nfc_dep_pni = 0;

	rc = digital_target_found(ddev, target, NFC_PROTO_ISO14443);

exit:
	dev_kfree_skb(resp);
	kfree(target);

	if (rc)
		digital_poll_next_tech(ddev);
}

static int digital_in_send_rats(struct nfc_digital_dev *ddev,
				struct nfc_target *target)
{
	int rc;
	struct sk_buff *skb;

	skb = digital_skb_alloc(ddev, 2);
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, 1) = DIGITAL_RATS_BYTE1;
	*skb_put(skb, 1) = DIGITAL_RATS_PARAM;

	rc = digital_in_send_cmd(ddev, skb, 30, digital_in_recv_ats,
				 target);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_in_recv_sel_res(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	struct nfc_target *target = arg;
	int rc;
	u8 sel_res;
	u8 nfc_proto;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (!DIGITAL_DRV_CAPS_IN_CRC(ddev)) {
		rc = digital_skb_check_crc_a(resp);
		if (rc) {
			PROTOCOL_ERR("4.4.1.3");
			goto exit;
		}
	}

	if (!resp->len) {
		rc = -EIO;
		goto exit;
	}

	sel_res = resp->data[0];

	if (!DIGITAL_SEL_RES_NFCID1_COMPLETE(sel_res)) {
		rc = digital_in_send_sdd_req(ddev, target);
		if (rc)
			goto exit;

		goto exit_free_skb;
	}

	target->sel_res = sel_res;

	if (DIGITAL_SEL_RES_IS_T2T(sel_res)) {
		nfc_proto = NFC_PROTO_MIFARE;
	} else if (DIGITAL_SEL_RES_IS_NFC_DEP(sel_res)) {
		nfc_proto = NFC_PROTO_NFC_DEP;
	} else if (DIGITAL_SEL_RES_IS_T4T(sel_res)) {
		rc = digital_in_send_rats(ddev, target);
		if (rc)
			goto exit;
		/*
		 * Skip target_found and don't free it for now. This will be
		 * done when receiving the ATS
		 */
		goto exit_free_skb;
	} else {
		rc = -EOPNOTSUPP;
		goto exit;
	}

	rc = digital_target_found(ddev, target, nfc_proto);

exit:
	kfree(target);

exit_free_skb:
	dev_kfree_skb(resp);

	if (rc)
		digital_poll_next_tech(ddev);
}

static int digital_in_send_sel_req(struct nfc_digital_dev *ddev,
				   struct nfc_target *target,
				   struct digital_sdd_res *sdd_res)
{
	struct sk_buff *skb;
	struct digital_sel_req *sel_req;
	u8 sel_cmd;
	int rc;

	skb = digital_skb_alloc(ddev, sizeof(struct digital_sel_req));
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(struct digital_sel_req));
	sel_req = (struct digital_sel_req *)skb->data;

	if (target->nfcid1_len <= 4)
		sel_cmd = DIGITAL_CMD_SEL_REQ_CL1;
	else if (target->nfcid1_len < 10)
		sel_cmd = DIGITAL_CMD_SEL_REQ_CL2;
	else
		sel_cmd = DIGITAL_CMD_SEL_REQ_CL3;

	sel_req->sel_cmd = sel_cmd;
	sel_req->b2 = 0x70;
	memcpy(sel_req->nfcid1, sdd_res->nfcid1, 4);
	sel_req->bcc = sdd_res->bcc;

	if (DIGITAL_DRV_CAPS_IN_CRC(ddev)) {
		rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				NFC_DIGITAL_FRAMING_NFCA_STANDARD_WITH_CRC_A);
		if (rc)
			goto exit;
	} else {
		digital_skb_add_crc_a(skb);
	}

	rc = digital_in_send_cmd(ddev, skb, 30, digital_in_recv_sel_res,
				 target);
exit:
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_in_recv_sdd_res(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	struct nfc_target *target = arg;
	struct digital_sdd_res *sdd_res;
	int rc;
	u8 offset, size;
	u8 i, bcc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (resp->len < DIGITAL_SDD_RES_LEN) {
		PROTOCOL_ERR("4.7.2.8");
		rc = -EINVAL;
		goto exit;
	}

	sdd_res = (struct digital_sdd_res *)resp->data;

	for (i = 0, bcc = 0; i < 4; i++)
		bcc ^= sdd_res->nfcid1[i];

	if (bcc != sdd_res->bcc) {
		PROTOCOL_ERR("4.7.2.6");
		rc = -EINVAL;
		goto exit;
	}

	if (sdd_res->nfcid1[0] == DIGITAL_SDD_RES_CT) {
		offset = 1;
		size = 3;
	} else {
		offset = 0;
		size = 4;
	}

	memcpy(target->nfcid1 + target->nfcid1_len, sdd_res->nfcid1 + offset,
	       size);
	target->nfcid1_len += size;

	rc = digital_in_send_sel_req(ddev, target, sdd_res);

exit:
	dev_kfree_skb(resp);

	if (rc) {
		kfree(target);
		digital_poll_next_tech(ddev);
	}
}

static int digital_in_send_sdd_req(struct nfc_digital_dev *ddev,
				   struct nfc_target *target)
{
	int rc;
	struct sk_buff *skb;
	u8 sel_cmd;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCA_STANDARD);
	if (rc)
		return rc;

	skb = digital_skb_alloc(ddev, 2);
	if (!skb)
		return -ENOMEM;

	if (target->nfcid1_len == 0)
		sel_cmd = DIGITAL_CMD_SEL_REQ_CL1;
	else if (target->nfcid1_len == 3)
		sel_cmd = DIGITAL_CMD_SEL_REQ_CL2;
	else
		sel_cmd = DIGITAL_CMD_SEL_REQ_CL3;

	*skb_put(skb, sizeof(u8)) = sel_cmd;
	*skb_put(skb, sizeof(u8)) = DIGITAL_SDD_REQ_SEL_PAR;

	return digital_in_send_cmd(ddev, skb, 30, digital_in_recv_sdd_res,
				   target);
}

static void digital_in_recv_sens_res(struct nfc_digital_dev *ddev, void *arg,
				     struct sk_buff *resp)
{
	struct nfc_target *target = NULL;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (resp->len < sizeof(u16)) {
		rc = -EIO;
		goto exit;
	}

	target = kzalloc(sizeof(struct nfc_target), GFP_KERNEL);
	if (!target) {
		rc = -ENOMEM;
		goto exit;
	}

	target->sens_res = __le16_to_cpu(*(__le16 *)resp->data);

	if (!DIGITAL_SENS_RES_IS_VALID(target->sens_res)) {
		PROTOCOL_ERR("4.6.3.3");
		rc = -EINVAL;
		goto exit;
	}

	if (DIGITAL_SENS_RES_IS_T1T(target->sens_res))
		rc = digital_target_found(ddev, target, NFC_PROTO_JEWEL);
	else
		rc = digital_in_send_sdd_req(ddev, target);

exit:
	dev_kfree_skb(resp);

	if (rc) {
		kfree(target);
		digital_poll_next_tech(ddev);
	}
}

int digital_in_send_sens_req(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	struct sk_buff *skb;
	int rc;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH,
				     NFC_DIGITAL_RF_TECH_106A);
	if (rc)
		return rc;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCA_SHORT);
	if (rc)
		return rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, sizeof(u8)) = DIGITAL_CMD_SENS_REQ;

	rc = digital_in_send_cmd(ddev, skb, 30, digital_in_recv_sens_res, NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

int digital_in_recv_mifare_res(struct sk_buff *resp)
{
	/* Successful READ command response is 16 data bytes + 2 CRC bytes long.
	 * Since the driver can't differentiate a ACK/NACK response from a valid
	 * READ response, the CRC calculation must be handled at digital level
	 * even if the driver supports it for this technology.
	 */
	if (resp->len == DIGITAL_MIFARE_READ_RES_LEN + DIGITAL_CRC_LEN) {
		if (digital_skb_check_crc_a(resp)) {
			PROTOCOL_ERR("9.4.1.2");
			return -EIO;
		}

		return 0;
	}

	/* ACK response (i.e. successful WRITE). */
	if (resp->len == 1 && resp->data[0] == DIGITAL_MIFARE_ACK_RES) {
		resp->data[0] = 0;
		return 0;
	}

	/* NACK and any other responses are treated as error. */
	return -EIO;
}

static void digital_in_recv_attrib_res(struct nfc_digital_dev *ddev, void *arg,
				       struct sk_buff *resp)
{
	struct nfc_target *target = arg;
	struct digital_attrib_res *attrib_res;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (resp->len < sizeof(*attrib_res)) {
		PROTOCOL_ERR("12.6.2");
		rc = -EIO;
		goto exit;
	}

	attrib_res = (struct digital_attrib_res *)resp->data;

	if (attrib_res->mbli_did & 0x0f) {
		PROTOCOL_ERR("12.6.2.1");
		rc = -EIO;
		goto exit;
	}

	rc = digital_target_found(ddev, target, NFC_PROTO_ISO14443_B);

exit:
	dev_kfree_skb(resp);
	kfree(target);

	if (rc)
		digital_poll_next_tech(ddev);
}

static int digital_in_send_attrib_req(struct nfc_digital_dev *ddev,
			       struct nfc_target *target,
			       struct digital_sensb_res *sensb_res)
{
	struct digital_attrib_req *attrib_req;
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, sizeof(*attrib_req));
	if (!skb)
		return -ENOMEM;

	attrib_req = (struct digital_attrib_req *)skb_put(skb,
							  sizeof(*attrib_req));

	attrib_req->cmd = DIGITAL_CMD_ATTRIB_REQ;
	memcpy(attrib_req->nfcid0, sensb_res->nfcid0,
	       sizeof(attrib_req->nfcid0));
	attrib_req->param1 = DIGITAL_ATTRIB_P1_TR0_DEFAULT |
			     DIGITAL_ATTRIB_P1_TR1_DEFAULT;
	attrib_req->param2 = DIGITAL_ATTRIB_P2_LISTEN_POLL_1 |
			     DIGITAL_ATTRIB_P2_POLL_LISTEN_1 |
			     DIGITAL_ATTRIB_P2_MAX_FRAME_256;
	attrib_req->param3 = sensb_res->proto_info[1] & 0x07;
	attrib_req->param4 = DIGITAL_ATTRIB_P4_DID(0);

	rc = digital_in_send_cmd(ddev, skb, 30, digital_in_recv_attrib_res,
				 target);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_in_recv_sensb_res(struct nfc_digital_dev *ddev, void *arg,
				      struct sk_buff *resp)
{
	struct nfc_target *target = NULL;
	struct digital_sensb_res *sensb_res;
	u8 fsci;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (resp->len != sizeof(*sensb_res)) {
		PROTOCOL_ERR("5.6.2.1");
		rc = -EIO;
		goto exit;
	}

	sensb_res = (struct digital_sensb_res *)resp->data;

	if (sensb_res->cmd != DIGITAL_CMD_SENSB_RES) {
		PROTOCOL_ERR("5.6.2");
		rc = -EIO;
		goto exit;
	}

	if (!(sensb_res->proto_info[1] & BIT(0))) {
		PROTOCOL_ERR("5.6.2.12");
		rc = -EIO;
		goto exit;
	}

	if (sensb_res->proto_info[1] & BIT(3)) {
		PROTOCOL_ERR("5.6.2.16");
		rc = -EIO;
		goto exit;
	}

	fsci = DIGITAL_SENSB_FSCI(sensb_res->proto_info[1]);
	if (fsci >= 8)
		ddev->target_fsc = DIGITAL_ATS_MAX_FSC;
	else
		ddev->target_fsc = digital_ats_fsc[fsci];

	target = kzalloc(sizeof(struct nfc_target), GFP_KERNEL);
	if (!target) {
		rc = -ENOMEM;
		goto exit;
	}

	rc = digital_in_send_attrib_req(ddev, target, sensb_res);

exit:
	dev_kfree_skb(resp);

	if (rc) {
		kfree(target);
		digital_poll_next_tech(ddev);
	}
}

int digital_in_send_sensb_req(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	struct digital_sensb_req *sensb_req;
	struct sk_buff *skb;
	int rc;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH,
				     NFC_DIGITAL_RF_TECH_106B);
	if (rc)
		return rc;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCB);
	if (rc)
		return rc;

	skb = digital_skb_alloc(ddev, sizeof(*sensb_req));
	if (!skb)
		return -ENOMEM;

	sensb_req = (struct digital_sensb_req *)skb_put(skb,
							sizeof(*sensb_req));

	sensb_req->cmd = DIGITAL_CMD_SENSB_REQ;
	sensb_req->afi = 0x00; /* All families and sub-families */
	sensb_req->param = DIGITAL_SENSB_N(0);

	rc = digital_in_send_cmd(ddev, skb, 30, digital_in_recv_sensb_res,
				 NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_in_recv_sensf_res(struct nfc_digital_dev *ddev, void *arg,
				   struct sk_buff *resp)
{
	int rc;
	u8 proto;
	struct nfc_target target;
	struct digital_sensf_res *sensf_res;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (resp->len < DIGITAL_SENSF_RES_MIN_LENGTH) {
		rc = -EIO;
		goto exit;
	}

	if (!DIGITAL_DRV_CAPS_IN_CRC(ddev)) {
		rc = digital_skb_check_crc_f(resp);
		if (rc) {
			PROTOCOL_ERR("6.4.1.8");
			goto exit;
		}
	}

	skb_pull(resp, 1);

	memset(&target, 0, sizeof(struct nfc_target));

	sensf_res = (struct digital_sensf_res *)resp->data;

	memcpy(target.sensf_res, sensf_res, resp->len);
	target.sensf_res_len = resp->len;

	memcpy(target.nfcid2, sensf_res->nfcid2, NFC_NFCID2_MAXSIZE);
	target.nfcid2_len = NFC_NFCID2_MAXSIZE;

	if (target.nfcid2[0] == DIGITAL_SENSF_NFCID2_NFC_DEP_B1 &&
	    target.nfcid2[1] == DIGITAL_SENSF_NFCID2_NFC_DEP_B2)
		proto = NFC_PROTO_NFC_DEP;
	else
		proto = NFC_PROTO_FELICA;

	rc = digital_target_found(ddev, &target, proto);

exit:
	dev_kfree_skb(resp);

	if (rc)
		digital_poll_next_tech(ddev);
}

int digital_in_send_sensf_req(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	struct digital_sensf_req *sensf_req;
	struct sk_buff *skb;
	int rc;
	u8 size;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH, rf_tech);
	if (rc)
		return rc;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCF);
	if (rc)
		return rc;

	size = sizeof(struct digital_sensf_req);

	skb = digital_skb_alloc(ddev, size);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, size);

	sensf_req = (struct digital_sensf_req *)skb->data;
	sensf_req->cmd = DIGITAL_CMD_SENSF_REQ;
	sensf_req->sc1 = 0xFF;
	sensf_req->sc2 = 0xFF;
	sensf_req->rc = 0;
	sensf_req->tsn = 0;

	*skb_push(skb, 1) = size + 1;

	if (!DIGITAL_DRV_CAPS_IN_CRC(ddev))
		digital_skb_add_crc_f(skb);

	rc = digital_in_send_cmd(ddev, skb, 30, digital_in_recv_sensf_res,
				 NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_in_recv_iso15693_inv_res(struct nfc_digital_dev *ddev,
		void *arg, struct sk_buff *resp)
{
	struct digital_iso15693_inv_res *res;
	struct nfc_target *target = NULL;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto out_free_skb;
	}

	if (resp->len != sizeof(*res)) {
		rc = -EIO;
		goto out_free_skb;
	}

	res = (struct digital_iso15693_inv_res *)resp->data;

	if (!DIGITAL_ISO15693_RES_IS_VALID(res->flags)) {
		PROTOCOL_ERR("ISO15693 - 10.3.1");
		rc = -EINVAL;
		goto out_free_skb;
	}

	target = kzalloc(sizeof(*target), GFP_KERNEL);
	if (!target) {
		rc = -ENOMEM;
		goto out_free_skb;
	}

	target->is_iso15693 = 1;
	target->iso15693_dsfid = res->dsfid;
	memcpy(target->iso15693_uid, &res->uid, sizeof(target->iso15693_uid));

	rc = digital_target_found(ddev, target, NFC_PROTO_ISO15693);

	kfree(target);

out_free_skb:
	dev_kfree_skb(resp);

	if (rc)
		digital_poll_next_tech(ddev);
}

int digital_in_send_iso15693_inv_req(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	struct digital_iso15693_inv_req *req;
	struct sk_buff *skb;
	int rc;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH,
				     NFC_DIGITAL_RF_TECH_ISO15693);
	if (rc)
		return rc;

	rc = digital_in_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_ISO15693_INVENTORY);
	if (rc)
		return rc;

	skb = digital_skb_alloc(ddev, sizeof(*req));
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(*req) - sizeof(req->mask)); /* No mask */
	req = (struct digital_iso15693_inv_req *)skb->data;

	/* Single sub-carrier, high data rate, no AFI, single slot
	 * Inventory command
	 */
	req->flags = DIGITAL_ISO15693_REQ_FLAG_DATA_RATE |
		     DIGITAL_ISO15693_REQ_FLAG_INVENTORY |
		     DIGITAL_ISO15693_REQ_FLAG_NB_SLOTS;
	req->cmd = DIGITAL_CMD_ISO15693_INVENTORY_REQ;
	req->mask_len = 0;

	rc = digital_in_send_cmd(ddev, skb, 30,
				 digital_in_recv_iso15693_inv_res, NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static int digital_tg_send_sel_res(struct nfc_digital_dev *ddev)
{
	struct sk_buff *skb;
	int rc;

	skb = digital_skb_alloc(ddev, 1);
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, 1) = DIGITAL_SEL_RES_NFC_DEP;

	if (!DIGITAL_DRV_CAPS_TG_CRC(ddev))
		digital_skb_add_crc_a(skb);

	rc = digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCA_ANTICOL_COMPLETE);
	if (rc) {
		kfree_skb(skb);
		return rc;
	}

	rc = digital_tg_send_cmd(ddev, skb, 300, digital_tg_recv_atr_req,
				 NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_tg_recv_sel_req(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (!DIGITAL_DRV_CAPS_TG_CRC(ddev)) {
		rc = digital_skb_check_crc_a(resp);
		if (rc) {
			PROTOCOL_ERR("4.4.1.3");
			goto exit;
		}
	}

	/* Silently ignore SEL_REQ content and send a SEL_RES for NFC-DEP */

	rc = digital_tg_send_sel_res(ddev);

exit:
	if (rc)
		digital_poll_next_tech(ddev);

	dev_kfree_skb(resp);
}

static int digital_tg_send_sdd_res(struct nfc_digital_dev *ddev)
{
	struct sk_buff *skb;
	struct digital_sdd_res *sdd_res;
	int rc, i;

	skb = digital_skb_alloc(ddev, sizeof(struct digital_sdd_res));
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(struct digital_sdd_res));
	sdd_res = (struct digital_sdd_res *)skb->data;

	sdd_res->nfcid1[0] = 0x08;
	get_random_bytes(sdd_res->nfcid1 + 1, 3);

	sdd_res->bcc = 0;
	for (i = 0; i < 4; i++)
		sdd_res->bcc ^= sdd_res->nfcid1[i];

	rc = digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				NFC_DIGITAL_FRAMING_NFCA_STANDARD_WITH_CRC_A);
	if (rc) {
		kfree_skb(skb);
		return rc;
	}

	rc = digital_tg_send_cmd(ddev, skb, 300, digital_tg_recv_sel_req,
				 NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

static void digital_tg_recv_sdd_req(struct nfc_digital_dev *ddev, void *arg,
				    struct sk_buff *resp)
{
	u8 *sdd_req;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	sdd_req = resp->data;

	if (resp->len < 2 || sdd_req[0] != DIGITAL_CMD_SEL_REQ_CL1 ||
	    sdd_req[1] != DIGITAL_SDD_REQ_SEL_PAR) {
		rc = -EINVAL;
		goto exit;
	}

	rc = digital_tg_send_sdd_res(ddev);

exit:
	if (rc)
		digital_poll_next_tech(ddev);

	dev_kfree_skb(resp);
}

static int digital_tg_send_sens_res(struct nfc_digital_dev *ddev)
{
	struct sk_buff *skb;
	u8 *sens_res;
	int rc;

	skb = digital_skb_alloc(ddev, 2);
	if (!skb)
		return -ENOMEM;

	sens_res = skb_put(skb, 2);

	sens_res[0] = (DIGITAL_SENS_RES_NFC_DEP >> 8) & 0xFF;
	sens_res[1] = DIGITAL_SENS_RES_NFC_DEP & 0xFF;

	rc = digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCA_STANDARD);
	if (rc) {
		kfree_skb(skb);
		return rc;
	}

	rc = digital_tg_send_cmd(ddev, skb, 300, digital_tg_recv_sdd_req,
				 NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

void digital_tg_recv_sens_req(struct nfc_digital_dev *ddev, void *arg,
			      struct sk_buff *resp)
{
	u8 sens_req;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	sens_req = resp->data[0];

	if (!resp->len || (sens_req != DIGITAL_CMD_SENS_REQ &&
	    sens_req != DIGITAL_CMD_ALL_REQ)) {
		rc = -EINVAL;
		goto exit;
	}

	rc = digital_tg_send_sens_res(ddev);

exit:
	if (rc)
		digital_poll_next_tech(ddev);

	dev_kfree_skb(resp);
}

static void digital_tg_recv_atr_or_sensf_req(struct nfc_digital_dev *ddev,
		void *arg, struct sk_buff *resp)
{
	if (!IS_ERR(resp) && (resp->len >= 2) &&
			(resp->data[1] == DIGITAL_CMD_SENSF_REQ))
		digital_tg_recv_sensf_req(ddev, arg, resp);
	else
		digital_tg_recv_atr_req(ddev, arg, resp);

	return;
}

static int digital_tg_send_sensf_res(struct nfc_digital_dev *ddev,
			      struct digital_sensf_req *sensf_req)
{
	struct sk_buff *skb;
	u8 size;
	int rc;
	struct digital_sensf_res *sensf_res;

	size = sizeof(struct digital_sensf_res);

	if (sensf_req->rc == DIGITAL_SENSF_REQ_RC_NONE)
		size -= sizeof(sensf_res->rd);

	skb = digital_skb_alloc(ddev, size);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, size);

	sensf_res = (struct digital_sensf_res *)skb->data;

	memset(sensf_res, 0, size);

	sensf_res->cmd = DIGITAL_CMD_SENSF_RES;
	sensf_res->nfcid2[0] = DIGITAL_SENSF_NFCID2_NFC_DEP_B1;
	sensf_res->nfcid2[1] = DIGITAL_SENSF_NFCID2_NFC_DEP_B2;
	get_random_bytes(&sensf_res->nfcid2[2], 6);

	switch (sensf_req->rc) {
	case DIGITAL_SENSF_REQ_RC_SC:
		sensf_res->rd[0] = sensf_req->sc1;
		sensf_res->rd[1] = sensf_req->sc2;
		break;
	case DIGITAL_SENSF_REQ_RC_AP:
		sensf_res->rd[0] = DIGITAL_SENSF_RES_RD_AP_B1;
		sensf_res->rd[1] = DIGITAL_SENSF_RES_RD_AP_B2;
		break;
	}

	*skb_push(skb, sizeof(u8)) = size + 1;

	if (!DIGITAL_DRV_CAPS_TG_CRC(ddev))
		digital_skb_add_crc_f(skb);

	rc = digital_tg_send_cmd(ddev, skb, 300,
				 digital_tg_recv_atr_or_sensf_req, NULL);
	if (rc)
		kfree_skb(skb);

	return rc;
}

void digital_tg_recv_sensf_req(struct nfc_digital_dev *ddev, void *arg,
			       struct sk_buff *resp)
{
	struct digital_sensf_req *sensf_req;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		resp = NULL;
		goto exit;
	}

	if (!DIGITAL_DRV_CAPS_TG_CRC(ddev)) {
		rc = digital_skb_check_crc_f(resp);
		if (rc) {
			PROTOCOL_ERR("6.4.1.8");
			goto exit;
		}
	}

	if (resp->len != sizeof(struct digital_sensf_req) + 1) {
		rc = -EINVAL;
		goto exit;
	}

	skb_pull(resp, 1);
	sensf_req = (struct digital_sensf_req *)resp->data;

	if (sensf_req->cmd != DIGITAL_CMD_SENSF_REQ) {
		rc = -EINVAL;
		goto exit;
	}

	rc = digital_tg_send_sensf_res(ddev, sensf_req);

exit:
	if (rc)
		digital_poll_next_tech(ddev);

	dev_kfree_skb(resp);
}

static int digital_tg_config_nfca(struct nfc_digital_dev *ddev)
{
	int rc;

	rc = digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH,
				     NFC_DIGITAL_RF_TECH_106A);
	if (rc)
		return rc;

	return digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				       NFC_DIGITAL_FRAMING_NFCA_NFC_DEP);
}

int digital_tg_listen_nfca(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	int rc;

	rc = digital_tg_config_nfca(ddev);
	if (rc)
		return rc;

	return digital_tg_listen(ddev, 300, digital_tg_recv_sens_req, NULL);
}

static int digital_tg_config_nfcf(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	int rc;

	rc = digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH, rf_tech);
	if (rc)
		return rc;

	return digital_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				       NFC_DIGITAL_FRAMING_NFCF_NFC_DEP);
}

int digital_tg_listen_nfcf(struct nfc_digital_dev *ddev, u8 rf_tech)
{
	int rc;
	u8 *nfcid2;

	rc = digital_tg_config_nfcf(ddev, rf_tech);
	if (rc)
		return rc;

	nfcid2 = kzalloc(NFC_NFCID2_MAXSIZE, GFP_KERNEL);
	if (!nfcid2)
		return -ENOMEM;

	nfcid2[0] = DIGITAL_SENSF_NFCID2_NFC_DEP_B1;
	nfcid2[1] = DIGITAL_SENSF_NFCID2_NFC_DEP_B2;
	get_random_bytes(nfcid2 + 2, NFC_NFCID2_MAXSIZE - 2);

	return digital_tg_listen(ddev, 300, digital_tg_recv_sensf_req, nfcid2);
}

void digital_tg_recv_md_req(struct nfc_digital_dev *ddev, void *arg,
			    struct sk_buff *resp)
{
	u8 rf_tech;
	int rc;

	if (IS_ERR(resp)) {
		resp = NULL;
		goto exit_free_skb;
	}

	rc = ddev->ops->tg_get_rf_tech(ddev, &rf_tech);
	if (rc)
		goto exit_free_skb;

	switch (rf_tech) {
	case NFC_DIGITAL_RF_TECH_106A:
		rc = digital_tg_config_nfca(ddev);
		if (rc)
			goto exit_free_skb;
		digital_tg_recv_sens_req(ddev, arg, resp);
		break;
	case NFC_DIGITAL_RF_TECH_212F:
	case NFC_DIGITAL_RF_TECH_424F:
		rc = digital_tg_config_nfcf(ddev, rf_tech);
		if (rc)
			goto exit_free_skb;
		digital_tg_recv_sensf_req(ddev, arg, resp);
		break;
	default:
		goto exit_free_skb;
	}

	return;

exit_free_skb:
	digital_poll_next_tech(ddev);
	dev_kfree_skb(resp);
}
