/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 ARM Limited */

/*
 * Below definitions may be found in kernel headers, However, they are
 * redefined here to decouple the MTE selftests compilations from them.
 */
#ifndef SEGV_MTEAERR
#define	SEGV_MTEAERR	8
#endif
#ifndef SEGV_MTESERR
#define	SEGV_MTESERR	9
#endif
#ifndef PROT_MTE
#define PROT_MTE	 0x20
#endif
#ifndef HWCAP2_MTE
#define HWCAP2_MTE	(1 << 18)
#endif

#ifndef PR_MTE_TCF_SHIFT
#define PR_MTE_TCF_SHIFT	1
#endif
#ifndef PR_MTE_TCF_NONE
#define PR_MTE_TCF_NONE		(0UL << PR_MTE_TCF_SHIFT)
#endif
#ifndef PR_MTE_TCF_SYNC
#define	PR_MTE_TCF_SYNC		(1UL << PR_MTE_TCF_SHIFT)
#endif
#ifndef PR_MTE_TCF_ASYNC
#define PR_MTE_TCF_ASYNC	(2UL << PR_MTE_TCF_SHIFT)
#endif
#ifndef PR_MTE_TAG_SHIFT
#define	PR_MTE_TAG_SHIFT	3
#endif

/* MTE Hardware feature definitions below. */
#define MT_TAG_SHIFT		56
#define MT_TAG_MASK		0xFUL
#define MT_FREE_TAG		0x0UL
#define MT_GRANULE_SIZE         16
#define MT_TAG_COUNT		16
#define MT_INCLUDE_TAG_MASK	0xFFFF
#define MT_EXCLUDE_TAG_MASK	0x0

#define MT_ALIGN_GRANULE	(MT_GRANULE_SIZE - 1)
#define MT_CLEAR_TAG(x)		((x) & ~(MT_TAG_MASK << MT_TAG_SHIFT))
#define MT_SET_TAG(x, y)	((x) | (y << MT_TAG_SHIFT))
#define MT_FETCH_TAG(x)		((x >> MT_TAG_SHIFT) & (MT_TAG_MASK))
#define MT_ALIGN_UP(x)		((x + MT_ALIGN_GRANULE) & ~(MT_ALIGN_GRANULE))

#define MT_PSTATE_TCO_SHIFT	25
#define MT_PSTATE_TCO_MASK	~(0x1 << MT_PSTATE_TCO_SHIFT)
#define MT_PSTATE_TCO_EN	1
#define MT_PSTATE_TCO_DIS	0

#define MT_EXCLUDE_TAG(x)		(1 << (x))
#define MT_INCLUDE_VALID_TAG(x)		(MT_INCLUDE_TAG_MASK ^ MT_EXCLUDE_TAG(x))
#define MT_INCLUDE_VALID_TAGS(x)	(MT_INCLUDE_TAG_MASK ^ (x))
#define MTE_ALLOW_NON_ZERO_TAG		MT_INCLUDE_VALID_TAG(0)
