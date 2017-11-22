/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#ifndef _SYS_XVATTR_H
#define	_SYS_XVATTR_H

#include <sys/vnode.h>

#define	AV_SCANSTAMP_SZ	32		/* length of anti-virus scanstamp */

/*
 * Structure of all optional attributes.
 */
typedef struct xoptattr {
	timestruc_t	xoa_createtime;	/* Create time of file */
	uint8_t		xoa_archive;
	uint8_t		xoa_system;
	uint8_t		xoa_readonly;
	uint8_t		xoa_hidden;
	uint8_t		xoa_nounlink;
	uint8_t		xoa_immutable;
	uint8_t		xoa_appendonly;
	uint8_t		xoa_nodump;
	uint8_t		xoa_opaque;
	uint8_t		xoa_av_quarantined;
	uint8_t		xoa_av_modified;
	uint8_t		xoa_av_scanstamp[AV_SCANSTAMP_SZ];
	uint8_t		xoa_reparse;
	uint64_t	xoa_generation;
	uint8_t		xoa_offline;
	uint8_t		xoa_sparse;
} xoptattr_t;

/*
 * The xvattr structure is really a variable length structure that
 * is made up of:
 * - The classic vattr_t (xva_vattr)
 * - a 32 bit quantity (xva_mapsize) that specifies the size of the
 *   attribute bitmaps in 32 bit words.
 * - A pointer to the returned attribute bitmap (needed because the
 *   previous element, the requested attribute bitmap) is variable length.
 * - The requested attribute bitmap, which is an array of 32 bit words.
 *   Callers use the XVA_SET_REQ() macro to set the bits corresponding to
 *   the attributes that are being requested.
 * - The returned attribute bitmap, which is an array of 32 bit words.
 *   File systems that support optional attributes use the XVA_SET_RTN()
 *   macro to set the bits corresponding to the attributes that are being
 *   returned.
 * - The xoptattr_t structure which contains the attribute values
 *
 * xva_mapsize determines how many words in the attribute bitmaps.
 * Immediately following the attribute bitmaps is the xoptattr_t.
 * xva_getxoptattr() is used to get the pointer to the xoptattr_t
 * section.
 */

#define	XVA_MAPSIZE	3		/* Size of attr bitmaps */
#define	XVA_MAGIC	0x78766174	/* Magic # for verification */

/*
 * The xvattr structure is an extensible structure which permits optional
 * attributes to be requested/returned.  File systems may or may not support
 * optional attributes.  They do so at their own discretion but if they do
 * support optional attributes, they must register the VFSFT_XVATTR feature
 * so that the optional attributes can be set/retrieved.
 *
 * The fields of the xvattr structure are:
 *
 * xva_vattr - The first element of an xvattr is a legacy vattr structure
 * which includes the common attributes.  If AT_XVATTR is set in the va_mask
 * then the entire structure is treated as an xvattr.  If AT_XVATTR is not
 * set, then only the xva_vattr structure can be used.
 *
 * xva_magic - 0x78766174 (hex for "xvat"). Magic number for verification.
 *
 * xva_mapsize - Size of requested and returned attribute bitmaps.
 *
 * xva_rtnattrmapp - Pointer to xva_rtnattrmap[].  We need this since the
 * size of the array before it, xva_reqattrmap[], could change which means
 * the location of xva_rtnattrmap[] could change.  This will allow unbundled
 * file systems to find the location of xva_rtnattrmap[] when the sizes change.
 *
 * xva_reqattrmap[] - Array of requested attributes.  Attributes are
 * represented by a specific bit in a specific element of the attribute
 * map array.  Callers set the bits corresponding to the attributes
 * that the caller wants to get/set.
 *
 * xva_rtnattrmap[] - Array of attributes that the file system was able to
 * process.  Not all file systems support all optional attributes.  This map
 * informs the caller which attributes the underlying file system was able
 * to set/get.  (Same structure as the requested attributes array in terms
 * of each attribute  corresponding to specific bits and array elements.)
 *
 * xva_xoptattrs - Structure containing values of optional attributes.
 * These values are only valid if the corresponding bits in xva_reqattrmap
 * are set and the underlying file system supports those attributes.
 */
