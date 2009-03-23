/*
 * Memory allocator tracing
 *
 * Copyright (C) 2008 Eduard - Gabriel Munteanu
 * Copyright (C) 2008 Pekka Enberg <penberg@cs.helsinki.fi>
 * Copyright (C) 2008 Frederic Weisbecker <fweisbec@gmail.com>
 */

#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/tracepoint.h>
#include <trace/kmemtrace.h>

#include "trace.h"
#include "trace_output.h"

/* Select an alternative, minimalistic output than the original one */
#define TRACE_KMEM_OPT_MINIMAL	0x1

static struct tracer_opt kmem_opts[] = {
	/* Default disable the minimalistic output */
	{ TRACER_OPT(kmem_minimalistic, TRACE_KMEM_OPT_MINIMAL) },
	{ }
};

static struct tracer_flags kmem_tracer_flags = {
	.val = 0,
	.opts = kmem_opts
};

static struct trace_array *kmemtrace_array;

/* Trace allocations */
static inline void kmemtrace_alloc(enum kmemtrace_type_id type_id,
				   unsigned long call_site,
				   const void *ptr,
				   size_t bytes_req,
				   size_t bytes_alloc,
				   gfp_t gfp_flags,
				   int node)
{
	struct ring_buffer_event *event;
	struct kmemtrace_alloc_entry *entry;
	struct trace_array *tr = kmemtrace_array;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry));
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, 0);

	entry->ent.type = TRACE_KMEM_ALLOC;
	entry->call_site = call_site;
	entry->ptr = ptr;
	entry->bytes_req = bytes_req;
	entry->bytes_alloc = bytes_alloc;
	entry->gfp_flags = gfp_flags;
	entry->node	=	node;

	ring_buffer_unlock_commit(tr->buffer, event);

	trace_wake_up();
}

static inline void kmemtrace_free(enum kmemtrace_type_id type_id,
				  unsigned long call_site,
				  const void *ptr)
{
	struct ring_buffer_event *event;
	struct kmemtrace_free_entry *entry;
	struct trace_array *tr = kmemtrace_array;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry));
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, 0);

	entry->ent.type = TRACE_KMEM_FREE;
	entry->type_id	= type_id;
	entry->call_site = call_site;
	entry->ptr = ptr;

	ring_buffer_unlock_commit(tr->buffer, event);

	trace_wake_up();
}

static void kmemtrace_kmalloc(unsigned long call_site,
			      const void *ptr,
			      size_t bytes_req,
			      size_t bytes_alloc,
			      gfp_t gfp_flags)
{
	kmemtrace_alloc(KMEMTRACE_TYPE_KMALLOC, call_site, ptr,
			bytes_req, bytes_alloc, gfp_flags, -1);
}

static void kmemtrace_kmem_cache_alloc(unsigned long call_site,
				       const void *ptr,
				       size_t bytes_req,
				       size_t bytes_alloc,
				       gfp_t gfp_flags)
{
	kmemtrace_alloc(KMEMTRACE_TYPE_CACHE, call_site, ptr,
			bytes_req, bytes_alloc, gfp_flags, -1);
}

static void kmemtrace_kmalloc_node(unsigned long call_site,
				   const void *ptr,
				   size_t bytes_req,
				   size_t bytes_alloc,
				   gfp_t gfp_flags,
				   int node)
{
	kmemtrace_alloc(KMEMTRACE_TYPE_KMALLOC, call_site, ptr,
			bytes_req, bytes_alloc, gfp_flags, node);
}

static void kmemtrace_kmem_cache_alloc_node(unsigned long call_site,
					    const void *ptr,
					    size_t bytes_req,
					    size_t bytes_alloc,
					    gfp_t gfp_flags,
					    int node)
{
	kmemtrace_alloc(KMEMTRACE_TYPE_CACHE, call_site, ptr,
			bytes_req, bytes_alloc, gfp_flags, node);
}

