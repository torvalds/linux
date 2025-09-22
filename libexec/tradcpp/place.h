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

#ifndef PLACE_H
#define PLACE_H

#include "bool.h"

enum places {
	P_NOWHERE,
	P_BUILTIN,
	P_COMMANDLINE,
	P_FILE,
};
struct place {
	enum places type;
	const struct placefile *file;
	unsigned line;
	unsigned column;
};

void place_init(void);
void place_cleanup(void);

void place_setnowhere(struct place *p);
void place_setbuiltin(struct place *p, unsigned num);
void place_setcommandline(struct place *p, unsigned word, unsigned column);
void place_setfilestart(struct place *p, const struct placefile *pf);

void place_addcolumns(struct place *, unsigned cols);
void place_addlines(struct place *, unsigned lines);

const char *place_getname(const struct place *);
const char *place_getparsedir(const struct place *incplace);
bool place_eq(const struct place *, const struct place *);
bool place_samefile(const struct place *, const struct place *);

void place_changefile(struct place *p, const char *name);

const struct placefile *place_addfile(const struct place *incplace,
				      const char *name, bool fromsystemdir);

#endif /* PLACE_H */
