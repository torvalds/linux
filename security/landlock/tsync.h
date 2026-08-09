/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Cross-thread ruleset enforcement
 *
 * Copyright © 2025 Google LLC
 */

#ifndef _SECURITY_LANDLOCK_TSYNC_H
#define _SECURITY_LANDLOCK_TSYNC_H

#include <linux/cred.h>
#include <linux/types.h>

int landlock_restrict_sibling_threads(const struct cred *old_cred,
				      const struct cred *new_cred,
				      u32 restrict_flags);

#endif /* _SECURITY_LANDLOCK_TSYNC_H */
