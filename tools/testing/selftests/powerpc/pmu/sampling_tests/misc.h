/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include "../event.h"

void *event_sample_buf_mmap(int fd, int mmap_pages);
void *__event_read_samples(void *sample_buff, size_t *size, u64 *sample_count);
