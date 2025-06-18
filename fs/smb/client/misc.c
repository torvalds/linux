// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
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
#include "cifsfs.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dns_resolve.h"
#include "dfs_cache.h"
#include "dfs.h"
#endif
#include "fs_context.h"
#include "cached_dir.h"

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
		spin_lock_init(&ret_buf->ses_lock);
		ret_buf->ses_status = SES_NEW;
		++ret_buf->ses_count;
		INIT_LIST_HEAD(&ret_buf->smb_ses_list);
		INIT_LIST_HEAD(&ret_buf->tcon_list);
		mutex_init(&ret_buf->session_mutex);
		spin_lock_init(&ret_buf->iface_lock);
		INIT_LIST_HEAD(&ret_buf->iface_list);
		spin_lock_init(&ret_buf->chan_lock);
	}
	return ret_buf;
}

void
sesInfoFree(struct cifs_ses *buf_to_free)
{
	struct cifs_server_iface *iface = NULL, *niface = NULL;

	if (buf_to_free == NULL) {
		cifs_dbg(FYI, "Null buffer passed to sesInfoFree\n");
		return;
	}

	unload_nls(buf_to_free->local_nls);
	atomic_dec(&sesInfoAllocCount);
	kfree(buf_to_free->serverOS);
	kfree(buf_to_free->serverDomain);
	kfree(buf_to_free->serverNOS);
	kfree_sensitive(buf_to_free->password);
	kfree_sensitive(buf_to_free->password2);
	kfree(buf_to_free->user_name);
	kfree(buf_to_free->domainName);
	kfree(buf_to_free->dns_dom);
	kfree_sensitive(buf_to_free->auth_key.response);
	spin_lock(&buf_to_free->iface_lock);
	list_for_each_entry_safe(iface, niface, &buf_to_free->iface_list,
				 iface_head)
		kref_put(&iface->refcount, release_iface);
	spin_unlock(&buf_to_free->iface_lock);
	kfree_sensitive(buf_to_free);
}

struct cifs_tcon *
tcon_info_alloc(bool dir_leases_enabled, enum smb3_tcon_ref_trace trace)
{
	struct cifs_tcon *ret_buf;
	static atomic_t tcon_debug_id;

	ret_buf = kzalloc(sizeof(*ret_buf), GFP_KERNEL);
	if (!ret_buf)
		return NULL;

	if (dir_leases_enabled == true) {
		ret_buf->cfids = init_cached_dirs();
		if (!ret_buf->cfids) {
			kfree(ret_buf);
			return NULL;
		}
	}
	/* else ret_buf->cfids is already set to NULL above */

	atomic_inc(&tconInfoAllocCount);
	ret_buf->status = TID_NEW;
	ret_buf->debug_id = atomic_inc_return(&tcon_debug_id);
	ret_buf->tc_count = 1;
	spin_lock_init(&ret_buf->tc_lock);
	INIT_LIST_HEAD(&ret_buf->openFileList);
	INIT_LIST_HEAD(&ret_buf->tcon_list);
	INIT_LIST_HEAD(&ret_buf->cifs_sb_list);
	spin_lock_init(&ret_buf->open_file_lock);
	spin_lock_init(&ret_buf->stat_lock);
	spin_lock_init(&ret_buf->sb_list_lock);
	atomic_set(&ret_buf->num_local_opens, 0);
	atomic_set(&ret_buf->num_remote_opens, 0);
	ret_buf->stats_from_time = ktime_get_real_seconds();
#ifdef CONFIG_CIFS_FSCACHE
	mutex_init(&ret_buf->fscache_lock);
#endif
	trace_smb3_tcon_ref(ret_buf->debug_id, ret_buf->tc_count, trace);
#ifdef CONFIG_CIFS_DFS_UPCALL
	INIT_LIST_HEAD(&ret_buf->dfs_ses_list);
#endif

	return ret_buf;
}

