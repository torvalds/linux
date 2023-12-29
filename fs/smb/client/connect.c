// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2011
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/pagevec.h>
#include <linux/freezer.h>
#include <linux/namei.h>
#include <linux/uuid.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <linux/inet.h>
#include <linux/module.h>
#include <keys/user-type.h>
#include <net/ipv6.h>
#include <linux/parser.h>
#include <linux/bvec.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "ntlmssp.h"
#include "nterr.h"
#include "rfc1002pdu.h"
#include "fscache.h"
#include "smb2proto.h"
#include "smbdirect.h"
#include "dns_resolve.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dfs.h"
#include "dfs_cache.h"
#endif
#include "fs_context.h"
#include "cifs_swn.h"

extern mempool_t *cifs_req_poolp;
extern bool disable_legacy_dialects;

/* FIXME: should these be tunable? */
#define TLINK_ERROR_EXPIRE	(1 * HZ)
#define TLINK_IDLE_EXPIRE	(600 * HZ)

/* Drop the connection to not overload the server */
#define MAX_STATUS_IO_TIMEOUT   5

static int ip_connect(struct TCP_Server_Info *server);
static int generic_ip_connect(struct TCP_Server_Info *server);
static void tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink);
static void cifs_prune_tlinks(struct work_struct *work);

/*
 * Resolve hostname and set ip addr in tcp ses. Useful for hostnames that may
 * get their ip addresses changed at some point.
 *
 * This should be called with server->srv_mutex held.
 */
static int reconn_set_ipaddr_from_hostname(struct TCP_Server_Info *server)
{
	int rc;
	int len;
	char *unc;
	struct sockaddr_storage ss;

	if (!server->hostname)
		return -EINVAL;

	/* if server hostname isn't populated, there's nothing to do here */
	if (server->hostname[0] == '\0')
		return 0;

	len = strlen(server->hostname) + 3;

	unc = kmalloc(len, GFP_KERNEL);
	if (!unc) {
		cifs_dbg(FYI, "%s: failed to create UNC path\n", __func__);
		return -ENOMEM;
	}
	scnprintf(unc, len, "\\\\%s", server->hostname);

	spin_lock(&server->srv_lock);
	ss = server->dstaddr;
	spin_unlock(&server->srv_lock);

	rc = dns_resolve_server_name_to_ip(unc, (struct sockaddr *)&ss, NULL);
	kfree(unc);

	if (rc < 0) {
		cifs_dbg(FYI, "%s: failed to resolve server part of %s to IP: %d\n",
			 __func__, server->hostname, rc);
	} else {
		spin_lock(&server->srv_lock);
		memcpy(&server->dstaddr, &ss, sizeof(server->dstaddr));
		spin_unlock(&server->srv_lock);
		rc = 0;
	}

	return rc;
}

static void smb2_query_server_interfaces(struct work_struct *work)
{
	int rc;
	struct cifs_tcon *tcon = container_of(work,
					struct cifs_tcon,
					query_interfaces.work);

	/*
	 * query server network interfaces, in case they change
	 */
	rc = SMB3_request_interfaces(0, tcon, false);
	if (rc) {
		cifs_dbg(FYI, "%s: failed to query server interfaces: %d\n",
				__func__, rc);
	}

	queue_delayed_work(cifsiod_wq, &tcon->query_interfaces,
			   (SMB_INTERFACE_POLL_INTERVAL * HZ));
}

/*
 * Update the tcpStatus for the server.
 * This is used to signal the cifsd thread to call cifs_reconnect
 * ONLY cifsd thread should call cifs_reconnect. For any other
 * thread, use this function
 *
 * @server: the tcp ses for which reconnect is needed
 * @all_channels: if this needs to be done for all channels
 */
void
cifs_signal_cifsd_for_reconnect(struct TCP_Server_Info *server,
				bool all_channels)
{
	struct TCP_Server_Info *pserver;
	struct cifs_ses *ses;
	int i;

	/* If server is a channel, select the primary channel */
	pserver = SERVER_IS_CHAN(server) ? server->primary_server : server;

	/* if we need to signal just this channel */
	if (!all_channels) {
		spin_lock(&server->srv_lock);
		if (server->tcpStatus != CifsExiting)
			server->tcpStatus = CifsNeedReconnect;
		spin_unlock(&server->srv_lock);
		return;
	}

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &pserver->smb_ses_list, smb_ses_list) {
		spin_lock(&ses->chan_lock);
		for (i = 0; i < ses->chan_count; i++) {
			spin_lock(&ses->chans[i].server->srv_lock);
			ses->chans[i].server->tcpStatus = CifsNeedReconnect;
			spin_unlock(&ses->chans[i].server->srv_lock);
		}
		spin_unlock(&ses->chan_lock);
	}
	spin_unlock(&cifs_tcp_ses_lock);
}

/*
 * Mark all sessions and tcons for reconnect.
 * IMPORTANT: make sure that this gets called only from
 * cifsd thread. For any other thread, use
 * cifs_signal_cifsd_for_reconnect
 *
 * @server: the tcp ses for which reconnect is needed
 * @server needs to be previously set to CifsNeedReconnect.
 * @mark_smb_session: whether even sessions need to be marked
 */
