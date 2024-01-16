/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NFC Digital Protocol stack
 * Copyright (c) 2013, Intel Corporation.
 */

#ifndef __DIGITAL_H
#define __DIGITAL_H

#include <net/nfc/nfc.h>
#include <net/nfc/digital.h>

#include <linux/crc-ccitt.h>
#include <linux/crc-itu-t.h>

#define PROTOCOL_ERR(req) pr_err("%d: NFC Digital Protocol error: %s\n", \
				 __LINE__, req)

#define DIGITAL_CMD_IN_SEND        0
#define DIGITAL_CMD_TG_SEND        1
#define DIGITAL_CMD_TG_LISTEN      2
#define DIGITAL_CMD_TG_LISTEN_MDAA 3
#define DIGITAL_CMD_TG_LISTEN_MD   4

#define DIGITAL_MAX_HEADER_LEN 7
#define DIGITAL_CRC_LEN        2

#define DIGITAL_SENSF_NFCID2_NFC_DEP_B1 0x01
#define DIGITAL_SENSF_NFCID2_NFC_DEP_B2 0xFE

#define DIGITAL_SENS_RES_NFC_DEP 0x0100
#define DIGITAL_SEL_RES_NFC_DEP  0x40
#define DIGITAL_SENSF_FELICA_SC  0xFFFF

#define DIGITAL_DRV_CAPS_IN_CRC(ddev) \
	((ddev)->driver_capabilities & NFC_DIGITAL_DRV_CAPS_IN_CRC)
#define DIGITAL_DRV_CAPS_TG_CRC(ddev) \
	((ddev)->driver_capabilities & NFC_DIGITAL_DRV_CAPS_TG_CRC)

struct digital_data_exch {
	data_exchange_cb_t cb;
	void *cb_context;
};

struct sk_buff *digital_skb_alloc(struct nfc_digital_dev *ddev,
				  unsigned int len);

int digital_send_cmd(struct nfc_digital_dev *ddev, u8 cmd_type,
		     struct sk_buff *skb, struct digital_tg_mdaa_params *params,
		     u16 timeout, nfc_digital_cmd_complete_t cmd_cb,
		     void *cb_context);

int digital_in_configure_hw(struct nfc_digital_dev *ddev, int type, int param);
static inline int digital_in_send_cmd(struct nfc_digital_dev *ddev,
				      struct sk_buff *skb, u16 timeout,
				      nfc_digital_cmd_complete_t cmd_cb,
				      void *cb_context)
{
	return digital_send_cmd(ddev, DIGITAL_CMD_IN_SEND, skb, NULL, timeout,
				cmd_cb, cb_context);
}

void digital_poll_next_tech(struct nfc_digital_dev *ddev);

int digital_in_send_sens_req(struct nfc_digital_dev *ddev, u8 rf_tech);
int digital_in_send_sensb_req(struct nfc_digital_dev *ddev, u8 rf_tech);
int digital_in_send_sensf_req(struct nfc_digital_dev *ddev, u8 rf_tech);
int digital_in_send_iso15693_inv_req(struct nfc_digital_dev *ddev, u8 rf_tech);

int digital_in_iso_dep_pull_sod(struct nfc_digital_dev *ddev,
				struct sk_buff *skb);
int digital_in_iso_dep_push_sod(struct nfc_digital_dev *ddev,
				struct sk_buff *skb);

int digital_target_found(struct nfc_digital_dev *ddev,
			 struct nfc_target *target, u8 protocol);

int digital_in_recv_mifare_res(struct sk_buff *resp);

int digital_in_send_atr_req(struct nfc_digital_dev *ddev,
			    struct nfc_target *target, __u8 comm_mode, __u8 *gb,
			    size_t gb_len);
int digital_in_send_dep_req(struct nfc_digital_dev *ddev,
			    struct nfc_target *target, struct sk_buff *skb,
			    struct digital_data_exch *data_exch);

int digital_tg_configure_hw(struct nfc_digital_dev *ddev, int type, int param);
static inline int digital_tg_send_cmd(struct nfc_digital_dev *ddev,
			struct sk_buff *skb, u16 timeout,
			nfc_digital_cmd_complete_t cmd_cb, void *cb_context)
{
	return digital_send_cmd(ddev, DIGITAL_CMD_TG_SEND, skb, NULL, timeout,
				cmd_cb, cb_context);
}

void digital_tg_recv_sens_req(struct nfc_digital_dev *ddev, void *arg,
			      struct sk_buff *resp);

void digital_tg_recv_sensf_req(struct nfc_digital_dev *ddev, void *arg,
			       struct sk_buff *resp);

static inline int digital_tg_listen(struct nfc_digital_dev *ddev, u16 timeout,
				    nfc_digital_cmd_complete_t cb, void *arg)
{
	return digital_send_cmd(ddev, DIGITAL_CMD_TG_LISTEN, NULL, NULL,
				timeout, cb, arg);
}

void digital_tg_recv_atr_req(struct nfc_digital_dev *ddev, void *arg,
			     struct sk_buff *resp);

int digital_tg_send_dep_res(struct nfc_digital_dev *ddev, struct sk_buff *skb);

int digital_tg_listen_nfca(struct nfc_digital_dev *ddev, u8 rf_tech);
int digital_tg_listen_nfcf(struct nfc_digital_dev *ddev, u8 rf_tech);
void digital_tg_recv_md_req(struct nfc_digital_dev *ddev, void *arg,
			    struct sk_buff *resp);

typedef u16 (*crc_func_t)(u16, const u8 *, size_t);

#define CRC_A_INIT 0x6363
#define CRC_B_INIT 0xFFFF
#define CRC_F_INIT 0x0000

void digital_skb_add_crc(struct sk_buff *skb, crc_func_t crc_func, u16 init,
			 u8 bitwise_inv, u8 msb_first);

static inline void digital_skb_add_crc_a(struct sk_buff *skb)
{
	digital_skb_add_crc(skb, crc_ccitt, CRC_A_INIT, 0, 0);
}

static inline void digital_skb_add_crc_b(struct sk_buff *skb)
{
	digital_skb_add_crc(skb, crc_ccitt, CRC_B_INIT, 1, 0);
}

static inline void digital_skb_add_crc_f(struct sk_buff *skb)
{
	digital_skb_add_crc(skb, crc_itu_t, CRC_F_INIT, 0, 1);
}

static inline void digital_skb_add_crc_none(struct sk_buff *skb)
{
	return;
}

int digital_skb_check_crc(struct sk_buff *skb, crc_func_t crc_func,
			  u16 crc_init, u8 bitwise_inv, u8 msb_first);

static inline int digital_skb_check_crc_a(struct sk_buff *skb)
{
	return digital_skb_check_crc(skb, crc_ccitt, CRC_A_INIT, 0, 0);
}

static inline int digital_skb_check_crc_b(struct sk_buff *skb)
{
	return digital_skb_check_crc(skb, crc_ccitt, CRC_B_INIT, 1, 0);
}

static inline int digital_skb_check_crc_f(struct sk_buff *skb)
{
	return digital_skb_check_crc(skb, crc_itu_t, CRC_F_INIT, 0, 1);
}

static inline int digital_skb_check_crc_none(struct sk_buff *skb)
{
	return 0;
}

#endif /* __DIGITAL_H */
