/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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

struct cmdtab;
struct bundle;
struct datalink;
struct prompt;

struct cmdargs {
  struct cmdtab const *cmdtab;		/* The entire command table */
  struct cmdtab const *cmd;		/* This command entry */
  int argc;				/* Number of arguments (excluding cmd */
  int argn;				/* Argument to start processing from */
  char const *const *argv;		/* Arguments */
  struct bundle *bundle;		/* Our bundle */
  struct datalink *cx;			/* Our context */
  struct prompt *prompt;		/* Who executed us */
};

struct cmdtab {
  const char *name;
  const char *alias;
  int (*func) (struct cmdargs const *);
  u_char lauth;
  const char *helpmes;
  const char *syntax;
  const void *args;
};

#define NEG_ACCEPTED (1)
#define NEG_ENABLED (2)
#define IsAccepted(x) ((x) & NEG_ACCEPTED)
#define IsEnabled(x) ((x) & NEG_ENABLED)

extern const char Version[];

extern void command_Expand(char **, int, char const *const *, struct bundle *,
                           int, pid_t);
extern void command_Free(int, char **);
extern int command_Expand_Interpret(char *, int, char *vector[MAXARGS], int);
extern int command_Interpret(char *, int, char *vector[MAXARGS]);
extern void command_Run(struct bundle *, int, char const *const *,
                        struct prompt *, const char *, struct datalink *);
extern int command_Decode(struct bundle *, char *, int, struct prompt *,
                           const char *);
extern struct link *command_ChooseLink(struct cmdargs const *);
extern const char *command_ShowNegval(unsigned);