void
cifs_mark_tcp_ses_conns_for_reconnect(struct TCP_Server_Info *server,
				      bool mark_smb_session)
{
	struct TCP_Server_Info *pserver;
	struct cifs_ses *ses, *nses;
	struct cifs_tcon *tcon;

	/*
	 * before reconnecting the tcp session, mark the smb session (uid) and the tid bad so they
	 * are not used until reconnected.
	 */
	cifs_dbg(FYI, "%s: marking necessary sessions and tcons for reconnect\n", __func__);

	/* If server is a channel, select the primary channel */
	pserver = SERVER_IS_CHAN(server) ? server->primary_server : server;


	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry_safe(ses, nses, &pserver->smb_ses_list, smb_ses_list) {
		/* check if iface is still active */
		spin_lock(&ses->chan_lock);
		if (!cifs_chan_is_iface_active(ses, server)) {
			spin_unlock(&ses->chan_lock);
			cifs_chan_update_iface(ses, server);
			spin_lock(&ses->chan_lock);
		}

		if (!mark_smb_session && cifs_chan_needs_reconnect(ses, server)) {
			spin_unlock(&ses->chan_lock);
			continue;
		}

		if (mark_smb_session)
			CIFS_SET_ALL_CHANS_NEED_RECONNECT(ses);
		else
			cifs_chan_set_need_reconnect(ses, server);

		cifs_dbg(FYI, "%s: channel connect bitmap: 0x%lx\n",
			 __func__, ses->chans_need_reconnect);

		/* If all channels need reconnect, then tcon needs reconnect */
		if (!mark_smb_session && !CIFS_ALL_CHANS_NEED_RECONNECT(ses)) {
			spin_unlock(&ses->chan_lock);
			continue;
		}
		spin_unlock(&ses->chan_lock);

		spin_lock(&ses->ses_lock);
		ses->ses_status = SES_NEED_RECON;
		spin_unlock(&ses->ses_lock);

		list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
			tcon->need_reconnect = true;
			spin_lock(&tcon->tc_lock);
			tcon->status = TID_NEED_RECON;
			spin_unlock(&tcon->tc_lock);
		}
		if (ses->tcon_ipc) {
			ses->tcon_ipc->need_reconnect = true;
			spin_lock(&ses->tcon_ipc->tc_lock);
			ses->tcon_ipc->status = TID_NEED_RECON;
			spin_unlock(&ses->tcon_ipc->tc_lock);
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
}

static void
cifs_abort_connection(struct TCP_Server_Info *server)
{
	struct mid_q_entry *mid, *nmid;
	struct list_head retry_list;

	server->maxBuf = 0;
	server->max_read = 0;

	/* do not want to be sending data on a socket we are freeing */
	cifs_dbg(FYI, "%s: tearing down socket\n", __func__);
	cifs_server_lock(server);
	if (server->ssocket) {
		cifs_dbg(FYI, "State: 0x%x Flags: 0x%lx\n", server->ssocket->state,
			 server->ssocket->flags);
		kernel_sock_shutdown(server->ssocket, SHUT_WR);
		cifs_dbg(FYI, "Post shutdown state: 0x%x Flags: 0x%lx\n", server->ssocket->state,
			 server->ssocket->flags);
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}
	server->sequence_number = 0;
	server->session_estab = false;
	kfree_sensitive(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;
	server->lstrp = jiffies;

	/* mark submitted MIDs for retry and issue callback */
	INIT_LIST_HEAD(&retry_list);
	cifs_dbg(FYI, "%s: moving mids to private list\n", __func__);
	spin_lock(&server->mid_lock);
	list_for_each_entry_safe(mid, nmid, &server->pending_mid_q, qhead) {
		kref_get(&mid->refcount);
		if (mid->mid_state == MID_REQUEST_SUBMITTED)
			mid->mid_state = MID_RETRY_NEEDED;
		list_move(&mid->qhead, &retry_list);
		mid->mid_flags |= MID_DELETED;
	}
	spin_unlock(&server->mid_lock);
	cifs_server_unlock(server);

	cifs_dbg(FYI, "%s: issuing mid callbacks\n", __func__);
	list_for_each_entry_safe(mid, nmid, &retry_list, qhead) {
		list_del_init(&mid->qhead);
		mid->callback(mid);
		release_mid(mid);
	}

	if (cifs_rdma_enabled(server)) {
		cifs_server_lock(server);
		smbd_destroy(server);
		cifs_server_unlock(server);
	}
}

static bool cifs_tcp_ses_needs_reconnect(struct TCP_Server_Info *server, int num_targets)
{
	spin_lock(&server->srv_lock);
	server->nr_targets = num_targets;
	if (server->tcpStatus == CifsExiting) {
		/* the demux thread will exit normally next time through the loop */
		spin_unlock(&server->srv_lock);
		wake_up(&server->response_q);
		return false;
	}

	cifs_dbg(FYI, "Mark tcp session as need reconnect\n");
	trace_smb3_reconnect(server->CurrentMid, server->conn_id,
			     server->hostname);
	server->tcpStatus = CifsNeedReconnect;

	spin_unlock(&server->srv_lock);
	return true;
}

/*
 * cifs tcp session reconnection
 *
 * mark tcp session as reconnecting so temporarily locked
 * mark all smb sessions as reconnecting for tcp session
 * reconnect tcp session
 * wake up waiters on reconnection? - (not needed currently)
 *
 * if mark_smb_session is passed as true, unconditionally mark
 * the smb session (and tcon) for reconnect as well. This value
 * doesn't really matter for non-multichannel scenario.
 *
 */
static int __cifs_reconnect(struct TCP_Server_Info *server,
			    bool mark_smb_session)
{
	int rc = 0;

	if (!cifs_tcp_ses_needs_reconnect(server, 1))
		return 0;

	cifs_mark_tcp_ses_conns_for_reconnect(server, mark_smb_session);

	cifs_abort_connection(server);

	do {
		try_to_freeze();
		cifs_server_lock(server);

		if (!cifs_swn_set_server_dstaddr(server)) {
			/* resolve the hostname again to make sure that IP address is up-to-date */
			rc = reconn_set_ipaddr_from_hostname(server);
			cifs_dbg(FYI, "%s: reconn_set_ipaddr_from_hostname: rc=%d\n", __func__, rc);
		}

		if (cifs_rdma_enabled(server))
			rc = smbd_reconnect(server);
		else
			rc = generic_ip_connect(server);
		if (rc) {
			cifs_server_unlock(server);
			cifs_dbg(FYI, "%s: reconnect error %d\n", __func__, rc);
			msleep(3000);
		} else {
			atomic_inc(&tcpSesReconnectCount);
			set_credits(server, 1);
			spin_lock(&server->srv_lock);
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsNeedNegotiate;
			spin_unlock(&server->srv_lock);
			cifs_swn_reset_server_dstaddr(server);
			cifs_server_unlock(server);
			mod_delayed_work(cifsiod_wq, &server->reconnect, 0);
		}
	} while (server->tcpStatus == CifsNeedReconnect);

	spin_lock(&server->srv_lock);
	if (server->tcpStatus == CifsNeedNegotiate)
		mod_delayed_work(cifsiod_wq, &server->echo, 0);
	spin_unlock(&server->srv_lock);

	wake_up(&server->response_q);
	return rc;
}

#ifdef CONFIG_CIFS_DFS_UPCALL
static int __reconnect_target_unlocked(struct TCP_Server_Info *server, const char *target)
{
	int rc;
	char *hostname;

	if (!cifs_swn_set_server_dstaddr(server)) {
		if (server->hostname != target) {
			hostname = extract_hostname(target);
			if (!IS_ERR(hostname)) {
				spin_lock(&server->srv_lock);
				kfree(server->hostname);
				server->hostname = hostname;
				spin_unlock(&server->srv_lock);
			} else {
				cifs_dbg(FYI, "%s: couldn't extract hostname or address from dfs target: %ld\n",
					 __func__, PTR_ERR(hostname));
				cifs_dbg(FYI, "%s: default to last target server: %s\n", __func__,
					 server->hostname);
			}
		}
		/* resolve the hostname again to make sure that IP address is up-to-date. */
		rc = reconn_set_ipaddr_from_hostname(server);
		cifs_dbg(FYI, "%s: reconn_set_ipaddr_from_hostname: rc=%d\n", __func__, rc);
	}
	/* Reconnect the socket */
	if (cifs_rdma_enabled(server))
		rc = smbd_reconnect(server);
	else
		rc = generic_ip_connect(server);

	return rc;
}

static int reconnect_target_unlocked(struct TCP_Server_Info *server, struct dfs_cache_tgt_list *tl,
				     struct dfs_cache_tgt_iterator **target_hint)
{
	int rc;
	struct dfs_cache_tgt_iterator *tit;

	*target_hint = NULL;

	/* If dfs target list is empty, then reconnect to last server */
	tit = dfs_cache_get_tgt_iterator(tl);
	if (!tit)
		return __reconnect_target_unlocked(server, server->hostname);

	/* Otherwise, try every dfs target in @tl */
	for (; tit; tit = dfs_cache_get_next_tgt(tl, tit)) {
		rc = __reconnect_target_unlocked(server, dfs_cache_get_tgt_name(tit));
		if (!rc) {
			*target_hint = tit;
			break;
		}
	}
	return rc;
}

static int reconnect_dfs_server(struct TCP_Server_Info *server)
{
	struct dfs_cache_tgt_iterator *target_hint = NULL;
	DFS_CACHE_TGT_LIST(tl);
	int num_targets = 0;
	int rc = 0;

	/*
	 * Determine the number of dfs targets the referral path in @cifs_sb resolves to.
	 *
	 * smb2_reconnect() needs to know how long it should wait based upon the number of dfs
	 * targets (server->nr_targets).  It's also possible that the cached referral was cleared
	 * through /proc/fs/cifs/dfscache or the target list is empty due to server settings after
	 * refreshing the referral, so, in this case, default it to 1.
	 */
	mutex_lock(&server->refpath_lock);
	if (!dfs_cache_noreq_find(server->leaf_fullpath + 1, NULL, &tl))
		num_targets = dfs_cache_get_nr_tgts(&tl);
	mutex_unlock(&server->refpath_lock);
	if (!num_targets)
		num_targets = 1;

	if (!cifs_tcp_ses_needs_reconnect(server, num_targets))
		return 0;

	/*
	 * Unconditionally mark all sessions & tcons for reconnect as we might be connecting to a
	 * different server or share during failover.  It could be improved by adding some logic to
	 * only do that in case it connects to a different server or share, though.
	 */
	cifs_mark_tcp_ses_conns_for_reconnect(server, true);

	cifs_abort_connection(server);

	do {
		try_to_freeze();
		cifs_server_lock(server);

		rc = reconnect_target_unlocked(server, &tl, &target_hint);
		if (rc) {
			/* Failed to reconnect socket */
			cifs_server_unlock(server);
			cifs_dbg(FYI, "%s: reconnect error %d\n", __func__, rc);
			msleep(3000);
			continue;
		}
		/*
		 * Socket was created.  Update tcp session status to CifsNeedNegotiate so that a
		 * process waiting for reconnect will know it needs to re-establish session and tcon
		 * through the reconnected target server.
		 */
		atomic_inc(&tcpSesReconnectCount);
		set_credits(server, 1);
		spin_lock(&server->srv_lock);
		if (server->tcpStatus != CifsExiting)
			server->tcpStatus = CifsNeedNegotiate;
		spin_unlock(&server->srv_lock);
		cifs_swn_reset_server_dstaddr(server);
		cifs_server_unlock(server);
		mod_delayed_work(cifsiod_wq, &server->reconnect, 0);
	} while (server->tcpStatus == CifsNeedReconnect);

	mutex_lock(&server->refpath_lock);
	dfs_cache_noreq_update_tgthint(server->leaf_fullpath + 1, target_hint);
	mutex_unlock(&server->refpath_lock);
	dfs_cache_free_tgts(&tl);

	/* Need to set up echo worker again once connection has been established */
	spin_lock(&server->srv_lock);
	if (server->tcpStatus == CifsNeedNegotiate)
		mod_delayed_work(cifsiod_wq, &server->echo, 0);
	spin_unlock(&server->srv_lock);

	wake_up(&server->response_q);
	return rc;
}

int cifs_reconnect(struct TCP_Server_Info *server, bool mark_smb_session)
{
	mutex_lock(&server->refpath_lock);
	if (!server->leaf_fullpath) {
		mutex_unlock(&server->refpath_lock);
		return __cifs_reconnect(server, mark_smb_session);
	}
	mutex_unlock(&server->refpath_lock);

	return reconnect_dfs_server(server);
}
#else
int cifs_reconnect(struct TCP_Server_Info *server, bool mark_smb_session)
{
	return __cifs_reconnect(server, mark_smb_session);
}
#endif

static void
cifs_echo_request(struct work_struct *work)
{
	int rc;
	struct TCP_Server_Info *server = container_of(work,
					struct TCP_Server_Info, echo.work);

	/*
	 * We cannot send an echo if it is disabled.
	 * Also, no need to ping if we got a response recently.
	 */

	if (server->tcpStatus == CifsNeedReconnect ||
	    server->tcpStatus == CifsExiting ||
	    server->tcpStatus == CifsNew ||
	    (server->ops->can_echo && !server->ops->can_echo(server)) ||
	    time_before(jiffies, server->lstrp + server->echo_interval - HZ))
		goto requeue_echo;

	rc = server->ops->echo ? server->ops->echo(server) : -ENOSYS;
	cifs_server_dbg(FYI, "send echo request: rc = %d\n", rc);

	/* Check witness registrations */
	cifs_swn_check();

requeue_echo:
	queue_delayed_work(cifsiod_wq, &server->echo, server->echo_interval);
}

static bool
allocate_buffers(struct TCP_Server_Info *server)
{
	if (!server->bigbuf) {
		server->bigbuf = (char *)cifs_buf_get();
		if (!server->bigbuf) {
			cifs_server_dbg(VFS, "No memory for large SMB response\n");
			msleep(3000);
			/* retry will check if exiting */
			return false;
		}
	} else if (server->large_buf) {
		/* we are reusing a dirty large buf, clear its start */
		memset(server->bigbuf, 0, HEADER_SIZE(server));
	}

	if (!server->smallbuf) {
		server->smallbuf = (char *)cifs_small_buf_get();
		if (!server->smallbuf) {
			cifs_server_dbg(VFS, "No memory for SMB response\n");
			msleep(1000);
			/* retry will check if exiting */
			return false;
		}
		/* beginning of smb buffer is cleared in our buf_get */
	} else {
		/* if existing small buf clear beginning */
		memset(server->smallbuf, 0, HEADER_SIZE(server));
	}

	return true;
}

static bool
server_unresponsive(struct TCP_Server_Info *server)
{
	/*
	 * We need to wait 3 echo intervals to make sure we handle such
	 * situations right:
	 * 1s  client sends a normal SMB request
	 * 2s  client gets a response
	 * 30s echo workqueue job pops, and decides we got a response recently
	 *     and don't need to send another
	 * ...
	 * 65s kernel_recvmsg times out, and we see that we haven't gotten
	 *     a response in >60s.
	 */
	spin_lock(&server->srv_lock);
	if ((server->tcpStatus == CifsGood ||
	    server->tcpStatus == CifsNeedNegotiate) &&
	    (!server->ops->can_echo || server->ops->can_echo(server)) &&
	    time_after(jiffies, server->lstrp + 3 * server->echo_interval)) {
		spin_unlock(&server->srv_lock);
		cifs_server_dbg(VFS, "has not responded in %lu seconds. Reconnecting...\n",
			 (3 * server->echo_interval) / HZ);
		cifs_reconnect(server, false);
		return true;
	}
	spin_unlock(&server->srv_lock);

	return false;
}

static inline bool
zero_credits(struct TCP_Server_Info *server)
{
	int val;

	spin_lock(&server->req_lock);
	val = server->credits + server->echo_credits + server->oplock_credits;
	if (server->in_flight == 0 && val == 0) {
		spin_unlock(&server->req_lock);
		return true;
	}
	spin_unlock(&server->req_lock);
	return false;
}

static int
cifs_readv_from_socket(struct TCP_Server_Info *server, struct msghdr *smb_msg)
{
	int length = 0;
	int total_read;

	for (total_read = 0; msg_data_left(smb_msg); total_read += length) {
		try_to_freeze();

		/* reconnect if no credits and no requests in flight */
		if (zero_credits(server)) {
			cifs_reconnect(server, false);
			return -ECONNABORTED;
		}

		if (server_unresponsive(server))
			return -ECONNABORTED;
		if (cifs_rdma_enabled(server) && server->smbd_conn)
			length = smbd_recv(server->smbd_conn, smb_msg);
		else
			length = sock_recvmsg(server->ssocket, smb_msg, 0);

		spin_lock(&server->srv_lock);
		if (server->tcpStatus == CifsExiting) {
			spin_unlock(&server->srv_lock);
			return -ESHUTDOWN;
		}

		if (server->tcpStatus == CifsNeedReconnect) {
			spin_unlock(&server->srv_lock);
			cifs_reconnect(server, false);
			return -ECONNABORTED;
		}
		spin_unlock(&server->srv_lock);

		if (length == -ERESTARTSYS ||
		    length == -EAGAIN ||
		    length == -EINTR) {
			/*
			 * Minimum sleep to prevent looping, allowing socket
			 * to clear and app threads to set tcpStatus
			 * CifsNeedReconnect if server hung.
			 */
			usleep_range(1000, 2000);
			length = 0;
			continue;
		}

		if (length <= 0) {
			cifs_dbg(FYI, "Received no data or error: %d\n", length);
			cifs_reconnect(server, false);
			return -ECONNABORTED;
		}
	}
	return total_read;
}

int
cifs_read_from_socket(struct TCP_Server_Info *server, char *buf,
		      unsigned int to_read)
{
	struct msghdr smb_msg = {};
	struct kvec iov = {.iov_base = buf, .iov_len = to_read};
	iov_iter_kvec(&smb_msg.msg_iter, ITER_DEST, &iov, 1, to_read);

	return cifs_readv_from_socket(server, &smb_msg);
}

ssize_t
cifs_discard_from_socket(struct TCP_Server_Info *server, size_t to_read)
{
	struct msghdr smb_msg = {};

	/*
	 *  iov_iter_discard already sets smb_msg.type and count and iov_offset
	 *  and cifs_readv_from_socket sets msg_control and msg_controllen
	 *  so little to initialize in struct msghdr
	 */
	iov_iter_discard(&smb_msg.msg_iter, ITER_DEST, to_read);

	return cifs_readv_from_socket(server, &smb_msg);
}

int
cifs_read_page_from_socket(struct TCP_Server_Info *server, struct page *page,
	unsigned int page_offset, unsigned int to_read)
{
	struct msghdr smb_msg = {};
	struct bio_vec bv;

	bvec_set_page(&bv, page, to_read, page_offset);
	iov_iter_bvec(&smb_msg.msg_iter, ITER_DEST, &bv, 1, to_read);
	return cifs_readv_from_socket(server, &smb_msg);
}

int
cifs_read_iter_from_socket(struct TCP_Server_Info *server, struct iov_iter *iter,
			   unsigned int to_read)
{
	struct msghdr smb_msg = { .msg_iter = *iter };
	int ret;

	iov_iter_truncate(&smb_msg.msg_iter, to_read);
	ret = cifs_readv_from_socket(server, &smb_msg);
	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

static bool
is_smb_response(struct TCP_Server_Info *server, unsigned char type)
{
	/*
	 * The first byte big endian of the length field,
	 * is actually not part of the length but the type
	 * with the most common, zero, as regular data.
	 */
	switch (type) {
	case RFC1002_SESSION_MESSAGE:
		/* Regular SMB response */
		return true;
	case RFC1002_SESSION_KEEP_ALIVE:
		cifs_dbg(FYI, "RFC 1002 session keep alive\n");
		break;
	case RFC1002_POSITIVE_SESSION_RESPONSE:
		cifs_dbg(FYI, "RFC 1002 positive session response\n");
		break;
	case RFC1002_NEGATIVE_SESSION_RESPONSE:
		/*
		 * We get this from Windows 98 instead of an error on
		 * SMB negprot response.
		 */
		cifs_dbg(FYI, "RFC 1002 negative session response\n");
		/* give server a second to clean up */
		msleep(1000);
		/*
		 * Always try 445 first on reconnect since we get NACK
		 * on some if we ever connected to port 139 (the NACK
		 * is since we do not begin with RFC1001 session
		 * initialize frame).
		 */
		cifs_set_port((struct sockaddr *)&server->dstaddr, CIFS_PORT);
		cifs_reconnect(server, true);
		break;
	default:
		cifs_server_dbg(VFS, "RFC 1002 unknown response type 0x%x\n", type);
		cifs_reconnect(server, true);
	}

	return false;
}

void
dequeue_mid(struct mid_q_entry *mid, bool malformed)
{
#ifdef CONFIG_CIFS_STATS2
	mid->when_received = jiffies;
#endif
	spin_lock(&mid->server->mid_lock);
	if (!malformed)
		mid->mid_state = MID_RESPONSE_RECEIVED;
	else
		mid->mid_state = MID_RESPONSE_MALFORMED;
	/*
	 * Trying to handle/dequeue a mid after the send_recv()
	 * function has finished processing it is a bug.
	 */
	if (mid->mid_flags & MID_DELETED) {
		spin_unlock(&mid->server->mid_lock);
		pr_warn_once("trying to dequeue a deleted mid\n");
	} else {
		list_del_init(&mid->qhead);
		mid->mid_flags |= MID_DELETED;
		spin_unlock(&mid->server->mid_lock);
	}
}

static unsigned int
smb2_get_credits_from_hdr(char *buffer, struct TCP_Server_Info *server)
{
	struct smb2_hdr *shdr = (struct smb2_hdr *)buffer;

	/*
	 * SMB1 does not use credits.
	 */
	if (is_smb1(server))
		return 0;

	return le16_to_cpu(shdr->CreditRequest);
}

static void
handle_mid(struct mid_q_entry *mid, struct TCP_Server_Info *server,
	   char *buf, int malformed)
{
	if (server->ops->check_trans2 &&
	    server->ops->check_trans2(mid, server, buf, malformed))
		return;
	mid->credits_received = smb2_get_credits_from_hdr(buf, server);
	mid->resp_buf = buf;
	mid->large_buf = server->large_buf;
	/* Was previous buf put in mpx struct for multi-rsp? */
	if (!mid->multiRsp) {
		/* smb buffer will be freed by user thread */
		if (server->large_buf)
			server->bigbuf = NULL;
		else
			server->smallbuf = NULL;
	}
	dequeue_mid(mid, malformed);
}

int
cifs_enable_signing(struct TCP_Server_Info *server, bool mnt_sign_required)
{
	bool srv_sign_required = server->sec_mode & server->vals->signing_required;
	bool srv_sign_enabled = server->sec_mode & server->vals->signing_enabled;
	bool mnt_sign_enabled;

	/*
	 * Is signing required by mnt options? If not then check
	 * global_secflags to see if it is there.
	 */
	if (!mnt_sign_required)
		mnt_sign_required = ((global_secflags & CIFSSEC_MUST_SIGN) ==
						CIFSSEC_MUST_SIGN);

	/*
	 * If signing is required then it's automatically enabled too,
	 * otherwise, check to see if the secflags allow it.
	 */
	mnt_sign_enabled = mnt_sign_required ? mnt_sign_required :
				(global_secflags & CIFSSEC_MAY_SIGN);

	/* If server requires signing, does client allow it? */
	if (srv_sign_required) {
		if (!mnt_sign_enabled) {
			cifs_dbg(VFS, "Server requires signing, but it's disabled in SecurityFlags!\n");
			return -EOPNOTSUPP;
		}
		server->sign = true;
	}

	/* If client requires signing, does server allow it? */
	if (mnt_sign_required) {
		if (!srv_sign_enabled) {
			cifs_dbg(VFS, "Server does not support signing!\n");
			return -EOPNOTSUPP;
		}
		server->sign = true;
	}

	if (cifs_rdma_enabled(server) && server->sign)
		cifs_dbg(VFS, "Signing is enabled, and RDMA read/write will be disabled\n");

	return 0;
}

static noinline_for_stack void
clean_demultiplex_info(struct TCP_Server_Info *server)
{
	int length;

	/* take it off the list, if it's not already */
	spin_lock(&server->srv_lock);
	list_del_init(&server->tcp_ses_list);
	spin_unlock(&server->srv_lock);

	cancel_delayed_work_sync(&server->echo);

	spin_lock(&server->srv_lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&server->srv_lock);
	wake_up_all(&server->response_q);

	/* check if we have blocked requests that need to free */
	spin_lock(&server->req_lock);
	if (server->credits <= 0)
		server->credits = 1;
	spin_unlock(&server->req_lock);
	/*
	 * Although there should not be any requests blocked on this queue it
	 * can not hurt to be paranoid and try to wake up requests that may
	 * haven been blocked when more than 50 at time were on the wire to the
	 * same server - they now will see the session is in exit state and get
	 * out of SendReceive.
	 */
	wake_up_all(&server->request_q);
	/* give those requests time to exit */
	msleep(125);
	if (cifs_rdma_enabled(server))
		smbd_destroy(server);
	if (server->ssocket) {
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}

	if (!list_empty(&server->pending_mid_q)) {
		struct list_head dispose_list;
		struct mid_q_entry *mid_entry;
		struct list_head *tmp, *tmp2;

		INIT_LIST_HEAD(&dispose_list);
		spin_lock(&server->mid_lock);
		list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cifs_dbg(FYI, "Clearing mid %llu\n", mid_entry->mid);
			kref_get(&mid_entry->refcount);
			mid_entry->mid_state = MID_SHUTDOWN;
			list_move(&mid_entry->qhead, &dispose_list);
			mid_entry->mid_flags |= MID_DELETED;
		}
		spin_unlock(&server->mid_lock);

		/* now walk dispose list and issue callbacks */
		list_for_each_safe(tmp, tmp2, &dispose_list) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cifs_dbg(FYI, "Callback mid %llu\n", mid_entry->mid);
			list_del_init(&mid_entry->qhead);
			mid_entry->callback(mid_entry);
			release_mid(mid_entry);
		}
		/* 1/8th of sec is more than enough time for them to exit */
		msleep(125);
	}

	if (!list_empty(&server->pending_mid_q)) {
		/*
		 * mpx threads have not exited yet give them at least the smb
		 * send timeout time for long ops.
		 *
		 * Due to delays on oplock break requests, we need to wait at
		 * least 45 seconds before giving up on a request getting a
		 * response and going ahead and killing cifsd.
		 */
		cifs_dbg(FYI, "Wait for exit from demultiplex thread\n");
		msleep(46000);
		/*
		 * If threads still have not exited they are probably never
		 * coming home not much else we can do but free the memory.
		 */
	}

	kfree(server->leaf_fullpath);
	kfree(server);

	length = atomic_dec_return(&tcpSesAllocCount);
	if (length > 0)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv);
}

static int
standard_receive3(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	int length;
	char *buf = server->smallbuf;
	unsigned int pdu_length = server->pdu_size;

	/* make sure this will fit in a large buffer */
	if (pdu_length > CIFSMaxBufSize + MAX_HEADER_SIZE(server) -
	    HEADER_PREAMBLE_SIZE(server)) {
		cifs_server_dbg(VFS, "SMB response too long (%u bytes)\n", pdu_length);
		cifs_reconnect(server, true);
		return -ECONNABORTED;
	}

	/* switch to large buffer if too big for a small one */
	if (pdu_length > MAX_CIFS_SMALL_BUFFER_SIZE - 4) {
		server->large_buf = true;
		memcpy(server->bigbuf, buf, server->total_read);
		buf = server->bigbuf;
	}

	/* now read the rest */
	length = cifs_read_from_socket(server, buf + HEADER_SIZE(server) - 1,
				       pdu_length - MID_HEADER_SIZE(server));

	if (length < 0)
		return length;
	server->total_read += length;

	dump_smb(buf, server->total_read);

	return cifs_handle_standard(server, mid);
}

int
cifs_handle_standard(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	char *buf = server->large_buf ? server->bigbuf : server->smallbuf;
	int rc;

	/*
	 * We know that we received enough to get to the MID as we
	 * checked the pdu_length earlier. Now check to see
	 * if the rest of the header is OK.
	 *
	 * 48 bytes is enough to display the header and a little bit
	 * into the payload for debugging purposes.
	 */
	rc = server->ops->check_message(buf, server->total_read, server);
	if (rc)
		cifs_dump_mem("Bad SMB: ", buf,
			min_t(unsigned int, server->total_read, 48));

	if (server->ops->is_session_expired &&
	    server->ops->is_session_expired(buf)) {
		cifs_reconnect(server, true);
		return -1;
	}

	if (server->ops->is_status_pending &&
	    server->ops->is_status_pending(buf, server))
		return -1;

	if (!mid)
		return rc;

	handle_mid(mid, server, buf, rc);
	return 0;
}

