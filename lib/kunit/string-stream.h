/* SPDX-License-Identifier: GPL-2.0 */
/*
 * C++ stream style string builder used in KUnit for building messages.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#ifndef _KUNIT_STRING_STREAM_H
#define _KUNIT_STRING_STREAM_H

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/stdarg.h>

struct string_stream_fragment {
	struct kunit *test;
	struct list_head node;
	char *fragment;
};

struct string_stream {
	size_t length;
	struct list_head fragments;
	/* length and fragments are protected by this lock */
	spinlock_t lock;
	struct kunit *test;
	gfp_t gfp;
};

struct kunit;

struct string_stream *alloc_string_stream(struct kunit *test, gfp_t gfp);

int __printf(2, 3) string_stream_add(struct string_stream *stream,
				     const char *fmt, ...);

int __printf(2, 0) string_stream_vadd(struct string_stream *stream,
				      const char *fmt,
				      va_list args);

char *string_stream_get_string(struct string_stream *stream);

int string_stream_append(struct string_stream *stream,
			 struct string_stream *other);

bool string_stream_is_empty(struct string_stream *stream);

int string_stream_destroy(struct string_stream *stream);

#endif /* _KUNIT_STRING_STREAM_H */
