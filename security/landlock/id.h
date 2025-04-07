/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Unique identification number generator
 *
 * Copyright © 2024-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_ID_H
#define _SECURITY_LANDLOCK_ID_H

#ifdef CONFIG_AUDIT

void __init landlock_init_id(void);

u64 landlock_get_id_range(size_t number_of_ids);

#else /* CONFIG_AUDIT */

static inline void __init landlock_init_id(void)
{
}

#endif /* CONFIG_AUDIT */

#endif /* _SECURITY_LANDLOCK_ID_H */
