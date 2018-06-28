// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009, Christoph Hellwig
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_da_format.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_da_btree.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_trans.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_buf_item.h"
#include "xfs_quota.h"
#include "xfs_iomap.h"
#include "xfs_aops.h"
#include "xfs_dquot_item.h"
#include "xfs_dquot.h"
#include "xfs_log_recover.h"
#include "xfs_inode_item.h"
#include "xfs_bmap_btree.h"
#include "xfs_filestream.h"
#include "xfs_fsmap.h"

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "xfs_trace.h"
