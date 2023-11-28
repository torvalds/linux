// SPDX-License-Identifier: GPL-2.0-only
/*
 * IEEE 802.15.4 scanning management
 *
 * Copyright (C) 2021 Qorvo US, Inc
 * Authors:
 *   - David Girault <david.girault@qorvo.com>
 *   - Miquel Raynal <miquel.raynal@bootlin.com>
 */

#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <net/mac802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"
#include "../ieee802154/nl802154.h"

#define IEEE802154_BEACON_MHR_SZ 13
#define IEEE802154_BEACON_PL_SZ 4
#define IEEE802154_MAC_CMD_MHR_SZ 23
#define IEEE802154_MAC_CMD_PL_SZ 1
#define IEEE802154_BEACON_SKB_SZ (IEEE802154_BEACON_MHR_SZ + \
				  IEEE802154_BEACON_PL_SZ)
#define IEEE802154_MAC_CMD_SKB_SZ (IEEE802154_MAC_CMD_MHR_SZ + \
				   IEEE802154_MAC_CMD_PL_SZ)

/* mac802154_scan_cleanup_locked() must be called upon scan completion or abort.
 * - Completions are asynchronous, not locked by the rtnl and decided by the
 *   scan worker.
 * - Aborts are decided by userspace, and locked by the rtnl.
 *
 * Concurrent modifications to the PHY, the interfaces or the hardware is in
 * general prevented by the rtnl. So in most cases we don't need additional
 * protection.
 *
 * However, the scan worker get's triggered without anybody noticing and thus we
 * must ensure the presence of the devices as well as data consistency:
 * - The sub-interface and device driver module get both their reference
 *   counters incremented whenever we start a scan, so they cannot disappear
 *   during operation.
 * - Data consistency is achieved by the use of rcu protected pointers.
 */
static int mac802154_scan_cleanup_locked(struct ieee802154_local *local,
					 struct ieee802154_sub_if_data *sdata,
					 bool aborted)
{
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct wpan_phy *wpan_phy = local->phy;
	struct cfg802154_scan_request *request;
	u8 arg;

	/* Prevent any further use of the scan request */
	clear_bit(IEEE802154_IS_SCANNING, &local->ongoing);
	cancel_delayed_work(&local->scan_work);
	request = rcu_replace_pointer(local->scan_req, NULL, 1);
	if (!request)
		return 0;
	kvfree_rcu_mightsleep(request);

	/* Advertize first, while we know the devices cannot be removed */
	if (aborted)
		arg = NL802154_SCAN_DONE_REASON_ABORTED;
	else
		arg = NL802154_SCAN_DONE_REASON_FINISHED;
	nl802154_scan_done(wpan_phy, wpan_dev, arg);

	/* Cleanup software stack */
	ieee802154_mlme_op_post(local);

	/* Set the hardware back in its original state */
	drv_set_channel(local, wpan_phy->current_page,
			wpan_phy->current_channel);
	ieee802154_configure_durations(wpan_phy, wpan_phy->current_page,
				       wpan_phy->current_channel);
	drv_stop(local);
	synchronize_net();
	sdata->required_filtering = sdata->iface_default_filtering;
	drv_start(local, sdata->required_filtering, &local->addr_filt);

	return 0;
}

int mac802154_abort_scan_locked(struct ieee802154_local *local,
				struct ieee802154_sub_if_data *sdata)
{
	ASSERT_RTNL();

	if (!mac802154_is_scanning(local))
		return -ESRCH;

	return mac802154_scan_cleanup_locked(local, sdata, true);
}

static unsigned int mac802154_scan_get_channel_time(u8 duration_order,
						    u8 symbol_duration)
{
	u64 base_super_frame_duration = (u64)symbol_duration *
		IEEE802154_SUPERFRAME_PERIOD * IEEE802154_SLOT_PERIOD;

	return usecs_to_jiffies(base_super_frame_duration *
				(BIT(duration_order) + 1));
}

static void mac802154_flush_queued_beacons(struct ieee802154_local *local)
{
	struct cfg802154_mac_pkt *mac_pkt, *tmp;

	list_for_each_entry_safe(mac_pkt, tmp, &local->rx_beacon_list, node) {
		list_del(&mac_pkt->node);
		kfree_skb(mac_pkt->skb);
		kfree(mac_pkt);
	}
}

