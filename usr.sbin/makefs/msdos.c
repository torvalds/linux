/*	$OpenBSD: msdos.c,v 1.13 2021/10/06 00:40:39 deraadt Exp $	*/
/*	$NetBSD: msdos.c,v 1.16 2016/01/30 09:59:27 mlelstv Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "ffs/buf.h"
#include "msdos/denode.h"
#include "makefs.h"
#include "msdos.h"
#include "msdos/mkfs_msdos.h"

static int msdos_populate_dir(const char *, struct denode *, fsnode *,
    fsnode *, fsinfo_t *);

void
msdos_prep_opts(fsinfo_t *fsopts)
{
	struct msdos_options *msdos_opt = ecalloc(1, sizeof(*msdos_opt));
	const option_t msdos_options[] = {
#define AOPT(_type, _name, _min) {					\
	.name = # _name,						\
	.type = _min == -1 ? OPT_STRPTR :				\
	    (_min == -2 ? OPT_BOOL :					\
	    (sizeof(_type) == 1 ? OPT_INT8 :				\
	    (sizeof(_type) == 2 ? OPT_INT16 :				\
	    (sizeof(_type) == 4 ? OPT_INT32 : OPT_INT64)))),		\
	.value = &msdos_opt->_name,					\
	.minimum = _min,						\
	.maximum = sizeof(_type) == 1 ? 0xff :				\
	    (sizeof(_type) == 2 ? 0xffff :				\
	    (sizeof(_type) == 4 ? 0xffffffff : 0x7fffffffffffffffLL)),	\
},
ALLOPTS
#undef AOPT
		{ .name = NULL }
	};

	fsopts->fs_specific = msdos_opt;
	fsopts->fs_options = copy_opts(msdos_options);
}

void
msdos_cleanup_opts(fsinfo_t *fsopts)
{
	free(fsopts->fs_specific);
	free(fsopts->fs_options);
}

int
msdos_parse_opts(const char *option, fsinfo_t *fsopts)
{
	struct msdos_options *msdos_opt = fsopts->fs_specific;
	option_t *msdos_options = fsopts->fs_options;

	int rv;

	assert(option != NULL);
	assert(fsopts != NULL);
	assert(msdos_opt != NULL);

	rv = set_option(msdos_options, option, NULL, 0);
	if (rv == -1)
		return rv;

	if (strcmp(msdos_options[rv].name, "volume_id") == 0)
		msdos_opt->volume_id_set = 1;
	else if (strcmp(msdos_options[rv].name, "media_descriptor") == 0)
		msdos_opt->media_descriptor_set = 1;
	else if (strcmp(msdos_options[rv].name, "hidden_sectors") == 0)
		msdos_opt->hidden_sectors_set = 1;
	return 1;
}


void
msdos_makefs(const char *image, const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	struct msdos_options *msdos_opt = fsopts->fs_specific;
	struct mkfsvnode vp, rootvp;
	struct msdosfsmount *pmp;

	assert(image != NULL);
	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);

	/*
	 * XXX: pick up other options from the msdos specific ones?
	 * Is minsize right here?
	 */
	msdos_opt->create_size = MAXIMUM(msdos_opt->create_size,
		fsopts->minsize);
	msdos_opt->offset = fsopts->offset;
	if (msdos_opt->bytes_per_sector == 0) {
		if (fsopts->sectorsize == -1)
			fsopts->sectorsize = 512;
		msdos_opt->bytes_per_sector = fsopts->sectorsize;
	} else if (fsopts->sectorsize == -1) {
		fsopts->sectorsize = msdos_opt->bytes_per_sector;
	} else if (fsopts->sectorsize != msdos_opt->bytes_per_sector) {
		err(1, "inconsistent sectorsize -S %u"
		    "!= -o bytes_per_sector %u",
		    fsopts->sectorsize, msdos_opt->bytes_per_sector);
	}

		/* create image */
	printf("Creating `%s'\n", image);
	if (mkfs_msdos(image, NULL, msdos_opt) == -1)
		return;

	fsopts->fd = open(image, O_RDWR);
	vp.fs = fsopts;

	if ((pmp = msdosfs_mount(&vp, 0)) == NULL)
		err(1, "msdosfs_mount");

	if (msdosfs_root(pmp, &rootvp) != 0)
		err(1, "msdosfs_root");

		/* populate image */
	printf("Populating `%s'\n", image);
	if (msdos_populate_dir(dir, VTODE(&rootvp), root, root, fsopts) == -1)
		errx(1, "Image file `%s' not created.", image);

	bcleanup();

	printf("Image `%s' complete\n", image);
}

static int
msdos_populate_dir(const char *path, struct denode *dir, fsnode *root,
    fsnode *parent, fsinfo_t *fsopts)
{
	fsnode *cur;
	char pbuf[PATH_MAX];

	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);

	for (cur = root->next; cur != NULL; cur = cur->next) {
		if ((size_t)snprintf(pbuf, sizeof(pbuf), "%s/%s", path,
		    cur->name) >= sizeof(pbuf)) {
			warnx("path %s too long", pbuf);
			return -1;
		}

		if ((cur->inode->flags & FI_ALLOCATED) == 0) {
			cur->inode->flags |= FI_ALLOCATED;
			if (cur != root) {
				fsopts->curinode++;
				cur->inode->ino = fsopts->curinode;
				cur->parent = parent;
			}
		}

		if (cur->inode->flags & FI_WRITTEN) {
			continue;	// hard link
		}
		cur->inode->flags |= FI_WRITTEN;

		if (cur->child) {
			struct denode *de;
			if ((de = msdosfs_mkdire(pbuf, dir, cur)) == NULL) {
				warn("msdosfs_mkdire %s", pbuf);
				return -1;
			}
			if (msdos_populate_dir(pbuf, de, cur->child, cur,
			    fsopts) == -1) {
				warn("msdos_populate_dir %s", pbuf);
				return -1;
			}
			continue;
		} else if (!S_ISREG(cur->type)) {
			warnx("skipping non-regular file %s/%s", cur->path,
			    cur->name);
			continue;
		}
		if (msdosfs_mkfile(pbuf, dir, cur) == NULL) {
			warn("msdosfs_mkfile %s", pbuf);
			return -1;
		}
	}
	return 0;
}
