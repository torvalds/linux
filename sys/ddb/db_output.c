/*	$OpenBSD: db_output.c,v 1.37 2021/06/10 12:33:48 bluhm Exp $	*/
/*	$NetBSD: db_output.c,v 1.13 1996/04/01 17:27:14 christos Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1993,1992,1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 * Printf and character output for debugger.
 */
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/stdarg.h>
#include <sys/systm.h>
#include <sys/stacktrace.h>

#include <dev/cons.h>

#include <machine/db_machdep.h>

#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_access.h>
#include <ddb/db_interface.h>
#include <ddb/db_sym.h>
#include <ddb/db_var.h>

/*
 *	Character output - tracks position in line.
 *	To do this correctly, we should know how wide
 *	the output device is - then we could zero
 *	the line position when the output device wraps
 *	around to the start of the next line.
 *
 *	Instead, we count the number of spaces printed
 *	since the last printing character so that we
 *	don't print trailing spaces.  This avoids most
 *	of the wraparounds.
 */

#ifndef	DB_MAX_LINE
#define	DB_MAX_LINE		24	/* maximum line */
#define DB_MAX_WIDTH		80	/* maximum width */
#endif	/* DB_MAX_LINE */

#define DB_MIN_MAX_WIDTH	20	/* minimum max width */
#define DB_MIN_MAX_LINE		3	/* minimum max line */
#define CTRL(c)			((c) & 0xff)

int	db_output_position = 0;		/* output column */
int	db_output_line = 0;		/* output line number */
int	db_last_non_space = 0;		/* last non-space character */
int	db_tab_stop_width = 8;		/* how wide are tab stops? */
#define	NEXT_TAB(i) \
	((((i) + db_tab_stop_width) / db_tab_stop_width) * db_tab_stop_width)
int	db_max_line = DB_MAX_LINE;	/* output max lines */
int	db_max_width = DB_MAX_WIDTH;	/* output line width */
int	db_radix = 16;			/* output numbers radix */

static void db_more(void);

/*
 * Force pending whitespace.
 */
void
db_force_whitespace(void)
{
	int last_print, next_tab;

	last_print = db_last_non_space;
	while (last_print < db_output_position) {
		next_tab = NEXT_TAB(last_print);
		if (next_tab <= db_output_position) {
			while (last_print < next_tab) { /* DON'T send a tab!!! */
				cnputc(' ');
				last_print++;
			}
		} else {
			cnputc(' ');
			last_print++;
		}
	}
	db_last_non_space = db_output_position;
}

static void
db_more(void)
{
	char *p;
	int quit_output = 0;

	for (p = "--db_more--"; *p; p++)
		cnputc(*p);
	switch(cngetc()) {
	case ' ':
		db_output_line = 0;
		break;
	case 'q':
	case CTRL('c'):
		db_output_line = 0;
		quit_output = 1;
		break;
	default:
		db_output_line--;
		break;
	}
	p = "\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b";
	while (*p)
		cnputc(*p++);
	if (quit_output) {
		db_error(0);
		/* NOTREACHED */
	}
}

/*
 * Output character.  Buffer whitespace.
 */
void
db_putchar(int c)
{
	if (db_max_line >= DB_MIN_MAX_LINE && db_output_line >= db_max_line-1)
		db_more();

	if (c > ' ' && c <= '~') {
		/*
		 * Printing character.
		 * If we have spaces to print, print them first.
		 * Use tabs if possible.
		 */
		db_force_whitespace();
		cnputc(c);
		db_output_position++;
		if (db_max_width >= DB_MIN_MAX_WIDTH &&
		    db_output_position >= db_max_width-1) {
			/* auto new line */
			cnputc('\n');
			db_output_position = 0;
			db_last_non_space = 0;
			db_output_line++;
		}
		db_last_non_space = db_output_position;
	} else if (c == '\n') {
		/* Return */
		cnputc(c);
		db_output_position = 0;
		db_last_non_space = 0;
		db_output_line++;
	} else if (c == '\t') {
		/* assume tabs every 8 positions */
		db_output_position = NEXT_TAB(db_output_position);
	} else if (c == ' ') {
		/* space */
		db_output_position++;
	} else if (c == '\007') {
		/* bell */
		cnputc(c);
	}
	/* other characters are assumed non-printing */
}

/*
 * Return output position
 */
int
db_print_position(void)
{
	return (db_output_position);
}

/*
 * End line if too long.
 */
void
db_end_line(int space)
{
	if (db_output_position >= db_max_width - space)
		db_printf("\n");
}

char *
db_format(char *buf, size_t bufsize, long val, int format, int alt, int width)
{
	const char *fmt;

	if (format == DB_FORMAT_Z || db_radix == 16)
		fmt = alt ? "-%#*lx" : "-%*lx";
	else if (db_radix == 8)
		fmt = alt ? "-%#*lo" : "-%*lo";
	else
		fmt = alt ? "-%#*lu" : "-%*lu";

	/* The leading '-' is a nasty (and beautiful) idea from NetBSD */
	if (val < 0 && format != DB_FORMAT_N)
		val = -val;
	else
		fmt++;

	snprintf(buf, bufsize, fmt, width, val);
	return (buf);
}

void
db_stack_dump(void)
{
	static struct cpu_info *intrace = NULL;
	struct cpu_info *tracing, *ci = curcpu();

	tracing = atomic_cas_ptr(&intrace, NULL, ci);
	if (tracing != NULL) {
		if (tracing == ci)
			printf("Faulted in traceback, aborting...\n");
		else
			printf("Parallel traceback, suppressed...\n");
		return;
	}

	printf("Starting stack trace...\n");
	db_stack_trace_print((db_expr_t)__builtin_frame_address(0), 1,
	    256 /* low limit */, "", printf);
	printf("End of stack trace.\n");
	membar_producer();
	intrace = NULL;
}

void
stacktrace_print(struct stacktrace *st, int (*pr)(const char *, ...))
{
	unsigned int i;

	for (i = 0; i < st->st_count; i++) {
		(*pr)("#%-2u ", i);
		db_printsym(st->st_pc[i], DB_STGY_PROC, pr);
		(*pr)("\n");
	}
	if (st->st_count == 0)
		(*pr)("<empty stack trace>\n");
}

void
db_resize(int cols, int rows)
{
	db_max_width = cols;
	db_max_line = rows;
}
