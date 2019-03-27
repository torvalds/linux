/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Rick Macklem
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/extattr.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <nfs/nfssvc.h>

#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfskpiport.h>
#include <fs/nfs/nfs.h>
#include <fs/nfs/nfsrvstate.h>

static void usage(void);

static struct option longopts[] = {
	{ "migrate",	required_argument,	NULL,	'm'	},
	{ "mirror",	required_argument,	NULL,	'r'	},
	{ NULL,		0,			NULL,	0	}
};

/*
 * This program creates a copy of the file's (first argument) data on the
 * new/recovering DS mirror.  If the file is already on the new/recovering
 * DS, it will simply exit(0).
 */
int
main(int argc, char *argv[])
{
	struct nfsd_pnfsd_args pnfsdarg;
	struct pnfsdsfile dsfile[NFSDEV_MAXMIRRORS];
	struct stat sb;
	struct statfs sf;
	struct addrinfo hints, *res, *nres;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	ssize_t xattrsize, xattrsize2;
	size_t mirlen;
	int ch, fnd, fndzero, i, migrateit, mirrorcnt, mirrorit, ret;
	int mirrorlevel;
	char host[MNAMELEN + NI_MAXHOST + 2], *cp;

	if (geteuid() != 0)
		errx(1, "Must be run as root/su");

	mirrorit = migrateit = 0;
	pnfsdarg.dspath = pnfsdarg.curdspath = NULL;
	while ((ch = getopt_long(argc, argv, "m:r:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'm':
			/* Migrate the file from the second DS to the first. */
			if (mirrorit != 0)
				errx(1, "-r and -m are mutually exclusive");
			migrateit = 1;
			pnfsdarg.curdspath = optarg;
			break;
		case 'r':
			/* Mirror the file on the specified DS. */
			if (migrateit != 0)
				errx(1, "-r and -m are mutually exclusive");
			mirrorit = 1;
			pnfsdarg.dspath = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (migrateit != 0) {
		if (argc != 2)
			usage();
		pnfsdarg.dspath = *argv++;
	} else if (argc != 1)
		usage();

	/* Get the pNFS service's mirror level. */
	mirlen = sizeof(mirrorlevel);
	ret = sysctlbyname("vfs.nfs.pnfsmirror", &mirrorlevel, &mirlen,
	    NULL, 0);
	if (ret < 0)
		errx(1, "Can't get vfs.nfs.pnfsmirror");

	if (pnfsdarg.dspath != NULL && pnfsdarg.curdspath != NULL &&
	    strcmp(pnfsdarg.dspath, pnfsdarg.curdspath) == 0)
		errx(1, "Can't migrate to same server");

	/*
	 * The host address and directory where the data storage file is
	 * located is in the extended attribute "pnfsd.dsfile".
	 */
	xattrsize = extattr_get_file(*argv, EXTATTR_NAMESPACE_SYSTEM,
	    "pnfsd.dsfile", dsfile, sizeof(dsfile));
	mirrorcnt = xattrsize / sizeof(struct pnfsdsfile);
	xattrsize2 = mirrorcnt * sizeof(struct pnfsdsfile);
	if (mirrorcnt < 1 || xattrsize != xattrsize2)
		errx(1, "Can't get extattr pnfsd.dsfile for %s", *argv);

	/* See if there is a 0.0.0.0 entry. */
	fndzero = 0;
	for (i = 0; i < mirrorcnt; i++) {
		if (dsfile[i].dsf_sin.sin_family == AF_INET &&
		    dsfile[i].dsf_sin.sin_addr.s_addr == 0)
			fndzero = 1;
	}

	/* If already mirrored for default case, just exit(0); */
	if (mirrorit == 0 && migrateit == 0 && (mirrorlevel < 2 ||
	    (fndzero == 0 && mirrorcnt >= mirrorlevel) ||
	    (fndzero != 0 && mirrorcnt > mirrorlevel)))
		exit(0);

	/* For the "-r" case, there must be a 0.0.0.0 entry. */
	if (mirrorit != 0 && (fndzero == 0 || mirrorlevel < 2 ||
	    mirrorcnt < 2 || mirrorcnt > mirrorlevel))
		exit(0);

	/* For pnfsdarg.dspath set, if it is already in list, just exit(0); */
	if (pnfsdarg.dspath != NULL) {
		/* Check the dspath to see that it's an NFS mount. */
		if (stat(pnfsdarg.dspath, &sb) < 0)
			errx(1, "Can't stat %s", pnfsdarg.dspath);
		if (!S_ISDIR(sb.st_mode))
			errx(1, "%s is not a directory", pnfsdarg.dspath);
		if (statfs(pnfsdarg.dspath, &sf) < 0)
			errx(1, "Can't fsstat %s", pnfsdarg.dspath);
		if (strcmp(sf.f_fstypename, "nfs") != 0)
			errx(1, "%s is not an NFS mount", pnfsdarg.dspath);
		if (strcmp(sf.f_mntonname, pnfsdarg.dspath) != 0)
			errx(1, "%s is not the mounted-on dir for the new DS",
			    pnfsdarg.dspath);
	
		/*
		 * Check the IP address of the NFS server against the entrie(s)
		 * in the extended attribute.
		 */
		strlcpy(host, sf.f_mntfromname, sizeof(host));
		cp = strchr(host, ':');
		if (cp == NULL)
			errx(1, "No <host>: in mount %s", host);
		*cp = '\0';
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(host, NULL, &hints, &res) != 0)
			errx(1, "Can't get address for %s", host);
		for (i = 0; i < mirrorcnt; i++) {
			nres = res;
			while (nres != NULL) {
				if (dsfile[i].dsf_sin.sin_family ==
				    nres->ai_family) {
					/*
					 * If there is already an entry for this
					 * DS, just exit(0), since copying isn't
					 * required.
					 */
					if (nres->ai_family == AF_INET &&
					    nres->ai_addrlen >= sizeof(sin)) {
						memcpy(&sin, nres->ai_addr,
						    sizeof(sin));
						if (sin.sin_addr.s_addr ==
						    dsfile[i].dsf_sin.sin_addr.s_addr)
							exit(0);
					} else if (nres->ai_family ==
					    AF_INET6 && nres->ai_addrlen >=
					    sizeof(sin6)) {
						memcpy(&sin6, nres->ai_addr,
						    sizeof(sin6));
						if (IN6_ARE_ADDR_EQUAL(&sin6.sin6_addr,
						    &dsfile[i].dsf_sin6.sin6_addr))
							exit(0);
					}
				}
				nres = nres->ai_next;
			}
		}
		freeaddrinfo(res);
	}

	/* For "-m", the pnfsdarg.curdspath must be in the list. */
	if (pnfsdarg.curdspath != NULL) {
		/* Check pnfsdarg.curdspath to see that it's an NFS mount. */
		if (stat(pnfsdarg.curdspath, &sb) < 0)
			errx(1, "Can't stat %s", pnfsdarg.curdspath);
		if (!S_ISDIR(sb.st_mode))
			errx(1, "%s is not a directory", pnfsdarg.curdspath);
		if (statfs(pnfsdarg.curdspath, &sf) < 0)
			errx(1, "Can't fsstat %s", pnfsdarg.curdspath);
		if (strcmp(sf.f_fstypename, "nfs") != 0)
			errx(1, "%s is not an NFS mount", pnfsdarg.curdspath);
		if (strcmp(sf.f_mntonname, pnfsdarg.curdspath) != 0)
			errx(1, "%s is not the mounted-on dir of the cur DS",
			    pnfsdarg.curdspath);
	
		/*
		 * Check the IP address of the NFS server against the entrie(s)
		 * in the extended attribute.
		 */
		strlcpy(host, sf.f_mntfromname, sizeof(host));
		cp = strchr(host, ':');
		if (cp == NULL)
			errx(1, "No <host>: in mount %s", host);
		*cp = '\0';
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(host, NULL, &hints, &res) != 0)
			errx(1, "Can't get address for %s", host);
		fnd = 0;
		for (i = 0; i < mirrorcnt && fnd == 0; i++) {
			nres = res;
			while (nres != NULL) {
				if (dsfile[i].dsf_sin.sin_family ==
				    nres->ai_family) {
					/*
					 * Note if the entry is found.
					 */
					if (nres->ai_family == AF_INET &&
					    nres->ai_addrlen >= sizeof(sin)) {
						memcpy(&sin, nres->ai_addr,
						    sizeof(sin));
						if (sin.sin_addr.s_addr ==
						    dsfile[i].dsf_sin.sin_addr.s_addr) {
							fnd = 1;
							break;
						}
					} else if (nres->ai_family ==
					    AF_INET6 && nres->ai_addrlen >=
					    sizeof(sin6)) {
						memcpy(&sin6, nres->ai_addr,
						    sizeof(sin6));
						if (IN6_ARE_ADDR_EQUAL(&sin6.sin6_addr,
						    &dsfile[i].dsf_sin6.sin6_addr)) {
							fnd = 1;
							break;
						}
					}
				}
				nres = nres->ai_next;
			}
		}
		freeaddrinfo(res);
		/*
		 * If not found just exit(0), since it is not on the
		 * source DS.
		 */
		if (fnd == 0)
			exit(0);
	}

	/* Do the copy via the nfssvc() syscall. */
	pnfsdarg.op = PNFSDOP_COPYMR;
	pnfsdarg.mdspath = *argv;
	ret = nfssvc(NFSSVC_PNFSDS, &pnfsdarg);
	if (ret < 0 && errno != EEXIST)
		err(1, "Copymr failed for file %s", *argv);
	exit(0);
}

static void
usage(void)
{

	fprintf(stderr, "pnfsdscopymr [-r recovered-DS-mounted-on-path] "
	    "[-m soure-DS-mounted-on-path destination-DS-mounted-on-path] "
	    "mds-filename");
	exit(1);
}

