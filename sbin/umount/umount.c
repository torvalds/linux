/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1989, 1993
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
"@(#) Copyright (c) 1980, 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)umount.c	8.8 (Berkeley) 5/8/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netdb.h>
#include <rpc/rpc.h>
#include <rpcsvc/mount.h>
#include <nfs/nfssvc.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mounttab.h"

typedef enum { FIND, REMOVE, CHECKUNIQUE } dowhat;

static struct addrinfo *nfshost_ai = NULL;
static int	fflag, vflag;
static char	*nfshost;

struct statfs *checkmntlist(char *);
int	 checkvfsname (const char *, char **);
struct statfs *getmntentry(const char *fromname, const char *onname,
	     fsid_t *fsid, dowhat what);
char   **makevfslist (const char *);
size_t	 mntinfo (struct statfs **);
int	 namematch (struct addrinfo *);
int	 parsehexfsid(const char *hex, fsid_t *fsid);
int	 sacmp (void *, void *);
int	 umountall (char **);
int	 checkname (char *, char **);
int	 umountfs(struct statfs *sfs);
void	 usage (void);
int	 xdr_dir (XDR *, char *);

int
main(int argc, char *argv[])
{
	int all, errs, ch, mntsize, error, nfsforce, ret;
	char **typelist = NULL;
	struct statfs *mntbuf, *sfs;
	struct addrinfo hints;

	nfsforce = all = errs = 0;
	while ((ch = getopt(argc, argv, "AaF:fh:Nnt:v")) != -1)
		switch (ch) {
		case 'A':
			all = 2;
			break;
		case 'a':
			all = 1;
			break;
		case 'F':
			setfstab(optarg);
			break;
		case 'f':
			fflag |= MNT_FORCE;
			break;
		case 'h':	/* -h implies -A. */
			all = 2;
			nfshost = optarg;
			break;
		case 'N':
			nfsforce = 1;
			break;
		case 'n':
			fflag |= MNT_NONBUSY;
			break;
		case 't':
			if (typelist != NULL)
				err(1, "only one -t option may be specified");
			typelist = makevfslist(optarg);
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if ((fflag & MNT_FORCE) != 0 && (fflag & MNT_NONBUSY) != 0)
		err(1, "-f and -n are mutually exclusive");

	if ((argc == 0 && !all) || (argc != 0 && all))
		usage();

	if (nfsforce != 0 && (argc == 0 || nfshost != NULL || typelist != NULL))
		usage();

	/* -h implies "-t nfs" if no -t flag. */
	if ((nfshost != NULL) && (typelist == NULL))
		typelist = makevfslist("nfs");

	if (nfshost != NULL) {
		memset(&hints, 0, sizeof hints);
		error = getaddrinfo(nfshost, NULL, &hints, &nfshost_ai);
		if (error)
			errx(1, "%s: %s", nfshost, gai_strerror(error));
	}

	switch (all) {
	case 2:
		if ((mntsize = mntinfo(&mntbuf)) <= 0)
			break;
		/*
		 * We unmount the nfs-mounts in the reverse order
		 * that they were mounted.
		 */
		for (errs = 0, mntsize--; mntsize > 0; mntsize--) {
			sfs = &mntbuf[mntsize];
			if (checkvfsname(sfs->f_fstypename, typelist))
				continue;
			if (strcmp(sfs->f_mntonname, "/dev") == 0)
				continue;
			if (umountfs(sfs) != 0)
				errs = 1;
		}
		free(mntbuf);
		break;
	case 1:
		if (setfsent() == 0)
			err(1, "%s", getfstab());
		errs = umountall(typelist);
		break;
	case 0:
		for (errs = 0; *argv != NULL; ++argv)
			if (nfsforce != 0) {
				/*
				 * First do the nfssvc() syscall to shut down
				 * the mount point and then do the forced
				 * dismount.
				 */
				ret = nfssvc(NFSSVC_FORCEDISM, *argv);
				if (ret >= 0)
					ret = unmount(*argv, MNT_FORCE);
				if (ret < 0) {
					warn("%s", *argv);
					errs = 1;
				}
			} else if (checkname(*argv, typelist) != 0)
				errs = 1;
		break;
	}
	exit(errs);
}

int
umountall(char **typelist)
{
	struct xvfsconf vfc;
	struct fstab *fs;
	int rval;
	char *cp;
	static int firstcall = 1;

	if ((fs = getfsent()) != NULL)
		firstcall = 0;
	else if (firstcall)
		errx(1, "fstab reading failure");
	else
		return (0);
	do {
		/* Ignore the root. */
		if (strcmp(fs->fs_file, "/") == 0)
			continue;
		/*
		 * !!!
		 * Historic practice: ignore unknown FSTAB_* fields.
		 */
		if (strcmp(fs->fs_type, FSTAB_RW) &&
		    strcmp(fs->fs_type, FSTAB_RO) &&
		    strcmp(fs->fs_type, FSTAB_RQ))
			continue;
		/* Ignore unknown file system types. */
		if (getvfsbyname(fs->fs_vfstype, &vfc) == -1)
			continue;
		if (checkvfsname(fs->fs_vfstype, typelist))
			continue;

		/*
		 * We want to unmount the file systems in the reverse order
		 * that they were mounted.  So, we save off the file name
		 * in some allocated memory, and then call recursively.
		 */
		if ((cp = malloc((size_t)strlen(fs->fs_file) + 1)) == NULL)
			err(1, "malloc failed");
		(void)strcpy(cp, fs->fs_file);
		rval = umountall(typelist);
		rval = checkname(cp, typelist) || rval;
		free(cp);
		return (rval);
	} while ((fs = getfsent()) != NULL);
	return (0);
}

/*
 * Do magic checks on mountpoint/device/fsid, and then call unmount(2).
 */
int
checkname(char *mntname, char **typelist)
{
	char buf[MAXPATHLEN];
	struct statfs sfsbuf;
	struct stat sb;
	struct statfs *sfs;
	char *delimp;
	dev_t dev;
	int len;

	/*
	 * 1. Check if the name exists in the mounttable.
	 */
	sfs = checkmntlist(mntname);
	/*
	 * 2. Remove trailing slashes if there are any. After that
	 * we look up the name in the mounttable again.
	 */
	if (sfs == NULL) {
		len = strlen(mntname);
		while (len > 1 && mntname[len - 1] == '/')
			mntname[--len] = '\0';
		sfs = checkmntlist(mntname);
	}
	/*
	 * 3. Check if the deprecated NFS syntax with an '@' has been used
	 * and translate it to the ':' syntax. Look up the name in the
	 * mount table again.
	 */
	if (sfs == NULL && (delimp = strrchr(mntname, '@')) != NULL) {
		snprintf(buf, sizeof(buf), "%s:%.*s", delimp + 1,
		    (int)(delimp - mntname), mntname);
		len = strlen(buf);
		while (len > 1 && buf[len - 1] == '/')
			buf[--len] = '\0';
		sfs = checkmntlist(buf);
	}
	/*
	 * 4. Resort to a statfs(2) call. This is the last check so that
	 * hung NFS filesystems for example can be unmounted without
	 * potentially blocking forever in statfs() as long as the
	 * filesystem is specified unambiguously. This covers all the
	 * hard cases such as symlinks and mismatches between the
	 * mount list and reality.
	 * We also do this if an ambiguous mount point was specified.
	 */
	if (sfs == NULL || (getmntentry(NULL, mntname, NULL, FIND) != NULL &&
	    getmntentry(NULL, mntname, NULL, CHECKUNIQUE) == NULL)) {
		if (statfs(mntname, &sfsbuf) != 0) {
			warn("%s: statfs", mntname);
		} else if (stat(mntname, &sb) != 0) {
			warn("%s: stat", mntname);
		} else if (S_ISDIR(sb.st_mode)) {
			/* Check that `mntname' is the root directory. */
			dev = sb.st_dev;
			snprintf(buf, sizeof(buf), "%s/..", mntname);
			if (stat(buf, &sb) != 0) {
				warn("%s: stat", buf);
			} else if (sb.st_dev == dev) {
				warnx("%s: not a file system root directory",
				    mntname);
				return (1);
			} else
				sfs = &sfsbuf;
		}
	}
	if (sfs == NULL) {
		warnx("%s: unknown file system", mntname);
		return (1);
	}
	if (checkvfsname(sfs->f_fstypename, typelist))
		return (1);
	return (umountfs(sfs));
}

/*
 * NFS stuff and unmount(2) call
 */
int
umountfs(struct statfs *sfs)
{
	char fsidbuf[64];
	enum clnt_stat clnt_stat;
	struct timeval try;
	struct addrinfo *ai, hints;
	int do_rpc;
	CLIENT *clp;
	char *nfsdirname, *orignfsdirname;
	char *hostp, *delimp;
	char buf[1024];
	struct nfscl_dumpmntopts dumpmntopts;
	const char *proto_ptr = NULL;

	ai = NULL;
	do_rpc = 0;
	hostp = NULL;
	nfsdirname = delimp = orignfsdirname = NULL;
	memset(&hints, 0, sizeof hints);

	if (strcmp(sfs->f_fstypename, "nfs") == 0) {
		if ((nfsdirname = strdup(sfs->f_mntfromname)) == NULL)
			err(1, "strdup");
		orignfsdirname = nfsdirname;
		if (*nfsdirname == '[' &&
		    (delimp = strchr(nfsdirname + 1, ']')) != NULL &&
		    *(delimp + 1) == ':') {
			hostp = nfsdirname + 1;
			nfsdirname = delimp + 2;
		} else if ((delimp = strrchr(nfsdirname, ':')) != NULL) {
			hostp = nfsdirname;
			nfsdirname = delimp + 1;
		}
		if (hostp != NULL) {
			*delimp = '\0';
			getaddrinfo(hostp, NULL, &hints, &ai);
			if (ai == NULL) {
				warnx("can't get net id for host");
			}
		}

		/*
		 * Check if we have to start the rpc-call later.
		 * If there are still identical nfs-names mounted,
		 * we skip the rpc-call. Obviously this has to
		 * happen before unmount(2), but it should happen
		 * after the previous namecheck.
		 * A non-NULL return means that this is the last
		 * mount from mntfromname that is still mounted.
		 */
		if (getmntentry(sfs->f_mntfromname, NULL, NULL,
		    CHECKUNIQUE) != NULL) {
			do_rpc = 1;
			proto_ptr = "udp";
			/*
			 * Try and find out whether this NFS mount is NFSv4 and
			 * what protocol is being used. If this fails, the
			 * default is NFSv2,3 and use UDP for the Unmount RPC.
			 */
			dumpmntopts.ndmnt_fname = sfs->f_mntonname;
			dumpmntopts.ndmnt_buf = buf;
			dumpmntopts.ndmnt_blen = sizeof(buf);
			if (nfssvc(NFSSVC_DUMPMNTOPTS, &dumpmntopts) >= 0) {
				if (strstr(buf, "nfsv4,") != NULL)
					do_rpc = 0;
				else if (strstr(buf, ",tcp,") != NULL)
					proto_ptr = "tcp";
			}
		}
	}

	if (!namematch(ai)) {
		free(orignfsdirname);
		return (1);
	}
	/* First try to unmount using the file system ID. */
	snprintf(fsidbuf, sizeof(fsidbuf), "FSID:%d:%d", sfs->f_fsid.val[0],
	    sfs->f_fsid.val[1]);
	if (unmount(fsidbuf, fflag | MNT_BYFSID) != 0) {
		/* XXX, non-root users get a zero fsid, so don't warn. */
		if (errno != ENOENT || sfs->f_fsid.val[0] != 0 ||
		    sfs->f_fsid.val[1] != 0)
			warn("unmount of %s failed", sfs->f_mntonname);
		if (errno != ENOENT) {
			free(orignfsdirname);
			return (1);
		}
		/* Compatibility for old kernels. */
		if (sfs->f_fsid.val[0] != 0 || sfs->f_fsid.val[1] != 0)
			warnx("retrying using path instead of file system ID");
		if (unmount(sfs->f_mntonname, fflag) != 0) {
			warn("unmount of %s failed", sfs->f_mntonname);
			free(orignfsdirname);
			return (1);
		}
	}
	/* Mark this this file system as unmounted. */
	getmntentry(NULL, NULL, &sfs->f_fsid, REMOVE);
	if (vflag)
		(void)printf("%s: unmount from %s\n", sfs->f_mntfromname,
		    sfs->f_mntonname);
	/*
	 * Report to mountd-server which nfsname
	 * has been unmounted.
	 */
	if (ai != NULL && !(fflag & MNT_FORCE) && do_rpc) {
		clp = clnt_create(hostp, MOUNTPROG, MOUNTVERS3, proto_ptr);
		if (clp  == NULL) {
			warnx("%s: %s", hostp,
			    clnt_spcreateerror("MOUNTPROG"));
			free(orignfsdirname);
			return (1);
		}
		clp->cl_auth = authsys_create_default();
		try.tv_sec = 20;
		try.tv_usec = 0;
		clnt_stat = clnt_call(clp, MOUNTPROC_UMNT, (xdrproc_t)xdr_dir,
		    nfsdirname, (xdrproc_t)xdr_void, (caddr_t)0, try);
		if (clnt_stat != RPC_SUCCESS) {
			warnx("%s: %s", hostp,
			    clnt_sperror(clp, "RPCMNT_UMOUNT"));
			free(orignfsdirname);
			return (1);
		}
		/*
		 * Remove the unmounted entry from /var/db/mounttab.
		 */
		if (read_mtab()) {
			clean_mtab(hostp, nfsdirname, vflag);
			if(!write_mtab(vflag))
				warnx("cannot remove mounttab entry %s:%s",
				    hostp, nfsdirname);
			free_mtab();
		}
		auth_destroy(clp->cl_auth);
		clnt_destroy(clp);
	}
	free(orignfsdirname);
	return (0);
}

struct statfs *
getmntentry(const char *fromname, const char *onname, fsid_t *fsid, dowhat what)
{
	static struct statfs *mntbuf;
	static size_t mntsize = 0;
	static int *mntcheck = NULL;
	struct statfs *sfs, *foundsfs;
	int i, count;

	if (mntsize <= 0) {
		if ((mntsize = mntinfo(&mntbuf)) <= 0)
			return (NULL);
	}
	if (mntcheck == NULL) {
		if ((mntcheck = calloc(mntsize + 1, sizeof(int))) == NULL)
			err(1, "calloc");
	}
	/*
	 * We want to get the file systems in the reverse order
	 * that they were mounted. Unmounted file systems are marked
	 * in a table called 'mntcheck'.
	 */
	count = 0;
	foundsfs = NULL;
	for (i = mntsize - 1; i >= 0; i--) {
		if (mntcheck[i])
			continue;
		sfs = &mntbuf[i];
		if (fromname != NULL && strcmp(sfs->f_mntfromname,
		    fromname) != 0)
			continue;
		if (onname != NULL && strcmp(sfs->f_mntonname, onname) != 0)
			continue;
		if (fsid != NULL && bcmp(&sfs->f_fsid, fsid,
		    sizeof(*fsid)) != 0)
			continue;

		switch (what) {
		case CHECKUNIQUE:
			foundsfs = sfs;
			count++;
			continue;
		case REMOVE:
			mntcheck[i] = 1;
			break;
		default:
			break;
		}
		return (sfs);
	}

	if (what == CHECKUNIQUE && count == 1)
		return (foundsfs);
	return (NULL);
}

int
sacmp(void *sa1, void *sa2)
{
	void *p1, *p2;
	int len;

	if (((struct sockaddr *)sa1)->sa_family !=
	    ((struct sockaddr *)sa2)->sa_family)
		return (1);

	switch (((struct sockaddr *)sa1)->sa_family) {
	case AF_INET:
		p1 = &((struct sockaddr_in *)sa1)->sin_addr;
		p2 = &((struct sockaddr_in *)sa2)->sin_addr;
		len = 4;
		break;
	case AF_INET6:
		p1 = &((struct sockaddr_in6 *)sa1)->sin6_addr;
		p2 = &((struct sockaddr_in6 *)sa2)->sin6_addr;
		len = 16;
		if (((struct sockaddr_in6 *)sa1)->sin6_scope_id !=
		    ((struct sockaddr_in6 *)sa2)->sin6_scope_id)
			return (1);
		break;
	default:
		return (1);
	}

	return memcmp(p1, p2, len);
}

int
namematch(struct addrinfo *ai)
{
	struct addrinfo *aip;

	if (nfshost == NULL || nfshost_ai == NULL)
		return (1);

	while (ai != NULL) {
		aip = nfshost_ai;
		while (aip != NULL) {
			if (sacmp(ai->ai_addr, aip->ai_addr) == 0)
				return (1);
			aip = aip->ai_next;
		}
		ai = ai->ai_next;
	}

	return (0);
}

struct statfs *
checkmntlist(char *mntname)
{
	struct statfs *sfs;
	fsid_t fsid;

	sfs = NULL;
	if (parsehexfsid(mntname, &fsid) == 0)
		sfs = getmntentry(NULL, NULL, &fsid, FIND);
	if (sfs == NULL)
		sfs = getmntentry(NULL, mntname, NULL, FIND);
	if (sfs == NULL)
		sfs = getmntentry(mntname, NULL, NULL, FIND);
	return (sfs);
}

size_t
mntinfo(struct statfs **mntbuf)
{
	static struct statfs *origbuf;
	size_t bufsize;
	int mntsize;

	mntsize = getfsstat(NULL, 0, MNT_NOWAIT);
	if (mntsize <= 0)
		return (0);
	bufsize = (mntsize + 1) * sizeof(struct statfs);
	if ((origbuf = malloc(bufsize)) == NULL)
		err(1, "malloc");
	mntsize = getfsstat(origbuf, (long)bufsize, MNT_NOWAIT);
	*mntbuf = origbuf;
	return (mntsize);
}

/*
 * Convert a hexadecimal filesystem ID to an fsid_t.
 * Returns 0 on success.
 */
int
parsehexfsid(const char *hex, fsid_t *fsid)
{
	char hexbuf[3];
	int i;

	if (strlen(hex) != sizeof(*fsid) * 2)
		return (-1);
	hexbuf[2] = '\0';
	for (i = 0; i < (int)sizeof(*fsid); i++) {
		hexbuf[0] = hex[i * 2];
		hexbuf[1] = hex[i * 2 + 1];
		if (!isxdigit(hexbuf[0]) || !isxdigit(hexbuf[1]))
			return (-1);
		((u_char *)fsid)[i] = strtol(hexbuf, NULL, 16);
	}
	return (0);
}

/*
 * xdr routines for mount rpc's
 */
int
xdr_dir(XDR *xdrsp, char *dirp)
{

	return (xdr_string(xdrsp, &dirp, MNTPATHLEN));
}

void
usage(void)
{

	(void)fprintf(stderr, "%s\n%s\n",
	    "usage: umount [-fNnv] special ... | node ... | fsid ...",
	    "       umount -a | -A [-F fstab] [-fnv] [-h host] [-t type]");
	exit(1);
}
