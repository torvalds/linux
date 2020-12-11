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

/* Change credits for different ops and return the total number of credits */
static int
change_conf(struct TCP_Server_Info *server)
{
	server->credits += server->echo_credits + server->oplock_credits;
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
	unsigned int add = credits->value;
	unsigned int instance = credits->instance;
	bool reconnect_detected = false;

	spin_lock(&server->req_lock);
	val = server->ops->get_credits_field(server, optype);

	/* eg found case where write overlapping reconnect messed up credits */
	if (((optype & CIFS_OP_MASK) == CIFS_NEG_OP) && (*val != 0))
		trace_smb3_reconnect_with_invalid_credits(server->CurrentMid,
			server->hostname, *val, add);
	if ((instance == 0) || (instance == server->reconnect_instance))
		*val += add;
	else
		reconnect_detected = true;

	if (*val > 65000) {
		*val = 65000; /* Don't get near 64K credits, avoid srv bugs */
		pr_warn_once("server overflowed SMB3 credits\n");
	}
	server->in_flight--;
	if (server->in_flight == 0 && (optype & CIFS_OP_MASK) != CIFS_NEG_OP)
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
	}
	spin_unlock(&server->req_lock);
	wake_up(&server->request_q);

	if (reconnect_detected)
		cifs_dbg(FYI, "trying to put %d credits from the old server instance %d\n",
			 add, instance);

	if (server->tcpStatus == CifsNeedReconnect
	    || server->tcpStatus == CifsExiting)
		return;

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
		trace_smb3_add_credits(server->CurrentMid,
			server->hostname, rc, add);
		cifs_dbg(FYI, "add %u credits total=%d\n", add, rc);
	}
}

static void
smb2_set_credits(struct TCP_Server_Info *server, const int val)
{
	spin_lock(&server->req_lock);
	server->credits = val;
	if (val == 1)
		server->reconnect_instance++;
	spin_unlock(&server->req_lock);
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
	unsigned int scredits;

	spin_lock(&server->req_lock);
	while (1) {
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
			if (server->tcpStatus == CifsExiting) {
				spin_unlock(&server->req_lock);
				return -ENOENT;
			}

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
	spin_unlock(&server->req_lock);
	return rc;
}

static int
smb2_adjust_credits(struct TCP_Server_Info *server,
		    struct cifs_credits *credits,
		    const unsigned int payload_size)
{
	int new_val = DIV_ROUND_UP(payload_size, SMB2_MAX_BUFFER_SIZE);

	if (!credits->value || credits->value == new_val)
		return 0;

	if (credits->value < new_val) {
		WARN_ONCE(1, "request has less credits (%d) than required (%d)",
			  credits->value, new_val);
		return -ENOTSUPP;
	}

	spin_lock(&server->req_lock);

	if (server->reconnect_instance != credits->instance) {
		spin_unlock(&server->req_lock);
		cifs_server_dbg(VFS, "trying to return %d credits to old session\n",
			 credits->value - new_val);
		return -EAGAIN;
	}

	server->credits += credits->value - new_val;
	spin_unlock(&server->req_lock);
	wake_up(&server->request_q);
	credits->value = new_val;
	return 0;
}

static __u64
smb2_get_next_mid(struct TCP_Server_Info *server)
{
	__u64 mid;
	/* for SMB2 we need the current value */
	spin_lock(&GlobalMid_Lock);
	mid = server->CurrentMid++;
	spin_unlock(&GlobalMid_Lock);
	return mid;
}

static void
smb2_revert_current_mid(struct TCP_Server_Info *server, const unsigned int val)
{
	spin_lock(&GlobalMid_Lock);
	if (server->CurrentMid >= val)
		server->CurrentMid -= val;
	spin_unlock(&GlobalMid_Lock);
}

static struct mid_q_entry *
smb2_find_mid(struct TCP_Server_Info *server, char *buf)
{
	struct mid_q_entry *mid;
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;
	__u64 wire_mid = le64_to_cpu(shdr->MessageId);

	if (shdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM) {
		cifs_server_dbg(VFS, "Encrypted frame parsing not supported yet\n");
		return NULL;
	}

	spin_lock(&GlobalMid_Lock);
	list_for_each_entry(mid, &server->pending_mid_q, qhead) {
		if ((mid->mid == wire_mid) &&
		    (mid->mid_state == MID_REQUEST_SUBMITTED) &&
		    (mid->command == shdr->Command)) {
			kref_get(&mid->refcount);
			spin_unlock(&GlobalMid_Lock);
			return mid;
		}
	}
	spin_unlock(&GlobalMid_Lock);
	return NULL;
}

static void
smb2_dump_detail(void *buf, struct TCP_Server_Info *server)
{
#ifdef CONFIG_CIFS_DEBUG2
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;

	cifs_server_dbg(VFS, "Cmd: %d Err: 0x%x Flags: 0x%x Mid: %llu Pid: %d\n",
		 shdr->Command, shdr->Status, shdr->Flags, shdr->MessageId,
		 shdr->ProcessId);
	cifs_server_dbg(VFS, "smb buf %p len %u\n", buf,
		 server->ops->calc_smb_size(buf, server));
#endif
}

static bool
smb2_need_neg(struct TCP_Server_Info *server)
{
	return server->max_read == 0;
}

static int
smb2_negotiate(const unsigned int xid, struct cifs_ses *ses)
{
	int rc;

	cifs_ses_server(ses)->CurrentMid = 0;
	rc = SMB2_negotiate(xid, ses);
	/* BB we probably don't need to retry with modern servers */
	if (rc == -EAGAIN)
		rc = -EHOSTDOWN;
	return rc;
}

static unsigned int
smb2_negotiate_wsize(struct cifs_tcon *tcon, struct smb_vol *volume_info)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int wsize;

	/* start with specified wsize, or default */
	wsize = volume_info->wsize ? volume_info->wsize : CIFS_DEFAULT_IOSIZE;
	wsize = min_t(unsigned int, wsize, server->max_write);
	if (!(server->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU))
		wsize = min_t(unsigned int, wsize, SMB2_MAX_BUFFER_SIZE);

	return wsize;
}

static unsigned int
smb3_negotiate_wsize(struct cifs_tcon *tcon, struct smb_vol *volume_info)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int wsize;

	/* start with specified wsize, or default */
	wsize = volume_info->wsize ? volume_info->wsize : SMB3_DEFAULT_IOSIZE;
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
smb2_negotiate_rsize(struct cifs_tcon *tcon, struct smb_vol *volume_info)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int rsize;

	/* start with specified rsize, or default */
	rsize = volume_info->rsize ? volume_info->rsize : CIFS_DEFAULT_IOSIZE;
	rsize = min_t(unsigned int, rsize, server->max_read);

	if (!(server->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU))
		rsize = min_t(unsigned int, rsize, SMB2_MAX_BUFFER_SIZE);

	return rsize;
}

static unsigned int
smb3_negotiate_rsize(struct cifs_tcon *tcon, struct smb_vol *volume_info)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int rsize;

	/* start with specified rsize, or default */
	rsize = volume_info->rsize ? volume_info->rsize : SMB3_DEFAULT_IOSIZE;
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

