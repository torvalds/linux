/*	$OpenBSD: privsep.c,v 1.29 2024/11/21 13:43:10 claudio Exp $ */

/*
 * Copyright (c) 2010 Yasuoka Masahiko <yasuoka@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/pfkeyv2.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"
#include "privsep.h"

#include "npppd.h"
#include "ppp.h"
#include "log.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

enum imsg_code {
	PRIVSEP_OK,
	PRIVSEP_OPEN,
	PRIVSEP_SOCKET,
	PRIVSEP_BIND,
	PRIVSEP_SENDTO,
	PRIVSEP_UNLINK,
	PRIVSEP_GET_USER_INFO,
	PRIVSEP_GET_IF_ADDR,
	PRIVSEP_SET_IF_ADDR,
	PRIVSEP_DEL_IF_ADDR,
	PRIVSEP_GET_IF_FLAGS,
	PRIVSEP_SET_IF_FLAGS
};

struct PRIVSEP_OPEN_ARG {
	char			 path[PATH_MAX];
	int			 flags;
};

struct PRIVSEP_SOCKET_ARG {
	int			 domain;
	int			 type;
	int			 protocol;
};

struct PRIVSEP_BIND_ARG {
	struct sockaddr_storage	 name;
	socklen_t		 namelen;
};

struct PRIVSEP_SENDTO_ARG {
	size_t			 len;
	int			 flags;
	struct sockaddr_storage	 to;
	socklen_t		 tolen;
	u_char			 msg[0];
};

struct PRIVSEP_UNLINK_ARG {
	char			 path[PATH_MAX];
};

struct PRIVSEP_GET_USER_INFO_ARG {
	char			 path[PATH_MAX];
	char			 username[MAX_USERNAME_LENGTH];
};

struct PRIVSEP_GET_IF_ADDR_ARG {
	char			 ifname[IFNAMSIZ];
};

struct PRIVSEP_GET_IF_ADDR_RESP {
	int			 retval;
	int			 rerrno;
	struct in_addr		 addr;
};

struct PRIVSEP_SET_IF_ADDR_ARG {
	char			 ifname[IFNAMSIZ];
	struct in_addr		 addr;
};

struct PRIVSEP_DEL_IF_ADDR_ARG {
	char			 ifname[IFNAMSIZ];
};

struct PRIVSEP_GET_IF_FLAGS_ARG {
	char			 ifname[IFNAMSIZ];
	int			 flags;
};

struct PRIVSEP_GET_IF_FLAGS_RESP {
	int			 retval;
	int			 rerrno;
	int			 flags;
};

struct PRIVSEP_SET_IF_FLAGS_ARG {
	char			 ifname[IFNAMSIZ];
	int			 flags;
};

struct PRIVSEP_COMMON_RESP {
	int			 retval;
	int			 rerrno;
};

struct PRIVSEP_GET_USER_INFO_RESP {
	int			 retval;
	int			 rerrno;
	char			 password[MAX_PASSWORD_LENGTH];
	struct in_addr		 framed_ip_address;
	struct in_addr		 framed_ip_netmask;
	char			 calling_number[NPPPD_PHONE_NUMBER_LEN + 1];
};

static void	 privsep_priv_main (int);
static void	 privsep_priv_dispatch_imsg (struct imsgbuf *);
int		 imsg_read_and_get(struct imsgbuf *, struct imsg *);
static int	 startswith(const char *, const char *);
static int	 privsep_recvfd (void);
static int	 privsep_common_resp (void);

static int	 privsep_npppd_check_open (struct PRIVSEP_OPEN_ARG *);
static int	 privsep_npppd_check_socket (struct PRIVSEP_SOCKET_ARG *);
static int	 privsep_npppd_check_bind (struct PRIVSEP_BIND_ARG *);
static int	 privsep_npppd_check_sendto (struct PRIVSEP_SENDTO_ARG *);
static int	 privsep_npppd_check_unlink (struct PRIVSEP_UNLINK_ARG *);
static int	 privsep_npppd_check_get_user_info (
		    struct PRIVSEP_GET_USER_INFO_ARG *);
static int	 privsep_npppd_check_get_if_addr (
		    struct PRIVSEP_GET_IF_ADDR_ARG *);
static int	 privsep_npppd_check_set_if_addr (
		    struct PRIVSEP_SET_IF_ADDR_ARG *);
static int	 privsep_npppd_check_del_if_addr (
		    struct PRIVSEP_DEL_IF_ADDR_ARG *);
static int	 privsep_npppd_check_get_if_flags (
		    struct PRIVSEP_GET_IF_FLAGS_ARG *);
static int	 privsep_npppd_check_set_if_flags (
		    struct PRIVSEP_SET_IF_FLAGS_ARG *);

static int		 privsep_sock = -1;
static struct imsgbuf	 privsep_ibuf;
static pid_t		 privsep_pid;

int
privsep_init(void)
{
	pid_t	 pid;
	int	 pairsock[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pairsock) == -1)
		return (-1);

	if ((pid = fork()) < 0)
		goto fail;
	else if (pid == 0) {
		setsid();
		/* privileged process */
		setproctitle("[priv]");
		close(pairsock[1]);
		privsep_priv_main(pairsock[0]);
		_exit(0);
		/* NOTREACHED */
	}
	close(pairsock[0]);
	privsep_sock = pairsock[1];
	privsep_pid = pid;
	if (imsgbuf_init(&privsep_ibuf, privsep_sock) == -1)
		goto fail;
	imsgbuf_allow_fdpass(&privsep_ibuf);

	return (0);
	/* NOTREACHED */
