/*	$OpenBSD: mount_nfs.c,v 1.56 2024/08/19 05:58:41 florian Exp $	*/
/*	$NetBSD: mount_nfs.c,v 1.12.4.1 1996/05/25 22:48:05 fvdl Exp $	*/

/*
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
#include <sys/stat.h>
#include <syslog.h>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>

#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "mntopts.h"

#define	ALTF_BG		0x1
#define ALTF_NOCONN	0x2
#define ALTF_DUMBTIMR	0x4
#define ALTF_INTR	0x8
#define ALTF_NFSV3	0x20
#define ALTF_RDIRPLUS	0x40
#define	ALTF_MNTUDP	0x80
#define ALTF_RESVPORT	0x100
#define ALTF_SEQPACKET	0x200
#define ALTF_SOFT	0x800
#define ALTF_TCP	0x1000
#define ALTF_PORT	0x2000
#define ALTF_NFSV2	0x4000
#define ALTF_NOAC       0x8000
#define ALTF_ACREGMIN	0x10000
#define ALTF_ACREGMAX	0x20000
#define ALTF_ACDIRMIN	0x40000
#define ALTF_ACDIRMAX	0x80000

const struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_WXALLOWED,
	MOPT_FORCE,
	MOPT_UPDATE,
	MOPT_SYNC,
	{ "bg", ALTF_BG, 0 },
	{ "conn", ALTF_NOCONN, MFLAG_INVERSE },
	{ "dumbtimer", ALTF_DUMBTIMR, 0 },
	{ "intr", ALTF_INTR, 0 },
	{ "nfsv3", ALTF_NFSV3, 0 },
	{ "rdirplus", ALTF_RDIRPLUS, 0 },
	{ "mntudp", ALTF_MNTUDP, 0 },
	{ "resvport", ALTF_RESVPORT, 0 },
	{ "soft", ALTF_SOFT, 0 },
	{ "tcp", ALTF_TCP, 0 },
	{ "port", ALTF_PORT, MFLAG_INTVAL },
	{ "nfsv2", ALTF_NFSV2, 0 },
	{ "ac", ALTF_NOAC, MFLAG_INVERSE },
	{ "acregmin", ALTF_ACREGMIN, MFLAG_INTVAL },
	{ "acregmax", ALTF_ACREGMAX, MFLAG_INTVAL },
	{ "acdirmin", ALTF_ACDIRMIN, MFLAG_INTVAL },
	{ "acdirmax", ALTF_ACDIRMAX, MFLAG_INTVAL },
	{ NULL }
};

struct nfs_args nfsdefargs = {
	NFS_ARGSVERSION,
	NULL,
	sizeof (struct sockaddr_in),
	SOCK_DGRAM,
	0,
	NULL,
	0,
	NFSMNT_NFSV3,
	NFS_WSIZE,
	NFS_RSIZE,
	NFS_READDIRSIZE,
	10,
	NFS_RETRANS,
	NFS_MAXGRPS,
	NFS_DEFRAHEAD,
	0,
	0,
	NULL,
	0,
	0,
	0,
	0
};

struct nfhret {
	u_long		stat;
	long		vers;
	long		auth;
	long		fhsize;
	u_char		nfh[NFSX_V3FHMAX];
};
#define	DEF_RETRY	10000
#define	BGRND	1
#define	ISBGRND	2
int retrycnt;
int opflags = 0;
int nfsproto = IPPROTO_UDP;
int mnttcp_ok = 1;
u_short port_no = 0;
int force2 = 0;
int force3 = 0;

int	getnfsargs(char *, struct nfs_args *);
void	set_rpc_maxgrouplist(int);
__dead	void usage(void);
int	xdr_dir(XDR *, char *);
int	xdr_fh(XDR *, struct nfhret *);

int
main(int argc, char *argv[])
{
	int c;
	struct nfs_args *nfsargsp;
	struct nfs_args nfsargs;
	int mntflags, num;
	char name[PATH_MAX], *options = NULL, *spec;
	const char *p;
	union mntval value;

	retrycnt = DEF_RETRY;

	mntflags = 0;
	nfsargs = nfsdefargs;
	nfsargsp = &nfsargs;
	while ((c = getopt(argc, argv,
	    "23a:bcdD:g:I:iL:lo:PR:r:sTt:w:x:U")) != -1)
		switch (c) {
		case '3':
			if (force2)
				errx(1, "-2 and -3 are mutually exclusive");
			force3 = 1;
			break;
		case '2':
			if (force3)
				errx(1, "-2 and -3 are mutually exclusive");
			force2 = 1;
			nfsargsp->flags &= ~NFSMNT_NFSV3;
			break;
		case 'a':
			num = (int) strtonum(optarg, 0, 4, &p);
			if (p)
				errx(1, "illegal -a value %s: %s", optarg, p);
			nfsargsp->readahead = num;
			nfsargsp->flags |= NFSMNT_READAHEAD;
			break;
		case 'b':
			opflags |= BGRND;
			break;
		case 'c':
			nfsargsp->flags |= NFSMNT_NOCONN;
			break;
		case 'D':
			/* backward compatibility */
			break;
		case 'd':
			nfsargsp->flags |= NFSMNT_DUMBTIMR;
			break;
		case 'g':
			num = (int) strtonum(optarg, 1, NGROUPS_MAX, &p);
			if (p)
				errx(1, "illegal -g value %s: %s", optarg, p);
			set_rpc_maxgrouplist(num);
			nfsargsp->maxgrouplist = num;
			nfsargsp->flags |= NFSMNT_MAXGRPS;
			break;
		case 'I':
			num = (int) strtonum(optarg, 1, INT_MAX, &p);
			if (p)
				errx(1, "illegal -I value %s: %s", optarg, p);
			nfsargsp->readdirsize = num;
			nfsargsp->flags |= NFSMNT_READDIRSIZE;
			break;
		case 'i':
			nfsargsp->flags |= NFSMNT_INT;
			break;
		case 'L':
			/* backward compatibility */
			break;
		case 'l':
			nfsargsp->flags |= NFSMNT_RDIRPLUS;
			break;
		case 'o':
			options = optarg;
			while (options != NULL) {
				switch (getmntopt(&options, &value, mopts,
				    &mntflags)) {
				case ALTF_BG:
					opflags |= BGRND;
					break;
				case ALTF_NOCONN:
					nfsargsp->flags |= NFSMNT_NOCONN;
					break;
				case ALTF_DUMBTIMR:
					nfsargsp->flags |= NFSMNT_DUMBTIMR;
					break;
				case ALTF_INTR:
					nfsargsp->flags |= NFSMNT_INT;
					break;
				case ALTF_NFSV3:
					if (force2)
						errx(1,
						    "conflicting version options");
					force3 = 1;
					break;
				case ALTF_NFSV2:
					if (force3)
						errx(1,
						    "conflicting version options");
					force2 = 1;
					nfsargsp->flags &= ~NFSMNT_NFSV3;
					break;
				case ALTF_RDIRPLUS:
					nfsargsp->flags |= NFSMNT_RDIRPLUS;
					break;
				case ALTF_MNTUDP:
					mnttcp_ok = 0;
					break;
				case ALTF_RESVPORT:
					nfsargsp->flags |= NFSMNT_RESVPORT;
					break;
				case ALTF_SOFT:
					nfsargsp->flags |= NFSMNT_SOFT;
					break;
				case ALTF_TCP:
					nfsargsp->sotype = SOCK_STREAM;
					nfsproto = IPPROTO_TCP;
					break;
				case ALTF_PORT:
					port_no = value.ival;
					break;
				case ALTF_NOAC:
					nfsargsp->flags |= (NFSMNT_ACREGMIN |
					    NFSMNT_ACREGMAX | NFSMNT_ACDIRMIN |
					    NFSMNT_ACDIRMAX);
					nfsargsp->acregmin = 0;
					nfsargsp->acregmax = 0;
					nfsargsp->acdirmin = 0;
					nfsargsp->acdirmax = 0;
					break;
				case ALTF_ACREGMIN:
					nfsargsp->flags |= NFSMNT_ACREGMIN;
					nfsargsp->acregmin = value.ival;
					break;
				case ALTF_ACREGMAX:
					nfsargsp->flags |= NFSMNT_ACREGMAX;
					nfsargsp->acregmax = value.ival;
					break;
				case ALTF_ACDIRMIN:
					nfsargsp->flags |= NFSMNT_ACDIRMIN;
					nfsargsp->acdirmin = value.ival;
					break;
				case ALTF_ACDIRMAX:
					nfsargsp->flags |= NFSMNT_ACDIRMAX;
					nfsargsp->acdirmax = value.ival;
					break;
				}
			}
			break;
		case 'P':
			/* backward compatibility */
			break;
		case 'R':
			num = (int) strtonum(optarg, 1, INT_MAX, &p);
			if (p)
				errx(1, "illegal -R value %s: %s", optarg, p);
			retrycnt = num;
			break;
		case 'r':
			num = (int) strtonum(optarg, 1, INT_MAX, &p);
			if (p)
				errx(1, "illegal -r value %s: %s", optarg, p);
			nfsargsp->rsize = num;
			nfsargsp->flags |= NFSMNT_RSIZE;
			break;
		case 's':
			nfsargsp->flags |= NFSMNT_SOFT;
			break;
		case 'T':
			nfsargsp->sotype = SOCK_STREAM;
			nfsproto = IPPROTO_TCP;
			break;
		case 't':
			num = (int) strtonum(optarg, 1, INT_MAX, &p);
			if (p)
				errx(1, "illegal -t value %s: %s", optarg, p);
			nfsargsp->timeo = num;
			nfsargsp->flags |= NFSMNT_TIMEO;
			break;
		case 'w':
			num = (int) strtonum(optarg, 1, INT_MAX, &p);
			if (p)
				errx(1, "illegal -w value %s: %s", optarg, p);
			nfsargsp->wsize = num;
			nfsargsp->flags |= NFSMNT_WSIZE;
			break;
		case 'x':
			num = (int) strtonum(optarg, 1, INT_MAX, &p);
			if (p)
				errx(1, "illegal -x value %s: %s", optarg, p);
			nfsargsp->retrans = num;
			nfsargsp->flags |= NFSMNT_RETRANS;
			break;
		case 'U':
			mnttcp_ok = 0;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	spec = *argv++;
	if (realpath(*argv, name) == NULL)
		err(1, "realpath %s", *argv);

	if (!getnfsargs(spec, nfsargsp))
		exit(1);
	if (mount(MOUNT_NFS, name, mntflags, nfsargsp)) {
		if (errno == EOPNOTSUPP)
			errx(1, "%s: Filesystem not supported by kernel", name);
		else
			err(1, "%s", name);
	}
	exit(0);
}

