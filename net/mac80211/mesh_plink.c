/*
 * Copyright (c) 2008 open80211s Ltd.
 * Author:     Luis Carlos Cobo <luisca@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/random.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "mesh.h"

#ifdef CONFIG_MAC80211_VERBOSE_MPL_DEBUG
#define mpl_dbg(fmt, args...)	printk(KERN_DEBUG fmt, ##args)
#else
#define mpl_dbg(fmt, args...)	do { (void)(0); } while (0)
#endif

#define PLINK_GET_FRAME_SUBTYPE(p) (p)
#define PLINK_GET_LLID(p) (p + 1)
#define PLINK_GET_PLID(p) (p + 3)

#define mod_plink_timer(s, t) (mod_timer(&s->plink_timer, \
				jiffies + HZ * t / 1000))

/* Peer link cancel reasons, all subject to ANA approval */
#define MESH_LINK_CANCELLED			2
#define MESH_MAX_NEIGHBORS			3
#define MESH_CAPABILITY_POLICY_VIOLATION	4
#define MESH_CLOSE_RCVD				5
#define MESH_MAX_RETRIES			6
#define MESH_CONFIRM_TIMEOUT			7
#define MESH_SECURITY_ROLE_NEGOTIATION_DIFFERS	8
#define MESH_SECURITY_AUTHENTICATION_IMPOSSIBLE	9
#define MESH_SECURITY_FAILED_VERIFICATION	10

#define dot11MeshMaxRetries(s) (s->u.mesh.mshcfg.dot11MeshMaxRetries)
#define dot11MeshRetryTimeout(s) (s->u.mesh.mshcfg.dot11MeshRetryTimeout)
#define dot11MeshConfirmTimeout(s) (s->u.mesh.mshcfg.dot11MeshConfirmTimeout)
#define dot11MeshHoldingTimeout(s) (s->u.mesh.mshcfg.dot11MeshHoldingTimeout)
#define dot11MeshMaxPeerLinks(s) (s->u.mesh.mshcfg.dot11MeshMaxPeerLinks)

enum plink_frame_type {
	PLINK_OPEN = 0,
	PLINK_CONFIRM,
	PLINK_CLOSE
};

enum plink_event {
	PLINK_UNDEFINED,
	OPN_ACPT,
	OPN_RJCT,
	OPN_IGNR,
	CNF_ACPT,
	CNF_RJCT,
	CNF_IGNR,
	CLS_ACPT,
	CLS_IGNR
};

static inline
void mesh_plink_inc_estab_count(struct ieee80211_sub_if_data *sdata)
{
	atomic_inc(&sdata->u.mesh.mshstats.estab_plinks);
	mesh_accept_plinks_update(sdata);
}

static inline
void mesh_plink_dec_estab_count(struct ieee80211_sub_if_data *sdata)
{
	atomic_dec(&sdata->u.mesh.mshstats.estab_plinks);
	mesh_accept_plinks_update(sdata);
}

/**
 * mesh_plink_fsm_restart - restart a mesh peer link finite state machine
 *
 * @sta: mes peer link to restart
 *
 * Locking: this function must be called holding sta->lock
 */
static inline void mesh_plink_fsm_restart(struct sta_info *sta)
{
	sta->plink_state = PLINK_LISTEN;
	sta->llid = sta->plid = sta->reason = 0;
	sta->plink_retries = 0;
}

/*
 * NOTE: This is just an alias for sta_info_alloc(), see notes
 *       on it in the lifecycle management section!
 */
static struct sta_info *mesh_plink_alloc(struct ieee80211_sub_if_data *sdata,
					 u8 *hw_addr, u32 rates)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	if (local->num_sta >= MESH_MAX_PLINKS)
		return NULL;

	sta = sta_info_alloc(sdata, hw_addr, GFP_ATOMIC);
	if (!sta)
		return NULL;

	sta->flags = WLAN_STA_AUTHORIZED;
	sta->sta.supp_rates[local->hw.conf.channel->band] = rates;
	rate_control_rate_init(sta);

	return sta;
}

/**
 * mesh_plink_deactivate - deactivate mesh peer link
 *
 * @sta: mesh peer link to deactivate
 *
 * All mesh paths with this peer as next hop will be flushed
 *
 * Locking: the caller must hold sta->lock
 */
static void __mesh_plink_deactivate(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	if (sta->plink_state == PLINK_ESTAB)
		mesh_plink_dec_estab_count(sdata);
	sta->plink_state = PLINK_BLOCKED;
	mesh_path_flush_by_nexthop(sta);
}

