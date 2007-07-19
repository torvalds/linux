#ifndef __LINUX_UDF_I_H
#define __LINUX_UDF_I_H

#include <linux/udf_fs_i.h>
static inline struct udf_inode_info *UDF_I(struct inode *inode)
{
	return list_entry(inode, struct udf_inode_info, vfs_inode);
}

#define UDF_I_LOCATION(X)	( UDF_I(X)->i_location )
#define UDF_I_LENEATTR(X)	( UDF_I(X)->i_lenEAttr )
#define UDF_I_LENALLOC(X)	( UDF_I(X)->i_lenAlloc )
#define UDF_I_LENEXTENTS(X)	( UDF_I(X)->i_lenExtents )
#define UDF_I_UNIQUE(X)		( UDF_I(X)->i_unique )
#define UDF_I_ALLOCTYPE(X)	( UDF_I(X)->i_alloc_type )
#define UDF_I_EFE(X)		( UDF_I(X)->i_efe )
#define UDF_I_USE(X)		( UDF_I(X)->i_use )
#define UDF_I_STRAT4096(X)	( UDF_I(X)->i_strat4096 )
#define UDF_I_NEXT_ALLOC_BLOCK(X)	( UDF_I(X)->i_next_alloc_block )
#define UDF_I_NEXT_ALLOC_GOAL(X)	( UDF_I(X)->i_next_alloc_goal )
#define UDF_I_CRTIME(X)		( UDF_I(X)->i_crtime )
#define UDF_I_SAD(X)		( UDF_I(X)->i_ext.i_sad )
#define UDF_I_LAD(X)		( UDF_I(X)->i_ext.i_lad )
#define UDF_I_DATA(X)		( UDF_I(X)->i_ext.i_data )

#endif				/* !defined(_LINUX_UDF_I_H) */
