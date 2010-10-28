/*
 * Copyright (C) ST-Ericsson AB 2010
 * Contact: Sjur Brendeland / sjur.brandeland@stericsson.com
 * Author: Amarnath Revanna / amarnath.bangalore.revanna@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CAIF_SHM_H_
#define CAIF_SHM_H_

struct shmdev_layer {
	u32 shm_base_addr;
	u32 shm_total_sz;
	u32 shm_id;
	u32 shm_loopback;
	void *hmbx;
	int (*pshmdev_mbxsend) (u32 shm_id, u32 mbx_msg);
	int (*pshmdev_mbxsetup) (void *pshmdrv_cb,
				struct shmdev_layer *pshm_dev, void *pshm_drv);
	struct net_device *pshm_netdev;
};

extern int caif_shmcore_probe(struct shmdev_layer *pshm_dev);
extern void caif_shmcore_remove(struct net_device *pshm_netdev);

#endif
