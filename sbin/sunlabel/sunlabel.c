/*-
 * Copyright (c) 2003 Jake Burkholder.
 * Copyright (c) 2004,2005 Joerg Wunsch.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/sun_disklabel.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgeom.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	_PATH_TMPFILE	"/tmp/EdDk.XXXXXXXXXX"
#define	_PATH_BOOT	"/boot/boot1"

static int bflag;
static int Bflag;
static int cflag;
static int eflag;
static int hflag;
static int nflag;
static int Rflag;
static int wflag;

static off_t mediasize;
static uint32_t sectorsize;

struct tags {
	const char *name;
	unsigned int id;
};

static int check_label(struct sun_disklabel *sl);
static void read_label(struct sun_disklabel *sl, const char *disk);
static void write_label(struct sun_disklabel *sl, const char *disk,
    const char *bootpath);
static void edit_label(struct sun_disklabel *sl, const char *disk,
    const char *bootpath);
static int parse_label(struct sun_disklabel *sl, const char *file);
static void print_label(struct sun_disklabel *sl, const char *disk, FILE *out);

static int parse_size(struct sun_disklabel *sl, int part, char *size);
static int parse_offset(struct sun_disklabel *sl, int part, char *offset);

static const char *flagname(unsigned int tag);
static const char *tagname(unsigned int tag);
static unsigned int parse_flag(struct sun_disklabel *sl, int part,
			       const char *flag);
static unsigned int parse_tag(struct sun_disklabel *sl, int part,
			      const char *tag);
static const char *make_h_number(uintmax_t u);

static void usage(void);

extern char *__progname;

static struct tags knowntags[] = {
	{ "unassigned",	VTOC_UNASSIGNED },
	{ "boot",	VTOC_BOOT },
	{ "root",	VTOC_ROOT },
	{ "swap",	VTOC_SWAP },
	{ "usr",	VTOC_USR },
	{ "backup",	VTOC_BACKUP },
	{ "stand",	VTOC_STAND },
	{ "var",	VTOC_VAR },
	{ "home",	VTOC_HOME },
	{ "altsctr",	VTOC_ALTSCTR },
	{ "cache",	VTOC_CACHE },
	{ "VxVM_pub",	VTOC_VXVM_PUB },
	{ "VxVM_priv",	VTOC_VXVM_PRIV },
};

static struct tags knownflags[] = {
	{ "wm", 0 },
	{ "wu", VTOC_UNMNT },
	{ "rm", VTOC_RONLY },
	{ "ru", VTOC_UNMNT | VTOC_RONLY },
};

/*
 * Disk label editor for sun disklabels.
 */
int
main(int ac, char **av)
{
	struct sun_disklabel sl;
	const char *bootpath;
	const char *proto;
	const char *disk;
	int ch;

	bootpath = _PATH_BOOT; 
	while ((ch = getopt(ac, av, "b:BcehnrRw")) != -1)
		switch (ch) {
		case 'b':
			bflag = 1;
			bootpath = optarg;
			break;
		case 'B':
			Bflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'e':
			eflag = 1;
			break;
		case 'h':
			hflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'r':
			fprintf(stderr, "Obsolete -r flag ignored\n");
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		default:
			usage();
			break;
		}
	if (bflag && !Bflag)
		usage();
	if (nflag && !(Bflag || eflag || Rflag || wflag))
		usage();
	if (eflag && (Rflag || wflag))
		usage();
	if (eflag)
		hflag = 0;
	ac -= optind;
	av += optind;
	if (ac == 0)
		usage();
	bzero(&sl, sizeof(sl));
	disk = av[0];
	if (wflag) {
		if (ac != 2 || strcmp(av[1], "auto") != 0)
			usage();
		read_label(&sl, disk);
		bzero(sl.sl_part, sizeof(sl.sl_part));
		sl.sl_part[SUN_RAWPART].sdkp_cyloffset = 0;
		sl.sl_part[SUN_RAWPART].sdkp_nsectors = sl.sl_ncylinders *
		    sl.sl_ntracks * sl.sl_nsectors;
		write_label(&sl, disk, bootpath);
	} else if (eflag) {
		if (ac != 1)
			usage();
		read_label(&sl, disk);
		if (sl.sl_magic != SUN_DKMAGIC)
			errx(1, "%s%s has no sun disklabel", _PATH_DEV, disk);
		edit_label(&sl, disk, bootpath);
	} else if (Rflag) {
		if (ac != 2)
			usage();
		proto = av[1];
		read_label(&sl, disk);
		if (parse_label(&sl, proto) != 0)
			errx(1, "%s: invalid label", proto);
		write_label(&sl, disk, bootpath);
	} else if (Bflag) {
		read_label(&sl, disk);
		if (sl.sl_magic != SUN_DKMAGIC)
			errx(1, "%s%s has no sun disklabel", _PATH_DEV, disk);
		write_label(&sl, disk, bootpath);
	} else {
		read_label(&sl, disk);
		if (sl.sl_magic != SUN_DKMAGIC)
			errx(1, "%s%s has no sun disklabel", _PATH_DEV, disk);
		print_label(&sl, disk, stdout);
	}
	return (0);
}

