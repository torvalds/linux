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

#include "cifsglob.h"
#include "smb2pdu.h"
#include "smb2proto.h"
#include "cifsproto.h"
#include "cifs_debug.h"

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
		cERROR(1, "disabling echoes and oplocks");
		break;
	case 2:
		server->echoes = true;
		server->oplocks = false;
		server->echo_credits = 1;
		cFYI(1, "disabling oplocks");
		break;
	default:
		server->echoes = true;
		server->oplocks = true;
		server->echo_credits = 1;
		server->oplock_credits = 1;
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
	*val += add;
	server->in_flight--;
	if (server->in_flight == 0 && (optype & CIFS_OP_MASK) != CIFS_NEG_OP)
		rc = change_conf(server);
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
	spin_unlock(&server->req_lock);
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
	return le16_to_cpu(((struct smb2_hdr *)mid->resp_buf)->CreditRequest);
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
	struct smb2_hdr *hdr = (struct smb2_hdr *)buf;

	spin_lock(&GlobalMid_Lock);
	list_for_each_entry(mid, &server->pending_mid_q, qhead) {
		if ((mid->mid == hdr->MessageId) &&
		    (mid->mid_state == MID_REQUEST_SUBMITTED) &&
		    (mid->command == hdr->Command)) {
			spin_unlock(&GlobalMid_Lock);
			return mid;
		}
	}
	spin_unlock(&GlobalMid_Lock);
	return NULL;
}

static void
smb2_dump_detail(void *buf)
{
#ifdef CONFIG_CIFS_DEBUG2
	struct smb2_hdr *smb = (struct smb2_hdr *)buf;

	cERROR(1, "Cmd: %d Err: 0x%x Flags: 0x%x Mid: %llu Pid: %d",
		  smb->Command, smb->Status, smb->Flags, smb->MessageId,
		  smb->ProcessId);
	cERROR(1, "smb buf %p len %u", smb, smb2_calc_size(smb));
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

static int
smb2_is_path_accessible(const unsigned int xid, struct cifs_tcon *tcon,
			struct cifs_sb_info *cifs_sb, const char *full_path)
{
	int rc;
	__u64 persistent_fid, volatile_fid;
	__le16 *utf16_path;

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	rc = SMB2_open(xid, tcon, utf16_path, &persistent_fid, &volatile_fid,
		       FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0);
	if (rc) {
		kfree(utf16_path);
		return rc;
	}

	rc = SMB2_close(xid, tcon, persistent_fid, volatile_fid);
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

static char *
smb2_build_path_to_root(struct smb_vol *vol, struct cifs_sb_info *cifs_sb,
			struct cifs_tcon *tcon)
{
	int pplen = vol->prepath ? strlen(vol->prepath) : 0;
	char *full_path = NULL;

	/* if no prefix path, simply set path to the root of share to "" */
	if (pplen == 0) {
		full_path = kzalloc(2, GFP_KERNEL);
		return full_path;
	}

	cERROR(1, "prefixpath is not supported for SMB2 now");
	return NULL;
}

struct smb_version_operations smb21_operations = {
	.setup_request = smb2_setup_request,
	.check_receive = smb2_check_receive,
	.add_credits = smb2_add_credits,
	.set_credits = smb2_set_credits,
	.get_credits_field = smb2_get_credits_field,
	.get_credits = smb2_get_credits,
	.get_next_mid = smb2_get_next_mid,
	.find_mid = smb2_find_mid,
	.check_message = smb2_check_message,
	.dump_detail = smb2_dump_detail,
	.need_neg = smb2_need_neg,
	.negotiate = smb2_negotiate,
	.sess_setup = SMB2_sess_setup,
	.logoff = SMB2_logoff,
	.tree_connect = SMB2_tcon,
	.tree_disconnect = SMB2_tdis,
	.is_path_accessible = smb2_is_path_accessible,
	.query_path_info = smb2_query_path_info,
	.get_srv_inum = smb2_get_srv_inum,
	.build_path_to_root = smb2_build_path_to_root,
};

struct smb_version_values smb21_values = {
	.version_string = SMB21_VERSION_STRING,
	.header_size = sizeof(struct smb2_hdr),
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.lock_cmd = SMB2_LOCK,
};
