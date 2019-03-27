/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Poul-Henning Kamp.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

/*
 * This file documents a private interface and it SHALL only be used
 * by kern/kern_conf.c and fs/devfs/...
 */

#ifndef _FS_DEVFS_DEVFS_INT_H_
#define	_FS_DEVFS_DEVFS_INT_H_

#include <sys/queue.h>

#ifdef _KERNEL

struct devfs_dirent;
struct devfs_mount;

struct cdev_privdata {
	struct file		*cdpd_fp;
	void			*cdpd_data;
	void			(*cdpd_dtr)(void *);
	LIST_ENTRY(cdev_privdata) cdpd_list;
};

struct cdev_priv {
	struct cdev		cdp_c;
	TAILQ_ENTRY(cdev_priv)	cdp_list;

	u_int			cdp_inode;

	u_int			cdp_flags;
#define CDP_ACTIVE		(1 << 0)
#define CDP_SCHED_DTR		(1 << 1)
#define	CDP_UNREF_DTR		(1 << 2)

	u_int			cdp_inuse;
	u_int			cdp_maxdirent;
	struct devfs_dirent	**cdp_dirents;
	struct devfs_dirent	*cdp_dirent0;

	TAILQ_ENTRY(cdev_priv)	cdp_dtr_list;
	void			(*cdp_dtr_cb)(void *);
	void			*cdp_dtr_cb_arg;

	LIST_HEAD(, cdev_privdata) cdp_fdpriv;
};

#define	cdev2priv(c)	__containerof(c, struct cdev_priv, cdp_c)

struct cdev	*devfs_alloc(int);
int	devfs_dev_exists(const char *);
void	devfs_free(struct cdev *);
void	devfs_create(struct cdev *);
void	devfs_destroy(struct cdev *);
void	devfs_destroy_cdevpriv(struct cdev_privdata *);

int	devfs_dir_find(const char *);
void	devfs_dir_ref_de(struct devfs_mount *, struct devfs_dirent *);
void	devfs_dir_unref_de(struct devfs_mount *, struct devfs_dirent *);
int	devfs_pathpath(const char *, const char *);

extern struct unrhdr *devfs_inos;
extern struct mtx devmtx;
extern struct mtx devfs_de_interlock;
extern struct sx clone_drain_lock;
extern struct mtx cdevpriv_mtx;
extern TAILQ_HEAD(cdev_priv_list, cdev_priv) cdevp_list;

#endif /* _KERNEL */

#endif /* !_FS_DEVFS_DEVFS_INT_H_ */
