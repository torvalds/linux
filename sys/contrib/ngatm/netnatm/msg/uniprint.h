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
 * $Begemot: libunimsg/netnatm/msg/uniprint.h,v 1.4 2004/07/08 08:22:08 brandt Exp $
 *
 * Print utility functions. These are only needed if you want to hook to
 * the format of the uni printing routines.
 */
#ifndef _NETNATM_MSG_UNIPRINT_H_
#define _NETNATM_MSG_UNIPRINT_H_

#include <netnatm/msg/uni_config.h>

/*
 * This structure is used to define value->string mappings.
 * It must be terminated by a { NULL, 0 } entry.
 */
struct uni_print_tbl {
	const char *name;
	u_int val;
};
void uni_print_tbl(const char *_entry, u_int _val,
    const struct uni_print_tbl *_tbl, struct unicx *_cx);

/* initialize printing. This must be called at the start from each external
 * callable printing function. */
void uni_print_init(char *_buf, size_t _bufsiz, struct unicx *_cx);

/* End the current (semantical) line. This takes care of indendation and
 * actually print the newline in the appropriate modes. */
void uni_print_eol(struct unicx *_cx);

/* Push or pop a prefix. This takes care of indendation. */
void uni_print_push_prefix(const char *_prefix, struct unicx *_cx);
void uni_print_pop_prefix(struct unicx *_cx);

/* Print a flag taking care of the right prefixing */
void uni_print_flag(const char *_flag, struct unicx *_cx);

/* Print an entry taking care of the right prefixing */
void uni_print_entry(struct unicx *_cx, const char *_entry,
    const char *_fmt, ...) __printflike(3, 4);

/* Generic printf */
void uni_printf(struct unicx *_cx, const char *_fmt, ...) __printflike(2, 3);

#endif
