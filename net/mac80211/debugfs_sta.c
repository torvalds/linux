// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2003-2005	Devicescape Software, Inc.
 * Copyright (c) 2006	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 * Copyright (C) 2018 - 2019 Intel Corporation
 */

#include <linux/debugfs.h>
#include <linux/ieee80211.h>
#include "ieee80211_i.h"
#include "debugfs.h"
#include "debugfs_sta.h"
#include "sta_info.h"
#include "driver-ops.h"

/* sta attributtes */

#define STA_READ(name, field, format_string)				\
static ssize_t sta_ ##name## _read(struct file *file,			\
				   char __user *userbuf,		\
				   size_t count, loff_t *ppos)		\
{									\
	struct sta_info *sta = file->private_data;			\
	return mac80211_format_buffer(userbuf, count, ppos, 		\
				      format_string, sta->field);	\
}
#define STA_READ_D(name, field) STA_READ(name, field, "%d\n")

#define STA_OPS(name)							\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define STA_OPS_RW(name)						\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.write = sta_##name##_write,					\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define STA_FILE(name, field, format)					\
		STA_READ_##format(name, field)				\
		STA_OPS(name)

STA_FILE(aid, sta.aid, D);

static const char * const sta_flag_names[] = {
#define FLAG(F) [WLAN_STA_##F] = #F
	FLAG(AUTH),
	FLAG(ASSOC),
	FLAG(PS_STA),
	FLAG(AUTHORIZED),
	FLAG(SHORT_PREAMBLE),
	FLAG(WDS),
	FLAG(CLEAR_PS_FILT),
	FLAG(MFP),
	FLAG(BLOCK_BA),
	FLAG(PS_DRIVER),
	FLAG(PSPOLL),
	FLAG(TDLS_PEER),
	FLAG(TDLS_PEER_AUTH),
	FLAG(TDLS_INITIATOR),
	FLAG(TDLS_CHAN_SWITCH),
	FLAG(TDLS_OFF_CHANNEL),
	FLAG(TDLS_WIDER_BW),
	FLAG(UAPSD),
	FLAG(SP),
	FLAG(4ADDR_EVENT),
	FLAG(INSERTED),
	FLAG(RATE_CONTROL),
	FLAG(TOFFSET_KNOWN),
	FLAG(MPSP_OWNER),
	FLAG(MPSP_RECIPIENT),
	FLAG(PS_DELIVER),
#undef FLAG
};

static ssize_t sta_flags_read(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	char buf[16 * NUM_WLAN_STA_FLAGS], *pos = buf;
	char *end = buf + sizeof(buf) - 1;
	struct sta_info *sta = file->private_data;
	unsigned int flg;

	BUILD_BUG_ON(ARRAY_SIZE(sta_flag_names) != NUM_WLAN_STA_FLAGS);

	for (flg = 0; flg < NUM_WLAN_STA_FLAGS; flg++) {
		if (test_sta_flag(sta, flg))
			pos += scnprintf(pos, end - pos, "%s\n",
					 sta_flag_names[flg]);
	}

	return simple_read_from_buffer(userbuf, count, ppos, buf, strlen(buf));
}
STA_OPS(flags);

static ssize_t sta_num_ps_buf_frames_read(struct file *file,
					  char __user *userbuf,
					  size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	char buf[17*IEEE80211_NUM_ACS], *p = buf;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		p += scnprintf(p, sizeof(buf)+buf-p, "AC%d: %d\n", ac,
			       skb_queue_len(&sta->ps_tx_buf[ac]) +
			       skb_queue_len(&sta->tx_filtered[ac]));
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(num_ps_buf_frames);

static ssize_t sta_last_seq_ctrl_read(struct file *file, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	char buf[15*IEEE80211_NUM_TIDS], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	for (i = 0; i < IEEE80211_NUM_TIDS; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%x ",
			       le16_to_cpu(sta->last_seq_ctrl[i]));
	p += scnprintf(p, sizeof(buf)+buf-p, "\n");
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(last_seq_ctrl);

#define AQM_TXQ_ENTRY_LEN 130

static ssize_t sta_aqm_read(struct file *file, char __user *userbuf,
			size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	struct ieee80211_local *local = sta->local;
	size_t bufsz = AQM_TXQ_ENTRY_LEN * (IEEE80211_NUM_TIDS + 2);
	char *buf = kzalloc(bufsz, GFP_KERNEL), *p = buf;
	struct txq_info *txqi;
	ssize_t rv;
	int i;

	if (!buf)
		return -ENOMEM;

	spin_lock_bh(&local->fq.lock);
	rcu_read_lock();

	p += scnprintf(p,
		       bufsz+buf-p,
		       "target %uus interval %uus ecn %s\n",
		       codel_time_to_us(sta->cparams.target),
		       codel_time_to_us(sta->cparams.interval),
		       sta->cparams.ecn ? "yes" : "no");
	p += scnprintf(p,
		       bufsz+buf-p,
		       "tid ac backlog-bytes backlog-packets new-flows drops marks overlimit collisions tx-bytes tx-packets flags\n");

	for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
		if (!sta->sta.txq[i])
			continue;
		txqi = to_txq_info(sta->sta.txq[i]);
		p += scnprintf(p, bufsz+buf-p,
			       "%d %d %u %u %u %u %u %u %u %u %u 0x%lx(%s%s%s)\n",
			       txqi->txq.tid,
			       txqi->txq.ac,
			       txqi->tin.backlog_bytes,
			       txqi->tin.backlog_packets,
			       txqi->tin.flows,
			       txqi->cstats.drop_count,
			       txqi->cstats.ecn_mark,
			       txqi->tin.overlimit,
			       txqi->tin.collisions,
			       txqi->tin.tx_bytes,
			       txqi->tin.tx_packets,
			       txqi->flags,
			       test_bit(IEEE80211_TXQ_STOP, &txqi->flags) ? "STOP" : "RUN",
			       test_bit(IEEE80211_TXQ_AMPDU, &txqi->flags) ? " AMPDU" : "",
			       test_bit(IEEE80211_TXQ_NO_AMSDU, &txqi->flags) ? " NO-AMSDU" : "");
	}

	rcu_read_unlock();
	spin_unlock_bh(&local->fq.lock);

	rv = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return rv;
}
STA_OPS(aqm);

static ssize_t sta_airtime_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	struct ieee80211_local *local = sta->sdata->local;
	size_t bufsz = 200;
	char *buf = kzalloc(bufsz, GFP_KERNEL), *p = buf;
	u64 rx_airtime = 0, tx_airtime = 0;
	s64 deficit[IEEE80211_NUM_ACS];
	ssize_t rv;
	int ac;

	if (!buf)
		return -ENOMEM;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		spin_lock_bh(&local->active_txq_lock[ac]);
		rx_airtime += sta->airtime[ac].rx_airtime;
		tx_airtime += sta->airtime[ac].tx_airtime;
		deficit[ac] = sta->airtime[ac].deficit;
		spin_unlock_bh(&local->active_txq_lock[ac]);
	}

	p += scnprintf(p, bufsz + buf - p,
		"RX: %llu us\nTX: %llu us\nWeight: %u\n"
		"Deficit: VO: %lld us VI: %lld us BE: %lld us BK: %lld us\n",
		rx_airtime,
		tx_airtime,
		sta->airtime_weight,
		deficit[0],
		deficit[1],
		deficit[2],
		deficit[3]);

	rv = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return rv;
}

