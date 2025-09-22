/*	$OpenBSD: umount.c,v 1.29 2019/06/28 13:32:46 deraadt Exp $	*/
/*	$NetBSD: umount.c,v 1.16 1996/05/11 14:13:55 mycroft Exp $	*/

/*-
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

#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include <nfs/rpcv2.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>

typedef enum { MNTON, MNTFROM } mntwhat;

int	fflag, verbose;
char	**typelist = NULL;
char	*nfshost;

char	*getmntname(char *, mntwhat, char *);
void	 maketypelist(char *);
int	 selected(const char *);
int	 namematch(struct hostent *);
int	 umountall(void);
int	 umountfs(char *);
void	 usage(void);
int	 xdr_dir(XDR *, char *);

int
main(int argc, char *argv[])
{
	int all, ch, errs;

	/* Start disks transferring immediately. */
	sync();

	all = 0;
	while ((ch = getopt(argc, argv, "afh:t:v")) != -1)
		switch (ch) {
		case 'a':
			all = 1;
			break;
		case 'f':
			fflag = MNT_FORCE;
			break;
		case 'h':	/* -h implies -a. */
			all = 1;
			nfshost = optarg;
			break;
		case 't':
			if (typelist != NULL)
				errx(1, "only one -t option may be specified.");
			maketypelist(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if ((argc == 0 && !all) || (argc != 0 && all))
		usage();

	/* -h implies "-t nfs" if no -t flag. */
	if ((nfshost != NULL) && (typelist == NULL))
		maketypelist(MOUNT_NFS);

	if (all)
		errs = umountall();
	else
		for (errs = 0; *argv != NULL; ++argv)
			if (umountfs(*argv) != 0)
				errs = 1;
	return (errs);
}

int
umountall(void)
{
	struct statfs *fs;
	int n;
	int rval;

	n = getmntinfo(&fs, MNT_NOWAIT);
	if (n == 0)
		err(1, NULL);

	rval = 0;
	while (--n >= 0) {
		/* Ignore the root. */
		if (strncmp(fs[n].f_mntonname, "/", MNAMELEN) == 0)
			continue;
		if (!selected(fs[n].f_fstypename))
			continue;
		if (umountfs(fs[n].f_mntonname))
			rval = 1;
	}
	return (rval);
}

int
umountfs(char *oname)
{
	struct hostent *hp;
#ifndef NO_NFS
	struct sockaddr_in saddr;
	struct timeval pertry, try;
	CLIENT *clp;
	int so;
#endif
	struct stat sb;
	char *delimp, *hostp, *mntpt;
	char *name, *newname, rname[PATH_MAX], type[MFSNAMELEN];

	if (isduid(oname, 0) || realpath(oname, rname) == NULL)
		mntpt = name = oname;
	else
		mntpt = name = rname;
	newname = NULL;

	/* If we can stat the file, check to see if it is a device or non-dir */
	if (stat(name, &sb) == 0) {
	    if (S_ISBLK(sb.st_mode)) {
		if ((mntpt = getmntname(name, MNTON, type)) == NULL) {
			warnx("%s: not currently mounted", name);
			return (1);
		}
	    } else if (!S_ISDIR(sb.st_mode)) {
		warnx("%s: not a directory or special device", name);
		return (1);
	    }
	}

	/*
	 * Look up the name in the mount table.
	 * 99.9% of the time the path in the kernel is the one
	 * realpath() returns but check the original just in case...
	 */
	if (!(newname = getmntname(name, MNTFROM, type)) &&
	    !(mntpt = getmntname(name, MNTON, type)) ) {
		mntpt = oname;
		if (!(newname = getmntname(oname, MNTFROM, type)) &&
		    !(mntpt = getmntname(oname, MNTON, type))) {
			warnx("%s: not currently mounted", oname);
			return (1);
		}
	}
	if (newname)
		name = newname;

	if (!selected(type))
		return (1);

	if (!strncmp(type, MOUNT_NFS, MFSNAMELEN)) {
		if ((delimp = strchr(name, '@')) != NULL) {
			hostp = delimp + 1;
			*delimp = '\0';
			hp = gethostbyname(hostp);
			*delimp = '@';
		} else if ((delimp = strchr(name, ':')) != NULL) {
			*delimp = '\0';
			hostp = name;
			hp = gethostbyname(hostp);
			name = delimp + 1;
			*delimp = ':';
		} else
			hp = NULL;
		if (!namematch(hp))
			return (1);
	}

	if (verbose)
		printf("%s: unmount from %s\n", name, mntpt);

	if (unmount(mntpt, fflag) == -1) {
		warn("%s", mntpt);
		return (1);
	}

#ifndef NO_NFS
	if (!strncmp(type, MOUNT_NFS, MFSNAMELEN) &&
	    (hp != NULL) && !(fflag & MNT_FORCE)) {
		enum clnt_stat clnt_stat;

		*delimp = '\0';
		memset(&saddr, 0, sizeof(saddr));
		saddr.sin_family = AF_INET;
		saddr.sin_port = 0;
		memmove(&saddr.sin_addr, hp->h_addr, hp->h_length);
		pertry.tv_sec = 3;
		pertry.tv_usec = 0;
		so = RPC_ANYSOCK;
		if ((clp = clntudp_create(&saddr,
		    RPCPROG_MNT, RPCMNT_VER1, pertry, &so)) == NULL) {
			clnt_pcreateerror("Cannot MNT RPC");
			return (1);
		}
		clp->cl_auth = authunix_create_default();
		try.tv_sec = 20;
		try.tv_usec = 0;
		clnt_stat = clnt_call(clp,
		    RPCMNT_UMOUNT, xdr_dir, name, xdr_void, (caddr_t)0, try);
		if (clnt_stat != RPC_SUCCESS) {
			clnt_perror(clp, "Bad MNT RPC");
			return (1);
		}
		auth_destroy(clp->cl_auth);
		clnt_destroy(clp);
	}
#endif
	return (0);
}

char *
getmntname(char *name, mntwhat what, char *type)
{
	struct statfs *mntbuf;
	int n;

	if ((n = getmntinfo(&mntbuf, MNT_NOWAIT)) == 0) {
		warn("getmntinfo");
		return (NULL);
	}
	while (--n >= 0) {
		if ((what == MNTON) &&
		    (strncmp(mntbuf[n].f_mntfromname, name, MNAMELEN) == 0 ||
		     strncmp(mntbuf[n].f_mntfromspec, name, MNAMELEN) == 0)) {
			if (type)
				memcpy(type, mntbuf[n].f_fstypename,
				    sizeof(mntbuf[n].f_fstypename));
			return (mntbuf[n].f_mntonname);
		}
		if ((what == MNTFROM) &&
		    (strncmp(mntbuf[n].f_mntonname, name, MNAMELEN) == 0)) {
			if (type)
				memcpy(type, mntbuf[n].f_fstypename,
				    sizeof(mntbuf[n].f_fstypename));
			return (mntbuf[n].f_mntfromname);
		}
	}
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
	if ((av = typelist = calloc(i + 1, sizeof(char *))) == NULL)
		err(1, NULL);
	av[0] = fslist;
	for (i = 1, nextcp = fslist; (nextcp = strchr(nextcp, ',')); i++) {
		*nextcp = '\0';
		av[i] = ++nextcp;
	}
	/* Terminate the array. */
	av[i] = NULL;
}

int
namematch(struct hostent *hp)
{
	char *cp, **np;

	if ((hp == NULL) || (nfshost == NULL))
		return (1);

	if (strcasecmp(nfshost, hp->h_name) == 0)
		return (1);

	if ((cp = strchr(hp->h_name, '.')) != NULL) {
		*cp = '\0';
		if (strcasecmp(nfshost, hp->h_name) == 0)
			return (1);
	}
	for (np = hp->h_aliases; *np; np++) {
		if (strcasecmp(nfshost, *np) == 0)
			return (1);
		if ((cp = strchr(*np, '.')) != NULL) {
			*cp = '\0';
			if (strcasecmp(nfshost, *np) == 0)
				return (1);
		}
	}
	return (0);
}

#ifndef NO_NFS
/*
 * xdr routines for mount rpc's
 */
int
xdr_dir(XDR *xdrsp, char *dirp)
{
	return (xdr_string(xdrsp, &dirp, RPCMNT_PATHLEN));
}
#endif

void
usage(void)
{
	fprintf(stderr,
	    "usage: %s\n       %s\n",
	    "umount [-fv] special | node",
	    "umount -a [-fv] [-h host] [-t type]");
	exit(1);
}
