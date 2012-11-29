/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (c) 2000-2001, 2010, Code Aurora Forum. All rights reserved.

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#ifndef __HCI_CORE_H
#define __HCI_CORE_H

#include <net/bluetooth/hci.h>

/* HCI priority */
#define HCI_PRIO_MAX	7

/* HCI Core structures */
struct inquiry_data {
	bdaddr_t	bdaddr;
	__u8		pscan_rep_mode;
	__u8		pscan_period_mode;
	__u8		pscan_mode;
	__u8		dev_class[3];
	__le16		clock_offset;
	__s8		rssi;
	__u8		ssp_mode;
};

struct inquiry_entry {
	struct list_head	all;		/* inq_cache.all */
	struct list_head	list;		/* unknown or resolve */
	enum {
		NAME_NOT_KNOWN,
		NAME_NEEDED,
		NAME_PENDING,
		NAME_KNOWN,
	} name_state;
	__u32			timestamp;
	struct inquiry_data	data;
};

struct discovery_state {
	int			type;
	enum {
		DISCOVERY_STOPPED,
		DISCOVERY_STARTING,
		DISCOVERY_FINDING,
		DISCOVERY_RESOLVING,
		DISCOVERY_STOPPING,
	} state;
	struct list_head	all;	/* All devices found during inquiry */
	struct list_head	unknown;	/* Name state not known */
	struct list_head	resolve;	/* Name needs to be resolved */
	__u32			timestamp;
};

struct hci_conn_hash {
	struct list_head list;
	unsigned int     acl_num;
	unsigned int     amp_num;
	unsigned int     sco_num;
	unsigned int     le_num;
};

struct bdaddr_list {
	struct list_head list;
	bdaddr_t bdaddr;
};

struct bt_uuid {
	struct list_head list;
	u8 uuid[16];
	u8 svc_hint;
};

struct smp_ltk {
	struct list_head list;
	bdaddr_t bdaddr;
	u8 bdaddr_type;
	u8 authenticated;
	u8 type;
	u8 enc_size;
	__le16 ediv;
	u8 rand[8];
	u8 val[16];
} __packed;

struct link_key {
	struct list_head list;
	bdaddr_t bdaddr;
	u8 type;
	u8 val[HCI_LINK_KEY_SIZE];
	u8 pin_len;
};

struct oob_data {
	struct list_head list;
	bdaddr_t bdaddr;
	u8 hash[16];
	u8 randomizer[16];
};

struct le_scan_params {
	u8 type;
	u16 interval;
	u16 window;
	int timeout;
};

#define HCI_MAX_SHORT_NAME_LENGTH	10

struct amp_assoc {
	__u16	len;
	__u16	offset;
	__u16	rem_len;
	__u16	len_so_far;
	__u8	data[HCI_MAX_AMP_ASSOC_SIZE];
};

#define NUM_REASSEMBLY 4
struct hci_dev {
	struct list_head list;
	struct mutex	lock;

	char		name[8];
	unsigned long	flags;
	__u16		id;
	__u8		bus;
	__u8		dev_type;
	bdaddr_t	bdaddr;
	__u8		dev_name[HCI_MAX_NAME_LENGTH];
	__u8		short_name[HCI_MAX_SHORT_NAME_LENGTH];
	__u8		eir[HCI_MAX_EIR_LENGTH];
	__u8		dev_class[3];
	__u8		major_class;
	__u8		minor_class;
	__u8		features[8];
	__u8		host_features[8];
	__u8		commands[64];
	__u8		hci_ver;
	__u16		hci_rev;
	__u8		lmp_ver;
	__u16		manufacturer;
	__u16		lmp_subver;
	__u16		voice_setting;
	__u8		io_capability;
	__s8		inq_tx_power;
	__u16		devid_source;
	__u16		devid_vendor;
	__u16		devid_product;
	__u16		devid_version;

	__u16		pkt_type;
	__u16		esco_type;
	__u16		link_policy;
	__u16		link_mode;

	__u32		idle_timeout;
	__u16		sniff_min_interval;
	__u16		sniff_max_interval;

	__u8		amp_status;
	__u32		amp_total_bw;
	__u32		amp_max_bw;
	__u32		amp_min_latency;
	__u32		amp_max_pdu;
	__u8		amp_type;
	__u16		amp_pal_cap;
	__u16		amp_assoc_size;
	__u32		amp_max_flush_to;
	__u32		amp_be_flush_to;

	struct amp_assoc	loc_assoc;

	__u8		flow_ctl_mode;

	unsigned int	auto_accept_delay;

	unsigned long	quirks;

	atomic_t	cmd_cnt;
	unsigned int	acl_cnt;
	unsigned int	sco_cnt;
	unsigned int	le_cnt;

