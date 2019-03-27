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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <libgeom.h>
#include <geom/label/g_label.h>

#include "core/geom.h"
#include "misc/subr.h"

#ifdef STATIC_GEOM_CLASSES
#define PUBSYM(x)	glabel_##x
#else
#define PUBSYM(x)	x
#endif

uint32_t PUBSYM(lib_version) = G_LIB_VERSION;
uint32_t PUBSYM(version) = G_LABEL_VERSION;

static void label_main(struct gctl_req *req, unsigned flags);
static void label_clear(struct gctl_req *req);
static void label_dump(struct gctl_req *req);
static void label_label(struct gctl_req *req);
static void label_refresh(struct gctl_req *req);

struct g_command PUBSYM(class_commands)[] = {
	{ "clear", G_FLAG_VERBOSE, label_main, G_NULL_OPTS,
	    "[-v] dev ..."
	},
	{ "create", G_FLAG_VERBOSE | G_FLAG_LOADKLD, NULL, G_NULL_OPTS,
	    "[-v] name dev"
	},
	{ "destroy", G_FLAG_VERBOSE, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-fv] name ..."
	},
	{ "dump", 0, label_main, G_NULL_OPTS,
	    "dev ..."
	},
	{ "label", G_FLAG_VERBOSE | G_FLAG_LOADKLD, label_main, G_NULL_OPTS,
	    "[-v] name dev"
	},
	{ "refresh", 0, label_main, G_NULL_OPTS,
	    "dev ..."
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
label_main(struct gctl_req *req, unsigned flags)
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
		label_label(req);
	else if (strcmp(name, "clear") == 0)
		label_clear(req);
	else if (strcmp(name, "dump") == 0)
		label_dump(req);
	else if (strcmp(name, "refresh") == 0)
		label_refresh(req);
	else
		gctl_error(req, "Unknown command: %s.", name);
}

static void
label_label(struct gctl_req *req)
{
	struct g_label_metadata md;
	const char *name, *label;
	u_char sector[512];
	int error, nargs;

	bzero(sector, sizeof(sector));
	nargs = gctl_get_int(req, "nargs");
	if (nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}

	/*
	 * Clear last sector first to spoil all components if device exists.
	 */
	name = gctl_get_ascii(req, "arg1");
	error = g_metadata_clear(name, NULL);
	if (error != 0) {
		gctl_error(req, "Can't store metadata on %s: %s.", name,
		    strerror(error));
		return;
	}

	strlcpy(md.md_magic, G_LABEL_MAGIC, sizeof(md.md_magic));
	md.md_version = G_LABEL_VERSION;
	label = gctl_get_ascii(req, "arg0");
	bzero(md.md_label, sizeof(md.md_label));
	strlcpy(md.md_label, label, sizeof(md.md_label));
	md.md_provsize = g_get_mediasize(name);
	if (md.md_provsize == 0) {
		gctl_error(req, "Can't get mediasize of %s: %s.", name,
		    strerror(errno));
		return;
	}

	/*
	 * Ok, store metadata.
	 */
	label_metadata_encode(&md, sector);
	error = g_metadata_store(name, sector, sizeof(sector));
	if (error != 0) {
		fprintf(stderr, "Can't store metadata on %s: %s.\n", name,
		    strerror(error));
		gctl_error(req, "Not done.");
	}
	if (verbose)
		printf("Metadata value stored on %s.\n", name);
}

static void
label_clear(struct gctl_req *req)
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
		error = g_metadata_clear(name, G_LABEL_MAGIC);
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
label_metadata_dump(const struct g_label_metadata *md)
{

	printf("    Magic string: %s\n", md->md_magic);
	printf("Metadata version: %u\n", (u_int)md->md_version);
	printf("           Label: %s\n", md->md_label);
}

static void
label_dump(struct gctl_req *req)
{
	struct g_label_metadata md, tmpmd;
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
		    G_LABEL_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Can't read metadata from %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		label_metadata_decode((u_char *)&tmpmd, &md);
		printf("Metadata on %s:\n", name);
		label_metadata_dump(&md);
		printf("\n");
	}
}

static void
label_refresh(struct gctl_req *req)
{
	const char *name;
	int i, nargs, fd;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	for (i = 0; i < nargs; i++) {
		name = gctl_get_ascii(req, "arg%d", i);
		fd = g_open(name, 1);
		if (fd == -1) {
			printf("Can't refresh metadata from %s: %s.\n",
			    name, strerror(errno));
		} else {
			printf("Metadata from %s refreshed.\n", name);
			(void)g_close(fd);
		}
	}
}
