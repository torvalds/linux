/*
 * ng_h4_prse.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_h4_prse.h,v 1.4 2005/10/31 17:57:43 max Exp $
 * $FreeBSD$
 */

/***************************************************************************
 ***************************************************************************
 **                  ng_parse definitions for the H4 node
 ***************************************************************************
 ***************************************************************************/

#ifndef _NETGRAPH_H4_PRSE_H_
#define _NETGRAPH_H4_PRSE_H_

/* 
 * H4 node command list
 */

/* Stat info */
static const struct ng_parse_struct_field	ng_h4_stat_type_fields[] =
{
	{ "pckts_recv",	&ng_parse_uint32_type, },
	{ "bytes_recv",	&ng_parse_uint32_type, },
	{ "pckts_sent",	&ng_parse_uint32_type, },
	{ "bytes_sent",	&ng_parse_uint32_type, },
	{ "oerrors",	&ng_parse_uint32_type, },
	{ "ierrors",	&ng_parse_uint32_type, },
	{ NULL, }
};
static const struct ng_parse_type		ng_h4_stat_type = {
	&ng_parse_struct_type,
	&ng_h4_stat_type_fields
};

static const struct ng_cmdlist	ng_h4_cmdlist[] = {
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_RESET,
		"reset",
		NULL,
		NULL
	},
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_GET_STATE,
		"get_state",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_GET_DEBUG,
		"get_debug",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_SET_DEBUG,
		"set_debug",
		&ng_parse_uint16_type,
		NULL
	},
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_GET_QLEN,
		"get_qlen",
		NULL,
		&ng_parse_int32_type
	},
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_SET_QLEN,
		"set_qlen",
		&ng_parse_int32_type,
		NULL
	},
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_GET_STAT,
		"get_stat",
		NULL,
		&ng_h4_stat_type
	},
	{
		NGM_H4_COOKIE,
		NGM_H4_NODE_RESET_STAT,
		"reset_stat",
		NULL,
		NULL
	},
	{ 0, }
};

#endif /* ndef _NETGRAPH_H4_PRSE_H_ */