	unsigned int	acl_mtu;
	unsigned int	sco_mtu;
	unsigned int	le_mtu;
	unsigned int	acl_pkts;
	unsigned int	sco_pkts;
	unsigned int	le_pkts;

	__u16		block_len;
	__u16		block_mtu;
	__u16		num_blocks;
	__u16		block_cnt;

	unsigned long	acl_last_tx;
	unsigned long	sco_last_tx;
	unsigned long	le_last_tx;

	struct workqueue_struct	*workqueue;

	struct work_struct	power_on;
	struct delayed_work	power_off;

	__u16			discov_timeout;
	struct delayed_work	discov_off;

	struct delayed_work	service_cache;

	struct timer_list	cmd_timer;

	struct work_struct	rx_work;
	struct work_struct	cmd_work;
	struct work_struct	tx_work;

	struct sk_buff_head	rx_q;
	struct sk_buff_head	raw_q;
	struct sk_buff_head	cmd_q;

	struct sk_buff		*sent_cmd;
	struct sk_buff		*reassembly[NUM_REASSEMBLY];

	struct mutex		req_lock;
	wait_queue_head_t	req_wait_q;
	__u32			req_status;
	__u32			req_result;

	__u16			init_last_cmd;

	struct list_head	mgmt_pending;

	struct discovery_state	discovery;
	struct hci_conn_hash	conn_hash;
	struct list_head	blacklist;

	struct list_head	uuids;

	struct list_head	link_keys;

	struct list_head	long_term_keys;

	struct list_head	remote_oob_data;

	struct hci_dev_stats	stat;

	struct sk_buff_head	driver_init;

	atomic_t		promisc;

	struct dentry		*debugfs;

	struct device		dev;

	struct rfkill		*rfkill;

	unsigned long		dev_flags;

	struct delayed_work	le_scan_disable;

	struct work_struct	le_scan;
	struct le_scan_params	le_scan_params;

	__s8			adv_tx_power;
	__u8			adv_data[HCI_MAX_AD_LENGTH];
	__u8			adv_data_len;

	int (*open)(struct hci_dev *hdev);
	int (*close)(struct hci_dev *hdev);
	int (*flush)(struct hci_dev *hdev);
	int (*send)(struct sk_buff *skb);
	void (*notify)(struct hci_dev *hdev, unsigned int evt);
	int (*ioctl)(struct hci_dev *hdev, unsigned int cmd, unsigned long arg);
};

#define HCI_PHY_HANDLE(handle)	(handle & 0xff)

struct hci_conn {
	struct list_head list;

	atomic_t	refcnt;

	bdaddr_t	dst;
	__u8		dst_type;
	__u16		handle;
	__u16		state;
	__u8		mode;
	__u8		type;
	bool		out;
	__u8		attempt;
	__u8		dev_class[3];
	__u8		features[8];
	__u16		interval;
	__u16		pkt_type;
	__u16		link_policy;
	__u32		link_mode;
	__u8		key_type;
	__u8		auth_type;
	__u8		sec_level;
	__u8		pending_sec_level;
	__u8		pin_length;
	__u8		enc_key_size;
	__u8		io_capability;
	__u32		passkey_notify;
	__u8		passkey_entered;
	__u16		disc_timeout;
	unsigned long	flags;

	__u8		remote_cap;
	__u8		remote_auth;
	__u8		remote_id;
	bool		flush_key;

	unsigned int	sent;

	struct sk_buff_head data_q;
	struct list_head chan_list;

	struct delayed_work disc_work;
	struct timer_list idle_timer;
	struct timer_list auto_accept_timer;

	struct device	dev;
	atomic_t	devref;

	struct hci_dev	*hdev;
	void		*l2cap_data;
	void		*sco_data;
	void		*smp_conn;
	struct amp_mgr	*amp_mgr;

	struct hci_conn	*link;

	void (*connect_cfm_cb)	(struct hci_conn *conn, u8 status);
	void (*security_cfm_cb)	(struct hci_conn *conn, u8 status);
	void (*disconn_cfm_cb)	(struct hci_conn *conn, u8 reason);
};

struct hci_chan {
	struct list_head list;
	__u16 handle;
	struct hci_conn *conn;
	struct sk_buff_head data_q;
	unsigned int	sent;
	__u8		state;
};

extern struct list_head hci_dev_list;
extern struct list_head hci_cb_list;
extern rwlock_t hci_dev_list_lock;
extern rwlock_t hci_cb_list_lock;

/* ----- HCI interface to upper protocols ----- */
extern int l2cap_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr);
extern void l2cap_connect_cfm(struct hci_conn *hcon, u8 status);
extern int l2cap_disconn_ind(struct hci_conn *hcon);
extern void l2cap_disconn_cfm(struct hci_conn *hcon, u8 reason);
extern int l2cap_security_cfm(struct hci_conn *hcon, u8 status, u8 encrypt);
extern int l2cap_recv_acldata(struct hci_conn *hcon, struct sk_buff *skb,
			      u16 flags);

