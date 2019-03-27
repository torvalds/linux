/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <libgeom.h>
#include <geom/shsec/g_shsec.h>

#include "core/geom.h"
#include "misc/subr.h"


uint32_t lib_version = G_LIB_VERSION;
uint32_t version = G_SHSEC_VERSION;

static void shsec_main(struct gctl_req *req, unsigned flags);
static void shsec_clear(struct gctl_req *req);
static void shsec_dump(struct gctl_req *req);
static void shsec_label(struct gctl_req *req);

struct g_command class_commands[] = {
	{ "clear", G_FLAG_VERBOSE, shsec_main, G_NULL_OPTS,
	    "[-v] prov ..."
	},
	{ "dump", 0, shsec_main, G_NULL_OPTS,
	    "prov ..."
	},
	{ "label", G_FLAG_VERBOSE | G_FLAG_LOADKLD, shsec_main,
	    {
		{ 'h', "hardcode", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-hv] name prov prov ..."
	},
	{ "stop", G_FLAG_VERBOSE, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-fv] name ..."
	},
	G_CMD_SENTINEL
};

static int verbose = 0;

static void
shsec_main(struct gctl_req *req, unsigned flags)
{
	const char *name;

	if ((flags & G_FLAG_VERBOSE) != 0)
		verbose = 1;

	name = gctl_get_ascii(req, "verb");
	if (name == NULL) {
		gctl_error(req, "No '%s' argument.", "verb");
		return;
	}
	if (strcmp(name, "label") == 0)
		shsec_label(req);
	else if (strcmp(name, "clear") == 0)
		shsec_clear(req);
	else if (strcmp(name, "dump") == 0)
		shsec_dump(req);
	else
		gctl_error(req, "Unknown command: %s.", name);
}

static void
shsec_label(struct gctl_req *req)
{
	struct g_shsec_metadata md;
	off_t compsize, msize;
	u_char sector[512];
	unsigned ssize, secsize;
	const char *name;
	int error, i, nargs, hardcode;

	bzero(sector, sizeof(sector));
	nargs = gctl_get_int(req, "nargs");
	if (nargs <= 2) {
		gctl_error(req, "Too few arguments.");
		return;
	}
	hardcode = gctl_get_int(req, "hardcode");

	/*
	 * Clear last sector first to spoil all components if device exists.
	 */
	compsize = 0;
	secsize = 0;
	for (i = 1; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		msize = g_get_mediasize(name);
		ssize = g_get_sectorsize(name);
		if (msize == 0 || ssize == 0) {
			gctl_error(req, "Can't get informations about %s: %s.",
			    name, strerror(errno));
			return;
		}
		msize -= ssize;
		if (compsize == 0 || (compsize > 0 && msize < compsize))
			compsize = msize;
		if (secsize == 0)
			secsize = ssize;
		else
			secsize = g_lcm(secsize, ssize);

		error = g_metadata_clear(name, NULL);
		if (error != 0) {
			gctl_error(req, "Can't store metadata on %s: %s.", name,
			    strerror(error));
			return;
		}
	}

	strlcpy(md.md_magic, G_SHSEC_MAGIC, sizeof(md.md_magic));
	md.md_version = G_SHSEC_VERSION;
	name = gctl_get_ascii(req, "arg0");
	strlcpy(md.md_name, name, sizeof(md.md_name));
	md.md_id = arc4random();
	md.md_all = nargs - 1;

	/*
	 * Ok, store metadata.
	 */
	for (i = 1; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		msize = g_get_mediasize(name);
		ssize = g_get_sectorsize(name);
		if (compsize < msize - ssize) {
			fprintf(stderr,
			    "warning: %s: only %jd bytes from %jd bytes used.\n",
			    name, (intmax_t)compsize, (intmax_t)(msize - ssize));
		}

		md.md_no = i - 1;
		md.md_provsize = msize;
		if (!hardcode)
			bzero(md.md_provider, sizeof(md.md_provider));
		else {
			if (strncmp(name, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
				name += sizeof(_PATH_DEV) - 1;
			strlcpy(md.md_provider, name, sizeof(md.md_provider));
		}
		shsec_metadata_encode(&md, sector);
		error = g_metadata_store(name, sector, sizeof(sector));
		if (error != 0) {
			fprintf(stderr, "Can't store metadata on %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		if (verbose)
			printf("Metadata value stored on %s.\n", name);
	}
}

static void
shsec_clear(struct gctl_req *req)
{
	const char *name;
	int error, i, nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	for (i = 0; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		error = g_metadata_clear(name, G_SHSEC_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Can't clear metadata on %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		if (verbose)
			printf("Metadata cleared on %s.\n", name);
	}
}

static void
shsec_metadata_dump(const struct g_shsec_metadata *md)
{

	printf("         Magic string: %s\n", md->md_magic);
	printf("     Metadata version: %u\n", (u_int)md->md_version);
	printf("          Device name: %s\n", md->md_name);
	printf("            Device ID: %u\n", (u_int)md->md_id);
	printf("          Disk number: %u\n", (u_int)md->md_no);
	printf("Total number of disks: %u\n", (u_int)md->md_all);
	printf("   Hardcoded provider: %s\n", md->md_provider);
}

static void
shsec_dump(struct gctl_req *req)
{
	struct g_shsec_metadata md, tmpmd;
	const char *name;
	int error, i, nargs;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	for (i = 0; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		error = g_metadata_read(name, (u_char *)&tmpmd, sizeof(tmpmd),
		    G_SHSEC_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Can't read metadata from %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		shsec_metadata_decode((u_char *)&tmpmd, &md);
		printf("Metadata on %s:\n", name);
		shsec_metadata_dump(&md);
		printf("\n");
	}
}
