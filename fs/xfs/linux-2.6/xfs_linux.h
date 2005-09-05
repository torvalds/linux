/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_LINUX__
#define __XFS_LINUX__

#include <linux/types.h>
#include <linux/config.h>

/*
 * Some types are conditional depending on the target system.
 * XFS_BIG_BLKNOS needs block layer disk addresses to be 64 bits.
 * XFS_BIG_INUMS needs the VFS inode number to be 64 bits, as well
 * as requiring XFS_BIG_BLKNOS to be set.
 */
#if defined(CONFIG_LBD) || (BITS_PER_LONG == 64)
# define XFS_BIG_BLKNOS	1
# if BITS_PER_LONG == 64
#  define XFS_BIG_INUMS	1
# else
#  define XFS_BIG_INUMS	0
# endif
#else
# define XFS_BIG_BLKNOS	0
# define XFS_BIG_INUMS	0
#endif

#include <xfs_types.h>
#include <xfs_arch.h>

#include <kmem.h>
#include <mrlock.h>
#include <spin.h>
#include <sv.h>
#include <mutex.h>
#include <sema.h>
#include <time.h>

#include <support/ktrace.h>
#include <support/debug.h>
#include <support/move.h>
#include <support/uuid.h>

#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/major.h>
#include <linux/pagemap.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/sort.h>

#include <asm/page.h>
#include <asm/div64.h>
#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include <xfs_behavior.h>
#include <xfs_vfs.h>
#include <xfs_cred.h>
#include <xfs_vnode.h>
#include <xfs_stats.h>
#include <xfs_sysctl.h>
#include <xfs_iops.h>
#include <xfs_aops.h>
#include <xfs_super.h>
#include <xfs_globals.h>
#include <xfs_fs_subr.h>
#include <xfs_lrw.h>
#include <xfs_buf.h>

/*
 * Feature macros (disable/enable)
 */
#undef  HAVE_REFCACHE	/* reference cache not needed for NFS in 2.6 */
#define HAVE_SENDFILE	/* sendfile(2) exists in 2.6, but not in 2.4 */

/*
 * State flag for unwritten extent buffers.
 *
 * We need to be able to distinguish between these and delayed
 * allocate buffers within XFS.  The generic IO path code does
 * not need to distinguish - we use the BH_Delay flag for both
 * delalloc and these ondisk-uninitialised buffers.
 */
BUFFER_FNS(PrivateStart, unwritten);
static inline void set_buffer_unwritten_io(struct buffer_head *bh)
{
	bh->b_end_io = linvfs_unwritten_done;
}

#define restricted_chown	xfs_params.restrict_chown.val
#define irix_sgid_inherit	xfs_params.sgid_inherit.val
#define irix_symlink_mode	xfs_params.symlink_mode.val
#define xfs_panic_mask		xfs_params.panic_mask.val
#define xfs_error_level		xfs_params.error_level.val
#define xfs_syncd_centisecs	xfs_params.syncd_timer.val
#define xfs_stats_clear		xfs_params.stats_clear.val
#define xfs_inherit_sync	xfs_params.inherit_sync.val
#define xfs_inherit_nodump	xfs_params.inherit_nodump.val
#define xfs_inherit_noatime	xfs_params.inherit_noatim.val
#define xfs_buf_timer_centisecs	xfs_params.xfs_buf_timer.val
#define xfs_buf_age_centisecs	xfs_params.xfs_buf_age.val
#define xfs_inherit_nosymlinks	xfs_params.inherit_nosym.val
#define xfs_rotorstep		xfs_params.rotorstep.val

#ifndef raw_smp_processor_id
#define raw_smp_processor_id()	smp_processor_id()
#endif
#define current_cpu()		raw_smp_processor_id()
#define current_pid()		(current->pid)
#define current_fsuid(cred)	(current->fsuid)
#define current_fsgid(cred)	(current->fsgid)

#define NBPP		PAGE_SIZE
#define DPPSHFT		(PAGE_SHIFT - 9)
#define NDPP		(1 << (PAGE_SHIFT - 9))
#define dtop(DD)	(((DD) + NDPP - 1) >> DPPSHFT)
#define dtopt(DD)	((DD) >> DPPSHFT)
#define dpoff(DD)	((DD) & (NDPP-1))

