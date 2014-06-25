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
	unsigned char		*buffer;
	unsigned int		size;
	unsigned int		len;
	unsigned int		readpos;
};

static inline void
seq_buf_init(struct seq_buf *s, unsigned char *buf, unsigned int size)
{
	s->buffer = buf;
	s->size = size;
	s->len = 0;
	s->readpos = 0;
}

/*
 * seq_buf have a buffer that might overflow. When this happens
 * the len and size are set to be equal.
 */
static inline bool
seq_buf_has_overflowed(struct seq_buf *s)
{
	return s->len == s->size;
}

static inline void
seq_buf_set_overflow(struct seq_buf *s)
{
	s->len = s->size;
}

/*
 * How much buffer is left on the seq_buf?
 */
static inline unsigned int
seq_buf_buffer_left(struct seq_buf *s)
{
	if (seq_buf_has_overflowed(s))
		return 0;

	return (s->size - 1) - s->len;
}

extern __printf(2, 3)
int seq_buf_printf(struct seq_buf *s, const char *fmt, ...);
extern __printf(2, 0)
int seq_buf_vprintf(struct seq_buf *s, const char *fmt, va_list args);
extern int
seq_buf_bprintf(struct seq_buf *s, const char *fmt, const u32 *binary);
extern int seq_buf_print_seq(struct seq_file *m, struct seq_buf *s);
extern int seq_buf_to_user(struct seq_buf *s, char __user *ubuf,
			   int cnt);
extern int seq_buf_puts(struct seq_buf *s, const char *str);
extern int seq_buf_putc(struct seq_buf *s, unsigned char c);
extern int seq_buf_putmem(struct seq_buf *s, const void *mem, unsigned int len);
extern int seq_buf_putmem_hex(struct seq_buf *s, const void *mem,
			      unsigned int len);
extern int seq_buf_path(struct seq_buf *s, const struct path *path);

extern int seq_buf_bitmask(struct seq_buf *s, const unsigned long *maskp,
			   int nmaskbits);

#endif /* _LINUX_SEQ_BUF_H */
