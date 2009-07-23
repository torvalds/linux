#include "cache.h"
#include "quote.h"

int quote_path_fully = 1;

/* Help to copy the thing properly quoted for the shell safety.
 * any single quote is replaced with '\'', any exclamation point
 * is replaced with '\!', and the whole thing is enclosed in a
 *
 * E.g.
 *  original     sq_quote     result
 *  name     ==> name      ==> 'name'
 *  a b      ==> a b       ==> 'a b'
 *  a'b      ==> a'\''b    ==> 'a'\''b'
 *  a!b      ==> a'\!'b    ==> 'a'\!'b'
 */
static inline int need_bs_quote(char c)
{
	return (c == '\'' || c == '!');
}

void sq_quote_buf(struct strbuf *dst, const char *src)
{
	char *to_free = NULL;

	if (dst->buf == src)
		to_free = strbuf_detach(dst, NULL);

	strbuf_addch(dst, '\'');
	while (*src) {
		size_t len = strcspn(src, "'!");
		strbuf_add(dst, src, len);
		src += len;
		while (need_bs_quote(*src)) {
			strbuf_addstr(dst, "'\\");
			strbuf_addch(dst, *src++);
			strbuf_addch(dst, '\'');
		}
	}
	strbuf_addch(dst, '\'');
	free(to_free);
}

void sq_quote_print(FILE *stream, const char *src)
{
	char c;

	fputc('\'', stream);
	while ((c = *src++)) {
		if (need_bs_quote(c)) {
			fputs("'\\", stream);
			fputc(c, stream);
			fputc('\'', stream);
		} else {
			fputc(c, stream);
		}
	}
	fputc('\'', stream);
}

void sq_quote_argv(struct strbuf *dst, const char** argv, size_t maxlen)
{
	int i;

	/* Copy into destination buffer. */
	strbuf_grow(dst, 255);
	for (i = 0; argv[i]; ++i) {
		strbuf_addch(dst, ' ');
		sq_quote_buf(dst, argv[i]);
		if (maxlen && dst->len > maxlen)
			die("Too many or long arguments");
	}
}

char *sq_dequote_step(char *arg, char **next)
{
	char *dst = arg;
	char *src = arg;
	char c;

	if (*src != '\'')
		return NULL;
	for (;;) {
		c = *++src;
		if (!c)
			return NULL;
		if (c != '\'') {
			*dst++ = c;
			continue;
		}
		/* We stepped out of sq */
		switch (*++src) {
		case '\0':
			*dst = 0;
			if (next)
				*next = NULL;
			return arg;
		case '\\':
			c = *++src;
			if (need_bs_quote(c) && *++src == '\'') {
				*dst++ = c;
				continue;
			}
		/* Fallthrough */
		default:
			if (!next || !isspace(*src))
				return NULL;
			do {
				c = *++src;
			} while (isspace(c));
			*dst = 0;
			*next = src;
			return arg;
		}
	}
}

char *sq_dequote(char *arg)
{
	return sq_dequote_step(arg, NULL);
}

int sq_dequote_to_argv(char *arg, const char ***argv, int *nr, int *alloc)
{
	char *next = arg;

	if (!*arg)
		return 0;
	do {
		char *dequoted = sq_dequote_step(next, &next);
		if (!dequoted)
			return -1;
		ALLOC_GROW(*argv, *nr + 1, *alloc);
		(*argv)[(*nr)++] = dequoted;
	} while (next);

	return 0;
}

/* 1 means: quote as octal
 * 0 means: quote as octal if (quote_path_fully)
 * -1 means: never quote
 * c: quote as "\\c"
 */
#define X8(x)   x, x, x, x, x, x, x, x
#define X16(x)  X8(x), X8(x)
static signed char const sq_lookup[256] = {
	/*           0    1    2    3    4    5    6    7 */
	/* 0x00 */   1,   1,   1,   1,   1,   1,   1, 'a',
	/* 0x08 */ 'b', 't', 'n', 'v', 'f', 'r',   1,   1,
	/* 0x10 */ X16(1),
	/* 0x20 */  -1,  -1, '"',  -1,  -1,  -1,  -1,  -1,
	/* 0x28 */ X16(-1), X16(-1), X16(-1),
	/* 0x58 */  -1,  -1,  -1,  -1,'\\',  -1,  -1,  -1,
	/* 0x60 */ X16(-1), X8(-1),
	/* 0x78 */  -1,  -1,  -1,  -1,  -1,  -1,  -1,   1,
	/* 0x80 */ /* set to 0 */
};

