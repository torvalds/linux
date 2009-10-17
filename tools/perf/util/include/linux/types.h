#ifndef _PERF_LINUX_TYPES_H_
#define _PERF_LINUX_TYPES_H_

#define DECLARE_BITMAP(name,bits) \
	unsigned long name[BITS_TO_LONGS(bits)]

#endif