static int
parse_server_interfaces(struct network_interface_info_ioctl_rsp *buf,
			size_t buf_len,
			struct cifs_server_iface **iface_list,
			size_t *iface_count)
{
	struct network_interface_info_ioctl_rsp *p;
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
	struct iface_info_ipv4 *p4;
	struct iface_info_ipv6 *p6;
	struct cifs_server_iface *info;
	ssize_t bytes_left;
	size_t next = 0;
	int nb_iface = 0;
	int rc = 0;

	*iface_list = NULL;
	*iface_count = 0;

	/*
	 * Fist pass: count and sanity check
	 */

	bytes_left = buf_len;
	p = buf;
	while (bytes_left >= sizeof(*p)) {
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

	if (bytes_left || p->Next)
		cifs_dbg(VFS, "%s: incomplete interface info\n", __func__);


	/*
	 * Second pass: extract info to internal structure
	 */

	*iface_list = kcalloc(nb_iface, sizeof(**iface_list), GFP_KERNEL);
	if (!*iface_list) {
		rc = -ENOMEM;
		goto out;
	}

	info = *iface_list;
	bytes_left = buf_len;
	p = buf;
	while (bytes_left >= sizeof(*p)) {
		info->speed = le64_to_cpu(p->LinkSpeed);
		info->rdma_capable = le32_to_cpu(p->Capability & RDMA_CAPABLE);
		info->rss_capable = le32_to_cpu(p->Capability & RSS_CAPABLE);

		cifs_dbg(FYI, "%s: adding iface %zu\n", __func__, *iface_count);
		cifs_dbg(FYI, "%s: speed %zu bps\n", __func__, info->speed);
		cifs_dbg(FYI, "%s: capabilities 0x%08x\n", __func__,
			 le32_to_cpu(p->Capability));

		switch (p->Family) {
		/*
		 * The kernel and wire socket structures have the same
		 * layout and use network byte order but make the
		 * conversion explicit in case either one changes.
		 */
		case INTERNETWORK:
			addr4 = (struct sockaddr_in *)&info->sockaddr;
			p4 = (struct iface_info_ipv4 *)p->Buffer;
			addr4->sin_family = AF_INET;
			memcpy(&addr4->sin_addr, &p4->IPv4Address, 4);

			/* [MS-SMB2] 2.2.32.5.1.1 Clients MUST ignore these */
			addr4->sin_port = cpu_to_be16(CIFS_PORT);

			cifs_dbg(FYI, "%s: ipv4 %pI4\n", __func__,
				 &addr4->sin_addr);
			break;
		case INTERNETWORKV6:
			addr6 =	(struct sockaddr_in6 *)&info->sockaddr;
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

		(*iface_count)++;
		info++;
next_iface:
		next = le32_to_cpu(p->Next);
		if (!next)
			break;
		p = (struct network_interface_info_ioctl_rsp *)((u8 *)p+next);
		bytes_left -= next;
	}

	if (!*iface_count) {
		rc = -EINVAL;
		goto out;
	}

out:
	if (rc) {
		kfree(*iface_list);
		*iface_count = 0;
		*iface_list = NULL;
	}
	return rc;
}

static int compare_iface(const void *ia, const void *ib)
{
	const struct cifs_server_iface *a = (struct cifs_server_iface *)ia;
	const struct cifs_server_iface *b = (struct cifs_server_iface *)ib;

	return a->speed == b->speed ? 0 : (a->speed > b->speed ? -1 : 1);
}

static int
SMB3_request_interfaces(const unsigned int xid, struct cifs_tcon *tcon)
{
	int rc;
	unsigned int ret_data_len = 0;
	struct network_interface_info_ioctl_rsp *out_buf = NULL;
	struct cifs_server_iface *iface_list;
	size_t iface_count;
	struct cifs_ses *ses = tcon->ses;

	rc = SMB2_ioctl(xid, tcon, NO_FILE_ID, NO_FILE_ID,
			FSCTL_QUERY_NETWORK_INTERFACE_INFO, true /* is_fsctl */,
			NULL /* no data input */, 0 /* no data input */,
			CIFSMaxBufSize, (char **)&out_buf, &ret_data_len);
	if (rc == -EOPNOTSUPP) {
		cifs_dbg(FYI,
			 "server does not support query network interfaces\n");
		goto out;
	} else if (rc != 0) {
		cifs_tcon_dbg(VFS, "error %d on ioctl to get interface list\n", rc);
		goto out;
	}

	rc = parse_server_interfaces(out_buf, ret_data_len,
				     &iface_list, &iface_count);
	if (rc)
		goto out;

	/* sort interfaces from fastest to slowest */
	sort(iface_list, iface_count, sizeof(*iface_list), compare_iface, NULL);

	spin_lock(&ses->iface_lock);
	kfree(ses->iface_list);
	ses->iface_list = iface_list;
	ses->iface_count = iface_count;
	ses->iface_last_update = jiffies;
	spin_unlock(&ses->iface_lock);

out:
	kfree(out_buf);
	return rc;
}

static void
smb2_close_cached_fid(struct kref *ref)
{
	struct cached_fid *cfid = container_of(ref, struct cached_fid,
					       refcount);

	if (cfid->is_valid) {
		cifs_dbg(FYI, "clear cached root file handle\n");
		SMB2_close(0, cfid->tcon, cfid->fid->persistent_fid,
			   cfid->fid->volatile_fid);
		cfid->is_valid = false;
		cfid->file_all_info_is_valid = false;
		cfid->has_lease = false;
	}
}

void close_shroot(struct cached_fid *cfid)
{
	mutex_lock(&cfid->fid_mutex);
	kref_put(&cfid->refcount, smb2_close_cached_fid);
	mutex_unlock(&cfid->fid_mutex);
}

void close_shroot_lease_locked(struct cached_fid *cfid)
{
	if (cfid->has_lease) {
		cfid->has_lease = false;
		kref_put(&cfid->refcount, smb2_close_cached_fid);
	}
}

void close_shroot_lease(struct cached_fid *cfid)
{
	mutex_lock(&cfid->fid_mutex);
	close_shroot_lease_locked(cfid);
	mutex_unlock(&cfid->fid_mutex);
}

void
smb2_cached_lease_break(struct work_struct *work)
{
	struct cached_fid *cfid = container_of(work,
				struct cached_fid, lease_break);

	close_shroot_lease(cfid);
}

/*
 * Open the directory at the root of a share
 */
int open_shroot(unsigned int xid, struct cifs_tcon *tcon,
		struct cifs_sb_info *cifs_sb,
		struct cached_fid **cfid)
{
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = ses->server;
	struct cifs_open_parms oparms;
	struct smb2_create_rsp *o_rsp = NULL;
	struct smb2_query_info_rsp *qi_rsp = NULL;
	int resp_buftype[2];
	struct smb_rqst rqst[2];
	struct kvec rsp_iov[2];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec qi_iov[1];
	int rc, flags = 0;
	__le16 utf16_path = 0; /* Null - since an open of top of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_II;
	struct cifs_fid *pfid;

	mutex_lock(&tcon->crfid.fid_mutex);
	if (tcon->crfid.is_valid) {
		cifs_dbg(FYI, "found a cached root file handle\n");
		*cfid = &tcon->crfid;
		kref_get(&tcon->crfid.refcount);
		mutex_unlock(&tcon->crfid.fid_mutex);
		return 0;
	}

	/*
	 * We do not hold the lock for the open because in case
	 * SMB2_open needs to reconnect, it will end up calling
	 * cifs_mark_open_files_invalid() which takes the lock again
	 * thus causing a deadlock
	 */

	mutex_unlock(&tcon->crfid.fid_mutex);

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	if (!server->ops->new_lease_key)
		return -EIO;

	pfid = tcon->crfid.fid;
	server->ops->new_lease_key(pfid);

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	/* Open */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms.tcon = tcon;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.fid = pfid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, &utf16_path);
	if (rc)
		goto oshr_free;
	smb2_set_next_command(tcon, &rqst[0]);

	memset(&qi_iov, 0, sizeof(qi_iov));
	rqst[1].rq_iov = qi_iov;
	rqst[1].rq_nvec = 1;

	rc = SMB2_query_info_init(tcon, server,
				  &rqst[1], COMPOUND_FID,
				  COMPOUND_FID, FILE_ALL_INFORMATION,
				  SMB2_O_INFO_FILE, 0,
				  sizeof(struct smb2_file_all_info) +
				  PATH_MAX * 2, 0, NULL);
	if (rc)
		goto oshr_free;

	smb2_set_related(&rqst[1]);

	rc = compound_send_recv(xid, ses, server,
				flags, 2, rqst,
				resp_buftype, rsp_iov);
	mutex_lock(&tcon->crfid.fid_mutex);

	/*
	 * Now we need to check again as the cached root might have
	 * been successfully re-opened from a concurrent process
	 */

	if (tcon->crfid.is_valid) {
		/* work was already done */

		/* stash fids for close() later */
		struct cifs_fid fid = {
			.persistent_fid = pfid->persistent_fid,
			.volatile_fid = pfid->volatile_fid,
		};

		/*
		 * caller expects this func to set pfid to a valid
		 * cached root, so we copy the existing one and get a
		 * reference.
		 */
		memcpy(pfid, tcon->crfid.fid, sizeof(*pfid));
		kref_get(&tcon->crfid.refcount);

		mutex_unlock(&tcon->crfid.fid_mutex);

		if (rc == 0) {
			/* close extra handle outside of crit sec */
			SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
		}
		rc = 0;
		goto oshr_free;
	}

	/* Cached root is still invalid, continue normaly */

	if (rc) {
		if (rc == -EREMCHG) {
			tcon->need_reconnect = true;
			pr_warn_once("server share %s deleted\n",
				     tcon->treeName);
		}
		goto oshr_exit;
	}

	atomic_inc(&tcon->num_remote_opens);

	o_rsp = (struct smb2_create_rsp *)rsp_iov[0].iov_base;
	oparms.fid->persistent_fid = o_rsp->PersistentFileId;
	oparms.fid->volatile_fid = o_rsp->VolatileFileId;
#ifdef CONFIG_CIFS_DEBUG2
	oparms.fid->mid = le64_to_cpu(o_rsp->sync_hdr.MessageId);
#endif /* CIFS_DEBUG2 */

	memcpy(tcon->crfid.fid, pfid, sizeof(struct cifs_fid));
	tcon->crfid.tcon = tcon;
	tcon->crfid.is_valid = true;
	kref_init(&tcon->crfid.refcount);

	/* BB TBD check to see if oplock level check can be removed below */
	if (o_rsp->OplockLevel == SMB2_OPLOCK_LEVEL_LEASE) {
		kref_get(&tcon->crfid.refcount);
		tcon->crfid.has_lease = true;
		smb2_parse_contexts(server, o_rsp,
				&oparms.fid->epoch,
				    oparms.fid->lease_key, &oplock,
				    NULL, NULL);
	} else
		goto oshr_exit;

	qi_rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
	if (le32_to_cpu(qi_rsp->OutputBufferLength) < sizeof(struct smb2_file_all_info))
		goto oshr_exit;
	if (!smb2_validate_and_copy_iov(
				le16_to_cpu(qi_rsp->OutputBufferOffset),
				sizeof(struct smb2_file_all_info),
				&rsp_iov[1], sizeof(struct smb2_file_all_info),
				(char *)&tcon->crfid.file_all_info))
		tcon->crfid.file_all_info_is_valid = true;

oshr_exit:
	mutex_unlock(&tcon->crfid.fid_mutex);
oshr_free:
	SMB2_open_free(&rqst[0]);
	SMB2_query_info_free(&rqst[1]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	if (rc == 0)
		*cfid = &tcon->crfid;
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
	bool no_cached_open = tcon->nohandlecache;
	struct cached_fid *cfid = NULL;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = &fid;
	oparms.reconnect = false;

	if (no_cached_open) {
		rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL,
			       NULL, NULL);
	} else {
		rc = open_shroot(xid, tcon, cifs_sb, &cfid);
		if (rc == 0)
			memcpy(&fid, cfid->fid, sizeof(struct cifs_fid));
	}
	if (rc)
		return;

	SMB3_request_interfaces(xid, tcon);

	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_ATTRIBUTE_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_DEVICE_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_VOLUME_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_SECTOR_SIZE_INFORMATION); /* SMB3 specific */
	if (no_cached_open)
		SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	else
		close_shroot(cfid);
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

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = &fid;
	oparms.reconnect = false;

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
	int rc;
	__le16 *utf16_path;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;

	if ((*full_path == 0) && tcon->crfid.is_valid)
		return 0;

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL,
		       NULL);
	if (rc) {
		kfree(utf16_path);
		return rc;
	}

	rc = SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	kfree(utf16_path);
	return rc;
}

static int
smb2_get_srv_inum(const unsigned int xid, struct cifs_tcon *tcon,
		  struct cifs_sb_info *cifs_sb, const char *full_path,
		  u64 *uniqueid, FILE_ALL_INFO *data)
{
	*uniqueid = le64_to_cpu(data->IndexNumber);
	return 0;
}

static int
smb2_query_file_info(const unsigned int xid, struct cifs_tcon *tcon,
		     struct cifs_fid *fid, FILE_ALL_INFO *data)
{
	int rc;
	struct smb2_file_all_info *smb2_data;

	smb2_data = kzalloc(sizeof(struct smb2_file_all_info) + PATH_MAX * 2,
			    GFP_KERNEL);
	if (smb2_data == NULL)
		return -ENOMEM;

