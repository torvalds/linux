/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1992, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)mount_nfs.c	8.11 (Berkeley) 5/4/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/uio.h>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include <rpcsvc/nfs_prot.h>
#include <rpcsvc/mount.h>

#include <nfsclient/nfs.h>

#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <unistd.h>

#include "mntopts.h"
#include "mounttab.h"

/* Table for af,sotype -> netid conversions. */
static struct nc_protos {
	const char *netid;
	int af;
	int sotype;
} nc_protos[] = {
	{"udp",		AF_INET,	SOCK_DGRAM},
	{"tcp",		AF_INET,	SOCK_STREAM},
	{"udp6",	AF_INET6,	SOCK_DGRAM},
	{"tcp6",	AF_INET6,	SOCK_STREAM},
	{NULL,		0,		0}
};

struct nfhret {
	u_long		stat;
	long		vers;
	long		auth;
	long		fhsize;
	u_char		nfh[NFS3_FHSIZE];
};
#define	BGRND	1
#define	ISBGRND	2
#define	OF_NOINET4	4
#define	OF_NOINET6	8
static int retrycnt = -1;
static int opflags = 0;
static int nfsproto = IPPROTO_TCP;
static int mnttcp_ok = 1;
static int noconn = 0;
/* The 'portspec' is the server nfs port; NULL means look up via rpcbind. */
static const char *portspec = NULL;
static struct sockaddr *addr;
static int addrlen = 0;
static u_char *fh = NULL;
static int fhsize = 0;
static int secflavor = -1;
static int got_principal = 0;

static enum mountmode {
	ANY,
	V2,
	V3,
	V4
} mountmode = ANY;

/* Return codes for nfs_tryproto. */
enum tryret {
	TRYRET_SUCCESS,
	TRYRET_TIMEOUT,		/* No response received. */
	TRYRET_REMOTEERR,	/* Error received from remote server. */
	TRYRET_LOCALERR		/* Local failure. */
};

static int	sec_name_to_num(const char *sec);
static const char	*sec_num_to_name(int num);
static int	getnfsargs(char *, struct iovec **iov, int *iovlen);
/* void	set_rpc_maxgrouplist(int); */
static struct netconfig *getnetconf_cached(const char *netid);
static const char	*netidbytype(int af, int sotype);
static void	usage(void) __dead2;
static int	xdr_dir(XDR *, char *);
static int	xdr_fh(XDR *, struct nfhret *);
static enum tryret nfs_tryproto(struct addrinfo *ai, char *hostp, char *spec,
    char **errstr, struct iovec **iov, int *iovlen);
static enum tryret returncode(enum clnt_stat stat, struct rpc_err *rpcerr);

