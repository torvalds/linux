/*
 *   fs/cifs/smb2misc.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/ctype.h>
#include "smb2pdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "smb2proto.h"
#include "cifs_debug.h"
#include "cifs_unicode.h"
#include "smb2status.h"

static int
check_smb2_hdr(struct smb2_hdr *hdr, __u64 mid)
{
	/*
	 * Make sure that this really is an SMB, that it is a response,
	 * and that the message ids match.
	 */
	if ((*(__le32 *)hdr->ProtocolId == SMB2_PROTO_NUMBER) &&
	    (mid == hdr->MessageId)) {
		if (hdr->Flags & SMB2_FLAGS_SERVER_TO_REDIR)
			return 0;
		else {
			/* only one valid case where server sends us request */
			if (hdr->Command == SMB2_OPLOCK_BREAK)
				return 0;
			else
				cifs_dbg(VFS, "Received Request not response\n");
		}
	} else { /* bad signature or mid */
		if (*(__le32 *)hdr->ProtocolId != SMB2_PROTO_NUMBER)
			cifs_dbg(VFS, "Bad protocol string signature header %x\n",
				 *(unsigned int *) hdr->ProtocolId);
		if (mid != hdr->MessageId)
			cifs_dbg(VFS, "Mids do not match: %llu and %llu\n",
				 mid, hdr->MessageId);
	}
	cifs_dbg(VFS, "Bad SMB detected. The Mid=%llu\n", hdr->MessageId);
	return 1;
}

/*
 *  The following table defines the expected "StructureSize" of SMB2 responses
 *  in order by SMB2 command.  This is similar to "wct" in SMB/CIFS responses.
 *
 *  Note that commands are defined in smb2pdu.h in le16 but the array below is
 *  indexed by command in host byte order
 */
static const __le16 smb2_rsp_struct_sizes[NUMBER_OF_SMB2_COMMANDS] = {
	/* SMB2_NEGOTIATE */ __constant_cpu_to_le16(65),
	/* SMB2_SESSION_SETUP */ __constant_cpu_to_le16(9),
	/* SMB2_LOGOFF */ __constant_cpu_to_le16(4),
	/* SMB2_TREE_CONNECT */ __constant_cpu_to_le16(16),
	/* SMB2_TREE_DISCONNECT */ __constant_cpu_to_le16(4),
	/* SMB2_CREATE */ __constant_cpu_to_le16(89),
	/* SMB2_CLOSE */ __constant_cpu_to_le16(60),
	/* SMB2_FLUSH */ __constant_cpu_to_le16(4),
	/* SMB2_READ */ __constant_cpu_to_le16(17),
	/* SMB2_WRITE */ __constant_cpu_to_le16(17),
	/* SMB2_LOCK */ __constant_cpu_to_le16(4),
	/* SMB2_IOCTL */ __constant_cpu_to_le16(49),
	/* BB CHECK this ... not listed in documentation */
	/* SMB2_CANCEL */ __constant_cpu_to_le16(0),
	/* SMB2_ECHO */ __constant_cpu_to_le16(4),
	/* SMB2_QUERY_DIRECTORY */ __constant_cpu_to_le16(9),
	/* SMB2_CHANGE_NOTIFY */ __constant_cpu_to_le16(9),
	/* SMB2_QUERY_INFO */ __constant_cpu_to_le16(9),
	/* SMB2_SET_INFO */ __constant_cpu_to_le16(2),
	/* BB FIXME can also be 44 for lease break */
	/* SMB2_OPLOCK_BREAK */ __constant_cpu_to_le16(24)
};

