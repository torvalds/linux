/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      from BSDI kern.c,v 1.2 1998/11/25 22:38:27 don Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <netdb.h>

#include "nlm_prot.h"
#include <nfs/nfsproto.h>
#include <nfs/nfs_lock.h>

#include "lockd.h"
#include "lockd_lock.h"
#include <nfsclient/nfs.h>

#define DAEMON_USERNAME	"daemon"

/* Lock request owner. */
typedef struct __owner {
	pid_t	 pid;				/* Process ID. */
	time_t	 tod;				/* Time-of-day. */
} OWNER;
static OWNER owner;

static char hostname[MAXHOSTNAMELEN + 1];	/* Hostname. */
static int devfd;

static void	client_cleanup(void);
static const char *from_addr(struct sockaddr *);
int	lock_request(LOCKD_MSG *);
static void	set_auth(CLIENT *cl, struct xucred *ucred);
void	show(LOCKD_MSG *);
int	test_request(LOCKD_MSG *);
int	unlock_request(LOCKD_MSG *);

static int
nfslockdans(int vers, struct lockd_ans *ansp)
{

	ansp->la_vers = vers;
	return (write(devfd, ansp, sizeof *ansp) <= 0);
}

/*
 * will break because fifo needs to be repopened when EOF'd
 */
#define lockd_seteuid(uid)	seteuid(uid)

#define d_calls (debug_level > 1)
#define d_args (debug_level > 2)

static const char *
from_addr(struct sockaddr *saddr)
{
	static char inet_buf[INET6_ADDRSTRLEN];

	if (getnameinfo(saddr, saddr->sa_len, inet_buf, sizeof(inet_buf),
			NULL, 0, NI_NUMERICHOST) == 0)
		return inet_buf;
	return "???";
}

void
client_cleanup(void)
{
	(void)lockd_seteuid(0);
	exit(-1);
}

/*
 * client_request --
 *	Loop around messages from the kernel, forwarding them off to
 *	NLM servers.
 */
pid_t
client_request(void)
{
	LOCKD_MSG msg;
	int nr, ret;
	pid_t child;
	uid_t daemon_uid;
	struct passwd *pw;

	/* Open the dev . */
	devfd = open(_PATH_DEV _PATH_NFSLCKDEV, O_RDWR | O_NONBLOCK);
	if (devfd < 0) {
		syslog(LOG_ERR, "open: %s: %m", _PATH_NFSLCKDEV);
		goto err;
	}

	signal(SIGPIPE, SIG_IGN);

	/*
	 * Create a separate process, the client code is really a separate
	 * daemon that shares a lot of code.
	 */
	switch (child = fork()) {
	case -1:
		err(1, "fork");
	case 0:
		setproctitle("client");
		break;
	default:
		setproctitle("server");
		return (child);
	}

	signal(SIGHUP, (sig_t)client_cleanup);
	signal(SIGTERM, (sig_t)client_cleanup);

	/* Setup. */
	(void)time(&owner.tod);
	owner.pid = getpid();
	(void)gethostname(hostname, sizeof(hostname) - 1);

	pw = getpwnam(DAEMON_USERNAME);
	if (pw == NULL) {
		syslog(LOG_ERR, "getpwnam: %s: %m", DAEMON_USERNAME);
		goto err;
	}
	daemon_uid = pw->pw_uid;
	/* drop our root privileges */
	(void)lockd_seteuid(daemon_uid);

	for (;;) {
		/* Read the fixed length message. */
		if ((nr = read(devfd, &msg, sizeof(msg))) == sizeof(msg)) {
			if (d_args)
				show(&msg);

			if (msg.lm_version != LOCKD_MSG_VERSION) {
				syslog(LOG_ERR,
				    "unknown msg type: %d", msg.lm_version);
			}
			/*
			 * Send it to the NLM server and don't grant the lock
			 * if we fail for any reason.
			 */
			switch (msg.lm_fl.l_type) {
			case F_RDLCK:
			case F_WRLCK:
				if (msg.lm_getlk)
					ret = test_request(&msg);
				else
					ret = lock_request(&msg);
				break;
			case F_UNLCK:
				ret = unlock_request(&msg);
				break;
			default:
				ret = 1;
				syslog(LOG_ERR,
				    "unknown lock type: %d", msg.lm_fl.l_type);
				break;
			}
			if (ret) {
				struct lockd_ans ans;

				ans.la_msg_ident = msg.lm_msg_ident;
				ans.la_errno = EHOSTUNREACH;

				if (nfslockdans(LOCKD_ANS_VERSION, &ans)) {
					syslog((errno == EPIPE ? LOG_INFO : 
						LOG_ERR), "process %lu: %m",
						(u_long)msg.lm_msg_ident.pid);
				}
			}
		} else if (nr == -1) {
			if (errno != EAGAIN) {
				syslog(LOG_ERR, "read: %s: %m", _PATH_NFSLCKDEV);
				goto err;
			}
		} else if (nr != 0) {
			syslog(LOG_ERR,
			    "%s: discard %d bytes", _PATH_NFSLCKDEV, nr);
		}
	}

	/* Reached only on error. */
err:
	(void)lockd_seteuid(0);
	_exit (1);
}

