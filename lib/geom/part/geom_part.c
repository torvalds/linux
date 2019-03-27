/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007, 2008 Marcel Moolenaar
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

#include <sys/stat.h>
#include <sys/vtoc.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgeom.h>
#include <libutil.h>
#include <paths.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "core/geom.h"
#include "misc/subr.h"

#ifdef STATIC_GEOM_CLASSES
#define	PUBSYM(x)	gpart_##x
#else
#define	PUBSYM(x)	x
#endif

uint32_t PUBSYM(lib_version) = G_LIB_VERSION;
uint32_t PUBSYM(version) = 0;

static char sstart[32];
static char ssize[32];
volatile sig_atomic_t undo_restore;

#define	GPART_AUTOFILL	"*"
#define	GPART_FLAGS	"C"

#define	GPART_PARAM_BOOTCODE	"bootcode"
#define	GPART_PARAM_INDEX	"index"
#define	GPART_PARAM_PARTCODE	"partcode"
#define	GPART_PARAM_SKIP_DSN	"skip_dsn"

static struct gclass *find_class(struct gmesh *, const char *);
static struct ggeom * find_geom(struct gclass *, const char *);
static int geom_is_withered(struct ggeom *);
static const char *find_geomcfg(struct ggeom *, const char *);
static const char *find_provcfg(struct gprovider *, const char *);
static struct gprovider *find_provider(struct ggeom *, off_t);
static const char *fmtsize(int64_t);
static int gpart_autofill(struct gctl_req *);
static int gpart_autofill_resize(struct gctl_req *);
static void gpart_bootcode(struct gctl_req *, unsigned int);
static void *gpart_bootfile_read(const char *, ssize_t *);
static _Noreturn void gpart_issue(struct gctl_req *, unsigned int);
static void gpart_show(struct gctl_req *, unsigned int);
static void gpart_show_geom(struct ggeom *, const char *, int);
static int gpart_show_hasopt(struct gctl_req *, const char *, const char *);
static void gpart_write_partcode(struct ggeom *, int, void *, ssize_t);
static void gpart_write_partcode_vtoc8(struct ggeom *, int, void *);
static void gpart_print_error(const char *);
static void gpart_backup(struct gctl_req *, unsigned int);
static void gpart_restore(struct gctl_req *, unsigned int);