	rc = SMB2_query_info(xid, tcon, fid->persistent_fid, fid->volatile_fid,
			     smb2_data);
	if (!rc)
		move_smb2_info_to_cifs(data, smb2_data);
	kfree(smb2_data);
	return rc;
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
		name = &src->ea_data[0];
		name_len = (size_t)src->ea_name_length;
		value = &src->ea_data[src->ea_name_length + 1];
		value_len = (size_t)le16_to_cpu(src->ea_value_length);

		if (name_len == 0)
			break;

		if (src_size < 8 + name_len + 1 + value_len) {
			cifs_dbg(FYI, "EA entry goes beyond length of list\n");
			rc = -EIO;
			goto out;
		}

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
	__le16 *utf16_path;
	struct kvec rsp_iov = {NULL, 0};
	int buftype = CIFS_NO_BUFFER;
	struct smb2_query_info_rsp *rsp;
	struct smb2_file_full_ea_info *info = NULL;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	rc = smb2_query_info_compound(xid, tcon, utf16_path,
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
	kfree(utf16_path);
	free_rsp_buf(buftype, rsp_iov.iov_base);
	return rc;
}


static int
smb2_set_ea(const unsigned int xid, struct cifs_tcon *tcon,
	    const char *path, const char *ea_name, const void *ea_value,
	    const __u16 ea_value_len, const struct nls_table *nls_codepage,
	    struct cifs_sb_info *cifs_sb)
{
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	__le16 *utf16_path = NULL;
	int ea_name_len = strlen(ea_name);
	int flags = 0;
	int len;
	struct smb_rqst rqst[3];
	int resp_buftype[3];
	struct kvec rsp_iov[3];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct cifs_open_parms oparms;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_fid fid;
	struct kvec si_iov[SMB2_SET_INFO_IOV_SIZE];
	unsigned int size[1];
	void *data[1];
	struct smb2_file_full_ea_info *ea = NULL;
	struct kvec close_iov[1];
	struct smb2_query_info_rsp *rsp;
	int rc, used_len = 0;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	if (ea_name_len > 255)
		return -EINVAL;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

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
			rc = smb2_query_info_compound(xid, tcon, utf16_path,
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
			if(CIFSMaxBufSize - MAX_SMB2_CREATE_RESPONSE_SIZE -
			   MAX_SMB2_CLOSE_RESPONSE_SIZE - 256 <
			   used_len + ea_name_len + ea_value_len + 1) {
				rc = -ENOSPC;
				goto sea_exit;
			}
		}
	}

	/* Open */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	memset(&oparms, 0, sizeof(oparms));
	oparms.tcon = tcon;
	oparms.desired_access = FILE_WRITE_EA;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto sea_exit;
	smb2_set_next_command(tcon, &rqst[0]);


	/* Set Info */
	memset(&si_iov, 0, sizeof(si_iov));
	rqst[1].rq_iov = si_iov;
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
	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);


	/* Close */
	memset(&close_iov, 0, sizeof(close_iov));
	rqst[2].rq_iov = close_iov;
	rqst[2].rq_nvec = 1;
	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, ses, server,
				flags, 3, rqst,
				resp_buftype, rsp_iov);
	/* no need to bump num_remote_opens because handle immediately closed */

 sea_exit:
	kfree(ea);
	kfree(utf16_path);
	SMB2_open_free(&rqst[0]);
	SMB2_set_info_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
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
		inode->i_mtime = cifs_NTtimeToUnix(file_inf.LastWriteTime);
	if (file_inf.ChangeTime)
		inode->i_ctime = cifs_NTtimeToUnix(file_inf.ChangeTime);
	if (file_inf.LastAccessTime)
		inode->i_atime = cifs_NTtimeToUnix(file_inf.LastAccessTime);

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
			FSCTL_SRV_REQUEST_RESUME_KEY, true /* is_fsctl */,
			NULL, 0 /* no input */, CIFSMaxBufSize,
			(char **)&res_key, &ret_data_len);

	if (rc) {
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

struct iqi_vars {
	struct smb_rqst rqst[3];
	struct kvec rsp_iov[3];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec qi_iov[1];
	struct kvec io_iov[SMB2_IOCTL_IOV_SIZE];
	struct kvec si_iov[SMB2_SET_INFO_IOV_SIZE];
	struct kvec close_iov[1];
};

static int
smb2_ioctl_query_info(const unsigned int xid,
		      struct cifs_tcon *tcon,
		      struct cifs_sb_info *cifs_sb,
		      __le16 *path, int is_dir,
		      unsigned long p)
{
	struct iqi_vars *vars;
	struct smb_rqst *rqst;
	struct kvec *rsp_iov;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	char __user *arg = (char __user *)p;
	struct smb_query_info qi;
	struct smb_query_info __user *pqi;
	int rc = 0;
	int flags = 0;
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

	vars = kzalloc(sizeof(*vars), GFP_ATOMIC);
	if (vars == NULL)
		return -ENOMEM;
	rqst = &vars->rqst[0];
	rsp_iov = &vars->rsp_iov[0];

	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;

	if (copy_from_user(&qi, arg, sizeof(struct smb_query_info)))
		goto e_fault;

	if (qi.output_buffer_length > 1024) {
		kfree(vars);
		return -EINVAL;
	}

	if (!ses || !server) {
		kfree(vars);
		return -EIO;
	}

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	buffer = memdup_user(arg + sizeof(struct smb_query_info),
			     qi.output_buffer_length);
	if (IS_ERR(buffer)) {
		kfree(vars);
		return PTR_ERR(buffer);
	}

	/* Open */
	rqst[0].rq_iov = &vars->open_iov[0];
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	memset(&oparms, 0, sizeof(oparms));
	oparms.tcon = tcon;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, create_options);
	oparms.fid = &fid;
	oparms.reconnect = false;

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
		goto iqinf_exit;
	smb2_set_next_command(tcon, &rqst[0]);

	/* Query */
	if (qi.flags & PASSTHRU_FSCTL) {
		/* Can eventually relax perm check since server enforces too */
		if (!capable(CAP_SYS_ADMIN))
			rc = -EPERM;
		else  {
			rqst[1].rq_iov = &vars->io_iov[0];
			rqst[1].rq_nvec = SMB2_IOCTL_IOV_SIZE;

			rc = SMB2_ioctl_init(tcon, server,
					     &rqst[1],
					     COMPOUND_FID, COMPOUND_FID,
					     qi.info_type, true, buffer,
					     qi.output_buffer_length,
					     CIFSMaxBufSize -
					     MAX_SMB2_CREATE_RESPONSE_SIZE -
					     MAX_SMB2_CLOSE_RESPONSE_SIZE);
		}
	} else if (qi.flags == PASSTHRU_SET_INFO) {
		/* Can eventually relax perm check since server enforces too */
		if (!capable(CAP_SYS_ADMIN))
			rc = -EPERM;
		else  {
			rqst[1].rq_iov = &vars->si_iov[0];
			rqst[1].rq_nvec = 1;

			size[0] = 8;
			data[0] = buffer;

			rc = SMB2_set_info_init(tcon, server,
					&rqst[1],
					COMPOUND_FID, COMPOUND_FID,
					current->tgid,
					FILE_END_OF_FILE_INFORMATION,
					SMB2_O_INFO_FILE, 0, data, size);
		}
	} else if (qi.flags == PASSTHRU_QUERY_INFO) {
		rqst[1].rq_iov = &vars->qi_iov[0];
		rqst[1].rq_nvec = 1;

		rc = SMB2_query_info_init(tcon, server,
				  &rqst[1], COMPOUND_FID,
				  COMPOUND_FID, qi.file_info_class,
				  qi.info_type, qi.additional_information,
				  qi.input_buffer_length,
				  qi.output_buffer_length, buffer);
	} else { /* unknown flags */
		cifs_tcon_dbg(VFS, "Invalid passthru query flags: 0x%x\n",
			      qi.flags);
		rc = -EINVAL;
	}

	if (rc)
		goto iqinf_exit;
	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);

	/* Close */
	rqst[2].rq_iov = &vars->close_iov[0];
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	if (rc)
		goto iqinf_exit;
	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, ses, server,
				flags, 3, rqst,
				resp_buftype, rsp_iov);
	if (rc)
		goto iqinf_exit;

	/* No need to bump num_remote_opens since handle immediately closed */
	if (qi.flags & PASSTHRU_FSCTL) {
		pqi = (struct smb_query_info __user *)arg;
		io_rsp = (struct smb2_ioctl_rsp *)rsp_iov[1].iov_base;
		if (le32_to_cpu(io_rsp->OutputCount) < qi.input_buffer_length)
			qi.input_buffer_length = le32_to_cpu(io_rsp->OutputCount);
		if (qi.input_buffer_length > 0 &&
		    le32_to_cpu(io_rsp->OutputOffset) + qi.input_buffer_length
		    > rsp_iov[1].iov_len)
			goto e_fault;

		if (copy_to_user(&pqi->input_buffer_length,
				 &qi.input_buffer_length,
				 sizeof(qi.input_buffer_length)))
			goto e_fault;

		if (copy_to_user((void __user *)pqi + sizeof(struct smb_query_info),
				 (const void *)io_rsp + le32_to_cpu(io_rsp->OutputOffset),
				 qi.input_buffer_length))
			goto e_fault;
	} else {
		pqi = (struct smb_query_info __user *)arg;
		qi_rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
		if (le32_to_cpu(qi_rsp->OutputBufferLength) < qi.input_buffer_length)
			qi.input_buffer_length = le32_to_cpu(qi_rsp->OutputBufferLength);
		if (copy_to_user(&pqi->input_buffer_length,
				 &qi.input_buffer_length,
				 sizeof(qi.input_buffer_length)))
			goto e_fault;

		if (copy_to_user(pqi + 1, qi_rsp->Buffer,
				 qi.input_buffer_length))
			goto e_fault;
	}

 iqinf_exit:
	kfree(vars);
	kfree(buffer);
	SMB2_open_free(&rqst[0]);
	if (qi.flags & PASSTHRU_FSCTL)
		SMB2_ioctl_free(&rqst[1]);
	else
		SMB2_query_info_free(&rqst[1]);

	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	return rc;

