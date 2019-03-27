/* $NetBSD: cd9660_archimedes.c,v 1.1 2009/01/10 22:06:29 bjh21 Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998, 2009 Ben Harris
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * cd9660_archimedes.c - support for RISC OS "ARCHIMEDES" extension
 *
 * RISC OS CDFS looks for a special block at the end of the System Use
 * Field for each file.  If present, this contains the RISC OS load
 * and exec address (used to hold the file timestamp and type), the
 * file attributes, and a flag indicating whether the first character
 * of the filename should be replaced with '!' (since many special
 * RISC OS filenames do).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util.h>

#include "makefs.h"
#include "cd9660.h"
#include "cd9660_archimedes.h"

/*
 * Convert a Unix time_t (non-leap seconds since 1970-01-01) to a RISC
 * OS time (non-leap(?) centiseconds since 1900-01-01(?)).
 */

static u_int64_t
riscos_date(time_t unixtime)
{
	u_int64_t base;

	base = 31536000ULL * 70 + 86400 * 17;
	return (((u_int64_t)unixtime) + base)*100;
}

/*
 * Add "ARCHIMEDES" metadata to a node if that seems appropriate.
 *
 * We touch regular files with names matching /,[0-9a-f]{3}$/ and
 * directories matching /^!/.
 */
static void
archimedes_convert_node(cd9660node *node)
{
	struct ISO_ARCHIMEDES *arc;
	size_t len;
	int type = -1;
	uint64_t stamp;

	if (node->su_tail_data != NULL)
		/* Something else already has the tail. */
		return;

	len = strlen(node->node->name);
	if (len < 1) return;

	if (len >= 4 && node->node->name[len-4] == ',')
		/* XXX should support ,xxx and ,lxa */
		type = strtoul(node->node->name + len - 3, NULL, 16);
	if (type == -1 && node->node->name[0] != '!')
		return;
	if (type == -1) type = 0;

	assert(sizeof(*arc) == 32);
	arc = ecalloc(1, sizeof(*arc));

	stamp = riscos_date(node->node->inode->st.st_mtime);

	memcpy(arc->magic, "ARCHIMEDES", 10);
	cd9660_731(0xfff00000 | (type << 8) | (stamp >> 32), arc->loadaddr);
	cd9660_731(stamp & 0x00ffffffffULL, arc->execaddr);
	arc->ro_attr = RO_ACCESS_UR | RO_ACCESS_OR;
	arc->cdfs_attr = node->node->name[0] == '!' ? CDFS_PLING : 0;
	node->su_tail_data = (void *)arc;
	node->su_tail_size = sizeof(*arc);
}

/*
 * Add "ARCHIMEDES" metadata to an entire tree recursively.
 */
void
archimedes_convert_tree(cd9660node *node)
{
	cd9660node *cn;

	assert(node != NULL);

	archimedes_convert_node(node);

		/* Recurse on children. */
	TAILQ_FOREACH(cn, &node->cn_children, cn_next_child)
		archimedes_convert_tree(cn);
}
