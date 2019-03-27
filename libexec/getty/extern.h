/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)extern.h	8.1 (Berkeley) 6/4/93
 * $FreeBSD$
 */

struct delayval;
struct termios;

extern	char **environ;
extern	char editedhost[];
extern	char hostname[];
extern	struct termios tmode, omode;
extern	struct gettyflags gettyflags[];
extern	struct gettynums gettynums[];
extern	struct gettystrs gettystrs[];

int	 adelay(int, struct delayval *);
const char *autobaud(void);
int	 delaybits(void);
void	 edithost(const char *);
void	 gendefaults(void);
void	 gettable(const char *, char *);
void	 makeenv(char *[]);
const char *portselector(void);
void	 set_ttydefaults(int);
void	 setchars(void);
void	 setdefaults(void);
void	 set_flags(int);
int	 speed(int);
int	 getty_chat(char *, int, int);