int
getnfsargs(char *spec, struct nfs_args *nfsargsp)
{
	CLIENT *clp;
	struct addrinfo hints, *res;
	static struct sockaddr_in saddr;
	struct timeval pertry, try;
	enum clnt_stat clnt_stat;
	int so = RPC_ANYSOCK, i, nfsvers, mntvers, orgcnt;
	char *hostp, *delimp;
	u_short tport;
	static struct nfhret nfhret;
	static char nam[MNAMELEN + 1];

	if (strlcpy(nam, spec, sizeof(nam)) >= sizeof(nam)) {
		errx(1, "hostname too long");
	}

	if ((delimp = strchr(spec, '@')) != NULL) {
		hostp = delimp + 1;
	} else if ((delimp = strchr(spec, ':')) != NULL) {
		hostp = spec;
		spec = delimp + 1;
	} else {
		warnx("no <host>:<dirpath> or <dirpath>@<host> spec");
		return (0);
	}
	*delimp = '\0';

	/*
	 * Handle an internet host address
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if (getaddrinfo(hostp, NULL, &hints, &res) != 0) {
		warnx("can't resolve address for host %s", hostp);
		return (0);
	}
	saddr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo(res);

	if (force2) {
		nfsvers = NFS_VER2;
		mntvers = RPCMNT_VER1;
	} else {
		nfsvers = NFS_VER3;
		mntvers = RPCMNT_VER3;
	}
	orgcnt = retrycnt;
tryagain:
	nfhret.stat = EACCES;	/* Mark not yet successful */
	while (retrycnt > 0) {
		saddr.sin_family = AF_INET;
		saddr.sin_port = htons(PMAPPORT);
		if ((tport = port_no ? port_no : pmap_getport(&saddr,
		    RPCPROG_NFS, nfsvers, nfsargsp->sotype == SOCK_STREAM ?
		    IPPROTO_TCP : IPPROTO_UDP)) == 0) {
			if ((opflags & ISBGRND) == 0)
				clnt_pcreateerror("NFS Portmap");
		} else {
			saddr.sin_port = 0;
			pertry.tv_sec = 10;
			pertry.tv_usec = 0;
			if (mnttcp_ok && nfsargsp->sotype == SOCK_STREAM)
			    clp = clnttcp_create(&saddr, RPCPROG_MNT, mntvers,
				&so, 0, 0);
			else
			    clp = clntudp_create(&saddr, RPCPROG_MNT, mntvers,
				pertry, &so);
			if (clp == NULL) {
				if ((opflags & ISBGRND) == 0)
					clnt_pcreateerror("Cannot MNT RPC");
			} else {
				clp->cl_auth = authunix_create_default();
				try.tv_sec = 10;
				try.tv_usec = 0;
				nfhret.auth = RPCAUTH_UNIX;
				nfhret.vers = mntvers;
				clnt_stat = clnt_call(clp, RPCMNT_MOUNT,
				    xdr_dir, spec, xdr_fh, &nfhret, try);
				if (clnt_stat != RPC_SUCCESS) {
					if (clnt_stat == RPC_PROGVERSMISMATCH) {
						if (nfsvers == NFS_VER3 &&
						    !force3) {
							retrycnt = orgcnt;
							nfsvers = NFS_VER2;
							mntvers = RPCMNT_VER1;
							nfsargsp->flags &=
							    ~NFSMNT_NFSV3;
							goto tryagain;
						} else {
							warnx("%s",
							    clnt_sperror(clp,
								"MNT RPC"));
						}
					}
					if ((opflags & ISBGRND) == 0)
						warnx("%s", clnt_sperror(clp,
						    "bad MNT RPC"));
				} else {
					auth_destroy(clp->cl_auth);
					clnt_destroy(clp);
					retrycnt = 0;
				}
			}
		}
		if (--retrycnt > 0) {
			if (opflags & BGRND) {
				opflags &= ~BGRND;
				if ((i = fork())) {
					if (i == -1)
						err(1, "fork");
					exit(0);
				}
				(void) setsid();
				(void) close(STDIN_FILENO);
				(void) close(STDOUT_FILENO);
				(void) close(STDERR_FILENO);
				(void) chdir("/");
				opflags |= ISBGRND;
			}
			sleep(60);
		}
	}
	if (nfhret.stat) {
		if (opflags & ISBGRND)
			exit(1);
		warnc(nfhret.stat, "can't access %s", spec);
		return (0);
	}
	saddr.sin_port = htons(tport);
	nfsargsp->addr = (struct sockaddr *) &saddr;
	nfsargsp->addrlen = sizeof (saddr);
	nfsargsp->fh = nfhret.nfh;
	nfsargsp->fhsize = nfhret.fhsize;
	nfsargsp->hostname = nam;
	return (1);
}