extern int sco_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr, __u8 *flags);
extern void sco_connect_cfm(struct hci_conn *hcon, __u8 status);
extern void sco_disconn_cfm(struct hci_conn *hcon, __u8 reason);
extern int sco_recv_scodata(struct hci_conn *hcon, struct sk_buff *skb);

/* ----- Inquiry cache ----- */
#define INQUIRY_CACHE_AGE_MAX   (HZ*30)   /* 30 seconds */
#define INQUIRY_ENTRY_AGE_MAX   (HZ*60)   /* 60 seconds */

static inline void discovery_init(struct hci_dev *hdev)
{
	hdev->discovery.state = DISCOVERY_STOPPED;
	INIT_LIST_HEAD(&hdev->discovery.all);
	INIT_LIST_HEAD(&hdev->discovery.unknown);
	INIT_LIST_HEAD(&hdev->discovery.resolve);
}

bool hci_discovery_active(struct hci_dev *hdev);

void hci_discovery_set_state(struct hci_dev *hdev, int state);

static inline int inquiry_cache_empty(struct hci_dev *hdev)
{
	return list_empty(&hdev->discovery.all);
}

static inline long inquiry_cache_age(struct hci_dev *hdev)
{
	struct discovery_state *c = &hdev->discovery;
	return jiffies - c->timestamp;
}

static inline long inquiry_entry_age(struct inquiry_entry *e)
{
	return jiffies - e->timestamp;
}

struct inquiry_entry *hci_inquiry_cache_lookup(struct hci_dev *hdev,
					       bdaddr_t *bdaddr);
struct inquiry_entry *hci_inquiry_cache_lookup_unknown(struct hci_dev *hdev,
						       bdaddr_t *bdaddr);
struct inquiry_entry *hci_inquiry_cache_lookup_resolve(struct hci_dev *hdev,
						       bdaddr_t *bdaddr,
						       int state);
void hci_inquiry_cache_update_resolve(struct hci_dev *hdev,
				      struct inquiry_entry *ie);
bool hci_inquiry_cache_update(struct hci_dev *hdev, struct inquiry_data *data,
			      bool name_known, bool *ssp);

/* ----- HCI Connections ----- */
enum {
	HCI_CONN_AUTH_PEND,
	HCI_CONN_REAUTH_PEND,
	HCI_CONN_ENCRYPT_PEND,
	HCI_CONN_RSWITCH_PEND,
	HCI_CONN_MODE_CHANGE_PEND,
	HCI_CONN_SCO_SETUP_PEND,
	HCI_CONN_LE_SMP_PEND,
	HCI_CONN_MGMT_CONNECTED,
	HCI_CONN_SSP_ENABLED,
	HCI_CONN_POWER_SAVE,
	HCI_CONN_REMOTE_OOB,
};

static inline bool hci_conn_ssp_enabled(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	return test_bit(HCI_SSP_ENABLED, &hdev->dev_flags) &&
	       test_bit(HCI_CONN_SSP_ENABLED, &conn->flags);
}

static inline void hci_conn_hash_add(struct hci_dev *hdev, struct hci_conn *c)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	list_add_rcu(&c->list, &h->list);
	switch (c->type) {
	case ACL_LINK:
		h->acl_num++;
		break;
	case AMP_LINK:
		h->amp_num++;
		break;
	case LE_LINK:
		h->le_num++;
		break;
	case SCO_LINK:
	case ESCO_LINK:
		h->sco_num++;
		break;
	}
}

static inline void hci_conn_hash_del(struct hci_dev *hdev, struct hci_conn *c)
{
	struct hci_conn_hash *h = &hdev->conn_hash;

	list_del_rcu(&c->list);
	synchronize_rcu();

	switch (c->type) {
	case ACL_LINK:
		h->acl_num--;
		break;
	case AMP_LINK:
		h->amp_num--;
		break;
	case LE_LINK:
		h->le_num--;
		break;
	case SCO_LINK:
	case ESCO_LINK:
		h->sco_num--;
		break;
	}
}

static inline unsigned int hci_conn_num(struct hci_dev *hdev, __u8 type)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	switch (type) {
	case ACL_LINK:
		return h->acl_num;
	case AMP_LINK:
		return h->amp_num;
	case LE_LINK:
		return h->le_num;
	case SCO_LINK:
	case ESCO_LINK:
		return h->sco_num;
	default:
		return 0;
	}
}

static inline struct hci_conn *hci_conn_hash_lookup_handle(struct hci_dev *hdev,
								__u16 handle)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn  *c;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &h->list, list) {
		if (c->handle == handle) {
			rcu_read_unlock();
			return c;
		}
	}
	rcu_read_unlock();

	return NULL;
}

