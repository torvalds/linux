/*
 *   fs/cifs/misc.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
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

#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/mempool.h>
#include <linux/vmalloc.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "smberr.h"
#include "nterr.h"
#include "cifs_unicode.h"
#include "smb2pdu.h"

extern mempool_t *cifs_sm_req_poolp;
extern mempool_t *cifs_req_poolp;

/* The xid serves as a useful identifier for each incoming vfs request,
   in a similar way to the mid which is useful to track each sent smb,
   and CurrentXid can also provide a running counter (although it
   will eventually wrap past zero) of the total vfs operations handled
   since the cifs fs was mounted */

unsigned int
_get_xid(void)
{
	unsigned int xid;

	spin_lock(&GlobalMid_Lock);
	GlobalTotalActiveXid++;

	/* keep high water mark for number of simultaneous ops in filesystem */
	if (GlobalTotalActiveXid > GlobalMaxActiveXid)
		GlobalMaxActiveXid = GlobalTotalActiveXid;
	if (GlobalTotalActiveXid > 65000)
		cifs_dbg(FYI, "warning: more than 65000 requests active\n");
	xid = GlobalCurrentXid++;
	spin_unlock(&GlobalMid_Lock);
	return xid;
}

void
_free_xid(unsigned int xid)
{
	spin_lock(&GlobalMid_Lock);
	/* if (GlobalTotalActiveXid == 0)
		BUG(); */
	GlobalTotalActiveXid--;
	spin_unlock(&GlobalMid_Lock);
}

struct cifs_ses *
sesInfoAlloc(void)
{
	struct cifs_ses *ret_buf;

	ret_buf = kzalloc(sizeof(struct cifs_ses), GFP_KERNEL);
	if (ret_buf) {
		atomic_inc(&sesInfoAllocCount);
		ret_buf->status = CifsNew;
		++ret_buf->ses_count;
		INIT_LIST_HEAD(&ret_buf->smb_ses_list);
		INIT_LIST_HEAD(&ret_buf->tcon_list);
		mutex_init(&ret_buf->session_mutex);
	}
	return ret_buf;
}

void
sesInfoFree(struct cifs_ses *buf_to_free)
{
	if (buf_to_free == NULL) {
		cifs_dbg(FYI, "Null buffer passed to sesInfoFree\n");
		return;
	}

	atomic_dec(&sesInfoAllocCount);
	kfree(buf_to_free->serverOS);
	kfree(buf_to_free->serverDomain);
	kfree(buf_to_free->serverNOS);
	kzfree(buf_to_free->password);
	kfree(buf_to_free->user_name);
	kfree(buf_to_free->domainName);
	kzfree(buf_to_free->auth_key.response);
	kzfree(buf_to_free);
}

struct cifs_tcon *
tconInfoAlloc(void)
{
	struct cifs_tcon *ret_buf;
	ret_buf = kzalloc(sizeof(struct cifs_tcon), GFP_KERNEL);
	if (ret_buf) {
		atomic_inc(&tconInfoAllocCount);
		ret_buf->tidStatus = CifsNew;
		++ret_buf->tc_count;
		INIT_LIST_HEAD(&ret_buf->openFileList);
		INIT_LIST_HEAD(&ret_buf->tcon_list);
		spin_lock_init(&ret_buf->open_file_lock);
		mutex_init(&ret_buf->prfid_mutex);
		ret_buf->prfid = kzalloc(sizeof(struct cifs_fid), GFP_KERNEL);
#ifdef CONFIG_CIFS_STATS
		spin_lock_init(&ret_buf->stat_lock);
#endif
	}
	return ret_buf;
}

void
tconInfoFree(struct cifs_tcon *buf_to_free)
{
	if (buf_to_free == NULL) {
		cifs_dbg(FYI, "Null buffer passed to tconInfoFree\n");
		return;
	}
	atomic_dec(&tconInfoAllocCount);
	kfree(buf_to_free->nativeFileSystem);
	kzfree(buf_to_free->password);
	kfree(buf_to_free->prfid);
	kfree(buf_to_free);
}

