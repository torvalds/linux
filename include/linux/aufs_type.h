/*
 * Copyright (C) 2005-2011 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __AUFS_TYPE_H__
#define __AUFS_TYPE_H__

#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/types.h>

#define AUFS_VERSION	"3.x-rcN"

/* todo? move this to linux-2.6.19/include/magic.h */
#define AUFS_SUPER_MAGIC	('a' << 24 | 'u' << 16 | 'f' << 8 | 's')

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_BRANCH_MAX_127
typedef __s8 aufs_bindex_t;
#define AUFS_BRANCH_MAX 127
#else
typedef __s16 aufs_bindex_t;
#ifdef CONFIG_AUFS_BRANCH_MAX_511
#define AUFS_BRANCH_MAX 511
#elif defined(CONFIG_AUFS_BRANCH_MAX_1023)
#define AUFS_BRANCH_MAX 1023
#elif defined(CONFIG_AUFS_BRANCH_MAX_32767)
#define AUFS_BRANCH_MAX 32767
#endif
#endif

#ifdef __KERNEL__
#ifndef AUFS_BRANCH_MAX
#error unknown CONFIG_AUFS_BRANCH_MAX value
#endif
#endif /* __KERNEL__ */

/* ---------------------------------------------------------------------- */

#define AUFS_NAME		"aufs"
#define AUFS_FSTYPE		AUFS_NAME

#define AUFS_ROOT_INO		2
#define AUFS_FIRST_INO		11

#define AUFS_WH_PFX		".wh."
#define AUFS_WH_PFX_LEN		((int)sizeof(AUFS_WH_PFX) - 1)
#define AUFS_WH_TMP_LEN		4
/* a limit for rmdir/rename a dir */
#define AUFS_MAX_NAMELEN	(NAME_MAX \
				- AUFS_WH_PFX_LEN * 2	/* doubly whiteouted */\
				- 1			/* dot */\
				- AUFS_WH_TMP_LEN)	/* hex */
#define AUFS_XINO_FNAME		"." AUFS_NAME ".xino"
#define AUFS_XINO_DEFPATH	"/tmp/" AUFS_XINO_FNAME
#define AUFS_XINO_TRUNC_INIT	64 /* blocks */
#define AUFS_XINO_TRUNC_STEP	4  /* blocks */
#define AUFS_DIRWH_DEF		3
#define AUFS_RDCACHE_DEF	10 /* seconds */
#define AUFS_RDCACHE_MAX	3600 /* seconds */
#define AUFS_RDBLK_DEF		512 /* bytes */
#define AUFS_RDHASH_DEF		32
#define AUFS_WKQ_NAME		AUFS_NAME "d"
#define AUFS_WKQ_PRE_NAME	AUFS_WKQ_NAME "_pre"
#define AUFS_MFS_DEF_SEC	30 /* seconds */
#define AUFS_MFS_MAX_SEC	3600 /* seconds */
#define AUFS_PLINK_WARN		100 /* number of plinks */

/* pseudo-link maintenace under /proc */
#define AUFS_PLINK_MAINT_NAME	"plink_maint"
#define AUFS_PLINK_MAINT_DIR	"fs/" AUFS_NAME
#define AUFS_PLINK_MAINT_PATH	AUFS_PLINK_MAINT_DIR "/" AUFS_PLINK_MAINT_NAME

#define AUFS_DIROPQ_NAME	AUFS_WH_PFX ".opq" /* whiteouted doubly */
#define AUFS_WH_DIROPQ		AUFS_WH_PFX AUFS_DIROPQ_NAME

#define AUFS_BASE_NAME		AUFS_WH_PFX AUFS_NAME
#define AUFS_PLINKDIR_NAME	AUFS_WH_PFX "plnk"
#define AUFS_ORPHDIR_NAME	AUFS_WH_PFX "orph"

/* doubly whiteouted */
#define AUFS_WH_BASE		AUFS_WH_PFX AUFS_BASE_NAME
#define AUFS_WH_PLINKDIR	AUFS_WH_PFX AUFS_PLINKDIR_NAME
#define AUFS_WH_ORPHDIR		AUFS_WH_PFX AUFS_ORPHDIR_NAME

/* branch permission */
#define AUFS_BRPERM_RW		"rw"
#define AUFS_BRPERM_RO		"ro"
#define AUFS_BRPERM_RR		"rr"
#define AUFS_BRPERM_WH		"wh"
#define AUFS_BRPERM_NLWH	"nolwh"
#define AUFS_BRPERM_ROWH	AUFS_BRPERM_RO "+" AUFS_BRPERM_WH
#define AUFS_BRPERM_RRWH	AUFS_BRPERM_RR "+" AUFS_BRPERM_WH
#define AUFS_BRPERM_RWNLWH	AUFS_BRPERM_RW "+" AUFS_BRPERM_NLWH

/* ---------------------------------------------------------------------- */

/* ioctl */
enum {
	/* readdir in userspace */
	AuCtl_RDU,
	AuCtl_RDU_INO,

	/* pathconf wrapper */
	AuCtl_WBR_FD,

	/* busy inode */
	AuCtl_IBUSY
};

/* borrowed from linux/include/linux/kernel.h */
#ifndef ALIGN
#define ALIGN(x, a)		__ALIGN_MASK(x, (typeof(x))(a)-1)
#define __ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))
#endif

/* borrowed from linux/include/linux/compiler-gcc3.h */
#ifndef __aligned
#define __aligned(x)			__attribute__((aligned(x)))
#endif

#ifdef __KERNEL__
#ifndef __packed
#define __packed			__attribute__((packed))
#endif
#endif

struct au_rdu_cookie {
	__u64		h_pos;
	__s16		bindex;
	__u8		flags;
	__u8		pad;
	__u32		generation;
} __aligned(8);

struct au_rdu_ent {
	__u64		ino;
	__s16		bindex;
	__u8		type;
	__u8		nlen;
	__u8		wh;
	char		name[0];
} __aligned(8);

static inline int au_rdu_len(int nlen)
{
	/* include the terminating NULL */
	return ALIGN(sizeof(struct au_rdu_ent) + nlen + 1,
		     sizeof(__u64));
}

union au_rdu_ent_ul {
	struct au_rdu_ent __user	*e;
	__u64				ul;
};

enum {
	AufsCtlRduV_SZ,
	AufsCtlRduV_End
};

struct aufs_rdu {
	/* input */
	union {
		__u64		sz;	/* AuCtl_RDU */
		__u64		nent;	/* AuCtl_RDU_INO */
	};
	union au_rdu_ent_ul	ent;
	__u16			verify[AufsCtlRduV_End];

	/* input/output */
	__u32			blk;

	/* output */
	union au_rdu_ent_ul	tail;
	/* number of entries which were added in a single call */
	__u64			rent;
	__u8			full;
	__u8			shwh;

	struct au_rdu_cookie	cookie;
} __aligned(8);

struct aufs_ibusy {
	__u64		ino, h_ino;
	__s16		bindex;
} __aligned(8);

#define AuCtlType		'A'
#define AUFS_CTL_RDU		_IOWR(AuCtlType, AuCtl_RDU, struct aufs_rdu)
#define AUFS_CTL_RDU_INO	_IOWR(AuCtlType, AuCtl_RDU_INO, struct aufs_rdu)
#define AUFS_CTL_WBR_FD		_IO(AuCtlType, AuCtl_WBR_FD)
#define AUFS_CTL_IBUSY		_IOWR(AuCtlType, AuCtl_IBUSY, struct aufs_ibusy)

#endif /* __AUFS_TYPE_H__ */
