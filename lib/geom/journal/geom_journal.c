/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#include <sys/types.h>
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <libgeom.h>
#include <geom/journal/g_journal.h>
#include <core/geom.h>
#include <misc/subr.h>

#include "geom_journal.h"


uint32_t lib_version = G_LIB_VERSION;
uint32_t version = G_JOURNAL_VERSION;

static void journal_main(struct gctl_req *req, unsigned flags);
static void journal_clear(struct gctl_req *req);
static void journal_dump(struct gctl_req *req);
static void journal_label(struct gctl_req *req);

struct g_command class_commands[] = {
	{ "clear", G_FLAG_VERBOSE, journal_main, G_NULL_OPTS,
	    "[-v] prov ..."
	},
	{ "dump", 0, journal_main, G_NULL_OPTS,
	    "prov ..."
	},
	{ "label", G_FLAG_VERBOSE, journal_main,
	    {
		{ 'c', "checksum", NULL, G_TYPE_BOOL },
		{ 'f', "force", NULL, G_TYPE_BOOL },
		{ 'h', "hardcode", NULL, G_TYPE_BOOL },
		{ 's', "jsize", "-1", G_TYPE_NUMBER },
		G_OPT_SENTINEL
	    },
	    "[-cfhv] [-s jsize] dataprov [jprov]"
	},
	{ "stop", G_FLAG_VERBOSE, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    "[-fv] name ..."
	},
	{ "sync", G_FLAG_VERBOSE, NULL, G_NULL_OPTS,
	    "[-v]"
	},
	G_CMD_SENTINEL
};

static int verbose = 0;

static void
journal_main(struct gctl_req *req, unsigned flags)
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
		journal_label(req);
	else if (strcmp(name, "clear") == 0)
		journal_clear(req);
	else if (strcmp(name, "dump") == 0)
		journal_dump(req);
	else
		gctl_error(req, "Unknown command: %s.", name);
}

static int
g_journal_fs_exists(const char *prov)
{

	if (g_journal_ufs_exists(prov))
		return (1);
#if 0
	if (g_journal_otherfs_exists(prov))
		return (1);
#endif
	return (0);
}

static int
g_journal_fs_using_last_sector(const char *prov)
{

	if (g_journal_ufs_using_last_sector(prov))
		return (1);
#if 0
	if (g_journal_otherfs_using_last_sector(prov))
		return (1);
#endif
	return (0);
}

