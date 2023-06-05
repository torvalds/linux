/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SEQ_BUF_H
#define _LINUX_SEQ_BUF_H

#include <linux/fs.h>

/*
 * Trace sequences are used to allow a function to call several other functions
 * to create a string of data to use.
 */

/**
 * seq_buf - seq buffer structure
 * @buffer:	pointer to the buffer
 * @size:	size of the buffer
 * @len:	the amount of data inside the buffer
 * @readpos:	The next position to read in the buffer.
 */
struct seq_buf {
	char			*buffer;
	size_t			size;
	size_t			len;
	loff_t			readpos;
};

static inline void seq_buf_clear(struct seq_buf *s)
{
	s->len = 0;
	s->readpos = 0;
}

static inline void
seq_buf_init(struct seq_buf *s, char *buf, unsigned int size)
{
	s->buffer = buf;
	s->size = size;
	seq_buf_clear(s);
}

/*
 * seq_buf have a buffer that might overflow. When this happens
 * the len and size are set to be equal.
 */
static inline bool
seq_buf_has_overflowed(struct seq_buf *s)
{
	return s->len > s->size;
}

static inline void
seq_buf_set_overflow(struct seq_buf *s)
{
	s->len = s->size + 1;
}

/*
 * How much buffer is left on the seq_buf?
 */
static inline unsigned int
seq_buf_buffer_left(struct seq_buf *s)
{
	if (seq_buf_has_overflowed(s))
		return 0;

	return s->size - s->len;
}

/* How much buffer was written? */
static inline unsigned int seq_buf_used(struct seq_buf *s)
{
	return min(s->len, s->size);
}

/**
 * seq_buf_terminate - Make sure buffer is nul terminated
 * @s: the seq_buf descriptor to terminate.
 *
 * This makes sure that the buffer in @s is nul terminated and
 * safe to read as a string.
 *
 * Note, if this is called when the buffer has overflowed, then
 * the last byte of the buffer is zeroed, and the len will still
 * point passed it.
 *
 * After this function is called, s->buffer is safe to use
 * in string operations.
 */
static inline void seq_buf_terminate(struct seq_buf *s)
{
	if (WARN_ON(s->size == 0))
		return;

	if (seq_buf_buffer_left(s))
		s->buffer[s->len] = 0;
	else
		s->buffer[s->size - 1] = 0;
}

/**
 * seq_buf_get_buf - get buffer to write arbitrary data to
 * @s: the seq_buf handle
 * @bufp: the beginning of the buffer is stored here
 *
 * Return the number of bytes available in the buffer, or zero if
 * there's no space.
 */
static inline size_t seq_buf_get_buf(struct seq_buf *s, char **bufp)
{
	WARN_ON(s->len > s->size + 1);

	if (s->len < s->size) {
		*bufp = s->buffer + s->len;
		return s->size - s->len;
	}

	*bufp = NULL;
	return 0;
}

/**
 * seq_buf_commit - commit data to the buffer
 * @s: the seq_buf handle
 * @num: the number of bytes to commit
 *
 * Commit @num bytes of data written to a buffer previously acquired
 * by seq_buf_get.  To signal an error condition, or that the data
 * didn't fit in the available space, pass a negative @num value.
 */
static inline void seq_buf_commit(struct seq_buf *s, int num)
{
	if (num < 0) {
		seq_buf_set_overflow(s);
	} else {
		/* num must be negative on overflow */
		BUG_ON(s->len + num > s->size);
		s->len += num;
	}
}

extern __printf(2, 3)
int seq_buf_printf(struct seq_buf *s, const char *fmt, ...);
extern __printf(2, 0)
int seq_buf_vprintf(struct seq_buf *s, const char *fmt, va_list args);
extern int seq_buf_print_seq(struct seq_file *m, struct seq_buf *s);
extern int seq_buf_to_user(struct seq_buf *s, char __user *ubuf,
			   int cnt);
extern int seq_buf_puts(struct seq_buf *s, const char *str);
extern int seq_buf_putc(struct seq_buf *s, unsigned char c);
extern int seq_buf_putmem(struct seq_buf *s, const void *mem, unsigned int len);
extern int seq_buf_putmem_hex(struct seq_buf *s, const void *mem,
			      unsigned int len);
extern int seq_buf_path(struct seq_buf *s, const struct path *path, const char *esc);
extern int seq_buf_hex_dump(struct seq_buf *s, const char *prefix_str,
			    int prefix_type, int rowsize, int groupsize,
			    const void *buf, size_t len, bool ascii);

#ifdef CONFIG_BINARY_PRINTF
extern int
seq_buf_bprintf(struct seq_buf *s, const char *fmt, const u32 *binary);
#endif

void seq_buf_do_printk(struct seq_buf *s, const char *lvl);

#endif /* _LINUX_SEQ_BUF_H */
