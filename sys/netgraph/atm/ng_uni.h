/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * Netgraph module for UNI 4.0
 */
#ifndef _NETGRAPH_ATM_NG_UNI_H_
#define _NETGRAPH_ATM_NG_UNI_H_

#define NG_UNI_NODE_TYPE "uni"
#define NGM_UNI_COOKIE	981112392

enum {
	NGM_UNI_GETDEBUG,	/* get debug flags */
	NGM_UNI_SETDEBUG,	/* set debug flags */
	NGM_UNI_GET_CONFIG,	/* get configuration */
	NGM_UNI_SET_CONFIG,	/* set configuration */
	NGM_UNI_ENABLE,		/* enable processing */
	NGM_UNI_DISABLE,	/* free resources and disable */
	NGM_UNI_GETSTATE,	/* retrieve coord state */
};

struct ngm_uni_debug {
	uint32_t	level[UNI_MAXFACILITY];
};
#define NGM_UNI_DEBUGLEVEL_INFO {				\
	&ng_parse_uint32_type,					\
	UNI_MAXFACILITY						\
}
#define NGM_UNI_DEBUG_INFO 					\
	{							\
	  { "level",	&ng_uni_debuglevel_type },		\
	  { NULL }						\
	}

#define NGM_UNI_CONFIG_INFO 					\
	{							\
	  { "proto",	&ng_parse_uint32_type },		\
	  { "popt",	&ng_parse_uint32_type },		\
	  { "option",	&ng_parse_uint32_type },		\
	  { "timer301",	&ng_parse_uint32_type },		\
	  { "timer303",	&ng_parse_uint32_type },		\
	  { "init303",	&ng_parse_uint32_type },		\
	  { "timer308",	&ng_parse_uint32_type },		\
	  { "init308",	&ng_parse_uint32_type },		\
	  { "timer309",	&ng_parse_uint32_type },		\
	  { "timer310",	&ng_parse_uint32_type },		\
	  { "timer313",	&ng_parse_uint32_type },		\
	  { "timer316",	&ng_parse_uint32_type },		\
	  { "init316",	&ng_parse_uint32_type },		\
	  { "timer317",	&ng_parse_uint32_type },		\
	  { "timer322",	&ng_parse_uint32_type },		\
	  { "init322",	&ng_parse_uint32_type },		\
	  { "timer397",	&ng_parse_uint32_type },		\
	  { "timer398",	&ng_parse_uint32_type },		\
	  { "timer399",	&ng_parse_uint32_type },		\
	  { NULL }						\
	}

struct ngm_uni_config_mask {
	uint32_t		mask;
	uint32_t		popt_mask;
	uint32_t		option_mask;
};
#define NGM_UNI_CONFIG_MASK_INFO 				\
	{							\
	  { "mask",		&ng_parse_hint32_type },	\
	  { "popt_mask",	&ng_parse_hint32_type },	\
	  { "option_mask",	&ng_parse_hint32_type },	\
	  { NULL }						\
	}

struct ngm_uni_set_config {
	struct uni_config		config;
	struct ngm_uni_config_mask	mask;
};
#define NGM_UNI_SET_CONFIG_INFO 				\
	{							\
	  { "config",		&ng_uni_config_type },		\
	  { "mask",		&ng_uni_config_mask_type },	\
	  { NULL }						\
	}

/*
 * API message
 */
struct uni_arg {
	uint32_t	sig;
	uint32_t	cookie;
	u_char		data[];
};

#endif