static inline struct hci_conn *hci_conn_hash_lookup_ba(struct hci_dev *hdev,
							__u8 type, bdaddr_t *ba)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn  *c;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &h->list, list) {
		if (c->type == type && !bacmp(&c->dst, ba)) {
			rcu_read_unlock();
			return c;
		}
	}

	rcu_read_unlock();

	return NULL;
}

static inline struct hci_conn *hci_conn_hash_lookup_state(struct hci_dev *hdev,
							__u8 type, __u16 state)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn  *c;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &h->list, list) {
		if (c->type == type && c->state == state) {
			rcu_read_unlock();
			return c;
		}
	}

	rcu_read_unlock();

	return NULL;
}

void hci_acl_disconn(struct hci_conn *conn, __u8 reason);
void hci_setup_sync(struct hci_conn *conn, __u16 handle);
void hci_sco_setup(struct hci_conn *conn, __u8 status);

struct hci_conn *hci_conn_add(struct hci_dev *hdev, int type, bdaddr_t *dst);
int hci_conn_del(struct hci_conn *conn);
void hci_conn_hash_flush(struct hci_dev *hdev);
void hci_conn_check_pending(struct hci_dev *hdev);
void hci_conn_accept(struct hci_conn *conn, int mask);

struct hci_chan *hci_chan_create(struct hci_conn *conn);
void hci_chan_del(struct hci_chan *chan);
void hci_chan_list_flush(struct hci_conn *conn);
struct hci_chan *hci_chan_lookup_handle(struct hci_dev *hdev, __u16 handle);

struct hci_conn *hci_connect(struct hci_dev *hdev, int type, bdaddr_t *dst,
			     __u8 dst_type, __u8 sec_level, __u8 auth_type);
int hci_conn_check_link_mode(struct hci_conn *conn);
int hci_conn_check_secure(struct hci_conn *conn, __u8 sec_level);
int hci_conn_security(struct hci_conn *conn, __u8 sec_level, __u8 auth_type);
int hci_conn_change_link_key(struct hci_conn *conn);
int hci_conn_switch_role(struct hci_conn *conn, __u8 role);

void hci_conn_enter_active_mode(struct hci_conn *conn, __u8 force_active);

void hci_conn_hold_device(struct hci_conn *conn);
void hci_conn_put_device(struct hci_conn *conn);

static inline void hci_conn_hold(struct hci_conn *conn)
{
	BT_DBG("hcon %p orig refcnt %d", conn, atomic_read(&conn->refcnt));

	atomic_inc(&conn->refcnt);
	cancel_delayed_work(&conn->disc_work);
}

static inline void hci_conn_put(struct hci_conn *conn)
{
	BT_DBG("hcon %p orig refcnt %d", conn, atomic_read(&conn->refcnt));

	if (atomic_dec_and_test(&conn->refcnt)) {
		unsigned long timeo;

		switch (conn->type) {
		case ACL_LINK:
		case LE_LINK:
			del_timer(&conn->idle_timer);
			if (conn->state == BT_CONNECTED) {
				timeo = conn->disc_timeout;
				if (!conn->out)
					timeo *= 2;
			} else {
				timeo = msecs_to_jiffies(10);
			}
			break;

		case AMP_LINK:
			timeo = conn->disc_timeout;
			break;

		default:
			timeo = msecs_to_jiffies(10);
			break;
		}

		cancel_delayed_work(&conn->disc_work);
		queue_delayed_work(conn->hdev->workqueue,
				   &conn->disc_work, timeo);
	}
}

/* ----- HCI Devices ----- */
static inline void hci_dev_put(struct hci_dev *d)
{
	BT_DBG("%s orig refcnt %d", d->name,
	       atomic_read(&d->dev.kobj.kref.refcount));

	put_device(&d->dev);
}

static inline struct hci_dev *hci_dev_hold(struct hci_dev *d)
{
	BT_DBG("%s orig refcnt %d", d->name,
	       atomic_read(&d->dev.kobj.kref.refcount));

	get_device(&d->dev);
	return d;
}

#define hci_dev_lock(d)		mutex_lock(&d->lock)
#define hci_dev_unlock(d)	mutex_unlock(&d->lock)

#define to_hci_dev(d) container_of(d, struct hci_dev, dev)
#define to_hci_conn(c) container_of(c, struct hci_conn, dev)

static inline void *hci_get_drvdata(struct hci_dev *hdev)
{
	return dev_get_drvdata(&hdev->dev);
}

static inline void hci_set_drvdata(struct hci_dev *hdev, void *data)
{
	dev_set_drvdata(&hdev->dev, data);
}

/* hci_dev_list shall be locked */
static inline uint8_t __hci_num_ctrl(void)
{
	uint8_t count = 0;
	struct list_head *p;

	list_for_each(p, &hci_dev_list) {
		count++;
	}

	return count;
}

