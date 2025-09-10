// SPDX-License-Identifier: GPL-2.0-only
/*
 * Infrastructure for migratable timers
 *
 * Copyright(C) 2022 linutronix GmbH
 */
#include <linux/cpuhotplug.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/timerqueue.h>
#include <trace/events/ipi.h>

#include "timer_migration.h"
#include "tick-internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/timer_migration.h>

/*
 * The timer migration mechanism is built on a hierarchy of groups. The
 * lowest level group contains CPUs, the next level groups of CPU groups
 * and so forth. The CPU groups are kept per node so for the normal case
 * lock contention won't happen across nodes. Depending on the number of
 * CPUs per node even the next level might be kept as groups of CPU groups
 * per node and only the levels above cross the node topology.
 *
 * Example topology for a two node system with 24 CPUs each.
 *
 * LVL 2                           [GRP2:0]
 *                              GRP1:0 = GRP1:M
 *
 * LVL 1            [GRP1:0]                      [GRP1:1]
 *               GRP0:0 - GRP0:2               GRP0:3 - GRP0:5
 *
 * LVL 0  [GRP0:0]  [GRP0:1]  [GRP0:2]  [GRP0:3]  [GRP0:4]  [GRP0:5]
 * CPUS     0-7       8-15      16-23     24-31     32-39     40-47
 *
 * The groups hold a timer queue of events sorted by expiry time. These
 * queues are updated when CPUs go in idle. When they come out of idle
 * ignore flag of events is set.
 *
 * Each group has a designated migrator CPU/group as long as a CPU/group is
 * active in the group. This designated role is necessary to avoid that all
 * active CPUs in a group try to migrate expired timers from other CPUs,
 * which would result in massive lock bouncing.
 *
 * When a CPU is awake, it checks in it's own timer tick the group
 * hierarchy up to the point where it is assigned the migrator role or if
 * no CPU is active, it also checks the groups where no migrator is set
 * (TMIGR_NONE).
 *
 * If it finds expired timers in one of the group queues it pulls them over
 * from the idle CPU and runs the timer function. After that it updates the
 * group and the parent groups if required.
 *
 * CPUs which go idle arm their CPU local timer hardware for the next local
 * (pinned) timer event. If the next migratable timer expires after the
 * next local timer or the CPU has no migratable timer pending then the
 * CPU does not queue an event in the LVL0 group. If the next migratable
 * timer expires before the next local timer then the CPU queues that timer
 * in the LVL0 group. In both cases the CPU marks itself idle in the LVL0
 * group.
 *
 * When CPU comes out of idle and when a group has at least a single active
 * child, the ignore flag of the tmigr_event is set. This indicates, that
 * the event is ignored even if it is still enqueued in the parent groups
 * timer queue. It will be removed when touching the timer queue the next
 * time. This spares locking in active path as the lock protects (after
 * setup) only event information. For more information about locking,
 * please read the section "Locking rules".
 *
 * If the CPU is the migrator of the group then it delegates that role to
 * the next active CPU in the group or sets migrator to TMIGR_NONE when
 * there is no active CPU in the group. This delegation needs to be
 * propagated up the hierarchy so hand over from other leaves can happen at
 * all hierarchy levels w/o doing a search.
 *
 * When the last CPU in the system goes idle, then it drops all migrator
 * duties up to the top level of the hierarchy (LVL2 in the example). It
 * then has to make sure, that it arms it's own local hardware timer for
 * the earliest event in the system.
 *
 *
 * Lifetime rules:
 * ---------------
 *
 * The groups are built up at init time or when CPUs come online. They are
 * not destroyed when a group becomes empty due to offlining. The group
 * just won't participate in the hierarchy management anymore. Destroying
 * groups would result in interesting race conditions which would just make
 * the whole mechanism slow and complex.
 *
 *
 * Locking rules:
 * --------------
 *
 * For setting up new groups and handling events it's required to lock both
 * child and parent group. The lock ordering is always bottom up. This also
 * includes the per CPU locks in struct tmigr_cpu. For updating the migrator and
 * active CPU/group information atomic_try_cmpxchg() is used instead and only
 * the per CPU tmigr_cpu->lock is held.
 *
 * During the setup of groups tmigr_level_list is required. It is protected by
 * @tmigr_mutex.
 *
 * When @timer_base->lock as well as tmigr related locks are required, the lock
 * ordering is: first @timer_base->lock, afterwards tmigr related locks.
 *
 *
 * Protection of the tmigr group state information:
 * ------------------------------------------------
 *
 * The state information with the list of active children and migrator needs to
 * be protected by a sequence counter. It prevents a race when updates in child
 * groups are propagated in changed order. The state update is performed
 * lockless and group wise. The following scenario describes what happens
 * without updating the sequence counter:
 *
 * Therefore, let's take three groups and four CPUs (CPU2 and CPU3 as well
 * as GRP0:1 will not change during the scenario):
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:0, GRP0:1
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = CPU0           migrator = CPU2
 *           active   = CPU0           active   = CPU2
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             active      idle           active      idle
 *
 *
 * 1. CPU0 goes idle. As the update is performed group wise, in the first step
 *    only GRP0:0 is updated. The update of GRP1:0 is pending as CPU0 has to
 *    walk the hierarchy.
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:0, GRP0:1
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *       --> migrator = TMIGR_NONE     migrator = CPU2
 *       --> active   =                active   = CPU2
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *         --> idle        idle           active      idle
 *
 * 2. While CPU0 goes idle and continues to update the state, CPU1 comes out of
 *    idle. CPU1 updates GRP0:0. The update for GRP1:0 is pending as CPU1 also
 *    has to walk the hierarchy. Both CPUs (CPU0 and CPU1) now walk the
 *    hierarchy to perform the needed update from their point of view. The
 *    currently visible state looks the following:
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:0, GRP0:1
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *       --> migrator = CPU1           migrator = CPU2
 *       --> active   = CPU1           active   = CPU2
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle    --> active         active      idle
 *
 * 3. Here is the race condition: CPU1 managed to propagate its changes (from
 *    step 2) through the hierarchy to GRP1:0 before CPU0 (step 1) did. The
 *    active members of GRP1:0 remain unchanged after the update since it is
 *    still valid from CPU1 current point of view:
 *
 *    LVL 1            [GRP1:0]
 *                 --> migrator = GRP0:1
 *                 --> active   = GRP0:0, GRP0:1
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = CPU1           migrator = CPU2
 *           active   = CPU1           active   = CPU2
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        active         active      idle
 *
 * 4. Now CPU0 finally propagates its changes (from step 1) to GRP1:0.
 *
 *    LVL 1            [GRP1:0]
 *                 --> migrator = GRP0:1
 *                 --> active   = GRP0:1
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = CPU1           migrator = CPU2
 *           active   = CPU1           active   = CPU2
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        active         active      idle
 *
 *
 * The race of CPU0 vs. CPU1 led to an inconsistent state in GRP1:0. CPU1 is
 * active and is correctly listed as active in GRP0:0. However GRP1:0 does not
 * have GRP0:0 listed as active, which is wrong. The sequence counter has been
 * added to avoid inconsistent states during updates. The state is updated
 * atomically only if all members, including the sequence counter, match the
 * expected value (compare-and-exchange).
 *
 * Looking back at the previous example with the addition of the sequence
 * counter: The update as performed by CPU0 in step 4 will fail. CPU1 changed
 * the sequence number during the update in step 3 so the expected old value (as
 * seen by CPU0 before starting the walk) does not match.
 *
 * Prevent race between new event and last CPU going inactive
 * ----------------------------------------------------------
 *
 * When the last CPU is going idle and there is a concurrent update of a new
 * first global timer of an idle CPU, the group and child states have to be read
 * while holding the lock in tmigr_update_events(). The following scenario shows
 * what happens, when this is not done.
 *
 * 1. Only CPU2 is active:
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:1
 *                     next_expiry = KTIME_MAX
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE     migrator = CPU2
 *           active   =                active   = CPU2
 *           next_expiry = KTIME_MAX   next_expiry = KTIME_MAX
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle           active      idle
 *
 * 2. Now CPU 2 goes idle (and has no global timer, that has to be handled) and
 *    propagates that to GRP0:1:
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:1
 *                     next_expiry = KTIME_MAX
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE --> migrator = TMIGR_NONE
 *           active   =            --> active   =
 *           next_expiry = KTIME_MAX   next_expiry = KTIME_MAX
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle       --> idle        idle
 *
 * 3. Now the idle state is propagated up to GRP1:0. As this is now the last
 *    child going idle in top level group, the expiry of the next group event
 *    has to be handed back to make sure no event is lost. As there is no event
 *    enqueued, KTIME_MAX is handed back to CPU2.
 *
 *    LVL 1            [GRP1:0]
 *                 --> migrator = TMIGR_NONE
 *                 --> active   =
 *                     next_expiry = KTIME_MAX
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE     migrator = TMIGR_NONE
 *           active   =                active   =
 *           next_expiry = KTIME_MAX   next_expiry = KTIME_MAX
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle       --> idle        idle
 *
 * 4. CPU 0 has a new timer queued from idle and it expires at TIMER0. CPU0
 *    propagates that to GRP0:0:
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = TMIGR_NONE
 *                     active   =
 *                     next_expiry = KTIME_MAX
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE     migrator = TMIGR_NONE
 *           active   =                active   =
 *       --> next_expiry = TIMER0      next_expiry  = KTIME_MAX
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle           idle        idle
 *
 * 5. GRP0:0 is not active, so the new timer has to be propagated to
 *    GRP1:0. Therefore the GRP1:0 state has to be read. When the stalled value
 *    (from step 2) is read, the timer is enqueued into GRP1:0, but nothing is
 *    handed back to CPU0, as it seems that there is still an active child in
 *    top level group.
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = TMIGR_NONE
 *                     active   =
 *                 --> next_expiry = TIMER0
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE     migrator = TMIGR_NONE
 *           active   =                active   =
 *           next_expiry = TIMER0      next_expiry  = KTIME_MAX
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle           idle        idle
 *
 * This is prevented by reading the state when holding the lock (when a new
 * timer has to be propagated from idle path)::
 *
 *   CPU2 (tmigr_inactive_up())          CPU0 (tmigr_new_timer_up())
 *   --------------------------          ---------------------------
 *   // step 3:
 *   cmpxchg(&GRP1:0->state);
 *   tmigr_update_events() {
 *       spin_lock(&GRP1:0->lock);
 *       // ... update events ...
 *       // hand back first expiry when GRP1:0 is idle
 *       spin_unlock(&GRP1:0->lock);
 *       // ^^^ release state modification
 *   }
 *                                       tmigr_update_events() {
 *                                           spin_lock(&GRP1:0->lock)
 *                                           // ^^^ acquire state modification
 *                                           group_state = atomic_read(&GRP1:0->state)
 *                                           // .... update events ...
 *                                           // hand back first expiry when GRP1:0 is idle
 *                                           spin_unlock(&GRP1:0->lock) <3>
 *                                           // ^^^ makes state visible for other
 *                                           // callers of tmigr_new_timer_up()
 *                                       }
 *
 * When CPU0 grabs the lock directly after cmpxchg, the first timer is reported
 * back to CPU0 and also later on to CPU2. So no timer is missed. A concurrent
 * update of the group state from active path is no problem, as the upcoming CPU
 * will take care of the group events.
 *
 * Required event and timerqueue update after a remote expiry:
 * -----------------------------------------------------------
 *
 * After expiring timers of a remote CPU, a walk through the hierarchy and
 * update of events and timerqueues is required. It is obviously needed if there
 * is a 'new' global timer but also if there is no new global timer but the
 * remote CPU is still idle.
 *
 * 1. CPU0 and CPU1 are idle and have both a global timer expiring at the same
 *    time. So both have an event enqueued in the timerqueue of GRP0:0. CPU3 is
 *    also idle and has no global timer pending. CPU2 is the only active CPU and
 *    thus also the migrator:
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:1
 *                 --> timerqueue = evt-GRP0:0
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE     migrator = CPU2
 *           active   =                active   = CPU2
 *           groupevt.ignore = false   groupevt.ignore = true
 *           groupevt.cpu = CPU0       groupevt.cpu =
 *           timerqueue = evt-CPU0,    timerqueue =
 *                        evt-CPU1
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle           active      idle
 *
 * 2. CPU2 starts to expire remote timers. It starts with LVL0 group
 *    GRP0:1. There is no event queued in the timerqueue, so CPU2 continues with
 *    the parent of GRP0:1: GRP1:0. In GRP1:0 it dequeues the first event. It
 *    looks at tmigr_event::cpu struct member and expires the pending timer(s)
 *    of CPU0.
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:1
 *                 --> timerqueue =
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE     migrator = CPU2
 *           active   =                active   = CPU2
 *           groupevt.ignore = false   groupevt.ignore = true
 *       --> groupevt.cpu = CPU0       groupevt.cpu =
 *           timerqueue = evt-CPU0,    timerqueue =
 *                        evt-CPU1
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle           active      idle
 *
 * 3. Some work has to be done after expiring the timers of CPU0. If we stop
 *    here, then CPU1's pending global timer(s) will not expire in time and the
 *    timerqueue of GRP0:0 has still an event for CPU0 enqueued which has just
 *    been processed. So it is required to walk the hierarchy from CPU0's point
 *    of view and update it accordingly. CPU0's event will be removed from the
 *    timerqueue because it has no pending timer. If CPU0 would have a timer
 *    pending then it has to expire after CPU1's first timer because all timers
 *    from this period were just expired. Either way CPU1's event will be first
 *    in GRP0:0's timerqueue and therefore set in the CPU field of the group
 *    event which is then enqueued in GRP1:0's timerqueue as GRP0:0 is still not
 *    active:
 *
 *    LVL 1            [GRP1:0]
 *                     migrator = GRP0:1
 *                     active   = GRP0:1
 *                 --> timerqueue = evt-GRP0:0
 *                   /                \
 *    LVL 0  [GRP0:0]                  [GRP0:1]
 *           migrator = TMIGR_NONE     migrator = CPU2
 *           active   =                active   = CPU2
 *           groupevt.ignore = false   groupevt.ignore = true
 *       --> groupevt.cpu = CPU1       groupevt.cpu =
 *       --> timerqueue = evt-CPU1     timerqueue =
 *              /         \                /         \
 *    CPUs     0           1              2           3
 *             idle        idle           active      idle
 *
 * Now CPU2 (migrator) will continue step 2 at GRP1:0 and will expire the
 * timer(s) of CPU1.
 *
 * The hierarchy walk in step 3 can be skipped if the migrator notices that a
 * CPU of GRP0:0 is active again. The CPU will mark GRP0:0 active and take care
 * of the group as migrator and any needed updates within the hierarchy.
 */

