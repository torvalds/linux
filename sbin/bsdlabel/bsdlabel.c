/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994, 1995 Gordon W. Ross
 * Copyright (c) 1994 Theo de Raadt
 * All rights reserved.
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Symmetric Computer Systems.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *      This product includes software developed by Theo de Raadt.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 *	from: $NetBSD: disksubr.c,v 1.13 2000/12/17 22:39:18 pk $
 */

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1987, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)disklabel.c	8.2 (Berkeley) 1/7/94";
/* from static char sccsid[] = "@(#)disklabel.c	1.2 (Symmetric) 11/28/85"; */
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <stdint.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/disk.h>
#define DKTYPENAMES
#define FSTYPENAMES
#define MAXPARTITIONS	20
#include <sys/disklabel.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <libgeom.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>

#include "pathnames.h"

static void	makelabel(const char *, struct disklabel *);
static int	geom_class_available(const char *);
static int	writelabel(void);
static int	readlabel(int flag);
static void	display(FILE *, const struct disklabel *);
static int	edit(void);
static int	editit(void);
static void	fixlabel(struct disklabel *);
static char	*skip(char *);
static char	*word(char *);
static int	getasciilabel(FILE *, struct disklabel *);
static int	getasciipartspec(char *, struct disklabel *, int, int);
static int	checklabel(struct disklabel *);
static void	usage(void);
static struct disklabel *getvirginlabel(void);

#define	DEFEDITOR	_PATH_VI
#define	DEFPARTITIONS	8

static char	*specname;
static char	*pname;
static char	tmpfil[] = PATH_TMPFILE;

static struct	disklabel lab;
static u_char	bootarea[BBSIZE];
static off_t	mediasize;
static ssize_t	secsize;
static char	blank[] = "";
static char	unknown[] = "unknown";

#define MAX_PART ('z')
#define MAX_NUM_PARTS (1 + MAX_PART - 'a')
static char    part_size_type[MAX_NUM_PARTS];
static char    part_offset_type[MAX_NUM_PARTS];
static int     part_set[MAX_NUM_PARTS];

static int	installboot;	/* non-zero if we should install a boot program */
static int	allfields;	/* present all fields in edit */
static char const *xxboot;	/* primary boot */

static uint32_t lba_offset;
#ifndef LABELSECTOR
#define LABELSECTOR -1
#endif
#ifndef LABELOFFSET
#define LABELOFFSET -1
#endif
static int labelsoffset = LABELSECTOR;
static int labeloffset = LABELOFFSET;
static int bbsize = BBSIZE;

static enum {
	UNSPEC, EDIT, READ, RESTORE, WRITE, WRITEBOOT
} op = UNSPEC;


static int	disable_write;   /* set to disable writing to disk label */
static int	is_file;	/* work on a file (abs. pathname), "-f" opt. */

