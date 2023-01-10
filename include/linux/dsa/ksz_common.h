/* SPDX-License-Identifier: GPL-2.0 */
/* Microchip switch tag common header
 *
 * Copyright (C) 2022 Microchip Technology Inc.
 */

#ifndef _NET_DSA_KSZ_COMMON_H_
#define _NET_DSA_KSZ_COMMON_H_

#include <net/dsa.h>

struct ksz_tagger_data {
	void (*hwtstamp_set_state)(struct dsa_switch *ds, bool on);
};

static inline struct ksz_tagger_data *
ksz_tagger_data(struct dsa_switch *ds)
{
	return ds->tagger_data;
}

#endif /* _NET_DSA_KSZ_COMMON_H_ */
