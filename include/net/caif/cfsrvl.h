/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 */

#ifndef CFSRVL_H_
#define CFSRVL_H_
#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/rculist.h>

struct cfsrvl {
	struct cflayer layer;
	bool open;
	bool phy_flow_on;
	bool modem_flow_on;
	bool supports_flowctrl;
	void (*release)(struct cflayer *layer);
	struct dev_info dev_info;
	void (*hold)(struct cflayer *lyr);
	void (*put)(struct cflayer *lyr);
	struct rcu_head rcu;
};

struct cflayer *cfvei_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfdgml_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfutill_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfvidl_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfrfml_create(u8 linkid, struct dev_info *dev_info,
				int mtu_size);
struct cflayer *cfdbgl_create(u8 linkid, struct dev_info *dev_info);

void cfsrvl_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
		     int phyid);

bool cfsrvl_phyid_match(struct cflayer *layer, int phyid);

void cfsrvl_init(struct cfsrvl *service,
			u8 channel_id,
			struct dev_info *dev_info,
			bool supports_flowctrl);
bool cfsrvl_ready(struct cfsrvl *service, int *err);
u8 cfsrvl_getphyid(struct cflayer *layer);

static inline void cfsrvl_get(struct cflayer *layr)
{
	struct cfsrvl *s = container_of(layr, struct cfsrvl, layer);
	if (layr == NULL || layr->up == NULL || s->hold == NULL)
		return;

	s->hold(layr->up);
}

static inline void cfsrvl_put(struct cflayer *layr)
{
	struct cfsrvl *s = container_of(layr, struct cfsrvl, layer);
	if (layr == NULL || layr->up == NULL || s->hold == NULL)
		return;

	s->put(layr->up);
}
#endif				/* CFSRVL_H_ */