int
main(int argc, char *argv[])
{
	FILE *t;
	int ch, error, fd;
	const char *name;
	
	error = 0;
	name = NULL;

	while ((ch = getopt(argc, argv, "ABb:efm:nRrw")) != -1)
		switch (ch) {
			case 'A':
				allfields = 1;
				break;
			case 'B':
				++installboot;
				break;
			case 'b':
				xxboot = optarg;
				break;
			case 'f':
				is_file=1;
				break;
			case 'm':
				if (!strcmp(optarg, "i386") ||
				    !strcmp(optarg, "amd64")) {
					labelsoffset = 1;
					labeloffset = 0;
					bbsize = 8192;
				} else {
					errx(1, "Unsupported architecture");
				}
				break;
			case 'n':
				disable_write = 1;
				break;
			case 'R':
				if (op != UNSPEC)
					usage();
				op = RESTORE;
				break;
			case 'e':
				if (op != UNSPEC)
					usage();
				op = EDIT;
				break;
			case 'r':
				/*
				 * We accept and ignore -r for compatibility with
				 * historical disklabel usage.
				 */
				break;
			case 'w':
				if (op != UNSPEC)
					usage();
				op = WRITE;
				break;
			case '?':
			default:
				usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();
	if (labelsoffset < 0 || labeloffset < 0)
		errx(1, "a -m <architecture> option must be specified");

	/* Figure out the names of the thing we're working on */
	if (is_file) {
		specname = argv[0];
	} else {
		specname = g_device_path(argv[0]);
		if (specname == NULL) {
			warn("unable to get correct path for %s", argv[0]);
			return(1);
		}
		fd = open(specname, O_RDONLY);
		if (fd < 0) {
			warn("error opening %s", specname);
			return(1);
		}
		pname = g_providername(fd);
		if (pname == NULL) {
			warn("error getting providername for %s", specname);
			close(fd);
			return(1);
		}
		close(fd);
	}

	if (installboot && op == UNSPEC)
		op = WRITEBOOT;
	else if (op == UNSPEC)
		op = READ;

	switch(op) {

	case UNSPEC:
		break;

	case EDIT:
		if (argc != 1)
			usage();
		readlabel(1);
		fixlabel(&lab);
		error = edit();
		break;

	case READ:
		if (argc != 1)
			usage();
		readlabel(1);
		display(stdout, NULL);
		error = checklabel(NULL);
		break;

	case RESTORE:
		if (argc != 2)
			usage();
		if (!(t = fopen(argv[1], "r")))
			err(4, "fopen %s", argv[1]);
		readlabel(0);
		if (!getasciilabel(t, &lab))
			exit(1);
		error = writelabel();
		break;

	case WRITE:
		if (argc == 2)
			name = argv[1];
		else if (argc == 1)
			name = "auto";
		else
			usage();
		readlabel(0);
		makelabel(name, &lab);
		fixlabel(&lab);
		if (checklabel(NULL) == 0)
			error = writelabel();
		break;

	case WRITEBOOT:

		readlabel(1);
		fixlabel(&lab);
		if (argc == 2)
			makelabel(argv[1], &lab);
		if (checklabel(NULL) == 0)
			error = writelabel();
		break;
	}
	exit(error);
}

static void
fixlabel(struct disklabel *lp)
{
	struct partition *dp;
	int i;

	for (i = 0; i < lp->d_npartitions; i++) {
		if (i == RAW_PART)
			continue;
		if (lp->d_partitions[i].p_size)
			return;
	}

	dp = &lp->d_partitions[0];
	dp->p_offset = BBSIZE / secsize;
	dp->p_size = lp->d_secperunit - dp->p_offset;
}

/*
 * Construct a prototype disklabel from /etc/disktab.
 */
static void
makelabel(const char *type, struct disklabel *lp)
{
	struct disklabel *dp;

	if (strcmp(type, "auto") == 0)
		dp = getvirginlabel();
	else
		dp = getdiskbyname(type);
	if (dp == NULL)
		errx(1, "%s: unknown disk type", type);
	*lp = *dp;
	bzero(lp->d_packname, sizeof(lp->d_packname));
}

static void
readboot(void)
{
	int fd;
	struct stat st;

	if (xxboot == NULL)
		xxboot = "/boot/boot";
	fd = open(xxboot, O_RDONLY);
	if (fd < 0)
		err(1, "cannot open %s", xxboot);
	fstat(fd, &st);
	if (st.st_size <= BBSIZE) {
		if (read(fd, bootarea, st.st_size) != st.st_size)
			err(1, "read error %s", xxboot);
		close(fd);
		return;
	}
	errx(1, "boot code %s is wrong size", xxboot);
}

static int
geom_class_available(const char *name)
{
	struct gclass *class;
	struct gmesh mesh;
	int error;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(1, error, "Cannot get GEOM tree");

	LIST_FOREACH(class, &mesh.lg_class, lg_class) {
		if (strcmp(class->lg_name, name) == 0) {
			geom_deletetree(&mesh);
			return (1);
		}
	}

	geom_deletetree(&mesh);

	return (0);
}

static int
writelabel(void)
{
	int i, fd, serrno;
	struct gctl_req *grq;
	char const *errstr;
	struct disklabel *lp = &lab;

	if (disable_write) {
		warnx("write to disk label suppressed - label was as follows:");
		display(stdout, NULL);
		return (0);
	}

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);
	if (installboot)
		readboot();
	for (i = 0; i < lab.d_npartitions; i++)
		if (lab.d_partitions[i].p_size)
			lab.d_partitions[i].p_offset += lba_offset;
	bsd_disklabel_le_enc(bootarea + labeloffset + labelsoffset * lab.d_secsize,
	    lp);

	fd = open(specname, O_RDWR);
	if (fd < 0) {
		if (is_file) {
			warn("cannot open file %s for writing label", specname);
			return(1);
		} else
			serrno = errno;

		if (geom_class_available("PART") != 0) {
			/*
			 * Since we weren't able open provider for
			 * writing, then recommend user to use gpart(8).
			 */
			warnc(serrno,
			    "cannot open provider %s for writing label",
			    specname);
			warnx("Try to use gpart(8).");
			return (1);
		}

		/* Give up if GEOM_BSD is not available. */
		if (geom_class_available("BSD") == 0) {
			warnc(serrno, "%s", specname);
			return (1);
		}

		grq = gctl_get_handle();
		gctl_ro_param(grq, "verb", -1, "write label");
		gctl_ro_param(grq, "class", -1, "BSD");
		gctl_ro_param(grq, "geom", -1, pname);
		gctl_ro_param(grq, "label", 148+16*8,
			bootarea + labeloffset + labelsoffset * lab.d_secsize);
		errstr = gctl_issue(grq);
		if (errstr != NULL) {
			warnx("%s", errstr);
			gctl_free(grq);
			return(1);
		}
		gctl_free(grq);
		if (installboot) {
			grq = gctl_get_handle();
			gctl_ro_param(grq, "verb", -1, "write bootcode");
			gctl_ro_param(grq, "class", -1, "BSD");
			gctl_ro_param(grq, "geom", -1, pname);
			gctl_ro_param(grq, "bootcode", BBSIZE, bootarea);
			errstr = gctl_issue(grq);
			if (errstr != NULL) {
				warnx("%s", errstr);
				gctl_free(grq);
				return (1);
			}
			gctl_free(grq);
		}
	} else {
		if (write(fd, bootarea, bbsize) != bbsize) {
			warn("write %s", specname);
			close (fd);
			return (1);
		}
		close (fd);
	}
	return (0);
}

static void
get_file_parms(int f)
{
	int i;
	struct stat sb;

	if (fstat(f, &sb) != 0)
		err(4, "fstat failed");
	i = sb.st_mode & S_IFMT;
	if (i != S_IFREG && i != S_IFLNK)
		errx(4, "%s is not a valid file or link", specname);
	secsize = DEV_BSIZE;
	mediasize = sb.st_size;
}

/*
 * Fetch disklabel for disk.
 */
