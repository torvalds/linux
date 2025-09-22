/*	$OpenBSD: newfs.c,v 1.120 2025/09/17 16:07:57 deraadt Exp $	*/
/*	$NetBSD: newfs.c,v 1.20 1996/05/16 07:13:03 thorpej Exp $	*/

/*
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1983, 1989, 1993, 1994
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

#include <sys/param.h>	/* DEV_BSIZE MAXBSIZE */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/disklabel.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <util.h>

#include "mntopts.h"
#include "pathnames.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_WXALLOWED,
	MOPT_NOPERM,
	MOPT_ASYNC,
	MOPT_UPDATE,
	MOPT_FORCE,
	{ NULL },
};

void	fatal(const char *fmt, ...)
	    __attribute__((__format__ (printf, 1, 2)))
	    __attribute__((__nonnull__ (1)));
__dead void	usage(void);
void	mkfs(struct partition *, char *, int, int, mode_t, uid_t, gid_t);
void	getphysmem(void);
void	rewritelabel(char *, int, struct disklabel *);
u_short	dkcksum(struct disklabel *);

/*
 * The following two constants set the default block and fragment sizes.
 * Both constants must be a power of 2 and meet the following constraints:
 *	MINBSIZE <= DESBLKSIZE <= MAXBSIZE
 *	sectorsize <= DESFRAGSIZE <= DESBLKSIZE
 *	DESBLKSIZE / DESFRAGSIZE <= 8
 */
#define	DFL_FRAGSIZE	2048
#define	DFL_BLKSIZE	16384

/*
 * MAXBLKPG determines the maximum number of data blocks which are
 * placed in a single cylinder group. The default is one indirect
 * block worth of data blocks.
 */
#define MAXBLKPG_FFS1(bsize)	((bsize) / sizeof(int32_t))
#define MAXBLKPG_FFS2(bsize)	((bsize) / sizeof(int64_t))

/*
 * Each file system has a number of inodes statically allocated.
 * We allocate one inode slot per NFPI fragments, expecting this
 * to be far more than we will ever need.
 */
#define	NFPI		4

int	mfs;			/* run as the memory based filesystem */
int	Nflag;			/* run without writing file system */
int	Oflag = 2;		/* 1 = 4.4BSD ffs, 2 = ffs2 */
daddr_t	fssize;			/* file system size in 512-byte blocks */
long long	sectorsize;		/* bytes/sector */
int	fsize = 0;		/* fragment size */
int	bsize = 0;		/* block size */
int	maxfrgspercg = INT_MAX;	/* maximum fragments per cylinder group */
int	minfree = MINFREE;	/* free space threshold */
int	opt = DEFAULTOPT;	/* optimization preference (space or time) */
int	reqopt = -1;		/* opt preference has not been specified */
int	density;		/* number of bytes per inode */
int	maxbpg;			/* maximum blocks per file in a cyl group */
int	avgfilesize = AVFILESIZ;/* expected average file size */
int	avgfilesperdir = AFPDIR;/* expected number of files per directory */
int	mntflags = MNT_ASYNC;	/* flags to be passed to mount */
int	quiet = 0;		/* quiet flag */
caddr_t	membase;		/* start address of memory based filesystem */
char	*disktype;
int	unlabeled;

extern	char *__progname;
struct disklabel *getdisklabel(char *, int);

#ifdef MFS
static void waitformount(char *, pid_t);
static int do_exec(const char *, const char *, char *const[]);
static int isdir(const char *);
static void copy(char *, char *);
static int gettmpmnt(char *, size_t);
#endif

int64_t physmem;

void
getphysmem(void)
{
	int mib[] = { CTL_HW, HW_PHYSMEM64 };
	size_t len = sizeof(physmem);
	
	if (sysctl(mib, 2, &physmem, &len, NULL, 0) != 0)
		err(1, "can't get physmem");
}