static void kmemtrace_kfree(unsigned long call_site, const void *ptr)
{
	kmemtrace_free(KMEMTRACE_TYPE_KMALLOC, call_site, ptr);
}

static void kmemtrace_kmem_cache_free(unsigned long call_site, const void *ptr)
{
	kmemtrace_free(KMEMTRACE_TYPE_CACHE, call_site, ptr);
}

static int kmemtrace_start_probes(void)
{
	int err;

	err = register_trace_kmalloc(kmemtrace_kmalloc);
	if (err)
		return err;
	err = register_trace_kmem_cache_alloc(kmemtrace_kmem_cache_alloc);
	if (err)
		return err;
	err = register_trace_kmalloc_node(kmemtrace_kmalloc_node);
	if (err)
		return err;
	err = register_trace_kmem_cache_alloc_node(kmemtrace_kmem_cache_alloc_node);
	if (err)
		return err;
	err = register_trace_kfree(kmemtrace_kfree);
	if (err)
		return err;
	err = register_trace_kmem_cache_free(kmemtrace_kmem_cache_free);

	return err;
}

static void kmemtrace_stop_probes(void)
{
	unregister_trace_kmalloc(kmemtrace_kmalloc);
	unregister_trace_kmem_cache_alloc(kmemtrace_kmem_cache_alloc);
	unregister_trace_kmalloc_node(kmemtrace_kmalloc_node);
	unregister_trace_kmem_cache_alloc_node(kmemtrace_kmem_cache_alloc_node);
	unregister_trace_kfree(kmemtrace_kfree);
	unregister_trace_kmem_cache_free(kmemtrace_kmem_cache_free);
}

static int kmem_trace_init(struct trace_array *tr)
{
	int cpu;
	kmemtrace_array = tr;

	for_each_cpu_mask(cpu, cpu_possible_map)
		tracing_reset(tr, cpu);

	kmemtrace_start_probes();

	return 0;
}

static void kmem_trace_reset(struct trace_array *tr)
{
	kmemtrace_stop_probes();
}

static void kmemtrace_headers(struct seq_file *s)
{
	/* Don't need headers for the original kmemtrace output */
	if (!(kmem_tracer_flags.val & TRACE_KMEM_OPT_MINIMAL))
		return;

	seq_printf(s, "#\n");
	seq_printf(s, "# ALLOC  TYPE  REQ   GIVEN  FLAGS     "
			"      POINTER         NODE    CALLER\n");
	seq_printf(s, "# FREE   |      |     |       |       "
			"       |   |            |        |\n");
	seq_printf(s, "# |\n\n");
}

/*
 * The two following functions give the original output from kmemtrace,
 * or something close to....perhaps they need some missing things
 */
static enum print_line_t
kmemtrace_print_alloc_original(struct trace_iterator *iter,
				struct kmemtrace_alloc_entry *entry)
{
	struct trace_seq *s = &iter->seq;
	int ret;

	/* Taken from the old linux/kmemtrace.h */
	ret = trace_seq_printf(s, "type_id %d call_site %lu ptr %lu "
	  "bytes_req %lu bytes_alloc %lu gfp_flags %lu node %d\n",
	   entry->type_id, entry->call_site, (unsigned long) entry->ptr,
	   (unsigned long) entry->bytes_req, (unsigned long) entry->bytes_alloc,
	   (unsigned long) entry->gfp_flags, entry->node);

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
kmemtrace_print_free_original(struct trace_iterator *iter,
				struct kmemtrace_free_entry *entry)
{
	struct trace_seq *s = &iter->seq;
	int ret;

	/* Taken from the old linux/kmemtrace.h */
	ret = trace_seq_printf(s, "type_id %d call_site %lu ptr %lu\n",
	   entry->type_id, entry->call_site, (unsigned long) entry->ptr);

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}


/* The two other following provide a more minimalistic output */
static enum print_line_t
kmemtrace_print_alloc_compress(struct trace_iterator *iter,
					struct kmemtrace_alloc_entry *entry)
{
	struct trace_seq *s = &iter->seq;
	int ret;

