/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 */
#ifndef _H_JFS_TYPES
#define	_H_JFS_TYPES

/*
 *	jfs_types.h:
 *
 * basic type/utility definitions
 *
 * note: this header file must be the 1st include file
 * of JFS include list in all JFS .c file.
 */

#include <linux/types.h>
#include <linux/nls.h>

/*
 * transaction and lock id's
 *
 * Don't change these without carefully considering the impact on the
 * size and alignment of all of the linelock variants
 */
typedef u16 tid_t;
typedef u16 lid_t;

/*
 * Almost identical to Linux's timespec, but not quite
 */
struct timestruc_t {
	__le32 tv_sec;
	__le32 tv_nsec;
};

/*
 *	handy
 */

#define LEFTMOSTONE	0x80000000
#define	HIGHORDER	0x80000000u	/* high order bit on	*/
#define	ONES		0xffffffffu	/* all bit on		*/

/*
 *	physical xd (pxd)
 *
 *	The leftmost 24 bits of len_addr are the extent length.
 *	The rightmost 8 bits of len_addr are the most signficant bits of
 *	the extent address
 */
typedef struct {
	__le32 len_addr;
	__le32 addr2;
} pxd_t;

/* xd_t field construction */

static inline void PXDlength(pxd_t *pxd, __u32 len)
{
	pxd->len_addr = (pxd->len_addr & cpu_to_le32(~0xffffff)) |
			cpu_to_le32(len & 0xffffff);
}

static inline void PXDaddress(pxd_t *pxd, __u64 addr)
{
	pxd->len_addr = (pxd->len_addr & cpu_to_le32(0xffffff)) |
			cpu_to_le32((addr >> 32)<<24);
	pxd->addr2 = cpu_to_le32(addr & 0xffffffff);
}

/* xd_t field extraction */
static inline __u32 lengthPXD(pxd_t *pxd)
{
	return le32_to_cpu((pxd)->len_addr) & 0xffffff;
}

static inline __u64 addressPXD(pxd_t *pxd)
{
	__u64 n = le32_to_cpu(pxd->len_addr) & ~0xffffff;
	return (n << 8) + le32_to_cpu(pxd->addr2);
}

#define MAXTREEHEIGHT 8
/* pxd list */
struct pxdlist {
	s16 maxnpxd;
	s16 npxd;
	pxd_t pxd[MAXTREEHEIGHT];
};


/*
 *	data extent descriptor (dxd)
 */
typedef struct {
	__u8 flag;	/* 1: flags */
	__u8 rsrvd[3];
	__le32 size;		/* 4: size in byte */
	pxd_t loc;		/* 8: address and length in unit of fsblksize */
} dxd_t;			/* - 16 - */

/* dxd_t flags */
#define	DXD_INDEX	0x80	/* B+-tree index */
#define	DXD_INLINE	0x40	/* in-line data extent */
#define	DXD_EXTENT	0x20	/* out-of-line single extent */
#define	DXD_FILE	0x10	/* out-of-line file (inode) */
#define DXD_CORRUPT	0x08	/* Inconsistency detected */

/* dxd_t field construction
 */
#define	DXDlength(dxd, len)	PXDlength(&(dxd)->loc, len)
#define	DXDaddress(dxd, addr)	PXDaddress(&(dxd)->loc, addr)
#define	lengthDXD(dxd)	lengthPXD(&(dxd)->loc)
#define	addressDXD(dxd)	addressPXD(&(dxd)->loc)
#define DXDsize(dxd, size32) ((dxd)->size = cpu_to_le32(size32))
#define sizeDXD(dxd)	le32_to_cpu((dxd)->size)

/*
 *	directory entry argument
 */
struct component_name {
	int namlen;
	wchar_t *name;
};


/*
 *	DASD limit information - stored in directory inode
 */
struct dasd {
	u8 thresh;		/* Alert Threshold (in percent)		*/
	u8 delta;		/* Alert Threshold delta (in percent)	*/
	u8 rsrvd1;
	u8 limit_hi;		/* DASD limit (in logical blocks)	*/
	__le32 limit_lo;	/* DASD limit (in logical blocks)	*/
	u8 rsrvd2[3];
	u8 used_hi;		/* DASD usage (in logical blocks)	*/
	__le32 used_lo;		/* DASD usage (in logical blocks)	*/
};

#define DASDLIMIT(dasdp) \
	(((u64)((dasdp)->limit_hi) << 32) + __le32_to_cpu((dasdp)->limit_lo))
#define setDASDLIMIT(dasdp, limit)\
{\
	(dasdp)->limit_hi = ((u64)limit) >> 32;\
	(dasdp)->limit_lo = __cpu_to_le32(limit);\
}
#define DASDUSED(dasdp) \
	(((u64)((dasdp)->used_hi) << 32) + __le32_to_cpu((dasdp)->used_lo))
#define setDASDUSED(dasdp, used)\
{\
	(dasdp)->used_hi = ((u64)used) >> 32;\
	(dasdp)->used_lo = __cpu_to_le32(used);\
}

#endif				/* !_H_JFS_TYPES */
