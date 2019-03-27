/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define TNOP		256
#define TLSH		257
#define TRSH		258
#define TCLK		259
#define TNLK		260
#define TSLK		261
#define TLALT		262
#define TLCTR		263
#define TNEXT		264
#define TRCTR		265
#define TRALT		266
#define TALK		267
#define TASH		268
#define TMETA		269
#define TRBT		270
#define TDBG		271
#define TFUNC		272
#define TSCRN		273
#define TLET		274
#define TNUM		275
#define TFLAG		276
#define TBTAB		277
#define TSUSP		278
#define TACC		279
#define TSPSC		280
#define TPREV		281
#define TPANIC		282
#define TLSHA		283
#define TRSHA		284
#define TLCTRA		285
#define TRCTRA		286
#define TLALTA		287
#define TRALTA		288
#define THALT		289
#define TPDWN		290
#define TPASTE		291

extern int number;
extern char letter;
extern FILE *yyin;

extern int yylex(void);
