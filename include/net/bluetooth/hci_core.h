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

#include <linux/interrupt.h>
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
	struct inquiry_entry	*next;
	__u32			timestamp;
	struct inquiry_data	data;
};

struct inquiry_cache {
	__u32			timestamp;
	struct inquiry_entry	*list;
};

struct hci_conn_hash {
	struct list_head list;
	unsigned int     acl_num;
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

struct key_master_id {
	__le16 ediv;
	u8 rand[8];
} __packed;

struct link_key_data {
	bdaddr_t bdaddr;
	u8 type;
	u8 val[16];
	u8 pin_len;
	u8 dlen;
	u8 data[0];
} __packed;

struct link_key {
	struct list_head list;
	bdaddr_t bdaddr;
	u8 type;
	u8 val[16];
	u8 pin_len;
	u8 dlen;
	u8 data[0];
};

struct oob_data {
	struct list_head list;
	bdaddr_t bdaddr;
	u8 hash[16];
	u8 randomizer[16];
};

struct adv_entry {
	struct list_head list;
	bdaddr_t bdaddr;
	u8 bdaddr_type;
};

#define NUM_REASSEMBLY 4
struct hci_dev {
	struct list_head list;
	struct mutex	lock;
	atomic_t	refcnt;

	char		name[8];
	unsigned long	flags;
	__u16		id;
	__u8		bus;
	__u8		dev_type;
	bdaddr_t	bdaddr;
	__u8		dev_name[HCI_MAX_NAME_LENGTH];
	__u8		eir[HCI_MAX_EIR_LENGTH];
	__u8		dev_class[3];
	__u8		major_class;
	__u8		minor_class;
	__u8		features[8];
	__u8		host_features[8];
	__u8		commands[64];
	__u8		ssp_mode;
	__u8		hci_ver;
	__u16		hci_rev;
	__u8		lmp_ver;
	__u16		manufacturer;
	__le16		lmp_subver;
	__u16		voice_setting;
	__u8		io_capability;

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

	struct inquiry_cache	inq_cache;
	struct hci_conn_hash	conn_hash;
	struct list_head	blacklist;

	struct list_head	uuids;

	struct list_head	link_keys;

	struct list_head	remote_oob_data;

	struct list_head	adv_entries;
	struct delayed_work	adv_work;

	struct hci_dev_stats	stat;

	struct sk_buff_head	driver_init;

	void			*driver_data;
	void			*core_data;

	atomic_t		promisc;

	struct dentry		*debugfs;

	struct device		*parent;
	struct device		dev;

	struct rfkill		*rfkill;

	struct module		*owner;

	unsigned long		dev_flags;

	int (*open)(struct hci_dev *hdev);
	int (*close)(struct hci_dev *hdev);
	int (*flush)(struct hci_dev *hdev);
	int (*send)(struct sk_buff *skb);
	void (*destruct)(struct hci_dev *hdev);
	void (*notify)(struct hci_dev *hdev, unsigned int evt);
	int (*ioctl)(struct hci_dev *hdev, unsigned int cmd, unsigned long arg);
};

struct hci_conn {
	struct list_head list;

	atomic_t	refcnt;

	bdaddr_t	dst;
	__u8		dst_type;
	__u16		handle;
	__u16		state;
	__u8		mode;
	__u8		type;
	__u8		out;
	__u8		attempt;
	__u8		dev_class[3];
	__u8		features[8];
	__u8		ssp_mode;
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
	__u8		power_save;
	__u16		disc_timeout;
	unsigned long	pend;

	__u8		remote_cap;
	__u8		remote_oob;
	__u8		remote_auth;

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

	struct hci_conn	*link;

	void (*connect_cfm_cb)	(struct hci_conn *conn, u8 status);
	void (*security_cfm_cb)	(struct hci_conn *conn, u8 status);
	void (*disconn_cfm_cb)	(struct hci_conn *conn, u8 reason);
};

struct hci_chan {
	struct list_head list;

