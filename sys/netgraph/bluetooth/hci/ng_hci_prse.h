/*
 * ng_hci_prse.h
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
 * $Id: ng_hci_prse.h,v 1.2 2003/03/18 00:09:36 max Exp $
 * $FreeBSD$
 */

/***************************************************************************
 ***************************************************************************
 **                  ng_parse definitions for the HCI node
 ***************************************************************************
 ***************************************************************************/

#ifndef _NETGRAPH_HCI_PRSE_H_
#define _NETGRAPH_HCI_PRSE_H_

/* BDADDR */
static const struct ng_parse_fixedarray_info	ng_hci_bdaddr_type_info = {
	&ng_parse_uint8_type,
	NG_HCI_BDADDR_SIZE
};
static const struct ng_parse_type		ng_hci_bdaddr_type = {
	&ng_parse_fixedarray_type,
	&ng_hci_bdaddr_type_info
};

/* Features */
static const struct ng_parse_fixedarray_info	ng_hci_features_type_info = {
	&ng_parse_uint8_type,
	NG_HCI_FEATURES_SIZE
};
static const struct ng_parse_type		ng_hci_features_type = {
	&ng_parse_fixedarray_type,
	&ng_hci_features_type_info
};

/* Buffer info */
static const struct ng_parse_struct_field	ng_hci_buffer_type_fields[] =
{
	{ "cmd_free",	&ng_parse_uint8_type,  },
	{ "sco_size",	&ng_parse_uint8_type,  },
	{ "sco_pkts",	&ng_parse_uint16_type, },
	{ "sco_free",	&ng_parse_uint16_type, },
	{ "acl_size",	&ng_parse_uint16_type, },
	{ "acl_pkts",	&ng_parse_uint16_type, },
	{ "acl_free",	&ng_parse_uint16_type, },
	{ NULL, }
};
static const struct ng_parse_type		ng_hci_buffer_type = {
	&ng_parse_struct_type,
	&ng_hci_buffer_type_fields
};

/* Stat info */
static const struct ng_parse_struct_field	ng_hci_stat_type_fields[] =
{
	{ "cmd_sent",	&ng_parse_uint32_type, },
	{ "evnt_recv",	&ng_parse_uint32_type, },
	{ "acl_recv",	&ng_parse_uint32_type, },
	{ "acl_sent",	&ng_parse_uint32_type, },
	{ "sco_recv",	&ng_parse_uint32_type, },
	{ "sco_sent",	&ng_parse_uint32_type, },
	{ "bytes_recv",	&ng_parse_uint32_type, },
	{ "bytes_sent",	&ng_parse_uint32_type, },
	{ NULL, }
};
static const struct ng_parse_type		ng_hci_stat_type = {
	&ng_parse_struct_type,
	&ng_hci_stat_type_fields
};

/* 
 * HCI node command list
 */

static const struct ng_cmdlist	ng_hci_cmdlist[] = {
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_STATE,
		"get_state",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_INIT,
		"init",
		NULL,
		NULL
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_DEBUG,
		"get_debug",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_SET_DEBUG,
		"set_debug",
		&ng_parse_uint16_type,
		NULL
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_BUFFER,
		"get_buff_info",
		NULL,
		&ng_hci_buffer_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_BDADDR,
		"get_bdaddr",
		NULL,
		&ng_hci_bdaddr_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_FEATURES,
		"get_features",
		NULL,
		&ng_hci_features_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_STAT,
		"get_stat",
		NULL,
		&ng_hci_stat_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_RESET_STAT,
		"reset_stat",
		NULL,
		NULL
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_FLUSH_NEIGHBOR_CACHE,
		"flush_ncache",
		NULL,
		NULL
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_LINK_POLICY_SETTINGS_MASK,
		"get_lm_mask",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_SET_LINK_POLICY_SETTINGS_MASK,
		"set_lm_mask",
		&ng_parse_uint16_type,
		NULL
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_PACKET_MASK,
		"get_pkt_mask",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_SET_PACKET_MASK,
		"set_pkt_mask",
		&ng_parse_uint16_type,
		NULL
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_GET_ROLE_SWITCH,
		"get_role_sw",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_HCI_COOKIE,
		NGM_HCI_NODE_SET_ROLE_SWITCH,
		"set_role_sw",
		&ng_parse_uint16_type,
		NULL
	},
	{ 0, }
};

#endif /* ndef _NETGRAPH_HCI_PRSE_H_ */

