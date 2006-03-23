/*
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
#ifndef __ASM_POWERPC_FIRMWARE_H
#define __ASM_POWERPC_FIRMWARE_H

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
#define FW_FEATURE_ISERIES	(1UL<<21)
#define FW_FEATURE_LPAR		(1UL<<22)

enum {
#ifdef CONFIG_PPC64
	FW_FEATURE_PSERIES_POSSIBLE = FW_FEATURE_PFT | FW_FEATURE_TCE |
		FW_FEATURE_SPRG0 | FW_FEATURE_DABR | FW_FEATURE_COPY |
		FW_FEATURE_ASR | FW_FEATURE_DEBUG | FW_FEATURE_TERM |
		FW_FEATURE_PERF | FW_FEATURE_DUMP | FW_FEATURE_INTERRUPT |
		FW_FEATURE_MIGRATE | FW_FEATURE_PERFMON | FW_FEATURE_CRQ |
		FW_FEATURE_VIO | FW_FEATURE_RDMA | FW_FEATURE_LLAN |
		FW_FEATURE_BULK | FW_FEATURE_XDABR | FW_FEATURE_MULTITCE |
		FW_FEATURE_SPLPAR | FW_FEATURE_LPAR,
	FW_FEATURE_PSERIES_ALWAYS = 0,
	FW_FEATURE_ISERIES_POSSIBLE = FW_FEATURE_ISERIES | FW_FEATURE_LPAR,
	FW_FEATURE_ISERIES_ALWAYS = FW_FEATURE_ISERIES | FW_FEATURE_LPAR,
	FW_FEATURE_POSSIBLE =
#ifdef CONFIG_PPC_PSERIES
		FW_FEATURE_PSERIES_POSSIBLE |
#endif
#ifdef CONFIG_PPC_ISERIES
		FW_FEATURE_ISERIES_POSSIBLE |
#endif
		0,
	FW_FEATURE_ALWAYS =
#ifdef CONFIG_PPC_PSERIES
		FW_FEATURE_PSERIES_ALWAYS &
#endif
#ifdef CONFIG_PPC_ISERIES
		FW_FEATURE_ISERIES_ALWAYS &
#endif
		FW_FEATURE_POSSIBLE,

#else /* CONFIG_PPC64 */
	FW_FEATURE_POSSIBLE = 0,
	FW_FEATURE_ALWAYS = 0,
#endif
};

/* This is used to identify firmware features which are available
 * to the kernel.
 */
extern unsigned long	ppc64_firmware_features;

static inline unsigned long firmware_has_feature(unsigned long feature)
{
	return (FW_FEATURE_ALWAYS & feature) ||
		(FW_FEATURE_POSSIBLE & ppc64_firmware_features & feature);
}

extern void system_reset_fwnmi(void);
extern void machine_check_fwnmi(void);

/* This is true if we are using the firmware NMI handler (typically LPAR) */
extern int fwnmi_active;

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_FIRMWARE_H */
