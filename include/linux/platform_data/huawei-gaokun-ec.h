// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei Matebook E Go Embedded Controller
 *
 * Copyright (C) 2024-2025 Pengyu Luo <mitltlatltl@gmail.com>
 */

#ifndef __HUAWEI_GAOKUN_EC_H__
#define __HUAWEI_GAOKUN_EC_H__

#define GAOKUN_UCSI_CCI_SIZE	4
#define GAOKUN_UCSI_MSGI_SIZE	16
#define GAOKUN_UCSI_READ_SIZE	(GAOKUN_UCSI_CCI_SIZE + GAOKUN_UCSI_MSGI_SIZE)
#define GAOKUN_UCSI_WRITE_SIZE	24 /* 8B CTRL, 16B MSGO */

#define GAOKUN_UCSI_NO_PORT_UPDATE	(-1)

#define GAOKUN_SMART_CHARGE_DATA_SIZE	4 /* mode, delay, start, end */

/* -------------------------------------------------------------------------- */

struct gaokun_ec;
struct gaokun_ucsi_reg;
struct notifier_block;

#define GAOKUN_MOD_NAME			"huawei_gaokun_ec"
#define GAOKUN_DEV_PSY			"psy"
#define GAOKUN_DEV_UCSI			"ucsi"

/* -------------------------------------------------------------------------- */
/* Common API */

int gaokun_ec_register_notify(struct gaokun_ec *ec,
			      struct notifier_block *nb);
void gaokun_ec_unregister_notify(struct gaokun_ec *ec,
				 struct notifier_block *nb);

int gaokun_ec_read(struct gaokun_ec *ec, const u8 *req,
		   size_t resp_len, u8 *resp);
int gaokun_ec_write(struct gaokun_ec *ec, const u8 *req);
int gaokun_ec_read_byte(struct gaokun_ec *ec, const u8 *req, u8 *byte);

/* -------------------------------------------------------------------------- */
/* API for PSY */

int gaokun_ec_psy_multi_read(struct gaokun_ec *ec, u8 reg,
			     size_t resp_len, u8 *resp);

static inline int gaokun_ec_psy_read_byte(struct gaokun_ec *ec,
					  u8 reg, u8 *byte)
{
	return gaokun_ec_psy_multi_read(ec, reg, sizeof(*byte), byte);
}

static inline int gaokun_ec_psy_read_word(struct gaokun_ec *ec,
					  u8 reg, u16 *word)
{
	return gaokun_ec_psy_multi_read(ec, reg, sizeof(*word), (u8 *)word);
}

int gaokun_ec_psy_get_smart_charge(struct gaokun_ec *ec,
				   u8 resp[GAOKUN_SMART_CHARGE_DATA_SIZE]);
int gaokun_ec_psy_set_smart_charge(struct gaokun_ec *ec,
				   const u8 req[GAOKUN_SMART_CHARGE_DATA_SIZE]);

int gaokun_ec_psy_get_smart_charge_enable(struct gaokun_ec *ec, bool *on);
int gaokun_ec_psy_set_smart_charge_enable(struct gaokun_ec *ec, bool on);

/* -------------------------------------------------------------------------- */
/* API for UCSI */

int gaokun_ec_ucsi_read(struct gaokun_ec *ec, u8 resp[GAOKUN_UCSI_READ_SIZE]);
int gaokun_ec_ucsi_write(struct gaokun_ec *ec,
			 const u8 req[GAOKUN_UCSI_WRITE_SIZE]);

int gaokun_ec_ucsi_get_reg(struct gaokun_ec *ec, struct gaokun_ucsi_reg *ureg);
int gaokun_ec_ucsi_pan_ack(struct gaokun_ec *ec, int port_id);

#endif /* __HUAWEI_GAOKUN_EC_H__ */
