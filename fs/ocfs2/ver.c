/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ver.c
 *
 * version string
 *
 * Copyright (C) 2002, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include "ver.h"

#define OCFS2_BUILD_VERSION "1.5.0"

#define VERSION_STR "OCFS2 " OCFS2_BUILD_VERSION

void ocfs2_print_version(void)
{
	printk(KERN_INFO "%s\n", VERSION_STR);
}

MODULE_DESCRIPTION(VERSION_STR);

MODULE_VERSION(OCFS2_BUILD_VERSION);
