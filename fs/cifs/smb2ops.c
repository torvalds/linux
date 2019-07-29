/*
 *  SMB2 version specific operations
 *
 *  Copyright (c) 2012, Jeff Layton <jlayton@redhat.com>
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License v2 as published
 *  by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *  the GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/pagemap.h>
#include <linux/vfs.h>
#include <linux/falloc.h>
#include <linux/scatterlist.h>
#include <linux/uuid.h>
#include <crypto/aead.h>
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

static int
change_conf(struct TCP_Server_Info *server)
{
	server->credits += server->echo_credits + server->oplock_credits;
	server->oplock_credits = server->echo_credits = 0;
	switch (server->credits) {
	case 0:
		return -1;
	case 1:
		server->echoes = false;
		server->oplocks = false;
		cifs_dbg(VFS, "disabling echoes and oplocks\n");
		break;
	case 2:
		server->echoes = true;
		server->oplocks = false;
		server->echo_credits = 1;
		cifs_dbg(FYI, "disabling oplocks\n");
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
	return 0;
}

static void
smb2_add_credits(struct TCP_Server_Info *server, const unsigned int add,
		 const int optype)
{
	int *val, rc = 0;
	spin_lock(&server->req_lock);
	val = server->ops->get_credits_field(server, optype);

	/* eg found case where write overlapping reconnect messed up credits */
	if (((optype & CIFS_OP_MASK) == CIFS_NEG_OP) && (*val != 0))
		trace_smb3_reconnect_with_invalid_credits(server->CurrentMid,
			server->hostname, *val);

	*val += add;
	if (*val > 65000) {
		*val = 65000; /* Don't get near 64K credits, avoid srv bugs */
		printk_once(KERN_WARNING "server overflowed SMB3 credits\n");
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
	if (rc)
		cifs_reconnect(server);
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
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)mid->resp_buf;

	return le16_to_cpu(shdr->CreditRequest);
}

