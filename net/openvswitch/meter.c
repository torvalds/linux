/*
 * Copyright (c) 2017 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/openvswitch.h>
#include <linux/netlink.h>
#include <linux/rculist.h>

#include <net/netlink.h>
#include <net/genetlink.h>

#include "datapath.h"
#include "meter.h"

#define METER_HASH_BUCKETS 1024

static const struct nla_policy meter_policy[OVS_METER_ATTR_MAX + 1] = {
	[OVS_METER_ATTR_ID] = { .type = NLA_U32, },
	[OVS_METER_ATTR_KBPS] = { .type = NLA_FLAG },
	[OVS_METER_ATTR_STATS] = { .len = sizeof(struct ovs_flow_stats) },
	[OVS_METER_ATTR_BANDS] = { .type = NLA_NESTED },
	[OVS_METER_ATTR_USED] = { .type = NLA_U64 },
	[OVS_METER_ATTR_CLEAR] = { .type = NLA_FLAG },
	[OVS_METER_ATTR_MAX_METERS] = { .type = NLA_U32 },
	[OVS_METER_ATTR_MAX_BANDS] = { .type = NLA_U32 },
};

static const struct nla_policy band_policy[OVS_BAND_ATTR_MAX + 1] = {
	[OVS_BAND_ATTR_TYPE] = { .type = NLA_U32, },
	[OVS_BAND_ATTR_RATE] = { .type = NLA_U32, },
	[OVS_BAND_ATTR_BURST] = { .type = NLA_U32, },
	[OVS_BAND_ATTR_STATS] = { .len = sizeof(struct ovs_flow_stats) },
};

static void rcu_free_ovs_meter_callback(struct rcu_head *rcu)
{
	struct dp_meter *meter = container_of(rcu, struct dp_meter, rcu);

	kfree(meter);
}

static void ovs_meter_free(struct dp_meter *meter)
{
	if (!meter)
		return;

	call_rcu(&meter->rcu, rcu_free_ovs_meter_callback);
}

static struct hlist_head *meter_hash_bucket(const struct datapath *dp,
					    u32 meter_id)
{
	return &dp->meters[meter_id & (METER_HASH_BUCKETS - 1)];
}

/* Call with ovs_mutex or RCU read lock. */
static struct dp_meter *lookup_meter(const struct datapath *dp,
				     u32 meter_id)
{
	struct dp_meter *meter;
	struct hlist_head *head;

	head = meter_hash_bucket(dp, meter_id);
	hlist_for_each_entry_rcu(meter, head, dp_hash_node) {
		if (meter->id == meter_id)
			return meter;
	}
	return NULL;
}

static void attach_meter(struct datapath *dp, struct dp_meter *meter)
{
	struct hlist_head *head = meter_hash_bucket(dp, meter->id);

	hlist_add_head_rcu(&meter->dp_hash_node, head);
}

static void detach_meter(struct dp_meter *meter)
{
	ASSERT_OVSL();
	if (meter)
		hlist_del_rcu(&meter->dp_hash_node);
}

static struct sk_buff *
ovs_meter_cmd_reply_start(struct genl_info *info, u8 cmd,
			  struct ovs_header **ovs_reply_header)
{
	struct sk_buff *skb;
	struct ovs_header *ovs_header = info->userhdr;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	*ovs_reply_header = genlmsg_put(skb, info->snd_portid,
					info->snd_seq,
					&dp_meter_genl_family, 0, cmd);
	if (!ovs_reply_header) {
		nlmsg_free(skb);
		return ERR_PTR(-EMSGSIZE);
	}
	(*ovs_reply_header)->dp_ifindex = ovs_header->dp_ifindex;

	return skb;
}

static int ovs_meter_cmd_reply_stats(struct sk_buff *reply, u32 meter_id,
				     struct dp_meter *meter)
{
	struct nlattr *nla;
	struct dp_meter_band *band;
	u16 i;

	if (nla_put_u32(reply, OVS_METER_ATTR_ID, meter_id))
		goto error;

	if (!meter)
		return 0;

	if (nla_put(reply, OVS_METER_ATTR_STATS,
		    sizeof(struct ovs_flow_stats), &meter->stats) ||
	    nla_put_u64_64bit(reply, OVS_METER_ATTR_USED, meter->used,
			      OVS_METER_ATTR_PAD))
		goto error;

	nla = nla_nest_start(reply, OVS_METER_ATTR_BANDS);
	if (!nla)
		goto error;

	band = meter->bands;

	for (i = 0; i < meter->n_bands; ++i, ++band) {
		struct nlattr *band_nla;

		band_nla = nla_nest_start(reply, OVS_BAND_ATTR_UNSPEC);
		if (!band_nla || nla_put(reply, OVS_BAND_ATTR_STATS,
					 sizeof(struct ovs_flow_stats),
					 &band->stats))
			goto error;
		nla_nest_end(reply, band_nla);
	}
	nla_nest_end(reply, nla);

