/*
 *  Top users/processes display for Unix
 *  Version 3
 *
 *  This program may be freely redistributed,
 *  but this entire comment MUST remain intact.
 *
 *  Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *  Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 *
 * $FreeBSD$
 */

/*
 *  This file contains the routines that display information on the screen.
 *  Each section of the screen has two routines:  one for initially writing
 *  all constant and dynamic text, and one for only updating the text that
 *  changes.  The prefix "i_" is used on all the "initial" routines and the
 *  prefix "u_" is used for all the "updating" routines.
 *
 *  ASSUMPTIONS:
 *        None of the "i_" routines use any of the termcap capabilities.
 *        In this way, those routines can be safely used on terminals that
 *        have minimal (or nonexistant) terminal capabilities.
 *
 *        The routines are called in this order:  *_loadave, i_timeofday,
 *        *_procstates, *_cpustates, *_memory, *_message, *_header,
 *        *_process, u_endscreen.
 */

#include <sys/cdefs.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termcap.h>
#include <time.h>
#include <unistd.h>

#include "screen.h"		/* interface to screen package */
#include "layout.h"		/* defines for screen position layout */
#include "display.h"
#include "top.h"
#include "machine.h"		/* we should eliminate this!!! */
#include "utils.h"

#ifdef DEBUG
FILE *debug;
#endif

static int lmpid = 0;
static int last_hi = 0;		/* used in u_process and u_endscreen */
static int lastline = 0;

#define lineindex(l) ((l)*screen_width)


/* things initialized by display_init and used thruout */

/* buffer of proc information lines for display updating */
static char *screenbuf = NULL;

static const char * const *procstate_names;
static const char * const *cpustate_names;
static const char * const *memory_names;
static const char * const *arc_names;
static const char * const *carc_names;
static const char * const *swap_names;

static int num_procstates;
static int num_cpustates;
static int num_memory;
static int num_swap;

static int *lprocstates;
static int *lcpustates;
static int *lmemory;
static int *lswap;

static int num_cpus;
static int *cpustate_columns;
static int cpustate_total_length;
static int cpustates_column;

static enum { OFF, ON, ERASE } header_status = ON;

static void summary_format(char *, int *, const char * const *);
static void line_update(char *, char *, int, int);

static int setup_buffer_bufsiz = 0;
static char * setup_buffer(char *, int);

int  x_lastpid =	10;
int  y_lastpid =	0;
int  x_loadave =	33;
int  x_loadave_nompid =	15;
int  y_loadave =	0;
int  x_procstate =	0;
int  y_procstate =	1;
int  x_brkdn =		15;
int  y_brkdn =		1;
int  x_mem =		5;
int  y_mem =		3;
int  x_arc =		5;
int  y_arc =		4;
int  x_carc =		5;
int  y_carc =		5;
int  x_swap =		6;
int  y_swap =		4;
int  y_message =	5;
int  x_header =		0;
int  y_header =		6;
int  x_idlecursor =	0;
int  y_idlecursor =	5;
int  y_procs =		7;

int  y_cpustates =	2;
int  Header_lines =	7;

int
display_resize(void)
{
    int lines;

    /* first, deallocate any previous buffer that may have been there */
    if (screenbuf != NULL)
    {
	free(screenbuf);
    }

    /* calculate the current dimensions */
    /* if operating in "dumb" mode, we only need one line */
    lines = smart_terminal ? screen_length - Header_lines : 1;

    if (lines < 0)
	lines = 0;

    /* now, allocate space for the screen buffer */
    screenbuf = calloc(lines, screen_width);
    if (screenbuf == NULL)
    {
	/* oops! */
	return(-1);
    }

    /* return number of lines available */
    /* for dumb terminals, pretend like we can show any amount */
    return(smart_terminal ? lines : Largest);
}