void
set_auth(CLIENT *cl, struct xucred *xucred)
{
	int ngroups;

	ngroups = xucred->cr_ngroups - 1;
	if (ngroups > NGRPS)
		ngroups = NGRPS;
        if (cl->cl_auth != NULL)
                cl->cl_auth->ah_ops->ah_destroy(cl->cl_auth);
        cl->cl_auth = authunix_create(hostname,
                        xucred->cr_uid,
                        xucred->cr_groups[0],
                        ngroups,
                        &xucred->cr_groups[1]);
}


/*
 * test_request --
 *	Convert a lock LOCKD_MSG into an NLM request, and send it off.
 */
int
test_request(LOCKD_MSG *msg)
{
	CLIENT *cli;
	struct timeval timeout = {0, 0};	/* No timeout, no response. */
	char dummy;

	if (d_calls)
		syslog(LOG_DEBUG, "test request: %s: %s to %s",
		    msg->lm_nfsv3 ? "V4" : "V1/3",
		    msg->lm_fl.l_type == F_WRLCK ? "write" : "read",
		    from_addr((struct sockaddr *)&msg->lm_addr));

	if (msg->lm_nfsv3) {
		struct nlm4_testargs arg4;

		arg4.cookie.n_bytes = (char *)&msg->lm_msg_ident;
		arg4.cookie.n_len = sizeof(msg->lm_msg_ident);
		arg4.exclusive = msg->lm_fl.l_type == F_WRLCK ? 1 : 0;
		arg4.alock.caller_name = hostname;
		arg4.alock.fh.n_bytes = (char *)&msg->lm_fh;
		arg4.alock.fh.n_len = msg->lm_fh_len;
		arg4.alock.oh.n_bytes = (char *)&owner;
		arg4.alock.oh.n_len = sizeof(owner);
		arg4.alock.svid = msg->lm_msg_ident.pid;
		arg4.alock.l_offset = msg->lm_fl.l_start;
		arg4.alock.l_len = msg->lm_fl.l_len;

		if ((cli = get_client(
		    (struct sockaddr *)&msg->lm_addr,
		    NLM_VERS4)) == NULL)
			return (1);

		set_auth(cli, &msg->lm_cred);
		(void)clnt_call(cli, NLM_TEST_MSG,
		    (xdrproc_t)xdr_nlm4_testargs, &arg4,
		    (xdrproc_t)xdr_void, &dummy, timeout);
	} else {
		struct nlm_testargs arg;

		arg.cookie.n_bytes = (char *)&msg->lm_msg_ident;
		arg.cookie.n_len = sizeof(msg->lm_msg_ident);
		arg.exclusive = msg->lm_fl.l_type == F_WRLCK ? 1 : 0;
		arg.alock.caller_name = hostname;
		arg.alock.fh.n_bytes = (char *)&msg->lm_fh;
		arg.alock.fh.n_len = msg->lm_fh_len;
		arg.alock.oh.n_bytes = (char *)&owner;
		arg.alock.oh.n_len = sizeof(owner);
		arg.alock.svid = msg->lm_msg_ident.pid;
		arg.alock.l_offset = msg->lm_fl.l_start;
		arg.alock.l_len = msg->lm_fl.l_len;

		if ((cli = get_client(
		    (struct sockaddr *)&msg->lm_addr,
		    NLM_VERS)) == NULL)
			return (1);

		set_auth(cli, &msg->lm_cred);
		(void)clnt_call(cli, NLM_TEST_MSG,
		    (xdrproc_t)xdr_nlm_testargs, &arg,
		    (xdrproc_t)xdr_void, &dummy, timeout);
	}
	return (0);
}

