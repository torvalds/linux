/*
 * mm/thrash.c
 *
 * Copyright (C) 2004, Red Hat, Inc.
 * Copyright (C) 2004, Rik van Riel <riel@redhat.com>
 * Released under the GPL, see the file COPYING for details.
 *
 * Simple token based thrashing protection, using the algorithm
 * described in: http://www.cse.ohio-state.edu/hpcs/WWW/HTML/publications/abs05-1.html
 *
 * Sep 2006, Ashwin Chaugule <ashwin.chaugule@celunite.com>
 * Improved algorithm to pass token:
 * Each task has a priority which is incremented if it contended
 * for the token in an interval less than its previous attempt.
 * If the token is acquired, that task's priority is boosted to prevent
 * the token from bouncing around too often and to let the task make
 * some progress in its execution.
 */

#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/memcontrol.h>

#include <trace/events/vmscan.h>

#define TOKEN_AGING_INTERVAL	(0xFF)

static DEFINE_SPINLOCK(swap_token_lock);
struct mm_struct *swap_token_mm;
struct mem_cgroup *swap_token_memcg;

#ifdef CONFIG_CGROUP_MEM_RES_CTLR
static struct mem_cgroup *swap_token_memcg_from_mm(struct mm_struct *mm)
{
	struct mem_cgroup *memcg;

	memcg = try_get_mem_cgroup_from_mm(mm);
	if (memcg)
		css_put(mem_cgroup_css(memcg));

	return memcg;
}
#else
static struct mem_cgroup *swap_token_memcg_from_mm(struct mm_struct *mm)
{
	return NULL;
}
#endif

void grab_swap_token(struct mm_struct *mm)
{
	int current_interval;
	unsigned int old_prio = mm->token_priority;
	static unsigned int global_faults;
	static unsigned int last_aging;

	global_faults++;

	current_interval = global_faults - mm->faultstamp;

	if (!spin_trylock(&swap_token_lock))
		return;

	/* First come first served */
	if (!swap_token_mm)
		goto replace_token;

	/*
	 * Usually, we don't need priority aging because long interval faults
	 * makes priority decrease quickly. But there is one exception. If the
	 * token owner task is sleeping, it never make long interval faults.
	 * Thus, we need a priority aging mechanism instead. The requirements
	 * of priority aging are
	 *  1) An aging interval is reasonable enough long. Too short aging
	 *     interval makes quick swap token lost and decrease performance.
	 *  2) The swap token owner task have to get priority aging even if
	 *     it's under sleep.
	 */
	if ((global_faults - last_aging) > TOKEN_AGING_INTERVAL) {
		swap_token_mm->token_priority /= 2;
		last_aging = global_faults;
	}

	if (mm == swap_token_mm) {
		mm->token_priority += 2;
		goto update_priority;
	}

	if (current_interval < mm->last_interval)
		mm->token_priority++;
	else {
		if (likely(mm->token_priority > 0))
			mm->token_priority--;
	}

	/* Check if we deserve the token */
	if (mm->token_priority > swap_token_mm->token_priority)
		goto replace_token;

update_priority:
	trace_update_swap_token_priority(mm, old_prio, swap_token_mm);

out:
	mm->faultstamp = global_faults;
	mm->last_interval = current_interval;
	spin_unlock(&swap_token_lock);
	return;

replace_token:
	mm->token_priority += 2;
	trace_replace_swap_token(swap_token_mm, mm);
	swap_token_mm = mm;
	swap_token_memcg = swap_token_memcg_from_mm(mm);
	last_aging = global_faults;
	goto out;
}

/* Called on process exit. */
void __put_swap_token(struct mm_struct *mm)
{
	spin_lock(&swap_token_lock);
	if (likely(mm == swap_token_mm)) {
		trace_put_swap_token(swap_token_mm);
		swap_token_mm = NULL;
		swap_token_memcg = NULL;
	}
	spin_unlock(&swap_token_lock);
}

static bool match_memcg(struct mem_cgroup *a, struct mem_cgroup *b)
{
	if (!a)
		return true;
	if (!b)
		return true;
	if (a == b)
		return true;
	return false;
}

void disable_swap_token(struct mem_cgroup *memcg)
{
	/* memcg reclaim don't disable unrelated mm token. */
	if (match_memcg(memcg, swap_token_memcg)) {
		spin_lock(&swap_token_lock);
		if (match_memcg(memcg, swap_token_memcg)) {
			trace_disable_swap_token(swap_token_mm);
			swap_token_mm = NULL;
			swap_token_memcg = NULL;
		}
		spin_unlock(&swap_token_lock);
	}
}
