/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Ruslan Ermilov <ru@FreeBSD.org>
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

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <libgeom.h>
#include <geom/cache/g_cache.h>

#include "core/geom.h"
#include "misc/subr.h"


uint32_t lib_version = G_LIB_VERSION;
uint32_t version = G_CACHE_VERSION;

#define	GCACHE_BLOCKSIZE	"65536"
#define	GCACHE_SIZE		"100"

static void cache_main(struct gctl_req *req, unsigned flags);
static void cache_clear(struct gctl_req *req);
static void cache_dump(struct gctl_req *req);
static void cache_label(struct gctl_req *req);

struct g_command class_commands[] = {
	{ "clear", G_FLAG_VERBOSE, cache_main, G_NULL_OPTS,
	    "[-v] prov ..."
	},
	{ "configure", G_FLAG_VERBOSE, NULL,
	    {
		{ 'b', "blocksize", "0", G_TYPE_NUMBER },
		{ 's', "size", "0", G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "[-v] [-b blocksize] [-s size] name"
	},
	{ "create", G_FLAG_VERBOSE | G_FLAG_LOADKLD, NULL,
	    {
		{ 'b', "blocksize", GCACHE_BLOCKSIZE, G_TYPE_NUMBER },
		{ 's', "size", GCACHE_SIZE, G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "[-v] [-b blocksize] [-s size] name prov"
	},
	{ "destroy", G_FLAG_VERBOSE, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-fv] name ..."
	},
	{ "dump", 0, cache_main, G_NULL_OPTS,
	    "prov ..."
	},
	{ "label", G_FLAG_VERBOSE | G_FLAG_LOADKLD, cache_main,
	    {
		{ 'b', "blocksize", GCACHE_BLOCKSIZE, G_TYPE_NUMBER },
		{ 's', "size", GCACHE_SIZE, G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "[-v] [-b blocksize] [-s size] name prov"
	},
	{ "reset", G_FLAG_VERBOSE, NULL, G_NULL_OPTS,
	    "[-v] name ..."
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
cache_main(struct gctl_req *req, unsigned flags)
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
		cache_label(req);
	else if (strcmp(name, "clear") == 0)
		cache_clear(req);
	else if (strcmp(name, "dump") == 0)
		cache_dump(req);
	else
		gctl_error(req, "Unknown command: %s.", name);
}

static void
cache_label(struct gctl_req *req)
{
	struct g_cache_metadata md;
	u_char sector[512];
	const char *name;
	int error, nargs;
	intmax_t val;

	bzero(sector, sizeof(sector));
	nargs = gctl_get_int(req, "nargs");
	if (nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}

	strlcpy(md.md_magic, G_CACHE_MAGIC, sizeof(md.md_magic));
	md.md_version = G_CACHE_VERSION;
	name = gctl_get_ascii(req, "arg0");
	strlcpy(md.md_name, name, sizeof(md.md_name));
	val = gctl_get_intmax(req, "blocksize");
	md.md_bsize = val;
	val = gctl_get_intmax(req, "size");
	md.md_size = val;

	name = gctl_get_ascii(req, "arg1");
	md.md_provsize = g_get_mediasize(name);
	if (md.md_provsize == 0) {
		fprintf(stderr, "Can't get mediasize of %s: %s.\n",
		    name, strerror(errno));
		gctl_error(req, "Not fully done.");
		return;
	}
	cache_metadata_encode(&md, sector);
	error = g_metadata_store(name, sector, sizeof(sector));
	if (error != 0) {
		fprintf(stderr, "Can't store metadata on %s: %s.\n",
		    name, strerror(error));
		gctl_error(req, "Not fully done.");
		return;
	}
	if (verbose)
		printf("Metadata value stored on %s.\n", name);
}

static void
cache_clear(struct gctl_req *req)
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
		error = g_metadata_clear(name, G_CACHE_MAGIC);
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
cache_metadata_dump(const struct g_cache_metadata *md)
{

	printf("         Magic string: %s\n", md->md_magic);
	printf("     Metadata version: %u\n", (u_int)md->md_version);
	printf("          Device name: %s\n", md->md_name);
	printf("           Block size: %u\n", (u_int)md->md_bsize);
	printf("           Cache size: %u\n", (u_int)md->md_size);
	printf("        Provider size: %ju\n", (uintmax_t)md->md_provsize);
}

static void
cache_dump(struct gctl_req *req)
{
	struct g_cache_metadata md, tmpmd;
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
		    G_CACHE_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Can't read metadata from %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		cache_metadata_decode((u_char *)&tmpmd, &md);
		printf("Metadata on %s:\n", name);
		cache_metadata_dump(&md);
		printf("\n");
	}
}