e_fault:
	rc = -EFAULT;
	goto iqinf_exit;
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
			cpu_to_le32(min_t(u32, len, tcon->max_bytes_chunk));

		/* Request server copy to target from src identified by key */
		rc = SMB2_ioctl(xid, tcon, trgtfile->fid.persistent_fid,
			trgtfile->fid.volatile_fid, FSCTL_SRV_COPYCHUNK_WRITE,
			true /* is_fsctl */, (char *)pcchunk,
			sizeof(struct copychunk_ioctl),	CIFSMaxBufSize,
			(char **)&retbuf, &ret_data_len);
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
			true /* is_fctl */,
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

	rc = smb2_set_file_size(xid, tcon, trgtfile, dest_off + len, false);
	if (rc)
		goto duplicate_extents_out;

	rc = SMB2_ioctl(xid, tcon, trgtfile->fid.persistent_fid,
			trgtfile->fid.volatile_fid,
			FSCTL_DUPLICATE_EXTENTS_TO_FILE,
			true /* is_fsctl */,
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
			true /* is_fsctl */,
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
			true /* is_fsctl */,
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
	    void __user *ioc_buf)
{
	struct smb3_notify notify;
	struct dentry *dentry = pfile->f_path.dentry;
	struct inode *inode = file_inode(pfile);
	struct cifs_sb_info *cifs_sb;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct cifs_tcon *tcon;
	unsigned char *path = NULL;
	__le16 *utf16_path = NULL;
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	int rc = 0;

	path = build_path_from_dentry(dentry);
	if (path == NULL)
		return -ENOMEM;

	cifs_sb = CIFS_SB(inode->i_sb);

	utf16_path = cifs_convert_path_to_utf16(path + 1, cifs_sb);
	if (utf16_path == NULL) {
		rc = -ENOMEM;
		goto notify_exit;
	}

	if (copy_from_user(&notify, ioc_buf, sizeof(struct smb3_notify))) {
		rc = -EFAULT;
		goto notify_exit;
	}

	tcon = cifs_sb_master_tcon(cifs_sb);
	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES | FILE_READ_DATA;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL,
		       NULL);
	if (rc)
		goto notify_exit;

	rc = SMB2_change_notify(xid, tcon, fid.persistent_fid, fid.volatile_fid,
				notify.watch_tree, notify.completion_filter);

	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);

	cifs_dbg(FYI, "change notify for path %s rc %d\n", path, rc);

notify_exit:
	kfree(path);
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

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES | FILE_READ_DATA;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = fid;
	oparms.reconnect = false;

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

	rc = compound_send_recv(xid, tcon->ses, server,
				flags, 2, rqst,
				resp_buftype, rsp_iov);

	/* If the open failed there is nothing to do */
	op_rsp = (struct smb2_create_rsp *)rsp_iov[0].iov_base;
	if (op_rsp == NULL || op_rsp->sync_hdr.Status != STATUS_SUCCESS) {
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
	if (qd_rsp->sync_hdr.Status == STATUS_NO_MORE_FILES) {
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
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;

	if (shdr->Status != STATUS_PENDING)
		return false;

	if (shdr->CreditRequest) {
		spin_lock(&server->req_lock);
		server->credits += le16_to_cpu(shdr->CreditRequest);
		spin_unlock(&server->req_lock);
		wake_up(&server->request_q);
	}

	return true;
}

static bool
smb2_is_session_expired(char *buf)
{
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;

	if (shdr->Status != STATUS_NETWORK_SESSION_EXPIRED &&
	    shdr->Status != STATUS_USER_SESSION_DELETED)
		return false;

	trace_smb3_ses_expired(shdr->TreeId, shdr->SessionId,
			       le16_to_cpu(shdr->Command),
			       le64_to_cpu(shdr->MessageId));
	cifs_dbg(FYI, "Session expired or deleted\n");

	return true;
}

static bool
smb2_is_status_io_timeout(char *buf)
{
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;

	if (shdr->Status == STATUS_IO_TIMEOUT)
		return true;
	else
		return false;
}

static int
smb2_oplock_response(struct cifs_tcon *tcon, struct cifs_fid *fid,
		     struct cifsInodeInfo *cinode)
{
	if (tcon->ses->server->capabilities & SMB2_GLOBAL_CAP_LEASING)
		return SMB2_lease_break(0, tcon, cinode->lease_key,
					smb2_get_lease_state(cinode));

	return SMB2_oplock_break(0, tcon, fid->persistent_fid,
				 fid->volatile_fid,
				 CIFS_CACHE_READ(cinode) ? 1 : 0);
}

void
smb2_set_related(struct smb_rqst *rqst)
{
	struct smb2_sync_hdr *shdr;

	shdr = (struct smb2_sync_hdr *)(rqst->rq_iov[0].iov_base);
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
	struct smb2_sync_hdr *shdr;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = ses->server;
	unsigned long len = smb_rqst_len(server, rqst);
	int i, num_padding;

	shdr = (struct smb2_sync_hdr *)(rqst->rq_iov[0].iov_base);
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
			 __le16 *utf16_path, u32 desired_access,
			 u32 class, u32 type, u32 output_len,
			 struct kvec *rsp, int *buftype,
			 struct cifs_sb_info *cifs_sb)
{
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	int flags = 0;
	struct smb_rqst rqst[3];
	int resp_buftype[3];
	struct kvec rsp_iov[3];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec qi_iov[1];
	struct kvec close_iov[1];
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	int rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms.tcon = tcon;
	oparms.desired_access = desired_access;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto qic_exit;
	smb2_set_next_command(tcon, &rqst[0]);

	memset(&qi_iov, 0, sizeof(qi_iov));
	rqst[1].rq_iov = qi_iov;
	rqst[1].rq_nvec = 1;

	rc = SMB2_query_info_init(tcon, server,
				  &rqst[1], COMPOUND_FID, COMPOUND_FID,
				  class, type, 0,
				  output_len, 0,
				  NULL);
	if (rc)
		goto qic_exit;
	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);

	memset(&close_iov, 0, sizeof(close_iov));
	rqst[2].rq_iov = close_iov;
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	if (rc)
		goto qic_exit;
	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, ses, server,
				flags, 3, rqst,
				resp_buftype, rsp_iov);
	if (rc) {
		free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
		if (rc == -EREMCHG) {
			tcon->need_reconnect = true;
			pr_warn_once("server share %s deleted\n",
				     tcon->treeName);
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
	return rc;
}

static int
smb2_queryfs(const unsigned int xid, struct cifs_tcon *tcon,
	     struct cifs_sb_info *cifs_sb, struct kstatfs *buf)
{
	struct smb2_query_info_rsp *rsp;
	struct smb2_fs_full_size_info *info = NULL;
	__le16 utf16_path = 0; /* Null - open root of share */
	struct kvec rsp_iov = {NULL, 0};
	int buftype = CIFS_NO_BUFFER;
	int rc;


	rc = smb2_query_info_compound(xid, tcon, &utf16_path,
				      FILE_READ_ATTRIBUTES,
				      FS_FULL_SIZE_INFORMATION,
				      SMB2_O_INFO_FILESYSTEM,
				      sizeof(struct smb2_fs_full_size_info),
				      &rsp_iov, &buftype, cifs_sb);
	if (rc)
		goto qfs_exit;

	rsp = (struct smb2_query_info_rsp *)rsp_iov.iov_base;
	buf->f_type = SMB2_MAGIC_NUMBER;
	info = (struct smb2_fs_full_size_info *)(
		le16_to_cpu(rsp->OutputBufferOffset) + (char *)rsp);
	rc = smb2_validate_iov(le16_to_cpu(rsp->OutputBufferOffset),
			       le32_to_cpu(rsp->OutputBufferLength),
			       &rsp_iov,
			       sizeof(struct smb2_fs_full_size_info));
	if (!rc)
		smb2_copy_fs_info_to_kstatfs(info, buf);

qfs_exit:
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

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL,
		       NULL, NULL);
	if (rc)
		return rc;

	rc = SMB311_posix_qfs_info(xid, tcon, fid.persistent_fid,
				   fid.volatile_fid, buf);
	buf->f_type = SMB2_MAGIC_NUMBER;
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
				true /* is_fsctl */,
				(char *)dfs_req, dfs_req_size, CIFSMaxBufSize,
				(char **)&dfs_rsp, &dfs_rsp_size);
	} while (rc == -EAGAIN);

	if (rc) {
		if ((rc != -ENOENT) && (rc != -EOPNOTSUPP))
			cifs_tcon_dbg(VFS, "ioctl error in %s rc=%d\n", __func__, rc);
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
		spin_unlock(&cifs_tcp_ses_lock);
	}
	kfree(utf16_path);
	kfree(dfs_req);
	kfree(dfs_rsp);
	return rc;
}

static int
parse_reparse_posix(struct reparse_posix_data *symlink_buf,
		      u32 plen, char **target_path,
		      struct cifs_sb_info *cifs_sb)
{
	unsigned int len;

	/* See MS-FSCC 2.1.2.6 for the 'NFS' style reparse tags */
	len = le16_to_cpu(symlink_buf->ReparseDataLength);

	if (le64_to_cpu(symlink_buf->InodeType) != NFS_SPECFILE_LNK) {
		cifs_dbg(VFS, "%lld not a supported symlink type\n",
			le64_to_cpu(symlink_buf->InodeType));
		return -EOPNOTSUPP;
	}

	*target_path = cifs_strndup_from_utf16(
				symlink_buf->PathBuffer,
				len, true, cifs_sb->local_nls);
	if (!(*target_path))
		return -ENOMEM;

	convert_delimiter(*target_path, '/');
	cifs_dbg(FYI, "%s: target path: %s\n", __func__, *target_path);

	return 0;
}

static int
parse_reparse_symlink(struct reparse_symlink_data_buffer *symlink_buf,
		      u32 plen, char **target_path,
		      struct cifs_sb_info *cifs_sb)
{
	unsigned int sub_len;
	unsigned int sub_offset;

	/* We handle Symbolic Link reparse tag here. See: MS-FSCC 2.1.2.4 */

	sub_offset = le16_to_cpu(symlink_buf->SubstituteNameOffset);
	sub_len = le16_to_cpu(symlink_buf->SubstituteNameLength);
	if (sub_offset + 20 > plen ||
	    sub_offset + sub_len + 20 > plen) {
		cifs_dbg(VFS, "srv returned malformed symlink buffer\n");
		return -EIO;
	}

