/*
 * net/tipc/config.c: TIPC configuration management code
 * 
 * Copyright (c) 2002-2006, Ericsson AB
 * Copyright (c) 2004-2006, Wind River Systems
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
#include "dbg.h"
#include "bearer.h"
#include "port.h"
#include "link.h"
#include "zone.h"
#include "addr.h"
#include "name_table.h"
#include "node.h"
#include "config.h"
#include "discover.h"

struct subscr_data {
	char usr_handle[8];
	u32 domain;
	u32 port_ref;
	struct list_head subd_list;
};

struct manager {
	u32 user_ref;
	u32 port_ref;
	u32 subscr_ref;
	u32 link_subscriptions;
	struct list_head link_subscribers;
};

static struct manager mng = { 0};

static DEFINE_SPINLOCK(config_lock);

static const void *req_tlv_area;	/* request message TLV area */
static int req_tlv_space;		/* request message TLV area size */
static int rep_headroom;		/* reply message headroom to use */


void tipc_cfg_link_event(u32 addr, char *name, int up)
{
	/* TIPC DOESN'T HANDLE LINK EVENT SUBSCRIPTIONS AT THE MOMENT */
}


struct sk_buff *tipc_cfg_reply_alloc(int payload_size)
{
	struct sk_buff *buf;

	buf = alloc_skb(rep_headroom + payload_size, GFP_ATOMIC);
	if (buf)
		skb_reserve(buf, rep_headroom);
	return buf;
}

int tipc_cfg_append_tlv(struct sk_buff *buf, int tlv_type, 
			void *tlv_data, int tlv_data_size)
{
	struct tlv_desc *tlv = (struct tlv_desc *)buf->tail;
	int new_tlv_space = TLV_SPACE(tlv_data_size);

	if (skb_tailroom(buf) < new_tlv_space) {
		dbg("tipc_cfg_append_tlv unable to append TLV\n");
		return 0;
	}
	skb_put(buf, new_tlv_space);
	tlv->tlv_type = htons(tlv_type);
	tlv->tlv_len  = htons(TLV_LENGTH(tlv_data_size));
	if (tlv_data_size && tlv_data)
		memcpy(TLV_DATA(tlv), tlv_data, tlv_data_size);
	return 1;
}

struct sk_buff *tipc_cfg_reply_unsigned_type(u16 tlv_type, u32 value)
{
	struct sk_buff *buf;
	__be32 value_net;

	buf = tipc_cfg_reply_alloc(TLV_SPACE(sizeof(value)));
	if (buf) {
		value_net = htonl(value);
		tipc_cfg_append_tlv(buf, tlv_type, &value_net, 
				    sizeof(value_net));
	}
	return buf;
}

struct sk_buff *tipc_cfg_reply_string_type(u16 tlv_type, char *string)
{
	struct sk_buff *buf;
	int string_len = strlen(string) + 1;

	buf = tipc_cfg_reply_alloc(TLV_SPACE(string_len));
	if (buf)
		tipc_cfg_append_tlv(buf, tlv_type, string, string_len);
	return buf;
}




#if 0

/* Now obsolete code for handling commands not yet implemented the new way */

int tipc_cfg_cmd(const struct tipc_cmd_msg * msg,
		 char *data,
		 u32 sz,
		 u32 *ret_size,
		 struct tipc_portid *orig)
{
	int rv = -EINVAL;
	u32 cmd = msg->cmd;

