/*-
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY FRAUNHOFER FOKUS
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * FRAUNHOFER FOKUS OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * In-kernel UNI stack message functions.
 */
#ifndef _NETGRAPH_ATM_NGATMBASE_H_
#define	_NETGRAPH_ATM_NGATMBASE_H_

/* forward declarations */
struct mbuf;
struct uni_msg;

struct mbuf *uni_msg_pack_mbuf(struct uni_msg *, void *, size_t);

#ifdef NGATM_DEBUG

struct uni_msg *_uni_msg_alloc(size_t, const char *, int);
struct uni_msg *_uni_msg_build(const char *, int, void *, ...);
void _uni_msg_destroy(struct uni_msg *, const char *, int);
int _uni_msg_unpack_mbuf(struct mbuf *, struct uni_msg **, const char *, int);

#define	uni_msg_alloc(S) _uni_msg_alloc((S), __FILE__, __LINE__)
#define	uni_msg_build(P...) _uni_msg_build(__FILE__, __LINE__, P)
#define	uni_msg_destroy(M) _uni_msg_destroy((M), __FILE__, __LINE__)
#define	uni_msg_unpack_mbuf(M, PP) \
	    _uni_msg_unpack_mbuf((M), (PP), __FILE__, __LINE__)

#else /* !NGATM_DEBUG */

struct uni_msg *uni_msg_alloc(size_t);
struct uni_msg *uni_msg_build(void *, ...);
void uni_msg_destroy(struct uni_msg *);
int uni_msg_unpack_mbuf(struct mbuf *, struct uni_msg **);

#endif
#endif
