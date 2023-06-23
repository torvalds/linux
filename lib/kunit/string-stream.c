// SPDX-License-Identifier: GPL-2.0
/*
 * C++ stream style string builder used in KUnit for building messages.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/test.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "string-stream.h"


static struct string_stream_fragment *alloc_string_stream_fragment(
		struct kunit *test, int len, gfp_t gfp)
{
	struct string_stream_fragment *frag;

	frag = kunit_kzalloc(test, sizeof(*frag), gfp);
	if (!frag)
		return ERR_PTR(-ENOMEM);

	frag->fragment = kunit_kmalloc(test, len, gfp);
	if (!frag->fragment)
		return ERR_PTR(-ENOMEM);

	return frag;
}

static void string_stream_fragment_destroy(struct kunit *test,
					   struct string_stream_fragment *frag)
{
	list_del(&frag->node);
	kunit_kfree(test, frag->fragment);
	kunit_kfree(test, frag);
}

int string_stream_vadd(struct string_stream *stream,
		       const char *fmt,
		       va_list args)
{
	struct string_stream_fragment *frag_container;
	int len;
	va_list args_for_counting;

	/* Make a copy because `vsnprintf` could change it */
	va_copy(args_for_counting, args);

	/* Need space for null byte. */
	len = vsnprintf(NULL, 0, fmt, args_for_counting) + 1;

	va_end(args_for_counting);

	frag_container = alloc_string_stream_fragment(stream->test,
						      len,
						      stream->gfp);
	if (IS_ERR(frag_container))
		return PTR_ERR(frag_container);

	len = vsnprintf(frag_container->fragment, len, fmt, args);
	spin_lock(&stream->lock);
	stream->length += len;
	list_add_tail(&frag_container->node, &stream->fragments);
	spin_unlock(&stream->lock);

	return 0;
}

int string_stream_add(struct string_stream *stream, const char *fmt, ...)
{
	va_list args;
	int result;

	va_start(args, fmt);
	result = string_stream_vadd(stream, fmt, args);
	va_end(args);

	return result;
}

static void string_stream_clear(struct string_stream *stream)
{
	struct string_stream_fragment *frag_container, *frag_container_safe;

	spin_lock(&stream->lock);
	list_for_each_entry_safe(frag_container,
				 frag_container_safe,
				 &stream->fragments,
				 node) {
		string_stream_fragment_destroy(stream->test, frag_container);
	}
	stream->length = 0;
	spin_unlock(&stream->lock);
}

char *string_stream_get_string(struct string_stream *stream)
{
	struct string_stream_fragment *frag_container;
	size_t buf_len = stream->length + 1; /* +1 for null byte. */
	char *buf;

	buf = kunit_kzalloc(stream->test, buf_len, stream->gfp);
	if (!buf)
		return NULL;

	spin_lock(&stream->lock);
	list_for_each_entry(frag_container, &stream->fragments, node)
		strlcat(buf, frag_container->fragment, buf_len);
	spin_unlock(&stream->lock);

	return buf;
}

int string_stream_append(struct string_stream *stream,
			 struct string_stream *other)
{
	const char *other_content;

	other_content = string_stream_get_string(other);

	if (!other_content)
		return -ENOMEM;

	return string_stream_add(stream, other_content);
}

bool string_stream_is_empty(struct string_stream *stream)
{
	return list_empty(&stream->fragments);
}

struct string_stream_alloc_context {
	struct kunit *test;
	gfp_t gfp;
};

struct string_stream *alloc_string_stream(struct kunit *test, gfp_t gfp)
{
	struct string_stream *stream;

	stream = kunit_kzalloc(test, sizeof(*stream), gfp);
	if (!stream)
		return ERR_PTR(-ENOMEM);

	stream->gfp = gfp;
	stream->test = test;
	INIT_LIST_HEAD(&stream->fragments);
	spin_lock_init(&stream->lock);

	return stream;
}

void string_stream_destroy(struct string_stream *stream)
{
	string_stream_clear(stream);
}