	return 0;
error:
	return -EMSGSIZE;
}

static int ovs_meter_cmd_features(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *reply;
	struct ovs_header *ovs_reply_header;
	struct nlattr *nla, *band_nla;
	int err;

	reply = ovs_meter_cmd_reply_start(info, OVS_METER_CMD_FEATURES,
					  &ovs_reply_header);
	if (!reply)
		return PTR_ERR(reply);

	if (nla_put_u32(reply, OVS_METER_ATTR_MAX_METERS, U32_MAX) ||
	    nla_put_u32(reply, OVS_METER_ATTR_MAX_BANDS, DP_MAX_BANDS))
		goto nla_put_failure;

	nla = nla_nest_start(reply, OVS_METER_ATTR_BANDS);
	if (!nla)
		goto nla_put_failure;

	band_nla = nla_nest_start(reply, OVS_BAND_ATTR_UNSPEC);
	if (!band_nla)
		goto nla_put_failure;
	/* Currently only DROP band type is supported. */
	if (nla_put_u32(reply, OVS_BAND_ATTR_TYPE, OVS_METER_BAND_TYPE_DROP))
		goto nla_put_failure;
	nla_nest_end(reply, band_nla);
	nla_nest_end(reply, nla);

	genlmsg_end(reply, ovs_reply_header);
	return genlmsg_reply(reply, info);

nla_put_failure:
	nlmsg_free(reply);
	err = -EMSGSIZE;
	return err;
}

static struct dp_meter *dp_meter_create(struct nlattr **a)
{
	struct nlattr *nla;
	int rem;
	u16 n_bands = 0;
	struct dp_meter *meter;
	struct dp_meter_band *band;
	int err;

	/* Validate attributes, count the bands. */
	if (!a[OVS_METER_ATTR_BANDS])
		return ERR_PTR(-EINVAL);

	nla_for_each_nested(nla, a[OVS_METER_ATTR_BANDS], rem)
		if (++n_bands > DP_MAX_BANDS)
			return ERR_PTR(-EINVAL);

	/* Allocate and set up the meter before locking anything. */
	meter = kzalloc(n_bands * sizeof(struct dp_meter_band) +
			sizeof(*meter), GFP_KERNEL);
	if (!meter)
		return ERR_PTR(-ENOMEM);

	meter->used = div_u64(ktime_get_ns(), 1000 * 1000);
	meter->kbps = a[OVS_METER_ATTR_KBPS] ? 1 : 0;
	meter->keep_stats = !a[OVS_METER_ATTR_CLEAR];
	spin_lock_init(&meter->lock);
	if (meter->keep_stats && a[OVS_METER_ATTR_STATS]) {
		meter->stats = *(struct ovs_flow_stats *)
			nla_data(a[OVS_METER_ATTR_STATS]);
	}
	meter->n_bands = n_bands;

	/* Set up meter bands. */
	band = meter->bands;
	nla_for_each_nested(nla, a[OVS_METER_ATTR_BANDS], rem) {
		struct nlattr *attr[OVS_BAND_ATTR_MAX + 1];
		u32 band_max_delta_t;

		err = nla_parse((struct nlattr **)&attr, OVS_BAND_ATTR_MAX,
				nla_data(nla), nla_len(nla), band_policy,
				NULL);
		if (err)
			goto exit_free_meter;

		if (!attr[OVS_BAND_ATTR_TYPE] ||
		    !attr[OVS_BAND_ATTR_RATE] ||
		    !attr[OVS_BAND_ATTR_BURST]) {
			err = -EINVAL;
			goto exit_free_meter;
		}

		band->type = nla_get_u32(attr[OVS_BAND_ATTR_TYPE]);
		band->rate = nla_get_u32(attr[OVS_BAND_ATTR_RATE]);
		band->burst_size = nla_get_u32(attr[OVS_BAND_ATTR_BURST]);
		/* Figure out max delta_t that is enough to fill any bucket.
		 * Keep max_delta_t size to the bucket units:
		 * pkts => 1/1000 packets, kilobits => bits.
		 */
		band_max_delta_t = (band->burst_size + band->rate) * 1000;
		/* Start with a full bucket. */
		band->bucket = band_max_delta_t;
		if (band_max_delta_t > meter->max_delta_t)
			meter->max_delta_t = band_max_delta_t;
		band++;
	}

	return meter;

exit_free_meter:
	kfree(meter);
	return ERR_PTR(err);
}