int
display_updatecpus(struct statics *statics)
{
    int lines;
    int i;

    /* call resize to do the dirty work */
    lines = display_resize();
    if (pcpu_stats)
		num_cpus = statics->ncpus;
    else
		num_cpus = 1;
    cpustates_column = 5;	/* CPU: */
    if (num_cpus > 1) {
		cpustates_column += 1 + digits(num_cpus); /* CPU #: */
	}

    /* fill the "last" array with all -1s, to insure correct updating */
	for (i = 0; i < num_cpustates * num_cpus; ++i) {
		lcpustates[i] = -1;
    }

    return(lines);
}

int
display_init(struct statics * statics)
{
    int lines;
    const char * const *pp;
    int *ip;
    int i;

    lines = display_updatecpus(statics);

    /* only do the rest if we need to */
    if (lines > -1)
    {
	/* save pointers and allocate space for names */
	procstate_names = statics->procstate_names;
	num_procstates = 8;
	assert(num_procstates > 0);
	lprocstates = calloc(num_procstates, sizeof(int));

	cpustate_names = statics->cpustate_names;

	swap_names = statics->swap_names;
	num_swap = 7;
	assert(num_swap > 0);
	lswap = calloc(num_swap, sizeof(int));
	num_cpustates = CPUSTATES;
	assert(num_cpustates > 0);
	lcpustates = calloc(num_cpustates * sizeof(int), statics->ncpus);
	cpustate_columns = calloc(num_cpustates, sizeof(int));

	memory_names = statics->memory_names;
	num_memory = 7;
	assert(num_memory > 0);
	lmemory = calloc(num_memory, sizeof(int));

	arc_names = statics->arc_names;
	carc_names = statics->carc_names;

	/* calculate starting columns where needed */
	cpustate_total_length = 0;
	pp = cpustate_names;
	ip = cpustate_columns;
	while (*pp != NULL)
	{
	    *ip++ = cpustate_total_length;
	    if ((i = strlen(*pp++)) > 0)
	    {
		cpustate_total_length += i + 8;
	    }
	}
    }

    /* return number of lines available */
    return(lines);
}

void
i_loadave(int mpid, double avenrun[])
{
    int i;

    /* i_loadave also clears the screen, since it is first */
    top_clear();

    /* mpid == -1 implies this system doesn't have an _mpid */
    if (mpid != -1)
    {
	printf("last pid: %5d;  ", mpid);
    }

    printf("load averages");

    for (i = 0; i < 3; i++)
    {
	printf("%c %5.2f",
	    i == 0 ? ':' : ',',
	    avenrun[i]);
    }
    lmpid = mpid;
}

void
u_loadave(int mpid, double *avenrun)
{
    int i;

    if (mpid != -1)
    {
	/* change screen only when value has really changed */
	if (mpid != lmpid)
	{
	    Move_to(x_lastpid, y_lastpid);
	    printf("%5d", mpid);
	    lmpid = mpid;
	}

	/* i remembers x coordinate to move to */
	i = x_loadave;
    }
    else
    {
	i = x_loadave_nompid;
    }

    /* move into position for load averages */
    Move_to(i, y_loadave);

    /* display new load averages */
    /* we should optimize this and only display changes */
    for (i = 0; i < 3; i++)
    {
	printf("%s%5.2f",
	    i == 0 ? "" : ", ",
	    avenrun[i]);
    }
}

void
i_timeofday(time_t *tod)
{
    /*
     *  Display the current time.
     *  "ctime" always returns a string that looks like this:
     *
     *	Sun Sep 16 01:03:52 1973
     *      012345678901234567890123
     *	          1         2
     *
     *  We want indices 11 thru 18 (length 8).
     */

    if (smart_terminal)
    {
	Move_to(screen_width - 8, 0);
    }
    else
    {
	fputs("    ", stdout);
    }
#ifdef DEBUG
    {
	char *foo;
	foo = ctime(tod);
	fputs(foo, stdout);
    }
#endif
    printf("%-8.8s\n", &(ctime(tod)[11]));
    lastline = 1;
}

