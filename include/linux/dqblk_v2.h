/*
 *  Definitions for vfsv0 quota format
 */

#ifndef _LINUX_DQBLK_V2_H
#define _LINUX_DQBLK_V2_H

#include <linux/dqblk_qtree.h>

/* Id number of quota format */
#define QFMT_VFS_V0 2

/* Numbers of blocks needed for updates */
#define V2_INIT_ALLOC QTREE_INIT_ALLOC
#define V2_INIT_REWRITE QTREE_INIT_REWRITE
#define V2_DEL_ALLOC QTREE_DEL_ALLOC
#define V2_DEL_REWRITE QTREE_DEL_REWRITE

struct v2_mem_dqinfo {
	struct qtree_mem_dqinfo i;
};

#endif /* _LINUX_DQBLK_V2_H */