static int
smb2_wait_mtu_credits(struct TCP_Server_Info *server, unsigned int size,
		      unsigned int *num, unsigned int *credits)
{
	int rc = 0;
	unsigned int scredits;

	spin_lock(&server->req_lock);
	while (1) {
		if (server->credits <= 0) {
			spin_unlock(&server->req_lock);
			cifs_num_waiters_inc(server);
			rc = wait_event_killable(server->request_q,
					has_credits(server, &server->credits));
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
			if (scredits == 1) {
				*num = SMB2_MAX_BUFFER_SIZE;
				*credits = 0;
				break;
			}

			/* leave one credit for a possible reopen */
			scredits--;
			*num = min_t(unsigned int, size,
				     scredits * SMB2_MAX_BUFFER_SIZE);

			*credits = DIV_ROUND_UP(*num, SMB2_MAX_BUFFER_SIZE);
			server->credits -= *credits;
			server->in_flight++;
			break;
		}
	}
	spin_unlock(&server->req_lock);
	return rc;
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

static struct mid_q_entry *
smb2_find_mid(struct TCP_Server_Info *server, char *buf)
{
	struct mid_q_entry *mid;
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;
	__u64 wire_mid = le64_to_cpu(shdr->MessageId);

	if (shdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM) {
		cifs_dbg(VFS, "encrypted frame parsing not supported yet");
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

	cifs_dbg(VFS, "Cmd: %d Err: 0x%x Flags: 0x%x Mid: %llu Pid: %d\n",
		 shdr->Command, shdr->Status, shdr->Flags, shdr->MessageId,
		 shdr->ProcessId);
	cifs_dbg(VFS, "smb buf %p len %u\n", buf,
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
	ses->server->CurrentMid = 0;
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
#ifdef CONFIG_CIFS_SMB_DIRECT
	if (server->rdma) {
		if (server->sign)
			wsize = min_t(unsigned int,
				wsize, server->smbd_conn->max_fragmented_send_size);
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
			wsize = min_t(unsigned int,
				wsize, server->smbd_conn->max_fragmented_send_size);
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
#ifdef CONFIG_CIFS_SMB_DIRECT
	if (server->rdma) {
		if (server->sign)
			rsize = min_t(unsigned int,
				rsize, server->smbd_conn->max_fragmented_recv_size);
		else
			rsize = min_t(unsigned int,
				rsize, server->smbd_conn->max_readwrite_size);
	}
#endif

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
			rsize = min_t(unsigned int,
				rsize, server->smbd_conn->max_fragmented_recv_size);
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
			(char **)&out_buf, &ret_data_len);
	if (rc == -EOPNOTSUPP) {
		cifs_dbg(FYI,
			 "server does not support query network interfaces\n");
		goto out;
	} else if (rc != 0) {
		cifs_dbg(VFS, "error %d on ioctl to get interface list\n", rc);
		goto out;
	}

	rc = parse_server_interfaces(out_buf, ret_data_len,
				     &iface_list, &iface_count);
	if (rc)
		goto out;

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
	}
}

void close_shroot(struct cached_fid *cfid)
{
	mutex_lock(&cfid->fid_mutex);
	kref_put(&cfid->refcount, smb2_close_cached_fid);
	mutex_unlock(&cfid->fid_mutex);
}

void
smb2_cached_lease_break(struct work_struct *work)
{
	struct cached_fid *cfid = container_of(work,
				struct cached_fid, lease_break);

	close_shroot(cfid);
}

/*
 * Open the directory at the root of a share
 */
int open_shroot(unsigned int xid, struct cifs_tcon *tcon, struct cifs_fid *pfid)
{
	struct cifs_open_parms oparams;
	int rc;
	__le16 srch_path = 0; /* Null - since an open of top of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_II;

	mutex_lock(&tcon->crfid.fid_mutex);
	if (tcon->crfid.is_valid) {
		cifs_dbg(FYI, "found a cached root file handle\n");
		memcpy(pfid, tcon->crfid.fid, sizeof(struct cifs_fid));
		kref_get(&tcon->crfid.refcount);
		mutex_unlock(&tcon->crfid.fid_mutex);
		return 0;
	}

	oparams.tcon = tcon;
	oparams.create_options = 0;
	oparams.desired_access = FILE_READ_ATTRIBUTES;
	oparams.disposition = FILE_OPEN;
	oparams.fid = pfid;
	oparams.reconnect = false;

	rc = SMB2_open(xid, &oparams, &srch_path, &oplock, NULL, NULL, NULL);
	if (rc == 0) {
		memcpy(tcon->crfid.fid, pfid, sizeof(struct cifs_fid));
		tcon->crfid.tcon = tcon;
		tcon->crfid.is_valid = true;
		kref_init(&tcon->crfid.refcount);
		kref_get(&tcon->crfid.refcount);
	}
	mutex_unlock(&tcon->crfid.fid_mutex);
	return rc;
}

static void
smb3_qfs_tcon(const unsigned int xid, struct cifs_tcon *tcon)
{
	int rc;
	__le16 srch_path = 0; /* Null - open root of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	bool no_cached_open = tcon->nohandlecache;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	if (no_cached_open)
		rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL,
			       NULL);
	else
		rc = open_shroot(xid, tcon, &fid);

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
		close_shroot(&tcon->crfid);

	return;
}

static void
smb2_qfs_tcon(const unsigned int xid, struct cifs_tcon *tcon)
{
	int rc;
	__le16 srch_path = 0; /* Null - open root of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL, NULL);
	if (rc)
		return;

	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_ATTRIBUTE_INFORMATION);
	SMB2_QFS_attr(xid, tcon, fid.persistent_fid, fid.volatile_fid,
			FS_DEVICE_INFORMATION);
	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	return;
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
	if (backup_cred(cifs_sb))
		oparms.create_options = CREATE_OPEN_BACKUP_INTENT;
	else
		oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL);
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

		if (name_len == 0) {
			break;
		}

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
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct smb2_file_full_ea_info *smb2_data;
	int ea_buf_size = SMB2_MIN_EA_BUF;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_EA;
	oparms.disposition = FILE_OPEN;
	if (backup_cred(cifs_sb))
		oparms.create_options = CREATE_OPEN_BACKUP_INTENT;
	else
		oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL);
	kfree(utf16_path);
	if (rc) {
		cifs_dbg(FYI, "open failed rc=%d\n", rc);
		return rc;
	}

	while (1) {
		smb2_data = kzalloc(ea_buf_size, GFP_KERNEL);
		if (smb2_data == NULL) {
			SMB2_close(xid, tcon, fid.persistent_fid,
				   fid.volatile_fid);
			return -ENOMEM;
		}

		rc = SMB2_query_eas(xid, tcon, fid.persistent_fid,
				    fid.volatile_fid,
				    ea_buf_size, smb2_data);

		if (rc != -E2BIG)
			break;

		kfree(smb2_data);
		ea_buf_size <<= 1;

		if (ea_buf_size > SMB2_MAX_EA_BUF) {
			cifs_dbg(VFS, "EA size is too large\n");
			SMB2_close(xid, tcon, fid.persistent_fid,
				   fid.volatile_fid);
			return -ENOMEM;
		}
	}

	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);

	/*
	 * If ea_name is NULL (listxattr) and there are no EAs, return 0 as it's
	 * not an error. Otherwise, the specified ea_name was not found.
	 */
	if (!rc)
		rc = move_smb2_ea_to_cifs(ea_data, buf_size, smb2_data,
					  SMB2_MAX_EA_BUF, ea_name);
	else if (!ea_name && rc == -ENODATA)
		rc = 0;

	kfree(smb2_data);
	return rc;
}


static int
smb2_set_ea(const unsigned int xid, struct cifs_tcon *tcon,
	    const char *path, const char *ea_name, const void *ea_value,
	    const __u16 ea_value_len, const struct nls_table *nls_codepage,
	    struct cifs_sb_info *cifs_sb)
{
	int rc;
	__le16 *utf16_path;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct smb2_file_full_ea_info *ea;
	int ea_name_len = strlen(ea_name);
	int len;

	if (ea_name_len > 255)
		return -EINVAL;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_WRITE_EA;
	oparms.disposition = FILE_OPEN;
	if (backup_cred(cifs_sb))
		oparms.create_options = CREATE_OPEN_BACKUP_INTENT;
	else
		oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL);
	kfree(utf16_path);
	if (rc) {
		cifs_dbg(FYI, "open failed rc=%d\n", rc);
		return rc;
	}

	len = sizeof(ea) + ea_name_len + ea_value_len + 1;
	ea = kzalloc(len, GFP_KERNEL);
	if (ea == NULL) {
		SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
		return -ENOMEM;
	}

	ea->ea_name_length = ea_name_len;
	ea->ea_value_length = cpu_to_le16(ea_value_len);
	memcpy(ea->ea_data, ea_name, ea_name_len + 1);
	memcpy(ea->ea_data + ea_name_len + 1, ea_value, ea_value_len);

	rc = SMB2_set_ea(xid, tcon, fid.persistent_fid, fid.volatile_fid, ea,
			 len);
	kfree(ea);

	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);

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
			NULL, 0 /* no input */,
			(char **)&res_key, &ret_data_len);

	if (rc) {
		cifs_dbg(VFS, "refcpy ioctl error %d getting resume key\n", rc);
		goto req_res_key_exit;
	}
	if (ret_data_len < sizeof(struct resume_key_req)) {
		cifs_dbg(VFS, "Invalid refcopy resume key length\n");
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
		      __le16 *path, int is_dir,
		      unsigned long p)
{
	struct cifs_ses *ses = tcon->ses;
	char __user *arg = (char __user *)p;
	struct smb_query_info qi;
	struct smb_query_info __user *pqi;
	int rc = 0;
	int flags = 0;
	struct smb2_query_info_rsp *rsp = NULL;
	void *buffer = NULL;
	struct smb_rqst rqst[3];
	int resp_buftype[3];
	struct kvec rsp_iov[3];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct cifs_open_parms oparms;
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_fid fid;
	struct kvec qi_iov[1];
	struct kvec close_iov[1];

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = resp_buftype[2] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	if (copy_from_user(&qi, arg, sizeof(struct smb_query_info)))
		return -EFAULT;

	if (qi.output_buffer_length > 1024)
		return -EINVAL;

	if (!ses || !(ses->server))
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	buffer = kmalloc(qi.output_buffer_length, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	if (copy_from_user(buffer, arg + sizeof(struct smb_query_info),
			   qi.output_buffer_length)) {
		rc = -EFAULT;
		goto iqinf_exit;
	}

	/* Open */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	memset(&oparms, 0, sizeof(oparms));
	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES | READ_CONTROL;
	oparms.disposition = FILE_OPEN;
	if (is_dir)
		oparms.create_options = CREATE_NOT_FILE;
	else
		oparms.create_options = CREATE_NOT_DIR;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, &rqst[0], &oplock, &oparms, path);
	if (rc)
		goto iqinf_exit;
	smb2_set_next_command(ses->server, &rqst[0], 0);

	/* Query */
	memset(&qi_iov, 0, sizeof(qi_iov));
	rqst[1].rq_iov = qi_iov;
	rqst[1].rq_nvec = 1;

	rc = SMB2_query_info_init(tcon, &rqst[1], COMPOUND_FID, COMPOUND_FID,
				  qi.file_info_class, qi.info_type,
				  qi.additional_information,
				  qi.input_buffer_length,
				  qi.output_buffer_length, buffer);
	if (rc)
		goto iqinf_exit;
	smb2_set_next_command(ses->server, &rqst[1], 0);
	smb2_set_related(&rqst[1]);

	/* Close */
	memset(&close_iov, 0, sizeof(close_iov));
	rqst[2].rq_iov = close_iov;
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, &rqst[2], COMPOUND_FID, COMPOUND_FID);
	if (rc)
		goto iqinf_exit;
	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, ses, flags, 3, rqst,
				resp_buftype, rsp_iov);
	if (rc)
		goto iqinf_exit;
	pqi = (struct smb_query_info __user *)arg;
	rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
	if (le32_to_cpu(rsp->OutputBufferLength) < qi.input_buffer_length)
		qi.input_buffer_length = le32_to_cpu(rsp->OutputBufferLength);
	if (copy_to_user(&pqi->input_buffer_length, &qi.input_buffer_length,
			 sizeof(qi.input_buffer_length))) {
		rc = -EFAULT;
		goto iqinf_exit;
	}
	if (copy_to_user(pqi + 1, rsp->Buffer, qi.input_buffer_length)) {
		rc = -EFAULT;
		goto iqinf_exit;
	}

 iqinf_exit:
	kfree(buffer);
	SMB2_open_free(&rqst[0]);
	SMB2_query_info_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
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

	cifs_dbg(FYI, "in smb2_copychunk_range - about to call request res key\n");
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
			sizeof(struct copychunk_ioctl),	(char **)&retbuf,
			&ret_data_len);
		if (rc == 0) {
			if (ret_data_len !=
					sizeof(struct copychunk_ioctl_rsp)) {
				cifs_dbg(VFS, "invalid cchunk response size\n");
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
				cifs_dbg(VFS, "invalid copy chunk response\n");
				rc = -EIO;
				goto cchunk_out;
			}
			if (le32_to_cpu(retbuf->ChunksWritten) != 1) {
				cifs_dbg(VFS, "invalid num chunks written\n");
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
			&setsparse, 1, NULL, NULL);
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
	cifs_dbg(FYI, "duplicate extents: src off %lld dst off %lld len %lld",
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
			NULL,
			&ret_data_len);

	if (ret_data_len > 0)
		cifs_dbg(FYI, "non-zero response length in duplicate extents");

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
			NULL,
			&ret_data_len);

}

