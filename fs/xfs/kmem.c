// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_message.h"
#include "xfs_trace.h"

void *
kmem_alloc(size_t size, xfs_km_flags_t flags)
{
	int	retries = 0;
	gfp_t	lflags = kmem_flags_convert(flags);
	void	*ptr;

	trace_kmem_alloc(size, flags, _RET_IP_);

	do {
		ptr = kmalloc(size, lflags);
		if (ptr || (flags & KM_MAYFAIL))
			return ptr;
		if (!(++retries % 100))
			xfs_err(NULL,
	"%s(%u) possible memory allocation deadlock size %u in %s (mode:0x%x)",
				current->comm, current->pid,
				(unsigned int)size, __func__, lflags);
		memalloc_retry_wait(lflags);
	} while (1);
}