int
main(int argc, char *argv[])
{
	int ch;
	struct partition *pp;
	struct disklabel *lp;
	struct disklabel mfsfakelabel;
	struct partition oldpartition;
	struct stat st;
	struct statfs *mp;
	struct rlimit rl;
	int fsi = -1, oflagset = 0, fso, len, n;
	char *cp = NULL, *s1, *s2, *special, *opstring, *realdev;
#ifdef MFS
	char mountfromname[BUFSIZ];
	char *pop = NULL, node[PATH_MAX];
	pid_t pid;
	struct stat mountpoint;
#endif
	uid_t mfsuid = 0;
	gid_t mfsgid = 0;
	mode_t mfsmode = 0;
	char *fstype = NULL;
	char **saveargv = argv;
	int ffsflag = 1;
	const char *errstr;
	long long fssize_input = 0;
	int fssize_usebytes = 0;
	int defaultfsize;
	u_int64_t nsecs;

	if (strstr(__progname, "mfs"))
		mfs = Nflag = quiet = Oflag = 1;

	getphysmem();

	opstring = mfs ?
	    "O:P:T:b:c:e:f:i:m:o:s:" :
	    "NO:S:T:b:c:e:f:g:h:i:m:o:qs:t:";
	while ((ch = getopt(argc, argv, opstring)) != -1) {
		switch (ch) {
		case 'N':
			Nflag = 1;
			break;
		case 'O':
			Oflag = strtonum(optarg, 1, 2, &errstr);
			if (errstr)
				fatal("%s: invalid ffs version", optarg);
			oflagset = 1;
			break;
		case 'S':
			if (scan_scaled(optarg, &sectorsize) == -1 ||
			    sectorsize <= 0 || (sectorsize % DEV_BSIZE))
				fatal("sector size invalid: %s", optarg);
			break;
		case 'T':
			disktype = optarg;
			break;
		case 'b':
			bsize = strtonum(optarg, MINBSIZE, MAXBSIZE, &errstr);
			if (errstr)
				fatal("block size is %s: %s", errstr, optarg);
			break;
		case 'c':
			maxfrgspercg = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				fatal("fragments per cylinder group is %s: %s",
				    errstr, optarg);
			break;
		case 'e':
			maxbpg = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				fatal("blocks per file in a cylinder group is"
				    " %s: %s", errstr, optarg);
			break;
		case 'f':
			fsize = strtonum(optarg, MINBSIZE / MAXFRAG, MAXBSIZE,
			    &errstr);
			if (errstr)
				fatal("fragment size is %s: %s",
				    errstr, optarg);
			break;
		case 'g':
			avgfilesize = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				fatal("average file size is %s: %s",
				    errstr, optarg);
			break;
		case 'h':
			avgfilesperdir = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				fatal("average files per dir is %s: %s",
				    errstr, optarg);
			break;
		case 'i':
			density = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				fatal("bytes per inode is %s: %s",
				    errstr, optarg);
			break;
		case 'm':
			minfree = strtonum(optarg, 0, 99, &errstr);
			if (errstr)
				fatal("free space %% is %s: %s",
				    errstr, optarg);
			break;
		case 'o':
			if (mfs)
				getmntopts(optarg, mopts, &mntflags);
			else {
				if (strcmp(optarg, "space") == 0)
					reqopt = opt = FS_OPTSPACE;
				else if (strcmp(optarg, "time") == 0)
					reqopt = opt = FS_OPTTIME;
				else
					fatal("%s: unknown optimization "
					    "preference: use `space' or `time'.",
					    optarg);
			}
			break;
		case 'q':
			quiet = 1;
			break;
		case 's':
			if (scan_scaled(optarg, &fssize_input) == -1 ||
			    fssize_input <= 0)
				fatal("file system size invalid: %s", optarg);
			fssize_usebytes = 0;    /* in case of multiple -s */
			for (s1 = optarg; *s1 != '\0'; s1++)
				if (isalpha((unsigned char)*s1)) {
					fssize_usebytes = 1;
					break;
				}
			break;
		case 't':
			fstype = optarg;
			if (strcmp(fstype, "ffs"))
				ffsflag = 0;
			break;
#ifdef MFS
		case 'P':
			pop = optarg;
			break;
#endif
		default:
			usage();
		}
		if (!ffsflag)
			break;
	}
	argc -= optind;
	argv += optind;

	if (ffsflag && argc - mfs != 1)
		usage();

	if (mfs) {
		/* Increase our data size to the max */
		if (getrlimit(RLIMIT_DATA, &rl) == 0) {
			rl.rlim_cur = rl.rlim_max;
			(void)setrlimit(RLIMIT_DATA, &rl);
		}
	}

	special = argv[0];

	if (!mfs) {
		char execname[PATH_MAX], name[PATH_MAX];

		if (fstype == NULL)
			fstype = readlabelfs(special, 0);
		if (fstype != NULL && strcmp(fstype, "ffs")) {
			snprintf(name, sizeof name, "newfs_%s", fstype);
			saveargv[0] = name;
			snprintf(execname, sizeof execname, "%s/newfs_%s",
			    _PATH_SBIN, fstype);
			(void)execv(execname, saveargv);
			snprintf(execname, sizeof execname, "%s/newfs_%s",
			    _PATH_USRSBIN, fstype);
			(void)execv(execname, saveargv);
			err(1, "%s not found", name);
		}
	}

	if (mfs && !strcmp(special, "swap")) {
		/*
		 * it's an MFS, mounted on "swap."  fake up a label.
		 * XXX XXX XXX
		 */
		fso = -1;	/* XXX; normally done below. */

		memset(&mfsfakelabel, 0, sizeof(mfsfakelabel));
		mfsfakelabel.d_secsize = 512;
		mfsfakelabel.d_nsectors = 64;
		mfsfakelabel.d_ntracks = 16;
		mfsfakelabel.d_ncylinders = 16;
		mfsfakelabel.d_secpercyl = 1024;
		DL_SETDSIZE(&mfsfakelabel, 16384);
		mfsfakelabel.d_npartitions = 1;
		mfsfakelabel.d_version = 1;
		DL_SETPSIZE(&mfsfakelabel.d_partitions[0], 16384);
		mfsfakelabel.d_partitions[0].p_fragblock =
		    DISKLABELV1_FFS_FRAGBLOCK(1024, 8);
		mfsfakelabel.d_partitions[0].p_cpg = 16;

		lp = &mfsfakelabel;
		pp = &mfsfakelabel.d_partitions[0];

		goto havelabel;
	}
	if (Nflag) {
		fso = -1;
	} else {
		fso = opendev(special, O_WRONLY, 0, &realdev);
		if (fso == -1)
			fatal("%s: %s", special, strerror(errno));
		special = realdev;

		/* Bail if target special is mounted */
		n = getmntinfo(&mp, MNT_NOWAIT);
		if (n == 0)
			fatal("%s: getmntinfo: %s", special, strerror(errno));

		len = sizeof(_PATH_DEV) - 1;
		s1 = special;
		if (strncmp(_PATH_DEV, s1, len) == 0)
			s1 += len;

		while (--n >= 0) {
			s2 = mp->f_mntfromname;
			if (strncmp(_PATH_DEV, s2, len) == 0) {
				s2 += len - 1;
				*s2 = 'r';
			}
			if (strcmp(s1, s2) == 0 || strcmp(s1, &s2[1]) == 0)
				fatal("%s is mounted on %s",
				    special, mp->f_mntonname);
			++mp;
		}
	}
	if (mfs && disktype != NULL) {
		lp = (struct disklabel *)getdiskbyname(disktype);
		if (lp == NULL)
			fatal("%s: unknown disk type", disktype);
		pp = &lp->d_partitions[1];
	} else {
		fsi = opendev(special, O_RDONLY, 0, NULL);
		if (fsi == -1)
			fatal("%s: %s", special, strerror(errno));
		if (fstat(fsi, &st) == -1)
			fatal("%s: %s", special, strerror(errno));
		if (!mfs) {
			if (S_ISBLK(st.st_mode))
				fatal("%s: block device", special);
			if (!S_ISCHR(st.st_mode))
				warnx("%s: not a character-special device",
				    special);
		}
		if (*argv[0] == '\0')
			fatal("empty partition name supplied");
		cp = argv[0] + strlen(argv[0]) - 1;
		if (DL_PARTNAME2NUM(*cp) == -1 && !isdigit((unsigned char)*cp))
			fatal("%s: can't figure out file system partition",
			    argv[0]);
		lp = getdisklabel(special, fsi);
		if (!mfs) {
			if (pledge("stdio disklabel tty", NULL) == -1)
				err(1, "pledge");
		}
		if (isdigit((unsigned char)*cp))
			pp = &lp->d_partitions[0];
		else
			pp = &lp->d_partitions[DL_PARTNAME2NUM(*cp)];
		if (DL_GETPSIZE(pp) == 0)
			fatal("%s: `%c' partition is unavailable",
			    argv[0], *cp);
	}
havelabel:
	if (sectorsize == 0) {
		sectorsize = lp->d_secsize;
		if (sectorsize <= 0)
			fatal("%s: no default sector size", argv[0]);
	}

	if (fssize_usebytes) {
		nsecs = fssize_input / sectorsize;
		if (fssize_input % sectorsize != 0)
			nsecs++;
	} else if (fssize_input == 0)
		nsecs = DL_GETPSIZE(pp);
	else
		nsecs = fssize_input;

	if (nsecs > DL_GETPSIZE(pp) && !mfs)
	       fatal("%s: maximum file system size on the `%c' partition is "
		   "%llu sectors", argv[0], *cp, DL_GETPSIZE(pp));

	/* Can't use DL_SECTOBLK() because sectorsize may not be from label! */
	fssize = nsecs * (sectorsize / DEV_BSIZE);
	if (oflagset == 0 && fssize >= INT_MAX)
		Oflag = 2;	/* FFS2 */
	defaultfsize = fsize == 0;
	if (fsize == 0) {
		fsize = DISKLABELV1_FFS_FSIZE(pp->p_fragblock);
		if (fsize <= 0)
			fsize = MAXIMUM(DFL_FRAGSIZE, lp->d_secsize);
	}
	if (bsize == 0) {
		bsize = DISKLABELV1_FFS_BSIZE(pp->p_fragblock);
		if (bsize <= 0)
			bsize = MINIMUM(DFL_BLKSIZE, 8 * fsize);
	}
	if (density == 0) {
		density = NFPI * fsize;
		/* large sectors lead to fewer inodes due to large fsize,
		   compensate */
		if (defaultfsize && sectorsize > DEV_BSIZE)
			density /= 2;
	}
	if (minfree < MINFREE && opt != FS_OPTSPACE && reqopt == -1) {
		warnx("warning: changing optimization to space "
		    "because minfree is less than %d%%\n", MINFREE);
		opt = FS_OPTSPACE;
	}
	if (maxbpg == 0) {
		if (Oflag <= 1)
			maxbpg = MAXBLKPG_FFS1(bsize);
		else
			maxbpg = MAXBLKPG_FFS2(bsize);
	}
	oldpartition = *pp;
#ifdef MFS
	if (mfs) {
		if (realpath(argv[1], node) == NULL)
			err(1, "realpath %s", argv[1]);
		if (stat(node, &mountpoint) == -1)
			err(ECANCELED, "stat %s", node);
		mfsuid = mountpoint.st_uid;
		mfsgid = mountpoint.st_gid;
		mfsmode = mountpoint.st_mode & ALLPERMS;
	}
#endif

	mkfs(pp, special, fsi, fso, mfsmode, mfsuid, mfsgid);
	if (!Nflag && memcmp(pp, &oldpartition, sizeof(oldpartition)))
		rewritelabel(special, fso, lp);
	if (!Nflag)
		close(fso);
	if (fsi != -1)
		close(fsi);
#ifdef MFS
	if (mfs) {
		struct mfs_args args;
		char tmpnode[PATH_MAX];

		if (pop != NULL && gettmpmnt(tmpnode, sizeof(tmpnode)) == 0)
			errx(1, "Cannot create tmp mountpoint for -P");
		memset(&args, 0, sizeof(args));
		args.base = membase;
		args.size = fssize * DEV_BSIZE;
		args.export_info.ex_root = -2;
		if (mntflags & MNT_RDONLY)
			args.export_info.ex_flags = MNT_EXRDONLY;
		if (mntflags & MNT_NOPERM)
			mntflags |= MNT_NODEV | MNT_NOEXEC;

		switch (pid = fork()) {
		case -1:
			err(10, "mfs");
		case 0:
			snprintf(mountfromname, sizeof(mountfromname),
			    "mfs:%d", getpid());
			break;
		default:
			if (pop != NULL) {
				waitformount(tmpnode, pid);
				copy(pop, tmpnode);
				unmount(tmpnode, 0);
				rmdir(tmpnode);
			}
			waitformount(node, pid);
			exit(0);
			/* NOTREACHED */
		}

		(void) setsid();
		(void) close(0);
		(void) close(1);
		(void) close(2);
		(void) chdir("/");

		args.fspec = mountfromname;
		if (pop != NULL) {
			int tmpflags = mntflags & ~MNT_RDONLY;
			if (mount(MOUNT_MFS, tmpnode, tmpflags, &args) == -1)
				exit(errno); /* parent prints message */
		}
		if (mount(MOUNT_MFS, node, mntflags, &args) == -1)
			exit(errno); /* parent prints message */
	}
#endif
	exit(0);
}

