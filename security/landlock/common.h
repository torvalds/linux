/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Common constants and helpers
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#ifndef _SECURITY_LANDLOCK_COMMON_H
#define _SECURITY_LANDLOCK_COMMON_H

#define LANDLOCK_NAME "landlock"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) LANDLOCK_NAME ": " fmt

#define BIT_INDEX(bit) HWEIGHT(bit - 1)

#endif /* _SECURITY_LANDLOCK_COMMON_H */
