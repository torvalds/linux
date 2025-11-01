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
#include <linux/dibs.h>

#include "smc.h"

#define SMC_EMULATED_ISM_CHID_MASK	0xFF00
#define SMC_ISM_IDENT_MASK		0x00FFFF

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

struct smc_ism_seid {
	u8 seid_string[24];
	u8 serial_number[4];
	u8 type[4];
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
void smc_ism_unregister_dmb(struct smcd_dev *dev,
			    struct smc_buf_desc *dmb_desc);
bool smc_ism_support_dmb_nocopy(struct smcd_dev *smcd);
int smc_ism_attach_dmb(struct smcd_dev *dev, u64 token,
		       struct smc_buf_desc *dmb_desc);
int smc_ism_detach_dmb(struct smcd_dev *dev, u64 token);
int smc_ism_signal_shutdown(struct smc_link_group *lgr);
void smc_ism_get_system_eid(u8 **eid);
u16 smc_ism_get_chid(struct smcd_dev *dev);
bool smc_ism_is_v2_capable(void);
void smc_ism_set_v2_capable(void);
int smc_ism_init(void);
void smc_ism_exit(void);
int smcd_nl_get_device(struct sk_buff *skb, struct netlink_callback *cb);

static inline int smc_ism_write(struct smcd_dev *smcd, u64 dmb_tok,
				unsigned int idx, bool sf, unsigned int offset,
				void *data, size_t len)
{
	int rc;

	rc = smcd->dibs->ops->move_data(smcd->dibs, dmb_tok, idx, sf, offset,
				       data, len);

	return rc < 0 ? rc : 0;
}

static inline bool __smc_ism_is_emulated(u16 chid)
{
	/* CHIDs in range of 0xFF00 to 0xFFFF are reserved
	 * for Emulated-ISM device.
	 *
	 * loopback-ism:	0xFFFF
	 * virtio-ism:		0xFF00 ~ 0xFFFE
	 */
	return ((chid & 0xFF00) == 0xFF00);
}

static inline bool smc_ism_is_emulated(struct smcd_dev *smcd)
{
	u16 chid = smcd->dibs->ops->get_fabric_id(smcd->dibs);

	return __smc_ism_is_emulated(chid);
}

static inline bool smc_ism_is_loopback(struct dibs_dev *dibs)
{
	return (dibs->ops->get_fabric_id(dibs) == DIBS_LOOPBACK_FABRIC);
}

static inline void copy_to_smcdgid(struct smcd_gid *sgid, uuid_t *dibs_gid)
{
	__be64 temp;

	memcpy(&temp, dibs_gid, sizeof(sgid->gid));
	sgid->gid = ntohll(temp);
	memcpy(&temp, (uint8_t *)dibs_gid + sizeof(sgid->gid),
	       sizeof(sgid->gid_ext));
	sgid->gid_ext = ntohll(temp);
}

static inline void copy_to_dibsgid(uuid_t *dibs_gid, struct smcd_gid *sgid)
{
	__be64 temp;

	temp = htonll(sgid->gid);
	memcpy(dibs_gid, &temp, sizeof(sgid->gid));
	temp = htonll(sgid->gid_ext);
	memcpy((uint8_t *)dibs_gid + sizeof(sgid->gid), &temp,
	       sizeof(sgid->gid_ext));
}

#endif
