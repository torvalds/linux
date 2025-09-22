/*	$OpenBSD: kqueue-user.c,v 1.2 2025/05/21 14:10:16 visa Exp $	*/

/*
 * Copyright (c) 2022 Visa Hankala
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
#include <sys/time.h>
#include <sys/event.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>

#include "main.h"

int
do_user(void)
{
	const struct timespec ts = { 0, 10000 };
	struct kevent kev[2];
	int dummy, dummy2, i, kq, n;

	ASS((kq = kqueue()) >= 0,
	    warn("kqueue"));

	/* Set up an event. */
	EV_SET(&kev[0], 1, EVFILT_USER, EV_ADD, ~0U & ~NOTE_TRIGGER, 0, NULL);
	ASS(kevent(kq, kev, 1, NULL, 0, NULL) == 0,
	    warn("kevent"));

	n = kevent(kq, NULL, 0, kev, 2, &ts);
	ASSX(n == 0);

	/* Activate the event. This updates `data' and `udata'. */
	EV_SET(&kev[0], 1, EVFILT_USER, 0, NOTE_TRIGGER | NOTE_FFNOP,
	    123, &dummy);
	n = kevent(kq, kev, 1, NULL, 0, NULL);
	ASSX(n == 0);

	/* Check active events. */
	n = kevent(kq, NULL, 0, kev, 2, &ts);
	ASSX(n == 1);
	ASSX(kev[0].ident == 1);
	ASSX(kev[0].fflags == NOTE_FFLAGSMASK);
	ASSX(kev[0].data == 123);
	ASSX(kev[0].udata == &dummy);

	/* Set up another event. */
	EV_SET(&kev[0], 2, EVFILT_USER, EV_ADD, NOTE_TRIGGER, 654, &dummy2);
	n = kevent(kq, kev, 1, NULL, 0, NULL);
	ASSX(n == 0);

	/* Check active events. This assumes a specific output order. */
	n = kevent(kq, NULL, 0, kev, 2, &ts);
	ASSX(n == 2);
	ASSX(kev[0].ident == 1);
	ASSX(kev[0].fflags == NOTE_FFLAGSMASK);
	ASSX(kev[0].data == 123);
	ASSX(kev[0].udata == &dummy);
	ASSX(kev[1].ident == 2);
	ASSX(kev[1].fflags == 0);
	ASSX(kev[1].data == 654);
	ASSX(kev[1].udata == &dummy2);

	/* Clear the first event. */
	EV_SET(&kev[0], 1, EVFILT_USER, EV_CLEAR, 0, 0, NULL);
	n = kevent(kq, kev, 1, NULL, 0, NULL);
	ASSX(n == 0);

	n = kevent(kq, NULL, 0, kev, 2, &ts);
	ASSX(n == 1);
	ASSX(kev[0].ident == 2);
	ASSX(kev[0].fflags == 0);
	ASSX(kev[0].data == 654);
	ASSX(kev[0].udata == &dummy2);

	/* Delete the second event. */
	EV_SET(&kev[0], 2, EVFILT_USER, EV_DELETE, 0, 0, NULL);
	n = kevent(kq, kev, 1, NULL, 0, NULL);
	ASSX(n == 0);

	n = kevent(kq, NULL, 0, kev, 2, &ts);
	ASSX(n == 0);

	/* Test self-clearing event. */
	EV_SET(&kev[0], 2, EVFILT_USER, EV_ADD | EV_CLEAR, 0x11, 42, &dummy);
	n = kevent(kq, kev, 1, kev, 2, &ts);
	ASSX(n == 0);

	EV_SET(&kev[0], 2, EVFILT_USER, 0, NOTE_TRIGGER | 0x3, 24, &dummy2);
	n = kevent(kq, kev, 1, kev, 2, &ts);
	ASSX(n == 1);
	ASSX(kev[0].ident == 2);
	ASSX(kev[0].fflags == 0x11);
	ASSX(kev[0].data == 24);
	ASSX(kev[0].udata == &dummy2);

	n = kevent(kq, NULL, 0, kev, 2, &ts);
	ASSX(n == 0);

	EV_SET(&kev[0], 2, EVFILT_USER, 0, NOTE_TRIGGER | 0x3, 9, &dummy2);
	n = kevent(kq, kev, 1, kev, 2, &ts);
	ASSX(n == 1);
	ASSX(kev[0].ident == 2);
	ASSX(kev[0].fflags == 0x11);
	ASSX(kev[0].data == 9);
	ASSX(kev[0].udata == &dummy2);

	EV_SET(&kev[0], 2, EVFILT_USER, EV_DELETE, 0, 0, NULL);
	n = kevent(kq, kev, 1, kev, 2, &ts);
	ASSX(n == 0);

	/* Change fflags. */
	EV_SET(&kev[0], 1, EVFILT_USER, 0, NOTE_FFCOPY | 0x00aa00, 0, NULL);
	n = kevent(kq, kev, 1, kev, 2, &ts);
	ASSX(n == 0);
	EV_SET(&kev[0], 1, EVFILT_USER, 0, NOTE_FFOR | 0xff00ff, 0, NULL);
	n = kevent(kq, kev, 1, kev, 2, &ts);
	ASSX(n == 0);
	EV_SET(&kev[0], 1, EVFILT_USER, 0, NOTE_TRIGGER | NOTE_FFAND | 0x0ffff0,
	    0, NULL);
	n = kevent(kq, kev, 1, kev, 2, &ts);
	ASSX(n == 1);
	ASSX(kev[0].ident == 1);
	ASSX(kev[0].fflags == 0x0faaf0);
	ASSX(kev[0].data == 0);
	ASSX(kev[0].udata == NULL);

	/* Test event limit. */
	for (i = 0;; i++) {
		EV_SET(&kev[0], i, EVFILT_USER, EV_ADD, 0, 0, NULL);
		n = kevent(kq, kev, 1, NULL, 0, NULL);
		if (n == -1) {
			ASSX(errno == ENOMEM);
			break;
		}
		ASSX(n == 0);
	}
	ASSX(i < 1000000);

	/* Delete one event, ... */
	EV_SET(&kev[0], 0, EVFILT_USER, EV_DELETE, 0, 0, NULL);
	n = kevent(kq, kev, 1, NULL, 0, NULL);
	ASSX(n == 0);

	/* ... after which adding should succeed. */
	EV_SET(&kev[0], 0, EVFILT_USER, EV_ADD, 0, 0, NULL);
	n = kevent(kq, kev, 1, NULL, 0, NULL);
	ASSX(n == 0);

	EV_SET(&kev[0], i, EVFILT_USER, EV_ADD, 0, 0, NULL);
	n = kevent(kq, kev, 1, NULL, 0, NULL);
	ASSX(n == -1);
	ASSX(errno == ENOMEM);

	close(kq);

	return (0);
}
