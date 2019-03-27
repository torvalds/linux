/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/endian.h>

#ifdef USE_MD5_HASH
#include <sys/md5.h>
#endif

#include <netsmb/smb.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_conn.h>

#include <fs/smbfs/smbfs.h>
#include <fs/smbfs/smbfs_node.h>
#include <fs/smbfs/smbfs_subr.h>

/*
 * Lack of inode numbers leads us to the problem of generating them.
 * Partially this problem can be solved by having a dir/file cache
 * with inode numbers generated from the incremented by one counter.
 * However this way will require too much kernel memory, gives all
 * sorts of locking and consistency problems, not to mentinon counter overflows.
 * So, I'm decided to use a hash function to generate pseudo random (and unique)
 * inode numbers.
 */
static long
smbfs_getino(struct smbnode *dnp, const char *name, int nmlen)
{
#ifdef USE_MD5_HASH
	MD5_CTX md5;
	u_int32_t state[4];
	long ino;
	int i;

	MD5Init(&md5);
	MD5Update(&md5, name, nmlen);
	MD5Final((u_char *)state, &md5);
	for (i = 0, ino = 0; i < 4; i++)
		ino += state[i];
	return dnp->n_ino + ino;
#endif
	u_int32_t ino;

	ino = dnp->n_ino + smbfs_hash(name, nmlen);
	if (ino <= 2)
		ino += 3;
	return ino;
}

static int
smbfs_smb_lockandx(struct smbnode *np, int op, u_int32_t pid, off_t start, off_t end,
	struct smb_cred *scred)
{
	struct smb_share *ssp = np->n_mount->sm_share;
	struct smb_rq *rqp;
	struct mbchain *mbp;
	u_char ltype = 0;
	int error;

	if (op == SMB_LOCK_SHARED)
		ltype |= SMB_LOCKING_ANDX_SHARED_LOCK;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_LOCKING_ANDX, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint8(mbp, 0xff);	/* secondary command */
	mb_put_uint8(mbp, 0);		/* MBZ */
	mb_put_uint16le(mbp, 0);
	mb_put_mem(mbp, (caddr_t)&np->n_fid, 2, MB_MSYSTEM);
	mb_put_uint8(mbp, ltype);	/* locktype */
	mb_put_uint8(mbp, 0);		/* oplocklevel - 0 seems is NO_OPLOCK */
	mb_put_uint32le(mbp, 0);	/* timeout - break immediately */
	mb_put_uint16le(mbp, op == SMB_LOCK_RELEASE ? 1 : 0);
	mb_put_uint16le(mbp, op == SMB_LOCK_RELEASE ? 0 : 1);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint16le(mbp, pid);
	mb_put_uint32le(mbp, start);
	mb_put_uint32le(mbp, end - start);
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	smb_rq_done(rqp);
	return error;
}

int
smbfs_smb_lock(struct smbnode *np, int op, caddr_t id,
	off_t start, off_t end,	struct smb_cred *scred)
{
	struct smb_share *ssp = np->n_mount->sm_share;

	if (SMB_DIALECT(SSTOVC(ssp)) < SMB_DIALECT_LANMAN1_0)
		/*
		 * TODO: use LOCK_BYTE_RANGE here.
		 */
		return EINVAL;
	else
		return smbfs_smb_lockandx(np, op, (uintptr_t)id, start, end, scred);
}

static int
smbfs_query_info_fs(struct smb_share *ssp, struct statfs *sbp,
	struct smb_cred *scred)
{
	struct smb_t2rq *t2p;
	struct mbchain *mbp;
	struct mdchain *mdp;
	uint32_t bsize, bpu;
	int64_t units, funits;
	int error;

	error = smb_t2_alloc(SSTOCP(ssp), SMB_TRANS2_QUERY_FS_INFORMATION,
	    scred, &t2p);
	if (error)
		return (error);
	mbp = &t2p->t2_tparam;
	mb_init(mbp);
	mb_put_uint16le(mbp, SMB_QUERY_FS_SIZE_INFO);
	t2p->t2_maxpcount = 2;
	t2p->t2_maxdcount = sizeof(int64_t) * 2 + sizeof(uint32_t) * 2;
	error = smb_t2_request(t2p);
	if (error) {
		smb_t2_done(t2p);
		return (error);
	}
	mdp = &t2p->t2_rdata;
	md_get_int64le(mdp, &units);
	md_get_int64le(mdp, &funits);
	md_get_uint32le(mdp, &bpu);
	md_get_uint32le(mdp, &bsize);
	sbp->f_bsize = bpu * bsize;	/* fundamental filesystem block size */
	sbp->f_blocks= (uint64_t)units;	/* total data blocks in filesystem */
	sbp->f_bfree = (uint64_t)funits;/* free blocks in fs */
	sbp->f_bavail= (uint64_t)funits;/* free blocks avail to non-superuser */
	sbp->f_files = 0xffff;		/* total file nodes in filesystem */
	sbp->f_ffree = 0xffff;		/* free file nodes in fs */
	smb_t2_done(t2p);
	return (0);
}


static int
smbfs_query_info_alloc(struct smb_share *ssp, struct statfs *sbp,
	struct smb_cred *scred)
{
	struct smb_t2rq *t2p;
	struct mbchain *mbp;
	struct mdchain *mdp;
	u_int16_t bsize;
	u_int32_t units, bpu, funits;
	int error;

