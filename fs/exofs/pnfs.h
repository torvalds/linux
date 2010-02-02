/*
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License  version 2 as published by the Free
 * Software Foundation.
 *
 */

/* FIXME: Remove this file once pnfs hits mainline */

#ifndef __EXOFS_PNFS_H__
#define __EXOFS_PNFS_H__

#if ! defined(__PNFS_OSD_XDR_H__)

enum pnfs_iomode {
	IOMODE_READ = 1,
	IOMODE_RW = 2,
	IOMODE_ANY = 3,
};

/* Layout Structure */
enum pnfs_osd_raid_algorithm4 {
	PNFS_OSD_RAID_0		= 1,
	PNFS_OSD_RAID_4		= 2,
	PNFS_OSD_RAID_5		= 3,
	PNFS_OSD_RAID_PQ	= 4     /* Reed-Solomon P+Q */
};

struct pnfs_osd_data_map {
	u32	odm_num_comps;
	u64	odm_stripe_unit;
	u32	odm_group_width;
	u32	odm_group_depth;
	u32	odm_mirror_cnt;
	u32	odm_raid_algorithm;
};

#endif /* ! defined(__PNFS_OSD_XDR_H__) */

#endif /* __EXOFS_PNFS_H__ */
