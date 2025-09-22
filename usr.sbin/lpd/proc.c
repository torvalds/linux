/*	$OpenBSD: proc.c,v 1.7 2024/11/21 13:34:51 claudio Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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
#include <sys/socket.h>

#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "proc.h"

struct imsgproc {
	TAILQ_ENTRY(imsgproc) tqe;
	int		 type;
	int		 instance;
	char		*title;
	pid_t		 pid;
	void		*arg;
	void		(*cb)(struct imsgproc *, struct imsg *, void *);
	struct imsgbuf	 imsgbuf;
	short		 events;
	struct event	 ev;

	struct {
		const uint8_t	*pos;
		const uint8_t	*end;
	} m_in;

	struct m_out {
		char		*buf;
		size_t		 alloc;
		size_t		 pos;
		uint32_t	 type;
		uint32_t	 peerid;
		pid_t		 pid;
		int		 fd;
	} m_out;
};

static struct imsgproc *proc_new(int);
static void proc_setsock(struct imsgproc *, int);
static void proc_callback(struct imsgproc *, struct imsg *);
static void proc_dispatch(int, short, void *);
static void proc_event_add(struct imsgproc *);

static TAILQ_HEAD(, imsgproc) procs = TAILQ_HEAD_INITIALIZER(procs);

pid_t
proc_getpid(struct imsgproc *p)
{
	return p->pid;
}

int
proc_gettype(struct imsgproc *p)
{
	return p->type;
}

int
proc_getinstance(struct imsgproc *p)
{
	return p->instance;
}

const char *
proc_gettitle(struct imsgproc *p)
{
	return p->title;
}

struct imsgproc *
proc_bypid(pid_t pid)
{
	struct imsgproc *p;

	TAILQ_FOREACH(p, &procs, tqe)
		if (pid == p->pid)
			return p;

	return NULL;
}

struct imsgproc *
proc_exec(int type, char **argv)
{
	struct imsgproc *p;
	int sp[2];
	pid_t pid;

	p = proc_new(type);
	if (p == NULL) {
		log_warn("%s: proc_new", __func__);
		return NULL;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, PF_UNSPEC, sp) == -1) {
		log_warn("%s: socketpair", __func__);
		proc_free(p);
		return NULL;
	}

	switch (pid = fork()) {
	case -1:
		log_warn("%s: fork", __func__);
		close(sp[0]);
		close(sp[1]);
		proc_free(p);
		return NULL;
	case 0:
		break;
	default:
		close(sp[0]);
		p->pid = pid;
		proc_setsock(p, sp[1]);
		return p;
	}

	if (dup2(sp[0], 3) == -1)
		fatal("%s: dup2", __func__);

	if (closefrom(4) == -1)
		fatal("%s: closefrom", __func__);

	execvp(argv[0], argv);
	fatal("%s: execvp: %s", __func__, argv[0]);
}

struct imsgproc *
proc_attach(int type, int fd)
{
	struct imsgproc *p;

	p = proc_new(type);
	if (p == NULL)
		return NULL;

	proc_setsock(p, fd);
	return p;
}

void
proc_settitle(struct imsgproc *p, const char *title)
{
	free(p->title);
	if (title) {
		p->title = strdup(title);
		if (p->title == NULL)
			log_warn("%s: strdup", __func__);
	}
	else
		p->title = NULL;
}

void
proc_setpid(struct imsgproc *p, pid_t pid)
{
	p->pid = pid;
}

void
proc_setcallback(struct imsgproc *p,
    void(*cb)(struct imsgproc *, struct imsg *, void *), void *arg)
{
	p->cb = cb;
	p->arg = arg;
}

void
proc_enable(struct imsgproc *p)
{
	proc_event_add(p);
}

void
proc_free(struct imsgproc *p)
{
	if (p == NULL)
		return;

	TAILQ_REMOVE(&procs, p, tqe);

	if (event_initialized(&p->ev))
		event_del(&p->ev);
	close(p->imsgbuf.fd);
	imsgbuf_clear(&p->imsgbuf);
	free(p->title);
	free(p);
}

static struct imsgproc *
proc_new(int type)
{
	struct imsgproc *p;

	p = calloc(1, sizeof(*p));
	if (p == NULL)
		return NULL;

	if (imsgbuf_init(&p->imsgbuf, -1) == -1) {
		free(p);
		return NULL;
	}
	imsgbuf_allow_fdpass(&p->imsgbuf);

	p->type = type;
	p->instance = -1;
	p->pid = -1;

	TAILQ_INSERT_TAIL(&procs, p, tqe);

	return p;
}

static void
proc_setsock(struct imsgproc *p, int sock)
{
	p->imsgbuf.fd = sock;
}

static void
proc_event_add(struct imsgproc *p)
{
	short	events;

	events = EV_READ;
	if (imsgbuf_queuelen(&p->imsgbuf) > 0)
		events |= EV_WRITE;

	if (p->events)
		event_del(&p->ev);

	p->events = events;
	if (events) {
		event_set(&p->ev, p->imsgbuf.fd, events, proc_dispatch, p);
		event_add(&p->ev, NULL);
	}
}

static void
proc_callback(struct imsgproc *p, struct imsg *imsg)
{
	if (imsg != NULL) {
		p->m_in.pos = imsg->data;
		p->m_in.end = p->m_in.pos + (imsg->hdr.len - sizeof(imsg->hdr));
	}
	else {
		p->m_in.pos = NULL;
		p->m_in.end = NULL;
	}

	p->cb(p, imsg, p->arg);
}

static void
proc_dispatch(int fd, short event, void *arg)
{
	struct imsgproc	*p = arg;
	struct imsg	 imsg;
	ssize_t		 n;

	p->events = 0;

	if (event & EV_READ) {
		n = imsgbuf_read(&p->imsgbuf);
		switch (n) {
		case -1:
			log_warn("%s: imsgbuf_read", __func__);
			proc_callback(p, NULL);
			return;
		case 0:
			/* This pipe is dead. */
			proc_callback(p, NULL);
			return;
		default:
			break;
		}
	}

	if (event & EV_WRITE) {
		if (imsgbuf_write(&p->imsgbuf) == -1) {
			if (errno != EPIPE)
				log_warn("%s: imsgbuf_write", __func__);
			proc_callback(p, NULL);
			return;
		}
	}

	for (;;) {
		if ((n = imsg_get(&p->imsgbuf, &imsg)) == -1) {
			log_warn("%s: imsg_get", __func__);
			proc_callback(p, NULL);
			return;
		}
		if (n == 0)
			break;

		proc_callback(p, &imsg);
		imsg_free(&imsg);
	}

	proc_event_add(p);
}

