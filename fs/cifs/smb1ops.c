/*
 *  SMB1 (CIFS) version specific operations
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
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifspdu.h"

/*
 * An NT cancel request header looks just like the original request except:
 *
 * The Command is SMB_COM_NT_CANCEL
 * The WordCount is zeroed out
 * The ByteCount is zeroed out
 *
 * This function mangles an existing request buffer into a
 * SMB_COM_NT_CANCEL request and then sends it.
 */
static int
send_nt_cancel(struct TCP_Server_Info *server, void *buf,
	       struct mid_q_entry *mid)
{
	int rc = 0;
	struct smb_hdr *in_buf = (struct smb_hdr *)buf;

	/* -4 for RFC1001 length and +2 for BCC field */
	in_buf->smb_buf_length = cpu_to_be32(sizeof(struct smb_hdr) - 4  + 2);
	in_buf->Command = SMB_COM_NT_CANCEL;
	in_buf->WordCount = 0;
	put_bcc(0, in_buf);

	mutex_lock(&server->srv_mutex);
	rc = cifs_sign_smb(in_buf, server, &mid->sequence_number);
	if (rc) {
		mutex_unlock(&server->srv_mutex);
		return rc;
	}
	rc = smb_send(server, in_buf, be32_to_cpu(in_buf->smb_buf_length));
	mutex_unlock(&server->srv_mutex);

	cFYI(1, "issued NT_CANCEL for mid %u, rc = %d",
		in_buf->Mid, rc);

	return rc;
}

static bool
cifs_compare_fids(struct cifsFileInfo *ob1, struct cifsFileInfo *ob2)
{
	return ob1->netfid == ob2->netfid;
}

static unsigned int
cifs_read_data_offset(char *buf)
{
	READ_RSP *rsp = (READ_RSP *)buf;
	return le16_to_cpu(rsp->DataOffset);
}

static unsigned int
cifs_read_data_length(char *buf)
{
	READ_RSP *rsp = (READ_RSP *)buf;
	return (le16_to_cpu(rsp->DataLengthHigh) << 16) +
	       le16_to_cpu(rsp->DataLength);
}

static struct mid_q_entry *
cifs_find_mid(struct TCP_Server_Info *server, char *buffer)
{
	struct smb_hdr *buf = (struct smb_hdr *)buffer;
	struct mid_q_entry *mid;

	spin_lock(&GlobalMid_Lock);
	list_for_each_entry(mid, &server->pending_mid_q, qhead) {
		if (mid->mid == buf->Mid &&
		    mid->mid_state == MID_REQUEST_SUBMITTED &&
		    le16_to_cpu(mid->command) == buf->Command) {
			spin_unlock(&GlobalMid_Lock);
			return mid;
		}
	}
	spin_unlock(&GlobalMid_Lock);
	return NULL;
}

static void
cifs_add_credits(struct TCP_Server_Info *server, const unsigned int add)
{
	spin_lock(&server->req_lock);
	server->credits += add;
	server->in_flight--;
	spin_unlock(&server->req_lock);
	wake_up(&server->request_q);
}

static void
cifs_set_credits(struct TCP_Server_Info *server, const int val)
{
	spin_lock(&server->req_lock);
	server->credits = val;
	server->oplocks = val > 1 ? enable_oplocks : false;
	spin_unlock(&server->req_lock);
}

static int *
cifs_get_credits_field(struct TCP_Server_Info *server)
{
	return &server->credits;
}

struct smb_version_operations smb1_operations = {
	.send_cancel = send_nt_cancel,
	.compare_fids = cifs_compare_fids,
	.setup_request = cifs_setup_request,
	.check_receive = cifs_check_receive,
	.add_credits = cifs_add_credits,
	.set_credits = cifs_set_credits,
	.get_credits_field = cifs_get_credits_field,
	.read_data_offset = cifs_read_data_offset,
	.read_data_length = cifs_read_data_length,
	.map_error = map_smb_to_linux_error,
	.find_mid = cifs_find_mid,
	.check_message = checkSMB,
	.dump_detail = cifs_dump_detail,
	.is_oplock_break = is_valid_oplock_break,
};

struct smb_version_values smb1_values = {
	.version_string = SMB1_VERSION_STRING,
	.large_lock_type = LOCKING_ANDX_LARGE_FILES,
	.exclusive_lock_type = 0,
	.shared_lock_type = LOCKING_ANDX_SHARED_LOCK,
	.unlock_lock_type = 0,
	.header_size = sizeof(struct smb_hdr),
	.max_header_size = MAX_CIFS_HDR_SIZE,
	.read_rsp_size = sizeof(READ_RSP),
};
