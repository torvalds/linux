/*	$OpenBSD: namei.h,v 1.50 2022/01/11 23:59:55 jsg Exp $	*/
/*	$NetBSD: namei.h,v 1.11 1996/02/09 18:25:20 christos Exp $	*/

/*
 * Copyright (c) 1985, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)namei.h	8.4 (Berkeley) 8/20/94
 */

#ifndef _SYS_NAMEI_H_
#define	_SYS_NAMEI_H_

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/uio.h>

struct unveil;

/*
 * Encapsulation of namei parameters.
 */
struct nameidata {
	/*
	 * Arguments to namei/lookup.
	 */
	const char *ni_dirp;		/* pathname pointer */
	int	ni_dirfd;		/* dirfd from *at() functions */
	enum	uio_seg ni_segflg;	/* location of pathname */
     /* u_long	ni_nameiop;		   namei operation */
     /* u_long	ni_flags;		   flags to namei */
     /* struct	proc *ni_proc;		   process requesting lookup */
	/*
	 * Arguments to lookup.
	 */
     /* struct	ucred *ni_cred;		   credentials */
	struct	vnode *ni_startdir;	/* starting directory */
	struct	vnode *ni_rootdir;	/* logical root directory */
	uint64_t ni_pledge;		/* expected pledge for namei */
	u_char ni_unveil;		/* required unveil flags for namei */
	/*
	 * Results: returned from/manipulated by lookup
	 */
	struct	vnode *ni_vp;		/* vnode of result */
	struct	vnode *ni_dvp;		/* vnode of intermediate directory */

	/*
	 * Shared between namei and lookup/commit routines.
	 */
	size_t	ni_pathlen;		/* remaining chars in path */
	char	*ni_next;		/* next location in pathname */
	u_long	ni_loopcnt;		/* count of symlinks encountered */
	struct unveil *ni_unveil_match; /* last matching unveil component */

	/*
	 * Lookup parameters: this structure describes the subset of
	 * information from the nameidata structure that is passed
	 * through the VOP interface.
	 */
	struct componentname {
		/*
		 * Arguments to lookup.
		 */
		u_long	cn_nameiop;	/* namei operation */
		u_long	cn_flags;	/* flags to namei */
		struct	proc *cn_proc;	/* process requesting lookup */
		struct	ucred *cn_cred;	/* credentials */
		/*
		 * Shared between lookup and commit routines.
		 */
		char	*cn_pnbuf;	/* pathname buffer */
		char	*cn_rpbuf;	/* realpath buffer */
		size_t	cn_rpi;		/* realpath index */
		char	*cn_nameptr;	/* pointer to looked up name */
		long	cn_namelen;	/* length of looked up component */
		long	cn_consume;	/* chars to consume in lookup() */
	} ni_cnd;
};

#ifdef _KERNEL
/*
 * namei operations
 */
#define	LOOKUP		0	/* perform name lookup only */
#define	CREATE		1	/* setup for file creation */
#define	DELETE		2	/* setup for file deletion */
#define	RENAME		3	/* setup for file renaming */
#define	OPMASK		3	/* mask for operation */
/*
 * namei operational modifier flags, stored in ni_cnd.flags
 */
#define	LOCKLEAF	0x0004	/* lock inode on return */
#define	LOCKPARENT	0x0008	/* want parent vnode returned locked */
#define	WANTPARENT	0x0010	/* want parent vnode returned unlocked */
#define	NOCACHE		0x0020	/* name must not be left in cache */
#define	FOLLOW		0x0040	/* follow symbolic links */
#define	NOFOLLOW	0x0000	/* do not follow symbolic links (pseudo) */
#define	MODMASK		0x00fc	/* mask of operational modifiers */
/*
 * Namei parameter descriptors.
 *
 * SAVENAME may be set by either the callers of namei or by VOP_LOOKUP.
 * If the caller of namei sets the flag (for example execve wants to
 * know the name of the program that is being executed), then it must
 * free the buffer. If VOP_LOOKUP sets the flag, then the buffer must
 * be freed by either the commit routine or the VOP_ABORT routine.
 * SAVESTART is set only by the callers of namei. It implies SAVENAME
 * plus the addition of saving the parent directory that contains the
 * name in ni_startdir. It allows repeated calls to lookup for the
 * name being sought. The caller is responsible for releasing the
 * buffer and for vrele'ing ni_startdir.
 */
#define	NOCROSSMOUNT	0x000100      /* do not cross mount points */
#define	RDONLY		0x000200      /* lookup with read-only semantics */
#define	HASBUF		0x000400      /* has allocated pathname buffer */
#define	SAVENAME	0x000800      /* save pathname buffer */
#define	SAVESTART	0x001000      /* save starting directory */
#define ISDOTDOT	0x002000      /* current component name is .. */
#define MAKEENTRY	0x004000      /* entry is to be added to name cache */
#define ISLASTCN	0x008000      /* this is last component of pathname */
#define ISSYMLINK	0x010000      /* symlink needs interpretation */
#define REALPATH	0x020000      /* save pathname buffer for realpath */
#define	REQUIREDIR	0x080000      /* must be a directory */
#define STRIPSLASHES    0x100000      /* strip trailing slashes */
#define PDIRUNLOCK	0x200000      /* vfs_lookup() unlocked parent dir */
#define BYPASSUNVEIL	0x400000      /* bypass pledgepath check */
#define KERNELPATH	0x800000      /* access file as kernel, not process */

/*
 * Initialization of an nameidata structure.
 */
void ndinitat(struct nameidata *ndp, u_long op, u_long flags,
    enum uio_seg segflg, int dirfd, const char *namep, struct proc *p);

