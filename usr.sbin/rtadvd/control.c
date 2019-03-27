/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "rtadvd.h"
#include "if.h"
#include "pathnames.h"
#include "control.h"

#define	CM_RECV_TIMEOUT	30

int
cm_recv(int fd, char *buf)
{
	ssize_t n;
	struct ctrl_msg_hdr	*cm;
	char *msg;
	struct pollfd pfds[1];
	int i;

	syslog(LOG_DEBUG, "<%s> enter, fd=%d", __func__, fd);

	memset(buf, 0, CM_MSG_MAXLEN);
	cm = (struct ctrl_msg_hdr *)buf;
	msg = (char *)buf + sizeof(*cm);

	pfds[0].fd = fd;
	pfds[0].events = POLLIN;

	for (;;) {
		i = poll(pfds, sizeof(pfds)/sizeof(pfds[0]),
		    CM_RECV_TIMEOUT);

		if (i == 0)
			continue;

		if (i < 0) {
			syslog(LOG_ERR, "<%s> poll error: %s",
			    __func__, strerror(errno));
			continue;
		}

		if (pfds[0].revents & POLLIN) {
			n = read(fd, cm, sizeof(*cm));
			if (n < 0 && errno == EAGAIN) {
				syslog(LOG_DEBUG,
				    "<%s> waiting...", __func__);
				continue;
			}
			break;
		}
	}

	if (n != (ssize_t)sizeof(*cm)) {
		syslog(LOG_WARNING,
		    "<%s> received a too small message.", __func__);
		goto cm_recv_err;
	}
	if (cm->cm_len > CM_MSG_MAXLEN) {
		syslog(LOG_WARNING,
		    "<%s> received a too large message.", __func__);
		goto cm_recv_err;
	}
	if (cm->cm_version != CM_VERSION) {
		syslog(LOG_WARNING,
		    "<%s> version mismatch", __func__);
		goto cm_recv_err;
	}
	if (cm->cm_type >= CM_TYPE_MAX) {
		syslog(LOG_WARNING,
		    "<%s> invalid msg type.", __func__);
		goto cm_recv_err;
	}

	syslog(LOG_DEBUG,
	    "<%s> ctrl msg received: type=%d", __func__,
	    cm->cm_type);

	if (cm->cm_len > sizeof(*cm)) {
		size_t msglen = cm->cm_len - sizeof(*cm);

		syslog(LOG_DEBUG,
		    "<%s> ctrl msg has payload (len=%zu)", __func__,
		    msglen);

		for (;;) {
			i = poll(pfds, sizeof(pfds)/sizeof(pfds[0]),
			    CM_RECV_TIMEOUT);

			if (i == 0)
				continue;

			if (i < 0) {
				syslog(LOG_ERR, "<%s> poll error: %s",
				    __func__, strerror(errno));
				continue;
			}

			if (pfds[0].revents & POLLIN) {
				n = read(fd, msg, msglen);
				if (n < 0 && errno == EAGAIN) {
					syslog(LOG_DEBUG,
					    "<%s> waiting...", __func__);
					continue;
				}
			}
			break;
		}
		if (n != (ssize_t)msglen) {
			syslog(LOG_WARNING,
			    "<%s> payload size mismatch.", __func__);
			goto cm_recv_err;
		}
		buf[CM_MSG_MAXLEN - 1] = '\0';
	}

	return (0);

cm_recv_err:
	close(fd);
	return (-1);
}

int
cm_send(int fd, char *buf)
{
	struct iovec iov[2];
	int iovcnt;
	ssize_t len;
	ssize_t iov_len_total;
	struct ctrl_msg_hdr *cm;
	char *msg;

	cm = (struct ctrl_msg_hdr *)buf;
	msg = (char *)buf + sizeof(*cm);

	iovcnt = 1;
	iov[0].iov_base = cm;
	iov[0].iov_len = sizeof(*cm);
	iov_len_total = iov[0].iov_len;
	if (cm->cm_len > sizeof(*cm)) {
		iovcnt++;
		iov[1].iov_base = msg;
		iov[1].iov_len = cm->cm_len - iov[0].iov_len;
		iov_len_total += iov[1].iov_len;
	}

	syslog(LOG_DEBUG,
	    "<%s> ctrl msg send: type=%d, count=%d, total_len=%zd", __func__,
	    cm->cm_type, iovcnt, iov_len_total);

	len = writev(fd, iov, iovcnt);
	syslog(LOG_DEBUG,
	    "<%s> ctrl msg send: length=%zd", __func__, len);

	if (len == -1) {
		syslog(LOG_DEBUG,
		    "<%s> write failed: (%d)%s", __func__, errno,
		    strerror(errno));
		close(fd);
		return (-1);
	}

	syslog(LOG_DEBUG,
	    "<%s> write length = %zd (actual)", __func__, len);
	syslog(LOG_DEBUG,
	    "<%s> write length = %zd (expected)", __func__, iov_len_total);

	if (len != iov_len_total) {
		close(fd);
		return (-1);
	}

	return (0);
}