static int ltotal = 0;
static char *procstates_buffer = NULL;

/*
 *  *_procstates(total, brkdn, names) - print the process summary line
 *
 *  Assumptions:  cursor is at the beginning of the line on entry
 *		  lastline is valid
 */

void
i_procstates(int total, int *brkdn)
{
    int i;

    procstates_buffer = setup_buffer(procstates_buffer, 0);

    /* write current number of processes and remember the value */
    printf("%d %s:", total, ps.thread ? "threads" : "processes");
    ltotal = total;

    /* put out enough spaces to get to column 15 */
    i = digits(total);
    while (i++ < (ps.thread ? 6 : 4))
    {
	putchar(' ');
    }

    /* format and print the process state summary */
    summary_format(procstates_buffer, brkdn, procstate_names);
    fputs(procstates_buffer, stdout);

    /* save the numbers for next time */
    memcpy(lprocstates, brkdn, num_procstates * sizeof(int));
}

void
u_procstates(int total, int *brkdn)
{
    static char *new = NULL;
    int i;

    new = setup_buffer(new, 0);

    /* update number of processes only if it has changed */
    if (ltotal != total)
    {
	/* move and overwrite */
	if (x_procstate == 0) {
	    Move_to(x_procstate, y_procstate);
	}
	else {
	    /* cursor is already there...no motion needed */
	    assert(lastline == 1);
	}
	printf("%d", total);

	/* if number of digits differs, rewrite the label */
	if (digits(total) != digits(ltotal))
	{
	    printf(" %s:", ps.thread ? "threads" : "processes");
	    /* put out enough spaces to get to column 15 */
	    i = digits(total);
	    while (i++ < (ps.thread ? 6 : 4))
	    {
		putchar(' ');
	    }
	    /* cursor may end up right where we want it!!! */
	}

	/* save new total */
	ltotal = total;
    }

    /* see if any of the state numbers has changed */
    if (memcmp(lprocstates, brkdn, num_procstates * sizeof(int)) != 0)
    {
	/* format and update the line */
	summary_format(new, brkdn, procstate_names);
	line_update(procstates_buffer, new, x_brkdn, y_brkdn);
	memcpy(lprocstates, brkdn, num_procstates * sizeof(int));
    }
}

void
i_cpustates(int *states)
{
    int i = 0;
    int value;
    const char * const *names;
    const char *thisname;
    int *hstates = states;
    int cpu;

for (cpu = 0; cpu < num_cpus; cpu++) {
    names = cpustate_names;

    /* print tag and bump lastline */
    if (num_cpus == 1)
	printf("\nCPU: ");
    else {
	value = printf("\nCPU %d: ", cpu);
	while (value++ <= cpustates_column)
		printf(" ");
    }
    lastline++;

    /* now walk thru the names and print the line */
    while ((thisname = *names++) != NULL)
    {
	if (*thisname != '\0')
	{
	    /* retrieve the value and remember it */
	    value = *states++;

	    /* if percentage is >= 1000, print it as 100% */
	    printf((value >= 1000 ? "%s%4.0f%% %s" : "%s%4.1f%% %s"),
		   (i++ % num_cpustates) == 0 ? "" : ", ",
		   ((float)value)/10.,
		   thisname);
	}
    }
}

    /* copy over values into "last" array */
    states = hstates;
    memcpy(lcpustates, states, num_cpustates * sizeof(int) * num_cpus);
}

