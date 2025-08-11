// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/poll.h>

void rust_helper_poll_wait(struct file *filp, wait_queue_head_t *wait_address,
			   poll_table *p)
{
	poll_wait(filp, wait_address, p);
}
