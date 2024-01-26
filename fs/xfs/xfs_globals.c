// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_error.h"

/*
 * Tunable XFS parameters.  xfs_params is required even when CONFIG_SYSCTL=n,
 * other XFS code uses these values.  Times are measured in centisecs (i.e.
 * 100ths of a second) with the exception of blockgc_timer, which is measured
 * in seconds.
 */
xfs_param_t xfs_params = {
			  /*	MIN		DFLT		MAX	*/
	.sgid_inherit	= {	0,		0,		1	},
	.symlink_mode	= {	0,		0,		1	},
	.panic_mask	= {	0,		0,		XFS_PTAG_MASK},
	.error_level	= {	0,		3,		11	},
	.syncd_timer	= {	1*100,		30*100,		7200*100},
	.stats_clear	= {	0,		0,		1	},
	.inherit_sync	= {	0,		1,		1	},
	.inherit_nodump	= {	0,		1,		1	},
	.inherit_noatim = {	0,		1,		1	},
	.xfs_buf_timer	= {	100/2,		1*100,		30*100	},
	.xfs_buf_age	= {	1*100,		15*100,		7200*100},
	.inherit_nosym	= {	0,		0,		1	},
	.rotorstep	= {	1,		1,		255	},
	.inherit_nodfrg	= {	0,		1,		1	},
	.fstrm_timer	= {	1,		30*100,		3600*100},
	.blockgc_timer	= {	1,		300,		3600*24},
};

struct xfs_globals xfs_globals = {
	.log_recovery_delay	=	0,	/* no delay by default */
	.mount_delay		=	0,	/* no delay by default */
#ifdef XFS_ASSERT_FATAL
	.bug_on_assert		=	true,	/* assert failures BUG() */
#else
	.bug_on_assert		=	false,	/* assert failures WARN() */
#endif
#ifdef DEBUG
	.pwork_threads		=	-1,	/* automatic thread detection */
	.larp			=	false,	/* log attribute replay */
#endif

	/*
	 * Leave this many record slots empty when bulk loading btrees.  By
	 * default we load new btree leaf blocks 75% full.
	 */
	.bload_leaf_slack	=	-1,

	/*
	 * Leave this many key/ptr slots empty when bulk loading btrees.  By
	 * default we load new btree node blocks 75% full.
	 */
	.bload_node_slack	=	-1,
};
