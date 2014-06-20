/*
 * trace_seq.c
 *
 * Copyright (C) 2008-2014 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/trace_seq.h>

int trace_print_seq(struct seq_file *m, struct trace_seq *s)
{
	int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;
	int ret;

	ret = seq_write(m, s->buffer, len);

	/*
	 * Only reset this buffer if we successfully wrote to the
	 * seq_file buffer.
	 */
	if (!ret)
		trace_seq_init(s);

	return ret;
}

/**
 * trace_seq_printf - sequence printing of trace information
 * @s: trace sequence descriptor
 * @fmt: printf format string
 *
 * It returns 0 if the trace oversizes the buffer's free
 * space, 1 otherwise.
 *
 * The tracer may use either sequence operations or its own
 * copy to user routines. To simplify formating of a trace
 * trace_seq_printf is used to store strings into a special
 * buffer (@s). Then the output may be either used by
 * the sequencer or pulled into another buffer.
 */
int
trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
{
	int len = (PAGE_SIZE - 1) - s->len;
	va_list ap;
	int ret;

	if (s->full || !len)
		return 0;

	va_start(ap, fmt);
	ret = vsnprintf(s->buffer + s->len, len, fmt, ap);
	va_end(ap);

	/* If we can't write it all, don't bother writing anything */
	if (ret >= len) {
		s->full = 1;
		return 0;
	}

	s->len += ret;

	return 1;
}
EXPORT_SYMBOL_GPL(trace_seq_printf);

/**
 * trace_seq_bitmask - put a list of longs as a bitmask print output
 * @s:		trace sequence descriptor
 * @maskp:	points to an array of unsigned longs that represent a bitmask
 * @nmaskbits:	The number of bits that are valid in @maskp
 *
 * It returns 0 if the trace oversizes the buffer's free
 * space, 1 otherwise.
 *
 * Writes a ASCII representation of a bitmask string into @s.
 */
int
trace_seq_bitmask(struct trace_seq *s, const unsigned long *maskp,
		  int nmaskbits)
{
	int len = (PAGE_SIZE - 1) - s->len;
	int ret;

	if (s->full || !len)
		return 0;

	ret = bitmap_scnprintf(s->buffer, len, maskp, nmaskbits);
	s->len += ret;

	return 1;
}
EXPORT_SYMBOL_GPL(trace_seq_bitmask);

/**
 * trace_seq_vprintf - sequence printing of trace information
 * @s: trace sequence descriptor
 * @fmt: printf format string
 *
 * The tracer may use either sequence operations or its own
 * copy to user routines. To simplify formating of a trace
 * trace_seq_printf is used to store strings into a special
 * buffer (@s). Then the output may be either used by
 * the sequencer or pulled into another buffer.
 */
int
trace_seq_vprintf(struct trace_seq *s, const char *fmt, va_list args)
{
	int len = (PAGE_SIZE - 1) - s->len;
	int ret;

	if (s->full || !len)
		return 0;

	ret = vsnprintf(s->buffer + s->len, len, fmt, args);

	/* If we can't write it all, don't bother writing anything */
	if (ret >= len) {
		s->full = 1;
		return 0;
	}

	s->len += ret;

	return len;
}
EXPORT_SYMBOL_GPL(trace_seq_vprintf);

int trace_seq_bprintf(struct trace_seq *s, const char *fmt, const u32 *binary)
{
	int len = (PAGE_SIZE - 1) - s->len;
	int ret;

	if (s->full || !len)
		return 0;

	ret = bstr_printf(s->buffer + s->len, len, fmt, binary);

	/* If we can't write it all, don't bother writing anything */
	if (ret >= len) {
		s->full = 1;
		return 0;
	}

	s->len += ret;

	return len;
}

/**
 * trace_seq_puts - trace sequence printing of simple string
 * @s: trace sequence descriptor
 * @str: simple string to record
 *
 * The tracer may use either the sequence operations or its own
 * copy to user routines. This function records a simple string
 * into a special buffer (@s) for later retrieval by a sequencer
 * or other mechanism.
 */
int trace_seq_puts(struct trace_seq *s, const char *str)
{
	int len = strlen(str);

	if (s->full)
		return 0;

	if (len > ((PAGE_SIZE - 1) - s->len)) {
		s->full = 1;
		return 0;
	}

	memcpy(s->buffer + s->len, str, len);
	s->len += len;

	return len;
}

int trace_seq_putc(struct trace_seq *s, unsigned char c)
{
	if (s->full)
		return 0;

	if (s->len >= (PAGE_SIZE - 1)) {
		s->full = 1;
		return 0;
	}

	s->buffer[s->len++] = c;

	return 1;
}
EXPORT_SYMBOL(trace_seq_putc);

int trace_seq_putmem(struct trace_seq *s, const void *mem, size_t len)
{
	if (s->full)
		return 0;

	if (len > ((PAGE_SIZE - 1) - s->len)) {
		s->full = 1;
		return 0;
	}

	memcpy(s->buffer + s->len, mem, len);
	s->len += len;

	return len;
}

#define HEX_CHARS		(MAX_MEMHEX_BYTES*2 + 1)

int trace_seq_putmem_hex(struct trace_seq *s, const void *mem, size_t len)
{
	unsigned char hex[HEX_CHARS];
	const unsigned char *data = mem;
	int i, j;

	if (s->full)
		return 0;

#ifdef __BIG_ENDIAN
	for (i = 0, j = 0; i < len; i++) {
#else
	for (i = len-1, j = 0; i >= 0; i--) {
#endif
		hex[j++] = hex_asc_hi(data[i]);
		hex[j++] = hex_asc_lo(data[i]);
	}
	hex[j++] = ' ';

	return trace_seq_putmem(s, hex, j);
}

void *trace_seq_reserve(struct trace_seq *s, size_t len)
{
	void *ret;

	if (s->full)
		return NULL;

	if (len > ((PAGE_SIZE - 1) - s->len)) {
		s->full = 1;
		return NULL;
	}

	ret = s->buffer + s->len;
	s->len += len;

	return ret;
}

int trace_seq_path(struct trace_seq *s, const struct path *path)
{
	unsigned char *p;

	if (s->full)
		return 0;

	if (s->len >= (PAGE_SIZE - 1)) {
		s->full = 1;
		return 0;
	}

	p = d_path(path, s->buffer + s->len, PAGE_SIZE - s->len);
	if (!IS_ERR(p)) {
		p = mangle_path(s->buffer + s->len, p, "\n");
		if (p) {
			s->len = p - s->buffer;
			return 1;
		}
	} else {
		s->buffer[s->len++] = '?';
		return 1;
	}

	s->full = 1;
	return 0;
}

ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf, size_t cnt)
{
	int len;
	int ret;

	if (!cnt)
		return 0;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + s->readpos, cnt);
	if (ret == cnt)
		return -EFAULT;

	cnt -= ret;

	s->readpos += cnt;
	return cnt;
}
