/*	$OpenBSD: mount.c,v 1.78 2024/05/09 08:35:40 florian Exp $	*/
/*	$NetBSD: mount.c,v 1.24 1995/11/18 03:34:29 cgd Exp $	*/

/*
 * Copyright (c) 1980, 1989, 1993, 1994
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

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>

#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>

#include "pathnames.h"

int	debug, verbose, skip;
char	**typelist = NULL;
enum { NONET_FILTER, NET_FILTER } filter = NONET_FILTER;

int	selected(const char *);
char   *catopt(char *, const char *);
char   *flags2opts(u_int32_t);
struct statfs
       *getmntpt(const char *);
int	hasopt(const char *, const char *);
void	maketypelist(char *);
void	mangle(char *, int *, const char **, int);
int	mountfs(const char *, const char *, const char *, const char *,
	    const char *, int);
void	prmount(struct statfs *);
int	disklabelcheck(struct fstab *);
__dead void	usage(void);

/* Map from mount options to printable formats. */
static struct opt {
	int o_opt;
	int o_silent;
	const char *o_name;
	const char *o_optname;
} optnames[] = {
	{ MNT_ASYNC,		0,	"asynchronous",		"async" },
	{ MNT_DEFEXPORTED,	1,	"exported to the world", "" },
	{ MNT_EXPORTED,		0,	"NFS exported",		"" },
	{ MNT_EXPORTANON,	1,	"anon uid mapping",	"" },
	{ MNT_EXRDONLY,		1,	"exported read-only",	"" },
	{ MNT_LOCAL,		0,	"local",		"" },
	{ MNT_NOATIME,		0,	"noatime",		"noatime" },
	{ MNT_NODEV,		0,	"nodev",		"nodev" },
	{ MNT_NOEXEC,		0,	"noexec",		"noexec" },
	{ MNT_NOSUID,		0,	"nosuid",		"nosuid" },
	{ MNT_NOPERM,		0,	"noperm",		"noperm" },
	{ MNT_WXALLOWED,	0,	"wxallowed",		"wxallowed" },
	{ MNT_QUOTA,		0,	"with quotas",		"" },
	{ MNT_RDONLY,		0,	"read-only",		"ro" },
	{ MNT_ROOTFS,		1,	"root file system",	"" },
	{ MNT_SYNCHRONOUS,	0,	"synchronous",		"sync" },
	{ MNT_SOFTDEP,		0,	"softdep",		"softdep" },
	{ 0,			0,	"",			"" }
};

