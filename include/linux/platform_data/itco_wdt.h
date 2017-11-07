/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Platform data for the Intel TCO Watchdog
 */

#ifndef _ITCO_WDT_H_
#define _ITCO_WDT_H_

/* Watchdog resources */
#define ICH_RES_IO_TCO		0
#define ICH_RES_IO_SMI		1
#define ICH_RES_MEM_OFF		2
#define ICH_RES_MEM_GCS_PMC	0

struct itco_wdt_platform_data {
	char name[32];
	unsigned int version;
	/* private data to be passed to update_no_reboot_bit API */
	void *no_reboot_priv;
	/* pointer for platform specific no reboot update function */
	int (*update_no_reboot_bit)(void *priv, bool set);
};

#endif /* _ITCO_WDT_H_ */
