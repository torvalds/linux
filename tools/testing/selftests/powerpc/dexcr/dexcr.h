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

bool dexcr_exists(void);

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