int
smb2_check_message(char *buf, unsigned int length)
{
	struct smb2_hdr *hdr = (struct smb2_hdr *)buf;
	struct smb2_pdu *pdu = (struct smb2_pdu *)hdr;
	__u64 mid = hdr->MessageId;
	__u32 len = get_rfc1002_length(buf);
	__u32 clc_len;  /* calculated length */
	int command;

	/* BB disable following printk later */
	cifs_dbg(FYI, "%s length: 0x%x, smb_buf_length: 0x%x\n",
		 __func__, length, len);

	/*
	 * Add function to do table lookup of StructureSize by command
	 * ie Validate the wct via smb2_struct_sizes table above
	 */

	if (length < sizeof(struct smb2_pdu)) {
		if ((length >= sizeof(struct smb2_hdr)) && (hdr->Status != 0)) {
			pdu->StructureSize2 = 0;
			/*
			 * As with SMB/CIFS, on some error cases servers may
			 * not return wct properly
			 */
			return 0;
		} else {
			cifs_dbg(VFS, "Length less than SMB header size\n");
		}
		return 1;
	}
	if (len > CIFSMaxBufSize + MAX_SMB2_HDR_SIZE - 4) {
		cifs_dbg(VFS, "SMB length greater than maximum, mid=%llu\n",
			 mid);
		return 1;
	}

	if (check_smb2_hdr(hdr, mid))
		return 1;

	if (hdr->StructureSize != SMB2_HEADER_STRUCTURE_SIZE) {
		cifs_dbg(VFS, "Illegal structure size %u\n",
			 le16_to_cpu(hdr->StructureSize));
		return 1;
	}

	command = le16_to_cpu(hdr->Command);
	if (command >= NUMBER_OF_SMB2_COMMANDS) {
		cifs_dbg(VFS, "Illegal SMB2 command %d\n", command);
		return 1;
	}

	if (smb2_rsp_struct_sizes[command] != pdu->StructureSize2) {
		if (command != SMB2_OPLOCK_BREAK_HE && (hdr->Status == 0 ||
		    pdu->StructureSize2 != SMB2_ERROR_STRUCTURE_SIZE2)) {
			/* error packets have 9 byte structure size */
			cifs_dbg(VFS, "Illegal response size %u for command %d\n",
				 le16_to_cpu(pdu->StructureSize2), command);
			return 1;
		} else if (command == SMB2_OPLOCK_BREAK_HE && (hdr->Status == 0)
			   && (le16_to_cpu(pdu->StructureSize2) != 44)
			   && (le16_to_cpu(pdu->StructureSize2) != 36)) {
			/* special case for SMB2.1 lease break message */
			cifs_dbg(VFS, "Illegal response size %d for oplock break\n",
				 le16_to_cpu(pdu->StructureSize2));
			return 1;
		}
	}

	if (4 + len != length) {
		cifs_dbg(VFS, "Total length %u RFC1002 length %u mismatch mid %llu\n",
			 length, 4 + len, mid);
		return 1;
	}

	clc_len = smb2_calc_size(hdr);

	if (4 + len != clc_len) {
		cifs_dbg(FYI, "Calculated size %u length %u mismatch mid %llu\n",
			 clc_len, 4 + len, mid);
		/* create failed on symlink */
		if (command == SMB2_CREATE_HE &&
		    hdr->Status == STATUS_STOPPED_ON_SYMLINK)
			return 0;
		/* Windows 7 server returns 24 bytes more */
		if (clc_len + 20 == len && command == SMB2_OPLOCK_BREAK_HE)
			return 0;
		/* server can return one byte more */
		if (clc_len == 4 + len + 1)
			return 0;
		return 1;
	}
	return 0;
}

/*
 * The size of the variable area depends on the offset and length fields
 * located in different fields for various SMB2 responses. SMB2 responses
 * with no variable length info, show an offset of zero for the offset field.
 */
static const bool has_smb2_data_area[NUMBER_OF_SMB2_COMMANDS] = {
	/* SMB2_NEGOTIATE */ true,
	/* SMB2_SESSION_SETUP */ true,
	/* SMB2_LOGOFF */ false,
	/* SMB2_TREE_CONNECT */	false,
	/* SMB2_TREE_DISCONNECT */ false,
	/* SMB2_CREATE */ true,
	/* SMB2_CLOSE */ false,
	/* SMB2_FLUSH */ false,
	/* SMB2_READ */	true,
	/* SMB2_WRITE */ false,
	/* SMB2_LOCK */	false,
	/* SMB2_IOCTL */ true,
	/* SMB2_CANCEL */ false, /* BB CHECK this not listed in documentation */
	/* SMB2_ECHO */ false,
	/* SMB2_QUERY_DIRECTORY */ true,
	/* SMB2_CHANGE_NOTIFY */ true,
	/* SMB2_QUERY_INFO */ true,
	/* SMB2_SET_INFO */ false,
	/* SMB2_OPLOCK_BREAK */ false
};

/*
 * Returns the pointer to the beginning of the data area. Length of the data
 * area and the offset to it (from the beginning of the smb are also returned.
 */
