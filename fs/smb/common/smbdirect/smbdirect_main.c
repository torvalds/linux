// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"
#include <linux/module.h>

struct smbdirect_module_state smbdirect_globals = {
	.mutex = __MUTEX_INITIALIZER(smbdirect_globals.mutex),
};

static __init int smbdirect_module_init(void)
{
	pr_notice("subsystem loading...\n");
	mutex_lock(&smbdirect_globals.mutex);

	/* TODO... */

	mutex_unlock(&smbdirect_globals.mutex);
	pr_notice("subsystem loaded\n");
	return 0;
}

static __exit void smbdirect_module_exit(void)
{
	pr_notice("subsystem unloading...\n");
	mutex_lock(&smbdirect_globals.mutex);

	/* TODO... */

	mutex_unlock(&smbdirect_globals.mutex);
	pr_notice("subsystem unloaded\n");
}

module_init(smbdirect_module_init);
module_exit(smbdirect_module_exit);

MODULE_DESCRIPTION("smbdirect subsystem");
MODULE_LICENSE("GPL");