struct smb_hdr *
cifs_buf_get(void)
{
	struct smb_hdr *ret_buf = NULL;
	/*
	 * SMB2 header is bigger than CIFS one - no problems to clean some
	 * more bytes for CIFS.
	 */
	size_t buf_size = sizeof(struct smb2_sync_hdr);

	/*
	 * We could use negotiated size instead of max_msgsize -
	 * but it may be more efficient to always alloc same size
	 * albeit slightly larger than necessary and maxbuffersize
	 * defaults to this and can not be bigger.
	 */
	ret_buf = mempool_alloc(cifs_req_poolp, GFP_NOFS);

	/* clear the first few header bytes */
	/* for most paths, more is cleared in header_assemble */
	memset(ret_buf, 0, buf_size + 3);
	atomic_inc(&bufAllocCount);
#ifdef CONFIG_CIFS_STATS2
	atomic_inc(&totBufAllocCount);
#endif /* CONFIG_CIFS_STATS2 */

	return ret_buf;
}

void
cifs_buf_release(void *buf_to_free)
{
	if (buf_to_free == NULL) {
		/* cifs_dbg(FYI, "Null buffer passed to cifs_buf_release\n");*/
		return;
	}
	mempool_free(buf_to_free, cifs_req_poolp);

	atomic_dec(&bufAllocCount);
	return;
}

struct smb_hdr *
cifs_small_buf_get(void)
{
	struct smb_hdr *ret_buf = NULL;

/* We could use negotiated size instead of max_msgsize -
   but it may be more efficient to always alloc same size
   albeit slightly larger than necessary and maxbuffersize
   defaults to this and can not be bigger */
	ret_buf = mempool_alloc(cifs_sm_req_poolp, GFP_NOFS);
	/* No need to clear memory here, cleared in header assemble */
	/*	memset(ret_buf, 0, sizeof(struct smb_hdr) + 27);*/
	atomic_inc(&smBufAllocCount);
#ifdef CONFIG_CIFS_STATS2
	atomic_inc(&totSmBufAllocCount);
#endif /* CONFIG_CIFS_STATS2 */

	return ret_buf;
}

void
cifs_small_buf_release(void *buf_to_free)
{

	if (buf_to_free == NULL) {
		cifs_dbg(FYI, "Null buffer passed to cifs_small_buf_release\n");
		return;
	}
	mempool_free(buf_to_free, cifs_sm_req_poolp);

	atomic_dec(&smBufAllocCount);
	return;
}

void
free_rsp_buf(int resp_buftype, void *rsp)
{
	if (resp_buftype == CIFS_SMALL_BUFFER)
		cifs_small_buf_release(rsp);
	else if (resp_buftype == CIFS_LARGE_BUFFER)
		cifs_buf_release(rsp);
}

/* NB: MID can not be set if treeCon not passed in, in that
   case it is responsbility of caller to set the mid */