/* GMT Token is @GMT-YYYY.MM.DD-HH.MM.SS Unicode which is 48 bytes + null */
#define GMT_TOKEN_SIZE 50

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
	struct smb_snapshot_array snapshot_in;

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid,
			FSCTL_SRV_ENUMERATE_SNAPSHOTS,
			true /* is_fsctl */,
			NULL, 0 /* no input data */,
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
smb2_query_dir_first(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *path, struct cifs_sb_info *cifs_sb,
		     struct cifs_fid *fid, __u16 search_flags,
		     struct cifs_search_info *srch_inf)
{
	__le16 *utf16_path;
	int rc;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES | FILE_READ_DATA;
	oparms.disposition = FILE_OPEN;
	if (backup_cred(cifs_sb))
		oparms.create_options = CREATE_OPEN_BACKUP_INTENT;
	else
		oparms.create_options = 0;
	oparms.fid = fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL);
	kfree(utf16_path);
	if (rc) {
		cifs_dbg(FYI, "open dir failed rc=%d\n", rc);
		return rc;
	}

	srch_inf->entries_in_buffer = 0;
	srch_inf->index_of_last_entry = 2;

	rc = SMB2_query_directory(xid, tcon, fid->persistent_fid,
				  fid->volatile_fid, 0, srch_inf);
	if (rc) {
		cifs_dbg(FYI, "query directory failed rc=%d\n", rc);
		SMB2_close(xid, tcon, fid->persistent_fid, fid->volatile_fid);
	}
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
smb2_is_status_pending(char *buf, struct TCP_Server_Info *server, int length)
{
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;

	if (shdr->Status != STATUS_PENDING)
		return false;

	if (!length) {
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
	shdr->Flags |= SMB2_FLAGS_RELATED_OPERATIONS;
}

char smb2_padding[7] = {0, 0, 0, 0, 0, 0, 0};

void
smb2_set_next_command(struct TCP_Server_Info *server, struct smb_rqst *rqst,
		      bool has_space_for_padding)
{
	struct smb2_sync_hdr *shdr;
	unsigned long len = smb_rqst_len(server, rqst);

	/* SMB headers in a compound are 8 byte aligned. */
	if (len & 7) {
		if (has_space_for_padding) {
			len = rqst->rq_iov[rqst->rq_nvec - 1].iov_len;
			rqst->rq_iov[rqst->rq_nvec - 1].iov_len =
				(len + 7) & ~7;
		} else {
			rqst->rq_iov[rqst->rq_nvec].iov_base = smb2_padding;
			rqst->rq_iov[rqst->rq_nvec].iov_len = 8 - (len & 7);
			rqst->rq_nvec++;
		}
		len = smb_rqst_len(server, rqst);
	}

	shdr = (struct smb2_sync_hdr *)(rqst->rq_iov[0].iov_base);
	shdr->NextCommand = cpu_to_le32(len);
}

static int
smb2_queryfs(const unsigned int xid, struct cifs_tcon *tcon,
	     struct kstatfs *buf)
{
	struct smb2_query_info_rsp *rsp;
	struct smb2_fs_full_size_info *info = NULL;
	struct smb_rqst rqst[3];
	int resp_buftype[3];
	struct kvec rsp_iov[3];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec qi_iov[1];
	struct kvec close_iov[1];
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = ses->server;
	__le16 srch_path = 0; /* Null - open root of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	int flags = 0;
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
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, &rqst[0], &oplock, &oparms, &srch_path);
	if (rc)
		goto qfs_exit;
	smb2_set_next_command(server, &rqst[0], 0);

	memset(&qi_iov, 0, sizeof(qi_iov));
	rqst[1].rq_iov = qi_iov;
	rqst[1].rq_nvec = 1;

	rc = SMB2_query_info_init(tcon, &rqst[1], COMPOUND_FID, COMPOUND_FID,
				  FS_FULL_SIZE_INFORMATION,
				  SMB2_O_INFO_FILESYSTEM, 0,
				  sizeof(struct smb2_fs_full_size_info), 0,
				  NULL);
	if (rc)
		goto qfs_exit;
	smb2_set_next_command(server, &rqst[1], 0);
	smb2_set_related(&rqst[1]);

	memset(&close_iov, 0, sizeof(close_iov));
	rqst[2].rq_iov = close_iov;
	rqst[2].rq_nvec = 1;

	rc = SMB2_close_init(tcon, &rqst[2], COMPOUND_FID, COMPOUND_FID);
	if (rc)
		goto qfs_exit;
	smb2_set_related(&rqst[2]);

	rc = compound_send_recv(xid, ses, flags, 3, rqst,
				resp_buftype, rsp_iov);
	if (rc)
		goto qfs_exit;

	rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
	buf->f_type = SMB2_MAGIC_NUMBER;
	info = (struct smb2_fs_full_size_info *)(
		le16_to_cpu(rsp->OutputBufferOffset) + (char *)rsp);
	rc = smb2_validate_iov(le16_to_cpu(rsp->OutputBufferOffset),
			       le32_to_cpu(rsp->OutputBufferLength),
			       &rsp_iov[1],
			       sizeof(struct smb2_fs_full_size_info));
	if (!rc)
		smb2_copy_fs_info_to_kstatfs(info, buf);

qfs_exit:
	SMB2_open_free(&rqst[0]);
	SMB2_query_info_free(&rqst[1]);
	SMB2_close_free(&rqst[2]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	free_rsp_buf(resp_buftype[2], rsp_iov[2].iov_base);
	return rc;
}

static int
smb311_queryfs(const unsigned int xid, struct cifs_tcon *tcon,
	     struct kstatfs *buf)
{
	int rc;
	__le16 srch_path = 0; /* Null - open root of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;

	if (!tcon->posix_extensions)
		return smb2_queryfs(xid, tcon, buf);

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, &srch_path, &oplock, NULL, NULL, NULL);
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

	cifs_dbg(FYI, "smb2_get_dfs_refer path <%s>\n", search_name);

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
				(char *)dfs_req, dfs_req_size,
				(char **)&dfs_rsp, &dfs_rsp_size);
	} while (rc == -EAGAIN);

	if (rc) {
		if ((rc != -ENOENT) && (rc != -EOPNOTSUPP))
			cifs_dbg(VFS, "ioctl error in smb2_get_dfs_refer rc=%d\n", rc);
		goto out;
	}

	rc = parse_dfs_referrals(dfs_rsp, dfs_rsp_size,
				 num_of_nodes, target_nodes,
				 nls_codepage, remap, search_name,
				 true /* is_unicode */);
	if (rc) {
		cifs_dbg(VFS, "parse error in smb2_get_dfs_refer rc=%d\n", rc);
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
#define SMB2_SYMLINK_STRUCT_SIZE \
	(sizeof(struct smb2_err_rsp) - 1 + sizeof(struct smb2_symlink_err_rsp))

static int
smb2_query_symlink(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *full_path, char **target_path,
		   struct cifs_sb_info *cifs_sb)
{
	int rc;
	__le16 *utf16_path;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;
	struct kvec err_iov = {NULL, 0};
	struct smb2_err_rsp *err_buf = NULL;
	int resp_buftype;
	struct smb2_symlink_err_rsp *symlink;
	unsigned int sub_len;
	unsigned int sub_offset;
	unsigned int print_len;
	unsigned int print_offset;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, full_path);

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms.tcon = tcon;
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	if (backup_cred(cifs_sb))
		oparms.create_options = CREATE_OPEN_BACKUP_INTENT;
	else
		oparms.create_options = 0;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, &err_iov,
		       &resp_buftype);
	if (!rc || !err_iov.iov_base) {
		rc = -ENOENT;
		goto free_path;
	}

	err_buf = err_iov.iov_base;
	if (le32_to_cpu(err_buf->ByteCount) < sizeof(struct smb2_symlink_err_rsp) ||
	    err_iov.iov_len < SMB2_SYMLINK_STRUCT_SIZE) {
		rc = -ENOENT;
		goto querty_exit;
	}

	/* open must fail on symlink - reset rc */
	rc = 0;
	symlink = (struct smb2_symlink_err_rsp *)err_buf->ErrorData;
	sub_len = le16_to_cpu(symlink->SubstituteNameLength);
	sub_offset = le16_to_cpu(symlink->SubstituteNameOffset);
	print_len = le16_to_cpu(symlink->PrintNameLength);
	print_offset = le16_to_cpu(symlink->PrintNameOffset);

	if (err_iov.iov_len < SMB2_SYMLINK_STRUCT_SIZE + sub_offset + sub_len) {
		rc = -ENOENT;
		goto querty_exit;
	}

	if (err_iov.iov_len <
	    SMB2_SYMLINK_STRUCT_SIZE + print_offset + print_len) {
		rc = -ENOENT;
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
	free_rsp_buf(resp_buftype, err_buf);
 free_path:
	kfree(utf16_path);
	return rc;
}

#ifdef CONFIG_CIFS_ACL
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

	if (backup_cred(cifs_sb))
		oparms.create_options = CREATE_OPEN_BACKUP_INTENT;
	else
		oparms.create_options = 0;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path) {
		rc = -ENOMEM;
		free_xid(xid);
		return ERR_PTR(rc);
	}

	oparms.tcon = tcon;
	oparms.desired_access = READ_CONTROL;
	oparms.disposition = FILE_OPEN;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL);
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