static int
check_label(struct sun_disklabel *sl)
{
	uint64_t nsectors;
	uint64_t ostart;
	uint64_t start;
	uint64_t oend;
	uint64_t end;
	int havevtoc;
	int warnonly;
	int i;
	int j;

	havevtoc = sl->sl_vtoc_sane == SUN_VTOC_SANE;

	nsectors = sl->sl_ncylinders * sl->sl_ntracks * sl->sl_nsectors;
	if (sl->sl_part[SUN_RAWPART].sdkp_cyloffset != 0 ||
	    sl->sl_part[SUN_RAWPART].sdkp_nsectors != nsectors) {
		warnx("partition c is incorrect, must start at 0 and cover "
		    "whole disk");
		return (1);
	}
	if (havevtoc && sl->sl_vtoc_map[2].svtoc_tag != VTOC_BACKUP) {
		warnx("partition c must have tag \"backup\"");
		return (1);
	}
	for (i = 0; i < SUN_NPART; i++) {
		if (i == 2 || sl->sl_part[i].sdkp_nsectors == 0)
			continue;
		start = (uint64_t)sl->sl_part[i].sdkp_cyloffset *
		    sl->sl_ntracks * sl->sl_nsectors;
		end = start + sl->sl_part[i].sdkp_nsectors;
		if (end > nsectors) {
			warnx("partition %c extends past end of disk",
			    'a' + i);
			return (1);
		}
		if (havevtoc) {
			if (sl->sl_vtoc_map[i].svtoc_tag == VTOC_BACKUP) {
				warnx("only partition c is allowed to have "
				    "tag \"backup\"");
				return (1);
			}
		}
		for (j = 0; j < SUN_NPART; j++) {
			/* 
			 * Overlaps for unmountable partitions are
			 * non-fatal but will be warned anyway.
			 */
			warnonly = havevtoc &&
				((sl->sl_vtoc_map[i].svtoc_flag & VTOC_UNMNT) != 0 ||
				 (sl->sl_vtoc_map[j].svtoc_flag & VTOC_UNMNT) != 0);

			if (j == 2 || j == i ||
			    sl->sl_part[j].sdkp_nsectors == 0)
				continue;
			ostart = (uint64_t)sl->sl_part[j].sdkp_cyloffset *
			    sl->sl_ntracks * sl->sl_nsectors;
			oend = ostart + sl->sl_part[j].sdkp_nsectors;
			if ((start <= ostart && end >= oend) ||
			    (start > ostart && start < oend) ||
			    (end > ostart && end < oend)) {
				warnx("partition %c overlaps partition %c",
				    'a' + i, 'a' + j);
				if (!warnonly)
					return (1);
			}
		}
	}
	return (0);
}