void
tconInfoFree(struct cifs_tcon *tcon, enum smb3_tcon_ref_trace trace)
{
	if (tcon == NULL) {
		cifs_dbg(FYI, "Null buffer passed to tconInfoFree\n");
		return;
	}
	trace_smb3_tcon_ref(tcon->debug_id, tcon->tc_count, trace);
	free_cached_dirs(tcon->cfids);
	atomic_dec(&tconInfoAllocCount);
	kfree(tcon->nativeFileSystem);
	kfree_sensitive(tcon->password);
	kfree(tcon->origin_fullpath);
	kfree(tcon);
}

struct smb_hdr *
cifs_buf_get(void)
{
	struct smb_hdr *ret_buf = NULL;
	/*
	 * SMB2 header is bigger than CIFS one - no problems to clean some
	 * more bytes for CIFS.
	 */
	size_t buf_size = sizeof(struct smb2_hdr);

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
	atomic_inc(&buf_alloc_count);
#ifdef CONFIG_CIFS_STATS2
	atomic_inc(&total_buf_alloc_count);
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

	atomic_dec(&buf_alloc_count);
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
	atomic_inc(&small_buf_alloc_count);
#ifdef CONFIG_CIFS_STATS2
	atomic_inc(&total_small_buf_alloc_count);
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

	atomic_dec(&small_buf_alloc_count);
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
   case it is responsibility of caller to set the mid */
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
			if (treeCon->ses->server)
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

	/*
	 * Windows NT server returns error resposne (e.g. STATUS_DELETE_PENDING
	 * or STATUS_OBJECT_NAME_NOT_FOUND or ERRDOS/ERRbadfile or any other)
	 * for some TRANS2 requests without the RESPONSE flag set in header.
	 */
	if (smb->Command == SMB_COM_TRANSACTION2 && smb->Status.CifsError != 0)
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
				 * one byte of bcc potentially uninitialized
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
	} else if (total_read < sizeof(*smb) + 2 * smb->WordCount) {
		cifs_dbg(VFS, "%s: can't read BCC due to invalid WordCount(%u)\n",
			 __func__, smb->WordCount);
		return -EIO;
	}

	/* otherwise, there is enough to get to the BCC */
	if (check_smb_hdr(smb))
		return -EIO;
	clc_len = smbCalcSize(smb);

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
	struct TCP_Server_Info *pserver;
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
		size_t len = srv->total_read - sizeof(pSMBr->hdr.smb_buf_length);

		if (get_bcc(buf) > sizeof(struct file_notify_information)) {
			data_offset = le32_to_cpu(pSMBr->DataOffset);

			if (data_offset >
			    len - sizeof(struct file_notify_information)) {
				cifs_dbg(FYI, "Invalid data_offset %u\n",
					 data_offset);
				return true;
			}
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
			cifs_dbg(FYI, "Invalid handle on oplock break\n");
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

	/* If server is a channel, select the primary channel */
	pserver = SERVER_IS_CHAN(srv) ? srv->primary_server : srv;

	/* look up tcon based on tid & uid */
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &pserver->smb_ses_list, smb_ses_list) {
		if (cifs_ses_exiting(ses))
			continue;
		list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
			if (tcon->tid != buf->Tid)
				continue;

			cifs_stats_inc(&tcon->stats.cifs_stats.num_oplock_brks);
			spin_lock(&tcon->open_file_lock);
			list_for_each_entry(netfile, &tcon->openFileList, tlist) {
				if (pSMB->Fid != netfile->fid.netfid)
					continue;

				cifs_dbg(FYI, "file id match, oplock break\n");
				pCifsInode = CIFS_I(d_inode(netfile->dentry));

				set_bit(CIFS_INODE_PENDING_OPLOCK_BREAK,
					&pCifsInode->flags);

				netfile->oplock_epoch = 0;
				netfile->oplock_level = pSMB->OplockLevel;
				netfile->oplock_break_cancelled = false;
				cifs_queue_oplock_break(netfile);

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
		struct cifs_tcon *tcon = NULL;

		if (cifs_sb->master_tlink)
			tcon = cifs_sb_master_tcon(cifs_sb);

		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;
		cifs_sb->mnt_cifs_serverino_autodisabled = true;
		cifs_dbg(VFS, "Autodisabling the use of server inode numbers on %s\n",
			 tcon ? tcon->tree_name : "new server");
		cifs_dbg(VFS, "The server doesn't seem to support them properly or the files might be on different servers (DFS)\n");
		cifs_dbg(VFS, "Hardlinks will not be recognized on this mount. Consider mounting with the \"noserverino\" option to silence this message.\n");

	}
}

