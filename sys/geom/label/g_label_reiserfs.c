/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Stanislav Sedov
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <geom/geom.h>
#include <geom/label/g_label.h>

#define REISERFS_NEW_DISK_OFFSET 64 * 1024
#define REISERFS_OLD_DISK_OFFSET 8 * 1024
#define REISERFS_SUPER_MAGIC	"ReIsEr"

typedef struct reiserfs_sb {
	uint8_t		fake1[52];
	char		s_magic[10];
	uint8_t		fake2[10];
	uint16_t	s_version;
	uint8_t		fake3[26];
	char		s_volume_name[16];
} reiserfs_sb_t;

static reiserfs_sb_t *
g_label_reiserfs_read_super(struct g_consumer *cp, off_t offset)
{
	reiserfs_sb_t *fs;
	u_int secsize;

	secsize = cp->provider->sectorsize;

	if ((offset % secsize) != 0)
		return (NULL);

	fs = (reiserfs_sb_t *)g_read_data(cp, offset, secsize, NULL);
	if (fs == NULL)
		return (NULL);

	if (strncmp(fs->s_magic, REISERFS_SUPER_MAGIC,
	    strlen(REISERFS_SUPER_MAGIC)) != 0) {
		g_free(fs);
		return (NULL);
	}

	return (fs);
}

static void
g_label_reiserfs_taste(struct g_consumer *cp, char *label, size_t size)
{
	struct g_provider *pp;
	reiserfs_sb_t *fs;

	g_topology_assert_not();
	pp = cp->provider;
	label[0] = '\0';

	/* Try old format */
	fs = g_label_reiserfs_read_super(cp, REISERFS_OLD_DISK_OFFSET);
	if (fs == NULL) {
		/* Try new format */
		fs = g_label_reiserfs_read_super(cp, REISERFS_NEW_DISK_OFFSET);
	}
	if (fs == NULL)
		return;

	/* Check version */
	if (fs->s_version == 2) {
		G_LABEL_DEBUG(1, "reiserfs file system detected on %s.",
		    pp->name);
	} else {
		goto exit_free;
	}

	/* Check for volume label */
	if (fs->s_volume_name[0] == '\0')
		goto exit_free;

	/* Terminate label */
	fs->s_volume_name[sizeof(fs->s_volume_name) - 1] = '\0';
	strlcpy(label, fs->s_volume_name, size);

exit_free:
	g_free(fs);
}

struct g_label_desc g_label_reiserfs = {
	.ld_taste = g_label_reiserfs_taste,
	.ld_dir = "reiserfs",
	.ld_enabled = 1
};

G_LABEL_INIT(reiserfs, g_label_reiserfs, "Create device nodes for REISERFS volumes");