struct disklabel *
getdisklabel(char *s, int fd)
{
	static struct disklabel lab;

	if (ioctl(fd, DIOCGDINFO, (char *)&lab) == -1) {
		if (disktype != NULL) {
			struct disklabel *lp;

			unlabeled++;
			lp = getdiskbyname(disktype);
			if (lp == NULL)
				fatal("%s: unknown disk type", disktype);
			return (lp);
		}
		warn("ioctl (GDINFO)");
		fatal("%s: can't read disk label; disk type must be specified",
		    s);
	}
	return (&lab);
}

void
rewritelabel(char *s, int fd, struct disklabel *lp)
{
	if (unlabeled)
		return;

	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);
	if (ioctl(fd, DIOCWDINFO, (char *)lp) == -1) {
		warn("ioctl (WDINFO)");
		fatal("%s: can't rewrite disk label", s);
	}
}

void
fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (fcntl(STDERR_FILENO, F_GETFL) == -1) {
		openlog(__progname, LOG_CONS, LOG_DAEMON);
		vsyslog(LOG_ERR, fmt, ap);
		closelog();
	} else {
		vwarnx(fmt, ap);
	}
	va_end(ap);
	exit(1);
	/*NOTREACHED*/
}

__dead void
usage(void)
{
	extern char *__progname;

	if (mfs) {
	    fprintf(stderr,
	        "usage: %s [-b block-size] [-c fragments-per-cylinder-group] "
		"[-e maxbpg]\n"
		"\t[-f frag-size] [-i bytes] [-m free-space] [-o options] "
		"[-P file]\n"
		"\t[-s size] special node\n",
		__progname);
	} else {
	    fprintf(stderr,
	        "usage: %s [-Nq] [-b block-size] "
		"[-c fragments-per-cylinder-group] [-e maxbpg]\n"
		"\t[-f frag-size] [-g avgfilesize] [-h avgfpdir] [-i bytes]\n"
		"\t[-m free-space] [-O filesystem-format] [-o optimization]\n"
		"\t[-S sector-size] [-s size] [-T disktype] [-t fstype] "
		"special\n",
		__progname);
	}

	exit(1);
}

