/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FTRACE_IRQ_H
#define _LINUX_FTRACE_IRQ_H

#ifdef CONFIG_HWLAT_TRACER
extern bool trace_hwlat_callback_enabled;
extern void trace_hwlat_callback(bool enter);
#endif

#ifdef CONFIG_OSNOISE_TRACER
extern bool trace_osnoise_callback_enabled;
extern void trace_osnoise_callback(bool enter);
#endif

static inline void ftrace_nmi_enter(void)
{
#ifdef CONFIG_HWLAT_TRACER
	if (trace_hwlat_callback_enabled)
		trace_hwlat_callback(true);
#endif
#ifdef CONFIG_OSNOISE_TRACER
	if (trace_osnoise_callback_enabled)
		trace_osnoise_callback(true);
#endif
}

static inline void ftrace_nmi_exit(void)
{
#ifdef CONFIG_HWLAT_TRACER
	if (trace_hwlat_callback_enabled)
		trace_hwlat_callback(false);
#endif
#ifdef CONFIG_OSNOISE_TRACER
	if (trace_osnoise_callback_enabled)
		trace_osnoise_callback(false);
#endif
}

#endif /* _LINUX_FTRACE_IRQ_H */
