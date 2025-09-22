/*	$OpenBSD: queue_proc.c,v 1.14 2024/11/21 13:42:22 claudio Exp $	*/

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
#include <fcntl.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

static struct imsgbuf	 ibuf;
static struct imsg	 imsg;
static size_t		 rlen;
static char		*rdata;

static void
queue_proc_call(void)
{
	ssize_t	n;

	if (imsgbuf_flush(&ibuf) == -1) {
		log_warn("warn: queue-proc: imsgbuf_flush");
		fatalx("queue-proc: exiting");
	}

	while (1) {
		if ((n = imsg_get(&ibuf, &imsg)) == -1) {
			log_warn("warn: queue-proc: imsg_get");
			break;
		}
		if (n) {
			rlen = imsg.hdr.len - IMSG_HEADER_SIZE;
			rdata = imsg.data;

			if (imsg.hdr.type != PROC_QUEUE_OK) {
				log_warnx("warn: queue-proc: bad response");
				break;
			}
			return;
		}

		if ((n = imsgbuf_read(&ibuf)) == -1) {
			log_warn("warn: queue-proc: imsgbuf_read");
			break;
		}

		if (n == 0) {
			log_warnx("warn: queue-proc: pipe closed");
			break;
		}
	}

	fatalx("queue-proc: exiting");
}

static void
queue_proc_read(void *dst, size_t len)
{
	if (len > rlen) {
		log_warnx("warn: queue-proc: bad msg len");
		fatalx("queue-proc: exiting");
	}

	memmove(dst, rdata, len);
	rlen -= len;
	rdata += len;
}

static void
queue_proc_end(void)
{
	if (rlen) {
		log_warnx("warn: queue-proc: bogus data");
		fatalx("queue-proc: exiting");
	}
	imsg_free(&imsg);
}

/*
 * API
 */

static int
queue_proc_close(void)
{
	int	r;

	imsg_compose(&ibuf, PROC_QUEUE_CLOSE, 0, 0, -1, NULL, 0);

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));
	queue_proc_end();

	return (r);
}

static int
queue_proc_message_create(uint32_t *msgid)
{
	int	r;

	imsg_compose(&ibuf, PROC_QUEUE_MESSAGE_CREATE, 0, 0, -1, NULL, 0);

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));
	if (r == 1)
		queue_proc_read(msgid, sizeof(*msgid));
	queue_proc_end();

	return (r);
}

static int
queue_proc_message_commit(uint32_t msgid, const char *path)
{
	int	r, fd;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		log_warn("queue-proc: open: %s", path);
		return (0);
	}

	imsg_compose(&ibuf, PROC_QUEUE_MESSAGE_COMMIT, 0, 0, fd, &msgid,
	    sizeof(msgid));

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));
	queue_proc_end();

	return (r);
}

static int
queue_proc_message_delete(uint32_t msgid)
{
	int	r;

	imsg_compose(&ibuf, PROC_QUEUE_MESSAGE_DELETE, 0, 0, -1, &msgid,
	    sizeof(msgid));

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));
	queue_proc_end();

	return (r);
}

static int
queue_proc_message_fd_r(uint32_t msgid)
{
	imsg_compose(&ibuf, PROC_QUEUE_MESSAGE_FD_R, 0, 0, -1, &msgid,
	    sizeof(msgid));

	queue_proc_call();
	queue_proc_end();

	return (imsg_get_fd(&imsg));
}

static int
queue_proc_envelope_create(uint32_t msgid, const char *buf, size_t len,
    uint64_t *evpid)
{
	struct ibuf	*b;
	int		 r;

	msgid = evpid_to_msgid(*evpid);
	b = imsg_create(&ibuf, PROC_QUEUE_ENVELOPE_CREATE, 0, 0,
	    sizeof(msgid) + len);
	if (imsg_add(b, &msgid, sizeof(msgid)) == -1 ||
	    imsg_add(b, buf, len) == -1)
		return (0);
	imsg_close(&ibuf, b);

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));
	if (r == 1)
		queue_proc_read(evpid, sizeof(*evpid));
	queue_proc_end();

	return (r);
}

static int
queue_proc_envelope_delete(uint64_t evpid)
{
	int	r;

	imsg_compose(&ibuf, PROC_QUEUE_ENVELOPE_DELETE, 0, 0, -1, &evpid,
	    sizeof(evpid));

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));
	queue_proc_end();

	return (r);
}

static int
queue_proc_envelope_update(uint64_t evpid, const char *buf, size_t len)
{
	struct ibuf	*b;
	int		 r;

	b = imsg_create(&ibuf, PROC_QUEUE_ENVELOPE_UPDATE, 0, 0,
	    len + sizeof(evpid));
	if (imsg_add(b, &evpid, sizeof(evpid)) == -1 ||
	    imsg_add(b, buf, len) == -1)
		return (0);
	imsg_close(&ibuf, b);

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));
	queue_proc_end();

	return (r);
}

static int
queue_proc_envelope_load(uint64_t evpid, char *buf, size_t len)
{
	int	r;

	imsg_compose(&ibuf, PROC_QUEUE_ENVELOPE_LOAD, 0, 0, -1, &evpid,
	    sizeof(evpid));

	queue_proc_call();

	if (rlen > len) {
		log_warnx("warn: queue-proc: buf too small");
		fatalx("queue-proc: exiting");
	}

	r = rlen;
	queue_proc_read(buf, rlen);
	queue_proc_end();

	return (r);
}

static int
queue_proc_envelope_walk(uint64_t *evpid, char *buf, size_t len)
{
	int	r;

	imsg_compose(&ibuf, PROC_QUEUE_ENVELOPE_WALK, 0, 0, -1, NULL, 0);

	queue_proc_call();
	queue_proc_read(&r, sizeof(r));

	if (r > 0) {
		queue_proc_read(evpid, sizeof(*evpid));
		if (rlen > len) {
			log_warnx("warn: queue-proc: buf too small");
			fatalx("queue-proc: exiting");
		}
		if (r != (int)rlen) {
			log_warnx("warn: queue-proc: len mismatch");
			fatalx("queue-proc: exiting");
		}
		queue_proc_read(buf, rlen);
	}
	queue_proc_end();

	return (r);
}

static int
queue_proc_init(struct passwd *pw, int server, const char *conf)
{
	uint32_t	version;
	int		fd;

	fd = fork_proc_backend("queue", conf, "queue-proc", 0);
	if (fd == -1)
		fatalx("queue-proc: exiting");

	if (imsgbuf_init(&ibuf, fd) == -1)
		fatal("queue-proc: exiting");
	imsgbuf_allow_fdpass(&ibuf);

	version = PROC_QUEUE_API_VERSION;
	imsg_compose(&ibuf, PROC_QUEUE_INIT, 0, 0, -1,
	    &version, sizeof(version));

	queue_api_on_close(queue_proc_close);
	queue_api_on_message_create(queue_proc_message_create);
	queue_api_on_message_commit(queue_proc_message_commit);
	queue_api_on_message_delete(queue_proc_message_delete);
	queue_api_on_message_fd_r(queue_proc_message_fd_r);
	queue_api_on_envelope_create(queue_proc_envelope_create);
	queue_api_on_envelope_delete(queue_proc_envelope_delete);
	queue_api_on_envelope_update(queue_proc_envelope_update);
	queue_api_on_envelope_load(queue_proc_envelope_load);
	queue_api_on_envelope_walk(queue_proc_envelope_walk);

	queue_proc_call();
	queue_proc_end();

	return (1);
}

struct queue_backend	queue_backend_proc = {
	queue_proc_init,
};
