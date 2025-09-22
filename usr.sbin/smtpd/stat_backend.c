/*	$OpenBSD: stat_backend.c,v 1.12 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
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

extern struct stat_backend	stat_backend_ramstat;

struct stat_backend *
stat_backend_lookup(const char *name)
{
	return &stat_backend_ramstat;
}

void
stat_increment(const char *key, size_t count)
{
	struct stat_value	*value;

	if (count == 0)
		return;

	value = stat_counter(count);

	m_create(p_control, IMSG_STAT_INCREMENT, 0, 0, -1);
	m_add_string(p_control, key);
	m_add_data(p_control, value, sizeof(*value));
	m_close(p_control);
}

void
stat_decrement(const char *key, size_t count)
{
	struct stat_value	*value;

	if (count == 0)
		return;

	value = stat_counter(count);

	m_create(p_control, IMSG_STAT_DECREMENT, 0, 0, -1);
	m_add_string(p_control, key);
	m_add_data(p_control, value, sizeof(*value));
	m_close(p_control);
}

void
stat_set(const char *key, const struct stat_value *value)
{
	m_create(p_control, IMSG_STAT_SET, 0, 0, -1);
	m_add_string(p_control, key);
	m_add_data(p_control, value, sizeof(*value));
	m_close(p_control);
}

/* helpers */

struct stat_value *
stat_counter(size_t counter)
{
	static struct stat_value value;

	value.type = STAT_COUNTER;
	value.u.counter = counter;
	return &value;
}

struct stat_value *
stat_timestamp(time_t timestamp)
{
	static struct stat_value value;

	value.type = STAT_TIMESTAMP;
	value.u.timestamp = timestamp;
	return &value;
}

struct stat_value *
stat_timeval(struct timeval *tv)
{
	static struct stat_value value;

	value.type = STAT_TIMEVAL;
	value.u.tv = *tv;
	return &value;
}

struct stat_value *
stat_timespec(struct timespec *ts)
{
	static struct stat_value value;

	value.type = STAT_TIMESPEC;
	value.u.ts = *ts;
	return &value;
}