static void
read_label(struct sun_disklabel *sl, const char *disk)
{
	char path[MAXPATHLEN];
	uint32_t fwsectors;
	uint32_t fwheads;
	char buf[SUN_SIZE];
	int fd, error;

	snprintf(path, sizeof(path), "%s%s", _PATH_DEV, disk);
	if ((fd = open(path, O_RDONLY)) < 0)
		err(1, "open %s", path);
	if (read(fd, buf, sizeof(buf)) != sizeof(buf))
		err(1, "read");
	error = sunlabel_dec(buf, sl);
	if (ioctl(fd, DIOCGMEDIASIZE, &mediasize) != 0)
		if (error)
			err(1, "%s: ioctl(DIOCGMEDIASIZE) failed", disk);
	if (ioctl(fd, DIOCGSECTORSIZE, &sectorsize) != 0) {
		if (error)
			err(1, "%s: DIOCGSECTORSIZE failed", disk);
		else
			sectorsize = 512;
	}
	if (error) {
		bzero(sl, sizeof(*sl));
		if (ioctl(fd, DIOCGFWSECTORS, &fwsectors) != 0)
			fwsectors = 63;
		if (ioctl(fd, DIOCGFWHEADS, &fwheads) != 0) {
			if (mediasize <= 63 * 1024 * sectorsize)
				fwheads = 1;
			else if (mediasize <= 63 * 16 * 1024 * sectorsize)
				fwheads = 16;
			else
				fwheads = 255;
		}
		sl->sl_rpm = 3600;
		sl->sl_pcylinders = mediasize / (fwsectors * fwheads *
		    sectorsize);
		sl->sl_sparespercyl = 0;
		sl->sl_interleave = 1;
		sl->sl_ncylinders = sl->sl_pcylinders - 2;
		sl->sl_acylinders = 2;
		sl->sl_nsectors = fwsectors;
		sl->sl_ntracks = fwheads;
		sl->sl_part[SUN_RAWPART].sdkp_cyloffset = 0;
		sl->sl_part[SUN_RAWPART].sdkp_nsectors = sl->sl_ncylinders *
		    sl->sl_ntracks * sl->sl_nsectors;
		if (mediasize > (off_t)4999L * 1024L * 1024L) {
			sprintf(sl->sl_text,
			    "FreeBSD%jdG cyl %u alt %u hd %u sec %u",
			    (intmax_t)(mediasize + 512 * 1024 * 1024) /
			        (1024 * 1024 * 1024),
			    sl->sl_ncylinders, sl->sl_acylinders,
			    sl->sl_ntracks, sl->sl_nsectors);
		} else {
			sprintf(sl->sl_text,
			    "FreeBSD%jdM cyl %u alt %u hd %u sec %u",
			    (intmax_t)(mediasize + 512 * 1024) / (1024 * 1024),
			    sl->sl_ncylinders, sl->sl_acylinders,
			    sl->sl_ntracks, sl->sl_nsectors);
		}
	}
	close(fd);
}

static void
write_label(struct sun_disklabel *sl, const char *disk, const char *bootpath)
{
	char path[MAXPATHLEN];
	char boot[SUN_BOOTSIZE];
	char buf[SUN_SIZE];
	const char *errstr;
	off_t off;
	int bfd;
	int fd;
	int i;
	struct gctl_req *grq;

	sl->sl_magic = SUN_DKMAGIC;

	if (check_label(sl) != 0)
		errx(1, "invalid label");

	bzero(buf, sizeof(buf));
	sunlabel_enc(buf, sl);

	if (nflag) {
		print_label(sl, disk, stdout);
		return;
	}
	if (Bflag) {
		if ((bfd = open(bootpath, O_RDONLY)) < 0)
			err(1, "open %s", bootpath);
		i = read(bfd, boot, sizeof(boot));
		if (i < 0)
			err(1, "read");
		else if (i != sizeof (boot))
			errx(1, "read wrong size boot code (%d)", i);
		close(bfd);
	}
	snprintf(path, sizeof(path), "%s%s", _PATH_DEV, disk);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		grq = gctl_get_handle();
		gctl_ro_param(grq, "verb", -1, "write label");
		gctl_ro_param(grq, "class", -1, "SUN");
		gctl_ro_param(grq, "geom", -1, disk);
		gctl_ro_param(grq, "label", sizeof buf, buf);
		errstr = gctl_issue(grq);
		if (errstr != NULL)
			errx(1, "%s", errstr);
		gctl_free(grq);
		if (Bflag) {
			grq = gctl_get_handle();
			gctl_ro_param(grq, "verb", -1, "write bootcode");
			gctl_ro_param(grq, "class", -1, "SUN");
			gctl_ro_param(grq, "geom", -1, disk);
			gctl_ro_param(grq, "bootcode", sizeof boot, boot);
			errstr = gctl_issue(grq);
			if (errstr != NULL)
				errx(1, "%s", errstr);
			gctl_free(grq);
		}
	} else {
		if (lseek(fd, 0, SEEK_SET) < 0)
			err(1, "lseek");
		if (write(fd, buf, sizeof(buf)) != sizeof(buf))
			err (1, "write");
		if (Bflag) {
			for (i = 0; i < SUN_NPART; i++) {
				if (sl->sl_part[i].sdkp_nsectors == 0)
					continue;
				off = sl->sl_part[i].sdkp_cyloffset *
				    sl->sl_ntracks * sl->sl_nsectors * 512;
				/*
				 * Ignore first SUN_SIZE bytes of boot code to
				 * avoid overwriting the label.
				 */
				if (lseek(fd, off + SUN_SIZE, SEEK_SET) < 0)
					err(1, "lseek");
				if (write(fd, boot + SUN_SIZE,
				    sizeof(boot) - SUN_SIZE) !=
				    sizeof(boot) - SUN_SIZE)
					err(1, "write");
			}
		}
		close(fd);
	}
	exit(0);
}