static void
smb2_add_credits_from_hdr(char *buffer, struct TCP_Server_Info *server)
{
	struct smb2_hdr *shdr = (struct smb2_hdr *)buffer;
	int scredits, in_flight;

	/*
	 * SMB1 does not use credits.
	 */
	if (is_smb1(server))
		return;

	if (shdr->CreditRequest) {
		spin_lock(&server->req_lock);
		server->credits += le16_to_cpu(shdr->CreditRequest);
		scredits = server->credits;
		in_flight = server->in_flight;
		spin_unlock(&server->req_lock);
		wake_up(&server->request_q);

		trace_smb3_hdr_credits(server->CurrentMid,
				server->conn_id, server->hostname, scredits,
				le16_to_cpu(shdr->CreditRequest), in_flight);
		cifs_server_dbg(FYI, "%s: added %u credits total=%d\n",
				__func__, le16_to_cpu(shdr->CreditRequest),
				scredits);
	}
}


static int
cifs_demultiplex_thread(void *p)
{
	int i, num_mids, length;
	struct TCP_Server_Info *server = p;
	unsigned int pdu_length;
	unsigned int next_offset;
	char *buf = NULL;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mids[MAX_COMPOUND];
	char *bufs[MAX_COMPOUND];
	unsigned int noreclaim_flag, num_io_timeout = 0;
	bool pending_reconnect = false;

	noreclaim_flag = memalloc_noreclaim_save();
	cifs_dbg(FYI, "Demultiplex PID: %d\n", task_pid_nr(current));

	length = atomic_inc_return(&tcpSesAllocCount);
	if (length > 1)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv);

	set_freezable();
	allow_kernel_signal(SIGKILL);
	while (server->tcpStatus != CifsExiting) {
		if (try_to_freeze())
			continue;

		if (!allocate_buffers(server))
			continue;

		server->large_buf = false;
		buf = server->smallbuf;
		pdu_length = 4; /* enough to get RFC1001 header */

		length = cifs_read_from_socket(server, buf, pdu_length);
		if (length < 0)
			continue;

		if (is_smb1(server))
			server->total_read = length;
		else
			server->total_read = 0;

		/*
		 * The right amount was read from socket - 4 bytes,
		 * so we can now interpret the length field.
		 */
		pdu_length = get_rfc1002_length(buf);

		cifs_dbg(FYI, "RFC1002 header 0x%x\n", pdu_length);
		if (!is_smb_response(server, buf[0]))
			continue;

		pending_reconnect = false;
next_pdu:
		server->pdu_size = pdu_length;

		/* make sure we have enough to get to the MID */
		if (server->pdu_size < MID_HEADER_SIZE(server)) {
			cifs_server_dbg(VFS, "SMB response too short (%u bytes)\n",
				 server->pdu_size);
			cifs_reconnect(server, true);
			continue;
		}

		/* read down to the MID */
		length = cifs_read_from_socket(server,
			     buf + HEADER_PREAMBLE_SIZE(server),
			     MID_HEADER_SIZE(server));
		if (length < 0)
			continue;
		server->total_read += length;

		if (server->ops->next_header) {
			if (server->ops->next_header(server, buf, &next_offset)) {
				cifs_dbg(VFS, "%s: malformed response (next_offset=%u)\n",
					 __func__, next_offset);
				cifs_reconnect(server, true);
				continue;
			}
			if (next_offset)
				server->pdu_size = next_offset;
		}

		memset(mids, 0, sizeof(mids));
		memset(bufs, 0, sizeof(bufs));
		num_mids = 0;

		if (server->ops->is_transform_hdr &&
		    server->ops->receive_transform &&
		    server->ops->is_transform_hdr(buf)) {
			length = server->ops->receive_transform(server,
								mids,
								bufs,
								&num_mids);
		} else {
			mids[0] = server->ops->find_mid(server, buf);
			bufs[0] = buf;
			num_mids = 1;

			if (!mids[0] || !mids[0]->receive)
				length = standard_receive3(server, mids[0]);
			else
				length = mids[0]->receive(server, mids[0]);
		}

		if (length < 0) {
			for (i = 0; i < num_mids; i++)
				if (mids[i])
					release_mid(mids[i]);
			continue;
		}

		if (server->ops->is_status_io_timeout &&
		    server->ops->is_status_io_timeout(buf)) {
			num_io_timeout++;
			if (num_io_timeout > MAX_STATUS_IO_TIMEOUT) {
				cifs_server_dbg(VFS,
						"Number of request timeouts exceeded %d. Reconnecting",
						MAX_STATUS_IO_TIMEOUT);

				pending_reconnect = true;
				num_io_timeout = 0;
			}
		}

		server->lstrp = jiffies;

		for (i = 0; i < num_mids; i++) {
			if (mids[i] != NULL) {
				mids[i]->resp_buf_size = server->pdu_size;

				if (bufs[i] != NULL) {
					if (server->ops->is_network_name_deleted &&
					    server->ops->is_network_name_deleted(bufs[i],
										 server)) {
						cifs_server_dbg(FYI,
								"Share deleted. Reconnect needed");
					}
				}

				if (!mids[i]->multiRsp || mids[i]->multiEnd)
					mids[i]->callback(mids[i]);

				release_mid(mids[i]);
			} else if (server->ops->is_oplock_break &&
				   server->ops->is_oplock_break(bufs[i],
								server)) {
				smb2_add_credits_from_hdr(bufs[i], server);
				cifs_dbg(FYI, "Received oplock break\n");
			} else {
				cifs_server_dbg(VFS, "No task to wake, unknown frame received! NumMids %d\n",
						atomic_read(&mid_count));
				cifs_dump_mem("Received Data is: ", bufs[i],
					      HEADER_SIZE(server));
				smb2_add_credits_from_hdr(bufs[i], server);
#ifdef CONFIG_CIFS_DEBUG2
				if (server->ops->dump_detail)
					server->ops->dump_detail(bufs[i],
								 server);
				cifs_dump_mids(server);
#endif /* CIFS_DEBUG2 */
			}
		}

		if (pdu_length > server->pdu_size) {
			if (!allocate_buffers(server))
				continue;
			pdu_length -= server->pdu_size;
			server->total_read = 0;
			server->large_buf = false;
			buf = server->smallbuf;
			goto next_pdu;
		}

		/* do this reconnect at the very end after processing all MIDs */
		if (pending_reconnect)
			cifs_reconnect(server, true);

	} /* end while !EXITING */

	/* buffer usually freed in free_mid - need to free it here on exit */
	cifs_buf_release(server->bigbuf);
	if (server->smallbuf) /* no sense logging a debug message if NULL */
		cifs_small_buf_release(server->smallbuf);

	task_to_wake = xchg(&server->tsk, NULL);
	clean_demultiplex_info(server);

	/* if server->tsk was NULL then wait for a signal before exiting */
	if (!task_to_wake) {
		set_current_state(TASK_INTERRUPTIBLE);
		while (!signal_pending(current)) {
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
		set_current_state(TASK_RUNNING);
	}

	memalloc_noreclaim_restore(noreclaim_flag);
	module_put_and_kthread_exit(0);
}

int
cifs_ipaddr_cmp(struct sockaddr *srcaddr, struct sockaddr *rhs)
{
	struct sockaddr_in *saddr4 = (struct sockaddr_in *)srcaddr;
	struct sockaddr_in *vaddr4 = (struct sockaddr_in *)rhs;
	struct sockaddr_in6 *saddr6 = (struct sockaddr_in6 *)srcaddr;
	struct sockaddr_in6 *vaddr6 = (struct sockaddr_in6 *)rhs;

	switch (srcaddr->sa_family) {
	case AF_UNSPEC:
		switch (rhs->sa_family) {
		case AF_UNSPEC:
			return 0;
		case AF_INET:
		case AF_INET6:
			return 1;
		default:
			return -1;
		}
	case AF_INET: {
		switch (rhs->sa_family) {
		case AF_UNSPEC:
			return -1;
		case AF_INET:
			return memcmp(saddr4, vaddr4,
				      sizeof(struct sockaddr_in));
		case AF_INET6:
			return 1;
		default:
			return -1;
		}
	}
	case AF_INET6: {
		switch (rhs->sa_family) {
		case AF_UNSPEC:
		case AF_INET:
			return -1;
		case AF_INET6:
			return memcmp(saddr6,
				      vaddr6,
				      sizeof(struct sockaddr_in6));
		default:
			return -1;
		}
	}
	default:
		return -1; /* don't expect to be here */
	}
}

/*
 * Returns true if srcaddr isn't specified and rhs isn't specified, or
 * if srcaddr is specified and matches the IP address of the rhs argument
 */
bool
cifs_match_ipaddr(struct sockaddr *srcaddr, struct sockaddr *rhs)
{
	switch (srcaddr->sa_family) {
	case AF_UNSPEC:
		return (rhs->sa_family == AF_UNSPEC);
	case AF_INET: {
		struct sockaddr_in *saddr4 = (struct sockaddr_in *)srcaddr;
		struct sockaddr_in *vaddr4 = (struct sockaddr_in *)rhs;
		return (saddr4->sin_addr.s_addr == vaddr4->sin_addr.s_addr);
	}
	case AF_INET6: {
		struct sockaddr_in6 *saddr6 = (struct sockaddr_in6 *)srcaddr;
		struct sockaddr_in6 *vaddr6 = (struct sockaddr_in6 *)rhs;
		return (ipv6_addr_equal(&saddr6->sin6_addr, &vaddr6->sin6_addr)
			&& saddr6->sin6_scope_id == vaddr6->sin6_scope_id);
	}
	default:
		WARN_ON(1);
		return false; /* don't expect to be here */
	}
}

/*
 * If no port is specified in addr structure, we try to match with 445 port
 * and if it fails - with 139 ports. It should be called only if address
 * families of server and addr are equal.
 */
static bool
match_port(struct TCP_Server_Info *server, struct sockaddr *addr)
{
	__be16 port, *sport;

	/* SMBDirect manages its own ports, don't match it here */
	if (server->rdma)
		return true;

	switch (addr->sa_family) {
	case AF_INET:
		sport = &((struct sockaddr_in *) &server->dstaddr)->sin_port;
		port = ((struct sockaddr_in *) addr)->sin_port;
		break;
	case AF_INET6:
		sport = &((struct sockaddr_in6 *) &server->dstaddr)->sin6_port;
		port = ((struct sockaddr_in6 *) addr)->sin6_port;
		break;
	default:
		WARN_ON(1);
		return false;
	}

	if (!port) {
		port = htons(CIFS_PORT);
		if (port == *sport)
			return true;

		port = htons(RFC1001_PORT);
	}

	return port == *sport;
}

static bool match_server_address(struct TCP_Server_Info *server, struct sockaddr *addr)
{
	if (!cifs_match_ipaddr(addr, (struct sockaddr *)&server->dstaddr))
		return false;

	return true;
}

static bool
match_security(struct TCP_Server_Info *server, struct smb3_fs_context *ctx)
{
	/*
	 * The select_sectype function should either return the ctx->sectype
	 * that was specified, or "Unspecified" if that sectype was not
	 * compatible with the given NEGOTIATE request.
	 */
	if (server->ops->select_sectype(server, ctx->sectype)
	     == Unspecified)
		return false;

	/*
	 * Now check if signing mode is acceptable. No need to check
	 * global_secflags at this point since if MUST_SIGN is set then
	 * the server->sign had better be too.
	 */
	if (ctx->sign && !server->sign)
		return false;

	return true;
}

/* this function must be called with srv_lock held */
static int match_server(struct TCP_Server_Info *server,
			struct smb3_fs_context *ctx,
			bool match_super)
{
	struct sockaddr *addr = (struct sockaddr *)&ctx->dstaddr;

	lockdep_assert_held(&server->srv_lock);

	if (ctx->nosharesock)
		return 0;

	/* this server does not share socket */
	if (server->nosharesock)
		return 0;

	/* If multidialect negotiation see if existing sessions match one */
	if (strcmp(ctx->vals->version_string, SMB3ANY_VERSION_STRING) == 0) {
		if (server->vals->protocol_id < SMB30_PROT_ID)
			return 0;
	} else if (strcmp(ctx->vals->version_string,
		   SMBDEFAULT_VERSION_STRING) == 0) {
		if (server->vals->protocol_id < SMB21_PROT_ID)
			return 0;
	} else if ((server->vals != ctx->vals) || (server->ops != ctx->ops))
		return 0;

	if (!net_eq(cifs_net_ns(server), current->nsproxy->net_ns))
		return 0;

	if (!cifs_match_ipaddr((struct sockaddr *)&ctx->srcaddr,
			       (struct sockaddr *)&server->srcaddr))
		return 0;
	/*
	 * When matching cifs.ko superblocks (@match_super == true), we can't
	 * really match either @server->leaf_fullpath or @server->dstaddr
	 * directly since this @server might belong to a completely different
	 * server -- in case of domain-based DFS referrals or DFS links -- as
	 * provided earlier by mount(2) through 'source' and 'ip' options.
	 *
	 * Otherwise, match the DFS referral in @server->leaf_fullpath or the
	 * destination address in @server->dstaddr.
	 *
	 * When using 'nodfs' mount option, we avoid sharing it with DFS
	 * connections as they might failover.
	 */
	if (!match_super) {
		if (!ctx->nodfs) {
			if (server->leaf_fullpath) {
				if (!ctx->leaf_fullpath ||
				    strcasecmp(server->leaf_fullpath,
					       ctx->leaf_fullpath))
					return 0;
			} else if (ctx->leaf_fullpath) {
				return 0;
			}
		} else if (server->leaf_fullpath) {
			return 0;
		}
	}

	/*
	 * Match for a regular connection (address/hostname/port) which has no
	 * DFS referrals set.
	 */
	if (!server->leaf_fullpath &&
	    (strcasecmp(server->hostname, ctx->server_hostname) ||
	     !match_server_address(server, addr) ||
	     !match_port(server, addr)))
		return 0;

	if (!match_security(server, ctx))
		return 0;

	if (server->echo_interval != ctx->echo_interval * HZ)
		return 0;

	if (server->rdma != ctx->rdma)
		return 0;

	if (server->ignore_signature != ctx->ignore_signature)
		return 0;

	if (server->min_offload != ctx->min_offload)
		return 0;

	return 1;
}