	/* Alloc entry */
	ret = trace_seq_printf(s, "  +      ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Type */
	switch (entry->type_id) {
	case KMEMTRACE_TYPE_KMALLOC:
		ret = trace_seq_printf(s, "K   ");
		break;
	case KMEMTRACE_TYPE_CACHE:
		ret = trace_seq_printf(s, "C   ");
		break;
	case KMEMTRACE_TYPE_PAGES:
		ret = trace_seq_printf(s, "P   ");
		break;
	default:
		ret = trace_seq_printf(s, "?   ");
	}

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Requested */
	ret = trace_seq_printf(s, "%4zu   ", entry->bytes_req);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Allocated */
	ret = trace_seq_printf(s, "%4zu   ", entry->bytes_alloc);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Flags
	 * TODO: would be better to see the name of the GFP flag names
	 */
	ret = trace_seq_printf(s, "%08x   ", entry->gfp_flags);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Pointer to allocated */
	ret = trace_seq_printf(s, "0x%tx   ", (ptrdiff_t)entry->ptr);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Node */
	ret = trace_seq_printf(s, "%4d   ", entry->node);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Call site */
	ret = seq_print_ip_sym(s, entry->call_site, 0);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	if (!trace_seq_printf(s, "\n"))
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t
kmemtrace_print_free_compress(struct trace_iterator *iter,
				struct kmemtrace_free_entry *entry)
{
	struct trace_seq *s = &iter->seq;
	int ret;

	/* Free entry */
	ret = trace_seq_printf(s, "  -      ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Type */
	switch (entry->type_id) {
	case KMEMTRACE_TYPE_KMALLOC:
		ret = trace_seq_printf(s, "K     ");
		break;
	case KMEMTRACE_TYPE_CACHE:
		ret = trace_seq_printf(s, "C     ");
		break;
	case KMEMTRACE_TYPE_PAGES:
		ret = trace_seq_printf(s, "P     ");
		break;
	default:
		ret = trace_seq_printf(s, "?     ");
	}

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Skip requested/allocated/flags */
	ret = trace_seq_printf(s, "                       ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Pointer to allocated */
	ret = trace_seq_printf(s, "0x%tx   ", (ptrdiff_t)entry->ptr);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Skip node */
	ret = trace_seq_printf(s, "       ");
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	/* Call site */
	ret = seq_print_ip_sym(s, entry->call_site, 0);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	if (!trace_seq_printf(s, "\n"))
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t kmemtrace_print_line(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;

	switch (entry->type) {
	case TRACE_KMEM_ALLOC: {
		struct kmemtrace_alloc_entry *field;
		trace_assign_type(field, entry);
		if (kmem_tracer_flags.val & TRACE_KMEM_OPT_MINIMAL)
			return kmemtrace_print_alloc_compress(iter, field);
		else
			return kmemtrace_print_alloc_original(iter, field);
	}

	case TRACE_KMEM_FREE: {
		struct kmemtrace_free_entry *field;
		trace_assign_type(field, entry);
		if (kmem_tracer_flags.val & TRACE_KMEM_OPT_MINIMAL)
			return kmemtrace_print_free_compress(iter, field);
		else
			return kmemtrace_print_free_original(iter, field);
	}

	default:
		return TRACE_TYPE_UNHANDLED;
	}
}

static struct tracer kmem_tracer __read_mostly = {
	.name		= "kmemtrace",
	.init		= kmem_trace_init,
	.reset		= kmem_trace_reset,
	.print_line	= kmemtrace_print_line,
	.print_header = kmemtrace_headers,
	.flags		= &kmem_tracer_flags
};

void kmemtrace_init(void)
{
	/* earliest opportunity to start kmem tracing */
}

static int __init init_kmem_tracer(void)
{
	return register_tracer(&kmem_tracer);
}

device_initcall(init_kmem_tracer);