static void
edit_label(struct sun_disklabel *sl, const char *disk, const char *bootpath)
{
	char tmpfil[] = _PATH_TMPFILE;
	const char *editor;
	int status;
	FILE *fp;
	pid_t pid;
	pid_t r;
	int fd;
	int c;

	if ((fd = mkstemp(tmpfil)) < 0)
		err(1, "mkstemp");
	if ((fp = fdopen(fd, "w")) == NULL)
		err(1, "fdopen");
	print_label(sl, disk, fp);
	fflush(fp);
	for (;;) {
		if ((pid = fork()) < 0)
			err(1, "fork");
		if (pid == 0) {
			if ((editor = getenv("EDITOR")) == NULL)
				editor = _PATH_VI;
			execlp(editor, editor, tmpfil, (char *)NULL);
			err(1, "execlp %s", editor);
		}
		status = 0;
		while ((r = wait(&status)) > 0 && r != pid)
			;
		if (WIFEXITED(status)) {
			if (parse_label(sl, tmpfil) == 0) {
				fclose(fp);
				unlink(tmpfil);
				write_label(sl, disk, bootpath);
				return;
			}
			printf("re-edit the label? [y]: ");
			fflush(stdout);
			c = getchar();
			if (c != EOF && c != '\n')
				while (getchar() != '\n')
					;
			if  (c == 'n') {
				fclose(fp);
				unlink(tmpfil);
				return;
			}
		}
	}
	fclose(fp);
	unlink(tmpfil);
	return;
}

