/*
 * Copyright (c) 1980, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
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

/*
 * Turn quota on/off for a filesystem.
 */
#include <sys/types.h>
#include <sys/mount.h>
#include <ufs/ufs/quota.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstab.h>
#include <unistd.h>

char *qfname = QUOTAFILENAME;
char *qfextension[] = INITQFNAMES;

int	aflag;		/* all file systems */
int	gflag;		/* operate on group quotas */
int	uflag;		/* operate on user quotas */
int	vflag;		/* verbose */

void	usage(char *whoami);
int	hasquota(struct fstab *fs, int type, char **qfnamep, int force);
int	quotaonoff(struct fstab *fs, int offmode, int type, char *qfpathname);
int	oneof(char *target, char *list[], int cnt);
int	readonly(struct fstab *fs);


int
main(int argc, char *argv[])
{
	struct fstab *fs;
	char *qfnp, *whoami;
	long argnum, done = 0;
	int i, offmode = 0, errs = 0;
	int ch;

	whoami = strrchr(*argv, '/') + 1;
	if (whoami == (char *)1)
		whoami = *argv;
	if (strcmp(whoami, "quotaoff") == 0)
		offmode = 1;
	else if (strcmp(whoami, "quotaon") != 0) {
		fprintf(stderr, "Name must be quotaon or quotaoff not %s\n",
			whoami);
		exit(1);
	}
	while ((ch = getopt(argc, argv, "avug")) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'g':
			gflag = 1;
			break;
		case 'u':
			uflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage(whoami);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0 && !aflag)
		usage(whoami);
	if (!gflag && !uflag) {
		gflag = 1;
		uflag = 1;
	}
	setfsent();
	while ((fs = getfsent()) != NULL) {
		if (strcmp(fs->fs_type, FSTAB_RW))
			continue;
		if (strcmp(fs->fs_vfstype, "ffs") &&
		    strcmp(fs->fs_vfstype, "ufs") &&
		    strcmp(fs->fs_vfstype, "mfs"))
			continue;
		if (aflag) {
			if (gflag && hasquota(fs, GRPQUOTA, &qfnp, 0))
				errs += quotaonoff(fs, offmode, GRPQUOTA, qfnp);
			if (uflag && hasquota(fs, USRQUOTA, &qfnp, 0))
				errs += quotaonoff(fs, offmode, USRQUOTA, qfnp);
			continue;
		}
		if ((argnum = oneof(fs->fs_file, argv, argc)) >= 0 ||
		    (argnum = oneof(fs->fs_spec, argv, argc)) >= 0) {
			done |= 1 << argnum;
			if (gflag) {
				hasquota(fs, GRPQUOTA, &qfnp, 1);
				errs += quotaonoff(fs, offmode, GRPQUOTA, qfnp);
			}
			if (uflag) {
				hasquota(fs, USRQUOTA, &qfnp, 1);
				errs += quotaonoff(fs, offmode, USRQUOTA, qfnp);
			}
		}
	}
	endfsent();
	for (i = 0; i < argc; i++)
		if ((done & (1 << i)) == 0)
			fprintf(stderr, "%s not found in fstab\n",
				argv[i]);
	exit(errs);
}

void
usage(char *whoami)
{

	fprintf(stderr, "usage: %s [-aguv] filesystem ...\n", whoami);
	exit(1);
}

int
quotaonoff(struct fstab *fs, int offmode, int type, char *qfpathname)
{
	if (strcmp(fs->fs_file, "/") && readonly(fs))
		return (1);
	if (offmode) {
		if (quotactl(fs->fs_file, QCMD(Q_QUOTAOFF, type), 0, 0) < 0) {
			fprintf(stderr, "quotaoff: ");
			perror(fs->fs_file);
			return (1);
		}
		if (vflag)
			printf("%s: %s quotas turned off\n", fs->fs_file,
			    qfextension[type]);
		return (0);
	}
	if (quotactl(fs->fs_file, QCMD(Q_QUOTAON, type), 0, qfpathname) < 0) {
		warn("%s: %s quotas using %s", fs->fs_file,
		    qfextension[type], qfpathname);
		return (1);
	}
	if (vflag)
		printf("%s: %s quotas turned on\n", fs->fs_file,
		    qfextension[type]);
	return (0);
}

/*
 * Check to see if target appears in list of size cnt.
 */
int
oneof(char *target, char *list[], int cnt)
{
	int i;

	for (i = 0; i < cnt; i++)
		if (strcmp(target, list[i]) == 0)
			return (i);
	return (-1);
}

/*
 * Check to see if a particular quota is to be enabled.
 */
int
hasquota(struct fstab *fs, int type, char **qfnamep, int force)
{
	char *opt;
	char *cp;
	static char initname, usrname[100], grpname[100];
	static char buf[BUFSIZ];

	if (!initname) {
		snprintf(usrname, sizeof usrname, "%s%s",
		    qfextension[USRQUOTA], qfname);
		snprintf(grpname, sizeof grpname, "%s%s",
		    qfextension[GRPQUOTA], qfname);
		initname = 1;
	}
	strlcpy(buf, fs->fs_mntops, sizeof buf);
	for (opt = strtok(buf, ","); opt; opt = strtok(NULL, ",")) {
		if ((cp = strchr(opt, '=')) != NULL)
			*cp++ = '\0';
		if (type == USRQUOTA && strcmp(opt, usrname) == 0)
			break;
		if (type == GRPQUOTA && strcmp(opt, grpname) == 0)
			break;
	}
	if (!force && !opt)
		return (0);
	if (cp) {
		*qfnamep = cp;
		return (1);
	}
	(void) snprintf(buf, sizeof buf, "%s/%s.%s", fs->fs_file,
	    qfname, qfextension[type]);
	*qfnamep = buf;
	return (1);
}

/*
 * Verify file system is mounted and not readonly.
 * MFS is special -- it puts "mfs:" in the kernel's mount table
 */
int
readonly(struct fstab *fs)
{
	struct statfs fsbuf;

	if (statfs(fs->fs_file, &fsbuf) < 0 ||
	    strcmp(fsbuf.f_mntonname, fs->fs_file) ||
	    strcmp(fsbuf.f_mntfromname, fs->fs_spec)) {
		if (strcmp(fs->fs_file, "mfs") ||
		    memcmp(fsbuf.f_mntfromname, "mfs:", sizeof("mfs:")-1))
			;
		else {
			printf("%s: not mounted\n", fs->fs_file);
			return (1);
		}
	}
	if (fsbuf.f_flags & MNT_RDONLY) {
		printf("%s: mounted read-only\n", fs->fs_file);
		return (1);
	}
	return (0);
}