#ifdef MFS

static void
waitformount(char *node, pid_t pid)
{
	char mountfromname[BUFSIZ];
	struct statfs sf;
	int status;
	pid_t res;

	snprintf(mountfromname, sizeof(mountfromname), "mfs:%d", pid);
	for (;;) {
		/*
		 * spin until the mount succeeds
		 * or the child exits
		 */
		usleep(1);

		/*
		 * XXX Here is a race condition: another process
		 * can mount a filesystem which hides our
		 * ramdisk before we see the success.
		 */
		if (statfs(node, &sf) == -1)
			err(ECANCELED, "statfs %s", node);
		if (!strcmp(sf.f_mntfromname, mountfromname) &&
		    !strncmp(sf.f_mntonname, node,
			     MNAMELEN) &&
		    !strcmp(sf.f_fstypename, "mfs")) {
			return;
		}
		res = waitpid(pid, &status, WNOHANG);
		if (res == -1)
			err(EDEADLK, "waitpid");
		if (res != pid)
			continue;
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == 0)
				exit(0);
			errx(1, "%s: mount: %s", node,
			     strerror(WEXITSTATUS(status)));
		} else
			errx(EDEADLK, "abnormal termination");
	}
}

static int
do_exec(const char *dir, const char *cmd, char *const argv[])
{
	pid_t pid;
	int ret, status;
	sig_t intsave, quitsave;

	switch (pid = fork()) {
	case -1:
		err(1, "fork");
	case 0:
		if (dir != NULL && chdir(dir) != 0)
			err(1, "chdir");
		if (execv(cmd, argv) != 0)
			err(1, "%s", cmd);
		break;
	default:
		intsave = signal(SIGINT, SIG_IGN);
		quitsave = signal(SIGQUIT, SIG_IGN);
		for (;;) {
			ret = waitpid(pid, &status, 0);
			if (ret == -1)
				err(11, "waitpid");
			if (WIFEXITED(status)) {
				status = WEXITSTATUS(status);
				if (status != 0)
					warnx("%s: exited", cmd);
				break;
			} else if (WIFSIGNALED(status)) {
				warnx("%s: %s", cmd,
				    strsignal(WTERMSIG(status)));
				status = 1;
				break;
			}
		}
		signal(SIGINT, intsave);
		signal(SIGQUIT, quitsave);
		return (status);
	}
	/* NOTREACHED */
	return (-1);
}

