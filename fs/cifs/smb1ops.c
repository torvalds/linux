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

#include <linux/pagemap.h>
#include <linux/vfs.h>
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

	/*
	 * The response to this call was already factored into the sequence
	 * number when the call went out, so we must adjust it back downward
	 * after signing here.
	 */
	--server->sequence_number;
	rc = smb_send(server, in_buf, be32_to_cpu(in_buf->smb_buf_length));
	mutex_unlock(&server->srv_mutex);

	cFYI(1, "issued NT_CANCEL for mid %u, rc = %d",
		in_buf->Mid, rc);

	return rc;
}

static bool
cifs_compare_fids(struct cifsFileInfo *ob1, struct cifsFileInfo *ob2)
{
	return ob1->fid.netfid == ob2->fid.netfid;
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
cifs_add_credits(struct TCP_Server_Info *server, const unsigned int add,
		 const int optype)
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
cifs_get_credits_field(struct TCP_Server_Info *server, const int optype)
{
	return &server->credits;
}

static unsigned int
cifs_get_credits(struct mid_q_entry *mid)
{
	return 1;
}

/*
 * Find a free multiplex id (SMB mid). Otherwise there could be
 * mid collisions which might cause problems, demultiplexing the
 * wrong response to this request. Multiplex ids could collide if
 * one of a series requests takes much longer than the others, or
 * if a very large number of long lived requests (byte range
 * locks or FindNotify requests) are pending. No more than
 * 64K-1 requests can be outstanding at one time. If no
 * mids are available, return zero. A future optimization
 * could make the combination of mids and uid the key we use
 * to demultiplex on (rather than mid alone).
 * In addition to the above check, the cifs demultiplex
 * code already used the command code as a secondary
 * check of the frame and if signing is negotiated the
 * response would be discarded if the mid were the same
 * but the signature was wrong. Since the mid is not put in the
 * pending queue until later (when it is about to be dispatched)
 * we do have to limit the number of outstanding requests
 * to somewhat less than 64K-1 although it is hard to imagine
 * so many threads being in the vfs at one time.
 */
static __u64
cifs_get_next_mid(struct TCP_Server_Info *server)
{
	__u64 mid = 0;
	__u16 last_mid, cur_mid;
	bool collision;

	spin_lock(&GlobalMid_Lock);

	/* mid is 16 bit only for CIFS/SMB */
	cur_mid = (__u16)((server->CurrentMid) & 0xffff);
	/* we do not want to loop forever */
	last_mid = cur_mid;
	cur_mid++;

	/*
	 * This nested loop looks more expensive than it is.
	 * In practice the list of pending requests is short,
	 * fewer than 50, and the mids are likely to be unique
	 * on the first pass through the loop unless some request
	 * takes longer than the 64 thousand requests before it
	 * (and it would also have to have been a request that
	 * did not time out).
	 */
	while (cur_mid != last_mid) {
		struct mid_q_entry *mid_entry;
		unsigned int num_mids;

		collision = false;
		if (cur_mid == 0)
			cur_mid++;

		num_mids = 0;
		list_for_each_entry(mid_entry, &server->pending_mid_q, qhead) {
			++num_mids;
			if (mid_entry->mid == cur_mid &&
			    mid_entry->mid_state == MID_REQUEST_SUBMITTED) {
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
			mid = (__u64)cur_mid;
			server->CurrentMid = mid;
			break;
		}
		cur_mid++;
	}
	spin_unlock(&GlobalMid_Lock);
	return mid;
}

/*
	return codes:
		0	not a transact2, or all data present
		>0	transact2 with that much data missing
		-EINVAL	invalid transact2
 */
static int
check2ndT2(char *buf)
{
	struct smb_hdr *pSMB = (struct smb_hdr *)buf;
	struct smb_t2_rsp *pSMBt;
	int remaining;
	__u16 total_data_size, data_in_this_rsp;

	if (pSMB->Command != SMB_COM_TRANSACTION2)
		return 0;

	/* check for plausible wct, bcc and t2 data and parm sizes */
	/* check for parm and data offset going beyond end of smb */
	if (pSMB->WordCount != 10) { /* coalesce_t2 depends on this */
		cFYI(1, "invalid transact2 word count");
		return -EINVAL;
	}

	pSMBt = (struct smb_t2_rsp *)pSMB;

	total_data_size = get_unaligned_le16(&pSMBt->t2_rsp.TotalDataCount);
	data_in_this_rsp = get_unaligned_le16(&pSMBt->t2_rsp.DataCount);

	if (total_data_size == data_in_this_rsp)
		return 0;
	else if (total_data_size < data_in_this_rsp) {
		cFYI(1, "total data %d smaller than data in frame %d",
			total_data_size, data_in_this_rsp);
		return -EINVAL;
	}

	remaining = total_data_size - data_in_this_rsp;

	cFYI(1, "missing %d bytes from transact2, check next response",
		remaining);
	if (total_data_size > CIFSMaxBufSize) {
		cERROR(1, "TotalDataSize %d is over maximum buffer %d",
			total_data_size, CIFSMaxBufSize);
		return -EINVAL;
	}
	return remaining;
}

static int
coalesce_t2(char *second_buf, struct smb_hdr *target_hdr)
{
	struct smb_t2_rsp *pSMBs = (struct smb_t2_rsp *)second_buf;
	struct smb_t2_rsp *pSMBt  = (struct smb_t2_rsp *)target_hdr;
	char *data_area_of_tgt;
	char *data_area_of_src;
	int remaining;
	unsigned int byte_count, total_in_tgt;
	__u16 tgt_total_cnt, src_total_cnt, total_in_src;

	src_total_cnt = get_unaligned_le16(&pSMBs->t2_rsp.TotalDataCount);
	tgt_total_cnt = get_unaligned_le16(&pSMBt->t2_rsp.TotalDataCount);

	if (tgt_total_cnt != src_total_cnt)
		cFYI(1, "total data count of primary and secondary t2 differ "
			"source=%hu target=%hu", src_total_cnt, tgt_total_cnt);

	total_in_tgt = get_unaligned_le16(&pSMBt->t2_rsp.DataCount);

	remaining = tgt_total_cnt - total_in_tgt;

	if (remaining < 0) {
		cFYI(1, "Server sent too much data. tgt_total_cnt=%hu "
			"total_in_tgt=%hu", tgt_total_cnt, total_in_tgt);
		return -EPROTO;
	}

	if (remaining == 0) {
		/* nothing to do, ignore */
		cFYI(1, "no more data remains");
		return 0;
	}

	total_in_src = get_unaligned_le16(&pSMBs->t2_rsp.DataCount);
	if (remaining < total_in_src)
		cFYI(1, "transact2 2nd response contains too much data");

	/* find end of first SMB data area */
	data_area_of_tgt = (char *)&pSMBt->hdr.Protocol +
				get_unaligned_le16(&pSMBt->t2_rsp.DataOffset);

	/* validate target area */
	data_area_of_src = (char *)&pSMBs->hdr.Protocol +
				get_unaligned_le16(&pSMBs->t2_rsp.DataOffset);

	data_area_of_tgt += total_in_tgt;

	total_in_tgt += total_in_src;
	/* is the result too big for the field? */
	if (total_in_tgt > USHRT_MAX) {
		cFYI(1, "coalesced DataCount too large (%u)", total_in_tgt);
		return -EPROTO;
	}
	put_unaligned_le16(total_in_tgt, &pSMBt->t2_rsp.DataCount);

	/* fix up the BCC */
	byte_count = get_bcc(target_hdr);
	byte_count += total_in_src;
	/* is the result too big for the field? */
	if (byte_count > USHRT_MAX) {
		cFYI(1, "coalesced BCC too large (%u)", byte_count);
		return -EPROTO;
	}
	put_bcc(byte_count, target_hdr);

	byte_count = be32_to_cpu(target_hdr->smb_buf_length);
	byte_count += total_in_src;
	/* don't allow buffer to overflow */
	if (byte_count > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4) {
		cFYI(1, "coalesced BCC exceeds buffer size (%u)", byte_count);
		return -ENOBUFS;
	}
	target_hdr->smb_buf_length = cpu_to_be32(byte_count);

	/* copy second buffer into end of first buffer */
	memcpy(data_area_of_tgt, data_area_of_src, total_in_src);

	if (remaining != total_in_src) {
		/* more responses to go */
		cFYI(1, "waiting for more secondary responses");
		return 1;
	}

	/* we are done */
	cFYI(1, "found the last secondary response");
	return 0;
}

static bool
cifs_check_trans2(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		  char *buf, int malformed)
{
	if (malformed)
		return false;
	if (check2ndT2(buf) <= 0)
		return false;
	mid->multiRsp = true;
	if (mid->resp_buf) {
		/* merge response - fix up 1st*/
		malformed = coalesce_t2(buf, mid->resp_buf);
		if (malformed > 0)
			return true;
		/* All parts received or packet is malformed. */
		mid->multiEnd = true;
		dequeue_mid(mid, malformed);
		return true;
	}
	if (!server->large_buf) {
		/*FIXME: switch to already allocated largebuf?*/
		cERROR(1, "1st trans2 resp needs bigbuf");
	} else {
		/* Have first buffer */
		mid->resp_buf = buf;
		mid->large_buf = true;
		server->bigbuf = NULL;
	}
	return true;
}

static bool
cifs_need_neg(struct TCP_Server_Info *server)
{
	return server->maxBuf == 0;
}

static int
cifs_negotiate(const unsigned int xid, struct cifs_ses *ses)
{
	int rc;
	rc = CIFSSMBNegotiate(xid, ses);
	if (rc == -EAGAIN) {
		/* retry only once on 1st time connection */
		set_credits(ses->server, 1);
		rc = CIFSSMBNegotiate(xid, ses);
		if (rc == -EAGAIN)
			rc = -EHOSTDOWN;
	}
	return rc;
}

static unsigned int
cifs_negotiate_wsize(struct cifs_tcon *tcon, struct smb_vol *volume_info)
{
	__u64 unix_cap = le64_to_cpu(tcon->fsUnixInfo.Capability);
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int wsize;

	/* start with specified wsize, or default */
	if (volume_info->wsize)
		wsize = volume_info->wsize;
	else if (tcon->unix_ext && (unix_cap & CIFS_UNIX_LARGE_WRITE_CAP))
		wsize = CIFS_DEFAULT_IOSIZE;
	else
		wsize = CIFS_DEFAULT_NON_POSIX_WSIZE;

	/* can server support 24-bit write sizes? (via UNIX extensions) */
	if (!tcon->unix_ext || !(unix_cap & CIFS_UNIX_LARGE_WRITE_CAP))
		wsize = min_t(unsigned int, wsize, CIFS_MAX_RFC1002_WSIZE);

	/*
	 * no CAP_LARGE_WRITE_X or is signing enabled without CAP_UNIX set?
	 * Limit it to max buffer offered by the server, minus the size of the
	 * WRITEX header, not including the 4 byte RFC1001 length.
	 */
	if (!(server->capabilities & CAP_LARGE_WRITE_X) ||
	    (!(server->capabilities & CAP_UNIX) &&
	     (server->sec_mode & (SECMODE_SIGN_ENABLED|SECMODE_SIGN_REQUIRED))))
		wsize = min_t(unsigned int, wsize,
				server->maxBuf - sizeof(WRITE_REQ) + 4);

	/* hard limit of CIFS_MAX_WSIZE */
	wsize = min_t(unsigned int, wsize, CIFS_MAX_WSIZE);

	return wsize;
}

static unsigned int
cifs_negotiate_rsize(struct cifs_tcon *tcon, struct smb_vol *volume_info)
{
	__u64 unix_cap = le64_to_cpu(tcon->fsUnixInfo.Capability);
	struct TCP_Server_Info *server = tcon->ses->server;
	unsigned int rsize, defsize;

	/*
	 * Set default value...
	 *
	 * HACK alert! Ancient servers have very small buffers. Even though
	 * MS-CIFS indicates that servers are only limited by the client's
	 * bufsize for reads, testing against win98se shows that it throws
	 * INVALID_PARAMETER errors if you try to request too large a read.
	 * OS/2 just sends back short reads.
	 *
	 * If the server doesn't advertise CAP_LARGE_READ_X, then assume that
	 * it can't handle a read request larger than its MaxBufferSize either.
	 */
	if (tcon->unix_ext && (unix_cap & CIFS_UNIX_LARGE_READ_CAP))
		defsize = CIFS_DEFAULT_IOSIZE;
	else if (server->capabilities & CAP_LARGE_READ_X)
		defsize = CIFS_DEFAULT_NON_POSIX_RSIZE;
	else
		defsize = server->maxBuf - sizeof(READ_RSP);

	rsize = volume_info->rsize ? volume_info->rsize : defsize;

	/*
	 * no CAP_LARGE_READ_X? Then MS-CIFS states that we must limit this to
	 * the client's MaxBufferSize.
	 */
	if (!(server->capabilities & CAP_LARGE_READ_X))
		rsize = min_t(unsigned int, CIFSMaxBufSize, rsize);

	/* hard limit of CIFS_MAX_RSIZE */
	rsize = min_t(unsigned int, rsize, CIFS_MAX_RSIZE);

	return rsize;
}

static void
cifs_qfs_tcon(const unsigned int xid, struct cifs_tcon *tcon)
{
	CIFSSMBQFSDeviceInfo(xid, tcon);
	CIFSSMBQFSAttributeInfo(xid, tcon);
}

static int
cifs_is_path_accessible(const unsigned int xid, struct cifs_tcon *tcon,
			struct cifs_sb_info *cifs_sb, const char *full_path)
{
	int rc;
	FILE_ALL_INFO *file_info;

	file_info = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (file_info == NULL)
		return -ENOMEM;

	rc = CIFSSMBQPathInfo(xid, tcon, full_path, file_info,
			      0 /* not legacy */, cifs_sb->local_nls,
			      cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (rc == -EOPNOTSUPP || rc == -EINVAL)
		rc = SMBQueryInformation(xid, tcon, full_path, file_info,
				cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				  CIFS_MOUNT_MAP_SPECIAL_CHR);
	kfree(file_info);
	return rc;
}

static int
cifs_query_path_info(const unsigned int xid, struct cifs_tcon *tcon,
		     struct cifs_sb_info *cifs_sb, const char *full_path,
		     FILE_ALL_INFO *data, bool *adjustTZ)
{
	int rc;

	/* could do find first instead but this returns more info */
	rc = CIFSSMBQPathInfo(xid, tcon, full_path, data, 0 /* not legacy */,
			      cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	/*
	 * BB optimize code so we do not make the above call when server claims
	 * no NT SMB support and the above call failed at least once - set flag
	 * in tcon or mount.
	 */
	if ((rc == -EOPNOTSUPP) || (rc == -EINVAL)) {
		rc = SMBQueryInformation(xid, tcon, full_path, data,
					 cifs_sb->local_nls,
					 cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		*adjustTZ = true;
	}
	return rc;
}

static int
cifs_get_srv_inum(const unsigned int xid, struct cifs_tcon *tcon,
		  struct cifs_sb_info *cifs_sb, const char *full_path,
		  u64 *uniqueid, FILE_ALL_INFO *data)
{
	/*
	 * We can not use the IndexNumber field by default from Windows or
	 * Samba (in ALL_INFO buf) but we can request it explicitly. The SNIA
	 * CIFS spec claims that this value is unique within the scope of a
	 * share, and the windows docs hint that it's actually unique
	 * per-machine.
	 *
	 * There may be higher info levels that work but are there Windows
	 * server or network appliances for which IndexNumber field is not
	 * guaranteed unique?
	 */
	return CIFSGetSrvInodeNumber(xid, tcon, full_path, uniqueid,
				     cifs_sb->local_nls,
				     cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
}

static int
cifs_query_file_info(const unsigned int xid, struct cifs_tcon *tcon,
		     struct cifs_fid *fid, FILE_ALL_INFO *data)
{
	return CIFSSMBQFileInfo(xid, tcon, fid->netfid, data);
}

static void
cifs_clear_stats(struct cifs_tcon *tcon)
{
#ifdef CONFIG_CIFS_STATS
	atomic_set(&tcon->stats.cifs_stats.num_writes, 0);
	atomic_set(&tcon->stats.cifs_stats.num_reads, 0);
	atomic_set(&tcon->stats.cifs_stats.num_flushes, 0);
	atomic_set(&tcon->stats.cifs_stats.num_oplock_brks, 0);
	atomic_set(&tcon->stats.cifs_stats.num_opens, 0);
	atomic_set(&tcon->stats.cifs_stats.num_posixopens, 0);
	atomic_set(&tcon->stats.cifs_stats.num_posixmkdirs, 0);
	atomic_set(&tcon->stats.cifs_stats.num_closes, 0);
	atomic_set(&tcon->stats.cifs_stats.num_deletes, 0);
	atomic_set(&tcon->stats.cifs_stats.num_mkdirs, 0);
	atomic_set(&tcon->stats.cifs_stats.num_rmdirs, 0);
	atomic_set(&tcon->stats.cifs_stats.num_renames, 0);
	atomic_set(&tcon->stats.cifs_stats.num_t2renames, 0);
	atomic_set(&tcon->stats.cifs_stats.num_ffirst, 0);
	atomic_set(&tcon->stats.cifs_stats.num_fnext, 0);
	atomic_set(&tcon->stats.cifs_stats.num_fclose, 0);
	atomic_set(&tcon->stats.cifs_stats.num_hardlinks, 0);
	atomic_set(&tcon->stats.cifs_stats.num_symlinks, 0);
	atomic_set(&tcon->stats.cifs_stats.num_locks, 0);
	atomic_set(&tcon->stats.cifs_stats.num_acl_get, 0);
	atomic_set(&tcon->stats.cifs_stats.num_acl_set, 0);
#endif
}

static void
cifs_print_stats(struct seq_file *m, struct cifs_tcon *tcon)
{
#ifdef CONFIG_CIFS_STATS
	seq_printf(m, " Oplocks breaks: %d",
		   atomic_read(&tcon->stats.cifs_stats.num_oplock_brks));
	seq_printf(m, "\nReads:  %d Bytes: %llu",
		   atomic_read(&tcon->stats.cifs_stats.num_reads),
		   (long long)(tcon->bytes_read));
	seq_printf(m, "\nWrites: %d Bytes: %llu",
		   atomic_read(&tcon->stats.cifs_stats.num_writes),
		   (long long)(tcon->bytes_written));
	seq_printf(m, "\nFlushes: %d",
		   atomic_read(&tcon->stats.cifs_stats.num_flushes));
	seq_printf(m, "\nLocks: %d HardLinks: %d Symlinks: %d",
		   atomic_read(&tcon->stats.cifs_stats.num_locks),
		   atomic_read(&tcon->stats.cifs_stats.num_hardlinks),
		   atomic_read(&tcon->stats.cifs_stats.num_symlinks));
	seq_printf(m, "\nOpens: %d Closes: %d Deletes: %d",
		   atomic_read(&tcon->stats.cifs_stats.num_opens),
		   atomic_read(&tcon->stats.cifs_stats.num_closes),
		   atomic_read(&tcon->stats.cifs_stats.num_deletes));
	seq_printf(m, "\nPosix Opens: %d Posix Mkdirs: %d",
		   atomic_read(&tcon->stats.cifs_stats.num_posixopens),
		   atomic_read(&tcon->stats.cifs_stats.num_posixmkdirs));
	seq_printf(m, "\nMkdirs: %d Rmdirs: %d",
		   atomic_read(&tcon->stats.cifs_stats.num_mkdirs),
		   atomic_read(&tcon->stats.cifs_stats.num_rmdirs));
	seq_printf(m, "\nRenames: %d T2 Renames %d",
		   atomic_read(&tcon->stats.cifs_stats.num_renames),
		   atomic_read(&tcon->stats.cifs_stats.num_t2renames));
	seq_printf(m, "\nFindFirst: %d FNext %d FClose %d",
		   atomic_read(&tcon->stats.cifs_stats.num_ffirst),
		   atomic_read(&tcon->stats.cifs_stats.num_fnext),
		   atomic_read(&tcon->stats.cifs_stats.num_fclose));
#endif
}

static void
cifs_mkdir_setinfo(struct inode *inode, const char *full_path,
		   struct cifs_sb_info *cifs_sb, struct cifs_tcon *tcon,
		   const unsigned int xid)
{
	FILE_BASIC_INFO info;
	struct cifsInodeInfo *cifsInode;
	u32 dosattrs;
	int rc;

	memset(&info, 0, sizeof(info));
	cifsInode = CIFS_I(inode);
	dosattrs = cifsInode->cifsAttrs|ATTR_READONLY;
	info.Attributes = cpu_to_le32(dosattrs);
	rc = CIFSSMBSetPathInfo(xid, tcon, full_path, &info, cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc == 0)
		cifsInode->cifsAttrs = dosattrs;
}

static int
cifs_open_file(const unsigned int xid, struct cifs_tcon *tcon, const char *path,
	       int disposition, int desired_access, int create_options,
	       struct cifs_fid *fid, __u32 *oplock, FILE_ALL_INFO *buf,
	       struct cifs_sb_info *cifs_sb)
{
	if (!(tcon->ses->capabilities & CAP_NT_SMBS))
		return SMBLegacyOpen(xid, tcon, path, disposition,
				     desired_access, create_options,
				     &fid->netfid, oplock, buf,
				     cifs_sb->local_nls, cifs_sb->mnt_cifs_flags
						& CIFS_MOUNT_MAP_SPECIAL_CHR);
	return CIFSSMBOpen(xid, tcon, path, disposition, desired_access,
			   create_options, &fid->netfid, oplock, buf,
			   cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
}

static void
cifs_set_fid(struct cifsFileInfo *cfile, struct cifs_fid *fid, __u32 oplock)
{
	struct cifsInodeInfo *cinode = CIFS_I(cfile->dentry->d_inode);
	cfile->fid.netfid = fid->netfid;
	cifs_set_oplock_level(cinode, oplock);
	cinode->can_cache_brlcks = cinode->clientCanCacheAll;
}

static void
cifs_close_file(const unsigned int xid, struct cifs_tcon *tcon,
		struct cifs_fid *fid)
{
	CIFSSMBClose(xid, tcon, fid->netfid);
}

static int
cifs_flush_file(const unsigned int xid, struct cifs_tcon *tcon,
		struct cifs_fid *fid)
{
	return CIFSSMBFlush(xid, tcon, fid->netfid);
}

static int
cifs_sync_read(const unsigned int xid, struct cifsFileInfo *cfile,
	       struct cifs_io_parms *parms, unsigned int *bytes_read,
	       char **buf, int *buf_type)
{
	parms->netfid = cfile->fid.netfid;
	return CIFSSMBRead(xid, parms, bytes_read, buf, buf_type);
}

static int
cifs_sync_write(const unsigned int xid, struct cifsFileInfo *cfile,
		struct cifs_io_parms *parms, unsigned int *written,
		struct kvec *iov, unsigned long nr_segs)
{

	parms->netfid = cfile->fid.netfid;
	return CIFSSMBWrite2(xid, parms, written, iov, nr_segs);
}

static int
smb_set_file_info(struct inode *inode, const char *full_path,
		  FILE_BASIC_INFO *buf, const unsigned int xid)
{
	int oplock = 0;
	int rc;
	__u16 netfid;
	__u32 netpid;
	struct cifsFileInfo *open_file;
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink = NULL;
	struct cifs_tcon *tcon;

	/* if the file is already open for write, just use that fileid */
	open_file = find_writable_file(cinode, true);
	if (open_file) {
		netfid = open_file->fid.netfid;
		netpid = open_file->pid;
		tcon = tlink_tcon(open_file->tlink);
		goto set_via_filehandle;
	}

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		rc = PTR_ERR(tlink);
		tlink = NULL;
		goto out;
	}
	tcon = tlink_tcon(tlink);

	/*
	 * NT4 apparently returns success on this call, but it doesn't really
	 * work.
	 */
	if (!(tcon->ses->flags & CIFS_SES_NT4)) {
		rc = CIFSSMBSetPathInfo(xid, tcon, full_path, buf,
					cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc == 0) {
			cinode->cifsAttrs = le32_to_cpu(buf->Attributes);
			goto out;
		} else if (rc != -EOPNOTSUPP && rc != -EINVAL)
			goto out;
	}

	cFYI(1, "calling SetFileInfo since SetPathInfo for times not supported "
		"by this server");
	rc = CIFSSMBOpen(xid, tcon, full_path, FILE_OPEN,
			 SYNCHRONIZE | FILE_WRITE_ATTRIBUTES, CREATE_NOT_DIR,
			 &netfid, &oplock, NULL, cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (rc != 0) {
		if (rc == -EIO)
			rc = -EINVAL;
		goto out;
	}

	netpid = current->tgid;

set_via_filehandle:
	rc = CIFSSMBSetFileInfo(xid, tcon, buf, netfid, netpid);
	if (!rc)
		cinode->cifsAttrs = le32_to_cpu(buf->Attributes);

	if (open_file == NULL)
		CIFSSMBClose(xid, tcon, netfid);
	else
		cifsFileInfo_put(open_file);
out:
	if (tlink != NULL)
		cifs_put_tlink(tlink);
	return rc;
}

static int
cifs_query_dir_first(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *path, struct cifs_sb_info *cifs_sb,
		     struct cifs_fid *fid, __u16 search_flags,
		     struct cifs_search_info *srch_inf)
{
	return CIFSFindFirst(xid, tcon, path, cifs_sb,
			     &fid->netfid, search_flags, srch_inf, true);
}

static int
cifs_query_dir_next(const unsigned int xid, struct cifs_tcon *tcon,
		    struct cifs_fid *fid, __u16 search_flags,
		    struct cifs_search_info *srch_inf)
{
	return CIFSFindNext(xid, tcon, fid->netfid, search_flags, srch_inf);
}

static int
cifs_close_dir(const unsigned int xid, struct cifs_tcon *tcon,
	       struct cifs_fid *fid)
{
	return CIFSFindClose(xid, tcon, fid->netfid);
}

static int
cifs_oplock_response(struct cifs_tcon *tcon, struct cifs_fid *fid,
		     struct cifsInodeInfo *cinode)
{
	return CIFSSMBLock(0, tcon, fid->netfid, current->tgid, 0, 0, 0, 0,
			   LOCKING_ANDX_OPLOCK_RELEASE, false,
			   cinode->clientCanCacheRead ? 1 : 0);
}

static int
cifs_queryfs(const unsigned int xid, struct cifs_tcon *tcon,
	     struct kstatfs *buf)
{
	int rc = -EOPNOTSUPP;

	buf->f_type = CIFS_MAGIC_NUMBER;

	/*
	 * We could add a second check for a QFS Unix capability bit
	 */
	if ((tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_POSIX_EXTENSIONS & le64_to_cpu(tcon->fsUnixInfo.Capability)))
		rc = CIFSSMBQFSPosixInfo(xid, tcon, buf);

	/*
	 * Only need to call the old QFSInfo if failed on newer one,
	 * e.g. by OS/2.
	 **/
	if (rc && (tcon->ses->capabilities & CAP_NT_SMBS))
		rc = CIFSSMBQFSInfo(xid, tcon, buf);

	/*
	 * Some old Windows servers also do not support level 103, retry with
	 * older level one if old server failed the previous call or we
	 * bypassed it because we detected that this was an older LANMAN sess
	 */
	if (rc)
		rc = SMBOldQFSInfo(xid, tcon, buf);
	return rc;
}

static int
cifs_mand_lock(const unsigned int xid, struct cifsFileInfo *cfile, __u64 offset,
	       __u64 length, __u32 type, int lock, int unlock, bool wait)
{
	return CIFSSMBLock(xid, tlink_tcon(cfile->tlink), cfile->fid.netfid,
			   current->tgid, length, offset, unlock, lock,
			   (__u8)type, wait, 0);
}

struct smb_version_operations smb1_operations = {
	.send_cancel = send_nt_cancel,
	.compare_fids = cifs_compare_fids,
	.setup_request = cifs_setup_request,
	.setup_async_request = cifs_setup_async_request,
	.check_receive = cifs_check_receive,
	.add_credits = cifs_add_credits,
	.set_credits = cifs_set_credits,
	.get_credits_field = cifs_get_credits_field,
	.get_credits = cifs_get_credits,
	.get_next_mid = cifs_get_next_mid,
	.read_data_offset = cifs_read_data_offset,
	.read_data_length = cifs_read_data_length,
	.map_error = map_smb_to_linux_error,
	.find_mid = cifs_find_mid,
	.check_message = checkSMB,
	.dump_detail = cifs_dump_detail,
	.clear_stats = cifs_clear_stats,
	.print_stats = cifs_print_stats,
	.is_oplock_break = is_valid_oplock_break,
	.check_trans2 = cifs_check_trans2,
	.need_neg = cifs_need_neg,
	.negotiate = cifs_negotiate,
	.negotiate_wsize = cifs_negotiate_wsize,
	.negotiate_rsize = cifs_negotiate_rsize,
	.sess_setup = CIFS_SessSetup,
	.logoff = CIFSSMBLogoff,
	.tree_connect = CIFSTCon,
	.tree_disconnect = CIFSSMBTDis,
	.get_dfs_refer = CIFSGetDFSRefer,
	.qfs_tcon = cifs_qfs_tcon,
	.is_path_accessible = cifs_is_path_accessible,
	.query_path_info = cifs_query_path_info,
	.query_file_info = cifs_query_file_info,
	.get_srv_inum = cifs_get_srv_inum,
	.set_path_size = CIFSSMBSetEOF,
	.set_file_size = CIFSSMBSetFileSize,
	.set_file_info = smb_set_file_info,
	.echo = CIFSSMBEcho,
	.mkdir = CIFSSMBMkDir,
	.mkdir_setinfo = cifs_mkdir_setinfo,
	.rmdir = CIFSSMBRmDir,
	.unlink = CIFSSMBDelFile,
	.rename_pending_delete = cifs_rename_pending_delete,
	.rename = CIFSSMBRename,
	.create_hardlink = CIFSCreateHardLink,
	.open = cifs_open_file,
	.set_fid = cifs_set_fid,
	.close = cifs_close_file,
	.flush = cifs_flush_file,
	.async_readv = cifs_async_readv,
	.async_writev = cifs_async_writev,
	.sync_read = cifs_sync_read,
	.sync_write = cifs_sync_write,
	.query_dir_first = cifs_query_dir_first,
	.query_dir_next = cifs_query_dir_next,
	.close_dir = cifs_close_dir,
	.calc_smb_size = smbCalcSize,
	.oplock_response = cifs_oplock_response,
	.queryfs = cifs_queryfs,
	.mand_lock = cifs_mand_lock,
	.mand_unlock_range = cifs_unlock_range,
	.push_mand_locks = cifs_push_mandatory_locks,
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
	.lock_cmd = cpu_to_le16(SMB_COM_LOCKING_ANDX),
	.cap_unix = CAP_UNIX,
	.cap_nt_find = CAP_NT_SMBS | CAP_NT_FIND,
	.cap_large_files = CAP_LARGE_FILES,
	.oplock_read = OPLOCK_READ,
};