#ifdef CONFIG_CIFS_ACL
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

	if (backup_cred(cifs_sb))
		oparms.create_options = CREATE_OPEN_BACKUP_INTENT;
	else
		oparms.create_options = 0;

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
	oparms.disposition = FILE_OPEN;
	oparms.path = path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL, NULL);
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
#endif /* CIFS_ACL */

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
#endif

static long smb3_zero_range(struct file *file, struct cifs_tcon *tcon,
			    loff_t offset, loff_t len, bool keep_size)
{
	struct inode *inode;
	struct cifsInodeInfo *cifsi;
	struct cifsFileInfo *cfile = file->private_data;
	struct file_zero_data_information fsctl_buf;
	long rc;
	unsigned int xid;

	xid = get_xid();

	inode = d_inode(cfile->dentry);
	cifsi = CIFS_I(inode);

	/* if file not oplocked can't be sure whether asking to extend size */
	if (!CIFS_CACHE_READ(cifsi))
		if (keep_size == false) {
			rc = -EOPNOTSUPP;
			free_xid(xid);
			return rc;
		}

	/*
	 * Must check if file sparse since fallocate -z (zero range) assumes
	 * non-sparse allocation
	 */
	if (!(cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE)) {
		rc = -EOPNOTSUPP;
		free_xid(xid);
		return rc;
	}

	/*
	 * need to make sure we are not asked to extend the file since the SMB3
	 * fsctl does not change the file size. In the future we could change
	 * this to zero the first part of the range then set the file size
	 * which for a non sparse file would zero the newly extended range
	 */
	if (keep_size == false)
		if (i_size_read(inode) < offset + len) {
			rc = -EOPNOTSUPP;
			free_xid(xid);
			return rc;
		}

	cifs_dbg(FYI, "offset %lld len %lld", offset, len);

	fsctl_buf.FileOffset = cpu_to_le64(offset);
	fsctl_buf.BeyondFinalZero = cpu_to_le64(offset + len);

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid, FSCTL_SET_ZERO_DATA,
			true /* is_fctl */, (char *)&fsctl_buf,
			sizeof(struct file_zero_data_information), NULL, NULL);
	free_xid(xid);
	return rc;
}

