/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1993,1995 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <a.out.h>
#include <ctype.h>
#include <dirent.h>
#include <elf-hints.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/link_aout.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldconfig.h"
#include "shlib.h"
#include "support.h"

#if DEBUG
/* test */
#undef _PATH_LD_HINTS
#define _PATH_LD_HINTS		"./ld.so.hints"
#undef _PATH_ELF_HINTS
#define _PATH_ELF_HINTS		"./ld-elf.so.hints"
#endif

#define	_PATH_LD32_HINTS	"/var/run/ld32.so.hints"
#define	_PATH_ELF32_HINTS	"/var/run/ld-elf32.so.hints"
#define	_PATH_ELFSOFT_HINTS	"/var/run/ld-elf-soft.so.hints"

#undef major
#undef minor

static int			verbose;
static int			nostd;
static int			justread;
static int			merge;
static int			rescan;
static const char		*hints_file;

struct shlib_list {
	/* Internal list of shared libraries found */
	char			*name;
	char			*path;
	int			dewey[MAXDEWEY];
	int			ndewey;
#define major dewey[0]
#define minor dewey[1]
	struct shlib_list	*next;
};

static struct shlib_list	*shlib_head = NULL, **shlib_tail = &shlib_head;
static char			*dir_list;

static int		buildhints(void);
static int		dodir(char *, int);
int			dofile(char *, int);
static void		enter(char *, char *, char *, int *, int);
static void		listhints(void);
static int		readhints(void);
static void		usage(void);

/*
 * Note on aout/a.out support.
 * To properly support shared libraries for compat2x, which are a.out, we need
 * to support a.out here.  As of 2013, bug reports are still coming in for this
 * feature (on amd64 no less), so we know it is still in use.
 */