fail:
	if (pairsock[0] >= 0) {
		close(pairsock[0]);
		close(pairsock[1]);
	}

	return (-1);
}

void
privsep_fini(void)
{
	imsgbuf_clear(&privsep_ibuf);
	if (privsep_sock >= 0) {
		close(privsep_sock);
		privsep_sock = -1;
	}
}

pid_t
privsep_priv_pid(void)
{
	return (privsep_pid);
}

/***********************************************************************
 * Functions for from jail
 ***********************************************************************/
int
priv_bind(int sock, const struct sockaddr *name, socklen_t namelen)
{
	struct PRIVSEP_BIND_ARG	 a;

	if (namelen > sizeof(a.name)) {
		errno = EINVAL;
		return (-1);
	}
	if ((sock = dup(sock)) == -1)
		return (-1);

	memcpy(&a.name, name, namelen);
	a.namelen = namelen;

	(void)imsg_compose(&privsep_ibuf, PRIVSEP_BIND, 0, 0, sock,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_common_resp());
}

int
priv_socket(int domain, int type, int protocol)
{
	struct PRIVSEP_SOCKET_ARG a;

	a.domain = domain;
	a.type = type;
	a.protocol = protocol;
	(void)imsg_compose(&privsep_ibuf, PRIVSEP_SOCKET, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_recvfd());
}

int
priv_open(const char *path, int flags)
{
	struct PRIVSEP_OPEN_ARG a;

	strlcpy(a.path, path, sizeof(a.path));
	a.flags = flags;
	(void)imsg_compose(&privsep_ibuf, PRIVSEP_OPEN, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_recvfd());
}

FILE *
priv_fopen(const char *path)
{
	int f;
	FILE *fp;

	if ((f = priv_open(path, O_RDONLY)) < 0)
		return (NULL);

	if ((fp = fdopen(f, "r")) == NULL) {
		close(f);
		return (NULL);
	} else
		return (fp);
}

int
priv_sendto(int s, const void *msg, int len, int flags,
    const struct sockaddr *to, socklen_t tolen)
{
	struct PRIVSEP_SENDTO_ARG	 a;
	struct iovec			 iov[2];

	if (tolen > sizeof(a.to)) {
		errno = EINVAL;
		return (-1);
	}
	if ((s = dup(s)) == -1)
		return (-1);

	a.len = len;
	a.flags = flags;
	a.tolen = tolen;
	if (tolen > 0)
		memcpy(&a.to, to, tolen);
	iov[0].iov_base = &a;
	iov[0].iov_len = offsetof(struct PRIVSEP_SENDTO_ARG, msg);
	iov[1].iov_base = (void *)msg;
	iov[1].iov_len = len;

	(void)imsg_composev(&privsep_ibuf, PRIVSEP_SENDTO, 0, 0, s,
	    iov, nitems(iov));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_common_resp());
}

int
priv_send(int s, const void *msg, int len, int flags)
{
	return (priv_sendto(s, msg, len, flags, NULL, 0));
}

