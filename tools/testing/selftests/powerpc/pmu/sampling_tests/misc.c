// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <ctype.h>

#include "misc.h"

#define PAGE_SIZE               sysconf(_SC_PAGESIZE)

/*
 * Allocate mmap buffer of "mmap_pages" number of
 * pages.
 */
void *event_sample_buf_mmap(int fd, int mmap_pages)
{
	size_t page_size = sysconf(_SC_PAGESIZE);
	size_t mmap_size;
	void *buff;

	if (mmap_pages <= 0)
		return NULL;

	if (fd <= 0)
		return NULL;

	mmap_size =  page_size * (1 + mmap_pages);
	buff = mmap(NULL, mmap_size,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (buff == MAP_FAILED) {
		perror("mmap() failed.");
		return NULL;
	}
	return buff;
}

/*
 * Post process the mmap buffer.
 * - If sample_count != NULL then return count of total
 *   number of samples present in the mmap buffer.
 * - If sample_count == NULL then return the address
 *   of first sample from the mmap buffer
 */
void *__event_read_samples(void *sample_buff, size_t *size, u64 *sample_count)
{
	size_t page_size = sysconf(_SC_PAGESIZE);
	struct perf_event_header *header = sample_buff + page_size;
	struct perf_event_mmap_page *metadata_page = sample_buff;
	unsigned long data_head, data_tail;

	/*
	 * PERF_RECORD_SAMPLE:
	 * struct {
	 *     struct perf_event_header hdr;
	 *     u64 data[];
	 * };
	 */

	data_head = metadata_page->data_head;
	/* sync memory before reading sample */
	mb();
	data_tail = metadata_page->data_tail;

	/* Check for sample_count */
	if (sample_count)
		*sample_count = 0;

	while (1) {
		/*
		 * Reads the mmap data buffer by moving
		 * the data_tail to know the last read data.
		 * data_head points to head in data buffer.
		 * refer "struct perf_event_mmap_page" in
		 * "include/uapi/linux/perf_event.h".
		 */
		if (data_head - data_tail < sizeof(header))
			return NULL;

		data_tail += sizeof(header);
		if (header->type == PERF_RECORD_SAMPLE) {
			*size = (header->size - sizeof(header));
			if (!sample_count)
				return sample_buff + page_size + data_tail;
			data_tail += *size;
			*sample_count += 1;
		} else {
			*size = (header->size - sizeof(header));
			if ((metadata_page->data_tail + *size) > metadata_page->data_head)
				data_tail = metadata_page->data_head;
			else
				data_tail += *size;
		}
		header = (struct perf_event_header *)((void *)header + header->size);
	}
	return NULL;
}
