// SPDX-License-Identifier: GPL-2.0-only
/*
 * Register read and write tracepoints
 *
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/ftrace.h>
#include <linux/module.h>
#include <asm-generic/io.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rwmmio.h>

#ifdef CONFIG_TRACE_MMIO_ACCESS
void log_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
		    unsigned long caller_addr, unsigned long caller_addr0)
{
	trace_rwmmio_write(caller_addr, caller_addr0, val, width, addr);
}
EXPORT_SYMBOL_GPL(log_write_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_write);

void log_post_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
			 unsigned long caller_addr, unsigned long caller_addr0)
{
	trace_rwmmio_post_write(caller_addr, caller_addr0, val, width, addr);
}
EXPORT_SYMBOL_GPL(log_post_write_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_post_write);

void log_read_mmio(u8 width, const volatile void __iomem *addr,
		   unsigned long caller_addr, unsigned long caller_addr0)
{
	trace_rwmmio_read(caller_addr, caller_addr0, width, addr);
}
EXPORT_SYMBOL_GPL(log_read_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_read);

void log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr,
			unsigned long caller_addr, unsigned long caller_addr0)
{
	trace_rwmmio_post_read(caller_addr, caller_addr0, val, width, addr);
}
EXPORT_SYMBOL_GPL(log_post_read_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_post_read);
#endif /* CONFIG_TRACE_MMIO_ACCESS */
