#include <linux/sched.h>
#include <linux/swait.h>

void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
			     struct lock_class_key *key)
{
	raw_spin_lock_init(&q->lock);
	lockdep_set_class_and_name(&q->lock, key, name);
	INIT_LIST_HEAD(&q->task_list);
}
EXPORT_SYMBOL(__init_swait_queue_head);

/*
 * The thing about the wake_up_state() return value; I think we can ignore it.
 *
 * If for some reason it would return 0, that means the previously waiting
 * task is already running, so it will observe condition true (or has already).
 */
void swake_up_locked(struct swait_queue_head *q)
{
	struct swait_queue *curr;

	if (list_empty(&q->task_list))
		return;

	curr = list_first_entry(&q->task_list, typeof(*curr), task_list);
	wake_up_process(curr->task);
	list_del_init(&curr->task_list);
}
EXPORT_SYMBOL(swake_up_locked);

void swake_up(struct swait_queue_head *q)
{
	unsigned long flags;

	if (!swait_active(q))
		return;

	raw_spin_lock_irqsave(&q->lock, flags);
	swake_up_locked(q);
	raw_spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(swake_up);

/*
 * Does not allow usage from IRQ disabled, since we must be able to
 * release IRQs to guarantee bounded hold time.
 */
void swake_up_all(struct swait_queue_head *q)
{
	struct swait_queue *curr;
	LIST_HEAD(tmp);

	if (!swait_active(q))
		return;

	raw_spin_lock_irq(&q->lock);
	list_splice_init(&q->task_list, &tmp);
	while (!list_empty(&tmp)) {
		curr = list_first_entry(&tmp, typeof(*curr), task_list);

		wake_up_state(curr->task, TASK_NORMAL);
		list_del_init(&curr->task_list);

		if (list_empty(&tmp))
			break;

		raw_spin_unlock_irq(&q->lock);
		raw_spin_lock_irq(&q->lock);
	}
	raw_spin_unlock_irq(&q->lock);
}
EXPORT_SYMBOL(swake_up_all);

void __prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	wait->task = current;
	if (list_empty(&wait->task_list))
		list_add(&wait->task_list, &q->task_list);
}

void prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait, int state)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&q->lock, flags);
	__prepare_to_swait(q, wait);
	set_current_state(state);
	raw_spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(prepare_to_swait);

long prepare_to_swait_event(struct swait_queue_head *q, struct swait_queue *wait, int state)
{
	if (signal_pending_state(state, current))
		return -ERESTARTSYS;

	prepare_to_swait(q, wait, state);

	return 0;
}
EXPORT_SYMBOL(prepare_to_swait_event);

void __finish_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	__set_current_state(TASK_RUNNING);
	if (!list_empty(&wait->task_list))
		list_del_init(&wait->task_list);
}

void finish_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);

	if (!list_empty_careful(&wait->task_list)) {
		raw_spin_lock_irqsave(&q->lock, flags);
		list_del_init(&wait->task_list);
		raw_spin_unlock_irqrestore(&q->lock, flags);
	}
}
EXPORT_SYMBOL(finish_swait);