int
main(int argc, char * const argv[])
{
	const char *mntonname, *vfstype;
	struct fstab *fs;
	struct statfs *mntbuf;
	FILE *mountdfp;
	pid_t pid;
	int all, ch, forceall, i, mntsize, rval, new;
	char *options, mntpath[PATH_MAX];

	all = forceall = 0;
	options = NULL;
	vfstype = "ffs";
	while ((ch = getopt(argc, argv, "AadfNo:rswt:uv")) != -1)
		switch (ch) {
		case 'A':
			all = forceall = 1;
			break;
		case 'a':
			all = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'f':
			if (!hasopt(options, "force"))
				options = catopt(options, "force");
			break;
		case 'N':
			filter = NET_FILTER;
			break;
		case 'o':
			if (*optarg)
				options = catopt(options, optarg);
			break;
		case 'r':
			if (!hasopt(options, "ro"))
				options = catopt(options, "ro");
			break;
		case 's':
			skip = 1;
			break;
		case 't':
			if (typelist != NULL)
				errx(1, "only one -t option may be specified.");
			maketypelist(optarg);
			vfstype = optarg;
			break;
		case 'u':
			if (!hasopt(options, "update"))
				options = catopt(options, "update");
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			if (!hasopt(options, "rw"))
				options = catopt(options, "rw");
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (typelist == NULL && argc == 2) {
		/*
		 * If -t flag has not been specified, and spec contains either
		 * a ':' or a '@' then assume that an NFS filesystem is being
		 * specified ala Sun.  If not, check the disklabel for a
		 * known filesystem type.
		 */
		if (strpbrk(argv[0], ":@") != NULL)
			vfstype = "nfs";
		else {
			char *labelfs = readlabelfs(argv[0], 0);
			if (labelfs != NULL)
				vfstype = labelfs;
		}
	}

	if (pledge("stdio rpath disklabel proc exec", NULL) == -1)
		err(1, "pledge");

#define	BADTYPE(type)							\
	(strcmp(type, FSTAB_RO) &&					\
	    strcmp(type, FSTAB_RW) && strcmp(type, FSTAB_RQ))

	rval = 0;
	new = 0;
	switch (argc) {
	case 0:
		if (all)
			while ((fs = getfsent()) != NULL) {
				if (BADTYPE(fs->fs_type))
					continue;
				switch (filter) {
				case NET_FILTER:
					if (!hasopt(fs->fs_mntops, "net"))
						continue;
					break;
				case NONET_FILTER:
					if (hasopt(fs->fs_mntops, "net"))
						continue;
					break;
				}
				if (!selected(fs->fs_vfstype))
					continue;
				if (hasopt(fs->fs_mntops, "noauto"))
					continue;
				if (disklabelcheck(fs))
					continue;
				if (mountfs(fs->fs_vfstype, fs->fs_spec,
				    fs->fs_file, options,
				    fs->fs_mntops, !forceall))
					rval = 1;
				else
					++new;
			}
		else {
			if ((mntsize = getmntinfo(&mntbuf, MNT_NOWAIT)) == 0)
				err(1, "getmntinfo");
			for (i = 0; i < mntsize; i++) {
				if (!selected(mntbuf[i].f_fstypename))
					continue;
				prmount(&mntbuf[i]);
			}
			return (rval);
		}
		break;
	case 1:
		if (typelist != NULL)
			usage();

		if (realpath(*argv, mntpath) == NULL) 
			strlcpy(mntpath, *argv, sizeof(mntpath));
		if (hasopt(options, "update")) {
			if ((mntbuf = getmntpt(mntpath)) == NULL)
				errx(1,
				    "unknown special file or file system %s.",
				    mntpath);
			if ((mntbuf->f_flags & MNT_ROOTFS) &&
			    !strcmp(mntbuf->f_mntfromname, "root_device")) {
				/* Lookup fstab for name of root device. */
				fs = getfsfile(mntbuf->f_mntonname);
				if (fs == NULL)
					errx(1,
					    "can't find fstab entry for %s.",
					    mntpath);
			} else {
				if ((fs = malloc(sizeof(*fs))) == NULL)
					err(1, NULL);
				fs->fs_vfstype = mntbuf->f_fstypename;
				fs->fs_spec = mntbuf->f_mntfromname;
			}
			/*
			 * It's an update, ignore the fstab file options.
			 * Get the current options, so we can change only
			 * the options which given via a command line.
			 */
			fs->fs_mntops = flags2opts(mntbuf->f_flags);
			mntonname = mntbuf->f_mntonname;
		} else {
			if ((fs = getfsfile(mntpath)) == NULL &&
			    (fs = getfsspec(mntpath)) == NULL)
				errx(1, "can't find fstab entry for %s.",
				    mntpath);
			if (BADTYPE(fs->fs_type))
				errx(1, "%s has unknown file system type.",
				    mntpath);
			mntonname = fs->fs_file;
		}
		rval = mountfs(fs->fs_vfstype, fs->fs_spec,
		    mntonname, options, fs->fs_mntops, skip);
		break;
	case 2:
		rval = mountfs(vfstype, argv[0], argv[1], options, NULL, skip);
		break;
	default:
		usage();
	}

	/*
	 * If the mount was successful and done by root, tell mountd the
	 * good news.  Pid checks are probably unnecessary, but don't hurt.
	 * XXX This should be done from kernel.
	 */
	if ((rval == 0 || new) && getuid() == 0 &&
	    (mountdfp = fopen(_PATH_MOUNTDPID, "r")) != NULL) {
		if (fscanf(mountdfp, "%d", &pid) == 1 &&
		    pid > 0 && kill(pid, SIGHUP) == -1 && errno != ESRCH)
			err(1, "signal mountd");
		(void)fclose(mountdfp);
	}

	return (rval);
}

int
hasopt(const char *mntopts, const char *option)
{
	int found;
	char *opt, *optbuf;

	if (mntopts == NULL)
		return (0);
	if ((optbuf = strdup(mntopts)) == NULL)
		err(1, NULL);
	found = 0;
	for (opt = optbuf; !found && opt != NULL; strsep(&opt, ","))
		found = !strncmp(opt, option, strlen(option));
	free(optbuf);
	return (found);
}

/*
 * Convert mount(2) flags to list of mount(8) options.
 */
char*
flags2opts(u_int32_t flags)
{
	char	*optlist;
	struct opt *p;

	optlist = NULL;
	for (p = optnames; p->o_opt; p++) {
		if (flags & p->o_opt && *p->o_optname)
			optlist = catopt(optlist, p->o_optname);
	}

	return(optlist);
}

int
mountfs(const char *vfstype, const char *spec, const char *name,
    const char *options, const char *mntopts, int skipmounted)
{
	char *cp;

	/* List of directories containing mount_xxx subcommands. */
	static const char *edirs[] = {
		_PATH_SBIN,
		_PATH_USRSBIN,
		NULL
	};
	const char **argv, **edir;
	struct statfs sf;
	pid_t pid;
	int argc, i, status, argvsize;
	char *optbuf, execname[PATH_MAX], mntpath[PATH_MAX];

	if (realpath(name, mntpath) == NULL) {
		warn("realpath %s", name);
		return (1);
	}

	name = mntpath;

	if (mntopts == NULL)
		mntopts = "";

	if (options == NULL) {
		if (*mntopts == '\0')
			options = "rw";
		else {
			options = mntopts;
			mntopts = "";
		}
	}

	/* options follows after mntopts, so they get priority over mntopts */
	if ((cp = strdup(mntopts)) == NULL)
		err(1, NULL);
	optbuf = catopt(cp, options);

	if (strcmp(name, "/") == 0) {
		if (!hasopt(optbuf, "update"))
			optbuf = catopt(optbuf, "update");
	} else if (skipmounted) {
		if (statfs(name, &sf) == -1) {
			warn("statfs %s", name);
			return (1);
		}
		/* XXX can't check f_mntfromname, thanks to mfs, etc. */
		if (strncmp(name, sf.f_mntonname, MNAMELEN) == 0 &&
		    strncmp(vfstype, sf.f_fstypename, MFSNAMELEN) == 0) {
			if (verbose) {
				printf("%s", sf.f_mntfromname);
				if (strncmp(sf.f_mntfromname,
				    sf.f_mntfromspec, MNAMELEN) != 0)
					printf(" (%s)", sf.f_mntfromspec);
				printf(" on %s type %.*s: %s\n",
				    sf.f_mntonname,
				    MFSNAMELEN, sf.f_fstypename,
				    "already mounted");
			}
			return (0);
		}
	}

	argvsize = 64;
	if((argv = reallocarray(NULL, argvsize, sizeof(char *))) == NULL)
		err(1, NULL);
	argc = 0;
	argv[argc++] = NULL;	/* this should be a full path name */
	mangle(optbuf, &argc, argv, argvsize - 4);
	argv[argc++] = spec;
	argv[argc++] = name;
	argv[argc] = NULL;

	if (debug) {
		(void)printf("exec: mount_%s", vfstype);
		for (i = 1; i < argc; i++)
			(void)printf(" %s", argv[i]);
		(void)printf("\n");
		free(optbuf);
		free(argv);
		return (0);
	}

	switch ((pid = fork())) {
	case -1:				/* Error. */
		warn("fork");
		free(optbuf);
		free(argv);
		return (1);
	case 0:					/* Child. */
		/* Go find an executable. */
		edir = edirs;
		do {
			(void)snprintf(execname,
			    sizeof(execname), "%s/mount_%s", *edir, vfstype);
			argv[0] = execname;
			execv(execname, (char * const *)argv);
			if (errno != ENOENT)
				warn("exec %s for %s", execname, name);
		} while (*++edir != NULL);

		if (errno == ENOENT)
			warn("no mount helper program found for %s", vfstype);
		_exit(1);
	default:				/* Parent. */
		free(optbuf);
		free(argv);

		if (waitpid(pid, &status, 0) == -1) {
			warn("waitpid");
			return (1);
		}

		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0)
				return (WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			warnx("%s: %s", name, strsignal(WTERMSIG(status)));
			return (1);
		}

		if (verbose) {
			if (statfs(name, &sf) == -1) {
				warn("statfs %s", name);
				return (1);
			}
			prmount(&sf);
		}
		break;
	}

	return (0);
}