	*ret_size = 0;
	switch (cmd) {
	case TIPC_REMOVE_LINK:
	case TIPC_CMD_BLOCK_LINK:
	case TIPC_CMD_UNBLOCK_LINK:
		if (!cfg_check_connection(orig))
			rv = link_control(msg->argv.link_name, msg->cmd, 0);
		break;
	case TIPC_ESTABLISH:
		{
			int connected;

			tipc_isconnected(mng.conn_port_ref, &connected);
			if (connected || !orig) {
				rv = TIPC_FAILURE;
				break;
			}
			rv = tipc_connect2port(mng.conn_port_ref, orig);
			if (rv == TIPC_OK)
				orig = 0;
			break;
		}
	case TIPC_GET_PEER_ADDRESS:
		*ret_size = link_peer_addr(msg->argv.link_name, data, sz);
		break;
	case TIPC_GET_ROUTES:
		rv = TIPC_OK;
		break;
	default: {}
	}
	if (*ret_size)
		rv = TIPC_OK;
	return rv;
}

static void cfg_cmd_event(struct tipc_cmd_msg *msg,
			  char *data,
			  u32 sz,        
			  struct tipc_portid const *orig)
{
	int rv = -EINVAL;
	struct tipc_cmd_result_msg rmsg;
	struct iovec msg_sect[2];
	int *arg;

	msg->cmd = ntohl(msg->cmd);

	cfg_prepare_res_msg(msg->cmd, msg->usr_handle, rv, &rmsg, msg_sect, 
			    data, 0);
	if (ntohl(msg->magic) != TIPC_MAGIC)
		goto exit;

	switch (msg->cmd) {
	case TIPC_CREATE_LINK:
		if (!cfg_check_connection(orig))
			rv = disc_create_link(&msg->argv.create_link);
		break;
	case TIPC_LINK_SUBSCRIBE:
		{
			struct subscr_data *sub;

			if (mng.link_subscriptions > 64)
				break;
			sub = (struct subscr_data *)kmalloc(sizeof(*sub),
							    GFP_ATOMIC);
			if (sub == NULL) {
				warn("Memory squeeze; dropped remote link subscription\n");
				break;
			}
			INIT_LIST_HEAD(&sub->subd_list);
			tipc_createport(mng.user_ref,
					(void *)sub,
					TIPC_HIGH_IMPORTANCE,
					0,
					0,
					(tipc_conn_shutdown_event)cfg_linksubscr_cancel,
					0,
					0,
					(tipc_conn_msg_event)cfg_linksubscr_cancel,
					0,
					&sub->port_ref);
			if (!sub->port_ref) {
				kfree(sub);
				break;
			}
			memcpy(sub->usr_handle,msg->usr_handle,
			       sizeof(sub->usr_handle));
			sub->domain = msg->argv.domain;
			list_add_tail(&sub->subd_list, &mng.link_subscribers);
			tipc_connect2port(sub->port_ref, orig);
			rmsg.retval = TIPC_OK;
			tipc_send(sub->port_ref, 2u, msg_sect);
			mng.link_subscriptions++;
			return;
		}
	default:
		rv = tipc_cfg_cmd(msg, data, sz, (u32 *)&msg_sect[1].iov_len, orig);
	}
	exit:
	rmsg.result_len = htonl(msg_sect[1].iov_len);
	rmsg.retval = htonl(rv);
	tipc_cfg_respond(msg_sect, 2u, orig);
}
#endif