static long smb3_punch_hole(struct file *file, struct cifs_tcon *tcon,
			    loff_t offset, loff_t len)
{
	struct inode *inode;
	struct cifsInodeInfo *cifsi;
	struct cifsFileInfo *cfile = file->private_data;
	struct file_zero_data_information fsctl_buf;
	long rc;
	unsigned int xid;
	__u8 set_sparse = 1;

	xid = get_xid();

	inode = d_inode(cfile->dentry);
	cifsi = CIFS_I(inode);

	/* Need to make file sparse, if not already, before freeing range. */
	/* Consider adding equivalent for compressed since it could also work */
	if (!smb2_set_sparse(xid, tcon, cfile, inode, set_sparse)) {
		rc = -EOPNOTSUPP;
		free_xid(xid);
		return rc;
	}

	cifs_dbg(FYI, "offset %lld len %lld", offset, len);

	fsctl_buf.FileOffset = cpu_to_le64(offset);
	fsctl_buf.BeyondFinalZero = cpu_to_le64(offset + len);

	rc = SMB2_ioctl(xid, tcon, cfile->fid.persistent_fid,
			cfile->fid.volatile_fid, FSCTL_SET_ZERO_DATA,
			true /* is_fctl */, (char *)&fsctl_buf,
			sizeof(struct file_zero_data_information), NULL, NULL);
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

	xid = get_xid();

	inode = d_inode(cfile->dentry);
	cifsi = CIFS_I(inode);

	/* if file not oplocked can't be sure whether asking to extend size */
	if (!CIFS_CACHE_READ(cifsi))
		if (keep_size == false) {
			free_xid(xid);
			return rc;
		}

	/*
	 * Files are non-sparse by default so falloc may be a no-op
	 * Must check if file sparse. If not sparse, and not extending
	 * then no need to do anything since file already allocated
	 */
	if ((cifsi->cifsAttrs & FILE_ATTRIBUTE_SPARSE_FILE) == 0) {
		if (keep_size == true)
			rc = 0;
		/* check if extending file */
		else if (i_size_read(inode) >= off + len)
			/* not extending file and already not sparse */
			rc = 0;
		/* BB: in future add else clause to extend file */
		else
			rc = -EOPNOTSUPP;
		free_xid(xid);
		return rc;
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
			free_xid(xid);
			return rc;
		}

		rc = smb2_set_sparse(xid, tcon, cfile, inode, false);
	}
	/* BB: else ... in future add code to extend file and set sparse */


	free_xid(xid);
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
			struct cifsInodeInfo *cinode, bool set_level2)
{
	if (set_level2)
		server->ops->set_oplock_level(cinode, SMB2_OPLOCK_LEVEL_II,
						0, NULL);
	else
		server->ops->set_oplock_level(cinode, 0, 0, NULL);
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

