/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * $Begemot: libunimsg/netnatm/unimsg.h,v 1.4 2004/07/08 08:21:46 brandt Exp $
 *
 * This defines the structure of messages as handled by this library.
 */
#ifndef _NETNATM_UNIMSG_H_
#define _NETNATM_UNIMSG_H_

#include <sys/types.h>
#ifdef _KERNEL
#ifdef __FreeBSD__
#include <sys/systm.h>
#endif
#include <sys/stdint.h>
#else
#include <string.h>
#include <stdint.h>
#endif

struct uni_msg {
	u_char		*b_wptr;	/* tail pointer */
	u_char		*b_rptr;	/* head pointer */
	u_char		*b_buf;		/* data buffer */
	u_char		*b_lim;		/* end of data buffer */
};

/* return the current length of the message */
#define uni_msg_len(M)		((size_t)((M)->b_wptr - (M)->b_rptr))

/* return the number of space behind the message */
#define uni_msg_space(M)	((size_t)((M)->b_lim - (M)->b_wptr))

/* return the amount of leading free space */
#define uni_msg_leading(M)	((size_t)((M)->b_rptr - (M)->b_buf))

/* return the maximum size of the message (length plus free space) */
#define uni_msg_size(M)		((size_t)((M)->b_lim - (M)->b_buf));

/* ensure that there is space for another S bytes. If reallocation fails
 * free message and return -1 */
#define uni_msg_ensure(M, S)					\
	((uni_msg_space(M) >= (S)) ? 0 : uni_msg_extend(M, S))

int uni_msg_append(struct uni_msg *, void *, size_t);
int uni_msg_extend(struct uni_msg *, size_t);

#define uni_msg_rptr(MSG, TYPE) ((TYPE)(void *)(MSG)->b_rptr)
#define uni_msg_wptr(MSG, TYPE) ((TYPE)(void *)(MSG)->b_wptr)

int uni_msg_prepend(struct uni_msg *, size_t);

#ifndef _KERNEL

struct uni_msg *uni_msg_alloc(size_t);
struct uni_msg *uni_msg_build(void *, ...);
void uni_msg_destroy(struct uni_msg *);
u_int uni_msg_strip32(struct uni_msg *);
u_int uni_msg_get32(struct uni_msg *);
int uni_msg_append32(struct uni_msg *, u_int);
int uni_msg_append8(struct uni_msg *, u_int);
u_int uni_msg_trail32(const struct uni_msg *, int);
struct uni_msg *uni_msg_dup(const struct uni_msg *);

#endif /* _KERNEL */
#endif
