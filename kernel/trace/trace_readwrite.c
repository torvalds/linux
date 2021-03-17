// SPDX-License-Identifier: GPL-2.0
/*
 * Register read and write tracepoints
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/log_mmiorw.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rwmmio.h>

#ifdef CONFIG_TRACE_MMIO_ACCESS
void __log_write_mmio(u64 val, u8 width, volatile void __iomem *addr)
{
	trace_rwmmio_write(CALLER_ADDR0, val, width, addr);
}
EXPORT_SYMBOL_GPL(__log_write_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_write);

void __log_read_mmio(u8 width, const volatile void __iomem *addr)
{
	trace_rwmmio_read(CALLER_ADDR0, width, addr);
}
EXPORT_SYMBOL_GPL(__log_read_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_read);

void __log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr)
{
	trace_rwmmio_post_read(CALLER_ADDR0, val, width, addr);
}
EXPORT_SYMBOL_GPL(__log_post_read_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwmmio_post_read);
#endif
