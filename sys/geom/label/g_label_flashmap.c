/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Ian Lepore <ian@FreeBSD.org>
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
#include <sys/slicer.h>

#include <geom/geom.h>
#include <geom/geom_flashmap.h>
#include <geom/geom_slice.h>
#include <geom/label/g_label.h>

#define	G_LABEL_FLASHMAP_SLICE_DIR	"flash"

static void
g_label_flashmap_taste(struct g_consumer *cp, char *label, size_t size)
{
	struct g_flashmap *gfp;
	struct g_slicer *gsp;
	struct g_provider *pp;

	g_topology_assert_not();

	pp = cp->provider;
	label[0] = '\0';

	/* We taste only partitions handled by flashmap */
	if (strncmp(pp->geom->class->name, FLASHMAP_CLASS_NAME,
	    sizeof(FLASHMAP_CLASS_NAME)) != 0)
		return;

	gsp = (struct g_slicer *)pp->geom->softc;
	gfp = (struct g_flashmap *)gsp->softc;

	/* If it's handled by flashmap it should have a label, but be safe. */
	if (gfp->labels[pp->index] == NULL)
		return;

	strlcpy(label, gfp->labels[pp->index], size);
}

struct g_label_desc g_label_flashmap = {
	.ld_taste = g_label_flashmap_taste,
	.ld_dir = G_LABEL_FLASHMAP_SLICE_DIR,
	.ld_enabled = 1
};

G_LABEL_INIT(flashmap, g_label_flashmap, "Create device nodes for Flashmap labels");
