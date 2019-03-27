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
 * $Begemot: libunimsg/netnatm/saal/sscfupriv.h,v 1.3 2003/09/19 12:02:03 hbb Exp $
 *
 * Private SSCF-UNI definitions.
 */
#ifdef _KERNEL
#ifdef __FreeBSD__
#include <netgraph/atm/sscfu/ng_sscfu_cust.h>
#endif
#else
#include "sscfucust.h"
#endif

/*
 * Structure for signal queueing.
 */
struct sscfu_sig {
	sscfu_sigq_link_t link;		/* link to next signal */
	enum saal_sig	sig;		/* the signal */
	struct SSCFU_MBUF_T *m;		/* associated message */
};

struct sscfu {
	enum sscfu_state	state;		/* SSCF state */
	const struct sscfu_funcs *funcs;	/* func vector */
	void			*aarg;		/* user arg */
	int			inhand;		/* need to queue signals */
	sscfu_sigq_head_t	sigs;		/* signal queue */
	u_int			debug;		/* debugging flags */
};

/*
 * Debugging
 */
#ifdef SSCFU_DEBUG
#define VERBOSE(S,M,F)	if ((S)->debug & (M)) (S)->funcs->verbose F
#else
#define VERBOSE(S,M,F)
#endif