struct g_command PUBSYM(class_commands)[] = {
	{ "add", 0, gpart_issue, {
		{ 'a', "alignment", GPART_AUTOFILL, G_TYPE_STRING },
		{ 'b', "start", GPART_AUTOFILL, G_TYPE_STRING },
		{ 's', "size", GPART_AUTOFILL, G_TYPE_STRING },
		{ 't', "type", NULL, G_TYPE_STRING },
		{ 'i', GPART_PARAM_INDEX, G_VAL_OPTIONAL, G_TYPE_NUMBER },
		{ 'l', "label", G_VAL_OPTIONAL, G_TYPE_STRING },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "-t type [-a alignment] [-b start] [-s size] [-i index] "
		"[-l label] [-f flags] geom"
	},
	{ "backup", 0, gpart_backup, G_NULL_OPTS,
	    "geom"
	},
	{ "bootcode", 0, gpart_bootcode, {
		{ 'b', GPART_PARAM_BOOTCODE, G_VAL_OPTIONAL, G_TYPE_STRING },
		{ 'p', GPART_PARAM_PARTCODE, G_VAL_OPTIONAL, G_TYPE_STRING },
		{ 'i', GPART_PARAM_INDEX, G_VAL_OPTIONAL, G_TYPE_NUMBER },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		{ 'N', GPART_PARAM_SKIP_DSN, NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL },
	    "[-N] [-b bootcode] [-p partcode -i index] [-f flags] geom"
	},
	{ "commit", 0, gpart_issue, G_NULL_OPTS,
	    "geom"
	},
	{ "create", 0, gpart_issue, {
		{ 's', "scheme", NULL, G_TYPE_STRING },
		{ 'n', "entries", G_VAL_OPTIONAL, G_TYPE_NUMBER },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "-s scheme [-n entries] [-f flags] provider"
	},
	{ "delete", 0, gpart_issue, {
		{ 'i', GPART_PARAM_INDEX, NULL, G_TYPE_NUMBER },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "-i index [-f flags] geom"
	},
	{ "destroy", 0, gpart_issue, {
		{ 'F', "force", NULL, G_TYPE_BOOL },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "[-F] [-f flags] geom"
	},
	{ "modify", 0, gpart_issue, {
		{ 'i', GPART_PARAM_INDEX, NULL, G_TYPE_NUMBER },
		{ 'l', "label", G_VAL_OPTIONAL, G_TYPE_STRING },
		{ 't', "type", G_VAL_OPTIONAL, G_TYPE_STRING },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "-i index [-l label] [-t type] [-f flags] geom"
	},
	{ "set", 0, gpart_issue, {
		{ 'a', "attrib", NULL, G_TYPE_STRING },
		{ 'i', GPART_PARAM_INDEX, G_VAL_OPTIONAL, G_TYPE_NUMBER },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "-a attrib [-i index] [-f flags] geom"
	},
	{ "show", 0, gpart_show, {
		{ 'l', "show_label", NULL, G_TYPE_BOOL },
		{ 'r', "show_rawtype", NULL, G_TYPE_BOOL },
		{ 'p', "show_providers", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL },
	    "[-l | -r] [-p] [geom ...]"
	},
	{ "undo", 0, gpart_issue, G_NULL_OPTS,
	    "geom"
	},
	{ "unset", 0, gpart_issue, {
		{ 'a', "attrib", NULL, G_TYPE_STRING },
		{ 'i', GPART_PARAM_INDEX, G_VAL_OPTIONAL, G_TYPE_NUMBER },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "-a attrib [-i index] [-f flags] geom"
	},
	{ "resize", 0, gpart_issue, {
		{ 'a', "alignment", GPART_AUTOFILL, G_TYPE_STRING },
		{ 's', "size", GPART_AUTOFILL, G_TYPE_STRING },
		{ 'i', GPART_PARAM_INDEX, NULL, G_TYPE_NUMBER },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "-i index [-a alignment] [-s size] [-f flags] geom"
	},
	{ "restore", 0, gpart_restore, {
		{ 'F', "force", NULL, G_TYPE_BOOL },
		{ 'l', "restore_labels", NULL, G_TYPE_BOOL },
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "[-lF] [-f flags] provider [...]"
	},
	{ "recover", 0, gpart_issue, {
		{ 'f', "flags", GPART_FLAGS, G_TYPE_STRING },
		G_OPT_SENTINEL },
	    "[-f flags] geom"
	},
	G_CMD_SENTINEL
};

static struct gclass *
find_class(struct gmesh *mesh, const char *name)
{
	struct gclass *classp;

	LIST_FOREACH(classp, &mesh->lg_class, lg_class) {
		if (strcmp(classp->lg_name, name) == 0)
			return (classp);
	}
	return (NULL);
}

static struct ggeom *
find_geom(struct gclass *classp, const char *name)
{
	struct ggeom *gp, *wgp;

	if (strncmp(name, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		name += sizeof(_PATH_DEV) - 1;
	wgp = NULL;
	LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
		if (strcmp(gp->lg_name, name) != 0)
			continue;
		if (!geom_is_withered(gp))
			return (gp);
		else
			wgp = gp;
	}
	return (wgp);
}

static int
geom_is_withered(struct ggeom *gp)
{
	struct gconfig *gc;

	LIST_FOREACH(gc, &gp->lg_config, lg_config) {
		if (!strcmp(gc->lg_name, "wither"))
			return (1);
	}
	return (0);
}

static const char *
find_geomcfg(struct ggeom *gp, const char *cfg)
{
	struct gconfig *gc;

	LIST_FOREACH(gc, &gp->lg_config, lg_config) {
		if (!strcmp(gc->lg_name, cfg))
			return (gc->lg_val);
	}
	return (NULL);
}

static const char *
find_provcfg(struct gprovider *pp, const char *cfg)
{
	struct gconfig *gc;

	LIST_FOREACH(gc, &pp->lg_config, lg_config) {
		if (!strcmp(gc->lg_name, cfg))
			return (gc->lg_val);
	}
	return (NULL);
}

static struct gprovider *
find_provider(struct ggeom *gp, off_t minsector)
{
	struct gprovider *pp, *bestpp;
	const char *s;
	off_t sector, bestsector;

	bestpp = NULL;
	bestsector = 0;
	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		s = find_provcfg(pp, "start");
		sector = (off_t)strtoimax(s, NULL, 0);
		if (sector < minsector)
			continue;
		if (bestpp != NULL && sector >= bestsector)
			continue;

		bestpp = pp;
		bestsector = sector;
	}
	return (bestpp);
}

static const char *
fmtsize(int64_t rawsz)
{
	static char buf[5];

	humanize_number(buf, sizeof(buf), rawsz, "", HN_AUTOSCALE,
	    HN_B | HN_NOSPACE | HN_DECIMAL);
	return (buf);
}

static const char *
fmtattrib(struct gprovider *pp)
{
	static char buf[128];
	struct gconfig *gc;
	u_int idx;

	buf[0] = '\0';
	idx = 0;
	LIST_FOREACH(gc, &pp->lg_config, lg_config) {
		if (strcmp(gc->lg_name, "attrib") != 0)
			continue;
		idx += snprintf(buf + idx, sizeof(buf) - idx, "%s%s",
		    (idx == 0) ? " [" : ",", gc->lg_val);
	}
	if (idx > 0)
		snprintf(buf + idx, sizeof(buf) - idx, "] ");
	return (buf);
}

#define	ALIGNDOWN(d, a)	((d) - (d) % (a))
#define	ALIGNUP(d, a)	((d) % (a) ? (d) - (d) % (a) + (a): (d))

static int
gpart_autofill_resize(struct gctl_req *req)
{
	struct gmesh mesh;
	struct gclass *cp;
	struct ggeom *gp;
	struct gprovider *pp;
	off_t last, size, start, new_size;
	off_t lba, new_lba, alignment, offset;
	const char *s;
	int error, idx, has_alignment;

	idx = (int)gctl_get_intmax(req, GPART_PARAM_INDEX);
	if (idx < 1)
		errx(EXIT_FAILURE, "invalid partition index");

	error = geom_gettree(&mesh);
	if (error)
		return (error);
	s = gctl_get_ascii(req, "class");
	if (s == NULL)
		abort();
	cp = find_class(&mesh, s);
	if (cp == NULL)
		errx(EXIT_FAILURE, "Class %s not found.", s);
	s = gctl_get_ascii(req, "arg0");
	if (s == NULL)
		abort();
	gp = find_geom(cp, s);
	if (gp == NULL)
		errx(EXIT_FAILURE, "No such geom: %s.", s);
	pp = LIST_FIRST(&gp->lg_consumer)->lg_provider;
	if (pp == NULL)
		errx(EXIT_FAILURE, "Provider for geom %s not found.", s);

	s = gctl_get_ascii(req, "alignment");
	has_alignment = (*s == '*') ? 0 : 1;
	alignment = 1;
	if (has_alignment) {
		error = g_parse_lba(s, pp->lg_sectorsize, &alignment);
		if (error)
			errc(EXIT_FAILURE, error, "Invalid alignment param");
		if (alignment == 0)
			errx(EXIT_FAILURE, "Invalid alignment param");
	} else {
		lba = pp->lg_stripesize / pp->lg_sectorsize;
		if (lba > 0)
			alignment = lba;
	}
	error = gctl_delete_param(req, "alignment");
	if (error)
		errc(EXIT_FAILURE, error, "internal error");

	s = gctl_get_ascii(req, "size");
	if (*s == '*')
		new_size = 0;
	else {
		error = g_parse_lba(s, pp->lg_sectorsize, &new_size);
		if (error)
			errc(EXIT_FAILURE, error, "Invalid size param");
		/* no autofill necessary. */
		if (has_alignment == 0)
			goto done;
	}

	offset = (pp->lg_stripeoffset / pp->lg_sectorsize) % alignment;
	s = find_geomcfg(gp, "last");
	if (s == NULL)
		errx(EXIT_FAILURE, "Final block not found for geom %s",
		    gp->lg_name);
	last = (off_t)strtoimax(s, NULL, 0);
	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		s = find_provcfg(pp, "index");
		if (s == NULL)
			continue;
		if (atoi(s) == idx)
			break;
	}
	if (pp == NULL)
		errx(EXIT_FAILURE, "invalid partition index");

	s = find_provcfg(pp, "start");
	start = (off_t)strtoimax(s, NULL, 0);
	s = find_provcfg(pp, "end");
	lba = (off_t)strtoimax(s, NULL, 0);
	size = lba - start + 1;

	pp = find_provider(gp, lba + 1);
	if (new_size > 0 && (new_size <= size || pp == NULL)) {
		/* The start offset may be not aligned, so we align the end
		 * offset and then calculate the size.
		 */
		new_size = ALIGNDOWN(start + offset + new_size,
		    alignment) - start - offset;
		goto done;
	}
	if (pp == NULL) {
		new_size = ALIGNDOWN(last + offset + 1, alignment) -
		    start - offset;
		if (new_size < size)
			return (ENOSPC);
	} else {
		s = find_provcfg(pp, "start");
		new_lba = (off_t)strtoimax(s, NULL, 0);
		/*
		 * Is there any free space between current and
		 * next providers?
		 */
		new_lba = ALIGNDOWN(new_lba + offset, alignment) - offset;
		if (new_lba > lba)
			new_size = new_lba - start;
		else {
			geom_deletetree(&mesh);
			return (ENOSPC);
		}
	}
done:
	snprintf(ssize, sizeof(ssize), "%jd", (intmax_t)new_size);
	gctl_change_param(req, "size", -1, ssize);
	geom_deletetree(&mesh);
	return (0);
}

static int
gpart_autofill(struct gctl_req *req)
{
	struct gmesh mesh;
	struct gclass *cp;
	struct ggeom *gp;
	struct gprovider *pp;
	off_t first, last, a_first;
	off_t size, start, a_lba;
	off_t lba, len, alignment, offset;
	uintmax_t grade;
	const char *s;
	int error, has_size, has_start, has_alignment;

	s = gctl_get_ascii(req, "verb");
	if (strcmp(s, "resize") == 0)
		return gpart_autofill_resize(req);
	if (strcmp(s, "add") != 0)
		return (0);

	error = geom_gettree(&mesh);
	if (error)
		return (error);
	s = gctl_get_ascii(req, "class");
	if (s == NULL)
		abort();
	cp = find_class(&mesh, s);
	if (cp == NULL)
		errx(EXIT_FAILURE, "Class %s not found.", s);
	s = gctl_get_ascii(req, "arg0");
	if (s == NULL)
		abort();
	gp = find_geom(cp, s);
	if (gp == NULL) {
		if (g_device_path(s) == NULL) {
			errx(EXIT_FAILURE, "No such geom %s.", s);
		} else {
			/*
			 * We don't free memory allocated by g_device_path() as
			 * we are about to exit.
			 */
			errx(EXIT_FAILURE,
			    "No partitioning scheme found on geom %s. Create one first using 'gpart create'.",
			    s);
		}
	}
	pp = LIST_FIRST(&gp->lg_consumer)->lg_provider;
	if (pp == NULL)
		errx(EXIT_FAILURE, "Provider for geom %s not found.", s);

	s = gctl_get_ascii(req, "alignment");
	has_alignment = (*s == '*') ? 0 : 1;
	alignment = 1;
	if (has_alignment) {
		error = g_parse_lba(s, pp->lg_sectorsize, &alignment);
		if (error)
			errc(EXIT_FAILURE, error, "Invalid alignment param");
		if (alignment == 0)
			errx(EXIT_FAILURE, "Invalid alignment param");
	}
	error = gctl_delete_param(req, "alignment");
	if (error)
		errc(EXIT_FAILURE, error, "internal error");

	s = gctl_get_ascii(req, "size");
	has_size = (*s == '*') ? 0 : 1;
	size = 0;
	if (has_size) {
		error = g_parse_lba(s, pp->lg_sectorsize, &size);
		if (error)
			errc(EXIT_FAILURE, error, "Invalid size param");
	}

	s = gctl_get_ascii(req, "start");
	has_start = (*s == '*') ? 0 : 1;
	start = 0ULL;
	if (has_start) {
		error = g_parse_lba(s, pp->lg_sectorsize, &start);
		if (error)
			errc(EXIT_FAILURE, error, "Invalid start param");
	}

	/* No autofill necessary. */
	if (has_size && has_start && !has_alignment)
		goto done;

	len = pp->lg_stripesize / pp->lg_sectorsize;
	if (len > 0 && !has_alignment)
		alignment = len;

	/* Adjust parameters to stripeoffset */
	offset = (pp->lg_stripeoffset / pp->lg_sectorsize) % alignment;
	start = ALIGNUP(start + offset, alignment);
	if (size > alignment)
		size = ALIGNDOWN(size, alignment);

	s = find_geomcfg(gp, "first");
	if (s == NULL)
		errx(EXIT_FAILURE, "Starting block not found for geom %s",
		    gp->lg_name);
	first = (off_t)strtoimax(s, NULL, 0);
	s = find_geomcfg(gp, "last");
	if (s == NULL)
		errx(EXIT_FAILURE, "Final block not found for geom %s",
		    gp->lg_name);
	last = (off_t)strtoimax(s, NULL, 0);
	grade = ~0ULL;
	a_first = ALIGNUP(first + offset, alignment);
	last = ALIGNDOWN(last + offset + 1, alignment) - 1;
	if (a_first < start)
		a_first = start;
	while ((pp = find_provider(gp, first)) != NULL) {
		s = find_provcfg(pp, "start");
		lba = (off_t)strtoimax(s, NULL, 0);
		a_lba = ALIGNDOWN(lba + offset, alignment);
		if (first < a_lba && a_first < a_lba) {
			/* Free space [first, lba> */
			len = a_lba - a_first;
			if (has_size) {
				if (len >= size &&
				    (uintmax_t)(len - size) < grade) {
					start = a_first;
					grade = len - size;
				}
			} else if (has_start) {
				if (start >= a_first && start < a_lba) {
					size = a_lba - start;
					grade = start - a_first;
				}
			} else {
				if (grade == ~0ULL || len > size) {
					start = a_first;
					size = len;
					grade = 0;
				}
			}
		}

		s = find_provcfg(pp, "end");
		first = (off_t)strtoimax(s, NULL, 0) + 1;
		if (first + offset > a_first)
			a_first = ALIGNUP(first + offset, alignment);
	}
	if (a_first <= last) {
		/* Free space [first-last] */
		len = ALIGNDOWN(last - a_first + 1, alignment);
		if (has_size) {
			if (len >= size &&
			    (uintmax_t)(len - size) < grade) {
				start = a_first;
				grade = len - size;
			}
		} else if (has_start) {
			if (start >= a_first && start <= last) {
				size = ALIGNDOWN(last - start + 1, alignment);
				grade = start - a_first;
			}
		} else {
			if (grade == ~0ULL || len > size) {
				start = a_first;
				size = len;
				grade = 0;
			}
		}
	}
	if (grade == ~0ULL) {
		geom_deletetree(&mesh);
		return (ENOSPC);
	}
	start -= offset;	/* Return back to real offset */
done:
	snprintf(ssize, sizeof(ssize), "%jd", (intmax_t)size);
	gctl_change_param(req, "size", -1, ssize);
	snprintf(sstart, sizeof(sstart), "%jd", (intmax_t)start);
	gctl_change_param(req, "start", -1, sstart);
	geom_deletetree(&mesh);
	return (0);
}

static void
gpart_show_geom(struct ggeom *gp, const char *element, int show_providers)
{
	struct gprovider *pp;
	const char *s, *scheme;
	off_t first, last, sector, end;
	off_t length, secsz;
	int idx, wblocks, wname, wmax;

	if (geom_is_withered(gp))
		return;
	scheme = find_geomcfg(gp, "scheme");
	if (scheme == NULL)
		errx(EXIT_FAILURE, "Scheme not found for geom %s", gp->lg_name);
	s = find_geomcfg(gp, "first");
	if (s == NULL)
		errx(EXIT_FAILURE, "Starting block not found for geom %s",
		    gp->lg_name);
	first = (off_t)strtoimax(s, NULL, 0);
	s = find_geomcfg(gp, "last");
	if (s == NULL)
		errx(EXIT_FAILURE, "Final block not found for geom %s",
		    gp->lg_name);
	last = (off_t)strtoimax(s, NULL, 0);
	wblocks = strlen(s);
	s = find_geomcfg(gp, "state");
	if (s == NULL)
		errx(EXIT_FAILURE, "State not found for geom %s", gp->lg_name);
	if (s != NULL && *s != 'C')
		s = NULL;
	wmax = strlen(gp->lg_name);
	if (show_providers) {
		LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
			wname = strlen(pp->lg_name);
			if (wname > wmax)
				wmax = wname;
		}
	}
	wname = wmax;
	pp = LIST_FIRST(&gp->lg_consumer)->lg_provider;
	secsz = pp->lg_sectorsize;
	printf("=>%*jd  %*jd  %*s  %s  (%s)%s\n",
	    wblocks, (intmax_t)first, wblocks, (intmax_t)(last - first + 1),
	    wname, gp->lg_name,
	    scheme, fmtsize(pp->lg_mediasize),
	    s ? " [CORRUPT]": "");

	while ((pp = find_provider(gp, first)) != NULL) {
		s = find_provcfg(pp, "start");
		sector = (off_t)strtoimax(s, NULL, 0);

		s = find_provcfg(pp, "end");
		end = (off_t)strtoimax(s, NULL, 0);
		length = end - sector + 1;

		s = find_provcfg(pp, "index");
		idx = atoi(s);
		if (first < sector) {
			printf("  %*jd  %*jd  %*s  - free -  (%s)\n",
			    wblocks, (intmax_t)first, wblocks,
			    (intmax_t)(sector - first), wname, "",
			    fmtsize((sector - first) * secsz));
		}
		if (show_providers) {
			printf("  %*jd  %*jd  %*s  %s %s (%s)\n",
			    wblocks, (intmax_t)sector, wblocks,
			    (intmax_t)length, wname, pp->lg_name,
			    find_provcfg(pp, element), fmtattrib(pp),
			    fmtsize(pp->lg_mediasize));
		} else
			printf("  %*jd  %*jd  %*d  %s %s (%s)\n",
			    wblocks, (intmax_t)sector, wblocks,
			    (intmax_t)length, wname, idx,
			    find_provcfg(pp, element), fmtattrib(pp),
			    fmtsize(pp->lg_mediasize));
		first = end + 1;
	}
	if (first <= last) {
		length = last - first + 1;
		printf("  %*jd  %*jd  %*s  - free -  (%s)\n",
		    wblocks, (intmax_t)first, wblocks, (intmax_t)length,
		    wname, "",
		    fmtsize(length * secsz));
	}
	printf("\n");
}

static int
gpart_show_hasopt(struct gctl_req *req, const char *opt, const char *elt)
{

	if (!gctl_get_int(req, "%s", opt))
		return (0);

	if (elt != NULL)
		errx(EXIT_FAILURE, "-l and -r are mutually exclusive");

	return (1);
}

static void
gpart_show(struct gctl_req *req, unsigned int fl __unused)
{
	struct gmesh mesh;
	struct gclass *classp;
	struct ggeom *gp;
	const char *element, *name;
	int error, i, nargs, show_providers;

	element = NULL;
	if (gpart_show_hasopt(req, "show_label", element))
		element = "label";
	if (gpart_show_hasopt(req, "show_rawtype", element))
		element = "rawtype";
	if (element == NULL)
		element = "type";

	name = gctl_get_ascii(req, "class");
	if (name == NULL)
		abort();
	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");
	classp = find_class(&mesh, name);
	if (classp == NULL) {
		geom_deletetree(&mesh);
		errx(EXIT_FAILURE, "Class %s not found.", name);
	}
	show_providers = gctl_get_int(req, "show_providers");
	nargs = gctl_get_int(req, "nargs");
	if (nargs > 0) {
		for (i = 0; i < nargs; i++) {
			name = gctl_get_ascii(req, "arg%d", i);
			gp = find_geom(classp, name);
			if (gp != NULL)
				gpart_show_geom(gp, element, show_providers);
			else
				errx(EXIT_FAILURE, "No such geom: %s.", name);
		}
	} else {
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			gpart_show_geom(gp, element, show_providers);
		}
	}
	geom_deletetree(&mesh);
}

static void
gpart_backup(struct gctl_req *req, unsigned int fl __unused)
{
	struct gmesh mesh;
	struct gclass *classp;
	struct gprovider *pp;
	struct ggeom *gp;
	const char *s, *scheme;
	off_t sector, end;
	off_t length;
	int error, i, windex, wblocks, wtype;

	if (gctl_get_int(req, "nargs") != 1)
		errx(EXIT_FAILURE, "Invalid number of arguments.");
	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");
	s = gctl_get_ascii(req, "class");
	if (s == NULL)
		abort();
	classp = find_class(&mesh, s);
	if (classp == NULL) {
		geom_deletetree(&mesh);
		errx(EXIT_FAILURE, "Class %s not found.", s);
	}
	s = gctl_get_ascii(req, "arg0");
	if (s == NULL)
		abort();
	gp = find_geom(classp, s);
	if (gp == NULL)
		errx(EXIT_FAILURE, "No such geom: %s.", s);
	scheme = find_geomcfg(gp, "scheme");
	if (scheme == NULL)
		abort();
	pp = LIST_FIRST(&gp->lg_consumer)->lg_provider;
	s = find_geomcfg(gp, "last");
	if (s == NULL)
		abort();
	wblocks = strlen(s);
	wtype = 0;
	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		s = find_provcfg(pp, "type");
		i = strlen(s);
		if (i > wtype)
			wtype = i;
	}
	s = find_geomcfg(gp, "entries");
	if (s == NULL)
		abort();
	windex = strlen(s);
	printf("%s %s\n", scheme, s);
	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		s = find_provcfg(pp, "start");
		sector = (off_t)strtoimax(s, NULL, 0);

		s = find_provcfg(pp, "end");
		end = (off_t)strtoimax(s, NULL, 0);
		length = end - sector + 1;

		s = find_provcfg(pp, "label");
		printf("%-*s %*s %*jd %*jd %s %s\n",
		    windex, find_provcfg(pp, "index"),
		    wtype, find_provcfg(pp, "type"),
		    wblocks, (intmax_t)sector,
		    wblocks, (intmax_t)length,
		    (s != NULL) ? s: "", fmtattrib(pp));
	}
	geom_deletetree(&mesh);
}

static int
skip_line(const char *p)
{

	while (*p != '\0') {
		if (*p == '#')
			return (1);
		if (isspace(*p) == 0)
			return (0);
		p++;
	}
	return (1);
}

static void
gpart_sighndl(int sig __unused)
{
	undo_restore = 1;
}

static void
gpart_restore(struct gctl_req *req, unsigned int fl __unused)
{
	struct gmesh mesh;
	struct gclass *classp;
	struct gctl_req *r;
	struct ggeom *gp;
	struct sigaction si_sa;
	const char *s, *flags, *errstr, *label;
	char **ap, *argv[6], line[BUFSIZ], *pline;
	int error, forced, i, l, nargs, created, rl;
	intmax_t n;

	nargs = gctl_get_int(req, "nargs");
	if (nargs < 1)
		errx(EXIT_FAILURE, "Invalid number of arguments.");

	forced = gctl_get_int(req, "force");
	flags = gctl_get_ascii(req, "flags");
	rl = gctl_get_int(req, "restore_labels");
	s = gctl_get_ascii(req, "class");
	if (s == NULL)
		abort();
	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");
	classp = find_class(&mesh, s);
	if (classp == NULL) {
		geom_deletetree(&mesh);
		errx(EXIT_FAILURE, "Class %s not found.", s);
	}

	sigemptyset(&si_sa.sa_mask);
	si_sa.sa_flags = 0;
	si_sa.sa_handler = gpart_sighndl;
	if (sigaction(SIGINT, &si_sa, 0) == -1)
		err(EXIT_FAILURE, "sigaction SIGINT");

	if (forced) {
		/* destroy existent partition table before restore */
		for (i = 0; i < nargs; i++) {
			s = gctl_get_ascii(req, "arg%d", i);
			gp = find_geom(classp, s);
			if (gp != NULL) {
				r = gctl_get_handle();
				gctl_ro_param(r, "class", -1,
				    classp->lg_name);
				gctl_ro_param(r, "verb", -1, "destroy");
				gctl_ro_param(r, "flags", -1, "restore");
				gctl_ro_param(r, "force", sizeof(forced),
				    &forced);
				gctl_ro_param(r, "arg0", -1, s);
				errstr = gctl_issue(r);
				if (errstr != NULL && errstr[0] != '\0') {
					gpart_print_error(errstr);
					gctl_free(r);
					goto backout;
				}
				gctl_free(r);
			}
		}
	}
	created = 0;
	while (undo_restore == 0 &&
	    fgets(line, sizeof(line) - 1, stdin) != NULL) {
		/* Format of backup entries:
		 * <scheme name> <number of entries>
		 * <index> <type> <start> <size> [label] ['['attrib[,attrib]']']
		 */
		pline = (char *)line;
		pline[strlen(line) - 1] = 0;
		if (skip_line(pline))
			continue;
		for (ap = argv;
		    (*ap = strsep(&pline, " \t")) != NULL;)
			if (**ap != '\0' && ++ap >= &argv[6])
				break;
		l = ap - &argv[0];
		label = pline = NULL;
		if (l == 1 || l == 2) { /* create table */
			if (created)
				errx(EXIT_FAILURE, "Incorrect backup format.");
			if (l == 2)
				n = strtoimax(argv[1], NULL, 0);
			for (i = 0; i < nargs; i++) {
				s = gctl_get_ascii(req, "arg%d", i);
				r = gctl_get_handle();
				gctl_ro_param(r, "class", -1,
				    classp->lg_name);
				gctl_ro_param(r, "verb", -1, "create");
				gctl_ro_param(r, "scheme", -1, argv[0]);
				if (l == 2)
					gctl_ro_param(r, "entries",
					    sizeof(n), &n);
				gctl_ro_param(r, "flags", -1, "restore");
				gctl_ro_param(r, "arg0", -1, s);
				errstr = gctl_issue(r);
				if (errstr != NULL && errstr[0] != '\0') {
					gpart_print_error(errstr);
					gctl_free(r);
					goto backout;
				}
				gctl_free(r);
			}
			created = 1;
			continue;
		} else if (l < 4 || created == 0)
			errx(EXIT_FAILURE, "Incorrect backup format.");
		else if (l == 5) {
			if (strchr(argv[4], '[') == NULL)
				label = argv[4];
			else
				pline = argv[4];
		} else if (l == 6) {
			label = argv[4];
			pline = argv[5];
		}
		/* Add partitions to each table */
		for (i = 0; i < nargs; i++) {
			s = gctl_get_ascii(req, "arg%d", i);
			r = gctl_get_handle();
			n = strtoimax(argv[0], NULL, 0);
			gctl_ro_param(r, "class", -1, classp->lg_name);
			gctl_ro_param(r, "verb", -1, "add");
			gctl_ro_param(r, "flags", -1, "restore");
			gctl_ro_param(r, GPART_PARAM_INDEX, sizeof(n), &n);
			gctl_ro_param(r, "type", -1, argv[1]);
			gctl_ro_param(r, "start", -1, argv[2]);
			gctl_ro_param(r, "size", -1, argv[3]);
			if (rl != 0 && label != NULL)
				gctl_ro_param(r, "label", -1, argv[4]);
			gctl_ro_param(r, "alignment", -1, GPART_AUTOFILL);
			gctl_ro_param(r, "arg0", -1, s);
			error = gpart_autofill(r);
			if (error != 0)
				errc(EXIT_FAILURE, error, "autofill");
			errstr = gctl_issue(r);
			if (errstr != NULL && errstr[0] != '\0') {
				gpart_print_error(errstr);
				gctl_free(r);
				goto backout;
			}
			gctl_free(r);
		}
		if (pline == NULL || *pline != '[')
			continue;
		/* set attributes */
		pline++;
		for (ap = argv;
		    (*ap = strsep(&pline, ",]")) != NULL;)
			if (**ap != '\0' && ++ap >= &argv[6])
				break;
		for (i = 0; i < nargs; i++) {
			l = ap - &argv[0];
			s = gctl_get_ascii(req, "arg%d", i);
			while (l > 0) {
				r = gctl_get_handle();
				gctl_ro_param(r, "class", -1, classp->lg_name);
				gctl_ro_param(r, "verb", -1, "set");
				gctl_ro_param(r, "flags", -1, "restore");
				gctl_ro_param(r, GPART_PARAM_INDEX,
				    sizeof(n), &n);
				gctl_ro_param(r, "attrib", -1, argv[--l]);
				gctl_ro_param(r, "arg0", -1, s);
				errstr = gctl_issue(r);
				if (errstr != NULL && errstr[0] != '\0') {
					gpart_print_error(errstr);
					gctl_free(r);
					goto backout;
				}
				gctl_free(r);
			}
		}
	}
	if (undo_restore)
		goto backout;
	/* commit changes if needed */
	if (strchr(flags, 'C') != NULL) {
		for (i = 0; i < nargs; i++) {
			s = gctl_get_ascii(req, "arg%d", i);
			r = gctl_get_handle();
			gctl_ro_param(r, "class", -1, classp->lg_name);
			gctl_ro_param(r, "verb", -1, "commit");
			gctl_ro_param(r, "arg0", -1, s);
			errstr = gctl_issue(r);
			if (errstr != NULL && errstr[0] != '\0') {
				gpart_print_error(errstr);
				gctl_free(r);
				goto backout;
			}
			gctl_free(r);
		}
	}
	gctl_free(req);
	geom_deletetree(&mesh);
	exit(EXIT_SUCCESS);

backout:
	for (i = 0; i < nargs; i++) {
		s = gctl_get_ascii(req, "arg%d", i);
		r = gctl_get_handle();
		gctl_ro_param(r, "class", -1, classp->lg_name);
		gctl_ro_param(r, "verb", -1, "undo");
		gctl_ro_param(r, "arg0", -1, s);
		gctl_issue(r);
		gctl_free(r);
	}
	gctl_free(req);
	geom_deletetree(&mesh);
	exit(EXIT_FAILURE);
}

static void *
gpart_bootfile_read(const char *bootfile, ssize_t *size)
{
	struct stat sb;
	void *code;
	int fd;

	if (stat(bootfile, &sb) == -1)
		err(EXIT_FAILURE, "%s", bootfile);
	if (!S_ISREG(sb.st_mode))
		errx(EXIT_FAILURE, "%s: not a regular file", bootfile);
	if (sb.st_size == 0)
		errx(EXIT_FAILURE, "%s: empty file", bootfile);
	if (*size > 0 && sb.st_size > *size)
		errx(EXIT_FAILURE, "%s: file too big (%zu limit)", bootfile,
		    *size);

	*size = sb.st_size;

	fd = open(bootfile, O_RDONLY);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", bootfile);
	code = malloc(*size);
	if (code == NULL)
		err(EXIT_FAILURE, NULL);
	if (read(fd, code, *size) != *size)
		err(EXIT_FAILURE, "%s", bootfile);
	close(fd);

	return (code);
}

static void
gpart_write_partcode(struct ggeom *gp, int idx, void *code, ssize_t size)
{
	char dsf[128];
	struct gprovider *pp;
	const char *s;
	char *buf;
	off_t bsize;
	int fd;

	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		s = find_provcfg(pp, "index");
		if (s == NULL)
			continue;
		if (atoi(s) == idx)
			break;
	}

	if (pp != NULL) {
		snprintf(dsf, sizeof(dsf), "/dev/%s", pp->lg_name);
		if (pp->lg_mediasize < size)
			errx(EXIT_FAILURE, "%s: not enough space", dsf);
		fd = open(dsf, O_WRONLY);
		if (fd == -1)
			err(EXIT_FAILURE, "%s", dsf);
		/*
		 * When writing to a disk device, the write must be
		 * sector aligned and not write to any partial sectors,
		 * so round up the buffer size to the next sector and zero it.
		 */
		bsize = (size + pp->lg_sectorsize - 1) /
		    pp->lg_sectorsize * pp->lg_sectorsize;
		buf = calloc(1, bsize);
		if (buf == NULL)
			err(EXIT_FAILURE, "%s", dsf);
		bcopy(code, buf, size);
		if (write(fd, buf, bsize) != bsize)
			err(EXIT_FAILURE, "%s", dsf);
		free(buf);
		close(fd);
		printf("partcode written to %s\n", pp->lg_name);
	} else
		errx(EXIT_FAILURE, "invalid partition index");
}

static void
gpart_write_partcode_vtoc8(struct ggeom *gp, int idx, void *code)
{
	char dsf[128];
	struct gprovider *pp;
	const char *s;
	int installed, fd;

	installed = 0;
	LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
		s = find_provcfg(pp, "index");
		if (s == NULL)
			continue;
		if (idx != 0 && atoi(s) != idx)
			continue;
		snprintf(dsf, sizeof(dsf), "/dev/%s", pp->lg_name);
		if (pp->lg_sectorsize != sizeof(struct vtoc8))
			errx(EXIT_FAILURE, "%s: unexpected sector "
			    "size (%d)\n", dsf, pp->lg_sectorsize);
		if (pp->lg_mediasize < VTOC_BOOTSIZE)
			continue;
		fd = open(dsf, O_WRONLY);
		if (fd == -1)
			err(EXIT_FAILURE, "%s", dsf);
		/*
		 * We ignore the first VTOC_BOOTSIZE bytes of boot code in
		 * order to avoid overwriting the label.
		 */
		if (lseek(fd, sizeof(struct vtoc8), SEEK_SET) !=
		    sizeof(struct vtoc8))
			err(EXIT_FAILURE, "%s", dsf);
		if (write(fd, (caddr_t)code + sizeof(struct vtoc8),
		    VTOC_BOOTSIZE - sizeof(struct vtoc8)) != VTOC_BOOTSIZE -
		    sizeof(struct vtoc8))
			err(EXIT_FAILURE, "%s", dsf);
		installed++;
		close(fd);
		if (idx != 0 && atoi(s) == idx)
			break;
	}
	if (installed == 0)
		errx(EXIT_FAILURE, "%s: no partitions", gp->lg_name);
	else
		printf("partcode written to %s\n",
		    idx != 0 ? pp->lg_name: gp->lg_name);
}

static void
gpart_bootcode(struct gctl_req *req, unsigned int fl)
{
	struct gmesh mesh;
	struct gclass *classp;
	struct ggeom *gp;
	const char *s;
	void *bootcode, *partcode;
	size_t bootsize, partsize;
	int error, idx, vtoc8;

	if (gctl_has_param(req, GPART_PARAM_BOOTCODE)) {
		s = gctl_get_ascii(req, GPART_PARAM_BOOTCODE);
		bootsize = 800 * 1024;		/* Arbitrary limit. */
		bootcode = gpart_bootfile_read(s, &bootsize);
		error = gctl_change_param(req, GPART_PARAM_BOOTCODE, bootsize,
		    bootcode);
		if (error)
			errc(EXIT_FAILURE, error, "internal error");
	} else
		bootcode = NULL;

	s = gctl_get_ascii(req, "class");
	if (s == NULL)
		abort();
	error = geom_gettree(&mesh);
	if (error != 0)
		errc(EXIT_FAILURE, error, "Cannot get GEOM tree");
	classp = find_class(&mesh, s);
	if (classp == NULL) {
		geom_deletetree(&mesh);
		errx(EXIT_FAILURE, "Class %s not found.", s);
	}
	if (gctl_get_int(req, "nargs") != 1)
		errx(EXIT_FAILURE, "Invalid number of arguments.");
	s = gctl_get_ascii(req, "arg0");
	if (s == NULL)
		abort();
	gp = find_geom(classp, s);
	if (gp == NULL)
		errx(EXIT_FAILURE, "No such geom: %s.", s);
	s = find_geomcfg(gp, "scheme");
	if (s == NULL)
		errx(EXIT_FAILURE, "Scheme not found for geom %s", gp->lg_name);
	if (strcmp(s, "VTOC8") == 0)
		vtoc8 = 1;
	else
		vtoc8 = 0;

	if (gctl_has_param(req, GPART_PARAM_PARTCODE)) {
		s = gctl_get_ascii(req, GPART_PARAM_PARTCODE);
		if (vtoc8 != 0)
			partsize = VTOC_BOOTSIZE;
		else
			partsize = 1024 * 1024;		/* Arbitrary limit. */
		partcode = gpart_bootfile_read(s, &partsize);
		error = gctl_delete_param(req, GPART_PARAM_PARTCODE);
		if (error)
			errc(EXIT_FAILURE, error, "internal error");
	} else
		partcode = NULL;

	if (gctl_has_param(req, GPART_PARAM_INDEX)) {
		if (partcode == NULL)
			errx(EXIT_FAILURE, "-i is only valid with -p");
		idx = (int)gctl_get_intmax(req, GPART_PARAM_INDEX);
		if (idx < 1)
			errx(EXIT_FAILURE, "invalid partition index");
		error = gctl_delete_param(req, GPART_PARAM_INDEX);
		if (error)
			errc(EXIT_FAILURE, error, "internal error");
	} else
		idx = 0;

	if (partcode != NULL) {
		if (vtoc8 == 0) {
			if (idx == 0)
				errx(EXIT_FAILURE, "missing -i option");
			gpart_write_partcode(gp, idx, partcode, partsize);
		} else {
			if (partsize != VTOC_BOOTSIZE)
				errx(EXIT_FAILURE, "invalid bootcode");
			gpart_write_partcode_vtoc8(gp, idx, partcode);
		}
	} else
		if (bootcode == NULL)
			errx(EXIT_FAILURE, "no -b nor -p");

	if (bootcode != NULL)
		gpart_issue(req, fl);

	geom_deletetree(&mesh);
	free(partcode);
}

static void
gpart_print_error(const char *errstr)
{
	char *errmsg;
	int error;

	error = strtol(errstr, &errmsg, 0);
	if (errmsg != errstr) {
		while (errmsg[0] == ' ')
			errmsg++;
		if (errmsg[0] != '\0')
			warnc(error, "%s", errmsg);
		else
			warnc(error, NULL);
	} else
		warnx("%s", errmsg);
}

static _Noreturn void
gpart_issue(struct gctl_req *req, unsigned int fl __unused)
{
	char buf[4096];
	const char *errstr;
	int error, status;

	if (gctl_get_int(req, "nargs") != 1)
		errx(EXIT_FAILURE, "Invalid number of arguments.");
	(void)gctl_delete_param(req, "nargs");

	/* autofill parameters (if applicable). */
	error = gpart_autofill(req);
	if (error) {
		warnc(error, "autofill");
		status = EXIT_FAILURE;
		goto done;
	}

	bzero(buf, sizeof(buf));
	gctl_rw_param(req, "output", sizeof(buf), buf);
	errstr = gctl_issue(req);
	if (errstr == NULL || errstr[0] == '\0') {
		if (buf[0] != '\0')
			printf("%s", buf);
		status = EXIT_SUCCESS;
		goto done;
	}

	gpart_print_error(errstr);
	status = EXIT_FAILURE;

 done:
	gctl_free(req);
	exit(status);
}
