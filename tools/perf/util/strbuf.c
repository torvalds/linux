// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "util.h"
#include <linux/kernel.h>
#include <errno.h>

/*
 * Used as the default ->buf value, so that people can always assume
 * buf is non NULL and ->buf is NUL terminated even for a freshly
 * initialized strbuf.
 */
char strbuf_slopbuf[1];

int strbuf_init(struct strbuf *sb, ssize_t hint)
{
	sb->alloc = sb->len = 0;
	sb->buf = strbuf_slopbuf;
	if (hint)
		return strbuf_grow(sb, hint);
	return 0;
}

void strbuf_release(struct strbuf *sb)
{
	if (sb->alloc) {
		zfree(&sb->buf);
		strbuf_init(sb, 0);
	}
}

char *strbuf_detach(struct strbuf *sb, size_t *sz)
{
	char *res = sb->alloc ? sb->buf : NULL;
	if (sz)
		*sz = sb->len;
	strbuf_init(sb, 0);
	return res;
}

int strbuf_grow(struct strbuf *sb, size_t extra)
{
	char *buf;
	size_t nr = sb->len + extra + 1;

	if (nr < sb->alloc)
		return 0;

	if (nr <= sb->len)
		return -E2BIG;

	if (alloc_nr(sb->alloc) > nr)
		nr = alloc_nr(sb->alloc);

	/*
	 * Note that sb->buf == strbuf_slopbuf if sb->alloc == 0, and it is
	 * a static variable. Thus we have to avoid passing it to realloc.
	 */
	buf = realloc(sb->alloc ? sb->buf : NULL, nr * sizeof(*buf));
	if (!buf)
		return -ENOMEM;

	sb->buf = buf;
	sb->alloc = nr;
	return 0;
}

int strbuf_addch(struct strbuf *sb, int c)
{
	int ret = strbuf_grow(sb, 1);
	if (ret)
		return ret;

	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
	return 0;
}

int strbuf_add(struct strbuf *sb, const void *data, size_t len)
{
	int ret = strbuf_grow(sb, len);
	if (ret)
		return ret;

	memcpy(sb->buf + sb->len, data, len);
	return strbuf_setlen(sb, sb->len + len);
}

static int strbuf_addv(struct strbuf *sb, const char *fmt, va_list ap)
{
	int len, ret;
	va_list ap_saved;

	if (!strbuf_avail(sb)) {
		ret = strbuf_grow(sb, 64);
		if (ret)
			return ret;
	}

	va_copy(ap_saved, ap);
	len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
	if (len < 0)
		return len;
	if (len > strbuf_avail(sb)) {
		ret = strbuf_grow(sb, len);
		if (ret)
			return ret;
		len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap_saved);
		va_end(ap_saved);
		if (len > strbuf_avail(sb)) {
			pr_debug("this should not happen, your vsnprintf is broken");
			return -EINVAL;
		}
	}
	return strbuf_setlen(sb, sb->len + len);
}

int strbuf_addf(struct strbuf *sb, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = strbuf_addv(sb, fmt, ap);
	va_end(ap);
	return ret;
}

ssize_t strbuf_read(struct strbuf *sb, int fd, ssize_t hint)
{
	size_t oldlen = sb->len;
	size_t oldalloc = sb->alloc;
	int ret;

	ret = strbuf_grow(sb, hint ? hint : 8192);
	if (ret)
		return ret;

	for (;;) {
		ssize_t cnt;

		cnt = read(fd, sb->buf + sb->len, sb->alloc - sb->len - 1);
		if (cnt < 0) {
			if (oldalloc == 0)
				strbuf_release(sb);
			else
				strbuf_setlen(sb, oldlen);
			return cnt;
		}
		if (!cnt)
			break;
		sb->len += cnt;
		ret = strbuf_grow(sb, 8192);
		if (ret)
			return ret;
	}

	sb->buf[sb->len] = '\0';
	return sb->len - oldlen;
}