void
prmount(struct statfs *sf)
{
	int flags;
	struct opt *o;
	int f = 0;

	printf("%s", sf->f_mntfromname);
	if (verbose &&
	    strncmp(sf->f_mntfromname, sf->f_mntfromspec, MNAMELEN) != 0)
		printf(" (%s)", sf->f_mntfromspec);
	printf(" on %s type %.*s", sf->f_mntonname,
	    MFSNAMELEN, sf->f_fstypename);

	flags = sf->f_flags & MNT_VISFLAGMASK;
	if (verbose && !(flags & MNT_RDONLY))
		(void)printf("%s%s", !f++ ? " (" : ", ", "rw");
	for (o = optnames; flags && o->o_opt; o++)
		if (flags & o->o_opt) {
			if (!o->o_silent)
				(void)printf("%s%s", !f++ ? " (" : ", ",
				    o->o_name);
			flags &= ~o->o_opt;
		}
	if (flags)
		(void)printf("%sunknown flag%s %#x", !f++ ? " (" : ", ",
		    flags & (flags - 1) ? "s" : "", flags);


	if (verbose) {
		char buf[26];
		time_t t = sf->f_ctime;

		if (ctime_r(&t, buf))
			printf(", ctime=%.24s", buf);
		else
			printf(", ctime=%lld", t);
	}

	/*
	 * Filesystem-specific options
	 * We only print the "interesting" values unless in verbose
	 * mode in order to keep the signal/noise ratio high.
	 */
	if (strcmp(sf->f_fstypename, MOUNT_NFS) == 0) {
		struct protoent *pr;
		struct nfs_args *nfs_args = &sf->mount_info.nfs_args;

		(void)printf("%s%s", !f++ ? " (" : ", ",
		    (nfs_args->flags & NFSMNT_NFSV3) ? "v3" : "v2");
		if (nfs_args->proto && (pr = getprotobynumber(nfs_args->proto)))
			(void)printf("%s%s", !f++ ? " (" : ", ", pr->p_name);
		else
			(void)printf("%s%s", !f++ ? " (" : ", ",
			    (nfs_args->sotype == SOCK_DGRAM) ? "udp" : "tcp");
		if (nfs_args->flags & NFSMNT_SOFT)
			(void)printf("%s%s", !f++ ? " (" : ", ", "soft");
		else if (verbose)
			(void)printf("%s%s", !f++ ? " (" : ", ", "hard");
		if (nfs_args->flags & NFSMNT_INT)
			(void)printf("%s%s", !f++ ? " (" : ", ", "intr");
		if (nfs_args->flags & NFSMNT_NOCONN)
			(void)printf("%s%s", !f++ ? " (" : ", ", "noconn");
		if (nfs_args->flags & NFSMNT_RDIRPLUS)
			(void)printf("%s%s", !f++ ? " (" : ", ", "rdirplus");
		if (verbose || nfs_args->wsize != NFS_WSIZE)
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "wsize", nfs_args->wsize);
		if (verbose || nfs_args->rsize != NFS_RSIZE)
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "rsize", nfs_args->rsize);
		if (verbose || nfs_args->readdirsize != NFS_READDIRSIZE)
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "rdirsize", nfs_args->readdirsize);
		if (verbose || nfs_args->timeo != 10) /* XXX */
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "timeo", nfs_args->timeo);
		if (verbose || nfs_args->retrans != NFS_RETRANS)
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "retrans", nfs_args->retrans);
		if (verbose || nfs_args->maxgrouplist != NFS_MAXGRPS)
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "maxgrouplist", nfs_args->maxgrouplist);
		if (verbose || nfs_args->readahead != NFS_DEFRAHEAD)
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "readahead", nfs_args->readahead);
		if (verbose) {
			(void)printf("%s%s=%d", !f++ ? " (" : ", ",
			    "acregmin", nfs_args->acregmin);
			(void)printf(", %s=%d",
			    "acregmax", nfs_args->acregmax);
			(void)printf(", %s=%d",
			    "acdirmin", nfs_args->acdirmin);
			(void)printf(", %s=%d",
			    "acdirmax", nfs_args->acdirmax);
		}