typedef struct xvattr {
	vattr_t		xva_vattr;	/* Embedded vattr structure */
	uint32_t	xva_magic;	/* Magic Number */
	uint32_t	xva_mapsize;	/* Size of attr bitmap (32-bit words) */
	uint32_t	*xva_rtnattrmapp;	/* Ptr to xva_rtnattrmap[] */
	uint32_t	xva_reqattrmap[XVA_MAPSIZE];	/* Requested attrs */
	uint32_t	xva_rtnattrmap[XVA_MAPSIZE];	/* Returned attrs */
	xoptattr_t	xva_xoptattrs;	/* Optional attributes */
} xvattr_t;

/*
 * Attribute bits used in the extensible attribute's (xva's) attribute
 * bitmaps.  Note that the bitmaps are made up of a variable length number
 * of 32-bit words.  The convention is to use XAT{n}_{attrname} where "n"
 * is the element in the bitmap (starting at 1).  This convention is for
 * the convenience of the maintainer to keep track of which element each
 * attribute belongs to.
 *
 * NOTE THAT CONSUMERS MUST *NOT* USE THE XATn_* DEFINES DIRECTLY.  CONSUMERS
 * MUST USE THE XAT_* DEFINES.
 */
#define	XAT0_INDEX	0LL		/* Index into bitmap for XAT0 attrs */
#define	XAT0_CREATETIME	0x00000001	/* Create time of file */
#define	XAT0_ARCHIVE	0x00000002	/* Archive */
#define	XAT0_SYSTEM	0x00000004	/* System */
#define	XAT0_READONLY	0x00000008	/* Readonly */
#define	XAT0_HIDDEN	0x00000010	/* Hidden */
#define	XAT0_NOUNLINK	0x00000020	/* Nounlink */
#define	XAT0_IMMUTABLE	0x00000040	/* immutable */
#define	XAT0_APPENDONLY	0x00000080	/* appendonly */
#define	XAT0_NODUMP	0x00000100	/* nodump */
#define	XAT0_OPAQUE	0x00000200	/* opaque */
#define	XAT0_AV_QUARANTINED	0x00000400	/* anti-virus quarantine */
#define	XAT0_AV_MODIFIED	0x00000800	/* anti-virus modified */
#define	XAT0_AV_SCANSTAMP	0x00001000	/* anti-virus scanstamp */
#define	XAT0_REPARSE	0x00002000	/* FS reparse point */
#define	XAT0_GEN	0x00004000	/* object generation number */
#define	XAT0_OFFLINE	0x00008000	/* offline */
#define	XAT0_SPARSE	0x00010000	/* sparse */

#define	XAT0_ALL_ATTRS	(XAT0_CREATETIME|XAT0_ARCHIVE|XAT0_SYSTEM| \
    XAT0_READONLY|XAT0_HIDDEN|XAT0_NOUNLINK|XAT0_IMMUTABLE|XAT0_APPENDONLY| \
    XAT0_NODUMP|XAT0_OPAQUE|XAT0_AV_QUARANTINED|  XAT0_AV_MODIFIED| \
    XAT0_AV_SCANSTAMP|XAT0_REPARSE|XATO_GEN|XAT0_OFFLINE|XAT0_SPARSE)

/* Support for XAT_* optional attributes */
#define	XVA_MASK		0xffffffff	/* Used to mask off 32 bits */
#define	XVA_SHFT		32		/* Used to shift index */

/*
 * Used to pry out the index and attribute bits from the XAT_* attributes
 * defined below.  Note that we're masking things down to 32 bits then
 * casting to uint32_t.
 */
#define	XVA_INDEX(attr)		((uint32_t)(((attr) >> XVA_SHFT) & XVA_MASK))
#define	XVA_ATTRBIT(attr)	((uint32_t)((attr) & XVA_MASK))

