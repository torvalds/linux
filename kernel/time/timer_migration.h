/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _KERNEL_TIME_MIGRATION_H
#define _KERNEL_TIME_MIGRATION_H

/* Per group capacity. Must be a power of 2! */
#define TMIGR_CHILDREN_PER_GROUP 8

/**
 * struct tmigr_event - a timer event associated to a CPU
 * @nextevt:	The node to enqueue an event in the parent group queue
 * @cpu:	The CPU to which this event belongs
 * @ignore:	Hint whether the event could be ignored; it is set when
 *		CPU or group is active;
 */
struct tmigr_event {
	struct timerqueue_node	nextevt;
	unsigned int		cpu;
	bool			ignore;
};

/**
 * struct tmigr_group - timer migration hierarchy group
 * @lock:		Lock protecting the event information and group hierarchy
 *			information during setup
 * @parent:		Pointer to the parent group
 * @groupevt:		Next event of the group which is only used when the
 *			group is !active. The group event is then queued into
 *			the parent timer queue.
 *			Ignore bit of @groupevt is set when the group is active.
 * @next_expiry:	Base monotonic expiry time of the next event of the
 *			group; It is used for the racy lockless check whether a
 *			remote expiry is required; it is always reliable
 * @events:		Timer queue for child events queued in the group
 * @migr_state:		State of the group (see union tmigr_state)
 * @level:		Hierarchy level of the group; Required during setup
 * @numa_node:		Required for setup only to make sure CPU and low level
 *			group information is NUMA local. It is set to NUMA node
 *			as long as the group level is per NUMA node (level <
 *			tmigr_crossnode_level); otherwise it is set to
 *			NUMA_NO_NODE
 * @num_children:	Counter of group children to make sure the group is only
 *			filled with TMIGR_CHILDREN_PER_GROUP; Required for setup
 *			only
 * @childmask:		childmask of the group in the parent group; is set
 *			during setup and will never change; can be read
 *			lockless
 * @list:		List head that is added to the per level
 *			tmigr_level_list; is required during setup when a
 *			new group needs to be connected to the existing
 *			hierarchy groups
 */
struct tmigr_group {
	raw_spinlock_t		lock;
	struct tmigr_group	*parent;
	struct tmigr_event	groupevt;
	u64			next_expiry;
	struct timerqueue_head	events;
	atomic_t		migr_state;
	unsigned int		level;
	int			numa_node;
	unsigned int		num_children;
	u8			childmask;
	struct list_head	list;
};

/**
 * struct tmigr_cpu - timer migration per CPU group
 * @lock:		Lock protecting the tmigr_cpu group information
 * @online:		Indicates whether the CPU is online; In deactivate path
 *			it is required to know whether the migrator in the top
 *			level group is to be set offline, while a timer is
 *			pending. Then another online CPU needs to be notified to
 *			take over the migrator role. Furthermore the information
 *			is required in CPU hotplug path as the CPU is able to go
 *			idle before the timer migration hierarchy hotplug AP is
 *			reached. During this phase, the CPU has to handle the
 *			global timers on its own and must not act as a migrator.
 * @idle:		Indicates whether the CPU is idle in the timer migration
 *			hierarchy
 * @remote:		Is set when timers of the CPU are expired remotely
 * @tmgroup:		Pointer to the parent group
 * @childmask:		childmask of tmigr_cpu in the parent group
 * @wakeup:		Stores the first timer when the timer migration
 *			hierarchy is completely idle and remote expiry was done;
 *			is returned to timer code in the idle path and is only
 *			used in idle path.
 * @cpuevt:		CPU event which could be enqueued into the parent group
 */
struct tmigr_cpu {
	raw_spinlock_t		lock;
	bool			online;
	bool			idle;
	bool			remote;
	struct tmigr_group	*tmgroup;
	u8			childmask;
	u64			wakeup;
	struct tmigr_event	cpuevt;
};

/**
 * union tmigr_state - state of tmigr_group
 * @state:	Combined version of the state - only used for atomic
 *		read/cmpxchg function
 * @struct:	Split version of the state - only use the struct members to
 *		update information to stay independent of endianness
 */
union tmigr_state {
	u32 state;
	/**
	 * struct - split state of tmigr_group
	 * @active:	Contains each childmask bit of the active children
	 * @migrator:	Contains childmask of the child which is migrator
	 * @seq:	Sequence counter needs to be increased when an update
	 *		to the tmigr_state is done. It prevents a race when
	 *		updates in the child groups are propagated in changed
	 *		order. Detailed information about the scenario is
	 *		given in the documentation at the begin of
	 *		timer_migration.c.
	 */
	struct {
		u8	active;
		u8	migrator;
		u16	seq;
	} __packed;
};

#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
extern void tmigr_handle_remote(void);
extern bool tmigr_requires_handle_remote(void);
extern void tmigr_cpu_activate(void);
extern u64 tmigr_cpu_deactivate(u64 nextevt);
extern u64 tmigr_cpu_new_timer(u64 nextevt);
extern u64 tmigr_quick_check(u64 nextevt);
#else
static inline void tmigr_handle_remote(void) { }
static inline bool tmigr_requires_handle_remote(void) { return false; }
static inline void tmigr_cpu_activate(void) { }
#endif

#endif
