// SPDX-License-Identifier: GPL-2.0-or-later
/* Error injection handling.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/sysctl.h>
#include "internal.h"

unsigned int cachefiles_error_injection_state;

static struct ctl_table_header *cachefiles_sysctl;
static struct ctl_table cachefiles_sysctls[] = {
	{
		.procname	= "error_injection",
		.data		= &cachefiles_error_injection_state,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec,
	},
};

int __init cachefiles_register_error_injection(void)
{
	cachefiles_sysctl = register_sysctl("cachefiles", cachefiles_sysctls);
	if (!cachefiles_sysctl)
		return -ENOMEM;
	return 0;

}

void cachefiles_unregister_error_injection(void)
{
	unregister_sysctl_table(cachefiles_sysctl);
}
