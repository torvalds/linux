#include "cache.h"

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

void strbuf_init(struct strbuf *sb, size_t hint)
{
	sb->alloc = sb->len = 0;
	sb->buf = strbuf_slopbuf;
	if (hint)
		strbuf_grow(sb, hint);
}

void strbuf_release(struct strbuf *sb)
{
	if (sb->alloc) {
		free(sb->buf);
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

void strbuf_attach(struct strbuf *sb, void *buf, size_t len, size_t alloc)
{
	strbuf_release(sb);
	sb->buf   = buf;
	sb->len   = len;
	sb->alloc = alloc;
	strbuf_grow(sb, 0);
	sb->buf[sb->len] = '\0';
}

void strbuf_grow(struct strbuf *sb, size_t extra)
{
	if (sb->len + extra + 1 <= sb->len)
		die("you want to use way too much memory");
	if (!sb->alloc)
		sb->buf = NULL;
	ALLOC_GROW(sb->buf, sb->len + extra + 1, sb->alloc);
}

void strbuf_trim(struct strbuf *sb)
{
	char *b = sb->buf;
	while (sb->len > 0 && isspace((unsigned char)sb->buf[sb->len - 1]))
		sb->len--;
	while (sb->len > 0 && isspace(*b)) {
		b++;
		sb->len--;
	}
	memmove(sb->buf, b, sb->len);
	sb->buf[sb->len] = '\0';
}
void strbuf_rtrim(struct strbuf *sb)
{
	while (sb->len > 0 && isspace((unsigned char)sb->buf[sb->len - 1]))
		sb->len--;
	sb->buf[sb->len] = '\0';
}

void strbuf_ltrim(struct strbuf *sb)
{
	char *b = sb->buf;
	while (sb->len > 0 && isspace(*b)) {
		b++;
		sb->len--;
	}
	memmove(sb->buf, b, sb->len);
	sb->buf[sb->len] = '\0';
}

void strbuf_tolower(struct strbuf *sb)
{
	int i;
	for (i = 0; i < sb->len; i++)
		sb->buf[i] = tolower(sb->buf[i]);
}

struct strbuf **strbuf_split(const struct strbuf *sb, int delim)
{
	int alloc = 2, pos = 0;
	char *n, *p;
	struct strbuf **ret;
	struct strbuf *t;

	ret = calloc(alloc, sizeof(struct strbuf *));
	p = n = sb->buf;
	while (n < sb->buf + sb->len) {
		int len;
		n = memchr(n, delim, sb->len - (n - sb->buf));
		if (pos + 1 >= alloc) {
			alloc = alloc * 2;
			ret = realloc(ret, sizeof(struct strbuf *) * alloc);
		}
		if (!n)
			n = sb->buf + sb->len - 1;
		len = n - p + 1;
		t = malloc(sizeof(struct strbuf));
		strbuf_init(t, len);
		strbuf_add(t, p, len);
		ret[pos] = t;
		ret[++pos] = NULL;
		p = ++n;
	}
	return ret;
}

void strbuf_list_free(struct strbuf **sbs)
{
	struct strbuf **s = sbs;

	while (*s) {
		strbuf_release(*s);
		free(*s++);
	}
	free(sbs);
}

int strbuf_cmp(const struct strbuf *a, const struct strbuf *b)
{
	int len = a->len < b->len ? a->len: b->len;
	int cmp = memcmp(a->buf, b->buf, len);
	if (cmp)
		return cmp;
	return a->len < b->len ? -1: a->len != b->len;
}

void strbuf_splice(struct strbuf *sb, size_t pos, size_t len,
				   const void *data, size_t dlen)
{
	if (pos + len < pos)
		die("you want to use way too much memory");
	if (pos > sb->len)
		die("`pos' is too far after the end of the buffer");
	if (pos + len > sb->len)
		die("`pos + len' is too far after the end of the buffer");

	if (dlen >= len)
		strbuf_grow(sb, dlen - len);
	memmove(sb->buf + pos + dlen,
			sb->buf + pos + len,
			sb->len - pos - len);
	memcpy(sb->buf + pos, data, dlen);
	strbuf_setlen(sb, sb->len + dlen - len);
}

void strbuf_insert(struct strbuf *sb, size_t pos, const void *data, size_t len)
{
	strbuf_splice(sb, pos, 0, data, len);
}

void strbuf_remove(struct strbuf *sb, size_t pos, size_t len)
{
	strbuf_splice(sb, pos, len, NULL, 0);
}

void strbuf_add(struct strbuf *sb, const void *data, size_t len)
{
	strbuf_grow(sb, len);
	memcpy(sb->buf + sb->len, data, len);
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_adddup(struct strbuf *sb, size_t pos, size_t len)
{
	strbuf_grow(sb, len);
	memcpy(sb->buf + sb->len, sb->buf + pos, len);
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_addf(struct strbuf *sb, const char *fmt, ...)
{
	int len;
	va_list ap;

	if (!strbuf_avail(sb))
		strbuf_grow(sb, 64);
	va_start(ap, fmt);
	len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
	va_end(ap);
	if (len < 0)
		die("your vsnprintf is broken");
	if (len > strbuf_avail(sb)) {
		strbuf_grow(sb, len);
		va_start(ap, fmt);
		len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
		va_end(ap);
		if (len > strbuf_avail(sb)) {
			die("this should not happen, your snprintf is broken");
		}
	}
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_expand(struct strbuf *sb, const char *format, expand_fn_t fn,
		   void *context)
{
	for (;;) {
		const char *percent;
		size_t consumed;

		percent = strchrnul(format, '%');
		strbuf_add(sb, format, percent - format);
		if (!*percent)
			break;
		format = percent + 1;

		consumed = fn(sb, format, context);
		if (consumed)
			format += consumed;
		else
			strbuf_addch(sb, '%');
	}
}

size_t strbuf_expand_dict_cb(struct strbuf *sb, const char *placeholder,
		void *context)
{
	struct strbuf_expand_dict_entry *e = context;
	size_t len;

	for (; e->placeholder && (len = strlen(e->placeholder)); e++) {
		if (!strncmp(placeholder, e->placeholder, len)) {
			if (e->value)
				strbuf_addstr(sb, e->value);
			return len;
		}
	}
	return 0;
}

size_t strbuf_fread(struct strbuf *sb, size_t size, FILE *f)
{
	size_t res;
	size_t oldalloc = sb->alloc;

	strbuf_grow(sb, size);
	res = fread(sb->buf + sb->len, 1, size, f);
	if (res > 0)
		strbuf_setlen(sb, sb->len + res);
	else if (res < 0 && oldalloc == 0)
		strbuf_release(sb);
	return res;
}

ssize_t strbuf_read(struct strbuf *sb, int fd, size_t hint)
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

#define STRBUF_MAXLINK (2*PATH_MAX)

int strbuf_readlink(struct strbuf *sb, const char *path, size_t hint)
{
	size_t oldalloc = sb->alloc;

	if (hint < 32)
		hint = 32;

	while (hint < STRBUF_MAXLINK) {
		int len;

		strbuf_grow(sb, hint);
		len = readlink(path, sb->buf, hint);
		if (len < 0) {
			if (errno != ERANGE)
				break;
		} else if (len < hint) {
			strbuf_setlen(sb, len);
			return 0;
		}

		/* .. the buffer was too small - try again */
		hint *= 2;
	}
	if (oldalloc == 0)
		strbuf_release(sb);
	return -1;
}

int strbuf_getline(struct strbuf *sb, FILE *fp, int term)
{
	int ch;

	strbuf_grow(sb, 0);
	if (feof(fp))
		return EOF;

	strbuf_reset(sb);
	while ((ch = fgetc(fp)) != EOF) {
		if (ch == term)
			break;
		strbuf_grow(sb, 1);
		sb->buf[sb->len++] = ch;
	}
	if (ch == EOF && sb->len == 0)
		return EOF;

	sb->buf[sb->len] = '\0';
	return 0;
}

int strbuf_read_file(struct strbuf *sb, const char *path, size_t hint)
{
	int fd, len;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	len = strbuf_read(sb, fd, hint);
	close(fd);
	if (len < 0)
		return -1;

	return len;
}
