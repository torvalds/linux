/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIMPLE_RING_BUFFER_H
#define _LINUX_SIMPLE_RING_BUFFER_H

#include <linux/list.h>
#include <linux/ring_buffer.h>
#include <linux/ring_buffer_types.h>
#include <linux/types.h>

/*
 * Ideally those struct would stay private but the caller needs to know
 * the allocation size for simple_ring_buffer_init().
 */
struct simple_buffer_page {
	struct list_head	link;
	struct buffer_data_page	*page;
	u64			entries;
	u32			write;
	u32			id;
};

struct simple_rb_per_cpu {
	struct simple_buffer_page	*tail_page;
	struct simple_buffer_page	*reader_page;
	struct simple_buffer_page	*head_page;
	struct simple_buffer_page	*bpages;
	struct trace_buffer_meta	*meta;
	u32				nr_pages;

#define SIMPLE_RB_UNAVAILABLE	0
#define SIMPLE_RB_READY		1
#define SIMPLE_RB_WRITING	2
	u32				status;

	u64				last_overrun;
	u64				write_stamp;

	struct simple_rb_cbs		*cbs;
};

int simple_ring_buffer_init(struct simple_rb_per_cpu *cpu_buffer, struct simple_buffer_page *bpages,
			    const struct ring_buffer_desc *desc);

void simple_ring_buffer_unload(struct simple_rb_per_cpu *cpu_buffer);

void *simple_ring_buffer_reserve(struct simple_rb_per_cpu *cpu_buffer, unsigned long length,
				 u64 timestamp);

void simple_ring_buffer_commit(struct simple_rb_per_cpu *cpu_buffer);

int simple_ring_buffer_enable_tracing(struct simple_rb_per_cpu *cpu_buffer, bool enable);

int simple_ring_buffer_reset(struct simple_rb_per_cpu *cpu_buffer);

int simple_ring_buffer_swap_reader_page(struct simple_rb_per_cpu *cpu_buffer);

int simple_ring_buffer_init_mm(struct simple_rb_per_cpu *cpu_buffer,
			       struct simple_buffer_page *bpages,
			       const struct ring_buffer_desc *desc,
			       void *(*load_page)(unsigned long va),
			       void (*unload_page)(void *va));

void simple_ring_buffer_unload_mm(struct simple_rb_per_cpu *cpu_buffer,
				  void (*unload_page)(void *));
#endif