static ssize_t sta_airtime_write(struct file *file, const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	struct ieee80211_local *local = sta->sdata->local;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		spin_lock_bh(&local->active_txq_lock[ac]);
		sta->airtime[ac].rx_airtime = 0;
		sta->airtime[ac].tx_airtime = 0;
		sta->airtime[ac].deficit = sta->airtime_weight;
		spin_unlock_bh(&local->active_txq_lock[ac]);
	}

	return count;
}
STA_OPS_RW(airtime);

static ssize_t sta_agg_status_read(struct file *file, char __user *userbuf,
					size_t count, loff_t *ppos)
{
	char buf[71 + IEEE80211_NUM_TIDS * 40], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	struct tid_ampdu_rx *tid_rx;
	struct tid_ampdu_tx *tid_tx;

	rcu_read_lock();

	p += scnprintf(p, sizeof(buf) + buf - p, "next dialog_token: %#02x\n",
			sta->ampdu_mlme.dialog_token_allocator + 1);
	p += scnprintf(p, sizeof(buf) + buf - p,
		       "TID\t\tRX\tDTKN\tSSN\t\tTX\tDTKN\tpending\n");

	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		bool tid_rx_valid;

		tid_rx = rcu_dereference(sta->ampdu_mlme.tid_rx[i]);
		tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[i]);
		tid_rx_valid = test_bit(i, sta->ampdu_mlme.agg_session_valid);

		p += scnprintf(p, sizeof(buf) + buf - p, "%02d", i);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t\t%x",
			       tid_rx_valid);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.2x",
			       tid_rx_valid ?
					sta->ampdu_mlme.tid_rx_token[i] : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.3x",
				tid_rx ? tid_rx->ssn : 0);

		p += scnprintf(p, sizeof(buf) + buf - p, "\t\t%x", !!tid_tx);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.2x",
				tid_tx ? tid_tx->dialog_token : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%03d",
				tid_tx ? skb_queue_len(&tid_tx->pending) : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\n");
	}
	rcu_read_unlock();

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}

static ssize_t sta_agg_status_write(struct file *file, const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	char _buf[25] = {}, *buf = _buf;
	struct sta_info *sta = file->private_data;
	bool start, tx;
	unsigned long tid;
	char *pos;
	int ret, timeout = 5000;

	if (count > sizeof(_buf))
		return -EINVAL;

	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;

	buf[sizeof(_buf) - 1] = '\0';
	pos = buf;
	buf = strsep(&pos, " ");
	if (!buf)
		return -EINVAL;

	if (!strcmp(buf, "tx"))
		tx = true;
	else if (!strcmp(buf, "rx"))
		tx = false;
	else
		return -EINVAL;

	buf = strsep(&pos, " ");
	if (!buf)
		return -EINVAL;
	if (!strcmp(buf, "start")) {
		start = true;
		if (!tx)
			return -EINVAL;
	} else if (!strcmp(buf, "stop")) {
		start = false;
	} else {
		return -EINVAL;
	}

	buf = strsep(&pos, " ");
	if (!buf)
		return -EINVAL;
	if (sscanf(buf, "timeout=%d", &timeout) == 1) {
		buf = strsep(&pos, " ");
		if (!buf || !tx || !start)
			return -EINVAL;
	}

	ret = kstrtoul(buf, 0, &tid);
	if (ret || tid >= IEEE80211_NUM_TIDS)
		return -EINVAL;

	if (tx) {
		if (start)
			ret = ieee80211_start_tx_ba_session(&sta->sta, tid,
							    timeout);
		else
			ret = ieee80211_stop_tx_ba_session(&sta->sta, tid);
	} else {
		__ieee80211_stop_rx_ba_session(sta, tid, WLAN_BACK_RECIPIENT,
					       3, true);
		ret = 0;
	}

	return ret ?: count;
}
STA_OPS_RW(agg_status);

