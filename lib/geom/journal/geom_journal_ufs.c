/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/mount.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <libufs.h>
#include <libgeom.h>
#include <core/geom.h>
#include <misc/subr.h>

#include "geom_journal.h"

static struct fs *
read_superblock(const char *prov)
{
	static struct uufsd disk;
	struct fs *fs;

	if (ufs_disk_fillout(&disk, prov) == -1)
		return (NULL);
	fs = &disk.d_fs;
	ufs_disk_close(&disk);
	return (fs);
}

int
g_journal_ufs_exists(const char *prov)
{

	return (read_superblock(prov) != NULL);
}

int
g_journal_ufs_using_last_sector(const char *prov)
{
	struct fs *fs;
	off_t psize, fssize;

	fs = read_superblock(prov);
	if (fs == NULL)
		return (0);
	/* Provider size in 512 bytes blocks. */
	psize = g_get_mediasize(prov) / DEV_BSIZE;
	/* File system size in 512 bytes blocks. */
	fssize = fsbtodb(fs, fs->fs_size);
	return (psize <= fssize);
}
