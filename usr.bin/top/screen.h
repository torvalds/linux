/*
 *  top - a top users display for Unix 4.2
 *
 *  This file contains all the definitions necessary to use the hand-written
 *  screen package in "screen.c"
 *
 *  $FreeBSD$
 */

#define TCputs(str)	tputs(str, 1, putchar)
#define putcap(str)	(void)((str) != NULL ? TCputs(str) : 0)
#define Move_to(x, y)	TCputs(tgoto(cursor_motion, x, y))

extern char ch_erase;		/* set to the user's erase character */
extern char ch_kill;		/* set to the user's kill  character */
extern char smart_terminal;     /* set if the terminal has sufficient termcap
				   capabilities for normal operation */

/* These are some termcap strings for use outside of "screen.c" */
extern char *cursor_motion;
extern char *clear_line;
extern char *clear_to_end;

/* rows and columns on the screen according to termcap */
extern int  screen_length;
extern int  screen_width;

int		clear_eol(int len);
void	top_standout(const char *msg);
void	top_clear(void);
void	reinit_screen(void);
void	get_screensize(void);
void	init_termcap(bool interactive);
void	end_screen(void);
void	init_screen(void);

