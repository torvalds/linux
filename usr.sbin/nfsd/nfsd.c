/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
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
"@(#) Copyright (c) 1989, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)nfsd.c	8.9 (Berkeley) 3/29/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/syslog.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/fcntl.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpcsvc/nfs_prot.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <nfs/nfssvc.h>

#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfskpiport.h>
#include <fs/nfs/nfs.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include <getopt.h>

static int	debug = 0;

#define	NFSD_STABLERESTART	"/var/db/nfs-stablerestart"
#define	NFSD_STABLEBACKUP	"/var/db/nfs-stablerestart.bak"
#define	MAXNFSDCNT	256
#define	DEFNFSDCNT	 4
#define	NFS_VER2	 2
#define NFS_VER3	 3
#define NFS_VER4	 4
static pid_t children[MAXNFSDCNT]; /* PIDs of children */
static pid_t masterpid;		   /* PID of master/parent */
static int nfsdcnt;		/* number of children */
static int nfsdcnt_set;
static int minthreads;
static int maxthreads;
static int nfssvc_nfsd;		/* Set to correct NFSSVC_xxx flag */
static int stablefd = -1;	/* Fd for the stable restart file */
static int backupfd;		/* Fd for the backup stable restart file */
static const char *getopt_shortopts;
static const char *getopt_usage;

static int minthreads_set;
static int maxthreads_set;

static struct option longopts[] = {
	{ "debug", no_argument, &debug, 1 },
	{ "minthreads", required_argument, &minthreads_set, 1 },
	{ "maxthreads", required_argument, &maxthreads_set, 1 },
	{ "pnfs", required_argument, NULL, 'p' },
	{ "mirror", required_argument, NULL, 'm' },
	{ NULL, 0, NULL, 0}
};

static void	cleanup(int);
static void	child_cleanup(int);
static void	killchildren(void);
static void	nfsd_exit(int);
static void	nonfs(int);
static void	reapchild(int);
static int	setbindhost(struct addrinfo **ia, const char *bindhost,
		    struct addrinfo hints);
static void	start_server(int, struct nfsd_nfsd_args *, const char *vhost);
static void	unregistration(void);
static void	usage(void);
static void	open_stable(int *, int *);
static void	copy_stable(int, int);
static void	backup_stable(int);
static void	set_nfsdcnt(int);
static void	parse_dsserver(const char *, struct nfsd_nfsd_args *);

/*
 * Nfs server daemon mostly just a user context for nfssvc()
 *
 * 1 - do file descriptor and signal cleanup
 * 2 - fork the nfsd(s)
 * 3 - create server socket(s)
 * 4 - register socket with rpcbind
 *
 * For connectionless protocols, just pass the socket into the kernel via.
 * nfssvc().
 * For connection based sockets, loop doing accepts. When you get a new
 * socket from accept, pass the msgsock into the kernel via. nfssvc().
 * The arguments are:
 *	-r - reregister with rpcbind
 *	-d - unregister with rpcbind
 *	-t - support tcp nfs clients
 *	-u - support udp nfs clients
 *	-e - forces it to run a server that supports nfsv4
 *	-p - enable a pNFS service
 *	-m - set the mirroring level for a pNFS service
 * followed by "n" which is the number of nfsds' to fork off
 */