static int ovs_meter_cmd_set(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct dp_meter *meter, *old_meter;
	struct sk_buff *reply;
	struct ovs_header *ovs_reply_header;
	struct ovs_header *ovs_header = info->userhdr;
	struct datapath *dp;
	int err;
	u32 meter_id;
	bool failed;

	meter = dp_meter_create(a);
	if (IS_ERR_OR_NULL(meter))
		return PTR_ERR(meter);

	reply = ovs_meter_cmd_reply_start(info, OVS_METER_CMD_SET,
					  &ovs_reply_header);
	if (IS_ERR(reply)) {
		err = PTR_ERR(reply);
		goto exit_free_meter;
	}

	ovs_lock();
	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp) {
		err = -ENODEV;
		goto exit_unlock;
	}

	if (!a[OVS_METER_ATTR_ID]) {
		err = -ENODEV;
		goto exit_unlock;
	}

	meter_id = nla_get_u32(a[OVS_METER_ATTR_ID]);

	/* Cannot fail after this. */
	old_meter = lookup_meter(dp, meter_id);
	detach_meter(old_meter);
	attach_meter(dp, meter);
	ovs_unlock();

	/* Build response with the meter_id and stats from
	 * the old meter, if any.
	 */
	failed = nla_put_u32(reply, OVS_METER_ATTR_ID, meter_id);
	WARN_ON(failed);
	if (old_meter) {
		spin_lock_bh(&old_meter->lock);
		if (old_meter->keep_stats) {
			err = ovs_meter_cmd_reply_stats(reply, meter_id,
							old_meter);
			WARN_ON(err);
		}
		spin_unlock_bh(&old_meter->lock);
		ovs_meter_free(old_meter);
	}

	genlmsg_end(reply, ovs_reply_header);
	return genlmsg_reply(reply, info);

exit_unlock:
	ovs_unlock();
	nlmsg_free(reply);
exit_free_meter:
	kfree(meter);
	return err;
}

static int ovs_meter_cmd_get(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	u32 meter_id;
	struct ovs_header *ovs_header = info->userhdr;
	struct ovs_header *ovs_reply_header;
	struct datapath *dp;
	int err;
	struct sk_buff *reply;
	struct dp_meter *meter;

	if (!a[OVS_METER_ATTR_ID])
		return -EINVAL;

	meter_id = nla_get_u32(a[OVS_METER_ATTR_ID]);

	reply = ovs_meter_cmd_reply_start(info, OVS_METER_CMD_GET,
					  &ovs_reply_header);
	if (IS_ERR(reply))
		return PTR_ERR(reply);

	ovs_lock();

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp) {
		err = -ENODEV;
		goto exit_unlock;
	}

	/* Locate meter, copy stats. */
	meter = lookup_meter(dp, meter_id);
	if (!meter) {
		err = -ENOENT;
		goto exit_unlock;
	}

	spin_lock_bh(&meter->lock);
	err = ovs_meter_cmd_reply_stats(reply, meter_id, meter);
	spin_unlock_bh(&meter->lock);
	if (err)
		goto exit_unlock;

	ovs_unlock();

	genlmsg_end(reply, ovs_reply_header);
	return genlmsg_reply(reply, info);

exit_unlock:
	ovs_unlock();
	nlmsg_free(reply);
	return err;
}

static int ovs_meter_cmd_del(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	u32 meter_id;
	struct ovs_header *ovs_header = info->userhdr;
	struct ovs_header *ovs_reply_header;
	struct datapath *dp;
	int err;
	struct sk_buff *reply;
	struct dp_meter *old_meter;

	if (!a[OVS_METER_ATTR_ID])
		return -EINVAL;
	meter_id = nla_get_u32(a[OVS_METER_ATTR_ID]);

	reply = ovs_meter_cmd_reply_start(info, OVS_METER_CMD_DEL,
					  &ovs_reply_header);
	if (IS_ERR(reply))
		return PTR_ERR(reply);

	ovs_lock();

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp) {
		err = -ENODEV;
		goto exit_unlock;
	}

	old_meter = lookup_meter(dp, meter_id);
	if (old_meter) {
		spin_lock_bh(&old_meter->lock);
		err = ovs_meter_cmd_reply_stats(reply, meter_id, old_meter);
		WARN_ON(err);
		spin_unlock_bh(&old_meter->lock);
		detach_meter(old_meter);
	}
	ovs_unlock();
	ovs_meter_free(old_meter);
	genlmsg_end(reply, ovs_reply_header);
	return genlmsg_reply(reply, info);

exit_unlock:
	ovs_unlock();
	nlmsg_free(reply);
	return err;
}

/* Meter action execution.
 *
 * Return true 'meter_id' drop band is triggered. The 'skb' should be
 * dropped by the caller'.
 */
bool ovs_meter_execute(struct datapath *dp, struct sk_buff *skb,
		       struct sw_flow_key *key, u32 meter_id)
{
	struct dp_meter *meter;
	struct dp_meter_band *band;
	long long int now_ms = div_u64(ktime_get_ns(), 1000 * 1000);
	long long int long_delta_ms;
	u32 delta_ms;
	u32 cost;
	int i, band_exceeded_max = -1;
	u32 band_exceeded_rate = 0;