	error = smb_t2_alloc(SSTOCP(ssp), SMB_TRANS2_QUERY_FS_INFORMATION,
	    scred, &t2p);
	if (error)
		return error;
	mbp = &t2p->t2_tparam;
	mb_init(mbp);
	mb_put_uint16le(mbp, SMB_INFO_ALLOCATION);
	t2p->t2_maxpcount = 4;
	t2p->t2_maxdcount = 4 * 4 + 2;
	error = smb_t2_request(t2p);
	if (error) {
		smb_t2_done(t2p);
		return error;
	}
	mdp = &t2p->t2_rdata;
	md_get_uint32(mdp, NULL);	/* fs id */
	md_get_uint32le(mdp, &bpu);
	md_get_uint32le(mdp, &units);
	md_get_uint32le(mdp, &funits);
	md_get_uint16le(mdp, &bsize);
	sbp->f_bsize = bpu * bsize;	/* fundamental filesystem block size */
	sbp->f_blocks= units;		/* total data blocks in filesystem */
	sbp->f_bfree = funits;		/* free blocks in fs */
	sbp->f_bavail= funits;		/* free blocks avail to non-superuser */
	sbp->f_files = 0xffff;		/* total file nodes in filesystem */
	sbp->f_ffree = 0xffff;		/* free file nodes in fs */
	smb_t2_done(t2p);
	return 0;
}

static int
smbfs_query_info_disk(struct smb_share *ssp, struct statfs *sbp,
	struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct mdchain *mdp;
	u_int16_t units, bpu, bsize, funits;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_QUERY_INFORMATION_DISK,
	    scred, &rqp);
	if (error)
		return (error);
	smb_rq_wstart(rqp);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	if (error) {
		smb_rq_done(rqp);
		return error;
	}
	smb_rq_getreply(rqp, &mdp);
	md_get_uint16le(mdp, &units);
	md_get_uint16le(mdp, &bpu);
	md_get_uint16le(mdp, &bsize);
	md_get_uint16le(mdp, &funits);
	sbp->f_bsize = bpu * bsize;	/* fundamental filesystem block size */
	sbp->f_blocks= units;		/* total data blocks in filesystem */
	sbp->f_bfree = funits;		/* free blocks in fs */
	sbp->f_bavail= funits;		/* free blocks avail to non-superuser */
	sbp->f_files = 0xffff;		/* total file nodes in filesystem */
	sbp->f_ffree = 0xffff;		/* free file nodes in fs */
	smb_rq_done(rqp);
	return 0;
}

int
smbfs_smb_statfs(struct smb_share *ssp, struct statfs *sbp,
	struct smb_cred *scred)
{

	if (SMB_DIALECT(SSTOVC(ssp)) >= SMB_DIALECT_LANMAN2_0) {
		if (smbfs_query_info_fs(ssp, sbp, scred) == 0)
			return (0);
		if (smbfs_query_info_alloc(ssp, sbp, scred) == 0)
			return (0);
	}
	return (smbfs_query_info_disk(ssp, sbp, scred));
}

static int
smbfs_smb_seteof(struct smbnode *np, int64_t newsize, struct smb_cred *scred)
{
	struct smb_t2rq *t2p;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	int error;

	error = smb_t2_alloc(SSTOCP(ssp), SMB_TRANS2_SET_FILE_INFORMATION,
	    scred, &t2p);
	if (error)
		return error;
	mbp = &t2p->t2_tparam;
	mb_init(mbp);
	mb_put_mem(mbp, (caddr_t)&np->n_fid, 2, MB_MSYSTEM);
	mb_put_uint16le(mbp, SMB_SET_FILE_END_OF_FILE_INFO);
	mb_put_uint32le(mbp, 0);
	mbp = &t2p->t2_tdata;
	mb_init(mbp);
	mb_put_int64le(mbp, newsize);
	mb_put_uint32le(mbp, 0);			/* padding */
	mb_put_uint16le(mbp, 0);
	t2p->t2_maxpcount = 2;
	t2p->t2_maxdcount = 0;
	error = smb_t2_request(t2p);
	smb_t2_done(t2p);
	return error;
}

static int
smb_smb_flush(struct smbnode *np, struct smb_cred *scred)
{
	struct smb_share *ssp = np->n_mount->sm_share;
	struct smb_rq *rqp;
	struct mbchain *mbp;
	int error;

	if ((np->n_flag & NOPEN) == 0 || !SMBTOV(np) ||
	    SMBTOV(np)->v_type != VREG)
		return 0; /* not a regular open file */
	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_FLUSH, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_mem(mbp, (caddr_t)&np->n_fid, 2, MB_MSYSTEM);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	smb_rq_done(rqp);
	if (!error)
		np->n_flag &= ~NFLUSHWIRE;
	return (error);
}

int
smbfs_smb_flush(struct smbnode *np, struct smb_cred *scred)
{
	if (np->n_flag & NFLUSHWIRE)
		return (smb_smb_flush(np, scred));
	return (0);
}

int
smbfs_smb_setfsize(struct smbnode *np, int64_t newsize, struct smb_cred *scred)
{
	struct smb_share *ssp = np->n_mount->sm_share;
	struct smb_rq *rqp;
	struct mbchain *mbp;
	int error;

	if (!smbfs_smb_seteof(np, newsize, scred)) {
		np->n_flag |= NFLUSHWIRE;
		return (0);
	}
	/* XXX: We should use SMB_COM_WRITE_ANDX to support large offsets */
	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_WRITE, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_mem(mbp, (caddr_t)&np->n_fid, 2, MB_MSYSTEM);
	mb_put_uint16le(mbp, 0);
	mb_put_uint32le(mbp, (uint32_t)newsize);
	mb_put_uint16le(mbp, 0);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_DATA);
	mb_put_uint16le(mbp, 0);
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	smb_rq_done(rqp);
	return error;
}

int
smbfs_smb_query_info(struct smbnode *np, const char *name, int len,
		     struct smbfattr *fap, struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	struct mdchain *mdp;
	u_int8_t wc;
	int error;
	u_int16_t wattr;
	u_int32_t lint;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_QUERY_INFORMATION, scred,
	    &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	do {
		error = smbfs_fullpath(mbp, SSTOVC(ssp), np, name, len);
		if (error)
			break;
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
		if (error)
			break;
		smb_rq_getreply(rqp, &mdp);
		if (md_get_uint8(mdp, &wc) != 0 || wc != 10) {
			error = EBADRPC;
			break;
		}
		md_get_uint16le(mdp, &wattr);
		fap->fa_attr = wattr;
		/*
		 * Be careful using the time returned here, as
		 * with FAT on NT4SP6, at least, the time returned is low
		 * 32 bits of 100s of nanoseconds (since 1601) so it rolls
		 * over about every seven minutes!
		 */
		md_get_uint32le(mdp, &lint); /* specs: secs since 1970 */
		if (lint)	/* avoid bogus zero returns */
			smb_time_server2local(lint, SSTOVC(ssp)->vc_sopt.sv_tz,
					      &fap->fa_mtime);
		md_get_uint32le(mdp, &lint);
		fap->fa_size = lint;
	} while(0);
	smb_rq_done(rqp);
	return error;
}