static int
parse_label(struct sun_disklabel *sl, const char *file)
{
	char offset[32];
	char size[32];
	char flag[32];
	char tag[32];
	char buf[128];
	char text[128];
	char volname[SUN_VOLNAME_LEN + 1];
	struct sun_disklabel sl1;
	char *bp;
	const char *what;
	uint8_t part;
	FILE *fp;
	int line;
	int rv;
	int wantvtoc;
	unsigned alt, cyl, hd, nr, sec;

	line = wantvtoc = 0;
	if ((fp = fopen(file, "r")) == NULL)
		err(1, "fopen");
	sl1 = *sl;
	bzero(&sl1.sl_part, sizeof(sl1.sl_part));
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		/*
		 * In order to recognize a partition entry, we search
		 * for lines starting with a single letter followed by
		 * a colon as their first non-white characters.  We
		 * silently ignore any other lines, so any comment etc.
		 * lines in the label template will be ignored.
		 *
		 * XXX We should probably also recognize the geometry
		 * fields on top, and allow changing the geometry
		 * emulated by this disk.
		 */
		for (bp = buf; isspace(*bp); bp++)
			;
		if (strncmp(bp, "text:", strlen("text:")) == 0) {
			bp += strlen("text:");
			rv = sscanf(bp,
			    " %s cyl %u alt %u hd %u sec %u",
			    text, &cyl, &alt, &hd, &sec);
			if (rv != 5) {
				warnx("%s, line %d: text label does not "
				    "contain required fields",
				    file, line + 1);
				fclose(fp);
				return (1);
			}
			if (alt != 2) {
				warnx("%s, line %d: # alt must be equal 2",
				    file, line + 1);
				fclose(fp);
				return (1);
			}
			if (cyl == 0 || cyl > USHRT_MAX) {
				what = "cyl";
				nr = cyl;
			unreasonable:
				warnx("%s, line %d: # %s %d unreasonable",
				    file, line + 1, what, nr);
				fclose(fp);
				return (1);
			}
			if (hd == 0 || hd > USHRT_MAX) {
				what = "hd";
				nr = hd;
				goto unreasonable;
			}
			if (sec == 0 || sec > USHRT_MAX) {
				what = "sec";
				nr = sec;
				goto unreasonable;
			}
			if (mediasize == 0)
				warnx("unit size unknown, no sector count "
				    "check could be done");
			else if ((uintmax_t)(cyl + alt) * sec * hd >
				 (uintmax_t)mediasize / sectorsize) {
				warnx("%s, line %d: sector count %ju exceeds "
				    "unit size %ju",
				    file, line + 1,
				    (uintmax_t)(cyl + alt) * sec * hd,
				    (uintmax_t)mediasize / sectorsize);
				fclose(fp);
				return (1);
			}
			sl1.sl_pcylinders = cyl + alt;
			sl1.sl_ncylinders = cyl;
			sl1.sl_acylinders = alt;
			sl1.sl_nsectors = sec;
			sl1.sl_ntracks = hd;
			memset(sl1.sl_text, 0, sizeof(sl1.sl_text));
			snprintf(sl1.sl_text, sizeof(sl1.sl_text),
			    "%s cyl %u alt %u hd %u sec %u",
			    text, cyl, alt, hd, sec);
			continue;
		}
		if (strncmp(bp, "volume name:", strlen("volume name:")) == 0) {
			wantvtoc = 1; /* Volume name requires VTOC. */
			bp += strlen("volume name:");
#if SUN_VOLNAME_LEN != 8
# error "scanf field width does not match SUN_VOLNAME_LEN"
#endif
			/*
			 * We set the field length to one more than
			 * SUN_VOLNAME_LEN to allow detecting an
			 * overflow.
			 */
			memset(volname, 0, sizeof volname);
			rv = sscanf(bp, " %9[^\n]", volname);
			if (rv != 1) {
				/* Clear the volume name. */
				memset(sl1.sl_vtoc_volname, 0,
				    SUN_VOLNAME_LEN);
			} else {
				memcpy(sl1.sl_vtoc_volname, volname,
				    SUN_VOLNAME_LEN);
				if (volname[SUN_VOLNAME_LEN] != '\0')
					warnx(
"%s, line %d: volume name longer than %d characters, truncating",
					    file, line + 1, SUN_VOLNAME_LEN);
			}
			continue;
		}
		if (strlen(bp) < 2 || bp[1] != ':') {
			line++;
			continue;
		}
		rv = sscanf(bp, "%c: %30s %30s %30s %30s",
		    &part, size, offset, tag, flag);
		if (rv < 3) {
		syntaxerr:
			warnx("%s: syntax error on line %d",
			    file, line + 1);
			fclose(fp);
			return (1);
		}
		if (parse_size(&sl1, part - 'a', size) ||
		    parse_offset(&sl1, part - 'a', offset))
			goto syntaxerr;
		if (rv > 3) {
			wantvtoc = 1;
			if (rv == 5 && parse_flag(&sl1, part - 'a', flag))
				goto syntaxerr;
			if (parse_tag(&sl1, part - 'a', tag))
				goto syntaxerr;
		}
		line++;
	}
	fclose(fp);
	if (wantvtoc) {
		sl1.sl_vtoc_sane = SUN_VTOC_SANE;
		sl1.sl_vtoc_vers = SUN_VTOC_VERSION;
		sl1.sl_vtoc_nparts = SUN_NPART;
	} else {
		sl1.sl_vtoc_sane = 0;
		sl1.sl_vtoc_vers = 0;
		sl1.sl_vtoc_nparts = 0;
		bzero(&sl1.sl_vtoc_map, sizeof(sl1.sl_vtoc_map));
	}
	*sl = sl1;
	return (check_label(sl));
}

