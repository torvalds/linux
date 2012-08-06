#ifndef __NETNS_SCTP_H__
#define __NETNS_SCTP_H__

struct netns_sctp {
	/* This is the global local address list.
	 * We actively maintain this complete list of addresses on
	 * the system by catching address add/delete events.
	 *
	 * It is a list of sctp_sockaddr_entry.
	 */
	struct list_head local_addr_list;
	struct list_head addr_waitq;
	struct timer_list addr_wq_timer;
	struct list_head auto_asconf_splist;
	spinlock_t addr_wq_lock;

	/* Lock that protects the local_addr_list writers */
	spinlock_t local_addr_lock;
};

#endif /* __NETNS_SCTP_H__ */