/*
 * Set DOS file attributes. mtime should be NULL for dialects above lm10
 */
int
smbfs_smb_setpattr(struct smbnode *np, u_int16_t attr, struct timespec *mtime,
	struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	u_long time;
	int error, svtz;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_SET_INFORMATION, scred,
	    &rqp);
	if (error)
		return (error);
	svtz = SSTOVC(ssp)->vc_sopt.sv_tz;
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, attr);
	if (mtime) {
		smb_time_local2server(mtime, svtz, &time);
	} else
		time = 0;
	mb_put_uint32le(mbp, time);		/* mtime */
	mb_put_mem(mbp, NULL, 5 * 2, MB_MZERO);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	do {
		error = smbfs_fullpath(mbp, SSTOVC(ssp), np, NULL, 0);
		if (error)
			break;
		mb_put_uint8(mbp, SMB_DT_ASCII);
		if (SMB_UNICODE_STRINGS(SSTOVC(ssp))) {
			mb_put_padbyte(mbp);
			mb_put_uint8(mbp, 0);	/* 1st byte of NULL Unicode char */
		}
		mb_put_uint8(mbp, 0);
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
		if (error) {
			SMBERROR("smb_rq_simple(rqp) => error %d\n", error);
			break;
		}
	} while(0);
	smb_rq_done(rqp);
	return error;
}

/*
 * Note, win95 doesn't support this call.
 */
int
smbfs_smb_setptime2(struct smbnode *np, struct timespec *mtime,
	struct timespec *atime, int attr, struct smb_cred *scred)
{
	struct smb_t2rq *t2p;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct smb_vc *vcp = SSTOVC(ssp);
	struct mbchain *mbp;
	u_int16_t date, time;
	int error, tzoff;

	error = smb_t2_alloc(SSTOCP(ssp), SMB_TRANS2_SET_PATH_INFORMATION,
	    scred, &t2p);
	if (error)
		return error;
	mbp = &t2p->t2_tparam;
	mb_init(mbp);
	mb_put_uint16le(mbp, SMB_INFO_STANDARD);
	mb_put_uint32le(mbp, 0);		/* MBZ */
	/* mb_put_uint8(mbp, SMB_DT_ASCII); specs incorrect */
	error = smbfs_fullpath(mbp, vcp, np, NULL, 0);
	if (error) {
		smb_t2_done(t2p);
		return error;
	}
	tzoff = vcp->vc_sopt.sv_tz;
	mbp = &t2p->t2_tdata;
	mb_init(mbp);
	mb_put_uint32le(mbp, 0);		/* creation time */
	if (atime)
		smb_time_unix2dos(atime, tzoff, &date, &time, NULL);
	else
		time = date = 0;
	mb_put_uint16le(mbp, date);
	mb_put_uint16le(mbp, time);
	if (mtime)
		smb_time_unix2dos(mtime, tzoff, &date, &time, NULL);
	else
		time = date = 0;
	mb_put_uint16le(mbp, date);
	mb_put_uint16le(mbp, time);
	mb_put_uint32le(mbp, 0);		/* file size */
	mb_put_uint32le(mbp, 0);		/* allocation unit size */
	mb_put_uint16le(mbp, attr);	/* DOS attr */
	mb_put_uint32le(mbp, 0);		/* EA size */
	t2p->t2_maxpcount = 5 * 2;
	t2p->t2_maxdcount = vcp->vc_txmax;
	error = smb_t2_request(t2p);
	smb_t2_done(t2p);
	return error;
}

/*
 * NT level. Specially for win9x
 */
int
smbfs_smb_setpattrNT(struct smbnode *np, u_short attr, struct timespec *mtime,
	struct timespec *atime, struct smb_cred *scred)
{
	struct smb_t2rq *t2p;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct smb_vc *vcp = SSTOVC(ssp);
	struct mbchain *mbp;
	int64_t tm;
	int error, tzoff;

	error = smb_t2_alloc(SSTOCP(ssp), SMB_TRANS2_SET_PATH_INFORMATION,
	    scred, &t2p);
	if (error)
		return error;
	mbp = &t2p->t2_tparam;
	mb_init(mbp);
	mb_put_uint16le(mbp, SMB_SET_FILE_BASIC_INFO);
	mb_put_uint32le(mbp, 0);		/* MBZ */
	/* mb_put_uint8(mbp, SMB_DT_ASCII); specs incorrect */
	error = smbfs_fullpath(mbp, vcp, np, NULL, 0);
	if (error) {
		smb_t2_done(t2p);
		return error;
	}
	tzoff = vcp->vc_sopt.sv_tz;
	mbp = &t2p->t2_tdata;
	mb_init(mbp);
	mb_put_int64le(mbp, 0);		/* creation time */
	if (atime) {
		smb_time_local2NT(atime, tzoff, &tm);
	} else
		tm = 0;
	mb_put_int64le(mbp, tm);
	if (mtime) {
		smb_time_local2NT(mtime, tzoff, &tm);
	} else
		tm = 0;
	mb_put_int64le(mbp, tm);
	mb_put_int64le(mbp, tm);		/* change time */
	mb_put_uint32le(mbp, attr);		/* attr */
	t2p->t2_maxpcount = 24;
	t2p->t2_maxdcount = 56;
	error = smb_t2_request(t2p);
	smb_t2_done(t2p);
	return error;
}

/*
 * Set file atime and mtime. Doesn't supported by core dialect.
 */