void cifs_set_oplock_level(struct cifsInodeInfo *cinode, __u32 oplock)
{
	oplock &= 0xF;

	if (oplock == OPLOCK_EXCLUSIVE) {
		cinode->oplock = CIFS_CACHE_WRITE_FLG | CIFS_CACHE_READ_FLG;
		cifs_dbg(FYI, "Exclusive Oplock granted on inode %p\n",
			 &cinode->netfs.inode);
	} else if (oplock == OPLOCK_READ) {
		cinode->oplock = CIFS_CACHE_READ_FLG;
		cifs_dbg(FYI, "Level II Oplock granted on inode %p\n",
			 &cinode->netfs.inode);
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

/**
 * cifs_queue_oplock_break - queue the oplock break handler for cfile
 * @cfile: The file to break the oplock on
 *
 * This function is called from the demultiplex thread when it
 * receives an oplock break for @cfile.
 *
 * Assumes the tcon->open_file_lock is held.
 * Assumes cfile->file_info_lock is NOT held.
 */
void cifs_queue_oplock_break(struct cifsFileInfo *cfile)
{
	/*
	 * Bump the handle refcount now while we hold the
	 * open_file_lock to enforce the validity of it for the oplock
	 * break handler. The matching put is done at the end of the
	 * handler.
	 */
	cifsFileInfo_get(cfile);

	queue_work(cifsoplockd_wq, &cfile->oplock_break);
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
		if (uid_eq(cifs_sb->ctx->backupuid, current_fsuid()))
			return true;
	}
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_BACKUPGID) {
		if (in_group_p(cifs_sb->ctx->backupgid))
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

/*
 * Critical section which runs after acquiring deferred_lock.
 * As there is no reference count on cifs_deferred_close, pdclose
 * should not be used outside deferred_lock.
 */
bool
cifs_is_deferred_close(struct cifsFileInfo *cfile, struct cifs_deferred_close **pdclose)
{
	struct cifs_deferred_close *dclose;

	list_for_each_entry(dclose, &CIFS_I(d_inode(cfile->dentry))->deferred_closes, dlist) {
		if ((dclose->netfid == cfile->fid.netfid) &&
			(dclose->persistent_fid == cfile->fid.persistent_fid) &&
			(dclose->volatile_fid == cfile->fid.volatile_fid)) {
			*pdclose = dclose;
			return true;
		}
	}
	return false;
}

/*
 * Critical section which runs after acquiring deferred_lock.
 */
void
cifs_add_deferred_close(struct cifsFileInfo *cfile, struct cifs_deferred_close *dclose)
{
	bool is_deferred = false;
	struct cifs_deferred_close *pdclose;

	is_deferred = cifs_is_deferred_close(cfile, &pdclose);
	if (is_deferred) {
		kfree(dclose);
		return;
	}

	dclose->tlink = cfile->tlink;
	dclose->netfid = cfile->fid.netfid;
	dclose->persistent_fid = cfile->fid.persistent_fid;
	dclose->volatile_fid = cfile->fid.volatile_fid;
	list_add_tail(&dclose->dlist, &CIFS_I(d_inode(cfile->dentry))->deferred_closes);
}

/*
 * Critical section which runs after acquiring deferred_lock.
 */
void
cifs_del_deferred_close(struct cifsFileInfo *cfile)
{
	bool is_deferred = false;
	struct cifs_deferred_close *dclose;

	is_deferred = cifs_is_deferred_close(cfile, &dclose);
	if (!is_deferred)
		return;
	list_del(&dclose->dlist);
	kfree(dclose);
}

void
cifs_close_deferred_file(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *cfile = NULL;
	struct file_list *tmp_list, *tmp_next_list;
	LIST_HEAD(file_head);

	if (cifs_inode == NULL)
		return;

	spin_lock(&cifs_inode->open_file_lock);
	list_for_each_entry(cfile, &cifs_inode->openFileList, flist) {
		if (delayed_work_pending(&cfile->deferred)) {
			if (cancel_delayed_work(&cfile->deferred)) {
				spin_lock(&cifs_inode->deferred_lock);
				cifs_del_deferred_close(cfile);
				spin_unlock(&cifs_inode->deferred_lock);

				tmp_list = kmalloc(sizeof(struct file_list), GFP_ATOMIC);
				if (tmp_list == NULL)
					break;
				tmp_list->cfile = cfile;
				list_add_tail(&tmp_list->list, &file_head);
			}
		}
	}
	spin_unlock(&cifs_inode->open_file_lock);

	list_for_each_entry_safe(tmp_list, tmp_next_list, &file_head, list) {
		_cifsFileInfo_put(tmp_list->cfile, false, false);
		list_del(&tmp_list->list);
		kfree(tmp_list);
	}
}

void
cifs_close_all_deferred_files(struct cifs_tcon *tcon)
{
	struct cifsFileInfo *cfile;
	struct file_list *tmp_list, *tmp_next_list;
	LIST_HEAD(file_head);

	spin_lock(&tcon->open_file_lock);
	list_for_each_entry(cfile, &tcon->openFileList, tlist) {
		if (delayed_work_pending(&cfile->deferred)) {
			if (cancel_delayed_work(&cfile->deferred)) {
				spin_lock(&CIFS_I(d_inode(cfile->dentry))->deferred_lock);
				cifs_del_deferred_close(cfile);
				spin_unlock(&CIFS_I(d_inode(cfile->dentry))->deferred_lock);

				tmp_list = kmalloc(sizeof(struct file_list), GFP_ATOMIC);
				if (tmp_list == NULL)
					break;
				tmp_list->cfile = cfile;
				list_add_tail(&tmp_list->list, &file_head);
			}
		}
	}
	spin_unlock(&tcon->open_file_lock);

	list_for_each_entry_safe(tmp_list, tmp_next_list, &file_head, list) {
		_cifsFileInfo_put(tmp_list->cfile, true, false);
		list_del(&tmp_list->list);
		kfree(tmp_list);
	}
}
void
cifs_close_deferred_file_under_dentry(struct cifs_tcon *tcon, const char *path)
{
	struct cifsFileInfo *cfile;
	struct file_list *tmp_list, *tmp_next_list;
	void *page;
	const char *full_path;
	LIST_HEAD(file_head);

	page = alloc_dentry_path();
	spin_lock(&tcon->open_file_lock);
	list_for_each_entry(cfile, &tcon->openFileList, tlist) {
		full_path = build_path_from_dentry(cfile->dentry, page);
		if (strstr(full_path, path)) {
			if (delayed_work_pending(&cfile->deferred)) {
				if (cancel_delayed_work(&cfile->deferred)) {
					spin_lock(&CIFS_I(d_inode(cfile->dentry))->deferred_lock);
					cifs_del_deferred_close(cfile);
					spin_unlock(&CIFS_I(d_inode(cfile->dentry))->deferred_lock);

					tmp_list = kmalloc(sizeof(struct file_list), GFP_ATOMIC);
					if (tmp_list == NULL)
						break;
					tmp_list->cfile = cfile;
					list_add_tail(&tmp_list->list, &file_head);
				}
			}
		}
	}
	spin_unlock(&tcon->open_file_lock);

	list_for_each_entry_safe(tmp_list, tmp_next_list, &file_head, list) {
		_cifsFileInfo_put(tmp_list->cfile, true, false);
		list_del(&tmp_list->list);
		kfree(tmp_list);
	}
	free_dentry_path(page);
}

/*
 * If a dentry has been deleted, all corresponding open handles should know that
 * so that we do not defer close them.
 */
void cifs_mark_open_handles_for_deleted_file(struct inode *inode,
					     const char *path)
{
	struct cifsFileInfo *cfile;
	void *page;
	const char *full_path;
	struct cifsInodeInfo *cinode = CIFS_I(inode);

	page = alloc_dentry_path();
	spin_lock(&cinode->open_file_lock);

	/*
	 * note: we need to construct path from dentry and compare only if the
	 * inode has any hardlinks. When number of hardlinks is 1, we can just
	 * mark all open handles since they are going to be from the same file.
	 */
	if (inode->i_nlink > 1) {
		list_for_each_entry(cfile, &cinode->openFileList, flist) {
			full_path = build_path_from_dentry(cfile->dentry, page);
			if (!IS_ERR(full_path) && strcmp(full_path, path) == 0)
				cfile->status_file_deleted = true;
		}
	} else {
		list_for_each_entry(cfile, &cinode->openFileList, flist)
			cfile->status_file_deleted = true;
	}
	spin_unlock(&cinode->open_file_lock);
	free_dentry_path(page);
}

/* parses DFS referral V3 structure
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
		cifs_dbg(VFS | ONCE, "%s: [path=%s] num_referrals must be at least > 0, but we got %d\n",
			 __func__, searchName, *num_of_nodes);
		rc = -ENOENT;
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

		node->ttl = le32_to_cpu(ref->TimeToLive);

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

/**
 * cifs_alloc_hash - allocate hash and hash context together
 * @name: The name of the crypto hash algo
 * @sdesc: SHASH descriptor where to put the pointer to the hash TFM
 *
 * The caller has to make sure @sdesc is initialized to either NULL or
 * a valid context. It can be freed via cifs_free_hash().
 */
int
cifs_alloc_hash(const char *name, struct shash_desc **sdesc)
{
	int rc = 0;
	struct crypto_shash *alg = NULL;

	if (*sdesc)
		return 0;

	alg = crypto_alloc_shash(name, 0, 0);
	if (IS_ERR(alg)) {
		cifs_dbg(VFS, "Could not allocate shash TFM '%s'\n", name);
		rc = PTR_ERR(alg);
		*sdesc = NULL;
		return rc;
	}

	*sdesc = kmalloc(sizeof(struct shash_desc) + crypto_shash_descsize(alg), GFP_KERNEL);
	if (*sdesc == NULL) {
		cifs_dbg(VFS, "no memory left to allocate shash TFM '%s'\n", name);
		crypto_free_shash(alg);
		return -ENOMEM;
	}

	(*sdesc)->tfm = alg;
	return 0;
}

/**
 * cifs_free_hash - free hash and hash context together
 * @sdesc: Where to find the pointer to the hash TFM
 *
 * Freeing a NULL descriptor is safe.
 */
void
cifs_free_hash(struct shash_desc **sdesc)
{
	if (unlikely(!sdesc) || !*sdesc)
		return;

	if ((*sdesc)->tfm) {
		crypto_free_shash((*sdesc)->tfm);
		(*sdesc)->tfm = NULL;
	}

	kfree_sensitive(*sdesc);
	*sdesc = NULL;
}

void extract_unc_hostname(const char *unc, const char **h, size_t *len)
{
	const char *end;

	/* skip initial slashes */
	while (*unc && (*unc == '\\' || *unc == '/'))
		unc++;

	end = unc;

	while (*end && !(*end == '\\' || *end == '/'))
		end++;

	*h = unc;
	*len = end - unc;
}

/**
 * copy_path_name - copy src path to dst, possibly truncating
 * @dst: The destination buffer
 * @src: The source name
 *
 * returns number of bytes written (including trailing nul)
 */
int copy_path_name(char *dst, const char *src)
{
	int name_len;

	/*
	 * PATH_MAX includes nul, so if strlen(src) >= PATH_MAX it
	 * will truncate and strlen(dst) will be PATH_MAX-1
	 */
	name_len = strscpy(dst, src, PATH_MAX);
	if (WARN_ON_ONCE(name_len < 0))
		name_len = PATH_MAX-1;

	/* we count the trailing nul */
	name_len++;
	return name_len;
}

struct super_cb_data {
	void *data;
	struct super_block *sb;
};

static void tcon_super_cb(struct super_block *sb, void *arg)
{
	struct super_cb_data *sd = arg;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *t1 = sd->data, *t2;

	if (sd->sb)
		return;

	cifs_sb = CIFS_SB(sb);
	t2 = cifs_sb_master_tcon(cifs_sb);

	spin_lock(&t2->tc_lock);
	if ((t1->ses == t2->ses ||
	     t1->ses->dfs_root_ses == t2->ses->dfs_root_ses) &&
	    t1->ses->server == t2->ses->server &&
	    t2->origin_fullpath &&
	    dfs_src_pathname_equal(t2->origin_fullpath, t1->origin_fullpath))
		sd->sb = sb;
	spin_unlock(&t2->tc_lock);
}

static struct super_block *__cifs_get_super(void (*f)(struct super_block *, void *),
					    void *data)
{
	struct super_cb_data sd = {
		.data = data,
		.sb = NULL,
	};
	struct file_system_type **fs_type = (struct file_system_type *[]) {
		&cifs_fs_type, &smb3_fs_type, NULL,
	};

	for (; *fs_type; fs_type++) {
		iterate_supers_type(*fs_type, f, &sd);
		if (sd.sb) {
			/*
			 * Grab an active reference in order to prevent automounts (DFS links)
			 * of expiring and then freeing up our cifs superblock pointer while
			 * we're doing failover.
			 */
			cifs_sb_active(sd.sb);
			return sd.sb;
		}
	}
	pr_warn_once("%s: could not find dfs superblock\n", __func__);
	return ERR_PTR(-EINVAL);
}

static void __cifs_put_super(struct super_block *sb)
{
	if (!IS_ERR_OR_NULL(sb))
		cifs_sb_deactive(sb);
}

struct super_block *cifs_get_dfs_tcon_super(struct cifs_tcon *tcon)
{
	spin_lock(&tcon->tc_lock);
	if (!tcon->origin_fullpath) {
		spin_unlock(&tcon->tc_lock);
		return ERR_PTR(-ENOENT);
	}
	spin_unlock(&tcon->tc_lock);
	return __cifs_get_super(tcon_super_cb, tcon);
}

void cifs_put_tcp_super(struct super_block *sb)
{
	__cifs_put_super(sb);
}

#ifdef CONFIG_CIFS_DFS_UPCALL
int match_target_ip(struct TCP_Server_Info *server,
		    const char *host, size_t hostlen,
		    bool *result)
{
	struct sockaddr_storage ss;
	int rc;

	cifs_dbg(FYI, "%s: hostname=%.*s\n", __func__, (int)hostlen, host);

	*result = false;

	rc = dns_resolve_name(server->dns_dom, host, hostlen,
			      (struct sockaddr *)&ss);
	if (rc < 0)
		return rc;

	spin_lock(&server->srv_lock);
	*result = cifs_match_ipaddr((struct sockaddr *)&server->dstaddr, (struct sockaddr *)&ss);
	spin_unlock(&server->srv_lock);
	cifs_dbg(FYI, "%s: ip addresses matched: %s\n", __func__, str_yes_no(*result));
	return 0;
}

int cifs_update_super_prepath(struct cifs_sb_info *cifs_sb, char *prefix)
{
	int rc;

	kfree(cifs_sb->prepath);
	cifs_sb->prepath = NULL;

	if (prefix && *prefix) {
		cifs_sb->prepath = cifs_sanitize_prepath(prefix, GFP_ATOMIC);
		if (IS_ERR(cifs_sb->prepath)) {
			rc = PTR_ERR(cifs_sb->prepath);
			cifs_sb->prepath = NULL;
			return rc;
		}
		if (cifs_sb->prepath)
			convert_delimiter(cifs_sb->prepath, CIFS_DIR_SEP(cifs_sb));
	}

	cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
	return 0;
}

/*
 * Handle weird Windows SMB server behaviour. It responds with
 * STATUS_OBJECT_NAME_INVALID code to SMB2 QUERY_INFO request for
 * "\<server>\<dfsname>\<linkpath>" DFS reference, where <dfsname> contains
 * non-ASCII unicode symbols.
 */
int cifs_inval_name_dfs_link_error(const unsigned int xid,
				   struct cifs_tcon *tcon,
				   struct cifs_sb_info *cifs_sb,
				   const char *full_path,
				   bool *islink)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifs_ses *ses = tcon->ses;
	size_t len;
	char *path;
	char *ref_path;

	*islink = false;

	/*
	 * Fast path - skip check when @full_path doesn't have a prefix path to
	 * look up or tcon is not DFS.
	 */
	if (strlen(full_path) < 2 || !cifs_sb ||
	    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS) ||
	    !is_tcon_dfs(tcon))
		return 0;

	spin_lock(&server->srv_lock);
	if (!server->leaf_fullpath) {
		spin_unlock(&server->srv_lock);
		return 0;
	}
	spin_unlock(&server->srv_lock);

	/*
	 * Slow path - tcon is DFS and @full_path has prefix path, so attempt
	 * to get a referral to figure out whether it is an DFS link.
	 */
	len = strnlen(tcon->tree_name, MAX_TREE_SIZE + 1) + strlen(full_path) + 1;
	path = kmalloc(len, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	scnprintf(path, len, "%s%s", tcon->tree_name, full_path);
	ref_path = dfs_cache_canonical_path(path + 1, cifs_sb->local_nls,
					    cifs_remap(cifs_sb));
	kfree(path);

	if (IS_ERR(ref_path)) {
		if (PTR_ERR(ref_path) != -EINVAL)
			return PTR_ERR(ref_path);
	} else {
		struct dfs_info3_param *refs = NULL;
		int num_refs = 0;

		/*
		 * XXX: we are not using dfs_cache_find() here because we might
		 * end up filling all the DFS cache and thus potentially
		 * removing cached DFS targets that the client would eventually
		 * need during failover.
		 */
		ses = CIFS_DFS_ROOT_SES(ses);
		if (ses->server->ops->get_dfs_refer &&
		    !ses->server->ops->get_dfs_refer(xid, ses, ref_path, &refs,
						     &num_refs, cifs_sb->local_nls,
						     cifs_remap(cifs_sb)))
			*islink = refs[0].server_type == DFS_TYPE_LINK;
		free_dfs_info_array(refs, num_refs);
		kfree(ref_path);
	}
	return 0;
}
#endif

int cifs_wait_for_server_reconnect(struct TCP_Server_Info *server, bool retry)
{
	int timeout = 10;
	int rc;

	spin_lock(&server->srv_lock);
	if (server->tcpStatus != CifsNeedReconnect) {
		spin_unlock(&server->srv_lock);
		return 0;
	}
	timeout *= server->nr_targets;
	spin_unlock(&server->srv_lock);

	/*
	 * Give demultiplex thread up to 10 seconds to each target available for
	 * reconnect -- should be greater than cifs socket timeout which is 7
	 * seconds.
	 *
	 * On "soft" mounts we wait once. Hard mounts keep retrying until
	 * process is killed or server comes back on-line.
	 */
	do {
		rc = wait_event_interruptible_timeout(server->response_q,
						      (server->tcpStatus != CifsNeedReconnect),
						      timeout * HZ);
		if (rc < 0) {
			cifs_dbg(FYI, "%s: aborting reconnect due to received signal\n",
				 __func__);
			return -ERESTARTSYS;
		}

		/* are we still trying to reconnect? */
		spin_lock(&server->srv_lock);
		if (server->tcpStatus != CifsNeedReconnect) {
			spin_unlock(&server->srv_lock);
			return 0;
		}
		spin_unlock(&server->srv_lock);
	} while (retry);

	cifs_dbg(FYI, "%s: gave up waiting on reconnect\n", __func__);
	return -EHOSTDOWN;
}
