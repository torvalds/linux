/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_ATTR_SF_H__
#define	__XFS_ATTR_SF_H__

/*
 * We generate this then sort it, attr_list() must return things in hash-order.
 */
typedef struct xfs_attr_sf_sort {
	uint8_t		entno;		/* entry number in original list */
	uint8_t		namelen;	/* length of name value (no null) */
	uint8_t		valuelen;	/* length of value */
	uint8_t		flags;		/* flags bits (see xfs_attr_leaf.h) */
	xfs_dahash_t	hash;		/* this entry's hash value */
	unsigned char	*name;		/* name value, pointer into buffer */
	void		*value;
} xfs_attr_sf_sort_t;

#define XFS_ATTR_SF_ENTSIZE_MAX			/* max space for name&value */ \
	((1 << (NBBY*(int)sizeof(uint8_t))) - 1)

/* space name/value uses */
static inline int xfs_attr_sf_entsize_byname(uint8_t nlen, uint8_t vlen)
{
	return sizeof(struct xfs_attr_sf_entry) + nlen + vlen;
}

/* space an entry uses */
static inline int xfs_attr_sf_entsize(struct xfs_attr_sf_entry *sfep)
{
	return struct_size(sfep, nameval, sfep->namelen + sfep->valuelen);
}

/* first entry in the SF attr fork */
static inline struct xfs_attr_sf_entry *
xfs_attr_sf_firstentry(struct xfs_attr_sf_hdr *hdr)
{
	return (struct xfs_attr_sf_entry *)(hdr + 1);
}

/* next entry after sfep */
static inline struct xfs_attr_sf_entry *
xfs_attr_sf_nextentry(struct xfs_attr_sf_entry *sfep)
{
	return (void *)sfep + xfs_attr_sf_entsize(sfep);
}

/* pointer to the space after the last entry, e.g. for adding a new one */
static inline struct xfs_attr_sf_entry *
xfs_attr_sf_endptr(struct xfs_attr_sf_hdr *sf)
{
	return (void *)sf + be16_to_cpu(sf->totsize);
}

#endif	/* __XFS_ATTR_SF_H__ */
