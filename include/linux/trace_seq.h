/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TRACE_SEQ_H
#define _LINUX_TRACE_SEQ_H

#include <linux/seq_buf.h>

#include <asm/page.h>

/*
 * Trace sequences are used to allow a function to call several other functions
 * to create a string of data to use.
 *
 * Have the trace seq to be 8K which is typically PAGE_SIZE * 2 on
 * most architectures. The TRACE_SEQ_BUFFER_SIZE (which is
 * TRACE_SEQ_SIZE minus the other fields of trace_seq), is the
 * max size the output of a trace event may be.
 */

#define TRACE_SEQ_SIZE		8192
#define TRACE_SEQ_BUFFER_SIZE	(TRACE_SEQ_SIZE - \
	(sizeof(struct seq_buf) + sizeof(size_t) + sizeof(int)))

struct trace_seq {
	char			buffer[TRACE_SEQ_BUFFER_SIZE];
	struct seq_buf		seq;
	size_t			readpos;
	int			full;
};

static inline void
trace_seq_init(struct trace_seq *s)
{
	seq_buf_init(&s->seq, s->buffer, TRACE_SEQ_BUFFER_SIZE);
	s->full = 0;
	s->readpos = 0;
}

/**
 * trace_seq_used - amount of actual data written to buffer
 * @s: trace sequence descriptor
 *
 * Returns the amount of data written to the buffer.
 *
 * IMPORTANT!
 *
 * Use this instead of @s->seq.len if you need to pass the amount
 * of data from the buffer to another buffer (userspace, or what not).
 * The @s->seq.len on overflow is bigger than the buffer size and
 * using it can cause access to undefined memory.
 */
static inline int trace_seq_used(struct trace_seq *s)
{
	return seq_buf_used(&s->seq);
}

/**
 * trace_seq_buffer_ptr - return pointer to next location in buffer
 * @s: trace sequence descriptor
 *
 * Returns the pointer to the buffer where the next write to
 * the buffer will happen. This is useful to save the location
 * that is about to be written to and then return the result
 * of that write.
 */
static inline char *
trace_seq_buffer_ptr(struct trace_seq *s)
{
	return s->buffer + seq_buf_used(&s->seq);
}

/**
 * trace_seq_has_overflowed - return true if the trace_seq took too much
 * @s: trace sequence descriptor
 *
 * Returns true if too much data was added to the trace_seq and it is
 * now full and will not take anymore.
 */
static inline bool trace_seq_has_overflowed(struct trace_seq *s)
{
	return s->full || seq_buf_has_overflowed(&s->seq);
}

/*
 * Currently only defined when tracing is enabled.
 */
#ifdef CONFIG_TRACING
extern __printf(2, 3)
void trace_seq_printf(struct trace_seq *s, const char *fmt, ...);
extern __printf(2, 0)
void trace_seq_vprintf(struct trace_seq *s, const char *fmt, va_list args);
extern __printf(2, 0)
void trace_seq_bprintf(struct trace_seq *s, const char *fmt, const u32 *binary);
extern int trace_print_seq(struct seq_file *m, struct trace_seq *s);
extern int trace_seq_to_user(struct trace_seq *s, char __user *ubuf,
			     int cnt);
extern void trace_seq_puts(struct trace_seq *s, const char *str);
extern void trace_seq_putc(struct trace_seq *s, unsigned char c);
extern void trace_seq_putmem(struct trace_seq *s, const void *mem, unsigned int len);
extern void trace_seq_putmem_hex(struct trace_seq *s, const void *mem,
				unsigned int len);
extern int trace_seq_path(struct trace_seq *s, const struct path *path);

extern void trace_seq_bitmask(struct trace_seq *s, const unsigned long *maskp,
			     int nmaskbits);

extern int trace_seq_hex_dump(struct trace_seq *s, const char *prefix_str,
			      int prefix_type, int rowsize, int groupsize,
			      const void *buf, size_t len, bool ascii);
char *trace_seq_acquire(struct trace_seq *s, unsigned int len);

#else /* CONFIG_TRACING */
static inline __printf(2, 3)
void trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
{
}
static inline __printf(2, 0)
void trace_seq_bprintf(struct trace_seq *s, const char *fmt, const u32 *binary)
{
}

static inline void
trace_seq_bitmask(struct trace_seq *s, const unsigned long *maskp,
		  int nmaskbits)
{
}

static inline int trace_print_seq(struct seq_file *m, struct trace_seq *s)
{
	return 0;
}
static inline int trace_seq_to_user(struct trace_seq *s, char __user *ubuf,
				    int cnt)
{
	return 0;
}
static inline void trace_seq_puts(struct trace_seq *s, const char *str)
{
}
static inline void trace_seq_putc(struct trace_seq *s, unsigned char c)
{
}
static inline void
trace_seq_putmem(struct trace_seq *s, const void *mem, unsigned int len)
{
}
static inline void trace_seq_putmem_hex(struct trace_seq *s, const void *mem,
				       unsigned int len)
{
}
static inline int trace_seq_path(struct trace_seq *s, const struct path *path)
{
	return 0;
}
static inline char *trace_seq_acquire(struct trace_seq *s, unsigned int len)
{
	return NULL;
}
#endif /* CONFIG_TRACING */

#endif /* _LINUX_TRACE_SEQ_H */
