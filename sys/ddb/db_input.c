/*	$OpenBSD: db_input.c,v 1.19 2020/10/15 03:14:00 deraadt Exp $	*/
/*	$NetBSD: db_input.c,v 1.7 1996/02/05 01:57:02 christos Exp $	*/

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
 *
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_var.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>

#include <dev/cons.h>

/*
 * Character input and editing.
 */

void db_putstring(char *, int);
void db_putnchars(int, int);
void db_delete(int, int);
void db_delete_line(void);
int db_inputchar(int);

/*
 * We don't track output position while editing input,
 * since input always ends with a new-line.  We just
 * reset the line position at the end.
 */
char *	db_lbuf_start;	/* start of input line buffer */
char *	db_lbuf_end;	/* end of input line buffer */
char *	db_lc;		/* current character */
char *	db_le;		/* one past last character */
#if DB_HISTORY_SIZE != 0
char    db_history[DB_HISTORY_SIZE];	/* start of history buffer */
int     db_history_size = DB_HISTORY_SIZE;/* size of history buffer */
char *  db_history_curr = db_history;	/* start of current line */
char *  db_history_last = db_history;	/* start of last line */
char *  db_history_prev = (char *) 0;	/* start of previous line */
#endif


#define	CTRL(c)		((c) & 0x1f)
#define	isspace(c)	((c) == ' ' || (c) == '\t')
#define	BLANK		' '
#define	BACKUP		'\b'

void
db_putstring(char *s, int count)
{
	while (--count >= 0)
		cnputc(*s++);
}

void
db_putnchars(int c, int count)
{
	while (--count >= 0)
		cnputc(c);
}

/*
 * Delete N characters, forward or backward
 */
#define	DEL_FWD		0
#define	DEL_BWD		1
void
db_delete(int n, int bwd)
{
	char *p;

	if (bwd) {
		db_lc -= n;
		db_putnchars(BACKUP, n);
	}
	for (p = db_lc; p < db_le-n; p++) {
		*p = *(p+n);
		cnputc(*p);
	}
	db_putnchars(BLANK, n);
	db_putnchars(BACKUP, db_le - db_lc);
	db_le -= n;
}

void
db_delete_line(void)
{
	db_delete(db_le - db_lc, DEL_FWD);
	db_delete(db_lc - db_lbuf_start, DEL_BWD);
	db_le = db_lc = db_lbuf_start;
}

#if DB_HISTORY_SIZE != 0
#define INC_DB_CURR() \
	do { \
		db_history_curr++; \
		if (db_history_curr > \
			db_history + db_history_size - 1) \
			db_history_curr = db_history; \
	} while (0)
#define DEC_DB_CURR() \
	do { \
		db_history_curr--; \
		if (db_history_curr < db_history) \
			db_history_curr = db_history + \
			db_history_size - 1; \
	} while (0)
#endif

