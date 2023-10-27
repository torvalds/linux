// SPDX-License-Identifier: GPL-2.0
/*
 * seq_buf.c
 *
 * Copyright (C) 2014 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * The seq_buf is a handy tool that allows you to pass a descriptor around
 * to a buffer that other functions can write to. It is similar to the
 * seq_file functionality but has some differences.
 *
 * To use it, the seq_buf must be initialized with seq_buf_init().
 * This will set up the counters within the descriptor. You can call
 * seq_buf_init() more than once to reset the seq_buf to start
 * from scratch.
 */
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/seq_buf.h>

/**
 * seq_buf_can_fit - can the new data fit in the current buffer?
 * @s: the seq_buf descriptor
 * @len: The length to see if it can fit in the current buffer
 *
 * Returns true if there's enough unused space in the seq_buf buffer
 * to fit the amount of new data according to @len.
 */
static bool seq_buf_can_fit(struct seq_buf *s, size_t len)
{
	return s->len + len <= s->size;
}

/**
 * seq_buf_print_seq - move the contents of seq_buf into a seq_file
 * @m: the seq_file descriptor that is the destination
 * @s: the seq_buf descriptor that is the source.
 *
 * Returns zero on success, non zero otherwise
 */
int seq_buf_print_seq(struct seq_file *m, struct seq_buf *s)
{
	unsigned int len = seq_buf_used(s);

	return seq_write(m, s->buffer, len);
}

/**
 * seq_buf_vprintf - sequence printing of information.
 * @s: seq_buf descriptor
 * @fmt: printf format string
 * @args: va_list of arguments from a printf() type function
 *
 * Writes a vnprintf() format into the sequencce buffer.
 *
 * Returns zero on success, -1 on overflow.
 */
int seq_buf_vprintf(struct seq_buf *s, const char *fmt, va_list args)
{
	int len;

	WARN_ON(s->size == 0);

	if (s->len < s->size) {
		len = vsnprintf(s->buffer + s->len, s->size - s->len, fmt, args);
		if (s->len + len < s->size) {
			s->len += len;
			return 0;
		}
	}
	seq_buf_set_overflow(s);
	return -1;
}

/**
 * seq_buf_printf - sequence printing of information
 * @s: seq_buf descriptor
 * @fmt: printf format string
 *
 * Writes a printf() format into the sequence buffer.
 *
 * Returns zero on success, -1 on overflow.
 */
int seq_buf_printf(struct seq_buf *s, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = seq_buf_vprintf(s, fmt, ap);
	va_end(ap);

	return ret;
}
EXPORT_SYMBOL_GPL(seq_buf_printf);

/**
 * seq_buf_do_printk - printk seq_buf line by line
 * @s: seq_buf descriptor
 * @lvl: printk level
 *
 * printk()-s a multi-line sequential buffer line by line. The function
 * makes sure that the buffer in @s is nul terminated and safe to read
 * as a string.
 */
void seq_buf_do_printk(struct seq_buf *s, const char *lvl)
{
	const char *start, *lf;

	if (s->size == 0 || s->len == 0)
		return;

	start = seq_buf_str(s);
	while ((lf = strchr(start, '\n'))) {
		int len = lf - start + 1;

		printk("%s%.*s", lvl, len, start);
		start = ++lf;
	}

	/* No trailing LF */
	if (start < s->buffer + s->len)
		printk("%s%s\n", lvl, start);
}
EXPORT_SYMBOL_GPL(seq_buf_do_printk);

#ifdef CONFIG_BINARY_PRINTF
/**
 * seq_buf_bprintf - Write the printf string from binary arguments
 * @s: seq_buf descriptor
 * @fmt: The format string for the @binary arguments
 * @binary: The binary arguments for @fmt.
 *
 * When recording in a fast path, a printf may be recorded with just
 * saving the format and the arguments as they were passed to the
 * function, instead of wasting cycles converting the arguments into
 * ASCII characters. Instead, the arguments are saved in a 32 bit
 * word array that is defined by the format string constraints.
 *
 * This function will take the format and the binary array and finish
 * the conversion into the ASCII string within the buffer.
 *
 * Returns zero on success, -1 on overflow.
 */
