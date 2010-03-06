/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_LINUX__
#define __XFS_LINUX__

#include <linux/types.h>

/*
 * XFS_BIG_BLKNOS needs block layer disk addresses to be 64 bits.
 * XFS_BIG_INUMS requires XFS_BIG_BLKNOS to be set.
 */
#if defined(CONFIG_LBDAF) || (BITS_PER_LONG == 64)
# define XFS_BIG_BLKNOS	1
# define XFS_BIG_INUMS	1
#else
# define XFS_BIG_BLKNOS	0
# define XFS_BIG_INUMS	0
#endif

#include <xfs_types.h>
#include <xfs_arch.h>

#include <kmem.h>
#include <mrlock.h>
#include <sv.h>
#include <time.h>

#include <support/debug.h>
#include <support/uuid.h>

#include <linux/semaphore.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
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
#include <linux/sort.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/ctype.h>
#include <linux/writeback.h>

#include <asm/page.h>
#include <asm/div64.h>
#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include <xfs_cred.h>
#include <xfs_vnode.h>
#include <xfs_stats.h>
#include <xfs_sysctl.h>
#include <xfs_iops.h>
#include <xfs_aops.h>
#include <xfs_super.h>
#include <xfs_globals.h>
#include <xfs_fs_subr.h>
#include <xfs_buf.h>

/*
 * Feature macros (disable/enable)
 */
#ifdef CONFIG_SMP
#define HAVE_PERCPU_SB	/* per cpu superblock counters are a 2.6 feature */
#else
#undef  HAVE_PERCPU_SB	/* per cpu superblock counters are a 2.6 feature */
#endif

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
#define xfs_inherit_nodefrag	xfs_params.inherit_nodfrg.val
#define xfs_fstrm_centisecs	xfs_params.fstrm_timer.val

#define current_cpu()		(raw_smp_processor_id())
#define current_pid()		(current->pid)
#define current_test_flags(f)	(current->flags & (f))
#define current_set_flags_nested(sp, f)		\
		(*(sp) = current->flags, current->flags |= (f))
#define current_clear_flags_nested(sp, f)	\
		(*(sp) = current->flags, current->flags &= ~(f))
#define current_restore_flags_nested(sp, f)	\
		(current->flags = ((current->flags & ~(f)) | (*(sp) & (f))))

#define spinlock_destroy(lock)

#define NBBY		8		/* number of bits per byte */

/*
 * Size of block device i/o is parameterized here.
 * Currently the system supports page-sized i/o.
 */
#define	BLKDEV_IOSHIFT		PAGE_CACHE_SHIFT
#define	BLKDEV_IOSIZE		(1<<BLKDEV_IOSHIFT)
/* number of BB's per block device block */
#define	BLKDEV_BB		BTOBB(BLKDEV_IOSIZE)

#define ENOATTR		ENODATA		/* Attribute not found */
#define EWRONGFS	EINVAL		/* Mount with wrong filesystem type */
#define EFSCORRUPTED	EUCLEAN		/* Filesystem is corrupted */

#define SYNCHRONIZE()	barrier()
#define __return_address __builtin_return_address(0)

#define dfltprid	0
#define MAXPATHLEN	1024

#define MIN(a,b)	(min(a,b))
#define MAX(a,b)	(max(a,b))
#define howmany(x, y)	(((x)+((y)-1))/(y))

/*
 * Various platform dependent calls that don't fit anywhere else
 */
#define xfs_sort(a,n,s,fn)	sort(a,n,s,fn,NULL)
#define xfs_stack_trace()	dump_stack()
#define xfs_itruncate_data(ip, off)	\
	(-vmtruncate(VFS_I(ip), (off)))


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

static inline __uint64_t howmany_64(__uint64_t x, __uint32_t y)
{
	x += y - 1;
	do_div(x, y);
	return x;
}

/* ARM old ABI has some weird alignment/padding */
#if defined(__arm__) && !defined(__ARM_EABI__)
#define __arch_pack __attribute__((packed))
#else
#define __arch_pack
#endif

#endif /* __XFS_LINUX__ */
