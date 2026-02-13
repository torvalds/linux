/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Red Hat, Inc. All Rights Reserved.
 *
 * Author: Coiby Xu <coxu@redhat.com>
 */

#ifndef _LINUX_SECURE_BOOT_H
#define _LINUX_SECURE_BOOT_H

#include <linux/types.h>

/*
 * Returns true if the platform secure boot is enabled.
 * Returns false if disabled or not supported.
 */
bool arch_get_secureboot(void);

#endif /* _LINUX_SECURE_BOOT_H */
