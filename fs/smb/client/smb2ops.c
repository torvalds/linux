// SPDX-License-Identifier: GPL-2.0
/*
 *  SMB2 version specific operations
 *
 *  Copyright (c) 2012, Jeff Layton <jlayton@redhat.com>
 */

#include <linux/pagemap.h>
#include <linux/vfs.h>
#include <linux/falloc.h>
#include <linux/scatterlist.h>
#include <linux/uuid.h>
#include <linux/sort.h>
#include <crypto/aead.h>
#include <linux/fiemap.h>
#include <uapi/linux/magic.h>
#include "cifsfs.h"
#include "cifsglob.h"
#include "smb2pdu.h"
#include "smb2proto.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_unicode.h"
#include "smb2status.h"
#include "smb2glob.h"
#include "cifs_ioctl.h"
#include "smbdirect.h"
#include "fscache.h"
#include "fs_context.h"
#include "cached_dir.h"

/* Change credits for different ops and return the total number of credits */
static int
change_conf(struct TCP_Server_Info *server)
{
	server->credits += server->echo_credits + server->oplock_credits;
	if (server->credits > server->max_credits)
		server->credits = server->max_credits;
	server->oplock_credits = server->echo_credits = 0;
	switch (server->credits) {
	case 0:
		return 0;
	case 1:
		server->echoes = false;
		server->oplocks = false;
		break;
	case 2:
		server->echoes = true;
		server->oplocks = false;
		server->echo_credits = 1;
		break;
	default:
		server->echoes = true;
		if (enable_oplocks) {
			server->oplocks = true;
			server->oplock_credits = 1;
		} else
			server->oplocks = false;

		server->echo_credits = 1;
	}
	server->credits -= server->echo_credits + server->oplock_credits;
	return server->credits + server->echo_credits + server->oplock_credits;
}

static void
smb2_add_credits(struct TCP_Server_Info *server,
		 const struct cifs_credits *credits, const int optype)
{
	int *val, rc = -1;
	int scredits, in_flight;
	unsigned int add = credits->value;
	unsigned int instance = credits->instance;
	bool reconnect_detected = false;
	bool reconnect_with_invalid_credits = false;

	spin_lock(&server->req_lock);
	val = server->ops->get_credits_field(server, optype);

	/* eg found case where write overlapping reconnect messed up credits */
	if (((optype & CIFS_OP_MASK) == CIFS_NEG_OP) && (*val != 0))
		reconnect_with_invalid_credits = true;

	if ((instance == 0) || (instance == server->reconnect_instance))
		*val += add;
	else
		reconnect_detected = true;

	if (*val > 65000) {
		*val = 65000; /* Don't get near 64K credits, avoid srv bugs */
		pr_warn_once("server overflowed SMB3 credits\n");
		trace_smb3_overflow_credits(server->CurrentMid,
					    server->conn_id, server->hostname, *val,
					    add, server->in_flight);
	}
	WARN_ON_ONCE(server->in_flight == 0);
	server->in_flight--;
	if (server->in_flight == 0 &&
	   ((optype & CIFS_OP_MASK) != CIFS_NEG_OP) &&
	   ((optype & CIFS_OP_MASK) != CIFS_SESS_OP))
		rc = change_conf(server);
	/*
	 * Sometimes server returns 0 credits on oplock break ack - we need to
	 * rebalance credits in this case.
	 */
	else if (server->in_flight > 0 && server->oplock_credits == 0 &&
		 server->oplocks) {
		if (server->credits > 1) {
			server->credits--;
			server->oplock_credits++;
		}
	} else if ((server->in_flight > 0) && (server->oplock_credits > 3) &&
		   ((optype & CIFS_OP_MASK) == CIFS_OBREAK_OP))
		/* if now have too many oplock credits, rebalance so don't starve normal ops */
		change_conf(server);

	scredits = *val;
	in_flight = server->in_flight;
	spin_unlock(&server->req_lock);
	wake_up(&server->request_q);

	if (reconnect_detected) {
		trace_smb3_reconnect_detected(server->CurrentMid,
			server->conn_id, server->hostname, scredits, add, in_flight);

		cifs_dbg(FYI, "trying to put %d credits from the old server instance %d\n",
			 add, instance);
	}

	if (reconnect_with_invalid_credits) {
		trace_smb3_reconnect_with_invalid_credits(server->CurrentMid,
			server->conn_id, server->hostname, scredits, add, in_flight);
		cifs_dbg(FYI, "Negotiate operation when server credits is non-zero. Optype: %d, server credits: %d, credits added: %d\n",
			 optype, scredits, add);
	}

	spin_lock(&server->srv_lock);
	if (server->tcpStatus == CifsNeedReconnect
	    || server->tcpStatus == CifsExiting) {
		spin_unlock(&server->srv_lock);
		return;
	}
	spin_unlock(&server->srv_lock);

	switch (rc) {
	case -1:
		/* change_conf hasn't been executed */
		break;
	case 0:
		cifs_server_dbg(VFS, "Possible client or server bug - zero credits\n");
		break;
	case 1:
		cifs_server_dbg(VFS, "disabling echoes and oplocks\n");
		break;
	case 2:
		cifs_dbg(FYI, "disabling oplocks\n");
		break;
	default:
		/* change_conf rebalanced credits for different types */
		break;
	}

	trace_smb3_add_credits(server->CurrentMid,
			server->conn_id, server->hostname, scredits, add, in_flight);
	cifs_dbg(FYI, "%s: added %u credits total=%d\n", __func__, add, scredits);
}

static void
smb2_set_credits(struct TCP_Server_Info *server, const int val)
{
	int scredits, in_flight;

	spin_lock(&server->req_lock);
	server->credits = val;
	if (val == 1) {
		server->reconnect_instance++;
		/*
		 * ChannelSequence updated for all channels in primary channel so that consistent
		 * across SMB3 requests sent on any channel. See MS-SMB2 3.2.4.1 and 3.2.7.1
		 */
		if (SERVER_IS_CHAN(server))
			server->primary_server->channel_sequence_num++;
		else
			server->channel_sequence_num++;
	}
	scredits = server->credits;
	in_flight = server->in_flight;
	spin_unlock(&server->req_lock);

	trace_smb3_set_credits(server->CurrentMid,
			server->conn_id, server->hostname, scredits, val, in_flight);
	cifs_dbg(FYI, "%s: set %u credits\n", __func__, val);

	/* don't log while holding the lock */
	if (val == 1)
		cifs_dbg(FYI, "set credits to 1 due to smb2 reconnect\n");
}

static int *
smb2_get_credits_field(struct TCP_Server_Info *server, const int optype)
{
	switch (optype) {
	case CIFS_ECHO_OP:
		return &server->echo_credits;
	case CIFS_OBREAK_OP:
		return &server->oplock_credits;
	default:
		return &server->credits;
	}
}

static unsigned int
smb2_get_credits(struct mid_q_entry *mid)
{
	return mid->credits_received;
}

static int
smb2_wait_mtu_credits(struct TCP_Server_Info *server, unsigned int size,
		      unsigned int *num, struct cifs_credits *credits)
{
	int rc = 0;
	unsigned int scredits, in_flight;

	spin_lock(&server->req_lock);
	while (1) {
		spin_unlock(&server->req_lock);

		spin_lock(&server->srv_lock);
		if (server->tcpStatus == CifsExiting) {
			spin_unlock(&server->srv_lock);
			return -ENOENT;
		}
		spin_unlock(&server->srv_lock);

		spin_lock(&server->req_lock);
		if (server->credits <= 0) {
			spin_unlock(&server->req_lock);
			cifs_num_waiters_inc(server);
			rc = wait_event_killable(server->request_q,
				has_credits(server, &server->credits, 1));
			cifs_num_waiters_dec(server);
			if (rc)
				return rc;
			spin_lock(&server->req_lock);
		} else {
			scredits = server->credits;
			/* can deadlock with reopen */
			if (scredits <= 8) {
				*num = SMB2_MAX_BUFFER_SIZE;
				credits->value = 0;
				credits->instance = 0;
				break;
			}

			/* leave some credits for reopen and other ops */
			scredits -= 8;
			*num = min_t(unsigned int, size,
				     scredits * SMB2_MAX_BUFFER_SIZE);

			credits->value =
				DIV_ROUND_UP(*num, SMB2_MAX_BUFFER_SIZE);
			credits->instance = server->reconnect_instance;
			server->credits -= credits->value;
			server->in_flight++;
			if (server->in_flight > server->max_in_flight)
				server->max_in_flight = server->in_flight;
			break;
		}
	}
	scredits = server->credits;
	in_flight = server->in_flight;
	spin_unlock(&server->req_lock);

	trace_smb3_wait_credits(server->CurrentMid,
			server->conn_id, server->hostname, scredits, -(credits->value), in_flight);
	cifs_dbg(FYI, "%s: removed %u credits total=%d\n",
			__func__, credits->value, scredits);

	return rc;
}

static int
smb2_adjust_credits(struct TCP_Server_Info *server,
		    struct cifs_credits *credits,
		    const unsigned int payload_size)
{
	int new_val = DIV_ROUND_UP(payload_size, SMB2_MAX_BUFFER_SIZE);
	int scredits, in_flight;

	if (!credits->value || credits->value == new_val)
		return 0;

	if (credits->value < new_val) {
		trace_smb3_too_many_credits(server->CurrentMid,
				server->conn_id, server->hostname, 0, credits->value - new_val, 0);
		cifs_server_dbg(VFS, "request has less credits (%d) than required (%d)",
				credits->value, new_val);

		return -EOPNOTSUPP;
	}

	spin_lock(&server->req_lock);

	if (server->reconnect_instance != credits->instance) {
		scredits = server->credits;
		in_flight = server->in_flight;
		spin_unlock(&server->req_lock);

		trace_smb3_reconnect_detected(server->CurrentMid,
			server->conn_id, server->hostname, scredits,
			credits->value - new_val, in_flight);
		cifs_server_dbg(VFS, "trying to return %d credits to old session\n",
			 credits->value - new_val);
		return -EAGAIN;
	}

	server->credits += credits->value - new_val;
	scredits = server->credits;
	in_flight = server->in_flight;
	spin_unlock(&server->req_lock);
	wake_up(&server->request_q);

	trace_smb3_adj_credits(server->CurrentMid,
			server->conn_id, server->hostname, scredits,
			credits->value - new_val, in_flight);
	cifs_dbg(FYI, "%s: adjust added %u credits total=%d\n",
			__func__, credits->value - new_val, scredits);

	credits->value = new_val;

	return 0;
}

static __u64
smb2_get_next_mid(struct TCP_Server_Info *server)
{
	__u64 mid;
	/* for SMB2 we need the current value */
	spin_lock(&server->mid_lock);
	mid = server->CurrentMid++;
	spin_unlock(&server->mid_lock);
	return mid;
}

static void
smb2_revert_current_mid(struct TCP_Server_Info *server, const unsigned int val)
{
	spin_lock(&server->mid_lock);
	if (server->CurrentMid >= val)
		server->CurrentMid -= val;
	spin_unlock(&server->mid_lock);
}

static struct mid_q_entry *
__smb2_find_mid(struct TCP_Server_Info *server, char *buf, bool dequeue)
{
	struct mid_q_entry *mid;
	struct smb2_hdr *shdr = (struct smb2_hdr *)buf;
	__u64 wire_mid = le64_to_cpu(shdr->MessageId);

	if (shdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM) {
		cifs_server_dbg(VFS, "Encrypted frame parsing not supported yet\n");
		return NULL;
	}

	spin_lock(&server->mid_lock);
	list_for_each_entry(mid, &server->pending_mid_q, qhead) {
		if ((mid->mid == wire_mid) &&
		    (mid->mid_state == MID_REQUEST_SUBMITTED) &&
		    (mid->command == shdr->Command)) {
			kref_get(&mid->refcount);
			if (dequeue) {
				list_del_init(&mid->qhead);
				mid->mid_flags |= MID_DELETED;
			}
			spin_unlock(&server->mid_lock);
			return mid;
		}
	}
	spin_unlock(&server->mid_lock);
	return NULL;
}

static struct mid_q_entry *
smb2_find_mid(struct TCP_Server_Info *server, char *buf)
{
	return __smb2_find_mid(server, buf, false);
}

static struct mid_q_entry *
smb2_find_dequeue_mid(struct TCP_Server_Info *server, char *buf)
{
	return __smb2_find_mid(server, buf, true);
}

static void
smb2_dump_detail(void *buf, struct TCP_Server_Info *server)
{
#ifdef CONFIG_CIFS_DEBUG2
	struct smb2_hdr *shdr = (struct smb2_hdr *)buf;

	cifs_server_dbg(VFS, "Cmd: %d Err: 0x%x Flags: 0x%x Mid: %llu Pid: %d\n",
		 shdr->Command, shdr->Status, shdr->Flags, shdr->MessageId,
		 shdr->Id.SyncId.ProcessId);
	cifs_server_dbg(VFS, "smb buf %p len %u\n", buf,
		 server->ops->calc_smb_size(buf));
#endif
}

static bool
smb2_need_neg(struct TCP_Server_Info *server)
{
	return server->max_read == 0;
}

static int
smb2_negotiate(const unsigned int xid,
	       struct cifs_ses *ses,
	       struct TCP_Server_Info *server)
{
	int rc;

	spin_lock(&server->mid_lock);
	server->CurrentMid = 0;
	spin_unlock(&server->mid_lock);
	rc = SMB2_negotiate(xid, ses, server);
	/* BB we probably don't need to retry with modern servers */
	if (rc == -EAGAIN)
		rc = -EHOSTDOWN;
	return rc;
}

static unsigned int
smb2_negotiate_wsize(struct cifs_tcon *tcon, struct smb3_fs_context *ctx)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int wsize;

	/* start with specified wsize, or default */
	wsize = ctx->wsize ? ctx->wsize : CIFS_DEFAULT_IOSIZE;
	wsize = min_t(unsigned int, wsize, server->max_write);
	if (!(server->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU))
		wsize = min_t(unsigned int, wsize, SMB2_MAX_BUFFER_SIZE);

	return wsize;
}

static unsigned int
smb3_negotiate_wsize(struct cifs_tcon *tcon, struct smb3_fs_context *ctx)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int wsize;

	/* start with specified wsize, or default */
	wsize = ctx->wsize ? ctx->wsize : SMB3_DEFAULT_IOSIZE;
	wsize = min_t(unsigned int, wsize, server->max_write);
#ifdef CONFIG_CIFS_SMB_DIRECT
	if (server->rdma) {
		if (server->sign)
			/*
			 * Account for SMB2 data transfer packet header and
			 * possible encryption header
			 */
			wsize = min_t(unsigned int,
				wsize,
				server->smbd_conn->max_fragmented_send_size -
					SMB2_READWRITE_PDU_HEADER_SIZE -
					sizeof(struct smb2_transform_hdr));
		else
			wsize = min_t(unsigned int,
				wsize, server->smbd_conn->max_readwrite_size);
	}
#endif
	if (!(server->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU))
		wsize = min_t(unsigned int, wsize, SMB2_MAX_BUFFER_SIZE);

	return wsize;
}

static unsigned int
smb2_negotiate_rsize(struct cifs_tcon *tcon, struct smb3_fs_context *ctx)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int rsize;

	/* start with specified rsize, or default */
	rsize = ctx->rsize ? ctx->rsize : CIFS_DEFAULT_IOSIZE;
	rsize = min_t(unsigned int, rsize, server->max_read);

	if (!(server->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU))
		rsize = min_t(unsigned int, rsize, SMB2_MAX_BUFFER_SIZE);

	return rsize;
}

static unsigned int
smb3_negotiate_rsize(struct cifs_tcon *tcon, struct smb3_fs_context *ctx)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int rsize;

	/* start with specified rsize, or default */
	rsize = ctx->rsize ? ctx->rsize : SMB3_DEFAULT_IOSIZE;
	rsize = min_t(unsigned int, rsize, server->max_read);
#ifdef CONFIG_CIFS_SMB_DIRECT
	if (server->rdma) {
		if (server->sign)
			/*
			 * Account for SMB2 data transfer packet header and
			 * possible encryption header
			 */
			rsize = min_t(unsigned int,
				rsize,
				server->smbd_conn->max_fragmented_recv_size -
					SMB2_READWRITE_PDU_HEADER_SIZE -
					sizeof(struct smb2_transform_hdr));
		else
			rsize = min_t(unsigned int,
				rsize, server->smbd_conn->max_readwrite_size);
	}
#endif

	if (!(server->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU))
		rsize = min_t(unsigned int, rsize, SMB2_MAX_BUFFER_SIZE);

	return rsize;
}

/*
 * compare two interfaces a and b
 * return 0 if everything matches.
 * return 1 if a is rdma capable, or rss capable, or has higher link speed
 * return -1 otherwise.
 */
static int
iface_cmp(struct cifs_server_iface *a, struct cifs_server_iface *b)
{
	int cmp_ret = 0;

	WARN_ON(!a || !b);
	if (a->rdma_capable == b->rdma_capable) {
		if (a->rss_capable == b->rss_capable) {
			if (a->speed == b->speed) {
				cmp_ret = cifs_ipaddr_cmp((struct sockaddr *) &a->sockaddr,
							  (struct sockaddr *) &b->sockaddr);
				if (!cmp_ret)
					return 0;
				else if (cmp_ret > 0)
					return 1;
				else
					return -1;
			} else if (a->speed > b->speed)
				return 1;
			else
				return -1;
		} else if (a->rss_capable > b->rss_capable)
			return 1;
		else
			return -1;
	} else if (a->rdma_capable > b->rdma_capable)
		return 1;
	else
		return -1;
}

static int
parse_server_interfaces(struct network_interface_info_ioctl_rsp *buf,
			size_t buf_len, struct cifs_ses *ses, bool in_mount)
{
	struct network_interface_info_ioctl_rsp *p;
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
	struct iface_info_ipv4 *p4;
	struct iface_info_ipv6 *p6;
	struct cifs_server_iface *info = NULL, *iface = NULL, *niface = NULL;
	struct cifs_server_iface tmp_iface;
	ssize_t bytes_left;
	size_t next = 0;
	int nb_iface = 0;
	int rc = 0, ret = 0;

	bytes_left = buf_len;
	p = buf;

	spin_lock(&ses->iface_lock);
	/* do not query too frequently, this time with lock held */
	if (ses->iface_last_update &&
	    time_before(jiffies, ses->iface_last_update +
			(SMB_INTERFACE_POLL_INTERVAL * HZ))) {
		spin_unlock(&ses->iface_lock);
		return 0;
	}

	/*
	 * Go through iface_list and do kref_put to remove
	 * any unused ifaces. ifaces in use will be removed
	 * when the last user calls a kref_put on it
	 */
	list_for_each_entry_safe(iface, niface, &ses->iface_list,
				 iface_head) {
		iface->is_active = 0;
		kref_put(&iface->refcount, release_iface);
		ses->iface_count--;
	}
	spin_unlock(&ses->iface_lock);

	/*
	 * Samba server e.g. can return an empty interface list in some cases,
	 * which would only be a problem if we were requesting multichannel
	 */
	if (bytes_left == 0) {
		/* avoid spamming logs every 10 minutes, so log only in mount */
		if ((ses->chan_max > 1) && in_mount)
			cifs_dbg(VFS,
				 "multichannel not available\n"
				 "Empty network interface list returned by server %s\n",
				 ses->server->hostname);
		rc = -EINVAL;
		goto out;
	}

	while (bytes_left >= sizeof(*p)) {
		memset(&tmp_iface, 0, sizeof(tmp_iface));
		tmp_iface.speed = le64_to_cpu(p->LinkSpeed);
		tmp_iface.rdma_capable = le32_to_cpu(p->Capability & RDMA_CAPABLE) ? 1 : 0;
		tmp_iface.rss_capable = le32_to_cpu(p->Capability & RSS_CAPABLE) ? 1 : 0;

		switch (p->Family) {
		/*
		 * The kernel and wire socket structures have the same
		 * layout and use network byte order but make the
		 * conversion explicit in case either one changes.
		 */
		case INTERNETWORK:
			addr4 = (struct sockaddr_in *)&tmp_iface.sockaddr;
			p4 = (struct iface_info_ipv4 *)p->Buffer;
			addr4->sin_family = AF_INET;
			memcpy(&addr4->sin_addr, &p4->IPv4Address, 4);

			/* [MS-SMB2] 2.2.32.5.1.1 Clients MUST ignore these */
			addr4->sin_port = cpu_to_be16(CIFS_PORT);

			cifs_dbg(FYI, "%s: ipv4 %pI4\n", __func__,
				 &addr4->sin_addr);
			break;
		case INTERNETWORKV6:
			addr6 =	(struct sockaddr_in6 *)&tmp_iface.sockaddr;
			p6 = (struct iface_info_ipv6 *)p->Buffer;
			addr6->sin6_family = AF_INET6;
			memcpy(&addr6->sin6_addr, &p6->IPv6Address, 16);

			/* [MS-SMB2] 2.2.32.5.1.2 Clients MUST ignore these */
			addr6->sin6_flowinfo = 0;
			addr6->sin6_scope_id = 0;
			addr6->sin6_port = cpu_to_be16(CIFS_PORT);

			cifs_dbg(FYI, "%s: ipv6 %pI6\n", __func__,
				 &addr6->sin6_addr);
			break;
		default:
			cifs_dbg(VFS,
				 "%s: skipping unsupported socket family\n",
				 __func__);
			goto next_iface;
		}

		/*
		 * The iface_list is assumed to be sorted by speed.
		 * Check if the new interface exists in that list.
		 * NEVER change iface. it could be in use.
		 * Add a new one instead
		 */
		spin_lock(&ses->iface_lock);
		list_for_each_entry_safe(iface, niface, &ses->iface_list,
					 iface_head) {
			ret = iface_cmp(iface, &tmp_iface);
			if (!ret) {
				/* just get a ref so that it doesn't get picked/freed */
				iface->is_active = 1;
				kref_get(&iface->refcount);
				ses->iface_count++;
				spin_unlock(&ses->iface_lock);
				goto next_iface;
			} else if (ret < 0) {
				/* all remaining ifaces are slower */
				kref_get(&iface->refcount);
				break;
			}
		}
		spin_unlock(&ses->iface_lock);

		/* no match. insert the entry in the list */
		info = kmalloc(sizeof(struct cifs_server_iface),
			       GFP_KERNEL);
		if (!info) {
			rc = -ENOMEM;
			goto out;
		}
		memcpy(info, &tmp_iface, sizeof(tmp_iface));

		/* add this new entry to the list */
		kref_init(&info->refcount);
		info->is_active = 1;

		cifs_dbg(FYI, "%s: adding iface %zu\n", __func__, ses->iface_count);
		cifs_dbg(FYI, "%s: speed %zu bps\n", __func__, info->speed);
		cifs_dbg(FYI, "%s: capabilities 0x%08x\n", __func__,
			 le32_to_cpu(p->Capability));

		spin_lock(&ses->iface_lock);
		if (!list_entry_is_head(iface, &ses->iface_list, iface_head)) {
			list_add_tail(&info->iface_head, &iface->iface_head);
			kref_put(&iface->refcount, release_iface);
		} else
			list_add_tail(&info->iface_head, &ses->iface_list);

		ses->iface_count++;
		spin_unlock(&ses->iface_lock);
		ses->iface_last_update = jiffies;
next_iface:
		nb_iface++;
		next = le32_to_cpu(p->Next);
		if (!next) {
			bytes_left -= sizeof(*p);
			break;
		}
		p = (struct network_interface_info_ioctl_rsp *)((u8 *)p+next);
		bytes_left -= next;
	}

	if (!nb_iface) {
		cifs_dbg(VFS, "%s: malformed interface info\n", __func__);
		rc = -EINVAL;
		goto out;
	}

	/* Azure rounds the buffer size up 8, to a 16 byte boundary */
	if ((bytes_left > 8) || p->Next)
		cifs_dbg(VFS, "%s: incomplete interface info\n", __func__);


	if (!ses->iface_count) {
		rc = -EINVAL;
		goto out;
	}

out:
	return rc;
}

