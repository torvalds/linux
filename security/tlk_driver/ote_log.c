/*
 * Copyright (c) 2013-2014 NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>

#include <asm/page.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>

#include "ote_protocol.h"

#define LOGBUF_SIZE 8192

struct circular_buffer {
	uint32_t size; /* Indicates the total size of the buffer */
	uint32_t start; /* Starting point of valid data in buffer */
	uint32_t end; /* First character which is empty (can be written to) */
	uint32_t overflow; /* Indicator whether buffer has overwritten itself */
	char *buf;
};

#if defined(CONFIG_OTE_ENABLE_LOGGER)

static int ote_logging_enabled;
struct circular_buffer *cb;

/*
 * Initialize the shared buffer for TLK logging.
 * The shared buffer is allocated in DMA memory to get uncached memory
 * since TLK directly writes to the physical address of the shared buffer.
 * The structure is declared in DMA memory too since it's members will
 * also be updated by the TLK directly to their physical addresses.
 */
static int circ_buf_init(struct circular_buffer **cbptr)
{

	dma_addr_t tp;

	*cbptr = (struct circular_buffer *) dma_alloc_coherent(NULL,
			sizeof(struct circular_buffer), &tp, GFP_KERNEL);
	if (!*cbptr) {
		pr_err("%s: no memory avaiable for circular buffer struct\n",
			__func__);
		return -ENOMEM;
	}
	memset(*cbptr, 0, sizeof(struct circular_buffer));

	(*cbptr)->start = 0;
	(*cbptr)->end = 0;
	(*cbptr)->size = LOGBUF_SIZE;

	(*cbptr)->buf = (char *) dma_alloc_coherent(NULL, LOGBUF_SIZE,
			&tp, GFP_KERNEL);
	if (!(*cbptr)->buf) {
			pr_err("%s: no memory avaiable for shared buffer\n",
				__func__);
		/* Frees the memory allocated using dma_alloc_coherent */
			dma_free_coherent(NULL,
				sizeof(struct circular_buffer), cbptr, tp);
			return -ENOMEM;
	}
	memset((*cbptr)->buf, 0, LOGBUF_SIZE);

	(*cbptr)->overflow = 0;

	return 0;
}

/*
 * Copy the contents of the circular buffer into a char buffer in order.
 * This helps to treat the buffer like a string and use it to tokenize it
 * into lines, tag and display it.
 */
static int circ_buf_copy(struct circular_buffer *cb, char *text)
{
	if (cb->end == cb->start)
		return 0;

	if (cb->end > cb->start) {
		if (abs(cb->end - cb->start) > LOGBUF_SIZE) {
			pr_err("%s: cbuf pointers corrupted\n", __func__);
			return -EINVAL;
		}

		memcpy(text, cb->buf + cb->start, cb->end - cb->start);

	} else if (cb->start > cb->end) {
		if (abs(cb->end - cb->start) > LOGBUF_SIZE) {
			pr_err("%s: cbuf pointers corrupted\n", __func__);
			return -EINVAL;
		}

		memcpy(text, cb->buf + cb->start, cb->size - cb->start);
		memcpy(text + cb->size - cb->start, cb->buf, cb->end);

	}

	return 0;
}

/*
 * Function which prints TLK logs.
 * Tokenizes the TLK logs into lines, tags each line
 * and prints it out to kmsg file.
 */
void ote_print_logs(void)
{
	char *text = NULL;
	char *temp = NULL;
	char *buffer = NULL;

	if (!ote_logging_enabled)
		return;

	buffer = kzalloc(LOGBUF_SIZE, GFP_KERNEL);
	BUG_ON(!buffer);

	/* This detects if the buffer proved to be too small to hold the data.
	 * If buffer is not large enough, it overwrites it's oldest data,
	 * This warning serves to alert the user to possibly use a bigger buffer
	 */
	if (cb->overflow == 1) {
		pr_info("\n[TLK] **WARNING** TLK buffer overwritten.\n\n");
		cb->overflow = 0;
	}

	if (circ_buf_copy(cb, buffer) != 0) {
		kfree(buffer);
		return;
	}
	cb->buf[cb->end] = '\0';

	/* In case no delimiter was found,
	 * the token is taken to be the entire string *stringp,
	 * and *stringp is made NULL.
	 */
	text = buffer;
	temp = strsep(&text, "\n");
	while (temp != NULL) {
		if (strnlen(temp, LOGBUF_SIZE))
			pr_info("[TLK] %s\n", temp);
		temp = strsep(&text, "\n");
	}

	/* Indicate that buffer is empty */
	cb->start = cb->end;
	kfree(buffer);
}
#else
void ote_print_logs(void) {}
#endif

/*
 * Call function to initialize circular buffer.
 * An SMC is made to send the virtual address of the structure to
 * the secure OS.
 */
static int __init ote_logger_init(void)
{
	uintptr_t smc_args[MAX_EXT_SMC_ARGS];

#if defined(CONFIG_OTE_ENABLE_LOGGER)
	if (circ_buf_init(&cb) != 0)
		return -1;

	smc_args[0] = TE_SMC_INIT_LOGGER;
	smc_args[1] = (uintptr_t)cb;

	/* enable logging only if secure firmware supports it */
	if (!tlk_generic_smc(smc_args[0], smc_args[1], 0))
		ote_logging_enabled = 1;

	ote_print_logs();
#else
	smc_args[0] = TE_SMC_INIT_LOGGER;
	smc_args[1] = 0;
	tlk_generic_smc(smc_args[0], smc_args[1], 0);
#endif

	return 0;
}

arch_initcall(ote_logger_init);
