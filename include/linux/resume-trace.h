#ifndef RESUME_TRACE_H
#define RESUME_TRACE_H

#ifdef CONFIG_PM_TRACE

struct device;
extern void set_trace_device(struct device *);
extern void generate_resume_trace(void *tracedata, unsigned int user);

#define TRACE_DEVICE(dev) set_trace_device(dev)
#define TRACE_RESUME(user) do {				\
	void *tracedata;				\
	asm volatile("movl $1f,%0\n"			\
		".section .tracedata,\"a\"\n"		\
		"1:\t.word %c1\n"			\
		"\t.long %c2\n"				\
		".previous"				\
		:"=r" (tracedata)			\
		: "i" (__LINE__), "i" (__FILE__));	\
	generate_resume_trace(tracedata, user);		\
} while (0)

#else

#define TRACE_DEVICE(dev) do { } while (0)
#define TRACE_RESUME(dev) do { } while (0)

#endif

#endif
