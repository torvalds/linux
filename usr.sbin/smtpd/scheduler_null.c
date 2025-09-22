/*	$OpenBSD: scheduler_null.c,v 1.11 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include "smtpd.h"

static int scheduler_null_init(const char *);
static int scheduler_null_insert(struct scheduler_info *);
static size_t scheduler_null_commit(uint32_t);
static size_t scheduler_null_rollback(uint32_t);
static int scheduler_null_update(struct scheduler_info *);
static int scheduler_null_delete(uint64_t);
static int scheduler_null_hold(uint64_t, uint64_t);
static int scheduler_null_release(int, uint64_t, int);
static int scheduler_null_batch(int, int*, size_t*, uint64_t*, int*);
static size_t scheduler_null_messages(uint32_t, uint32_t *, size_t);
static size_t scheduler_null_envelopes(uint64_t, struct evpstate *, size_t);
static int scheduler_null_schedule(uint64_t);
static int scheduler_null_remove(uint64_t);
static int scheduler_null_suspend(uint64_t);
static int scheduler_null_resume(uint64_t);

struct scheduler_backend scheduler_backend_null = {
	scheduler_null_init,

	scheduler_null_insert,
	scheduler_null_commit,
	scheduler_null_rollback,

	scheduler_null_update,
	scheduler_null_delete,
	scheduler_null_hold,
	scheduler_null_release,

	scheduler_null_batch,

	scheduler_null_messages,
	scheduler_null_envelopes,
	scheduler_null_schedule,
	scheduler_null_remove,
	scheduler_null_suspend,
	scheduler_null_resume,
};

static int
scheduler_null_init(const char *arg)
{
	return (1);
}

static int
scheduler_null_insert(struct scheduler_info *si)
{
	return (0);
}

static size_t
scheduler_null_commit(uint32_t msgid)
{
	return (0);
}

static size_t
scheduler_null_rollback(uint32_t msgid)
{
	return (0);
}

static int
scheduler_null_update(struct scheduler_info *si)
{
	return (0);
}

static int
scheduler_null_delete(uint64_t evpid)
{
	return (0);
}

static int
scheduler_null_hold(uint64_t evpid, uint64_t holdq)
{
	return (0);
}

static int
scheduler_null_release(int type, uint64_t holdq, int n)
{
	return (0);
}

static int
scheduler_null_batch(int typemask, int *delay, size_t *count, uint64_t *evpids, int *types)
{
	*delay = 0;

	return (0);
}

static int
scheduler_null_schedule(uint64_t evpid)
{
	return (0);
}

static int
scheduler_null_remove(uint64_t evpid)
{
	return (0);
}

static int
scheduler_null_suspend(uint64_t evpid)
{
	return (0);
}

static int
scheduler_null_resume(uint64_t evpid)
{
	return (0);
}

static size_t
scheduler_null_messages(uint32_t from, uint32_t *dst, size_t size)
{
	return (0);
}

static size_t
scheduler_null_envelopes(uint64_t from, struct evpstate *dst, size_t size)
{
	return (0);
}
