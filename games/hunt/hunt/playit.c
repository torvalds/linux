/*	$OpenBSD: playit.c,v 1.13 2016/08/27 02:06:40 guenther Exp $	*/
/*	$NetBSD: playit.c,v 1.4 1997/10/20 00:37:15 lukem Exp $	*/
/*
 * Copyright (c) 1983-2003, Regents of the University of California.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are 
 * met:
 * 
 * + Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in the 
 *   documentation and/or other materials provided with the distribution.
 * + Neither the name of the University of California, San Francisco nor 
 *   the names of its contributors may be used to endorse or promote 
 *   products derived from this software without specific prior written 
 *   permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS 
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED 
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/select.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "display.h"
#include "hunt.h"
#include "client.h"

static int	nchar_send;
static FLAG	Last_player;
static int	Otto_expect;

# define	MAX_SEND	5

/*
 * ibuf is the input buffer used for the stream from the driver.
 * It is small because we do not check for user input when there
 * are characters in the input buffer.
 */
static int		icnt = 0;
static unsigned char	ibuf[256], *iptr = ibuf;

#define	GETCHR()	(--icnt < 0 ? getchr() : *iptr++)

static	unsigned char	getchr(void);
static	void		send_stuff(void);

/*
 * playit:
 *	Play a given game, handling all the curses commands from
 *	the driver.
 */
void
playit(void)
{
	int		ch;
	int		y, x;
	u_int32_t	version;
	int		otto_y, otto_x;
	char		otto_face = ' ';
	int		chars_processed;

	if (read(Socket, &version, sizeof version) != sizeof version) {
		bad_con();
	}
	if (ntohl(version) != HUNT_VERSION) {
		bad_ver();
	}
	errno = 0;
	nchar_send = MAX_SEND;
	Otto_expect = 0;
	while ((ch = GETCHR()) != EOF) {
		switch (ch & 0377) {
		  case MOVE:
			y = GETCHR();
			x = GETCHR();
			display_move(y, x);
			break;

		  case CLRTOEOL:
			display_clear_eol();
			break;
		  case CLEAR:
			display_clear_the_screen();
			break;
		  case REFRESH:
			display_refresh();
			break;
		  case REDRAW:
			display_redraw_screen();
			display_refresh();
			break;
		  case ENDWIN:
			display_refresh();
			if ((ch = GETCHR()) == LAST_PLAYER)
				Last_player = TRUE;
			ch = EOF;
			goto out;
		  case BELL:
			display_beep();
			break;
		  case READY:
			chars_processed = GETCHR();
			display_refresh();
			if (nchar_send < 0)
				tcflush(STDIN_FILENO, TCIFLUSH);
			nchar_send = MAX_SEND;
			if (Otto_mode) {
				/*
				 * The driver returns the number of keypresses
				 * that it has processed. Use this to figure
				 * out if otto's commands have completed.
				 */
				Otto_expect -= chars_processed;
				if (Otto_expect == 0) {
					/* not very fair! */
					static char buf[MAX_SEND * 2];
					int len;

					/* Ask otto what it wants to do: */
					len = otto(otto_y, otto_x, otto_face,
						buf, sizeof buf);
					if (len) {
						/* Pass it on to the driver: */
						write(Socket, buf, len);
						/* Update expectations: */
						Otto_expect += len;
					}
				}
			}
			break;
		  case ADDCH:
			ch = GETCHR();
			/* FALLTHROUGH */
		  default:
			if (!isprint(ch))
				ch = ' ';
			display_put_ch(ch);
			if (Otto_mode)
				switch (ch) {
				case '<':
				case '>':
				case '^':
				case 'v':
					otto_face = ch;
					display_getyx(&otto_y, &otto_x);
					otto_x--;
					break;
				}
			break;
		}
	}
out:
	(void) close(Socket);
}

/*
 * getchr:
 *	Grab input and pass it along to the driver
 *	Return any characters from the driver
 *	When this routine is called by GETCHR, we already know there are
 *	no characters in the input buffer.
 */
static unsigned char
getchr(void)
{
	fd_set	readfds, s_readfds;
	int	nfds, s_nfds;

	FD_ZERO(&s_readfds);
	FD_SET(Socket, &s_readfds);
	FD_SET(STDIN_FILENO, &s_readfds);
	s_nfds = (Socket > STDIN_FILENO) ? Socket : STDIN_FILENO;
	s_nfds++;

one_more_time:
	do {
		errno = 0;
		readfds = s_readfds;
		nfds = s_nfds;
		nfds = select(nfds, &readfds, NULL, NULL, NULL);
	} while (nfds <= 0 && errno == EINTR);

	if (FD_ISSET(STDIN_FILENO, &readfds))
		send_stuff();
	if (!FD_ISSET(Socket, &readfds))
		goto one_more_time;
	icnt = read(Socket, ibuf, sizeof ibuf);
	if (icnt <= 0) {
		bad_con();
	}
	iptr = ibuf;
	icnt--;
	return *iptr++;
}