int
smbfs_smb_setftime(struct smbnode *np, struct timespec *mtime,
	struct timespec *atime, struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	u_int16_t date, time;
	int error, tzoff;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_SET_INFORMATION2, scred,
	    &rqp);
	if (error)
		return (error);
	tzoff = SSTOVC(ssp)->vc_sopt.sv_tz;
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_mem(mbp, (caddr_t)&np->n_fid, 2, MB_MSYSTEM);
	mb_put_uint32le(mbp, 0);		/* creation time */

	if (atime)
		smb_time_unix2dos(atime, tzoff, &date, &time, NULL);
	else
		time = date = 0;
	mb_put_uint16le(mbp, date);
	mb_put_uint16le(mbp, time);
	if (mtime)
		smb_time_unix2dos(mtime, tzoff, &date, &time, NULL);
	else
		time = date = 0;
	mb_put_uint16le(mbp, date);
	mb_put_uint16le(mbp, time);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	SMBSDEBUG("%d\n", error);
	smb_rq_done(rqp);
	return error;
}

/*
 * Set DOS file attributes.
 * Looks like this call can be used only if SMB_CAP_NT_SMBS bit is on.
 */
int
smbfs_smb_setfattrNT(struct smbnode *np, u_int16_t attr, struct timespec *mtime,
	struct timespec *atime, struct smb_cred *scred)
{
	struct smb_t2rq *t2p;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	int64_t tm;
	int error, svtz;

	error = smb_t2_alloc(SSTOCP(ssp), SMB_TRANS2_SET_FILE_INFORMATION,
	    scred, &t2p);
	if (error)
		return error;
	svtz = SSTOVC(ssp)->vc_sopt.sv_tz;
	mbp = &t2p->t2_tparam;
	mb_init(mbp);
	mb_put_mem(mbp, (caddr_t)&np->n_fid, 2, MB_MSYSTEM);
	mb_put_uint16le(mbp, SMB_SET_FILE_BASIC_INFO);
	mb_put_uint32le(mbp, 0);
	mbp = &t2p->t2_tdata;
	mb_init(mbp);
	mb_put_int64le(mbp, 0);		/* creation time */
	if (atime) {
		smb_time_local2NT(atime, svtz, &tm);
	} else
		tm = 0;
	mb_put_int64le(mbp, tm);
	if (mtime) {
		smb_time_local2NT(mtime, svtz, &tm);
	} else
		tm = 0;
	mb_put_int64le(mbp, tm);
	mb_put_int64le(mbp, tm);		/* change time */
	mb_put_uint16le(mbp, attr);
	mb_put_uint32le(mbp, 0);			/* padding */
	mb_put_uint16le(mbp, 0);
	t2p->t2_maxpcount = 2;
	t2p->t2_maxdcount = 0;
	error = smb_t2_request(t2p);
	smb_t2_done(t2p);
	return error;
}


int
smbfs_smb_open(struct smbnode *np, int accmode, struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	struct mdchain *mdp;
	u_int8_t wc;
	u_int16_t fid, wattr, grantedmode;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_OPEN, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, accmode);
	mb_put_uint16le(mbp, SMB_FA_SYSTEM | SMB_FA_HIDDEN);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	do {
		error = smbfs_fullpath(mbp, SSTOVC(ssp), np, NULL, 0);
		if (error)
			break;
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
		if (error)
			break;
		smb_rq_getreply(rqp, &mdp);
		if (md_get_uint8(mdp, &wc) != 0 || wc != 7) {
			error = EBADRPC;
			break;
		}
		md_get_uint16(mdp, &fid);
		md_get_uint16le(mdp, &wattr);
		md_get_uint32(mdp, NULL);	/* mtime */
		md_get_uint32(mdp, NULL);	/* fsize */
		md_get_uint16le(mdp, &grantedmode);
		/*
		 * TODO: refresh attributes from this reply
		 */
	} while(0);
	smb_rq_done(rqp);
	if (error)
		return error;
	np->n_fid = fid;
	np->n_rwstate = grantedmode;
	return 0;
}


int
smbfs_smb_close(struct smb_share *ssp, u_int16_t fid, struct timespec *mtime,
	struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct mbchain *mbp;
	u_long time;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_CLOSE, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_mem(mbp, (caddr_t)&fid, sizeof(fid), MB_MSYSTEM);
	if (mtime) {
		smb_time_local2server(mtime, SSTOVC(ssp)->vc_sopt.sv_tz, &time);
	} else
		time = 0;
	mb_put_uint32le(mbp, time);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	smb_rq_done(rqp);
	return error;
}

int
smbfs_smb_create(struct smbnode *dnp, const char *name, int nmlen,
	struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = dnp->n_mount->sm_share;
	struct mbchain *mbp;
	struct mdchain *mdp;
	struct timespec ctime;
	u_int8_t wc;
	u_int16_t fid;
	u_long tm;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_CREATE, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, SMB_FA_ARCHIVE);		/* attributes  */
	nanotime(&ctime);
	smb_time_local2server(&ctime, SSTOVC(ssp)->vc_sopt.sv_tz, &tm);
	mb_put_uint32le(mbp, tm);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	error = smbfs_fullpath(mbp, SSTOVC(ssp), dnp, name, nmlen);
	if (!error) {
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
		if (!error) {
			smb_rq_getreply(rqp, &mdp);
			md_get_uint8(mdp, &wc);
			if (wc == 1)
				md_get_uint16(mdp, &fid);
			else
				error = EBADRPC;
		}
	}
	smb_rq_done(rqp);
	if (error)
		return error;
	smbfs_smb_close(ssp, fid, &ctime, scred);
	return error;
}

int
smbfs_smb_delete(struct smbnode *np, struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_DELETE, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, SMB_FA_SYSTEM | SMB_FA_HIDDEN);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	error = smbfs_fullpath(mbp, SSTOVC(ssp), np, NULL, 0);
	if (!error) {
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
	}
	smb_rq_done(rqp);
	return error;
}

