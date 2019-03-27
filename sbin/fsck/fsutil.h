/*	$NetBSD: fsutil.h,v 1.114 2009/10/21 01:07:46 snj Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
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

void pfatal(const char *, ...) __printflike(1, 2);
void pwarn(const char *, ...) __printflike(1, 2);
void perr(const char *, ...) __printflike(1, 2);
void panic(const char *, ...) __dead2 __printflike(1, 2);
const char *devcheck(const char *);
const char *cdevname(void);
void setcdevname(const char *, int);
struct statfs *getmntpt(const char *);
void *emalloc(size_t);
void *erealloc(void *, size_t);
char *estrdup(const char *);

#define	CHECK_PREEN	0x0001
#define	CHECK_VERBOSE	0x0002
#define	CHECK_DEBUG	0x0004
#define	CHECK_BACKGRD	0x0008
#define	DO_BACKGRD	0x0010
#define	CHECK_CLEAN	0x0020

struct fstab;
int checkfstab(int, int (*)(struct fstab *), 
    int (*) (const char *, const char *, const char *, const char *, pid_t *));
