/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Namespace hooks
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _SECURITY_LANDLOCK_NS_H
#define _SECURITY_LANDLOCK_NS_H

#include <linux/types.h>

u64 landlock_ns_type_to_bit(u32 ns_type);
u64 landlock_ns_types_to_bits(u64 ns_types);

__init void landlock_add_ns_hooks(void);

#endif /* _SECURITY_LANDLOCK_NS_H */