void
header_assemble(struct smb_hdr *buffer, char smb_command /* command */ ,
		const struct cifs_tcon *treeCon, int word_count
		/* length of fixed section (word count) in two byte units  */)
{
	char *temp = (char *) buffer;

	memset(temp, 0, 256); /* bigger than MAX_CIFS_HDR_SIZE */

	buffer->smb_buf_length = cpu_to_be32(
	    (2 * word_count) + sizeof(struct smb_hdr) -
	    4 /*  RFC 1001 length field does not count */  +
	    2 /* for bcc field itself */) ;

	buffer->Protocol[0] = 0xFF;
	buffer->Protocol[1] = 'S';
	buffer->Protocol[2] = 'M';
	buffer->Protocol[3] = 'B';
	buffer->Command = smb_command;
	buffer->Flags = 0x00;	/* case sensitive */
	buffer->Flags2 = SMBFLG2_KNOWS_LONG_NAMES;
	buffer->Pid = cpu_to_le16((__u16)current->tgid);
	buffer->PidHigh = cpu_to_le16((__u16)(current->tgid >> 16));
	if (treeCon) {
		buffer->Tid = treeCon->tid;
		if (treeCon->ses) {
			if (treeCon->ses->capabilities & CAP_UNICODE)
				buffer->Flags2 |= SMBFLG2_UNICODE;
			if (treeCon->ses->capabilities & CAP_STATUS32)
				buffer->Flags2 |= SMBFLG2_ERR_STATUS;

			/* Uid is not converted */
			buffer->Uid = treeCon->ses->Suid;
			buffer->Mid = get_next_mid(treeCon->ses->server);
		}
		if (treeCon->Flags & SMB_SHARE_IS_IN_DFS)
			buffer->Flags2 |= SMBFLG2_DFS;
		if (treeCon->nocase)
			buffer->Flags  |= SMBFLG_CASELESS;
		if ((treeCon->ses) && (treeCon->ses->server))
			if (treeCon->ses->server->sign)
				buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;
	}

/*  endian conversion of flags is now done just before sending */
	buffer->WordCount = (char) word_count;
	return;
}

static int
check_smb_hdr(struct smb_hdr *smb)
{
	/* does it have the right SMB "signature" ? */
	if (*(__le32 *) smb->Protocol != cpu_to_le32(0x424d53ff)) {
		cifs_dbg(VFS, "Bad protocol string signature header 0x%x\n",
			 *(unsigned int *)smb->Protocol);
		return 1;
	}

	/* if it's a response then accept */
	if (smb->Flags & SMBFLG_RESPONSE)
		return 0;

	/* only one valid case where server sends us request */
	if (smb->Command == SMB_COM_LOCKING_ANDX)
		return 0;

	cifs_dbg(VFS, "Server sent request, not response. mid=%u\n",
		 get_mid(smb));
	return 1;
}

int
checkSMB(char *buf, unsigned int total_read, struct TCP_Server_Info *server)
{
	struct smb_hdr *smb = (struct smb_hdr *)buf;
	__u32 rfclen = be32_to_cpu(smb->smb_buf_length);
	__u32 clc_len;  /* calculated length */
	cifs_dbg(FYI, "checkSMB Length: 0x%x, smb_buf_length: 0x%x\n",
		 total_read, rfclen);

	/* is this frame too small to even get to a BCC? */
	if (total_read < 2 + sizeof(struct smb_hdr)) {
		if ((total_read >= sizeof(struct smb_hdr) - 1)
			    && (smb->Status.CifsError != 0)) {
			/* it's an error return */
			smb->WordCount = 0;
			/* some error cases do not return wct and bcc */
			return 0;
		} else if ((total_read == sizeof(struct smb_hdr) + 1) &&
				(smb->WordCount == 0)) {
			char *tmp = (char *)smb;
			/* Need to work around a bug in two servers here */
			/* First, check if the part of bcc they sent was zero */
			if (tmp[sizeof(struct smb_hdr)] == 0) {
				/* some servers return only half of bcc
				 * on simple responses (wct, bcc both zero)
				 * in particular have seen this on
				 * ulogoffX and FindClose. This leaves
				 * one byte of bcc potentially unitialized
				 */
				/* zero rest of bcc */
				tmp[sizeof(struct smb_hdr)+1] = 0;
				return 0;
			}
			cifs_dbg(VFS, "rcvd invalid byte count (bcc)\n");
		} else {
			cifs_dbg(VFS, "Length less than smb header size\n");
		}
		return -EIO;
	}

	/* otherwise, there is enough to get to the BCC */
	if (check_smb_hdr(smb))
		return -EIO;
	clc_len = smbCalcSize(smb, server);

	if (4 + rfclen != total_read) {
		cifs_dbg(VFS, "Length read does not match RFC1001 length %d\n",
			 rfclen);
		return -EIO;
	}

	if (4 + rfclen != clc_len) {
		__u16 mid = get_mid(smb);
		/* check if bcc wrapped around for large read responses */
		if ((rfclen > 64 * 1024) && (rfclen > clc_len)) {
			/* check if lengths match mod 64K */
			if (((4 + rfclen) & 0xFFFF) == (clc_len & 0xFFFF))
				return 0; /* bcc wrapped */
		}
		cifs_dbg(FYI, "Calculated size %u vs length %u mismatch for mid=%u\n",
			 clc_len, 4 + rfclen, mid);

		if (4 + rfclen < clc_len) {
			cifs_dbg(VFS, "RFC1001 size %u smaller than SMB for mid=%u\n",
				 rfclen, mid);
			return -EIO;
		} else if (rfclen > clc_len + 512) {
			/*
			 * Some servers (Windows XP in particular) send more
			 * data than the lengths in the SMB packet would
			 * indicate on certain calls (byte range locks and
			 * trans2 find first calls in particular). While the
			 * client can handle such a frame by ignoring the
			 * trailing data, we choose limit the amount of extra
			 * data to 512 bytes.
			 */
			cifs_dbg(VFS, "RFC1001 size %u more than 512 bytes larger than SMB for mid=%u\n",
				 rfclen, mid);
			return -EIO;
		}
	}
	return 0;
}