static DEFINE_MUTEX(tmigr_mutex);
static struct list_head *tmigr_level_list __read_mostly;

static unsigned int tmigr_hierarchy_levels __read_mostly;
static unsigned int tmigr_crossnode_level __read_mostly;

static DEFINE_PER_CPU(struct tmigr_cpu, tmigr_cpu);

#define TMIGR_NONE	0xFF
#define BIT_CNT		8

static inline bool tmigr_is_not_available(struct tmigr_cpu *tmc)
{
	return !(tmc->tmgroup && tmc->online);
}

/*
 * Returns true, when @childmask corresponds to the group migrator or when the
 * group is not active - so no migrator is set.
 */
static bool tmigr_check_migrator(struct tmigr_group *group, u8 childmask)
{
	union tmigr_state s;

	s.state = atomic_read(&group->migr_state);

	if ((s.migrator == childmask) || (s.migrator == TMIGR_NONE))
		return true;

	return false;
}

static bool tmigr_check_migrator_and_lonely(struct tmigr_group *group, u8 childmask)
{
	bool lonely, migrator = false;
	unsigned long active;
	union tmigr_state s;

	s.state = atomic_read(&group->migr_state);

	if ((s.migrator == childmask) || (s.migrator == TMIGR_NONE))
		migrator = true;

	active = s.active;
	lonely = bitmap_weight(&active, BIT_CNT) <= 1;

	return (migrator && lonely);
}

