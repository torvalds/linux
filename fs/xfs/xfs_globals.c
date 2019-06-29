// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"

/*
 * Tunable XFS parameters.  xfs_params is required even when CONFIG_SYSCTL=n,
 * other XFS code uses these values.  Times are measured in centisecs (i.e.
 * 100ths of a second) with the exception of eofb_timer and cowb_timer, which
 * are measured in seconds.
 */
xfs_param_t xfs_params = {
			  /*	MIN		DFLT		MAX	*/
	.sgid_inherit	= {	0,		0,		1	},
	.symlink_mode	= {	0,		0,		1	},
	.panic_mask	= {	0,		0,		256	},
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
	.eofb_timer	= {	1,		300,		3600*24},
	.cowb_timer	= {	1,		1800,		3600*24},
};

struct xfs_globals xfs_globals = {
	.log_recovery_delay	=	0,	/* no delay by default */
	.mount_delay		=	0,	/* no delay by default */
#ifdef XFS_ASSERT_FATAL
	.bug_on_assert		=	true,	/* assert failures BUG() */
#else
	.bug_on_assert		=	false,	/* assert failures WARN() */
#endif
};