int
main(int argc, char *argv[])
{
	int c;
	struct iovec *iov;
	int num, iovlen;
	char *mntname, *p, *spec, *tmp;
	char mntpath[MAXPATHLEN], errmsg[255];
	char hostname[MAXHOSTNAMELEN + 1], gssn[MAXHOSTNAMELEN + 50];
	const char *gssname;

	iov = NULL;
	iovlen = 0;
	memset(errmsg, 0, sizeof(errmsg));
	gssname = NULL;

	while ((c = getopt(argc, argv,
	    "23a:bcdD:g:I:iLlNo:PR:r:sTt:w:x:U")) != -1)
		switch (c) {
		case '2':
			mountmode = V2;
			break;
		case '3':
			mountmode = V3;
			break;
		case 'a':
			printf("-a deprecated, use -o readahead=<value>\n");
			build_iovec(&iov, &iovlen, "readahead", optarg, (size_t)-1);
			break;
		case 'b':
			opflags |= BGRND;
			break;
		case 'c':
			printf("-c deprecated, use -o noconn\n");
			build_iovec(&iov, &iovlen, "noconn", NULL, 0);
			noconn = 1;
			break;
		case 'D':
			printf("-D deprecated, use -o deadthresh=<value>\n");
			build_iovec(&iov, &iovlen, "deadthresh", optarg, (size_t)-1);
			break;
		case 'd':
			printf("-d deprecated, use -o dumbtimer");
			build_iovec(&iov, &iovlen, "dumbtimer", NULL, 0);
			break;
		case 'g':
			printf("-g deprecated, use -o maxgroups");
			num = strtol(optarg, &p, 10);
			if (*p || num <= 0)
				errx(1, "illegal -g value -- %s", optarg);
			//set_rpc_maxgrouplist(num);
			build_iovec(&iov, &iovlen, "maxgroups", optarg, (size_t)-1);
			break;
		case 'I':
			printf("-I deprecated, use -o readdirsize=<value>\n");
			build_iovec(&iov, &iovlen, "readdirsize", optarg, (size_t)-1);
			break;
		case 'i':
			printf("-i deprecated, use -o intr\n");
			build_iovec(&iov, &iovlen, "intr", NULL, 0);
			break;
		case 'L':
			printf("-L deprecated, use -o nolockd\n");
			build_iovec(&iov, &iovlen, "nolockd", NULL, 0);
			break;
		case 'l':
			printf("-l deprecated, -o rdirplus\n");
			build_iovec(&iov, &iovlen, "rdirplus", NULL, 0);
			break;
		case 'N':
			printf("-N deprecated, do not specify -o resvport\n");
			break;
		case 'o': {
			int pass_flag_to_nmount;
			char *opt = optarg;
			while (opt) {
				char *pval = NULL;
				char *pnextopt = NULL;
				const char *val = "";
				pass_flag_to_nmount = 1;
				pnextopt = strchr(opt, ',');
				if (pnextopt != NULL) {
					*pnextopt = '\0';
					pnextopt++;
				}
				pval = strchr(opt, '=');
				if (pval != NULL) {
					*pval = '\0';
					val = pval + 1;
				}
				if (strcmp(opt, "bg") == 0) {
					opflags |= BGRND;
					pass_flag_to_nmount=0;
				} else if (strcmp(opt, "fg") == 0) {
					/* same as not specifying -o bg */
					pass_flag_to_nmount=0;
				} else if (strcmp(opt, "gssname") == 0) {
					pass_flag_to_nmount = 0;
					gssname = val;
				} else if (strcmp(opt, "mntudp") == 0) {
					mnttcp_ok = 0;
					nfsproto = IPPROTO_UDP;
				} else if (strcmp(opt, "udp") == 0) {
					nfsproto = IPPROTO_UDP;
				} else if (strcmp(opt, "tcp") == 0) {
					nfsproto = IPPROTO_TCP;
				} else if (strcmp(opt, "noinet4") == 0) {
					pass_flag_to_nmount=0;
					opflags |= OF_NOINET4;
				} else if (strcmp(opt, "noinet6") == 0) {
					pass_flag_to_nmount=0;
					opflags |= OF_NOINET6;
				} else if (strcmp(opt, "noconn") == 0) {
					noconn = 1; 
				} else if (strcmp(opt, "nfsv2") == 0) {
					pass_flag_to_nmount=0;
					mountmode = V2;
				} else if (strcmp(opt, "nfsv3") == 0) {
					mountmode = V3;
				} else if (strcmp(opt, "nfsv4") == 0) {
					pass_flag_to_nmount=0;
					mountmode = V4;
					nfsproto = IPPROTO_TCP;
					if (portspec == NULL)
						portspec = "2049";
				} else if (strcmp(opt, "port") == 0) {
					pass_flag_to_nmount=0;
					asprintf(&tmp, "%d", atoi(val));
					if (tmp == NULL)
						err(1, "asprintf");
					portspec = tmp;
				} else if (strcmp(opt, "principal") == 0) {
					got_principal = 1;
				} else if (strcmp(opt, "proto") == 0) {
					pass_flag_to_nmount=0;
					if (strcmp(val, "tcp") == 0) {
						nfsproto = IPPROTO_TCP;
						opflags |= OF_NOINET6;
						build_iovec(&iov, &iovlen,
						    "tcp", NULL, 0);
					} else if (strcmp(val, "udp") == 0) {
						mnttcp_ok = 0;
						nfsproto = IPPROTO_UDP;
						opflags |= OF_NOINET6;
						build_iovec(&iov, &iovlen,
						    "udp", NULL, 0);
					} else if (strcmp(val, "tcp6") == 0) {
						nfsproto = IPPROTO_TCP;
						opflags |= OF_NOINET4;
						build_iovec(&iov, &iovlen,
						    "tcp", NULL, 0);
					} else if (strcmp(val, "udp6") == 0) {
						mnttcp_ok = 0;
						nfsproto = IPPROTO_UDP;
						opflags |= OF_NOINET4;
						build_iovec(&iov, &iovlen,
						    "udp", NULL, 0);
					} else {
						errx(1,
						    "illegal proto value -- %s",
						    val);
					}
				} else if (strcmp(opt, "sec") == 0) {
					/*
					 * Don't add this option to
					 * the iovec yet - we will
					 * negotiate which sec flavor
					 * to use with the remote
					 * mountd.
					 */
					pass_flag_to_nmount=0;
					secflavor = sec_name_to_num(val);
					if (secflavor < 0) {
						errx(1,
						    "illegal sec value -- %s",
						    val);
					}
				} else if (strcmp(opt, "retrycnt") == 0) {
					pass_flag_to_nmount=0;
					num = strtol(val, &p, 10);
					if (*p || num < 0)
						errx(1, "illegal retrycnt value -- %s", val);
					retrycnt = num;
				} else if (strcmp(opt, "maxgroups") == 0) {
					num = strtol(val, &p, 10);
					if (*p || num <= 0)
						errx(1, "illegal maxgroups value -- %s", val);
					//set_rpc_maxgrouplist(num);
				} else if (strcmp(opt, "vers") == 0) {
					num = strtol(val, &p, 10);
					if (*p || num <= 0)
						errx(1, "illegal vers value -- "
						    "%s", val);
					switch (num) {
					case 2:
						mountmode = V2;
						break;
					case 3:
						mountmode = V3;
						build_iovec(&iov, &iovlen,
						    "nfsv3", NULL, 0);
						break;
					case 4:
						mountmode = V4;
						nfsproto = IPPROTO_TCP;
						if (portspec == NULL)
							portspec = "2049";
						break;
					default:
						errx(1, "illegal nfs version "
						    "value -- %s", val);
					}
					pass_flag_to_nmount=0;
				}
				if (pass_flag_to_nmount) {
					build_iovec(&iov, &iovlen, opt,
					    __DECONST(void *, val),
					    strlen(val) + 1);
				}
				opt = pnextopt;
			}
			}
			break;
		case 'P':
			/* obsolete for -o noresvport now default */
			printf("-P deprecated, use -o noresvport\n");
			build_iovec(&iov, &iovlen, "noresvport", NULL, 0);
			break;
		case 'R':
			printf("-R deprecated, use -o retrycnt=<retrycnt>\n");
			num = strtol(optarg, &p, 10);
			if (*p || num < 0)
				errx(1, "illegal -R value -- %s", optarg);
			retrycnt = num;
			break;
		case 'r':
			printf("-r deprecated, use -o rsize=<rsize>\n");
			build_iovec(&iov, &iovlen, "rsize", optarg, (size_t)-1);
			break;
		case 's':
			printf("-s deprecated, use -o soft\n");
			build_iovec(&iov, &iovlen, "soft", NULL, 0);
			break;
		case 'T':
			nfsproto = IPPROTO_TCP;
			printf("-T deprecated, use -o tcp\n");
			break;
		case 't':
			printf("-t deprecated, use -o timeout=<value>\n");
			build_iovec(&iov, &iovlen, "timeout", optarg, (size_t)-1);
			break;
		case 'w':
			printf("-w deprecated, use -o wsize=<value>\n");
			build_iovec(&iov, &iovlen, "wsize", optarg, (size_t)-1);
			break;
		case 'x':
			printf("-x deprecated, use -o retrans=<value>\n");
			build_iovec(&iov, &iovlen, "retrans", optarg, (size_t)-1);
			break;
		case 'U':
			printf("-U deprecated, use -o mntudp\n");
			mnttcp_ok = 0;
			nfsproto = IPPROTO_UDP;
			build_iovec(&iov, &iovlen, "mntudp", NULL, 0);
			break;
		default:
			usage();
			break;
		}
	argc -= optind;
	argv += optind;

	if (argc != 2) {
		usage();
		/* NOTREACHED */
	}

	spec = *argv++;
	mntname = *argv;

	if (retrycnt == -1)
		/* The default is to keep retrying forever. */
		retrycnt = 0;

	if (modfind("nfscl") < 0) {
		/* Not present in kernel, try loading it */
		if (kldload("nfscl") < 0 ||
		    modfind("nfscl") < 0)
			errx(1, "nfscl is not available");
	}

	/*
	 * Add the fqdn to the gssname, as required.
	 */
	if (gssname != NULL) {
		if (strchr(gssname, '@') == NULL &&
		    gethostname(hostname, MAXHOSTNAMELEN) == 0) {
			snprintf(gssn, sizeof (gssn), "%s@%s", gssname,
			    hostname);
			gssname = gssn;
		}
		build_iovec(&iov, &iovlen, "gssname",
		    __DECONST(void *, gssname), strlen(gssname) + 1);
	}

	if (!getnfsargs(spec, &iov, &iovlen))
		exit(1);

	/* resolve the mountpoint with realpath(3) */
	if (checkpath(mntname, mntpath) != 0)
		err(1, "%s", mntpath);

	build_iovec_argf(&iov, &iovlen, "fstype", "nfs");
	build_iovec(&iov, &iovlen, "fspath", mntpath, (size_t)-1);
	build_iovec(&iov, &iovlen, "errmsg", errmsg, sizeof(errmsg));

	if (nmount(iov, iovlen, 0))
		err(1, "nmount: %s%s%s", mntpath, errmsg[0] ? ", " : "",
		    errmsg);

	exit(0);
}