static bool tmigr_check_lonely(struct tmigr_group *group)
{
	unsigned long active;
	union tmigr_state s;

	s.state = atomic_read(&group->migr_state);

	active = s.active;

	return bitmap_weight(&active, BIT_CNT) <= 1;
}

/**
 * struct tmigr_walk - data required for walking the hierarchy
 * @nextexp:		Next CPU event expiry information which is handed into
 *			the timer migration code by the timer code
 *			(get_next_timer_interrupt())
 * @firstexp:		Contains the first event expiry information when
 *			hierarchy is completely idle.  When CPU itself was the
 *			last going idle, information makes sure, that CPU will
 *			be back in time. When using this value in the remote
 *			expiry case, firstexp is stored in the per CPU tmigr_cpu
 *			struct of CPU which expires remote timers. It is updated
 *			in top level group only. Be aware, there could occur a
 *			new top level of the hierarchy between the 'top level
 *			call' in tmigr_update_events() and the check for the
 *			parent group in walk_groups(). Then @firstexp might
 *			contain a value != KTIME_MAX even if it was not the
 *			final top level. This is not a problem, as the worst
 *			outcome is a CPU which might wake up a little early.
 * @evt:		Pointer to tmigr_event which needs to be queued (of idle
 *			child group)
 * @childmask:		groupmask of child group
 * @remote:		Is set, when the new timer path is executed in
 *			tmigr_handle_remote_cpu()
 * @basej:		timer base in jiffies
 * @now:		timer base monotonic
 * @check:		is set if there is the need to handle remote timers;
 *			required in tmigr_requires_handle_remote() only
 * @tmc_active:		this flag indicates, whether the CPU which triggers
 *			the hierarchy walk is !idle in the timer migration
 *			hierarchy. When the CPU is idle and the whole hierarchy is
 *			idle, only the first event of the top level has to be
 *			considered.
 */
struct tmigr_walk {
	u64			nextexp;
	u64			firstexp;
	struct tmigr_event	*evt;
	u8			childmask;
	bool			remote;
	unsigned long		basej;
	u64			now;
	bool			check;
	bool			tmc_active;
};

typedef bool (*up_f)(struct tmigr_group *, struct tmigr_group *, struct tmigr_walk *);

static void __walk_groups(up_f up, struct tmigr_walk *data,
			  struct tmigr_cpu *tmc)
{
	struct tmigr_group *child = NULL, *group = tmc->tmgroup;

	do {
		WARN_ON_ONCE(group->level >= tmigr_hierarchy_levels);

		if (up(group, child, data))
			break;

		child = group;
		/*
		 * Pairs with the store release on group connection
		 * to make sure group initialization is visible.
		 */
		group = READ_ONCE(group->parent);
		data->childmask = child->groupmask;
		WARN_ON_ONCE(!data->childmask);
	} while (group);
}

static void walk_groups(up_f up, struct tmigr_walk *data, struct tmigr_cpu *tmc)
{
	lockdep_assert_held(&tmc->lock);

	__walk_groups(up, data, tmc);
}

/*
 * Returns the next event of the timerqueue @group->events
 *
 * Removes timers with ignore flag and update next_expiry of the group. Values
 * of the group event are updated in tmigr_update_events() only.
 */
static struct tmigr_event *tmigr_next_groupevt(struct tmigr_group *group)
{
	struct timerqueue_node *node = NULL;
	struct tmigr_event *evt = NULL;

	lockdep_assert_held(&group->lock);

	WRITE_ONCE(group->next_expiry, KTIME_MAX);

	while ((node = timerqueue_getnext(&group->events))) {
		evt = container_of(node, struct tmigr_event, nextevt);

		if (!READ_ONCE(evt->ignore)) {
			WRITE_ONCE(group->next_expiry, evt->nextevt.expires);
			return evt;
		}

		/*
		 * Remove next timers with ignore flag, because the group lock
		 * is held anyway
		 */
		if (!timerqueue_del(&group->events, node))
			break;
	}

	return NULL;
}

/*
 * Return the next event (with the expiry equal or before @now)
 *
 * Event, which is returned, is also removed from the queue.
 */
static struct tmigr_event *tmigr_next_expired_groupevt(struct tmigr_group *group,
						       u64 now)
{
	struct tmigr_event *evt = tmigr_next_groupevt(group);

	if (!evt || now < evt->nextevt.expires)
		return NULL;

	/*
	 * The event is ready to expire. Remove it and update next group event.
	 */
	timerqueue_del(&group->events, &evt->nextevt);
	tmigr_next_groupevt(group);

	return evt;
}

static u64 tmigr_next_groupevt_expires(struct tmigr_group *group)
{
	struct tmigr_event *evt;

	evt = tmigr_next_groupevt(group);

	if (!evt)
		return KTIME_MAX;
	else
		return evt->nextevt.expires;
}

static bool tmigr_active_up(struct tmigr_group *group,
			    struct tmigr_group *child,
			    struct tmigr_walk *data)
{
	union tmigr_state curstate, newstate;
	bool walk_done;
	u8 childmask;

	childmask = data->childmask;
	/*
	 * No memory barrier is required here in contrast to
	 * tmigr_inactive_up(), as the group state change does not depend on the
	 * child state.
	 */
	curstate.state = atomic_read(&group->migr_state);

	do {
		newstate = curstate;
		walk_done = true;

		if (newstate.migrator == TMIGR_NONE) {
			newstate.migrator = childmask;

			/* Changes need to be propagated */
			walk_done = false;
		}

		newstate.active |= childmask;
		newstate.seq++;

	} while (!atomic_try_cmpxchg(&group->migr_state, &curstate.state, newstate.state));

	trace_tmigr_group_set_cpu_active(group, newstate, childmask);

	/*
	 * The group is active (again). The group event might be still queued
	 * into the parent group's timerqueue but can now be handled by the
	 * migrator of this group. Therefore the ignore flag for the group event
	 * is updated to reflect this.
	 *
	 * The update of the ignore flag in the active path is done lockless. In
	 * worst case the migrator of the parent group observes the change too
	 * late and expires remotely all events belonging to this group. The
	 * lock is held while updating the ignore flag in idle path. So this
	 * state change will not be lost.
	 */
	WRITE_ONCE(group->groupevt.ignore, true);

