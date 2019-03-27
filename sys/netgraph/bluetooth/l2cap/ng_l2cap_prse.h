/*
 * ng_l2cap_prse.h
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
 * $Id: ng_l2cap_prse.h,v 1.2 2003/04/28 21:44:59 max Exp $
 * $FreeBSD$
 */

/***************************************************************************
 ***************************************************************************
 **                ng_parse definitions for the L2CAP node
 ***************************************************************************
 ***************************************************************************/

#ifndef _NETGRAPH_L2CAP_PRSE_H_
#define _NETGRAPH_L2CAP_PRSE_H_

/* 
 * L2CAP node command list
 */

static const struct ng_cmdlist	ng_l2cap_cmdlist[] = {
	{
		NGM_L2CAP_COOKIE,
		NGM_L2CAP_NODE_GET_FLAGS,
		"get_flags",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_L2CAP_COOKIE,
		NGM_L2CAP_NODE_GET_DEBUG,
		"get_debug",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_L2CAP_COOKIE,
		NGM_L2CAP_NODE_SET_DEBUG,
		"set_debug",
		&ng_parse_uint16_type,
		NULL
	},
	{
		NGM_L2CAP_COOKIE,
		NGM_L2CAP_NODE_GET_AUTO_DISCON_TIMO,
		"get_disc_timo",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_L2CAP_COOKIE,
		NGM_L2CAP_NODE_SET_AUTO_DISCON_TIMO,
		"set_disc_timo",
		&ng_parse_uint16_type,
		NULL
	},
	{ 0, }
};

#endif /* ndef _NETGRAPH_L2CAP_PRSE_H_ */