struct hci_dev *hci_dev_get(int index);
struct hci_dev *hci_get_route(bdaddr_t *dst, bdaddr_t *src);

struct hci_dev *hci_alloc_dev(void);
void hci_free_dev(struct hci_dev *hdev);
int hci_register_dev(struct hci_dev *hdev);
void hci_unregister_dev(struct hci_dev *hdev);
int hci_suspend_dev(struct hci_dev *hdev);
int hci_resume_dev(struct hci_dev *hdev);
int hci_dev_open(__u16 dev);
int hci_dev_close(__u16 dev);
int hci_dev_reset(__u16 dev);
int hci_dev_reset_stat(__u16 dev);
int hci_dev_cmd(unsigned int cmd, void __user *arg);
int hci_get_dev_list(void __user *arg);
int hci_get_dev_info(void __user *arg);
int hci_get_conn_list(void __user *arg);
int hci_get_conn_info(struct hci_dev *hdev, void __user *arg);
int hci_get_auth_info(struct hci_dev *hdev, void __user *arg);
int hci_inquiry(void __user *arg);

struct bdaddr_list *hci_blacklist_lookup(struct hci_dev *hdev,
					 bdaddr_t *bdaddr);
int hci_blacklist_clear(struct hci_dev *hdev);
int hci_blacklist_add(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 type);
int hci_blacklist_del(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 type);

int hci_uuids_clear(struct hci_dev *hdev);

int hci_link_keys_clear(struct hci_dev *hdev);
struct link_key *hci_find_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr);
int hci_add_link_key(struct hci_dev *hdev, struct hci_conn *conn, int new_key,
		     bdaddr_t *bdaddr, u8 *val, u8 type, u8 pin_len);
struct smp_ltk *hci_find_ltk(struct hci_dev *hdev, __le16 ediv, u8 rand[8]);
int hci_add_ltk(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 addr_type, u8 type,
		int new_key, u8 authenticated, u8 tk[16], u8 enc_size,
		__le16 ediv, u8 rand[8]);
struct smp_ltk *hci_find_ltk_by_addr(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 addr_type);
int hci_remove_ltk(struct hci_dev *hdev, bdaddr_t *bdaddr);
int hci_smp_ltks_clear(struct hci_dev *hdev);
int hci_remove_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr);

int hci_remote_oob_data_clear(struct hci_dev *hdev);
struct oob_data *hci_find_remote_oob_data(struct hci_dev *hdev,
							bdaddr_t *bdaddr);
int hci_add_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 *hash,
								u8 *randomizer);
int hci_remove_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr);

int hci_update_ad(struct hci_dev *hdev);

void hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb);

int hci_recv_frame(struct sk_buff *skb);
int hci_recv_fragment(struct hci_dev *hdev, int type, void *data, int count);
int hci_recv_stream_fragment(struct hci_dev *hdev, void *data, int count);

void hci_init_sysfs(struct hci_dev *hdev);
int hci_add_sysfs(struct hci_dev *hdev);
void hci_del_sysfs(struct hci_dev *hdev);
void hci_conn_init_sysfs(struct hci_conn *conn);
void hci_conn_add_sysfs(struct hci_conn *conn);
void hci_conn_del_sysfs(struct hci_conn *conn);

#define SET_HCIDEV_DEV(hdev, pdev) ((hdev)->dev.parent = (pdev))

/* ----- LMP capabilities ----- */
#define lmp_encrypt_capable(dev)   ((dev)->features[0] & LMP_ENCRYPT)
#define lmp_rswitch_capable(dev)   ((dev)->features[0] & LMP_RSWITCH)
#define lmp_hold_capable(dev)      ((dev)->features[0] & LMP_HOLD)
#define lmp_sniff_capable(dev)     ((dev)->features[0] & LMP_SNIFF)
#define lmp_park_capable(dev)      ((dev)->features[1] & LMP_PARK)
#define lmp_inq_rssi_capable(dev)  ((dev)->features[3] & LMP_RSSI_INQ)
#define lmp_esco_capable(dev)      ((dev)->features[3] & LMP_ESCO)
#define lmp_bredr_capable(dev)     (!((dev)->features[4] & LMP_NO_BREDR))
#define lmp_le_capable(dev)        ((dev)->features[4] & LMP_LE)
#define lmp_sniffsubr_capable(dev) ((dev)->features[5] & LMP_SNIFF_SUBR)
#define lmp_pause_enc_capable(dev) ((dev)->features[5] & LMP_PAUSE_ENC)
#define lmp_ext_inq_capable(dev)   ((dev)->features[6] & LMP_EXT_INQ)
#define lmp_le_br_capable(dev)     !!((dev)->features[6] & LMP_SIMUL_LE_BR)
#define lmp_ssp_capable(dev)       ((dev)->features[6] & LMP_SIMPLE_PAIR)
#define lmp_no_flush_capable(dev)  ((dev)->features[6] & LMP_NO_FLUSH)
#define lmp_lsto_capable(dev)      ((dev)->features[7] & LMP_LSTO)
#define lmp_inq_tx_pwr_capable(dev) ((dev)->features[7] & LMP_INQ_TX_PWR)
#define lmp_ext_feat_capable(dev)  ((dev)->features[7] & LMP_EXTFEATURES)