static int
sec_name_to_num(const char *sec)
{
	if (!strcmp(sec, "krb5"))
		return (RPCSEC_GSS_KRB5);
	if (!strcmp(sec, "krb5i"))
		return (RPCSEC_GSS_KRB5I);
	if (!strcmp(sec, "krb5p"))
		return (RPCSEC_GSS_KRB5P);
	if (!strcmp(sec, "sys"))
		return (AUTH_SYS);
	return (-1);
}

static const char *
sec_num_to_name(int flavor)
{
	switch (flavor) {
	case RPCSEC_GSS_KRB5:
		return ("krb5");
	case RPCSEC_GSS_KRB5I:
		return ("krb5i");
	case RPCSEC_GSS_KRB5P:
		return ("krb5p");
	case AUTH_SYS:
		return ("sys");
	}
	return (NULL);
}

static int
getnfsargs(char *spec, struct iovec **iov, int *iovlen)
{
	struct addrinfo hints, *ai_nfs, *ai;
	enum tryret ret;
	int ecode, speclen, remoteerr, offset, have_bracket = 0;
	char *hostp, *delimp, *errstr;
	size_t len;
	static char nam[MNAMELEN + 1], pname[MAXHOSTNAMELEN + 5];

	if (*spec == '[' && (delimp = strchr(spec + 1, ']')) != NULL &&
	    *(delimp + 1) == ':') {
		hostp = spec + 1;
		spec = delimp + 2;
		have_bracket = 1;
	} else if ((delimp = strrchr(spec, ':')) != NULL) {
		hostp = spec;
		spec = delimp + 1;
	} else if ((delimp = strrchr(spec, '@')) != NULL) {
		warnx("path@server syntax is deprecated, use server:path");
		hostp = delimp + 1;
	} else {
		warnx("no <host>:<dirpath> nfs-name");
		return (0);
	}
	*delimp = '\0';

	/*
	 * If there has been a trailing slash at mounttime it seems
	 * that some mountd implementations fail to remove the mount
	 * entries from their mountlist while unmounting.
	 */
	for (speclen = strlen(spec); 
		speclen > 1 && spec[speclen - 1] == '/';
		speclen--)
		spec[speclen - 1] = '\0';
	if (strlen(hostp) + strlen(spec) + 1 > MNAMELEN) {
		warnx("%s:%s: %s", hostp, spec, strerror(ENAMETOOLONG));
		return (0);
	}
	/* Make both '@' and ':' notations equal */
	if (*hostp != '\0') {
		len = strlen(hostp);
		offset = 0;
		if (have_bracket)
			nam[offset++] = '[';
		memmove(nam + offset, hostp, len);
		if (have_bracket)
			nam[len + offset++] = ']';
		nam[len + offset++] = ':';
		memmove(nam + len + offset, spec, speclen);
		nam[len + speclen + offset] = '\0';
	}

	/*
	 * Handle an internet host address.
	 */
	memset(&hints, 0, sizeof hints);
	hints.ai_flags = AI_NUMERICHOST;
	if (nfsproto == IPPROTO_TCP)
		hints.ai_socktype = SOCK_STREAM;
	else if (nfsproto == IPPROTO_UDP)
		hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(hostp, portspec, &hints, &ai_nfs) != 0) {
		hints.ai_flags = AI_CANONNAME;
		if ((ecode = getaddrinfo(hostp, portspec, &hints, &ai_nfs))
		    != 0) {
			if (portspec == NULL)
				errx(1, "%s: %s", hostp, gai_strerror(ecode));
			else
				errx(1, "%s:%s: %s", hostp, portspec,
				    gai_strerror(ecode));
			return (0);
		}

		/*
		 * For a Kerberized nfs mount where the "principal"
		 * argument has not been set, add it here.
		 */
		if (got_principal == 0 && secflavor != AUTH_SYS &&
		    ai_nfs->ai_canonname != NULL) {
			snprintf(pname, sizeof (pname), "nfs@%s",
			    ai_nfs->ai_canonname);
			build_iovec(iov, iovlen, "principal", pname,
			    strlen(pname) + 1);
		}
	}

	ret = TRYRET_LOCALERR;
	for (;;) {
		/*
		 * Try each entry returned by getaddrinfo(). Note the
		 * occurrence of remote errors by setting `remoteerr'.
		 */
		remoteerr = 0;
		for (ai = ai_nfs; ai != NULL; ai = ai->ai_next) {
			if ((ai->ai_family == AF_INET6) &&
			    (opflags & OF_NOINET6))
				continue;
			if ((ai->ai_family == AF_INET) && 
			    (opflags & OF_NOINET4))
				continue;
			ret = nfs_tryproto(ai, hostp, spec, &errstr, iov,
			    iovlen);
			if (ret == TRYRET_SUCCESS)
				break;
			if (ret != TRYRET_LOCALERR)
				remoteerr = 1;
			if ((opflags & ISBGRND) == 0)
				fprintf(stderr, "%s\n", errstr);
		}
		if (ret == TRYRET_SUCCESS)
			break;

		/* Exit if all errors were local. */
		if (!remoteerr)
			exit(1);

		/*
		 * If retrycnt == 0, we are to keep retrying forever.
		 * Otherwise decrement it, and exit if it hits zero.
		 */
		if (retrycnt != 0 && --retrycnt == 0)
			exit(1);

		if ((opflags & (BGRND | ISBGRND)) == BGRND) {
			warnx("Cannot immediately mount %s:%s, backgrounding",
			    hostp, spec);
			opflags |= ISBGRND;
			if (daemon(0, 0) != 0)
				err(1, "daemon");
		}
		sleep(60);
	}
	freeaddrinfo(ai_nfs);

	build_iovec(iov, iovlen, "hostname", nam, (size_t)-1);
	/* Add mounted file system to PATH_MOUNTTAB */
	if (mountmode != V4 && !add_mtab(hostp, spec))
		warnx("can't update %s for %s:%s", PATH_MOUNTTAB, hostp, spec);
	return (1);
}

