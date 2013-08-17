/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CFMUXL_H_
#define CFMUXL_H_
#include <net/caif/caif_layer.h>

struct cfsrvl;
struct cffrml;

struct cflayer *cfmuxl_create(void);
int cfmuxl_set_uplayer(struct cflayer *layr, struct cflayer *up, u8 linkid);
struct cflayer *cfmuxl_remove_dnlayer(struct cflayer *layr, u8 phyid);
int cfmuxl_set_dnlayer(struct cflayer *layr, struct cflayer *up, u8 phyid);
struct cflayer *cfmuxl_remove_uplayer(struct cflayer *layr, u8 linkid);

#endif				/* CFMUXL_H_ */