/*
 * lock_request --
 *	Convert a lock LOCKD_MSG into an NLM request, and send it off.
 */
int
lock_request(LOCKD_MSG *msg)
{
	CLIENT *cli;
	struct nlm4_lockargs arg4;
	struct nlm_lockargs arg;
	struct timeval timeout = {0, 0};	/* No timeout, no response. */
	char dummy;

	if (d_calls)
		syslog(LOG_DEBUG, "lock request: %s: %s to %s",
		    msg->lm_nfsv3 ? "V4" : "V1/3",
		    msg->lm_fl.l_type == F_WRLCK ? "write" : "read",
		    from_addr((struct sockaddr *)&msg->lm_addr));

	if (msg->lm_nfsv3) {
		arg4.cookie.n_bytes = (char *)&msg->lm_msg_ident;
		arg4.cookie.n_len = sizeof(msg->lm_msg_ident);
		arg4.block = msg->lm_wait ? 1 : 0;
		arg4.exclusive = msg->lm_fl.l_type == F_WRLCK ? 1 : 0;
		arg4.alock.caller_name = hostname;
		arg4.alock.fh.n_bytes = (char *)&msg->lm_fh;
		arg4.alock.fh.n_len = msg->lm_fh_len;
		arg4.alock.oh.n_bytes = (char *)&owner;
		arg4.alock.oh.n_len = sizeof(owner);
		arg4.alock.svid = msg->lm_msg_ident.pid;
		arg4.alock.l_offset = msg->lm_fl.l_start;
		arg4.alock.l_len = msg->lm_fl.l_len;
		arg4.reclaim = 0;
		arg4.state = nsm_state;

		if ((cli = get_client(
		    (struct sockaddr *)&msg->lm_addr,
		    NLM_VERS4)) == NULL)
			return (1);

		set_auth(cli, &msg->lm_cred);
		(void)clnt_call(cli, NLM_LOCK_MSG,
		    (xdrproc_t)xdr_nlm4_lockargs, &arg4,
		    (xdrproc_t)xdr_void, &dummy, timeout);
	} else {
		arg.cookie.n_bytes = (char *)&msg->lm_msg_ident;
		arg.cookie.n_len = sizeof(msg->lm_msg_ident);
		arg.block = msg->lm_wait ? 1 : 0;
		arg.exclusive = msg->lm_fl.l_type == F_WRLCK ? 1 : 0;
		arg.alock.caller_name = hostname;
		arg.alock.fh.n_bytes = (char *)&msg->lm_fh;
		arg.alock.fh.n_len = msg->lm_fh_len;
		arg.alock.oh.n_bytes = (char *)&owner;
		arg.alock.oh.n_len = sizeof(owner);
		arg.alock.svid = msg->lm_msg_ident.pid;
		arg.alock.l_offset = msg->lm_fl.l_start;
		arg.alock.l_len = msg->lm_fl.l_len;
		arg.reclaim = 0;
		arg.state = nsm_state;

		if ((cli = get_client(
		    (struct sockaddr *)&msg->lm_addr,
		    NLM_VERS)) == NULL)
			return (1);

		set_auth(cli, &msg->lm_cred);
		(void)clnt_call(cli, NLM_LOCK_MSG,
		    (xdrproc_t)xdr_nlm_lockargs, &arg,
		    (xdrproc_t)xdr_void, &dummy, timeout);
	}
	return (0);
}

