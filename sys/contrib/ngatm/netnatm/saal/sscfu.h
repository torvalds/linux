/*
 * Copyright (c) 1996-2003
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
 * $Begemot: libunimsg/netnatm/saal/sscfu.h,v 1.4 2004/07/08 08:22:15 brandt Exp $
 *
 * Public include file for UNI SSCF
 */
#ifndef _NETNATM_SAAL_SSCFU_H_
#define _NETNATM_SAAL_SSCFU_H_

#include <sys/types.h>
#include <netnatm/saal/sscopdef.h>
#include <netnatm/saal/sscfudef.h>

/*
 * Define how a buffer looks like.
 */
#ifdef _KERNEL
#ifdef __FreeBSD__
#define SSCFU_MBUF_T mbuf
#endif
#else
#define SSCFU_MBUF_T uni_msg
#endif

struct SSCFU_MBUF_T;
struct sscfu;

/* functions to be supplied by the SSCOP user */
struct sscfu_funcs {
	/* upper (SAAL) interface output */
	void	(*send_upper)(struct sscfu *, void *, enum saal_sig,
		    struct SSCFU_MBUF_T *);

	/* lower (SSCOP) interface output */
	void	(*send_lower)(struct sscfu *, void *, enum sscop_aasig,
		    struct SSCFU_MBUF_T *, u_int);

	/* function to move the SSCOP window */
	void	(*window)(struct sscfu *, void *, u_int);

	/* debugging function */
	void	(*verbose)(struct sscfu *, void *, const char *, ...)
		    __printflike(3, 4);
};

/* Function defined by the SSCF-UNI code */

/* allocate and initialize a new SSCF instance */
struct sscfu *sscfu_create(void *, const struct sscfu_funcs *);

/* destroy an SSCF instance and free all resources */
void sscfu_destroy(struct sscfu *);

/* reset the SSCF to the released state */
void sscfu_reset(struct sscfu *);

/* lower input interface (SSCOP signals) */
void sscfu_input(struct sscfu *, enum sscop_aasig, struct SSCFU_MBUF_T *, u_int);

/* upper input interface (SAAL) */
int sscfu_saalsig(struct sscfu *, enum saal_sig, struct SSCFU_MBUF_T *);

/* retrieve the current state */
enum sscfu_state sscfu_getstate(const struct sscfu *);

/* char'ify signals and states */
const char *sscfu_signame(enum saal_sig);
const char *sscfu_statename(enum sscfu_state);

/* retrieve the default set of parameters for SSCOP */
u_int sscfu_getdefparam(struct sscop_param *);

/* get/set debugging flags */
void sscfu_setdebug(struct sscfu *, u_int);
u_int sscfu_getdebug(const struct sscfu *);

#endif