static int
parse_size(struct sun_disklabel *sl, int part, char *size)
{
	uintmax_t nsectors;
	uintmax_t total;
	uintmax_t n;
	char *p;
	int i;

	nsectors = 0;
	n = strtoumax(size, &p, 10);
	if (*p != '\0') {
		if (strcmp(size, "*") == 0) {
			total = sl->sl_ncylinders * sl->sl_ntracks *
			    sl->sl_nsectors;
			for (i = 0; i < part; i++) {
				if (i == 2)
					continue;
				nsectors += sl->sl_part[i].sdkp_nsectors;
			}
			n = total - nsectors;
		} else if (p[1] == '\0' && (p[0] == 'C' || p[0] == 'c')) {
			n = n * sl->sl_ntracks * sl->sl_nsectors;
		} else if (p[1] == '\0' && (p[0] == 'K' || p[0] == 'k')) {
			n = roundup((n * 1024) / 512,
			    sl->sl_ntracks * sl->sl_nsectors);
		} else if (p[1] == '\0' && (p[0] == 'M' || p[0] == 'm')) {
			n = roundup((n * 1024 * 1024) / 512,
			    sl->sl_ntracks * sl->sl_nsectors);
		} else if (p[1] == '\0' && (p[0] == 'S' || p[0] == 's')) {
			/* size in sectors, no action neded */
		} else if (p[1] == '\0' && (p[0] == 'G' || p[0] == 'g')) {
			n = roundup((n * 1024 * 1024 * 1024) / 512,
			    sl->sl_ntracks * sl->sl_nsectors);
		} else
			return (-1);
	} else if (cflag) {
		n = n * sl->sl_ntracks * sl->sl_nsectors;
	}
	sl->sl_part[part].sdkp_nsectors = n;
	return (0);
}

static int
parse_offset(struct sun_disklabel *sl, int part, char *offset)
{
	uintmax_t nsectors;
	uintmax_t n;
	char *p;
	int i;

	nsectors = 0;
	n = strtoumax(offset, &p, 10);
	if (*p != '\0') {
		if (strcmp(offset, "*") == 0) {
			for (i = 0; i < part; i++) {
				if (i == 2)
					continue;
				nsectors += sl->sl_part[i].sdkp_nsectors;
			}
			n = nsectors / (sl->sl_nsectors * sl->sl_ntracks);
		} else
			return (-1);
	}
	sl->sl_part[part].sdkp_cyloffset = n;
	return (0);
}

static void
print_label(struct sun_disklabel *sl, const char *disk, FILE *out)
{
	int i, j;
	int havevtoc;
	uintmax_t secpercyl;
	/* Long enough to hex-encode each character. */
	char volname[4 * SUN_VOLNAME_LEN + 1];

	havevtoc = sl->sl_vtoc_sane == SUN_VTOC_SANE;
	secpercyl = sl->sl_nsectors * sl->sl_ntracks;

	fprintf(out,
"# /dev/%s:\n"
"text: %s\n"
"bytes/sector: %d\n"
"sectors/cylinder: %ju\n",
	    disk,
	    sl->sl_text,
	    sectorsize,
	    secpercyl);
	if (eflag)
		fprintf(out,
		    "# max sectors/unit (including alt cylinders): %ju\n",
		    (uintmax_t)mediasize / sectorsize);
	fprintf(out,
"sectors/unit: %ju\n",
	    secpercyl * sl->sl_ncylinders);
	if (havevtoc && sl->sl_vtoc_volname[0] != '\0') {
		for (i = j = 0; i < SUN_VOLNAME_LEN; i++) {
			if (sl->sl_vtoc_volname[i] == '\0')
				break;
			if (isprint(sl->sl_vtoc_volname[i]))
				volname[j++] = sl->sl_vtoc_volname[i];
			else
				j += sprintf(volname + j, "\\x%02X",
				    sl->sl_vtoc_volname[i]);
		}
		volname[j] = '\0';
		fprintf(out, "volume name: %s\n", volname);
	}
	fprintf(out,
"\n"
"%d partitions:\n"
"#\n",
	    SUN_NPART);
	if (!hflag) {
		fprintf(out, "# Size is in %s.", cflag? "cylinders": "sectors");
		if (eflag)
			fprintf(out,
"  Use %%d%c, %%dK, %%dM or %%dG to specify in %s,\n"
"# kilobytes, megabytes or gigabytes respectively, or '*' to specify rest of\n"
"# disk.\n",
			    cflag? 's': 'c',
			    cflag? "sectors": "cylinders");
		else
			putc('\n', out);
		fprintf(out, "# Offset is in cylinders.");
		if (eflag)
			fprintf(out,
"  Use '*' to calculate offsets automatically.\n"
"#\n");
		else
			putc('\n', out);
	}
	if (havevtoc)
		fprintf(out,
"#    size       offset      tag         flag\n"
"#    ---------- ----------  ----------  ----\n"
			);
	else
		fprintf(out,
"#    size       offset\n"
"#    ---------- ----------\n"
			);

	for (i = 0; i < SUN_NPART; i++) {
		if (sl->sl_part[i].sdkp_nsectors == 0)
			continue;
		if (hflag) {
			fprintf(out, "  %c: %10s",
			    'a' + i,
			    make_h_number((uintmax_t)
				sl->sl_part[i].sdkp_nsectors * 512));
			fprintf(out, " %10s",
			    make_h_number((uintmax_t)
				sl->sl_part[i].sdkp_cyloffset * 512
				* secpercyl));
		} else {
			fprintf(out, "  %c: %10ju %10u",
			    'a' + i,
			    sl->sl_part[i].sdkp_nsectors / (cflag? secpercyl: 1),
			    sl->sl_part[i].sdkp_cyloffset);
		}
		if (havevtoc)
			fprintf(out, " %11s %5s",
			    tagname(sl->sl_vtoc_map[i].svtoc_tag),
			    flagname(sl->sl_vtoc_map[i].svtoc_flag));
		putc('\n', out);
	}
}

