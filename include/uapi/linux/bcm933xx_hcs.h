/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Broadcom Cable Modem firmware format
 */

#ifndef __BCM933XX_HCS_H
#define __BCM933XX_HCS_H

#include <linux/types.h>

struct bcm_hcs {
	__u16 magic;
	__u16 control;
	__u16 rev_maj;
	__u16 rev_min;
	__u32 build_date;
	__u32 filelen;
	__u32 ldaddress;
	char filename[64];
	__u16 hcs;
	__u16 her_znaet_chto;
	__u32 crc;
};

#endif /* __BCM933XX_HCS */
