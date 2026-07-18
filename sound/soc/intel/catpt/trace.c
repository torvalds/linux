// SPDX-License-Identifier: GPL-2.0-only

#include <linux/types.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

#define BYTES_PER_LINE 16
#define MAX_CHUNK_SIZE ((PAGE_SIZE - 150) /* Place for trace header */	\
			/ (2 * BYTES_PER_LINE + 4) /* chars per line */	\
			* BYTES_PER_LINE)

void trace_catpt_ipc_payload(const void *data, size_t size)
{
	size_t remaining = size;
	size_t offset = 0;

	while (remaining > 0) {
		u32 chunk;

		chunk = min_t(size_t, remaining, MAX_CHUNK_SIZE);
		trace_catpt_ipc_payload_chunk(data, chunk, offset, size);

		remaining -= chunk;
		offset += chunk;
	}
}