void
u_cpustates(int *states)
{
    int value;
    const char * const *names;
    const char *thisname;
    int *hstates = states;
    int *lp;
    int *colp;
    int cpu;

for (cpu = 0; cpu < num_cpus; cpu++) {
    names = cpustate_names;

    Move_to(cpustates_column, y_cpustates + cpu);
    lastline = y_cpustates + cpu;
    lp = lcpustates + (cpu * num_cpustates);
    colp = cpustate_columns;

    /* we could be much more optimal about this */
    while ((thisname = *names++) != NULL)
    {
	if (*thisname != '\0')
	{
	    /* did the value change since last time? */
	    if (*lp != *states)
	    {
		/* yes, move and change */
		Move_to(cpustates_column + *colp, y_cpustates + cpu);
		lastline = y_cpustates + cpu;

		/* retrieve value and remember it */
		value = *states;

		/* if percentage is >= 1000, print it as 100% */
		printf((value >= 1000 ? "%4.0f" : "%4.1f"),
		       ((double)value)/10.);

		/* remember it for next time */
		*lp = value;
	    }
	}

	/* increment and move on */
	lp++;
	states++;
	colp++;
    }
}

    states = hstates;
}

void
z_cpustates(void)
{
    int i = 0;
    const char * const *names;
    const char *thisname;
    int cpu, value;

    for (cpu = 0; cpu < num_cpus; cpu++) {
	    names = cpustate_names;

	    /* show tag and bump lastline */
	    if (num_cpus == 1)
		    printf("\nCPU: ");
	    else {
		    value = printf("\nCPU %d: ", cpu);
		    while (value++ <= cpustates_column)
			    printf(" ");
	    }
	    lastline++;

	    while ((thisname = *names++) != NULL)
	    {
		    if (*thisname != '\0')
		    {
			    printf("%s    %% %s", (i++ % num_cpustates) == 0 ? "" : ", ", thisname);
		    }
	    }
    }

    /* fill the "last" array with all -1s, to insure correct updating */
	for (i = 0; i < num_cpustates * num_cpus; ++i) {
		lcpustates[i] = -1;
    }
}

/*
 *  *_memory(stats) - print "Memory: " followed by the memory summary string
 *
 *  Assumptions:  cursor is on "lastline"
 *                for i_memory ONLY: cursor is on the previous line
 */

static char *memory_buffer = NULL;

void
i_memory(int *stats)
{
    memory_buffer = setup_buffer(memory_buffer, 0);

    fputs("\nMem: ", stdout);
    lastline++;

    /* format and print the memory summary */
    summary_format(memory_buffer, stats, memory_names);
    fputs(memory_buffer, stdout);
}

void
u_memory(int *stats)
{
    static char *new = NULL;

    new = setup_buffer(new, 0);

    /* format the new line */
    summary_format(new, stats, memory_names);
    line_update(memory_buffer, new, x_mem, y_mem);
}

/*
 *  *_arc(stats) - print "ARC: " followed by the ARC summary string
 *
 *  Assumptions:  cursor is on "lastline"
 *                for i_arc ONLY: cursor is on the previous line
 */
static char *arc_buffer = NULL;

void
i_arc(int *stats)
{
    arc_buffer = setup_buffer(arc_buffer, 0);

    if (arc_names == NULL)
	return;

    fputs("\nARC: ", stdout);
    lastline++;

    /* format and print the memory summary */
    summary_format(arc_buffer, stats, arc_names);
    fputs(arc_buffer, stdout);
}

void
u_arc(int *stats)
{
    static char *new = NULL;

    new = setup_buffer(new, 0);

    if (arc_names == NULL)
	return;

    /* format the new line */
    summary_format(new, stats, arc_names);
    line_update(arc_buffer, new, x_arc, y_arc);
}


/*
 *  *_carc(stats) - print "Compressed ARC: " followed by the summary string
 *
 *  Assumptions:  cursor is on "lastline"
 *                for i_carc ONLY: cursor is on the previous line
 */
static char *carc_buffer = NULL;

void
i_carc(int *stats)
{
    carc_buffer = setup_buffer(carc_buffer, 0);

    if (carc_names == NULL)
	return;

    fputs("\n     ", stdout);
    lastline++;

    /* format and print the memory summary */
    summary_format(carc_buffer, stats, carc_names);
    fputs(carc_buffer, stdout);
}

