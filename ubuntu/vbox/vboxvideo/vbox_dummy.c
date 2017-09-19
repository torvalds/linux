/*  $Id: vbox_dummy.c $ */
/** @file
 * VirtualBox Additions Linux kernel video driver, dummy driver for
 * older kernels.
 */

/*
 * Copyright (C) 2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

static int __init vbox_init(void)
{
    return -EINVAL;
}
static void __exit vbox_exit(void)
{
}

module_init(vbox_init);
module_exit(vbox_exit);

MODULE_LICENSE("GPL");