	meter = lookup_meter(dp, meter_id);
	/* Do not drop the packet when there is no meter. */
	if (!meter)
		return false;

	/* Lock the meter while using it. */
	spin_lock(&meter->lock);

	long_delta_ms = (now_ms - meter->used); /* ms */

	/* Make sure delta_ms will not be too large, so that bucket will not
	 * wrap around below.
	 */
	delta_ms = (long_delta_ms > (long long int)meter->max_delta_t)
		   ? meter->max_delta_t : (u32)long_delta_ms;

	/* Update meter statistics.
	 */
	meter->used = now_ms;
	meter->stats.n_packets += 1;
	meter->stats.n_bytes += skb->len;

	/* Bucket rate is either in kilobits per second, or in packets per
	 * second.  We maintain the bucket in the units of either bits or
	 * 1/1000th of a packet, correspondingly.
	 * Then, when rate is multiplied with milliseconds, we get the
	 * bucket units:
	 * msec * kbps = bits, and
	 * msec * packets/sec = 1/1000 packets.
	 *
	 * 'cost' is the number of bucket units in this packet.
	 */
	cost = (meter->kbps) ? skb->len * 8 : 1000;

	/* Update all bands and find the one hit with the highest rate. */
	for (i = 0; i < meter->n_bands; ++i) {
		long long int max_bucket_size;

		band = &meter->bands[i];
		max_bucket_size = (band->burst_size + band->rate) * 1000;

		band->bucket += delta_ms * band->rate;
		if (band->bucket > max_bucket_size)
			band->bucket = max_bucket_size;

		if (band->bucket >= cost) {
			band->bucket -= cost;
		} else if (band->rate > band_exceeded_rate) {
			band_exceeded_rate = band->rate;
			band_exceeded_max = i;
		}
	}

	if (band_exceeded_max >= 0) {
		/* Update band statistics. */
		band = &meter->bands[band_exceeded_max];
		band->stats.n_packets += 1;
		band->stats.n_bytes += skb->len;

		/* Drop band triggered, let the caller drop the 'skb'.  */
		if (band->type == OVS_METER_BAND_TYPE_DROP) {
			spin_unlock(&meter->lock);
			return true;
		}
	}

	spin_unlock(&meter->lock);
	return false;
}

static struct genl_ops dp_meter_genl_ops[] = {
	{ .cmd = OVS_METER_CMD_FEATURES,
		.flags = 0,		  /* OK for unprivileged users. */
		.policy = meter_policy,
		.doit = ovs_meter_cmd_features
	},
	{ .cmd = OVS_METER_CMD_SET,
		.flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN
					   *  privilege.
					   */
		.policy = meter_policy,
		.doit = ovs_meter_cmd_set,
	},
	{ .cmd = OVS_METER_CMD_GET,
		.flags = 0,		  /* OK for unprivileged users. */
		.policy = meter_policy,
		.doit = ovs_meter_cmd_get,
	},
	{ .cmd = OVS_METER_CMD_DEL,
		.flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN
					   *  privilege.
					   */
		.policy = meter_policy,
		.doit = ovs_meter_cmd_del
	},
};

static const struct genl_multicast_group ovs_meter_multicast_group = {
	.name = OVS_METER_MCGROUP,
};

struct genl_family dp_meter_genl_family __ro_after_init = {
	.hdrsize = sizeof(struct ovs_header),
	.name = OVS_METER_FAMILY,
	.version = OVS_METER_VERSION,
	.maxattr = OVS_METER_ATTR_MAX,
	.netnsok = true,
	.parallel_ops = true,
	.ops = dp_meter_genl_ops,
	.n_ops = ARRAY_SIZE(dp_meter_genl_ops),
	.mcgrps = &ovs_meter_multicast_group,
	.n_mcgrps = 1,
	.module = THIS_MODULE,
};

int ovs_meters_init(struct datapath *dp)
{
	int i;

	dp->meters = kmalloc_array(METER_HASH_BUCKETS,
				   sizeof(struct hlist_head), GFP_KERNEL);

	if (!dp->meters)
		return -ENOMEM;

	for (i = 0; i < METER_HASH_BUCKETS; i++)
		INIT_HLIST_HEAD(&dp->meters[i]);

	return 0;
}

void ovs_meters_exit(struct datapath *dp)
{
	int i;

	for (i = 0; i < METER_HASH_BUCKETS; i++) {
		struct hlist_head *head = &dp->meters[i];
		struct dp_meter *meter;
		struct hlist_node *n;

		hlist_for_each_entry_safe(meter, n, head, dp_hash_node)
			kfree(meter);
	}

	kfree(dp->meters);
}