/**
 * __mesh_plink_deactivate - deactivate mesh peer link
 *
 * @sta: mesh peer link to deactivate
 *
 * All mesh paths with this peer as next hop will be flushed
 */
void mesh_plink_deactivate(struct sta_info *sta)
{
	spin_lock_bh(&sta->lock);
	__mesh_plink_deactivate(sta);
	spin_unlock_bh(&sta->lock);
}

static int mesh_plink_frame_tx(struct ieee80211_sub_if_data *sdata,
		enum plink_frame_type action, u8 *da, __le16 llid, __le16 plid,
		__le16 reason) {
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb = dev_alloc_skb(local->hw.extra_tx_headroom + 400);
	struct ieee80211_mgmt *mgmt;
	bool include_plid = false;
	u8 *pos;
	int ie_len;

	if (!skb)
		return -1;
	skb_reserve(skb, local->hw.extra_tx_headroom);
	/* 25 is the size of the common mgmt part (24) plus the size of the
	 * common action part (1)
	 */
	mgmt = (struct ieee80211_mgmt *)
		skb_put(skb, 25 + sizeof(mgmt->u.action.u.plink_action));
	memset(mgmt, 0, 25 + sizeof(mgmt->u.action.u.plink_action));
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
	/* BSSID is left zeroed, wildcard value */
	mgmt->u.action.category = PLINK_CATEGORY;
	mgmt->u.action.u.plink_action.action_code = action;

	if (action == PLINK_CLOSE)
		mgmt->u.action.u.plink_action.aux = reason;
	else {
		mgmt->u.action.u.plink_action.aux = cpu_to_le16(0x0);
		if (action == PLINK_CONFIRM) {
			pos = skb_put(skb, 4);
			/* two-byte status code followed by two-byte AID */
			memset(pos, 0, 4);
		}
		mesh_mgmt_ies_add(skb, sdata);
	}

	/* Add Peer Link Management element */
	switch (action) {
	case PLINK_OPEN:
		ie_len = 3;
		break;
	case PLINK_CONFIRM:
		ie_len = 5;
		include_plid = true;
		break;
	case PLINK_CLOSE:
	default:
		if (!plid)
			ie_len = 5;
		else {
			ie_len = 7;
			include_plid = true;
		}
		break;
	}

	pos = skb_put(skb, 2 + ie_len);
	*pos++ = WLAN_EID_PEER_LINK;
	*pos++ = ie_len;
	*pos++ = action;
	memcpy(pos, &llid, 2);
	if (include_plid) {
		pos += 2;
		memcpy(pos, &plid, 2);
	}
	if (action == PLINK_CLOSE) {
		pos += 2;
		memcpy(pos, &reason, 2);
	}

	ieee80211_tx_skb(sdata, skb, 1);
	return 0;
}

void mesh_neighbour_update(u8 *hw_addr, u32 rates, struct ieee80211_sub_if_data *sdata,
			   bool peer_accepting_plinks)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	rcu_read_lock();

	sta = sta_info_get(local, hw_addr);
	if (!sta) {
		sta = mesh_plink_alloc(sdata, hw_addr, rates);
		if (!sta) {
			rcu_read_unlock();
			return;
		}
		if (sta_info_insert(sta)) {
			rcu_read_unlock();
			return;
		}
	}

	sta->last_rx = jiffies;
	sta->sta.supp_rates[local->hw.conf.channel->band] = rates;
	if (peer_accepting_plinks && sta->plink_state == PLINK_LISTEN &&
			sdata->u.mesh.accepting_plinks &&
			sdata->u.mesh.mshcfg.auto_open_plinks)
		mesh_plink_open(sta);

	rcu_read_unlock();
}

