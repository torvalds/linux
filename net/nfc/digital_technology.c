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

#include "digital.h"

#define DIGITAL_CMD_SENS_REQ    0x26
#define DIGITAL_CMD_ALL_REQ     0x52
#define DIGITAL_CMD_SEL_REQ_CL1 0x93
#define DIGITAL_CMD_SEL_REQ_CL2 0x95
#define DIGITAL_CMD_SEL_REQ_CL3 0x97

#define DIGITAL_SDD_REQ_SEL_PAR 0x20

#define DIGITAL_SDD_RES_CT  0x88
#define DIGITAL_SDD_RES_LEN 5

static void digital_in_recv_sens_res(struct nfc_digital_dev *ddev, void *arg,
				     struct sk_buff *resp)
{
	if (!IS_ERR(resp))
		dev_kfree_skb(resp);

	digital_poll_next_tech(ddev);
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