static int
readlabel(int flag)
{
	ssize_t nbytes;
	uint32_t lba;
	int f, i;
	int error;

	f = open(specname, O_RDONLY);
	if (f < 0)
		err(1, "%s", specname);
	if (is_file)
		get_file_parms(f);
	else {
		mediasize = g_mediasize(f);
		secsize = g_sectorsize(f);
		if (secsize < 0 || mediasize < 0)
			err(4, "cannot get disk geometry");
	}
	if (mediasize > (off_t)0xffffffff * secsize)
		errx(1,
		    "disks with more than 2^32-1 sectors are not supported");
	(void)lseek(f, (off_t)0, SEEK_SET);
	nbytes = read(f, bootarea, BBSIZE);
	if (nbytes == -1)
		err(4, "%s read", specname);
	if (nbytes != BBSIZE)
		errx(4, "couldn't read %d bytes from %s", BBSIZE, specname);
	close (f);
	error = bsd_disklabel_le_dec(
	    bootarea + (labeloffset + labelsoffset * secsize),
	    &lab, MAXPARTITIONS);
	if (flag && error)
		errx(1, "%s: no valid label found", specname);

	if (is_file)
		return(0);

	/*
	 * Compensate for absolute block addressing by finding the
	 * smallest partition offset and if the offset of the 'c'
	 * partition is equal to that, subtract it from all offsets.
	 */
	lba = ~0;
	for (i = 0; i < lab.d_npartitions; i++) {
		if (lab.d_partitions[i].p_size)
			lba = MIN(lba, lab.d_partitions[i].p_offset);
	}
	if (lba != 0 && lab.d_partitions[RAW_PART].p_offset == lba) {
		for (i = 0; i < lab.d_npartitions; i++) {
			if (lab.d_partitions[i].p_size)
				lab.d_partitions[i].p_offset -= lba;
		}
		/*
		 * Save the offset so that we can write the label
		 * back with absolute block addresses.
		 */
		lba_offset = lba;
	}
	return (error);
}


static void
display(FILE *f, const struct disklabel *lp)
{
	int i, j;
	const struct partition *pp;

	if (lp == NULL)
		lp = &lab;

	fprintf(f, "# %s:\n", specname);
	if (allfields) {
		if (lp->d_type < DKMAXTYPES)
			fprintf(f, "type: %s\n", dktypenames[lp->d_type]);
		else
			fprintf(f, "type: %u\n", lp->d_type);
		fprintf(f, "disk: %.*s\n", (int)sizeof(lp->d_typename),
			lp->d_typename);
		fprintf(f, "label: %.*s\n", (int)sizeof(lp->d_packname),
			lp->d_packname);
		fprintf(f, "flags:");
		if (lp->d_flags & D_REMOVABLE)
			fprintf(f, " removeable");
		if (lp->d_flags & D_ECC)
			fprintf(f, " ecc");
		if (lp->d_flags & D_BADSECT)
			fprintf(f, " badsect");
		fprintf(f, "\n");
		fprintf(f, "bytes/sector: %lu\n", (u_long)lp->d_secsize);
		fprintf(f, "sectors/track: %lu\n", (u_long)lp->d_nsectors);
		fprintf(f, "tracks/cylinder: %lu\n", (u_long)lp->d_ntracks);
		fprintf(f, "sectors/cylinder: %lu\n", (u_long)lp->d_secpercyl);
		fprintf(f, "cylinders: %lu\n", (u_long)lp->d_ncylinders);
		fprintf(f, "sectors/unit: %lu\n", (u_long)lp->d_secperunit);
		fprintf(f, "rpm: %u\n", lp->d_rpm);
		fprintf(f, "interleave: %u\n", lp->d_interleave);
		fprintf(f, "trackskew: %u\n", lp->d_trackskew);
		fprintf(f, "cylinderskew: %u\n", lp->d_cylskew);
		fprintf(f, "headswitch: %lu\t\t# milliseconds\n",
		    (u_long)lp->d_headswitch);
		fprintf(f, "track-to-track seek: %ld\t# milliseconds\n",
		    (u_long)lp->d_trkseek);
		fprintf(f, "drivedata: ");
		for (i = NDDATA - 1; i >= 0; i--)
			if (lp->d_drivedata[i])
				break;
		if (i < 0)
			i = 0;
		for (j = 0; j <= i; j++)
			fprintf(f, "%lu ", (u_long)lp->d_drivedata[j]);
		fprintf(f, "\n\n");
	}
	fprintf(f, "%u partitions:\n", lp->d_npartitions);
	fprintf(f,
	    "#          size     offset    fstype   [fsize bsize bps/cpg]\n");
	pp = lp->d_partitions;
	for (i = 0; i < lp->d_npartitions; i++, pp++) {
		if (pp->p_size) {
			fprintf(f, "  %c: %10lu %10lu  ", 'a' + i,
			   (u_long)pp->p_size, (u_long)pp->p_offset);
			if (pp->p_fstype < FSMAXTYPES)
				fprintf(f, "%8.8s", fstypenames[pp->p_fstype]);
			else
				fprintf(f, "%8d", pp->p_fstype);
			switch (pp->p_fstype) {

			case FS_UNUSED:				/* XXX */
				fprintf(f, "    %5lu %5lu %2s",
				    (u_long)pp->p_fsize,
				    (u_long)(pp->p_fsize * pp->p_frag), "");
				break;

			case FS_BSDFFS:
				fprintf(f, "    %5lu %5lu %5u",
				    (u_long)pp->p_fsize,
				    (u_long)(pp->p_fsize * pp->p_frag),
				    pp->p_cpg);
				break;

			case FS_BSDLFS:
				fprintf(f, "    %5lu %5lu %5d",
				    (u_long)pp->p_fsize,
				    (u_long)(pp->p_fsize * pp->p_frag),
				    pp->p_cpg);
				break;

			default:
				fprintf(f, "%20.20s", "");
				break;
			}
			if (i == RAW_PART) {
				fprintf(f, "  # \"raw\" part, don't edit");
			}
			fprintf(f, "\n");
		}
	}
	fflush(f);
}