int
main(int argc, char **argv)
{
	struct nfsd_addsock_args addsockargs;
	struct addrinfo *ai_udp, *ai_tcp, *ai_udp6, *ai_tcp6, hints;
	struct netconfig *nconf_udp, *nconf_tcp, *nconf_udp6, *nconf_tcp6;
	struct netbuf nb_udp, nb_tcp, nb_udp6, nb_tcp6;
	struct sockaddr_storage peer;
	fd_set ready, sockbits;
	int ch, connect_type_cnt, i, maxsock, msgsock;
	socklen_t len;
	int on = 1, unregister, reregister, sock;
	int tcp6sock, ip6flag, tcpflag, tcpsock;
	int udpflag, ecode, error, s;
	int bindhostc, bindanyflag, rpcbreg, rpcbregcnt;
	int nfssvc_addsock;
	int longindex = 0;
	int nfs_minvers = NFS_VER2;
	size_t nfs_minvers_size;
	const char *lopt;
	char **bindhost = NULL;
	pid_t pid;
	struct nfsd_nfsd_args nfsdargs;
	const char *vhostname = NULL;

	nfsdargs.mirrorcnt = 1;
	nfsdargs.addr = NULL;
	nfsdargs.addrlen = 0;
	nfsdcnt = DEFNFSDCNT;
	unregister = reregister = tcpflag = maxsock = 0;
	bindanyflag = udpflag = connect_type_cnt = bindhostc = 0;
	getopt_shortopts = "ah:n:rdtuep:m:V:";
	getopt_usage =
	    "usage:\n"
	    "  nfsd [-ardtue] [-h bindip]\n"
	    "       [-n numservers] [--minthreads #] [--maxthreads #]\n"
	    "       [-p/--pnfs dsserver0:/dsserver0-mounted-on-dir,...,"
	    "dsserverN:/dsserverN-mounted-on-dir] [-m mirrorlevel]\n"
	    "       [-V virtual_hostname]\n";
	while ((ch = getopt_long(argc, argv, getopt_shortopts, longopts,
		    &longindex)) != -1)
		switch (ch) {
		case 'V':
			if (strlen(optarg) <= MAXHOSTNAMELEN)
				vhostname = optarg;
			else
				warnx("Virtual host name (%s) is too long",
				    optarg);
			break;
		case 'a':
			bindanyflag = 1;
			break;
		case 'n':
			set_nfsdcnt(atoi(optarg));
			break;
		case 'h':
			bindhostc++;
			bindhost = realloc(bindhost,sizeof(char *)*bindhostc);
			if (bindhost == NULL) 
				errx(1, "Out of memory");
			bindhost[bindhostc-1] = strdup(optarg);
			if (bindhost[bindhostc-1] == NULL)
				errx(1, "Out of memory");
			break;
		case 'r':
			reregister = 1;
			break;
		case 'd':
			unregister = 1;
			break;
		case 't':
			tcpflag = 1;
			break;
		case 'u':
			udpflag = 1;
			break;
		case 'e':
			/* now a no-op, since this is the default */
			break;
		case 'p':
			/* Parse out the DS server host names and mount pts. */
			parse_dsserver(optarg, &nfsdargs);
			break;
		case 'm':
			/* Set the mirror level for a pNFS service. */
			i = atoi(optarg);
			if (i < 2 || i > NFSDEV_MAXMIRRORS)
				errx(1, "Mirror level out of range 2<-->%d",
				    NFSDEV_MAXMIRRORS);
			nfsdargs.mirrorcnt = i;
			break;
		case 0:
			lopt = longopts[longindex].name;
			if (!strcmp(lopt, "minthreads")) {
				minthreads = atoi(optarg);
			} else if (!strcmp(lopt, "maxthreads")) {
				maxthreads = atoi(optarg);
			}
			break;
		default:
		case '?':
			usage();
		}
	if (!tcpflag && !udpflag)
		udpflag = 1;
	argv += optind;
	argc -= optind;
	if (minthreads_set && maxthreads_set && minthreads > maxthreads)
		errx(EX_USAGE,
		    "error: minthreads(%d) can't be greater than "
		    "maxthreads(%d)", minthreads, maxthreads);

	/*
	 * XXX
	 * Backward compatibility, trailing number is the count of daemons.
	 */
	if (argc > 1)
		usage();
	if (argc == 1)
		set_nfsdcnt(atoi(argv[0]));

	/*
	 * Unless the "-o" option was specified, try and run "nfsd".
	 * If "-o" was specified, try and run "nfsserver".
	 */
	if (modfind("nfsd") < 0) {
		/* Not present in kernel, try loading it */
		if (kldload("nfsd") < 0 || modfind("nfsd") < 0)
			errx(1, "NFS server is not available");
	}

	ip6flag = 1;
	s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1) {
		if (errno != EPROTONOSUPPORT && errno != EAFNOSUPPORT)
			err(1, "socket");
		ip6flag = 0;
	} else if (getnetconfigent("udp6") == NULL ||
		getnetconfigent("tcp6") == NULL) {
		ip6flag = 0;
	}
	if (s != -1)
		close(s);

	if (bindhostc == 0 || bindanyflag) {
		bindhostc++;
		bindhost = realloc(bindhost,sizeof(char *)*bindhostc);
		if (bindhost == NULL) 
			errx(1, "Out of memory");
		bindhost[bindhostc-1] = strdup("*");
		if (bindhost[bindhostc-1] == NULL) 
			errx(1, "Out of memory");
	}

	nfs_minvers_size = sizeof(nfs_minvers);
	error = sysctlbyname("vfs.nfsd.server_min_nfsvers", &nfs_minvers,
	    &nfs_minvers_size, NULL, 0);
	if (error != 0 || nfs_minvers < NFS_VER2 || nfs_minvers > NFS_VER4) {
		warnx("sysctlbyname(vfs.nfsd.server_min_nfsvers) failed,"
		    " defaulting to NFSv2");
		nfs_minvers = NFS_VER2;
	}

	if (unregister) {
		unregistration();
		exit (0);
	}
	if (reregister) {
		if (udpflag) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			ecode = getaddrinfo(NULL, "nfs", &hints, &ai_udp);
			if (ecode != 0)
				err(1, "getaddrinfo udp: %s", gai_strerror(ecode));
			nconf_udp = getnetconfigent("udp");
			if (nconf_udp == NULL)
				err(1, "getnetconfigent udp failed");
			nb_udp.buf = ai_udp->ai_addr;
			nb_udp.len = nb_udp.maxlen = ai_udp->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_udp,
				    &nb_udp))
					err(1, "rpcb_set udp failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_udp,
				    &nb_udp))
					err(1, "rpcb_set udp failed");
			freeaddrinfo(ai_udp);
		}
		if (udpflag && ip6flag) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			ecode = getaddrinfo(NULL, "nfs", &hints, &ai_udp6);
			if (ecode != 0)
				err(1, "getaddrinfo udp6: %s", gai_strerror(ecode));
			nconf_udp6 = getnetconfigent("udp6");
			if (nconf_udp6 == NULL)
				err(1, "getnetconfigent udp6 failed");
			nb_udp6.buf = ai_udp6->ai_addr;
			nb_udp6.len = nb_udp6.maxlen = ai_udp6->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_udp6,
				    &nb_udp6))
					err(1, "rpcb_set udp6 failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_udp6,
				    &nb_udp6))
					err(1, "rpcb_set udp6 failed");
			freeaddrinfo(ai_udp6);
		}
		if (tcpflag) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			ecode = getaddrinfo(NULL, "nfs", &hints, &ai_tcp);
			if (ecode != 0)
				err(1, "getaddrinfo tcp: %s", gai_strerror(ecode));
			nconf_tcp = getnetconfigent("tcp");
			if (nconf_tcp == NULL)
				err(1, "getnetconfigent tcp failed");
			nb_tcp.buf = ai_tcp->ai_addr;
			nb_tcp.len = nb_tcp.maxlen = ai_tcp->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_tcp,
				    &nb_tcp))
					err(1, "rpcb_set tcp failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_tcp,
				    &nb_tcp))
					err(1, "rpcb_set tcp failed");
			freeaddrinfo(ai_tcp);
		}
		if (tcpflag && ip6flag) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			ecode = getaddrinfo(NULL, "nfs", &hints, &ai_tcp6);
			if (ecode != 0)
				err(1, "getaddrinfo tcp6: %s", gai_strerror(ecode));
			nconf_tcp6 = getnetconfigent("tcp6");
			if (nconf_tcp6 == NULL)
				err(1, "getnetconfigent tcp6 failed");
			nb_tcp6.buf = ai_tcp6->ai_addr;
			nb_tcp6.len = nb_tcp6.maxlen = ai_tcp6->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_tcp6,
				    &nb_tcp6))
					err(1, "rpcb_set tcp6 failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_tcp6, 
				   &nb_tcp6))
					err(1, "rpcb_set tcp6 failed");
			freeaddrinfo(ai_tcp6);
		}
		exit (0);
	}
	if (debug == 0) {
		daemon(0, 0);
		(void)signal(SIGHUP, SIG_IGN);
		(void)signal(SIGINT, SIG_IGN);
		/*
		 * nfsd sits in the kernel most of the time.  It needs
		 * to ignore SIGTERM/SIGQUIT in order to stay alive as long
		 * as possible during a shutdown, otherwise loopback
		 * mounts will not be able to unmount. 
		 */
		(void)signal(SIGTERM, SIG_IGN);
		(void)signal(SIGQUIT, SIG_IGN);
	}
	(void)signal(SIGSYS, nonfs);
	(void)signal(SIGCHLD, reapchild);
	(void)signal(SIGUSR2, backup_stable);

	openlog("nfsd", LOG_PID | (debug ? LOG_PERROR : 0), LOG_DAEMON);

	/*
	 * For V4, we open the stablerestart file and call nfssvc()
	 * to get it loaded. This is done before the daemons do the
	 * regular nfssvc() call to service NFS requests.
	 * (This way the file remains open until the last nfsd is killed
	 *  off.)
	 * It and the backup copy will be created as empty files
	 * the first time this nfsd is started and should never be
	 * deleted/replaced if at all possible. It should live on a
	 * local, non-volatile storage device that does not do hardware
	 * level write-back caching. (See SCSI doc for more information
	 * on how to prevent write-back caching on SCSI disks.)
	 */
	open_stable(&stablefd, &backupfd);
	if (stablefd < 0) {
		syslog(LOG_ERR, "Can't open %s: %m\n", NFSD_STABLERESTART);
		exit(1);
	}
	/* This system call will fail for old kernels, but that's ok. */
	nfssvc(NFSSVC_BACKUPSTABLE, NULL);
	if (nfssvc(NFSSVC_STABLERESTART, (caddr_t)&stablefd) < 0) {
		syslog(LOG_ERR, "Can't read stable storage file: %m\n");
		exit(1);
	}
	nfssvc_addsock = NFSSVC_NFSDADDSOCK;
	nfssvc_nfsd = NFSSVC_NFSDNFSD | NFSSVC_NEWSTRUCT;

	if (tcpflag) {
		/*
		 * For TCP mode, we fork once to start the first
		 * kernel nfsd thread. The kernel will add more
		 * threads as needed.
		 */
		masterpid = getpid();
		pid = fork();
		if (pid == -1) {
			syslog(LOG_ERR, "fork: %m");
			nfsd_exit(1);
		}
		if (pid) {
			children[0] = pid;
		} else {
			(void)signal(SIGUSR1, child_cleanup);
			setproctitle("server");
			start_server(0, &nfsdargs, vhostname);
		}
	}

	(void)signal(SIGUSR1, cleanup);
	FD_ZERO(&sockbits);
 
	rpcbregcnt = 0;
	/* Set up the socket for udp and rpcb register it. */
	if (udpflag) {
		rpcbreg = 0;
		for (i = 0; i < bindhostc; i++) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			if (setbindhost(&ai_udp, bindhost[i], hints) == 0) {
				rpcbreg = 1;
				rpcbregcnt++;
				if ((sock = socket(ai_udp->ai_family,
				    ai_udp->ai_socktype,
				    ai_udp->ai_protocol)) < 0) {
					syslog(LOG_ERR,
					    "can't create udp socket");
					nfsd_exit(1);
				}
				if (bind(sock, ai_udp->ai_addr,
				    ai_udp->ai_addrlen) < 0) {
					syslog(LOG_ERR,
					    "can't bind udp addr %s: %m",
					    bindhost[i]);
					nfsd_exit(1);
				}
				freeaddrinfo(ai_udp);
				addsockargs.sock = sock;
				addsockargs.name = NULL;
				addsockargs.namelen = 0;
				if (nfssvc(nfssvc_addsock, &addsockargs) < 0) {
					syslog(LOG_ERR, "can't Add UDP socket");
					nfsd_exit(1);
				}
				(void)close(sock);
			}
		}
		if (rpcbreg == 1) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			ecode = getaddrinfo(NULL, "nfs", &hints, &ai_udp);
			if (ecode != 0) {
				syslog(LOG_ERR, "getaddrinfo udp: %s",
				   gai_strerror(ecode));
				nfsd_exit(1);
			}
			nconf_udp = getnetconfigent("udp");
			if (nconf_udp == NULL)
				err(1, "getnetconfigent udp failed");
			nb_udp.buf = ai_udp->ai_addr;
			nb_udp.len = nb_udp.maxlen = ai_udp->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_udp,
				    &nb_udp))
					err(1, "rpcb_set udp failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_udp,
				    &nb_udp))
					err(1, "rpcb_set udp failed");
			freeaddrinfo(ai_udp);
		}
	}

	/* Set up the socket for udp6 and rpcb register it. */
	if (udpflag && ip6flag) {
		rpcbreg = 0;
		for (i = 0; i < bindhostc; i++) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			if (setbindhost(&ai_udp6, bindhost[i], hints) == 0) {
				rpcbreg = 1;
				rpcbregcnt++;
				if ((sock = socket(ai_udp6->ai_family,
				    ai_udp6->ai_socktype,
				    ai_udp6->ai_protocol)) < 0) {
					syslog(LOG_ERR,
						"can't create udp6 socket");
					nfsd_exit(1);
				}
				if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
				    &on, sizeof on) < 0) {
					syslog(LOG_ERR,
					    "can't set v6-only binding for "
					    "udp6 socket: %m");
					nfsd_exit(1);
				}
				if (bind(sock, ai_udp6->ai_addr,
				    ai_udp6->ai_addrlen) < 0) {
					syslog(LOG_ERR,
					    "can't bind udp6 addr %s: %m",
					    bindhost[i]);
					nfsd_exit(1);
				}
				freeaddrinfo(ai_udp6);
				addsockargs.sock = sock;
				addsockargs.name = NULL;
				addsockargs.namelen = 0;
				if (nfssvc(nfssvc_addsock, &addsockargs) < 0) {
					syslog(LOG_ERR,
					    "can't add UDP6 socket");
					nfsd_exit(1);
				}
				(void)close(sock);    
			}
		}
		if (rpcbreg == 1) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			ecode = getaddrinfo(NULL, "nfs", &hints, &ai_udp6);
			if (ecode != 0) {
				syslog(LOG_ERR, "getaddrinfo udp6: %s",
				   gai_strerror(ecode));
				nfsd_exit(1);
			}
			nconf_udp6 = getnetconfigent("udp6");
			if (nconf_udp6 == NULL)
				err(1, "getnetconfigent udp6 failed");
			nb_udp6.buf = ai_udp6->ai_addr;
			nb_udp6.len = nb_udp6.maxlen = ai_udp6->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_udp6,
				    &nb_udp6))
					err(1,
					    "rpcb_set udp6 failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_udp6,
				    &nb_udp6))
					err(1,
					    "rpcb_set udp6 failed");
			freeaddrinfo(ai_udp6);
		}
	}

	/* Set up the socket for tcp and rpcb register it. */
	if (tcpflag) {
		rpcbreg = 0;
		for (i = 0; i < bindhostc; i++) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			if (setbindhost(&ai_tcp, bindhost[i], hints) == 0) {
				rpcbreg = 1;
				rpcbregcnt++;
				if ((tcpsock = socket(AF_INET, SOCK_STREAM,
				    0)) < 0) {
					syslog(LOG_ERR,
					    "can't create tcp socket");
					nfsd_exit(1);
				}
				if (setsockopt(tcpsock, SOL_SOCKET,
				    SO_REUSEADDR,
				    (char *)&on, sizeof(on)) < 0)
					syslog(LOG_ERR,
					     "setsockopt SO_REUSEADDR: %m");
				if (bind(tcpsock, ai_tcp->ai_addr,
				    ai_tcp->ai_addrlen) < 0) {
					syslog(LOG_ERR,
					    "can't bind tcp addr %s: %m",
					    bindhost[i]);
					nfsd_exit(1);
				}
				if (listen(tcpsock, -1) < 0) {
					syslog(LOG_ERR, "listen failed");
					nfsd_exit(1);
				}
				freeaddrinfo(ai_tcp);
				FD_SET(tcpsock, &sockbits);
				maxsock = tcpsock;
				connect_type_cnt++;
			}
		}
		if (rpcbreg == 1) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			ecode = getaddrinfo(NULL, "nfs", &hints,
			     &ai_tcp);
			if (ecode != 0) {
				syslog(LOG_ERR, "getaddrinfo tcp: %s",
				   gai_strerror(ecode));
				nfsd_exit(1);
			}
			nconf_tcp = getnetconfigent("tcp");
			if (nconf_tcp == NULL)
				err(1, "getnetconfigent tcp failed");
			nb_tcp.buf = ai_tcp->ai_addr;
			nb_tcp.len = nb_tcp.maxlen = ai_tcp->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_tcp,
				    &nb_tcp))
					err(1, "rpcb_set tcp failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_tcp,
				    &nb_tcp))
					err(1, "rpcb_set tcp failed");
			freeaddrinfo(ai_tcp);
		}
	}

	/* Set up the socket for tcp6 and rpcb register it. */
	if (tcpflag && ip6flag) {
		rpcbreg = 0;
		for (i = 0; i < bindhostc; i++) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			if (setbindhost(&ai_tcp6, bindhost[i], hints) == 0) {
				rpcbreg = 1;
				rpcbregcnt++;
				if ((tcp6sock = socket(ai_tcp6->ai_family,
				    ai_tcp6->ai_socktype,
				    ai_tcp6->ai_protocol)) < 0) {
					syslog(LOG_ERR,
					    "can't create tcp6 socket");
					nfsd_exit(1);
				}
				if (setsockopt(tcp6sock, SOL_SOCKET,
				    SO_REUSEADDR,
				    (char *)&on, sizeof(on)) < 0)
					syslog(LOG_ERR,
					    "setsockopt SO_REUSEADDR: %m");
				if (setsockopt(tcp6sock, IPPROTO_IPV6,
				    IPV6_V6ONLY, &on, sizeof on) < 0) {
					syslog(LOG_ERR,
					"can't set v6-only binding for tcp6 "
					    "socket: %m");
					nfsd_exit(1);
				}
				if (bind(tcp6sock, ai_tcp6->ai_addr,
				    ai_tcp6->ai_addrlen) < 0) {
					syslog(LOG_ERR,
					    "can't bind tcp6 addr %s: %m",
					    bindhost[i]);
					nfsd_exit(1);
				}
				if (listen(tcp6sock, -1) < 0) {
					syslog(LOG_ERR, "listen failed");
					nfsd_exit(1);
				}
				freeaddrinfo(ai_tcp6);
				FD_SET(tcp6sock, &sockbits);
				if (maxsock < tcp6sock)
					maxsock = tcp6sock;
				connect_type_cnt++;
			}
		}
		if (rpcbreg == 1) {
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = AF_INET6;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			ecode = getaddrinfo(NULL, "nfs", &hints, &ai_tcp6);
			if (ecode != 0) {
				syslog(LOG_ERR, "getaddrinfo tcp6: %s",
				   gai_strerror(ecode));
				nfsd_exit(1);
			}
			nconf_tcp6 = getnetconfigent("tcp6");
			if (nconf_tcp6 == NULL)
				err(1, "getnetconfigent tcp6 failed");
			nb_tcp6.buf = ai_tcp6->ai_addr;
			nb_tcp6.len = nb_tcp6.maxlen = ai_tcp6->ai_addrlen;
			if (nfs_minvers == NFS_VER2)
				if (!rpcb_set(NFS_PROGRAM, 2, nconf_tcp6,
				    &nb_tcp6))
					err(1, "rpcb_set tcp6 failed");
			if (nfs_minvers <= NFS_VER3)
				if (!rpcb_set(NFS_PROGRAM, 3, nconf_tcp6,
				    &nb_tcp6))
					err(1, "rpcb_set tcp6 failed");
			freeaddrinfo(ai_tcp6);
		}
	}

	if (rpcbregcnt == 0) {
		syslog(LOG_ERR, "rpcb_set() failed, nothing to do: %m");
		nfsd_exit(1);
	}

	if (tcpflag && connect_type_cnt == 0) {
		syslog(LOG_ERR, "tcp connects == 0, nothing to do: %m");
		nfsd_exit(1);
	}

	setproctitle("master");
	/*
	 * We always want a master to have a clean way to shut nfsd down
	 * (with unregistration): if the master is killed, it unregisters and
	 * kills all children. If we run for UDP only (and so do not have to
	 * loop waiting for accept), we instead make the parent
	 * a "server" too. start_server will not return.
	 */
	if (!tcpflag)
		start_server(1, &nfsdargs, vhostname);

	/*
	 * Loop forever accepting connections and passing the sockets
	 * into the kernel for the mounts.
	 */
	for (;;) {
		ready = sockbits;
		if (connect_type_cnt > 1) {
			if (select(maxsock + 1,
			    &ready, NULL, NULL, NULL) < 1) {
				error = errno;
				if (error == EINTR)
					continue;
				syslog(LOG_ERR, "select failed: %m");
				nfsd_exit(1);
			}
		}
		for (tcpsock = 0; tcpsock <= maxsock; tcpsock++) {
			if (FD_ISSET(tcpsock, &ready)) {
				len = sizeof(peer);
				if ((msgsock = accept(tcpsock,
				    (struct sockaddr *)&peer, &len)) < 0) {
					error = errno;
					syslog(LOG_ERR, "accept failed: %m");
					if (error == ECONNABORTED ||
					    error == EINTR)
						continue;
					nfsd_exit(1);
				}
				if (setsockopt(msgsock, SOL_SOCKET,
				    SO_KEEPALIVE, (char *)&on, sizeof(on)) < 0)
					syslog(LOG_ERR,
					    "setsockopt SO_KEEPALIVE: %m");
				addsockargs.sock = msgsock;
				addsockargs.name = (caddr_t)&peer;
				addsockargs.namelen = len;
				nfssvc(nfssvc_addsock, &addsockargs);
				(void)close(msgsock);
			}
		}
	}
}

