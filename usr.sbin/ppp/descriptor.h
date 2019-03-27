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

#define PHYSICAL_DESCRIPTOR (1)
#define SERVER_DESCRIPTOR (2)
#define PROMPT_DESCRIPTOR (3)
#define CHAT_DESCRIPTOR (4)
#define DATALINK_DESCRIPTOR (5)
#define BUNDLE_DESCRIPTOR (6)
#define MPSERVER_DESCRIPTOR (7)
#define RADIUS_DESCRIPTOR (8)
#define CHAP_DESCRIPTOR (9)

struct bundle;

struct fdescriptor {
  int type;

  int (*UpdateSet)(struct fdescriptor *, fd_set *, fd_set *, fd_set *, int *);
  int (*IsSet)(struct fdescriptor *, const fd_set *);
  void (*Read)(struct fdescriptor *, struct bundle *, const fd_set *);
  int (*Write)(struct fdescriptor *, struct bundle *, const fd_set *);
};

#define descriptor_UpdateSet(d, r, w, e, n) ((*(d)->UpdateSet)(d, r, w, e, n))
#define descriptor_IsSet(d, s) ((*(d)->IsSet)(d, s))
#define descriptor_Read(d, b, f) ((*(d)->Read)(d, b, f))
#define descriptor_Write(d, b, f) ((*(d)->Write)(d, b, f))