int
priv_unlink(const char *path)
{
	struct PRIVSEP_UNLINK_ARG a;

	strlcpy(a.path, path, sizeof(a.path));
	(void)imsg_compose(&privsep_ibuf, PRIVSEP_UNLINK, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_common_resp());
}

int
priv_get_user_info(const char *path, const char *username,
    npppd_auth_user **puser)
{
	struct imsg				 imsg;
	ssize_t					 n;
	struct PRIVSEP_GET_USER_INFO_RESP	*r;
	struct PRIVSEP_GET_USER_INFO_ARG	 a;
	npppd_auth_user				*u;
	char					*cp;
	int					 sz;

	strlcpy(a.path, path, sizeof(a.path));
	strlcpy(a.username, username, sizeof(a.username));

	(void)imsg_compose(&privsep_ibuf, PRIVSEP_GET_USER_INFO, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	if ((n = imsg_read_and_get(&privsep_ibuf, &imsg)) == -1)
		return (-1);
	if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*r)) {
		errno = EACCES;
		goto on_error;
	}
	r = imsg.data;
	if (r->retval != 0) {
		errno = r->rerrno;
		goto on_error;
	}

	sz = strlen(username) + strlen(r->password) +
	    strlen(r->calling_number) + 3;

	if ((u = malloc(offsetof(npppd_auth_user, space[sz]))) == NULL)
		goto on_error;

	cp = u->space;

	u->username = cp;
	n = strlcpy(cp, username, sz);
	cp += ++n; sz -= n;

	u->password = cp;
	n = strlcpy(cp, r->password, sz);
	cp += ++n; sz -= n;

	u->calling_number = cp;
	n = strlcpy(cp, r->calling_number, sz);
	cp += ++n; sz -= n;

	u->framed_ip_address = r->framed_ip_address;
	u->framed_ip_netmask = r->framed_ip_netmask;

	*puser = u;
	imsg_free(&imsg);

	return (0);

on_error:
	imsg_free(&imsg);
	return (-1);
}

int
priv_get_if_addr(const char *ifname, struct in_addr *addr)
{
	struct PRIVSEP_GET_IF_ADDR_ARG   a;
	struct PRIVSEP_GET_IF_ADDR_RESP *r;
	struct imsg			 imsg;
	int				 retval = -1;

	strlcpy(a.ifname, ifname, sizeof(a.ifname));

	(void)imsg_compose(&privsep_ibuf, PRIVSEP_GET_IF_ADDR, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	if (imsg_read_and_get(&privsep_ibuf, &imsg) == -1)
		return (-1);

	if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*r))
		errno = EACCES;
	else {
		r = imsg.data;
		if (r->retval != -1)
			*addr = r->addr;
		else
			errno = r->rerrno;
		retval = r->retval;
	}
	imsg_free(&imsg);

	return (retval);
}

int
priv_delete_if_addr(const char *ifname)
{
	struct PRIVSEP_DEL_IF_ADDR_ARG   a;

	strlcpy(a.ifname, ifname, sizeof(a.ifname));
	(void)imsg_compose(&privsep_ibuf, PRIVSEP_DEL_IF_ADDR, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_common_resp());
}

int
priv_set_if_addr(const char *ifname, struct in_addr *addr)
{
	struct PRIVSEP_SET_IF_ADDR_ARG   a;

	strlcpy(a.ifname, ifname, sizeof(a.ifname));
	a.addr = *addr;
	(void)imsg_compose(&privsep_ibuf, PRIVSEP_SET_IF_ADDR, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_common_resp());
}

int
priv_get_if_flags(const char *ifname, int *pflags)
{
	struct PRIVSEP_GET_IF_FLAGS_ARG		 a;
	struct PRIVSEP_GET_IF_FLAGS_RESP	*r;
	struct imsg				 imsg;
	int					 retval = -1;

	strlcpy(a.ifname, ifname, sizeof(a.ifname));
	a.flags = 0;

	(void)imsg_compose(&privsep_ibuf, PRIVSEP_GET_IF_FLAGS, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	if (imsg_read_and_get(&privsep_ibuf, &imsg) == -1)
		return (-1);
	if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*r))
		errno = EACCES;
	else {
		r = imsg.data;
		*pflags = r->flags;
		if (r->retval != 0)
			errno = r->rerrno;
		retval = r->retval;
	}
	imsg_free(&imsg);

	return (retval);
}