bool
is_valid_oplock_break(char *buffer, struct TCP_Server_Info *srv)
{
	struct smb_hdr *buf = (struct smb_hdr *)buffer;
	struct smb_com_lock_req *pSMB = (struct smb_com_lock_req *)buf;
	struct list_head *tmp, *tmp1, *tmp2;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct cifsInodeInfo *pCifsInode;
	struct cifsFileInfo *netfile;

	cifs_dbg(FYI, "Checking for oplock break or dnotify response\n");
	if ((pSMB->hdr.Command == SMB_COM_NT_TRANSACT) &&
	   (pSMB->hdr.Flags & SMBFLG_RESPONSE)) {
		struct smb_com_transaction_change_notify_rsp *pSMBr =
			(struct smb_com_transaction_change_notify_rsp *)buf;
		struct file_notify_information *pnotify;
		__u32 data_offset = 0;
		if (get_bcc(buf) > sizeof(struct file_notify_information)) {
			data_offset = le32_to_cpu(pSMBr->DataOffset);

			pnotify = (struct file_notify_information *)
				((char *)&pSMBr->hdr.Protocol + data_offset);
			cifs_dbg(FYI, "dnotify on %s Action: 0x%x\n",
				 pnotify->FileName, pnotify->Action);
			/*   cifs_dump_mem("Rcvd notify Data: ",buf,
				sizeof(struct smb_hdr)+60); */
			return true;
		}
		if (pSMBr->hdr.Status.CifsError) {
			cifs_dbg(FYI, "notify err 0x%x\n",
				 pSMBr->hdr.Status.CifsError);
			return true;
		}
		return false;
	}
	if (pSMB->hdr.Command != SMB_COM_LOCKING_ANDX)
		return false;
	if (pSMB->hdr.Flags & SMBFLG_RESPONSE) {
		/* no sense logging error on invalid handle on oplock
		   break - harmless race between close request and oplock
		   break response is expected from time to time writing out
		   large dirty files cached on the client */
		if ((NT_STATUS_INVALID_HANDLE) ==
		   le32_to_cpu(pSMB->hdr.Status.CifsError)) {
			cifs_dbg(FYI, "invalid handle on oplock break\n");
			return true;
		} else if (ERRbadfid ==
		   le16_to_cpu(pSMB->hdr.Status.DosError.Error)) {
			return true;
		} else {
			return false; /* on valid oplock brk we get "request" */
		}
	}
	if (pSMB->hdr.WordCount != 8)
		return false;

	cifs_dbg(FYI, "oplock type 0x%x level 0x%x\n",
		 pSMB->LockType, pSMB->OplockLevel);
	if (!(pSMB->LockType & LOCKING_ANDX_OPLOCK_RELEASE))
		return false;

	/* look up tcon based on tid & uid */
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &srv->smb_ses_list) {
		ses = list_entry(tmp, struct cifs_ses, smb_ses_list);
		list_for_each(tmp1, &ses->tcon_list) {
			tcon = list_entry(tmp1, struct cifs_tcon, tcon_list);
			if (tcon->tid != buf->Tid)
				continue;

			cifs_stats_inc(&tcon->stats.cifs_stats.num_oplock_brks);
			spin_lock(&tcon->open_file_lock);
			list_for_each(tmp2, &tcon->openFileList) {
				netfile = list_entry(tmp2, struct cifsFileInfo,
						     tlist);
				if (pSMB->Fid != netfile->fid.netfid)
					continue;

				cifs_dbg(FYI, "file id match, oplock break\n");
				pCifsInode = CIFS_I(d_inode(netfile->dentry));

				set_bit(CIFS_INODE_PENDING_OPLOCK_BREAK,
					&pCifsInode->flags);

				/*
				 * Set flag if the server downgrades the oplock
				 * to L2 else clear.
				 */
				if (pSMB->OplockLevel)
					set_bit(
					   CIFS_INODE_DOWNGRADE_OPLOCK_TO_L2,
					   &pCifsInode->flags);
				else
					clear_bit(
					   CIFS_INODE_DOWNGRADE_OPLOCK_TO_L2,
					   &pCifsInode->flags);

				queue_work(cifsoplockd_wq,
					   &netfile->oplock_break);
				netfile->oplock_break_cancelled = false;

				spin_unlock(&tcon->open_file_lock);
				spin_unlock(&cifs_tcp_ses_lock);
				return true;
			}
			spin_unlock(&tcon->open_file_lock);
			spin_unlock(&cifs_tcp_ses_lock);
			cifs_dbg(FYI, "No matching file for oplock break\n");
			return true;
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
	cifs_dbg(FYI, "Can not process oplock break for non-existent connection\n");
	return true;
}

void
dump_smb(void *buf, int smb_buf_length)
{
	if (traceSMB == 0)
		return;

	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_NONE, 8, 2, buf,
		       smb_buf_length, true);
}

