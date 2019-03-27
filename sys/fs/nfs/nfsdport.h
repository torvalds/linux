/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Rick Macklem, University of Guelph
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

/*
 * These macros handle nfsvattr fields. They look a bit silly here, but
 * are quite different for the Darwin port.
 */
#define	NFSVNO_ATTRINIT(n)		(VATTR_NULL(&((n)->na_vattr)))
#define	NFSVNO_SETATTRVAL(n, f, v)	((n)->na_##f = (v))
#define	NFSVNO_SETACTIVE(n, f)
#define	NFSVNO_UNSET(n, f)		((n)->na_##f = VNOVAL)
#define	NFSVNO_NOTSETMODE(n)		((n)->na_mode == ((mode_t)VNOVAL))
#define	NFSVNO_ISSETMODE(n)		((n)->na_mode != ((mode_t)VNOVAL))
#define	NFSVNO_NOTSETUID(n)		((n)->na_uid == ((uid_t)VNOVAL))
#define	NFSVNO_ISSETUID(n)		((n)->na_uid != ((uid_t)VNOVAL))
#define	NFSVNO_NOTSETGID(n)		((n)->na_gid == ((gid_t)VNOVAL))
#define	NFSVNO_ISSETGID(n)		((n)->na_gid != ((gid_t)VNOVAL))
#define	NFSVNO_NOTSETSIZE(n)		((n)->na_size == VNOVAL)
#define	NFSVNO_ISSETSIZE(n)		((n)->na_size != VNOVAL)
#define	NFSVNO_NOTSETATIME(n)		((n)->na_atime.tv_sec == VNOVAL)
#define	NFSVNO_ISSETATIME(n)		((n)->na_atime.tv_sec != VNOVAL)
#define	NFSVNO_NOTSETMTIME(n)		((n)->na_mtime.tv_sec == VNOVAL)
#define	NFSVNO_ISSETMTIME(n)		((n)->na_mtime.tv_sec != VNOVAL)

/*
 * This structure acts as a "catch-all" for information that
 * needs to be returned by nfsd_fhtovp().
 */
struct nfsexstuff {
	int	nes_exflag;			/* export flags */
	int	nes_numsecflavor;		/* # of security flavors */
	int	nes_secflavors[MAXSECFLAVORS];	/* and the flavors */
};

/*
 * These are NO-OPS for BSD until Isilon upstreams EXITCODE support.
 * EXITCODE is an in-memory ring buffer that holds the routines failing status.
 * This is a valuable tool to use when debugging and analyzing issues.
 * In addition to recording a routine's failing status, it offers
 * logging of routines for call stack tracing.
 * EXITCODE should be used only in routines that return a true errno value, as
 * that value will be formatted to a displayable errno string.  Routines that 
 * return regular int status that are not true errno should not set EXITCODE.
 * If you want to log routine tracing, you can add EXITCODE(0) to any routine.
 * NFS extended the EXITCODE with EXITCODE2 to record either the routine's
 * exit errno status or the nd_repstat.
 */
#define	NFSEXITCODE(error)
#define	NFSEXITCODE2(error, nd)

#define	NFSVNO_EXINIT(e)		((e)->nes_exflag = 0)
#define	NFSVNO_EXPORTED(e)		((e)->nes_exflag & MNT_EXPORTED)
#define	NFSVNO_EXRDONLY(e)		((e)->nes_exflag & MNT_EXRDONLY)
#define	NFSVNO_EXPORTANON(e)		((e)->nes_exflag & MNT_EXPORTANON)
#define	NFSVNO_EXSTRICTACCESS(e)	((e)->nes_exflag & MNT_EXSTRICTACCESS)
#define	NFSVNO_EXV4ONLY(e)		((e)->nes_exflag & MNT_EXV4ONLY)

#define	NFSVNO_SETEXRDONLY(e)	((e)->nes_exflag = (MNT_EXPORTED|MNT_EXRDONLY))

#define	NFSVNO_CMPFH(f1, f2)						\
    ((f1)->fh_fsid.val[0] == (f2)->fh_fsid.val[0] &&			\
     (f1)->fh_fsid.val[1] == (f2)->fh_fsid.val[1] &&			\
     bcmp(&(f1)->fh_fid, &(f2)->fh_fid, sizeof(struct fid)) == 0)

#define	NFSLOCKHASH(f) 							\
	(&nfslockhash[nfsrv_hashfh(f) % nfsrv_lockhashsize])

#define	NFSFPVNODE(f)	((struct vnode *)((f)->f_data))
#define	NFSFPCRED(f)	((f)->f_cred)
#define	NFSFPFLAG(f)	((f)->f_flag)

#define	NFSNAMEICNDSET(n, c, o, f)	do {				\
	(n)->cn_cred = (c);						\
	(n)->cn_nameiop = (o);						\
	(n)->cn_flags = (f);						\
    } while (0)

/*
 * A little bit of Darwin vfs kpi.
 */
#define	vnode_mount(v)	((v)->v_mount)
#define	vfs_statfs(m)	(&((m)->mnt_stat))

#define	NFSPATHLEN_T	size_t

/*
 * These are set to the minimum and maximum size of a server file
 * handle.
 */
#define	NFSRV_MINFH	(sizeof (fhandle_t))
#define	NFSRV_MAXFH	(sizeof (fhandle_t))

/* Use this macro for debug printfs. */
#define	NFSD_DEBUG(level, ...)	do {					\
		if (nfsd_debuglevel >= (level))				\
			printf(__VA_ARGS__);				\
	} while (0)

