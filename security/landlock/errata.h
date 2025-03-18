/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Errata information
 *
 * Copyright © 2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_ERRATA_H
#define _SECURITY_LANDLOCK_ERRATA_H

#include <linux/init.h>

struct landlock_erratum {
	const int abi;
	const u8 number;
};

/* clang-format off */
#define LANDLOCK_ERRATUM(NUMBER) \
	{ \
		.abi = LANDLOCK_ERRATA_ABI, \
		.number = NUMBER, \
	},
/* clang-format on */

/*
 * Some fixes may require user space to check if they are applied on the running
 * kernel before using a specific feature.  For instance, this applies when a
 * restriction was previously too restrictive and is now getting relaxed (for
 * compatibility or semantic reasons).  However, non-visible changes for
 * legitimate use (e.g. security fixes) do not require an erratum.
 */
static const struct landlock_erratum landlock_errata_init[] __initconst = {

/*
 * Only Sparse may not implement __has_include.  If a compiler does not
 * implement __has_include, a warning will be printed at boot time (see
 * setup.c).
 */
#ifdef __has_include

#define LANDLOCK_ERRATA_ABI 1
#if __has_include("errata/abi-1.h")
#include "errata/abi-1.h"
#endif
#undef LANDLOCK_ERRATA_ABI

#define LANDLOCK_ERRATA_ABI 2
#if __has_include("errata/abi-2.h")
#include "errata/abi-2.h"
#endif
#undef LANDLOCK_ERRATA_ABI

#define LANDLOCK_ERRATA_ABI 3
#if __has_include("errata/abi-3.h")
#include "errata/abi-3.h"
#endif
#undef LANDLOCK_ERRATA_ABI

#define LANDLOCK_ERRATA_ABI 4
#if __has_include("errata/abi-4.h")
#include "errata/abi-4.h"
#endif
#undef LANDLOCK_ERRATA_ABI

/*
 * For each new erratum, we need to include all the ABI files up to the impacted
 * ABI to make all potential future intermediate errata easy to backport.
 *
 * If such change involves more than one ABI addition, then it must be in a
 * dedicated commit with the same Fixes tag as used for the actual fix.
 *
 * Each commit creating a new security/landlock/errata/abi-*.h file must have a
 * Depends-on tag to reference the commit that previously added the line to
 * include this new file, except if the original Fixes tag is enough.
 *
 * Each erratum must be documented in its related ABI file, and a dedicated
 * commit must update Documentation/userspace-api/landlock.rst to include this
 * erratum.  This commit will not be backported.
 */

#endif

	{}
};

#endif /* _SECURITY_LANDLOCK_ERRATA_H */
