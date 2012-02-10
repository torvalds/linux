/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOUND_SAIF_H__
#define __SOUND_SAIF_H__

struct mxs_saif_platform_data {
	bool master_mode;	/* if true use master mode */
	int master_id;		/* id of the master if in slave mode */
};
#endif