int
priv_set_if_flags(const char *ifname, int flags)
{
	struct PRIVSEP_SET_IF_FLAGS_ARG   a;

	strlcpy(a.ifname, ifname, sizeof(a.ifname));
	a.flags = flags;

	(void)imsg_compose(&privsep_ibuf, PRIVSEP_SET_IF_FLAGS, 0, 0, -1,
	    &a, sizeof(a));
	imsgbuf_flush(&privsep_ibuf);

	return (privsep_common_resp());
}

static int
privsep_recvfd(void)
{
	struct PRIVSEP_COMMON_RESP	*r;
	struct imsg			 imsg;
	int				 retval = -1;

	if (imsg_read_and_get(&privsep_ibuf, &imsg) == -1)
		return (-1);
	if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*r))
		errno = EACCES;
	else {
		r = imsg.data;
		retval = r->retval;
		if (r->retval != 0)
			errno = r->rerrno;
		else
			retval = imsg_get_fd(&imsg);
	}
	imsg_free(&imsg);

	return (retval);
}

static int
privsep_common_resp(void)
{
	struct PRIVSEP_COMMON_RESP	*r;
	struct imsg			 imsg;
	int				 retval = -1;

	if (imsg_read_and_get(&privsep_ibuf, &imsg) == -1) {
		errno = EACCES;
		return (-1);
	}
	if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*r))
		errno = EACCES;
	else {
		r = imsg.data;
		if (r->retval != 0)
			errno = r->rerrno;
		retval = r->retval;
	}
	imsg_free(&imsg);

	return (retval);
}

/***********************************************************************
 * privileged process
 ***********************************************************************/
static void
privsep_priv_main(int sock)
{
	struct imsgbuf	 ibuf;

	if (imsgbuf_init(&ibuf, sock) == -1)
		fatal("imsgbuf_init");
	imsgbuf_allow_fdpass(&ibuf);
	privsep_priv_dispatch_imsg(&ibuf);
	imsgbuf_clear(&ibuf);
	close(sock);

	exit(EXIT_SUCCESS);
}