struct TCP_Server_Info *
cifs_find_tcp_session(struct smb3_fs_context *ctx)
{
	struct TCP_Server_Info *server;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		spin_lock(&server->srv_lock);
		/*
		 * Skip ses channels since they're only handled in lower layers
		 * (e.g. cifs_send_recv).
		 */
		if (SERVER_IS_CHAN(server) ||
		    !match_server(server, ctx, false)) {
			spin_unlock(&server->srv_lock);
			continue;
		}
		spin_unlock(&server->srv_lock);

		++server->srv_count;
		spin_unlock(&cifs_tcp_ses_lock);
		cifs_dbg(FYI, "Existing tcp session with server found\n");
		return server;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

void
cifs_put_tcp_session(struct TCP_Server_Info *server, int from_reconnect)
{
	struct task_struct *task;

	spin_lock(&cifs_tcp_ses_lock);
	if (--server->srv_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	/* srv_count can never go negative */
	WARN_ON(server->srv_count < 0);

	put_net(cifs_net_ns(server));

	list_del_init(&server->tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	/* For secondary channels, we pick up ref-count on the primary server */
	if (SERVER_IS_CHAN(server))
		cifs_put_tcp_session(server->primary_server, from_reconnect);

	cancel_delayed_work_sync(&server->echo);

	if (from_reconnect)
		/*
		 * Avoid deadlock here: reconnect work calls
		 * cifs_put_tcp_session() at its end. Need to be sure
		 * that reconnect work does nothing with server pointer after
		 * that step.
		 */
		cancel_delayed_work(&server->reconnect);
	else
		cancel_delayed_work_sync(&server->reconnect);

	spin_lock(&server->srv_lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&server->srv_lock);

	cifs_crypto_secmech_release(server);

	kfree_sensitive(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;
	kfree(server->hostname);
	server->hostname = NULL;

	task = xchg(&server->tsk, NULL);
	if (task)
		send_sig(SIGKILL, task, 1);
}

struct TCP_Server_Info *
cifs_get_tcp_session(struct smb3_fs_context *ctx,
		     struct TCP_Server_Info *primary_server)
{
	struct TCP_Server_Info *tcp_ses = NULL;
	int rc;

	cifs_dbg(FYI, "UNC: %s\n", ctx->UNC);

	/* see if we already have a matching tcp_ses */
	tcp_ses = cifs_find_tcp_session(ctx);
	if (tcp_ses)
		return tcp_ses;

	tcp_ses = kzalloc(sizeof(struct TCP_Server_Info), GFP_KERNEL);
	if (!tcp_ses) {
		rc = -ENOMEM;
		goto out_err;
	}

	tcp_ses->hostname = kstrdup(ctx->server_hostname, GFP_KERNEL);
	if (!tcp_ses->hostname) {
		rc = -ENOMEM;
		goto out_err;
	}

	if (ctx->leaf_fullpath) {
		tcp_ses->leaf_fullpath = kstrdup(ctx->leaf_fullpath, GFP_KERNEL);
		if (!tcp_ses->leaf_fullpath) {
			rc = -ENOMEM;
			goto out_err;
		}
	}

	if (ctx->nosharesock)
		tcp_ses->nosharesock = true;

	tcp_ses->ops = ctx->ops;
	tcp_ses->vals = ctx->vals;
	cifs_set_net_ns(tcp_ses, get_net(current->nsproxy->net_ns));

	tcp_ses->conn_id = atomic_inc_return(&tcpSesNextId);
	tcp_ses->noblockcnt = ctx->rootfs;
	tcp_ses->noblocksnd = ctx->noblocksnd || ctx->rootfs;
	tcp_ses->noautotune = ctx->noautotune;
	tcp_ses->tcp_nodelay = ctx->sockopt_tcp_nodelay;
	tcp_ses->rdma = ctx->rdma;
	tcp_ses->in_flight = 0;
	tcp_ses->max_in_flight = 0;
	tcp_ses->credits = 1;
	if (primary_server) {
		spin_lock(&cifs_tcp_ses_lock);
		++primary_server->srv_count;
		spin_unlock(&cifs_tcp_ses_lock);
		tcp_ses->primary_server = primary_server;
	}
	init_waitqueue_head(&tcp_ses->response_q);
	init_waitqueue_head(&tcp_ses->request_q);
	INIT_LIST_HEAD(&tcp_ses->pending_mid_q);
	mutex_init(&tcp_ses->_srv_mutex);
	memcpy(tcp_ses->workstation_RFC1001_name,
		ctx->source_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	memcpy(tcp_ses->server_RFC1001_name,
		ctx->target_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	tcp_ses->session_estab = false;
	tcp_ses->sequence_number = 0;
	tcp_ses->channel_sequence_num = 0; /* only tracked for primary channel */
	tcp_ses->reconnect_instance = 1;
	tcp_ses->lstrp = jiffies;
	tcp_ses->compress_algorithm = cpu_to_le16(ctx->compression);
	spin_lock_init(&tcp_ses->req_lock);
	spin_lock_init(&tcp_ses->srv_lock);
	spin_lock_init(&tcp_ses->mid_lock);
	INIT_LIST_HEAD(&tcp_ses->tcp_ses_list);
	INIT_LIST_HEAD(&tcp_ses->smb_ses_list);
	INIT_DELAYED_WORK(&tcp_ses->echo, cifs_echo_request);
	INIT_DELAYED_WORK(&tcp_ses->reconnect, smb2_reconnect_server);
	mutex_init(&tcp_ses->reconnect_mutex);
#ifdef CONFIG_CIFS_DFS_UPCALL
	mutex_init(&tcp_ses->refpath_lock);
#endif
	memcpy(&tcp_ses->srcaddr, &ctx->srcaddr,
	       sizeof(tcp_ses->srcaddr));
	memcpy(&tcp_ses->dstaddr, &ctx->dstaddr,
		sizeof(tcp_ses->dstaddr));
	if (ctx->use_client_guid)
		memcpy(tcp_ses->client_guid, ctx->client_guid,
		       SMB2_CLIENT_GUID_SIZE);
	else
		generate_random_uuid(tcp_ses->client_guid);
	/*
	 * at this point we are the only ones with the pointer
	 * to the struct since the kernel thread not created yet
	 * no need to spinlock this init of tcpStatus or srv_count
	 */
	tcp_ses->tcpStatus = CifsNew;
	++tcp_ses->srv_count;

	if (ctx->echo_interval >= SMB_ECHO_INTERVAL_MIN &&
		ctx->echo_interval <= SMB_ECHO_INTERVAL_MAX)
		tcp_ses->echo_interval = ctx->echo_interval * HZ;
	else
		tcp_ses->echo_interval = SMB_ECHO_INTERVAL_DEFAULT * HZ;
	if (tcp_ses->rdma) {
#ifndef CONFIG_CIFS_SMB_DIRECT
		cifs_dbg(VFS, "CONFIG_CIFS_SMB_DIRECT is not enabled\n");
		rc = -ENOENT;
		goto out_err_crypto_release;
#endif
		tcp_ses->smbd_conn = smbd_get_connection(
			tcp_ses, (struct sockaddr *)&ctx->dstaddr);
		if (tcp_ses->smbd_conn) {
			cifs_dbg(VFS, "RDMA transport established\n");
			rc = 0;
			goto smbd_connected;
		} else {
			rc = -ENOENT;
			goto out_err_crypto_release;
		}
	}
	rc = ip_connect(tcp_ses);
	if (rc < 0) {
		cifs_dbg(VFS, "Error connecting to socket. Aborting operation.\n");
		goto out_err_crypto_release;
	}
smbd_connected:
	/*
	 * since we're in a cifs function already, we know that
	 * this will succeed. No need for try_module_get().
	 */
	__module_get(THIS_MODULE);
	tcp_ses->tsk = kthread_run(cifs_demultiplex_thread,
				  tcp_ses, "cifsd");
	if (IS_ERR(tcp_ses->tsk)) {
		rc = PTR_ERR(tcp_ses->tsk);
		cifs_dbg(VFS, "error %d create cifsd thread\n", rc);
		module_put(THIS_MODULE);
		goto out_err_crypto_release;
	}
	tcp_ses->min_offload = ctx->min_offload;
	/*
	 * at this point we are the only ones with the pointer
	 * to the struct since the kernel thread not created yet
	 * no need to spinlock this update of tcpStatus
	 */
	spin_lock(&tcp_ses->srv_lock);
	tcp_ses->tcpStatus = CifsNeedNegotiate;
	spin_unlock(&tcp_ses->srv_lock);

	if ((ctx->max_credits < 20) || (ctx->max_credits > 60000))
		tcp_ses->max_credits = SMB2_MAX_CREDITS_AVAILABLE;
	else
		tcp_ses->max_credits = ctx->max_credits;

	tcp_ses->nr_targets = 1;
	tcp_ses->ignore_signature = ctx->ignore_signature;
	/* thread spawned, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcp_ses->tcp_ses_list, &cifs_tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	/* queue echo request delayed work */
	queue_delayed_work(cifsiod_wq, &tcp_ses->echo, tcp_ses->echo_interval);

	return tcp_ses;

out_err_crypto_release:
	cifs_crypto_secmech_release(tcp_ses);

	put_net(cifs_net_ns(tcp_ses));

out_err:
	if (tcp_ses) {
		if (SERVER_IS_CHAN(tcp_ses))
			cifs_put_tcp_session(tcp_ses->primary_server, false);
		kfree(tcp_ses->hostname);
		kfree(tcp_ses->leaf_fullpath);
		if (tcp_ses->ssocket)
			sock_release(tcp_ses->ssocket);
		kfree(tcp_ses);
	}
	return ERR_PTR(rc);
}

/* this function must be called with ses_lock and chan_lock held */
static int match_session(struct cifs_ses *ses, struct smb3_fs_context *ctx)
{
	if (ctx->sectype != Unspecified &&
	    ctx->sectype != ses->sectype)
		return 0;

	/*
	 * If an existing session is limited to less channels than
	 * requested, it should not be reused
	 */
	if (ses->chan_max < ctx->max_channels)
		return 0;

	switch (ses->sectype) {
	case Kerberos:
		if (!uid_eq(ctx->cred_uid, ses->cred_uid))
			return 0;
		break;
	default:
		/* NULL username means anonymous session */
		if (ses->user_name == NULL) {
			if (!ctx->nullauth)
				return 0;
			break;
		}

		/* anything else takes username/password */
		if (strncmp(ses->user_name,
			    ctx->username ? ctx->username : "",
			    CIFS_MAX_USERNAME_LEN))
			return 0;
		if ((ctx->username && strlen(ctx->username) != 0) &&
		    ses->password != NULL &&
		    strncmp(ses->password,
			    ctx->password ? ctx->password : "",
			    CIFS_MAX_PASSWORD_LEN))
			return 0;
	}

	if (strcmp(ctx->local_nls->charset, ses->local_nls->charset))
		return 0;

	return 1;
}

/**
 * cifs_setup_ipc - helper to setup the IPC tcon for the session
 * @ses: smb session to issue the request on
 * @ctx: the superblock configuration context to use for building the
 *       new tree connection for the IPC (interprocess communication RPC)
 *
 * A new IPC connection is made and stored in the session
 * tcon_ipc. The IPC tcon has the same lifetime as the session.
 */
static int
cifs_setup_ipc(struct cifs_ses *ses, struct smb3_fs_context *ctx)
{
	int rc = 0, xid;
	struct cifs_tcon *tcon;
	char unc[SERVER_NAME_LENGTH + sizeof("//x/IPC$")] = {0};
	bool seal = false;
	struct TCP_Server_Info *server = ses->server;

	/*
	 * If the mount request that resulted in the creation of the
	 * session requires encryption, force IPC to be encrypted too.
	 */
	if (ctx->seal) {
		if (server->capabilities & SMB2_GLOBAL_CAP_ENCRYPTION)
			seal = true;
		else {
			cifs_server_dbg(VFS,
				 "IPC: server doesn't support encryption\n");
			return -EOPNOTSUPP;
		}
	}

	/* no need to setup directory caching on IPC share, so pass in false */
	tcon = tcon_info_alloc(false);
	if (tcon == NULL)
		return -ENOMEM;

	spin_lock(&server->srv_lock);
	scnprintf(unc, sizeof(unc), "\\\\%s\\IPC$", server->hostname);
	spin_unlock(&server->srv_lock);

	xid = get_xid();
	tcon->ses = ses;
	tcon->ipc = true;
	tcon->seal = seal;
	rc = server->ops->tree_connect(xid, ses, unc, tcon, ctx->local_nls);
	free_xid(xid);

	if (rc) {
		cifs_server_dbg(VFS, "failed to connect to IPC (rc=%d)\n", rc);
		tconInfoFree(tcon);
		goto out;
	}

	cifs_dbg(FYI, "IPC tcon rc=%d ipc tid=0x%x\n", rc, tcon->tid);

	spin_lock(&tcon->tc_lock);
	tcon->status = TID_GOOD;
	spin_unlock(&tcon->tc_lock);
	ses->tcon_ipc = tcon;
out:
	return rc;
}

/**
 * cifs_free_ipc - helper to release the session IPC tcon
 * @ses: smb session to unmount the IPC from
 *
 * Needs to be called everytime a session is destroyed.
 *
 * On session close, the IPC is closed and the server must release all tcons of the session.
 * No need to send a tree disconnect here.
 *
 * Besides, it will make the server to not close durable and resilient files on session close, as
 * specified in MS-SMB2 3.3.5.6 Receiving an SMB2 LOGOFF Request.
 */
static int
cifs_free_ipc(struct cifs_ses *ses)
{
	struct cifs_tcon *tcon = ses->tcon_ipc;

	if (tcon == NULL)
		return 0;

	tconInfoFree(tcon);
	ses->tcon_ipc = NULL;
	return 0;
}

static struct cifs_ses *
cifs_find_smb_ses(struct TCP_Server_Info *server, struct smb3_fs_context *ctx)
{
	struct cifs_ses *ses, *ret = NULL;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
		spin_lock(&ses->ses_lock);
		if (ses->ses_status == SES_EXITING) {
			spin_unlock(&ses->ses_lock);
			continue;
		}
		spin_lock(&ses->chan_lock);
		if (match_session(ses, ctx)) {
			spin_unlock(&ses->chan_lock);
			spin_unlock(&ses->ses_lock);
			ret = ses;
			break;
		}
		spin_unlock(&ses->chan_lock);
		spin_unlock(&ses->ses_lock);
	}
	if (ret)
		cifs_smb_ses_inc_refcount(ret);
	spin_unlock(&cifs_tcp_ses_lock);
	return ret;
}

void __cifs_put_smb_ses(struct cifs_ses *ses)
{
	unsigned int rc, xid;
	unsigned int chan_count;
	struct TCP_Server_Info *server = ses->server;

	spin_lock(&ses->ses_lock);
	if (ses->ses_status == SES_EXITING) {
		spin_unlock(&ses->ses_lock);
		return;
	}
	spin_unlock(&ses->ses_lock);

	cifs_dbg(FYI, "%s: ses_count=%d\n", __func__, ses->ses_count);
	cifs_dbg(FYI,
		 "%s: ses ipc: %s\n", __func__, ses->tcon_ipc ? ses->tcon_ipc->tree_name : "NONE");

	spin_lock(&cifs_tcp_ses_lock);
	if (--ses->ses_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}
	spin_lock(&ses->ses_lock);
	if (ses->ses_status == SES_GOOD)
		ses->ses_status = SES_EXITING;
	spin_unlock(&ses->ses_lock);
	spin_unlock(&cifs_tcp_ses_lock);

	/* ses_count can never go negative */
	WARN_ON(ses->ses_count < 0);

	spin_lock(&ses->ses_lock);
	if (ses->ses_status == SES_EXITING && server->ops->logoff) {
		spin_unlock(&ses->ses_lock);
		cifs_free_ipc(ses);
		xid = get_xid();
		rc = server->ops->logoff(xid, ses);
		if (rc)
			cifs_server_dbg(VFS, "%s: Session Logoff failure rc=%d\n",
				__func__, rc);
		_free_xid(xid);
	} else {
		spin_unlock(&ses->ses_lock);
		cifs_free_ipc(ses);
	}

	spin_lock(&cifs_tcp_ses_lock);
	list_del_init(&ses->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	chan_count = ses->chan_count;

	/* close any extra channels */
	if (chan_count > 1) {
		int i;

		for (i = 1; i < chan_count; i++) {
			if (ses->chans[i].iface) {
				kref_put(&ses->chans[i].iface->refcount, release_iface);
				ses->chans[i].iface = NULL;
			}
			cifs_put_tcp_session(ses->chans[i].server, 0);
			ses->chans[i].server = NULL;
		}
	}

	/* we now account for primary channel in iface->refcount */
	if (ses->chans[0].iface) {
		kref_put(&ses->chans[0].iface->refcount, release_iface);
		ses->chans[0].server = NULL;
	}

	sesInfoFree(ses);
	cifs_put_tcp_session(server, 0);
}

#ifdef CONFIG_KEYS

/* strlen("cifs:a:") + CIFS_MAX_DOMAINNAME_LEN + 1 */
#define CIFSCREDS_DESC_SIZE (7 + CIFS_MAX_DOMAINNAME_LEN + 1)

/* Populate username and pw fields from keyring if possible */
static int
cifs_set_cifscreds(struct smb3_fs_context *ctx, struct cifs_ses *ses)
{
	int rc = 0;
	int is_domain = 0;
	const char *delim, *payload;
	char *desc;
	ssize_t len;
	struct key *key;
	struct TCP_Server_Info *server = ses->server;
	struct sockaddr_in *sa;
	struct sockaddr_in6 *sa6;
	const struct user_key_payload *upayload;

	desc = kmalloc(CIFSCREDS_DESC_SIZE, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	/* try to find an address key first */
	switch (server->dstaddr.ss_family) {
	case AF_INET:
		sa = (struct sockaddr_in *)&server->dstaddr;
		sprintf(desc, "cifs:a:%pI4", &sa->sin_addr.s_addr);
		break;
	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)&server->dstaddr;
		sprintf(desc, "cifs:a:%pI6c", &sa6->sin6_addr.s6_addr);
		break;
	default:
		cifs_dbg(FYI, "Bad ss_family (%hu)\n",
			 server->dstaddr.ss_family);
		rc = -EINVAL;
		goto out_err;
	}

	cifs_dbg(FYI, "%s: desc=%s\n", __func__, desc);
	key = request_key(&key_type_logon, desc, "");
	if (IS_ERR(key)) {
		if (!ses->domainName) {
			cifs_dbg(FYI, "domainName is NULL\n");
			rc = PTR_ERR(key);
			goto out_err;
		}

		/* didn't work, try to find a domain key */
		sprintf(desc, "cifs:d:%s", ses->domainName);
		cifs_dbg(FYI, "%s: desc=%s\n", __func__, desc);
		key = request_key(&key_type_logon, desc, "");
		if (IS_ERR(key)) {
			rc = PTR_ERR(key);
			goto out_err;
		}
		is_domain = 1;
	}

	down_read(&key->sem);
	upayload = user_key_payload_locked(key);
	if (IS_ERR_OR_NULL(upayload)) {
		rc = upayload ? PTR_ERR(upayload) : -EINVAL;
		goto out_key_put;
	}

	/* find first : in payload */
	payload = upayload->data;
	delim = strnchr(payload, upayload->datalen, ':');
	cifs_dbg(FYI, "payload=%s\n", payload);
	if (!delim) {
		cifs_dbg(FYI, "Unable to find ':' in payload (datalen=%d)\n",
			 upayload->datalen);
		rc = -EINVAL;
		goto out_key_put;
	}

	len = delim - payload;
	if (len > CIFS_MAX_USERNAME_LEN || len <= 0) {
		cifs_dbg(FYI, "Bad value from username search (len=%zd)\n",
			 len);
		rc = -EINVAL;
		goto out_key_put;
	}

	ctx->username = kstrndup(payload, len, GFP_KERNEL);
	if (!ctx->username) {
		cifs_dbg(FYI, "Unable to allocate %zd bytes for username\n",
			 len);
		rc = -ENOMEM;
		goto out_key_put;
	}
	cifs_dbg(FYI, "%s: username=%s\n", __func__, ctx->username);

	len = key->datalen - (len + 1);
	if (len > CIFS_MAX_PASSWORD_LEN || len <= 0) {
		cifs_dbg(FYI, "Bad len for password search (len=%zd)\n", len);
		rc = -EINVAL;
		kfree(ctx->username);
		ctx->username = NULL;
		goto out_key_put;
	}

	++delim;
	ctx->password = kstrndup(delim, len, GFP_KERNEL);
	if (!ctx->password) {
		cifs_dbg(FYI, "Unable to allocate %zd bytes for password\n",
			 len);
		rc = -ENOMEM;
		kfree(ctx->username);
		ctx->username = NULL;
		goto out_key_put;
	}

	/*
	 * If we have a domain key then we must set the domainName in the
	 * for the request.
	 */
	if (is_domain && ses->domainName) {
		ctx->domainname = kstrdup(ses->domainName, GFP_KERNEL);
		if (!ctx->domainname) {
			cifs_dbg(FYI, "Unable to allocate %zd bytes for domain\n",
				 len);
			rc = -ENOMEM;
			kfree(ctx->username);
			ctx->username = NULL;
			kfree_sensitive(ctx->password);
			ctx->password = NULL;
			goto out_key_put;
		}
	}

	strscpy(ctx->workstation_name, ses->workstation_name, sizeof(ctx->workstation_name));

out_key_put:
	up_read(&key->sem);
	key_put(key);
out_err:
	kfree(desc);
	cifs_dbg(FYI, "%s: returning %d\n", __func__, rc);
	return rc;
}
#else /* ! CONFIG_KEYS */
static inline int
cifs_set_cifscreds(struct smb3_fs_context *ctx __attribute__((unused)),
		   struct cifs_ses *ses __attribute__((unused)))
{
	return -ENOSYS;
}
#endif /* CONFIG_KEYS */

/**
 * cifs_get_smb_ses - get a session matching @ctx data from @server
 * @server: server to setup the session to
 * @ctx: superblock configuration context to use to setup the session
 *
 * This function assumes it is being called from cifs_mount() where we
 * already got a server reference (server refcount +1). See
 * cifs_get_tcon() for refcount explanations.
 */
struct cifs_ses *
cifs_get_smb_ses(struct TCP_Server_Info *server, struct smb3_fs_context *ctx)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_ses *ses;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;

	xid = get_xid();

	ses = cifs_find_smb_ses(server, ctx);
	if (ses) {
		cifs_dbg(FYI, "Existing smb sess found (status=%d)\n",
			 ses->ses_status);

		spin_lock(&ses->chan_lock);
		if (cifs_chan_needs_reconnect(ses, server)) {
			spin_unlock(&ses->chan_lock);
			cifs_dbg(FYI, "Session needs reconnect\n");

			mutex_lock(&ses->session_mutex);
			rc = cifs_negotiate_protocol(xid, ses, server);
			if (rc) {
				mutex_unlock(&ses->session_mutex);
				/* problem -- put our ses reference */
				cifs_put_smb_ses(ses);
				free_xid(xid);
				return ERR_PTR(rc);
			}

			rc = cifs_setup_session(xid, ses, server,
						ctx->local_nls);
			if (rc) {
				mutex_unlock(&ses->session_mutex);
				/* problem -- put our reference */
				cifs_put_smb_ses(ses);
				free_xid(xid);
				return ERR_PTR(rc);
			}
			mutex_unlock(&ses->session_mutex);

			spin_lock(&ses->chan_lock);
		}
		spin_unlock(&ses->chan_lock);

		/* existing SMB ses has a server reference already */
		cifs_put_tcp_session(server, 0);
		free_xid(xid);
		return ses;
	}

	rc = -ENOMEM;

	cifs_dbg(FYI, "Existing smb sess not found\n");
	ses = sesInfoAlloc();
	if (ses == NULL)
		goto get_ses_fail;

	/* new SMB session uses our server ref */
	ses->server = server;
	if (server->dstaddr.ss_family == AF_INET6)
		sprintf(ses->ip_addr, "%pI6", &addr6->sin6_addr);
	else
		sprintf(ses->ip_addr, "%pI4", &addr->sin_addr);

	if (ctx->username) {
		ses->user_name = kstrdup(ctx->username, GFP_KERNEL);
		if (!ses->user_name)
			goto get_ses_fail;
	}

	/* ctx->password freed at unmount */
	if (ctx->password) {
		ses->password = kstrdup(ctx->password, GFP_KERNEL);
		if (!ses->password)
			goto get_ses_fail;
	}
	if (ctx->domainname) {
		ses->domainName = kstrdup(ctx->domainname, GFP_KERNEL);
		if (!ses->domainName)
			goto get_ses_fail;
	}

	strscpy(ses->workstation_name, ctx->workstation_name, sizeof(ses->workstation_name));

	if (ctx->domainauto)
		ses->domainAuto = ctx->domainauto;
	ses->cred_uid = ctx->cred_uid;
	ses->linux_uid = ctx->linux_uid;

	ses->sectype = ctx->sectype;
	ses->sign = ctx->sign;
	ses->local_nls = load_nls(ctx->local_nls->charset);

	/* add server as first channel */
	spin_lock(&ses->chan_lock);
	ses->chans[0].server = server;
	ses->chan_count = 1;
	ses->chan_max = ctx->multichannel ? ctx->max_channels:1;
	ses->chans_need_reconnect = 1;
	spin_unlock(&ses->chan_lock);

	mutex_lock(&ses->session_mutex);
	rc = cifs_negotiate_protocol(xid, ses, server);
	if (!rc)
		rc = cifs_setup_session(xid, ses, server, ctx->local_nls);
	mutex_unlock(&ses->session_mutex);

	/* each channel uses a different signing key */
	spin_lock(&ses->chan_lock);
	memcpy(ses->chans[0].signkey, ses->smb3signingkey,
	       sizeof(ses->smb3signingkey));
	spin_unlock(&ses->chan_lock);

	if (rc)
		goto get_ses_fail;

	/*
	 * success, put it on the list and add it as first channel
	 * note: the session becomes active soon after this. So you'll
	 * need to lock before changing something in the session.
	 */
	spin_lock(&cifs_tcp_ses_lock);
	ses->dfs_root_ses = ctx->dfs_root_ses;
	if (ses->dfs_root_ses)
		ses->dfs_root_ses->ses_count++;
	list_add(&ses->smb_ses_list, &server->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_setup_ipc(ses, ctx);

	free_xid(xid);

	return ses;

get_ses_fail:
	sesInfoFree(ses);
	free_xid(xid);
	return ERR_PTR(rc);
}

/* this function must be called with tc_lock held */
static int match_tcon(struct cifs_tcon *tcon, struct smb3_fs_context *ctx)
{
	struct TCP_Server_Info *server = tcon->ses->server;

	if (tcon->status == TID_EXITING)
		return 0;

	if (tcon->origin_fullpath) {
		if (!ctx->source ||
		    !dfs_src_pathname_equal(ctx->source,
					    tcon->origin_fullpath))
			return 0;
	} else if (!server->leaf_fullpath &&
		   strncmp(tcon->tree_name, ctx->UNC, MAX_TREE_SIZE)) {
		return 0;
	}
	if (tcon->seal != ctx->seal)
		return 0;
	if (tcon->snapshot_time != ctx->snapshot_time)
		return 0;
	if (tcon->handle_timeout != ctx->handle_timeout)
		return 0;
	if (tcon->no_lease != ctx->no_lease)
		return 0;
	if (tcon->nodelete != ctx->nodelete)
		return 0;
	return 1;
}

static struct cifs_tcon *
cifs_find_tcon(struct cifs_ses *ses, struct smb3_fs_context *ctx)
{
	struct cifs_tcon *tcon;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
		spin_lock(&tcon->tc_lock);
		if (!match_tcon(tcon, ctx)) {
			spin_unlock(&tcon->tc_lock);
			continue;
		}
		++tcon->tc_count;
		spin_unlock(&tcon->tc_lock);
		spin_unlock(&cifs_tcp_ses_lock);
		return tcon;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

void
cifs_put_tcon(struct cifs_tcon *tcon)
{
	unsigned int xid;
	struct cifs_ses *ses;

	/*
	 * IPC tcon share the lifetime of their session and are
	 * destroyed in the session put function
	 */
	if (tcon == NULL || tcon->ipc)
		return;

	ses = tcon->ses;
	cifs_dbg(FYI, "%s: tc_count=%d\n", __func__, tcon->tc_count);
	spin_lock(&cifs_tcp_ses_lock);
	spin_lock(&tcon->tc_lock);
	if (--tcon->tc_count > 0) {
		spin_unlock(&tcon->tc_lock);
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	/* tc_count can never go negative */
	WARN_ON(tcon->tc_count < 0);

	list_del_init(&tcon->tcon_list);
	tcon->status = TID_EXITING;
	spin_unlock(&tcon->tc_lock);
	spin_unlock(&cifs_tcp_ses_lock);

	/* cancel polling of interfaces */
	cancel_delayed_work_sync(&tcon->query_interfaces);
#ifdef CONFIG_CIFS_DFS_UPCALL
	cancel_delayed_work_sync(&tcon->dfs_cache_work);
#endif

	if (tcon->use_witness) {
		int rc;

		rc = cifs_swn_unregister(tcon);
		if (rc < 0) {
			cifs_dbg(VFS, "%s: Failed to unregister for witness notifications: %d\n",
					__func__, rc);
		}
	}

	xid = get_xid();
	if (ses->server->ops->tree_disconnect)
		ses->server->ops->tree_disconnect(xid, tcon);
	_free_xid(xid);

	cifs_fscache_release_super_cookie(tcon);
	tconInfoFree(tcon);
	cifs_put_smb_ses(ses);
}

/**
 * cifs_get_tcon - get a tcon matching @ctx data from @ses
 * @ses: smb session to issue the request on
 * @ctx: the superblock configuration context to use for building the
 *
 * - tcon refcount is the number of mount points using the tcon.
 * - ses refcount is the number of tcon using the session.
 *
 * 1. This function assumes it is being called from cifs_mount() where
 *    we already got a session reference (ses refcount +1).
 *
 * 2. Since we're in the context of adding a mount point, the end
 *    result should be either:
 *
 * a) a new tcon already allocated with refcount=1 (1 mount point) and
 *    its session refcount incremented (1 new tcon). This +1 was
 *    already done in (1).
 *
 * b) an existing tcon with refcount+1 (add a mount point to it) and
 *    identical ses refcount (no new tcon). Because of (1) we need to
 *    decrement the ses refcount.
 */
static struct cifs_tcon *
cifs_get_tcon(struct cifs_ses *ses, struct smb3_fs_context *ctx)
{
	struct cifs_tcon *tcon;
	bool nohandlecache;
	int rc, xid;

	tcon = cifs_find_tcon(ses, ctx);
	if (tcon) {
		/*
		 * tcon has refcount already incremented but we need to
		 * decrement extra ses reference gotten by caller (case b)
		 */
		cifs_dbg(FYI, "Found match on UNC path\n");
		cifs_put_smb_ses(ses);
		return tcon;
	}

	if (!ses->server->ops->tree_connect) {
		rc = -ENOSYS;
		goto out_fail;
	}

	if (ses->server->dialect >= SMB20_PROT_ID &&
	    (ses->server->capabilities & SMB2_GLOBAL_CAP_DIRECTORY_LEASING))
		nohandlecache = ctx->nohandlecache;
	else
		nohandlecache = true;
	tcon = tcon_info_alloc(!nohandlecache);
	if (tcon == NULL) {
		rc = -ENOMEM;
		goto out_fail;
	}
	tcon->nohandlecache = nohandlecache;

	if (ctx->snapshot_time) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "Use SMB2 or later for snapshot mount option\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else
			tcon->snapshot_time = ctx->snapshot_time;
	}

	if (ctx->handle_timeout) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "Use SMB2.1 or later for handle timeout option\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else
			tcon->handle_timeout = ctx->handle_timeout;
	}

	tcon->ses = ses;
	if (ctx->password) {
		tcon->password = kstrdup(ctx->password, GFP_KERNEL);
		if (!tcon->password) {
			rc = -ENOMEM;
			goto out_fail;
		}
	}

	if (ctx->seal) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
				 "SMB3 or later required for encryption\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else if (tcon->ses->server->capabilities &
					SMB2_GLOBAL_CAP_ENCRYPTION)
			tcon->seal = true;
		else {
			cifs_dbg(VFS, "Encryption is not supported on share\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
	}

	if (ctx->linux_ext) {
		if (ses->server->posix_ext_supported) {
			tcon->posix_extensions = true;
			pr_warn_once("SMB3.11 POSIX Extensions are experimental\n");
		} else if ((ses->server->vals->protocol_id == SMB311_PROT_ID) ||
		    (strcmp(ses->server->vals->version_string,
		     SMB3ANY_VERSION_STRING) == 0) ||
		    (strcmp(ses->server->vals->version_string,
		     SMBDEFAULT_VERSION_STRING) == 0)) {
			cifs_dbg(VFS, "Server does not support mounting with posix SMB3.11 extensions\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else {
			cifs_dbg(VFS, "Check vers= mount option. SMB3.11 "
				"disabled but required for POSIX extensions\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
	}

	xid = get_xid();
	rc = ses->server->ops->tree_connect(xid, ses, ctx->UNC, tcon,
					    ctx->local_nls);
	free_xid(xid);
	cifs_dbg(FYI, "Tcon rc = %d\n", rc);
	if (rc)
		goto out_fail;

	tcon->use_persistent = false;
	/* check if SMB2 or later, CIFS does not support persistent handles */
	if (ctx->persistent) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB3 or later required for persistent handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else if (ses->server->capabilities &
			   SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
			tcon->use_persistent = true;
		else /* persistent handles requested but not supported */ {
			cifs_dbg(VFS,
				"Persistent handles not supported on share\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
	} else if ((tcon->capabilities & SMB2_SHARE_CAP_CONTINUOUS_AVAILABILITY)
	     && (ses->server->capabilities & SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
	     && (ctx->nopersistent == false)) {
		cifs_dbg(FYI, "enabling persistent handles\n");
		tcon->use_persistent = true;
	} else if (ctx->resilient) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB2.1 or later required for resilient handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
		tcon->use_resilient = true;
	}

	tcon->use_witness = false;
	if (IS_ENABLED(CONFIG_CIFS_SWN_UPCALL) && ctx->witness) {
		if (ses->server->vals->protocol_id >= SMB30_PROT_ID) {
			if (tcon->capabilities & SMB2_SHARE_CAP_CLUSTER) {
				/*
				 * Set witness in use flag in first place
				 * to retry registration in the echo task
				 */
				tcon->use_witness = true;
				/* And try to register immediately */
				rc = cifs_swn_register(tcon);
				if (rc < 0) {
					cifs_dbg(VFS, "Failed to register for witness notifications: %d\n", rc);
					goto out_fail;
				}
			} else {
				/* TODO: try to extend for non-cluster uses (eg multichannel) */
				cifs_dbg(VFS, "witness requested on mount but no CLUSTER capability on share\n");
				rc = -EOPNOTSUPP;
				goto out_fail;
			}
		} else {
			cifs_dbg(VFS, "SMB3 or later required for witness option\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
	}

	/* If the user really knows what they are doing they can override */
	if (tcon->share_flags & SMB2_SHAREFLAG_NO_CACHING) {
		if (ctx->cache_ro)
			cifs_dbg(VFS, "cache=ro requested on mount but NO_CACHING flag set on share\n");
		else if (ctx->cache_rw)
			cifs_dbg(VFS, "cache=singleclient requested on mount but NO_CACHING flag set on share\n");
	}

	if (ctx->no_lease) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
				"SMB2 or later required for nolease option\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		} else
			tcon->no_lease = ctx->no_lease;
	}

	/*
	 * We can have only one retry value for a connection to a share so for
	 * resources mounted more than once to the same server share the last
	 * value passed in for the retry flag is used.
	 */
	tcon->retry = ctx->retry;
	tcon->nocase = ctx->nocase;
	tcon->broken_sparse_sup = ctx->no_sparse;
	tcon->max_cached_dirs = ctx->max_cached_dirs;
	tcon->nodelete = ctx->nodelete;
	tcon->local_lease = ctx->local_lease;
	INIT_LIST_HEAD(&tcon->pending_opens);
	tcon->status = TID_GOOD;

	INIT_DELAYED_WORK(&tcon->query_interfaces,
			  smb2_query_server_interfaces);
	if (ses->server->dialect >= SMB30_PROT_ID &&
	    (ses->server->capabilities & SMB2_GLOBAL_CAP_MULTI_CHANNEL)) {
		/* schedule query interfaces poll */
		queue_delayed_work(cifsiod_wq, &tcon->query_interfaces,
				   (SMB_INTERFACE_POLL_INTERVAL * HZ));
	}
#ifdef CONFIG_CIFS_DFS_UPCALL
	INIT_DELAYED_WORK(&tcon->dfs_cache_work, dfs_cache_refresh);
#endif
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcon->tcon_list, &ses->tcon_list);
	spin_unlock(&cifs_tcp_ses_lock);

	return tcon;

out_fail:
	tconInfoFree(tcon);
	return ERR_PTR(rc);
}

void
cifs_put_tlink(struct tcon_link *tlink)
{
	if (!tlink || IS_ERR(tlink))
		return;

	if (!atomic_dec_and_test(&tlink->tl_count) ||
	    test_bit(TCON_LINK_IN_TREE, &tlink->tl_flags)) {
		tlink->tl_time = jiffies;
		return;
	}

	if (!IS_ERR(tlink_tcon(tlink)))
		cifs_put_tcon(tlink_tcon(tlink));
	kfree(tlink);
	return;
}

static int
compare_mount_options(struct super_block *sb, struct cifs_mnt_data *mnt_data)
{
	struct cifs_sb_info *old = CIFS_SB(sb);
	struct cifs_sb_info *new = mnt_data->cifs_sb;
	unsigned int oldflags = old->mnt_cifs_flags & CIFS_MOUNT_MASK;
	unsigned int newflags = new->mnt_cifs_flags & CIFS_MOUNT_MASK;

	if ((sb->s_flags & CIFS_MS_MASK) != (mnt_data->flags & CIFS_MS_MASK))
		return 0;

	if (old->mnt_cifs_serverino_autodisabled)
		newflags &= ~CIFS_MOUNT_SERVER_INUM;

	if (oldflags != newflags)
		return 0;

	/*
	 * We want to share sb only if we don't specify an r/wsize or
	 * specified r/wsize is greater than or equal to existing one.
	 */
	if (new->ctx->wsize && new->ctx->wsize < old->ctx->wsize)
		return 0;

	if (new->ctx->rsize && new->ctx->rsize < old->ctx->rsize)
		return 0;

	if (!uid_eq(old->ctx->linux_uid, new->ctx->linux_uid) ||
	    !gid_eq(old->ctx->linux_gid, new->ctx->linux_gid))
		return 0;

	if (old->ctx->file_mode != new->ctx->file_mode ||
	    old->ctx->dir_mode != new->ctx->dir_mode)
		return 0;

	if (strcmp(old->local_nls->charset, new->local_nls->charset))
		return 0;

	if (old->ctx->acregmax != new->ctx->acregmax)
		return 0;
	if (old->ctx->acdirmax != new->ctx->acdirmax)
		return 0;
	if (old->ctx->closetimeo != new->ctx->closetimeo)
		return 0;

	return 1;
}

static int match_prepath(struct super_block *sb,
			 struct cifs_tcon *tcon,
			 struct cifs_mnt_data *mnt_data)
{
	struct smb3_fs_context *ctx = mnt_data->ctx;
	struct cifs_sb_info *old = CIFS_SB(sb);
	struct cifs_sb_info *new = mnt_data->cifs_sb;
	bool old_set = (old->mnt_cifs_flags & CIFS_MOUNT_USE_PREFIX_PATH) &&
		old->prepath;
	bool new_set = (new->mnt_cifs_flags & CIFS_MOUNT_USE_PREFIX_PATH) &&
		new->prepath;

	if (tcon->origin_fullpath &&
	    dfs_src_pathname_equal(tcon->origin_fullpath, ctx->source))
		return 1;

	if (old_set && new_set && !strcmp(new->prepath, old->prepath))
		return 1;
	else if (!old_set && !new_set)
		return 1;

	return 0;
}

int
cifs_match_super(struct super_block *sb, void *data)
{
	struct cifs_mnt_data *mnt_data = data;
	struct smb3_fs_context *ctx;
	struct cifs_sb_info *cifs_sb;
	struct TCP_Server_Info *tcp_srv;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	int rc = 0;

	spin_lock(&cifs_tcp_ses_lock);
	cifs_sb = CIFS_SB(sb);

	/* We do not want to use a superblock that has been shutdown */
	if (CIFS_MOUNT_SHUTDOWN & cifs_sb->mnt_cifs_flags) {
		spin_unlock(&cifs_tcp_ses_lock);
		return 0;
	}

	tlink = cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));
	if (IS_ERR_OR_NULL(tlink)) {
		pr_warn_once("%s: skip super matching due to bad tlink(%p)\n",
			     __func__, tlink);
		spin_unlock(&cifs_tcp_ses_lock);
		return 0;
	}
	tcon = tlink_tcon(tlink);
	ses = tcon->ses;
	tcp_srv = ses->server;

	ctx = mnt_data->ctx;

	spin_lock(&tcp_srv->srv_lock);
	spin_lock(&ses->ses_lock);
	spin_lock(&ses->chan_lock);
	spin_lock(&tcon->tc_lock);
	if (!match_server(tcp_srv, ctx, true) ||
	    !match_session(ses, ctx) ||
	    !match_tcon(tcon, ctx) ||
	    !match_prepath(sb, tcon, mnt_data)) {
		rc = 0;
		goto out;
	}

	rc = compare_mount_options(sb, mnt_data);
out:
	spin_unlock(&tcon->tc_lock);
	spin_unlock(&ses->chan_lock);
	spin_unlock(&ses->ses_lock);
	spin_unlock(&tcp_srv->srv_lock);

	spin_unlock(&cifs_tcp_ses_lock);
	cifs_put_tlink(tlink);
	return rc;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key cifs_key[2];
static struct lock_class_key cifs_slock_key[2];

static inline void
cifs_reclassify_socket4(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(!sock_allow_reclassification(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET-CIFS",
		&cifs_slock_key[0], "sk_lock-AF_INET-CIFS", &cifs_key[0]);
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(!sock_allow_reclassification(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET6-CIFS",
		&cifs_slock_key[1], "sk_lock-AF_INET6-CIFS", &cifs_key[1]);
}
#else
static inline void
cifs_reclassify_socket4(struct socket *sock)
{
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
}
#endif

/* See RFC1001 section 14 on representation of Netbios names */
static void rfc1002mangle(char *target, char *source, unsigned int length)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < (length); i++) {
		/* mask a nibble at a time and encode */
		target[j] = 'A' + (0x0F & (source[i] >> 4));
		target[j+1] = 'A' + (0x0F & source[i]);
		j += 2;
	}

}

static int
bind_socket(struct TCP_Server_Info *server)
{
	int rc = 0;
	if (server->srcaddr.ss_family != AF_UNSPEC) {
		/* Bind to the specified local IP address */
		struct socket *socket = server->ssocket;
		rc = kernel_bind(socket,
				 (struct sockaddr *) &server->srcaddr,
				 sizeof(server->srcaddr));
		if (rc < 0) {
			struct sockaddr_in *saddr4;
			struct sockaddr_in6 *saddr6;
			saddr4 = (struct sockaddr_in *)&server->srcaddr;
			saddr6 = (struct sockaddr_in6 *)&server->srcaddr;
			if (saddr6->sin6_family == AF_INET6)
				cifs_server_dbg(VFS, "Failed to bind to: %pI6c, error: %d\n",
					 &saddr6->sin6_addr, rc);
			else
				cifs_server_dbg(VFS, "Failed to bind to: %pI4, error: %d\n",
					 &saddr4->sin_addr.s_addr, rc);
		}
	}
	return rc;
}

static int
ip_rfc1001_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	/*
	 * some servers require RFC1001 sessinit before sending
	 * negprot - BB check reconnection in case where second
	 * sessinit is sent but no second negprot
	 */
	struct rfc1002_session_packet req = {};
	struct smb_hdr *smb_buf = (struct smb_hdr *)&req;
	unsigned int len;

	req.trailer.session_req.called_len = sizeof(req.trailer.session_req.called_name);

	if (server->server_RFC1001_name[0] != 0)
		rfc1002mangle(req.trailer.session_req.called_name,
			      server->server_RFC1001_name,
			      RFC1001_NAME_LEN_WITH_NULL);
	else
		rfc1002mangle(req.trailer.session_req.called_name,
			      DEFAULT_CIFS_CALLED_NAME,
			      RFC1001_NAME_LEN_WITH_NULL);

	req.trailer.session_req.calling_len = sizeof(req.trailer.session_req.calling_name);

	/* calling name ends in null (byte 16) from old smb convention */
	if (server->workstation_RFC1001_name[0] != 0)
		rfc1002mangle(req.trailer.session_req.calling_name,
			      server->workstation_RFC1001_name,
			      RFC1001_NAME_LEN_WITH_NULL);
	else
		rfc1002mangle(req.trailer.session_req.calling_name,
			      "LINUX_CIFS_CLNT",
			      RFC1001_NAME_LEN_WITH_NULL);

	/*
	 * As per rfc1002, @len must be the number of bytes that follows the
	 * length field of a rfc1002 session request payload.
	 */
	len = sizeof(req) - offsetof(struct rfc1002_session_packet, trailer.session_req);

	smb_buf->smb_buf_length = cpu_to_be32((RFC1002_SESSION_REQUEST << 24) | len);
	rc = smb_send(server, smb_buf, len);
	/*
	 * RFC1001 layer in at least one server requires very short break before
	 * negprot presumably because not expecting negprot to follow so fast.
	 * This is a simple solution that works without complicating the code
	 * and causes no significant slowing down on mount for everyone else
	 */
	usleep_range(1000, 2000);

	return rc;
}

static int
generic_ip_connect(struct TCP_Server_Info *server)
{
	struct sockaddr *saddr;
	struct socket *socket;
	int slen, sfamily;
	__be16 sport;
	int rc = 0;

	saddr = (struct sockaddr *) &server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6) {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&server->dstaddr;

		sport = ipv6->sin6_port;
		slen = sizeof(struct sockaddr_in6);
		sfamily = AF_INET6;
		cifs_dbg(FYI, "%s: connecting to [%pI6]:%d\n", __func__, &ipv6->sin6_addr,
				ntohs(sport));
	} else {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)&server->dstaddr;

		sport = ipv4->sin_port;
		slen = sizeof(struct sockaddr_in);
		sfamily = AF_INET;
		cifs_dbg(FYI, "%s: connecting to %pI4:%d\n", __func__, &ipv4->sin_addr,
				ntohs(sport));
	}

	if (server->ssocket) {
		socket = server->ssocket;
	} else {
		rc = __sock_create(cifs_net_ns(server), sfamily, SOCK_STREAM,
				   IPPROTO_TCP, &server->ssocket, 1);
		if (rc < 0) {
			cifs_server_dbg(VFS, "Error %d creating socket\n", rc);
			return rc;
		}

		/* BB other socket options to set KEEPALIVE, NODELAY? */
		cifs_dbg(FYI, "Socket created\n");
		socket = server->ssocket;
		socket->sk->sk_allocation = GFP_NOFS;
		socket->sk->sk_use_task_frag = false;
		if (sfamily == AF_INET6)
			cifs_reclassify_socket6(socket);
		else
			cifs_reclassify_socket4(socket);
	}

	rc = bind_socket(server);
	if (rc < 0)
		return rc;

	/*
	 * Eventually check for other socket options to change from
	 * the default. sock_setsockopt not used because it expects
	 * user space buffer
	 */
	socket->sk->sk_rcvtimeo = 7 * HZ;
	socket->sk->sk_sndtimeo = 5 * HZ;

	/* make the bufsizes depend on wsize/rsize and max requests */
	if (server->noautotune) {
		if (socket->sk->sk_sndbuf < (200 * 1024))
			socket->sk->sk_sndbuf = 200 * 1024;
		if (socket->sk->sk_rcvbuf < (140 * 1024))
			socket->sk->sk_rcvbuf = 140 * 1024;
	}

	if (server->tcp_nodelay)
		tcp_sock_set_nodelay(socket->sk);

	cifs_dbg(FYI, "sndbuf %d rcvbuf %d rcvtimeo 0x%lx\n",
		 socket->sk->sk_sndbuf,
		 socket->sk->sk_rcvbuf, socket->sk->sk_rcvtimeo);

	rc = kernel_connect(socket, saddr, slen,
			    server->noblockcnt ? O_NONBLOCK : 0);
	/*
	 * When mounting SMB root file systems, we do not want to block in
	 * connect. Otherwise bail out and then let cifs_reconnect() perform
	 * reconnect failover - if possible.
	 */
	if (server->noblockcnt && rc == -EINPROGRESS)
		rc = 0;
	if (rc < 0) {
		cifs_dbg(FYI, "Error %d connecting to server\n", rc);
		trace_smb3_connect_err(server->hostname, server->conn_id, &server->dstaddr, rc);
		sock_release(socket);
		server->ssocket = NULL;
		return rc;
	}
	trace_smb3_connect_done(server->hostname, server->conn_id, &server->dstaddr);
	if (sport == htons(RFC1001_PORT))
		rc = ip_rfc1001_connect(server);

	return rc;
}

static int
ip_connect(struct TCP_Server_Info *server)
{
	__be16 *sport;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6)
		sport = &addr6->sin6_port;
	else
		sport = &addr->sin_port;

	if (*sport == 0) {
		int rc;

		/* try with 445 port at first */
		*sport = htons(CIFS_PORT);

		rc = generic_ip_connect(server);
		if (rc >= 0)
			return rc;

		/* if it failed, try with 139 port */
		*sport = htons(RFC1001_PORT);
	}

	return generic_ip_connect(server);
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
void reset_cifs_unix_caps(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb, struct smb3_fs_context *ctx)
{
	/*
	 * If we are reconnecting then should we check to see if
	 * any requested capabilities changed locally e.g. via
	 * remount but we can not do much about it here
	 * if they have (even if we could detect it by the following)
	 * Perhaps we could add a backpointer to array of sb from tcon
	 * or if we change to make all sb to same share the same
	 * sb as NFS - then we only have one backpointer to sb.
	 * What if we wanted to mount the server share twice once with
	 * and once without posixacls or posix paths?
	 */
	__u64 saved_cap = le64_to_cpu(tcon->fsUnixInfo.Capability);

	if (ctx && ctx->no_linux_ext) {
		tcon->fsUnixInfo.Capability = 0;
		tcon->unix_ext = 0; /* Unix Extensions disabled */
		cifs_dbg(FYI, "Linux protocol extensions disabled\n");
		return;
	} else if (ctx)
		tcon->unix_ext = 1; /* Unix Extensions supported */

	if (!tcon->unix_ext) {
		cifs_dbg(FYI, "Unix extensions disabled so not set on reconnect\n");
		return;
	}

	if (!CIFSSMBQFSUnixInfo(xid, tcon)) {
		__u64 cap = le64_to_cpu(tcon->fsUnixInfo.Capability);
		cifs_dbg(FYI, "unix caps which server supports %lld\n", cap);
		/*
		 * check for reconnect case in which we do not
		 * want to change the mount behavior if we can avoid it
		 */
		if (ctx == NULL) {
			/*
			 * turn off POSIX ACL and PATHNAMES if not set
			 * originally at mount time
			 */
			if ((saved_cap & CIFS_UNIX_POSIX_ACL_CAP) == 0)
				cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
			if ((saved_cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
					cifs_dbg(VFS, "POSIXPATH support change\n");
				cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
			} else if ((cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				cifs_dbg(VFS, "possible reconnect error\n");
				cifs_dbg(VFS, "server disabled POSIX path support\n");
			}
		}

		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cifs_dbg(VFS, "per-share encryption not supported yet\n");

		cap &= CIFS_UNIX_CAP_MASK;
		if (ctx && ctx->no_psx_acl)
			cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
		else if (CIFS_UNIX_POSIX_ACL_CAP & cap) {
			cifs_dbg(FYI, "negotiated posix acl support\n");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIXACL;
		}

		if (ctx && ctx->posix_paths == 0)
			cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
		else if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) {
			cifs_dbg(FYI, "negotiate posix pathnames\n");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIX_PATHS;
		}

		cifs_dbg(FYI, "Negotiate caps 0x%x\n", (int)cap);
#ifdef CONFIG_CIFS_DEBUG2
		if (cap & CIFS_UNIX_FCNTL_CAP)
			cifs_dbg(FYI, "FCNTL cap\n");
		if (cap & CIFS_UNIX_EXTATTR_CAP)
			cifs_dbg(FYI, "EXTATTR cap\n");
		if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
			cifs_dbg(FYI, "POSIX path cap\n");
		if (cap & CIFS_UNIX_XATTR_CAP)
			cifs_dbg(FYI, "XATTR cap\n");
		if (cap & CIFS_UNIX_POSIX_ACL_CAP)
			cifs_dbg(FYI, "POSIX ACL cap\n");
		if (cap & CIFS_UNIX_LARGE_READ_CAP)
			cifs_dbg(FYI, "very large read cap\n");
		if (cap & CIFS_UNIX_LARGE_WRITE_CAP)
			cifs_dbg(FYI, "very large write cap\n");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_CAP)
			cifs_dbg(FYI, "transport encryption cap\n");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cifs_dbg(FYI, "mandatory transport encryption cap\n");
#endif /* CIFS_DEBUG2 */
		if (CIFSSMBSetFSUnixInfo(xid, tcon, cap)) {
			if (ctx == NULL)
				cifs_dbg(FYI, "resetting capabilities failed\n");
			else
				cifs_dbg(VFS, "Negotiating Unix capabilities with the server failed. Consider mounting with the Unix Extensions disabled if problems are found by specifying the nounix mount option.\n");

		}
	}
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

int cifs_setup_cifs_sb(struct cifs_sb_info *cifs_sb)
{
	struct smb3_fs_context *ctx = cifs_sb->ctx;

	INIT_DELAYED_WORK(&cifs_sb->prune_tlinks, cifs_prune_tlinks);

	spin_lock_init(&cifs_sb->tlink_tree_lock);
	cifs_sb->tlink_tree = RB_ROOT;

	cifs_dbg(FYI, "file mode: %04ho  dir mode: %04ho\n",
		 ctx->file_mode, ctx->dir_mode);

	/* this is needed for ASCII cp to Unicode converts */
	if (ctx->iocharset == NULL) {
		/* load_nls_default cannot return null */
		cifs_sb->local_nls = load_nls_default();
	} else {
		cifs_sb->local_nls = load_nls(ctx->iocharset);
		if (cifs_sb->local_nls == NULL) {
			cifs_dbg(VFS, "CIFS mount error: iocharset %s not found\n",
				 ctx->iocharset);
			return -ELIBACC;
		}
	}
	ctx->local_nls = cifs_sb->local_nls;

	smb3_update_mnt_flags(cifs_sb);

	if (ctx->direct_io)
		cifs_dbg(FYI, "mounting share using direct i/o\n");
	if (ctx->cache_ro) {
		cifs_dbg(VFS, "mounting share with read only caching. Ensure that the share will not be modified while in use.\n");
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_RO_CACHE;
	} else if (ctx->cache_rw) {
		cifs_dbg(VFS, "mounting share in single client RW caching mode. Ensure that no other systems will be accessing the share.\n");
		cifs_sb->mnt_cifs_flags |= (CIFS_MOUNT_RO_CACHE |
					    CIFS_MOUNT_RW_CACHE);
	}

	if ((ctx->cifs_acl) && (ctx->dynperm))
		cifs_dbg(VFS, "mount option dynperm ignored if cifsacl mount option supported\n");

	if (ctx->prepath) {
		cifs_sb->prepath = kstrdup(ctx->prepath, GFP_KERNEL);
		if (cifs_sb->prepath == NULL)
			return -ENOMEM;
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
	}

	return 0;
}

/* Release all succeed connections */
void cifs_mount_put_conns(struct cifs_mount_ctx *mnt_ctx)
{
	int rc = 0;

	if (mnt_ctx->tcon)
		cifs_put_tcon(mnt_ctx->tcon);
	else if (mnt_ctx->ses)
		cifs_put_smb_ses(mnt_ctx->ses);
	else if (mnt_ctx->server)
		cifs_put_tcp_session(mnt_ctx->server, 0);
	mnt_ctx->cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_POSIX_PATHS;
	free_xid(mnt_ctx->xid);
}

int cifs_mount_get_session(struct cifs_mount_ctx *mnt_ctx)
{
	struct TCP_Server_Info *server = NULL;
	struct smb3_fs_context *ctx;
	struct cifs_ses *ses = NULL;
	unsigned int xid;
	int rc = 0;

	xid = get_xid();

	if (WARN_ON_ONCE(!mnt_ctx || !mnt_ctx->fs_ctx)) {
		rc = -EINVAL;
		goto out;
	}
	ctx = mnt_ctx->fs_ctx;

	/* get a reference to a tcp session */
	server = cifs_get_tcp_session(ctx, NULL);
	if (IS_ERR(server)) {
		rc = PTR_ERR(server);
		server = NULL;
		goto out;
	}

	/* get a reference to a SMB session */
	ses = cifs_get_smb_ses(server, ctx);
	if (IS_ERR(ses)) {
		rc = PTR_ERR(ses);
		ses = NULL;
		goto out;
	}

	if ((ctx->persistent == true) && (!(ses->server->capabilities &
					    SMB2_GLOBAL_CAP_PERSISTENT_HANDLES))) {
		cifs_server_dbg(VFS, "persistent handles not supported by server\n");
		rc = -EOPNOTSUPP;
	}

out:
	mnt_ctx->xid = xid;
	mnt_ctx->server = server;
	mnt_ctx->ses = ses;
	mnt_ctx->tcon = NULL;

	return rc;
}

int cifs_mount_get_tcon(struct cifs_mount_ctx *mnt_ctx)
{
	struct TCP_Server_Info *server;
	struct cifs_sb_info *cifs_sb;
	struct smb3_fs_context *ctx;
	struct cifs_tcon *tcon = NULL;
	int rc = 0;

	if (WARN_ON_ONCE(!mnt_ctx || !mnt_ctx->server || !mnt_ctx->ses || !mnt_ctx->fs_ctx ||
			 !mnt_ctx->cifs_sb)) {
		rc = -EINVAL;
		goto out;
	}
	server = mnt_ctx->server;
	ctx = mnt_ctx->fs_ctx;
	cifs_sb = mnt_ctx->cifs_sb;

	/* search for existing tcon to this server share */
	tcon = cifs_get_tcon(mnt_ctx->ses, ctx);
	if (IS_ERR(tcon)) {
		rc = PTR_ERR(tcon);
		tcon = NULL;
		goto out;
	}

	/* if new SMB3.11 POSIX extensions are supported do not remap / and \ */
	if (tcon->posix_extensions)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_POSIX_PATHS;

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	/* tell server which Unix caps we support */
	if (cap_unix(tcon->ses)) {
		/*
		 * reset of caps checks mount to see if unix extensions disabled
		 * for just this mount.
		 */
		reset_cifs_unix_caps(mnt_ctx->xid, tcon, cifs_sb, ctx);
		spin_lock(&tcon->ses->server->srv_lock);
		if ((tcon->ses->server->tcpStatus == CifsNeedReconnect) &&
		    (le64_to_cpu(tcon->fsUnixInfo.Capability) &
		     CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)) {
			spin_unlock(&tcon->ses->server->srv_lock);
			rc = -EACCES;
			goto out;
		}
		spin_unlock(&tcon->ses->server->srv_lock);
	} else
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
		tcon->unix_ext = 0; /* server does not support them */

	/* do not care if a following call succeed - informational */
	if (!tcon->pipe && server->ops->qfs_tcon) {
		server->ops->qfs_tcon(mnt_ctx->xid, tcon, cifs_sb);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RO_CACHE) {
			if (tcon->fsDevInfo.DeviceCharacteristics &
			    cpu_to_le32(FILE_READ_ONLY_DEVICE))
				cifs_dbg(VFS, "mounted to read only share\n");
			else if ((cifs_sb->mnt_cifs_flags &
				  CIFS_MOUNT_RW_CACHE) == 0)
				cifs_dbg(VFS, "read only mount of RW share\n");
			/* no need to log a RW mount of a typical RW share */
		}
	}

	/*
	 * Clamp the rsize/wsize mount arguments if they are too big for the server
	 * and set the rsize/wsize to the negotiated values if not passed in by
	 * the user on mount
	 */
	if ((cifs_sb->ctx->wsize == 0) ||
	    (cifs_sb->ctx->wsize > server->ops->negotiate_wsize(tcon, ctx)))
		cifs_sb->ctx->wsize = server->ops->negotiate_wsize(tcon, ctx);
	if ((cifs_sb->ctx->rsize == 0) ||
	    (cifs_sb->ctx->rsize > server->ops->negotiate_rsize(tcon, ctx)))
		cifs_sb->ctx->rsize = server->ops->negotiate_rsize(tcon, ctx);

	/*
	 * The cookie is initialized from volume info returned above.
	 * Inside cifs_fscache_get_super_cookie it checks
	 * that we do not get super cookie twice.
	 */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_FSCACHE)
		cifs_fscache_get_super_cookie(tcon);

out:
	mnt_ctx->tcon = tcon;
	return rc;
}

static int mount_setup_tlink(struct cifs_sb_info *cifs_sb, struct cifs_ses *ses,
			     struct cifs_tcon *tcon)
{
	struct tcon_link *tlink;

	/* hang the tcon off of the superblock */
	tlink = kzalloc(sizeof(*tlink), GFP_KERNEL);
	if (tlink == NULL)
		return -ENOMEM;

	tlink->tl_uid = ses->linux_uid;
	tlink->tl_tcon = tcon;
	tlink->tl_time = jiffies;
	set_bit(TCON_LINK_MASTER, &tlink->tl_flags);
	set_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);

	cifs_sb->master_tlink = tlink;
	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(cifsiod_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);
	return 0;
}

static int
cifs_are_all_path_components_accessible(struct TCP_Server_Info *server,
					unsigned int xid,
					struct cifs_tcon *tcon,
					struct cifs_sb_info *cifs_sb,
					char *full_path,
					int added_treename)
{
	int rc;
	char *s;
	char sep, tmp;
	int skip = added_treename ? 1 : 0;

	sep = CIFS_DIR_SEP(cifs_sb);
	s = full_path;

	rc = server->ops->is_path_accessible(xid, tcon, cifs_sb, "");
	while (rc == 0) {
		/* skip separators */
		while (*s == sep)
			s++;
		if (!*s)
			break;
		/* next separator */
		while (*s && *s != sep)
			s++;
		/*
		 * if the treename is added, we then have to skip the first
		 * part within the separators
		 */
		if (skip) {
			skip = 0;
			continue;
		}
		/*
		 * temporarily null-terminate the path at the end of
		 * the current component
		 */
		tmp = *s;
		*s = 0;
		rc = server->ops->is_path_accessible(xid, tcon, cifs_sb,
						     full_path);
		*s = tmp;
	}
	return rc;
}

/*
 * Check if path is remote (i.e. a DFS share).
 *
 * Return -EREMOTE if it is, otherwise 0 or -errno.
 */
int cifs_is_path_remote(struct cifs_mount_ctx *mnt_ctx)
{
	int rc;
	struct cifs_sb_info *cifs_sb = mnt_ctx->cifs_sb;
	struct TCP_Server_Info *server = mnt_ctx->server;
	unsigned int xid = mnt_ctx->xid;
	struct cifs_tcon *tcon = mnt_ctx->tcon;
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	char *full_path;

	if (!server->ops->is_path_accessible)
		return -EOPNOTSUPP;

	/*
	 * cifs_build_path_to_root works only when we have a valid tcon
	 */
	full_path = cifs_build_path_to_root(ctx, cifs_sb, tcon,
					    tcon->Flags & SMB_SHARE_IS_IN_DFS);
	if (full_path == NULL)
		return -ENOMEM;

	cifs_dbg(FYI, "%s: full_path: %s\n", __func__, full_path);

	rc = server->ops->is_path_accessible(xid, tcon, cifs_sb,
					     full_path);
	if (rc != 0 && rc != -EREMOTE)
		goto out;

	if (rc != -EREMOTE) {
		rc = cifs_are_all_path_components_accessible(server, xid, tcon,
			cifs_sb, full_path, tcon->Flags & SMB_SHARE_IS_IN_DFS);
		if (rc != 0) {
			cifs_server_dbg(VFS, "cannot query dirs between root and final path, enabling CIFS_MOUNT_USE_PREFIX_PATH\n");
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
			rc = 0;
		}
	}

out:
	kfree(full_path);
	return rc;
}

#ifdef CONFIG_CIFS_DFS_UPCALL
int cifs_mount(struct cifs_sb_info *cifs_sb, struct smb3_fs_context *ctx)
{
	struct cifs_mount_ctx mnt_ctx = { .cifs_sb = cifs_sb, .fs_ctx = ctx, };
	bool isdfs;
	int rc;

	INIT_LIST_HEAD(&mnt_ctx.dfs_ses_list);

	rc = dfs_mount_share(&mnt_ctx, &isdfs);
	if (rc)
		goto error;
	if (!isdfs)
		goto out;

	/*
	 * After reconnecting to a different server, unique ids won't match anymore, so we disable
	 * serverino. This prevents dentry revalidation to think the dentry are stale (ESTALE).
	 */
	cifs_autodisable_serverino(cifs_sb);
	/*
	 * Force the use of prefix path to support failover on DFS paths that resolve to targets
	 * that have different prefix paths.
	 */
	cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
	kfree(cifs_sb->prepath);
	cifs_sb->prepath = ctx->prepath;
	ctx->prepath = NULL;

out:
	cifs_try_adding_channels(mnt_ctx.ses);
	rc = mount_setup_tlink(cifs_sb, mnt_ctx.ses, mnt_ctx.tcon);
	if (rc)
		goto error;

	free_xid(mnt_ctx.xid);
	return rc;

error:
	dfs_put_root_smb_sessions(&mnt_ctx.dfs_ses_list);
	cifs_mount_put_conns(&mnt_ctx);
	return rc;
}
#else
int cifs_mount(struct cifs_sb_info *cifs_sb, struct smb3_fs_context *ctx)
{
	int rc = 0;
	struct cifs_mount_ctx mnt_ctx = { .cifs_sb = cifs_sb, .fs_ctx = ctx, };

	rc = cifs_mount_get_session(&mnt_ctx);
	if (rc)
		goto error;

	rc = cifs_mount_get_tcon(&mnt_ctx);
	if (rc)
		goto error;

	rc = cifs_is_path_remote(&mnt_ctx);
	if (rc == -EREMOTE)
		rc = -EOPNOTSUPP;
	if (rc)
		goto error;

	rc = mount_setup_tlink(cifs_sb, mnt_ctx.ses, mnt_ctx.tcon);
	if (rc)
		goto error;

	free_xid(mnt_ctx.xid);
	return rc;

error:
	cifs_mount_put_conns(&mnt_ctx);
	return rc;
}
#endif

/*
 * Issue a TREE_CONNECT request.
 */
int
CIFSTCon(const unsigned int xid, struct cifs_ses *ses,
	 const char *tree, struct cifs_tcon *tcon,
	 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	TCONX_REQ *pSMB;
	TCONX_RSP *pSMBr;
	unsigned char *bcc_ptr;
	int rc = 0;
	int length;
	__u16 bytes_left, count;

	if (ses == NULL)
		return -EIO;

	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL)
		return -ENOMEM;

	smb_buffer_response = smb_buffer;

	header_assemble(smb_buffer, SMB_COM_TREE_CONNECT_ANDX,
			NULL /*no tid */ , 4 /*wct */ );

	smb_buffer->Mid = get_next_mid(ses->server);
	smb_buffer->Uid = ses->Suid;
	pSMB = (TCONX_REQ *) smb_buffer;
	pSMBr = (TCONX_RSP *) smb_buffer_response;

	pSMB->AndXCommand = 0xFF;
	pSMB->Flags = cpu_to_le16(TCON_EXTENDED_SECINFO);
	bcc_ptr = &pSMB->Password[0];

	pSMB->PasswordLength = cpu_to_le16(1);	/* minimum */
	*bcc_ptr = 0; /* password is null byte */
	bcc_ptr++;              /* skip password */
	/* already aligned so no need to do it below */

	if (ses->server->sign)
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
	}
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		length =
		    cifs_strtoUTF16((__le16 *) bcc_ptr, tree,
			6 /* max utf8 char length in bytes */ *
			(/* server len*/ + 256 /* share len */), nls_codepage);
		bcc_ptr += 2 * length;	/* convert num 16 bit words to bytes */
		bcc_ptr += 2;	/* skip trailing null */
	} else {		/* ASCII */
		strcpy(bcc_ptr, tree);
		bcc_ptr += strlen(tree) + 1;
	}
	strcpy(bcc_ptr, "?????");
	bcc_ptr += strlen("?????");
	bcc_ptr += 1;
	count = bcc_ptr - &pSMB->Password[0];
	be32_add_cpu(&pSMB->hdr.smb_buf_length, count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length,
			 0);

	/* above now done in SendReceive */
	if (rc == 0) {
		bool is_unicode;

		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		bytes_left = get_bcc(smb_buffer_response);
		length = strnlen(bcc_ptr, bytes_left - 2);
		if (smb_buffer->Flags2 & SMBFLG2_UNICODE)
			is_unicode = true;
		else
			is_unicode = false;


		/* skip service field (NB: this field is always ASCII) */
		if (length == 3) {
			if ((bcc_ptr[0] == 'I') && (bcc_ptr[1] == 'P') &&
			    (bcc_ptr[2] == 'C')) {
				cifs_dbg(FYI, "IPC connection\n");
				tcon->ipc = true;
				tcon->pipe = true;
			}
		} else if (length == 2) {
			if ((bcc_ptr[0] == 'A') && (bcc_ptr[1] == ':')) {
				/* the most common case */
				cifs_dbg(FYI, "disk share connection\n");
			}
		}
		bcc_ptr += length + 1;
		bytes_left -= (length + 1);
		strscpy(tcon->tree_name, tree, sizeof(tcon->tree_name));

		/* mostly informational -- no need to fail on error here */
		kfree(tcon->nativeFileSystem);
		tcon->nativeFileSystem = cifs_strndup_from_utf16(bcc_ptr,
						      bytes_left, is_unicode,
						      nls_codepage);

		cifs_dbg(FYI, "nativeFileSystem=%s\n", tcon->nativeFileSystem);

		if ((smb_buffer_response->WordCount == 3) ||
			 (smb_buffer_response->WordCount == 7))
			/* field is in same location */
			tcon->Flags = le16_to_cpu(pSMBr->OptionalSupport);
		else
			tcon->Flags = 0;
		cifs_dbg(FYI, "Tcon flags: 0x%x\n", tcon->Flags);
	}

	cifs_buf_release(smb_buffer);
	return rc;
}

static void delayed_free(struct rcu_head *p)
{
	struct cifs_sb_info *cifs_sb = container_of(p, struct cifs_sb_info, rcu);

	unload_nls(cifs_sb->local_nls);
	smb3_cleanup_fs_context(cifs_sb->ctx);
	kfree(cifs_sb);
}

void
cifs_umount(struct cifs_sb_info *cifs_sb)
{
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node;
	struct tcon_link *tlink;

	cancel_delayed_work_sync(&cifs_sb->prune_tlinks);

	spin_lock(&cifs_sb->tlink_tree_lock);
	while ((node = rb_first(root))) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);
		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(node, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	kfree(cifs_sb->prepath);
	call_rcu(&cifs_sb->rcu, delayed_free);
}

int
cifs_negotiate_protocol(const unsigned int xid, struct cifs_ses *ses,
			struct TCP_Server_Info *server)
{
	int rc = 0;

	if (!server->ops->need_neg || !server->ops->negotiate)
		return -ENOSYS;

	/* only send once per connect */
	spin_lock(&server->srv_lock);
	if (server->tcpStatus != CifsGood &&
	    server->tcpStatus != CifsNew &&
	    server->tcpStatus != CifsNeedNegotiate) {
		spin_unlock(&server->srv_lock);
		return -EHOSTDOWN;
	}

	if (!server->ops->need_neg(server) &&
	    server->tcpStatus == CifsGood) {
		spin_unlock(&server->srv_lock);
		return 0;
	}

	server->tcpStatus = CifsInNegotiate;
	spin_unlock(&server->srv_lock);

	rc = server->ops->negotiate(xid, ses, server);
	if (rc == 0) {
		spin_lock(&server->srv_lock);
		if (server->tcpStatus == CifsInNegotiate)
			server->tcpStatus = CifsGood;
		else
			rc = -EHOSTDOWN;
		spin_unlock(&server->srv_lock);
	} else {
		spin_lock(&server->srv_lock);
		if (server->tcpStatus == CifsInNegotiate)
			server->tcpStatus = CifsNeedNegotiate;
		spin_unlock(&server->srv_lock);
	}

	return rc;
}

int
cifs_setup_session(const unsigned int xid, struct cifs_ses *ses,
		   struct TCP_Server_Info *server,
		   struct nls_table *nls_info)
{
	int rc = -ENOSYS;
	struct TCP_Server_Info *pserver = SERVER_IS_CHAN(server) ? server->primary_server : server;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&pserver->dstaddr;
	struct sockaddr_in *addr = (struct sockaddr_in *)&pserver->dstaddr;
	bool is_binding = false;

	spin_lock(&ses->ses_lock);
	cifs_dbg(FYI, "%s: channel connect bitmap: 0x%lx\n",
		 __func__, ses->chans_need_reconnect);

	if (ses->ses_status != SES_GOOD &&
	    ses->ses_status != SES_NEW &&
	    ses->ses_status != SES_NEED_RECON) {
		spin_unlock(&ses->ses_lock);
		return -EHOSTDOWN;
	}

	/* only send once per connect */
	spin_lock(&ses->chan_lock);
	if (CIFS_ALL_CHANS_GOOD(ses)) {
		if (ses->ses_status == SES_NEED_RECON)
			ses->ses_status = SES_GOOD;
		spin_unlock(&ses->chan_lock);
		spin_unlock(&ses->ses_lock);
		return 0;
	}

	cifs_chan_set_in_reconnect(ses, server);
	is_binding = !CIFS_ALL_CHANS_NEED_RECONNECT(ses);
	spin_unlock(&ses->chan_lock);

	if (!is_binding) {
		ses->ses_status = SES_IN_SETUP;

		/* force iface_list refresh */
		ses->iface_last_update = 0;
	}
	spin_unlock(&ses->ses_lock);

	/* update ses ip_addr only for primary chan */
	if (server == pserver) {
		if (server->dstaddr.ss_family == AF_INET6)
			scnprintf(ses->ip_addr, sizeof(ses->ip_addr), "%pI6", &addr6->sin6_addr);
		else
			scnprintf(ses->ip_addr, sizeof(ses->ip_addr), "%pI4", &addr->sin_addr);
	}

	if (!is_binding) {
		ses->capabilities = server->capabilities;
		if (!linuxExtEnabled)
			ses->capabilities &= (~server->vals->cap_unix);

		if (ses->auth_key.response) {
			cifs_dbg(FYI, "Free previous auth_key.response = %p\n",
				 ses->auth_key.response);
			kfree_sensitive(ses->auth_key.response);
			ses->auth_key.response = NULL;
			ses->auth_key.len = 0;
		}
	}

	cifs_dbg(FYI, "Security Mode: 0x%x Capabilities: 0x%x TimeAdjust: %d\n",
		 server->sec_mode, server->capabilities, server->timeAdj);

	if (server->ops->sess_setup)
		rc = server->ops->sess_setup(xid, ses, server, nls_info);

	if (rc) {
		cifs_server_dbg(VFS, "Send error in SessSetup = %d\n", rc);
		spin_lock(&ses->ses_lock);
		if (ses->ses_status == SES_IN_SETUP)
			ses->ses_status = SES_NEED_RECON;
		spin_lock(&ses->chan_lock);
		cifs_chan_clear_in_reconnect(ses, server);
		spin_unlock(&ses->chan_lock);
		spin_unlock(&ses->ses_lock);
	} else {
		spin_lock(&ses->ses_lock);
		if (ses->ses_status == SES_IN_SETUP)
			ses->ses_status = SES_GOOD;
		spin_lock(&ses->chan_lock);
		cifs_chan_clear_in_reconnect(ses, server);
		cifs_chan_clear_need_reconnect(ses, server);
		spin_unlock(&ses->chan_lock);
		spin_unlock(&ses->ses_lock);
	}

	return rc;
}

static int
cifs_set_vol_auth(struct smb3_fs_context *ctx, struct cifs_ses *ses)
{
	ctx->sectype = ses->sectype;

	/* krb5 is special, since we don't need username or pw */
	if (ctx->sectype == Kerberos)
		return 0;

	return cifs_set_cifscreds(ctx, ses);
}

static struct cifs_tcon *
cifs_construct_tcon(struct cifs_sb_info *cifs_sb, kuid_t fsuid)
{
	int rc;
	struct cifs_tcon *master_tcon = cifs_sb_master_tcon(cifs_sb);
	struct cifs_ses *ses;
	struct cifs_tcon *tcon = NULL;
	struct smb3_fs_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return ERR_PTR(-ENOMEM);

	ctx->local_nls = cifs_sb->local_nls;
	ctx->linux_uid = fsuid;
	ctx->cred_uid = fsuid;
	ctx->UNC = master_tcon->tree_name;
	ctx->retry = master_tcon->retry;
	ctx->nocase = master_tcon->nocase;
	ctx->nohandlecache = master_tcon->nohandlecache;
	ctx->local_lease = master_tcon->local_lease;
	ctx->no_lease = master_tcon->no_lease;
	ctx->resilient = master_tcon->use_resilient;
	ctx->persistent = master_tcon->use_persistent;
	ctx->handle_timeout = master_tcon->handle_timeout;
	ctx->no_linux_ext = !master_tcon->unix_ext;
	ctx->linux_ext = master_tcon->posix_extensions;
	ctx->sectype = master_tcon->ses->sectype;
	ctx->sign = master_tcon->ses->sign;
	ctx->seal = master_tcon->seal;
	ctx->witness = master_tcon->use_witness;

	rc = cifs_set_vol_auth(ctx, master_tcon->ses);
	if (rc) {
		tcon = ERR_PTR(rc);
		goto out;
	}

	/* get a reference for the same TCP session */
	spin_lock(&cifs_tcp_ses_lock);
	++master_tcon->ses->server->srv_count;
	spin_unlock(&cifs_tcp_ses_lock);

	ses = cifs_get_smb_ses(master_tcon->ses->server, ctx);
	if (IS_ERR(ses)) {
		tcon = (struct cifs_tcon *)ses;
		cifs_put_tcp_session(master_tcon->ses->server, 0);
		goto out;
	}

	tcon = cifs_get_tcon(ses, ctx);
	if (IS_ERR(tcon)) {
		cifs_put_smb_ses(ses);
		goto out;
	}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if (cap_unix(ses))
		reset_cifs_unix_caps(0, tcon, NULL, ctx);
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

out:
	kfree(ctx->username);
	kfree_sensitive(ctx->password);
	kfree(ctx);

	return tcon;
}

struct cifs_tcon *
cifs_sb_master_tcon(struct cifs_sb_info *cifs_sb)
{
	return tlink_tcon(cifs_sb_master_tlink(cifs_sb));
}

/* find and return a tlink with given uid */
static struct tcon_link *
tlink_rb_search(struct rb_root *root, kuid_t uid)
{
	struct rb_node *node = root->rb_node;
	struct tcon_link *tlink;

	while (node) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);

		if (uid_gt(tlink->tl_uid, uid))
			node = node->rb_left;
		else if (uid_lt(tlink->tl_uid, uid))
			node = node->rb_right;
		else
			return tlink;
	}
	return NULL;
}