int
SMB3_request_interfaces(const unsigned int xid, struct cifs_tcon *tcon, bool in_mount)
{
	int rc;
	unsigned int ret_data_len = 0;
	struct network_interface_info_ioctl_rsp *out_buf = NULL;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *pserver;

	/* do not query too frequently */
	if (ses->iface_last_update &&
	    time_before(jiffies, ses->iface_last_update +
			(SMB_INTERFACE_POLL_INTERVAL * HZ)))
		return 0;

	rc = SMB2_ioctl(xid, tcon, NO_FILE_ID, NO_FILE_ID,
			FSCTL_QUERY_NETWORK_INTERFACE_INFO,
			NULL /* no data input */, 0 /* no data input */,
			CIFSMaxBufSize, (char **)&out_buf, &ret_data_len);
	if (rc == -EOPNOTSUPP) {
		cifs_dbg(FYI,
			 "server does not support query network interfaces\n");
		ret_data_len = 0;
	} else if (rc != 0) {
		cifs_tcon_dbg(VFS, "error %d on ioctl to get interface list\n", rc);
		goto out;
	}

	rc = parse_server_interfaces(out_buf, ret_data_len, ses, in_mount);
	if (rc)
		goto out;

	/* check if iface is still active */
	pserver = ses->chans[0].server;
	if (pserver && !cifs_chan_is_iface_active(ses, pserver))
		cifs_chan_update_iface(ses, pserver);

out:
	kfree(out_buf);
	return rc;
}

static void
smb3_qfs_tcon(const unsigned int xid, struct cifs_tcon *tcon,
	      struct cifs_sb_info *cifs_sb)
{
	int rc;
	__le16 srch_path = 0; /* Null - open root of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct cached_fid *cfid = NULL;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = "",
		.desired_access = FILE_READ_ATTRIBUTES,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = &fid,
	};

	rc = open_cached_dir(xid, tcon, "", cifs_sb, false, &cfid);
	if (rc == 0)
		memcpy(&fid, &cfid->fid, sizeof(struct cifs_fid));
	else
		rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL,
			       NULL, NULL);
	if (rc)
		return;

	SMB3_request_interfaces(xid, tcon, true /* called during  mount */);

	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_ATTRIBUTE_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_DEVICE_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_VOLUME_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_SECTOR_SIZE_INFORMATION); /* SMB3 specific */
	if (cfid == NULL)
		SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	else
		close_cached_dir(cfid);
}

static void
smb2_qfs_tcon(const unsigned int xid, struct cifs_tcon *tcon,
	      struct cifs_sb_info *cifs_sb)
{
	int rc;
	__le16 srch_path = 0; /* Null - open root of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = "",
		.desired_access = FILE_READ_ATTRIBUTES,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = &fid,
	};

	rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL,
		       NULL, NULL);
	if (rc)
		return;

	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_ATTRIBUTE_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_DEVICE_INFORMATION);
	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
}

static int
smb2_is_path_accessible(const unsigned int xid, struct cifs_tcon *tcon,
			struct cifs_sb_info *cifs_sb, const char *full_path)
{
	__le16 *utf16_path;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	int err_buftype = CIFS_NO_BUFFER;
	struct cifs_open_parms oparms;
	struct kvec err_iov = {};
	struct cifs_fid fid;
	struct cached_fid *cfid;
	bool islink;
	int rc, rc2;

	rc = open_cached_dir(xid, tcon, full_path, cifs_sb, true, &cfid);
	if (!rc) {
		if (cfid->has_lease) {
			close_cached_dir(cfid);
			return 0;
		}
		close_cached_dir(cfid);
	}

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = full_path,
		.desired_access = FILE_READ_ATTRIBUTES,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = &fid,
	};

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL,
		       &err_iov, &err_buftype);
	if (rc) {
		struct smb2_hdr *hdr = err_iov.iov_base;

		if (unlikely(!hdr || err_buftype == CIFS_NO_BUFFER))
			goto out;

		if (rc != -EREMOTE && hdr->Status == STATUS_OBJECT_NAME_INVALID) {
			rc2 = cifs_inval_name_dfs_link_error(xid, tcon, cifs_sb,
							     full_path, &islink);
			if (rc2) {
				rc = rc2;
				goto out;
			}
			if (islink)
				rc = -EREMOTE;
		}
		if (rc == -EREMOTE && IS_ENABLED(CONFIG_CIFS_DFS_UPCALL) && cifs_sb &&
		    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS))
			rc = -EOPNOTSUPP;
		goto out;
	}

	rc = SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);

out:
	free_rsp_buf(err_buftype, err_iov.iov_base);
	kfree(utf16_path);
	return rc;
}

static int smb2_get_srv_inum(const unsigned int xid, struct cifs_tcon *tcon,
			     struct cifs_sb_info *cifs_sb, const char *full_path,
			     u64 *uniqueid, struct cifs_open_info_data *data)
{
	*uniqueid = le64_to_cpu(data->fi.IndexNumber);
	return 0;
}

static int smb2_query_file_info(const unsigned int xid, struct cifs_tcon *tcon,
				struct cifsFileInfo *cfile, struct cifs_open_info_data *data)
{
	struct cifs_fid *fid = &cfile->fid;

	if (cfile->symlink_target) {
		data->symlink_target = kstrdup(cfile->symlink_target, GFP_KERNEL);
		if (!data->symlink_target)
			return -ENOMEM;
	}
	return SMB2_query_info(xid, tcon, fid->persistent_fid, fid->volatile_fid, &data->fi);
}

#ifdef CONFIG_CIFS_XATTR
static ssize_t
move_smb2_ea_to_cifs(char *dst, size_t dst_size,
		     struct smb2_file_full_ea_info *src, size_t src_size,
		     const unsigned char *ea_name)
{
	int rc = 0;
	unsigned int ea_name_len = ea_name ? strlen(ea_name) : 0;
	char *name, *value;
	size_t buf_size = dst_size;
	size_t name_len, value_len, user_name_len;

	while (src_size > 0) {
		name_len = (size_t)src->ea_name_length;
		value_len = (size_t)le16_to_cpu(src->ea_value_length);

		if (name_len == 0)
			break;

		if (src_size < 8 + name_len + 1 + value_len) {
			cifs_dbg(FYI, "EA entry goes beyond length of list\n");
			rc = -EIO;
			goto out;
		}

		name = &src->ea_data[0];
		value = &src->ea_data[src->ea_name_length + 1];

		if (ea_name) {
			if (ea_name_len == name_len &&
			    memcmp(ea_name, name, name_len) == 0) {
				rc = value_len;
				if (dst_size == 0)
					goto out;
				if (dst_size < value_len) {
					rc = -ERANGE;
					goto out;
				}
				memcpy(dst, value, value_len);
				goto out;
			}
		} else {
			/* 'user.' plus a terminating null */
			user_name_len = 5 + 1 + name_len;

			if (buf_size == 0) {
				/* skip copy - calc size only */
				rc += user_name_len;
			} else if (dst_size >= user_name_len) {
				dst_size -= user_name_len;
				memcpy(dst, "user.", 5);
				dst += 5;
				memcpy(dst, src->ea_data, name_len);
				dst += name_len;
				*dst = 0;
				++dst;
				rc += user_name_len;
			} else {
				/* stop before overrun buffer */
				rc = -ERANGE;
				break;
			}
		}

		if (!src->next_entry_offset)
			break;

		if (src_size < le32_to_cpu(src->next_entry_offset)) {
			/* stop before overrun buffer */
			rc = -ERANGE;
			break;
		}
		src_size -= le32_to_cpu(src->next_entry_offset);
		src = (void *)((char *)src +
			       le32_to_cpu(src->next_entry_offset));
	}

	/* didn't find the named attribute */
	if (ea_name)
		rc = -ENODATA;

out:
	return (ssize_t)rc;
}

static ssize_t
smb2_query_eas(const unsigned int xid, struct cifs_tcon *tcon,
	       const unsigned char *path, const unsigned char *ea_name,
	       char *ea_data, size_t buf_size,
	       struct cifs_sb_info *cifs_sb)
{
	int rc;
	struct kvec rsp_iov = {NULL, 0};
	int buftype = CIFS_NO_BUFFER;
	struct smb2_query_info_rsp *rsp;
	struct smb2_file_full_ea_info *info = NULL;

	rc = smb2_query_info_compound(xid, tcon, path,
				      FILE_READ_EA,
				      FILE_FULL_EA_INFORMATION,
				      SMB2_O_INFO_FILE,
				      CIFSMaxBufSize -
				      MAX_SMB2_CREATE_RESPONSE_SIZE -
				      MAX_SMB2_CLOSE_RESPONSE_SIZE,
				      &rsp_iov, &buftype, cifs_sb);
	if (rc) {
		/*
		 * If ea_name is NULL (listxattr) and there are no EAs,
		 * return 0 as it's not an error. Otherwise, the specified
		 * ea_name was not found.
		 */
		if (!ea_name && rc == -ENODATA)
			rc = 0;
		goto qeas_exit;
	}

	rsp = (struct smb2_query_info_rsp *)rsp_iov.iov_base;
	rc = smb2_validate_iov(le16_to_cpu(rsp->OutputBufferOffset),
			       le32_to_cpu(rsp->OutputBufferLength),
			       &rsp_iov,
			       sizeof(struct smb2_file_full_ea_info));
	if (rc)
		goto qeas_exit;

	info = (struct smb2_file_full_ea_info *)(
			le16_to_cpu(rsp->OutputBufferOffset) + (char *)rsp);
	rc = move_smb2_ea_to_cifs(ea_data, buf_size, info,
			le32_to_cpu(rsp->OutputBufferLength), ea_name);

 qeas_exit:
	free_rsp_buf(buftype, rsp_iov.iov_base);
	return rc;
}

static int
smb2_set_ea(const unsigned int xid, struct cifs_tcon *tcon,
	    const char *path, const char *ea_name, const void *ea_value,
	    const __u16 ea_value_len, const struct nls_table *nls_codepage,
	    struct cifs_sb_info *cifs_sb)
{
	struct smb2_compound_vars *vars;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	struct smb_rqst *rqst;
	struct kvec *rsp_iov;
	__le16 *utf16_path = NULL;
	int ea_name_len = strlen(ea_name);
	int flags = CIFS_CP_CREATE_CLOSE_OP;
	int len;
	int resp_buftype[3];
	struct cifs_open_parms oparms;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_fid fid;
	unsigned int size[1];
	void *data[1];
	struct smb2_file_full_ea_info *ea = NULL;
	struct smb2_query_info_rsp *rsp;
	int rc, used_len = 0;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	if (ea_name_len > 255)
		return -EINVAL;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	vars = kzalloc(sizeof(*vars), GFP_KERNEL);
	if (!vars) {
		rc = -ENOMEM;
		goto out_free_path;
	}
	rqst = vars->rqst;
	rsp_iov = vars->rsp_iov;

	if (ses->server->ops->query_all_EAs) {
		if (!ea_value) {
			rc = ses->server->ops->query_all_EAs(xid, tcon, path,
							     ea_name, NULL, 0,
							     cifs_sb);
			if (rc == -ENODATA)
				goto sea_exit;
		} else {
			/* If we are adding a attribute we should first check
			 * if there will be enough space available to store
			 * the new EA. If not we should not add it since we
			 * would not be able to even read the EAs back.
			 */
			rc = smb2_query_info_compound(xid, tcon, path,
				      FILE_READ_EA,
				      FILE_FULL_EA_INFORMATION,
				      SMB2_O_INFO_FILE,
				      CIFSMaxBufSize -
				      MAX_SMB2_CREATE_RESPONSE_SIZE -
				      MAX_SMB2_CLOSE_RESPONSE_SIZE,
				      &rsp_iov[1], &resp_buftype[1], cifs_sb);
			if (rc == 0) {
				rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
				used_len = le32_to_cpu(rsp->OutputBufferLength);
			}
			free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
			resp_buftype[1] = CIFS_NO_BUFFER;
			memset(&rsp_iov[1], 0, sizeof(rsp_iov[1]));
			rc = 0;

			/* Use a fudge factor of 256 bytes in case we collide
			 * with a different set_EAs command.
			 */
			if (CIFSMaxBufSize - MAX_SMB2_CREATE_RESPONSE_SIZE -
			   MAX_SMB2_CLOSE_RESPONSE_SIZE - 256 <
			   used_len + ea_name_len + ea_value_len + 1) {
				rc = -ENOSPC;
				goto sea_exit;
			}
		}
	}

	/* Open */
	rqst[0].rq_iov = vars->open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = path,
		.desired_access = FILE_WRITE_EA,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = &fid,
	};

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto sea_exit;
	smb2_set_next_command(tcon, &rqst[0]);


	/* Set Info */
	rqst[1].rq_iov = vars->si_iov;
	rqst[1].rq_nvec = 1;

	len = sizeof(*ea) + ea_name_len + ea_value_len + 1;
	ea = kzalloc(len, GFP_KERNEL);
	if (ea == NULL) {
		rc = -ENOMEM;
		goto sea_exit;
	}

	ea->ea_name_length = ea_name_len;
	ea->ea_value_length = cpu_to_le16(ea_value_len);
	memcpy(ea->ea_data, ea_name, ea_name_len + 1);
	memcpy(ea->ea_data + ea_name_len + 1, ea_value, ea_value_len);

	size[0] = len;
	data[0] = ea;

	rc = SMB2_set_info_init(tcon, server,
				&rqst[1], COMPOUND_FID,
				COMPOUND_FID, current->tgid,
				FILE_FULL_EA_INFORMATION,
				SMB2_O_INFO_FILE, 0, data, size);
	if (rc)
		goto sea_exit;
	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);

	/* Close */
	rqst[2].rq_iov = &vars->close_iov;
	rqst[2].rq_nvec = 1;
	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	if (rc)
		goto sea_exit;
	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, ses, server,
				flags, 3, rqst,
				resp_buftype, rsp_iov);
	/* no need to bump num_remote_opens because handle immediately closed */

 sea_exit:
	kfree(ea);
	SMB2_open_free(&rqst[0]);
	SMB2_set_info_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	kfree(vars);
out_free_path:
	kfree(utf16_path);
	return rc;
}
#endif

static bool
smb2_can_echo(struct TCP_Server_Info *server)
{
	return server->echoes;
}

static void
smb2_clear_stats(struct cifs_tcon *tcon)
{
	int i;

	for (i = 0; i < NUMBER_OF_SMB2_COMMANDS; i++) {
		atomic_set(&tcon->stats.smb2_stats.smb2_com_sent[i], 0);
		atomic_set(&tcon->stats.smb2_stats.smb2_com_failed[i], 0);
	}
}

static void
smb2_dump_share_caps(struct seq_file *m, struct cifs_tcon *tcon)
{
	seq_puts(m, "\n\tShare Capabilities:");
	if (tcon->capabilities & SMB2_SHARE_CAP_DFS)
		seq_puts(m, " DFS,");
	if (tcon->capabilities & SMB2_SHARE_CAP_CONTINUOUS_AVAILABILITY)
		seq_puts(m, " CONTINUOUS AVAILABILITY,");
	if (tcon->capabilities & SMB2_SHARE_CAP_SCALEOUT)
		seq_puts(m, " SCALEOUT,");
	if (tcon->capabilities & SMB2_SHARE_CAP_CLUSTER)
		seq_puts(m, " CLUSTER,");
	if (tcon->capabilities & SMB2_SHARE_CAP_ASYMMETRIC)
		seq_puts(m, " ASYMMETRIC,");
	if (tcon->capabilities == 0)
		seq_puts(m, " None");
	if (tcon->ss_flags & SSINFO_FLAGS_ALIGNED_DEVICE)
		seq_puts(m, " Aligned,");
	if (tcon->ss_flags & SSINFO_FLAGS_PARTITION_ALIGNED_ON_DEVICE)
		seq_puts(m, " Partition Aligned,");
	if (tcon->ss_flags & SSINFO_FLAGS_NO_SEEK_PENALTY)
		seq_puts(m, " SSD,");
	if (tcon->ss_flags & SSINFO_FLAGS_TRIM_ENABLED)
		seq_puts(m, " TRIM-support,");

	seq_printf(m, "\tShare Flags: 0x%x", tcon->share_flags);
	seq_printf(m, "\n\ttid: 0x%x", tcon->tid);
	if (tcon->perf_sector_size)
		seq_printf(m, "\tOptimal sector size: 0x%x",
			   tcon->perf_sector_size);
	seq_printf(m, "\tMaximal Access: 0x%x", tcon->maximal_access);
}

static void
smb2_print_stats(struct seq_file *m, struct cifs_tcon *tcon)
{
	atomic_t *sent = tcon->stats.smb2_stats.smb2_com_sent;
	atomic_t *failed = tcon->stats.smb2_stats.smb2_com_failed;

	/*
	 *  Can't display SMB2_NEGOTIATE, SESSION_SETUP, LOGOFF, CANCEL and ECHO
	 *  totals (requests sent) since those SMBs are per-session not per tcon
	 */
	seq_printf(m, "\nBytes read: %llu  Bytes written: %llu",
		   (long long)(tcon->bytes_read),
		   (long long)(tcon->bytes_written));
	seq_printf(m, "\nOpen files: %d total (local), %d open on server",
		   atomic_read(&tcon->num_local_opens),
		   atomic_read(&tcon->num_remote_opens));
	seq_printf(m, "\nTreeConnects: %d total %d failed",
		   atomic_read(&sent[SMB2_TREE_CONNECT_HE]),
		   atomic_read(&failed[SMB2_TREE_CONNECT_HE]));
	seq_printf(m, "\nTreeDisconnects: %d total %d failed",
		   atomic_read(&sent[SMB2_TREE_DISCONNECT_HE]),
		   atomic_read(&failed[SMB2_TREE_DISCONNECT_HE]));
	seq_printf(m, "\nCreates: %d total %d failed",
		   atomic_read(&sent[SMB2_CREATE_HE]),
		   atomic_read(&failed[SMB2_CREATE_HE]));
	seq_printf(m, "\nCloses: %d total %d failed",
		   atomic_read(&sent[SMB2_CLOSE_HE]),
		   atomic_read(&failed[SMB2_CLOSE_HE]));
	seq_printf(m, "\nFlushes: %d total %d failed",
		   atomic_read(&sent[SMB2_FLUSH_HE]),
		   atomic_read(&failed[SMB2_FLUSH_HE]));
	seq_printf(m, "\nReads: %d total %d failed",
		   atomic_read(&sent[SMB2_READ_HE]),
		   atomic_read(&failed[SMB2_READ_HE]));
	seq_printf(m, "\nWrites: %d total %d failed",
		   atomic_read(&sent[SMB2_WRITE_HE]),
		   atomic_read(&failed[SMB2_WRITE_HE]));
	seq_printf(m, "\nLocks: %d total %d failed",
		   atomic_read(&sent[SMB2_LOCK_HE]),
		   atomic_read(&failed[SMB2_LOCK_HE]));
	seq_printf(m, "\nIOCTLs: %d total %d failed",
		   atomic_read(&sent[SMB2_IOCTL_HE]),
		   atomic_read(&failed[SMB2_IOCTL_HE]));
	seq_printf(m, "\nQueryDirectories: %d total %d failed",
		   atomic_read(&sent[SMB2_QUERY_DIRECTORY_HE]),
		   atomic_read(&failed[SMB2_QUERY_DIRECTORY_HE]));
	seq_printf(m, "\nChangeNotifies: %d total %d failed",
		   atomic_read(&sent[SMB2_CHANGE_NOTIFY_HE]),
		   atomic_read(&failed[SMB2_CHANGE_NOTIFY_HE]));
	seq_printf(m, "\nQueryInfos: %d total %d failed",
		   atomic_read(&sent[SMB2_QUERY_INFO_HE]),
		   atomic_read(&failed[SMB2_QUERY_INFO_HE]));
	seq_printf(m, "\nSetInfos: %d total %d failed",
		   atomic_read(&sent[SMB2_SET_INFO_HE]),
		   atomic_read(&failed[SMB2_SET_INFO_HE]));
	seq_printf(m, "\nOplockBreaks: %d sent %d failed",
		   atomic_read(&sent[SMB2_OPLOCK_BREAK_HE]),
		   atomic_read(&failed[SMB2_OPLOCK_BREAK_HE]));
}

static void
smb2_set_fid(struct cifsFileInfo *cfile, struct cifs_fid *fid, __u32 oplock)
{
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct TCP_Server_Info *server = tlink_tcon(cfile->tlink)->ses->server;

	cfile->fid.persistent_fid = fid->persistent_fid;
	cfile->fid.volatile_fid = fid->volatile_fid;
	cfile->fid.access = fid->access;
#ifdef CONFIG_CIFS_DEBUG2
	cfile->fid.mid = fid->mid;
#endif /* CIFS_DEBUG2 */
	server->ops->set_oplock_level(cinode, oplock, fid->epoch,
				      &fid->purge_cache);
	cinode->can_cache_brlcks = CIFS_CACHE_WRITE(cinode);
	memcpy(cfile->fid.create_guid, fid->create_guid, 16);
}

static void
smb2_close_file(const unsigned int xid, struct cifs_tcon *tcon,
		struct cifs_fid *fid)
{
	SMB2_close(xid, tcon, fid->persistent_fid, fid->volatile_fid);
}

static void
smb2_close_getattr(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifsFileInfo *cfile)
{
	struct smb2_file_network_open_info file_inf;
	struct inode *inode;
	int rc;

	rc = __SMB2_close(xid, tcon, cfile->fid.persistent_fid,
		   cfile->fid.volatile_fid, &file_inf);
	if (rc)
		return;

	inode = d_inode(cfile->dentry);

	spin_lock(&inode->i_lock);
	CIFS_I(inode)->time = jiffies;

	/* Creation time should not need to be updated on close */
	if (file_inf.LastWriteTime)
		inode_set_mtime_to_ts(inode,
				      cifs_NTtimeToUnix(file_inf.LastWriteTime));
	if (file_inf.ChangeTime)
		inode_set_ctime_to_ts(inode,
				      cifs_NTtimeToUnix(file_inf.ChangeTime));
	if (file_inf.LastAccessTime)
		inode_set_atime_to_ts(inode,
				      cifs_NTtimeToUnix(file_inf.LastAccessTime));

	/*
	 * i_blocks is not related to (i_size / i_blksize),
	 * but instead 512 byte (2**9) size is required for
	 * calculating num blocks.
	 */
	if (le64_to_cpu(file_inf.AllocationSize) > 4096)
		inode->i_blocks =
			(512 - 1 + le64_to_cpu(file_inf.AllocationSize)) >> 9;

	/* End of file and Attributes should not have to be updated on close */
	spin_unlock(&inode->i_lock);
}

static int
SMB2_request_res_key(const unsigned int xid, struct cifs_tcon *tcon,
		     u64 persistent_fid, u64 volatile_fid,
		     struct copychunk_ioctl *pcchunk)
{
	int rc;
	unsigned int ret_data_len;
	struct resume_key_req *res_key;

	rc = SMB2_ioctl(xid, tcon, persistent_fid, volatile_fid,
			FSCTL_SRV_REQUEST_RESUME_KEY, NULL, 0 /* no input */,
			CIFSMaxBufSize, (char **)&res_key, &ret_data_len);

	if (rc == -EOPNOTSUPP) {
		pr_warn_once("Server share %s does not support copy range\n", tcon->tree_name);
		goto req_res_key_exit;
	} else if (rc) {
		cifs_tcon_dbg(VFS, "refcpy ioctl error %d getting resume key\n", rc);
		goto req_res_key_exit;
	}
	if (ret_data_len < sizeof(struct resume_key_req)) {
		cifs_tcon_dbg(VFS, "Invalid refcopy resume key length\n");
		rc = -EINVAL;
		goto req_res_key_exit;
	}
	memcpy(pcchunk->SourceKey, res_key->ResumeKey, COPY_CHUNK_RES_KEY_SIZE);

req_res_key_exit:
	kfree(res_key);
	return rc;
}

