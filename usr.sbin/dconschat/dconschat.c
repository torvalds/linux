/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003
 * 	Hidetoshi Shimokawa. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
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
 * 
 * $Id: dconschat.c,v 1.76 2003/10/23 06:21:13 simokawa Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <dev/dcons/dcons.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <err.h>
#include <string.h>
#include <sys/eui64.h>
#include <sys/event.h>
#include <sys/time.h>
#include <arpa/telnet.h>

#include <sys/ioccom.h>
#include <dev/firewire/firewire.h>
#include <dev/firewire/iec13213.h>

#include <kvm.h>
#include <nlist.h>

#include <sys/errno.h>

#define	DCONS_POLL_HZ		100
#define	DCONS_POLL_OFFLINE	2	/* sec */

#define RETRY 3

#ifdef CSRVAL_VENDOR_PRIVATE
#define	USE_CROM 1
#else
#define	USE_CROM 0
#endif

int verbose = 0;
int tc_set = 0;
int poll_hz = DCONS_POLL_HZ;
static u_char abreak[3] = {13 /* CR */, 126 /* ~ */, 2 /* ^B */};

#define IS_CONSOLE(p)	((p)->port == DCONS_CON)
#define IS_GDB(p)	((p)->port == DCONS_GDB)

static struct dcons_state {
	int fd;
	kvm_t *kd;
	int kq;
	off_t paddr;
	off_t reset;
#define F_READY		(1 << 1)
#define F_RD_ONLY	(1 << 2)
#define F_ALT_BREAK	(1 << 3)
#define F_TELNET	(1 << 4)
#define F_USE_CROM	(1 << 5)
#define F_ONE_SHOT	(1 << 6)
#define F_REPLAY	(1 << 7)
	int flags;
	enum {
		TYPE_KVM,
		TYPE_FW
	} type;
	int escape_state;
	struct dcons_port {
		int port;
		int sport;
		struct dcons_ch o;
		struct dcons_ch i;
		u_int32_t optr;
		u_int32_t iptr;
		int s;
		int infd;
		int outfd;
		struct addrinfo *res;
		int skip_read;
	} port[DCONS_NPORT];
	struct timespec to;
	struct timespec zero;
	struct termios tsave;
	struct termios traw;
	char escape;
} sc;

static int dconschat_write_dcons(struct dcons_state *, int, char *, int);

static int
dread(struct dcons_state *dc, void *buf, size_t n, off_t offset)
{
	switch (dc->type) {
	case TYPE_FW:
		return (pread(dc->fd, buf, n, offset));
	case TYPE_KVM:
		return (kvm_read(dc->kd, offset, buf, n));
	}
	return (-1);
}

static int
dwrite(struct dcons_state *dc, void *buf, size_t n, off_t offset)
{
	if ((dc->flags & F_RD_ONLY) != 0)
		return (n);

	switch (dc->type) {
	case TYPE_FW:
		return (pwrite(dc->fd, buf, n, offset));
	case TYPE_KVM:
		return (kvm_write(dc->kd, offset, buf, n));
	}
	return (-1);
}

static void
dconschat_reset_target(struct dcons_state *dc, struct dcons_port *p)
{
	char buf[PAGE_SIZE];
	if (dc->reset == 0)
		return;

	snprintf(buf, PAGE_SIZE,
	    "\r\n[dconschat reset target(addr=0x%jx)...]\r\n",
	    (intmax_t)dc->reset);
	write(p->outfd, buf, strlen(buf));
	bzero(&buf[0], PAGE_SIZE);
	dwrite(dc, (void *)buf, PAGE_SIZE, dc->reset);
}


static void
dconschat_suspend(struct dcons_state *dc, struct dcons_port *p)
{
	if (p->sport != 0)
		return;

	if (tc_set)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &dc->tsave);

	printf("\n[dconschat suspend]\n");
	kill(getpid(), SIGTSTP);

	if (tc_set)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &dc->traw);
}

static void
dconschat_sigchld(int s)
{
	struct kevent kev;
	struct dcons_port *p;
	char buf[256];

	p = &sc.port[DCONS_CON];

	snprintf(buf, 256, "\r\n[child exit]\r\n");
	write(p->outfd, buf, strlen(buf));

	if (tc_set)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &sc.traw);

	EV_SET(&kev, p->infd, EVFILT_READ, EV_ADD, NOTE_LOWAT, 1, (void *)p);
	kevent(sc.kq, &kev, 1, NULL, 0, &sc.zero);
}