static void
usage(void)
{

	fprintf(stderr, "usage:"
"\t%s [-r] [-c | -h] disk\n"
"\t\t(to read label)\n"
"\t%s -B [-b boot1] [-n] disk\n"
"\t\t(to install boot program only)\n"
"\t%s -R [-B [-b boot1]] [-r] [-n] [-c] disk protofile\n"
"\t\t(to restore label)\n"
"\t%s -e [-B [-b boot1]] [-r] [-n] [-c] disk\n"
"\t\t(to edit label)\n"
"\t%s -w [-B [-b boot1]] [-r] [-n] disk type\n"
"\t\t(to write default label)\n",
	     __progname,
	     __progname,
	     __progname,
	     __progname,
	     __progname);
	exit(1);
}

/*
 * Return VTOC tag and flag names for tag or flag ID, resp.
 */
static const char *
tagname(unsigned int tag)
{
	static char buf[32];
	size_t i;
	struct tags *tp;

	for (i = 0, tp = knowntags; i < nitems(knowntags); i++, tp++)
		if (tp->id == tag)
			return (tp->name);

	sprintf(buf, "%u", tag);

	return (buf);
}

static const char *
flagname(unsigned int flag)
{
	static char buf[32];
	size_t i;
	struct tags *tp;

	for (i = 0, tp = knownflags; i < nitems(knownflags); i++, tp++)
		if (tp->id == flag)
			return (tp->name);

	sprintf(buf, "%u", flag);

	return (buf);
}

static unsigned int
parse_tag(struct sun_disklabel *sl, int part, const char *tag)
{
	struct tags *tp;
	char *endp;
	size_t i;
	unsigned long l;

	for (i = 0, tp = knowntags; i < nitems(knowntags); i++, tp++)
		if (strcmp(tp->name, tag) == 0) {
			sl->sl_vtoc_map[part].svtoc_tag = (uint16_t)tp->id;
			return (0);
		}

	l = strtoul(tag, &endp, 0);
	if (*tag != '\0' && *endp == '\0') {
		sl->sl_vtoc_map[part].svtoc_tag = (uint16_t)l;
		return (0);
	}

	return (-1);
}

static unsigned int
parse_flag(struct sun_disklabel *sl, int part, const char *flag)
{
	struct tags *tp;
	char *endp;
	size_t i;
	unsigned long l;

	for (i = 0, tp = knownflags; i < nitems(knownflags); i++, tp++)
		if (strcmp(tp->name, flag) == 0) {
			sl->sl_vtoc_map[part].svtoc_flag = (uint16_t)tp->id;
			return (0);
		}

	l = strtoul(flag, &endp, 0);
	if (*flag != '\0' && *endp == '\0') {
		sl->sl_vtoc_map[part].svtoc_flag = (uint16_t)l;
		return (0);
	}

	return (-1);
}

/*
 * Convert argument into `human readable' byte number form.
 */
static const char *
make_h_number(uintmax_t u)
{
	static char buf[32];
	double d;

	if (u == 0) {
		strcpy(buf, "0B");
	} else if (u > 2000000000UL) {
		d = (double)u / 1e9;
		sprintf(buf, "%.1fG", d);
	} else if (u > 2000000UL) {
		d = (double)u / 1e6;
		sprintf(buf, "%.1fM", d);
	} else {
		d = (double)u / 1e3;
		sprintf(buf, "%.1fK", d);
	}

	return (buf);
}