#ifndef SMALL
	} else if (strcmp(sf->f_fstypename, MOUNT_MFS) == 0) {
		int headerlen;
		long blocksize;
		char *header;

		header = getbsize(&headerlen, &blocksize);
		(void)printf("%s%s=%lu %s", !f++ ? " (" : ", ",
		    "size", sf->mount_info.mfs_args.size / blocksize, header);
#endif /* SMALL */
	} else if (strcmp(sf->f_fstypename, MOUNT_MSDOS) == 0) {
		struct msdosfs_args *msdosfs_args = &sf->mount_info.msdosfs_args;

		if (verbose || msdosfs_args->uid || msdosfs_args->gid)
			(void)printf("%s%s=%u, %s=%u", !f++ ? " (" : ", ",
			    "uid", msdosfs_args->uid, "gid", msdosfs_args->gid);
		if (verbose || msdosfs_args->mask != 0755)
			(void)printf("%s%s=0%o", !f++ ? " (" : ", ",
			    "mask", msdosfs_args->mask);
		if (msdosfs_args->flags & MSDOSFSMNT_SHORTNAME)
			(void)printf("%s%s", !f++ ? " (" : ", ", "short");
		if (msdosfs_args->flags & MSDOSFSMNT_LONGNAME)
			(void)printf("%s%s", !f++ ? " (" : ", ", "long");
		if (msdosfs_args->flags & MSDOSFSMNT_NOWIN95)
			(void)printf("%s%s", !f++ ? " (" : ", ", "nowin95");
	} else if (strcmp(sf->f_fstypename, MOUNT_CD9660) == 0) {
		struct iso_args *iso_args = &sf->mount_info.iso_args;

		if (iso_args->flags & ISOFSMNT_NORRIP)
			(void)printf("%s%s", !f++ ? " (" : ", ", "norrip");
		if (iso_args->flags & ISOFSMNT_GENS)
			(void)printf("%s%s", !f++ ? " (" : ", ", "gens");
		if (iso_args->flags & ISOFSMNT_EXTATT)
			(void)printf("%s%s", !f++ ? " (" : ", ", "extatt");
#ifndef SMALL
	} else if (strcmp(sf->f_fstypename, MOUNT_TMPFS) == 0) {
		struct tmpfs_args *tmpfs_args = &sf->mount_info.tmpfs_args;

		if (verbose || tmpfs_args->ta_root_uid || tmpfs_args->ta_root_gid)
			(void)printf("%s%s=%u, %s=%u", !f++ ? " (" : ", ",
			    "uid", tmpfs_args->ta_root_uid, "gid", tmpfs_args->ta_root_gid);
		if (verbose || tmpfs_args->ta_root_mode != 040755)
			(void)printf("%s%s=%04o", !f++ ? " (" : ", ",
			    "mode", tmpfs_args->ta_root_mode & 07777);
		if (verbose || tmpfs_args->ta_size_max)
			(void)printf("%s%s=%lu", !f++ ? " (" : ", ",
			    "size", (unsigned long)tmpfs_args->ta_size_max);
		if (verbose || tmpfs_args->ta_nodes_max)
			(void)printf("%s%s=%lu", !f++ ? " (" : ", ",
			    "inodes", (unsigned long)tmpfs_args->ta_nodes_max);
#endif /* SMALL */
	}
	(void)printf(f ? ")\n" : "\n");
}

