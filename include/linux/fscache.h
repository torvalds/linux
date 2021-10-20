/* SPDX-License-Identifier: GPL-2.0-or-later */
/* General filesystem caching interface
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * NOTE!!! See:
 *
 *	Documentation/filesystems/caching/netfs-api.rst
 *
 * for a description of the network filesystem interface declared here.
 */

#ifndef _LINUX_FSCACHE_H
#define _LINUX_FSCACHE_H

#include <linux/fs.h>
#include <linux/netfs.h>

#if defined(CONFIG_FSCACHE) || defined(CONFIG_FSCACHE_MODULE)
#define __fscache_available (1)
#define fscache_available() (1)
#define fscache_cookie_valid(cookie) (cookie)
#define fscache_cookie_enabled(cookie) (cookie)
#else
#define __fscache_available (0)
#define fscache_available() (0)
#define fscache_cookie_valid(cookie) (0)
#define fscache_cookie_enabled(cookie) (0)
#endif

#endif /* _LINUX_FSCACHE_H */