static int
smb2_ioctl_query_info(const unsigned int xid,
		      struct cifs_tcon *tcon,
		      struct cifs_sb_info *cifs_sb,
		      __le16 *path, int is_dir,
		      unsigned long p)
{
	struct smb2_compound_vars *vars;
	struct smb_rqst *rqst;
	struct kvec *rsp_iov;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	char __user *arg = (char __user *)p;
	struct smb_query_info qi;
	struct smb_query_info __user *pqi;
	int rc = 0;
	int flags = CIFS_CP_CREATE_CLOSE_OP;
	struct smb2_query_info_rsp *qi_rsp = NULL;
	struct smb2_ioctl_rsp *io_rsp = NULL;
	void *buffer = NULL;
	int resp_buftype[3];
	struct cifs_open_parms oparms;
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_fid fid;
	unsigned int size[2];
	void *data[2];
	int create_options = is_dir ? CREATE_NOT_FILE : CREATE_NOT_DIR;
	void (*free_req1_func)(struct smb_rqst *r);

	vars = kzalloc(sizeof(*vars), GFP_ATOMIC);
	if (vars == NULL)
		return -ENOMEM;
	rqst = &vars->rqst[0];
	rsp_iov = &vars->rsp_iov[0];

	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;

	if (copy_from_user(&qi, arg, sizeof(struct smb_query_info))) {
		rc = -EFAULT;
		goto free_vars;
	}
	if (qi.output_buffer_length > 1024) {
		rc = -EINVAL;
		goto free_vars;
	}

	if (!ses || !server) {
		rc = -EIO;
		goto free_vars;
	}

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	if (qi.output_buffer_length) {
		buffer = memdup_user(arg + sizeof(struct smb_query_info), qi.output_buffer_length);
		if (IS_ERR(buffer)) {
			rc = PTR_ERR(buffer);
			goto free_vars;
		}
	}

	/* Open */
	rqst[0].rq_iov = &vars->open_iov[0];
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, create_options),
		.fid = &fid,
	};

	if (qi.flags & PASSTHRU_FSCTL) {
		switch (qi.info_type & FSCTL_DEVICE_ACCESS_MASK) {
		case FSCTL_DEVICE_ACCESS_FILE_READ_WRITE_ACCESS:
			oparms.desired_access = FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
			break;
		case FSCTL_DEVICE_ACCESS_FILE_ANY_ACCESS:
			oparms.desired_access = GENERIC_ALL;
			break;
		case FSCTL_DEVICE_ACCESS_FILE_READ_ACCESS:
			oparms.desired_access = GENERIC_READ;
			break;
		case FSCTL_DEVICE_ACCESS_FILE_WRITE_ACCESS:
			oparms.desired_access = GENERIC_WRITE;
			break;
		}
	} else if (qi.flags & PASSTHRU_SET_INFO) {
		oparms.desired_access = GENERIC_WRITE;
	} else {
		oparms.desired_access = FILE_READ_ATTRIBUTES | READ_CONTROL;
	}

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, path);
	if (rc)
		goto free_output_buffer;
	smb2_set_next_command(tcon, &rqst[0]);

	/* Query */
	if (qi.flags & PASSTHRU_FSCTL) {
		/* Can eventually relax perm check since server enforces too */
		if (!capable(CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto free_open_req;
		}
		rqst[1].rq_iov = &vars->io_iov[0];
		rqst[1].rq_nvec = SMB2_IOCTL_IOV_SIZE;

		rc = SMB2_ioctl_init(tcon, server, &rqst[1], COMPOUND_FID, COMPOUND_FID,
				     qi.info_type, buffer, qi.output_buffer_length,
				     CIFSMaxBufSize - MAX_SMB2_CREATE_RESPONSE_SIZE -
				     MAX_SMB2_CLOSE_RESPONSE_SIZE);
		free_req1_func = SMB2_ioctl_free;
	} else if (qi.flags == PASSTHRU_SET_INFO) {
		/* Can eventually relax perm check since server enforces too */
		if (!capable(CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto free_open_req;
		}
		if (qi.output_buffer_length < 8) {
			rc = -EINVAL;
			goto free_open_req;
		}
		rqst[1].rq_iov = vars->si_iov;
		rqst[1].rq_nvec = 1;

		/* MS-FSCC 2.4.13 FileEndOfFileInformation */
		size[0] = 8;
		data[0] = buffer;

		rc = SMB2_set_info_init(tcon, server, &rqst[1], COMPOUND_FID, COMPOUND_FID,
					current->tgid, FILE_END_OF_FILE_INFORMATION,
					SMB2_O_INFO_FILE, 0, data, size);
		free_req1_func = SMB2_set_info_free;
	} else if (qi.flags == PASSTHRU_QUERY_INFO) {
		rqst[1].rq_iov = &vars->qi_iov;
		rqst[1].rq_nvec = 1;

		rc = SMB2_query_info_init(tcon, server,
				  &rqst[1], COMPOUND_FID,
				  COMPOUND_FID, qi.file_info_class,
				  qi.info_type, qi.additional_information,
				  qi.input_buffer_length,
				  qi.output_buffer_length, buffer);
		free_req1_func = SMB2_query_info_free;
	} else { /* unknown flags */
		cifs_tcon_dbg(VFS, "Invalid passthru query flags: 0x%x\n",
			      qi.flags);
		rc = -EINVAL;
	}

	if (rc)
		goto free_open_req;
	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);

	/* Close */
	rqst[2].rq_iov = &vars->close_iov;
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	if (rc)
		goto free_req_1;
	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, ses, server,
				flags, 3, rqst,
				resp_buftype, rsp_iov);
	if (rc)
		goto out;

	/* No need to bump num_remote_opens since handle immediately closed */
	if (qi.flags & PASSTHRU_FSCTL) {
		pqi = (struct smb_query_info __user *)arg;
		io_rsp = (struct smb2_ioctl_rsp *)rsp_iov[1].iov_base;
		if (le32_to_cpu(io_rsp->OutputCount) < qi.input_buffer_length)
			qi.input_buffer_length = le32_to_cpu(io_rsp->OutputCount);
		if (qi.input_buffer_length > 0 &&
		    le32_to_cpu(io_rsp->OutputOffset) + qi.input_buffer_length
		    > rsp_iov[1].iov_len) {
			rc = -EFAULT;
			goto out;
		}

		if (copy_to_user(&pqi->input_buffer_length,
				 &qi.input_buffer_length,
				 sizeof(qi.input_buffer_length))) {
			rc = -EFAULT;
			goto out;
		}

		if (copy_to_user((void __user *)pqi + sizeof(struct smb_query_info),
				 (const void *)io_rsp + le32_to_cpu(io_rsp->OutputOffset),
				 qi.input_buffer_length))
			rc = -EFAULT;
	} else {
		pqi = (struct smb_query_info __user *)arg;
		qi_rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
		if (le32_to_cpu(qi_rsp->OutputBufferLength) < qi.input_buffer_length)
			qi.input_buffer_length = le32_to_cpu(qi_rsp->OutputBufferLength);
		if (copy_to_user(&pqi->input_buffer_length,
				 &qi.input_buffer_length,
				 sizeof(qi.input_buffer_length))) {
			rc = -EFAULT;
			goto out;
		}

		if (copy_to_user(pqi + 1, qi_rsp->Buffer,
				 qi.input_buffer_length))
			rc = -EFAULT;
	}

out:
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	SMB2_close_free(&rqst[2]);
free_req_1:
	free_req1_func(&rqst[1]);
free_open_req:
	SMB2_open_free(&rqst[0]);
free_output_buffer:
	kfree(buffer);
free_vars:
	kfree(vars);
	return rc;
}

static ssize_t
smb2_copychunk_range(const unsigned int xid,
			struct cifsFileInfo *srcfile,
			struct cifsFileInfo *trgtfile, u64 src_off,
			u64 len, u64 dest_off)
{
	int rc;
	unsigned int ret_data_len;
	struct copychunk_ioctl *pcchunk;
	struct copychunk_ioctl_rsp *retbuf = NULL;
	struct cifs_tcon *tcon;
	int chunks_copied = 0;
	bool chunk_sizes_updated = false;
	ssize_t bytes_written, total_bytes_written = 0;

	pcchunk = kmalloc(sizeof(struct copychunk_ioctl), GFP_KERNEL);
	if (pcchunk == NULL)
		return -ENOMEM;

	cifs_dbg(FYI, "%s: about to call request res key\n", __func__);
	/* Request a key from the server to identify the source of the copy */
	rc = SMB2_request_res_key(xid, tlink_tcon(srcfile->tlink),
				srcfile->fid.persistent_fid,
				srcfile->fid.volatile_fid, pcchunk);

	/* Note: request_res_key sets res_key null only if rc !=0 */
	if (rc)
		goto cchunk_out;

	/* For now array only one chunk long, will make more flexible later */
	pcchunk->ChunkCount = cpu_to_le32(1);
	pcchunk->Reserved = 0;
	pcchunk->Reserved2 = 0;

	tcon = tlink_tcon(trgtfile->tlink);

	while (len > 0) {
		pcchunk->SourceOffset = cpu_to_le64(src_off);
		pcchunk->TargetOffset = cpu_to_le64(dest_off);
		pcchunk->Length =
			cpu_to_le32(min_t(u64, len, tcon->max_bytes_chunk));

		/* Request server copy to target from src identified by key */
		kfree(retbuf);
		retbuf = NULL;
		rc = SMB2_ioctl(xid, tcon, trgtfile->fid.persistent_fid,
			trgtfile->fid.volatile_fid, FSCTL_SRV_COPYCHUNK_WRITE,
			(char *)pcchunk, sizeof(struct copychunk_ioctl),
			CIFSMaxBufSize, (char **)&retbuf, &ret_data_len);
		if (rc == 0) {
			if (ret_data_len !=
					sizeof(struct copychunk_ioctl_rsp)) {
				cifs_tcon_dbg(VFS, "Invalid cchunk response size\n");
				rc = -EIO;
				goto cchunk_out;
			}
			if (retbuf->TotalBytesWritten == 0) {
				cifs_dbg(FYI, "no bytes copied\n");
				rc = -EIO;
				goto cchunk_out;
			}
			/*
			 * Check if server claimed to write more than we asked
			 */
			if (le32_to_cpu(retbuf->TotalBytesWritten) >
			    le32_to_cpu(pcchunk->Length)) {
				cifs_tcon_dbg(VFS, "Invalid copy chunk response\n");
				rc = -EIO;
				goto cchunk_out;
			}
			if (le32_to_cpu(retbuf->ChunksWritten) != 1) {
				cifs_tcon_dbg(VFS, "Invalid num chunks written\n");
				rc = -EIO;
				goto cchunk_out;
			}
			chunks_copied++;

			bytes_written = le32_to_cpu(retbuf->TotalBytesWritten);
			src_off += bytes_written;
			dest_off += bytes_written;
			len -= bytes_written;
			total_bytes_written += bytes_written;

			cifs_dbg(FYI, "Chunks %d PartialChunk %d Total %zu\n",
				le32_to_cpu(retbuf->ChunksWritten),
				le32_to_cpu(retbuf->ChunkBytesWritten),
				bytes_written);
		} else if (rc == -EINVAL) {
			if (ret_data_len != sizeof(struct copychunk_ioctl_rsp))
				goto cchunk_out;

			cifs_dbg(FYI, "MaxChunks %d BytesChunk %d MaxCopy %d\n",
				le32_to_cpu(retbuf->ChunksWritten),
				le32_to_cpu(retbuf->ChunkBytesWritten),
				le32_to_cpu(retbuf->TotalBytesWritten));

			/*
			 * Check if this is the first request using these sizes,
			 * (ie check if copy succeed once with original sizes
			 * and check if the server gave us different sizes after
			 * we already updated max sizes on previous request).
			 * if not then why is the server returning an error now
			 */
			if ((chunks_copied != 0) || chunk_sizes_updated)
				goto cchunk_out;

			/* Check that server is not asking us to grow size */
			if (le32_to_cpu(retbuf->ChunkBytesWritten) <
					tcon->max_bytes_chunk)
				tcon->max_bytes_chunk =
					le32_to_cpu(retbuf->ChunkBytesWritten);
			else
				goto cchunk_out; /* server gave us bogus size */

			/* No need to change MaxChunks since already set to 1 */
			chunk_sizes_updated = true;
		} else
			goto cchunk_out;
	}

cchunk_out:
	kfree(pcchunk);
	kfree(retbuf);
	if (rc)
		return rc;
	else
		return total_bytes_written;
}

static int
smb2_flush_file(const unsigned int xid, struct cifs_tcon *tcon,
		struct cifs_fid *fid)
{
	return SMB2_flush(xid, tcon, fid->persistent_fid, fid->volatile_fid);
}

static unsigned int
smb2_read_data_offset(char *buf)
{
	struct smb2_read_rsp *rsp = (struct smb2_read_rsp *)buf;

	return rsp->DataOffset;
}

static unsigned int
smb2_read_data_length(char *buf, bool in_remaining)
{
	struct smb2_read_rsp *rsp = (struct smb2_read_rsp *)buf;

	if (in_remaining)
		return le32_to_cpu(rsp->DataRemaining);

	return le32_to_cpu(rsp->DataLength);
}


static int
smb2_sync_read(const unsigned int xid, struct cifs_fid *pfid,
	       struct cifs_io_parms *parms, unsigned int *bytes_read,
	       char **buf, int *buf_type)
{
	parms->persistent_fid = pfid->persistent_fid;
	parms->volatile_fid = pfid->volatile_fid;
	return SMB2_read(xid, parms, bytes_read, buf, buf_type);
}

static int
smb2_sync_write(const unsigned int xid, struct cifs_fid *pfid,
		struct cifs_io_parms *parms, unsigned int *written,
		struct kvec *iov, unsigned long nr_segs)
{

	parms->persistent_fid = pfid->persistent_fid;
	parms->volatile_fid = pfid->volatile_fid;
	return SMB2_write(xid, parms, written, iov, nr_segs);
}

/* Set or clear the SPARSE_FILE attribute based on value passed in setsparse */
static bool smb2_set_sparse(const unsigned int xid, struct cifs_tcon *tcon,
		struct cifsFileInfo *cfile, struct inode *inode, __u8 setsparse)
{
	struct cifsInodeInfo *cifsi;
	int rc;

	cifsi = CIFS_I(inode);

	/* if file already sparse don't bother setting sparse again */
	if ((cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE) && setsparse)
		return true; /* already sparse */

	if (!(cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE) && !setsparse)
		return true; /* already not sparse */

	/*
	 * Can't check for sparse support on share the usual way via the
	 * FS attribute info (FILE_SUPPORTS_SPARSE_FILES) on the share
	 * since Samba server doesn't set the flag on the share, yet
	 * supports the set sparse FSCTL and returns sparse correctly
	 * in the file attributes. If we fail setting sparse though we
	 * mark that server does not support sparse files for this share
	 * to avoid repeatedly sending the unsupported fsctl to server
	 * if the file is repeatedly extended.
	 */
	if (tcon->broken_sparse_sup)
		return false;

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid, FSCTL_SET_SPARSE,
			&setsparse, 1, CIFSMaxBufSize, NULL, NULL);
	if (rc) {
		tcon->broken_sparse_sup = true;
		cifs_dbg(FYI, "set sparse rc = %d\n", rc);
		return false;
	}

	if (setsparse)
		cifsi->cifsAttrs |= FILE_ATTRIBUTE_SPARSE_FILE;
	else
		cifsi->cifsAttrs &= (~FILE_ATTRIBUTE_SPARSE_FILE);

	return true;
}

static int
smb2_set_file_size(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifsFileInfo *cfile, __u64 size, bool set_alloc)
{
	__le64 eof = cpu_to_le64(size);
	struct inode *inode;

	/*
	 * If extending file more than one page make sparse. Many Linux fs
	 * make files sparse by default when extending via ftruncate
	 */
	inode = d_inode(cfile->dentry);

	if (!set_alloc && (size > inode->i_size + 8192)) {
		__u8 set_sparse = 1;

		/* whether set sparse succeeds or not, extend the file */
		smb2_set_sparse(xid, tcon, cfile, inode, set_sparse);
	}

	return SMB2_set_eof(xid, tcon, cfile->fid.persistent_fid,
			    cfile->fid.volatile_fid, cfile->pid, &eof);
}

static int
smb2_duplicate_extents(const unsigned int xid,
			struct cifsFileInfo *srcfile,
			struct cifsFileInfo *trgtfile, u64 src_off,
			u64 len, u64 dest_off)
{
	int rc;
	unsigned int ret_data_len;
	struct inode *inode;
	struct duplicate_extents_to_file dup_ext_buf;
	struct cifs_tcon *tcon = tlink_tcon(trgtfile->tlink);

	/* server fileays advertise duplicate extent support with this flag */
	if ((le32_to_cpu(tcon->fsAttrInfo.Attributes) &
	     FILE_SUPPORTS_BLOCK_REFCOUNTING) == 0)
		return -EOPNOTSUPP;

	dup_ext_buf.VolatileFileHandle = srcfile->fid.volatile_fid;
	dup_ext_buf.PersistentFileHandle = srcfile->fid.persistent_fid;
	dup_ext_buf.SourceFileOffset = cpu_to_le64(src_off);
	dup_ext_buf.TargetFileOffset = cpu_to_le64(dest_off);
	dup_ext_buf.ByteCount = cpu_to_le64(len);
	cifs_dbg(FYI, "Duplicate extents: src off %lld dst off %lld len %lld\n",
		src_off, dest_off, len);

	inode = d_inode(trgtfile->dentry);
	if (inode->i_size < dest_off + len) {
		rc = smb2_set_file_size(xid, tcon, trgtfile, dest_off + len, false);
		if (rc)
			goto duplicate_extents_out;

		/*
		 * Although also could set plausible allocation size (i_blocks)
		 * here in addition to setting the file size, in reflink
		 * it is likely that the target file is sparse. Its allocation
		 * size will be queried on next revalidate, but it is important
		 * to make sure that file's cached size is updated immediately
		 */
		cifs_setsize(inode, dest_off + len);
	}
	rc = SMB2_ioctl(xid, tcon, trgtfile->fid.persistent_fid,
			trgtfile->fid.volatile_fid,
			FSCTL_DUPLICATE_EXTENTS_TO_FILE,
			(char *)&dup_ext_buf,
			sizeof(struct duplicate_extents_to_file),
			CIFSMaxBufSize, NULL,
			&ret_data_len);

	if (ret_data_len > 0)
		cifs_dbg(FYI, "Non-zero response length in duplicate extents\n");

duplicate_extents_out:
	return rc;
}

static int
smb2_set_compression(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifsFileInfo *cfile)
{
	return SMB2_set_compression(xid, tcon, cfile->fid.persistent_fid,
			    cfile->fid.volatile_fid);
}

static int
smb3_set_integrity(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifsFileInfo *cfile)
{
	struct fsctl_set_integrity_information_req integr_info;
	unsigned int ret_data_len;

	integr_info.ChecksumAlgorithm = cpu_to_le16(CHECKSUM_TYPE_UNCHANGED);
	integr_info.Flags = 0;
	integr_info.Reserved = 0;

	return SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid,
			FSCTL_SET_INTEGRITY_INFORMATION,
			(char *)&integr_info,
			sizeof(struct fsctl_set_integrity_information_req),
			CIFSMaxBufSize, NULL,
			&ret_data_len);

}

/* GMT Token is @GMT-YYYY.MM.DD-HH.MM.SS Unicode which is 48 bytes + null */
#define GMT_TOKEN_SIZE 50

#define MIN_SNAPSHOT_ARRAY_SIZE 16 /* See MS-SMB2 section 3.3.5.15.1 */

/*
 * Input buffer contains (empty) struct smb_snapshot array with size filled in
 * For output see struct SRV_SNAPSHOT_ARRAY in MS-SMB2 section 2.2.32.2
 */
static int
smb3_enum_snapshots(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifsFileInfo *cfile, void __user *ioc_buf)
{
	char *retbuf = NULL;
	unsigned int ret_data_len = 0;
	int rc;
	u32 max_response_size;
	struct smb_snapshot_array snapshot_in;

	/*
	 * On the first query to enumerate the list of snapshots available
	 * for this volume the buffer begins with 0 (number of snapshots
	 * which can be returned is zero since at that point we do not know
	 * how big the buffer needs to be). On the second query,
	 * it (ret_data_len) is set to number of snapshots so we can
	 * know to set the maximum response size larger (see below).
	 */
	if (get_user(ret_data_len, (unsigned int __user *)ioc_buf))
		return -EFAULT;

	/*
	 * Note that for snapshot queries that servers like Azure expect that
	 * the first query be minimal size (and just used to get the number/size
	 * of previous versions) so response size must be specified as EXACTLY
	 * sizeof(struct snapshot_array) which is 16 when rounded up to multiple
	 * of eight bytes.
	 */
	if (ret_data_len == 0)
		max_response_size = MIN_SNAPSHOT_ARRAY_SIZE;
	else
		max_response_size = CIFSMaxBufSize;

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid,
			FSCTL_SRV_ENUMERATE_SNAPSHOTS,
			NULL, 0 /* no input data */, max_response_size,
			(char **)&retbuf,
			&ret_data_len);
	cifs_dbg(FYI, "enum snaphots ioctl returned %d and ret buflen is %d\n",
			rc, ret_data_len);
	if (rc)
		return rc;

	if (ret_data_len && (ioc_buf != NULL) && (retbuf != NULL)) {
		/* Fixup buffer */
		if (copy_from_user(&snapshot_in, ioc_buf,
		    sizeof(struct smb_snapshot_array))) {
			rc = -EFAULT;
			kfree(retbuf);
			return rc;
		}

		/*
		 * Check for min size, ie not large enough to fit even one GMT
		 * token (snapshot).  On the first ioctl some users may pass in
		 * smaller size (or zero) to simply get the size of the array
		 * so the user space caller can allocate sufficient memory
		 * and retry the ioctl again with larger array size sufficient
		 * to hold all of the snapshot GMT tokens on the second try.
		 */
		if (snapshot_in.snapshot_array_size < GMT_TOKEN_SIZE)
			ret_data_len = sizeof(struct smb_snapshot_array);

		/*
		 * We return struct SRV_SNAPSHOT_ARRAY, followed by
		 * the snapshot array (of 50 byte GMT tokens) each
		 * representing an available previous version of the data
		 */
		if (ret_data_len > (snapshot_in.snapshot_array_size +
					sizeof(struct smb_snapshot_array)))
			ret_data_len = snapshot_in.snapshot_array_size +
					sizeof(struct smb_snapshot_array);

		if (copy_to_user(ioc_buf, retbuf, ret_data_len))
			rc = -EFAULT;
	}

	kfree(retbuf);
	return rc;
}



static int
smb3_notify(const unsigned int xid, struct file *pfile,
	    void __user *ioc_buf, bool return_changes)
{
	struct smb3_notify_info notify;
	struct smb3_notify_info __user *pnotify_buf;
	struct dentry *dentry = pfile->f_path.dentry;
	struct inode *inode = file_inode(pfile);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct cifs_tcon *tcon;
	const unsigned char *path;
	char *returned_ioctl_info = NULL;
	void *page = alloc_dentry_path();
	__le16 *utf16_path = NULL;
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	int rc = 0;
	__u32 ret_len = 0;

	path = build_path_from_dentry(dentry, page);
	if (IS_ERR(path)) {
		rc = PTR_ERR(path);
		goto notify_exit;
	}

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (utf16_path == NULL) {
		rc = -ENOMEM;
		goto notify_exit;
	}

	if (return_changes) {
		if (copy_from_user(&notify, ioc_buf, sizeof(struct smb3_notify_info))) {
			rc = -EFAULT;
			goto notify_exit;
		}
	} else {
		if (copy_from_user(&notify, ioc_buf, sizeof(struct smb3_notify))) {
			rc = -EFAULT;
			goto notify_exit;
		}
		notify.data_len = 0;
	}

	tcon = cifs_sb_master_tcon(cifs_sb);
	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = path,
		.desired_access = FILE_READ_ATTRIBUTES | FILE_READ_DATA,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = &fid,
	};

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL,
		       NULL);
	if (rc)
		goto notify_exit;

	rc = SMB2_change_notify(xid, tcon, fid.persistent_fid, fid.volatile_fid,
				notify.watch_tree, notify.completion_filter,
				notify.data_len, &returned_ioctl_info, &ret_len);

	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);

	cifs_dbg(FYI, "change notify for path %s rc %d\n", path, rc);
	if (return_changes && (ret_len > 0) && (notify.data_len > 0)) {
		if (ret_len > notify.data_len)
			ret_len = notify.data_len;
		pnotify_buf = (struct smb3_notify_info __user *)ioc_buf;
		if (copy_to_user(pnotify_buf->notify_data, returned_ioctl_info, ret_len))
			rc = -EFAULT;
		else if (copy_to_user(&pnotify_buf->data_len, &ret_len, sizeof(ret_len)))
			rc = -EFAULT;
	}
	kfree(returned_ioctl_info);