/*
 * xdr routines for mount rpc's
 */
int
xdr_dir(XDR *xdrsp, char *dirp)
{
	return (xdr_string(xdrsp, &dirp, RPCMNT_PATHLEN));
}

int
xdr_fh(XDR *xdrsp, struct nfhret *np)
{
	int i;
	long auth, authcnt, authfnd = 0;

	if (!xdr_u_long(xdrsp, &np->stat))
		return (0);
	if (np->stat)
		return (1);
	switch (np->vers) {
	case 1:
		np->fhsize = NFSX_V2FH;
		return (xdr_opaque(xdrsp, (caddr_t)np->nfh, NFSX_V2FH));
	case 3:
		if (!xdr_long(xdrsp, &np->fhsize))
			return (0);
		if (np->fhsize <= 0 || np->fhsize > NFSX_V3FHMAX)
			return (0);
		if (!xdr_opaque(xdrsp, (caddr_t)np->nfh, np->fhsize))
			return (0);
		if (!xdr_long(xdrsp, &authcnt))
			return (0);
		for (i = 0; i < authcnt; i++) {
			if (!xdr_long(xdrsp, &auth))
				return (0);
			if (auth == np->auth)
				authfnd++;
		}
		/*
		 * Some servers, such as DEC's OSF/1 return a nil authenticator
		 * list to indicate RPCAUTH_UNIX.
		 */
		if (!authfnd && (authcnt > 0 || np->auth != RPCAUTH_UNIX))
			np->stat = EAUTH;
		return (1);
	}
	return (0);
}

__dead void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr,
	    "usage: %s [-23bcdilsTU] [-a maxreadahead] [-g maxgroups]\n"
	    "\t[-I readdirsize] [-o options] [-R retrycnt] [-r readsize]\n"
	    "\t[-t timeout] [-w writesize] [-x retrans] rhost:path node\n",
	    __progname);
	exit(1);
}