#define NDINITAT(ndp, op, flags, segflg, dirfd, namep, p)  \
	ndinitat(ndp, op, flags, segflg, dirfd, namep, p)

#define NDINIT(ndp, op, flags, segflp, namep, p) \
	ndinitat(ndp, op, flags, segflp, AT_FDCWD, namep, p)

/* Defined for users of NDINIT(). */
#define	AT_FDCWD	-100
#endif

/*
 * This structure describes the elements in the cache of recent
 * names looked up by namei.
 */

#define	NAMECACHE_MAXLEN 31 /* maximum name segment length we bother with */

struct	namecache {
	TAILQ_ENTRY(namecache) nc_lru;	/* Regular Entry LRU chain */
	TAILQ_ENTRY(namecache) nc_neg;	/* Negative Entry LRU chain */
	RBT_ENTRY(namecache) n_rbcache;	/* Namecache rb tree from vnode */
	TAILQ_ENTRY(namecache) nc_me;	/* ncp's referring to me */
	struct	vnode *nc_dvp;		/* vnode of parent of name */
	u_long	nc_dvpid;		/* capability number of nc_dvp */
	struct	vnode *nc_vp;		/* vnode the name refers to */
	u_long	nc_vpid;		/* capability number of nc_vp */
	char	nc_nlen;		/* length of name */
	char	nc_name[NAMECACHE_MAXLEN];	/* segment name */
};

#ifdef _KERNEL
struct	namecache_rb_cache;

int	namei(struct nameidata *ndp);
int	vfs_lookup(struct nameidata *ndp);
int	vfs_relookup(struct vnode *dvp, struct vnode **vpp,
		      struct componentname *cnp);
void cache_tree_init(struct namecache_rb_cache *);
void cache_purge(struct vnode *);
int cache_lookup(struct vnode *, struct vnode **, struct componentname *);
void cache_enter(struct vnode *, struct vnode *, struct componentname *);
int cache_revlookup(struct vnode *, struct vnode **, char **, char *);
void nchinit(void);
struct mount;
void cache_purgevfs(struct mount *);

int unveil_add(struct proc *, struct nameidata *, const char *);
void unveil_removevnode(struct vnode *);
ssize_t unveil_find_cover(struct vnode *, struct proc *);
struct unveil *unveil_lookup(struct vnode *, struct process *, ssize_t *);
void unveil_start_relative(struct proc *, struct nameidata *, struct vnode *);
void unveil_check_component(struct proc *, struct nameidata *, struct vnode *);
int unveil_check_final(struct proc *, struct nameidata *);

extern struct pool namei_pool;

#endif

/*
 * Stats on usefulness of namei caches.
 */
struct	nchstats {
	u_int64_t	ncs_goodhits;	/* hits that we can really use */
	u_int64_t	ncs_neghits;	/* negative hits that we can use */
	u_int64_t	ncs_badhits;	/* hits we must drop */
	u_int64_t	ncs_falsehits;	/* hits with id mismatch */
	u_int64_t	ncs_miss;	/* misses */
	u_int64_t	ncs_long;	/* long names that ignore cache */
	u_int64_t	ncs_pass2;	/* names found with passes == 2 */
	u_int64_t	ncs_2passes;	/* number of times we attempt it */
	u_int64_t	ncs_revhits;	/* reverse-cache hits */
	u_int64_t	ncs_revmiss;	/* reverse-cache misses */
	u_int64_t	ncs_dothits;	/* hits on '.' lookups */
	u_int64_t	ncs_dotdothits;	/* hits on '..' lookups */
};

/* These sysctl names are only really used by sysctl(8) */
#define KERN_NCHSTATS_GOODHITS		1
#define KERN_NCHSTATS_NEGHITS		2
#define KERN_NCHSTATS_BADHITS		3
#define KERN_NCHSTATS_FALSEHITS		4
#define KERN_NCHSTATS_MISS		5
#define KERN_NCHSTATS_LONG		6
#define KERN_NCHSTATS_PASS2		7
#define KERN_NCHSTATS_2PASSES		8
#define KERN_NCHSTATS_REVHITS           9
#define KERN_NCHSTATS_REVMISS           10
#define KERN_NCHSTATS_DOTHITS		11
#define KERN_NCHSTATS_DOTDOTHITS	12
#define KERN_NCHSTATS_MAXID		13

#define CTL_KERN_NCHSTATS_NAMES {		\
	{ 0, 0 },				\
	{ "good_hits", CTLTYPE_QUAD },		\
	{ "negative_hits", CTLTYPE_QUAD },	\
	{ "bad_hits", CTLTYPE_QUAD },		\
	{ "false_hits", CTLTYPE_QUAD },		\
	{ "misses", CTLTYPE_QUAD },		\
	{ "long_names", CTLTYPE_QUAD },		\
	{ "pass2", CTLTYPE_QUAD },		\
	{ "2passes", CTLTYPE_QUAD },		\
	{ "ncs_revhits", CTLTYPE_QUAD },	\
	{ "ncs_revmiss", CTLTYPE_QUAD },	\
	{ "ncs_dothits", CTLTYPE_QUAD },	\
	{ "nch_dotdothits", CTLTYPE_QUAD },	\
}

/* Unveil flags for namei */
#define	UNVEIL_READ	0x01
#define	UNVEIL_WRITE	0x02
#define	UNVEIL_CREATE	0x04
#define	UNVEIL_EXEC	0x08
#define	UNVEIL_USERSET	0x10
#define	UNVEIL_MASK	0x0F

#endif /* !_SYS_NAMEI_H_ */
