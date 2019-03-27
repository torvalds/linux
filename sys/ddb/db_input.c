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
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>

#include <ddb/ddb.h>
#include <ddb/db_output.h>

/*
 * Character input and editing.
 */

/*
 * We don't track output position while editing input,
 * since input always ends with a new-line.  We just
 * reset the line position at the end.
 */
static char *	db_lbuf_start;	/* start of input line buffer */
static char *	db_lbuf_end;	/* end of input line buffer */
static char *	db_lc;		/* current character */
static char *	db_le;		/* one past last character */

/*
 * Simple input line history support.
 */
static char	db_lhistory[2048];
static int	db_lhistlsize, db_lhistidx, db_lhistcur;
static int	db_lhist_nlines;

#define	CTRL(c)		((c) & 0x1f)
#define	BLANK		' '
#define	BACKUP		'\b'

static int	cnmaygetc(void);
static void	db_delete(int n, int bwd);
static int	db_inputchar(int c);
static void	db_putnchars(int c, int count);
static void	db_putstring(char *s, int count);

static void
db_putstring(s, count)
	char	*s;
	int	count;
{
	while (--count >= 0)
	    cnputc(*s++);
}

static void
db_putnchars(c, count)
	int	c;
	int	count;
{
	while (--count >= 0)
	    cnputc(c);
}

/*
 * Delete N characters, forward or backward
 */
#define	DEL_FWD		0
#define	DEL_BWD		1
static void
db_delete(n, bwd)
	int	n;
	int	bwd;
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

/* returns true at end-of-line */
static int
db_inputchar(c)
	int	c;
{
	static int escstate;

	if (escstate == 1) {
		/* ESC seen, look for [ or O */
		if (c == '[' || c == 'O')
			escstate++;
		else
			escstate = 0; /* re-init state machine */
		return (0);
	} else if (escstate == 2) {
		escstate = 0;
		/*
		 * If a valid cursor key has been found, translate
		 * into an emacs-style control key, and fall through.
		 * Otherwise, drop off.
		 */
		switch (c) {
		case 'A':	/* up */
			c = CTRL('p');
			break;
		case 'B':	/* down */
			c = CTRL('n');
			break;
		case 'C':	/* right */
			c = CTRL('f');
			break;
		case 'D':	/* left */
			c = CTRL('b');
			break;
		default:
			return (0);
		}
	}

	switch (c) {
	    case CTRL('['):
		escstate = 1;
		break;
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
	    case CTRL('u'):
	    case CTRL('c'):
		/* kill entire line: */
		/* at first, delete to beginning of line */
		if (db_lc > db_lbuf_start)
		    db_delete(db_lc - db_lbuf_start, DEL_BWD);
		/* FALLTHROUGH */
	    case CTRL('k'):
		/* delete to end of line */
		if (db_lc < db_le)
		    db_delete(db_le - db_lc, DEL_FWD);
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
	    case CTRL('w'):
		/* erase previous word */
		for (; db_lc > db_lbuf_start;) {
		    if (*(db_lc - 1) != ' ')
			break;
		    db_delete(1, DEL_BWD);
		}
		for (; db_lc > db_lbuf_start;) {
		    if (*(db_lc - 1) == ' ')
			break;
		    db_delete(1, DEL_BWD);
		}
		break;
	    case CTRL('r'):
		db_putstring("^R\n", 3);
	    redraw:
		if (db_le > db_lbuf_start) {
		    db_putstring(db_lbuf_start, db_le - db_lbuf_start);
		    db_putnchars(BACKUP, db_le - db_lc);
		}
		break;
	    case CTRL('p'):
		/* Make previous history line the active one. */
		if (db_lhistcur >= 0) {
		    bcopy(db_lhistory + db_lhistcur * db_lhistlsize,
			  db_lbuf_start, db_lhistlsize);
		    db_lhistcur--;
		    goto hist_redraw;
		}
		break;
	    case CTRL('n'):
		/* Make next history line the active one. */
		if (db_lhistcur < db_lhistidx - 1) {
		    db_lhistcur += 2;
		    bcopy(db_lhistory + db_lhistcur * db_lhistlsize,
			  db_lbuf_start, db_lhistlsize);
		} else {
		    /*
		     * ^N through tail of history, reset the
		     * buffer to zero length.
		     */
		    *db_lbuf_start = '\0';
		    db_lhistcur = db_lhistidx;
		}

	    hist_redraw:
		db_putnchars(BACKUP, db_lc - db_lbuf_start);
		db_putnchars(BLANK, db_le - db_lbuf_start);
		db_putnchars(BACKUP, db_le - db_lbuf_start);
		db_le = strchr(db_lbuf_start, '\0');
		if (db_le[-1] == '\r' || db_le[-1] == '\n')
		    *--db_le = '\0';
		db_lc = db_le;
		goto redraw;

	    case -1:
		/*
		 * eek! the console returned eof.
		 * probably that means we HAVE no console.. we should try bail
		 * XXX
		 */
		c = '\r';
	    case '\n':
		/* FALLTHROUGH */
	    case '\r':
		*db_le++ = c;
		return (1);
	    default:
		if (db_le == db_lbuf_end) {
		    cnputc('\007');
		}
		else if (c >= ' ' && c <= '~') {
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
	return (0);
}

static int
cnmaygetc()
{
	return (-1);
}

int
db_readline(lstart, lsize)
	char *	lstart;
	int	lsize;
{

	if (lsize < 2)
		return (0);
	if (lsize != db_lhistlsize) {
		/*
		 * (Re)initialize input line history.  Throw away any
		 * existing history.
		 */
		db_lhist_nlines = sizeof(db_lhistory) / lsize;
		db_lhistlsize = lsize;
		db_lhistidx = -1;
	}
	db_lhistcur = db_lhistidx;

	db_force_whitespace();	/* synch output position */

	db_lbuf_start = lstart;
	db_lbuf_end   = lstart + lsize - 2;	/* Will append NL and NUL. */
	db_lc = lstart;
	db_le = lstart;

	while (!db_inputchar(cngetc()))
	    continue;

	db_capture_write(lstart, db_le - db_lbuf_start);
	db_printf("\n");	/* synch output position */
	*db_le = 0;

	if (db_le - db_lbuf_start > 1) {
	    /* Maintain input line history for non-empty lines. */
	    if (++db_lhistidx == db_lhist_nlines) {
		/* Rotate history. */
		bcopy(db_lhistory + db_lhistlsize, db_lhistory,
		      db_lhistlsize * (db_lhist_nlines - 1));
		db_lhistidx--;
	    }
	    bcopy(lstart, db_lhistory + db_lhistidx * db_lhistlsize,
		  db_lhistlsize);
	}

	return (db_le - db_lbuf_start);
}

void
db_check_interrupt(void)
{
	int	c;

	c = cnmaygetc();
	switch (c) {
	    case -1:		/* no character */
		return;

	    case CTRL('c'):
		db_error((char *)0);
		/*NOTREACHED*/

	    case CTRL('s'):
		do {
		    c = cnmaygetc();
		    if (c == CTRL('c'))
			db_error((char *)0);
		} while (c != CTRL('q'));
		break;

	    default:
		/* drop on floor */
		break;
	}
}
