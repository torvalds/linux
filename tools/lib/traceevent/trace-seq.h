// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2009, 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#ifndef _TRACE_SEQ_H
#define _TRACE_SEQ_H

#include <stdarg.h>
#include <stdio.h>

/* ----------------------- trace_seq ----------------------- */

#ifndef TRACE_SEQ_BUF_SIZE
#define TRACE_SEQ_BUF_SIZE 4096
#endif

enum trace_seq_fail {
	TRACE_SEQ__GOOD,
	TRACE_SEQ__BUFFER_POISONED,
	TRACE_SEQ__MEM_ALLOC_FAILED,
};

/*
 * Trace sequences are used to allow a function to call several other functions
 * to create a string of data to use (up to a max of PAGE_SIZE).
 */

struct trace_seq {
	char			*buffer;
	unsigned int		buffer_size;
	unsigned int		len;
	unsigned int		readpos;
	enum trace_seq_fail	state;
};

void trace_seq_init(struct trace_seq *s);
void trace_seq_reset(struct trace_seq *s);
void trace_seq_destroy(struct trace_seq *s);

extern int trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
extern int trace_seq_vprintf(struct trace_seq *s, const char *fmt, va_list args)
	__attribute__ ((format (printf, 2, 0)));

extern int trace_seq_puts(struct trace_seq *s, const char *str);
extern int trace_seq_putc(struct trace_seq *s, unsigned char c);

extern void trace_seq_terminate(struct trace_seq *s);

extern int trace_seq_do_fprintf(struct trace_seq *s, FILE *fp);
extern int trace_seq_do_printf(struct trace_seq *s);

#endif /* _TRACE_SEQ_H */
