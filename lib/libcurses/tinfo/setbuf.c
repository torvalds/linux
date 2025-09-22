/* $OpenBSD: setbuf.c,v 1.5 2010/01/12 23:22:06 nicm Exp $ */

/****************************************************************************
 * Copyright (c) 1998-2003,2007 Free Software Foundation, Inc.              *
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

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
**	setbuf.c
**
**	Support for set_term(), reset_shell_mode(), reset_prog_mode().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: setbuf.c,v 1.5 2010/01/12 23:22:06 nicm Exp $")

/*
 * If the output file descriptor is connected to a tty (the typical case) it
 * will probably be line-buffered.  Keith Bostic pointed out that we don't want
 * this; it hoses people running over networks by forcing out a bunch of small
 * packets instead of one big one, so screen updates on ptys look jerky.
 * Restore block buffering to prevent this minor lossage.
 *
 * The buffer size is a compromise.  Ideally we'd like a buffer that can hold
 * the maximum possible update size (the whole screen plus cup commands to
 * change lines as it's painted).  On a 66-line xterm this can become
 * excessive.  So we min it with the amount of data we think we can get through
 * two Ethernet packets (maximum packet size - 100 for TCP/IP overhead).
 *
 * Why two ethernet packets?  It used to be one, on the theory that said
 * packets define the maximum size of atomic update.  But that's less than the
 * 2000 chars on a 25 x 80 screen, and we don't want local updates to flicker
 * either.  Two packet lengths will handle up to a 35 x 80 screen.
 *
 * The magic '6' is the estimated length of the end-of-line cup sequence to go
 * to the next line.  It's generous.  We used to mess with the buffering in
 * init_mvcur() after cost computation, but that lost the sequences emitted by
 * init_acs() in setupscreen().
 *
 * "The setvbuf function may be used only after the stream pointed to by stream
 * has been associated with an open file and before any other operation is
 * performed on the stream." (ISO 7.9.5.6.)
 *
 * Grrrr...
 *
 * On a lighter note, many implementations do in fact allow an application to
 * reset the buffering after it has been written to.  We try to do this because
 * otherwise we leave stdout in buffered mode after endwin() is called.  (This
 * also happens with SVr4 curses).
 *
 * There are pros/cons:
 *
 * con:
 *	There is no guarantee that we can reestablish buffering once we've
 *	dropped it.
 *
 *	We _may_ lose data if the implementation does not coordinate this with
 *	fflush.
 *
 * pro:
 *	An implementation is more likely to refuse to change the buffering than
 *	to do it in one of the ways mentioned above.
 *
 *	The alternative is to have the application try to change buffering
 *	itself, which is certainly no improvement.
 *
 * Just in case it does not work well on a particular system, the calls to
 * change buffering are all via the macro NC_BUFFERED.  Some implementations
 * do indeed get confused by changing setbuf on/off, and will overrun the
 * buffer.  So we disable this by default (there may yet be a workaround).
 */
NCURSES_EXPORT(void)
_nc_set_buffer(FILE *ofp, bool buffered)
{
    /* optional optimization hack -- do before any output to ofp */
#if HAVE_SETVBUF || HAVE_SETBUFFER
    if (SP->_buffered != buffered) {
	unsigned buf_len;
	char *buf_ptr;

	if (getenv("NCURSES_NO_SETBUF") != 0)
	    return;

	fflush(ofp);
#ifdef __DJGPP__
	setmode(ofp, O_BINARY);
#endif
	if (buffered != 0) {
	    buf_len = min(LINES * (COLS + 6), 2800);
	    if ((buf_ptr = SP->_setbuf) == 0) {
		if ((buf_ptr = typeMalloc(char, buf_len)) == NULL)
		      return;
		SP->_setbuf = buf_ptr;
		/* Don't try to free this! */
	    }
#if !USE_SETBUF_0
	    else
		return;
#endif
	} else {
#if !USE_SETBUF_0
	    return;
#else
	    buf_len = 0;
	    buf_ptr = 0;
#endif
	}

#if HAVE_SETVBUF
#ifdef SETVBUF_REVERSED		/* pre-svr3? */
	(void) setvbuf(ofp, buf_ptr, buf_len, buf_len ? _IOFBF : _IOLBF);
#else
	(void) setvbuf(ofp, buf_ptr, buf_len ? _IOFBF : _IOLBF, buf_len);
#endif
#elif HAVE_SETBUFFER
	(void) setbuffer(ofp, buf_ptr, (int) buf_len);
#endif

	SP->_buffered = buffered;
    }
#endif /* HAVE_SETVBUF || HAVE_SETBUFFER */
}
