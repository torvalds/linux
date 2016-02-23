/*
 * mailbox-omap.h
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _PLAT_MAILBOX_H
#define _PLAT_MAILBOX_H

/* Interrupt register configuration types */
#define MBOX_INTR_CFG_TYPE1	(0)
#define MBOX_INTR_CFG_TYPE2	(1)

/**
 * struct omap_mbox_dev_info - OMAP mailbox device attribute info
 * @name:	name of the mailbox device
 * @tx_id:	mailbox queue id used for transmitting messages
 * @rx_id:	mailbox queue id on which messages are received
 * @irq_id:	irq identifier number to use from the hwmod data
 * @usr_id:	mailbox user id for identifying the interrupt into
 *			the MPU interrupt controller.
 */
struct omap_mbox_dev_info {
	const char *name;
	u32 tx_id;
	u32 rx_id;
	u32 irq_id;
	u32 usr_id;
};

/**
 * struct omap_mbox_pdata - OMAP mailbox platform data
 * @intr_type:	type of interrupt configuration registers used
			while programming mailbox queue interrupts
 * @num_users:	number of users (processor devices) that the mailbox
 *			h/w block can interrupt
 * @num_fifos:	number of h/w fifos within the mailbox h/w block
 * @info_cnt:	number of mailbox devices for the platform
 * @info:	array of mailbox device attributes
 */
struct omap_mbox_pdata {
	u32 intr_type;
	u32 num_users;
	u32 num_fifos;
	u32 info_cnt;
	struct omap_mbox_dev_info *info;
};

#endif /* _PLAT_MAILBOX_H */
