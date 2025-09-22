/*	$OpenBSD: scheduler_proc.c,v 1.13 2024/11/21 13:42:22 claudio Exp $	*/

/*
 * Copyright (c) 2013 Eric Faurot <eric@openbsd.org>
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
#include <string.h>

#include "smtpd.h"
#include "log.h"

static struct imsgbuf	 ibuf;
static struct imsg	 imsg;
static size_t		 rlen;
static char		*rdata;

static void
scheduler_proc_call(void)
{
	ssize_t	n;

	if (imsgbuf_flush(&ibuf) == -1) {
		log_warn("warn: scheduler-proc: imsgbuf_flush");
		fatalx("scheduler-proc: exiting");
	}

	while (1) {
		if ((n = imsg_get(&ibuf, &imsg)) == -1) {
			log_warn("warn: scheduler-proc: imsg_get");
			break;
		}
		if (n) {
			rlen = imsg.hdr.len - IMSG_HEADER_SIZE;
			rdata = imsg.data;

			if (imsg.hdr.type != PROC_SCHEDULER_OK) {
				log_warnx("warn: scheduler-proc: bad response");
				break;
			}
			return;
		}

		if ((n = imsgbuf_read(&ibuf)) == -1) {
			log_warn("warn: scheduler-proc: imsgbuf_read");
			break;
		}

		if (n == 0) {
			log_warnx("warn: scheduler-proc: pipe closed");
			break;
		}
	}

	fatalx("scheduler-proc: exiting");
}

static void
scheduler_proc_read(void *dst, size_t len)
{
	if (len > rlen) {
		log_warnx("warn: scheduler-proc: bad msg len");
		fatalx("scheduler-proc: exiting");
	}

	memmove(dst, rdata, len);
	rlen -= len;
	rdata += len;
}

static void
scheduler_proc_end(void)
{
	if (rlen) {
		log_warnx("warn: scheduler-proc: bogus data");
		fatalx("scheduler-proc: exiting");
	}
	imsg_free(&imsg);
}

/*
 * API
 */

static int
scheduler_proc_init(const char *conf)
{
	int		fd, r;
	uint32_t	version;

	fd = fork_proc_backend("scheduler", conf, "scheduler-proc", 0);
	if (fd == -1)
		fatalx("scheduler-proc: exiting");

	if (imsgbuf_init(&ibuf, fd) == -1)
		fatal("scheduler-proc: exiting");
	imsgbuf_allow_fdpass(&ibuf);

	version = PROC_SCHEDULER_API_VERSION;
	imsg_compose(&ibuf, PROC_SCHEDULER_INIT, 0, 0, -1,
	    &version, sizeof(version));
	scheduler_proc_call();
	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (1);
}