static void
mac802154_scan_get_next_channel(struct ieee802154_local *local,
				struct cfg802154_scan_request *scan_req,
				u8 *channel)
{
	(*channel)++;
	*channel = find_next_bit((const unsigned long *)&scan_req->channels,
				 IEEE802154_MAX_CHANNEL + 1,
				 *channel);
}

static int mac802154_scan_find_next_chan(struct ieee802154_local *local,
					 struct cfg802154_scan_request *scan_req,
					 u8 page, u8 *channel)
{
	mac802154_scan_get_next_channel(local, scan_req, channel);
	if (*channel > IEEE802154_MAX_CHANNEL)
		return -EINVAL;

	return 0;
}

static int mac802154_scan_prepare_beacon_req(struct ieee802154_local *local)
{
	memset(&local->scan_beacon_req, 0, sizeof(local->scan_beacon_req));
	local->scan_beacon_req.mhr.fc.type = IEEE802154_FC_TYPE_MAC_CMD;
	local->scan_beacon_req.mhr.fc.dest_addr_mode = IEEE802154_SHORT_ADDRESSING;
	local->scan_beacon_req.mhr.fc.version = IEEE802154_2003_STD;
	local->scan_beacon_req.mhr.fc.source_addr_mode = IEEE802154_NO_ADDRESSING;
	local->scan_beacon_req.mhr.dest.mode = IEEE802154_ADDR_SHORT;
	local->scan_beacon_req.mhr.dest.pan_id = cpu_to_le16(IEEE802154_PANID_BROADCAST);
	local->scan_beacon_req.mhr.dest.short_addr = cpu_to_le16(IEEE802154_ADDR_BROADCAST);
	local->scan_beacon_req.mac_pl.cmd_id = IEEE802154_CMD_BEACON_REQ;

	return 0;
}

static int mac802154_transmit_beacon_req(struct ieee802154_local *local,
					 struct ieee802154_sub_if_data *sdata)
{
	struct sk_buff *skb;
	int ret;

	skb = alloc_skb(IEEE802154_MAC_CMD_SKB_SZ, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	skb->dev = sdata->dev;

	ret = ieee802154_mac_cmd_push(skb, &local->scan_beacon_req, NULL, 0);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	return ieee802154_mlme_tx(local, sdata, skb);
}

void mac802154_scan_worker(struct work_struct *work)
{
	struct ieee802154_local *local =
		container_of(work, struct ieee802154_local, scan_work.work);
	struct cfg802154_scan_request *scan_req;
	struct ieee802154_sub_if_data *sdata;
	unsigned int scan_duration = 0;
	struct wpan_phy *wpan_phy;
	u8 scan_req_duration;
	u8 page, channel;
	int ret;

	/* Ensure the device receiver is turned off when changing channels
	 * because there is no atomic way to change the channel and know on
	 * which one a beacon might have been received.
	 */
	drv_stop(local);
	synchronize_net();
	mac802154_flush_queued_beacons(local);

	rcu_read_lock();
	scan_req = rcu_dereference(local->scan_req);
	if (unlikely(!scan_req)) {
		rcu_read_unlock();
		return;
	}

	sdata = IEEE802154_WPAN_DEV_TO_SUB_IF(scan_req->wpan_dev);

	/* Wait an arbitrary amount of time in case we cannot use the device */
	if (local->suspended || !ieee802154_sdata_running(sdata)) {
		rcu_read_unlock();
		queue_delayed_work(local->mac_wq, &local->scan_work,
				   msecs_to_jiffies(1000));
		return;
	}

	wpan_phy = scan_req->wpan_phy;
	scan_req_duration = scan_req->duration;

	/* Look for the next valid chan */
	page = local->scan_page;
	channel = local->scan_channel;
	do {
		ret = mac802154_scan_find_next_chan(local, scan_req, page, &channel);
		if (ret) {
			rcu_read_unlock();
			goto end_scan;
		}
	} while (!ieee802154_chan_is_valid(scan_req->wpan_phy, page, channel));

	rcu_read_unlock();

	/* Bypass the stack on purpose when changing the channel */
	rtnl_lock();
	ret = drv_set_channel(local, page, channel);
	rtnl_unlock();
	if (ret) {
		dev_err(&sdata->dev->dev,
			"Channel change failure during scan, aborting (%d)\n", ret);
		goto end_scan;
	}

	local->scan_page = page;
	local->scan_channel = channel;

	rtnl_lock();
	ret = drv_start(local, IEEE802154_FILTERING_3_SCAN, &local->addr_filt);
	rtnl_unlock();
	if (ret) {
		dev_err(&sdata->dev->dev,
			"Restarting failure after channel change, aborting (%d)\n", ret);
		goto end_scan;
	}

	if (scan_req->type == NL802154_SCAN_ACTIVE) {
		ret = mac802154_transmit_beacon_req(local, sdata);
		if (ret)
			dev_err(&sdata->dev->dev,
				"Error when transmitting beacon request (%d)\n", ret);
	}

	ieee802154_configure_durations(wpan_phy, page, channel);
	scan_duration = mac802154_scan_get_channel_time(scan_req_duration,
							wpan_phy->symbol_duration);
	dev_dbg(&sdata->dev->dev,
		"Scan page %u channel %u for %ums\n",
		page, channel, jiffies_to_msecs(scan_duration));
	queue_delayed_work(local->mac_wq, &local->scan_work, scan_duration);
	return;

end_scan:
	rtnl_lock();
	mac802154_scan_cleanup_locked(local, sdata, false);
	rtnl_unlock();
}

int mac802154_trigger_scan_locked(struct ieee802154_sub_if_data *sdata,
				  struct cfg802154_scan_request *request)
{
	struct ieee802154_local *local = sdata->local;