/*
 * The following defines present a "flat namespace" so that consumers don't
 * need to keep track of which element belongs to which bitmap entry.
 *
 * NOTE THAT THESE MUST NEVER BE OR-ed TOGETHER
 */
#define	XAT_CREATETIME		((XAT0_INDEX << XVA_SHFT) | XAT0_CREATETIME)
#define	XAT_ARCHIVE		((XAT0_INDEX << XVA_SHFT) | XAT0_ARCHIVE)
#define	XAT_SYSTEM		((XAT0_INDEX << XVA_SHFT) | XAT0_SYSTEM)
#define	XAT_READONLY		((XAT0_INDEX << XVA_SHFT) | XAT0_READONLY)
#define	XAT_HIDDEN		((XAT0_INDEX << XVA_SHFT) | XAT0_HIDDEN)
#define	XAT_NOUNLINK		((XAT0_INDEX << XVA_SHFT) | XAT0_NOUNLINK)
#define	XAT_IMMUTABLE		((XAT0_INDEX << XVA_SHFT) | XAT0_IMMUTABLE)
#define	XAT_APPENDONLY		((XAT0_INDEX << XVA_SHFT) | XAT0_APPENDONLY)
#define	XAT_NODUMP		((XAT0_INDEX << XVA_SHFT) | XAT0_NODUMP)
#define	XAT_OPAQUE		((XAT0_INDEX << XVA_SHFT) | XAT0_OPAQUE)
#define	XAT_AV_QUARANTINED	((XAT0_INDEX << XVA_SHFT) | XAT0_AV_QUARANTINED)
#define	XAT_AV_MODIFIED		((XAT0_INDEX << XVA_SHFT) | XAT0_AV_MODIFIED)
#define	XAT_AV_SCANSTAMP	((XAT0_INDEX << XVA_SHFT) | XAT0_AV_SCANSTAMP)
#define	XAT_REPARSE		((XAT0_INDEX << XVA_SHFT) | XAT0_REPARSE)
#define	XAT_GEN			((XAT0_INDEX << XVA_SHFT) | XAT0_GEN)
#define	XAT_OFFLINE		((XAT0_INDEX << XVA_SHFT) | XAT0_OFFLINE)
#define	XAT_SPARSE		((XAT0_INDEX << XVA_SHFT) | XAT0_SPARSE)

/*
 * The returned attribute map array (xva_rtnattrmap[]) is located past the
 * requested attribute map array (xva_reqattrmap[]).  Its location changes
 * when the array sizes change.  We use a separate pointer in a known location
 * (xva_rtnattrmapp) to hold the location of xva_rtnattrmap[].  This is
 * set in xva_init()
 */
#define	XVA_RTNATTRMAP(xvap)	((xvap)->xva_rtnattrmapp)

/*
 * XVA_SET_REQ() sets an attribute bit in the proper element in the bitmap
 * of requested attributes (xva_reqattrmap[]).
 */
#define	XVA_SET_REQ(xvap, attr)					\
	ASSERT((xvap)->xva_vattr.va_mask & AT_XVATTR);		\
	ASSERT((xvap)->xva_magic == XVA_MAGIC);			\
	(xvap)->xva_reqattrmap[XVA_INDEX(attr)] |= XVA_ATTRBIT(attr)
/*
 * XVA_CLR_REQ() clears an attribute bit in the proper element in the bitmap
 * of requested attributes (xva_reqattrmap[]).
 */
#define	XVA_CLR_REQ(xvap, attr)					\
	ASSERT((xvap)->xva_vattr.va_mask & AT_XVATTR);		\
	ASSERT((xvap)->xva_magic == XVA_MAGIC);			\
	(xvap)->xva_reqattrmap[XVA_INDEX(attr)] &= ~XVA_ATTRBIT(attr)

/*
 * XVA_SET_RTN() sets an attribute bit in the proper element in the bitmap
 * of returned attributes (xva_rtnattrmap[]).
 */