/*
 * Try to set up the NFS arguments according to the address
 * family, protocol (and possibly port) specified in `ai'.
 *
 * Returns TRYRET_SUCCESS if successful, or:
 *   TRYRET_TIMEOUT		The server did not respond.
 *   TRYRET_REMOTEERR		The server reported an error.
 *   TRYRET_LOCALERR		Local failure.
 *
 * In all error cases, *errstr will be set to a statically-allocated string
 * describing the error.
 */
static enum tryret
nfs_tryproto(struct addrinfo *ai, char *hostp, char *spec, char **errstr,
    struct iovec **iov, int *iovlen)
{
	static char errbuf[256];
	struct sockaddr_storage nfs_ss;
	struct netbuf nfs_nb;
	struct nfhret nfhret;
	struct timeval try;
	struct rpc_err rpcerr;
	CLIENT *clp;
	struct netconfig *nconf, *nconf_mnt;
	const char *netid, *netid_mnt, *secname;
	int doconnect, nfsvers, mntvers, sotype;
	enum clnt_stat clntstat;
	enum mountmode trymntmode;

	sotype = 0;
	trymntmode = mountmode;
	errbuf[0] = '\0';
	*errstr = errbuf;

	if (nfsproto == IPPROTO_TCP)
		sotype = SOCK_STREAM;
	else if (nfsproto == IPPROTO_UDP)
		sotype = SOCK_DGRAM;

