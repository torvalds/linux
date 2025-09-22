/*	$OpenBSD: ccp.h,v 1.6 2002/09/13 00:12:10 deraadt Exp $	*/

/*
 * ccp.h - Definitions for PPP Compression Control Protocol.
 *
 * Copyright (c) 1989-2002 Paul Mackerras. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Paul Mackerras
 *     <paulus@samba.org>".
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

typedef struct ccp_options {
    u_int bsd_compress: 1;	/* do BSD Compress? */
    u_int deflate: 1;		/* do Deflate? */
    u_int predictor_1: 1;	/* do Predictor-1? */
    u_int predictor_2: 1;	/* do Predictor-2? */
    u_int deflate_correct: 1;	/* use correct code for deflate? */
    u_int deflate_draft: 1;	/* use draft RFC code for deflate? */
    u_short bsd_bits;		/* # bits/code for BSD Compress */
    u_short deflate_size;	/* lg(window size) for Deflate */
    short method;		/* code for chosen compression method */
} ccp_options;

extern fsm ccp_fsm[];
extern ccp_options ccp_wantoptions[];
extern ccp_options ccp_gotoptions[];
extern ccp_options ccp_allowoptions[];
extern ccp_options ccp_hisoptions[];

extern struct protent ccp_protent;
