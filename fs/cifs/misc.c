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
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "smberr.h"
#include "nterr.h"
#include "cifs_unicode.h"

extern mempool_t *cifs_sm_req_poolp;
extern mempool_t *cifs_req_poolp;

/* The xid serves as a useful identifier for each incoming vfs request,
   in a similar way to the mid which is useful to track each sent smb,
   and CurrentXid can also provide a running counter (although it
   will eventually wrap past zero) of the total vfs operations handled
   since the cifs fs was mounted */

unsigned int
_GetXid(void)
{
	unsigned int xid;

	spin_lock(&GlobalMid_Lock);
	GlobalTotalActiveXid++;

	/* keep high water mark for number of simultaneous ops in filesystem */
	if (GlobalTotalActiveXid > GlobalMaxActiveXid)
		GlobalMaxActiveXid = GlobalTotalActiveXid;
	if (GlobalTotalActiveXid > 65000)
		cFYI(1, "warning: more than 65000 requests active");
	xid = GlobalCurrentXid++;
	spin_unlock(&GlobalMid_Lock);
	return xid;
}

void
_FreeXid(unsigned int xid)
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
		cFYI(1, "Null buffer passed to sesInfoFree");
		return;
	}

	atomic_dec(&sesInfoAllocCount);
	kfree(buf_to_free->serverOS);
	kfree(buf_to_free->serverDomain);
	kfree(buf_to_free->serverNOS);
	if (buf_to_free->password) {
		memset(buf_to_free->password, 0, strlen(buf_to_free->password));
		kfree(buf_to_free->password);
	}
	kfree(buf_to_free->user_name);
	kfree(buf_to_free->domainName);
	kfree(buf_to_free);
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
		cFYI(1, "Null buffer passed to tconInfoFree");
		return;
	}
	atomic_dec(&tconInfoAllocCount);
	kfree(buf_to_free->nativeFileSystem);
	if (buf_to_free->password) {
		memset(buf_to_free->password, 0, strlen(buf_to_free->password));
		kfree(buf_to_free->password);
	}
	kfree(buf_to_free);
}

struct smb_hdr *
cifs_buf_get(void)
{
	struct smb_hdr *ret_buf = NULL;

/* We could use negotiated size instead of max_msgsize -
   but it may be more efficient to always alloc same size
   albeit slightly larger than necessary and maxbuffersize
   defaults to this and can not be bigger */
	ret_buf = mempool_alloc(cifs_req_poolp, GFP_NOFS);

	/* clear the first few header bytes */
	/* for most paths, more is cleared in header_assemble */
	if (ret_buf) {
		memset(ret_buf, 0, sizeof(struct smb_hdr) + 3);
		atomic_inc(&bufAllocCount);
#ifdef CONFIG_CIFS_STATS2
		atomic_inc(&totBufAllocCount);
#endif /* CONFIG_CIFS_STATS2 */
	}

	return ret_buf;
}

