// SPDX-License-Identifier: GPL-2.0
/*
 * C++ stream style string builder used in KUnit for building messages.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "string-stream.h"


static struct string_stream_fragment *alloc_string_stream_fragment(int len, gfp_t gfp)
{
	struct string_stream_fragment *frag;

	frag = kzalloc(sizeof(*frag), gfp);
	if (!frag)
		return ERR_PTR(-ENOMEM);

	frag->fragment = kmalloc(len, gfp);
	if (!frag->fragment) {
		kfree(frag);
		return ERR_PTR(-ENOMEM);
	}

	return frag;
}

static void string_stream_fragment_destroy(struct string_stream_fragment *frag)
{
	list_del(&frag->node);
	kfree(frag->fragment);
	kfree(frag);
}

int string_stream_vadd(struct string_stream *stream,
		       const char *fmt,
		       va_list args)
{
	struct string_stream_fragment *frag_container;
	int buf_len, result_len;
	va_list args_for_counting;

	/* Make a copy because `vsnprintf` could change it */
	va_copy(args_for_counting, args);

	/* Evaluate length of formatted string */
	buf_len = vsnprintf(NULL, 0, fmt, args_for_counting);

	va_end(args_for_counting);

	if (buf_len == 0)
		return 0;

	/* Reserve one extra for possible appended newline. */
	if (stream->append_newlines)
		buf_len++;

	/* Need space for null byte. */
	buf_len++;

	frag_container = alloc_string_stream_fragment(buf_len, stream->gfp);
	if (IS_ERR(frag_container))
		return PTR_ERR(frag_container);

	if (stream->append_newlines) {
		/* Don't include reserved newline byte in writeable length. */
		result_len = vsnprintf(frag_container->fragment, buf_len - 1, fmt, args);

		/* Append newline if necessary. */
		if (frag_container->fragment[result_len - 1] != '\n')
			result_len = strlcat(frag_container->fragment, "\n", buf_len);
	} else {
		result_len = vsnprintf(frag_container->fragment, buf_len, fmt, args);
	}

	spin_lock(&stream->lock);
	stream->length += result_len;
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

void string_stream_clear(struct string_stream *stream)
{
	struct string_stream_fragment *frag_container, *frag_container_safe;

	spin_lock(&stream->lock);
	list_for_each_entry_safe(frag_container,
				 frag_container_safe,
				 &stream->fragments,
				 node) {
		string_stream_fragment_destroy(frag_container);
	}
	stream->length = 0;
	spin_unlock(&stream->lock);
}

char *string_stream_get_string(struct string_stream *stream)
{
	struct string_stream_fragment *frag_container;
	size_t buf_len = stream->length + 1; /* +1 for null byte. */
	char *buf;

	buf = kzalloc(buf_len, stream->gfp);
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
	int ret;

	other_content = string_stream_get_string(other);

	if (!other_content)
		return -ENOMEM;

	ret = string_stream_add(stream, other_content);
	kfree(other_content);

	return ret;
}

bool string_stream_is_empty(struct string_stream *stream)
{
	return list_empty(&stream->fragments);
}

struct string_stream *alloc_string_stream(gfp_t gfp)
{
	struct string_stream *stream;

	stream = kzalloc(sizeof(*stream), gfp);
	if (!stream)
		return ERR_PTR(-ENOMEM);

	stream->gfp = gfp;
	INIT_LIST_HEAD(&stream->fragments);
	spin_lock_init(&stream->lock);

	return stream;
}

void string_stream_destroy(struct string_stream *stream)
{
	KUNIT_STATIC_STUB_REDIRECT(string_stream_destroy, stream);

	if (IS_ERR_OR_NULL(stream))
		return;

	string_stream_clear(stream);
	kfree(stream);
}

static void resource_free_string_stream(void *p)
{
	struct string_stream *stream = p;

	string_stream_destroy(stream);
}

struct string_stream *kunit_alloc_string_stream(struct kunit *test, gfp_t gfp)
{
	struct string_stream *stream;

	stream = alloc_string_stream(gfp);
	if (IS_ERR(stream))
		return stream;

	if (kunit_add_action_or_reset(test, resource_free_string_stream, stream) != 0)
		return ERR_PTR(-ENOMEM);

	return stream;
}

void kunit_free_string_stream(struct kunit *test, struct string_stream *stream)
{
	kunit_release_action(test, resource_free_string_stream, (void *)stream);
}
