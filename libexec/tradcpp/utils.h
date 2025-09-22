/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stddef.h>
#include "bool.h"

struct place;

#if defined(__CLANG__) || defined(__GNUC__)
#define PF(a, b) __attribute__((__format__(__printf__, a, b)))
#define DEAD __attribute__((__noreturn__))
#define UNUSED __attribute__((__unused__))
#else
#define PF(a, b)
#define DEAD
#define UNUSED
#endif

#define HOWMANY(arr) (sizeof(arr)/sizeof((arr)[0]))

extern const char ws[];
extern const char alnum[];


void *domalloc(size_t len);
void *dorealloc(void *ptr, size_t oldlen, size_t newlen);
void dofree(void *ptr, size_t len);

char *dostrdup(const char *s);
char *dostrdup2(const char *s, const char *t);
char *dostrdup3(const char *s, const char *t, const char *u);
char *dostrndup(const char *s, size_t len);
void dostrfree(char *s);

size_t notrailingws(char *buf, size_t len);
bool is_identifier(const char *str);

/* in place.c */
void complain_init(const char *progname);
PF(2, 3) void complain(const struct place *, const char *fmt, ...);
void complain_fail(void);
bool complain_failed(void);

void debuglog_open(const struct place *p, /*const*/ char *file);
void debuglog_close(void);
PF(2, 3) void debuglog(const struct place *p, const char *fmt, ...);
PF(3, 4) void debuglog2(const struct place *p, const struct place *p2,
			const char *fmt, ...);

/* in main.c */
void freestringlater(char *s);
DEAD void die(void);