	struct hci_conn *conn;
	struct sk_buff_head data_q;
	unsigned int	sent;
};

extern struct list_head hci_dev_list;
extern struct list_head hci_cb_list;
extern rwlock_t hci_dev_list_lock;
extern rwlock_t hci_cb_list_lock;

/* ----- HCI interface to upper protocols ----- */
extern int l2cap_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr);
extern int l2cap_connect_cfm(struct hci_conn *hcon, u8 status);
extern int l2cap_disconn_ind(struct hci_conn *hcon);
extern int l2cap_disconn_cfm(struct hci_conn *hcon, u8 reason);
extern int l2cap_security_cfm(struct hci_conn *hcon, u8 status, u8 encrypt);
extern int l2cap_recv_acldata(struct hci_conn *hcon, struct sk_buff *skb, u16 flags);

extern int sco_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr);
extern int sco_connect_cfm(struct hci_conn *hcon, __u8 status);
extern int sco_disconn_cfm(struct hci_conn *hcon, __u8 reason);
extern int sco_recv_scodata(struct hci_conn *hcon, struct sk_buff *skb);

/* ----- Inquiry cache ----- */
#define INQUIRY_CACHE_AGE_MAX   (HZ*30)   /* 30 seconds */
#define INQUIRY_ENTRY_AGE_MAX   (HZ*60)   /* 60 seconds */

static inline void inquiry_cache_init(struct hci_dev *hdev)
{
	struct inquiry_cache *c = &hdev->inq_cache;
	c->list = NULL;
}

static inline int inquiry_cache_empty(struct hci_dev *hdev)
{
	struct inquiry_cache *c = &hdev->inq_cache;
	return c->list == NULL;
}

static inline long inquiry_cache_age(struct hci_dev *hdev)
{
	struct inquiry_cache *c = &hdev->inq_cache;
	return jiffies - c->timestamp;
}

static inline long inquiry_entry_age(struct inquiry_entry *e)
{
	return jiffies - e->timestamp;
}

struct inquiry_entry *hci_inquiry_cache_lookup(struct hci_dev *hdev,
							bdaddr_t *bdaddr);
void hci_inquiry_cache_update(struct hci_dev *hdev, struct inquiry_data *data);

/* ----- HCI Connections ----- */
enum {
	HCI_CONN_AUTH_PEND,
	HCI_CONN_REAUTH_PEND,
	HCI_CONN_ENCRYPT_PEND,
	HCI_CONN_RSWITCH_PEND,
	HCI_CONN_MODE_CHANGE_PEND,
	HCI_CONN_SCO_SETUP_PEND,
	HCI_CONN_LE_SMP_PEND,
};

static inline void hci_conn_hash_init(struct hci_dev *hdev)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	INIT_LIST_HEAD(&h->list);
	h->acl_num = 0;
	h->sco_num = 0;
	h->le_num = 0;
}

static inline void hci_conn_hash_add(struct hci_dev *hdev, struct hci_conn *c)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	list_add_rcu(&c->list, &h->list);
	switch (c->type) {
	case ACL_LINK:
		h->acl_num++;
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

void hci_acl_connect(struct hci_conn *conn);
void hci_acl_disconn(struct hci_conn *conn, __u8 reason);
void hci_add_sco(struct hci_conn *conn, __u16 handle);
void hci_setup_sync(struct hci_conn *conn, __u16 handle);
void hci_sco_setup(struct hci_conn *conn, __u8 status);

struct hci_conn *hci_conn_add(struct hci_dev *hdev, int type, bdaddr_t *dst);
int hci_conn_del(struct hci_conn *conn);
void hci_conn_hash_flush(struct hci_dev *hdev);
void hci_conn_check_pending(struct hci_dev *hdev);

struct hci_chan *hci_chan_create(struct hci_conn *conn);
int hci_chan_del(struct hci_chan *chan);
void hci_chan_list_flush(struct hci_conn *conn);

struct hci_conn *hci_connect(struct hci_dev *hdev, int type, bdaddr_t *dst,
						__u8 sec_level, __u8 auth_type);
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
	atomic_inc(&conn->refcnt);
	cancel_delayed_work_sync(&conn->disc_work);
}