void
cifs_autodisable_serverino(struct cifs_sb_info *cifs_sb)
{
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;
		cifs_dbg(VFS, "Autodisabling the use of server inode numbers on %s. This server doesn't seem to support them properly. Hardlinks will not be recognized on this mount. Consider mounting with the \"noserverino\" option to silence this message.\n",
			 cifs_sb_master_tcon(cifs_sb)->treeName);
	}
}

void cifs_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock)
{
	oplock &= 0xF;

	if (oplock == OPLOCK_EXCLUSIVE) {
		cinode->oplock = CIFS_CACHE_WRITE_FLG | CIFS_CACHE_READ_FLG;
		cifs_dbg(FYI, "Exclusive Oplock granted on inode %p\n",
			 &cinode->vfs_inode);
	} else if (oplock == OPLOCK_READ) {
		cinode->oplock = CIFS_CACHE_READ_FLG;
		cifs_dbg(FYI, "Level II Oplock granted on inode %p\n",
			 &cinode->vfs_inode);
	} else
		cinode->oplock = 0;
}

/*
 * We wait for oplock breaks to be processed before we attempt to perform
 * writes.
 */
int cifs_get_writer(struct cifsInodeInfo *cinode)
{
	int rc;

start:
	rc = wait_on_bit(&cinode->flags, CIFS_INODE_PENDING_OPLOCK_BREAK,
			 TASK_KILLABLE);
	if (rc)
		return rc;

	spin_lock(&cinode->writers_lock);
	if (!cinode->writers)
		set_bit(CIFS_INODE_PENDING_WRITERS, &cinode->flags);
	cinode->writers++;
	/* Check to see if we have started servicing an oplock break */
	if (test_bit(CIFS_INODE_PENDING_OPLOCK_BREAK, &cinode->flags)) {
		cinode->writers--;
		if (cinode->writers == 0) {
			clear_bit(CIFS_INODE_PENDING_WRITERS, &cinode->flags);
			wake_up_bit(&cinode->flags, CIFS_INODE_PENDING_WRITERS);
		}
		spin_unlock(&cinode->writers_lock);
		goto start;
	}
	spin_unlock(&cinode->writers_lock);
	return 0;
}

