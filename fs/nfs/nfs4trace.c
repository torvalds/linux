// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013 Trond Myklebust <Trond.Myklebust@netapp.com>
 */
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "internal.h"
#include "nfs4session.h"
#include "callback.h"
#include "pnfs.h"

#define CREATE_TRACE_POINTS
#include "nfs4trace.h"

#ifdef CONFIG_NFS_V4_1
EXPORT_TRACEPOINT_SYMBOL_GPL(nfs4_pnfs_read);
EXPORT_TRACEPOINT_SYMBOL_GPL(nfs4_pnfs_write);
EXPORT_TRACEPOINT_SYMBOL_GPL(nfs4_pnfs_commit_ds);

EXPORT_TRACEPOINT_SYMBOL_GPL(pnfs_mds_fallback_pg_init_read);
EXPORT_TRACEPOINT_SYMBOL_GPL(pnfs_mds_fallback_pg_init_write);
EXPORT_TRACEPOINT_SYMBOL_GPL(pnfs_mds_fallback_pg_get_mirror_count);
EXPORT_TRACEPOINT_SYMBOL_GPL(pnfs_mds_fallback_read_done);
EXPORT_TRACEPOINT_SYMBOL_GPL(pnfs_mds_fallback_write_done);
EXPORT_TRACEPOINT_SYMBOL_GPL(pnfs_mds_fallback_read_pagelist);
EXPORT_TRACEPOINT_SYMBOL_GPL(pnfs_mds_fallback_write_pagelist);

EXPORT_TRACEPOINT_SYMBOL_GPL(ff_layout_read_error);
EXPORT_TRACEPOINT_SYMBOL_GPL(ff_layout_write_error);
EXPORT_TRACEPOINT_SYMBOL_GPL(ff_layout_commit_error);

EXPORT_TRACEPOINT_SYMBOL_GPL(fl_getdevinfo);
#endif