notify_exit:
	free_dentry_path(page);
	kfree(utf16_path);
	return rc;
}

static int
smb2_query_dir_first(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *path, struct cifs_sb_info *cifs_sb,
		     struct cifs_fid *fid, __u16 search_flags,
		     struct cifs_search_info *srch_inf)
{
	__le16 *utf16_path;
	struct smb_rqst rqst[2];
	struct kvec rsp_iov[2];
	int resp_buftype[2];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec qd_iov[SMB2_QUERY_DIRECTORY_IOV_SIZE];
	int rc, flags = 0;
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct smb2_query_directory_rsp *qd_rsp = NULL;
	struct smb2_create_rsp *op_rsp = NULL;
	struct TCP_Server_Info *server = cifs_pick_channel(tcon->ses);
	int retry_count = 0;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	/* Open */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = path,
		.desired_access = FILE_READ_ATTRIBUTES | FILE_READ_DATA,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = fid,
	};

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto qdf_free;
	smb2_set_next_command(tcon, &rqst[0]);

	/* Query directory */
	srch_inf->entries_in_buffer = 0;
	srch_inf->index_of_last_entry = 2;

	memset(&qd_iov, 0, sizeof(qd_iov));
	rqst[1].rq_iov = qd_iov;
	rqst[1].rq_nvec = SMB2_QUERY_DIRECTORY_IOV_SIZE;

	rc = SMB2_query_directory_init(xid, tcon, server,
				       &rqst[1],
				       COMPOUND_FID, COMPOUND_FID,
				       0, srch_inf->info_level);
	if (rc)
		goto qdf_free;

	smb2_set_related(&rqst[1]);

again:
	rc = compound_send_recv(xid, tcon->ses, server,
				flags, 2, rqst,
				resp_buftype, rsp_iov);

	if (rc == -EAGAIN && retry_count++ < 10)
		goto again;

	/* If the open failed there is nothing to do */
	op_rsp = (struct smb2_create_rsp *)rsp_iov[0].iov_base;
	if (op_rsp == NULL || op_rsp->hdr.Status != STATUS_SUCCESS) {
		cifs_dbg(FYI, "query_dir_first: open failed rc=%d\n", rc);
		goto qdf_free;
	}
	fid->persistent_fid = op_rsp->PersistentFileId;
	fid->volatile_fid = op_rsp->VolatileFileId;

	/* Anything else than ENODATA means a genuine error */
	if (rc && rc != -ENODATA) {
		SMB2_close(xid, tcon, fid->persistent_fid, fid->volatile_fid);
		cifs_dbg(FYI, "query_dir_first: query directory failed rc=%d\n", rc);
		trace_smb3_query_dir_err(xid, fid->persistent_fid,
					 tcon->tid, tcon->ses->Suid, 0, 0, rc);
		goto qdf_free;
	}

	atomic_inc(&tcon->num_remote_opens);

	qd_rsp = (struct smb2_query_directory_rsp *)rsp_iov[1].iov_base;
	if (qd_rsp->hdr.Status == STATUS_NO_MORE_FILES) {
		trace_smb3_query_dir_done(xid, fid->persistent_fid,
					  tcon->tid, tcon->ses->Suid, 0, 0);
		srch_inf->endOfSearch = true;
		rc = 0;
		goto qdf_free;
	}

	rc = smb2_parse_query_directory(tcon, &rsp_iov[1], resp_buftype[1],
					srch_inf);
	if (rc) {
		trace_smb3_query_dir_err(xid, fid->persistent_fid, tcon->tid,
			tcon->ses->Suid, 0, 0, rc);
		goto qdf_free;
	}
	resp_buftype[1] = CIFS_NO_BUFFER;

	trace_smb3_query_dir_done(xid, fid->persistent_fid, tcon->tid,
			tcon->ses->Suid, 0, srch_inf->entries_in_buffer);

 qdf_free:
	kfree(utf16_path);
	SMB2_open_free(&rqst[0]);
	SMB2_query_directory_free(&rqst[1]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	return rc;
}

static int
smb2_query_dir_next(const unsigned int xid, struct cifs_tcon *tcon,
		    struct cifs_fid *fid, __u16 search_flags,
		    struct cifs_search_info *srch_inf)
{
	return SMB2_query_directory(xid, tcon, fid->persistent_fid,
				    fid->volatile_fid, 0, srch_inf);
}

static int
smb2_close_dir(const unsigned int xid, struct cifs_tcon *tcon,
	       struct cifs_fid *fid)
{
	return SMB2_close(xid, tcon, fid->persistent_fid, fid->volatile_fid);
}

/*
 * If we negotiate SMB2 protocol and get STATUS_PENDING - update
 * the number of credits and return true. Otherwise - return false.
 */
static bool
smb2_is_status_pending(char *buf, struct TCP_Server_Info *server)
{
	struct smb2_hdr *shdr = (struct smb2_hdr *)buf;
	int scredits, in_flight;

	if (shdr->Status != STATUS_PENDING)
		return false;

	if (shdr->CreditRequest) {
		spin_lock(&server->req_lock);
		server->credits += le16_to_cpu(shdr->CreditRequest);
		scredits = server->credits;
		in_flight = server->in_flight;
		spin_unlock(&server->req_lock);
		wake_up(&server->request_q);

		trace_smb3_pend_credits(server->CurrentMid,
				server->conn_id, server->hostname, scredits,
				le16_to_cpu(shdr->CreditRequest), in_flight);
		cifs_dbg(FYI, "%s: status pending add %u credits total=%d\n",
				__func__, le16_to_cpu(shdr->CreditRequest), scredits);
	}

	return true;
}

static bool
smb2_is_session_expired(char *buf)
{
	struct smb2_hdr *shdr = (struct smb2_hdr *)buf;

	if (shdr->Status != STATUS_NETWORK_SESSION_EXPIRED &&
	    shdr->Status != STATUS_USER_SESSION_DELETED)
		return false;

	trace_smb3_ses_expired(le32_to_cpu(shdr->Id.SyncId.TreeId),
			       le64_to_cpu(shdr->SessionId),
			       le16_to_cpu(shdr->Command),
			       le64_to_cpu(shdr->MessageId));
	cifs_dbg(FYI, "Session expired or deleted\n");

	return true;
}

static bool
smb2_is_status_io_timeout(char *buf)
{
	struct smb2_hdr *shdr = (struct smb2_hdr *)buf;

	if (shdr->Status == STATUS_IO_TIMEOUT)
		return true;
	else
		return false;
}

static bool
smb2_is_network_name_deleted(char *buf, struct TCP_Server_Info *server)
{
	struct smb2_hdr *shdr = (struct smb2_hdr *)buf;
	struct TCP_Server_Info *pserver;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;

	if (shdr->Status != STATUS_NETWORK_NAME_DELETED)
		return false;

	/* If server is a channel, select the primary channel */
	pserver = SERVER_IS_CHAN(server) ? server->primary_server : server;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &pserver->smb_ses_list, smb_ses_list) {
		list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
			if (tcon->tid == le32_to_cpu(shdr->Id.SyncId.TreeId)) {
				spin_lock(&tcon->tc_lock);
				tcon->need_reconnect = true;
				spin_unlock(&tcon->tc_lock);
				spin_unlock(&cifs_tcp_ses_lock);
				pr_warn_once("Server share %s deleted.\n",
					     tcon->tree_name);
				return true;
			}
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	return false;
}

static int
smb2_oplock_response(struct cifs_tcon *tcon, __u64 persistent_fid,
		__u64 volatile_fid, __u16 net_fid, struct cifsInodeInfo *cinode)
{
	if (tcon->ses->server->capabilities & SMB2_GLOBAL_CAP_LEASING)
		return SMB2_lease_break(0, tcon, cinode->lease_key,
					smb2_get_lease_state(cinode));

	return SMB2_oplock_break(0, tcon, persistent_fid, volatile_fid,
				 CIFS_CACHE_READ(cinode) ? 1 : 0);
}

void
smb2_set_related(struct smb_rqst *rqst)
{
	struct smb2_hdr *shdr;

	shdr = (struct smb2_hdr *)(rqst->rq_iov[0].iov_base);
	if (shdr == NULL) {
		cifs_dbg(FYI, "shdr NULL in smb2_set_related\n");
		return;
	}
	shdr->Flags |= SMB2_FLAGS_RELATED_OPERATIONS;
}

char smb2_padding[7] = {0, 0, 0, 0, 0, 0, 0};

void
smb2_set_next_command(struct cifs_tcon *tcon, struct smb_rqst *rqst)
{
	struct smb2_hdr *shdr;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = ses->server;
	unsigned long len = smb_rqst_len(server, rqst);
	int i, num_padding;

	shdr = (struct smb2_hdr *)(rqst->rq_iov[0].iov_base);
	if (shdr == NULL) {
		cifs_dbg(FYI, "shdr NULL in smb2_set_next_command\n");
		return;
	}

	/* SMB headers in a compound are 8 byte aligned. */

	/* No padding needed */
	if (!(len & 7))
		goto finished;

	num_padding = 8 - (len & 7);
	if (!smb3_encryption_required(tcon)) {
		/*
		 * If we do not have encryption then we can just add an extra
		 * iov for the padding.
		 */
		rqst->rq_iov[rqst->rq_nvec].iov_base = smb2_padding;
		rqst->rq_iov[rqst->rq_nvec].iov_len = num_padding;
		rqst->rq_nvec++;
		len += num_padding;
	} else {
		/*
		 * We can not add a small padding iov for the encryption case
		 * because the encryption framework can not handle the padding
		 * iovs.
		 * We have to flatten this into a single buffer and add
		 * the padding to it.
		 */
		for (i = 1; i < rqst->rq_nvec; i++) {
			memcpy(rqst->rq_iov[0].iov_base +
			       rqst->rq_iov[0].iov_len,
			       rqst->rq_iov[i].iov_base,
			       rqst->rq_iov[i].iov_len);
			rqst->rq_iov[0].iov_len += rqst->rq_iov[i].iov_len;
		}
		memset(rqst->rq_iov[0].iov_base + rqst->rq_iov[0].iov_len,
		       0, num_padding);
		rqst->rq_iov[0].iov_len += num_padding;
		len += num_padding;
		rqst->rq_nvec = 1;
	}

 finished:
	shdr->NextCommand = cpu_to_le32(len);
}

/*
 * Passes the query info response back to the caller on success.
 * Caller need to free this with free_rsp_buf().
 */
int
smb2_query_info_compound(const unsigned int xid, struct cifs_tcon *tcon,
			 const char *path, u32 desired_access,
			 u32 class, u32 type, u32 output_len,
			 struct kvec *rsp, int *buftype,
			 struct cifs_sb_info *cifs_sb)
{
	struct smb2_compound_vars *vars;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	int flags = CIFS_CP_CREATE_CLOSE_OP;
	struct smb_rqst *rqst;
	int resp_buftype[3];
	struct kvec *rsp_iov;
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	int rc;
	__le16 *utf16_path;
	struct cached_fid *cfid = NULL;

	if (!path)
		path = "";
	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	vars = kzalloc(sizeof(*vars), GFP_KERNEL);
	if (!vars) {
		rc = -ENOMEM;
		goto out_free_path;
	}
	rqst = vars->rqst;
	rsp_iov = vars->rsp_iov;

	/*
	 * We can only call this for things we know are directories.
	 */
	if (!strcmp(path, ""))
		open_cached_dir(xid, tcon, path, cifs_sb, false,
				&cfid); /* cfid null if open dir failed */

	rqst[0].rq_iov = vars->open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = path,
		.desired_access = desired_access,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = &fid,
	};

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto qic_exit;
	smb2_set_next_command(tcon, &rqst[0]);

	rqst[1].rq_iov = &vars->qi_iov;
	rqst[1].rq_nvec = 1;

	if (cfid) {
		rc = SMB2_query_info_init(tcon, server,
					  &rqst[1],
					  cfid->fid.persistent_fid,
					  cfid->fid.volatile_fid,
					  class, type, 0,
					  output_len, 0,
					  NULL);
	} else {
		rc = SMB2_query_info_init(tcon, server,
					  &rqst[1],
					  COMPOUND_FID,
					  COMPOUND_FID,
					  class, type, 0,
					  output_len, 0,
					  NULL);
	}
	if (rc)
		goto qic_exit;
	if (!cfid) {
		smb2_set_next_command(tcon, &rqst[1]);
		smb2_set_related(&rqst[1]);
	}

	rqst[2].rq_iov = &vars->close_iov;
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	if (rc)
		goto qic_exit;
	smb2_set_related(&rqst[2]);

	if (cfid) {
		rc = compound_send_recv(xid, ses, server,
					flags, 1, &rqst[1],
					&resp_buftype[1], &rsp_iov[1]);
	} else {
		rc = compound_send_recv(xid, ses, server,
					flags, 3, rqst,
					resp_buftype, rsp_iov);
	}
	if (rc) {
		free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
		if (rc == -EREMCHG) {
			tcon->need_reconnect = true;
			pr_warn_once("server share %s deleted\n",
				     tcon->tree_name);
		}
		goto qic_exit;
	}
	*rsp = rsp_iov[1];
	*buftype = resp_buftype[1];

 qic_exit:
	SMB2_open_free(&rqst[0]);
	SMB2_query_info_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	if (cfid)
		close_cached_dir(cfid);
	kfree(vars);
out_free_path:
	kfree(utf16_path);
	return rc;
}

static int
smb2_queryfs(const unsigned int xid, struct cifs_tcon *tcon,
	     struct cifs_sb_info *cifs_sb, struct kstatfs *buf)
{
	struct smb2_query_info_rsp *rsp;
	struct smb2_fs_full_size_info *info = NULL;
	struct kvec rsp_iov = {NULL, 0};
	int buftype = CIFS_NO_BUFFER;
	int rc;


	rc = smb2_query_info_compound(xid, tcon, "",
				      FILE_READ_ATTRIBUTES,
				      FS_FULL_SIZE_INFORMATION,
				      SMB2_O_INFO_FILESYSTEM,
				      sizeof(struct smb2_fs_full_size_info),
				      &rsp_iov, &buftype, cifs_sb);
	if (rc)
		goto qfs_exit;

	rsp = (struct smb2_query_info_rsp *)rsp_iov.iov_base;
	buf->f_type = SMB2_SUPER_MAGIC;
	info = (struct smb2_fs_full_size_info *)(
		le16_to_cpu(rsp->OutputBufferOffset) + (char *)rsp);
	rc = smb2_validate_iov(le16_to_cpu(rsp->OutputBufferOffset),
			       le32_to_cpu(rsp->OutputBufferLength),
			       &rsp_iov,
			       sizeof(struct smb2_fs_full_size_info));
	if (!rc)
		smb2_copy_fs_info_to_kstatfs(info, buf);

qfs_exit:
	trace_smb3_qfs_done(xid, tcon->tid, tcon->ses->Suid, tcon->tree_name, rc);
	free_rsp_buf(buftype, rsp_iov.iov_base);
	return rc;
}

static int
smb311_queryfs(const unsigned int xid, struct cifs_tcon *tcon,
	       struct cifs_sb_info *cifs_sb, struct kstatfs *buf)
{
	int rc;
	__le16 srch_path = 0; /* Null - open root of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;

	if (!tcon->posix_extensions)
		return smb2_queryfs(xid, tcon, cifs_sb, buf);

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = "",
		.desired_access = FILE_READ_ATTRIBUTES,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, 0),
		.fid = &fid,
	};

	rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL,
		       NULL, NULL);
	if (rc)
		return rc;

	rc = SMB311_posix_qfs_info(xid, tcon, fid.persistent_fid,
				   fid.volatile_fid, buf);
	buf->f_type = SMB2_SUPER_MAGIC;
	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	return rc;
}

static bool
smb2_compare_fids(struct cifsFileInfo *ob1, struct cifsFileInfo *ob2)
{
	return ob1->fid.persistent_fid == ob2->fid.persistent_fid &&
	       ob1->fid.volatile_fid == ob2->fid.volatile_fid;
}

static int
smb2_mand_lock(const unsigned int xid, struct cifsFileInfo *cfile, __u64 offset,
	       __u64 length, __u32 type, int lock, int unlock, bool wait)
{
	if (unlock && !lock)
		type = SMB2_LOCKFLAG_UNLOCK;
	return SMB2_lock(xid, tlink_tcon(cfile->tlink),
			 cfile->fid.persistent_fid, cfile->fid.volatile_fid,
			 current->tgid, length, offset, type, wait);
}

static void
smb2_get_lease_key(struct inode *inode, struct cifs_fid *fid)
{
	memcpy(fid->lease_key, CIFS_I(inode)->lease_key, SMB2_LEASE_KEY_SIZE);
}

static void
smb2_set_lease_key(struct inode *inode, struct cifs_fid *fid)
{
	memcpy(CIFS_I(inode)->lease_key, fid->lease_key, SMB2_LEASE_KEY_SIZE);
}

static void
smb2_new_lease_key(struct cifs_fid *fid)
{
	generate_random_uuid(fid->lease_key);
}

static int
smb2_get_dfs_refer(const unsigned int xid, struct cifs_ses *ses,
		   const char *search_name,
		   struct dfs_info3_param **target_nodes,
		   unsigned int *num_of_nodes,
		   const struct nls_table *nls_codepage, int remap)
{
	int rc;
	__le16 *utf16_path = NULL;
	int utf16_path_len = 0;
	struct cifs_tcon *tcon;
	struct fsctl_get_dfs_referral_req *dfs_req = NULL;
	struct get_dfs_referral_rsp *dfs_rsp = NULL;
	u32 dfs_req_size = 0, dfs_rsp_size = 0;
	int retry_count = 0;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, search_name);

	/*
	 * Try to use the IPC tcon, otherwise just use any
	 */
	tcon = ses->tcon_ipc;
	if (tcon == NULL) {
		spin_lock(&cifs_tcp_ses_lock);
		tcon = list_first_entry_or_null(&ses->tcon_list,
						struct cifs_tcon,
						tcon_list);
		if (tcon)
			tcon->tc_count++;
		spin_unlock(&cifs_tcp_ses_lock);
	}

	if (tcon == NULL) {
		cifs_dbg(VFS, "session %p has no tcon available for a dfs referral request\n",
			 ses);
		rc = -ENOTCONN;
		goto out;
	}

	utf16_path = cifs_strndup_to_utf16(search_name, PATH_MAX,
					   &utf16_path_len,
					   nls_codepage, remap);
	if (!utf16_path) {
		rc = -ENOMEM;
		goto out;
	}

	dfs_req_size = sizeof(*dfs_req) + utf16_path_len;
	dfs_req = kzalloc(dfs_req_size, GFP_KERNEL);
	if (!dfs_req) {
		rc = -ENOMEM;
		goto out;
	}

	/* Highest DFS referral version understood */
	dfs_req->MaxReferralLevel = DFS_VERSION;

	/* Path to resolve in an UTF-16 null-terminated string */
	memcpy(dfs_req->RequestFileName, utf16_path, utf16_path_len);

	do {
		rc = SMB2_ioctl(xid, tcon, NO_FILE_ID, NO_FILE_ID,
				FSCTL_DFS_GET_REFERRALS,
				(char *)dfs_req, dfs_req_size, CIFSMaxBufSize,
				(char **)&dfs_rsp, &dfs_rsp_size);
		if (!is_retryable_error(rc))
			break;
		usleep_range(512, 2048);
	} while (++retry_count < 5);

	if (!rc && !dfs_rsp)
		rc = -EIO;
	if (rc) {
		if (!is_retryable_error(rc) && rc != -ENOENT && rc != -EOPNOTSUPP)
			cifs_tcon_dbg(VFS, "%s: ioctl error: rc=%d\n", __func__, rc);
		goto out;
	}

	rc = parse_dfs_referrals(dfs_rsp, dfs_rsp_size,
				 num_of_nodes, target_nodes,
				 nls_codepage, remap, search_name,
				 true /* is_unicode */);
	if (rc) {
		cifs_tcon_dbg(VFS, "parse error in %s rc=%d\n", __func__, rc);
		goto out;
	}

 out:
	if (tcon && !tcon->ipc) {
		/* ipc tcons are not refcounted */
		spin_lock(&cifs_tcp_ses_lock);
		tcon->tc_count--;
		/* tc_count can never go negative */
		WARN_ON(tcon->tc_count < 0);
		spin_unlock(&cifs_tcp_ses_lock);
	}
	kfree(utf16_path);
	kfree(dfs_req);
	kfree(dfs_rsp);
	return rc;
}

/* See MS-FSCC 2.1.2.6 for the 'NFS' style reparse tags */
static int parse_reparse_posix(struct reparse_posix_data *buf,
			       struct cifs_sb_info *cifs_sb,
			       struct cifs_open_info_data *data)
{
	unsigned int len;
	u64 type;

	switch ((type = le64_to_cpu(buf->InodeType))) {
	case NFS_SPECFILE_LNK:
		len = le16_to_cpu(buf->ReparseDataLength);
		data->symlink_target = cifs_strndup_from_utf16(buf->DataBuffer,
							       len, true,
							       cifs_sb->local_nls);
		if (!data->symlink_target)
			return -ENOMEM;
		convert_delimiter(data->symlink_target, '/');
		cifs_dbg(FYI, "%s: target path: %s\n",
			 __func__, data->symlink_target);
		break;
	case NFS_SPECFILE_CHR:
	case NFS_SPECFILE_BLK:
	case NFS_SPECFILE_FIFO:
	case NFS_SPECFILE_SOCK:
		break;
	default:
		cifs_dbg(VFS, "%s: unhandled inode type: 0x%llx\n",
			 __func__, type);
		return -EOPNOTSUPP;
	}
	return 0;
}

static int parse_reparse_symlink(struct reparse_symlink_data_buffer *sym,
				 u32 plen, bool unicode,
				 struct cifs_sb_info *cifs_sb,
				 struct cifs_open_info_data *data)
{
	unsigned int len;
	unsigned int offs;

	/* We handle Symbolic Link reparse tag here. See: MS-FSCC 2.1.2.4 */

	offs = le16_to_cpu(sym->SubstituteNameOffset);
	len = le16_to_cpu(sym->SubstituteNameLength);
	if (offs + 20 > plen || offs + len + 20 > plen) {
		cifs_dbg(VFS, "srv returned malformed symlink buffer\n");
		return -EIO;
	}

	data->symlink_target = cifs_strndup_from_utf16(sym->PathBuffer + offs,
						       len, unicode,
						       cifs_sb->local_nls);
	if (!data->symlink_target)
		return -ENOMEM;

	convert_delimiter(data->symlink_target, '/');
	cifs_dbg(FYI, "%s: target path: %s\n", __func__, data->symlink_target);

	return 0;
}

int parse_reparse_point(struct reparse_data_buffer *buf,
			u32 plen, struct cifs_sb_info *cifs_sb,
			bool unicode, struct cifs_open_info_data *data)
{
	if (plen < sizeof(*buf)) {
		cifs_dbg(VFS, "%s: reparse buffer is too small. Must be at least 8 bytes but was %d\n",
			 __func__, plen);
		return -EIO;
	}

	if (plen < le16_to_cpu(buf->ReparseDataLength) + sizeof(*buf)) {
		cifs_dbg(VFS, "%s: invalid reparse buf length: %d\n",
			 __func__, plen);
		return -EIO;
	}

	data->reparse.buf = buf;

	/* See MS-FSCC 2.1.2 */
	switch (le32_to_cpu(buf->ReparseTag)) {
	case IO_REPARSE_TAG_NFS:
		return parse_reparse_posix((struct reparse_posix_data *)buf,
					   cifs_sb, data);
	case IO_REPARSE_TAG_SYMLINK:
		return parse_reparse_symlink(
			(struct reparse_symlink_data_buffer *)buf,
			plen, unicode, cifs_sb, data);
	case IO_REPARSE_TAG_LX_SYMLINK:
	case IO_REPARSE_TAG_AF_UNIX:
	case IO_REPARSE_TAG_LX_FIFO:
	case IO_REPARSE_TAG_LX_CHR:
	case IO_REPARSE_TAG_LX_BLK:
		return 0;
	default:
		cifs_dbg(VFS, "%s: unhandled reparse tag: 0x%08x\n",
			 __func__, le32_to_cpu(buf->ReparseTag));
		return -EOPNOTSUPP;
	}
}

