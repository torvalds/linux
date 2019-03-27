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
 * Netgraph module for ITU-T Q.2120 UNI SSCF.
 */
#ifndef _NETGRAPH_ATM_NG_SSCFU_H_
#define	_NETGRAPH_ATM_NG_SSCFU_H_

#define NG_SSCFU_NODE_TYPE "sscfu"
#define NGM_SSCFU_COOKIE	980517963

/* Netgraph control messages */
enum {
	NGM_SSCFU_GETDEFPARAM = 1,	/* get default SSCOP parameters */
	NGM_SSCFU_ENABLE,		/* enable processing */
	NGM_SSCFU_DISABLE,		/* disable processing */
	NGM_SSCFU_GETDEBUG,		/* get debug flags */
	NGM_SSCFU_SETDEBUG,		/* set debug flags */
	NGM_SSCFU_GETSTATE,		/* get current state */
};

/* getdefparam return */
struct ng_sscfu_getdefparam {
	struct sscop_param	param;
	uint32_t		mask;
};
#define NG_SSCFU_GETDEFPARAM_INFO 				\
	{							\
	  { "param",		&ng_sscop_param_type },		\
	  { "mask",		&ng_parse_uint32_type },	\
	  { NULL }						\
	}

/*
 * Upper interface
 */
struct sscfu_arg {
	uint32_t	sig;
	u_char		data[];
};
#endif
