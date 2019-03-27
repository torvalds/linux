/*-
 * Copyright (c) 2005 Andrey Simonenko
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/socket.h>
#include <sys/un.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "uc_common.h"

#ifndef LISTENQ
# define LISTENQ	1
#endif

#ifndef	TIMEOUT
# define TIMEOUT	2
#endif

#define	SYNC_SERVER	0
#define	SYNC_CLIENT	1
#define	SYNC_RECV	0
#define	SYNC_SEND	1

#define	LOGMSG_SIZE	128

void
uc_output(const char *format, ...)
{
	char buf[LOGMSG_SIZE];
	va_list ap;

	va_start(ap, format);
	if (vsnprintf(buf, sizeof(buf), format, ap) < 0)
		err(EXIT_FAILURE, "output: vsnprintf failed");
	write(STDOUT_FILENO, buf, strlen(buf));
	va_end(ap);
}

void
uc_logmsg(const char *format, ...)
{
	char buf[LOGMSG_SIZE];
	va_list ap;
	int errno_save;

	errno_save = errno;
	va_start(ap, format);
	if (vsnprintf(buf, sizeof(buf), format, ap) < 0)
		err(EXIT_FAILURE, "logmsg: vsnprintf failed");
	if (errno_save == 0)
		uc_output("%s: %s\n", uc_cfg.proc_name, buf);
	else
		uc_output("%s: %s: %s\n", uc_cfg.proc_name, buf,
		    strerror(errno_save));
	va_end(ap);
	errno = errno_save;
}

void
uc_vlogmsgx(const char *format, va_list ap)
{
	char buf[LOGMSG_SIZE];

	if (vsnprintf(buf, sizeof(buf), format, ap) < 0)
		err(EXIT_FAILURE, "uc_logmsgx: vsnprintf failed");
	uc_output("%s: %s\n", uc_cfg.proc_name, buf);
}

void
uc_logmsgx(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	uc_vlogmsgx(format, ap);
	va_end(ap);
}

void
uc_dbgmsg(const char *format, ...)
{
	va_list ap;

	if (uc_cfg.debug) {
		va_start(ap, format);
		uc_vlogmsgx(format, ap);
		va_end(ap);
	}
}

int
uc_socket_create(void)
{
	struct timeval tv;
	int fd;

	fd = socket(PF_LOCAL, uc_cfg.sock_type, 0);
	if (fd < 0) {
		uc_logmsg("socket_create: socket(PF_LOCAL, %s, 0)", uc_cfg.sock_type_str);
		return (-1);
	}
	if (uc_cfg.server_flag)
		uc_cfg.serv_sock_fd = fd;

	tv.tv_sec = TIMEOUT;
	tv.tv_usec = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 ||
	    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
		uc_logmsg("socket_create: setsockopt(SO_RCVTIMEO/SO_SNDTIMEO)");
		goto failed;
	}

	if (uc_cfg.server_flag) {
		if (bind(fd, (struct sockaddr *)&uc_cfg.serv_addr_sun,
		    uc_cfg.serv_addr_sun.sun_len) < 0) {
			uc_logmsg("socket_create: bind(%s)",
			    uc_cfg.serv_addr_sun.sun_path);
			goto failed;
		}
		if (uc_cfg.sock_type == SOCK_STREAM) {
			int val;

			if (listen(fd, LISTENQ) < 0) {
				uc_logmsg("socket_create: listen");
				goto failed;
			}
			val = fcntl(fd, F_GETFL, 0);
			if (val < 0) {
				uc_logmsg("socket_create: fcntl(F_GETFL)");
				goto failed;
			}
			if (fcntl(fd, F_SETFL, val | O_NONBLOCK) < 0) {
				uc_logmsg("socket_create: fcntl(F_SETFL)");
				goto failed;
			}
		}
	}

	return (fd);

failed:
	if (close(fd) < 0)
		uc_logmsg("socket_create: close");
	if (uc_cfg.server_flag)
		if (unlink(uc_cfg.serv_addr_sun.sun_path) < 0)
			uc_logmsg("socket_close: unlink(%s)",
			    uc_cfg.serv_addr_sun.sun_path);
	return (-1);
}

int
uc_socket_close(int fd)
{
	int rv;

	rv = 0;
	if (close(fd) < 0) {
		uc_logmsg("socket_close: close");
		rv = -1;
	}
	if (uc_cfg.server_flag && fd == uc_cfg.serv_sock_fd)
		if (unlink(uc_cfg.serv_addr_sun.sun_path) < 0) {
			uc_logmsg("socket_close: unlink(%s)",
			    uc_cfg.serv_addr_sun.sun_path);
			rv = -1;
		}
	return (rv);
}

int
uc_socket_connect(int fd)
{
	uc_dbgmsg("connect");

	if (connect(fd, (struct sockaddr *)&uc_cfg.serv_addr_sun,
	    uc_cfg.serv_addr_sun.sun_len) < 0) {
		uc_logmsg("socket_connect: connect(%s)", uc_cfg.serv_addr_sun.sun_path);
		return (-1);
	}
	return (0);
}

int
uc_sync_recv(void)
{
	ssize_t ssize;
	int fd;
	char buf;

	uc_dbgmsg("sync: wait");

	fd = uc_cfg.sync_fd[uc_cfg.server_flag ? SYNC_SERVER : SYNC_CLIENT][SYNC_RECV];

	ssize = read(fd, &buf, 1);
	if (ssize < 0) {
		uc_logmsg("sync_recv: read");
		return (-1);
	}
	if (ssize < 1) {
		uc_logmsgx("sync_recv: read %zd of 1 byte", ssize);
		return (-1);
	}

	uc_dbgmsg("sync: received");

	return (0);
}

int
uc_sync_send(void)
{
	ssize_t ssize;
	int fd;

	uc_dbgmsg("sync: send");

	fd = uc_cfg.sync_fd[uc_cfg.server_flag ? SYNC_CLIENT : SYNC_SERVER][SYNC_SEND];

	ssize = write(fd, "", 1);
	if (ssize < 0) {
		uc_logmsg("uc_sync_send: write");
		return (-1);
	}
	if (ssize < 1) {
		uc_logmsgx("uc_sync_send: sent %zd of 1 byte", ssize);
		return (-1);
	}

	return (0);
}

int
uc_message_send(int fd, const struct msghdr *msghdr)
{
	const struct cmsghdr *cmsghdr;
	size_t size;
	ssize_t ssize;

	size = msghdr->msg_iov != 0 ? msghdr->msg_iov->iov_len : 0;
	uc_dbgmsg("send: data size %zu", size);
	uc_dbgmsg("send: msghdr.msg_controllen %u",
	    (u_int)msghdr->msg_controllen);
	cmsghdr = CMSG_FIRSTHDR(msghdr);
	if (cmsghdr != NULL)
		uc_dbgmsg("send: cmsghdr.cmsg_len %u",
		    (u_int)cmsghdr->cmsg_len);

	ssize = sendmsg(fd, msghdr, 0);
	if (ssize < 0) {
		uc_logmsg("message_send: sendmsg");
		return (-1);
	}
	if ((size_t)ssize != size) {
		uc_logmsgx("message_send: sendmsg: sent %zd of %zu bytes",
		    ssize, size);
		return (-1);
	}

	if (!uc_cfg.send_data_flag)
		if (uc_sync_send() < 0)
			return (-1);

	return (0);
}

int
uc_message_sendn(int fd, struct msghdr *msghdr)
{
	u_int i;

	for (i = 1; i <= uc_cfg.ipc_msg.msg_num; ++i) {
		uc_dbgmsg("message #%u", i);
		if (uc_message_send(fd, msghdr) < 0)
			return (-1);
	}
	return (0);
}

int
uc_message_recv(int fd, struct msghdr *msghdr)
{
	const struct cmsghdr *cmsghdr;
	size_t size;
	ssize_t ssize;

	if (!uc_cfg.send_data_flag)
		if (uc_sync_recv() < 0)
			return (-1);

	size = msghdr->msg_iov != NULL ? msghdr->msg_iov->iov_len : 0;
	ssize = recvmsg(fd, msghdr, MSG_WAITALL);
	if (ssize < 0) {
		uc_logmsg("message_recv: recvmsg");
		return (-1);
	}
	if ((size_t)ssize != size) {
		uc_logmsgx("message_recv: recvmsg: received %zd of %zu bytes",
		    ssize, size);
		return (-1);
	}

	uc_dbgmsg("recv: data size %zd", ssize);
	uc_dbgmsg("recv: msghdr.msg_controllen %u",
	    (u_int)msghdr->msg_controllen);
	cmsghdr = CMSG_FIRSTHDR(msghdr);
	if (cmsghdr != NULL)
		uc_dbgmsg("recv: cmsghdr.cmsg_len %u",
		    (u_int)cmsghdr->cmsg_len);

	if (memcmp(uc_cfg.ipc_msg.buf_recv, uc_cfg.ipc_msg.buf_send, size) != 0) {
		uc_logmsgx("message_recv: received message has wrong content");
		return (-1);
	}

	return (0);
}

int
uc_socket_accept(int listenfd)
{
	fd_set rset;
	struct timeval tv;
	int fd, rv, val;

	uc_dbgmsg("accept");

	FD_ZERO(&rset);
	FD_SET(listenfd, &rset);
	tv.tv_sec = TIMEOUT;
	tv.tv_usec = 0;
	rv = select(listenfd + 1, &rset, (fd_set *)NULL, (fd_set *)NULL, &tv);
	if (rv < 0) {
		uc_logmsg("socket_accept: select");
		return (-1);
	}
	if (rv == 0) {
		uc_logmsgx("socket_accept: select timeout");
		return (-1);
	}

	fd = accept(listenfd, (struct sockaddr *)NULL, (socklen_t *)NULL);
	if (fd < 0) {
		uc_logmsg("socket_accept: accept");
		return (-1);
	}

	val = fcntl(fd, F_GETFL, 0);
	if (val < 0) {
		uc_logmsg("socket_accept: fcntl(F_GETFL)");
		goto failed;
	}
	if (fcntl(fd, F_SETFL, val & ~O_NONBLOCK) < 0) {
		uc_logmsg("socket_accept: fcntl(F_SETFL)");
		goto failed;
	}

	return (fd);

failed:
	if (close(fd) < 0)
		uc_logmsg("socket_accept: close");
	return (-1);
}

int
uc_check_msghdr(const struct msghdr *msghdr, size_t size)
{
	if (msghdr->msg_flags & MSG_TRUNC) {
		uc_logmsgx("msghdr.msg_flags has MSG_TRUNC");
		return (-1);
	}
	if (msghdr->msg_flags & MSG_CTRUNC) {
		uc_logmsgx("msghdr.msg_flags has MSG_CTRUNC");
		return (-1);
	}
	if (msghdr->msg_controllen < size) {
		uc_logmsgx("msghdr.msg_controllen %u < %zu",
		    (u_int)msghdr->msg_controllen, size);
		return (-1);
	}
	if (msghdr->msg_controllen > 0 && size == 0) {
		uc_logmsgx("msghdr.msg_controllen %u > 0",
		    (u_int)msghdr->msg_controllen);
		return (-1);
	}
	return (0);
}

int
uc_check_cmsghdr(const struct cmsghdr *cmsghdr, int type, size_t size)
{
	if (cmsghdr == NULL) {
		uc_logmsgx("cmsghdr is NULL");
		return (-1);
	}
	if (cmsghdr->cmsg_level != SOL_SOCKET) {
		uc_logmsgx("cmsghdr.cmsg_level %d != SOL_SOCKET",
		    cmsghdr->cmsg_level);
		return (-1);
	}
	if (cmsghdr->cmsg_type != type) {
		uc_logmsgx("cmsghdr.cmsg_type %d != %d",
		    cmsghdr->cmsg_type, type);
		return (-1);
	}
	if (cmsghdr->cmsg_len != CMSG_LEN(size)) {
		uc_logmsgx("cmsghdr.cmsg_len %u != %zu",
		    (u_int)cmsghdr->cmsg_len, CMSG_LEN(size));
		return (-1);
	}
	return (0);
}

static void
uc_msghdr_init_generic(struct msghdr *msghdr, struct iovec *iov, void *cmsg_data)
{
	msghdr->msg_name = NULL;
	msghdr->msg_namelen = 0;
	if (uc_cfg.send_data_flag) {
		iov->iov_base = uc_cfg.server_flag ?
		    uc_cfg.ipc_msg.buf_recv : uc_cfg.ipc_msg.buf_send;
		iov->iov_len = uc_cfg.ipc_msg.buf_size;
		msghdr->msg_iov = iov;
		msghdr->msg_iovlen = 1;
	} else {
		msghdr->msg_iov = NULL;
		msghdr->msg_iovlen = 0;
	}
	msghdr->msg_control = cmsg_data;
	msghdr->msg_flags = 0;
}

void
uc_msghdr_init_server(struct msghdr *msghdr, struct iovec *iov,
    void *cmsg_data, size_t cmsg_size)
{
	uc_msghdr_init_generic(msghdr, iov, cmsg_data);
	msghdr->msg_controllen = cmsg_size;
	uc_dbgmsg("init: data size %zu", msghdr->msg_iov != NULL ?
	    msghdr->msg_iov->iov_len : (size_t)0);
	uc_dbgmsg("init: msghdr.msg_controllen %u",
	    (u_int)msghdr->msg_controllen);
}

void
uc_msghdr_init_client(struct msghdr *msghdr, struct iovec *iov,
    void *cmsg_data, size_t cmsg_size, int type, size_t arr_size)
{
	struct cmsghdr *cmsghdr;

	uc_msghdr_init_generic(msghdr, iov, cmsg_data);
	if (cmsg_data != NULL) {
		if (uc_cfg.send_array_flag)
			uc_dbgmsg("sending an array");
		else
			uc_dbgmsg("sending a scalar");
		msghdr->msg_controllen = uc_cfg.send_array_flag ?
		    cmsg_size : CMSG_SPACE(0);
		cmsghdr = CMSG_FIRSTHDR(msghdr);
		cmsghdr->cmsg_level = SOL_SOCKET;
		cmsghdr->cmsg_type = type;
		cmsghdr->cmsg_len = CMSG_LEN(uc_cfg.send_array_flag ? arr_size : 0);
	} else
		msghdr->msg_controllen = 0;
}

int
uc_client_fork(void)
{
	int fd1, fd2;

	if (pipe(uc_cfg.sync_fd[SYNC_SERVER]) < 0 ||
	    pipe(uc_cfg.sync_fd[SYNC_CLIENT]) < 0) {
		uc_logmsg("client_fork: pipe");
		return (-1);
	}
	uc_cfg.client_pid = fork();
	if (uc_cfg.client_pid == (pid_t)-1) {
		uc_logmsg("client_fork: fork");
		return (-1);
	}
	if (uc_cfg.client_pid == 0) {
		uc_cfg.proc_name = "CLIENT";
		uc_cfg.server_flag = false;
		fd1 = uc_cfg.sync_fd[SYNC_SERVER][SYNC_RECV];
		fd2 = uc_cfg.sync_fd[SYNC_CLIENT][SYNC_SEND];
	} else {
		fd1 = uc_cfg.sync_fd[SYNC_SERVER][SYNC_SEND];
		fd2 = uc_cfg.sync_fd[SYNC_CLIENT][SYNC_RECV];
	}
	if (close(fd1) < 0 || close(fd2) < 0) {
		uc_logmsg("client_fork: close");
		return (-1);
	}
	return (uc_cfg.client_pid != 0);
}

void
uc_client_exit(int rv)
{
	if (close(uc_cfg.sync_fd[SYNC_SERVER][SYNC_SEND]) < 0 ||
	    close(uc_cfg.sync_fd[SYNC_CLIENT][SYNC_RECV]) < 0) {
		uc_logmsg("client_exit: close");
		rv = -1;
	}
	rv = rv == 0 ? EXIT_SUCCESS : -rv;
	uc_dbgmsg("exit: code %d", rv);
	_exit(rv);
}

int
uc_client_wait(void)
{
	int status;
	pid_t pid;

	uc_dbgmsg("waiting for client");

	if (close(uc_cfg.sync_fd[SYNC_SERVER][SYNC_RECV]) < 0 ||
	    close(uc_cfg.sync_fd[SYNC_CLIENT][SYNC_SEND]) < 0) {
		uc_logmsg("client_wait: close");
		return (-1);
	}

	pid = waitpid(uc_cfg.client_pid, &status, 0);
	if (pid == (pid_t)-1) {
		uc_logmsg("client_wait: waitpid");
		return (-1);
	}

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != EXIT_SUCCESS) {
			uc_logmsgx("client exit status is %d",
			    WEXITSTATUS(status));
			return (-WEXITSTATUS(status));
		}
	} else {
		if (WIFSIGNALED(status))
			uc_logmsgx("abnormal termination of client, signal %d%s",
			    WTERMSIG(status), WCOREDUMP(status) ?
			    " (core file generated)" : "");
		else
			uc_logmsgx("termination of client, unknown status");
		return (-1);
	}

	return (0);
}

int
uc_check_groups(const char *gid_arr_str, const gid_t *gid_arr,
    const char *gid_num_str, int gid_num, bool all_gids)
{
	int i;

	for (i = 0; i < gid_num; ++i)
		uc_dbgmsg("%s[%d] %lu", gid_arr_str, i, (u_long)gid_arr[i]);

	if (all_gids) {
		if (gid_num != uc_cfg.proc_cred.gid_num) {
			uc_logmsgx("%s %d != %d", gid_num_str, gid_num,
			    uc_cfg.proc_cred.gid_num);
			return (-1);
		}
	} else {
		if (gid_num > uc_cfg.proc_cred.gid_num) {
			uc_logmsgx("%s %d > %d", gid_num_str, gid_num,
			    uc_cfg.proc_cred.gid_num);
			return (-1);
		}
	}
	if (memcmp(gid_arr, uc_cfg.proc_cred.gid_arr,
	    gid_num * sizeof(*gid_arr)) != 0) {
		uc_logmsgx("%s content is wrong", gid_arr_str);
		for (i = 0; i < gid_num; ++i)
			if (gid_arr[i] != uc_cfg.proc_cred.gid_arr[i]) {
				uc_logmsgx("%s[%d] %lu != %lu",
				    gid_arr_str, i, (u_long)gid_arr[i],
				    (u_long)uc_cfg.proc_cred.gid_arr[i]);
				break;
			}
		return (-1);
	}
	return (0);
}

int
uc_check_scm_creds_cmsgcred(struct cmsghdr *cmsghdr)
{
	const struct cmsgcred *cmcred;
	int rc;

	if (uc_check_cmsghdr(cmsghdr, SCM_CREDS, sizeof(struct cmsgcred)) < 0)
		return (-1);

	cmcred = (struct cmsgcred *)CMSG_DATA(cmsghdr);

	uc_dbgmsg("cmsgcred.cmcred_pid %ld", (long)cmcred->cmcred_pid);
	uc_dbgmsg("cmsgcred.cmcred_uid %lu", (u_long)cmcred->cmcred_uid);
	uc_dbgmsg("cmsgcred.cmcred_euid %lu", (u_long)cmcred->cmcred_euid);
	uc_dbgmsg("cmsgcred.cmcred_gid %lu", (u_long)cmcred->cmcred_gid);
	uc_dbgmsg("cmsgcred.cmcred_ngroups %d", cmcred->cmcred_ngroups);

	rc = 0;

	if (cmcred->cmcred_pid != uc_cfg.client_pid) {
		uc_logmsgx("cmsgcred.cmcred_pid %ld != %ld",
		    (long)cmcred->cmcred_pid, (long)uc_cfg.client_pid);
		rc = -1;
	}
	if (cmcred->cmcred_uid != uc_cfg.proc_cred.uid) {
		uc_logmsgx("cmsgcred.cmcred_uid %lu != %lu",
		    (u_long)cmcred->cmcred_uid, (u_long)uc_cfg.proc_cred.uid);
		rc = -1;
	}
	if (cmcred->cmcred_euid != uc_cfg.proc_cred.euid) {
		uc_logmsgx("cmsgcred.cmcred_euid %lu != %lu",
		    (u_long)cmcred->cmcred_euid, (u_long)uc_cfg.proc_cred.euid);
		rc = -1;
	}
	if (cmcred->cmcred_gid != uc_cfg.proc_cred.gid) {
		uc_logmsgx("cmsgcred.cmcred_gid %lu != %lu",
		    (u_long)cmcred->cmcred_gid, (u_long)uc_cfg.proc_cred.gid);
		rc = -1;
	}
	if (cmcred->cmcred_ngroups == 0) {
		uc_logmsgx("cmsgcred.cmcred_ngroups == 0");
		rc = -1;
	}
	if (cmcred->cmcred_ngroups < 0) {
		uc_logmsgx("cmsgcred.cmcred_ngroups %d < 0",
		    cmcred->cmcred_ngroups);
		rc = -1;
	}
	if (cmcred->cmcred_ngroups > CMGROUP_MAX) {
		uc_logmsgx("cmsgcred.cmcred_ngroups %d > %d",
		    cmcred->cmcred_ngroups, CMGROUP_MAX);
		rc = -1;
	}
	if (cmcred->cmcred_groups[0] != uc_cfg.proc_cred.egid) {
		uc_logmsgx("cmsgcred.cmcred_groups[0] %lu != %lu (EGID)",
		    (u_long)cmcred->cmcred_groups[0], (u_long)uc_cfg.proc_cred.egid);
		rc = -1;
	}
	if (uc_check_groups("cmsgcred.cmcred_groups", cmcred->cmcred_groups,
	    "cmsgcred.cmcred_ngroups", cmcred->cmcred_ngroups, false) < 0)
		rc = -1;
	return (rc);
}

int
uc_check_scm_creds_sockcred(struct cmsghdr *cmsghdr)
{
	const struct sockcred *sc;
	int rc;

	if (uc_check_cmsghdr(cmsghdr, SCM_CREDS,
	    SOCKCREDSIZE(uc_cfg.proc_cred.gid_num)) < 0)
		return (-1);

	sc = (struct sockcred *)CMSG_DATA(cmsghdr);

	rc = 0;

	uc_dbgmsg("sockcred.sc_uid %lu", (u_long)sc->sc_uid);
	uc_dbgmsg("sockcred.sc_euid %lu", (u_long)sc->sc_euid);
	uc_dbgmsg("sockcred.sc_gid %lu", (u_long)sc->sc_gid);
	uc_dbgmsg("sockcred.sc_egid %lu", (u_long)sc->sc_egid);
	uc_dbgmsg("sockcred.sc_ngroups %d", sc->sc_ngroups);

	if (sc->sc_uid != uc_cfg.proc_cred.uid) {
		uc_logmsgx("sockcred.sc_uid %lu != %lu",
		    (u_long)sc->sc_uid, (u_long)uc_cfg.proc_cred.uid);
		rc = -1;
	}
	if (sc->sc_euid != uc_cfg.proc_cred.euid) {
		uc_logmsgx("sockcred.sc_euid %lu != %lu",
		    (u_long)sc->sc_euid, (u_long)uc_cfg.proc_cred.euid);
		rc = -1;
	}
	if (sc->sc_gid != uc_cfg.proc_cred.gid) {
		uc_logmsgx("sockcred.sc_gid %lu != %lu",
		    (u_long)sc->sc_gid, (u_long)uc_cfg.proc_cred.gid);
		rc = -1;
	}
	if (sc->sc_egid != uc_cfg.proc_cred.egid) {
		uc_logmsgx("sockcred.sc_egid %lu != %lu",
		    (u_long)sc->sc_egid, (u_long)uc_cfg.proc_cred.egid);
		rc = -1;
	}
	if (sc->sc_ngroups == 0) {
		uc_logmsgx("sockcred.sc_ngroups == 0");
		rc = -1;
	}
	if (sc->sc_ngroups < 0) {
		uc_logmsgx("sockcred.sc_ngroups %d < 0",
		    sc->sc_ngroups);
		rc = -1;
	}
	if (sc->sc_ngroups != uc_cfg.proc_cred.gid_num) {
		uc_logmsgx("sockcred.sc_ngroups %d != %u",
		    sc->sc_ngroups, uc_cfg.proc_cred.gid_num);
		rc = -1;
	}
	if (uc_check_groups("sockcred.sc_groups", sc->sc_groups,
	    "sockcred.sc_ngroups", sc->sc_ngroups, true) < 0)
		rc = -1;
	return (rc);
}