void cifs_put_writer(struct cifsInodeInfo *cinode)
{
	spin_lock(&cinode->writers_lock);
	cinode->writers--;
	if (cinode->writers == 0) {
		clear_bit(CIFS_INODE_PENDING_WRITERS, &cinode->flags);
		wake_up_bit(&cinode->flags, CIFS_INODE_PENDING_WRITERS);
	}
	spin_unlock(&cinode->writers_lock);
}

void cifs_done_oplock_break(struct cifsInodeInfo *cinode)
{
	clear_bit(CIFS_INODE_PENDING_OPLOCK_BREAK, &cinode->flags);
	wake_up_bit(&cinode->flags, CIFS_INODE_PENDING_OPLOCK_BREAK);
}

bool
backup_cred(struct cifs_sb_info *cifs_sb)
{
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_BACKUPUID) {
		if (uid_eq(cifs_sb->mnt_backupuid, current_fsuid()))
			return true;
	}
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_BACKUPGID) {
		if (in_group_p(cifs_sb->mnt_backupgid))
			return true;
	}

	return false;
}

void
cifs_del_pending_open(struct cifs_pending_open *open)
{
	spin_lock(&tlink_tcon(open->tlink)->open_file_lock);
	list_del(&open->olist);
	spin_unlock(&tlink_tcon(open->tlink)->open_file_lock);
}

void
cifs_add_pending_open_locked(struct cifs_fid *fid, struct tcon_link *tlink,
			     struct cifs_pending_open *open)
{
	memcpy(open->lease_key, fid->lease_key, SMB2_LEASE_KEY_SIZE);
	open->oplock = CIFS_OPLOCK_NO_CHANGE;
	open->tlink = tlink;
	fid->pending_open = open;
	list_add_tail(&open->olist, &tlink_tcon(tlink)->pending_opens);
}

void
cifs_add_pending_open(struct cifs_fid *fid, struct tcon_link *tlink,
		      struct cifs_pending_open *open)
{
	spin_lock(&tlink_tcon(tlink)->open_file_lock);
	cifs_add_pending_open_locked(fid, tlink, open);
	spin_unlock(&tlink_tcon(open->tlink)->open_file_lock);
}

/* parses DFS refferal V3 structure
 * caller is responsible for freeing target_nodes
 * returns:
 * - on success - 0
 * - on failure - errno
 */
