/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
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
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */
/*
 * 	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

/*
 * Printf and character output for debugger.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/stdarg.h>

#include <ddb/ddb.h>
#include <ddb/db_output.h>

struct dbputchar_arg {
	size_t	da_nbufr;
	size_t	da_remain;
	char	*da_pbufr;
	char	*da_pnext;
};

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
static int	db_output_position = 0;		/* output column */
static int	db_last_non_space = 0;		/* last non-space character */
db_expr_t	db_tab_stop_width = 8;		/* how wide are tab stops? */
#define	NEXT_TAB(i) rounddown((i) + db_tab_stop_width, db_tab_stop_width)
db_expr_t	db_max_width = 79;		/* output line width */
db_expr_t	db_lines_per_page = 20;		/* lines per page */
volatile int	db_pager_quit;			/* user requested quit */
static int	db_newlines;			/* # lines this page */
static int	db_maxlines;			/* max lines/page when paging */
static int	ddb_use_printf = 0;
SYSCTL_INT(_debug, OID_AUTO, ddb_use_printf, CTLFLAG_RW, &ddb_use_printf, 0,
    "use printf for all ddb output");

static void	db_putc(int c);
static void	db_puts(const char *str);
static void	db_putchar(int c, void *arg);
static void	db_pager(void);

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
			db_capture_writech(' ');
			last_print++;
		}
	    }
	    else {
		cnputc(' ');
		db_capture_writech(' ');
		last_print++;
	    }
	}
	db_last_non_space = db_output_position;
}

/*
 * Output character.  Buffer whitespace.
 */
static void
db_putchar(int c, void *arg)
{
	struct dbputchar_arg *dap = arg;

	if (dap->da_pbufr == NULL) {

		 /* No bufferized output is provided. */
		db_putc(c);
	} else {

		*dap->da_pnext++ = c;
		dap->da_remain--;

		/* Leave always the buffer 0 terminated. */
		*dap->da_pnext = '\0';

		/* Check if the buffer needs to be flushed. */
		if (dap->da_remain < 2 || c == '\n') {
			db_puts(dap->da_pbufr);
			dap->da_pnext = dap->da_pbufr;
			dap->da_remain = dap->da_nbufr;
			*dap->da_pnext = '\0';
		}
	}
}

static void
db_putc(int c)
{

	/*
	 * If not in the debugger or the user requests it, output data to
	 * both the console and the message buffer.
	 */
	if (!kdb_active || ddb_use_printf) {
		printf("%c", c);
		if (!kdb_active)
			return;
		if (c == '\r' || c == '\n')
			db_check_interrupt();
		if (c == '\n' && db_maxlines > 0) {
			db_newlines++;
			if (db_newlines >= db_maxlines)
				db_pager();
		}
		return;
	}

	/* Otherwise, output data directly to the console. */
	if (c > ' ' && c <= '~') {
	    /*
	     * Printing character.
	     * If we have spaces to print, print them first.
	     * Use tabs if possible.
	     */
	    db_force_whitespace();
	    cnputc(c);
	    db_capture_writech(c);
	    db_output_position++;
	    db_last_non_space = db_output_position;
	}
	else if (c == '\n') {
	    /* Newline */
	    cnputc(c);
	    db_capture_writech(c);
	    db_output_position = 0;
	    db_last_non_space = 0;
	    db_check_interrupt();
	    if (db_maxlines > 0) {
		    db_newlines++;
		    if (db_newlines >= db_maxlines)
			    db_pager();
	    }
	}
	else if (c == '\r') {
	    /* Return */
	    cnputc(c);
	    db_capture_writech(c);
	    db_output_position = 0;
	    db_last_non_space = 0;
	    db_check_interrupt();
	}
	else if (c == '\t') {
	    /* assume tabs every 8 positions */
	    db_output_position = NEXT_TAB(db_output_position);
	}
	else if (c == ' ') {
	    /* space */
	    db_output_position++;
	}
	else if (c == '\007') {
	    /* bell */
	    cnputc(c);
	    /* No need to beep in a log: db_capture_writech(c); */
	}
	/* other characters are assumed non-printing */
}