	oplock &= 0xFF;
	if (oplock == SMB2_OPLOCK_LEVEL_NOCHANGE)
		return;

	cinode->oplock = 0;
	if (oplock & SMB2_LEASE_READ_CACHING_HE) {
		cinode->oplock |= CIFS_CACHE_READ_FLG;
		strcat(message, "R");
	}
	if (oplock & SMB2_LEASE_HANDLE_CACHING_HE) {
		cinode->oplock |= CIFS_CACHE_HANDLE_FLG;
		strcat(message, "H");
	}
	if (oplock & SMB2_LEASE_WRITE_CACHING_HE) {
		cinode->oplock |= CIFS_CACHE_WRITE_FLG;
		strcat(message, "W");
	}
	if (!cinode->oplock)
		strcat(message, "None");
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
		   struct smb_rqst *old_rq)
{
	struct smb2_sync_hdr *shdr =
			(struct smb2_sync_hdr *)old_rq->rq_iov[0].iov_base;

	memset(tr_hdr, 0, sizeof(struct smb2_transform_hdr));
	tr_hdr->ProtocolId = SMB2_TRANSFORM_PROTO_NUM;
	tr_hdr->OriginalMessageSize = cpu_to_le32(orig_len);
	tr_hdr->Flags = cpu_to_le16(0x01);
	get_random_bytes(&tr_hdr->Nonce, SMB3_AES128CMM_NONCE);
	memcpy(&tr_hdr->SessionId, &shdr->SessionId, 8);
}