#define NBBY		8		/* number of bits per byte */
#define	NBPC		PAGE_SIZE	/* Number of bytes per click */
#define	BPCSHIFT	PAGE_SHIFT	/* LOG2(NBPC) if exact */

/*
 * Size of block device i/o is parameterized here.
 * Currently the system supports page-sized i/o.
 */
#define	BLKDEV_IOSHIFT		BPCSHIFT
#define	BLKDEV_IOSIZE		(1<<BLKDEV_IOSHIFT)
/* number of BB's per block device block */
#define	BLKDEV_BB		BTOBB(BLKDEV_IOSIZE)

/* bytes to clicks */
#define	btoc(x)		(((__psunsigned_t)(x)+(NBPC-1))>>BPCSHIFT)
#define	btoct(x)	((__psunsigned_t)(x)>>BPCSHIFT)
#define	btoc64(x)	(((__uint64_t)(x)+(NBPC-1))>>BPCSHIFT)
#define	btoct64(x)	((__uint64_t)(x)>>BPCSHIFT)
#define	io_btoc(x)	(((__psunsigned_t)(x)+(IO_NBPC-1))>>IO_BPCSHIFT)
#define	io_btoct(x)	((__psunsigned_t)(x)>>IO_BPCSHIFT)

/* off_t bytes to clicks */
#define offtoc(x)       (((__uint64_t)(x)+(NBPC-1))>>BPCSHIFT)
#define offtoct(x)      ((xfs_off_t)(x)>>BPCSHIFT)

/* clicks to off_t bytes */
#define	ctooff(x)	((xfs_off_t)(x)<<BPCSHIFT)

/* clicks to bytes */
#define	ctob(x)		((__psunsigned_t)(x)<<BPCSHIFT)
#define btoct(x)        ((__psunsigned_t)(x)>>BPCSHIFT)
#define	ctob64(x)	((__uint64_t)(x)<<BPCSHIFT)
#define	io_ctob(x)	((__psunsigned_t)(x)<<IO_BPCSHIFT)

/* bytes to clicks */
#define btoc(x)         (((__psunsigned_t)(x)+(NBPC-1))>>BPCSHIFT)

#ifndef CELL_CAPABLE
#define FSC_NOTIFY_NAME_CHANGED(vp)
#endif

#ifndef ENOATTR
#define ENOATTR		ENODATA		/* Attribute not found */
#endif

/* Note: EWRONGFS never visible outside the kernel */
#define	EWRONGFS	EINVAL		/* Mount with wrong filesystem type */

/*
 * XXX EFSCORRUPTED needs a real value in errno.h. asm-i386/errno.h won't
 *     return codes out of its known range in errno.
 * XXX Also note: needs to be < 1000 and fairly unique on Linux (mustn't
 *     conflict with any code we use already or any code a driver may use)
 * XXX Some options (currently we do #2):
 *	1/ New error code ["Filesystem is corrupted", _after_ glibc updated]
 *	2/ 990 ["Unknown error 990"]
 *	3/ EUCLEAN ["Structure needs cleaning"]
 *	4/ Convert EFSCORRUPTED to EIO [just prior to return into userspace]
 */
#define EFSCORRUPTED    990		/* Filesystem is corrupted */

#define SYNCHRONIZE()	barrier()
#define __return_address __builtin_return_address(0)

/*
 * IRIX (BSD) quotactl makes use of separate commands for user/group,
 * whereas on Linux the syscall encodes this information into the cmd
 * field (see the QCMD macro in quota.h).  These macros help keep the
 * code portable - they are not visible from the syscall interface.
 */
#define Q_XSETGQLIM	XQM_CMD(8)	/* set groups disk limits */
#define Q_XGETGQUOTA	XQM_CMD(9)	/* get groups disk limits */
#define Q_XSETPQLIM	XQM_CMD(10)	/* set projects disk limits */
#define Q_XGETPQUOTA	XQM_CMD(11)	/* get projects disk limits */

/* IRIX uses a dynamic sizing algorithm (ndquot = 200 + numprocs*2) */
/* we may well need to fine-tune this if it ever becomes an issue.  */
#define DQUOT_MAX_HEURISTIC	1024	/* NR_DQUOTS */
#define ndquot			DQUOT_MAX_HEURISTIC

