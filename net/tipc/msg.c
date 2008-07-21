/*
 * net/tipc/msg.c: TIPC message header routines
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "addr.h"
#include "dbg.h"
#include "msg.h"
#include "bearer.h"


#ifdef CONFIG_TIPC_DEBUG

void tipc_msg_dbg(struct print_buf *buf, struct tipc_msg *msg, const char *str)
{
	u32 usr = msg_user(msg);
	tipc_printf(buf, str);

	switch (usr) {
	case MSG_BUNDLER:
		tipc_printf(buf, "BNDL::");
		tipc_printf(buf, "MSGS(%u):", msg_msgcnt(msg));
		break;
	case BCAST_PROTOCOL:
		tipc_printf(buf, "BCASTP::");
		break;
	case MSG_FRAGMENTER:
		tipc_printf(buf, "FRAGM::");
		switch (msg_type(msg)) {
		case FIRST_FRAGMENT:
			tipc_printf(buf, "FIRST:");
			break;
		case FRAGMENT:
			tipc_printf(buf, "BODY:");
			break;
		case LAST_FRAGMENT:
			tipc_printf(buf, "LAST:");
			break;
		default:
			tipc_printf(buf, "UNKNOWN:%x",msg_type(msg));

		}
		tipc_printf(buf, "NO(%u/%u):",msg_long_msgno(msg),
			    msg_fragm_no(msg));
		break;
	case TIPC_LOW_IMPORTANCE:
	case TIPC_MEDIUM_IMPORTANCE:
	case TIPC_HIGH_IMPORTANCE:
	case TIPC_CRITICAL_IMPORTANCE:
		tipc_printf(buf, "DAT%u:", msg_user(msg));
		if (msg_short(msg)) {
			tipc_printf(buf, "CON:");
			break;
		}
		switch (msg_type(msg)) {
		case TIPC_CONN_MSG:
			tipc_printf(buf, "CON:");
			break;
		case TIPC_MCAST_MSG:
			tipc_printf(buf, "MCST:");
			break;
		case TIPC_NAMED_MSG:
			tipc_printf(buf, "NAM:");
			break;
		case TIPC_DIRECT_MSG:
			tipc_printf(buf, "DIR:");
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE %u",msg_type(msg));
		}
		if (msg_routed(msg) && !msg_non_seq(msg))
			tipc_printf(buf, "ROUT:");
		if (msg_reroute_cnt(msg))
			tipc_printf(buf, "REROUTED(%u):",
				    msg_reroute_cnt(msg));
		break;
	case NAME_DISTRIBUTOR:
		tipc_printf(buf, "NMD::");
		switch (msg_type(msg)) {
		case PUBLICATION:
			tipc_printf(buf, "PUBL(%u):", (msg_size(msg) - msg_hdr_sz(msg)) / 20);	/* Items */
			break;
		case WITHDRAWAL:
			tipc_printf(buf, "WDRW:");
			break;
		default:
			tipc_printf(buf, "UNKNOWN:%x",msg_type(msg));
		}
		if (msg_routed(msg))
			tipc_printf(buf, "ROUT:");
		if (msg_reroute_cnt(msg))
			tipc_printf(buf, "REROUTED(%u):",
				    msg_reroute_cnt(msg));
		break;
	case CONN_MANAGER:
		tipc_printf(buf, "CONN_MNG:");
		switch (msg_type(msg)) {
		case CONN_PROBE:
			tipc_printf(buf, "PROBE:");
			break;
		case CONN_PROBE_REPLY:
			tipc_printf(buf, "PROBE_REPLY:");
			break;
		case CONN_ACK:
			tipc_printf(buf, "CONN_ACK:");
			tipc_printf(buf, "ACK(%u):",msg_msgcnt(msg));
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		if (msg_routed(msg))
			tipc_printf(buf, "ROUT:");
		if (msg_reroute_cnt(msg))
			tipc_printf(buf, "REROUTED(%u):",msg_reroute_cnt(msg));
		break;
	case LINK_PROTOCOL:
		tipc_printf(buf, "PROT:TIM(%u):",msg_timestamp(msg));
		switch (msg_type(msg)) {
		case STATE_MSG:
			tipc_printf(buf, "STATE:");
			tipc_printf(buf, "%s:",msg_probe(msg) ? "PRB" :"");
			tipc_printf(buf, "NXS(%u):",msg_next_sent(msg));
			tipc_printf(buf, "GAP(%u):",msg_seq_gap(msg));
			tipc_printf(buf, "LSTBC(%u):",msg_last_bcast(msg));
			break;
		case RESET_MSG:
			tipc_printf(buf, "RESET:");
			if (msg_size(msg) != msg_hdr_sz(msg))
				tipc_printf(buf, "BEAR:%s:",msg_data(msg));
			break;
		case ACTIVATE_MSG:
			tipc_printf(buf, "ACTIVATE:");
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		tipc_printf(buf, "PLANE(%c):",msg_net_plane(msg));
		tipc_printf(buf, "SESS(%u):",msg_session(msg));
		break;
	case CHANGEOVER_PROTOCOL:
		tipc_printf(buf, "TUNL:");
		switch (msg_type(msg)) {
		case DUPLICATE_MSG:
			tipc_printf(buf, "DUPL:");
			break;
		case ORIGINAL_MSG:
			tipc_printf(buf, "ORIG:");
			tipc_printf(buf, "EXP(%u)",msg_msgcnt(msg));
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		break;
	case ROUTE_DISTRIBUTOR:
		tipc_printf(buf, "ROUTING_MNG:");
		switch (msg_type(msg)) {
		case EXT_ROUTING_TABLE:
			tipc_printf(buf, "EXT_TBL:");
			tipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case LOCAL_ROUTING_TABLE:
			tipc_printf(buf, "LOCAL_TBL:");
			tipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case SLAVE_ROUTING_TABLE:
			tipc_printf(buf, "DP_TBL:");
			tipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case ROUTE_ADDITION:
			tipc_printf(buf, "ADD:");
			tipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case ROUTE_REMOVAL:
			tipc_printf(buf, "REMOVE:");
			tipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		break;
	case LINK_CONFIG:
		tipc_printf(buf, "CFG:");
		switch (msg_type(msg)) {
		case DSC_REQ_MSG:
			tipc_printf(buf, "DSC_REQ:");
			break;
		case DSC_RESP_MSG:
			tipc_printf(buf, "DSC_RESP:");
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x:",msg_type(msg));
			break;
		}
		break;
	default:
		tipc_printf(buf, "UNKNOWN USER:");
	}

	switch (usr) {
	case CONN_MANAGER:
	case TIPC_LOW_IMPORTANCE:
	case TIPC_MEDIUM_IMPORTANCE:
	case TIPC_HIGH_IMPORTANCE:
	case TIPC_CRITICAL_IMPORTANCE:
		switch (msg_errcode(msg)) {
		case TIPC_OK:
			break;
		case TIPC_ERR_NO_NAME:
			tipc_printf(buf, "NO_NAME:");
			break;
		case TIPC_ERR_NO_PORT:
			tipc_printf(buf, "NO_PORT:");
			break;
		case TIPC_ERR_NO_NODE:
			tipc_printf(buf, "NO_PROC:");
			break;
		case TIPC_ERR_OVERLOAD:
			tipc_printf(buf, "OVERLOAD:");
			break;
		case TIPC_CONN_SHUTDOWN:
			tipc_printf(buf, "SHUTDOWN:");
			break;
		default:
			tipc_printf(buf, "UNKNOWN ERROR(%x):",
				    msg_errcode(msg));
		}
	default:{}
	}

	tipc_printf(buf, "HZ(%u):", msg_hdr_sz(msg));
	tipc_printf(buf, "SZ(%u):", msg_size(msg));
	tipc_printf(buf, "SQNO(%u):", msg_seqno(msg));

	if (msg_non_seq(msg))
		tipc_printf(buf, "NOSEQ:");
	else {
		tipc_printf(buf, "ACK(%u):", msg_ack(msg));
	}
	tipc_printf(buf, "BACK(%u):", msg_bcast_ack(msg));
	tipc_printf(buf, "PRND(%x)", msg_prevnode(msg));

	if (msg_isdata(msg)) {
		if (msg_named(msg)) {
			tipc_printf(buf, "NTYP(%u):", msg_nametype(msg));
			tipc_printf(buf, "NINST(%u)", msg_nameinst(msg));
		}
	}

	if ((usr != LINK_PROTOCOL) && (usr != LINK_CONFIG) &&
	    (usr != MSG_BUNDLER)) {
		if (!msg_short(msg)) {
			tipc_printf(buf, ":ORIG(%x:%u):",
				    msg_orignode(msg), msg_origport(msg));
			tipc_printf(buf, ":DEST(%x:%u):",
				    msg_destnode(msg), msg_destport(msg));
		} else {
			tipc_printf(buf, ":OPRT(%u):", msg_origport(msg));
			tipc_printf(buf, ":DPRT(%u):", msg_destport(msg));
		}
		if (msg_routed(msg) && !msg_non_seq(msg))
			tipc_printf(buf, ":TSEQN(%u)", msg_transp_seqno(msg));
	}
	if (msg_user(msg) == NAME_DISTRIBUTOR) {
		tipc_printf(buf, ":ONOD(%x):", msg_orignode(msg));
		tipc_printf(buf, ":DNOD(%x):", msg_destnode(msg));
		if (msg_routed(msg)) {
			tipc_printf(buf, ":CSEQN(%u)", msg_transp_seqno(msg));
		}
	}

	if (msg_user(msg) ==  LINK_CONFIG) {
		u32* raw = (u32*)msg;
		struct tipc_media_addr* orig = (struct tipc_media_addr*)&raw[5];
		tipc_printf(buf, ":REQL(%u):", msg_req_links(msg));
		tipc_printf(buf, ":DDOM(%x):", msg_dest_domain(msg));
		tipc_printf(buf, ":NETID(%u):", msg_bc_netid(msg));
		tipc_media_addr_printf(buf, orig);
	}
	if (msg_user(msg) == BCAST_PROTOCOL) {
		tipc_printf(buf, "BCNACK:AFTER(%u):", msg_bcgap_after(msg));
		tipc_printf(buf, "TO(%u):", msg_bcgap_to(msg));
	}
	tipc_printf(buf, "\n");
	if ((usr == CHANGEOVER_PROTOCOL) && (msg_msgcnt(msg))) {
		tipc_msg_dbg(buf, msg_get_wrapped(msg), "      /");
	}
	if ((usr == MSG_FRAGMENTER) && (msg_type(msg) == FIRST_FRAGMENT)) {
		tipc_msg_dbg(buf, msg_get_wrapped(msg), "      /");
	}
}

#endif