	ASSERT_RTNL();

	if (mac802154_is_scanning(local))
		return -EBUSY;

	if (request->type != NL802154_SCAN_PASSIVE &&
	    request->type != NL802154_SCAN_ACTIVE)
		return -EOPNOTSUPP;

	/* Store scanning parameters */
	rcu_assign_pointer(local->scan_req, request);

	/* Software scanning requires to set promiscuous mode, so we need to
	 * pause the Tx queue during the entire operation.
	 */
	ieee802154_mlme_op_pre(local);

	sdata->required_filtering = IEEE802154_FILTERING_3_SCAN;
	local->scan_page = request->page;
	local->scan_channel = -1;
	set_bit(IEEE802154_IS_SCANNING, &local->ongoing);
	if (request->type == NL802154_SCAN_ACTIVE)
		mac802154_scan_prepare_beacon_req(local);

	nl802154_scan_started(request->wpan_phy, request->wpan_dev);

	queue_delayed_work(local->mac_wq, &local->scan_work, 0);

	return 0;
}

int mac802154_process_beacon(struct ieee802154_local *local,
			     struct sk_buff *skb,
			     u8 page, u8 channel)
{
	struct ieee802154_beacon_hdr *bh = (void *)skb->data;
	struct ieee802154_addr *src = &mac_cb(skb)->source;
	struct cfg802154_scan_request *scan_req;
	struct ieee802154_coord_desc desc;

	if (skb->len != sizeof(*bh))
		return -EINVAL;

	if (unlikely(src->mode == IEEE802154_ADDR_NONE))
		return -EINVAL;

	dev_dbg(&skb->dev->dev,
		"BEACON received on page %u channel %u\n",
		page, channel);

	memcpy(&desc.addr, src, sizeof(desc.addr));
	desc.page = page;
	desc.channel = channel;
	desc.link_quality = mac_cb(skb)->lqi;
	desc.superframe_spec = get_unaligned_le16(skb->data);
	desc.gts_permit = bh->gts_permit;

	trace_802154_scan_event(&desc);

	rcu_read_lock();
	scan_req = rcu_dereference(local->scan_req);
	if (likely(scan_req))
		nl802154_scan_event(scan_req->wpan_phy, scan_req->wpan_dev, &desc);
	rcu_read_unlock();

	return 0;
}

static int mac802154_transmit_beacon(struct ieee802154_local *local,
				     struct wpan_dev *wpan_dev)
{
	struct cfg802154_beacon_request *beacon_req;
	struct ieee802154_sub_if_data *sdata;
	struct sk_buff *skb;
	int ret;

	/* Update the sequence number */
	local->beacon.mhr.seq = atomic_inc_return(&wpan_dev->bsn) & 0xFF;