int
smbfs_smb_rename(struct smbnode *src, struct smbnode *tdnp,
	const char *tname, int tnmlen, struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = src->n_mount->sm_share;
	struct mbchain *mbp;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_RENAME, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, SMB_FA_SYSTEM | SMB_FA_HIDDEN);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	do {
		error = smbfs_fullpath(mbp, SSTOVC(ssp), src, NULL, 0);
		if (error)
			break;
		mb_put_uint8(mbp, SMB_DT_ASCII);
		error = smbfs_fullpath(mbp, SSTOVC(ssp), tdnp, tname, tnmlen);
		if (error)
			break;
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
	} while(0);
	smb_rq_done(rqp);
	return error;
}

int
smbfs_smb_move(struct smbnode *src, struct smbnode *tdnp,
	const char *tname, int tnmlen, u_int16_t flags, struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = src->n_mount->sm_share;
	struct mbchain *mbp;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_MOVE, scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, SMB_TID_UNKNOWN);
	mb_put_uint16le(mbp, 0x20);	/* delete target file */
	mb_put_uint16le(mbp, flags);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	do {
		error = smbfs_fullpath(mbp, SSTOVC(ssp), src, NULL, 0);
		if (error)
			break;
		mb_put_uint8(mbp, SMB_DT_ASCII);
		error = smbfs_fullpath(mbp, SSTOVC(ssp), tdnp, tname, tnmlen);
		if (error)
			break;
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
	} while(0);
	smb_rq_done(rqp);
	return error;
}

int
smbfs_smb_mkdir(struct smbnode *dnp, const char *name, int len,
	struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = dnp->n_mount->sm_share;
	struct mbchain *mbp;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_CREATE_DIRECTORY, scred,
	    &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	error = smbfs_fullpath(mbp, SSTOVC(ssp), dnp, name, len);
	if (!error) {
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
	}
	smb_rq_done(rqp);
	return error;
}

int
smbfs_smb_rmdir(struct smbnode *np, struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct mbchain *mbp;
	int error;

	error = smb_rq_alloc(SSTOCP(ssp), SMB_COM_DELETE_DIRECTORY, scred,
	    &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	error = smbfs_fullpath(mbp, SSTOVC(ssp), np, NULL, 0);
	if (!error) {
		smb_rq_bend(rqp);
		error = smb_rq_simple(rqp);
	}
	smb_rq_done(rqp);
	return error;
}

static int
smbfs_smb_search(struct smbfs_fctx *ctx)
{
	struct smb_vc *vcp = SSTOVC(ctx->f_ssp);
	struct smb_rq *rqp;
	struct mbchain *mbp;
	struct mdchain *mdp;
	u_int8_t wc, bt;
	u_int16_t ec, dlen, bc;
	int maxent, error, iseof = 0;

	maxent = min(ctx->f_left, (vcp->vc_txmax - SMB_HDRLEN - 3) / SMB_DENTRYLEN);
	if (ctx->f_rq) {
		smb_rq_done(ctx->f_rq);
		ctx->f_rq = NULL;
	}
	error = smb_rq_alloc(SSTOCP(ctx->f_ssp), SMB_COM_SEARCH, ctx->f_scred, &rqp);
	if (error)
		return (error);
	ctx->f_rq = rqp;
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, maxent);	/* max entries to return */
	mb_put_uint16le(mbp, ctx->f_attrmask);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);	/* buffer format */
	if (ctx->f_flags & SMBFS_RDD_FINDFIRST) {
		error = smbfs_fullpath(mbp, vcp, ctx->f_dnp, ctx->f_wildcard, ctx->f_wclen);
		if (error)
			return error;
		mb_put_uint8(mbp, SMB_DT_VARIABLE);
		mb_put_uint16le(mbp, 0);	/* context length */
		ctx->f_flags &= ~SMBFS_RDD_FINDFIRST;
	} else {
		if (SMB_UNICODE_STRINGS(vcp)) {
			mb_put_padbyte(mbp);
			mb_put_uint8(mbp, 0);
		}
		mb_put_uint8(mbp, 0);	/* file name length */
		mb_put_uint8(mbp, SMB_DT_VARIABLE);
		mb_put_uint16le(mbp, SMB_SKEYLEN);
		mb_put_mem(mbp, ctx->f_skey, SMB_SKEYLEN, MB_MSYSTEM);
	}
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	if (error) {
		if (rqp->sr_errclass == ERRDOS && rqp->sr_serror == ERRnofiles) {
			error = 0;
			iseof = 1;
			ctx->f_flags |= SMBFS_RDD_EOF;
		} else
			return error;
	}
	smb_rq_getreply(rqp, &mdp);
	md_get_uint8(mdp, &wc);
	if (wc != 1) 
		return iseof ? ENOENT : EBADRPC;
	md_get_uint16le(mdp, &ec);
	if (ec == 0)
		return ENOENT;
	ctx->f_ecnt = ec;
	md_get_uint16le(mdp, &bc);
	if (bc < 3)
		return EBADRPC;
	bc -= 3;
	md_get_uint8(mdp, &bt);
	if (bt != SMB_DT_VARIABLE)
		return EBADRPC;
	md_get_uint16le(mdp, &dlen);
	if (dlen != bc || dlen % SMB_DENTRYLEN != 0)
		return EBADRPC;
	return 0;
}

static int
smbfs_findopenLM1(struct smbfs_fctx *ctx, struct smbnode *dnp,
	const char *wildcard, int wclen, int attr, struct smb_cred *scred)
{
	ctx->f_attrmask = attr;
	if (wildcard) {
		if (wclen == 1 && wildcard[0] == '*') {
			ctx->f_wildcard = "*.*";
			ctx->f_wclen = 3;
		} else {
			ctx->f_wildcard = wildcard;
			ctx->f_wclen = wclen;
		}
	} else {
		ctx->f_wildcard = NULL;
		ctx->f_wclen = 0;
	}
	ctx->f_name = ctx->f_fname;
	return 0;
}