static inline void hci_conn_put(struct hci_conn *conn)
{
	if (atomic_dec_and_test(&conn->refcnt)) {
		unsigned long timeo;
		if (conn->type == ACL_LINK || conn->type == LE_LINK) {
			del_timer(&conn->idle_timer);
			if (conn->state == BT_CONNECTED) {
				timeo = msecs_to_jiffies(conn->disc_timeout);
				if (!conn->out)
					timeo *= 2;
			} else {
				timeo = msecs_to_jiffies(10);
			}
		} else {
			timeo = msecs_to_jiffies(10);
		}
		cancel_delayed_work_sync(&conn->disc_work);
		queue_delayed_work(conn->hdev->workqueue,
					&conn->disc_work, jiffies + timeo);
	}
}

/* ----- HCI Devices ----- */
static inline void __hci_dev_put(struct hci_dev *d)
{
	if (atomic_dec_and_test(&d->refcnt))
		d->destruct(d);
}

/*
 * hci_dev_put and hci_dev_hold are macros to avoid dragging all the
 * overhead of all the modular infrastructure into this header.
 */
#define hci_dev_put(d)		\
do {				\
	__hci_dev_put(d);	\
	module_put(d->owner);	\
} while (0)

static inline struct hci_dev *__hci_dev_hold(struct hci_dev *d)
{
	atomic_inc(&d->refcnt);
	return d;
}

#define hci_dev_hold(d)						\
({								\
	try_module_get(d->owner) ? __hci_dev_hold(d) : NULL;	\
})

#define hci_dev_lock(d)		mutex_lock(&d->lock)
#define hci_dev_unlock(d)	mutex_unlock(&d->lock)

struct hci_dev *hci_dev_get(int index);
struct hci_dev *hci_get_route(bdaddr_t *src, bdaddr_t *dst);

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

struct bdaddr_list *hci_blacklist_lookup(struct hci_dev *hdev, bdaddr_t *bdaddr);
int hci_blacklist_clear(struct hci_dev *hdev);
int hci_blacklist_add(struct hci_dev *hdev, bdaddr_t *bdaddr);
int hci_blacklist_del(struct hci_dev *hdev, bdaddr_t *bdaddr);

int hci_uuids_clear(struct hci_dev *hdev);

int hci_link_keys_clear(struct hci_dev *hdev);
struct link_key *hci_find_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr);
int hci_add_link_key(struct hci_dev *hdev, struct hci_conn *conn, int new_key,
			bdaddr_t *bdaddr, u8 *val, u8 type, u8 pin_len);
struct link_key *hci_find_ltk(struct hci_dev *hdev, __le16 ediv, u8 rand[8]);
struct link_key *hci_find_link_key_type(struct hci_dev *hdev,
					bdaddr_t *bdaddr, u8 type);
int hci_add_ltk(struct hci_dev *hdev, int new_key, bdaddr_t *bdaddr,
			u8 key_size, __le16 ediv, u8 rand[8], u8 ltk[16]);
int hci_remove_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr);

int hci_remote_oob_data_clear(struct hci_dev *hdev);
struct oob_data *hci_find_remote_oob_data(struct hci_dev *hdev,
							bdaddr_t *bdaddr);
int hci_add_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 *hash,
								u8 *randomizer);
int hci_remove_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr);

#define ADV_CLEAR_TIMEOUT (3*60*HZ) /* Three minutes */
int hci_adv_entries_clear(struct hci_dev *hdev);
struct adv_entry *hci_find_adv_entry(struct hci_dev *hdev, bdaddr_t *bdaddr);
int hci_add_adv_entry(struct hci_dev *hdev,
					struct hci_ev_le_advertising_info *ev);

void hci_del_off_timer(struct hci_dev *hdev);

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