void
u_carc(int *stats)
{
    static char *new = NULL;

    new = setup_buffer(new, 0);

    if (carc_names == NULL)
	return;

    /* format the new line */
    summary_format(new, stats, carc_names);
    line_update(carc_buffer, new, x_carc, y_carc);
}

/*
 *  *_swap(stats) - print "Swap: " followed by the swap summary string
 *
 *  Assumptions:  cursor is on "lastline"
 *                for i_swap ONLY: cursor is on the previous line
 */

static char *swap_buffer = NULL;

void
i_swap(int *stats)
{
    swap_buffer = setup_buffer(swap_buffer, 0);

    fputs("\nSwap: ", stdout);
    lastline++;

    /* format and print the swap summary */
    summary_format(swap_buffer, stats, swap_names);
    fputs(swap_buffer, stdout);
}

void
u_swap(int *stats)
{
    static char *new = NULL;

    new = setup_buffer(new, 0);

    /* format the new line */
    summary_format(new, stats, swap_names);
    line_update(swap_buffer, new, x_swap, y_swap);
}

/*
 *  *_message() - print the next pending message line, or erase the one
 *                that is there.
 *
 *  Note that u_message is (currently) the same as i_message.
 *
 *  Assumptions:  lastline is consistent
 */

/*
 *  i_message is funny because it gets its message asynchronously (with
 *	respect to screen updates).
 */

#define NEXT_MSG_ADDLEN 5
static char *next_msg = NULL;
static int msglen = 0;
/* Invariant: msglen is always the length of the message currently displayed
   on the screen (even when next_msg doesn't contain that message). */

void
i_message(void)
{
    next_msg = setup_buffer(next_msg, NEXT_MSG_ADDLEN);

    while (lastline < y_message)
    {
	fputc('\n', stdout);
	lastline++;
    }
    if (next_msg[0] != '\0')
    {
	top_standout(next_msg);
	msglen = strlen(next_msg);
	next_msg[0] = '\0';
    }
    else if (msglen > 0)
    {
	(void) clear_eol(msglen);
	msglen = 0;
    }
}

void
u_message(void)
{
    i_message();
}

static int header_length;

/*
 * Trim a header string to the current display width and return a newly
 * allocated area with the trimmed header.
 */

char *
trim_header(const char *text)
{
	char *s;
	int width;

	s = NULL;
	width = screen_width;
	header_length = strlen(text);
	if (header_length >= width) {
		s = strndup(text, width);
		if (s == NULL)
			return (NULL);
	}
	return (s);
}

/*
 *  *_header(text) - print the header for the process area
 *
 *  Assumptions:  cursor is on the previous line and lastline is consistent
 */

void
i_header(const char *text)
{
    char *s;

    s = trim_header(text);
    if (s != NULL)
	text = s;

    if (header_status == ON)
    {
	putchar('\n');
	fputs(text, stdout);
	lastline++;
    }
    else if (header_status == ERASE)
    {
	header_status = OFF;
    }
    free(s);
}

void
u_header(const char *text __unused)
{

    if (header_status == ERASE)
    {
	putchar('\n');
	lastline++;
	clear_eol(header_length);
	header_status = OFF;
    }
}

/*
 *  *_process(line, thisline) - print one process line
 *
 *  Assumptions:  lastline is consistent
 */

void
i_process(int line, char *thisline)
{
    char *p;
    char *base;

    /* make sure we are on the correct line */
    while (lastline < y_procs + line)
    {
	putchar('\n');
	lastline++;
    }

    /* truncate the line to conform to our current screen width */
    int len = strlen(thisline);
    if (screen_width < len)
    {
	thisline[screen_width] = '\0';
    }

    /* write the line out */
    fputs(thisline, stdout);

    /* copy it in to our buffer */
    base = smart_terminal ? screenbuf + lineindex(line) : screenbuf;
    p = stpcpy(base, thisline);

    /* zero fill the rest of it */
    if (p - base < screen_width)
    {
	memset(p, 0, screen_width - (p - base));
    }
}