	if ((netid = netidbytype(ai->ai_family, sotype)) == NULL) {
		snprintf(errbuf, sizeof errbuf,
		    "af %d sotype %d not supported", ai->ai_family, sotype);
		return (TRYRET_LOCALERR);
	}
	if ((nconf = getnetconf_cached(netid)) == NULL) {
		snprintf(errbuf, sizeof errbuf, "%s: %s", netid, nc_sperror());
		return (TRYRET_LOCALERR);
	}
	/* The RPCPROG_MNT netid may be different. */
	if (mnttcp_ok) {
		netid_mnt = netid;
		nconf_mnt = nconf;
	} else {
		if ((netid_mnt = netidbytype(ai->ai_family, SOCK_DGRAM))
		     == NULL) {
			snprintf(errbuf, sizeof errbuf,
			    "af %d sotype SOCK_DGRAM not supported",
			     ai->ai_family);
			return (TRYRET_LOCALERR);
		}
		if ((nconf_mnt = getnetconf_cached(netid_mnt)) == NULL) {
			snprintf(errbuf, sizeof errbuf, "%s: %s", netid_mnt,
			    nc_sperror());
			return (TRYRET_LOCALERR);
		}
	}

tryagain:
	if (trymntmode == V4) {
		nfsvers = 4;
		mntvers = 3; /* Workaround for GCC. */
	} else if (trymntmode == V2) {
		nfsvers = 2;
		mntvers = 1;
	} else {
		nfsvers = 3;
		mntvers = 3;
	}

