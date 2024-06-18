// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit fixture to have a (configurable) wiphy
 *
 * Copyright (C) 2023 Intel Corporation
 */
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include "util.h"

int t_wiphy_init(struct kunit_resource *resource, void *ctx)
{
	struct kunit *test = kunit_get_current_test();
	struct cfg80211_ops *ops;
	struct wiphy *wiphy;
	struct t_wiphy_priv *priv;

	ops = kzalloc(sizeof(*ops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ops);

	wiphy = wiphy_new_nm(ops, sizeof(*priv), "kunit");
	KUNIT_ASSERT_NOT_NULL(test, wiphy);

	priv = wiphy_priv(wiphy);
	priv->ctx = ctx;
	priv->ops = ops;

	/* Initialize channels, feel free to add more here channels/bands */
	memcpy(priv->channels_2ghz, channels_2ghz, sizeof(channels_2ghz));
	wiphy->bands[NL80211_BAND_2GHZ] = &priv->band_2ghz;
	priv->band_2ghz.channels = priv->channels_2ghz;
	priv->band_2ghz.n_channels = ARRAY_SIZE(channels_2ghz);

	resource->data = wiphy;
	resource->name = "wiphy";

	return 0;
}

void t_wiphy_exit(struct kunit_resource *resource)
{
	struct t_wiphy_priv *priv;
	struct cfg80211_ops *ops;

	priv = wiphy_priv(resource->data);
	ops = priv->ops;

	/* Should we ensure anything about the state here?
	 * e.g. full destruction or no calls to any ops on destruction?
	 */

	wiphy_free(resource->data);
	kfree(ops);
}