void
u_process(int line, char *newline)
{
    char *optr;
    int screen_line = line + Header_lines;
    char *bufferline;

    /* remember a pointer to the current line in the screen buffer */
    bufferline = &screenbuf[lineindex(line)];

    /* truncate the line to conform to our current screen width */
    int len = strlen(newline);
    if (screen_width < len)
    {
	newline[screen_width] = '\0';
    }

    /* is line higher than we went on the last display? */
    if (line >= last_hi)
    {
	/* yes, just ignore screenbuf and write it out directly */
	/* get positioned on the correct line */
	if (screen_line - lastline == 1)
	{
	    putchar('\n');
	    lastline++;
	}
	else
	{
	    Move_to(0, screen_line);
	    lastline = screen_line;
	}

	/* now write the line */
	fputs(newline, stdout);

	/* copy it in to the buffer */
	optr = stpcpy(bufferline, newline);

	/* zero fill the rest of it */
	if (optr - bufferline < screen_width)
	{
	    memset(optr, 0, screen_width - (optr - bufferline));
	}
    }
    else
    {
	line_update(bufferline, newline, 0, line + Header_lines);
    }
}

void
u_endscreen(int hi)
{
    int screen_line = hi + Header_lines;
    int i;

    if (smart_terminal)
    {
	if (hi < last_hi)
	{
	    /* need to blank the remainder of the screen */
	    /* but only if there is any screen left below this line */
	    if (lastline + 1 < screen_length)
	    {
		/* efficiently move to the end of currently displayed info */
		if (screen_line - lastline < 5)
		{
		    while (lastline < screen_line)
		    {
			putchar('\n');
			lastline++;
		    }
		}
		else
		{
		    Move_to(0, screen_line);
		    lastline = screen_line;
		}

		if (clear_to_end)
		{
		    /* we can do this the easy way */
		    putcap(clear_to_end);
		}
		else
		{
		    /* use clear_eol on each line */
		    i = hi;
		    while ((void) clear_eol(strlen(&screenbuf[lineindex(i++)])), i < last_hi)
		    {
			putchar('\n');
		    }
		}
	    }
	}
	last_hi = hi;

	/* move the cursor to a pleasant place */
	Move_to(x_idlecursor, y_idlecursor);
	lastline = y_idlecursor;
    }
    else
    {
	/* separate this display from the next with some vertical room */
	fputs("\n\n", stdout);
    }
}

void
display_header(int t)
{

    if (t)
    {
	header_status = ON;
    }
    else if (header_status == ON)
    {
	header_status = ERASE;
    }
}

void
new_message(int type, const char *msgfmt, ...)
{
    va_list args;
    size_t i;

    va_start(args, msgfmt);

    /* first, format the message */
    vsnprintf(next_msg, setup_buffer_bufsiz + NEXT_MSG_ADDLEN,
		    msgfmt, args);

    va_end(args);

    if (msglen > 0)
    {
	/* message there already -- can we clear it? */
	if (!overstrike)
	{
	    /* yes -- write it and clear to end */
	    i = strlen(next_msg);
	    if ((type & MT_delayed) == 0)
	    {
			if (type & MT_standout) {
				top_standout(next_msg);
			} else {
				fputs(next_msg, stdout);
			}
			clear_eol(msglen - i);
			msglen = i;
			next_msg[0] = '\0';
	    }
	}
    }
    else
    {
	if ((type & MT_delayed) == 0)
	{
		if (type & MT_standout) {
			top_standout(next_msg);
		} else {
			fputs(next_msg, stdout);
		}
	    msglen = strlen(next_msg);
	    next_msg[0] = '\0';
	}
    }
}

void
clear_message(void)
{
    if (clear_eol(msglen) == 1)
    {
	putchar('\r');
    }
}