static int
setbindhost(struct addrinfo **ai, const char *bindhost, struct addrinfo hints)
{
	int ecode;
	u_int32_t host_addr[4];  /* IPv4 or IPv6 */
	const char *hostptr;

	if (bindhost == NULL || strcmp("*", bindhost) == 0)
		hostptr = NULL;
	else
		hostptr = bindhost;

	if (hostptr != NULL) {
		switch (hints.ai_family) {
		case AF_INET:
			if (inet_pton(AF_INET, hostptr, host_addr) == 1) {
				hints.ai_flags = AI_NUMERICHOST;
			} else {
				if (inet_pton(AF_INET6, hostptr,
				    host_addr) == 1)
					return (1);
			}
			break;
		case AF_INET6:
			if (inet_pton(AF_INET6, hostptr, host_addr) == 1) {
				hints.ai_flags = AI_NUMERICHOST;
			} else {
				if (inet_pton(AF_INET, hostptr,
				    host_addr) == 1)
					return (1);
			}
			break;
		default:
			break;
		}
	}
	
	ecode = getaddrinfo(hostptr, "nfs", &hints, ai);
	if (ecode != 0) {
		syslog(LOG_ERR, "getaddrinfo %s: %s", bindhost,
		    gai_strerror(ecode));
		return (1);
	}
	return (0);
}