static inline int sq_must_quote(char c)
{
	return sq_lookup[(unsigned char)c] + quote_path_fully > 0;
}

/*
 * Returns the longest prefix not needing a quote up to maxlen if
 * positive.
 * This stops at the first \0 because it's marked as a character
 * needing an escape.
 */
static ssize_t next_quote_pos(const char *s, ssize_t maxlen)
{
	ssize_t len;

	if (maxlen < 0) {
		for (len = 0; !sq_must_quote(s[len]); len++);
	} else {
		for (len = 0; len < maxlen && !sq_must_quote(s[len]); len++);
	}
	return len;
}

/*
 * C-style name quoting.
 *
 * (1) if sb and fp are both NULL, inspect the input name and counts the
 *     number of bytes that are needed to hold c_style quoted version of name,
 *     counting the double quotes around it but not terminating NUL, and
 *     returns it.
 *     However, if name does not need c_style quoting, it returns 0.
 *
 * (2) if sb or fp are not NULL, it emits the c_style quoted version
 *     of name, enclosed with double quotes if asked and needed only.
 *     Return value is the same as in (1).
 */
static size_t quote_c_style_counted(const char *name, ssize_t maxlen,
                                    struct strbuf *sb, FILE *fp, int no_dq)
{
#define EMIT(c)							\
	do {							\
		if (sb) strbuf_addch(sb, (c));			\
		if (fp) fputc((c), fp);				\
		count++;					\
	} while (0)

#define EMITBUF(s, l)						\
	do {							\
		int __ret;					\
		if (sb) strbuf_add(sb, (s), (l));		\
		if (fp) __ret = fwrite((s), (l), 1, fp);	\
		count += (l);					\
	} while (0)

	ssize_t len, count = 0;
	const char *p = name;

	for (;;) {
		int ch;

		len = next_quote_pos(p, maxlen);
		if (len == maxlen || !p[len])
			break;

		if (!no_dq && p == name)
			EMIT('"');

		EMITBUF(p, len);
		EMIT('\\');
		p += len;
		ch = (unsigned char)*p++;
		if (sq_lookup[ch] >= ' ') {
			EMIT(sq_lookup[ch]);
		} else {
			EMIT(((ch >> 6) & 03) + '0');
			EMIT(((ch >> 3) & 07) + '0');
			EMIT(((ch >> 0) & 07) + '0');
		}
	}

	EMITBUF(p, len);
	if (p == name)   /* no ending quote needed */
		return 0;

	if (!no_dq)
		EMIT('"');
	return count;
}

size_t quote_c_style(const char *name, struct strbuf *sb, FILE *fp, int nodq)
{
	return quote_c_style_counted(name, -1, sb, fp, nodq);
}

void quote_two_c_style(struct strbuf *sb, const char *prefix, const char *path, int nodq)
{
	if (quote_c_style(prefix, NULL, NULL, 0) ||
	    quote_c_style(path, NULL, NULL, 0)) {
		if (!nodq)
			strbuf_addch(sb, '"');
		quote_c_style(prefix, sb, NULL, 1);
		quote_c_style(path, sb, NULL, 1);
		if (!nodq)
			strbuf_addch(sb, '"');
	} else {
		strbuf_addstr(sb, prefix);
		strbuf_addstr(sb, path);
	}
}

void write_name_quoted(const char *name, FILE *fp, int terminator)
{
	if (terminator) {
		quote_c_style(name, NULL, fp, 0);
	} else {
		fputs(name, fp);
	}
	fputc(terminator, fp);
}

void write_name_quotedpfx(const char *pfx, ssize_t pfxlen,
			  const char *name, FILE *fp, int terminator)
{
	int needquote = 0;

	if (terminator) {
		needquote = next_quote_pos(pfx, pfxlen) < pfxlen
			|| name[next_quote_pos(name, -1)];
	}
	if (needquote) {
		fputc('"', fp);
		quote_c_style_counted(pfx, pfxlen, NULL, fp, 1);
		quote_c_style(name, NULL, fp, 1);
		fputc('"', fp);
	} else {
		int ret;

		ret = fwrite(pfx, pfxlen, 1, fp);
		fputs(name, fp);
	}
	fputc(terminator, fp);
}