void
m_compose(struct imsgproc *p, uint32_t type, uint32_t peerid, pid_t pid, int fd,
    const void *data, size_t len)
{
	if (imsg_compose(&p->imsgbuf, type, peerid, pid, fd, data, len) == -1)
		fatal("%s: imsg_compose", __func__);

	proc_event_add(p);
}

void
m_create(struct imsgproc *p, uint32_t type, uint32_t peerid, pid_t pid, int fd)
{
	p->m_out.pos = 0;
	p->m_out.type = type;
	p->m_out.peerid = peerid;
	p->m_out.pid = pid;
	p->m_out.fd = fd;
}

void
m_close(struct imsgproc *p)
{
	if (imsg_compose(&p->imsgbuf, p->m_out.type, p->m_out.peerid,
	    p->m_out.pid, p->m_out.fd, p->m_out.buf, p->m_out.pos) == -1)
		fatal("%s: imsg_compose", __func__);

	proc_event_add(p);
}

void
m_add(struct imsgproc *p, const void *data, size_t len)
{
	size_t	 alloc;
	void	*tmp;

	if (p->m_out.pos + len + IMSG_HEADER_SIZE > MAX_IMSGSIZE)
		fatalx("%s: message too large", __func__);

	alloc = p->m_out.alloc ? p->m_out.alloc : 128;
	while (p->m_out.pos + len > alloc)
		alloc *= 2;
	if (alloc != p->m_out.alloc) {
		tmp = recallocarray(p->m_out.buf, p->m_out.alloc, alloc, 1);
		if (tmp == NULL)
			fatal("%s: reallocarray", __func__);
		p->m_out.alloc = alloc;
		p->m_out.buf = tmp;
	}

	memmove(p->m_out.buf + p->m_out.pos, data, len);
	p->m_out.pos += len;
}

