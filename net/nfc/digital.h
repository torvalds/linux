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

#ifndef __DIGITAL_H
#define __DIGITAL_H

#include <net/nfc/nfc.h>
#include <net/nfc/digital.h>

#define PR_DBG(fmt, ...)  pr_debug("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#define PR_ERR(fmt, ...)  pr_err("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#define PROTOCOL_ERR(req) pr_err("%s:%d: NFC Digital Protocol error: %s\n", \
				 __func__, __LINE__, req)

#define DIGITAL_CMD_IN_SEND        0
#define DIGITAL_CMD_TG_SEND        1
#define DIGITAL_CMD_TG_LISTEN      2
#define DIGITAL_CMD_TG_LISTEN_MDAA 3

#define DIGITAL_MAX_HEADER_LEN 7
#define DIGITAL_CRC_LEN        2

struct sk_buff *digital_skb_alloc(struct nfc_digital_dev *ddev,
				  unsigned int len);

int digital_send_cmd(struct nfc_digital_dev *ddev, u8 cmd_type,
		     struct sk_buff *skb, u16 timeout,
		     nfc_digital_cmd_complete_t cmd_cb, void *cb_context);

int digital_in_configure_hw(struct nfc_digital_dev *ddev, int type, int param);
static inline int digital_in_send_cmd(struct nfc_digital_dev *ddev,
				      struct sk_buff *skb, u16 timeout,
				      nfc_digital_cmd_complete_t cmd_cb,
				      void *cb_context)
{
	return digital_send_cmd(ddev, DIGITAL_CMD_IN_SEND, skb, timeout, cmd_cb,
				cb_context);
}

void digital_poll_next_tech(struct nfc_digital_dev *ddev);

int digital_in_send_sens_req(struct nfc_digital_dev *ddev, u8 rf_tech);

#endif /* __DIGITAL_H */
