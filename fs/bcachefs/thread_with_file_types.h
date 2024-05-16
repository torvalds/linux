/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_THREAD_WITH_FILE_TYPES_H
#define _BCACHEFS_THREAD_WITH_FILE_TYPES_H

#include "darray.h"

struct stdio_buf {
	spinlock_t		lock;
	wait_queue_head_t	wait;
	darray_char		buf;
};

struct stdio_redirect {
	struct stdio_buf	input;
	struct stdio_buf	output;

	spinlock_t		input_lock;
	wait_queue_head_t	input_wait;
	darray_char		input_buf;
	bool			done;
};

#endif /* _BCACHEFS_THREAD_WITH_FILE_TYPES_H */