static void
set_nfsdcnt(int proposed)
{

	if (proposed < 1) {
		warnx("nfsd count too low %d; reset to %d", proposed,
		    DEFNFSDCNT);
		nfsdcnt = DEFNFSDCNT;
	} else if (proposed > MAXNFSDCNT) {
		warnx("nfsd count too high %d; truncated to %d", proposed,
		    MAXNFSDCNT);
		nfsdcnt = MAXNFSDCNT;
	} else
		nfsdcnt = proposed;
	nfsdcnt_set = 1;
}

static void
usage(void)
{
	(void)fprintf(stderr, "%s", getopt_usage);
	exit(1);
}

static void
nonfs(__unused int signo)
{
	syslog(LOG_ERR, "missing system call: NFS not available");
}

static void
reapchild(__unused int signo)
{
	pid_t pid;
	int i;

	while ((pid = wait3(NULL, WNOHANG, NULL)) > 0) {
		for (i = 0; i < nfsdcnt; i++)
			if (pid == children[i])
				children[i] = -1;
	}
}

static void
unregistration(void)
{
	if ((!rpcb_unset(NFS_PROGRAM, 2, NULL)) ||
	    (!rpcb_unset(NFS_PROGRAM, 3, NULL)))
		syslog(LOG_ERR, "rpcb_unset failed");
}

