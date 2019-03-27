/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
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

#define CHAT_EXPECT 0
#define CHAT_SEND   1
#define CHAT_DONE   2
#define CHAT_FAILED 3

#define MAXABORTS   50

struct physical;

struct chat {
  struct fdescriptor desc;
  struct physical *physical;

  int state;				/* Our CHAT_* status */

  char script[LINE_LEN];		/* Our arg buffer */
  char *argv[MAXARGS];			/* Our arguments (pointing to script) */
  int argc;				/* Number of argv's */

  int arg;				/* Our current arg number */
  char exp[LINE_LEN];			/* Our translated current argument */
  char *argptr;				/* Our current arg pointer */
  int arglen;				/* The length of argptr */
  char *nargptr;			/* Our next for expect-send-expect */

  char buf[LINE_LEN*2];			/* Our input */
  char *bufstart;			/* start of relevant data */
  char *bufend;				/* end of relevant data */

  int TimeoutSec;			/* Expect timeout value */
  int TimedOut;				/* We timed out */

  const char *phone;			/* Our phone number */

  struct {
    struct {
      char *data;			/* Abort the dial if we get one */
      int len;
    } string[MAXABORTS];
    int num;				/* How many AbortStrings */
  } abort;

  struct pppTimer pause;		/* Inactivity timer */
  struct pppTimer timeout;		/* TimeoutSec timer */
};

#define descriptor2chat(d) \
  ((d)->type == CHAT_DESCRIPTOR ? (struct chat *)(d) : NULL)
#define	VECSIZE(v)	(sizeof(v) / sizeof(v[0]))

extern void chat_Init(struct chat *, struct physical *);
extern int chat_Setup(struct chat *, const char *, const char *);
extern void chat_Finish(struct chat *);
extern void chat_Destroy(struct chat *);
