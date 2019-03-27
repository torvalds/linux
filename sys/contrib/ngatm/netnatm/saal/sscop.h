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
 * $Begemot: libunimsg/netnatm/saal/sscop.h,v 1.4 2004/07/08 08:22:16 brandt Exp $
 *
 * External interface to sscop.
 */
#ifndef _NETNATM_SAAL_SSCOP_H_
#define _NETNATM_SAAL_SSCOP_H_

#include <netnatm/saal/sscopdef.h>

/*
 * Define how a buffer looks like.
 */
#ifdef _KERNEL
#ifdef __FreeBSD__
#define SSCOP_MBUF_T mbuf
#endif
#else
#define SSCOP_MBUF_T uni_msg
#endif

struct SSCOP_MBUF_T;
struct sscop;

/*
 * Vector for user functions
 */
struct sscop_funcs {
	/* management signal from SSCOP */
	void	(*send_manage)(struct sscop *, void *, enum sscop_maasig,
		    struct SSCOP_MBUF_T *, u_int, u_int);

	/* AAL signal from SSCOP */
	void	(*send_upper)(struct sscop *, void *, enum sscop_aasig,
		    struct SSCOP_MBUF_T *, u_int);

	/* send a PDU to the wire */
	void	(*send_lower)(struct sscop *, void *,
		    struct SSCOP_MBUF_T *);

	/* print a message */
	void	(*verbose)(struct sscop *, void *, const char *, ...)
		    __printflike(3,4);

#ifndef _KERNEL
	/* start a timer */
	void	*(*start_timer)(struct sscop *, void *, u_int,
		    void (*)(void *));

	/* stop a timer */
	void	(*stop_timer)(struct sscop *, void *, void *);
#endif
};

/* Function defined by the SSCOP code */

/* create a new SSCOP instance and initialize to default values */
struct sscop *sscop_create(void *, const struct sscop_funcs *);

/* destroy an SSCOP instance */
void sscop_destroy(struct sscop *);

/* get the current parameters of an SSCOP */
void sscop_getparam(const struct sscop *, struct sscop_param *);

/* set new parameters in an SSCOP */
int sscop_setparam(struct sscop *, struct sscop_param *, u_int *);

/* deliver an signal to the SSCOP */
int sscop_aasig(struct sscop *, enum sscop_aasig, struct SSCOP_MBUF_T *, u_int);

/* deliver an management signal to the SSCOP */
int sscop_maasig(struct sscop *, enum sscop_maasig, struct SSCOP_MBUF_T *);

/* SSCOP input function */
void sscop_input(struct sscop *, struct SSCOP_MBUF_T *);

/* Move the window by a given number of messages. Return the new window */
u_int sscop_window(struct sscop *, u_int);

/* declare the lower layer busy or not busy */
u_int sscop_setbusy(struct sscop *, int);

/* retrieve the state */
enum sscop_state sscop_getstate(const struct sscop *);

/* map signals to strings */
const char *sscop_msigname(enum sscop_maasig);
const char *sscop_signame(enum sscop_aasig);
const char *sscop_statename(enum sscop_state);

/* set/get debugging state */
void sscop_setdebug(struct sscop *, u_int);
u_int sscop_getdebug(const struct sscop *);

/* reset the instance */
void sscop_reset(struct sscop *);

#endif
