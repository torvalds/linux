/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __AUFS_TYPE_H__
#define __AUFS_TYPE_H__

#define AUFS_NAME	"aufs"

#ifdef __KERNEL__
/*
 * define it before including all other headers.
 * sched.h may use pr_* macros before defining "current", so define the
 * no-current version first, and re-define later.
 */
#define pr_fmt(fmt)	AUFS_NAME " %s:%d: " fmt, __func__, __LINE__
#include <linux/sched.h>
#undef pr_fmt
#define pr_fmt(fmt) \
		AUFS_NAME " %s:%d:%.*s[%d]: " fmt, __func__, __LINE__, \
		(int)sizeof(current->comm), current->comm, current->pid
#else
#include <stdint.h>
#include <sys/types.h>
#endif /* __KERNEL__ */

#include <linux/limits.h>

#define AUFS_VERSION	"3.9"

/* todo? move this to linux-2.6.19/include/magic.h */
#define AUFS_SUPER_MAGIC	('a' << 24 | 'u' << 16 | 'f' << 8 | 's')

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_BRANCH_MAX_127
typedef int8_t aufs_bindex_t;
#define AUFS_BRANCH_MAX 127
#else
typedef int16_t aufs_bindex_t;
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

#define AUFS_FSTYPE		AUFS_NAME

#define AUFS_ROOT_INO		2
#define AUFS_FIRST_INO		11

#define AUFS_WH_PFX		".wh."
#define AUFS_WH_PFX_LEN		((int)sizeof(AUFS_WH_PFX) - 1)
#define AUFS_WH_TMP_LEN		4
/* a limit for rmdir/rename a dir and copyup */
#define AUFS_MAX_NAMELEN	(NAME_MAX \
				- AUFS_WH_PFX_LEN * 2	/* doubly whiteouted */\
				- 1			/* dot */\
				- AUFS_WH_TMP_LEN)	/* hex */
#define AUFS_XINO_FNAME		"." AUFS_NAME ".xino"
#define AUFS_XINO_DEFPATH	"/tmp/" AUFS_XINO_FNAME
#define AUFS_XINO_DEF_SEC	30 /* seconds */
#define AUFS_XINO_DEF_TRUNC	45 /* percentage */
#define AUFS_DIRWH_DEF		3
#define AUFS_RDCACHE_DEF	10 /* seconds */
#define AUFS_RDCACHE_MAX	3600 /* seconds */
#define AUFS_RDBLK_DEF		512 /* bytes */
#define AUFS_RDHASH_DEF		32
#define AUFS_WKQ_NAME		AUFS_NAME "d"
#define AUFS_MFS_DEF_SEC	30 /* seconds */
#define AUFS_MFS_MAX_SEC	3600 /* seconds */
#define AUFS_PLINK_WARN		50 /* number of plinks in a single bucket */

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

/* branch permissions and attributes */
#define AUFS_BRPERM_RW		"rw"
#define AUFS_BRPERM_RO		"ro"
#define AUFS_BRPERM_RR		"rr"
#define AUFS_BRATTR_COO_REG	"coo_reg"
#define AUFS_BRATTR_COO_ALL	"coo_all"
#define AUFS_BRATTR_UNPIN	"unpin"
#define AUFS_BRRATTR_WH		"wh"
#define AUFS_BRWATTR_NLWH	"nolwh"
#define AUFS_BRWATTR_MOO	"moo"

/* ---------------------------------------------------------------------- */

/* ioctl */
enum {
	/* readdir in userspace */
	AuCtl_RDU,
	AuCtl_RDU_INO,

	/* pathconf wrapper */
	AuCtl_WBR_FD,

	/* busy inode */
	AuCtl_IBUSY,

	/* move-down */
	AuCtl_MVDOWN
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
	uint64_t	h_pos;
	int16_t		bindex;
	uint8_t		flags;
	uint8_t		pad;
	uint32_t	generation;
} __aligned(8);

struct au_rdu_ent {
	uint64_t	ino;
	int16_t		bindex;
	uint8_t		type;
	uint8_t		nlen;
	uint8_t		wh;
	char		name[0];
} __aligned(8);

static inline int au_rdu_len(int nlen)
{
	/* include the terminating NULL */
	return ALIGN(sizeof(struct au_rdu_ent) + nlen + 1,
		     sizeof(uint64_t));
}

union au_rdu_ent_ul {
	struct au_rdu_ent __user	*e;
	uint64_t			ul;
};

