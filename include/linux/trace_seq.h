#ifndef _LINUX_TRACE_SEQ_H
#define _LINUX_TRACE_SEQ_H

#include <linux/fs.h>

#include <asm/page.h>

/*
 * Trace sequences are used to allow a function to call several other functions
 * to create a string of data to use (up to a max of PAGE_SIZE).
 */

struct trace_seq {
	unsigned char		buffer[PAGE_SIZE];
	unsigned int		len;
	unsigned int		readpos;
	int			full;
};

static inline void
trace_seq_init(struct trace_seq *s)
{
	s->len = 0;
	s->readpos = 0;
	s->full = 0;
}

#define MAX_MEMHEX_BYTES	8

/*
 * Currently only defined when tracing is enabled.
 */
#ifdef CONFIG_TRACING
extern __printf(2, 3)
int trace_seq_printf(struct trace_seq *s, const char *fmt, ...);
extern __printf(2, 0)
int trace_seq_vprintf(struct trace_seq *s, const char *fmt, va_list args);
extern int
trace_seq_bprintf(struct trace_seq *s, const char *fmt, const u32 *binary);
extern int trace_print_seq(struct seq_file *m, struct trace_seq *s);
extern int trace_seq_to_user(struct trace_seq *s, char __user *ubuf,
			     int cnt);
extern int trace_seq_puts(struct trace_seq *s, const char *str);
extern int trace_seq_putc(struct trace_seq *s, unsigned char c);
extern int trace_seq_putmem(struct trace_seq *s, const void *mem, unsigned int len);
extern int trace_seq_putmem_hex(struct trace_seq *s, const void *mem,
				unsigned int len);
extern void *trace_seq_reserve(struct trace_seq *s, unsigned int len);
extern int trace_seq_path(struct trace_seq *s, const struct path *path);

extern int trace_seq_bitmask(struct trace_seq *s, const unsigned long *maskp,
			     int nmaskbits);

#else /* CONFIG_TRACING */
static inline int trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
{
	return 0;
}
static inline int
trace_seq_bprintf(struct trace_seq *s, const char *fmt, const u32 *binary)
{
	return 0;
}

static inline int
trace_seq_bitmask(struct trace_seq *s, const unsigned long *maskp,
		  int nmaskbits)
{
	return 0;
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
static inline int trace_seq_puts(struct trace_seq *s, const char *str)
{
	return 0;
}
static inline int trace_seq_putc(struct trace_seq *s, unsigned char c)
{
	return 0;
}
static inline int
trace_seq_putmem(struct trace_seq *s, const void *mem, unsigned int len)
{
	return 0;
}
static inline int trace_seq_putmem_hex(struct trace_seq *s, const void *mem,
				       unsigned int len)
{
	return 0;
}
static inline void *trace_seq_reserve(struct trace_seq *s, unsigned int len)
{
	return NULL;
}
static inline int trace_seq_path(struct trace_seq *s, const struct path *path)
{
	return 0;
}
#endif /* CONFIG_TRACING */

#endif /* _LINUX_TRACE_SEQ_H */
