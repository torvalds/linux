#ifndef RESUME_TRACE_H
#define RESUME_TRACE_H

#ifdef CONFIG_PM_TRACE
#include <asm/resume-trace.h>

extern int pm_trace_enabled;

struct device;
extern void set_trace_device(struct device *);
extern void generate_resume_trace(void *tracedata, unsigned int user);

#define TRACE_DEVICE(dev) do { \
	if (pm_trace_enabled) \
		set_trace_device(dev); \
	} while(0)

#else

#define TRACE_DEVICE(dev) do { } while (0)
#define TRACE_RESUME(dev) do { } while (0)

#endif

#endif
