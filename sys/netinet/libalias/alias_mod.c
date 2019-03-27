/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Paolo Pisati <piso@FreeBSD.org>
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
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include <sys/libkern.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#else
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#endif

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#ifdef _KERNEL
#include <netinet/libalias/alias_local.h>
#include <netinet/libalias/alias_mod.h>
#else
#include "alias_local.h"
#include "alias_mod.h"
#endif

/* Protocol and userland module handlers chains. */
static TAILQ_HEAD(handler_chain, proto_handler) handler_chain =
    TAILQ_HEAD_INITIALIZER(handler_chain);

static int
attach_handler(struct proto_handler *p)
{
	struct proto_handler *b;

	TAILQ_FOREACH(b, &handler_chain, link) {
		if ((b->pri == p->pri) &&
		    (b->dir == p->dir) &&
		    (b->proto == p->proto))
			return (EEXIST);
		if (b->pri > p->pri) {
			TAILQ_INSERT_BEFORE(b, p, link);
			return (0);
		}
	}

	TAILQ_INSERT_TAIL(&handler_chain, p, link);

	return (0);
}

int
LibAliasAttachHandlers(struct proto_handler *p)
{
	int error;

	while (p->dir != NODIR) {
		error = attach_handler(p);
		if (error)
			return (error);
		p++;
	}

	return (0);
}

/* XXXGL: should be void, but no good reason to break ABI */
int
LibAliasDetachHandlers(struct proto_handler *p)
{

	while (p->dir != NODIR) {
		TAILQ_REMOVE(&handler_chain, p, link);
		p++;
	}

	return (0);
}

int
find_handler(int8_t dir, int8_t proto, struct libalias *la, struct ip *ip,
    struct alias_data *ad)
{
	struct proto_handler *p;

	TAILQ_FOREACH(p, &handler_chain, link)
		if ((p->dir & dir) && (p->proto & proto) &&
		    p->fingerprint(la, ad) == 0)
			return (p->protohandler(la, ip, ad));

	return (ENOENT);
}

struct proto_handler *
first_handler(void)
{

	return (TAILQ_FIRST(&handler_chain));
}

#ifndef _KERNEL
/* Dll manipulation code - this code is not thread safe... */
SLIST_HEAD(dll_chain, dll) dll_chain = SLIST_HEAD_INITIALIZER(dll_chain);
int
attach_dll(struct dll *p)
{
	struct dll *b;

	SLIST_FOREACH(b, &dll_chain, next) {
		if (!strncmp(b->name, p->name, DLL_LEN))
			return (EEXIST); /* Dll name conflict. */
	}
	SLIST_INSERT_HEAD(&dll_chain, p, next);
	return (0);
}

void *
detach_dll(char *p)
{
	struct dll *b, *b_tmp;
	void *error;

	b = NULL;
	error = NULL;
	SLIST_FOREACH_SAFE(b, &dll_chain, next, b_tmp)
		if (!strncmp(b->name, p, DLL_LEN)) {
			SLIST_REMOVE(&dll_chain, b, dll, next);
			error = b;
			break;
		}
	return (error);
}

struct dll *
walk_dll_chain(void)
{
	struct dll *t;

	t = SLIST_FIRST(&dll_chain);
	if (t == NULL)
		return (NULL);
	SLIST_REMOVE_HEAD(&dll_chain, next);
	return (t);
}
#endif /* !_KERNEL */
