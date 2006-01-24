/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2003-2005 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_CE_PROVIDER_H
#define _ASM_IA64_SN_CE_PROVIDER_H

#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/tioce.h>

/*
 * Common TIOCE structure shared between the prom and kernel
 *
 * DO NOT CHANGE THIS STRUCT WITHOUT MAKING CORRESPONDING CHANGES TO THE
 * PROM VERSION.
 */
struct tioce_common {
	struct pcibus_bussoft	ce_pcibus;	/* common pciio header */

	u32		ce_rev;
	u64		ce_kernel_private;
	u64		ce_prom_private;
};

struct tioce_kernel {
	struct tioce_common	*ce_common;
	spinlock_t		ce_lock;
	struct list_head	ce_dmamap_list;

	u64		ce_ate40_shadow[TIOCE_NUM_M40_ATES];
	u64		ce_ate3240_shadow[TIOCE_NUM_M3240_ATES];
	u32		ce_ate3240_pagesize;

	u8			ce_port1_secondary;

	/* per-port resources */
	struct {
		int 		dirmap_refcnt;
		u64	dirmap_shadow;
	} ce_port[TIOCE_NUM_PORTS];
};

struct tioce_dmamap {
	struct list_head	ce_dmamap_list;	/* headed by tioce_kernel */
	u32		refcnt;

	u64		nbytes;		/* # bytes mapped */

	u64		ct_start;	/* coretalk start address */
	u64		pci_start;	/* bus start address */

	u64		*ate_hw;	/* hw ptr of first ate in map */
	u64		*ate_shadow;	/* shadow ptr of firat ate */
	u16		ate_count;	/* # ate's in the map */
};

extern int tioce_init_provider(void);

#endif  /* __ASM_IA64_SN_CE_PROVIDER_H */