static int
edit(void)
{
	int c, fd;
	struct disklabel label;
	FILE *fp;

	if ((fd = mkstemp(tmpfil)) == -1 ||
	    (fp = fdopen(fd, "w")) == NULL) {
		warnx("can't create %s", tmpfil);
		return (1);
	}
	display(fp, NULL);
	fclose(fp);
	for (;;) {
		if (!editit())
			break;
		fp = fopen(tmpfil, "r");
		if (fp == NULL) {
			warnx("can't reopen %s for reading", tmpfil);
			break;
		}
		bzero((char *)&label, sizeof(label));
		c = getasciilabel(fp, &label);
		fclose(fp);
		if (c) {
			lab = label;
			if (writelabel() == 0) {
				(void) unlink(tmpfil);
				return (0);
			}
		}
		printf("re-edit the label? [y]: ");
		fflush(stdout);
		c = getchar();
		if (c != EOF && c != (int)'\n')
			while (getchar() != (int)'\n')
				;
		if  (c == (int)'n')
			break;
	}
	(void) unlink(tmpfil);
	return (1);
}

static int
editit(void)
{
	int pid, xpid;
	int locstat, omask;
	const char *ed;
	uid_t uid;
	gid_t gid;

	omask = sigblock(sigmask(SIGINT)|sigmask(SIGQUIT)|sigmask(SIGHUP));
	while ((pid = fork()) < 0) {
		if (errno == EPROCLIM) {
			warnx("you have too many processes");
			return(0);
		}
		if (errno != EAGAIN) {
			warn("fork");
			return(0);
		}
		sleep(1);
	}
	if (pid == 0) {
		sigsetmask(omask);
		gid = getgid();
		if (setresgid(gid, gid, gid) == -1)
			err(1, "setresgid");
		uid = getuid();
		if (setresuid(uid, uid, uid) == -1)
			err(1, "setresuid");
		if ((ed = getenv("EDITOR")) == (char *)0)
			ed = DEFEDITOR;
		execlp(ed, ed, tmpfil, (char *)0);
		err(1, "%s", ed);
	}
	while ((xpid = wait(&locstat)) >= 0)
		if (xpid == pid)
			break;
	sigsetmask(omask);
	return(!locstat);
}

static char *
skip(char *cp)
{

	while (*cp != '\0' && isspace(*cp))
		cp++;
	if (*cp == '\0' || *cp == '#')
		return (NULL);
	return (cp);
}

static char *
word(char *cp)
{
	char c;

	while (*cp != '\0' && !isspace(*cp) && *cp != '#')
		cp++;
	if ((c = *cp) != '\0') {
		*cp++ = '\0';
		if (c != '#')
			return (skip(cp));
	}
	return (NULL);
}

/*
 * Read an ascii label in from fd f,
 * in the same format as that put out by display(),
 * and fill in lp.
 */