/* insert a tcon_link into the tree */
static void
tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct tcon_link *tlink;

	while (*new) {
		tlink = rb_entry(*new, struct tcon_link, tl_rbnode);
		parent = *new;

		if (uid_gt(tlink->tl_uid, new_tlink->tl_uid))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&new_tlink->tl_rbnode, parent, new);
	rb_insert_color(&new_tlink->tl_rbnode, root);
}

/*
 * Find or construct an appropriate tcon given a cifs_sb and the fsuid of the
 * current task.
 *
 * If the superblock doesn't refer to a multiuser mount, then just return
 * the master tcon for the mount.
 *
 * First, search the rbtree for an existing tcon for this fsuid. If one
 * exists, then check to see if it's pending construction. If it is then wait
 * for construction to complete. Once it's no longer pending, check to see if
 * it failed and either return an error or retry construction, depending on
 * the timeout.
 *
 * If one doesn't exist then insert a new tcon_link struct into the tree and
 * try to construct a new one.
 */
struct tcon_link *
cifs_sb_tlink(struct cifs_sb_info *cifs_sb)
{
	int ret;
	kuid_t fsuid = current_fsuid();
	struct tcon_link *tlink, *newtlink;

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		return cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));

	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
	if (tlink)
		cifs_get_tlink(tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	if (tlink == NULL) {
		newtlink = kzalloc(sizeof(*tlink), GFP_KERNEL);
		if (newtlink == NULL)
			return ERR_PTR(-ENOMEM);
		newtlink->tl_uid = fsuid;
		newtlink->tl_tcon = ERR_PTR(-EACCES);
		set_bit(TCON_LINK_PENDING, &newtlink->tl_flags);
		set_bit(TCON_LINK_IN_TREE, &newtlink->tl_flags);
		cifs_get_tlink(newtlink);

		spin_lock(&cifs_sb->tlink_tree_lock);
		/* was one inserted after previous search? */
		tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
		if (tlink) {
			cifs_get_tlink(tlink);
			spin_unlock(&cifs_sb->tlink_tree_lock);
			kfree(newtlink);
			goto wait_for_construction;
		}
		tlink = newtlink;
		tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
		spin_unlock(&cifs_sb->tlink_tree_lock);
	} else {
wait_for_construction:
		ret = wait_on_bit(&tlink->tl_flags, TCON_LINK_PENDING,
				  TASK_INTERRUPTIBLE);
		if (ret) {
			cifs_put_tlink(tlink);
			return ERR_PTR(-ERESTARTSYS);
		}

		/* if it's good, return it */
		if (!IS_ERR(tlink->tl_tcon))
			return tlink;

		/* return error if we tried this already recently */
		if (time_before(jiffies, tlink->tl_time + TLINK_ERROR_EXPIRE)) {
			cifs_put_tlink(tlink);
			return ERR_PTR(-EACCES);
		}

		if (test_and_set_bit(TCON_LINK_PENDING, &tlink->tl_flags))
			goto wait_for_construction;
	}

	tlink->tl_tcon = cifs_construct_tcon(cifs_sb, fsuid);
	clear_bit(TCON_LINK_PENDING, &tlink->tl_flags);
	wake_up_bit(&tlink->tl_flags, TCON_LINK_PENDING);

	if (IS_ERR(tlink->tl_tcon)) {
		cifs_put_tlink(tlink);
		return ERR_PTR(-EACCES);
	}

	return tlink;
}

