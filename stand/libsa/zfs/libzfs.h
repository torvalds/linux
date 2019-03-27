/*-
 * Copyright (c) 2012 Andriy Gapon <avg@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BOOT_LIBZFS_H_
#define _BOOT_LIBZFS_H_

#define	ZFS_MAXNAMELEN	256

/*
 * ZFS fully-qualified device descriptor.
 */
struct zfs_devdesc {
	struct devdesc	dd;		/* Must be first. */
	uint64_t	pool_guid;
	uint64_t	root_guid;
};

#ifdef LOADER_GELI_SUPPORT
#include <crypto/intake.h>
#endif

int	zfs_parsedev(struct zfs_devdesc *dev, const char *devspec,
		     const char **path);
char	*zfs_fmtdev(void *vdev);
int	zfs_probe_dev(const char *devname, uint64_t *pool_guid);
int	zfs_list(const char *name);
uint64_t ldi_get_size(void *);
void	init_zfs_bootenv(const char *currdev);
int	zfs_bootenv(const char *name);
int	zfs_belist_add(const char *name, uint64_t __unused);
int	zfs_set_env(void);

extern struct devsw zfs_dev;
extern struct fs_ops zfs_fsops;

#endif /*_BOOT_LIBZFS_H_*/
