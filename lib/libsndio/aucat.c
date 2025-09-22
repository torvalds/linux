/*	$OpenBSD: aucat.c,v 1.79 2021/11/07 20:51:47 ratchov Exp $	*/
/*
 * Copyright (c) 2008 Alexandre Ratchov <alex@caoua.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aucat.h"
#include "debug.h"


/*
 * read a message, return 0 if not completed
 */
int
_aucat_rmsg(struct aucat *hdl, int *eof)
{
	ssize_t n;
	unsigned char *data;

	if (hdl->rstate != RSTATE_MSG) {
		DPRINTF("_aucat_rmsg: bad state\n");
		abort();
	}
	while (hdl->rtodo > 0) {
		data = (unsigned char *)&hdl->rmsg;
		data += sizeof(struct amsg) - hdl->rtodo;
		while ((n = read(hdl->fd, data, hdl->rtodo)) == -1) {
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN) {
				*eof = 1;
				DPERROR("_aucat_rmsg: read");
			}
			return 0;
		}
		if (n == 0) {
			DPRINTF("_aucat_rmsg: eof\n");
			*eof = 1;
			return 0;
		}
		hdl->rtodo -= n;
	}
	if (ntohl(hdl->rmsg.cmd) == AMSG_DATA) {
		hdl->rtodo = ntohl(hdl->rmsg.u.data.size);
		hdl->rstate = RSTATE_DATA;
	} else {
		hdl->rtodo = sizeof(struct amsg);
		hdl->rstate = RSTATE_MSG;
	}
	return 1;
}

/*
 * write a message, return 0 if not completed
 */
int
_aucat_wmsg(struct aucat *hdl, int *eof)
{
	ssize_t n;
	unsigned char *data;

	if (hdl->wstate == WSTATE_IDLE) {
		hdl->wstate = WSTATE_MSG;
		hdl->wtodo = sizeof(struct amsg);
	}
	if (hdl->wstate != WSTATE_MSG) {
		DPRINTF("_aucat_wmsg: bad state\n");
		abort();
	}
	while (hdl->wtodo > 0) {
		data = (unsigned char *)&hdl->wmsg;
		data += sizeof(struct amsg) - hdl->wtodo;
		while ((n = write(hdl->fd, data, hdl->wtodo)) == -1) {
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN) {
				*eof = 1;
				DPERROR("_aucat_wmsg: write");
			}
			return 0;
		}
		hdl->wtodo -= n;
	}
	if (ntohl(hdl->wmsg.cmd) == AMSG_DATA) {
		hdl->wtodo = ntohl(hdl->wmsg.u.data.size);
		hdl->wstate = WSTATE_DATA;
	} else {
		hdl->wtodo = 0xdeadbeef;
		hdl->wstate = WSTATE_IDLE;
	}
	return 1;
}

size_t
_aucat_rdata(struct aucat *hdl, void *buf, size_t len, int *eof)
{
	ssize_t n;

	if (hdl->rstate != RSTATE_DATA) {
		DPRINTF("_aucat_rdata: bad state\n");
		abort();
	}
	if (len > hdl->rtodo)
		len = hdl->rtodo;
	while ((n = read(hdl->fd, buf, len)) == -1) {
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN) {
			*eof = 1;
			DPERROR("_aucat_rdata: read");
		}
		return 0;
	}
	if (n == 0) {
		DPRINTF("_aucat_rdata: eof\n");
		*eof = 1;
		return 0;
	}
	hdl->rtodo -= n;
	if (hdl->rtodo == 0) {
		hdl->rstate = RSTATE_MSG;
		hdl->rtodo = sizeof(struct amsg);
	}
	DPRINTFN(2, "_aucat_rdata: read: n = %zd\n", n);
	return n;
}