static int smb2_parse_reparse_point(struct cifs_sb_info *cifs_sb,
				    struct kvec *rsp_iov,
				    struct cifs_open_info_data *data)
{
	struct reparse_data_buffer *buf;
	struct smb2_ioctl_rsp *io = rsp_iov->iov_base;
	u32 plen = le32_to_cpu(io->OutputCount);

	buf = (struct reparse_data_buffer *)((u8 *)io +
					     le32_to_cpu(io->OutputOffset));
	return parse_reparse_point(buf, plen, cifs_sb, true, data);
}

static int smb2_query_reparse_point(const unsigned int xid,
				    struct cifs_tcon *tcon,
				    struct cifs_sb_info *cifs_sb,
				    const char *full_path,
				    u32 *tag, struct kvec *rsp,
				    int *rsp_buftype)
{
	struct smb2_compound_vars *vars;
	int rc;
	__le16 *utf16_path = NULL;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct TCP_Server_Info *server = cifs_pick_channel(tcon->ses);
	int flags = CIFS_CP_CREATE_CLOSE_OP;
	struct smb_rqst *rqst;
	int resp_buftype[3];
	struct kvec *rsp_iov;
	struct smb2_ioctl_rsp *ioctl_rsp;
	struct reparse_data_buffer *reparse_buf;
	u32 off, count, len;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, full_path);

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	vars = kzalloc(sizeof(*vars), GFP_KERNEL);
	if (!vars) {
		rc = -ENOMEM;
		goto out_free_path;
	}
	rqst = vars->rqst;
	rsp_iov = vars->rsp_iov;

	/*
	 * setup smb2open - TODO add optimization to call cifs_get_readable_path
	 * to see if there is a handle already open that we can use
	 */
	rqst[0].rq_iov = vars->open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = full_path,
		.desired_access = FILE_READ_ATTRIBUTES,
		.disposition = FILE_OPEN,
		.create_options = cifs_create_options(cifs_sb, OPEN_REPARSE_POINT),
		.fid = &fid,
	};

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto query_rp_exit;
	smb2_set_next_command(tcon, &rqst[0]);


	/* IOCTL */
	rqst[1].rq_iov = vars->io_iov;
	rqst[1].rq_nvec = SMB2_IOCTL_IOV_SIZE;

	rc = SMB2_ioctl_init(tcon, server,
			     &rqst[1], COMPOUND_FID,
			     COMPOUND_FID, FSCTL_GET_REPARSE_POINT, NULL, 0,
			     CIFSMaxBufSize -
			     MAX_SMB2_CREATE_RESPONSE_SIZE -
			     MAX_SMB2_CLOSE_RESPONSE_SIZE);
	if (rc)
		goto query_rp_exit;

	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);

	/* Close */
	rqst[2].rq_iov = &vars->close_iov;
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	if (rc)
		goto query_rp_exit;

	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, tcon->ses, server,
				flags, 3, rqst,
				resp_buftype, rsp_iov);

	ioctl_rsp = rsp_iov[1].iov_base;

	/*
	 * Open was successful and we got an ioctl response.
	 */
	if (rc == 0) {
		/* See MS-FSCC 2.3.23 */
		off = le32_to_cpu(ioctl_rsp->OutputOffset);
		count = le32_to_cpu(ioctl_rsp->OutputCount);
		if (check_add_overflow(off, count, &len) ||
		    len > rsp_iov[1].iov_len) {
			cifs_tcon_dbg(VFS, "%s: invalid ioctl: off=%d count=%d\n",
				      __func__, off, count);
			rc = -EIO;
			goto query_rp_exit;
		}

		reparse_buf = (void *)((u8 *)ioctl_rsp + off);
		len = sizeof(*reparse_buf);
		if (count < len ||
		    count < le16_to_cpu(reparse_buf->ReparseDataLength) + len) {
			cifs_tcon_dbg(VFS, "%s: invalid ioctl: off=%d count=%d\n",
				      __func__, off, count);
			rc = -EIO;
			goto query_rp_exit;
		}
		*tag = le32_to_cpu(reparse_buf->ReparseTag);
		*rsp = rsp_iov[1];
		*rsp_buftype = resp_buftype[1];
		resp_buftype[1] = CIFS_NO_BUFFER;
	}

 query_rp_exit:
	SMB2_open_free(&rqst[0]);
	SMB2_ioctl_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	kfree(vars);
out_free_path:
	kfree(utf16_path);
	return rc;
}

static struct cifs_ntsd *
get_smb2_acl_by_fid(struct cifs_sb_info *cifs_sb,
		    const struct cifs_fid *cifsfid, u32 *pacllen, u32 info)
{
	struct cifs_ntsd *pntsd = NULL;
	unsigned int xid;
	int rc = -EOPNOTSUPP;
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);

	if (IS_ERR(tlink))
		return ERR_CAST(tlink);

	xid = get_xid();
	cifs_dbg(FYI, "trying to get acl\n");

	rc = SMB2_query_acl(xid, tlink_tcon(tlink), cifsfid->persistent_fid,
			    cifsfid->volatile_fid, (void **)&pntsd, pacllen,
			    info);
	free_xid(xid);

	cifs_put_tlink(tlink);

	cifs_dbg(FYI, "%s: rc = %d ACL len %d\n", __func__, rc, *pacllen);
	if (rc)
		return ERR_PTR(rc);
	return pntsd;

}

static struct cifs_ntsd *
get_smb2_acl_by_path(struct cifs_sb_info *cifs_sb,
		     const char *path, u32 *pacllen, u32 info)
{
	struct cifs_ntsd *pntsd = NULL;
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	unsigned int xid;
	int rc;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	__le16 *utf16_path;

	cifs_dbg(FYI, "get smb3 acl for path %s\n", path);
	if (IS_ERR(tlink))
		return ERR_CAST(tlink);

	tcon = tlink_tcon(tlink);
	xid = get_xid();

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path) {
		rc = -ENOMEM;
		free_xid(xid);
		return ERR_PTR(rc);
	}

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = path,
		.desired_access = READ_CONTROL,
		.disposition = FILE_OPEN,
		/*
		 * When querying an ACL, even if the file is a symlink
		 * we want to open the source not the target, and so
		 * the protocol requires that the client specify this
		 * flag when opening a reparse point
		 */
		.create_options = cifs_create_options(cifs_sb, 0) |
				  OPEN_REPARSE_POINT,
		.fid = &fid,
	};

	if (info & SACL_SECINFO)
		oparms.desired_access |= SYSTEM_SECURITY;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL,
		       NULL);
	kfree(utf16_path);
	if (!rc) {
		rc = SMB2_query_acl(xid, tlink_tcon(tlink), fid.persistent_fid,
				    fid.volatile_fid, (void **)&pntsd, pacllen,
				    info);
		SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	}

	cifs_put_tlink(tlink);
	free_xid(xid);

	cifs_dbg(FYI, "%s: rc = %d ACL len %d\n", __func__, rc, *pacllen);
	if (rc)
		return ERR_PTR(rc);
	return pntsd;
}

static int
set_smb2_acl(struct cifs_ntsd *pnntsd, __u32 acllen,
		struct inode *inode, const char *path, int aclflag)
{
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	unsigned int xid;
	int rc, access_flags = 0;
	struct cifs_tcon *tcon;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink = cifs_sb_tlink(cifs_sb);
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	__le16 *utf16_path;

	cifs_dbg(FYI, "set smb3 acl for path %s\n", path);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);

	tcon = tlink_tcon(tlink);
	xid = get_xid();

	if (aclflag & CIFS_ACL_OWNER || aclflag & CIFS_ACL_GROUP)
		access_flags |= WRITE_OWNER;
	if (aclflag & CIFS_ACL_SACL)
		access_flags |= SYSTEM_SECURITY;
	if (aclflag & CIFS_ACL_DACL)
		access_flags |= WRITE_DAC;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path) {
		rc = -ENOMEM;
		free_xid(xid);
		return rc;
	}

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.desired_access = access_flags,
		.create_options = cifs_create_options(cifs_sb, 0),
		.disposition = FILE_OPEN,
		.path = path,
		.fid = &fid,
	};

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL,
		       NULL, NULL);
	kfree(utf16_path);
	if (!rc) {
		rc = SMB2_set_acl(xid, tlink_tcon(tlink), fid.persistent_fid,
			    fid.volatile_fid, pnntsd, acllen, aclflag);
		SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	}

	cifs_put_tlink(tlink);
	free_xid(xid);
	return rc;
}

/* Retrieve an ACL from the server */
static struct cifs_ntsd *
get_smb2_acl(struct cifs_sb_info *cifs_sb,
	     struct inode *inode, const char *path,
	     u32 *pacllen, u32 info)
{
	struct cifs_ntsd *pntsd = NULL;
	struct cifsFileInfo *open_file = NULL;

	if (inode && !(info & SACL_SECINFO))
		open_file = find_readable_file(CIFS_I(inode), true);
	if (!open_file || (info & SACL_SECINFO))
		return get_smb2_acl_by_path(cifs_sb, path, pacllen, info);

	pntsd = get_smb2_acl_by_fid(cifs_sb, &open_file->fid, pacllen, info);
	cifsFileInfo_put(open_file);
	return pntsd;
}

static long smb3_zero_data(struct file *file, struct cifs_tcon *tcon,
			     loff_t offset, loff_t len, unsigned int xid)
{
	struct cifsFileInfo *cfile = file->private_data;
	struct file_zero_data_information fsctl_buf;

	cifs_dbg(FYI, "Offset %lld len %lld\n", offset, len);

	fsctl_buf.FileOffset = cpu_to_le64(offset);
	fsctl_buf.BeyondFinalZero = cpu_to_le64(offset + len);

	return SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			  cfile->fid.volatile_fid, FSCTL_SET_ZERO_DATA,
			  (char *)&fsctl_buf,
			  sizeof(struct file_zero_data_information),
			  0, NULL, NULL);
}

static long smb3_zero_range(struct file *file, struct cifs_tcon *tcon,
			    loff_t offset, loff_t len, bool keep_size)
{
	struct cifs_ses *ses = tcon->ses;
	struct inode *inode = file_inode(file);
	struct cifsInodeInfo *cifsi = CIFS_I(inode);
	struct cifsFileInfo *cfile = file->private_data;
	unsigned long long new_size;
	long rc;
	unsigned int xid;
	__le64 eof;

	xid = get_xid();

	trace_smb3_zero_enter(xid, cfile->fid.persistent_fid, tcon->tid,
			      ses->Suid, offset, len);

	inode_lock(inode);
	filemap_invalidate_lock(inode->i_mapping);

	/*
	 * We zero the range through ioctl, so we need remove the page caches
	 * first, otherwise the data may be inconsistent with the server.
	 */
	truncate_pagecache_range(inode, offset, offset + len - 1);

	/* if file not oplocked can't be sure whether asking to extend size */
	rc = -EOPNOTSUPP;
	if (keep_size == false && !CIFS_CACHE_READ(cifsi))
		goto zero_range_exit;

	rc = smb3_zero_data(file, tcon, offset, len, xid);
	if (rc < 0)
		goto zero_range_exit;

	/*
	 * do we also need to change the size of the file?
	 */
	new_size = offset + len;
	if (keep_size == false && (unsigned long long)i_size_read(inode) < new_size) {
		eof = cpu_to_le64(new_size);
		rc = SMB2_set_eof(xid, tcon, cfile->fid.persistent_fid,
				  cfile->fid.volatile_fid, cfile->pid, &eof);
		if (rc >= 0) {
			truncate_setsize(inode, new_size);
			fscache_resize_cookie(cifs_inode_cookie(inode), new_size);
		}
	}

 zero_range_exit:
	filemap_invalidate_unlock(inode->i_mapping);
	inode_unlock(inode);
	free_xid(xid);
	if (rc)
		trace_smb3_zero_err(xid, cfile->fid.persistent_fid, tcon->tid,
			      ses->Suid, offset, len, rc);
	else
		trace_smb3_zero_done(xid, cfile->fid.persistent_fid, tcon->tid,
			      ses->Suid, offset, len);
	return rc;
}

static long smb3_punch_hole(struct file *file, struct cifs_tcon *tcon,
			    loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	struct cifsFileInfo *cfile = file->private_data;
	struct file_zero_data_information fsctl_buf;
	long rc;
	unsigned int xid;
	__u8 set_sparse = 1;

	xid = get_xid();

	inode_lock(inode);
	/* Need to make file sparse, if not already, before freeing range. */
	/* Consider adding equivalent for compressed since it could also work */
	if (!smb2_set_sparse(xid, tcon, cfile, inode, set_sparse)) {
		rc = -EOPNOTSUPP;
		goto out;
	}

	filemap_invalidate_lock(inode->i_mapping);
	/*
	 * We implement the punch hole through ioctl, so we need remove the page
	 * caches first, otherwise the data may be inconsistent with the server.
	 */
	truncate_pagecache_range(inode, offset, offset + len - 1);

	cifs_dbg(FYI, "Offset %lld len %lld\n", offset, len);

	fsctl_buf.FileOffset = cpu_to_le64(offset);
	fsctl_buf.BeyondFinalZero = cpu_to_le64(offset + len);

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid, FSCTL_SET_ZERO_DATA,
			(char *)&fsctl_buf,
			sizeof(struct file_zero_data_information),
			CIFSMaxBufSize, NULL, NULL);
	filemap_invalidate_unlock(inode->i_mapping);
out:
	inode_unlock(inode);
	free_xid(xid);
	return rc;
}

static int smb3_simple_fallocate_write_range(unsigned int xid,
					     struct cifs_tcon *tcon,
					     struct cifsFileInfo *cfile,
					     loff_t off, loff_t len,
					     char *buf)
{
	struct cifs_io_parms io_parms = {0};
	int nbytes;
	int rc = 0;
	struct kvec iov[2];

	io_parms.netfid = cfile->fid.netfid;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.persistent_fid = cfile->fid.persistent_fid;
	io_parms.volatile_fid = cfile->fid.volatile_fid;

	while (len) {
		io_parms.offset = off;
		io_parms.length = len;
		if (io_parms.length > SMB2_MAX_BUFFER_SIZE)
			io_parms.length = SMB2_MAX_BUFFER_SIZE;
		/* iov[0] is reserved for smb header */
		iov[1].iov_base = buf;
		iov[1].iov_len = io_parms.length;
		rc = SMB2_write(xid, &io_parms, &nbytes, iov, 1);
		if (rc)
			break;
		if (nbytes > len)
			return -EINVAL;
		buf += nbytes;
		off += nbytes;
		len -= nbytes;
	}
	return rc;
}

static int smb3_simple_fallocate_range(unsigned int xid,
				       struct cifs_tcon *tcon,
				       struct cifsFileInfo *cfile,
				       loff_t off, loff_t len)
{
	struct file_allocated_range_buffer in_data, *out_data = NULL, *tmp_data;
	u32 out_data_len;
	char *buf = NULL;
	loff_t l;
	int rc;

	in_data.file_offset = cpu_to_le64(off);
	in_data.length = cpu_to_le64(len);
	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid,
			FSCTL_QUERY_ALLOCATED_RANGES,
			(char *)&in_data, sizeof(in_data),
			1024 * sizeof(struct file_allocated_range_buffer),
			(char **)&out_data, &out_data_len);
	if (rc)
		goto out;

	buf = kzalloc(1024 * 1024, GFP_KERNEL);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	tmp_data = out_data;
	while (len) {
		/*
		 * The rest of the region is unmapped so write it all.
		 */
		if (out_data_len == 0) {
			rc = smb3_simple_fallocate_write_range(xid, tcon,
					       cfile, off, len, buf);
			goto out;
		}

		if (out_data_len < sizeof(struct file_allocated_range_buffer)) {
			rc = -EINVAL;
			goto out;
		}

		if (off < le64_to_cpu(tmp_data->file_offset)) {
			/*
			 * We are at a hole. Write until the end of the region
			 * or until the next allocated data,
			 * whichever comes next.
			 */
			l = le64_to_cpu(tmp_data->file_offset) - off;
			if (len < l)
				l = len;
			rc = smb3_simple_fallocate_write_range(xid, tcon,
					       cfile, off, l, buf);
			if (rc)
				goto out;
			off = off + l;
			len = len - l;
			if (len == 0)
				goto out;
		}
		/*
		 * We are at a section of allocated data, just skip forward
		 * until the end of the data or the end of the region
		 * we are supposed to fallocate, whichever comes first.
		 */
		l = le64_to_cpu(tmp_data->length);
		if (len < l)
			l = len;
		off += l;
		len -= l;

		tmp_data = &tmp_data[1];
		out_data_len -= sizeof(struct file_allocated_range_buffer);
	}

 out:
	kfree(out_data);
	kfree(buf);
	return rc;
}


static long smb3_simple_falloc(struct file *file, struct cifs_tcon *tcon,
			    loff_t off, loff_t len, bool keep_size)
{
	struct inode *inode;
	struct cifsInodeInfo *cifsi;
	struct cifsFileInfo *cfile = file->private_data;
	long rc = -EOPNOTSUPP;
	unsigned int xid;
	__le64 eof;

	xid = get_xid();

	inode = d_inode(cfile->dentry);
	cifsi = CIFS_I(inode);

	trace_smb3_falloc_enter(xid, cfile->fid.persistent_fid, tcon->tid,
				tcon->ses->Suid, off, len);
	/* if file not oplocked can't be sure whether asking to extend size */
	if (!CIFS_CACHE_READ(cifsi))
		if (keep_size == false) {
			trace_smb3_falloc_err(xid, cfile->fid.persistent_fid,
				tcon->tid, tcon->ses->Suid, off, len, rc);
			free_xid(xid);
			return rc;
		}

	/*
	 * Extending the file
	 */
	if ((keep_size == false) && i_size_read(inode) < off + len) {
		rc = inode_newsize_ok(inode, off + len);
		if (rc)
			goto out;

		if (cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE)
			smb2_set_sparse(xid, tcon, cfile, inode, false);

		eof = cpu_to_le64(off + len);
		rc = SMB2_set_eof(xid, tcon, cfile->fid.persistent_fid,
				  cfile->fid.volatile_fid, cfile->pid, &eof);
		if (rc == 0) {
			cifsi->server_eof = off + len;
			cifs_setsize(inode, off + len);
			cifs_truncate_page(inode->i_mapping, inode->i_size);
			truncate_setsize(inode, off + len);
		}
		goto out;
	}

	/*
	 * Files are non-sparse by default so falloc may be a no-op
	 * Must check if file sparse. If not sparse, and since we are not
	 * extending then no need to do anything since file already allocated
	 */
	if ((cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE) == 0) {
		rc = 0;
		goto out;
	}

	if (keep_size == true) {
		/*
		 * We can not preallocate pages beyond the end of the file
		 * in SMB2
		 */
		if (off >= i_size_read(inode)) {
			rc = 0;
			goto out;
		}
		/*
		 * For fallocates that are partially beyond the end of file,
		 * clamp len so we only fallocate up to the end of file.
		 */
		if (off + len > i_size_read(inode)) {
			len = i_size_read(inode) - off;
		}
	}

	if ((keep_size == true) || (i_size_read(inode) >= off + len)) {
		/*
		 * At this point, we are trying to fallocate an internal
		 * regions of a sparse file. Since smb2 does not have a
		 * fallocate command we have two otions on how to emulate this.
		 * We can either turn the entire file to become non-sparse
		 * which we only do if the fallocate is for virtually
		 * the whole file,  or we can overwrite the region with zeroes
		 * using SMB2_write, which could be prohibitevly expensive
		 * if len is large.
		 */
		/*
		 * We are only trying to fallocate a small region so
		 * just write it with zero.
		 */
		if (len <= 1024 * 1024) {
			rc = smb3_simple_fallocate_range(xid, tcon, cfile,
							 off, len);
			goto out;
		}

		/*
		 * Check if falloc starts within first few pages of file
		 * and ends within a few pages of the end of file to
		 * ensure that most of file is being forced to be
		 * fallocated now. If so then setting whole file sparse
		 * ie potentially making a few extra pages at the beginning
		 * or end of the file non-sparse via set_sparse is harmless.
		 */
		if ((off > 8192) || (off + len + 8192 < i_size_read(inode))) {
			rc = -EOPNOTSUPP;
			goto out;
		}
	}

	smb2_set_sparse(xid, tcon, cfile, inode, false);
	rc = 0;

out:
	if (rc)
		trace_smb3_falloc_err(xid, cfile->fid.persistent_fid, tcon->tid,
				tcon->ses->Suid, off, len, rc);
	else
		trace_smb3_falloc_done(xid, cfile->fid.persistent_fid, tcon->tid,
				tcon->ses->Suid, off, len);

	free_xid(xid);
	return rc;
}

static long smb3_collapse_range(struct file *file, struct cifs_tcon *tcon,
			    loff_t off, loff_t len)
{
	int rc;
	unsigned int xid;
	struct inode *inode = file_inode(file);
	struct cifsFileInfo *cfile = file->private_data;
	struct cifsInodeInfo *cifsi = CIFS_I(inode);
	__le64 eof;
	loff_t old_eof;

	xid = get_xid();

	inode_lock(inode);

	old_eof = i_size_read(inode);
	if ((off >= old_eof) ||
	    off + len >= old_eof) {
		rc = -EINVAL;
		goto out;
	}

	filemap_invalidate_lock(inode->i_mapping);
	rc = filemap_write_and_wait_range(inode->i_mapping, off, old_eof - 1);
	if (rc < 0)
		goto out_2;

	truncate_pagecache_range(inode, off, old_eof);

	rc = smb2_copychunk_range(xid, cfile, cfile, off + len,
				  old_eof - off - len, off);
	if (rc < 0)
		goto out_2;

	eof = cpu_to_le64(old_eof - len);
	rc = SMB2_set_eof(xid, tcon, cfile->fid.persistent_fid,
			  cfile->fid.volatile_fid, cfile->pid, &eof);
	if (rc < 0)
		goto out_2;

	rc = 0;

	cifsi->server_eof = i_size_read(inode) - len;
	truncate_setsize(inode, cifsi->server_eof);
	fscache_resize_cookie(cifs_inode_cookie(inode), cifsi->server_eof);
out_2:
	filemap_invalidate_unlock(inode->i_mapping);
 out:
	inode_unlock(inode);
	free_xid(xid);
	return rc;
}

static long smb3_insert_range(struct file *file, struct cifs_tcon *tcon,
			      loff_t off, loff_t len)
{
	int rc;
	unsigned int xid;
	struct cifsFileInfo *cfile = file->private_data;
	struct inode *inode = file_inode(file);
	__le64 eof;
	__u64  count, old_eof;

	xid = get_xid();

	inode_lock(inode);

	old_eof = i_size_read(inode);
	if (off >= old_eof) {
		rc = -EINVAL;
		goto out;
	}

	count = old_eof - off;
	eof = cpu_to_le64(old_eof + len);

	filemap_invalidate_lock(inode->i_mapping);
	rc = filemap_write_and_wait_range(inode->i_mapping, off, old_eof + len - 1);
	if (rc < 0)
		goto out_2;
	truncate_pagecache_range(inode, off, old_eof);

	rc = SMB2_set_eof(xid, tcon, cfile->fid.persistent_fid,
			  cfile->fid.volatile_fid, cfile->pid, &eof);
	if (rc < 0)
		goto out_2;

	truncate_setsize(inode, old_eof + len);
	fscache_resize_cookie(cifs_inode_cookie(inode), i_size_read(inode));

	rc = smb2_copychunk_range(xid, cfile, cfile, off, count, off + len);
	if (rc < 0)
		goto out_2;

	rc = smb3_zero_data(file, tcon, off, len, xid);
	if (rc < 0)
		goto out_2;

	rc = 0;
out_2:
	filemap_invalidate_unlock(inode->i_mapping);
 out:
	inode_unlock(inode);
	free_xid(xid);
	return rc;
}

