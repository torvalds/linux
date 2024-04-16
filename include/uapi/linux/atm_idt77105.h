/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* atm_idt77105.h - Driver-specific declarations of the IDT77105 driver (for
 * use by driver-specific utilities) */

/* Written 1999 by Greg Banks <gnb@linuxfan.com>. Copied from atm_suni.h. */


#ifndef LINUX_ATM_IDT77105_H
#define LINUX_ATM_IDT77105_H

#include <linux/types.h>
#include <linux/atmioc.h>
#include <linux/atmdev.h>

/*
 * Structure for IDT77105_GETSTAT and IDT77105_GETSTATZ ioctls.
 * Pointed to by `arg' in atmif_sioc.
 */
struct idt77105_stats {
        __u32 symbol_errors;  /* wire symbol errors */
        __u32 tx_cells;       /* cells transmitted */
        __u32 rx_cells;       /* cells received */
        __u32 rx_hec_errors;  /* Header Error Check errors on receive */
};

#define IDT77105_GETSTAT	_IOW('a',ATMIOC_PHYPRV+2,struct atmif_sioc)	/* get stats */
#define IDT77105_GETSTATZ	_IOW('a',ATMIOC_PHYPRV+3,struct atmif_sioc)	/* get stats and zero */

#endif