size_t
_aucat_wdata(struct aucat *hdl, const void *buf, size_t len,
   unsigned int wbpf, int *eof)
{
	ssize_t n;
	size_t datasize;

	switch (hdl->wstate) {
	case WSTATE_IDLE:
		datasize = len;
		if (datasize > AMSG_DATAMAX)
			datasize = AMSG_DATAMAX;
		datasize -= datasize % wbpf;
		if (datasize == 0)
			datasize = wbpf;
		hdl->wmsg.cmd = htonl(AMSG_DATA);
		hdl->wmsg.u.data.size = htonl(datasize);
		hdl->wtodo = sizeof(struct amsg);
		hdl->wstate = WSTATE_MSG;
		/* FALLTHROUGH */
	case WSTATE_MSG:
		if (!_aucat_wmsg(hdl, eof))
			return 0;
	}
	if (len > hdl->wtodo)
		len = hdl->wtodo;
	if (len == 0) {
		DPRINTF("_aucat_wdata: len == 0\n");
		abort();
	}
	while ((n = write(hdl->fd, buf, len)) == -1) {
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN) {
			*eof = 1;
			DPERROR("_aucat_wdata: write");
		}
		return 0;
	}
	DPRINTFN(2, "_aucat_wdata: write: n = %zd\n", n);
	hdl->wtodo -= n;
	if (hdl->wtodo == 0) {
		hdl->wstate = WSTATE_IDLE;
		hdl->wtodo = 0xdeadbeef;
	}
	return n;
}

static int
aucat_mkcookie(unsigned char *cookie)
{
#define COOKIE_DIR	"/.sndio"
#define COOKIE_SUFFIX	"/.sndio/cookie"
#define TEMPL_SUFFIX	".XXXXXXXX"
	struct stat sb;
	char *home, *path = NULL, *tmp = NULL;
	size_t home_len, path_len;
	int fd, len;

	/* please gcc */
	path_len = 0xdeadbeef;

	/*
	 * try to load the cookie
	 */
	home = issetugid() ? NULL : getenv("HOME");
	if (home == NULL)
		goto bad_gen;
	home_len = strlen(home);
	path = malloc(home_len + sizeof(COOKIE_SUFFIX));
	if (path == NULL)
		goto bad_gen;
	memcpy(path, home, home_len);
	memcpy(path + home_len, COOKIE_SUFFIX, sizeof(COOKIE_SUFFIX));
	path_len = home_len + sizeof(COOKIE_SUFFIX) - 1;
	fd = open(path, O_RDONLY);
	if (fd == -1) {
		if (errno != ENOENT)
			DPERROR(path);
		goto bad_gen;
	}
	if (fstat(fd, &sb) == -1) {
		DPERROR(path);
		goto bad_close;
	}
	if (sb.st_mode & 0077) {
		DPRINTF("%s has wrong permissions\n", path);
		goto bad_close;
	}
	len = read(fd, cookie, AMSG_COOKIELEN);
	if (len == -1) {
		DPERROR(path);
		goto bad_close;
	}
	if (len != AMSG_COOKIELEN) {
		DPRINTF("%s: short read\n", path);
		goto bad_close;
	}
	close(fd);
	goto done;
bad_close:
	close(fd);
bad_gen:
	/*
	 * generate a new cookie
	 */
	arc4random_buf(cookie, AMSG_COOKIELEN);

	/*
	 * try to save the cookie
	 */

	if (home == NULL)
		goto done;
	tmp = malloc(path_len + sizeof(TEMPL_SUFFIX));
	if (tmp == NULL)
		goto done;

	/* create ~/.sndio directory */
	memcpy(tmp, home, home_len);
	memcpy(tmp + home_len, COOKIE_DIR, sizeof(COOKIE_DIR));
	if (mkdir(tmp, 0755) == -1 && errno != EEXIST)
		goto done;

	/* create cookie file in it */
	memcpy(tmp, path, path_len);
	memcpy(tmp + path_len, TEMPL_SUFFIX, sizeof(TEMPL_SUFFIX));
	fd = mkstemp(tmp);
	if (fd == -1) {
		DPERROR(tmp);
		goto done;
	}
	if (write(fd, cookie, AMSG_COOKIELEN) == -1) {
		DPERROR(tmp);
		unlink(tmp);
		close(fd);
		goto done;
	}
	close(fd);
	if (rename(tmp, path) == -1) {
		DPERROR(tmp);
		unlink(tmp);
	}
done:
	free(tmp);
	free(path);
	return 1;
}