static void
privsep_priv_dispatch_imsg(struct imsgbuf *ibuf)
{
	struct imsg	 imsg;

	for (;;) {
		if (imsg_read_and_get(ibuf, &imsg) == -1)
			return;

		switch (imsg.hdr.type) {
		case PRIVSEP_OPEN: {
			int				 f = -1;
			struct PRIVSEP_OPEN_ARG		*a = imsg.data;
			struct PRIVSEP_COMMON_RESP	 r = { -1, 0 };

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_open(a))
				r.rerrno = EACCES;
			else {
				if ((f = open(a->path, a->flags & ~O_CREAT))
				    == -1)
					r.rerrno = errno;
				else
					r.retval = 0;
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, f,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_SOCKET: {
			int				 s = -1;
			struct PRIVSEP_SOCKET_ARG	*a = imsg.data;
			struct PRIVSEP_COMMON_RESP	 r = { -1, 0 };

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_socket(a))
				r.rerrno = EACCES;
			else {
				if ((s = socket(a->domain, a->type,
				    a->protocol)) == -1)
					r.rerrno = errno;
				else
					r.retval = 0;
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, s,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_UNLINK: {
			struct PRIVSEP_UNLINK_ARG *a = imsg.data;
			struct PRIVSEP_COMMON_RESP r = { -1, 0 };

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_unlink(a))
				r.rerrno = EACCES;
			else {
				if ((r.retval = unlink(a->path)) != 0)
					r.rerrno = errno;
			}

			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_BIND: {
			struct PRIVSEP_BIND_ARG	*a = imsg.data;
			struct PRIVSEP_COMMON_RESP r = { -1, 0 };
			int fd;

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a) ||
			    (fd = imsg_get_fd(&imsg)) < 0)
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_bind(a))
				r.rerrno = EACCES;
			else {
				if ((r.retval = bind(fd,
				    (struct sockaddr *)&a->name, a->namelen))
				    != 0)
					r.rerrno = errno;
				close(fd);
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_GET_USER_INFO: {
			struct PRIVSEP_GET_USER_INFO_ARG *a = imsg.data;
			struct PRIVSEP_GET_USER_INFO_RESP r;
			int   retval;
			char *str, *buf, *db[2] = { NULL, NULL };

			memset(&r, 0, sizeof(r));
			r.retval = -1;
			r.framed_ip_address.s_addr = INADDR_NAS_SELECT;
			r.framed_ip_netmask.s_addr = INADDR_NONE;
			str = buf = NULL;

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a)) {
				r.rerrno = EINVAL;
				(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
				    &r, sizeof(r));
				return;
			}
			db[0] = a->path;
			if (privsep_npppd_check_get_user_info(a))
				r.rerrno = EACCES;
			else if ((retval = cgetent(&buf, db, a->username))
			    == 0) {
				if ((retval = cgetstr(buf, "password", &str))
				    >= 0) {
					if (strlcpy(r.password, str,
					    sizeof(r.password)) >=
					    sizeof(r.password))
						goto on_broken_entry;
					free(str);
					str = NULL;
				}
				if ((retval = cgetstr(buf, "calling-number",
				    &str)) >= 0) {
					if (strlcpy(r.calling_number, str,
					    sizeof(r.calling_number)) >=
					    sizeof(r.calling_number))
						goto on_broken_entry;
					free(str);
					str = NULL;
				}
				if ((retval = cgetstr(buf, "framed-ip-address",
				    &str)) >= 0) {
					if (inet_pton(AF_INET, str,
					    &r.framed_ip_address) != 1)
						goto on_broken_entry;
					free(str);
					str = NULL;
				}

				if ((retval = cgetstr(buf, "framed-ip-netmask",
				    &str)) >= 0) {
					if (inet_pton(AF_INET, str,
					    &r.framed_ip_netmask) != 1)
						goto on_broken_entry;
					free(str);
					str = NULL;
				}
				cgetclose();
				free(buf);
				r.retval = 0;
			} else if (retval == -1) {
				buf = NULL;
on_broken_entry:
				free(buf);
				free(str);
				r.retval = -1;
				r.rerrno = ENOENT;
			} else {
				r.retval = retval;
				r.rerrno = errno;
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_SENDTO: {
			struct PRIVSEP_SENDTO_ARG *a = imsg.data;
			struct PRIVSEP_COMMON_RESP r = { -1, 0 };
			int fd;

			if (imsg.hdr.len < IMSG_HEADER_SIZE + sizeof(*a) ||
			    imsg.hdr.len < IMSG_HEADER_SIZE +
				offsetof(struct PRIVSEP_SENDTO_ARG,
					msg[a->len]))
				r.rerrno = EMSGSIZE;
			else if ((fd = imsg_get_fd(&imsg)) < 0)
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_sendto(a))
				r.rerrno = EACCES;
			else {
				if (a->tolen > 0)
					r.retval = sendto(fd, a->msg,
					    a->len, a->flags,
					    (struct sockaddr *)&a->to,
					    a->tolen);
				else
					r.retval = send(fd, a->msg, a->len,
					    a->flags);
				if (r.retval < 0)
					r.rerrno = errno;
				close(fd);
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_GET_IF_ADDR: {
			int                              s;
			struct ifreq                     ifr;
			struct PRIVSEP_GET_IF_ADDR_ARG  *a = imsg.data;
			struct PRIVSEP_GET_IF_ADDR_RESP  r;

			memset(&r, 0, sizeof(r));
			r.retval = -1;
			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_get_if_addr(a))
				r.rerrno = EACCES;
			else {
				memset(&ifr, 0, sizeof(ifr));
				strlcpy(ifr.ifr_name, a->ifname,
				    sizeof(ifr.ifr_name));
				if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
				    ioctl(s, SIOCGIFADDR, &ifr) != 0) {
					r.retval = -1;
					r.rerrno = errno;
				} else {
					r.retval = 0;
					r.addr = ((struct sockaddr_in *)
					    &ifr.ifr_addr)->sin_addr;
				}
				if (s >= 0)
					close(s);
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_SET_IF_ADDR: {
			int                              s;
			struct ifaliasreq                ifra;
			struct PRIVSEP_SET_IF_ADDR_ARG  *a = imsg.data;
			struct PRIVSEP_COMMON_RESP       r = { -1, 0 };
			struct sockaddr_in              *sin4;

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_set_if_addr(a))
				r.rerrno = EACCES;
			else {
				memset(&ifra, 0, sizeof(ifra));
				strlcpy(ifra.ifra_name, a->ifname,
				    sizeof(ifra.ifra_name));

				sin4 = (struct sockaddr_in *)&ifra.ifra_addr;
				sin4->sin_family = AF_INET;
				sin4->sin_len = sizeof(struct sockaddr_in);
				sin4->sin_addr = a->addr;

				sin4 = (struct sockaddr_in *)&ifra.ifra_mask;
				sin4->sin_family = AF_INET;
				sin4->sin_len = sizeof(struct sockaddr_in);
				sin4->sin_addr.s_addr = 0xffffffffUL;

				sin4 =
				    (struct sockaddr_in *)&ifra.ifra_broadaddr;
				sin4->sin_family = AF_INET;
				sin4->sin_len = sizeof(struct sockaddr_in);
				sin4->sin_addr.s_addr = 0;

				if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
				    ioctl(s, SIOCAIFADDR, &ifra) != 0) {
					r.retval = -1;
					r.rerrno = errno;
				} else
					r.retval = 0;
				if (s >= 0)
					close(s);
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_DEL_IF_ADDR: {
			int                              s;
			struct ifreq                     ifr;
			struct PRIVSEP_DEL_IF_ADDR_ARG  *a = imsg.data;
			struct PRIVSEP_COMMON_RESP       r = { 0, -1 };

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_del_if_addr(a))
				r.rerrno = EACCES;
			else {
				memset(&ifr, 0, sizeof(ifr));
				strlcpy(ifr.ifr_name, a->ifname,
				    sizeof(ifr.ifr_name));
				if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
				    ioctl(s, SIOCDIFADDR, &ifr) != 0) {
					r.retval = -1;
					r.rerrno = errno;
				} else
					r.retval = 0;
				if (s >= 0)
					close(s);
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_GET_IF_FLAGS: {
			int                               s;
			struct ifreq                      ifr;
			struct PRIVSEP_GET_IF_FLAGS_ARG  *a = imsg.data;
			struct PRIVSEP_GET_IF_FLAGS_RESP  r;

			memset(&r, 0, sizeof(r));
			r.retval = -1;

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_get_if_flags(a)) {
				r.rerrno = EACCES;
			} else {
				memset(&ifr, 0, sizeof(ifr));
				strlcpy(ifr.ifr_name, a->ifname,
				    sizeof(ifr.ifr_name));
				if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
				    ioctl(s, SIOCGIFFLAGS, &ifr) != 0) {
					r.retval = -1;
					r.rerrno = errno;
				} else {
					r.retval = 0;
					r.flags = ifr.ifr_flags;
				}
				if (s >= 0)
					close(s);
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		case PRIVSEP_SET_IF_FLAGS: {
			int                               s;
			struct ifreq                      ifr;
			struct PRIVSEP_SET_IF_FLAGS_ARG  *a = imsg.data;
			struct PRIVSEP_COMMON_RESP        r = { -1, 0 };

			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(*a))
				r.rerrno = EINVAL;
			else if (privsep_npppd_check_set_if_flags(a))
				r.rerrno = EACCES;
			else {
				memset(&ifr, 0, sizeof(ifr));
				strlcpy(ifr.ifr_name, a->ifname,
				    sizeof(ifr.ifr_name));
				ifr.ifr_flags = a->flags;
				if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ||
				    ioctl(s, SIOCGIFFLAGS, &ifr) != 0) {
					r.retval = -1;
					r.rerrno = errno;
				} else
					r.retval = 0;
				if (s >= 0)
					close(s);
			}
			(void)imsg_compose(ibuf, PRIVSEP_OK, 0, 0, -1,
			    &r, sizeof(r));
			imsgbuf_flush(ibuf);
		    }
			break;
		}
		imsg_free(&imsg);
	}
}

