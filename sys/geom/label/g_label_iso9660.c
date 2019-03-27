/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <geom/geom.h>
#include <geom/label/g_label.h>

#define G_LABEL_ISO9660_DIR	"iso9660"

#define	ISO9660_MAGIC	"\x01" "CD001" "\x01\x00"
#define	ISO9660_OFFSET	0x8000
#define	VOLUME_LEN	32


static void
g_label_iso9660_taste(struct g_consumer *cp, char *label, size_t size)
{
	struct g_provider *pp;
	char *sector, *volume;

	g_topology_assert_not();
	pp = cp->provider;
	label[0] = '\0';

	if ((ISO9660_OFFSET % pp->sectorsize) != 0)
		return;
	sector = (char *)g_read_data(cp, ISO9660_OFFSET, pp->sectorsize,
	    NULL);
	if (sector == NULL)
		return;
	if (bcmp(sector, ISO9660_MAGIC, sizeof(ISO9660_MAGIC) - 1) != 0) {
		g_free(sector);
		return;
	}
	G_LABEL_DEBUG(1, "ISO9660 file system detected on %s.", pp->name);
	volume = sector + 0x28;
	bzero(label, size);
	strlcpy(label, volume, MIN(size, VOLUME_LEN));
	g_free(sector);
	g_label_rtrim(label, size);
}

struct g_label_desc g_label_iso9660 = {
	.ld_taste = g_label_iso9660_taste,
	.ld_dir = G_LABEL_ISO9660_DIR,
	.ld_enabled = 1
};

G_LABEL_INIT(iso9660, g_label_iso9660, "Create device nodes for ISO9660 volume names");
