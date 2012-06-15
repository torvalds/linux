/*
 * net/tipc/msg.c: TIPC message header routines
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2005, 2010-2011, Wind River Systems
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
#include "msg.h"

u32 tipc_msg_tot_importance(struct tipc_msg *m)
{
	if (likely(msg_isdata(m))) {
		if (likely(msg_orignode(m) == tipc_own_addr))
			return msg_importance(m);
		return msg_importance(m) + 4;
	}
	if ((msg_user(m) == MSG_FRAGMENTER)  &&
	    (msg_type(m) == FIRST_FRAGMENT))
		return msg_importance(msg_get_wrapped(m));
	return msg_importance(m);
}


void tipc_msg_init(struct tipc_msg *m, u32 user, u32 type,
			    u32 hsize, u32 destnode)
{
	memset(m, 0, hsize);
	msg_set_version(m);
	msg_set_user(m, user);
	msg_set_hdr_sz(m, hsize);
	msg_set_size(m, hsize);
	msg_set_prevnode(m, tipc_own_addr);
	msg_set_type(m, type);
	msg_set_orignode(m, tipc_own_addr);
	msg_set_destnode(m, destnode);
}

/**
 * tipc_msg_build - create message using specified header and data
 *
 * Note: Caller must not hold any locks in case copy_from_user() is interrupted!
 *
 * Returns message data size or errno
 */
int tipc_msg_build(struct tipc_msg *hdr, struct iovec const *msg_sect,
		   u32 num_sect, unsigned int total_len,
			    int max_size, int usrmem, struct sk_buff **buf)
{
	int dsz, sz, hsz, pos, res, cnt;

	dsz = total_len;
	pos = hsz = msg_hdr_sz(hdr);
	sz = hsz + dsz;
	msg_set_size(hdr, sz);
	if (unlikely(sz > max_size)) {
		*buf = NULL;
		return dsz;
	}

	*buf = tipc_buf_acquire(sz);
	if (!(*buf))
		return -ENOMEM;
	skb_copy_to_linear_data(*buf, hdr, hsz);
	for (res = 1, cnt = 0; res && (cnt < num_sect); cnt++) {
		if (likely(usrmem))
			res = !copy_from_user((*buf)->data + pos,
					      msg_sect[cnt].iov_base,
					      msg_sect[cnt].iov_len);
		else
			skb_copy_to_linear_data_offset(*buf, pos,
						       msg_sect[cnt].iov_base,
						       msg_sect[cnt].iov_len);
		pos += msg_sect[cnt].iov_len;
	}
	if (likely(res))
		return dsz;

	kfree_skb(*buf);
	*buf = NULL;
	return -EFAULT;
}

#ifdef CONFIG_TIPC_DEBUG
void tipc_msg_dbg(struct print_buf *buf, struct tipc_msg *msg, const char *str)
{
	u32 usr = msg_user(msg);
	tipc_printf(buf, KERN_DEBUG);
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
			tipc_printf(buf, "UNKNOWN:%x", msg_type(msg));

		}
		tipc_printf(buf, "NO(%u/%u):", msg_long_msgno(msg),
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
			tipc_printf(buf, "UNKNOWN TYPE %u", msg_type(msg));
		}
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
			tipc_printf(buf, "UNKNOWN:%x", msg_type(msg));
		}
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
			tipc_printf(buf, "ACK(%u):", msg_msgcnt(msg));
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x", msg_type(msg));
		}
		if (msg_reroute_cnt(msg))
			tipc_printf(buf, "REROUTED(%u):", msg_reroute_cnt(msg));
		break;
	case LINK_PROTOCOL:
		switch (msg_type(msg)) {
		case STATE_MSG:
			tipc_printf(buf, "STATE:");
			tipc_printf(buf, "%s:", msg_probe(msg) ? "PRB" : "");
			tipc_printf(buf, "NXS(%u):", msg_next_sent(msg));
			tipc_printf(buf, "GAP(%u):", msg_seq_gap(msg));
			tipc_printf(buf, "LSTBC(%u):", msg_last_bcast(msg));
			break;
		case RESET_MSG:
			tipc_printf(buf, "RESET:");
			if (msg_size(msg) != msg_hdr_sz(msg))
				tipc_printf(buf, "BEAR:%s:", msg_data(msg));
			break;
		case ACTIVATE_MSG:
			tipc_printf(buf, "ACTIVATE:");
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x", msg_type(msg));
		}
		tipc_printf(buf, "PLANE(%c):", msg_net_plane(msg));
		tipc_printf(buf, "SESS(%u):", msg_session(msg));
		break;
	case CHANGEOVER_PROTOCOL:
		tipc_printf(buf, "TUNL:");
		switch (msg_type(msg)) {
		case DUPLICATE_MSG:
			tipc_printf(buf, "DUPL:");
			break;
		case ORIGINAL_MSG:
			tipc_printf(buf, "ORIG:");
			tipc_printf(buf, "EXP(%u)", msg_msgcnt(msg));
			break;
		default:
			tipc_printf(buf, "UNKNOWN TYPE:%x", msg_type(msg));
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
			tipc_printf(buf, "UNKNOWN TYPE:%x:", msg_type(msg));
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
	default:
		break;
	}

	tipc_printf(buf, "HZ(%u):", msg_hdr_sz(msg));
	tipc_printf(buf, "SZ(%u):", msg_size(msg));
	tipc_printf(buf, "SQNO(%u):", msg_seqno(msg));

	if (msg_non_seq(msg))
		tipc_printf(buf, "NOSEQ:");
	else
		tipc_printf(buf, "ACK(%u):", msg_ack(msg));
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
	}
	if (msg_user(msg) == NAME_DISTRIBUTOR) {
		tipc_printf(buf, ":ONOD(%x):", msg_orignode(msg));
		tipc_printf(buf, ":DNOD(%x):", msg_destnode(msg));
	}

	if (msg_user(msg) ==  LINK_CONFIG) {
		struct tipc_media_addr orig;

		tipc_printf(buf, ":DDOM(%x):", msg_dest_domain(msg));
		tipc_printf(buf, ":NETID(%u):", msg_bc_netid(msg));
		memcpy(orig.value, msg_media_addr(msg), sizeof(orig.value));
		orig.media_id = 0;
		orig.broadcast = 0;
		tipc_media_addr_printf(buf, &orig);
	}
	if (msg_user(msg) == BCAST_PROTOCOL) {
		tipc_printf(buf, "BCNACK:AFTER(%u):", msg_bcgap_after(msg));
		tipc_printf(buf, "TO(%u):", msg_bcgap_to(msg));
	}
	tipc_printf(buf, "\n");
	if ((usr == CHANGEOVER_PROTOCOL) && (msg_msgcnt(msg)))
		tipc_msg_dbg(buf, msg_get_wrapped(msg), "      /");
	if ((usr == MSG_FRAGMENTER) && (msg_type(msg) == FIRST_FRAGMENT))
		tipc_msg_dbg(buf, msg_get_wrapped(msg), "      /");
}
#endif
