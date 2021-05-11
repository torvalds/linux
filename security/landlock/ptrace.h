/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Ptrace hooks
 *
 * Copyright © 2017-2019 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019 ANSSI
 */

#ifndef _SECURITY_LANDLOCK_PTRACE_H
#define _SECURITY_LANDLOCK_PTRACE_H

__init void landlock_add_ptrace_hooks(void);

#endif /* _SECURITY_LANDLOCK_PTRACE_H */