struct statfs *
getmntpt(const char *name)
{
	struct statfs *mntbuf;
	int i, mntsize;

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (i = 0; i < mntsize; i++)
		if (strcmp(mntbuf[i].f_mntfromname, name) == 0 ||
		    strcmp(mntbuf[i].f_mntonname, name) == 0)
			return (&mntbuf[i]);
	return (NULL);
}

static enum { IN_LIST, NOT_IN_LIST } which;

int
selected(const char *type)
{
	char **av;

	/* If no type specified, it's always selected. */
	if (typelist == NULL)
		return (1);
	for (av = typelist; *av != NULL; ++av)
		if (!strncmp(type, *av, MFSNAMELEN))
			return (which == IN_LIST ? 1 : 0);
	return (which == IN_LIST ? 0 : 1);
}

void
maketypelist(char *fslist)
{
	int i;
	char *nextcp, **av;

	if ((fslist == NULL) || (fslist[0] == '\0'))
		errx(1, "empty type list");

	/*
	 * XXX
	 * Note: the syntax is "noxxx,yyy" for no xxx's and
	 * no yyy's, not the more intuitive "noxxx,noyyy".
	 */
	if (fslist[0] == 'n' && fslist[1] == 'o') {
		fslist += 2;
		which = NOT_IN_LIST;
	} else
		which = IN_LIST;

	/* Count the number of types. */
	for (i = 1, nextcp = fslist; (nextcp = strchr(nextcp, ',')); i++)
		++nextcp;

	/* Build an array of that many types. */
	if ((av = typelist = reallocarray(NULL, i + 1, sizeof(char *))) == NULL)
		err(1, NULL);
	av[0] = fslist;
	for (i = 1, nextcp = fslist; (nextcp = strchr(nextcp, ',')); i++) {
		*nextcp = '\0';
		av[i] = ++nextcp;
	}
	/* Terminate the array. */
	av[i] = NULL;
}