	skb = alloc_skb(IEEE802154_BEACON_SKB_SZ, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	rcu_read_lock();
	beacon_req = rcu_dereference(local->beacon_req);
	if (unlikely(!beacon_req)) {
		rcu_read_unlock();
		kfree_skb(skb);
		return -EINVAL;
	}

	sdata = IEEE802154_WPAN_DEV_TO_SUB_IF(beacon_req->wpan_dev);
	skb->dev = sdata->dev;

	rcu_read_unlock();

	ret = ieee802154_beacon_push(skb, &local->beacon);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	/* Using the MLME transmission helper for sending beacons is a bit
	 * overkill because we do not really care about the final outcome.
	 *
	 * Even though, going through the whole net stack with a regular
	 * dev_queue_xmit() is not relevant either because we want beacons to be
	 * sent "now" rather than go through the whole net stack scheduling
	 * (qdisc & co).
	 *
	 * Finally, using ieee802154_subif_start_xmit() would only be an option
	 * if we had a generic transmit helper which would acquire the
	 * HARD_TX_LOCK() to prevent buffer handling conflicts with regular
	 * packets.
	 *
	 * So for now we keep it simple and send beacons with our MLME helper,
	 * even if it stops the ieee802154 queue entirely during these
	 * transmissions, wich anyway does not have a huge impact on the
	 * performances given the current design of the stack.
	 */
	return ieee802154_mlme_tx(local, sdata, skb);
}

void mac802154_beacon_worker(struct work_struct *work)
{
	struct ieee802154_local *local =
		container_of(work, struct ieee802154_local, beacon_work.work);
	struct cfg802154_beacon_request *beacon_req;
	struct ieee802154_sub_if_data *sdata;
	struct wpan_dev *wpan_dev;
	u8 interval;
	int ret;

	rcu_read_lock();
	beacon_req = rcu_dereference(local->beacon_req);
	if (unlikely(!beacon_req)) {
		rcu_read_unlock();
		return;
	}

	sdata = IEEE802154_WPAN_DEV_TO_SUB_IF(beacon_req->wpan_dev);

	/* Wait an arbitrary amount of time in case we cannot use the device */
	if (local->suspended || !ieee802154_sdata_running(sdata)) {
		rcu_read_unlock();
		queue_delayed_work(local->mac_wq, &local->beacon_work,
				   msecs_to_jiffies(1000));
		return;
	}

	wpan_dev = beacon_req->wpan_dev;
	interval = beacon_req->interval;

	rcu_read_unlock();

	dev_dbg(&sdata->dev->dev, "Sending beacon\n");
	ret = mac802154_transmit_beacon(local, wpan_dev);
	if (ret)
		dev_err(&sdata->dev->dev,
			"Beacon could not be transmitted (%d)\n", ret);

	if (interval < IEEE802154_ACTIVE_SCAN_DURATION)
		queue_delayed_work(local->mac_wq, &local->beacon_work,
				   local->beacon_interval);
}

int mac802154_stop_beacons_locked(struct ieee802154_local *local,
				  struct ieee802154_sub_if_data *sdata)
{
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct cfg802154_beacon_request *request;

	ASSERT_RTNL();

	if (!mac802154_is_beaconing(local))
		return -ESRCH;

	clear_bit(IEEE802154_IS_BEACONING, &local->ongoing);
	cancel_delayed_work(&local->beacon_work);
	request = rcu_replace_pointer(local->beacon_req, NULL, 1);
	if (!request)
		return 0;
	kvfree_rcu_mightsleep(request);

	nl802154_beaconing_done(wpan_dev);

	return 0;
}

int mac802154_send_beacons_locked(struct ieee802154_sub_if_data *sdata,
				  struct cfg802154_beacon_request *request)
{
	struct ieee802154_local *local = sdata->local;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;

	ASSERT_RTNL();

	if (mac802154_is_beaconing(local))
		mac802154_stop_beacons_locked(local, sdata);

	/* Store beaconing parameters */
	rcu_assign_pointer(local->beacon_req, request);

	set_bit(IEEE802154_IS_BEACONING, &local->ongoing);

	memset(&local->beacon, 0, sizeof(local->beacon));
	local->beacon.mhr.fc.type = IEEE802154_FC_TYPE_BEACON;
	local->beacon.mhr.fc.security_enabled = 0;
	local->beacon.mhr.fc.frame_pending = 0;
	local->beacon.mhr.fc.ack_request = 0;
	local->beacon.mhr.fc.intra_pan = 0;
	local->beacon.mhr.fc.dest_addr_mode = IEEE802154_NO_ADDRESSING;
	local->beacon.mhr.fc.version = IEEE802154_2003_STD;
	local->beacon.mhr.fc.source_addr_mode = IEEE802154_EXTENDED_ADDRESSING;
	atomic_set(&request->wpan_dev->bsn, -1);
	local->beacon.mhr.source.mode = IEEE802154_ADDR_LONG;
	local->beacon.mhr.source.pan_id = request->wpan_dev->pan_id;
	local->beacon.mhr.source.extended_addr = request->wpan_dev->extended_addr;
	local->beacon.mac_pl.beacon_order = request->interval;
	if (request->interval <= IEEE802154_MAX_SCAN_DURATION)
		local->beacon.mac_pl.superframe_order = request->interval;
	local->beacon.mac_pl.final_cap_slot = 0xf;
	local->beacon.mac_pl.battery_life_ext = 0;
	local->beacon.mac_pl.pan_coordinator = !wpan_dev->parent;
	local->beacon.mac_pl.assoc_permit = 1;

