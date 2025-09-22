/*	$OpenBSD: bootarg.c,v 1.12 2015/09/02 01:52:26 yasuoka Exp $	*/

/*
 * Copyright (c) 1997,1998 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <lib/libsa/stand.h>
#include <stand/boot/bootarg.h>

static bootarg_t *bootarg_list;

void
addbootarg(int t, size_t l, void *p)
{
	bootarg_t *q = alloc(sizeof(*q) + l - sizeof(q->ba_arg));

	q->ba_type = t;
	q->ba_size = sizeof(*q) + l - sizeof(q->ba_arg);
	bcopy(p, q->ba_arg, l);
	q->ba_next = bootarg_list;
	bootarg_list = q;
}

void
makebootargs(caddr_t v, size_t *lenp)
{
	bootarg_t *p;
	u_char *q;
	size_t l;

	/* get total size */
	l = sizeof(*p);
	for (p = bootarg_list; p != NULL; p = p->ba_next) {
		l += p->ba_size;
		if (*lenp < l) {
#ifdef DEBUG
			printf("makebootargs: too many args\n");
#endif
			l -= p->ba_size;
			break;
		}
	}
	*lenp = l;
	/* copy them out */
	for (p = bootarg_list, q = v;
	     p != NULL && ((q + p->ba_size) - (u_char *)v) <= l - sizeof(*p);
	     q += p->ba_size, p = p->ba_next) {
#ifdef DEBUG
		printf("%d,%d ", p->ba_type, p->ba_size);
#endif
		bcopy(p, q, p->ba_size);
	}
	p = (bootarg_t *)q;
	p->ba_type = BOOTARG_END;
}

void
makebootargs32(caddr_t v, size_t *lenp)
{
	bootarg_t       *p;
	u_char          *q;
	size_t           l;
	size_t           adj;
	struct bootarg32 {
		int      ba_type;
		int      ba_size;
		int      ba_nextX;
		char     ba_arg[1];
	}               *p32;

	adj = (ssize_t)sizeof(struct bootarg32) - sizeof(bootarg_t);
	/* get total size */
	l = sizeof(*p32);
	for (p = bootarg_list; p != NULL; p = p->ba_next) {
		l += p->ba_size + adj;
		if (*lenp < l) {
#ifdef DEBUG
			printf("makebootargs: too many args\n");
#endif
			l -= p->ba_size + adj;
			break;
		}
	}
	*lenp = l;
	/* copy them out */
	for (p = bootarg_list, q = v; p != NULL; p = p->ba_next) {
		if (q + p->ba_size + adj > (u_char *)v + l - sizeof(*p32))
			break;
		p32 = (struct bootarg32 *)q;
		p32->ba_type = p->ba_type;
		p32->ba_size = p->ba_size + adj;
		bcopy(p->ba_arg, p32->ba_arg,
		    p->ba_size - (sizeof(*p) - sizeof(p->ba_arg)));
#ifdef DEBUG
		printf("%d,%d ", p->ba_type, p->ba_size);
#endif
		q += p32->ba_size;
	}
	p32 = (struct bootarg32 *)q;
	p32->ba_type = BOOTARG_END;
}