static int
aucat_connect_tcp(struct aucat *hdl, char *host, unsigned int unit)
{
	int s, error, opt;
	struct addrinfo *ailist, *ai, aihints;
	char serv[NI_MAXSERV];

	snprintf(serv, sizeof(serv), "%u", unit + AUCAT_PORT);
	memset(&aihints, 0, sizeof(struct addrinfo));
	aihints.ai_socktype = SOCK_STREAM;
	aihints.ai_protocol = IPPROTO_TCP;
	error = getaddrinfo(host, serv, &aihints, &ailist);
	if (error) {
		DPRINTF("%s: %s\n", host, gai_strerror(error));
		return 0;
	}
	s = -1;
	for (ai = ailist; ai != NULL; ai = ai->ai_next) {
		s = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC,
		    ai->ai_protocol);
		if (s == -1) {
			DPERROR("socket");
			continue;
		}
	restart:
		if (connect(s, ai->ai_addr, ai->ai_addrlen) == -1) {
			if (errno == EINTR)
				goto restart;
			DPERROR("connect");
			close(s);
			s = -1;
			continue;
		}
		break;
	}
	freeaddrinfo(ailist);
	if (s == -1)
		return 0;
	opt = 1;
	if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(int)) == -1) {
		DPERROR("setsockopt");
		close(s);
		return 0;
	}
	hdl->fd = s;
	return 1;
}

static int
aucat_connect_un(struct aucat *hdl, unsigned int unit)
{
	struct sockaddr_un ca;
	socklen_t len = sizeof(struct sockaddr_un);
	uid_t uid;
	int s;

	uid = geteuid();
	snprintf(ca.sun_path, sizeof(ca.sun_path),
	    SOCKPATH_DIR "-%u/" SOCKPATH_FILE "%u", uid, unit);
	ca.sun_family = AF_UNIX;
	s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (s == -1)
		return 0;
	while (connect(s, (struct sockaddr *)&ca, len) == -1) {
		if (errno == EINTR)
			continue;
		DPERROR(ca.sun_path);
		/* try shared server */
		snprintf(ca.sun_path, sizeof(ca.sun_path),
		    SOCKPATH_DIR "/" SOCKPATH_FILE "%u", unit);
		while (connect(s, (struct sockaddr *)&ca, len) == -1) {
			if (errno == EINTR)
				continue;
			DPERROR(ca.sun_path);
			close(s);
			return 0;
		}
		break;
	}
	hdl->fd = s;
	DPRINTFN(2, "%s: connected\n", ca.sun_path);
	return 1;
}

static const char *
parsestr(const char *str, char *rstr, unsigned int max)
{
	const char *p = str;

	while (*p != '\0' && *p != ',' && *p != '/') {
		if (--max == 0) {
			DPRINTF("%s: string too long\n", str);
			return NULL;
		}
		*rstr++ = *p++;
	}
	if (str == p) {
		DPRINTF("%s: string expected\n", str);
		return NULL;
	}
	*rstr = '\0';
	return p;
}