static loff_t smb3_llseek(struct file *file, struct cifs_tcon *tcon, loff_t offset, int whence)
{
	struct cifsFileInfo *wrcfile, *cfile = file->private_data;
	struct cifsInodeInfo *cifsi;
	struct inode *inode;
	int rc = 0;
	struct file_allocated_range_buffer in_data, *out_data = NULL;
	u32 out_data_len;
	unsigned int xid;

	if (whence != SEEK_HOLE && whence != SEEK_DATA)
		return generic_file_llseek(file, offset, whence);

	inode = d_inode(cfile->dentry);
	cifsi = CIFS_I(inode);

	if (offset < 0 || offset >= i_size_read(inode))
		return -ENXIO;

	xid = get_xid();
	/*
	 * We need to be sure that all dirty pages are written as they
	 * might fill holes on the server.
	 * Note that we also MUST flush any written pages since at least
	 * some servers (Windows2016) will not reflect recent writes in
	 * QUERY_ALLOCATED_RANGES until SMB2_flush is called.
	 */
	wrcfile = find_writable_file(cifsi, FIND_WR_ANY);
	if (wrcfile) {
		filemap_write_and_wait(inode->i_mapping);
		smb2_flush_file(xid, tcon, &wrcfile->fid);
		cifsFileInfo_put(wrcfile);
	}

	if (!(cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE)) {
		if (whence == SEEK_HOLE)
			offset = i_size_read(inode);
		goto lseek_exit;
	}

	in_data.file_offset = cpu_to_le64(offset);
	in_data.length = cpu_to_le64(i_size_read(inode));

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid,
			FSCTL_QUERY_ALLOCATED_RANGES,
			(char *)&in_data, sizeof(in_data),
			sizeof(struct file_allocated_range_buffer),
			(char **)&out_data, &out_data_len);
	if (rc == -E2BIG)
		rc = 0;
	if (rc)
		goto lseek_exit;

	if (whence == SEEK_HOLE && out_data_len == 0)
		goto lseek_exit;

	if (whence == SEEK_DATA && out_data_len == 0) {
		rc = -ENXIO;
		goto lseek_exit;
	}

	if (out_data_len < sizeof(struct file_allocated_range_buffer)) {
		rc = -EINVAL;
		goto lseek_exit;
	}
	if (whence == SEEK_DATA) {
		offset = le64_to_cpu(out_data->file_offset);
		goto lseek_exit;
	}
	if (offset < le64_to_cpu(out_data->file_offset))
		goto lseek_exit;

	offset = le64_to_cpu(out_data->file_offset) + le64_to_cpu(out_data->length);

 lseek_exit:
	free_xid(xid);
	kfree(out_data);
	if (!rc)
		return vfs_setpos(file, offset, inode->i_sb->s_maxbytes);
	else
		return rc;
}

static int smb3_fiemap(struct cifs_tcon *tcon,
		       struct cifsFileInfo *cfile,
		       struct fiemap_extent_info *fei, u64 start, u64 len)
{
	unsigned int xid;
	struct file_allocated_range_buffer in_data, *out_data;
	u32 out_data_len;
	int i, num, rc, flags, last_blob;
	u64 next;

	rc = fiemap_prep(d_inode(cfile->dentry), fei, start, &len, 0);
	if (rc)
		return rc;

	xid = get_xid();
 again:
	in_data.file_offset = cpu_to_le64(start);
	in_data.length = cpu_to_le64(len);

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid,
			FSCTL_QUERY_ALLOCATED_RANGES,
			(char *)&in_data, sizeof(in_data),
			1024 * sizeof(struct file_allocated_range_buffer),
			(char **)&out_data, &out_data_len);
	if (rc == -E2BIG) {
		last_blob = 0;
		rc = 0;
	} else
		last_blob = 1;
	if (rc)
		goto out;

	if (out_data_len && out_data_len < sizeof(struct file_allocated_range_buffer)) {
		rc = -EINVAL;
		goto out;
	}
	if (out_data_len % sizeof(struct file_allocated_range_buffer)) {
		rc = -EINVAL;
		goto out;
	}

	num = out_data_len / sizeof(struct file_allocated_range_buffer);
	for (i = 0; i < num; i++) {
		flags = 0;
		if (i == num - 1 && last_blob)
			flags |= FIEMAP_EXTENT_LAST;

		rc = fiemap_fill_next_extent(fei,
				le64_to_cpu(out_data[i].file_offset),
				le64_to_cpu(out_data[i].file_offset),
				le64_to_cpu(out_data[i].length),
				flags);
		if (rc < 0)
			goto out;
		if (rc == 1) {
			rc = 0;
			goto out;
		}
	}

	if (!last_blob) {
		next = le64_to_cpu(out_data[num - 1].file_offset) +
		  le64_to_cpu(out_data[num - 1].length);
		len = len - (next - start);
		start = next;
		goto again;
	}

 out:
	free_xid(xid);
	kfree(out_data);
	return rc;
}

static long smb3_fallocate(struct file *file, struct cifs_tcon *tcon, int mode,
			   loff_t off, loff_t len)
{
	/* KEEP_SIZE already checked for by do_fallocate */
	if (mode & FALLOC_FL_PUNCH_HOLE)
		return smb3_punch_hole(file, tcon, off, len);
	else if (mode & FALLOC_FL_ZERO_RANGE) {
		if (mode & FALLOC_FL_KEEP_SIZE)
			return smb3_zero_range(file, tcon, off, len, true);
		return smb3_zero_range(file, tcon, off, len, false);
	} else if (mode == FALLOC_FL_KEEP_SIZE)
		return smb3_simple_falloc(file, tcon, off, len, true);
	else if (mode == FALLOC_FL_COLLAPSE_RANGE)
		return smb3_collapse_range(file, tcon, off, len);
	else if (mode == FALLOC_FL_INSERT_RANGE)
		return smb3_insert_range(file, tcon, off, len);
	else if (mode == 0)
		return smb3_simple_falloc(file, tcon, off, len, false);

	return -EOPNOTSUPP;
}

static void
smb2_downgrade_oplock(struct TCP_Server_Info *server,
		      struct cifsInodeInfo *cinode, __u32 oplock,
		      unsigned int epoch, bool *purge_cache)
{
	server->ops->set_oplock_level(cinode, oplock, 0, NULL);
}

static void
smb21_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock,
		       unsigned int epoch, bool *purge_cache);

static void
smb3_downgrade_oplock(struct TCP_Server_Info *server,
		       struct cifsInodeInfo *cinode, __u32 oplock,
		       unsigned int epoch, bool *purge_cache)
{
	unsigned int old_state = cinode->oplock;
	unsigned int old_epoch = cinode->epoch;
	unsigned int new_state;

	if (epoch > old_epoch) {
		smb21_set_oplock_level(cinode, oplock, 0, NULL);
		cinode->epoch = epoch;
	}

	new_state = cinode->oplock;
	*purge_cache = false;

	if ((old_state & CIFS_CACHE_READ_FLG) != 0 &&
	    (new_state & CIFS_CACHE_READ_FLG) == 0)
		*purge_cache = true;
	else if (old_state == new_state && (epoch - old_epoch > 1))
		*purge_cache = true;
}

static void
smb2_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock,
		      unsigned int epoch, bool *purge_cache)
{
	oplock &= 0xFF;
	cinode->lease_granted = false;
	if (oplock == SMB2_OPLOCK_LEVEL_NOCHANGE)
		return;
	if (oplock == SMB2_OPLOCK_LEVEL_BATCH) {
		cinode->oplock = CIFS_CACHE_RHW_FLG;
		cifs_dbg(FYI, "Batch Oplock granted on inode %p\n",
			 &cinode->netfs.inode);
	} else if (oplock == SMB2_OPLOCK_LEVEL_EXCLUSIVE) {
		cinode->oplock = CIFS_CACHE_RW_FLG;
		cifs_dbg(FYI, "Exclusive Oplock granted on inode %p\n",
			 &cinode->netfs.inode);
	} else if (oplock == SMB2_OPLOCK_LEVEL_II) {
		cinode->oplock = CIFS_CACHE_READ_FLG;
		cifs_dbg(FYI, "Level II Oplock granted on inode %p\n",
			 &cinode->netfs.inode);
	} else
		cinode->oplock = 0;
}

static void
smb21_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock,
		       unsigned int epoch, bool *purge_cache)
{
	char message[5] = {0};
	unsigned int new_oplock = 0;

	oplock &= 0xFF;
	cinode->lease_granted = true;
	if (oplock == SMB2_OPLOCK_LEVEL_NOCHANGE)
		return;

	/* Check if the server granted an oplock rather than a lease */
	if (oplock & SMB2_OPLOCK_LEVEL_EXCLUSIVE)
		return smb2_set_oplock_level(cinode, oplock, epoch,
					     purge_cache);

	if (oplock & SMB2_LEASE_READ_CACHING_HE) {
		new_oplock |= CIFS_CACHE_READ_FLG;
		strcat(message, "R");
	}
	if (oplock & SMB2_LEASE_HANDLE_CACHING_HE) {
		new_oplock |= CIFS_CACHE_HANDLE_FLG;
		strcat(message, "H");
	}
	if (oplock & SMB2_LEASE_WRITE_CACHING_HE) {
		new_oplock |= CIFS_CACHE_WRITE_FLG;
		strcat(message, "W");
	}
	if (!new_oplock)
		strncpy(message, "None", sizeof(message));

	cinode->oplock = new_oplock;
	cifs_dbg(FYI, "%s Lease granted on inode %p\n", message,
		 &cinode->netfs.inode);
}