	*target_path = cifs_strndup_from_utf16(
				symlink_buf->PathBuffer + sub_offset,
				sub_len, true, cifs_sb->local_nls);
	if (!(*target_path))
		return -ENOMEM;

	convert_delimiter(*target_path, '/');
	cifs_dbg(FYI, "%s: target path: %s\n", __func__, *target_path);

	return 0;
}

static int
parse_reparse_point(struct reparse_data_buffer *buf,
		    u32 plen, char **target_path,
		    struct cifs_sb_info *cifs_sb)
{
	if (plen < sizeof(struct reparse_data_buffer)) {
		cifs_dbg(VFS, "reparse buffer is too small. Must be at least 8 bytes but was %d\n",
			 plen);
		return -EIO;
	}

	if (plen < le16_to_cpu(buf->ReparseDataLength) +
	    sizeof(struct reparse_data_buffer)) {
		cifs_dbg(VFS, "srv returned invalid reparse buf length: %d\n",
			 plen);
		return -EIO;
	}

	/* See MS-FSCC 2.1.2 */
	switch (le32_to_cpu(buf->ReparseTag)) {
	case IO_REPARSE_TAG_NFS:
		return parse_reparse_posix(
			(struct reparse_posix_data *)buf,
			plen, target_path, cifs_sb);
	case IO_REPARSE_TAG_SYMLINK:
		return parse_reparse_symlink(
			(struct reparse_symlink_data_buffer *)buf,
			plen, target_path, cifs_sb);
	default:
		cifs_dbg(VFS, "srv returned unknown symlink buffer tag:0x%08x\n",
			 le32_to_cpu(buf->ReparseTag));
		return -EOPNOTSUPP;
	}
}

#define SMB2_SYMLINK_STRUCT_SIZE \
	(sizeof(struct smb2_err_rsp) - 1 + sizeof(struct smb2_symlink_err_rsp))

static int
smb2_query_symlink(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifs_sb_info *cifs_sb, const char *full_path,
		   char **target_path, bool is_reparse_point)
{
	int rc;
	__le16 *utf16_path = NULL;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct kvec err_iov = {NULL, 0};
	struct smb2_err_rsp *err_buf = NULL;
	struct smb2_symlink_err_rsp *symlink;
	struct TCP_Server_Info *server = cifs_pick_channel(tcon->ses);
	unsigned int sub_len;
	unsigned int sub_offset;
	unsigned int print_len;
	unsigned int print_offset;
	int flags = 0;
	struct smb_rqst rqst[3];
	int resp_buftype[3];
	struct kvec rsp_iov[3];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec io_iov[SMB2_IOCTL_IOV_SIZE];
	struct kvec close_iov[1];
	struct smb2_create_rsp *create_rsp;
	struct smb2_ioctl_rsp *ioctl_rsp;
	struct reparse_data_buffer *reparse_buf;
	int create_options = is_reparse_point ? OPEN_REPARSE_POINT : 0;
	u32 plen;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, full_path);

	*target_path = NULL;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	/* Open */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	memset(&oparms, 0, sizeof(oparms));
	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, create_options);
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto querty_exit;
	smb2_set_next_command(tcon, &rqst[0]);


	/* IOCTL */
	memset(&io_iov, 0, sizeof(io_iov));
	rqst[1].rq_iov = io_iov;
	rqst[1].rq_nvec = SMB2_IOCTL_IOV_SIZE;

	rc = SMB2_ioctl_init(tcon, server,
			     &rqst[1], fid.persistent_fid,
			     fid.volatile_fid, FSCTL_GET_REPARSE_POINT,
			     true /* is_fctl */, NULL, 0,
			     CIFSMaxBufSize -
			     MAX_SMB2_CREATE_RESPONSE_SIZE -
			     MAX_SMB2_CLOSE_RESPONSE_SIZE);
	if (rc)
		goto querty_exit;

	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);


	/* Close */
	memset(&close_iov, 0, sizeof(close_iov));
	rqst[2].rq_iov = close_iov;
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, server,
			     &rqst[2], COMPOUND_FID, COMPOUND_FID, false);
	if (rc)
		goto querty_exit;

	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, tcon->ses, server,
				flags, 3, rqst,
				resp_buftype, rsp_iov);

	create_rsp = rsp_iov[0].iov_base;
	if (create_rsp && create_rsp->sync_hdr.Status)
		err_iov = rsp_iov[0];
	ioctl_rsp = rsp_iov[1].iov_base;

	/*
	 * Open was successful and we got an ioctl response.
	 */
	if ((rc == 0) && (is_reparse_point)) {
		/* See MS-FSCC 2.3.23 */

		reparse_buf = (struct reparse_data_buffer *)
			((char *)ioctl_rsp +
			 le32_to_cpu(ioctl_rsp->OutputOffset));
		plen = le32_to_cpu(ioctl_rsp->OutputCount);

		if (plen + le32_to_cpu(ioctl_rsp->OutputOffset) >
		    rsp_iov[1].iov_len) {
			cifs_tcon_dbg(VFS, "srv returned invalid ioctl len: %d\n",
				 plen);
			rc = -EIO;
			goto querty_exit;
		}

		rc = parse_reparse_point(reparse_buf, plen, target_path,
					 cifs_sb);
		goto querty_exit;
	}

	if (!rc || !err_iov.iov_base) {
		rc = -ENOENT;
		goto querty_exit;
	}

	err_buf = err_iov.iov_base;
	if (le32_to_cpu(err_buf->ByteCount) < sizeof(struct smb2_symlink_err_rsp) ||
	    err_iov.iov_len < SMB2_SYMLINK_STRUCT_SIZE) {
		rc = -EINVAL;
		goto querty_exit;
	}

	symlink = (struct smb2_symlink_err_rsp *)err_buf->ErrorData;
	if (le32_to_cpu(symlink->SymLinkErrorTag) != SYMLINK_ERROR_TAG ||
	    le32_to_cpu(symlink->ReparseTag) != IO_REPARSE_TAG_SYMLINK) {
		rc = -EINVAL;
		goto querty_exit;
	}

	/* open must fail on symlink - reset rc */
	rc = 0;
	sub_len = le16_to_cpu(symlink->SubstituteNameLength);
	sub_offset = le16_to_cpu(symlink->SubstituteNameOffset);
	print_len = le16_to_cpu(symlink->PrintNameLength);
	print_offset = le16_to_cpu(symlink->PrintNameOffset);

	if (err_iov.iov_len < SMB2_SYMLINK_STRUCT_SIZE + sub_offset + sub_len) {
		rc = -EINVAL;
		goto querty_exit;
	}

	if (err_iov.iov_len <
	    SMB2_SYMLINK_STRUCT_SIZE + print_offset + print_len) {
		rc = -EINVAL;
		goto querty_exit;
	}

	*target_path = cifs_strndup_from_utf16(
				(char *)symlink->PathBuffer + sub_offset,
				sub_len, true, cifs_sb->local_nls);
	if (!(*target_path)) {
		rc = -ENOMEM;
		goto querty_exit;
	}
	convert_delimiter(*target_path, '/');
	cifs_dbg(FYI, "%s: target path: %s\n", __func__, *target_path);

 querty_exit:
	cifs_dbg(FYI, "query symlink rc %d\n", rc);
	kfree(utf16_path);
	SMB2_open_free(&rqst[0]);
	SMB2_ioctl_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	return rc;
}

int
smb2_query_reparse_tag(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifs_sb_info *cifs_sb, const char *full_path,
		   __u32 *tag)
{
	int rc;
	__le16 *utf16_path = NULL;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct TCP_Server_Info *server = cifs_pick_channel(tcon->ses);
	int flags = 0;
	struct smb_rqst rqst[3];
	int resp_buftype[3];
	struct kvec rsp_iov[3];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec io_iov[SMB2_IOCTL_IOV_SIZE];
	struct kvec close_iov[1];
	struct smb2_ioctl_rsp *ioctl_rsp;
	struct reparse_data_buffer *reparse_buf;
	u32 plen;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, full_path);

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	/*
	 * setup smb2open - TODO add optimization to call cifs_get_readable_path
	 * to see if there is a handle already open that we can use
	 */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	memset(&oparms, 0, sizeof(oparms));
	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = cifs_create_options(cifs_sb, OPEN_REPARSE_POINT);
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
	if (rc)
		goto query_rp_exit;
	smb2_set_next_command(tcon, &rqst[0]);


	/* IOCTL */
	memset(&io_iov, 0, sizeof(io_iov));
	rqst[1].rq_iov = io_iov;
	rqst[1].rq_nvec = SMB2_IOCTL_IOV_SIZE;

	rc = SMB2_ioctl_init(tcon, server,
			     &rqst[1], fid.persistent_fid,
			     fid.volatile_fid, FSCTL_GET_REPARSE_POINT,
			     true /* is_fctl */, NULL, 0,
			     CIFSMaxBufSize -
			     MAX_SMB2_CREATE_RESPONSE_SIZE -
			     MAX_SMB2_CLOSE_RESPONSE_SIZE);
	if (rc)
		goto query_rp_exit;

	smb2_set_next_command(tcon, &rqst[1]);
	smb2_set_related(&rqst[1]);


	/* Close */
	memset(&close_iov, 0, sizeof(close_iov));
	rqst[2].rq_iov = close_iov;
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

		reparse_buf = (struct reparse_data_buffer *)
			((char *)ioctl_rsp +
			 le32_to_cpu(ioctl_rsp->OutputOffset));
		plen = le32_to_cpu(ioctl_rsp->OutputCount);

		if (plen + le32_to_cpu(ioctl_rsp->OutputOffset) >
		    rsp_iov[1].iov_len) {
			cifs_tcon_dbg(FYI, "srv returned invalid ioctl len: %d\n",
				 plen);
			rc = -EIO;
			goto query_rp_exit;
		}
		*tag = le32_to_cpu(reparse_buf->ReparseTag);
	}

 query_rp_exit:
	kfree(utf16_path);
	SMB2_open_free(&rqst[0]);
	SMB2_ioctl_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	return rc;
}

