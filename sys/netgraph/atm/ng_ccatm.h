/*-
 * Copyright (c) 2001-2002
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
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
 */

/*
 * Interface to ng_ccatm
 */
#ifndef _NETGRAPH_ATM_NG_CCATM_H_
#define _NETGRAPH_ATM_NG_CCATM_H_

#define NG_CCATM_NODE_TYPE	"ccatm"
#define NGM_CCATM_COOKIE	984046139

enum {
	NGM_CCATM_DUMP,			/* dump internal status */
	NGM_CCATM_STOP,			/* stop all processing, close all */
	NGM_CCATM_START,		/* start processing */
	NGM_CCATM_CLEAR,		/* clear prefix/address table */
	NGM_CCATM_GET_ADDRESSES,	/* get list of all addresses */
	NGM_CCATM_ADDRESS_REGISTERED,	/* registration ok */
	NGM_CCATM_ADDRESS_UNREGISTERED,	/* unregistration ok */
	NGM_CCATM_SET_PORT_PARAM,	/* set port parameters */
	NGM_CCATM_GET_PORT_PARAM,	/* get port parameters */
	NGM_CCATM_GET_PORTLIST,		/* get list of port numbers */
	NGM_CCATM_GETSTATE,		/* get port status */
	NGM_CCATM_SETLOG,		/* set/get loglevel */
	NGM_CCATM_RESET,		/* reset everything */
	NGM_CCATM_GET_EXSTAT,		/* get extended status */
};

/*
 * This must be synchronized with unistruct.h::struct uni_addr
 */
#define	NGM_CCATM_ADDR_ARRAY_INFO				\
	{							\
	  &ng_parse_hint8_type,					\
	  UNI_ADDR_MAXLEN					\
	}

#define NGM_CCATM_UNI_ADDR_INFO 				\
 	{							\
	  { "type",	&ng_parse_uint32_type },		\
	  { "plan",	&ng_parse_uint32_type },		\
	  { "len",	&ng_parse_uint32_type },		\
	  { "addr",	&ng_ccatm_addr_array_type },		\
	  { NULL }						\
	}

/*
 * Address request
 */
struct ngm_ccatm_addr_req {
	uint32_t	port;
	struct uni_addr	addr;
};
#define	NGM_CCATM_ADDR_REQ_INFO					\
	{							\
	  { "port",	&ng_parse_uint32_type },		\
	  { "addr",	&ng_ccatm_uni_addr_type },		\
	  { NULL },						\
	}

/*
 * Get current address list
 */
struct ngm_ccatm_get_addresses {
	uint32_t	count;
	struct ngm_ccatm_addr_req addr[];
};
#define	NGM_CCATM_ADDR_REQ_ARRAY_INFO				\
	{							\
	  &ng_ccatm_addr_req_type,				\
	  ng_ccatm_addr_req_array_getlen			\
	}
#define NGM_CCATM_GET_ADDRESSES_INFO 				\
	{							\
	  { "count",	&ng_parse_uint32_type },		\
	  { "addr",	&ng_ccatm_addr_req_array_type },	\
	  { NULL }						\
	}

/*
 * Port as parameter
 */
struct ngm_ccatm_port {
	uint32_t	port;
};
#define NGM_CCATM_PORT_INFO 					\
	{							\
	  { "port",	&ng_parse_uint32_type },		\
	  { NULL }						\
	}

/*
 * Port parameters.
 * This must be synchronized with atmapi.h::struct atm_port_info.
 */
#define	NGM_CCATM_ESI_INFO						\
	{								\
	  &ng_parse_hint8_type,						\
	  6								\
	}
#define NGM_CCATM_ATM_PORT_INFO 					\
	{								\
	  { "port",		&ng_parse_uint32_type },		\
	  { "pcr",		&ng_parse_uint32_type },		\
	  { "max_vpi_bits",	&ng_parse_uint32_type },		\
	  { "max_vci_bits",	&ng_parse_uint32_type },		\
	  { "max_svpc_vpi",	&ng_parse_uint32_type },		\
	  { "max_svcc_vpi",	&ng_parse_uint32_type },		\
	  { "min_svcc_vci",	&ng_parse_uint32_type },		\
	  { "esi",		&ng_ccatm_esi_type },			\
	  { "num_addr",		&ng_parse_uint32_type },		\
	  { NULL }							\
	}

/*
 * List of port numbers
 */
struct ngm_ccatm_portlist {
	uint32_t	nports;
	uint32_t	ports[];
};
#define	NGM_CCATM_PORT_ARRAY_INFO					\
	{								\
	  &ng_parse_uint32_type,					\
	  ng_ccatm_port_array_getlen					\
	}
#define NGM_CCATM_PORTLIST_INFO 					\
	{								\
	  { "nports",	&ng_parse_uint32_type },			\
	  { "ports",	&ng_ccatm_port_array_type },			\
	  { NULL }							\
	}

struct ccatm_op {
	uint32_t	op;	/* request code */
};

#endif