	if (portspec != NULL) {
		/* `ai' contains the complete nfsd sockaddr. */
		nfs_nb.buf = ai->ai_addr;
		nfs_nb.len = nfs_nb.maxlen = ai->ai_addrlen;
	} else {
		/* Ask the remote rpcbind. */
		nfs_nb.buf = &nfs_ss;
		nfs_nb.len = nfs_nb.maxlen = sizeof nfs_ss;

		if (!rpcb_getaddr(NFS_PROGRAM, nfsvers, nconf, &nfs_nb,
		    hostp)) {
			if (rpc_createerr.cf_stat == RPC_PROGVERSMISMATCH &&
			    trymntmode == ANY) {
				trymntmode = V2;
				goto tryagain;
			}
			snprintf(errbuf, sizeof errbuf, "[%s] %s:%s: %s",
			    netid, hostp, spec,
			    clnt_spcreateerror("RPCPROG_NFS"));
			return (returncode(rpc_createerr.cf_stat,
			    &rpc_createerr.cf_error));
		}
	}

	/* Check that the server (nfsd) responds on the port we have chosen. */
	clp = clnt_tli_create(RPC_ANYFD, nconf, &nfs_nb, NFS_PROGRAM, nfsvers,
	    0, 0);
	if (clp == NULL) {
		snprintf(errbuf, sizeof errbuf, "[%s] %s:%s: %s", netid,
		    hostp, spec, clnt_spcreateerror("nfsd: RPCPROG_NFS"));
		return (returncode(rpc_createerr.cf_stat,
		    &rpc_createerr.cf_error));
	}
	if (sotype == SOCK_DGRAM && noconn == 0) {
		/*
		 * Use connect(), to match what the kernel does. This
		 * catches cases where the server responds from the
		 * wrong source address.
		 */
		doconnect = 1;
		if (!clnt_control(clp, CLSET_CONNECT, (char *)&doconnect)) {
			clnt_destroy(clp);
			snprintf(errbuf, sizeof errbuf,
			    "[%s] %s:%s: CLSET_CONNECT failed", netid, hostp,
			    spec);
			return (TRYRET_LOCALERR);
		}
	}

