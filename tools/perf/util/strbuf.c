#include "cache.h"
#include <linux/kernel.h>

int prefixcmp(const char *str, const char *prefix)
{
	for (; ; str++, prefix++)
		if (!*prefix)
			return 0;
		else if (*str != *prefix)
			return (unsigned char)*prefix - (unsigned char)*str;
}

/*
 * Used as the default ->buf value, so that people can always assume
 * buf is non NULL and ->buf is NUL terminated even for a freshly
 * initialized strbuf.
 */
char strbuf_slopbuf[1];

void strbuf_init(struct strbuf *sb, ssize_t hint)
{
	sb->alloc = sb->len = 0;
	sb->buf = strbuf_slopbuf;
	if (hint)
		strbuf_grow(sb, hint);
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

void strbuf_grow(struct strbuf *sb, size_t extra)
{
	if (sb->len + extra + 1 <= sb->len)
		die("you want to use way too much memory");
	if (!sb->alloc)
		sb->buf = NULL;
	ALLOC_GROW(sb->buf, sb->len + extra + 1, sb->alloc);
}

void strbuf_addch(struct strbuf *sb, int c)
{
	strbuf_grow(sb, 1);
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

void strbuf_add(struct strbuf *sb, const void *data, size_t len)
{
	strbuf_grow(sb, len);
	memcpy(sb->buf + sb->len, data, len);
	strbuf_setlen(sb, sb->len + len);
}

static void strbuf_addv(struct strbuf *sb, const char *fmt, va_list ap)
{
	int len;
	va_list ap_saved;

	if (!strbuf_avail(sb))
		strbuf_grow(sb, 64);

	va_copy(ap_saved, ap);
	len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
	if (len < 0)
		die("your vsnprintf is broken");
	if (len > strbuf_avail(sb)) {
		strbuf_grow(sb, len);
		len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap_saved);
		va_end(ap_saved);
		if (len > strbuf_avail(sb)) {
			die("this should not happen, your vsnprintf is broken");
		}
	}
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_addf(struct strbuf *sb, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	strbuf_addv(sb, fmt, ap);
	va_end(ap);
}

ssize_t strbuf_read(struct strbuf *sb, int fd, ssize_t hint)
{
	size_t oldlen = sb->len;
	size_t oldalloc = sb->alloc;

	strbuf_grow(sb, hint ? hint : 8192);
	for (;;) {
		ssize_t cnt;

		cnt = read(fd, sb->buf + sb->len, sb->alloc - sb->len - 1);
		if (cnt < 0) {
			if (oldalloc == 0)
				strbuf_release(sb);
			else
				strbuf_setlen(sb, oldlen);
			return -1;
		}
		if (!cnt)
			break;
		sb->len += cnt;
		strbuf_grow(sb, 8192);
	}

	sb->buf[sb->len] = '\0';
	return sb->len - oldlen;
}