static int
scheduler_proc_insert(struct scheduler_info *si)
{
	int	r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_INSERT");

	imsg_compose(&ibuf, PROC_SCHEDULER_INSERT, 0, 0, -1, si, sizeof(*si));

	scheduler_proc_call();
	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

static size_t
scheduler_proc_commit(uint32_t msgid)
{
	size_t	s;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_COMMIT");

	imsg_compose(&ibuf, PROC_SCHEDULER_COMMIT, 0, 0, -1,
	    &msgid, sizeof(msgid));

	scheduler_proc_call();
	scheduler_proc_read(&s, sizeof(s));
	scheduler_proc_end();

	return (s);
}

static size_t
scheduler_proc_rollback(uint32_t msgid)
{
	size_t	s;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_ROLLBACK");

	imsg_compose(&ibuf, PROC_SCHEDULER_ROLLBACK, 0, 0, -1,
	    &msgid, sizeof(msgid));

	scheduler_proc_call();
	scheduler_proc_read(&s, sizeof(s));
	scheduler_proc_end();

	return (s);
}

static int
scheduler_proc_update(struct scheduler_info *si)
{
	int	r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_UPDATE");

	imsg_compose(&ibuf, PROC_SCHEDULER_UPDATE, 0, 0, -1, si, sizeof(*si));

	scheduler_proc_call();
	scheduler_proc_read(&r, sizeof(r));
	if (r == 1)
		scheduler_proc_read(si, sizeof(*si));
	scheduler_proc_end();

	return (r);
}

static int
scheduler_proc_delete(uint64_t evpid)
{
	int	r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_DELETE");

	imsg_compose(&ibuf, PROC_SCHEDULER_DELETE, 0, 0, -1,
	    &evpid, sizeof(evpid));

	scheduler_proc_call();
	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

static int
scheduler_proc_hold(uint64_t evpid, uint64_t holdq)
{
	struct ibuf	*buf;
	int		 r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_HOLD");

	buf = imsg_create(&ibuf, PROC_SCHEDULER_HOLD, 0, 0,
	    sizeof(evpid) + sizeof(holdq));
	if (buf == NULL)
		return (-1);
	if (imsg_add(buf, &evpid, sizeof(evpid)) == -1)
		return (-1);
	if (imsg_add(buf, &holdq, sizeof(holdq)) == -1)
		return (-1);
	imsg_close(&ibuf, buf);

	scheduler_proc_call();

	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

static int
scheduler_proc_release(int type, uint64_t holdq, int n)
{
	struct ibuf	*buf;
	int		 r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_RELEASE");

	buf = imsg_create(&ibuf, PROC_SCHEDULER_RELEASE, 0, 0,
	    sizeof(holdq) + sizeof(n));
	if (buf == NULL)
		return (-1);
	if (imsg_add(buf, &type, sizeof(type)) == -1)
		return (-1);
	if (imsg_add(buf, &holdq, sizeof(holdq)) == -1)
		return (-1);
	if (imsg_add(buf, &n, sizeof(n)) == -1)
		return (-1);
	imsg_close(&ibuf, buf);

	scheduler_proc_call();

	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

static int
scheduler_proc_batch(int typemask, int *delay, size_t *count, uint64_t *evpids, int *types)
{
	struct ibuf	*buf;
	int		 r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_BATCH");

	buf = imsg_create(&ibuf, PROC_SCHEDULER_BATCH, 0, 0,
	    sizeof(typemask) + sizeof(*count));
	if (buf == NULL)
		return (-1);
	if (imsg_add(buf, &typemask, sizeof(typemask)) == -1)
		return (-1);
	if (imsg_add(buf, count, sizeof(*count)) == -1)
		return (-1);
	imsg_close(&ibuf, buf);

	scheduler_proc_call();
	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_read(delay, sizeof(*delay));
	scheduler_proc_read(count, sizeof(*count));
	if (r > 0) {
		scheduler_proc_read(evpids, sizeof(*evpids) * (*count));
		scheduler_proc_read(types, sizeof(*types) * (*count));
	}
	scheduler_proc_end();

	return (r);
}

static size_t
scheduler_proc_messages(uint32_t from, uint32_t *dst, size_t size)
{
	struct ibuf	*buf;
	size_t		 s;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_MESSAGES");

	buf = imsg_create(&ibuf, PROC_SCHEDULER_MESSAGES, 0, 0,
	    sizeof(from) + sizeof(size));
	if (buf == NULL)
		return (-1);
	if (imsg_add(buf, &from, sizeof(from)) == -1)
		return (-1);
	if (imsg_add(buf, &size, sizeof(size)) == -1)
		return (-1);
	imsg_close(&ibuf, buf);

	scheduler_proc_call();

	s = rlen / sizeof(*dst);
	scheduler_proc_read(dst, s * sizeof(*dst));
	scheduler_proc_end();

	return (s);
}

static size_t
scheduler_proc_envelopes(uint64_t from, struct evpstate *dst, size_t size)
{
	struct ibuf	*buf;
	size_t		 s;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_ENVELOPES");

	buf = imsg_create(&ibuf, PROC_SCHEDULER_ENVELOPES, 0, 0,
	    sizeof(from) + sizeof(size));
	if (buf == NULL)
		return (-1);
	if (imsg_add(buf, &from, sizeof(from)) == -1)
		return (-1);
	if (imsg_add(buf, &size, sizeof(size)) == -1)
		return (-1);
	imsg_close(&ibuf, buf);

	scheduler_proc_call();

	s = rlen / sizeof(*dst);
	scheduler_proc_read(dst, s * sizeof(*dst));
	scheduler_proc_end();

	return (s);
}

static int
scheduler_proc_schedule(uint64_t evpid)
{
	int	r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_SCHEDULE");

	imsg_compose(&ibuf, PROC_SCHEDULER_SCHEDULE, 0, 0, -1,
	    &evpid, sizeof(evpid));

	scheduler_proc_call();

	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

static int
scheduler_proc_remove(uint64_t evpid)
{
	int	r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_REMOVE");

	imsg_compose(&ibuf, PROC_SCHEDULER_REMOVE, 0, 0, -1,
	    &evpid, sizeof(evpid));

	scheduler_proc_call();

	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

static int
scheduler_proc_suspend(uint64_t evpid)
{
	int	r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_SUSPEND");

	imsg_compose(&ibuf, PROC_SCHEDULER_SUSPEND, 0, 0, -1,
	    &evpid, sizeof(evpid));

	scheduler_proc_call();

	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

static int
scheduler_proc_resume(uint64_t evpid)
{
	int	r;

	log_debug("debug: scheduler-proc: PROC_SCHEDULER_RESUME");

	imsg_compose(&ibuf, PROC_SCHEDULER_RESUME, 0, 0, -1,
	    &evpid, sizeof(evpid));

	scheduler_proc_call();

	scheduler_proc_read(&r, sizeof(r));
	scheduler_proc_end();

	return (r);
}

struct scheduler_backend scheduler_backend_proc = {
	scheduler_proc_init,
	scheduler_proc_insert,
	scheduler_proc_commit,
	scheduler_proc_rollback,
	scheduler_proc_update,
	scheduler_proc_delete,
	scheduler_proc_hold,
	scheduler_proc_release,
	scheduler_proc_batch,
	scheduler_proc_messages,
	scheduler_proc_envelopes,
	scheduler_proc_schedule,
	scheduler_proc_remove,
	scheduler_proc_suspend,
	scheduler_proc_resume,
};