int
imsg_read_and_get(struct imsgbuf *ibuf, struct imsg *imsg)
{
	ssize_t	 n;

	for (;;) {
		if (imsgbuf_read(ibuf) != 1)
			return (-1);
		if ((n = imsg_get(ibuf, imsg)) < 0)
			return (-1);
		if (n == 0)
			continue;
		break;
	}

	return (0);
}

static int
startswith(const char *str, const char *prefix)
{
	return (strncmp(str, prefix, strlen(prefix)) == 0)? 1 : 0;
}

static int
privsep_npppd_check_open(struct PRIVSEP_OPEN_ARG *arg)
{
	int i;
	struct _allow_paths {
		const char *path;
		int path_is_prefix;
		int readonly;
	} const allow_paths[] = {
		{ NPPPD_DIR "/",	1,	1 },
		{ "/dev/bpf",		0,	0 },
		{ "/etc/resolv.conf",	0,	1 },
		{ "/dev/tun",		1,	0 },
		{ "/dev/pppac",		1,	0 },
		{ "/dev/pppx",		1,	0 }
	};

	/* O_NONBLOCK is the only 'extra' flag permitted */
	if (arg->flags & ~(O_ACCMODE | O_NONBLOCK))
		return (1);
	for (i = 0; i < (int)nitems(allow_paths); i++) {
		if (allow_paths[i].path_is_prefix) {
			if (!startswith(arg->path, allow_paths[i].path))
				continue;
		} else if (strcmp(arg->path, allow_paths[i].path) != 0)
			continue;
		if (allow_paths[i].readonly) {
			if ((arg->flags & O_ACCMODE) != O_RDONLY)
				continue;
		}
		return (0);
	}
	return (1);
}