/* We can not use the normal sg_set_buf() as we will sometimes pass a
 * stack object as buf.
 */
static inline void smb2_sg_set_buf(struct scatterlist *sg, const void *buf,
				   unsigned int buflen)
{
	sg_set_page(sg, virt_to_page(buf), buflen, offset_in_page(buf));
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
	list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
		if (ses->Suid != ses_id)
			continue;
		ses_enc_key = enc ? ses->smb3encryptionkey :
							ses->smb3decryptionkey;
		memcpy(key, ses_enc_key, SMB3_SIGN_KEY_SIZE);
		spin_unlock(&cifs_tcp_ses_lock);
		return 0;
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
		cifs_dbg(VFS, "%s: Could not get %scryption key\n", __func__,
			 enc ? "en" : "de");
		return 0;
	}

	rc = smb3_crypto_aead_allocate(server);
	if (rc) {
		cifs_dbg(VFS, "%s: crypto alloc failed\n", __func__);
		return rc;
	}

	tfm = enc ? server->secmech.ccmaesencrypt :
						server->secmech.ccmaesdecrypt;
	rc = crypto_aead_setkey(tfm, key, SMB3_SIGN_KEY_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Failed to set aead key %d\n", __func__, rc);
		return rc;
	}

	rc = crypto_aead_setauthsize(tfm, SMB2_SIGNATURE_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Failed to set authsize %d\n", __func__, rc);
		return rc;
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		cifs_dbg(VFS, "%s: Failed to alloc aead request", __func__);
		return -ENOMEM;
	}

	if (!enc) {
		memcpy(sign, &tr_hdr->Signature, SMB2_SIGNATURE_SIZE);
		crypt_len += SMB2_SIGNATURE_SIZE;
	}

	sg = init_sg(num_rqst, rqst, sign);
	if (!sg) {
		cifs_dbg(VFS, "%s: Failed to init sg", __func__);
		rc = -ENOMEM;
		goto free_req;
	}

	iv_len = crypto_aead_ivsize(tfm);
	iv = kzalloc(iv_len, GFP_KERNEL);
	if (!iv) {
		cifs_dbg(VFS, "%s: Failed to alloc IV", __func__);
		rc = -ENOMEM;
		goto free_sg;
	}
	iv[0] = 3;
	memcpy(iv + 1, (char *)tr_hdr->Nonce, SMB3_AES128CMM_NONCE);

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
	fill_transform_hdr(tr_hdr, orig_len, old_rq);

	rc = crypt_message(server, num_rqst, new_rq, 1);
	cifs_dbg(FYI, "encrypt message returned %d", rc);
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
		 unsigned int npages, unsigned int page_data_size)
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
	cifs_dbg(FYI, "decrypt message returned %d\n", rc);

	if (rc)
		return rc;

	memmove(buf, iov[1].iov_base, buf_data_size);

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
		cifs_dbg(VFS, "only big read responses are supported\n");
		return -ENOTSUPP;
	}

	if (server->ops->is_session_expired &&
	    server->ops->is_session_expired(buf)) {
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return -1;
	}

	if (server->ops->is_status_pending &&
			server->ops->is_status_pending(buf, server, 0))
		return -1;

	rdata->result = server->ops->map_error(buf, false);
	if (rdata->result != 0) {
		cifs_dbg(FYI, "%s: server returned error %d\n",
			 __func__, rdata->result);
		dequeue_mid(mid, rdata->result);
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

	/* set up first iov for signature check */
	rdata->iov[0].iov_base = buf;
	rdata->iov[0].iov_len = 4;
	rdata->iov[1].iov_base = buf + 4;
	rdata->iov[1].iov_len = server->vals->read_rsp_size - 4;
	cifs_dbg(FYI, "0: iov_base=%p iov_len=%zu\n",
		 rdata->iov[0].iov_base, server->vals->read_rsp_size);

	length = rdata->copy_into_pages(server, rdata, &iter);

	kfree(bvec);

	if (length < 0)
		return length;

	dequeue_mid(mid, false);
	return length;
}

