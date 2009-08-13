/*
 * ring buffer based tracer for analyzing per-socket skb sources
 *
 * Neil Horman <nhorman@tuxdriver.com>
 * Copyright (C) 2009
 *
 *
 */

#include <linux/init.h>
#include <linux/debugfs.h>
#include <trace/events/skb.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/netdevice.h>
#include <net/sock.h>

#include "trace.h"
#include "trace_output.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(skb_copy_datagram_iovec);

static struct trace_array *skb_trace;
static int __read_mostly trace_skb_source_enabled;

static void probe_skb_dequeue(const struct sk_buff *skb, int len)
{
	struct ring_buffer_event *event;
	struct trace_skb_event *entry;
	struct trace_array *tr = skb_trace;
	struct net_device *dev;

	if (!trace_skb_source_enabled)
		return;

	if (in_interrupt())
		return;

	event = trace_buffer_lock_reserve(tr, TRACE_SKB_SOURCE,
					  sizeof(*entry), 0, 0);
	if (!event)
		return;
	entry = ring_buffer_event_data(event);

	entry->event_data.pid = current->pid;
	entry->event_data.anid = page_to_nid(virt_to_page(skb->data));
	entry->event_data.cnid = cpu_to_node(smp_processor_id());
	entry->event_data.len = len;
	entry->event_data.rx_queue = skb->queue_mapping;
	entry->event_data.ccpu = smp_processor_id();

	dev = dev_get_by_index(sock_net(skb->sk), skb->iif);
	if (dev) {
		memcpy(entry->event_data.ifname, dev->name, IFNAMSIZ);
		dev_put(dev);
	} else {
		strcpy(entry->event_data.ifname, "Unknown");
	}

	trace_buffer_unlock_commit(tr, event, 0, 0);
}

static int tracing_skb_source_register(void)
{
	int ret;

	ret = register_trace_skb_copy_datagram_iovec(probe_skb_dequeue);
	if (ret)
		pr_info("skb source trace: Couldn't activate dequeue tracepoint");

	return ret;
}

static void start_skb_source_trace(struct trace_array *tr)
{
	trace_skb_source_enabled = 1;
}

static void stop_skb_source_trace(struct trace_array *tr)
{
	trace_skb_source_enabled = 0;
}

static void skb_source_trace_reset(struct trace_array *tr)
{
	trace_skb_source_enabled = 0;
	unregister_trace_skb_copy_datagram_iovec(probe_skb_dequeue);
}


static int skb_source_trace_init(struct trace_array *tr)
{
	int cpu;
	skb_trace = tr;

	trace_skb_source_enabled = 1;
	tracing_skb_source_register();

	for_each_cpu(cpu, cpu_possible_mask)
		tracing_reset(tr, cpu);
	return 0;
}

static enum print_line_t skb_source_print_line(struct trace_iterator *iter)
{
	int ret = 0;
	struct trace_entry *entry = iter->ent;
	struct trace_skb_event *event;
	struct skb_record *record;
	struct trace_seq *s = &iter->seq;

	trace_assign_type(event, entry);
	record = &event->event_data;
	if (entry->type != TRACE_SKB_SOURCE)
		return TRACE_TYPE_UNHANDLED;

	ret = trace_seq_printf(s, "	%d	%d	%d	%s	%d	%d	%d\n",
			record->pid,
			record->anid,
			record->cnid,
			record->ifname,
			record->rx_queue,
			record->ccpu,
			record->len);

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static void skb_source_print_header(struct seq_file *s)
{
	seq_puts(s, "#	PID	ANID	CNID	IFC	RXQ	CCPU	LEN\n");
	seq_puts(s, "#	 |	 |	 |	 |	 |	 |	 |\n");
}

static struct tracer skb_source_tracer __read_mostly =
{
	.name		= "skb_sources",
	.init		= skb_source_trace_init,
	.start		= start_skb_source_trace,
	.stop		= stop_skb_source_trace,
	.reset		= skb_source_trace_reset,
	.print_line	= skb_source_print_line,
	.print_header	= skb_source_print_header,
};

static int init_skb_source_trace(void)
{
	return register_tracer(&skb_source_tracer);
}
device_initcall(init_skb_source_trace);
