/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_TIOCL_H
#define _LINUX_TIOCL_H

#define TIOCL_SETSEL	2	/* set a selection */
#define 	TIOCL_SELCHAR	0	/* select characters */
#define 	TIOCL_SELWORD	1	/* select whole words */
#define 	TIOCL_SELLINE	2	/* select whole lines */
#define 	TIOCL_SELPOINTER	3	/* show the pointer */
#define 	TIOCL_SELCLEAR	4	/* clear visibility of selection */
#define 	TIOCL_SELMOUSEREPORT	16	/* report beginning of selection */
#define 	TIOCL_SELBUTTONMASK	15	/* button mask for report */
/* selection extent */
struct tiocl_selection {
	unsigned short xs;	/* X start */
	unsigned short ys;	/* Y start */
	unsigned short xe;	/* X end */
	unsigned short ye;	/* Y end */
	unsigned short sel_mode;	/* selection mode */
};

#define TIOCL_PASTESEL	3	/* paste previous selection */
#define TIOCL_UNBLANKSCREEN	4	/* unblank screen */

#define TIOCL_SELLOADLUT	5
	/* set characters to be considered alphabetic when selecting */
	/* u32[8] bit array, 4 bytes-aligned with type */

/* these two don't return a value: they write it back in the type */
#define TIOCL_GETSHIFTSTATE	6	/* write shift state */
#define TIOCL_GETMOUSEREPORTING	7	/* write whether mouse event are reported */
#define TIOCL_SETVESABLANK	10	/* set vesa blanking mode */
#define TIOCL_SETKMSGREDIRECT	11	/* restrict kernel messages to a vt */
#define TIOCL_GETFGCONSOLE	12	/* get foreground vt */
#define TIOCL_SCROLLCONSOLE	13	/* scroll console */
#define TIOCL_BLANKSCREEN	14	/* keep screen blank even if a key is pressed */
#define TIOCL_BLANKEDSCREEN	15	/* return which vt was blanked */
#define TIOCL_GETKMSGREDIRECT	17	/* get the vt the kernel messages are restricted to */
#define TIOCL_GETBRACKETEDPASTE	18	/* get whether paste may be bracketed */

#endif /* _LINUX_TIOCL_H */
