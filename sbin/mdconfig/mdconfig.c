/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2004 Poul-Henning Kamp <phk@FreeBSD.org>
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Edward Tomasz Napierala
 * under sponsorship from the FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/devicestat.h>
#include <sys/ioctl.h>
#include <sys/linker.h>
#include <sys/mdioctl.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <assert.h>
#include <devstat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgeom.h>
#include <libutil.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct md_ioctl mdio;
static enum {UNSET, ATTACH, DETACH, RESIZE, LIST} action = UNSET;
static int nflag;

static void usage(void);
static void md_set_file(const char *);
static int md_find(const char *, const char *);
static int md_query(const char *, const int, const char *);
static int md_list(const char *, int, const char *);
static char *geom_config_get(struct gconf *g, const char *name);
static void md_prthumanval(char *length);

#define OPT_VERBOSE	0x01
#define OPT_UNIT	0x02
#define OPT_DONE	0x04
#define OPT_LIST	0x10

#define CLASS_NAME_MD	"MD"

static void
usage(void)
{

	fprintf(stderr,
"usage: mdconfig -a -t type [-n] [-o [no]option] ... [-f file]\n"
"                [-s size] [-S sectorsize] [-u unit] [-L label]\n"
"                [-x sectors/track] [-y heads/cylinder]\n"
"       mdconfig -d -u unit [-o [no]force]\n"
"       mdconfig -r -u unit -s size [-o [no]force]\n"
"       mdconfig -l [-v] [-n] [-f file] [-u unit]\n"
"       mdconfig file\n");
	fprintf(stderr, "\t\ttype = {malloc, vnode, swap}\n");
	fprintf(stderr, "\t\toption = {cache, cluster, compress, force,\n");
	fprintf(stderr, "\t\t          readonly, reserve, ro, verify}\n");
	fprintf(stderr, "\t\tsize = %%d (512 byte blocks), %%db (B),\n");
	fprintf(stderr, "\t\t       %%dk (kB), %%dm (MB), %%dg (GB), \n");
	fprintf(stderr, "\t\t       %%dt (TB), or %%dp (PB)\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch, fd, i, vflag;
	char *p;
	char *fflag = NULL, *sflag = NULL, *tflag = NULL, *uflag = NULL;

	bzero(&mdio, sizeof(mdio));
	mdio.md_file = malloc(PATH_MAX);
	mdio.md_label = malloc(PATH_MAX);
	if (mdio.md_file == NULL || mdio.md_label == NULL)
		err(1, "could not allocate memory");
	vflag = 0;
	bzero(mdio.md_file, PATH_MAX);
	bzero(mdio.md_label, PATH_MAX);

	if (argc == 1)
		usage();

	while ((ch = getopt(argc, argv, "ab:df:lno:rs:S:t:u:vx:y:L:")) != -1) {
		switch (ch) {
		case 'a':
			if (action != UNSET && action != ATTACH)
				errx(1, "-a is mutually exclusive "
				    "with -d, -r, and -l");
			action = ATTACH;
			break;
		case 'd':
			if (action != UNSET && action != DETACH)
				errx(1, "-d is mutually exclusive "
				    "with -a, -r, and -l");
			action = DETACH;
			mdio.md_options |= MD_AUTOUNIT;
			break;
		case 'r':
			if (action != UNSET && action != RESIZE)
				errx(1, "-r is mutually exclusive "
				    "with -a, -d, and -l");
			action = RESIZE;
			mdio.md_options |= MD_AUTOUNIT;
			break;
		case 'l':
			if (action != UNSET && action != LIST)
				errx(1, "-l is mutually exclusive "
				    "with -a, -r, and -d");
			action = LIST;
			mdio.md_options |= MD_AUTOUNIT;
			break;
		case 'n':
			nflag = 1;
			break;
		case 't':
			if (tflag != NULL)
				errx(1, "-t can be passed only once");
			tflag = optarg;
			if (!strcmp(optarg, "malloc")) {
				mdio.md_type = MD_MALLOC;
				mdio.md_options |= MD_AUTOUNIT | MD_COMPRESS;
			} else if (!strcmp(optarg, "vnode")) {
				mdio.md_type = MD_VNODE;
				mdio.md_options |= MD_CLUSTER | MD_AUTOUNIT | MD_COMPRESS;
			} else if (!strcmp(optarg, "swap")) {
				mdio.md_type = MD_SWAP;
				mdio.md_options |= MD_CLUSTER | MD_AUTOUNIT | MD_COMPRESS;
			} else if (!strcmp(optarg, "null")) {
				mdio.md_type = MD_NULL;
				mdio.md_options |= MD_CLUSTER | MD_AUTOUNIT | MD_COMPRESS;
			} else
				errx(1, "unknown type: %s", optarg);
			break;
		case 'f':
			if (fflag != NULL)
				errx(1, "-f can be passed only once");
			fflag = realpath(optarg, NULL);
			if (fflag == NULL)
				err(1, "realpath");
			break;
		case 'o':
			if (!strcmp(optarg, "async"))
				mdio.md_options |= MD_ASYNC;
			else if (!strcmp(optarg, "noasync"))
				mdio.md_options &= ~MD_ASYNC;
			else if (!strcmp(optarg, "cache"))
				mdio.md_options |= MD_CACHE;
			else if (!strcmp(optarg, "nocache"))
				mdio.md_options &= ~MD_CACHE;
			else if (!strcmp(optarg, "cluster"))
				mdio.md_options |= MD_CLUSTER;
			else if (!strcmp(optarg, "nocluster"))
				mdio.md_options &= ~MD_CLUSTER;
			else if (!strcmp(optarg, "compress"))
				mdio.md_options |= MD_COMPRESS;
			else if (!strcmp(optarg, "nocompress"))
				mdio.md_options &= ~MD_COMPRESS;
			else if (!strcmp(optarg, "force"))
				mdio.md_options |= MD_FORCE;
			else if (!strcmp(optarg, "noforce"))
				mdio.md_options &= ~MD_FORCE;
			else if (!strcmp(optarg, "readonly"))
				mdio.md_options |= MD_READONLY;
			else if (!strcmp(optarg, "noreadonly"))
				mdio.md_options &= ~MD_READONLY;
			else if (!strcmp(optarg, "ro"))
				mdio.md_options |= MD_READONLY;
			else if (!strcmp(optarg, "noro"))
				mdio.md_options &= ~MD_READONLY;
			else if (!strcmp(optarg, "reserve"))
				mdio.md_options |= MD_RESERVE;
			else if (!strcmp(optarg, "noreserve"))
				mdio.md_options &= ~MD_RESERVE;
			else if (!strcmp(optarg, "verify"))
				mdio.md_options |= MD_VERIFY;
			else if (!strcmp(optarg, "noverify"))
				mdio.md_options &= ~MD_VERIFY;
			else
				errx(1, "unknown option: %s", optarg);
			break;
		case 'S':
			mdio.md_sectorsize = strtoul(optarg, &p, 0);
			break;
		case 's':
			if (sflag != NULL)
				errx(1, "-s can be passed only once");
			sflag = optarg;
			mdio.md_mediasize = (off_t)strtoumax(optarg, &p, 0);
			if (p == NULL || *p == '\0')
				mdio.md_mediasize *= DEV_BSIZE;
			else if (*p == 'b' || *p == 'B')
				; /* do nothing */
			else if (*p == 'k' || *p == 'K')
				mdio.md_mediasize <<= 10;
			else if (*p == 'm' || *p == 'M')
				mdio.md_mediasize <<= 20;
			else if (*p == 'g' || *p == 'G')
				mdio.md_mediasize <<= 30;
			else if (*p == 't' || *p == 'T') {
				mdio.md_mediasize <<= 30;
				mdio.md_mediasize <<= 10;
			} else if (*p == 'p' || *p == 'P') {
				mdio.md_mediasize <<= 30;
				mdio.md_mediasize <<= 20;
			} else
				errx(1, "unknown suffix on -s argument");
			break;
		case 'u':
			if (!strncmp(optarg, _PATH_DEV, sizeof(_PATH_DEV) - 1))
				optarg += sizeof(_PATH_DEV) - 1;
			if (!strncmp(optarg, MD_NAME, sizeof(MD_NAME) - 1))
				optarg += sizeof(MD_NAME) - 1;
			uflag = optarg;
			break;
		case 'v':
			vflag = OPT_VERBOSE;
			break;
		case 'x':
			mdio.md_fwsectors = strtoul(optarg, &p, 0);
			break;
		case 'y':
			mdio.md_fwheads = strtoul(optarg, &p, 0);
			break;
		case 'L':
			strlcpy(mdio.md_label, optarg, PATH_MAX);
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (action == UNSET)
		action = ATTACH;

	if (action == ATTACH) {
		if (tflag == NULL) {
			/*
			 * Try to infer the type based on other arguments.
			 */
			if (fflag != NULL || argc > 0) {
				/* Imply ``-t vnode'' */
				mdio.md_type = MD_VNODE;
				mdio.md_options |= MD_CLUSTER | MD_AUTOUNIT |
				    MD_COMPRESS;
			} else if (sflag != NULL) {
				/* Imply ``-t swap'' */
				mdio.md_type = MD_SWAP;
				mdio.md_options |= MD_CLUSTER | MD_AUTOUNIT |
				    MD_COMPRESS;
			} else
				errx(1, "unable to determine type");
		}

		if ((fflag != NULL || argc > 0) && mdio.md_type != MD_VNODE)
			errx(1, "only -t vnode can be used with file name");

		if (mdio.md_type == MD_VNODE) {
			if (fflag != NULL) {
				if (argc != 0)
					usage();
				md_set_file(fflag);
			} else {
				if (argc != 1)
					usage();
				md_set_file(*argv);
			}

			if ((mdio.md_options & MD_READONLY) == 0 &&
			    access(mdio.md_file, W_OK) < 0 &&
			    (errno == EACCES || errno == EPERM ||
			     errno == EROFS)) {
				warnx("WARNING: opening backing store: %s "
				    "readonly", mdio.md_file);
				mdio.md_options |= MD_READONLY;
			}
		}

		if ((mdio.md_type == MD_MALLOC || mdio.md_type == MD_SWAP ||
		    mdio.md_type == MD_NULL) && sflag == NULL)
			errx(1, "must specify -s for -t malloc, -t swap, "
			    "or -t null");
		if (mdio.md_type == MD_VNODE && mdio.md_file[0] == '\0')
			errx(1, "must specify -f for -t vnode");
	} else {
		if (mdio.md_sectorsize != 0)
			errx(1, "-S can only be used with -a");
		if (action != RESIZE && sflag != NULL)
			errx(1, "-s can only be used with -a and -r");
		if (mdio.md_fwsectors != 0)
			errx(1, "-x can only be used with -a");
		if (mdio.md_fwheads != 0)
			errx(1, "-y can only be used with -a");
		if (fflag != NULL && action != LIST)
			errx(1, "-f can only be used with -a and -l");
		if (tflag != NULL)
			errx(1, "-t can only be used with -a");
		if (argc > 0)
			errx(1, "file can only be used with -a");
		if ((action != DETACH && action != RESIZE) &&
		    (mdio.md_options & ~MD_AUTOUNIT) != 0)
			errx(1, "-o can only be used with -a, -d, and -r");
		if (action == DETACH &&
		    (mdio.md_options & ~(MD_FORCE | MD_AUTOUNIT)) != 0)
			errx(1, "only -o [no]force can be used with -d");
		if (action == RESIZE &&
		    (mdio.md_options & ~(MD_FORCE | MD_RESERVE | MD_AUTOUNIT)) != 0)
			errx(1, "only -o [no]force and -o [no]reserve can be used with -r");
	}

	if (action == RESIZE && sflag == NULL)
		errx(1, "must specify -s for -r");

	if (action != LIST && vflag == OPT_VERBOSE)
		errx(1, "-v can only be used with -l");

	if (uflag != NULL) {
		mdio.md_unit = strtoul(uflag, &p, 0);
		if (mdio.md_unit == (unsigned)ULONG_MAX || *p != '\0')
			errx(1, "bad unit: %s", uflag);
		mdio.md_options &= ~MD_AUTOUNIT;
	}

	mdio.md_version = MDIOVERSION;

	if (!kld_isloaded("g_md") && kld_load("geom_md") == -1)
		err(1, "failed to load geom_md module");

	fd = open(_PATH_DEV MDCTL_NAME, O_RDWR, 0);
	if (fd < 0)
		err(1, "open(%s%s)", _PATH_DEV, MDCTL_NAME);

	if (action == ATTACH) {
		i = ioctl(fd, MDIOCATTACH, &mdio);
		if (i < 0)
			err(1, "ioctl(%s%s)", _PATH_DEV, MDCTL_NAME);
		if (mdio.md_options & MD_AUTOUNIT)
			printf("%s%d\n", nflag ? "" : MD_NAME, mdio.md_unit);
	} else if (action == DETACH) {
		if (mdio.md_options & MD_AUTOUNIT)
			errx(1, "-d requires -u");
		i = ioctl(fd, MDIOCDETACH, &mdio);
		if (i < 0)
			err(1, "ioctl(%s%s)", _PATH_DEV, MDCTL_NAME);
	} else if (action == RESIZE) {
		if (mdio.md_options & MD_AUTOUNIT)
			errx(1, "-r requires -u");
		i = ioctl(fd, MDIOCRESIZE, &mdio);
		if (i < 0)
			err(1, "ioctl(%s%s)", _PATH_DEV, MDCTL_NAME);
	} else if (action == LIST) {
		if (mdio.md_options & MD_AUTOUNIT) {
			/*
			 * Listing all devices. This is why we pass NULL
			 * together with OPT_LIST.
			 */
			return (md_list(NULL, OPT_LIST | vflag, fflag));
		} else
			return (md_query(uflag, vflag, fflag));
	} else
		usage();
	close(fd);
	return (0);
}

static void
md_set_file(const char *fn)
{
	struct stat sb;
	int fd;

	if (realpath(fn, mdio.md_file) == NULL)
		err(1, "could not find full path for %s", fn);
	fd = open(mdio.md_file, O_RDONLY);
	if (fd < 0)
		err(1, "could not open %s", fn);
	if (fstat(fd, &sb) == -1)
		err(1, "could not stat %s", fn);
	if (!S_ISREG(sb.st_mode))
		errx(1, "%s is not a regular file", fn);
	if (mdio.md_mediasize == 0)
		mdio.md_mediasize = sb.st_size;
	close(fd);
}

/*
 * Lists md(4) disks. Is used also as a query routine, since it handles XML
 * interface. 'units' can be NULL for listing memory disks. It might be
 * coma-separated string containing md(4) disk names. 'opt' distinguished
 * between list and query mode.
 */
static int
md_list(const char *units, int opt, const char *fflag)
{
	struct gmesh gm;
	struct gprovider *pp;
	struct gconf *gc;
	struct gident *gid;
	struct devstat *gsp;
	struct ggeom *gg;
	struct gclass *gcl;
	void *sq;
	int retcode, ffound, ufound;
	char *length;
	const char *type, *file, *label;

	type = file = length = NULL;

	retcode = geom_gettree(&gm);
	if (retcode != 0)
		return (-1);
	retcode = geom_stats_open();
	if (retcode != 0)
		return (-1);
	sq = geom_stats_snapshot_get();
	if (sq == NULL)
		return (-1);

	ffound = ufound = 0;
	while ((gsp = geom_stats_snapshot_next(sq)) != NULL) {
		gid = geom_lookupid(&gm, gsp->id);
		if (gid == NULL)
			continue;
		if (gid->lg_what == ISPROVIDER) {
			pp = gid->lg_ptr;
			gg = pp->lg_geom;
			gcl = gg->lg_class;
			if (strcmp(gcl->lg_name, CLASS_NAME_MD) != 0)
				continue;
			if ((opt & OPT_UNIT) && (units != NULL)) {
				retcode = md_find(units, pp->lg_name);
				if (retcode != 1)
					continue;
				else
					ufound = 1;
			}
			gc = &pp->lg_config;
			type = geom_config_get(gc, "type");
			if (type != NULL && (strcmp(type, "vnode") == 0 ||
			    strcmp(type, "preload") == 0)) {
				file = geom_config_get(gc, "file");
				if (fflag != NULL &&
				    strcmp(fflag, file) != 0)
					continue;
				else
					ffound = 1;
			} else if (fflag != NULL)
					continue;
			if (nflag && strncmp(pp->lg_name, MD_NAME, 2) == 0)
				printf("%s", pp->lg_name + 2);
			else
				printf("%s", pp->lg_name);

			if (opt & OPT_VERBOSE ||
			    ((opt & OPT_UNIT) && fflag == NULL)) {
				length = geom_config_get(gc, "length");
				printf("\t%s\t", type);
				if (length != NULL)
					md_prthumanval(length);
				if (file == NULL)
					file = "-";
				printf("\t%s", file);
				file = NULL;
				label = geom_config_get(gc, "label");
				if (label == NULL)
					label = "";
				printf("\t%s", label);
			}
			opt |= OPT_DONE;
			if ((opt & OPT_LIST) && !(opt & OPT_VERBOSE))
				printf(" ");
			else
				printf("\n");
		}
	}
	if ((opt & OPT_LIST) && (opt & OPT_DONE) && !(opt & OPT_VERBOSE))
		printf("\n");
	/* XXX: Check if it's enough to clean everything. */
	geom_stats_snapshot_free(sq);
	if (opt & OPT_UNIT) {
		if (((fflag == NULL) && ufound) ||
		    ((fflag == NULL) && (units != NULL) && ufound) ||
		    ((fflag != NULL) && ffound) ||
		    ((fflag != NULL) && (units != NULL) && ufound && ffound))
			return (0);
	} else if (opt & OPT_LIST) {
		if ((fflag == NULL) ||
		    ((fflag != NULL) && ffound))
			return (0);
	}
	return (-1);
}

/*
 * Returns value of 'name' from gconfig structure.
 */
static char *
geom_config_get(struct gconf *g, const char *name)
{
	struct gconfig *gce;

	LIST_FOREACH(gce, g, lg_config) {
		if (strcmp(gce->lg_name, name) == 0)
			return (gce->lg_val);
	}
	return (NULL);
}

/*
 * List is comma separated list of MD disks. name is a
 * device name we look for.  Returns 1 if found and 0
 * otherwise.
 */
static int
md_find(const char *list, const char *name)
{
	int ret;
	char num[PATH_MAX];
	char *ptr, *p, *u;

	ret = 0;
	ptr = strdup(list);
	if (ptr == NULL)
		return (-1);
	for (p = ptr; (u = strsep(&p, ",")) != NULL;) {
		if (strncmp(u, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
			u += sizeof(_PATH_DEV) - 1;
		/* Just in case user specified number instead of full name */
		snprintf(num, sizeof(num), "%s%s", MD_NAME, u);
		if (strcmp(u, name) == 0 || strcmp(num, name) == 0) {
			ret = 1;
			break;
		}
	}
	free(ptr);
	return (ret);
}

static void
md_prthumanval(char *length)
{
	char buf[6];
	uintmax_t bytes;
	char *endptr;

	errno = 0;
	bytes = strtoumax(length, &endptr, 10);
	if (errno != 0 || *endptr != '\0' || bytes > INT64_MAX)
		return;
	humanize_number(buf, sizeof(buf), (int64_t)bytes, "",
	    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
	(void)printf("%6s", buf);
}

static int
md_query(const char *name, const int opt, const char *fflag)
{

	return (md_list(name, opt | OPT_UNIT, fflag));
}
