/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND Beerware)
 *
 * Copyright (c) 1998-2011 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Portions of this code were taken from or based on ftpio.c:
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * Major Changelog:
 *
 * Dag-Erling Smørgrav
 * 9 Jun 1998
 *
 * Incorporated into libfetch
 *
 * Jordan K. Hubbard
 * 17 Jan 1996
 *
 * Turned inside out. Now returns xfers as new file ids, not as a special
 * `state' of FTP_t
 *
 * $ftpioId: ftpio.c,v 1.30 1998/04/11 07:28:53 phk Exp $
 *
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fetch.h"
#include "common.h"
#include "ftperr.h"

#define FTP_ANONYMOUS_USER	"anonymous"

#define FTP_CONNECTION_ALREADY_OPEN	125
#define FTP_OPEN_DATA_CONNECTION	150
#define FTP_OK				200
#define FTP_FILE_STATUS			213
#define FTP_SERVICE_READY		220
#define FTP_TRANSFER_COMPLETE		226
#define FTP_PASSIVE_MODE		227
#define FTP_LPASSIVE_MODE		228
#define FTP_EPASSIVE_MODE		229
#define FTP_LOGGED_IN			230
#define FTP_FILE_ACTION_OK		250
#define FTP_DIRECTORY_CREATED		257 /* multiple meanings */
#define FTP_FILE_CREATED		257 /* multiple meanings */
#define FTP_WORKING_DIRECTORY		257 /* multiple meanings */
#define FTP_NEED_PASSWORD		331
#define FTP_NEED_ACCOUNT		332
#define FTP_FILE_OK			350
#define FTP_SYNTAX_ERROR		500
#define FTP_PROTOCOL_ERROR		999

static struct url cached_host;
static conn_t	*cached_connection;

#define isftpreply(foo)				\
	(isdigit((unsigned char)foo[0]) &&	\
	    isdigit((unsigned char)foo[1]) &&	\
	    isdigit((unsigned char)foo[2]) &&	\
	    (foo[3] == ' ' || foo[3] == '\0'))
#define isftpinfo(foo) \
	(isdigit((unsigned char)foo[0]) &&	\
	    isdigit((unsigned char)foo[1]) &&	\
	    isdigit((unsigned char)foo[2]) &&	\
	    foo[3] == '-')

/*
 * Translate IPv4 mapped IPv6 address to IPv4 address
 */
static void
unmappedaddr(struct sockaddr_in6 *sin6)
{
	struct sockaddr_in *sin4;
	u_int32_t addr;
	int port;

	if (sin6->sin6_family != AF_INET6 ||
	    !IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
		return;
	sin4 = (struct sockaddr_in *)sin6;
	addr = *(u_int32_t *)(uintptr_t)&sin6->sin6_addr.s6_addr[12];
	port = sin6->sin6_port;
	memset(sin4, 0, sizeof(struct sockaddr_in));
	sin4->sin_addr.s_addr = addr;
	sin4->sin_port = port;
	sin4->sin_family = AF_INET;
	sin4->sin_len = sizeof(struct sockaddr_in);
}

/*
 * Get server response
 */
static int
ftp_chkerr(conn_t *conn)
{
	if (fetch_getln(conn) == -1) {
		fetch_syserr();
		return (-1);
	}
	if (isftpinfo(conn->buf)) {
		while (conn->buflen && !isftpreply(conn->buf)) {
			if (fetch_getln(conn) == -1) {
				fetch_syserr();
				return (-1);
			}
		}
	}

	while (conn->buflen &&
	    isspace((unsigned char)conn->buf[conn->buflen - 1]))
		conn->buflen--;
	conn->buf[conn->buflen] = '\0';

	if (!isftpreply(conn->buf)) {
		ftp_seterr(FTP_PROTOCOL_ERROR);
		return (-1);
	}

	conn->err = (conn->buf[0] - '0') * 100
	    + (conn->buf[1] - '0') * 10
	    + (conn->buf[2] - '0');

	return (conn->err);
}

/*
 * Send a command and check reply
 */
static int
ftp_cmd(conn_t *conn, const char *fmt, ...)
{
	va_list ap;
	size_t len;
	char *msg;
	int r;

	va_start(ap, fmt);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (msg == NULL) {
		errno = ENOMEM;
		fetch_syserr();
		return (-1);
	}

	r = fetch_putln(conn, msg, len);
	free(msg);

	if (r == -1) {
		fetch_syserr();
		return (-1);
	}

	return (ftp_chkerr(conn));
}

/*
 * Return a pointer to the filename part of a path
 */
static const char *
ftp_filename(const char *file, int *len, int *type)
{
	const char *s;

	if ((s = strrchr(file, '/')) == NULL)
		s = file;
	else
		s = s + 1;
	*len = strlen(s);
	if (*len > 7 && strncmp(s + *len - 7, ";type=", 6) == 0) {
		*type = s[*len - 1];
		*len -= 7;
	} else {
		*type = '\0';
	}
	return (s);
}

/*
 * Get current working directory from the reply to a CWD, PWD or CDUP
 * command.
 */
static int
ftp_pwd(conn_t *conn, char *pwd, size_t pwdlen)
{
	char *src, *dst, *end;
	int q;

	if (conn->err != FTP_WORKING_DIRECTORY &&
	    conn->err != FTP_FILE_ACTION_OK)
		return (FTP_PROTOCOL_ERROR);
	end = conn->buf + conn->buflen;
	src = conn->buf + 4;
	if (src >= end || *src++ != '"')
		return (FTP_PROTOCOL_ERROR);
	for (q = 0, dst = pwd; src < end && pwdlen--; ++src) {
		if (!q && *src == '"')
			q = 1;
		else if (q && *src != '"')
			break;
		else if (q)
			*dst++ = '"', q = 0;
		else
			*dst++ = *src;
	}
	if (!pwdlen)
		return (FTP_PROTOCOL_ERROR);
	*dst = '\0';
#if 0
	DEBUGF("pwd: [%s]\n", pwd);
#endif
	return (FTP_OK);
}

/*
 * Change working directory to the directory that contains the specified
 * file.
 */
static int
ftp_cwd(conn_t *conn, const char *file)
{
	const char *beg, *end;
	char pwd[PATH_MAX];
	int e, i, len;

	/* If no slashes in name, no need to change dirs. */
	if ((end = strrchr(file, '/')) == NULL)
		return (0);
	if ((e = ftp_cmd(conn, "PWD")) != FTP_WORKING_DIRECTORY ||
	    (e = ftp_pwd(conn, pwd, sizeof(pwd))) != FTP_OK) {
		ftp_seterr(e);
		return (-1);
	}
	for (;;) {
		len = strlen(pwd);

		/* Look for a common prefix between PWD and dir to fetch. */
		for (i = 0; i <= len && i <= end - file; ++i)
			if (pwd[i] != file[i])
				break;
#if 0
		DEBUGF("have: [%.*s|%s]\n", i, pwd, pwd + i);
		DEBUGF("want: [%.*s|%s]\n", i, file, file + i);
#endif
		/* Keep going up a dir until we have a matching prefix. */
		if (pwd[i] == '\0' && (file[i - 1] == '/' || file[i] == '/'))
			break;
		if ((e = ftp_cmd(conn, "CDUP")) != FTP_FILE_ACTION_OK ||
		    (e = ftp_cmd(conn, "PWD")) != FTP_WORKING_DIRECTORY ||
		    (e = ftp_pwd(conn, pwd, sizeof(pwd))) != FTP_OK) {
			ftp_seterr(e);
			return (-1);
		}
	}

#ifdef FTP_COMBINE_CWDS
	/* Skip leading slashes, even "////". */
	for (beg = file + i; beg < end && *beg == '/'; ++beg, ++i)
		/* nothing */ ;

	/* If there is no trailing dir, we're already there. */
	if (beg >= end)
		return (0);

	/* Change to the directory all in one chunk (e.g., foo/bar/baz). */
	e = ftp_cmd(conn, "CWD %.*s", (int)(end - beg), beg);
	if (e == FTP_FILE_ACTION_OK)
		return (0);
#endif /* FTP_COMBINE_CWDS */

	/* That didn't work so go back to legacy behavior (multiple CWDs). */
	for (beg = file + i; beg < end; beg = file + i + 1) {
		while (*beg == '/')
			++beg, ++i;
		for (++i; file + i < end && file[i] != '/'; ++i)
			/* nothing */ ;
		e = ftp_cmd(conn, "CWD %.*s", file + i - beg, beg);
		if (e != FTP_FILE_ACTION_OK) {
			ftp_seterr(e);
			return (-1);
		}
	}
	return (0);
}

/*
 * Set transfer mode and data type
 */
static int
ftp_mode_type(conn_t *conn, int mode, int type)
{
	int e;

	switch (mode) {
	case 0:
	case 's':
		mode = 'S';
	case 'S':
		break;
	default:
		return (FTP_PROTOCOL_ERROR);
	}
	if ((e = ftp_cmd(conn, "MODE %c", mode)) != FTP_OK) {
		if (mode == 'S') {
			/*
			 * Stream mode is supposed to be the default - so
			 * much so that some servers not only do not
			 * support any other mode, but do not support the
			 * MODE command at all.
			 *
			 * If "MODE S" fails, it is unlikely that we
			 * previously succeeded in setting a different
			 * mode.  Therefore, we simply hope that the
			 * server is already in the correct mode, and
			 * silently ignore the failure.
			 */
		} else {
			return (e);
		}
	}

	switch (type) {
	case 0:
	case 'i':
		type = 'I';
	case 'I':
		break;
	case 'a':
		type = 'A';
	case 'A':
		break;
	case 'd':
		type = 'D';
	case 'D':
		/* can't handle yet */
	default:
		return (FTP_PROTOCOL_ERROR);
	}
	if ((e = ftp_cmd(conn, "TYPE %c", type)) != FTP_OK)
		return (e);

	return (FTP_OK);
}

/*
 * Request and parse file stats
 */
static int
ftp_stat(conn_t *conn, const char *file, struct url_stat *us)
{
	char *ln;
	const char *filename;
	int filenamelen, type;
	struct tm tm;
	time_t t;
	int e;

	us->size = -1;
	us->atime = us->mtime = 0;

	filename = ftp_filename(file, &filenamelen, &type);

	if ((e = ftp_mode_type(conn, 0, type)) != FTP_OK) {
		ftp_seterr(e);
		return (-1);
	}

	e = ftp_cmd(conn, "SIZE %.*s", filenamelen, filename);
	if (e != FTP_FILE_STATUS) {
		ftp_seterr(e);
		return (-1);
	}
	for (ln = conn->buf + 4; *ln && isspace((unsigned char)*ln); ln++)
		/* nothing */ ;
	for (us->size = 0; *ln && isdigit((unsigned char)*ln); ln++)
		us->size = us->size * 10 + *ln - '0';
	if (*ln && !isspace((unsigned char)*ln)) {
		ftp_seterr(FTP_PROTOCOL_ERROR);
		us->size = -1;
		return (-1);
	}
	if (us->size == 0)
		us->size = -1;
	DEBUGF("size: [%lld]\n", (long long)us->size);

	e = ftp_cmd(conn, "MDTM %.*s", filenamelen, filename);
	if (e != FTP_FILE_STATUS) {
		ftp_seterr(e);
		return (-1);
	}
	for (ln = conn->buf + 4; *ln && isspace((unsigned char)*ln); ln++)
		/* nothing */ ;
	switch (strspn(ln, "0123456789")) {
	case 14:
		break;
	case 15:
		ln++;
		ln[0] = '2';
		ln[1] = '0';
		break;
	default:
		ftp_seterr(FTP_PROTOCOL_ERROR);
		return (-1);
	}
	if (sscanf(ln, "%04d%02d%02d%02d%02d%02d",
	    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
	    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
		ftp_seterr(FTP_PROTOCOL_ERROR);
		return (-1);
	}
	tm.tm_mon--;
	tm.tm_year -= 1900;
	tm.tm_isdst = -1;
	t = timegm(&tm);
	if (t == (time_t)-1)
		t = time(NULL);
	us->mtime = t;
	us->atime = t;
	DEBUGF("last modified: [%04d-%02d-%02d %02d:%02d:%02d]\n",
	    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	    tm.tm_hour, tm.tm_min, tm.tm_sec);
	return (0);
}

/*
 * I/O functions for FTP
 */
struct ftpio {
	conn_t	*cconn;		/* Control connection */
	conn_t	*dconn;		/* Data connection */
	int	 dir;		/* Direction */
	int	 eof;		/* EOF reached */
	int	 err;		/* Error code */
};

static int	 ftp_readfn(void *, char *, int);
static int	 ftp_writefn(void *, const char *, int);
static fpos_t	 ftp_seekfn(void *, fpos_t, int);
static int	 ftp_closefn(void *);

static int
ftp_readfn(void *v, char *buf, int len)
{
	struct ftpio *io;
	int r;

	io = (struct ftpio *)v;
	if (io == NULL) {
		errno = EBADF;
		return (-1);
	}
	if (io->cconn == NULL || io->dconn == NULL || io->dir == O_WRONLY) {
		errno = EBADF;
		return (-1);
	}
	if (io->err) {
		errno = io->err;
		return (-1);
	}
	if (io->eof)
		return (0);
	r = fetch_read(io->dconn, buf, len);
	if (r > 0)
		return (r);
	if (r == 0) {
		io->eof = 1;
		return (0);
	}
	if (errno != EINTR)
		io->err = errno;
	return (-1);
}

static int
ftp_writefn(void *v, const char *buf, int len)
{
	struct ftpio *io;
	int w;

	io = (struct ftpio *)v;
	if (io == NULL) {
		errno = EBADF;
		return (-1);
	}
	if (io->cconn == NULL || io->dconn == NULL || io->dir == O_RDONLY) {
		errno = EBADF;
		return (-1);
	}
	if (io->err) {
		errno = io->err;
		return (-1);
	}
	w = fetch_write(io->dconn, buf, len);
	if (w >= 0)
		return (w);
	if (errno != EINTR)
		io->err = errno;
	return (-1);
}

static fpos_t
ftp_seekfn(void *v, fpos_t pos __unused, int whence __unused)
{
	struct ftpio *io;

	io = (struct ftpio *)v;
	if (io == NULL) {
		errno = EBADF;
		return (-1);
	}
	errno = ESPIPE;
	return (-1);
}

static int
ftp_closefn(void *v)
{
	struct ftpio *io;
	int r;

	io = (struct ftpio *)v;
	if (io == NULL) {
		errno = EBADF;
		return (-1);
	}
	if (io->dir == -1)
		return (0);
	if (io->cconn == NULL || io->dconn == NULL) {
		errno = EBADF;
		return (-1);
	}
	fetch_close(io->dconn);
	io->dir = -1;
	io->dconn = NULL;
	DEBUGF("Waiting for final status\n");
	r = ftp_chkerr(io->cconn);
	if (io->cconn == cached_connection && io->cconn->ref == 1)
		cached_connection = NULL;
	fetch_close(io->cconn);
	free(io);
	return (r == FTP_TRANSFER_COMPLETE) ? 0 : -1;
}

static FILE *
ftp_setup(conn_t *cconn, conn_t *dconn, int mode)
{
	struct ftpio *io;
	FILE *f;

	if (cconn == NULL || dconn == NULL)
		return (NULL);
	if ((io = malloc(sizeof(*io))) == NULL)
		return (NULL);
	io->cconn = cconn;
	io->dconn = dconn;
	io->dir = mode;
	io->eof = io->err = 0;
	f = funopen(io, ftp_readfn, ftp_writefn, ftp_seekfn, ftp_closefn);
	if (f == NULL)
		free(io);
	return (f);
}

/*
 * Transfer file
 */
static FILE *
ftp_transfer(conn_t *conn, const char *oper, const char *file,
    int mode, off_t offset, const char *flags)
{
	struct sockaddr_storage sa;
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin4;
	const char *bindaddr;
	const char *filename;
	int filenamelen, type;
	int low, pasv, verbose;
	int e, sd = -1;
	socklen_t l;
	char *s;
	FILE *df;

	/* check flags */
	low = CHECK_FLAG('l');
	pasv = CHECK_FLAG('p') || !CHECK_FLAG('P');
	verbose = CHECK_FLAG('v');

	/* passive mode */
	if ((s = getenv("FTP_PASSIVE_MODE")) != NULL)
		pasv = (strncasecmp(s, "no", 2) != 0);

	/* isolate filename */
	filename = ftp_filename(file, &filenamelen, &type);

	/* set transfer mode and data type */
	if ((e = ftp_mode_type(conn, 0, type)) != FTP_OK)
		goto ouch;

	/* find our own address, bind, and listen */
	l = sizeof(sa);
	if (getsockname(conn->sd, (struct sockaddr *)&sa, &l) == -1)
		goto sysouch;
	if (sa.ss_family == AF_INET6)
		unmappedaddr((struct sockaddr_in6 *)&sa);

	/* open data socket */
	if ((sd = socket(sa.ss_family, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		fetch_syserr();
		return (NULL);
	}

	if (pasv) {
		u_char addr[64];
		char *ln, *p;
		unsigned int i;
		int port;

		/* send PASV command */
		if (verbose)
			fetch_info("setting passive mode");
		switch (sa.ss_family) {
		case AF_INET:
			if ((e = ftp_cmd(conn, "PASV")) != FTP_PASSIVE_MODE)
				goto ouch;
			break;
		case AF_INET6:
			if ((e = ftp_cmd(conn, "EPSV")) != FTP_EPASSIVE_MODE) {
				if (e == -1)
					goto ouch;
				if ((e = ftp_cmd(conn, "LPSV")) !=
				    FTP_LPASSIVE_MODE)
					goto ouch;
			}
			break;
		default:
			e = FTP_PROTOCOL_ERROR; /* XXX: error code should be prepared */
			goto ouch;
		}

		/*
		 * Find address and port number. The reply to the PASV command
		 * is IMHO the one and only weak point in the FTP protocol.
		 */
		ln = conn->buf;
		switch (e) {
		case FTP_PASSIVE_MODE:
		case FTP_LPASSIVE_MODE:
			for (p = ln + 3; *p && !isdigit((unsigned char)*p); p++)
				/* nothing */ ;
			if (!*p) {
				e = FTP_PROTOCOL_ERROR;
				goto ouch;
			}
			l = (e == FTP_PASSIVE_MODE ? 6 : 21);
			for (i = 0; *p && i < l; i++, p++)
				addr[i] = strtol(p, &p, 10);
			if (i < l) {
				e = FTP_PROTOCOL_ERROR;
				goto ouch;
			}
			break;
		case FTP_EPASSIVE_MODE:
			for (p = ln + 3; *p && *p != '('; p++)
				/* nothing */ ;
			if (!*p) {
				e = FTP_PROTOCOL_ERROR;
				goto ouch;
			}
			++p;
			if (sscanf(p, "%c%c%c%d%c", &addr[0], &addr[1], &addr[2],
				&port, &addr[3]) != 5 ||
			    addr[0] != addr[1] ||
			    addr[0] != addr[2] || addr[0] != addr[3]) {
				e = FTP_PROTOCOL_ERROR;
				goto ouch;
			}
			break;
		}

		/* seek to required offset */
		if (offset)
			if (ftp_cmd(conn, "REST %lu", (u_long)offset) != FTP_FILE_OK)
				goto sysouch;

		/* construct sockaddr for data socket */
		l = sizeof(sa);
		if (getpeername(conn->sd, (struct sockaddr *)&sa, &l) == -1)
			goto sysouch;
		if (sa.ss_family == AF_INET6)
			unmappedaddr((struct sockaddr_in6 *)&sa);
		switch (sa.ss_family) {
		case AF_INET6:
			sin6 = (struct sockaddr_in6 *)&sa;
			if (e == FTP_EPASSIVE_MODE)
				sin6->sin6_port = htons(port);
			else {
				memcpy(&sin6->sin6_addr, addr + 2, 16);
				memcpy(&sin6->sin6_port, addr + 19, 2);
			}
			break;
		case AF_INET:
			sin4 = (struct sockaddr_in *)&sa;
			if (e == FTP_EPASSIVE_MODE)
				sin4->sin_port = htons(port);
			else {
				memcpy(&sin4->sin_addr, addr, 4);
				memcpy(&sin4->sin_port, addr + 4, 2);
			}
			break;
		default:
			e = FTP_PROTOCOL_ERROR; /* XXX: error code should be prepared */
			break;
		}

		/* connect to data port */
		if (verbose)
			fetch_info("opening data connection");
		bindaddr = getenv("FETCH_BIND_ADDRESS");
		if (bindaddr != NULL && *bindaddr != '\0' &&
		    (e = fetch_bind(sd, sa.ss_family, bindaddr)) != 0)
			goto ouch;
		if (connect(sd, (struct sockaddr *)&sa, sa.ss_len) == -1)
			goto sysouch;

		/* make the server initiate the transfer */
		if (verbose)
			fetch_info("initiating transfer");
		e = ftp_cmd(conn, "%s %.*s", oper, filenamelen, filename);
		if (e != FTP_CONNECTION_ALREADY_OPEN && e != FTP_OPEN_DATA_CONNECTION)
			goto ouch;

	} else {
		u_int32_t a;
		u_short p;
		int arg, d;
		char *ap;
		char hname[INET6_ADDRSTRLEN];

		switch (sa.ss_family) {
		case AF_INET6:
			((struct sockaddr_in6 *)&sa)->sin6_port = 0;
#ifdef IPV6_PORTRANGE
			arg = low ? IPV6_PORTRANGE_DEFAULT : IPV6_PORTRANGE_HIGH;
			if (setsockopt(sd, IPPROTO_IPV6, IPV6_PORTRANGE,
				(char *)&arg, sizeof(arg)) == -1)
				goto sysouch;
#endif
			break;
		case AF_INET:
			((struct sockaddr_in *)&sa)->sin_port = 0;
			arg = low ? IP_PORTRANGE_DEFAULT : IP_PORTRANGE_HIGH;
			if (setsockopt(sd, IPPROTO_IP, IP_PORTRANGE,
				(char *)&arg, sizeof(arg)) == -1)
				goto sysouch;
			break;
		}
		if (verbose)
			fetch_info("binding data socket");
		if (bind(sd, (struct sockaddr *)&sa, sa.ss_len) == -1)
			goto sysouch;
		if (listen(sd, 1) == -1)
			goto sysouch;

		/* find what port we're on and tell the server */
		if (getsockname(sd, (struct sockaddr *)&sa, &l) == -1)
			goto sysouch;
		switch (sa.ss_family) {
		case AF_INET:
			sin4 = (struct sockaddr_in *)&sa;
			a = ntohl(sin4->sin_addr.s_addr);
			p = ntohs(sin4->sin_port);
			e = ftp_cmd(conn, "PORT %d,%d,%d,%d,%d,%d",
			    (a >> 24) & 0xff, (a >> 16) & 0xff,
			    (a >> 8) & 0xff, a & 0xff,
			    (p >> 8) & 0xff, p & 0xff);
			break;
		case AF_INET6:
#define UC(b)	(((int)b)&0xff)
			e = -1;
			sin6 = (struct sockaddr_in6 *)&sa;
			sin6->sin6_scope_id = 0;
			if (getnameinfo((struct sockaddr *)&sa, sa.ss_len,
				hname, sizeof(hname),
				NULL, 0, NI_NUMERICHOST) == 0) {
				e = ftp_cmd(conn, "EPRT |%d|%s|%d|", 2, hname,
				    htons(sin6->sin6_port));
				if (e == -1)
					goto ouch;
			}
			if (e != FTP_OK) {
				ap = (char *)&sin6->sin6_addr;
				e = ftp_cmd(conn,
				    "LPRT %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
				    6, 16,
				    UC(ap[0]), UC(ap[1]), UC(ap[2]), UC(ap[3]),
				    UC(ap[4]), UC(ap[5]), UC(ap[6]), UC(ap[7]),
				    UC(ap[8]), UC(ap[9]), UC(ap[10]), UC(ap[11]),
				    UC(ap[12]), UC(ap[13]), UC(ap[14]), UC(ap[15]),
				    2,
				    (ntohs(sin6->sin6_port) >> 8) & 0xff,
				    ntohs(sin6->sin6_port)        & 0xff);
			}
			break;
		default:
			e = FTP_PROTOCOL_ERROR; /* XXX: error code should be prepared */
			goto ouch;
		}
		if (e != FTP_OK)
			goto ouch;

		/* seek to required offset */
		if (offset)
			if (ftp_cmd(conn, "REST %ju", (uintmax_t)offset) != FTP_FILE_OK)
				goto sysouch;

		/* make the server initiate the transfer */
		if (verbose)
			fetch_info("initiating transfer");
		e = ftp_cmd(conn, "%s %.*s", oper, filenamelen, filename);
		if (e != FTP_CONNECTION_ALREADY_OPEN && e != FTP_OPEN_DATA_CONNECTION)
			goto ouch;

		/* accept the incoming connection and go to town */
		if ((d = accept(sd, NULL, NULL)) == -1)
			goto sysouch;
		close(sd);
		sd = d;
	}

	if ((df = ftp_setup(conn, fetch_reopen(sd), mode)) == NULL)
		goto sysouch;
	return (df);

sysouch:
	fetch_syserr();
	if (sd >= 0)
		close(sd);
	return (NULL);

ouch:
	if (e != -1)
		ftp_seterr(e);
	if (sd >= 0)
		close(sd);
	return (NULL);
}

/*
 * Authenticate
 */
static int
ftp_authenticate(conn_t *conn, struct url *url, struct url *purl)
{
	const char *user, *pwd, *logname;
	char pbuf[MAXHOSTNAMELEN + MAXLOGNAME + 1];
	int e, len;

	/* XXX FTP_AUTH, and maybe .netrc */

	/* send user name and password */
	if (url->user[0] == '\0')
		fetch_netrc_auth(url);
	user = url->user;
	if (*user == '\0')
		if ((user = getenv("FTP_LOGIN")) != NULL)
			DEBUGF("FTP_LOGIN=%s\n", user);
	if (user == NULL || *user == '\0')
		user = FTP_ANONYMOUS_USER;
	if (purl && url->port == fetch_default_port(url->scheme))
		e = ftp_cmd(conn, "USER %s@%s", user, url->host);
	else if (purl)
		e = ftp_cmd(conn, "USER %s@%s@%d", user, url->host, url->port);
	else
		e = ftp_cmd(conn, "USER %s", user);

	/* did the server request a password? */
	if (e == FTP_NEED_PASSWORD) {
		pwd = url->pwd;
		if (*pwd == '\0')
			if ((pwd = getenv("FTP_PASSWORD")) != NULL)
				DEBUGF("FTP_PASSWORD=%s\n", pwd);
		if (pwd == NULL || *pwd == '\0') {
			if ((logname = getlogin()) == NULL)
				logname = FTP_ANONYMOUS_USER;
			if ((len = snprintf(pbuf, MAXLOGNAME + 1, "%s@", logname)) < 0)
				len = 0;
			else if (len > MAXLOGNAME)
				len = MAXLOGNAME;
			gethostname(pbuf + len, sizeof(pbuf) - len);
			pwd = pbuf;
		}
		e = ftp_cmd(conn, "PASS %s", pwd);
	}

	return (e);
}

/*
 * Log on to FTP server
 */
static conn_t *
ftp_connect(struct url *url, struct url *purl, const char *flags)
{
	conn_t *conn;
	int e, direct, verbose;
#ifdef INET6
	int af = AF_UNSPEC;
#else
	int af = AF_INET;
#endif

	direct = CHECK_FLAG('d');
	verbose = CHECK_FLAG('v');
	if (CHECK_FLAG('4'))
		af = AF_INET;
	else if (CHECK_FLAG('6'))
		af = AF_INET6;

	if (direct)
		purl = NULL;

	/* check for proxy */
	if (purl) {
		/* XXX proxy authentication! */
		conn = fetch_connect(purl->host, purl->port, af, verbose);
	} else {
		/* no proxy, go straight to target */
		conn = fetch_connect(url->host, url->port, af, verbose);
		purl = NULL;
	}

	/* check connection */
	if (conn == NULL)
		/* fetch_connect() has already set an error code */
		return (NULL);

	/* expect welcome message */
	if ((e = ftp_chkerr(conn)) != FTP_SERVICE_READY)
		goto fouch;

	/* authenticate */
	if ((e = ftp_authenticate(conn, url, purl)) != FTP_LOGGED_IN)
		goto fouch;

	/* TODO: Request extended features supported, if any (RFC 3659). */

	/* done */
	return (conn);

fouch:
	if (e != -1)
		ftp_seterr(e);
	fetch_close(conn);
	return (NULL);
}

/*
 * Disconnect from server
 */
static void
ftp_disconnect(conn_t *conn)
{
	(void)ftp_cmd(conn, "QUIT");
	if (conn == cached_connection && conn->ref == 1)
		cached_connection = NULL;
	fetch_close(conn);
}

/*
 * Check if we're already connected
 */
static int
ftp_isconnected(struct url *url)
{
	return (cached_connection
	    && (strcmp(url->host, cached_host.host) == 0)
	    && (strcmp(url->user, cached_host.user) == 0)
	    && (strcmp(url->pwd, cached_host.pwd) == 0)
	    && (url->port == cached_host.port));
}

/*
 * Check the cache, reconnect if no luck
 */
static conn_t *
ftp_cached_connect(struct url *url, struct url *purl, const char *flags)
{
	conn_t *conn;
	int e;

	/* set default port */
	if (!url->port)
		url->port = fetch_default_port(url->scheme);

	/* try to use previously cached connection */
	if (ftp_isconnected(url)) {
		e = ftp_cmd(cached_connection, "NOOP");
		if (e == FTP_OK || e == FTP_SYNTAX_ERROR)
			return (fetch_ref(cached_connection));
	}

	/* connect to server */
	if ((conn = ftp_connect(url, purl, flags)) == NULL)
		return (NULL);
	if (cached_connection)
		ftp_disconnect(cached_connection);
	cached_connection = fetch_ref(conn);
	memcpy(&cached_host, url, sizeof(*url));
	return (conn);
}

/*
 * Check the proxy settings
 */
static struct url *
ftp_get_proxy(struct url * url, const char *flags)
{
	struct url *purl;
	char *p;

	if (flags != NULL && strchr(flags, 'd') != NULL)
		return (NULL);
	if (fetch_no_proxy_match(url->host))
		return (NULL);
	if (((p = getenv("FTP_PROXY")) || (p = getenv("ftp_proxy")) ||
		(p = getenv("HTTP_PROXY")) || (p = getenv("http_proxy"))) &&
	    *p && (purl = fetchParseURL(p)) != NULL) {
		if (!*purl->scheme) {
			if (getenv("FTP_PROXY") || getenv("ftp_proxy"))
				strcpy(purl->scheme, SCHEME_FTP);
			else
				strcpy(purl->scheme, SCHEME_HTTP);
		}
		if (!purl->port)
			purl->port = fetch_default_proxy_port(purl->scheme);
		if (strcmp(purl->scheme, SCHEME_FTP) == 0 ||
		    strcmp(purl->scheme, SCHEME_HTTP) == 0)
			return (purl);
		fetchFreeURL(purl);
	}
	return (NULL);
}

/*
 * Process an FTP request
 */
FILE *
ftp_request(struct url *url, const char *op, struct url_stat *us,
    struct url *purl, const char *flags)
{
	conn_t *conn;
	int oflag;

	/* check if we should use HTTP instead */
	if (purl && (strcmp(purl->scheme, SCHEME_HTTP) == 0 ||
	    strcmp(purl->scheme, SCHEME_HTTPS) == 0)) {
		if (strcmp(op, "STAT") == 0)
			return (http_request(url, "HEAD", us, purl, flags));
		else if (strcmp(op, "RETR") == 0)
			return (http_request(url, "GET", us, purl, flags));
		/*
		 * Our HTTP code doesn't support PUT requests yet, so try
		 * a direct connection.
		 */
	}

	/* connect to server */
	conn = ftp_cached_connect(url, purl, flags);
	if (purl)
		fetchFreeURL(purl);
	if (conn == NULL)
		return (NULL);

	/* change directory */
	if (ftp_cwd(conn, url->doc) == -1)
		goto errsock;

	/* stat file */
	if (us && ftp_stat(conn, url->doc, us) == -1
	    && fetchLastErrCode != FETCH_PROTO
	    && fetchLastErrCode != FETCH_UNAVAIL)
		goto errsock;

	/* just a stat */
	if (strcmp(op, "STAT") == 0) {
		--conn->ref;
		ftp_disconnect(conn);
		return (FILE *)1; /* bogus return value */
	}
	if (strcmp(op, "STOR") == 0 || strcmp(op, "APPE") == 0)
		oflag = O_WRONLY;
	else
		oflag = O_RDONLY;

	/* initiate the transfer */
	return (ftp_transfer(conn, op, url->doc, oflag, url->offset, flags));

errsock:
	ftp_disconnect(conn);
	return (NULL);
}

/*
 * Get and stat file
 */
FILE *
fetchXGetFTP(struct url *url, struct url_stat *us, const char *flags)
{
	return (ftp_request(url, "RETR", us, ftp_get_proxy(url, flags), flags));
}

/*
 * Get file
 */
FILE *
fetchGetFTP(struct url *url, const char *flags)
{
	return (fetchXGetFTP(url, NULL, flags));
}

/*
 * Put file
 */
FILE *
fetchPutFTP(struct url *url, const char *flags)
{
	return (ftp_request(url, CHECK_FLAG('a') ? "APPE" : "STOR", NULL,
	    ftp_get_proxy(url, flags), flags));
}

/*
 * Get file stats
 */
int
fetchStatFTP(struct url *url, struct url_stat *us, const char *flags)
{
	FILE *f;

	f = ftp_request(url, "STAT", us, ftp_get_proxy(url, flags), flags);
	if (f == NULL)
		return (-1);
	/*
	 * When op is "STAT", ftp_request() will return either NULL or
	 * (FILE *)1, never a valid FILE *, so we mustn't fclose(f) before
	 * returning, as it would cause a segfault.
	 */
	return (0);
}

/*
 * List a directory
 */
struct url_ent *
fetchListFTP(struct url *url __unused, const char *flags __unused)
{
	warnx("fetchListFTP(): not implemented");
	return (NULL);
}
