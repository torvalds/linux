/*
 * Memory mapped I/O tracing
 *
 * Copyright (C) 2008 Pekka Paalanen <pq@iki.fi>
 */

#define DEBUG 1

#include <linux/kernel.h>
#include <linux/mmiotrace.h>

#include "trace.h"

static struct trace_array *mmio_trace_array;

static void mmio_reset_data(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static void mmio_trace_init(struct trace_array *tr)
{
	pr_debug("in %s\n", __func__);
	mmio_trace_array = tr;
	if (tr->ctrl) {
		mmio_reset_data(tr);
		enable_mmiotrace();
	}
}

static void mmio_trace_reset(struct trace_array *tr)
{
	pr_debug("in %s\n", __func__);
	if (tr->ctrl)
		disable_mmiotrace();
	mmio_reset_data(tr);
	mmio_trace_array = NULL;
}

static void mmio_trace_ctrl_update(struct trace_array *tr)
{
	pr_debug("in %s\n", __func__);
	if (tr->ctrl) {
		mmio_reset_data(tr);
		enable_mmiotrace();
	} else {
		disable_mmiotrace();
	}
}

/* XXX: This is not called for trace_pipe file! */
void mmio_print_header(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	trace_seq_printf(s, "VERSION broken 20070824\n");
	/* TODO: print /proc/bus/pci/devices contents as PCIDEV lines */
}

static int mmio_print_rw(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct mmiotrace_rw *rw	= &entry->mmiorw;
	struct trace_seq *s	= &iter->seq;
	unsigned long long t	= ns2usecs(entry->t);
	unsigned long usec_rem	= do_div(t, 1000000ULL);
	unsigned secs		= (unsigned long)t;
	int ret = 1;

	switch (entry->mmiorw.opcode) {
	case MMIO_READ:
		ret = trace_seq_printf(s,
			"R %d %lu.%06lu %d 0x%lx 0x%lx 0x%lx %d\n",
			rw->width, secs, usec_rem, rw->map_id, rw->phys,
			rw->value, rw->pc, entry->pid);
		break;
	case MMIO_WRITE:
		ret = trace_seq_printf(s,
			"W %d %lu.%06lu %d 0x%lx 0x%lx 0x%lx %d\n",
			rw->width, secs, usec_rem, rw->map_id, rw->phys,
			rw->value, rw->pc, entry->pid);
		break;
	case MMIO_UNKNOWN_OP:
		ret = trace_seq_printf(s,
			"UNKNOWN %lu.%06lu %d 0x%lx %02x,%02x,%02x 0x%lx %d\n",
			secs, usec_rem, rw->map_id, rw->phys,
			(rw->value >> 16) & 0xff, (rw->value >> 8) & 0xff,
			(rw->value >> 0) & 0xff, rw->pc, entry->pid);
		break;
	default:
		ret = trace_seq_printf(s, "rw what?\n");
		break;
	}
	if (ret)
		return 1;
	return 0;
}

static int mmio_print_map(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct mmiotrace_map *m	= &entry->mmiomap;
	struct trace_seq *s	= &iter->seq;
	unsigned long long t	= ns2usecs(entry->t);
	unsigned long usec_rem	= do_div(t, 1000000ULL);
	unsigned secs		= (unsigned long)t;
	int ret = 1;

	switch (entry->mmiorw.opcode) {
	case MMIO_PROBE:
		ret = trace_seq_printf(s,
			"MAP %lu.%06lu %d 0x%lx 0x%lx 0x%lx 0x%lx %d\n",
			secs, usec_rem, m->map_id, m->phys, m->virt, m->len,
			0UL, entry->pid);
		break;
	case MMIO_UNPROBE:
		ret = trace_seq_printf(s,
			"UNMAP %lu.%06lu %d 0x%lx %d\n",
			secs, usec_rem, m->map_id, 0UL, entry->pid);
		break;
	default:
		ret = trace_seq_printf(s, "map what?\n");
		break;
	}
	if (ret)
		return 1;
	return 0;
}

/* return 0 to abort printing without consuming current entry in pipe mode */
static int mmio_print_line(struct trace_iterator *iter)
{
	switch (iter->ent->type) {
	case TRACE_MMIO_RW:
		return mmio_print_rw(iter);
	case TRACE_MMIO_MAP:
		return mmio_print_map(iter);
	default:
		return 1; /* ignore unknown entries */
	}
}

static struct tracer mmio_tracer __read_mostly =
{
	.name		= "mmiotrace",
	.init		= mmio_trace_init,
	.reset		= mmio_trace_reset,
	.open		= mmio_print_header,
	.ctrl_update	= mmio_trace_ctrl_update,
	.print_line	= mmio_print_line,
};

__init static int init_mmio_trace(void)
{
	return register_tracer(&mmio_tracer);
}
device_initcall(init_mmio_trace);

void mmio_trace_rw(struct mmiotrace_rw *rw)
{
	struct trace_array *tr = mmio_trace_array;
	struct trace_array_cpu *data = tr->data[smp_processor_id()];
	__trace_mmiotrace_rw(tr, data, rw);
}

void mmio_trace_mapping(struct mmiotrace_map *map)
{
	struct trace_array *tr = mmio_trace_array;
	struct trace_array_cpu *data;

	preempt_disable();
	data = tr->data[smp_processor_id()];
	__trace_mmiotrace_map(tr, data, map);
	preempt_enable();
}
