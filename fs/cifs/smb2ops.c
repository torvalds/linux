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
	if (server->in_flight == 0)
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

struct smb_version_operations smb21_operations = {
	.setup_request = smb2_setup_request,
	.check_receive = smb2_check_receive,
	.add_credits = smb2_add_credits,
	.set_credits = smb2_set_credits,
	.get_credits_field = smb2_get_credits_field,
	.get_credits = smb2_get_credits,
	.get_next_mid = smb2_get_next_mid,
};

struct smb_version_values smb21_values = {
	.version_string = SMB21_VERSION_STRING,
	.lock_cmd = SMB2_LOCK,
};
