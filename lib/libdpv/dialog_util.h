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
 *
 * $FreeBSD$
 */

#ifndef _DIALOG_UTIL_H_
#define _DIALOG_UTIL_H_

#include <sys/types.h>

#include "dialogrc.h"

#define DIALOG_SPAWN_DEBUG	0	/* Debug spawning of [X]dialog(1) */

/* dialog(3) and [X]dialog(1) characteristics */
#define DIALOG		"dialog"
#define XDIALOG		"Xdialog"
#define PROMPT_MAX	16384
#define ENV_DIALOG	"DIALOG"
#define ENV_USE_COLOR	"USE_COLOR"
#define ENV_XDIALOG_HIGH_DIALOG_COMPAT	"XDIALOG_HIGH_DIALOG_COMPAT"
extern uint8_t dialog_test;
extern uint8_t use_libdialog;
extern uint8_t use_dialog;
extern uint8_t use_xdialog;
extern uint8_t use_color;
extern char dialog[];

/* dialog(3) and [X]dialog(1) functionality */
extern char *title, *backtitle;
extern int dheight, dwidth;

__BEGIN_DECLS
uint8_t		 dialog_prompt_nlstate(const char *_prompt);
void		 dialog_maxsize_free(void);
char		*dialog_prompt_lastline(char *_prompt, uint8_t _nlstate);
int		 dialog_maxcols(void);
int		 dialog_maxrows(void);
int		 dialog_prompt_wrappedlines(char *_prompt, int _ncols,
		    uint8_t _nlstate);
int		 dialog_spawn_gauge(char *_init_prompt, pid_t *_pid);
int		 tty_maxcols(void);
#define		 tty_maxrows() dialog_maxrows()
unsigned int	 dialog_prompt_longestline(const char *_prompt,
		    uint8_t _nlstate);
unsigned int	 dialog_prompt_numlines(const char *_prompt, uint8_t _nlstate);
__END_DECLS

#endif /* !_DIALOG_UTIL_H_ */
