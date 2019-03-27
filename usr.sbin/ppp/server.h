/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Brian Somers <brian@Awfulhak.org>
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

struct bundle;

struct server {
  struct fdescriptor desc;
  int fd;

  struct {
    char passwd[50];

    char sockname[PATH_MAX];		/* Points to local socket path */
    mode_t mask;

    u_short port;			/* tcp socket */
  } cfg;
};

enum server_stat {
  SERVER_OK,				/* Diagnostic socket available */
  SERVER_INVALID,			/* Bad args, can't be set up */
  SERVER_FAILED,			/* Failed - lack of resources */
  SERVER_UNSET				/* Not already set up */
};

#define descriptor2server(d) \
  ((d)->type == SERVER_DESCRIPTOR ? (struct server *)(d) : NULL)

extern struct server server;

extern enum server_stat server_LocalOpen(struct bundle *, const char *, mode_t);
extern enum server_stat server_TcpOpen(struct bundle *, u_short);
extern enum server_stat server_Reopen(struct bundle *);
extern int server_Close(struct bundle *);
extern int server_Clear(struct bundle *);