int
readline(char *buffer, int size, int numeric)
{
    char *ptr = buffer;
    char ch;
    char cnt = 0;
    char maxcnt = 0;

    /* allow room for null terminator */
    size -= 1;

    /* read loop */
    while ((fflush(stdout), read(0, ptr, 1) > 0))
    {
	/* newline means we are done */
	if ((ch = *ptr) == '\n' || ch == '\r')
	{
	    break;
	}

	/* handle special editing characters */
	if (ch == ch_kill)
	{
	    /* kill line -- account for overstriking */
	    if (overstrike)
	    {
		msglen += maxcnt;
	    }

	    /* return null string */
	    *buffer = '\0';
	    putchar('\r');
	    return(-1);
	}
	else if (ch == ch_erase)
	{
	    /* erase previous character */
	    if (cnt <= 0)
	    {
		/* none to erase! */
		putchar('\7');
	    }
	    else
	    {
		fputs("\b \b", stdout);
		ptr--;
		cnt--;
	    }
	}
	/* check for character validity and buffer overflow */
	else if (cnt == size || (numeric && !isdigit(ch)) ||
		!isprint(ch))
	{
	    /* not legal */
	    putchar('\7');
	}
	else
	{
	    /* echo it and store it in the buffer */
	    putchar(ch);
	    ptr++;
	    cnt++;
	    if (cnt > maxcnt)
	    {
		maxcnt = cnt;
	    }
	}
    }

    /* all done -- null terminate the string */
    *ptr = '\0';

    /* account for the extra characters in the message area */
    /* (if terminal overstrikes, remember the furthest they went) */
    msglen += overstrike ? maxcnt : cnt;

    /* return either inputted number or string length */
    putchar('\r');
    return(cnt == 0 ? -1 : numeric ? atoi(buffer) : cnt);
}

/* internal support routines */

static void
summary_format(char *str, int *numbers, const char * const *names)
{
    char *p;
    int num;
    const char *thisname;
    char rbuf[6];

    /* format each number followed by its string */
    p = str;
    while ((thisname = *names++) != NULL)
    {
	/* get the number to format */
	num = *numbers++;

	/* display only non-zero numbers */
	if (num > 0)
	{
	    /* is this number in kilobytes? */
	    if (thisname[0] == 'K')
	    {
		/* yes: format it as a memory value */
		p = stpcpy(p, format_k(num));

		/* skip over the K, since it was included by format_k */
		p = stpcpy(p, thisname+1);
	    }
	    /* is this number a ratio? */
	    else if (thisname[0] == ':')
	    {
		(void) snprintf(rbuf, sizeof(rbuf), "%.2f",
		    (float)*(numbers - 2) / (float)num);
		p = stpcpy(p, rbuf);
		p = stpcpy(p, thisname);
	    }
	    else
	    {
		p = stpcpy(p, itoa(num));
		p = stpcpy(p, thisname);
	    }
	}

	/* ignore negative numbers, but display corresponding string */
	else if (num < 0)
	{
	    p = stpcpy(p, thisname);
	}
    }

    /* if the last two characters in the string are ", ", delete them */
    p -= 2;
    if (p >= str && p[0] == ',' && p[1] == ' ')
    {
	*p = '\0';
    }
}