static struct sk_buff *cfg_enable_bearer(void)
{
	struct tipc_bearer_config *args;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_BEARER_CONFIG))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	args = (struct tipc_bearer_config *)TLV_DATA(req_tlv_area);
	if (tipc_enable_bearer(args->name,
			       ntohl(args->detect_scope),
			       ntohl(args->priority)))
		return tipc_cfg_reply_error_string("unable to enable bearer");

	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_disable_bearer(void)
{
	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_BEARER_NAME))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	if (tipc_disable_bearer((char *)TLV_DATA(req_tlv_area)))
		return tipc_cfg_reply_error_string("unable to disable bearer");

	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_own_addr(void)
{
	u32 addr;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_NET_ADDR))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	addr = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (addr == tipc_own_addr)
		return tipc_cfg_reply_none();
	if (!tipc_addr_node_valid(addr))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (node address)");
	if (tipc_mode == TIPC_NET_MODE)
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
						   " (cannot change node address once assigned)");
	tipc_own_addr = addr;

	/* 
	 * Must release all spinlocks before calling start_net() because
	 * Linux version of TIPC calls eth_media_start() which calls
	 * register_netdevice_notifier() which may block!
	 *
	 * Temporarily releasing the lock should be harmless for non-Linux TIPC,
	 * but Linux version of eth_media_start() should really be reworked
	 * so that it can be called with spinlocks held.
	 */

	spin_unlock_bh(&config_lock);
	tipc_core_start_net();
	spin_lock_bh(&config_lock);
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_remote_mng(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	tipc_remote_management = (value != 0);
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_publications(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != delimit(value, 1, 65535))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (max publications must be 1-65535)");
	tipc_max_publications = value;
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_subscriptions(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != delimit(value, 1, 65535))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (max subscriptions must be 1-65535");
	tipc_max_subscriptions = value;
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_ports(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == tipc_max_ports)
		return tipc_cfg_reply_none();
	if (value != delimit(value, 127, 65535))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (max ports must be 127-65535)");
	if (tipc_mode != TIPC_NOT_RUNNING)
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
			" (cannot change max ports while TIPC is active)");
	tipc_max_ports = value;
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_zones(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == tipc_max_zones)
		return tipc_cfg_reply_none();
	if (value != delimit(value, 1, 255))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (max zones must be 1-255)");
	if (tipc_mode == TIPC_NET_MODE)
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
			" (cannot change max zones once TIPC has joined a network)");
	tipc_max_zones = value;
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_clusters(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != delimit(value, 1, 1))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (max clusters fixed at 1)");
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_nodes(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == tipc_max_nodes)
		return tipc_cfg_reply_none();
	if (value != delimit(value, 8, 2047))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (max nodes must be 8-2047)");
	if (tipc_mode == TIPC_NET_MODE)
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
			" (cannot change max nodes once TIPC has joined a network)");
	tipc_max_nodes = value;
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_slaves(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != 0)
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
						   " (max secondary nodes fixed at 0)");
	return tipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_netid(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == tipc_net_id)
		return tipc_cfg_reply_none();
	if (value != delimit(value, 1, 9999))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (network id must be 1-9999)");
	if (tipc_mode == TIPC_NET_MODE)
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
			" (cannot change network id once TIPC has joined a network)");
	tipc_net_id = value;
	return tipc_cfg_reply_none();
}

struct sk_buff *tipc_cfg_do_cmd(u32 orig_node, u16 cmd, const void *request_area,
				int request_space, int reply_headroom)
{
	struct sk_buff *rep_tlv_buf;

	spin_lock_bh(&config_lock);

	/* Save request and reply details in a well-known location */

	req_tlv_area = request_area;
	req_tlv_space = request_space;
	rep_headroom = reply_headroom;

	/* Check command authorization */

	if (likely(orig_node == tipc_own_addr)) {
		/* command is permitted */
	} else if (cmd >= 0x8000) {
		rep_tlv_buf = tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
							  " (cannot be done remotely)");
		goto exit;
	} else if (!tipc_remote_management) {
		rep_tlv_buf = tipc_cfg_reply_error_string(TIPC_CFG_NO_REMOTE);
		goto exit;
	}
	else if (cmd >= 0x4000) {
		u32 domain = 0;

		if ((tipc_nametbl_translate(TIPC_ZM_SRV, 0, &domain) == 0) ||
		    (domain != orig_node)) {
			rep_tlv_buf = tipc_cfg_reply_error_string(TIPC_CFG_NOT_ZONE_MSTR);
			goto exit;
		}
	}

	/* Call appropriate processing routine */

	switch (cmd) {
	case TIPC_CMD_NOOP:
		rep_tlv_buf = tipc_cfg_reply_none();
		break;
	case TIPC_CMD_GET_NODES:
		rep_tlv_buf = tipc_node_get_nodes(req_tlv_area, req_tlv_space);
		break;
	case TIPC_CMD_GET_LINKS:
		rep_tlv_buf = tipc_node_get_links(req_tlv_area, req_tlv_space);
		break;
	case TIPC_CMD_SHOW_LINK_STATS:
		rep_tlv_buf = tipc_link_cmd_show_stats(req_tlv_area, req_tlv_space);
		break;
	case TIPC_CMD_RESET_LINK_STATS:
		rep_tlv_buf = tipc_link_cmd_reset_stats(req_tlv_area, req_tlv_space);
		break;
	case TIPC_CMD_SHOW_NAME_TABLE:
		rep_tlv_buf = tipc_nametbl_get(req_tlv_area, req_tlv_space);
		break;
	case TIPC_CMD_GET_BEARER_NAMES:
		rep_tlv_buf = tipc_bearer_get_names();
		break;
	case TIPC_CMD_GET_MEDIA_NAMES:
		rep_tlv_buf = tipc_media_get_names();
		break;
	case TIPC_CMD_SHOW_PORTS:
		rep_tlv_buf = tipc_port_get_ports();
		break;
#if 0
	case TIPC_CMD_SHOW_PORT_STATS:
		rep_tlv_buf = port_show_stats(req_tlv_area, req_tlv_space);
		break;
	case TIPC_CMD_RESET_PORT_STATS:
		rep_tlv_buf = tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED);
		break;