	return walk_done;
}

static void __tmigr_cpu_activate(struct tmigr_cpu *tmc)
{
	struct tmigr_walk data;

	data.childmask = tmc->groupmask;

	trace_tmigr_cpu_active(tmc);

	tmc->cpuevt.ignore = true;
	WRITE_ONCE(tmc->wakeup, KTIME_MAX);

	walk_groups(&tmigr_active_up, &data, tmc);
}

/**
 * tmigr_cpu_activate() - set this CPU active in timer migration hierarchy
 *
 * Call site timer_clear_idle() is called with interrupts disabled.
 */
void tmigr_cpu_activate(void)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);

	if (tmigr_is_not_available(tmc))
		return;

	if (WARN_ON_ONCE(!tmc->idle))
		return;

	raw_spin_lock(&tmc->lock);
	tmc->idle = false;
	__tmigr_cpu_activate(tmc);
	raw_spin_unlock(&tmc->lock);
}

/*
 * Returns true, if there is nothing to be propagated to the next level
 *
 * @data->firstexp is set to expiry of first gobal event of the (top level of
 * the) hierarchy, but only when hierarchy is completely idle.
 *
 * The child and group states need to be read under the lock, to prevent a race
 * against a concurrent tmigr_inactive_up() run when the last CPU goes idle. See
 * also section "Prevent race between new event and last CPU going inactive" in
 * the documentation at the top.
 *
 * This is the only place where the group event expiry value is set.
 */
static
bool tmigr_update_events(struct tmigr_group *group, struct tmigr_group *child,
			 struct tmigr_walk *data)
{
	struct tmigr_event *evt, *first_childevt;
	union tmigr_state childstate, groupstate;
	bool remote = data->remote;
	bool walk_done = false;
	bool ignore;
	u64 nextexp;

	if (child) {
		raw_spin_lock(&child->lock);
		raw_spin_lock_nested(&group->lock, SINGLE_DEPTH_NESTING);

		childstate.state = atomic_read(&child->migr_state);
		groupstate.state = atomic_read(&group->migr_state);

		if (childstate.active) {
			walk_done = true;
			goto unlock;
		}

		first_childevt = tmigr_next_groupevt(child);
		nextexp = child->next_expiry;
		evt = &child->groupevt;

		/*
		 * This can race with concurrent idle exit (activate).
		 * If the current writer wins, a useless remote expiration may
		 * be scheduled. If the activate wins, the event is properly
		 * ignored.
		 */
		ignore = (nextexp == KTIME_MAX) ? true : false;
		WRITE_ONCE(evt->ignore, ignore);
	} else {
		nextexp = data->nextexp;

		first_childevt = evt = data->evt;
		ignore = evt->ignore;

		/*
		 * Walking the hierarchy is required in any case when a
		 * remote expiry was done before. This ensures to not lose
		 * already queued events in non active groups (see section
		 * "Required event and timerqueue update after a remote
		 * expiry" in the documentation at the top).
		 *
		 * The two call sites which are executed without a remote expiry
		 * before, are not prevented from propagating changes through
		 * the hierarchy by the return:
		 *  - When entering this path by tmigr_new_timer(), @evt->ignore
		 *    is never set.
		 *  - tmigr_inactive_up() takes care of the propagation by
		 *    itself and ignores the return value. But an immediate
		 *    return is possible if there is a parent, sparing group
		 *    locking at this level, because the upper walking call to
		 *    the parent will take care about removing this event from
		 *    within the group and update next_expiry accordingly.
		 *
		 * However if there is no parent, ie: the hierarchy has only a
		 * single level so @group is the top level group, make sure the
		 * first event information of the group is updated properly and
		 * also handled properly, so skip this fast return path.
		 */
		if (ignore && !remote && group->parent)
			return true;

		raw_spin_lock(&group->lock);

		childstate.state = 0;
		groupstate.state = atomic_read(&group->migr_state);
	}

	/*
	 * If the child event is already queued in the group, remove it from the
	 * queue when the expiry time changed only or when it could be ignored.
	 */
	if (timerqueue_node_queued(&evt->nextevt)) {
		if ((evt->nextevt.expires == nextexp) && !ignore) {
			/* Make sure not to miss a new CPU event with the same expiry */
			evt->cpu = first_childevt->cpu;
			goto check_toplvl;
		}

		if (!timerqueue_del(&group->events, &evt->nextevt))
			WRITE_ONCE(group->next_expiry, KTIME_MAX);
	}

	if (ignore) {
		/*
		 * When the next child event could be ignored (nextexp is
		 * KTIME_MAX) and there was no remote timer handling before or
		 * the group is already active, there is no need to walk the
		 * hierarchy even if there is a parent group.
		 *
		 * The other way round: even if the event could be ignored, but
		 * if a remote timer handling was executed before and the group
		 * is not active, walking the hierarchy is required to not miss
		 * an enqueued timer in the non active group. The enqueued timer
		 * of the group needs to be propagated to a higher level to
		 * ensure it is handled.
		 */
		if (!remote || groupstate.active)
			walk_done = true;
	} else {
		evt->nextevt.expires = nextexp;
		evt->cpu = first_childevt->cpu;

		if (timerqueue_add(&group->events, &evt->nextevt))
			WRITE_ONCE(group->next_expiry, nextexp);
	}

check_toplvl:
	if (!group->parent && (groupstate.migrator == TMIGR_NONE)) {
		walk_done = true;

		/*
		 * Nothing to do when update was done during remote timer
		 * handling. First timer in top level group which needs to be
		 * handled when top level group is not active, is calculated
		 * directly in tmigr_handle_remote_up().
		 */
		if (remote)
			goto unlock;

		/*
		 * The top level group is idle and it has to be ensured the
		 * global timers are handled in time. (This could be optimized
		 * by keeping track of the last global scheduled event and only
		 * arming it on the CPU if the new event is earlier. Not sure if
		 * its worth the complexity.)
		 */
		data->firstexp = tmigr_next_groupevt_expires(group);
	}

	trace_tmigr_update_events(child, group, childstate, groupstate,
				  nextexp);

unlock:
	raw_spin_unlock(&group->lock);

	if (child)
		raw_spin_unlock(&child->lock);

	return walk_done;
}

static bool tmigr_new_timer_up(struct tmigr_group *group,
			       struct tmigr_group *child,
			       struct tmigr_walk *data)
{
	return tmigr_update_events(group, child, data);
}

/*
 * Returns the expiry of the next timer that needs to be handled. KTIME_MAX is
 * returned, if an active CPU will handle all the timer migration hierarchy
 * timers.
 */
static u64 tmigr_new_timer(struct tmigr_cpu *tmc, u64 nextexp)
{
	struct tmigr_walk data = { .nextexp = nextexp,
				   .firstexp = KTIME_MAX,
				   .evt = &tmc->cpuevt };

	lockdep_assert_held(&tmc->lock);

	if (tmc->remote)
		return KTIME_MAX;

	trace_tmigr_cpu_new_timer(tmc);

	tmc->cpuevt.ignore = false;
	data.remote = false;

	walk_groups(&tmigr_new_timer_up, &data, tmc);

	/* If there is a new first global event, make sure it is handled */
	return data.firstexp;
}