/*
 * send_stuff:
 *	Send standard input characters to the driver
 */
static void
send_stuff(void)
{
	int		count;
	char		*sp, *nsp;
	static char	inp[BUFSIZ];
	static char	Buf[BUFSIZ];

	/* Drain the user's keystrokes: */
	count = read(STDIN_FILENO, Buf, sizeof Buf);
	if (count < 0)
		err(1, "read");
	if (count == 0)
		return;

	if (nchar_send <= 0 && !no_beep) {
		display_beep();
		return;
	}

	/*
	 * look for 'q'uit commands; if we find one,
	 * confirm it.  If it is not confirmed, strip
	 * it out of the input
	 */
	Buf[count] = '\0';
	for (sp = Buf, nsp = inp; *sp != '\0'; sp++, nsp++) {
		*nsp = map_key[(int)*sp];
		if (*nsp == 'q')
			intr(0);
	}
	count = nsp - inp;
	if (count) {
		nchar_send -= count;
		if (nchar_send < 0)
			count += nchar_send;
		(void) write(Socket, inp, count);
		if (Otto_mode) {
			/*
			 * The user can insert commands over otto.
			 * So, otto shouldn't be alarmed when the 
			 * server processes more than otto asks for.
			 */
			Otto_expect += count;
		}
	}
}

/*
 * quit:
 *	Handle the end of the game when the player dies
 */
int
quit(int old_status)
{
	int	explain, ch;

	if (Last_player)
		return Q_QUIT;
	if (Otto_mode)
		return otto_quit(old_status);
	display_move(HEIGHT, 0);
	display_put_str("Re-enter game [ynwo]? ");
	display_clear_eol();
	explain = FALSE;
	for (;;) {
		display_refresh();
		if (isupper(ch = getchar()))
			ch = tolower(ch);
		if (ch == 'y')
			return old_status;
		else if (ch == 'o')
			break;
		else if (ch == 'n') {
			display_move(HEIGHT, 0);
			display_put_str("Write a parting message [yn]? ");
			display_clear_eol();
			display_refresh();
			for (;;) {
				if (isupper(ch = getchar()))
					ch = tolower(ch);
				if (ch == 'y')
					goto get_message;
				if (ch == 'n')
					return Q_QUIT;
			}
		}
		else if (ch == 'w') {
			static	char	buf[WIDTH + WIDTH % 2];
			char		*cp, c;

get_message:
			c = ch;		/* save how we got here */
			display_move(HEIGHT, 0);
			display_put_str("Message: ");
			display_clear_eol();
			display_refresh();
			cp = buf;
			for (;;) {
				display_refresh();
				if ((ch = getchar()) == '\n' || ch == '\r')
					break;
				if (display_iserasechar(ch))
				{
					if (cp > buf) {
						int y, x;

						display_getyx(&y, &x);
						display_move(y, x - 1);
						cp -= 1;
						display_clear_eol();
					}
					continue;
				}
				else if (display_iskillchar(ch))
				{
					int y, x;

					display_getyx(&y, &x);
					display_move(y, x - (cp - buf));
					cp = buf;
					display_clear_eol();
					continue;
				} else if (!isprint(ch)) {
					display_beep();
					continue;
				}
				display_put_ch(ch);
				*cp++ = ch;
				if (cp + 1 >= buf + sizeof buf)
					break;
			}
			*cp = '\0';
			Send_message = buf;
			return (c == 'w') ? old_status : Q_MESSAGE;
		}
		display_beep();
		if (!explain) {
			display_put_str("(Yes, No, Write message, or Options) ");
			explain = TRUE;
		}
	}

	display_move(HEIGHT, 0);
	display_put_str("Scan, Cloak, Flying, or Quit? ");
	display_clear_eol();
	display_refresh();
	explain = FALSE;
	for (;;) {
		if (isupper(ch = getchar()))
			ch = tolower(ch);
		if (ch == 's')
			return Q_SCAN;
		else if (ch == 'c')
			return Q_CLOAK;
		else if (ch == 'f')
			return Q_FLY;
		else if (ch == 'q')
			return Q_QUIT;
		display_beep();
		if (!explain) {
			display_put_str("[SCFQ] ");
			explain = TRUE;
		}
		display_refresh();
	}
}

/*
 * do_message:
 *	Send a message to the driver and return
 */
void
do_message(void)
{
	u_int32_t	version;

	if (read(Socket, &version, sizeof version) != sizeof version) {
		bad_con();
	}
	if (ntohl(version) != HUNT_VERSION) {
		bad_ver();
	}
	if (write(Socket, Send_message, strlen(Send_message)) < 0) {
		bad_con();
	}
	(void) close(Socket);
}