static ssize_t sta_ht_capa_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
#define PRINT_HT_CAP(_cond, _str) \
	do { \
	if (_cond) \
			p += scnprintf(p, sizeof(buf)+buf-p, "\t" _str "\n"); \
	} while (0)
	char buf[512], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	struct ieee80211_sta_ht_cap *htc = &sta->sta.ht_cap;

	p += scnprintf(p, sizeof(buf) + buf - p, "ht %ssupported\n",
			htc->ht_supported ? "" : "not ");
	if (htc->ht_supported) {
		p += scnprintf(p, sizeof(buf)+buf-p, "cap: %#.4x\n", htc->cap);

		PRINT_HT_CAP((htc->cap & BIT(0)), "RX LDPC");
		PRINT_HT_CAP((htc->cap & BIT(1)), "HT20/HT40");
		PRINT_HT_CAP(!(htc->cap & BIT(1)), "HT20");

		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 0, "Static SM Power Save");
		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 1, "Dynamic SM Power Save");
		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 3, "SM Power Save disabled");

		PRINT_HT_CAP((htc->cap & BIT(4)), "RX Greenfield");
		PRINT_HT_CAP((htc->cap & BIT(5)), "RX HT20 SGI");
		PRINT_HT_CAP((htc->cap & BIT(6)), "RX HT40 SGI");
		PRINT_HT_CAP((htc->cap & BIT(7)), "TX STBC");

		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 0, "No RX STBC");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 1, "RX STBC 1-stream");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 2, "RX STBC 2-streams");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 3, "RX STBC 3-streams");

		PRINT_HT_CAP((htc->cap & BIT(10)), "HT Delayed Block Ack");

		PRINT_HT_CAP(!(htc->cap & BIT(11)), "Max AMSDU length: "
			     "3839 bytes");
		PRINT_HT_CAP((htc->cap & BIT(11)), "Max AMSDU length: "
			     "7935 bytes");

		/*
		 * For beacons and probe response this would mean the BSS
		 * does or does not allow the usage of DSSS/CCK HT40.
		 * Otherwise it means the STA does or does not use
		 * DSSS/CCK HT40.
		 */
		PRINT_HT_CAP((htc->cap & BIT(12)), "DSSS/CCK HT40");
		PRINT_HT_CAP(!(htc->cap & BIT(12)), "No DSSS/CCK HT40");

		/* BIT(13) is reserved */

		PRINT_HT_CAP((htc->cap & BIT(14)), "40 MHz Intolerant");

		PRINT_HT_CAP((htc->cap & BIT(15)), "L-SIG TXOP protection");

		p += scnprintf(p, sizeof(buf)+buf-p, "ampdu factor/density: %d/%d\n",
				htc->ampdu_factor, htc->ampdu_density);
		p += scnprintf(p, sizeof(buf)+buf-p, "MCS mask:");

		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
			p += scnprintf(p, sizeof(buf)+buf-p, " %.2x",
					htc->mcs.rx_mask[i]);
		p += scnprintf(p, sizeof(buf)+buf-p, "\n");

		/* If not set this is meaningless */
		if (le16_to_cpu(htc->mcs.rx_highest)) {
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "MCS rx highest: %d Mbps\n",
				       le16_to_cpu(htc->mcs.rx_highest));
		}

		p += scnprintf(p, sizeof(buf)+buf-p, "MCS tx params: %x\n",
				htc->mcs.tx_params);
	}

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(ht_capa);