static int
smbfs_findnextLM1(struct smbfs_fctx *ctx, int limit)
{
	struct mdchain *mbp;
	struct smb_rq *rqp;
	char *cp;
	u_int8_t battr;
	u_int16_t date, time;
	u_int32_t size;
	int error;

	if (ctx->f_ecnt == 0) {
		if (ctx->f_flags & SMBFS_RDD_EOF)
			return ENOENT;
		ctx->f_left = ctx->f_limit = limit;
		error = smbfs_smb_search(ctx);
		if (error)
			return error;
	}
	rqp = ctx->f_rq;
	smb_rq_getreply(rqp, &mbp);
	md_get_mem(mbp, ctx->f_skey, SMB_SKEYLEN, MB_MSYSTEM);
	md_get_uint8(mbp, &battr);
	md_get_uint16le(mbp, &time);
	md_get_uint16le(mbp, &date);
	md_get_uint32le(mbp, &size);
	cp = ctx->f_name;
	md_get_mem(mbp, cp, sizeof(ctx->f_fname), MB_MSYSTEM);
	cp[sizeof(ctx->f_fname) - 1] = 0;
	cp += strlen(cp) - 1;
	while (*cp == ' ' && cp >= ctx->f_name)
		*cp-- = 0;
	ctx->f_attr.fa_attr = battr;
	smb_dos2unixtime(date, time, 0, rqp->sr_vc->vc_sopt.sv_tz,
	    &ctx->f_attr.fa_mtime);
	ctx->f_attr.fa_size = size;
	ctx->f_nmlen = strlen(ctx->f_name);
	ctx->f_ecnt--;
	ctx->f_left--;
	return 0;
}

static int
smbfs_findcloseLM1(struct smbfs_fctx *ctx)
{
	if (ctx->f_rq)
		smb_rq_done(ctx->f_rq);
	return 0;
}

/*
 * TRANS2_FIND_FIRST2/NEXT2, used for NT LM12 dialect
 */
static int
smbfs_smb_trans2find2(struct smbfs_fctx *ctx)
{
	struct smb_t2rq *t2p;
	struct smb_vc *vcp = SSTOVC(ctx->f_ssp);
	struct mbchain *mbp;
	struct mdchain *mdp;
	u_int16_t tw, flags;
	int error;

	if (ctx->f_t2) {
		smb_t2_done(ctx->f_t2);
		ctx->f_t2 = NULL;
	}
	ctx->f_flags &= ~SMBFS_RDD_GOTRNAME;
	flags = 8 | 2;			/* <resume> | <close if EOS> */
	if (ctx->f_flags & SMBFS_RDD_FINDSINGLE) {
		flags |= 1;		/* close search after this request */
		ctx->f_flags |= SMBFS_RDD_NOCLOSE;
	}
	if (ctx->f_flags & SMBFS_RDD_FINDFIRST) {
		error = smb_t2_alloc(SSTOCP(ctx->f_ssp), SMB_TRANS2_FIND_FIRST2,
		    ctx->f_scred, &t2p);
		if (error)
			return error;
		ctx->f_t2 = t2p;
		mbp = &t2p->t2_tparam;
		mb_init(mbp);
		mb_put_uint16le(mbp, ctx->f_attrmask);
		mb_put_uint16le(mbp, ctx->f_limit);
		mb_put_uint16le(mbp, flags);
		mb_put_uint16le(mbp, ctx->f_infolevel);
		mb_put_uint32le(mbp, 0);
		error = smbfs_fullpath(mbp, vcp, ctx->f_dnp, ctx->f_wildcard, ctx->f_wclen);
		if (error)
			return error;
	} else	{
		error = smb_t2_alloc(SSTOCP(ctx->f_ssp), SMB_TRANS2_FIND_NEXT2,
		    ctx->f_scred, &t2p);
		if (error)
			return error;
		ctx->f_t2 = t2p;
		mbp = &t2p->t2_tparam;
		mb_init(mbp);
		mb_put_mem(mbp, (caddr_t)&ctx->f_Sid, 2, MB_MSYSTEM);
		mb_put_uint16le(mbp, ctx->f_limit);
		mb_put_uint16le(mbp, ctx->f_infolevel);
		mb_put_uint32le(mbp, 0);		/* resume key */
		mb_put_uint16le(mbp, flags);
		if (ctx->f_rname)
			mb_put_mem(mbp, ctx->f_rname, ctx->f_rnamelen + 1, MB_MSYSTEM);
		else
			mb_put_uint8(mbp, 0);	/* resume file name */
#if 0
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 200 * 1000;	/* 200ms */
		if (vcp->vc_flags & SMBC_WIN95) {
			/*
			 * some implementations suggests to sleep here
			 * for 200ms, due to the bug in the Win95.
			 * I've didn't notice any problem, but put code
			 * for it.
			 */
			 pause("fix95", tvtohz(&tv));
		}
#endif
	}
	t2p->t2_maxpcount = 5 * 2;
	t2p->t2_maxdcount = vcp->vc_txmax;
	error = smb_t2_request(t2p);
	if (error)
		return error;
	mdp = &t2p->t2_rparam;
	if (ctx->f_flags & SMBFS_RDD_FINDFIRST) {
		if ((error = md_get_uint16(mdp, &ctx->f_Sid)) != 0)
			return error;
		ctx->f_flags &= ~SMBFS_RDD_FINDFIRST;
	}
	if ((error = md_get_uint16le(mdp, &tw)) != 0)
		return error;
	ctx->f_ecnt = tw;
	if ((error = md_get_uint16le(mdp, &tw)) != 0)
		return error;
	if (tw)
		ctx->f_flags |= SMBFS_RDD_EOF | SMBFS_RDD_NOCLOSE;
	if ((error = md_get_uint16le(mdp, &tw)) != 0)
		return error;
	if ((error = md_get_uint16le(mdp, &tw)) != 0)
		return error;
	if (ctx->f_ecnt == 0) {
		ctx->f_flags |= SMBFS_RDD_EOF | SMBFS_RDD_NOCLOSE;
		return ENOENT;
	}
	ctx->f_rnameofs = tw;
	mdp = &t2p->t2_rdata;
	if (mdp->md_top == NULL) {
		printf("bug: ecnt = %d, but data is NULL (please report)\n", ctx->f_ecnt);
		return ENOENT;
	}
	if (mdp->md_top->m_len == 0) {
		printf("bug: ecnt = %d, but m_len = 0 and m_next = %p (please report)\n", ctx->f_ecnt,mbp->mb_top->m_next);
		return ENOENT;
	}
	ctx->f_eofs = 0;
	return 0;
}