static void tmigr_handle_remote_cpu(unsigned int cpu, u64 now,
				    unsigned long jif)
{
	struct timer_events tevt;
	struct tmigr_walk data;
	struct tmigr_cpu *tmc;

	tmc = per_cpu_ptr(&tmigr_cpu, cpu);

	raw_spin_lock_irq(&tmc->lock);

	/*
	 * If the remote CPU is offline then the timers have been migrated to
	 * another CPU.
	 *
	 * If tmigr_cpu::remote is set, at the moment another CPU already
	 * expires the timers of the remote CPU.
	 *
	 * If tmigr_event::ignore is set, then the CPU returns from idle and
	 * takes care of its timers.
	 *
	 * If the next event expires in the future, then the event has been
	 * updated and there are no timers to expire right now. The CPU which
	 * updated the event takes care when hierarchy is completely
	 * idle. Otherwise the migrator does it as the event is enqueued.
	 */
	if (!tmc->online || tmc->remote || tmc->cpuevt.ignore ||
	    now < tmc->cpuevt.nextevt.expires) {
		raw_spin_unlock_irq(&tmc->lock);
		return;
	}

	trace_tmigr_handle_remote_cpu(tmc);

	tmc->remote = true;
	WRITE_ONCE(tmc->wakeup, KTIME_MAX);

	/* Drop the lock to allow the remote CPU to exit idle */
	raw_spin_unlock_irq(&tmc->lock);

	if (cpu != smp_processor_id())
		timer_expire_remote(cpu);

	/*
	 * Lock ordering needs to be preserved - timer_base locks before tmigr
	 * related locks (see section "Locking rules" in the documentation at
	 * the top). During fetching the next timer interrupt, also tmc->lock
	 * needs to be held. Otherwise there is a possible race window against
	 * the CPU itself when it comes out of idle, updates the first timer in
	 * the hierarchy and goes back to idle.
	 *
	 * timer base locks are dropped as fast as possible: After checking
	 * whether the remote CPU went offline in the meantime and after
	 * fetching the next remote timer interrupt. Dropping the locks as fast
	 * as possible keeps the locking region small and prevents holding
	 * several (unnecessary) locks during walking the hierarchy for updating
	 * the timerqueue and group events.
	 */
	local_irq_disable();
	timer_lock_remote_bases(cpu);
	raw_spin_lock(&tmc->lock);

	/*
	 * When the CPU went offline in the meantime, no hierarchy walk has to
	 * be done for updating the queued events, because the walk was
	 * already done during marking the CPU offline in the hierarchy.
	 *
	 * When the CPU is no longer idle, the CPU takes care of the timers and
	 * also of the timers in the hierarchy.
	 *
	 * (See also section "Required event and timerqueue update after a
	 * remote expiry" in the documentation at the top)
	 */
	if (!tmc->online || !tmc->idle) {
		timer_unlock_remote_bases(cpu);
		goto unlock;
	}

	/* next	event of CPU */
	fetch_next_timer_interrupt_remote(jif, now, &tevt, cpu);
	timer_unlock_remote_bases(cpu);

	data.nextexp = tevt.global;
	data.firstexp = KTIME_MAX;
	data.evt = &tmc->cpuevt;
	data.remote = true;

	/*
	 * The update is done even when there is no 'new' global timer pending
	 * on the remote CPU (see section "Required event and timerqueue update
	 * after a remote expiry" in the documentation at the top)
	 */
	walk_groups(&tmigr_new_timer_up, &data, tmc);

unlock:
	tmc->remote = false;
	raw_spin_unlock_irq(&tmc->lock);
}

static bool tmigr_handle_remote_up(struct tmigr_group *group,
				   struct tmigr_group *child,
				   struct tmigr_walk *data)
{
	struct tmigr_event *evt;
	unsigned long jif;
	u8 childmask;
	u64 now;

	jif = data->basej;
	now = data->now;

	childmask = data->childmask;

	trace_tmigr_handle_remote(group);
again:
	/*
	 * Handle the group only if @childmask is the migrator or if the
	 * group has no migrator. Otherwise the group is active and is
	 * handled by its own migrator.
	 */
	if (!tmigr_check_migrator(group, childmask))
		return true;

	raw_spin_lock_irq(&group->lock);

	evt = tmigr_next_expired_groupevt(group, now);

	if (evt) {
		unsigned int remote_cpu = evt->cpu;

		raw_spin_unlock_irq(&group->lock);

		tmigr_handle_remote_cpu(remote_cpu, now, jif);

		/* check if there is another event, that needs to be handled */
		goto again;
	}

	/*
	 * Keep track of the expiry of the first event that needs to be handled
	 * (group->next_expiry was updated by tmigr_next_expired_groupevt(),
	 * next was set by tmigr_handle_remote_cpu()).
	 */
	data->firstexp = group->next_expiry;

	raw_spin_unlock_irq(&group->lock);

	return false;
}

/**
 * tmigr_handle_remote() - Handle global timers of remote idle CPUs
 *
 * Called from the timer soft interrupt with interrupts enabled.
 */
void tmigr_handle_remote(void)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);
	struct tmigr_walk data;

	if (tmigr_is_not_available(tmc))
		return;

	data.childmask = tmc->groupmask;
	data.firstexp = KTIME_MAX;

	/*
	 * NOTE: This is a doubled check because the migrator test will be done
	 * in tmigr_handle_remote_up() anyway. Keep this check to speed up the
	 * return when nothing has to be done.
	 */
	if (!tmigr_check_migrator(tmc->tmgroup, tmc->groupmask)) {
		/*
		 * If this CPU was an idle migrator, make sure to clear its wakeup
		 * value so it won't chase timers that have already expired elsewhere.
		 * This avoids endless requeue from tmigr_new_timer().
		 */
		if (READ_ONCE(tmc->wakeup) == KTIME_MAX)
			return;
	}

	data.now = get_jiffies_update(&data.basej);

	/*
	 * Update @tmc->wakeup only at the end and do not reset @tmc->wakeup to
	 * KTIME_MAX. Even if tmc->lock is not held during the whole remote
	 * handling, tmc->wakeup is fine to be stale as it is called in
	 * interrupt context and tick_nohz_next_event() is executed in interrupt
	 * exit path only after processing the last pending interrupt.
	 */

	__walk_groups(&tmigr_handle_remote_up, &data, tmc);

	raw_spin_lock_irq(&tmc->lock);
	WRITE_ONCE(tmc->wakeup, data.firstexp);
	raw_spin_unlock_irq(&tmc->lock);
}

static bool tmigr_requires_handle_remote_up(struct tmigr_group *group,
					    struct tmigr_group *child,
					    struct tmigr_walk *data)
{
	u8 childmask;

	childmask = data->childmask;

	/*
	 * Handle the group only if the child is the migrator or if the group
	 * has no migrator. Otherwise the group is active and is handled by its
	 * own migrator.
	 */
	if (!tmigr_check_migrator(group, childmask))
		return true;

	/*
	 * When there is a parent group and the CPU which triggered the
	 * hierarchy walk is not active, proceed the walk to reach the top level
	 * group before reading the next_expiry value.
	 */
	if (group->parent && !data->tmc_active)
		return false;

	/*
	 * The lock is required on 32bit architectures to read the variable
	 * consistently with a concurrent writer. On 64bit the lock is not
	 * required because the read operation is not split and so it is always
	 * consistent.
	 */
	if (IS_ENABLED(CONFIG_64BIT)) {
		data->firstexp = READ_ONCE(group->next_expiry);
		if (data->now >= data->firstexp) {
			data->check = true;
			return true;
		}
	} else {
		raw_spin_lock(&group->lock);
		data->firstexp = group->next_expiry;
		if (data->now >= group->next_expiry) {
			data->check = true;
			raw_spin_unlock(&group->lock);
			return true;
		}
		raw_spin_unlock(&group->lock);
	}