	if (request->interval == IEEE802154_ACTIVE_SCAN_DURATION)
		return 0;

	/* Start the beacon work */
	local->beacon_interval =
		mac802154_scan_get_channel_time(request->interval,
						request->wpan_phy->symbol_duration);
	queue_delayed_work(local->mac_wq, &local->beacon_work, 0);

	return 0;
}

int mac802154_perform_association(struct ieee802154_sub_if_data *sdata,
				  struct ieee802154_pan_device *coord,
				  __le16 *short_addr)
{
	u64 ceaddr = swab64((__force u64)coord->extended_addr);
	struct ieee802154_association_req_frame frame = {};
	struct ieee802154_local *local = sdata->local;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct sk_buff *skb;
	int ret;

	frame.mhr.fc.type = IEEE802154_FC_TYPE_MAC_CMD;
	frame.mhr.fc.security_enabled = 0;
	frame.mhr.fc.frame_pending = 0;
	frame.mhr.fc.ack_request = 1; /* We always expect an ack here */
	frame.mhr.fc.intra_pan = 0;
	frame.mhr.fc.dest_addr_mode = (coord->mode == IEEE802154_ADDR_LONG) ?
		IEEE802154_EXTENDED_ADDRESSING : IEEE802154_SHORT_ADDRESSING;
	frame.mhr.fc.version = IEEE802154_2003_STD;
	frame.mhr.fc.source_addr_mode = IEEE802154_EXTENDED_ADDRESSING;
	frame.mhr.source.mode = IEEE802154_ADDR_LONG;
	frame.mhr.source.pan_id = cpu_to_le16(IEEE802154_PANID_BROADCAST);
	frame.mhr.source.extended_addr = wpan_dev->extended_addr;
	frame.mhr.dest.mode = coord->mode;
	frame.mhr.dest.pan_id = coord->pan_id;
	if (coord->mode == IEEE802154_ADDR_LONG)
		frame.mhr.dest.extended_addr = coord->extended_addr;
	else
		frame.mhr.dest.short_addr = coord->short_addr;
	frame.mhr.seq = atomic_inc_return(&wpan_dev->dsn) & 0xFF;
	frame.mac_pl.cmd_id = IEEE802154_CMD_ASSOCIATION_REQ;
	frame.assoc_req_pl.device_type = 1;
	frame.assoc_req_pl.power_source = 1;
	frame.assoc_req_pl.rx_on_when_idle = 1;
	frame.assoc_req_pl.alloc_addr = 1;

	skb = alloc_skb(IEEE802154_MAC_CMD_SKB_SZ + sizeof(frame.assoc_req_pl),
			GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	skb->dev = sdata->dev;

	ret = ieee802154_mac_cmd_push(skb, &frame, &frame.assoc_req_pl,
				      sizeof(frame.assoc_req_pl));
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	local->assoc_dev = coord;
	reinit_completion(&local->assoc_done);
	set_bit(IEEE802154_IS_ASSOCIATING, &local->ongoing);

	ret = ieee802154_mlme_tx_one_locked(local, sdata, skb);
	if (ret) {
		if (ret > 0)
			ret = (ret == IEEE802154_NO_ACK) ? -EREMOTEIO : -EIO;
		dev_warn(&sdata->dev->dev,
			 "No ASSOC REQ ACK received from %8phC\n", &ceaddr);
		goto clear_assoc;
	}

	ret = wait_for_completion_killable_timeout(&local->assoc_done, 10 * HZ);
	if (ret <= 0) {
		dev_warn(&sdata->dev->dev,
			 "No ASSOC RESP received from %8phC\n", &ceaddr);
		ret = -ETIMEDOUT;
		goto clear_assoc;
	}

	if (local->assoc_status != IEEE802154_ASSOCIATION_SUCCESSFUL) {
		if (local->assoc_status == IEEE802154_PAN_AT_CAPACITY)
			ret = -ERANGE;
		else
			ret = -EPERM;

		dev_warn(&sdata->dev->dev,
			 "Negative ASSOC RESP received from %8phC: %s\n", &ceaddr,
			 local->assoc_status == IEEE802154_PAN_AT_CAPACITY ?
			 "PAN at capacity" : "access denied");
	}

	ret = 0;
	*short_addr = local->assoc_addr;

clear_assoc:
	clear_bit(IEEE802154_IS_ASSOCIATING, &local->ongoing);
	local->assoc_dev = NULL;

	return ret;
}

int mac802154_process_association_resp(struct ieee802154_sub_if_data *sdata,
				       struct sk_buff *skb)
{
	struct ieee802154_addr *src = &mac_cb(skb)->source;
	struct ieee802154_addr *dest = &mac_cb(skb)->dest;
	u64 deaddr = swab64((__force u64)dest->extended_addr);
	struct ieee802154_local *local = sdata->local;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct ieee802154_assoc_resp_pl resp_pl = {};

	if (skb->len != sizeof(resp_pl))
		return -EINVAL;

	if (unlikely(src->mode != IEEE802154_EXTENDED_ADDRESSING ||
		     dest->mode != IEEE802154_EXTENDED_ADDRESSING))
		return -EINVAL;

	if (unlikely(dest->extended_addr != wpan_dev->extended_addr ||
		     src->extended_addr != local->assoc_dev->extended_addr))
		return -ENODEV;

	memcpy(&resp_pl, skb->data, sizeof(resp_pl));
	local->assoc_addr = resp_pl.short_addr;
	local->assoc_status = resp_pl.status;

	dev_dbg(&skb->dev->dev,
		"ASSOC RESP 0x%x received from %8phC, getting short address %04x\n",
		local->assoc_status, &deaddr, local->assoc_addr);

	complete(&local->assoc_done);

	return 0;
}

int mac802154_send_disassociation_notif(struct ieee802154_sub_if_data *sdata,
					struct ieee802154_pan_device *target,
					u8 reason)
{
	struct ieee802154_disassociation_notif_frame frame = {};
	u64 teaddr = swab64((__force u64)target->extended_addr);
	struct ieee802154_local *local = sdata->local;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct sk_buff *skb;
	int ret;

	frame.mhr.fc.type = IEEE802154_FC_TYPE_MAC_CMD;
	frame.mhr.fc.security_enabled = 0;
	frame.mhr.fc.frame_pending = 0;
	frame.mhr.fc.ack_request = 1;
	frame.mhr.fc.intra_pan = 1;
	frame.mhr.fc.dest_addr_mode = (target->mode == IEEE802154_ADDR_LONG) ?
		IEEE802154_EXTENDED_ADDRESSING : IEEE802154_SHORT_ADDRESSING;
	frame.mhr.fc.version = IEEE802154_2003_STD;
	frame.mhr.fc.source_addr_mode = IEEE802154_EXTENDED_ADDRESSING;
	frame.mhr.source.mode = IEEE802154_ADDR_LONG;
	frame.mhr.source.pan_id = wpan_dev->pan_id;
	frame.mhr.source.extended_addr = wpan_dev->extended_addr;
	frame.mhr.dest.mode = target->mode;
	frame.mhr.dest.pan_id = wpan_dev->pan_id;
	if (target->mode == IEEE802154_ADDR_LONG)
		frame.mhr.dest.extended_addr = target->extended_addr;
	else
		frame.mhr.dest.short_addr = target->short_addr;
	frame.mhr.seq = atomic_inc_return(&wpan_dev->dsn) & 0xFF;
	frame.mac_pl.cmd_id = IEEE802154_CMD_DISASSOCIATION_NOTIFY;
	frame.disassoc_pl = reason;

	skb = alloc_skb(IEEE802154_MAC_CMD_SKB_SZ + sizeof(frame.disassoc_pl),
			GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	skb->dev = sdata->dev;

	ret = ieee802154_mac_cmd_push(skb, &frame, &frame.disassoc_pl,
				      sizeof(frame.disassoc_pl));
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	ret = ieee802154_mlme_tx_one_locked(local, sdata, skb);
	if (ret) {
		dev_warn(&sdata->dev->dev,
			 "No DISASSOC ACK received from %8phC\n", &teaddr);
		if (ret > 0)
			ret = (ret == IEEE802154_NO_ACK) ? -EREMOTEIO : -EIO;
		return ret;
	}

	dev_dbg(&sdata->dev->dev, "DISASSOC ACK received from %8phC\n", &teaddr);
	return 0;
}

static int
mac802154_send_association_resp_locked(struct ieee802154_sub_if_data *sdata,
				       struct ieee802154_pan_device *target,
				       struct ieee802154_assoc_resp_pl *assoc_resp_pl)
{
	u64 teaddr = swab64((__force u64)target->extended_addr);
	struct ieee802154_association_resp_frame frame = {};
	struct ieee802154_local *local = sdata->local;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct sk_buff *skb;
	int ret;

	frame.mhr.fc.type = IEEE802154_FC_TYPE_MAC_CMD;
	frame.mhr.fc.security_enabled = 0;
	frame.mhr.fc.frame_pending = 0;
	frame.mhr.fc.ack_request = 1; /* We always expect an ack here */
	frame.mhr.fc.intra_pan = 1;
	frame.mhr.fc.dest_addr_mode = IEEE802154_EXTENDED_ADDRESSING;
	frame.mhr.fc.version = IEEE802154_2003_STD;
	frame.mhr.fc.source_addr_mode = IEEE802154_EXTENDED_ADDRESSING;
	frame.mhr.source.mode = IEEE802154_ADDR_LONG;
	frame.mhr.source.extended_addr = wpan_dev->extended_addr;
	frame.mhr.dest.mode = IEEE802154_ADDR_LONG;
	frame.mhr.dest.pan_id = wpan_dev->pan_id;
	frame.mhr.dest.extended_addr = target->extended_addr;
	frame.mhr.seq = atomic_inc_return(&wpan_dev->dsn) & 0xFF;
	frame.mac_pl.cmd_id = IEEE802154_CMD_ASSOCIATION_RESP;

	skb = alloc_skb(IEEE802154_MAC_CMD_SKB_SZ + sizeof(*assoc_resp_pl),
			GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	skb->dev = sdata->dev;

	ret = ieee802154_mac_cmd_push(skb, &frame, assoc_resp_pl,
				      sizeof(*assoc_resp_pl));
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	ret = ieee802154_mlme_tx_locked(local, sdata, skb);
	if (ret) {
		dev_warn(&sdata->dev->dev,
			 "No ASSOC RESP ACK received from %8phC\n", &teaddr);
		if (ret > 0)
			ret = (ret == IEEE802154_NO_ACK) ? -EREMOTEIO : -EIO;
		return ret;
	}

	return 0;
}

int mac802154_process_association_req(struct ieee802154_sub_if_data *sdata,
				      struct sk_buff *skb)
{
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct ieee802154_addr *src = &mac_cb(skb)->source;
	struct ieee802154_addr *dest = &mac_cb(skb)->dest;
	struct ieee802154_assoc_resp_pl assoc_resp_pl = {};
	struct ieee802154_assoc_req_pl assoc_req_pl;
	struct ieee802154_pan_device *child, *exchild;
	struct ieee802154_addr tmp = {};
	u64 ceaddr;
	int ret;

	if (skb->len != sizeof(assoc_req_pl))
		return -EINVAL;

	if (unlikely(src->mode != IEEE802154_EXTENDED_ADDRESSING))
		return -EINVAL;

	if (unlikely(dest->pan_id != wpan_dev->pan_id))
		return -ENODEV;

	if (dest->mode == IEEE802154_EXTENDED_ADDRESSING &&
	    unlikely(dest->extended_addr != wpan_dev->extended_addr))
		return -ENODEV;
	else if (dest->mode == IEEE802154_SHORT_ADDRESSING &&
		 unlikely(dest->short_addr != wpan_dev->short_addr))
		return -ENODEV;

	if (wpan_dev->parent) {
		dev_dbg(&sdata->dev->dev,
			"Ignoring ASSOC REQ, not the PAN coordinator\n");
		return -ENODEV;
	}

	mutex_lock(&wpan_dev->association_lock);

	memcpy(&assoc_req_pl, skb->data, sizeof(assoc_req_pl));
	if (assoc_req_pl.assoc_type) {
		dev_err(&skb->dev->dev, "Fast associations not supported yet\n");
		ret = -EOPNOTSUPP;
		goto unlock;
	}

	child = kzalloc(sizeof(*child), GFP_KERNEL);
	if (!child) {
		ret = -ENOMEM;
		goto unlock;
	}

	child->extended_addr = src->extended_addr;
	child->mode = IEEE802154_EXTENDED_ADDRESSING;
	ceaddr = swab64((__force u64)child->extended_addr);

	if (wpan_dev->nchildren >= wpan_dev->max_associations) {
		if (!wpan_dev->max_associations)
			assoc_resp_pl.status = IEEE802154_PAN_ACCESS_DENIED;
		else
			assoc_resp_pl.status = IEEE802154_PAN_AT_CAPACITY;
		assoc_resp_pl.short_addr = cpu_to_le16(IEEE802154_ADDR_SHORT_BROADCAST);
		dev_dbg(&sdata->dev->dev,
			"Refusing ASSOC REQ from child %8phC, %s\n", &ceaddr,
			assoc_resp_pl.status == IEEE802154_PAN_ACCESS_DENIED ?
			"access denied" : "too many children");
	} else {
		assoc_resp_pl.status = IEEE802154_ASSOCIATION_SUCCESSFUL;
		if (assoc_req_pl.alloc_addr) {
			assoc_resp_pl.short_addr = cfg802154_get_free_short_addr(wpan_dev);
			child->mode = IEEE802154_SHORT_ADDRESSING;
		} else {
			assoc_resp_pl.short_addr = cpu_to_le16(IEEE802154_ADDR_SHORT_UNSPEC);
		}
		child->short_addr = assoc_resp_pl.short_addr;
		dev_dbg(&sdata->dev->dev,
			"Accepting ASSOC REQ from child %8phC, providing short address 0x%04x\n",
			&ceaddr, le16_to_cpu(child->short_addr));
	}

	ret = mac802154_send_association_resp_locked(sdata, child, &assoc_resp_pl);
	if (ret || assoc_resp_pl.status != IEEE802154_ASSOCIATION_SUCCESSFUL) {
		kfree(child);
		goto unlock;
	}

	dev_dbg(&sdata->dev->dev,
		"Successful association with new child %8phC\n", &ceaddr);

	/* Ensure this child is not already associated (might happen due to
	 * retransmissions), in this case drop the ex structure.
	 */
	tmp.mode = child->mode;
	tmp.extended_addr = child->extended_addr;
	exchild = cfg802154_device_is_child(wpan_dev, &tmp);
	if (exchild) {
		dev_dbg(&sdata->dev->dev,
			"Child %8phC was already known\n", &ceaddr);
		list_del(&exchild->node);
	}

	list_add(&child->node, &wpan_dev->children);
	wpan_dev->nchildren++;

unlock:
	mutex_unlock(&wpan_dev->association_lock);
	return ret;
}

int mac802154_process_disassociation_notif(struct ieee802154_sub_if_data *sdata,
					   struct sk_buff *skb)
{
	struct ieee802154_addr *src = &mac_cb(skb)->source;
	struct ieee802154_addr *dest = &mac_cb(skb)->dest;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct ieee802154_pan_device *child;
	struct ieee802154_addr target;
	bool parent;
	u64 teaddr;

	if (skb->len != sizeof(u8))
		return -EINVAL;

	if (unlikely(src->mode != IEEE802154_EXTENDED_ADDRESSING))
		return -EINVAL;

	if (dest->mode == IEEE802154_EXTENDED_ADDRESSING &&
	    unlikely(dest->extended_addr != wpan_dev->extended_addr))
		return -ENODEV;
	else if (dest->mode == IEEE802154_SHORT_ADDRESSING &&
		 unlikely(dest->short_addr != wpan_dev->short_addr))
		return -ENODEV;

	if (dest->pan_id != wpan_dev->pan_id)
		return -ENODEV;

	target.mode = IEEE802154_EXTENDED_ADDRESSING;
	target.extended_addr = src->extended_addr;
	teaddr = swab64((__force u64)target.extended_addr);
	dev_dbg(&skb->dev->dev, "Processing DISASSOC NOTIF from %8phC\n", &teaddr);

	mutex_lock(&wpan_dev->association_lock);
	parent = cfg802154_device_is_parent(wpan_dev, &target);
	if (!parent)
		child = cfg802154_device_is_child(wpan_dev, &target);
	if (!parent && !child) {
		mutex_unlock(&wpan_dev->association_lock);
		return -EINVAL;
	}

	if (parent) {
		kfree(wpan_dev->parent);
		wpan_dev->parent = NULL;
	} else {
		list_del(&child->node);
		kfree(child);
		wpan_dev->nchildren--;
	}

	mutex_unlock(&wpan_dev->association_lock);

	return 0;
}