static void
dconschat_fork_gdb(struct dcons_state *dc, struct dcons_port *p)
{
	pid_t pid;
	char buf[256], com[256];
	struct kevent kev;

	pid = fork();
	if (pid < 0) {
		snprintf(buf, 256, "\r\n[%s: fork failed]\r\n", __FUNCTION__);
		write(p->outfd, buf, strlen(buf));
	}


	if (pid == 0) {
		/* child */
		if (tc_set)
			tcsetattr(STDIN_FILENO, TCSADRAIN, &dc->tsave);

		snprintf(com, sizeof(buf), "kgdb -r :%d kernel",
			dc->port[DCONS_GDB].sport);
		snprintf(buf, 256, "\n[fork %s]\n", com);
		write(p->outfd, buf, strlen(buf));

		execl("/bin/sh", "/bin/sh", "-c", com, NULL);

		snprintf(buf, 256, "\n[fork failed]\n");
		write(p->outfd, buf, strlen(buf));

		if (tc_set)
			tcsetattr(STDIN_FILENO, TCSADRAIN, &dc->traw);

		exit(0);
	} else {
		signal(SIGCHLD, dconschat_sigchld);
		EV_SET(&kev, p->infd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		kevent(sc.kq, &kev, 1, NULL, 0, &sc.zero);
	}
}


static void
dconschat_cleanup(int sig)
{
	struct dcons_state *dc;
	int status;

	dc = &sc;
	if (tc_set != 0)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &dc->tsave);

	if (sig > 0)
		printf("\n[dconschat exiting with signal %d ...]\n", sig);
	else
		printf("\n[dconschat exiting...]\n");
	wait(&status);
	exit(0);
}

#if USE_CROM
static int
dconschat_get_crom(struct dcons_state *dc)
{
	off_t addr;
	int i, state = 0;
	u_int32_t buf, hi = 0, lo = 0, reset_hi = 0, reset_lo = 0;
	struct csrreg *reg; 

	reg = (struct csrreg *)&buf;
	addr = 0xffff;
	addr = (addr << 32) | 0xf0000400;
	for (i = 20; i < 0x400; i += 4) {
		if (dread(dc, &buf, 4, addr + i) < 0) {
			if (verbose)
				warn("crom read faild");
			goto out;
		}
		buf = ntohl(buf);
		if (verbose)
			printf("%d %02x %06x\n", state, reg->key, reg->val);
		switch (state) {
		case 0:
			if (reg->key == CSRKEY_SPEC &&
					reg->val == CSRVAL_VENDOR_PRIVATE)
				state = 1;
			break;
		case 1:
			if (reg->key == CSRKEY_VER &&
					reg->val == DCONS_CSR_VAL_VER)
				state = 2;
			break;
		case 2:
			switch (reg->key) {
			case DCONS_CSR_KEY_HI:
				hi = reg->val;
				break;
			case DCONS_CSR_KEY_LO:
				lo = reg->val;
				break;
			case DCONS_CSR_KEY_RESET_HI:
				reset_hi = reg->val;
				break;
			case DCONS_CSR_KEY_RESET_LO:
				reset_lo = reg->val;
				goto out;
				break;
			case 0x81:
				break;
			default:
				state = 0;
			}
			break;
		}
	}
out:
	if (verbose)
		printf("addr: %06x %06x\n", hi, lo); 
	dc->paddr = ((off_t)hi << 24) | lo;
	dc->reset = ((off_t)reset_hi << 24) | reset_lo;
	if (dc->paddr == 0)
		return (-1);
	return (0);
}
#endif

static void
dconschat_ready(struct dcons_state *dc, int ready, char *reason)
{
	static char oldreason[64] = "";
	int old;

	old = (dc->flags & F_READY) ? 1 : 0;

	if (ready) {
		dc->flags |= F_READY;
		if (ready != old)
			printf("[dcons connected]\r\n");
		oldreason[0] = 0;
	} else {
		dc->flags &= ~F_READY;
		if (strncmp(oldreason, reason, sizeof(oldreason)) != 0) {
			printf("[dcons disconnected (%s)]\r\n", reason);
			strlcpy(oldreason, reason, sizeof(oldreason));
		}
	}
}

