/*
 *  include/asm-ppc64/firmware.h
 *
 *  Extracted from include/asm-ppc64/cputable.h
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_PPC_FIRMWARE_H
#define __ASM_PPC_FIRMWARE_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/* firmware feature bitmask values */
#define FIRMWARE_MAX_FEATURES 63

#define FW_FEATURE_PFT		(1UL<<0)
#define FW_FEATURE_TCE		(1UL<<1)
#define FW_FEATURE_SPRG0	(1UL<<2)
#define FW_FEATURE_DABR		(1UL<<3)
#define FW_FEATURE_COPY		(1UL<<4)
#define FW_FEATURE_ASR		(1UL<<5)
#define FW_FEATURE_DEBUG	(1UL<<6)
#define FW_FEATURE_TERM		(1UL<<7)
#define FW_FEATURE_PERF		(1UL<<8)
#define FW_FEATURE_DUMP		(1UL<<9)
#define FW_FEATURE_INTERRUPT	(1UL<<10)
#define FW_FEATURE_MIGRATE	(1UL<<11)
#define FW_FEATURE_PERFMON	(1UL<<12)
#define FW_FEATURE_CRQ		(1UL<<13)
#define FW_FEATURE_VIO		(1UL<<14)
#define FW_FEATURE_RDMA		(1UL<<15)
#define FW_FEATURE_LLAN		(1UL<<16)
#define FW_FEATURE_BULK		(1UL<<17)
#define FW_FEATURE_XDABR	(1UL<<18)
#define FW_FEATURE_MULTITCE	(1UL<<19)
#define FW_FEATURE_SPLPAR	(1UL<<20)

/* This is used to identify firmware features which are available
 * to the kernel.
 */
extern unsigned long	ppc64_firmware_features;

static inline unsigned long firmware_has_feature(unsigned long feature)
{
	return ppc64_firmware_features & feature;
}

typedef struct {
    unsigned long val;
    char * name;
} firmware_feature_t;

extern firmware_feature_t firmware_features_table[];

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* __ASM_PPC_FIRMWARE_H */
