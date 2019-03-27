/*
 *  Top - a top users display for Berkeley Unix
 *
 *  This file defines the locations on tne screen for various parts of the
 *  display.  These definitions are used by the routines in "display.c" for
 *  cursor addressing.
 *
 * $FreeBSD$
 */

extern int  x_lastpid;		/* 10 */
extern int  y_lastpid;		/* 0 */
extern int  x_loadave;		/* 33 */
extern int  x_loadave_nompid;	/* 15 */
extern int  y_loadave;		/* 0 */
extern int  x_procstate;	/* 0 */
extern int  y_procstate;	/* 1 */
extern int  x_brkdn;		/* 15 */
extern int  y_brkdn;		/* 1 */
extern int  x_mem;		/* 5 */
extern int  y_mem;		/* 3 */
extern int  x_arc;		/* 5 */
extern int  y_arc;		/* 4 */
extern int  x_carc;		/* 5 */
extern int  y_carc;		/* 5 */
extern int  x_swap;		/* 6 */
extern int  y_swap;		/* 4 */
extern int  y_message;		/* 5 */
extern int  x_header;		/* 0 */
extern int  y_header;		/* 6 */
extern int  x_idlecursor;	/* 0 */
extern int  y_idlecursor;	/* 5 */
extern int  y_procs;		/* 7 */

extern int  y_cpustates;	/* 2 */
