/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Jaakko Heinonen <jh@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/sx.h>

#include <fs/devfs/devfs.h>
#include <fs/devfs/devfs_int.h>

struct dirlistent {
	char			*dir;
	int			refcnt;
	LIST_ENTRY(dirlistent)	link;
};

static LIST_HEAD(, dirlistent) devfs_dirlist =
    LIST_HEAD_INITIALIZER(devfs_dirlist);

static MALLOC_DEFINE(M_DEVFS4, "DEVFS4", "DEVFS directory list");

static struct mtx dirlist_mtx;
MTX_SYSINIT(dirlist_mtx, &dirlist_mtx, "devfs dirlist lock", MTX_DEF);

/* Returns 1 if the path is in the directory list. */
int
devfs_dir_find(const char *path)
{
	struct dirlistent *dle;

	mtx_lock(&dirlist_mtx);
	LIST_FOREACH(dle, &devfs_dirlist, link) {
		if (devfs_pathpath(dle->dir, path) != 0) {
			mtx_unlock(&dirlist_mtx);
			return (1);
		}
	}
	mtx_unlock(&dirlist_mtx);

	return (0);
}

static struct dirlistent *
devfs_dir_findent_locked(const char *dir)
{
	struct dirlistent *dle;

	mtx_assert(&dirlist_mtx, MA_OWNED);

	LIST_FOREACH(dle, &devfs_dirlist, link) {
		if (strcmp(dir, dle->dir) == 0)
			return (dle);
	}

	return (NULL);
}

static void
devfs_dir_ref(const char *dir)
{
	struct dirlistent *dle, *dle_new;

	if (*dir == '\0')
		return;

	dle_new = malloc(sizeof(*dle), M_DEVFS4, M_WAITOK);
	dle_new->dir = strdup(dir, M_DEVFS4);
	dle_new->refcnt = 1;

	mtx_lock(&dirlist_mtx);
	dle = devfs_dir_findent_locked(dir);
	if (dle != NULL) {
		dle->refcnt++;
		mtx_unlock(&dirlist_mtx);
		free(dle_new->dir, M_DEVFS4);
		free(dle_new, M_DEVFS4);
		return;
	}
	LIST_INSERT_HEAD(&devfs_dirlist, dle_new, link);
	mtx_unlock(&dirlist_mtx);
}

void
devfs_dir_ref_de(struct devfs_mount *dm, struct devfs_dirent *de)
{
	char dirname[SPECNAMELEN + 1], *namep;

	namep = devfs_fqpn(dirname, dm, de, NULL);
	KASSERT(namep != NULL, ("devfs_ref_dir_de: NULL namep"));

	devfs_dir_ref(namep);
}

static void
devfs_dir_unref(const char *dir)
{
	struct dirlistent *dle;

	if (*dir == '\0')
		return;

	mtx_lock(&dirlist_mtx);
	dle = devfs_dir_findent_locked(dir);
	KASSERT(dle != NULL, ("devfs_dir_unref: dir %s not referenced", dir));
	dle->refcnt--;
	KASSERT(dle->refcnt >= 0, ("devfs_dir_unref: negative refcnt"));
	if (dle->refcnt == 0) {
		LIST_REMOVE(dle, link);
		mtx_unlock(&dirlist_mtx);
		free(dle->dir, M_DEVFS4);
		free(dle, M_DEVFS4);
	} else
		mtx_unlock(&dirlist_mtx);
}

void
devfs_dir_unref_de(struct devfs_mount *dm, struct devfs_dirent *de)
{
	char dirname[SPECNAMELEN + 1], *namep;

	namep = devfs_fqpn(dirname, dm, de, NULL);
	KASSERT(namep != NULL, ("devfs_unref_dir_de: NULL namep"));

	devfs_dir_unref(namep);
}

/* Returns 1 if the path p1 contains the path p2. */
int
devfs_pathpath(const char *p1, const char *p2)
{

	for (;;p1++, p2++) {
		if (*p1 != *p2) {
			if (*p1 == '/' && *p2 == '\0')
				return (1);
			else
				return (0);
		} else if (*p1 == '\0')
			return (1);
	}
	/* NOTREACHED */
}