void
m_add_int(struct imsgproc *p, int v)
{
	m_add(p, &v, sizeof(v));
};

void
m_add_u32(struct imsgproc *p, uint32_t v)
{
	m_add(p, &v, sizeof(v));
};

void
m_add_u64(struct imsgproc *p, uint64_t v)
{
	m_add(p, &v, sizeof(v));
}

void
m_add_size(struct imsgproc *p, size_t v)
{
	m_add(p, &v, sizeof(v));
}

void
m_add_time(struct imsgproc *p, time_t v)
{
	m_add(p, &v, sizeof(v));
}

void
m_add_string(struct imsgproc *p, const char *str)
{
	if (str) {
		m_add(p, "s", 1);
		m_add(p, str, strlen(str) + 1);
	}
	else
		m_add(p, "\0", 1);
}

void
m_add_sockaddr(struct imsgproc *p, const struct sockaddr *sa)
{
	m_add_size(p, sa->sa_len);
	m_add(p, sa, sa->sa_len);
}

void
m_end(struct imsgproc *p)
{
	if (p->m_in.pos != p->m_in.end)
		fatal("%s: %zi bytes left", __func__,
		    p->m_in.end - p->m_in.pos);
}

int
m_is_eom(struct imsgproc *p)
{
	return (p->m_in.pos == p->m_in.end);
}

void
m_get(struct imsgproc *p, void *dst, size_t sz)
{
	if (sz > MAX_IMSGSIZE ||
	    p->m_in.end - p->m_in.pos < (ssize_t)sz )
		fatalx("%s: %zu bytes requested, %zi left", __func__, sz,
		    p->m_in.end - p->m_in.pos);

	memmove(dst, p->m_in.pos, sz);
	p->m_in.pos += sz;
}

void
m_get_int(struct imsgproc *p, int *dst)
{
	m_get(p, dst, sizeof(*dst));
}

void
m_get_u32(struct imsgproc *p, uint32_t *dst)
{
	m_get(p, dst, sizeof(*dst));
}

void
m_get_u64(struct imsgproc *p, uint64_t *dst)
{
	m_get(p, dst, sizeof(*dst));
}

void
m_get_size(struct imsgproc *p, size_t *dst)
{
	m_get(p, dst, sizeof(*dst));
}

void
m_get_time(struct imsgproc *p, time_t *dst)
{
	m_get(p, dst, sizeof(*dst));
}

void
m_get_string(struct imsgproc *p, const char **dst)
{
	char *end, c;

	if (p->m_in.pos >= p->m_in.end)
		fatalx("%s: no data left", __func__);

	c = *p->m_in.pos++;
	if (c == '\0') {
		*dst = NULL;
		return;
	}

	if (p->m_in.pos >= p->m_in.end)
		fatalx("%s: no data left", __func__);
	end = memchr(p->m_in.pos, 0, p->m_in.end - p->m_in.pos);
	if (end == NULL)
		fatalx("%s: unterminated string", __func__);

	*dst = p->m_in.pos;
	p->m_in.pos = end + 1;
}

void
m_get_sockaddr(struct imsgproc *p, struct sockaddr *dst)
{
	size_t len;

	m_get_size(p, &len);
	m_get(p, dst, len);
}