static struct cifs_ntsd *
get_smb2_acl_by_fid(struct cifs_sb_info *cifs_sb,
		const struct cifs_fid *cifsfid, u32 *pacllen)
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
			    cifsfid->volatile_fid, (void **)&pntsd, pacllen);
	free_xid(xid);

	cifs_put_tlink(tlink);

	cifs_dbg(FYI, "%s: rc = %d ACL len %d\n", __func__, rc, *pacllen);
	if (rc)
		return ERR_PTR(rc);
	return pntsd;

}

static struct cifs_ntsd *
get_smb2_acl_by_path(struct cifs_sb_info *cifs_sb,
		const char *path, u32 *pacllen)
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

	oparms.tcon = tcon;
	oparms.desired_access = READ_CONTROL;
	oparms.disposition = FILE_OPEN;
	/*
	 * When querying an ACL, even if the file is a symlink we want to open
	 * the source not the target, and so the protocol requires that the
	 * client specify this flag when opening a reparse point
	 */
	oparms.create_options = cifs_create_options(cifs_sb, 0) | OPEN_REPARSE_POINT;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL,
		       NULL);
	kfree(utf16_path);
	if (!rc) {
		rc = SMB2_query_acl(xid, tlink_tcon(tlink), fid.persistent_fid,
			    fid.volatile_fid, (void **)&pntsd, pacllen);
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

	if (aclflag == CIFS_ACL_OWNER || aclflag == CIFS_ACL_GROUP)
		access_flags = WRITE_OWNER;
	else
		access_flags = WRITE_DAC;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path) {
		rc = -ENOMEM;
		free_xid(xid);
		return rc;
	}

	oparms.tcon = tcon;
	oparms.desired_access = access_flags;
	oparms.create_options = cifs_create_options(cifs_sb, 0);
	oparms.disposition = FILE_OPEN;
	oparms.path = path;
	oparms.fid = &fid;
	oparms.reconnect = false;

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
				      u32 *pacllen)
{
	struct cifs_ntsd *pntsd = NULL;
	struct cifsFileInfo *open_file = NULL;

	if (inode)
		open_file = find_readable_file(CIFS_I(inode), true);
	if (!open_file)
		return get_smb2_acl_by_path(cifs_sb, path, pacllen);

	pntsd = get_smb2_acl_by_fid(cifs_sb, &open_file->fid, pacllen);
	cifsFileInfo_put(open_file);
	return pntsd;
}

static long smb3_zero_range(struct file *file, struct cifs_tcon *tcon,
			    loff_t offset, loff_t len, bool keep_size)
{
	struct cifs_ses *ses = tcon->ses;
	struct inode *inode;
	struct cifsInodeInfo *cifsi;
	struct cifsFileInfo *cfile = file->private_data;
	struct file_zero_data_information fsctl_buf;
	long rc;
	unsigned int xid;
	__le64 eof;

	xid = get_xid();

	inode = d_inode(cfile->dentry);
	cifsi = CIFS_I(inode);

	trace_smb3_zero_enter(xid, cfile->fid.persistent_fid, tcon->tid,
			      ses->Suid, offset, len);

	/*
	 * We zero the range through ioctl, so we need remove the page caches
	 * first, otherwise the data may be inconsistent with the server.
	 */
	truncate_pagecache_range(inode, offset, offset + len - 1);

	/* if file not oplocked can't be sure whether asking to extend size */
	if (!CIFS_CACHE_READ(cifsi))
		if (keep_size == false) {
			rc = -EOPNOTSUPP;
			trace_smb3_zero_err(xid, cfile->fid.persistent_fid,
				tcon->tid, ses->Suid, offset, len, rc);
			free_xid(xid);
			return rc;
		}

	cifs_dbg(FYI, "Offset %lld len %lld\n", offset, len);

	fsctl_buf.FileOffset = cpu_to_le64(offset);
	fsctl_buf.BeyondFinalZero = cpu_to_le64(offset + len);

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid, FSCTL_SET_ZERO_DATA, true,
			(char *)&fsctl_buf,
			sizeof(struct file_zero_data_information),
			0, NULL, NULL);
	if (rc)
		goto zero_range_exit;

	/*
	 * do we also need to change the size of the file?
	 */
	if (keep_size == false && i_size_read(inode) < offset + len) {
		eof = cpu_to_le64(offset + len);
		rc = SMB2_set_eof(xid, tcon, cfile->fid.persistent_fid,
				  cfile->fid.volatile_fid, cfile->pid, &eof);
	}

 zero_range_exit:
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
	struct inode *inode;
	struct cifsFileInfo *cfile = file->private_data;
	struct file_zero_data_information fsctl_buf;
	long rc;
	unsigned int xid;
	__u8 set_sparse = 1;

	xid = get_xid();

	inode = d_inode(cfile->dentry);

	/* Need to make file sparse, if not already, before freeing range. */
	/* Consider adding equivalent for compressed since it could also work */
	if (!smb2_set_sparse(xid, tcon, cfile, inode, set_sparse)) {
		rc = -EOPNOTSUPP;
		free_xid(xid);
		return rc;
	}

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
			true /* is_fctl */, (char *)&fsctl_buf,
			sizeof(struct file_zero_data_information),
			CIFSMaxBufSize, NULL, NULL);
	free_xid(xid);
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

		if ((cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE) == 0)
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

	if ((keep_size == true) || (i_size_read(inode) >= off + len)) {
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
			FSCTL_QUERY_ALLOCATED_RANGES, true,
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
			FSCTL_QUERY_ALLOCATED_RANGES, true,
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
	if (oplock == SMB2_OPLOCK_LEVEL_NOCHANGE)
		return;
	if (oplock == SMB2_OPLOCK_LEVEL_BATCH) {
		cinode->oplock = CIFS_CACHE_RHW_FLG;
		cifs_dbg(FYI, "Batch Oplock granted on inode %p\n",
			 &cinode->vfs_inode);
	} else if (oplock == SMB2_OPLOCK_LEVEL_EXCLUSIVE) {
		cinode->oplock = CIFS_CACHE_RW_FLG;
		cifs_dbg(FYI, "Exclusive Oplock granted on inode %p\n",
			 &cinode->vfs_inode);
	} else if (oplock == SMB2_OPLOCK_LEVEL_II) {
		cinode->oplock = CIFS_CACHE_READ_FLG;
		cifs_dbg(FYI, "Level II Oplock granted on inode %p\n",
			 &cinode->vfs_inode);
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
		 &cinode->vfs_inode);
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

static bool
smb2_is_read_op(__u32 oplock)
{
	return oplock == SMB2_OPLOCK_LEVEL_II;
}

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
		return SMB2_LEASE_WRITE_CACHING | SMB2_LEASE_READ_CACHING;
	else if (oplock == SMB2_OPLOCK_LEVEL_II)
		return SMB2_LEASE_READ_CACHING;
	else if (oplock == SMB2_OPLOCK_LEVEL_BATCH)
		return SMB2_LEASE_HANDLE_CACHING | SMB2_LEASE_READ_CACHING |
		       SMB2_LEASE_WRITE_CACHING;
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
	if (lc->lcontext.LeaseFlags & SMB2_LEASE_FLAG_BREAK_IN_PROGRESS)
		return SMB2_OPLOCK_LEVEL_NOCHANGE;
	return le32_to_cpu(lc->lcontext.LeaseState);
}

static __u8
smb3_parse_lease_buf(void *buf, unsigned int *epoch, char *lease_key)
{
	struct create_lease_v2 *lc = (struct create_lease_v2 *)buf;

	*epoch = le16_to_cpu(lc->lcontext.Epoch);
	if (lc->lcontext.LeaseFlags & SMB2_LEASE_FLAG_BREAK_IN_PROGRESS)
		return SMB2_OPLOCK_LEVEL_NOCHANGE;
	if (lease_key)
		memcpy(lease_key, &lc->lcontext.LeaseKey, SMB2_LEASE_KEY_SIZE);
	return le32_to_cpu(lc->lcontext.LeaseState);
}

