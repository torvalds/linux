/* $OpenBSD: tty_input.h,v 1.3 2010/01/12 23:22:07 nicm Exp $ */

/****************************************************************************
 * Copyright (c) 1998,2000 Free Software Foundation, Inc.                   *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/*
 * $Id: tty_input.h,v 1.3 2010/01/12 23:22:07 nicm Exp $
 */

#ifndef TTY_INPUT_H
#define TTY_INPUT_H 1

extern NCURSES_EXPORT(bool) _nc_tty_mouse_mask (mmask_t);
extern NCURSES_EXPORT(bool) _nc_tty_pending (void);
extern NCURSES_EXPORT(int)  _nc_tty_next_event (int);
extern NCURSES_EXPORT(void) _nc_tty_flags_changed (void);
extern NCURSES_EXPORT(void) _nc_tty_flush (void);
extern NCURSES_EXPORT(void) _nc_tty_input_resume (void);
extern NCURSES_EXPORT(void) _nc_tty_input_suspend (void);

struct tty_input_data {
	int             _ifd;           /* input file ptr for screen        */
	int             _keypad_xmit;   /* current terminal state           */
	int             _meta_on;       /* current terminal state           */

	/*
	 * These are the data that support the mouse interface.
	 */
	bool            (*_mouse_event) (SCREEN *);
	bool            (*_mouse_inline)(SCREEN *);
	bool            (*_mouse_parse) (int);
	void            (*_mouse_resume)(SCREEN *);
	void            (*_mouse_wrap)  (SCREEN *);
	int             _mouse_fd;      /* file-descriptor, if any */
	int             mousetype;
};

#endif /* TTY_INPUT_H */