static void
killchildren(void)
{
	int i;

	for (i = 0; i < nfsdcnt; i++) {
		if (children[i] > 0)
			kill(children[i], SIGKILL);
	}
}

/*
 * Cleanup master after SIGUSR1.
 */
static void
cleanup(__unused int signo)
{
	nfsd_exit(0);
}

/*
 * Cleanup child after SIGUSR1.
 */
static void
child_cleanup(__unused int signo)
{
	exit(0);
}

static void
nfsd_exit(int status)
{
	killchildren();
	unregistration();
	exit(status);
}

static int
get_tuned_nfsdcount(void)
{
	int ncpu, error, tuned_nfsdcnt;
	size_t ncpu_size;

	ncpu_size = sizeof(ncpu);
	error = sysctlbyname("hw.ncpu", &ncpu, &ncpu_size, NULL, 0);
	if (error) {
		warnx("sysctlbyname(hw.ncpu) failed defaulting to %d nfs servers",
		    DEFNFSDCNT);
		tuned_nfsdcnt = DEFNFSDCNT;
	} else {
		tuned_nfsdcnt = ncpu * 8;
	}
	return tuned_nfsdcnt;
}

static void
start_server(int master, struct nfsd_nfsd_args *nfsdargp, const char *vhost)
{
	char principal[MAXHOSTNAMELEN + 5];
	int status, error;
	char hostname[MAXHOSTNAMELEN + 1], *cp;
	struct addrinfo *aip, hints;

	status = 0;
	if (vhost == NULL)
		gethostname(hostname, sizeof (hostname));
	else
		strlcpy(hostname, vhost, sizeof (hostname));
	snprintf(principal, sizeof (principal), "nfs@%s", hostname);
	if ((cp = strchr(hostname, '.')) == NULL ||
	    *(cp + 1) == '\0') {
		/* If not fully qualified, try getaddrinfo() */
		memset((void *)&hints, 0, sizeof (hints));
		hints.ai_flags = AI_CANONNAME;
		error = getaddrinfo(hostname, NULL, &hints, &aip);
		if (error == 0) {
			if (aip->ai_canonname != NULL &&
			    (cp = strchr(aip->ai_canonname, '.')) !=
			    NULL && *(cp + 1) != '\0')
				snprintf(principal, sizeof (principal),
				    "nfs@%s", aip->ai_canonname);
			freeaddrinfo(aip);
		}
	}
	nfsdargp->principal = principal;

	if (nfsdcnt_set)
		nfsdargp->minthreads = nfsdargp->maxthreads = nfsdcnt;
	else {
		nfsdargp->minthreads = minthreads_set ? minthreads : get_tuned_nfsdcount();
		nfsdargp->maxthreads = maxthreads_set ? maxthreads : nfsdargp->minthreads;
		if (nfsdargp->maxthreads < nfsdargp->minthreads)
			nfsdargp->maxthreads = nfsdargp->minthreads;
	}
	error = nfssvc(nfssvc_nfsd, nfsdargp);
	if (error < 0 && errno == EAUTH) {
		/*
		 * This indicates that it could not register the
		 * rpcsec_gss credentials, usually because the
		 * gssd daemon isn't running.
		 * (only the experimental server with nfsv4)
		 */
		syslog(LOG_ERR, "No gssd, using AUTH_SYS only");
		principal[0] = '\0';
		error = nfssvc(nfssvc_nfsd, nfsdargp);
	}
	if (error < 0) {
		if (errno == ENXIO) {
			syslog(LOG_ERR, "Bad -p option, cannot run");
			if (masterpid != 0 && master == 0)
				kill(masterpid, SIGUSR1);
		} else
			syslog(LOG_ERR, "nfssvc: %m");
		status = 1;
	}
	if (master)
		nfsd_exit(status);
	else
		exit(status);
}

