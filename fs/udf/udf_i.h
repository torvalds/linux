#ifndef __LINUX_UDF_I_H
#define __LINUX_UDF_I_H

#include <linux/udf_fs_i.h>
static inline struct udf_inode_info *UDF_I(struct inode *inode)
{
	return list_entry(inode, struct udf_inode_info, vfs_inode);
}

#endif /* !defined(_LINUX_UDF_I_H) */