static int
dconschat_fetch_header(struct dcons_state *dc)
{
	char ebuf[64];
	struct dcons_buf dbuf;
	int j;

#if USE_CROM
	if (dc->paddr == 0 && (dc->flags & F_USE_CROM) != 0) {
		if (dconschat_get_crom(dc)) {
			dconschat_ready(dc, 0, "get crom failed");
			return (-1);
		}
	}
#endif

	if (dread(dc, &dbuf, DCONS_HEADER_SIZE, dc->paddr) < 0) {
		dconschat_ready(dc, 0, "read header failed");
		return (-1);
	}
	if (dbuf.magic != htonl(DCONS_MAGIC)) {
		if ((dc->flags & F_USE_CROM) !=0)
			dc->paddr = 0;
		snprintf(ebuf, sizeof(ebuf), "wrong magic 0x%08x", dbuf.magic);
		dconschat_ready(dc, 0, ebuf);
		return (-1);
	}
	if (ntohl(dbuf.version) != DCONS_VERSION) {
		snprintf(ebuf, sizeof(ebuf),
#if __FreeBSD_version < 500000
		    "wrong version %ld,%d",
#else
		    "wrong version %d,%d",
#endif
		    ntohl(dbuf.version), DCONS_VERSION);
		/* XXX exit? */
		dconschat_ready(dc, 0, ebuf);
		return (-1);
	}

	for (j = 0; j < DCONS_NPORT; j++) {
		struct dcons_ch *o, *i;
		off_t newbuf;
		int new = 0;

		o = &dc->port[j].o;
		newbuf = dc->paddr + ntohl(dbuf.ooffset[j]);
		o->size = ntohl(dbuf.osize[j]);

		if (newbuf != o->buf) {
			/* buffer address has changes */
			new = 1;
			o->gen = ntohl(dbuf.optr[j]) >> DCONS_GEN_SHIFT;
			o->pos = ntohl(dbuf.optr[j]) & DCONS_POS_MASK;
			o->buf = newbuf;
		}

		i = &dc->port[j].i;
		i->size = ntohl(dbuf.isize[j]);
		i->gen = ntohl(dbuf.iptr[j]) >> DCONS_GEN_SHIFT;
		i->pos = ntohl(dbuf.iptr[j]) & DCONS_POS_MASK;
		i->buf = dc->paddr + ntohl(dbuf.ioffset[j]);

		if (verbose) {
			printf("port %d   size offset   gen   pos\n", j);
#if __FreeBSD_version < 500000
			printf("output: %5d %6ld %5d %5d\n"
				"input : %5d %6ld %5d %5d\n",
#else
			printf("output: %5d %6d %5d %5d\n"
				"input : %5d %6d %5d %5d\n",
#endif
			o->size, ntohl(dbuf.ooffset[j]), o->gen, o->pos,
			i->size, ntohl(dbuf.ioffset[j]), i->gen, i->pos);
		}

		if (IS_CONSOLE(&dc->port[j]) && new &&
		    (dc->flags & F_REPLAY) !=0) {
			if (o->gen > 0)
				o->gen --;
			else
				o->pos = 0;
		}
	}
	dconschat_ready(dc, 1, NULL);
	return(0);
}

static int
dconschat_get_ptr (struct dcons_state *dc) {
	int dlen, i;
	u_int32_t ptr[DCONS_NPORT*2+1];
	static int retry = RETRY;
	char ebuf[64];

again:
	dlen = dread(dc, &ptr, sizeof(ptr),
		dc->paddr + __offsetof(struct dcons_buf, magic));

	if (dlen < 0) {
		if (errno == ETIMEDOUT)
			if (retry -- > 0)
				goto again;
		dconschat_ready(dc, 0, "get ptr failed");
		return(-1);
	}
	if (ptr[0] != htonl(DCONS_MAGIC)) {
		if ((dc->flags & F_USE_CROM) !=0)
			dc->paddr = 0;
		snprintf(ebuf, sizeof(ebuf), "wrong magic 0x%08x", ptr[0]);
		dconschat_ready(dc, 0, ebuf);
		return(-1);
	}
	retry = RETRY;
	for (i = 0; i < DCONS_NPORT; i ++) {
		dc->port[i].optr = ntohl(ptr[i + 1]);
		dc->port[i].iptr = ntohl(ptr[DCONS_NPORT + i + 1]);
	}
	return(0);
}

#define MAX_XFER 2048
static int
dconschat_read_dcons(struct dcons_state *dc, int port, char *buf, int len)
{
	struct dcons_ch *ch;
	u_int32_t ptr, pos, gen, next_gen;
	int rlen, dlen, lost;
	int retry = RETRY;

	ch = &dc->port[port].o;
	ptr = dc->port[port].optr;
	gen = ptr >> DCONS_GEN_SHIFT;
	pos = ptr & DCONS_POS_MASK;
	if (gen == ch->gen && pos == ch->pos)
		return (-1);

	next_gen = DCONS_NEXT_GEN(ch->gen);
	/* XXX sanity check */
	if (gen == ch->gen) {
		if (pos > ch->pos)
			goto ok;
		lost = ch->size * DCONS_GEN_MASK - ch->pos;
		ch->pos = 0;
	} else if (gen == next_gen) {
		if (pos <= ch->pos)
			goto ok;
		lost = pos - ch->pos;
		ch->pos = pos;
	} else {
		lost = gen - ch->gen;
		if (lost < 0)
			lost += DCONS_GEN_MASK;
		if (verbose)
			printf("[genskip %d]", lost);
		lost = lost * ch->size - ch->pos;
		ch->pos = 0;
		ch->gen = gen;
	}
	/* generation skipped !! */
	/* XXX discard */
	if (verbose)
		printf("[lost %d]", lost);
ok:
	if (gen == ch->gen)
		rlen = pos - ch->pos;
	else
		rlen = ch->size - ch->pos;

	if (rlen > MAX_XFER)
		rlen = MAX_XFER;
	if (rlen > len)
		rlen = len;

#if 1
	if (verbose == 1)
		printf("[%d]", rlen); fflush(stdout);
#endif

again:
	dlen = dread(dc, buf, rlen, ch->buf + ch->pos);
	if (dlen < 0) {
		if (errno == ETIMEDOUT)
			if (retry -- > 0)
				goto again;
		dconschat_ready(dc, 0, "read buffer failed");
		return(-1);
	}
	if (dlen != rlen)
		warnx("dlen(%d) != rlen(%d)\n", dlen, rlen);
	ch->pos += dlen;
	if (ch->pos >= ch->size) {
		ch->gen = next_gen;
		ch->pos = 0;
		if (verbose)
			printf("read_dcons: gen=%d", ch->gen);
	}
	return (dlen);
}

static int
dconschat_write_dcons(struct dcons_state *dc, int port, char *buf, int blen)
{
	struct dcons_ch *ch;
	u_int32_t ptr;
	int len, wlen;
	int retry = RETRY;

	ch = &dc->port[port].i;
	ptr = dc->port[port].iptr;

	/* the others may advance the pointer sync with it */
	ch->gen = ptr >> DCONS_GEN_SHIFT;
	ch->pos = ptr & DCONS_POS_MASK;

	while(blen > 0) {
		wlen = MIN(blen, ch->size - ch->pos);
		wlen = MIN(wlen, MAX_XFER);
		len = dwrite(dc, buf, wlen, ch->buf + ch->pos);
		if (len < 0) {
			if (errno == ETIMEDOUT)
				if (retry -- > 0)
					continue; /* try again */
			dconschat_ready(dc, 0, "write buffer failed");
			return(-1);
		}
		ch->pos += len;
		buf += len;
		blen -= len;
		if (ch->pos >= ch->size) {
			ch->gen = DCONS_NEXT_GEN(ch->gen);
			ch->pos = 0;
			if (verbose)
				printf("write_dcons: gen=%d", ch->gen);
				
		}
	}

	ptr = DCONS_MAKE_PTR(ch);
	dc->port[port].iptr = ptr;

	if (verbose > 2)
		printf("(iptr: 0x%x)", ptr);
again:
	len = dwrite(dc, &ptr, sizeof(u_int32_t),
		dc->paddr + __offsetof(struct dcons_buf, iptr[port]));
	if (len < 0) {
		if (errno == ETIMEDOUT)
			if (retry -- > 0)
				goto again;
		dconschat_ready(dc, 0, "write ptr failed");
		return(-1);
	}
	return(0);
}


static int
dconschat_write_socket(int fd, char *buf, int len)
{
	write(fd, buf, len);
	if (verbose > 1) {
		buf[len] = 0;
		printf("<- %s\n", buf);
	}
	return (0);
}

static void
dconschat_init_socket(struct dcons_state *dc, int port, char *host, int sport)
{
	struct addrinfo hints, *res;
	int on = 1, error;
	char service[10];
	struct kevent kev;
	struct dcons_port *p;

	p = &dc->port[port];
	p->port = port;
	p->sport = sport;
	p->infd = p->outfd = -1;

	if (sport < 0)
		return;

	if (sport == 0) {

		/* Use stdin and stdout */
		p->infd = STDIN_FILENO;
		p->outfd = STDOUT_FILENO;
		p->s = -1;
		if (tc_set == 0 &&
		    tcgetattr(STDIN_FILENO, &dc->tsave) == 0) {
			dc->traw = dc->tsave;
			cfmakeraw(&dc->traw);
			tcsetattr(STDIN_FILENO, TCSADRAIN, &dc->traw);
			tc_set = 1;
		}
		EV_SET(&kev, p->infd, EVFILT_READ, EV_ADD, NOTE_LOWAT, 1,
		    (void *)p);
		kevent(dc->kq, &kev, 1, NULL, 0, &dc->zero);
		return;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
#if 1	/* gdb can talk v4 only */
	hints.ai_family = PF_INET;
#else
	hints.ai_family = PF_UNSPEC;
#endif
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	if (verbose)
		printf("%s:%d for port %d\n",
			host == NULL ? "*" : host, sport, port);
	snprintf(service, sizeof(service), "%d", sport);
	error = getaddrinfo(host, service,  &hints, &res);
	if (error)
		errx(1, "tcp/%s: %s\n", service, gai_strerror(error));
	p->res = res;
	p->s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (p->s < 0)
		err(1, "socket");
	setsockopt(p->s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	if (bind(p->s, p->res->ai_addr, p->res->ai_addrlen) < 0) {
		err(1, "bind");
	}
	if (listen(p->s, 1) < 0)
		err(1, "listen");
	EV_SET(&kev, p->s, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void *)p);
	error = kevent(dc->kq, &kev, 1, NULL, 0, &dc->to);
	if (error < 0)
		err(1, "kevent");
	return;
}

static int
dconschat_accept_socket(struct dcons_state *dc, struct dcons_port *p)
{
	socklen_t addrlen;
	int ns, flags;
	struct kevent kev;

	/* accept connection */
	addrlen = p->res->ai_addrlen;
	ns = accept(p->s, p->res->ai_addr, &addrlen);
	if (ns < 0)
		err(1, "accept");
	if (verbose)
		printf("port%d accepted\n", p->port);

	flags = fcntl(ns, F_GETFL, 0);
	flags |= O_NDELAY;
	fcntl(ns, F_SETFL, flags);
#if 1
	if (IS_CONSOLE(p) && (dc->flags & F_TELNET) != 0) {
		char sga[] = {IAC, WILL, TELOPT_SGA};
		char linemode[] = {IAC, DONT, TELOPT_LINEMODE};
		char echo[] = {IAC, WILL, TELOPT_ECHO};
		char bin[] = {IAC, DO, TELOPT_BINARY};

		write(ns, sga, sizeof(sga));
		write(ns, linemode, sizeof(linemode));
		write(ns, echo, sizeof(echo));
		write(ns, bin, sizeof(bin));
		p->skip_read = 0;
	}
#endif
	/* discard backlog on GDB port */
	if (IS_GDB(p)) {
		char buf[2048];
		int len;

		while ((len = dconschat_read_dcons(dc, DCONS_GDB, &buf[0],
				 2048)) > 0)
			if (verbose)
				printf("discard %d chars on GDB port\n", len);
	}

	p->infd = p->outfd = ns;
	EV_SET(&kev, ns, EVFILT_READ, EV_ADD, NOTE_LOWAT, 1, (void *)p);
	kevent(dc->kq, &kev, 1, NULL, 0, &dc->zero);
	return(0);
}

static int
dconschat_read_filter(struct dcons_state *dc, struct dcons_port *p,
    u_char *sp, int slen, u_char *dp, int *dlen)
{
	int skip;
	char *buf;

	while (slen > 0) {
		skip = 0;
		if (IS_CONSOLE(p)) {
			if ((dc->flags & F_TELNET) != 0) {
				/* XXX Telnet workarounds */
				if (p->skip_read -- > 0) {
					sp ++;
					slen --;
					continue;
				}
				if (*sp == IAC) {
					if (verbose)
						printf("(IAC)");
					p->skip_read = 2;
					sp ++;
					slen --;
					continue;
				}
				if (*sp == 0) {
					if (verbose)
						printf("(0 stripped)");
					sp ++;
					slen --;
					continue;
				}
			}
			switch (dc->escape_state) {
			case STATE1:
				if (*sp == dc->escape) {
					skip = 1;
					dc->escape_state = STATE2;
				} else
					dc->escape_state = STATE0;
				break;
			case STATE2:
				dc->escape_state = STATE0;
				skip = 1;
				if (*sp == '.')
					dconschat_cleanup(0);
				else if (*sp == CTRL('B')) {
					bcopy(abreak, dp, 3);
					dp += 3;
					*dlen += 3;
				}
				else if (*sp == CTRL('G'))
					dconschat_fork_gdb(dc, p);
				else if ((*sp == CTRL('R'))
						&& (dc->reset != 0)) {
					dc->escape_state = STATE3;
					buf = "\r\n[Are you sure to reset target? (y/N)]";
					write(p->outfd, buf, strlen(buf));
				} else if (*sp == CTRL('Z'))
					dconschat_suspend(dc, p);
				else {
					skip = 0;
					*dp++ = dc->escape;
					(*dlen) ++;
				}
				break;
			case STATE3:
				dc->escape_state = STATE0;
				skip = 1;
				if (*sp == 'y')
					dconschat_reset_target(dc, p);
				else {
					write(p->outfd, sp, 1);
					write(p->outfd, "\r\n", 2);
				}
				break;
			}
			if (*sp == KEY_CR)
				dc->escape_state = STATE1;
		} else if (IS_GDB(p)) {
			/* GDB: ^C -> CR+~+^B */
			if (*sp == CTRL('C') && (dc->flags & F_ALT_BREAK) != 0) {
				bcopy(abreak, dp, 3);
				dp += 3;
				sp ++;
				*dlen += 3;
				/* discard rest of the packet */
				slen = 0;
				break;
			}
		}
		if (!skip) {
			*dp++ = *sp;
			(*dlen) ++;
		}
		sp ++;
		slen --;
	}
	return (*dlen);
			
}

static int
dconschat_read_socket(struct dcons_state *dc, struct dcons_port *p)
{
	struct kevent kev;
	int len, wlen;
	char rbuf[MAX_XFER], wbuf[MAX_XFER+2];

	if ((len = read(p->infd, rbuf, sizeof(rbuf))) > 0) {
		wlen = 0;
		dconschat_read_filter(dc, p, rbuf, len, wbuf, &wlen);
		/* XXX discard if not ready*/
		if (wlen > 0 && (dc->flags & F_READY) != 0) {
			dconschat_write_dcons(dc, p->port, wbuf, wlen);
			if (verbose > 1) {
				wbuf[wlen] = 0;
				printf("-> %s\n", wbuf);
			} else if (verbose == 1) {
				printf("(%d)", wlen);
				fflush(stdout);
			}
		}
	} else {
		if (verbose) {
			if (len == 0)
				warnx("port%d: closed", p->port);
			else
				warn("port%d: read", p->port);
		}
		EV_SET(&kev, p->infd, EVFILT_READ,
			EV_DELETE, 0, 0, NULL);
		kevent(dc->kq, &kev, 1, NULL, 0, &dc->zero);
		close(p->infd);
		close(p->outfd);
		/* XXX exit for pipe case XXX */
		EV_SET(&kev, p->s, EVFILT_READ,
				EV_ADD | EV_ONESHOT, 0, 0, (void *) p);
		kevent(dc->kq, &kev, 1, NULL, 0, &dc->zero);
		p->infd = p->outfd = -1;
	}
	return(0);
}
#define NEVENT 5
static int
dconschat_proc_socket(struct dcons_state *dc)
{
	struct kevent elist[NEVENT], *e;
	int i, n;
	struct dcons_port *p;

	n = kevent(dc->kq, NULL, 0, elist, NEVENT, &dc->to);
	for (i = 0; i < n; i ++) {
		e = &elist[i];
		p = (struct dcons_port *)e->udata;
		if (e->ident == p->s) {
			dconschat_accept_socket(dc, p);
		} else {
			dconschat_read_socket(dc, p);
		}
	}
	return(0);
}

static int
dconschat_proc_dcons(struct dcons_state *dc)
{
	int port, len, err;
	char buf[MAX_XFER];
	struct dcons_port *p;

	err = dconschat_get_ptr(dc);
	if (err) {
		/* XXX we should stop write operation too. */
		return err;
	}
	for (port = 0; port < DCONS_NPORT; port ++) {
		p = &dc->port[port];
		if (p->infd < 0)
			continue;
		while ((len = dconschat_read_dcons(dc, port, buf,
		    sizeof(buf))) > 0) {
			dconschat_write_socket(p->outfd, buf, len);
			if ((err = dconschat_get_ptr(dc)))
				return (err);
		}
		if ((dc->flags & F_ONE_SHOT) != 0 && len <= 0)
			dconschat_cleanup(0);
	}
	return 0;
}

static int
dconschat_start_session(struct dcons_state *dc)
{
	int counter = 0;
	int retry = 0;
	int retry_unit_init = MAX(1, poll_hz / 10);
	int retry_unit_offline = poll_hz * DCONS_POLL_OFFLINE;
	int retry_unit = retry_unit_init;
	int retry_max = retry_unit_offline / retry_unit;

	while (1) {
		if (((dc->flags & F_READY) == 0) && ++counter > retry_unit) {
			counter = 0;
			retry ++;
			if (retry > retry_max)
				retry_unit = retry_unit_offline;
			if (verbose) {
				printf("%d/%d ", retry, retry_max);
				fflush(stdout);
			}
			dconschat_fetch_header(dc);
		}
		if ((dc->flags & F_READY) != 0) {
			counter = 0;
			retry = 0;
			retry_unit = retry_unit_init;
			dconschat_proc_dcons(dc);
		}
		dconschat_proc_socket(dc);
	}
	return (0);
}

static void
usage(void)
{
	fprintf(stderr,
 	    "usage: dconschat [-brvwRT1] [-h hz] [-C port] [-G port]\n"
	    "\t\t\t[-M core] [-N system]\n"
	    "\t\t\t[-u unit] [-a address] [-t target_eui64]\n"
	    "\t-b	translate ctrl-C to CR+~+ctrl-B on gdb port\n"
	    "\t-v	verbose\n"
	    "\t-w	listen on wildcard address rather than localhost\n"
	    "\t-r	replay old buffer on connection\n"
	    "\t-R	read-only\n"
	    "\t-T	enable Telnet protocol workaround on console port\n"
	    "\t-1	one shot: read buffer and exit\n"
	    "\t-h	polling rate\n"
	    "\t-C	port number for console port\n"
	    "\t-G	port number for gdb port\n"
	    "\t(for KVM)\n"
	    "\t-M	core file\n"
	    "\t-N	system file\n"
	    "\t(for FireWire)\n"
	    "\t-u	specify unit number of the bus\n"
	    "\t-t	EUI64 of target host (must be specified)\n"
	    "\t-a	physical address of dcons buffer on target host\n"
	);
	exit(0);
}
int
main(int argc, char **argv)
{
	struct dcons_state *dc;
	struct fw_eui64 eui;
	struct eui64 target;
	char devname[256], *core = NULL, *system = NULL;
	int i, ch, error;
	int unit=0, wildcard=0;
	int port[DCONS_NPORT];

	bzero(&sc, sizeof(sc));
	dc = &sc;
	dc->flags |= USE_CROM ? F_USE_CROM : 0;

	/* default ports */
	port[0] = 0;	/* stdin/out for console */
	port[1] = -1;	/* disable gdb port */

	/* default escape char */
	dc->escape = KEY_TILDE;

	while ((ch = getopt(argc, argv, "a:be:h:rt:u:vwC:G:M:N:RT1")) != -1) {
		switch(ch) {
		case 'a':
			dc->paddr = strtoull(optarg, NULL, 0);
			dc->flags &= ~F_USE_CROM;
			break;
		case 'b':
			dc->flags |= F_ALT_BREAK;
			break;
		case 'e':
			dc->escape = optarg[0];
			break;
		case 'h':
			poll_hz = strtoul(optarg, NULL, 0);
			if (poll_hz == 0)
				poll_hz = DCONS_POLL_HZ;
			break;
		case 'r':
			dc->flags |= F_REPLAY;
			break;
		case 't':
			if (eui64_hostton(optarg, &target) != 0 &&
			    eui64_aton(optarg, &target) != 0)
				errx(1, "invalid target: %s", optarg);
			eui.hi = ntohl(*(u_int32_t*)&(target.octet[0]));
			eui.lo = ntohl(*(u_int32_t*)&(target.octet[4]));
			dc->type = TYPE_FW;
			break;
		case 'u':
			unit = strtol(optarg, NULL, 0);
			break;
		case 'v':
			verbose ++;
			break;
		case 'w':
			wildcard = 1;
			break;
		case 'C':
			port[0] = strtol(optarg, NULL, 0);
			break;
		case 'G':
			port[1] = strtol(optarg, NULL, 0);
			break;
		case 'M':
			core = optarg;
			break;	
		case 'N':
			system = optarg;
			break;	
		case 'R':
			dc->flags |= F_RD_ONLY;
			break;
		case 'T':
			dc->flags |= F_TELNET;
			break;
		case '1':
			dc->flags |= F_ONE_SHOT | F_REPLAY;
			break;
		default:
			usage();
		}
	}
	if (dc->paddr == 0 && (dc->flags & F_USE_CROM) == 0) {
		warnx("no address specified");
		usage();
	}

	if (port[0] < 0 && port[1] < 0) {
		warnx("no port specified");
		usage();
	}

	/* set signal handler */
	signal(SIGHUP, dconschat_cleanup);
	signal(SIGINT, dconschat_cleanup);
	signal(SIGPIPE, dconschat_cleanup);
	signal(SIGTERM, dconschat_cleanup);

	/* init firewire */
	switch (dc->type) {
	case TYPE_FW:
#define MAXDEV 10
		for (i = 0; i < MAXDEV; i ++) {
			snprintf(devname, sizeof(devname),
			    "/dev/fwmem%d.%d", unit, i);
			dc->fd = open(devname, O_RDWR);
			if (dc->fd >= 0)
				goto found;
		}
		err(1, "open");
found:
		error = ioctl(dc->fd, FW_SDEUI64, &eui);
		if (error)
			err(1, "ioctl");
		break;
	case TYPE_KVM:
	{
		struct nlist nl[] = {{"dcons_buf"}, {""}};
		void *dcons_buf;

		dc->kd = kvm_open(system, core, NULL,
		    (dc->flags & F_RD_ONLY) ? O_RDONLY : O_RDWR, "dconschat");
		if (dc->kd == NULL)
			errx(1, "kvm_open");

		if (kvm_nlist(dc->kd, nl) < 0)
			errx(1, "kvm_nlist: %s", kvm_geterr(dc->kd));

		if (kvm_read(dc->kd, nl[0].n_value, &dcons_buf,
		    sizeof(void *)) < 0)
			errx(1, "kvm_read: %s", kvm_geterr(dc->kd));
		dc->paddr = (uintptr_t)dcons_buf;
		if (verbose)
			printf("dcons_buf: 0x%x\n", (uint)dc->paddr);
		break;
	}
	}
	dconschat_fetch_header(dc);

	/* init sockets */
	dc->kq = kqueue();
	if (poll_hz == 1) {
		dc->to.tv_sec = 1;
		dc->to.tv_nsec = 0;
	} else {
		dc->to.tv_sec = 0;
		dc->to.tv_nsec = 1000 * 1000 * 1000 / poll_hz;
	}
	dc->zero.tv_sec = 0;
	dc->zero.tv_nsec = 0;
	for (i = 0; i < DCONS_NPORT; i++)
		dconschat_init_socket(dc, i,
		    wildcard ? NULL : "localhost", port[i]);

	dconschat_start_session(dc);

	for (i = 0; i < DCONS_NPORT; i++) {
		freeaddrinfo(dc->port[i].res);
	}
	return (0);
}
