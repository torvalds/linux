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

struct smb_version_operations smb1_operations = {
	.send_cancel = send_nt_cancel,
	.compare_fids = cifs_compare_fids,
	.setup_request = cifs_setup_request,
	.check_receive = cifs_check_receive,
};

struct smb_version_values smb1_values = {
	.version_string = SMB1_VERSION_STRING,
	.large_lock_type = LOCKING_ANDX_LARGE_FILES,
	.exclusive_lock_type = 0,
	.shared_lock_type = LOCKING_ANDX_SHARED_LOCK,
	.unlock_lock_type = 0,
	.header_size = sizeof(struct smb_hdr),
	.max_header_size = MAX_CIFS_HDR_SIZE,
};
