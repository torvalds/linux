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

#ifndef _LIBBE_IMPL_H
#define _LIBBE_IMPL_H

#include <libzfs.h>

#include "be.h"

struct libbe_handle {
	char root[BE_MAXPATHLEN];
	char rootfs[BE_MAXPATHLEN];
	char bootfs[BE_MAXPATHLEN];
	size_t altroot_len;
	zpool_handle_t *active_phandle;
	libzfs_handle_t *lzh;
	be_error_t error;
	bool print_on_err;
};

struct libbe_deep_clone {
	libbe_handle_t *lbh;
	const char *bename;
	const char *snapname;
	const char *be_root;
};

struct libbe_dccb {
	libbe_handle_t *lbh;
	zfs_handle_t *zhp;
	nvlist_t *props;
};

typedef struct prop_data {
	nvlist_t *list;
	libbe_handle_t *lbh;
	bool single_object;	/* list will contain props directly */
} prop_data_t;

int prop_list_builder_cb(zfs_handle_t *, void *);
int be_proplist_update(prop_data_t *);

char *be_mountpoint_augmented(libbe_handle_t *lbh, char *mountpoint);

/* Clobbers any previous errors */
int set_error(libbe_handle_t *, be_error_t);

#endif  /* _LIBBE_IMPL_H */