static void
journal_label(struct gctl_req *req)
{
	struct g_journal_metadata md;
	const char *data, *journal, *str;
	u_char sector[512];
	intmax_t jsize, msize, ssize;
	int error, force, i, nargs, checksum, hardcode;

	bzero(sector, sizeof(sector));
	nargs = gctl_get_int(req, "nargs");
	str = NULL;	/* gcc */

	strlcpy(md.md_magic, G_JOURNAL_MAGIC, sizeof(md.md_magic));
	md.md_version = G_JOURNAL_VERSION;
	md.md_id = arc4random();
	md.md_joffset = 0;
	md.md_jid = 0;
	md.md_flags = GJ_FLAG_CLEAN;
	checksum = gctl_get_int(req, "checksum");
	if (checksum)
		md.md_flags |= GJ_FLAG_CHECKSUM;
	force = gctl_get_int(req, "force");
	hardcode = gctl_get_int(req, "hardcode");

	if (nargs != 1 && nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}

	/* Verify the given providers. */
	for (i = 0; i < nargs; i++) {
		str = gctl_get_ascii(req, "arg%d", i);
		if (g_get_mediasize(str) == 0) {
			gctl_error(req, "Invalid provider %s.", str);
			return;
		}
	}

	data = gctl_get_ascii(req, "arg0");
	jsize = gctl_get_intmax(req, "jsize");
	journal = NULL;
	switch (nargs) {
	case 1:
		if (!force && g_journal_fs_exists(data)) {
			gctl_error(req, "File system exists on %s and this "
			    "operation would destroy it.\nUse -f if you "
			    "really want to do it.", data);
			return;
		}
		journal = data;
		msize = g_get_mediasize(data);
		ssize = g_get_sectorsize(data);
		if (jsize == -1) {
			/*
			 * No journal size specified. 1GB should be safe
			 * default.
			 */
			jsize = 1073741824ULL;
		} else {
			if (jsize < 104857600) {
				gctl_error(req, "Journal too small.");
				return;
			}
			if ((jsize % ssize) != 0) {
				gctl_error(req, "Invalid journal size.");
				return;
			}
		}
		if (jsize + ssize >= msize) {
			gctl_error(req, "Provider too small for journalling. "
			    "You can try smaller jsize (default is %jd).",
			    jsize);
			return;
		}
		md.md_jstart = msize - ssize - jsize;
		md.md_jend = msize - ssize;
		break;
	case 2:
		if (!force && g_journal_fs_using_last_sector(data)) {
			gctl_error(req, "File system on %s is using the last "
			    "sector and this operation is going to overwrite "
			    "it. Use -f if you really want to do it.", data);
			return;
		}
		journal = gctl_get_ascii(req, "arg1");
		if (jsize != -1) {
			gctl_error(req, "jsize argument is valid only for "
			    "all-in-one configuration.");
			return;
		}
		msize = g_get_mediasize(journal);
		ssize = g_get_sectorsize(journal);
		md.md_jstart = 0;
		md.md_jend = msize - ssize;
		break;
	}

	if (g_get_sectorsize(data) != g_get_sectorsize(journal)) {
		gctl_error(req, "Not equal sector sizes.");
		return;
	}

	/*
	 * Clear last sector first, to spoil all components if device exists.
	 */
	for (i = 0; i < nargs; i++) {
		str = gctl_get_ascii(req, "arg%d", i);
		error = g_metadata_clear(str, NULL);
		if (error != 0) {
			gctl_error(req, "Cannot clear metadata on %s: %s.", str,
			    strerror(error));
			return;
		}
	}

	/*
	 * Ok, store metadata.
	 */
	for (i = 0; i < nargs; i++) {
		switch (i) {
		case 0:
			str = data;
			md.md_type = GJ_TYPE_DATA;
			if (nargs == 1)
				md.md_type |= GJ_TYPE_JOURNAL;
			break;
		case 1:
			str = journal;
			md.md_type = GJ_TYPE_JOURNAL;
			break;
		}
		md.md_provsize = g_get_mediasize(str);
		assert(md.md_provsize != 0);
		if (!hardcode)
			bzero(md.md_provider, sizeof(md.md_provider));
		else {
			if (strncmp(str, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
				str += sizeof(_PATH_DEV) - 1;
			strlcpy(md.md_provider, str, sizeof(md.md_provider));
		}
		journal_metadata_encode(&md, sector);
		error = g_metadata_store(str, sector, sizeof(sector));
		if (error != 0) {
			fprintf(stderr, "Cannot store metadata on %s: %s.\n",
			    str, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		if (verbose)
			printf("Metadata value stored on %s.\n", str);
	}
}

static void
journal_clear(struct gctl_req *req)
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
		error = g_metadata_clear(name, G_JOURNAL_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Cannot clear metadata on %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		if (verbose)
			printf("Metadata cleared on %s.\n", name);
	}
}

static void
journal_dump(struct gctl_req *req)
{
	struct g_journal_metadata md, tmpmd;
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
		    G_JOURNAL_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Cannot read metadata from %s: %s.\n",
			    name, strerror(error));
			gctl_error(req, "Not fully done.");
			continue;
		}
		if (journal_metadata_decode((u_char *)&tmpmd, &md) != 0) {
			fprintf(stderr, "MD5 hash mismatch for %s, skipping.\n",
			    name);
			gctl_error(req, "Not fully done.");
			continue;
		}
		printf("Metadata on %s:\n", name);
		journal_metadata_dump(&md);
		printf("\n");
	}
}