#endif
	case TIPC_CMD_SET_LOG_SIZE:
		rep_tlv_buf = tipc_log_resize(req_tlv_area, req_tlv_space);
		break;
	case TIPC_CMD_DUMP_LOG:
		rep_tlv_buf = tipc_log_dump();
		break;
	case TIPC_CMD_SET_LINK_TOL:
	case TIPC_CMD_SET_LINK_PRI:
	case TIPC_CMD_SET_LINK_WINDOW:
		rep_tlv_buf = tipc_link_cmd_config(req_tlv_area, req_tlv_space, cmd);
		break;
	case TIPC_CMD_ENABLE_BEARER:
		rep_tlv_buf = cfg_enable_bearer();
		break;
	case TIPC_CMD_DISABLE_BEARER:
		rep_tlv_buf = cfg_disable_bearer();
		break;
	case TIPC_CMD_SET_NODE_ADDR:
		rep_tlv_buf = cfg_set_own_addr();
		break;
	case TIPC_CMD_SET_REMOTE_MNG:
		rep_tlv_buf = cfg_set_remote_mng();
		break;
	case TIPC_CMD_SET_MAX_PORTS:
		rep_tlv_buf = cfg_set_max_ports();
		break;
	case TIPC_CMD_SET_MAX_PUBL:
		rep_tlv_buf = cfg_set_max_publications();
		break;
	case TIPC_CMD_SET_MAX_SUBSCR:
		rep_tlv_buf = cfg_set_max_subscriptions();
		break;
	case TIPC_CMD_SET_MAX_ZONES:
		rep_tlv_buf = cfg_set_max_zones();
		break;
	case TIPC_CMD_SET_MAX_CLUSTERS:
		rep_tlv_buf = cfg_set_max_clusters();
		break;
	case TIPC_CMD_SET_MAX_NODES:
		rep_tlv_buf = cfg_set_max_nodes();
		break;
	case TIPC_CMD_SET_MAX_SLAVES:
		rep_tlv_buf = cfg_set_max_slaves();
		break;
	case TIPC_CMD_SET_NETID:
		rep_tlv_buf = cfg_set_netid();
		break;
	case TIPC_CMD_GET_REMOTE_MNG:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_remote_management);
		break;
	case TIPC_CMD_GET_MAX_PORTS:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_max_ports);
		break;
	case TIPC_CMD_GET_MAX_PUBL:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_max_publications);
		break;
	case TIPC_CMD_GET_MAX_SUBSCR:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_max_subscriptions);
		break;
	case TIPC_CMD_GET_MAX_ZONES:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_max_zones);
		break;
	case TIPC_CMD_GET_MAX_CLUSTERS:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_max_clusters);
		break;
	case TIPC_CMD_GET_MAX_NODES:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_max_nodes);
		break;
	case TIPC_CMD_GET_MAX_SLAVES:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_max_slaves);
		break;
	case TIPC_CMD_GET_NETID:
		rep_tlv_buf = tipc_cfg_reply_unsigned(tipc_net_id);
		break;
	default:
		rep_tlv_buf = tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
							  " (unknown command)");
		break;
	}

	/* Return reply buffer */
