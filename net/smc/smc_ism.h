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
#include <linux/types.h>
#include <linux/mutex.h>

#include "smc.h"

#define SMC_VIRTUAL_ISM_CHID_MASK	0xFF00

struct smcd_dev_list {	/* List of SMCD devices */
	struct list_head list;
	struct mutex mutex;	/* Protects list of devices */
};

extern struct smcd_dev_list	smcd_dev_list;	/* list of smcd devices */

struct smc_ism_vlanid {			/* VLAN id set on ISM device */
	struct list_head list;
	unsigned short vlanid;		/* Vlan id */
	refcount_t refcnt;		/* Reference count */
};

struct smcd_dev;

int smc_ism_cantalk(struct smcd_gid *peer_gid, unsigned short vlan_id,
		    struct smcd_dev *dev);
void smc_ism_set_conn(struct smc_connection *conn);
void smc_ism_unset_conn(struct smc_connection *conn);
int smc_ism_get_vlan(struct smcd_dev *dev, unsigned short vlan_id);
int smc_ism_put_vlan(struct smcd_dev *dev, unsigned short vlan_id);
int smc_ism_register_dmb(struct smc_link_group *lgr, int buf_size,
			 struct smc_buf_desc *dmb_desc);
int smc_ism_unregister_dmb(struct smcd_dev *dev, struct smc_buf_desc *dmb_desc);
int smc_ism_signal_shutdown(struct smc_link_group *lgr);
void smc_ism_get_system_eid(u8 **eid);
u16 smc_ism_get_chid(struct smcd_dev *dev);
bool smc_ism_is_v2_capable(void);
int smc_ism_init(void);
void smc_ism_exit(void);
int smcd_nl_get_device(struct sk_buff *skb, struct netlink_callback *cb);

static inline int smc_ism_write(struct smcd_dev *smcd, u64 dmb_tok,
				unsigned int idx, bool sf, unsigned int offset,
				void *data, size_t len)
{
	int rc;

	rc = smcd->ops->move_data(smcd, dmb_tok, idx, sf, offset, data, len);
	return rc < 0 ? rc : 0;
}

static inline bool __smc_ism_is_virtual(u16 chid)
{
	/* CHIDs in range of 0xFF00 to 0xFFFF are reserved
	 * for virtual ISM device.
	 *
	 * loopback-ism:	0xFFFF
	 * virtio-ism:		0xFF00 ~ 0xFFFE
	 */
	return ((chid & 0xFF00) == 0xFF00);
}

static inline bool smc_ism_is_virtual(struct smcd_dev *smcd)
{
	u16 chid = smcd->ops->get_chid(smcd);

	return __smc_ism_is_virtual(chid);
}

#endif