	try.tv_sec = 10;
	try.tv_usec = 0;
	clntstat = clnt_call(clp, NFSPROC_NULL, (xdrproc_t)xdr_void, NULL,
			 (xdrproc_t)xdr_void, NULL, try);
	if (clntstat != RPC_SUCCESS) {
		if (clntstat == RPC_PROGVERSMISMATCH && trymntmode == ANY) {
			clnt_destroy(clp);
			trymntmode = V2;
			goto tryagain;
		}
		clnt_geterr(clp, &rpcerr);
		snprintf(errbuf, sizeof errbuf, "[%s] %s:%s: %s", netid,
		    hostp, spec, clnt_sperror(clp, "NFSPROC_NULL"));
		clnt_destroy(clp);
		return (returncode(clntstat, &rpcerr));
	}
	clnt_destroy(clp);

	/*
	 * For NFSv4, there is no mount protocol.
	 */
	if (trymntmode == V4) {
		/*
		 * Store the server address in nfsargsp, making
		 * sure to copy any locally allocated structures.
		 */
		addrlen = nfs_nb.len;
		addr = malloc(addrlen);
		if (addr == NULL)
			err(1, "malloc");
		bcopy(nfs_nb.buf, addr, addrlen);

		build_iovec(iov, iovlen, "addr", addr, addrlen);
		secname = sec_num_to_name(secflavor);
		if (secname != NULL) {
			build_iovec(iov, iovlen, "sec",
			    __DECONST(void *, secname), (size_t)-1);
		}
		build_iovec(iov, iovlen, "nfsv4", NULL, 0);
		build_iovec(iov, iovlen, "dirpath", spec, (size_t)-1);

		return (TRYRET_SUCCESS);
	}

	/* Send the MOUNTPROC_MNT RPC to get the root filehandle. */
	try.tv_sec = 10;
	try.tv_usec = 0;
	clp = clnt_tp_create(hostp, MOUNTPROG, mntvers, nconf_mnt);
	if (clp == NULL) {
		snprintf(errbuf, sizeof errbuf, "[%s] %s:%s: %s", netid_mnt,
		    hostp, spec, clnt_spcreateerror("RPCMNT: clnt_create"));
		return (returncode(rpc_createerr.cf_stat,
		    &rpc_createerr.cf_error));
	}
	clp->cl_auth = authsys_create_default();
	nfhret.auth = secflavor;
	nfhret.vers = mntvers;
	clntstat = clnt_call(clp, MOUNTPROC_MNT, (xdrproc_t)xdr_dir, spec, 
			 (xdrproc_t)xdr_fh, &nfhret,
	    try);
	auth_destroy(clp->cl_auth);
	if (clntstat != RPC_SUCCESS) {
		if (clntstat == RPC_PROGVERSMISMATCH && trymntmode == ANY) {
			clnt_destroy(clp);
			trymntmode = V2;
			goto tryagain;
		}
		clnt_geterr(clp, &rpcerr);
		snprintf(errbuf, sizeof errbuf, "[%s] %s:%s: %s", netid_mnt,
		    hostp, spec, clnt_sperror(clp, "RPCPROG_MNT"));
		clnt_destroy(clp);
		return (returncode(clntstat, &rpcerr));
	}
	clnt_destroy(clp);

	if (nfhret.stat != 0) {
		snprintf(errbuf, sizeof errbuf, "[%s] %s:%s: %s", netid_mnt,
		    hostp, spec, strerror(nfhret.stat));
		return (TRYRET_REMOTEERR);
	}

	/*
	 * Store the filehandle and server address in nfsargsp, making
	 * sure to copy any locally allocated structures.
	 */
	addrlen = nfs_nb.len;
	addr = malloc(addrlen);
	fhsize = nfhret.fhsize;
	fh = malloc(fhsize);
	if (addr == NULL || fh == NULL)
		err(1, "malloc");
	bcopy(nfs_nb.buf, addr, addrlen);
	bcopy(nfhret.nfh, fh, fhsize);

	build_iovec(iov, iovlen, "addr", addr, addrlen);
	build_iovec(iov, iovlen, "fh", fh, fhsize);
	secname = sec_num_to_name(nfhret.auth);
	if (secname) {
		build_iovec(iov, iovlen, "sec",
		    __DECONST(void *, secname), (size_t)-1);
	}
	if (nfsvers == 3)
		build_iovec(iov, iovlen, "nfsv3", NULL, 0);

	return (TRYRET_SUCCESS);
}

