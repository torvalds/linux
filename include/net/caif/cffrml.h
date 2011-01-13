/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CFFRML_H_
#define CFFRML_H_
#include <net/caif/caif_layer.h>

struct cffrml;
struct cflayer *cffrml_create(u16 phyid, bool DoFCS);
void cffrml_set_uplayer(struct cflayer *this, struct cflayer *up);
void cffrml_set_dnlayer(struct cflayer *this, struct cflayer *dn);

#endif /* CFFRML_H_ */
