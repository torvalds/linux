/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Ivan Voras <ivoras@freebsd.org>
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
#include <fcntl.h>
#include <unistd.h>
#include <libgeom.h>
#include <err.h>
#include <assert.h>

#include <core/geom.h>
#include <misc/subr.h>

#include <geom/virstor/g_virstor_md.h>
#include <geom/virstor/g_virstor.h>

uint32_t lib_version = G_LIB_VERSION;
uint32_t version = G_VIRSTOR_VERSION;

#define	GVIRSTOR_CHUNK_SIZE	"4M"
#define	GVIRSTOR_VIR_SIZE	"2T"

#if G_LIB_VERSION == 1
/* Support RELENG_6 */
#define G_TYPE_BOOL G_TYPE_NONE
#endif

/*
 * virstor_main gets called by the geom(8) utility
 */
static void virstor_main(struct gctl_req *req, unsigned flags);

struct g_command class_commands[] = {
	{ "clear", G_FLAG_VERBOSE, virstor_main, G_NULL_OPTS,
	    "[-v] prov ..."
	},
	{ "dump", 0, virstor_main, G_NULL_OPTS,
	    "prov ..."
	},
	{ "label", G_FLAG_VERBOSE | G_FLAG_LOADKLD, virstor_main,
	    {
		{ 'h', "hardcode", NULL, G_TYPE_BOOL},
		{ 'm', "chunk_size", GVIRSTOR_CHUNK_SIZE, G_TYPE_NUMBER},
		{ 's', "vir_size", GVIRSTOR_VIR_SIZE, G_TYPE_NUMBER},
		G_OPT_SENTINEL
	    },
	    "[-h] [-v] [-m chunk_size] [-s vir_size] name provider0 [provider1 ...]"
	},
	{ "destroy", G_FLAG_VERBOSE, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL},
		G_OPT_SENTINEL
	    },
	    "[-fv] name ..."
	},
	{ "stop", G_FLAG_VERBOSE, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL},
		G_OPT_SENTINEL
	    },
	    "[-fv] name ... (alias for \"destroy\")"
	},
	{ "add", G_FLAG_VERBOSE, NULL,
	    {
		{ 'h', "hardcode", NULL, G_TYPE_BOOL},
		G_OPT_SENTINEL
	    },
	    "[-vh] name prov [prov ...]"
	},
	{ "remove", G_FLAG_VERBOSE, NULL, G_NULL_OPTS,
	    "[-v] name ..."
	},
	G_CMD_SENTINEL
};

static int verbose = 0;

/* Helper functions' declarations */
static void virstor_clear(struct gctl_req *req);
static void virstor_dump(struct gctl_req *req);
static void virstor_label(struct gctl_req *req);

/* Dispatcher function (no real work done here, only verbose flag recorder) */
static void
virstor_main(struct gctl_req *req, unsigned flags)
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
		virstor_label(req);
	else if (strcmp(name, "clear") == 0)
		virstor_clear(req);
	else if (strcmp(name, "dump") == 0)
		virstor_dump(req);
	else
		gctl_error(req, "%s: Unknown command: %s.", __func__, name);

	/* No CTASSERT in userland
	CTASSERT(VIRSTOR_MAP_BLOCK_ENTRIES*VIRSTOR_MAP_ENTRY_SIZE == MAXPHYS);
	*/
}

/*
 * Labels a new geom Meaning: parses and checks the parameters, calculates &
 * writes metadata to the relevant providers so when the next round of
 * "tasting" comes (which will be just after the provider(s) are closed) geom
 * can be instantiated with the tasted metadata.
 */