void
cifs_buf_release(void *buf_to_free)
{
	if (buf_to_free == NULL) {
		/* cFYI(1, "Null buffer passed to cifs_buf_release");*/
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
	if (ret_buf) {
	/* No need to clear memory here, cleared in header assemble */
	/*	memset(ret_buf, 0, sizeof(struct smb_hdr) + 27);*/
		atomic_inc(&smBufAllocCount);
#ifdef CONFIG_CIFS_STATS2
		atomic_inc(&totSmBufAllocCount);
#endif /* CONFIG_CIFS_STATS2 */

	}
	return ret_buf;
}

void
cifs_small_buf_release(void *buf_to_free)
{

	if (buf_to_free == NULL) {
		cFYI(1, "Null buffer passed to cifs_small_buf_release");
		return;
	}
	mempool_free(buf_to_free, cifs_sm_req_poolp);

	atomic_dec(&smBufAllocCount);
	return;
}

/*
	Find a free multiplex id (SMB mid). Otherwise there could be
	mid collisions which might cause problems, demultiplexing the
	wrong response to this request. Multiplex ids could collide if
	one of a series requests takes much longer than the others, or
	if a very large number of long lived requests (byte range
	locks or FindNotify requests) are pending.  No more than
	64K-1 requests can be outstanding at one time.  If no
	mids are available, return zero.  A future optimization
	could make the combination of mids and uid the key we use
	to demultiplex on (rather than mid alone).
	In addition to the above check, the cifs demultiplex
	code already used the command code as a secondary
	check of the frame and if signing is negotiated the
	response would be discarded if the mid were the same
	but the signature was wrong.  Since the mid is not put in the
	pending queue until later (when it is about to be dispatched)
	we do have to limit the number of outstanding requests
	to somewhat less than 64K-1 although it is hard to imagine
	so many threads being in the vfs at one time.
*/
__u16 GetNextMid(struct TCP_Server_Info *server)
{
	__u16 mid = 0;
	__u16 last_mid;
	bool collision;

	spin_lock(&GlobalMid_Lock);
	last_mid = server->CurrentMid; /* we do not want to loop forever */
	server->CurrentMid++;
	/* This nested loop looks more expensive than it is.
	In practice the list of pending requests is short,
	fewer than 50, and the mids are likely to be unique
	on the first pass through the loop unless some request
	takes longer than the 64 thousand requests before it
	(and it would also have to have been a request that
	 did not time out) */
	while (server->CurrentMid != last_mid) {
		struct mid_q_entry *mid_entry;
		unsigned int num_mids;

		collision = false;
		if (server->CurrentMid == 0)
			server->CurrentMid++;

		num_mids = 0;
		list_for_each_entry(mid_entry, &server->pending_mid_q, qhead) {
			++num_mids;
			if (mid_entry->mid == server->CurrentMid &&
			    mid_entry->midState == MID_REQUEST_SUBMITTED) {
				/* This mid is in use, try a different one */
				collision = true;
				break;
			}
		}

		/*
		 * if we have more than 32k mids in the list, then something
		 * is very wrong. Possibly a local user is trying to DoS the
		 * box by issuing long-running calls and SIGKILL'ing them. If
		 * we get to 2^16 mids then we're in big trouble as this
		 * function could loop forever.
		 *
		 * Go ahead and assign out the mid in this situation, but force
		 * an eventual reconnect to clean out the pending_mid_q.
		 */
		if (num_mids > 32768)
			server->tcpStatus = CifsNeedReconnect;

		if (!collision) {
			mid = server->CurrentMid;
			break;
		}
		server->CurrentMid++;
	}
	spin_unlock(&GlobalMid_Lock);
	return mid;
}

/* NB: MID can not be set if treeCon not passed in, in that
   case it is responsbility of caller to set the mid */
void
header_assemble(struct smb_hdr *buffer, char smb_command /* command */ ,
		const struct cifs_tcon *treeCon, int word_count
		/* length of fixed section (word count) in two byte units  */)
{
	struct list_head *temp_item;
	struct cifs_ses *ses;
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
			buffer->Mid = GetNextMid(treeCon->ses->server);
			if (multiuser_mount != 0) {
		/* For the multiuser case, there are few obvious technically  */
		/* possible mechanisms to match the local linux user (uid)    */
		/* to a valid remote smb user (smb_uid):		      */
		/* 	1) Query Winbind (or other local pam/nss daemon       */
		/* 	  for userid/password/logon_domain or credential      */
		/*      2) Query Winbind for uid to sid to username mapping   */
		/* 	   and see if we have a matching password for existing*/
		/*         session for that user perhas getting password by   */
		/*         adding a new pam_cifs module that stores passwords */
		/*         so that the cifs vfs can get at that for all logged*/
		/*	   on users					      */
		/*	3) (Which is the mechanism we have chosen)	      */
		/*	   Search through sessions to the same server for a   */
		/*	   a match on the uid that was passed in on mount     */
		/*         with the current processes uid (or euid?) and use  */
		/* 	   that smb uid.   If no existing smb session for     */
		/* 	   that uid found, use the default smb session ie     */
		/*         the smb session for the volume mounted which is    */
		/* 	   the same as would be used if the multiuser mount   */
		/* 	   flag were disabled.  */

		/*  BB Add support for establishing new tCon and SMB Session  */
		/*      with userid/password pairs found on the smb session   */
		/*	for other target tcp/ip addresses 		BB    */
				if (current_fsuid() != treeCon->ses->linux_uid) {
					cFYI(1, "Multiuser mode and UID "
						 "did not match tcon uid");
					spin_lock(&cifs_tcp_ses_lock);
					list_for_each(temp_item, &treeCon->ses->server->smb_ses_list) {
						ses = list_entry(temp_item, struct cifs_ses, smb_ses_list);
						if (ses->linux_uid == current_fsuid()) {
							if (ses->server == treeCon->ses->server) {
								cFYI(1, "found matching uid substitute right smb_uid");
								buffer->Uid = ses->Suid;
								break;
							} else {
				/* BB eventually call cifs_setup_session here */
								cFYI(1, "local UID found but no smb sess with this server exists");
							}
						}
					}
					spin_unlock(&cifs_tcp_ses_lock);
				}
			}
		}
		if (treeCon->Flags & SMB_SHARE_IS_IN_DFS)
			buffer->Flags2 |= SMBFLG2_DFS;
		if (treeCon->nocase)
			buffer->Flags  |= SMBFLG_CASELESS;
		if ((treeCon->ses) && (treeCon->ses->server))
			if (treeCon->ses->server->sec_mode &
			  (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
				buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;
	}

/*  endian conversion of flags is now done just before sending */
	buffer->WordCount = (char) word_count;
	return;
}

static int
check_smb_hdr(struct smb_hdr *smb, __u16 mid)
{
	/* does it have the right SMB "signature" ? */
	if (*(__le32 *) smb->Protocol != cpu_to_le32(0x424d53ff)) {
		cERROR(1, "Bad protocol string signature header 0x%x",
			*(unsigned int *)smb->Protocol);
		return 1;
	}

	/* Make sure that message ids match */
	if (mid != smb->Mid) {
		cERROR(1, "Mids do not match. received=%u expected=%u",
			smb->Mid, mid);
		return 1;
	}

	/* if it's a response then accept */
	if (smb->Flags & SMBFLG_RESPONSE)
		return 0;

	/* only one valid case where server sends us request */
	if (smb->Command == SMB_COM_LOCKING_ANDX)
		return 0;

	cERROR(1, "Server sent request, not response. mid=%u", smb->Mid);
	return 1;
}

int
checkSMB(struct smb_hdr *smb, __u16 mid, unsigned int total_read)
{
	__u32 rfclen = be32_to_cpu(smb->smb_buf_length);
	__u32 clc_len;  /* calculated length */
	cFYI(0, "checkSMB Length: 0x%x, smb_buf_length: 0x%x",
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
			cERROR(1, "rcvd invalid byte count (bcc)");
		} else {
			cERROR(1, "Length less than smb header size");
		}
		return -EIO;
	}

	/* otherwise, there is enough to get to the BCC */
	if (check_smb_hdr(smb, mid))
		return -EIO;
	clc_len = smbCalcSize(smb);

	if (4 + rfclen != total_read) {
		cERROR(1, "Length read does not match RFC1001 length %d",
				rfclen);
		return -EIO;
	}

	if (4 + rfclen != clc_len) {
		/* check if bcc wrapped around for large read responses */
		if ((rfclen > 64 * 1024) && (rfclen > clc_len)) {
			/* check if lengths match mod 64K */
			if (((4 + rfclen) & 0xFFFF) == (clc_len & 0xFFFF))
				return 0; /* bcc wrapped */
		}
		cFYI(1, "Calculated size %u vs length %u mismatch for mid=%u",
				clc_len, 4 + rfclen, smb->Mid);

		if (4 + rfclen < clc_len) {
			cERROR(1, "RFC1001 size %u smaller than SMB for mid=%u",
					rfclen, smb->Mid);
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
			cERROR(1, "RFC1001 size %u more than 512 bytes larger "
				  "than SMB for mid=%u", rfclen, smb->Mid);
			return -EIO;
		}
	}
	return 0;
}