/* quote path as relative to the given prefix */
char *quote_path_relative(const char *in, int len,
			  struct strbuf *out, const char *prefix)
{
	int needquote;

	if (len < 0)
		len = strlen(in);

	/* "../" prefix itself does not need quoting, but "in" might. */
	needquote = (next_quote_pos(in, len) < len);
	strbuf_setlen(out, 0);
	strbuf_grow(out, len);

	if (needquote)
		strbuf_addch(out, '"');
	if (prefix) {
		int off = 0;
		while (prefix[off] && off < len && prefix[off] == in[off])
			if (prefix[off] == '/') {
				prefix += off + 1;
				in += off + 1;
				len -= off + 1;
				off = 0;
			} else
				off++;

		for (; *prefix; prefix++)
			if (*prefix == '/')
				strbuf_addstr(out, "../");
	}

	quote_c_style_counted (in, len, out, NULL, 1);

	if (needquote)
		strbuf_addch(out, '"');
	if (!out->len)
		strbuf_addstr(out, "./");

	return out->buf;
}

/*
 * C-style name unquoting.
 *
 * Quoted should point at the opening double quote.
 * + Returns 0 if it was able to unquote the string properly, and appends the
 *   result in the strbuf `sb'.
 * + Returns -1 in case of error, and doesn't touch the strbuf. Though note
 *   that this function will allocate memory in the strbuf, so calling
 *   strbuf_release is mandatory whichever result unquote_c_style returns.
 *
 * Updates endp pointer to point at one past the ending double quote if given.
 */
int unquote_c_style(struct strbuf *sb, const char *quoted, const char **endp)
{
	size_t oldlen = sb->len, len;
	int ch, ac;

	if (*quoted++ != '"')
		return -1;

	for (;;) {
		len = strcspn(quoted, "\"\\");
		strbuf_add(sb, quoted, len);
		quoted += len;

		switch (*quoted++) {
		  case '"':
			if (endp)
				*endp = quoted;
			return 0;
		  case '\\':
			break;
		  default:
			goto error;
		}

		switch ((ch = *quoted++)) {
		case 'a': ch = '\a'; break;
		case 'b': ch = '\b'; break;
		case 'f': ch = '\f'; break;
		case 'n': ch = '\n'; break;
		case 'r': ch = '\r'; break;
		case 't': ch = '\t'; break;
		case 'v': ch = '\v'; break;

		case '\\': case '"':
			break; /* verbatim */

		/* octal values with first digit over 4 overflow */
		case '0': case '1': case '2': case '3':
					ac = ((ch - '0') << 6);
			if ((ch = *quoted++) < '0' || '7' < ch)
				goto error;
					ac |= ((ch - '0') << 3);
			if ((ch = *quoted++) < '0' || '7' < ch)
				goto error;
					ac |= (ch - '0');
					ch = ac;
					break;
				default:
			goto error;
			}
		strbuf_addch(sb, ch);
		}

  error:
	strbuf_setlen(sb, oldlen);
	return -1;
}

/* quoting as a string literal for other languages */

void perl_quote_print(FILE *stream, const char *src)
{
	const char sq = '\'';
	const char bq = '\\';
	char c;

	fputc(sq, stream);
	while ((c = *src++)) {
		if (c == sq || c == bq)
			fputc(bq, stream);
		fputc(c, stream);
	}
	fputc(sq, stream);
}

void python_quote_print(FILE *stream, const char *src)
{
	const char sq = '\'';
	const char bq = '\\';
	const char nl = '\n';
	char c;

	fputc(sq, stream);
	while ((c = *src++)) {
		if (c == nl) {
			fputc(bq, stream);
			fputc('n', stream);
			continue;
		}
		if (c == sq || c == bq)
			fputc(bq, stream);
		fputc(c, stream);
	}
	fputc(sq, stream);
}

void tcl_quote_print(FILE *stream, const char *src)
{
	char c;

	fputc('"', stream);
	while ((c = *src++)) {
		switch (c) {
		case '[': case ']':
		case '{': case '}':
		case '$': case '\\': case '"':
			fputc('\\', stream);
		default:
			fputc(c, stream);
			break;
		case '\f':
			fputs("\\f", stream);
			break;
		case '\r':
			fputs("\\r", stream);
			break;
		case '\n':
			fputs("\\n", stream);
			break;
		case '\t':
			fputs("\\t", stream);
			break;
		case '\v':
			fputs("\\v", stream);
			break;
		}
	}
	fputc('"', stream);
}
