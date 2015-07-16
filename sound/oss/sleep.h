#include <linux/wait.h>

/*
 * Do not use. This is a replacement for the old
 * "interruptible_sleep_on_timeout" function that has been
 * deprecated for ages. All users should instead try to use
 * wait_event_interruptible_timeout.
 */

static inline long
oss_broken_sleep_on(wait_queue_head_t *q, long timeout)
{
	DEFINE_WAIT(wait);
	prepare_to_wait(q, &wait, TASK_INTERRUPTIBLE);
	timeout = schedule_timeout(timeout);
	finish_wait(q, &wait);
	return timeout;
}