/*
 * Open the stable restart file and return the file descriptor for it.
 */
static void
open_stable(int *stable_fdp, int *backup_fdp)
{
	int stable_fd, backup_fd = -1, ret;
	struct stat st, backup_st;

	/* Open and stat the stable restart file. */
	stable_fd = open(NFSD_STABLERESTART, O_RDWR, 0);
	if (stable_fd < 0)
		stable_fd = open(NFSD_STABLERESTART, O_RDWR | O_CREAT, 0600);
	if (stable_fd >= 0) {
		ret = fstat(stable_fd, &st);
		if (ret < 0) {
			close(stable_fd);
			stable_fd = -1;
		}
	}

	/* Open and stat the backup stable restart file. */
	if (stable_fd >= 0) {
		backup_fd = open(NFSD_STABLEBACKUP, O_RDWR, 0);
		if (backup_fd < 0)
			backup_fd = open(NFSD_STABLEBACKUP, O_RDWR | O_CREAT,
			    0600);
		if (backup_fd >= 0) {
			ret = fstat(backup_fd, &backup_st);
			if (ret < 0) {
				close(backup_fd);
				backup_fd = -1;
			}
		}
		if (backup_fd < 0) {
			close(stable_fd);
			stable_fd = -1;
		}
	}

	*stable_fdp = stable_fd;
	*backup_fdp = backup_fd;
	if (stable_fd < 0)
		return;

	/* Sync up the 2 files, as required. */
	if (st.st_size > 0)
		copy_stable(stable_fd, backup_fd);
	else if (backup_st.st_size > 0)
		copy_stable(backup_fd, stable_fd);
}