static void
smb3_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock,
		      unsigned int epoch, bool *purge_cache)
{
	unsigned int old_oplock = cinode->oplock;

	smb21_set_oplock_level(cinode, oplock, epoch, purge_cache);

	if (purge_cache) {
		*purge_cache = false;
		if (old_oplock == CIFS_CACHE_READ_FLG) {
			if (cinode->oplock == CIFS_CACHE_READ_FLG &&
			    (epoch - cinode->epoch > 0))
				*purge_cache = true;
			else if (cinode->oplock == CIFS_CACHE_RH_FLG &&
				 (epoch - cinode->epoch > 1))
				*purge_cache = true;
			else if (cinode->oplock == CIFS_CACHE_RHW_FLG &&
				 (epoch - cinode->epoch > 1))
				*purge_cache = true;
			else if (cinode->oplock == 0 &&
				 (epoch - cinode->epoch > 0))
				*purge_cache = true;
		} else if (old_oplock == CIFS_CACHE_RH_FLG) {
			if (cinode->oplock == CIFS_CACHE_RH_FLG &&
			    (epoch - cinode->epoch > 0))
				*purge_cache = true;
			else if (cinode->oplock == CIFS_CACHE_RHW_FLG &&
				 (epoch - cinode->epoch > 1))
				*purge_cache = true;
		}
		cinode->epoch = epoch;
	}
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
static bool
smb2_is_read_op(__u32 oplock)
{
	return oplock == SMB2_OPLOCK_LEVEL_II;
}
#endif /* CIFS_ALLOW_INSECURE_LEGACY */

static bool
smb21_is_read_op(__u32 oplock)
{
	return (oplock & SMB2_LEASE_READ_CACHING_HE) &&
	       !(oplock & SMB2_LEASE_WRITE_CACHING_HE);
}

static __le32
map_oplock_to_lease(u8 oplock)
{
	if (oplock == SMB2_OPLOCK_LEVEL_EXCLUSIVE)
		return SMB2_LEASE_WRITE_CACHING_LE | SMB2_LEASE_READ_CACHING_LE;
	else if (oplock == SMB2_OPLOCK_LEVEL_II)
		return SMB2_LEASE_READ_CACHING_LE;
	else if (oplock == SMB2_OPLOCK_LEVEL_BATCH)
		return SMB2_LEASE_HANDLE_CACHING_LE | SMB2_LEASE_READ_CACHING_LE |
		       SMB2_LEASE_WRITE_CACHING_LE;
	return 0;
}

static char *
smb2_create_lease_buf(u8 *lease_key, u8 oplock)
{
	struct create_lease *buf;

	buf = kzalloc(sizeof(struct create_lease), GFP_KERNEL);
	if (!buf)
		return NULL;

	memcpy(&buf->lcontext.LeaseKey, lease_key, SMB2_LEASE_KEY_SIZE);
	buf->lcontext.LeaseState = map_oplock_to_lease(oplock);

	buf->ccontext.DataOffset = cpu_to_le16(offsetof
					(struct create_lease, lcontext));
	buf->ccontext.DataLength = cpu_to_le32(sizeof(struct lease_context));
	buf->ccontext.NameOffset = cpu_to_le16(offsetof
				(struct create_lease, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);
	/* SMB2_CREATE_REQUEST_LEASE is "RqLs" */
	buf->Name[0] = 'R';
	buf->Name[1] = 'q';
	buf->Name[2] = 'L';
	buf->Name[3] = 's';
	return (char *)buf;
}

static char *
smb3_create_lease_buf(u8 *lease_key, u8 oplock)
{
	struct create_lease_v2 *buf;

	buf = kzalloc(sizeof(struct create_lease_v2), GFP_KERNEL);
	if (!buf)
		return NULL;

	memcpy(&buf->lcontext.LeaseKey, lease_key, SMB2_LEASE_KEY_SIZE);
	buf->lcontext.LeaseState = map_oplock_to_lease(oplock);

	buf->ccontext.DataOffset = cpu_to_le16(offsetof
					(struct create_lease_v2, lcontext));
	buf->ccontext.DataLength = cpu_to_le32(sizeof(struct lease_context_v2));
	buf->ccontext.NameOffset = cpu_to_le16(offsetof
				(struct create_lease_v2, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);
	/* SMB2_CREATE_REQUEST_LEASE is "RqLs" */
	buf->Name[0] = 'R';
	buf->Name[1] = 'q';
	buf->Name[2] = 'L';
	buf->Name[3] = 's';
	return (char *)buf;
}

static __u8
smb2_parse_lease_buf(void *buf, unsigned int *epoch, char *lease_key)
{
	struct create_lease *lc = (struct create_lease *)buf;

	*epoch = 0; /* not used */
	if (lc->lcontext.LeaseFlags & SMB2_LEASE_FLAG_BREAK_IN_PROGRESS_LE)
		return SMB2_OPLOCK_LEVEL_NOCHANGE;
	return le32_to_cpu(lc->lcontext.LeaseState);
}

static __u8
smb3_parse_lease_buf(void *buf, unsigned int *epoch, char *lease_key)
{
	struct create_lease_v2 *lc = (struct create_lease_v2 *)buf;

	*epoch = le16_to_cpu(lc->lcontext.Epoch);
	if (lc->lcontext.LeaseFlags & SMB2_LEASE_FLAG_BREAK_IN_PROGRESS_LE)
		return SMB2_OPLOCK_LEVEL_NOCHANGE;
	if (lease_key)
		memcpy(lease_key, &lc->lcontext.LeaseKey, SMB2_LEASE_KEY_SIZE);
	return le32_to_cpu(lc->lcontext.LeaseState);
}

static unsigned int
smb2_wp_retry_size(struct inode *inode)
{
	return min_t(unsigned int, CIFS_SB(inode->i_sb)->ctx->wsize,
		     SMB2_MAX_BUFFER_SIZE);
}

static bool
smb2_dir_needs_close(struct cifsFileInfo *cfile)
{
	return !cfile->invalidHandle;
}

static void
fill_transform_hdr(struct smb2_transform_hdr *tr_hdr, unsigned int orig_len,
		   struct smb_rqst *old_rq, __le16 cipher_type)
{
	struct smb2_hdr *shdr =
			(struct smb2_hdr *)old_rq->rq_iov[0].iov_base;

	memset(tr_hdr, 0, sizeof(struct smb2_transform_hdr));
	tr_hdr->ProtocolId = SMB2_TRANSFORM_PROTO_NUM;
	tr_hdr->OriginalMessageSize = cpu_to_le32(orig_len);
	tr_hdr->Flags = cpu_to_le16(0x01);
	if ((cipher_type == SMB2_ENCRYPTION_AES128_GCM) ||
	    (cipher_type == SMB2_ENCRYPTION_AES256_GCM))
		get_random_bytes(&tr_hdr->Nonce, SMB3_AES_GCM_NONCE);
	else
		get_random_bytes(&tr_hdr->Nonce, SMB3_AES_CCM_NONCE);
	memcpy(&tr_hdr->SessionId, &shdr->SessionId, 8);
}

static void *smb2_aead_req_alloc(struct crypto_aead *tfm, const struct smb_rqst *rqst,
				 int num_rqst, const u8 *sig, u8 **iv,
				 struct aead_request **req, struct sg_table *sgt,
				 unsigned int *num_sgs, size_t *sensitive_size)
{
	unsigned int req_size = sizeof(**req) + crypto_aead_reqsize(tfm);
	unsigned int iv_size = crypto_aead_ivsize(tfm);
	unsigned int len;
	u8 *p;

	*num_sgs = cifs_get_num_sgs(rqst, num_rqst, sig);
	if (IS_ERR_VALUE((long)(int)*num_sgs))
		return ERR_PTR(*num_sgs);

	len = iv_size;
	len += crypto_aead_alignmask(tfm) & ~(crypto_tfm_ctx_alignment() - 1);
	len = ALIGN(len, crypto_tfm_ctx_alignment());
	len += req_size;
	len = ALIGN(len, __alignof__(struct scatterlist));
	len += array_size(*num_sgs, sizeof(struct scatterlist));
	*sensitive_size = len;

	p = kvzalloc(len, GFP_NOFS);
	if (!p)
		return ERR_PTR(-ENOMEM);

	*iv = (u8 *)PTR_ALIGN(p, crypto_aead_alignmask(tfm) + 1);
	*req = (struct aead_request *)PTR_ALIGN(*iv + iv_size,
						crypto_tfm_ctx_alignment());
	sgt->sgl = (struct scatterlist *)PTR_ALIGN((u8 *)*req + req_size,
						   __alignof__(struct scatterlist));
	return p;
}

static void *smb2_get_aead_req(struct crypto_aead *tfm, struct smb_rqst *rqst,
			       int num_rqst, const u8 *sig, u8 **iv,
			       struct aead_request **req, struct scatterlist **sgl,
			       size_t *sensitive_size)
{
	struct sg_table sgtable = {};
	unsigned int skip, num_sgs, i, j;
	ssize_t rc;
	void *p;

	p = smb2_aead_req_alloc(tfm, rqst, num_rqst, sig, iv, req, &sgtable,
				&num_sgs, sensitive_size);
	if (IS_ERR(p))
		return ERR_CAST(p);

	sg_init_marker(sgtable.sgl, num_sgs);

	/*
	 * The first rqst has a transform header where the
	 * first 20 bytes are not part of the encrypted blob.
	 */
	skip = 20;

	for (i = 0; i < num_rqst; i++) {
		struct iov_iter *iter = &rqst[i].rq_iter;
		size_t count = iov_iter_count(iter);

		for (j = 0; j < rqst[i].rq_nvec; j++) {
			cifs_sg_set_buf(&sgtable,
					rqst[i].rq_iov[j].iov_base + skip,
					rqst[i].rq_iov[j].iov_len - skip);

			/* See the above comment on the 'skip' assignment */
			skip = 0;
		}
		sgtable.orig_nents = sgtable.nents;

		rc = extract_iter_to_sg(iter, count, &sgtable,
					num_sgs - sgtable.nents, 0);
		iov_iter_revert(iter, rc);
		sgtable.orig_nents = sgtable.nents;
	}

	cifs_sg_set_buf(&sgtable, sig, SMB2_SIGNATURE_SIZE);
	sg_mark_end(&sgtable.sgl[sgtable.nents - 1]);
	*sgl = sgtable.sgl;
	return p;
}

static int
smb2_get_enc_key(struct TCP_Server_Info *server, __u64 ses_id, int enc, u8 *key)
{
	struct TCP_Server_Info *pserver;
	struct cifs_ses *ses;
	u8 *ses_enc_key;

	/* If server is a channel, select the primary channel */
	pserver = SERVER_IS_CHAN(server) ? server->primary_server : server;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &pserver->smb_ses_list, smb_ses_list) {
		if (ses->Suid == ses_id) {
			spin_lock(&ses->ses_lock);
			ses_enc_key = enc ? ses->smb3encryptionkey :
				ses->smb3decryptionkey;
			memcpy(key, ses_enc_key, SMB3_ENC_DEC_KEY_SIZE);
			spin_unlock(&ses->ses_lock);
			spin_unlock(&cifs_tcp_ses_lock);
			return 0;
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	trace_smb3_ses_not_found(ses_id);

	return -EAGAIN;
}
/*
 * Encrypt or decrypt @rqst message. @rqst[0] has the following format:
 * iov[0]   - transform header (associate data),
 * iov[1-N] - SMB2 header and pages - data to encrypt.
 * On success return encrypted data in iov[1-N] and pages, leave iov[0]
 * untouched.
 */
static int
crypt_message(struct TCP_Server_Info *server, int num_rqst,
	      struct smb_rqst *rqst, int enc)
{
	struct smb2_transform_hdr *tr_hdr =
		(struct smb2_transform_hdr *)rqst[0].rq_iov[0].iov_base;
	unsigned int assoc_data_len = sizeof(struct smb2_transform_hdr) - 20;
	int rc = 0;
	struct scatterlist *sg;
	u8 sign[SMB2_SIGNATURE_SIZE] = {};
	u8 key[SMB3_ENC_DEC_KEY_SIZE];
	struct aead_request *req;
	u8 *iv;
	DECLARE_CRYPTO_WAIT(wait);
	struct crypto_aead *tfm;
	unsigned int crypt_len = le32_to_cpu(tr_hdr->OriginalMessageSize);
	void *creq;
	size_t sensitive_size;

	rc = smb2_get_enc_key(server, le64_to_cpu(tr_hdr->SessionId), enc, key);
	if (rc) {
		cifs_server_dbg(FYI, "%s: Could not get %scryption key. sid: 0x%llx\n", __func__,
			 enc ? "en" : "de", le64_to_cpu(tr_hdr->SessionId));
		return rc;
	}

	rc = smb3_crypto_aead_allocate(server);
	if (rc) {
		cifs_server_dbg(VFS, "%s: crypto alloc failed\n", __func__);
		return rc;
	}

	tfm = enc ? server->secmech.enc : server->secmech.dec;

	if ((server->cipher_type == SMB2_ENCRYPTION_AES256_CCM) ||
		(server->cipher_type == SMB2_ENCRYPTION_AES256_GCM))
		rc = crypto_aead_setkey(tfm, key, SMB3_GCM256_CRYPTKEY_SIZE);
	else
		rc = crypto_aead_setkey(tfm, key, SMB3_GCM128_CRYPTKEY_SIZE);

	if (rc) {
		cifs_server_dbg(VFS, "%s: Failed to set aead key %d\n", __func__, rc);
		return rc;
	}

	rc = crypto_aead_setauthsize(tfm, SMB2_SIGNATURE_SIZE);
	if (rc) {
		cifs_server_dbg(VFS, "%s: Failed to set authsize %d\n", __func__, rc);
		return rc;
	}

	creq = smb2_get_aead_req(tfm, rqst, num_rqst, sign, &iv, &req, &sg,
				 &sensitive_size);
	if (IS_ERR(creq))
		return PTR_ERR(creq);

	if (!enc) {
		memcpy(sign, &tr_hdr->Signature, SMB2_SIGNATURE_SIZE);
		crypt_len += SMB2_SIGNATURE_SIZE;
	}

	if ((server->cipher_type == SMB2_ENCRYPTION_AES128_GCM) ||
	    (server->cipher_type == SMB2_ENCRYPTION_AES256_GCM))
		memcpy(iv, (char *)tr_hdr->Nonce, SMB3_AES_GCM_NONCE);
	else {
		iv[0] = 3;
		memcpy(iv + 1, (char *)tr_hdr->Nonce, SMB3_AES_CCM_NONCE);
	}

	aead_request_set_tfm(req, tfm);
	aead_request_set_crypt(req, sg, sg, crypt_len, iv);
	aead_request_set_ad(req, assoc_data_len);

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  crypto_req_done, &wait);

	rc = crypto_wait_req(enc ? crypto_aead_encrypt(req)
				: crypto_aead_decrypt(req), &wait);

	if (!rc && enc)
		memcpy(&tr_hdr->Signature, sign, SMB2_SIGNATURE_SIZE);

	kvfree_sensitive(creq, sensitive_size);
	return rc;
}

/*
 * Clear a read buffer, discarding the folios which have XA_MARK_0 set.
 */
static void cifs_clear_xarray_buffer(struct xarray *buffer)
{
	struct folio *folio;

	XA_STATE(xas, buffer, 0);

	rcu_read_lock();
	xas_for_each_marked(&xas, folio, ULONG_MAX, XA_MARK_0) {
		folio_put(folio);
	}
	rcu_read_unlock();
	xa_destroy(buffer);
}

void
smb3_free_compound_rqst(int num_rqst, struct smb_rqst *rqst)
{
	int i;

	for (i = 0; i < num_rqst; i++)
		if (!xa_empty(&rqst[i].rq_buffer))
			cifs_clear_xarray_buffer(&rqst[i].rq_buffer);
}

/*
 * This function will initialize new_rq and encrypt the content.
 * The first entry, new_rq[0], only contains a single iov which contains
 * a smb2_transform_hdr and is pre-allocated by the caller.
 * This function then populates new_rq[1+] with the content from olq_rq[0+].
 *
 * The end result is an array of smb_rqst structures where the first structure
 * only contains a single iov for the transform header which we then can pass
 * to crypt_message().
 *
 * new_rq[0].rq_iov[0] :  smb2_transform_hdr pre-allocated by the caller
 * new_rq[1+].rq_iov[*] == old_rq[0+].rq_iov[*] : SMB2/3 requests
 */
static int
smb3_init_transform_rq(struct TCP_Server_Info *server, int num_rqst,
		       struct smb_rqst *new_rq, struct smb_rqst *old_rq)
{
	struct smb2_transform_hdr *tr_hdr = new_rq[0].rq_iov[0].iov_base;
	struct page *page;
	unsigned int orig_len = 0;
	int i, j;
	int rc = -ENOMEM;

	for (i = 1; i < num_rqst; i++) {
		struct smb_rqst *old = &old_rq[i - 1];
		struct smb_rqst *new = &new_rq[i];
		struct xarray *buffer = &new->rq_buffer;
		size_t size = iov_iter_count(&old->rq_iter), seg, copied = 0;

		orig_len += smb_rqst_len(server, old);
		new->rq_iov = old->rq_iov;
		new->rq_nvec = old->rq_nvec;

		xa_init(buffer);

		if (size > 0) {
			unsigned int npages = DIV_ROUND_UP(size, PAGE_SIZE);

			for (j = 0; j < npages; j++) {
				void *o;

				rc = -ENOMEM;
				page = alloc_page(GFP_KERNEL|__GFP_HIGHMEM);
				if (!page)
					goto err_free;
				page->index = j;
				o = xa_store(buffer, j, page, GFP_KERNEL);
				if (xa_is_err(o)) {
					rc = xa_err(o);
					put_page(page);
					goto err_free;
				}

				xa_set_mark(buffer, j, XA_MARK_0);

				seg = min_t(size_t, size - copied, PAGE_SIZE);
				if (copy_page_from_iter(page, 0, seg, &old->rq_iter) != seg) {
					rc = -EFAULT;
					goto err_free;
				}
				copied += seg;
			}
			iov_iter_xarray(&new->rq_iter, ITER_SOURCE,
					buffer, 0, size);
			new->rq_iter_size = size;
		}
	}

	/* fill the 1st iov with a transform header */
	fill_transform_hdr(tr_hdr, orig_len, old_rq, server->cipher_type);

	rc = crypt_message(server, num_rqst, new_rq, 1);
	cifs_dbg(FYI, "Encrypt message returned %d\n", rc);
	if (rc)
		goto err_free;

	return rc;

err_free:
	smb3_free_compound_rqst(num_rqst - 1, &new_rq[1]);
	return rc;
}

static int
smb3_is_transform_hdr(void *buf)
{
	struct smb2_transform_hdr *trhdr = buf;

	return trhdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM;
}

static int
decrypt_raw_data(struct TCP_Server_Info *server, char *buf,
		 unsigned int buf_data_size, struct iov_iter *iter,
		 bool is_offloaded)
{
	struct kvec iov[2];
	struct smb_rqst rqst = {NULL};
	size_t iter_size = 0;
	int rc;

	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(struct smb2_transform_hdr);
	iov[1].iov_base = buf + sizeof(struct smb2_transform_hdr);
	iov[1].iov_len = buf_data_size;

	rqst.rq_iov = iov;
	rqst.rq_nvec = 2;
	if (iter) {
		rqst.rq_iter = *iter;
		rqst.rq_iter_size = iov_iter_count(iter);
		iter_size = iov_iter_count(iter);
	}

	rc = crypt_message(server, 1, &rqst, 0);
	cifs_dbg(FYI, "Decrypt message returned %d\n", rc);

	if (rc)
		return rc;

	memmove(buf, iov[1].iov_base, buf_data_size);

	if (!is_offloaded)
		server->total_read = buf_data_size + iter_size;

	return rc;
}

static int
cifs_copy_pages_to_iter(struct xarray *pages, unsigned int data_size,
			unsigned int skip, struct iov_iter *iter)
{
	struct page *page;
	unsigned long index;

	xa_for_each(pages, index, page) {
		size_t n, len = min_t(unsigned int, PAGE_SIZE - skip, data_size);

		n = copy_page_to_iter(page, skip, len, iter);
		if (n != len) {
			cifs_dbg(VFS, "%s: something went wrong\n", __func__);
			return -EIO;
		}
		data_size -= n;
		skip = 0;
	}

	return 0;
}

static int
handle_read_data(struct TCP_Server_Info *server, struct mid_q_entry *mid,
		 char *buf, unsigned int buf_len, struct xarray *pages,
		 unsigned int pages_len, bool is_offloaded)
{
	unsigned int data_offset;
	unsigned int data_len;
	unsigned int cur_off;
	unsigned int cur_page_idx;
	unsigned int pad_len;
	struct cifs_readdata *rdata = mid->callback_data;
	struct smb2_hdr *shdr = (struct smb2_hdr *)buf;
	int length;
	bool use_rdma_mr = false;

	if (shdr->Command != SMB2_READ) {
		cifs_server_dbg(VFS, "only big read responses are supported\n");
		return -EOPNOTSUPP;
	}

	if (server->ops->is_session_expired &&
	    server->ops->is_session_expired(buf)) {
		if (!is_offloaded)
			cifs_reconnect(server, true);
		return -1;
	}

	if (server->ops->is_status_pending &&
			server->ops->is_status_pending(buf, server))
		return -1;

	/* set up first two iov to get credits */
	rdata->iov[0].iov_base = buf;
	rdata->iov[0].iov_len = 0;
	rdata->iov[1].iov_base = buf;
	rdata->iov[1].iov_len =
		min_t(unsigned int, buf_len, server->vals->read_rsp_size);
	cifs_dbg(FYI, "0: iov_base=%p iov_len=%zu\n",
		 rdata->iov[0].iov_base, rdata->iov[0].iov_len);
	cifs_dbg(FYI, "1: iov_base=%p iov_len=%zu\n",
		 rdata->iov[1].iov_base, rdata->iov[1].iov_len);

	rdata->result = server->ops->map_error(buf, true);
	if (rdata->result != 0) {
		cifs_dbg(FYI, "%s: server returned error %d\n",
			 __func__, rdata->result);
		/* normal error on read response */
		if (is_offloaded)
			mid->mid_state = MID_RESPONSE_RECEIVED;
		else
			dequeue_mid(mid, false);
		return 0;
	}

	data_offset = server->ops->read_data_offset(buf);
#ifdef CONFIG_CIFS_SMB_DIRECT
	use_rdma_mr = rdata->mr;
#endif
	data_len = server->ops->read_data_length(buf, use_rdma_mr);

	if (data_offset < server->vals->read_rsp_size) {
		/*
		 * win2k8 sometimes sends an offset of 0 when the read
		 * is beyond the EOF. Treat it as if the data starts just after
		 * the header.
		 */
		cifs_dbg(FYI, "%s: data offset (%u) inside read response header\n",
			 __func__, data_offset);
		data_offset = server->vals->read_rsp_size;
	} else if (data_offset > MAX_CIFS_SMALL_BUFFER_SIZE) {
		/* data_offset is beyond the end of smallbuf */
		cifs_dbg(FYI, "%s: data offset (%u) beyond end of smallbuf\n",
			 __func__, data_offset);
		rdata->result = -EIO;
		if (is_offloaded)
			mid->mid_state = MID_RESPONSE_MALFORMED;
		else
			dequeue_mid(mid, rdata->result);
		return 0;
	}

	pad_len = data_offset - server->vals->read_rsp_size;

	if (buf_len <= data_offset) {
		/* read response payload is in pages */
		cur_page_idx = pad_len / PAGE_SIZE;
		cur_off = pad_len % PAGE_SIZE;

		if (cur_page_idx != 0) {
			/* data offset is beyond the 1st page of response */
			cifs_dbg(FYI, "%s: data offset (%u) beyond 1st page of response\n",
				 __func__, data_offset);
			rdata->result = -EIO;
			if (is_offloaded)
				mid->mid_state = MID_RESPONSE_MALFORMED;
			else
				dequeue_mid(mid, rdata->result);
			return 0;
		}

		if (data_len > pages_len - pad_len) {
			/* data_len is corrupt -- discard frame */
			rdata->result = -EIO;
			if (is_offloaded)
				mid->mid_state = MID_RESPONSE_MALFORMED;
			else
				dequeue_mid(mid, rdata->result);
			return 0;
		}

		/* Copy the data to the output I/O iterator. */
		rdata->result = cifs_copy_pages_to_iter(pages, pages_len,
							cur_off, &rdata->iter);
		if (rdata->result != 0) {
			if (is_offloaded)
				mid->mid_state = MID_RESPONSE_MALFORMED;
			else
				dequeue_mid(mid, rdata->result);
			return 0;
		}
		rdata->got_bytes = pages_len;

	} else if (buf_len >= data_offset + data_len) {
		/* read response payload is in buf */
		WARN_ONCE(pages && !xa_empty(pages),
			  "read data can be either in buf or in pages");
		length = copy_to_iter(buf + data_offset, data_len, &rdata->iter);
		if (length < 0)
			return length;
		rdata->got_bytes = data_len;
	} else {
		/* read response payload cannot be in both buf and pages */
		WARN_ONCE(1, "buf can not contain only a part of read data");
		rdata->result = -EIO;
		if (is_offloaded)
			mid->mid_state = MID_RESPONSE_MALFORMED;
		else
			dequeue_mid(mid, rdata->result);
		return 0;
	}

	if (is_offloaded)
		mid->mid_state = MID_RESPONSE_RECEIVED;
	else
		dequeue_mid(mid, false);
	return 0;
}

struct smb2_decrypt_work {
	struct work_struct decrypt;
	struct TCP_Server_Info *server;
	struct xarray buffer;
	char *buf;
	unsigned int len;
};


static void smb2_decrypt_offload(struct work_struct *work)
{
	struct smb2_decrypt_work *dw = container_of(work,
				struct smb2_decrypt_work, decrypt);
	int rc;
	struct mid_q_entry *mid;
	struct iov_iter iter;

	iov_iter_xarray(&iter, ITER_DEST, &dw->buffer, 0, dw->len);
	rc = decrypt_raw_data(dw->server, dw->buf, dw->server->vals->read_rsp_size,
			      &iter, true);
	if (rc) {
		cifs_dbg(VFS, "error decrypting rc=%d\n", rc);
		goto free_pages;
	}

	dw->server->lstrp = jiffies;
	mid = smb2_find_dequeue_mid(dw->server, dw->buf);
	if (mid == NULL)
		cifs_dbg(FYI, "mid not found\n");
	else {
		mid->decrypted = true;
		rc = handle_read_data(dw->server, mid, dw->buf,
				      dw->server->vals->read_rsp_size,
				      &dw->buffer, dw->len,
				      true);
		if (rc >= 0) {
#ifdef CONFIG_CIFS_STATS2
			mid->when_received = jiffies;
#endif
			if (dw->server->ops->is_network_name_deleted)
				dw->server->ops->is_network_name_deleted(dw->buf,
									 dw->server);

			mid->callback(mid);
		} else {
			spin_lock(&dw->server->srv_lock);
			if (dw->server->tcpStatus == CifsNeedReconnect) {
				spin_lock(&dw->server->mid_lock);
				mid->mid_state = MID_RETRY_NEEDED;
				spin_unlock(&dw->server->mid_lock);
				spin_unlock(&dw->server->srv_lock);
				mid->callback(mid);
			} else {
				spin_lock(&dw->server->mid_lock);
				mid->mid_state = MID_REQUEST_SUBMITTED;
				mid->mid_flags &= ~(MID_DELETED);
				list_add_tail(&mid->qhead,
					&dw->server->pending_mid_q);
				spin_unlock(&dw->server->mid_lock);
				spin_unlock(&dw->server->srv_lock);
			}
		}
		release_mid(mid);
	}

free_pages:
	cifs_clear_xarray_buffer(&dw->buffer);
	cifs_small_buf_release(dw->buf);
	kfree(dw);
}


static int
receive_encrypted_read(struct TCP_Server_Info *server, struct mid_q_entry **mid,
		       int *num_mids)
{
	struct page *page;
	char *buf = server->smallbuf;
	struct smb2_transform_hdr *tr_hdr = (struct smb2_transform_hdr *)buf;
	struct iov_iter iter;
	unsigned int len, npages;
	unsigned int buflen = server->pdu_size;
	int rc;
	int i = 0;
	struct smb2_decrypt_work *dw;

	dw = kzalloc(sizeof(struct smb2_decrypt_work), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;
	xa_init(&dw->buffer);
	INIT_WORK(&dw->decrypt, smb2_decrypt_offload);
	dw->server = server;

	*num_mids = 1;
	len = min_t(unsigned int, buflen, server->vals->read_rsp_size +
		sizeof(struct smb2_transform_hdr)) - HEADER_SIZE(server) + 1;

	rc = cifs_read_from_socket(server, buf + HEADER_SIZE(server) - 1, len);
	if (rc < 0)
		goto free_dw;
	server->total_read += rc;

	len = le32_to_cpu(tr_hdr->OriginalMessageSize) -
		server->vals->read_rsp_size;
	dw->len = len;
	npages = DIV_ROUND_UP(len, PAGE_SIZE);

	rc = -ENOMEM;
	for (; i < npages; i++) {
		void *old;

		page = alloc_page(GFP_KERNEL|__GFP_HIGHMEM);
		if (!page)
			goto discard_data;
		page->index = i;
		old = xa_store(&dw->buffer, i, page, GFP_KERNEL);
		if (xa_is_err(old)) {
			rc = xa_err(old);
			put_page(page);
			goto discard_data;
		}
		xa_set_mark(&dw->buffer, i, XA_MARK_0);
	}

	iov_iter_xarray(&iter, ITER_DEST, &dw->buffer, 0, npages * PAGE_SIZE);

	/* Read the data into the buffer and clear excess bufferage. */
	rc = cifs_read_iter_from_socket(server, &iter, dw->len);
	if (rc < 0)
		goto discard_data;

	server->total_read += rc;
	if (rc < npages * PAGE_SIZE)
		iov_iter_zero(npages * PAGE_SIZE - rc, &iter);
	iov_iter_revert(&iter, npages * PAGE_SIZE);
	iov_iter_truncate(&iter, dw->len);

	rc = cifs_discard_remaining_data(server);
	if (rc)
		goto free_pages;

	/*
	 * For large reads, offload to different thread for better performance,
	 * use more cores decrypting which can be expensive
	 */

	if ((server->min_offload) && (server->in_flight > 1) &&
	    (server->pdu_size >= server->min_offload)) {
		dw->buf = server->smallbuf;
		server->smallbuf = (char *)cifs_small_buf_get();

		queue_work(decrypt_wq, &dw->decrypt);
		*num_mids = 0; /* worker thread takes care of finding mid */
		return -1;
	}

	rc = decrypt_raw_data(server, buf, server->vals->read_rsp_size,
			      &iter, false);
	if (rc)
		goto free_pages;

	*mid = smb2_find_mid(server, buf);
	if (*mid == NULL) {
		cifs_dbg(FYI, "mid not found\n");
	} else {
		cifs_dbg(FYI, "mid found\n");
		(*mid)->decrypted = true;
		rc = handle_read_data(server, *mid, buf,
				      server->vals->read_rsp_size,
				      &dw->buffer, dw->len, false);
		if (rc >= 0) {
			if (server->ops->is_network_name_deleted) {
				server->ops->is_network_name_deleted(buf,
								server);
			}
		}
	}

free_pages:
	cifs_clear_xarray_buffer(&dw->buffer);
free_dw:
	kfree(dw);
	return rc;
discard_data:
	cifs_discard_remaining_data(server);
	goto free_pages;
}

static int
receive_encrypted_standard(struct TCP_Server_Info *server,
			   struct mid_q_entry **mids, char **bufs,
			   int *num_mids)
{
	int ret, length;
	char *buf = server->smallbuf;
	struct smb2_hdr *shdr;
	unsigned int pdu_length = server->pdu_size;
	unsigned int buf_size;
	unsigned int next_cmd;
	struct mid_q_entry *mid_entry;
	int next_is_large;
	char *next_buffer = NULL;

	*num_mids = 0;

	/* switch to large buffer if too big for a small one */
	if (pdu_length > MAX_CIFS_SMALL_BUFFER_SIZE) {
		server->large_buf = true;
		memcpy(server->bigbuf, buf, server->total_read);
		buf = server->bigbuf;
	}

	/* now read the rest */
	length = cifs_read_from_socket(server, buf + HEADER_SIZE(server) - 1,
				pdu_length - HEADER_SIZE(server) + 1);
	if (length < 0)
		return length;
	server->total_read += length;

	buf_size = pdu_length - sizeof(struct smb2_transform_hdr);
	length = decrypt_raw_data(server, buf, buf_size, NULL, false);
	if (length)
		return length;

	next_is_large = server->large_buf;
one_more:
	shdr = (struct smb2_hdr *)buf;
	next_cmd = le32_to_cpu(shdr->NextCommand);
	if (next_cmd) {
		if (WARN_ON_ONCE(next_cmd > pdu_length))
			return -1;
		if (next_is_large)
			next_buffer = (char *)cifs_buf_get();
		else
			next_buffer = (char *)cifs_small_buf_get();
		memcpy(next_buffer, buf + next_cmd, pdu_length - next_cmd);
	}

	mid_entry = smb2_find_mid(server, buf);
	if (mid_entry == NULL)
		cifs_dbg(FYI, "mid not found\n");
	else {
		cifs_dbg(FYI, "mid found\n");
		mid_entry->decrypted = true;
		mid_entry->resp_buf_size = server->pdu_size;
	}

	if (*num_mids >= MAX_COMPOUND) {
		cifs_server_dbg(VFS, "too many PDUs in compound\n");
		return -1;
	}
	bufs[*num_mids] = buf;
	mids[(*num_mids)++] = mid_entry;

	if (mid_entry && mid_entry->handle)
		ret = mid_entry->handle(server, mid_entry);
	else
		ret = cifs_handle_standard(server, mid_entry);

	if (ret == 0 && next_cmd) {
		pdu_length -= next_cmd;
		server->large_buf = next_is_large;
		if (next_is_large)
			server->bigbuf = buf = next_buffer;
		else
			server->smallbuf = buf = next_buffer;
		goto one_more;
	} else if (ret != 0) {
		/*
		 * ret != 0 here means that we didn't get to handle_mid() thus
		 * server->smallbuf and server->bigbuf are still valid. We need
		 * to free next_buffer because it is not going to be used
		 * anywhere.
		 */
		if (next_is_large)
			free_rsp_buf(CIFS_LARGE_BUFFER, next_buffer);
		else
			free_rsp_buf(CIFS_SMALL_BUFFER, next_buffer);
	}

	return ret;
}

static int
smb3_receive_transform(struct TCP_Server_Info *server,
		       struct mid_q_entry **mids, char **bufs, int *num_mids)
{
	char *buf = server->smallbuf;
	unsigned int pdu_length = server->pdu_size;
	struct smb2_transform_hdr *tr_hdr = (struct smb2_transform_hdr *)buf;
	unsigned int orig_len = le32_to_cpu(tr_hdr->OriginalMessageSize);

	if (pdu_length < sizeof(struct smb2_transform_hdr) +
						sizeof(struct smb2_hdr)) {
		cifs_server_dbg(VFS, "Transform message is too small (%u)\n",
			 pdu_length);
		cifs_reconnect(server, true);
		return -ECONNABORTED;
	}

	if (pdu_length < orig_len + sizeof(struct smb2_transform_hdr)) {
		cifs_server_dbg(VFS, "Transform message is broken\n");
		cifs_reconnect(server, true);
		return -ECONNABORTED;
	}

	/* TODO: add support for compounds containing READ. */
	if (pdu_length > CIFSMaxBufSize + MAX_HEADER_SIZE(server)) {
		return receive_encrypted_read(server, &mids[0], num_mids);
	}

	return receive_encrypted_standard(server, mids, bufs, num_mids);
}

int
smb3_handle_read_data(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	char *buf = server->large_buf ? server->bigbuf : server->smallbuf;

	return handle_read_data(server, mid, buf, server->pdu_size,
				NULL, 0, false);
}

static int
smb2_next_header(char *buf)
{
	struct smb2_hdr *hdr = (struct smb2_hdr *)buf;
	struct smb2_transform_hdr *t_hdr = (struct smb2_transform_hdr *)buf;

	if (hdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM)
		return sizeof(struct smb2_transform_hdr) +
		  le32_to_cpu(t_hdr->OriginalMessageSize);

	return le32_to_cpu(hdr->NextCommand);
}

int cifs_sfu_make_node(unsigned int xid, struct inode *inode,
		       struct dentry *dentry, struct cifs_tcon *tcon,
		       const char *full_path, umode_t mode, dev_t dev)
{
	struct cifs_open_info_data buf = {};
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifs_open_parms oparms;
	struct cifs_io_parms io_parms = {};
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_fid fid;
	unsigned int bytes_written;
	struct win_dev *pdev;
	struct kvec iov[2];
	__u32 oplock = server->oplocks ? REQ_OPLOCK : 0;
	int rc;

	if (!S_ISCHR(mode) && !S_ISBLK(mode) && !S_ISFIFO(mode))
		return -EPERM;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.cifs_sb = cifs_sb,
		.desired_access = GENERIC_WRITE,
		.create_options = cifs_create_options(cifs_sb, CREATE_NOT_DIR |
						      CREATE_OPTION_SPECIAL),
		.disposition = FILE_CREATE,
		.path = full_path,
		.fid = &fid,
	};

	rc = server->ops->open(xid, &oparms, &oplock, &buf);
	if (rc)
		return rc;

	/*
	 * BB Do not bother to decode buf since no local inode yet to put
	 * timestamps in, but we can reuse it safely.
	 */
	pdev = (struct win_dev *)&buf.fi;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.length = sizeof(*pdev);
	iov[1].iov_base = pdev;
	iov[1].iov_len = sizeof(*pdev);
	if (S_ISCHR(mode)) {
		memcpy(pdev->type, "IntxCHR", 8);
		pdev->major = cpu_to_le64(MAJOR(dev));
		pdev->minor = cpu_to_le64(MINOR(dev));
	} else if (S_ISBLK(mode)) {
		memcpy(pdev->type, "IntxBLK", 8);
		pdev->major = cpu_to_le64(MAJOR(dev));
		pdev->minor = cpu_to_le64(MINOR(dev));
	} else if (S_ISFIFO(mode)) {
		memcpy(pdev->type, "LnxFIFO", 8);
	}

	rc = server->ops->sync_write(xid, &fid, &io_parms,
				     &bytes_written, iov, 1);
	server->ops->close(xid, tcon, &fid);
	d_drop(dentry);
	/* FIXME: add code here to set EAs */
	cifs_free_open_info(&buf);
	return rc;
}

static int smb2_make_node(unsigned int xid, struct inode *inode,
			  struct dentry *dentry, struct cifs_tcon *tcon,
			  const char *full_path, umode_t mode, dev_t dev)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	/*
	 * Check if mounted with mount parm 'sfu' mount parm.
	 * SFU emulation should work with all servers, but only
	 * supports block and char device (no socket & fifo),
	 * and was used by default in earlier versions of Windows
	 */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL))
		return -EPERM;
	/*
	 * TODO: Add ability to create instead via reparse point. Windows (e.g.
	 * their current NFS server) uses this approach to expose special files
	 * over SMB2/SMB3 and Samba will do this with SMB3.1.1 POSIX Extensions
	 */
	return cifs_sfu_make_node(xid, inode, dentry, tcon,
				  full_path, mode, dev);
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
struct smb_version_operations smb20_operations = {
	.compare_fids = smb2_compare_fids,
	.setup_request = smb2_setup_request,
	.setup_async_request = smb2_setup_async_request,
	.check_receive = smb2_check_receive,
	.add_credits = smb2_add_credits,
	.set_credits = smb2_set_credits,
	.get_credits_field = smb2_get_credits_field,
	.get_credits = smb2_get_credits,
	.wait_mtu_credits = cifs_wait_mtu_credits,
	.get_next_mid = smb2_get_next_mid,
	.revert_current_mid = smb2_revert_current_mid,
	.read_data_offset = smb2_read_data_offset,
	.read_data_length = smb2_read_data_length,
	.map_error = map_smb2_to_linux_error,
	.find_mid = smb2_find_mid,
	.check_message = smb2_check_message,
	.dump_detail = smb2_dump_detail,
	.clear_stats = smb2_clear_stats,
	.print_stats = smb2_print_stats,
	.is_oplock_break = smb2_is_valid_oplock_break,
	.handle_cancelled_mid = smb2_handle_cancelled_mid,
	.downgrade_oplock = smb2_downgrade_oplock,
	.need_neg = smb2_need_neg,
	.negotiate = smb2_negotiate,
	.negotiate_wsize = smb2_negotiate_wsize,
	.negotiate_rsize = smb2_negotiate_rsize,
	.sess_setup = SMB2_sess_setup,
	.logoff = SMB2_logoff,
	.tree_connect = SMB2_tcon,
	.tree_disconnect = SMB2_tdis,
	.qfs_tcon = smb2_qfs_tcon,
	.is_path_accessible = smb2_is_path_accessible,
	.can_echo = smb2_can_echo,
	.echo = SMB2_echo,
	.query_path_info = smb2_query_path_info,
	.query_reparse_point = smb2_query_reparse_point,
	.get_srv_inum = smb2_get_srv_inum,
	.query_file_info = smb2_query_file_info,
	.set_path_size = smb2_set_path_size,
	.set_file_size = smb2_set_file_size,
	.set_file_info = smb2_set_file_info,
	.set_compression = smb2_set_compression,
	.mkdir = smb2_mkdir,
	.mkdir_setinfo = smb2_mkdir_setinfo,
	.rmdir = smb2_rmdir,
	.unlink = smb2_unlink,
	.rename = smb2_rename_path,
	.create_hardlink = smb2_create_hardlink,
	.parse_reparse_point = smb2_parse_reparse_point,
	.query_mf_symlink = smb3_query_mf_symlink,
	.create_mf_symlink = smb3_create_mf_symlink,
	.open = smb2_open_file,
	.set_fid = smb2_set_fid,
	.close = smb2_close_file,
	.flush = smb2_flush_file,
	.async_readv = smb2_async_readv,
	.async_writev = smb2_async_writev,
	.sync_read = smb2_sync_read,
	.sync_write = smb2_sync_write,
	.query_dir_first = smb2_query_dir_first,
	.query_dir_next = smb2_query_dir_next,
	.close_dir = smb2_close_dir,
	.calc_smb_size = smb2_calc_size,
	.is_status_pending = smb2_is_status_pending,
	.is_session_expired = smb2_is_session_expired,
	.oplock_response = smb2_oplock_response,
	.queryfs = smb2_queryfs,
	.mand_lock = smb2_mand_lock,
	.mand_unlock_range = smb2_unlock_range,
	.push_mand_locks = smb2_push_mandatory_locks,
	.get_lease_key = smb2_get_lease_key,
	.set_lease_key = smb2_set_lease_key,
	.new_lease_key = smb2_new_lease_key,
	.calc_signature = smb2_calc_signature,
	.is_read_op = smb2_is_read_op,
	.set_oplock_level = smb2_set_oplock_level,
	.create_lease_buf = smb2_create_lease_buf,
	.parse_lease_buf = smb2_parse_lease_buf,
	.copychunk_range = smb2_copychunk_range,
	.wp_retry_size = smb2_wp_retry_size,
	.dir_needs_close = smb2_dir_needs_close,
	.get_dfs_refer = smb2_get_dfs_refer,
	.select_sectype = smb2_select_sectype,
#ifdef CONFIG_CIFS_XATTR
	.query_all_EAs = smb2_query_eas,
	.set_EA = smb2_set_ea,
#endif /* CIFS_XATTR */
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
	.make_node = smb2_make_node,
	.fiemap = smb3_fiemap,
	.llseek = smb3_llseek,
	.is_status_io_timeout = smb2_is_status_io_timeout,
	.is_network_name_deleted = smb2_is_network_name_deleted,
};
#endif /* CIFS_ALLOW_INSECURE_LEGACY */

struct smb_version_operations smb21_operations = {
	.compare_fids = smb2_compare_fids,
	.setup_request = smb2_setup_request,
	.setup_async_request = smb2_setup_async_request,
	.check_receive = smb2_check_receive,
	.add_credits = smb2_add_credits,
	.set_credits = smb2_set_credits,
	.get_credits_field = smb2_get_credits_field,
	.get_credits = smb2_get_credits,
	.wait_mtu_credits = smb2_wait_mtu_credits,
	.adjust_credits = smb2_adjust_credits,
	.get_next_mid = smb2_get_next_mid,
	.revert_current_mid = smb2_revert_current_mid,
	.read_data_offset = smb2_read_data_offset,
	.read_data_length = smb2_read_data_length,
	.map_error = map_smb2_to_linux_error,
	.find_mid = smb2_find_mid,
	.check_message = smb2_check_message,
	.dump_detail = smb2_dump_detail,
	.clear_stats = smb2_clear_stats,
	.print_stats = smb2_print_stats,
	.is_oplock_break = smb2_is_valid_oplock_break,
	.handle_cancelled_mid = smb2_handle_cancelled_mid,
	.downgrade_oplock = smb2_downgrade_oplock,
	.need_neg = smb2_need_neg,
	.negotiate = smb2_negotiate,
	.negotiate_wsize = smb2_negotiate_wsize,
	.negotiate_rsize = smb2_negotiate_rsize,
	.sess_setup = SMB2_sess_setup,
	.logoff = SMB2_logoff,
	.tree_connect = SMB2_tcon,
	.tree_disconnect = SMB2_tdis,
	.qfs_tcon = smb2_qfs_tcon,
	.is_path_accessible = smb2_is_path_accessible,
	.can_echo = smb2_can_echo,
	.echo = SMB2_echo,
	.query_path_info = smb2_query_path_info,
	.query_reparse_point = smb2_query_reparse_point,
	.get_srv_inum = smb2_get_srv_inum,
	.query_file_info = smb2_query_file_info,
	.set_path_size = smb2_set_path_size,
	.set_file_size = smb2_set_file_size,
	.set_file_info = smb2_set_file_info,
	.set_compression = smb2_set_compression,
	.mkdir = smb2_mkdir,
	.mkdir_setinfo = smb2_mkdir_setinfo,
	.rmdir = smb2_rmdir,
	.unlink = smb2_unlink,
	.rename = smb2_rename_path,
	.create_hardlink = smb2_create_hardlink,
	.parse_reparse_point = smb2_parse_reparse_point,
	.query_mf_symlink = smb3_query_mf_symlink,
	.create_mf_symlink = smb3_create_mf_symlink,
	.open = smb2_open_file,
	.set_fid = smb2_set_fid,
	.close = smb2_close_file,
	.flush = smb2_flush_file,
	.async_readv = smb2_async_readv,
	.async_writev = smb2_async_writev,
	.sync_read = smb2_sync_read,
	.sync_write = smb2_sync_write,
	.query_dir_first = smb2_query_dir_first,
	.query_dir_next = smb2_query_dir_next,
	.close_dir = smb2_close_dir,
	.calc_smb_size = smb2_calc_size,
	.is_status_pending = smb2_is_status_pending,
	.is_session_expired = smb2_is_session_expired,
	.oplock_response = smb2_oplock_response,
	.queryfs = smb2_queryfs,
	.mand_lock = smb2_mand_lock,
	.mand_unlock_range = smb2_unlock_range,
	.push_mand_locks = smb2_push_mandatory_locks,
	.get_lease_key = smb2_get_lease_key,
	.set_lease_key = smb2_set_lease_key,
	.new_lease_key = smb2_new_lease_key,
	.calc_signature = smb2_calc_signature,
	.is_read_op = smb21_is_read_op,
	.set_oplock_level = smb21_set_oplock_level,
	.create_lease_buf = smb2_create_lease_buf,
	.parse_lease_buf = smb2_parse_lease_buf,
	.copychunk_range = smb2_copychunk_range,
	.wp_retry_size = smb2_wp_retry_size,
	.dir_needs_close = smb2_dir_needs_close,
	.enum_snapshots = smb3_enum_snapshots,
	.notify = smb3_notify,
	.get_dfs_refer = smb2_get_dfs_refer,
	.select_sectype = smb2_select_sectype,
#ifdef CONFIG_CIFS_XATTR
	.query_all_EAs = smb2_query_eas,
	.set_EA = smb2_set_ea,
#endif /* CIFS_XATTR */
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
	.make_node = smb2_make_node,
	.fiemap = smb3_fiemap,
	.llseek = smb3_llseek,
	.is_status_io_timeout = smb2_is_status_io_timeout,
	.is_network_name_deleted = smb2_is_network_name_deleted,
};

struct smb_version_operations smb30_operations = {
	.compare_fids = smb2_compare_fids,
	.setup_request = smb2_setup_request,
	.setup_async_request = smb2_setup_async_request,
	.check_receive = smb2_check_receive,
	.add_credits = smb2_add_credits,
	.set_credits = smb2_set_credits,
	.get_credits_field = smb2_get_credits_field,
	.get_credits = smb2_get_credits,
	.wait_mtu_credits = smb2_wait_mtu_credits,
	.adjust_credits = smb2_adjust_credits,
	.get_next_mid = smb2_get_next_mid,
	.revert_current_mid = smb2_revert_current_mid,
	.read_data_offset = smb2_read_data_offset,
	.read_data_length = smb2_read_data_length,
	.map_error = map_smb2_to_linux_error,
	.find_mid = smb2_find_mid,
	.check_message = smb2_check_message,
	.dump_detail = smb2_dump_detail,
	.clear_stats = smb2_clear_stats,
	.print_stats = smb2_print_stats,
	.dump_share_caps = smb2_dump_share_caps,
	.is_oplock_break = smb2_is_valid_oplock_break,
	.handle_cancelled_mid = smb2_handle_cancelled_mid,
	.downgrade_oplock = smb3_downgrade_oplock,
	.need_neg = smb2_need_neg,
	.negotiate = smb2_negotiate,
	.negotiate_wsize = smb3_negotiate_wsize,
	.negotiate_rsize = smb3_negotiate_rsize,
	.sess_setup = SMB2_sess_setup,
	.logoff = SMB2_logoff,
	.tree_connect = SMB2_tcon,
	.tree_disconnect = SMB2_tdis,
	.qfs_tcon = smb3_qfs_tcon,
	.is_path_accessible = smb2_is_path_accessible,
	.can_echo = smb2_can_echo,
	.echo = SMB2_echo,
	.query_path_info = smb2_query_path_info,
	/* WSL tags introduced long after smb2.1, enable for SMB3, 3.11 only */
	.query_reparse_point = smb2_query_reparse_point,
	.get_srv_inum = smb2_get_srv_inum,
	.query_file_info = smb2_query_file_info,
	.set_path_size = smb2_set_path_size,
	.set_file_size = smb2_set_file_size,
	.set_file_info = smb2_set_file_info,
	.set_compression = smb2_set_compression,
	.mkdir = smb2_mkdir,
	.mkdir_setinfo = smb2_mkdir_setinfo,
	.rmdir = smb2_rmdir,
	.unlink = smb2_unlink,
	.rename = smb2_rename_path,
	.create_hardlink = smb2_create_hardlink,
	.parse_reparse_point = smb2_parse_reparse_point,
	.query_mf_symlink = smb3_query_mf_symlink,
	.create_mf_symlink = smb3_create_mf_symlink,
	.open = smb2_open_file,
	.set_fid = smb2_set_fid,
	.close = smb2_close_file,
	.close_getattr = smb2_close_getattr,
	.flush = smb2_flush_file,
	.async_readv = smb2_async_readv,
	.async_writev = smb2_async_writev,
	.sync_read = smb2_sync_read,
	.sync_write = smb2_sync_write,
	.query_dir_first = smb2_query_dir_first,
	.query_dir_next = smb2_query_dir_next,
	.close_dir = smb2_close_dir,
	.calc_smb_size = smb2_calc_size,
	.is_status_pending = smb2_is_status_pending,
	.is_session_expired = smb2_is_session_expired,
	.oplock_response = smb2_oplock_response,
	.queryfs = smb2_queryfs,
	.mand_lock = smb2_mand_lock,
	.mand_unlock_range = smb2_unlock_range,
	.push_mand_locks = smb2_push_mandatory_locks,
	.get_lease_key = smb2_get_lease_key,
	.set_lease_key = smb2_set_lease_key,
	.new_lease_key = smb2_new_lease_key,
	.generate_signingkey = generate_smb30signingkey,
	.calc_signature = smb3_calc_signature,
	.set_integrity  = smb3_set_integrity,
	.is_read_op = smb21_is_read_op,
	.set_oplock_level = smb3_set_oplock_level,
	.create_lease_buf = smb3_create_lease_buf,
	.parse_lease_buf = smb3_parse_lease_buf,
	.copychunk_range = smb2_copychunk_range,
	.duplicate_extents = smb2_duplicate_extents,
	.validate_negotiate = smb3_validate_negotiate,
	.wp_retry_size = smb2_wp_retry_size,
	.dir_needs_close = smb2_dir_needs_close,
	.fallocate = smb3_fallocate,
	.enum_snapshots = smb3_enum_snapshots,
	.notify = smb3_notify,
	.init_transform_rq = smb3_init_transform_rq,
	.is_transform_hdr = smb3_is_transform_hdr,
	.receive_transform = smb3_receive_transform,
	.get_dfs_refer = smb2_get_dfs_refer,
	.select_sectype = smb2_select_sectype,
#ifdef CONFIG_CIFS_XATTR
	.query_all_EAs = smb2_query_eas,
	.set_EA = smb2_set_ea,
#endif /* CIFS_XATTR */
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
	.make_node = smb2_make_node,
	.fiemap = smb3_fiemap,
	.llseek = smb3_llseek,
	.is_status_io_timeout = smb2_is_status_io_timeout,
	.is_network_name_deleted = smb2_is_network_name_deleted,
};

struct smb_version_operations smb311_operations = {
	.compare_fids = smb2_compare_fids,
	.setup_request = smb2_setup_request,
	.setup_async_request = smb2_setup_async_request,
	.check_receive = smb2_check_receive,
	.add_credits = smb2_add_credits,
	.set_credits = smb2_set_credits,
	.get_credits_field = smb2_get_credits_field,
	.get_credits = smb2_get_credits,
	.wait_mtu_credits = smb2_wait_mtu_credits,
	.adjust_credits = smb2_adjust_credits,
	.get_next_mid = smb2_get_next_mid,
	.revert_current_mid = smb2_revert_current_mid,
	.read_data_offset = smb2_read_data_offset,
	.read_data_length = smb2_read_data_length,
	.map_error = map_smb2_to_linux_error,
	.find_mid = smb2_find_mid,
	.check_message = smb2_check_message,
	.dump_detail = smb2_dump_detail,
	.clear_stats = smb2_clear_stats,
	.print_stats = smb2_print_stats,
	.dump_share_caps = smb2_dump_share_caps,
	.is_oplock_break = smb2_is_valid_oplock_break,
	.handle_cancelled_mid = smb2_handle_cancelled_mid,
	.downgrade_oplock = smb3_downgrade_oplock,
	.need_neg = smb2_need_neg,
	.negotiate = smb2_negotiate,
	.negotiate_wsize = smb3_negotiate_wsize,
	.negotiate_rsize = smb3_negotiate_rsize,
	.sess_setup = SMB2_sess_setup,
	.logoff = SMB2_logoff,
	.tree_connect = SMB2_tcon,
	.tree_disconnect = SMB2_tdis,
	.qfs_tcon = smb3_qfs_tcon,
	.is_path_accessible = smb2_is_path_accessible,
	.can_echo = smb2_can_echo,
	.echo = SMB2_echo,
	.query_path_info = smb2_query_path_info,
	.query_reparse_point = smb2_query_reparse_point,
	.get_srv_inum = smb2_get_srv_inum,
	.query_file_info = smb2_query_file_info,
	.set_path_size = smb2_set_path_size,
	.set_file_size = smb2_set_file_size,
	.set_file_info = smb2_set_file_info,
	.set_compression = smb2_set_compression,
	.mkdir = smb2_mkdir,
	.mkdir_setinfo = smb2_mkdir_setinfo,
	.posix_mkdir = smb311_posix_mkdir,
	.rmdir = smb2_rmdir,
	.unlink = smb2_unlink,
	.rename = smb2_rename_path,
	.create_hardlink = smb2_create_hardlink,
	.parse_reparse_point = smb2_parse_reparse_point,
	.query_mf_symlink = smb3_query_mf_symlink,
	.create_mf_symlink = smb3_create_mf_symlink,
	.open = smb2_open_file,
	.set_fid = smb2_set_fid,
	.close = smb2_close_file,
	.close_getattr = smb2_close_getattr,
	.flush = smb2_flush_file,
	.async_readv = smb2_async_readv,
	.async_writev = smb2_async_writev,
	.sync_read = smb2_sync_read,
	.sync_write = smb2_sync_write,
	.query_dir_first = smb2_query_dir_first,
	.query_dir_next = smb2_query_dir_next,
	.close_dir = smb2_close_dir,
	.calc_smb_size = smb2_calc_size,
	.is_status_pending = smb2_is_status_pending,
	.is_session_expired = smb2_is_session_expired,
	.oplock_response = smb2_oplock_response,
	.queryfs = smb311_queryfs,
	.mand_lock = smb2_mand_lock,
	.mand_unlock_range = smb2_unlock_range,
	.push_mand_locks = smb2_push_mandatory_locks,
	.get_lease_key = smb2_get_lease_key,
	.set_lease_key = smb2_set_lease_key,
	.new_lease_key = smb2_new_lease_key,
	.generate_signingkey = generate_smb311signingkey,
	.calc_signature = smb3_calc_signature,
	.set_integrity  = smb3_set_integrity,
	.is_read_op = smb21_is_read_op,
	.set_oplock_level = smb3_set_oplock_level,
	.create_lease_buf = smb3_create_lease_buf,
	.parse_lease_buf = smb3_parse_lease_buf,
	.copychunk_range = smb2_copychunk_range,
	.duplicate_extents = smb2_duplicate_extents,
/*	.validate_negotiate = smb3_validate_negotiate, */ /* not used in 3.11 */
	.wp_retry_size = smb2_wp_retry_size,
	.dir_needs_close = smb2_dir_needs_close,
	.fallocate = smb3_fallocate,
	.enum_snapshots = smb3_enum_snapshots,
	.notify = smb3_notify,
	.init_transform_rq = smb3_init_transform_rq,
	.is_transform_hdr = smb3_is_transform_hdr,
	.receive_transform = smb3_receive_transform,
	.get_dfs_refer = smb2_get_dfs_refer,
	.select_sectype = smb2_select_sectype,
#ifdef CONFIG_CIFS_XATTR
	.query_all_EAs = smb2_query_eas,
	.set_EA = smb2_set_ea,
#endif /* CIFS_XATTR */
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
	.make_node = smb2_make_node,
	.fiemap = smb3_fiemap,
	.llseek = smb3_llseek,
	.is_status_io_timeout = smb2_is_status_io_timeout,
	.is_network_name_deleted = smb2_is_network_name_deleted,
};

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
struct smb_version_values smb20_values = {
	.version_string = SMB20_VERSION_STRING,
	.protocol_id = SMB20_PROT_ID,
	.req_capabilities = 0, /* MBZ */
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease),
};
#endif /* ALLOW_INSECURE_LEGACY */

