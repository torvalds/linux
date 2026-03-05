/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Module signature handling.
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_MODULE_SIGNATURE_H
#define _LINUX_MODULE_SIGNATURE_H

#include <linux/types.h>
#include <uapi/linux/module_signature.h>

int mod_check_sig(const struct module_signature *ms, size_t file_len,
		  const char *name);

#endif /* _LINUX_MODULE_SIGNATURE_H */