/*
 * Copy the stable restart file to the backup or vice versa.
 */
static void
copy_stable(int from_fd, int to_fd)
{
	int cnt, ret;
	static char buf[1024];

	ret = lseek(from_fd, (off_t)0, SEEK_SET);
	if (ret >= 0)
		ret = lseek(to_fd, (off_t)0, SEEK_SET);
	if (ret >= 0)
		ret = ftruncate(to_fd, (off_t)0);
	if (ret >= 0)
		do {
			cnt = read(from_fd, buf, 1024);
			if (cnt > 0)
				ret = write(to_fd, buf, cnt);
			else if (cnt < 0)
				ret = cnt;
		} while (cnt > 0 && ret >= 0);
	if (ret >= 0)
		ret = fsync(to_fd);
	if (ret < 0)
		syslog(LOG_ERR, "stable restart copy failure: %m");
}

/*
 * Back up the stable restart file when indicated by the kernel.
 */
static void
backup_stable(__unused int signo)
{

	if (stablefd >= 0)
		copy_stable(stablefd, backupfd);
}

/*
 * Parse the pNFS string and extract the DS servers and ports numbers.
 */
static void
parse_dsserver(const char *optionarg, struct nfsd_nfsd_args *nfsdargp)
{
	char *cp, *cp2, *dsaddr, *dshost, *dspath, *dsvol, nfsprt[9];
	char *mdspath, *mdsp, ip6[INET6_ADDRSTRLEN];
	const char *ad;
	int ecode;
	u_int adsiz, dsaddrcnt, dshostcnt, dspathcnt, hostsiz, pathsiz;
	u_int mdspathcnt;
	size_t dsaddrsiz, dshostsiz, dspathsiz, nfsprtsiz, mdspathsiz;
	struct addrinfo hints, *ai_tcp, *res;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;

	cp = strdup(optionarg);
	if (cp == NULL)
		errx(1, "Out of memory");

	/* Now, do the host names. */
	dspathsiz = 1024;
	dspathcnt = 0;
	dspath = malloc(dspathsiz);
	if (dspath == NULL)
		errx(1, "Out of memory");
	dshostsiz = 1024;
	dshostcnt = 0;
	dshost = malloc(dshostsiz);
	if (dshost == NULL)
		errx(1, "Out of memory");
	dsaddrsiz = 1024;
	dsaddrcnt = 0;
	dsaddr = malloc(dsaddrsiz);
	if (dsaddr == NULL)
		errx(1, "Out of memory");
	mdspathsiz = 1024;
	mdspathcnt = 0;
	mdspath = malloc(mdspathsiz);
	if (mdspath == NULL)
		errx(1, "Out of memory");

	/* Put the NFS port# in "." form. */
	snprintf(nfsprt, 9, ".%d.%d", 2049 >> 8, 2049 & 0xff);
	nfsprtsiz = strlen(nfsprt);

	ai_tcp = NULL;
	/* Loop around for each DS server name. */
	do {
		cp2 = strchr(cp, ',');
		if (cp2 != NULL) {
			/* Not the last DS in the list. */
			*cp2++ = '\0';
			if (*cp2 == '\0')
				usage();
		}

		dsvol = strchr(cp, ':');
		if (dsvol == NULL || *(dsvol + 1) == '\0')
			usage();
		*dsvol++ = '\0';

		/* Optional path for MDS file system to be stored on DS. */
		mdsp = strchr(dsvol, '#');
		if (mdsp != NULL) {
			if (*(mdsp + 1) == '\0' || mdsp <= dsvol)
				usage();
			*mdsp++ = '\0';
		}

		/* Append this pathname to dspath. */
		pathsiz = strlen(dsvol);
		if (dspathcnt + pathsiz + 1 > dspathsiz) {
			dspathsiz *= 2;
			dspath = realloc(dspath, dspathsiz);
			if (dspath == NULL)
				errx(1, "Out of memory");
		}
		strcpy(&dspath[dspathcnt], dsvol);
		dspathcnt += pathsiz + 1;

		/* Append this pathname to mdspath. */
		if (mdsp != NULL)
			pathsiz = strlen(mdsp);
		else
			pathsiz = 0;
		if (mdspathcnt + pathsiz + 1 > mdspathsiz) {
			mdspathsiz *= 2;
			mdspath = realloc(mdspath, mdspathsiz);
			if (mdspath == NULL)
				errx(1, "Out of memory");
		}
		if (mdsp != NULL)
			strcpy(&mdspath[mdspathcnt], mdsp);
		else
			mdspath[mdspathcnt] = '\0';
		mdspathcnt += pathsiz + 1;

		if (ai_tcp != NULL)
			freeaddrinfo(ai_tcp);

		/* Get the fully qualified domain name and IP address. */
		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		ecode = getaddrinfo(cp, NULL, &hints, &ai_tcp);
		if (ecode != 0)
			err(1, "getaddrinfo pnfs: %s %s", cp,
			    gai_strerror(ecode));
		ad = NULL;
		for (res = ai_tcp; res != NULL; res = res->ai_next) {
			if (res->ai_addr->sa_family == AF_INET) {
				if (res->ai_addrlen < sizeof(sin))
					err(1, "getaddrinfo() returned "
					    "undersized IPv4 address");
				/*
				 * Mips cares about sockaddr_in alignment,
				 * so copy the address.
				 */
				memcpy(&sin, res->ai_addr, sizeof(sin));
				ad = inet_ntoa(sin.sin_addr);
				break;
			} else if (res->ai_family == AF_INET6) {
				if (res->ai_addrlen < sizeof(sin6))
					err(1, "getaddrinfo() returned "
					    "undersized IPv6 address");
				/*
				 * Mips cares about sockaddr_in6 alignment,
				 * so copy the address.
				 */
				memcpy(&sin6, res->ai_addr, sizeof(sin6));
				ad = inet_ntop(AF_INET6, &sin6.sin6_addr, ip6,
				    sizeof(ip6));

				/*
				 * XXX
				 * Since a link local address will only
				 * work if the client and DS are in the
				 * same scope zone, only use it if it is
				 * the only address.
				 */
				if (ad != NULL &&
				    !IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr))
					break;
			}
		}
		if (ad == NULL)
			err(1, "No IP address for %s", cp);

		/* Append this address to dsaddr. */
		adsiz = strlen(ad);
		if (dsaddrcnt + adsiz + nfsprtsiz + 1 > dsaddrsiz) {
			dsaddrsiz *= 2;
			dsaddr = realloc(dsaddr, dsaddrsiz);
			if (dsaddr == NULL)
				errx(1, "Out of memory");
		}
		strcpy(&dsaddr[dsaddrcnt], ad);
		strcat(&dsaddr[dsaddrcnt], nfsprt);
		dsaddrcnt += adsiz + nfsprtsiz + 1;

		/* Append this hostname to dshost. */
		hostsiz = strlen(ai_tcp->ai_canonname);
		if (dshostcnt + hostsiz + 1 > dshostsiz) {
			dshostsiz *= 2;
			dshost = realloc(dshost, dshostsiz);
			if (dshost == NULL)
				errx(1, "Out of memory");
		}
		strcpy(&dshost[dshostcnt], ai_tcp->ai_canonname);
		dshostcnt += hostsiz + 1;

		cp = cp2;
	} while (cp != NULL);

	nfsdargp->addr = dsaddr;
	nfsdargp->addrlen = dsaddrcnt;
	nfsdargp->dnshost = dshost;
	nfsdargp->dnshostlen = dshostcnt;
	nfsdargp->dspath = dspath;
	nfsdargp->dspathlen = dspathcnt;
	nfsdargp->mdspath = mdspath;
	nfsdargp->mdspathlen = mdspathcnt;
	freeaddrinfo(ai_tcp);
}

