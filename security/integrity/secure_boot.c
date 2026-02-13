// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Red Hat, Inc. All Rights Reserved.
 *
 * Author: Coiby Xu <coxu@redhat.com>
 */
#include <linux/secure_boot.h>

/*
 * Default weak implementation.
 * Architectures that support secure boot must override this.
 */
__weak bool arch_get_secureboot(void)
{
	return false;
}
