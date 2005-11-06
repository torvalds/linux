/**************************************************************************
 *             Copyright (C) 2005, Silicon Graphics, Inc.                 *
 *									  *
 *  These coded instructions, statements, and computer programs	 contain  *
 *  unpublished	 proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may	not be disclosed  *
 *  to	third  parties	or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/

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

	uint32_t		ce_rev;
	uint64_t		ce_kernel_private;
	uint64_t		ce_prom_private;
};

struct tioce_kernel {
	struct tioce_common	*ce_common;
	spinlock_t		ce_lock;
	struct list_head	ce_dmamap_list;

	uint64_t		ce_ate40_shadow[TIOCE_NUM_M40_ATES];
	uint64_t		ce_ate3240_shadow[TIOCE_NUM_M3240_ATES];
	uint32_t		ce_ate3240_pagesize;

	uint8_t			ce_port1_secondary;

	/* per-port resources */
	struct {
		int 		dirmap_refcnt;
		uint64_t	dirmap_shadow;
	} ce_port[TIOCE_NUM_PORTS];
};

struct tioce_dmamap {
	struct list_head	ce_dmamap_list;	/* headed by tioce_kernel */
	uint32_t		refcnt;

	uint64_t		nbytes;		/* # bytes mapped */

	uint64_t		ct_start;	/* coretalk start address */
	uint64_t		pci_start;	/* bus start address */

	uint64_t		*ate_hw;	/* hw ptr of first ate in map */
	uint64_t		*ate_shadow;	/* shadow ptr of firat ate */
	uint16_t		ate_count;	/* # ate's in the map */
};

extern int tioce_init_provider(void);

#endif  /* __ASM_IA64_SN_CE_PROVIDER_H */