char *
smb2_get_data_area_len(int *off, int *len, struct smb2_hdr *hdr)
{
	*off = 0;
	*len = 0;

	/* error responses do not have data area */
	if (hdr->Status && hdr->Status != STATUS_MORE_PROCESSING_REQUIRED &&
	    (((struct smb2_err_rsp *)hdr)->StructureSize) ==
						SMB2_ERROR_STRUCTURE_SIZE2)
		return NULL;

	/*
	 * Following commands have data areas so we have to get the location
	 * of the data buffer offset and data buffer length for the particular
	 * command.
	 */
	switch (hdr->Command) {
	case SMB2_NEGOTIATE:
		*off = le16_to_cpu(
		    ((struct smb2_negotiate_rsp *)hdr)->SecurityBufferOffset);
		*len = le16_to_cpu(
		    ((struct smb2_negotiate_rsp *)hdr)->SecurityBufferLength);
		break;
	case SMB2_SESSION_SETUP:
		*off = le16_to_cpu(
		    ((struct smb2_sess_setup_rsp *)hdr)->SecurityBufferOffset);
		*len = le16_to_cpu(
		    ((struct smb2_sess_setup_rsp *)hdr)->SecurityBufferLength);
		break;
	case SMB2_CREATE:
		*off = le32_to_cpu(
		    ((struct smb2_create_rsp *)hdr)->CreateContextsOffset);
		*len = le32_to_cpu(
		    ((struct smb2_create_rsp *)hdr)->CreateContextsLength);
		break;
	case SMB2_QUERY_INFO:
		*off = le16_to_cpu(
		    ((struct smb2_query_info_rsp *)hdr)->OutputBufferOffset);
		*len = le32_to_cpu(
		    ((struct smb2_query_info_rsp *)hdr)->OutputBufferLength);
		break;
	case SMB2_READ:
		*off = ((struct smb2_read_rsp *)hdr)->DataOffset;
		*len = le32_to_cpu(((struct smb2_read_rsp *)hdr)->DataLength);
		break;
	case SMB2_QUERY_DIRECTORY:
		*off = le16_to_cpu(
		  ((struct smb2_query_directory_rsp *)hdr)->OutputBufferOffset);
		*len = le32_to_cpu(
		  ((struct smb2_query_directory_rsp *)hdr)->OutputBufferLength);
		break;
	case SMB2_IOCTL:
		*off = le32_to_cpu(
		  ((struct smb2_ioctl_rsp *)hdr)->OutputOffset);
		*len = le32_to_cpu(((struct smb2_ioctl_rsp *)hdr)->OutputCount);
		break;
	case SMB2_CHANGE_NOTIFY:
	default:
		/* BB FIXME for unimplemented cases above */
		cifs_dbg(VFS, "no length check for command\n");
		break;
	}

	/*
	 * Invalid length or offset probably means data area is invalid, but
	 * we have little choice but to ignore the data area in this case.
	 */
	if (*off > 4096) {
		cifs_dbg(VFS, "offset %d too large, data area ignored\n", *off);
		*len = 0;
		*off = 0;
	} else if (*off < 0) {
		cifs_dbg(VFS, "negative offset %d to data invalid ignore data area\n",
			 *off);
		*off = 0;
		*len = 0;
	} else if (*len < 0) {
		cifs_dbg(VFS, "negative data length %d invalid, data area ignored\n",
			 *len);
		*len = 0;
	} else if (*len > 128 * 1024) {
		cifs_dbg(VFS, "data area larger than 128K: %d\n", *len);
		*len = 0;
	}

	/* return pointer to beginning of data area, ie offset from SMB start */
	if ((*off != 0) && (*len != 0))
		return hdr->ProtocolId + *off;
	else
		return NULL;
}

/*
 * Calculate the size of the SMB message based on the fixed header
 * portion, the number of word parameters and the data portion of the message.
 */
unsigned int
smb2_calc_size(void *buf)
{
	struct smb2_hdr *hdr = (struct smb2_hdr *)buf;
	struct smb2_pdu *pdu = (struct smb2_pdu *)hdr;
	int offset; /* the offset from the beginning of SMB to data area */
	int data_length; /* the length of the variable length data area */
	/* Structure Size has already been checked to make sure it is 64 */
	int len = 4 + le16_to_cpu(pdu->hdr.StructureSize);

	/*
	 * StructureSize2, ie length of fixed parameter area has already
	 * been checked to make sure it is the correct length.
	 */
	len += le16_to_cpu(pdu->StructureSize2);

	if (has_smb2_data_area[le16_to_cpu(hdr->Command)] == false)
		goto calc_size_exit;

	smb2_get_data_area_len(&offset, &data_length, hdr);
	cifs_dbg(FYI, "SMB2 data length %d offset %d\n", data_length, offset);

	if (data_length > 0) {
		/*
		 * Check to make sure that data area begins after fixed area,
		 * Note that last byte of the fixed area is part of data area
		 * for some commands, typically those with odd StructureSize,
		 * so we must add one to the calculation (and 4 to account for
		 * the size of the RFC1001 hdr.
		 */
		if (offset + 4 + 1 < len) {
			cifs_dbg(VFS, "data area offset %d overlaps SMB2 header %d\n",
				 offset + 4 + 1, len);
			data_length = 0;
		} else {
			len = 4 + offset + data_length;
		}
	}
calc_size_exit:
	cifs_dbg(FYI, "SMB2 len %d\n", len);
	return len;
}