/*
 * unlock_request --
 *	Convert an unlock LOCKD_MSG into an NLM request, and send it off.
 */
int
unlock_request(LOCKD_MSG *msg)
{
	CLIENT *cli;
	struct nlm4_unlockargs arg4;
	struct nlm_unlockargs arg;
	struct timeval timeout = {0, 0};	/* No timeout, no response. */
	char dummy;

	if (d_calls)
		syslog(LOG_DEBUG, "unlock request: %s: to %s",
		    msg->lm_nfsv3 ? "V4" : "V1/3",
		    from_addr((struct sockaddr *)&msg->lm_addr));

	if (msg->lm_nfsv3) {
		arg4.cookie.n_bytes = (char *)&msg->lm_msg_ident;
		arg4.cookie.n_len = sizeof(msg->lm_msg_ident);
		arg4.alock.caller_name = hostname;
		arg4.alock.fh.n_bytes = (char *)&msg->lm_fh;
		arg4.alock.fh.n_len = msg->lm_fh_len;
		arg4.alock.oh.n_bytes = (char *)&owner;
		arg4.alock.oh.n_len = sizeof(owner);
		arg4.alock.svid = msg->lm_msg_ident.pid;
		arg4.alock.l_offset = msg->lm_fl.l_start;
		arg4.alock.l_len = msg->lm_fl.l_len;

		if ((cli = get_client(
		    (struct sockaddr *)&msg->lm_addr,
		    NLM_VERS4)) == NULL)
			return (1);

		set_auth(cli, &msg->lm_cred);
		(void)clnt_call(cli, NLM_UNLOCK_MSG,
		    (xdrproc_t)xdr_nlm4_unlockargs, &arg4,
		    (xdrproc_t)xdr_void, &dummy, timeout);
	} else {
		arg.cookie.n_bytes = (char *)&msg->lm_msg_ident;
		arg.cookie.n_len = sizeof(msg->lm_msg_ident);
		arg.alock.caller_name = hostname;
		arg.alock.fh.n_bytes = (char *)&msg->lm_fh;
		arg.alock.fh.n_len = msg->lm_fh_len;
		arg.alock.oh.n_bytes = (char *)&owner;
		arg.alock.oh.n_len = sizeof(owner);
		arg.alock.svid = msg->lm_msg_ident.pid;
		arg.alock.l_offset = msg->lm_fl.l_start;
		arg.alock.l_len = msg->lm_fl.l_len;

		if ((cli = get_client(
		    (struct sockaddr *)&msg->lm_addr,
		    NLM_VERS)) == NULL)
			return (1);

		set_auth(cli, &msg->lm_cred);
		(void)clnt_call(cli, NLM_UNLOCK_MSG,
		    (xdrproc_t)xdr_nlm_unlockargs, &arg,
		    (xdrproc_t)xdr_void, &dummy, timeout);
	}

	return (0);
}

