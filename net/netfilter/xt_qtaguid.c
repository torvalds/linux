/*
 * Kernel iptables module to track stats for packets based on user tags.
 *
 * (C) 2011 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * There are run-time debug flags enabled via the debug_mask module param, or
 * via the DEFAULT_DEBUG_MASK. See xt_qtaguid_internal.h.
 */
#define DEBUG

#include <linux/file.h>
#include <linux/inetdevice.h>
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_qtaguid.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <net/addrconf.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <linux/netfilter/xt_socket.h>
#include "xt_qtaguid_internal.h"
#include "xt_qtaguid_print.h"

/*
 * We only use the xt_socket funcs within a similar context to avoid unexpected
 * return values.
 */
#define XT_SOCKET_SUPPORTED_HOOKS \
	((1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_IN))


static const char *module_procdirname = "xt_qtaguid";
static struct proc_dir_entry *xt_qtaguid_procdir;

static unsigned int proc_iface_perms = S_IRUGO;
module_param_named(iface_perms, proc_iface_perms, uint, S_IRUGO | S_IWUSR);

static struct proc_dir_entry *xt_qtaguid_stats_file;
static unsigned int proc_stats_perms = S_IRUGO;
module_param_named(stats_perms, proc_stats_perms, uint, S_IRUGO | S_IWUSR);

static struct proc_dir_entry *xt_qtaguid_ctrl_file;
#ifdef CONFIG_ANDROID_PARANOID_NETWORK
static unsigned int proc_ctrl_perms = S_IRUGO | S_IWUGO;
#else
static unsigned int proc_ctrl_perms = S_IRUGO | S_IWUSR;
#endif
module_param_named(ctrl_perms, proc_ctrl_perms, uint, S_IRUGO | S_IWUSR);

#ifdef CONFIG_ANDROID_PARANOID_NETWORK
#include <linux/android_aid.h>
static gid_t proc_stats_readall_gid = AID_NET_BW_STATS;
static gid_t proc_ctrl_write_gid = AID_NET_BW_ACCT;
#else
/* 0 means, don't limit anybody */
static gid_t proc_stats_readall_gid;
static gid_t proc_ctrl_write_gid;
#endif
module_param_named(stats_readall_gid, proc_stats_readall_gid, uint,
		   S_IRUGO | S_IWUSR);
module_param_named(ctrl_write_gid, proc_ctrl_write_gid, uint,
		   S_IRUGO | S_IWUSR);

/*
 * Limit the number of active tags (via socket tags) for a given UID.
 * Multiple processes could share the UID.
 */
static int max_sock_tags = DEFAULT_MAX_SOCK_TAGS;
module_param(max_sock_tags, int, S_IRUGO | S_IWUSR);

/*
 * After the kernel has initiallized this module, it is still possible
 * to make it passive.
 * Setting passive to Y:
 *  - the iface stats handling will not act on notifications.
 *  - iptables matches will never match.
 *  - ctrl commands silently succeed.
 *  - stats are always empty.
 * This is mostly usefull when a bug is suspected.
 */
static bool module_passive;
module_param_named(passive, module_passive, bool, S_IRUGO | S_IWUSR);

/*
 * Control how qtaguid data is tracked per proc/uid.
 * Setting tag_tracking_passive to Y:
 *  - don't create proc specific structs to track tags
 *  - don't check that active tag stats exceed some limits.
 *  - don't clean up socket tags on process exits.
 * This is mostly usefull when a bug is suspected.
 */
static bool qtu_proc_handling_passive;
module_param_named(tag_tracking_passive, qtu_proc_handling_passive, bool,
		   S_IRUGO | S_IWUSR);


#define QTU_DEV_NAME "xt_qtaguid"

uint debug_mask = DEFAULT_DEBUG_MASK;
module_param(debug_mask, uint, S_IRUGO | S_IWUSR);

/*---------------------------------------------------------------------------*/
static const char *iface_stat_procdirname = "iface_stat";
static struct proc_dir_entry *iface_stat_procdir;

/*
 * Ordering of locks:
 *  outer locks:
 *    iface_stat_list_lock
 *    sock_tag_list_lock
 *  inner locks:
 *    uid_tag_data_tree_lock
 *    tag_counter_set_list_lock
 * Notice how sock_tag_list_lock is held sometimes when uid_tag_data_tree_lock
 * is acquired.
 *
 * Call tree with all lock holders as of 2011-09-06:
 *
 *   qtaguid_ctrl_parse()
 *     ctrl_cmd_delete()
 *       sock_tag_list_lock
 *       tag_counter_set_list_lock
 *       iface_stat_list_lock
 *         iface_entry->tag_stat_list_lock
 *       uid_tag_data_tree_lock
 *     ctrl_cmd_counter_set()
 *       tag_counter_set_list_lock
 *     ctrl_cmd_tag()
 *       sock_tag_list_lock
 *         get_tag_ref()
 *           uid_tag_data_tree_lock
 *       uid_tag_data_tree_lock
 *     ctrl_cmd_untag()
 *       sock_tag_list_lock
 *         uid_tag_data_tree_lock
 *
 *   qtaguid_mt()
 *     account_for_uid()
 *       if_tag_stat_update()
 *     get_sock_stat()
 *       sock_tag_list_lock
 *        iface_entry->tag_stat_list_lock
 *        tag_stat_update()
 *          get_active_counter_set()
 *            tag_counter_set_list_lock
 *
 *   iface_netdev_event_handler()
 *     iface_stat_create()
 *       iface_stat_list_lock
 *     iface_stat_update()
 *       iface_stat_list_lock
 *
 *   iface_inet6addr_event_handler()
 *     iface_stat_create_ipv6()
 *       iface_stat_list_lock
 *     iface_stat_update()
 *       iface_stat_list_lock
 *
 *   iface_inetaddr_event_handler()
 *     iface_stat_create()
 *       iface_stat_list_lock
 *     iface_stat_update()
 *       iface_stat_list_lock
 *
 *   qtaguid_ctrl_proc_read()
 *     sock_tag_list_lock
 *     sock_tag_list_lock
 *     uid_tag_data_tree_lock
 *     iface_stat_list_lock
 *
 *   qtaguid_stats_proc_read()
 *     iface_stat_list_lock
 *       iface_entry->tag_stat_list_lock
 *
 *   qtudev_open()
 *     uid_tag_data_tree_lock
 *
 *   qtud_dev_release()
 *     sock_tag_list_lock
 *       uid_tag_data_tree_lock
 */
static LIST_HEAD(iface_stat_list);
static DEFINE_SPINLOCK(iface_stat_list_lock);

static struct rb_root sock_tag_tree = RB_ROOT;
static DEFINE_SPINLOCK(sock_tag_list_lock);

static struct rb_root tag_counter_set_tree = RB_ROOT;
static DEFINE_SPINLOCK(tag_counter_set_list_lock);

static struct rb_root uid_tag_data_tree = RB_ROOT;
static DEFINE_SPINLOCK(uid_tag_data_tree_lock);

static struct rb_root proc_qtu_data_tree = RB_ROOT;
/* No proc_qtu_data_tree_lock; use uid_tag_data_tree_lock */

static struct qtaguid_event_counts qtu_events;
/*----------------------------------------------*/
static bool can_manipulate_uids(void)
{
	/* root pwnd */
	return unlikely(!current_fsuid()) || unlikely(!proc_ctrl_write_gid)
		|| in_egroup_p(proc_ctrl_write_gid);
}

static bool can_impersonate_uid(uid_t uid)
{
	return uid == current_fsuid() || can_manipulate_uids();
}

static bool can_read_other_uid_stats(uid_t uid)
{
	/* root pwnd */
	return unlikely(!current_fsuid()) || uid == current_fsuid()
		|| unlikely(!proc_stats_readall_gid)
		|| in_egroup_p(proc_stats_readall_gid);
}

static inline void dc_add_byte_packets(struct data_counters *counters, int set,
				  enum ifs_tx_rx direction,
				  enum ifs_proto ifs_proto,
				  int bytes,
				  int packets)
{
	counters->bpc[set][direction][ifs_proto].bytes += bytes;
	counters->bpc[set][direction][ifs_proto].packets += packets;
}

static inline uint64_t dc_sum_bytes(struct data_counters *counters,
				    int set,
				    enum ifs_tx_rx direction)
{
	return counters->bpc[set][direction][IFS_TCP].bytes
		+ counters->bpc[set][direction][IFS_UDP].bytes
		+ counters->bpc[set][direction][IFS_PROTO_OTHER].bytes;
}

static inline uint64_t dc_sum_packets(struct data_counters *counters,
				      int set,
				      enum ifs_tx_rx direction)
{
	return counters->bpc[set][direction][IFS_TCP].packets
		+ counters->bpc[set][direction][IFS_UDP].packets
		+ counters->bpc[set][direction][IFS_PROTO_OTHER].packets;
}

static struct tag_node *tag_node_tree_search(struct rb_root *root, tag_t tag)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct tag_node *data = rb_entry(node, struct tag_node, node);
		int result;
		RB_DEBUG("qtaguid: tag_node_tree_search(0x%llx): "
			 " node=%p data=%p\n", tag, node, data);
		result = tag_compare(tag, data->tag);
		RB_DEBUG("qtaguid: tag_node_tree_search(0x%llx): "
			 " data.tag=0x%llx (uid=%u) res=%d\n",
			 tag, data->tag, get_uid_from_tag(data->tag), result);
		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static void tag_node_tree_insert(struct tag_node *data, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct tag_node *this = rb_entry(*new, struct tag_node,
						 node);
		int result = tag_compare(data->tag, this->tag);
		RB_DEBUG("qtaguid: %s(): tag=0x%llx"
			 " (uid=%u)\n", __func__,
			 this->tag,
			 get_uid_from_tag(this->tag));
		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			BUG();
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void tag_stat_tree_insert(struct tag_stat *data, struct rb_root *root)
{
	tag_node_tree_insert(&data->tn, root);
}

static struct tag_stat *tag_stat_tree_search(struct rb_root *root, tag_t tag)
{
	struct tag_node *node = tag_node_tree_search(root, tag);
	if (!node)
		return NULL;
	return rb_entry(&node->node, struct tag_stat, tn.node);
}

static void tag_counter_set_tree_insert(struct tag_counter_set *data,
					struct rb_root *root)
{
	tag_node_tree_insert(&data->tn, root);
}

static struct tag_counter_set *tag_counter_set_tree_search(struct rb_root *root,
							   tag_t tag)
{
	struct tag_node *node = tag_node_tree_search(root, tag);
	if (!node)
		return NULL;
	return rb_entry(&node->node, struct tag_counter_set, tn.node);

}

static void tag_ref_tree_insert(struct tag_ref *data, struct rb_root *root)
{
	tag_node_tree_insert(&data->tn, root);
}

static struct tag_ref *tag_ref_tree_search(struct rb_root *root, tag_t tag)
{
	struct tag_node *node = tag_node_tree_search(root, tag);
	if (!node)
		return NULL;
	return rb_entry(&node->node, struct tag_ref, tn.node);
}

static struct sock_tag *sock_tag_tree_search(struct rb_root *root,
					     const struct sock *sk)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct sock_tag *data = rb_entry(node, struct sock_tag,
						 sock_node);
		if (sk < data->sk)
			node = node->rb_left;
		else if (sk > data->sk)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static void sock_tag_tree_insert(struct sock_tag *data, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct sock_tag *this = rb_entry(*new, struct sock_tag,
						 sock_node);
		parent = *new;
		if (data->sk < this->sk)
			new = &((*new)->rb_left);
		else if (data->sk > this->sk)
			new = &((*new)->rb_right);
		else
			BUG();
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->sock_node, parent, new);
	rb_insert_color(&data->sock_node, root);
}