/* ----- Extended LMP capabilities ----- */
#define lmp_host_ssp_capable(dev)  ((dev)->host_features[0] & LMP_HOST_SSP)
#define lmp_host_le_capable(dev)   !!((dev)->host_features[0] & LMP_HOST_LE)
#define lmp_host_le_br_capable(dev) !!((dev)->host_features[0] & LMP_HOST_LE_BREDR)

/* returns true if at least one AMP active */
static inline bool hci_amp_capable(void)
{
	struct hci_dev *hdev;
	bool ret = false;

	read_lock(&hci_dev_list_lock);
	list_for_each_entry(hdev, &hci_dev_list, list)
		if (hdev->amp_type == HCI_AMP &&
		    test_bit(HCI_UP, &hdev->flags))
			ret = true;
	read_unlock(&hci_dev_list_lock);

	return ret;
}

/* ----- HCI protocols ----- */
#define HCI_PROTO_DEFER             0x01

static inline int hci_proto_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr,
					__u8 type, __u8 *flags)
{
	switch (type) {
	case ACL_LINK:
		return l2cap_connect_ind(hdev, bdaddr);

	case SCO_LINK:
	case ESCO_LINK:
		return sco_connect_ind(hdev, bdaddr, flags);

	default:
		BT_ERR("unknown link type %d", type);
		return -EINVAL;
	}
}

static inline void hci_proto_connect_cfm(struct hci_conn *conn, __u8 status)
{
	switch (conn->type) {
	case ACL_LINK:
	case LE_LINK:
		l2cap_connect_cfm(conn, status);
		break;

	case SCO_LINK:
	case ESCO_LINK:
		sco_connect_cfm(conn, status);
		break;

	default:
		BT_ERR("unknown link type %d", conn->type);
		break;
	}

	if (conn->connect_cfm_cb)
		conn->connect_cfm_cb(conn, status);
}

static inline int hci_proto_disconn_ind(struct hci_conn *conn)
{
	if (conn->type != ACL_LINK && conn->type != LE_LINK)
		return HCI_ERROR_REMOTE_USER_TERM;

	return l2cap_disconn_ind(conn);
}

static inline void hci_proto_disconn_cfm(struct hci_conn *conn, __u8 reason)
{
	switch (conn->type) {
	case ACL_LINK:
	case LE_LINK:
		l2cap_disconn_cfm(conn, reason);
		break;

	case SCO_LINK:
	case ESCO_LINK:
		sco_disconn_cfm(conn, reason);
		break;

	/* L2CAP would be handled for BREDR chan */
	case AMP_LINK:
		break;

	default:
		BT_ERR("unknown link type %d", conn->type);
		break;
	}

	if (conn->disconn_cfm_cb)
		conn->disconn_cfm_cb(conn, reason);
}

static inline void hci_proto_auth_cfm(struct hci_conn *conn, __u8 status)
{
	__u8 encrypt;

	if (conn->type != ACL_LINK && conn->type != LE_LINK)
		return;

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags))
		return;

	encrypt = (conn->link_mode & HCI_LM_ENCRYPT) ? 0x01 : 0x00;
	l2cap_security_cfm(conn, status, encrypt);

	if (conn->security_cfm_cb)
		conn->security_cfm_cb(conn, status);
}

static inline void hci_proto_encrypt_cfm(struct hci_conn *conn, __u8 status,
								__u8 encrypt)
{
	if (conn->type != ACL_LINK && conn->type != LE_LINK)
		return;

	l2cap_security_cfm(conn, status, encrypt);

	if (conn->security_cfm_cb)
		conn->security_cfm_cb(conn, status);
}

/* ----- HCI callbacks ----- */
struct hci_cb {
	struct list_head list;

	char *name;

	void (*security_cfm)	(struct hci_conn *conn, __u8 status,
								__u8 encrypt);
	void (*key_change_cfm)	(struct hci_conn *conn, __u8 status);
	void (*role_switch_cfm)	(struct hci_conn *conn, __u8 status, __u8 role);
};

