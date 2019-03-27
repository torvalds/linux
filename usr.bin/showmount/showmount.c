/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1995
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993, 1995\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)showmount.c	8.3 (Berkeley) 3/29/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <err.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include <rpcsvc/mount.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

/* Constant defs */
#define	ALL	1
#define	DIRS	2

#define	DODUMP			0x1
#define	DOEXPORTS		0x2
#define	DOPARSABLEEXPORTS	0x4

struct mountlist {
	struct mountlist *ml_left;
	struct mountlist *ml_right;
	char	ml_host[MNTNAMLEN+1];
	char	ml_dirp[MNTPATHLEN+1];
};

struct grouplist {
	struct grouplist *gr_next;
	char	gr_name[MNTNAMLEN+1];
};

struct exportslist {
	struct exportslist *ex_next;
	struct grouplist *ex_groups;
	char	ex_dirp[MNTPATHLEN+1];
};

static struct mountlist *mntdump;
static struct exportslist *exportslist;
static int type = 0;

void print_dump(struct mountlist *);
static void usage(void);
int xdr_mntdump(XDR *, struct mountlist **);
int xdr_exportslist(XDR *, struct exportslist **);
int tcp_callrpc(const char *host, int prognum, int versnum, int procnum,
		xdrproc_t inproc, char *in, xdrproc_t outproc, char *out);

/*
 * This command queries the NFS mount daemon for it's mount list and/or
 * it's exports list and prints them out.
 * See "NFS: Network File System Protocol Specification, RFC1094, Appendix A"
 * and the "Network File System Protocol XXX.."
 * for detailed information on the protocol.
 */
