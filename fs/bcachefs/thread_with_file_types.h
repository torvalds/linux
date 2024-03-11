/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_THREAD_WITH_FILE_TYPES_H
#define _BCACHEFS_THREAD_WITH_FILE_TYPES_H

struct stdio_redirect {
	spinlock_t		output_lock;
	wait_queue_head_t	output_wait;
	struct printbuf		output_buf;

	spinlock_t		input_lock;
	wait_queue_head_t	input_wait;
	struct printbuf		input_buf;
	bool			done;
};

#endif /* _BCACHEFS_THREAD_WITH_FILE_TYPES_H */
