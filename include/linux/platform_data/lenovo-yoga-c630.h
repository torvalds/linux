// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Linaro Ltd
 * Authors:
 *    Bjorn Andersson
 *    Dmitry Baryshkov
 */

#ifndef _LENOVO_YOGA_C630_DATA_H
#define _LENOVO_YOGA_C630_DATA_H

struct yoga_c630_ec;
struct notifier_block;

#define YOGA_C630_MOD_NAME	"lenovo_yoga_c630"

#define YOGA_C630_DEV_UCSI	"ucsi"
#define YOGA_C630_DEV_PSY	"psy"

int yoga_c630_ec_read8(struct yoga_c630_ec *ec, u8 addr);
int yoga_c630_ec_read16(struct yoga_c630_ec *ec, u8 addr);

int yoga_c630_ec_register_notify(struct yoga_c630_ec *ec, struct notifier_block *nb);
void yoga_c630_ec_unregister_notify(struct yoga_c630_ec *ec, struct notifier_block *nb);

#define YOGA_C630_UCSI_WRITE_SIZE	8
#define YOGA_C630_UCSI_CCI_SIZE		4
#define YOGA_C630_UCSI_DATA_SIZE	16
#define YOGA_C630_UCSI_READ_SIZE	(YOGA_C630_UCSI_CCI_SIZE + YOGA_C630_UCSI_DATA_SIZE)

u16 yoga_c630_ec_ucsi_get_version(struct yoga_c630_ec *ec);
int yoga_c630_ec_ucsi_write(struct yoga_c630_ec *ec,
			    const u8 req[YOGA_C630_UCSI_WRITE_SIZE]);
int yoga_c630_ec_ucsi_read(struct yoga_c630_ec *ec,
			   u8 resp[YOGA_C630_UCSI_READ_SIZE]);

#define LENOVO_EC_EVENT_USB		0x20
#define LENOVO_EC_EVENT_UCSI		0x21
#define LENOVO_EC_EVENT_HPD		0x22
#define LENOVO_EC_EVENT_BAT_STATUS	0x24
#define LENOVO_EC_EVENT_BAT_INFO	0x25
#define LENOVO_EC_EVENT_BAT_ADPT_STATUS	0x37

#endif