#define SET_HCIDEV_DEV(hdev, pdev) ((hdev)->parent = (pdev))

/* ----- LMP capabilities ----- */
#define lmp_rswitch_capable(dev)   ((dev)->features[0] & LMP_RSWITCH)
#define lmp_encrypt_capable(dev)   ((dev)->features[0] & LMP_ENCRYPT)
#define lmp_sniff_capable(dev)     ((dev)->features[0] & LMP_SNIFF)
#define lmp_sniffsubr_capable(dev) ((dev)->features[5] & LMP_SNIFF_SUBR)
#define lmp_esco_capable(dev)      ((dev)->features[3] & LMP_ESCO)
#define lmp_ssp_capable(dev)       ((dev)->features[6] & LMP_SIMPLE_PAIR)
#define lmp_no_flush_capable(dev)  ((dev)->features[6] & LMP_NO_FLUSH)
#define lmp_le_capable(dev)        ((dev)->features[4] & LMP_LE)

/* ----- Extended LMP capabilities ----- */
#define lmp_host_le_capable(dev)   ((dev)->host_features[0] & LMP_HOST_LE)

/* ----- HCI protocols ----- */
static inline int hci_proto_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr,
								__u8 type)
{
	switch (type) {
	case ACL_LINK:
		return l2cap_connect_ind(hdev, bdaddr);

	case SCO_LINK:
	case ESCO_LINK:
		return sco_connect_ind(hdev, bdaddr);

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

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend))
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
	struct list_head *p;
	__u8 encrypt;

	hci_proto_auth_cfm(conn, status);

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend))
		return;

	encrypt = (conn->link_mode & HCI_LM_ENCRYPT) ? 0x01 : 0x00;

	read_lock(&hci_cb_list_lock);
	list_for_each(p, &hci_cb_list) {
		struct hci_cb *cb = list_entry(p, struct hci_cb, list);
		if (cb->security_cfm)
			cb->security_cfm(conn, status, encrypt);
	}
	read_unlock(&hci_cb_list_lock);
}

static inline void hci_encrypt_cfm(struct hci_conn *conn, __u8 status,
								__u8 encrypt)
{
	struct list_head *p;

	if (conn->sec_level == BT_SECURITY_SDP)
		conn->sec_level = BT_SECURITY_LOW;

	if (conn->pending_sec_level > conn->sec_level)
		conn->sec_level = conn->pending_sec_level;

	hci_proto_encrypt_cfm(conn, status, encrypt);

	read_lock(&hci_cb_list_lock);
	list_for_each(p, &hci_cb_list) {
		struct hci_cb *cb = list_entry(p, struct hci_cb, list);
		if (cb->security_cfm)
			cb->security_cfm(conn, status, encrypt);
	}
	read_unlock(&hci_cb_list_lock);
}

static inline void hci_key_change_cfm(struct hci_conn *conn, __u8 status)
{
	struct list_head *p;

	read_lock(&hci_cb_list_lock);
	list_for_each(p, &hci_cb_list) {
		struct hci_cb *cb = list_entry(p, struct hci_cb, list);
		if (cb->key_change_cfm)
			cb->key_change_cfm(conn, status);
	}
	read_unlock(&hci_cb_list_lock);
}

static inline void hci_role_switch_cfm(struct hci_conn *conn, __u8 status,
								__u8 role)
{
	struct list_head *p;

	read_lock(&hci_cb_list_lock);
	list_for_each(p, &hci_cb_list) {
		struct hci_cb *cb = list_entry(p, struct hci_cb, list);
		if (cb->role_switch_cfm)
			cb->role_switch_cfm(conn, status, role);
	}
	read_unlock(&hci_cb_list_lock);
}

int hci_register_cb(struct hci_cb *hcb);
int hci_unregister_cb(struct hci_cb *hcb);

int hci_register_notifier(struct notifier_block *nb);
int hci_unregister_notifier(struct notifier_block *nb);

