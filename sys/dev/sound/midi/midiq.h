/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Mathew Kanner
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

#ifndef MIDIQ_H
#define MIDIQ_H

#define MIDIQ_MOVE(a,b,c) bcopy(b,a,c)

#define MIDIQ_HEAD(name, type)          \
struct name {                           \
        int h, t, s;                    \
        type * b;                        \
}

#define MIDIQ_INIT(head, buf, size) do {                \
        (head).h=(head).t=0;                          \
        (head).s=size;                                 \
        (head).b=buf;                                  \
} while (0)

#define MIDIQ_EMPTY(head)       ((head).h == (head).t )

#define MIDIQ_LENBASE(head)         ((head).h - (head).t < 0 ? \
                                        (head).h - (head).t + (head).s : \
                                        (head).h - (head).t)

#define MIDIQ_FULL(head)        ((head).h == -1)
#define MIDIQ_AVAIL(head)       (MIDIQ_FULL(head) ? 0 : (head).s - MIDIQ_LENBASE(head))
#define MIDIQ_LEN(head)		((head).s - MIDIQ_AVAIL(head))
#define MIDIQ_DEBUG 0
/*
 * No protection against overflow, underflow
 */
#define MIDIQ_ENQ(head, buf, size) do {                                                                 \
		if(MIDIQ_DEBUG)\
                	printf("#1 %p %p bytes copied %jd tran req s %d h %d t %d\n",            \
			       &(head).b[(head).h], (buf),                                        \
			       (intmax_t)(sizeof(*(head).b) *                                     \
					  MIN( (size), (head).s - (head).h) ),                   \
			       (size), (head).h, (head).t);               \
                MIDIQ_MOVE(&(head).b[(head).h], (buf), sizeof(*(head).b) * MIN((size), (head).s - (head).h));                       \
                if( (head).s - (head).h < (size) ) {                                                    \
			if(MIDIQ_DEBUG) \
                        	printf("#2 %p %p bytes copied %jd\n",  (head).b, (buf) + (head).s - (head).h, (intmax_t)sizeof(*(head).b) * ((size) - (head).s + (head).h) );      \
                        MIDIQ_MOVE((head).b, (buf) + (head).s - (head).h, sizeof(*(head).b) * ((size) - (head).s + (head).h) );      \
		} \
                (head).h+=(size);                                                                         \
                (head).h%=(head).s;                                                                     \
		if(MIDIQ_EMPTY(head)) (head).h=-1; \
		if(MIDIQ_DEBUG)\
                	printf("#E h %d t %d\n", (head).h, (head).t);                       \
} while (0)

#define MIDIQ_DEQ_I(head, buf, size, move, update) do {                                                                 \
		if(MIDIQ_FULL(head)) (head).h=(head).t; \
		if(MIDIQ_DEBUG)\
                	printf("#1 %p %p bytes copied %jd tran req s %d h %d t %d\n", &(head).b[(head).t], (buf), (intmax_t)sizeof(*(head).b) * MIN((size), (head).s - (head).t), (size), (head).h, (head).t);                       \
                if (move) MIDIQ_MOVE((buf), &(head).b[(head).t], sizeof(*(head).b) * MIN((size), (head).s - (head).t));                       \
                if( (head).s - (head).t < (size) ) {                                                    \
			if(MIDIQ_DEBUG) \
                        	printf("#2 %p %p bytes copied %jd\n",  (head).b, (buf) + (head).s - (head).t, (intmax_t)sizeof(*(head).b) * ((size) - (head).s + (head).t) );      \
                        if (move) MIDIQ_MOVE((buf) + (head).s - (head).t, (head).b, sizeof(*(head).b) * ((size) - (head).s + (head).t) );      \
		} \
		if (update) { \
                (head).t+=(size);                                                                         \
                (head).t%=(head).s;                                                                     \
		} else { \
		  if (MIDIQ_EMPTY(head)) (head).h=-1; \
		} \
		if(MIDIQ_DEBUG)\
                	printf("#E h %d t %d\n", (head).h, (head).t);                       \
} while (0)

#define MIDIQ_SIZE(head) ((head).s)
#define MIDIQ_CLEAR(head) ((head).h = (head).t = 0)
#define MIDIQ_BUF(head) ((head).b)
#define MIDIQ_DEQ(head, buf, size) MIDIQ_DEQ_I(head, buf, size, 1, 1)
#define MIDIQ_PEEK(head, buf, size) MIDIQ_DEQ_I(head, buf, size, 1, 0)
#define MIDIQ_POP(head, size) MIDIQ_DEQ_I(head, &head, size, 0, 1)

#endif