static void
line_update(char *old, char *new, int start, int line)
{
    int ch;
    int diff;
    int newcol = start + 1;
    int lastcol = start;
    char cursor_on_line = false;
    char *current;

    /* compare the two strings and only rewrite what has changed */
    current = old;
#ifdef DEBUG
    fprintf(debug, "line_update, starting at %d\n", start);
    fputs(old, debug);
    fputc('\n', debug);
    fputs(new, debug);
    fputs("\n-\n", debug);
#endif

    /* start things off on the right foot		    */
    /* this is to make sure the invariants get set up right */
    if ((ch = *new++) != *old)
    {
	if (line - lastline == 1 && start == 0)
	{
	    putchar('\n');
	}
	else
	{
	    Move_to(start, line);
	}
	cursor_on_line = true;
	putchar(ch);
	*old = ch;
	lastcol = start + 1;
    }
    old++;

    /*
     *  main loop -- check each character.  If the old and new aren't the
     *	same, then update the display.  When the distance from the
     *	current cursor position to the new change is small enough,
     *	the characters that belong there are written to move the
     *	cursor over.
     *
     *	Invariants:
     *	    lastcol is the column where the cursor currently is sitting
     *		(always one beyond the end of the last mismatch).
     */
    do		/* yes, a do...while */
    {
	if ((ch = *new++) != *old)
	{
	    /* new character is different from old	  */
	    /* make sure the cursor is on top of this character */
	    diff = newcol - lastcol;
	    if (diff > 0)
	    {
		/* some motion is required--figure out which is shorter */
		if (diff < 6 && cursor_on_line)
		{
		    /* overwrite old stuff--get it out of the old buffer */
		    printf("%.*s", diff, &current[lastcol-start]);
		}
		else
		{
		    /* use cursor addressing */
		    Move_to(newcol, line);
		    cursor_on_line = true;
		}
		/* remember where the cursor is */
		lastcol = newcol + 1;
	    }
	    else
	    {
		/* already there, update position */
		lastcol++;
	    }

	    /* write what we need to */
	    if (ch == '\0')
	    {
		/* at the end--terminate with a clear-to-end-of-line */
		(void) clear_eol(strlen(old));
	    }
	    else
	    {
		/* write the new character */
		putchar(ch);
	    }
	    /* put the new character in the screen buffer */
	    *old = ch;
	}

	/* update working column and screen buffer pointer */
	newcol++;
	old++;

    } while (ch != '\0');

    /* zero out the rest of the line buffer -- MUST BE DONE! */
    diff = screen_width - newcol;
    if (diff > 0)
    {
	memset(old, 0, diff);
    }

    /* remember where the current line is */
    if (cursor_on_line)
    {
	lastline = line;
    }
}

/*
 *  printable(str) - make the string pointed to by "str" into one that is
 *	printable (i.e.: all ascii), by converting all non-printable
 *	characters into '?'.  Replacements are done in place and a pointer
 *	to the original buffer is returned.
 */

char *
printable(char str[])
{
    char *ptr;
    char ch;

    ptr = str;
    while ((ch = *ptr) != '\0')
    {
	if (!isprint(ch))
	{
	    *ptr = '?';
	}
	ptr++;
    }
    return(str);
}

void
i_uptime(struct timeval *bt, time_t *tod)
{
    time_t uptime;
    int days, hrs, mins, secs;

    if (bt->tv_sec != -1) {
	uptime = *tod - bt->tv_sec;
	days = uptime / 86400;
	uptime %= 86400;
	hrs = uptime / 3600;
	uptime %= 3600;
	mins = uptime / 60;
	secs = uptime % 60;

	/*
	 *  Display the uptime.
	 */

	if (smart_terminal)
	{
	    Move_to((screen_width - 24) - (days > 9 ? 1 : 0), 0);
	}
	else
	{
	    fputs(" ", stdout);
	}
	printf(" up %d+%02d:%02d:%02d", days, hrs, mins, secs);
    }
}

#define SETUPBUFFER_MIN_SCREENWIDTH 80
#define SETUPBUFFER_REQUIRED_ADDBUFSIZ 2

static char *
setup_buffer(char *buffer, int addlen)
{
    size_t len;

    setup_buffer_bufsiz = screen_width;
    if (setup_buffer_bufsiz < SETUPBUFFER_MIN_SCREENWIDTH)
    {
	setup_buffer_bufsiz = SETUPBUFFER_MIN_SCREENWIDTH;
    }

    free(buffer);
    len = setup_buffer_bufsiz + addlen + SETUPBUFFER_REQUIRED_ADDBUFSIZ;
    buffer = calloc(len, sizeof(char));
    if (buffer == NULL)
    {
	errx(4, "can't allocate sufficient memory");
    }

    return buffer;
}
