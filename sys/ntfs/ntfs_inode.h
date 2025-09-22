/*	$OpenBSD: ntfs_inode.h,v 1.8 2021/03/11 13:31:35 jsg Exp $	*/
/*	$NetBSD: ntfs_inode.h,v 1.1 2002/12/23 17:38:33 jdolecek Exp $	*/

/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko
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
 *	Id: ntfs_inode.h,v 1.4 1999/05/12 09:43:00 semenu Exp
 */

#define	IN_HASHED	0x0800	/* Inode is on hash list */
#define	IN_LOADED	0x8000	/* ntvattrs loaded */
#define	IN_PRELOADED	0x4000	/* loaded from directory entry */

struct ntnode {
	struct vnode   *i_devvp;	/* vnode of blk dev we live on */
	dev_t           i_dev;		/* Device associated with the inode. */

	LIST_ENTRY(ntnode)	i_hash;
	TAILQ_ENTRY(ntnode)	i_loaded;

	struct ntnode  *i_next;
	struct ntnode **i_prev;

	struct ntfsmount       *i_mp;
	ntfsino_t       i_number;
	u_int32_t       i_flag;

	/* locking */
	struct rwlock	i_lock;
	int		i_usecount;

	LIST_HEAD(,fnode)	i_fnlist;
	LIST_HEAD(,ntvattr)	i_valist;

	long		i_nlink;	/* MFR */
	u_int64_t	i_mainrec;	/* MFR */
	u_int32_t	i_frflag;	/* MFR */
};

#define	FN_PRELOADED	0x0001
#define	FN_VALID	0x0002
#define	FN_AATTRNAME	0x0004	/* space allocated for f_attrname */
struct fnode {
	LIST_ENTRY(fnode) f_fnlist;
	struct vnode   *f_vp;		/* Associated vnode */
	struct ntnode  *f_ip;		/* Associated ntnode */
	u_long		f_flag;

	ntfs_times_t	f_times;	/* $NAME/dirinfo */
	u_int32_t	f_pnumber;	/* $NAME/dirinfo */
	u_int32_t       f_fflag;	/* $NAME/dirinfo */
	u_int64_t	f_size;		/* defattr/dirinfo: */
	u_int64_t	f_allocated;	/* defattr/dirinfo */

	u_int32_t	f_attrtype;
	char	       *f_attrname;

	/* for ntreaddir */
	u_int32_t       f_lastdattr;
	u_int32_t       f_lastdblnum;
	u_int32_t       f_lastdoff;
	u_int32_t       f_lastdnum;
	caddr_t         f_dirblbuf;
	u_int32_t       f_dirblsz;
};

/* This overlays the fid structure (see <sys/mount.h>) */
struct ntfid {
	u_int16_t ntfid_len;	/* Length of structure. */
	u_int16_t ntfid_pad;	/* Force 32-bit alignment. */
	ntfsino_t ntfid_ino;	/* File number (ino). */
	u_int8_t  ntfid_attr;	/* Attribute identifier */
#ifdef notyet
	int32_t   ntfid_gen;	/* Generation number. */
#endif
};