int seq_buf_bprintf(struct seq_buf *s, const char *fmt, const u32 *binary)
{
	unsigned int len = seq_buf_buffer_left(s);
	int ret;

	WARN_ON(s->size == 0);

	if (s->len < s->size) {
		ret = bstr_printf(s->buffer + s->len, len, fmt, binary);
		if (s->len + ret < s->size) {
			s->len += ret;
			return 0;
		}
	}
	seq_buf_set_overflow(s);
	return -1;
}
#endif /* CONFIG_BINARY_PRINTF */

/**
 * seq_buf_puts - sequence printing of simple string
 * @s: seq_buf descriptor
 * @str: simple string to record
 *
 * Copy a simple string into the sequence buffer.
 *
 * Returns zero on success, -1 on overflow
 */
int seq_buf_puts(struct seq_buf *s, const char *str)
{
	size_t len = strlen(str);

	WARN_ON(s->size == 0);

	/* Add 1 to len for the trailing null byte which must be there */
	len += 1;

	if (seq_buf_can_fit(s, len)) {
		memcpy(s->buffer + s->len, str, len);
		/* Don't count the trailing null byte against the capacity */
		s->len += len - 1;
		return 0;
	}
	seq_buf_set_overflow(s);
	return -1;
}

/**
 * seq_buf_putc - sequence printing of simple character
 * @s: seq_buf descriptor
 * @c: simple character to record
 *
 * Copy a single character into the sequence buffer.
 *
 * Returns zero on success, -1 on overflow
 */
int seq_buf_putc(struct seq_buf *s, unsigned char c)
{
	WARN_ON(s->size == 0);

	if (seq_buf_can_fit(s, 1)) {
		s->buffer[s->len++] = c;
		return 0;
	}
	seq_buf_set_overflow(s);
	return -1;
}

/**
 * seq_buf_putmem - write raw data into the sequenc buffer
 * @s: seq_buf descriptor
 * @mem: The raw memory to copy into the buffer
 * @len: The length of the raw memory to copy (in bytes)
 *
 * There may be cases where raw memory needs to be written into the
 * buffer and a strcpy() would not work. Using this function allows
 * for such cases.
 *
 * Returns zero on success, -1 on overflow
 */
int seq_buf_putmem(struct seq_buf *s, const void *mem, unsigned int len)
{
	WARN_ON(s->size == 0);

	if (seq_buf_can_fit(s, len)) {
		memcpy(s->buffer + s->len, mem, len);
		s->len += len;
		return 0;
	}
	seq_buf_set_overflow(s);
	return -1;
}

#define MAX_MEMHEX_BYTES	8U
#define HEX_CHARS		(MAX_MEMHEX_BYTES*2 + 1)

/**
 * seq_buf_putmem_hex - write raw memory into the buffer in ASCII hex
 * @s: seq_buf descriptor
 * @mem: The raw memory to write its hex ASCII representation of
 * @len: The length of the raw memory to copy (in bytes)
 *
 * This is similar to seq_buf_putmem() except instead of just copying the
 * raw memory into the buffer it writes its ASCII representation of it
 * in hex characters.
 *
 * Returns zero on success, -1 on overflow
 */