struct smb_version_values smb21_values = {
	.version_string = SMB21_VERSION_STRING,
	.protocol_id = SMB21_PROT_ID,
	.req_capabilities = 0, /* MBZ on negotiate req until SMB3 dialect */
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease),
};

struct smb_version_values smb3any_values = {
	.version_string = SMB3ANY_VERSION_STRING,
	.protocol_id = SMB302_PROT_ID, /* doesn't matter, send protocol array */
	.req_capabilities = SMB2_GLOBAL_CAP_DFS | SMB2_GLOBAL_CAP_LEASING | SMB2_GLOBAL_CAP_LARGE_MTU | SMB2_GLOBAL_CAP_PERSISTENT_HANDLES | SMB2_GLOBAL_CAP_ENCRYPTION | SMB2_GLOBAL_CAP_DIRECTORY_LEASING,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease_v2),
};

struct smb_version_values smbdefault_values = {
	.version_string = SMBDEFAULT_VERSION_STRING,
	.protocol_id = SMB302_PROT_ID, /* doesn't matter, send protocol array */
	.req_capabilities = SMB2_GLOBAL_CAP_DFS | SMB2_GLOBAL_CAP_LEASING | SMB2_GLOBAL_CAP_LARGE_MTU | SMB2_GLOBAL_CAP_PERSISTENT_HANDLES | SMB2_GLOBAL_CAP_ENCRYPTION | SMB2_GLOBAL_CAP_DIRECTORY_LEASING,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease_v2),
};

struct smb_version_values smb30_values = {
	.version_string = SMB30_VERSION_STRING,
	.protocol_id = SMB30_PROT_ID,
	.req_capabilities = SMB2_GLOBAL_CAP_DFS | SMB2_GLOBAL_CAP_LEASING | SMB2_GLOBAL_CAP_LARGE_MTU | SMB2_GLOBAL_CAP_PERSISTENT_HANDLES | SMB2_GLOBAL_CAP_ENCRYPTION | SMB2_GLOBAL_CAP_DIRECTORY_LEASING,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease_v2),
};

struct smb_version_values smb302_values = {
	.version_string = SMB302_VERSION_STRING,
	.protocol_id = SMB302_PROT_ID,
	.req_capabilities = SMB2_GLOBAL_CAP_DFS | SMB2_GLOBAL_CAP_LEASING | SMB2_GLOBAL_CAP_LARGE_MTU | SMB2_GLOBAL_CAP_PERSISTENT_HANDLES | SMB2_GLOBAL_CAP_ENCRYPTION | SMB2_GLOBAL_CAP_DIRECTORY_LEASING,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease_v2),
};

struct smb_version_values smb311_values = {
	.version_string = SMB311_VERSION_STRING,
	.protocol_id = SMB311_PROT_ID,
	.req_capabilities = SMB2_GLOBAL_CAP_DFS | SMB2_GLOBAL_CAP_LEASING | SMB2_GLOBAL_CAP_LARGE_MTU | SMB2_GLOBAL_CAP_PERSISTENT_HANDLES | SMB2_GLOBAL_CAP_ENCRYPTION | SMB2_GLOBAL_CAP_DIRECTORY_LEASING,
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp),
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease_v2),
};
