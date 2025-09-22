/*	$OpenBSD: mproc.c,v 1.47 2024/11/21 13:42:22 claudio Exp $	*/

/*
 * Copyright (c) 2012 Eric Faurot <eric@faurot.net>
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

static void mproc_dispatch(int, short, void *);

int
mproc_fork(struct mproc *p, const char *path, char *argv[])
{
	int sp[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sp) == -1)
		return (-1);

	io_set_nonblocking(sp[0]);
	io_set_nonblocking(sp[1]);

	if ((p->pid = fork()) == -1)
		goto err;

	if (p->pid == 0) {
		/* child process */
		dup2(sp[0], STDIN_FILENO);
		if (closefrom(STDERR_FILENO + 1) == -1)
			exit(1);

		execv(path, argv);
		fatal("execv: %s", path);
	}

	/* parent process */
	close(sp[0]);
	mproc_init(p, sp[1]);
	return (0);

err:
	log_warn("warn: Failed to start process %s, instance of %s", argv[0], path);
	close(sp[0]);
	close(sp[1]);
	return (-1);
}

void
mproc_init(struct mproc *p, int fd)
{
	if (imsgbuf_init(&p->imsgbuf, fd) == -1)
		fatal("mproc_init: imsgbuf_init");
	if (p->proc != PROC_CLIENT)
		imsgbuf_allow_fdpass(&p->imsgbuf);
}

void
mproc_clear(struct mproc *p)
{
	log_debug("debug: clearing p=%s, fd=%d, pid=%d", p->name, p->imsgbuf.fd, p->pid);

	if (p->events)
		event_del(&p->ev);
	close(p->imsgbuf.fd);
	imsgbuf_clear(&p->imsgbuf);
}

void
mproc_enable(struct mproc *p)
{
	if (p->enable == 0) {
		log_trace(TRACE_MPROC, "mproc: %s -> %s: enabled",
		    proc_name(smtpd_process),
		    proc_name(p->proc));
		p->enable = 1;
	}
	mproc_event_add(p);
}

void
mproc_disable(struct mproc *p)
{
	if (p->enable == 1) {
		log_trace(TRACE_MPROC, "mproc: %s -> %s: disabled",
		    proc_name(smtpd_process),
		    proc_name(p->proc));
		p->enable = 0;
	}
	mproc_event_add(p);
}

void
mproc_event_add(struct mproc *p)
{
	short	events;

	if (p->enable)
		events = EV_READ;
	else
		events = 0;

	if (imsgbuf_queuelen(&p->imsgbuf) > 0)
		events |= EV_WRITE;

	if (p->events)
		event_del(&p->ev);

	p->events = events;
	if (events) {
		event_set(&p->ev, p->imsgbuf.fd, events, mproc_dispatch, p);
		event_add(&p->ev, NULL);
	}
}

static void
mproc_dispatch(int fd, short event, void *arg)
{
	struct mproc	*p = arg;
	struct imsg	 imsg;
	ssize_t		 n;

	p->events = 0;

	if (event & EV_READ) {

		n = imsgbuf_read(&p->imsgbuf);

		switch (n) {
		case -1:
			log_warn("warn: %s -> %s: imsgbuf_read",
			    proc_name(smtpd_process),  p->name);
			fatal("exiting");
			/* NOTREACHED */
		case 0:
			/* this pipe is dead, so remove the event handler */
			log_debug("debug: %s -> %s: pipe closed",
			    proc_name(smtpd_process),  p->name);
			p->handler(p, NULL);
			return;
		default:
			break;
		}
	}

	if (event & EV_WRITE) {
		if (imsgbuf_write(&p->imsgbuf) == -1) {
			/* this pipe is dead, so remove the event handler */
			log_debug("debug: %s -> %s: pipe closed",
			    proc_name(smtpd_process),  p->name);
			p->handler(p, NULL);
			return;
		}
	}

	for (;;) {
		if ((n = imsg_get(&p->imsgbuf, &imsg)) == -1) {

			if (smtpd_process == PROC_CONTROL &&
			    p->proc == PROC_CLIENT) {
				log_warnx("warn: client sent invalid imsg "
				    "over control socket");
				p->handler(p, NULL);
				return;
			}
			log_warn("fatal: %s: error in imsg_get for %s",
			    proc_name(smtpd_process),  p->name);
			fatalx(NULL);
		}
		if (n == 0)
			break;

		p->handler(p, &imsg);

		imsg_free(&imsg);
	}

	mproc_event_add(p);
}

void
m_forward(struct mproc *p, struct imsg *imsg)
{
	imsg_compose(&p->imsgbuf, imsg->hdr.type, imsg->hdr.peerid,
	    imsg->hdr.pid, imsg_get_fd(imsg), imsg->data,
	    imsg->hdr.len - sizeof(imsg->hdr));

	if (imsg->hdr.type != IMSG_STAT_DECREMENT &&
	    imsg->hdr.type != IMSG_STAT_INCREMENT)
		log_trace(TRACE_MPROC, "mproc: %s -> %s : %zu %s (forward)",
		    proc_name(smtpd_process),
		    proc_name(p->proc),
		    imsg->hdr.len - sizeof(imsg->hdr),
		    imsg_to_str(imsg->hdr.type));

	mproc_event_add(p);
}