char *
catopt(char *s0, const char *s1)
{
	char *cp;

	if (s0 && *s0) {
		if (asprintf(&cp, "%s,%s", s0, s1) == -1)
			err(1, NULL);
	} else {
		if ((cp = strdup(s1)) == NULL)
			err(1, NULL);
	}

	free(s0);
	return cp;
}

void
mangle(char *options, int *argcp, const char **argv, int argcmax)
{
	char *p, *s;
	int argc;

	argcmax -= 2;
	argc = *argcp;
	for (s = options; argc <= argcmax && (p = strsep(&s, ",")) != NULL;)
		if (*p != '\0') {
			if (*p == '-') {
				argv[argc++] = p;
				p = strchr(p, '=');
				if (p) {
					*p = '\0';
					argv[argc++] = p + 1;
				}
			} else {
				argv[argc++] = "-o";
				argv[argc++] = p;
			}
		}

	*argcp = argc;
}

__dead void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: mount [-AadfNruvw] [-t type]\n"
	    "       mount [-dfrsuvw] special | node\n"
	    "       mount [-dfruvw] [-o options] [-t type] special node\n");
	exit(1);
}

int
disklabelcheck(struct fstab *fs)
{
	char *labelfs;

	if (strcmp(fs->fs_vfstype, "nfs") != 0 ||
	    strpbrk(fs->fs_spec, ":@") == NULL) {
		labelfs = readlabelfs(fs->fs_spec, 0);
		if (labelfs == NULL ||
		    strcmp(labelfs, fs->fs_vfstype) == 0)
			return (0);
		if (strcmp(fs->fs_vfstype, "ufs") == 0 &&
		    strcmp(labelfs, "ffs") == 0) {
			warnx("%s: fstab uses outdated type 'ufs' -- fix please",
			    fs->fs_spec);
			return (0);
		}
		if (strcmp(fs->fs_vfstype, "mfs") == 0 &&
		    strcmp(labelfs, "ffs") == 0)
			return (0);
		warnx("%s: fstab type %s != disklabel type %s",
		    fs->fs_spec, fs->fs_vfstype, labelfs);
		return (1);
	}
	return (0);
}