bool
is_valid_oplock_break(struct smb_hdr *buf, struct TCP_Server_Info *srv)
{
	struct smb_com_lock_req *pSMB = (struct smb_com_lock_req *)buf;
	struct list_head *tmp, *tmp1, *tmp2;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct cifsInodeInfo *pCifsInode;
	struct cifsFileInfo *netfile;

	cFYI(1, "Checking for oplock break or dnotify response");
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
			cFYI(1, "dnotify on %s Action: 0x%x",
				 pnotify->FileName, pnotify->Action);
			/*   cifs_dump_mem("Rcvd notify Data: ",buf,
				sizeof(struct smb_hdr)+60); */
			return true;
		}
		if (pSMBr->hdr.Status.CifsError) {
			cFYI(1, "notify err 0x%d",
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
			cFYI(1, "invalid handle on oplock break");
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

	cFYI(1, "oplock type 0x%d level 0x%d",
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

			cifs_stats_inc(&tcon->num_oplock_brks);
			spin_lock(&cifs_file_list_lock);
			list_for_each(tmp2, &tcon->openFileList) {
				netfile = list_entry(tmp2, struct cifsFileInfo,
						     tlist);
				if (pSMB->Fid != netfile->netfid)
					continue;

				cFYI(1, "file id match, oplock break");
				pCifsInode = CIFS_I(netfile->dentry->d_inode);

				cifs_set_oplock_level(pCifsInode,
					pSMB->OplockLevel ? OPLOCK_READ : 0);
				queue_work(system_nrt_wq,
					   &netfile->oplock_break);
				netfile->oplock_break_cancelled = false;

				spin_unlock(&cifs_file_list_lock);
				spin_unlock(&cifs_tcp_ses_lock);
				return true;
			}
			spin_unlock(&cifs_file_list_lock);
			spin_unlock(&cifs_tcp_ses_lock);
			cFYI(1, "No matching file for oplock break");
			return true;
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
	cFYI(1, "Can not process oplock break for non-existent connection");
	return true;
}

void
dump_smb(struct smb_hdr *smb_buf, int smb_buf_length)
{
	int i, j;
	char debug_line[17];
	unsigned char *buffer;

	if (traceSMB == 0)
		return;

	buffer = (unsigned char *) smb_buf;
	for (i = 0, j = 0; i < smb_buf_length; i++, j++) {
		if (i % 8 == 0) {
			/* have reached the beginning of line */
			printk(KERN_DEBUG "| ");
			j = 0;
		}
		printk("%0#4x ", buffer[i]);
		debug_line[2 * j] = ' ';
		if (isprint(buffer[i]))
			debug_line[1 + (2 * j)] = buffer[i];
		else
			debug_line[1 + (2 * j)] = '_';

		if (i % 8 == 7) {
			/* reached end of line, time to print ascii */
			debug_line[16] = 0;
			printk(" | %s\n", debug_line);
		}
	}
	for (; j < 8; j++) {
		printk("     ");
		debug_line[2 * j] = ' ';
		debug_line[1 + (2 * j)] = ' ';
	}
	printk(" | %s\n", debug_line);
	return;
}

void
cifs_autodisable_serverino(struct cifs_sb_info *cifs_sb)
{
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;
		cERROR(1, "Autodisabling the use of server inode numbers on "
			   "%s. This server doesn't seem to support them "
			   "properly. Hardlinks will not be recognized on this "
			   "mount. Consider mounting with the \"noserverino\" "
			   "option to silence this message.",
			   cifs_sb_master_tcon(cifs_sb)->treeName);
	}
}

void cifs_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock)
{
	oplock &= 0xF;

	if (oplock == OPLOCK_EXCLUSIVE) {
		cinode->clientCanCacheAll = true;
		cinode->clientCanCacheRead = true;
		cFYI(1, "Exclusive Oplock granted on inode %p",
		     &cinode->vfs_inode);
	} else if (oplock == OPLOCK_READ) {
		cinode->clientCanCacheAll = false;
		cinode->clientCanCacheRead = true;
		cFYI(1, "Level II Oplock granted on inode %p",
		    &cinode->vfs_inode);
	} else {
		cinode->clientCanCacheAll = false;
		cinode->clientCanCacheRead = false;
	}
}

bool
backup_cred(struct cifs_sb_info *cifs_sb)
{
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_BACKUPUID) {
		if (cifs_sb->mnt_backupuid == current_fsuid())
			return true;
	}
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_BACKUPGID) {
		if (in_group_p(cifs_sb->mnt_backupgid))
			return true;
	}

	return false;
}