static int
getasciilabel(FILE *f, struct disklabel *lp)
{
	char *cp, *endp;
	const char **cpp;
	u_int part;
	char *tp, line[BUFSIZ];
	u_long v;
	int lineno = 0, errors = 0;
	int i;

	makelabel("auto", lp);
	bzero(&part_set, sizeof(part_set));
	bzero(&part_size_type, sizeof(part_size_type));
	bzero(&part_offset_type, sizeof(part_offset_type));
	lp->d_bbsize = BBSIZE;				/* XXX */
	lp->d_sbsize = 0;				/* XXX */
	while (fgets(line, sizeof(line) - 1, f)) {
		lineno++;
		if ((cp = strchr(line,'\n')) != NULL)
			*cp = '\0';
		cp = skip(line);
		if (cp == NULL)
			continue;
		tp = strchr(cp, ':');
		if (tp == NULL) {
			fprintf(stderr, "line %d: syntax error\n", lineno);
			errors++;
			continue;
		}
		*tp++ = '\0', tp = skip(tp);
		if (!strcmp(cp, "type")) {
			if (tp == NULL)
				tp = unknown;
			cpp = dktypenames;
			for (; cpp < &dktypenames[DKMAXTYPES]; cpp++)
				if (*cpp && !strcmp(*cpp, tp)) {
					lp->d_type = cpp - dktypenames;
					break;
				}
			if (cpp < &dktypenames[DKMAXTYPES])
				continue;
			errno = 0;
			v = strtoul(tp, &endp, 10);
			if (errno != 0 || *endp != '\0')
				v = DKMAXTYPES;
			if (v >= DKMAXTYPES)
				fprintf(stderr, "line %d:%s %lu\n", lineno,
				    "Warning, unknown disk type", v);
			else
				lp->d_type = v;
			continue;
		}
		if (!strcmp(cp, "flags")) {
			for (v = 0; (cp = tp) && *cp != '\0';) {
				tp = word(cp);
				if (!strcmp(cp, "removeable"))
					v |= D_REMOVABLE;
				else if (!strcmp(cp, "ecc"))
					v |= D_ECC;
				else if (!strcmp(cp, "badsect"))
					v |= D_BADSECT;
				else {
					fprintf(stderr,
					    "line %d: %s: bad flag\n",
					    lineno, cp);
					errors++;
				}
			}
			lp->d_flags = v;
			continue;
		}
		if (!strcmp(cp, "drivedata")) {
			for (i = 0; (cp = tp) && *cp != '\0' && i < NDDATA;) {
				lp->d_drivedata[i++] = strtoul(cp, NULL, 10);
				tp = word(cp);
			}
			continue;
		}
		if (sscanf(cp, "%lu partitions", &v) == 1) {
			if (v > MAXPARTITIONS) {
				fprintf(stderr,
				    "line %d: bad # of partitions\n", lineno);
				lp->d_npartitions = MAXPARTITIONS;
				errors++;
			} else if (v < DEFPARTITIONS) {
				fprintf(stderr,
				    "line %d: bad # of partitions\n", lineno);
				lp->d_npartitions = DEFPARTITIONS;
				errors++;
			} else
				lp->d_npartitions = v;
			continue;
		}
		if (tp == NULL)
			tp = blank;
		if (!strcmp(cp, "disk")) {
			strncpy(lp->d_typename, tp, sizeof (lp->d_typename));
			continue;
		}
		if (!strcmp(cp, "label")) {
			strncpy(lp->d_packname, tp, sizeof (lp->d_packname));
			continue;
		}
		if (!strcmp(cp, "bytes/sector")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0 || (v % DEV_BSIZE) != 0) {
				fprintf(stderr,
				    "line %d: %s: bad sector size\n",
				    lineno, tp);
				errors++;
			} else
				lp->d_secsize = v;
			continue;
		}
		if (!strcmp(cp, "sectors/track")) {
			v = strtoul(tp, NULL, 10);
#if (ULONG_MAX != 0xffffffffUL)
			if (v == 0 || v > 0xffffffff)
#else
			if (v == 0)
#endif
			{
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_nsectors = v;
			continue;
		}
		if (!strcmp(cp, "sectors/cylinder")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_secpercyl = v;
			continue;
		}
		if (!strcmp(cp, "tracks/cylinder")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_ntracks = v;
			continue;
		}
		if (!strcmp(cp, "cylinders")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_ncylinders = v;
			continue;
		}
		if (!strcmp(cp, "sectors/unit")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_secperunit = v;
			continue;
		}
		if (!strcmp(cp, "rpm")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0 || v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_rpm = v;
			continue;
		}
		if (!strcmp(cp, "interleave")) {
			v = strtoul(tp, NULL, 10);
			if (v == 0 || v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_interleave = v;
			continue;
		}
		if (!strcmp(cp, "trackskew")) {
			v = strtoul(tp, NULL, 10);
			if (v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_trackskew = v;
			continue;
		}
		if (!strcmp(cp, "cylinderskew")) {
			v = strtoul(tp, NULL, 10);
			if (v > USHRT_MAX) {
				fprintf(stderr, "line %d: %s: bad %s\n",
				    lineno, tp, cp);
				errors++;
			} else
				lp->d_cylskew = v;
			continue;
		}
		if (!strcmp(cp, "headswitch")) {
			v = strtoul(tp, NULL, 10);
			lp->d_headswitch = v;
			continue;
		}
		if (!strcmp(cp, "track-to-track seek")) {
			v = strtoul(tp, NULL, 10);
			lp->d_trkseek = v;
			continue;
		}
		/* the ':' was removed above */
		if (*cp < 'a' || *cp > MAX_PART || cp[1] != '\0') {
			fprintf(stderr,
			    "line %d: %s: Unknown disklabel field\n", lineno,
			    cp);
			errors++;
			continue;
		}

		/* Process a partition specification line. */
		part = *cp - 'a';
		if (part >= lp->d_npartitions) {
			fprintf(stderr,
			    "line %d: partition name out of range a-%c: %s\n",
			    lineno, 'a' + lp->d_npartitions - 1, cp);
			errors++;
			continue;
		}
		part_set[part] = 1;

		if (getasciipartspec(tp, lp, part, lineno) != 0) {
			errors++;
			break;
		}
	}
	errors += checklabel(lp);
	return (errors == 0);
}

#define NXTNUM(n) do { \
	if (tp == NULL) { \
		fprintf(stderr, "line %d: too few numeric fields\n", lineno); \
		return (1); \
	} else { \
		cp = tp, tp = word(cp); \
		(n) = strtoul(cp, NULL, 10); \
	} \
} while (0)

/* retain 1 character following number */
#define NXTWORD(w,n) do { \
	if (tp == NULL) { \
		fprintf(stderr, "line %d: too few numeric fields\n", lineno); \
		return (1); \
	} else { \
		char *tmp; \
		cp = tp, tp = word(cp); \
		(n) = strtoul(cp, &tmp, 10); \
		if (tmp) (w) = *tmp; \
	} \
} while (0)

/*
 * Read a partition line into partition `part' in the specified disklabel.
 * Return 0 on success, 1 on failure.
 */
