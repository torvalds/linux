/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PM_TRACE_H
#define PM_TRACE_H

#include <linux/types.h>
#ifdef CONFIG_PM_TRACE
#include <asm/pm-trace.h>

extern int pm_trace_enabled;
extern bool pm_trace_rtc_abused;

static inline bool pm_trace_rtc_valid(void)
{
	return !pm_trace_rtc_abused;
}

static inline int pm_trace_is_enabled(void)
{
       return pm_trace_enabled;
}

struct device;
extern void set_trace_device(struct device *);
extern void generate_pm_trace(const void *tracedata, unsigned int user);
extern int show_trace_dev_match(char *buf, size_t size);

#define TRACE_DEVICE(dev) do { \
	if (pm_trace_enabled) \
		set_trace_device(dev); \
	} while(0)

#else

static inline bool pm_trace_rtc_valid(void) { return true; }
static inline int pm_trace_is_enabled(void) { return 0; }

#define TRACE_DEVICE(dev) do { } while (0)
#define TRACE_RESUME(dev) do { } while (0)
#define TRACE_SUSPEND(dev) do { } while (0)

#endif

#endif
