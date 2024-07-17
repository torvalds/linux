/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * POWER Dynamic Execution Control Facility (DEXCR)
 *
 * This header file contains helper functions and macros
 * required for all the DEXCR related test cases.
 */
#ifndef _SELFTESTS_POWERPC_DEXCR_DEXCR_H
#define _SELFTESTS_POWERPC_DEXCR_DEXCR_H

#include <stdbool.h>
#include <sys/prctl.h>
#include <sys/types.h>

#include "reg.h"

#define DEXCR_PR_BIT(aspect)	__MASK(63 - (32 + (aspect)))
#define DEXCR_PR_SBHE		DEXCR_PR_BIT(0)
#define DEXCR_PR_IBRTPD		DEXCR_PR_BIT(3)
#define DEXCR_PR_SRAPD		DEXCR_PR_BIT(4)
#define DEXCR_PR_NPHIE		DEXCR_PR_BIT(5)

#define PPC_RAW_HASH_ARGS(b, i, a) \
	((((i) >> 3) & 0x1F) << 21 | (a) << 16 | (b) << 11 | (((i) >> 8) & 0x1))
#define PPC_RAW_HASHST(b, i, a) \
	str(.long (0x7C0005A4 | PPC_RAW_HASH_ARGS(b, i, a));)
#define PPC_RAW_HASHCHK(b, i, a) \
	str(.long (0x7C0005E4 | PPC_RAW_HASH_ARGS(b, i, a));)

struct dexcr_aspect {
	const char *name;	/* Short display name */
	const char *opt;	/* Option name for chdexcr */
	const char *desc;	/* Expanded aspect meaning */
	unsigned int index;	/* Aspect bit index in DEXCR */
	unsigned long prctl;	/* 'which' value for get/set prctl */
};

static const struct dexcr_aspect aspects[] = {
	{
		.name = "SBHE",
		.opt = "sbhe",
		.desc = "Speculative branch hint enable",
		.index = 0,
		.prctl = PR_PPC_DEXCR_SBHE,
	},
	{
		.name = "IBRTPD",
		.opt = "ibrtpd",
		.desc = "Indirect branch recurrent target prediction disable",
		.index = 3,
		.prctl = PR_PPC_DEXCR_IBRTPD,
	},
	{
		.name = "SRAPD",
		.opt = "srapd",
		.desc = "Subroutine return address prediction disable",
		.index = 4,
		.prctl = PR_PPC_DEXCR_SRAPD,
	},
	{
		.name = "NPHIE",
		.opt = "nphie",
		.desc = "Non-privileged hash instruction enable",
		.index = 5,
		.prctl = PR_PPC_DEXCR_NPHIE,
	},
	{
		.name = "PHIE",
		.opt = "phie",
		.desc = "Privileged hash instruction enable",
		.index = 6,
		.prctl = -1,
	},
};

bool dexcr_exists(void);

bool pr_dexcr_aspect_supported(unsigned long which);

bool pr_dexcr_aspect_editable(unsigned long which);

int pr_get_dexcr(unsigned long pr_aspect);

int pr_set_dexcr(unsigned long pr_aspect, unsigned long ctrl);

unsigned int pr_which_to_aspect(unsigned long which);

bool hashchk_triggers(void);

enum dexcr_source {
	DEXCR,		/* Userspace DEXCR value */
	HDEXCR,		/* Hypervisor enforced DEXCR value */
	EFFECTIVE,	/* Bitwise OR of UDEXCR and ENFORCED DEXCR bits */
};

unsigned int get_dexcr(enum dexcr_source source);

void await_child_success(pid_t pid);

void hashst(unsigned long lr, void *sp);

void hashchk(unsigned long lr, void *sp);

void do_bad_hashchk(void);

#endif  /* _SELFTESTS_POWERPC_DEXCR_DEXCR_H */