/* returns `1' at end-of-line */
int
db_inputchar(int c)
{
	switch (c) {
	case CTRL('b'):
		/* back up one character */
		if (db_lc > db_lbuf_start) {
			cnputc(BACKUP);
			db_lc--;
		}
		break;
	case CTRL('f'):
		/* forward one character */
		if (db_lc < db_le) {
			cnputc(*db_lc);
			db_lc++;
		}
		break;
	case CTRL('a'):
		/* beginning of line */
		while (db_lc > db_lbuf_start) {
			cnputc(BACKUP);
			db_lc--;
		}
		break;
	case CTRL('e'):
		/* end of line */
		while (db_lc < db_le) {
			cnputc(*db_lc);
			db_lc++;
		}
		break;
	case CTRL('w'):
		/* erase word back */
		while (db_lc > db_lbuf_start && db_lc[-1] != BLANK)
			db_delete(1, DEL_BWD);
		break;
	case CTRL('h'):
	case 0177:
		/* erase previous character */
		if (db_lc > db_lbuf_start)
			db_delete(1, DEL_BWD);
		break;
	case CTRL('d'):
		/* erase next character */
		if (db_lc < db_le)
			db_delete(1, DEL_FWD);
		break;
	case CTRL('k'):
		/* delete to end of line */
		if (db_lc < db_le)
			db_delete(db_le - db_lc, DEL_FWD);
		break;
	case CTRL('u'):
		/* delete line */
		db_delete_line();
		break;
	case CTRL('t'):
		/* twiddle last 2 characters */
		if (db_lc >= db_lbuf_start + 2) {
			c = db_lc[-2];
			db_lc[-2] = db_lc[-1];
			db_lc[-1] = c;
			cnputc(BACKUP);
			cnputc(BACKUP);
			cnputc(db_lc[-2]);
			cnputc(db_lc[-1]);
		}
		break;
#if DB_HISTORY_SIZE != 0
	case CTRL('p'):
		DEC_DB_CURR();
		while (db_history_curr != db_history_last) {
			DEC_DB_CURR();
			if (*db_history_curr == '\0')
				break;
		}
		db_delete_line();
		if (db_history_curr == db_history_last) {
			INC_DB_CURR();
			db_le = db_lc = db_lbuf_start;
		} else {
			char *p;
			INC_DB_CURR();
			for (p = db_history_curr,
			    db_le = db_lbuf_start;*p; ) {
				*db_le++ = *p++;
				if (p == db_history + db_history_size)
					p = db_history;
			}
			db_lc = db_le;
		}
		db_putstring(db_lbuf_start, db_le - db_lbuf_start);
		break;
	case CTRL('n'):
		while (db_history_curr != db_history_last) {
			if (*db_history_curr == '\0')
				break;
			INC_DB_CURR();
		}
		if (db_history_curr != db_history_last) {
			INC_DB_CURR();
			db_delete_line();
			if (db_history_curr != db_history_last) {
				char *p;
				for (p = db_history_curr,
				     db_le = db_lbuf_start; *p;) {
					*db_le++ = *p++;
					if (p == db_history + db_history_size)
						p = db_history;
				}
				db_lc = db_le;
			}
			db_putstring(db_lbuf_start, db_le - db_lbuf_start);
		}
		break;
#endif
	case CTRL('r'):
		db_putstring("^R\n", 3);
		if (db_le > db_lbuf_start) {
			db_putstring(db_lbuf_start, db_le - db_lbuf_start);
			db_putnchars(BACKUP, db_le - db_lc);
		}
		break;
	case '\n':
	case '\r':
#if DB_HISTORY_SIZE != 0
		/*
		 * Check whether current line is the same
		 * as previous saved line.  If it is, don`t
		 * save it.
		 */
		if (db_history_curr == db_history_prev) {
			char *pp, *pc;

			/*
			 * Is it the same?
			 */
			for (pp = db_history_prev, pc = db_lbuf_start;
			    pc != db_le && *pp; ) {
				if (*pp != *pc)
					break;
				if (++pp == db_history + db_history_size)
					pp = db_history;
				pc++;
			}
			if (!*pp && pc == db_le) {
				/*
				 * Repeated previous line. Don`t save.
				 */
				db_history_curr = db_history_last;
				*db_le++ = c;
				return 1;
			}
		}
		if (db_le != db_lbuf_start) {
			char *p;
			db_history_prev = db_history_last;
			for (p = db_lbuf_start; p != db_le; p++) {
				*db_history_last++ = *p;
				if (db_history_last ==
				    db_history + db_history_size)
					db_history_last = db_history;
			}
			*db_history_last++ = '\0';
			if (db_history_last == db_history + db_history_size)
				db_history_last = db_history;
		}
		db_history_curr = db_history_last;
#endif
		*db_le++ = c;
		return 1;
	default:
		if (db_le == db_lbuf_end) {
			cnputc('\007');
		} else if (c >= ' ' && c <= '~') {
			char *p;

			for (p = db_le; p > db_lc; p--)
				*p = *(p-1);
			*db_lc++ = c;
			db_le++;
			cnputc(c);
			db_putstring(db_lc, db_le - db_lc);
			db_putnchars(BACKUP, db_le - db_lc);
		}
		break;
	}
	return 0;
}

int
db_readline(char *lstart, int lsize)
{
	db_force_whitespace();	/* synch output position */

	db_lbuf_start = lstart;
	db_lbuf_end   = lstart + lsize - 1;
	db_lc = lstart;
	db_le = lstart;

	while (!db_inputchar(cngetc()))
		continue;

	db_putchar('\n');	/* synch output position */

	*db_le = 0;
	return (db_le - db_lbuf_start);
}
