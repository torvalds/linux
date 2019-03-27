/*-
 * Copyright (c) 2013-2014 Devin Teske <dteske@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <curses.h>
#include <dialog.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "dialog_util.h"
#include "status.h"

/* static globals */
static char *status_buf = NULL;
static int status_bufsize = -1;
static int status_row;
static int status_width;

/*
 * Print a `one-liner' status message at the bottom of the screen. Messages are
 * trimmed to fit within the console length (ANSI coloring not accounted for).
 */
void
status_printf(const char *fmt, ...)
{
	int n, attrs;
	chtype color = dlg_color_pair(dlg_color_table[BUTTON_ACTIVE_ATTR].fg,
	    dlg_color_table[SCREEN_ATTR].bg) | A_BOLD;
	va_list args;

	status_row = tty_maxrows() - 1;
	status_width = tty_maxcols();

	/* NULL is a special convention meaning "erase the old stuff" */
	if (fmt == NULL) {
		move(status_row, 0);
		clrtoeol();
		return;
	}

	/* Resize buffer if terminal width is greater */
	if ((status_width + 1) > status_bufsize) {
		status_buf = realloc(status_buf, status_width + 1);
		if (status_buf == NULL) {
			status_bufsize = -1;
			return;
		}
		status_bufsize = status_width + 1;
	}

	/* Print the message within a space-filled buffer */
	memset(status_buf, ' ', status_width);
	va_start(args, fmt);
	n = vsnprintf(status_buf, status_width + 1, fmt, args);
	va_end(args);

	/* If vsnprintf(3) produced less bytes than the maximum, change the
	 * implicitly-added NUL-terminator into a space and terminate at max */
	if (n < status_width) {
		status_buf[n] = ' ';
		status_buf[status_width] = '\0';
	}

	/* Print text in screen bg, button active fg, and bold */
	attrs = getattrs(stdscr);
	attrset(color);
	mvaddstr(status_row, 0, status_buf);
	attrset(attrs);

	/* Seat the cursor over the last character at absolute lower-right */
	move(status_row, status_width - 1);
	refresh();
}

/*
 * Free allocated items initialized by status_printf()
 */
void
status_free(void)
{
	if (status_buf != NULL) {
		free(status_buf);
		status_buf = NULL;
	}
}
