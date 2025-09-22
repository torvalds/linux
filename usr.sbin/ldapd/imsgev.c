/*	$OpenBSD: imsgev.c,v 1.13 2024/11/21 13:39:07 claudio Exp $ */

/*
 * Copyright (c) 2009 Eric Faurot <eric@openbsd.org>
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
#include <sys/uio.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imsgev.h"
#include "log.h"

void imsgev_add(struct imsgev *);
void imsgev_dispatch(int, short, void *);
void imsgev_disconnect(struct imsgev *, int);

void
imsgev_init(struct imsgev *iev, int fd, void *data,
    void (*callback)(struct imsgev *, int, struct imsg *),
    void (*needfd)(struct imsgev *))
{
	if (imsgbuf_init(&iev->ibuf, fd) == -1)
		fatal("imsgbuf_init");
	imsgbuf_allow_fdpass(&iev->ibuf);
	iev->terminate = 0;

	iev->data = data;
	iev->handler = imsgev_dispatch;
	iev->callback = callback;
	iev->needfd = needfd;

	iev->events = EV_READ;
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev);
	event_add(&iev->ev, NULL);
}

int
imsgev_compose(struct imsgev *iev, u_int16_t type, u_int32_t peerid,
    uint32_t pid, int fd, void *data, u_int16_t datalen)
{
	int	r;

	r = imsg_compose(&iev->ibuf, type, peerid, pid, fd, data, datalen);
	if (r != -1)
		imsgev_add(iev);

	return (r);
}

void
imsgev_close(struct imsgev *iev)
{
	iev->terminate = 1;
	imsgev_add(iev);
}

void
imsgev_clear(struct imsgev *iev)
{
	event_del(&iev->ev);
	imsgbuf_clear(&iev->ibuf);
	close(iev->ibuf.fd);
}

void
imsgev_add(struct imsgev *iev)
{
	short	events = 0;

	if (!iev->terminate)
		events = EV_READ;
	if (imsgbuf_queuelen(&iev->ibuf) > 0 || iev->terminate)
		events |= EV_WRITE;

	/* optimization: skip event_{del/set/add} if already set */
	if (events == iev->events)
		return;

	iev->events = events;
	event_del(&iev->ev);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev);
	event_add(&iev->ev, NULL);
}

void
imsgev_dispatch(int fd, short ev, void *humppa)
{
	struct imsgev	*iev = humppa;
	struct imsgbuf	*ibuf = &iev->ibuf;
	struct imsg	 imsg;
	ssize_t		 n;

	iev->events = 0;

	if (ev & EV_READ) {
		/* if we don't have enough fds, free one up and retry */
		if (getdtablesize() <= getdtablecount() +
		    (int)((CMSG_SPACE(sizeof(int))-CMSG_SPACE(0))/sizeof(int)))
			iev->needfd(iev);

		if ((n = imsgbuf_read(ibuf)) == -1) {
			imsgev_disconnect(iev, IMSGEV_EREAD);
			return;
		}
		if (n == 0) {
			/*
			 * Connection is closed for reading, and we assume
			 * it is also closed for writing, so we error out
			 * if write data is pending.
			 */
			imsgev_disconnect(iev,
			    imsgbuf_queuelen(&iev->ibuf) > 0 ? IMSGEV_EWRITE :
			    IMSGEV_DONE);
			return;
		}
	}

	if (ev & EV_WRITE) {
		/*
		 * We wanted to write data out but the connection is either
		 * closed, or some error occured. Both case are not recoverable
		 * from the imsg perspective, so we treat it as a WRITE error.
		 */
		if (imsgbuf_write(ibuf) == -1) {
			imsgev_disconnect(iev, IMSGEV_EWRITE);
			return;
		}
	}

	while (iev->terminate == 0) {
		if ((n = imsg_get(ibuf, &imsg)) == -1) {
			imsgev_disconnect(iev, IMSGEV_EIMSG);
			return;
		}
		if (n == 0)
			break;
		iev->callback(iev, IMSGEV_IMSG, &imsg);
		imsg_free(&imsg);
	}

	if (iev->terminate && imsgbuf_queuelen(&iev->ibuf) == 0) {
		imsgev_disconnect(iev, IMSGEV_DONE);
		return;
	}

	imsgev_add(iev);
}

void
imsgev_disconnect(struct imsgev *iev, int code)
{
	iev->callback(iev, code, NULL);
}
