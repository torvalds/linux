#ifndef __MAC802154_DRIVER_OPS
#define __MAC802154_DRIVER_OPS

#include <linux/types.h>
#include <linux/rtnetlink.h>

#include <net/mac802154.h>

#include "ieee802154_i.h"

static inline int
drv_xmit_async(struct ieee802154_local *local, struct sk_buff *skb)
{
	return local->ops->xmit_async(&local->hw, skb);
}

static inline int
drv_xmit_sync(struct ieee802154_local *local, struct sk_buff *skb)
{
	/* don't allow other operations while sync xmit */
	ASSERT_RTNL();

	might_sleep();

	return local->ops->xmit_sync(&local->hw, skb);
}

static inline int drv_start(struct ieee802154_local *local)
{
	might_sleep();

	local->started = true;
	smp_mb();

	return local->ops->start(&local->hw);
}

static inline void drv_stop(struct ieee802154_local *local)
{
	might_sleep();

	local->ops->stop(&local->hw);

	/* sync away all work on the tasklet before clearing started */
	tasklet_disable(&local->tasklet);
	tasklet_enable(&local->tasklet);

	barrier();

	local->started = false;
}

static inline int
drv_set_channel(struct ieee802154_local *local, u8 page, u8 channel)
{
	might_sleep();

	return local->ops->set_channel(&local->hw, page, channel);
}

static inline int drv_set_tx_power(struct ieee802154_local *local, s8 dbm)
{
	might_sleep();

	if (!local->ops->set_txpower) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return local->ops->set_txpower(&local->hw, dbm);
}

static inline int drv_set_cca_mode(struct ieee802154_local *local,
				   const struct wpan_phy_cca *cca)
{
	might_sleep();

	if (!local->ops->set_cca_mode) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return local->ops->set_cca_mode(&local->hw, cca);
}

static inline int drv_set_lbt_mode(struct ieee802154_local *local, bool mode)
{
	might_sleep();

	if (!local->ops->set_lbt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return local->ops->set_lbt(&local->hw, mode);
}

static inline int
drv_set_cca_ed_level(struct ieee802154_local *local, s32 ed_level)
{
	might_sleep();

	if (!local->ops->set_cca_ed_level) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return local->ops->set_cca_ed_level(&local->hw, ed_level);
}

static inline int drv_set_pan_id(struct ieee802154_local *local, __le16 pan_id)
{
	struct ieee802154_hw_addr_filt filt;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.pan_id = pan_id;

	return local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_PANID_CHANGED);
}

static inline int
drv_set_extended_addr(struct ieee802154_local *local, __le64 extended_addr)
{
	struct ieee802154_hw_addr_filt filt;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.ieee_addr = extended_addr;

	return local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_IEEEADDR_CHANGED);
}

static inline int
drv_set_short_addr(struct ieee802154_local *local, __le16 short_addr)
{
	struct ieee802154_hw_addr_filt filt;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.short_addr = short_addr;

	return local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_SADDR_CHANGED);
}

static inline int
drv_set_pan_coord(struct ieee802154_local *local, bool is_coord)
{
	struct ieee802154_hw_addr_filt filt;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.pan_coord = is_coord;

	return local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_PANC_CHANGED);
}

static inline int
drv_set_csma_params(struct ieee802154_local *local, u8 min_be, u8 max_be,
		    u8 max_csma_backoffs)
{
	might_sleep();

	if (!local->ops->set_csma_params) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return local->ops->set_csma_params(&local->hw, min_be, max_be,
					   max_csma_backoffs);
}

static inline int
drv_set_max_frame_retries(struct ieee802154_local *local, s8 max_frame_retries)
{
	might_sleep();

	if (!local->ops->set_frame_retries) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return local->ops->set_frame_retries(&local->hw, max_frame_retries);
}

static inline int
drv_set_promiscuous_mode(struct ieee802154_local *local, bool on)
{
	might_sleep();

	if (!local->ops->set_promiscuous_mode) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return local->ops->set_promiscuous_mode(&local->hw, on);
}

#endif /* __MAC802154_DRIVER_OPS */
