/*-
 * Copyright (c) 2009 Rick Macklem, University of Guelph
 * All rights reserved.
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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/wait.h>

#include <nfs/nfssvc.h>

#include <rpc/rpc.h>

#include <fs/nfs/rpcv2.h>
#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfskpiport.h>
#include <fs/nfs/nfs.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/* Global defs */
#ifdef DEBUG
#define	syslog(e, s)	fprintf(stderr,(s))
static int	debug = 1;
#else
static int	debug = 0;
#endif

static pid_t	children;

static void	nonfs(int);
static void	reapchild(int);
static void	usage(void);
static void	cleanup(int);
static void	child_cleanup(int);
static void	nfscbd_exit(int);
static void	killchildren(void);

/*
 * Nfs callback server daemon.
 *
 * 1 - do file descriptor and signal cleanup
 * 2 - fork the nfscbd(s)
 * 4 - create callback server socket(s)
 * 5 - set up server socket for rpc
 *
 * For connectionless protocols, just pass the socket into the kernel via.
 * nfssvc().
 * For connection based sockets, loop doing accepts. When you get a new
 * socket from accept, pass the msgsock into the kernel via. nfssvc().
 */
int
main(int argc, char *argv[])
{
	struct nfscbd_args nfscbdargs;
	struct nfsd_nfscbd_args nfscbdargs2;
	struct sockaddr_in inetaddr, inetpeer;
	fd_set ready, sockbits;
	int ch, connect_type_cnt, len, maxsock, msgsock, error;
	int nfssvc_flag, on, sock, tcpsock, ret, mustfreeai = 0;
	char *cp, princname[128];
	char myname[MAXHOSTNAMELEN], *myfqdnname = NULL;
	struct addrinfo *aip, hints;
	pid_t pid;
	short myport = NFSV4_CBPORT;

	if (modfind("nfscl") < 0) {
		/* Not present in kernel, try loading it */
		if (kldload("nfscl") < 0 ||
		    modfind("nfscl") < 0)
			errx(1, "nfscl is not available");
	}
	/*
	 * First, get our fully qualified host name, if possible.
	 */
	if (gethostname(myname, MAXHOSTNAMELEN) >= 0) {
		cp = strchr(myname, '.');
		if (cp != NULL && *(cp + 1) != '\0') {
			cp = myname;
		} else {
			/*
			 * No domain on myname, so try looking it up.
			 */
			cp = NULL;
			memset((void *)&hints, 0, sizeof (hints));
			hints.ai_flags = AI_CANONNAME;
			error = getaddrinfo(myname, NULL, &hints, &aip);
			if (error == 0) {
			    if (aip->ai_canonname != NULL &&
				(cp = strchr(aip->ai_canonname, '.')) != NULL
				&& *(cp + 1) != '\0') {
				    cp = aip->ai_canonname;
				    mustfreeai = 1;
			    } else {
				    freeaddrinfo(aip);
			    }
			}
		}
		if (cp == NULL)
			warnx("Can't get fully qualified host name");
		myfqdnname = cp;
	}

	princname[0] = '\0';
#define	GETOPT	"p:P:"
#define	USAGE	"[ -p port_num ] [ -P client_principal ]"
	while ((ch = getopt(argc, argv, GETOPT)) != -1)
		switch (ch) {
		case 'p':
			myport = atoi(optarg);
			if (myport < 1) {
				warnx("port# non-positive, reset to %d",
				    NFSV4_CBPORT);
				myport = NFSV4_CBPORT;
			}
			break;
		case 'P':
			cp = optarg;
			if (cp != NULL && strlen(cp) > 0 &&
			    strlen(cp) < sizeof (princname)) {
				if (strchr(cp, '@') == NULL &&
				    myfqdnname != NULL)
					snprintf(princname, sizeof (princname),
					    "%s@%s", cp, myfqdnname);
				else
					strlcpy(princname, cp,
					    sizeof (princname));
			} else {
				warnx("client princ invalid. ignored\n");
			}
			break;
		default:
		case '?':
			usage();
		}
	argv += optind;
	argc -= optind;

	if (argc > 0)
		usage();

	if (mustfreeai)
		freeaddrinfo(aip);
	nfscbdargs2.principal = (const char *)princname;
	if (debug == 0) {
		daemon(0, 0);
		(void)signal(SIGTERM, SIG_IGN);
		(void)signal(SIGHUP, SIG_IGN);
		(void)signal(SIGINT, SIG_IGN);
		(void)signal(SIGQUIT, SIG_IGN);
	}
	(void)signal(SIGSYS, nonfs);
	(void)signal(SIGCHLD, reapchild);

	openlog("nfscbd:", LOG_PID, LOG_DAEMON);

	pid = fork();
	if (pid < 0) {
		syslog(LOG_ERR, "fork: %m");
		nfscbd_exit(1);
	} else if (pid > 0) {
		children = pid;
	} else {
		(void)signal(SIGUSR1, child_cleanup);
		setproctitle("server");
		nfssvc_flag = NFSSVC_NFSCBD;
		if (nfssvc(nfssvc_flag, &nfscbdargs2) < 0) {
			syslog(LOG_ERR, "nfssvc: %m");
			nfscbd_exit(1);
		}
		exit(0);
	}
	(void)signal(SIGUSR1, cleanup);

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "can't create udp socket");
		nfscbd_exit(1);
	}
	memset(&inetaddr, 0, sizeof inetaddr);
	inetaddr.sin_family = AF_INET;
	inetaddr.sin_addr.s_addr = INADDR_ANY;
	inetaddr.sin_port = htons(myport);
	inetaddr.sin_len = sizeof(inetaddr);
	ret = bind(sock, (struct sockaddr *)&inetaddr, sizeof(inetaddr));
	/* If bind() fails, this is a restart, so just skip UDP. */
	if (ret == 0) {
		len = sizeof(inetaddr);
		if (getsockname(sock, (struct sockaddr *)&inetaddr, &len) < 0){
			syslog(LOG_ERR, "can't get bound addr");
			nfscbd_exit(1);
		}
		nfscbdargs.port = ntohs(inetaddr.sin_port);
		if (nfscbdargs.port != myport) {
			syslog(LOG_ERR, "BAD PORT#");
			nfscbd_exit(1);
		}
		nfscbdargs.sock = sock;
		nfscbdargs.name = NULL;
		nfscbdargs.namelen = 0;
		if (nfssvc(NFSSVC_CBADDSOCK, &nfscbdargs) < 0) {
			syslog(LOG_ERR, "can't Add UDP socket");
			nfscbd_exit(1);
		}
	}
	(void)close(sock);

	/* Now set up the master server socket waiting for tcp connections. */
	on = 1;
	FD_ZERO(&sockbits);
	connect_type_cnt = 0;
	if ((tcpsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		syslog(LOG_ERR, "can't create tcp socket");
		nfscbd_exit(1);
	}
	if (setsockopt(tcpsock,
	    SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
		syslog(LOG_ERR, "setsockopt SO_REUSEADDR: %m");
	/* sin_port is already set */
	inetaddr.sin_family = AF_INET;
	inetaddr.sin_addr.s_addr = INADDR_ANY;
	inetaddr.sin_port = htons(myport);
	inetaddr.sin_len = sizeof(inetaddr);
	if (bind(tcpsock,
	    (struct sockaddr *)&inetaddr, sizeof (inetaddr)) < 0) {
		syslog(LOG_ERR, "can't bind tcp addr");
		nfscbd_exit(1);
	}
	if (listen(tcpsock, 5) < 0) {
		syslog(LOG_ERR, "listen failed");
		nfscbd_exit(1);
	}
	FD_SET(tcpsock, &sockbits);
	maxsock = tcpsock;
	connect_type_cnt++;

	setproctitle("master");

	/*
	 * Loop forever accepting connections and passing the sockets
	 * into the kernel for the mounts.
	 */
	for (;;) {
		ready = sockbits;
		if (connect_type_cnt > 1) {
			if (select(maxsock + 1,
			    &ready, NULL, NULL, NULL) < 1) {
				syslog(LOG_ERR, "select failed: %m");
				nfscbd_exit(1);
			}
		}
		if (FD_ISSET(tcpsock, &ready)) {
			len = sizeof(inetpeer);
			if ((msgsock = accept(tcpsock,
			    (struct sockaddr *)&inetpeer, &len)) < 0) {
				syslog(LOG_ERR, "accept failed: %m");
				nfscbd_exit(1);
			}
			memset(inetpeer.sin_zero, 0,
			    sizeof (inetpeer.sin_zero));
			if (setsockopt(msgsock, SOL_SOCKET,
			    SO_KEEPALIVE, (char *)&on, sizeof(on)) < 0)
				syslog(LOG_ERR,
				    "setsockopt SO_KEEPALIVE: %m");
			nfscbdargs.sock = msgsock;
			nfscbdargs.name = (caddr_t)&inetpeer;
			nfscbdargs.namelen = sizeof(inetpeer);
			nfssvc(NFSSVC_CBADDSOCK, &nfscbdargs);
			(void)close(msgsock);
		}
	}
}

static void
usage(void)
{

	errx(1, "usage: nfscbd %s", USAGE);
}

static void
nonfs(int signo __unused)
{
	syslog(LOG_ERR, "missing system call: NFS not available");
}

static void
reapchild(int signo __unused)
{
	pid_t pid;

	while ((pid = wait3(NULL, WNOHANG, NULL)) > 0) {
		if (pid == children)
			children = -1;
	}
}

static void
killchildren(void)
{

	if (children > 0)
		kill(children, SIGKILL);
}

/*
 * Cleanup master after SIGUSR1.
 */
static void
cleanup(int signo __unused)
{
	nfscbd_exit(0);
}

/*
 * Cleanup child after SIGUSR1.
 */
static void
child_cleanup(int signo __unused)
{
	exit(0);
}

static void
nfscbd_exit(int status __unused)
{
	killchildren();
	exit(status);
}