/*
 * Catagorise a RPC return status and error into an `enum tryret'
 * return code.
 */
static enum tryret
returncode(enum clnt_stat clntstat, struct rpc_err *rpcerr)
{

	switch (clntstat) {
	case RPC_TIMEDOUT:
		return (TRYRET_TIMEOUT);
	case RPC_PMAPFAILURE:
	case RPC_PROGNOTREGISTERED:
	case RPC_PROGVERSMISMATCH:
	/* XXX, these can be local or remote. */
	case RPC_CANTSEND:
	case RPC_CANTRECV:
		return (TRYRET_REMOTEERR);
	case RPC_SYSTEMERROR:
		switch (rpcerr->re_errno) {
		case ETIMEDOUT:
			return (TRYRET_TIMEOUT);
		case ENOMEM:
			break;
		default:
			return (TRYRET_REMOTEERR);
		}
		/* FALLTHROUGH */
	default:
		break;
	}
	return (TRYRET_LOCALERR);
}

/*
 * Look up a netid based on an address family and socket type.
 * `af' is the address family, and `sotype' is SOCK_DGRAM or SOCK_STREAM.
 *
 * XXX there should be a library function for this.
 */
static const char *
netidbytype(int af, int sotype)
{
	struct nc_protos *p;

	for (p = nc_protos; p->netid != NULL; p++) {
		if (af != p->af || sotype != p->sotype)
			continue;
		return (p->netid);
	}
	return (NULL);
}

/*
 * Look up a netconfig entry based on a netid, and cache the result so
 * that we don't need to remember to call freenetconfigent().
 *
 * Otherwise it behaves just like getnetconfigent(), so nc_*error()
 * work on failure.
 */
static struct netconfig *
getnetconf_cached(const char *netid)
{
	static struct nc_entry {
		struct netconfig *nconf;
		struct nc_entry *next;
	} *head;
	struct nc_entry *p;
	struct netconfig *nconf;

	for (p = head; p != NULL; p = p->next)
		if (strcmp(netid, p->nconf->nc_netid) == 0)
			return (p->nconf);

	if ((nconf = getnetconfigent(netid)) == NULL)
		return (NULL);
	if ((p = malloc(sizeof(*p))) == NULL)
		err(1, "malloc");
	p->nconf = nconf;
	p->next = head;
	head = p;

	return (p->nconf);
}

/*
 * xdr routines for mount rpc's
 */
static int
xdr_dir(XDR *xdrsp, char *dirp)
{
	return (xdr_string(xdrsp, &dirp, MNTPATHLEN));
}

static int
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
		np->fhsize = NFS_FHSIZE;
		return (xdr_opaque(xdrsp, (caddr_t)np->nfh, NFS_FHSIZE));
	case 3:
		if (!xdr_long(xdrsp, &np->fhsize))
			return (0);
		if (np->fhsize <= 0 || np->fhsize > NFS3_FHSIZE)
			return (0);
		if (!xdr_opaque(xdrsp, (caddr_t)np->nfh, np->fhsize))
			return (0);
		if (!xdr_long(xdrsp, &authcnt))
			return (0);
		for (i = 0; i < authcnt; i++) {
			if (!xdr_long(xdrsp, &auth))
				return (0);
			if (np->auth == -1) {
				np->auth = auth;
				authfnd++;
			} else if (auth == np->auth) {
				authfnd++;
			}
		}
		/*
		 * Some servers, such as DEC's OSF/1 return a nil authenticator
		 * list to indicate RPCAUTH_UNIX.
		 */
		if (authcnt == 0 && np->auth == -1)
			np->auth = AUTH_SYS;
		if (!authfnd && (authcnt > 0 || np->auth != AUTH_SYS))
			np->stat = EAUTH;
		return (1);
	}
	return (0);
}

static void
usage(void)
{
	(void)fprintf(stderr, "%s\n%s\n%s\n%s\n",
"usage: mount_nfs [-23bcdiLlNPsTU] [-a maxreadahead] [-D deadthresh]",
"                 [-g maxgroups] [-I readdirsize] [-o options] [-R retrycnt]",
"                 [-r readsize] [-t timeout] [-w writesize] [-x retrans]",
"                 rhost:path node");
	exit(1);
}
