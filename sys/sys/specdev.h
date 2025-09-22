/*	$OpenBSD: specdev.h,v 1.41 2022/06/26 05:20:42 visa Exp $	*/
/*	$NetBSD: specdev.h,v 1.12 1996/02/13 13:13:01 mycroft Exp $	*/

/*
 * Copyright (c) 1990, 1993
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
 *	@(#)specdev.h	8.3 (Berkeley) 8/10/94
 */

SLIST_HEAD(vnodechain, vnode);

/*
 * This structure defines the information maintained about
 * special devices. It is allocated in checkalias and freed
 * in vgone.
 */
struct specinfo {
	struct	vnodechain *si_hashchain;
	SLIST_ENTRY(vnode) si_specnext;
	struct  mount *si_mountpoint;
	dev_t	si_rdev;
	struct	lockf_state *si_lockf;
	daddr_t si_lastr;
	union {
		struct vnode *ci_parent; /* pointer back to parent device */
		u_int8_t *ci_bitmap; /* bitmap of devices cloned off us */
	} si_ci;
};

struct cloneinfo {
	struct vnode *ci_vp; /* cloned vnode */
	void *ci_data; /* original vnode's v_data */
};

/*
 * Exported shorthand
 */
#define v_rdev v_specinfo->si_rdev
#define v_hashchain v_specinfo->si_hashchain
#define v_specnext v_specinfo->si_specnext
#define v_specmountpoint v_specinfo->si_mountpoint
#define v_speclockf v_specinfo->si_lockf
#define v_specparent v_specinfo->si_ci.ci_parent
#define v_specbitmap v_specinfo->si_ci.ci_bitmap

/*
 * We use the upper 16 bits of the minor to record the clone instance.
 * This gives us 8 bits for encoding the real minor number.
 */
#define CLONE_SHIFT	8
#define CLONE_MAPSZ	128

/*
 * Special device management
 */
#define	SPECHSZ	64
#if	((SPECHSZ&(SPECHSZ-1)) == 0)
#define	SPECHASH(rdev)	(((rdev>>5)+(rdev))&(SPECHSZ-1))
#else
#define	SPECHASH(rdev)	(((unsigned)((rdev>>5)+(rdev)))%SPECHSZ)
#endif

#ifdef	_KERNEL

extern struct vnodechain speclisth[SPECHSZ];

/*
 * Prototypes for special file operations on vnodes.
 */
int	spec_getattr(void *);
int	spec_setattr(void *);
int	spec_access(void *);
int	spec_open(void *);
int	spec_close(void *);
int	spec_read(void *);
int	spec_write(void *);
int	spec_ioctl(void *);
int	spec_kqfilter(void *);
int	spec_fsync(void *);
int	spec_inactive(void *);
int	spec_strategy(void *);
int	spec_print(void *);
int	spec_pathconf(void *);
int	spec_advlock(void *);

#endif	/* _KERNEL */