static int
getasciipartspec(char *tp, struct disklabel *lp, int part, int lineno)
{
	struct partition *pp;
	char *cp, *endp;
	const char **cpp;
	u_long v;

	pp = &lp->d_partitions[part];
	cp = NULL;

	v = 0;
	NXTWORD(part_size_type[part],v);
	if (v == 0 && part_size_type[part] != '*') {
		fprintf(stderr,
		    "line %d: %s: bad partition size\n", lineno, cp);
		return (1);
	}
	pp->p_size = v;

	v = 0;
	NXTWORD(part_offset_type[part],v);
	if (v == 0 && part_offset_type[part] != '*' &&
	    part_offset_type[part] != '\0') {
		fprintf(stderr,
		    "line %d: %s: bad partition offset\n", lineno, cp);
		return (1);
	}
	pp->p_offset = v;
	if (tp == NULL) {
		fprintf(stderr, "line %d: missing file system type\n", lineno);
		return (1);
	}
	cp = tp, tp = word(cp);
	for (cpp = fstypenames; cpp < &fstypenames[FSMAXTYPES]; cpp++)
		if (*cpp && !strcmp(*cpp, cp))
			break;
	if (*cpp != NULL) {
		pp->p_fstype = cpp - fstypenames;
	} else {
		if (isdigit(*cp)) {
			errno = 0;
			v = strtoul(cp, &endp, 10);
			if (errno != 0 || *endp != '\0')
				v = FSMAXTYPES;
		} else
			v = FSMAXTYPES;
		if (v >= FSMAXTYPES) {
			fprintf(stderr,
			    "line %d: Warning, unknown file system type %s\n",
			    lineno, cp);
			v = FS_UNUSED;
		}
		pp->p_fstype = v;
	}

	switch (pp->p_fstype) {
	case FS_UNUSED:
	case FS_BSDFFS:
	case FS_BSDLFS:
		/* accept defaults for fsize/frag/cpg */
		if (tp) {
			NXTNUM(pp->p_fsize);
			if (pp->p_fsize == 0)
				break;
			NXTNUM(v);
			pp->p_frag = v / pp->p_fsize;
			if (tp != NULL)
				NXTNUM(pp->p_cpg);
		}
		/* else default to 0's */
		break;
	default:
		break;
	}
	return (0);
}

/*
 * Check disklabel for errors and fill in
 * derived fields according to supplied values.
 */