static unsigned int
smb2_wp_retry_size(struct inode *inode)
{
	return min_t(unsigned int, CIFS_SB(inode->i_sb)->wsize,
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
	struct smb2_sync_hdr *shdr =
			(struct smb2_sync_hdr *)old_rq->rq_iov[0].iov_base;

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

/* We can not use the normal sg_set_buf() as we will sometimes pass a
 * stack object as buf.
 */
static inline void smb2_sg_set_buf(struct scatterlist *sg, const void *buf,
				   unsigned int buflen)
{
	void *addr;
	/*
	 * VMAP_STACK (at least) puts stack into the vmalloc address space
	 */
	if (is_vmalloc_addr(buf))
		addr = vmalloc_to_page(buf);
	else
		addr = virt_to_page(buf);
	sg_set_page(sg, addr, buflen, offset_in_page(buf));
}

/* Assumes the first rqst has a transform header as the first iov.
 * I.e.
 * rqst[0].rq_iov[0]  is transform header
 * rqst[0].rq_iov[1+] data to be encrypted/decrypted
 * rqst[1+].rq_iov[0+] data to be encrypted/decrypted
 */
static struct scatterlist *
init_sg(int num_rqst, struct smb_rqst *rqst, u8 *sign)
{
	unsigned int sg_len;
	struct scatterlist *sg;
	unsigned int i;
	unsigned int j;
	unsigned int idx = 0;
	int skip;

	sg_len = 1;
	for (i = 0; i < num_rqst; i++)
		sg_len += rqst[i].rq_nvec + rqst[i].rq_npages;

	sg = kmalloc_array(sg_len, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg)
		return NULL;

	sg_init_table(sg, sg_len);
	for (i = 0; i < num_rqst; i++) {
		for (j = 0; j < rqst[i].rq_nvec; j++) {
			/*
			 * The first rqst has a transform header where the
			 * first 20 bytes are not part of the encrypted blob
			 */
			skip = (i == 0) && (j == 0) ? 20 : 0;
			smb2_sg_set_buf(&sg[idx++],
					rqst[i].rq_iov[j].iov_base + skip,
					rqst[i].rq_iov[j].iov_len - skip);
			}

		for (j = 0; j < rqst[i].rq_npages; j++) {
			unsigned int len, offset;

			rqst_page_get_length(&rqst[i], j, &len, &offset);
			sg_set_page(&sg[idx++], rqst[i].rq_pages[j], len, offset);
		}
	}
	smb2_sg_set_buf(&sg[idx], sign, SMB2_SIGNATURE_SIZE);
	return sg;
}

static int
smb2_get_enc_key(struct TCP_Server_Info *server, __u64 ses_id, int enc, u8 *key)
{
	struct cifs_ses *ses;
	u8 *ses_enc_key;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
			if (ses->Suid == ses_id) {
				ses_enc_key = enc ? ses->smb3encryptionkey :
					ses->smb3decryptionkey;
				memcpy(key, ses_enc_key, SMB3_SIGN_KEY_SIZE);
				spin_unlock(&cifs_tcp_ses_lock);
				return 0;
			}
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	return 1;
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
	u8 key[SMB3_SIGN_KEY_SIZE];
	struct aead_request *req;
	char *iv;
	unsigned int iv_len;
	DECLARE_CRYPTO_WAIT(wait);
	struct crypto_aead *tfm;
	unsigned int crypt_len = le32_to_cpu(tr_hdr->OriginalMessageSize);

	rc = smb2_get_enc_key(server, tr_hdr->SessionId, enc, key);
	if (rc) {
		cifs_server_dbg(VFS, "%s: Could not get %scryption key\n", __func__,
			 enc ? "en" : "de");
		return rc;
	}

	rc = smb3_crypto_aead_allocate(server);
	if (rc) {
		cifs_server_dbg(VFS, "%s: crypto alloc failed\n", __func__);
		return rc;
	}

	tfm = enc ? server->secmech.ccmaesencrypt :
						server->secmech.ccmaesdecrypt;

	if (server->cipher_type == SMB2_ENCRYPTION_AES256_GCM)
		rc = crypto_aead_setkey(tfm, key, SMB3_GCM256_CRYPTKEY_SIZE);
	else
		rc = crypto_aead_setkey(tfm, key, SMB3_SIGN_KEY_SIZE);

	if (rc) {
		cifs_server_dbg(VFS, "%s: Failed to set aead key %d\n", __func__, rc);
		return rc;
	}

	rc = crypto_aead_setauthsize(tfm, SMB2_SIGNATURE_SIZE);
	if (rc) {
		cifs_server_dbg(VFS, "%s: Failed to set authsize %d\n", __func__, rc);
		return rc;
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		cifs_server_dbg(VFS, "%s: Failed to alloc aead request\n", __func__);
		return -ENOMEM;
	}

	if (!enc) {
		memcpy(sign, &tr_hdr->Signature, SMB2_SIGNATURE_SIZE);
		crypt_len += SMB2_SIGNATURE_SIZE;
	}

	sg = init_sg(num_rqst, rqst, sign);
	if (!sg) {
		cifs_server_dbg(VFS, "%s: Failed to init sg\n", __func__);
		rc = -ENOMEM;
		goto free_req;
	}

	iv_len = crypto_aead_ivsize(tfm);
	iv = kzalloc(iv_len, GFP_KERNEL);
	if (!iv) {
		cifs_server_dbg(VFS, "%s: Failed to alloc iv\n", __func__);
		rc = -ENOMEM;
		goto free_sg;
	}

	if ((server->cipher_type == SMB2_ENCRYPTION_AES128_GCM) ||
	    (server->cipher_type == SMB2_ENCRYPTION_AES256_GCM))
		memcpy(iv, (char *)tr_hdr->Nonce, SMB3_AES_GCM_NONCE);
	else {
		iv[0] = 3;
		memcpy(iv + 1, (char *)tr_hdr->Nonce, SMB3_AES_CCM_NONCE);
	}

	aead_request_set_crypt(req, sg, sg, crypt_len, iv);
	aead_request_set_ad(req, assoc_data_len);

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  crypto_req_done, &wait);

	rc = crypto_wait_req(enc ? crypto_aead_encrypt(req)
				: crypto_aead_decrypt(req), &wait);

	if (!rc && enc)
		memcpy(&tr_hdr->Signature, sign, SMB2_SIGNATURE_SIZE);

	kfree(iv);
free_sg:
	kfree(sg);
free_req:
	kfree(req);
	return rc;
}

void
smb3_free_compound_rqst(int num_rqst, struct smb_rqst *rqst)
{
	int i, j;

	for (i = 0; i < num_rqst; i++) {
		if (rqst[i].rq_pages) {
			for (j = rqst[i].rq_npages - 1; j >= 0; j--)
				put_page(rqst[i].rq_pages[j]);
			kfree(rqst[i].rq_pages);
		}
	}
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
	struct page **pages;
	struct smb2_transform_hdr *tr_hdr = new_rq[0].rq_iov[0].iov_base;
	unsigned int npages;
	unsigned int orig_len = 0;
	int i, j;
	int rc = -ENOMEM;

	for (i = 1; i < num_rqst; i++) {
		npages = old_rq[i - 1].rq_npages;
		pages = kmalloc_array(npages, sizeof(struct page *),
				      GFP_KERNEL);
		if (!pages)
			goto err_free;

		new_rq[i].rq_pages = pages;
		new_rq[i].rq_npages = npages;
		new_rq[i].rq_offset = old_rq[i - 1].rq_offset;
		new_rq[i].rq_pagesz = old_rq[i - 1].rq_pagesz;
		new_rq[i].rq_tailsz = old_rq[i - 1].rq_tailsz;
		new_rq[i].rq_iov = old_rq[i - 1].rq_iov;
		new_rq[i].rq_nvec = old_rq[i - 1].rq_nvec;

		orig_len += smb_rqst_len(server, &old_rq[i - 1]);

		for (j = 0; j < npages; j++) {
			pages[j] = alloc_page(GFP_KERNEL|__GFP_HIGHMEM);
			if (!pages[j])
				goto err_free;
		}

		/* copy pages form the old */
		for (j = 0; j < npages; j++) {
			char *dst, *src;
			unsigned int offset, len;

			rqst_page_get_length(&new_rq[i], j, &len, &offset);

			dst = (char *) kmap(new_rq[i].rq_pages[j]) + offset;
			src = (char *) kmap(old_rq[i - 1].rq_pages[j]) + offset;

			memcpy(dst, src, len);
			kunmap(new_rq[i].rq_pages[j]);
			kunmap(old_rq[i - 1].rq_pages[j]);
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
		 unsigned int buf_data_size, struct page **pages,
		 unsigned int npages, unsigned int page_data_size,
		 bool is_offloaded)
{
	struct kvec iov[2];
	struct smb_rqst rqst = {NULL};
	int rc;

	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(struct smb2_transform_hdr);
	iov[1].iov_base = buf + sizeof(struct smb2_transform_hdr);
	iov[1].iov_len = buf_data_size;

	rqst.rq_iov = iov;
	rqst.rq_nvec = 2;
	rqst.rq_pages = pages;
	rqst.rq_npages = npages;
	rqst.rq_pagesz = PAGE_SIZE;
	rqst.rq_tailsz = (page_data_size % PAGE_SIZE) ? : PAGE_SIZE;

	rc = crypt_message(server, 1, &rqst, 0);
	cifs_dbg(FYI, "Decrypt message returned %d\n", rc);

	if (rc)
		return rc;

	memmove(buf, iov[1].iov_base, buf_data_size);

	if (!is_offloaded)
		server->total_read = buf_data_size + page_data_size;

	return rc;
}

static int
read_data_into_pages(struct TCP_Server_Info *server, struct page **pages,
		     unsigned int npages, unsigned int len)
{
	int i;
	int length;

	for (i = 0; i < npages; i++) {
		struct page *page = pages[i];
		size_t n;

		n = len;
		if (len >= PAGE_SIZE) {
			/* enough data to fill the page */
			n = PAGE_SIZE;
			len -= n;
		} else {
			zero_user(page, len, PAGE_SIZE - len);
			len = 0;
		}
		length = cifs_read_page_from_socket(server, page, 0, n);
		if (length < 0)
			return length;
		server->total_read += length;
	}

	return 0;
}

static int
init_read_bvec(struct page **pages, unsigned int npages, unsigned int data_size,
	       unsigned int cur_off, struct bio_vec **page_vec)
{
	struct bio_vec *bvec;
	int i;

	bvec = kcalloc(npages, sizeof(struct bio_vec), GFP_KERNEL);
	if (!bvec)
		return -ENOMEM;

	for (i = 0; i < npages; i++) {
		bvec[i].bv_page = pages[i];
		bvec[i].bv_offset = (i == 0) ? cur_off : 0;
		bvec[i].bv_len = min_t(unsigned int, PAGE_SIZE, data_size);
		data_size -= bvec[i].bv_len;
	}

	if (data_size != 0) {
		cifs_dbg(VFS, "%s: something went wrong\n", __func__);
		kfree(bvec);
		return -EIO;
	}

	*page_vec = bvec;
	return 0;
}

static int
handle_read_data(struct TCP_Server_Info *server, struct mid_q_entry *mid,
		 char *buf, unsigned int buf_len, struct page **pages,
		 unsigned int npages, unsigned int page_data_size)
{
	unsigned int data_offset;
	unsigned int data_len;
	unsigned int cur_off;
	unsigned int cur_page_idx;
	unsigned int pad_len;
	struct cifs_readdata *rdata = mid->callback_data;
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;
	struct bio_vec *bvec = NULL;
	struct iov_iter iter;
	struct kvec iov;
	int length;
	bool use_rdma_mr = false;

	if (shdr->Command != SMB2_READ) {
		cifs_server_dbg(VFS, "only big read responses are supported\n");
		return -ENOTSUPP;
	}

	if (server->ops->is_session_expired &&
	    server->ops->is_session_expired(buf)) {
		cifs_reconnect(server);
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
			dequeue_mid(mid, rdata->result);
			return 0;
		}

		if (data_len > page_data_size - pad_len) {
			/* data_len is corrupt -- discard frame */
			rdata->result = -EIO;
			dequeue_mid(mid, rdata->result);
			return 0;
		}

		rdata->result = init_read_bvec(pages, npages, page_data_size,
					       cur_off, &bvec);
		if (rdata->result != 0) {
			dequeue_mid(mid, rdata->result);
			return 0;
		}

		iov_iter_bvec(&iter, WRITE, bvec, npages, data_len);
	} else if (buf_len >= data_offset + data_len) {
		/* read response payload is in buf */
		WARN_ONCE(npages > 0, "read data can be either in buf or in pages");
		iov.iov_base = buf + data_offset;
		iov.iov_len = data_len;
		iov_iter_kvec(&iter, WRITE, &iov, 1, data_len);
	} else {
		/* read response payload cannot be in both buf and pages */
		WARN_ONCE(1, "buf can not contain only a part of read data");
		rdata->result = -EIO;
		dequeue_mid(mid, rdata->result);
		return 0;
	}

	length = rdata->copy_into_pages(server, rdata, &iter);

	kfree(bvec);

	if (length < 0)
		return length;

	dequeue_mid(mid, false);
	return length;
}

struct smb2_decrypt_work {
	struct work_struct decrypt;
	struct TCP_Server_Info *server;
	struct page **ppages;
	char *buf;
	unsigned int npages;
	unsigned int len;
};


static void smb2_decrypt_offload(struct work_struct *work)
{
	struct smb2_decrypt_work *dw = container_of(work,
				struct smb2_decrypt_work, decrypt);
	int i, rc;
	struct mid_q_entry *mid;

	rc = decrypt_raw_data(dw->server, dw->buf, dw->server->vals->read_rsp_size,
			      dw->ppages, dw->npages, dw->len, true);
	if (rc) {
		cifs_dbg(VFS, "error decrypting rc=%d\n", rc);
		goto free_pages;
	}

	dw->server->lstrp = jiffies;
	mid = smb2_find_mid(dw->server, dw->buf);
	if (mid == NULL)
		cifs_dbg(FYI, "mid not found\n");
	else {
		mid->decrypted = true;
		rc = handle_read_data(dw->server, mid, dw->buf,
				      dw->server->vals->read_rsp_size,
				      dw->ppages, dw->npages, dw->len);
		mid->callback(mid);
		cifs_mid_q_entry_release(mid);
	}

free_pages:
	for (i = dw->npages-1; i >= 0; i--)
		put_page(dw->ppages[i]);

	kfree(dw->ppages);
	cifs_small_buf_release(dw->buf);
	kfree(dw);
}


static int
receive_encrypted_read(struct TCP_Server_Info *server, struct mid_q_entry **mid,
		       int *num_mids)
{
	char *buf = server->smallbuf;
	struct smb2_transform_hdr *tr_hdr = (struct smb2_transform_hdr *)buf;
	unsigned int npages;
	struct page **pages;
	unsigned int len;
	unsigned int buflen = server->pdu_size;
	int rc;
	int i = 0;
	struct smb2_decrypt_work *dw;

	*num_mids = 1;
	len = min_t(unsigned int, buflen, server->vals->read_rsp_size +
		sizeof(struct smb2_transform_hdr)) - HEADER_SIZE(server) + 1;

	rc = cifs_read_from_socket(server, buf + HEADER_SIZE(server) - 1, len);
	if (rc < 0)
		return rc;
	server->total_read += rc;

	len = le32_to_cpu(tr_hdr->OriginalMessageSize) -
		server->vals->read_rsp_size;
	npages = DIV_ROUND_UP(len, PAGE_SIZE);

	pages = kmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		rc = -ENOMEM;
		goto discard_data;
	}

	for (; i < npages; i++) {
		pages[i] = alloc_page(GFP_KERNEL|__GFP_HIGHMEM);
		if (!pages[i]) {
			rc = -ENOMEM;
			goto discard_data;
		}
	}

	/* read read data into pages */
	rc = read_data_into_pages(server, pages, npages, len);
	if (rc)
		goto free_pages;

	rc = cifs_discard_remaining_data(server);
	if (rc)
		goto free_pages;

	/*
	 * For large reads, offload to different thread for better performance,
	 * use more cores decrypting which can be expensive
	 */

	if ((server->min_offload) && (server->in_flight > 1) &&
	    (server->pdu_size >= server->min_offload)) {
		dw = kmalloc(sizeof(struct smb2_decrypt_work), GFP_KERNEL);
		if (dw == NULL)
			goto non_offloaded_decrypt;

		dw->buf = server->smallbuf;
		server->smallbuf = (char *)cifs_small_buf_get();

		INIT_WORK(&dw->decrypt, smb2_decrypt_offload);

		dw->npages = npages;
		dw->server = server;
		dw->ppages = pages;
		dw->len = len;
		queue_work(decrypt_wq, &dw->decrypt);
		*num_mids = 0; /* worker thread takes care of finding mid */
		return -1;
	}

non_offloaded_decrypt:
	rc = decrypt_raw_data(server, buf, server->vals->read_rsp_size,
			      pages, npages, len, false);
	if (rc)
		goto free_pages;

	*mid = smb2_find_mid(server, buf);
	if (*mid == NULL)
		cifs_dbg(FYI, "mid not found\n");
	else {
		cifs_dbg(FYI, "mid found\n");
		(*mid)->decrypted = true;
		rc = handle_read_data(server, *mid, buf,
				      server->vals->read_rsp_size,
				      pages, npages, len);
	}

free_pages:
	for (i = i - 1; i >= 0; i--)
		put_page(pages[i]);
	kfree(pages);
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
	struct smb2_sync_hdr *shdr;
	unsigned int pdu_length = server->pdu_size;
	unsigned int buf_size;
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
	length = decrypt_raw_data(server, buf, buf_size, NULL, 0, 0, false);
	if (length)
		return length;

	next_is_large = server->large_buf;
one_more:
	shdr = (struct smb2_sync_hdr *)buf;
	if (shdr->NextCommand) {
		if (next_is_large)
			next_buffer = (char *)cifs_buf_get();
		else
			next_buffer = (char *)cifs_small_buf_get();
		memcpy(next_buffer,
		       buf + le32_to_cpu(shdr->NextCommand),
		       pdu_length - le32_to_cpu(shdr->NextCommand));
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

	if (ret == 0 && shdr->NextCommand) {
		pdu_length -= le32_to_cpu(shdr->NextCommand);
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
						sizeof(struct smb2_sync_hdr)) {
		cifs_server_dbg(VFS, "Transform message is too small (%u)\n",
			 pdu_length);
		cifs_reconnect(server);
		return -ECONNABORTED;
	}

	if (pdu_length < orig_len + sizeof(struct smb2_transform_hdr)) {
		cifs_server_dbg(VFS, "Transform message is broken\n");
		cifs_reconnect(server);
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
				NULL, 0, 0);
}

static int
smb2_next_header(char *buf)
{
	struct smb2_sync_hdr *hdr = (struct smb2_sync_hdr *)buf;
	struct smb2_transform_hdr *t_hdr = (struct smb2_transform_hdr *)buf;

	if (hdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM)
		return sizeof(struct smb2_transform_hdr) +
		  le32_to_cpu(t_hdr->OriginalMessageSize);

	return le32_to_cpu(hdr->NextCommand);
}

static int
smb2_make_node(unsigned int xid, struct inode *inode,
	       struct dentry *dentry, struct cifs_tcon *tcon,
	       char *full_path, umode_t mode, dev_t dev)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	int rc = -EPERM;
	FILE_ALL_INFO *buf = NULL;
	struct cifs_io_parms io_parms = {0};
	__u32 oplock = 0;
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	unsigned int bytes_written;
	struct win_dev *pdev;
	struct kvec iov[2];

	/*
	 * Check if mounted with mount parm 'sfu' mount parm.
	 * SFU emulation should work with all servers, but only
	 * supports block and char device (no socket & fifo),
	 * and was used by default in earlier versions of Windows
	 */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL))
		goto out;

	/*
	 * TODO: Add ability to create instead via reparse point. Windows (e.g.
	 * their current NFS server) uses this approach to expose special files
	 * over SMB2/SMB3 and Samba will do this with SMB3.1.1 POSIX Extensions
	 */

	if (!S_ISCHR(mode) && !S_ISBLK(mode))
		goto out;

	cifs_dbg(FYI, "sfu compat create special file\n");

	buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	oparms.desired_access = GENERIC_WRITE;
	oparms.create_options = cifs_create_options(cifs_sb, CREATE_NOT_DIR |
						    CREATE_OPTION_SPECIAL);
	oparms.disposition = FILE_CREATE;
	oparms.path = full_path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	if (tcon->ses->server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;
	rc = tcon->ses->server->ops->open(xid, &oparms, &oplock, buf);
	if (rc)
		goto out;

	/*
	 * BB Do not bother to decode buf since no local inode yet to put
	 * timestamps in, but we can reuse it safely.
	 */

	pdev = (struct win_dev *)buf;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.offset = 0;
	io_parms.length = sizeof(struct win_dev);
	iov[1].iov_base = buf;
	iov[1].iov_len = sizeof(struct win_dev);
	if (S_ISCHR(mode)) {
		memcpy(pdev->type, "IntxCHR", 8);
		pdev->major = cpu_to_le64(MAJOR(dev));
		pdev->minor = cpu_to_le64(MINOR(dev));
		rc = tcon->ses->server->ops->sync_write(xid, &fid, &io_parms,
							&bytes_written, iov, 1);
	} else if (S_ISBLK(mode)) {
		memcpy(pdev->type, "IntxBLK", 8);
		pdev->major = cpu_to_le64(MAJOR(dev));
		pdev->minor = cpu_to_le64(MINOR(dev));
		rc = tcon->ses->server->ops->sync_write(xid, &fid, &io_parms,
							&bytes_written, iov, 1);
	}
	tcon->ses->server->ops->close(xid, tcon, &fid);
	d_drop(dentry);

	/* FIXME: add code here to set EAs */
out:
	kfree(buf);
	return rc;
}


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
	.query_symlink = smb2_query_symlink,
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
};

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
	.query_symlink = smb2_query_symlink,
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
	.query_reparse_tag = smb2_query_reparse_tag,
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
	.query_symlink = smb2_query_symlink,
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
	.query_reparse_tag = smb2_query_reparse_tag,
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
	.query_symlink = smb2_query_symlink,
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
};

struct smb_version_values smb20_values = {
	.version_string = SMB20_VERSION_STRING,
	.protocol_id = SMB20_PROT_ID,
	.req_capabilities = 0, /* MBZ */
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease),
};

struct smb_version_values smb21_values = {
	.version_string = SMB21_VERSION_STRING,
	.protocol_id = SMB21_PROT_ID,
	.req_capabilities = 0, /* MBZ on negotiate req until SMB3 dialect */
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
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
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
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
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
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
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
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
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
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
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease_v2),
};