	return false;
}

/**
 * tmigr_requires_handle_remote() - Check the need of remote timer handling
 *
 * Must be called with interrupts disabled.
 */
bool tmigr_requires_handle_remote(void)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);
	struct tmigr_walk data;
	unsigned long jif;
	bool ret = false;

	if (tmigr_is_not_available(tmc))
		return ret;

	data.now = get_jiffies_update(&jif);
	data.childmask = tmc->groupmask;
	data.firstexp = KTIME_MAX;
	data.tmc_active = !tmc->idle;
	data.check = false;

	/*
	 * If the CPU is active, walk the hierarchy to check whether a remote
	 * expiry is required.
	 *
	 * Check is done lockless as interrupts are disabled and @tmc->idle is
	 * set only by the local CPU.
	 */
	if (!tmc->idle) {
		__walk_groups(&tmigr_requires_handle_remote_up, &data, tmc);

		return data.check;
	}

	/*
	 * When the CPU is idle, compare @tmc->wakeup with @data.now. The lock
	 * is required on 32bit architectures to read the variable consistently
	 * with a concurrent writer. On 64bit the lock is not required because
	 * the read operation is not split and so it is always consistent.
	 */
	if (IS_ENABLED(CONFIG_64BIT)) {
		if (data.now >= READ_ONCE(tmc->wakeup))
			return true;
	} else {
		raw_spin_lock(&tmc->lock);
		if (data.now >= tmc->wakeup)
			ret = true;
		raw_spin_unlock(&tmc->lock);
	}

	return ret;
}

/**
 * tmigr_cpu_new_timer() - enqueue next global timer into hierarchy (idle tmc)
 * @nextexp:	Next expiry of global timer (or KTIME_MAX if not)
 *
 * The CPU is already deactivated in the timer migration
 * hierarchy. tick_nohz_get_sleep_length() calls tick_nohz_next_event()
 * and thereby the timer idle path is executed once more. @tmc->wakeup
 * holds the first timer, when the timer migration hierarchy is
 * completely idle.
 *
 * Returns the first timer that needs to be handled by this CPU or KTIME_MAX if
 * nothing needs to be done.
 */
u64 tmigr_cpu_new_timer(u64 nextexp)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);
	u64 ret;

	if (tmigr_is_not_available(tmc))
		return nextexp;

	raw_spin_lock(&tmc->lock);

	ret = READ_ONCE(tmc->wakeup);
	if (nextexp != KTIME_MAX) {
		if (nextexp != tmc->cpuevt.nextevt.expires ||
		    tmc->cpuevt.ignore) {
			ret = tmigr_new_timer(tmc, nextexp);
			/*
			 * Make sure the reevaluation of timers in idle path
			 * will not miss an event.
			 */
			WRITE_ONCE(tmc->wakeup, ret);
		}
	}
	trace_tmigr_cpu_new_timer_idle(tmc, nextexp);
	raw_spin_unlock(&tmc->lock);
	return ret;
}

static bool tmigr_inactive_up(struct tmigr_group *group,
			      struct tmigr_group *child,
			      struct tmigr_walk *data)
{
	union tmigr_state curstate, newstate, childstate;
	bool walk_done;
	u8 childmask;

	childmask = data->childmask;
	childstate.state = 0;

	/*
	 * The memory barrier is paired with the cmpxchg() in tmigr_active_up()
	 * to make sure the updates of child and group states are ordered. The
	 * ordering is mandatory, as the group state change depends on the child
	 * state.
	 */
	curstate.state = atomic_read_acquire(&group->migr_state);

	for (;;) {
		if (child)
			childstate.state = atomic_read(&child->migr_state);

		newstate = curstate;
		walk_done = true;

		/* Reset active bit when the child is no longer active */
		if (!childstate.active)
			newstate.active &= ~childmask;

		if (newstate.migrator == childmask) {
			/*
			 * Find a new migrator for the group, because the child
			 * group is idle!
			 */
			if (!childstate.active) {
				unsigned long new_migr_bit, active = newstate.active;

				new_migr_bit = find_first_bit(&active, BIT_CNT);

				if (new_migr_bit != BIT_CNT) {
					newstate.migrator = BIT(new_migr_bit);
				} else {
					newstate.migrator = TMIGR_NONE;

					/* Changes need to be propagated */
					walk_done = false;
				}
			}
		}

		newstate.seq++;

		WARN_ON_ONCE((newstate.migrator != TMIGR_NONE) && !(newstate.active));

		if (atomic_try_cmpxchg(&group->migr_state, &curstate.state, newstate.state)) {
			trace_tmigr_group_set_cpu_inactive(group, newstate, childmask);
			break;
		}

		/*
		 * The memory barrier is paired with the cmpxchg() in
		 * tmigr_active_up() to make sure the updates of child and group
		 * states are ordered. It is required only when the above
		 * try_cmpxchg() fails.
		 */
		smp_mb__after_atomic();
	}

	data->remote = false;

	/* Event Handling */
	tmigr_update_events(group, child, data);

	return walk_done;
}

static u64 __tmigr_cpu_deactivate(struct tmigr_cpu *tmc, u64 nextexp)
{
	struct tmigr_walk data = { .nextexp = nextexp,
				   .firstexp = KTIME_MAX,
				   .evt = &tmc->cpuevt,
				   .childmask = tmc->groupmask };

	/*
	 * If nextexp is KTIME_MAX, the CPU event will be ignored because the
	 * local timer expires before the global timer, no global timer is set
	 * or CPU goes offline.
	 */
	if (nextexp != KTIME_MAX)
		tmc->cpuevt.ignore = false;

	walk_groups(&tmigr_inactive_up, &data, tmc);
	return data.firstexp;
}

/**
 * tmigr_cpu_deactivate() - Put current CPU into inactive state
 * @nextexp:	The next global timer expiry of the current CPU
 *
 * Must be called with interrupts disabled.
 *
 * Return: the next event expiry of the current CPU or the next event expiry
 * from the hierarchy if this CPU is the top level migrator or the hierarchy is
 * completely idle.
 */
u64 tmigr_cpu_deactivate(u64 nextexp)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);
	u64 ret;

	if (tmigr_is_not_available(tmc))
		return nextexp;

	raw_spin_lock(&tmc->lock);

	ret = __tmigr_cpu_deactivate(tmc, nextexp);

	tmc->idle = true;

	/*
	 * Make sure the reevaluation of timers in idle path will not miss an
	 * event.
	 */
	WRITE_ONCE(tmc->wakeup, ret);

	trace_tmigr_cpu_idle(tmc, nextexp);
	raw_spin_unlock(&tmc->lock);
	return ret;
}

/**
 * tmigr_quick_check() - Quick forecast of next tmigr event when CPU wants to
 *			 go idle
 * @nextevt:	The next global timer expiry of the current CPU
 *
 * Return:
 * * KTIME_MAX		- when it is probable that nothing has to be done (not
 *			  the only one in the level 0 group; and if it is the
 *			  only one in level 0 group, but there are more than a
 *			  single group active on the way to top level)
 * * nextevt		- when CPU is offline and has to handle timer on its own
 *			  or when on the way to top in every group only a single
 *			  child is active but @nextevt is before the lowest
 *			  next_expiry encountered while walking up to top level.
 * * next_expiry	- value of lowest expiry encountered while walking groups
 *			  if only a single child is active on each and @nextevt
 *			  is after this lowest expiry.
 */