static void mesh_plink_timer(unsigned long data)
{
	struct sta_info *sta;
	__le16 llid, plid, reason;
	struct ieee80211_sub_if_data *sdata;

	/*
	 * This STA is valid because sta_info_destroy() will
	 * del_timer_sync() this timer after having made sure
	 * it cannot be readded (by deleting the plink.)
	 */
	sta = (struct sta_info *) data;

	spin_lock_bh(&sta->lock);
	if (sta->ignore_plink_timer) {
		sta->ignore_plink_timer = false;
		spin_unlock_bh(&sta->lock);
		return;
	}
	mpl_dbg("Mesh plink timer for %pM fired on state %d\n",
		sta->sta.addr, sta->plink_state);
	reason = 0;
	llid = sta->llid;
	plid = sta->plid;
	sdata = sta->sdata;

	switch (sta->plink_state) {
	case PLINK_OPN_RCVD:
	case PLINK_OPN_SNT:
		/* retry timer */
		if (sta->plink_retries < dot11MeshMaxRetries(sdata)) {
			u32 rand;
			mpl_dbg("Mesh plink for %pM (retry, timeout): %d %d\n",
				sta->sta.addr, sta->plink_retries,
				sta->plink_timeout);
			get_random_bytes(&rand, sizeof(u32));
			sta->plink_timeout = sta->plink_timeout +
					     rand % sta->plink_timeout;
			++sta->plink_retries;
			mod_plink_timer(sta, sta->plink_timeout);
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_OPEN, sta->sta.addr, llid,
					    0, 0);
			break;
		}
		reason = cpu_to_le16(MESH_MAX_RETRIES);
		/* fall through on else */
	case PLINK_CNF_RCVD:
		/* confirm timer */
		if (!reason)
			reason = cpu_to_le16(MESH_CONFIRM_TIMEOUT);
		sta->plink_state = PLINK_HOLDING;
		mod_plink_timer(sta, dot11MeshHoldingTimeout(sdata));
		spin_unlock_bh(&sta->lock);
		mesh_plink_frame_tx(sdata, PLINK_CLOSE, sta->sta.addr, llid, plid,
				    reason);
		break;
	case PLINK_HOLDING:
		/* holding timer */
		del_timer(&sta->plink_timer);
		mesh_plink_fsm_restart(sta);
		spin_unlock_bh(&sta->lock);
		break;
	default:
		spin_unlock_bh(&sta->lock);
		break;
	}
}

static inline void mesh_plink_timer_set(struct sta_info *sta, int timeout)
{
	sta->plink_timer.expires = jiffies + (HZ * timeout / 1000);
	sta->plink_timer.data = (unsigned long) sta;
	sta->plink_timer.function = mesh_plink_timer;
	sta->plink_timeout = timeout;
	add_timer(&sta->plink_timer);
}

int mesh_plink_open(struct sta_info *sta)
{
	__le16 llid;
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	spin_lock_bh(&sta->lock);
	get_random_bytes(&llid, 2);
	sta->llid = llid;
	if (sta->plink_state != PLINK_LISTEN) {
		spin_unlock_bh(&sta->lock);
		return -EBUSY;
	}
	sta->plink_state = PLINK_OPN_SNT;
	mesh_plink_timer_set(sta, dot11MeshRetryTimeout(sdata));
	spin_unlock_bh(&sta->lock);
	mpl_dbg("Mesh plink: starting establishment with %pM\n",
		sta->sta.addr);

	return mesh_plink_frame_tx(sdata, PLINK_OPEN,
				   sta->sta.addr, llid, 0, 0);
}

void mesh_plink_block(struct sta_info *sta)
{
	spin_lock_bh(&sta->lock);
	__mesh_plink_deactivate(sta);
	sta->plink_state = PLINK_BLOCKED;
	spin_unlock_bh(&sta->lock);
}


