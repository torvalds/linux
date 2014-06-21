/*
 * trace_seq.c
 *
 * Copyright (C) 2008-2014 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * The trace_seq is a handy tool that allows you to pass a descriptor around
 * to a buffer that other functions can write to. It is similar to the
 * seq_file functionality but has some differences.
 *
 * To use it, the trace_seq must be initialized with trace_seq_init().
 * This will set up the counters within the descriptor. You can call
 * trace_seq_init() more than once to reset the trace_seq to start
 * from scratch.
 * 
 * The buffer size is currently PAGE_SIZE, although it may become dynamic
 * in the future.
 *
 * A write to the buffer will either succed or fail. That is, unlike
 * sprintf() there will not be a partial write (well it may write into
 * the buffer but it wont update the pointers). This allows users to
 * try to write something into the trace_seq buffer and if it fails
 * they can flush it and try again.
 *
 */
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/trace_seq.h>

/* How much buffer is left on the trace_seq? */
#define TRACE_SEQ_BUF_LEFT(s) ((PAGE_SIZE - 1) - (s)->len)

/* How much buffer is written? */
#define TRACE_SEQ_BUF_USED(s) min((s)->len, (unsigned int)(PAGE_SIZE - 1))

/**
 * trace_print_seq - move the contents of trace_seq into a seq_file
 * @m: the seq_file descriptor that is the destination
 * @s: the trace_seq descriptor that is the source.
 *
 * Returns 0 on success and non zero on error. If it succeeds to
 * write to the seq_file it will reset the trace_seq, otherwise
 * it does not modify the trace_seq to let the caller try again.
 */
