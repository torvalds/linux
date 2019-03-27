/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/msg/priv.h,v 1.4 2003/10/10 14:50:05 hbb Exp $
 *
 * Private definitions for the IE code file.
 */
#ifndef unimsg_priv_h
#define unimsg_priv_h

#ifdef _KERNEL
#include <sys/systm.h>
#include <machine/stdarg.h>
#define PANIC(X) panic X
#else
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#define PANIC(X) abort()
#endif

/*
 * Define a structure for the declaration of information elements.
 * For each coding scheme a quadrupel of check, print, encode and
 * decode functions must be defined. A structure of the same format
 * is used for messages.
 */
typedef void (*uni_print_f)(const union uni_ieall *, struct unicx *);
typedef int (*uni_check_f)(union uni_ieall *, struct unicx *);
typedef int (*uni_encode_f)(struct uni_msg *, union uni_ieall *,
    struct unicx *);
typedef int (*uni_decode_f)(union uni_ieall *, struct uni_msg *, u_int,
    struct unicx *);

typedef void (*uni_msg_print_f)(const union uni_msgall *, struct unicx *);
typedef int (*uni_msg_check_f)(struct uni_all *, struct unicx *);
typedef int (*uni_msg_encode_f)(struct uni_msg *, union uni_msgall *,
    struct unicx *);
typedef int (*uni_msg_decode_f)(union uni_msgall *, struct uni_msg *,
    enum uni_ietype, struct uni_iehdr *, u_int, struct unicx *);

struct iedecl {
	u_int		flags;		/* information element flags */
	u_int		maxlen;		/* maximum size */
	uni_print_f	print;
	uni_check_f	check;
	uni_encode_f	encode;
	uni_decode_f	decode;
};

struct msgdecl {
	u_int		flags;
	const char	*name;
	uni_msg_print_f	print;
	uni_msg_check_f	check;
	uni_msg_encode_f encode;
	uni_msg_decode_f decode;
};

enum {
	UNIFL_DEFAULT	= 0x0001,
	UNIFL_ACCESS	= 0x0002,
};

extern const struct iedecl *uni_ietable[256][4];
extern const struct msgdecl *uni_msgtable[256];

/*
 * Need to check range here because declaring a variable as a enum does not
 * guarantee that the values will be legal.
 */
#define GET_IEDECL(IE, CODING) 						\
({									\
	const struct iedecl *_decl = NULL;				\
									\
	if((CODING) <= 3 && (IE) <= 255)				\
	    if((_decl = uni_ietable[IE][CODING]) != NULL)		\
		if((_decl->flags & UNIFL_DEFAULT) != 0)			\
		    if((_decl = uni_ietable[IE][0]) == NULL)		\
			PANIC(("IE %02x,%02x -- no default", CODING,IE));\
	_decl;								\
})


enum {
	DEC_OK,
	DEC_ILL,
	DEC_ERR,
};

void uni_print_ie_internal(enum uni_ietype, const union uni_ieall *,
    struct unicx *);

#endif