static int
smbfs_smb_findclose2(struct smbfs_fctx *ctx)
{
	struct smb_rq *rqp;
	struct mbchain *mbp;
	int error;

	error = smb_rq_alloc(SSTOCP(ctx->f_ssp), SMB_COM_FIND_CLOSE2,
	    ctx->f_scred, &rqp);
	if (error)
		return (error);
	smb_rq_getrequest(rqp, &mbp);
	smb_rq_wstart(rqp);
	mb_put_mem(mbp, (caddr_t)&ctx->f_Sid, 2, MB_MSYSTEM);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	smb_rq_done(rqp);
	return error;
}

static int
smbfs_findopenLM2(struct smbfs_fctx *ctx, struct smbnode *dnp,
	const char *wildcard, int wclen, int attr, struct smb_cred *scred)
{
	if (SMB_UNICODE_STRINGS(SSTOVC(ctx->f_ssp))) {
		ctx->f_name = malloc(SMB_MAXFNAMELEN * 2, M_SMBFSDATA, M_WAITOK);
	} else
		ctx->f_name = malloc(SMB_MAXFNAMELEN, M_SMBFSDATA, M_WAITOK);
	ctx->f_infolevel = SMB_DIALECT(SSTOVC(ctx->f_ssp)) < SMB_DIALECT_NTLM0_12 ?
	    SMB_INFO_STANDARD : SMB_FIND_FILE_DIRECTORY_INFO;
	ctx->f_attrmask = attr;
	ctx->f_wildcard = wildcard;
	ctx->f_wclen = wclen;
	return 0;
}

static int
smbfs_findnextLM2(struct smbfs_fctx *ctx, int limit)
{
	struct mdchain *mbp;
	struct smb_t2rq *t2p;
	char *cp;
	u_int8_t tb;
	u_int16_t date, time, wattr;
	u_int32_t size, next, dattr;
	int64_t lint;
	int error, svtz, cnt, fxsz, nmlen, recsz;

	if (ctx->f_ecnt == 0) {
		if (ctx->f_flags & SMBFS_RDD_EOF)
			return ENOENT;
		ctx->f_left = ctx->f_limit = limit;
		error = smbfs_smb_trans2find2(ctx);
		if (error)
			return error;
	}
	t2p = ctx->f_t2;
	mbp = &t2p->t2_rdata;
	svtz = SSTOVC(ctx->f_ssp)->vc_sopt.sv_tz;
	switch (ctx->f_infolevel) {
	    case SMB_INFO_STANDARD:
		next = 0;
		fxsz = 0;
		md_get_uint16le(mbp, &date);
		md_get_uint16le(mbp, &time);	/* creation time */
		md_get_uint16le(mbp, &date);
		md_get_uint16le(mbp, &time);	/* access time */
		smb_dos2unixtime(date, time, 0, svtz, &ctx->f_attr.fa_atime);
		md_get_uint16le(mbp, &date);
		md_get_uint16le(mbp, &time);	/* access time */
		smb_dos2unixtime(date, time, 0, svtz, &ctx->f_attr.fa_mtime);
		md_get_uint32le(mbp, &size);
		ctx->f_attr.fa_size = size;
		md_get_uint32(mbp, NULL);	/* allocation size */
		md_get_uint16le(mbp, &wattr);
		ctx->f_attr.fa_attr = wattr;
		md_get_uint8(mbp, &tb);
		size = nmlen = tb;
		fxsz = 23;
		recsz = next = 24 + nmlen;	/* docs misses zero byte at end */
		break;
	    case SMB_FIND_FILE_DIRECTORY_INFO:
		md_get_uint32le(mbp, &next);
		md_get_uint32(mbp, NULL);	/* file index */
		md_get_int64(mbp, NULL);	/* creation time */
		md_get_int64le(mbp, &lint);
		smb_time_NT2local(lint, svtz, &ctx->f_attr.fa_atime);
		md_get_int64le(mbp, &lint);
		smb_time_NT2local(lint, svtz, &ctx->f_attr.fa_mtime);
		md_get_int64le(mbp, &lint);
		smb_time_NT2local(lint, svtz, &ctx->f_attr.fa_ctime);
		md_get_int64le(mbp, &lint);	/* file size */
		ctx->f_attr.fa_size = lint;
		md_get_int64(mbp, NULL);	/* real size (should use) */
		md_get_uint32le(mbp, &dattr);	/* EA */
		ctx->f_attr.fa_attr = dattr;
		md_get_uint32le(mbp, &size);	/* name len */
		fxsz = 64;
		recsz = next ? next : fxsz + size;
		break;
	    default:
		SMBERROR("unexpected info level %d\n", ctx->f_infolevel);
		return EINVAL;
	}
	if (SMB_UNICODE_STRINGS(SSTOVC(ctx->f_ssp))) {
		nmlen = min(size, SMB_MAXFNAMELEN * 2);
	} else
		nmlen = min(size, SMB_MAXFNAMELEN);
	cp = ctx->f_name;
	error = md_get_mem(mbp, cp, nmlen, MB_MSYSTEM);
	if (error)
		return error;
	if (next) {
		cnt = next - nmlen - fxsz;
		if (cnt > 0)
			md_get_mem(mbp, NULL, cnt, MB_MSYSTEM);
		else if (cnt < 0) {
			SMBERROR("out of sync\n");
			return EBADRPC;
		}
	}
	if (SMB_UNICODE_STRINGS(SSTOVC(ctx->f_ssp))) {
		if (nmlen > 1 && cp[nmlen - 1] == 0 && cp[nmlen - 2] == 0)
			nmlen -= 2;
	} else
		if (nmlen && cp[nmlen - 1] == 0)
			nmlen--;
	if (nmlen == 0)
		return EBADRPC;

	next = ctx->f_eofs + recsz;
	if (ctx->f_rnameofs && (ctx->f_flags & SMBFS_RDD_GOTRNAME) == 0 &&
	    (ctx->f_rnameofs >= ctx->f_eofs && ctx->f_rnameofs < next)) {
		/*
		 * Server needs a resume filename.
		 */
		if (ctx->f_rnamelen <= nmlen) {
			if (ctx->f_rname)
				free(ctx->f_rname, M_SMBFSDATA);
			ctx->f_rname = malloc(nmlen + 1, M_SMBFSDATA, M_WAITOK);
			ctx->f_rnamelen = nmlen;
		}
		bcopy(ctx->f_name, ctx->f_rname, nmlen);
		ctx->f_rname[nmlen] = 0;
		ctx->f_flags |= SMBFS_RDD_GOTRNAME;
	}
	ctx->f_nmlen = nmlen;
	ctx->f_eofs = next;
	ctx->f_ecnt--;
	ctx->f_left--;
	return 0;
}