int seq_buf_putmem_hex(struct seq_buf *s, const void *mem,
		       unsigned int len)
{
	unsigned char hex[HEX_CHARS];
	const unsigned char *data = mem;
	unsigned int start_len;
	int i, j;

	WARN_ON(s->size == 0);

	BUILD_BUG_ON(MAX_MEMHEX_BYTES * 2 >= HEX_CHARS);

	while (len) {
		start_len = min(len, MAX_MEMHEX_BYTES);
#ifdef __BIG_ENDIAN
		for (i = 0, j = 0; i < start_len; i++) {
#else
		for (i = start_len-1, j = 0; i >= 0; i--) {
#endif
			hex[j++] = hex_asc_hi(data[i]);
			hex[j++] = hex_asc_lo(data[i]);
		}
		if (WARN_ON_ONCE(j == 0 || j/2 > len))
			break;

		/* j increments twice per loop */
		hex[j++] = ' ';

		seq_buf_putmem(s, hex, j);
		if (seq_buf_has_overflowed(s))
			return -1;

		len -= start_len;
		data += start_len;
	}
	return 0;
}

/**
 * seq_buf_path - copy a path into the sequence buffer
 * @s: seq_buf descriptor
 * @path: path to write into the sequence buffer.
 * @esc: set of characters to escape in the output
 *
 * Write a path name into the sequence buffer.
 *
 * Returns the number of written bytes on success, -1 on overflow
 */
int seq_buf_path(struct seq_buf *s, const struct path *path, const char *esc)
{
	char *buf;
	size_t size = seq_buf_get_buf(s, &buf);
	int res = -1;

	WARN_ON(s->size == 0);

	if (size) {
		char *p = d_path(path, buf, size);
		if (!IS_ERR(p)) {
			char *end = mangle_path(buf, p, esc);
			if (end)
				res = end - buf;
		}
	}
	seq_buf_commit(s, res);

	return res;
}

/**
 * seq_buf_to_user - copy the sequence buffer to user space
 * @s: seq_buf descriptor
 * @ubuf: The userspace memory location to copy to
 * @start: The first byte in the buffer to copy
 * @cnt: The amount to copy
 *
 * Copies the sequence buffer into the userspace memory pointed to
 * by @ubuf. It starts from @start and writes up to @cnt characters
 * or until it reaches the end of the content in the buffer (@s->len),
 * whichever comes first.
 *
 * On success, it returns a positive number of the number of bytes
 * it copied.
 *
 * On failure it returns -EBUSY if all of the content in the
 * sequence has been already read, which includes nothing in the
 * sequence (@s->len == @start).
 *
 * Returns -EFAULT if the copy to userspace fails.
 */
int seq_buf_to_user(struct seq_buf *s, char __user *ubuf, size_t start, int cnt)
{
	int len;
	int ret;

	if (!cnt)
		return 0;

	len = seq_buf_used(s);

	if (len <= start)
		return -EBUSY;

	len -= start;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + start, cnt);
	if (ret == cnt)
		return -EFAULT;

	return cnt - ret;
}

/**
 * seq_buf_hex_dump - print formatted hex dump into the sequence buffer
 * @s: seq_buf descriptor
 * @prefix_str: string to prefix each line with;
 *  caller supplies trailing spaces for alignment if desired
 * @prefix_type: controls whether prefix of an offset, address, or none
 *  is printed (%DUMP_PREFIX_OFFSET, %DUMP_PREFIX_ADDRESS, %DUMP_PREFIX_NONE)
 * @rowsize: number of bytes to print per line; must be 16 or 32
 * @groupsize: number of bytes to print at a time (1, 2, 4, 8; default = 1)
 * @buf: data blob to dump
 * @len: number of bytes in the @buf
 * @ascii: include ASCII after the hex output
 *
 * Function is an analogue of print_hex_dump() and thus has similar interface.
 *
 * linebuf size is maximal length for one line.
 * 32 * 3 - maximum bytes per line, each printed into 2 chars + 1 for
 *	separating space
 * 2 - spaces separating hex dump and ascii representation
 * 32 - ascii representation
 * 1 - terminating '\0'
 *
 * Returns zero on success, -1 on overflow
 */
int seq_buf_hex_dump(struct seq_buf *s, const char *prefix_str, int prefix_type,
		     int rowsize, int groupsize,
		     const void *buf, size_t len, bool ascii)
{
	const u8 *ptr = buf;
	int i, linelen, remaining = len;
	unsigned char linebuf[32 * 3 + 2 + 32 + 1];
	int ret;

	if (rowsize != 16 && rowsize != 32)
		rowsize = 16;

	for (i = 0; i < len; i += rowsize) {
		linelen = min(remaining, rowsize);
		remaining -= rowsize;

		hex_dump_to_buffer(ptr + i, linelen, rowsize, groupsize,
				   linebuf, sizeof(linebuf), ascii);

		switch (prefix_type) {
		case DUMP_PREFIX_ADDRESS:
			ret = seq_buf_printf(s, "%s%p: %s\n",
			       prefix_str, ptr + i, linebuf);
			break;
		case DUMP_PREFIX_OFFSET:
			ret = seq_buf_printf(s, "%s%.8x: %s\n",
					     prefix_str, i, linebuf);
			break;
		default:
			ret = seq_buf_printf(s, "%s%s\n", prefix_str, linebuf);
			break;
		}
		if (ret)
			return ret;
	}
	return 0;
}
