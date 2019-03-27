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
#ifndef _FS_SMBFS_SMBFS_SUBR_H_
#define _FS_SMBFS_SMBFS_SUBR_H_

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_SMBFSDATA);
MALLOC_DECLARE(M_SMBFSCRED);
#endif

#define SMBFSERR(format, args...) printf("%s: "format, __func__ ,## args)

#ifdef SMB_VNODE_DEBUG
#define SMBVDEBUG(format, args...) printf("%s: "format, __func__ ,## args)
#else
#define SMBVDEBUG(format, args...)
#endif

/*
 * Possible lock commands
 */
#define SMB_LOCK_EXCL		0
#define	SMB_LOCK_SHARED		1
#define	SMB_LOCK_RELEASE	2

struct smbmount;
struct proc;
struct timespec;
struct ucred;
struct vattr;
struct vnode;
struct statfs;

struct smbfattr {
	int		fa_attr;
	int64_t		fa_size;
	struct timespec	fa_atime;
	struct timespec	fa_ctime;
	struct timespec	fa_mtime;
	long		fa_ino;
};

/*
 * Context to perform findfirst/findnext/findclose operations
 */
#define	SMBFS_RDD_FINDFIRST	0x01
#define	SMBFS_RDD_EOF		0x02
#define	SMBFS_RDD_FINDSINGLE	0x04
#define	SMBFS_RDD_USESEARCH	0x08
#define	SMBFS_RDD_NOCLOSE	0x10
#define	SMBFS_RDD_GOTRNAME	0x1000

/*
 * Search context supplied by server
 */
#define	SMB_SKEYLEN		21			/* search context */
#define SMB_DENTRYLEN		(SMB_SKEYLEN + 22)	/* entire entry */

struct smbfs_fctx {
	/*
	 * Setable values
	 */
	int		f_flags;	/* SMBFS_RDD_ */
	/*
	 * Return values
	 */
	struct smbfattr	f_attr;		/* current attributes */
	char *		f_name;		/* current file name */
	int		f_nmlen;	/* name len */
	/*
	 * Internal variables
	 */
	int		f_limit;	/* maximum number of entries */
	int		f_attrmask;	/* SMB_FA_ */
	int		f_wclen;
	const char *	f_wildcard;
	struct smbnode*	f_dnp;
	struct smb_cred*f_scred;
	struct smb_share *f_ssp;
	union {
		struct smb_rq *	uf_rq;
		struct smb_t2rq * uf_t2;
	} f_urq;
	int		f_left;		/* entries left */
	int		f_ecnt;		/* entries left in the current response */
	int		f_eofs;		/* entry offset in the parameter block */
	u_char 		f_skey[SMB_SKEYLEN]; /* server side search context */
	u_char		f_fname[8 + 1 + 3 + 1]; /* common case for 8.3 filenames */
	u_int16_t	f_Sid;
	u_int16_t	f_infolevel;
	int		f_rnamelen;
	char *		f_rname;	/* resume name/key */
	int		f_rnameofs;
};

#define f_rq	f_urq.uf_rq
#define f_t2	f_urq.uf_t2

/*
 * smb level
 */
int  smbfs_smb_lock(struct smbnode *np, int op, caddr_t id,
	off_t start, off_t end,	struct smb_cred *scred);
int  smbfs_smb_statfs(struct smb_share *ssp, struct statfs *sbp,
	struct smb_cred *scred);
int  smbfs_smb_setfsize(struct smbnode *np, int64_t newsize,
	struct smb_cred *scred);

int  smbfs_smb_query_info(struct smbnode *np, const char *name, int len,
	struct smbfattr *fap, struct smb_cred *scred);
int  smbfs_smb_setpattr(struct smbnode *np, u_int16_t attr,
	struct timespec *mtime, struct smb_cred *scred);
int  smbfs_smb_setptime2(struct smbnode *np, struct timespec *mtime,
	struct timespec *atime, int attr, struct smb_cred *scred);
int  smbfs_smb_setpattrNT(struct smbnode *np, u_int16_t attr,
	struct timespec *mtime, struct timespec *atime, struct smb_cred *scred);

int  smbfs_smb_setftime(struct smbnode *np, struct timespec *mtime,
	struct timespec *atime, struct smb_cred *scred);
int  smbfs_smb_setfattrNT(struct smbnode *np, u_int16_t attr,
	struct timespec *mtime,	struct timespec *atime, struct smb_cred *scred);

int  smbfs_smb_open(struct smbnode *np, int accmode, struct smb_cred *scred);
int  smbfs_smb_close(struct smb_share *ssp, u_int16_t fid,
	 struct timespec *mtime, struct smb_cred *scred);
int  smbfs_smb_create(struct smbnode *dnp, const char *name, int len,
	struct smb_cred *scred);
int  smbfs_smb_delete(struct smbnode *np, struct smb_cred *scred);
int  smbfs_smb_flush(struct smbnode *np, struct smb_cred *scred);
int  smbfs_smb_rename(struct smbnode *src, struct smbnode *tdnp,
	const char *tname, int tnmlen, struct smb_cred *scred);
int  smbfs_smb_move(struct smbnode *src, struct smbnode *tdnp,
	const char *tname, int tnmlen, u_int16_t flags, struct smb_cred *scred);
int  smbfs_smb_mkdir(struct smbnode *dnp, const char *name, int len,
	struct smb_cred *scred);
int  smbfs_smb_rmdir(struct smbnode *np, struct smb_cred *scred);
int  smbfs_findopen(struct smbnode *dnp, const char *wildcard, int wclen,
	int attr, struct smb_cred *scred, struct smbfs_fctx **ctxpp);
int  smbfs_findnext(struct smbfs_fctx *ctx, int limit, struct smb_cred *scred);
int  smbfs_findclose(struct smbfs_fctx *ctx, struct smb_cred *scred);
int  smbfs_fullpath(struct mbchain *mbp, struct smb_vc *vcp,
	struct smbnode *dnp, const char *name, int nmlen);
int  smbfs_smb_lookup(struct smbnode *dnp, const char *name, int nmlen,
	struct smbfattr *fap, struct smb_cred *scred);

int  smbfs_fname_tolocal(struct smb_vc *vcp, char *name, int *nmlen, int caseopt);

void  smb_time_local2server(struct timespec *tsp, int tzoff, u_long *seconds);
void  smb_time_server2local(u_long seconds, int tzoff, struct timespec *tsp);
void  smb_time_NT2local(int64_t nsec, int tzoff, struct timespec *tsp);
void  smb_time_local2NT(struct timespec *tsp, int tzoff, int64_t *nsec);
void  smb_time_unix2dos(struct timespec *tsp, int tzoff, u_int16_t *ddp, 
	     u_int16_t *dtp, u_int8_t *dhp);
void smb_dos2unixtime (u_int dd, u_int dt, u_int dh, int tzoff, struct timespec *tsp);

void *smbfs_malloc_scred(void);
void smbfs_free_scred(void *);
#endif /* !_FS_SMBFS_SMBFS_SUBR_H_ */