static inline void hci_auth_cfm(struct hci_conn *conn, __u8 status)
{
	struct hci_cb *cb;
	__u8 encrypt;

	hci_proto_auth_cfm(conn, status);

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags))
		return;

	encrypt = (conn->link_mode & HCI_LM_ENCRYPT) ? 0x01 : 0x00;

	read_lock(&hci_cb_list_lock);
	list_for_each_entry(cb, &hci_cb_list, list) {
		if (cb->security_cfm)
			cb->security_cfm(conn, status, encrypt);
	}
	read_unlock(&hci_cb_list_lock);
}

static inline void hci_encrypt_cfm(struct hci_conn *conn, __u8 status,
								__u8 encrypt)
{
	struct hci_cb *cb;

	if (conn->sec_level == BT_SECURITY_SDP)
		conn->sec_level = BT_SECURITY_LOW;

	if (conn->pending_sec_level > conn->sec_level)
		conn->sec_level = conn->pending_sec_level;

	hci_proto_encrypt_cfm(conn, status, encrypt);

	read_lock(&hci_cb_list_lock);
	list_for_each_entry(cb, &hci_cb_list, list) {
		if (cb->security_cfm)
			cb->security_cfm(conn, status, encrypt);
	}
	read_unlock(&hci_cb_list_lock);
}

static inline void hci_key_change_cfm(struct hci_conn *conn, __u8 status)
{
	struct hci_cb *cb;

	read_lock(&hci_cb_list_lock);
	list_for_each_entry(cb, &hci_cb_list, list) {
		if (cb->key_change_cfm)
			cb->key_change_cfm(conn, status);
	}
	read_unlock(&hci_cb_list_lock);
}

static inline void hci_role_switch_cfm(struct hci_conn *conn, __u8 status,
								__u8 role)
{
	struct hci_cb *cb;

	read_lock(&hci_cb_list_lock);
	list_for_each_entry(cb, &hci_cb_list, list) {
		if (cb->role_switch_cfm)
			cb->role_switch_cfm(conn, status, role);
	}
	read_unlock(&hci_cb_list_lock);
}

static inline bool eir_has_data_type(u8 *data, size_t data_len, u8 type)
{
	size_t parsed = 0;

	if (data_len < 2)
		return false;

	while (parsed < data_len - 1) {
		u8 field_len = data[0];

		if (field_len == 0)
			break;

		parsed += field_len + 1;

		if (parsed > data_len)
			break;

		if (data[1] == type)
			return true;

		data += field_len + 1;
	}

	return false;
}

static inline size_t eir_get_length(u8 *eir, size_t eir_len)
{
	size_t parsed = 0;

	while (parsed < eir_len) {
		u8 field_len = eir[0];

		if (field_len == 0)
			return parsed;

		parsed += field_len + 1;
		eir += field_len + 1;
	}

	return eir_len;
}

static inline u16 eir_append_data(u8 *eir, u16 eir_len, u8 type, u8 *data,
				  u8 data_len)
{
	eir[eir_len++] = sizeof(type) + data_len;
	eir[eir_len++] = type;
	memcpy(&eir[eir_len], data, data_len);
	eir_len += data_len;

	return eir_len;
}

int hci_register_cb(struct hci_cb *hcb);
int hci_unregister_cb(struct hci_cb *hcb);

int hci_send_cmd(struct hci_dev *hdev, __u16 opcode, __u32 plen, void *param);
void hci_send_acl(struct hci_chan *chan, struct sk_buff *skb, __u16 flags);
void hci_send_sco(struct hci_conn *conn, struct sk_buff *skb);

void *hci_sent_cmd_data(struct hci_dev *hdev, __u16 opcode);

/* ----- HCI Sockets ----- */
void hci_send_to_sock(struct hci_dev *hdev, struct sk_buff *skb);
void hci_send_to_control(struct sk_buff *skb, struct sock *skip_sk);
void hci_send_to_monitor(struct hci_dev *hdev, struct sk_buff *skb);

void hci_sock_dev_event(struct hci_dev *hdev, int event);

/* Management interface */
#define DISCOV_TYPE_BREDR		(BIT(BDADDR_BREDR))
#define DISCOV_TYPE_LE			(BIT(BDADDR_LE_PUBLIC) | \
					 BIT(BDADDR_LE_RANDOM))
#define DISCOV_TYPE_INTERLEAVED		(BIT(BDADDR_BREDR) | \
					 BIT(BDADDR_LE_PUBLIC) | \
					 BIT(BDADDR_LE_RANDOM))

int mgmt_control(struct sock *sk, struct msghdr *msg, size_t len);
int mgmt_index_added(struct hci_dev *hdev);
int mgmt_index_removed(struct hci_dev *hdev);
int mgmt_powered(struct hci_dev *hdev, u8 powered);
int mgmt_discoverable(struct hci_dev *hdev, u8 discoverable);
int mgmt_connectable(struct hci_dev *hdev, u8 connectable);
int mgmt_write_scan_failed(struct hci_dev *hdev, u8 scan, u8 status);
int mgmt_new_link_key(struct hci_dev *hdev, struct link_key *key,
		      bool persistent);
