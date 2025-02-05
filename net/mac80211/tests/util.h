/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Utilities for mac80211 unit testing
 *
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __MAC80211_UTILS_H
#define __MAC80211_UTILS_H

#include "../ieee80211_i.h"

struct t_sdata {
	struct ieee80211_sub_if_data *sdata;
	struct wiphy *wiphy;
	struct ieee80211_local local;

	void *ctx;

	struct ieee80211_supported_band band_2ghz;
	struct ieee80211_supported_band band_5ghz;
};

#define T_SDATA(test) ({						\
		struct t_sdata *__t_sdata =				\
			kunit_alloc_resource(test, t_sdata_init,	\
					     t_sdata_exit,		\
					     GFP_KERNEL, NULL);		\
									\
		KUNIT_ASSERT_NOT_NULL(test, __t_sdata);			\
		__t_sdata;						\
	})

int t_sdata_init(struct kunit_resource *resource, void *data);
void t_sdata_exit(struct kunit_resource *resource);

#endif /* __MAC80211_UTILS_H */