#define	XVA_SET_RTN(xvap, attr)					\
	ASSERT((xvap)->xva_vattr.va_mask & AT_XVATTR);		\
	ASSERT((xvap)->xva_magic == XVA_MAGIC);			\
	(XVA_RTNATTRMAP(xvap))[XVA_INDEX(attr)] |= XVA_ATTRBIT(attr)

/*
 * XVA_ISSET_REQ() checks the requested attribute bitmap (xva_reqattrmap[])
 * to see of the corresponding attribute bit is set.  If so, returns non-zero.
 */
#define	XVA_ISSET_REQ(xvap, attr)					\
	((((xvap)->xva_vattr.va_mask & AT_XVATTR) &&			\
		((xvap)->xva_magic == XVA_MAGIC) &&			\
		((xvap)->xva_mapsize > XVA_INDEX(attr))) ?		\
	((xvap)->xva_reqattrmap[XVA_INDEX(attr)] & XVA_ATTRBIT(attr)) :	0)

/*
 * XVA_ISSET_RTN() checks the returned attribute bitmap (xva_rtnattrmap[])
 * to see of the corresponding attribute bit is set.  If so, returns non-zero.
 */
#define	XVA_ISSET_RTN(xvap, attr)					\
	((((xvap)->xva_vattr.va_mask & AT_XVATTR) &&			\
		((xvap)->xva_magic == XVA_MAGIC) &&			\
		((xvap)->xva_mapsize > XVA_INDEX(attr))) ?		\
	((XVA_RTNATTRMAP(xvap))[XVA_INDEX(attr)] & XVA_ATTRBIT(attr)) : 0)

/*
 * Zero out the structure, set the size of the requested/returned bitmaps,
 * set AT_XVATTR in the embedded vattr_t's va_mask, and set up the pointer
 * to the returned attributes array.
 */
static inline void
xva_init(xvattr_t *xvap)
{
	bzero(xvap, sizeof (xvattr_t));
	xvap->xva_mapsize = XVA_MAPSIZE;
	xvap->xva_magic = XVA_MAGIC;
	xvap->xva_vattr.va_mask = ATTR_XVATTR;
	xvap->xva_rtnattrmapp = &(xvap->xva_rtnattrmap)[0];
}

/*
 * If AT_XVATTR is set, returns a pointer to the embedded xoptattr_t
 * structure.  Otherwise, returns NULL.
 */
static inline xoptattr_t *
xva_getxoptattr(xvattr_t *xvap)
{
	xoptattr_t *xoap = NULL;
	if (xvap->xva_vattr.va_mask & AT_XVATTR)
			xoap = &xvap->xva_xoptattrs;
	return (xoap);
}

#define	MODEMASK	07777		/* mode bits plus permission bits */
#define	PERMMASK	00777		/* permission bits */

/*
 * VOP_ACCESS flags
 */
#define	V_ACE_MASK	0x1	/* mask represents  NFSv4 ACE permissions */
#define	V_APPEND	0x2	/* want to do append only check */

/*
 * Structure used on VOP_GETSECATTR and VOP_SETSECATTR operations
 */

typedef struct vsecattr {
	uint_t		vsa_mask;	/* See below */
	int		vsa_aclcnt;	/* ACL entry count */
	void		*vsa_aclentp;	/* pointer to ACL entries */
	int		vsa_dfaclcnt;	/* default ACL entry count */
	void		*vsa_dfaclentp;	/* pointer to default ACL entries */
	size_t		vsa_aclentsz;	/* ACE size in bytes of vsa_aclentp */
	uint_t		vsa_aclflags;	/* ACE ACL flags */
} vsecattr_t;

/* vsa_mask values */
#define	VSA_ACL			0x0001
#define	VSA_ACLCNT		0x0002
#define	VSA_DFACL		0x0004
#define	VSA_DFACLCNT		0x0008
#define	VSA_ACE			0x0010
#define	VSA_ACECNT		0x0020
#define	VSA_ACE_ALLTYPES	0x0040
#define	VSA_ACE_ACLFLAGS	0x0080	/* get/set ACE ACL flags */

#endif /* _SYS_XVATTR_H */