int
main(int argc, char **argv)
{
	char strvised[MNTPATHLEN * 4 + 1];
	register struct exportslist *exp;
	register struct grouplist *grp;
	register int rpcs = 0, mntvers = 3;
	const char *host;
	int ch, estat, nbytes;

	while ((ch = getopt(argc, argv, "adEe13")) != -1)
		switch (ch) {
		case 'a':
			if (type == 0) {
				type = ALL;
				rpcs |= DODUMP;
			} else
				usage();
			break;
		case 'd':
			if (type == 0) {
				type = DIRS;
				rpcs |= DODUMP;
			} else
				usage();
			break;
		case 'E':
			rpcs |= DOPARSABLEEXPORTS;
			break;
		case 'e':
			rpcs |= DOEXPORTS;
			break;
		case '1':
			mntvers = 1;
			break;
		case '3':
			mntvers = 3;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if ((rpcs & DOPARSABLEEXPORTS) != 0) {
		if ((rpcs & DOEXPORTS) != 0)
			errx(1, "-E cannot be used with -e");
		if ((rpcs & DODUMP) != 0)
			errx(1, "-E cannot be used with -a or -d");
	}

	if (argc > 0)
		host = *argv;
	else
		host = "localhost";

	if (rpcs == 0)
		rpcs = DODUMP;

	if (rpcs & DODUMP)
		if ((estat = tcp_callrpc(host, MOUNTPROG, mntvers,
			MOUNTPROC_DUMP, (xdrproc_t)xdr_void, (char *)0,
			(xdrproc_t)xdr_mntdump, (char *)&mntdump)) != 0) {
			clnt_perrno(estat);
			errx(1, "can't do mountdump rpc");
		}
	if (rpcs & (DOEXPORTS | DOPARSABLEEXPORTS))
		if ((estat = tcp_callrpc(host, MOUNTPROG, mntvers,
			MOUNTPROC_EXPORT, (xdrproc_t)xdr_void, (char *)0,
			(xdrproc_t)xdr_exportslist, (char *)&exportslist)) != 0) {
			clnt_perrno(estat);
			errx(1, "can't do exports rpc");
		}

	/* Now just print out the results */
	if (rpcs & DODUMP) {
		switch (type) {
		case ALL:
			printf("All mount points on %s:\n", host);
			break;
		case DIRS:
			printf("Directories on %s:\n", host);
			break;
		default:
			printf("Hosts on %s:\n", host);
			break;
		}
		print_dump(mntdump);
	}
	if (rpcs & DOEXPORTS) {
		printf("Exports list on %s:\n", host);
		exp = exportslist;
		while (exp) {
			printf("%-34s ", exp->ex_dirp);
			grp = exp->ex_groups;
			if (grp == NULL) {
				printf("Everyone\n");
			} else {
				while (grp) {
					printf("%s ", grp->gr_name);
					grp = grp->gr_next;
				}
				printf("\n");
			}
			exp = exp->ex_next;
		}
	}
	if (rpcs & DOPARSABLEEXPORTS) {
		exp = exportslist;
		while (exp) {
			nbytes = strsnvis(strvised, sizeof(strvised),
			    exp->ex_dirp, VIS_GLOB | VIS_NL, "\"'$");
			if (nbytes == -1)
				err(1, "strsnvis");
			printf("%s\n", strvised);
			exp = exp->ex_next;
		}
	}
	exit(0);
}

/*
 * tcp_callrpc has the same interface as callrpc, but tries to
 * use tcp as transport method in order to handle large replies.
 */
int 
tcp_callrpc(const char *host, int prognum, int versnum, int procnum,
    xdrproc_t inproc, char *in, xdrproc_t outproc, char *out)
{
	CLIENT *client;
	struct timeval timeout;
	int rval;

	if ((client = clnt_create(host, prognum, versnum, "tcp")) == NULL &&
	    (client = clnt_create(host, prognum, versnum, "udp")) == NULL)
		return ((int) rpc_createerr.cf_stat);

	timeout.tv_sec = 25;
	timeout.tv_usec = 0;
	rval = (int) clnt_call(client, procnum, 
			       inproc, in,
			       outproc, out,
			       timeout);
	clnt_destroy(client);
 	return rval;
}

/*
 * Xdr routine for retrieving the mount dump list
 */
int
xdr_mntdump(XDR *xdrsp, struct mountlist **mlp)
{
	register struct mountlist *mp;
	register struct mountlist *tp;
	register struct mountlist **otp;
	int val, val2;
	int bool;
	char *strp;

	*mlp = (struct mountlist *)0;
	if (!xdr_bool(xdrsp, &bool))
		return (0);
	while (bool) {
		mp = (struct mountlist *)malloc(sizeof(struct mountlist));
		if (mp == NULL)
			return (0);
		mp->ml_left = mp->ml_right = (struct mountlist *)0;
		strp = mp->ml_host;
		if (!xdr_string(xdrsp, &strp, MNTNAMLEN)) {
			free(mp);
			return (0);
		}
		strp = mp->ml_dirp;
		if (!xdr_string(xdrsp, &strp, MNTPATHLEN)) {
			free(mp);
			return (0);
		}

		/*
		 * Build a binary tree on sorted order of either host or dirp.
		 * Drop any duplications.
		 */
		if (*mlp == NULL) {
			*mlp = mp;
		} else {
			tp = *mlp;
			while (tp) {
				val = strcmp(mp->ml_host, tp->ml_host);
				val2 = strcmp(mp->ml_dirp, tp->ml_dirp);
				switch (type) {
				case ALL:
					if (val == 0) {
						if (val2 == 0) {
							free((caddr_t)mp);
							goto next;
						}
						val = val2;
					}
					break;
				case DIRS:
					if (val2 == 0) {
						free((caddr_t)mp);
						goto next;
					}
					val = val2;
					break;
				default:
					if (val == 0) {
						free((caddr_t)mp);
						goto next;
					}
					break;
				}
				if (val < 0) {
					otp = &tp->ml_left;
					tp = tp->ml_left;
				} else {
					otp = &tp->ml_right;
					tp = tp->ml_right;
				}
			}
			*otp = mp;
		}
next:
		if (!xdr_bool(xdrsp, &bool))
			return (0);
	}
	return (1);
}

/*
 * Xdr routine to retrieve exports list
 */
int
xdr_exportslist(XDR *xdrsp, struct exportslist **exp)
{
	register struct exportslist *ep;
	register struct grouplist *gp;
	int bool, grpbool;
	char *strp;

	*exp = (struct exportslist *)0;
	if (!xdr_bool(xdrsp, &bool))
		return (0);
	while (bool) {
		ep = (struct exportslist *)malloc(sizeof(struct exportslist));
		if (ep == NULL)
			return (0);
		ep->ex_groups = (struct grouplist *)0;
		strp = ep->ex_dirp;
		if (!xdr_string(xdrsp, &strp, MNTPATHLEN))
			return (0);
		if (!xdr_bool(xdrsp, &grpbool))
			return (0);
		while (grpbool) {
			gp = (struct grouplist *)malloc(sizeof(struct grouplist));
			if (gp == NULL)
				return (0);
			strp = gp->gr_name;
			if (!xdr_string(xdrsp, &strp, MNTNAMLEN))
				return (0);
			gp->gr_next = ep->ex_groups;
			ep->ex_groups = gp;
			if (!xdr_bool(xdrsp, &grpbool))
				return (0);
		}
		ep->ex_next = *exp;
		*exp = ep;
		if (!xdr_bool(xdrsp, &bool))
			return (0);
	}
	return (1);
}

static void
usage(void)
{
	fprintf(stderr, "usage: showmount [-a | -d] [-e3] [host]\n");
	exit(1);
}

/*
 * Print the binary tree in inorder so that output is sorted.
 */
void
print_dump(struct mountlist *mp)
{

	if (mp == NULL)
		return;
	if (mp->ml_left)
		print_dump(mp->ml_left);
	switch (type) {
	case ALL:
		printf("%s:%s\n", mp->ml_host, mp->ml_dirp);
		break;
	case DIRS:
		printf("%s\n", mp->ml_dirp);
		break;
	default:
		printf("%s\n", mp->ml_host);
		break;
	}
	if (mp->ml_right)
		print_dump(mp->ml_right);
}
