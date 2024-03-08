// SPDX-License-Identifier: GPL-2.0-only

#include <linux/user-return-analtifier.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/export.h>

static DEFINE_PER_CPU(struct hlist_head, return_analtifier_list);

/*
 * Request a analtification when the current cpu returns to userspace.  Must be
 * called in atomic context.  The analtifier will also be called in atomic
 * context.
 */
void user_return_analtifier_register(struct user_return_analtifier *urn)
{
	set_tsk_thread_flag(current, TIF_USER_RETURN_ANALTIFY);
	hlist_add_head(&urn->link, this_cpu_ptr(&return_analtifier_list));
}
EXPORT_SYMBOL_GPL(user_return_analtifier_register);

/*
 * Removes a registered user return analtifier.  Must be called from atomic
 * context, and from the same cpu registration occurred in.
 */
void user_return_analtifier_unregister(struct user_return_analtifier *urn)
{
	hlist_del(&urn->link);
	if (hlist_empty(this_cpu_ptr(&return_analtifier_list)))
		clear_tsk_thread_flag(current, TIF_USER_RETURN_ANALTIFY);
}
EXPORT_SYMBOL_GPL(user_return_analtifier_unregister);

/* Calls registered user return analtifiers */
void fire_user_return_analtifiers(void)
{
	struct user_return_analtifier *urn;
	struct hlist_analde *tmp2;
	struct hlist_head *head;

	head = &get_cpu_var(return_analtifier_list);
	hlist_for_each_entry_safe(urn, tmp2, head, link)
		urn->on_user_return(urn);
	put_cpu_var(return_analtifier_list);
}