int trace_print_seq(struct seq_file *m, struct trace_seq *s)
{
	unsigned int len = TRACE_SEQ_BUF_USED(s);
	int ret;

	ret = seq_write(m, s->buffer, len);

	/*
	 * Only reset this buffer if we successfully wrote to the
	 * seq_file buffer. This lets the caller try again or
	 * do something else with the contents.
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
 * The tracer may use either sequence operations or its own
 * copy to user routines. To simplify formating of a trace
 * trace_seq_printf() is used to store strings into a special
 * buffer (@s). Then the output may be either used by
 * the sequencer or pulled into another buffer.
 *
 * Returns 1 if we successfully written all the contents to
 *   the buffer.
  * Returns 0 if we the length to write is bigger than the
 *   reserved buffer space. In this case, nothing gets written.
 */
int trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
{
	unsigned int len = TRACE_SEQ_BUF_LEFT(s);
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
 * trace_seq_bitmask - write a bitmask array in its ASCII representation
 * @s:		trace sequence descriptor
 * @maskp:	points to an array of unsigned longs that represent a bitmask
 * @nmaskbits:	The number of bits that are valid in @maskp
 *
 * Writes a ASCII representation of a bitmask string into @s.
 *
 * Returns 1 if we successfully written all the contents to
 *   the buffer.
 * Returns 0 if we the length to write is bigger than the
 *   reserved buffer space. In this case, nothing gets written.
 */
int trace_seq_bitmask(struct trace_seq *s, const unsigned long *maskp,
		      int nmaskbits)
{
	unsigned int len = TRACE_SEQ_BUF_LEFT(s);
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
 *
 * Returns how much it wrote to the buffer.
 */
int trace_seq_vprintf(struct trace_seq *s, const char *fmt, va_list args)
{
	unsigned int len = TRACE_SEQ_BUF_LEFT(s);
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

/**
 * trace_seq_bprintf - Write the printf string from binary arguments
 * @s: trace sequence descriptor
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
 * Returns how much it wrote to the buffer.
 */
int trace_seq_bprintf(struct trace_seq *s, const char *fmt, const u32 *binary)
{
	unsigned int len = TRACE_SEQ_BUF_LEFT(s);
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
EXPORT_SYMBOL_GPL(trace_seq_bprintf);

/**
 * trace_seq_puts - trace sequence printing of simple string
 * @s: trace sequence descriptor
 * @str: simple string to record
 *
 * The tracer may use either the sequence operations or its own
 * copy to user routines. This function records a simple string
 * into a special buffer (@s) for later retrieval by a sequencer
 * or other mechanism.
 *
 * Returns how much it wrote to the buffer.
 */
int trace_seq_puts(struct trace_seq *s, const char *str)
{
	unsigned int len = strlen(str);

	if (s->full)
		return 0;

	if (len > TRACE_SEQ_BUF_LEFT(s)) {
		s->full = 1;
		return 0;
	}

	memcpy(s->buffer + s->len, str, len);
	s->len += len;

	return len;
}
EXPORT_SYMBOL_GPL(trace_seq_puts);

/**
 * trace_seq_putc - trace sequence printing of simple character
 * @s: trace sequence descriptor
 * @c: simple character to record
 *
 * The tracer may use either the sequence operations or its own
 * copy to user routines. This function records a simple charater
 * into a special buffer (@s) for later retrieval by a sequencer
 * or other mechanism.
 *
 * Returns how much it wrote to the buffer.
 */
int trace_seq_putc(struct trace_seq *s, unsigned char c)
{
	if (s->full)
		return 0;

	if (TRACE_SEQ_BUF_LEFT(s) < 1) {
		s->full = 1;
		return 0;
	}

	s->buffer[s->len++] = c;

	return 1;
}
EXPORT_SYMBOL_GPL(trace_seq_putc);

/**
 * trace_seq_putmem - write raw data into the trace_seq buffer
 * @s: trace sequence descriptor
 * @mem: The raw memory to copy into the buffer
 * @len: The length of the raw memory to copy (in bytes)
 *
 * There may be cases where raw memory needs to be written into the
 * buffer and a strcpy() would not work. Using this function allows
 * for such cases.
 *
 * Returns how much it wrote to the buffer.
 */
int trace_seq_putmem(struct trace_seq *s, const void *mem, unsigned int len)
{
	if (s->full)
		return 0;

	if (len > TRACE_SEQ_BUF_LEFT(s)) {
		s->full = 1;
		return 0;
	}

	memcpy(s->buffer + s->len, mem, len);
	s->len += len;

	return len;
}
EXPORT_SYMBOL_GPL(trace_seq_putmem);

#define MAX_MEMHEX_BYTES	8U
#define HEX_CHARS		(MAX_MEMHEX_BYTES*2 + 1)

/**
 * trace_seq_putmem_hex - write raw memory into the buffer in ASCII hex
 * @s: trace sequence descriptor
 * @mem: The raw memory to write its hex ASCII representation of
 * @len: The length of the raw memory to copy (in bytes)
 *
 * This is similar to trace_seq_putmem() except instead of just copying the
 * raw memory into the buffer it writes its ASCII representation of it
 * in hex characters.
 *
 * Returns how much it wrote to the buffer.
 */
int trace_seq_putmem_hex(struct trace_seq *s, const void *mem,
			 unsigned int len)
{
	unsigned char hex[HEX_CHARS];
	const unsigned char *data = mem;
	unsigned int start_len;
	int i, j;
	int cnt = 0;

	if (s->full)
		return 0;

	while (len) {
		start_len = min(len, HEX_CHARS - 1);
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
		len -= j / 2;
		hex[j++] = ' ';

		cnt += trace_seq_putmem(s, hex, j);
	}
	return cnt;
}
EXPORT_SYMBOL_GPL(trace_seq_putmem_hex);

/**
 * trace_seq_reserve - reserve space on the sequence buffer
 * @s: trace sequence descriptor
 * @len: The amount to reserver.
 *
 * If for some reason there is a need to save some space on the
 * buffer to fill in later, this function is used for that purpose.
 * The given length will be reserved and the pointer to that
 * location on the buffer is returned, unless there is not enough
 * buffer left to hold the given length then NULL is returned.
 */
void *trace_seq_reserve(struct trace_seq *s, unsigned int len)
{
	void *ret;

	if (s->full)
		return NULL;

	if (len > TRACE_SEQ_BUF_LEFT(s)) {
		s->full = 1;
		return NULL;
	}

	ret = s->buffer + s->len;
	s->len += len;

	return ret;
}
EXPORT_SYMBOL_GPL(trace_seq_reserve);

/**
 * trace_seq_path - copy a path into the sequence buffer
 * @s: trace sequence descriptor
 * @path: path to write into the sequence buffer.
 *
 * Write a path name into the sequence buffer.
 *
 * Returns 1 if we successfully written all the contents to
 *   the buffer.
 * Returns 0 if we the length to write is bigger than the
 *   reserved buffer space. In this case, nothing gets written.
 */
int trace_seq_path(struct trace_seq *s, const struct path *path)
{
	unsigned char *p;

	if (s->full)
		return 0;

	if (TRACE_SEQ_BUF_LEFT(s) < 1) {
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
EXPORT_SYMBOL_GPL(trace_seq_path);

/**
 * trace_seq_to_user - copy the squence buffer to user space
 * @s: trace sequence descriptor
 * @ubuf: The userspace memory location to copy to
 * @cnt: The amount to copy
 *
 * Copies the sequence buffer into the userspace memory pointed to
 * by @ubuf. It starts from the last read position (@s->readpos)
 * and writes up to @cnt characters or till it reaches the end of
 * the content in the buffer (@s->len), which ever comes first.
 *
 * On success, it returns a positive number of the number of bytes
 * it copied.
 *
 * On failure it returns -EBUSY if all of the content in the
 * sequence has been already read, which includes nothing in the
 * sequenc (@s->len == @s->readpos).
 *
 * Returns -EFAULT if the copy to userspace fails.
 */
int trace_seq_to_user(struct trace_seq *s, char __user *ubuf, int cnt)
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
EXPORT_SYMBOL_GPL(trace_seq_to_user);
