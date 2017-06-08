#ifndef __MAC802154_DRIVER_OPS
#define __MAC802154_DRIVER_OPS

#include <linux/types.h>
#include <linux/rtnetlink.h>

#include <net/mac802154.h>

#include "ieee802154_i.h"
#include "trace.h"

static inline int
drv_xmit_async(struct ieee802154_local *local, struct sk_buff *skb)
{
	return local->ops->xmit_async(&local->hw, skb);
}

static inline int
drv_xmit_sync(struct ieee802154_local *local, struct sk_buff *skb)
{
	might_sleep();

	return local->ops->xmit_sync(&local->hw, skb);
}

static inline int drv_start(struct ieee802154_local *local)
{
	int ret;

	might_sleep();

	trace_802154_drv_start(local);
	local->started = true;
	smp_mb();
	ret = local->ops->start(&local->hw);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline void drv_stop(struct ieee802154_local *local)
{
	might_sleep();

	trace_802154_drv_stop(local);
	local->ops->stop(&local->hw);
	trace_802154_drv_return_void(local);

	/* sync away all work on the tasklet before clearing started */
	tasklet_disable(&local->tasklet);
	tasklet_enable(&local->tasklet);

	barrier();

	local->started = false;
}

static inline int
drv_set_channel(struct ieee802154_local *local, u8 page, u8 channel)
{
	int ret;

	might_sleep();

	trace_802154_drv_set_channel(local, page, channel);
	ret = local->ops->set_channel(&local->hw, page, channel);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int drv_set_tx_power(struct ieee802154_local *local, s32 mbm)
{
	int ret;

	might_sleep();

	if (!local->ops->set_txpower) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	trace_802154_drv_set_tx_power(local, mbm);
	ret = local->ops->set_txpower(&local->hw, mbm);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int drv_set_cca_mode(struct ieee802154_local *local,
				   const struct wpan_phy_cca *cca)
{
	int ret;

	might_sleep();

	if (!local->ops->set_cca_mode) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	trace_802154_drv_set_cca_mode(local, cca);
	ret = local->ops->set_cca_mode(&local->hw, cca);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int drv_set_lbt_mode(struct ieee802154_local *local, bool mode)
{
	int ret;

	might_sleep();

	if (!local->ops->set_lbt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	trace_802154_drv_set_lbt_mode(local, mode);
	ret = local->ops->set_lbt(&local->hw, mode);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_set_cca_ed_level(struct ieee802154_local *local, s32 mbm)
{
	int ret;

	might_sleep();

	if (!local->ops->set_cca_ed_level) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	trace_802154_drv_set_cca_ed_level(local, mbm);
	ret = local->ops->set_cca_ed_level(&local->hw, mbm);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int drv_set_pan_id(struct ieee802154_local *local, __le16 pan_id)
{
	struct ieee802154_hw_addr_filt filt;
	int ret;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.pan_id = pan_id;

	trace_802154_drv_set_pan_id(local, pan_id);
	ret = local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_PANID_CHANGED);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_set_extended_addr(struct ieee802154_local *local, __le64 extended_addr)
{
	struct ieee802154_hw_addr_filt filt;
	int ret;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.ieee_addr = extended_addr;

	trace_802154_drv_set_extended_addr(local, extended_addr);
	ret = local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_IEEEADDR_CHANGED);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_set_short_addr(struct ieee802154_local *local, __le16 short_addr)
{
	struct ieee802154_hw_addr_filt filt;
	int ret;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.short_addr = short_addr;

	trace_802154_drv_set_short_addr(local, short_addr);
	ret = local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_SADDR_CHANGED);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_set_pan_coord(struct ieee802154_local *local, bool is_coord)
{
	struct ieee802154_hw_addr_filt filt;
	int ret;

	might_sleep();

	if (!local->ops->set_hw_addr_filt) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	filt.pan_coord = is_coord;

	trace_802154_drv_set_pan_coord(local, is_coord);
	ret = local->ops->set_hw_addr_filt(&local->hw, &filt,
					    IEEE802154_AFILT_PANC_CHANGED);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_set_csma_params(struct ieee802154_local *local, u8 min_be, u8 max_be,
		    u8 max_csma_backoffs)
{
	int ret;

	might_sleep();

	if (!local->ops->set_csma_params) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	trace_802154_drv_set_csma_params(local, min_be, max_be,
					 max_csma_backoffs);
	ret = local->ops->set_csma_params(&local->hw, min_be, max_be,
					   max_csma_backoffs);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_set_max_frame_retries(struct ieee802154_local *local, s8 max_frame_retries)
{
	int ret;

	might_sleep();

	if (!local->ops->set_frame_retries) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	trace_802154_drv_set_max_frame_retries(local, max_frame_retries);
	ret = local->ops->set_frame_retries(&local->hw, max_frame_retries);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_set_promiscuous_mode(struct ieee802154_local *local, bool on)
{
	int ret;

	might_sleep();

	if (!local->ops->set_promiscuous_mode) {
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	trace_802154_drv_set_promiscuous_mode(local, on);
	ret = local->ops->set_promiscuous_mode(&local->hw, on);
	trace_802154_drv_return_int(local, ret);
	return ret;
}

#endif /* __MAC802154_DRIVER_OPS */
