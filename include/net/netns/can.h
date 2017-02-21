/*
 * can in net namespaces
 */

#ifndef __NETNS_CAN_H__
#define __NETNS_CAN_H__

#include <linux/spinlock.h>

struct dev_rcv_lists;

struct netns_can {
#if IS_ENABLED(CONFIG_PROC_FS)
	struct proc_dir_entry *proc_dir;
	struct proc_dir_entry *pde_version;
	struct proc_dir_entry *pde_stats;
	struct proc_dir_entry *pde_reset_stats;
	struct proc_dir_entry *pde_rcvlist_all;
	struct proc_dir_entry *pde_rcvlist_fil;
	struct proc_dir_entry *pde_rcvlist_inv;
	struct proc_dir_entry *pde_rcvlist_sff;
	struct proc_dir_entry *pde_rcvlist_eff;
	struct proc_dir_entry *pde_rcvlist_err;
#endif

	/* receive filters subscribed for 'all' CAN devices */
	struct dev_rcv_lists *can_rx_alldev_list;
	spinlock_t can_rcvlists_lock;
};

#endif /* __NETNS_CAN_H__ */