static void sock_tag_tree_erase(struct rb_root *st_to_free_tree)
{
	struct rb_node *node;
	struct sock_tag *st_entry;

	node = rb_first(st_to_free_tree);
	while (node) {
		st_entry = rb_entry(node, struct sock_tag, sock_node);
		node = rb_next(node);
		CT_DEBUG("qtaguid: %s(): "
			 "erase st: sk=%p tag=0x%llx (uid=%u)\n", __func__,
			 st_entry->sk,
			 st_entry->tag,
			 get_uid_from_tag(st_entry->tag));
		rb_erase(&st_entry->sock_node, st_to_free_tree);
		sockfd_put(st_entry->socket);
		kfree(st_entry);
	}
}

static struct proc_qtu_data *proc_qtu_data_tree_search(struct rb_root *root,
						       const pid_t pid)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct proc_qtu_data *data = rb_entry(node,
						      struct proc_qtu_data,
						      node);
		if (pid < data->pid)
			node = node->rb_left;
		else if (pid > data->pid)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static void proc_qtu_data_tree_insert(struct proc_qtu_data *data,
				      struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct proc_qtu_data *this = rb_entry(*new,
						      struct proc_qtu_data,
						      node);
		parent = *new;
		if (data->pid < this->pid)
			new = &((*new)->rb_left);
		else if (data->pid > this->pid)
			new = &((*new)->rb_right);
		else
			BUG();
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void uid_tag_data_tree_insert(struct uid_tag_data *data,
				     struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct uid_tag_data *this = rb_entry(*new,
						     struct uid_tag_data,
						     node);
		parent = *new;
		if (data->uid < this->uid)
			new = &((*new)->rb_left);
		else if (data->uid > this->uid)
			new = &((*new)->rb_right);
		else
			BUG();
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static struct uid_tag_data *uid_tag_data_tree_search(struct rb_root *root,
						     uid_t uid)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct uid_tag_data *data = rb_entry(node,
						     struct uid_tag_data,
						     node);
		if (uid < data->uid)
			node = node->rb_left;
		else if (uid > data->uid)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

/*
 * Allocates a new uid_tag_data struct if needed.
 * Returns a pointer to the found or allocated uid_tag_data.
 * Returns a PTR_ERR on failures, and lock is not held.
 * If found is not NULL:
 *   sets *found to true if not allocated.
 *   sets *found to false if allocated.
 */
struct uid_tag_data *get_uid_data(uid_t uid, bool *found_res)
{
	struct uid_tag_data *utd_entry;

	/* Look for top level uid_tag_data for the UID */
	utd_entry = uid_tag_data_tree_search(&uid_tag_data_tree, uid);
	DR_DEBUG("qtaguid: get_uid_data(%u) utd=%p\n", uid, utd_entry);

	if (found_res)
		*found_res = utd_entry;
	if (utd_entry)
		return utd_entry;

	utd_entry = kzalloc(sizeof(*utd_entry), GFP_ATOMIC);
	if (!utd_entry) {
		pr_err("qtaguid: get_uid_data(%u): "
		       "tag data alloc failed\n", uid);
		return ERR_PTR(-ENOMEM);
	}

	utd_entry->uid = uid;
	utd_entry->tag_ref_tree = RB_ROOT;
	uid_tag_data_tree_insert(utd_entry, &uid_tag_data_tree);
	DR_DEBUG("qtaguid: get_uid_data(%u) new utd=%p\n", uid, utd_entry);
	return utd_entry;
}

/* Never returns NULL. Either PTR_ERR or a valid ptr. */
static struct tag_ref *new_tag_ref(tag_t new_tag,
				   struct uid_tag_data *utd_entry)
{
	struct tag_ref *tr_entry;
	int res;

	if (utd_entry->num_active_tags + 1 > max_sock_tags) {
		pr_info("qtaguid: new_tag_ref(0x%llx): "
			"tag ref alloc quota exceeded. max=%d\n",
			new_tag, max_sock_tags);
		res = -EMFILE;
		goto err_res;

	}

	tr_entry = kzalloc(sizeof(*tr_entry), GFP_ATOMIC);
	if (!tr_entry) {
		pr_err("qtaguid: new_tag_ref(0x%llx): "
		       "tag ref alloc failed\n",
		       new_tag);
		res = -ENOMEM;
		goto err_res;
	}
	tr_entry->tn.tag = new_tag;
	/* tr_entry->num_sock_tags  handled by caller */
	utd_entry->num_active_tags++;
	tag_ref_tree_insert(tr_entry, &utd_entry->tag_ref_tree);
	DR_DEBUG("qtaguid: new_tag_ref(0x%llx): "
		 " inserted new tag ref\n",
		 new_tag);
	return tr_entry;

err_res:
	return ERR_PTR(res);
}

static struct tag_ref *lookup_tag_ref(tag_t full_tag,
				      struct uid_tag_data **utd_res)
{
	struct uid_tag_data *utd_entry;
	struct tag_ref *tr_entry;
	bool found_utd;
	uid_t uid = get_uid_from_tag(full_tag);

	DR_DEBUG("qtaguid: lookup_tag_ref(tag=0x%llx (uid=%u))\n",
		 full_tag, uid);

	utd_entry = get_uid_data(uid, &found_utd);
	if (IS_ERR_OR_NULL(utd_entry)) {
		if (utd_res)
			*utd_res = utd_entry;
		return NULL;
	}

	tr_entry = tag_ref_tree_search(&utd_entry->tag_ref_tree, full_tag);
	if (utd_res)
		*utd_res = utd_entry;
	DR_DEBUG("qtaguid: lookup_tag_ref(0x%llx) utd_entry=%p tr_entry=%p\n",
		 full_tag, utd_entry, tr_entry);
	return tr_entry;
}

/* Never returns NULL. Either PTR_ERR or a valid ptr. */
static struct tag_ref *get_tag_ref(tag_t full_tag,
				   struct uid_tag_data **utd_res)
{
	struct uid_tag_data *utd_entry;
	struct tag_ref *tr_entry;

	DR_DEBUG("qtaguid: get_tag_ref(0x%llx)\n",
		 full_tag);
	spin_lock_bh(&uid_tag_data_tree_lock);
	tr_entry = lookup_tag_ref(full_tag, &utd_entry);
	BUG_ON(IS_ERR_OR_NULL(utd_entry));
	if (!tr_entry)
		tr_entry = new_tag_ref(full_tag, utd_entry);

	spin_unlock_bh(&uid_tag_data_tree_lock);
	if (utd_res)
		*utd_res = utd_entry;
	DR_DEBUG("qtaguid: get_tag_ref(0x%llx) utd=%p tr=%p\n",
		 full_tag, utd_entry, tr_entry);
	return tr_entry;
}

/* Checks and maybe frees the UID Tag Data entry */
static void put_utd_entry(struct uid_tag_data *utd_entry)
{
	/* Are we done with the UID tag data entry? */
	if (RB_EMPTY_ROOT(&utd_entry->tag_ref_tree)) {
		DR_DEBUG("qtaguid: %s(): "
			 "erase utd_entry=%p uid=%u "
			 "by pid=%u tgid=%u uid=%u\n", __func__,
			 utd_entry, utd_entry->uid,
			 current->pid, current->tgid, current_fsuid());
		BUG_ON(utd_entry->num_active_tags);
		rb_erase(&utd_entry->node, &uid_tag_data_tree);
		kfree(utd_entry);
	} else {
		DR_DEBUG("qtaguid: %s(): "
			 "utd_entry=%p still has %d tags\n", __func__,
			 utd_entry, utd_entry->num_active_tags);
		BUG_ON(!utd_entry->num_active_tags);
	}
}

/*
 * If no sock_tags are using this tag_ref,
 * decrements refcount of utd_entry, removes tr_entry
 * from utd_entry->tag_ref_tree and frees.
 */
static void free_tag_ref_from_utd_entry(struct tag_ref *tr_entry,
					struct uid_tag_data *utd_entry)
{
	DR_DEBUG("qtaguid: %s(): %p tag=0x%llx (uid=%u)\n", __func__,
		 tr_entry, tr_entry->tn.tag,
		 get_uid_from_tag(tr_entry->tn.tag));
	if (!tr_entry->num_sock_tags) {
		BUG_ON(!utd_entry->num_active_tags);
		utd_entry->num_active_tags--;
		rb_erase(&tr_entry->tn.node, &utd_entry->tag_ref_tree);
		DR_DEBUG("qtaguid: %s(): erased %p\n", __func__, tr_entry);
		kfree(tr_entry);
	}
}

static void put_tag_ref_tree(tag_t full_tag, struct uid_tag_data *utd_entry)
{
	struct rb_node *node;
	struct tag_ref *tr_entry;
	tag_t acct_tag;

	DR_DEBUG("qtaguid: %s(tag=0x%llx (uid=%u))\n", __func__,
		 full_tag, get_uid_from_tag(full_tag));
	acct_tag = get_atag_from_tag(full_tag);
	node = rb_first(&utd_entry->tag_ref_tree);
	while (node) {
		tr_entry = rb_entry(node, struct tag_ref, tn.node);
		node = rb_next(node);
		if (!acct_tag || tr_entry->tn.tag == full_tag)
			free_tag_ref_from_utd_entry(tr_entry, utd_entry);
	}
}

static int read_proc_u64(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len;
	uint64_t value;
	char *p = page;
	uint64_t *iface_entry = data;

	if (!data)
		return 0;

	value = *iface_entry;
	p += sprintf(p, "%llu\n", value);
	len = (p - page) - off;
	*eof = (len <= count) ? 1 : 0;
	*start = page + off;
	return len;
}

static int read_proc_bool(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len;
	bool value;
	char *p = page;
	bool *bool_entry = data;

	if (!data)
		return 0;

	value = *bool_entry;
	p += sprintf(p, "%u\n", value);
	len = (p - page) - off;
	*eof = (len <= count) ? 1 : 0;
	*start = page + off;
	return len;
}

static int get_active_counter_set(tag_t tag)
{
	int active_set = 0;
	struct tag_counter_set *tcs;

	MT_DEBUG("qtaguid: get_active_counter_set(tag=0x%llx)"
		 " (uid=%u)\n",
		 tag, get_uid_from_tag(tag));
	/* For now we only handle UID tags for active sets */
	tag = get_utag_from_tag(tag);
	spin_lock_bh(&tag_counter_set_list_lock);
	tcs = tag_counter_set_tree_search(&tag_counter_set_tree, tag);
	if (tcs)
		active_set = tcs->active_set;
	spin_unlock_bh(&tag_counter_set_list_lock);
	return active_set;
}

/*
 * Find the entry for tracking the specified interface.
 * Caller must hold iface_stat_list_lock
 */
static struct iface_stat *get_iface_entry(const char *ifname)
{
	struct iface_stat *iface_entry;

	/* Find the entry for tracking the specified tag within the interface */
	if (ifname == NULL) {
		pr_info("qtaguid: iface_stat: get() NULL device name\n");
		return NULL;
	}

	/* Iterate over interfaces */
	list_for_each_entry(iface_entry, &iface_stat_list, list) {
		if (!strcmp(ifname, iface_entry->ifname))
			goto done;
	}
	iface_entry = NULL;
done:
	return iface_entry;
}

static void iface_create_proc_worker(struct work_struct *work)
{
	struct proc_dir_entry *proc_entry;
	struct iface_stat_work *isw = container_of(work, struct iface_stat_work,
						   iface_work);
	struct iface_stat *new_iface  = isw->iface_entry;

	/* iface_entries are not deleted, so safe to manipulate. */
	proc_entry = proc_mkdir(new_iface->ifname, iface_stat_procdir);
	if (IS_ERR_OR_NULL(proc_entry)) {
		pr_err("qtaguid: iface_stat: create_proc(): alloc failed.\n");
		kfree(isw);
		return;
	}

	new_iface->proc_ptr = proc_entry;

	create_proc_read_entry("tx_bytes", proc_iface_perms, proc_entry,
			read_proc_u64, &new_iface->tx_bytes);
	create_proc_read_entry("rx_bytes", proc_iface_perms, proc_entry,
			read_proc_u64, &new_iface->rx_bytes);
	create_proc_read_entry("tx_packets", proc_iface_perms, proc_entry,
			read_proc_u64, &new_iface->tx_packets);
	create_proc_read_entry("rx_packets", proc_iface_perms, proc_entry,
			read_proc_u64, &new_iface->rx_packets);
	create_proc_read_entry("active", proc_iface_perms, proc_entry,
			read_proc_bool, &new_iface->active);

	IF_DEBUG("qtaguid: iface_stat: create_proc(): done "
		 "entry=%p dev=%s\n", new_iface, new_iface->ifname);
	kfree(isw);
}

/* Caller must hold iface_stat_list_lock */
static struct iface_stat *iface_alloc(const char *ifname)
{
	struct iface_stat *new_iface;
	struct iface_stat_work *isw;

	new_iface = kzalloc(sizeof(*new_iface), GFP_ATOMIC);
	if (new_iface == NULL) {
		pr_err("qtaguid: iface_stat: create(%s): "
		       "iface_stat alloc failed\n", ifname);
		return NULL;
	}
	new_iface->ifname = kstrdup(ifname, GFP_ATOMIC);
	if (new_iface->ifname == NULL) {
		pr_err("qtaguid: iface_stat: create(%s): "
		       "ifname alloc failed\n", ifname);
		kfree(new_iface);
		return NULL;
	}
	spin_lock_init(&new_iface->tag_stat_list_lock);
	new_iface->active = true;
	new_iface->tag_stat_tree = RB_ROOT;

	/*
	 * ipv6 notifier chains are atomic :(
	 * No create_proc_read_entry() for you!
	 */
	isw = kmalloc(sizeof(*isw), GFP_ATOMIC);
	if (!isw) {
		pr_err("qtaguid: iface_stat: create(%s): "
		       "work alloc failed\n", new_iface->ifname);
		kfree(new_iface->ifname);
		kfree(new_iface);
		return NULL;
	}
	isw->iface_entry = new_iface;
	INIT_WORK(&isw->iface_work, iface_create_proc_worker);
	schedule_work(&isw->iface_work);
	list_add(&new_iface->list, &iface_stat_list);
	return new_iface;
}

/*
 * Create a new entry for tracking the specified interface.
 * Do nothing if the entry already exists.
 * Called when an interface is configured with a valid IP address.
 */
void iface_stat_create(const struct net_device *net_dev,
		       struct in_ifaddr *ifa)
{
	struct in_device *in_dev = NULL;
	const char *ifname;
	struct iface_stat *entry;
	__be32 ipaddr = 0;
	struct iface_stat *new_iface;

	IF_DEBUG("qtaguid: iface_stat: create(%s): ifa=%p netdev=%p\n",
		 net_dev ? net_dev->name : "?",
		 ifa, net_dev);
	if (!net_dev) {
		pr_err("qtaguid: iface_stat: create(): no net dev\n");
		return;
	}

	ifname = net_dev->name;
	if (!ifa) {
		in_dev = in_dev_get(net_dev);
		if (!in_dev) {
			pr_err("qtaguid: iface_stat: create(%s): no inet dev\n",
			       ifname);
			return;
		}
		IF_DEBUG("qtaguid: iface_stat: create(%s): in_dev=%p\n",
			 ifname, in_dev);
		for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
			IF_DEBUG("qtaguid: iface_stat: create(%s): "
				 "ifa=%p ifa_label=%s\n",
				 ifname, ifa,
				 ifa->ifa_label ? ifa->ifa_label : "(null)");
			if (ifa->ifa_label && !strcmp(ifname, ifa->ifa_label))
				break;
		}
	}

	if (!ifa) {
		IF_DEBUG("qtaguid: iface_stat: create(%s): no matching IP\n",
			 ifname);
		goto done_put;
	}
	ipaddr = ifa->ifa_local;

	spin_lock_bh(&iface_stat_list_lock);
	entry = get_iface_entry(ifname);
	if (entry != NULL) {
		IF_DEBUG("qtaguid: iface_stat: create(%s): entry=%p\n",
			 ifname, entry);
		if (ipv4_is_loopback(ipaddr)) {
			entry->active = false;
			IF_DEBUG("qtaguid: iface_stat: create(%s): "
				 "disable tracking of loopback dev\n",
				 ifname);
		} else {
			entry->active = true;
			IF_DEBUG("qtaguid: iface_stat: create(%s): "
				 "enable tracking. ip=%pI4\n",
				 ifname, &ipaddr);
		}
		goto done_unlock_put;
	} else if (ipv4_is_loopback(ipaddr)) {
		IF_DEBUG("qtaguid: iface_stat: create(%s): "
			 "ignore loopback dev. ip=%pI4\n", ifname, &ipaddr);
		goto done_unlock_put;
	}

	new_iface = iface_alloc(ifname);
	IF_DEBUG("qtaguid: iface_stat: create(%s): done "
		 "entry=%p ip=%pI4\n", ifname, new_iface, &ipaddr);

done_unlock_put:
	spin_unlock_bh(&iface_stat_list_lock);
done_put:
	if (in_dev)
		in_dev_put(in_dev);
}

void iface_stat_create_ipv6(const struct net_device *net_dev,
			    struct inet6_ifaddr *ifa)
{
	struct in_device *in_dev;
	const char *ifname;
	struct iface_stat *entry;
	struct iface_stat *new_iface;
	int addr_type;

	IF_DEBUG("qtaguid: iface_stat: create6(): ifa=%p netdev=%p->name=%s\n",
		 ifa, net_dev, net_dev ? net_dev->name : "");
	if (!net_dev) {
		pr_err("qtaguid: iface_stat: create6(): no net dev!\n");
		return;
	}
	ifname = net_dev->name;

	in_dev = in_dev_get(net_dev);
	if (!in_dev) {
		pr_err("qtaguid: iface_stat: create6(%s): no inet dev\n",
		       ifname);
		return;
	}

	IF_DEBUG("qtaguid: iface_stat: create6(%s): in_dev=%p\n",
		 ifname, in_dev);

	if (!ifa) {
		IF_DEBUG("qtaguid: iface_stat: create6(%s): no matching IP\n",
			 ifname);
		goto done_put;
	}
	addr_type = ipv6_addr_type(&ifa->addr);

	spin_lock_bh(&iface_stat_list_lock);
	entry = get_iface_entry(ifname);
	if (entry != NULL) {
		IF_DEBUG("qtaguid: iface_stat: create6(%s): entry=%p\n",
			 ifname, entry);
		if (addr_type & IPV6_ADDR_LOOPBACK) {
			entry->active = false;
			IF_DEBUG("qtaguid: iface_stat: create6(%s): "
				 "disable tracking of loopback dev\n",
				 ifname);
		} else {
			entry->active = true;
			IF_DEBUG("qtaguid: iface_stat: create6(%s): "
				 "enable tracking. ip=%pI6c\n",
				 ifname, &ifa->addr);
		}
		goto done_unlock_put;
	} else if (addr_type & IPV6_ADDR_LOOPBACK) {
		IF_DEBUG("qtaguid: iface_stat: create6(%s): "
			 "ignore loopback dev. ip=%pI6c\n",
			 ifname, &ifa->addr);
		goto done_unlock_put;
	}

	new_iface = iface_alloc(ifname);
	IF_DEBUG("qtaguid: iface_stat: create6(%s): done "
		 "entry=%p ip=%pI6c\n", ifname, new_iface, &ifa->addr);

done_unlock_put:
	spin_unlock_bh(&iface_stat_list_lock);
done_put:
	in_dev_put(in_dev);
}

static struct sock_tag *get_sock_stat_nl(const struct sock *sk)
{
	MT_DEBUG("qtaguid: get_sock_stat_nl(sk=%p)\n", sk);
	return sock_tag_tree_search(&sock_tag_tree, sk);
}

static struct sock_tag *get_sock_stat(const struct sock *sk)
{
	struct sock_tag *sock_tag_entry;
	MT_DEBUG("qtaguid: get_sock_stat(sk=%p)\n", sk);
	if (!sk)
		return NULL;
	spin_lock_bh(&sock_tag_list_lock);
	sock_tag_entry = get_sock_stat_nl(sk);
	spin_unlock_bh(&sock_tag_list_lock);
	return sock_tag_entry;
}

static void
data_counters_update(struct data_counters *dc, int set,
		     enum ifs_tx_rx direction, int proto, int bytes)
{
	switch (proto) {
	case IPPROTO_TCP:
		dc_add_byte_packets(dc, set, direction, IFS_TCP, bytes, 1);
		break;
	case IPPROTO_UDP:
		dc_add_byte_packets(dc, set, direction, IFS_UDP, bytes, 1);
		break;
	case IPPROTO_IP:
	default:
		dc_add_byte_packets(dc, set, direction, IFS_PROTO_OTHER, bytes,
				    1);
		break;
	}
}

/*
 * Update stats for the specified interface. Do nothing if the entry
 * does not exist (when a device was never configured with an IP address).
 * Called when an device is being unregistered.
 */
static void iface_stat_update(struct net_device *dev)
{
	struct rtnl_link_stats64 dev_stats, *stats;
	struct iface_stat *entry;

	stats = dev_get_stats(dev, &dev_stats);
	spin_lock_bh(&iface_stat_list_lock);
	entry = get_iface_entry(dev->name);
	if (entry == NULL) {
		IF_DEBUG("qtaguid: iface_stat: update(%s): not tracked\n",
			 dev->name);
		spin_unlock_bh(&iface_stat_list_lock);
		return;
	}
	IF_DEBUG("qtaguid: iface_stat: update(%s): entry=%p\n",
		 dev->name, entry);
	if (entry->active) {
		entry->tx_bytes += stats->tx_bytes;
		entry->tx_packets += stats->tx_packets;
		entry->rx_bytes += stats->rx_bytes;
		entry->rx_packets += stats->rx_packets;
		entry->active = false;
		IF_DEBUG("qtaguid: iface_stat: update(%s): "
			 " disable tracking. rx/tx=%llu/%llu\n",
			 dev->name, stats->rx_bytes, stats->tx_bytes);
	} else {
		IF_DEBUG("qtaguid: iface_stat: update(%s): disabled\n",
			dev->name);
	}
	spin_unlock_bh(&iface_stat_list_lock);
}

static void tag_stat_update(struct tag_stat *tag_entry,
			enum ifs_tx_rx direction, int proto, int bytes)
{
	int active_set;
	active_set = get_active_counter_set(tag_entry->tn.tag);
	MT_DEBUG("qtaguid: tag_stat_update(tag=0x%llx (uid=%u) set=%d "
		 "dir=%d proto=%d bytes=%d)\n",
		 tag_entry->tn.tag, get_uid_from_tag(tag_entry->tn.tag),
		 active_set, direction, proto, bytes);
	data_counters_update(&tag_entry->counters, active_set, direction,
			     proto, bytes);
	if (tag_entry->parent_counters)
		data_counters_update(tag_entry->parent_counters, active_set,
				     direction, proto, bytes);
}

/*
 * Create a new entry for tracking the specified {acct_tag,uid_tag} within
 * the interface.
 * iface_entry->tag_stat_list_lock should be held.
 */
static struct tag_stat *create_if_tag_stat(struct iface_stat *iface_entry,
					   tag_t tag)
{
	struct tag_stat *new_tag_stat_entry = NULL;
	IF_DEBUG("qtaguid: iface_stat: %s(): ife=%p tag=0x%llx"
		 " (uid=%u)\n", __func__,
		 iface_entry, tag, get_uid_from_tag(tag));
	new_tag_stat_entry = kzalloc(sizeof(*new_tag_stat_entry), GFP_ATOMIC);
	if (!new_tag_stat_entry) {
		pr_err("qtaguid: iface_stat: tag stat alloc failed\n");
		goto done;
	}
	new_tag_stat_entry->tn.tag = tag;
	tag_stat_tree_insert(new_tag_stat_entry, &iface_entry->tag_stat_tree);
done:
	return new_tag_stat_entry;
}

static void if_tag_stat_update(const char *ifname, uid_t uid,
			       const struct sock *sk, enum ifs_tx_rx direction,
			       int proto, int bytes)
{
	struct tag_stat *tag_stat_entry;
	tag_t tag, acct_tag;
	tag_t uid_tag;
	struct data_counters *uid_tag_counters;
	struct sock_tag *sock_tag_entry;
	struct iface_stat *iface_entry;
	struct tag_stat *new_tag_stat;
	MT_DEBUG("qtaguid: if_tag_stat_update(ifname=%s "
		"uid=%u sk=%p dir=%d proto=%d bytes=%d)\n",
		 ifname, uid, sk, direction, proto, bytes);


	iface_entry = get_iface_entry(ifname);
	if (!iface_entry) {
		pr_err("qtaguid: iface_stat: stat_update() %s not found\n",
		       ifname);
		return;
	}
	/* It is ok to process data when an iface_entry is inactive */

	MT_DEBUG("qtaguid: iface_stat: stat_update() dev=%s entry=%p\n",
		 ifname, iface_entry);

	/*
	 * Look for a tagged sock.
	 * It will have an acct_uid.
	 */
	sock_tag_entry = get_sock_stat(sk);
	if (sock_tag_entry) {
		tag = sock_tag_entry->tag;
		acct_tag = get_atag_from_tag(tag);
		uid_tag = get_utag_from_tag(tag);
	} else {
		acct_tag = make_atag_from_value(0);
		tag = combine_atag_with_uid(acct_tag, uid);
		uid_tag = make_tag_from_uid(uid);
	}
	MT_DEBUG("qtaguid: iface_stat: stat_update(): "
		 " looking for tag=0x%llx (uid=%u) in ife=%p\n",
		 tag, get_uid_from_tag(tag), iface_entry);
	/* Loop over tag list under this interface for {acct_tag,uid_tag} */
	spin_lock_bh(&iface_entry->tag_stat_list_lock);

	tag_stat_entry = tag_stat_tree_search(&iface_entry->tag_stat_tree,
					      tag);
	if (tag_stat_entry) {
		/*
		 * Updating the {acct_tag, uid_tag} entry handles both stats:
		 * {0, uid_tag} will also get updated.
		 */
		tag_stat_update(tag_stat_entry, direction, proto, bytes);
		spin_unlock_bh(&iface_entry->tag_stat_list_lock);
		return;
	}

	/* Loop over tag list under this interface for {0,uid_tag} */
	tag_stat_entry = tag_stat_tree_search(&iface_entry->tag_stat_tree,
					      uid_tag);
	if (!tag_stat_entry) {
		/* Here: the base uid_tag did not exist */
		/*
		 * No parent counters. So
		 *  - No {0, uid_tag} stats and no {acc_tag, uid_tag} stats.
		 */
		new_tag_stat = create_if_tag_stat(iface_entry, uid_tag);
		uid_tag_counters = &new_tag_stat->counters;
	} else {
		uid_tag_counters = &tag_stat_entry->counters;
	}

	if (acct_tag) {
		new_tag_stat = create_if_tag_stat(iface_entry, tag);
		new_tag_stat->parent_counters = uid_tag_counters;
	}
	spin_unlock_bh(&iface_entry->tag_stat_list_lock);
	tag_stat_update(new_tag_stat, direction, proto, bytes);
}

static int iface_netdev_event_handler(struct notifier_block *nb,
				      unsigned long event, void *ptr) {
	struct net_device *dev = ptr;

	if (unlikely(module_passive))
		return NOTIFY_DONE;

	IF_DEBUG("qtaguid: iface_stat: netdev_event(): "
		 "ev=0x%lx netdev=%p->name=%s\n",
		 event, dev, dev ? dev->name : "");

	switch (event) {
	case NETDEV_UP:
		iface_stat_create(dev, NULL);
		break;
	case NETDEV_DOWN:
		iface_stat_update(dev);
		break;
	}
	return NOTIFY_DONE;
}

static int iface_inet6addr_event_handler(struct notifier_block *nb,
					 unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = ptr;
	struct net_device *dev;

	if (unlikely(module_passive))
		return NOTIFY_DONE;

	IF_DEBUG("qtaguid: iface_stat: inet6addr_event(): "
		 "ev=0x%lx ifa=%p\n",
		 event, ifa);

	switch (event) {
	case NETDEV_UP:
		BUG_ON(!ifa || !ifa->idev);
		dev = (struct net_device *)ifa->idev->dev;
		iface_stat_create_ipv6(dev, ifa);
		atomic64_inc(&qtu_events.iface_events);
		break;
	case NETDEV_DOWN:
		BUG_ON(!ifa || !ifa->idev);
		dev = (struct net_device *)ifa->idev->dev;
		iface_stat_update(dev);
		atomic64_inc(&qtu_events.iface_events);
		break;
	}
	return NOTIFY_DONE;
}

static int iface_inetaddr_event_handler(struct notifier_block *nb,
					unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	struct net_device *dev;

	if (unlikely(module_passive))
		return NOTIFY_DONE;

	IF_DEBUG("qtaguid: iface_stat: inetaddr_event(): "
		 "ev=0x%lx ifa=%p\n",
		 event, ifa);

	switch (event) {
	case NETDEV_UP:
		BUG_ON(!ifa || !ifa->ifa_dev);
		dev = ifa->ifa_dev->dev;
		iface_stat_create(dev, ifa);
		atomic64_inc(&qtu_events.iface_events);
		break;
	case NETDEV_DOWN:
		BUG_ON(!ifa || !ifa->ifa_dev);
		dev = ifa->ifa_dev->dev;
		iface_stat_update(dev);
		atomic64_inc(&qtu_events.iface_events);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block iface_netdev_notifier_blk = {
	.notifier_call = iface_netdev_event_handler,
};

static struct notifier_block iface_inetaddr_notifier_blk = {
	.notifier_call = iface_inetaddr_event_handler,
};

static struct notifier_block iface_inet6addr_notifier_blk = {
	.notifier_call = iface_inet6addr_event_handler,
};

static int __init iface_stat_init(struct proc_dir_entry *parent_procdir)
{
	int err;

	iface_stat_procdir = proc_mkdir(iface_stat_procdirname, parent_procdir);
	if (!iface_stat_procdir) {
		pr_err("qtaguid: iface_stat: init failed to create proc entry\n");
		err = -1;
		goto err;
	}
	err = register_netdevice_notifier(&iface_netdev_notifier_blk);
	if (err) {
		pr_err("qtaguid: iface_stat: init "
		       "failed to register dev event handler\n");
		goto err_zap_entry;
	}
	err = register_inetaddr_notifier(&iface_inetaddr_notifier_blk);
	if (err) {
		pr_err("qtaguid: iface_stat: init "
		       "failed to register ipv4 dev event handler\n");
		goto err_unreg_nd;
	}

	err = register_inet6addr_notifier(&iface_inet6addr_notifier_blk);
	if (err) {
		pr_err("qtaguid: iface_stat: init "
		       "failed to register ipv6 dev event handler\n");
		goto err_unreg_ip4_addr;
	}
	return 0;

err_unreg_ip4_addr:
	unregister_inetaddr_notifier(&iface_inetaddr_notifier_blk);
err_unreg_nd:
	unregister_netdevice_notifier(&iface_netdev_notifier_blk);
err_zap_entry:
	remove_proc_entry(iface_stat_procdirname, parent_procdir);
err:
	return err;
}

static struct sock *qtaguid_find_sk(const struct sk_buff *skb,
				    struct xt_action_param *par)
{
	struct sock *sk;
	unsigned int hook_mask = (1 << par->hooknum);

	MT_DEBUG("qtaguid: find_sk(skb=%p) hooknum=%d family=%d\n", skb,
		 par->hooknum, par->family);

	/*
	 * Let's not abuse the the xt_socket_get*_sk(), or else it will
	 * return garbage SKs.
	 */
	if (!(hook_mask & XT_SOCKET_SUPPORTED_HOOKS))
		return NULL;

	switch (par->family) {
	case NFPROTO_IPV6:
		sk = xt_socket_get6_sk(skb, par);
		break;
	case NFPROTO_IPV4:
		sk = xt_socket_get4_sk(skb, par);
		break;
	default:
		return NULL;
	}

	/*
	 * Seems to be issues on the file ptr for TCP_TIME_WAIT SKs.
	 * http://kerneltrap.org/mailarchive/linux-netdev/2010/10/21/6287959
	 * Not fixed in 3.0-r3 :(
	 */
	if (sk) {
		MT_DEBUG("qtaguid: %p->sk_proto=%u "
			 "->sk_state=%d\n", sk, sk->sk_protocol, sk->sk_state);
		if (sk->sk_state  == TCP_TIME_WAIT) {
			xt_socket_put_sk(sk);
			sk = NULL;
		}
	}
	return sk;
}

static void account_for_uid(const struct sk_buff *skb,
			    const struct sock *alternate_sk, uid_t uid,
			    struct xt_action_param *par)
{
	const struct net_device *el_dev;

	if (!skb->dev) {
		MT_DEBUG("qtaguid[%d]: no skb->dev\n", par->hooknum);
		el_dev = par->in ? : par->out;
	} else {
		const struct net_device *other_dev;
		el_dev = skb->dev;
		other_dev = par->in ? : par->out;
		if (el_dev != other_dev) {
			MT_DEBUG("qtaguid[%d]: skb->dev=%p %s vs "
				"par->(in/out)=%p %s\n",
				par->hooknum, el_dev, el_dev->name, other_dev,
				other_dev->name);
		}
	}

	if (unlikely(!el_dev)) {
		pr_info("qtaguid[%d]: no par->in/out?!!\n", par->hooknum);
	} else if (unlikely(!el_dev->name)) {
		pr_info("qtaguid[%d]: no dev->name?!!\n", par->hooknum);
	} else {
		MT_DEBUG("qtaguid[%d]: dev name=%s type=%d\n",
			 par->hooknum,
			 el_dev->name,
			 el_dev->type);

		if_tag_stat_update(el_dev->name, uid,
				skb->sk ? skb->sk : alternate_sk,
				par->in ? IFS_RX : IFS_TX,
				ip_hdr(skb)->protocol, skb->len);
	}
}

static bool qtaguid_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_qtaguid_match_info *info = par->matchinfo;
	const struct file *filp;
	bool got_sock = false;
	struct sock *sk;
	uid_t sock_uid;
	bool res;

	if (unlikely(module_passive))
		return (info->match ^ info->invert) == 0;

	MT_DEBUG("qtaguid[%d]: entered skb=%p par->in=%p/out=%p fam=%d\n",
		 par->hooknum, skb, par->in, par->out, par->family);

	if (skb == NULL) {
		res = (info->match ^ info->invert) == 0;
		goto ret_res;
	}

	sk = skb->sk;

	if (sk == NULL) {
		/*
		 * A missing sk->sk_socket happens when packets are in-flight
		 * and the matching socket is already closed and gone.
		 */
		sk = qtaguid_find_sk(skb, par);
		/*
		 * If we got the socket from the find_sk(), we will need to put
		 * it back, as nf_tproxy_get_sock_v4() got it.
		 */
		got_sock = sk;
		if (sk)
			atomic64_inc(&qtu_events.match_found_sk_in_ct);
	} else {
		atomic64_inc(&qtu_events.match_found_sk);
	}
	MT_DEBUG("qtaguid[%d]: sk=%p got_sock=%d proto=%d\n",
		par->hooknum, sk, got_sock, ip_hdr(skb)->protocol);
	if (sk != NULL) {
		MT_DEBUG("qtaguid[%d]: sk=%p->sk_socket=%p->file=%p\n",
			par->hooknum, sk, sk->sk_socket,
			sk->sk_socket ? sk->sk_socket->file : (void *)-1LL);
		filp = sk->sk_socket ? sk->sk_socket->file : NULL;
		MT_DEBUG("qtaguid[%d]: filp...uid=%u\n",
			par->hooknum, filp ? filp->f_cred->fsuid : -1);
	}

	if (sk == NULL || sk->sk_socket == NULL) {
		/*
		 * Here, the qtaguid_find_sk() using connection tracking
		 * couldn't find the owner, so for now we just count them
		 * against the system.
		 */
		/*
		 * TODO: unhack how to force just accounting.
		 * For now we only do iface stats when the uid-owner is not
		 * requested.
		 */
		if (!(info->match & XT_QTAGUID_UID))
			account_for_uid(skb, sk, 0, par);
		MT_DEBUG("qtaguid[%d]: leaving (sk?sk->sk_socket)=%p\n",
			par->hooknum,
			sk ? sk->sk_socket : NULL);
		res = (info->match ^ info->invert) == 0;
		atomic64_inc(&qtu_events.match_found_sk_none);
		goto put_sock_ret_res;
	} else if (info->match & info->invert & XT_QTAGUID_SOCKET) {
		res = false;
		goto put_sock_ret_res;
	}
	filp = sk->sk_socket->file;
	if (filp == NULL) {
		MT_DEBUG("qtaguid[%d]: leaving filp=NULL\n", par->hooknum);
		res = ((info->match ^ info->invert) &
			(XT_QTAGUID_UID | XT_QTAGUID_GID)) == 0;
		goto put_sock_ret_res;
	}
	sock_uid = filp->f_cred->fsuid;
	/*
	 * TODO: unhack how to force just accounting.
	 * For now we only do iface stats when the uid-owner is not requested
	 */
	if (!(info->match & XT_QTAGUID_UID))
		account_for_uid(skb, sk, sock_uid, par);

	/*
	 * The following two tests fail the match when:
	 *    id not in range AND no inverted condition requested
	 * or id     in range AND    inverted condition requested
	 * Thus (!a && b) || (a && !b) == a ^ b
	 */
	if (info->match & XT_QTAGUID_UID)
		if ((filp->f_cred->fsuid >= info->uid_min &&
		     filp->f_cred->fsuid <= info->uid_max) ^
		    !(info->invert & XT_QTAGUID_UID)) {
			MT_DEBUG("qtaguid[%d]: leaving uid not matching\n",
				 par->hooknum);
			res = false;
			goto put_sock_ret_res;
		}
	if (info->match & XT_QTAGUID_GID)
		if ((filp->f_cred->fsgid >= info->gid_min &&
				filp->f_cred->fsgid <= info->gid_max) ^
			!(info->invert & XT_QTAGUID_GID)) {
			MT_DEBUG("qtaguid[%d]: leaving gid not matching\n",
				par->hooknum);
			res = false;
			goto put_sock_ret_res;
		}

	MT_DEBUG("qtaguid[%d]: leaving matched\n", par->hooknum);
	res = true;

put_sock_ret_res:
	if (got_sock)
		xt_socket_put_sk(sk);
ret_res:
	MT_DEBUG("qtaguid[%d]: left %d\n", par->hooknum, res);
	return res;
}

/*
 * Procfs reader to get all active socket tags using style "1)" as described in
 * fs/proc/generic.c
 */
static int qtaguid_ctrl_proc_read(char *page, char **num_items_returned,
				  off_t items_to_skip, int char_count, int *eof,
				  void *data)
{
	char *outp = page;
	int len;
	uid_t uid;
	struct rb_node *node;
	struct sock_tag *sock_tag_entry;
	int item_index = 0;
	int indent_level = 0;
	long f_count;

	if (unlikely(module_passive)) {
		*eof = 1;
		return 0;
	}

	if (*eof)
		return 0;

	CT_DEBUG("qtaguid: proc ctrl page=%p off=%ld char_count=%d *eof=%d\n",
		page, items_to_skip, char_count, *eof);

	spin_lock_bh(&sock_tag_list_lock);
	for (node = rb_first(&sock_tag_tree);
	     node;
	     node = rb_next(node)) {
		if (item_index++ < items_to_skip)
			continue;
		sock_tag_entry = rb_entry(node, struct sock_tag, sock_node);
		uid = get_uid_from_tag(sock_tag_entry->tag);
		CT_DEBUG("qtaguid: proc_read(): sk=%p tag=0x%llx (uid=%u) "
			 "pid=%u\n",
			 sock_tag_entry->sk,
			 sock_tag_entry->tag,
			 uid,
			 sock_tag_entry->pid
			);
		f_count = atomic_long_read(
			&sock_tag_entry->socket->file->f_count);
		len = snprintf(outp, char_count,
			       "sock=%p tag=0x%llx (uid=%u) pid=%u "
			       "f_count=%lu\n",
			       sock_tag_entry->sk,
			       sock_tag_entry->tag, uid,
			       sock_tag_entry->pid, f_count);
		if (len >= char_count) {
			spin_unlock_bh(&sock_tag_list_lock);
			*outp = '\0';
			return outp - page;
		}
		outp += len;
		char_count -= len;
		(*num_items_returned)++;
	}
	spin_unlock_bh(&sock_tag_list_lock);

	if (item_index++ >= items_to_skip) {
		len = snprintf(outp, char_count,
			       "events: sockets_tagged=%llu "
			       "sockets_untagged=%llu "
			       "counter_set_changes=%llu "
			       "delete_cmds=%llu "
			       "iface_events=%llu "
			       "match_found_sk=%llu "
			       "match_found_sk_in_ct=%llu "
			       "match_found_sk_none=%llu\n",
			       atomic64_read(&qtu_events.sockets_tagged),
			       atomic64_read(&qtu_events.sockets_untagged),
			       atomic64_read(&qtu_events.counter_set_changes),
			       atomic64_read(&qtu_events.delete_cmds),
			       atomic64_read(&qtu_events.iface_events),
			       atomic64_read(&qtu_events.match_found_sk),
			       atomic64_read(&qtu_events.match_found_sk_in_ct),
			       atomic64_read(&qtu_events.match_found_sk_none));
		if (len >= char_count) {
			*outp = '\0';
			return outp - page;
		}
		outp += len;
		char_count -= len;
		(*num_items_returned)++;
	}

#ifdef CDEBUG
	/* Count the following as part of the last item_index */
	if (item_index > items_to_skip) {
		CT_DEBUG("qtaguid: proc ctrl state debug {\n");
		spin_lock_bh(&sock_tag_list_lock);
		prdebug_sock_tag_tree(indent_level, &sock_tag_tree);
		spin_unlock_bh(&sock_tag_list_lock);

		spin_lock_bh(&uid_tag_data_tree_lock);
		prdebug_uid_tag_data_tree(indent_level, &uid_tag_data_tree);
		prdebug_proc_qtu_data_tree(indent_level, &proc_qtu_data_tree);
		spin_unlock_bh(&uid_tag_data_tree_lock);

		spin_lock_bh(&iface_stat_list_lock);
		prdebug_iface_stat_list(indent_level, &iface_stat_list);
		spin_unlock_bh(&iface_stat_list_lock);

		CT_DEBUG("qtaguid: proc ctrl state debug }\n");


	}
#endif

	*eof = 1;
	return outp - page;
}

/*
 * Delete socket tags, and stat tags associated with a given
 * accouting tag and uid.
 */
static int ctrl_cmd_delete(const char *input)
{
	char cmd;
	uid_t uid;
	uid_t entry_uid;
	tag_t acct_tag;
	tag_t tag;
	int res, argc;
	struct iface_stat *iface_entry;
	struct rb_node *node;
	struct sock_tag *st_entry;
	struct rb_root st_to_free_tree = RB_ROOT;
	struct tag_stat *ts_entry;
	struct tag_counter_set *tcs_entry;
	struct tag_ref *tr_entry;
	struct uid_tag_data *utd_entry;

	argc = sscanf(input, "%c %llu %u", &cmd, &acct_tag, &uid);
	CT_DEBUG("qtaguid: ctrl_delete(%s): argc=%d cmd=%c "
		 "user_tag=0x%llx uid=%u\n", input, argc, cmd,
		 acct_tag, uid);
	if (argc < 2) {
		res = -EINVAL;
		goto err;
	}
	if (!valid_atag(acct_tag)) {
		pr_info("qtaguid: ctrl_delete(%s): invalid tag\n", input);
		res = -EINVAL;
		goto err;
	}
	if (argc < 3) {
		uid = current_fsuid();
	} else if (!can_impersonate_uid(uid)) {
		pr_info("qtaguid: ctrl_delete(%s): "
			"insufficient priv from pid=%u tgid=%u uid=%u\n",
			input, current->pid, current->tgid, current_fsuid());
		res = -EPERM;
		goto err;
	}

	tag = combine_atag_with_uid(acct_tag, uid);
	CT_DEBUG("qtaguid: ctrl_delete(): "
		 "looking for tag=0x%llx (uid=%u)\n",
		 tag, uid);

	/* Delete socket tags */
	spin_lock_bh(&sock_tag_list_lock);
	node = rb_first(&sock_tag_tree);
	while (node) {
		st_entry = rb_entry(node, struct sock_tag, sock_node);
		entry_uid = get_uid_from_tag(st_entry->tag);
		node = rb_next(node);
		if (entry_uid != uid)
			continue;

		CT_DEBUG("qtaguid: ctrl_delete(): st tag=0x%llx (uid=%u)\n",
			 st_entry->tag, entry_uid);

		if (!acct_tag || st_entry->tag == tag) {
			rb_erase(&st_entry->sock_node, &sock_tag_tree);
			/* Can't sockfd_put() within spinlock, do it later. */
			sock_tag_tree_insert(st_entry, &st_to_free_tree);
			tr_entry = lookup_tag_ref(st_entry->tag, NULL);
			BUG_ON(tr_entry->num_sock_tags <= 0);
			tr_entry->num_sock_tags--;
		}
	}
	spin_unlock_bh(&sock_tag_list_lock);

	sock_tag_tree_erase(&st_to_free_tree);

	/* Delete tag counter-sets */
	spin_lock_bh(&tag_counter_set_list_lock);
	/* Counter sets are only on the uid tag, not full tag */
	tcs_entry = tag_counter_set_tree_search(&tag_counter_set_tree, tag);
	if (tcs_entry) {
		CT_DEBUG("qtaguid: ctrl_delete(): "
			 "erase tcs: tag=0x%llx (uid=%u) set=%d\n",
			 tcs_entry->tn.tag,
			 get_uid_from_tag(tcs_entry->tn.tag),
			 tcs_entry->active_set);
		rb_erase(&tcs_entry->tn.node, &tag_counter_set_tree);
		kfree(tcs_entry);
	}
	spin_unlock_bh(&tag_counter_set_list_lock);

	/*
	 * If acct_tag is 0, then all entries belonging to uid are
	 * erased.
	 */
	spin_lock_bh(&iface_stat_list_lock);
	list_for_each_entry(iface_entry, &iface_stat_list, list) {
		spin_lock_bh(&iface_entry->tag_stat_list_lock);
		node = rb_first(&iface_entry->tag_stat_tree);
		while (node) {
			ts_entry = rb_entry(node, struct tag_stat, tn.node);
			entry_uid = get_uid_from_tag(ts_entry->tn.tag);
			node = rb_next(node);

			CT_DEBUG("qtaguid: ctrl_delete(): "
				 "ts tag=0x%llx (uid=%u)\n",
				 ts_entry->tn.tag, entry_uid);

			if (entry_uid != uid)
				continue;
			if (!acct_tag || ts_entry->tn.tag == tag) {
				CT_DEBUG("qtaguid: ctrl_delete(): "
					 "erase ts: %s 0x%llx %u\n",
					 iface_entry->ifname,
					 get_atag_from_tag(ts_entry->tn.tag),
					 entry_uid);
				rb_erase(&ts_entry->tn.node,
					 &iface_entry->tag_stat_tree);
				kfree(ts_entry);
			}
		}
		spin_unlock_bh(&iface_entry->tag_stat_list_lock);
	}
	spin_unlock_bh(&iface_stat_list_lock);

	/* Cleanup the uid_tag_data */
	spin_lock_bh(&uid_tag_data_tree_lock);
	node = rb_first(&uid_tag_data_tree);
	while (node) {
		utd_entry = rb_entry(node, struct uid_tag_data, node);
		entry_uid = utd_entry->uid;
		node = rb_next(node);

		CT_DEBUG("qtaguid: ctrl_delete(): "
			 "utd uid=%u\n",
			 entry_uid);

		if (entry_uid != uid)
			continue;
		/*
		 * Go over the tag_refs, and those that don't have
		 * sock_tags using them are freed.
		 */
		put_tag_ref_tree(tag, utd_entry);
		put_utd_entry(utd_entry);
	}
	spin_unlock_bh(&uid_tag_data_tree_lock);

	atomic64_inc(&qtu_events.delete_cmds);
	res = 0;

err:
	return res;
}

static int ctrl_cmd_counter_set(const char *input)
{
	char cmd;
	uid_t uid = 0;
	tag_t tag;
	int res, argc;
	struct tag_counter_set *tcs;
	int counter_set;

	argc = sscanf(input, "%c %d %u", &cmd, &counter_set, &uid);
	CT_DEBUG("qtaguid: ctrl_counterset(%s): argc=%d cmd=%c "
		 "set=%d uid=%u\n", input, argc, cmd,
		 counter_set, uid);
	if (argc != 3) {
		res = -EINVAL;
		goto err;
	}
	if (counter_set < 0 || counter_set >= IFS_MAX_COUNTER_SETS) {
		pr_info("qtaguid: ctrl_counterset(%s): invalid counter_set range\n",
			input);
		res = -EINVAL;
		goto err;
	}
	if (!can_manipulate_uids()) {
		pr_info("qtaguid: ctrl_counterset(%s): "
			"insufficient priv from pid=%u tgid=%u uid=%u\n",
			input, current->pid, current->tgid, current_fsuid());
		res = -EPERM;
		goto err;
	}

	tag = make_tag_from_uid(uid);
	spin_lock_bh(&tag_counter_set_list_lock);
	tcs = tag_counter_set_tree_search(&tag_counter_set_tree, tag);
	if (!tcs) {
		tcs = kzalloc(sizeof(*tcs), GFP_ATOMIC);
		if (!tcs) {
			spin_unlock_bh(&tag_counter_set_list_lock);
			pr_err("qtaguid: ctrl_counterset(%s): "
			       "failed to alloc counter set\n",
			       input);
			res = -ENOMEM;
			goto err;
		}
		tcs->tn.tag = tag;
		tag_counter_set_tree_insert(tcs, &tag_counter_set_tree);
		CT_DEBUG("qtaguid: ctrl_counterset(%s): added tcs tag=0x%llx "
			 "(uid=%u) set=%d\n",
			 input, tag, get_uid_from_tag(tag), counter_set);
	}
	tcs->active_set = counter_set;
	spin_unlock_bh(&tag_counter_set_list_lock);
	atomic64_inc(&qtu_events.counter_set_changes);
	res = 0;

err:
	return res;
}

static int ctrl_cmd_tag(const char *input)
{
	char cmd;
	int sock_fd = 0;
	uid_t uid = 0;
	tag_t acct_tag = make_atag_from_value(0);
	tag_t full_tag;
	struct socket *el_socket;
	int res, argc;
	struct sock_tag *sock_tag_entry;
	struct tag_ref *tag_ref_entry;
	struct uid_tag_data *uid_tag_data_entry;
	struct proc_qtu_data *pqd_entry;

	/* Unassigned args will get defaulted later. */
	argc = sscanf(input, "%c %d %llu %u", &cmd, &sock_fd, &acct_tag, &uid);
	CT_DEBUG("qtaguid: ctrl_tag(%s): argc=%d cmd=%c sock_fd=%d "
		 "acct_tag=0x%llx uid=%u\n", input, argc, cmd, sock_fd,
		 acct_tag, uid);
	if (argc < 2) {
		res = -EINVAL;
		goto err;
	}
	el_socket = sockfd_lookup(sock_fd, &res);  /* This locks the file */
	if (!el_socket) {
		pr_info("qtaguid: ctrl_tag(%s): failed to lookup"
			" sock_fd=%d err=%d\n", input, sock_fd, res);
		goto err;
	}
	CT_DEBUG("qtaguid: ctrl_tag(%s): socket->...->f_count=%ld ->sk=%p\n",
		 input, atomic_long_read(&el_socket->file->f_count),
		 el_socket->sk);
	if (argc < 3) {
		acct_tag = make_atag_from_value(0);
	} else if (!valid_atag(acct_tag)) {
		pr_info("qtaguid: ctrl_tag(%s): invalid tag\n", input);
		res = -EINVAL;
		goto err_put;
	}
	CT_DEBUG("qtaguid: ctrl_tag(%s): "
		 "pid=%u tgid=%u uid=%u euid=%u fsuid=%u "
		 "in_group=%d in_egroup=%d\n",
		 input, current->pid, current->tgid, current_uid(),
		 current_euid(), current_fsuid(),
		 in_group_p(proc_ctrl_write_gid),
		 in_egroup_p(proc_ctrl_write_gid));
	if (argc < 4) {
		uid = current_fsuid();
	} else if (!can_impersonate_uid(uid)) {
		pr_info("qtaguid: ctrl_tag(%s): "
			"insufficient priv from pid=%u tgid=%u uid=%u\n",
			input, current->pid, current->tgid, current_fsuid());
		res = -EPERM;
		goto err_put;
	}
	full_tag = combine_atag_with_uid(acct_tag, uid);

	spin_lock_bh(&sock_tag_list_lock);
	sock_tag_entry = get_sock_stat_nl(el_socket->sk);
	tag_ref_entry = get_tag_ref(full_tag, &uid_tag_data_entry);
	if (IS_ERR(tag_ref_entry)) {
		res = PTR_ERR(tag_ref_entry);
		spin_unlock_bh(&sock_tag_list_lock);
		goto err_put;
	}
	tag_ref_entry->num_sock_tags++;
	if (sock_tag_entry) {
		struct tag_ref *prev_tag_ref_entry;

		CT_DEBUG("qtaguid: ctrl_tag(%s): retag for sk=%p "
			 "st@%p ...->f_count=%ld\n",
			 input, el_socket->sk, sock_tag_entry,
			 atomic_long_read(&el_socket->file->f_count));
		/*
		 * This is a re-tagging, so release the sock_fd that was
		 * locked at the time of the 1st tagging.
		 * There is still the ref from this call's sockfd_lookup() so
		 * it can be done within the spinlock.
		 */
		sockfd_put(sock_tag_entry->socket);
		prev_tag_ref_entry = lookup_tag_ref(sock_tag_entry->tag,
						    &uid_tag_data_entry);
		BUG_ON(IS_ERR_OR_NULL(prev_tag_ref_entry));
		BUG_ON(prev_tag_ref_entry->num_sock_tags <= 0);
		prev_tag_ref_entry->num_sock_tags--;
		sock_tag_entry->tag = full_tag;
	} else {
		CT_DEBUG("qtaguid: ctrl_tag(%s): newtag for sk=%p\n",
			 input, el_socket->sk);
		sock_tag_entry = kzalloc(sizeof(*sock_tag_entry),
					 GFP_ATOMIC);
		if (!sock_tag_entry) {
			pr_err("qtaguid: ctrl_tag(%s): "
			       "socket tag alloc failed\n",
			       input);
			spin_unlock_bh(&sock_tag_list_lock);
			res = -ENOMEM;
			goto err_tag_unref_put;
		}
		sock_tag_entry->sk = el_socket->sk;
		sock_tag_entry->socket = el_socket;
		sock_tag_entry->pid = current->tgid;
		sock_tag_entry->tag = combine_atag_with_uid(acct_tag,
							    uid);
		spin_lock_bh(&uid_tag_data_tree_lock);
		pqd_entry = proc_qtu_data_tree_search(
			&proc_qtu_data_tree, current->tgid);
		/*
		 * TODO: remove if, and start failing.
		 * At first, we want to catch user-space code that is not
		 * opening the /dev/xt_qtaguid.
		 */
		WARN_ONCE(IS_ERR_OR_NULL(pqd_entry),
			  "qtaguid: User space forgot to open /dev/xt_qtaguid? "
			  "pid=%u tgid=%u uid=%u\n",
			  current->pid, current->tgid, current_fsuid());
		if (!IS_ERR_OR_NULL(pqd_entry)) {
			list_add(&sock_tag_entry->list,
				 &pqd_entry->sock_tag_list);
		}
		spin_unlock_bh(&uid_tag_data_tree_lock);

		sock_tag_tree_insert(sock_tag_entry, &sock_tag_tree);
		atomic64_inc(&qtu_events.sockets_tagged);
	}
	spin_unlock_bh(&sock_tag_list_lock);
	/* We keep the ref to the socket (file) until it is untagged */
	CT_DEBUG("qtaguid: ctrl_tag(%s): done st@%p ...->f_count=%ld\n",
		 input, sock_tag_entry,
		 atomic_long_read(&el_socket->file->f_count));
	return 0;

err_tag_unref_put:
	BUG_ON(tag_ref_entry->num_sock_tags <= 0);
	tag_ref_entry->num_sock_tags--;
	free_tag_ref_from_utd_entry(tag_ref_entry, uid_tag_data_entry);
err_put:
	CT_DEBUG("qtaguid: ctrl_tag(%s): done. ...->f_count=%ld\n",
		 input, atomic_long_read(&el_socket->file->f_count) - 1);
	/* Release the sock_fd that was grabbed by sockfd_lookup(). */
	sockfd_put(el_socket);
	return res;

err:
	CT_DEBUG("qtaguid: ctrl_tag(%s): done.\n", input);
	return res;
}

static int ctrl_cmd_untag(const char *input)
{
	char cmd;
	int sock_fd = 0;
	struct socket *el_socket;
	int res, argc;
	struct sock_tag *sock_tag_entry;
	struct tag_ref *tag_ref_entry;
	struct uid_tag_data *utd_entry;
	struct proc_qtu_data *pqd_entry;

	argc = sscanf(input, "%c %d", &cmd, &sock_fd);
	CT_DEBUG("qtaguid: ctrl_untag(%s): argc=%d cmd=%c sock_fd=%d\n",
		 input, argc, cmd, sock_fd);
	if (argc < 2) {
		res = -EINVAL;
		goto err;
	}
	el_socket = sockfd_lookup(sock_fd, &res);  /* This locks the file */
	if (!el_socket) {
		pr_info("qtaguid: ctrl_untag(%s): failed to lookup"
			" sock_fd=%d err=%d\n", input, sock_fd, res);
		goto err;
	}
	CT_DEBUG("qtaguid: ctrl_untag(%s): socket->...->f_count=%ld ->sk=%p\n",
		 input, atomic_long_read(&el_socket->file->f_count),
		 el_socket->sk);
	spin_lock_bh(&sock_tag_list_lock);
	sock_tag_entry = get_sock_stat_nl(el_socket->sk);
	if (!sock_tag_entry) {
		spin_unlock_bh(&sock_tag_list_lock);
		res = -EINVAL;
		goto err_put;
	}
	/*
	 * The socket already belongs to the current process
	 * so it can do whatever it wants to it.
	 */
	rb_erase(&sock_tag_entry->sock_node, &sock_tag_tree);

	tag_ref_entry = lookup_tag_ref(sock_tag_entry->tag, &utd_entry);
	BUG_ON(!tag_ref_entry);
	BUG_ON(tag_ref_entry->num_sock_tags <= 0);
	spin_lock_bh(&uid_tag_data_tree_lock);
	pqd_entry = proc_qtu_data_tree_search(
		&proc_qtu_data_tree, current->tgid);
	/*
	 * TODO: remove if, and start failing.
	 * At first, we want to catch user-space code that is not
	 * opening the /dev/xt_qtaguid.
	 */
	WARN_ONCE(IS_ERR_OR_NULL(pqd_entry),
		  "qtaguid: User space forgot to open /dev/xt_qtaguid? "
		  "pid=%u tgid=%u uid=%u\n",
		  current->pid, current->tgid, current_fsuid());
	if (!IS_ERR_OR_NULL(pqd_entry))
		list_del(&sock_tag_entry->list);
	spin_unlock_bh(&uid_tag_data_tree_lock);
	/*
	 * We don't free tag_ref from the utd_entry here,
	 * only during a cmd_delete().
	 */
	tag_ref_entry->num_sock_tags--;
	spin_unlock_bh(&sock_tag_list_lock);
	/*
	 * Release the sock_fd that was grabbed at tag time,
	 * and once more for the sockfd_lookup() here.
	 */
	sockfd_put(sock_tag_entry->socket);
	CT_DEBUG("qtaguid: ctrl_untag(%s): done. st@%p ...->f_count=%ld\n",
		 input, sock_tag_entry,
		 atomic_long_read(&el_socket->file->f_count) - 1);
	sockfd_put(el_socket);

	kfree(sock_tag_entry);
	atomic64_inc(&qtu_events.sockets_untagged);

	return 0;

err_put:
	CT_DEBUG("qtaguid: ctrl_untag(%s): done. socket->...->f_count=%ld\n",
		 input, atomic_long_read(&el_socket->file->f_count) - 1);
	/* Release the sock_fd that was grabbed by sockfd_lookup(). */
	sockfd_put(el_socket);
	return res;

err:
	CT_DEBUG("qtaguid: ctrl_untag(%s): done.\n", input);
	return res;
}

static int qtaguid_ctrl_parse(const char *input, int count)
{
	char cmd;
	int res;

	cmd = input[0];
	/* Collect params for commands */
	switch (cmd) {
	case 'd':
		res = ctrl_cmd_delete(input);
		break;

	case 's':
		res = ctrl_cmd_counter_set(input);
		break;

	case 't':
		res = ctrl_cmd_tag(input);
		break;

	case 'u':
		res = ctrl_cmd_untag(input);
		break;

	default:
		res = -EINVAL;
		goto err;
	}
	if (!res)
		res = count;
err:
	CT_DEBUG("qtaguid: ctrl(%s): res=%d\n", input, res);
	return res;
}

#define MAX_QTAGUID_CTRL_INPUT_LEN 255
static int qtaguid_ctrl_proc_write(struct file *file, const char __user *buffer,
			unsigned long count, void *data)
{
	char input_buf[MAX_QTAGUID_CTRL_INPUT_LEN];

	if (unlikely(module_passive))
		return count;

	if (count >= MAX_QTAGUID_CTRL_INPUT_LEN)
		return -EINVAL;

	if (copy_from_user(input_buf, buffer, count))
		return -EFAULT;

	input_buf[count] = '\0';
	return qtaguid_ctrl_parse(input_buf, count);
}

struct proc_print_info {
	char *outp;
	char **num_items_returned;
	struct iface_stat *iface_entry;
	struct tag_stat *ts_entry;
	int item_index;
	int items_to_skip;
	int char_count;
};

static int pp_stats_line(struct proc_print_info *ppi, int cnt_set)
{
	int len;
	struct data_counters *cnts;

	if (!ppi->item_index) {
		if (ppi->item_index++ < ppi->items_to_skip)
			return 0;
		len = snprintf(ppi->outp, ppi->char_count,
			       "idx iface acct_tag_hex uid_tag_int cnt_set "
			       "rx_bytes rx_packets "
			       "tx_bytes tx_packets "
			       "rx_tcp_packets rx_tcp_bytes "
			       "rx_udp_packets rx_udp_bytes "
			       "rx_other_packets rx_other_bytes "
			       "tx_tcp_packets tx_tcp_bytes "
			       "tx_udp_packets tx_udp_bytes "
			       "tx_other_packets tx_other_bytes\n");
	} else {
		tag_t tag = ppi->ts_entry->tn.tag;
		uid_t stat_uid = get_uid_from_tag(tag);

		if (!can_read_other_uid_stats(stat_uid)) {
			CT_DEBUG("qtaguid: stats line: "
				 "%s 0x%llx %u: insufficient priv "
				 "from pid=%u tgid=%u uid=%u\n",
				 ppi->iface_entry->ifname,
				 get_atag_from_tag(tag), stat_uid,
				 current->pid, current->tgid, current_fsuid());
			return 0;
		}
		if (ppi->item_index++ < ppi->items_to_skip)
			return 0;
		cnts = &ppi->ts_entry->counters;
		len = snprintf(
			ppi->outp, ppi->char_count,
			"%d %s 0x%llx %u %u "
			"%llu %llu "
			"%llu %llu "
			"%llu %llu "
			"%llu %llu "
			"%llu %llu "
			"%llu %llu "
			"%llu %llu "
			"%llu %llu\n",
			ppi->item_index,
			ppi->iface_entry->ifname,
			get_atag_from_tag(tag),
			stat_uid,
			cnt_set,
			dc_sum_bytes(cnts, cnt_set, IFS_RX),
			dc_sum_packets(cnts, cnt_set, IFS_RX),
			dc_sum_bytes(cnts, cnt_set, IFS_TX),
			dc_sum_packets(cnts, cnt_set, IFS_TX),
			cnts->bpc[cnt_set][IFS_RX][IFS_TCP].bytes,
			cnts->bpc[cnt_set][IFS_RX][IFS_TCP].packets,
			cnts->bpc[cnt_set][IFS_RX][IFS_UDP].bytes,
			cnts->bpc[cnt_set][IFS_RX][IFS_UDP].packets,
			cnts->bpc[cnt_set][IFS_RX][IFS_PROTO_OTHER].bytes,
			cnts->bpc[cnt_set][IFS_RX][IFS_PROTO_OTHER].packets,
			cnts->bpc[cnt_set][IFS_TX][IFS_TCP].bytes,
			cnts->bpc[cnt_set][IFS_TX][IFS_TCP].packets,
			cnts->bpc[cnt_set][IFS_TX][IFS_UDP].bytes,
			cnts->bpc[cnt_set][IFS_TX][IFS_UDP].packets,
			cnts->bpc[cnt_set][IFS_TX][IFS_PROTO_OTHER].bytes,
			cnts->bpc[cnt_set][IFS_TX][IFS_PROTO_OTHER].packets);
	}
	return len;
}

bool pp_sets(struct proc_print_info *ppi)
{
	int len;
	int counter_set;
	for (counter_set = 0; counter_set < IFS_MAX_COUNTER_SETS;
	     counter_set++) {
		len = pp_stats_line(ppi, counter_set);
		if (len >= ppi->char_count) {
			*ppi->outp = '\0';
			return false;
		}
		if (len) {
			ppi->outp += len;
			ppi->char_count -= len;
			(*ppi->num_items_returned)++;
		}
	}
	return true;
}

/*
 * Procfs reader to get all tag stats using style "1)" as described in
 * fs/proc/generic.c
 * Groups all protocols tx/rx bytes.
 */
static int qtaguid_stats_proc_read(char *page, char **num_items_returned,
				off_t items_to_skip, int char_count, int *eof,
				void *data)
{
	struct proc_print_info ppi;
	int len;

	ppi.outp = page;
	ppi.item_index = 0;
	ppi.char_count = char_count;
	ppi.num_items_returned = num_items_returned;
	ppi.items_to_skip = items_to_skip;

	if (unlikely(module_passive)) {
		len = pp_stats_line(&ppi, 0);
		/* The header should always be shorter than the buffer. */
		BUG_ON(len >= ppi.char_count);
		(*num_items_returned)++;
		*eof = 1;
		return len;
	}

	CT_DEBUG("qtaguid:proc stats page=%p *num_items_returned=%p off=%ld "
		"char_count=%d *eof=%d\n", page, *num_items_returned,
		items_to_skip, char_count, *eof);

	if (*eof)
		return 0;

	/* The idx is there to help debug when things go belly up. */
	len = pp_stats_line(&ppi, 0);
	/* Don't advance the outp unless the whole line was printed */
	if (len >= ppi.char_count) {
		*ppi.outp = '\0';
		return ppi.outp - page;
	}
	if (len) {
		ppi.outp += len;
		ppi.char_count -= len;
		(*num_items_returned)++;
	}

	spin_lock_bh(&iface_stat_list_lock);
	list_for_each_entry(ppi.iface_entry, &iface_stat_list, list) {
		struct rb_node *node;
		spin_lock_bh(&ppi.iface_entry->tag_stat_list_lock);
		for (node = rb_first(&ppi.iface_entry->tag_stat_tree);
		     node;
		     node = rb_next(node)) {
			ppi.ts_entry = rb_entry(node, struct tag_stat, tn.node);
			if (!pp_sets(&ppi)) {
				spin_unlock_bh(
					&ppi.iface_entry->tag_stat_list_lock);
				spin_unlock_bh(&iface_stat_list_lock);
				return ppi.outp - page;
			}
		}
		spin_unlock_bh(&ppi.iface_entry->tag_stat_list_lock);
	}
	spin_unlock_bh(&iface_stat_list_lock);

	*eof = 1;
	return ppi.outp - page;
}

/*------------------------------------------*/
static int qtudev_open(struct inode *inode, struct file *file)
{
	struct uid_tag_data *utd_entry;
	struct proc_qtu_data  *pqd_entry;
	struct proc_qtu_data  *new_pqd_entry = 0;
	int res;
	bool utd_entry_found;

	if (unlikely(qtu_proc_handling_passive))
		return 0;

	DR_DEBUG("qtaguid: qtudev_open(): pid=%u tgid=%u uid=%u\n",
		 current->pid, current->tgid, current_fsuid());

	spin_lock_bh(&uid_tag_data_tree_lock);

	/* Look for existing uid data, or alloc one. */
	utd_entry = get_uid_data(current_fsuid(), &utd_entry_found);
	if (IS_ERR_OR_NULL(utd_entry)) {
		res = PTR_ERR(utd_entry);
		goto err;
	}

	/* Look for existing PID based proc_data */
	pqd_entry = proc_qtu_data_tree_search(&proc_qtu_data_tree,
					      current->tgid);
	if (pqd_entry) {
		pr_err("qtaguid: qtudev_open(): %u/%u %u "
		       "%s already opened\n",
		       current->pid, current->tgid, current_fsuid(),
		       QTU_DEV_NAME);
		res = -EBUSY;
		goto err_unlock_free_utd;
	}

	new_pqd_entry = kzalloc(sizeof(*new_pqd_entry), GFP_ATOMIC);
	if (!new_pqd_entry) {
		pr_err("qtaguid: qtudev_open(): %u/%u %u: "
		       "proc data alloc failed\n",
		       current->pid, current->tgid, current_fsuid());
		res = -ENOMEM;
		goto err_unlock_free_utd;
	}
	new_pqd_entry->pid = current->tgid;
	INIT_LIST_HEAD(&new_pqd_entry->sock_tag_list);
	new_pqd_entry->parent_tag_data = utd_entry;

	proc_qtu_data_tree_insert(new_pqd_entry,
				  &proc_qtu_data_tree);

	spin_unlock_bh(&uid_tag_data_tree_lock);
	DR_DEBUG("qtaguid: tracking data for uid=%u\n", current_fsuid());
	file->private_data = new_pqd_entry;
	return 0;

err_unlock_free_utd:
	if (!utd_entry_found) {
		rb_erase(&utd_entry->node, &uid_tag_data_tree);
		kfree(utd_entry);
	}
	spin_unlock_bh(&uid_tag_data_tree_lock);
err:
	return res;
}

static int qtudev_release(struct inode *inode, struct file *file)
{
	struct proc_qtu_data  *pqd_entry = file->private_data;
	struct uid_tag_data  *utd_entry = pqd_entry->parent_tag_data;
	struct sock_tag *st_entry;
	struct rb_root st_to_free_tree = RB_ROOT;
	struct list_head *entry, *next;
	struct tag_ref *tr;

	if (unlikely(qtu_proc_handling_passive))
		return 0;

	/*
	 * Do not trust the current->pid, it might just be a kworker cleaning
	 * up after a dead proc.
	 */
	DR_DEBUG("qtaguid: qtudev_release(): "
		 "pid=%u tgid=%u uid=%u "
		 "pqd_entry=%p->pid=%u utd_entry=%p->active_tags=%d\n",
		 current->pid, current->tgid, pqd_entry->parent_tag_data->uid,
		 pqd_entry, pqd_entry->pid, utd_entry,
		 utd_entry->num_active_tags);

	spin_lock_bh(&sock_tag_list_lock);
	spin_lock_bh(&uid_tag_data_tree_lock);

	/*
	 * If this proc didn't actually tag anything for itself, or has already
	 * willingly cleaned up itself ...
	 */
	put_utd_entry(utd_entry);

	list_for_each_safe(entry, next, &pqd_entry->sock_tag_list) {
		st_entry = list_entry(entry, struct sock_tag, list);
		DR_DEBUG("qtaguid: %s(): "
			 "erase sock_tag=%p->sk=%p pid=%u tgid=%u uid=%u\n",
			 __func__,
			 st_entry, st_entry->sk,
			 current->pid, current->tgid,
			 pqd_entry->parent_tag_data->uid);

		utd_entry = uid_tag_data_tree_search(
			&uid_tag_data_tree,
			get_uid_from_tag(st_entry->tag));
		BUG_ON(IS_ERR_OR_NULL(utd_entry));
		DR_DEBUG("qtaguid: %s(): "
			 "looking for tag=0x%llx in utd_entry=%p\n", __func__,
			 st_entry->tag, utd_entry);
		tr = tag_ref_tree_search(&utd_entry->tag_ref_tree,
					 st_entry->tag);
		BUG_ON(!tr);
		BUG_ON(tr->num_sock_tags <= 0);
		tr->num_sock_tags--;
		free_tag_ref_from_utd_entry(tr, utd_entry);

		rb_erase(&st_entry->sock_node, &sock_tag_tree);
		list_del(&st_entry->list);
		/* Can't sockfd_put() within spinlock, do it later. */
		sock_tag_tree_insert(st_entry, &st_to_free_tree);

		/* Do not put_utd_entry(utd_entry) someone elses utd_entry */
	}

	rb_erase(&pqd_entry->node, &proc_qtu_data_tree);
	kfree(pqd_entry);
	file->private_data = NULL;

	spin_unlock_bh(&uid_tag_data_tree_lock);
	spin_unlock_bh(&sock_tag_list_lock);


	sock_tag_tree_erase(&st_to_free_tree);


	return 0;
}

/*------------------------------------------*/
static const struct file_operations qtudev_fops = {
	.owner = THIS_MODULE,
	.open = qtudev_open,
	.release = qtudev_release,
};

static struct miscdevice qtu_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = QTU_DEV_NAME,
	.fops = &qtudev_fops,
	/* How sad it doesn't allow for defaults: .mode = S_IRUGO | S_IWUSR */
};

/*------------------------------------------*/
static int __init qtaguid_proc_register(struct proc_dir_entry **res_procdir)
{
	int ret;
	*res_procdir = proc_mkdir(module_procdirname, init_net.proc_net);
	if (!*res_procdir) {
		pr_err("qtaguid: failed to create proc/.../xt_qtaguid\n");
		ret = -ENOMEM;
		goto no_dir;
	}

	xt_qtaguid_ctrl_file = create_proc_entry("ctrl", proc_ctrl_perms,
						*res_procdir);
	if (!xt_qtaguid_ctrl_file) {
		pr_err("qtaguid: failed to create xt_qtaguid/ctrl "
			" file\n");
		ret = -ENOMEM;
		goto no_ctrl_entry;
	}
	xt_qtaguid_ctrl_file->read_proc = qtaguid_ctrl_proc_read;
	xt_qtaguid_ctrl_file->write_proc = qtaguid_ctrl_proc_write;

	xt_qtaguid_stats_file = create_proc_entry("stats", proc_stats_perms,
						*res_procdir);
	if (!xt_qtaguid_stats_file) {
		pr_err("qtaguid: failed to create xt_qtaguid/stats "
			"file\n");
		ret = -ENOMEM;
		goto no_stats_entry;
	}
	xt_qtaguid_stats_file->read_proc = qtaguid_stats_proc_read;
	/*
	 * TODO: add support counter hacking
	 * xt_qtaguid_stats_file->write_proc = qtaguid_stats_proc_write;
	 */
	return 0;

no_stats_entry:
	remove_proc_entry("ctrl", *res_procdir);
no_ctrl_entry:
	remove_proc_entry("xt_qtaguid", NULL);
no_dir:
	return ret;
}

static struct xt_match qtaguid_mt_reg __read_mostly = {
	/*
	 * This module masquerades as the "owner" module so that iptables
	 * tools can deal with it.
	 */
	.name       = "owner",
	.revision   = 1,
	.family     = NFPROTO_UNSPEC,
	.match      = qtaguid_mt,
	.matchsize  = sizeof(struct xt_qtaguid_match_info),
	.me         = THIS_MODULE,
};

static int __init qtaguid_mt_init(void)
{
	if (qtaguid_proc_register(&xt_qtaguid_procdir)
	    || iface_stat_init(xt_qtaguid_procdir)
	    || xt_register_match(&qtaguid_mt_reg)
	    || misc_register(&qtu_device))
		return -1;
	return 0;
}

/*
 * TODO: allow unloading of the module.
 * For now stats are permanent.
 * Kconfig forces'y/n' and never an 'm'.
 */

module_init(qtaguid_mt_init);
MODULE_AUTHOR("jpa <jpa@google.com>");
MODULE_DESCRIPTION("Xtables: socket owner+tag matching and associated stats");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_owner");
MODULE_ALIAS("ip6t_owner");
MODULE_ALIAS("ipt_qtaguid");
MODULE_ALIAS("ip6t_qtaguid");
