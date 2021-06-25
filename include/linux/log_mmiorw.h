/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __LOG_MMIORW_H__
#define __LOG_MMIORW_H__

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/tracepoint-defs.h>

/*
 * TODO - io.h is included in NVHE files and these tracepoints are getting
 * enabled for NVHE too. To avoid these tracepoints enabling in NHVE below
 * condition is introduced.
 * !(defined(__DISABLE_TRACE_MMIO__))
 */
#if IS_ENABLED(CONFIG_TRACE_MMIO_ACCESS) && !(defined(__DISABLE_TRACE_MMIO__))
DECLARE_TRACEPOINT(rwmmio_write);
DECLARE_TRACEPOINT(rwmmio_read);
DECLARE_TRACEPOINT(rwmmio_post_read);

void __log_write_mmio(u64 val, u8 width, volatile void __iomem *addr);
void __log_read_mmio(u8 width, const volatile void __iomem *addr);
void __log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr);

#define log_write_mmio(val, width, addr)		\
do {							\
	if (tracepoint_enabled(rwmmio_write))		\
		__log_write_mmio(val, width, addr);	\
} while (0)

#define log_read_mmio(width, addr)			\
do {							\
	if (tracepoint_enabled(rwmmio_read))		\
		__log_read_mmio(width, addr);		\
} while (0)

#define log_post_read_mmio(val, width, addr)		\
do {							\
	if (tracepoint_enabled(rwmmio_post_read))	\
		__log_post_read_mmio(val, width, addr);	\
} while (0)

#else
static inline void log_write_mmio(u64 val, u8 width, volatile void __iomem *addr)
{ }
static inline void log_read_mmio(u8 width, const volatile void __iomem *addr)
{ }
static inline void log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr)
{ }
#endif /* CONFIG_TRACE_MMIO_ACCESS */

#endif /* __LOG_MMIORW_H__  */