/* IRIX uses the current size of the name cache to guess a good value */
/* - this isn't the same but is a good enough starting point for now. */
#define DQUOT_HASH_HEURISTIC	files_stat.nr_files

/* IRIX inodes maintain the project ID also, zero this field on Linux */
#define DEFAULT_PROJID	0
#define dfltprid	DEFAULT_PROJID

#define MAXPATHLEN	1024

#define MIN(a,b)	(min(a,b))
#define MAX(a,b)	(max(a,b))
#define howmany(x, y)	(((x)+((y)-1))/(y))
#define roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#define qsort(a,n,s,fn)	sort(a,n,s,fn,NULL)

/*
 * Various platform dependent calls that don't fit anywhere else
 */
#define xfs_stack_trace()	dump_stack()
#define xfs_itruncate_data(ip, off)	\
	(-vmtruncate(LINVFS_GET_IP(XFS_ITOV(ip)), (off)))
#define xfs_statvfs_fsid(statp, mp)	\
	({ u64 id = huge_encode_dev((mp)->m_dev);	\
	   __kernel_fsid_t *fsid = &(statp)->f_fsid;	\
	(fsid->val[0] = (u32)id, fsid->val[1] = (u32)(id >> 32)); })


/* Move the kernel do_div definition off to one side */

#if defined __i386__
/* For ia32 we need to pull some tricks to get past various versions
 * of the compiler which do not like us using do_div in the middle
 * of large functions.
 */
static inline __u32 xfs_do_div(void *a, __u32 b, int n)
{
	__u32	mod;

	switch (n) {
		case 4:
			mod = *(__u32 *)a % b;
			*(__u32 *)a = *(__u32 *)a / b;
			return mod;
		case 8:
			{
			unsigned long __upper, __low, __high, __mod;
			__u64	c = *(__u64 *)a;
			__upper = __high = c >> 32;
			__low = c;
			if (__high) {
				__upper = __high % (b);
				__high = __high / (b);
			}
			asm("divl %2":"=a" (__low), "=d" (__mod):"rm" (b), "0" (__low), "1" (__upper));
			asm("":"=A" (c):"a" (__low),"d" (__high));
			*(__u64 *)a = c;
			return __mod;
			}
	}

	/* NOTREACHED */
	return 0;
}

/* Side effect free 64 bit mod operation */
static inline __u32 xfs_do_mod(void *a, __u32 b, int n)
{
	switch (n) {
		case 4:
			return *(__u32 *)a % b;
		case 8:
			{
			unsigned long __upper, __low, __high, __mod;
			__u64	c = *(__u64 *)a;
			__upper = __high = c >> 32;
			__low = c;
			if (__high) {
				__upper = __high % (b);
				__high = __high / (b);
			}
			asm("divl %2":"=a" (__low), "=d" (__mod):"rm" (b), "0" (__low), "1" (__upper));
			asm("":"=A" (c):"a" (__low),"d" (__high));
			return __mod;
			}
	}

	/* NOTREACHED */
	return 0;
}
#else
static inline __u32 xfs_do_div(void *a, __u32 b, int n)
{
	__u32	mod;

	switch (n) {
		case 4:
			mod = *(__u32 *)a % b;
			*(__u32 *)a = *(__u32 *)a / b;
			return mod;
		case 8:
			mod = do_div(*(__u64 *)a, b);
			return mod;
	}

	/* NOTREACHED */
	return 0;
}

/* Side effect free 64 bit mod operation */
static inline __u32 xfs_do_mod(void *a, __u32 b, int n)
{
	switch (n) {
		case 4:
			return *(__u32 *)a % b;
		case 8:
			{
			__u64	c = *(__u64 *)a;
			return do_div(c, b);
			}
	}

	/* NOTREACHED */
	return 0;
}
#endif

#undef do_div
#define do_div(a, b)	xfs_do_div(&(a), (b), sizeof(a))
#define do_mod(a, b)	xfs_do_mod(&(a), (b), sizeof(a))

static inline __uint64_t roundup_64(__uint64_t x, __uint32_t y)
{
	x += y - 1;
	do_div(x, y);
	return(x * y);
}

#endif /* __XFS_LINUX__ */