int
parse_dfs_referrals(struct get_dfs_referral_rsp *rsp, u32 rsp_size,
		    unsigned int *num_of_nodes,
		    struct dfs_info3_param **target_nodes,
		    const struct nls_table *nls_codepage, int remap,
		    const char *searchName, bool is_unicode)
{
	int i, rc = 0;
	char *data_end;
	struct dfs_referral_level_3 *ref;

	*num_of_nodes = le16_to_cpu(rsp->NumberOfReferrals);

	if (*num_of_nodes < 1) {
		cifs_dbg(VFS, "num_referrals: must be at least > 0, but we get num_referrals = %d\n",
			 *num_of_nodes);
		rc = -EINVAL;
		goto parse_DFS_referrals_exit;
	}

	ref = (struct dfs_referral_level_3 *) &(rsp->referrals);
	if (ref->VersionNumber != cpu_to_le16(3)) {
		cifs_dbg(VFS, "Referrals of V%d version are not supported, should be V3\n",
			 le16_to_cpu(ref->VersionNumber));
		rc = -EINVAL;
		goto parse_DFS_referrals_exit;
	}

	/* get the upper boundary of the resp buffer */
	data_end = (char *)rsp + rsp_size;

	cifs_dbg(FYI, "num_referrals: %d dfs flags: 0x%x ...\n",
		 *num_of_nodes, le32_to_cpu(rsp->DFSFlags));

	*target_nodes = kcalloc(*num_of_nodes, sizeof(struct dfs_info3_param),
				GFP_KERNEL);
	if (*target_nodes == NULL) {
		rc = -ENOMEM;
		goto parse_DFS_referrals_exit;
	}

	/* collect necessary data from referrals */
	for (i = 0; i < *num_of_nodes; i++) {
		char *temp;
		int max_len;
		struct dfs_info3_param *node = (*target_nodes)+i;

		node->flags = le32_to_cpu(rsp->DFSFlags);
		if (is_unicode) {
			__le16 *tmp = kmalloc(strlen(searchName)*2 + 2,
						GFP_KERNEL);
			if (tmp == NULL) {
				rc = -ENOMEM;
				goto parse_DFS_referrals_exit;
			}
			cifsConvertToUTF16((__le16 *) tmp, searchName,
					   PATH_MAX, nls_codepage, remap);
			node->path_consumed = cifs_utf16_bytes(tmp,
					le16_to_cpu(rsp->PathConsumed),
					nls_codepage);
			kfree(tmp);
		} else
			node->path_consumed = le16_to_cpu(rsp->PathConsumed);

		node->server_type = le16_to_cpu(ref->ServerType);
		node->ref_flag = le16_to_cpu(ref->ReferralEntryFlags);

		/* copy DfsPath */
		temp = (char *)ref + le16_to_cpu(ref->DfsPathOffset);
		max_len = data_end - temp;
		node->path_name = cifs_strndup_from_utf16(temp, max_len,
						is_unicode, nls_codepage);
		if (!node->path_name) {
			rc = -ENOMEM;
			goto parse_DFS_referrals_exit;
		}

		/* copy link target UNC */
		temp = (char *)ref + le16_to_cpu(ref->NetworkAddressOffset);
		max_len = data_end - temp;
		node->node_name = cifs_strndup_from_utf16(temp, max_len,
						is_unicode, nls_codepage);
		if (!node->node_name) {
			rc = -ENOMEM;
			goto parse_DFS_referrals_exit;
		}

		ref++;
	}

parse_DFS_referrals_exit:
	if (rc) {
		free_dfs_info_array(*target_nodes, *num_of_nodes);
		*target_nodes = NULL;
		*num_of_nodes = 0;
	}
	return rc;
}

