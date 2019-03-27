/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2012, 2013 SRI International
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1987, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)xinstall.c	8.1 (Berkeley) 7/21/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <md5.h>
#include <paths.h>
#include <pwd.h>
#include <ripemd.h>
#include <sha.h>
#include <sha256.h>
#include <sha512.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <vis.h>

#include "mtree.h"

#define MAX_CMP_SIZE	(16 * 1024 * 1024)

#define	LN_ABSOLUTE	0x01
#define	LN_RELATIVE	0x02
#define	LN_HARD		0x04
#define	LN_SYMBOLIC	0x08
#define	LN_MIXED	0x10

#define	DIRECTORY	0x01		/* Tell install it's a directory. */
#define	SETFLAGS	0x02		/* Tell install to set flags. */
#define	NOCHANGEBITS	(UF_IMMUTABLE | UF_APPEND | SF_IMMUTABLE | SF_APPEND)
#define	BACKUP_SUFFIX	".old"

typedef union {
	MD5_CTX		MD5;
	RIPEMD160_CTX	RIPEMD160;
	SHA1_CTX	SHA1;
	SHA256_CTX	SHA256;
	SHA512_CTX	SHA512;
}	DIGEST_CTX;

static enum {
	DIGEST_NONE = 0,
	DIGEST_MD5,
	DIGEST_RIPEMD160,
	DIGEST_SHA1,
	DIGEST_SHA256,
	DIGEST_SHA512,
} digesttype = DIGEST_NONE;

extern char **environ;

static gid_t gid;
static uid_t uid;
static int dobackup, docompare, dodir, dolink, dopreserve, dostrip, dounpriv,
    safecopy, verbose;
static int haveopt_f, haveopt_g, haveopt_m, haveopt_o;
static mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
static FILE *metafp;
static const char *group, *owner;
static const char *suffix = BACKUP_SUFFIX;
static char *destdir, *digest, *fflags, *metafile, *tags;

static int	compare(int, const char *, size_t, int, const char *, size_t,
		    char **);
static char	*copy(int, const char *, int, const char *, off_t);
static int	create_newfile(const char *, int, struct stat *);
static int	create_tempfile(const char *, char *, size_t);
static char	*quiet_mktemp(char *template);
static char	*digest_file(const char *);
static void	digest_init(DIGEST_CTX *);
static void	digest_update(DIGEST_CTX *, const char *, size_t);
static char	*digest_end(DIGEST_CTX *, char *);
static int	do_link(const char *, const char *, const struct stat *);
static void	do_symlink(const char *, const char *, const struct stat *);
static void	makelink(const char *, const char *, const struct stat *);
static void	install(const char *, const char *, u_long, u_int);
static void	install_dir(char *);
static void	metadata_log(const char *, const char *, struct timespec *,
		    const char *, const char *, off_t);
static int	parseid(const char *, id_t *);
static void	strip(const char *);
static int	trymmap(int);
static void	usage(void);