int
csock_accept(struct sockinfo *s)
{
	struct sockaddr_un	sun;
	int	flags;
	int	fd;

	sun.sun_len = sizeof(sun);
	if ((fd = accept(s->si_fd, (struct sockaddr *)&sun,
		    (socklen_t *)&sun.sun_len)) == -1) {
		if (errno != EWOULDBLOCK && errno != EINTR)
			syslog(LOG_WARNING, "<%s> accept ", __func__);
		syslog(LOG_WARNING, "<%s> Xaccept: %s", __func__, strerror(errno));
		return (-1);
	}
	if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
		syslog(LOG_WARNING, "<%s> fcntl F_GETFL", __func__);
		close(s->si_fd);
		return (-1);
	}
	if ((flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK)) == -1) {
		syslog(LOG_WARNING, "<%s> fcntl F_SETFL", __func__);
		return (-1);
	}
	syslog(LOG_DEBUG, "<%s> accept connfd=%d, listenfd=%d", __func__,
	    fd, s->si_fd);

	return (fd);
}

int
csock_close(struct sockinfo *s)
{
	close(s->si_fd);
	unlink(s->si_name);
	syslog(LOG_DEBUG, "<%s> remove %s", __func__, s->si_name);
	return (0);
}

int
csock_listen(struct sockinfo *s)
{
	if (s->si_fd == -1) {
		syslog(LOG_ERR, "<%s> listen failed", __func__);
		return (-1);
	}
	if (listen(s->si_fd, SOCK_BACKLOG) == -1) {
		syslog(LOG_ERR, "<%s> listen failed", __func__);
		return (-1);
	}

	return (0);
}