static ssize_t sta_vht_capa_read(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	char buf[512], *p = buf;
	struct sta_info *sta = file->private_data;
	struct ieee80211_sta_vht_cap *vhtc = &sta->sta.vht_cap;

	p += scnprintf(p, sizeof(buf) + buf - p, "VHT %ssupported\n",
			vhtc->vht_supported ? "" : "not ");
	if (vhtc->vht_supported) {
		p += scnprintf(p, sizeof(buf) + buf - p, "cap: %#.8x\n",
			       vhtc->cap);
#define PFLAG(a, b)							\
		do {							\
			if (vhtc->cap & IEEE80211_VHT_CAP_ ## a)	\
				p += scnprintf(p, sizeof(buf) + buf - p, \
					       "\t\t%s\n", b);		\
		} while (0)

		switch (vhtc->cap & 0x3) {
		case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\tMAX-MPDU-3895\n");
			break;
		case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\tMAX-MPDU-7991\n");
			break;
		case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\tMAX-MPDU-11454\n");
			break;
		default:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\tMAX-MPDU-UNKNOWN\n");
		}
		switch (vhtc->cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) {
		case 0:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\t80Mhz\n");
			break;
		case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\t160Mhz\n");
			break;
		case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\t80+80Mhz\n");
			break;
		default:
			p += scnprintf(p, sizeof(buf) + buf - p,
				       "\t\tUNKNOWN-MHZ: 0x%x\n",
				       (vhtc->cap >> 2) & 0x3);
		}
		PFLAG(RXLDPC, "RXLDPC");
		PFLAG(SHORT_GI_80, "SHORT-GI-80");
		PFLAG(SHORT_GI_160, "SHORT-GI-160");
		PFLAG(TXSTBC, "TXSTBC");
		p += scnprintf(p, sizeof(buf) + buf - p,
			       "\t\tRXSTBC_%d\n", (vhtc->cap >> 8) & 0x7);
		PFLAG(SU_BEAMFORMER_CAPABLE, "SU-BEAMFORMER-CAPABLE");
		PFLAG(SU_BEAMFORMEE_CAPABLE, "SU-BEAMFORMEE-CAPABLE");
		p += scnprintf(p, sizeof(buf) + buf - p,
			"\t\tBEAMFORMEE-STS: 0x%x\n",
			(vhtc->cap & IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK) >>
			IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);
		p += scnprintf(p, sizeof(buf) + buf - p,
			"\t\tSOUNDING-DIMENSIONS: 0x%x\n",
			(vhtc->cap & IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK)
			>> IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT);
		PFLAG(MU_BEAMFORMER_CAPABLE, "MU-BEAMFORMER-CAPABLE");
		PFLAG(MU_BEAMFORMEE_CAPABLE, "MU-BEAMFORMEE-CAPABLE");
		PFLAG(VHT_TXOP_PS, "TXOP-PS");
		PFLAG(HTC_VHT, "HTC-VHT");
		p += scnprintf(p, sizeof(buf) + buf - p,
			"\t\tMPDU-LENGTH-EXPONENT: 0x%x\n",
			(vhtc->cap & IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK) >>
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT);
		PFLAG(VHT_LINK_ADAPTATION_VHT_UNSOL_MFB,
		      "LINK-ADAPTATION-VHT-UNSOL-MFB");
		p += scnprintf(p, sizeof(buf) + buf - p,
			"\t\tLINK-ADAPTATION-VHT-MRQ-MFB: 0x%x\n",
			(vhtc->cap & IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB) >> 26);
		PFLAG(RX_ANTENNA_PATTERN, "RX-ANTENNA-PATTERN");
		PFLAG(TX_ANTENNA_PATTERN, "TX-ANTENNA-PATTERN");

		p += scnprintf(p, sizeof(buf)+buf-p, "RX MCS: %.4x\n",
			       le16_to_cpu(vhtc->vht_mcs.rx_mcs_map));
		if (vhtc->vht_mcs.rx_highest)
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "MCS RX highest: %d Mbps\n",
				       le16_to_cpu(vhtc->vht_mcs.rx_highest));
		p += scnprintf(p, sizeof(buf)+buf-p, "TX MCS: %.4x\n",
			       le16_to_cpu(vhtc->vht_mcs.tx_mcs_map));
		if (vhtc->vht_mcs.tx_highest)
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "MCS TX highest: %d Mbps\n",
				       le16_to_cpu(vhtc->vht_mcs.tx_highest));
#undef PFLAG
	}

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(vht_capa);

