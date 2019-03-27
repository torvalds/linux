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
 * $Begemot: libunimsg/netnatm/sig/uni.h,v 1.5 2004/07/08 08:22:24 brandt Exp $
 *
 * Public UNI interface
 */
#ifndef _NETNATM_SIG_UNI_H_
#define _NETNATM_SIG_UNI_H_

#include <netnatm/sig/unidef.h>

struct uni;

/* functions to be supplied by the user */
struct uni_funcs {
	/* output to the upper layer */
	void	(*uni_output)(struct uni *, void *, enum uni_sig,
		    uint32_t, struct uni_msg *);

	/* output to the SAAL */
	void	(*saal_output)(struct uni *, void *, enum saal_sig,
		    struct uni_msg *);

	/* verbosity */
	void	(*verbose)(struct uni *, void *, enum uni_verb,
		    const char *, ...) __printflike(4, 5);

	/* function to 'print' status */
	void	(*status)(struct uni *, void *, void *,
		    const char *, ...) __printflike(4, 5);

#ifndef _KERNEL
	/* start a timer */
	void	*(*start_timer)(struct uni *, void *, u_int,
		    void (*)(void *), void *);

	/* stop a timer */
	void	(*stop_timer)(struct uni *, void *, void *);
#endif
};

/* create a UNI instance */
struct uni *uni_create(void *, const struct uni_funcs *);

/* destroy a UNI instance, free all resources */
void uni_destroy(struct uni *);

/* generate a status report */
void uni_status(struct uni *, void *);

/* get current instance configuration */
void uni_get_config(const struct uni *, struct uni_config *);

/* set new instance configuration */
void uni_set_config(struct uni *, const struct uni_config *,
	uint32_t *, uint32_t *, uint32_t *);

/* input from the SAAL to the instance */
void  uni_saal_input(struct uni *, enum saal_sig, struct uni_msg *);

/* input from the upper layer to the instance */
void uni_uni_input(struct uni *, enum uni_sig, uint32_t, struct uni_msg *);

/* do work on pending signals */
void uni_work(struct uni *);

/* set debuging level */
void uni_set_debug(struct uni *, enum uni_verb, u_int level);
u_int uni_get_debug(const struct uni *, enum uni_verb);

/* reset a UNI instance */
void uni_reset(struct uni *);

/* states */
u_int uni_getcustate(const struct uni *);

/* return a reference to the coding/decoding context */
struct unicx *uni_context(struct uni *);

#endif