int mgmt_device_connected(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
			  u8 addr_type, u32 flags, u8 *name, u8 name_len,
			  u8 *dev_class);
int mgmt_device_disconnected(struct hci_dev *hdev, bdaddr_t *bdaddr,
			     u8 link_type, u8 addr_type, u8 reason);
int mgmt_disconnect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr,
			   u8 link_type, u8 addr_type, u8 status);
int mgmt_connect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
			u8 addr_type, u8 status);
int mgmt_pin_code_request(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 secure);
int mgmt_pin_code_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				 u8 status);
int mgmt_pin_code_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 status);
int mgmt_user_confirm_request(struct hci_dev *hdev, bdaddr_t *bdaddr,
			      u8 link_type, u8 addr_type, __le32 value,
			      u8 confirm_hint);
int mgmt_user_confirm_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 link_type, u8 addr_type, u8 status);
int mgmt_user_confirm_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
					 u8 link_type, u8 addr_type, u8 status);
int mgmt_user_passkey_request(struct hci_dev *hdev, bdaddr_t *bdaddr,
			      u8 link_type, u8 addr_type);
int mgmt_user_passkey_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 link_type, u8 addr_type, u8 status);
int mgmt_user_passkey_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
					 u8 link_type, u8 addr_type, u8 status);
int mgmt_user_passkey_notify(struct hci_dev *hdev, bdaddr_t *bdaddr,
			     u8 link_type, u8 addr_type, u32 passkey,
			     u8 entered);
int mgmt_auth_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
		     u8 addr_type, u8 status);
int mgmt_auth_enable_complete(struct hci_dev *hdev, u8 status);
int mgmt_ssp_enable_complete(struct hci_dev *hdev, u8 enable, u8 status);
int mgmt_set_class_of_dev_complete(struct hci_dev *hdev, u8 *dev_class,
				   u8 status);
int mgmt_set_local_name_complete(struct hci_dev *hdev, u8 *name, u8 status);
int mgmt_read_local_oob_data_reply_complete(struct hci_dev *hdev, u8 *hash,
					    u8 *randomizer, u8 status);
int mgmt_le_enable_complete(struct hci_dev *hdev, u8 enable, u8 status);
int mgmt_device_found(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
		      u8 addr_type, u8 *dev_class, s8 rssi, u8 cfm_name,
		      u8 ssp, u8 *eir, u16 eir_len);
int mgmt_remote_name(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
		     u8 addr_type, s8 rssi, u8 *name, u8 name_len);
int mgmt_start_discovery_failed(struct hci_dev *hdev, u8 status);
int mgmt_stop_discovery_failed(struct hci_dev *hdev, u8 status);
int mgmt_discovering(struct hci_dev *hdev, u8 discovering);
int mgmt_interleaved_discovery(struct hci_dev *hdev);
int mgmt_device_blocked(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 type);
int mgmt_device_unblocked(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 type);
bool mgmt_valid_hdev(struct hci_dev *hdev);
int mgmt_new_ltk(struct hci_dev *hdev, struct smp_ltk *key, u8 persistent);

/* HCI info for socket */
#define hci_pi(sk) ((struct hci_pinfo *) sk)

struct hci_pinfo {
	struct bt_sock    bt;
	struct hci_dev    *hdev;
	struct hci_filter filter;
	__u32             cmsg_mask;
	unsigned short   channel;
};

/* HCI security filter */
#define HCI_SFLT_MAX_OGF  5

struct hci_sec_filter {
	__u32 type_mask;
	__u32 event_mask[2];
	__u32 ocf_mask[HCI_SFLT_MAX_OGF + 1][4];
};

/* ----- HCI requests ----- */
#define HCI_REQ_DONE	  0
#define HCI_REQ_PEND	  1
#define HCI_REQ_CANCELED  2

#define hci_req_lock(d)		mutex_lock(&d->req_lock)
#define hci_req_unlock(d)	mutex_unlock(&d->req_lock)

void hci_req_complete(struct hci_dev *hdev, __u16 cmd, int result);

void hci_le_conn_update(struct hci_conn *conn, u16 min, u16 max,
					u16 latency, u16 to_multiplier);
void hci_le_start_enc(struct hci_conn *conn, __le16 ediv, __u8 rand[8],
							__u8 ltk[16]);
int hci_do_inquiry(struct hci_dev *hdev, u8 length);
int hci_cancel_inquiry(struct hci_dev *hdev);
int hci_le_scan(struct hci_dev *hdev, u8 type, u16 interval, u16 window,
		int timeout);
int hci_cancel_le_scan(struct hci_dev *hdev);

u8 bdaddr_to_le(u8 bdaddr_type);

#endif /* __HCI_CORE_H */
