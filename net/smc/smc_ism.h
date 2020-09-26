/* SPDX-License-Identifier: GPL-2.0 */
/* Shared Memory Communications Direct over ISM devices (SMC-D)
 *
 * SMC-D ISM device structure definitions.
 *
 * Copyright IBM Corp. 2018
 */

#ifndef SMCD_ISM_H
#define SMCD_ISM_H

#include <linux/uio.h>
#include <linux/mutex.h>

#include "smc.h"

struct smcd_dev_list {	/* List of SMCD devices */
	struct list_head list;
	struct mutex mutex;	/* Protects list of devices */
};

extern struct smcd_dev_list	smcd_dev_list;	/* list of smcd devices */
extern bool	smc_ism_v2_capable;	/* HW supports ISM V2 and thus
					 * System EID is defined
					 */

struct smc_ism_vlanid {			/* VLAN id set on ISM device */
	struct list_head list;
	unsigned short vlanid;		/* Vlan id */
	refcount_t refcnt;		/* Reference count */
};

struct smc_ism_position {	/* ISM device position to write to */
	u64 token;		/* Token of DMB */
	u32 offset;		/* Offset into DMBE */
	u8 index;		/* Index of DMBE */
	u8 signal;		/* Generate interrupt on owner side */
};

struct smcd_dev;

int smc_ism_cantalk(u64 peer_gid, unsigned short vlan_id, struct smcd_dev *dev);
void smc_ism_set_conn(struct smc_connection *conn);
void smc_ism_unset_conn(struct smc_connection *conn);
int smc_ism_get_vlan(struct smcd_dev *dev, unsigned short vlan_id);
int smc_ism_put_vlan(struct smcd_dev *dev, unsigned short vlan_id);
int smc_ism_register_dmb(struct smc_link_group *lgr, int buf_size,
			 struct smc_buf_desc *dmb_desc);
int smc_ism_unregister_dmb(struct smcd_dev *dev, struct smc_buf_desc *dmb_desc);
int smc_ism_write(struct smcd_dev *dev, const struct smc_ism_position *pos,
		  void *data, size_t len);
int smc_ism_signal_shutdown(struct smc_link_group *lgr);
void smc_ism_get_system_eid(struct smcd_dev *dev, u8 **eid);
void smc_ism_init(void);
#endif