static int
receive_encrypted_read(struct TCP_Server_Info *server, struct mid_q_entry **mid)
{
	char *buf = server->smallbuf;
	struct smb2_transform_hdr *tr_hdr = (struct smb2_transform_hdr *)buf;
	unsigned int npages;
	struct page **pages;
	unsigned int len;
	unsigned int buflen = server->pdu_size;
	int rc;
	int i = 0;

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

	rc = decrypt_raw_data(server, buf, server->vals->read_rsp_size,
			      pages, npages, len);
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
	char *tmpbuf;
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
	length = decrypt_raw_data(server, buf, buf_size, NULL, 0, 0);
	if (length)
		return length;

	next_is_large = server->large_buf;
 one_more:
	shdr = (struct smb2_sync_hdr *)buf;
	if (shdr->NextCommand) {
		if (next_is_large) {
			tmpbuf = server->bigbuf;
			next_buffer = (char *)cifs_buf_get();
		} else {
			tmpbuf = server->smallbuf;
			next_buffer = (char *)cifs_small_buf_get();
		}
		memcpy(next_buffer,
		       tmpbuf + le32_to_cpu(shdr->NextCommand),
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
		cifs_dbg(VFS, "too many PDUs in compound\n");
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
			server->bigbuf = next_buffer;
		else
			server->smallbuf = next_buffer;

		buf += le32_to_cpu(shdr->NextCommand);
		goto one_more;
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
		cifs_dbg(VFS, "Transform message is too small (%u)\n",
			 pdu_length);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return -ECONNABORTED;
	}

	if (pdu_length < orig_len + sizeof(struct smb2_transform_hdr)) {
		cifs_dbg(VFS, "Transform message is broken\n");
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return -ECONNABORTED;
	}

	/* TODO: add support for compounds containing READ. */
	if (pdu_length > CIFSMaxBufSize + MAX_HEADER_SIZE(server))
		return receive_encrypted_read(server, &mids[0]);

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
#ifdef CONFIG_CIFS_ACL
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
#endif /* CIFS_ACL */
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
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
	.get_next_mid = smb2_get_next_mid,
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
	.get_dfs_refer = smb2_get_dfs_refer,
	.select_sectype = smb2_select_sectype,
#ifdef CONFIG_CIFS_XATTR
	.query_all_EAs = smb2_query_eas,
	.set_EA = smb2_set_ea,
#endif /* CIFS_XATTR */
#ifdef CONFIG_CIFS_ACL
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
#endif /* CIFS_ACL */
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
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
	.get_next_mid = smb2_get_next_mid,
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
	.downgrade_oplock = smb2_downgrade_oplock,
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
	.init_transform_rq = smb3_init_transform_rq,
	.is_transform_hdr = smb3_is_transform_hdr,
	.receive_transform = smb3_receive_transform,
	.get_dfs_refer = smb2_get_dfs_refer,
	.select_sectype = smb2_select_sectype,
#ifdef CONFIG_CIFS_XATTR
	.query_all_EAs = smb2_query_eas,
	.set_EA = smb2_set_ea,
#endif /* CIFS_XATTR */
#ifdef CONFIG_CIFS_ACL
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
#endif /* CIFS_ACL */
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
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
	.get_next_mid = smb2_get_next_mid,
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
	.downgrade_oplock = smb2_downgrade_oplock,
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
	.init_transform_rq = smb3_init_transform_rq,
	.is_transform_hdr = smb3_is_transform_hdr,
	.receive_transform = smb3_receive_transform,
	.get_dfs_refer = smb2_get_dfs_refer,
	.select_sectype = smb2_select_sectype,
#ifdef CONFIG_CIFS_XATTR
	.query_all_EAs = smb2_query_eas,
	.set_EA = smb2_set_ea,
#endif /* CIFS_XATTR */
#ifdef CONFIG_CIFS_ACL
	.get_acl = get_smb2_acl,
	.get_acl_by_fid = get_smb2_acl_by_fid,
	.set_acl = set_smb2_acl,
#endif /* CIFS_ACL */
	.next_header = smb2_next_header,
	.ioctl_query_info = smb2_ioctl_query_info,
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