static int
isdir(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0)
		err(1, "cannot stat %s", path);
	if (!S_ISDIR(st.st_mode) && !S_ISBLK(st.st_mode))
		errx(1, "%s: not a dir or a block device", path);
	return (S_ISDIR(st.st_mode));
}

static void
copy(char *src, char *dst)
{
	int ret, dir, created = 0;
	struct ufs_args mount_args;
	char mountpoint[MNAMELEN];
	char *const argv[] = { "pax", "-rw", "-pe", ".", dst, NULL } ;

	dir = isdir(src);
	if (dir)
		strlcpy(mountpoint, src, sizeof(mountpoint));
	else {
		created = gettmpmnt(mountpoint, sizeof(mountpoint));
		memset(&mount_args, 0, sizeof(mount_args));
		mount_args.fspec = src;
		ret = mount(MOUNT_FFS, mountpoint, MNT_RDONLY, &mount_args);
		if (ret != 0) {
			int saved_errno = errno;
			if (created && rmdir(mountpoint) != 0)
				warn("rmdir %s", mountpoint);
			if (unmount(dst, 0) != 0)
				warn("unmount %s", dst);
			errc(1, saved_errno, "mount %s %s", src, mountpoint);
		}
	}
	ret = do_exec(mountpoint, "/bin/pax", argv);
	if (!dir && unmount(mountpoint, 0) != 0)
		warn("unmount %s", mountpoint);
	if (created && rmdir(mountpoint) != 0)
		warn("rmdir %s", mountpoint);
	if (ret != 0) {
		if (unmount(dst, 0) != 0)
			warn("unmount %s", dst);
		errx(1, "copy %s to %s failed", mountpoint, dst);
	}
}

static int
gettmpmnt(char *mountpoint, size_t len)
{
	const char *tmp = _PATH_TMP;
	const char *mnt = _PATH_MNT;
	struct statfs fs;
	size_t n;

	if (statfs(tmp, &fs) != 0)
		err(1, "statfs %s", tmp);
	if (fs.f_flags & MNT_RDONLY) {
		if (statfs(mnt, &fs) != 0)
			err(1, "statfs %s", mnt);
		if (strcmp(fs.f_mntonname, "/") != 0)
			errx(1, "tmp mountpoint %s busy", mnt);
		if (strlcpy(mountpoint, mnt, len) >= len)
			errx(1, "tmp mountpoint %s too long", mnt);
		return (0);
	}
	n = strlcpy(mountpoint, tmp, len);
	if (n >= len)
		errx(1, "tmp mount point too long");
	if (mountpoint[n - 1] != '/')
		strlcat(mountpoint, "/", len);
	n = strlcat(mountpoint, "mntXXXXXXXXXX", len);
	if (n >= len)
		errx(1, "tmp mount point too long");
	if (mkdtemp(mountpoint) == NULL)
		err(1, "mkdtemp %s", mountpoint);
	return (1);
}

#endif /* MFS */