void
m_compose(struct mproc *p, uint32_t type, uint32_t peerid, pid_t pid, int fd,
    void *data, size_t len)
{
	imsg_compose(&p->imsgbuf, type, peerid, pid, fd, data, len);

	if (type != IMSG_STAT_DECREMENT &&
	    type != IMSG_STAT_INCREMENT)
		log_trace(TRACE_MPROC, "mproc: %s -> %s : %zu %s",
		    proc_name(smtpd_process),
		    proc_name(p->proc),
		    len,
		    imsg_to_str(type));

	mproc_event_add(p);
}

void
m_composev(struct mproc *p, uint32_t type, uint32_t peerid, pid_t pid,
    int fd, const struct iovec *iov, int n)
{
	size_t	len;
	int	i;

	imsg_composev(&p->imsgbuf, type, peerid, pid, fd, iov, n);

	len = 0;
	for (i = 0; i < n; i++)
		len += iov[i].iov_len;

	if (type != IMSG_STAT_DECREMENT &&
	    type != IMSG_STAT_INCREMENT)
		log_trace(TRACE_MPROC, "mproc: %s -> %s : %zu %s",
		    proc_name(smtpd_process),
		    proc_name(p->proc),
		    len,
		    imsg_to_str(type));

	mproc_event_add(p);
}

void
m_create(struct mproc *p, uint32_t type, uint32_t peerid, pid_t pid, int fd)
{
	p->m_pos = 0;
	p->m_type = type;
	p->m_peerid = peerid;
	p->m_pid = pid;
	p->m_fd = fd;
}

void
m_add(struct mproc *p, const void *data, size_t len)
{
	size_t	 alloc;
	void	*tmp;

	if (p->m_pos + len + IMSG_HEADER_SIZE > MAX_IMSGSIZE) {
		log_warnx("warn: message too large");
		fatal(NULL);
	}

	alloc = p->m_alloc ? p->m_alloc : 128;
	while (p->m_pos + len > alloc)
		alloc *= 2;
	if (alloc != p->m_alloc) {
		log_trace(TRACE_MPROC, "mproc: %s -> %s: realloc %zu -> %zu",
		    proc_name(smtpd_process),
		    proc_name(p->proc),
		    p->m_alloc,
		    alloc);

		tmp = recallocarray(p->m_buf, p->m_alloc, alloc, 1);
		if (tmp == NULL)
			fatal("realloc");
		p->m_alloc = alloc;
		p->m_buf = tmp;
	}

	memmove(p->m_buf + p->m_pos, data, len);
	p->m_pos += len;
}

void
m_close(struct mproc *p)
{
	if (imsg_compose(&p->imsgbuf, p->m_type, p->m_peerid, p->m_pid, p->m_fd,
	    p->m_buf, p->m_pos) == -1)
		fatal("imsg_compose");

	log_trace(TRACE_MPROC, "mproc: %s -> %s : %zu %s",
		    proc_name(smtpd_process),
		    proc_name(p->proc),
		    p->m_pos,
		    imsg_to_str(p->m_type));

	mproc_event_add(p);
}

void
m_flush(struct mproc *p)
{
	if (imsg_compose(&p->imsgbuf, p->m_type, p->m_peerid, p->m_pid, p->m_fd,
	    p->m_buf, p->m_pos) == -1)
		fatal("imsg_compose");

	log_trace(TRACE_MPROC, "mproc: %s -> %s : %zu %s (flush)",
	    proc_name(smtpd_process),
	    proc_name(p->proc),
	    p->m_pos,
	    imsg_to_str(p->m_type));

	p->m_pos = 0;

	if (imsgbuf_flush(&p->imsgbuf) == -1)
		fatal("imsgbuf_flush");
}

static struct imsg * current;

static void
m_error(const char *error)
{
	char	buf[512];

	(void)snprintf(buf, sizeof buf, "%s: %s: %s",
	    proc_name(smtpd_process),
	    imsg_to_str(current->hdr.type),
	    error);
	fatalx("%s", buf);
}

void
m_msg(struct msg *m, struct imsg *imsg)
{
	current = imsg;
	m->pos = imsg->data;
	m->end = m->pos + (imsg->hdr.len - sizeof(imsg->hdr));
}

void
m_end(struct msg *m)
{
	if (m->pos != m->end)
		m_error("not at msg end");
}

int
m_is_eom(struct msg *m)
{
	return (m->pos == m->end);
}

static inline void
m_get(struct msg *m, void *dst, size_t sz)
{
	if (sz > MAX_IMSGSIZE ||
	    m->end - m->pos < (ssize_t)sz)
		fatalx("msg too short");

	memmove(dst, m->pos, sz);
	m->pos += sz;
}

void
m_add_int(struct mproc *m, int v)
{
	m_add(m, &v, sizeof(v));
};

void
m_add_u32(struct mproc *m, uint32_t u32)
{
	m_add(m, &u32, sizeof(u32));
};