int
main(int argc, char *argv[])
{
	struct stat from_sb, to_sb;
	mode_t *set;
	u_long fset;
	int ch, no_target;
	u_int iflags;
	char *p;
	const char *to_name;

	fset = 0;
	iflags = 0;
	group = owner = NULL;
	while ((ch = getopt(argc, argv, "B:bCcD:df:g:h:l:M:m:N:o:pSsT:Uv")) !=
	     -1)
		switch((char)ch) {
		case 'B':
			suffix = optarg;
			/* FALLTHROUGH */
		case 'b':
			dobackup = 1;
			break;
		case 'C':
			docompare = 1;
			break;
		case 'c':
			/* For backwards compatibility. */
			break;
		case 'D':
			destdir = optarg;
			break;
		case 'd':
			dodir = 1;
			break;
		case 'f':
			haveopt_f = 1;
			fflags = optarg;
			break;
		case 'g':
			haveopt_g = 1;
			group = optarg;
			break;
		case 'h':
			digest = optarg;
			break;
		case 'l':
			for (p = optarg; *p != '\0'; p++)
				switch (*p) {
				case 's':
					dolink &= ~(LN_HARD|LN_MIXED);
					dolink |= LN_SYMBOLIC;
					break;
				case 'h':
					dolink &= ~(LN_SYMBOLIC|LN_MIXED);
					dolink |= LN_HARD;
					break;
				case 'm':
					dolink &= ~(LN_SYMBOLIC|LN_HARD);
					dolink |= LN_MIXED;
					break;
				case 'a':
					dolink &= ~LN_RELATIVE;
					dolink |= LN_ABSOLUTE;
					break;
				case 'r':
					dolink &= ~LN_ABSOLUTE;
					dolink |= LN_RELATIVE;
					break;
				default:
					errx(1, "%c: invalid link type", *p);
					/* NOTREACHED */
				}
			break;
		case 'M':
			metafile = optarg;
			break;
		case 'm':
			haveopt_m = 1;
			if (!(set = setmode(optarg)))
				errx(EX_USAGE, "invalid file mode: %s",
				     optarg);
			mode = getmode(set, 0);
			free(set);
			break;
		case 'N':
			if (!setup_getid(optarg))
				err(EX_OSERR, "Unable to use user and group "
				    "databases in `%s'", optarg);
			break;
		case 'o':
			haveopt_o = 1;
			owner = optarg;
			break;
		case 'p':
			docompare = dopreserve = 1;
			break;
		case 'S':
			safecopy = 1;
			break;
		case 's':
			dostrip = 1;
			break;
		case 'T':
			tags = optarg;
			break;
		case 'U':
			dounpriv = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	/* some options make no sense when creating directories */
	if (dostrip && dodir) {
		warnx("-d and -s may not be specified together");
		usage();
	}

	if (getenv("DONTSTRIP") != NULL) {
		warnx("DONTSTRIP set - will not strip installed binaries");
		dostrip = 0;
	}

	/* must have at least two arguments, except when creating directories */
	if (argc == 0 || (argc == 1 && !dodir))
		usage();

	if (digest != NULL) {
		if (strcmp(digest, "none") == 0) {
			digesttype = DIGEST_NONE;
		} else if (strcmp(digest, "md5") == 0) {
		       digesttype = DIGEST_MD5;
		} else if (strcmp(digest, "rmd160") == 0) {
			digesttype = DIGEST_RIPEMD160;
		} else if (strcmp(digest, "sha1") == 0) {
			digesttype = DIGEST_SHA1;
		} else if (strcmp(digest, "sha256") == 0) {
			digesttype = DIGEST_SHA256;
		} else if (strcmp(digest, "sha512") == 0) {
			digesttype = DIGEST_SHA512;
		} else {
			warnx("unknown digest `%s'", digest);
			usage();
		}
	}

	/* need to make a temp copy so we can compare stripped version */
	if (docompare && dostrip)
		safecopy = 1;

	/* get group and owner id's */
	if (group != NULL && !dounpriv) {
		if (gid_from_group(group, &gid) == -1) {
			id_t id;
			if (!parseid(group, &id))
				errx(1, "unknown group %s", group);
			gid = id;
		}
	} else
		gid = (gid_t)-1;

	if (owner != NULL && !dounpriv) {
		if (uid_from_user(owner, &uid) == -1) {
			id_t id;
			if (!parseid(owner, &id))
				errx(1, "unknown user %s", owner);
			uid = id;
		}
	} else
		uid = (uid_t)-1;

	if (fflags != NULL && !dounpriv) {
		if (strtofflags(&fflags, &fset, NULL))
			errx(EX_USAGE, "%s: invalid flag", fflags);
		iflags |= SETFLAGS;
	}

	if (metafile != NULL) {
		if ((metafp = fopen(metafile, "a")) == NULL)
			warn("open %s", metafile);
	} else
		digesttype = DIGEST_NONE;

	if (dodir) {
		for (; *argv != NULL; ++argv)
			install_dir(*argv);
		exit(EX_OK);
		/* NOTREACHED */
	}

	to_name = argv[argc - 1];
	no_target = stat(to_name, &to_sb);
	if (!no_target && S_ISDIR(to_sb.st_mode)) {
		if (dolink & LN_SYMBOLIC) {
			if (lstat(to_name, &to_sb) != 0)
				err(EX_OSERR, "%s vanished", to_name);
			if (S_ISLNK(to_sb.st_mode)) {
				if (argc != 2) {
					errno = ENOTDIR;
					err(EX_USAGE, "%s", to_name);
				}
				install(*argv, to_name, fset, iflags);
				exit(EX_OK);
			}
		}
		for (; *argv != to_name; ++argv)
			install(*argv, to_name, fset, iflags | DIRECTORY);
		exit(EX_OK);
		/* NOTREACHED */
	}

	/* can't do file1 file2 directory/file */
	if (argc != 2) {
		if (no_target)
			warnx("target directory `%s' does not exist", 
			    argv[argc - 1]);
		else
			warnx("target `%s' is not a directory",
			    argv[argc - 1]);
		usage();
	}

	if (!no_target && !dolink) {
		if (stat(*argv, &from_sb))
			err(EX_OSERR, "%s", *argv);
		if (!S_ISREG(to_sb.st_mode)) {
			errno = EFTYPE;
			err(EX_OSERR, "%s", to_name);
		}
		if (to_sb.st_dev == from_sb.st_dev &&
		    to_sb.st_ino == from_sb.st_ino)
			errx(EX_USAGE, 
			    "%s and %s are the same file", *argv, to_name);
	}
	install(*argv, to_name, fset, iflags);
	exit(EX_OK);
	/* NOTREACHED */
}

static char *
digest_file(const char *name)
{

	switch (digesttype) {
	case DIGEST_MD5:
		return (MD5File(name, NULL));
	case DIGEST_RIPEMD160:
		return (RIPEMD160_File(name, NULL));
	case DIGEST_SHA1:
		return (SHA1_File(name, NULL));
	case DIGEST_SHA256:
		return (SHA256_File(name, NULL));
	case DIGEST_SHA512:
		return (SHA512_File(name, NULL));
	default:
		return (NULL);
	}
}

static void
digest_init(DIGEST_CTX *c)
{

	switch (digesttype) {
	case DIGEST_NONE:
		break;
	case DIGEST_MD5:
		MD5Init(&(c->MD5));
		break;
	case DIGEST_RIPEMD160:
		RIPEMD160_Init(&(c->RIPEMD160));
		break;
	case DIGEST_SHA1:
		SHA1_Init(&(c->SHA1));
		break;
	case DIGEST_SHA256:
		SHA256_Init(&(c->SHA256));
		break;
	case DIGEST_SHA512:
		SHA512_Init(&(c->SHA512));
		break;
	}
}

static void
digest_update(DIGEST_CTX *c, const char *data, size_t len)
{

	switch (digesttype) {
	case DIGEST_NONE:
		break;
	case DIGEST_MD5:
		MD5Update(&(c->MD5), data, len);
		break;
	case DIGEST_RIPEMD160:
		RIPEMD160_Update(&(c->RIPEMD160), data, len);
		break;
	case DIGEST_SHA1:
		SHA1_Update(&(c->SHA1), data, len);
		break;
	case DIGEST_SHA256:
		SHA256_Update(&(c->SHA256), data, len);
		break;
	case DIGEST_SHA512:
		SHA512_Update(&(c->SHA512), data, len);
		break;
	}
}

static char *
digest_end(DIGEST_CTX *c, char *buf)
{

	switch (digesttype) {
	case DIGEST_MD5:
		return (MD5End(&(c->MD5), buf));
	case DIGEST_RIPEMD160:
		return (RIPEMD160_End(&(c->RIPEMD160), buf));
	case DIGEST_SHA1:
		return (SHA1_End(&(c->SHA1), buf));
	case DIGEST_SHA256:
		return (SHA256_End(&(c->SHA256), buf));
	case DIGEST_SHA512:
		return (SHA512_End(&(c->SHA512), buf));
	default:
		return (NULL);
	}
}

/*
 * parseid --
 *	parse uid or gid from arg into id, returning non-zero if successful
 */
static int
parseid(const char *name, id_t *id)
{
	char	*ep;
	errno = 0;
	*id = (id_t)strtoul(name, &ep, 10);
	if (errno || *ep != '\0')
		return (0);
	return (1);
}

/*
 * quiet_mktemp --
 *	mktemp implementation used mkstemp to avoid mktemp warnings.  We
 *	really do need mktemp semantics here as we will be creating a link.
 */
static char *
quiet_mktemp(char *template)
{
	int fd;

	if ((fd = mkstemp(template)) == -1)
		return (NULL);
	close (fd);
	if (unlink(template) == -1)
		err(EX_OSERR, "unlink %s", template);
	return (template);
}

/*
 * do_link --
 *	make a hard link, obeying dorename if set
 *	return -1 on failure
 */
static int
do_link(const char *from_name, const char *to_name,
    const struct stat *target_sb)
{
	char tmpl[MAXPATHLEN];
	int ret;

	if (safecopy && target_sb != NULL) {
		(void)snprintf(tmpl, sizeof(tmpl), "%s.inst.XXXXXX", to_name);
		/* This usage is safe. */
		if (quiet_mktemp(tmpl) == NULL)
			err(EX_OSERR, "%s: mktemp", tmpl);
		ret = link(from_name, tmpl);
		if (ret == 0) {
			if (target_sb->st_mode & S_IFDIR && rmdir(to_name) ==
			    -1) {
				unlink(tmpl);
				err(EX_OSERR, "%s", to_name);
			}
#if HAVE_STRUCT_STAT_ST_FLAGS
			if (target_sb->st_flags & NOCHANGEBITS)
				(void)chflags(to_name, target_sb->st_flags &
				     ~NOCHANGEBITS);
#endif
			if (verbose)
				printf("install: link %s -> %s\n",
				    from_name, to_name);
			ret = rename(tmpl, to_name);
			/*
			 * If rename has posix semantics, then the temporary
			 * file may still exist when from_name and to_name point
			 * to the same file, so unlink it unconditionally.
			 */
			(void)unlink(tmpl);
		}
		return (ret);
	} else {
		if (verbose)
			printf("install: link %s -> %s\n",
			    from_name, to_name);
		return (link(from_name, to_name));
	}
}

/*
 * do_symlink --
 *	Make a symbolic link, obeying dorename if set. Exit on failure.
 */
static void
do_symlink(const char *from_name, const char *to_name,
    const struct stat *target_sb)
{
	char tmpl[MAXPATHLEN];

	if (safecopy && target_sb != NULL) {
		(void)snprintf(tmpl, sizeof(tmpl), "%s.inst.XXXXXX", to_name);
		/* This usage is safe. */
		if (quiet_mktemp(tmpl) == NULL)
			err(EX_OSERR, "%s: mktemp", tmpl);

		if (symlink(from_name, tmpl) == -1)
			err(EX_OSERR, "symlink %s -> %s", from_name, tmpl);

		if (target_sb->st_mode & S_IFDIR && rmdir(to_name) == -1) {
			(void)unlink(tmpl);
			err(EX_OSERR, "%s", to_name);
		}
#if HAVE_STRUCT_STAT_ST_FLAGS
		if (target_sb->st_flags & NOCHANGEBITS)
			(void)chflags(to_name, target_sb->st_flags &
			     ~NOCHANGEBITS);
#endif
		if (verbose)
			printf("install: symlink %s -> %s\n",
			    from_name, to_name);
		if (rename(tmpl, to_name) == -1) {
			/* Remove temporary link before exiting. */
			(void)unlink(tmpl);
			err(EX_OSERR, "%s: rename", to_name);
		}
	} else {
		if (verbose)
			printf("install: symlink %s -> %s\n",
			    from_name, to_name);
		if (symlink(from_name, to_name) == -1)
			err(EX_OSERR, "symlink %s -> %s", from_name, to_name);
	}
}

/*
 * makelink --
 *	make a link from source to destination
 */
static void
makelink(const char *from_name, const char *to_name,
    const struct stat *target_sb)
{
	char	src[MAXPATHLEN], dst[MAXPATHLEN], lnk[MAXPATHLEN];
	struct stat	to_sb;

	/* Try hard links first. */
	if (dolink & (LN_HARD|LN_MIXED)) {
		if (do_link(from_name, to_name, target_sb) == -1) {
			if ((dolink & LN_HARD) || errno != EXDEV)
				err(EX_OSERR, "link %s -> %s", from_name, to_name);
		} else {
			if (stat(to_name, &to_sb))
				err(EX_OSERR, "%s: stat", to_name);
			if (S_ISREG(to_sb.st_mode)) {
				/*
				 * XXX: hard links to anything other than
				 * plain files are not metalogged
				 */
				int omode;
				const char *oowner, *ogroup;
				char *offlags;
				char *dres;

				/*
				 * XXX: use underlying perms, unless
				 * overridden on command line.
				 */
				omode = mode;
				if (!haveopt_m)
					mode = (to_sb.st_mode & 0777);
				oowner = owner;
				if (!haveopt_o)
					owner = NULL;
				ogroup = group;
				if (!haveopt_g)
					group = NULL;
				offlags = fflags;
				if (!haveopt_f)
					fflags = NULL;
				dres = digest_file(from_name);
				metadata_log(to_name, "file", NULL, NULL,
				    dres, to_sb.st_size);
				free(dres);
				mode = omode;
				owner = oowner;
				group = ogroup;
				fflags = offlags;
			}
			return;
		}
	}

	/* Symbolic links. */
	if (dolink & LN_ABSOLUTE) {
		/* Convert source path to absolute. */
		if (realpath(from_name, src) == NULL)
			err(EX_OSERR, "%s: realpath", from_name);
		do_symlink(src, to_name, target_sb);
		/* XXX: src may point outside of destdir */
		metadata_log(to_name, "link", NULL, src, NULL, 0);
		return;
	}

	if (dolink & LN_RELATIVE) {
		char *to_name_copy, *cp, *d, *ld, *ls, *s;

		if (*from_name != '/') {
			/* this is already a relative link */
			do_symlink(from_name, to_name, target_sb);
			/* XXX: from_name may point outside of destdir. */
			metadata_log(to_name, "link", NULL, from_name, NULL, 0);
			return;
		}

		/* Resolve pathnames. */
		if (realpath(from_name, src) == NULL)
			err(EX_OSERR, "%s: realpath", from_name);

		/*
		 * The last component of to_name may be a symlink,
		 * so use realpath to resolve only the directory.
		 */
		to_name_copy = strdup(to_name);
		if (to_name_copy == NULL)
			err(EX_OSERR, "%s: strdup", to_name);
		cp = dirname(to_name_copy);
		if (realpath(cp, dst) == NULL)
			err(EX_OSERR, "%s: realpath", cp);
		/* .. and add the last component. */
		if (strcmp(dst, "/") != 0) {
			if (strlcat(dst, "/", sizeof(dst)) > sizeof(dst))
				errx(1, "resolved pathname too long");
		}
		strcpy(to_name_copy, to_name);
		cp = basename(to_name_copy);
		if (strlcat(dst, cp, sizeof(dst)) > sizeof(dst))
			errx(1, "resolved pathname too long");
		free(to_name_copy);

		/* Trim common path components. */
		ls = ld = NULL;
		for (s = src, d = dst; *s == *d; ls = s, ld = d, s++, d++)
			continue;
		/*
		 * If we didn't end after a directory separator, then we've
		 * falsely matched the last component.  For example, if one
		 * invoked install -lrs /lib/foo.so /libexec/ then the source
		 * would terminate just after the separator while the
		 * destination would terminate in the middle of 'libexec',
		 * leading to a full directory getting falsely eaten.
		 */
		if ((ls != NULL && *ls != '/') || (ld != NULL && *ld != '/'))
			s--, d--;
		while (*s != '/')
			s--, d--;

		/* Count the number of directories we need to backtrack. */
		for (++d, lnk[0] = '\0'; *d; d++)
			if (*d == '/')
				(void)strlcat(lnk, "../", sizeof(lnk));

		(void)strlcat(lnk, ++s, sizeof(lnk));

		do_symlink(lnk, to_name, target_sb);
		/* XXX: Link may point outside of destdir. */
		metadata_log(to_name, "link", NULL, lnk, NULL, 0);
		return;
	}

	/*
	 * If absolute or relative was not specified, try the names the
	 * user provided.
	 */
	do_symlink(from_name, to_name, target_sb);
	/* XXX: from_name may point outside of destdir. */
	metadata_log(to_name, "link", NULL, from_name, NULL, 0);
}

/*
 * install --
 *	build a path name and install the file
 */
static void
install(const char *from_name, const char *to_name, u_long fset, u_int flags)
{
	struct stat from_sb, temp_sb, to_sb;
	struct timespec tsb[2];
	int devnull, files_match, from_fd, serrno, target;
	int tempcopy, temp_fd, to_fd;
	char backup[MAXPATHLEN], *p, pathbuf[MAXPATHLEN], tempfile[MAXPATHLEN];
	char *digestresult;

	files_match = 0;
	from_fd = -1;
	to_fd = -1;

	/* If try to install NULL file to a directory, fails. */
	if (flags & DIRECTORY || strcmp(from_name, _PATH_DEVNULL)) {
		if (!dolink) {
			if (stat(from_name, &from_sb))
				err(EX_OSERR, "%s", from_name);
			if (!S_ISREG(from_sb.st_mode)) {
				errno = EFTYPE;
				err(EX_OSERR, "%s", from_name);
			}
		}
		/* Build the target path. */
		if (flags & DIRECTORY) {
			(void)snprintf(pathbuf, sizeof(pathbuf), "%s%s%s",
			    to_name,
			    to_name[strlen(to_name) - 1] == '/' ? "" : "/",
			    (p = strrchr(from_name, '/')) ? ++p : from_name);
			to_name = pathbuf;
		}
		devnull = 0;
	} else {
		devnull = 1;
	}

	target = (lstat(to_name, &to_sb) == 0);

	if (dolink) {
		if (target && !safecopy) {
			if (to_sb.st_mode & S_IFDIR && rmdir(to_name) == -1)
				err(EX_OSERR, "%s", to_name);
#if HAVE_STRUCT_STAT_ST_FLAGS
			if (to_sb.st_flags & NOCHANGEBITS)
				(void)chflags(to_name,
				    to_sb.st_flags & ~NOCHANGEBITS);
#endif
			unlink(to_name);
		}
		makelink(from_name, to_name, target ? &to_sb : NULL);
		return;
	}

	if (target && !S_ISREG(to_sb.st_mode) && !S_ISLNK(to_sb.st_mode)) {
		errno = EFTYPE;
		warn("%s", to_name);
		return;
	}

	/* Only copy safe if the target exists. */
	tempcopy = safecopy && target;

	if (!devnull && (from_fd = open(from_name, O_RDONLY, 0)) < 0)
		err(EX_OSERR, "%s", from_name);

	/* If we don't strip, we can compare first. */
	if (docompare && !dostrip && target && S_ISREG(to_sb.st_mode)) {
		if ((to_fd = open(to_name, O_RDONLY, 0)) < 0)
			err(EX_OSERR, "%s", to_name);
		if (devnull)
			files_match = to_sb.st_size == 0;
		else
			files_match = !(compare(from_fd, from_name,
			    (size_t)from_sb.st_size, to_fd,
			    to_name, (size_t)to_sb.st_size, &digestresult));

		/* Close "to" file unless we match. */
		if (!files_match)
			(void)close(to_fd);
	}

	if (!files_match) {
		if (tempcopy) {
			to_fd = create_tempfile(to_name, tempfile,
			    sizeof(tempfile));
			if (to_fd < 0)
				err(EX_OSERR, "%s", tempfile);
		} else {
			if ((to_fd = create_newfile(to_name, target,
			    &to_sb)) < 0)
				err(EX_OSERR, "%s", to_name);
			if (verbose)
				(void)printf("install: %s -> %s\n",
				    from_name, to_name);
		}
		if (!devnull)
			digestresult = copy(from_fd, from_name, to_fd,
			     tempcopy ? tempfile : to_name, from_sb.st_size);
		else
			digestresult = NULL;
	}

	if (dostrip) {
		strip(tempcopy ? tempfile : to_name);

		/*
		 * Re-open our fd on the target, in case we used a strip
		 * that does not work in-place -- like GNU binutils strip.
		 */
		close(to_fd);
		to_fd = open(tempcopy ? tempfile : to_name, O_RDONLY, 0);
		if (to_fd < 0)
			err(EX_OSERR, "stripping %s", to_name);
	}

	/*
	 * Compare the stripped temp file with the target.
	 */
	if (docompare && dostrip && target && S_ISREG(to_sb.st_mode)) {
		temp_fd = to_fd;

		/* Re-open to_fd using the real target name. */
		if ((to_fd = open(to_name, O_RDONLY, 0)) < 0)
			err(EX_OSERR, "%s", to_name);

		if (fstat(temp_fd, &temp_sb)) {
			serrno = errno;
			(void)unlink(tempfile);
			errno = serrno;
			err(EX_OSERR, "%s", tempfile);
		}

		if (compare(temp_fd, tempfile, (size_t)temp_sb.st_size, to_fd,
			    to_name, (size_t)to_sb.st_size, &digestresult)
			    == 0) {
			/*
			 * If target has more than one link we need to
			 * replace it in order to snap the extra links.
			 * Need to preserve target file times, though.
			 */
			if (to_sb.st_nlink != 1) {
				tsb[0] = to_sb.st_atim;
				tsb[1] = to_sb.st_mtim;
				(void)utimensat(AT_FDCWD, tempfile, tsb, 0);
			} else {
				files_match = 1;
				(void)unlink(tempfile);
			}
			(void) close(temp_fd);
		}
	} else if (dostrip)
		digestresult = digest_file(tempfile);

	/*
	 * Move the new file into place if doing a safe copy
	 * and the files are different (or just not compared).
	 */
	if (tempcopy && !files_match) {
#if HAVE_STRUCT_STAT_ST_FLAGS
		/* Try to turn off the immutable bits. */
		if (to_sb.st_flags & NOCHANGEBITS)
			(void)chflags(to_name, to_sb.st_flags & ~NOCHANGEBITS);
#endif
		if (dobackup) {
			if ((size_t)snprintf(backup, MAXPATHLEN, "%s%s", to_name,
			    suffix) != strlen(to_name) + strlen(suffix)) {
				unlink(tempfile);
				errx(EX_OSERR, "%s: backup filename too long",
				    to_name);
			}
			if (verbose)
				(void)printf("install: %s -> %s\n", to_name, backup);
			if (unlink(backup) < 0 && errno != ENOENT) {
				serrno = errno;
#if HAVE_STRUCT_STAT_ST_FLAGS
				if (to_sb.st_flags & NOCHANGEBITS)
					(void)chflags(to_name, to_sb.st_flags);
#endif
				unlink(tempfile);
				errno = serrno;
				err(EX_OSERR, "unlink: %s", backup);
			}
			if (link(to_name, backup) < 0) {
				serrno = errno;
				unlink(tempfile);
#if HAVE_STRUCT_STAT_ST_FLAGS
				if (to_sb.st_flags & NOCHANGEBITS)
					(void)chflags(to_name, to_sb.st_flags);
#endif
				errno = serrno;
				err(EX_OSERR, "link: %s to %s", to_name,
				     backup);
			}
		}
		if (verbose)
			(void)printf("install: %s -> %s\n", from_name, to_name);
		if (rename(tempfile, to_name) < 0) {
			serrno = errno;
			unlink(tempfile);
			errno = serrno;
			err(EX_OSERR, "rename: %s to %s",
			    tempfile, to_name);
		}

		/* Re-open to_fd so we aren't hosed by the rename(2). */
		(void) close(to_fd);
		if ((to_fd = open(to_name, O_RDONLY, 0)) < 0)
			err(EX_OSERR, "%s", to_name);
	}

	/*
	 * Preserve the timestamp of the source file if necessary.
	 */
	if (dopreserve && !files_match && !devnull) {
		tsb[0] = from_sb.st_atim;
		tsb[1] = from_sb.st_mtim;
		(void)utimensat(AT_FDCWD, to_name, tsb, 0);
	}

	if (fstat(to_fd, &to_sb) == -1) {
		serrno = errno;
		(void)unlink(to_name);
		errno = serrno;
		err(EX_OSERR, "%s", to_name);
	}

	/*
	 * Set owner, group, mode for target; do the chown first,
	 * chown may lose the setuid bits.
	 */
	if (!dounpriv && ((gid != (gid_t)-1 && gid != to_sb.st_gid) ||
	    (uid != (uid_t)-1 && uid != to_sb.st_uid) ||
	    (mode != (to_sb.st_mode & ALLPERMS)))) {
#if HAVE_STRUCT_STAT_ST_FLAGS
		/* Try to turn off the immutable bits. */
		if (to_sb.st_flags & NOCHANGEBITS)
			(void)fchflags(to_fd, to_sb.st_flags & ~NOCHANGEBITS);
#endif
	}

	if (!dounpriv & 
	    (gid != (gid_t)-1 && gid != to_sb.st_gid) ||
	    (uid != (uid_t)-1 && uid != to_sb.st_uid))
		if (fchown(to_fd, uid, gid) == -1) {
			serrno = errno;
			(void)unlink(to_name);
			errno = serrno;
			err(EX_OSERR,"%s: chown/chgrp", to_name);
		}

	if (mode != (to_sb.st_mode & ALLPERMS)) {
		if (fchmod(to_fd,
		     dounpriv ? mode & (S_IRWXU|S_IRWXG|S_IRWXO) : mode)) {
			serrno = errno;
			(void)unlink(to_name);
			errno = serrno;
			err(EX_OSERR, "%s: chmod", to_name);
		}
	}
#if HAVE_STRUCT_STAT_ST_FLAGS
	/*
	 * If provided a set of flags, set them, otherwise, preserve the
	 * flags, except for the dump flag.
	 * NFS does not support flags.  Ignore EOPNOTSUPP flags if we're just
	 * trying to turn off UF_NODUMP.  If we're trying to set real flags,
	 * then warn if the fs doesn't support it, otherwise fail.
	 */
	if (!dounpriv & !devnull && (flags & SETFLAGS ||
	    (from_sb.st_flags & ~UF_NODUMP) != to_sb.st_flags) &&
	    fchflags(to_fd,
	    flags & SETFLAGS ? fset : from_sb.st_flags & ~UF_NODUMP)) {
		if (flags & SETFLAGS) {
			if (errno == EOPNOTSUPP)
				warn("%s: chflags", to_name);
			else {
				serrno = errno;
				(void)unlink(to_name);
				errno = serrno;
				err(EX_OSERR, "%s: chflags", to_name);
			}
		}
	}
#endif

	(void)close(to_fd);
	if (!devnull)
		(void)close(from_fd);

	metadata_log(to_name, "file", tsb, NULL, digestresult, to_sb.st_size);
	free(digestresult);
}

/*
 * compare --
 *	compare two files; non-zero means files differ
 */
static int
compare(int from_fd, const char *from_name __unused, size_t from_len,
	int to_fd, const char *to_name __unused, size_t to_len,
	char **dresp)
{
	char *p, *q;
	int rv;
	int done_compare;
	DIGEST_CTX ctx;

	rv = 0;
	if (from_len != to_len)
		return 1;

	if (from_len <= MAX_CMP_SIZE) {
		if (dresp != NULL)
			digest_init(&ctx);
		done_compare = 0;
		if (trymmap(from_fd) && trymmap(to_fd)) {
			p = mmap(NULL, from_len, PROT_READ, MAP_SHARED,
			    from_fd, (off_t)0);
			if (p == MAP_FAILED)
				goto out;
			q = mmap(NULL, from_len, PROT_READ, MAP_SHARED,
			    to_fd, (off_t)0);
			if (q == MAP_FAILED) {
				munmap(p, from_len);
				goto out;
			}

			rv = memcmp(p, q, from_len);
			if (dresp != NULL)
				digest_update(&ctx, p, from_len);
			munmap(p, from_len);
			munmap(q, from_len);
			done_compare = 1;
		}
	out:
		if (!done_compare) {
			char buf1[MAXBSIZE];
			char buf2[MAXBSIZE];
			int n1, n2;

			rv = 0;
			lseek(from_fd, 0, SEEK_SET);
			lseek(to_fd, 0, SEEK_SET);
			while (rv == 0) {
				n1 = read(from_fd, buf1, sizeof(buf1));
				if (n1 == 0)
					break;		/* EOF */
				else if (n1 > 0) {
					n2 = read(to_fd, buf2, n1);
					if (n2 == n1)
						rv = memcmp(buf1, buf2, n1);
					else
						rv = 1;	/* out of sync */
				} else
					rv = 1;		/* read failure */
				digest_update(&ctx, buf1, n1);
			}
			lseek(from_fd, 0, SEEK_SET);
			lseek(to_fd, 0, SEEK_SET);
		}
	} else
		rv = 1;	/* don't bother in this case */

	if (dresp != NULL) {
		if (rv == 0)
			*dresp = digest_end(&ctx, NULL);
		else
			(void)digest_end(&ctx, NULL);
	}

	return rv;
}

/*
 * create_tempfile --
 *	create a temporary file based on path and open it
 */
static int
create_tempfile(const char *path, char *temp, size_t tsize)
{
	char *p;

	(void)strncpy(temp, path, tsize);
	temp[tsize - 1] = '\0';
	if ((p = strrchr(temp, '/')) != NULL)
		p++;
	else
		p = temp;
	(void)strncpy(p, "INS@XXXX", &temp[tsize - 1] - p);
	temp[tsize - 1] = '\0';
	return (mkstemp(temp));
}

/*
 * create_newfile --
 *	create a new file, overwriting an existing one if necessary
 */
static int
create_newfile(const char *path, int target, struct stat *sbp)
{
	char backup[MAXPATHLEN];
	int saved_errno = 0;
	int newfd;

	if (target) {
		/*
		 * Unlink now... avoid ETXTBSY errors later.  Try to turn
		 * off the append/immutable bits -- if we fail, go ahead,
		 * it might work.
		 */
#if HAVE_STRUCT_STAT_ST_FLAGS
		if (sbp->st_flags & NOCHANGEBITS)
			(void)chflags(path, sbp->st_flags & ~NOCHANGEBITS);
#endif

		if (dobackup) {
			if ((size_t)snprintf(backup, MAXPATHLEN, "%s%s",
			    path, suffix) != strlen(path) + strlen(suffix)) {
				saved_errno = errno;
#if HAVE_STRUCT_STAT_ST_FLAGS
				if (sbp->st_flags & NOCHANGEBITS)
					(void)chflags(path, sbp->st_flags);
#endif
				errno = saved_errno;
				errx(EX_OSERR, "%s: backup filename too long",
				    path);
			}
			(void)snprintf(backup, MAXPATHLEN, "%s%s",
			    path, suffix);
			if (verbose)
				(void)printf("install: %s -> %s\n",
				    path, backup);
			if (rename(path, backup) < 0) {
				saved_errno = errno;
#if HAVE_STRUCT_STAT_ST_FLAGS
				if (sbp->st_flags & NOCHANGEBITS)
					(void)chflags(path, sbp->st_flags);
#endif
				errno = saved_errno;
				err(EX_OSERR, "rename: %s to %s", path, backup);
			}
		} else
			if (unlink(path) < 0)
				saved_errno = errno;
	}

	newfd = open(path, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
	if (newfd < 0 && saved_errno != 0)
		errno = saved_errno;
	return newfd;
}

/*
 * copy --
 *	copy from one file to another
 */
static char *
copy(int from_fd, const char *from_name, int to_fd, const char *to_name,
    off_t size)
{
	int nr, nw;
	int serrno;
	char *p;
	char buf[MAXBSIZE];
	int done_copy;
	DIGEST_CTX ctx;

	/* Rewind file descriptors. */
	if (lseek(from_fd, (off_t)0, SEEK_SET) == (off_t)-1)
		err(EX_OSERR, "lseek: %s", from_name);
	if (lseek(to_fd, (off_t)0, SEEK_SET) == (off_t)-1)
		err(EX_OSERR, "lseek: %s", to_name);

	digest_init(&ctx);

	/*
	 * Mmap and write if less than 8M (the limit is so we don't totally
	 * trash memory on big files.  This is really a minor hack, but it
	 * wins some CPU back.
	 */
	done_copy = 0;
	if (size <= 8 * 1048576 && trymmap(from_fd) &&
	    (p = mmap(NULL, (size_t)size, PROT_READ, MAP_SHARED,
		    from_fd, (off_t)0)) != MAP_FAILED) {
		nw = write(to_fd, p, size);
		if (nw != size) {
			serrno = errno;
			(void)unlink(to_name);
			if (nw >= 0) {
				errx(EX_OSERR,
     "short write to %s: %jd bytes written, %jd bytes asked to write",
				    to_name, (uintmax_t)nw, (uintmax_t)size);
			} else {
				errno = serrno;
				err(EX_OSERR, "%s", to_name);
			}
		}
		digest_update(&ctx, p, size);
		(void)munmap(p, size);
		done_copy = 1;
	}
	if (!done_copy) {
		while ((nr = read(from_fd, buf, sizeof(buf))) > 0) {
			if ((nw = write(to_fd, buf, nr)) != nr) {
				serrno = errno;
				(void)unlink(to_name);
				if (nw >= 0) {
					errx(EX_OSERR,
     "short write to %s: %jd bytes written, %jd bytes asked to write",
					    to_name, (uintmax_t)nw,
					    (uintmax_t)size);
				} else {
					errno = serrno;
					err(EX_OSERR, "%s", to_name);
				}
			}
			digest_update(&ctx, buf, nr);
		}
		if (nr != 0) {
			serrno = errno;
			(void)unlink(to_name);
			errno = serrno;
			err(EX_OSERR, "%s", from_name);
		}
	}
	if (safecopy && fsync(to_fd) == -1) {
		serrno = errno;
		(void)unlink(to_name);
		errno = serrno;
		err(EX_OSERR, "fsync failed for %s", to_name);
	}
	return (digest_end(&ctx, NULL));
}

/*
 * strip --
 *	use strip(1) to strip the target file
 */
static void
strip(const char *to_name)
{
	const char *stripbin;
	const char *args[3];
	pid_t pid;
	int error, status;

	stripbin = getenv("STRIPBIN");
	if (stripbin == NULL)
		stripbin = "strip";
	args[0] = stripbin;
	args[1] = to_name;
	args[2] = NULL;
	error = posix_spawnp(&pid, stripbin, NULL, NULL,
	    __DECONST(char **, args), environ);
	if (error != 0) {
		(void)unlink(to_name);
		errc(error == EAGAIN || error == EPROCLIM || error == ENOMEM ?
		    EX_TEMPFAIL : EX_OSERR, error, "spawn %s", stripbin);
	}
	if (waitpid(pid, &status, 0) == -1) {
		error = errno;
		(void)unlink(to_name);
		errc(EX_SOFTWARE, error, "wait");
		/* NOTREACHED */
	}
	if (status != 0) {
		(void)unlink(to_name);
		errx(EX_SOFTWARE, "strip command %s failed on %s",
		    stripbin, to_name);
	}
}

/*
 * install_dir --
 *	build directory hierarchy
 */
static void
install_dir(char *path)
{
	char *p;
	struct stat sb;
	int ch, tried_mkdir;

	for (p = path;; ++p)
		if (!*p || (p != path && *p  == '/')) {
			tried_mkdir = 0;
			ch = *p;
			*p = '\0';
again:
			if (stat(path, &sb) < 0) {
				if (errno != ENOENT || tried_mkdir)
					err(EX_OSERR, "stat %s", path);
				if (mkdir(path, 0755) < 0) {
					tried_mkdir = 1;
					if (errno == EEXIST)
						goto again;
					err(EX_OSERR, "mkdir %s", path);
				}
				if (verbose)
					(void)printf("install: mkdir %s\n",
					    path);
			} else if (!S_ISDIR(sb.st_mode))
				errx(EX_OSERR, "%s exists but is not a directory", path);
			if (!(*p = ch))
				break;
 		}

	if (!dounpriv) {
		if ((gid != (gid_t)-1 || uid != (uid_t)-1) &&
		    chown(path, uid, gid))
			warn("chown %u:%u %s", uid, gid, path);
		/* XXXBED: should we do the chmod in the dounpriv case? */
		if (chmod(path, mode))
			warn("chmod %o %s", mode, path);
	}
	metadata_log(path, "dir", NULL, NULL, NULL, 0);
}

/*
 * metadata_log --
 *	if metafp is not NULL, output mtree(8) full path name and settings to
 *	metafp, to allow permissions to be set correctly by other tools,
 *	or to allow integrity checks to be performed.
 */
static void
metadata_log(const char *path, const char *type, struct timespec *ts,
	const char *slink, const char *digestresult, off_t size)
{
	static const char extra[] = { ' ', '\t', '\n', '\\', '#', '\0' };
	const char *p;
	char *buf;
	size_t destlen;
	struct flock metalog_lock;

	if (!metafp)	
		return;
	/* Buffer for strsvis(3). */
	buf = (char *)malloc(4 * strlen(path) + 1);
	if (buf == NULL) {
		warnx("%s", strerror(ENOMEM));
		return;
	}

	/* Lock log file. */
	metalog_lock.l_start = 0;
	metalog_lock.l_len = 0;
	metalog_lock.l_whence = SEEK_SET;
	metalog_lock.l_type = F_WRLCK;
	if (fcntl(fileno(metafp), F_SETLKW, &metalog_lock) == -1) {
		warn("can't lock %s", metafile);
		free(buf);
		return;
	}

	/* Remove destdir. */
	p = path;
	if (destdir) {
		destlen = strlen(destdir);
		if (strncmp(p, destdir, destlen) == 0 &&
		    (p[destlen] == '/' || p[destlen] == '\0'))
			p += destlen;
	}
	while (*p && *p == '/')
		p++;
	strsvis(buf, p, VIS_OCTAL, extra);
	p = buf;
	/* Print details. */
	fprintf(metafp, ".%s%s type=%s", *p ? "/" : "", p, type);
	if (owner)
		fprintf(metafp, " uname=%s", owner);
	if (group)
		fprintf(metafp, " gname=%s", group);
	fprintf(metafp, " mode=%#o", mode);
	if (slink) {
		strsvis(buf, slink, VIS_CSTYLE, extra);	/* encode link */
		fprintf(metafp, " link=%s", buf);
	}
	if (*type == 'f') /* type=file */
		fprintf(metafp, " size=%lld", (long long)size);
	if (ts != NULL && dopreserve)
		fprintf(metafp, " time=%lld.%09ld",
			(long long)ts[1].tv_sec, ts[1].tv_nsec);
	if (digestresult && digest)
		fprintf(metafp, " %s=%s", digest, digestresult);
	if (fflags)
		fprintf(metafp, " flags=%s", fflags);
	if (tags)
		fprintf(metafp, " tags=%s", tags);
	fputc('\n', metafp);
	/* Flush line. */
	fflush(metafp);

	/* Unlock log file. */
	metalog_lock.l_type = F_UNLCK;
	if (fcntl(fileno(metafp), F_SETLKW, &metalog_lock) == -1)
		warn("can't unlock %s", metafile);
	free(buf);
}

/*
 * usage --
 *	print a usage message and die
 */
static void
usage(void)
{
	(void)fprintf(stderr,
"usage: install [-bCcpSsUv] [-f flags] [-g group] [-m mode] [-o owner]\n"
"               [-M log] [-D dest] [-h hash] [-T tags]\n"
"               [-B suffix] [-l linkflags] [-N dbdir]\n"
"               file1 file2\n"
"       install [-bCcpSsUv] [-f flags] [-g group] [-m mode] [-o owner]\n"
"               [-M log] [-D dest] [-h hash] [-T tags]\n"
"               [-B suffix] [-l linkflags] [-N dbdir]\n"
"               file1 ... fileN directory\n"
"       install -dU [-vU] [-g group] [-m mode] [-N dbdir] [-o owner]\n"
"               [-M log] [-D dest] [-h hash] [-T tags]\n"
"               directory ...\n");
	exit(EX_USAGE);
	/* NOTREACHED */
}

/*
 * trymmap --
 *	return true (1) if mmap should be tried, false (0) if not.
 */
static int
trymmap(int fd)
{
/*
 * The ifdef is for bootstrapping - f_fstypename doesn't exist in
 * pre-Lite2-merge systems.
 */
#ifdef MFSNAMELEN
	struct statfs stfs;

	if (fstatfs(fd, &stfs) != 0)
		return (0);
	if (strcmp(stfs.f_fstypename, "ufs") == 0 ||
	    strcmp(stfs.f_fstypename, "cd9660") == 0)
		return (1);
#endif
	return (0);
}