static int
checklabel(struct disklabel *lp)
{
	struct partition *pp;
	int i, errors = 0;
	char part;
	u_long base_offset, needed, total_size, total_percent, current_offset;
	long free_space;
	int seen_default_offset;
	int hog_part;
	int j;
	struct partition *pp2;

	if (lp == NULL)
		lp = &lab;

	if (allfields) {

		if (lp->d_secsize == 0) {
			fprintf(stderr, "sector size 0\n");
			return (1);
		}
		if (lp->d_nsectors == 0) {
			fprintf(stderr, "sectors/track 0\n");
			return (1);
		}
		if (lp->d_ntracks == 0) {
			fprintf(stderr, "tracks/cylinder 0\n");
			return (1);
		}
		if  (lp->d_ncylinders == 0) {
			fprintf(stderr, "cylinders/unit 0\n");
			errors++;
		}
		if (lp->d_rpm == 0)
			warnx("revolutions/minute 0");
		if (lp->d_secpercyl == 0)
			lp->d_secpercyl = lp->d_nsectors * lp->d_ntracks;
		if (lp->d_secperunit == 0)
			lp->d_secperunit = lp->d_secpercyl * lp->d_ncylinders;
		if (lp->d_bbsize == 0) {
			fprintf(stderr, "boot block size 0\n");
			errors++;
		} else if (lp->d_bbsize % lp->d_secsize)
			warnx("boot block size %% sector-size != 0");
		if (lp->d_npartitions > MAXPARTITIONS) {
			warnx("number of partitions (%lu) > MAXPARTITIONS (%d)",
			    (u_long)lp->d_npartitions, MAXPARTITIONS);
			errors++;
		}
		if (lp->d_npartitions < DEFPARTITIONS) {
			warnx("number of partitions (%lu) < DEFPARTITIONS (%d)",
			    (u_long)lp->d_npartitions, DEFPARTITIONS);
			errors++;
		}
	} else {
		struct disklabel *vl;

		vl = getvirginlabel();
		if (lp->d_secsize == 0)
			lp->d_secsize = vl->d_secsize;
		if (lp->d_nsectors == 0)
			lp->d_nsectors = vl->d_nsectors;
		if (lp->d_ntracks == 0)
			lp->d_ntracks = vl->d_ntracks;
		if (lp->d_ncylinders == 0)
			lp->d_ncylinders = vl->d_ncylinders;
		if (lp->d_rpm == 0)
			lp->d_rpm = vl->d_rpm;
		if (lp->d_interleave == 0)
			lp->d_interleave = vl->d_interleave;
		if (lp->d_secpercyl == 0)
			lp->d_secpercyl = vl->d_secpercyl;
		if (lp->d_secperunit == 0 ||
		    lp->d_secperunit > vl->d_secperunit)
			lp->d_secperunit = vl->d_secperunit;
		if (lp->d_bbsize == 0)
			lp->d_bbsize = vl->d_bbsize;
		if (lp->d_npartitions < DEFPARTITIONS ||
		    lp->d_npartitions > MAXPARTITIONS)
			lp->d_npartitions = vl->d_npartitions;
	}


	/* first allocate space to the partitions, then offsets */
	total_size = 0; /* in sectors */
	total_percent = 0; /* in percent */
	hog_part = -1;
	/* find all fixed partitions */
	for (i = 0; i < lp->d_npartitions; i++) {
		pp = &lp->d_partitions[i];
		if (part_set[i]) {
			if (part_size_type[i] == '*') {
				if (i == RAW_PART) {
					pp->p_size = lp->d_secperunit;
				} else {
					if (hog_part != -1)
						warnx("Too many '*' partitions (%c and %c)",
						    hog_part + 'a',i + 'a');
					else
						hog_part = i;
				}
			} else {
				off_t size;

				size = pp->p_size;
				switch (part_size_type[i]) {
				case '%':
					total_percent += size;
					break;
				case 't':
				case 'T':
					size *= 1024ULL;
					/* FALLTHROUGH */
				case 'g':
				case 'G':
					size *= 1024ULL;
					/* FALLTHROUGH */
				case 'm':
				case 'M':
					size *= 1024ULL;
					/* FALLTHROUGH */
				case 'k':
				case 'K':
					size *= 1024ULL;
					break;
				case '\0':
					break;
				default:
					warnx("unknown multiplier suffix '%c' for partition %c (should be K, M, G or T)",
					    part_size_type[i], i + 'a');
					break;
				}
				/* don't count %'s yet */
				if (part_size_type[i] != '%') {
					/*
					 * for all not in sectors, convert to
					 * sectors
					 */
					if (part_size_type[i] != '\0') {
						if (size % lp->d_secsize != 0)
							warnx("partition %c not an integer number of sectors",
							    i + 'a');
						size /= lp->d_secsize;
						pp->p_size = size;
					}
					/* else already in sectors */
					if (i != RAW_PART)
						total_size += size;
				}
			}
		}
	}

	/* Find out the total free space, excluding the boot block area. */
	base_offset = BBSIZE / secsize;
	free_space = 0;
	for (i = 0; i < lp->d_npartitions; i++) {
		pp = &lp->d_partitions[i];
		if (!part_set[i] || i == RAW_PART ||
		    part_size_type[i] == '%' || part_size_type[i] == '*')
			continue;
		if (pp->p_offset > base_offset)
			free_space += pp->p_offset - base_offset;
		if (pp->p_offset + pp->p_size > base_offset)
			base_offset = pp->p_offset + pp->p_size;
	}
	if (base_offset < lp->d_secperunit)
		free_space += lp->d_secperunit - base_offset;

	/* handle % partitions - note %'s don't need to add up to 100! */
	if (total_percent != 0) {
		if (total_percent > 100) {
			fprintf(stderr,"total percentage %lu is greater than 100\n",
			    total_percent);
			errors++;
		}

		if (free_space > 0) {
			for (i = 0; i < lp->d_npartitions; i++) {
				pp = &lp->d_partitions[i];
				if (part_set[i] && part_size_type[i] == '%') {
					/* careful of overflows! and integer roundoff */
					pp->p_size = ((double)pp->p_size/100) * free_space;
					total_size += pp->p_size;

					/* FIX we can lose a sector or so due to roundoff per
					   partition.  A more complex algorithm could avoid that */
				}
			}
		} else {
			fprintf(stderr,
			    "%ld sectors available to give to '*' and '%%' partitions\n",
			    free_space);
			errors++;
			/* fix?  set all % partitions to size 0? */
		}
	}
	/* give anything remaining to the hog partition */
	if (hog_part != -1) {
		/*
		 * Find the range of offsets usable by '*' partitions around
		 * the hog partition and how much space they need.
		 */
		needed = 0;
		base_offset = BBSIZE / secsize;
		for (i = hog_part - 1; i >= 0; i--) {
			pp = &lp->d_partitions[i];
			if (!part_set[i] || i == RAW_PART)
				continue;
			if (part_offset_type[i] == '*') {
				needed += pp->p_size;
				continue;
			}
			base_offset = pp->p_offset + pp->p_size;
			break;
		}
		current_offset = lp->d_secperunit;
		for (i = lp->d_npartitions - 1; i > hog_part; i--) {
			pp = &lp->d_partitions[i];
			if (!part_set[i] || i == RAW_PART)
				continue;
			if (part_offset_type[i] == '*') {
				needed += pp->p_size;
				continue;
			}
			current_offset = pp->p_offset;
		}

		if (current_offset - base_offset <= needed) {
			fprintf(stderr, "Cannot find space for partition %c\n",
			    hog_part + 'a');
			fprintf(stderr,
			    "Need more than %lu sectors between %lu and %lu\n",
			    needed, base_offset, current_offset);
			errors++;
			lp->d_partitions[hog_part].p_size = 0;
		} else {
			lp->d_partitions[hog_part].p_size = current_offset -
			    base_offset - needed;
			total_size += lp->d_partitions[hog_part].p_size;
		}
	}

	/* Now set the offsets for each partition */
	current_offset = BBSIZE / secsize; /* in sectors */
	seen_default_offset = 0;
	for (i = 0; i < lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (part_set[i]) {
			if (part_offset_type[i] == '*') {
				if (i == RAW_PART) {
					pp->p_offset = 0;
				} else {
					pp->p_offset = current_offset;
					seen_default_offset = 1;
				}
			} else {
				/* allow them to be out of order for old-style tables */
				if (pp->p_offset < current_offset &&
				    seen_default_offset && i != RAW_PART &&
				    pp->p_fstype != FS_VINUM) {
					fprintf(stderr,
"Offset %ld for partition %c overlaps previous partition which ends at %lu\n",
					    (long)pp->p_offset,i+'a',current_offset);
					fprintf(stderr,
"Labels with any *'s for offset must be in ascending order by sector\n");
					errors++;
				} else if (pp->p_offset != current_offset &&
				    i != RAW_PART && seen_default_offset) {
					/*
					 * this may give unneeded warnings if
					 * partitions are out-of-order
					 */
					warnx(
"Offset %ld for partition %c doesn't match expected value %ld",
					    (long)pp->p_offset, i + 'a', current_offset);
				}
			}
			if (i != RAW_PART)
				current_offset = pp->p_offset + pp->p_size;
		}
	}

	for (i = 0; i < lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_size == 0 && pp->p_offset != 0)
			warnx("partition %c: size 0, but offset %lu",
			    part, (u_long)pp->p_offset);
#ifdef notdef
		if (pp->p_size % lp->d_secpercyl)
			warnx("partition %c: size %% cylinder-size != 0",
			    part);
		if (pp->p_offset % lp->d_secpercyl)
			warnx("partition %c: offset %% cylinder-size != 0",
			    part);
#endif
		if (pp->p_offset > lp->d_secperunit) {
			fprintf(stderr,
			    "partition %c: offset past end of unit\n", part);
			errors++;
		}
		if (pp->p_offset + pp->p_size > lp->d_secperunit) {
			fprintf(stderr,
			"partition %c: partition extends past end of unit\n",
			    part);
			errors++;
		}
		if (i == RAW_PART) {
			if (pp->p_fstype != FS_UNUSED)
				warnx("partition %c is not marked as unused!",part);
			if (pp->p_offset != 0)
				warnx("partition %c doesn't start at 0!",part);
			if (pp->p_size != lp->d_secperunit)
				warnx("partition %c doesn't cover the whole unit!",part);

			if ((pp->p_fstype != FS_UNUSED) || (pp->p_offset != 0) ||
			    (pp->p_size != lp->d_secperunit)) {
				warnx("An incorrect partition %c may cause problems for "
				    "standard system utilities",part);
			}
		}

		/* check for overlaps */
		/* this will check for all possible overlaps once and only once */
		for (j = 0; j < i; j++) {
			pp2 = &lp->d_partitions[j];
			if (j != RAW_PART && i != RAW_PART &&
			    pp->p_fstype != FS_VINUM &&
			    pp2->p_fstype != FS_VINUM &&
			    part_set[i] && part_set[j]) {
				if (pp2->p_offset < pp->p_offset + pp->p_size &&
				    (pp2->p_offset + pp2->p_size > pp->p_offset ||
					pp2->p_offset >= pp->p_offset)) {
					fprintf(stderr,"partitions %c and %c overlap!\n",
					    j + 'a', i + 'a');
					errors++;
				}
			}
		}
	}
	for (; i < lp->d_npartitions; i++) {
		part = 'a' + i;
		pp = &lp->d_partitions[i];
		if (pp->p_size || pp->p_offset)
			warnx("unused partition %c: size %d offset %lu",
			    'a' + i, pp->p_size, (u_long)pp->p_offset);
	}
	return (errors);
}