void mesh_rx_plink_frame(struct ieee80211_sub_if_data *sdata, struct ieee80211_mgmt *mgmt,
			 size_t len, struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee802_11_elems elems;
	struct sta_info *sta;
	enum plink_event event;
	enum plink_frame_type ftype;
	size_t baselen;
	u8 ie_len;
	u8 *baseaddr;
	__le16 plid, llid, reason;

	/* need action_code, aux */
	if (len < IEEE80211_MIN_ACTION_SIZE + 3)
		return;

	if (is_multicast_ether_addr(mgmt->da)) {
		mpl_dbg("Mesh plink: ignore frame from multicast address");
		return;
	}

	baseaddr = mgmt->u.action.u.plink_action.variable;
	baselen = (u8 *) mgmt->u.action.u.plink_action.variable - (u8 *) mgmt;
	if (mgmt->u.action.u.plink_action.action_code == PLINK_CONFIRM) {
		baseaddr += 4;
		baselen -= 4;
	}
	ieee802_11_parse_elems(baseaddr, len - baselen, &elems);
	if (!elems.peer_link) {
		mpl_dbg("Mesh plink: missing necessary peer link ie\n");
		return;
	}

	ftype = *((u8 *)PLINK_GET_FRAME_SUBTYPE(elems.peer_link));
	ie_len = elems.peer_link_len;
	if ((ftype == PLINK_OPEN && ie_len != 3) ||
	    (ftype == PLINK_CONFIRM && ie_len != 5) ||
	    (ftype == PLINK_CLOSE && ie_len != 5 && ie_len != 7)) {
		mpl_dbg("Mesh plink: incorrect plink ie length\n");
		return;
	}

	if (ftype != PLINK_CLOSE && (!elems.mesh_id || !elems.mesh_config)) {
		mpl_dbg("Mesh plink: missing necessary ie\n");
		return;
	}
	/* Note the lines below are correct, the llid in the frame is the plid
	 * from the point of view of this host.
	 */
	memcpy(&plid, PLINK_GET_LLID(elems.peer_link), 2);
	if (ftype == PLINK_CONFIRM || (ftype == PLINK_CLOSE && ie_len == 7))
		memcpy(&llid, PLINK_GET_PLID(elems.peer_link), 2);

	rcu_read_lock();

	sta = sta_info_get(local, mgmt->sa);
	if (!sta && ftype != PLINK_OPEN) {
		mpl_dbg("Mesh plink: cls or cnf from unknown peer\n");
		rcu_read_unlock();
		return;
	}

	if (sta && sta->plink_state == PLINK_BLOCKED) {
		rcu_read_unlock();
		return;
	}

	/* Now we will figure out the appropriate event... */
	event = PLINK_UNDEFINED;
	if (ftype != PLINK_CLOSE && (!mesh_matches_local(&elems, sdata))) {
		switch (ftype) {
		case PLINK_OPEN:
			event = OPN_RJCT;
			break;
		case PLINK_CONFIRM:
			event = CNF_RJCT;
			break;
		case PLINK_CLOSE:
			/* avoid warning */
			break;
		}
		spin_lock_bh(&sta->lock);
	} else if (!sta) {
		/* ftype == PLINK_OPEN */
		u32 rates;
		if (!mesh_plink_free_count(sdata)) {
			mpl_dbg("Mesh plink error: no more free plinks\n");
			rcu_read_unlock();
			return;
		}

		rates = ieee80211_sta_get_rates(local, &elems, rx_status->band);
		sta = mesh_plink_alloc(sdata, mgmt->sa, rates);
		if (!sta) {
			mpl_dbg("Mesh plink error: plink table full\n");
			rcu_read_unlock();
			return;
		}
		if (sta_info_insert(sta)) {
			rcu_read_unlock();
			return;
		}
		event = OPN_ACPT;
		spin_lock_bh(&sta->lock);
	} else {
		spin_lock_bh(&sta->lock);
		switch (ftype) {
		case PLINK_OPEN:
			if (!mesh_plink_free_count(sdata) ||
			    (sta->plid && sta->plid != plid))
				event = OPN_IGNR;
			else
				event = OPN_ACPT;
			break;
		case PLINK_CONFIRM:
			if (!mesh_plink_free_count(sdata) ||
			    (sta->llid != llid || sta->plid != plid))
				event = CNF_IGNR;
			else
				event = CNF_ACPT;
			break;
		case PLINK_CLOSE:
			if (sta->plink_state == PLINK_ESTAB)
				/* Do not check for llid or plid. This does not
				 * follow the standard but since multiple plinks
				 * per sta are not supported, it is necessary in
				 * order to avoid a livelock when MP A sees an
				 * establish peer link to MP B but MP B does not
				 * see it. This can be caused by a timeout in
				 * B's peer link establishment or B beign
				 * restarted.
				 */
				event = CLS_ACPT;
			else if (sta->plid != plid)
				event = CLS_IGNR;
			else if (ie_len == 7 && sta->llid != llid)
				event = CLS_IGNR;
			else
				event = CLS_ACPT;
			break;
		default:
			mpl_dbg("Mesh plink: unknown frame subtype\n");
			spin_unlock_bh(&sta->lock);
			rcu_read_unlock();
			return;
		}
	}

	mpl_dbg("Mesh plink (peer, state, llid, plid, event): %pM %d %d %d %d\n",
		mgmt->sa, sta->plink_state,
		le16_to_cpu(sta->llid), le16_to_cpu(sta->plid),
		event);
	reason = 0;
	switch (sta->plink_state) {
		/* spin_unlock as soon as state is updated at each case */
	case PLINK_LISTEN:
		switch (event) {
		case CLS_ACPT:
			mesh_plink_fsm_restart(sta);
			spin_unlock_bh(&sta->lock);
			break;
		case OPN_ACPT:
			sta->plink_state = PLINK_OPN_RCVD;
			sta->plid = plid;
			get_random_bytes(&llid, 2);
			sta->llid = llid;
			mesh_plink_timer_set(sta, dot11MeshRetryTimeout(sdata));
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_OPEN, sta->sta.addr, llid,
					    0, 0);
			mesh_plink_frame_tx(sdata, PLINK_CONFIRM, sta->sta.addr,
					    llid, plid, 0);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_OPN_SNT:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
			reason = cpu_to_le16(MESH_CAPABILITY_POLICY_VIOLATION);
		case CLS_ACPT:
			if (!reason)
				reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			sta->plink_state = PLINK_HOLDING;
			if (!mod_plink_timer(sta,
					     dot11MeshHoldingTimeout(sdata)))
				sta->ignore_plink_timer = true;

			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CLOSE, sta->sta.addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			/* retry timer is left untouched */
			sta->plink_state = PLINK_OPN_RCVD;
			sta->plid = plid;
			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CONFIRM, sta->sta.addr, llid,
					    plid, 0);
			break;
		case CNF_ACPT:
			sta->plink_state = PLINK_CNF_RCVD;
			if (!mod_plink_timer(sta,
					     dot11MeshConfirmTimeout(sdata)))
				sta->ignore_plink_timer = true;

			spin_unlock_bh(&sta->lock);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_OPN_RCVD:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
			reason = cpu_to_le16(MESH_CAPABILITY_POLICY_VIOLATION);
		case CLS_ACPT:
			if (!reason)
				reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			sta->plink_state = PLINK_HOLDING;
			if (!mod_plink_timer(sta,
					     dot11MeshHoldingTimeout(sdata)))
				sta->ignore_plink_timer = true;

			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CLOSE, sta->sta.addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CONFIRM, sta->sta.addr, llid,
					    plid, 0);
			break;
		case CNF_ACPT:
			del_timer(&sta->plink_timer);
			sta->plink_state = PLINK_ESTAB;
			mesh_plink_inc_estab_count(sdata);
			spin_unlock_bh(&sta->lock);
			mpl_dbg("Mesh plink with %pM ESTABLISHED\n",
				sta->sta.addr);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_CNF_RCVD:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
			reason = cpu_to_le16(MESH_CAPABILITY_POLICY_VIOLATION);
		case CLS_ACPT:
			if (!reason)
				reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			sta->plink_state = PLINK_HOLDING;
			if (!mod_plink_timer(sta,
					     dot11MeshHoldingTimeout(sdata)))
				sta->ignore_plink_timer = true;

			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CLOSE, sta->sta.addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			del_timer(&sta->plink_timer);
			sta->plink_state = PLINK_ESTAB;
			mesh_plink_inc_estab_count(sdata);
			spin_unlock_bh(&sta->lock);
			mpl_dbg("Mesh plink with %pM ESTABLISHED\n",
				sta->sta.addr);
			mesh_plink_frame_tx(sdata, PLINK_CONFIRM, sta->sta.addr, llid,
					    plid, 0);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_ESTAB:
		switch (event) {
		case CLS_ACPT:
			reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			__mesh_plink_deactivate(sta);
			sta->plink_state = PLINK_HOLDING;
			llid = sta->llid;
			mod_plink_timer(sta, dot11MeshHoldingTimeout(sdata));
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CLOSE, sta->sta.addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CONFIRM, sta->sta.addr, llid,
					    plid, 0);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;
	case PLINK_HOLDING:
		switch (event) {
		case CLS_ACPT:
			if (del_timer(&sta->plink_timer))
				sta->ignore_plink_timer = 1;
			mesh_plink_fsm_restart(sta);
			spin_unlock_bh(&sta->lock);
			break;
		case OPN_ACPT:
		case CNF_ACPT:
		case OPN_RJCT:
		case CNF_RJCT:
			llid = sta->llid;
			reason = sta->reason;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(sdata, PLINK_CLOSE, sta->sta.addr,
					    llid, plid, reason);
			break;
		default:
			spin_unlock_bh(&sta->lock);
		}
		break;
	default:
		/* should not get here, PLINK_BLOCKED is dealt with at the
		 * beggining of the function
		 */
		spin_unlock_bh(&sta->lock);
		break;
	}

	rcu_read_unlock();
}
