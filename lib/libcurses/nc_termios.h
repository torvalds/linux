/****************************************************************************
 * Copyright 2018,2020 Thomas E. Dickey                                     *
 * Copyright 2011-2014,2017 Free Software Foundation, Inc.                  *
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
 *  Author: Thomas E. Dickey                        2011                    *
 ****************************************************************************/

/* $Id: nc_termios.h,v 1.1 2023/10/17 09:52:08 nicm Exp $ */

#ifndef NC_TERMIOS_included
#define NC_TERMIOS_included 1

#include <ncurses_cfg.h>

#if HAVE_TERMIOS_H && HAVE_TCGETATTR

#else /* !HAVE_TERMIOS_H */

#if HAVE_TERMIO_H

/* Add definitions to make termio look like termios.
 * But ifdef it, since there are some implementations
 * that try to do this for us in a fake <termio.h>.
 */
#ifndef TCSADRAIN
#define TCSADRAIN TCSETAW
#endif
#ifndef TCSAFLUSH
#define TCSAFLUSH TCSETAF
#endif
#ifndef tcsetattr
#define tcsetattr(fd, cmd, arg) ioctl(fd, cmd, arg)
#endif
#ifndef tcgetattr
#define tcgetattr(fd, arg) ioctl(fd, TCGETA, arg)
#endif
#ifndef cfgetospeed
#define cfgetospeed(t) ((t)->c_cflag & CBAUD)
#endif
#ifndef TCIFLUSH
#define TCIFLUSH 0
#endif
#ifndef tcflush
#define tcflush(fd, arg) ioctl(fd, TCFLSH, arg)
#endif

#if defined(EXP_WIN32_DRIVER)
#undef TERMIOS
#endif

#else /* !HAVE_TERMIO_H */

#if defined(_WIN32) && !defined(EXP_WIN32_DRIVER)

/* lflag bits */
#define ISIG	0x0001
#define ICANON	0x0002
#define ECHO	0x0004
#define ECHOE	0x0008
#define ECHOK	0x0010
#define ECHONL	0x0020
#define NOFLSH	0x0040
#define IEXTEN	0x0100

#define VEOF	     4
#define VERASE	     5
#define VINTR	     6
#define VKILL	     7
#define VMIN	     9
#define VQUIT	    10
#define VTIME	    16

/* iflag bits */
#define IGNBRK	0x00001
#define BRKINT	0x00002
#define IGNPAR	0x00004
#define INPCK	0x00010
#define ISTRIP	0x00020
#define INLCR	0x00040
#define IGNCR	0x00080
#define ICRNL	0x00100
#define IXON	0x00400
#define IXOFF	0x01000
#define PARMRK	0x10000

/* oflag bits */
#define OPOST	0x00001

/* cflag bits */
#define CBAUD	 0x0100f
#define B0	 0x00000
#define B50	 0x00001
#define B75	 0x00002
#define B110	 0x00003
#define B134	 0x00004
#define B150	 0x00005
#define B200	 0x00006
#define B300	 0x00007
#define B600	 0x00008
#define B1200	 0x00009
#define B1800	 0x0000a
#define B2400	 0x0000b
#define B4800	 0x0000c
#define B9600	 0x0000d

#define CSIZE	 0x00030
#define CS8	 0x00030
#define CSTOPB	 0x00040
#define CREAD	 0x00080
#define PARENB	 0x00100
#define PARODD	 0x00200
#define HUPCL	 0x00400
#define CLOCAL	 0x00800

#define TCIFLUSH	0
#define TCSADRAIN	3

#ifndef cfgetospeed
#define cfgetospeed(t) ((t)->c_cflag & CBAUD)
#endif

#ifndef tcsetattr
#define tcsetattr(fd, opt, arg) _nc_mingw_tcsetattr(fd, opt, arg)
#endif

#ifndef tcgetattr
#define tcgetattr(fd, arg) _nc_mingw_tcgetattr(fd, arg)
#endif

#ifndef tcflush
#define tcflush(fd, queue) _nc_mingw_tcflush(fd, queue)
#endif

#undef  ttyname
#define ttyname(fd) NULL

#endif /* _WIN32 */
#endif /* HAVE_TERMIO_H */

#endif /* HAVE_TERMIOS_H */

#endif /* NC_TERMIOS_included */