/* Note: caller must free return buffer */
__le16 *
cifs_convert_path_to_utf16(const char *from, struct cifs_sb_info *cifs_sb)
{
	int len;
	const char *start_of_path;
	__le16 *to;

	/* Windows doesn't allow paths beginning with \ */
	if (from[0] == '\\')
		start_of_path = from + 1;
	else
		start_of_path = from;
	to = cifs_strndup_to_utf16(start_of_path, PATH_MAX, &len,
				   cifs_sb->local_nls,
				   cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
	return to;
}

__le32
smb2_get_lease_state(struct cifsInodeInfo *cinode)
{
	__le32 lease = 0;

	if (CIFS_CACHE_WRITE(cinode))
		lease |= SMB2_LEASE_WRITE_CACHING;
	if (CIFS_CACHE_HANDLE(cinode))
		lease |= SMB2_LEASE_HANDLE_CACHING;
	if (CIFS_CACHE_READ(cinode))
		lease |= SMB2_LEASE_READ_CACHING;
	return lease;
}

struct smb2_lease_break_work {
	struct work_struct lease_break;
	struct tcon_link *tlink;
	__u8 lease_key[16];
	__le32 lease_state;
};

static void
cifs_ses_oplock_break(struct work_struct *work)
{
	struct smb2_lease_break_work *lw = container_of(work,
				struct smb2_lease_break_work, lease_break);
	int rc;

	rc = SMB2_lease_break(0, tlink_tcon(lw->tlink), lw->lease_key,
			      lw->lease_state);
	cifs_dbg(FYI, "Lease release rc %d\n", rc);
	cifs_put_tlink(lw->tlink);
	kfree(lw);
}

static bool
smb2_tcon_has_lease(struct cifs_tcon *tcon, struct smb2_lease_break *rsp,
		    struct smb2_lease_break_work *lw)
{
	bool found;
	__u8 lease_state;
	struct list_head *tmp;
	struct cifsFileInfo *cfile;
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifs_pending_open *open;
	struct cifsInodeInfo *cinode;
	int ack_req = le32_to_cpu(rsp->Flags &
				  SMB2_NOTIFY_BREAK_LEASE_FLAG_ACK_REQUIRED);

	lease_state = le32_to_cpu(rsp->NewLeaseState);

	list_for_each(tmp, &tcon->openFileList) {
		cfile = list_entry(tmp, struct cifsFileInfo, tlist);
		cinode = CIFS_I(cfile->dentry->d_inode);

		if (memcmp(cinode->lease_key, rsp->LeaseKey,
							SMB2_LEASE_KEY_SIZE))
			continue;

		cifs_dbg(FYI, "found in the open list\n");
		cifs_dbg(FYI, "lease key match, lease break 0x%d\n",
			 le32_to_cpu(rsp->NewLeaseState));

		server->ops->set_oplock_level(cinode, lease_state, 0, NULL);

		if (ack_req)
			cfile->oplock_break_cancelled = false;
		else
			cfile->oplock_break_cancelled = true;

		queue_work(cifsiod_wq, &cfile->oplock_break);
		kfree(lw);
		return true;
	}

	found = false;
	list_for_each_entry(open, &tcon->pending_opens, olist) {
		if (memcmp(open->lease_key, rsp->LeaseKey,
			   SMB2_LEASE_KEY_SIZE))
			continue;

		if (!found && ack_req) {
			found = true;
			memcpy(lw->lease_key, open->lease_key,
			       SMB2_LEASE_KEY_SIZE);
			lw->tlink = cifs_get_tlink(open->tlink);
			queue_work(cifsiod_wq, &lw->lease_break);
		}

		cifs_dbg(FYI, "found in the pending open list\n");
		cifs_dbg(FYI, "lease key match, lease break 0x%d\n",
			 le32_to_cpu(rsp->NewLeaseState));

		open->oplock = lease_state;
	}
	return found;
}

static bool
smb2_is_valid_lease_break(char *buffer)
{
	struct smb2_lease_break *rsp = (struct smb2_lease_break *)buffer;
	struct list_head *tmp, *tmp1, *tmp2;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct smb2_lease_break_work *lw;

	lw = kmalloc(sizeof(struct smb2_lease_break_work), GFP_KERNEL);
	if (!lw)
		return false;

	INIT_WORK(&lw->lease_break, cifs_ses_oplock_break);
	lw->lease_state = rsp->NewLeaseState;

	cifs_dbg(FYI, "Checking for lease break\n");

	/* look up tcon based on tid & uid */
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &cifs_tcp_ses_list) {
		server = list_entry(tmp, struct TCP_Server_Info, tcp_ses_list);

		list_for_each(tmp1, &server->smb_ses_list) {
			ses = list_entry(tmp1, struct cifs_ses, smb_ses_list);

			spin_lock(&cifs_file_list_lock);
			list_for_each(tmp2, &ses->tcon_list) {
				tcon = list_entry(tmp2, struct cifs_tcon,
						  tcon_list);
				cifs_stats_inc(
				    &tcon->stats.cifs_stats.num_oplock_brks);
				if (smb2_tcon_has_lease(tcon, rsp, lw)) {
					spin_unlock(&cifs_file_list_lock);
					spin_unlock(&cifs_tcp_ses_lock);
					return true;
				}
			}
			spin_unlock(&cifs_file_list_lock);
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
	kfree(lw);
	cifs_dbg(FYI, "Can not process lease break - no lease matched\n");
	return false;
}

bool
smb2_is_valid_oplock_break(char *buffer, struct TCP_Server_Info *server)
{
	struct smb2_oplock_break *rsp = (struct smb2_oplock_break *)buffer;
	struct list_head *tmp, *tmp1, *tmp2;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct cifsInodeInfo *cinode;
	struct cifsFileInfo *cfile;

	cifs_dbg(FYI, "Checking for oplock break\n");

	if (rsp->hdr.Command != SMB2_OPLOCK_BREAK)
		return false;

	if (rsp->StructureSize !=
				smb2_rsp_struct_sizes[SMB2_OPLOCK_BREAK_HE]) {
		if (le16_to_cpu(rsp->StructureSize) == 44)
			return smb2_is_valid_lease_break(buffer);
		else
			return false;
	}

	cifs_dbg(FYI, "oplock level 0x%d\n", rsp->OplockLevel);

	/* look up tcon based on tid & uid */
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifs_ses, smb_ses_list);
		list_for_each(tmp1, &ses->tcon_list) {
			tcon = list_entry(tmp1, struct cifs_tcon, tcon_list);

			cifs_stats_inc(&tcon->stats.cifs_stats.num_oplock_brks);
			spin_lock(&cifs_file_list_lock);
			list_for_each(tmp2, &tcon->openFileList) {
				cfile = list_entry(tmp2, struct cifsFileInfo,
						     tlist);
				if (rsp->PersistentFid !=
				    cfile->fid.persistent_fid ||
				    rsp->VolatileFid !=
				    cfile->fid.volatile_fid)
					continue;

				cifs_dbg(FYI, "file id match, oplock break\n");
				cinode = CIFS_I(cfile->dentry->d_inode);

				if (!CIFS_CACHE_WRITE(cinode) &&
				    rsp->OplockLevel == SMB2_OPLOCK_LEVEL_NONE)
					cfile->oplock_break_cancelled = true;
				else
					cfile->oplock_break_cancelled = false;

				server->ops->set_oplock_level(cinode,
				  rsp->OplockLevel ? SMB2_OPLOCK_LEVEL_II : 0,
				  0, NULL);

				queue_work(cifsiod_wq, &cfile->oplock_break);

				spin_unlock(&cifs_file_list_lock);
				spin_unlock(&cifs_tcp_ses_lock);
				return true;
			}
			spin_unlock(&cifs_file_list_lock);
			spin_unlock(&cifs_tcp_ses_lock);
			cifs_dbg(FYI, "No matching file for oplock break\n");
			return true;
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
	cifs_dbg(FYI, "Can not process oplock break for non-existent connection\n");
	return false;
}
