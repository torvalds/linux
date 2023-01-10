/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2008, Intel Corporation.
 *
 * Author: Lucy Liu <lucy.liu@intel.com>
 */

#ifndef __NET_DCBNL_H__
#define __NET_DCBNL_H__

#include <linux/dcbnl.h>

struct net_device;

struct dcb_app_type {
	int	ifindex;
	struct dcb_app	  app;
	struct list_head  list;
	u8	dcbx;
};

int dcb_setapp(struct net_device *, struct dcb_app *);
u8 dcb_getapp(struct net_device *, struct dcb_app *);
int dcb_ieee_setapp(struct net_device *, struct dcb_app *);
int dcb_ieee_delapp(struct net_device *, struct dcb_app *);
u8 dcb_ieee_getapp_mask(struct net_device *, struct dcb_app *);

struct dcb_ieee_app_prio_map {
	u64 map[IEEE_8021QAZ_MAX_TCS];
};
void dcb_ieee_getapp_prio_dscp_mask_map(const struct net_device *dev,
					struct dcb_ieee_app_prio_map *p_map);

struct dcb_ieee_app_dscp_map {
	u8 map[64];
};
void dcb_ieee_getapp_dscp_prio_mask_map(const struct net_device *dev,
					struct dcb_ieee_app_dscp_map *p_map);
u8 dcb_ieee_getapp_default_prio_mask(const struct net_device *dev);

int dcbnl_ieee_notify(struct net_device *dev, int event, int cmd,
		      u32 seq, u32 pid);
int dcbnl_cee_notify(struct net_device *dev, int event, int cmd,
		     u32 seq, u32 pid);

/*
 * Ops struct for the netlink callbacks.  Used by DCB-enabled drivers through
 * the netdevice struct.
 */
struct dcbnl_rtnl_ops {
	/* IEEE 802.1Qaz std */
	int (*ieee_getets) (struct net_device *, struct ieee_ets *);
	int (*ieee_setets) (struct net_device *, struct ieee_ets *);
	int (*ieee_getmaxrate) (struct net_device *, struct ieee_maxrate *);
	int (*ieee_setmaxrate) (struct net_device *, struct ieee_maxrate *);
	int (*ieee_getqcn) (struct net_device *, struct ieee_qcn *);
	int (*ieee_setqcn) (struct net_device *, struct ieee_qcn *);
	int (*ieee_getqcnstats) (struct net_device *, struct ieee_qcn_stats *);
	int (*ieee_getpfc) (struct net_device *, struct ieee_pfc *);
	int (*ieee_setpfc) (struct net_device *, struct ieee_pfc *);
	int (*ieee_getapp) (struct net_device *, struct dcb_app *);
	int (*ieee_setapp) (struct net_device *, struct dcb_app *);
	int (*ieee_delapp) (struct net_device *, struct dcb_app *);
	int (*ieee_peer_getets) (struct net_device *, struct ieee_ets *);
	int (*ieee_peer_getpfc) (struct net_device *, struct ieee_pfc *);

	/* CEE std */
	u8   (*getstate)(struct net_device *);
	u8   (*setstate)(struct net_device *, u8);
	void (*getpermhwaddr)(struct net_device *, u8 *);
	void (*setpgtccfgtx)(struct net_device *, int, u8, u8, u8, u8);
	void (*setpgbwgcfgtx)(struct net_device *, int, u8);
	void (*setpgtccfgrx)(struct net_device *, int, u8, u8, u8, u8);
	void (*setpgbwgcfgrx)(struct net_device *, int, u8);
	void (*getpgtccfgtx)(struct net_device *, int, u8 *, u8 *, u8 *, u8 *);
	void (*getpgbwgcfgtx)(struct net_device *, int, u8 *);
	void (*getpgtccfgrx)(struct net_device *, int, u8 *, u8 *, u8 *, u8 *);
	void (*getpgbwgcfgrx)(struct net_device *, int, u8 *);
	void (*setpfccfg)(struct net_device *, int, u8);
	void (*getpfccfg)(struct net_device *, int, u8 *);
	u8   (*setall)(struct net_device *);
	u8   (*getcap)(struct net_device *, int, u8 *);
	int  (*getnumtcs)(struct net_device *, int, u8 *);
	int  (*setnumtcs)(struct net_device *, int, u8);
	u8   (*getpfcstate)(struct net_device *);
	void (*setpfcstate)(struct net_device *, u8);
	void (*getbcncfg)(struct net_device *, int, u32 *);
	void (*setbcncfg)(struct net_device *, int, u32);
	void (*getbcnrp)(struct net_device *, int, u8 *);
	void (*setbcnrp)(struct net_device *, int, u8);
	int  (*setapp)(struct net_device *, u8, u16, u8);
	int  (*getapp)(struct net_device *, u8, u16);
	u8   (*getfeatcfg)(struct net_device *, int, u8 *);
	u8   (*setfeatcfg)(struct net_device *, int, u8);

	/* DCBX configuration */
	u8   (*getdcbx)(struct net_device *);
	u8   (*setdcbx)(struct net_device *, u8);

	/* peer apps */
	int (*peer_getappinfo)(struct net_device *, struct dcb_peer_app_info *,
			       u16 *);
	int (*peer_getapptable)(struct net_device *, struct dcb_app *);

	/* CEE peer */
	int (*cee_peer_getpg) (struct net_device *, struct cee_pg *);
	int (*cee_peer_getpfc) (struct net_device *, struct cee_pfc *);

	/* buffer settings */
	int (*dcbnl_getbuffer)(struct net_device *, struct dcbnl_buffer *);
	int (*dcbnl_setbuffer)(struct net_device *, struct dcbnl_buffer *);

	/* apptrust */
	int (*dcbnl_setapptrust)(struct net_device *, u8 *, int);
	int (*dcbnl_getapptrust)(struct net_device *, u8 *, int *);
};

#endif /* __NET_DCBNL_H__ */