int
lock_answer(int pid, netobj *netcookie, int result, int *pid_p, int version)
{
	struct lockd_ans ans;

	if (netcookie->n_len != sizeof(ans.la_msg_ident)) {
		if (pid == -1) {	/* we're screwed */
			syslog(LOG_ERR, "inedible nlm cookie");
			return -1;
		}
		ans.la_msg_ident.pid = pid;
		ans.la_msg_ident.msg_seq = -1;
	} else {
		memcpy(&ans.la_msg_ident, netcookie->n_bytes,
		    sizeof(ans.la_msg_ident));
	}

	if (d_calls)
		syslog(LOG_DEBUG, "lock answer: pid %lu: %s %d",
		    (unsigned long)ans.la_msg_ident.pid,
		    version == NLM_VERS4 ? "nlmv4" : "nlmv3",
		    result);

	ans.la_set_getlk_pid = 0;
	if (version == NLM_VERS4)
		switch (result) {
		case nlm4_granted:
			ans.la_errno = 0;
			break;
		default:
			ans.la_errno = EACCES;
			break;
		case nlm4_denied:
			if (pid_p == NULL)
				ans.la_errno = EAGAIN;
			else {
				/* this is an answer to a nlm_test msg */
				ans.la_set_getlk_pid = 1;
				ans.la_getlk_pid = *pid_p;
				ans.la_errno = 0;
			}
			break;
		case nlm4_denied_nolocks:
			ans.la_errno = EAGAIN;
			break;
		case nlm4_blocked:
			return -1;
			/* NOTREACHED */
		case nlm4_denied_grace_period:
			ans.la_errno = EAGAIN;
			break;
		case nlm4_deadlck:
			ans.la_errno = EDEADLK;
			break;
		case nlm4_rofs:
			ans.la_errno = EROFS;
			break;
		case nlm4_stale_fh:
			ans.la_errno = ESTALE;
			break;
		case nlm4_fbig:
			ans.la_errno = EFBIG;
			break;
		case nlm4_failed:
			ans.la_errno = EACCES;
			break;
		}
	else
		switch (result) {
		case nlm_granted:
			ans.la_errno = 0;
			break;
		default:
			ans.la_errno = EACCES;
			break;
		case nlm_denied:
			if (pid_p == NULL)
				ans.la_errno = EAGAIN;
			else {
				/* this is an answer to a nlm_test msg */
				ans.la_set_getlk_pid = 1;
				ans.la_getlk_pid = *pid_p;
				ans.la_errno = 0;
			}
			break;
		case nlm_denied_nolocks:
			ans.la_errno = EAGAIN;
			break;
		case nlm_blocked:
			return -1;
			/* NOTREACHED */
		case nlm_denied_grace_period:
			ans.la_errno = EAGAIN;
			break;
		case nlm_deadlck:
			ans.la_errno = EDEADLK;
			break;
		}

	if (nfslockdans(LOCKD_ANS_VERSION, &ans)) {
		syslog(((errno == EPIPE || errno == ESRCH) ? 
			LOG_INFO : LOG_ERR), 
			"process %lu: %m", (u_long)ans.la_msg_ident.pid);
		return -1;
	}
	return 0;
}

/*
 * show --
 *	Display the contents of a kernel LOCKD_MSG structure.
 */
void
show(LOCKD_MSG *mp)
{
	static char hex[] = "0123456789abcdef";
	struct fid *fidp;
	fsid_t *fsidp;
	size_t len;
	u_int8_t *p, *t, buf[NFS_SMALLFH*3+1];

	syslog(LOG_DEBUG, "process ID: %lu\n", (long)mp->lm_msg_ident.pid);

	fsidp = (fsid_t *)&mp->lm_fh;
	fidp = (struct fid *)((u_int8_t *)&mp->lm_fh + sizeof(fsid_t));

	for (t = buf, p = (u_int8_t *)mp->lm_fh,
	    len = mp->lm_fh_len;
	    len > 0; ++p, --len) {
		*t++ = '\\';
		*t++ = hex[(*p & 0xf0) >> 4];
		*t++ = hex[*p & 0x0f];
	}
	*t = '\0';

	syslog(LOG_DEBUG, "fh_len %d, fh %s\n", (int)mp->lm_fh_len, buf);

	/* Show flock structure. */
	syslog(LOG_DEBUG, "start %llu; len %llu; pid %lu; type %d; whence %d\n",
	    (unsigned long long)mp->lm_fl.l_start,
	    (unsigned long long)mp->lm_fl.l_len, (u_long)mp->lm_fl.l_pid,
	    mp->lm_fl.l_type, mp->lm_fl.l_whence);

	/* Show wait flag. */
	syslog(LOG_DEBUG, "wait was %s\n", mp->lm_wait ? "set" : "not set");
}