static void
db_puts(const char *str)
{
	int i;

	for (i = 0; str[i] != '\0'; i++)
		db_putc(str[i]);
}

/*
 * Turn on the pager.
 */
void
db_enable_pager(void)
{
	if (db_maxlines == 0) {
		db_maxlines = db_lines_per_page;
		db_newlines = 0;
		db_pager_quit = 0;
	}
}

/*
 * Turn off the pager.
 */
void
db_disable_pager(void)
{
	db_maxlines = 0;
}

/*
 * A simple paging callout function.  It supports several simple more(1)-like
 * commands as well as a quit command that sets db_pager_quit which db
 * commands can poll to see if they should terminate early.
 */
void
db_pager(void)
{
	int c, done;

	db_capture_enterpager();
	db_printf("--More--\r");
	done = 0;
	while (!done) {
		c = cngetc();
		switch (c) {
		case 'e':
		case 'j':
		case '\n':
			/* Just one more line. */
			db_maxlines = 1;
			done++;
			break;
		case 'd':
			/* Half a page. */
			db_maxlines = db_lines_per_page / 2;
			done++;
			break;
		case 'f':
		case ' ':
			/* Another page. */
			db_maxlines = db_lines_per_page;
			done++;
			break;
		case 'q':
		case 'Q':
		case 'x':
		case 'X':
			/* Quit */
			db_maxlines = 0;
			db_pager_quit = 1;
			done++;
			break;
#if 0
			/* FALLTHROUGH */
		default:
			cnputc('\007');
#endif
		}
	}
	db_printf("        ");
	db_force_whitespace();
	db_printf("\r");
	db_newlines = 0;
	db_capture_exitpager();
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
 * Printing
 */
int
db_printf(const char *fmt, ...)
{
#ifdef DDB_BUFR_SIZE
	char bufr[DDB_BUFR_SIZE];
#endif
	struct dbputchar_arg dca;
	va_list	listp;
	int retval;

#ifdef DDB_BUFR_SIZE
	dca.da_pbufr = bufr;
	dca.da_pnext = dca.da_pbufr;
	dca.da_nbufr = sizeof(bufr);
	dca.da_remain = sizeof(bufr);
	*dca.da_pnext = '\0';
#else
	dca.da_pbufr = NULL;
#endif

	va_start(listp, fmt);
	retval = kvprintf (fmt, db_putchar, &dca, db_radix, listp);
	va_end(listp);

#ifdef DDB_BUFR_SIZE
	if (*dca.da_pbufr != '\0')
		db_puts(dca.da_pbufr);
#endif
	return (retval);
}

int db_indent;

void
db_iprintf(const char *fmt,...)
{
#ifdef DDB_BUFR_SIZE
	char bufr[DDB_BUFR_SIZE];
#endif
	struct dbputchar_arg dca;
	int i;
	va_list listp;

	for (i = db_indent; i >= 8; i -= 8)
		db_printf("\t");
	while (--i >= 0)
		db_printf(" ");

#ifdef DDB_BUFR_SIZE
	dca.da_pbufr = bufr;
	dca.da_pnext = dca.da_pbufr;
	dca.da_nbufr = sizeof(bufr);
	dca.da_remain = sizeof(bufr);
	*dca.da_pnext = '\0';
#else
	dca.da_pbufr = NULL;
#endif

	va_start(listp, fmt);
	kvprintf (fmt, db_putchar, &dca, db_radix, listp);
	va_end(listp);

#ifdef DDB_BUFR_SIZE
	if (*dca.da_pbufr != '\0')
		db_puts(dca.da_pbufr);
#endif
}

/*
 * End line if too long.
 */
void
db_end_line(int field_width)
{
	if (db_output_position + field_width > db_max_width)
	    db_printf("\n");
}