exit:
	spin_unlock_bh(&config_lock);
	return rep_tlv_buf;
}

static void cfg_named_msg_event(void *userdata,
				u32 port_ref,
				struct sk_buff **buf,
				const unchar *msg,
				u32 size,
				u32 importance, 
				struct tipc_portid const *orig,
				struct tipc_name_seq const *dest)
{
	struct tipc_cfg_msg_hdr *req_hdr;
	struct tipc_cfg_msg_hdr *rep_hdr;
	struct sk_buff *rep_buf;

	/* Validate configuration message header (ignore invalid message) */

	req_hdr = (struct tipc_cfg_msg_hdr *)msg;
	if ((size < sizeof(*req_hdr)) ||
	    (size != TCM_ALIGN(ntohl(req_hdr->tcm_len))) ||
	    (ntohs(req_hdr->tcm_flags) != TCM_F_REQUEST)) {
		warn("Invalid configuration message discarded\n");
		return;
	}

	/* Generate reply for request (if can't, return request) */

	rep_buf = tipc_cfg_do_cmd(orig->node,
				  ntohs(req_hdr->tcm_type), 
				  msg + sizeof(*req_hdr),
				  size - sizeof(*req_hdr),
				  BUF_HEADROOM + MAX_H_SIZE + sizeof(*rep_hdr));
	if (rep_buf) {
		skb_push(rep_buf, sizeof(*rep_hdr));
		rep_hdr = (struct tipc_cfg_msg_hdr *)rep_buf->data;
		memcpy(rep_hdr, req_hdr, sizeof(*rep_hdr));
		rep_hdr->tcm_len = htonl(rep_buf->len);
		rep_hdr->tcm_flags &= htons(~TCM_F_REQUEST);
	} else {
		rep_buf = *buf;
		*buf = NULL;
	}

	/* NEED TO ADD CODE TO HANDLE FAILED SEND (SUCH AS CONGESTION) */
	tipc_send_buf2port(port_ref, orig, rep_buf, rep_buf->len);
}

int tipc_cfg_init(void)
{
	struct tipc_name_seq seq;
	int res;

	memset(&mng, 0, sizeof(mng));
	INIT_LIST_HEAD(&mng.link_subscribers);

	res = tipc_attach(&mng.user_ref, NULL, NULL);
	if (res)
		goto failed;

	res = tipc_createport(mng.user_ref, NULL, TIPC_CRITICAL_IMPORTANCE,
			      NULL, NULL, NULL,
			      NULL, cfg_named_msg_event, NULL,
			      NULL, &mng.port_ref);
	if (res)
		goto failed;

	seq.type = TIPC_CFG_SRV;
	seq.lower = seq.upper = tipc_own_addr;
	res = tipc_nametbl_publish_rsv(mng.port_ref, TIPC_ZONE_SCOPE, &seq);
	if (res)
		goto failed;

	return 0;

failed:
	err("Unable to create configuration service\n");
	tipc_detach(mng.user_ref);
	mng.user_ref = 0;
	return res;
}

void tipc_cfg_stop(void)
{
	if (mng.user_ref) {
		tipc_detach(mng.user_ref);
		mng.user_ref = 0;
	}
}
