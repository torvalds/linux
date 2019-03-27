/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 *
 * Netgraph module for Q.2110 SSCOP
 */
#ifndef _NETGRAPH_ATM_NG_SSCOP_H_
#define _NETGRAPH_ATM_NG_SSCOP_H_

#define NG_SSCOP_NODE_TYPE "sscop"
#define NGM_SSCOP_COOKIE	980175044

/* Netgraph control messages */
enum {
	NGM_SSCOP_GETPARAM = 1,		/* get parameters */
	NGM_SSCOP_SETPARAM,		/* set parameters */
	NGM_SSCOP_ENABLE,		/* enable processing */
	NGM_SSCOP_DISABLE,		/* disable and reset */
	NGM_SSCOP_GETDEBUG,		/* get debugging flags */
	NGM_SSCOP_SETDEBUG,		/* set debugging flags */
	NGM_SSCOP_GETSTATE,		/* get current SSCOP state */
};

/* This must be in-sync with the definition in sscopdef.h */
#define NG_SSCOP_PARAM_INFO 					\
	{							\
	  { "timer_cc",		&ng_parse_uint32_type },	\
	  { "timer_poll",	&ng_parse_uint32_type },	\
	  { "timer_keep_alive",	&ng_parse_uint32_type },	\
	  { "timer_no_response",&ng_parse_uint32_type },	\
	  { "timer_idle",	&ng_parse_uint32_type },	\
	  { "maxk",		&ng_parse_uint32_type },	\
	  { "maxj",		&ng_parse_uint32_type },	\
	  { "maxcc",		&ng_parse_uint32_type },	\
	  { "maxpd",		&ng_parse_uint32_type },	\
	  { "maxstat",		&ng_parse_uint32_type },	\
	  { "mr",		&ng_parse_uint32_type },	\
	  { "flags",		&ng_parse_uint32_type },	\
	  { NULL }						\
	}


struct ng_sscop_setparam {
	uint32_t		mask;
	struct sscop_param	param;
};
#define NG_SSCOP_SETPARAM_INFO 					\
	{							\
	  { "mask",		&ng_parse_uint32_type },	\
	  { "param",		&ng_sscop_param_type },		\
	  { NULL }						\
	}

struct ng_sscop_setparam_resp {
	uint32_t		mask;
	int32_t			error;
};
#define NG_SSCOP_SETPARAM_RESP_INFO 				\
	{							\
	  { "mask",		&ng_parse_uint32_type },	\
	  { "error",		&ng_parse_int32_type },		\
	  { NULL }						\
	}

/*
 * Upper interface
 */
struct sscop_arg {
	uint32_t	sig;
	uint32_t	arg;	/* opt. sequence number or clear-buff */
	u_char		data[];
};

struct sscop_marg {
	uint32_t	sig;
	u_char		data[];
};
struct sscop_merr {
	uint32_t	sig;
	uint32_t	err;	/* error code */
	uint32_t	cnt;	/* error count */
};

#endif