u64 tmigr_quick_check(u64 nextevt)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);
	struct tmigr_group *group = tmc->tmgroup;

	if (tmigr_is_not_available(tmc))
		return nextevt;

	if (WARN_ON_ONCE(tmc->idle))
		return nextevt;

	if (!tmigr_check_migrator_and_lonely(tmc->tmgroup, tmc->groupmask))
		return KTIME_MAX;

	do {
		if (!tmigr_check_lonely(group))
			return KTIME_MAX;

		/*
		 * Since current CPU is active, events may not be sorted
		 * from bottom to the top because the CPU's event is ignored
		 * up to the top and its sibling's events not propagated upwards.
		 * Thus keep track of the lowest observed expiry.
		 */
		nextevt = min_t(u64, nextevt, READ_ONCE(group->next_expiry));
		group = group->parent;
	} while (group);

	return nextevt;
}

/*
 * tmigr_trigger_active() - trigger a CPU to become active again
 *
 * This function is executed on a CPU which is part of cpu_online_mask, when the
 * last active CPU in the hierarchy is offlining. With this, it is ensured that
 * the other CPU is active and takes over the migrator duty.
 */
static long tmigr_trigger_active(void *unused)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);

	WARN_ON_ONCE(!tmc->online || tmc->idle);

	return 0;
}

static int tmigr_cpu_offline(unsigned int cpu)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);
	int migrator;
	u64 firstexp;

	raw_spin_lock_irq(&tmc->lock);
	tmc->online = false;
	WRITE_ONCE(tmc->wakeup, KTIME_MAX);

	/*
	 * CPU has to handle the local events on his own, when on the way to
	 * offline; Therefore nextevt value is set to KTIME_MAX
	 */
	firstexp = __tmigr_cpu_deactivate(tmc, KTIME_MAX);
	trace_tmigr_cpu_offline(tmc);
	raw_spin_unlock_irq(&tmc->lock);

	if (firstexp != KTIME_MAX) {
		migrator = cpumask_any_but(cpu_online_mask, cpu);
		work_on_cpu(migrator, tmigr_trigger_active, NULL);
	}

	return 0;
}

static int tmigr_cpu_online(unsigned int cpu)
{
	struct tmigr_cpu *tmc = this_cpu_ptr(&tmigr_cpu);

	/* Check whether CPU data was successfully initialized */
	if (WARN_ON_ONCE(!tmc->tmgroup))
		return -EINVAL;

	raw_spin_lock_irq(&tmc->lock);
	trace_tmigr_cpu_online(tmc);
	tmc->idle = timer_base_is_idle();
	if (!tmc->idle)
		__tmigr_cpu_activate(tmc);
	tmc->online = true;
	raw_spin_unlock_irq(&tmc->lock);
	return 0;
}

static void tmigr_init_group(struct tmigr_group *group, unsigned int lvl,
			     int node)
{
	union tmigr_state s;

	raw_spin_lock_init(&group->lock);

	group->level = lvl;
	group->numa_node = lvl < tmigr_crossnode_level ? node : NUMA_NO_NODE;

	group->num_children = 0;

	s.migrator = TMIGR_NONE;
	s.active = 0;
	s.seq = 0;
	atomic_set(&group->migr_state, s.state);

	/*
	 * If this is a new top-level, prepare its groupmask in advance.
	 * This avoids accidents where yet another new top-level is
	 * created in the future and made visible before the current groupmask.
	 */
	if (list_empty(&tmigr_level_list[lvl])) {
		group->groupmask = BIT(0);
		/*
		 * The previous top level has prepared its groupmask already,
		 * simply account it as the first child.
		 */
		if (lvl > 0)
			group->num_children = 1;
	}

	timerqueue_init_head(&group->events);
	timerqueue_init(&group->groupevt.nextevt);
	group->groupevt.nextevt.expires = KTIME_MAX;
	WRITE_ONCE(group->next_expiry, KTIME_MAX);
	group->groupevt.ignore = true;
}

static struct tmigr_group *tmigr_get_group(unsigned int cpu, int node,
					   unsigned int lvl)
{
	struct tmigr_group *tmp, *group = NULL;

	lockdep_assert_held(&tmigr_mutex);

	/* Try to attach to an existing group first */
	list_for_each_entry(tmp, &tmigr_level_list[lvl], list) {
		/*
		 * If @lvl is below the cross NUMA node level, check whether
		 * this group belongs to the same NUMA node.
		 */
		if (lvl < tmigr_crossnode_level && tmp->numa_node != node)
			continue;

		/* Capacity left? */
		if (tmp->num_children >= TMIGR_CHILDREN_PER_GROUP)
			continue;

		/*
		 * TODO: A possible further improvement: Make sure that all CPU
		 * siblings end up in the same group of the lowest level of the
		 * hierarchy. Rely on the topology sibling mask would be a
		 * reasonable solution.
		 */

		group = tmp;
		break;
	}

	if (group)
		return group;

	/* Allocate and	set up a new group */
	group = kzalloc_node(sizeof(*group), GFP_KERNEL, node);
	if (!group)
		return ERR_PTR(-ENOMEM);

	tmigr_init_group(group, lvl, node);

	/* Setup successful. Add it to the hierarchy */
	list_add(&group->list, &tmigr_level_list[lvl]);
	trace_tmigr_group_set(group);
	return group;
}

static void tmigr_connect_child_parent(struct tmigr_group *child,
				       struct tmigr_group *parent,
				       bool activate)
{
	struct tmigr_walk data;

	raw_spin_lock_irq(&child->lock);
	raw_spin_lock_nested(&parent->lock, SINGLE_DEPTH_NESTING);

	if (activate) {
		/*
		 * @child is the old top and @parent the new one. In this
		 * case groupmask is pre-initialized and @child already
		 * accounted, along with its new sibling corresponding to the
		 * CPU going up.
		 */
		WARN_ON_ONCE(child->groupmask != BIT(0) || parent->num_children != 2);
	} else {
		/* Adding @child for the CPU going up to @parent. */
		child->groupmask = BIT(parent->num_children++);
	}

	/*
	 * Make sure parent initialization is visible before publishing it to a
	 * racing CPU entering/exiting idle. This RELEASE barrier enforces an
	 * address dependency that pairs with the READ_ONCE() in __walk_groups().
	 */
	smp_store_release(&child->parent, parent);

	raw_spin_unlock(&parent->lock);
	raw_spin_unlock_irq(&child->lock);

	trace_tmigr_connect_child_parent(child);

	if (!activate)
		return;

	/*
	 * To prevent inconsistent states, active children need to be active in
	 * the new parent as well. Inactive children are already marked inactive
	 * in the parent group:
	 *
	 * * When new groups were created by tmigr_setup_groups() starting from
	 *   the lowest level (and not higher then one level below the current
	 *   top level), then they are not active. They will be set active when
	 *   the new online CPU comes active.
	 *
	 * * But if a new group above the current top level is required, it is
	 *   mandatory to propagate the active state of the already existing
	 *   child to the new parent. So tmigr_connect_child_parent() is
	 *   executed with the formerly top level group (child) and the newly
	 *   created group (parent).
	 *
	 * * It is ensured that the child is active, as this setup path is
	 *   executed in hotplug prepare callback. This is exectued by an
	 *   already connected and !idle CPU. Even if all other CPUs go idle,
	 *   the CPU executing the setup will be responsible up to current top
	 *   level group. And the next time it goes inactive, it will release
	 *   the new childmask and parent to subsequent walkers through this
	 *   @child. Therefore propagate active state unconditionally.
	 */
	data.childmask = child->groupmask;

	/*
	 * There is only one new level per time (which is protected by
	 * tmigr_mutex). When connecting the child and the parent and set the
	 * child active when the parent is inactive, the parent needs to be the
	 * uppermost level. Otherwise there went something wrong!
	 */
	WARN_ON(!tmigr_active_up(parent, child, &data) && parent->parent);
}