enum {
	AufsCtlRduV_SZ,
	AufsCtlRduV_End
};

struct aufs_rdu {
	/* input */
	union {
		uint64_t	sz;	/* AuCtl_RDU */
		uint64_t	nent;	/* AuCtl_RDU_INO */
	};
	union au_rdu_ent_ul	ent;
	uint16_t		verify[AufsCtlRduV_End];

	/* input/output */
	uint32_t		blk;

	/* output */
	union au_rdu_ent_ul	tail;
	/* number of entries which were added in a single call */
	uint64_t		rent;
	uint8_t			full;
	uint8_t			shwh;

	struct au_rdu_cookie	cookie;
} __aligned(8);

/* ---------------------------------------------------------------------- */

struct aufs_wbr_fd {
	uint32_t	oflags;
	int16_t		brid;
} __aligned(8);

/* ---------------------------------------------------------------------- */

struct aufs_ibusy {
	uint64_t	ino, h_ino;
	int16_t		bindex;
} __aligned(8);

/* ---------------------------------------------------------------------- */

/* error code for move-down */
/* the actual message strings are implemented in aufs-util.git */
enum {
	EAU_MVDOWN_OPAQUE = 1,
	EAU_MVDOWN_WHITEOUT,
	EAU_MVDOWN_UPPER,
	EAU_MVDOWN_BOTTOM,
	EAU_MVDOWN_NOUPPER,
	EAU_MVDOWN_NOLOWERBR,
	EAU_Last
};

/* flags for move-down */
#define AUFS_MVDOWN_DMSG	1
#define AUFS_MVDOWN_OWLOWER	(1 << 1)	/* overwrite lower */
#define AUFS_MVDOWN_KUPPER	(1 << 2)	/* keep upper */
#define AUFS_MVDOWN_ROLOWER	(1 << 3)	/* do even if lower is RO */
#define AUFS_MVDOWN_ROLOWER_R	(1 << 4)	/* did on lower RO */
#define AUFS_MVDOWN_ROUPPER	(1 << 5)	/* do even if upper is RO */
#define AUFS_MVDOWN_ROUPPER_R	(1 << 6)	/* did on upper RO */
#define AUFS_MVDOWN_BRID_UPPER	(1 << 7)	/* upper brid */
#define AUFS_MVDOWN_BRID_LOWER	(1 << 8)	/* lower brid */
#define AUFS_MVDOWN_STFS	(1 << 9)	/* req. stfs */
#define AUFS_MVDOWN_STFS_FAILED	(1 << 10)	/* output: stfs is unusable */
#define AUFS_MVDOWN_BOTTOM	(1 << 11)	/* output: no more lowers */

/* index for move-down */
enum {
	AUFS_MVDOWN_UPPER,
	AUFS_MVDOWN_LOWER,
	AUFS_MVDOWN_NARRAY
};

/*
 * additional info of move-down
 * number of free blocks and inodes.
 * subset of struct kstatfs, but smaller and always 64bit.
 */
struct aufs_stfs {
	uint64_t	f_blocks;
	uint64_t	f_bavail;
	uint64_t	f_files;
	uint64_t	f_ffree;
};

struct aufs_stbr {
	int16_t			brid;	/* optional input */
	int16_t			bindex;	/* output */
	struct aufs_stfs	stfs;	/* output when AUFS_MVDOWN_STFS set */
} __aligned(8);

struct aufs_mvdown {
	uint32_t		flags;			/* input/output */
	struct aufs_stbr	stbr[AUFS_MVDOWN_NARRAY]; /* input/output */
	int8_t			au_errno;		/* output */
} __aligned(8);

/* ---------------------------------------------------------------------- */

#define AuCtlType		'A'
#define AUFS_CTL_RDU		_IOWR(AuCtlType, AuCtl_RDU, struct aufs_rdu)
#define AUFS_CTL_RDU_INO	_IOWR(AuCtlType, AuCtl_RDU_INO, struct aufs_rdu)
#define AUFS_CTL_WBR_FD		_IOW(AuCtlType, AuCtl_WBR_FD, \
				     struct aufs_wbr_fd)
#define AUFS_CTL_IBUSY		_IOWR(AuCtlType, AuCtl_IBUSY, struct aufs_ibusy)
#define AUFS_CTL_MVDOWN		_IOWR(AuCtlType, AuCtl_MVDOWN, \
				      struct aufs_mvdown)

#endif /* __AUFS_TYPE_H__ */
