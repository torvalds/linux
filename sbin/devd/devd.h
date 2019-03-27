/*-
 * DEVD (Device action daemon)
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 M. Warner Losh <imp@freebsd.org>.
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

#ifndef DEVD_H
#define DEVD_H

/** @warning This file needs to be purely 'C' compatible.
 */
struct event_proc;
struct eps;
__BEGIN_DECLS
void add_attach(int, struct event_proc *);
void add_detach(int, struct event_proc *);
void add_directory(const char *);
void add_nomatch(int, struct event_proc *);
void add_notify(int, struct event_proc *);
struct event_proc *add_to_event_proc(struct event_proc *, struct eps *);
struct eps *new_match(const char *, const char *);
struct eps *new_media(const char *, const char *);
struct eps *new_action(const char *);
void set_pidfile(const char *);
void set_variable(const char *, const char *);
void yyerror(const char *s);
int  yylex(void);
int  yyparse(void);
extern int lineno;
__END_DECLS

#define PATH_DEVCTL	"/dev/devctl"
#define DEVCTL_MAXBUF	8192

#endif /* DEVD_H */