static ssize_t sta_he_capa_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	char *buf, *p;
	size_t buf_sz = PAGE_SIZE;
	struct sta_info *sta = file->private_data;
	struct ieee80211_sta_he_cap *hec = &sta->sta.he_cap;
	struct ieee80211_he_mcs_nss_supp *nss = &hec->he_mcs_nss_supp;
	u8 ppe_size;
	u8 *cap;
	int i;
	ssize_t ret;

	buf = kmalloc(buf_sz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = buf;

	p += scnprintf(p, buf_sz + buf - p, "HE %ssupported\n",
		       hec->has_he ? "" : "not ");
	if (!hec->has_he)
		goto out;

	cap = hec->he_cap_elem.mac_cap_info;
	p += scnprintf(p, buf_sz + buf - p,
		       "MAC-CAP: %#.2x %#.2x %#.2x %#.2x %#.2x %#.2x\n",
		       cap[0], cap[1], cap[2], cap[3], cap[4], cap[5]);

#define PRINT(fmt, ...)							\
	p += scnprintf(p, buf_sz + buf - p, "\t\t" fmt "\n",		\
		       ##__VA_ARGS__)

#define PFLAG(t, n, a, b)						\
	do {								\
		if (cap[n] & IEEE80211_HE_##t##_CAP##n##_##a)		\
			PRINT("%s", b);					\
	} while (0)

#define PFLAG_RANGE(t, i, n, s, m, off, fmt)				\
	do {								\
		u8 msk = IEEE80211_HE_##t##_CAP##i##_##n##_MASK;	\
		u8 idx = ((cap[i] & msk) >> (ffs(msk) - 1)) + off;	\
		PRINT(fmt, (s << idx) + (m * idx));			\
	} while (0)

#define PFLAG_RANGE_DEFAULT(t, i, n, s, m, off, fmt, a, b)		\
	do {								\
		if (cap[i] == IEEE80211_HE_##t ##_CAP##i##_##n##_##a) {	\
			PRINT("%s", b);					\
			break;						\
		}							\
		PFLAG_RANGE(t, i, n, s, m, off, fmt);			\
	} while (0)

	PFLAG(MAC, 0, HTC_HE, "HTC-HE");
	PFLAG(MAC, 0, TWT_REQ, "TWT-REQ");
	PFLAG(MAC, 0, TWT_RES, "TWT-RES");
	PFLAG_RANGE_DEFAULT(MAC, 0, DYNAMIC_FRAG, 0, 1, 0,
			    "DYNAMIC-FRAG-LEVEL-%d", NOT_SUPP, "NOT-SUPP");
	PFLAG_RANGE_DEFAULT(MAC, 0, MAX_NUM_FRAG_MSDU, 1, 0, 0,
			    "MAX-NUM-FRAG-MSDU-%d", UNLIMITED, "UNLIMITED");

	PFLAG_RANGE_DEFAULT(MAC, 1, MIN_FRAG_SIZE, 128, 0, -1,
			    "MIN-FRAG-SIZE-%d", UNLIMITED, "UNLIMITED");
	PFLAG_RANGE_DEFAULT(MAC, 1, TF_MAC_PAD_DUR, 0, 8, 0,
			    "TF-MAC-PAD-DUR-%dUS", MASK, "UNKNOWN");
	PFLAG_RANGE(MAC, 1, MULTI_TID_AGG_RX_QOS, 0, 1, 1,
		    "MULTI-TID-AGG-RX-QOS-%d");

	if (cap[0] & IEEE80211_HE_MAC_CAP0_HTC_HE) {
		switch (((cap[2] << 1) | (cap[1] >> 7)) & 0x3) {
		case 0:
			PRINT("LINK-ADAPTATION-NO-FEEDBACK");
			break;
		case 1:
			PRINT("LINK-ADAPTATION-RESERVED");
			break;
		case 2:
			PRINT("LINK-ADAPTATION-UNSOLICITED-FEEDBACK");
			break;
		case 3:
			PRINT("LINK-ADAPTATION-BOTH");
			break;
		}
	}

	PFLAG(MAC, 2, ALL_ACK, "ALL-ACK");
	PFLAG(MAC, 2, TRS, "TRS");
	PFLAG(MAC, 2, BSR, "BSR");
	PFLAG(MAC, 2, BCAST_TWT, "BCAST-TWT");
	PFLAG(MAC, 2, 32BIT_BA_BITMAP, "32BIT-BA-BITMAP");
	PFLAG(MAC, 2, MU_CASCADING, "MU-CASCADING");
	PFLAG(MAC, 2, ACK_EN, "ACK-EN");

	PFLAG(MAC, 3, OMI_CONTROL, "OMI-CONTROL");
	PFLAG(MAC, 3, OFDMA_RA, "OFDMA-RA");

	switch (cap[3] & IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK) {
	case IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_USE_VHT:
		PRINT("MAX-AMPDU-LEN-EXP-USE-VHT");
		break;
	case IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_1:
		PRINT("MAX-AMPDU-LEN-EXP-VHT-1");
		break;
	case IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_2:
		PRINT("MAX-AMPDU-LEN-EXP-VHT-2");
		break;
	case IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_RESERVED:
		PRINT("MAX-AMPDU-LEN-EXP-RESERVED");
		break;
	}

	PFLAG(MAC, 3, AMSDU_FRAG, "AMSDU-FRAG");
	PFLAG(MAC, 3, FLEX_TWT_SCHED, "FLEX-TWT-SCHED");
	PFLAG(MAC, 3, RX_CTRL_FRAME_TO_MULTIBSS, "RX-CTRL-FRAME-TO-MULTIBSS");

	PFLAG(MAC, 4, BSRP_BQRP_A_MPDU_AGG, "BSRP-BQRP-A-MPDU-AGG");
	PFLAG(MAC, 4, QTP, "QTP");
	PFLAG(MAC, 4, BQR, "BQR");
	PFLAG(MAC, 4, SRP_RESP, "SRP-RESP");
	PFLAG(MAC, 4, NDP_FB_REP, "NDP-FB-REP");
	PFLAG(MAC, 4, OPS, "OPS");
	PFLAG(MAC, 4, AMDSU_IN_AMPDU, "AMSDU-IN-AMPDU");

	PRINT("MULTI-TID-AGG-TX-QOS-%d", ((cap[5] << 1) | (cap[4] >> 7)) & 0x7);

	PFLAG(MAC, 5, SUBCHAN_SELECVITE_TRANSMISSION,
	      "SUBCHAN-SELECVITE-TRANSMISSION");
	PFLAG(MAC, 5, UL_2x996_TONE_RU, "UL-2x996-TONE-RU");
	PFLAG(MAC, 5, OM_CTRL_UL_MU_DATA_DIS_RX, "OM-CTRL-UL-MU-DATA-DIS-RX");
	PFLAG(MAC, 5, HE_DYNAMIC_SM_PS, "HE-DYNAMIC-SM-PS");
	PFLAG(MAC, 5, PUNCTURED_SOUNDING, "PUNCTURED-SOUNDING");
	PFLAG(MAC, 5, HT_VHT_TRIG_FRAME_RX, "HT-VHT-TRIG-FRAME-RX");

	cap = hec->he_cap_elem.phy_cap_info;
	p += scnprintf(p, buf_sz + buf - p,
		       "PHY CAP: %#.2x %#.2x %#.2x %#.2x %#.2x %#.2x %#.2x %#.2x %#.2x %#.2x %#.2x\n",
		       cap[0], cap[1], cap[2], cap[3], cap[4], cap[5], cap[6],
		       cap[7], cap[8], cap[9], cap[10]);

	PFLAG(PHY, 0, CHANNEL_WIDTH_SET_40MHZ_IN_2G,
	      "CHANNEL-WIDTH-SET-40MHZ-IN-2G");
	PFLAG(PHY, 0, CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G,
	      "CHANNEL-WIDTH-SET-40MHZ-80MHZ-IN-5G");
	PFLAG(PHY, 0, CHANNEL_WIDTH_SET_160MHZ_IN_5G,
	      "CHANNEL-WIDTH-SET-160MHZ-IN-5G");
	PFLAG(PHY, 0, CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
	      "CHANNEL-WIDTH-SET-80PLUS80-MHZ-IN-5G");
	PFLAG(PHY, 0, CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G,
	      "CHANNEL-WIDTH-SET-RU-MAPPING-IN-2G");
	PFLAG(PHY, 0, CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G,
	      "CHANNEL-WIDTH-SET-RU-MAPPING-IN-5G");

	switch (cap[1] & IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK) {
	case IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_20MHZ:
		PRINT("PREAMBLE-PUNC-RX-80MHZ-ONLY-SECOND-20MHZ");
		break;
	case IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_40MHZ:
		PRINT("PREAMBLE-PUNC-RX-80MHZ-ONLY-SECOND-40MHZ");
		break;
	case IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_20MHZ:
		PRINT("PREAMBLE-PUNC-RX-160MHZ-ONLY-SECOND-20MHZ");
		break;
	case IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_40MHZ:
		PRINT("PREAMBLE-PUNC-RX-160MHZ-ONLY-SECOND-40MHZ");
		break;
	}

	PFLAG(PHY, 1, DEVICE_CLASS_A,
	      "IEEE80211-HE-PHY-CAP1-DEVICE-CLASS-A");
	PFLAG(PHY, 1, LDPC_CODING_IN_PAYLOAD,
	      "LDPC-CODING-IN-PAYLOAD");
	PFLAG(PHY, 1, HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US,
	      "HY-CAP1-HE-LTF-AND-GI-FOR-HE-PPDUS-0-8US");
	PRINT("MIDAMBLE-RX-MAX-NSTS-%d", ((cap[2] << 1) | (cap[1] >> 7)) & 0x3);

	PFLAG(PHY, 2, NDP_4x_LTF_AND_3_2US, "NDP-4X-LTF-AND-3-2US");
	PFLAG(PHY, 2, STBC_TX_UNDER_80MHZ, "STBC-TX-UNDER-80MHZ");
	PFLAG(PHY, 2, STBC_RX_UNDER_80MHZ, "STBC-RX-UNDER-80MHZ");
	PFLAG(PHY, 2, DOPPLER_TX, "DOPPLER-TX");
	PFLAG(PHY, 2, DOPPLER_RX, "DOPPLER-RX");
	PFLAG(PHY, 2, UL_MU_FULL_MU_MIMO, "UL-MU-FULL-MU-MIMO");
	PFLAG(PHY, 2, UL_MU_PARTIAL_MU_MIMO, "UL-MU-PARTIAL-MU-MIMO");

	switch (cap[3] & IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK) {
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_NO_DCM:
		PRINT("DCM-MAX-CONST-TX-NO-DCM");
		break;
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK:
		PRINT("DCM-MAX-CONST-TX-BPSK");
		break;
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK:
		PRINT("DCM-MAX-CONST-TX-QPSK");
		break;
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM:
		PRINT("DCM-MAX-CONST-TX-16-QAM");
		break;
	}

	PFLAG(PHY, 3, DCM_MAX_TX_NSS_1, "DCM-MAX-TX-NSS-1");
	PFLAG(PHY, 3, DCM_MAX_TX_NSS_2, "DCM-MAX-TX-NSS-2");

	switch (cap[3] & IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK) {
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_NO_DCM:
		PRINT("DCM-MAX-CONST-RX-NO-DCM");
		break;
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK:
		PRINT("DCM-MAX-CONST-RX-BPSK");
		break;
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK:
		PRINT("DCM-MAX-CONST-RX-QPSK");
		break;
	case IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM:
		PRINT("DCM-MAX-CONST-RX-16-QAM");
		break;
	}

	PFLAG(PHY, 3, DCM_MAX_RX_NSS_1, "DCM-MAX-RX-NSS-1");
	PFLAG(PHY, 3, DCM_MAX_RX_NSS_2, "DCM-MAX-RX-NSS-2");
	PFLAG(PHY, 3, RX_HE_MU_PPDU_FROM_NON_AP_STA,
	      "RX-HE-MU-PPDU-FROM-NON-AP-STA");
	PFLAG(PHY, 3, SU_BEAMFORMER, "SU-BEAMFORMER");

	PFLAG(PHY, 4, SU_BEAMFORMEE, "SU-BEAMFORMEE");
	PFLAG(PHY, 4, MU_BEAMFORMER, "MU-BEAMFORMER");

	PFLAG_RANGE(PHY, 4, BEAMFORMEE_MAX_STS_UNDER_80MHZ, 0, 1, 4,
		    "BEAMFORMEE-MAX-STS-UNDER-%d");
	PFLAG_RANGE(PHY, 4, BEAMFORMEE_MAX_STS_ABOVE_80MHZ, 0, 1, 4,
		    "BEAMFORMEE-MAX-STS-ABOVE-%d");

	PFLAG_RANGE(PHY, 5, BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ, 0, 1, 1,
		    "NUM-SND-DIM-UNDER-80MHZ-%d");
	PFLAG_RANGE(PHY, 5, BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ, 0, 1, 1,
		    "NUM-SND-DIM-ABOVE-80MHZ-%d");
	PFLAG(PHY, 5, NG16_SU_FEEDBACK, "NG16-SU-FEEDBACK");
	PFLAG(PHY, 5, NG16_MU_FEEDBACK, "NG16-MU-FEEDBACK");

	PFLAG(PHY, 6, CODEBOOK_SIZE_42_SU, "CODEBOOK-SIZE-42-SU");
	PFLAG(PHY, 6, CODEBOOK_SIZE_75_MU, "CODEBOOK-SIZE-75-MU");
	PFLAG(PHY, 6, TRIG_SU_BEAMFORMER_FB, "TRIG-SU-BEAMFORMER-FB");
	PFLAG(PHY, 6, TRIG_MU_BEAMFORMER_FB, "TRIG-MU-BEAMFORMER-FB");
	PFLAG(PHY, 6, TRIG_CQI_FB, "TRIG-CQI-FB");
	PFLAG(PHY, 6, PARTIAL_BW_EXT_RANGE, "PARTIAL-BW-EXT-RANGE");
	PFLAG(PHY, 6, PARTIAL_BANDWIDTH_DL_MUMIMO,
	      "PARTIAL-BANDWIDTH-DL-MUMIMO");
	PFLAG(PHY, 6, PPE_THRESHOLD_PRESENT, "PPE-THRESHOLD-PRESENT");

	PFLAG(PHY, 7, SRP_BASED_SR, "SRP-BASED-SR");
	PFLAG(PHY, 7, POWER_BOOST_FACTOR_AR, "POWER-BOOST-FACTOR-AR");
	PFLAG(PHY, 7, HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
	      "HE-SU-MU-PPDU-4XLTF-AND-08-US-GI");
	PFLAG_RANGE(PHY, 7, MAX_NC, 0, 1, 1, "MAX-NC-%d");
	PFLAG(PHY, 7, STBC_TX_ABOVE_80MHZ, "STBC-TX-ABOVE-80MHZ");
	PFLAG(PHY, 7, STBC_RX_ABOVE_80MHZ, "STBC-RX-ABOVE-80MHZ");

	PFLAG(PHY, 8, HE_ER_SU_PPDU_4XLTF_AND_08_US_GI,
	      "HE-ER-SU-PPDU-4XLTF-AND-08-US-GI");
	PFLAG(PHY, 8, 20MHZ_IN_40MHZ_HE_PPDU_IN_2G,
	      "20MHZ-IN-40MHZ-HE-PPDU-IN-2G");
	PFLAG(PHY, 8, 20MHZ_IN_160MHZ_HE_PPDU, "20MHZ-IN-160MHZ-HE-PPDU");
	PFLAG(PHY, 8, 80MHZ_IN_160MHZ_HE_PPDU, "80MHZ-IN-160MHZ-HE-PPDU");
	PFLAG(PHY, 8, HE_ER_SU_1XLTF_AND_08_US_GI,
	      "HE-ER-SU-1XLTF-AND-08-US-GI");
	PFLAG(PHY, 8, MIDAMBLE_RX_TX_2X_AND_1XLTF,
	      "MIDAMBLE-RX-TX-2X-AND-1XLTF");

	switch (cap[8] & IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK) {
	case IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242:
		PRINT("DCM-MAX-RU-242");
		break;
	case IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484:
		PRINT("DCM-MAX-RU-484");
		break;
	case IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996:
		PRINT("DCM-MAX-RU-996");
		break;
	case IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_2x996:
		PRINT("DCM-MAX-RU-2x996");
		break;
	}

	PFLAG(PHY, 9, LONGER_THAN_16_SIGB_OFDM_SYM,
	      "LONGER-THAN-16-SIGB-OFDM-SYM");
	PFLAG(PHY, 9, NON_TRIGGERED_CQI_FEEDBACK,
	      "NON-TRIGGERED-CQI-FEEDBACK");
	PFLAG(PHY, 9, TX_1024_QAM_LESS_THAN_242_TONE_RU,
	      "TX-1024-QAM-LESS-THAN-242-TONE-RU");
	PFLAG(PHY, 9, RX_1024_QAM_LESS_THAN_242_TONE_RU,
	      "RX-1024-QAM-LESS-THAN-242-TONE-RU");
	PFLAG(PHY, 9, RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB,
	      "RX-FULL-BW-SU-USING-MU-WITH-COMP-SIGB");
	PFLAG(PHY, 9, RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB,
	      "RX-FULL-BW-SU-USING-MU-WITH-NON-COMP-SIGB");

	switch (cap[9] & IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_MASK) {
	case IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_0US:
		PRINT("NOMINAL-PACKET-PADDING-0US");
		break;
	case IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_8US:
		PRINT("NOMINAL-PACKET-PADDING-8US");
		break;
	case IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US:
		PRINT("NOMINAL-PACKET-PADDING-16US");
		break;
	}

#undef PFLAG_RANGE_DEFAULT
#undef PFLAG_RANGE
#undef PFLAG

#define PRINT_NSS_SUPP(f, n)						\
	do {								\
		int _i;							\
		u16 v = le16_to_cpu(nss->f);				\
		p += scnprintf(p, buf_sz + buf - p, n ": %#.4x\n", v);	\
		for (_i = 0; _i < 8; _i += 2) {				\
			switch ((v >> _i) & 0x3) {			\
			case 0:						\
				PRINT(n "-%d-SUPPORT-0-7", _i / 2);	\
				break;					\
			case 1:						\
				PRINT(n "-%d-SUPPORT-0-9", _i / 2);	\
				break;					\
			case 2:						\
				PRINT(n "-%d-SUPPORT-0-11", _i / 2);	\
				break;					\
			case 3:						\
				PRINT(n "-%d-NOT-SUPPORTED", _i / 2);	\
				break;					\
			}						\
		}							\
	} while (0)

	PRINT_NSS_SUPP(rx_mcs_80, "RX-MCS-80");
	PRINT_NSS_SUPP(tx_mcs_80, "TX-MCS-80");

	if (cap[0] & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) {
		PRINT_NSS_SUPP(rx_mcs_160, "RX-MCS-160");
		PRINT_NSS_SUPP(tx_mcs_160, "TX-MCS-160");
	}

	if (cap[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
		PRINT_NSS_SUPP(rx_mcs_80p80, "RX-MCS-80P80");
		PRINT_NSS_SUPP(tx_mcs_80p80, "TX-MCS-80P80");
	}

#undef PRINT_NSS_SUPP
#undef PRINT

	if (!(cap[6] & IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT))
		goto out;

	p += scnprintf(p, buf_sz + buf - p, "PPE-THRESHOLDS: %#.2x",
		       hec->ppe_thres[0]);

	ppe_size = ieee80211_he_ppe_size(hec->ppe_thres[0], cap);
	for (i = 1; i < ppe_size; i++) {
		p += scnprintf(p, buf_sz + buf - p, " %#.2x",
			       hec->ppe_thres[i]);
	}
	p += scnprintf(p, buf_sz + buf - p, "\n");

out:
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
	kfree(buf);
	return ret;
}
STA_OPS(he_capa);

#define DEBUGFS_ADD(name) \
	debugfs_create_file(#name, 0400, \
		sta->debugfs_dir, sta, &sta_ ##name## _ops);

#define DEBUGFS_ADD_COUNTER(name, field)				\
	if (sizeof(sta->field) == sizeof(u32))				\
		debugfs_create_u32(#name, 0400, sta->debugfs_dir,	\
			(u32 *) &sta->field);				\
	else								\
		debugfs_create_u64(#name, 0400, sta->debugfs_dir,	\
			(u64 *) &sta->field);

void ieee80211_sta_debugfs_add(struct sta_info *sta)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct dentry *stations_dir = sta->sdata->debugfs.subdir_stations;
	u8 mac[3*ETH_ALEN];

	if (!stations_dir)
		return;

	snprintf(mac, sizeof(mac), "%pM", sta->sta.addr);

	/*
	 * This might fail due to a race condition:
	 * When mac80211 unlinks a station, the debugfs entries
	 * remain, but it is already possible to link a new
	 * station with the same address which triggers adding
	 * it to debugfs; therefore, if the old station isn't
	 * destroyed quickly enough the old station's debugfs
	 * dir might still be around.
	 */
	sta->debugfs_dir = debugfs_create_dir(mac, stations_dir);

	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(aid);
	DEBUGFS_ADD(num_ps_buf_frames);
	DEBUGFS_ADD(last_seq_ctrl);
	DEBUGFS_ADD(agg_status);
	DEBUGFS_ADD(ht_capa);
	DEBUGFS_ADD(vht_capa);
	DEBUGFS_ADD(he_capa);

	DEBUGFS_ADD_COUNTER(rx_duplicates, rx_stats.num_duplicates);
	DEBUGFS_ADD_COUNTER(rx_fragments, rx_stats.fragments);
	DEBUGFS_ADD_COUNTER(tx_filtered, status_stats.filtered);

	if (local->ops->wake_tx_queue)
		DEBUGFS_ADD(aqm);

	if (wiphy_ext_feature_isset(local->hw.wiphy,
				    NL80211_EXT_FEATURE_AIRTIME_FAIRNESS))
		DEBUGFS_ADD(airtime);

	if (sizeof(sta->driver_buffered_tids) == sizeof(u32))
		debugfs_create_x32("driver_buffered_tids", 0400,
				   sta->debugfs_dir,
				   (u32 *)&sta->driver_buffered_tids);
	else
		debugfs_create_x64("driver_buffered_tids", 0400,
				   sta->debugfs_dir,
				   (u64 *)&sta->driver_buffered_tids);

	drv_sta_add_debugfs(local, sdata, &sta->sta, sta->debugfs_dir);
}

void ieee80211_sta_debugfs_remove(struct sta_info *sta)
{
	debugfs_remove_recursive(sta->debugfs_dir);
	sta->debugfs_dir = NULL;
}