int hci_send_cmd(struct hci_dev *hdev, __u16 opcode, __u32 plen, void *param);
void hci_send_acl(struct hci_chan *chan, struct sk_buff *skb, __u16 flags);
void hci_send_sco(struct hci_conn *conn, struct sk_buff *skb);

void *hci_sent_cmd_data(struct hci_dev *hdev, __u16 opcode);

void hci_si_event(struct hci_dev *hdev, int type, int dlen, void *data);

/* ----- HCI Sockets ----- */
void hci_send_to_sock(struct hci_dev *hdev, struct sk_buff *skb,
							struct sock *skip_sk);

/* Management interface */
int mgmt_control(struct sock *sk, struct msghdr *msg, size_t len);
int mgmt_index_added(struct hci_dev *hdev);
int mgmt_index_removed(struct hci_dev *hdev);
int mgmt_powered(struct hci_dev *hdev, u8 powered);
int mgmt_discoverable(struct hci_dev *hdev, u8 discoverable);
int mgmt_connectable(struct hci_dev *hdev, u8 connectable);
int mgmt_write_scan_failed(struct hci_dev *hdev, u8 scan, u8 status);
int mgmt_new_link_key(struct hci_dev *hdev, struct link_key *key,
								u8 persistent);
int mgmt_connected(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
								u8 addr_type);
int mgmt_disconnected(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
								u8 addr_type);
int mgmt_disconnect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 status);
int mgmt_connect_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
						u8 addr_type, u8 status);
int mgmt_pin_code_request(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 secure);
int mgmt_pin_code_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status);
int mgmt_pin_code_neg_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status);
int mgmt_user_confirm_request(struct hci_dev *hdev, bdaddr_t *bdaddr,
						__le32 value, u8 confirm_hint);
int mgmt_user_confirm_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status);
int mgmt_user_confirm_neg_reply_complete(struct hci_dev *hdev,
						bdaddr_t *bdaddr, u8 status);
int mgmt_user_passkey_request(struct hci_dev *hdev, bdaddr_t *bdaddr);
int mgmt_user_passkey_reply_complete(struct hci_dev *hdev, bdaddr_t *bdaddr,
								u8 status);
int mgmt_user_passkey_neg_reply_complete(struct hci_dev *hdev,
						bdaddr_t *bdaddr, u8 status);
int mgmt_auth_failed(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 status);
int mgmt_set_local_name_complete(struct hci_dev *hdev, u8 *name, u8 status);
int mgmt_read_local_oob_data_reply_complete(struct hci_dev *hdev, u8 *hash,
						u8 *randomizer, u8 status);
int mgmt_device_found(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 link_type,
				u8 addr_type, u8 *dev_class, s8 rssi, u8 *eir);
int mgmt_remote_name(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 *name);
int mgmt_start_discovery_failed(struct hci_dev *hdev, u8 status);
int mgmt_stop_discovery_failed(struct hci_dev *hdev, u8 status);
int mgmt_discovering(struct hci_dev *hdev, u8 discovering);
int mgmt_device_blocked(struct hci_dev *hdev, bdaddr_t *bdaddr);
int mgmt_device_unblocked(struct hci_dev *hdev, bdaddr_t *bdaddr);

/* HCI info for socket */
#define hci_pi(sk) ((struct hci_pinfo *) sk)

/* HCI socket flags */
#define HCI_PI_MGMT_INIT	0

struct hci_pinfo {
	struct bt_sock    bt;
	struct hci_dev    *hdev;
	struct hci_filter filter;
	__u32             cmsg_mask;
	unsigned short   channel;
	unsigned long     flags;
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
void hci_le_ltk_reply(struct hci_conn *conn, u8 ltk[16]);
void hci_le_ltk_neg_reply(struct hci_conn *conn);

int hci_do_inquiry(struct hci_dev *hdev, u8 length);
int hci_cancel_inquiry(struct hci_dev *hdev);

#endif /* __HCI_CORE_H */