struct cifs_aio_ctx *
cifs_aio_ctx_alloc(void)
{
	struct cifs_aio_ctx *ctx;

	ctx = kzalloc(sizeof(struct cifs_aio_ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	INIT_LIST_HEAD(&ctx->list);
	mutex_init(&ctx->aio_mutex);
	init_completion(&ctx->done);
	kref_init(&ctx->refcount);
	return ctx;
}

void
cifs_aio_ctx_release(struct kref *refcount)
{
	struct cifs_aio_ctx *ctx = container_of(refcount,
					struct cifs_aio_ctx, refcount);

	cifsFileInfo_put(ctx->cfile);
	kvfree(ctx->bv);
	kfree(ctx);
}

#define CIFS_AIO_KMALLOC_LIMIT (1024 * 1024)

int
setup_aio_ctx_iter(struct cifs_aio_ctx *ctx, struct iov_iter *iter, int rw)
{
	ssize_t rc;
	unsigned int cur_npages;
	unsigned int npages = 0;
	unsigned int i;
	size_t len;
	size_t count = iov_iter_count(iter);
	unsigned int saved_len;
	size_t start;
	unsigned int max_pages = iov_iter_npages(iter, INT_MAX);
	struct page **pages = NULL;
	struct bio_vec *bv = NULL;

	if (iter->type & ITER_KVEC) {
		memcpy(&ctx->iter, iter, sizeof(struct iov_iter));
		ctx->len = count;
		iov_iter_advance(iter, count);
		return 0;
	}

	if (max_pages * sizeof(struct bio_vec) <= CIFS_AIO_KMALLOC_LIMIT)
		bv = kmalloc_array(max_pages, sizeof(struct bio_vec),
				   GFP_KERNEL);

	if (!bv) {
		bv = vmalloc(max_pages * sizeof(struct bio_vec));
		if (!bv)
			return -ENOMEM;
	}

	if (max_pages * sizeof(struct page *) <= CIFS_AIO_KMALLOC_LIMIT)
		pages = kmalloc_array(max_pages, sizeof(struct page *),
				      GFP_KERNEL);

	if (!pages) {
		pages = vmalloc(max_pages * sizeof(struct page *));
		if (!pages) {
			kvfree(bv);
			return -ENOMEM;
		}
	}

	saved_len = count;

	while (count && npages < max_pages) {
		rc = iov_iter_get_pages(iter, pages, count, max_pages, &start);
		if (rc < 0) {
			cifs_dbg(VFS, "couldn't get user pages (rc=%zd)\n", rc);
			break;
		}

		if (rc > count) {
			cifs_dbg(VFS, "get pages rc=%zd more than %zu\n", rc,
				 count);
			break;
		}

		iov_iter_advance(iter, rc);
		count -= rc;
		rc += start;
		cur_npages = DIV_ROUND_UP(rc, PAGE_SIZE);

		if (npages + cur_npages > max_pages) {
			cifs_dbg(VFS, "out of vec array capacity (%u vs %u)\n",
				 npages + cur_npages, max_pages);
			break;
		}

		for (i = 0; i < cur_npages; i++) {
			len = rc > PAGE_SIZE ? PAGE_SIZE : rc;
			bv[npages + i].bv_page = pages[i];
			bv[npages + i].bv_offset = start;
			bv[npages + i].bv_len = len - start;
			rc -= len;
			start = 0;
		}

		npages += cur_npages;
	}

	kvfree(pages);
	ctx->bv = bv;
	ctx->len = saved_len - count;
	ctx->npages = npages;
	iov_iter_bvec(&ctx->iter, ITER_BVEC | rw, ctx->bv, npages, ctx->len);
	return 0;
}

/**
 * cifs_alloc_hash - allocate hash and hash context together
 *
 * The caller has to make sure @sdesc is initialized to either NULL or
 * a valid context. Both can be freed via cifs_free_hash().
 */
int
cifs_alloc_hash(const char *name,
		struct crypto_shash **shash, struct sdesc **sdesc)
{
	int rc = 0;
	size_t size;

	if (*sdesc != NULL)
		return 0;

	*shash = crypto_alloc_shash(name, 0, 0);
	if (IS_ERR(*shash)) {
		cifs_dbg(VFS, "could not allocate crypto %s\n", name);
		rc = PTR_ERR(*shash);
		*shash = NULL;
		*sdesc = NULL;
		return rc;
	}

	size = sizeof(struct shash_desc) + crypto_shash_descsize(*shash);
	*sdesc = kmalloc(size, GFP_KERNEL);
	if (*sdesc == NULL) {
		cifs_dbg(VFS, "no memory left to allocate crypto %s\n", name);
		crypto_free_shash(*shash);
		*shash = NULL;
		return -ENOMEM;
	}

	(*sdesc)->shash.tfm = *shash;
	(*sdesc)->shash.flags = 0x0;
	return 0;
}

/**
 * cifs_free_hash - free hash and hash context together
 *
 * Freeing a NULL hash or context is safe.
 */
void
cifs_free_hash(struct crypto_shash **shash, struct sdesc **sdesc)
{
	kfree(*sdesc);
	*sdesc = NULL;
	if (*shash)
		crypto_free_shash(*shash);
	*shash = NULL;
}
