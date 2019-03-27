/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Ivan Voras <ivoras@FreeBSD.org>
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
#include <geom/geom_disk.h>
#include <geom/label/g_label.h>
#include <geom/multipath/g_multipath.h>


#define G_LABEL_DISK_IDENT_DIR	"diskid"

static char* classes_pass[] = { G_DISK_CLASS_NAME, G_MULTIPATH_CLASS_NAME,
    NULL };

static void
g_label_disk_ident_taste(struct g_consumer *cp, char *label, size_t size)
{
	struct g_class *cls;
	char ident[DISK_IDENT_SIZE];
	int ident_len, found, i;

	g_topology_assert_not();
	label[0] = '\0';

	cls = cp->provider->geom->class;

	/* 
	 * Get the GEOM::ident string, and construct a label in the format
	 * "CLASS_NAME-ident"
	 */
	ident_len = sizeof(ident);
	if (g_io_getattr("GEOM::ident", cp, &ident_len, ident) == 0) {
		if (ident_len == 0 || ident[0] == '\0')
			return;
		for (i = 0, found = 0; classes_pass[i] != NULL; i++)
			if (strcmp(classes_pass[i], cls->name) == 0) {
				found = 1;
				break;
			}
		if (!found)
			return;
		/*
		 * We can safely ignore the result of snprintf(): the label
		 * will simply be truncated, which at most is only annoying.
		 */
		(void)snprintf(label, size, "%s-%s", cls->name, ident);
	}
}

struct g_label_desc g_label_disk_ident = {
	.ld_taste = g_label_disk_ident_taste,
	.ld_dir = G_LABEL_DISK_IDENT_DIR,
	.ld_enabled = 1
};

G_LABEL_INIT(disk_ident, g_label_disk_ident, "Create device nodes for drives "
    "which export a disk identification string");