int
main(int argc, char **argv)
{
	int		i, c;
	int		rval = 0;
	int		is_aout = 0;
	int		is_32 = 0;
	int		is_soft = 0;

	while (argc > 1) {
		if (strcmp(argv[1], "-aout") == 0) {
			is_aout = 1;
			argc--;
			argv++;
		} else if (strcmp(argv[1], "-elf") == 0) {
			is_aout = 0;
			argc--;
			argv++;
		} else if (strcmp(argv[1], "-32") == 0) {
			is_32 = 1;
			argc--;
			argv++;
		} else if (strcmp(argv[1], "-soft") == 0) {
			is_soft = 1;
			argc--;
			argv++;
		} else {
			break;
		}
	}

	if (is_soft)
		hints_file = _PATH_ELFSOFT_HINTS;	/* Never will have a.out softfloat */
	else if (is_32)
		hints_file = is_aout ? _PATH_LD32_HINTS : _PATH_ELF32_HINTS;
	else
		hints_file = is_aout ? _PATH_LD_HINTS : _PATH_ELF_HINTS;
	if (argc == 1)
		rescan = 1;
	else while((c = getopt(argc, argv, "Rf:imrsv")) != -1) {
		switch (c) {
		case 'R':
			rescan = 1;
			break;
		case 'f':
			hints_file = optarg;
			break;
		case 'i':
			insecure = 1;
			break;
		case 'm':
			merge = 1;
			break;
		case 'r':
			justread = 1;
			break;
		case 's':
			nostd = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (!is_aout) {
		if (justread)
			list_elf_hints(hints_file);
		else
			update_elf_hints(hints_file, argc - optind,
			    argv + optind, merge || rescan);
		return 0;
	}

	/* Here begins the aout libs processing */
	dir_list = strdup("");

	if (justread || merge || rescan) {
		if ((rval = readhints()) != 0)
			return rval;
	}

	if (!nostd && !merge && !rescan)
		std_search_path();

	/* Add any directories/files from the command line */
	if (!justread) {
		for (i = optind; i < argc; i++) {
			struct stat stbuf;

			if (stat(argv[i], &stbuf) == -1) {
				warn("%s", argv[i]);
				rval = -1;
			} else if (strcmp(argv[i], "/usr/lib") == 0) {
				warnx("WARNING! '%s' can not be used", argv[i]);
				rval = -1;
			} else {
				/*
				 * See if this is a directory-containing
				 * file instead of a directory
				 */
				if (S_ISREG(stbuf.st_mode))
					rval |= dofile(argv[i], 0);
				else
					add_search_path(argv[i]);
			}
		}
	}

	for (i = 0; i < n_search_dirs; i++) {
		char *cp = concat(dir_list, *dir_list?":":"", search_dirs[i]);
		free(dir_list);
		dir_list = cp;
	}

	if (justread) {
		listhints();
		return 0;
	}

	for (i = 0; i < n_search_dirs; i++)
		rval |= dodir(search_dirs[i], 1);

	rval |= buildhints();

	return rval;
}

static void
usage(void)
{
	fprintf(stderr,
	"usage: ldconfig [-32] [-aout | -elf] [-Rimrsv] [-f hints_file] [directory | file ...]\n");
	exit(1);
}
	
int
dofile(char *fname, int silent)
{
	FILE *hfp;
	char buf[MAXPATHLEN];
	int rval = 0;
	char *cp, *sp;

	if ((hfp = fopen(fname, "r")) == NULL) {
		warn("%s", fname);
		return -1;
	}

	while (fgets(buf, sizeof(buf), hfp)) {
		cp = buf;
		while (isspace(*cp))
			cp++;
		if (*cp == '#' || *cp == '\0')
			continue;
		sp = cp;
		while (!isspace(*cp) && *cp != '\0')
			cp++;

		if (*cp != '\n') {
			*cp = '\0';
			warnx("%s: trailing characters ignored", sp);
		}

		*cp = '\0';

		rval |= dodir(sp, silent);
	}

	(void)fclose(hfp);
	return rval;
}

int
dodir(char *dir, int silent)
{
	DIR		*dd;
	struct dirent	*dp;
	char		name[MAXPATHLEN];
	int		dewey[MAXDEWEY], ndewey;

	if ((dd = opendir(dir)) == NULL) {
		if (silent && errno == ENOENT)	/* Ignore the error */
			return 0;
		warn("%s", dir);
		return -1;
	}

	while ((dp = readdir(dd)) != NULL) {
		int n;
		char *cp;

		/* Check for `lib' prefix */
		if (dp->d_name[0] != 'l' ||
		    dp->d_name[1] != 'i' ||
		    dp->d_name[2] != 'b')
			continue;

		/* Copy the entry minus prefix */
		(void)strcpy(name, dp->d_name + 3);
		n = strlen(name);
		if (n < 4)
			continue;

		/* Find ".so." in name */
		for (cp = name + n - 4; cp > name; --cp) {
			if (cp[0] == '.' &&
			    cp[1] == 's' &&
			    cp[2] == 'o' &&
			    cp[3] == '.')
				break;
		}
		if (cp <= name)
			continue;

		*cp = '\0';
		if (!isdigit(*(cp+4)))
			continue;

		bzero((caddr_t)dewey, sizeof(dewey));
		ndewey = getdewey(dewey, cp + 4);
		if (ndewey < 2)
			continue;
		enter(dir, dp->d_name, name, dewey, ndewey);
	}

	closedir(dd);
	return 0;
}

static void
enter(char *dir, char *file, char *name, int dewey[], int ndewey)
{
	struct shlib_list	*shp;

	for (shp = shlib_head; shp; shp = shp->next) {
		if (strcmp(name, shp->name) != 0 || major != shp->major)
			continue;

		/* Name matches existing entry */
		if (cmpndewey(dewey, ndewey, shp->dewey, shp->ndewey) > 0) {

			/* Update this entry with higher versioned lib */
			if (verbose)
				printf("Updating lib%s.%d.%d to %s/%s\n",
					shp->name, shp->major, shp->minor,
					dir, file);

			free(shp->name);
			shp->name = strdup(name);
			free(shp->path);
			shp->path = concat(dir, "/", file);
			bcopy(dewey, shp->dewey, sizeof(shp->dewey));
			shp->ndewey = ndewey;
		}
		break;
	}

	if (shp)
		/* Name exists: older version or just updated */
		return;

	/* Allocate new list element */
	if (verbose)
		printf("Adding %s/%s\n", dir, file);

	shp = (struct shlib_list *)xmalloc(sizeof *shp);
	shp->name = strdup(name);
	shp->path = concat(dir, "/", file);
	bcopy(dewey, shp->dewey, sizeof(shp->dewey));
	shp->ndewey = ndewey;
	shp->next = NULL;

	*shlib_tail = shp;
	shlib_tail = &shp->next;
}


static int
hinthash(char *cp, int vmajor)
{
	int	k = 0;

	while (*cp)
		k = (((k << 1) + (k >> 14)) ^ (*cp++)) & 0x3fff;

	k = (((k << 1) + (k >> 14)) ^ (vmajor*257)) & 0x3fff;

	return k;
}

int
buildhints(void)
{
	struct hints_header	hdr;
	struct hints_bucket	*blist;
	struct shlib_list	*shp;
	char			*strtab;
	int			i, n, str_index = 0;
	int			strtab_sz = 0;	/* Total length of strings */
	int			nhints = 0;	/* Total number of hints */
	int			fd;
	char			*tmpfilename;

	for (shp = shlib_head; shp; shp = shp->next) {
		strtab_sz += 1 + strlen(shp->name);
		strtab_sz += 1 + strlen(shp->path);
		nhints++;
	}

	/* Fill hints file header */
	hdr.hh_magic = HH_MAGIC;
	hdr.hh_version = LD_HINTS_VERSION_2;
	hdr.hh_nbucket = 1 * nhints;
	n = hdr.hh_nbucket * sizeof(struct hints_bucket);
	hdr.hh_hashtab = sizeof(struct hints_header);
	hdr.hh_strtab = hdr.hh_hashtab + n;
	hdr.hh_dirlist = strtab_sz;
	strtab_sz += 1 + strlen(dir_list);
	hdr.hh_strtab_sz = strtab_sz;
	hdr.hh_ehints = hdr.hh_strtab + hdr.hh_strtab_sz;

	if (verbose)
		printf("Totals: entries %d, buckets %ld, string size %d\n",
			nhints, (long)hdr.hh_nbucket, strtab_sz);

	/* Allocate buckets and string table */
	blist = (struct hints_bucket *)xmalloc(n);
	bzero((char *)blist, n);
	for (i = 0; i < hdr.hh_nbucket; i++)
		/* Empty all buckets */
		blist[i].hi_next = -1;

	strtab = (char *)xmalloc(strtab_sz);

	/* Enter all */
	for (shp = shlib_head; shp; shp = shp->next) {
		struct hints_bucket	*bp;

		bp = blist +
		  (hinthash(shp->name, shp->major) % hdr.hh_nbucket);

		if (bp->hi_pathx) {
			int	j;

			for (j = 0; j < hdr.hh_nbucket; j++) {
				if (blist[j].hi_pathx == 0)
					break;
			}
			if (j == hdr.hh_nbucket) {
				warnx("bummer!");
				return -1;
			}
			while (bp->hi_next != -1)
				bp = &blist[bp->hi_next];
			bp->hi_next = j;
			bp = blist + j;
		}

		/* Insert strings in string table */
		bp->hi_namex = str_index;
		strcpy(strtab + str_index, shp->name);
		str_index += 1 + strlen(shp->name);

		bp->hi_pathx = str_index;
		strcpy(strtab + str_index, shp->path);
		str_index += 1 + strlen(shp->path);

		/* Copy versions */
		bcopy(shp->dewey, bp->hi_dewey, sizeof(bp->hi_dewey));
		bp->hi_ndewey = shp->ndewey;
	}

	/* Copy search directories */
	strcpy(strtab + str_index, dir_list);
	str_index += 1 + strlen(dir_list);

	/* Sanity check */
	if (str_index != strtab_sz) {
		errx(1, "str_index(%d) != strtab_sz(%d)", str_index, strtab_sz);
	}

	tmpfilename = concat(hints_file, ".XXXXXXXXXX", "");
	umask(0);	/* Create with exact permissions */
	if ((fd = mkstemp(tmpfilename)) == -1) {
		warn("%s", tmpfilename);
		return -1;
	}
	fchmod(fd, 0444);

	if (write(fd, &hdr, sizeof(struct hints_header)) !=
						sizeof(struct hints_header)) {
		warn("%s", hints_file);
		return -1;
	}
	if (write(fd, blist, hdr.hh_nbucket * sizeof(*blist)) !=
				(ssize_t)(hdr.hh_nbucket * sizeof(*blist))) {
		warn("%s", hints_file);
		return -1;
	}
	if (write(fd, strtab, strtab_sz) != strtab_sz) {
		warn("%s", hints_file);
		return -1;
	}
	if (close(fd) != 0) {
		warn("%s", hints_file);
		return -1;
	}
	if (rename(tmpfilename, hints_file) != 0) {
		warn("%s", hints_file);
		return -1;
	}

	return 0;
}

static int
readhints(void)
{
	int			fd;
	void			*addr;
	long			fsize;
	long			msize;
	struct hints_header	*hdr;
	struct hints_bucket	*blist;
	char			*strtab;
	struct shlib_list	*shp;
	int			i;

	if ((fd = open(hints_file, O_RDONLY, 0)) == -1) {
		warn("%s", hints_file);
		return -1;
	}

	msize = PAGE_SIZE;
	addr = mmap(0, msize, PROT_READ, MAP_PRIVATE, fd, 0);

	if (addr == MAP_FAILED) {
		warn("%s", hints_file);
		return -1;
	}

	hdr = (struct hints_header *)addr;
	if (HH_BADMAG(*hdr)) {
		warnx("%s: bad magic: %lo", hints_file,
			(unsigned long)hdr->hh_magic);
		return -1;
	}

	if (hdr->hh_version != LD_HINTS_VERSION_1 &&
	    hdr->hh_version != LD_HINTS_VERSION_2) {
		warnx("unsupported version: %ld", (long)hdr->hh_version);
		return -1;
	}

	if (hdr->hh_ehints > msize) {
		fsize = hdr->hh_ehints;
		munmap(addr, msize);
		addr = mmap(0, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
		if (addr == MAP_FAILED) {
			warn("%s", hints_file);
			return -1;
		}
		hdr = (struct hints_header *)addr;
	}
	close(fd);

	strtab = (char *)addr + hdr->hh_strtab;

	if (hdr->hh_version >= LD_HINTS_VERSION_2)
		add_search_path(strtab + hdr->hh_dirlist);
	else if (rescan)
		errx(1, "%s too old and does not contain the search path",
			hints_file);

	if (rescan)
		return 0;

	blist = malloc(sizeof(*blist) * hdr->hh_nbucket);
	if (blist == NULL)
		err(1, "readhints");
	memcpy(blist, (char *)addr + hdr->hh_hashtab,
		sizeof(*blist) * hdr->hh_nbucket);


	for (i = 0; i < hdr->hh_nbucket; i++) {
		struct hints_bucket	*bp = &blist[i];

		/* Sanity check */
		if (bp->hi_namex >= hdr->hh_strtab_sz) {
			warnx("bad name index: %#x", bp->hi_namex);
			free(blist);
			return -1;
		}
		if (bp->hi_pathx >= hdr->hh_strtab_sz) {
			warnx("bad path index: %#x", bp->hi_pathx);
			free(blist);
			return -1;
		}

		/* Allocate new list element */
		shp = (struct shlib_list *)xmalloc(sizeof *shp);
		shp->name = strdup(strtab + bp->hi_namex);
		shp->path = strdup(strtab + bp->hi_pathx);
		bcopy(bp->hi_dewey, shp->dewey, sizeof(shp->dewey));
		shp->ndewey = bp->hi_ndewey;
		shp->next = NULL;

		*shlib_tail = shp;
		shlib_tail = &shp->next;
	}

	free(blist);
	return 0;
}

static void
listhints(void)
{
	struct shlib_list	*shp;
	int			i;

	printf("%s:\n", hints_file);
	printf("\tsearch directories: %s\n", dir_list);

	for (i = 0, shp = shlib_head; shp; i++, shp = shp->next)
		printf("\t%d:-l%s.%d.%d => %s\n",
			i, shp->name, shp->major, shp->minor, shp->path);

	return;
}
