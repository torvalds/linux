/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CFSRVL_H_
#define CFSRVL_H_
#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/types.h>
struct cfsrvl {
	struct cflayer layer;
	bool open;
	bool phy_flow_on;
	bool modem_flow_on;
	struct dev_info dev_info;
};

struct cflayer *cfvei_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfdgml_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfutill_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfvidl_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfrfml_create(u8 linkid, struct dev_info *dev_info);
struct cflayer *cfdbgl_create(u8 linkid, struct dev_info *dev_info);
bool cfsrvl_phyid_match(struct cflayer *layer, int phyid);
void cfservl_destroy(struct cflayer *layer);
void cfsrvl_init(struct cfsrvl *service,
		 u8 channel_id,
		 struct dev_info *dev_info);
bool cfsrvl_ready(struct cfsrvl *service, int *err);
u8 cfsrvl_getphyid(struct cflayer *layer);

#endif				/* CFSRVL_H_ */
