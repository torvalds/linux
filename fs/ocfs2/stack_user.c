/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * stack_user.c
 *
 * Code which interfaces ocfs2 with fs/dlm and a userspace stack.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>

#include "stackglue.h"


static int __init user_stack_init(void)
{
	return 0;
}

static void __exit user_stack_exit(void)
{
}

MODULE_AUTHOR("Oracle");
MODULE_DESCRIPTION("ocfs2 driver for userspace cluster stacks");
MODULE_LICENSE("GPL");
module_init(user_stack_init);
module_exit(user_stack_exit);
