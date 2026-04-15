/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_TRACE_REMOTE_H
#define _LINUX_TRACE_REMOTE_H

#include <linux/dcache.h>
#include <linux/ring_buffer.h>
#include <linux/trace_remote_event.h>

/**
 * struct trace_remote_callbacks - Callbacks used by Tracefs to control the remote
 * @init:		Called once the remote has been registered. Allows the
 *			caller to extend the Tracefs remote directory
 * @load_trace_buffer:  Called before Tracefs accesses the trace buffer for the first
 *			time. Must return a &trace_buffer_desc
 *			(most likely filled with trace_remote_alloc_buffer())
 * @unload_trace_buffer:
 *			Called once Tracefs has no use for the trace buffer
 *			(most likely call trace_remote_free_buffer())
 * @enable_tracing:	Called on Tracefs tracing_on. It is expected from the
 *			remote to allow writing.
 * @swap_reader_page:	Called when Tracefs consumes a new page from a
 *			ring-buffer. It is expected from the remote to isolate a
 * @reset:		Called on `echo 0 > trace`. It is expected from the
 *			remote to reset all ring-buffer pages.
 *			new reader-page from the @cpu ring-buffer.
 * @enable_event:	Called on events/event_name/enable. It is expected from
 *			the remote to allow the writing event @id.
 */
struct trace_remote_callbacks {
	int	(*init)(struct dentry *d, void *priv);
	struct trace_buffer_desc *(*load_trace_buffer)(unsigned long size, void *priv);
	void	(*unload_trace_buffer)(struct trace_buffer_desc *desc, void *priv);
	int	(*enable_tracing)(bool enable, void *priv);
	int	(*swap_reader_page)(unsigned int cpu, void *priv);
	int	(*reset)(unsigned int cpu, void *priv);
	int	(*enable_event)(unsigned short id, bool enable, void *priv);
};

int trace_remote_register(const char *name, struct trace_remote_callbacks *cbs, void *priv,
			  struct remote_event *events, size_t nr_events);

int trace_remote_alloc_buffer(struct trace_buffer_desc *desc, size_t desc_size, size_t buffer_size,
			      const struct cpumask *cpumask);

void trace_remote_free_buffer(struct trace_buffer_desc *desc);

#endif
