/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Limits for different components
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2021-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_LIMITS_H
#define _SECURITY_LANDLOCK_LIMITS_H

#include <linux/bitops.h>
#include <linux/limits.h>
#include <uapi/linux/landlock.h>

/* clang-format off */

#define LANDLOCK_MAX_NUM_LAYERS		16
#define LANDLOCK_MAX_NUM_RULES		U32_MAX

#define LANDLOCK_LAST_ACCESS_FS		LANDLOCK_ACCESS_FS_IOCTL_DEV
#define LANDLOCK_MASK_ACCESS_FS		((LANDLOCK_LAST_ACCESS_FS << 1) - 1)
#define LANDLOCK_NUM_ACCESS_FS		__const_hweight64(LANDLOCK_MASK_ACCESS_FS)

#define LANDLOCK_LAST_ACCESS_NET	LANDLOCK_ACCESS_NET_CONNECT_TCP
#define LANDLOCK_MASK_ACCESS_NET	((LANDLOCK_LAST_ACCESS_NET << 1) - 1)
#define LANDLOCK_NUM_ACCESS_NET		__const_hweight64(LANDLOCK_MASK_ACCESS_NET)

#define LANDLOCK_LAST_SCOPE		LANDLOCK_SCOPE_SIGNAL
#define LANDLOCK_MASK_SCOPE		((LANDLOCK_LAST_SCOPE << 1) - 1)
#define LANDLOCK_NUM_SCOPE		__const_hweight64(LANDLOCK_MASK_SCOPE)

#define LANDLOCK_LAST_RESTRICT_SELF	LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF
#define LANDLOCK_MASK_RESTRICT_SELF	((LANDLOCK_LAST_RESTRICT_SELF << 1) - 1)

/* clang-format on */

#endif /* _SECURITY_LANDLOCK_LIMITS_H */