static int
privsep_npppd_check_socket(struct PRIVSEP_SOCKET_ARG *arg)
{
	/* npppd uses routing socket */
	if (arg->domain == PF_ROUTE && arg->type == SOCK_RAW &&
	    arg->protocol  == AF_UNSPEC)
		return (0);

	/* npppd uses raw ip socket for GRE */
	if (arg->domain == AF_INET && arg->type == SOCK_RAW &&
	    arg->protocol == IPPROTO_GRE)
		return (0);

	/* L2TP uses PF_KEY socket to delete IPsec-SA */
	if (arg->domain == PF_KEY && arg->type == SOCK_RAW &&
	    arg->protocol == PF_KEY_V2)
		return (0);

	return (1);
}

static int
privsep_npppd_check_bind(struct PRIVSEP_BIND_ARG *arg)
{
	return (1);
}

static int
privsep_npppd_check_sendto(struct PRIVSEP_SENDTO_ARG *arg)
{
	/* for reply npppdctl's request */
	if (arg->flags == 0 && arg->tolen > 0 &&
	    arg->to.ss_family == AF_UNIX)
		return (0);

	/* for sending a routing socket message. */
	if (arg->flags == 0 && arg->tolen == 0)
		return (0);

	return (1);
}

static int
privsep_npppd_check_unlink(struct PRIVSEP_UNLINK_ARG *arg)
{

	return (1);
}

static int
privsep_npppd_check_get_user_info(struct PRIVSEP_GET_USER_INFO_ARG *arg)
{
	int l;

	l = strlen(NPPPD_DIR "/");
	if (strncmp(arg->path, NPPPD_DIR "/", l) == 0)
		return (0);

	return (1);
}

static int
privsep_npppd_check_ifname(const char *ifname)
{
	if (startswith(ifname, "tun") ||
	    startswith(ifname, "pppac") ||
	    startswith(ifname, "pppx"))
		return (0);

	return (0);
}

static int
privsep_npppd_check_get_if_addr(struct PRIVSEP_GET_IF_ADDR_ARG *arg)
{
	return (privsep_npppd_check_ifname(arg->ifname));
}

static int
privsep_npppd_check_set_if_addr(struct PRIVSEP_SET_IF_ADDR_ARG *arg)
{
	return (privsep_npppd_check_ifname(arg->ifname));
}

static int
privsep_npppd_check_del_if_addr(struct PRIVSEP_DEL_IF_ADDR_ARG *arg)
{
	return (privsep_npppd_check_ifname(arg->ifname));
}

static int
privsep_npppd_check_get_if_flags(struct PRIVSEP_GET_IF_FLAGS_ARG *arg)
{
	return (privsep_npppd_check_ifname(arg->ifname));
}

static int
privsep_npppd_check_set_if_flags(struct PRIVSEP_SET_IF_FLAGS_ARG *arg)
{
	return (privsep_npppd_check_ifname(arg->ifname));
}