static int
smbfs_findcloseLM2(struct smbfs_fctx *ctx)
{
	if (ctx->f_name)
		free(ctx->f_name, M_SMBFSDATA);
	if (ctx->f_t2)
		smb_t2_done(ctx->f_t2);
	if ((ctx->f_flags & SMBFS_RDD_NOCLOSE) == 0)
		smbfs_smb_findclose2(ctx);
	return 0;
}

int
smbfs_findopen(struct smbnode *dnp, const char *wildcard, int wclen, int attr,
	struct smb_cred *scred, struct smbfs_fctx **ctxpp)
{
	struct smbfs_fctx *ctx;
	int error;

	ctx = malloc(sizeof(*ctx), M_SMBFSDATA, M_WAITOK | M_ZERO);
	ctx->f_ssp = dnp->n_mount->sm_share;
	ctx->f_dnp = dnp;
	ctx->f_flags = SMBFS_RDD_FINDFIRST;
	ctx->f_scred = scred;
	if (SMB_DIALECT(SSTOVC(ctx->f_ssp)) < SMB_DIALECT_LANMAN2_0 ||
	    (dnp->n_mount->sm_flags & SMBFS_MOUNT_NO_LONG)) {
		ctx->f_flags |= SMBFS_RDD_USESEARCH;
		error = smbfs_findopenLM1(ctx, dnp, wildcard, wclen, attr, scred);
	} else
		error = smbfs_findopenLM2(ctx, dnp, wildcard, wclen, attr, scred);
	if (error)
		smbfs_findclose(ctx, scred);
	else
		*ctxpp = ctx;
	return error;
}

int
smbfs_findnext(struct smbfs_fctx *ctx, int limit, struct smb_cred *scred)
{
	int error;

	if (limit == 0)
		limit = 1000000;
	else if (limit > 1)
		limit *= 4;	/* imperical */
	ctx->f_scred = scred;
	for (;;) {
		if (ctx->f_flags & SMBFS_RDD_USESEARCH) {
			error = smbfs_findnextLM1(ctx, limit);
		} else
			error = smbfs_findnextLM2(ctx, limit);
		if (error)
			return error;
		if (SMB_UNICODE_STRINGS(SSTOVC(ctx->f_ssp))) {
			if ((ctx->f_nmlen == 2 &&
			     *(u_int16_t *)ctx->f_name == htole16(0x002e)) ||
			    (ctx->f_nmlen == 4 &&
			     *(u_int32_t *)ctx->f_name == htole32(0x002e002e)))
				continue;
		} else
			if ((ctx->f_nmlen == 1 && ctx->f_name[0] == '.') ||
			    (ctx->f_nmlen == 2 && ctx->f_name[0] == '.' &&
			     ctx->f_name[1] == '.'))
				continue;
		break;
	}
	smbfs_fname_tolocal(SSTOVC(ctx->f_ssp), ctx->f_name, &ctx->f_nmlen,
			    ctx->f_dnp->n_mount->sm_caseopt);
	ctx->f_attr.fa_ino = smbfs_getino(ctx->f_dnp, ctx->f_name, ctx->f_nmlen);
	return 0;
}

int
smbfs_findclose(struct smbfs_fctx *ctx, struct smb_cred *scred)
{
	ctx->f_scred = scred;
	if (ctx->f_flags & SMBFS_RDD_USESEARCH) {
		smbfs_findcloseLM1(ctx);
	} else
		smbfs_findcloseLM2(ctx);
	if (ctx->f_rname)
		free(ctx->f_rname, M_SMBFSDATA);
	free(ctx, M_SMBFSDATA);
	return 0;
}

int
smbfs_smb_lookup(struct smbnode *dnp, const char *name, int nmlen,
	struct smbfattr *fap, struct smb_cred *scred)
{
	struct smbfs_fctx *ctx;
	int error;

	if (dnp == NULL || (dnp->n_ino == 2 && name == NULL)) {
		bzero(fap, sizeof(*fap));
		fap->fa_attr = SMB_FA_DIR;
		fap->fa_ino = 2;
		return 0;
	}
	MPASS(!(nmlen == 2 && name[0] == '.' && name[1] == '.'));
	MPASS(!(nmlen == 1 && name[0] == '.'));
	ASSERT_VOP_ELOCKED(dnp->n_vnode, "smbfs_smb_lookup");
	error = smbfs_findopen(dnp, name, nmlen,
	    SMB_FA_SYSTEM | SMB_FA_HIDDEN | SMB_FA_DIR, scred, &ctx);
	if (error)
		return error;
	ctx->f_flags |= SMBFS_RDD_FINDSINGLE;
	error = smbfs_findnext(ctx, 1, scred);
	if (error == 0) {
		*fap = ctx->f_attr;
		if (name == NULL)
			fap->fa_ino = dnp->n_ino;
	}
	smbfs_findclose(ctx, scred);
	return error;
}