/*
 * When operating on a "virgin" disk, try getting an initial label
 * from the associated device driver.  This might work for all device
 * drivers that are able to fetch some initial device parameters
 * without even having access to a (BSD) disklabel, like SCSI disks,
 * most IDE drives, or vn devices.
 *
 * The device name must be given in its "canonical" form.
 */
static struct disklabel *
getvirginlabel(void)
{
	static struct disklabel loclab;
	struct partition *dp;
	int f;
	u_int u;

	if ((f = open(specname, O_RDONLY)) == -1) {
		warn("cannot open %s", specname);
		return (NULL);
	}

	if (is_file)
		get_file_parms(f);
	else {
		mediasize = g_mediasize(f);
		secsize = g_sectorsize(f);
		if (secsize < 0 || mediasize < 0) {
			close (f);
			return (NULL);
		}
	}
	memset(&loclab, 0, sizeof loclab);
	loclab.d_magic = DISKMAGIC;
	loclab.d_magic2 = DISKMAGIC;
	loclab.d_secsize = secsize;
	loclab.d_secperunit = mediasize / secsize;

	/*
	 * Nobody in these enlightened days uses the CHS geometry for
	 * anything, but nonetheless try to get it right.  If we fail
	 * to get any good ideas from the device, construct something
	 * which is IBM-PC friendly.
	 */
	if (ioctl(f, DIOCGFWSECTORS, &u) == 0)
		loclab.d_nsectors = u;
	else
		loclab.d_nsectors = 63;
	if (ioctl(f, DIOCGFWHEADS, &u) == 0)
		loclab.d_ntracks = u;
	else if (loclab.d_secperunit <= 63*1*1024)
		loclab.d_ntracks = 1;
	else if (loclab.d_secperunit <= 63*16*1024)
		loclab.d_ntracks = 16;
	else
		loclab.d_ntracks = 255;
	loclab.d_secpercyl = loclab.d_ntracks * loclab.d_nsectors;
	loclab.d_ncylinders = loclab.d_secperunit / loclab.d_secpercyl;
	loclab.d_npartitions = DEFPARTITIONS;

	/* Various (unneeded) compat stuff */
	loclab.d_rpm = 3600;
	loclab.d_bbsize = BBSIZE;
	loclab.d_interleave = 1;
	strncpy(loclab.d_typename, "amnesiac",
	    sizeof(loclab.d_typename));

	dp = &loclab.d_partitions[RAW_PART];
	dp->p_size = loclab.d_secperunit;
	loclab.d_checksum = dkcksum(&loclab);
	close (f);
	return (&loclab);
}

static void
usage(void)
{

	fprintf(stderr,
	"%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
	"usage: bsdlabel disk",
	"\t\t(to read label)",
	"	bsdlabel -w [-n] [-m machine] disk [type]",
	"\t\t(to write label with existing boot program)",
	"	bsdlabel -e [-n] [-m machine] disk",
	"\t\t(to edit label)",
	"	bsdlabel -R [-n] [-m machine] disk protofile",
	"\t\t(to restore label with existing boot program)",
	"	bsdlabel -B [-b boot] [-m machine] disk",
	"\t\t(to install boot program with existing on-disk label)",
	"	bsdlabel -w -B [-n] [-b boot] [-m machine] disk [type]",
	"\t\t(to write label and install boot program)",
	"	bsdlabel -R -B [-n] [-b boot] [-m machine] disk protofile",
		"\t\t(to restore label and install boot program)"
	);
	exit(1);
}
