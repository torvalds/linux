/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Kyle J. Kneitinger <kyle@kneit.in>
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

#ifndef _LIBBE_H
#define _LIBBE_H

#include <libnvpair.h>
#include <stdbool.h>

#define BE_MAXPATHLEN    512

typedef struct libbe_handle libbe_handle_t;

typedef enum be_error {
	BE_ERR_SUCCESS = 0,     /* No error */
	BE_ERR_INVALIDNAME,     /* invalid boot env name */
	BE_ERR_EXISTS,          /* boot env name already taken */
	BE_ERR_NOENT,           /* boot env doesn't exist */
	BE_ERR_PERMS,           /* insufficient permissions */
	BE_ERR_DESTROYACT,      /* cannot destroy active boot env */
	BE_ERR_DESTROYMNT,      /* destroying a mounted be requires force */
	BE_ERR_BADPATH,		/* path not suitable for operation */
	BE_ERR_PATHBUSY,	/* requested path is busy */
	BE_ERR_PATHLEN,         /* provided name exceeds maximum length limit */
	BE_ERR_BADMOUNT,        /* mountpoint is not '/' */
	BE_ERR_NOORIGIN,        /* could not open snapshot's origin */
	BE_ERR_MOUNTED,         /* boot environment is already mounted */
	BE_ERR_NOMOUNT,         /* boot environment is not mounted */
	BE_ERR_ZFSOPEN,         /* calling zfs_open() failed */
	BE_ERR_ZFSCLONE,        /* error when calling zfs_clone to create be */
	BE_ERR_IO,		/* error when doing some I/O operation */
	BE_ERR_NOPOOL,		/* operation not supported on this pool */
	BE_ERR_NOMEM,		/* insufficient memory */
	BE_ERR_UNKNOWN,         /* unknown error */
	BE_ERR_INVORIGIN,       /* invalid origin */
} be_error_t;


/* Library handling functions: be.c */
libbe_handle_t *libbe_init(const char *root);
void libbe_close(libbe_handle_t *);

/* Bootenv information functions: be_info.c */
const char *be_active_name(libbe_handle_t *);
const char *be_active_path(libbe_handle_t *);
const char *be_nextboot_name(libbe_handle_t *);
const char *be_nextboot_path(libbe_handle_t *);
const char *be_root_path(libbe_handle_t *);

int be_get_bootenv_props(libbe_handle_t *, nvlist_t *);
int be_get_dataset_props(libbe_handle_t *, const char *, nvlist_t *);
int be_get_dataset_snapshots(libbe_handle_t *, const char *, nvlist_t *);
int be_prop_list_alloc(nvlist_t **be_list);
void be_prop_list_free(nvlist_t *be_list);

int be_activate(libbe_handle_t *, const char *, bool);

/* Bootenv creation functions */
int be_create(libbe_handle_t *, const char *);
int be_create_from_existing(libbe_handle_t *, const char *, const char *);
int be_create_from_existing_snap(libbe_handle_t *, const char *, const char *);
int be_snapshot(libbe_handle_t *, const char *, const char *, bool, char *);

/* Bootenv manipulation functions */
int be_rename(libbe_handle_t *, const char *, const char *);

/* Bootenv removal functions */

typedef enum {
	BE_DESTROY_FORCE	= 1 << 0,
	BE_DESTROY_ORIGIN	= 1 << 1,
} be_destroy_opt_t;

int be_destroy(libbe_handle_t *, const char *, int);

/* Bootenv mounting functions: be_access.c */

typedef enum {
	BE_MNT_FORCE		= 1 << 0,
	BE_MNT_DEEP		= 1 << 1,
} be_mount_opt_t;

int be_mount(libbe_handle_t *, char *, char *, int, char *);
int be_unmount(libbe_handle_t *, char *, int);
int be_mounted_at(libbe_handle_t *, const char *path, nvlist_t *);

/* Error related functions: be_error.c */
int libbe_errno(libbe_handle_t *);
const char *libbe_error_description(libbe_handle_t *);
void libbe_print_on_error(libbe_handle_t *, bool);

/* Utility Functions */
int be_root_concat(libbe_handle_t *, const char *, char *);
int be_validate_name(libbe_handle_t * __unused, const char *);
int be_validate_snap(libbe_handle_t *, const char *);
int be_exists(libbe_handle_t *, char *);

int be_export(libbe_handle_t *, const char *, int fd);
int be_import(libbe_handle_t *, const char *, int fd);

#if SOON
int be_add_child(libbe_handle_t *, const char *, bool);
#endif
void be_nicenum(uint64_t num, char *buf, size_t buflen);

#endif  /* _LIBBE_H */
