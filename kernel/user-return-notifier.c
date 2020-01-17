// SPDX-License-Identifier: GPL-2.0-only

#include <linux/user-return-yestifier.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/export.h>

static DEFINE_PER_CPU(struct hlist_head, return_yestifier_list);

/*
 * Request a yestification when the current cpu returns to userspace.  Must be
 * called in atomic context.  The yestifier will also be called in atomic
 * context.
 */
void user_return_yestifier_register(struct user_return_yestifier *urn)
{
	set_tsk_thread_flag(current, TIF_USER_RETURN_NOTIFY);
	hlist_add_head(&urn->link, this_cpu_ptr(&return_yestifier_list));
}
EXPORT_SYMBOL_GPL(user_return_yestifier_register);

/*
 * Removes a registered user return yestifier.  Must be called from atomic
 * context, and from the same cpu registration occurred in.
 */
void user_return_yestifier_unregister(struct user_return_yestifier *urn)
{
	hlist_del(&urn->link);
	if (hlist_empty(this_cpu_ptr(&return_yestifier_list)))
		clear_tsk_thread_flag(current, TIF_USER_RETURN_NOTIFY);
}
EXPORT_SYMBOL_GPL(user_return_yestifier_unregister);

/* Calls registered user return yestifiers */
void fire_user_return_yestifiers(void)
{
	struct user_return_yestifier *urn;
	struct hlist_yesde *tmp2;
	struct hlist_head *head;

	head = &get_cpu_var(return_yestifier_list);
	hlist_for_each_entry_safe(urn, tmp2, head, link)
		urn->on_user_return(urn);
	put_cpu_var(return_yestifier_list);
}