static void
virstor_label(struct gctl_req *req)
{
	struct g_virstor_metadata md;
	off_t msize;
	unsigned char *sect;
	unsigned int i;
	size_t ssize, secsize;
	const char *name;
	char param[32];
	int hardcode, nargs, error;
	struct virstor_map_entry *map;
	size_t total_chunks;	/* We'll run out of memory if
				   this needs to be bigger. */
	unsigned int map_chunks; /* Chunks needed by the map (map size). */
	size_t map_size;	/* In bytes. */
	ssize_t written;
	int fd;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 2) {
		gctl_error(req, "Too few arguments (%d): expecting: name "
		    "provider0 [provider1 ...]", nargs);
		return;
	}

	hardcode = gctl_get_int(req, "hardcode");

	/*
	 * Initialize constant parts of metadata: magic signature, version,
	 * name.
	 */
	bzero(&md, sizeof(md));
	strlcpy(md.md_magic, G_VIRSTOR_MAGIC, sizeof(md.md_magic));
	md.md_version = G_VIRSTOR_VERSION;
	name = gctl_get_ascii(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 0);
		return;
	}
	strlcpy(md.md_name, name, sizeof(md.md_name));

	md.md_virsize = (off_t)gctl_get_intmax(req, "vir_size");
	md.md_chunk_size = gctl_get_intmax(req, "chunk_size");
	md.md_count = nargs - 1;

	if (md.md_virsize == 0 || md.md_chunk_size == 0) {
		gctl_error(req, "Virtual size and chunk size must be non-zero");
		return;
	}

	if (md.md_chunk_size % MAXPHYS != 0) {
		/* XXX: This is not strictly needed, but it's convenient to
		 * impose some limitations on it, so why not MAXPHYS. */
		size_t new_size = rounddown(md.md_chunk_size, MAXPHYS);
		if (new_size < md.md_chunk_size)
			new_size += MAXPHYS;
		fprintf(stderr, "Resizing chunk size to be a multiple of "
		    "MAXPHYS (%d kB).\n", MAXPHYS / 1024);
		fprintf(stderr, "New chunk size: %zu kB\n", new_size / 1024);
		md.md_chunk_size = new_size;
	}

	if (md.md_virsize % md.md_chunk_size != 0) {
		off_t chunk_count = md.md_virsize / md.md_chunk_size;
		md.md_virsize = chunk_count * md.md_chunk_size;
		fprintf(stderr, "Resizing virtual size to be a multiple of "
		    "chunk size.\n");
		fprintf(stderr, "New virtual size: %zu MB\n",
		    (size_t)(md.md_virsize/(1024 * 1024)));
	}

	msize = secsize = 0;
	for (i = 1; i < (unsigned)nargs; i++) {
		snprintf(param, sizeof(param), "arg%u", i);
		name = gctl_get_ascii(req, "%s", param);
		ssize = g_get_sectorsize(name);
		if (ssize == 0)
			fprintf(stderr, "%s for %s\n", strerror(errno), name);
		msize += g_get_mediasize(name);
		if (secsize == 0)
			secsize = ssize;
		else if (secsize != ssize) {
			gctl_error(req, "Devices need to have same sector size "
			    "(%u on %s needs to be %u).",
			    (u_int)ssize, name, (u_int)secsize);
			return;
		}
	}

	if (secsize == 0) {
		gctl_error(req, "Device not specified");
		return;
	}

	if (md.md_chunk_size % secsize != 0) {
		fprintf(stderr, "Error: chunk size is not a multiple of sector "
		    "size.");
		gctl_error(req, "Chunk size (in bytes) must be multiple of %u.",
		    (unsigned int)secsize);
		return;
	}

	total_chunks = md.md_virsize / md.md_chunk_size;
	map_size = total_chunks * sizeof(*map);
	assert(md.md_virsize % md.md_chunk_size == 0);

	ssize = map_size % secsize;
	if (ssize != 0) {
		size_t add_chunks = (secsize - ssize) / sizeof(*map);
		total_chunks += add_chunks;
		md.md_virsize = (off_t)total_chunks * (off_t)md.md_chunk_size;
		map_size = total_chunks * sizeof(*map);
		fprintf(stderr, "Resizing virtual size to fit virstor "
		    "structures.\n");
		fprintf(stderr, "New virtual size: %ju MB (%zu new chunks)\n",
		    (uintmax_t)(md.md_virsize / (1024 * 1024)), add_chunks);
	}

	if (verbose)
		printf("Total virtual chunks: %zu (%zu MB each), %ju MB total "
		    "virtual size.\n",
		    total_chunks, (size_t)(md.md_chunk_size / (1024 * 1024)),
		    md.md_virsize/(1024 * 1024));

	if ((off_t)md.md_virsize < msize)
		fprintf(stderr, "WARNING: Virtual storage size < Physical "
		    "available storage (%ju < %ju)\n", md.md_virsize, msize);

	/* Clear last sector first to spoil all components if device exists. */
	if (verbose)
		printf("Clearing metadata on");

	for (i = 1; i < (unsigned)nargs; i++) {
		snprintf(param, sizeof(param), "arg%u", i);
		name = gctl_get_ascii(req, "%s", param);

		if (verbose)
			printf(" %s", name);

		msize = g_get_mediasize(name);
		ssize = g_get_sectorsize(name);
		if (msize == 0 || ssize == 0) {
			gctl_error(req, "Can't retrieve information about "
			    "%s: %s.", name, strerror(errno));
			return;
		}
		if (msize < (off_t) MAX(md.md_chunk_size*4, map_size))
			gctl_error(req, "Device %s is too small", name);
		error = g_metadata_clear(name, NULL);
		if (error != 0) {
			gctl_error(req, "Can't clear metadata on %s: %s.", name,
			    strerror(error));
			return;
		}
	}


	/* Write allocation table to the first provider - this needs to be done
	 * before metadata is written because when kernel tastes it it's too
	 * late */
	name = gctl_get_ascii(req, "arg1"); /* device with metadata */
	if (verbose)
		printf(".\nWriting allocation table to %s...", name);

	/* How many chunks does the map occupy? */
	map_chunks = map_size/md.md_chunk_size;
	if (map_size % md.md_chunk_size != 0)
		map_chunks++;
	if (verbose) {
		printf(" (%zu MB, %d chunks) ", map_size/(1024*1024), map_chunks);
		fflush(stdout);
	}

	if (strncmp(name, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		fd = open(name, O_RDWR);
	else {
		sprintf(param, "%s%s", _PATH_DEV, name);
		fd = open(param, O_RDWR);
	}
	if (fd < 0)
		gctl_error(req, "Cannot open provider %s to write map", name);

	/* Do it with calloc because there might be a need to set up chunk flags
	 * in the future */
	map = calloc(total_chunks, sizeof(*map));
	if (map == NULL) {
		gctl_error(req,
		    "Out of memory (need %zu bytes for allocation map)",
		    map_size);
	}

	written = pwrite(fd, map, map_size, 0);
	free(map);
	if ((size_t)written != map_size) {
		if (verbose) {
			fprintf(stderr, "\nTried to write %zu, written %zd (%s)\n",
			    map_size, written, strerror(errno));
		}
		gctl_error(req, "Error writing out allocation map!");
		return;
	}
	close (fd);

	if (verbose)
		printf("\nStoring metadata on ");

	/*
	 * ID is randomly generated, unique for a geom. This is used to
	 * recognize all providers belonging to one geom.
	 */
	md.md_id = arc4random();

	/* Ok, store metadata. */
	for (i = 1; i < (unsigned)nargs; i++) {
		snprintf(param, sizeof(param), "arg%u", i);
		name = gctl_get_ascii(req, "%s", param);

		msize = g_get_mediasize(name);
		ssize = g_get_sectorsize(name);

		if (verbose)
			printf("%s ", name);

		/* this provider's position/type in geom */
		md.no = i - 1;
		/* this provider's size */
		md.provsize = msize;
		/* chunk allocation info */
		md.chunk_count = md.provsize / md.md_chunk_size;
		if (verbose)
			printf("(%u chunks) ", md.chunk_count);
		/* Check to make sure last sector is unused */
		if ((off_t)(md.chunk_count * md.md_chunk_size) > (off_t)(msize-ssize))
		    md.chunk_count--;
		md.chunk_next = 0;
		if (i != 1) {
			md.chunk_reserved = 0;
			md.flags = 0;
		} else {
			md.chunk_reserved = map_chunks * 2;
			md.flags = VIRSTOR_PROVIDER_ALLOCATED |
			    VIRSTOR_PROVIDER_CURRENT;
			md.chunk_next = md.chunk_reserved;
			if (verbose)
				printf("(%u reserved) ", md.chunk_reserved);
		}

		if (!hardcode)
			bzero(md.provider, sizeof(md.provider));
		else {
			/* convert "/dev/something" to "something" */
			if (strncmp(name, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0) {
				strlcpy(md.provider, name + sizeof(_PATH_DEV) - 1,
				    sizeof(md.provider));
			} else
				strlcpy(md.provider, name, sizeof(md.provider));
		}
		sect = malloc(ssize);
		if (sect == NULL)
			err(1, "Cannot allocate sector of %zu bytes", ssize);
		bzero(sect, ssize);
		virstor_metadata_encode(&md, sect);
		error = g_metadata_store(name, sect, ssize);
		free(sect);
		if (error != 0) {
			if (verbose)
				printf("\n");
			fprintf(stderr, "Can't store metadata on %s: %s.\n",
			    name, strerror(error));
			gctl_error(req,
			    "Not fully done (error storing metadata).");
			return;
		}
	}
#if 0
	if (verbose)
		printf("\n");
#endif
}

/* Clears metadata on given provider(s) IF it's owned by us */
static void
virstor_clear(struct gctl_req *req)
{
	const char *name;
	char param[32];
	unsigned i;
	int nargs, error;
	int fd;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1) {
		gctl_error(req, "Too few arguments.");
		return;
	}
	for (i = 0; i < (unsigned)nargs; i++) {
		snprintf(param, sizeof(param), "arg%u", i);
		name = gctl_get_ascii(req, "%s", param);

		error = g_metadata_clear(name, G_VIRSTOR_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Can't clear metadata on %s: %s "
			    "(do I own it?)\n", name, strerror(error));
			gctl_error(req,
			    "Not fully done (can't clear metadata).");
			continue;
		}
		if (strncmp(name, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
			fd = open(name, O_RDWR);
		else {
			sprintf(param, "%s%s", _PATH_DEV, name);
			fd = open(param, O_RDWR);
		}
		if (fd < 0) {
			gctl_error(req, "Cannot clear header sector for %s",
			    name);
			continue;
		}
		if (verbose)
			printf("Metadata cleared on %s.\n", name);
	}
}

/* Print some metadata information */
static void
virstor_metadata_dump(const struct g_virstor_metadata *md)
{
	printf("          Magic string: %s\n", md->md_magic);
	printf("      Metadata version: %u\n", (u_int) md->md_version);
	printf("           Device name: %s\n", md->md_name);
	printf("             Device ID: %u\n", (u_int) md->md_id);
	printf("        Provider index: %u\n", (u_int) md->no);
	printf("      Active providers: %u\n", (u_int) md->md_count);
	printf("    Hardcoded provider: %s\n",
	    md->provider[0] != '\0' ? md->provider : "(not hardcoded)");
	printf("          Virtual size: %u MB\n",
	    (unsigned int)(md->md_virsize/(1024 * 1024)));
	printf("            Chunk size: %u kB\n", md->md_chunk_size / 1024);
	printf("    Chunks on provider: %u\n", md->chunk_count);
	printf("           Chunks free: %u\n", md->chunk_count - md->chunk_next);
	printf("       Reserved chunks: %u\n", md->chunk_reserved);
}

/* Called by geom(8) via gvirstor_main() to dump metadata information */
static void
virstor_dump(struct gctl_req *req)
{
	struct g_virstor_metadata md;
	u_char tmpmd[512];	/* temporary buffer */
	const char *name;
	char param[16];
	int nargs, error, i;

	assert(sizeof(tmpmd) >= sizeof(md));

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1) {
		gctl_error(req, "Too few arguments.");
		return;
	}
	for (i = 0; i < nargs; i++) {
		snprintf(param, sizeof(param), "arg%u", i);
		name = gctl_get_ascii(req, "%s", param);

		error = g_metadata_read(name, (u_char *) & tmpmd, sizeof(tmpmd),
		    G_VIRSTOR_MAGIC);
		if (error != 0) {
			fprintf(stderr, "Can't read metadata from %s: %s.\n",
			    name, strerror(error));
			gctl_error(req,
			    "Not fully done (error reading metadata).");
			continue;
		}
		virstor_metadata_decode((u_char *) & tmpmd, &md);
		printf("Metadata on %s:\n", name);
		virstor_metadata_dump(&md);
		printf("\n");
	}
}