int
_aucat_open(struct aucat *hdl, const char *str, unsigned int mode)
{
	extern char *__progname;
	int eof;
	char host[NI_MAXHOST], opt[AMSG_OPTMAX];
	const char *p;
	unsigned int unit, devnum, type;

	if ((p = _sndio_parsetype(str, "snd")) != NULL)
		type = 0;
	else if ((p = _sndio_parsetype(str, "midithru")) != NULL)
		type = 1;
	else if ((p = _sndio_parsetype(str, "midi")) != NULL)
		type = 2;
	else {
		DPRINTF("%s: unsupported device type\n", str);
		return -1;
	}
	if (*p == '@') {
		p = parsestr(++p, host, NI_MAXHOST);
		if (p == NULL)
			return 0;
	} else
		*host = '\0';
	if (*p == ',') {
		p = _sndio_parsenum(++p, &unit, 15);
		if (p == NULL)
			return 0;
	} else
		unit = 0;
	if (*p != '/') {
		DPRINTF("%s: '/' expected\n", str);
		return 0;
	}
	p++;
	if (type == 0) {
		if (*p < '0' || *p > '9') {
			devnum = AMSG_NODEV;
			p = parsestr(p, opt, AMSG_OPTMAX);
			if (p == NULL)
				return 0;
		} else {
			p = _sndio_parsenum(p, &devnum, 15);
			if (p == NULL)
				return 0;
			if (*p == '.') {
				p = parsestr(++p, opt, AMSG_OPTMAX);
				if (p == NULL)
					return 0;
			} else
				strlcpy(opt, "default", AMSG_OPTMAX);
		}
	} else {
		p = _sndio_parsenum(p, &devnum, 15);
		if (p == NULL)
			return 0;
		memset(opt, 0, sizeof(opt));
	}
	if (*p != '\0') {
		DPRINTF("%s: junk at end of dev name\n", p);
		return 0;
	}
	devnum += type * 16; /* XXX */
	DPRINTFN(2, "_aucat_open: host=%s unit=%u devnum=%u opt=%s\n",
	    host, unit, devnum, opt);
	if (host[0] != '\0') {
		if (!aucat_connect_tcp(hdl, host, unit))
			return 0;
	} else {
		if (!aucat_connect_un(hdl, unit))
			return 0;
	}
	hdl->rstate = RSTATE_MSG;
	hdl->rtodo = sizeof(struct amsg);
	hdl->wstate = WSTATE_IDLE;
	hdl->wtodo = 0xdeadbeef;
	hdl->maxwrite = 0;

	/*
	 * say hello to server
	 */
	AMSG_INIT(&hdl->wmsg);
	hdl->wmsg.cmd = htonl(AMSG_AUTH);
	if (!aucat_mkcookie(hdl->wmsg.u.auth.cookie))
		goto bad_connect;
	hdl->wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(hdl, &eof))
		goto bad_connect;
	AMSG_INIT(&hdl->wmsg);
	hdl->wmsg.cmd = htonl(AMSG_HELLO);
	hdl->wmsg.u.hello.version = AMSG_VERSION;
	hdl->wmsg.u.hello.mode = htons(mode);
	hdl->wmsg.u.hello.devnum = devnum;
	hdl->wmsg.u.hello.id = htonl(getpid());
	strlcpy(hdl->wmsg.u.hello.who, __progname,
	    sizeof(hdl->wmsg.u.hello.who));
	strlcpy(hdl->wmsg.u.hello.opt, opt,
	    sizeof(hdl->wmsg.u.hello.opt));
	hdl->wtodo = sizeof(struct amsg);
	if (!_aucat_wmsg(hdl, &eof))
		goto bad_connect;
	hdl->rtodo = sizeof(struct amsg);
	if (!_aucat_rmsg(hdl, &eof)) {
		DPRINTF("aucat_init: mode refused\n");
		goto bad_connect;
	}
	if (ntohl(hdl->rmsg.cmd) != AMSG_ACK) {
		DPRINTF("aucat_init: protocol err\n");
		goto bad_connect;
	}
	return 1;
 bad_connect:
	while (close(hdl->fd) == -1 && errno == EINTR)
		; /* retry */
	return 0;
}

void
_aucat_close(struct aucat *hdl, int eof)
{
	char dummy[sizeof(struct amsg)];
	ssize_t n;

	if (!eof) {
		AMSG_INIT(&hdl->wmsg);
		hdl->wmsg.cmd = htonl(AMSG_BYE);
		hdl->wtodo = sizeof(struct amsg);
		if (!_aucat_wmsg(hdl, &eof))
			goto bad_close;

		/*
		 * block until the peer disconnects
		 */
		while (1) {
			n = read(hdl->fd, dummy, sizeof(dummy));
			if (n == -1) {
				if (errno == EINTR)
					continue;
				break;
			}
			if (n == 0)
				break;
		}
	}
 bad_close:
	while (close(hdl->fd) == -1 && errno == EINTR)
		; /* nothing */
}

int
_aucat_setfl(struct aucat *hdl, int nbio, int *eof)
{
	if (fcntl(hdl->fd, F_SETFL, nbio ? O_NONBLOCK : 0) == -1) {
		DPERROR("_aucat_setfl: fcntl");
		*eof = 1;
		return 0;
	}
	return 1;
}

int
_aucat_pollfd(struct aucat *hdl, struct pollfd *pfd, int events)
{
	if (hdl->rstate == RSTATE_MSG)
		events |= POLLIN;
	pfd->fd = hdl->fd;
	pfd->events = events;
	return 1;
}

int
_aucat_revents(struct aucat *hdl, struct pollfd *pfd)
{
	int revents = pfd->revents;

	DPRINTFN(2, "_aucat_revents: revents: %x\n", revents);
	return revents;
}