/*
 * periodic workqueue job that scans tcon_tree for a superblock and closes
 * out tcons.
 */
static void
cifs_prune_tlinks(struct work_struct *work)
{
	struct cifs_sb_info *cifs_sb = container_of(work, struct cifs_sb_info,
						    prune_tlinks.work);
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node;
	struct rb_node *tmp;
	struct tcon_link *tlink;

	/*
	 * Because we drop the spinlock in the loop in order to put the tlink
	 * it's not guarded against removal of links from the tree. The only
	 * places that remove entries from the tree are this function and
	 * umounts. Because this function is non-reentrant and is canceled
	 * before umount can proceed, this is safe.
	 */
	spin_lock(&cifs_sb->tlink_tree_lock);
	node = rb_first(root);
	while (node != NULL) {
		tmp = node;
		node = rb_next(tmp);
		tlink = rb_entry(tmp, struct tcon_link, tl_rbnode);

		if (test_bit(TCON_LINK_MASTER, &tlink->tl_flags) ||
		    atomic_read(&tlink->tl_count) != 0 ||
		    time_after(tlink->tl_time + TLINK_IDLE_EXPIRE, jiffies))
			continue;

		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(tmp, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(cifsiod_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);
}

#ifndef CONFIG_CIFS_DFS_UPCALL
int cifs_tree_connect(const unsigned int xid, struct cifs_tcon *tcon, const struct nls_table *nlsc)
{
	int rc;
	const struct smb_version_operations *ops = tcon->ses->server->ops;

	/* only send once per connect */
	spin_lock(&tcon->tc_lock);
	if (tcon->status == TID_GOOD) {
		spin_unlock(&tcon->tc_lock);
		return 0;
	}

	if (tcon->status != TID_NEW &&
	    tcon->status != TID_NEED_TCON) {
		spin_unlock(&tcon->tc_lock);
		return -EHOSTDOWN;
	}

	tcon->status = TID_IN_TCON;
	spin_unlock(&tcon->tc_lock);

	rc = ops->tree_connect(xid, tcon->ses, tcon->tree_name, tcon, nlsc);
	if (rc) {
		spin_lock(&tcon->tc_lock);
		if (tcon->status == TID_IN_TCON)
			tcon->status = TID_NEED_TCON;
		spin_unlock(&tcon->tc_lock);
	} else {
		spin_lock(&tcon->tc_lock);
		if (tcon->status == TID_IN_TCON)
			tcon->status = TID_GOOD;
		tcon->need_reconnect = false;
		spin_unlock(&tcon->tc_lock);
	}

	return rc;
}
#endif