int
csock_open(struct sockinfo *s, mode_t mode)
{
	int flags;
	struct sockaddr_un	sun;
	mode_t	old_umask;

	if (s == NULL) {
		syslog(LOG_ERR, "<%s> internal error.", __func__);
		exit(1);
	}
	if (s->si_name == NULL)
		s->si_name = _PATH_CTRL_SOCK;

	if ((s->si_fd = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		syslog(LOG_ERR,
		    "<%s> cannot open control socket", __func__);
		return (-1);
	}
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	sun.sun_len = sizeof(sun);
	strlcpy(sun.sun_path, s->si_name, sizeof(sun.sun_path));

	if (unlink(s->si_name) == -1)
		if (errno != ENOENT) {
			syslog(LOG_ERR,
			    "<%s> unlink %s", __func__, s->si_name);
			close(s->si_fd);
			return (-1);
		}
	old_umask = umask(S_IXUSR|S_IXGRP|S_IXOTH);
	if (bind(s->si_fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		syslog(LOG_ERR,
		    "<%s> bind failed: %s", __func__, s->si_name);
		close(s->si_fd);
		umask(old_umask);
		return (-1);
	}
	umask(old_umask);
	if (chmod(s->si_name, mode) == -1) {
		syslog(LOG_ERR,
		    "<%s> chmod failed: %s", __func__, s->si_name);
		goto csock_open_err;
	}
	if ((flags = fcntl(s->si_fd, F_GETFL, 0)) == -1) {
		syslog(LOG_ERR,
		    "<%s> fcntl F_GETFL failed: %s", __func__, s->si_name);
		goto csock_open_err;
	}
	if ((flags = fcntl(s->si_fd, F_SETFL, flags | O_NONBLOCK)) == -1) {
		syslog(LOG_ERR,
		    "<%s> fcntl F_SETFL failed: %s", __func__, s->si_name);
		goto csock_open_err;
	}

	return (s->si_fd);

csock_open_err:
	close(s->si_fd);
	unlink(s->si_name);
	return (-1);
}

struct ctrl_msg_pl *
cm_bin2pl(char *str, struct ctrl_msg_pl *cp)
{
	size_t len;
	size_t *lenp;
	char *p;

	memset(cp, 0, sizeof(*cp));

	p = str;

	lenp = (size_t *)p;
	len = *lenp++;
	p = (char *)lenp;
	syslog(LOG_DEBUG, "<%s> len(ifname) = %zu", __func__, len);
	if (len > 0) {
		cp->cp_ifname = malloc(len + 1);
		if (cp->cp_ifname == NULL) {
			syslog(LOG_ERR, "<%s> malloc", __func__);
			exit(1);
		}
		memcpy(cp->cp_ifname, p, len);
		cp->cp_ifname[len] = '\0';
		p += len;
	}

	lenp = (size_t *)p;
	len = *lenp++;
	p = (char *)lenp;
	syslog(LOG_DEBUG, "<%s> len(key) = %zu", __func__, len);
	if (len > 0) {
		cp->cp_key = malloc(len + 1);
		if (cp->cp_key == NULL) {
			syslog(LOG_ERR, "<%s> malloc", __func__);
			exit(1);
		}
		memcpy(cp->cp_key, p, len);
		cp->cp_key[len] = '\0';
		p += len;
	}

	lenp = (size_t *)p;
	len = *lenp++;
	p = (char *)lenp;
	syslog(LOG_DEBUG, "<%s> len(val) = %zu", __func__, len);
	if (len > 0) {
		cp->cp_val = malloc(len + 1);
		if (cp->cp_val == NULL) {
			syslog(LOG_ERR, "<%s> malloc", __func__);
			exit(1);
		}
		memcpy(cp->cp_val, p, len);
		cp->cp_val[len] = '\0';
		cp->cp_val_len = len;
	} else
		cp->cp_val_len = 0;

	return (cp);
}

size_t
cm_pl2bin(char *str, struct ctrl_msg_pl *cp)
{
	size_t len;
	size_t *lenp;
	char *p;
	struct ctrl_msg_hdr *cm;

	len = sizeof(size_t);
	if (cp->cp_ifname != NULL)
		len += strlen(cp->cp_ifname);
	len += sizeof(size_t);
	if (cp->cp_key != NULL)
		len += strlen(cp->cp_key);
	len += sizeof(size_t);
	if (cp->cp_val != NULL && cp->cp_val_len > 0)
		len += cp->cp_val_len;

	if (len > CM_MSG_MAXLEN - sizeof(*cm)) {
		syslog(LOG_DEBUG, "<%s> msg too long (len=%zu)",
		    __func__, len);
		return (0);
	}
	syslog(LOG_DEBUG, "<%s> msglen=%zu", __func__, len);
	memset(str, 0, len);
	p = str;
	lenp = (size_t *)p;
	
	if (cp->cp_ifname != NULL) {
		*lenp++ = strlen(cp->cp_ifname);
		p = (char *)lenp;
		memcpy(p, cp->cp_ifname, strlen(cp->cp_ifname));
		p += strlen(cp->cp_ifname);
	} else {
		*lenp++ = '\0';
		p = (char *)lenp;
	}

	lenp = (size_t *)p;
	if (cp->cp_key != NULL) {
		*lenp++ = strlen(cp->cp_key);
		p = (char *)lenp;
		memcpy(p, cp->cp_key, strlen(cp->cp_key));
		p += strlen(cp->cp_key);
	} else {
		*lenp++ = '\0';
		p = (char *)lenp;
	}

	lenp = (size_t *)p;
	if (cp->cp_val != NULL && cp->cp_val_len > 0) {
		*lenp++ = cp->cp_val_len;
		p = (char *)lenp;
		memcpy(p, cp->cp_val, cp->cp_val_len);
		p += cp->cp_val_len;
	} else {
		*lenp++ = '\0';
		p = (char *)lenp;
	}

	return (len);
}

size_t
cm_str2bin(char *bin, void *str, size_t len)
{
	struct ctrl_msg_hdr *cm;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (len > CM_MSG_MAXLEN - sizeof(*cm)) {
		syslog(LOG_DEBUG, "<%s> msg too long (len=%zu)",
		    __func__, len);
		return (0);
	}
	syslog(LOG_DEBUG, "<%s> msglen=%zu", __func__, len);
	memcpy(bin, (char *)str, len);

	return (len);
}

void *
cm_bin2str(char *bin, void *str, size_t len)
{

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	memcpy((char *)str, bin, len);

	return (str);
}