static int tmigr_setup_groups(unsigned int cpu, unsigned int node)
{
	struct tmigr_group *group, *child, **stack;
	int top = 0, err = 0, i = 0;
	struct list_head *lvllist;

	stack = kcalloc(tmigr_hierarchy_levels, sizeof(*stack), GFP_KERNEL);
	if (!stack)
		return -ENOMEM;

	do {
		group = tmigr_get_group(cpu, node, i);
		if (IS_ERR(group)) {
			err = PTR_ERR(group);
			break;
		}

		top = i;
		stack[i++] = group;

		/*
		 * When booting only less CPUs of a system than CPUs are
		 * available, not all calculated hierarchy levels are required.
		 *
		 * The loop is aborted as soon as the highest level, which might
		 * be different from tmigr_hierarchy_levels, contains only a
		 * single group.
		 */
		if (group->parent || list_is_singular(&tmigr_level_list[i - 1]))
			break;

	} while (i < tmigr_hierarchy_levels);

	/* Assert single root */
	WARN_ON_ONCE(!err && !group->parent && !list_is_singular(&tmigr_level_list[top]));

	while (i > 0) {
		group = stack[--i];

		if (err < 0) {
			list_del(&group->list);
			kfree(group);
			continue;
		}

		WARN_ON_ONCE(i != group->level);

		/*
		 * Update tmc -> group / child -> group connection
		 */
		if (i == 0) {
			struct tmigr_cpu *tmc = per_cpu_ptr(&tmigr_cpu, cpu);

			raw_spin_lock_irq(&group->lock);

			tmc->tmgroup = group;
			tmc->groupmask = BIT(group->num_children++);

			raw_spin_unlock_irq(&group->lock);

			trace_tmigr_connect_cpu_parent(tmc);

			/* There are no children that need to be connected */
			continue;
		} else {
			child = stack[i - 1];
			/* Will be activated at online time */
			tmigr_connect_child_parent(child, group, false);
		}

		/* check if uppermost level was newly created */
		if (top != i)
			continue;

		WARN_ON_ONCE(top == 0);

		lvllist = &tmigr_level_list[top];

		/*
		 * Newly created root level should have accounted the upcoming
		 * CPU's child group and pre-accounted the old root.
		 */
		if (group->num_children == 2 && list_is_singular(lvllist)) {
			/*
			 * The target CPU must never do the prepare work, except
			 * on early boot when the boot CPU is the target. Otherwise
			 * it may spuriously activate the old top level group inside
			 * the new one (nevertheless whether old top level group is
			 * active or not) and/or release an uninitialized childmask.
			 */
			WARN_ON_ONCE(cpu == raw_smp_processor_id());

			lvllist = &tmigr_level_list[top - 1];
			list_for_each_entry(child, lvllist, list) {
				if (child->parent)
					continue;

				tmigr_connect_child_parent(child, group, true);
			}
		}
	}

	kfree(stack);

	return err;
}

static int tmigr_add_cpu(unsigned int cpu)
{
	int node = cpu_to_node(cpu);
	int ret;

	mutex_lock(&tmigr_mutex);
	ret = tmigr_setup_groups(cpu, node);
	mutex_unlock(&tmigr_mutex);

	return ret;
}

static int tmigr_cpu_prepare(unsigned int cpu)
{
	struct tmigr_cpu *tmc = per_cpu_ptr(&tmigr_cpu, cpu);
	int ret = 0;

	/* Not first online attempt? */
	if (tmc->tmgroup)
		return ret;

	raw_spin_lock_init(&tmc->lock);
	timerqueue_init(&tmc->cpuevt.nextevt);
	tmc->cpuevt.nextevt.expires = KTIME_MAX;
	tmc->cpuevt.ignore = true;
	tmc->cpuevt.cpu = cpu;
	tmc->remote = false;
	WRITE_ONCE(tmc->wakeup, KTIME_MAX);

	ret = tmigr_add_cpu(cpu);
	if (ret < 0)
		return ret;

	if (tmc->groupmask == 0)
		return -EINVAL;

	return ret;
}

static int __init tmigr_init(void)
{
	unsigned int cpulvl, nodelvl, cpus_per_node, i;
	unsigned int nnodes = num_possible_nodes();
	unsigned int ncpus = num_possible_cpus();
	int ret = -ENOMEM;

	BUILD_BUG_ON_NOT_POWER_OF_2(TMIGR_CHILDREN_PER_GROUP);

	/* Nothing to do if running on UP */
	if (ncpus == 1)
		return 0;

	/*
	 * Calculate the required hierarchy levels. Unfortunately there is no
	 * reliable information available, unless all possible CPUs have been
	 * brought up and all NUMA nodes are populated.
	 *
	 * Estimate the number of levels with the number of possible nodes and
	 * the number of possible CPUs. Assume CPUs are spread evenly across
	 * nodes. We cannot rely on cpumask_of_node() because it only works for
	 * online CPUs.
	 */
	cpus_per_node = DIV_ROUND_UP(ncpus, nnodes);

	/* Calc the hierarchy levels required to hold the CPUs of a node */
	cpulvl = DIV_ROUND_UP(order_base_2(cpus_per_node),
			      ilog2(TMIGR_CHILDREN_PER_GROUP));

	/* Calculate the extra levels to connect all nodes */
	nodelvl = DIV_ROUND_UP(order_base_2(nnodes),
			       ilog2(TMIGR_CHILDREN_PER_GROUP));

	tmigr_hierarchy_levels = cpulvl + nodelvl;

	/*
	 * If a NUMA node spawns more than one CPU level group then the next
	 * level(s) of the hierarchy contains groups which handle all CPU groups
	 * of the same NUMA node. The level above goes across NUMA nodes. Store
	 * this information for the setup code to decide in which level node
	 * matching is no longer required.
	 */
	tmigr_crossnode_level = cpulvl;

	tmigr_level_list = kcalloc(tmigr_hierarchy_levels, sizeof(struct list_head), GFP_KERNEL);
	if (!tmigr_level_list)
		goto err;

	for (i = 0; i < tmigr_hierarchy_levels; i++)
		INIT_LIST_HEAD(&tmigr_level_list[i]);

	pr_info("Timer migration: %d hierarchy levels; %d children per group;"
		" %d crossnode level\n",
		tmigr_hierarchy_levels, TMIGR_CHILDREN_PER_GROUP,
		tmigr_crossnode_level);

	ret = cpuhp_setup_state(CPUHP_TMIGR_PREPARE, "tmigr:prepare",
				tmigr_cpu_prepare, NULL);
	if (ret)
		goto err;

	ret = cpuhp_setup_state(CPUHP_AP_TMIGR_ONLINE, "tmigr:online",
				tmigr_cpu_online, tmigr_cpu_offline);
	if (ret)
		goto err;

	return 0;

err:
	pr_err("Timer migration setup failed\n");
	return ret;
}
early_initcall(tmigr_init);