void
m_add_size(struct mproc *m, size_t sz)
{
	m_add(m, &sz, sizeof(sz));
};

void
m_add_time(struct mproc *m, time_t v)
{
	m_add(m, &v, sizeof(v));
};

void
m_add_timeval(struct mproc *m, struct timeval *tv)
{
	m_add(m, tv, sizeof(*tv));
}


void
m_add_string(struct mproc *m, const char *v)
{
	if (v) {
		m_add(m, "s", 1);
		m_add(m, v, strlen(v) + 1);
	}
	else
		m_add(m, "\0", 1);
};

void
m_add_data(struct mproc *m, const void *v, size_t len)
{
	m_add_size(m, len);
	m_add(m, v, len);
};

void
m_add_id(struct mproc *m, uint64_t v)
{
	m_add(m, &v, sizeof(v));
}

void
m_add_evpid(struct mproc *m, uint64_t v)
{
	m_add(m, &v, sizeof(v));
}

void
m_add_msgid(struct mproc *m, uint32_t v)
{
	m_add(m, &v, sizeof(v));
}

void
m_add_sockaddr(struct mproc *m, const struct sockaddr *sa)
{
	m_add_size(m, sa->sa_len);
	m_add(m, sa, sa->sa_len);
}

void
m_add_mailaddr(struct mproc *m, const struct mailaddr *maddr)
{
	m_add(m, maddr, sizeof(*maddr));
}

void
m_add_envelope(struct mproc *m, const struct envelope *evp)
{
	char	buf[sizeof(*evp)];

	envelope_dump_buffer(evp, buf, sizeof(buf));
	m_add_evpid(m, evp->id);
	m_add_string(m, buf);
}

void
m_add_params(struct mproc *m, struct dict *d)
{
	const char *key;
	char *value;
	void *iter;

	if (d == NULL) {
		m_add_size(m, 0);
		return;
	}
	m_add_size(m, dict_count(d));
	iter = NULL;
	while (dict_iter(d, &iter, &key, (void **)&value)) {
		m_add_string(m, key);
		m_add_string(m, value);
	}
}

void
m_get_int(struct msg *m, int *i)
{
	m_get(m, i, sizeof(*i));
}

void
m_get_u32(struct msg *m, uint32_t *u32)
{
	m_get(m, u32, sizeof(*u32));
}

void
m_get_size(struct msg *m, size_t *sz)
{
	m_get(m, sz, sizeof(*sz));
}

void
m_get_time(struct msg *m, time_t *t)
{
	m_get(m, t, sizeof(*t));
}

void
m_get_timeval(struct msg *m, struct timeval *tv)
{
	m_get(m, tv, sizeof(*tv));
}

void
m_get_string(struct msg *m, const char **s)
{
	uint8_t	*end;
	char c;

	if (m->pos >= m->end)
		m_error("msg too short");

	c = *m->pos++;
	if (c == '\0') {
		*s = NULL;
		return;
	}

	if (m->pos >= m->end)
		m_error("msg too short");
	end = memchr(m->pos, 0, m->end - m->pos);
	if (end == NULL)
		m_error("unterminated string");

	*s = m->pos;
	m->pos = end + 1;
}

void
m_get_data(struct msg *m, const void **data, size_t *sz)
{
	m_get_size(m, sz);

	if (*sz == 0) {
		*data = NULL;
		return;
	}

	if (m->pos + *sz > m->end)
		m_error("msg too short");

	*data = m->pos;
	m->pos += *sz;
}

void
m_get_evpid(struct msg *m, uint64_t *evpid)
{
	m_get(m, evpid, sizeof(*evpid));
}

void
m_get_msgid(struct msg *m, uint32_t *msgid)
{
	m_get(m, msgid, sizeof(*msgid));
}

void
m_get_id(struct msg *m, uint64_t *id)
{
	m_get(m, id, sizeof(*id));
}

void
m_get_sockaddr(struct msg *m, struct sockaddr *sa)
{
	size_t len;

	m_get_size(m, &len);
	m_get(m, sa, len);
}

void
m_get_mailaddr(struct msg *m, struct mailaddr *maddr)
{
	m_get(m, maddr, sizeof(*maddr));
}

void
m_get_envelope(struct msg *m, struct envelope *evp)
{
	uint64_t	 evpid;
	const char	*buf;

	m_get_evpid(m, &evpid);
	m_get_string(m, &buf);
	if (buf == NULL)
		fatalx("empty envelope buffer");

	if (!envelope_load_buffer(evp, buf, strlen(buf)))
		fatalx("failed to retrieve envelope");
	evp->id = evpid;
}

void
m_get_params(struct msg *m, struct dict *d)
{
	size_t	c;
	const char *key;
	const char *value;
	char *tmp;

	dict_init(d);

	m_get_size(m, &c);

	for (; c; c--) {
		m_get_string(m, &key);
		m_get_string(m, &value);
		if ((tmp = strdup(value)) == NULL)
			fatal("m_get_params");
		dict_set(d, key, tmp);
	}
}

void
m_clear_params(struct dict *d)
{
	char *value;

	while (dict_poproot(d, (void **)&value))
		free(value);
}
